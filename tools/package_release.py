#!/usr/bin/env python3
"""
tools/package_release.py -- turn a build directory into the release package
of one platform.  CI (.github/workflows/release.yml) and a local run use the
same steps; docs/release.md says how.

  python tools/package_release.py windows --build-dir B --version 0.1.0 --out O
  python tools/package_release.py linux   --build-dir B --version 0.1.0 --out O
  python tools/package_release.py macos   --build-dir B --version 0.1.0 --out O
  python tools/package_release.py placeholders --aletheia-root A
  python tools/package_release.py --self-test

What each platform makes (names are fixed; the launcher downloads by them):

  windows  eidos-standalone-<version>-windows.zip
           eidos.exe (the build's eidos_boot.exe), SDL3.dll, LICENSE,
           LEGAL.md, README.txt.  The files sit at the top of the zip.
  macos    eidos-standalone-<version>-macos.zip
           eidos (the build's eidos_boot), libSDL3 beside it with the load
           path rewritten to @executable_path, LICENSE, LEGAL.md,
           README.txt.  Unsigned.
  linux    eidos-standalone-<version>-linux-x86_64.AppImage
           An AppDir (eidos, desktop file, icon, AppRun) turned into an
           AppImage by linuxdeploy (--linuxdeploy PATH), which also bundles
           the shared libraries.  Without --linuxdeploy the AppDir is left in
           the output folder and the script says so.

placeholders: the src/game tree includes a few generated files (aletheia's
build/gen/*.inc, tools/gendata.py) that hold bytes of the player's own ROM.
A release build runs on a machine with no ROM, so this writes the missing
ones with stand-ins: the SHA-1 round constants (public values) and zeros for
the rest (ARM9 start-up microcode and the DSi key block, which the port never
runs, and the 24 vertex halfwords of the trade screen's cube, which draws as
nothing).  A file that exists is never replaced.  docs/release.md lists this
as an open decision.

No game data goes into a package.  Every file is checked against a
deny-list (ROM images, extracted archives, anything under a data root)
before it is added, and the build directory's own data folder is never
read.  Standard library only.
"""

import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The game executable is the build target eidos_boot, or in the 64-bit
# build (tools/build64.py) the host program, eidos.  The package name is
# eidos.
TARGET = "eidos_boot"

# Names and extensions no package may hold.  A ROM, a save, an extracted
# NitroFS member or an overlay is the player's own data.
DENY_SUFFIXES = (".nds", ".dsi", ".srl", ".sav", ".dsv", ".bin", ".narc",
                 ".sdat", ".swav", ".nclr", ".ncgr", ".nscr", ".ncer",
                 ".nanr", ".nsbmd", ".nsbtx", ".nsbca", ".nsbta", ".nsbtp",
                 ".nsbma", ".nsbva", ".gz", ".7z", ".rar")
DENY_DIRS = ("extract", "data", "a", "roms", "rom", "saves", "nitrofs",
             "overlay", "overlays", "arm9", "arm7")
DENY_NAMES = ("arm9_dec.bin", "arm9.bin", "arm7.bin", "y9.bin", "banner.bin",
              "header.bin", "fnt.bin", "fat.bin")

# What each package may hold, besides the executable and the libraries.
DOCS = ("LICENSE", "LEGAL.md")

README_TXT = """eidos %(version)s

eidos is an unofficial fan project. It is not affiliated with or endorsed by
Nintendo, Game Freak, Creatures Inc. or The Pokemon Company. It contains no
game data: you need your own legally obtained copy of Pokemon Black or
White. Pokemon and Nintendo DS are trademarks of their respective owners.

To play:
  1. Unzip this folder anywhere.
  2. Run %(run)s.
  3. Pick your own Pokemon Black ROM once. It is checked and extracted.

Saves, settings and the extracted data live in your user folder
(%(userdir)s), never next to the program.

Source and updates: https://github.com/Project-Holon/eidos
"""

MAC_NOTE = """
This build is not signed by Apple, so macOS blocks it the first time and
says it cannot be opened. Open Terminal, go to this folder (type cd and a
space, drag the folder onto the Terminal window, press Return), then run:
  xattr -dr com.apple.quarantine .
This removes the "downloaded from the internet" mark from these files
only. You do not need to turn off Gatekeeper.
"""


class Refused(Exception):
    pass


def check_name(path):
    """Refuse a path that looks like game data."""
    norm = path.replace("\\", "/").strip("/")
    parts = norm.lower().split("/")
    base = parts[-1]
    if base in DENY_NAMES or base.endswith(DENY_SUFFIXES):
        raise Refused("refusing to package %s: it looks like game data" % path)
    for d in parts[:-1]:
        if d in DENY_DIRS:
            raise Refused("refusing to package %s: it is under a data "
                          "folder (%s)" % (path, d))


def check_source(src):
    """Refuse a source file that is a ROM, whatever its name is."""
    with open(src, "rb") as fh:
        head = fh.read(0x200)
    # A DS ROM header: the 12-byte title, the 4-byte game code, and the
    # Nintendo logo checksum 0xCF56 at 0x15C.  A package file never has it.
    if len(head) >= 0x160 and head[0x15C:0x15E] == b"\x56\xcf":
        raise Refused("refusing to package %s: it has a DS ROM header" % src)


class Package:
    """Files to put in a package, each checked before it is accepted."""

    def __init__(self):
        self.items = []     # (arcname, source path, mode)

    def add(self, arcname, src, mode=0o644):
        check_name(arcname)
        if not os.path.isfile(src):
            raise Refused("missing file: %s" % src)
        check_source(src)
        self.items.append((arcname, src, mode))

    def add_text(self, arcname, text, mode=0o644, scratch=None):
        check_name(arcname)
        path = os.path.join(scratch, arcname)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
        self.items.append((arcname, path, mode))

    def write_zip(self, out):
        names = [a for a, _, _ in self.items]
        if len(set(names)) != len(names):
            raise Refused("duplicate file names in the package")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for arc, src, mode in sorted(self.items):
                info = zipfile.ZipInfo(arc, date_time=(2020, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | mode) << 16
                with open(src, "rb") as fh:
                    z.writestr(info, fh.read())


def find_exe(build, names):
    for n in names:
        p = os.path.join(build, n)
        if os.path.isfile(p):
            return p
    raise Refused("%s not found in %s (build the %s target first)"
                  % (names[0], build, TARGET))


def find_lib(build, patterns, extra_dirs):
    """The first existing file among the build dir and extra dirs."""
    for d in [build] + extra_dirs:
        if not d or not os.path.isdir(d):
            continue
        for pat in patterns:
            p = os.path.join(d, pat)
            if os.path.isfile(p):
                return p
    return None


def handler_params(version):
    """sdk/handler_params.json (tools/handler_params.py) with the release's
    version in it: the battle handler keys mods may change, for kenon."""
    with open(os.path.join(ROOT, "sdk", "handler_params.json"), encoding="utf-8") as fh:
        doc = json.load(fh)
    doc["eidos"] = version
    return json.dumps(doc, indent=1) + "\n"


def features(version):
    """sdk/features.json with the release's version in it: the first eidos
    version that reads each table, key and archive a mod may use, for kenon
    (it writes a mod's `eidos` line from it)."""
    with open(os.path.join(ROOT, "sdk", "features.json"), encoding="utf-8") as fh:
        doc = json.load(fh)
    doc["eidos"] = version
    return json.dumps(doc, indent=1) + "\n"


def common(pkg, version, run, userdir, scratch, extra_note=""):
    for d in DOCS:
        pkg.add(d, os.path.join(ROOT, d))
    pkg.add("recomplauncher.json", os.path.join(ROOT, "recomplauncher.json"))
    pkg.add_text("handler_params.json", handler_params(version), scratch=scratch)
    pkg.add_text("features.json", features(version), scratch=scratch)
    pkg.add_text("README.txt", README_TXT % {
        "version": version, "run": run, "userdir": userdir} + extra_note,
        scratch=scratch)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def package_windows(args, scratch):
    pkg = Package()
    pkg.add("eidos.exe", find_exe(args.build_dir, [TARGET + ".exe", "eidos.exe"]), 0o755)
    sdl = find_lib(args.build_dir, ["SDL3.dll"],
                   [os.path.dirname(a) for a in args.lib])
    for extra in args.lib:
        if os.path.isfile(extra):
            pkg.add(os.path.basename(extra), extra)
    if not any(a == "SDL3.dll" for a, _, _ in pkg.items):
        if not sdl:
            raise Refused("SDL3.dll not found in %s (pass --lib PATH)"
                          % args.build_dir)
        pkg.add("SDL3.dll", sdl)
    common(pkg, args.version, "eidos.exe", "%APPDATA%\\eidos", scratch)
    out = os.path.join(args.out, "eidos-standalone-%s-windows.zip" % args.version)
    pkg.write_zip(out)
    return out


def package_macos(args, scratch):
    pkg = Package()
    exe = find_exe(args.build_dir, [TARGET, "eidos"])
    pkg.add("eidos", exe, 0o755)
    # Copy the executable and the SDL3 library to a scratch folder so the
    # load path can be rewritten without touching the build directory.
    work = os.path.join(scratch, "mac")
    os.makedirs(work)
    exe_copy = os.path.join(work, "eidos")
    shutil.copy2(exe, exe_copy)
    pkg.items = [("eidos", exe_copy, 0o755)]
    libs = [a for a in args.lib if os.path.isfile(a)]
    if not libs:
        found = find_lib(args.build_dir, ["libSDL3.0.dylib", "libSDL3.dylib"], [])
        libs = [found] if found else []
    for lib in libs:
        dst = os.path.join(work, os.path.basename(lib))
        shutil.copy2(lib, dst)
        os.chmod(dst, 0o755)
        if sys.platform == "darwin":
            subprocess.check_call(["install_name_tool", "-id",
                                   "@executable_path/" + os.path.basename(lib), dst])
            deps = subprocess.check_output(["otool", "-L", exe_copy], text=True)
            for line in deps.splitlines()[1:]:
                dep = line.strip().split(" ")[0]
                if os.path.basename(dep) == os.path.basename(lib) and \
                        not dep.startswith("@executable_path"):
                    subprocess.check_call(["install_name_tool", "-change", dep,
                                           "@executable_path/" + os.path.basename(lib),
                                           exe_copy])
        pkg.add(os.path.basename(lib), dst, 0o755)
    if sys.platform == "darwin":
        # An unsigned file still has to carry an ad-hoc signature on Apple
        # silicon, and install_name_tool invalidated the old one.
        for _, src, _ in pkg.items:
            if os.path.basename(src) == "eidos" or src.endswith(".dylib"):
                subprocess.call(["codesign", "--force", "--sign", "-", src])
    common(pkg, args.version, "eidos", "~/Library/Application Support/eidos",
           scratch, MAC_NOTE)
    out = os.path.join(args.out, "eidos-standalone-%s-macos.zip" % args.version)
    pkg.write_zip(out)
    return out


DESKTOP = """[Desktop Entry]
Type=Application
Name=eidos
Comment=Pokemon Black native port
Exec=eidos
Icon=eidos
Categories=Game;
Terminal=false
"""

APPRUN = """#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
exec "$HERE/usr/bin/eidos" "$@"
"""


def package_linux(args, scratch):
    exe = find_exe(args.build_dir, [TARGET, "eidos"])
    check_source(exe)
    appdir = os.path.join(args.out, "eidos.AppDir")
    if os.path.isdir(appdir):
        shutil.rmtree(appdir)
    for sub in ("usr/bin", "usr/lib", "usr/share/applications",
                "usr/share/icons/hicolor/256x256/apps"):
        os.makedirs(os.path.join(appdir, sub))
    shutil.copy2(exe, os.path.join(appdir, "usr/bin/eidos"))
    os.chmod(os.path.join(appdir, "usr/bin/eidos"), 0o755)
    icon = os.path.join(ROOT, "assets", "icon-256.png")     # tools/mkicon.py
    check_name("eidos.png")
    shutil.copy2(icon, os.path.join(appdir, "eidos.png"))
    shutil.copy2(icon, os.path.join(appdir, "usr/share/icons/hicolor/256x256/apps/eidos.png"))
    for d in (appdir, os.path.join(appdir, "usr/share/applications")):
        with open(os.path.join(d, "eidos.desktop"), "w", newline="\n") as fh:
            fh.write(DESKTOP)
    with open(os.path.join(appdir, "AppRun"), "w", newline="\n") as fh:
        fh.write(APPRUN)
    os.chmod(os.path.join(appdir, "AppRun"), 0o755)
    shutil.copy2(os.path.join(ROOT, "recomplauncher.json"), appdir)
    with open(os.path.join(appdir, "handler_params.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(handler_params(args.version))
    with open(os.path.join(appdir, "features.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(features(args.version))
    share = os.path.join(appdir, "usr/share/doc/eidos")
    os.makedirs(share)
    for d in DOCS:
        shutil.copy2(os.path.join(ROOT, d), share)
    for lib in args.lib:
        if os.path.isfile(lib):
            shutil.copy2(lib, os.path.join(appdir, "usr/lib"))
    check_tree(appdir)
    out = os.path.join(args.out, "eidos-standalone-%s-linux-x86_64.AppImage" % args.version)
    if not args.linuxdeploy:
        print("package_release: AppDir written to %s; pass --linuxdeploy PATH "
              "to make the AppImage" % appdir)
        return None
    env = dict(os.environ, OUTPUT=out, LDAI_OUTPUT=out,
               APPIMAGE_EXTRACT_AND_RUN="1", NO_STRIP="1",
               LDAI_VERSION=args.version)
    subprocess.check_call([args.linuxdeploy, "--appdir", appdir,
                           "--desktop-file", os.path.join(appdir, "eidos.desktop"),
                           "--icon-file", os.path.join(appdir, "eidos.png"),
                           "--output", "appimage"], env=env, cwd=args.out)
    if not os.path.isfile(out):
        # linuxdeploy's AppImage plugin names the file itself when OUTPUT
        # is ignored.
        made = [f for f in os.listdir(args.out) if f.endswith(".AppImage")]
        if len(made) != 1:
            raise Refused("linuxdeploy did not write one AppImage in %s" % args.out)
        os.replace(os.path.join(args.out, made[0]), out)
    return out


def check_tree(top):
    for dp, _, files in os.walk(top):
        for f in files:
            full = os.path.join(dp, f)
            check_name(os.path.relpath(full, top))
            check_source(full)


SHA1_CONSTANTS = (0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6)


def placeholders(aletheia_root):
    """Write the missing build/gen/*.inc files with ROM-free stand-ins."""
    tools = os.path.join(aletheia_root, "tools")
    sys.path.insert(0, tools)
    try:
        import gendata
    except ImportError as exc:
        raise Refused("cannot import %s/gendata.py: %s" % (tools, exc))
    out_dir = os.path.join(aletheia_root, "build", "gen")
    os.makedirs(out_dir, exist_ok=True)
    for name in sorted(gendata.SPECS):
        path = os.path.join(out_dir, name + ".inc")
        if os.path.exists(path):
            continue
        _addr, size, _region = gendata.SPECS[name]
        if name.startswith("words_sha1"):
            data = b"".join(c.to_bytes(4, "little") for c in SHA1_CONSTANTS)
        else:
            data = bytes(size)
        if len(data) != size:
            raise Refused("%s: expected %d bytes, built %d" % (name, size, len(data)))
        text = gendata.render(data, words=name.startswith(("microcode_", "words_")),
                              halves=name.startswith("h16_"),
                              raw=name.startswith("bytes_"))
        with open(path, "w", newline="\n") as fh:
            fh.write(text)
        print("package_release: wrote stand-in %s" % path)


def self_test():
    """The deny-list refuses what it must and accepts what it must."""
    bad = ["black.nds", "a/0/0/2", "extract/data/x.txt", "data/foo",
           "arm9_dec.bin", "x/y.narc", "saves/slot1.sav"]
    good = ["eidos.exe", "SDL3.dll", "README.txt", "LICENSE", "LEGAL.md",
            "usr/lib/libSDL3.so.0", "eidos.png"]
    for b in bad:
        try:
            check_name(b)
        except Refused:
            continue
        raise SystemExit("self-test: %s was accepted" % b)
    for g in good:
        check_name(g)
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "innocent.dll")
        with open(rom, "wb") as fh:
            fh.write(b"\0" * 0x15C + b"\x56\xcf" + b"\0" * 0x100)
        try:
            check_source(rom)
        except Refused:
            pass
        else:
            raise SystemExit("self-test: a ROM header was accepted")
    print("package_release: self-test ok")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("platform", nargs="?",
                    choices=("windows", "linux", "macos", "placeholders"))
    ap.add_argument("--aletheia-root", help="placeholders: the aletheia checkout")
    ap.add_argument("--build-dir")
    ap.add_argument("--version")
    ap.add_argument("--out", default=".")
    ap.add_argument("--lib", action="append", default=[],
                    help="a shared library to include (repeat); SDL3.dll "
                         "is found in the build directory when not given")
    ap.add_argument("--linuxdeploy", help="path to linuxdeploy (linux)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        self_test()
        return 0
    if args.platform == "placeholders":
        if not args.aletheia_root:
            ap.error("placeholders needs --aletheia-root")
        try:
            placeholders(os.path.abspath(args.aletheia_root))
        except Refused as exc:
            print("package_release: %s" % exc, file=sys.stderr)
            return 1
        return 0
    if not (args.platform and args.build_dir and args.version):
        ap.error("platform, --build-dir and --version are required")
    args.build_dir = os.path.abspath(args.build_dir)
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory() as scratch:
            fn = {"windows": package_windows, "linux": package_linux,
                  "macos": package_macos}[args.platform]
            out = fn(args, scratch)
    except Refused as exc:
        print("package_release: %s" % exc, file=sys.stderr)
        return 1
    if out:
        print("package_release: wrote %s (%d bytes, sha256 %s)"
              % (out, os.path.getsize(out), sha256(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
