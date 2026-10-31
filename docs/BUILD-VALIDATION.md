# Fresh-clone build verification — 2026-10-31

**Passed:** a new clone of the prepared source snapshot was configured and built
with ESP-IDF 6.1 at the pinned revision. Both recovery and Squeezelite images were
generated successfully. The build used a new project/build directory and the
existing pinned SDK/toolchain installation; ccache was enabled. This is a successful
clean-checkout build, not a claim of identical bytes across different build dates.

During the build, the owner chose a real public upstream fork with **local-only S31
changes pending review**. The local fork branch contains the same firmware sources
as the clean build. A comparison of all 5,686 source-snapshot files found only three
documentation changes (README, provenance, build instructions); all sdkconfig
values and dependency versions are unchanged. Full details are in
[`build-validation.json`](build-validation.json).

| Artifact/check | Result |
|---|---|
| Application (`squeezelite.bin`) | 4,128,496 bytes; below the 5 MiB OTA slot limit |
| Recovery (`recovery.bin`) | 1,695,392 bytes; below the 4 MiB recovery slot limit |
| Factory image | Bootloader, partition table, clean OTA selector, recovery and application |
| Image inspection | ESP32-S31, 16 MB flash, DIO 80 MHz; checksum and image hash valid |
| Partition and merge checks | Passed; each merged segment matches its source image |
| NVS data in factory image | Blank; no device settings were read or copied |
| ESP-IDF patch | Applied to a pristine pinned checkout; second run was idempotent |
| Vendored Sendspin | All 230 files match the patched development sources |
| Hardware test in this packaging task | None; the board was not flashed |

The firmware sources were built at snapshot commit
`8e83e08a15dd0598cf1b3a4439dd41b6f2bfb47c`; equivalent local fork source commit:
`e6bafac3ecb9d9c75e460e8b1e35e18f34ac9974`.
The manifest records both, without pretending the binaries came from a different
build checkout. Some upstream/SDK deprecation and CMake warnings remain.

Review the [build log](build-evidence/fresh-clone-build.txt),
[application inspection](build-evidence/application-image-info.txt),
[artifact manifest](../firmware/experimental/manifest.json), and
[review checklist](REVIEW.md). Exact generated configuration and dependency-lock
files are retained beside the log for comparison with the manifest hashes.

