#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Serialize source changes with their display/control callbacks. Acquire before
 * decode/output locks; never acquire from a PCM callback. Recursive because a
 * transport may report a synchronous command during disconnection. */
void audio_source_lock(void);
void audio_source_unlock(void);
/* Called/read only with the source lock held. Invalidates async artwork. */
void audio_source_changed(void);
uint32_t audio_source_generation(void);
#ifdef __cplusplus
}
#endif
