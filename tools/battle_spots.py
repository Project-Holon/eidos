#!/usr/bin/env python3
"""
tools/battle_spots.py -- the battle spot table of the one-screen battle
backdrop (docs/battle-backdrop.md, engine/world/bmap_spots.c).

Every place a battle can start gets one row:

  T  a trainer: one that sees the player (event type 1, its sight range in
     the NPC record), a gym leader the player talks to, or a character whose
     own script starts a trainer battle (N, the rivals, Team Plasma: the
     zone's script file holds a StartTrainerBattle command, 0x85, in the
     script the character runs).  The battle starts on a tile of its sight
     line or beside it.
  A  a wild area: one patch of encounter tiles (grass, cave floor, sand,
     water; the attribute's encounter flag) in a zone with an encounter
     table.  The battle starts on any tile of the patch.

For each row the tool also works out where a player naturally is when the
battle starts (the "preview" position): a walk over the zone's walk grid from
its entrances (warps and the zone's edges) gives the tile of the sight line
or of the patch that is reached first, and the direction the player came
from.  The spot editor shows the battle there first.

Commands:
  gen   --data DIR        build engine/world/battle_spots.tsv from the
                          extracted ROM (DIR holds a/0/0/8 and the rest);
                          rows already in the table keep their placement
  merge FILE              take the placements of FILE (the spot editor's
                          battle_spots.tsv) into the table
  inc                     write engine/world/battle_spots_tsv.inc (the table
                          as a C string, built into eidos)

gen and merge also write the .inc.
"""

import argparse
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE = os.path.join(HERE, '..', 'engine', 'world', 'battle_spots.tsv')
INC = os.path.join(HERE, '..', 'engine', 'world', 'battle_spots_tsv.inc')

COLUMNS = ['id', 'kind', 'zone', 'label', 'tx', 'tz', 'dir', 'range',
           'rx0', 'rz0', 'rx1', 'rz1', 'px', 'pz', 'pf',
           'mode', 'sx', 'sz', 'angle', 'ax0', 'az0', 'ax1', 'az1', 'ok', 'fl', 'fd', 'iz', 'tid', 'iy', 'ih', 'is', 'it', 'ia',
           'cz', 'cy', 'ch', 'cs', 'ct', 'ca', 'tfx', 'tfy', 'tfz', 'tux', 'tuy', 'tuz', 'vw', 'vm', 'tn', 'tsz', 'tos']
PLACEMENT = ['mode', 'sx', 'sz', 'angle', 'ax0', 'az0', 'ax1', 'az1', 'ok', 'fl', 'fd', 'iz', 'iy', 'ih', 'is', 'it', 'ia',
             'cz', 'cy', 'ch', 'cs', 'ct', 'ca', 'tfx', 'tfy', 'tfz', 'tux', 'tuy', 'tuz', 'vm', 'tsz', 'tos']

WATER = {0x14, 0x17, 0x1C, 0x3D, 0x3E, 0x3F, 0x42, 0x43}
GYMS = {7: 'Striaton Gym', 18: 'Nacrene Gym', 19: 'Nacrene Gym', 29: 'Castelia Gym',
        63: 'Nimbasa Gym', 97: 'Driftveil Gym', 108: 'Mistralton Gym',
        114: 'Icirrus Gym', 121: 'Opelucid Gym'}
STRAY_TRAINERS = {30}
# Black City (Black) and White Forest (White): their residents are not in
# the zone's event file.  The game places them (ov020 field_wfbc_data_local.c)
# at the tiles of a table in archive a/1/5/4, member 0x22 for Black City and
# 0x23 for White Forest: ten (x, z) byte pairs, the residents' places, before
# the items'.  A resident the player talks to can battle; White Forest's grass
# has wild Pokemon although its zone header names no encounter table.
WFBC = {0: 0x22, 424: 0x23}
WFBC_WILD = {424}
LEADER_CODES = {85, 86, 87, 88, 89, 90, 91, 162, 178, 179}
# NPC dir codes: 0 up (-z), 1 down (+z), 2 left (-x), 3 right (+x)
DIRV = {0: (0, -1), 1: (0, 1), 2: (-1, 0), 3: (1, 0)}


def facing_deg(vx, vz):
    """The game's facing as degrees: 0 north (-z), 90 west, 180 south, 270 east."""
    return {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(vx, vz)]


# ---------------------------------------------------------------- ROM data

def narc(path):
    d = open(path, 'rb').read()
    off, fat, img = 16, None, None
    while off < len(d):
        mg = d[off:off + 4]
        sz = struct.unpack_from('<I', d, off + 4)[0]
        if mg == b'BTAF':
            n = struct.unpack_from('<I', d, off + 8)[0]
            fat = [struct.unpack_from('<II', d, off + 12 + 8 * i) for i in range(n)]
        if mg == b'GMIF':
            img = off + 8
        off += sz
    return [d[img + a:img + b] for a, b in fat]


def msg_bank(member):
    """One text bank of a/0/0/2, decrypted (plain text only)."""
    ne = struct.unpack_from('<H', member, 2)[0]
    bo = struct.unpack_from('<I', member, 12)[0]
    out = []
    for k in range(ne):
        o, n, _ = struct.unpack_from('<IHH', member, bo + 4 + 8 * k)
        key = (0x7C89 + 0x2983 * k) & 0xFFFF
        s = ''
        for j in range(n):
            c = struct.unpack_from('<H', member, bo + o + 2 * j)[0] ^ key
            key = ((key << 3) | (key >> 13)) & 0xFFFF
            if c == 0xFFFF:
                break
            if c in (0x246D, 0x246E):       # the male and female signs
                s += '♂' if c == 0x246D else '♀'
            elif c in (0x2486, 0x2487):     # the PK and MN glyphs
                s += 'Pokemon' if c == 0x2486 else ''
            else:
                s += chr(c) if c < 0xD800 else ''
        out.append(s)
    return out


class Rom:
    def __init__(self, root):
        a = os.path.join(root, 'a')
        self.zh = narc(os.path.join(a, '0', '1', '2'))[0]
        self.mats = narc(os.path.join(a, '0', '0', '9'))
        self.chunks = narc(os.path.join(a, '0', '0', '8'))
        self.events = narc(os.path.join(a, '1', '2', '5'))
        self.trdata = narc(os.path.join(a, '0', '9', '2'))
        self.scripts = narc(os.path.join(a, '0', '5', '7'))
        msgs = narc(os.path.join(a, '0', '0', '2'))
        self.places = msg_bank(msgs[89])
        self.classes = msg_bank(msgs[191])
        self.wfbc = narc(os.path.join(a, '1', '5', '4'))
        self.species = msg_bank(msgs[70])
        self.nzones = len(self.zh) // 48

    def zone(self, z):
        h = self.zh[z * 48:z * 48 + 48]
        return dict(matrix=struct.unpack_from('<H', h, 4)[0],
                    encount=struct.unpack_from('<H', h, 0x14)[0],
                    event=struct.unpack_from('<H', h, 0x16)[0],
                    place=struct.unpack_from('<H', h, 0x1A)[0] & 0x3FF)

    def name(self, z):
        if z in GYMS:
            return GYMS[z]
        p = self.zone(z)['place']
        return self.places[p] if p < len(self.places) and self.places[p] else 'Zone %d' % z

    def tiles(self, z):
        """{(tx, tz): (cls, terrain, encounter)} over the zone's own cells;
        cls 'open', 'water' or 'blocked' (open beats water beats blocked over
        the planes of a two-plane chunk)."""
        m = self.mats[self.zone(z)['matrix']]
        mode = struct.unpack_from('<H', m, 0)[0]
        w, h = struct.unpack_from('<HH', m, 4)
        cells = struct.unpack_from('<%dI' % (w * h), m, 8)
        zones = struct.unpack_from('<%dI' % (w * h), m, 8 + 4 * w * h) if mode == 1 else None
        if zones is not None and z not in zones:
            zones = None
        rank = {'blocked': 0, 'water': 1, 'open': 2}
        out = {}
        for ci, c in enumerate(cells):
            if c == 0xFFFFFFFF or c >= len(self.chunks):
                continue
            if zones is not None and zones[ci] != z:
                continue
            b = self.chunks[c]
            if b[:2] == b'NG':
                continue
            planes = 2 if b[:2] == b'GC' else 1
            cx, cz = ci % w, ci // w
            for pl in range(planes):
                at = struct.unpack_from('<I', b, 8 + 4 * pl)[0]
                if at + 4 + 8192 > len(b) or struct.unpack_from('<HH', b, at) != (32, 32):
                    continue
                for i in range(1024):
                    attr = struct.unpack_from('<I', b, at + 4 + 8 * i + 4)[0] & 0x7FFFFFFF
                    t, flags = attr & 0xFFFF, attr >> 16
                    k = 'water' if t in WATER else ('blocked' if flags & 1 else 'open')
                    key = (cx * 32 + i % 32, cz * 32 + i // 32)
                    old = out.get(key)
                    enc = bool(flags & 4) or (old is not None and old[2])
                    if old is None or rank[k] > rank[old[0]]:
                        out[key] = (k, t, enc)
                    else:
                        out[key] = (old[0], old[1], enc)
        return out

    def event_file(self, z):
        f = self.events[self.zone(z)['event']]
        if len(f) < 8:
            return [], []
        nbg, nnpc, ncon = f[4], f[5], f[6]
        off = 8 + nbg * 0x14
        npcs = []
        for i in range(nnpc):
            r = off + i * 0x24
            h = struct.unpack_from('<10H', f, r)
            if struct.unpack_from('<I', f, r + 0x18)[0] != 0:
                continue                    # a rail position
            gx, gz = struct.unpack_from('<HH', f, r + 0x1C)
            npcs.append(dict(obj=h[0], code=h[1], move=h[2], type=h[3], script=h[5],
                             dir=h[6], sight=h[7], gx=gx, gz=gz))
        off += nnpc * 0x24
        warps = []
        for i in range(ncon):
            r = off + i * 0x14
            pos_type = struct.unpack_from('<H', f, r + 6)[0]
            if pos_type != 0:
                continue
            x, _y, zz, sx, sz = struct.unpack_from('<hhhHH', f, r + 8)
            warps.append((x, zz, max(sx, 1), max(sz, 1)))
        return npcs, warps

    def story_battles(self, z):
        """{script number: [fight, ...]} of the zone's scripts that start a
        battle, a fight being a dict (kind 'T' with tid and tn, or 'W' with
        species and level).  The commands: 0x85 StartTrainerBattle (u16
        trainer, second, third), 0x86 StartMultiTrainerBattle (ally, then
        two opponents), 0x94 StartTrainerBattleWithMon, 0x178
        StartScriptedWildBattle (species, level, flags).  The script file
        begins with u32 offsets (each from the end of its own field) ended by
        u16 0xFD13; the commands are u16 numbers with their arguments.  A byte
        scan: the arguments must be trainer ids or 0, so a stray number in the
        data is rare and costs at most a row to check by eye (trainer 30 starts a run of
        stray words in Castelia City: its 0x86 and 0x94 are skipped, its
        0x85 stays as the table had it).  Commands less than 100 bytes apart
        are the versions of one fight (the opponent depends on the player's
        starter); the ids of a fight are consecutive, first and tn of them."""
        sid = struct.unpack_from('<H', self.zh, z * 48 + 6)[0]
        if sid >= len(self.scripts):
            return {}
        f = self.scripts[sid]
        ents, o = [], 0
        while o + 4 <= len(f) and struct.unpack_from('<H', f, o)[0] != 0xFD13:
            ents.append(o + 4 + struct.unpack_from('<i', f, o)[0])
            o += 4
        ntr = len(self.trdata)
        hits = collections.defaultdict(list)        # script -> [(offset, what)]
        for o in range(0, len(f) - 9):
            cmd = struct.unpack_from('<H', f, o)[0]
            if cmd in (0x85, 0x86, 0x94):
                n = 3 if cmd == 0x85 else 4
                args = struct.unpack_from('<%dH' % n, f, o + 2)
                if not all(a == 0 or a < ntr for a in args) or args[0] == 0:
                    continue
                tids = [t for t in (args[1:3] if cmd == 0x86 else args[:1]) if t]
                if not tids or (cmd != 0x85 and args[0] in STRAY_TRAINERS):
                    continue
                what = ('T', tuple(tids))
            elif cmd == 0x178:
                # StartScriptedWildBattle: u16 species, u16 level, u16 flags
                a, b, _c = struct.unpack_from('<HHH', f, o + 2)
                if not 1 <= a <= 649 or not 1 <= b <= 100:
                    continue
                what = ('W', a, b)
            else:
                continue
            sn = max((i + 1 for i, e in enumerate(ents) if e <= o), default=0)
            if sn:
                hits[sn].append((o, what))
        out = {}
        for sn, hs in hits.items():
            clusters, last = [], None
            for o, what in hs:
                if last is None or o - last > 100 or what[0] != clusters[-1][0][1][0]:
                    clusters.append([])
                clusters[-1].append((o, what))
                last = o
            fights, seen = [], set()
            for cl in clusters:
                if cl[0][1][0] == 'W':
                    # the versions of one fight (Reshiram or Zekrom): the first
                    w = cl[0][1]
                    if w not in seen:
                        seen.add(w)
                        fights.append(dict(kind='W', species=w[1], level=w[2]))
                    continue
                ids = sorted({t for _o, w in cl for t in w[1]})
                run = [ids[0]]
                for t in ids[1:] + [None]:
                    if t is not None and t == run[-1] + 1:
                        run.append(t)
                        continue
                    if tuple(run) not in seen:
                        seen.add(tuple(run))
                        fights.append(dict(kind='T', tid=run[0], tn=len(run)))
                    run = [t]
            out[sn] = fights
        return out

    def script_movers(self, z, sn):
        """{object id: count} of the movement commands (0x64: u16 object,
        s32 movement) in script `sn` of the zone: the characters it walks
        (a story battle's opponent walks up to the player)."""
        sid = struct.unpack_from('<H', self.zh, z * 48 + 6)[0]
        f = self.scripts[sid]
        ents, o = [], 0
        while o + 4 <= len(f) and struct.unpack_from('<H', f, o)[0] != 0xFD13:
            ents.append(o + 4 + struct.unpack_from('<i', f, o)[0])
            o += 4
        if not 1 <= sn <= len(ents):
            return {}
        start = ents[sn - 1]
        end = min([e for e in ents if e > start] + [len(f)])
        out = {}
        for o in range(start, end - 7):
            if struct.unpack_from('<H', f, o)[0] == 0x64:
                ob = struct.unpack_from('<H', f, o + 2)[0]
                out[ob] = out.get(ob, 0) + 1
        return out

    def trainer_class(self, tid):
        if 0 <= tid < len(self.trdata) and len(self.trdata[tid]) > 1:
            c = self.trdata[tid][1]
            if c < len(self.classes):
                return self.classes[c]
        return 'Trainer'


# ---------------------------------------------------------------- analysis

def walk_cost(cell):
    if cell is None or cell[0] == 'blocked':
        return None
    return 1 if cell[0] == 'open' else 4         # water: surfing, later in the game


def reach(tiles, warps):
    """Distance from the zone's entrances over the walk grid, and the step
    each tile was reached by (dx, dz)."""
    dist, step = {}, {}
    heap = []
    import heapq
    starts = set()
    for x, z, sx, sz in warps:
        for a in range(sx):
            for b in range(sz):
                starts.add((x + a, z + b))
    for (x, z), c in tiles.items():
        if walk_cost(c) is None:
            continue
        for dx, dz in DIRV.values():
            if (x + dx, z + dz) not in tiles:
                starts.add((x, z))
                break
    for s in starts:
        if walk_cost(tiles.get(s)) is not None or s in tiles:
            dist[s] = 0
            step[s] = None
            heapq.heappush(heap, (0, s))
    while heap:
        d, (x, z) = heapq.heappop(heap)
        if d > dist.get((x, z), 1 << 30):
            continue
        for dx, dz in DIRV.values():
            n = (x + dx, z + dz)
            c = walk_cost(tiles.get(n))
            if c is None:
                continue
            if d + c < dist.get(n, 1 << 30):
                dist[n] = d + c
                step[n] = (dx, dz)
                heapq.heappush(heap, (d + c, n))
    return dist, step


def patches(tiles):
    """The zone's encounter tiles in patches: tiles two or fewer apart join
    one patch; land and water apart."""
    enc = {k for k, c in tiles.items() if c[2] and c[0] != 'blocked'}
    seen, out = set(), []
    for t in sorted(enc):
        if t in seen:
            continue
        water = tiles[t][0] == 'water'
        comp, q = [], collections.deque([t])
        seen.add(t)
        while q:
            x, z = q.popleft()
            comp.append((x, z))
            for a in range(-2, 3):
                for b in range(-2, 3):
                    n = (x + a, z + b)
                    if n in enc and n not in seen and (tiles[n][0] == 'water') == water:
                        seen.add(n)
                        q.append(n)
        out.append((water, comp))
    return out


def tile_center(t):
    return t[0] * 16 + 8, t[1] * 16 + 8


def analyse(rom):
    rows = []
    for z in range(rom.nzones):
        info = rom.zone(z)
        if info['matrix'] >= len(rom.mats):
            continue
        npcs, warps = rom.event_file(z)
        trainers = [n for n in npcs if n['type'] == 1 or (z in GYMS and n['code'] in LEADER_CODES)]
        story = rom.story_battles(z)
        own = {(n['gx'], n['gz']) for n in trainers}
        storied = [n for n in npcs if n['type'] != 1 and n['script'] in story and (n['gx'], n['gz']) not in own]
        # a battle in another script: the character that script walks up,
        # else the room's only character (an Elite Four room)
        byobj = {n['obj']: n for n in npcs}
        mine = {n['script'] for n in npcs}
        for sn in story:
            if sn in mine:
                continue
            mv = {ob: c for ob, c in rom.script_movers(z, sn).items() if ob in byobj}
            pick = max(mv, key=mv.get) if mv else (npcs[0]['obj'] if len(npcs) == 1 else None)
            if pick is not None:
                n = dict(byobj[pick])
                if (n['gx'], n['gz']) not in own and all(m['obj'] != n['obj'] for m in storied):
                    n['script'] = sn
                    n['walked'] = bool(mv)
                    storied.append(n)
        has_enc = info['encount'] != 0xFFFF or z in WFBC_WILD
        if not trainers and not has_enc and not storied and not rom.story_battles(z) and z not in WFBC:
            continue
        tiles = rom.tiles(z)
        if not tiles:
            continue
        dist, step = reach(tiles, warps)
        name = rom.name(z)
        seen_tiles = set()
        nt = 0
        for n in trainers:
            if (n['gx'], n['gz']) in seen_tiles:
                continue                    # the Striaton triplets share one tile
            seen_tiles.add((n['gx'], n['gz']))
            vx, vz = DIRV.get(n['dir'], (0, 1))
            leader = n['type'] != 1
            rng = 0 if leader else n['sight']
            line = [(n['gx'] + vx * k, n['gz'] + vz * k) for k in range(1, max(rng, 1) + 1)]
            line = [t for t in line if walk_cost(tiles.get(t)) is not None] or [(n['gx'] + vx, n['gz'] + vz)]
            best = min(line, key=lambda t: dist.get(t, 1 << 30))
            nt += 0 if leader else 1
            if leader:
                label = '%s: leader' % name
            else:
                label = '%s: %s (trainer %d)' % (name, rom.trainer_class(n['script'] - 3000), nt)
            px, pz = tile_center(best)
            rows.append(dict(id='T%d.%d' % (z, n['obj']), kind='T', zone=z, label=label,
                             tid=n['script'] - 3000 if n['script'] >= 3000 else '-',
                             tx=n['gx'], tz=n['gz'], dir=n['dir'], range=rng,
                             px=px, pz=pz, pf=facing_deg(-vx, -vz), mode='auto'))
        if z in WFBC:
            pos = rom.wfbc[WFBC[z]]
            for k in range(10):
                tx, tz = pos[2 * k], pos[2 * k + 1]
                near = [(tx + dx, tz + dz) for dx, dz in (DIRV[1], DIRV[0], DIRV[2], DIRV[3])]
                near = [t for t in near if walk_cost(tiles.get(t)) is not None] or [(tx, tz + 1)]
                best = min(near, key=lambda t: dist.get(t, 1 << 30))
                fx, fz = tx - best[0], tz - best[1]
                px, pz = tile_center(best)
                rows.append(dict(id='T%d.r%d' % (z, k + 1), kind='T', zone=z,
                                 label='%s: resident %d' % (name, k + 1), tid='-',
                                 tx=tx, tz=tz, dir=1, range=0,
                                 px=px, pz=pz, pf=facing_deg(fx, fz), mode='auto'))
        # a gym leader's battle is the leader row's (the leader's own talk
        # script jumps to it): its trainers go there, no second row.  The
        # leaders of one gym (the starter picks one) are one range.
        if z in GYMS:
            lead = [w for fl in story.values() for w in fl
                    if w['kind'] == 'T' and rom.trainer_class(w['tid']) == 'Leader']
            if lead:
                lo = min(w['tid'] for w in lead)
                hi = max(w['tid'] + w['tn'] - 1 for w in lead)
                tid, tn = (lo, hi - lo + 1) if hi - lo < 4 else (lead[0]['tid'], lead[0]['tn'])
                for r in rows:
                    if r['zone'] == z and r['label'].endswith(': leader') and r.get('tid', '-') == '-':
                        r['tid'], r['tn'] = tid, tn
            for sn in list(story):
                if any(w in lead for w in story[sn]):
                    del story[sn]           # a leader's script: its other fights are the gym's
            storied = [n for n in storied if story.get(n['script'])]
        # a character that starts off the walkable map (a script hides it
        # there and walks it in: Tubeline Bridge's bikers) has no place yet
        storied = [n for n in storied if not n.get('walked') or walk_cost(tiles.get((n['gx'], n['gz']))) is not None
                   or any(walk_cost(tiles.get((n['gx'] + dx, n['gz'] + dz))) is not None for dx, dz in DIRV.values())]
        # the trainers that already have a row (the gym trainers of a
        # leader's script are the sight-line ones)
        covered = {int(r['tid']) + k for r in rows if str(r.get('tid', '-')).isdigit()
                   for k in range(int(r.get('tn', 1)) if str(r.get('tn', 1)).isdigit() else 1)}
        for n in storied:
            # a story battle: the character stands where its header puts it
            # (the script may walk it up first), the player faces it
            vx, vz = DIRV.get(n['dir'], (0, 1))
            near = [(n['gx'] + vx, n['gz'] + vz)] + [(n['gx'] + dx, n['gz'] + dz) for dx, dz in DIRV.values()]
            near = [t for t in near if walk_cost(tiles.get(t)) is not None] or [(n['gx'] + vx, n['gz'] + vz)]
            best = min(near, key=lambda t: dist.get(t, 1 << 30))
            px, pz = tile_center(best)
            fx, fz = n['gx'] - best[0], n['gz'] - best[1]
            pf = facing_deg(fx, fz) if (fx, fz) in DIRV.values() else facing_deg(-vx, -vz)
            fights = [w for w in story[n['script']]
                      if w['kind'] == 'W' or not any(w['tid'] + k in covered for k in range(w['tn']))]
            for k, what in enumerate(fights):
                # the first fight of the character's script keeps the plain id;
                # the next ones get b, c and so on
                rid = 'T%d.%d%s' % (z, n['obj'], 'bcdefgh'[k - 1] if k else '')
                tn = 1
                if what['kind'] == 'T':
                    label, tid, tn = '%s: %s (story)' % (name, rom.trainer_class(what['tid'])), what['tid'], what['tn']
                else:
                    # a legendary (or another one-off Pokemon): the trainer column
                    # holds 100000 + level * 1000 + species (the editor's preview
                    # starts a wild battle with it)
                    sp = rom.species[what['species']] if what['species'] < len(rom.species) else '#%d' % what['species']
                    label, tid = '%s: %s (legendary)' % (name, sp), 100000 + what['level'] * 1000 + what['species']
                rows.append(dict(id=rid, kind='T', zone=z,
                                 label=label, tid=tid, tn=tn,
                                 tx=n['gx'], tz=n['gz'], dir=n['dir'], range=0,
                                 px=px, pz=pz, pf=pf, mode='auto'))
        # every other fight a script of the zone starts (a guard by a door, a
        # scene's grunt, an Elite Four rematch, a gym trainer the leader's
        # script calls): a row of its own, at the character that runs the
        # script, else where the player comes in.  Fights of one script with
        # the same trainer class and consecutive ids are the versions of one
        # fight (the starter picks one): they share the row.
        covered = {int(r['tid']) + k for r in rows if str(r.get('tid', '-')).isdigit()
                   for k in range(int(r.get('tn', 1)) if str(r.get('tn', 1)).isdigit() else 1)}
        entry = min(dist, key=lambda t: (dist[t], t)) if dist else None
        if entry is None:
            # no way in found (the walk found no entrance): the walkable tile
            # nearest the middle of the zone's walkable ground
            walk = [t for t in tiles if walk_cost(tiles.get(t)) is not None]
            if walk:
                mx = sum(t[0] for t in walk) / len(walk)
                mz = sum(t[1] for t in walk) / len(walk)
                entry = min(walk, key=lambda t: ((t[0] - mx) ** 2 + (t[1] - mz) ** 2, t))
        nx = 0
        for sn, fl in sorted(rom.story_battles(z).items()):
            fights = sorted((w for w in fl if w['kind'] == 'T' and
                             not any(w['tid'] + k in covered for k in range(w['tn']))), key=lambda w: w['tid'])
            merged = []
            for w in fights:
                last = merged[-1] if merged else None
                if (last is not None and w['tid'] == last['tid'] + last['tn'] and
                        rom.trainer_class(w['tid']) == rom.trainer_class(last['tid'])):
                    last['tn'] += w['tn']
                else:
                    merged.append(dict(w))
            runner = next((n for n in npcs if n['script'] == sn), None)
            for w in merged:
                if runner is not None:
                    vx, vz = DIRV.get(runner['dir'], (0, 1))
                    near = [(runner['gx'] + vx, runner['gz'] + vz)] + \
                           [(runner['gx'] + dx, runner['gz'] + dz) for dx, dz in DIRV.values()]
                    near = [t for t in near if walk_cost(tiles.get(t)) is not None] or \
                           [(runner['gx'] + vx, runner['gz'] + vz)]
                    best = min(near, key=lambda t: dist.get(t, 1 << 30))
                    fx, fz = runner['gx'] - best[0], runner['gz'] - best[1]
                    pf = facing_deg(fx, fz) if (fx, fz) in DIRV.values() else facing_deg(-vx, -vz)
                    tx, tz, d, rid = runner['gx'], runner['gz'], runner['dir'], 'T%d.s%d' % (z, sn)
                elif entry is not None:
                    best, pf = entry, 0
                    s0 = step.get(entry)
                    pf = facing_deg(*s0) if s0 else 0
                    tx, tz, d, rid = entry[0], entry[1], 1, 'T%d.s%d' % (z, sn)
                else:
                    continue
                nx += 1
                rid += '' if len(merged) == 1 else 'abcdefgh'[merged.index(w)]
                px, pz = tile_center(best)
                rows.append(dict(id=rid, kind='T', zone=z,
                                 label='%s: %s (story)' % (name, rom.trainer_class(w['tid'])),
                                 tid=w['tid'], tn=w['tn'], tx=tx, tz=tz, dir=d, range=0,
                                 px=px, pz=pz, pf=pf, mode='auto'))
                covered.update(range(w['tid'], w['tid'] + w['tn']))
        if has_enc:
            np = 0
            for water, comp in sorted(patches(tiles), key=lambda p: (min(p[1])[1], min(p[1])[0])):
                if len(comp) < 2:
                    continue                # a lone tile (a hidden grass tuft)
                np += 1
                xs = [t[0] for t in comp]
                zs = [t[1] for t in comp]
                first = min(comp, key=lambda t: (dist.get(t, 1 << 30), t))
                s = step.get(first)
                pf = facing_deg(*s) if s else 0
                px, pz = tile_center(first)
                rows.append(dict(id='A%d.%d' % (z, np), kind='A', zone=z,
                                 label='%s: %s %d' % (name, 'water' if water else 'area', np),
                                 rx0=min(xs), rz0=min(zs), rx1=max(xs), rz1=max(zs),
                                 px=px, pz=pz, pf=pf, mode='auto'))
            if np == 0 and z in WFBC_WILD:
                # White Forest's grass comes with the blocks the game builds
                # around the residents, not in the map's own tiles: one area
                # over the walkable ground
                walk = [t for t in tiles if walk_cost(tiles.get(t)) is not None]
                if walk:
                    xs = [t[0] for t in walk]
                    zs = [t[1] for t in walk]
                    mx, mz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
                    mid = min(walk, key=lambda t: ((t[0] - mx) ** 2 + (t[1] - mz) ** 2, t))
                    px, pz = tile_center(mid)
                    rows.append(dict(id='A%d.1' % z, kind='A', zone=z, label='%s: area 1' % name,
                                     rx0=min(xs), rz0=min(zs), rx1=max(xs), rz1=max(zs),
                                     px=px, pz=pz, pf=0, mode='auto'))
    return rows


# ---------------------------------------------------------------- the table

# A row can have several views (the column vw: 0 or missing for the first,
# 1 and on for the others, written as rows of the same id after it).  The
# table keys a first view by its id and another view by 'id#N'.

def row_key(r):
    v = r.get('vw', '-')
    return r['id'] if v in ('-', '', '0') else '%s#%s' % (r['id'], v)


def read_table(path):
    rows = collections.OrderedDict()
    if not os.path.exists(path):
        return rows
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        f = line.split('\t')
        f += ['-'] * (len(COLUMNS) - len(f))
        r = dict(zip(COLUMNS, f))
        rows[row_key(r)] = r
    return rows


def fields(r):
    """A row's fields; the columns added after the first 33 are left out while
    they hold nothing, so a table from before them stays as it was."""
    f = [str(r.get(c, '-')) for c in COLUMNS]
    while len(f) > 33 and f[-1] == '-':
        f.pop()
    return f


def grouped(rows):
    """The rows with each id's other views after its first one."""
    views = collections.defaultdict(list)
    for k, r in rows.items():
        if k != r['id']:
            views[r['id']].append(r)
    for k, r in rows.items():
        if k == r['id']:
            yield r
            for v in sorted(views.get(k, []), key=lambda x: int(x['vw'])):
                yield v


def write_table(path, rows):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# battle spots: tools/battle_spots.py, engine/world/bmap_spots.c\n')
        f.write('#' + '\t'.join(COLUMNS) + '\n')
        for r in grouped(rows):
            f.write('\t'.join(fields(r)) + '\n')


def write_inc(rows):
    with open(INC, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Generated by tools/battle_spots.py from battle_spots.tsv; do not edit. */\n')
        for r in grouped(rows):
            line = '\t'.join(fields(r))
            f.write('"%s\\n"\n' % line.replace('\\', '\\\\').replace('"', '\\"'))


def cmd_gen(args):
    rom = Rom(args.data)
    old = read_table(TABLE)
    rows = collections.OrderedDict()
    for r in analyse(rom):
        full = {c: '-' for c in COLUMNS}
        full.update({k: str(v) for k, v in r.items()})
        full['ok'] = '0'
        if r['id'] in old:
            for c in PLACEMENT:
                full[c] = old[r['id']][c]
        rows[r['id']] = full
    write_table(TABLE, rows)
    write_inc(rows)
    kinds = collections.Counter(r['kind'] for r in rows.values())
    print('%d rows: %d trainers, %d areas' % (len(rows), kinds['T'], kinds['A']))


def cmd_merge(args):
    rows = read_table(TABLE)
    theirs = read_table(args.file)
    n = 0
    ids = {r['id'] for r in theirs.values()}
    for k in [k for k, r in rows.items() if r['id'] in ids and k not in theirs]:
        del rows[k]                         # a view the editor deleted
    for k, r in theirs.items():
        if k in rows and k == r['id']:
            for c in PLACEMENT:
                rows[k][c] = r[c]
            n += 1
        else:
            rows[k] = r                     # a row or a view the editor added
            n += 1
    write_table(TABLE, rows)
    write_inc(rows)
    print('merged %d rows' % n)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    g = sp.add_parser('gen')
    g.add_argument('--data', required=True)
    m = sp.add_parser('merge')
    m.add_argument('file')
    sp.add_parser('inc')
    args = ap.parse_args()
    if args.cmd == 'gen':
        cmd_gen(args)
    elif args.cmd == 'merge':
        cmd_merge(args)
    else:
        write_inc(read_table(TABLE))


if __name__ == '__main__':
    main()
