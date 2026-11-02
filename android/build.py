#!/usr/bin/env python3
"""android/build.py -- eidos for Android: libmain.so and the APK.

The game is the same wasm32 module as the desktop's (tools/build64.py builds
it); host/ is compiled with the NDK into libmain.so, SDL3 from its source
release into libSDL3.so, and SDL3's android-project (this folder) packs them
into an APK whose SDLActivity loads both (android/README.md).

    python3 android/build.py --module <build64>/module/eidos_boot.wasm
                             [--build-dir build-android] [--release]

Needs the Android SDK (ANDROID_HOME, or Android Studio's default place) with
an NDK, and a JDK 17 or later (JAVA_HOME, or Android Studio's own).
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ABI = "arm64-v8a"


def run(args, **kw):
    print("+ " + " ".join(args), flush=True)
    subprocess.check_call(args, **kw)


def last_line(script, *args):
    out = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", script)] + list(args),
                                  text=True)
    return out.strip().splitlines()[-1]


def wasm_tools():
    out = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "fetch_wasm.py")], text=True)
    return dict(l.split("=", 1) for l in out.splitlines() if "=" in l)


def android_sdk():
    for d in (os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"),
              os.path.expanduser("~/Library/Android/sdk"), os.path.expanduser("~/Android/Sdk")):
        if d and os.path.isdir(d):
            return d
    sys.exit("android/build.py: no Android SDK (set ANDROID_HOME)")


def ndk(sdk, wanted):
    if wanted:
        return wanted
    found = sorted(glob.glob(os.path.join(sdk, "ndk", "*")))
    if not found:
        sys.exit("android/build.py: no NDK in %s/ndk (sdkmanager \"ndk;27.3.13750724\")" % sdk)
    return found[-1]


def java_home():
    for d in (os.environ.get("JAVA_HOME"),
              "/Applications/Android Studio.app/Contents/jbr/Contents/Home"):
        if d and os.path.isfile(os.path.join(d, "bin", "java")):
            return d
    sys.exit("android/build.py: no JDK (set JAVA_HOME)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--module", required=True, help="eidos_boot.wasm (tools/build64.py's module/)")
    ap.add_argument("--build-dir", default="build-android")
    ap.add_argument("--ndk", help="the NDK (default: the newest under the SDK)")
    ap.add_argument("--platform", default="android-26", help="the oldest Android it runs on")
    ap.add_argument("--release", action="store_true", help="assembleRelease (unsigned)")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()

    sdk = android_sdk()
    nd = ndk(sdk, a.ndk)
    # "SDL3 <version> source ready in <folder>"
    sdl = last_line("fetch_sdl.py", "--flavor", "source").split(" ready in ", 1)[-1]
    t = wasm_tools()
    build = os.path.abspath(a.build_dir)
    gen = ["-G", "Ninja"] if shutil.which("ninja") else []
    env = dict(os.environ, CMAKE_POLICY_VERSION_MINIMUM="3.5")

    run(["cmake", "-S", os.path.join(ROOT, "host"), "-B", build] + gen + [
        "-DCMAKE_TOOLCHAIN_FILE=%s/build/cmake/android.toolchain.cmake" % nd,
        "-DANDROID_ABI=" + ABI, "-DANDROID_PLATFORM=" + a.platform,
        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS=-O2",
        "-DEIDOS_WASM_MODULE=" + os.path.abspath(a.module),
        "-DWABT_ROOT=" + t["WABT_ROOT"], "-DWASM2C=" + t["WASM2C"], "-DWASM_OPT=" + t["WASM_OPT"],
        "-DEIDOS_HOST_SDL_SOURCE=" + sdl], env=env)
    run(["cmake", "--build", build, "--parallel", str(a.jobs), "--target", "eidos", "SDL3-shared"], env=env)

    libs = os.path.join(build, "jniLibs", ABI)
    os.makedirs(libs, exist_ok=True)
    shutil.copy2(os.path.join(build, "libmain.so"), libs)
    shutil.copy2(os.path.join(build, "sdl3", "libSDL3.so"), libs)

    genv = dict(os.environ, ANDROID_HOME=sdk, JAVA_HOME=java_home())
    here = os.path.dirname(os.path.abspath(__file__))
    run([os.path.join(here, "gradlew"), "--no-daemon",
         "assembleRelease" if a.release else "assembleDebug",
         "-Peidos.sdl=" + sdl, "-Peidos.jniLibs=" + os.path.join(build, "jniLibs")],
        cwd=here, env=genv)
    kind = "release" if a.release else "debug"
    print("android/build.py: %s" % os.path.join(here, "app", "build", "outputs", "apk", kind))
    return 0


if __name__ == "__main__":
    sys.exit(main())
