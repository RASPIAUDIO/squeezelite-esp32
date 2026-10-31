/* S31 external A2DP decoder. AAC/SBC PCM16 and LDAC PCM32 feed the DSP sink.
 * Codec setup follows ESP-IDF 6.1 a2dp_sink_stream_aac (CC0/Unlicense).
 * A mutex serializes decoder lifetime/output with stop/reconfiguration; a
 * generation on queued packets prevents an old session reaching a new one.
 */
#include "bt_sink_codec.h"
#if CONFIG_IDF_TARGET_ESP32S31 && CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_reg.h"
#include "esp_aac_dec.h"
#include "esp_sbc_dec.h"
#include "platform_config.h"
#include "ldac/ldac_rx.h"

#define QUEUE_DEPTH 32
#define PCM_BYTES 8192
static const char *TAG = "BT_CODEC";
typedef struct { esp_a2d_audio_buff_t *buffer; uint32_t generation; } packet_t;
static QueueHandle_t queue;
static SemaphoreHandle_t mutex;
static esp_audio_dec_handle_t decoder;
static ldac_rx_t *ldac_decoder;
static bt_data_cb_t output_cb, output32_cb;
static esp_a2d_mcc_t config;
static bool configured;
static _Atomic bool playing;
static _Atomic uint32_t generation, dropped;
static uint32_t packets, frames, errors;
static int rate, channels;
static unsigned endpoint_count;
static uint8_t *pcm;

static const char *codec_name(void) {
    return config.type == ESP_A2D_MCT_M24 ? "AAC" :
           config.type == ESP_A2D_MCT_NON_A2DP ? "LDAC" : "SBC";
}

static void close_locked(void) {
    if (decoder) esp_audio_dec_close(decoder);
    decoder = NULL;
    free(ldac_decoder);
    ldac_decoder = NULL;
}

static void flush_locked(void) {
    playing = false;
    ++generation;
    packet_t packet;
    while (xQueueReceive(queue, &packet, 0) == pdTRUE)
        esp_a2d_audio_buff_free(packet.buffer);
    close_locked();
}

static bool open_locked(void) {
    if (config.type == ESP_A2D_MCT_NON_A2DP) {
        if (!output32_cb) return false;
        ldac_decoder = malloc(sizeof(*ldac_decoder));
        if (!ldac_decoder || !ldac_rx_init(ldac_decoder, rate, config.cie.ldac_info[7])) {
            free(ldac_decoder);
            ldac_decoder = NULL;
            return false;
        }
        return true;
    }
    esp_audio_dec_cfg_t cfg = {0};
    esp_sbc_dec_cfg_t sbc = ESP_SBC_DEC_CONFIG_DEFAULT();
    esp_aac_dec_cfg_t aac = ESP_AAC_DEC_CONFIG_DEFAULT();
    if (config.type == ESP_A2D_MCT_SBC) {
        sbc.ch_num = channels;
        sbc.sbc_mode = ESP_SBC_MODE_STD;
        sbc.enable_plc = true;
        cfg.type = ESP_AUDIO_TYPE_SBC;
        cfg.cfg = &sbc;
        cfg.cfg_sz = sizeof(sbc);
    } else if (config.type == ESP_A2D_MCT_M24) {
        aac.sample_rate = rate;
        aac.channel = channels;
        aac.bits_per_sample = ESP_AUDIO_BIT16;
        aac.no_adts_header = true;
        aac.aac_plus_enable = false; /* only LC advertised */
        cfg.type = ESP_AUDIO_TYPE_AAC;
        cfg.cfg = &aac;
        cfg.cfg_sz = sizeof(aac);
    } else return false;
    esp_audio_err_t result = esp_audio_dec_open(&cfg, &decoder);
    if (result != ESP_AUDIO_ERR_OK) {
        decoder = NULL;
        ESP_LOGE(TAG, "decoder open failed: %d", result);
        return false;
    }
    return true;
}

static void ldac_output(void *unused, const int32_t *samples, size_t count) {
    if (!frames) ESP_LOGI(TAG, "Decoded LDAC PCM32: %d Hz %d source channels", rate, channels);
    output32_cb((const uint8_t *)samples, count * 8);
    frames += count;
}

static void decode_task(void *unused) {
    packet_t packet;
    for (;;) {
        if (xQueueReceive(queue, &packet, portMAX_DELAY) != pdTRUE) continue;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (playing && (decoder || ldac_decoder) && packet.generation == generation) {
            esp_audio_dec_in_raw_t in = {.buffer = packet.buffer->data, .len = packet.buffer->data_len};
            ++packets;
            if (ldac_decoder) {
                int result = ldac_rx_process(ldac_decoder, in.buffer, in.len,
                    packet.buffer->number_frame, packet.buffer->timestamp, ldac_output, NULL);
                if (result < 0 && (++errors <= 5 || errors % 100 == 0))
                    ESP_LOGW(TAG, "LDAC rejected packet: errors=%"PRIu32" gaps=%"PRIu32,
                             errors, ldac_decoder->gaps);
            }
            while (decoder && in.len > 0) {
                esp_audio_dec_out_frame_t out = {.buffer = pcm, .len = PCM_BYTES / 2};
                in.consumed = 0;
                esp_audio_err_t err = esp_audio_dec_process(decoder, &in, &out);
                if (err != ESP_AUDIO_ERR_OK || in.consumed <= 0 || in.consumed > in.len ||
                    out.decoded_size > PCM_BYTES / 2 || out.decoded_size % (channels * 2)) {
                    if (++errors <= 5 || errors % 100 == 0)
                        ESP_LOGW(TAG, "decode error=%d consumed=%d/%d output=%d errors=%"PRIu32,
                                 err, in.consumed, in.len, out.decoded_size, errors);
                    break;
                }
                if (out.decoded_size) {
                    if (!frames) {
                        esp_audio_dec_info_t info = {0};
                        if (esp_audio_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK ||
                            info.bits_per_sample != 16 || info.channel != channels || info.sample_rate != rate) {
                            ++errors;
                            playing = false;
                            ESP_LOGE(TAG, "Unexpected decoded PCM: %"PRIu32" Hz %u bit %u channels",
                                     info.sample_rate, info.bits_per_sample, info.channel);
                            break;
                        }
                        ESP_LOGI(TAG, "Decoded PCM verified: %"PRIu32" Hz %u bit %u channels",
                                 info.sample_rate, info.bits_per_sample, info.channel);
                    }
                    /* The shared player expects interleaved stereo PCM16. */
                    if (channels == 1) {
                        int16_t *samples = (int16_t *)pcm;
                        for (int i = out.decoded_size / 2 - 1; i >= 0; --i)
                            samples[2*i] = samples[2*i+1] = samples[i];
                        out.decoded_size *= 2;
                    }
                    output_cb(pcm, out.decoded_size);
                    frames += out.decoded_size / 4;
                }
                in.buffer += in.consumed;
                in.len -= in.consumed;
            }
            if (packets == 1 || packets % 500 == 0)
                ESP_LOGI(TAG, "%s packets=%"PRIu32" pcm_frames=%"PRIu32" errors=%"PRIu32" drops=%"PRIu32,
                         codec_name(), packets, frames, errors, dropped);
        }
        xSemaphoreGive(mutex);
        esp_a2d_audio_buff_free(packet.buffer);
    }
}

esp_err_t bt_sink_codec_init(bt_data_cb_t output, bt_data_cb_t output32) {
    if (mutex) return ESP_OK; /* service lifetime matches BT host lifetime */
    output_cb = output;
    output32_cb = output32;
    mutex = xSemaphoreCreateMutex();
    queue = xQueueCreate(QUEUE_DEPTH, sizeof(packet_t));
    pcm = malloc(PCM_BYTES);
    if (!mutex || !queue || !pcm) goto fail;
    if (esp_sbc_dec_register() != ESP_AUDIO_ERR_OK || esp_aac_dec_register() != ESP_AUDIO_ERR_OK) goto fail;
    if (xTaskCreate(decode_task, "BTDecode", 10240, NULL, 5, NULL) != pdPASS) goto fail;
    return ESP_OK;
fail:
    if (queue) vQueueDelete(queue);
    if (mutex) vSemaphoreDelete(mutex);
    free(pcm); pcm = NULL; mutex = NULL; queue = NULL;
    return ESP_ERR_NO_MEM;
}

int bt_sink_codec_configure(const esp_a2d_mcc_t *mcc) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    flush_locked();
    configured = false;
    rate = 0;
    channels = 2;
    config = *mcc;
    if (mcc->type == ESP_A2D_MCT_SBC) {
        const esp_a2d_cie_sbc_t *s = &mcc->cie.sbc_info;
        if (s->samp_freq & ESP_A2D_SBC_CIE_SF_48K) rate = 48000;
        else if (s->samp_freq & ESP_A2D_SBC_CIE_SF_44K) rate = 44100;
        else if (s->samp_freq & ESP_A2D_SBC_CIE_SF_32K) rate = 32000;
        else if (s->samp_freq & ESP_A2D_SBC_CIE_SF_16K) rate = 16000;
        if (s->ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) channels = 1;
    } else if (mcc->type == ESP_A2D_MCT_M24) {
        const esp_a2d_cie_m24_t *a = &mcc->cie.m24_info;
        ESP_LOGI(TAG, "AAC configuration object=0x%x vbr=%u bitrate=%u",
                 a->obj_type, a->vbr, (unsigned)((a->br1 << 16) | (a->br2 << 8) | a->br3));
        if (a->samp_freq2 & ESP_A2D_M24_CIE_SF2_48K) rate = 48000;
        else if (a->samp_freq1 & ESP_A2D_M24_CIE_SF1_44K) rate = 44100;
        if (a->ch & ESP_A2D_M24_CIE_CH_1) channels = 1;
        if (!(a->obj_type & (ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC | ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC))) rate = 0;
    } else if (mcc->type == ESP_A2D_MCT_NON_A2DP) {
        const uint8_t *l = mcc->cie.ldac_info;
        static const uint8_t identity[6] = {0x2d, 1, 0, 0, 0xaa, 0};
        if (!memcmp(l, identity, 6) && (l[7] == 1 || l[7] == 2 || l[7] == 4)) {
            rate = l[6] == 0x20 ? 44100 : l[6] == 0x10 ? 48000 :
                   l[6] == 0x08 ? 88200 : l[6] == 0x04 ? 96000 : 0;
            channels = l[7] == 4 ? 1 : 2;
        }
    }
    configured = rate != 0;
    ESP_LOGI(TAG, "negotiated %s rate=%d channels=%d valid=%d",
             codec_name(), rate, channels, configured);
    xSemaphoreGive(mutex);
    return rate;
}

bool bt_sink_codec_start(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    flush_locked();
    packets = frames = errors = dropped = 0;
    playing = configured && open_locked();
    bool ok = playing;
    xSemaphoreGive(mutex);
    return ok;
}

void bt_sink_codec_stop(bool forget_config) {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    flush_locked();
    if (forget_config) configured = false;
    ESP_LOGI(TAG, "stopped packets=%"PRIu32" pcm_frames=%"PRIu32" errors=%"PRIu32" drops=%"PRIu32,
             packets, frames, errors, dropped);
    xSemaphoreGive(mutex);
}

void bt_sink_codec_receive(esp_a2d_conn_hdl_t connection, esp_a2d_audio_buff_t *buffer) {
    if (!buffer) return;
    /* Never wait for decoding/output in the Bluedroid callback. */
    bool queued = false;
    if (buffer->data && buffer->data_len && queue) {
        uint32_t epoch = generation;
        if (playing) {
            packet_t packet = {buffer, epoch};
            queued = xQueueSend(queue, &packet, 0) == pdTRUE;
            if (!queued) ++dropped;
        }
    }
    if (!queued) esp_a2d_audio_buff_free(buffer);
}

esp_err_t bt_sink_codec_register_endpoints(void) {
    /* Diagnostic/compatibility overrides; auto offers LDAC, AAC, then SBC. */
    char *mode = config_alloc_get_default(NVS_TYPE_STR, "bt_codec", "auto", 0);
    bool sbc_only = mode && strcmp(mode, "sbc") == 0;
    bool offer_ldac = output32_cb && !sbc_only && (!mode || strcmp(mode, "aac") != 0);
    free(mode);
    char *rate_mode = config_alloc_get_default(NVS_TYPE_STR, "bt_aac_rate", "auto", 0);
    bool aac_44_only = rate_mode && strcmp(rate_mode, "44100") == 0;
    free(rate_mode);
    endpoint_count = sbc_only ? 1 : offer_ldac ? 3 : 2;
    ESP_LOGI(TAG, "Registering %s; maximum SEPs=%d", sbc_only ? "SBC" : offer_ldac ? "LDAC/AAC/SBC" : "AAC/SBC", ESP_A2D_MAX_SEPS);
    if (endpoint_count > ESP_A2D_MAX_SEPS) return ESP_ERR_INVALID_SIZE;
    unsigned endpoint = 0;
    if (offer_ldac) {
        esp_a2d_mcc_t ldac = {.type = ESP_A2D_MCT_NON_A2DP};
        const uint8_t capability[8] = {0x2d, 1, 0, 0, 0xaa, 0, 0x3c, 7};
        memcpy(ldac.cie.ldac_info, capability, sizeof(capability));
        esp_err_t err = esp_a2d_sink_register_stream_endpoint(endpoint++, &ldac);
        ESP_LOGI(TAG, "LDAC endpoint request: %s", esp_err_to_name(err));
        if (err != ESP_OK) return err;
    }
    esp_a2d_mcc_t aac = {.type = ESP_A2D_MCT_M24};
    aac.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC | ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
    aac.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_44K;
    aac.cie.m24_info.samp_freq2 = aac_44_only ? 0 : ESP_A2D_M24_CIE_SF2_48K;
    ESP_LOGI(TAG, "AAC rate capabilities: %s", aac_44_only ? "44100" : "44100/48000");
    aac.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_1 | ESP_A2D_M24_CIE_CH_2;
    aac.cie.m24_info.vbr = ESP_A2D_M24_CIE_VBR_SUPPORT;
    /* Advertise a usable AAC-LC ceiling instead of the 23-bit field maximum.
     * Some sources configure their encoder from this advertised value. */
    const unsigned aac_max_bitrate = 320000;
    aac.cie.m24_info.br1 = (aac_max_bitrate >> 16) & 0x7f;
    aac.cie.m24_info.br2 = (aac_max_bitrate >> 8) & 0xff;
    aac.cie.m24_info.br3 = aac_max_bitrate & 0xff;
    if (!sbc_only) {
        esp_err_t err = esp_a2d_sink_register_stream_endpoint(endpoint++, &aac);
        ESP_LOGI(TAG, "AAC endpoint request: %s", esp_err_to_name(err));
        if (err != ESP_OK) return err;
    }
    esp_a2d_mcc_t sbc = {.type = ESP_A2D_MCT_SBC};
    sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_16K | ESP_A2D_SBC_CIE_SF_32K | ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K;
    sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO | ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL | ESP_A2D_SBC_CIE_CH_MODE_STEREO | ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    sbc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 | ESP_A2D_SBC_CIE_BLOCK_LEN_8 | ESP_A2D_SBC_CIE_BLOCK_LEN_12 | ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    sbc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    sbc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR | ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    sbc.cie.sbc_info.min_bitpool = 2;
    sbc.cie.sbc_info.max_bitpool = 53;
    ESP_LOGI(TAG, "Offering %s", sbc_only ? "SBC only (bt_codec=sbc)" : offer_ldac ? "LDAC + AAC-LC + SBC fallback" : "AAC-LC + SBC fallback");
    return esp_a2d_sink_register_stream_endpoint(endpoint, &sbc);
}

unsigned bt_sink_codec_endpoint_count(void) { return endpoint_count; }
#endif
