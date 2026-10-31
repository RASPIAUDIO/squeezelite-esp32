#include <stdlib.h>
#include <string.h>

#include "audio_stream.h"

#include "audio_buffer.h"
#include "audio_decoder.h"
#include "audio_receiver_internal.h"

extern const audio_stream_ops_t audio_stream_realtime_ops;
extern const audio_stream_ops_t audio_stream_buffered_ops;

static bool apply_aac_transient_mute(audio_receiver_state_t *state,
                                     int16_t *buffer, size_t samples,
                                     int channels) {
  if (!audio_decoder_is_aac(state->decoder)) {
    return false;
  }

  if ((state->blocks_read_in_sequence <= 2) &&
      (state->blocks_read_in_sequence != state->blocks_read)) {
    memset(buffer, 0, samples * channels * sizeof(int16_t));
    return true;
  }

  return false;
}

bool audio_stream_accept_timestamp(audio_receiver_state_t *state,
                                   uint32_t timestamp) {
  if (!state) {
    return false;
  }

  // Blanket gate: reject everything between seek_flush and the next anchor.
  // Deliberately checked before decrypt/decode in the buffered TCP task so
  // old-track backlog is drained from the socket without decoder CPU or PCM
  // ring use.
  if (state->discard_all_until_anchor) {
    return false;
  }

  // Post-seek RTP window gate: discard frames outside [discard_before_rtp,
  // discard_above_rtp].  The TCP socket buffer can hold many seconds of
  // pre-seek audio; both gates together handle both seek directions:
  //   discard_before_rtp — forward seek: stale frames have lower RTP
  //   discard_above_rtp  — backward seek: stale frames have much higher RTP
  // Each self-disarms on the first frame that passes it.
  if (state->discard_before_rtp_valid) {
    if ((int32_t)(timestamp - state->discard_before_rtp) < 0) {
      return false; // below lower bound — forward-seek stale frame
    }
    state->discard_before_rtp_valid = false;
  }
  if (state->discard_above_rtp_valid) {
    if ((int32_t)(timestamp - state->discard_above_rtp) > 0) {
      return false; // above upper bound — backward-seek stale frame
    }
    state->discard_above_rtp_valid = false;
  }

  return true;
}

// Read-only variant of the RTP gate used for the post-decode re-check.  Unlike
// audio_stream_accept_timestamp() it does NOT disarm the window gates on a
// passing frame — disarming is the job of the ordered pre-decode pass.  If a
// concurrent seek armed a window gate while this frame was mid-decode and the
// frame happens to fall inside the new window, disarming here would clear the
// gate and let subsequent stale TCP backlog through.  This check only reports
// whether the frame must be dropped.
static bool timestamp_is_gated(const audio_receiver_state_t *state,
                               uint32_t timestamp) {
  if (state->discard_all_until_anchor) {
    return true;
  }
  if (state->discard_before_rtp_valid &&
      (int32_t)(timestamp - state->discard_before_rtp) < 0) {
    return true;
  }
  if (state->discard_above_rtp_valid &&
      (int32_t)(timestamp - state->discard_above_rtp) > 0) {
    return true;
  }
  return false;
}

bool audio_stream_process_accepted_frame(audio_receiver_state_t *state,
                                         uint32_t timestamp,
                                         const uint8_t *audio_data,
                                         size_t audio_len) {
  if (!state || !state->decoder) {
    return false;
  }

  size_t capacity_samples = 0;
  int16_t *decode_buffer =
      audio_buffer_get_decode_buffer(&state->buffer, &capacity_samples);
  if (!decode_buffer || capacity_samples == 0) {
    return false;
  }

  audio_decode_info_t info = {0};
  int decoded_samples =
      audio_decoder_decode(state->decoder, audio_data, audio_len, decode_buffer,
                           capacity_samples, &info);
  if (decoded_samples <= 0) {
    return false;
  }

  int channels =
      info.channels > 0 ? info.channels : state->stream->format.channels;
  if (channels <= 0) {
    channels = 2;
  }

  // Validation fingerprint before jitter scheduling/DSP. Include RTP timestamp
  // so packet-order-independent aggregates still bind samples to their position.
  uint32_t hash = 2166136261u;
  for (unsigned n=0; n<4; n++) hash=(hash ^ ((timestamp >> (8*n)) & 255))*16777619u;
  size_t pcm_size=(size_t)decoded_samples*channels*sizeof(int16_t);
  const uint8_t *pcm_bytes=(const uint8_t *)decode_buffer;
  for (size_t n=0; n<pcm_size; n++) hash=(hash ^ pcm_bytes[n])*16777619u;
  state->stats.pcm_probe_packets++;
  state->stats.pcm_probe_bytes+=pcm_size;
  state->stats.pcm_probe_xor^=hash;
  state->stats.pcm_probe_sum+=hash;

  apply_aac_transient_mute(state, decode_buffer, (size_t)decoded_samples,
                           channels);

  // Re-check the gates after decode.  A concurrent seek/anchor flush (RTSP
  // task) can set discard_all_until_anchor OR arm the RTP window gates
  // (discard_before_rtp / discard_above_rtp, Path B) and flush the ring while
  // this frame was being decrypted/decoded.  Use the read-only predicate so a
  // stale mid-flight frame is dropped without disarming a gate a concurrent
  // seek just armed (which would let later backlog through).
  if (timestamp_is_gated(state, timestamp)) {
    return false;
  }

  return audio_buffer_queue_decoded(&state->buffer, &state->stats, timestamp,
                                    decode_buffer, (size_t)decoded_samples,
                                    channels);
}

bool audio_stream_process_frame(audio_receiver_state_t *state,
                                uint32_t timestamp, const uint8_t *audio_data,
                                size_t audio_len) {
  if (!audio_stream_accept_timestamp(state, timestamp)) {
    return false;
  }
  return audio_stream_process_accepted_frame(state, timestamp, audio_data,
                                             audio_len);
}

audio_stream_t *audio_stream_create_realtime(void) {
  audio_stream_t *stream = calloc(1, sizeof(*stream));
  if (!stream) {
    return NULL;
  }

  stream->ops = &audio_stream_realtime_ops;
  stream->type = AUDIO_STREAM_REALTIME;
  return stream;
}

audio_stream_t *audio_stream_create_buffered(void) {
  audio_stream_t *stream = calloc(1, sizeof(*stream));
  if (!stream) {
    return NULL;
  }

  stream->ops = &audio_stream_buffered_ops;
  stream->type = AUDIO_STREAM_BUFFERED;
  return stream;
}

void audio_stream_destroy(audio_stream_t *stream) {
  if (!stream) {
    return;
  }

  if (stream->ops && stream->ops->destroy) {
    stream->ops->destroy(stream);
    return;
  }

  free(stream);
}

bool audio_stream_uses_buffer(audio_stream_type_t type) {
  return type == AUDIO_STREAM_BUFFERED;
}
