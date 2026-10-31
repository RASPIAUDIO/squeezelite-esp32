/* SPDX-License-Identifier: MIT
 * S31 adapter to Espressif's ES8311 driver, pinned to the FIP validation version.
 * I2S and amplifier GPIO remain owned by output_i2s; the shared I2C bus by services.
 */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "adac.h"
#include "i2c_bus.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "platform_config.h"

static const audio_codec_if_t *codec;
static const audio_codec_ctrl_if_t *control;
static const char *TAG = "DAC ES8311";
static bool mic_diagnostic;

static void deinit(void)
{
    if (codec) audio_codec_delete_codec_if(codec);
    if (control) audio_codec_delete_ctrl_if(control);
    codec = NULL;
    control = NULL;
}

static bool samplerate(uint32_t rate, uint8_t bits)
{
    if (!codec || rate > 96000) return false;
    esp_codec_dev_sample_info_t format = {
        .sample_rate = rate, .bits_per_sample = bits,
        /* The ES8311 coefficient table requires 128x at 88.2 kHz. */
        .channel = 2, .mclk_multiple = rate > 48000 ? 128 : 256,
    };
    int err = codec->hw_base.set_fs(&codec->hw_base, &format, ESP_CODEC_DEV_TYPE_OUT);
    if (err) ESP_LOGE(TAG, "Format %lu Hz/%u bits failed: %d", (unsigned long)rate, bits, err);
    return err == ESP_CODEC_DEV_OK;
}

static bool init(char *config, int port, i2s_config_param_t *i2s, bool *mck)
{
    *mck = true;
    char *diag = config_alloc_get_default(NVS_TYPE_STR, "audio_wire_diag", "n", 0);
    mic_diagnostic = diag && (!strcasecmp(diag, "y") || !strcmp(diag, "1"));
    free(diag);
    int address = adac_init(config, port);
    if (!i2c_bus_is_initialized() || address != 0x18) return false;
    audio_codec_i2c_cfg_t i2c = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_get_handle(), .clock_speed_hz = 400000,
    };
    control = audio_codec_new_i2c_ctrl(&i2c);
    if (!control) return false;
    es8311_codec_cfg_t cfg = {
        .ctrl_if = control,
        .sys_cfg = {.is_master = false, .no_mclk = false},
        .dac_cfg = {.ref_enable = true},
        /* Match the pinned Function-CoreBoard-1 board definition: the
         * NS4150B adds 6 dB. The codec driver compensates this gain when
         * converting the requested 0 dB output level to its DAC register.
         * GPIO57 remains controlled by output_i2s, hence pa_pin = -1. */
        .pa_cfg = {.pa_pin = -1, .hw_gain = {.pa_gain = 6.0f}},
    };
    codec = es8311_codec_new(&cfg);
    if (!codec || !samplerate(i2s->sample_rate, i2s->bits_per_sample)) {
        deinit();
        return false;
    }
    int err = codec->dac_if->ops.set_vol(codec, 3, 0.0f);
    if (err) {
        ESP_LOGE(TAG, "Setting DAC gain failed: %d", err);
        deinit();
    }
    return err == ESP_CODEC_DEV_OK;
}

static void power(adac_power_e mode)
{
    if (!codec) return;
    /* Diagnostic only: enable the onboard microphone alongside the DAC
     * reference, to distinguish digital transport from acoustic feedback. */
    if (mic_diagnostic && mode == ADAC_ON) {
        int mic_err = codec->adc_if->ops.enable(codec, true);
        mic_err |= codec->adc_if->ops.set_vol(codec, 1, 42.0f);
        ESP_LOGI(TAG, "Diagnostic onboard ADC gain 42 dB, result %d", mic_err);
    }
    int err = codec->dac_if->ops.enable(codec, mode == ADAC_ON);
    if (err) ESP_LOGE(TAG, "DAC power transition failed: %d", err);
    if (mic_diagnostic && mode != ADAC_ON)
        codec->adc_if->ops.enable(codec, false);
}

/* output_i2s controls PA57 and implements per-channel software volume. */
static void speaker(bool active) { (void)active; }
static void headset(bool active) { (void)active; }
static bool volume(unsigned left, unsigned right) { (void)left; (void)right; return false; }

const struct adac_s dac_es8311 = {
    .model = "ES8311", .init = init, .deinit = deinit,
    .power = power, .speaker = speaker, .headset = headset,
    .volume = volume, .samplerate = samplerate,
};
#endif
