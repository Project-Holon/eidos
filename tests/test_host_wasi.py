"""Run filesystem regressions against host/wasi.c without SDL or a game.

The generated-header fixture supplies the memory interface and unused sleep
import. All filesystem operations execute the shared host implementation.
"""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "wasm-rt.h").write_text("""#pragma once
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int32_t s32;
typedef struct { u8 *data; u64 size; } wasm_rt_memory_t;
""")
        (root / "game.h").write_text("""typedef struct { wasm_rt_memory_t memory; } w2c_game;
static inline wasm_rt_memory_t *w2c_game_memory(w2c_game *g) { return &g->memory; }
void w2c_eidos_sleep_ns(struct w2c_eidos *, u64);
u32 w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1 *, u32);
""")
        (root / "listing/tmp").mkdir(parents=True)
        (root / "listing/one").touch()
        (root / "listing/tmp/two").touch()
        executable = root / "probe"
        subprocess.run([
            "cc", "-std=c11", "-D_DARWIN_C_SOURCE", "-D_POSIX_C_SOURCE=200809L",
            "-I" + str(root), "-I" + str(ROOT / "host"),
            str(ROOT / "tests/test_host_wasi.c"), str(ROOT / "host/wasi.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable), str(root / "listing")], check=True)


if __name__ == "__main__":
    main()
