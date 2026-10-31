# S31 firmware binaries

Version: `s31-v0.1.0-exp`, ESP32-S31 Function-CoreBoard-1, 16 MB flash / 16 MB PSRAM,
32-bit PCM pipeline, onboard ES8311 audio defaults.

- `factory.bin`: full first-install image, flashed at 0x0 after erase.
- `squeezelite.bin`: application image, initial OTA slot at 0x410000.
- `recovery.bin`, `bootloader.bin`, `partition-table.bin`, `boot_app0.bin`: individual images.
- `manifest.json`: offsets, sizes, SHA256 values and build-source provenance.
- `SHA256SUMS`: checksums for the six binary files.
- `web-installer-manifest.json`: manifest for compatible installers; the RASPIAUDIO webloader is maintained separately.

Follow [installation instructions](../../docs/INSTALL.md), particularly the
distinction between first installation and settings-preserving application updates.
Read the [build verification](../../docs/BUILD-VALIDATION.md) and
[qualification limits](../../docs/STATUS.md). These rebuilt images have not been
flashed in this packaging task. Existing hardware validations refer to the prior
development firmware, not a new listening test of these exact bytes.

Published with the owner's approval in the `s31` branch of
[RASPIAUDIO/squeezelite-esp32](https://github.com/RASPIAUDIO/squeezelite-esp32).
For browser installation, use [apps.raspiaudio.com](https://apps.raspiaudio.com/).
That webloader has its own release cycle; these files retain their original build
version and hashes.
