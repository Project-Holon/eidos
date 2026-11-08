#!/usr/bin/env python3
"""Post a published release to the Discord releases channel: one short
message (the release, its one-sentence summary, where to get it) and the
full changelog as a few images (tools/changelog_images.py), so the channel
needs no scrolling.  A full release pings the release role; a pre-release
does not.

The summary is the plain text at the top of the version's CHANGELOG.md
section, before its first "###"; without it, the message has none.

Run by .github/workflows/discord.yml when a release is published.  The
webhook comes from the DISCORD_RELEASE_WEBHOOK environment variable (a
repository secret), never from a file.

    python tools/discord_release.py v0.2.4 --url URL [--prerelease] [--role ID]
                                    [--fonts DIR] [--dry-run]
"""

import argparse
import json
import os
import re
import sys
import tempfile
import urllib.request
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_notes import changelog_section  # noqa: E402

CONTENT_MAX = 2000          # Discord's limit for a message's text


def version_of(tag):
    """v0.2.4-pre.1 -> 0.2.4: a test build has its version's changelog."""
    return re.sub(r"-.*$", "", tag.lstrip("vV"))


def summary(section):
    """The text before the section's first "###", as one paragraph."""
    head = section.split("\n### ")[0] if not section.startswith("### ") else ""
    text = " ".join(l.strip() for l in head.split("\n") if l.strip())
    return [text] if text else []


def message(tag, url, prerelease, role):
    version = version_of(tag)
    section = changelog_section(version) or ""
    name = "eidos %s" % tag.lstrip("vV")
    lines = []
    lines.append("%s**%s is out!**%s" % ("<@&%s> " % role if role else "", name,
                                          " (pre-release)" if prerelease else ""))
    lines.append("")
    lines += summary(section)
    lines.append("")
    if prerelease:
        lines.append("A test build: download it from the release page: <%s>" % url)
    else:
        lines.append("Update from your launcher, or download it from the release page: <%s>" % url)
    lines.append("The full changelog is below.")
    text = "\n".join(lines)
    return text[:CONTENT_MAX]


def multipart(payload, files):
    """A multipart/form-data body: payload_json and files[n]."""
    boundary = uuid.uuid4().hex
    out = []
    out.append(("--%s\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n"
                "Content-Type: application/json\r\n\r\n" % boundary).encode())
    out.append(json.dumps(payload).encode("utf-8"))
    out.append(b"\r\n")
    for i, path in enumerate(files):
        out.append(("--%s\r\nContent-Disposition: form-data; name=\"files[%d]\"; "
                    "filename=\"%s\"\r\nContent-Type: image/png\r\n\r\n"
                    % (boundary, i, os.path.basename(path))).encode())
        out.append(open(path, "rb").read())
        out.append(b"\r\n")
    out.append(("--%s--\r\n" % boundary).encode())
    return b"".join(out), "multipart/form-data; boundary=%s" % boundary


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("tag")
    ap.add_argument("--url", required=True, help="the release's page")
    ap.add_argument("--prerelease", action="store_true")
    ap.add_argument("--role", help="the role to ping (its id)")
    ap.add_argument("--fonts", help="Inter's folder (tools/changelog_images.py)")
    ap.add_argument("--dry-run", action="store_true", help="print the message and make the images, send nothing")
    args = ap.parse_args()

    from changelog_images import render
    text = message(args.tag, args.url, args.prerelease, args.role)
    out = tempfile.mkdtemp(prefix="eidos-changelog-")
    images = render(version_of(args.tag), out, args.fonts, args.tag.lstrip("vV"))[:10]
    # the images as plain attachments, under the message
    payload = {
        "content": text,
        "attachments": [{"id": i, "filename": os.path.basename(p)} for i, p in enumerate(images)],
        "allowed_mentions": {"roles": [args.role] if args.role else []},
    }
    if args.dry_run:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
        print("images:", *images, sep="\n  ")
        return
    hook = os.environ.get("DISCORD_RELEASE_WEBHOOK", "")
    if not hook:
        sys.exit("DISCORD_RELEASE_WEBHOOK is not set")
    body, ctype = multipart(payload, images)
    req = urllib.request.Request(
        hook, data=body, method="POST",
        headers={"Content-Type": ctype,
                 # Discord's front end refuses Python's default agent
                 "User-Agent": "eidos-release (https://github.com/Project-Holon/eidos, 1)"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        print("posted:", resp.status)


if __name__ == "__main__":
    main()
