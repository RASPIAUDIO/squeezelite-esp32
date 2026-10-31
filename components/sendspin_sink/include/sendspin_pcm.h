#pragma once
#include <stdint.h>
#include <string.h>

// Packed signed little-endian PCM to full-scale signed PCM32. No arithmetic
// right-shift assumption, unaligned loads, or signed left-shift overflow.
// Caller validates width (2/3/4 bytes) and complete interleaved frames.
static inline int32_t mn_sendspin_pcm32(const uint8_t *p, unsigned width) {
    uint32_t word = 0;
    for (unsigned i = 0; i < width; ++i)
        word |= (uint32_t)p[i] << (8 * (i + 4 - width));
    int32_t result;
    memcpy(&result, &word, sizeof(result));
    return result;
}
