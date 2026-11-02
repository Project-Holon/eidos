#!/usr/bin/env python3
"""ios/build.py -- eidos for iOS: the app bundle.

The game is the same wasm32 module as the desktop's (tools/build64.py builds
it); host/ and SDL3 (from its source release) are compiled for iOS into
eidos.app (ios/README.md).

    python3 ios/build.py --module <build64>/module/eidos_boot.wasm
                         [--build-dir build-ios] [--device]

The default is the simulator (arm64); --device builds for iPhones and iPads,
which Xcode then has to sign.
"""

import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(args, **kw):
    print("+ " + " ".join(args), flush=True)
    subprocess.check_call(args, **kw)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--module", required=True, help="eidos_boot.wasm (tools/build64.py's module/)")
    ap.add_argument("--build-dir", default="build-ios")
    ap.add_argument("--device", action="store_true", help="for devices, not the simulator")
    ap.add_argument("--deployment-target", default="15.0")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()

    out = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "fetch_sdl.py"),
                                   "--flavor", "source"], text=True)
    sdl = out.strip().splitlines()[-1]
    out = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "fetch_wasm.py")], text=True)
    t = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    build = os.path.abspath(a.build_dir)
    gen = ["-G", "Ninja"] if shutil.which("ninja") else []
    env = dict(os.environ, CMAKE_POLICY_VERSION_MINIMUM="3.5")

    run(["cmake", "-S", os.path.join(ROOT, "host"), "-B", build] + gen + [
        "-DCMAKE_SYSTEM_NAME=iOS",
        "-DCMAKE_OSX_SYSROOT=" + ("iphoneos" if a.device else "iphonesimulator"),
        "-DCMAKE_OSX_ARCHITECTURES=arm64",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + a.deployment_target,
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS=-O2",
        "-DEIDOS_WASM_MODULE=" + os.path.abspath(a.module),
        "-DWABT_ROOT=" + t["WABT_ROOT"], "-DWASM2C=" + t["WASM2C"], "-DWASM_OPT=" + t["WASM_OPT"],
        "-DEIDOS_HOST_SDL_SOURCE=" + sdl], env=env)
    run(["cmake", "--build", build, "--parallel", str(a.jobs), "--target", "eidos"], env=env)
    print("ios/build.py: %s" % os.path.join(build, "eidos.app"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
