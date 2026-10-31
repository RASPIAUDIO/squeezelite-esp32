"""Verify the pinned ESP-IDF and apply the reviewed clock arithmetic patch."""
import argparse
import os
from pathlib import Path
import subprocess

IDF_COMMIT = "fff9895c82d744c7237be8847347bdd1b07c6643"
ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path, default=os.environ.get("IDF_PATH"))
    args = parser.parse_args()
    if not args.idf:
        parser.error("Set IDF_PATH or supply --idf")
    sdk = args.idf.resolve()
    def git(*params, **kwargs):
        return subprocess.run(["git", "-C", str(sdk), *params], capture_output=True, text=True, **kwargs)
    revision = git("rev-parse", "HEAD", check=True).stdout.strip()
    if revision != IDF_COMMIT:
        raise SystemExit(f"Expected ESP-IDF {IDF_COMMIT}; found {revision}. Use a dedicated pinned SDK checkout.")
    patch = str(ROOT / "patches/esp-idf-4f9e257-hal-overflow.patch")
    reverse = git("apply", "--reverse", "--check", patch)
    if reverse.returncode == 0:
        print("ESP-IDF clock patch already present")
    else:
        result = git("apply", "--check", patch)
        if result.returncode:
            raise SystemExit("SDK differs from the expected patch base: " + result.stderr)
        git("apply", patch, check=True)
        print("Applied ESP-IDF clock arithmetic patch")
    changed = git("diff", "--name-only", check=True).stdout.splitlines()
    if changed != ["components/hal/hal_utils.c"]:
        raise SystemExit(f"Unexpected SDK modifications: {changed}")
    print(f"Pinned ESP-IDF verified: {revision}")

if __name__ == "__main__":
    main()

