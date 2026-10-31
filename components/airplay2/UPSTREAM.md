# AirPlay 2 receiver integration (in progress)

Source: https://github.com/rbouteiller/airplay-esp32
Pinned revision: 811d5f8af750096683dcec7c95d1cbc08c2f7601
License: upstream non-commercial license, retained in LICENSE.

Selected RTSP/HAP, clocks, audio receive/decode and plist sources are vendored.
The original board drivers, Wi-Fi manager, web server and DSP are not imported.
MnCast must keep its existing output/DSP and source arbitration.

Local changes: ESP-IDF 6.1 PSA AES/RSA adaptation; shared libsodium signing
extension; existing mDNS ownership; MnCast audio adapter. See Git diff against
the pinned upstream for the exact changes. Startup clock acquisition, initial
FLUSH handling and diagnostics are also adapted. Short encrypted AP2 ALAC/AAC
and legacy AP1 ALAC tests now pass on S31; full qualification remains pending.
See docs/AIRPLAY2-PORT.md and validation/evidence/airplay2/build9-summary.json
for the evidence and its limits. Build 10 additionally tracks PTP sample source
identity and retains a fresh matching-master lock at the first RTSP anchor;
nine AP1/AP2 reconnect cycles passed. Compilation alone does not validate AirPlay 2.

Later changes normalize seek comparisons to the same clock instant, capture
actual pause duration, preserve display ownership on pause, and serialize RTSP
replacement against shared-state cleanup. A five-minute buffered ALAC test
passes without verbose diagnostics; enabling heavy UART diagnostics caused
11 late-frame drops in the comparison run. See the port document for hashes,
individual positive/negative test evidence and remaining qualification limits.
