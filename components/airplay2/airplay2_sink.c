#include "airplay2_sink.h"
#include "audio_receiver.h"
#include "audio_output.h"
#include "rtsp_events.h"
#include "rtsp_server.h"
#include "mdns_airplay.h"
#include "hap.h"
#include "ptp_clock.h"
#include "settings.h"
#include "dacp_client.h"
#include "raop_sink.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sodium.h"
#include "mdns.h"
#include "nvs.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mn_airplay2";
static mn_airplay2_output_t out;
static atomic_uint epoch;
static atomic_uint source_rate = 44100;
static atomic_uint output_rate;
static atomic_bool running;
static atomic_bool disconnect_requested;
static TaskHandle_t worker;
static bool initialized, started;
static char device_name[65];
static float volume_db = -15;
static atomic_uint write_shortfalls;

static const char *rng_name(void) { return "esp-hardware"; }
static void rng_buf(void * const buf, const size_t size) { esp_fill_random(buf, size); }
static randombytes_implementation hardware_rng = {
    .implementation_name = rng_name,
    .random = esp_random,
    .buf = rng_buf,
};

/* Configure before starting any consumer (including Sendspin/Noise). The
 * desktop libsodium default tries /dev/urandom, which does not exist on ESP.
 * Radio initialization precedes network pairing and supplies hardware entropy. */
void mn_airplay2_configure(const mn_airplay2_output_t *ops) {
    randombytes_set_implementation(&hardware_rng);
    out = *ops;
}
bool mn_airplay2_active(void) { return started; }

static void end_output(void) {
    uint32_t old = atomic_exchange(&epoch, 0);
    if (old && out.end) out.end(old);
}

static void receiver_event(rtsp_event_t event, const rtsp_event_data_t *data, void *arg) {
    (void)arg;
    uint32_t current = atomic_load(&epoch);
    switch (event) {
    case RTSP_EVENT_PLAYING:
        if (!current || !out.owns(current) || atomic_load(&source_rate) != atomic_load(&output_rate)) {
            uint32_t rate = atomic_load(&source_rate);
            current = out.begin(rate);
            atomic_store(&output_rate, rate);
            atomic_store(&epoch, current);
            if (current) out.volume(current, volume_db);
        } else if (out.pause) {
            out.pause(current, false);
        }
        break;
    case RTSP_EVENT_PAUSED:
        if (current && out.pause) out.pause(current, true);
        break;
    case RTSP_EVENT_DISCONNECTED:
        end_output();
        break;
    case RTSP_EVENT_METADATA:
        if (data && current) out.metadata(current, data->metadata.artist,
            data->metadata.album, data->metadata.title,
            data->metadata.position_secs, data->metadata.duration_secs);
        break;
    default: break;
    }
}

void mn_airplay2_artwork(const uint8_t *data, size_t size) {
    uint32_t current = atomic_load(&epoch);
    if (current && data && size) out.artwork(current, data, size);
}

static void playback_worker(void *arg) {
    (void)arg;
    /* The decoder splits AAC frames into AAC_FRAMES_PER_PACKET (352) chunks;
     * allow the timing servo one extra sample. */
    int16_t pcm[353*2];
    int64_t last_stats = 0;
    while (atomic_load(&running)) {
        if (atomic_exchange(&disconnect_requested, false)) {
            audio_receiver_set_playing(false);
            end_output();
            if (dacp_is_active()) dacp_send_playpause();
        }
        uint32_t current = atomic_load(&epoch);
        if (!current || !out.owns(current) || !audio_receiver_is_playing()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t frames = audio_receiver_read(pcm, 353);
        if (!frames) {
            memset(pcm, 0, 352*2*sizeof(int16_t));
            frames = 352;
        }
        size_t done = 0;
        while (done < frames && atomic_load(&running) && atomic_load(&epoch) == current &&
               out.owns(current) && audio_receiver_is_playing()) {
            size_t n = out.write(current, pcm + done*2, frames-done, 25);
            done += n;
            if (!n) vTaskDelay(1);
        }
        if (done != frames) atomic_fetch_add(&write_shortfalls, 1);
        int64_t now = esp_timer_get_time();
        if (now-last_stats > 10000000) {
            audio_stats_t st;
            audio_receiver_get_stats(&st);
            ESP_LOGI(TAG, "stats received=%lu decoded=%lu dropped=%lu decrypt_errors=%lu underruns=%lu overruns=%lu late=%lu write_short=%u PTP=%d",
                st.packets_received, st.packets_decoded, st.packets_dropped, st.decrypt_errors,
                st.buffer_underruns, st.buffer_overruns, st.late_frames,
                atomic_load(&write_shortfalls), ptp_clock_is_locked());
            last_stats = now;
        }
    }
    worker = NULL;
    vTaskDelete(NULL);
}

bool mn_airplay2_start(const char *name) {
    if (started) return true;
    if (!out.begin) return false;
    snprintf(device_name, sizeof(device_name), "%s", name);
    if (!initialized) {
        nvs_handle_t saved;
        if (nvs_open("mn_airplay2", NVS_READONLY, &saved) == ESP_OK) {
            float db;
            size_t len = sizeof(db);
            if (nvs_get_blob(saved, "volume", &db, &len) == ESP_OK &&
                len == sizeof(db) && isfinite(db) && db >= -144 && db <= 0)
                volume_db = db;
            nvs_close(saved);
        }
        if (hap_init() != ESP_OK || audio_receiver_init() != ESP_OK) return false;
        if (rtsp_events_register(receiver_event, NULL) != 0) return false;
        dacp_init();
        initialized = true;
    }
    esp_err_t err = ptp_clock_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    atomic_store(&running, true);
    if (xTaskCreatePinnedToCore(playback_worker, "ap2_output", 6144, NULL,
            AUDIO_PLAYBACK_TASK_PRIORITY, &worker, 1) != pdPASS) {
        atomic_store(&running, false);
        ptp_clock_stop();
        return false;
    }
    if (rtsp_server_start() != ESP_OK || mdns_airplay_init() != ESP_OK) {
        mn_airplay2_stop();
        return false;
    }
    started = true;
    ESP_LOGI(TAG, "AirPlay 2 and legacy RAOP listening; name=%s", device_name);
    return true;
}

void mn_airplay2_stop(void) {
    mdns_service_remove("_airplay", "_tcp");
    mdns_service_remove("_raop", "_tcp");
    rtsp_server_stop();
    atomic_store(&running, false);
    for (int n=0; worker && n<100; n++) vTaskDelay(pdMS_TO_TICKS(10));
    audio_receiver_stop();
    end_output();
    ptp_clock_stop();
    started = false;
}

/* Called under the source lock by Squeezelite. Do not wait for RTSP tasks. */
void mn_airplay2_disconnect(void) { atomic_store(&disconnect_requested, true); }

void mn_airplay2_control(int event) {
    switch (event) {
    case RAOP_VOLUME_UP: dacp_send_volume_up(); break;
    case RAOP_VOLUME_DOWN: dacp_send_volume_down(); break;
    case RAOP_PREV: dacp_send_prev(); break;
    case RAOP_NEXT: dacp_send_next(); break;
    case RAOP_STOP: mn_airplay2_disconnect(); break;
    default: dacp_send_playpause(); break;
    }
}

void audio_output_flush(void) { uint32_t e=atomic_load(&epoch); if(e) out.flush(e); }
void audio_output_set_source_rate(int rate) { atomic_store(&source_rate, rate); }
uint32_t audio_output_get_hardware_latency_us(void) { return 30000; }
bool audio_output_get_pipeline_us(int64_t *now, uint32_t *delay) {
    return out.pipeline && out.pipeline(atomic_load(&epoch), now, delay);
}

/* Persist only this receiver's volume and HAP identity. Never touch Wi-Fi,
 * the DAC configuration, DSP or Sendspin credentials. */
esp_err_t settings_get_volume(float *db) { *db=volume_db; return ESP_OK; }
esp_err_t settings_set_volume(float db) {
    if (!isfinite(db)) return ESP_ERR_INVALID_ARG;
    volume_db = db;
    uint32_t e=atomic_load(&epoch);
    if (e) out.volume(e, db);
    return ESP_OK;
}
esp_err_t settings_persist_volume(void) {
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("mn_airplay2", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err=nvs_set_blob(nvs,"volume",&volume_db,sizeof(volume_db));
    if (err==ESP_OK) err=nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
esp_err_t settings_get_device_name(char *name, size_t size) {
    snprintf(name,size,"%s",device_name); return ESP_OK;
}
void wifi_get_mac_str(char *str, size_t size) {
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(str,size,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
