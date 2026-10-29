#!/usr/bin/env python3
"""
armdata.py -- the ARM9 data the matched code names, bound to the DS image.

The matched code reaches the game's data (tables, strings, the SDK's state)
through `extern` names pinned to DS addresses by each file's `symbols:`
block, through literal casts that tools/dsaddr.py turns into bw_ds_ptr, and
through pointers stored inside the data itself.  On the DS all three are one
memory: the ARM9 image, loaded at 0x02004000, its autoload blocks copied to
their addresses, the .bss after it.  docs/armdata.md is the design; in short:

  * The image itself is the data.  At every bw_ds_reset the runtime
    (sdk/armdata.c) copies the player's own extract/arm9_dec.bin into the
    port's flat main RAM at the DS addresses it occupies on the console
    (the static module and each autoload block), so a literal cast, a
    pointer read out of a table and an extern name all reach the same
    bytes, laid out exactly as on the DS.
  * Every extern data name a matched source in the build uses, and no port
    source defines, becomes a pointer variable: the forced-include header
    this script writes says `#define NAME (*bw_dsref_NAME)`, which turns the
    matched file's own `extern T NAME;` into `extern T (*bw_dsref_NAME);`
    and every use into a dereference.  The runtime binds each variable to
    bw_ds_ptr(address) at reset.  Several names for one address are several
    variables bound to the same byte, so aliases need no special case.
  * Every word of initialised data that holds a code or data address is
    listed by location and target; the runtime rewrites it to the host
    address (bw_ds_ptr of a data target, the host function for a code
    target) on an ILP32 host, and refuses on an LP64 one.

This script reads only metadata, never game bytes:

  layout/layout.json         the static module, autoload and overlay ranges
  layout/data_objects.json   data objects: address, size, kind, and for the
                             pointer-holding ones which words are pointers
                             and to which address
  layout/matched/**/*.json   which function is at which address
  layout/symbols_sdk.txt     SDK functions by address
  asm/arm9/*.s               only the `-> symbol` annotations of the
  asm/overlays/ovNNN.s       reassembly listings (which word points at which
                             symbol); the words themselves are not read
  the build's sources        `symbols:` blocks, extern declarations and the
                             definitions each file makes

and writes two files into the build tree:

  --out-c   the pointer variables, the name table, the copy ranges, the
            relocation list and the function table the relocations use
  --out-h   the forced-include header for the matched sources

Neither contains a byte of the image: initial contents are copied at run
time out of the player's own file.

    python tools/armdata.py --aletheia-root ../aletheia --sources list.txt \\
        --out-c bw_arm9_data.c --out-h bw_arm9_data.h [--summary]

`--region ovNNN` selects an overlay instead of the ARM9 static module (the
copy ranges and relocations then describe that overlay's image).  Compiled
overlays get their tables from tools/ovlink.py, which reuses this module
(docs/overlays.md).

Standard library only.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gametree  # noqa: E402  (pair_members)
import alcache   # noqa: E402  (parse cache)

_PARSES = alcache.ParseCache("armdata-sourcefile", [
    os.path.abspath(__file__),
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "gamecut.py")])

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXCLUSIONS = os.path.join(EIDOS_ROOT, "tools", "armdata_exclusions.txt")
# The hookable set (docs/mod-interface.md, section 1): the rows a mod may
# override.  The ARM9's rows are the addresses the code entries table
# carries, so a code pointer stored in the data reaches the registry for
# them (sdk/armdata.c, bw_arm9_code_entry).
DECLARED_SET = os.path.join(EIDOS_ROOT, "mod", "declared_set.txt")

# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------

# The leading `*` is optional: src/game/arm9/gx_load2d.c's header lost it.
SYMBOLS_HDR = re.compile(r"^\s*\*?\s*symbols:\s*$")
SYMBOL_ROW = re.compile(
    r"^\s*\*\s+([A-Za-z_]\w*)\s+(0x[0-9A-Fa-f]{1,8})\s+(thumb|arm|data)\b(.*)$")
ALIGN_COL = re.compile(r"^a(\d+)$")
# An address spelled into a name: g_020A6C58, d_0214EA54_hdr, g0209DEA0,
# tbl_0209DEB0, str_0203C238, func_02005440, ov009_func_0215580C.
NAME_ADDR = re.compile(r"(?:^|_|[A-Za-z])(0[0-9A-Fa-f]{7})(?:_|$)")
LINE_DIRECTIVE = re.compile(r'^\s*#\s*line\s+\d+\s+"([^"]+)"', re.M)

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Bool", "__inline",
    "__restrict", "__attribute__", "__declspec", "asm", "__asm",
}

# Addresses below this are not memory: the SDK's linker-defined constants
# (SDK_IRQ_STACKSIZE = 0x800, SDK_OVERLAY_NUMBER = 0xED) are symbols whose
# *address* is the value, and the matched code only ever casts them.
CONST_LIMIT = 0x01000000
MAIN_RAM_BASE = 0x02000000
# The DSi's extended RAM.  This NITRO port backs no region for it, so a name
# there is its address as a value, like the SDK constants below CONST_LIMIT.
TWL_EXT_RAM_BASE = 0x0D000000


def hx(v: int) -> str:
    return "0x%08X" % v


def name_address(name: str):
    """The DS address spelled into a name, or None."""
    m = None
    for m in NAME_ADDR.finditer(name):
        pass
    if m is None:
        return None
    v = int(m.group(1), 16)
    return v if 0x01FF8000 <= v < 0x03000000 else None


def strip_c(text: str) -> str:
    """Comments, string and character literals and preprocessor lines blanked,
    newlines kept so offsets stay meaningful."""
    out = []
    i, n = 0, len(text)
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start:
            j = i
            while j < n and text[j] in " \t":
                j += 1
            if j < n and text[j] == "#":
                # A preprocessor line, with its continuations.
                k = j
                while k < n:
                    if text[k] == "\n" and text[k - 1] != "\\":
                        break
                    k += 1
                out.append(re.sub(r"[^\n]", " ", text[i:k]))
                i = k
                continue
            at_line_start = False
        if c == "\n":
            out.append(c)
            at_line_start = True
            i += 1
        elif text.startswith("/*", i):
            k = text.find("*/", i + 2)
            k = n if k < 0 else k + 2
            out.append(re.sub(r"[^\n]", " ", text[i:k]))
            i = k
        elif text.startswith("//", i):
            k = text.find("\n", i)
            k = n if k < 0 else k
            out.append(" " * (k - i))
            i = k
        elif c in "\"'":
            k = i + 1
            while k < n and text[k] != c:
                if text[k] == "\\":
                    k += 1
                elif text[k] == "\n":
                    break
                k += 1
            k = min(k + 1, n)
            out.append(c + " " * (k - i - 2) + c if k - i >= 2 else " " * (k - i))
            i = k
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_externs(code: str) -> str:
    """`code` with every `extern` declaration blanked, up to its `;` at
    brace depth zero (a struct or union body inside it included)."""
    out = []
    pos = 0
    for m in re.finditer(r"\bextern\b", code):
        if m.start() < pos:
            continue
        out.append(code[pos:m.start()])
        depth = 0
        i = m.end()
        while i < len(code):
            c = code[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            elif c == ";" and depth <= 0:
                break
            i += 1
        out.append(" ")
        pos = i + 1
    out.append(code[pos:])
    return "".join(out)


def split_top(text: str, sep: str):
    """Split on `sep` outside (), [] and {}."""
    parts, depth, cur = [], 0, []
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    return parts


def remove_brackets(text: str, open_: str, close: str) -> str:
    out, depth = [], 0
    for c in text:
        if c == open_:
            depth += 1
            continue
        if c == close:
            depth -= 1
            continue
        if depth == 0:
            out.append(c)
    return "".join(out)


def declarator_name(decl: str):
    """(name, is_function) for one declarator, or (None, False)."""
    decl = decl.split("=", 1)[0]
    decl = remove_brackets(decl, "[", "]")
    decl = remove_brackets(decl, "{", "}")
    # Strip attribute and declspec groups.
    decl = re.sub(r"__attribute__\s*\(\(.*?\)\)", " ", decl)
    decl = re.sub(r"__declspec\s*\(.*?\)", " ", decl)
    # The first parenthesis decides: `(*name)` is a pointer declarator (to a
    # function or to an array); anything else is a parameter list, and the
    # name is in front of it.  Parameter names inside it are never the name.
    m = re.match(r"^[^(]*\(\s*\*+\s*(?:const\s+|volatile\s+)*([A-Za-z_]\w*)\s*[)\[]", decl)
    if m:
        return m.group(1), False
    if "(" in decl:
        head = decl.split("(", 1)[0]
        ids = [i for i in re.findall(r"[A-Za-z_]\w*", head) if i not in C_KEYWORDS]
        return (ids[-1], True) if ids else (None, False)
    ids = [i for i in re.findall(r"[A-Za-z_]\w*", decl) if i not in C_KEYWORDS]
    return (ids[-1], False) if ids else (None, False)


_GAME_ENTRIES = {}


def _game_addresses(path):
    """[(name, address)] of a src/game unit, from tools/gametree.py, or None
    for any other file."""
    p = path.replace("\\", "/")
    if "/src/game/" not in p:
        return None
    root, rel = p.split("/src/game/", 1)
    if root not in _GAME_ENTRIES:
        _GAME_ENTRIES[root] = {e["source"]: list(zip(e["functions"], e["addresses"]))
                               for e in gametree.entries(root)}
    return _GAME_ENTRIES[root].get("src/game/" + rel, [])


class SourceFile:
    """What one C file declares, defines and names."""

    def __init__(self, path: str, text: str, matched: bool):
        import gamecut
        text = gamecut.knr_to_ansi(text)     # old-style definitions, see gamecut
        text = gamecut.host_branches(text)   # the host side of #ifdef __MWERKS__
        self.path = path
        self.matched = matched
        self.symbols = {}         # name -> (addr, kind, region, align)
        self.extern_objects = set()
        self.extern_functions = set()
        self.defined_objects = set()      # non-static, file scope
        self.defined_functions = set()    # non-static
        self.members = set()              # identifiers after . or ->
        self.init_names = set()           # identifiers in file-scope initialisers
        self.shadowed = set()             # declared or used other than as the global
        self.identifiers = set()
        self.header_functions = []        # (name, address) from the header
        self.header_isa = None            # "arm" / "thumb" from the header
        self._header(text)
        self._symbols(text)
        code = strip_c(text)
        self._scan(code)
        # src/game now shares extern object declarations through headers.
        # A referenced data symbol in the unit's address manifest still
        # needs a binding when its declaration is no longer in this file.
        self.extern_objects.update(
            name for name, row in self.symbols.items()
            if row[1] == "data" and name in self.identifiers
            and name not in self.defined_objects
            and name not in self.defined_functions
            and name not in self.extern_functions
            and name not in self.shadowed)
        # A function declared `static` anywhere in the file is internal even
        # when its definition omits the keyword (C keeps the first linkage):
        # src/game/arm9/game_beacon.c's GAME_BEACON_WriteItemUse.
        self.defined_functions -= set(re.findall(
            r"\bstatic\b[^;{}()=]*?\b([A-Za-z_]\w*)\s*\(", code))

    def _header(self, text: str):
        head = text[:4096]
        am = re.search(r"^\s*\*\s*address:\s*(0x[0-9A-Fa-f]{8})", head, re.M)
        im = re.search(r"^\s*\*\s*isa:\s*(arm|thumb)\b", head, re.M)
        if im:
            self.header_isa = im.group(1)
        fm = re.search(r"^\s*\*\s*function:\s*(.+)$", head, re.M)
        if not fm:
            return
        listed = fm.group(1).strip()
        # A unit's list runs on over indented lines while each ends in a
        # comma (" *           sub_020074D8, sub_0200752C").
        rest = head[fm.end():].splitlines()[1:]
        k = 0
        while listed.endswith(",") and k < len(rest):
            cm = re.match(r"^\s*\*\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*\s*,?)\s*$",
                          rest[k])
            if not cm:
                break
            listed += " " + cm.group(1)
            k += 1
        names = [n.strip() for n in listed.split(",") if n.strip()]
        game = _game_addresses(self.path)
        if game is not None:
            # src/game: the list is not in address order, so the header's
            # one address belongs to whichever function starts the unit;
            # every member's address comes from the manifests instead
            # (tools/gametree.py).
            for n, a in game:
                if a is not None:
                    self.header_functions.append((n, a))
            return
        for i, n in enumerate(names):
            a = name_address(n)
            if a is None and i == 0 and am:
                a = int(am.group(1), 16)
            if a is not None:
                self.header_functions.append((n, a))

    def _symbols(self, text: str):
        in_block = False
        for line in text.splitlines():
            if SYMBOLS_HDR.match(line):
                in_block = True
                continue
            if not in_block:
                continue
            m = SYMBOL_ROW.match(line)
            if m:
                region, align = None, None
                for col in m.group(4).split():
                    am = ALIGN_COL.match(col)
                    if am:
                        align = int(am.group(1))
                    elif region is None:
                        region = col
                self.symbols[m.group(1)] = (int(m.group(2), 16), m.group(3),
                                            region, align)
            elif not line.strip().startswith("*") or line.strip().startswith("*/"):
                in_block = False
            elif line.strip() == "*":
                in_block = False

    def _scan(self, code: str):
        self.identifiers = set(re.findall(r"[A-Za-z_]\w*", code))
        self.members = set(re.findall(r"(?:\.|->)\s*([A-Za-z_]\w*)", code))
        self._shadows(code)
        # The statement so far is code[start:i]: a slice when it is needed
        # (a top-level brace or semicolon), not a string rebuilt at every
        # brace, which made a file with large initialisers quadratic.
        depth = 0
        start = 0
        i, n = 0, len(code)
        while i < n:
            c = code[i]
            if c == "{":
                if depth == 0:
                    head = code[start:i]
                    if re.search(r"\)\s*$", head) and "=" not in head:
                        # A function body: record the definition, skip the body.
                        self._function_definition(head)
                        k, d = i, 0
                        while k < n:
                            if code[k] == "{":
                                d += 1
                            elif code[k] == "}":
                                d -= 1
                                if d == 0:
                                    break
                            k += 1
                        i = k + 1
                        start = i
                        continue
                depth += 1
            elif c == "}":
                depth -= 1
            if c == ";" and depth == 0:
                self._declaration(code[start:i])
                start = i + 1
            i += 1

    NOT_TYPES = {"return", "case", "sizeof", "else", "do", "goto", "extern"}

    def _shadows(self, code: str):
        """Every identifier the file uses in a way a `#define NAME (*p)`
        would break: a struct/union/enum tag, a label, a member, a typedef
        name, or the name of a non-extern declaration (a local, a parameter,
        a struct member).  Heuristic and deliberately broad: a name caught
        here by mistake only costs it the pointer binding (it becomes a
        registered host object instead, see Model)."""
        sh = set(re.findall(r"\b(?:struct|union|enum)\s+([A-Za-z_]\w*)", code))
        sh |= set(re.findall(r"\bgoto\s+([A-Za-z_]\w*)", code))
        sh |= set(re.findall(r"(?:^|[;{}])\s*([A-Za-z_]\w*)\s*:(?!:)", code, re.M))
        # An extern declaration, struct body included, declares the global
        # itself: `extern struct { ... } d_02148BCC;` uses the name as the
        # object, which a `#define NAME (*p)` keeps valid.  Only what is
        # left once they are removed can shadow.
        nonext = strip_externs(code)
        sh |= set(re.findall(r"\}\s*\**\s*([A-Za-z_]\w*)\s*[;,\[]", nonext))
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\**\s*\b([A-Za-z_]\w*)\s*(?=[;=\[,)])", nonext):
            if m.group(1) not in self.NOT_TYPES:
                sh.add(m.group(2))
        self.shadowed = sh

    def _function_definition(self, head: str):
        words = head.split()
        if "static" in words:
            return
        name, is_fn = declarator_name(head)
        if name:
            self.defined_functions.add(name)

    def _declaration(self, stmt: str):
        s = " ".join(stmt.split())
        if not s or s.startswith("typedef"):
            return
        words = re.findall(r"[A-Za-z_]\w*", s.split("(")[0])
        is_extern = "extern" in words
        is_static = "static" in words
        # Drop a struct/union/enum body that is part of the type.
        body_free = s
        while True:
            m = re.search(r"\{[^{}]*\}", body_free)
            if not m:
                break
            body_free = body_free[:m.start()] + body_free[m.end():]
        if "=" in s and not is_extern:
            init = s.split("=", 1)[1]
            self.init_names |= set(re.findall(r"[A-Za-z_]\w*", init))
        decls = split_top(body_free, ",")
        for k, d in enumerate(decls):
            name, is_fn = declarator_name(d)
            if not name:
                continue
            if k == 0:
                # `struct Tag;` or `enum E;` alone declares no object.
                if re.match(r"^\s*(?:struct|union|enum)\s+\w+\s*$", d):
                    continue
            if is_extern:
                (self.extern_functions if is_fn else self.extern_objects).add(name)
            elif is_fn:
                self.extern_functions.add(name)      # a prototype
            elif not is_static:
                self.defined_objects.add(name)


def read_text(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


# --------------------------------------------------------------------------
# aletheia metadata
# --------------------------------------------------------------------------

class Layout:
    """Ranges of one region (the ARM9 static module and its autoloads, or one
    overlay), as (start, end, image_address, kind) with image_address None
    for zero-initialised ranges."""

    def __init__(self, root: str, region: str):
        with open(os.path.join(root, "layout", "layout.json"), encoding="utf-8") as fh:
            lay = json.load(fh)
        self.region = region
        self.overlay = -1
        self.copy = []       # (start, end, image_address, label)
        self.zero = []       # (start, end, label)
        self.code = []       # (start, end): code, relocations skipped
        self.twl = None
        self.overlay_ranges = []
        load = lay["arm9"]["load_address"]
        self.load_address = load
        if region == "arm9":
            st = lay["arm9"]["static"]
            self.copy.append((st["ram_start"], st["ram_end"], st["ram_start"], "static"))
            self.zero.append((st["bss_start"], st["bss_end"], "static .bss"))
            for al in lay["arm9"]["autoloads"]:
                label = "autoload%d %s" % (al["index"], al["kind"])
                img = load + al["src_offset"]
                if al["ram_end"] > al["ram_start"]:
                    self.copy.append((al["ram_start"], al["ram_end"], img, label))
                if al["bss_end"] > al["bss_start"]:
                    self.zero.append((al["bss_start"], al["bss_end"], label + " .bss"))
        else:
            m = re.match(r"^ov(\d{3})$", region)
            if not m:
                raise SystemExit("armdata.py: unknown region %r" % region)
            self.overlay = int(m.group(1))
            ov = [o for o in lay["overlays"] if o["id"] == self.overlay]
            if not ov:
                raise SystemExit("armdata.py: no overlay %d in layout.json" % self.overlay)
            ov = ov[0]
            # Overlay image addresses are offsets into its own file.
            self.copy.append((ov["ram_start"], ov["bss_start"], 0, "overlay %d" % self.overlay))
            if ov["bss_end"] > ov["bss_start"]:
                self.zero.append((ov["bss_start"], ov["bss_end"], "overlay .bss"))
        tw = lay.get("twl")
        if tw:
            self.twl = (tw["ram_start"], tw["bss_end"])
        for o in lay["overlays"]:
            self.overlay_ranges.append((o["ram_start"], o["bss_end"], o["id"]))

    def section(self, addr: int) -> str:
        for s, e, _img, label in self.copy:
            if s <= addr < e:
                return "data"
        for s, e, label in self.zero:
            if s <= addr < e:
                return "bss"
        if addr < CONST_LIMIT:
            return "const"
        if self.twl and self.twl[0] <= addr < self.twl[1]:
            return "twl"
        for s, e, _id in self.overlay_ranges:
            if s <= addr < e:
                return "overlay"
        return "other"

    def in_copy(self, addr: int) -> bool:
        return any(s <= addr < e for s, e, _i, _l in self.copy)


def load_data_objects(root: str, region: str):
    path = os.path.join(root, "layout", "data_objects.json")
    if not os.path.exists(path):
        return None, [], {}
    with open(path, encoding="utf-8") as fh:
        d = json.load(fh)
    r = d.get("regions", {}).get(region)
    if r is None:
        return None, [], {}
    rng = (int(r["range"][0], 16), int(r["range"][1], 16))
    return rng, r["objects"], {int(e["addr"], 16): e.get("kind") for e in r.get("externs", [])}


def object_pointer_words(obj):
    """(offset, target) for every non-null pointer word of one data object."""
    out = []
    kind = obj["kind"]
    targets = obj.get("targets") or []
    if kind == "pointer_table":
        for i, t in enumerate(targets):
            out.append((4 * i, t))
    elif kind == "struct":
        off, ti = 0, 0
        for typ, count in obj.get("fields", []):
            size = {"ptr": 4, "u32": 4, "s32": 4, "u16": 2, "s16": 2,
                    "u8": 1, "s8": 1}.get(typ)
            if size is None:
                raise SystemExit("armdata.py: unknown field type %r in %s"
                                 % (typ, obj["name"]))
            for _ in range(count):
                if typ == "ptr":
                    out.append((off, targets[ti] if ti < len(targets) else 0))
                    ti += 1
                off += size
    res = []
    for off, t in out:
        if isinstance(t, str):
            t = int(t, 16)
        if t:
            res.append((int(obj["addr"], 16) + off, t))
    return res


ANNOT = re.compile(
    r"/\*\s*([0-9A-Fa-f]{8})\s+->\s+([A-Za-z_]\w*?)(?:\+0x([0-9A-Fa-f]+))?(\s+\(thumb\))?\s*\*/")
WORD_LINE = re.compile(r"^\s*\.word\b")


def listing_annotations(root: str, region: str, layout: Layout):
    """{location: target} from the `-> symbol` annotations of the reassembly
    listings.  Only the annotation is read: the symbol's address comes from
    its name (every listing symbol spells its address, or is a section
    named in layout.json), never from the word's value."""
    if region == "arm9":
        files = sorted(glob.glob(os.path.join(root, "asm", "arm9", "*.s")))
    else:
        # aletheia keeps one listing per overlay, asm/overlays/ovNNN.s.
        files = sorted(glob.glob(os.path.join(root, "asm", "overlays", region + ".s")))
    bases = {"arm9_main": layout.load_address}
    lay_path = os.path.join(root, "layout", "layout.json")
    with open(lay_path, encoding="utf-8") as fh:
        lay = json.load(fh)
    for al in lay["arm9"]["autoloads"]:
        bases["arm9_autoload%d_%s" % (al["index"], al["kind"].lower())] = al["ram_start"]
        if al["kind"] == "ITCM":
            bases["arm9_itcm"] = al["ram_start"]
        if al["kind"] == "VRAM_LCDC":
            bases["arm9_vram_lcdc"] = al["ram_start"]
    out, unknown = {}, set()
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "->" not in line or not WORD_LINE.match(line):
                    continue
                m = ANNOT.search(line)
                if not m:
                    continue
                loc = int(m.group(1), 16)
                sym = m.group(2)
                off = int(m.group(3), 16) if m.group(3) else 0
                base = bases.get(sym)
                if base is None:
                    base = name_address(sym)
                if base is None:
                    unknown.add(sym)
                    continue
                tgt = base + off
                if m.group(4):
                    tgt |= 1
                out[loc] = tgt
    return out, unknown


def read_symbol_txt(path: str):
    by_addr = {}
    if not os.path.exists(path):
        return by_addr
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            try:
                addr = int(parts[0], 16)
            except (ValueError, IndexError):
                continue
            if len(parts) >= 2:
                by_addr.setdefault(addr, []).append(parts[1])
    return by_addr


def load_function_addrs(root: str, region: str):
    """address -> (name, isa): the tree's own table of the functions it
    defines (`layout/function_addrs.tsv`).

    The tree's current names come first in the map, because the old
    layout's per-packet shards still carry the names the tree has since
    renamed, and a name the tree no longer defines is a name the port
    cannot link against."""
    out = {}
    path = os.path.join(root, "layout", "function_addrs.tsv")
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4 or parts[0] != region:
                continue
            try:
                addr = int(parts[1], 16)
            except ValueError:
                continue
            if addr not in out:
                out[addr] = (parts[2], parts[3])
    return out


def load_function_names(root: str, sources, region: str = None):
    """address -> [names]: the SDK's symbols by address, and every function a
    source of the build says it defines (its `function:`/`address:` header,
    its `symbols:` rows for code).  The manifest shards say the same about
    matched files but are thousands of small files; the build's own headers
    are what decides which names exist here anyway.

    A file that defines several functions gives ONE `address:` line, so its
    later functions (a descriptive name, no address in the header) never
    reach this map, and a data word pointing at one is left unrelocated by
    the runtime scan (sdk/armdata.c, scan_code_pointers).  aletheia's
    per-packet manifest (layout/matched/<region>/*.json) pairs each unit's
    `functions` with its `addresses`; reading that pair completes the map.
    The pair is exact, so the scan keeps keying on exact function entries
    (Thumb bit included) and no code-range heuristic is introduced."""
    by_addr = {}
    isa_by_addr = {}
    for fname in ("symbols_sdk.txt", "runtime_arm9.txt"):
        for addr, names in read_symbol_txt(os.path.join(root, "layout", fname)).items():
            by_addr.setdefault(addr, []).extend(names)
    table_rows = {n: a for a, ns in by_addr.items() for n in ns}
    for sf in sources:
        for name, addr in sf.header_functions:
            by_addr.setdefault(addr, []).append(name)
        for name, (addr, kind, _r, _a) in sf.symbols.items():
            if kind in ("arm", "thumb"):
                by_addr.setdefault(addr, []).append(name)
    if region:
        for shard in sorted(glob.glob(os.path.join(
                root, "layout", "matched", region, "*.json"))):
            try:
                with open(shard, encoding="utf-8") as fh:
                    d = json.load(fh)
            except (OSError, ValueError):
                continue
            for entry in d.get("functions") or []:
                isa = entry.get("isa")
                names = entry.get("functions") or []
                addrs = entry.get("addresses") or []
                if not names or not addrs:
                    names = [entry["name"]] if entry.get("name") else []
                    addrs = [entry["address"]] if entry.get("address") else []
                rows = None if region.startswith("ov") else table_rows
                paired = gametree.pair_members(names, [int(a, 16) for a in addrs], rows)
                for name, a in zip(names, paired):
                    if a is None:
                        continue
                    by_addr.setdefault(a, []).append(name)
                    if isa in ("arm", "thumb"):
                        isa_by_addr.setdefault(a, isa)
    # The tree's own table of what it defines comes first: a name the tree
    # has renamed is not a name the port can link against, and the shards
    # above still carry the old one.
    if region:
        for addr, (name, isa) in load_function_addrs(root, region).items():
            names = by_addr.get(addr) or []
            by_addr[addr] = [name] + [n for n in names if n != name]
            if isa in ("arm", "thumb"):
                isa_by_addr.setdefault(addr, isa)
    return by_addr, isa_by_addr


# --------------------------------------------------------------------------
# Exclusions
# --------------------------------------------------------------------------

def load_exclusions(path: str):
    """name -> (address or None, size or None, owner).  See the file."""
    out = {}
    if not path or not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            name = parts[0]
            addr = int(parts[1], 16) if len(parts) > 1 and parts[1] != "-" else None
            size = int(parts[2], 16) if len(parts) > 2 and parts[2] != "-" else None
            owner = parts[3] if len(parts) > 3 else "?"
            out[name] = (addr, size, owner)
    return out


def declared_arm9_rows(path: str = DECLARED_SET):
    """The ARM9's rows of mod/declared_set.txt: (address, name).  The rows
    are the addresses a mod may hook (docs/mod-interface.md, section 1), and
    the code entries table carries them, so a code pointer stored in the
    data reaches the registry for them (sdk/armdata.c, bw_arm9_code_entry).

    The file is the port's own, not aletheia's: a tree without it has no
    declared rows, and the tables are the ones the code scan lists."""
    rows = []
    if not path or not os.path.exists(path):
        return rows
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) < 3 or parts[2].strip() != "arm9":
                continue
            try:
                addr = int(parts[0].strip(), 16)
            except ValueError:
                continue
            rows.append((addr, parts[1].strip()))
    return rows


# --------------------------------------------------------------------------
# The model
# --------------------------------------------------------------------------

# The ARM9 static module's load address: the first byte of arm9_dec.bin is
# DS 0x02004000 (layout.json's arm9.load_address).
ARM9_IMAGE_BASE = 0x02004000
# `ldr ip, [pc]; bx ip`, then the target word: a linker veneer.
VENEER_CODE = bytes.fromhex("00c09fe51cff2fe1")
# `ldr ip, [pc, #4]; add r0, r0, #imm; bx ip`, then the target word: an
# adjustor thunk, the target called with r0 moved by imm (the vtable at
# 0x020A7F3C points at two of them, 0x0204D4E0 and 0x0204D4F0).
ADJUST_LDR = bytes.fromhex("04c09fe5")
ADJUST_BX = bytes.fromhex("1cff2fe1")


class Model:
    def __init__(self, args):
        self.root = os.path.abspath(args.aletheia_root)
        self.image = None
        self.adjust = {}           # thunk address -> (imm, target name)
        if getattr(args, "arm9_image", None) and os.path.exists(args.arm9_image):
            with open(args.arm9_image, "rb") as fh:
                self.image = fh.read()
        self.region = args.region
        self.warnings = []
        self.scan_code = getattr(args, "code_scan", False)
        self.layout = Layout(self.root, self.region)
        self.data_range, self.objects, self.extern_kinds = load_data_objects(self.root, self.region)
        self.exclusions = load_exclusions(args.exclusions)
        self.sources = []
        self.missing = []
        self._game_cuts = {}
        if getattr(args, "cut_list", None) and os.path.exists(args.cut_list):
            with open(args.cut_list, encoding="utf-8") as fh:
                for line in fh:
                    p, _, names = line.rstrip("\n").partition("\t")
                    if p:
                        self._game_cuts[os.path.normcase(os.path.abspath(p))] = \
                            set(n for n in names.split(",") if n)
        # Port sources compiled with the header like matched ones (the
        # boot's transliterations of asm functions name game data by its
        # matched spelling): their names are bound too.
        self.bind_also = {os.path.normcase(os.path.abspath(p))
                          for p in getattr(args, "bind_source", [])}
        outs = {os.path.normcase(os.path.abspath(p))
                for p in (args.out_c, args.out_h) if p}
        for path in args.source_paths:
            if os.path.normcase(os.path.abspath(path)) in outs:
                continue
            path = self._original(path)
            if not os.path.exists(path):
                # A file the build generates (overlay_table.c, a dsaddr copy
                # of a file outside src/matched) that does not exist yet.  A
                # missing file in aletheia's tree is a wrong path, not that.
                if os.path.normcase(os.path.abspath(path)).startswith(
                        os.path.normcase(self.root + os.sep)):
                    raise SystemExit("armdata.py: no such source %s" % path)
                self.missing.append(path)
                continue
            text = read_text(path)
            if "/src/game/" in path.replace("\\", "/"):
                # As the copy compiles: the functions the port defines and
                # the asm bodies are cut (cmake/dsaddr.cmake, --cut-list).
                import gamecut
                text = gamecut.cut(text, self._game_cuts.get(
                    os.path.normcase(os.path.abspath(path)), set()), True)[0]
            matched = self._is_matched(path, text)
            self.sources.append(_PARSES.get(("SourceFile", path, matched), text,
                                            lambda: SourceFile(path, text, matched)))
        self.func_names, self.isa_by_addr = load_function_names(
            self.root, self.sources, self.region)
        self._collect_definitions()
        self._collect_names()
        self._collect_relocations(args)
        self._resolve_functions(args.all_functions)
        self._code_entries()

    # -- which sources are matched ----------------------------------------

    def _original(self, path: str) -> str:
        """A dsaddr copy (<build>/dsaddr/<rel>, cmake/dsaddr.cmake) stands
        for <aletheia>/src/matched/<rel>: the rewrite changes casts only, so
        the original says the same about names, and it exists before the
        build has made the copy."""
        p = path.replace("\\", "/")
        i = p.rfind("/dsaddr/")
        if i >= 0:
            rel = p[i + len("/dsaddr/"):]
            if not rel.startswith("_other/"):
                orig = os.path.join(self.root, "src", "matched", rel)
                if os.path.exists(orig):
                    return orig
        return path

    def _is_matched(self, path: str, text: str) -> bool:
        if os.path.normcase(os.path.abspath(path)) in self.bind_also:
            return True
        p = os.path.abspath(path).replace("\\", "/").lower()
        src = (self.root.replace("\\", "/") + "/src/").lower()
        if p.startswith(src):
            return True
        m = LINE_DIRECTIVE.search(text[:4096])
        if m:
            orig = os.path.abspath(m.group(1)).replace("\\", "/").lower()
            if orig.startswith(src):
                return True
        # A dsaddr copy of a file with nothing to rewrite has no #line; it
        # lives under a dsaddr/ directory of the build tree.
        return "/dsaddr/" in p

    # -- what the build defines --------------------------------------------

    def _collect_definitions(self):
        self.defined_objects = {}
        self.defined_functions = {}
        for sf in self.sources:
            for n in sf.defined_objects:
                self.defined_objects.setdefault(n, sf.path)
            for n in sf.defined_functions:
                self.defined_functions.setdefault(n, sf.path)
        # Every symbols-block row in the build, for names that a file uses
        # without a row of its own.
        self.rows = {}
        for sf in self.sources:
            for n, row in sf.symbols.items():
                self.rows.setdefault(n, row)

    def address_of(self, name: str, sf: SourceFile = None):
        if sf is not None and name in sf.symbols:
            return sf.symbols[name][0], "symbols"
        if name in self.rows:
            return self.rows[name][0], "symbols"
        ex = self.exclusions.get(name)
        if ex and ex[0] is not None:
            return ex[0], "exclusions"
        a = name_address(name)
        if a is not None:
            return a, "name"
        return None, None

    # -- the names to bind ---------------------------------------------------

    def _collect_names(self):
        refs = {}          # name -> [sources]
        kinds = {}
        for sf in self.sources:
            if not sf.matched:
                continue
            used = set(sf.extern_objects)
            for n, row in sf.symbols.items():
                if row[1] == "data" and (n in sf.extern_functions or n in sf.extern_objects):
                    used.add(n)
                # A data row the file uses but declares in its own header
                # (aletheia's per-file headers: net_save.h's
                # `extern const ProcData gNetSaveMainProc;`), not here.
                elif (row[1] == "data" and n in sf.identifiers
                      and n not in sf.defined_objects and n not in sf.shadowed):
                    used.add(n)
            for n in used:
                refs.setdefault(n, []).append(sf)
        self.port_defined = {}     # name -> (addr, size, owner) of a port object
        self.names = {}            # name -> dict
        self.unresolved = {}       # name -> reason
        self.excluded_used = {}
        for n in sorted(refs):
            srcs = refs[n]
            if n in self.defined_objects or n in self.defined_functions:
                addr, _how = self.address_of(n, srcs[0])
                ex = self.exclusions.get(n)
                definer = self.defined_objects.get(n) or self.defined_functions.get(n)
                if ex is None and not self._is_matched_path(definer):
                    self.warnings.append(
                        "%s is defined by %s but not listed in the exclusions"
                        % (n, os.path.basename(definer)))
                self.excluded_used[n] = (addr, definer)
                continue
            addr, how = self.address_of(n, srcs[0])
            if addr is None:
                self.unresolved[n] = "no address in any symbols block or name"
                continue
            # Disagreeing rows for one name are a metadata error.
            for sf in srcs[1:]:
                if n in sf.symbols and sf.symbols[n][0] != addr:
                    self.warnings.append("%s: %s says %s, %s says %s" % (
                        n, os.path.basename(srcs[0].path), hx(addr),
                        os.path.basename(sf.path), hx(sf.symbols[n][0])))
            # A name some matched file also uses as a tag, member, local,
            # label or in a static initialiser cannot be a macro: it becomes
            # a host object registered at its address instead.
            conflict = [os.path.basename(sf.path) for sf in self.sources
                        if sf.matched and n in sf.identifiers
                        and (n in sf.members or n in sf.init_names or n in sf.shadowed)]
            row = None
            for sf in srcs:
                if n in sf.symbols:
                    row = sf.symbols[n]
                    break
            self.names[n] = {
                "name": n,
                "addr": addr,
                "how": how,
                "align": row[3] if row and row[3] else 0,
                "region_tag": row[2] if row else None,
                "users": sorted(os.path.basename(sf.path) for sf in srcs),
                "object": sorted(conflict),
            }
        # Port objects by address (exclusions with an address, plus the
        # build-defined names a matched file pins by address).
        for n, (addr, size, owner) in self.exclusions.items():
            if addr is None:
                continue
            if n in self.defined_objects:
                self.port_defined[n] = (addr, size or 0, owner)
        self._bind()

    def _is_matched_path(self, path):
        for sf in self.sources:
            if sf.path == path:
                return sf.matched
        return False

    def port_object_at(self, addr: int):
        """(name, base, size) of the port object whose DS range holds addr."""
        best = None
        for n, (a, size, _o) in sorted(self.port_defined.items()):
            if a == addr or (size and a <= addr < a + size):
                # The latest start wins; at one start, the entry with an
                # extent (the registered one) wins over a bare alias.
                key = (a, 1 if size else 0)
                if best is None or key > (best[1], 1 if best[2] else 0):
                    best = (n, a, size)
        return best

    def _bind(self):
        sizes = {int(o["addr"], 16): o["size"] for o in self.objects}
        for n, e in self.names.items():
            addr = e["addr"]
            sec = self.layout.section(addr)
            e["section"] = sec
            e["size"] = sizes.get(addr, 0)
            port = self.port_object_at(addr)
            # Below main RAM nothing backs a name: the ITCM's code (the
            # autoload copied to 0x01FF8000), which matched files name only
            # for its address (`&OS_IrqHandler` as a sentinel).  The name is
            # that address as a value, as for a linker constant.
            #
            # The DSi's extended RAM (0x0D000000) is the same: this NITRO port
            # backs no region for it, and a matched file names it only in a
            # dead TWL branch (OS_GetInitArenaLo case 2 behind OS_IsRunOnTwl).
            # Binding it to a DS pointer would panic at reset (sdk/armdata.c,
            # bw_data_bind), so the name is its address as a value too.
            itcm = sec != "const" and (addr < MAIN_RAM_BASE
                                       or addr >= TWL_EXT_RAM_BASE) and port is None
            # A linker symbol a static initialiser names (SDK_STATIC_BSS_END
            # in src/game's crt0.c tables): its address is the value, never
            # storage to register over whatever lies there.
            if n.startswith("SDK_") and e["object"] and port is None:
                e["bind"] = "CONST"
                continue
            if e["object"] and sec != "const" and not itcm and port is None:
                e["bind"] = "OBJECT"
                continue
            if sec == "const" or itcm:
                e["bind"] = "CONST"
            elif port is not None:
                e["bind"] = "HOST"
                e["host"] = port
            else:
                e["bind"] = "DS"
        self._size_objects()

    def _size_objects(self):
        """Extent of each OBJECT name: its data_objects.json size, else up to
        the next address anything else in the build starts at (another name,
        a port object, a data object, a section edge), so that no two
        registrations can overlap."""
        bounds = set()
        for e in self.names.values():
            if e["section"] != "const":
                bounds.add(e["addr"])
        for _n, (a, size, _o) in self.port_defined.items():
            bounds.add(a)
            if size:
                bounds.add(a + size)
        for o in self.objects:
            bounds.add(int(o["addr"], 16))
        for s, e, _i, _l in self.layout.copy:
            bounds |= {s, e}
        for s, e, _l in self.layout.zero:
            bounds |= {s, e}
        bounds = sorted(bounds)
        self.host_objects = {}       # address -> name
        for n in sorted(self.names, key=lambda k: (self.names[k]["addr"], k)):
            e = self.names[n]
            if e["bind"] != "OBJECT":
                continue
            a = e["addr"]
            nxt = [b for b in bounds if b > a]
            limit = (nxt[0] - a) if nxt else 0x1000
            size = e["size"] or limit
            if size > limit:
                size = limit
            if size > 0x10000:
                self.warnings.append("%s: object extent 0x%X capped at 0x10000" % (n, size))
                size = 0x10000
            e["size"] = size
            if a in self.host_objects:
                self.warnings.append(
                    "%s and %s both need a host object at %s; only the first is "
                    "registered, the second is separate storage"
                    % (self.host_objects[a], n, hx(a)))
                e["registered"] = False
            else:
                self.host_objects[a] = n
                e["registered"] = True

    # -- relocations ---------------------------------------------------------

    def _collect_relocations(self, args):
        relocs = {}
        source = {}
        lo = self.data_range[0] if self.data_range else None
        for obj in self.objects:
            for loc, tgt in object_pointer_words(obj):
                relocs[loc] = tgt
                source[loc] = "data_objects"
        annotations, unknown = ({}, set())
        if not args.no_listing:
            annotations, unknown = listing_annotations(self.root, self.region, self.layout)
        for sym in sorted(unknown):
            self.warnings.append("listing symbol %s has no address; its words are skipped" % sym)
        self.annotated = len(annotations)
        disagree = 0
        for loc, tgt in annotations.items():
            if not self.layout.in_copy(loc):
                continue
            # Code: the static module below the first data object is code
            # and literal pools, which the host never reads as data.
            if self.region == "arm9" and lo is not None:
                st = self.layout.copy[0]
                if st[0] <= loc < lo:
                    continue
            if self._in_code_autoload(loc):
                continue
            if loc in relocs:
                if relocs[loc] != tgt:
                    disagree += 1
                    self.warnings.append(
                        "word %s: data_objects says %s, the listing %s; using the listing"
                        % (hx(loc), hx(relocs[loc]), hx(tgt)))
                    relocs[loc] = tgt
                continue
            relocs[loc] = tgt
            source[loc] = "listing"
        self.reloc_skipped_port = []
        self.relocs = []
        for loc in sorted(relocs):
            port = self.port_object_at(loc)
            if port is not None and port[2]:
                self.reloc_skipped_port.append((loc, port[0]))
                continue
            self.relocs.append({"where": loc, "target": relocs[loc], "from": source[loc]})

    def _in_code_autoload(self, loc: int) -> bool:
        # The ITCM and VRAM autoloads hold code; DTCM / MAIN_TWL ones data.
        for s, e, _img, label in self.layout.copy:
            if s <= loc < e and ("ITCM" in label or "VRAM" in label):
                return True
        return False

    # -- relocation targets ---------------------------------------------------

    def is_code(self, tgt: int) -> bool:
        if tgt & 1:
            return True
        self._function_name(0)                  # loads self._veneers
        if tgt in self._veneers:
            return True                         # a linker veneer is code
        if self._image_island(tgt):
            return True                         # the same, read from the image
        kind = self.extern_kinds.get(tgt)
        if kind is not None:
            return kind == "func"
        if (tgt & ~1) in self.func_names:
            return True
        if self.data_range and self.data_range[0] <= tgt < self.data_range[1]:
            return False
        sec = self.layout.section(tgt)
        if sec == "data" and self.region == "arm9":
            st = self.layout.copy[0]
            lo = self.data_range[0] if self.data_range else st[1]
            if st[0] <= tgt < lo:
                return True
            for s, e, _img, label in self.layout.copy:
                if s <= tgt < e and ("ITCM" in label or "VRAM" in label):
                    return True
        return False

    def _resolve_functions(self, all_functions: bool):
        self.funcs = {}            # target value -> (name or None)
        self.anchors = {}          # port name -> (addr, size)
        for r in self.relocs:
            tgt = r["target"]
            if self.is_code(tgt):
                r["kind"] = "CODE"
                if tgt not in self.funcs:
                    self.funcs[tgt] = self._function_name(tgt & ~1)
            else:
                port = self.port_object_at(tgt)
                if port is not None:
                    r["kind"] = "HOST"
                    r["anchor"] = port[0]
                    self.anchors[port[0]] = (port[1], port[2])
                else:
                    r["kind"] = "DATA"
        if all_functions:
            for addr, names in self.func_names.items():
                for n in names:
                    if n in self.defined_functions:
                        self.funcs.setdefault(addr, n)
                        break
        for n, e in self.names.items():
            if e["bind"] == "HOST":
                self.anchors[e["host"][0]] = (e["host"][1], e["host"][2])

    # -- the code-pointer scan (docs/armdata.md, "Code pointers the metadata
    #    does not list") ----------------------------------------------------

    def _isa_of(self, name: str, addr: int):
        """'arm' or 'thumb' for the function the build defines as `name` at
        `addr`: aletheia's manifest when it names the address, else the
        defining file's header when it lists the function, else any
        symbols-block row in the build for a spelling of that address."""
        if addr in self.isa_by_addr:
            return self.isa_by_addr[addr]
        sf = self.by_path.get(self.defined_functions.get(name))
        if sf is not None and sf.header_isa:
            if any(a == addr for _n, a in sf.header_functions):
                return sf.header_isa
        cands = list(self.func_names.get(addr, [])) + [
            name, "func_%08X" % addr, "sub_%08X" % addr]
        for n in cands:
            row = self.rows.get(n)
            if row is not None and row[0] == addr and row[1] in ("arm", "thumb"):
                return row[1]
        return None

    def _code_entries(self):
        """Every function the build defines in this region, by the value a
        code pointer to it holds (Thumb entries with bit 0 set), and the
        ranges whose words the runtime checks against them: the data the
        metadata covers (the static module from its first data object on,
        and the data autoloads).  A word there that equals an entry exactly,
        with the entry's own instruction-set bit, is a code pointer the
        metadata did not list; the runtime relocates it like a listed one.

        The ARM9's declared addresses (mod/declared_set.txt) are in the
        table as well: they are the addresses a mod may hook, and a code
        pointer stored in the data reaches the registry for them
        (sdk/armdata.c, bw_arm9_code_entry).  The code scan lists the same
        functions when the program links them all; this makes the table
        carry the seam's addresses by contract."""
        self.scan_ranges = []
        self.isa_unknown = 0
        self.declared_rows = []
        self.declared_missing = []
        if self.region != "arm9":
            return
        self.by_path = {sf.path: sf for sf in self.sources}
        self._declared_entries()
        if not self.scan_code:
            return
        for addr in sorted(self.func_names):
            n = self._function_name(addr)
            if n is None:
                continue
            isa = self._isa_of(n, addr)
            if isa is None:
                self.isa_unknown += 1
                continue
            val = addr | (1 if isa == "thumb" else 0)
            self.funcs.setdefault(val, n)
        # Veneers are ARM code: a pointer to one holds its even address.
        self._function_name(0)                  # loads self._veneers
        for v in sorted(self._veneers):
            n = self._function_name(v)
            if n is not None:
                self.funcs.setdefault(v, n)
        # The same islands read out of the image, for a pin with no listing.
        if self.image is not None:
            img = self.image
            for off in range(0, len(img) - 15, 4):
                if img[off:off + 8] == VENEER_CODE or (
                        img[off:off + 4] == ADJUST_LDR and img[off + 8:off + 12] == ADJUST_BX):
                    v = ARM9_IMAGE_BASE + off
                    if v in self.funcs:
                        continue
                    n = self._function_name(v)
                    if n is not None:
                        self.funcs.setdefault(v, n)
        lo = self.data_range[0] if self.data_range else None
        for s_, e_, _img, label in self.layout.copy:
            if "ITCM" in label or "VRAM" in label:
                continue
            if label == "static":
                if lo is None:
                    continue
                s_ = max(s_, lo)
            if e_ > s_:
                self.scan_ranges.append((s_, e_))

    def _declared_entries(self):
        """The ARM9's declared addresses (mod/declared_set.txt) in the code
        entries table: the addresses a mod may hook (docs/mod-interface.md,
        section 1), so a code pointer stored in the data reaches the registry
        for them (sdk/armdata.c, bw_arm9_code_entry).

        The entry is keyed by the value a pointer to the function holds, the
        even address for an ARM function and the odd one for a Thumb
        function, so the runtime's scan finds the word.  A row whose
        instruction set is not known anywhere has no such value: the row is
        reported and left out, because an entry with the wrong bit would
        never match the word."""
        for addr, name in declared_arm9_rows():
            isa = self.isa_by_addr.get(addr)
            if isa is None:
                isa = self._isa_of(self._function_name(addr) or name, addr)
            if isa is None:
                self.declared_missing.append((addr, name))
                continue
            val = addr | (1 if isa == "thumb" else 0)
            # The build's own name for the function, or None: the entry is
            # then a trap, the way a listed relocation's is (the game calls
            # the address and the port says which one it is).
            self.funcs.setdefault(val, self._function_name(addr))
            self.declared_rows.append((addr, name, val))

    def _function_name(self, addr: int):
        cands = list(self.func_names.get(addr, []))
        a = "func_%08X" % addr
        for extra in (a, "sub_%08X" % addr):
            if extra not in cands:
                cands.append(extra)
        for n in cands:
            if n in self.defined_functions:
                return n
        # A linker veneer (the listing's veneer_ labels) is another address
        # of its target: a table that points at the veneer reaches the
        # function behind it (the allocator table at 0x020A1754).
        if not hasattr(self, "_veneers"):
            import closure  # noqa: E402  (tools/closure.py, imported late)
            self._veneers = closure.listing_veneers(self.root)
        target = self._veneers.get(addr)
        if target is not None and target != addr:
            return self._function_name(target)
        return self._image_thunk(addr)

    def _image_island(self, addr: int) -> bool:
        """True when the image holds a veneer or an adjustor thunk at addr."""
        if self.image is None or self.region != "arm9":
            return False
        off = addr - ARM9_IMAGE_BASE
        if off < 0 or off & 3 or off + 16 > len(self.image):
            return False
        img = self.image
        return img[off:off + 8] == VENEER_CODE or (
            img[off:off + 4] == ADJUST_LDR and img[off + 8:off + 12] == ADJUST_BX
            and (int.from_bytes(img[off + 4:off + 8], "little") & 0xFFFFFF00) == 0xE2800000)

    def _image_thunk(self, addr: int):
        """The name for a veneer or an adjustor thunk at `addr` in the
        player's ARM9 image (--arm9-image), or None.  The listing labels the
        veneers, but it is generated and not in a clean export of the pin."""
        if self.image is None or self.region != "arm9":
            return None
        off = addr - ARM9_IMAGE_BASE
        if off < 0 or off + 16 > len(self.image):
            return None
        code = self.image[off:off + 16]
        if code[:8] == VENEER_CODE:
            target = int.from_bytes(code[8:12], "little")
            if target != addr:
                return self._function_name(target & ~1)
            return None
        word = int.from_bytes(code[4:8], "little")
        if code[:4] == ADJUST_LDR and code[8:12] == ADJUST_BX \
                and (word & 0xFFFFFF00) == 0xE2800000:
            target = int.from_bytes(code[12:16], "little")
            tname = self._function_name(target & ~1)
            if tname:
                self.adjust[addr] = (word & 0xFF, tname)
                return "bw_adj_%08X" % addr
        return None


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

def c_ident_ok(name: str) -> bool:
    return re.match(r"^[A-Za-z_]\w*$", name) is not None


def write_header(m: Model, path: str):
    guard = "BW_ARMDATA_%s_H" % m.region.upper()
    lines = [
        "/*",
        " * Generated by tools/armdata.py for region %s; do not edit." % m.region,
        " *",
        " * Forced include for the matched sources of this build (docs/armdata.md).",
        " * Each data name below becomes a dereference of a pointer variable that",
        " * sdk/armdata.c binds, at every bw_ds_reset, to the name's DS address in",
        " * the port's copy of the ARM9 image.  A matched file's own declaration",
        " * `extern T NAME;` becomes `extern T (*bw_dsref_NAME);`, so each file keeps",
        " * its own type for the object.  Not for port code: port code reaches DS",
        " * data through bw_ds_ptr.",
        " */",
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
    ]
    for n in sorted(m.names):
        e = m.names[n]
        if e["bind"] == "OBJECT":
            lines.append("/* %s is a host object (%s): used as a member, tag, local "
                         "or in an initialiser */" % (n, hx(e["addr"])))
            continue
        lines.append("#define %s (*bw_dsref_%s)   /* %s %s */" % (
            n, n, hx(e["addr"]), e["section"]))
    lines += ["", "#endif /* %s */" % guard, ""]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


SECTION_ENUM = {
    "data": "BW_DATA_SEC_DATA", "bss": "BW_DATA_SEC_BSS",
    "const": "BW_DATA_SEC_CONST", "twl": "BW_DATA_SEC_TWL",
    "overlay": "BW_DATA_SEC_OVERLAY", "other": "BW_DATA_SEC_OTHER",
}


def write_c(m: Model, path: str):
    sym = "bw_%s_data" % m.region
    out = []
    w = out.append
    w("/*")
    w(" * Generated by tools/armdata.py for region %s; do not edit." % m.region)
    w(" *")
    w(" * Addresses, sizes and names only: no byte of the game is in this file.")
    w(" * sdk/armdata.c copies the image out of the player's own extract at run")
    w(" * time and uses these tables to relocate and bind it (docs/armdata.md).")
    w(" */")
    w("")
    w('#include "armdata.h"')
    w("")
    # Functions the relocations point at.
    defined = sorted({n for n in m.funcs.values() if n and not n.startswith("bw_adj_")})
    traps = sorted(t for t, n in m.funcs.items() if not n)
    if m.adjust:
        w("/* Adjustor thunks in the image (`ldr ip, [pc, #4]; add r0, r0, #imm;")
        w(" * bx ip`): the target called with its first argument moved, and the")
        w(" * other argument registers passed on. */")
        w("#include <stdint.h>")
        for a in sorted(m.adjust):
            imm, tname = m.adjust[a]
            w("extern void %s(void);" % tname)
            w("static uintptr_t bw_adj_%08X(char *a0, uintptr_t a1, uintptr_t a2, uintptr_t a3)" % a)
            w("{")
            w("    return ((uintptr_t (*)(char *, uintptr_t, uintptr_t, uintptr_t))%s)(a0 + 0x%X, a1, a2, a3);"
              % (tname, imm))
            w("}")
        w("")
    import closure  # noqa: E402  (tools/closure.py: HOST_RESERVED)
    csym = lambda n: closure.HOST_RESERVED.get(n, n)  # noqa: E731
    if defined:
        w("/* Functions the build defines that initialised data points at.  The")
        w(" * real prototypes differ; only the address is taken here. */")
        for n in defined:
            w("extern void %s(void);" % csym(n))
        w("")
    if traps:
        w("/* Code the data points at that the build does not define: calling it")
        w(" * panics with the DS address instead of jumping somewhere wrong. */")
        for t in sorted({t & ~1 for t in traps}):
            w("static void bw_trap_%08X(void) { bw_data_trap(0x%08Xu); }" % (t, t))
        w("")
    anchors = sorted(m.anchors)
    if anchors:
        w("/* Port objects (tools/armdata_exclusions.txt) that names or pointers")
        w(" * reach by address. */")
        for n in anchors:
            w("extern char %s[];" % n)
        w("")
    # Pointer variables.
    refs = [n for n in sorted(m.names) if m.names[n]["bind"] != "OBJECT"]
    if refs:
        w("/* The pointer variables of the forced-include header. */")
        for n in refs:
            w("void *bw_dsref_%s;" % n)
        w("")
    objs = [n for n in sorted(m.names, key=lambda k: (m.names[k]["addr"], k))
            if m.names[n]["bind"] == "OBJECT"]
    if objs:
        w("/* Names a matched file also uses as a tag, member, local or in an")
        w(" * initialiser, so they cannot be macros: real host objects, filled")
        w(" * from the image and registered at their DS address.  Each matched")
        w(" * file declares its own type for them; the storage is untyped cells. */")
        for n in objs:
            w("BwDataCell %s[BW_DATA_CELLS(0x%Xu)];   /* %s, users: %s */" % (
                n, m.names[n]["size"], hx(m.names[n]["addr"]),
                ", ".join(m.names[n]["object"])))
        w("")
    # Names.
    w("static const BwDataName k_names[] = {")
    for n in sorted(m.names, key=lambda k: (m.names[k]["addr"], k)):
        e = m.names[n]
        anchor = "NULL"
        anchor_addr = 0
        ref = "&bw_dsref_%s" % n
        if e["bind"] == "HOST":
            anchor = e["host"][0]
            anchor_addr = e["host"][1]
        elif e["bind"] == "OBJECT":
            ref = "NULL"
            anchor = n
            anchor_addr = e["addr"]
        w('    { "%s", 0x%08Xu, %s, BW_DATA_BIND_%s, %s, %u, 0x%Xu, %s, 0x%08Xu },' % (
            n, e["addr"], ref, e["bind"], SECTION_ENUM[e["section"]],
            e["align"], e["size"], anchor, anchor_addr))
    if not m.names:
        w("    { NULL, 0, NULL, 0, 0, 0, 0, NULL, 0 }")
    w("};")
    w("")
    # Ranges.
    w("static const BwDataRange k_ranges[] = {")
    for s, e, img, label in m.layout.copy:
        w("    { 0x%08Xu, 0x%Xu, 0x%08Xu, 1 },   /* %s */" % (s, e - s, img, label))
    for s, e, label in m.layout.zero:
        w("    { 0x%08Xu, 0x%Xu, 0, 0 },   /* %s */" % (s, e - s, label))
    w("};")
    w("")
    # Functions.
    fn_index = {}
    w("static const BwDataFunc k_funcs[] = {")
    for i, t in enumerate(sorted(m.funcs)):
        n = m.funcs[t]
        fn_index[t] = i
        if n and n.startswith("bw_adj_"):
            w('    { 0x%08Xu, (BwDataFn)%s, "%s" },' % (t, n, n))
        elif n:
            w('    { 0x%08Xu, %s, "%s" },' % (t, csym(n), n))
        else:
            w('    { 0x%08Xu, bw_trap_%08X, NULL },' % (t, t & ~1))
    if not m.funcs:
        w("    { 0, NULL, NULL }")
    w("};")
    w("")
    # Anchors.
    anchor_index = {}
    w("static const BwDataAnchor k_anchors[] = {")
    for i, n in enumerate(anchors):
        anchor_index[n] = i
        a, size = m.anchors[n]
        w('    { 0x%08Xu, 0x%Xu, %s, "%s" },' % (a, size, n, n))
    if not anchors:
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    # Relocations.
    w("static const BwDataReloc k_relocs[] = {")
    for r in m.relocs:
        if r["kind"] == "CODE":
            idx = fn_index[r["target"]]
        elif r["kind"] == "HOST":
            idx = anchor_index[r["anchor"]]
        else:
            idx = 0
        w("    { 0x%08Xu, 0x%08Xu, BW_DATA_RELOC_%s, %u }," % (
            r["where"], r["target"], r["kind"], idx))
    if not m.relocs:
        w("    { 0, 0, 0, 0 }")
    w("};")
    w("")
    w("/* Where the runtime looks for code pointers the metadata does not list. */")
    w("static const BwDataRange k_scan[] = {")
    for s_, e_ in m.scan_ranges:
        w("    { 0x%08Xu, 0x%Xu, 0, 0 }," % (s_, e_ - s_))
    if not m.scan_ranges:
        w("    { 0, 0, 0, 0 }")
    w("};")
    w("")
    w("static const BwDataObject k_objects[] = {")
    for n in objs:
        e = m.names[n]
        if e["registered"]:
            w('    { 0x%08Xu, 0x%Xu, %s, "%s" },' % (e["addr"], e["size"], n, n))
    if not any(m.names[n]["registered"] for n in objs):
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    w("const BwDataImage %s = {" % sym)
    w('    "%s", %d,' % (m.region, m.layout.overlay))
    w("    k_ranges, %u," % (len(m.layout.copy) + len(m.layout.zero)))
    w("    k_names, %u," % len(m.names))
    w("    k_relocs, %u," % len(m.relocs))
    w("    k_funcs, %u," % len(m.funcs))
    w("    k_anchors, %u," % len(anchors))
    w("    k_objects, %u," % sum(1 for n in objs if m.names[n]["registered"]))
    w("    k_scan, %u" % len(m.scan_ranges))
    w("};")
    w("")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))


def summary(m: Model, out):
    secs = {}
    for e in m.names.values():
        secs[e["section"]] = secs.get(e["section"], 0) + 1
    addrs = {e["addr"] for e in m.names.values()}
    kinds = {}
    for r in m.relocs:
        kinds[r["kind"]] = kinds.get(r["kind"], 0) + 1
    traps = sum(1 for n in m.funcs.values() if not n)
    p = lambda s: print(s, file=out)
    p("armdata %s: %d sources (%d matched, %d not generated yet)" % (
        m.region, len(m.sources), sum(1 for s in m.sources if s.matched),
        len(m.missing)))
    p("  names bound      %d at %d addresses  %s" % (
        len(m.names), len(addrs), " ".join("%s=%d" % kv for kv in sorted(secs.items()))))
    p("  host objects     %d (names that cannot be macros)" % sum(
        1 for e in m.names.values() if e["bind"] == "OBJECT"))
    p("  defined by build %d (port objects and matched definitions)" % len(m.excluded_used))
    p("  unresolved       %d" % len(m.unresolved))
    p("  relocations      %d  %s" % (len(m.relocs), " ".join("%s=%d" % kv for kv in sorted(kinds.items()))))
    p("  code targets     %d (%d defined by the build, %d trap)" % (
        len(m.funcs), len(m.funcs) - traps, traps))
    p("  skipped in port objects %d" % len(m.reloc_skipped_port))
    p("  code-pointer scan %d range(s); %d defined functions with no known "
      "instruction set left out" % (len(m.scan_ranges), m.isa_unknown))
    if m.region == "arm9":
        p("  declared rows    %d of %d in the code entries (a stored pointer "
          "to one reaches the registry)" % (
              len(m.declared_rows),
              len(m.declared_rows) + len(m.declared_missing)))
        for addr, name in m.declared_missing:
            p("    no instruction set %-24s %s" % (name, hx(addr)))
    for n, why in sorted(m.unresolved.items()):
        p("  unresolved %-28s %s" % (n, why))
    for n, (addr, definer) in sorted(m.excluded_used.items()):
        p("  build-defined %-28s %-10s %s" % (
            n, hx(addr) if addr is not None else "-", os.path.basename(definer)))
    for wmsg in m.warnings:
        p("  warning: " + wmsg)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--sources", action="append", default=[],
                    help="file listing the build's sources, one path per line")
    ap.add_argument("--source", action="append", default=[],
                    help="one source path (repeatable)")
    ap.add_argument("--bind-source", action="append", default=[],
                    help="a port source compiled with the header: bind the "
                         "data names it uses like a matched file's (repeatable)")
    ap.add_argument("--region", default="arm9")
    ap.add_argument("--cut-list",
                    help="src/game: `path<TAB>name,...` rows of functions the "
                         "port defines; each src/game source is read with those "
                         "and its asm bodies cut (tools/gamecut.py)")
    ap.add_argument("--exclusions", default=DEFAULT_EXCLUSIONS)
    ap.add_argument("--out-c")
    ap.add_argument("--out-h")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--all-functions", action="store_true",
                    help="list every function the build defines, not only "
                         "the ones initialised data points at")
    ap.add_argument("--code-scan", action="store_true",
                    help="list every function the build defines, and the data "
                         "ranges, for the run-time scan for code pointers the "
                         "metadata misses (a program that links everything it "
                         "names: the boot)")
    ap.add_argument("--arm9-image",
                    help="the player's extract/arm9_dec.bin, for the linker veneers "
                         "and adjustor thunks the listing would name (optional)")
    ap.add_argument("--no-listing", action="store_true",
                    help="ignore asm/ listings (data_objects.json only)")
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 on any warning or unresolved name")
    args = ap.parse_args(argv)
    paths = list(args.source) + list(args.bind_source)
    for lst in args.sources:
        with open(lst, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if line and line.endswith(".c"):
                    paths.append(line)
    seen = set()
    args.source_paths = [p for p in paths if not (p in seen or seen.add(p))]
    m = Model(args)
    if args.out_h:
        write_header(m, args.out_h)
    if args.out_c:
        write_c(m, args.out_c)
    if args.summary:
        summary(m, sys.stdout)
    if args.strict and (m.warnings or m.unresolved):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
