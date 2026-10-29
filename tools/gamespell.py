#!/usr/bin/env python3
"""gamespell.py -- the port's old spellings of game functions, for src/game.

The port's own C (sdk/, stubs/, tests/, demo/) calls some game functions by
the names src/matched gave them: an address spelling (sub_0202FC48) or a
descriptive name src/game has since changed (SAVEDATA_PanicFailPrintf).
src/game defines each one under its own name (GFL_HEAP_InitManager,
GF_PANIC_FailPrintf).  When the build compiles src/game
(EIDOS_SOURCE_TREE=game, docs/game-tree.md) this tool writes one header of
`#define <old> <src/game name>` lines that every port source is compiled
with, so the port keeps one spelling and links against either tree.

A spelling gets a line when:
  - a port source names it and no port source defines it: the port calls the
    game's function, and the header redirects the call;
  - a port source defines it and it is the port's own copy of the game's
    function at that address (stubs/reimpl_0204960C.c is the archive seek):
    the tree's copy of that function is cut from the build (the port
    provides the address), so the port's copy has to carry the tree's name
    for the tree's own callers to reach it.  A port source that defines the
    tree's name as well (the port has a function of its own by that name),
    that only forwards to it (sdk/aliases.c), or that stands in for another
    function (stubs/unit_members.c) keeps its own spelling, and of the
    port's spellings of one address only one takes the tree's name;
  - src/game does not define it under that name;
  - its address is known (the spelling itself, a src/matched file's
    `function:` header, or a symbols row whose name has a capital letter or
    a leading underscore) and src/game defines a function at that address.

The header follows the tree's content, not its path: it records a
fingerprint of everything it was made from (the tree's files, the port's own
sources, tools/gamespell_extra.txt and this tool), and it is written again
when that fingerprint changes.  --stamp prints the fingerprint of the same
inputs, so the build can ask whether the header is current without walking
the tree (CMakeLists.txt, "The src/game names").

    gamespell.py ALETHEIA_ROOT -o OUT.h DIR [DIR ...]
    gamespell.py --stamp ALETHEIA_ROOT -o OUT.h DIR [DIR ...]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alcache  # noqa: E402
import gametree  # noqa: E402

PORT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRA_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "gamespell_extra.txt")

ADDR = re.compile(r"^(?:func|sub)_([0-9A-Fa-f]{8})$")
IDENT_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
DEFINITION = re.compile(r"(?m)^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)[ \t]*\([^;{}]*\)\s*\{")
SYMROW = re.compile(r"^\s*\*\s+([A-Za-z_]\w*)\s+0x([0-9A-Fa-f]{8})\s+(arm|thumb)\b")
# A tree file the port's build lists name (CMakeLists.txt, cmake/boot.cmake).
TREE_FILE = re.compile(r"(?:\$\{ALETHEIA_ROOT\}/)?(src/matched/[A-Za-z0-9_/\.]+\.c)")
# A port file the port's build lists name.
PORT_FILE = re.compile(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([A-Za-z0-9_/\.]+\.c)")
# The header's own line that says what it was made from.
STAMP_LINE = re.compile(r"^/\* inputs: ([0-9a-f]+) \*/$", re.M)


def port_sources(dirs):
    """{path: text} of every port source under the given directories."""
    out = {}
    for d in dirs:
        for dirpath, _dirs, files in os.walk(d):
            for f in sorted(files):
                if not f.endswith((".c", ".h")):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    out[path] = fh.read()
    return out


def port_names(sources):
    """The names the port uses, the names it defines, and where."""
    used, defined, definer = set(), set(), {}
    for path, text in sources.items():
        # Examples in block comments are not definitions or calls.
        text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
        used.update(IDENT_CALL.findall(text))
        for name in DEFINITION.findall(text):
            defined.add(name)
            definer.setdefault(name, path)
    return used, defined, definer


def matched_addresses(root):
    """{name: address} from src/matched's symbols rows and headers."""
    out = {}
    base = os.path.join(root, "src", "matched")
    for dirpath, _dirs, files in os.walk(base):
        for f in files:
            if not f.endswith(".c"):
                continue
            with open(os.path.join(dirpath, f), encoding="utf-8", errors="replace") as fh:
                head = fh.read(16384)
            m = re.search(r"^ \* function:\s*(\w+)\s*$", head, re.M)
            a = re.search(r"^ \* address:\s*0x([0-9A-Fa-f]{8})", head, re.M)
            r = re.search(r"^ \* region:\s*(\S+)", head, re.M)
            if m and a and (r is None or not r.group(1).startswith("ov")):
                out.setdefault(m.group(1), int(a.group(1), 16))
            for line in head.split("\n"):
                s = SYMROW.match(line)
                # A symbols row may name a callee by a local's word (alloc,
                # close, value): only a name with a capital letter or a
                # leading underscore is taken from one.
                if s and re.search(r"[A-Z]|^_", s.group(1)):
                    out.setdefault(s.group(1), int(s.group(2), 16))
    return out


# ---------------------------------------------------------------------------
# what the port compiles of the tree
# ---------------------------------------------------------------------------

def cmake_blocks(text, names):
    """The text of every CMake block whose head names one of `names`."""
    out = []
    for name in names:
        head = r"(?m)^\s*(?:set|list)\s*\(\s*(?:APPEND\s+)?" + re.escape(name) + r"\b"
        for m in re.finditer(head, text):
            i = text.index("(", m.start())
            depth = 0
            for j in range(i, len(text)):
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        break
            out.append(text[i:j])
    return out


def read_text(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def build_lists():
    """The port's own file lists, out of the two build files that hold them.

    CMakeLists.txt names the library's shim sources (the SDK sources and the
    stub sources) and the tree's matched files the library compiles;
    cmake/boot.cmake names the boot's own port sources.  A path named in a
    list that a condition guards is read as if the condition holds: the
    list is then a superset of what the build compiles, which keeps the map
    on the safe side.
    """
    cmake = read_text(os.path.join(PORT_ROOT, "CMakeLists.txt"))
    boot = read_text(os.path.join(PORT_ROOT, "cmake", "boot.cmake"))
    shims, tree_files = set(), set()
    for block in cmake_blocks(cmake, ["BW_SDK_SOURCES", "BW_STUB_SOURCES"]):
        shims |= set(PORT_FILE.findall(block))
    for block in cmake_blocks(cmake, ["BW_MATCHED_SOURCES", "BW_HEAP_MATCHED",
                                      "BW_TCBL_MATCHED"]):
        tree_files |= set(TREE_FILE.findall(block))
    port_sources_ = set()
    for block in cmake_blocks(boot, ["EIDOS_BOOT_PORT_SOURCES"]):
        port_sources_ |= set(PORT_FILE.findall(block))
    return shims, port_sources_, tree_files


def tree_file_address(root, rel):
    """The address a tree file holds (its header's `address:`)."""
    path = os.path.join(root, rel.replace("/", os.sep))
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            head = fh.read(4096)
    except OSError:
        return None
    m = re.search(r"^ \* address:\s*0x([0-9A-Fa-f]{8})", head, re.M)
    return int(m.group(1), 16) if m else None


def compiled_tree_addresses(root, tree_files):
    """The addresses whose tree file the port compiles.

    A tree file the library compiles holds the tree's name for its address,
    so the port's own copy of that address keeps its own spelling: the two
    would be one function twice.
    """
    out = {}
    for rel in sorted(tree_files):
        addr = tree_file_address(root, rel)
        if addr is not None:
            out.setdefault(addr, rel)
    return out


# ---------------------------------------------------------------------------
# the map
# ---------------------------------------------------------------------------

def port_copies(sources, definer, pairs, game_at, game_names, compiled,
                 own_files, sdk_names):
    """The port's own copies of a game function: {old spelling: tree name}.

    A port source that defines a name carrying a DS address and that the
    boot compiles as the port's own code is the port's copy of the game's
    function at that address: the tree's copy of that function is cut from
    the build (the port provides the address), so the port's copy has to
    carry the tree's name for the tree's own callers to reach it.

    The port's copy keeps its own spelling when the port has a function of
    its own by the tree's name, when the tree's own file for the address is
    compiled as well (it provides the name), when the port's source only
    forwards to the tree's name (sdk/aliases.c's aliases), or when it
    stands in for another function (stubs/unit_members.c).  Of the port's
    spellings of one address only one takes the tree's name.
    """
    rows = {}
    game_addr = {}
    for addr, name in game_at.items():
        game_addr.setdefault(name, addr)
    for name, path in sorted(definer.items()):
        m = ADDR.match(name)
        if not m:
            continue
        rel = os.path.relpath(path, PORT_ROOT).replace("\\", "/")
        if rel not in own_files:
            continue        # the boot does not compile it as the port's code
        addr = int(m.group(1), 16)
        target = game_at.get(addr)
        if not target or target == name:
            continue
        if target in definer:
            continue        # the port has a function of its own by that name
        if name in game_names:
            continue        # the tree defines it under that name
        if addr in compiled:
            continue        # the tree's own file provides the name
        calls = set(IDENT_CALL.findall(sources[path]))
        if target in calls:
            continue        # a forwarder to the tree's name: it keeps its own
        if any(pairs.get(c) == target for c in calls):
            continue        # a forwarder through the map: it keeps its own
        if any(c in game_addr and c not in sdk_names
               and game_addr[c] != addr for c in calls):
            continue        # stands in for another function
        rows.setdefault(addr, []).append((name, target, path))
    out = {}
    for addr, spellings in rows.items():
        # One spelling per address takes the tree's name: the port's own copy
        # in the file named after the address (stubs/reimpl_0204960C.c) has
        # priority, the other spellings are the port's aliases for it.
        spellings.sort(key=lambda r: (0 if ("%08X" % addr) in
                                       os.path.basename(r[2]) else 1, r[0]))
        out[spellings[0][0]] = spellings[0][1]
    return out


def replacement_copies(cmake, sources, definer, pairs, game_at, game_names, sdk_names):
    """Name width-dependent replacements without renaming their forwarders."""
    files = {rel
             for block in cmake_blocks(cmake, ["BW_HEAP_REIMPL", "BW_TCBL_REIMPL"])
             for rel in PORT_FILE.findall(block)}
    return port_copies(sources, definer, pairs, game_at, game_names,
                       set(), files, sdk_names)


def fingerprint(root, dirs):
    """A digest of everything the header is made from.

    The tree's own files (their paths, sizes and times, the key
    tools/alcache.py keeps its memo under), the export's stamp (the revision
    the copy was made from), the port's own sources, tools/gamespell_extra.txt
    and this tool.  Any change to them changes the header's content, so the
    fingerprint says whether the header is current.
    """
    h = hashlib.sha1()
    h.update(alcache.fingerprint(root, ("src/game", "src/matched", "layout"))
             .encode("ascii"))
    stamp = os.path.join(root, ".eidos-aletheia-rev")
    if os.path.exists(stamp):
        with open(stamp, "rb") as fh:
            h.update(fh.read())
    for d in dirs:
        for dirpath, dirs_, files in os.walk(d):
            dirs_.sort()
            for f in sorted(files):
                path = os.path.join(dirpath, f)
                try:
                    st = os.stat(path)
                except OSError:
                    continue
                h.update(("%s\0%d\0%d" % (os.path.relpath(path, d),
                                          st.st_size, st.st_mtime_ns))
                         .encode("utf-8", "surrogatepass"))
    for path in (EXTRA_FILE, os.path.abspath(__file__)):
        if os.path.exists(path):
            with open(path, "rb") as fh:
                h.update(fh.read())
    return h.hexdigest()


def header_stamp(path):
    """The fingerprint the header says it was made from, or None."""
    if os.path.isfile(path):
        try:
            with open(path + ".inputs", encoding="utf-8") as fh:
                stamp = fh.read().strip()
            if re.fullmatch(r"[0-9a-f]{40}", stamp):
                return stamp
        except OSError:
            pass
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            m = STAMP_LINE.search(fh.read())
    except OSError:
        return None
    return m.group(1) if m else None


def spellings_of(path):
    """How many spellings a header carries."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return len(re.findall(r"^#define \w+ \w+$", fh.read(), re.M))
    except OSError:
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("root")
    ap.add_argument("-o", dest="out", required=True)
    ap.add_argument("--stamp", action="store_true",
                    help="print the fingerprint of the inputs and stop")
    ap.add_argument("dirs", nargs="+")
    a = ap.parse_args()

    stamp = fingerprint(a.root, a.dirs)
    if a.stamp:
        print(stamp)
        return 0

    # The header is current when it was made from the same inputs.  The walk
    # of the tree's src/matched is the slow part of writing it, and nothing
    # has changed, so it is skipped.
    if header_stamp(a.out) == stamp:
        print("gamespell.py: %d spellings (unchanged)"
              % spellings_of(a.out))
        return 0

    sources = port_sources(a.dirs)
    used, defined, definer = port_names(sources)
    game_at, game_names = {}, set()
    for e in gametree.entries(a.root):
        game_names.update(e["functions"])
        if e["region"].startswith("ov"):
            continue
        for n, addr in zip(e["functions"], e["addresses"]):
            if addr is not None:
                game_at.setdefault(addr, n)
    old = dict(alcache.memo(a.root, "gamespell.matched_addresses", ("src/matched",),
                            lambda: matched_addresses(a.root)))
    # Names the port still uses that the checkout's src/matched no longer
    # spells (renamed there too): tools/gamespell_extra.txt, `name address`.
    if os.path.exists(EXTRA_FILE):
        with open(EXTRA_FILE, encoding="utf-8") as fh:
            for line in fh:
                parts = line.split("#", 1)[0].split()
                if len(parts) == 2:
                    old.setdefault(parts[0], int(parts[1], 16))
    pairs = {}
    for name in sorted(used - defined - game_names):
        m = ADDR.match(name)
        if not m and not re.search(r"[A-Z]|^_", name):
            continue        # memset, close: a C library or local's word
        addr = int(m.group(1), 16) if m else old.get(name)
        target = game_at.get(addr) if addr is not None else None
        if target and target != name:
            pairs[name] = target
    # The port's own copies of a game function: the tree's copy is cut from
    # the build, so the port's copy carries the tree's name.
    shims, boot_sources, tree_files = build_lists()
    compiled = compiled_tree_addresses(a.root, tree_files)
    sdk_names = set(gametree._symbol_table(a.root, "layout/symbols_sdk.txt"))
    for name, target in port_copies(sources, definer, pairs, game_at,
                                    game_names, compiled,
                                    shims | boot_sources, sdk_names).items():
        if target != name and name not in game_names:
            pairs[name] = target
    # These lists replace matched sources at a different pointer width.
    # They must export the game's name even though build_lists' superset
    # also includes the mutually exclusive matched source at that address.
    cmake = read_text(os.path.join(PORT_ROOT, "CMakeLists.txt"))
    pairs.update(replacement_copies(cmake, sources, definer, pairs, game_at,
                                    game_names, sdk_names))
    lines = ["#define %s %s\n" % (name, pairs[name]) for name in sorted(pairs)]
    text = ("/* Generated by tools/gamespell.py: the port's spellings of game\n"
            " * functions src/game names otherwise (docs/game-tree.md).  A target\n"
            " * that compiles its own src/matched copies with its own renames\n"
            " * (tests/test_pxi.c and the like) defines EIDOS_NO_GAMESPELL. */\n"
            "/* inputs: %s */\n"
            "#if !defined(EIDOS_GAMESPELL_H) && !defined(EIDOS_NO_GAMESPELL)\n"
            "#define EIDOS_GAMESPELL_H\n" % stamp
            + "".join(lines) + "#endif\n")
    old_text = None
    if os.path.exists(a.out):
        with open(a.out, encoding="utf-8") as fh:
            old_text = fh.read()
    # A source-body edit can change the fingerprint without changing any
    # spelling. Keep that metadata separate from the forced include: a
    # comment-only header rewrite otherwise recompiles the whole game.
    if STAMP_LINE.sub("", old_text or "") != STAMP_LINE.sub("", text):
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    with open(a.out + ".inputs", "w", encoding="utf-8") as fh:
        fh.write(stamp + "\n")
    print("gamespell.py: %d spellings" % len(lines))


if __name__ == "__main__":
    main()
