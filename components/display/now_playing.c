#include <stdio.h>
#include <string.h>
#include "now_playing.h"
#include "gds.h"
#include "gds_private.h"
#include "gds_draw.h"
#include "gds_font.h"

bool now_playing_supported(struct GDS_Device *d) {
    return d && d->Mode == GDS_RGB565 && d->Framebuffer && !d->DrawRGB &&
           !d->ClearWindow && d->Width >= 320 && d->Height >= 240;
}
int now_playing_cover_size(struct GDS_Device *d) {
    int h = d->Height - 32, w = d->Width - 112;
    return h < w ? h : w;
}
static int cover_x(struct GDS_Device *d) { return d->Width - 12 - now_playing_cover_size(d); }
static void rect(struct GDS_Device *d, int x, int y, int w, int h, int color) {
    for (int row = y; row < y + h; row++) GDS_DrawHLine(d, x, row, w, color);
}
/* Restore shared GDS font state so LMS and the recovery screen are unaffected. */
static void text(struct GDS_Device *d, int x, int y, int right, const char *s) {
    const struct GDS_FontDef *font = d->Font;
    bool prop = d->FontForceProportional, mono = d->FontForceMonospace;
    int width = d->TextWidth;
    GDS_SetFont(d, &Font_liberation_mono_9x15);
    GDS_SetTextWidth(d, right);
    GDS_FontDrawString(d, x, y, s, GDS_COLOR_WHITE);
    GDS_SetFont(d, font);
    d->FontForceProportional = prop; d->FontForceMonospace = mono;
    GDS_SetTextWidth(d, width);
}
int now_playing_title_width(const char *title) { return strlen(title) * 9; }
void now_playing_title(struct GDS_Device *d, const char *title, int offset) {
    rect(d, 0, 0, d->Width, 27, GDS_COLOR_BLACK);
    text(d, 8 - offset, 7, d->Width - 8, title);
    /* The font clips at x=0; explicitly keep the left safe margin black. */
    rect(d, 0, 0, 8, 27, GDS_COLOR_BLACK);
}
void now_playing_info(struct GDS_Device *d, const char *source, uint32_t seconds) {
    int right = cover_x(d) - 8;
    char time[16];
    snprintf(time, sizeof(time), "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    rect(d, 8, d->Height - 62, right - 8, 58, GDS_COLOR_BLACK);
    text(d, 8, d->Height - 56, right, time);
    text(d, 8, d->Height - 28, right, !strcmp(source,"BLUETOOTH") ? "BT" : source);
}
void now_playing_spectrum(struct GDS_Device *d, const uint8_t *levels, uint8_t *peaks) {
    int width = cover_x(d) - 20, bottom = d->Height - 72;
    int height = d->Height - 116, step = width / NOW_PLAYING_BANDS;
    rect(d, 8, 36, width, bottom - 35, GDS_COLOR_BLACK);
    for (int i = 0; i < NOW_PLAYING_BANDS; i++) {
        unsigned value = levels ? levels[i] : 0;
        if (value > 100) value = 100;
        peaks[i] = value > peaks[i] ? value : (peaks[i] > 3 ? peaks[i] - 3 : 0);
        int h = value * height / 100, peak = peaks[i] * height / 100;
        int x = 8 + i * step, w = step > 2 ? step - 2 : 1;
        if (h) rect(d, x, bottom - h + 1, w, h, 0x433a);
        if (peak) rect(d, x, bottom - peak, w, 2, GDS_COLOR_WHITE);
    }
}
void now_playing_cover(struct GDS_Device *d, const uint16_t *pixels, int width, int height) {
    int side = now_playing_cover_size(d), x = cover_x(d), y = 28;
    rect(d, x, y, side, side, GDS_COLOR_BLACK);
    if (!pixels || width <= 0 || height <= 0) return;
    /* Aspect-preserving fit, including small default artwork. No per-frame allocation. */
    int w = width >= height ? side : side * width / height;
    int h = height >= width ? side : side * height / width;
    x += (side - w) / 2; y += (side - h) / 2;
    for (int row = 0; row < h; row++) for (int col = 0; col < w; col++)
        GDS_DrawPixel(d, x + col, y + row, pixels[(row * height / h) * width + col * width / w]);
}
