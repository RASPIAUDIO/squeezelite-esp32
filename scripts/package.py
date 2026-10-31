"""Package a completed S31 build, with clean factory boot selection and SHA256."""
import argparse
import binascii
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-s31")
    parser.add_argument("--output", type=Path, default=ROOT / "firmware/experimental")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    flash = json.loads((build / "flasher_args.json").read_text())
    assert flash["extra_esptool_args"]["chip"] == "esp32s31"
    assert flash["flash_settings"]["flash_size"] == "16MB"
    expected = {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                "0xd000": "ota_data_initial.bin", "0x10000": "recovery.bin", "0x410000": "squeezelite.bin"}
    assert flash["flash_files"] == expected, "Unexpected flash layout; review before packaging"
    sizes = {"recovery.bin": 0x400000, "squeezelite.bin": 0x500000}
    for name, limit in sizes.items():
        assert 0 < (build / name).stat().st_size <= limit, f"{name} exceeds partition"
    output.mkdir(parents=True, exist_ok=True)
    files = {}
    for offset, relative in expected.items():
        if relative == "ota_data_initial.bin":
            # Same sequence/CRC algorithm as ESP-IDF's otatool.py. Select ota_0,
            # not the factory recovery application, on the first boot.
            name = "boot_app0.bin"
            data = bytearray(b"\xff" * 0x2000)
            seq = struct.pack("<I", 1)
            data[:4] = seq
            struct.pack_into("<I", data, 28, binascii.crc32(seq, 0xFFFFFFFF) & 0xFFFFFFFF)
            (output / name).write_bytes(data)
        else:
            name = Path(relative).name
            shutil.copy2(build / relative, output / name)
        files[offset] = name
    merge = [sys.executable, "-m", "esptool", "--chip", "esp32s31", "merge-bin",
             "--flash-mode", "dio", "--flash-size", "16MB", "--flash-freq", "80m",
             "--output", str(output / "factory.bin")]
    for offset, name in files.items():
        merge += [offset, str(output / name)]
    subprocess.run(merge, check=True)
    merged = (output / "factory.bin").read_bytes()
    for offset, name in files.items():
        data = (output / name).read_bytes()
        assert merged[int(offset, 16):int(offset, 16)+len(data)] == data, f"Merge mismatch: {name}"
    assert merged[0x9000:0xd000] == b"\xff" * 0x4000, "Factory NVS must be blank"
    def git(*params):
        return subprocess.check_output(["git", "-C", str(ROOT), *params], text=True).strip()
    manifest = {
        "name": "Squeezelite ESP32-S31", "version": (ROOT / "version.txt").read_text().strip(),
        "status": "experimental", "source_commit": git("rev-parse", "HEAD"),
        "sdk_commit": subprocess.check_output(["git", "-C", os.environ["IDF_PATH"], "rev-parse", "HEAD"], text=True).strip(),
        "sdk_patch_sha256": sha(ROOT / "patches/esp-idf-4f9e257-hal-overflow.patch"),
        "sdkconfig_sha256": sha(ROOT / "sdkconfig.sendspin"),
        "dependency_lock_sha256": sha(ROOT / "dependencies.lock"),
        "audio_depth": 32, "board": "ESP32-S31 Function-CoreBoard-1, 16MB flash / 16MB PSRAM",
        "default_audio": "onboard ES8311", "factory_flash_offset": "0x0",
        "application_flash_offset": "0x410000", "flash_files": files,
        "personal_settings_included": False,
        "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(output.glob("*.bin"))}
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{value['sha256']}  {name}\n" for name, value in manifest["artifacts"].items()))
    installer = {"name": "S31 - Squeezelite (experimental)", "version": manifest["version"],
                 "improv_wait_time": 30, "new_install_prompt_erase": True,
                 "builds": [{"chipFamily": "ESP32-S31", "parts": [{"path": "factory.bin", "offset": 0}]}]}
    (output / "web-installer-manifest.json").write_text(json.dumps(installer, indent=2) + "\n")
    print(json.dumps({"packaged": str(output), "source_commit": manifest["source_commit"], "artifacts": len(manifest["artifacts"])}))

if __name__ == "__main__":
    main()

