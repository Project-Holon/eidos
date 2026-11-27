"""The battle handlers' adjustable numbers, read from the game itself
(sdk/battle_handlers.h, docs/battle-handlers.md).

For each move, ability and item with a handler table (the registries
gWazaHandlerRegistry, gAbilityHandlerRegistry, gItemHandlerRegistry in
overlay 93), this finds its handler functions from the overlay's own bytes
(the registry rows, each getter's table, the table's rows), then reads each
handler's source in aletheia (src/game/ov093/hand_*.c) for the numbers it
uses at the places eidos can change: a stat change's stat and amount, a
status it adds, a multiplier, an HP fraction, a chance, a type, a weather.

A key is offered only where its place is sure: one place of that kind in a
handler (counting the helpers it calls), with a literal value.  Two places
of a kind in one handler are left out ("not adjustable"), never guessed.

    python -I tools/handler_params.py <aletheia root> <extract dir> <out dir>

Writes <out dir>/handler_params.json (for kenon) and
<out dir>/battle_handler_sites.inc (sdk/battle_handlers.c).
"""
import json
import os
import re
import struct
import sys

ALETHEIA, EXTRACT, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
SRC = os.path.join(ALETHEIA, "src", "game", "ov093")
FILES = ["hand_tokusei.c", "hand_item.c", "hand_waza.c", "hand_common.c"]

KINDS = {  # registry symbol, rows, factor type, name
    "move": ("gWazaHandlerRegistry", 0x102, 0),
    "ability": ("gAbilityHandlerRegistry", 0x9E, 4),
    "item": ("gItemHandlerRegistry", 0xAC, 5),          # (ITEM_HANDLER_COUNT)
}


def die(msg):
    sys.exit("handler_params: " + msg)


# ---- the overlay ----------------------------------------------------------

def overlay_base(ov):
    y9 = open(os.path.join(EXTRACT, "y9.bin"), "rb").read()
    for at in range(0, len(y9), 32):
        oid, ram = struct.unpack_from("<II", y9, at)
        if oid == ov:
            return ram
    die("overlay %d not in y9.bin" % ov)


BASE = overlay_base(93)
BIN = open(os.path.join(EXTRACT, "overlays_dec", "y9_0093.bin"), "rb").read()


def u32_at(addr):
    off = addr - BASE
    if off < 0 or off + 4 > len(BIN):
        die("address 0x%08X is outside overlay 93" % addr)
    return struct.unpack_from("<I", BIN, off)[0]


# ---- names ----------------------------------------------------------------

FUNC_AT = {}
for line in open(os.path.join(ALETHEIA, "layout", "function_addrs.tsv"), encoding="utf-8"):
    f = line.rstrip("\n").split("\t")
    if len(f) >= 3 and f[1].startswith("0x") and f[0] == "ov093":
        FUNC_AT[int(f[1], 16) & ~1] = f[2]

SOURCE = {}
MATCHED = os.path.join(ALETHEIA, "src", "matched", "ov")
for folder, keep in ((SRC, lambda n: n.endswith(".c")), (MATCHED, lambda n: n.startswith("ov093_") and n.endswith(".c"))):
    for name in sorted(os.listdir(folder)):
        if keep(name):
            SOURCE[os.path.join(folder, name)] = open(os.path.join(folder, name), encoding="utf-8",
                                                      errors="replace").read()

DATA_AT = {}
for text in SOURCE.values():
    for m in re.finditer(r"^\s*\*\s+(g\w+)\s+0x([0-9A-Fa-f]{8})\s+data", text, re.M):
        DATA_AT.setdefault(m.group(1), int(m.group(2), 16))

HEAD = re.compile(r"^[A-Za-z_][\w \*]*?\b(\w+)\s*\(([^;{}()]*)\)\s*\{", re.M)


def function_bodies(text):
    """Each function defined in `text`: name -> (parameters, body), by
    matching braces (comments and strings skipped)."""
    out = {}
    for m in HEAD.finditer(text):
        if m.group(1) in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        depth, i = 1, m.end()
        while i < len(text) and depth > 0:
            c = text[i]
            if text.startswith("/*", i):
                j = text.find("*/", i + 2)
                i = len(text) if j < 0 else j + 2
                continue
            if text.startswith("//", i):
                j = text.find("\n", i)
                i = len(text) if j < 0 else j
                continue
            if c == '"' or c == "'":
                j = i + 1
                while j < len(text) and text[j] != c:
                    j += 2 if text[j] == "\\" else 1
                i = j + 1
                continue
            depth += c == "{"
            depth -= c == "}"
            i += 1
        out.setdefault(m.group(1), (m.group(2), text[m.end():i - 1]))
    return out


BODIES = {}
for text in SOURCE.values():
    for name, v in function_bodies(text).items():
        BODIES.setdefault(name, v)


def getter_table(getter):
    """The handler table a getter returns and the count it sets."""
    if getter not in BODIES:
        return None, None, 0
    body = BODIES[getter][1]
    # (a table can start at another's row: `return gMotorDriveHandlers + 1;`)
    t = re.search(r"return\s+(?:\([^()]*\)\s*)?&?\s*(g\w+)\s*(?:\+\s*(\d+)\s*)?;", body)
    c = re.search(r"\*\s*\w+\s*=\s*(?:HAND_\w+PackAbilityHandlerInfo\(\s*\w+\s*,\s*)?(0x[0-9A-Fa-f]+|\d+)", body)
    if t is None or c is None:
        return None, None, 0
    return t.group(1), int(c.group(1), 0), int(t.group(2) or 0)


def registry(kind):
    sym, rows, _ = KINDS[kind]
    at = DATA_AT.get(sym)
    if at is None:
        die("no address for " + sym)
    out = {}
    i = 0
    while rows is None or i < rows:
        rid, fn = u32_at(at + i * 8), u32_at(at + i * 8 + 4)
        if fn == 0:
            i += 1
            continue            # (an empty row: the item registry's last, {0, 0})
        getter = FUNC_AT.get(fn & ~1)
        if getter is None:
            die("%s row %d: no function at 0x%08X" % (sym, i, fn))
        out[rid] = getter
        i += 1
    return out


def handlers_of(getter):
    table, count, first = getter_table(getter)
    if table is None or table not in DATA_AT:
        return None
    rows = []
    for k in range(first, first + count):
        ev, fn = u32_at(DATA_AT[table] + k * 8), u32_at(DATA_AT[table] + k * 8 + 4)
        rows.append((ev, FUNC_AT.get(fn & ~1, "0x%08X" % fn)))
    return rows


# ---- the constants --------------------------------------------------------

CONST = {}
for text in list(SOURCE.values()) + [open(os.path.join(SRC, h), encoding="utf-8", errors="replace").read()
                                     for h in os.listdir(SRC) if h.endswith(".h")]:
    for m in re.finditer(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+([^\n/]+)", text, re.M):
        CONST.setdefault(m.group(1), m.group(2).strip())
    for m in re.finditer(r"\b([A-Z_][A-Z0-9_]*)\s*=\s*(-?(?:0x[0-9A-Fa-f]+|\d+))\s*[,}\n]", text):
        CONST.setdefault(m.group(1), m.group(2))


def value_of(expr, depth=0):
    """A constant expression's value (numbers, the macros above, + - * << >>
    and parentheses), or None when it is not a constant."""
    expr = expr.strip()
    if depth > 8 or not expr:
        return None
    expr = re.sub(r"\(\s*(?:u8|u16|u32|s8|s16|s32|int|unsigned(?:\s+int)?)\s*\)", "", expr)
    expr = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", expr)    # (1U, 0x10u)
    # names: not the x of 0x10 nor a hex digit run (a name starts after no digit or letter)
    names = re.findall(r"(?<![0-9A-Za-z_])[A-Za-z_]\w*", expr)
    for n in names:
        if n not in CONST:
            return None
        v = value_of(CONST[n], depth + 1)
        if v is None:
            return None
        expr = re.sub(r"(?<![0-9A-Za-z_])%s\b" % n, str(v), expr)
    if not re.fullmatch(r"[0-9xXa-fA-F\s()+\-*<>]+", expr):
        return None
    try:
        return int(eval(expr, {"__builtins__": {}}))     # (digits and operators only: checked above)
    except Exception:
        return None


# ---- the places a number can be changed ------------------------------------

# Site kinds and the engine's patch points (sdk/battle_handlers.h, BW_BH_SITE_*).
RANK, SICK, WEATHER, CHANCE, HP, RATIO, TYPE = "rank", "sick", "weather", "chance", "hp", "ratio", "type"
RECORD_KIND = {14: RANK, 12: SICK, 0x1D: WEATHER}
RECORD_FIELDS = {RANK: [("statId|rankKind", "stat"), ("amount|volume", "stages")],
                 SICK: [("sickKind", "status")], WEATHER: [("weather", "weather")]}
CALLS = {  # primitive -> (argument index, site, key)
    "BTL_CALC_RollPercent": (0, CHANCE, "chance"),
    "BTL_CALC_DivideMaxHp": (1, HP, "hp"),
    "BTL_CALC_DivideMaxHpMin1": (1, HP, "hp"),
    "BTL_EVENT_MultiplyVar": (1, RATIO, "ratio"),
}
TYPE_HELPERS = {  # a helper that takes the type it tests -> argument index
    "HAND_TOKUSEI_PinchBoostType": 2,
    "HAND_TOKUSEI_AbsorbCheckMoveType": 2,
    "HAND_ITEM_WeakenHitOfType": 4,
    "HAND_ITEM_BoostMovePowerOfType": 3,
}


def split_args(s):
    out, depth, cur = [], 0, ""
    for c in s:
        if c == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += c in "(["
        depth -= c in ")]"
        cur += c
    if cur.strip():
        out.append(cur)
    return [a.strip() for a in out]


def calls_in(body):
    """(name, args text, start) of each call in body, outermost first."""
    out = []
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
        if m.group(1) in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        depth, i = 1, m.end()
        while i < len(body) and depth:
            depth += body[i] == "("
            depth -= body[i] == ")"
            i += 1
        out.append((m.group(1), body[m.end():i - 1], m.start()))
    return out


def loop_spans(body):
    spans = []
    for m in re.finditer(r"\b(for|while|do)\b", body):
        j = body.find("{", m.end())
        k = body.find(";", m.end()) if m.group(1) != "for" else body.find(")", m.end())
        if j < 0:
            continue
        depth, i = 1, j + 1
        while i < len(body) and depth:
            depth += body[i] == "{"
            depth -= body[i] == "}"
            i += 1
        spans.append((m.start(), i))
    return spans


def in_loop(spans, at):
    return any(a <= at < b for a, b in spans)


COMMITS = ("BTL_SERVER_FLOW_CommitCmdRecord", "BTL_SERVER_FLOW_PushCmdRecord")
DISPLAY_ONLY = (2, 3, 4)


def sites_of(func, args=None, depth=0, looped=False, late=False):
    """The sites one run of `func` reaches, in the order they are written:
    list of (site, key, value or None, looped, late).  `late`: a command
    was committed before it (the engine's own work runs inside a commit, so
    a site after one may not be the first of its kind).  `args`: the
    caller's literal arguments, by parameter."""
    if func not in BODIES or depth > 6:
        return []
    params_text, body = BODIES[func]
    params = [re.findall(r"(\w+)\s*(?:\[\s*\])?$", p.strip()) for p in split_args(params_text)]
    params = [p[0] if p else "" for p in params]
    sub = dict(zip(params, args or []))

    def lit(expr):
        e = expr.strip()
        if e in sub and sub[e] is not None:
            return sub[e]
        return value_of(e)

    spans = loop_spans(body)
    calls = calls_in(body)
    # The first commit whose engine work can reach a site.  The ability
    # pop-up in and out and a message (kinds 2, 3, 4: ExecCmdAbilityPopupIn,
    # ExecCmdAbilityPopupOut, ExecCmdPushMessage) only queue display.
    kinds = {m.group(1): value_of(m.group(2)) for m in re.finditer(
        r"(\w+)\s*=\s*(?:\([^()]*\)\s*)?BTL_SERVER_FLOW_AllocCmdRecord\(\s*\w+\s*,\s*([^,]+),", body)}
    first_commit = len(body) + 1
    for name, a, at in calls:
        if name not in COMMITS:
            continue
        argv = split_args(a)
        kind = (value_of(argv[1]) if name == "BTL_SERVER_FLOW_PushCmdRecord" and len(argv) > 1
                else kinds.get(argv[1].strip() if len(argv) > 1 else ""))
        if kind not in DISPLAY_ONLY:
            first_commit = min(first_commit, at)
    out = []
    # records: kind and the fields set on the same variable (applied at their own commit)
    for m in re.finditer(r"(\w+)\s*=\s*(?:\([^()]*\)\s*)?BTL_SERVER_FLOW_AllocCmdRecord\(\s*\w+\s*,\s*([^,]+),", body):
        var, kind = m.group(1), value_of(m.group(2))
        site = RECORD_KIND.get(kind)
        if site is None:
            continue
        loop = looped or in_loop(spans, m.start())
        for fields, key in RECORD_FIELDS[site]:
            f = re.search(r"\b%s->(?:%s)\s*=\s*([^;]+);" % (re.escape(var), fields), body[m.end():])
            out.append((site, key, lit(f.group(1)) if f else None, loop, late or m.start() > first_commit))
    for name, a, at in calls:
        loop = looped or in_loop(spans, at)
        after = late or at > first_commit
        argv = split_args(a)
        if name in COMMITS:
            continue                # (the engine's own work: not the handler's numbers)
        if name in CALLS:
            idx, site, key = CALLS[name]
            out.append((site, key, lit(argv[idx]) if idx < len(argv) else None, loop, after))
        elif name in TYPE_HELPERS:
            idx = TYPE_HELPERS[name]
            out.append((TYPE, "type", lit(argv[idx]) if idx < len(argv) else None, loop, after))
            out += sites_of(name, [lit(x) for x in argv], depth + 1, loop, after)
        elif name in BODIES:
            # any function of the overlay it calls may reach a site first
            out += sites_of(name, [lit(x) for x in argv], depth + 1, loop, after)
    return out


# ---- names for the values ---------------------------------------------------

STAT = {1: "atk", 2: "def", 3: "spatk", 4: "spdef", 5: "speed", 6: "accuracy", 7: "evasion"}
STATUS = {1: "paralysis", 2: "sleep", 3: "freeze", 4: "burn", 5: "poison", 6: "confusion", 7: "infatuation"}
TYPES = ["normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost", "steel", "fire", "water",
         "grass", "electric", "psychic", "ice", "dragon", "dark"]
WEATHER_NAMES = {1: "sun", 2: "rain", 3: "hail", 4: "sand"}


def shown(key, v):
    """The value as a table line writes it, or None when it is not one."""
    if key == "stat":
        return STAT.get(v)
    if key == "stages":
        return v if -6 <= v <= 6 else None
    if key == "status":
        return STATUS.get(v)
    if key == "chance":
        return v if 0 <= v <= 100 else None
    if key == "type":
        return TYPES[v] if 0 <= v < len(TYPES) else None
    if key == "ratio":
        return round(v / 4096.0, 4) if 0 < v <= 4 * 4096 else None
    if key == "hp":
        return "1/%d" % v if 1 <= v <= 16 else None
    if key == "weather":
        return WEATHER_NAMES.get(v)
    return None


# ---- the factor's work slots ------------------------------------------------

WORK_SLOTS = 7          # struct BtlEventFactor: u32 work[7]
TYPE_WORDS = {"int", "u8", "u16", "u32", "s8", "s16", "s32", "void", "unsigned", "char", "const", "uptr", "sptr"}


def work_slots(func, aliases=("work",), depth=0, seen=None):
    """The factor work slots a handler reads or writes (factor->work, which
    handlers get as their `work` parameter), following the overlay's
    functions it passes that pointer to.  A slot it reaches by a value not
    known here counts as every slot."""
    seen = set() if seen is None else seen
    if func not in BODIES or depth > 6 or (func, aliases) in seen:
        return set()
    seen.add((func, aliases))
    params_text, body = BODIES[func]
    out = set()
    every = set(range(WORK_SLOTS))
    for a in aliases:
        for m in re.finditer(r"\b%s\s*\[([^\]]+)\]" % re.escape(a), body):
            v = value_of(m.group(1))
            out |= every if v is None else {v}
        for m in re.finditer(r"\*\s*%s\b" % re.escape(a), body):
            before = body[:m.start()].rstrip()
            if before and (before[-1].isalnum() or before[-1] == "_"):
                if before.split()[-1] in TYPE_WORDS:
                    continue            # (a declaration: int *work)
            out.add(0)
    for m in re.finditer(r"\bfactor\s*->\s*work\s*\[([^\]]+)\]", body):
        v = value_of(m.group(1))
        out |= every if v is None else {v}
    for m in re.finditer(r"BTL_EVENT_(?:Set|Get)FactorWork\(\s*\w+\s*,\s*([^,)]+)", body):
        v = value_of(m.group(1))
        out |= every if v is None else {v}
    for name, a, at in calls_in(body):
        if name not in BODIES or name in COMMITS:
            continue
        argv = split_args(a)
        params = [re.findall(r"(\w+)\s*(?:\[\s*\])?$", p.strip()) for p in split_args(BODIES[name][0])]
        params = [p[0] if p else "" for p in params]
        passed = tuple(p for k, p in enumerate(params) if k < len(argv) and argv[k].strip() in aliases + ("factor->work",))
        if passed:
            out |= work_slots(name, passed, depth + 1, seen)
    return out


# eidos's own move bases (sdk/battle_handlers.h BW_BH_NAMED_*, handlers in
# stubs/ov/patch_ov093_hand_waza.c), keyed by the name a line writes.  By
# hand: the code is eidos's, not the game's, so there is nothing to read.
# Their keys are the rows of k_named_sites in sdk/battle_handlers.c.
NAMED_MOVE_BASES = {
    "misty_terrain": {"keys": {}, "notes": [
        "eidos base: for five turns, grounded Pokemon get no major status, confusion or Yawn, Rest fails for them, "
        "and Dragon moves do half damage to them.",
        "Move record: category 10, target 10 (as Gravity).",
        "Lines: a/0/0/2 bank 15 lines 400 (start) and 401 (end), bank 14 lines 2003 to 2005 (status blocked)."]},
    "flower_shield": {"keys": {"type": "grass", "stat": "def", "stages": 1}, "notes": [
        "eidos base: raises the stat of every Pokemon of the type on the field. Fails when there is none.",
        "Move record: category 10, target 10 (as Haze)."]},
    "crafty_shield": {"keys": {}, "notes": [
        "eidos base: for the rest of the turn, status moves other Pokemon use on the user's side fail. "
        "Moves that target every Pokemon (Perish Song) still work.",
        "Move record: category 11, target 12, priority 3 (as Quick Guard).",
        "Lines: a/0/0/2 bank 14 lines 2006 to 2008 (start) and 2009 to 2011 (move blocked)."]},
    "fairy_lock": {"keys": {}, "notes": [
        "eidos base: no Pokemon on the field can flee or switch out during the next turn. "
        "Fails when every Pokemon is already trapped.",
        "Move record: category 10, target 10 (as Haze).",
        "Lines: a/0/0/2 bank 15 line 402."]},
    "geomancy": {"keys": {}, "notes": [
        "eidos base: Razor Wind's charge turn with its own line. The stat changes come from the move record. "
        "Power Herb skips the charge.",
        "Move record: category 2, target 7, the charge flag and its stat changes.",
        "Lines: a/0/0/2 bank 14 lines 2000 to 2002 (charge turn)."]},
}

SITE_ENUM = {RANK: "BW_BH_SITE_RANK", SICK: "BW_BH_SITE_SICK", WEATHER: "BW_BH_SITE_WEATHER",
             CHANCE: "BW_BH_SITE_CHANCE", HP: "BW_BH_SITE_HP", RATIO: "BW_BH_SITE_RATIO", TYPE: "BW_BH_SITE_TYPE"}


def analyse(kind):
    """id -> {"keys": {...}, "sites": [(handler, site, index)], "notes": [...]}"""
    out = {}
    for rid, getter in sorted(registry(kind).items()):
        rows = handlers_of(getter) or []
        count = {}
        keys, sites, notes = {}, [], []
        seen = {}                   # a function listed for several events: one set of keys
        for h, (ev, fn) in enumerate(rows):
            if fn in seen:
                sites += [(h, site, n) for site, n in seen[fn]]
                continue
            seen[fn] = []
            found = sites_of(fn)
            by_site = {}
            for site, key, v, loop, late in found:
                by_site.setdefault(site, []).append((key, v, loop, late))
            for site, entries in by_site.items():
                # one place of the kind in this handler (a record counts its fields once)
                places = len([e for e in entries if e[0] == entries[0][0]])
                if places != 1:
                    notes.append("%s: %d places of %s, not adjustable" % (fn, places, site))
                    continue
                if any(e[2] for e in entries):
                    notes.append("%s: its %s runs in a loop, not adjustable" % (fn, site))
                    continue
                if any(e[3] for e in entries):
                    notes.append("%s: its %s comes after a command, not adjustable" % (fn, site))
                    continue
                vals = {k: shown(k, v) if v is not None else None for k, v, _, _ in entries}
                if any(x is None for x in vals.values()):
                    notes.append("%s: its %s is decided at run time, not adjustable" % (fn, site))
                    continue
                count[site] = count.get(site, 0) + 1
                n = count[site]
                for k, x in vals.items():
                    keys[k if n == 1 else "%s.%d" % (k, n)] = x
                sites.append((h, site, n))
                seen[fn].append((site, n))
        # the factor work slots its handlers keep state in (an item's slot 6
        # is the engine's own forced-use flag, the same for every item)
        work = set()
        for fn in seen:
            params = [re.findall(r"(\w+)\s*(?:\[\s*\])?$", p.strip()) for p in split_args(BODIES.get(fn, ("", ""))[0])]
            params = [p[0] if p else "" for p in params]
            if len(params) >= 4 and params[3]:
                work |= work_slots(fn, (params[3],))
            else:
                work |= work_slots(fn, ())
        if kind == "item":
            work.discard(6)
        out[rid] = {"keys": keys, "sites": sites, "notes": notes, "work": sorted(work),
                    "handlers": [fn for _, fn in rows]}
    return out


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    pin = os.environ.get("ALETHEIA_PIN", "")
    doc = {"format": 1, "aletheia": pin, "keys": {
        "stat": sorted(STAT.values()), "stages": [-6, 6], "status": sorted(STATUS.values()), "chance": [0, 100],
        "type": TYPES, "ratio": [0.1, 4.0], "hp": ["1/1", "1/16"], "weather": sorted(WEATHER_NAMES.values())}}
    inc = ["/* sdk/battle_handler_sites.inc: generated by tools/handler_params.py from aletheia's",
           " * overlay 93 handler tables and hand_*.c; do not edit.  One row a place a handler",
           " * uses a number eidos can change: { type, base id, handler, site, index }. */"]
    winc = ["/* sdk/battle_handler_work.inc: generated by tools/handler_params.py; do not edit.",
            " * The factor work slots each built-in id's handlers keep state in, as a mask:",
            " * { type, id, mask }.  Two bases of one entry may not share a slot. */"]
    for kind, (_, _, ftype) in KINDS.items():
        res = analyse(kind)
        doc[kind] = {}
        for rid, r in res.items():
            if r["keys"] or r["notes"] or r["work"]:
                e = {"keys": r["keys"], "notes": r["notes"]}
                if r["work"]:
                    e["work"] = r["work"]
                doc[kind][str(rid)] = e
            for h, site, n in r["sites"]:
                inc.append("{ %d, %d, %d, %s, %d }," % (ftype, rid, h, SITE_ENUM[site], n))
            if r["work"]:
                winc.append("{ %d, %d, 0x%02X }," % (ftype, rid, sum(1 << s for s in r["work"])))
        if kind == "move":
            doc[kind].update(NAMED_MOVE_BASES)
        adj = sum(1 for r in res.values() if r["keys"])
        print("%-8s %3d ids, %3d with adjustable keys, %4d keys" % (
            kind, len(res), adj, sum(len(r["keys"]) for r in res.values())))
    open(os.path.join(OUT, "handler_params.json"), "w", newline="\n").write(json.dumps(doc, indent=1) + "\n")
    open(os.path.join(OUT, "battle_handler_sites.inc"), "w", newline="\n").write("\n".join(inc) + "\n")
    open(os.path.join(OUT, "battle_handler_work.inc"), "w", newline="\n").write("\n".join(winc) + "\n")
