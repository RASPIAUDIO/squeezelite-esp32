// MnCast Sendspin adapter. MIT; sendspin-cpp retains its Apache-2.0 license.
#include "sendspin_sink.h"
#include "sendspin/client.h"
#include "sendspin/player_role.h"
#include "sendspin/metadata_role.h"
#include "sendspin/artwork_role.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_console.h"
#include "nvs.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <cstring>
#include "cJSON.h"
#include "sendspin/persistence_codec.h"

using namespace sendspin;
// Shared with the output path, protected by its mutex. Keep this definition
// outside decode_external: the recovery image supplies its own decode stubs.
uint32_t mn_sendspin_output_epoch = 0;
static const char *TAG = "mn_sendspin";
static mn_sendspin_output_t output;
static std::atomic<bool> stopping{false};
static std::atomic<bool> running{false};
static std::mutex feedback_mutex;
static PlayerRole *feedback_player = nullptr;
static uint32_t feedback_epoch = 0;
static std::mutex web_mutex;
static std::string web_snapshot="{}";
static std::string active_name="MnCast Sendspin";
static bool boot_enabled=false;
static std::atomic<bool> records_dirty{true};
static bool identity_error=false;
static std::string pairing_token;
static std::mutex pairing_mutex;

// Only emitted on explicit local-console request; never in periodic logs or mDNS.
static int token_command(int argc, char **argv) {
    std::lock_guard<std::mutex> lock(pairing_mutex);
    if(pairing_token.empty()) { printf("Sendspin not running\n"); return 1; }
    printf("SENDSPIN_PAIRING_TOKEN=%s\n",pairing_token.c_str());
    return 0;
}

// Configuration lives with the identity, but public settings never contain keys.
static std::string configured_name(nvs_handle_t h) {
    char name[64]; size_t len=sizeof(name);
    return nvs_get_str(h,"web_name",name,&len)==ESP_OK ? name : "MnCast Sendspin";
}
static bool load_web_settings(bool fallback) {
    std::lock_guard<std::mutex> lock(web_mutex);
    nvs_handle_t h;
    if(nvs_open("sendspin",NVS_READWRITE,&h)!=ESP_OK) return false;
    uint8_t enabled=fallback,forget=0;
    nvs_get_u8(h,"web_enabled",&enabled);
    nvs_get_u8(h,"web_forget",&forget);
    if(forget) {
        // Before client.start(): no live record store can rewrite revoked keys.
        // Keep device identity, volume and output delay. Rotate the setup token.
        bool ok=true;
        for(unsigned i=0;i<255;++i) {
            auto key=persistence_keys::record_slot_key(i);
            esp_err_t e=nvs_erase_key(h,key.c_str());
            ok &= e==ESP_OK || e==ESP_ERR_NVS_NOT_FOUND;
        }
        for(const char *key:{persistence_keys::RECORD_ORDER,persistence_keys::PAIRING_PSK,persistence_keys::LAST_PLAYED}) {
            esp_err_t e=nvs_erase_key(h,key); ok &= e==ESP_OK || e==ESP_ERR_NVS_NOT_FOUND;
        }
        if(ok && nvs_commit(h)==ESP_OK) {
            ok=nvs_set_u8(h,"web_forget",0)==ESP_OK && nvs_commit(h)==ESP_OK;
        } else ok=false;
        if(!ok) { identity_error=true; enabled=0; }
    }
    active_name=configured_name(h);
    nvs_close(h); boot_enabled=enabled!=0;
    return boot_enabled;
}
// Squeezelite pthread stacks live in PSRAM. NVS reads of existing values
// can enter flash/cache guards; execute provisioning on an internal task stack.
struct SettingsRequest { bool fallback; bool result; SemaphoreHandle_t done; };
static void settings_task(void *arg) {
    auto *request=static_cast<SettingsRequest *>(arg);
    bool fallback=request->fallback;
    bool result=load_web_settings(fallback);
    request->result=result;
    xSemaphoreGive(request->done);
    vTaskDelete(nullptr);
}
extern "C" bool mn_sendspin_enabled(bool fallback) {
    SettingsRequest request{fallback,false,xSemaphoreCreateBinary()};
    if(!request.done) return false;
    if(xTaskCreate(settings_task,"SsSettings",4096,&request,5,nullptr)!=pdPASS) {
        vSemaphoreDelete(request.done); return false;
    }
    xSemaphoreTake(request.done,portMAX_DELAY);
    vSemaphoreDelete(request.done);
    return request.result;
}
extern "C" bool mn_sendspin_web_config(bool enabled,const char *name,bool forget) {
    size_t len=name ? strlen(name) : 0;
    if(!len || len>63) return false;
    bool visible=false;
    for(size_t i=0;i<len;++i) { if((unsigned char)name[i]<32 || name[i]==127) return false; if(name[i]!=' ') visible=true; }
    if(!visible) return false;
    nvs_handle_t h;
    if(nvs_open("sendspin",NVS_READWRITE,&h)!=ESP_OK) return false;
    bool ok=nvs_set_str(h,"web_name",name)==ESP_OK && nvs_set_u8(h,"web_enabled",enabled)==ESP_OK;
    if(forget) ok=ok && nvs_set_u8(h,"web_forget",1)==ESP_OK;
    ok=ok && nvs_commit(h)==ESP_OK; nvs_close(h); return ok;
}
extern "C" char *mn_sendspin_web_token() {
    std::lock_guard<std::mutex> lock(pairing_mutex);
    return pairing_token.empty() ? nullptr : strdup(pairing_token.c_str());
}
extern "C" char *mn_sendspin_status_json() {
    std::lock_guard<std::mutex> lock(web_mutex);
    cJSON *j=cJSON_Parse(web_snapshot.c_str()); if(!j) j=cJSON_CreateObject();
    uint8_t enabled=boot_enabled,forget=0; std::string name=active_name; nvs_handle_t h;
    if(nvs_open("sendspin",NVS_READONLY,&h)==ESP_OK) {
        nvs_get_u8(h,"web_enabled",&enabled); nvs_get_u8(h,"web_forget",&forget);
        name=configured_name(h); nvs_close(h);
    }
    cJSON_AddBoolToObject(j,"supported",true);
    cJSON_AddBoolToObject(j,"enabled",enabled);
    cJSON_AddBoolToObject(j,"running",running.load());
    cJSON_AddBoolToObject(j,"identity_error",identity_error);
    cJSON_AddStringToObject(j,"name",name.c_str());
    cJSON_AddStringToObject(j,"active_name",active_name.c_str());
    cJSON_AddBoolToObject(j,"forget_pending",forget);
    cJSON_AddBoolToObject(j,"restart_required",forget || bool(enabled)!=boot_enabled || name!=active_name);
    char *json=cJSON_PrintUnformatted(j); cJSON_Delete(j); return json;
}

struct Network : SendspinNetworkProvider {
    bool is_network_ready() override {
        auto *n = esp_netif_get_default_netif();
        esp_netif_ip_info_t ip{};
        return n && esp_netif_is_netif_up(n) && esp_netif_get_ip_info(n, &ip)==ESP_OK && ip.ip.addr;
    }
};

struct Persistence : SendspinPersistenceProvider {
    nvs_handle_t handle = 0;
    bool open() { return nvs_open("sendspin", NVS_READWRITE, &handle)==ESP_OK; }
    ~Persistence() override { if (handle) nvs_close(handle); }
    std::optional<std::vector<uint8_t>> load_blob(const std::string &key) override {
        size_t size=0;
        if (nvs_get_blob(handle,key.c_str(),nullptr,&size)!=ESP_OK || size>8192) return std::nullopt;
        std::vector<uint8_t> bytes(size);
        if (nvs_get_blob(handle,key.c_str(),bytes.data(),&size)!=ESP_OK) return std::nullopt;
        return bytes;
    }
    bool save_blob(const std::string &key,const uint8_t *data,size_t len) override {
        bool ok=nvs_set_blob(handle,key.c_str(),data,len)==ESP_OK;
        if(ok && key.rfind("rec_",0)==0) records_dirty=true;
        return ok;
    }
    bool commit() override { return nvs_commit(handle)==ESP_OK; }
};

struct Listener : PlayerRoleListener, MetadataRoleListener, ArtworkRoleListener, SendspinClientListener {
    PlayerRole *player=nullptr;
    SendspinClient *client=nullptr;
    std::atomic<bool> active{false};
    std::atomic<uint32_t> epoch{0};
    ServerMetadataStateObject metadata;
    MetadataRole *metadata_role=nullptr;
    int64_t next_progress=0;
    void present_progress() {
        if (!active.load() || !metadata_role || !output.progress) return;
        int64_t now=client->now_us();
        if (now<next_progress) return;
        next_progress=now+250000;
        // MA sends advancing progress for live radio, but no finite duration.
        // Without a finite duration, present LIVE instead of time since tuning in.
        bool known=metadata.progress.has_value() && metadata.progress->track_duration>0;
        // The library uses the synchronized server timestamp and playback speed.
        // Poll even without new metadata: it interpolates playback, freezes on
        // pause and replaces the anchor on seek. Never run a second local clock.
        output.progress(epoch.load(),known,known ? metadata_role->get_track_progress_ms() : 0);
    }
    bool controls_dirty=false;
    bool paired_connection=false;
    void on_trust_changed(ConnectionTrust trust) override { paired_connection=trust==ConnectionTrust::USER; }
    void on_pairing_succeeded(const std::string &) override { ESP_LOGI(TAG,"Pairing saved"); }
    int64_t controls_changed_at=0;
    int64_t clock_anchor_server=0;
    int64_t previous_clock_anchor=0;
    wifi_ps_type_t previous_wifi_ps=WIFI_PS_NONE;
    bool restore_wifi_ps=false;
    ArtworkRole *artwork=nullptr;
    std::mutex image_mutex;
    void *pending_image=nullptr;
    unsigned image_generation=0;
    bool image_due=false;
    uint32_t image_epoch=0;
    int64_t image_deadline=0;
    ~Listener() override { output.image_free(pending_image); }
    void on_image_decode(uint8_t slot, const uint8_t *data, size_t length, SendspinImageFormat format) override {
        if (slot) return;
        unsigned generation;
        { std::lock_guard<std::mutex> lock(image_mutex); generation=image_generation; }
        void *image=format==SendspinImageFormat::JPEG ? output.image_decode(data,length) : nullptr;
        std::lock_guard<std::mutex> lock(image_mutex);
        if (generation!=image_generation) { output.image_free(image); return; }
        output.image_free(pending_image);
        pending_image=image;
        ESP_LOGI(TAG,"Artwork JPEG %u bytes: %s",(unsigned)length,image ? "decoded" : "rejected");
    }
    void on_image_display(uint8_t slot, uint32_t lateness_ms) override {
        if (slot) return;
        {
            std::lock_guard<std::mutex> lock(image_mutex);
            image_due=true;
            image_epoch=epoch.load();
            image_deadline=client->now_us()+2000000;
        }
        present_image();
    }
    void on_image_clear(uint8_t slot) override {
        if (slot) return;
        {
            std::lock_guard<std::mutex> lock(image_mutex);
            ++image_generation; // A concurrent decode must not resurrect a cleared frame.
            output.image_free(pending_image); pending_image=nullptr;
            image_due=false;
        }
        if (active.load()) output.image_show(epoch.load(),nullptr);
        artwork->frame_done(slot);
    }
    void present_image() {
        std::unique_lock<std::mutex> lock(image_mutex);
        if (!image_due) return;
        if (!image_epoch && active.load()) image_epoch=epoch.load();
        bool shown=image_epoch && output.image_show(image_epoch,pending_image);
        if (shown || client->now_us()>=image_deadline) {
            ESP_LOGI(TAG,"Artwork framebuffer %s",shown ? "updated" : "skipped (no ownership/display)");
            output.image_free(pending_image); pending_image=nullptr; image_due=false;
            lock.unlock();
            // Keep the slot gated until the frame is consumed, including deferred
            // presentation. A later decode must not replace a frame before its due time.
            artwork->frame_done(0);
        }
    }
    void on_request_high_performance() override {
        // Library contract: only change platform mode here, no client/role calls.
        auto *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!sta || esp_netif_get_default_netif()!=sta) return;
        if (esp_wifi_get_ps(&previous_wifi_ps)==ESP_OK && previous_wifi_ps!=WIFI_PS_NONE &&
            esp_wifi_set_ps(WIFI_PS_NONE)==ESP_OK) {
            restore_wifi_ps=true;
            ESP_LOGI(TAG,"WiFi power saving suspended for Sendspin");
        }
    }
    void on_release_high_performance() override {
        if (!restore_wifi_ps) return;
        wifi_ps_type_t current;
        if (esp_wifi_get_ps(&current)==ESP_OK && current==WIFI_PS_NONE &&
            esp_wifi_set_ps(previous_wifi_ps)==ESP_OK)
            ESP_LOGI(TAG,"WiFi power saving restored after Sendspin");
        restore_wifi_ps=false;
    }
    void controls_changed() { controls_dirty=true; controls_changed_at=client->now_us(); }
    void save_controls(Persistence &store, bool force=false) {
        // NVS flash operations may stall execution/cache access. Persist after
        // playback stops, never on a debounce timer in an active audio stream.
        if (active.load() && !force) return;
        if (!controls_dirty || (!force && client->now_us()-controls_changed_at<2000000)) return;
        const uint8_t state[]={player->get_volume(),static_cast<uint8_t>(player->get_muted())};
        if (store.save_blob("ui_state",state,sizeof(state)) && store.commit()) controls_dirty=false;
    }
    void on_time_sync_updated(float error) override {
        // Use a reference near the live server time. Extrapolating to server
        // time zero amplifies tiny drift corrections by the server's uptime.
        if (!clock_anchor_server) clock_anchor_server=client->now_us()-client->get_client_time(0);
        int64_t mapped=client->get_client_time(clock_anchor_server);
        ESP_LOGI(TAG,"Clock uncertainty %.1f us; anchor correction %lld us",static_cast<double>(error),
                 static_cast<long long>(previous_clock_anchor ? mapped-previous_clock_anchor : 0));
        previous_clock_anchor=mapped;
    }
    size_t on_audio_write(uint8_t *data,size_t length,uint32_t timeout) override {
        size_t written=active.load() ? output.write(epoch.load(),data,length,timeout) : 0;
        // Revoked ownership returns immediately, unlike normal sink backpressure.
        // The sync worker retries zero writes: yield until the main loop/server
        // processes leave/stream-end, otherwise it starves IDLE and trips WDT.
        // Never pretend these discarded bytes were accepted or played.
        if (length && !written && timeout) vTaskDelay(1);
        return written;
    }
    void on_stream_start() override {
        const auto &p=player->get_current_stream_params();
        uint32_t token=p.is_complete() ? output.begin(*p.sample_rate,*p.bit_depth,*p.channels) : 0;
        bool accepted=token!=0;
        epoch.store(token);
        { std::lock_guard<std::mutex> lock(feedback_mutex); feedback_epoch=token; }
        active.store(accepted);
        next_progress=0;
        if (!accepted) { client->set_available(false); ESP_LOGW(TAG,"Output unavailable or format rejected"); return; }
        output.volume(token,player->get_volume(),player->get_muted());
        output.metadata(token,metadata.artist.value_or("").c_str(),metadata.album.value_or("").c_str(),metadata.title.value_or("").c_str());
        ESP_LOGI(TAG,"Stream %lu Hz, %u bits, %u channels",(unsigned long)*p.sample_rate,*p.bit_depth,*p.channels);
    }
    void on_stream_end() override {
        active.store(false);
        { std::lock_guard<std::mutex> lock(feedback_mutex); feedback_epoch=0; }
        output.end(epoch.exchange(0));
    }
    void on_volume_changed(uint8_t v) override {
        controls_changed();
        if(active.load()) output.volume(epoch.load(),v,player->get_muted());
    }
    void on_mute_changed(bool m) override {
        controls_changed();
        if(active.load()) output.volume(epoch.load(),player->get_volume(),m);
    }
    void on_metadata(const ServerMetadataStateObject &m) override {
        metadata=m;
        next_progress=0;
        if(active.load()) output.metadata(epoch.load(),m.artist.value_or("").c_str(),m.album.value_or("").c_str(),m.title.value_or("").c_str());
    }
    void on_metadata_clear() override { metadata={}; next_progress=0; if(active.load()) output.metadata(epoch.load(),"","",""); }
};

static void run_client() {
    Network network;
    Persistence store;
    if (!store.open()) { ESP_LOGE(TAG,"Cannot open identity store"); return; }
    SendspinClientConfig cfg;
    cfg.name=active_name; cfg.manufacturer="RASPIAUDIO";
    cfg.product_name="MnCast S31"; cfg.software_version="s31-sendspin-dev";
    cfg.server_port=8928;
    // Squeezelite already runs its configuration HTTP server.
    cfg.httpd_ctrl_port=32769;
    Listener listener; // Must outlive client and its roles, including start failures.
    SendspinClient client(std::move(cfg));
    PlayerRoleConfig pc;
    // Explicit wire formats; packed 24-bit PCM expands to the existing Q31 path.
    pc.audio_formats={{SendspinCodecFormat::FLAC,2,48000,24},
                      {SendspinCodecFormat::PCM,2,48000,24},
                      {SendspinCodecFormat::FLAC,2,44100,24},
                      {SendspinCodecFormat::PCM,2,44100,24},
                      {SendspinCodecFormat::FLAC,2,48000,16},
                      {SendspinCodecFormat::PCM,2,48000,16},
                      {SendspinCodecFormat::FLAC,2,44100,16},
                      {SendspinCodecFormat::PCM,2,44100,16}};
    pc.audio_buffer_capacity=512*1024;
#if CONFIG_MNCAST_SENDSPIN_EXPERIMENTAL_HIRES
    // Opt-in only: tonal FLAC192 passed a short quiet test, not music endurance.
    // Raw PCM192 had network stalls and lost sync, so it is not advertised.
    pc.audio_formats.push_back({SendspinCodecFormat::FLAC,2,96000,24});
    pc.audio_formats.push_back({SendspinCodecFormat::FLAC,2,192000,24});
    pc.audio_buffer_capacity=1024*1024; // >= 500 ms raw PCM192/24, plus framing
    ESP_LOGW(TAG,"Experimental high-rate FLAC advertised; music endurance unverified");
#endif
    auto &player=client.add_player(std::move(pc));
    listener.player=&player; listener.client=&client;
    player.set_listener(&listener);
    client.set_listener(&listener);
    listener.metadata_role=&client.add_metadata();
    listener.metadata_role->set_listener(&listener);
    ArtworkRoleConfig ac;
    ImageSlotPreference slot;
    slot.source=SendspinImageSource::ALBUM; slot.format=SendspinImageFormat::JPEG;
    slot.width=208; slot.height=208; slot.max_image_bytes=64*1024;
    slot.require_frame_done=true;
    ac.preferred_formats.push_back(slot);
    listener.artwork=&client.add_artwork(std::move(ac));
    listener.artwork->set_listener(&listener);
    client.set_network_provider(&network);
    client.set_persistence_provider(&store);
    // Paired-only by default. Provisioning UI/token is a subsequent integration step.
    if (client.start()) {
        // The library persists pairing/output delay, but not volume or mute.
        // Match the user's selected 100% initially, then restore Sendspin's own
        // controls on reboot. Debounce later writes to avoid flash churn.
        auto controls=store.load_blob("ui_state");
        bool valid=controls && controls->size()==2 && (*controls)[0]<=100 && (*controls)[1]<=1;
        player.update_volume(valid ? (*controls)[0] : 100);
        player.update_muted(valid ? (*controls)[1]!=0 : false);
        {
            std::lock_guard<std::mutex> lock(pairing_mutex);
            pairing_token=client.pairing_token().value_or("");
        }
        { std::lock_guard<std::mutex> lock(feedback_mutex); feedback_player=&player; }
        bool advertised=false;
        int64_t next_snapshot=0;
        unsigned records=0;
        unsigned advertise_tick=0;
        uint32_t revoked_epoch=0;
        ESP_LOGI(TAG,"Client started; waiting for network/mDNS readiness");
        while(!stopping.load()) {
            // Network services initialise after the player task. Retry instead
            // of permanently losing discovery when mdns_init has not run yet.
            if(!advertised && (++advertise_tick % 100)==0 && network.is_network_ready()) {
                mdns_txt_item_t txt[]={{"path","/sendspin"}};
                esp_err_t err=mdns_service_add(active_name.c_str(),"_sendspin","_tcp",8928,txt,1);
                advertised=err==ESP_OK;
                if(advertised) ESP_LOGI(TAG,"mDNS service advertised on port 8928");
            }
            // A leave belongs to the revoked epoch, never to its replacement.
            // Post it once: the library processes it asynchronously.
            uint32_t active_epoch=listener.epoch.load();
            if (listener.active.load() && active_epoch!=revoked_epoch && !output.owns(active_epoch)) {
                revoked_epoch=active_epoch;
                ESP_LOGI(TAG,"Output taken by another source; leaving Sendspin group");
                client.leave();
            }
            client.set_available(output.available());
            client.loop();
            listener.present_progress();
            listener.present_image();
            listener.save_controls(store);
            if(client.now_us()>=next_snapshot) {
                next_snapshot=client.now_us()+1000000;
                if(records_dirty.exchange(false)) {
                    records=0;
                    for(unsigned i=0;i<255;++i) {
                        auto blob=store.load_blob(persistence_keys::record_slot_key(i));
                        if(blob && decode_pairing_record(blob->data(),blob->size())) ++records;
                    }
                }
                cJSON *j=cJSON_CreateObject();
                bool connected=client.is_connected();
                bool playing=listener.active.load() && output.owns(listener.epoch.load());
                cJSON_AddBoolToObject(j,"connected",connected);
                cJSON_AddBoolToObject(j,"playing",playing);
                cJSON_AddBoolToObject(j,"time_synced",connected && client.is_time_synced());
                cJSON_AddBoolToObject(j,"paired_connection",connected && listener.paired_connection);
                cJSON_AddNumberToObject(j,"pairings",records);
                auto server=client.get_server_information();
                cJSON_AddStringToObject(j,"server",server ? server->name.c_str() : "");
                // Read-only diagnostics from the same metadata clock used by the LCD.
                const auto &progress=listener.metadata.progress;
                const bool progress_known=progress.has_value() && progress->track_duration>0;
                cJSON_AddBoolToObject(j,"progress_known",progress_known);
                cJSON_AddNumberToObject(j,"position_ms",progress_known ? listener.metadata_role->get_track_progress_ms() : 0);
                cJSON_AddNumberToObject(j,"duration_ms",progress ? progress->track_duration : 0);
                cJSON_AddNumberToObject(j,"playback_speed",progress ? progress->playback_speed : 0);
                const auto &p=player.get_current_stream_params();
                if(playing && p.is_complete()) {
                    cJSON_AddNumberToObject(j,"sample_rate",*p.sample_rate);
                    cJSON_AddNumberToObject(j,"bit_depth",*p.bit_depth);
                    cJSON_AddNumberToObject(j,"channels",*p.channels);
                    cJSON_AddStringToObject(j,"codec",*p.codec==SendspinCodecFormat::FLAC ? "FLAC" : "PCM");
                    cJSON_AddStringToObject(j,"title",listener.metadata.title.value_or("").c_str());
                }
                char *json=cJSON_PrintUnformatted(j); cJSON_Delete(j);
                if(json) { std::lock_guard<std::mutex> lock(web_mutex); web_snapshot=json; free(json); }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        { std::lock_guard<std::mutex> lock(feedback_mutex); feedback_player=nullptr; }
        if(advertised) mdns_service_remove("_sendspin","_tcp");
        listener.save_controls(store,true);
        client.stop();
        { std::lock_guard<std::mutex> lock(pairing_mutex); pairing_token.clear(); }
    } else ESP_LOGE(TAG,"Sendspin start failed");
}

static void task(void *) {
    // Let all C++ locals destruct before FreeRTOS deletes the task.
    run_client();
    { std::lock_guard<std::mutex> lock(web_mutex); web_snapshot="{}"; }
    running=false;
    vTaskDelete(nullptr);
}

extern "C" bool mn_sendspin_init(const mn_sendspin_output_t *ops) {
    if(running.exchange(true)) return true;
    output=*ops; stopping=false;
    esp_console_cmd_t cmd{};
    cmd.command="sendspin_token";
    cmd.help="Show Sendspin pairing token on explicit console request (secret)";
    cmd.func=token_command;
    esp_console_cmd_register(&cmd);
    if(xTaskCreate(task,"sendspin",8192,nullptr,5,nullptr)!=pdPASS) {
        esp_console_cmd_deregister("sendspin_token");
        running=false; return false;
    }
    return true;
}
extern "C" void mn_sendspin_deinit() {
    stopping=true;
    while(running.load()) vTaskDelay(pdMS_TO_TICKS(10));
    esp_console_cmd_deregister("sendspin_token");
}
extern "C" void mn_sendspin_played(uint32_t epoch,uint32_t frames,int64_t finish_us) {
    std::lock_guard<std::mutex> lock(feedback_mutex);
    if(feedback_player && epoch && epoch==feedback_epoch && frames) feedback_player->notify_audio_played(frames,finish_us);
}
