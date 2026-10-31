# Provenance

- Original project: https://github.com/sle118/squeezelite-esp32 (sle118, philippe44 and contributors).
- Initial porting source: https://github.com/emlynmac/squeezelite-esp32,
  branch `migration-5.5-emlynmac`, commit `07d30d9249614e0483762b0a2c938f07a296aa72`.
- S31 development snapshot: private `RASPIAUDIOadmin/MNCAST_ESP31`,
  commit `b8612a29f233db65a432ff8cda45f6c29a292c36`, plus the explicitly included
  Sendspin patch recorded in `patches/sendspin-9a15c83-timing-diagnostics.patch`.
- ESP-IDF: `fff9895c82d744c7237be8847347bdd1b07c6643`, with the included HAL patch.
- AirPlay 2: https://github.com/rbouteiller/airplay-esp32,
  commit `811d5f8af750096683dcec7c95d1cbc08c2f7601`; adapter and local changes in
  `components/airplay2`, upstream license retained there.
- Codec snapshots: `vendor/SOURCES.json`.
- Former submodules, now vendored for complete source archives: `docs/vendor-snapshots.json`.
- Registry dependencies and checksums: `dependencies.lock`.

The public GitHub fork is `RASPIAUDIOadmin/squeezelite-esp32`, with parent
`sle118/squeezelite-esp32`. The local `codex/s31-port` branch is based on upstream
`master-v4.3` commit `ee1720c7556ffec5f43329aadd47c20546bdbb79` and retains that history.
It integrates the S31 distribution snapshot prepared in private commit
`8e83e08a15dd0598cf1b3a4439dd41b6f2bfb47c`, whose migration provenance is listed above.
No private research history, credentials, recordings or device backups are imported.
The S31 branch and binaries remain local until Olivier has reviewed and approved
publication. The existing private snapshot repository is retained as a backup.
The original project structure is kept at the root to make upstream comparison easier.
To follow upstream locally: `git remote add upstream https://github.com/sle118/squeezelite-esp32.git`.

All code and notices supplied by the original projects retain their respective
licenses. S31 changes include RISC-V source codecs, board/IDF6 migration, 32-bit
DSP, Bluetooth codec integration, Sendspin, Improv, and experimental AirPlay 2.

