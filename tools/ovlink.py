#!/usr/bin/env python3
"""
ovlink.py -- compile the game's ARM9 overlays into one host executable.

On the DS an overlay is a file copied to a fixed RAM window when the game
asks for it; several overlays share one window and the linker gave each its
own symbols, so `func_02155244` is one function in ov009 and another in
ov010 (aletheia's metadata repeats about a thousand names this way).  A host
executable has one global namespace and no windows.  This tool works out,
from aletheia's metadata and the build's own sources, what it takes to put
a set of overlays into one link anyway (docs/overlays.md is the design):

 1. A namespace per overlay.  Every external name an overlay's sources
    define, and every name they use whose address lies inside the overlay's
    own window, is renamed to `ovNNN_<name>` by a forced-include header
    (`#define func_02155244 ov010_func_02155244`).  The matched files are
    not edited (portability rule 1).  A spelling that differs from the
    defining file's (an SDK name for a func_ address) is renamed to the
    definition by address, inside the overlay as across to ARM9.  A unit
    that spells one function twice (the ARM9's name and its own alias)
    declares that one name twice, and the two prototypes conflict: the
    spelling the unit declares itself keeps a name of its own, and a
    forwarder calls the target with the unit's own arguments.

 2. Calls into a window from outside it.  A name whose address lies in a
    window the referring code does not own (ARM9 code calling 0x021DFC20,
    one overlay calling another) keeps its plain spelling, and the plain
    name is a generated trampoline: it asks the overlay registry which
    overlay is resident over the address and calls that overlay's function,
    or panics naming what is resident.  Code pointers stored in data take
    the same route.

 3. The overlay's data.  The image is copied to its window in the port's
    main RAM at load (sdk/overlay_image.c), as the ARM9 image is at reset
    (docs/armdata.md): every data name an overlay's sources use becomes a
    pointer variable private to that overlay (`#define d_021BD8E8
    (*bw_dsref_ov088_d_021BD8E8)`), bound when the overlay is loaded, and
    every pointer word the metadata lists in the image is relocated then.

 4. What the build lacks.  Every function known in a compiled overlay that
    no compiled source defines (unmatched, asm-bodied, excluded) is a stub
    that panics naming the overlay and the address, so a partly matched
    overlay loads and runs until it reaches one.  Optionally (--arm9-stubs)
    ARM9 names the overlays call and the link does not define get the same
    treatment, for a link without the boot.

 5. Implicit declarations and mwcc builtins.  A call with no declaration in
    scope gets C89's `int name();` (64-bit returns excepted), as
    tools/bootlink.py does for the boot; `__builtin__clear` and `__memcpy`
    map to port functions.

 6. Code addresses written as literals.  `(void *)0x021B6279` in an overlay
    source is a Thumb function's address passed as a callback; the dsaddr
    rewrite turns it into bw_ds_ptr, which would hand out the copied code
    bytes.  A source that casts a known function address gets a translator
    in place of bw_ds_ptr that answers the host function for those
    addresses and bw_ds_ptr for everything else.

  7. A declared function the unit that defines it calls.  The call binds by
     name and does not pass the slot (docs/mod-interface.md, section 2), so
     the row's event does not fire there.  The definition is cut out of that
     unit (the cut the port already uses for the functions it supplies
     itself, tools/gamecut.py) and put in a unit of its own, and the unit's
     calls reach the slot like every other caller's do.  The text is the
     game's own.

Run once at configure time (cmake/overlays.cmake):

    python tools/ovlink.py --aletheia-root R --overlays "9;10;88" \\
        --link-sources lib_sources.txt --out-dir build/ovl/boot [--arm9-stubs]

Writes, under --out-dir (rewritten only when they change):

    ovl_units.txt        one line per translation unit: id|source|header
    include/ovNNN.h      the overlay's renames and data bindings
    include/tu/*.h       one forced-include header per translation unit
    ovNNN_link.c         the overlay's stubs, data image, code-literal
                         translator and static initialisers
    ovl_dispatch.c       the trampolines and the table of compiled overlays
    ovl_arm9_stubs.c     ARM9 names the link lacks (empty without --arm9-stubs)
    ovl_arm9_needs.txt   every ARM9 name the overlays use, and whether the
                         link defines it
    ovl_report.txt       counts and every decision worth a second look

Reads only metadata and C sources, never game bytes: layout/layout.json,
layout/matched/*/*.json, layout/symbols_sdk.txt, layout/data_objects.json,
asm/overlays/ovNNN.s (only the `-> symbol` annotations and the `.global`
labels), aletheia's src/matched/ and the port's own sources.

Standard library only.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdata          # noqa: E402  (name_address, SourceFile, exclusions)
import overlay_table    # noqa: E402  (KNOWN_SINIT)
import closure          # noqa: E402  (listing_veneers)
import gametree         # noqa: E402  (pair_members)

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXCLUSIONS = os.path.join(EIDOS_ROOT, "tools", "armdata_exclusions.txt")
DEFAULT_OV_EXCLUSIONS = os.path.join(EIDOS_ROOT, "tools", "ovlink_exclusions.txt")

# A data name below main RAM is bound to its address as a value: the SDK's
# linker constants (armdata.py's CONST_LIMIT), and the ITCM, whose code the
# overlays name only for its address (`&OS_IrqHandler` as a sentinel).  The
# same value tools/bootlink.py gives such a name in the boot.
CONST_LIMIT = 0x02000000
ADDR_FUNC = re.compile(r"^(?:[A-Za-z0-9]+_)?(?:func|sub|unit|veneer)_([0-9A-Fa-f]{8})$")

# The title path (docs/title-closure.md): boot, save check and title menu,
# opening cinematic.
TITLE_SET = [9, 10, 11, 13, 17, 228, 88, 179]

# Standard C library functions the overlays call (the ARM9's MSL copies on
# the DS); on the host the C library provides them, so they are never
# stubbed.
HOST_LIBC = {
    "memcpy", "memset", "memmove", "memcmp", "memchr", "strlen", "strcpy",
    "strncpy", "strcmp", "strncmp", "strchr", "strrchr", "strstr", "strcat",
    "strncat", "strtol", "strtoul", "atoi", "abs", "labs", "sprintf",
    "snprintf", "vsprintf", "vsnprintf", "sscanf", "qsort", "bsearch",
    "toupper", "tolower", "isdigit", "isalpha", "isspace", "isxdigit",
    "isupper", "islower", "isalnum", "isprint",
}

# mwcc builtins the matched files call without a declaration, and the port
# function each one becomes (sdk/overlay_image.c).
MWCC_BUILTINS = {
    "__builtin__clear": ("bw_mwcc_clear", "void bw_mwcc_clear(void *dst, unsigned int size);"),
    "__memcpy": ("bw_mwcc_memcpy",
                 "void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size);"),
}


# ---------------------------------------------------------------------------
# Reading one C source
# ---------------------------------------------------------------------------
#
# The same reading tools/closure.py's SourceScan does on the boot-link
# branch: what a file defines (and with which linkage), declares, uses and
# calls.  A declared name counts only when the file uses it outside its own
# declaration.

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
MEMBER = re.compile(r"(?:\.|->)\s*[A-Za-z_]\w*")
ASM_BODY = re.compile(r"\b(?:asm|__asm)\b")
ATTRIBUTE = re.compile(r"\b__attribute__\s*\(")
TAG_BODY = re.compile(r"\b(struct|union|enum)\b(\s+[A-Za-z_]\w*)?\s*\{")
TAG_NAME = re.compile(r"\b(struct|union|enum)\s+(?!_tag\b)[A-Za-z_]\w*")
LOCAL_DECL = re.compile(r"\b([A-Za-z_]\w*)\s+\**\s*([A-Za-z_]\w*)\s*(?=[=;,\[)])")
FUNC_PTR_DECL = re.compile(r"\(\s*\*+\s*(?:const\s+|volatile\s+)*([A-Za-z_]\w*)\s*[\)\[]")
RET_PTR_DECL = re.compile(r"\(\s*\*+\s*([A-Za-z_]\w*)\s*\(")
# A declaration inside a function body: `void func_02156730(void);`.
BLOCK_PROTO = re.compile(
    r"(?:(?<=[;{}])|^)\s*(?:extern\s+)?((?:(?:const|volatile|unsigned|signed|struct|union|enum)\s+)*"
    r"[A-Za-z_]\w*)\s*(\**)\s*\(?\s*\**\s*([A-Za-z_]\w*)\s*\(([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*)\)"
    r"\s*\)?\s*(?:\[[^\]]*\])?\s*;")
SYMBOLS_HDR = re.compile(r"^\s*\*\s*symbols:\s*$")
SYMBOL_ROW = re.compile(
    r"^\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]{1,8})\s+(thumb|arm|data)\b(.*)$")
LITERAL_CAST = re.compile(
    r"\(\s*(?:const\s+|volatile\s+)*[A-Za-z_][\w\s]*?\*+\s*\)\s*\(?\s*(0x0?2[0-9A-Fa-f]{6,7})[uUlL]*")

NOT_TYPES = {"return", "case", "goto", "else", "do", "sizeof", "typedef"}

C_RESERVED = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "asm", "__asm", "__attribute__",
    "__inline", "_Bool", "defined", "nofralloc", "naked",
}


def strip_source(text):
    """Comments, string and character literals blanked; line splices joined."""
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _matching(text, i, open_ch, close_ch):
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def _drop_attributes(text):
    while True:
        m = ATTRIBUTE.search(text)
        if not m:
            return text
        end = _matching(text, m.end() - 1, "(", ")")
        text = text[:m.start()] + " " + text[end:]


def _split_top(text, sep):
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return parts


import alcache  # noqa: E402  (parse caches)

_GAMECUT_PY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gamecut.py")
_ARMDATA_PARSES = alcache.ParseCache("armdata-sourcefile", [armdata.__file__, _GAMECUT_PY])
_SCANS = alcache.ParseCache("ovlink-scan", [os.path.abspath(__file__), armdata.__file__,
                                             _GAMECUT_PY])


_INCLUDE_RE = re.compile(r'^[ 	]*#[ 	]*include[ 	]+"([^"]+)"', re.M)
_INC_TYPEDEFS = {}


def _headers_need_rewrite(paths):
    """Whether one of the headers, or of the headers it includes, is a
    header the build rewrites.

    A header that holds a fixed DS address is copied next to the copy of
    the unit that includes it and rewritten there (tools/dsaddr.py,
    _rewrite_local_headers).  The moved unit names the tree's headers by
    their own path, so it must not name such a header: the rewrite would
    land in the tree, which the port never edits."""
    import dsaddr
    seen = set()
    stack = list(paths)
    while stack:
        path = stack.pop()
        key = os.path.normcase(os.path.abspath(path))
        if key in seen or not os.path.isfile(path):
            continue
        seen.add(key)
        text = read_text(path)
        try:
            rewrites, _residue = dsaddr.analyse(text)
        except Exception:               # a header the tool cannot read
            rewrites = []
        if rewrites:
            return True
        base = os.path.dirname(os.path.abspath(path))
        for inc in _INCLUDE_RE.findall(text):
            stack.append(os.path.normpath(os.path.join(base, inc)))
    return False


def unit_prelude(path, text, upto):
    """The part of a unit that its own text needs: the headers it includes
    and the typedefs it declares, in the order the unit has them.

    A unit that is moved out is compiled on its own, so it must carry what
    its own unit had: the tree's style puts a typedef between two includes
    (the SDK's OSIntrMode before the header that uses it), and dropping it
    leaves the moved unit without the type.

    The headers are named by their own path in the tree, as the moved unit
    is compiled in the build directory.  Returns None when a header the unit
    includes is not in the tree.
    """
    base = os.path.dirname(os.path.abspath(path))
    out = []
    i = 0
    while i < upto:
        m = re.match(r"[ \t]*typedef\b", text[i:])
        if m:
            # The declaration ends at the semicolon that closes it, which
            # for a struct is not the first one (the tree's own style puts
            # the body on one line).
            j = i
            depth = 0
            while j < upto:
                c = text[j]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                elif c == ";" and depth <= 0:
                    break
                j += 1
            if j >= upto or j >= len(text) or text[j] != ";":
                break
            out.append(text[i:j + 1].strip())
            i = j + 1
            continue
        m = re.match(r'[ \t]*#[ \t]*include[ \t]+"([^"]+)"', text[i:])
        if m:
            inc = m.group(1)
            hp = os.path.normpath(os.path.join(base, inc))
            if not os.path.isfile(hp):
                return None
            out.append('#include "%s"' % hp.replace("\\", "/"))
            i += m.end()
            continue
        j = text.find("\n", i)
        if j < 0:
            break
        i = j + 1
    return out


def include_typedefs(path, text):
    """The typedef names of the headers `text` includes with quotes, found
    beside `path`, and of theirs: src/game keeps u8, BOOL and the SDK types
    in shared headers (types.h, sdk_os.h), and a name the scanner does not
    know as a type reads as a call (`u8 (*fn)(...)`, a cast)."""
    out = {}
    for inc in _INCLUDE_RE.findall(text):
        hp = os.path.normcase(os.path.normpath(os.path.join(os.path.dirname(path), inc)))
        if hp not in _INC_TYPEDEFS:
            _INC_TYPEDEFS[hp] = {}          # a cycle stops here
            if os.path.isfile(hp):
                _INC_TYPEDEFS[hp] = dict(Scan(hp, read_text(hp)).typedefs)
        out.update(_INC_TYPEDEFS[hp])
    return out


def include_signature(path, text):
    """Which headers `text` includes and when they changed, for a parse
    cache key (the typedefs above come from them)."""
    sig = []
    for inc in _INCLUDE_RE.findall(text):
        hp = os.path.normpath(os.path.join(os.path.dirname(path), inc))
        try:
            st = os.stat(hp)
            sig.append("%s:%d:%d" % (hp, st.st_size, st.st_mtime_ns))
        except OSError:
            sig.append(hp)
    return "|".join(sig)


class Scan:
    """What one source defines, declares, uses and calls."""

    def __init__(self, path, text):
        import gamecut
        text = gamecut.knr_to_ansi(text)     # old-style definitions, see gamecut
        text = gamecut.host_branches(text)   # the host side of #ifdef __MWERKS__
        self.path = path
        self.symbols = {}          # name -> (addr, kind)
        self.defined = set()       # functions and objects with a body or storage here
        self.statics = set()
        self.declared = set()      # extern or prototype, not static
        self.prototyped = set()    # declared as a function
        self.used = set()
        self.called = set()
        self.macros = set()
        self.typedefs = {}         # name -> the type text
        self.locals = set()
        self.heads = {}            # defined function -> its declarator text
        self.decls = {}            # declared function -> its declaration text
        self.func_defs = set()     # names defined with a body
        self.block_protos = set()  # functions declared inside a body
        self.prototyped_static = set()
        self._parse_symbols(text)
        code = strip_source(text)
        self.has_asm = bool(ASM_BODY.search(code))
        self.literals = set(int(v, 16) for v in LITERAL_CAST.findall(code))
        # Tagged struct and union bodies (ov093's `union SickCont { ... };`,
        # no typedef), as typedef-shaped text under a key no name can have,
        # so _norm_type can tell a one-word aggregate passed by value.
        for m in re.finditer(r"\b(struct|union)\s+([A-Za-z_]\w*)\s*\{", code):
            depth, i = 0, m.end() - 1
            while i < len(code):
                if code[i] == "{":
                    depth += 1
                elif code[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            key = " %s %s" % (m.group(1), m.group(2))
            self.typedefs.setdefault(key, "typedef %s %s %s" % (
                m.group(1), m.group(2), code[m.end() - 1:i + 1]))
        # Types from the headers the file includes (src/game's types.h).
        for n, t in include_typedefs(path, text).items():
            self.typedefs.setdefault(n, t)
        self._scan(code)
        # stubs/stubs.h's BW_STUB(name, address, why) defines `name`.
        for m in re.finditer(r"\bBW_STUB\s*\(\s*([A-Za-z_]\w*)", code):
            self.defined.add(m.group(1))
            self.func_defs.add(m.group(1))
        # armdata's reading of the same file: which names are used as a
        # member, a tag, a local or in a static initialiser.
        sf = _ARMDATA_PARSES.get(("armdata.SourceFile", path, True), text,
                                 lambda: armdata.SourceFile(path, text, True))
        self.conflicts = {n for n in sf.identifiers
                          if n in sf.members or n in sf.init_names or n in sf.shadowed}

    def _parse_symbols(self, text):
        in_block = False
        for line in text.splitlines():
            if SYMBOLS_HDR.match(line):
                in_block = True
                continue
            if in_block:
                m = SYMBOL_ROW.match(line)
                if m:
                    self.symbols[m.group(1)] = (int(m.group(2), 16), m.group(3))
                elif line.strip().startswith("*/") or not line.strip().startswith("*"):
                    in_block = False

    def _use(self, text):
        text = MEMBER.sub(" ", text)
        self.used.update(IDENT.findall(text))
        self.called.update(CALL.findall(text))

    def _scan(self, code):
        body_lines = []
        for line in code.split("\n"):
            s = line.lstrip()
            if s.startswith("#"):
                m = re.match(r"#\s*define\s+([A-Za-z_]\w*)(\([^)]*\))?(.*)", s)
                if m:
                    self.macros.add(m.group(1))
                    self._use(m.group(3))
                body_lines.append("")
            else:
                body_lines.append(line)
        code = _drop_attributes("\n".join(body_lines))
        i, n, start = 0, len(code), 0
        while i < n:
            c = code[i]
            if c == "(":
                i = _matching(code, i, "(", ")")
                continue
            if c == "[":
                i = _matching(code, i, "[", "]")
                continue
            if c == "{":
                head = code[start:i]
                stripped = head.rstrip()
                tag = TAG_BODY.search(head + "{")
                is_tag = tag is not None and tag.end() == len(head) + 1
                if stripped.endswith(")") and not is_tag \
                        and "=" not in _split_top(head, ",")[-1]:
                    end = _matching(code, i, "{", "}")
                    self._function(head, code[i:end])
                    i = start = end
                    continue
                i = _matching(code, i, "{", "}")
                continue
            if c == ";":
                self._declaration(code[start:i])
                start = i + 1
            i += 1

    def _function(self, head, body):
        head = head.strip()
        name = None
        m = FUNC_PTR_DECL.search(head)
        if m and head.find("(") == m.start():
            name = m.group(1)
        else:
            words = IDENT.findall(head.split("(")[0])
            if words:
                name = words[-1]
        if name:
            self.defined.add(name)
            self.func_defs.add(name)
            self.heads[name] = " ".join(head.split())
            if re.search(r"\bstatic\b", head.split("(")[0]):
                self.statics.add(name)
        if "(" in head:
            params = head[head.find("(") + 1:head.rfind(")")]
            for param in _split_top(params, ","):
                fp = FUNC_PTR_DECL.search(param)
                idents = [w for w in IDENT.findall(re.sub(r"\[[^\]]*\]", " ", param))
                          if w not in C_RESERVED]
                if fp:
                    self.locals.add(fp.group(1))
                elif idents:
                    self.locals.add(idents[-1])
        for bm in BLOCK_PROTO.finditer(body):
            typ, stars, bname = bm.group(1), bm.group(2), bm.group(3)
            last = typ.split()[-1]
            if last in NOT_TYPES:
                continue
            if stars or last in C_RESERVED or last in self.typedefs or last in INT_TYPES:
                self.block_protos.add(bname)
        self.locals.update(fp.group(1) for fp in FUNC_PTR_DECL.finditer(body))
        for typ, var in LOCAL_DECL.findall(body):
            if typ in self.typedefs or typ in C_RESERVED:
                self.locals.add(var)
        self._use(body)

    def _declaration(self, decl):
        decl = decl.strip()
        if not decl:
            return
        raw = decl
        pos = 0
        while True:
            m = TAG_BODY.search(decl, pos)
            if not m:
                break
            end = _matching(decl, m.end() - 1, "{", "}")
            repl = " %s _tag " % m.group(1)
            decl = decl[:m.start()] + repl + decl[end:]
            pos = m.start() + len(repl)
        decl = TAG_NAME.sub(lambda t: t.group(1) + " _tag", decl)
        head_words = IDENT.findall(_split_top(decl, "=")[0].split("(")[0])
        is_typedef = "typedef" in head_words
        is_extern = "extern" in head_words
        is_static = "static" in head_words
        # The declaration's type words, for a declarator that carries none
        # (`extern void f(void), g(void);`): the type belongs to the first.
        decl_type = None
        for piece in _split_top(decl, ","):
            parts = _split_top(piece, "=")
            declarator = parts[0]
            if len(parts) > 1:
                self._use("=".join(parts[1:]))
            m = FUNC_PTR_DECL.search(declarator)
            if m and m.start() != declarator.find("("):
                m = None
            ret_ptr = RET_PTR_DECL.search(declarator)
            if ret_ptr and ret_ptr.start() != declarator.find("("):
                ret_ptr = None
            if ret_ptr:
                # `T (*name(params))[6]`: a function returning a pointer.
                name, is_func = ret_ptr.group(1), True
            elif m:
                name, is_func = m.group(1), False
            elif "(" in declarator:
                idents = IDENT.findall(declarator.split("(")[0])
                name, is_func = (idents[-1] if idents else None), True
            else:
                idents = [w for w in IDENT.findall(re.sub(r"\[[^\]]*\]", " ", declarator))
                          if w not in C_RESERVED]
                name, is_func = (idents[-1] if idents else None), False
            if not name or name in C_RESERVED or name == "_tag":
                continue
            for bound in re.findall(r"\[([^\]]*)\]", declarator):
                self._use(bound)
            if is_typedef:
                self.typedefs[name] = raw
            elif is_static:
                self.statics.add(name)
                if is_func:
                    self.prototyped_static.add(name)
                else:
                    self.defined.add(name)
            elif is_extern or is_func:
                self.declared.add(name)
                if is_func:
                    self.prototyped.add(name)
                    # The declaration's own text: the prototype the file
                    # declares the name with.  A unit that declares a name
                    # with its own prototype keeps it (Link._own_names).
                    pre = IDENT.findall(_split_top(declarator, "(")[0])
                    typew = [w for w in pre[:-1]
                             if w not in ("extern", "static", "typedef", "inline",
                                          "__inline", "register")]
                    if typew and decl_type is None:
                        decl_type = typew
                    head = declarator
                    if not typew and decl_type:
                        head = " ".join(decl_type) + " " + declarator
                    self.decls[name] = head
            else:
                self.defined.add(name)

    def exported(self):
        """Names this file gives external linkage."""
        return self.defined - self.statics

    def references(self, universe):
        """(names needed from elsewhere, implicit calls)."""
        local = self.defined | self.statics | self.macros | set(self.typedefs) | C_RESERVED
        refs = (self.declared & self.used) - local
        refs |= {n for n in self.symbols if n in self.used and n not in local}
        block = (self.block_protos & self.used) - local
        implicit = {name for name in self.called - self.declared - self.block_protos
                    - local - self.locals if universe(name)}
        return refs | block | implicit, implicit

    def static_undefined(self):
        """Static functions this file declares and uses but does not define:
        mwcc's link resolved them against another file's definition; a host
        compiler cannot (an internal symbol with no body)."""
        return {n for n in self.prototyped_static
                if n not in self.func_defs and n in self.used}

    def is_code(self, name):
        sym = self.symbols.get(name)
        if sym is not None:
            return sym[1] != "data"
        if name in self.prototyped or name in self.func_defs or name in self.block_protos:
            return True
        if name in self.declared:
            return False            # an object, perhaps a function pointer
        return name in self.called


# Port-side macros the build decides (--define), for the `#if
# EIDOS_HEAP_MATCHED` blocks in sdk/ that decide which names the link has;
# the same reading tools/bootlink.py does.
PORT_DEFINES = {}
PP_IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _pp_value(kind, expr):
    """True/False for a conditional PORT_DEFINES decides, None otherwise."""
    expr = expr.split("/*")[0].split("//")[0].strip()
    if kind in ("ifdef", "ifndef"):
        if expr not in PORT_DEFINES:
            return None
        return kind == "ifdef"
    neg = expr.startswith("!")
    name = expr[1:].strip() if neg else expr
    if name in PORT_DEFINES:
        value = PORT_DEFINES[name] not in ("0", "")
        return not value if neg else value
    return None


def preprocess(text):
    """Blank the branches of the conditionals PORT_DEFINES decides."""
    out = []
    stack = []                 # [this branch live, a branch taken, decided]
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


def read_text(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def read_list(path):
    out = []
    if not path or not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                out.append(line.replace("\\", "/"))
    return out


def _word_aggregate_defs(text):
    """The bw_word_<Name> types `text` uses, defined: the real struct or
    union under wasm32 (WordAggregate), unsigned int elsewhere."""
    used = sorted(set(re.findall(r"\bbw_word_(\w+)", text)) & set(WORD_AGGREGATES))
    if not used:
        return ""
    out = ["/* One-word structs and unions by value (tools/ovlink.py, WordAggregate). */"]
    for n in used:
        body = " ".join(WORD_AGGREGATES[n].split())
        body = body[body.find("{"):body.rfind("}") + 1]
        kind = "union" if re.match(r"^\s*typedef\s+union\b", WORD_AGGREGATES[n]) else "struct"
        out += ["#ifndef BW_WORD_%s" % n,
                "#define BW_WORD_%s" % n,
                "#if defined(__wasm__)",
                "typedef %s %s bw_word_%s;" % (kind, body, n),
                "static inline unsigned int bw_word_%s_bits(bw_word_%s v)" % (n, n),
                "{ unsigned int w; __builtin_memcpy(&w, &v, 4); return w; }",
                "static inline bw_word_%s bw_word_%s_of(unsigned int w)" % (n, n),
                "{ bw_word_%s v; __builtin_memcpy(&v, &w, 4); return v; }" % n,
                "#else",
                "typedef unsigned int bw_word_%s;" % n,
                "#define bw_word_%s_bits(v) (v)" % n,
                "#define bw_word_%s_of(w) (w)" % n,
                "#endif",
                "#endif"]
    return "\n".join(out) + "\n"


def write_if_changed(path, text):
    defs = _word_aggregate_defs(text)
    if defs:
        # after the file's last #include, before anything uses them; an
        # #include inside an #if block counts as the block's #endif, so the
        # definitions never land inside a conditional
        lines = text.split("\n")
        at, depth, inner = -1, 0, False
        for k, l in enumerate(lines):
            d = l.lstrip()
            if d.startswith("#if"):
                depth += 1
            elif d.startswith("#endif") and depth > 0:
                depth -= 1
                if depth == 0 and inner:
                    at, inner = k, False
            elif d.startswith("#include"):
                if depth == 0:
                    at, inner = k, False
                else:
                    inner = True
        lines.insert(at + 1, defs)
        text = "\n".join(lines)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as fh:
            if fh.read() == text:
                return
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def hx(v):
    return "0x%08X" % v


# The declared hookable set: mod/declared_set.txt (docs/mod-interface.md,
# section 1).  The overlay rows get a slot here; the arm9 rows are
# tools/bootlink.py's.
DECLARED_SET = os.path.join(EIDOS_ROOT, "mod", "declared_set.txt")


def read_declared_set():
    """Rows of mod/declared_set.txt: (address, name, region, events).  `#`
    starts a comment; a line is tab separated.  The fourth column names the
    stable events the port binds to the function (docs/mod-events.md,
    section 1); it is empty for a raw-hook-only row, and it may name two
    events (catch.attempt,catch.result)."""
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
            events = []
            if len(parts) > 3:
                events = [e.strip() for e in parts[3].split(",") if e.strip()]
            rows.append((int(parts[0], 16), parts[1], parts[2], events))
    return rows


# ---------------------------------------------------------------------------
# Signatures for the trampolines
# ---------------------------------------------------------------------------
#
# A trampoline forwards its arguments, so it needs a prototype.  It is taken
# from the definition of the function it forwards to and normalised to
# types every host passes the same way: any pointer (data or function) is
# `void *`, every integer typedef the matched files use is its C type.  A
# signature with a struct passed by value, a float or `...` is not
# normalised; its trampoline panics instead of forwarding.
#
# The game's own type names (a typedef in one of the game's headers) are
# resolved the same way, from the typedefs the file carries: an enum is an
# int, a struct or union by value is the one word the port carries it in, a
# pointer and a function pointer are `void *`.  The port's own normaliser
# cannot read the game's headers (it is a separate program), so the
# generator publishes the resolution of every name it meets
# (Link.game_types) and the port reads the published table
# (docs/mod-overrides.md, "The prototype is checked").

INT_TYPES = {
    "u8": "unsigned char", "s8": "signed char", "vu8": "unsigned char", "vs8": "signed char",
    "u16": "unsigned short", "s16": "short", "vu16": "unsigned short", "vs16": "short",
    "u32": "unsigned int", "s32": "int", "vu32": "unsigned int", "vs32": "int",
    "u64": "unsigned long long", "s64": "long long", "fx64": "long long",
    "fx32": "int", "fx16": "short", "BOOL": "int", "bool": "int", "_Bool": "int",
    "size_t": "unsigned int", "OSTick": "unsigned long long",
    # the integers that hold an address (aletheia types.h): a word on this
    # 32-bit host, as size_t is; types.h has two typedefs for them, one per
    # target, so the typedef table cannot resolve them
    "uptr": "unsigned int", "sptr": "int",
}
BASE_WORDS = {"unsigned", "signed", "char", "short", "int", "long", "void"}

# The manifest layer's limit: how many type names its table holds.  It is
# sdk/mod_manifest.c's BW_MOD_TYPES_MAX, and the two must agree: the
# generator publishes at most this many names and the port keeps at most
# this many.  A name that does not fit is not read, so a stated prototype
# that names it is reported and not compared (there is no false refusal).
BW_MOD_TYPES_MAX = 4096


_WORD_SCALARS = {"u32", "s32", "int", "unsigned", "BOOL", "fx32", "vu32", "vs32"}


class TypeNames(dict):
    """The game's own type names, and the type each resolves to.

    A name two files resolve differently is a conflict: the port reads one
    answer, so the generator says so and keeps the name out of the table
    (Link.game_types)."""

    def __init__(self):
        super().__init__()
        self.conflicts = {}

    def __setitem__(self, name, type_):
        old = self.get(name)
        if old is not None and old != type_:
            self.conflicts[name] = (old, type_)
        super().__setitem__(name, type_)


def _word_aggregate(body):
    """True when a struct or union body is one 32-bit word: a union whose
    members are 32-bit scalars or structs of bitfields of at most 32 bits
    (ov093's BtlvSlot, the battle's packed condition word), or such a
    struct.  Passed by value, it travels as an unsigned int does in the
    host's calling convention, so a trampoline can carry it as one."""
    inner = body[body.find("{") + 1:body.rfind("}")]
    members = [m.strip() for m in _split_top(inner, ";") if m.strip()]
    if not members:
        return False
    has_word = False
    for m in members:
        if "{" in m:
            fields = [f.strip() for f in m[m.find("{") + 1:m.rfind("}")].split(";") if f.strip()]
            bits = 0
            for f in fields:
                fm = re.match(r"^(?:unsigned\s+int|u32|s32|int|unsigned)\s+\w*\s*:\s*(\d+)$", f)
                if not fm:
                    return False
                bits += int(fm.group(1))
            if bits > 32:
                return False
            has_word = has_word or bits > 16
        else:
            fm = re.match(r"^(\w+)\s+\w+$", m)
            if not fm or fm.group(1) not in _WORD_SCALARS:
                return False
            has_word = True
    return has_word


class WordAggregate(str):
    """A one-word struct or union by value, normalised: it compares as
    "unsigned int" (what i686 and ARM pass and return it as, so every rule
    below treats it as one), but prints as bw_word_<Name>, which
    write_if_changed defines.  The wasm32 C ABI passes and returns an
    aggregate through memory, not as a word, so there the generated code
    must use the real type for its calls to agree with the game's."""

    def __new__(cls, name, body):
        o = str.__new__(cls, "unsigned int")
        o.name = name
        o.body = body
        WORD_AGGREGATES[name] = body
        return o

    def __str__(self):
        return "bw_word_%s" % self.name

    def __format__(self, spec):
        return format(str(self), spec)


# name -> the typedef text of every WordAggregate made
WORD_AGGREGATES = {}


def zero_of(t):
    """A zero of normalised type `t`, as an expression."""
    if isinstance(t, WordAggregate):
        return "bw_word_%s_of(0u)" % t.name
    return "0"


def word_bits(t, expr):
    """`expr`, of normalised type `t`, as the unsigned int a cast can take:
    a WordAggregate's 32 bits, anything else as it is."""
    if isinstance(t, WordAggregate):
        return "bw_word_%s_bits(%s)" % (t.name, expr)
    return expr


def word_cast(t, expr):
    """`(t)expr` for a word `expr`; a WordAggregate is rebuilt from its
    bits (C has no cast to a struct or union)."""
    if isinstance(t, WordAggregate):
        return "bw_word_%s_of((unsigned int)(%s))" % (t.name, expr)
    return "(%s)%s" % (t, expr)


def _norm_type(text, typedefs, depth=0):
    """C type text (no declarator name) -> normalised spelling, or None.

    This is the rule a function's own prototype is normalised by: a struct
    or a union by value is normalised only when the port carries it in one
    word, so a function that takes a bigger one has no prototype at all
    (and so no slot).  The game's own type names are read the way the port
    reads a type spelled out (_name_type), which is the table the port's
    normaliser asks."""
    t = re.sub(r"\b(?:const|volatile|register|static|extern|inline|__inline)\b", " ", text)
    if "*" in t or "[" in t or "(" in t:
        return "void *"
    words = IDENT.findall(t)
    if not words:
        return None
    if words[0] == "enum":
        return "int"
    if words[0] in ("struct", "union"):
        # `union SickCont cont`: the body under that tag, from a typedef
        # that names it, decides.
        if len(words) == 2:
            for td in typedefs.values():
                if re.match(r"^\s*typedef\s+%s\s+%s\s*\{" % (words[0], re.escape(words[1])), td) \
                        and _word_aggregate(td):
                    return WordAggregate(words[1], td)
        return None
    if all(w in BASE_WORDS for w in words):
        s = set(words)
        if words == ["void"]:
            return "void"
        n_long = words.count("long")
        uns = "unsigned" in s
        if "char" in s:
            return "unsigned char" if uns else ("signed char" if "signed" in s else "char")
        if "short" in s:
            return "unsigned short" if uns else "short"
        if n_long >= 2:
            return "unsigned long long" if uns else "long long"
        if n_long == 1:
            return "unsigned long" if uns else "long"
        return "unsigned int" if uns else "int"
    if len(words) == 1:
        w = words[0]
        if w in INT_TYPES:
            return INT_TYPES[w]
        td = typedefs.get(w)
        if td and depth < 8:
            # A typedef of a struct or a union carries its own body, and the
            # body is not a declarator: a '*' inside it does not make the
            # type a pointer.  The body decides (a struct or a union by
            # value is the one word the port carries it in), and a body of
            # more than one word is not normalised at all.
            if re.match(r"^\s*typedef\s+(?:struct|union)\b", td) and "{" in td:
                if not _word_aggregate(td):
                    return None
                return WordAggregate(w, td)
            m = re.match(r"^\s*typedef\s+(.*?)\b" + re.escape(w) + r"\s*$", " ".join(td.split()))
            if m:
                r = _norm_type(m.group(1), typedefs, depth + 1)
                if r is not None:
                    return r
            if re.search(r"\(\s*\*\s*" + re.escape(w) + r"\s*\)", td):
                return "void *"
            if re.match(r"^\s*typedef\s+(?:struct|union)\b[^{;]*\{", td) and _word_aggregate(td):
                return WordAggregate(w, td)
        return None
    return None


# The C types the port's own normaliser produces (sdk/mod_manifest.c's
# norm_type): a type the table publishes must be one of them, so the two
# sides' answers have the same shape.
PORT_TYPES = {
    "void", "char", "signed char", "unsigned char", "short", "unsigned short",
    "int", "unsigned int", "long", "unsigned long", "long long",
    "unsigned long long", "void *",
}


def _name_type(name, typedefs, depth=0):
    """The C type a typedef name stands for, the way the port's own
    normaliser reads a type spelled out (sdk/mod_manifest.c's norm_type):
    an enum is an int, a struct or a union by value is the one word the port
    carries it in (unsigned int), a pointer and a function pointer are
    void *, and the game's integer typedefs are their C types.

    This is the table's rule (Link.game_types).  A row's own prototype is
    the stricter one (_norm_type): a struct or a union of more than one word
    is not normalised at all, because the port cannot carry it, so the row
    has no prototype and there is nothing to compare.  The two never meet:
    a row that carries a prototype never names such an aggregate.
    """
    if name in INT_TYPES:
        return INT_TYPES[name]
    td = typedefs.get(name)
    if td is None or depth >= 8:
        return None
    if re.match(r"^\s*typedef\s+(?:struct|union)\b[^;{]*\{", td):
        # A struct or a union by value: the body is not a declarator, so a
        # '*' or a '[' inside it does not make the type a pointer, and the
        # port carries the aggregate in one word.
        return "unsigned int"
    if re.search(r"\(\s*\*\s*" + re.escape(name) + r"\s*\)", td):
        return "void *"                    # a function pointer
    if "[" in td:
        return "void *"                    # an array
    m = re.match(r"^\s*typedef\s+(.*?)\b" + re.escape(name) + r"\s*$",
                 " ".join(td.split()))
    if m is None:
        return None
    text = re.sub(r"\b(?:const|volatile|register|static|extern|inline|__inline)\b",
                  " ", m.group(1))
    if "*" in text or "(" in text or "[" in text:
        return "void *"                    # a pointer
    words = IDENT.findall(text)
    if not words:
        return None
    if words[0] == "enum":
        return "int"
    if words[0] in ("struct", "union"):
        return "unsigned int"
    if len(words) == 1 and words[0] not in BASE_WORDS:
        return _name_type(words[0], typedefs, depth + 1)
    r = _norm_type(text, typedefs)          # the base types
    if r is None or r not in PORT_TYPES:
        return None                        # a float, and the like
    return r


def _strip_declarator_name(param):
    """'const u8 *buf' -> 'const u8 *'; 'int' -> 'int'."""
    p = re.sub(r"\[[^\]]*\]", "[]", param.strip())
    if FUNC_PTR_DECL.search(p) or "(" in p:
        return "void *"
    words = IDENT.findall(p)
    if len(words) >= 2 and words[-1] not in BASE_WORDS and words[-1] not in INT_TYPES \
            and words[-1] not in ("const", "volatile"):
        k = p.rfind(words[-1])
        p = p[:k] + p[k + len(words[-1]):]
    return p


def signature(head, name, typedefs):
    """(return type, [param types]) of a definition head, normalised, or None."""
    i = head.find(name + "(")
    if i < 0:
        m = re.search(r"\b" + re.escape(name) + r"\s*\(", head)
        if not m:
            return None
        i = m.start()
    before = head[:i]
    rest = head[i + len(name):].strip()
    if not rest.startswith("("):
        return None
    params = rest[1:rest.rfind(")")]
    if not before.strip():
        ret = "int"                    # C89 implicit int
    else:
        ret = _norm_type(before, typedefs)
    if ret is None:
        return None
    out = []
    ps = [p for p in _split_top(params, ",") if p.strip()]
    if len(ps) == 1 and ps[0].strip() == "void":
        ps = []
    for p in ps:
        if p.strip() == "...":
            return None
        t = _norm_type(_strip_declarator_name(p), typedefs)
        if t is None or t == "void":
            return None
        out.append(t)
    return ret, tuple(out)


_WORD_TYPES = {"int", "unsigned int", "short", "unsigned short", "char", "signed char",
               "unsigned char", "long", "unsigned long", "void *"}


def _word_call(sig):
    """True when a normalised signature passes and returns only word-sized
    scalars and pointers: a call through it can carry another function's
    arguments as words (the trampolines' cross-prototype call)."""
    ret, params = sig
    return (ret == "void" or ret in _WORD_TYPES) and all(t in _WORD_TYPES for t in params)


def proto(ret, name, params):
    plist = ", ".join("%s a%d" % (t, k) for k, t in enumerate(params)) or "void"
    sep = "" if ret.endswith("*") else " "
    return "%s%s%s(%s)" % (ret, sep, name, plist)


def proto_decl(ret, name, params):
    plist = ", ".join(str(t) for t in params) or "void"
    sep = "" if ret.endswith("*") else " "
    return "%s%s%s(%s);" % (ret, sep, name, plist)


def proto_text(sig):
    """A normalised signature as the report spells it."""
    ret, params = sig
    return "%s (%s)" % (ret, ", ".join(map(str, params)) or "void")


# The event layer's limit: how many arguments a bound game function may
# take.  It is sdk/mod_event.h's BW_EVENT_ARGS_MAX, and the two must agree:
# the call the slot builds carries the arguments as words.  A slot with more
# is reported by the generator, not papered over.
BW_EVENT_ARGS_MAX = 16


def event_call_init(params):
    """The BwEventCall initialiser for a slot's parameters: the arguments
    as words, and their count (sdk/mod_event.h)."""
    init = ", ".join("(uintptr_t)%s" % word_bits(params[k], "a%d" % k) for k in range(len(params)))
    return "{ { %s }, %du, 0 }" % (init or "0", len(params))


def event_call_args(params):
    """The call's arguments, cast back to the function's own parameter
    types: the original is called with these, not with the incoming
    arguments, so a handler's change to an argument reaches the game
    (docs/mod-implementation.md, section 1)."""
    return ", ".join(word_cast(params[k], "call.a[%d]" % k) for k in range(len(params)))


def event_result(ret, expr):
    """`expr`, the result of a call, as the call's s64 result."""
    if expr == "0" or expr == zero_of(ret):
        return "(s64)0"
    if ret.endswith("*"):
        return "(s64)(uintptr_t)%s" % expr
    return "(s64)%s" % word_bits(ret, expr)


def event_value(ret):
    """The call's result as the slot's own return type."""
    if ret.endswith("*"):
        return "(void *)(uintptr_t)call.ret"
    return word_cast(ret, "call.ret")


def slot_function(region_expr, addr, slot):
    """The generated slot for one declared address: the indirection at the
    call site (docs/mod-interface.md, section 2) and, for a row that carries
    a stable event, the event's enter and leave around the call
    (docs/mod-implementation.md, section 1, "At the slot").  Its default is
    the original, so with no mod installed it behaves as the direct call did.

    The switch is not asked here: the registry asks it, per hook, with the
    name the mod's manifest stated or the row's own name (docs/mod-overrides.md,
    "The switch").  A hook whose switch is off is skipped and the walk goes
    on to the next link inward, which a gate here could not do: it would
    send the whole chain past the function.

    `slot` is {name, declared, original, sig, events}: the events are the
    declared set's fourth column, empty for a raw-hook-only row.  Every
    slot pushes its (region, address) on the scope stack and pops it on
    return, so a binding that is inside or outside a function can tell
    (docs/mod-implementation.md, section 1, "Scope")."""
    ret, params = slot["sig"]
    name = slot["name"]
    events = slot.get("events") or []
    args = ", ".join("a%d" % k for k in range(len(params)))
    cast = "%s (*)(%s)" % (ret, ", ".join(map(str, params)) or "void")
    key = "0x%08Xu" % addr
    out = [proto(ret, name, params), "{"]
    if events:
        out.append("    BwEventCall    call = %s;" % event_call_init(params))
        out.append("    BwEventPayload payload;")
    out.append("    void *h = bw_mod_slot_lookup(%s, %s);" % (region_expr, key))
    if ret != "void":
        out.append("    %s%s" % (ret, "r;" if ret.endswith("*") else " r;"))
    out.append("")
    out.append("    /* The scope: this function runs, so a scoped binding can tell. */")
    out.append("    bw_mod_event_push(%s, %s);" % (region_expr, key))
    if events:
        out.append("    if (bw_mod_event_enter(%s, %s, &call, &payload)) {" % (region_expr, key))
        out.append("        bw_mod_event_pop();")
        if ret != "void":
            out.append("        return %s;" % event_value(ret))
        else:
            out.append("        return;")
        out.append("    }")
    cargs = event_call_args(params) if events else args
    out.append("    if (h != NULL) {")
    if ret == "void":
        out.append("        ((%s)h)(%s);" % (cast, cargs))
    else:
        out.append("        r = ((%s)h)(%s);" % (cast, cargs))
    out.append("    } else {")
    if ret == "void":
        out.append("        %s(%s);" % (slot["original"], cargs))
    else:
        out.append("        r = %s(%s);" % (slot["original"], cargs))
    out.append("    }")
    if events:
        if ret != "void":
            out.append("    call.ret = %s;" % event_result(ret, "r"))
        out.append("    bw_mod_event_leave(%s, %s, &call, &payload);" % (region_expr, key))
    out.append("    bw_mod_event_pop();")
    if ret != "void":
        out.append("    return %s;" % (event_value(ret) if events else "r"))
    out.append("}")
    out.append("")
    return "\n".join(out)


def implicit_return(head, name):
    """The return type of a non-prototype declaration (tools/bootlink.py's
    rule): `int`, as mwcc compiled the call, unless the definition returns a
    64-bit value."""
    if not head:
        return "int"
    before = head.split("(")[0]
    before = before[:before.rfind(name)] if name in before else before
    words = [w for w in IDENT.findall(before)
             if w not in ("static", "inline", "extern", "const", "volatile")]
    if "*" not in before and (any(w in ("u64", "s64", "fx64", "OSTick") for w in words)
                              or " ".join(words).count("long long")):
        return "long long"
    return "int"


# ---------------------------------------------------------------------------
# aletheia's metadata
# ---------------------------------------------------------------------------

class Overlay:
    def __init__(self, o):
        self.id = o["id"]
        self.lo = o["ram_start"]
        self.code_end = o["ram_start"] + o["code_size"]
        self.bss_start = o["bss_start"]
        self.hi = o["bss_end"]
        self.code_size = o["code_size"]
        self.bss_size = o["bss_size"]
        self.sinit_count = max(0, o.get("static_init_count", 1) - 1)
        self.tag = "ov%03d" % self.id

    def owns(self, addr):
        return self.lo <= addr < self.hi


class Aletheia:
    def __init__(self, root):
        self.root = root
        with open(os.path.join(root, "layout", "layout.json"), encoding="utf-8") as fh:
            lay = json.load(fh)
        self.overlays = {o["id"]: Overlay(o) for o in lay["overlays"]}
        self.sdk_by_name = {}       # name -> [(addr, region)]
        self.sdk_at = {}            # (region, addr) -> name
        self._read_sdk()
        self.ov_sources = {}        # id -> [source rel path]
        self.ov_names = {}          # id -> {addr: [names]} (every shard entry)
        self.src_names = {}         # source rel -> [(name, addr)]
        self.arm9_at = {}           # ARM9 addr -> [names] (shards)
        self.arm9_src = {}          # ARM9 source rel -> [(name, addr)]
        self._read_shards()

    def _read_sdk(self):
        """The SDK's symbols (symbols_sdk.txt) and the runtime helpers
        (runtime_arm9.txt), by name and by region and address."""
        for fname in ("symbols_sdk.txt", "runtime_arm9.txt"):
            path = os.path.join(self.root, "layout", fname)
            if not os.path.exists(path):
                continue
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    if line.startswith("#"):
                        continue
                    parts = line.split()
                    if len(parts) < 2 or not re.fullmatch(r"[0-9A-Fa-f]{8}", parts[0]):
                        continue
                    addr = int(parts[0], 16)
                    region = parts[3] if len(parts) >= 4 else "arm9"
                    if not region.startswith("ov"):
                        region = "arm9"
                    self.sdk_by_name.setdefault(parts[1], []).append((addr, region))
                    self.sdk_at.setdefault((region, addr), parts[1])

    def _record(self, region, src, placed):
        m = re.match(r"^ov(\d{3})$", region)
        if m:
            oid = int(m.group(1))
            self.ov_sources.setdefault(oid, [])
            if src not in self.ov_sources[oid]:
                self.ov_sources[oid].append(src)
            for n, a in placed:
                self.ov_names.setdefault(oid, {}).setdefault(a, []).append(n)
            self.src_names.setdefault(src, []).extend(placed)
        else:
            for n, a in placed:
                self.arm9_at.setdefault(a, []).append(n)
            self.arm9_src.setdefault(src, []).extend(placed)

    def _read_game(self):
        """src/game's translation units (tools/gametree.py, docs/game-tree.md):
        each unit's functions at the addresses the manifests give them."""
        import gametree
        for e in gametree.entries(self.root):
            region = e["region"]
            placed = []
            for n, a in zip(e["functions"], e["addresses"]):
                if a is None:
                    hits = [ad for ad, rg in self.sdk_by_name.get(n, []) if rg == region
                            or (rg == "arm9" and not region.startswith("ov"))]
                    a = hits[0] if hits else None
                if a is not None:
                    placed.append((n, a))
            self._record(region, e["source"], placed)

    def _read_shards(self):
        if os.environ.get("EIDOS_SOURCE_TREE", "matched") == "game"                 and os.path.isdir(os.path.join(self.root, "src", "game")):
            # (A tree without src/game, such as the synthetic one the tests
            # link, is read the src/matched way whatever the build's switch.)
            self._read_game()
            return
        pattern = os.path.join(self.root, "layout", "matched", "*", "*.json")
        for path in sorted(glob.glob(pattern)):
            with open(path, encoding="utf-8") as fh:
                shard = json.load(fh)
            for e in shard.get("functions", []):
                if e.get("status") not in ("EXACT", "EXACT*"):
                    continue
                region = e.get("region", "arm9")
                src = e["source"].replace("\\", "/")
                names = e.get("functions") or [e["name"]]
                # aletheia pairs each unit's `functions` with its
                # `addresses`; an older shard spells the same list `covers`.
                # A descriptive member (SAVEDATA_GetTotalSize) has no address
                # in its name, so without the pair it is placed nowhere and an
                # address-spelled caller looks like a name the link lacks.
                member_addrs = e.get("addresses") or e.get("covers")
                if member_addrs:
                    rows = {n: hs[0][0] for n, hs in self.sdk_by_name.items()
                            if hs and hs[0][1] == region}
                    member_addrs = gametree.pair_members(names, [int(a, 16) for a in member_addrs], rows)
                base = int(e["address"], 16)
                placed = []
                for k, n in enumerate(names):
                    m = ADDR_FUNC.match(n)
                    if member_addrs and k < len(member_addrs) and member_addrs[k] is not None:
                        a = member_addrs[k]
                    elif m:
                        a = int(m.group(1), 16)
                    elif k == 0:
                        a = base
                    else:
                        hits = [ad for ad, rg in self.sdk_by_name.get(n, []) if rg == region]
                        a = hits[0] if hits else None
                    if a is not None:
                        placed.append((n, a))
                m = re.match(r"^ov(\d{3})$", region)
                if m:
                    oid = int(m.group(1))
                    self.ov_sources.setdefault(oid, [])
                    if src not in self.ov_sources[oid]:
                        self.ov_sources[oid].append(src)
                    for n, a in placed:
                        self.ov_names.setdefault(oid, {}).setdefault(a, []).append(n)
                    self.src_names.setdefault(src, []).extend(placed)
                else:
                    for n, a in placed:
                        self.arm9_at.setdefault(a, []).append(n)
                    self.arm9_src.setdefault(src, []).extend(placed)

    def windows(self, addr):
        return [o.id for o in self.overlays.values() if o.owns(addr)]

    def in_any_window(self, addr):
        return any(o.owns(addr) for o in self.overlays.values())

    def listing(self, oid):
        """(annotations {loc: (symbol, target)}, functions {addr: name}) from
        asm/overlays/ovNNN.s."""
        path = os.path.join(self.root, "asm", "overlays", "ov%03d.s" % oid)
        ann, funcs = {}, {}
        if not os.path.exists(path):
            return ann, funcs
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith(".global"):
                    name = s.split()[-1]
                    m = ADDR_FUNC.match(name)
                    if m and not name.startswith("veneer_"):
                        funcs[int(m.group(1), 16)] = name
                    continue
                if "->" not in line or not armdata.WORD_LINE.match(line):
                    continue
                m = armdata.ANNOT.search(line)
                if not m:
                    continue
                sym = m.group(2)
                off = int(m.group(3), 16) if m.group(3) else 0
                base = armdata.name_address(sym)
                if base is None:
                    continue
                tgt = base + off
                if m.group(4):
                    tgt |= 1
                ann[int(m.group(1), 16)] = (sym, tgt)
        return ann, funcs

    def thumb_funcs(self, oid):
        """Addresses of the overlay's Thumb functions: a `.thumb_func`
        after a function's `.global` in asm/overlays/ovNNN.s."""
        path = os.path.join(self.root, "asm", "overlays", "ov%03d.s" % oid)
        out, last = set(), None
        if not os.path.exists(path):
            return out
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith(".global"):
                    m = ADDR_FUNC.match(s.split()[-1])
                    last = int(m.group(1), 16) if m else None
                elif s.startswith(".thumb_func") and last is not None:
                    out.add(last)
        return out

    def data_object_relocs(self, oid):
        rng, objects, _ext = armdata.load_data_objects(self.root, "ov%03d" % oid)
        out = {}
        for obj in objects:
            for loc, tgt in armdata.object_pointer_words(obj):
                out[loc] = tgt
        return out


# ---------------------------------------------------------------------------
# The link
# ---------------------------------------------------------------------------

def flat_name(rel):
    rel = rel[:-2] if rel.endswith(".c") else rel
    return re.sub(r"[^A-Za-z0-9_]", "_", rel)


class Unit:
    def __init__(self, oid, rel, path, scan):
        self.oid = oid
        self.rel = rel
        self.path = path
        self.scan = scan
        self.protos = {}           # name -> return type (implicit calls)
        self.builtins = set()
        self.builtins_declared = set()
        self.private = {}          # name -> this file's own copy's name
        self.shares = {}           # name -> (private name, target, signature):
                                   # this file's own name for a function it
                                   # spells together with another name
        self.inc_decls = set()     # names the headers this file includes
                                   # declare
        self.literal_table = False
        self.header = None
        self.cuts = set()          # src/game: functions a port patch defines
        self.self_calls = set()    # src/game: declared names it defines and
                                   # calls itself (routed through the slot)


class OvState:
    def __init__(self, ov):
        self.ov = ov
        self.units = []
        self.excluded = []         # (rel, why)
        self.replaced = []         # (rel, port file): asm bodies in C
        self.standins = []         # port stand-ins (stubs/ov/) compiled in
        self.thumb = set()         # Thumb function addresses (the listing)
        self.defs = {}             # exported name -> unit
        self.at = {}               # addr -> defined function name (exported)
        self.known = set()         # function addresses known in the window
        self.renames = {}          # spelling -> ovNNN_ name
        self.stubs = {}            # addr -> canonical name (func_XXXXXXXX)
        self.data = {}             # name -> dict
        self.objects = {}          # name -> dict (host objects, conflicts)
        self.relocs = []           # {where, target, kind, ...}
        self.code_literals = {}    # value -> symbol expression
        self.sinits = []           # symbol names
        self.unresolved = {}
        self.slot_names = {}       # declared name -> the slot it goes through
        self.mod_slots = []        # declared rows: {addr, declared, original, sig}
        self.moves = []            # declared functions moved out of their unit


# The ARM9 static module's load address (layout.json's arm9.load_address,
# docs/armdata.md): the first byte of arm9_dec.bin is DS 0x02004000.
ARM9_IMAGE_BASE = 0x02004000
# A linker veneer: `ldr ip, [pc]` and `bx ip`, then the target word.
VENEER_CODE = b"\x00\xc0\x9f\xe5\x1c\xff\x2f\xe1"

class Link:
    def __init__(self, args):
        self.args = args
        self.root = os.path.abspath(args.aletheia_root)
        self.al = Aletheia(self.root)
        self.notes = []
        self.event_notes = []      # declared rows whose event cannot fire
        self.ids = self._parse_ids(args.overlays)
        # The declared overlay rows (docs/mod-interface.md, section 1): the
        # (region, address) pairs a mod may override, and the events the
        # port binds to them.
        self.declared = {}          # overlay tag -> {addr: declared name}
        self.declared_events = {}   # (overlay tag, addr) -> [event names]
        self.declared_by_addr = {}  # addr -> (overlay id, declared name, events)
        for addr, dname, region, events in read_declared_set():
            if region == "arm9":
                continue
            self.declared.setdefault(region, {})[addr] = dname
            self.declared_events[(region, addr)] = events
            m = re.match(r"^ov0*(\d+)$", region)
            if m:
                self.declared_by_addr.setdefault(
                    addr, (int(m.group(1)), dname, events))
        self.image = None
        self.veneers = {}          # veneer address -> target address (listing)
        self.veneer_fwd = {}       # forwarder name -> (target name, signature)
        if getattr(args, "arm9_image", None) and os.path.exists(args.arm9_image):
            with open(args.arm9_image, "rb") as fh:
                self.image = fh.read()
        self.exclusions = armdata.load_exclusions(args.exclusions)
        self.ov_excl = {}
        for line in read_list(args.ov_exclusions):
            parts = line.split(None, 1)
            self.ov_excl[parts[0]] = parts[1] if len(parts) > 1 else "listed"
        self._read_link()
        self.state = {}
        self.tramps = {}           # plain name -> addr
        self.arm9_needs = {}       # name -> (addr or None, defined by link?)
        self.arm9_stub = {}        # name -> addr or None
        self.types = None          # the game's own type names, when asked
        self._read_overlays()
        self._link_refs()
        for oid in self.ids:
            self._resolve(self.state[oid])
        for oid in self.ids:
            self._data(self.state[oid])
        self._finish_stubs()
        self._mod_slots()

    # -- inputs ------------------------------------------------------------

    def _parse_ids(self, text):
        text = (text or "").strip()
        if text.upper() == "ALL":
            ids = sorted(self.al.ov_sources)
        elif text.upper() in ("TITLE", ""):
            ids = list(TITLE_SET)
        else:
            ids = []
            for part in re.split(r"[;, ]+", text):
                if not part:
                    continue
                if part.upper() == "TITLE":
                    ids += TITLE_SET          # the title path plus more ids
                    continue
                m = re.match(r"^(?:ov)?0*(\d+)$", part, re.I)
                if not m:
                    raise SystemExit("ovlink.py: bad overlay id %r" % part)
                ids.append(int(m.group(1)))
        for oid in ids:
            if oid not in self.al.overlays:
                raise SystemExit("ovlink.py: no overlay %d in layout.json" % oid)
        return sorted(set(ids))

    def rel_of(self, path):
        p = os.path.abspath(path).replace("\\", "/")
        r = self.root.replace("\\", "/")
        if p.lower().startswith(r.lower() + "/"):
            return p[len(r) + 1:]
        return None

    def _read_link(self):
        """The rest of the link: what it defines, and where."""
        self.game_cuts = None
        if getattr(self.args, "cut_list", None) and os.path.exists(self.args.cut_list):
            self.game_cuts = {}
            for row in read_list(self.args.cut_list):
                path, _, names = row.partition("\t")
                self.game_cuts[os.path.normcase(os.path.abspath(path))] = \
                    set(n for n in names.split(",") if n)
        self.link_defs = set()
        self.link_at = {}          # addr -> defined name
        self.link_scans = []
        self.link_heads = {}
        self.link_typedefs = {}
        paths = []
        for lst in self.args.link_sources:
            paths += read_list(lst)
        seen = set()
        for p in paths:
            if not p.endswith(".c") or p in seen:
                continue
            seen.add(p)
            if not os.path.exists(p):
                continue                 # generated later in the build
            ptext = preprocess(read_text(p))
            if "/src/game/" in p.replace("\\", "/") and self.game_cuts is not None:
                # As the boot compiles it (tools/bootlink.py plan_game): the
                # functions the port defines and the asm bodies are cut, so
                # a call to one resolves to the port's spelling by address.
                import gamecut
                ptext = gamecut.cut(ptext, self.game_cuts.get(
                    os.path.normcase(os.path.abspath(p)), set()), True)[0]
            sc = _SCANS.get(("ovlink.Scan", p, include_signature(p, ptext)), ptext,
                            lambda: Scan(p, ptext))
            self.link_scans.append(sc)
            for n in sc.exported():
                self.link_defs.add(n)
                if n in sc.heads:
                    self.link_heads[n] = (sc.heads[n], sc.typedefs)
            rel = self.rel_of(p)
            if rel and rel in self.al.arm9_src:
                for n, a in self.al.arm9_src[rel]:
                    if n in sc.exported():
                        self.link_at.setdefault(a, n)
        for n in sorted(self.link_defs):
            a = armdata.name_address(n)
            if a is None:
                hits = [ad for ad, rg in self.al.sdk_by_name.get(n, [])
                        if not rg.startswith("ov")]
                a = hits[0] if hits else None
            if a is not None:
                self.link_at.setdefault(a, n)
        # A linker veneer is another address of its target (the listing's
        # veneer_ labels, closure.listing_veneers, or the image's islands):
        # an overlay calling the veneer reaches what the link defines at the
        # target, through a forwarder (veneer_target, write_dispatch).
        self.veneers = dict(closure.listing_veneers(self.root))
        # A data word or a code pointer at a veneer's address names the
        # target too (a call goes through the forwarder instead).
        for v, t in self.veneers.items():
            if t in self.link_at:
                self.link_at.setdefault(v, self.link_at[t])

    def veneer_target(self, addr):
        """The target of the linker veneer at `addr`, or None: the listing's
        `veneer_XXXXXXXX` label when the listing is there, else the player's
        ARM9 image (--arm9-image).  The listing is generated and not in a
        clean export of the pin; the image is what it was made from:
        `ldr ip, [pc]; bx ip` (0xE59FC000 0xE12FFF1C), then the target word."""
        if addr is None:
            return None
        if (addr & ~1) in self.veneers:
            return self.veneers[addr & ~1]
        if self.image is None:
            return None
        off = (addr & ~1) - ARM9_IMAGE_BASE
        if off < 0 or off + 12 > len(self.image) or self.image[off:off + 8] != VENEER_CODE:
            return None
        return int.from_bytes(self.image[off + 8:off + 12], "little")

    def _read_overlays(self):
        for oid in self.ids:
            ov = self.al.overlays[oid]
            st = OvState(ov)
            self.state[oid] = st
            ann, lfuncs = self.al.listing(oid)
            st.listing = ann
            st.thumb = self.al.thumb_funcs(oid)
            st.known |= set(lfuncs)
            st.known |= set(self.al.ov_names.get(oid, {}))
            for (region, a), n in self.al.sdk_at.items():
                if region == ov.tag:
                    st.known.add(a)
            for rel in sorted(self.al.ov_sources.get(oid, [])):
                path = os.path.join(self.root, rel)
                if rel in self.ov_excl:
                    st.excluded.append((rel, self.ov_excl[rel]))
                    continue
                if not os.path.exists(path):
                    st.excluded.append((rel, "missing in the checkout"))
                    continue
                sc = Scan(path, read_text(path))
                if sc.has_asm and rel.startswith("src/game/"):
                    # A src/game unit keeps its C: its copy has the asm bodies
                    # cut out (tools/gamecut.py; cmake/overlays.cmake passes
                    # --cut-asm), so it is read the way it will compile, and
                    # the cut functions are stubbed like any other the link
                    # lacks.
                    import gamecut
                    cut_text, _cut = gamecut.cut(read_text(path), set(), True)
                    sc = Scan(path, cut_text)
                if sc.has_asm:
                    # Portable C for an asm-bodied file, when the port has
                    # written it (sdk/asm/ov/<file>, as sdk/asm/ for ARM9),
                    # compiled under the overlay's names like any other.
                    repl = os.path.join(self.args.asm_dir, os.path.basename(rel)) \
                        if self.args.asm_dir else None
                    if repl and os.path.exists(repl):
                        sc = Scan(repl, read_text(repl))
                        st.units.append(Unit(oid, rel, repl, sc))
                        st.replaced.append((rel, repl))
                        continue
                    st.excluded.append((rel, "mwcc asm body"))
                    continue
                undef = sc.static_undefined()
                if undef:
                    st.excluded.append((rel, "uses static functions another file defines (%s), "
                                        "which only mwcc's link resolves"
                                        % ", ".join(sorted(undef))))
                    continue
                st.units.append(Unit(oid, rel, path, sc))
            self._port_standins(oid, st)
            self._owners(st)
            self._mod_moves(st) if getattr(self.args, "mod_moves", False) else None
            # A move changes what a unit owns: the function is now a unit of
            # its own, and the names the moved text uses are the original
            # unit's own no more, so they become the overlay's names and
            # every unit that calls them reaches the renamed ones.  The
            # ownership is worked out again, and the overlay's header (which
            # is written later) then carries the right renames.
            self._owners(st)
            for k in range(ov.sinit_count):
                addrs = overlay_table.KNOWN_SINIT.get(oid, [])
                if k < len(addrs):
                    st.known.add(addrs[k])

    def _port_standins(self, oid, st):
        """Port C in stubs/ov/ for one overlay, two kinds:

        - a stand-in for a function aletheia has not matched,
          stubs/ov/ovNNN_<name>.c, written from the listing, compiled as one
          more unit of overlay NNN so the function is that code instead of
          a panicking stub.  One whose functions a matched file of the
          overlay now defines is left out and reported: it is obsolete;
        - a reimplementation of a matched file, stubs/ov/reimpl_ovNNN_<name>.c,
          whose header names the file it takes the place of
          (` * replaces: src/matched/ov/...`), as stubs/reimpl_ files do for
          the ARM9: the matched file is left out and this one compiled in
          its place (an argument the matched C forwards in a register)."""
        d = self.args.port_ov_dir
        if not d or not os.path.isdir(d):
            return
        # A patch of a src/game unit, stubs/ov/patch_ovNNN_<name>.c: its header
        # names the unit and the functions it defines in the unit's place
        # (` * patches: src/game/ov009/dwc_match.c NameA NameB`).  Those
        # definitions are cut from the unit's copy (tools/gamecut.py) and the
        # patch is compiled beside it: one function changes, the file stays.
        for fname in sorted(os.listdir(d)):
            if not (fname.startswith("patch_" + st.ov.tag + "_") and fname.endswith(".c")):
                continue
            path = os.path.join(d, fname)
            text = read_text(path)
            if getattr(self.args, "no_platform_ui", False) and re.search(
                    r"\beidos_(?:settings_|sui_|opt_proc_)", text):
                # Keep the game's own menu functions when this build has
                # no platform settings model. Other gameplay fixes stay.
                continue
            m = re.search(r"^\s*\*\s*patches:\s*(\S+)\s+(.+)$", text[:4096], re.M)
            if not m:
                raise SystemExit("ovlink.py: %s names no unit and functions it patches" % path)
            rel, names = m.group(1), set(m.group(2).split())
            unit = next((u for u in st.units if u.rel == rel), None)
            if unit is None:
                st.excluded.append((path, "patches %s, which this overlay does not "
                                    "compile (renamed or gone in aletheia?)" % rel))
                self.notes.append("port patch %s patches nothing" % path)
                continue
            import gamecut
            unit.cuts |= names
            cut_text, cut = gamecut.cut(read_text(unit.path), unit.cuts, True, True)
            missing = sorted(names - set(cut))
            if missing:
                self.notes.append("port patch %s: %s not defined in %s"
                                  % (path, ", ".join(missing), rel))
            unit.scan = Scan(unit.path, cut_text)
            st.units.append(Unit(oid, "port/ov/" + fname, path, Scan(path, text)))
            st.replaced.append((rel + " (" + ", ".join(sorted(names)) + ")", path))
        for fname in sorted(os.listdir(d)):
            if not (fname.startswith("reimpl_" + st.ov.tag + "_") and fname.endswith(".c")):
                continue
            path = os.path.join(d, fname)
            text = read_text(path)
            m = re.search(r"^\s*\*\s*replaces:\s*(\S+)", text[:4096], re.M)
            if not m:
                raise SystemExit("ovlink.py: %s names no file it replaces" % path)
            rel = m.group(1)
            keep = [u for u in st.units if u.rel != rel]
            if len(keep) == len(st.units):
                st.excluded.append((path, "replaces %s, which this overlay does not "
                                    "compile (renamed or gone in aletheia?)" % rel))
                self.notes.append("port reimplementation %s replaces nothing" % path)
                continue
            st.units = keep
            st.units.append(Unit(oid, "port/ov/" + fname, path, Scan(path, text)))
            st.replaced.append((rel, path))
        defined = set()
        for u in st.units:
            defined |= set(u.scan.exported())
        for fname in sorted(os.listdir(d)):
            if not (fname.startswith(st.ov.tag + "_") and fname.endswith(".c")):
                continue
            path = os.path.join(d, fname)
            sc = Scan(path, read_text(path))
            clash = sorted(set(sc.exported()) & defined)
            if clash:
                st.excluded.append((path, "port stand-in made obsolete by aletheia's "
                                    "match of %s: delete it" % ", ".join(clash)))
                self.notes.append("obsolete port stand-in %s (%s matched)"
                                  % (path, ", ".join(clash)))
                continue
            rel = "port/ov/" + fname
            st.units.append(Unit(oid, rel, path, sc))
            st.standins.append(path)

    # -- the declared functions whose own unit calls them -------------------

    def _mod_moves(self, st):
        """The declared functions whose own unit calls them.

        A call inside the unit that defines the function binds by name and
        does not pass the slot (docs/mod-interface.md, section 2), so the
        event the row carries does not fire there, and a raw hook on the row
        misses those calls.  The port's own answer for a merged unit is the
        one it uses when the port supplies a function of one (docs/game-tree.md):
        the definition is cut out of the unit's copy and put in a unit of its
        own, and the unit's calls reach the slot like every other caller's
        do.

        The moved text is the game's own (the cut copy the build compiles),
        and it is compiled as a unit of the overlay, so the game's code keeps
        its own names and the port's code does not collide with them.  A
        function that uses what only its own unit has (a file-local helper or
        object, a name the unit owns for itself) is left where it is: the
        port reports it, and the row keeps the limit it had.
        """
        decl = self.declared.get(st.ov.tag, {})
        for _round in range(len(decl) + 1):
            # A round moves what it can; the next round sees the units the
            # last one changed (a call that left the unit that held it), so
            # the row that still has a call inside its own unit is moved too.
            moved = 0
            for addr in sorted(decl):
                dname = decl[addr]
                u = st.defs.get(dname)
                if u is None:
                    continue          # the row's function is not in this link
                if dname not in u.scan.called:
                    continue          # the unit does not call it: nothing to fix
                if dname in u.cuts:
                    continue          # a port patch defines it
                if dname in u.scan.statics:
                    continue
                if self._move_function(st, u, dname, addr):
                    moved += 1
            if not moved:
                break

    def _move_function(self, st, u, name, addr):
        """Cut `name` out of `u`'s copy and put it in a unit of its own.

        The cut is the port's own (tools/gamecut.py, the cut list cmake/
        overlays.cmake passes to tools/dsaddr.py): the unit's copy loses the
        definition, and the unit's calls to the name reach the slot.  The
        moved text is written into a file of the build directory and listed
        as one more unit of the overlay, so it is compiled under the
        overlay's names like any other.

        Returns True when the function was moved."""
        import gamecut
        if not os.path.exists(u.path):
            return False
        text = read_text(u.path)
        cut_text, _cut = gamecut.cut(text, u.cuts, True, True)
        body = None
        for fname, hs, bo, be, is_asm in gamecut.functions(cut_text):
            if fname != name:
                continue
            if is_asm:
                self.notes.append(
                    "%s: %s (0x%08X) has an mwcc asm body: the call inside its "
                    "own unit does not pass the slot" % (st.ov.tag, name, addr))
                return False
            body = cut_text[hs:be]
            line = cut_text.count("\n", 0, hs) + 1
            break
        if body is None:
            return False
        used = set(IDENT.findall(body))
        clash = sorted((used & u.scan.statics) | set(u.private) | set(u.shares))
        if clash:
            self.notes.append(
                "%s: %s (0x%08X) uses %s, which only its own unit has: the call "
                "inside that unit does not pass the slot"
                % (st.ov.tag, name, addr, ", ".join(clash)))
            return False
        incs = self._unit_includes(u, cut_text)
        if incs is None:
            return False
        if _headers_need_rewrite(incs):
            # The build rewrites a header that holds a fixed DS address, and
            # it writes the rewrite next to the copy it compiles.  The moved
            # unit names the tree's headers by their own path, so the
            # rewrite would land in the tree: the function stays where it is.
            self.notes.append(
                "%s: %s (0x%08X) needs a header the build rewrites: the call "
                "inside its own unit does not pass the slot"
                % (st.ov.tag, name, addr))
            return False
        # The moved unit carries the unit's own headers and typedefs, in the
        # order the unit has them: a typedef the unit declares before the
        # header that uses it (the SDK's OSIntrMode) is part of what the
        # moved text needs.
        prelude = unit_prelude(u.path, cut_text, hs)
        if prelude is None:
            return False
        out_dir = os.path.join(os.path.abspath(self.args.out_dir), "gen")
        os.makedirs(out_dir, exist_ok=True)
        fname = "%s_%s.c" % (st.ov.tag, name)
        path = os.path.join(out_dir, fname)
        rel = "port/ov/" + fname
        lines = [
            "/*",
            " * %s" % GEN,
            " *",
            " * The game's own %s, moved out of %s." % (name, u.rel),
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
        # The diagnostics name the game's own file and line (the port's
        # rule for a matched source: tools/dsaddr.py).
        lines.append('#line %d "%s"' % (line, u.path.replace("\\", "/")))
        lines.append(body)
        write_if_changed(path, "\n".join(lines) + "\n")
        unit = Unit(st.ov.id, rel, path, Scan(path, read_text(path)))
        st.units.append(unit)
        st.defs[name] = unit
        u.private.pop(name, None)
        # The unit's copy loses the definition (and the file-scope prototype
        # of it), so the build cuts it the way the port's own patches are cut.
        u.cuts.add(name)
        cut_text, _cut = gamecut.cut(text, u.cuts, True, True)
        u.scan = Scan(u.path, cut_text)
        st.moves.append((name, addr, u.rel, rel))
        return True

    def _unit_includes(self, u, text):
        """The quoted headers `u` includes, by their own path, or None when
        one of them is not in the tree.

        The moved unit is compiled in the build directory, so it names the
        tree's headers by their own path: the tree's headers are the ones
        the unit's copy compiles against, and the build's copy of a header
        that holds a fixed DS address is the one the port rewrites."""
        base = os.path.dirname(os.path.abspath(u.path))
        out = []
        for inc in _INCLUDE_RE.findall(text):
            hp = os.path.normpath(os.path.join(base, inc))
            if not os.path.isfile(hp):
                return None
            out.append(hp.replace("\\", "/"))
        return out

    def _owners(self, st):
        """One definition per name.  Some matched files define, besides the
        function their manifest entry claims, neighbours another file claims
        too (a unit kept next to the single-function files split out of it);
        mwcc's link took the claimed one.  The owner of a name is the file
        whose entry claims it, else the first file defining it; every other
        file's copy gets a name private to that file, which its own calls
        keep using."""
        ov = st.ov
        owner = {}
        for u in st.units:
            exp = u.scan.exported()
            for n, _a in self.al.src_names.get(u.rel, []):
                if n in exp:
                    owner.setdefault(n, u)
        for u in st.units:
            for n in sorted(u.scan.exported()):
                owner.setdefault(n, u)
        for u in st.units:
            for n in u.scan.exported():
                if owner[n] is not u:
                    u.private[n] = "%s__%s" % (self.own(st, n), flat_name(os.path.basename(u.rel)))
        st.defs = owner
        for u in st.units:
            sc = u.scan
            for n, a in self.al.src_names.get(u.rel, []):
                if owner.get(n) is u and n in sc.func_defs:
                    st.at.setdefault(a, n)
        # A port patch defines a function src/game lists under another file
        # (stubs/ov/patch_ov021_scrcmd_msgwin.c's SCRCMD_MSGWIN_CmdInfoSysWinMsg):
        # its address is the one the overlay's own table gives the name.
        by_name = {}
        for ad, names in self.al.ov_names.get(ov.id, {}).items():
            for nm in names:
                by_name.setdefault(nm, ad)
        for n, u in sorted(owner.items()):
            if n not in u.scan.func_defs:
                continue
            a = armdata.name_address(n)
            if a is not None and ov.owns(a):
                st.at.setdefault(a, n)
            elif n in by_name and ov.owns(by_name[n]) and by_name[n] not in st.at:
                st.at[by_name[n]] = n
            else:
                for ad, rg in self.al.sdk_by_name.get(n, []):
                    if rg == ov.tag:
                        st.at.setdefault(ad, n)

    def universe(self, name):
        if name in MWCC_BUILTINS:
            return True
        if armdata.name_address(name) is not None:
            return True
        if name in self.al.sdk_by_name or name in self.link_defs:
            return True
        return any(name in st.defs for st in self.state.values())

    def address(self, name, sc, st=None):
        """(address, how) of a name one source uses."""
        sym = sc.symbols.get(name)
        if sym is not None:
            return sym[0]
        if st is not None and name in st.defs:
            for a, n in st.at.items():
                if n == name:
                    return a
        a = armdata.name_address(name)
        if a is not None:
            return a
        hits = self.al.sdk_by_name.get(name, [])
        if st is not None:
            own = [ad for ad, rg in hits if rg == st.ov.tag]
            if own:
                return own[0]
        arm = [ad for ad, rg in hits if not rg.startswith("ov")]
        if arm:
            return arm[0]
        m = re.match(r"^ov(\d{3})_id$", name)
        if m:
            # The linker's overlay id symbol, its value the overlay number:
            # a file that names it without a symbols row (src/game's
            # ov010/event_wifi_proc.c, ov020_id).
            return int(m.group(1))
        return None

    # -- names -------------------------------------------------------------

    def own(self, st, name):
        return "%s_%s" % (st.ov.tag, name)

    def stub_name(self, st, addr):
        st.stubs.setdefault(addr, "func_%08X" % addr)
        return self.own(st, st.stubs[addr])

    def own_code(self, st, addr):
        """The ovNNN_ symbol for code at `addr` (even) in st's window."""
        n = st.at.get(addr)
        if n is not None:
            return self.own(st, n)
        return self.stub_name(st, addr)

    def _game_calls(self, sc, implicit):
        """Names the file calls without declaring that the layout knows as
        game code.

        src/game's units are merged: one file carries the functions of
        several matched ones, and its own prototype block does not declare
        every name its body calls (ov088's save_control_intr.c calls
        SAVE_CONTROL_CancelSave 0x020071A8 without declaring it).  The unit's
        own `symbols:` block carries the address, so the call is a game call
        the port has to declare.  tools/closure.py reads the same block the
        same way on the boot side (SourceScan.references), and a host
        compiler rejects an implicit declaration.

        Only ARM9 code counts here: an overlay's own functions are resolved
        against the overlay's definitions, and a name the layout does not know
        is not the port's to declare.
        """
        extra = set()
        for n in sc.called:
            if n in implicit or n in sc.declared or n in sc.block_protos:
                continue
            if n in sc.locals or n in sc.defined or n in sc.statics \
                    or n in sc.macros or n in sc.typedefs:
                continue
            sym = sc.symbols.get(n)
            if sym is None:
                continue
            if not sc.is_code(n):
                continue
            addr = sym[0] & ~1
            if addr in self.al.arm9_at:
                extra.add(n)
        return extra

    def _resolve(self, st):
        ov = st.ov
        for u in st.units:
            sc = u.scan
            # A src/game unit holds a whole original file, so a declared
            # function's callers are often in its own file
            # (FIELD_EVENT_CHECK_CheckStepEvents, the player.step event).
            # Those calls go through the slot too: the unit's copy renames
            # the definition to the overlay's own name (tools/gamecut.py,
            # rename_defs) and every other use of the name is the slot's.
            if "/src/game/" in u.path.replace("\\", "/") and os.path.exists(u.path):
                asm_bodies = None
                for n in sorted(sc.called):
                    if (st.defs.get(n) is not u or n in u.cuts or
                            n in sc.statics):
                        continue        # a patch's, or the unit's own static
                    addr = self.declared_addr(st, n, sc)
                    if addr is None:
                        continue
                    if asm_bodies is None:
                        import gamecut
                        asm_bodies = {f for f, _h, _b, _e, a in
                                      gamecut.functions(read_text(u.path)) if a}
                    if n in asm_bodies:
                        continue        # its body comes from sdk/asm by name
                    st.slot_names[n] = self.slot_name(st, addr)
                    u.self_calls.add(n)
            refs, implicit = sc.references(self.universe)
            implicit = set(implicit) | self._game_calls(sc, set(implicit))
            refs = set(refs) | implicit
            # An overlay id a macro pastes together (`ov##n##_id`, so the
            # scanner sees only the declaration): resolve every declared one.
            refs = refs | {n for n in sc.declared
                           if re.match(r"^ov\d{3}_id$", n) and n not in sc.defined}
            # Everything this file exports is the overlay's.
            for n in sc.exported():
                st.renames[n] = self.own(st, n)
            for n in sorted(refs):
                if n in MWCC_BUILTINS:
                    # Implicit (the matched files) or declared by the file
                    # itself (src/game's gamespy units): either way the port's
                    # body; a declared one keeps the file's own prototype.
                    u.builtins.add(n)
                    if n not in implicit:
                        u.builtins_declared.add(n)
                    continue
                if n in st.defs:
                    addr = self.declared_addr(st, n, sc)
                    if addr is not None and st.defs[n] is not u and n in sc.called:
                        # A declared overlay address: a non-owning caller
                        # inside the window goes through the slot
                        # (docs/mod-interface.md, section 2).  The defining
                        # unit keeps the overlay's own name.
                        st.slot_names[n] = self.slot_name(st, addr)
                    else:
                        st.renames[n] = self.own(st, n)
                    if n in implicit:
                        d = st.defs[n].scan
                        u.protos[n] = implicit_return(d.heads.get(n), n)
                    continue
                addr = self.address(n, sc, st)
                code = sc.is_code(n) or n in implicit
                if addr is not None and ov.owns(addr):
                    if code:
                        tgt = self.own_code(st, addr & ~1)
                        st.renames[n] = tgt
                        if n in implicit:
                            dn = st.at.get(addr & ~1)
                            head = st.defs[dn].scan.heads.get(dn) if dn else None
                            u.protos[n] = implicit_return(head, dn or n)
                    else:
                        self._data_name(st, n, addr, sc)
                    continue
                if n not in self.link_defs and addr is not None and addr >= CONST_LIMIT \
                        and self.al.in_any_window(addr):
                    if code:
                        self.tramps.setdefault(n, addr & ~1)
                        if n in implicit:
                            u.protos[n] = "int"
                    else:
                        self._data_name(st, n, addr, sc)
                    continue
                # ARM9, the runtime, a port object, or a linker constant.
                if n in self.link_defs:
                    self.arm9_needs.setdefault(n, (addr, True))
                    if n in implicit:
                        h = self.link_heads.get(n)
                        u.protos[n] = implicit_return(h[0] if h else None, n)
                    continue
                if code and n in HOST_LIBC:
                    # The host's C library defines it, with the standard
                    # meaning the matched file declares it for.
                    self.arm9_needs.setdefault(n, (addr, True))
                    if n in implicit:
                        u.protos[n] = "int"
                    continue
                if code:
                    # A linker veneer: another address of what the link
                    # defines at its target.  The name becomes a forwarder of
                    # its own (bw_veneer_<address>, write_dispatch) rather
                    # than the target's name, so a unit that declares two
                    # veneers of one target with different prototypes
                    # (G3D_CAMERA_Delete and G3D_LIGHT_DeleteSlots both reach
                    # GFL_HEAP_Free) still compiles.
                    vt = self.veneer_target(addr) if addr is not None else None
                    vname = self.link_at.get(vt & ~1) if vt is not None else None
                    vhead = self.link_heads.get(vname) if vname else None
                    vsig = signature(vhead[0], vname, vhead[1]) if vhead else None
                    if vsig is not None:
                        fwd = "bw_veneer_%08X" % (addr & ~1)
                        self.veneer_fwd[fwd] = (vname, vsig)
                        st.renames[n] = fwd
                        self.arm9_needs.setdefault(n, (addr, True))
                        if n in implicit:
                            u.protos[n] = implicit_return(vhead[0], n)
                        continue
                    tgt = self.link_at.get(addr & ~1) if addr is not None else None
                    if tgt is not None and tgt != n:
                        st.renames[n] = tgt
                        self.arm9_needs.setdefault(n, (addr, True))
                    else:
                        # Nothing defines it.  The overlays name it by the
                        # spelling the ARM9's own files use (the one a boot
                        # link stubs it under), and it is stubbed here only
                        # when no other source of the link reaches it.
                        canon = self.arm9_canonical(addr) if addr is not None else None
                        canon = canon or n
                        if canon != n:
                            st.renames[n] = canon
                        self.arm9_needs.setdefault(n, (addr, False))
                        if addr is None or (addr & ~1) not in self.link_open_at:
                            self.arm9_stub.setdefault(canon, addr)
                        tgt = canon
                    if n in implicit:
                        h = self.link_heads.get(tgt or n)
                        u.protos[n] = implicit_return(h[0] if h else None, n)
                    continue
                if addr is None:
                    st.unresolved[n] = os.path.basename(u.rel)
                    continue
                self._data_name(st, n, addr, sc)
            # Code addresses written as literals.
            for v in sorted(sc.literals):
                sym = self.code_symbol(st, v)
                if sym is not None:
                    st.code_literals[v] = sym
                    u.literal_table = True
        # The declared addresses a non-owning caller goes through a slot for:
        # the overlay header renames the spelling to the slot.
        st.renames.update(st.slot_names)
        self._own_names(st)

    # -- the names a unit keeps for itself -----------------------------------

    def own_signature(self, u, name):
        """The normalised prototype of the unit's own declaration of a
        name, or None when it does not normalise."""
        head = u.scan.decls.get(name)
        if head is None:
            return None
        return signature(head, name, u.scan.typedefs)

    def host_signature(self, st, name):
        """The normalised prototype of the host function a renamed name
        reaches, or None: the overlay's own function, a link function, a
        veneer forwarder."""
        if name in self.veneer_fwd:
            return self.veneer_fwd[name][1]
        head = self.link_heads.get(name)
        if head is not None:
            return signature(head[0], name, head[1])
        m = re.match(r"^%s_(.+)$" % re.escape(st.ov.tag), name)
        if m:
            unit = st.defs.get(m.group(1))
            if unit is not None:
                own = unit.scan.heads.get(m.group(1))
                if own is not None:
                    return signature(own, m.group(1), unit.scan.typedefs)
        return None

    def _own_names(self, st):
        """A name the unit declares with its own prototype keeps a name of
        its own when the name it would be merged with is declared
        differently.

        A unit may spell one function twice: the ARM9's name, which the
        game's header declares, and the unit's own alias, which the unit
        declares itself.  The port gives one address one name, so the two
        declarations of that name would conflict (the same name, two
        prototypes).  The spelling the unit declares itself keeps a name
        of its own, private to the unit, and the port forwards it to the
        target (write_dispatch): the unit's call still reaches the
        function.

        The headers' prototypes are not read (the scanner sees the unit's
        own file only), so a spelling the unit does not declare itself
        cannot be shown to agree with the unit's own: the unit's name is
        the one that keeps its own."""
        for u in st.units:
            u.inc_decls = closure.included_declarations(
                u.path, (os.path.join(closure.EIDOS_ROOT, "sdk"),))
            scope = set(u.scan.prototyped) | u.inc_decls
            groups = {}
            for n in sorted(scope):
                tgt = st.renames.get(n)
                if tgt is None or tgt == n:
                    continue
                groups.setdefault(tgt, []).append(n)
            for tgt, names in sorted(groups.items()):
                if len(names) < 2:
                    continue
                sigs = {}
                for m in names:
                    sig = self.own_signature(u, m)
                    if sig is not None:
                        sigs[m] = sig
                own = [n for n in names if n in u.scan.prototyped]
                if not own:
                    self.notes.append(
                        "%s %s: %s and %s are one function, and the port does "
                        "not read the headers' prototypes: a difference between "
                        "their declarations would not be seen" % (
                            st.ov.tag, u.rel, names[0], names[1]))
                    continue
                for n in own:
                    if n in u.scan.func_defs:
                        # The unit defines it: the definition is the
                        # authority, and a name of its own would leave the
                        # target undefined.
                        self.notes.append(
                            "%s %s: the unit defines %s, and %s is the name it "
                            "is merged with: the port does not read the headers' "
                            "prototypes, so a difference would not be seen" % (
                                st.ov.tag, u.rel, n, tgt))
                        continue
                    sig = sigs.get(n)
                    if sig is None:
                        self.notes.append(
                            "%s %s: the prototype of %s does not normalise: the "
                            "name keeps the one it has" % (st.ov.tag, u.rel, n))
                        continue
                    others = [m for m in names if m != n]
                    if all(m in sigs and sigs[m] == sig for m in others):
                        continue        # every other declaration agrees
                    u.shares[n] = ("%s__%s" % (self.own(st, n),
                                               flat_name(os.path.basename(u.rel))),
                                   tgt, sig)

    def declared_addr(self, st, name, sc):
        """The declared overlay address of `name`, or None.  The key is the
        (region, address) pair, never the bare address."""
        decl = self.declared.get(st.ov.tag)
        if not decl:
            return None
        addr = self.address(name, sc, st)
        if addr is None:
            return None
        addr &= ~1
        return addr if addr in decl else None

    def slot_name(self, st, addr):
        return "bw_slot_%s_%08X" % (st.ov.tag, addr)

    def _mod_slots(self):
        """The declared overlay rows and the slot each one gets: the
        indirection at the call site (docs/mod-interface.md, section 2) and,
        for a row that carries a stable event, the event's enter and leave
        around the call (docs/mod-implementation.md, section 1).  A row whose
        definition the link does not carry has no slot; it is reported and
        skipped."""
        for oid in self.ids:
            st = self.state[oid]
            decl = self.declared.get(st.ov.tag, {})
            for addr in sorted(decl):
                dname = decl[addr]
                dn, sig = self.target_signature(st, addr, dname)
                proto = None
                if dn is not None:
                    if sig is None:
                        # The definition's prototype does not normalise: a
                        # slot cannot forward, so the row is refused
                        # (docs/mod-interface.md, section 2).
                        self.notes.append(
                            "%s: declared 0x%08X (%s): the prototype of %s does "
                            "not normalise" % (st.ov.tag, addr, dname, dn))
                        continue
                    original = self.own(st, dn)
                    # The prototype the manifest's `sig=` is compared with
                    # (docs/mod-overrides.md, "The prototype is checked").
                    proto = proto_text(sig)
                elif addr in st.stubs or addr in st.known:
                    # A stub: the slot calls it with no arguments.
                    original = self.own_code(st, addr)
                    sig = ("void", ())
                    # The game's own prototype is not known: the row
                    # publishes none, and the manifest's `sig=` is not
                    # compared with it.
                else:
                    self.notes.append("%s: declared 0x%08X (%s) is not in the link"
                                      % (st.ov.tag, addr, dname))
                    # No slot: a caller already routed to it calls the
                    # function itself again.
                    slot = self.slot_name(st, addr)
                    for n in [k for k, v in st.slot_names.items() if v == slot]:
                        del st.slot_names[n]
                    continue
                events = self.declared_events.get((st.ov.tag, addr), [])
                if events and len(sig[1]) > BW_EVENT_ARGS_MAX:
                    # The event's call carries the arguments as words, and
                    # there are only so many: the row is reported, and the
                    # slot keeps the plain indirection.
                    self.event_notes.append(
                        "%s: declared 0x%08X (%s): %d arguments, more than the "
                        "event layer's %d: the event does not fire here"
                        % (st.ov.tag, addr, dname, len(sig[1]), BW_EVENT_ARGS_MAX))
                    events = []
                st.mod_slots.append({
                    "name": self.slot_name(st, addr),
                    "addr": addr,
                    "declared": dname,
                    "original": original,
                    "sig": sig,
                    "proto": proto,
                    "events": events,
                })

    def game_typedefs(self):
        """The game's own files, and the typedef names each one carries.

        A file carries the typedefs it declares itself and the ones the
        headers it includes declare (Scan.include_typedefs), so the map is
        the game's own type names as that file sees them.  The port's own
        sources are not the game's type names and are left out."""
        out = []
        for oid in self.ids:
            st = self.state[oid]
            for u in st.units:
                if self.rel_of(u.path) is not None:
                    out.append((u.path, u.scan.typedefs))
        for sc in self.link_scans:
            if self.rel_of(sc.path) is not None:
                out.append((sc.path, sc.typedefs))
        return out

    def game_types(self):
        """Every type name the game's own headers define, and the C type
        each resolves to.

        The port's normaliser cannot read the game's headers (it is a
        separate program, and the tree is not there at run time), so the
        generator publishes the resolution of every name it meets here and
        the port reads the published table (docs/mod-overrides.md, "The
        prototype is checked").  Every name the game defines is published,
        not only the ones the declared rows' prototypes use, so a stated
        prototype that names a game type is compared whatever function it
        names.

        A name is published when every file that carries it resolves it the
        same way.  A name two files resolve differently is a conflict: the
        port reads one answer, so the generator says so and keeps the name
        out of the table; a stated prototype that names it is then reported
        and not compared, and there is no false refusal.  A name the port
        cannot read at all (a float, the host's own pointer-width
        integers) is not published either, and the port reports a stated
        prototype that names it.

        The table holds at most BW_MOD_TYPES_MAX names, the manifest
        layer's limit: a name that does not fit is not read, the generator
        says so, and the port reports a stated prototype that names it."""
        if self.types is not None:
            return self.types
        met = TypeNames()
        where = {}
        for path, typedefs in self.game_typedefs():
            for name in typedefs:
                if not IDENT.fullmatch(name):
                    continue            # a tagged body's key, not a name
                r = _name_type(name, typedefs)
                if r is None:
                    continue            # the port cannot read this one
                met[name] = r           # TypeNames holds the conflicts
                where.setdefault(name, {}).setdefault(r, []).append(path)
        rows = []
        for name in sorted(met):
            if name in met.conflicts:
                # A conflict: the files do not agree, and the port reads one
                # answer, so the name is kept out of the table.
                got = where.get(name, {})
                seen = []
                for r in sorted(got):
                    seen.append("%s (%s)" % (r, os.path.basename(got[r][0])))
                self.notes.append("the game's type name %s resolves to %s: the "
                                  "files do not agree, and the name is not "
                                  "published, so a stated prototype that names "
                                  "it is reported and not compared"
                                  % (name, " and to ".join(seen)))
                continue
            rows.append((name, met[name]))
        if len(rows) > BW_MOD_TYPES_MAX:
            dropped = rows[BW_MOD_TYPES_MAX:]
            self.notes.append("the game's type names hold %d names, %d fit: %d "
                              "are not read (the first is %s)"
                              % (len(rows), BW_MOD_TYPES_MAX, len(dropped),
                                 dropped[0][0]))
            rows = rows[:BW_MOD_TYPES_MAX]
        self.types = sorted(rows)
        return self.types

    def arm9_canonical(self, addr):
        """The name the ARM9's own matched files define at `addr` (an
        asm-bodied file's, say), else the SDK's or the runtime's, else
        None."""
        names = self.al.arm9_at.get(addr & ~1)
        if names:
            return names[0]
        return self.al.sdk_at.get(("arm9", addr & ~1))

    def code_symbol(self, st, value):
        """The host symbol for a literal code address, or None if the value is
        not known to be code."""
        e = value & ~1
        thumb = value & 1
        if st.ov.owns(e):
            if e in st.at or (e in st.known) or (thumb and e < st.ov.code_end):
                return self.own_code(st, e)
            return None
        if self.al.in_any_window(e):
            if thumb or any(e in self.al.ov_names.get(w, {}) for w in self.al.windows(e)):
                name = "func_%08X" % e
                self.tramps.setdefault(name, e)
                return name
            return None
        n = self.link_at.get(e)
        if n is not None and (thumb or e in self.al.arm9_at):
            return n
        return None

    def _data_name(self, st, n, addr, sc):
        e = st.data.get(n)
        if e is None:
            e = {"name": n, "addr": addr, "conflict": False}
            st.data[n] = e

    def _link_refs(self):
        """Names the rest of the link uses inside an overlay window; and the
        names its matched sources use that it does not define, which the
        boot link stubs itself (tools/bootlink.py), so ovlink must not."""
        self.link_open = set()
        self.link_open_at = set()
        for sc in self.link_scans:
            refs, _imp = sc.references(self.universe)
            for n in refs:
                if n in self.link_defs:
                    continue
                addr = self.address(n, sc)
                if self.rel_of(sc.path):
                    self.link_open.add(n)
                    if addr is not None:
                        self.link_open_at.add(addr & ~1)
                if addr is None or addr < CONST_LIMIT or not self.al.in_any_window(addr):
                    continue
                if sc.is_code(n):
                    self.tramps.setdefault(n, addr & ~1)

    # -- data and relocations ------------------------------------------------

    def port_object_at(self, addr):
        best = None
        for n, (a, size, _o) in sorted(self.exclusions.items()):
            if a is None or n not in self.link_defs:
                continue
            if a == addr or (size and a <= addr < a + size):
                key = (a, 1 if size else 0)
                if best is None or key > (best[1], 1 if best[2] else 0):
                    best = (n, a, size)
        return best

    def _data(self, st):
        ov = st.ov
        conflicts = set()
        for u in st.units:
            conflicts |= u.scan.conflicts
        bounds = sorted({e["addr"] for e in st.data.values()} | {ov.lo, ov.bss_start, ov.hi})
        for n, e in sorted(st.data.items()):
            a = e["addr"]
            e["section"] = ("data" if ov.lo <= a < ov.bss_start else
                            "bss" if ov.bss_start <= a < ov.hi else
                            "const" if a < CONST_LIMIT else
                            "overlay" if self.al.in_any_window(a) else "other")
            port = self.port_object_at(a) if a >= CONST_LIMIT else None
            if n in conflicts and a >= CONST_LIMIT and port is None:
                nxt = [b for b in bounds if b > a]
                size = min((nxt[0] - a) if nxt else 0x100, 0x1000)
                e["bind"] = "OBJECT"
                e["size"] = size
                st.renames[n] = self.own(st, n)
                st.objects[n] = e
                continue
            if a < CONST_LIMIT:
                e["bind"] = "CONST"
            elif port is not None:
                e["bind"] = "HOST"
                e["host"] = port
            else:
                e["bind"] = "DS"
            e["size"] = 0
        # Pointer words: the listing's annotations and data_objects.json.
        words = {loc: tgt for loc, (_s, tgt) in st.listing.items()}
        syms = {loc: s for loc, (s, _t) in st.listing.items()}
        for loc, tgt in self.al.data_object_relocs(ov.id).items():
            words.setdefault(loc, tgt)
        for loc in sorted(words):
            if not (ov.lo <= loc < ov.code_end):
                continue
            tgt = words[loc]
            sym = syms.get(loc, "")
            e = tgt & ~1
            is_code = bool(tgt & 1) or ADDR_FUNC.match(sym) is not None \
                or e in st.known or e in self.al.arm9_at
            r = {"where": loc, "target": tgt}
            if is_code:
                r["kind"] = "CODE"
                if ov.owns(e):
                    r["fn"] = self.own_code(st, e)
                elif self.al.in_any_window(e):
                    name = "func_%08X" % e
                    self.tramps.setdefault(name, e)
                    r["fn"] = name
                else:
                    r["fn"] = self.link_at.get(e)
            else:
                port = self.port_object_at(tgt)
                if port is not None:
                    r["kind"] = "HOST"
                    r["anchor"] = port
                else:
                    r["kind"] = "DATA"
            st.relocs.append(r)
        # Static initialisers.
        for a in overlay_table.KNOWN_SINIT.get(ov.id, [])[:ov.sinit_count]:
            st.sinits.append(self.own_code(st, a & ~1))

    def declared_signature(self, rid, addr, dname):
        """The normalised prototype of a declared row whose overlay is not
        compiled, read from its matched source (aletheia's metadata), so a
        trampoline over the address can call an override with the right
        arguments."""
        for rel in self.al.ov_sources.get(rid, []):
            rows = self.al.src_names.get(rel)
            if not rows or (dname, addr) not in rows:
                continue
            path = os.path.join(self.root, rel)
            if not os.path.exists(path):
                continue
            sc = Scan(path, read_text(path))
            head = sc.heads.get(dname)
            if head is not None:
                return signature(head, dname, sc.typedefs)
        return None

    def _finish_stubs(self):
        """Every known function of a compiled overlay that nothing defines is a
        stub; and every trampoline's target in a compiled overlay exists."""
        for name, addr in self.tramps.items():
            for w in self.al.windows(addr):
                if w in self.state:
                    self.own_code(self.state[w], addr)
        for st in self.state.values():
            for a in sorted(st.known):
                if st.ov.owns(a) and a not in st.at:
                    self.stub_name(st, a & ~1)

    # -- trampolines ---------------------------------------------------------

    def target_signature(self, st, addr, dname=None):
        n = st.at.get(addr)
        if n is None and dname in st.defs:
            # The declared row's own name, defined here but with no address
            # in the manifests yet (a src/game function its header does not
            # list): the row names it.
            n = dname
        if n is None:
            return None, None
        sc = st.defs[n].scan
        head = sc.heads.get(n)
        if head is None:
            return n, None
        return n, signature(head, n, sc.typedefs)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

GEN = "Generated by tools/ovlink.py; do not edit."


def write_overlay_header(link, st, path):
    ov = st.ov
    lines = [
        "/*",
        " * %s" % GEN,
        " *",
        " * Forced include for every matched source of overlay %d (docs/overlays.md):" % ov.id,
        " * its own names become %s_<name>, and every data name it uses is a" % ov.tag,
        " * pointer variable of this overlay that sdk/overlay_image.c binds when the",
        " * overlay is loaded.",
        " */",
        "#ifndef BW_OVL_%s_H" % ov.tag.upper(),
        "#define BW_OVL_%s_H" % ov.tag.upper(),
        "",
        "/* A few SDK files mark a prototype __attribute__((pure)); cl.exe has",
        " * no __attribute__, and the mark changes nothing the host relies on. */",
        "#if defined(_MSC_VER) && !defined(__clang__) && !defined(__attribute__)",
        "#define __attribute__(x)",
        "#endif",
        "",
        "/* Names inside the overlay's window 0x%08X..0x%08X. */" % (ov.lo, ov.hi),
    ]
    for n in sorted(st.renames):
        if st.renames[n] != n:
            lines.append("#define %s %s" % (n, st.renames[n]))
    binds = [n for n in sorted(st.data) if st.data[n]["bind"] != "OBJECT"
             and n not in st.renames]
    if binds:
        lines += ["", "/* Data names: pointer variables bound at load. */"]
        for n in binds:
            e = st.data[n]
            lines.append("#define %s (*bw_dsref_%s_%s)   /* %s %s */"
                         % (n, ov.tag, n, hx(e["addr"]), e["section"]))
    lines += ["", "#endif", ""]
    write_if_changed(path, "\n".join(lines))


def write_unit_header(link, u, ov_header, path):
    st = link.state[u.oid]
    lines = ["/* %s  Overlay %d, %s. */" % (GEN, u.oid, u.rel),
             '#include "%s"' % ov_header.replace("\\", "/")]
    for n in sorted(u.private):
        lines.append("#undef %s" % n)
        lines.append("#define %s %s   /* another file of the overlay owns %s */"
                     % (n, u.private[n], n))
    for n in sorted(u.shares):
        # The unit declares this name with its own prototype, and the name it
        # shares the target with is declared differently: the unit's own
        # spelling keeps its own name, and the forwarder in ovl_dispatch.c
        # calls the target (tools/ovlink.py, Link._own_names).
        lines.append("#undef %s" % n)
        lines.append("#define %s %s   /* the unit's own name for %s: its "
                     "prototype differs from the name it shares */"
                     % (n, u.shares[n][0], n))
    for n in sorted(st.slot_names):
        if st.defs.get(n) is u and n in u.self_calls:
            # The unit calls it itself: the name stays the slot's here too,
            # and the unit's copy names the definition the overlay's own way
            # (the .cut file's renames).
            continue
        if st.defs.get(n) is u:
            # The overlay header renames a declared name to its slot for a
            # non-owning caller; the defining unit keeps the overlay's own
            # name, so its definition is not renamed away
            # (docs/mod-interface.md, section 2).
            lines.append("#undef %s" % n)
            lines.append("#define %s %s   /* the defining unit keeps its own name */"
                         % (n, link.own(st, n)))
    for n in sorted(u.builtins):
        port, decl = MWCC_BUILTINS[n]
        if n not in u.builtins_declared:
            lines.append(decl)
        lines.append("#define %s %s" % (n, port))
    if u.literal_table:
        fn = "bw_ovl_ds_ptr_%s" % st.ov.tag
        lines.append("/* Code addresses this file writes as literals resolve to host")
        lines.append(" * functions (tools/ovlink.py, step 6). */")
        lines.append("void *%s(unsigned int addr);" % fn)
        lines.append("#define bw_ds_ptr %s" % fn)
    inc_decls = u.inc_decls
    for n in sorted(u.protos):
        if n in inc_decls:
            # The file's own `#include` header declares this name; the
            # prototype here would conflict with that declaration.  Same
            # reading tools/bootlink.py does for the boot's units.
            continue
        if n not in u.cuts and ((u.scan is not None and n in u.scan.func_defs)
                                or n in _defined_in(u.path)):
            # Defined in this file: its own definition is its prototype, and
            # an old-style one here would conflict with it
            # (btlv_core.c's BTLV_CORE_StartPrintMessage is void).
            continue
        rt = u.protos[n]
        lines.append("%s %s();" % (rt, n))
    write_if_changed(path, "\n".join(lines) + "\n")


_DEFINED_IN = {}


def _defined_in(path):
    """The functions a source file defines, by tools/gamecut.py's reading
    (brace matching over the masked text): a Scan can lose its place in a
    long unit (ov093/btlv_core.c) and miss the later definitions."""
    if path not in _DEFINED_IN:
        import gamecut
        try:
            _DEFINED_IN[path] = {f[0] for f in gamecut.functions(read_text(path))}
        except OSError:
            _DEFINED_IN[path] = set()
    return _DEFINED_IN[path]


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def write_overlay_c(link, st, path):
    ov = st.ov
    out = []
    w = out.append
    w("/*")
    w(" * %s" % GEN)
    w(" *")
    w(" * Overlay %d (0x%08X..0x%08X): the stubs for the functions this build" % (ov.id, ov.lo, ov.hi))
    w(" * lacks, the data image sdk/overlay_image.c copies, binds and relocates at")
    w(" * load, and the static initialisers.  Addresses, sizes and names only.")
    w(" */")
    w("")
    w("#include <stdint.h>")
    w("")
    w('#include "armdata.h"')
    w('#include "dsmem.h"')
    w('#include "overlay_image.h"')
    w('#include "mod_slot.h"')
    w('#include "mod_hooks.h"')
    w('#include "mod_event.h"')
    w("")
    # Stubs.
    if st.stubs:
        w("/* Functions of the overlay no compiled source defines: reaching one")
        w(" * panics with the overlay and the address. */")
        for a in sorted(st.stubs):
            n = link.own(st, st.stubs[a])
            w("void %s(void);" % n)
            w("void %s(void) { bw_ovl_stub(%du, 0x%08Xu); }" % (n, ov.id, a))
        w("")
    # Functions the relocations, the literals and the initialisers name.
    funcs = {}
    for r in st.relocs:
        if r["kind"] == "CODE":
            funcs[r["target"]] = r.get("fn")
    stub_names = {link.own(st, n) for n in st.stubs.values()}
    slot_orig = {s["original"]: s for s in st.mod_slots if s["original"] not in stub_names}
    externs = sorted({n for n in list(funcs.values()) + list(st.code_literals.values()) + st.sinits
                      if n and n not in stub_names and n not in slot_orig})
    if externs:
        w("/* Functions defined elsewhere in the build that the data, the literals")
        w(" * or the initialisers point at.  Only the address is taken. */")
        for n in externs:
            w("extern void %s(void);" % n)
        w("")
    if slot_orig:
        w("/* The declared originals: normalised prototypes of their")
        w(" * definitions, so a slot can forward its arguments. */")
        for n in sorted(slot_orig):
            ret, params = slot_orig[n]["sig"]
            w("%s" % proto_decl(ret, n, params))
        w("")

    def fnref(n):
        """A function name as a BwDataFn value.  A declared original is
        declared with its real prototype (so the slot can call it), which is
        not BwDataFn; the cast here is the one the table needs.  Every other
        name is declared void(void) already."""
        return "(BwDataFn)%s" % n if n in slot_orig else n
    # The declared slots: the indirection a non-owning caller goes through.
    if st.mod_slots:
        w("/* The declared slots of this overlay (docs/mod-interface.md,")
        w(" * section 2).  A caller inside the window that is not the defining")
        w(" * unit compiles to a call to one of these; its default is the")
        w(" * original, so with no mod installed it behaves as the direct call")
        w(" * did. */")
        for s in st.mod_slots:
            w("%s" % slot_function("BW_MOD_REGION_OVL(%d)" % ov.id, s["addr"], s))
        w("")
    traps = sorted({t & ~1 for t, n in funcs.items() if not n})
    for t in traps:
        w("static void bw_trap_%08X(void) { bw_data_trap(0x%08Xu); }" % (t, t))
    if traps:
        w("")
    # Anchors.
    anchors = {}
    for e in st.data.values():
        if e["bind"] == "HOST":
            anchors[e["host"][0]] = (e["host"][1], e["host"][2])
    for r in st.relocs:
        if r["kind"] == "HOST":
            anchors[r["anchor"][0]] = (r["anchor"][1], r["anchor"][2])
    for n in sorted(anchors):
        w("extern char %s[];" % n)
    if anchors:
        w("")
    refs = [n for n in sorted(st.data) if st.data[n]["bind"] != "OBJECT"]
    for n in refs:
        w("void *bw_dsref_%s_%s;" % (ov.tag, n))
    if refs:
        w("")
    objs = sorted(st.objects)
    for n in objs:
        e = st.objects[n]
        w("BwDataCell %s[BW_DATA_CELLS(0x%Xu)];   /* %s: used as a member, tag, local "
          "or in an initialiser */" % (link.own(st, n), e["size"], hx(e["addr"])))
    if objs:
        w("")
    w("static const BwDataName k_names[] = {")
    for n in sorted(st.data, key=lambda k: (st.data[k]["addr"], k)):
        e = st.data[n]
        ref, anchor, aaddr = "&bw_dsref_%s_%s" % (ov.tag, n), "NULL", 0
        if e["bind"] == "HOST":
            anchor, aaddr = e["host"][0], e["host"][1]
        elif e["bind"] == "OBJECT":
            ref, anchor, aaddr = "NULL", link.own(st, n), e["addr"]
        w("    { %s, 0x%08Xu, %s, BW_DATA_BIND_%s, %s, 0, 0x%Xu, %s, 0x%08Xu }," % (
            c_str(n), e["addr"], ref, e["bind"], armdata.SECTION_ENUM[e["section"]],
            e["size"], anchor, aaddr))
    if not st.data:
        w("    { NULL, 0, NULL, 0, 0, 0, 0, NULL, 0 }")
    w("};")
    w("")
    w("static const BwDataRange k_ranges[] = {")
    w("    { 0x%08Xu, 0x%Xu, 0u, 1 },   /* code and data, from the overlay's file */"
      % (ov.lo, ov.code_size))
    nranges = 1
    if ov.bss_size:
        w("    { 0x%08Xu, 0x%Xu, 0u, 0 },   /* .bss */" % (ov.bss_start, ov.bss_size))
        nranges += 1
    w("};")
    w("")
    fn_index = {}
    w("static const BwDataFunc k_funcs[] = {")
    for i, t in enumerate(sorted(funcs)):
        fn_index[t] = i
        n = funcs[t]
        if n:
            w("    { 0x%08Xu, %s, %s }," % (t, fnref(n), c_str(n)))
        else:
            w("    { 0x%08Xu, bw_trap_%08X, NULL }," % (t, t & ~1))
    if not funcs:
        w("    { 0, NULL, NULL }")
    w("};")
    w("")
    anchor_index = {}
    w("static const BwDataAnchor k_anchors[] = {")
    for i, n in enumerate(sorted(anchors)):
        anchor_index[n] = i
        a, size = anchors[n]
        w("    { 0x%08Xu, 0x%Xu, %s, %s }," % (a, size or 0, n, c_str(n)))
    if not anchors:
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    w("static const BwDataReloc k_relocs[] = {")
    for r in st.relocs:
        idx = 0
        if r["kind"] == "CODE":
            idx = fn_index[r["target"]]
        elif r["kind"] == "HOST":
            idx = anchor_index[r["anchor"][0]]
        w("    { 0x%08Xu, 0x%08Xu, BW_DATA_RELOC_%s, %u }," % (r["where"], r["target"], r["kind"], idx))
    if not st.relocs:
        w("    { 0, 0, 0, 0 }")
    w("};")
    w("")
    w("/* Host objects for names that cannot be macros: filled at load and")
    w(" * registered at their address, withdrawn when another overlay takes the")
    w(" * window (sdk/overlay_image.c); docs/overlays.md, \"Data\". */")
    w("static const BwDataObject k_objects[] = {")
    for n in objs:
        e = st.objects[n]
        w("    { 0x%08Xu, 0x%Xu, %s, %s }," % (e["addr"], e["size"], link.own(st, n), c_str(n)))
    if not objs:
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    w("static const BwDataImage k_image = {")
    w("    %s, %d," % (c_str(ov.tag), ov.id))
    w("    k_ranges, %du," % nranges)
    w("    k_names, %du," % len(st.data))
    w("    k_relocs, %du," % len(st.relocs))
    w("    k_funcs, %du," % len(funcs))
    w("    k_anchors, %du," % len(anchors))
    w("    k_objects, %du," % len(objs))
    w("    NULL, 0u")
    w("};")
    w("")
    # Static initialisers.
    w("static const BwDataFn k_sinits[] = {")
    for n in st.sinits:
        w("    %s," % fnref(n))
    if not st.sinits:
        w("    NULL")
    w("};")
    w("")
    # The code-literal translator.
    if st.code_literals:
        fn = "bw_ovl_ds_ptr_%s" % ov.tag
        w("/* bw_ds_ptr for the sources of this overlay that write code addresses")
        w(" * as literals (tools/ovlink.py, step 6). */")
        w("void *%s(unsigned int addr);" % fn)
        w("void *%s(unsigned int addr)" % fn)
        w("{")
        w("    switch (addr) {")
        for v in sorted(st.code_literals):
            w("    case 0x%08Xu: return (void *)(uintptr_t)%s;" % (v, st.code_literals[v]))
        w("    default: return bw_ds_ptr(addr);")
        w("    }")
        w("}")
        w("")
    # Every function of the overlay by the value a code pointer to it holds,
    # for the ARM9 data's pointers into the window (sdk/overlay_image.c,
    # bw_ovl_code_entry).
    entries = {}
    for a, n in st.at.items():
        entries[a | (1 if a in st.thumb else 0)] = link.own(st, n)
    for a, n in st.stubs.items():
        entries.setdefault(a | (1 if a in st.thumb else 0), link.own(st, n))
    declared = set(stub_names) | set(externs) | set(slot_orig)
    fresh = sorted(set(entries.values()) - declared)
    if fresh:
        w("/* The overlay's functions, for bw_ovl_code_entry; only the address is")
        w(" * taken here. */")
        for n in fresh:
            w("extern void %s(void);" % n)
        w("")
    w("static const BwDataFunc k_entries[] = {")
    for v in sorted(entries):
        w("    { 0x%08Xu, %s, %s }," % (v, fnref(entries[v]), c_str(entries[v])))
    if not entries:
        w("    { 0, NULL, NULL }")
    w("};")
    w("")
    # The declared originals, published when this window loads
    # (sdk/overlay_image.c, provider_load), so a wrap override can call them.
    if st.mod_slots:
        w("/* The declared originals of this overlay, published when its window")
        w(" * loads (sdk/overlay_image.c, provider_load).  ovl_dispatch.c's")
        w(" * bw_ovl_mod_publish picks the table by overlay id. */")
        w("static const BwModSlotOriginal k_mod_originals[] = {")
        for s in st.mod_slots:
            w("    { %du, 0x%08Xu, (void *)%s }," % (ov.id, s["addr"], s["original"]))
        w("};")
        w("")
        w("void bw_ovl_mod_publish_%s(void)" % ov.tag)
        w("{")
        w("    bw_mod_slot_publish_table(k_mod_originals, %du);" % len(st.mod_slots))
        w("}")
        w("")
    w("const BwOvlImage bw_ovl_%s = {" % ov.tag)
    w("    %du, &k_image, k_sinits, %du, %du, %du," % (
        ov.id, len(st.sinits), len(st.at), len(st.stubs)))
    w("    k_entries, %du" % len(entries))
    w("};")
    w("")
    write_if_changed(path, "\n".join(out))


def write_dispatch(link, path, set_name):
    out = []
    w = out.append
    w("/*")
    w(" * %s" % GEN)
    w(" *")
    w(" * The overlays compiled into this executable, and the trampolines: every")
    w(" * plain name a source outside an overlay's window uses for an address")
    w(" * inside it.  A trampoline calls the function of the overlay resident")
    w(" * over its address, or panics naming what is resident")
    w(" * (docs/overlays.md, \"Calls into a window\").  Pointer arguments travel as")
    w(" * void *, which every target eidos builds for passes like any other")
    w(" * pointer.")
    w(" */")
    w("")
    w('#include "overlay_image.h"')
    w('#include "mod_slot.h"')
    w('#include "mod_hooks.h"')
    w('#include "mod_event.h"')
    w("")
    if link.veneer_fwd:
        # Linker veneers the overlays call: each is another address of its
        # target, so its forwarder calls the target with the arguments and
        # returns what it returns (the target's own prototype, normalised).
        w("/* ---- linker veneers: an ARM9 address of their target ---- */")
        w("")
        for fwd in sorted(link.veneer_fwd):
            tname, (ret, params) = link.veneer_fwd[fwd]
            for tag in sorted({t for p in params + (ret,)
                               for t in re.findall(r"\bstruct\s+([A-Za-z_]\w*)", p)}):
                w("struct %s;" % tag)
            w(proto_decl(ret, tname, params))
            w("%s;" % proto(ret, fwd, params))
            w(proto(ret, fwd, params))
            w("{")
            args = ", ".join("a%d" % k for k in range(len(params)))
            w("    %s%s(%s);" % ("" if ret == "void" else "return ", tname, args))
            w("}")
            w("")
    # The names a unit keeps for itself: a unit that spells one function
    # twice (the ARM9's name and its own alias) would get one name for both,
    # and the two declarations of that name would conflict.  The unit's own
    # name keeps its own, and this forwarder calls the target with the
    # unit's own arguments (tools/ovlink.py, Link._own_names).
    fwds = []
    for oid in link.ids:
        st = link.state[oid]
        for u in st.units:
            for n in sorted(u.shares):
                private, target, sig = u.shares[n]
                fwds.append((private, target, sig, st, u, n))
    if fwds:
        w("/* ---- the names a unit keeps for itself ---- */")
        w("")
        for private, target, sig, st, u, n in fwds:
            ret, params = sig
            # The target's own prototype, normalised; the alias's when the
            # link does not say (a stub the overlay lacks): the call site's
            # declaration is the authority for the arguments.
            tsig = link.host_signature(st, target) or sig
            tret, tparams = tsig
            for tag in sorted({t for p in tparams + (tret,)
                               for t in re.findall(r"\bstruct\s+([A-Za-z_]\w*)", p)}):
                w("struct %s;" % tag)
            w(proto_decl(tret, target, tparams))
            w("%s;" % proto(ret, private, params))
            w(proto(ret, private, params))
            w("{")
            call = ", ".join(
                (("a%d" % k) if t == params[k] and str(t) == str(params[k])
                 else word_cast(t, word_bits(params[k], "a%d" % k)))
                if k < len(params) else zero_of(t)
                for k, t in enumerate(tparams))
            if ret == "void" or tret == "void":
                w("    %s(%s);" % (target, call))
                if ret != "void":
                    w("    return %s;" % zero_of(ret))
            elif str(ret) == str(tret):
                w("    return %s(%s);" % (target, call))
            else:
                w("    return %s;" % word_cast(ret, word_bits(tret, "%s(%s)" % (target, call))))
            w("}")
            w("")
    declared = {}
    body = []
    for name in sorted(link.tramps):
        addr = link.tramps[name]
        cands = link.al.windows(addr)
        sig = None
        targets = []
        for c in cands:
            st = link.state.get(c)
            if st is None:
                targets.append((c, None, None))
                continue
            dn, s = link.target_signature(st, addr)
            targets.append((c, link.own_code(st, addr) if dn else None, s))
            if s is not None and (sig is None or dn == name):
                # The overlay that defines the trampoline's own name gives
                # the prototype (SAVE_OUTSIDE_IsCorrupt is ov228's; ov183
                # has another function at 0x021DFE98).
                sig = s
        decl = link.declared_by_addr.get(addr)
        events = []
        if decl is not None:
            # A declared address: the declared function's prototype is the
            # contract, from aletheia's metadata when the overlay is not
            # compiled into this build.
            rid, dname, events = decl
            dsig = link.declared_signature(rid, addr, dname)
            if dsig is not None:
                sig = dsig
        if sig is None:
            sig = ("void", ())
            forward = False
        else:
            forward = True
        ret, params = sig
        args = ", ".join("a%d" % k for k in range(len(params)))
        key = "0x%08Xu" % addr
        reg = "BW_MOD_REGION_OVL(%d)" % decl[0] if decl is not None else None
        # A trampoline over a declared address is another route to the
        # function the row binds: the event fires here too, and the scope
        # stack names the row (docs/mod-implementation.md, section 1).
        ev = bool(decl is not None and events and forward)
        if decl is not None and events and not forward:
            link.event_notes.append(
                "trampoline %s at 0x%08X: the declared row's prototype does not "
                "normalise, so the event does not fire here" % (name, addr))
        if decl is not None and events and forward and len(params) > BW_EVENT_ARGS_MAX:
            link.event_notes.append(
                "trampoline %s at 0x%08X: %d arguments, more than the event "
                "layer's %d: the event does not fire here"
                % (name, addr, len(params), BW_EVENT_ARGS_MAX))
            ev = False
        cargs = event_call_args(params) if ev else args

        def leave(expr, indent):
            """The lines a trampoline leaves with: the event's leave, the
            scope's pop, and the return.  `expr` is the call's result, or
            None for a call that returns nothing.  The result is held
            before the scope pops, so the pop happens after the call."""
            pad = " " * indent
            out = []
            if expr is not None:
                if ev:
                    out.append("%scall.ret = %s;" % (pad, event_result(ret, expr)))
                elif decl is not None:
                    out.append("%sr = %s;" % (pad, expr))
            if ev:
                out.append("%sbw_mod_event_leave(%s, %s, &call, &payload);"
                           % (pad, reg, key))
            if decl is not None:
                out.append("%sbw_mod_event_pop();" % pad)
            if expr is None:
                out.append("%sreturn;" % pad)
            elif ev:
                out.append("%sreturn %s;" % (pad, event_value(ret)))
            elif decl is not None:
                out.append("%sreturn r;" % pad)
            else:
                out.append("%sreturn %s;" % (pad, expr))
            return out

        body.append("/* %s at 0x%08X: %s */" % (
            name, addr, ", ".join("ov%03d" % c for c in cands) or "no overlay"))
        body.append("%s;" % proto(ret, name, params))
        body.append(proto(ret, name, params))
        body.append("{")
        if ev:
            body.append("    BwEventCall    call = %s;" % event_call_init(params))
            body.append("    BwEventPayload payload;")
        if forward and decl is not None:
            # A declared address: an installed override wins over whichever
            # overlay is resident (docs/mod-interface.md, section 2).
            body.append("    void *h = bw_mod_slot_lookup(%s, %s);" % (reg, key))
        if decl is not None and ret != "void" and not ev:
            # The result is held, so the scope pops after the call.
            body.append("    %s%s" % (ret, "r;" if ret.endswith("*") else " r;"))
        if decl is not None:
            body.append("    bw_mod_event_push(%s, %s);" % (reg, key))
        if ev:
            body.append("    if (bw_mod_event_enter(%s, %s, &call, &payload)) {" % (reg, key))
            body.append("        bw_mod_event_pop();")
            if ret == "void":
                body.append("        return;")
            else:
                body.append("        return %s;" % event_value(ret))
            body.append("    }")
        if forward and decl is not None:
            # The registry asks the switch, per hook: a hook whose switch is
            # off is skipped, and the walk goes on inward (docs/mod-overrides.md,
            # "The switch").
            body.append("    if (h != NULL) {")
            if ret == "void":
                body.append("        ((void (*)(%s))h)(%s);"
                            % (", ".join(map(str, params)) or "void", cargs))
                body += leave(None, 8)
            else:
                body += leave("((%s (*)(%s))h)(%s)"
                              % (ret, ", ".join(map(str, params)) or "void", cargs), 8)
            body.append("    }")
        body.append("    int ov = bw_ovl_resident_at(%s);" % key)
        for c, sym, s in targets:
            if c not in link.state:
                continue
            st = link.state[c]
            if sym is None:
                body.append("    if (ov == %d) {" % c)
                body.append("        bw_ovl_stub(%du, %s);" % (c, key))
                body.append("    }")
                continue
            if forward and s is not None and s != sig and _word_call(sig) and _word_call(s):
                # Another function at the same address in this overlay
                # (ov010 calls 0x021F385C as ov059's SP_POKE_GIMMICK_UpdatePairB;
                # leaving the player's house ov065 is resident there with
                # FIELD_GIMMICK_TIME_EFFECT_StartEffect).  The DS calls
                # whatever is resident with the caller's registers; the host
                # does the same with the caller's arguments as words, each
                # cast to the callee's type, missing ones zero.
                if sym not in declared:
                    declared[sym] = proto_decl(s[0], sym, s[1])
                wargs = ", ".join(
                    (word_cast(t, "(uintptr_t)%s" % word_bits(params[k], "a%d" % k))
                     if not isinstance(t, WordAggregate)
                     else word_cast(t, word_bits(params[k], "a%d" % k)))
                    if k < len(params) else zero_of(t)
                    for k, t in enumerate(s[1]))
                body.append("    if (ov == %d) {" % c)
                if ret == "void" or s[0] == "void":
                    body.append("        %s(%s);" % (sym, wargs))
                    body += leave(None if ret == "void" else zero_of(ret), 8)
                elif isinstance(ret, WordAggregate) or isinstance(s[0], WordAggregate):
                    body += leave(word_cast(ret, word_bits(s[0], "%s(%s)" % (sym, wargs))), 8)
                else:
                    body += leave("(%s)(uintptr_t)%s(%s)" % (ret, sym, wargs), 8)
                body.append("    }")
                continue
            if not forward or s != sig:
                body.append("    if (ov == %d) {" % c)
                body.append("        bw_ovl_dispatch_fail(%s, %s, ov, BW_OVL_WHY_SIGNATURE);"
                            % (key, c_str(name)))
                body.append("    }")
                continue
            if sym not in declared:
                declared[sym] = proto_decl(s[0], sym, s[1])
            body.append("    if (ov == %d) {" % c)
            if ret == "void":
                body.append("        %s(%s);" % (sym, cargs))
                body += leave(None, 8)
            else:
                body += leave("%s(%s)" % (sym, cargs), 8)
            body.append("    }")
        body.append("    bw_ovl_dispatch_fail(%s, %s, ov, ov < 0 ? BW_OVL_WHY_NONE "
                    ": BW_OVL_WHY_NOT_COMPILED);" % (key, c_str(name)))
        for k in range(len(params)):
            body.append("    (void)a%d;" % k)
        if decl is None and not ev:
            # A plain trampoline: the dispatch failed, and there is no scope
            # and no event to leave.
            if ret != "void":
                body.append("    return %s;" % zero_of(ret))
        else:
            body += leave(zero_of(ret) if ret != "void" else None, 4)
        body.append("}")
        body.append("")
    if declared:
        w("/* The overlay functions the trampolines forward to, as normalised")
        w(" * prototypes of their definitions. */")
        for sym in sorted(declared):
            w(declared[sym])
        w("")
    out += body
    for oid in link.ids:
        w("extern const BwOvlImage bw_ovl_ov%03d;" % oid)
    w("")
    w("static const BwOvlImage *const k_images[] = {")
    for oid in link.ids:
        w("    &bw_ovl_ov%03d," % oid)
    if not link.ids:
        w("    NULL")
    w("};")
    w("")
    w("/* The overlays of set %s: install with bw_ovl_install. */" % set_name)
    w("const BwOvlImageSet bw_ovl_set_%s = { k_images, %du };" % (set_name, len(link.ids)))
    w("")
    # Publish each compiled overlay's declared originals when it loads
    # (sdk/overlay_image.c, provider_load).  The per-overlay table lives in
    # ovNNN_link.c; this dispatches by id.  The dispatcher is registered at
    # start-up, so sdk/mod_slot.c's bw_ovl_mod_publish reaches it without a
    # link-time reference to the generated code (a link with no overlay has
    # none, and its registry stays empty).
    publishing = [oid for oid in link.ids if link.state[oid].mod_slots]
    if publishing:
        w("/* Publish the declared originals of a compiled overlay when its")
        w(" * window loads (sdk/overlay_image.c, provider_load). */")
        for oid in publishing:
            w("void bw_ovl_mod_publish_ov%03d(void);" % oid)
        w("")
        w("static void bw_ovl_mod_publish_dispatch(unsigned int id)")
        w("{")
        w("    switch (id) {")
        for oid in publishing:
            w("    case %du: bw_ovl_mod_publish_ov%03d(); break;" % (oid, oid))
        w("    default: break;")
        w("    }")
        w("}")
        w("")
        # The declared rows' names and their prototypes, published at
        # start-up: the manifest's check asks them as the mod loads
        # (docs/mod-overrides.md, "The prototype is checked").  The boot's
        # link file publishes every row's name; the overlays' rows add their
        # prototypes here, for the overlays this link compiles.
        w("/* The declared rows of the overlays this link compiles: their names")
        w(" * and their prototypes.  The name is the switch a hook on the row")
        w(" * answers by when the mod's manifest states none; the prototype is")
        w(" * the function's own, normalised the way tools/ovlink.py's signature")
        w(" * normalises it, and it is what the manifest's `sig=` is compared")
        w(" * with.  The boot's link file publishes the same rows' names. */")
        w("static const BwModSlotProto k_mod_proto_rows[] = {")
        for oid in publishing:
            st = link.state[oid]
            for s in st.mod_slots:
                w("    { BW_MOD_REGION_OVL(%d), 0x%08Xu, \"%s\", %s }," % (
                    oid, s["addr"], s["declared"],
                    "\"%s\"" % s["proto"] if s["proto"] is not None else "NULL"))
        w("};")
        w("")
    # The game's own type names, and the C type each resolves to: the table
    # the port's normaliser reads, so a manifest's `sig=` that names one of
    # them is read the way the build read the function's own prototype.
    types = link.game_types()
    if types:
        w("/* The game's own type names, and the C type each resolves to.")
        w(" *")
        w(" * The generator knows the game's headers, so it resolves a name the")
        w(" * port cannot read (WMErrCode, WMCallbackFunc, WMLinkLevel, ...): an")
        w(" * enum is an int, a struct by value is the one word the port carries")
        w(" * it in, a pointer and a function pointer are void *.  Every name the")
        w(" * game defines is here, not only the ones the declared rows use, so a")
        w(" * stated prototype that names a game type is compared whatever")
        w(" * function it names.  The port reads this table, so the two sides")
        w(" * reach the same answer")
        w(" * (docs/mod-overrides.md, \"The prototype is checked\").")
        w(" *")
        w(" * The declaration is repeated here, the way sdk/mod_manifest.c")
        w(" * repeats the port's own: the two must agree. */")
        w("typedef struct BwModType {")
        w("    const char *name;   /* the game's own type name */")
        w("    const char *type;   /* the C type it resolves to */")
        w("} BwModType;")
        w("extern void bw_mod_manifest_publish_types(const BwModType *rows,")
        w("                                      unsigned int count);")
        w("static const BwModType k_mod_type_rows[] = {")
        for name, ttype in types:
            w("    { %s, %s }," % (c_str(name), c_str(ttype)))
        w("};")
        w("")
    if publishing or types:
        w("#if defined(_MSC_VER) && !defined(__clang__)")
        w("/* cl.exe: a start-up initialiser in the C run-time's constructor")
        w(" * section. */")
        w("typedef void (*BwModCtor)(void);")
        w("static void bw_ovl_mod_register(void);")
        w("#pragma section(\".CRT$XCU\", read)")
        w("__declspec(allocate(\".CRT$XCU\")) BwModCtor bw_ovl_mod_ctor =")
        w("    bw_ovl_mod_register;")
        w("static void bw_ovl_mod_register(void)")
        w("{")
        if publishing:
            w("    bw_mod_slot_set_overlay_publish(bw_ovl_mod_publish_dispatch);")
            w("    bw_mod_publish_protos(k_mod_proto_rows, %du);"
              % sum(len(link.state[oid].mod_slots) for oid in publishing))
        if types:
            w("    bw_mod_manifest_publish_types(k_mod_type_rows, %du);" % len(types))
        w("}")
        w("#else")
        w("__attribute__((constructor)) static void bw_ovl_mod_register(void)")
        w("{")
        if publishing:
            w("    bw_mod_slot_set_overlay_publish(bw_ovl_mod_publish_dispatch);")
            w("    bw_mod_publish_protos(k_mod_proto_rows, %du);"
              % sum(len(link.state[oid].mod_slots) for oid in publishing))
        if types:
            w("    bw_mod_manifest_publish_types(k_mod_type_rows, %du);" % len(types))
        w("}")
        w("#endif")
        w("")
    write_if_changed(path, "\n".join(out))


def write_arm9_stubs(link, path, enabled):
    out = ["/*", " * %s" % GEN, " *",
           " * ARM9 functions the overlays call that nothing else in this link",
           " * defines: each panics with its address and name.  Only written for a",
           " * link without the boot (ovlink.py --arm9-stubs).", " */", "",
           '#include "overlay_image.h"', ""]
    if enabled:
        for n in sorted(link.arm9_stub):
            if n in link.link_open or n in link.tramps:
                continue          # the boot link's own stub, or a trampoline
            a = link.arm9_stub[n]
            out.append("void %s(void);" % n)
            out.append("void %s(void) { bw_ovl_arm9_stub(0x%08Xu, %s); }"
                       % (n, a or 0, c_str(n)))
    else:
        out.append("typedef int bw_ovl_no_arm9_stubs;")
    out.append("")
    write_if_changed(path, "\n".join(out))


def write_report(link, path):
    r = []
    p = r.append
    tot_units = sum(len(st.units) for st in link.state.values())
    tot_slots = sum(len(st.mod_slots) for st in link.state.values())
    ev_slots = sum(1 for st in link.state.values() for s in st.mod_slots
                    if s.get("events"))
    p("ovlink: %d overlays, %d translation units, %d trampolines, %d ARM9 names "
      "(%d not in the link), %d declared slots (%d with an event), "
      "%d declared rows not in the link" % (
          len(link.ids), tot_units, len(link.tramps), len(link.arm9_needs),
          sum(1 for v in link.arm9_needs.values() if not v[1]),
          tot_slots, ev_slots,
          sum(1 for oid in link.ids
              for a in link.declared.get(link.state[oid].ov.tag, {})
              if a not in [s["addr"] for s in link.state[oid].mod_slots])))
    for oid in link.ids:
        st = link.state[oid]
        kinds = {}
        for e in st.data.values():
            kinds[e["bind"]] = kinds.get(e["bind"], 0) + 1
        rk = {}
        for x in st.relocs:
            rk[x["kind"]] = rk.get(x["kind"], 0) + 1
        p("%s  0x%08X..0x%08X  units %d  excluded %d  defined %d  stubs %d  data %s  "
          "relocs %s  code literals %d  implicit decls %d  private copies %d  "
          "own names %d  slots %d" % (
              st.ov.tag, st.ov.lo, st.ov.hi, len(st.units), len(st.excluded), len(st.at),
              len(st.stubs), " ".join("%s=%d" % kv for kv in sorted(kinds.items())) or "0",
              " ".join("%s=%d" % kv for kv in sorted(rk.items())) or "0",
              len(st.code_literals), sum(len(u.protos) for u in st.units),
              sum(len(u.private) for u in st.units),
              sum(len(u.shares) for u in st.units), len(st.mod_slots)))
    for oid in link.ids:
        st = link.state[oid]
        for rel, why in st.excluded:
            p("  %s excluded %s: %s" % (st.ov.tag, rel, why))
        for rel, repl in st.replaced:
            p("  %s asm body %s: compiled from %s" % (st.ov.tag, rel, repl))
        for standin in st.standins:
            p("  %s port stand-in %s: an unmatched function, written from the listing"
              % (st.ov.tag, standin))
        for n, src in sorted(st.unresolved.items()):
            p("  %s unresolved %s (%s): no address in any symbols block, name or SDK table"
              % (st.ov.tag, n, src))
        for n in sorted(st.objects):
            p("  %s host object %s at %s: a matched file uses the name as a member, tag, "
              "local or in an initialiser" % (st.ov.tag, n, hx(st.objects[n]["addr"])))
        for u in st.units:
            for n in sorted(u.private):
                p("  %s private copy %s in %s (%s owns it)" % (
                    st.ov.tag, n, u.rel, st.defs[n].rel))
            for n, (private, target, sig) in sorted(u.shares.items()):
                p("  %s own name %s in %s: the unit declares it as %s, the name "
                  "it shares is %s: %s keeps the unit's own and a forwarder "
                  "calls the target" % (
                      st.ov.tag, n, u.rel, proto_text(sig), target, private))
        for v, s in sorted(st.code_literals.items()):
            p("  %s code literal 0x%08X -> %s" % (st.ov.tag, v, s))
    for n in sorted(link.tramps):
        p("  trampoline %s 0x%08X (%s)" % (n, link.tramps[n], ", ".join(
            "ov%03d" % c for c in link.al.windows(link.tramps[n]))))
    for oid in link.ids:
        st = link.state[oid]
        for s in st.mod_slots:
            p("  %s slot %s 0x%08X (%s -> %s%s)" % (
                st.ov.tag, s["name"], s["addr"], s["declared"], s["original"],
                (", events " + ",".join(s["events"])) if s.get("events") else ""))
    for oid in link.ids:
        st = link.state[oid]
        for name, addr, rel, moved in st.moves:
            p("  %s moved %s 0x%08X out of %s into %s: the unit's own calls "
              "reach the slot" % (st.ov.tag, name, addr, rel, moved))
    for name, ttype in link.game_types():
        p("  game type %s: %s" % (name, ttype))
    for line in link.notes:
        p("  note: " + line)
    for line in link.event_notes:
        p("  EVENT NOTE: " + line)
    write_if_changed(path, "\n".join(r) + "\n")
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--overlays", default="TITLE",
                    help="overlay ids separated by ';' or ',', ALL, or TITLE")
    ap.add_argument("--link-sources", action="append", default=[],
                    help="file listing the other C sources of the link (repeatable)")
    ap.add_argument("--exclusions", default=DEFAULT_EXCLUSIONS)
    ap.add_argument("--ov-exclusions", default=DEFAULT_OV_EXCLUSIONS)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--set-name", default="build")
    ap.add_argument("--arm9-stubs", action="store_true")
    ap.add_argument("--asm-dir", default=os.path.join(EIDOS_ROOT, "sdk", "asm", "ov"),
                    help="portable C for asm-bodied overlay files, by file name")
    ap.add_argument("--port-ov-dir", default=os.path.join(EIDOS_ROOT, "stubs", "ov"),
                    help="port stand-ins for unmatched overlay functions, "
                         "ovNNN_<name>.c")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--no-platform-ui", action="store_true",
                    help="keep original game menus instead of platform settings patches")
    ap.add_argument("--cut-list",
                    help="src/game: the boot's cut list (tools/bootlink.py "
                         "plan_game, `path<TAB>name,...`); the link sources are "
                         "read as their cut copies compile")
    ap.add_argument("--arm9-image",
                    help="the player's extract/arm9_dec.bin, for the linker "
                         "veneers the listing would label (optional)")
    ap.add_argument("--define", action="append", default=[],
                    help="NAME=VALUE a port source is compiled with (repeatable)")
    ap.add_argument("--mod-moves", action="store_true",
                    help="move a declared function out of the unit that calls "
                         "it, so the call reaches the slot (docs/mod-interface.md, "
                         "section 2).  Off until the moved unit carries the "
                         "unit's own enums and data references as well.")
    args = ap.parse_args(argv)
    for d in args.define:
        k, _, v = d.partition("=")
        PORT_DEFINES[k] = v
    if not re.match(r"^[A-Za-z_]\w*$", args.set_name):
        raise SystemExit("ovlink.py: --set-name must be a C identifier")

    link = Link(args)
    out = os.path.abspath(args.out_dir)
    inc = os.path.join(out, "include")
    units = []
    for oid in link.ids:
        st = link.state[oid]
        oh = os.path.join(inc, "%s.h" % st.ov.tag)
        write_overlay_header(link, st, oh)
        for u in st.units:
            u.header = os.path.join(inc, "tu", flat_name(u.rel) + ".h")
            write_unit_header(link, u, oh, u.header)
            cut = ""
            routed = sorted(n for n in u.self_calls if n in st.slot_names)
            if u.cuts or routed:
                cut = os.path.join(inc, "tu", flat_name(u.rel) + ".cut")
                write_if_changed(cut, "%s\t%s\t%s\n" % (
                    u.path.replace("\\", "/"), ",".join(sorted(u.cuts)),
                    ",".join("%s=%s" % (n, link.own(st, n)) for n in routed)))
                cut = "|" + cut.replace("\\", "/")
            units.append("%d|%s|%s%s" % (oid, u.path.replace("\\", "/"),
                                          u.header.replace("\\", "/"), cut))
        write_overlay_c(link, st, os.path.join(out, "%s_link.c" % st.ov.tag))
    write_if_changed(os.path.join(out, "ovl_c.txt"), "".join(
        os.path.join(out, "ov%03d_link.c" % oid).replace("\\", "/") + "\n"
        for oid in link.ids))
    write_dispatch(link, os.path.join(out, "ovl_dispatch.c"), args.set_name)
    write_arm9_stubs(link, os.path.join(out, "ovl_arm9_stubs.c"), args.arm9_stubs)
    write_if_changed(os.path.join(out, "ovl_units.txt"), "".join(x + "\n" for x in units))
    # What a re-run depends on beyond the tool: the overlays' shard
    # directories (a new matched file adds a shard there) and listings.
    deps = []
    for oid in link.ids:
        for rel in ("layout/matched/ov%03d" % oid, "asm/overlays/ov%03d.s" % oid):
            path = os.path.join(link.root, rel)
            if os.path.exists(path):
                deps.append(path.replace("\\", "/"))
    write_if_changed(os.path.join(out, "ovl_depends.txt"), "".join(x + "\n" for x in deps))
    needs = ["%s %s %s" % (n, hx(a) if a is not None else "-",
                           "defined" if d else "missing")
             for n, (a, d) in sorted(link.arm9_needs.items())]
    write_if_changed(os.path.join(out, "ovl_arm9_needs.txt"), "".join(x + "\n" for x in needs))
    # The matched ARM9 sources that define what the overlays call and the
    # link lacks: seeds for the boot's closure (cmake/boot.cmake), as
    # paths under src/matched/.
    by_name, by_addr = {}, {}
    for src, rows in link.al.arm9_src.items():
        for n, a in rows:
            by_name.setdefault(n, src)
            by_addr.setdefault(a, src)
    seeds = set()
    for n, (a, d) in link.arm9_needs.items():
        if d:
            continue
        src = by_name.get(n) or (by_addr.get(a & ~1) if a is not None else None)
        if src is None:
            # A veneer: the boot needs the source of its target.
            vt = link.veneer_target(a)
            src = by_addr.get(vt & ~1) if vt is not None else None
        if src and src.startswith("src/matched/"):
            seeds.add(src[len("src/matched/"):])
    write_if_changed(os.path.join(out, "ovl_arm9_seeds.txt"),
                     "".join(x + "\n" for x in sorted(seeds)))
    rep = write_report(link, os.path.join(out, "ovl_report.txt"))
    for line in link.event_notes:
        print("ovlink.py: %s" % line, file=sys.stderr)
    if args.summary:
        print("\n".join(rep[:1 + len(link.ids)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
