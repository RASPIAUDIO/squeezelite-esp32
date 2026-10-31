/* SPDX-License-Identifier: MIT
 * S31 adapter: legacy 16-bit effects library, floating-point 32-bit EQ.
 * Ten octave peaking bands; this is not bit-identical to the Xtensa-only EQ.
 */
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "esp_equalizer.h"
#include "esp_ae_eq.h"
#include "esp_log.h"

enum { BANDS = 10, CHANNELS = 2, CHUNK = 128 };
static const uint32_t centers[BANDS] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
/* Trapezoidal state-variable bell, mathematically equivalent to an RBJ
 * peaking biquad. Compute the high-pass first to avoid near-unit coefficient
 * cancellation at low frequencies. Equations: Andrew Simper, Cytomic,
 * https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
 * Float PCM retains the 24 significant source bits; there is no s16 stage.
 */
typedef struct { float g, k, a1, mix, s1, s2; } bell_t;
typedef struct {
    int rate, channels, bits;
    esp_ae_eq_handle_t handle[CHANNELS];
    esp_ae_eq_filter_para_t bands[CHANNELS][BANDS];
    bell_t bell[CHANNELS][BANDS];
    int16_t scratch[CHUNK];
} eq_t;

void esp_equalizer_uninit(void *handle)
{
    eq_t *eq = handle;
    if (!eq) return;
    for (int c = 0; c < eq->channels; ++c)
        if (eq->handle[c]) esp_ae_eq_close(eq->handle[c]);
    free(eq);
}

void *esp_equalizer_init(int channels, int rate, int bands, int xmms)
{
    return esp_equalizer_init_bits(channels, rate, bands, xmms, 16);
}

void *esp_equalizer_init_bits(int channels, int rate, int bands, int xmms, int bits)
{
    if (channels < 1 || channels > CHANNELS || bands != BANDS ||
        rate < 8000 || rate > (bits == 32 ? 192000 : 96000) ||
        xmms || (bits != 16 && bits != 32)) return NULL;
    eq_t *eq = calloc(1, sizeof(*eq));
    if (!eq) return NULL;
    eq->rate = rate;
    eq->channels = channels;
    eq->bits = bits;
    for (int c = 0; c < channels; ++c) {
        for (int b = 0; b < BANDS; ++b) {
            eq->bands[c][b] = (esp_ae_eq_filter_para_t) {
                .filter_type = ESP_AE_EQ_FILTER_PEAK,
                .fc = centers[b] < rate / 2 ? centers[b] : rate / 4,
                .q = 1.41421356f, .gain = 0,
            };
        }
        if (bits == 32) continue;
        esp_ae_eq_cfg_t cfg = {.sample_rate = rate, .channel = 1,
            .bits_per_sample = bits, .filter_num = BANDS, .para = eq->bands[c]};
        if (esp_ae_eq_open(&cfg, &eq->handle[c]) != ESP_AE_ERR_OK) {
            esp_equalizer_uninit(eq);
            return NULL;
        }
        /* A zero-gain peaking section should be an exact bypass. Leaving all
         * ten sections enabled in effects 1.4.3 produces a persistent DC
         * residue on S31, even with 32-bit samples. Keep neutral bands out
         * of the processing chain instead of relying on coefficient cancellation. */
        for (int b = 0; b < BANDS; ++b) {
            if (esp_ae_eq_disable_filter(eq->handle[c], b) != ESP_AE_ERR_OK) {
                ESP_LOGE("S31 EQ", "Failed to disable neutral band %d channel %d", b, c);
                esp_equalizer_uninit(eq);
                return NULL;
            }
        }
    }
    return eq;
}

void esp_equalizer_set_band_value(void *handle, float gain, int band, int channel)
{
    eq_t *eq = handle;
    if (!eq || band < 0 || band >= BANDS || channel < 0 || channel >= eq->channels || !isfinite(gain)) return;
    /* Frequencies outside Nyquist cannot be represented at this rate. */
    if (centers[band] >= eq->rate / 2) gain = 0;
    if (gain < -15 || gain > 15) {
        ESP_LOGW("S31 EQ", "Band gain %.1f dB limited to the effects API range +/-15 dB", gain);
        gain = fmaxf(-15, fminf(15, gain));
    }
    esp_ae_eq_filter_para_t param = eq->bands[channel][band];
    if (param.gain == gain) return;
    param.gain = gain;
    if (eq->bits == 32) {
        bell_t *f = &eq->bell[channel][band];
        const double a = pow(10.0, gain / 40.0);
        const double g = tan(M_PI * param.fc / eq->rate);
        const double k = 1.0 / (sqrt(2.0) * a);
        f->g = g;
        f->k = k;
        f->a1 = 1.0 / (1.0 + g * (g + k));
        f->mix = k * (a * a - 1.0);
        if (gain == 0) f->s1 = f->s2 = 0;
        eq->bands[channel][band] = param;
        return;
    }
    esp_ae_err_t err = esp_ae_eq_set_filter_para(eq->handle[channel], band, &param);
    if (err == ESP_AE_ERR_OK) {
        err = gain == 0 ? esp_ae_eq_disable_filter(eq->handle[channel], band) :
                          esp_ae_eq_enable_filter(eq->handle[channel], band);
    }
    if (err == ESP_AE_ERR_OK)
        eq->bands[channel][band] = param;
    else ESP_LOGE("S31 EQ", "Failed to set band %d channel %d", band, channel);
}

int esp_equalizer_process(void *handle, unsigned char *buf, int length, int rate, int channels)
{
    eq_t *eq = handle;
    if (!eq || !buf || length < 0 || rate != eq->rate || channels != eq->channels) return 0;
    const int sample_bytes = eq->bits / 8;
    if (length % (sample_bytes * channels)) return 0;
    int frames = length / (sample_bytes * channels);
    if (eq->bits == 32) {
        int32_t *samples = (int32_t *)buf;
        for (int c = 0; c < channels; ++c) {
            int active[BANDS], count = 0;
            for (int b = 0; b < BANDS; ++b)
                if (eq->bands[c][b].gain != 0) active[count++] = b;
            if (!count) continue; /* Exact integer bypass, including all 32 bits. */
            for (int i = 0; i < frames; ++i) {
                float value = samples[i*channels+c] * (1.0f / 2147483648.0f);
                for (int n = 0; n < count; ++n) {
                    bell_t *f = &eq->bell[c][active[n]];
                    float hp = (value - (f->k + f->g)*f->s1 - f->s2)*f->a1;
                    float bp = f->g*hp + f->s1;
                    f->s1 += 2.0f*f->g*hp;
                    f->s2 += 2.0f*f->g*bp;
                    value += f->mix*bp;
                }
                /* Saturate once after the cascade; never wrap an overloaded
                 * positive signal into a negative integer. Boosts still need
                 * source/volume headroom, as with the legacy EQ. */
                samples[i*channels+c] = value >= 1.0f ? INT32_MAX :
                    value <= -1.0f ? INT32_MIN : (int32_t)(value * 2147483648.0f);
            }
        }
        return length;
    }
    for (int offset = 0; offset < frames; offset += CHUNK) {
        int count = frames - offset < CHUNK ? frames - offset : CHUNK;
        for (int c = 0; c < channels; ++c) {
            for (int i = 0; i < count; ++i)
                eq->scratch[i] = ((int16_t *)buf)[(offset+i)*channels+c];
            if (esp_ae_eq_process(eq->handle[c], count, eq->scratch, eq->scratch) != ESP_AE_ERR_OK) {
                ESP_LOGE("S31 EQ", "Processing failed");
                return offset * channels * sample_bytes;
            }
            for (int i = 0; i < count; ++i)
                ((int16_t *)buf)[(offset+i)*channels+c] = eq->scratch[i];
        }
    }
    return length;
}

int esp_equalizer_get_band_count(void *handle) { return handle ? BANDS : 0; }
float esp_equalizer_get_band_value(void *handle, int band, int channel)
{
    eq_t *eq = handle;
    if (!eq || band < 0 || band >= BANDS || channel < 0 || channel >= eq->channels) return 0;
    return eq->bands[channel][band].gain;
}
