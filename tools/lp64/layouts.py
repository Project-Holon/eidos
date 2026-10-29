#!/usr/bin/env python3
"""
tools/lp64/layouts.py -- compare the struct layouts the matched sources get
on several targets against the DS's own (ARM EABI).

Why this exists: docs/lp64.md has to say which host configurations give the
matched code the DS's struct layouts, because a struct image (save data, a
table in the ARM9 data, a field reached through `u8 pad[0x14]`) is only right
where every field lands where the DS put it.  "ILP32" is not one answer:
i686 Linux aligns a 64-bit integer or a double to 4 bytes inside a struct,
the ARM EABI aligns it to 8.  This script measures it instead of arguing it.

For every matched translation unit in an ILP32 build's compile_commands.json
it runs clang once per target with -fdump-record-layouts-simple (the layout
of every record the unit's code generation needed: sizes, alignments, field
offsets, bit-field placement) and compares each record with the
armv5te-none-eabi layout of the same record in the same unit.

    python tools/lp64/layouts.py --build <ilp32-build> --clang <clang>

It writes nothing but its report.  -ffreestanding and -fno-short-enums on
every target: the DS build's enums are int-sized (CodeWarrior "-enum int").
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

REFERENCE = "armv5te-none-eabi"
TARGETS = [REFERENCE, "wasm32-unknown-unknown", "i686-w64-windows-gnu",
           "i686-linux-gnu", "i686-linux-gnu -malign-double", "x86_64-w64-windows-gnu", "x86_64-linux-gnu",
           "aarch64-linux-gnu"]


def flags_of(entry):
    args = entry.get("arguments") or shlex.split(entry["command"].replace("\\", "/"), posix=True)
    out = []
    i = 1
    while i < len(args):
        a = args[i]
        if a in ("-o", "-c"):
            i += 2 if a == "-o" else 1
            continue
        if a.startswith(("-D", "-I", "-include", "-std", "-fsigned-char")):
            out.append(a)
            if a in ("-include", "-I", "-D"):
                out.append(args[i + 1])
                i += 1
        i += 1
    return out


def parse(text):
    """{record name: (size, align, offsets, bitfields)} from the dump."""
    recs = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r"^Type: (.*)$", line)
        if m:
            cur = {"name": m.group(1).strip()}
            continue
        if cur is None:
            continue
        m = re.match(r"^\s+(Size|Alignment|DataSize):(\d+)", line)
        if m:
            cur[m.group(1)] = int(m.group(2))
            continue
        m = re.match(r"^\s+FieldOffsets: \[(.*)\]", line)
        if m:
            cur["offsets"] = m.group(1)
            continue
        m = re.match(r"^\s+BitFields:\[(.*)", line)
        if m:
            cur["bitfields"] = m.group(1)
            name = re.sub(r" at [^)]*\)", ")", cur["name"])
            recs[name] = (cur.get("Size"), cur.get("Alignment"),
                          cur.get("offsets", ""), cur.get("bitfields", ""))
            cur = None
    return recs


def dump(job):
    clang, target, flags, src, tmp, cwd = job
    triple, _, extra = target.partition(" ")
    cmd = [clang, "--target=" + triple] + extra.split() + ["-ffreestanding", "-fno-short-enums",
           "-w", "-S", "-emit-llvm", "-o", tmp, "-Xclang",
           "-fdump-record-layouts-simple"] + flags + [src]
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if p.returncode != 0:
        errors = [line for line in p.stderr.splitlines() if "error:" in line]
        return None, "\n".join(errors[:3]) or p.stderr[-1000:]
    return parse(p.stdout), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--clang", required=True)
    ap.add_argument("--aletheia-root", required=True,
                    help="a unit is matched code if it is under <root>/src/ or is a "
                         "tools/dsaddr.py copy in the build tree")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--json")
    ap.add_argument("--details-json", help="per-unit differences and compiler failures")
    ap.add_argument("--compdb", help="compilation database, including Ninja -t compdb output")
    ap.add_argument("--target", action="append", help="target triple to compare (repeatable)")
    ap.add_argument("--filter", default="", help="substring of the translation-unit path")
    ap.add_argument("--limit", type=int, help="limit the number of units for a smoke check")
    a = ap.parse_args()

    targets = [REFERENCE] + (a.target or TARGETS[1:])
    entries = json.load(open(a.compdb or os.path.join(a.build, "compile_commands.json")))
    seen = set()
    units = []
    for e in entries:
        f = e["file"].replace("\\", "/")
        root = a.aletheia_root.replace("\\", "/").rstrip("/") + "/src/"
        game = (f.startswith(root) or "/dsaddr/" in f
                or "/boot/unit/src_game_" in f or "/src/src/game/" in f)
        if f in seen or not f.endswith(".c") or not game or a.filter not in f:
            continue
        seen.add(f)
        units.append((f, flags_of(e), e.get("directory", a.build)))
    if a.limit:
        units = units[:a.limit]
    if not units:
        ap.error("no game C translation units matched the compilation database")

    with tempfile.TemporaryDirectory(prefix="eidos_layouts_") as tmpdir:
        jobs = []
        for n, (f, fl, cwd) in enumerate(units):
            for k, t in enumerate(targets):
                jobs.append((a.clang, t, fl, f, os.path.join(tmpdir, "%d_%d.ll" % (n, k)), cwd))
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            res = list(ex.map(dump, jobs))

    per_target = {t: {"units": 0, "records": 0, "differ": 0, "units_differ": 0,
                      "size_differ": 0} for t in targets[1:]}
    examples = {t: [] for t in targets[1:]}
    distinct = {t: set() for t in targets[1:]}
    details, failures = [], []
    compared_units = 0
    for n, (f, _, _) in enumerate(units):
        r = {t: res[n * len(targets) + k][0] for k, t in enumerate(targets)}
        for k, t in enumerate(targets):
            error = res[n * len(targets) + k][1]
            if error:
                failures.append({"file": f, "target": t, "error": error})
        ref = r[REFERENCE]
        if ref is None:
            continue
        compared_units += 1
        for t in targets[1:]:
            if r[t] is None:
                continue
            pt = per_target[t]
            pt["units"] += 1
            unit_bad = False
            for name, lay in ref.items():
                other = r[t].get(name)
                if other is None:
                    continue
                pt["records"] += 1
                if other != lay:
                    details.append({"file": f, "record": name, "target": t,
                                    "arm_bits": lay, "host_bits": other})
                    pt["differ"] += 1
                    distinct[t].add(name)
                    unit_bad = True
                    if other[0] != lay[0]:
                        pt["size_differ"] += 1
                    if len(examples[t]) < 6:
                        examples[t].append((os.path.basename(f), name, lay[:3], other[:3]))
            if unit_bad:
                pt["units_differ"] += 1

    print("game units with an ARM reference layout: %d of %d" % (compared_units, len(units)))
    print("compiler failures (reported, not treated as passing): %d" % len(failures))
    print("reference: %s, -fno-short-enums, -ffreestanding\n" % REFERENCE)
    print("%-32s %8s %10s %12s %10s %13s" % ("target", "units", "records", "differ", "size", "units hit"))
    for t in targets[1:]:
        pt = per_target[t]
        print("%-32s %8d %10d %12d %10d %13d   (%d distinct record types)" % (
            t, pt["units"], pt["records"], pt["differ"], pt["size_differ"],
            pt["units_differ"], len(distinct[t])))
    for t in targets[1:]:
        if examples[t]:
            print("\n%s, first differences (unit, record, ARM size/align/offsets, target's):" % t)
            for ex_ in examples[t][:4]:
                print("  %s  %s\n     arm %s\n     %s %s" % (ex_[0], ex_[1], ex_[2], t.split("-")[0], ex_[3]))
    if a.json:
        json.dump({t: sorted(distinct[t]) for t in distinct}, open(a.json, "w"), indent=1)
    if a.details_json:
        with open(a.details_json, "w") as output:
            json.dump({"reference": REFERENCE, "units": len(units), "summary": per_target,
                       "differences": details, "failures": failures}, output, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
