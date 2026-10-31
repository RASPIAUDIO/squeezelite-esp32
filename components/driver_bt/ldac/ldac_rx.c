#include "ldac_rx.h"
#include <string.h>

bool ldac_rx_init(ldac_rx_t *rx, int rate, unsigned channel_mode) {
    if (!rx) return false;
    int rate_id = rate == 44100 ? 0 : rate == 48000 ? 1 : rate == 88200 ? 2 : rate == 96000 ? 3 : -1;
    int channel_id = channel_mode == 4 ? 0 : channel_mode == 2 ? 1 : channel_mode == 1 ? 2 : -1;
    if (rate_id < 0 || channel_id < 0) return false;
    memset(rx, 0, sizeof(*rx));
    rx->rate_id = rate_id;
    rx->channel_id = channel_id;
    return ldacdecInit(&rx->decoder) == 0;
}

static void discard_state(ldac_rx_t *rx) {
    rx->fragment_size = 0;
    ldacdecReset(&rx->decoder);
}

static int reject(ldac_rx_t *rx) {
    ++rx->rejected;
    discard_state(rx);
    return -1;
}

static int decode_frames(ldac_rx_t *rx, const uint8_t *data, size_t length, unsigned count,
                         ldac_rx_output_t output, void *opaque) {
    size_t offset = 0;
    /* Validate the whole packet's framing/configuration before emitting PCM. */
    for (unsigned i = 0; i < count; ++i) {
        if (length-offset < 3 || data[offset] != 0xaa ||
            (data[offset+1] >> 5) != rx->rate_id ||
            ((data[offset+1] >> 3) & 3) != rx->channel_id) return reject(rx);
        size_t size = (((data[offset+1] & 7) << 6) | (data[offset+2] >> 2)) + 4;
        if (size > length-offset) return reject(rx);
        offset += size;
    }
    if (offset != length) return reject(rx);
    offset = 0;
    int total = 0;
    for (unsigned i = 0; i < count; ++i) {
        int used = 0;
        int n = ldacDecode32(&rx->decoder, data+offset, length-offset, rx->pcm, 512, &used);
        if (n < 0 || used <= 0) return reject(rx);
        if (rx->channel_id == 0) {
            for (int j = n-1; j >= 0; --j) rx->pcm[j*2] = rx->pcm[j*2+1] = rx->pcm[j];
        }
        output(opaque, rx->pcm, (size_t)n);
        total += n;
        offset += used;
    }
    return total;
}

int ldac_rx_process(ldac_rx_t *rx, const uint8_t *payload, size_t length,
                    uint16_t sequence, uint32_t timestamp,
                    ldac_rx_output_t output, void *opaque) {
    if (!rx || !payload || !output) return -1;
    if (rx->have_sequence) {
        uint16_t delta = sequence-rx->expected_sequence;
        if (delta >= 0x8000) { ++rx->duplicates; return 0; }
        if (delta) { ++rx->gaps; discard_state(rx); }
    }
    rx->have_sequence = true;
    rx->expected_sequence = sequence+1;
    if (length < 2 || length > 1 + 15*515 || (payload[0] & 0x10)) return reject(rx);
    unsigned header = *payload++;
    --length;
    if (!(header & 0x80)) {
        if ((header & 0x60) || !(header & 15)) return reject(rx);
        if (rx->fragment_size) discard_state(rx);
        return decode_frames(rx, payload, length, header & 15, output, opaque);
    }
    /* Fragmented LDAC transport frames fit the 9-bit frame-length field. */
    if (header & 0x40) {
        if (header & 0x20) return reject(rx);
        if (rx->fragment_size) discard_state(rx);
        rx->fragment_timestamp = timestamp;
        rx->fragment_size = 0;
    } else if (!rx->fragment_size || timestamp != rx->fragment_timestamp) {
        return reject(rx);
    }
    if (length > sizeof(rx->fragment)-rx->fragment_size) return reject(rx);
    memcpy(rx->fragment+rx->fragment_size, payload, length);
    rx->fragment_size += length;
    if (!(header & 0x20)) return 0;
    size_t complete = rx->fragment_size;
    rx->fragment_size = 0;
    return decode_frames(rx, rx->fragment, complete, 1, output, opaque);
}
