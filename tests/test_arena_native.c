#include <stdint.h>
#include "port.h"
#include "dsmem.h"
#include "port_test.h"

void OS_SetArenaLo(u32 id, void *lo);
void OS_SetArenaHi(u32 id, void *hi);
void *OS_GetArenaHi(u32 id);
void *bw_os_alloc_from_arena_lo(u32 id, u32 size, u32 align);

int main(void)
{
    void *base, *end, *first, *second;
    bw_port_reset();
    base = bw_ds_ptr(0x02030000u);
    end = bw_ds_ptr(0x02031000u);
    OS_SetArenaLo(0, base);
    OS_SetArenaHi(0, end);
    BW_REQUIRE("low pointer round trip", OS_GetArenaLo(0) == base);
    BW_REQUIRE("high pointer round trip", OS_GetArenaHi(0) == end);
    BW_CHECK_EQ("DS table stores a DS address",
                *(u32 *)bw_ds_ptr(0x02FFFDA0u), 0x02030000u);
    first = bw_os_alloc_from_arena_lo(0, 63, 32);
    second = bw_os_alloc_from_arena_lo(0, 16, 32);
    BW_REQUIRE("first allocation", first == base);
    BW_REQUIRE("rounded second allocation", second == (u8 *)base + 64);
    BW_CHECK_EQ("alignment", (uintptr_t)second & 31u, 0);
    BW_REQUIRE("oversize refused", bw_os_alloc_from_arena_lo(0, 0x1000, 4) == NULL);
    BW_REPORT("test_arena_native");
}
