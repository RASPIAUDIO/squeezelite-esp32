/* MnCast LDAC sink protocol helpers. Wire constants follow AOSP LDAC CIE.
 * All callers supply an AVDT_CODEC_SIZE buffer (at least 11 bytes).
 * This is a build-local ESP-IDF extension, not a generic vendor-codec API. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MNCAST_AV_CODEC_LDAC 0xff
/* IDF's unused-codec sentinel is also 0xff; keep it distinct from the wire ID. */
#define MNCAST_AV_CODEC_NONE 0xfe

static inline bool mncast_ldac_valid(const uint8_t *p, bool capability) {
    static const uint8_t prefix[9] = {10, 0, 0xff, 0x2d, 1, 0, 0, 0xaa, 0};
    if (memcmp(p, prefix, sizeof(prefix))) return false;
    unsigned rate = p[9], mode = p[10];
    if (!rate || (rate & ~(capability ? 0x3fU : 0x3cU)) || !mode || (mode & ~7U)) return false;
    return capability || (!(rate & (rate-1)) && !(mode & (mode-1)));
}

static inline bool mncast_ldac_match(const uint8_t *cfg, const uint8_t *cap) {
    return mncast_ldac_valid(cfg, false) && mncast_ldac_valid(cap, true) &&
           !(cfg[9] & ~cap[9]) && !(cfg[10] & ~cap[10]);
}

static inline bool mncast_ldac_intersects(const uint8_t *a, const uint8_t *b) {
    return mncast_ldac_valid(a, true) && mncast_ldac_valid(b, true) &&
           (a[9] & b[9] & 0x3c) && (a[10] & b[10]);
}

static inline bool mncast_ldac_choose(uint8_t *out, const uint8_t *peer,
                                      const uint8_t *local) {
    if (!mncast_ldac_intersects(peer, local)) return false;
    unsigned rate = peer[9] & local[9], mode = peer[10] & local[10];
    memcpy(out, peer, 11);
    out[9] = rate & 4 ? 4 : rate & 8 ? 8 : rate & 16 ? 16 : 32;
    out[10] = mode & 1 ? 1 : mode & 2 ? 2 : 4;
    return true;
}
