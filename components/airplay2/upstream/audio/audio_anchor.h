#pragma once
#include <stdint.h>

/* Compare anchors at the same network instant. RTP may wrap, and an anchor
 * may name an earlier point on the same timeline rather than "right now".
 * Split seconds/remainder before multiplying, so a long track does not need
 * an artificial ten-minute limit and intermediate nanoseconds cannot overflow. */
static inline uint32_t audio_anchor_rtp_at_time(uint32_t rtp, uint64_t anchor_ns,
                                               uint64_t now_ns, int rate) {
  int64_t delta = (int64_t)(now_ns - anchor_ns);
  int64_t samples = (delta / 1000000000LL) * rate +
                    (delta % 1000000000LL) * rate / 1000000000LL;
  return rtp + (uint32_t)samples;
}
