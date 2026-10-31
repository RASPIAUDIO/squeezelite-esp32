#include "sdkconfig.h"
#include "audio_source.h"
#include "sys/lock.h"

#if CONFIG_MNCAST_SENDSPIN
static _lock_t source_lock;
static uint32_t generation;
void audio_source_lock(void) { _lock_acquire_recursive(&source_lock); }
void audio_source_unlock(void) { _lock_release_recursive(&source_lock); }
void audio_source_changed(void) { if (++generation == 0) ++generation; }
uint32_t audio_source_generation(void) { return generation; }
#else
void audio_source_lock(void) {}
void audio_source_unlock(void) {}
void audio_source_changed(void) {}
uint32_t audio_source_generation(void) { return 0; }
#endif
