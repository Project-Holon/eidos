#!/usr/bin/env python3
"""
tools/lp64/sigfix.py -- adapters for the matched code's disagreeing prototypes,
so a wasm32 link calls the definition instead of a trap.

Why this exists: aletheia's matched files each declare the externs they call
with the prototype their call site needed, and the same function is declared
differently in different files (docs/pointer-width.md, signal A2; a void
function declared as returning int, a call with one argument more than the
definition takes).  On the DS, and on i686, that is harmless: arguments are
in registers or caller-cleaned on the stack and an unused return register is
never read.  A wasm call is type-checked: wasm-ld replaces every direct call
whose wasm signature differs from the definition's with a stub that traps,
and warns "function signature mismatch".

The port may not edit the matched files (docs/building.md, portability rule 1), so
the fix lives in the build, the same way `-Dg_02146B64=sRootList` already
does: for each calling translation unit whose declaration disagrees, compile
it with `-D<func>=<func>__as_<sig>` and link a generated adapter

    u32 func_02017FB0__as_i_i(u32 a0) { func_02017FB0(a0); return 0; }

with the caller's wasm signature, which calls the definition with the
definition's: extra arguments are dropped, missing ones are passed as 0, a
missing return value is 0, an unused one is discarded.  wasm-ld names both
sides of each mismatch; this module turns its warnings into those defines
and that C file.  It is run in a loop (link, fix, relink) because wasm-ld
reports one pair of disagreeing objects per symbol at a time.

Library use: tools/lp64/bench/build.py.  Command line:

    python tools/lp64/sigfix.py --log <wasm-ld output> --nm <llvm-nm> \
        --map <json: object name in the log -> object path> --out adapters.c
"""

import argparse
import json
import re
import subprocess
import sys

C_TYPE = {"i32": "unsigned int", "i64": "unsigned long long",
          "f32": "float", "f64": "double"}


def parse_sig(text):
    params, ret = text.split(" -> ")
    params = params.strip()[1:-1].strip()
    plist = [p.strip() for p in params.split(",")] if params else []
    return tuple(plist), ret.strip()


def parse_log(log):
    """[(symbol, [(sig, object), (sig, object)])] from wasm-ld's warnings."""
    out = []
    lines = log.splitlines()
    for i, line in enumerate(lines):
        m = re.search(r"function signature mismatch: (\S+)", line)
        if not m or i + 2 >= len(lines):
            continue
        sides = []
        for l2 in lines[i + 1:i + 3]:
            m2 = re.search(r"defined as (\(.*?\) -> \S+) in (.*)$", l2)
            if m2:
                sides.append((parse_sig(m2.group(1)), m2.group(2).strip()))
        if len(sides) == 2:
            out.append((m.group(1), sides))
    return out


def defined_symbols(nm, obj):
    p = subprocess.run([nm, "--defined-only", "--format=just-symbols", obj],
                       capture_output=True, text=True)
    return set(p.stdout.split())


def sig_tag(sig):
    params, ret = sig
    p = "_".join(x[0] + x[1:] for x in params) or "v"
    r = ret[0] + ret[1:] if ret != "void" else "v"
    return "%s__%s" % (p, r)


def adapter(sym, caller, callee):
    """C text of `sym`__as_<caller sig>, calling `sym` with `callee`'s."""
    cp, cr = caller
    dp, dr = callee
    name = "%s__as_%s" % (sym, sig_tag(caller))
    proto_args = ", ".join("%s a%d" % (C_TYPE[t], k) for k, t in enumerate(cp)) or "void"
    decl_args = ", ".join(C_TYPE[t] for t in dp) or "void"
    call_args = []
    for k, t in enumerate(dp):
        if k < len(cp) and cp[k] == t:
            call_args.append("a%d" % k)
        elif k < len(cp):
            call_args.append("(%s)a%d" % (C_TYPE[t], k))
        else:
            call_args.append("0")
    ret_c = C_TYPE.get(cr, "void")
    callee_ret = C_TYPE.get(dr, "void")
    call = "%s(%s)" % (sym, ", ".join(call_args))
    if cr == "void":
        body = "    %s%s;\n" % ("(void)" if dr != "void" else "", call)
    elif dr == "void":
        body = "    %s;\n    return 0;\n" % call
    else:
        body = "    return (%s)%s;\n" % (ret_c, call)
    return name, ("extern %s %s(%s);\n%s %s(%s)\n{\n%s}\n"
                  % (callee_ret, sym, decl_args, ret_c, name, proto_args, body))


def plan(log, nm, objmap):
    """({caller object path: {sym: adapter name}}, {adapter name: C text}).

    objmap maps the object names wasm-ld prints to object paths."""
    defines = {}
    adapters = {}
    for sym, sides in parse_log(log):
        defs = []
        paths = []
        for sig, obj in sides:
            if re.search(r"\.a\([^)]+\)$", obj):
                # Archive member: defines sym (that is why wasm-ld extracted it),
                # but cannot be recompiled with -D defines.
                defs.append(True)
                paths.append(None)
            else:
                p = objmap.get(obj)
                paths.append(p)
                defs.append(sym in defined_symbols(nm, p) if p else False)
        if defs.count(True) != 1:
            continue                    # not exactly one defined: cannot adapt
        callee_idx = defs.index(True)
        caller_idx = defs.index(False)
        caller_obj = paths[caller_idx]
        if not caller_obj:
            continue                    # caller not in objmap: cannot recompile
        callee_sig = sides[callee_idx][0]
        caller_sig = sides[caller_idx][0]
        name, text = adapter(sym, caller_sig, callee_sig)
        adapters[name] = text
        defines.setdefault(caller_obj, {})[sym] = name
    return defines, adapters


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    defines, adapters = plan(open(a.log).read(), a.nm, json.load(open(a.map)))
    with open(a.out, "w") as f:
        f.write("/* generated by tools/lp64/sigfix.py */\n\n")
        for name in sorted(adapters):
            f.write(adapters[name] + "\n")
    for obj, d in sorted(defines.items()):
        print(obj, " ".join("-D%s=%s" % kv for kv in sorted(d.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
