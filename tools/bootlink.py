#!/usr/bin/env python3
"""
bootlink.py -- what it takes to link the game's own boot (docs/boot.md).

The boot (NitroMain and the closure of its matched callees, about 1400
translation units) is compiled from aletheia's matched C unchanged.  Linking
it on a host needs five things the DS linker gives for free, and this tool
works each of them out mechanically from aletheia's layout and sources and
from the port's own C:

 1. The source list.  tools/closure.py's fixpoint from NitroMain's callees,
    minus the matched files a port shim replaces (cmake/boot.cmake,
    EIDOS_BOOT_SHIM_REPLACED, checked here against the names the shims
    actually define), minus the files whose bodies are mwcc `asm` (a host
    compiler cannot build them; sdk/asm/<file>.c replaces one when it
    exists), minus the files the eidos library already compiles.

 2. Names by address.  The DS links by address, a host by name: a caller
    that says RTC_Init reaches a matched file that defines func_02088788,
    and a caller that says func_02088610 reaches the shim's
    PXI_SendWordByFifo.  Each translation unit gets a forced-include header
    (-include, /FI) that renames, for that file only, every such spelling
    to the one that is defined.  The matched file is not touched
    (portability rule 1).

 3. Implicit declarations.  mwcc accepts a call with no declaration in
    scope; GCC 14 does not.  The same header declares each such callee with
    the return type of its own definition and no parameter list (C99's
    non-prototype declaration: arguments get the default promotions, which
    is what the DS code was compiled against).

 4. Link-time constants.  `extern char SDK_IRQ_STACKSIZE[]` is 0x800
    because the DS linker puts the symbol *at* 0x800 (closure.py, class
    `const`).  The header renames each to `(*bw_lc_NAME)`, a pointer object
    whose value is the constant, so `(u32)SDK_IRQ_STACKSIZE` is 0x800 and
    `&SDK_AUTOLOAD_DTCM_START` is a pointer.  A value inside the DS address
    space is set at start-up to bw_ds_ptr(value) (bw_boot_link_init), so
    SDK_AUTOLOAD_DTCM_START names the bytes at DS 0x02FE0000 and the game's
    vblank flag, OS_WaitIrq and the shims all see the same word.  DTCM
    objects (0x02FE0000-0x02FE3FFF) no shim defines are pinned the same way.

 5. Data.  Every ARM9 data name a matched file uses is tools/armdata.py's
    (docs/armdata.md): bound at reset to its DS address in the player's own
    image.  This tool reads that generator's header (--armdata-header) and
    leaves every name it binds alone; a data name it does not bind and
    nothing defines fails the generation, by name.

 6. Functions nothing defines, generated and listed.  A linker veneer (a
    `ldr ip, [pc]; bx ip` island with no source) the boot names is another
    address of its target: the image says so (--arm9-image; aletheia's
    listing labels the same islands `veneer_XXXXXXXX`), so it becomes a
    forwarder to what defines the target, and the target's source joins the
    closure (plan).  Unmatched game code (`open`) and the asm-bodied SDK
    files of step 1 with no sdk/asm/ replacement yet become stubs that
    panic with their address and name when called.

The declared set (mod/declared_set.txt) is emitted as well: one table of
the whole set, the arm9 rows and the overlay rows together, beside the slot
publications in boot_link.c, and bw_boot_link_init registers it
(docs/mod-state.md, section 4).  The table is the run-time answer to the
manifest's check, so it carries every row, not only the rows this link
defines.

Two modes:

    bootlink.py plan --aletheia-root R --seed S ... --exclude-file F
                     --shim-sources L --port-sources P --library-matched M
                     --asm-dir D [--define NAME=VALUE] --out-dir O
        configure time: writes O/boot_matched.txt (the sources to compile),
        O/boot_asm.txt (asm-bodied files and how each is handled),
        O/boot_asm_subst.txt (sdk/asm replacements to compile) and
        O/boot_plan.txt (a summary).  Fails when the exclusion list and the
        shims' definitions disagree.

    bootlink.py generate --aletheia-root R --plan-dir O --shim-sources L
                     --port-sources P --armdata-header H
                     [--define NAME=VALUE] --out-dir G
        build time: writes one header per translation unit
        (G/include/<name>.h), G/boot_link.c and
        G/boot_link_report.txt.

Standard library only; reads aletheia, writes only under the given dirs.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

# Run from the build: no __pycache__ in the source tree.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import closure  # noqa: E402
import gametree  # noqa: E402  (pair_members)
import ovlink    # noqa: E402  (the prototype normaliser, tools/ovlink.py)

DATA_ADDR_NAME = re.compile(r"^[A-Za-z]+_([0-9A-Fa-f]{8})$")
BW_REG = re.compile(r"\bBW_REG\(\s*(0x[0-9A-Fa-f]+)u?\s*,\s*([A-Za-z_]\w*)")
DTCM_LO, DTCM_HI = 0x02FE0000, 0x02FE4000
# A function whose whole body is one call (an alias forwarder):
# `T name(params) { [return] [(cast)] callee(...); }`.
FORWARDER = re.compile(
    r"\b([A-Za-z_]\w*)\s*\([^;{}()]*\)\s*\{\s*(?:return\s+)?(?:\([^()]*\)\s*)?"
    r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;\s*\}")

VENEERS = closure.VENEERS
DS_LO, DS_HI = 0x02000000, 0x08000000

# The ARM9 static module's load address (layout.json's arm9.load_address,
# docs/armdata.md): the first byte of arm9_dec.bin is DS 0x02004000.
ARM9_IMAGE_BASE = 0x02004000
# A linker veneer: `ldr ip, [pc]` and `bx ip`, then the target word.
VENEER_CODE = b"\x00\xc0\x9f\xe5\x1c\xff\x2f\xe1"


def read_image(path):
    """The player's ARM9 image, or None.  Its first byte is DS 0x02004000
    (layout.json's arm9.load_address, docs/armdata.md)."""
    if not path or not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return fh.read()


def veneer_target(img, addr):
    """The target of the linker veneer at `addr` in the image, or None.

    aletheia's reassembly listing (asm/arm9/main.s) labels each veneer
    `veneer_XXXXXXXX` and tools/closure.py reads those labels, but the
    listing is generated and is not in a clean export of the pin, so a
    veneer outside the hand-kept table there is missed and becomes a panic
    stub.  The image is what the listing was made from: the 12-byte island
    `ldr ip, [pc]; bx ip` (0xE59FC000 0xE12FFF1C) and the target word after
    it, at the veneer's own address in the static module.
    """
    if img is None or addr is None:
        return None
    off = addr - ARM9_IMAGE_BASE
    if off < 0 or off + 12 > len(img) or img[off:off + 8] != VENEER_CODE:
        return None
    return int.from_bytes(img[off + 8:off + 12], "little")


def read_list(path):
    out = []
    if not path:
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                out.append(line.replace("\\", "/"))
    return out


def write_if_changed(path, text):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as fh:
            if fh.read() == text:
                return
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def rel_to_root(path):
    """A source path as the manifest spells it (src/...), or None."""
    path = os.path.abspath(path).replace("\\", "/")
    root = os.path.abspath(closure.ROOT).replace("\\", "/")
    if path.lower().startswith(root.lower() + "/"):
        return path[len(root) + 1:]
    return None


# Port-side macros the generated build passes (bootlink.py --define), for
# the `#if EIDOS_HEAP_MATCHED` blocks in sdk/ that decide which names exist.
PORT_DEFINES = {}
PP_IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _pp_value(kind, expr):
    """True/False for the conditionals this build decides, None otherwise."""
    expr = expr.split("/*")[0].split("//")[0].strip()
    if kind in ("ifdef", "ifndef"):
        if expr not in PORT_DEFINES:
            return None
        return (kind == "ifdef")
    neg = expr.startswith("!")
    name = expr[1:].strip() if neg else expr
    if name in PORT_DEFINES:
        value = PORT_DEFINES[name] not in ("0", "")
        return not value if neg else value
    return None


def preprocess(text):
    """Blank the branches of the conditionals PORT_DEFINES decides."""
    out = []
    stack = []                 # (this branch live, some branch taken, decided)
    for line in text.split("\n"):
        m = PP_IF.match(line)
        live = all(frame[0] for frame in stack)
        if m:
            kind, expr = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                v = _pp_value(kind, expr)
                stack.append([v is not False, v is True, v is not None])
            elif kind == "elif" and stack:
                frame = stack[-1]
                v = _pp_value("if", expr)
                if frame[2] and v is not None:
                    frame[0] = (not frame[1]) and v
                    frame[1] = frame[1] or v
                else:
                    frame[0], frame[2] = True, False
            elif kind == "else" and stack:
                frame = stack[-1]
                frame[0] = (not frame[1]) if frame[2] else True
            elif kind == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if live else "")
    return "\n".join(out)


def scan_file(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return closure.SourceScan(preprocess(fh.read()))


def header_name(path):
    """include/<flattened path>.h for one translation unit."""
    rel = rel_to_root(path) or ("port/" + os.path.basename(path))
    return re.sub(r"[^A-Za-z0-9_]", "_", rel[:-2] if rel.endswith(".c") else rel) + ".h"


class Shims:
    """Every non-static name the eidos library (and boot port code) defines."""

    def __init__(self, idx, paths):
        self.defines = {}            # name -> defining file
        self.dups = []
        self.data_at = {}            # DS address -> data object name
        self.forwards = {}           # name -> the one function its body calls
        for path in paths:
            if not os.path.exists(path):
                continue             # generated at build time (overlay table)
            rel = rel_to_root(path)
            if rel and rel in idx.sources:
                sc = idx.scan(rel)
            else:
                sc = scan_file(path)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
                for addr, name in BW_REG.findall(text):
                    self.data_at.setdefault(int(addr, 16), name)
                for name, callee in FORWARDER.findall(text):
                    self.forwards.setdefault(name, callee)
            for name in sorted(sc.defined - sc.statics):
                if name in self.defines and self.defines[name] != path:
                    self.dups.append((name, self.defines[name], path))
                    continue
                self.defines[name] = path


# ---------------------------------------------------------------------------
# plan
# ---------------------------------------------------------------------------

def load_member_addresses(idx):
    """Fill in the per-member addresses aletheia's manifest lists.

    closure.Index maps a unit's members by the address their own name spells
    (func_XXXXXXXX) or the SDK table, so a member aletheia names
    descriptively (SAVEDATA_GetTotalSize at 0x02033BF8 in unit_02033BF0.c)
    gets none, and a caller that spells it func_02033BF8 looks like an open
    function: bootlink.py emits a panic stub beside the unit's real
    definition.  The manifest pairs each unit's `functions` with its
    `addresses` (an older shard spells the same list `covers`); read that
    pair here, where the index does not, so a reference by any member
    address resolves to the unit that defines it.  tools/ovlink.py reads the
    same pair, so the boot and the overlays rename a spelling together and
    neither side loses the stub the other resolves to.
    """
    if closure.SOURCE_TREE == "game":
        return      # closure.Index._load_game placed every member already
    pattern = os.path.join(closure.ROOT, "layout", "matched", "**", "*.json")
    for path in sorted(glob.glob(pattern, recursive=True)):
        try:
            with open(path, encoding="utf-8") as fh:
                shard = json.load(fh)
        except (OSError, ValueError):
            continue
        for e in shard.get("functions", []):
            if e.get("status") not in ("EXACT", "EXACT*"):
                continue
            source = (e.get("source") or "").replace("\\", "/")
            entry = idx.sources.get(source)
            if entry is None:
                continue
            addrs = e.get("addresses") or e.get("covers") or []
            # Overlays share address ranges, so only the static image is
            # resolved by address (closure.Index._load_shards does the same).
            static = not entry["region"].startswith("ov")
            rows = dict(idx.rt_by_name)
            for k, v in idx.sdk_by_name.items():
                rows.setdefault(k, v)
            paired = gametree.pair_members(entry["functions"], [int(a, 16) for a in addrs],
                                           rows if static else None)
            for k, name in enumerate(entry["functions"]):
                if k >= len(addrs) or paired[k] is None:
                    break
                addr = paired[k]
                idx.member_addr[(source, name)] = addr
                idx.member_by_addr.setdefault((source, addr), name)
                if static:
                    idx.by_addr.setdefault(addr, entry)


def defined_addr(idx, rel, name):
    """The DS address of a function `rel` (a matched source, or None for
    port code) defines as `name`.

    The manifest knows the lead function of every unit by address whatever
    it is called (OS_LockCard at 0x02084E38, NHTTP_CleanupAsync at
    0x0206074C, names the SDK table spells otherwise or not at all); a
    member's address comes from its func_/sub_ spelling or the SDK table.
    So any name whose address a compiled source defines resolves to it.
    """
    if rel:
        # The manifest's own address for the lead first: the SDK table's
        # spelling of a name can sit at another address (a signature
        # matcher's guess), and the manifest is what the DS build linked.
        entry = idx.sources.get(rel)
        if entry is not None and closure.SOURCE_TREE != "game" \
                and name in (entry["name"], entry["functions"][0]):
            # (A src/game unit lists its functions in no address order: its
            # members' addresses are all in member_addr below.)
            return entry["address"]
        addr = idx.member_addr.get((rel, name))
        if addr is not None:
            return addr
    addr = idx._name_addr(name)
    if addr is None:
        addr = idx.rt_by_name.get(name)
    return addr


_OVERLAYS = None


def overlays_at(idx, addr):
    """The overlays whose code range holds `addr`, as "ov012" or "ov012 or
    ov013": the one aletheia's manifest has a function at exactly `addr`
    when there is one, else every overlay loaded over that range (overlays
    share address ranges, so the DS address alone does not say which)."""
    global _OVERLAYS
    if _OVERLAYS is None:
        _OVERLAYS = []
        path = os.path.join(closure.ROOT, "layout", "layout.json")
        if os.path.exists(path):
            import json
            with open(path, encoding="utf-8") as fh:
                for ov in json.load(fh).get("overlays", []):
                    _OVERLAYS.append((ov["id"], ov["ram_start"], ov["ram_end"]))
    exact = sorted({e["region"] for e in idx.entries
                    if e["address"] == addr and e["region"].startswith("ov")})
    if exact:
        return " or ".join(exact)
    hits = ["ov%03d" % i for i, lo, hi in _OVERLAYS if lo <= addr < hi]
    if len(hits) > 4:
        return "one of %d overlays, %s .. %s" % (len(hits), hits[0], hits[-1])
    return " or ".join(hits)


def port_references(idx, path):
    """The matched sources whose functions a port file calls, by name or by
    address spelling."""
    if not os.path.exists(path):
        return []
    sc = scan_file(path)
    out = set()
    for name in sc.references(idx.is_global_name)[0]:
        if name in sc.defined:
            continue
        owner = idx.defines.get(name)
        if owner is None:
            addr = idx._name_addr(name)
            entry = idx.entry_at(VENEERS.get(addr, addr)) if addr is not None else None
            owner = entry["source"] if entry else None
        if owner is not None:
            out.add(owner)
    return sorted(out)


def defined_names(idx, source):
    """A matched source's non-static names, the SDK names at their
    addresses, and the address spelling (func_XXXXXXXX) of each, so that a
    shim matches the file by address whatever either side calls it."""
    sc = idx.scan(source)
    out = set()
    for name in sc.defined - sc.statics:
        out.add(name)
        addr = defined_addr(idx, source, name)
        if addr is not None:
            out.add("func_%08X" % addr)
            if addr in idx.sdk_by_addr:
                out.add(idx.sdk_by_addr[addr])
    return out


THUNK_BODY = re.compile(
    r"^\s*(?:void|int|u32|s32|unsigned\s+int|BOOL)\s+([A-Za-z_]\w*)\s*\(\s*void\s*\)\s*\{"
    r"\s*(?:return\s+)?([A-Za-z_]\w*)\s*\(\s*\)\s*;\s*\}", re.M)


def thunk_of(idx, source):
    """(name, callee) when the matched source's one function is a bare tail
    call `void f(void) { g(); }`, else None."""
    sc = idx.scan(source)
    funcs = sc.defined - sc.statics
    if len(funcs) != 1:
        return None
    text = open(os.path.join(closure.ROOT, source), encoding="utf-8", errors="replace").read()
    code = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    m = THUNK_BODY.search(code)
    if not m or m.group(1) not in funcs:
        return None
    return m.group(1), m.group(2), text


def write_thunk(idx, source, out_dir):
    """A port unit for a tail-call thunk whose callee takes arguments: the
    thunk forwards them, as the DS does (r0-r3 untouched by `ldr ip, =g;
    bx ip`), with the callee's prototype normalised as tools/ovlink.py's
    trampolines have it.  Returns (path, None) or (None, why)."""
    import ovlink  # noqa: E402  (tools/ovlink.py, the prototype normaliser)

    t = thunk_of(idx, source)
    if t is None:
        return None, None
    name, callee, text = t
    addr, _kind = idx.address_of(source, callee)
    if addr is None:
        return None, "callee %s has no address" % callee
    entry = idx.entry_at(addr)
    if entry is None:
        return None, "nothing defines 0x%08X" % addr
    csrc = entry["source"]
    path = os.path.join(closure.ROOT, csrc)
    sc = ovlink.Scan(path, ovlink.read_text(path))
    cname = None
    for n in sorted(sc.heads):
        if defined_addr(idx, csrc, n) == addr:
            cname = n
            break
    if cname is None:
        return None, "no definition of 0x%08X in %s" % (addr, csrc)
    sig = ovlink.signature(sc.heads[cname], cname, sc.typedefs)
    if sig is None:
        return None, "the prototype of %s does not normalise" % cname
    ret, params = sig
    if not params and ret == "void":
        return None, None                  # nothing travels: the C is right
    isa = re.search(r"^\s*\*\s*isa:\s*(arm|thumb)\b", text[:4096], re.M)
    args_ = ", ".join("a%d" % k for k in range(len(params)))
    lines = [
        "/*",
        " * function: %s" % name,
        " * isa:      %s" % (isa.group(1) if isa else "arm"),
        " *",
        " * Generated by tools/bootlink.py in place of %s:" % source,
        " * `%s` is a tail call (the DS passes r0-r3 through" % ("%s() { %s(); }" % (name, callee)),
        " * untouched), so it forwards what %s takes" % cname,
        " * (%s), and returns what it returns.  docs/boot.md," % source_of_decl(csrc),
        " * \"Arguments in registers\".",
        " */",
        "",
        "%s" % ovlink.proto_decl(ret, callee, params),
        "%s;" % ovlink.proto(ret, name, params),
        "",
        ovlink.proto(ret, name, params),
        "{",
        "    %s%s(%s);" % ("" if ret == "void" else "return ", callee, args_),
        "}",
        "",
    ]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, os.path.basename(source)).replace("\\", "/")
    write_if_changed(out, "\n".join(lines))
    return out, None


def source_of_decl(rel):
    return rel


def plan(args):
    idx = closure.Index()
    load_member_addresses(idx)
    image = read_image(getattr(args, "arm9_image", None))
    seeds = [idx.resolve_seed(s) for s in args.seed]
    # A seed file lists whole directories' worth (EIDOS_BOOT_ALL_GAME): a
    # file aletheia has added to src/matched before its manifest lists it
    # is left out until the manifest does, rather than failing the plan.
    unlisted = []
    for path in args.seed_file:
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                s = line.strip()
                if not s:
                    continue
                try:
                    seeds.append(idx.resolve_seed(s))
                except SystemExit:
                    unlisted.append(s)
    excluded = read_list(args.exclude_file)
    if closure.SOURCE_TREE == "game":
        # src/matched paths: src/game cuts the shims' functions by name
        # instead (plan_game), so the list only documents them.  Its
        # src/game rows are units the port replaces whole (crt0.c: the
        # port starts the game itself).
        args.game_excluded = set(s for s in excluded if s.startswith("src/game/"))
        excluded = list(args.game_excluded)
    unknown = [s for s in excluded if s not in idx.sources]
    if unknown:
        raise SystemExit("bootlink.py: excluded sources not in the manifest: %s"
                         % ", ".join(unknown))
    library = set()
    for path in read_list(args.library_matched):
        rel = rel_to_root(path)
        if rel:
            library.add(rel)

    # The boot's own port code (NitroMain's reimplementation, sdk/boot/,
    # stubs/reimpl_ files, the sdk/asm/ replacements of asm files in the
    # closure) calls matched functions too: those seed the walk as well,
    # until nothing new is reached.
    port_paths = read_list(args.port_sources) if args.port_sources else []
    asm_dir_abs = os.path.abspath(args.asm_dir) if args.asm_dir else None
    library_paths = set(read_list(args.library_matched))
    while True:
        sources, open_refs = idx.closure(seeds, stop=set(excluded))
        extra = list(port_paths)
        if asm_dir_abs:
            for source in sources:
                repl = os.path.join(asm_dir_abs, os.path.basename(source))
                if idx.scan(source).has_asm and os.path.exists(repl):
                    extra.append(repl)
        more = []
        for path in extra:
            for rel in port_references(idx, path):
                if rel not in seeds and rel not in sources and rel not in library \
                        and os.path.join(closure.ROOT, rel).replace("\\", "/") not in library_paths:
                    more.append(rel)
        # A function the boot names that nothing defines, whose bytes in the
        # image are a linker veneer: generate() emits a forwarder to the
        # target, so the target's source has to be in the link.
        for (_name, klass, detail), _srcs in open_refs.items():
            if klass != "open" or not detail.startswith("0x"):
                continue
            target = veneer_target(image, int(detail, 16))
            entry = idx.entry_at(target) if target is not None else None
            if entry is not None:
                rel = entry["source"]
                if rel not in seeds and rel not in sources and rel not in library:
                    more.append(rel)
        if not more:
            break
        seeds = seeds + sorted(set(more))

    # The library's port code (sdk/, stubs/, and the stubs/reimpl_ files its
    # matched lists carry) is what the boot's matched files must not clash
    # with.
    shim_paths = read_list(args.library_matched)
    for listing in args.shim_sources:
        shim_paths += read_list(listing)
    shims = Shims(idx, [p for p in shim_paths if rel_to_root(p) is None])

    # The exclusion list has to be exactly the closure's sources that define
    # a name a shim defines: one missing is a duplicate symbol at link time,
    # one extra is a matched file left out for no reason.
    asm_dir = os.path.abspath(args.asm_dir) if args.asm_dir else None
    shim_set = {os.path.normcase(os.path.abspath(p)) for p in shim_paths}

    def asm_replacement(source):
        """sdk/asm/<same name>.c for an asm-bodied source, or None."""
        if not asm_dir or not idx.scan(source).has_asm:
            return None
        repl = os.path.join(asm_dir, os.path.basename(source))
        return repl if os.path.exists(repl) else None

    # A shim that only forwards to the address's other spelling
    # (sdk/aliases.c: sub_02060C5C calls func_02060C5C) is a second name for
    # the matched function, not a replacement of it.
    shim_addrs = set()
    for n in shims.defines:
        addr = idx._name_addr(n)
        if addr is None:
            continue
        callee = shims.forwards.get(n)
        if callee is not None and idx._name_addr(callee) == addr:
            continue
        shim_addrs.add(addr)
    clashes = {}
    for source in sources:
        if source in library or asm_replacement(source):
            # An asm file is never compiled: its sdk/asm/ replacement (in
            # the library or compiled for the boot) defines its names.
            continue
        # By name, and by address: a shim defining sub_02086788 and a
        # matched file defining func_02086788 are one function twice, and a
        # caller would reach one or the other depending on its spelling.
        hit = sorted(n for n in defined_names(idx, source)
                     if n in shims.defines
                     or (idx._name_addr(n) is not None
                         and idx._name_addr(n) in shim_addrs))
        if hit:
            clashes[source] = hit
    in_closure = set(sources)
    missing = sorted(s for s in clashes if s not in excluded)
    extra = sorted(s for s in excluded if s in in_closure and s not in clashes)

    if closure.SOURCE_TREE == "game":
        return plan_game(args, idx, sources, seeds, unlisted, library, shims, shim_addrs,
                         shim_set)

    compile_list, asm_rows, subst, thunks = [], [], [], []
    for source in sorted(sources):
        if source in excluded or source in library:
            continue
        if idx.scan(source).has_asm:
            repl = asm_replacement(source)
            if repl and os.path.normcase(os.path.abspath(repl)) in shim_set:
                # BW_ASMSDK_SOURCES: already in the eidos library.
                asm_rows.append("%s in-library %s" % (source, repl.replace("\\", "/")))
            elif repl:
                asm_rows.append("%s replaced-by %s" % (source, repl.replace("\\", "/")))
                subst.append(repl.replace("\\", "/"))
            else:
                asm_rows.append("%s pending" % source)
            continue
        thunk, why = write_thunk(idx, source, os.path.join(args.out_dir, "thunk"))
        if thunk is not None:
            thunks.append("%s thunk %s" % (source, thunk))
            subst.append(thunk)
            continue
        if why is not None:
            thunks.append("%s kept: %s" % (source, why))
        compile_list.append(os.path.join(closure.ROOT, source).replace("\\", "/"))

    out = args.out_dir
    write_if_changed(os.path.join(out, "boot_thunks.txt"), "".join(r + "\n" for r in thunks))
    write_if_changed(os.path.join(out, "boot_matched.txt"), "".join(p + "\n" for p in compile_list))
    write_if_changed(os.path.join(out, "boot_asm.txt"), "".join(r + "\n" for r in asm_rows))
    write_if_changed(os.path.join(out, "boot_asm_subst.txt"), "".join(p + "\n" for p in subst))

    lines = [
        "closure from %d seeds: %d matched translation units" % (len(seeds), len(sources)),
        "  replaced by a port shim (cmake/boot.cmake): %d"
        % len([s for s in sources if s in excluded]),
        "  already in the eidos library: %d" % len([s for s in sources if s in library]),
        "  mwcc asm bodies: %d (%d with an sdk/asm replacement)" % (len(asm_rows), len(subst)),
        "  tail-call thunks forwarding their callee's arguments: %d"
        % len([t for t in thunks if " thunk " in t]),
        "  compiled for the boot: %d" % len(compile_list),
        "  seeds not in aletheia's manifest yet, left out: %d%s" % (
            len(unlisted), (" (" + ", ".join(sorted(os.path.basename(u) for u in unlisted)) + ")")
            if unlisted else ""),
    ]
    for source in missing:
        lines.append("NOT EXCLUDED, defines a shim's name: %s (%s)"
                     % (source, ", ".join(clashes[source])))
    for source in extra:
        lines.append("excluded without a name clash (kept out by decision): %s" % source)
    write_if_changed(os.path.join(out, "boot_plan.txt"), "\n".join(lines) + "\n")
    print("\n".join(lines))
    if missing:
        return 1
    return 0


def _is_moved_unit(args, path):
    """Whether `path` is a function the plan moved out of the unit that
    called it (move_declared_calls): it is written in the build directory's
    `gen` folder."""
    gen = os.path.join(os.path.abspath(args.out_dir), "gen")
    return os.path.dirname(os.path.abspath(path)) == os.path.normcase(gen) \
        or os.path.dirname(os.path.abspath(path)) == gen


def move_declared_calls(args, idx, source, full, text, drop, declared):
    """The declared functions this unit calls: they are cut out of the unit's
    copy and put in a unit of their own, so the unit's calls reach the slot.

    A call inside the unit that defines a declared function binds by name
    and does not pass the slot (docs/mod-interface.md, section 2), so the
    event the row carries does not fire there and a raw hook on the row
    misses those calls.  The port's own answer for a merged unit is the one
    it uses when the port supplies a function of one (docs/game-tree.md):
    the definition is cut out of the unit's copy and put in a unit of its
    own, and the unit's calls reach the slot like every other caller's do.

    The moved text is the game's own, and the file is written into the build
    directory and listed with the boot's other units, so it is compiled
    under the boot's names like any other.  A function that uses what only
    its own unit has (a file-local helper or object) is left where it is:
    the port says so, and the row keeps the limit it had.

    Returns the paths of the moved files.
    """
    import gamecut
    out = []
    sc = idx.scan(source)
    base = os.path.dirname(os.path.abspath(full))
    for name, hs, bo, be, is_asm in gamecut.functions(text):
        if name in drop or is_asm:
            continue
        addr = defined_addr(idx, source, name)
        if addr is None or declared.get(name) != addr:
            continue
        if name not in sc.called:
            continue
        body = text[hs:be]
        used = set(re.findall(r"[A-Za-z_]\w*", body))
        clash = sorted(used & sc.statics)
        if clash:
            print("bootlink.py: %s uses %s, which only its own unit has: the "
                  "call inside that unit does not pass the slot"
                  % (name, ", ".join(clash)))
            continue
        incs = []
        for inc in re.findall(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', text, re.M):
            hp = os.path.normpath(os.path.join(base, inc))
            if not os.path.isfile(hp):
                incs = None
                break
            incs.append(hp.replace("\\", "/"))
        if incs is None:
            continue
        if ovlink._headers_need_rewrite(incs):
            # The build rewrites a header that holds a fixed DS address, and
            # it writes the rewrite next to the copy it compiles.  The moved
            # unit names the tree's headers by their own path, so the
            # rewrite would land in the tree: the function stays where it is.
            print("bootlink.py: %s needs a header the build rewrites: the "
                  "call inside its own unit does not pass the slot" % name)
            continue
        # The moved unit carries the unit's own headers and typedefs, in the
        # order the unit has them: a typedef the unit declares before the
        # header that uses it (the SDK's OSIntrMode) is part of what the
        # moved text needs.
        prelude = ovlink.unit_prelude(full, text, hs)
        if prelude is None:
            continue
        gen = os.path.join(os.path.abspath(args.out_dir), "gen")
        os.makedirs(gen, exist_ok=True)
        path = os.path.join(gen, "%s.c" % name)
        lines = [
            "/*",
            " * generated by tools/bootlink.py; do not edit",
            " *",
            " * The game's own %s, moved out of %s." % (name, source),
            " *",
            " * A call inside the unit that defines a declared function binds by",
            " * name and does not pass the slot, so the row's event does not fire",
            " * there (docs/mod-interface.md, section 2).  The definition is cut",
            " * out of that unit and put here, and the unit's calls reach the",
            " * slot like every other caller's do.  The text is the game's own.",
            " */",
            "",
        ]
        for prelude_line in prelude:
            lines.append(prelude_line)
        lines.append("")
        lines.append('#line %d "%s"' % (text.count("\n", 0, hs) + 1,
                                        full.replace("\\", "/")))
        lines.append(body)
        write_if_changed(path, "\n".join(lines) + "\n")
        drop.append(name)
        out.append(path)
    return out


def plan_game(args, idx, sources, seeds, unlisted, library, shims, shim_addrs, shim_set):
    """plan() for src/game (docs/game-tree.md).  Every unit of the closure is
    compiled; what the port supplies itself is cut out of the unit's copy by
    name instead of leaving the file out: a function a shim or the eidos
    library defines (by name or by address), and every mwcc asm body, which
    sdk/asm/ supplies by name where a replacement exists."""
    import gamecut
    out = args.out_dir
    lib_names = set()
    for rel in library:
        lib_names |= defined_names(idx, rel)
    asm_dir = os.path.abspath(args.asm_dir) if args.asm_dir else None
    asm_by_name, asm_by_addr = {}, {}
    if asm_dir and os.path.isdir(asm_dir):
        for fname in sorted(os.listdir(asm_dir)):
            if fname.endswith(".c"):
                path = os.path.join(asm_dir, fname)
                for n in scan_file(path).defined:
                    asm_by_name.setdefault(n, path.replace("\\", "/"))
                    # sdk/asm/ spells many by address (func_0200421A.c, the
                    # src/matched name): src/game's SVC_CpuSet is that file.
                    a = idx._name_addr(n)
                    if a is not None:
                        asm_by_addr.setdefault(a, path.replace("\\", "/"))
    cuts, compile_list, asm_rows, subst = [], [], [], set()
    ncut = 0
    # The declared rows whose own unit calls them (docs/mod-interface.md,
    # section 2): a call inside the unit that defines the function binds by
    # name and does not pass the slot, so the definition is cut out of that
    # unit and put in a unit of its own, and the unit's calls reach the slot
    # like every other caller's do.
    declared = {}
    for addr, dname, _region, _events in read_declared_set("arm9"):
        declared.setdefault(dname, addr)
    for source in sorted(sources):
        if source in getattr(args, "game_excluded", ()):
            continue
        full = os.path.join(closure.ROOT, source).replace("\\", "/")
        with open(full, encoding="latin-1") as fh:
            text = fh.read()
        drop = []
        for name, _hs, _bo, _be, is_asm in gamecut.functions(text):
            addr = defined_addr(idx, source, name)
            port = name in shims.defines or name in lib_names or name in LIBC_FUNCS \
                or (addr is not None and (addr in shim_addrs
                                          or "func_%08X" % addr in shims.defines))
            if port:
                drop.append(name)
            elif is_asm:
                repl = asm_by_name.get(name)
                if repl is None and addr is not None:
                    repl = asm_by_addr.get(addr)
                if repl is None:
                    asm_rows.append("%s pending %s" % (source, name))
                elif os.path.normcase(os.path.abspath(repl)) in shim_set:
                    asm_rows.append("%s in-library %s %s" % (source, name, repl))
                else:
                    asm_rows.append("%s replaced-by %s %s" % (source, name, repl))
                    subst.add(repl)
        for path in (move_declared_calls(args, idx, source, full, text, drop,
                                         declared)
                     if getattr(args, "mod_moves", False) else []):
            compile_list.append(path)
        if drop:
            cuts.append("%s\t%s" % (full, ",".join(sorted(drop))))
            ncut += len(drop)
        compile_list.append(full)
    write_if_changed(os.path.join(out, "boot_cuts.txt"), "".join(r + "\n" for r in cuts))
    write_if_changed(os.path.join(out, "boot_thunks.txt"), "")
    write_if_changed(os.path.join(out, "boot_matched.txt"), "".join(p + "\n" for p in compile_list))
    write_if_changed(os.path.join(out, "boot_asm.txt"), "".join(r + "\n" for r in asm_rows))
    write_if_changed(os.path.join(out, "boot_asm_subst.txt"), "".join(p + "\n" for p in sorted(subst)))
    lines = [
        "closure from %d seeds: %d src/game translation units" % (len(seeds), len(sources)),
        "  functions cut because the port defines them: %d (boot_cuts.txt)" % ncut,
        "  mwcc asm bodies cut: %d (%d with an sdk/asm replacement)"
        % (len(asm_rows), sum(1 for r in asm_rows if " replaced-by " in r or " in-library " in r)),
        "  compiled for the boot: %d" % len(compile_list),
        "  seeds the tree does not define, left out: %d" % len(unlisted),
    ]
    write_if_changed(os.path.join(out, "boot_plan.txt"), "\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


# ---------------------------------------------------------------------------
# generate
# ---------------------------------------------------------------------------

def return_type(head, name):
    """The return type for a non-prototype declaration of `name`.

    mwcc compiled each such call as C89's implicit `int name()`, and the
    callers use the result that way (some store a void function's "result",
    some put a pointer in an int).  So the declaration is `int name()`, the
    one the DS code was compiled against, unless the definition returns a
    64-bit value, which does not travel in one int-sized register on any
    host.
    """
    before = head.split("(")[0]
    before = before[:before.rfind(name)] if name in before else before
    words = [w for w in re.findall(r"[A-Za-z_]\w*", before)
             if w not in ("static", "inline", "extern", "const", "volatile")]
    if "*" not in before and (any(w in ("u64", "s64", "fx64", "OSTick") for w in words)
                              or " ".join(words).count("long long")):
        return "long long"
    return "int"


# mwcc builtins a few matched files call with no declaration, and the port
# function each becomes (sdk/overlay_image.c; tools/ovlink.py does the same
# for the overlays).
MWCC_BUILTINS = {
    "__builtin__clear": ("bw_mwcc_clear", "void bw_mwcc_clear(void *dst, unsigned int size);"),
    "__memcpy": ("bw_mwcc_memcpy",
                 "void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size);"),
}

# Standard C library functions provided by the platform libc (-lm).
# In CodeWarrior runtime.a, routines like sqrt were hand-written asm bodies
# in cw_runtime.c. On modern host/WASI targets, libc provides them.
LIBC_FUNCS = {"sqrt"}


PARAM_LIST = re.compile(r"\(([^()]*)\)")
STRUCT_TAG = re.compile(r"\bstruct\s+([A-Za-z_]\w*)")


def param_struct_tags(path):
    """Struct tags that appear inside a parenthesised list before any
    declaration of the tag at file scope."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            code = closure.strip_source(fh.read())
    except OSError:
        return set()
    out = set()
    for m in PARAM_LIST.finditer(code):
        for tag in STRUCT_TAG.findall(m.group(1)):
            first = re.search(r"\bstruct\s+" + tag + r"\b", code)
            if first is not None and first.start() >= m.start():
                out.add(tag)
    return out


class Unit:
    """One translation unit of the boot and what its header must say."""

    def __init__(self, path, rel, scan):
        self.path = path
        self.rel = rel              # src/... for a matched file, else None
        self.scan = scan
        self.renames = {}           # spelling -> defined name
        self.consts = set()
        self.protos = {}            # name -> return type
        self.private = {}           # name -> this file's own copy's name


# The declared hookable set: mod/declared_set.txt (docs/mod-interface.md,
# section 1).  The arm9 rows get a slot here; the overlay rows are
# tools/ovlink.py's.
DECLARED_SET = os.path.join(closure.EIDOS_ROOT, "mod", "declared_set.txt")


def read_declared_set(region=None):
    """Rows of mod/declared_set.txt: (address, name, region, events), the rows
    of `region` only when it is given.  `#` starts a comment; a line is tab
    separated.  The fourth column names the stable events the port binds to
    the function (docs/mod-events.md, section 1); it is empty for a
    raw-hook-only row, and it may name two events (catch.attempt,
    catch.result)."""
    rows = []
    if not os.path.exists(DECLARED_SET):
        return rows
    with open(DECLARED_SET, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0]
            if not line.strip():
                continue
            parts = [p.strip() for p in line.rstrip("\n").split("\t")]
            if len(parts) < 3 or not parts[0].startswith("0x"):
                continue
            if region is not None and parts[2] != region:
                continue
            events = []
            if len(parts) > 3:
                events = [e.strip() for e in parts[3].split(",") if e.strip()]
            rows.append((int(parts[0], 16), parts[1], parts[2], events))
    return rows


# The regions of the declared set (sdk/mod_slot.h): the ARM9's static
# module, whose value is the largest, so a table sorted by region keeps
# the overlay rows first, and an overlay's own id.
ARM9_REGION = 0xFFFFFFFF


def region_expr(region):
    """(the C expression for a declared set's region token, the region's
    value) for a token the port knows, (None, None) for one it does not.
    `arm9` is the ARM9's static module, `ovNNN` the overlay with that id
    (sdk/mod_slot.h)."""
    if region == "arm9":
        return "BW_MOD_REGION_ARM9", ARM9_REGION
    m = re.match(r"^ov0*(\d+)$", region)
    if m is None:
        return None, None
    oid = int(m.group(1))
    return "BW_MOD_REGION_OVL(%d)" % oid, oid


def definition_signature(path, name):
    """(return type, [param types]) of `name` as the file at `path` defines
    it, normalised by tools/ovlink.py's signature (the call write_thunk
    makes), or None when it does not normalise."""
    if not path or not os.path.exists(path):
        return None
    sc = ovlink.Scan(path, ovlink.read_text(path))
    head = sc.heads.get(name)
    if head is None:
        return None
    return ovlink.signature(head, name, sc.typedefs)


def slot_function(region_expr, addr, slot):
    """The generated slot for one declared address: the indirection at the
    call site (docs/mod-interface.md, section 2) and, for a row that carries
    a stable event, the event's enter and leave around the call
    (docs/mod-implementation.md, section 1, "At the slot").  Its default is
    the original, so with no mod installed it behaves as the direct call did.
    The shape is tools/ovlink.py's, which the overlays use too."""
    return ovlink.slot_function(region_expr, addr, slot)


def generate(args):
    idx = closure.Index()
    load_member_addresses(idx)
    image = read_image(getattr(args, "arm9_image", None))
    pdir = args.plan_dir
    matched = read_list(os.path.join(pdir, "boot_matched.txt"))
    port = read_list(args.port_sources) + read_list(os.path.join(pdir, "boot_asm_subst.txt"))
    asm_rows = [r.split() for r in read_list(os.path.join(pdir, "boot_asm.txt"))]
    data_bound, data_objects, data_lines = read_armdata_header(args.armdata_header)

    spellings = {}
    if getattr(args, "gamespell", None) and os.path.exists(args.gamespell):
        with open(args.gamespell, encoding="utf-8") as fh:
            for m in re.finditer(r"^#define\s+(\w+)\s+(\w+)\s*$", fh.read(), re.M):
                spellings[m.group(1)] = m.group(2)

    def rename_to(unit, name, target):
        """Rename `name` to `target` in the unit's generated header.

        The gamespell header (forced in before the unit's own text) already
        renames the port's old spelling to the tree's name.  Where it maps
        `target` back to `name`, the two would cancel: the port's own file
        would call the port's old name, while the port's copy of the game's
        function carries the tree's name (the gamespell header renamed its
        definition too), and nothing would define the name the call
        reaches.  Such a rename is left out: the two names are one symbol
        already.
        """
        if spellings.get(target) == name:
            return
        unit.renames[name] = target

    units = []
    game_cuts = {}
    if closure.SOURCE_TREE == "game":
        # A src/game unit compiles as its cut copy (plan_game, dsaddr.py
        # --cut-list --cut-asm): read it that way, or every function the
        # port defines would look defined twice.
        for row in read_list(os.path.join(pdir, "boot_cuts.txt")):
            p, _, names = row.partition("\t")
            game_cuts[os.path.normcase(os.path.abspath(p))] = set(n for n in names.split(",") if n)
    for path in matched:
        rel = rel_to_root(path)
        if rel is None and _is_moved_unit(args, path):
            # A function the plan moved out of the unit that called it
            # (move_declared_calls): it is a unit of the link like any
            # other, and it carries the name the row declares.
            rel = "gen/" + os.path.basename(path)
        if closure.SOURCE_TREE == "game" and rel is not None:
            import gamecut
            with open(path, encoding="latin-1") as fh:
                text = fh.read()
            drop = game_cuts.get(os.path.normcase(os.path.abspath(path)), set())
            idx._scans[rel] = closure.SourceScan(gamecut.cut(text, drop, True)[0])
        units.append(Unit(path, rel, idx.scan(rel) if rel is not None
                          else scan_file(path)))
    for path in port:
        units.append(Unit(path, None, scan_file(path)))

    # What is defined, and where.
    shims = Shims(idx, read_list(args.shim_sources)
                  + read_list(os.path.join(pdir, "library_matched.txt")))
    defined = dict(shims.defines)
    for name in LIBC_FUNCS:
        defined[name] = "<libc>"
    # The data generator's host objects are definitions like any other.
    for name in data_objects:
        defined.setdefault(name, args.armdata_header)
    # Two library files defining one name is the library's own business
    # (often an #if this scan does not evaluate); only a boot file against
    # anything is a duplicate this link would hit.
    #
    # Two boot files defining one name: a matched file that carries, besides
    # the function its manifest entry claims, a neighbour another file
    # claims too (a unit kept next to the single-function files split out of
    # it; the overlays' ARM9 callees bring such pairs in).  mwcc's link took
    # the claimed one.  The claiming file owns the name; the other's copy is
    # renamed to a name of its own in that file's header, and its own calls
    # keep using it (tools/ovlink.py does the same inside an overlay).
    unit_at = {u.path: u for u in units}

    def claims(u, name):
        entry = idx.sources.get(u.rel) if u.rel else None
        return bool(entry) and name in (entry.get("functions") or [entry.get("name")])

    dups = []
    for unit in units:
        for name in sorted(unit.scan.defined - unit.scan.statics):
            if name in defined and defined[name] != unit.path:
                other = unit_at.get(defined[name])
                if other is not None and other.rel and unit.rel:
                    owner, copy = ((unit, other) if claims(unit, name) and not claims(other, name)
                                   else (other, unit))
                    copy.private[name] = "%s__%s" % (
                        name, re.sub(r"[^A-Za-z0-9_]", "_", os.path.basename(copy.rel)[:-2]))
                    defined[name] = owner.path
                    continue
                dups.append((name, defined[name], unit.path))
                continue
            defined[name] = unit.path

    # Defined functions by address (func_/sub_ spellings and SDK names), and
    # defined data by address (g_XXXXXXXX spellings and BW_REG rows).
    # A tail-call thunk (boot/thunk/) defines its matched source's function
    # at that source's address: sub_0202C0C8 in func_0202B7E0.c is
    # ISS_SWITCH_SYS_ZoneChange, which the thunk for func_0202C0C8.c defines.
    thunk_rel = {}
    for row in read_list(os.path.join(pdir, "boot_thunks.txt")):
        parts = row.split(None, 2)
        if len(parts) == 3 and parts[1] == "thunk":
            thunk_rel[os.path.normcase(os.path.abspath(parts[2]))] = parts[0]
    func_at, data_at = {}, dict(shims.data_at)
    for name in sorted(defined):
        rel = rel_to_root(defined[name])             or thunk_rel.get(os.path.normcase(os.path.abspath(defined[name])))
        addr = defined_addr(idx, rel, name)
        if addr is not None:
            func_at.setdefault(addr, name)
        m = DATA_ADDR_NAME.match(name)
        if m and not closure.ADDR_NAME.match(name):
            data_at.setdefault(int(m.group(1), 16), name)

    # The declared ARM9 rows and the slot each one gets (docs/mod-interface.md,
    # section 2).  The definition's prototype comes from the defining unit and
    # is normalised by tools/ovlink.py's signature.  A row whose definition the
    # link does not carry is reported and skipped, like an unbound data name.
    arm9_slots = {}            # address -> slot dict
    slot_skipped = []          # (declared name, address, why)
    slot_notes = []            # (declared name, address, note)
    for addr, dname, _region, events in read_declared_set("arm9"):
        oname = func_at.get(addr)
        if oname is None and dname in defined:
            # A unit's member whose spelling is not func_/sub_ and not in the
            # SDK table (BEACON_SetFlag in unit_0200C0E8.c): the declared row
            # carries the address, and the unit defines the name.
            oname = dname
        if oname is None:
            slot_skipped.append((dname, addr, "nothing in the link defines it"))
            continue
        sig = definition_signature(defined.get(oname), oname)
        if sig is None:
            slot_skipped.append((dname, addr,
                                 "%s: its prototype does not normalise" % oname))
            continue
        if events and len(sig[1]) > ovlink.BW_EVENT_ARGS_MAX:
            # The event's call carries the arguments as words, and there are
            # only so many: the row is reported, and the slot keeps the plain
            # indirection.
            slot_notes.append((dname, addr,
                               "%d arguments, more than the event layer's %d: "
                               "the event does not fire here"
                               % (len(sig[1]), ovlink.BW_EVENT_ARGS_MAX)))
            events = []
        arm9_slots[addr] = {
            "name": "bw_slot_arm9_%08X" % addr,
            "declared": dname,
            "original": oname,
            "sig": sig,
            "events": events,
        }

    # The declared set, the whole of it (docs/mod-state.md, section 4): the
    # arm9 rows and the overlay rows together.  The table answers the
    # manifest's check for every region, so it does not depend on what this
    # link carries, nor on what has loaded: a row the link does not define
    # has no slot, and an overlay's slot does not exist until that overlay
    # loads, but the address is declared either way.
    declared = []              # (region value, region expression, address)
    declared_unknown = []      # (name, address, region token)
    for addr, dname, region, events in read_declared_set():
        expr, value = region_expr(region)
        if expr is None:
            declared_unknown.append((dname, addr, region))
            continue
        declared.append((value, expr, addr))
    declared.sort(key=lambda row: (row[0], row[2]))

    # The rows' own names and their prototypes, as the registry keeps them
    # (docs/mod-overrides.md, "The prototype is checked").  Every row's name
    # is published: it is the switch a hook on the row answers by when the
    # mod's manifest states none.  The arm9 rows' prototypes are the ones
    # this link carries; the overlays' rows publish their own, from
    # tools/ovlink.py, when the overlay is compiled in.
    proto_rows = []            # (region expression, address, name, prototype)
    for addr, dname, region, events in read_declared_set():
        expr, value = region_expr(region)
        if expr is None:
            continue
        sig = arm9_slots.get(addr, {}).get("sig") if region == "arm9" else None
        proto_rows.append((expr, addr, dname,
                           ovlink.proto_text(sig) if sig is not None else None))

    # Every data spelling the boot uses, by address, so one object stands
    # for one DS address.
    data_names = {}            # address -> set of spellings
    data_noaddr = set()
    missing_funcs = {}         # name -> (address or None, why)
    consts = {}                # name -> value

    # The functions the asm-bodied files define, by name and by address:
    # a stub takes the defining file's own spelling, so the sdk/asm/
    # replacement (same file name, same names) drops in without a rename.
    asm_pending, asm_at = {}, {}
    for row in asm_rows:
        if len(row) >= 3 and row[1] == "pending":
            # src/game (plan_game): one row per cut asm body, by name.
            asm_pending[row[2]] = row[0]
            addr = defined_addr(idx, row[0], row[2])
            if addr is not None:
                asm_at.setdefault(addr, row[2])
        elif len(row) >= 2 and row[1] == "pending":
            sc = idx.scan(row[0])
            for name in sorted(sc.defined - sc.statics):
                asm_pending[name] = row[0]
                addr = defined_addr(idx, row[0], name)
                if addr is not None:
                    asm_at.setdefault(addr, name)
    missing_refs = []          # (unit, name, address)

    def refs_of(unit):
        if unit.rel:
            names = unit.scan.references(idx.is_global_name)[0]
            out = []
            for name in names:
                addr, kind = idx.address_of(unit.rel, name)
                klass, _detail = idx.classify(unit.rel, name)
                if klass == "local" and closure.SOURCE_TREE == "game" \
                        and name not in unit.scan.defined:
                    # src/game: the unit's header lists the name but its copy
                    # has it cut (an asm body, or a function the port
                    # defines): the definition is elsewhere or a stub.
                    klass = "matched"
                if klass == "local":
                    # The file calls itself by another name (an SDK
                    # function's recursion through its library name).
                    own = None
                    if addr is not None and name not in unit.scan.defined:
                        own = idx.owner_name(idx.sources[unit.rel], addr)
                    if own and own != name:
                        rename_to(unit, name, own)
                    continue
                out.append((name, addr, kind, klass))
            return out
        out = []
        for name in unit.scan.references(idx.is_global_name)[0]:
            # The port's old spelling of a game function compiles as the
            # src/game name (tools/gamespell.py's header): no stub for it.
            spelled = name
            name = spellings.get(name, name)
            addr = idx._name_addr(name)
            if addr is None and spelled != name:
                addr = idx._name_addr(spelled)
            out.append((name, addr, None, "data" if addr is None and
                        spelled not in unit.scan.prototyped else "func"))
        return out

    # A name's DS address as any boot file's `symbols:` block records it,
    # for the files whose own header does not list it.
    known_addr = {}
    for unit in units:
        for name, (addr, _kind) in unit.scan.symbols.items():
            known_addr.setdefault(name, addr)

    pending_data = []          # (unit, name, address)
    for unit in units:
        for name, addr, kind, klass in refs_of(unit):
            if name in data_bound:
                # Bound to its DS address by the data header (a matched
                # file's own declaration, read through a pointer variable).
                continue
            if addr is None and name in known_addr:
                addr = known_addr[name]
            if addr is not None and addr in arm9_slots \
                    and name in unit.scan.called \
                    and defined.get(arm9_slots[addr]["original"]) != unit.path:
                # A declared ARM9 address: the caller goes through the slot.
                # The defining unit keeps its own name and gets no rename.
                rename_to(unit, name, arm9_slots[addr]["name"])
                continue
            if closure.is_link_constant(name, addr, kind):
                if addr is None:
                    raise SystemExit("bootlink.py: link constant %s has no value in %s"
                                     % (name, unit.path))
                consts[name] = addr
                unit.consts.add(name)
                continue
            if name in defined:
                # A spelling another file defines at another address: the
                # SDK's name reused for a different function (sub_0204968C.c
                # calls 0x020307B0 "ENV_SetResourceSet"; the SDK's is at
                # 0x0206434C).  The file's own symbols row says which one it
                # calls, and mwcc's link went by that address.
                if kind in ("arm", "thumb") and addr is not None and addr in func_at \
                        and func_at[addr] != name:
                    daddr = defined_addr(idx, rel_to_root(defined[name]), name)
                    if daddr is not None and daddr != addr:
                        rename_to(unit, name, func_at[addr])
                continue
            is_data = kind == "data" or klass == "data"
            if not is_data and addr is not None and addr in func_at:
                rename_to(unit, name, func_at[addr])
                continue
            if not is_data and addr in VENEERS and VENEERS[addr] in func_at:
                # A linker veneer is another address of its target.
                rename_to(unit, name, func_at[VENEERS[addr]])
                continue
            if not is_data and klass != "data":
                if addr is not None and DTCM_LO <= addr < DTCM_HI:
                    is_data = True
                else:
                    missing_refs.append((unit, name, addr))
                    continue
            if addr is None:
                m = DATA_ADDR_NAME.match(name)
                addr = int(m.group(1), 16) if m else None
            pending_data.append((unit, name, addr))
            if addr is None:
                data_noaddr.add(name)
            else:
                data_names.setdefault(addr, set()).add(name)

    # One stub per missing function address, under the asm file's spelling
    # when there is one, else the first spelling in sort order.
    spellings = {}
    for _unit, name, addr in missing_refs:
        spellings.setdefault(addr if addr is not None else name, set()).add(name)
    stub_name = {}
    veneer_funcs = {}          # name -> (target name, target signature)
    for key, names in spellings.items():
        if not isinstance(key, str) and key in asm_at:
            stub_name[key] = asm_at[key]
        else:
            stub_name[key] = sorted(names)[0]
        name = stub_name[key]
        addr = None if isinstance(key, str) else key
        if name in defined:
            # The chosen stub name is already a definition in this link (a
            # compiled unit or a shim): the asm file's spelling is that
            # definition under another name, so rename the reference onto it
            # and emit no stub.  MYSTATUS_NameToStrbuf (0x02008500) is the
            # case: the asm file names it and unit_020084BC.c defines it as C.
            continue
        if addr is not None:
            target = veneer_target(image, addr)
            tname = func_at.get(target) if target is not None else None
            sig = definition_signature(defined.get(tname), tname) if tname else None
            if sig is not None:
                # A linker veneer the listing would have labelled: the image
                # says it is another address of tname.  Define it as a
                # forwarder to tname (the overlays reach it by this spelling
                # and ovlink leaves it to this link; tools/bootlink.py plan
                # put tname's source in the closure).
                veneer_funcs[name] = (tname, sig)
                continue
        if name in asm_pending:
            why = "asm body, %s" % asm_pending[name]
        elif addr is not None and overlays_at(idx, addr):
            why = "open, overlay code (%s) called from the static module"                 % overlays_at(idx, addr)
        else:
            why = "open"
        missing_funcs[name] = (addr, why)
    for unit, name, addr in missing_refs:
        target = stub_name[addr if addr is not None else name]
        if target != name:
            rename_to(unit, name, target)

    views = {}                 # second spelling in one file -> canonical
    # One spelling per DS address: the one a shim or data source defines,
    # else the g_XXXXXXXX spelling, else the first in sort order.  DTCM
    # objects nobody defines are pinned to the DS bytes, like a constant.
    canonical = {}
    pinned = {}
    for addr, names in sorted(data_names.items()):
        if addr in data_at:
            canonical[addr] = data_at[addr]
            continue
        spelled = sorted(n for n in names if n == "g_%08X" % addr)
        canonical[addr] = spelled[0] if spelled else sorted(names)[0]
        if DTCM_LO <= addr < DTCM_HI:
            pinned[canonical[addr]] = addr
    for unit, name, addr in pending_data:
        if addr is None:
            continue
        target = canonical[addr]
        if target != name and (target in unit.scan.declared or target in unit.scan.defined):
            # The file names this address twice, with two types (a struct and
            # a word view of it): a rename would make one declaration
            # conflict with the other, so the second spelling reads through
            # a pointer to the first.
            unit.consts.add(name)
            views[name] = target
            continue
        if target in pinned:
            unit.consts.add(target)
            if target != name:
                rename_to(unit, name, target)
        elif target != name:
            rename_to(unit, name, target)

    # Implicit calls: a non-prototype declaration of what the call reaches.
    heads = {}
    for unit in units:
        heads.update({n: h for n, h in unit.scan.heads.items() if n not in unit.scan.statics})
    for unit in units:
        if not unit.rel:
            continue
        # A name the file's own `#include` header declares must not get a
        # generated non-prototype declaration: the header's declaration,
        # parsed after the forced include, would conflict with it.  The
        # generated header cannot see the file's includes; this scan can.
        inc_decls = closure.included_declarations(unit.path)
        for name in idx.implicit_calls(unit.rel):
            if name in inc_decls:
                continue
            target = unit.renames.get(name, name)
            if target in inc_decls:
                continue
            if target in unit.scan.declared or target in unit.scan.defined:
                continue
            head = heads.get(target)
            if head is None and target in defined and os.path.exists(defined[target]):
                head = scan_file(defined[target]).heads.get(target)
            unit.protos[target] = return_type(head, target) if head else "int"

    # Write the headers.
    out = args.out_dir
    incdir = os.path.join(out, "include")
    for unit in units:
        lines = ["/* generated by tools/bootlink.py for %s; do not edit */"
                 % (unit.rel or os.path.basename(unit.path))]
        # The data header's lines for the names this unit names and does
        # not define as a macro itself (a file that says `#define tpState
        # g_02152FA4` binds through the other name).
        idents, macros = unit_identifiers(unit.path)
        for name in sorted(data_lines):
            if name in idents and name not in macros:
                lines.append(data_lines[name])
        for name in sorted(unit.renames):
            lines.append("#define %s %s" % (name, unit.renames[name]))
        for name in sorted(closure.HOST_RESERVED):
            if name in idents and name not in unit.renames:
                lines.append("#define %s %s   /* the host C runtime's own name */"
                             % (name, closure.HOST_RESERVED[name]))
        for name in sorted(unit.private):
            lines.append("#define %s %s   /* another boot file owns %s */"
                         % (name, unit.private[name], name))
        # A struct tag a prototype names only inside its parameter list is
        # scoped to that list (GCC warns, with no option to say otherwise);
        # mwcc took it as the file's tag.  A forward declaration first makes
        # it one.
        for tag in sorted(param_struct_tags(unit.path)):
            lines.append("struct %s;" % tag)
        for name in sorted(MWCC_BUILTINS):
            if name in unit.scan.called and name not in unit.scan.declared \
                    and name not in unit.scan.defined:
                port, decl = MWCC_BUILTINS[name]
                lines.append(decl)
                lines.append("#define %s %s" % (name, port))
        for name in sorted(unit.consts):
            lines.append("#define %s (*bw_lc_%s)" % (name, name))
        for name in sorted(unit.protos):
            rtype = unit.protos[name]
            sep = "" if rtype.endswith("*") else " "
            lines.append("%s%s%s();" % (rtype, sep, name))
        write_if_changed(os.path.join(incdir, header_name(unit.path)), "\n".join(lines) + "\n")

    write_link_c(os.path.join(out, "boot_link.c"), consts, pinned, views, missing_funcs,
                 arm9_slots, veneer_funcs, declared, proto_rows)
    # Data the data header does not bind and nothing defines has no
    # storage: the link would fail on it, so the generator does, by name.
    unbound = sorted((canonical[a], a) for a in data_names
                     if canonical[a] not in defined and canonical[a] not in pinned)
    unbound += sorted((n, None) for n in data_noaddr if n not in defined)

    report = ["translation units: %d matched, %d port" % (len(matched), len(port)),
              "renamed spellings: %d" % sum(len(u.renames) for u in units),
              "implicit declarations supplied: %d in %d files"
              % (sum(len(u.protos) for u in units), sum(1 for u in units if u.protos)),
              "link constants: %d (%d pinned DTCM objects)" % (len(consts), len(pinned)),
              "data names bound by the data header: %d" % len(data_bound),
              "data objects with no storage: %d" % len(unbound),
              "functions stubbed: %d open, %d asm-pending"
              % (sum(1 for v in missing_funcs.values() if v[1].startswith("open")),
                 sum(1 for v in missing_funcs.values() if not v[1].startswith("open"))),
              "linker veneers: %d (forwarders to their image target)" % len(veneer_funcs),
              "mod slots: %d arm9 emitted (%d with an event), %d declared rows skipped"
              % (len(arm9_slots),
                 sum(1 for s in arm9_slots.values() if s["events"]),
                 len(slot_skipped)),
              "declared set: %d rows in the table (%d arm9, %d overlay)"
              % (len(declared),
                 sum(1 for row in declared if row[0] == ARM9_REGION),
                 sum(1 for row in declared if row[0] != ARM9_REGION))]
    for name, a, b in dups:
        report.append("DUPLICATE %s: %s and %s" % (name, a, b))
    report.append("")
    report.append("mod slots (a generated indirection per declared ARM9 address):")
    for addr in sorted(arm9_slots):
        report.append("  %-36s 0x%08X -> %s%s" % (
            arm9_slots[addr]["declared"], addr, arm9_slots[addr]["original"],
            (", events " + ",".join(arm9_slots[addr]["events"]))
            if arm9_slots[addr]["events"] else ""))
    for dname, addr, why in slot_skipped:
        report.append("  SKIPPED %-28s 0x%08X %s" % (dname, addr, why))
    for dname, addr, note in slot_notes:
        report.append("  NOTE %-30s 0x%08X %s" % (dname, addr, note))
    if declared_unknown:
        report.append("")
        report.append("declared rows with a region the port does not know:")
        for dname, addr, region in declared_unknown:
            report.append("  %-36s 0x%08X %s" % (dname, addr, region))
    report.append("")
    report.append("implicit declarations:")
    for unit in units:
        for name in sorted(unit.protos):
            report.append("  %s: %s %s()" % (unit.rel or unit.path, unit.protos[name], name))
    report.append("")
    report.append("linker veneers (the image's `ldr ip, [pc]; bx ip` island, forwarded to its target):")
    for name in sorted(veneer_funcs):
        report.append("  %-36s -> %s" % (name, veneer_funcs[name][0]))
    report.append("")
    report.append("functions stubbed:")
    for name in sorted(missing_funcs):
        addr, why = missing_funcs[name]
        report.append("  %-36s %-10s %s" % (name, "0x%08X" % addr if addr is not None else "-", why))
    report.append("")
    report.append("data objects with no storage (the data header binds no such name):")
    for name, addr in unbound:
        report.append("  %-36s %s" % (name, "0x%08X" % addr if addr is not None else "-"))
    write_if_changed(os.path.join(out, "boot_link_report.txt"), "\n".join(report) + "\n")
    print("\n".join(report[:8]))
    for name, addr in unbound:
        print("bootlink.py: no storage for data %s (%s)"
              % (name, "0x%08X" % addr if addr is not None else "no address"), file=sys.stderr)
    for dname, addr, why in slot_skipped:
        print("bootlink.py: no slot for %s (0x%08X): %s" % (dname, addr, why),
              file=sys.stderr)
    for dname, addr, note in slot_notes:
        print("bootlink.py: %s (0x%08X): %s" % (dname, addr, note),
              file=sys.stderr)
    for dname, addr, region in declared_unknown:
        print("bootlink.py: %s (0x%08X): unknown region %s: the row is not "
              "declared" % (dname, addr, region), file=sys.stderr)
    return 1 if dups or unbound else 0


ARMDATA_REF = re.compile(r"^#define\s+([A-Za-z_]\w*)\s+\(\*bw_dsref_\1\)", re.M)
ARMDATA_OBJ = re.compile(r"^/\*\s+([A-Za-z_]\w*)\s+is a host object", re.M)


def read_armdata_header(path):
    """The names tools/armdata.py's header binds, its host objects, and the
    header's #define line for each bound name."""
    if not path:
        return set(), set(), {}
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    objects = set(ARMDATA_OBJ.findall(text))
    lines = {}
    for line in text.splitlines():
        m = ARMDATA_REF.match(line)
        if m:
            lines[m.group(1)] = line
    return set(lines) | objects, objects, lines


IDENT = re.compile(r"[A-Za-z_]\w*")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)", re.M)


def unit_identifiers(path):
    """Every identifier in a source, and the names it #defines."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    return set(IDENT.findall(text)), set(DEFINE.findall(text))


LINK_C_HEAD = """\
/*
 * boot_link.c -- generated by tools/bootlink.py; do not edit.
 *
 * Link-time constants and the functions nothing defines yet, for the
 * game's own boot (docs/boot.md, "Linking the boot").
 *
 * bw_lc_NAME holds the value the DS linker gives NAME's *address*; each
 * translation unit that names NAME reads it as (*bw_lc_NAME) through its
 * generated header.  The matched files declare NAME with several types
 * (char[], u32[], a function); the object is a pointer in every one of
 * them, so its size and representation agree on every target.
 *
 * A value inside the DS address space is a host pointer, set by
 * bw_boot_link_init after bw_ds_reset: SDK_AUTOLOAD_DTCM_START is
 * bw_ds_ptr(0x02FE0000), the same bytes sdk/os_interrupt.c reaches as
 * BW_DS_INTR_CHECK_BUF - 0x3FF8.
 *
 * Every function below the constants is a STUB: it panics with its DS
 * address when called (with keep-going on, it reports once and returns;
 * sdk/port.h, bw_stub_reached).  "open" is game code aletheia has not matched;
 * "asm body" is an SDK file written as mwcc assembly, which sdk/asm/
 * replaces (bootlink.py plan picks a replacement up by file name).
 */

#include <stdint.h>

#include "dsmem.h"
#include "port.h"
#include "mod_slot.h"
#include "mod_hooks.h"
#include "mod_event.h"

void bw_boot_link_init(void);
"""


def veneer_forwarder(name, target, sig):
    """A forwarder for the linker veneer `name`, calling `target` (the
    image's target word) with the arguments the DS left in r0-r3 and
    returning what it returns.  `sig` is the target's normalised
    (return type, [param types]), from tools/ovlink.py's signature."""
    ret, params = sig
    tags = sorted({t for p in params
                   for t in re.findall(r"\bstruct\s+([A-Za-z_]\w*)", p)})
    args = ", ".join("a%d" % k for k in range(len(params)))
    out = ["struct %s;" % tag for tag in tags]
    out.append(ovlink.proto_decl(ret, target, params))
    out.append(ovlink.proto(ret, name, params))
    out.append("{")
    out.append("    %s%s(%s);" % ("" if ret == "void" else "return ", target, args))
    out.append("}")
    out.append("")
    return "\n".join(out)


def write_link_c(path, consts, pinned, views, missing_funcs, slots=None,
                 veneers=None, declared=None, protos=None):
    slots = slots or {}
    veneers = veneers or {}
    declared = declared or []
    protos = protos or []
    lines = [LINK_C_HEAD]
    lines.append("/* ---- link-time constants ---- */\n")
    runtime = []               # (name, initial value), set at start-up
    for name in sorted(set(consts) | set(pinned)):
        value = consts.get(name, pinned.get(name))
        if DS_LO <= value < DS_HI:
            lines.append("void *bw_lc_%s;   /* DS 0x%08X */" % (name, value))
            runtime.append((name, "bw_ds_ptr(0x%08Xu)" % value))
        else:
            lines.append("void *bw_lc_%s = (void *)(uintptr_t)0x%08Xu;" % (name, value))
    lines.append("")
    lines.append("/* ---- second views: one file, two spellings of one object ---- */\n")
    for name in sorted(views):
        target = views[name]
        if target in pinned:
            lines.append("void *bw_lc_%s;   /* = bw_lc_%s */" % (name, target))
            runtime.append((name, "bw_lc_%s" % target))
        else:
            lines.append("extern char %s[];" % target)
            lines.append("void *bw_lc_%s = (void *)%s;" % (name, target))
    lines.append("")
    if slots:
        lines.append("/* ---- mod slots: the originals the slots call ---- */\n")
        for addr in sorted(slots):
            ret, params = slots[addr]["sig"]
            lines.append(ovlink.proto_decl(ret, slots[addr]["original"], params))
        lines.append("")
    if declared:
        lines.append("/* ---- the declared set: every row of mod/declared_set.txt ---- */\n")
        lines.append("/* The manifest's check asks this table whether a mod's override")
        lines.append(" * names a declared address (docs/mod-state.md, section 4).  It")
        lines.append(" * carries the whole set, the arm9 rows and the overlay rows")
        lines.append(" * together: a row this link does not carry has no slot, and an")
        lines.append(" * overlay's slot does not exist until that overlay loads, but the")
        lines.append(" * address is declared either way. */")
        lines.append("static const BwModDeclared bw_declared_rows[] = {")
        for _value, expr, addr in declared:
            lines.append("    { %s, 0x%08Xu }," % (expr, addr))
        lines.append("};")
        lines.append("")
    if protos:
        lines.append("/* ---- the declared rows: their names and their prototypes ---- */\n")
        lines.append("/* The name is the switch a hook on the row answers by when the")
        lines.append(" * mod's manifest states none; the prototype is the function's own,")
        lines.append(" * normalised the way tools/ovlink.py's signature normalises it,")
        lines.append(" * and it is what the manifest's `sig=` is compared with")
        lines.append(" * (docs/mod-overrides.md, \"The prototype is checked\").  Every")
        lines.append(" * row's name is here; the arm9 rows' prototypes are the ones this")
        lines.append(" * link carries, and the overlays' rows publish their own, from")
        lines.append(" * tools/ovlink.py. */")
        lines.append("static const BwModSlotProto bw_proto_rows[] = {")
        for expr, addr, dname, proto in protos:
            lines.append("    { %s, 0x%08Xu, \"%s\", %s }," % (
                expr, addr, dname,
                "\"%s\"" % proto if proto is not None else "NULL"))
        lines.append("};")
        lines.append("")
    lines.append("void bw_boot_link_init(void)\n{")
    # The declared set first: a mod loads before any overlay does, and the
    # manifest's check asks the set as the mod loads.
    if declared:
        lines.append("    bw_mod_declare_table(bw_declared_rows, %du);" % len(declared))
    # The rows' names and prototypes beside it: the manifest's check asks
    # them as the mod loads too.
    if protos:
        lines.append("    bw_mod_publish_protos(bw_proto_rows, %du);" % len(protos))
    # Addresses first: a second view of a pinned object copies its pointer.
    for name, value in sorted(runtime, key=lambda r: r[1].startswith("bw_lc_")):
        lines.append("    bw_lc_%s = %s;" % (name, value))
    # Publish each declared original so a wrap override can call it.
    for addr in sorted(slots):
        lines.append("    bw_mod_slot_publish(BW_MOD_REGION_ARM9, 0x%08Xu, (void *)%s);"
                     % (addr, slots[addr]["original"]))
    lines.append("}\n")
    if slots:
        lines.append("/* ---- mod slots: one indirection per declared ARM9 address ---- */\n")
        for addr in sorted(slots):
            lines.append(slot_function("BW_MOD_REGION_ARM9", addr, slots[addr]))
    if veneers:
        lines.append("/* ---- linker veneers: an image address of its target ---- */\n")
        for name in sorted(veneers):
            target, sig = veneers[name]
            lines.append(veneer_forwarder(name, target, sig))
    lines.append("/* ---- STUBS: functions nothing defines yet ---- */\n")
    for name in sorted(missing_funcs):
        addr, why = missing_funcs[name]
        lines.append("void %s(void);" % name)
        lines.append("void %s(void)\n{\n    bw_stub_reached(\"boot stub reached (%s)\", 0x%08Xu, \"%s\");\n}\n"
                     % (name, why.replace("\\", "/"), addr or 0, name))
    write_if_changed(path, "\n".join(lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = ap.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--aletheia-root", required=True)
    p.add_argument("--seed", action="append", default=[])
    p.add_argument("--seed-file", action="append", default=[],
                   help="file listing seeds, one per line (repeatable)")
    p.add_argument("--exclude-file", required=True)
    p.add_argument("--shim-sources", action="append", required=True,
                   help="file listing port sources (repeatable)")
    p.add_argument("--library-matched")
    p.add_argument("--asm-dir")
    p.add_argument("--arm9-image",
                   help="the player's extract/arm9_dec.bin, for the linker "
                        "veneers the listing would label (optional)")
    p.add_argument("--port-sources",
                   help="file listing the boot's port sources, whose calls seed the walk")
    p.add_argument("--define", action="append", default=[],
                   help="NAME=VALUE a port #if depends on (repeatable)")
    p.add_argument("--mod-moves", action="store_true",
                   help="move a declared function out of the unit that calls "
                        "it, so the call reaches the slot (docs/mod-interface.md, "
                        "section 2).  Off until the moved unit carries the "
                        "unit's own enums and data references as well.")
    p.add_argument("--out-dir", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--aletheia-root", required=True)
    g.add_argument("--plan-dir", required=True)
    g.add_argument("--shim-sources", required=True)
    g.add_argument("--port-sources", required=True)
    g.add_argument("--gamespell",
                   help="src/game: tools/gamespell.py's header, the port's old "
                        "spellings of game functions")
    g.add_argument("--armdata-header",
                   help="tools/armdata.py's forced-include header: the data names it binds")
    g.add_argument("--arm9-image",
                   help="the player's extract/arm9_dec.bin, for the linker "
                        "veneers the listing would label (optional)")
    g.add_argument("--define", action="append", default=[],
                   help="NAME=VALUE a port #if depends on (repeatable)")
    g.add_argument("--out-dir", required=True)
    args = ap.parse_args(argv)
    closure.ROOT = os.path.abspath(args.aletheia_root)
    for item in getattr(args, "define", []):
        name, _, value = item.partition("=")
        PORT_DEFINES[name] = value
    return plan(args) if args.mode == "plan" else generate(args)


if __name__ == "__main__":
    sys.exit(main())
