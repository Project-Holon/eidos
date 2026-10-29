#!/usr/bin/env python3
"""
dsaddr.py -- rewrite a matched source's fixed DS addresses for the host.

A matched file reaches DS hardware and fixed RAM through casts of integer
literals to pointers (`*(vu16 *)0x04000208 = 1`).  On a host those are wild
pointers.  This tool copies a matched source into the build tree and turns
every cast of an integer literal whose value lies in a DS region
(0x02000000-0x07FFFFFF) into a call of sdk/dsmem.h's translator:

    (vu16 *)0x04000208            ->  ((vu16 *)bw_ds_ptr(0x04000208u))
    (volatile u32 *)(0x04000000)  ->  ((volatile u32 *)bw_ds_ptr(0x04000000u))
    (u8 *)(0x06000000 + off)      ->  ((u8 *)bw_ds_ptr(0x06000000 + off))

docs/boot.md, "Fixed addresses", is the design.  The rewrite is textual and
local: comments, strings, character constants and every literal that is not
the operand of a pointer cast are left byte for byte as they were, and the
line structure is kept, so a `#line` directive makes diagnostics and
__FILE__ name the original file and line.

A file with something to rewrite gets a three-line prelude: a comment, a
declaration of bw_ds_ptr and `#line 1 "<original path>"`.  The declaration
is spelled with `unsigned int` rather than by including sdk/dsmem.h,
because a matched translation unit carries its own u8/u16/u32 typedefs and
must not see a second set (sdk/types.h's header comment).  `unsigned int`
is the type uint32_t names on every target eidos builds for;
tests/test_dsaddr.py compiles the prelude next to sdk/dsmem.h to prove the
two declarations agree on the compiler in use.

The address rewrite leaves files without address uses unchanged. Generated
host copies also retain the DS widths of recovered u32/s32 typedefs and
shared RTC records, and adapt selected declarations at native pointer boundaries.

The same rewrite applies where a name provably stands for a DS address
(see "Names that stand for DS addresses" below): a file-local constant
used as the cast operand (`(vu32 *)REG_DMA0SAD_ADDR`), the parameter of a
file-local register macro whose every invocation passes a DS address
(`#define REG16(a) (*(vu16 *)(a))`), and the address parameter of a
function in the CALLEES table, which receives a DS address as an integer
and casts it itself.

What is deliberately not rewritten, and reported by --survey and --report
instead:

  - DS-range literals outside a pointer cast (arithmetic, comparisons,
    integer casts, arguments): the value is a number there, and whether
    it is used as an address is only known at run time;
  - casts to a function pointer type: bw_ds_ptr returns host data memory,
    and calling it would be no more correct than calling the DS address;
  - pointer casts in the initializer of a file-scope or `static` object,
    which C requires to be a constant expression; a call is not one;
  - a pointer cast of a DS literal that is by itself the address argument
    of a CALLEES function: the callee translates it, so it must receive
    the DS address.

--report classifies every literal left alone (resolved elsewhere,
harmless, in an asm body, or needing a port answer, each with its
reason) into docs/dsaddr-residue.md.

Usage:
    dsaddr.py IN.c -o OUT.c [--name PATH]   rewrite one file
    dsaddr.py --survey DIR [--json FILE]     report casts and residue
    dsaddr.py --report DIR [-o FILE]         the classified residue report
    dsaddr.py --check IN.c ...               exit 1 if any file would change

Standard library only.
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import sys
from collections import Counter, defaultdict

DS_LO = 0x02000000
DS_HI = 0x07FFFFFF

REGIONS = (
    (0x02000000, 0x02FFFFFF, "main"),
    (0x03000000, 0x03FFFFFF, "wram"),
    (0x04000000, 0x04FFFFFF, "io"),
    (0x05000000, 0x05FFFFFF, "palette"),
    (0x06000000, 0x06FFFFFF, "vram"),
    (0x07000000, 0x07FFFFFF, "oam"),
)

PRELUDE_DECL = "void *bw_ds_ptr(unsigned int);"

QUALIFIERS = {"const", "volatile", "restrict", "__restrict", "__volatile__"}
# Identifiers that cannot end a type name; a parenthesised group made of
# them is an expression, not a cast.
NON_TYPE_WORDS = {"sizeof", "return", "case", "if", "while", "for", "switch",
                  "do", "else", "goto", "_Alignof", "defined"}

_INT_RE = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F]+|[0-9]+)([uUlL]*)(?![\w.])")
_IDENT_RE = re.compile(r"[A-Za-z_]\w*")
_PTR_TYPEDEF_RE = re.compile(
    r"\btypedef\b[^;{}()]*\*\s*(?:(?:const|volatile|restrict)\s+)*([A-Za-z_]\w*)\s*;")


def region_of(value: int) -> str | None:
    for lo, hi, name in REGIONS:
        if lo <= value <= hi:
            return name
    return None


def int_value(digits: str) -> int:
    if digits[:2] in ("0x", "0X"):
        return int(digits, 16)
    if len(digits) > 1 and digits[0] == "0":
        try:
            return int(digits, 8)
        except ValueError:
            return -1
    return int(digits, 10)


# ---------------------------------------------------------------------------
# Masking: comments and literal contents become blanks of the same length
# ---------------------------------------------------------------------------

def mask(src: str) -> str:
    """Return `src` with every comment replaced by spaces and the contents of
    every string and character literal replaced by 'x', newlines kept, so
    offsets into the mask are offsets into the source and nothing inside a
    comment or a literal can look like code."""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = i
            while j < n and src[j] != "\n":
                if src[j] == "\\" and src.startswith("\n", j + 1):
                    j += 2
                    continue
                if src[j] == "\\" and src.startswith("\r\n", j + 1):
                    j += 3
                    continue
                j += 1
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                if out[k] != "\n":
                    out[k] = "x"
            i = j + 1
        else:
            i += 1
    return "".join(out)


def _spliced(m: str, nl: int) -> bool:
    """Whether the newline at `nl` ends a line with a backslash splice
    (LF or CRLF line endings)."""
    j = nl - 1
    if j >= 0 and m[j] == "\r":
        j -= 1
    return j >= 0 and m[j] == "\\"


def _skip_ws_back(m: str, i: int) -> int:
    """Index of the last non-blank character at or before i (blank includes a
    backslash-newline splice), or -1."""
    while i >= 0:
        c = m[i]
        if c in " \t\r\n\f\v":
            i -= 1
        elif c == "\\" and i + 1 < len(m) and m[i + 1] in "\r\n":
            i -= 1
        else:
            return i
    return -1


def _skip_ws_fwd(m: str, i: int) -> int:
    n = len(m)
    while i < n:
        c = m[i]
        if c in " \t\r\n\f\v":
            i += 1
        elif c == "\\" and i + 1 < n and m[i + 1] in "\r\n":
            i += 1
        else:
            return i
    return n


def _match_back(m: str, close: int) -> int:
    """Index of the '(' matching the ')' at `close`, or -1."""
    depth = 0
    i = close
    while i >= 0:
        c = m[i]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
            if depth == 0:
                return i
        elif c in ";{}":
            return -1
        i -= 1
    return -1


def _match_fwd(m: str, open_: int) -> int:
    depth = 0
    i = open_
    n = len(m)
    while i < n:
        c = m[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        elif c in ";{}":
            return -1
        i += 1
    return -1


def classify_type(text: str, ptr_typedefs: set[str]) -> str:
    """What the contents of a parenthesised group before a literal are:
    'ptr' (an object pointer type name), 'funcptr', 'int' (a type name with
    no pointer), or 'expr' (not a type name at all)."""
    t = re.sub(r"\\\r?\n", " ", text).strip()
    if not t:
        return "expr"
    if "(" in t:
        # An abstract declarator in parentheses: `R (*)(A)` is a function
        # pointer, `R (**)(A)` a pointer to one (a data slot) and
        # `T (*)[N]` a pointer to an array; the last two are object
        # pointers and are rewritten like any other.
        d = re.search(r"\(\s*(\*[\s\w*]*)\)\s*(.)?", t)
        if not d:
            return "expr"
        if d.group(1).count("*") >= 2 or d.group(2) == "[":
            return "ptr"
        return "funcptr"
    if re.search(r"[^\w\s*]", t):
        return "expr"
    words = _IDENT_RE.findall(t)
    if not words or words[0] in NON_TYPE_WORDS or words[-1] in NON_TYPE_WORDS:
        return "expr"
    if "*" in t:
        # identifiers, then the pointer part: stars and qualifiers only
        head, _, tail = t.partition("*")
        if not _IDENT_RE.search(head):
            return "expr"
        if any(w not in QUALIFIERS for w in _IDENT_RE.findall(tail)):
            return "expr"
        return "ptr"
    base = [w for w in words if w not in QUALIFIERS]
    if base and base[-1] in ptr_typedefs:
        return "ptr"
    # Two identifiers in a row is only a type name (`unsigned int`); a
    # lone identifier is a type name or a macro parameter / variable.
    return "int"


_DEFINE_RE = re.compile(r"[ \t]*#[ \t]*define\b")
# Keywords after which a parenthesised group starts an expression (so may be
# a cast), where any other identifier would make it a call's argument list.
EXPR_KEYWORDS = {"return", "case", "else", "do", "sizeof", "goto"}


def _is_call_paren(m: str, open_: int, ptr_typedefs: set[str]) -> bool:
    """Whether the '(' at `open_` opens a call's argument list (or an
    `if`/`while` condition), so the group cannot be a cast."""
    before = _skip_ws_back(m, open_ - 1)
    if before < 0:
        return False
    c = m[before]
    if c.isalnum() or c == "_":
        s = before
        while s > 0 and (m[s - 1].isalnum() or m[s - 1] == "_"):
            s -= 1
        return m[s:before + 1] not in EXPR_KEYWORDS
    if c == "]":
        return True
    if c == ")":
        # `(u32)(u8 *)0x...` is a cast of a cast; `f(a)(...)` and
        # `if (x)(...)` are not
        o = _match_back(m, before)
        if o < 0 or _is_call_paren(m, o, ptr_typedefs):
            return True
        return classify_type(m[o + 1:before], ptr_typedefs) == "expr"
    return False


# ---------------------------------------------------------------------------
# Scope: is an offset at file scope (where an initializer must be constant)
# ---------------------------------------------------------------------------

def _scope_map(m: str):
    """For each '{' decide whether it opens a function body (or a block in
    one) or a brace initializer / struct body at file scope, and return a
    function telling whether an offset lies outside every function body.
    Preprocessor lines are skipped: a macro body is not at any scope."""
    events = []          # (offset, +1/-1 for function depth)
    stack = []           # True when the brace counts as function depth
    func_depth = 0
    line_start = True
    in_pp = False
    i, n = 0, len(m)
    while i < n:
        c = m[i]
        if c == "\n":
            if not _spliced(m, i):
                in_pp = False
            line_start = True
            i += 1
            continue
        if line_start and c == "#":
            in_pp = True
        if c not in " \t\r\f\v":
            line_start = False
        if in_pp:
            i += 1
            continue
        if c == "{":
            if func_depth > 0:
                is_func = True
            else:
                p = _skip_ws_back(m, i - 1)
                # `) {` is a function body; `= {`, `, {`, `{ {`, a struct
                # or enum tag is an initializer or a type body.
                is_func = p >= 0 and m[p] == ")"
            stack.append(is_func)
            if is_func:
                func_depth += 1
                events.append((i, func_depth))
        elif c == "}":
            if stack and stack.pop():
                func_depth -= 1
                events.append((i, func_depth))
        i += 1

    def at_file_scope(off: int) -> bool:
        depth = 0
        for pos, d in events:
            if pos > off:
                break
            depth = d
        return depth == 0
    return at_file_scope


def _in_static_init(m: str, off: int) -> bool:
    """Whether `off`, inside a function, lies in the initializer of a
    `static` local, which C also requires to be a constant expression."""
    braces = 0
    i = off - 1
    while i >= 0:
        c = m[i]
        if c == "}":
            braces += 1
        elif c == "{":
            if braces:
                braces -= 1
            else:
                p = _skip_ws_back(m, i - 1)
                if p < 0 or m[p] not in "=,{":
                    break           # a block: the statement starts here
        elif c == ";" and not braces:
            break
        i -= 1
    seg = m[i + 1:off]
    return bool(re.search(r"\bstatic\b", seg)) and "=" in seg


def _pp_line_bounds(m: str, off: int):
    """(start, is_preprocessor) of the logical line holding `off`."""
    s = off
    while True:
        s = m.rfind("\n", 0, s)
        if s <= 0 or not _spliced(m, s):
            break
    s = 0 if s < 0 else s + 1
    k = _skip_ws_fwd(m, s)
    return s, k < len(m) and m[k] == "#"


# ---------------------------------------------------------------------------
# The rewrite
# ---------------------------------------------------------------------------

class Site:
    __slots__ = ("kind", "line", "value", "region", "start", "end", "text",
                 "type", "form", "via", "call", "prev_op", "next_op", "asm",
                 "uses", "note")

    def __init__(self, kind, line, value, start, end, text, type_=None,
                 form=None, via=None):
        self.kind = kind          # 'rewrite' or a residue kind
        self.line = line
        self.value = value        # None: an address only known at run time
        self.region = region_of(value) if value is not None else "param"
        self.start = start
        self.end = end
        self.text = text
        self.type = type_
        self.form = form
        self.via = via            # how a non-literal operand was proven DS
        self.call = None          # (callee, argument index, leading) or None
        self.prev_op = ""
        self.next_op = ""
        self.asm = False
        self.uses = None          # 'define': where the constant is used
        self.note = None


# ---------------------------------------------------------------------------
# Names that stand for DS addresses
# ---------------------------------------------------------------------------
#
# Three places hold a DS address under a name instead of as a literal, and
# each gets the same rewrite as a literal operand, ((T)bw_ds_ptr(operand)),
# only where the tool can show the name always holds a DS address:
#
#   - an object-like `#define NAME 0x04000180` (or one whose expansion
#     starts with such a name or literal) used as a pointer-cast operand:
#     rewritten at the use site, `(vu32 *)NAME` -> `((vu32 *)bw_ds_ptr(NAME))`;
#   - a function-like macro defined in the file that casts its parameter,
#     `#define REG16(a) (*(vu16 *)(a))`: the cast in the macro body is
#     rewritten when every invocation in the file passes a DS address
#     (a literal or a DS constant leads the argument) and none is in a
#     constant initializer; otherwise the macro is left alone and its
#     DS arguments are reported as 'macro-arg-mixed';
#   - a function in CALLEES below, which receives a DS address as an
#     integer and casts it itself: casts of the listed parameters inside
#     its body are rewritten.

# Each entry: the function's name in the matched corpus, its parameters
# that carry a DS address (name: argument index), the names other matched
# files call it by, and the evidence.  The function receives the DS
# address as a u32 and casts it itself, so its casts of that parameter go
# through bw_ds_ptr, and a caller must hand it the DS address: a literal
# argument already is one, and a pointer cast of a DS literal standing as
# the whole argument (`(void *)0x04000050`, from callers whose prototype
# says pointer) is left alone at that position for the same reason.
# `dsaddr.py --report` lists every call of every entry with the argument
# it passes and marks the ones that are neither ("check"): those callers
# pass a host pointer and need a port answer before they enter the build.
CALLEES = {
    # G2x_SetBlendBrightness_ (libgx g2.o): `addr` is REG_BLDCNT or
    # REG_DB_BLDCNT; it stores BLDCNT at addr and BLDY at addr + 4.  Callers
    # pass 0x04000050 or 0x04001050, as a literal or a pointer cast of
    # one; eight pass a pointer reached otherwise (listed as "check").
    "func_0207D658": {"params": {"addr": 0},
                      "aliases": ("G2x_SetBlendBrightness_",)},
    # G2x_ChangeBlendBrightness_ (libgx g2.o): same register pair as
    # above; every caller passes a literal.
    "func_0207D6C0": {"params": {"addr": 0},
                      "aliases": ("G2x_ChangeBlendBrightness_",)},
    # G2x_SetBlendBrightnessExt_ (libgx g2.o): BLDCNT, BLDALPHA (addr + 2)
    # and BLDY (addr + 4) of either engine; every caller passes a literal.
    "func_0207D67C": {"params": {"addr": 0},
                      "aliases": ("G2x_SetBlendBrightnessExt_",)},
    # G2x_SetBGyAffine_ (libgx g2.o): `addr` is the BGxPA register of an
    # affine background (0x04000020/30, 0x04001020/30); four words stored.
    # Callers pass a literal or a pointer cast of one.
    "func_0207D59C": {"params": {"addr": 0},
                      "aliases": ("G2x_SetBGyAffine_",)},
}

CALLEE_NAMES = {}
for _n, _e in CALLEES.items():
    CALLEE_NAMES[_n] = _n
    for _a in _e["aliases"]:
        CALLEE_NAMES[_a] = _n
del _n, _e

INT_TYPE_WORDS = {"u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
                  "int", "unsigned", "signed", "long", "short", "char",
                  "uintptr_t", "intptr_t", "size_t", "uint32_t", "int32_t"}

_DEFINE_LINE_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\(([^)]*)\))?([^\n]*)", re.M)


class Define:
    __slots__ = ("name", "params", "name_at", "body_start", "body_end", "body")

    def __init__(self, name, params, name_at, body_start, body_end, body):
        self.name = name
        self.body = body
        self.params = params      # None: object-like
        self.name_at = name_at
        self.body_start = body_start
        self.body_end = body_end


def _defines(m: str):
    """The file's #defines by name; a name defined more than once maps to
    None (which definition a use sees depends on where it is)."""
    flat = re.sub(r"\\\r?\n", lambda x: " " * len(x.group()), m)
    out = {}
    for d in _DEFINE_LINE_RE.finditer(flat):
        params = None
        if d.group(2) is not None:
            params = [p.strip() for p in d.group(3).split(",")]
            if params == [""]:
                params = []
        name = d.group(1)
        body_end = d.end(4)
        while body_end > d.start(4) and flat[body_end - 1] in " \t\r":
            body_end -= 1
        out[name] = None if name in out else Define(
            name, params, d.start(1), d.start(4), body_end,
            flat[d.start(4):body_end])
    return out


def _strip_outer(t: str) -> str:
    while t.startswith("("):
        close = _match_fwd(t, 0)
        if close == len(t) - 1:
            t = t[1:-1].strip()
            continue
        if close > 0:
            words = _IDENT_RE.findall(t[1:close])
            if (words and all(w in INT_TYPE_WORDS or w in QUALIFIERS for w in words)
                    and not re.search(r"[^\w\s]", t[1:close])):
                t = t[close + 1:].strip()      # a leading integer cast
                continue
        break
    return t


def ds_lead(text: str, defs, depth: int = 0):
    """The DS address an expression starts from, when its leading operand
    (through parentheses, integer casts and file-local constants) is a
    DS-range literal; None otherwise."""
    t = _strip_outer(text.strip())
    lit = re.match(r"(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*(?![\w.])", t)
    if lit:
        v = int_value(lit.group(1))
        return v if DS_LO <= v <= DS_HI else None
    ident = _IDENT_RE.match(t)
    if ident and depth < 8:
        d = defs.get(ident.group(0))
        rest = t[ident.end():].lstrip()
        if d is not None and d.params is None and not rest.startswith("("):
            return ds_lead(d.body, defs, depth + 1)
    return None


def _postfix_after(m: str, i: int) -> bool:
    q = _skip_ws_fwd(m, i)
    return q < len(m) and (m[q] in "([." or m.startswith("->", q))


def _cast_at(m: str, ls: int, lend: int, ptr_typedefs, ident: bool):
    """Whether the operand starting at the token m[ls:lend] is the operand
    of a cast.  Returns (cast_kind, open_, close, a, b, form): the cast's
    parentheses at open_/close, the operand at a..b, its form ('literal',
    'paren' or 'expr'); cast_kind is None when no cast applies to it."""
    if ident and _postfix_after(m, lend):
        return None, -1, -1, ls, lend, "literal"
    a, b = ls, lend
    while True:
        p = _skip_ws_back(m, a - 1)
        q = _skip_ws_fwd(m, b)
        if p >= 0 and m[p] == "(" and q < len(m) and m[q] == ")":
            pp = _skip_ws_back(m, p - 1)
            # `f(0x..)`/`sizeof(0x..)`: a call's parentheses, not a wrapper
            if pp >= 0 and (m[pp].isalnum() or m[pp] == "_"):
                break
            a, b = p, q + 1
            continue
        break
    form = "literal" if (a, b) == (ls, lend) else "paren"
    p = _skip_ws_back(m, a - 1)
    # `(T *)(0x06000000 + off)`: the token opens a parenthesised operand
    # that is more than the token.
    if p >= 0 and m[p] == "(":
        pp = _skip_ws_back(m, p - 1)
        if pp >= 0 and m[pp] == ")":
            close = _match_fwd(m, p)
            if close > 0:
                a, b = p, close + 1
                form = "expr"
                p = pp
    if ident and form == "paren" and _postfix_after(m, b):
        return None, -1, -1, a, b, form
    if p >= 0 and m[p] == ")":
        open_ = _match_back(m, p)
        if open_ >= 0:
            if _is_call_paren(m, open_, ptr_typedefs):
                return "expr", open_, p, a, b, form
            return classify_type(m[open_ + 1:p], ptr_typedefs), open_, p, a, b, form
    return None, -1, -1, a, b, form


def _split_args(m: str, open_: int, close: int):
    """(start, end) of each top-level argument between the parentheses."""
    out, depth, s = [], 0, open_ + 1
    for i in range(open_ + 1, close):
        c = m[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append((s, i))
            s = i + 1
    out.append((s, close))
    return out


def _match_brace(m: str, open_: int) -> int:
    depth = 0
    for i in range(open_, len(m)):
        if m[i] == "{":
            depth += 1
        elif m[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(m) - 1


def _enclosing_call(m: str, off: int):
    """(callee name, argument index, argument start) of the innermost call
    whose argument list holds `off`, or None."""
    i = off - 1
    while True:
        depth = 0
        while i >= 0:
            c = m[i]
            if c in ")]":
                depth += 1
            elif c in "([":
                if depth == 0:
                    break
                depth -= 1
            elif c in ";{}":
                return None
            i -= 1
        if i < 0 or m[i] == "[":
            return None
        open_ = i
        p = _skip_ws_back(m, open_ - 1)
        if p >= 0 and (m[p].isalnum() or m[p] == "_"):
            s = p
            while s > 0 and (m[s - 1].isalnum() or m[s - 1] == "_"):
                s -= 1
            name = m[s:p + 1]
            if name in NON_TYPE_WORDS or name in EXPR_KEYWORDS:
                return None
            close = _match_fwd(m, open_)
            if close < 0:
                return None
            for k, (a, b) in enumerate(_split_args(m, open_, close)):
                if a <= off < b:
                    return name, k, a, b
            return None
        i = open_ - 1


def _callee_address_arg(m: str, start: int, end: int) -> bool:
    """Whether m[start:end] is, by itself, the address argument of a call
    of a CALLEES function."""
    call = _enclosing_call(m, start)
    if not call or call[0] not in CALLEE_NAMES:
        return False
    name, argno, a, b = call
    if argno not in CALLEES[CALLEE_NAMES[name]]["params"].values():
        return False
    return _skip_ws_fwd(m, a) == start and _skip_ws_back(m, b - 1) == end - 1


_OPS2 = ("<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "&=", "|=", "^=",
         "+=", "-=", "->", "*=", "/=")


def _op_after(m: str, i: int) -> str:
    q = _skip_ws_fwd(m, i)
    if q >= len(m):
        return ""
    if m[q:q + 3] in ("<<=", ">>="):
        return m[q:q + 3]
    if m[q:q + 2] in _OPS2:
        return m[q:q + 2]
    return m[q]


def _op_before(m: str, i: int) -> str:
    p = _skip_ws_back(m, i - 1)
    if p < 0:
        return ""
    if p >= 1 and m[p - 1:p + 1] in _OPS2:
        return m[p - 1:p + 1]
    return m[p]


_ASM_LINE_RE = re.compile(
    r"^[ \t]*(?:[A-Za-z_.]\w*:)?[ \t]*"
    r"(?:ldr|str|mov|mvn|dcd|dcw|dcb|orr|and|eor|bic|add|sub|rsb|cmp|cmn|"
    r"tst|teq|mul|mla|mcr|mrc|ldm|stm|adr)[a-z]{0,4}[ \t]+[^;\n]*$",
    re.I | re.M)


def _asm_lines(m: str):
    """Line starts of the lines that are instructions of an asm body, in a
    file that has one."""
    if not re.search(r"\b(?:asm|__asm|__asm__)\b", m):
        return set()
    return {a.start() for a in _ASM_LINE_RE.finditer(m)}


def analyse(src: str):
    """Find every DS-range integer literal in code, and every name that
    stands for one, and decide what to do with each.  Returns (rewrites,
    residue): rewrites are Sites with start/end/text to splice in,
    non-overlapping and sorted; residue are Sites left alone."""
    m = mask(src)
    ptr_typedefs = set(_PTR_TYPEDEF_RE.findall(m))
    defs = _defines(m)
    scope = []

    starts = [0] + [nl.end() for nl in re.finditer("\n", src)]

    def line_of(off):
        return bisect.bisect_right(starts, off)

    def const_context(off):
        if not scope:
            scope.append(_scope_map(m))
        return scope[0](off) or _in_static_init(m, off)

    cands, statics = [], []

    def cast_site(ls, cast, value, inner, via):
        """Record the rewrite (or the constant-initializer residue) of the
        pointer cast `cast` found for the operand at ls."""
        _, open_, p, a, b, form = cast
        pp_start, in_pp = _pp_line_bounds(m, ls)
        if not in_pp and const_context(open_):
            statics.append(Site("static-init", line_of(ls), value, open_, b,
                                None, src[open_ + 1:p].strip(), form, via))
            return
        type_text = src[open_ + 1:p].strip()
        if form == "expr":
            inner = src[a + 1:b - 1].strip()
        if in_pp:
            form += "-in-" + ("define" if _DEFINE_RE.match(m, pp_start)
                              else "directive")
        cands.append(Site("rewrite", line_of(ls), value, open_, b,
                          "((%s)bw_ds_ptr(%s))" % (type_text, inner),
                          type_text, form, via))

    # 1. Literals.
    pending = []
    for lit in _INT_RE.finditer(m):
        value = int_value(lit.group(1))
        if not (DS_LO <= value <= DS_HI):
            continue
        ls, lend = lit.start(), lit.end()
        cast = _cast_at(m, ls, lend, ptr_typedefs, False)
        if cast[0] == "ptr" and _callee_address_arg(m, cast[1], cast[4]):
            # `func_0207D658((void *)0x04000050, ...)`: the callee
            # translates its address parameter itself, so the caller must
            # hand it the DS address, not a host pointer.
            pending.append((ls, lend, value, (None,) + cast[1:], "callee-arg"))
        elif cast[0] == "ptr":
            cast_site(ls, cast, value, "0x%08Xu" % value, None)
        else:
            pending.append((ls, lend, value, cast, None))

    # 2. File-local constants.
    ds_defs = {}
    for name, d in defs.items():
        if d is not None and d.params is None:
            v = ds_lead(m[d.body_start:d.body_end], defs)
            if v is not None:
                ds_defs[name] = v
    for name, v in ds_defs.items():
        for u in re.finditer(r"\b%s\b" % re.escape(name), m):
            if u.start() == defs[name].name_at:
                continue
            cast = _cast_at(m, u.start(), u.end(), ptr_typedefs, True)
            if cast[0] == "ptr":
                cast_site(u.start(), cast, v, name, "define " + name)

    # 3. Function-like macros that cast a parameter.
    covered, mixed = [], []        # (start, end, macro or callee name)
    for name, d in defs.items():
        if d is None or not d.params:
            continue
        body_casts = []
        for k, prm in enumerate(d.params):
            if not _IDENT_RE.fullmatch(prm):
                continue
            for u in re.finditer(r"\b%s\b" % re.escape(prm),
                                 m[d.body_start:d.body_end]):
                s = d.body_start + u.start()
                if m[s - 1:s] == "#" or m[s + len(prm):s + len(prm) + 2] == "##":
                    continue
                cast = _cast_at(m, s, s + len(prm), ptr_typedefs, True)
                if cast[0] == "ptr" and cast[1] >= d.body_start:
                    body_casts.append((k, prm, s, cast))
        if not body_casts:
            continue
        idxs = sorted({k for k, _, _, _ in body_casts})
        ok, ds_args, any_inv = True, [], False
        for u in re.finditer(r"\b%s\b" % re.escape(name), m):
            if u.start() == d.name_at:
                continue
            q = _skip_ws_fwd(m, u.end())
            if q >= len(m) or m[q] != "(":
                pp_start, in_pp = _pp_line_bounds(m, u.start())
                if in_pp and not _DEFINE_RE.match(m, pp_start):
                    continue       # #ifdef / #undef
                ok = False
                continue
            close = _match_fwd(m, q)
            if close < 0:
                ok = False
                continue
            any_inv = True
            args = _split_args(m, q, close)
            if not _pp_line_bounds(m, u.start())[1] and const_context(u.start()):
                ok = False
            for k in idxs:
                if k >= len(args):
                    ok = False
                    continue
                a, b = args[k]
                if ds_lead(m[a:b], defs) is None:
                    ok = False
                else:
                    ds_args.append((a, b, name))
        if not any_inv:
            continue
        if ok:
            for k, prm, s, cast in body_casts:
                cast_site(s, cast, None, prm, "macro %s(%s)" % (name, prm))
            covered.extend(ds_args)
        else:
            mixed.extend(ds_args)

    # 4. Functions in CALLEES: casts of their address parameters.
    for name, ent in CALLEES.items():
        for u in re.finditer(r"\b%s\s*\(" % re.escape(name), m):
            open_ = u.end() - 1
            close = _match_fwd(m, open_)
            if close < 0:
                continue
            q = _skip_ws_fwd(m, close + 1)
            if q >= len(m) or m[q] != "{" or not const_context(u.start()):
                continue
            end = _match_brace(m, q)
            for prm in ent["params"]:
                for v in re.finditer(r"\b%s\b" % re.escape(prm), m[q:end]):
                    s = q + v.start()
                    cast = _cast_at(m, s, s + len(prm), ptr_typedefs, True)
                    if cast[0] == "ptr":
                        cast_site(s, cast, None, prm, "callee %s(%s)" % (name, prm))

    # Keep the outermost of overlapping candidates.
    cands.sort(key=lambda s: (s.start, -s.end))
    rewrites, last_end = [], -1
    for s in cands:
        if s.start >= last_end:
            rewrites.append(s)
            last_end = s.end
    taken = sorted([(s.start, s.end) for s in rewrites] +
                   [(s.start, s.end) for s in statics])

    def inside(off, spans):
        return any(sp[0] <= off < sp[1] for sp in spans)

    def in_taken(off):
        return inside(off, taken)

    asm_lines = _asm_lines(m)

    def on_asm_line(off):
        return (m.rfind("\n", 0, off) + 1) in asm_lines

    residue = list(statics)
    for ls, lend, value, cast, forced in pending:
        if in_taken(ls):
            continue
        if forced:
            site = Site(forced, line_of(ls), value, ls, lend, None,
                        src[cast[1] + 1:cast[2]].strip())
            call = _enclosing_call(m, cast[1])
            site.call = (call[0], call[1], True)
            site.note = CALLEE_NAMES[call[0]]
            residue.append(site)
            continue
        cast_kind, open_, p = cast[0], cast[1], cast[2]
        pp_start, in_pp = _pp_line_bounds(m, ls)
        is_define = in_pp and _DEFINE_RE.match(m, pp_start)
        kind = "bare"
        if cast_kind == "funcptr":
            kind = "funcptr-cast"
        elif cast_kind == "int":
            kind = "int-cast"
        elif in_pp and not is_define:
            kind = "directive"
        elif is_define:
            for def_name in ds_defs:
                d = defs[def_name]
                if d.body_start <= ls < d.body_end:
                    kind = "define"
                    break
        site = Site(kind, line_of(ls), value, ls, lend, None,
                    src[open_ + 1:p].strip() if cast_kind else None)
        a, b = cast[3], cast[4]
        site.prev_op = _op_before(m, a)
        site.next_op = _op_after(m, b)
        site.asm = on_asm_line(ls)
        call = None if in_pp and not is_define else _enclosing_call(m, ls)
        if call:
            callee, argno, ca, cb = call
            site.call = (callee, argno, ds_lead(m[ca:cb], defs) is not None)
        if kind == "define":
            uses = Counter()
            for u in re.finditer(r"\b%s\b" % re.escape(def_name), m):
                if u.start() == defs[def_name].name_at:
                    continue
                us = u.start()
                if in_taken(us):
                    uses["cast"] += 1
                elif inside(us, covered):
                    uses["covered"] += 1
                elif on_asm_line(us):
                    uses["asm"] += 1
                elif any(dd is not None and dd.name in ds_defs
                         and dd.body_start <= us < dd.body_end
                         for dd in defs.values()):
                    uses["nested"] += 1
                else:
                    uses["other"] += 1
            site.uses = dict(uses)
            site.note = def_name
            if uses and not (uses["other"] or uses["asm"]) and (uses["cast"] or uses["covered"]):
                site.kind = "define-resolved"
        elif kind == "bare":
            if inside(ls, covered):
                site.kind = "macro-arg"
            elif inside(ls, mixed):
                site.kind = "macro-arg-mixed"
                site.note = next(n for a, b, n in mixed if a <= ls < b)
            elif site.call and site.call[2]:
                target = CALLEE_NAMES.get(site.call[0])
                if target and site.call[1] in CALLEES[target]["params"].values():
                    site.kind = "callee-arg"
                    site.note = target
        residue.append(site)
    residue.sort(key=lambda s: s.start)
    return rewrites, residue


# aletheia's src/game/sdk_ioreg.h spells a register address
# IOREG_ADDR(0x04000280): the plain number on the DS, a pointer-sized
# integer on a host.  Here it is the number again, so the address is seen
# (and translated) like any other fixed address.
_IOREG_ADDR_RE = re.compile(r"\bIOREG_ADDR\(\s*(0[xX][0-9A-Fa-f]+[uUlL]*)\s*\)")


def rewrite(src: str, name: str):
    """The rewritten text of `src` (identical when there is nothing to
    rewrite) and the analysis behind it."""
    src = _IOREG_ADDR_RE.sub(r"\1", src)
    rewrites, residue = analyse(src)
    if not rewrites:
        return src, rewrites, residue
    parts, pos = [], 0
    for s in rewrites:
        parts.append(src[pos:s.start])
        parts.append(s.text)
        # A cast split over lines keeps its line count, so everything after
        # it keeps its line number (inside a macro body, as a splice).
        lost = src.count("\n", s.start, s.end) - s.text.count("\n")
        if lost > 0:
            parts.append(("\\\n" if "-in-" in s.form else "\n") * lost)
        pos = s.end
    parts.append(src[pos:])
    body = "".join(parts)
    shown = name.replace("\\", "/").replace('"', '\\"')
    prelude = ("/* tools/dsaddr.py: fixed DS addresses go through bw_ds_ptr */\n"
               "%s\n#line 1 \"%s\"\n" % (PRELUDE_DECL, shown))
    return prelude + body, rewrites, residue


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

def _read(path: str) -> str:
    with open(path, "rb") as f:
        return f.read().decode("latin-1")


def _write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    data = text.encode("latin-1")
    try:
        with open(path, "rb") as f:
            if f.read() == data:
                # Ninja's restat records the completed generator. Preserve
                # identical outputs so changing the rewriter does not force
                # every game translation unit to compile again.
                return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def _normalise_context(src: str, site: Site) -> str:
    s = src.rfind("\n", 0, site.start) + 1
    e = src.find("\n", site.end)
    e = len(src) if e < 0 else e
    line = src[s:e].strip()
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r"/\*.*?\*/", "", line).strip()
    line = re.sub(r"\b0[xX][0-9a-fA-F]+[uUlL]*\b", "N", line)
    line = re.sub(r"\b\d+[uUlL]*\b", "n", line)
    line = re.sub(r"\b(?:func|sub|unit|g|data|ov\d+_func|D)_[0-9A-Fa-f_]+\w*", "SYM", line)
    line = re.sub(r"\s+", " ", line)
    return line[:110]


def survey(root: str, json_out: str | None, show: int) -> int:
    files = []
    for d, _, names in os.walk(root):
        for nm in names:
            if nm.endswith(".c"):
                files.append(os.path.join(d, nm))
    files.sort()
    casts = Counter()
    cast_forms = Counter()
    cast_types = Counter()
    resid = Counter()
    resid_groups = defaultdict(list)
    changed = 0
    for path in files:
        src = _read(path)
        rel = os.path.relpath(path, root).replace("\\", "/")
        rewrites, residue = analyse(src)
        if rewrites:
            changed += 1
        for s in rewrites:
            casts[s.region] += 1
            cast_forms[s.form] += 1
            cast_types[re.sub(r"\s+", " ", s.type)] += 1
        for s in residue:
            resid[(s.kind, s.region)] += 1
            if s.kind == "int-cast" or s.kind == "funcptr-cast":
                key = (s.kind, s.region, "(%s)N" % re.sub(r"\s+", " ", s.type or ""))
            else:
                key = (s.kind, s.region, _normalise_context(src, s))
            resid_groups[key].append("%s:%d" % (rel, s.line))
    total = sum(casts.values())
    print("files scanned: %d, files rewritten: %d" % (len(files), changed))
    print("pointer casts rewritten: %d" % total)
    for r, c in sorted(casts.items(), key=lambda x: -x[1]):
        print("  %-8s %6d" % (r, c))
    print("  by form: " + ", ".join("%s %d" % kv for kv in cast_forms.most_common()))
    print("  types (top 25): " + ", ".join("%s %d" % kv for kv in cast_types.most_common(25)))
    print("residue (DS-range literals left alone), by kind and region:")
    for (k, r), c in sorted(resid.items(), key=lambda x: (x[0][0], -x[1])):
        print("  %-16s %-8s %6d" % (k, r, c))
    if show:
        print("residue groups (I/O and non-main regions, and every cast form):")
        for key, sites in sorted(resid_groups.items(), key=lambda kv: (kv[0][0], kv[0][1], -len(kv[1]))):
            kind, region, pat = key
            if region == "main" and kind in ("bare", "define", "directive"):
                continue
            ex = ", ".join(sites[:show]) + (" ..." if len(sites) > show else "")
            print("  [%s %s x%d] %s\n      %s" % (kind, region, len(sites), pat, ex))
    if json_out:
        data = {
            "files": len(files), "files_rewritten": changed,
            "casts": dict(casts), "forms": dict(cast_forms),
            "residue": [{"kind": k, "region": r, "pattern": p, "sites": s}
                        for (k, r, p), s in sorted(resid_groups.items())],
        }
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)
    return 0


# ---------------------------------------------------------------------------
# The residue report (docs/dsaddr-residue.md)
# ---------------------------------------------------------------------------
#
# Every DS-range literal the rewrite leaves alone gets one category, decided
# by the rules in classify() from what the literal is next to; each category
# carries a verdict and the reason, which the report prints once above its
# sites.  The report is generated, never edited by hand:
#
#     python tools/dsaddr.py --report <aletheia>/src/matched -o docs/dsaddr-residue.md

# Main RAM below this holds the ARM9 static image and every overlay; a
# literal under it (and above the base) names game code or data.
IMAGE_HI = 0x02400000

BITWISE_OPS = {"&", "|", "^", "~", "<<", ">>", "&=", "|=", "^=", "<<=", ">>="}
COMPARE_OPS = {"<", "<=", ">", ">=", "==", "!="}
MD5_MACROS = {"FF", "GG", "HH", "II"}

RESOLVED, HARMLESS, ASM, PORT = "resolved", "harmless", "asm", "port"

# category: (verdict, title, reason)
CATEGORIES = {
    "macro-arg": (RESOLVED, "Register macro arguments",
        "The argument of a function-like macro defined in the same file whose "
        "pointer cast of that parameter now goes through bw_ds_ptr: every "
        "invocation in the file passes a DS address, so the macro body is "
        "rewritten and the literal reaches bw_ds_ptr through it."),
    "callee-arg": (RESOLVED, "Address arguments of translating callees",
        "The address argument of a function in CALLEES, whose own casts of "
        "that parameter go through bw_ds_ptr: a literal, or a pointer cast "
        "of a literal that the tool leaves alone at this position so the "
        "callee receives the DS address (see \"CALLEES and their callers\" "
        "below for the evidence)."),
    "define-resolved": (RESOLVED, "Constants used only as pointer-cast operands",
        "A file-local #define whose every use is the operand of a pointer "
        "cast (or the argument of a rewritten macro); each use is rewritten "
        "at the use site."),
    "asm": (ASM, "Operands in asm bodies",
        "An operand (or a constant used only as one) of an instruction in an "
        "mwcc asm function body.  Those files are transliterated by the "
        "asmsdk lane; the address is handled there."),
    "unused-define": (HARMLESS, "Constants nothing uses",
        "A file-local #define with no use in the file; the value never "
        "reaches the program."),
    "directive": (HARMLESS, "Preprocessor arithmetic",
        "Inside an #if or another directive; evaluated by the preprocessor "
        "and never part of the program."),
    "decimal": (HARMLESS, "Decimal numbers",
        "A decimal constant that happens to fall in the DS range (powers of "
        "ten for digit conversion); a number, not an address."),
    "md5": (HARMLESS, "MD5 round constants",
        "The additive constant of an MD5 round macro; a number, not an "
        "address."),
    "pxi": (HARMLESS, "PXI command words",
        "A word for the ARM7 (sent through PXI_SendWordByFifo or an SDK "
        "wrapper of it, or stored into a PXI send buffer): command and "
        "parameter bits that happen to fall in the DS range, not an "
        "address.  The port's ARM7 stand-in decodes the same word."),
    "mask": (HARMLESS, "Bit patterns",
        "An operand of a bitwise operator: a mask, a field of a control "
        "word (DISPCNT, MCCNT1, a DMA control word, a PXI command), not an "
        "address.  The value is the same on the host."),
    "range": (HARMLESS, "Range checks on DS address values",
        "A bound in a comparison whose other side is a DS address held as an "
        "integer (a ROM header field, an overlay's load address, a register "
        "value).  The comparison gives the same answer on the host as long "
        "as that value is still a DS address, which it is when it comes "
        "from game data."),
    "range-host": (PORT, "Range checks on converted pointers",
        "A bound in a comparison with an integer conversion of a pointer "
        "(`(u32)ptr`).  On the host the pointer is a host address, so the "
        "test answers differently (and on LP64 the conversion truncates); "
        "the function needs a port replacement or bw_ds_addr_of."),
    "macro-arg-mixed": (PORT, "Arguments of register macros left alone",
        "The argument of a file-local macro that casts its parameter to a "
        "pointer, where some other invocation passes a value the tool cannot "
        "show is a DS address; the macro is left alone and its DS "
        "invocations are wild pointers on the host."),
    "pointer-param": (PORT, "Integers passed where the callee takes a pointer",
        "The caller declares the parameter as an integer and passes a DS "
        "address; the matched definition takes a pointer and dereferences "
        "it.  On the host the two translation units disagree on the type "
        "(on LP64 the argument is not even the width of the parameter).  "
        "Port answer: a wrapper include for the caller that declares the "
        "callee with its pointer type, so the argument becomes a pointer "
        "cast this tool rewrites, or a port replacement of the callee that "
        "takes the integer and calls bw_ds_ptr."),
    "call-int": (PORT, "Hardware addresses passed to integer parameters",
        "A hardware address (I/O, VRAM, palette, OAM, WRAM) passed to a "
        "function whose matched definition takes an integer.  Before the "
        "function joins CALLEES its body must be checked: every cast of that "
        "parameter must be an address use, and every caller must pass a DS "
        "address."),
    "call-unknown": (PORT, "Hardware addresses passed to functions not in the corpus",
        "A hardware address passed to a function with no matched "
        "definition (or one under another name).  The answer depends on "
        "the callee: the port's implementation of it must translate the "
        "address, or the callee joins CALLEES once it is matched."),
    "call-image": (PORT, "Game code or data addresses passed as integers",
        "An address inside the ARM9 or an overlay (a callback, a data "
        "table, a heap or task descriptor) passed as an integer.  On the "
        "host the code and data live at host addresses; the port needs the "
        "symbol map (the address to the host object or function) wherever "
        "the callee uses it."),
    "call-main": (PORT, "Main RAM addresses passed as integers",
        "A main RAM address outside the game image (the shared work area, "
        "a region bound) passed as an integer.  The callee decides whether "
        "it is used as an address; listed so each is looked at when its "
        "callee enters the build."),
    "held-image": (PORT, "Game code or data addresses held as integers",
        "An address inside the ARM9 or an overlay stored, returned or cast "
        "to an integer type.  Needs the port's symbol map when the integer "
        "is used as an address."),
    "held-hw": (PORT, "Hardware addresses held as integers",
        "A DS hardware or work-area address stored in a variable, a field, "
        "an enum or a return value, to be cast to a pointer later.  The "
        "later cast is of a non-literal and is not rewritten, so it needs "
        "a port answer (a translating accessor, or a rewrite of that "
        "function)."),
    "arith": (PORT, "Address arithmetic outside a pointer cast",
        "A DS address in arithmetic whose result is used as an address "
        "(as an argument, or cast after the arithmetic with the literal "
        "not leading).  Needs a port answer where the result is used."),
    "define-other": (PORT, "Constants with uses other than pointer casts",
        "A file-local address #define with at least one use that is not a "
        "pointer-cast operand (an argument, arithmetic held in an "
        "integer); the uses that are casts are rewritten, the others need "
        "the answer their own category gives."),
    "static-init": (PORT, "Pointer casts in constant initializers",
        "A pointer cast in a static initializer, which C requires to be a "
        "constant expression; bw_ds_ptr is a call.  Needs the object to be "
        "initialised at run time by the port."),
    "funcptr": (PORT, "Function pointer casts",
        "A cast of a DS address to a function pointer type; needs the "
        "port's symbol map."),
}

ORDER = list(CATEGORIES)


_HOST_PTR_RE = re.compile(
    r"\(\s*(?:u32|unsigned(?:\s+int|\s+long)?|uintptr_t|int|s32)\s*\)\s*&?\s*[A-Za-z_]")

# Functions whose argument is a PXI word (the ARM7 FIFO protocol), by any
# name a matched file calls them; and the send buffers PXI words are built
# in.
PXI_CALLEES = {"PXI_SendWordByFifo", "func_02088610", "TPi_SendData"}
_PXI_STORE_RE = re.compile(r"\bsendData\s*\[[^]]*\]\s*=|#\s*define\s+\w*PXI\w*\s")

# Sites the rules above cannot decide, each with the evidence: (file,
# text on the line, category, why).
SITE_OVERRIDES = (
    ("arm9_sdk/func_0208270C.c", "addStart ==", "range-host",
     "addStart/addEnd are computed from `src`, a DMA source pointer the "
     "caller converted to u32"),
    ("arm9_sdk/func_0208270C.c", "addEnd ==", "range-host",
     "addStart/addEnd are computed from `src`, a DMA source pointer the "
     "caller converted to u32"),
    ("arm9_sdk/func_02086CBC.c", "p >= 0x05000000u", "range-host",
     "`p` is `(unsigned int)ptr`, a pointer converted two lines up"),
    ("ov/func_021CD9E0.c", "== 0x021CDC0D", "held-image",
     "compares a stored callback against a function's (Thumb) address; on "
     "the host the stored value is a host function pointer"),
)


def classify(site: Site, line_text: str, literal: str, fn_index,
             rel: str = "") -> str:
    """The report category of one residue site (see CATEGORIES)."""
    for f, text, cat, _ in SITE_OVERRIDES:
        if rel == f and text in line_text:
            return cat
    k = site.kind
    if k in ("macro-arg", "callee-arg", "define-resolved", "directive",
             "static-init", "macro-arg-mixed"):
        return k
    if k == "funcptr-cast":
        return "funcptr"
    if site.asm:
        return "asm"
    if _PXI_STORE_RE.search(line_text) or (site.call and site.call[0] in PXI_CALLEES):
        return "pxi"
    if k == "define":
        uses = site.uses or {}
        if not uses:
            return "unused-define"
        if uses.get("asm") and not uses.get("other"):
            return "asm"
        return "define-other"
    if not literal.lower().startswith("0x"):
        return "decimal"
    if site.call and site.call[0] in MD5_MACROS:
        return "md5"
    ops = {site.prev_op, site.next_op}
    if ops & BITWISE_OPS:
        return "mask"
    if ops & COMPARE_OPS:
        return "range-host" if _HOST_PTR_RE.search(line_text) else "range"
    image = site.region == "main" and DS_LO < site.value < IMAGE_HI
    if site.call and site.call[2]:
        callee, argno = site.call[0], site.call[1]
        if image:
            return "call-image"
        if site.region == "main":
            return "call-main"
        fn = fn_index.get(callee)
        if fn is None or argno >= len(fn[1]):
            return "call-unknown"
        return "pointer-param" if fn[1][argno] else "call-int"
    if image:
        return "held-image"
    if ops & {"+", "-", "*"}:
        return "arith"
    return "held-hw"


_HEADER_RE = re.compile(r"^\s*\*\s*(function|address|sdk):\s*(\S+)", re.M)


def function_index(files):
    """name -> (file, [is_pointer per parameter]) for every function a
    matched file defines, under its own name, its func_<address> name and
    its SDK name."""
    index = {}
    for rel, src in files:
        head = src[:3000]
        names = {}
        for mo in _HEADER_RE.finditer(head):
            names[mo.group(1)] = mo.group(2)
        fn = names.get("function")
        if not fn:
            continue
        d = re.search(r"\b%s\s*\(([^)]*)\)\s*\{" % re.escape(fn), src)
        if not d:
            continue
        params = [p.strip() for p in d.group(1).split(",")]
        if params in ([""], ["void"]):
            params = []
        ptr = ["*" in p or "[" in p for p in params]
        aliases = {fn}
        if names.get("address", "").startswith("0x"):
            aliases.add("func_" + names["address"][2:].upper())
        if names.get("sdk"):
            aliases.add(names["sdk"])
        for a in aliases:
            index.setdefault(a, (rel, ptr))
    return index


def callee_calls(files):
    """Every call of a CALLEES function: (target, file, line, argument)."""
    out = []
    pat = re.compile(r"\b(%s)\s*\(" % "|".join(sorted(map(re.escape, CALLEE_NAMES))))
    for rel, src in files:
        if not pat.search(src):
            continue
        m = mask(src)
        defs = _defines(m)
        for mo in pat.finditer(m):
            p = _skip_ws_back(m, mo.start() - 1)
            if p >= 0 and (m[p].isalnum() or m[p] == "_" or m[p] == "*"):
                s = p
                while s > 0 and (m[s - 1].isalnum() or m[s - 1] == "_"):
                    s -= 1
                if m[s:p + 1] != "return":
                    continue        # a declaration or a definition
            open_ = mo.end() - 1
            close = _match_fwd(m, open_)
            if close < 0:
                continue
            target = CALLEE_NAMES[mo.group(1)]
            args = _split_args(m, open_, close)
            for k in sorted(CALLEES[target]["params"].values()):
                if k < len(args):
                    a, b = args[k]
                    text = re.sub(r"\s+", " ", src[a:b].strip())
                    line = src.count("\n", 0, mo.start()) + 1
                    if ds_lead(m[a:b], defs) is not None:
                        how = "yes"
                    elif _DS_PTR_ARG_RE.match(m[a:b].strip()) and ds_lead(
                            m[a:b].strip().split(")", 1)[1], defs) is not None:
                        how = "yes (pointer cast kept)"
                    else:
                        how = "check"
                    out.append((target, rel, line, text, how))
    return out


# `(void *)0x04000050`: a pointer cast of a DS literal as a whole argument.
_DS_PTR_ARG_RE = re.compile(r"\(\s*[A-Za-z_][\w\s]*\*[\s*]*\)\s*\(?\s*0[xX]")


def _md_code(text: str) -> str:
    text = text.replace("`", "'").replace("|", "\\|")
    return "`%s`" % text if text else ""


def build_report(root: str) -> str:
    files = []
    for d, _, names in os.walk(root):
        for nm in names:
            if nm.endswith(".c"):
                path = os.path.join(d, nm)
                files.append((os.path.relpath(path, root).replace("\\", "/"),
                              _read(path)))
    files.sort()
    fn_index = function_index(files)
    by_cat = defaultdict(list)
    via = Counter()
    n_rewrites = 0
    macros = []
    for rel, src in files:
        rewrites, residue = analyse(src)
        n_rewrites += len(rewrites)
        lines = None
        seen_macros = set()
        for s in rewrites:
            if s.via:
                via[s.via.split()[0]] += 1
                if s.via.startswith("macro") and s.via not in seen_macros:
                    seen_macros.add(s.via)
                    macros.append((rel, s.line, s.via.split(None, 1)[1], s.type))
        for s in residue:
            if lines is None:
                lines = src.split("\n")
            text = lines[s.line - 1].strip()
            cat = classify(s, text, src[s.start:s.end], fn_index, rel)
            why = [o[3] for o in SITE_OVERRIDES if o[0] == rel and o[1] in text]
            if why:
                detail = why[0]
            elif s.kind in ("define", "define-resolved"):
                detail = s.note + (" (uses: %s)" % ", ".join(
                    "%s %d" % kv for kv in sorted((s.uses or {}).items()))
                    if s.uses else " (no uses)")
            elif s.call:
                detail = "argument %d of %s" % (s.call[1] + 1, s.call[0])
            elif s.kind == "macro-arg-mixed":
                detail = "macro " + (s.note or "")
            else:
                detail = ""
            by_cat[cat].append((rel, s.line, s.value, s.region, text, detail))
    calls = callee_calls(files)

    total = sum(len(v) for v in by_cat.values())
    verdicts = Counter()
    for cat, sites in by_cat.items():
        verdicts[CATEGORIES[cat][0]] += len(sites)
    out = []
    w = out.append
    w("# DS address residue")
    w("")
    w("Generated by `python tools/dsaddr.py --report <aletheia>/src/matched "
      "-o docs/dsaddr-residue.md`; do not edit by hand.  docs/boot.md, "
      "\"Fixed addresses\", is the design.")
    w("")
    w("`tools/dsaddr.py` rewrites every pointer cast of a DS address it can "
      "prove (a literal, a file-local constant, the parameter of a "
      "file-local register macro whose every invocation passes a DS "
      "address, or an address parameter of a function in its CALLEES "
      "table) into a call of `bw_ds_ptr`.  This file lists every DS-range "
      "literal it leaves alone, each with a category, the category's "
      "verdict and the reason.")
    w("")
    w("Corpus: %d matched files, %d pointer casts rewritten (%s)." % (
        len(files), n_rewrites,
        ", ".join("%d through a %s" % (via[k], k) for k in ("define", "macro", "callee")
                  if via[k])))
    w("")
    w("| verdict | sites | meaning |")
    w("|---|---:|---|")
    meaning = {
        RESOLVED: "reaches bw_ds_ptr through a rewrite elsewhere in the file or callee",
        HARMLESS: "a number, not an address; the same on the host",
        ASM: "inside an asm body; the asmsdk lane handles it",
        PORT: "needs a port answer when its file enters the build",
    }
    for v in (RESOLVED, HARMLESS, ASM, PORT):
        w("| %s | %d | %s |" % (v, verdicts[v], meaning[v]))
    w("| total | %d | |" % total)
    w("")
    w("| category | verdict | sites |")
    w("|---|---|---:|")
    for cat in ORDER:
        if by_cat.get(cat):
            w("| [%s](#%s) | %s | %d |" % (CATEGORIES[cat][1], cat,
                                           CATEGORIES[cat][0], len(by_cat[cat])))
    w("")
    for cat in ORDER:
        sites = by_cat.get(cat)
        if not sites:
            continue
        verdict, title, reason = CATEGORIES[cat]
        w('<a id="%s"></a>' % cat)
        w("")
        w("## %s (%s, %d)" % (title, verdict, len(sites)))
        w("")
        w(reason)
        w("")
        w("| site | value | context | detail |")
        w("|---|---|---|---|")
        for rel, line, value, region, text, detail in sorted(sites):
            w("| %s:%d | 0x%08X %s | %s | %s |" % (
                rel, line, value, region, _md_code(text[:100]), detail))
        w("")
    w("## CALLEES and their callers")
    w("")
    w("Every call of a function in the CALLEES table, with the address "
      "argument it passes.  The function translates that parameter itself, "
      "so a caller must pass the DS address: \"yes\" when a literal or a "
      "file-local DS constant leads the argument, \"yes (pointer cast "
      "kept)\" when the argument is a pointer cast of a DS literal, which "
      "the tool leaves alone at this position so the DS address is what "
      "arrives.  \"check\" is a caller that passes a pointer reached some "
      "other way (the address of a rewritten register macro, a local "
      "pointer variable): on the host that is a host address, which the "
      "callee's bw_ds_ptr rejects, so each one needs a port answer (a "
      "wrapper that hands the callee the DS address) before its file "
      "enters the build.  A caller whose prototype has a pointer parameter "
      "disagrees with the definition's u32 on LP64.  That mismatch is in "
      "the matched code and outside what C99 guarantees; in practice the "
      "address is the first argument, which every calling convention eidos "
      "targets passes in a register (or a 4-byte stack slot on 32-bit "
      "x86), so the callee reads the DS address from its low 32 bits.")
    w("")
    w("| callee | site | argument | DS |")
    w("|---|---|---|---|")
    for target, rel, line, text, how in sorted(calls):
        w("| %s | %s:%d | %s | %s |" % (target, rel, line, _md_code(text), how))
    w("")
    w("## Register macros rewritten")
    w("")
    w("| macro | defined at | cast to |")
    w("|---|---|---|")
    for rel, line, name, type_ in sorted(macros):
        w("| %s | %s:%d | %s |" % (_md_code(name), rel, line, _md_code(type_)))
    w("")
    return "\n".join(out)


def _need(source: str, old: str, new: str) -> str:
    """A patch of aletheia's text that must find that text.  Without it the
    copy keeps the DS layout silently, so a change in aletheia stops the
    build here instead."""
    if old not in source:
        raise SystemExit("dsaddr.py: the text to patch is gone; review the "
                         "patch against aletheia: %r" % old)
    return source.replace(old, new)


def _need_re(pattern, repl, source: str, flags: int = 0) -> str:
    """_need for a pattern."""
    out, count = re.subn(pattern, repl, source, flags=flags)
    if count == 0:
        raise SystemExit("dsaddr.py: the text to patch is gone; review the "
                         "patch against aletheia: %r" % pattern)
    return out


def native_matched_types(source: str, path: str) -> str:
    """Keep recovered DS scalar names fixed-width in generated host copies."""
    source = re.sub(
        r"(?m)^([ \t]*typedef[ \t]+(?:volatile[ \t]+)?(?:unsigned[ \t]+|signed[ \t]+)?)"
        r"long([ \t]+(?:u32|s32|vu32|vs32)[ \t]*;)", r"\1int\2", source)
    # convert.o spells these shared SDK fields directly rather than as u32.
    source = re.sub(r"typedef struct (RTCDate|RTCTime)\b.*?\} \1;",
                    lambda m: re.sub(r"\bunsigned\s+long\b(?!\s+long)",
                                     "unsigned int", m.group()), source, flags=re.S)
    if path.replace("\\", "/").endswith("/src/matched/arm9/func_02035F38.c"):
        # KEY_MainSys's getter returns the native Key object, not an integer.
        source = _need(source, "extern u32 func_0203521C(u32 *p);",
                                "extern void *func_0203521C(u32 *p);")
    return source


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inputs", nargs="*")
    ap.add_argument("-o", "--output")
    ap.add_argument("--name", help="path the #line directive names (default: the input)")
    ap.add_argument("--survey", metavar="DIR")
    ap.add_argument("--report", metavar="DIR",
                    help="write the residue report for DIR (to -o, or stdout)")
    ap.add_argument("--json", metavar="FILE")
    ap.add_argument("--show", type=int, default=4, metavar="N",
                    help="--survey: example sites per residue group (0: none)")
    ap.add_argument("--cut-list", metavar="FILE",
                    help="src/game: lines `<path>\t<name>,<name>...` naming the "
                         "function definitions to cut from this file's copy "
                         "(tools/gamecut.py, docs/game-tree.md)")
    ap.add_argument("--cut-decls", action="store_true",
                    help="with --cut-list: the cut functions leave no "
                         "declaration (a port patch defines them with "
                         "another type)")
    ap.add_argument("--cut-asm", action="store_true",
                    help="src/game: cut every mwcc asm body from the copy")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any input would be rewritten")
    a = ap.parse_args(argv)
    if a.survey:
        return survey(a.survey, a.json, a.show)
    if a.report:
        text = build_report(a.report)
        if a.output:
            with open(a.output, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        else:
            sys.stdout.write(text)
        return 0
    if a.check:
        bad = 0
        for p in a.inputs:
            _, rw, _ = rewrite(_read(p), p)
            for s in rw:
                print("%s:%d: 0x%08X (%s)" % (p, s.line, s.value, s.type))
            bad += bool(rw)
        return 1 if bad else 0
    if len(a.inputs) != 1 or not a.output:
        ap.error("give one input and -o OUTPUT, or --survey DIR, or --check")
    src_path = a.inputs[0]
    name = a.name or os.path.abspath(src_path)
    source = _read(src_path)
    if "/src/matched/" in os.path.abspath(src_path).replace("\\", "/"):
        source = native_matched_types(source, src_path)
    if src_path.replace("\\", "/").endswith("/src/game/arm9/tcb_list.c"):
        source = "#include <stddef.h>\n" + source
        source = _need(source, "return (idx << 5) + 0x38;",
                                "return offsetof(struct TcbSystem, freeStackStorage) + "
                                "idx * (sizeof(struct TcbTask *) + sizeof(struct TcbTask));")
    if src_path.replace("\\", "/").endswith("/src/game/arm9/card_backup.c"):
        # ARM7 consumes the original 32-bit command protocol. Native buffer
        # pointers must cross that boundary as registered DS addresses.
        source = "#include <stdint.h>\nextern unsigned int bw_ds_wire_addr(uintptr_t);\n" + source
        source = _need(source, "const u32 arg = (uptr)p->cmd;",
                                "const u32 arg = bw_ds_wire_addr((uintptr_t)p->cmd);")
        source = _need_re(r"(p->cmd->(?:src|dst)\s*=\s*)([^;]+);",
                        r"\1bw_ds_wire_addr((uintptr_t)(\2));", source)
    if 'include/G_EnvModule.h"' in source and "struct EnvEntry {" in source:
        # The heap manager and its readers must agree with src/game's
        # pointer-typed HeapRecord at every host pointer width.
        source = _need_re(r"\bu32\s+field04;", "void *field04;", source)
        source = _need_re(r"\bu32\s+field08;", "void *field08;", source)
        source = source.replace("u32 GFL_HEAP_GetParentHeap(", "void *GFL_HEAP_GetParentHeap(")
        source = source.replace("u32 GFL_HEAP_GetBlock(", "void *GFL_HEAP_GetBlock(")
    if src_path.replace("\\", "/").endswith("/src/game/arm9/nns_fnd_expheap.c"):
        # The native common header grows with its link and child-list
        # pointers. Its recovered aliases must use those same offsets.
        source = "#include <stddef.h>\n" + source
        source = _need(source, "pad_00[0x18]", "pad_00[offsetof(NNSiFndHeapHead, heapStart)]")
        source = _need(source, "pad00[0x0c]", "pad00[offsetof(NNSiFndHeapHead, childList)]")
        source = _need(source, "u32 min_val;", "uptr min_val;")
        source = _need(source, "u32 max_val;", "uptr max_val;")
        source = _need(source, "struct DList *list, u32 val", "struct DList *list, uptr val")
        source = _need(source, "NNSi_FndFindListContainHeap(u32 a)", "NNSi_FndFindListContainHeap(uptr a)")
        source = _need(source, "NNSi_FndInitMBlock(int *p", "NNSi_FndInitMBlock(sptr *p")
        source = _need(source, "int  pair[2];", "sptr pair[2];")
        source = _need(source, "(u8 *)heap + 0x38", "(u8 *)heap + sizeof(struct ExpHeap)")
        source = _need(source, "(char *)q + 0x10", "(char *)q + sizeof(struct MBlock)")
        source = _need(source, "NNS_FndInitList(&pHeapHd->childList, 4)",
                                "NNS_FndInitList(&pHeapHd->childList, offsetof(NNSiFndHeapHead, link))")
        source = _need(source, "NNS_FndInitList(&sRootList, 4)",
                                "NNS_FndInitList(&sRootList, offsetof(NNSiFndHeapHead, link))")
    if src_path.replace("\\", "/").endswith("/src/game/arm9/os_arena.c"):
        # The recovered host form widened the arena pointer but left its
        # unsigned alignment mask at 32 bits, clearing the high address.
        source = _need(source, "u32 inv;", "uptr inv;")
        source = _need(source, "u32 mask;", "uptr mask;")
        source = _need(source, "(u32)(n)", "(uptr)(n)")
    if src_path.replace("\\", "/").endswith("/src/game/arm9/tcbl.c"):
        # src/game has real pointers, but these two row-stride literals
        # still count DS header bytes. Adapt only the generated host copy.
        source = _need(source, "tcbl->workSize + 0x18",
                                "tcbl->workSize + sizeof(struct TcblTask)")
        source = _need(source, "(((workSize + 3) & ~3) + 0x18)",
                                "(((workSize + sizeof(void *) - 1) & ~(sizeof(void *) - 1)) + sizeof(struct TcblTask))")
        source = _need(source, "tcbl->workSize = workSize;",
                                "tcbl->workSize = (workSize + sizeof(void *) - 1) & ~(sizeof(void *) - 1);")
        source = "#define tcbl_File sub_0203101C_File\n" + source
    if a.cut_list or a.cut_asm:
        import gamecut
        drop = set()
        renames = {}
        if a.cut_list and os.path.exists(a.cut_list):
            key = os.path.normcase(os.path.abspath(src_path)).replace("\\", "/")
            with open(a.cut_list, encoding="utf-8") as fh:
                for line in fh:
                    cols = line.rstrip("\n").split("\t")
                    path, names = cols[0], cols[1] if len(cols) > 1 else ""
                    if os.path.normcase(os.path.abspath(path)).replace("\\", "/") == key:
                        drop.update(n for n in names.split(",") if n)
                        # the third column: definitions renamed, so the
                        # unit's own calls reach the name's slot
                        # (tools/ovlink.py, self_calls)
                        for pair in (cols[2] if len(cols) > 2 else "").split(","):
                            old, _, new = pair.partition("=")
                            if old and new:
                                renames[old] = new
        source, _cut = gamecut.cut(source, drop, a.cut_asm, a.cut_decls)
        if renames:
            source = gamecut.rename_defs(source, renames)
        spath = os.path.abspath(src_path).replace("\\", "/")
        if "/src/game/" in spath:
            # declarations that widen an 8/16-bit return (gamecut.fix_returns)
            source = gamecut.fix_returns(source, gamecut.narrow_returns(
                spath[:spath.rindex("/src/game/")]))
            # functions that fall off their end (tools/gamereturns.txt)
            source = gamecut.add_returns(source, gamecut.returns_table())
            # mwcc's hand-written va_start/va_arg (gamecut.fix_va_macros)
            source = gamecut.fix_va_macros(source)
            _rewrite_local_headers(src_path, a.output, set())
            # Make a newly introduced host header a visible source change.
            # Otherwise Ninja's old depfile still names the original header,
            # and an unchanged .c can keep an object built with its old ABI.
            out_dir = os.path.dirname(os.path.abspath(a.output))

            def _host_include(m):
                header = os.path.normpath(os.path.join(out_dir, m.group(1)))
                if os.path.isfile(header):
                    return m.group(0).replace('"%s"' % m.group(1),
                                              '"%s"' % header.replace("\\", "/"))
                return m.group(0)

            source = _LOCAL_INCLUDE_RE.sub(_host_include, source)
            text = _rewrite_with_defines(source, name, src_path)
            _write(a.output, text)
            return 0
    text, _, _ = rewrite(source, name)
    _write(a.output, text)
    return 0


_NUM_DEFINE_RE = re.compile(r"(?m)^[ \t]*#[ \t]*define[ \t]+[A-Za-z_]\w*[ \t]+\(?\s*0[xX][0-9A-Fa-f]+[uUlL]*\s*\)?[ \t]*$")
_IOREG_DEFINE_RE = re.compile(
    r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+"
    r"IOREG_ADDR\([ \t]*(0[xX][0-9A-Fa-f]+[uUlL]*)[ \t]*\)[ \t]*$")
_PREFIX_BEGIN = "/* tools/dsaddr.py: address constants of the included headers */\n"
_PREFIX_END = "/* tools/dsaddr.py: end of the included constants */\n"


def _header_defines(path, seen):
    """The `#define NAME 0x...` lines of the headers `path` includes with
    quotes, recursively (src/game/sdk_ioreg.h's REG_DISPCNT_ADDR), so a cast
    of such a name to a pointer (`(*(vu32 *)REG_DISPCNT_ADDR)`, in the unit
    or in sdk_ioreg_reg.h) is seen as the fixed address it is."""
    out = []
    base = os.path.dirname(os.path.abspath(path))
    for inc in _LOCAL_INCLUDE_RE.findall(_read(path)):
        hp = os.path.normpath(os.path.join(base, inc))
        if hp in seen or not os.path.isfile(hp):
            continue
        seen.add(hp)
        text = _IOREG_ADDR_RE.sub(r"\1", _read(hp))
        out.extend(m.group(0) for m in _NUM_DEFINE_RE.finditer(text))
        # Aletheia's pointer-sized address wrapper still denotes the same
        # DS register. Feed its numeric value to the address rewriter.
        out.extend("#define %s %s" % m.groups()
                   for m in _IOREG_DEFINE_RE.finditer(text))
        out.extend(_header_defines(hp, seen))
    return out


def _rewrite_with_defines(text, name, path):
    """rewrite(text) with the address constants of `path`'s headers known;
    the constants themselves are not part of the result."""
    defs = _header_defines(path, set())
    if not defs:
        return rewrite(text, name)[0]
    prefix = _PREFIX_BEGIN + "\n".join(defs) + "\n" + _PREFIX_END
    out, _, _ = rewrite(prefix + text, name)
    i = out.find(_PREFIX_BEGIN)
    j = out.find(_PREFIX_END)
    if i < 0 or j < i:
        return rewrite(text, name)[0]
    return out[:i] + out[j + len(_PREFIX_END):]


_LOCAL_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)


def _rewrite_local_headers(src_path, out_path, seen):
    """Rewrite the quoted headers of a src/game unit that hold fixed DS
    addresses (nns_g3d_sbc_types.h's GXFIFO, `*(volatile u32 *)0x04000400`)
    to the copy's tree, at the path the include names relative to the copy.
    A quoted include looks in the including file's directory first, so the
    copy compiles against them and the hardware writes reach bw_ds_ptr;
    otherwise they land in plain memory and the 3D engine never sees them.
    A header on the way to one is copied too.  Returns True when the header
    at out_path was written."""
    src_dir = os.path.dirname(os.path.abspath(src_path))
    out_dir = os.path.dirname(os.path.abspath(out_path))
    wrote_any = False
    for inc in _LOCAL_INCLUDE_RE.findall(_read(src_path)):
        hp = os.path.normpath(os.path.join(src_dir, inc))
        if hp in seen or not os.path.isfile(hp):
            continue
        seen.add(hp)
        oh = os.path.normpath(os.path.join(out_dir, inc))
        own = _read(hp)
        text = _rewrite_with_defines(own, hp, hp)
        if hp.replace("\\", "/").endswith("/src/game/arm9/card_common.h"):
            # Only the shared command is wire data. CARDiCommon's working
            # pointers remain pointer-sized on the host.
            text = _need_re(r"(typedef struct CARDiCommandArg\b.*?} CARDiCommandArg;)",
                          lambda m: m.group(0).replace("uptr src;", "u32 src;")
                                                   .replace("uptr dst;", "u32 dst;"),
                          text, flags=re.S)
        rewrites = text != own
        nested = _rewrite_local_headers(hp, oh, seen)
        if rewrites or nested:
            # The copy's own quoted includes: one the copy tree holds now
            # resolves next to it; any other names the original by its full
            # path, which the copy's directory would not find
            # (sdk_ioreg_reg.h's "sdk_ioreg.h").
            hdir, odir = os.path.dirname(hp), os.path.dirname(oh)

            def _abs(m):
                if os.path.isfile(os.path.join(odir, m.group(1))):
                    return m.group(0)
                full = os.path.normpath(os.path.join(hdir, m.group(1))).replace("\\", "/")
                return m.group(0).replace('"%s"' % m.group(1), '"%s"' % full)
            text = _LOCAL_INCLUDE_RE.sub(_abs, text)
            _write_atomic(oh, text)
            wrote_any = True
        elif os.path.isfile(oh) and os.path.normcase(oh) != os.path.normcase(hp):
            # A copy an earlier aletheia revision needed (the header held an
            # address then, ov102/irc_screen_types.h): it would shadow the
            # header the unit includes now.
            try:
                os.remove(oh)
            except OSError:
                pass
    return wrote_any


def _write_atomic(path, text):
    """_write for a file several dsaddr.py runs may produce at once."""
    data = text.encode("latin-1")
    try:
        with open(path, "rb") as f:
            if f.read() == data:
                return
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.%d.tmp" % (path, os.getpid())
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


if __name__ == "__main__":
    sys.exit(main())
