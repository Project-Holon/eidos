#!/usr/bin/env python3
"""
tools/fetch_shadertools.py -- the shader compilers the SDL3 GPU backend's
shaders are built with (docs/graphics-backends.md), into a cache outside the
repository.  Only a shader change needs them: the compiled shaders are
committed (engine/platform/shaders/), so a normal build, CI and the phone
builds never run these.

Two pieces, each pinned and verified against a SHA-256 recorded below:

  DXC 1.9.2609         Microsoft's HLSL compiler, the release build (it
                       writes DXIL and SPIR-V; the Windows SDK's copy has no
                       SPIR-V): the shaders' HLSL to DXIL (Direct3D 12) and
                       SPIR-V (Vulkan)
  SPIRV-Cross          vulkan-sdk-1.4.363.0, built here from its source with
                       the host's C++ compiler and CMake: SPIR-V to MSL (Metal)

DXC's prebuilt releases exist for Windows x86_64 and Linux x86_64.

Where things go.  --cache, else EIDOS_SHADERTOOLS_CACHE, else the per-user
cache (Windows %LOCALAPPDATA%\\eidos\\shadertools, macOS
~/Library/Caches/eidos/shadertools, elsewhere ~/.cache/eidos/shadertools).

Usage:

    python tools/fetch_shadertools.py            # fetch, unpack, build
    python tools/fetch_shadertools.py --locate   # print DXC= and SPIRV_CROSS=

Standard library only.
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

DXC_TAG = "v1.9.2609"
DXC_PKGS = {
    "windows-x86_64": ("dxc_2026_09_29.zip",
                       "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"),
    "linux-x86_64": ("linux_dxc_2026_09_28.x86_x64.tar.gz",
                     "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1"),
}
SPIRV_CROSS_TAG = "vulkan-sdk-1.4.363.0"
SPIRV_CROSS_SHA = "f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721"


def host():
    m = platform.machine().lower()
    arch = "x86_64" if m in ("amd64", "x86_64") else "arm64" if m in ("arm64", "aarch64") else m
    osn = "windows" if sys.platform == "win32" else "macos" if sys.platform == "darwin" else "linux"
    return "%s-%s" % (osn, arch)


def cache_dir(arg):
    if arg:
        return arg
    if os.environ.get("EIDOS_SHADERTOOLS_CACHE"):
        return os.environ["EIDOS_SHADERTOOLS_CACHE"]
    if sys.platform == "win32":
        return os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "eidos",
                            "shadertools")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Caches/eidos/shadertools")
    return os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "eidos",
                        "shadertools")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(url, path, want):
    if not os.path.exists(path) or sha256(path) != want:
        print("fetch_shadertools: downloading %s" % url, flush=True)
        tmp = path + ".part"
        with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
        got = sha256(tmp)
        if got != want:
            os.remove(tmp)
            sys.exit("fetch_shadertools: %s has SHA-256 %s, not the recorded %s" % (url, got, want))
        os.replace(tmp, path)


def paths(cache):
    exe = ".exe" if sys.platform == "win32" else ""
    dxc = os.path.join(cache, "dxc", "bin", "x64" if sys.platform == "win32" else "", "dxc" + exe)
    dxc = os.path.normpath(dxc)
    cross = os.path.join(cache, "spirv-cross-build", "spirv-cross" + exe)
    return dxc, cross


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache")
    ap.add_argument("--locate", action="store_true")
    args = ap.parse_args()
    cache = cache_dir(args.cache)
    dxc, cross = paths(cache)
    if args.locate:
        print("DXC=%s" % dxc)
        print("SPIRV_CROSS=%s" % cross)
        return 0 if os.path.exists(dxc) and os.path.exists(cross) else 1
    os.makedirs(cache, exist_ok=True)

    h = host()
    if h not in DXC_PKGS:
        sys.exit("fetch_shadertools: no DXC release for %s (Windows or Linux x86_64)" % h)
    name, want = DXC_PKGS[h]
    arc = os.path.join(cache, name)
    fetch("https://github.com/microsoft/DirectXShaderCompiler/releases/download/%s/%s"
          % (DXC_TAG, name), arc, want)
    if not os.path.exists(dxc):
        out = os.path.join(cache, "dxc")
        shutil.rmtree(out, ignore_errors=True)
        if name.endswith(".zip"):
            zipfile.ZipFile(arc).extractall(out)
        else:
            with tarfile.open(arc) as t:
                t.extractall(out)
        if not sys.platform == "win32" and os.path.exists(dxc):
            os.chmod(dxc, 0o755)

    src_arc = os.path.join(cache, "spirv-cross-%s.tar.gz" % SPIRV_CROSS_TAG)
    fetch("https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/%s.tar.gz"
          % SPIRV_CROSS_TAG, src_arc, SPIRV_CROSS_SHA)
    if not os.path.exists(cross):
        src = os.path.join(cache, "spirv-cross-src")
        shutil.rmtree(src, ignore_errors=True)
        with tarfile.open(src_arc) as t:
            t.extractall(src)
        top = os.path.join(src, os.listdir(src)[0])
        build = os.path.dirname(cross)
        cmake = os.environ.get("CMAKE", "cmake")
        subprocess.check_call([cmake, "-S", top, "-B", build, "-G", "Ninja",
                               "-DCMAKE_BUILD_TYPE=Release",
                               "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
                               "-DSPIRV_CROSS_SHARED=OFF",
                               "-DSPIRV_CROSS_STATIC=ON",
                               "-DSPIRV_CROSS_CLI=ON"])
        subprocess.check_call([cmake, "--build", build, "--target", "spirv-cross"])
    for p in (dxc, cross):
        if not os.path.exists(p):
            sys.exit("fetch_shadertools: %s is missing" % p)
    print("DXC=%s" % dxc)
    print("SPIRV_CROSS=%s" % cross)
    return 0


if __name__ == "__main__":
    sys.exit(main())
