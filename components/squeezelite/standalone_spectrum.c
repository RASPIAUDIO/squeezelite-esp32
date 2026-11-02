/* Read-only, bounded PCM snapshot for the standalone display. No audio DSP. */
#include "squeezelite.h"
#include "display.h"
#include "esp_dsp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

#define N 128
static EXT_RAM_BSS_ATTR float window[N], samples[N*2], pcm[N*2], power[N/2];
static int64_t last_data;
static bool reported;

static bool get_spectrum(uint8_t *levels, unsigned bands) {
    if (!levels || !bands || !visu_export.buffer || pthread_mutex_trylock(&visu_export.mutex)) return false;
    bool running = visu_export.running && visu_export.buffer;
    unsigned rate = visu_export.rate, step = rate >= 176400 ? 4 : rate >= 88200 ? 2 : 1;
    int64_t now = esp_timer_get_time();
    if (!running || visu_export.level < N * step) {
        if (!running || now - last_data > 300000) memset(levels, 0, bands);
        pthread_mutex_unlock(&visu_export.mutex);
        return false;
    }
    /* 16-bit analysis precision only; original PCM32/24-bit output is untouched.
     * Average adjacent samples for high-rate display analysis. */
    const s16_t *p = (const s16_t *)visu_export.buffer + BYTES_PER_FRAME / 4 - 1;
    for (unsigned i = 0; i < N; i++) for (unsigned ch = 0; ch < 2; ch++) {
        float sum = 0;
        for (unsigned j = 0; j < step; j++)
            sum += p[(i*step+j)*BYTES_PER_FRAME/2 + ch*BYTES_PER_FRAME/4];
        pcm[2*i+ch] = sum / (32768.0f * step);
    }
    visu_export.level = 0;
    pthread_mutex_unlock(&visu_export.mutex);
    last_data = now;
    memset(power, 0, sizeof(power));
    /* Combine channel POWERS, not samples: opposite-phase stereo stays visible. */
    for (unsigned ch = 0; ch < 2; ch++) {
        for (unsigned i = 0; i < N; i++) {
            samples[2*i] = pcm[2*i+ch] * window[i]; samples[2*i+1] = 0;
        }
        dsps_fft2r_fc32(samples, N);
        dsps_bit_rev_fc32_ansi(samples, N);
        for (unsigned k = 1; k < N/2; k++)
            power[k] += (samples[2*k]*samples[2*k] + samples[2*k+1]*samples[2*k+1]) * (8.0f/(N*N));
    }
    float effective_rate = (float)rate / step;
    float upper = fminf(20000, effective_rate * .49f);
    for (unsigned i = 0; i < bands; i++) {
        float lo = i ? 250 * powf(upper / 250, (float)i / bands) : 0;
        float hi = 250 * powf(upper / 250, (float)(i+1) / bands);
        unsigned a = fmaxf(1, floorf(lo*N/effective_rate));
        unsigned b = fminf(N/2-1, fmaxf(a, ceilf(hi*N/effective_rate)-1));
        float peak = 0;
        for (unsigned k = a; k <= b; k++) peak = fmaxf(peak, power[k]);
        float db = 10 * log10f(peak + 1e-12f);
        levels[i] = (uint8_t)fminf(100, fmaxf(0, (db + 60) * (100.0f/60)));
    }
    if (!reported) {
        for (unsigned i = 0; i < bands; i++) if (levels[i]) {
            ESP_LOGI("spectrum", "Live PCM analysis active: %u Hz, %u bands", rate, bands);
            reported = true;
            break;
        }
    }
    return true;
}
void standalone_spectrum_init(void) {
    /* sb_displayer_init has already initialized ESP-DSP's 128-point table. */
    dsps_wind_hann_f32(window, N);
    display_spectrum = get_spectrum;
}
