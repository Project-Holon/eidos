#!/usr/bin/env python3
"""Supply the DS packet alignment through a Clang virtual header overlay.

Aletheia remains unchanged. This is deliberately scoped to the one wire
record whose long long uses CodeWarrior's four-byte member alignment; all
source size checks and the host libc's own layouts remain in force.
"""
import argparse
import json
from pathlib import Path
import re


def packet_header(text):
    pattern = r"(typedef struct GdsPacketDownload \{.*?\} GdsPacketDownload;)"
    matches = list(re.finditer(pattern, text, re.S))
    if len(matches) != 1 or "long long key;" not in matches[0].group():
        raise ValueError("GdsPacketDownload changed; review its DS layout")
    return re.sub(pattern, r"#pragma pack(push, 4)\n\1\n#pragma pack(pop)",
                  text, count=1, flags=re.S)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--aletheia-root", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    header = args.aletheia_root.resolve() / "src/game/ov125/gds_rap_types.h"
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    patched = out / header.name
    patched.write_text(packet_header(header.read_text()))
    overlay = {"version": 0, "use-external-names": False, "roots": [
        {"type": "file", "name": str(header), "external-contents": str(patched)}]}
    (out / "headers.json").write_text(json.dumps(overlay, indent=2) + "\n")


if __name__ == "__main__":
    main()
