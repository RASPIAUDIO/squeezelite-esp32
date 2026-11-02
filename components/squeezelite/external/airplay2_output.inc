/* AirPlay 2 output bridge. Included after the shared sink implementation so
 * the same locks, volume, DSP and I2S path are used by every source. */
static uint32_t ap2_output_epoch;

static bool ap2_output_owns(uint32_t epoch) {
    LOCK_O;
    bool owns = epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2;
    UNLOCK_O;
    return owns;
}

static uint32_t ap2_output_begin(uint32_t rate) {
    if (rate != 44100 && rate != 48000) return 0;
    audio_source_lock();
    LOCK_D; LOCK_O;
    audio_source_changed();
    _buf_flush(outputbuf);
    _buf_limit(outputbuf, 0);
    output.track_start = NULL;
    output.fade = FADE_INACTIVE;
    output.current_sample_rate = output.next_sample_rate = rate;
    output.external = DECODE_AP2;
    output.frames_played = output.frames_played_dmp = 0;
    output.state = OUTPUT_RUNNING;
    sink_state = SINK_RUNNING;
    if (++ap2_output_epoch == 0) ++ap2_output_epoch;
    uint32_t epoch = ap2_output_epoch;
    if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
    UNLOCK_O; UNLOCK_D;
    displayer_control(DISPLAYER_ACTIVATE, "AIRPLAY", true);
    displayer_artwork(NULL);
    displayer_control(DISPLAYER_TIMER_RUN);
    raop_sink_activate_controls();
    audio_source_unlock();
    LOG_INFO("AirPlay 2 output epoch:%u rate:%u PCM16 to PCM%d", epoch, rate, BYTES_PER_FRAME * 4);
    return epoch;
}

static size_t ap2_output_write(uint32_t epoch, const int16_t *pcm, size_t frames, uint32_t timeout_ms) {
    uint32_t start = gettime_ms();
    size_t done = 0;
    do {
        LOCK_O;
        bool owner = epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2 &&
                     output.state == OUTPUT_RUNNING;
        if (!owner) { UNLOCK_O; break; }
        /* Keep at most 20 ms downstream: AirPlay owns the large jitter buffer
         * and schedules against this queue plus the hardware backlog. */
        size_t used = _buf_used(outputbuf) / BYTES_PER_FRAME;
        size_t cap = output.current_sample_rate / 50;
        size_t count = min(frames-done, used < cap ? cap-used : 0);
        count = min(count, _buf_cont_write(outputbuf) / BYTES_PER_FRAME);
        count = min(count, _buf_space(outputbuf) / BYTES_PER_FRAME);
        if (count) {
            for (size_t i = 0; i < count * 2; i++) {
#if BYTES_PER_FRAME == 8
                ((int32_t *)outputbuf->writep)[i] = (int32_t)pcm[done*2+i] * 65536;
#else
                ((int16_t *)outputbuf->writep)[i] = pcm[done*2+i];
#endif
            }
            _buf_inc_writep(outputbuf, count * BYTES_PER_FRAME);
            done += count;
        }
        UNLOCK_O;
        if (done == frames || gettime_ms()-start >= timeout_ms) break;
        vTaskDelay(1);
    } while (true);
    return done;
}

static void ap2_output_end(uint32_t epoch) {
    audio_source_lock();
    LOCK_D; LOCK_O;
    bool owner = epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2;
    if (owner) {
        audio_source_changed();
        _buf_flush(outputbuf);
        output.external = 0;
        output.state = OUTPUT_STOPPED;
        output.stop_time = gettime_ms();
        sink_state = SINK_ABORT;
    }
    UNLOCK_O; UNLOCK_D;
    if (owner) {
        actrls_unset();
        displayer_control(DISPLAYER_SHUTDOWN);
    }
    audio_source_unlock();
}

static void ap2_output_flush(uint32_t epoch) {
    LOCK_O;
    if (epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2)
        _buf_flush(outputbuf);
    UNLOCK_O;
}

/* A pause retains source ownership, metadata and artwork. A subsequent source
 * takeover invalidates ownership normally; resuming then needs a new epoch. */
static void ap2_output_pause(uint32_t epoch, bool paused) {
    audio_source_lock();
    LOCK_O;
    bool owner = epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2;
    if (owner) {
        if (paused) _buf_flush(outputbuf);
        output.state = paused ? OUTPUT_STOPPED : OUTPUT_RUNNING;
        output.stop_time = gettime_ms();
        sink_state = paused ? SINK_ABORT : SINK_RUNNING;
    }
    UNLOCK_O;
    if (owner) displayer_control(paused ? DISPLAYER_TIMER_PAUSE : DISPLAYER_TIMER_RUN);
    audio_source_unlock();
}

static bool ap2_output_pipeline(uint32_t epoch, int64_t *now_us, uint32_t *delay_us) {
    LOCK_O;
    bool owner = epoch && epoch == ap2_output_epoch && output.external == DECODE_AP2;
    uint32_t rate = output.current_sample_rate;
    if (owner && rate) {
        *now_us = esp_timer_get_time();
        /* Same device backlog estimate as the existing RAOP synchronizer.
         * This is not an independent I2S hardware completion counter. */
        uint64_t frames = output.device_frames + output.frames_in_process;
        uint64_t elapsed = (uint64_t)(gettime_ms()-output.updated) * rate / 1000;
        frames = frames > elapsed ? frames-elapsed : 0;
        frames += _buf_used(outputbuf) / BYTES_PER_FRAME;
        *delay_us = frames * 1000000ULL / rate;
    }
    UNLOCK_O;
    return owner && rate;
}

static void ap2_output_volume(uint32_t epoch, float db) {
    if (!isfinite(db)) return;
    audio_source_lock();
    if (ap2_output_owns(epoch)) {
        float normalized = db <= -30 ? 0 : db >= 0 ? 1 : (db + 30) / 30;
        unsigned gain = (unsigned)(normalized * normalized * FIXED_ONE);
        set_volume_owned(DECODE_AP2, gain, gain);
    }
    audio_source_unlock();
}

static void ap2_output_metadata(uint32_t epoch, const char *artist, const char *album,
        const char *title, uint32_t position, uint32_t duration) {
    audio_source_lock();
    if (ap2_output_owns(epoch)) {
        displayer_metadata((char *)artist, (char *)album, (char *)title);
        // Receiver metadata is in seconds; the common display API uses ms.
        displayer_timer(DISPLAYER_ELAPSED, position * 1000, duration * 1000);
    }
    audio_source_unlock();
}

static void ap2_output_artwork(uint32_t epoch, const uint8_t *data, size_t size) {
    if (!ap2_output_owns(epoch) || size > 512*1024) return;
    void *image = displayer_image_decode(data, size);
    if (!image) return;
    audio_source_lock();
    if (ap2_output_owns(epoch)) displayer_image_show(image);
    audio_source_unlock();
    // show() copies into the framebuffer; it never takes image ownership.
    displayer_image_free(image);
}

static const mn_airplay2_output_t ap2_output_ops = {
    ap2_output_begin, ap2_output_owns, ap2_output_write, ap2_output_end,
    ap2_output_flush, ap2_output_pipeline, ap2_output_volume,
    ap2_output_metadata, ap2_output_artwork, ap2_output_pause
};
