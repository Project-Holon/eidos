"""The renderer benchmark: standard scenes, each run paced (the game's own
speed: is it smooth?) and at the most the machine can do (how much room is
left?), one table at the end.  Each run writes eidos_boot's --bench files
(engine/frame/prof.h) into OUT.

    python tools/bench.py --exe PATH/eidos_boot --save PATH/pokeblack.sav [--out DIR]
                          [--scenes field,battle] [--modes paced,max] [--frames 3000]

The save must stand in the field after the first Pokemon Centre (the battle
scene starts a trainer battle there).  The runs use a profile folder of
their own under OUT: the save is a copy, the player's own saves are never
touched.  Extra environment for every run: --env NAME=VALUE (repeatable),
for example EIDOS_RENDERER to pick a backend.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

BOOT = 5800          # the title, Continue, and the field running (frames)

# What a run's log must show for its numbers to count: the scene was really
# reached (a save the game does not load starts a New Game instead, and the
# presses then measure the professor's intro).
REACHED = {
    "field": "eidos: debug move: field:200 at frame",
    "battle": "eidos: debug battle: trainer 33",
}

SCENES = {
    # walking back and forth in town: the field's 2D, 3D and scrolling
    "field": {
        "EIDOS_DEBUG_MOVE_WHEN": "field:200",
        "EIDOS_DEBUG_MOVE": ",".join(["0:walk:L:4", "0:walk:R:4"] * 32),
    },
    # a whole trainer battle: the battle's 3D, the backdrop, the effects
    "battle": {
        "EIDOS_DEBUG_WHEN": "field:200",
        "EIDOS_DEBUG_BATTLE": "trainer 33",
        "EIDOS_DEBUG_A_UNTIL": "whiteout",
        "EIDOS_DEBUG_A_FROM": "5600",
    },
}


def run(args, scene, mode):
    tag = "%s_%s" % (scene, mode)
    root = os.path.join(args.out, tag)
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "prof", "saves"))
    shutil.copy(args.save, os.path.join(root, "prof", "saves", "pokeblack.sav"))
    with open(os.path.join(root, "input.txt"), "w") as f:
        f.write("4700 press START 2\n5420 press A 2\n5700 press A 2\n")
    env = dict(os.environ)
    env.update(SCENES[scene])
    env.update({"EIDOS_NO_AUDIO": "1", "EIDOS_WINDOW_SIZE": args.size})
    if args.hidden:
        env["EIDOS_WINDOW_HIDDEN"] = "1"
    if mode == "max":
        env["EIDOS_FAST_FORWARD"] = "max"
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k] = v
    bench = os.path.join(root, "bench.json")
    cmd = [args.exe, "--profile-dir", os.path.join(root, "prof"), "--window",
           "--frames", str(BOOT + 600 + args.frames), "--input", os.path.join(root, "input.txt"),
           "--bench", "%s:%d" % (bench, BOOT + 600)]
    with open(os.path.join(root, "run.txt"), "w") as log:
        subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=root,
                       timeout=args.timeout)
    with open(os.path.join(root, "run.txt"), encoding="utf-8", errors="replace") as log:
        if REACHED[scene] not in log.read():
            print("bench: %s %s did not reach its scene (no \"%s\" in run.txt): not counted; "
                  "does the save load?" % (scene, mode, REACHED[scene]), flush=True)
            return None
    try:
        with open(bench) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--save", required=True)
    ap.add_argument("--out", default="bench_out")
    ap.add_argument("--scenes", default="field,battle")
    ap.add_argument("--modes", default="paced,max")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--hidden", action="store_true", help="hide the window (numbers then miss the "
                    "display's own pacing)")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--timeout", type=int, default=1200)
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    rows = []
    for scene in args.scenes.split(","):
        for mode in args.modes.split(","):
            print("bench: %s %s ..." % (scene, mode), flush=True)
            r = run(args, scene, mode)
            rows.append((scene, mode, r))
    print()
    print("%-7s %-6s %-14s %6s %8s %8s %8s %8s %6s %6s" % (
        "scene", "mode", "backend", "frames", "fps", "work50", "work99", "worst", "late", "drop"))
    for scene, mode, r in rows:
        if r is None:
            print("%-7s %-6s (no result: see %s)" % (scene, mode,
                  os.path.join(args.out, scene + "_" + mode, "run.txt")))
            continue
        iv, wk = r["interval_ms"], r["work_ms"]
        print("%-7s %-6s %-14s %6d %8.1f %8.2f %8.2f %8.2f %6d %6d" % (
            scene, mode, r.get("backend", "?")[:14], r["frames"],
            1000.0 / iv["mean"] if iv["mean"] else 0.0, wk["p50"], wk["p99"], iv["max"],
            iv["over_20"], iv["over_33_3"]))
    print("\nfps: frames a second; work50/work99: the median and 99th-percentile work in a "
          "frame (ms); worst: the longest gap between frames (ms); late: gaps over 20 ms; "
          "drop: gaps over 33.3 ms (a frame missed).")
    with open(os.path.join(args.out, "summary.json"), "w") as f:
        json.dump([{"scene": s, "mode": m, "result": r} for s, m, r in rows], f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
