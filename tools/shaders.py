#!/usr/bin/env python3
"""
tools/shaders.py -- compiles the SDL3 GPU backend's shaders
(engine/platform/shaders/*.hlsl) into engine/platform/shaders/shaders_gen.h,
the committed file the build uses (docs/graphics-backends.md).

Each .hlsl holds a vertex shader `vs_main` and a fragment shader `fs_main`
(either may be missing).  Each is compiled three ways:

  DXIL    DXC, shader model 6.0          (Direct3D 12)
  SPIR-V  DXC -spirv, Vulkan 1.0         (Vulkan)
  MSL     SPIRV-Cross from the SPIR-V    (Metal, MSL 2.0)

and written as byte arrays named <file>_<stage>_<format> (stage vs or fs),
with each stage's uniform-buffer, sampler and storage counts, which SDL's
SDL_GPUShaderCreateInfo asks for.

The compilers come from tools/fetch_shadertools.py (run it first).

    python tools/shaders.py           # write shaders_gen.h
    python tools/shaders.py --check   # fail if shaders_gen.h is out of date

Standard library only.
"""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "engine", "platform", "shaders")
OUT = os.path.join(SRC, "shaders_gen.h")


def tools():
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "fetch_shadertools.py"),
                        "--locate"], capture_output=True, text=True)
    found = dict(l.split("=", 1) for l in r.stdout.splitlines() if "=" in l)
    if r.returncode != 0:
        sys.exit("shaders: the compilers are missing; run tools/fetch_shadertools.py")
    return found["DXC"], found["SPIRV_CROSS"]


def counts(hlsl, stage):
    """The resources one stage uses, as SDL counts them: the space a stage's
    resources sit in (vertex: textures space0, uniforms space1; fragment:
    textures space2, uniforms space3) tells them apart."""
    tex_space, ub_space = ("space0", "space1") if stage == "vs" else ("space2", "space3")
    samplers = len(re.findall(r"register\(\s*t\d+\s*,\s*%s\s*\)" % tex_space, hlsl))
    storage = len(re.findall(r"register\(\s*u\d+\s*,\s*%s\s*\)" % tex_space, hlsl))
    ubufs = len(re.findall(r"register\(\s*b\d+\s*,\s*%s\s*\)" % ub_space, hlsl))
    return samplers, storage, ubufs


def c_bytes(name, data):
    lines = ["static const unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def compile_all():
    dxc, cross = tools()
    parts = ["/* engine/platform/shaders/shaders_gen.h -- written by tools/shaders.py from",
             " * the .hlsl files next to it: do not edit. */",
             "#ifndef EIDOS_SHADERS_GEN_H", "#define EIDOS_SHADERS_GEN_H", ""]
    tmp = tempfile.mkdtemp(prefix="eidos_shaders_")
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".hlsl"):
            continue
        base = fn[:-5]
        path = os.path.join(SRC, fn)
        with open(path, encoding="utf-8") as f:
            hlsl = f.read()
        for stage, entry, profile in (("vs", "vs_main", "vs_6_0"), ("fs", "fs_main", "ps_6_0")):
            if re.search(r"\b%s\s*\(" % entry, hlsl) is None:
                continue
            name = "%s_%s" % (base, stage)
            dxil = os.path.join(tmp, name + ".dxil")
            spv = os.path.join(tmp, name + ".spv")
            msl = os.path.join(tmp, name + ".msl")
            for args in (["-T", profile, "-E", entry, "-Fo", dxil, path],
                         ["-spirv", "-fspv-target-env=vulkan1.0", "-T", profile, "-E", entry,
                          "-Fo", spv, path]):
                r = subprocess.run([dxc, "-nologo", "-Wno-ignored-attributes"] + args,
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit("shaders: %s (%s):\n%s" % (fn, entry, r.stderr))
            r = subprocess.run([cross, "--msl", "--msl-version", "20000",
                                "--msl-decoration-binding", spv, "--output", msl],
                               capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit("shaders: %s (%s) to MSL:\n%s" % (fn, entry, r.stderr))
            samplers, storage, ubufs = counts(hlsl, stage)
            parts.append("/* %s, %s */" % (fn, entry))
            for fmt, p in (("dxil", dxil), ("spirv", spv)):
                with open(p, "rb") as f:
                    parts.append(c_bytes("%s_%s" % (name, fmt), f.read()))
            with open(msl, "rb") as f:
                parts.append(c_bytes("%s_msl" % name, f.read() + b"\0"))
            parts.append("#define %s_SAMPLERS %d" % (name.upper(), samplers))
            parts.append("#define %s_STORAGE %d" % (name.upper(), storage))
            parts.append("#define %s_UNIFORMS %d" % (name.upper(), ubufs))
            parts.append("#define %s_ENTRY \"%s\"" % (name.upper(), entry))
            parts.append("")
    parts.append("#endif")
    return "\n".join(parts) + "\n"


def main():
    text = compile_all()
    if "--check" in sys.argv[1:]:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if old != text:
            print("shaders: out of date (run tools/shaders.py): %s" % OUT)
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("shaders: wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
