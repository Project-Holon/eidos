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
TURNS, DAMAGE, POWER, ACCURACY = "turns", "damage", "power", "accuracy"
RECORD_KIND = {14: RANK, 12: SICK, 0x1D: WEATHER}
RECORD_FIELDS = {RANK: [("statId|rankKind", "stat"), ("amount|volume", "stages")],
                 SICK: [("sickKind", "status")], WEATHER: [("weather", "weather")]}
CALLS = {  # primitive -> (argument index, site, key)
    "BTL_CALC_RollPercent": (0, CHANCE, "chance"),
    "BTL_CALC_DivideMaxHp": (1, HP, "hp"),
    "BTL_CALC_DivideMaxHpMin1": (1, HP, "hp"),
    "BTL_EVENT_MultiplyVar": (1, RATIO, "ratio"),
    # a duration word: the turns (stubs/ov/patch_ov093_btl_sickcont_moves.c, _btl_calc_moves.c)
    "BTL_SICKCONT_MakeKind2Index": (0, TURNS, "turns"),
    "BTL_SICKCONT_MakeKind2": (0, TURNS, "turns"),
    "BTL_CALC_MakeSickContTurns": (0, TURNS, "turns"),
    # the power times a whole factor (Gust, Hex, Acrobatics...): a ratio of factor * 4096
    # (stubs/ov/patch_ov093_hand_common_moves.c)
    "HAND_COMMON_EventScaleDamage": (0, RATIO, "ratio"),
}
SCALE = {"HAND_COMMON_EventScaleDamage": 4096}      # a call's literal in the key's units
# BTL_EVENT_OverwriteVar of these event variables with a literal (stubs/ov/patch_ov093_btl_event_moves.c).
# 0x1F is the stat of a move's stat change at event 0x59, not a chance: Ancient Power's handler, its only
# writer, sets 10 (every stat but accuracy and evasion). Its `chance` is the record's chance of that change
# (10 in the records of Ancient Power, Silver Wind and Ominous Wind), used where the server rolls it
# (patch_ov093_btl_server_flow_moves.c RollMoveAddedEffectChance, sdk/battle_handlers.c bw_bh_move_chance).
VAR_SITES = {0x37: (DAMAGE, "damage"), 0x30: (POWER, "power"), 0x2B: (ACCURACY, "accuracy"),
             0x26: (CHANCE, "chance"), 0x1F: (CHANCE, "chance"), 0x24: (TURNS, "turns")}
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
            v = lit(argv[idx]) if idx < len(argv) else None
            out.append((site, key, v * SCALE[name] if v is not None and name in SCALE else v, loop, after))
        elif name == "BTL_EVENT_OverwriteVar" and argv and value_of(argv[0]) in VAR_SITES:
            site, key = VAR_SITES[value_of(argv[0])]
            out.append((site, key, lit(argv[1]) if len(argv) > 1 else None, loop, after))
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
    if key == "turns":
        return v if 1 <= v <= 63 else None
    if key == "damage":
        return v if 1 <= v <= 999 else None
    if key == "power":
        return v if 1 <= v <= 255 else None
    if key == "accuracy":
        return v if 1 <= v <= 100 else None
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
    "misty_terrain": {"keys": {"turns": 5, "ratio": 0.5}, "notes": [
        "eidos base: for five turns (turns), grounded Pokemon get no major status, confusion or Yawn, Rest fails for "
        "them, and Dragon moves do half damage (ratio) to them. One terrain is up at a time: another kind replaces "
        "it, the same kind fails.",
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
    "fell_stinger": {"keys": {"stat": "atk", "stages": 3}, "notes": [
        "eidos base: when the move knocks a target out, the user's stat rises (Attack +3, Generation VII; "
        "stages=2 for VI).",
        "Move record: a damaging move (as Bug Bite: category 0, target 0, power 50)."]},
    "needs_status": {"keys": {"status": "poison"}, "notes": [
        "eidos base: the move fails on a target without the status (Venom Drench: poison). "
        "The move record gives what it does to one that has it (its stat changes, its damage).",
        "Move record: as Venom Drench, category 2 (stat changes on the target), target 5, "
        "Attack, Sp. Atk and Speed -1."]},
    "spiky_shield": {"keys": {"hp": "1/8"}, "notes": [
        "eidos base: Protect (its streak, its line). A contact move it stops costs the attacker an HP fraction "
        "(Rough Skin's line).",
        "The game's checks of Protect by id count it as Protect (the streak, Metronome's and Assist's lists, the AI).",
        "Move record: a copy of Protect's (priority 4)."]},
    "kings_shield": {"keys": {"stat": "atk", "stages": -2}, "notes": [
        "eidos base: Protect from damaging moves only (status moves pass). A contact move it stops lowers the "
        "attacker's stat (Attack -2, Generations VI and VII; stages=-1 for VIII).",
        "Move record: a copy of Protect's (priority 4)."]},
    "baneful_bunker": {"keys": {"status": "poison"}, "notes": [
        "eidos base: Protect. A contact move it stops gives the attacker the status (poison).",
        "Move record: a copy of Protect's (priority 4)."]},
    "contact_guard": {"keys": {"hp": None, "stat": None, "stages": None, "status": None}, "notes": [
        "eidos building block: Protect, and a contact move it stops does what its keys say to the attacker: "
        "lose `hp`, `stat` changed by `stages`, `status`. Any of them, or none.",
        "Move record: a copy of Protect's (priority 4)."]},
    "super_effective": {"keys": {"type": None}, "notes": [
        "eidos building block: the move is super effective against the type, whatever the type chart says. "
        "No handlers: a part of a line with the move's own base (`600 = 58 + super_effective type=water`).",
        "Move record: the move's own (its type, power, added effect)."]},
    "freeze_dry": {"keys": {"type": "water"}, "notes": [
        "eidos base: super_effective against Water (Freeze-Dry).",
        "Move record: as Ice Beam, power 70, freeze chance 10."]},
    "second_type": {"keys": {"type": None}, "notes": [
        "eidos building block: the move's effectiveness is that of its type and of the second type, together "
        "(Fighting and Flying: Flying Press). Same-type power stays its own type's.",
        "Move record: the move's own."]},
    "flying_press": {"keys": {"type": "flying"}, "notes": [
        "eidos base: second_type Flying (Flying Press).",
        "Move record: a Fighting move, power 80 (100 from Generation VII), accuracy 95, contact."]},
    "add_type": {"keys": {"type": None}, "notes": [
        "eidos building block: each target gets the type as a third type until it leaves the field or its type "
        "changes. Fails on a target that has the type. Line: bank 14 lines 2030 to 2032 (English, and French from "
        "Soak's \"prend le type\"; the game has no line that says a type was added in the other languages).",
        "Move record: a copy of Soak's."]},
    "trick_or_treat": {"keys": {"type": "ghost"}, "notes": [
        "eidos base: add_type Ghost (Trick-or-Treat).",
        "Move record: as Soak, Ghost."]},
    "forests_curse": {"keys": {"type": "grass"}, "notes": [
        "eidos base: add_type Grass (Forest's Curse).",
        "Move record: as Soak, Grass."]},
    "grassy_terrain": {"keys": {"turns": 5, "ratio": 1.5, "hp": "1/16"}, "notes": [
        "eidos base: for five turns, grounded Pokemon heal an HP fraction at each turn end, their Grass moves have "
        "more power (1.5, Generation VII; 1.3 for VIII), and Earthquake, Magnitude and Bulldoze do half damage to them.",
        "Move record: as Gravity.",
        "Lines: bank 15 lines 403 (start), 404 (end); the heal uses the game's \"HP was restored\" line."]},
    "electric_terrain": {"keys": {"turns": 5, "ratio": 1.5}, "notes": [
        "eidos base: for five turns, grounded Pokemon cannot fall asleep or get drowsy (Rest fails), and their "
        "Electric moves have more power.",
        "Move record: as Gravity.",
        "Lines: bank 15 lines 405, 406; bank 14 lines 2012 to 2014 (sleep blocked)."]},
    "psychic_terrain": {"keys": {"turns": 5, "ratio": 1.5}, "notes": [
        "eidos base: for five turns, grounded Pokemon are safe from other sides' moves of raised priority (the move "
        "record's, or an ability's: Prankster), and their Psychic moves have more power.",
        "Move record: as Gravity.",
        "Lines: bank 15 lines 407, 408; bank 14 lines 2015 to 2017 (move blocked)."]},
    "hits_floating": {"keys": {}, "notes": [
        "eidos building block: a Ground move hits Flying and floating Pokemon (Levitate, Magnet Rise, Air Balloon) "
        "as neutral. Thousand Arrows: 479 + hits_floating (Smack Down grounds the target).",
        "Move record: the move's own."]},
    "parting_shot": {"keys": {"stat": "atk", "stages": -1, "stat.2": "spatk", "stages.2": -1}, "notes": [
        "eidos base: the target's two stats fall, then the user switches out as after U-turn.",
        "Move record: a copy of Soak's; the stat changes are the base's."]},
    "topsy_turvy": {"keys": {}, "notes": [
        "eidos base: each target's stat stages turn the other way. Fails when none has a stage changed.",
        "Move record: a copy of Soak's.",
        "Lines: bank 14 lines 2018 to 2020."]},
    "needs_berry": {"keys": {}, "notes": [
        "eidos building block: the move fails unless the user has eaten a berry since it came in. It cannot be chosen "
        "before (the AI leaves it out; choosing it shows bank 14 lines 2045 to 2047, English only: \"But it failed!\" in "
        "the other languages).",
        "Move record: the move's own."]},
    "belch": {"keys": {}, "notes": [
        "eidos base: needs_berry (Belch).",
        "Move record: Poison, special, power 120, accuracy 90."]},
    "burn_up": {"keys": {"type": "fire"}, "notes": [
        "eidos base: fails unless the user has the type; after the damage the user loses it. A Pokemon of that type "
        "alone is left with no type: every move hits it for neutral damage.",
        "Move record: Fire, special, power 130, thaws the user.",
        "Lines: bank 14 lines 2021 to 2023."]},
    "shore_up": {"keys": {"ratio": 0.5, "ratio.2": 0.667}, "notes": [
        "eidos base: heals a part of the HP (ratio), more in a sandstorm (ratio.2: two thirds, Generation VII).",
        "Move record: as Recover."]},
    "strength_sap": {"keys": {"stat": "atk", "stages": -1}, "notes": [
        "eidos base: the user heals as much HP as the target's Attack (with its stages), then the target's stat falls. "
        "Fails when the target's Attack is at its lowest.",
        "Move record: a copy of Soak's."]},
    "rototiller": {"keys": {"type": "grass", "stat": "atk", "stages": 1, "stat.2": "spatk", "stages.2": 1},
                   "notes": [
        "eidos base: every grounded Pokemon of the type on the field has two stats raised. Fails when there is none.",
        "Move record: as Gravity."]},
    "magnetic_flux": {"keys": {"stat": "def", "stages": 1, "stat.2": "spdef", "stages.2": 1}, "notes": [
        "eidos base: every Pokemon on the user's side with Plus or Minus has two stats raised. Fails when there is none.",
        "Move record: a copy of Quick Guard's."]},
    "set_type": {"keys": {"type": "water"}, "notes": [
        "eidos building block: each target becomes the type alone (Soak with a type of your choice).",
        "Move record: as Soak."]},
    "electrify": {"keys": {"type": "electric"}, "notes": [
        "eidos base: the target's moves for the rest of the turn are of the type.",
        "Move record: as Soak.",
        "Lines: bank 14 lines 2024 to 2026."]},
    "ion_deluge": {"keys": {"type": "electric", "type.2": "normal"}, "notes": [
        "eidos base: for the rest of the turn, moves of type.2 (Normal) are of the type (Electric). Fails while it is up.",
        "Move record: as Gravity, priority 1.",
        "Lines: bank 15 line 409."]},
    "sticky_web": {"keys": {"stat": "speed", "stages": -1}, "notes": [
        "eidos base: a web on the foes' side: each grounded Pokemon that comes in there has its stat lowered. "
        "Fails while it is up. Defog takes it off its target's side, Rapid Spin off its user's (and the moves that "
        "work like them).",
        "Move record: as Spikes.",
        "Lines: bank 15 lines 410 (your side), 411 (the foe's side), 412 and 413 (taken away); bank 14 lines 2027 to "
        "2029 (caught, English only: the Speed line shows in the other languages)."]},
    "powder": {"keys": {"type": "fire", "hp": "1/4"}, "notes": [
        "eidos base: for the rest of the turn, a move of the type the target uses fails and explodes, costing it an HP "
        "fraction. Grass Pokemon are not covered.",
        "Move record: as Soak, Bug, priority 1.",
        "Lines: bank 14 lines 2033 to 2035 (covered), bank 15 line 414 (exploded); English only."]},
    "throat_chop": {"keys": {"turns": 2}, "notes": [
        "eidos base: each target the move hits cannot use sound moves for this turn and the next: they cannot be "
        "chosen, and fail when called.",
        "Move record: Dark, physical, power 80, contact.",
        "Lines: bank 14 lines 2036 to 2038 (Heal Block's shape; English only, \"But it failed!\" in the others)."]},
    "laser_focus": {"keys": {}, "notes": [
        "eidos base: the user's moves are critical hits for the rest of the turn and the next, unless the target is "
        "safe from them (Battle Armor, Lucky Chant).",
        "Move record: as Focus Energy.",
        "Lines: bank 14 lines 2039 to 2041 (English only)."]},
    "stomping_tantrum": {"keys": {"ratio": 2.0}, "notes": [
        "eidos base: the power is multiplied when the user's last move failed (missed, had no effect or did nothing).",
        "Move record: Ground, physical, power 75, contact."]},
    "spectral_thief": {"keys": {}, "notes": [
        "eidos base: before the damage, the user takes the target's raised stat stages for its own (up to +6), and "
        "the target's go back to neutral.",
        "Move record: Ghost, physical, power 90, contact.",
        "Lines: bank 14 lines 2042 to 2044 (English only)."]},
    "pollen_puff": {"keys": {"hp": "1/2"}, "notes": [
        "eidos base: aimed at an ally, the move heals it an HP fraction in place of the damage (Present's heal).",
        "Move record: Bug, special, power 90, aimed at any other Pokemon."]},
}

# eidos's own ability bases (sdk/battle_handlers.h BW_BH_PIXILATE and on, handlers
# in stubs/ov/patch_ov093_hand_tokusei.c), keyed by the name a line writes.  By
# hand, as the move bases: their keys are the rows of k_named_ability_keys in
# sdk/battle_handlers.c, their values the defaults of the patch's code.
_ATE_NOTE = ("eidos base: the holder's Normal moves take the type and have `ratio` times the power "
             "(1.2, Generation VII).")
_KIND_NOTE = ("`kind`: contact, punch, sound (the move record's flags), physical, special, status, or bite, "
              "pulse, ball, powder (eidos/tables/move_kinds.txt).")
NAMED_ABILITY_BASES = {
    "pixilate": {"keys": {"type": "fairy", "ratio": 1.2}, "notes": [
        _ATE_NOTE, "Without a `type`, the added type named fairy in types.txt; with none, the moves keep their "
        "type."]},
    "aerilate": {"keys": {"type": "flying", "ratio": 1.2}, "notes": [_ATE_NOTE]},
    "refrigerate": {"keys": {"type": "ice", "ratio": 1.2}, "notes": [_ATE_NOTE]},
    "galvanize": {"keys": {"type": "electric", "ratio": 1.2}, "notes": [_ATE_NOTE]},
    "boost_kind": {"keys": {"kind": "contact", "ratio": 1.3}, "notes": [
        "eidos base: the holder's moves of the kind have `ratio` times the power (Tough Claws, Strong Jaw, "
        "Mega Launcher, Punk Rock's attack side, Sharpness).", _KIND_NOTE]},
    "tough_claws": {"keys": {"ratio": 1.3}, "notes": ["eidos base: contact moves have 1.3 times the power."]},
    "strong_jaw": {"keys": {"ratio": 1.5}, "notes": ["eidos base: bite moves have 1.5 times the power."]},
    "mega_launcher": {"keys": {"ratio": 1.5}, "notes": [
        "eidos base: pulse moves have 1.5 times the power. Heal Pulse heals as the move's own handler does."]},
    "boost_type": {"keys": {"type": "steel", "ratio": 1.5}, "notes": [
        "eidos base: the holder's moves of the type have `ratio` times the power (Steelworker, Transistor, "
        "Dragon's Maw, Rocky Payload)."]},
    "guard_kind": {"keys": {"kind": "contact", "ratio": 0.5}, "notes": [
        "eidos base: moves of the kind do `ratio` of their damage to the holder (Fluffy's contact half, Ice "
        "Scales with kind=special, Punk Rock's sound half).", _KIND_NOTE]},
    "fur_coat": {"keys": {"ratio": 2.0}, "notes": [
        "eidos base: the holder's Defense is `ratio` times as high against physical moves."]},
    "block_kind": {"keys": {"kind": "ball"}, "notes": [
        "eidos base: moves of the kind have no effect on the holder, with its pop-up and the line "
        "\"It doesn't affect ...\" (Bulletproof, Overcoat's powder side with kind=powder).", _KIND_NOTE]},
    "bulletproof": {"keys": {}, "notes": ["eidos base: ball and bomb moves have no effect on the holder."]},
    "priority_type": {"keys": {"type": "flying", "priority": 1}, "notes": [
        "eidos base: the holder's moves of the type get `priority` more priority. With `hp`, only while its "
        "HP is at least that fraction of its maximum."]},
    "gale_wings": {"keys": {"priority": 1, "hp": "1/1"}, "notes": [
        "eidos base: Flying moves get +1 priority while the holder's HP is full (hp=1/1, Generation VII; "
        "priority_type for Generation VI's at any HP)."]},
    "entry_stat": {"keys": {"stat": "atk", "stages": 1}, "notes": [
        "eidos base: the holder's own stat changes when it enters the battle (Intrepid Sword, Dauntless "
        "Shield with stat=def)."]},
    "ko_stat": {"keys": {"stat": "atk", "stages": 1}, "notes": [
        "eidos base: the holder's stat changes for each Pokemon its move knocks out (Moxie on any stat: "
        "Chilling Neigh, Grim Neigh with stat=spatk)."]},
    "hit_type_stat": {"keys": {"type": "water", "stat": "def", "stages": 2}, "notes": [
        "eidos base: hit by a move of the type (or `type.2`), the holder's stat changes (Water Compaction; "
        "Steam Engine: type=fire type.2=water stat=speed stages=6; Thermal Exchange: type=fire stat=atk "
        "stages=1)."]},
    "sweet_veil": {"keys": {}, "notes": [
        "eidos base: the holder and its side cannot fall asleep, become drowsy or use Rest; the pop-up and "
        "\"It doesn't affect ...\" when it holds one off."], "work": [0, 1]},
    "aroma_veil": {"keys": {}, "notes": [
        "eidos base: the holder and its side are safe from infatuation, Taunt, Torment, Disable, Heal Block "
        "and Encore."], "work": [2, 3]},
    "flower_veil": {"keys": {"type": "grass"}, "notes": [
        "eidos base: Pokemon of the type on the holder's side keep their stats and get no major status from "
        "the other side; the line of Clear Body or \"It doesn't affect ...\"."], "work": [4, 5]},
    "cheek_pouch": {"keys": {"hp": "1/3"}, "notes": [
        "eidos base: when the holder eats its held berry, it also heals that fraction of its maximum HP. A "
        "berry it eats through Bug Bite or Pluck does not count."], "work": [0]},
    "protean": {"keys": {}, "notes": [
        "eidos base: when its move goes ahead (not asleep, frozen, flinching or fully paralysed), the holder "
        "takes the move's type, after Normalize and the -ate bases, with the pop-up and Color Change's line, "
        "before \"X used Y!\". A move Metronome or Sleep Talk calls counts, not the caller. Not Struggle."]},
    "magician": {"keys": {}, "notes": [
        "eidos base: holding nothing, the holder takes the item of the first Pokemon its move hits, with "
        "Pickpocket's line. Not with a move Sheer Force powers."]},
    "symbiosis": {"keys": {}, "notes": [
        "eidos base: when an ally uses up its held item (a berry, a herb, a gem; not one taken from it), the "
        "holder passes it its own, with Bestow's line. Not Mail, nor an item bound to a species."]},
    "parental_bond": {"keys": {"ratio": 0.25, "kind": "one_hit"}, "notes": [
        "eidos base: the holder's damaging moves that hit once hit twice, the second hit with `ratio` of the "
        "power (1/4, Generation VII; 0.5 for Generation VI), with \"Hit 2 times!\". Not moves that hit "
        "several targets, moves with a charge turn (the record's flag Sleep Talk tests), nor moves of `kind` "
        "(one_hit: Self-Destruct, Explosion, Endeavor, Fling and Final Gambit; move_kinds.txt changes the "
        "list)."], "work": [0]},
    "stance_change": {"keys": {"form": 1, "form.2": 0}, "notes": [
        "eidos base: before a damaging move (\"X used Y!\") the holder takes its attack form (`form`), before King's Shield (a "
        "move whose line names kings_shield) its guard form (`form.2`), with its pop-up and \"X transformed!\". "
        "It takes the guard form again when it leaves the battle. The species needs both forms (a mod's "
        "Aegislash-style species)."]},
    "grass_pelt": {"keys": {"ratio": 1.5}, "notes": [
        "eidos base: the holder's Defense is `ratio` times as high while Grassy Terrain (grassy_terrain) is up."]},
}
KINDS_OF_MOVE = ["contact", "punch", "sound", "bite", "pulse", "ball", "powder", "physical", "special", "status",
                 "one_hit"]

SITE_ENUM = {RANK: "BW_BH_SITE_RANK", SICK: "BW_BH_SITE_SICK", WEATHER: "BW_BH_SITE_WEATHER",
             CHANCE: "BW_BH_SITE_CHANCE", HP: "BW_BH_SITE_HP", RATIO: "BW_BH_SITE_RATIO", TYPE: "BW_BH_SITE_TYPE",
             TURNS: "BW_BH_SITE_TURNS", DAMAGE: "BW_BH_SITE_DAMAGE", POWER: "BW_BH_SITE_POWER",
             ACCURACY: "BW_BH_SITE_ACCURACY"}


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
        "type": TYPES, "ratio": [0.1, 4.0], "hp": ["1/1", "1/16"], "weather": sorted(WEATHER_NAMES.values()),
        "turns": [1, 63], "damage": [1, 999], "power": [1, 255], "accuracy": [1, 100],
        "kind": KINDS_OF_MOVE, "priority": [-6, 6], "form": [0, 31]}}
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
        if kind == "ability":
            doc[kind].update(NAMED_ABILITY_BASES)
        adj = sum(1 for r in res.values() if r["keys"])
        print("%-8s %3d ids, %3d with adjustable keys, %4d keys" % (
            kind, len(res), adj, sum(len(r["keys"]) for r in res.values())))
    open(os.path.join(OUT, "handler_params.json"), "w", newline="\n").write(json.dumps(doc, indent=1) + "\n")
    open(os.path.join(OUT, "battle_handler_sites.inc"), "w", newline="\n").write("\n".join(inc) + "\n")
    open(os.path.join(OUT, "battle_handler_work.inc"), "w", newline="\n").write("\n".join(winc) + "\n")
