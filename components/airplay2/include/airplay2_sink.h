#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All PCM is stereo signed 16-bit, promoted by the common output engine.
 * Epochs prevent delayed packets/metadata from reclaiming a different source. */
typedef struct {
    uint32_t (*begin)(uint32_t rate);
    bool (*owns)(uint32_t epoch);
    size_t (*write)(uint32_t epoch, const int16_t *pcm, size_t frames, uint32_t timeout_ms);
    void (*end)(uint32_t epoch);
    void (*flush)(uint32_t epoch);
    bool (*pipeline)(uint32_t epoch, int64_t *now_us, uint32_t *delay_us);
    void (*volume)(uint32_t epoch, float db);
    void (*metadata)(uint32_t epoch, const char *artist, const char *album, const char *title,
                     uint32_t position, uint32_t duration);
    void (*artwork)(uint32_t epoch, const uint8_t *data, size_t size);
    void (*pause)(uint32_t epoch, bool paused);
} mn_airplay2_output_t;

void mn_airplay2_configure(const mn_airplay2_output_t *output);
bool mn_airplay2_start(const char *name);
void mn_airplay2_stop(void);
void mn_airplay2_disconnect(void);
bool mn_airplay2_active(void);
void mn_airplay2_control(int raop_event);
