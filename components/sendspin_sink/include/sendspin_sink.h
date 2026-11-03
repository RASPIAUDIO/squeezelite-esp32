#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Callbacks into the existing output pipeline, without a second I2S owner. */
#define MN_SENDSPIN_SOURCE_ID 4
/* Accessed only while the Squeezelite output mutex is held. */
extern uint32_t mn_sendspin_output_epoch;
typedef struct {
    uint32_t (*begin)(uint32_t rate, uint8_t depth, uint8_t channels);
    size_t (*write)(uint32_t epoch, const uint8_t *pcm, size_t bytes, uint32_t timeout_ms);
    void (*end)(uint32_t epoch);
    bool (*available)(void);
    void (*volume)(uint32_t epoch, uint8_t percent, bool muted);
    void (*metadata)(uint32_t epoch, const char *artist, const char *album, const char *title);
    void *(*image_decode)(const uint8_t *data, size_t length);
    bool (*image_show)(uint32_t epoch, void *image);
    void (*image_free)(void *image);
    bool (*owns)(uint32_t epoch);
    void (*progress)(uint32_t epoch, bool known, uint32_t position_ms);
} mn_sendspin_output_t;
/* Web API: returned JSON/token strings must be freed by the caller. */
bool mn_sendspin_enabled(bool fallback);
char *mn_sendspin_status_json(void);
char *mn_sendspin_web_token(void);
bool mn_sendspin_web_config(bool enabled, const char *name, bool forget);
bool mn_sendspin_init(const mn_sendspin_output_t *output);
void mn_sendspin_deinit(void);
/* Called after I2S submission, for actual source frames only; timestamp is
 * the estimated completion of this block in the DMA queue, in esp_timer us. */
void mn_sendspin_played(uint32_t epoch, uint32_t frames, int64_t finish_us);
/* Output implementation: validate ownership and apply controls atomically. */
bool mn_sendspin_volume_epoch(uint32_t epoch, uint8_t percent, bool muted);
#ifdef __cplusplus
}
#endif
