# Building the S31 experimental release

The supported, tested host for this release is Windows with PowerShell and Python 3.12.
Use a short local path if Windows tools encounter path limits. Linux is not yet qualified.
No files from the original development workspace are required.

## Toolchain

Use ESP-IDF **6.1 at commit `fff9895c82d744c7237be8847347bdd1b07c6643`**, not a moving branch.
In a dedicated directory:

```powershell
git clone https://github.com/espressif/esp-idf.git esp-idf-s31
cd esp-idf-s31
git checkout fff9895c82d744c7237be8847347bdd1b07c6643
git submodule update --init --recursive
./install.ps1 esp32s31
./export.ps1
```

The pinned SDK selects the compiler/tools; the original build used RISC-V GCC
15.2.0_20251204 and esptool 5.6.0. The build script checks the SDK revision and
applies the included HAL arithmetic patch. Use a dedicated SDK checkout because
that one SDK source file is modified. The LDAC adaptations are generated inside
the build directory, without additional SDK edits.

## Build from a fresh clone

```powershell
git clone --branch codex/s31-port <path-to-local-fork> s31-buildcheck
cd s31-buildcheck
python -m pip install -r scripts/requirements-build.txt
./scripts/build.ps1
python scripts/package.py --build build-s31 --output firmware/experimental
```

Until the S31 branch is approved for publication, clone the local fork repository
as shown above. The public fork at `RASPIAUDIOadmin/squeezelite-esp32` contains
only the original upstream branches. After publication, its URL can replace
`<path-to-local-fork>`; do not expect the S31 branch to be available remotely yet.

All project Git dependencies are vendored source snapshots, including the
modified Sendspin library. Their revisions are in `docs/vendor-snapshots.json`;
codec revisions are in `vendor/SOURCES.json`. No project submodule update is needed.
ESP-IDF registry components are downloaded according to `dependencies.lock`.
This is a complete project source checkout, not an offline copy of the entire SDK:
Espressif also supplies proprietary binary radio/audio libraries through the SDK
and component registry. Their sources cannot be supplied by this project.

The release uses `sdkconfig.sendspin`, **DEPTH=32**, and `partitions-s31.csv`.
Other historical configurations/build scripts are retained as upstream references,
not supported S31 release recipes. `SPOTIFY_SECRET` must be unset for release builds.

The firmware build embeds the committed web assets. Their editable sources are in
`components/wifi-manager/webapp/src`; the upstream web package and webpack config
are retained in that directory. A Node/webpack rebuild is separate from the firmware
build and is not required for a fresh firmware compilation.

See `firmware/experimental/manifest.json` for image hashes and exact build provenance.
The package script produces only build artifacts: it never reads the device or NVS.

