"""Check sdk/features.json against the code (the ctest check_features).

features.json tells kenon the first eidos version that reads each table, key
and archive a mod may use, so it can write a mod's `eidos = X.Y.Z` line.  It
is kept by hand (the keys inside a table file live in hand-written parsers),
so this check makes sure every table file a mod's eidos/tables/ may hold
and every `limit.` manifest key the code reads is listed, and that every
version in it is X.Y.Z.  New keys inside a file go into features.json in the
same commit as the parser change.

    python -I tools/check_features.py <eidos root>
"""
import json
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
doc = json.load(open(os.path.join(ROOT, "sdk", "features.json"), encoding="utf-8"))
feats = doc["features"]
bad = []

for k, v in feats.items():
    if not re.fullmatch(r"\d+\.\d+\.\d+", v):
        bad.append("%s: version %r is not X.Y.Z" % (k, v))

tables, limits = set(), set()
for d in ("sdk", "engine", "demo"):
    for base, _, files in os.walk(os.path.join(ROOT, d)):
        for f in files:
            if not f.endswith(".c"):
                continue
            text = open(os.path.join(base, f), encoding="utf-8", errors="replace").read()
            # a mod's table file: named next to eidos/tables/ (the path, or a loader given the name)
            if "eidos/tables/" in text:
                for m in re.finditer(r'eidos/tables/([a-z_]+\.txt)', text):
                    tables.add(m.group(1))
                for m in re.finditer(r'(?:load_file|load_table|read_table|table_path)\([^;]*?"([a-z_]+\.txt)"', text):
                    tables.add(m.group(1))
            for m in re.finditer(r'"(limit\.[a-z_]+)"', text):
                limits.add(m.group(1))

for t in sorted(tables):
    if "tables/" + t not in feats:
        bad.append("tables/%s is read by the code and not in sdk/features.json" % t)
# limit.items, limit.moves and limit.species are read from 0.2.7 on
for k in sorted(limits - {"limit.items", "limit.moves", "limit.species"}):
    if "manifest " + k not in feats:
        bad.append("manifest %s is read by the code and not in sdk/features.json" % k)

if bad:
    print("\n".join(bad))
    sys.exit(1)
print("features.json: %d entries, %d table files and %d limit keys covered" % (len(feats), len(tables), len(limits)))
