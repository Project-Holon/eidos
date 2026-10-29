#!/usr/bin/env python3
"""Build unmodified Aletheia task code through wasm32 -> C -> native macOS.

This is a compiler/runtime smoke test, not a playable game or a full port.
No ROM or WASI sysroot is needed. All generated files stay under --out.
"""
import argparse
from pathlib import Path
import subprocess


def run(command):
    subprocess.run([str(arg) for arg in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aletheia-root", required=True, type=Path)
    parser.add_argument("--wabt", required=True, type=Path, help="WABT 1.0.42 installation")
    parser.add_argument("--wasm-ld", required=True, type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    root, wabt, output = (path.resolve() for path in
                          (args.aletheia_root, args.wabt, args.out))
    fixture = Path(__file__).resolve().parent / "macos_smoke"
    output.mkdir(parents=True, exist_ok=True)
    sources = [root / "src/game/arm9/tcb_list.c", fixture / "scheduler.c"]
    objects = []
    for index, source in enumerate(sources):
        obj = output / (str(index) + ".o")
        run([args.clang, "--target=wasm32-unknown-unknown", "-ffreestanding",
             "-fno-short-enums", "-O2", "-std=c99", "-I", root / "src/game/arm9",
             "-c", source, "-o", obj])
        objects.append(obj)
    module = output / "scheduler.wasm"
    # LLVM's linker selects its driver by argv[0]; preserve wasm-ld's
    # symlink name instead of resolving it to the generic lld binary.
    run([args.wasm_ld.absolute(), "--no-entry", "--export=run_checks",
         "--export=pointer_width", "--export=total_checks", *objects, "-o", module])
    run([wabt / "bin/wasm-validate", module])
    run([wabt / "bin/wasm2c", module, "--module-name=scheduler",
         "-o", output / "scheduler.c"])
    runtime = wabt / "share/wabt/wasm2c"
    executable = output / "scheduler_native"
    run([args.cc, "-O2", "-I", output, "-I", wabt / "include", "-I", runtime,
         output / "scheduler.c", runtime / "wasm-rt-impl.c",
         runtime / "wasm-rt-mem-impl.c", runtime / "wasm-rt-exceptions-impl.c",
         fixture / "main.c", "-lm", "-o", executable])
    run([executable])


if __name__ == "__main__":
    main()
