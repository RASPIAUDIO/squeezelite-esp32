# Local experimental images — not published

Version: `s31-v0.1.0-exp`, ESP32-S31 Function-CoreBoard-1, 16 MB flash / 16 MB PSRAM,
32-bit PCM pipeline, onboard ES8311 audio defaults.

- `factory.bin`: full first-install image, flashed at 0x0 after erase.
- `squeezelite.bin`: application image, initial OTA slot at 0x410000.
- `recovery.bin`, `bootloader.bin`, `partition-table.bin`, `boot_app0.bin`: individual images.
- `manifest.json`: offsets, sizes, SHA256 values and build-source provenance.
- `SHA256SUMS`: checksums for the six binary files.
- `web-installer-manifest.json`: candidate manifest for a future reviewed installer deployment.

Follow [installation instructions](../../docs/INSTALL.md), particularly the
distinction between first installation and settings-preserving application updates.
Read the [build verification](../../docs/BUILD-VALIDATION.md) and
[qualification limits](../../docs/STATUS.md). These rebuilt images have not been
flashed in this packaging task. Existing hardware validations refer to the prior
development firmware, not a new listening test of these exact bytes.

All files remain local pending the owner's review. No S31 release or binary has
been published to the public fork or to the public web installer.
