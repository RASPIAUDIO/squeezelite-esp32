# Third-party notices

This is a collection of components under their respective licenses, not a new
blanket license over every file. Preserve the individual copyright and license
notices when redistributing. In particular:

- Squeezelite audio engine: GPL-3.0-or-later notices in `components/squeezelite`.
- Squeezelite-ESP32 integration/web code: upstream notices, including MIT portions.
- cspot/Bell and their dependencies: notices in `components/spotify/cspot`.
- AirPlay 2: `components/airplay2/LICENSE` (non-commercial license, Rémi Bouteiller).
- Sendspin: `components/sendspin_lib/LICENSE`.
- LDAC decoder: `components/driver_bt/ldac/LICENSE`.
- Source codecs: individual LICENSE/COPYING files in `vendor/`; origins in `vendor/SOURCES.json`.
- libtelnet: `components/telnet/libtelnet/COPYING`.
- UML state machine: `components/wifi-manager/UML-State-Machine-in-C/LICENSE`.
- ESP-IDF and managed components: licenses distributed with the pinned SDK/registry packages.

Some Espressif components contain vendor-provided binary libraries. Their source
is not part of this project; the dependency lock identifies the exact packages.
Before any public/commercial distribution, review the combined licensing terms,
particularly the experimental AirPlay 2 component. No commercial rights are implied.

