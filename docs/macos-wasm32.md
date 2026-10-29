# macOS wasm32 development checks

The shipping architecture proposed in `docs/lp64.md` preserves the game's
32-bit ABI: compile game C to wasm32, translate it to C with WABT's wasm2c,
then compile that C and the platform host into a native macOS executable.
The wasm2c stage does not require a browser or a WebAssembly interpreter.

## Batch layout audit

`tools/lp64/layouts.py` compares compiler-generated record layouts with ARM
EABI. It accepts Ninja's compilation database, generated game wrappers,
explicit target triples, and a detailed JSON report. Compiler failures are
reported separately; a failed compilation is never counted as a match.

From an existing native build:

```sh
ninja -C build-macos -t compdb > ../macos-compile-commands.json
python3 tools/lp64/layouts.py --build build-macos \
  --compdb ../macos-compile-commands.json --aletheia-root ../aletheia \
  --clang /usr/bin/clang --target arm64-apple-macos11 \
  --target wasm32-unknown-unknown --jobs 8 \
  --details-json ../macos-layout-audit.json
```

The initial audit, before the native scalar and pointer layout fixes,
used Aletheia `751566854b4e3974969a78f046b9d78d4436a8b0` and generated
source copies from that checkpoint:

| Comparison | Compared records | Different layouts | Units with differences |
|---|---:|---:|---:|
| ARM EABI reference → macOS ARM64 | 4,798 | 2,457 | 929 |
| ARM EABI reference → wasm32 | 4,798 | 0 | 0 |

The audit obtained ARM reference layouts for 1,342 of 1,403 units. The
remaining 61 units include missing generated inputs and compilation errors;
the JSON retains all 161 failed target compilations. Matching layouts do not
prove runtime correctness, callback signature compatibility, or gameplay.
The ARM EABI reference is not a substitute for the game's own size checks:
CodeWarrior uses four-byte alignment for some 64-bit record members, which
the existing MinGW build handles separately. Wire records still need those
checks when moving to WASI.

## Native compiler/runtime smoke test

`tools/lp64/macos_smoke.py` builds **unmodified**
`aletheia/src/game/arm9/tcb_list.c`, validates the wasm module, translates it
to C, builds the WABT runtime and a native host, and runs task-pool tests.
It needs no ROM or WASI sysroot. It writes only into `--out`.

Dependencies: a clang with wasm32 support, LLVM's `wasm-ld`, WABT 1.0.42
including its runtime sources, and a native C compiler. Preserve the
`wasm-ld` symlink name: LLVM selects its linker driver from that name.

```sh
python3 tools/lp64/macos_smoke.py --aletheia-root ../aletheia \
  --wabt "$EIDOS_WABT_ROOT" \
  --wasm-ld "$EIDOS_WASM_LD" \
  --out ../wasm-scheduler-smoke
```

Set `EIDOS_WABT_ROOT` to the WABT installation and `EIDOS_WASM_LD` to
LLVM's wasm-ld executable. These tool locations are independent of the repo.

On macOS ARM64 this produces a Mach-O ARM64 executable and reports:

```text
scheduler wasm2c: host pointers 8 bytes, game pointers 4 bytes, 329 checks, failure line 0
```

WABT's macOS ARM64 release archive was checked against its published SHA256:
`wabt-1.0.42-macos-arm64.tar.gz.sha256`.

## Full-game build

The full game uses the shared two-stage build in `tools/build64.py`:
WASI compilation and signature repair, followed by wasm2c translation and
native compilation of `host/`. See `docs/lp64.md` for its architecture.
The SDK's imports are declared in `sdk/wasm_host.h`; the game threads use
host fibers, with a separate module stack for each fiber. Socket waits yield
to the other fibers, and filesystem calls use `host/wasi.c`.

With the toolchain already installed by `tools/fetch_wasm.py` and SDL3
available from Homebrew:

```sh
python3 tools/build64.py --build-dir build-macos64 \
  --aletheia-root ../aletheia --sdl3-prefix "$(brew --prefix sdl3)"
```

`EIDOS_WASM` controls the optional mod runtime, not whether the game itself
is compiled to WebAssembly. The full build enables that runtime and relay
TLS. Generated sources and native objects are managed by CMake/Ninja,
including header dependencies and compiler options.

The standalone scheduler smoke test only checks the compiler/runtime path.
Before packaging a macOS app, verify boot with extracted assets, new game,
field movement, battles, and save/continue in the full build. Direct LP64
builds remain diagnostic because the game's data layouts assume 32-bit
pointers.
