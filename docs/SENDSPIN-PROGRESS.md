# Sendspin display progress

Local correction prepared on 2026-11-03. This change is not yet flashed or
validated on the physical player.

## Cause and behavior

The Sendspin bridge previously forwarded title/artist/artwork but not playback
position. Display activation reset its counter to zero and no Sendspin code
started it. This affected Sendspin tracks as well as live streams.

The bridge now polls the pinned Sendspin metadata role every 250 ms on its
main loop. Its existing `get_track_progress_ms()` interpolates position using
the synchronized server timestamp and playback speed, freezes when speed is
zero, and uses new anchors after seeking. The display consumes this authoritative
position and does not run a second elapsed-time clock.

- A usable timeline displays its position as minutes/seconds, including a paused
  track at zero when its duration is known.
- Missing progress, or a placeholder with zero duration, position and speed,
  displays **LIVE**. This is the chosen UI fallback for unavailable position;
  missing metadata does not itself prove that the content is a radio station.
- A duration of zero alone does not imply LIVE: a stream with a supplied position
  or nonzero speed still has a usable timeline.
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
with valid position, metadata clear, duration clamp, inactive/revoked source,
250 ms polling, busy-display retry and Wi-Fi-screen protection.

`python scripts/tests/test_standalone_display.py --out <directory>` also checks
that LIVE changes the time region without overwriting the cover, alongside the
existing 320x240/480x320 and PCM16/PCM32 visualizer regression tests.

Remaining device checks after a separately authorized flash: Music Assistant
track progression, pause/resume, seek forwards/backwards, radio LIVE fallback,
transition between track and radio, and source switching while metadata arrives.
Host tests cannot validate real server timing or panel appearance.

## Local build

The ESP-IDF 6.1 S31 PCM32 build completed successfully as
`s31-progress-20261103`. Progress tests and display/PCM regression tests passed.
Application SHA-256:
`9ae31d91c5109f07d247b40c4755fd7d18f017680ec52d458eba9b8b42700564`.
No device was flashed and no remote repository or webloader was updated.
