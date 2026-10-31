# Qualification scope

This distribution packages the existing S31 port; it does not turn every historical
test into a guarantee for every source, output or sample-rate combination.

- Ethernet playback, ROSCO Wi-Fi playback, local tones, FIP, AirPlay 1 and Bluetooth
  were confirmed by the user. Wi-Fi 5 GHz and Wi-Fi 6 remain unqualified.
- Audio+ V3 left/right output and FLAC 24-bit / 192 kHz over Ethernet were confirmed.
  32-bit refers to the internal PCM/output word path, not newly created source precision.
- EQ (10 bands), loudness and resampling are implemented on that PCM32 path.
  High-rate concurrent DSP remains workload-dependent; forced 176.4-to-192 kHz
  conversion with all effects has shown underflows. Do not advertise all combinations
  as dropout-free. 352.8/384 kHz are not release guarantees with Ethernet enabled.
- AirPlay 2 has passed encrypted software-sender ALAC/AAC, pause/volume/artwork,
  reconnect and AP1/AP2 ownership tests. Tests of about five minutes are not an
  overnight stability or Apple-device/multiroom qualification. AirPlay PCM is not
  claimed to carry 24-bit high-resolution content here.
- Bluetooth SBC/AAC/LDAC playback was confirmed by listening. There is no claim
  of Bluetooth lossless audio.
- Sendspin pairing/streaming and web settings are included. The touchscreen is deferred.
- Improv Serial and the post-flash Wi-Fi flow were confirmed by the user.
- Spotify Connect discovery works but playback still encounters audio-key rejection,
  also observed on the older ESP32 player. This release does not claim to fix it.

The build19 AirPlay 2 enable/disable control persisted through reboot in API/NVS
tests. A visual browser test of that specific checkbox was not completed in the
development session. Public installer deployment is separate from this repository release.

