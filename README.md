# Squeezelite-ESP32 for ESP32-S31

**More power. More audio features.**

High-quality Bluetooth audio, native Gigabit Ethernet, FLAC playback up to
**24-bit / 192 kHz**, and **AirPlay 2**: this ESP32-S31 port extends
[Squeezelite-ESP32](https://github.com/sle118/squeezelite-esp32) for the next
RASPIAUDIO MnCast, with a 32-bit audio pipeline and more ways to play your music.

- **High-quality Bluetooth audio:** LDAC and AAC, with SBC fallback; playback
  confirmed with a Xiaomi phone.
- **Native Gigabit Ethernet:** the S31 Ethernet MAC connects directly to the
  onboard YT8531 PHY over RGMII. Wired audio playback is validated; Gigabit describes
  the hardware interface, not a measured 1 Gbit/s application throughput.
- **High-resolution FLAC:** 24-bit / 192 kHz playback validated over Ethernet
  with the external RASPIAUDIO Audio+ V3 DAC.
- **AirPlay 2 and AirPlay 1:** an AirPlay 2 receiver with artwork support and a
  selectable legacy AirPlay 1 mode. See the qualification details below.
- **32-bit audio processing:** 10-band equalizer, loudness and resampling,
  with high-rate workload limits documented in [status](docs/STATUS.md).
- **Sendspin:** a Music Assistant client for playback from your Home Assistant
  audio setup, with connection and pairing controls in the local web interface.
- **Color display and easy setup:** track information and album artwork on an
  ILI9341 screen, plus Improv Serial and a QR-assisted captive portal for Wi-Fi.

The board's Gigabit Ethernet capability is documented by
[Espressif](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html).

## Install or build

- Use the local [firmware binaries](firmware/experimental).
  A public S31 release will be made only after review and approval.
- Follow [installation instructions](docs/INSTALL.md); **S31/16 MB only**.
- Compile using the pinned SDK and [fresh-clone build instructions](docs/BUILD.md).
- Review the successful [fresh-clone build verification](docs/BUILD-VALIDATION.md)
  and the [local review checklist](docs/REVIEW.md) before publication.
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
| Native Gigabit Ethernet, Wi-Fi | Audio playback validated; Ethernet throughput not benchmarked at 1 Gbit/s; Wi-Fi 5 GHz / Wi-Fi 6 not qualified |
| FLAC 24-bit / 192 kHz | Validated on external Audio+ V3 with Ethernet; not a claim for the onboard ES8311 |
| 32-bit DSP | 10-band EQ, loudness and resampling implemented; see [limits](docs/STATUS.md) |
| Bluetooth Classic | SBC, AAC and LDAC playback confirmed with Xiaomi |
| AirPlay 1 | Playback and artwork validated |
| AirPlay 2 | Software-sender protocol tests passed; Apple-device/multiroom qualification pending |
| Sendspin | Paired and tested with Music Assistant; local web settings included |
| Display | ILI9341 text/artwork validated; touchscreen deferred |
| Wi-Fi onboarding | Captive portal/QR and Improv Serial tested |
| Spotify Connect | Discoverable, but audio-key rejection remains unresolved; playback not validated |

## Development and review

This is the local `codex/s31-port` development branch of the
[RASPIAUDIO fork](https://github.com/RASPIAUDIOadmin/squeezelite-esp32).
The public fork currently contains upstream code only. The S31 changes and binaries
remain local pending review and approval for publication.

## Credits and licenses

Thanks to **Sébastien (sle118), Philippe (philippe44)**, the Squeezelite contributors,
emlynmac, Sendspin, Espressif and the codec/library authors. See [provenance](UPSTREAM.md)
and [third-party licenses](THIRD_PARTY_NOTICES.md). Original license notices are retained.
AirPlay 2 includes Rémi Bouteiller's `airplay-esp32` code under its own non-commercial
license; this repository does not relicense it or grant commercial permission.

