# Install on the S31 Function-CoreBoard-1

These images are for **ESP32-S31, 16 MB flash / 16 MB PSRAM**, not ESP32, S3 or
Raspberry Pi. Factory defaults use the onboard ES8311 and speaker amplifier.
Configure the external Audio+ profile separately if that is your connected output.

Install esptool 5.6.0 (or use the pinned IDF Python environment). Replace COM21 with
your actual serial port. Verify image hashes against `SHA256SUMS` first.

## First installation: resets existing settings

The factory image contains the bootloader, partition table, clean OTA boot selector,
recovery and application. It contains **no device NVS backup, Wi-Fi credentials,
Spotify credentials, Bluetooth bonds or Sendspin pairing keys**.
Erase flash for a genuinely clean first installation; this also clears the separate
settings partition that is beyond the merged image.

```powershell
python -m esptool --chip esp32s31 --port COM21 erase-flash
python -m esptool --chip esp32s31 --port COM21 write-flash 0x0 firmware/experimental/factory.bin
```

Ethernet can be used directly. For Wi-Fi use Improv Serial in a compatible Chromium
Web Serial installer, or the fallback access point/captive portal. A Web installer
manifest is supplied; the custom public RASPIAUDIO installer is maintained separately
and is not automatically updated by this private release.

## Application update: existing matching S31 partition layout only

`squeezelite.bin` is an application image, not a factory image. Use the device's
application update mechanism where supported. A serial update of the initial
`ota_0` slot is:

```powershell
python -m esptool --chip esp32s31 --port COM21 write-flash 0x410000 firmware/experimental/squeezelite.bin
```

This does not erase NVS, but it updates only `ota_0`: if your device is running
`ota_1` following an OTA update, use its update mechanism or select the correct boot
slot. Do not write the factory image as a routine settings-preserving update.

The layout is in `partitions-s31.csv`; individual binaries and offsets are recorded
in `manifest.json`. Application size must stay below 0x500000 bytes and recovery
below 0x400000. Local package validation checks these bounds and merged segments.

