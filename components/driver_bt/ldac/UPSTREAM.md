# LDAC decoder provenance and local changes

Source: https://github.com/hegdi/libldacdec
Commit: `7f3bd6cc1586b764b02f2e4f20b16c7ff18758b9`
License: MIT; see LICENSE, copyright Dirk Helbig 2021.

Only the decoder C sources and headers are vendored. The separately obtained
AOSP encoder is used to generate host test vectors, not included in firmware.

Local changes:

- Explicit input length and bounded bit consumption (Huffman lookahead can
  zero-pad but consuming missing bits fails).
- Validate header, band count, gradient ranges, scale factors, word lengths,
  invalid spectrum codes and output capacity before table access/output.
- Parse both mono blocks in dual-channel mode independently.
- Correct descending gradient, fine-quantizer index and scale-factor-zero gain.
- Export interleaved PCM32 without first rounding to PCM16, saturating the final
  conversion. Decode all blocks before updating MDCT overlap state.
- Fix bit-reversal zero-width and signed-shift undefined behavior.
- Prefix internal external symbols with `ldac_` to avoid other audio libraries.
- Remove the unused and unsafe null-packet construction API.
- Separate stream reset from shared-table initialization; compact transform
  tables without reducing precision (39,956 bytes of unused storage removed).
- Add a bounded A2DP packet/fragment receiver in ldac_rx.c, with sequence-loss
  reset and a stereo PCM32 callback.

Call ldacdecInit before use, then ldacdecReset after a lost/corrupt frame. Initialization writes
shared Huffman/transform tables: serialize initialization and decoding across
all instances. The firmware must call this decoder from its codec task/mutex,
not the Bluetooth receive callback. A failed frame leaves output untouched, but
the context requires reset. Changing sample rate also requires a reset.

Validation programs: validation/ldac_host_api.c, ldac_host_matrix.py and
ldac_host_safety.c. Results: validation/evidence/ldac/host-matrix32.json and
host-safety-linux.log. The tests do not establish Bluetooth negotiation,
packetization, real-time S31 performance, or product certification.
