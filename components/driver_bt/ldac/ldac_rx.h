#pragma once
#include <stdbool.h>
#include "ldacdec.h"

/* One context per stream, serialized by its owner. Output is always stereo
 * PCM32 (mono is duplicated). No allocation or Bluetooth dependency here. */
typedef void (*ldac_rx_output_t)(void *opaque, const int32_t *pcm, size_t frames);
typedef struct {
    ldacdec_t decoder;
    int32_t pcm[512];
    uint8_t fragment[515];
    size_t fragment_size;
    uint32_t fragment_timestamp;
    uint16_t expected_sequence;
    uint8_t rate_id, channel_id;
    bool have_sequence;
    uint32_t gaps, duplicates, rejected;
} ldac_rx_t;

bool ldac_rx_init(ldac_rx_t *rx, int rate, unsigned channel_mode);
/* Full LDAC media payload, including its 1-byte header. Sequence is RTP's
 * 16-bit sequence; timestamp is RTP's sample timestamp. Returns PCM frames
 * emitted, zero for pending fragments/duplicates, or negative on malformed data. */
int ldac_rx_process(ldac_rx_t *rx, const uint8_t *payload, size_t length,
                    uint16_t sequence, uint32_t timestamp,
                    ldac_rx_output_t output, void *opaque);
