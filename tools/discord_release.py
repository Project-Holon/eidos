#!/usr/bin/env python3
"""Post a published release to the Discord releases channel: a ping for the
release role, the release's name and link, and its changelog section.

Run by .github/workflows/discord.yml when a release is published (a draft
the release workflow made, published by hand).  The webhook comes from the
DISCORD_RELEASE_WEBHOOK environment variable (a repository secret), never
from a file.

    python tools/discord_release.py v0.2.4 --url URL [--prerelease] [--role ID] [--dry-run]
"""

import argparse
import json
import os
import re
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_notes import changelog_section  # noqa: E402

# Discord's limits: 4096 characters for an embed's text, 10 embeds and 6000
# characters in all for a message.
EMBED_TEXT = 4096
MESSAGE_TEXT = 6000
COLOUR = 0x5865F2


def version_of(tag):
    """v0.2.4-pre.1 -> 0.2.4: a test build has its version's changelog."""
    return re.sub(r"-.*$", "", tag.lstrip("vV"))


def chunks(text, size):
    """The text cut at line ends into parts of at most size characters."""
    out, cur = [], ""
    for line in text.split("\n"):
        while len(line) > size:
            if cur:
                out.append(cur)
                cur = ""
            out.append(line[:size])
            line = line[size:]
        add = line if not cur else cur + "\n" + line
        if len(add) > size:
            out.append(cur)
            cur = line
        else:
            cur = add
    if cur.strip():
        out.append(cur)
    return out


def cards(section):
    """One card per "### " heading: (title, text); text before the first
    heading is a card with no title."""
    out, title, lines = [], None, []
    for line in section.split("\n"):
        if line.startswith("### "):
            if title is not None or "".join(lines).strip():
                out.append((title, "\n".join(lines).strip()))
            title, lines = line[4:].strip(), []
        else:
            lines.append(line)
    if title is not None or "".join(lines).strip():
        out.append((title, "\n".join(lines).strip()))
    return out


def messages(tag, url, prerelease, role):
    """The messages to send: the first has the ping, the title and the link;
    the cards fill messages of at most 10 cards and 6000 characters."""
    version = version_of(tag)
    section = changelog_section(version) or "See the release page for the notes."
    title = "eidos %s%s" % (tag, " (pre-release)" if prerelease else "")
    # <url>: the cards are the preview, so Discord adds none of its own
    head = "%s**%s** is out\n<%s>" % ("<@&%s> " % role if role else "", title, url)
    embeds = []
    for name, text in cards(section):
        for i, part in enumerate(chunks(text, EMBED_TEXT) or [""]):
            embed = {"description": part, "color": COLOUR}
            if name:
                embed["title"] = name if i == 0 else name + " (more)"
            embeds.append(embed)
    out, cur, size = [], [], 0
    for e in embeds:
        n = len(e["description"]) + len(e.get("title", ""))
        if cur and (len(cur) == 10 or size + n > MESSAGE_TEXT - 200):
            out.append(cur)
            cur, size = [], 0
        cur.append(e)
        size += n
    if cur:
        out.append(cur)
    msgs = [{"embeds": m, "allowed_mentions": {"roles": []}} for m in out] or [
        {"embeds": [], "allowed_mentions": {"roles": []}}]
    msgs[0]["content"] = head
    msgs[0]["allowed_mentions"] = {"roles": [role] if role else []}
    return msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("tag")
    ap.add_argument("--url", required=True, help="the release's page")
    ap.add_argument("--prerelease", action="store_true")
    ap.add_argument("--role", help="the role to ping (its id)")
    ap.add_argument("--dry-run", action="store_true", help="print the message, send nothing")
    args = ap.parse_args()
    msgs = messages(args.tag, args.url, args.prerelease, args.role)
    if args.dry_run:
        sys.stdout.reconfigure(encoding="utf-8")
        print(json.dumps(msgs, indent=2, ensure_ascii=False))
        return
    hook = os.environ.get("DISCORD_RELEASE_WEBHOOK", "")
    if not hook:
        sys.exit("DISCORD_RELEASE_WEBHOOK is not set")
    for body in msgs:
        req = urllib.request.Request(
            hook, data=json.dumps(body).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json",
                     # Discord's front end refuses Python's default agent
                     "User-Agent": "eidos-release (https://github.com/Project-Holon/eidos, 1)"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            print("posted:", resp.status)


if __name__ == "__main__":
    main()
