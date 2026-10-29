"""Exercise native fiber stack walking and repeated context switches."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "wasm-rt.h").write_text("""#pragma once
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { unsigned char *data; u64 size; } wasm_rt_memory_t;
""")
        (root / "game.h").write_text("""typedef struct { u32 sp; } w2c_game;
static inline wasm_rt_memory_t *w2c_game_memory(w2c_game *g) { (void)g; return 0; }
static inline u32 *w2c_game_0x5F_stack_pointer(w2c_game *g) { return &g->sp; }
void w2c_game_bw_fiber_main(w2c_game *, u32);
""")
        (root / "probe.c").write_text("""#include "host.h"
#include <execinfo.h>
#include <assert.h>
void w2c_eidos_fiber_create(struct w2c_eidos *, u32, u32);
void w2c_eidos_fiber_switch(struct w2c_eidos *, u32);
void w2c_eidos_fiber_exit(struct w2c_eidos *, u32);
w2c_game g_game;
static int visits;
void w2c_game_bw_fiber_main(w2c_game *g, u32 id) {
    assert(id == 1);
    for (int i = 0; i < 3; ++i) {
        void *frames[64];
        /* Some macOS versions stop at a stack the thread does not own
         * and return 0: the walk must end, not crash. */
        (void)backtrace(frames, 64);
        assert(g->sp == 1234);
        ++visits;
        w2c_eidos_fiber_switch(0, 0);
    }
    w2c_eidos_fiber_exit(0, 0);
}
int main(void) {
    host_fibers_init();
    g_game.sp = 5678;
    for (int generation = 0; generation < 2; ++generation) {
        w2c_eidos_fiber_create(0, 1, 1234);
        for (int i = 0; i < 4; ++i) {
            w2c_eidos_fiber_switch(0, 1);
            assert(g_game.sp == 5678);
        }
    }
    assert(visits == 6);
    return 0;
}
""")
        executable = root / "probe"
        subprocess.run([
            "cc", "-O2", "-Wno-deprecated-declarations",
            "-I" + str(root), "-I" + str(ROOT / "host"),
            str(root / "probe.c"), str(ROOT / "host/fiber.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    main()
