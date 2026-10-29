/* Filesystem regression fixture for the shared native WASI adapter. */
#include "host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

w2c_game g_game;
struct w2c_wasi__snapshot__preview1 g_wasi;
struct w2c_eidos g_eidos;

void w2c_eidos_sleep_ns(struct w2c_eidos *e, u64 ns)
{
    (void)e;
    (void)ns;
}

u32 w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1 *,
    u32, u32, u32, u32, u32, u64, u64, u32, u32);
u32 w2c_wasi__snapshot__preview1_fd_readdir(struct w2c_wasi__snapshot__preview1 *,
    u32, u32, u32, u64, u32);

int main(int argc, char **argv)
{
    static u8 memory[8192];
    u32 dir, file, used, first_size = 0, count = 0;
    u64 cookie = 0;
    u8 first[2048];
    unsigned i;

    assert(argc == 2);
    g_game.memory.data = memory;
    g_game.memory.size = sizeof memory;
    host_wasi_init();
    memcpy(memory + 128, argv[1], strlen(argv[1]));
    assert(w2c_wasi__snapshot__preview1_path_open(&g_wasi, 3, 1,
        128, (u32)strlen(argv[1]), 2, 2, 0, 0, 0) == 0);
    memcpy(&dir, memory, 4);

    for (i = 0; i < 2; ++i) {
        assert(w2c_wasi__snapshot__preview1_fd_readdir(&g_wasi,
            dir, 1024, sizeof first, 0, 4) == 0);
        memcpy(&used, memory + 4, 4);
        assert(used > 0);
        if (i == 0) {
            first_size = used;
            memcpy(first, memory + 1024, used);
        } else {
            assert(used == first_size);
            assert(memcmp(first, memory + 1024, used) == 0);
        }
    }

    /* Each 28-byte page holds one header and its short filename. */
    for (i = 0; i < 10; ++i) {
        assert(w2c_wasi__snapshot__preview1_fd_readdir(&g_wasi,
            dir, 1024, 28, cookie, 4) == 0);
        memcpy(&used, memory + 4, 4);
        if (used == 0) break;
        assert(used >= 24);
        memcpy(&cookie, memory + 1024, 8);
        ++count;
    }
    assert(count == 4); /* ., .., tmp, one */

    /* A component also present at / must stay relative to this descriptor. */
    memcpy(memory + 128, "tmp/two", 7);
    assert(w2c_wasi__snapshot__preview1_path_open(&g_wasi,
        dir, 1, 128, 7, 0, 2, 0, 0, 0) == 0);
    memcpy(&file, memory, 4);
    assert(w2c_wasi__snapshot__preview1_fd_close(&g_wasi, file) == 0);
    assert(w2c_wasi__snapshot__preview1_fd_close(&g_wasi, dir) == 0);
    puts("host WASI: repeat scan, pagination, and directory-relative paths passed");
    return 0;
}
