"""Namespace codec internals in a build-only copy of Espressif's archive.

Its public esp_* API and weak platform hooks remain unchanged. The managed
component and its license are untouched. Renaming both definitions and references
prevents incompatible FLAC/Vorbis implementations from satisfying player symbols.
"""
import argparse
import pathlib
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ("nm", "objcopy", "input", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    listing = subprocess.run(
        [args.nm, "-g", "--defined-only", args.input],
        check=True, capture_output=True, text=True,
    ).stdout
    symbols = set()
    for line in listing.splitlines():
        match = re.fullmatch(r"[0-9a-fA-F]+\s+([A-Za-z])\s+(\S+)", line.strip())
        if not match:
            continue
        kind, symbol = match.groups()
        # Weak symbols intentionally bind to the platform's logging/memory hooks.
        if kind.upper() in ("W", "V", "U") or symbol.startswith("esp_"):
            continue
        symbols.add(symbol)
    if not {"FLAC__stream_decoder_new", "ov_clear"}.issubset(symbols):
        raise RuntimeError("Unexpected codec archive: expected FLAC/Vorbis exports missing")
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    mapping = output.with_suffix(".symbols")
    mapping.write_text("".join(f"{s} mn_btcodec_{s}\n" for s in sorted(symbols)), encoding="ascii")
    subprocess.run([args.objcopy, "--redefine-syms=" + str(mapping), args.input, str(output)], check=True)
    print(f"Isolated {len(symbols)} private audio codec symbols")


if __name__ == "__main__":
    main()
