/*
 * sdk/os_arena.c -- the OS arena, the memory the game's heaps are built in.
 *
 * On the DS the SDK hands the game fixed regions of main RAM ("arenas") and
 * the game carves its heaps out of them once at boot.  The call that does it
 * is OS_AllocFromArenaLo at 0x02086788 (aletheia's layout/symbols_sdk.txt):
 * take `size` bytes off the low end of arena `id`, aligned to `align`, and
 * move the low end up.  There is no free; an arena only ever shrinks from the
 * bottom.  src/matched/sub_0202FC48.c, the heap environment constructor, is
 * the port's one caller, under the name sub_02086788: it takes the entry
 * table, the id table and every heap's region from arena 0.
 *
 * The host has no main RAM to hand out, so this file holds one region the
 * port allocates itself and serves every arena id out of it.  That is the
 * only substitution: from here upwards the heaps are the game's own
 * NitroSystem expanded heaps (src/matched/func_02060814.c and friends),
 * walking free lists inside this region exactly as they walk main RAM on the
 * DS, and a pointer the game's allocator hands out is an address inside it.
 *
 * The game's own boot (EIDOS_BOOT, docs/boot.md) has an arena of its own:
 * OS_InitArena fills the SDK's arena table in the shared work area (the
 * low ends at DS 0x02FFFDA0, the high ends at 0x02FFFDC4, one word per id)
 * from OS_GetInitArenaLo/Hi (sdk/asm/), which return host addresses inside
 * the port's flat main RAM.  The four accessors below are that table's;
 * aletheia's matched ones (src/calib/func_0208656C.c and its three
 * neighbours) compute the slot as `(idx << 2) + 0x02FFFDC4` and cast the
 * sum, which tools/dsaddr.py cannot rewrite (docs/dsaddr-residue.md,
 * "Address arithmetic outside a pointer cast"), so the port supplies them
 * here through bw_ds_ptr.  OS_AllocFromArenaLo serves the host region when
 * a test set one up (bw_arena_reset) and the table otherwise, which is the
 * boot: then the game's heaps live in the DS main RAM, where the DS has
 * them.  The table holds 32-bit words, so the boot's arena needs 32-bit
 * pointers, as the boot does anyway (docs/armdata.md, "Pointer width").
 *
 * Built only where EIDOS_HEAP_MATCHED is on, which needs 32-bit pointers:
 * the expanded heap's block header is 0x10 bytes with two pointers in it, and
 * the matched code both hardcodes the 0x10 and does its address arithmetic in
 * `int` (docs/pointer-width.md).
 */

#include <stdlib.h>
#include <string.h>

#include "dsmem.h"
#include "port.h"

#define BW_ARENA_LO_TABLE 0x02FFFDA0u   /* OSi arena info: lo[id] */
#define BW_ARENA_HI_TABLE 0x02FFFDC4u   /*                 hi[id] */

static u8  *g_arena_base;
static u32  g_arena_size;
static u32  g_arena_lo;      /* offset of the low end, grows upwards */

int bw_arena_reset(u32 size)
{
    free(g_arena_base);
    g_arena_base = NULL;
    g_arena_size = 0;
    g_arena_lo = 0;

    if (size == 0) {
        return 0;
    }
    /* malloc's own alignment is at least 8 on every host this builds for,
     * which is more than any alignment the game asks the arena for (4). */
    g_arena_base = (u8 *)malloc(size);
    if (g_arena_base == NULL) {
        return -1;
    }
    /* A fresh DS arena is whatever main RAM held; the heap constructor
     * writes every header it later reads, so nothing depends on this, and a
     * known fill makes a stray read show up as a pattern rather than as
     * whatever the host left there. */
    memset(g_arena_base, 0xA5, size);
    g_arena_size = size;
    return 0;
}

u32 bw_arena_used(void)
{
    return g_arena_lo;
}

u32 bw_arena_size(void)
{
    return g_arena_size;
}

int bw_arena_contains(const void *p)
{
    const u8 *b = (const u8 *)p;

    return g_arena_base != NULL && b >= g_arena_base
        && b < g_arena_base + g_arena_size;
}

/*
 * OS_AllocFromArenaLo.  Returns NULL when the arena cannot satisfy the
 * request, which is the SDK's contract and what sub_0202FC48 tests for: it
 * records the index of the heap that did not fit in the manager's error
 * halfword and returns 0.
 */
void *sub_02086788(u32 id, u32 size, u32 align)
{
    u32 lo;

    if (g_arena_base == NULL) {
        return bw_os_alloc_from_arena_lo(id, size, align);
    }
    if (align < 4u) {
        align = 4u;
    }
    lo = (g_arena_lo + (align - 1u)) & ~(align - 1u);
    if (lo > g_arena_size || size > g_arena_size - lo) {
        return NULL;
    }
    g_arena_lo = lo + size;
    return g_arena_base + lo;
}

/* ------------------------------------------------------------------ */
/* The SDK's arena table (os_arena.o)                                  */
/* ------------------------------------------------------------------ */

static volatile u32 *bw_arena_slot(u32 table, u32 id)
{
    return (volatile u32 *)bw_ds_ptr(table + (id << 2));
}

/* The shared table is DS data: keep DS addresses in its 32-bit slots,
 * translating at the API boundary rather than truncating host pointers. */
static void *bw_arena_host(u32 address)
{
    return address != 0 ? bw_ds_ptr(address) : NULL;
}

static u32 bw_arena_address(const void *host)
{
    u32 address;
    if (host == NULL) return 0;
    address = bw_ds_addr_of(host);
    if (address != 0) return address;
    return bw_ds_wire_addr((uintptr_t)host); /* Accept numeric SDK linker constants. */
}

/* OS_GetArenaHi at 0x0208656C. */
void *OS_GetArenaHi(u32 id)
{
    return bw_arena_host(*bw_arena_slot(BW_ARENA_HI_TABLE, id));
}

/* OS_GetArenaLo at 0x02086580. */
void *OS_GetArenaLo(u32 id)
{
    return bw_arena_host(*bw_arena_slot(BW_ARENA_LO_TABLE, id));
}

/* OS_SetArenaHi at 0x02086760. */
void OS_SetArenaHi(u32 id, void *hi)
{
    *bw_arena_slot(BW_ARENA_HI_TABLE, id) = bw_arena_address(hi);
}

/* OS_SetArenaLo at 0x02086774. */
void OS_SetArenaLo(u32 id, void *lo)
{
    *bw_arena_slot(BW_ARENA_LO_TABLE, id) = bw_arena_address(lo);
}

/*
 * OS_AllocFromArenaLo on the table, as src/calib/func_02086788.c does it:
 * NULL for an arena with no low end; else round the low end up to `align`,
 * round the new low end up too, refuse past the high end, store it.
 */
void *bw_os_alloc_from_arena_lo(u32 id, u32 size, u32 align)
{
    uintptr_t lo = (uintptr_t)OS_GetArenaLo(id);
    uintptr_t start;
    uintptr_t end;
    uintptr_t mask = (uintptr_t)align - 1u;

    if (lo == 0) {
        return NULL;
    }
    start = (lo + mask) & ~mask;
    end = (start + size + mask) & ~mask;
    if (end > (uintptr_t)OS_GetArenaHi(id)) {
        return NULL;
    }
    OS_SetArenaLo(id, (void *)(uintptr_t)end);
    return (void *)(uintptr_t)start;
}
