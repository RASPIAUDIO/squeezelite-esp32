# Squeezelite-ESP32 for ESP32-S31 (experimental)

An ESP32-S31 port of [Squeezelite-ESP32](https://github.com/sle118/squeezelite-esp32),
developed for the next RASPIAUDIO MnCast. Includes Ethernet/Wi-Fi, a 32-bit audio
pipeline, Bluetooth Classic, AirPlay, Sendspin, and a local configuration web UI.
This is the local `codex/s31-port` development branch of the
[RASPIAUDIO fork](https://github.com/RASPIAUDIOadmin/squeezelite-esp32).
**Pending review: the public fork currently contains upstream code only.**
The S31 changes and binaries in this working tree have not been pushed publicly.

## Install or build

- Use the local files in [`firmware/experimental`](firmware/experimental).
  A public S31 release will be made only after review and approval.
- Follow [installation instructions](docs/INSTALL.md); **S31/16 MB only**.
- Compile using the pinned SDK and [fresh-clone build instructions](docs/BUILD.md).
- Connect Ethernet, or configure Wi-Fi using Improv Serial or the fallback access point.
  The local web interface contains Services, Sendspin, audio, display and NVS settings.
  AirPlay 2 is enabled by default; disabling it selects the legacy AirPlay 1 receiver
  while the main AirPlay service remains enabled. Save and reboot to apply.

## Hardware and pinout

Test board: **Espressif ESP32-S31 Function-CoreBoard-1**, 16 MB flash / 16 MB PSRAM,
with YT8531 Ethernet, ES8311 codec and NS4150B speaker amplifier.
The default firmware uses the onboard audio: I2C SDA51/SCL50, I2S MCLK52,
BCLK53, LRCLK55, DOUT56, amplifier enable57. Numbers here are **GPIOs**.

Optional **RASPIAUDIO Audio+ V3** and **LCDwiki MSP2834 2.8-inch ILI9341 IPS**:

| Function | S31 GPIO / power | S31 J2 physical pin | Peripheral pin |
|---|---|---|---|
| Audio+ supply | 5 V | 39 | Audio+ 2 |
| Audio+ ground | GND | 38 | Audio+ 6 |
| Audio+ BCLK | GPIO48 | 14 | Audio+ 12 |
| Audio+ LRCLK | GPIO49 | 11 | Audio+ 35 |
| Audio+ data in | GPIO35 | 25 | Audio+ 40 |
| LCD supply | 3.3 V | 35 | LCD 1 / VCC |
| LCD ground | GND | 37 | LCD 2 / GND |
| LCD CS | GPIO40 | 19 | LCD 3 |
| LCD reset | GPIO43 | 17 | LCD 4 |
| LCD DC | GPIO42 | 20 | LCD 5 |
| LCD MOSI | GPIO39 | 21 | LCD 6 |
| LCD clock | GPIO38 | 22 | LCD 7 |

**J2 is not Raspberry Pi pin-compatible: do not stack the Audio+ directly.**
Use the Audio+ top male connector and the corrected [wiring drawing](docs/assets/cablage-audio-v3-s31-v2-corrige.png).
See [hardware settings and orientation](docs/HARDWARE.md) before changing the audio profile.
The LCD is used in landscape 320x240; touchscreen support is not qualified.

## Status

| Feature | Validation / limit |
|---|---|
| Ethernet, Wi-Fi | Playback validated; Wi-Fi 5 GHz / Wi-Fi 6 not qualified |
| FLAC 24-bit / 192 kHz | Validated on external Audio+ V3 with Ethernet; not a claim for the onboard ES8311 |
| 32-bit DSP | 10-band EQ, loudness and resampling implemented; see [limits](docs/STATUS.md) |
| Bluetooth Classic | SBC, AAC and LDAC playback confirmed with Xiaomi |
| AirPlay 1 | Playback and artwork validated |
| AirPlay 2 | Experimental; software-sender protocol tests, Apple/multiroom qualification pending |
| Sendspin | Paired and tested with Music Assistant; local web settings included |
| Display | ILI9341 text/artwork validated; touchscreen deferred |
| Wi-Fi onboarding | Captive portal/QR and Improv Serial tested |
| Spotify Connect | Discoverable, but audio-key rejection remains unresolved; playback not validated |

## Credits and licenses

Thanks to **Sébastien (sle118), Philippe (philippe44)**, the Squeezelite contributors,
emlynmac, Sendspin, Espressif and the codec/library authors. See [provenance](UPSTREAM.md)
and [third-party licenses](THIRD_PARTY_NOTICES.md). Original license notices are retained.
AirPlay 2 includes Rémi Bouteiller's `airplay-esp32` code under its own non-commercial
license; this repository does not relicense it or grant commercial permission.

