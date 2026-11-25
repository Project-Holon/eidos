/*
 * sdk/os_alloc.c -- the OS heap module shim, onto malloc/free.
 *
 * The DS build carves fixed arenas out of main RAM and hands out blocks from
 * them by heap id; the matched game code never sees the arena, only the id
 * and the pointer.  On the host there is no reason to reproduce the arena:
 * the ids are kept as an argument and ignored, and every block comes from
 * malloc.  What the port *does* reproduce is the alignment contract, because
 * the game's allocator honours the `align` argument and some matched code
 * relies on it.
 *
 * What is shimmed here is the SDK pair OS_AllocFromHeap / OS_FreeToHeap and
 * the host's aligned-block helper that the rest of the port allocates
 * through.
 *
 * The game's own allocation entry point is no longer shimmed here.
 * sub_02030734, the raw allocator sub_02030098, their bookkeeping, the
 * release entry sub_020307B0 and the expanded heap's own front door
 * func_02060CA0 are matched sources in the build now (CMakeLists.txt, "the
 * heap layer"), so the port supplies only what they bottom out in:
 * stubs/reimpl_02060A08.c and stubs/reimpl_02060ABC.c, the two free-list
 * walks func_02060CA0 dispatches to, and stubs/reimpl_02030164.c, the raw
 * free.  All three go through bw_block_alloc / bw_block_free below, which is
 * what makes an allocation and its release agree about where the malloc
 * block really starts.
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"

/*
 * An over-aligned block stores its malloc base in the word immediately
 * before the pointer handed out, so free can recover it.  Alignments up to
 * 32 bytes cover every value the matched code passes (4, 8, 0x20).
 */
#define BW_HEAP_HEADER 16u

void *bw_aligned_alloc(u32 size, u32 align)
{
    unsigned char *base;
    unsigned char *user;
    size_t slack;

    if (align < 4) {
        align = 4;
    }
    if (align > 256) {
        align = 256;
    }
    slack = (size_t)align + BW_HEAP_HEADER;
    base = (unsigned char *)malloc((size_t)size + slack);
    if (base == NULL) {
        return NULL;
    }
    user = base + BW_HEAP_HEADER;
    user += (align - (size_t)((uintptr_t)user % align)) % align;
    memcpy(user - sizeof(unsigned char *), &base, sizeof(unsigned char *));
    return user;
}

void bw_aligned_free(void *p)
{
    unsigned char *base;

    if (p == NULL) {
        return;
    }
    memcpy(&base, (unsigned char *)p - sizeof(unsigned char *), sizeof(base));
    free(base);
}

/* ------------------------------------------------------------------ */
/* The expanded heap's blocks                                          */
/* ------------------------------------------------------------------ */

/*
 * What the two halves of the expanded-heap allocator hand back, and what the
 * raw free gives up again, on an LP64 host.  Under ILP32 (EIDOS_HEAP_MATCHED)
 * none of this is used: the heap is the game's own NitroSystem expanded heap
 * built in sdk/os_arena.c's region.  On LP64 the expanded heap cannot run,
 * so the block comes from malloc and the head/tail distinction has no
 * effect on the address.
 * Nothing above this line can see the difference: the direction flag exists
 * so that long-lived allocations can be kept from fragmenting the low end of
 * a fixed arena, and a host heap has neither a fixed arena nor that failure
 * mode.  src/matched/sub_02030098.c, the only caller of the front door, tests
 * the result against null and nothing else.
 *
 * **What is aligned is the payload, not the block.**  sub_02030098 adds a
 * 0x1c-byte header to the request and hands out `block + 0x1c`, so on the DS,
 * where 0x1c is a multiple of four and four is all anything needs, the two
 * are the same question.  On a 64-bit host they are not: 0x1c is not a
 * multiple of eight and the game's own structures store host pointers now
 * (sdk/tcbl.h), so an eight-aligned block would hand out a payload that is
 * only four-aligned and every pointer in a task-callback node would be
 * misaligned.  These two functions therefore align `block + 0x1c` and let
 * the block itself land where it must.  The coupling to the header size
 * lives here and nowhere else.
 */
#define BW_BLOCK_HEADER 0x1cu
#define BW_BLOCK_ALIGN  ((u32)sizeof(void *) > 4u ? (u32)sizeof(void *) : 4u)
#define BW_BLOCK_GUARD  64u

void *bw_block_alloc(u32 size, u32 align)
{
    unsigned char *base;
    unsigned char *payload;
    unsigned char *block;
    size_t misalign;

    if (align < BW_BLOCK_ALIGN) {
        align = BW_BLOCK_ALIGN;
    }
    if (align > 256u) {
        align = 256u;
    }

    base = (unsigned char *)malloc((size_t)BW_BLOCK_GUARD + (size_t)align
                                   + (size_t)BW_BLOCK_HEADER + (size_t)size);
    if (base == NULL) {
        return NULL;
    }

    payload = base + BW_BLOCK_GUARD + BW_BLOCK_HEADER;
    misalign = (size_t)((uintptr_t)payload % (uintptr_t)align);
    if (misalign != 0) {
        payload += (size_t)align - misalign;
    }
    block = payload - BW_BLOCK_HEADER;

    memcpy(block - sizeof(base), &base, sizeof(base));
    return block;
}

void bw_block_free(void *block)
{
    unsigned char *base;

    if (block == NULL) {
        return;
    }
    memcpy(&base, (unsigned char *)block - sizeof(base), sizeof(base));
    free(base);
}

/* ------------------------------------------------------------------ */
/* SDK names                                                           */
/* ------------------------------------------------------------------ */

/*
 * Portability note, and the first real one this lane found.  Some matched
 * sources declare these as all-u32, e.g.
 *
 *     extern void OS_FreeToHeap(u32 a, u32 b, u32 heap);
 *
 * because on the DS a pointer *is* a u32 and the matcher only needs the
 * relocation, not the type.  On an LP64 host that declaration cannot carry a
 * pointer.  Neither of these two is in the current port closure, so nothing
 * is broken today; when a TU that declares them this way enters the closure,
 * the fix belongs in the port layer (a wrapper include that predeclares the
 * pointer-typed form before the matched file's own extern), never in the
 * matched file, which must keep its DS bytes.
 */
/*
 * The extra heap, as the DS has it.  On a DS (not a DSi) the game's own
 * OS_CreateExtraHeap (src/game/arm9/os_alloc.c, compiled as written) turns
 * the 16 KiB at 0x02000000, under the ARM9 module, into one more OS heap,
 * and GFL_USE_InitSystem takes 0x3F00 bytes of it as GFL heap 9
 * (GFL_HEAP_RegisterBlockHeap).  Taken from the host instead, heap 9 and
 * what lives in it (heap 5 while the game system runs) were nowhere in the
 * DS address space, and a reader of the DS's memory (a RetroAchievements set
 * reads heap 9's child count at 0x02000034) saw zeros.  So for that one heap
 * the two calls do what the game's own do, on the game's own records
 * (OSiHeapInfo, the extra heap's handle in g_020A86AC) and with its own list
 * helpers: the block is 0x02000020, as on the DS.  Every other heap stays on
 * the host.  The records hold 32-bit pointers: a build with wider ones
 * keeps the host block.
 */
typedef struct BwCell {
    struct BwCell *prev;
    struct BwCell *next;
    long size;
} BwCell;

typedef struct BwHeapDesc {
    long size;
    BwCell *free;
    BwCell *allocated;
} BwHeapDesc;

typedef struct BwHeapInfo {
    volatile int currentHeap;
    int numHeaps;
    void *arenaStart;
    void *arenaEnd;
    BwHeapDesc *heapArray;
} BwHeapInfo;

typedef struct BwExtraHeap {        /* os_alloc.o's .data at 0x020A86AC */
    int handle;                     /* OSi_ExtraHeapHandle, -1 none */
    int arena;                      /* OSi_ExtraHeapArenaId */
} BwExtraHeap;

extern void *bw_dsref_OSiHeapInfo __attribute__((weak));
extern void *bw_dsref_g_020A86AC __attribute__((weak));
extern BwCell *DLAddFront(BwCell *list, BwCell *cell) __attribute__((weak));
extern BwCell *DLExtract(BwCell *list, BwCell *cell) __attribute__((weak));
extern BwCell *DLInsert(BwCell *list, BwCell *cell) __attribute__((weak));

/* The extra heap's record when `arena` and `heap` name it, else NULL. */
static BwHeapDesc *extra_heap(u32 arena, void *heap)
{
    const BwExtraHeap *x;
    BwHeapInfo *info;

    if (sizeof(void *) != 4u || &bw_dsref_OSiHeapInfo == NULL || &bw_dsref_g_020A86AC == NULL
        || bw_dsref_OSiHeapInfo == NULL || bw_dsref_g_020A86AC == NULL
        || DLAddFront == NULL || DLExtract == NULL || DLInsert == NULL || arena >= 9u) {
        return NULL;
    }
    x = (const BwExtraHeap *)bw_dsref_g_020A86AC;
    if (x->handle < 0 || (int)(intptr_t)heap != x->handle || (int)arena != x->arena) {
        return NULL;
    }
    info = ((BwHeapInfo **)bw_dsref_OSiHeapInfo)[arena];
    return info != NULL && x->handle < info->numHeaps ? &info->heapArray[x->handle] : NULL;
}

void *OS_AllocFromHeap(u32 arena, void *heap, u32 size)
{
    BwHeapDesc *hd = extra_heap(arena, heap);
    BwCell *cell;
    long want, leftover;

    if (hd == NULL) {
        return bw_aligned_alloc(size, 4);
    }
    /* the game's own OS_AllocFromHeap: the first cell that fits, split when
     * at least 0x40 bytes are left, the block after the cell's 0x20 bytes */
    want = (long)((size + 0x3Fu) & ~0x1Fu);
    for (cell = hd->free; cell != NULL && want > cell->size; cell = cell->next) {
    }
    if (cell == NULL) {
        return NULL;
    }
    leftover = cell->size - want;
    if ((u32)leftover < 0x40u) {
        hd->free = DLExtract(hd->free, cell);
    } else {
        BwCell *rest = (BwCell *)((u8 *)cell + want);

        cell->size = want;
        rest->size = leftover;
        rest->prev = cell->prev;
        rest->next = cell->next;
        if (rest->next != NULL) {
            rest->next->prev = rest;
        }
        if (rest->prev != NULL) {
            rest->prev->next = rest;
        } else {
            hd->free = rest;
        }
    }
    hd->allocated = DLAddFront(hd->allocated, cell);
    return (u8 *)cell + 0x20;
}

void OS_FreeToHeap(u32 arena, void *heap, void *block)
{
    BwHeapDesc *hd = extra_heap(arena, heap);

    if (hd == NULL) {
        bw_aligned_free(block);
        return;
    }
    if (block != NULL) {
        BwCell *cell = (BwCell *)((u8 *)block - 0x20);

        hd->allocated = DLExtract(hd->allocated, cell);
        hd->free = DLInsert(hd->free, cell);
    }
}
