# RGB565 display refresh investigation — 2026-11-05

The user reported persistent bright traces around the spectrum and overlapping
timer digits on the physical ILI9341 panel. The photo also showed isolated marks
near the source label and left edge of the title. These are distinct from the
intentional white spectrum peak markers.

## Findings and candidate correction

The renderer already clears the spectrum and information rectangles before
redrawing. A host regression using the production renderer, font code, and
ILI9341 update routine exercised 400 changing spectra and timer values per
resolution. It verified the emulated panel against the framebuffer after every
update and compared the cleared final scene with a freshly drawn scene. The
original implementation passed: the physical artifact was not reproduced in
this model, so its root cause is not yet established.

The RGB565 driver previously addressed a dirty rectangle once and sent its
pixels in multiple SPI transactions. The candidate correction explicitly sets
row/column bounds and RAMWR for every bounded payload. Each payload now fills
one complete window, including the fallback path without the internal buffer.
This removes dependence on the panel retaining its write cursor between payload
transactions. The drawing layout, peak decay and configured 10 MHz SPI clock
are unchanged.

The panel emulator now requires each payload to fill exactly a newly addressed
window. Standalone display tests pass at 320x240 and 480x320; PCM visualization
tests pass in PCM16 and PCM32. All eight Wi-Fi setup/QR regression cases pass.

## Firmware candidate

Candidate artifact: `squeezelite-s31-refresh-20261105.bin` (PCM32).
The application version embedded by the cached superbuild remains
`s31-progress-20261103`; identify this candidate by its binary hash, not that
version label. The build log confirms recompilation of `ILI9341.c`.
Application SHA-256:
`ff045ad591dc5a843bb42b88bcd5e00995c3e3dd9b40e3f2703222272aa4141b`.

Flashed to the S31 on COM21 at application offset `0x410000`; esptool verified
the written data hash and reset the board. Music Assistant then started
"A Change Is Gonna Come" through MnCast Sendspin. The device reported connected
and playing with an advancing position. This verifies playback state, not
physical display cleanliness.

Local evidence is under `evidence/s31-validation/` in the parent workspace:
`refresh-build-20261105.log`, `refresh-flash-20261105.log`,
`refresh-regression-after.log`, `refresh-wifi-regression.log`, and
`refresh-ma-playing-20261105.png`.

The physical panel must be observed during continued animation before calling
the reported artifact fixed. Host tests alone cannot establish this.

## Physical feedback and second candidate

The user reports that timer digits are still corrupted with the first candidate;
the spectrum may be slightly better, with traces particularly at its edges.
The first candidate is therefore **not a validated fix**.

The second candidate replaces variable horizontal dirty rectangles with fixed,
full-width RGB565 stripes (up to 2048 bytes, whole rows). Only changed stripes
are transmitted, but their black margins are sent too. The shadow buffer is
updated after a successful pixel payload instead of before transmission.
This does not prove the underlying hardware cause. It increases SPI traffic for
narrow changes; the physical validation and short playback check are recorded
below. No high-rate audio endurance claim is made for this change.

Host coverage adds corruption at both ends of a panel row followed by a central
pixel change, and an injected failed payload followed by a successful retry.
Both cases pass with and without the internal staging buffer, at both display
resolutions. The existing animation and Wi-Fi QR regressions also pass.

The second candidate was built and flashed on 2026-11-05, with esptool verifying
the written hash. The device HTTP status reports `s31-fullrows-20261105` and
Sendspin resumed the existing Music Assistant queue ("July Tree", Nina Simone).
Artifact: `squeezelite-s31-fullrows-20261105.bin`, SHA-256
`7c1bb9db007d37a1f0af78d599e839e6bd9688d3cccc212c27fca69b8bd4b984`.
Evidence files share the `refresh-fullrows-` prefix in the same local folder.
On 2026-11-05 the user answered **"Oui, tout est propre"** when asked whether
the timer digits and both ends of the spectrum remained clean after 30 seconds.
The second candidate is therefore validated for the reported visible artifacts
on this physical 320x240 ILI9341 panel. The first candidate remains unsuccessful.

A 40-second HTTP observation during "Gimme Some" showed Sendspin connected and
playing throughout, with position advancing normally (44.1 kHz, 24-bit stream).
The parallel telnet connection returned no log bytes, so it provides no evidence
about underruns or CPU load. This is a short playback-state check, not an audio
continuity or endurance validation.
