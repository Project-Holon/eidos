#!/usr/bin/env python3
"""Writes sdk/preview_save.c: the save the style chooser's preview plays on.

The preview (engine/ui/ui.c, the title menu's style chooser) shows the real
game on a save of its own.  That save is not a file: it is in the program,
packed (raw DEFLATE, sdk/inflate.h), unpacked into memory when the preview
starts and given to the backup chip from memory (sdk/arm7/fs.c,
bw_arm7_backup_set_memory), which then writes nothing to any disk.  It is
never a save slot and the title menu never offers it.

The save itself is made in the game: a fresh game's early save, a party of
six and a filled Bag set by the test switches (docs/test-switches.md:
EIDOS_DEBUG_PARTY, EIDOS_DEBUG_HELD, EIDOS_DEBUG_ITEMS), the player standing
free in Accumula Town, then the game's own Save.  It holds save data only:
no picture, text or code of the game.

    python tools/preview_save.py SAVE.sav          writes sdk/preview_save.c
"""
import os
import sys
import zlib

SIZE = 0x80000      # the backup chip of the game: 512 KiB


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    if len(data) != SIZE:
        sys.exit("%s: %d bytes, not the chip's %d" % (sys.argv[1], len(data), SIZE))
    c = zlib.compressobj(9, zlib.DEFLATED, -15)     # raw DEFLATE, no zlib header
    z = c.compress(data) + c.flush()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sdk", "preview_save.c")
    lines = ["/*",
             " * sdk/preview_save.c -- the save the style chooser's preview plays on,",
             " * packed (raw DEFLATE, sdk/inflate.h).  Written by tools/preview_save.py;",
             " * do not edit.  It is unpacked into memory for the preview only",
             " * (sdk/save_slots.c, bw_slots_try_begin): never a file, never a save slot.",
             " * Save data only: no picture, text or code of the game.",
             " */",
             "",
             "const unsigned int bw_preview_save_size = %du;" % SIZE,
             "const unsigned int bw_preview_save_crc = 0x%08Xu;" % (zlib.crc32(data) & 0xFFFFFFFF),
             "const unsigned int bw_preview_save_z_size = %du;" % len(z),
             "const unsigned char bw_preview_save_z[%d] = {" % len(z)]
    for i in range(0, len(z), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in z[i:i + 16]) + ",")
    lines.append("};")
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: %d bytes packed to %d" % (os.path.normpath(out), SIZE, len(z)))


if __name__ == "__main__":
    main()
