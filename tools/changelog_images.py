#!/usr/bin/env python3
"""A release's changelog section drawn as a few square images of one size,
for the Discord post (tools/discord_release.py), in the launcher's look:
morphe's colours, the striped backdrop, game windows with an outline and an
inner rim, small capital section labels, the "EIDOS" display title.

The page is HTML drawn by a headless Chrome (or Edge): the script lays the
notes into pages in the browser, reads how many it made, then takes the
picture and cuts it into one image a page.

    python tools/changelog_images.py 0.2.4 --out DIR [--theme day|night]
        [--design window|rows|bar] [--fonts DIR] [--label TEXT]

The theme follows the launcher: by default an even last version number
(0.2.4) is day (Reshiram's whites and reds), an odd one night (Zekrom's
black and blue).  --fonts names a folder with MarcellusSC-Regular.ttf and
Inter-Regular.ttf, Inter-SemiBold.ttf, Inter-Bold.ttf.  Needs Pillow.
"""

import argparse
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_notes import changelog_section  # noqa: E402

SIZE = 1200             # every image: SIZE x SIZE
MAX_PAGES = 10          # Discord takes 10 files a message

# morphe ui/theme.css, the launcher's colours
THEMES = {
    "night": dict(bg="#121419", stripe="rgba(255,255,255,0.025)", panel="#1c2028",
                  panel2="#242a34", panel3="#2d3440", outline="#07080a", rim="#3a4352",
                  ink="#f3f5f8", muted="#9aa4b4", faint="#6b7586", accent="#2f8fe8",
                  accent_dark="#1b64b0", accent_ink="#ffffff", gold="#f4c542"),
    "day": dict(bg="#eeeef5", stripe="rgba(74,74,106,0.045)", panel="#ffffff",
                panel2="#f2f2f8", panel3="#e2e2ee", outline="#4a4a6a", rim="#d6d6e6",
                ink="#23233a", muted="#5d6282", faint="#8a8fab", accent="#e0442c",
                accent_dark="#b8321e", accent_ink="#ffffff", gold="#f08a24"),
}

CSS = """
@font-face { font-family: Display; src: url("%(display)s"); }
@font-face { font-family: Body; src: url("%(regular)s"); font-weight: 400; }
@font-face { font-family: Body; src: url("%(semibold)s"); font-weight: 600; }
@font-face { font-family: Body; src: url("%(bold)s"); font-weight: 700; }
:root {
  --bg: %(bg)s; --stripe: %(stripe)s; --panel: %(panel)s; --panel-2: %(panel2)s;
  --panel-3: %(panel3)s; --outline: %(outline)s; --rim: %(rim)s; --ink: %(ink)s;
  --muted: %(muted)s; --faint: %(faint)s; --accent: %(accent)s;
  --accent-dark: %(accent_dark)s; --accent-ink: %(accent_ink)s; --gold: %(gold)s;
}
* { box-sizing: border-box; }
html, body { margin: 0; background: #000; }
body { font: 27px/1.42 Body, sans-serif; color: var(--ink); -webkit-font-smoothing: antialiased; }
.page {
  width: %(size)dpx; height: %(size)dpx; overflow: hidden; position: relative;
  padding: 44px 48px 64px;
  background-color: var(--bg);
  background-image: repeating-linear-gradient(135deg, var(--stripe) 0 2px, transparent 2px 8px);
  display: flex; flex-direction: column; gap: 26px;
}
.page > * { flex-shrink: 0; }
.window {
  background: var(--panel); border: 3px solid var(--outline); border-radius: 14px;
  box-shadow: inset 0 0 0 3px var(--rim);
}
.head { display: flex; align-items: baseline; gap: 22px; padding: 4px 6px 0; }
.head .display { font-family: Display, serif; letter-spacing: 0.08em; font-size: 78px; line-height: 1; }
.head .ver { font-size: 44px; font-weight: 700; }
.head .tag { margin-left: auto; align-self: center; font-size: 22px; font-weight: 700;
  padding: 4px 14px; border-radius: 6px; background: var(--accent); color: var(--accent-ink); }
.label { font-size: 21px; font-weight: 700; letter-spacing: 0.12em; text-transform: uppercase;
  color: var(--faint); margin: 0 0 10px 4px; }
.items { list-style: none; margin: 0; padding: 8px 10px; }
.items li { padding: 14px 16px; }
.items li + li { border-top: 2px solid var(--panel-3); }
.items b { font-weight: 700; color: var(--ink); }
.items .t { color: var(--muted); }
.items code { font: inherit; font-size: 0.9em; font-weight: 600; color: var(--ink);
  background: var(--panel-3); padding: 1px 9px; border-radius: 6px; white-space: nowrap; }
.d-window .items b::after, .d-bar .items b::after { content: ":"; }
.foot { position: absolute; left: 48px; right: 48px; bottom: 20px; display: flex;
  justify-content: space-between; font-size: 20px; color: var(--faint); }
.foot .brand { font-family: Display, serif; letter-spacing: 0.08em; }

/* rows: each item a launcher row, its label a badge above the text */
.d-rows .items li { display: flex; flex-direction: column; gap: 6px; }
.d-rows .items b { align-self: flex-start; font-size: 21px; padding: 2px 12px; border-radius: 6px;
  background: var(--panel-3); color: var(--ink); }

/* bar: each section's title is the game's selection bar, items under it */
.d-bar .label { margin: 0; padding: 12px 22px; border-radius: 11px 11px 0 0;
  background: linear-gradient(90deg, var(--accent), var(--accent-dark));
  color: var(--accent-ink); letter-spacing: 0.1em; border-bottom: 3px solid var(--outline); }
.d-bar .sec.window { overflow: hidden; }
"""

SCRIPT = """
const W = %(size)d, MAX = %(max)d;
const data = JSON.parse(document.getElementById('data').textContent);
const design = document.body.dataset.design;
const root = document.getElementById('pages');
let pages = [];
function newPage(first) {
  const p = document.createElement('div');
  p.className = 'page';
  if (first) {
    const h = document.createElement('div');
    h.className = 'head';
    h.innerHTML = data.head;
    p.appendChild(h);
  }
  root.appendChild(p);
  pages.push(p);
  return p;
}
function section(page, title) {
  const s = document.createElement('div');
  s.className = 'sec' + (design === 'bar' ? ' window' : '');
  const l = document.createElement('div');
  l.className = 'label';
  l.textContent = title;
  const ul = document.createElement('ul');
  ul.className = 'items' + (design === 'bar' ? '' : ' window');
  s.appendChild(l);
  s.appendChild(ul);
  page.appendChild(s);
  return ul;
}
// a page's boxes end above the page number, with room to spare
const LIMIT = W - 84;
function bottom(p) {
  const top = p.getBoundingClientRect().top;
  let b = 0;
  for (const c of p.children) {
    if (c.classList.contains('foot')) continue;
    b = Math.max(b, c.getBoundingClientRect().bottom - top);
  }
  return b;
}
const over = (p) => bottom(p) > LIMIT;
// lay out only once the fonts are in: measured with a stand-in font, the
// boxes grew past the page when the real one arrived
document.fonts.ready.then(() => {
  let page = newPage(true);
  for (const sec of data.sections) {
    let ul = section(page, sec.title), placed = 0;
    for (const item of sec.items) {
      const li = document.createElement('li');
      li.innerHTML = item;
      ul.appendChild(li);
      if (over(page)) {
        ul.removeChild(li);
        if (placed === 0) ul.parentNode.remove();
        page = newPage(false);
        ul = section(page, placed === 0 ? sec.title : sec.title + ' (continued)');
        ul.appendChild(li);
      }
      placed++;
    }
  }
  let worst = 0;
  pages.forEach((p, i) => {
    worst = Math.max(worst, bottom(p));
    const f = document.createElement('div');
    f.className = 'foot';
    f.innerHTML = '<span class="brand">EIDOS ' + data.version + '</span><span>' +
      (i + 1) + ' / ' + pages.length + '</span>';
    p.appendChild(f);
  });
  document.body.dataset.worst = Math.ceil(worst);
  document.body.dataset.pages = pages.length;
});
"""


def sections(text):
    """[(title, [item, ...])] from the "###" parts; an item is one "- " line."""
    out = []
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("### "):
            out.append((s[4:].strip(), []))
        elif out and s.startswith("- "):
            out[-1][1].append(s[2:].strip())
        elif out and s and out[-1][1]:
            out[-1][1][-1] += " " + s
    return out


def item_html(item):
    """**Label**: text, with `code`, as HTML."""
    t = html.escape(item, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    m = re.match(r"\*\*(.+?)\*\*:?\s*(.*)$", t)
    if m:
        return '<b>%s</b> <span class="t">%s</span>' % (m.group(1), m.group(2))
    return '<span class="t">%s</span>' % re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)


def browser():
    for p in (os.environ.get("CHROME"), shutil.which("google-chrome"), shutil.which("chromium"),
              shutil.which("chromium-browser"),
              r"C:\Program Files\Google\Chrome\Application\chrome.exe",
              r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"):
        if p and os.path.exists(p):
            return p
    sys.exit("no Chrome or Edge found (set CHROME)")


def file_url(path):
    return "file:///" + os.path.abspath(path).replace("\\", "/").lstrip("/")


def theme_of(version):
    last = int(re.findall(r"\d+", version)[-1])
    return "day" if last % 2 == 0 else "night"


def render(version, out, font_dir, label=None, theme=None, design="bar"):
    import json
    text = changelog_section(version)
    if text is None:
        sys.exit("CHANGELOG.md has no section for %s" % version)
    theme = theme or theme_of(version)
    shown = label or version
    pre = "-" in shown
    head = ('<span class="display">EIDOS</span><span class="ver">%s</span>%s'
            % (html.escape(shown.split("-")[0]),
               '<span class="tag">Pre-release</span>' if pre else
               '<span class="tag">What\'s new</span>'))
    data = {"head": head, "version": html.escape(shown),
            "sections": [{"title": t, "items": [item_html(i) for i in its]}
                         for t, its in sections(text)]}
    fonts = {k: file_url(os.path.join(font_dir, n)) for k, n in (
        ("display", "MarcellusSC-Regular.ttf"), ("regular", "Inter-Regular.ttf"),
        ("semibold", "Inter-SemiBold.ttf"), ("bold", "Inter-Bold.ttf"))}
    css = CSS % dict(THEMES[theme], size=SIZE, **fonts)
    page = ("<!doctype html><html><head><meta charset='utf-8'><style>%s</style></head>"
            "<body class='d-%s' data-design='%s'><div id='pages'></div>"
            "<script id='data' type='application/json'>%s</script><script>%s</script>"
            "</body></html>") % (css, design, design, json.dumps(data).replace("</", "<\\/"),
                                 SCRIPT % dict(size=SIZE, max=MAX_PAGES))
    tmp = tempfile.mkdtemp(prefix="eidos-cl-")
    src = os.path.join(tmp, "notes.html")
    open(src, "w", encoding="utf-8").write(page)
    exe = browser()
    common = [exe, "--headless=new", "--disable-gpu", "--hide-scrollbars",
              "--force-device-scale-factor=1", "--allow-file-access-from-files",
              "--user-data-dir=" + os.path.join(tmp, "profile"), "--no-first-run",
              "--virtual-time-budget=4000"]
    dom = subprocess.run(common + ["--window-size=%d,%d" % (SIZE, SIZE * MAX_PAGES),
                                   "--dump-dom", file_url(src)],
                         capture_output=True, text=True, encoding="utf-8", timeout=120).stdout
    m = re.search(r'data-pages="(\d+)"', dom)
    if not m:
        sys.exit("the browser laid out no pages")
    worst = re.search(r'data-worst="(\d+)"', dom)
    if worst is None or int(worst.group(1)) > SIZE - 84:
        sys.exit("a page's boxes run past its bottom (%s px of %d)"
                 % (worst.group(1) if worst else "?", SIZE - 84))
    n = min(int(m.group(1)), MAX_PAGES)
    shot = os.path.join(tmp, "all.png")
    subprocess.run(common + ["--window-size=%d,%d" % (SIZE, SIZE * n),
                             "--screenshot=" + shot, file_url(src)],
                   capture_output=True, timeout=120, check=True)
    whole = Image.open(shot).convert("RGB")
    os.makedirs(out, exist_ok=True)
    paths = []
    for i in range(n):
        path = os.path.join(out, "changelog-%d.png" % (i + 1))
        whole.crop((0, i * SIZE, SIZE, (i + 1) * SIZE)).save(path, optimize=True)
        paths.append(path)
    shutil.rmtree(tmp, ignore_errors=True)
    return paths


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version")
    ap.add_argument("--out", required=True)
    ap.add_argument("--fonts", required=True, help="MarcellusSC-Regular.ttf and Inter's three weights")
    ap.add_argument("--label", help="the name in the header (a tag such as 0.2.4-pre.1)")
    ap.add_argument("--theme", choices=("day", "night"), help="default: by the version's last number")
    ap.add_argument("--design", choices=("bar", "window", "rows"), default="bar")
    args = ap.parse_args()
    for p in render(args.version.lstrip("vV"), args.out, args.fonts, args.label,
                    args.theme, args.design):
        print(p)


if __name__ == "__main__":
    main()
