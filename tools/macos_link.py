#!/usr/bin/env python3
"""Link Mach-O executables with ELF-style optional weak references.

Apple ld requires -U even for an undefined weak reference with no dylib
provider. Allow only explicitly weak symbols; other undefined references
still fail the link. Inspect objects, not C spellings,
so generated game-name mappings and per-target defines are respected.

Apple ld also loads an archive member to satisfy a weak reference, where
GNU ld and lld load members only for strong ones. A member brought in that
way brings its own references (the game's functions a tool does not have)
and its definitions (doubles of a tool's own). So the archives are first
resolved as an ELF linker would, and an archive whose members Apple ld
would load only for weak references is given to it as a copy without them.
EIDOS_MACOS_LINK_VERBOSE=1 lists the members left out.
"""
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def weak_symbols(output):
    weak, required = set(), set()
    for line in output.splitlines():
        if "(undefined)" not in line or "external " not in line:
            continue
        symbol = line.split("external ", 1)[1].split()[0]
        (weak if "weak external " in line else required).add(symbol)
    return weak, required


def symbol_table(output):
    """nm -m output: [(file or archive member, defined, strong refs,
    weak refs)], in order."""
    tables, current = [], None
    for line in output.splitlines():
        if line.endswith(":") and not line.startswith(" ") and "(undefined)" not in line \
                and not re.match(r"[0-9a-f]{8,16} ", line):
            current = (line[:-1], set(), set(), set())
            tables.append(current)
            continue
        if current is None or " external " not in line:
            continue
        symbol = line.split("external ", 1)[1].split()[0]
        if "(undefined)" in line:
            (current[3] if "weak external " in line else current[2]).add(symbol)
        else:
            current[1].add(symbol)
    return tables


def read_symbols(nm, path):
    """The symbols of one object or of each member of one archive."""
    output = subprocess.check_output([nm, "-m", path], text=True, stderr=subprocess.DEVNULL)
    tables = symbol_table(output)
    if not path.endswith(".a"):
        if not tables:
            tables = symbol_table(path + ":\n" + output)
        defined, strong, weak = set(), set(), set()
        for _, d, s, w in tables:
            defined |= d
            strong |= s
            weak |= w
        return [(path, defined, strong, weak)]
    # Apple nm heads each member with its own name ("member.o:"); others
    # write "archive(member.o):"
    return [((t[0][len(path) + 1:-1] if t[0].startswith(path + "(") else t[0]),) + t[1:]
            for t in tables]


def weak_only_members(nm, objects, archives):
    """{archive: (member count, [(position, member, weak symbols it
    answers)])}: the members an ELF linker leaves out that Apple ld would
    load for weak references."""
    defined, strong, weak = set(), set(), set()
    for path in objects:
        for _, d, s, w in read_symbols(nm, path):
            defined |= d
            strong |= s
            weak |= w
    members = {a: read_symbols(nm, a) for a in archives}
    loaded = {a: set() for a in archives}
    changed = True
    while changed:
        changed = False
        for a in archives:
            for i, (_, d, s, w) in enumerate(members[a]):
                if i not in loaded[a] and d & (strong - defined):
                    loaded[a].add(i)
                    defined |= d
                    strong |= s
                    weak |= w
                    changed = True
    result = {}
    for a in archives:
        out = []
        for i, (name, d, _, _) in enumerate(members[a]):
            answers = d & (weak - defined)
            if i not in loaded[a] and answers:
                out.append((i, name, sorted(answers)))
        if out:
            result[a] = (len(members[a]), out)
    return result


def archive_members(path):
    """The members of a BSD/Apple ar archive, symbol table excluded:
    [(name, the member's bytes from its header to its end)]."""
    with open(path, "rb") as stream:
        data = stream.read()
    if not data.startswith(b"!<arch>\n"):
        return None
    members, pos = [], 8
    while pos + 60 <= len(data):
        header = data[pos:pos + 60]
        name = header[:16].decode("ascii", "replace").strip()
        size = int(header[48:58].decode("ascii").strip())
        start = pos + 60
        if name.startswith("#1/"):
            name = data[start:start + int(name[3:])].rstrip(b"\0").decode("utf-8", "replace")
        if not name.startswith("__.SYMDEF"):
            members.append((name, data[pos:start + size]))
        pos = start + size + (size & 1)
    return members


def without_members(archive, drop, count, directory):
    """A copy of the archive without the members at the given positions (in
    nm's order, which is the archive's); None when the archive cannot be
    read as nm listed it.  Positions, not names: a name can be there twice
    (two fs.c)."""
    members = archive_members(archive)
    if members is None or len(members) != count:
        return None
    copy = os.path.join(directory, "%d_%s" % (len(os.listdir(directory)), os.path.basename(archive)))
    with open(copy, "wb") as out:
        out.write(b"!<arch>\n")
        for i, (_, blob) in enumerate(members):
            if i in drop:
                continue
            out.write(blob)
            if len(blob) & 1:
                out.write(b"\n")
    subprocess.check_call(["ranlib", copy], stderr=subprocess.DEVNULL)
    return copy


def expand_response(args):
    result = []
    for arg in args:
        if arg.startswith("@"):
            with open(arg[1:], encoding="utf-8") as stream:
                result.extend(expand_response(shlex.split(stream.read())))
        else:
            result.append(arg)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("missing linker command")
    inputs = [p for p in expand_response(command[1:])
              if p.endswith((".o", ".a")) and os.path.isfile(p)]
    weak = set()
    if inputs:
        output = subprocess.check_output([args.nm, "-m", *inputs], text=True)
        weak, _ = weak_symbols(output)
    flags = ["-Wl,-U," + symbol for symbol in sorted(weak)]
    scratch = None
    objects = [p for p in inputs if p.endswith(".o")]
    archives = []
    for p in inputs:
        if p.endswith(".a") and p not in archives:
            archives.append(p)
    if weak and archives:
        drop = weak_only_members(args.nm, objects, archives)
        if drop:
            scratch = tempfile.mkdtemp(prefix="macos_link_")
            swap = {}
            for archive, (count, members) in drop.items():
                copy = without_members(archive, {i for i, _, _ in members}, count, scratch)
                if copy is None:
                    print("macos_link: %s kept whole (not readable as listed)" % archive,
                          file=sys.stderr)
                    continue
                swap[archive] = copy
                if os.environ.get("EIDOS_MACOS_LINK_VERBOSE"):
                    for _, member, answers in members:
                        print("macos_link: left out %s(%s): weak references only (%s)"
                              % (os.path.basename(archive), member, ", ".join(answers)),
                              file=sys.stderr)
            if swap:
                command = [command[0]] + [swap.get(a, a) for a in expand_response(command[1:])]
    try:
        result = subprocess.call(command + flags)
    finally:
        if scratch is not None:
            shutil.rmtree(scratch, ignore_errors=True)
    if result or not weak:
        return result
    # An archive can contain strong references in members the linker never
    # extracts. Check the finished image so only references actually linked
    # into this executable decide whether a provider is required. Imports
    # satisfied by a dylib are not dynamically looked up.
    expanded = expand_response(command[1:])
    executable = expanded[expanded.index("-o") + 1] if "-o" in expanded else "a.out"
    output = subprocess.check_output([args.nm, "-m", executable], text=True)
    _, required = weak_symbols("\n".join(
        line for line in output.splitlines() if "(dynamically looked up)" in line))
    missing = weak & required
    if missing:
        os.unlink(executable)
        print("macos_link: missing required providers: " + ", ".join(sorted(missing)),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
