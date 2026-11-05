#!/usr/bin/env python3
"""follow_pack.py -- a sprite mod for the following Pokemon (sdk/follow.c).

Black and White have no walking sprites for the Unova Pokemon (494 to 649).
This makes a mod that supplies them, from a folder of overworld sheets laid
out the way pokeemerald-expansion keeps its own: graphics/pokemon/<name>/
overworld.png and overworld_normal.pal, with the species numbers read from
include/constants/species.h.  The sprites are not part of eidos; the
sheets are the user's own copy.

A sheet is six frames in a row (down, down, up, up, left, left), 32x32 or
64x64 (halved here), 16 colours with index 0 transparent.  Each species is
written as follow/<species>.bin: the 16 colours (BGR555), then the frames
as the field's sprites store them: up, up, down, down, left, left, 32x32
4bpp, the low nibble first.

    python tools/follow_pack.py PEE_ROOT mods/follow-sprites

Needs Pillow.
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

from PIL import Image

FIRST, LAST = 494, 649
ORDER = (2, 3, 0, 1, 4, 5)          # the sheet's frames in the field's order


def species_names(root: Path) -> dict[int, str]:
    text = (root / "include/constants/species.h").read_text()
    names: dict[int, str] = {}
    for m in re.finditer(r"SPECIES_(\w+)\s*=\s*(\d+)\b", text):
        n = int(m.group(2))
        if FIRST <= n <= LAST and n not in names:
            names[n] = m.group(1).lower()
    return names


def sheet_dir(root: Path, name: str) -> Path | None:
    base = root / "graphics/pokemon"
    for d in (base / name, base / name.split("_")[0]):
        if (d / "overworld.png").exists():
            return d
        sub = sorted(d.glob("*/overworld.png"))
        if sub:
            return sub[0].parent
    return None


def palette(d: Path, img: Image.Image) -> list[tuple[int, int, int]]:
    pal = d / "overworld_normal.pal"
    if pal.exists():
        vals = [int(x) for x in pal.read_text().split()[3:3 + 48]]
    else:
        vals = list((img.getpalette() or [])[:48])
    vals += [0] * (48 - len(vals))
    return [tuple(vals[i * 3:i * 3 + 3]) for i in range(16)]


def halve(frame: Image.Image) -> Image.Image:
    """64x64 to 32x32: the commonest opaque index of each 2x2 block, or
    transparent when fewer than two of the four are opaque."""
    out = Image.new("P", (32, 32))
    dst, src = out.load(), frame.load()
    for y in range(32):
        for x in range(32):
            v = [src[x * 2 + i, y * 2 + j] for j in (0, 1) for i in (0, 1)]
            o = [c for c in v if c]
            dst[x, y] = max(set(o), key=o.count) if len(o) >= 2 else 0
    return out


def pack(d: Path) -> bytes:
    img = Image.open(d / "overworld.png")
    if img.mode != "P":
        raise ValueError(f"{d}: not a 16-colour sheet")
    size = img.height
    frames = [img.crop((i * size, 0, i * size + size, size)) for i in range(6)]
    if size == 64:
        frames = [halve(f) for f in frames]
    out = bytearray()
    for r, g, b in palette(d, img):
        out += struct.pack("<H", (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    for i in ORDER:
        px = frames[i].load()
        for y in range(32):
            for x in range(0, 32, 2):
                out.append((px[x, y] & 15) | ((px[x + 1, y] & 15) << 4))
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: follow_pack.py PEE_ROOT OUT_DIR", file=sys.stderr)
        return 2
    root, dest = Path(sys.argv[1]), Path(sys.argv[2])
    (dest / "follow").mkdir(parents=True, exist_ok=True)
    (dest / "eidos_mod.txt").write_text("name = Following Pokemon sprites\n")
    done = 0
    for n, name in sorted(species_names(root).items()):
        d = sheet_dir(root, name)
        if d is None:
            print(f"no sheet for {n} {name}")
            continue
        (dest / "follow" / f"{n:03d}.bin").write_bytes(pack(d))
        done += 1
    print(f"{done} species written to {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
