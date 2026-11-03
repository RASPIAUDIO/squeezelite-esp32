/* Standalone now-playing layout; drawing only, never changes audio samples. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct GDS_Device;
#define NOW_PLAYING_BANDS 12
bool now_playing_supported(struct GDS_Device *d);
int now_playing_cover_size(struct GDS_Device *d);
int now_playing_title_width(const char *title);
void now_playing_title(struct GDS_Device *d, const char *title, int offset);
void now_playing_info(struct GDS_Device *d, const char *source, uint32_t seconds, bool live);
void now_playing_spectrum(struct GDS_Device *d, const uint8_t *levels, uint8_t *peaks);
void now_playing_cover(struct GDS_Device *d, const uint16_t *pixels, int width, int height);
