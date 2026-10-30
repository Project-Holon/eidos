#!/usr/bin/env python3
"""levelup_cases.py -- Pokemon and levels for testing level-ups.

Reads the cartridge's own tables (the base stats a/0/1/6, the level-up
moves a/0/1/8, the evolutions a/0/1/9, the species names a/0/0/2 file 70)
and lists, for the levels asked, the species whose NEXT level does one of:

  learn    teaches a move while the Pokemon already knows four
  evolve   evolves it by level
  both     both at once

A Pokemon made at a level knows the last four moves it learnt up to that
level (POKE_TOOL_SetupPartyParam), which is what "knows four" checks.

    python tools/levelup_cases.py ROM.nds [--max-level 30] [--limit 8]

The output's lines are what EIDOS_DEBUG_PARTY takes: "species:level:-1"
puts the Pokemon one experience point short of that next level.
"""

import argparse
import struct
import sys


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


class Rom:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        self.fnt, _fnt_size, self.fat = struct.unpack_from('<III', self.b, 0x40)

    def file_id(self, path):
        d = 0
        for name in path.split('/'):
            sub_off = u32(self.b, self.fnt + d * 8)
            fid = u16(self.b, self.fnt + d * 8 + 4)
            p = self.fnt + sub_off
            while True:
                l = self.b[p]
                p += 1
                if l == 0:
                    raise KeyError(path)
                n = self.b[p:p + (l & 0x7F)].decode('ascii')
                p += l & 0x7F
                if l & 0x80:
                    did = u16(self.b, p)
                    p += 2
                    if n == name:
                        d = did & 0xFFF
                        break
                elif n == name:
                    return fid
                else:
                    fid += 1

    def file(self, path):
        s, e = struct.unpack_from('<II', self.b, self.fat + self.file_id(path) * 8)
        return self.b[s:e]

    def narc(self, path):
        b = self.file(path)
        btaf = 0x10
        n = u32(b, btaf + 8)
        ents = [struct.unpack_from('<II', b, btaf + 12 + i * 8) for i in range(n)]
        btnf = btaf + u32(b, btaf + 4)
        gmif = btnf + u32(b, btnf + 4)
        return [b[gmif + 8 + s:gmif + 8 + e] for s, e in ents]


def msg_strings(data):
    """A Gen V message file's first section, decrypted."""
    n_sec, n_ent = struct.unpack_from('<HH', data, 0)
    sec_off = u32(data, 0x0C)
    out = []
    for i in range(n_ent):
        off, count = struct.unpack_from('<IH', data, sec_off + 4 + i * 8)
        chars = list(struct.unpack_from('<%dH' % count, data, sec_off + off))
        key = chars[-1] ^ 0xFFFF
        for k in range(count - 1, -1, -1):
            chars[k] ^= key
            key = ((key >> 3) | (key << 13)) & 0xFFFF
        s = ''
        for c in chars:
            if c == 0xFFFF:
                break
            s += chr(c) if c < 0xE000 else '?'
        out.append(s)
    return out


EVO_LEVEL_METHODS = {4, 9, 10, 11, 12, 13, 14, 15}   # by level (the plain one and its variants)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--max-level', type=int, default=30)
    ap.add_argument('--limit', type=int, default=8)
    a = ap.parse_args(argv)
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')   # names hold the gender signs
    rom = Rom(a.rom)
    learn = rom.narc('a/0/1/8')
    evo = rom.narc('a/0/1/9')
    try:
        names = msg_strings(rom.narc('a/0/0/2')[70])
    except Exception:
        names = []

    def name(sp):
        return names[sp] if sp < len(names) else '#%d' % sp

    rows = {'learn': [], 'evolve': [], 'both': []}
    for sp in range(1, min(650, len(learn))):
        moves = []
        b = learn[sp]
        for o in range(0, len(b) - 3, 4):
            mv, lv = struct.unpack_from('<HH', b, o)
            if mv == 0xFFFF:
                break
            moves.append((lv, mv))
        evos = []
        e = evo[sp] if sp < len(evo) else b''
        for o in range(0, len(e) - 5, 6):
            method, param, target = struct.unpack_from('<HHH', e, o)
            if method in EVO_LEVEL_METHODS and target:
                evos.append((param, target, method))
        for lv in range(2, a.max_level):
            known = [m for l, m in moves if l <= lv]
            knows4 = len(dict.fromkeys(known)) >= 4
            new = [m for l, m in moves if l == lv + 1 and m not in known]
            ev = [t for p, t, m in evos if m == 4 and p == lv + 1]
            if new and knows4 and ev:
                rows['both'].append((sp, lv, ev[0]))
            elif new and knows4:
                rows['learn'].append((sp, lv, None))
            elif ev:
                rows['evolve'].append((sp, lv, ev[0]))
    for kind in ('learn', 'evolve', 'both'):
        print('%s:' % kind)
        for sp, lv, target in sorted(rows[kind], key=lambda r: (r[1], r[0]))[:a.limit]:
            extra = (' -> %s' % name(target)) if target else ''
            print('  %-12s %d:%d:-1   (lv %d -> %d%s)' % (name(sp), sp, lv, lv, lv + 1, extra))
    return 0


if __name__ == '__main__':
    sys.exit(main())
