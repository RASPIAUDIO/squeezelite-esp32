/* SPDX-License-Identifier: MIT
 * Diagnostic only, NVS audio_wire_diag=y. Receives physical SDOUT on GPIO54
 * using I2S1 as slave of existing BCLK53/WS55; never drives those clocks.
 * ES8311 ref_enable provides microphone ADC L + digital DAC reference R.
 * Only spectral summaries are logged; no microphone samples are saved.
 * The digital reference does not measure the analog DAC output.
 */
#include "sdkconfig.h"
#if CONFIG_MNCAST_S31_COREBOARD
#include "squeezelite.h"
#include "services.h"
#include "platform_config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdatomic.h>
#include "audio_wire_probe.h"

static const char *TAG = "WIRE_PROBE";
static i2s_chan_handle_t receiver;
static atomic_bool run_probe, task_live;
extern struct outputstate output;
extern struct buffer *outputbuf;

static void probe_task(void *arg) {
    int16_t *samples = arg;
    uint32_t last_log = 0;
    while (atomic_load(&run_probe)) {
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(receiver, samples, 4096 * 4, &bytes, 150);
        uint32_t now = gettime_ms();
        if (now - last_log < 250) continue;
        last_log = now;
        if (err != ESP_OK || bytes < 8) {
            ESP_LOGW(TAG, "RX status=%s bytes=%u", esp_err_to_name(err), (unsigned)bytes);
            continue;
        }
        unsigned frames = bytes / 4;
        uint32_t peak[2] = {0}, crossings[2] = {0}, first[2] = {0}, last[2] = {0};
        for (unsigned f = 0; f < frames; ++f) {
            for (unsigned c = 0; c < 2; ++c) {
                int32_t v = samples[2*f+c];
                uint32_t mag = v < 0 ? -v : v;
                if (mag > peak[c]) peak[c] = mag;
                if (f && samples[2*(f-1)+c] <= 0 && v > 0) {
                    if (!crossings[c]) first[c] = f;
                    last[c] = f;
                    ++crossings[c];
                }
            }
        }
        mutex_lock(outputbuf->mutex);
        unsigned logical_rate = output.current_sample_rate;
        mutex_unlock(outputbuf->mutex);
        uint32_t mhz[2] = {0};
        for (unsigned c = 0; c < 2; ++c)
            if (crossings[c] > 1)
                mhz[c] = (uint64_t)(crossings[c]-1) * logical_rate * 1000 / (last[c]-first[c]);
        ESP_LOGI(TAG, "SDOUT54 frames=%u peakL=%lu peakR=%lu HzL=%lu.%03lu HzR=%lu.%03lu logical_fs=%u",
            frames, (unsigned long)peak[0], (unsigned long)peak[1],
            (unsigned long)(mhz[0]/1000), (unsigned long)(mhz[0]%1000),
            (unsigned long)(mhz[1]/1000), (unsigned long)(mhz[1]%1000), logical_rate);
        if (logical_rate) {
            float level[2][4];
            for (unsigned c = 0; c < 2; ++c) {
                for (unsigned k = 0; k < 4; ++k) {
                    float coeff = 2 * cosf(2 * 3.14159265358979f * (440 + 110*k) / logical_rate);
                    float s1 = 0, s2 = 0;
                    for (unsigned f = 0; f < frames; ++f) {
                        float s = samples[2*f+c] / 32768.0f + coeff * s1 - s2;
                        s2 = s1; s1 = s;
                    }
                    float power = (s1*s1 + s2*s2 - coeff*s1*s2) * 4 / ((float)frames*frames);
                    level[c][k] = 10 * log10f(fmaxf(power, 1e-18f));
                }
            }
            ESP_LOGI(TAG, "GOERTZEL dBFS 440/550/660/770 micL=%.1f,%.1f,%.1f,%.1f refR=%.1f,%.1f,%.1f,%.1f",
                level[0][0],level[0][1],level[0][2],level[0][3],
                level[1][0],level[1][1],level[1][2],level[1][3]);
        }
    }
    free(samples);
    atomic_store(&task_live, false);
    vTaskDelete(NULL);
}

void audio_wire_probe_start(void) {
    char *value = config_alloc_get_default(NVS_TYPE_STR, "audio_wire_diag", "n", 0);
    bool enabled = value && (!strcasecmp(value, "y") || !strcmp(value, "1"));
    free(value);
    if (!enabled || receiver) return;
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
    cfg.dma_desc_num = 4;
    cfg.dma_frame_num = 512;
    esp_err_t err = i2s_new_channel(&cfg, NULL, &receiver);
    if (err != ESP_OK) goto failed;
    i2s_std_config_t format = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = 53, .ws = 55,
                     .dout = I2S_GPIO_UNUSED, .din = 54},
    };
    /* Separate slave receiver; use XTAL rather than RGMII's APLL. The actual
     * receive bit/frame clocks come from the TX0 GPIO pads. */
    format.clk_cfg.clk_src = I2S_CLK_SRC_XTAL;
    err = i2s_channel_init_std_mode(receiver, &format);
    if (err != ESP_OK) goto failed;
    err = i2s_channel_enable(receiver);
    if (err != ESP_OK) goto failed;
    atomic_store(&run_probe, true);
    atomic_store(&task_live, true);
    void *samples = heap_caps_malloc(4096 * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!samples || xTaskCreate(probe_task, "wire_probe", 6144, samples, 3, NULL) != pdPASS) {
        free(samples);
        atomic_store(&task_live, false);
        i2s_channel_disable(receiver);
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    ESP_LOGI(TAG, "Enabled I2S1 slave on BCLK53/WS55/SDOUT54; no analog validation");
    return;
failed:
    ESP_LOGE(TAG, "Cannot enable diagnostic: %s", esp_err_to_name(err));
    if (receiver) i2s_del_channel(receiver);
    receiver = NULL;
}

void audio_wire_probe_stop(void) {
    if (!receiver) return;
    atomic_store(&run_probe, false);
    while (atomic_load(&task_live)) vTaskDelay(pdMS_TO_TICKS(10));
    i2s_channel_disable(receiver);
    i2s_del_channel(receiver);
    receiver = NULL;
}
#endif
