#!/usr/bin/env python3
"""Link Mach-O executables with ELF-style optional weak references.

Apple ld requires -U even for an undefined weak reference with no dylib
provider. Allow only explicitly weak symbols; other undefined references
still fail the link. Inspect objects, not C spellings,
so generated game-name mappings and per-target defines are respected.
"""
import argparse
import os
import shlex
import subprocess
import sys


def weak_symbols(output):
    weak, required = set(), set()
    for line in output.splitlines():
        if "(undefined)" not in line or "external " not in line:
            continue
        symbol = line.split("external ", 1)[1].split()[0]
        (weak if "weak external " in line else required).add(symbol)
    return weak, required


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
    result = subprocess.call(command + flags)
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
