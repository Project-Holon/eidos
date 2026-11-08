#!/usr/bin/env python3
"""The fingerprint of what a release job builds from, so a release run can
reuse a job's package from an earlier run that built the same inputs
(.github/workflows/release.yml, docs/release.md).

    python3 tools/release_inputs.py JOB [--extra TEXT] [--list]

The fingerprint is a SHA-256 over: the job's files (their git blob ids,
from `git ls-files -s`, so it needs no history), the job's own block of the
release workflow (and the meta job's, which sets the version), the jobs it
takes a file from (the phones and the Switch take the Linux job's game
module), and --extra (the version and the aletheia revision).  It prints
`hash=<hex>` for $GITHUB_OUTPUT; --list prints the files instead.

A file that goes into no build (the docs, the changelog, the progress data,
the release-notes and Discord tools) is not in any fingerprint, so changing
one reuses every package.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "release.yml")

# Not an input of any build.
NEVER = (
    "docs/", "progress/", "research_notes/", "reports/", "launcher/",
    ".github/", "android/", "ios/",
    "README.md", "CHANGELOG.md", "claims.json", ".gitignore",
    "tools/release_notes.py", "tools/discord_release.py",
    "tools/changelog_images.py", "tools/release_inputs.py",
)

# What each job builds from: the game's sources (everything but NEVER), plus
# its own folders; the launchers build from launcher/ alone.
JOBS = {
    "windows": {"game": True},
    "linux": {"game": True},
    "macos": {"game": True},
    "vita": {"game": True},
    "android": {"game": True, "also": ["android/"], "after": ["linux"]},
    "ios": {"game": True, "also": ["ios/"], "after": ["linux"]},
    "switch": {"game": True, "after": ["linux"]},
    "launcher": {"game": False, "also": ["launcher/", "assets/", "tools/launcher_update_json.py"]},
    "launcher-unix": {"game": False, "also": ["launcher/", "assets/", "tools/launcher_update_json.py"]},
}


def tracked():
    out = subprocess.run(["git", "ls-files", "-s"], cwd=ROOT, capture_output=True,
                         text=True, check=True).stdout
    files = []
    for line in out.splitlines():
        meta, path = line.split("\t", 1)
        files.append((path, meta.split()[1]))
    return files


def job_files(job, files):
    spec = JOBS[job]
    out = []
    for path, blob in files:
        also = any(path.startswith(p) for p in spec.get("also", []))
        game = spec["game"] and not any(
            path == p or path.startswith(p) for p in NEVER)
        if also or game:
            out.append((path, blob))
    return out


def workflow_block(name):
    """The job's block of release.yml: its line at two spaces and what
    follows until the next job's."""
    text = open(WORKFLOW, encoding="utf-8").read()
    m = re.search(r"^  %s:\n(.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)" % re.escape(name), text,
                  re.M | re.S)
    if m is None:
        sys.exit("release.yml has no job %s" % name)
    return m.group(0)


def fingerprint(job, files, extra):
    h = hashlib.sha256()
    h.update(("job %s\n" % job).encode())
    for path, blob in job_files(job, files):
        h.update(("%s %s\n" % (blob, path)).encode())
    h.update(workflow_block("meta").encode())
    h.update(workflow_block(job).encode())
    for dep in JOBS[job].get("after", []):
        h.update(("after %s %s\n" % (dep, fingerprint(dep, files, extra))).encode())
    h.update(("extra %s\n" % extra).encode())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("job", choices=sorted(JOBS))
    ap.add_argument("--extra", default="", help="the version, the aletheia revision, a matrix entry")
    ap.add_argument("--list", action="store_true", help="print the job's files")
    args = ap.parse_args()
    files = tracked()
    if args.list:
        for path, _ in job_files(args.job, files):
            print(path)
        return
    print("hash=%s" % fingerprint(args.job, files, args.extra))


if __name__ == "__main__":
    main()
