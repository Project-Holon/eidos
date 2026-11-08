#!/usr/bin/env python3
"""The notes of a GitHub release: the version's changelog, what to download,
and the notice.

    python tools/release_notes.py VERSION [--assets DIR] [--test-build TAG] > notes.md

VERSION is the release's version (0.1.5).  The changelog section is the one
headed "## VERSION" in CHANGELOG.md.  --assets names the folder of the
release's files: a platform's download line is only written when its file is
there (no macOS line without a macOS build).  --test-build marks the notes
as a test build's (a pre-release, tagged TAG).
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (file name, {ver} for the version; what it is): the order of the table.
# The launchers and the phone apps have no version in their names (the
# README links .../releases/latest/download/<name>); the desktop games keep
# it, as installed launchers look for eidos-*-<platform>
# (launcher/src-tauri/src/versions.rs).
DOWNLOADS = [
    ("eidos-launcher-windows-setup.exe",
     "Windows: the launcher. It installs and updates eidos and keeps your "
     "profiles and mods. Start here."),
    ("eidos-launcher-macos.dmg",
     "macOS (Apple silicon): the launcher. Not signed by Apple: open "
     "`READ ME FIRST.txt` inside it for the one Terminal line it needs."),
    ("eidos-launcher-linux-x86_64.AppImage",
     "Linux: the launcher. Make it executable and run it."),
    ("eidos-android.apk",
     "Android (64-bit ARM, Android 8 or later): install it from the file "
     "(allow installs from this source when Android asks)."),
    ("eidos-ios.ipa",
     "iPhone and iPad (iOS 15 or later): not signed. Install it with a "
     "sideloading tool (AltStore, Sideloadly) and your own Apple ID."),
]

# The desktop games on their own, without the launcher: folded away under
# the main table, as most players want the launcher.
STANDALONE = [
    ("eidos-{ver}-windows.zip",
     "Windows: eidos on its own. Unzip it anywhere and run `eidos.exe`."),
    ("eidos-{ver}-macos.zip",
     "macOS (Apple silicon): eidos on its own. Not signed by Apple: "
     "`README.txt` inside it has the one Terminal line it needs."),
    ("eidos-{ver}-linux-x86_64.AppImage",
     "Linux: eidos on its own. Make it executable and run it."),
    ("eidos-{ver}-switch.zip",
     "Nintendo Switch (homebrew, custom firmware): unzip it to the SD card's "
     "root, put your extracted game in `switch/eidos/extract/`, start it "
     "from the Homebrew Menu over a game (hold R) for the full memory. "
     "Early: tested in an emulator only."),
    ("eidos-{ver}-vita.vpk",
     "PlayStation Vita (homebrew, HENkaku/Ensō): install it with VitaShell; "
     "your extracted game goes in `ux0:data/eidos/extract/`. The fast "
     "renderer needs `libshacccg.suprx` extracted from your own Vita "
     "(`ur0:data/`). Early: tested in an emulator only."),
]

NOTICE = (
    "eidos is an unofficial fan project, not affiliated with or endorsed by "
    "Nintendo, Game Freak, Creatures Inc. or The Pokemon Company. It contains "
    "no game data: you need your own copy of Pokemon Black or White. "
    "Pokemon and Nintendo DS are trademarks of their respective owners."
)


def changelog_section(version):
    path = os.path.join(ROOT, "CHANGELOG.md")
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return None
    m = re.search(r"^## %s\s*$(.*?)(?=^## |\Z)" % re.escape(version), text,
                  re.M | re.S)
    return unwrap(m.group(1).strip()) if m else None


def unwrap(text):
    """CHANGELOG.md is wrapped at 72 columns, but a release body keeps every
    line break, so each list item and paragraph becomes one line."""
    out = []
    for line in text.split("\n"):
        s = line.strip()
        starts = (not s or s.startswith(("- ", "* ", "#", "|", "```"))
                  or re.match(r"\d+\. ", s))
        if out and out[-1].strip() and not starts:
            out[-1] = out[-1].rstrip() + " " + s
        else:
            out.append(line.rstrip())
    return "\n".join(out)


def table(downloads, version, have):
    """The rows of a download table, only for the files that are there."""
    rows = ["| File | |", "| --- | --- |"]
    for name, what in downloads:
        name = name.format(ver=version)
        if have is not None and name not in have:
            continue
        rows.append("| `%s` | %s |" % (name, what))
    return rows


def notes(version, assets, test_build=None):
    have = set(os.listdir(assets)) if assets and os.path.isdir(assets) else None
    out = []
    section = changelog_section(version)
    if test_build:
        out.append("> **Test build** (%s) of eidos %s, to check the downloads "
                   "before the release. The launcher does not offer it unless "
                   "*Settings > Updates > Show test builds in Versions* is on, "
                   "and it does not update itself to it." % (test_build, version))
        out.append("")
    out.append("## What's new in %s" % version)
    out.append("")
    out.append(section or "- Fixes and small changes.")
    out.append("")
    out.append("## Download")
    out.append("")
    out.extend(table(DOWNLOADS, version, have))
    out.append("")
    rows = table(STANDALONE, version, have)
    if len(rows) > 2:
        out.append("<details>")
        out.append("<summary>eidos without the launcher (Windows, macOS, "
                   "Linux)</summary>")
        out.append("")
        out.extend(rows)
        out.append("")
        out.append("</details>")
        out.append("")
    out.append("On the first start, eidos asks for your own Pokemon Black or "
               "White ROM. `SHA256SUMS` lists the checksum of each file; "
               "`latest.json` and the `.app.tar.gz` are for the launcher's "
               "own updates.")
    out.append("")
    out.append("---")
    out.append("")
    out.append("<sub>%s</sub>" % NOTICE)
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version")
    ap.add_argument("--assets", help="the folder of the release's files")
    ap.add_argument("--test-build", help="a test build's tag (the notes say so)")
    args = ap.parse_args()
    # UTF-8 on every system (Windows' console default would turn é into a
    # byte GitHub cannot read)
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stdout.write(notes(args.version.lstrip("vV"), args.assets, args.test_build))


if __name__ == "__main__":
    main()
