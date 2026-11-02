# Wi-Fi setup screen rendering

The recovery screen keeps the existing Wi-Fi QR payload and captive portal.
It does not add a second QR, a touchscreen dependency, or LVGL.

Changes:

- RASPIAUDIO blue (`#4064d7`), white text and a black background.
- Insets around all content, a separate QR column and a full-width credentials area.
- Space for the maximum 32-byte SSID and 64-byte password, including QR escaping.
- Exact-width filled QR modules. The previous calls supplied an inclusive right
  coordinate to `GDS_DrawBox`, whose filled rectangle omits that last column.
- Restore the previous font, font flags and text width when leaving the renderer.

## Reproducible host validation

Requires Clang and Python with `opencv-python`:

```sh
python scripts/tests/test_wifi_setup.py --out /path/to/test-output
```

The harness compiles the production renderer, Nayuki encoder, GDS font and drawing
code, and ILI9341 driver. It checks normal, escaped, maximum-length and open-network
credentials at 320x240 and 480x320. OpenCV independently decodes the QR. Additional
checks cover solid finder borders, safe margins, restored text state, and exact
framebuffer reconstruction from ILI9341 address-window/data transfers, including
replacement of a previous screen and an unchanged refresh.

These host checks do not replace physical verification of panel inversion, SPI
signal integrity, readability or phone scanning. The documented MSP2834 IPS panel
requires the `invert` layout flag; see [Hardware](HARDWARE.md#lcd).

Test output uses synthetic/default example credentials. Never commit real NVS
dumps or saved Wi-Fi passwords.
