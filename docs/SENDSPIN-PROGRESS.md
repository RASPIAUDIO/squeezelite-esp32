# Sendspin display progress

Local correction and device testing on 2026-11-03. Publication is separate from
the device validation described below.

## Cause and behavior

The Sendspin bridge previously forwarded title/artist/artwork but not playback
position. Display activation reset its counter to zero and no Sendspin code
started it. This affected Sendspin tracks as well as live streams.

The bridge now polls the pinned Sendspin metadata role every 250 ms on its
main loop. Its existing `get_track_progress_ms()` interpolates position using
the synchronized server timestamp and playback speed, freezes when speed is
zero, and uses new anchors after seeking. The display consumes this authoritative
position and does not run a second elapsed-time clock.

- A finite-duration timeline displays its position as minutes/seconds, including a paused
  track at zero when its duration is known.
- Missing progress or a zero duration displays **LIVE**. Real Music Assistant
  radio testing showed advancing position and playback speed with zero duration;
  checking only for an empty progress object was therefore insufficient.
  Unknown-duration non-radio content also uses this UI fallback because this
  metadata does not identify the media type.
- Metadata removal clears the old position. Updates are scoped to the current
  source epoch so a stale Sendspin callback cannot overwrite AirPlay/Bluetooth.
- A busy display is retried on the next poll, without blocking the output mutex
  or doing SPI work on the audio thread.
- Existing stream-end behavior is retained: if the server ends the stream on
  pause, Sendspin releases the display; resume reapplies current metadata. A pause
  conveyed through metadata while the stream stays active freezes the position.

Spotify Connect's direct cspot path is unchanged. Spotify content routed through
Music Assistant/Sendspin benefits from this correction; the transport used in
the user's earlier Spotify test was not identified.

## Host verification

`python scripts/tests/test_sendspin_progress.py` compiles the actual polling,
source-ownership and display-state function bodies together with the pinned
library's interpolation function and simulated clock/locks. Use a C++17 compiler
via `CXX` (Clang, or `CXX=cl` from a Visual Studio developer shell).

Cases cover progression without new metadata, paused position, forward/backward
seeks, resume, paused zero position, absent/empty/live timelines, unknown duration
with advancing position (radio LIVE), metadata clear, duration clamp, inactive/revoked source,
250 ms polling, busy-display retry and Wi-Fi-screen protection.

`python scripts/tests/test_standalone_display.py --out <directory>` also checks
that LIVE changes the time region without overwriting the cover, alongside the
existing 320x240/480x320 and PCM16/PCM32 visualizer regression tests.

Host tests cannot validate real server timing or panel appearance. Device
results are recorded below. Cross-source switching under concurrent metadata
was covered by the host ownership tests, not repeated on the device in this run.

## Local build

The ESP-IDF 6.1 S31 PCM32 build completed successfully as
`s31-progress-20261103`. Progress tests and display/PCM regression tests passed.
Application SHA-256:
`9ae31d91c5109f07d247b40c4755fd7d18f017680ec52d458eba9b8b42700564`.
This initial image was flashed and its write verified. A follow-up diagnostic
build adds read-only `progress_known`, `position_ms`, `duration_ms` and
`playback_speed` to `/sendspin.json`, using the same metadata clock as the LCD.
These fields verify the input to the display, not the physical panel rendering.
No remote repository or webloader was updated.

## Device validation — 2026-11-03

The final application was flashed on COM21 at offset `0x410000`; esptool verified
the written hash and the player reconnected to Music Assistant. Final SHA-256:
`93e0a871d2dbcbd571fac5a518964be402d495681c4415419a4779e875360b50`.

Tests used the actual Music Assistant web interface and the paired MnCast
Sendspin player. The local LMS service remained stopped. No microphone recording
was made. Device status was sampled approximately once per second.

| Scenario | Observed result |
| --- | --- |
| Track playback | Nina Simone, *I Put A Spell On You*, FLAC 24-bit / 44.1 kHz via Sendspin. Position advanced from 3.374 to 29.523 seconds over the sampled play segment. User confirmed the LCD counter advances. |
| Pause | Server speed became zero; position remained exactly 34.675 seconds for 37 consecutive samples. Music Assistant also ended the audio stream, so the existing stream-end path releases the display. Retaining the paused screen is not implemented by this change. |
| Resume | The new stream resumed around the saved position (first playing sample 35.251 seconds), then advanced normally. |
| Seek forward | From approximately 58 seconds to 121.874 seconds, matching the UI's approximately 2:01 target. A stopped-stream transition precedes the new stream. |
| Seek backward | From approximately 141.986 to 32.145 seconds, matching the approximately 0:30 target plus restart/playback time. |
| Radio | FIP plays via MA/Sendspin at 48 kHz. MA supplies advancing progress with duration zero and speed 1000. The corrected display selects LIVE. User confirmed LIVE on the physical screen. |

Pause and seek values above are device metadata-clock observations correlated
with the Music Assistant UI. They are not claims of a separately photographed
LCD value for each operation. Final state: FIP left playing on MnCast Sendspin.

Local evidence is under the workspace's `evidence/s31-validation/` directory:
`progress-ma-device.jsonl`, `progress-ma-pause.png`,
`progress-ma-seek-forward.png`, `progress-ma-fip-final.png`, and
`sendspin-progress-live-final-flash.log`. The final binary is
`squeezelite-s31-progress-live-final-20261103.bin`.

The progress regression test was extended with the real MA radio case (nonzero
elapsed position and speed, zero duration) and passed using the native MSVC
compiler. The diagnostic build and final ESP-IDF PCM32 build completed.
