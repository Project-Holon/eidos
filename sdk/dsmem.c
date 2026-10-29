/*
 * sdk/dsmem.c -- the DS address space, backed by host memory.
 *
 * Why this exists: the matched code reaches hardware and fixed RAM through
 * integer-literal casts (`*(vu16 *)0x04000208 = 1`), which tools/dsaddr.py
 * rewrites into bw_ds_ptr(0x04000208u).  This file decides what host byte
 * each DS address is.  docs/boot.md, "The DS address space", is the design
 * and sdk/dsmem.h the contract; what follows is how each region is backed
 * and where the backing is an approximation.
 *
 * Lookup order.  The registry of host objects is consulted first, for every
 * address: a global the port defines and registers at its DS address
 * (sdk/game_globals.c) is the same bytes whether the matched code names it
 * or casts its address.  The registry is an array sorted by DS address with
 * no two ranges overlapping (bw_ds_register panics on an overlap), so a
 * binary search for the last range starting at or below the address
 * decides it.  Only then does the address fall through to its region.
 *
 * Identity mapping.  Where host pointers are 32 bits and the host lets it
 * (sdk/host_vm.c's bw_vm_reserve_at; on Win32 through a relaunch, see
 * bw_ds_identity_prepare), each region's backing lives at its own DS
 * address: main RAM at host 0x02000000, WRAM at 0x03000000, the register
 * file at 0x04000000 and 0x04100000, palette at 0x05000000, OAM at
 * 0x07000000, and the VRAM window reserved as an inaccessible guard (its
 * banks move with VRAMCNT, so VRAM stays behind the page table).  Then a
 * DS address the game keeps in its data, or passes as an integer, is the
 * host address of its bytes, and only code pointers and registered objects
 * still need translating.  The registry still comes first in bw_ds_ptr, so
 * a registered object answers for its range as before; a raw dereference
 * of an address inside one reaches the region's bytes, not the object.
 * Main RAM is committed whole in this mode, because a raw dereference
 * never passes through main_ptr's commit.  Without identity (LP64, a host
 * that refused the ranges, BW_DS_IDENTITY=0) every region is the movable
 * block or array described below.
 *
 * Regions.
 *   main RAM  16 MiB, flat, TWL layout: no 4 MiB NTR mirroring, so
 *             0x027FFxxx and 0x02FFFxxx are different bytes.  One
 *             contiguous host block (pointer arithmetic from a translated
 *             address must stay inside it), reserved through sdk/host_vm.c
 *             and committed in 64 KiB pieces as addresses are first
 *             translated.  A translation commits its own piece and the next
 *             one, so an object reached through a pointer may straddle one
 *             piece boundary; a stride past that faults loudly on hosts that
 *             commit lazily (Win32) rather than writing somewhere wrong.
 *   WRAM      256 KiB at 0x03000000, mirrored over the whole 0x03xxxxxx
 *             window.  WRAMCNT banking between the CPUs is not modelled:
 *             the ARM9 always sees all of it.
 *   I/O       a 64 KiB register file for 0x04000000-0x0400FFFF, plus 32
 *             bytes at 0x04100000 for the IPC receive FIFO and the card
 *             data port, which the SDK reads by literal address too.
 *   palette   2 KiB, mirrored over 0x05xxxxxx as the hardware does.
 *   VRAM      the nine banks, through a page table (below).
 *   OAM       2 KiB, mirrored over 0x07xxxxxx as the hardware does.
 *
 * VRAM.  The banks live in one host array in LCDC order (A at 0x06800000
 * through I ending at 0x068A3FFF), so a bank and the bank after it are
 * adjacent host bytes, which is what makes the common two-bank layouts (A
 * then B as main BG, F then G) contiguous for pointer arithmetic too.
 * bw_vram_remap reads VRAMCNT_A..I and rebuilds a table of 16 KiB pages for
 * the CPU window (every bank start and every size is a multiple of 16 KiB)
 * and tables of 8 KiB pieces for the spaces only the engines see
 * (bw_vram_slot).  The mapping follows GBATEK's VRAMCNT table for every
 * MST/OFS combination; the engine windows are then filled in with their
 * hardware mirrors (main BG every 512 KiB, main OBJ every 256 KiB, sub BG
 * and sub OBJ every 128 KiB).  When two banks are mapped over the same page
 * the hardware ORs their reads and writes both; here the first bank in
 * A..I order owns the page and the other is not visible there.  The game
 * does not overlap banks on purpose, so this is an approximation of a
 * configuration it should never produce.
 *
 * The mapping is kept current two ways: the VRAMCNT write hooks bw_ds_reset
 * installs (bw_io_write*, bw_io_sync), and a check in every VRAM
 * translation that the ten VRAMCNT bytes still equal the ones the table
 * was built from.  The second is what lets a matched GX_SetBank* store to
 * VRAMCNT through bw_ds_ptr and clear the bank on the next line, with no
 * synchronisation point in between.
 *
 * I/O hooks.  One hook per register address, ranges never overlapping
 * (bw_io_hook panics otherwise).  bw_io_write16/32 store, then run every
 * hook whose register the store touched, with the register's value before
 * and after the store, whether or not the value changed (a write-one-to-
 * clear register needs the call even then).  bw_io_sync compares the file
 * with the copy taken at the last sync and runs the hook of every register
 * whose bytes differ; a matched store of an unchanged value is invisible to
 * it, which is the cost of plain-memory registers (sdk/dsmem.h).  A hook
 * that updates another hooked register should do it with bw_io_write*, so
 * that register's hook runs too.
 *
 * Access hooks (sdk/dsmem_ext.h) are the other kind: a range of I/O
 * addresses whose every translation calls a function before the pointer is
 * returned, and whose function also runs at the top of bw_io_sync.  They
 * exist for ports (the geometry FIFO), where a stream of stores to one
 * address is the data and a compare against the last sync sees one value.
 *
 * Threads.  The game runs one thread at a time under the port's run token
 * (docs/boot.md, "Threads, interrupts and the frame"), so nothing here
 * locks.  There is no lazy global initialisation either: bw_ds_reset builds
 * every table explicitly, before any game thread exists, and a translation
 * before it panics instead of initialising on first use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "host_vm.h"

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static int g_ready;

/* -1 before the first reset decided it, 0 movable blocks, 1 identity */
static int g_identity = -1;

/* main RAM */
#define MAIN_PIECE_SHIFT 16u
#define MAIN_PIECE_SIZE  (1u << MAIN_PIECE_SHIFT)
#define MAIN_PIECES      (BW_DS_MAIN_SIZE >> MAIN_PIECE_SHIFT)

static u8 *g_main;
static u8  g_main_committed[MAIN_PIECES];

/* the small fixed regions: at their DS addresses in identity mode, else
 * these arrays */
static u8 g_wram_store[BW_DS_WRAM_SIZE];
static u8 g_pal_store[BW_DS_PAL_SIZE];
static u8 g_oam_store[BW_DS_OAM_SIZE];
static u8 *g_wram = g_wram_store;
static u8 *g_pal = g_pal_store;
static u8 *g_oam = g_oam_store;

/* The register file: the 64 KiB window, then the 0x04100000 block.  An
 * index into the file (io_index) is an offset into the window, or
 * BW_DS_IO_SIZE plus an offset into the block; io_byte turns it into the
 * byte, which lives at its DS address in identity mode.  No register
 * straddles the two parts. */
#define IO_HI_BASE 0x04100000u
#define IO_HI_SIZE 0x20u
#define IO_FILE_SIZE (BW_DS_IO_SIZE + IO_HI_SIZE)

static u8 g_io_lo_store[BW_DS_IO_SIZE];
static u8 g_io_hi_store[IO_HI_SIZE];
static u8 *g_io_lo = g_io_lo_store;
static u8 *g_io_hi = g_io_hi_store;
static u8 g_io_synced[IO_FILE_SIZE];

static u8 *io_byte(u32 idx)
{
    return idx < BW_DS_IO_SIZE ? &g_io_lo[idx] : &g_io_hi[idx - BW_DS_IO_SIZE];
}

static void io_snapshot(void)
{
    memcpy(g_io_synced, g_io_lo, BW_DS_IO_SIZE);
    memcpy(&g_io_synced[BW_DS_IO_SIZE], g_io_hi, IO_HI_SIZE);
}

#define KEYINPUT_OFF 0x130u

struct IoHook {
    u32           off;     /* index into the register file (io_byte) */
    u32           width;
    BwIoWriteHook fn;
};

#define IO_HOOK_MAX 128
static struct IoHook g_hooks[IO_HOOK_MAX];
static int           g_hook_count;

/* access hooks (sdk/dsmem_ext.h): one call per translation in a range */
struct IoAccess {
    u32            lo, hi;
    BwIoAccessHook fn;
};

#define IO_ACCESS_MAX 4
static struct IoAccess g_access[IO_ACCESS_MAX];
static int             g_access_count;

/* the registry */
struct RegEntry {
    u32         addr;
    u32         size;
    u8         *host;
    const char *name;
};

static struct RegEntry *g_reg;
static size_t           g_reg_count;
static size_t           g_reg_cap;

/* ------------------------------------------------------------------ */
/* VRAM state                                                          */
/* ------------------------------------------------------------------ */

#define VRAM_TOTAL      0xA4000u
#define VRAM_PAGE_SHIFT 14u
#define VRAM_PAGE_SIZE  (1u << VRAM_PAGE_SHIFT)
#define VRAM_PAGES      (BW_DS_VRAM_SIZE >> VRAM_PAGE_SHIFT)
#define VSLOT_SHIFT     13u
#define VSLOT_SIZE      (1u << VSLOT_SHIFT)
#define VSLOT_MAX       (0x80000u >> VSLOT_SHIFT)   /* the texture space */

#define VRAM_LCDC_BASE    0x06800000u
#define VRAM_MAIN_BG      0x06000000u
#define VRAM_SUB_BG       0x06200000u
#define VRAM_MAIN_OBJ     0x06400000u
#define VRAM_SUB_OBJ      0x06600000u

#define VRAMCNT_OFF 0x240u

static u8  g_vram[VRAM_TOTAL];
static u8 *g_vpage[VRAM_PAGES];

/*
 * Where each bank's bytes live.  Normally its piece of g_vram.  With the
 * VRAM window identity-mapped (bw_ds_identity_prepare in a 32-bit build,
 * see "VRAM at its own addresses" below), a bank the CPU can see lives at
 * the CPU address VRAMCNT maps it to, so a VRAM address the game computes
 * (G2_GetBG0CharPtr adds an offset to 0x06000000) is the host address of
 * the byte, and its bytes move with it when VRAMCNT remaps it.
 */
static u8 *g_bank_home[BW_VRAM_BANKS];
static int g_vram_window;           /* 1: the window is the banks' home    */
static int g_vram_window_wanted;    /* bw_ds_identity_prepare asked for it */
static u8  g_vram_move[VRAM_TOTAL]; /* bytes in flight during a remap     */

/* The parts of the 16 MiB window a bank can be mapped at: the four engine
 * windows up to their mirror period, and LCDC. */
static const u32 k_window_base[] = { 0x06000000u, 0x06200000u, 0x06400000u,
                                     0x06600000u, 0x06800000u };
static const u32 k_window_size[] = { 0x80000u, 0x20000u, 0x40000u,
                                     0x20000u, 0xA4000u };
#define VRAM_WINDOW_PARTS 5

static int g_probing;               /* map_bank only reports the CPU base  */
static u32 g_probe_base;
static u8 *g_vslot[BW_VSLOT_KINDS][VSLOT_MAX];
static u8  g_vramcnt_built[BW_VRAM_BANKS];

/* Offset into g_vram, size, and register offset of each bank. */
static const u32 k_bank_off[BW_VRAM_BANKS] = {
    0x00000, 0x20000, 0x40000, 0x60000, 0x80000,
    0x90000, 0x94000, 0x98000, 0xA0000
};
static const u32 k_bank_size[BW_VRAM_BANKS] = {
    0x20000, 0x20000, 0x20000, 0x20000, 0x10000,
    0x04000, 0x04000, 0x08000, 0x04000
};
/* VRAMCNT_A..G are 0x04000240..246; 0x04000247 is WRAMCNT; H and I follow. */
static const u32 k_bank_reg[BW_VRAM_BANKS] = {
    0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249
};

static const u32 k_slot_size[BW_VSLOT_KINDS] = {
    0x80000, 0x18000, 0x8000, 0x2000, 0x8000, 0x2000, 0x40000
};

/* ------------------------------------------------------------------ */
/* Registry                                                            */
/* ------------------------------------------------------------------ */

/* Index of the last entry whose range starts at or below addr, or -1. */
static long reg_floor(u32 addr)
{
    size_t lo = 0;
    size_t hi = g_reg_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (g_reg[mid].addr <= addr) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (long)lo - 1;
}

static const struct RegEntry *reg_find(u32 addr)
{
    long i = reg_floor(addr);

    if (i >= 0 && addr - g_reg[i].addr < g_reg[i].size) {
        return &g_reg[i];
    }
    return NULL;
}

static void reg_overlap_panic(u32 addr, u32 size, const char *name,
                              const struct RegEntry *e)
{
    char msg[256];

    snprintf(msg, sizeof(msg),
             "'%s' [0x%08lX, +0x%lX) overlaps '%s' [0x%08lX, +0x%lX)",
             name != NULL ? name : "?", (unsigned long)addr,
             (unsigned long)size, e->name != NULL ? e->name : "?",
             (unsigned long)e->addr, (unsigned long)e->size);
    bw_panic("bw_ds_register", addr, msg);
}

void bw_ds_register(u32 addr, void *host, u32 size, const char *name)
{
    long   below;
    size_t at;

    if (host == NULL || size == 0u) {
        bw_panic("bw_ds_register", addr, name);
    }
    if (addr + size < addr && addr + size != 0u) {
        bw_panic("bw_ds_register", addr, "range wraps past 0xFFFFFFFF");
    }

    below = reg_floor(addr);
    at = (size_t)(below + 1);
    if (below >= 0 && addr - g_reg[below].addr < g_reg[below].size) {
        reg_overlap_panic(addr, size, name, &g_reg[below]);
    }
    if (at < g_reg_count && g_reg[at].addr - addr < size) {
        reg_overlap_panic(addr, size, name, &g_reg[at]);
    }

    if (g_reg_count == g_reg_cap) {
        size_t cap = g_reg_cap != 0u ? g_reg_cap * 2u : 64u;
        struct RegEntry *grown = (struct RegEntry *)realloc(
            g_reg, cap * sizeof(*grown));
        if (grown == NULL) {
            bw_panic("bw_ds_register", addr, "out of host memory");
        }
        g_reg = grown;
        g_reg_cap = cap;
    }
    memmove(&g_reg[at + 1u], &g_reg[at],
            (g_reg_count - at) * sizeof(g_reg[0]));
    g_reg[at].addr = addr;
    g_reg[at].size = size;
    g_reg[at].host = (u8 *)host;
    g_reg[at].name = name;
    g_reg_count++;
}

int bw_ds_registered(u32 addr, u32 size)
{
    long below = reg_floor(addr);
    size_t at = (size_t)(below + 1);

    if (size == 0u) {
        return 0;
    }
    if (below >= 0 && addr - g_reg[below].addr < g_reg[below].size) {
        return 1;
    }
    return at < g_reg_count && g_reg[at].addr - addr < size;
}

int bw_ds_unregister(u32 addr)
{
    size_t i;

    for (i = 0; i < g_reg_count; i++) {
        if (g_reg[i].addr == addr) {
            memmove(&g_reg[i], &g_reg[i + 1u],
                    (g_reg_count - i - 1u) * sizeof(g_reg[0]));
            g_reg_count--;
            return 1;
        }
        if (g_reg[i].addr > addr) {
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Register file                                                       */
/* ------------------------------------------------------------------ */

/* File index (io_byte) of [addr, addr + width), or -1 if not wholly inside. */
static long io_index(u32 addr, u32 width)
{
    if (addr >= BW_DS_IO_BASE && addr - BW_DS_IO_BASE <= BW_DS_IO_SIZE - width) {
        return (long)(addr - BW_DS_IO_BASE);
    }
    if (addr >= IO_HI_BASE && addr - IO_HI_BASE <= IO_HI_SIZE - width) {
        return (long)(BW_DS_IO_SIZE + (addr - IO_HI_BASE));
    }
    return -1;
}

static u32 io_addr_of_index(u32 idx)
{
    return idx < BW_DS_IO_SIZE ? BW_DS_IO_BASE + idx
                               : IO_HI_BASE + (idx - BW_DS_IO_SIZE);
}

static u32 io_get(const u8 *p, u32 width)
{
    switch (width) {
    case 1:  return *p;
    case 2:  return bw_rd16(p);
    default: return bw_rd32(p);
    }
}

static void io_put(u8 *p, u32 width, u32 v)
{
    switch (width) {
    case 1:  *p = (u8)v; break;
    case 2:  bw_wr16(p, (u16)v); break;
    default: bw_wr32(p, v); break;
    }
}

static long io_checked(u32 addr, u32 width, const char *who)
{
    long idx = io_index(addr, width);

    if (idx < 0) {
        bw_panic(who, addr, "not in the I/O register file");
    }
    return idx;
}

volatile u8 *bw_io(u32 addr)
{
    return (volatile u8 *)io_byte((u32)io_checked(addr, 1, "bw_io"));
}

u16 bw_io_read16(u32 addr)
{
    return (u16)io_get(io_byte((u32)io_checked(addr, 2, "bw_io_read16")), 2);
}

u32 bw_io_read32(u32 addr)
{
    return io_get(io_byte((u32)io_checked(addr, 4, "bw_io_read32")), 4);
}

static void io_write(u32 addr, u32 width, u32 v, const char *who)
{
    u32 idx = (u32)io_checked(addr, width, who);
    int hit[IO_HOOK_MAX];
    u32 old[IO_HOOK_MAX];
    int n = 0;
    int i;

    for (i = 0; i < g_hook_count; i++) {
        const struct IoHook *h = &g_hooks[i];
        if (h->off < idx + width && idx < h->off + h->width) {
            hit[n] = i;
            old[n] = io_get(io_byte(h->off), h->width);
            n++;
        }
    }
    io_put(io_byte(idx), width, v);
    memcpy(&g_io_synced[idx], io_byte(idx), width);
    for (i = 0; i < n; i++) {
        /* Re-read the slot each time: an earlier hook may have changed the
         * hook table (it never should, but stale copies would be worse). */
        const struct IoHook h = g_hooks[hit[i]];
        h.fn(io_addr_of_index(h.off), old[i], io_get(io_byte(h.off), h.width));
        memcpy(&g_io_synced[h.off], io_byte(h.off), h.width);
    }
}

void bw_io_write16(u32 addr, u16 v)
{
    io_write(addr, 2, v, "bw_io_write16");
}

void bw_io_write32(u32 addr, u32 v)
{
    io_write(addr, 4, v, "bw_io_write32");
}

void bw_io_hook(u32 addr, u32 width, BwIoWriteHook hook)
{
    u32 idx;
    int i;

    if (width != 1u && width != 2u && width != 4u) {
        bw_panic("bw_io_hook", addr, "width must be 1, 2 or 4");
    }
    if ((addr & (width - 1u)) != 0u) {
        bw_panic("bw_io_hook", addr, "register is not aligned to its width");
    }
    idx = (u32)io_checked(addr, width, "bw_io_hook");

    for (i = 0; i < g_hook_count; i++) {
        struct IoHook *h = &g_hooks[i];
        if (h->off == idx) {
            if (hook == NULL) {
                *h = g_hooks[--g_hook_count];
            } else {
                h->width = width;
                h->fn = hook;
                memcpy(&g_io_synced[idx], io_byte(idx), width);
            }
            return;
        }
        if (h->off < idx + width && idx < h->off + h->width) {
            bw_panic("bw_io_hook", addr, "overlaps another register's hook");
        }
    }
    if (hook == NULL) {
        return;
    }
    if (g_hook_count == IO_HOOK_MAX) {
        bw_panic("bw_io_hook", addr, "hook table full");
    }
    g_hooks[g_hook_count].off = idx;
    g_hooks[g_hook_count].width = width;
    g_hooks[g_hook_count].fn = hook;
    g_hook_count++;
    /* A hook sees changes from the moment it is installed, not before. */
    memcpy(&g_io_synced[idx], io_byte(idx), width);
}

void bw_io_access_hook(u32 lo, u32 hi, BwIoAccessHook fn)
{
    int i;

    if (fn == NULL || hi <= lo || io_checked(lo, 1, "bw_io_access_hook") < 0
        || io_checked(hi - 1u, 1, "bw_io_access_hook") < 0) {
        bw_panic("bw_io_access_hook", lo, "empty range or no function");
    }
    for (i = 0; i < g_access_count; i++) {
        if (lo < g_access[i].hi && g_access[i].lo < hi) {
            bw_panic("bw_io_access_hook", lo, "overlaps another access hook");
        }
    }
    if (g_access_count >= IO_ACCESS_MAX) {
        bw_panic("bw_io_access_hook", lo, "access hook table full");
    }
    g_access[g_access_count].lo = lo;
    g_access[g_access_count].hi = hi;
    g_access[g_access_count].fn = fn;
    g_access_count++;
}

static void io_access(u32 addr)
{
    int i;

    for (i = 0; i < g_access_count; i++) {
        if (addr >= g_access[i].lo && addr < g_access[i].hi) {
            g_access[i].fn(addr, 0);
            return;
        }
    }
}

void bw_io_sync(void)
{
    int i;

    /* Drain whatever an access hook left pending before comparing, so a
     * register the drain updates is seen by this very sync. */
    for (i = 0; i < g_access_count; i++) {
        g_access[i].fn(0, 1);
    }
    for (i = 0; i < g_hook_count; i++) {
        const struct IoHook h = g_hooks[i];
        u32 before;

        if (memcmp(io_byte(h.off), &g_io_synced[h.off], h.width) == 0) {
            continue;
        }
        before = io_get(&g_io_synced[h.off], h.width);
        memcpy(&g_io_synced[h.off], io_byte(h.off), h.width);
        h.fn(io_addr_of_index(h.off), before, io_get(io_byte(h.off), h.width));
        memcpy(&g_io_synced[h.off], io_byte(h.off), h.width);
    }
    io_snapshot();
}

/* ------------------------------------------------------------------ */
/* VRAM                                                                */
/* ------------------------------------------------------------------ */

static void vram_current(void);

u8 *bw_vram_bank(int bank, u32 *size_out)
{
    if (bank < 0 || bank >= BW_VRAM_BANKS) {
        if (size_out != NULL) {
            *size_out = 0;
        }
        return NULL;
    }
    if (size_out != NULL) {
        *size_out = k_bank_size[bank];
    }
    vram_current();
    return g_bank_home[bank];
}

/* Map `size` bytes of bank memory at CPU address `ds`; first bank wins. */
static void map_cpu(u32 ds, u8 *mem, u32 size)
{
    u32 off;

    if (g_probing) {
        if (g_probe_base == 0u) {
            g_probe_base = ds;
        }
        return;
    }
    for (off = 0; off < size; off += VRAM_PAGE_SIZE) {
        u32 page = (ds - BW_DS_VRAM_BASE + off) >> VRAM_PAGE_SHIFT;
        if (page < VRAM_PAGES && g_vpage[page] == NULL) {
            g_vpage[page] = mem + off;
        }
    }
}

static void map_slot(int kind, u32 at, u8 *mem, u32 size)
{
    u32 off;

    if (g_probing) {
        return;
    }
    for (off = 0; off < size; off += VSLOT_SIZE) {
        u32 piece = (at + off) >> VSLOT_SHIFT;
        if (at + off < k_slot_size[kind] && g_vslot[kind][piece] == NULL) {
            g_vslot[kind][piece] = mem + off;
        }
    }
}

/* Repeat the first `period` bytes of a 2 MiB engine window over all of it. */
static void mirror_window(u32 base, u32 period)
{
    u32 first = (base - BW_DS_VRAM_BASE) >> VRAM_PAGE_SHIFT;
    u32 per = period >> VRAM_PAGE_SHIFT;
    u32 pages = 0x200000u >> VRAM_PAGE_SHIFT;
    u32 p;

    for (p = per; p < pages; p++) {
        u8 *src = g_vpage[first + (p % per)];
        if (g_vpage[first + p] == NULL && src != NULL) {
            g_vpage[first + p] = src;
        }
    }
}

static void map_bank(int bank, u8 cnt)
{
    u8 *mem = g_bank_home[bank];
    u32 size = k_bank_size[bank];
    u32 ofs = (cnt >> 3) & 3u;
    u32 mst = cnt & 7u;
    u32 fg_ofs = 0x4000u * (ofs & 1u) + 0x10000u * (ofs >> 1);

    if ((cnt & 0x80u) == 0u) {
        return;
    }
    if (bank <= BW_VRAM_B) {
        mst &= 3u;   /* A and B have a two-bit MST */
    }
    if (mst == 0u) {
        map_cpu(VRAM_LCDC_BASE + k_bank_off[bank], mem, size);
        return;
    }

    switch (bank) {
    case BW_VRAM_A:
    case BW_VRAM_B:
        if (mst == 1u) {
            map_cpu(VRAM_MAIN_BG + 0x20000u * ofs, mem, size);
        } else if (mst == 2u) {
            map_cpu(VRAM_MAIN_OBJ + 0x20000u * (ofs & 1u), mem, size);
        } else {
            map_slot(BW_VSLOT_TEX, 0x20000u * ofs, mem, size);
        }
        break;
    case BW_VRAM_C:
    case BW_VRAM_D:
        if (mst == 1u) {
            map_cpu(VRAM_MAIN_BG + 0x20000u * ofs, mem, size);
        } else if (mst == 2u) {
            map_slot(BW_VSLOT_ARM7, 0x20000u * (ofs & 1u), mem, size);
        } else if (mst == 3u) {
            map_slot(BW_VSLOT_TEX, 0x20000u * ofs, mem, size);
        } else if (mst == 4u) {
            map_cpu(bank == BW_VRAM_C ? VRAM_SUB_BG : VRAM_SUB_OBJ, mem, size);
        }
        break;
    case BW_VRAM_E:
        if (mst == 1u) {
            map_cpu(VRAM_MAIN_BG, mem, size);
        } else if (mst == 2u) {
            map_cpu(VRAM_MAIN_OBJ, mem, size);
        } else if (mst == 3u) {
            map_slot(BW_VSLOT_TEXPAL, 0, mem, size);
        } else if (mst == 4u) {
            map_slot(BW_VSLOT_BGEXT_MAIN, 0, mem, 0x8000u);
        }
        break;
    case BW_VRAM_F:
    case BW_VRAM_G:
        if (mst == 1u) {
            map_cpu(VRAM_MAIN_BG + fg_ofs, mem, size);
        } else if (mst == 2u) {
            map_cpu(VRAM_MAIN_OBJ + fg_ofs, mem, size);
        } else if (mst == 3u) {
            map_slot(BW_VSLOT_TEXPAL, 0x4000u * ((ofs & 1u) + (ofs >> 1) * 4u),
                     mem, size);
        } else if (mst == 4u) {
            map_slot(BW_VSLOT_BGEXT_MAIN, 0x4000u * (ofs & 1u), mem, size);
        } else if (mst == 5u) {
            map_slot(BW_VSLOT_OBJEXT_MAIN, 0, mem, 0x2000u);
        }
        break;
    case BW_VRAM_H:
        if (mst == 1u) {
            map_cpu(VRAM_SUB_BG, mem, size);
        } else if (mst == 2u) {
            map_slot(BW_VSLOT_BGEXT_SUB, 0, mem, size);
        }
        break;
    case BW_VRAM_I:
        if (mst == 1u) {
            map_cpu(VRAM_SUB_BG + 0x8000u, mem, size);
        } else if (mst == 2u) {
            map_cpu(VRAM_SUB_OBJ, mem, size);
        } else if (mst == 3u) {
            map_slot(BW_VSLOT_OBJEXT_SUB, 0, mem, 0x2000u);
        }
        break;
    default:
        break;
    }
}

/*
 * VRAM at its own addresses.  With the window committed, each bank the CPU
 * can see is kept at the first CPU address VRAMCNT maps it to (the engine
 * window or LCDC), unless an earlier bank already claims bytes there, in
 * which case it stays in its own store and is invisible there, as the page
 * table already has it.  A remap moves the bytes of every bank whose home
 * changes: all of them out first, then each into its new home, so two banks
 * trading places keep their contents.  The address a bank leaves keeps
 * stale bytes; the hardware would show nothing there, and the game does
 * not read a window it just unmapped.  Mirrors past each engine window's
 * period are not backed: a raw access there faults at its DS address
 * (bw_ds_ptr still folds them onto the bank).
 */
static int window_holds(u32 base, u32 size)
{
    int i;

    for (i = 0; i < VRAM_WINDOW_PARTS; i++) {
        if (base >= k_window_base[i] && size <= k_window_size[i]
            && base - k_window_base[i] <= k_window_size[i] - size) {
            return 1;
        }
    }
    return 0;
}

static void vram_rehome(const u8 *cnt)
{
    u8 *home[BW_VRAM_BANKS];
    u32 lo[BW_VRAM_BANKS];
    int b, c;

    for (b = 0; b < BW_VRAM_BANKS; b++) {
        u32 base;

        g_probing = 1;
        g_probe_base = 0;
        map_bank(b, cnt[b]);
        g_probing = 0;
        base = g_probe_base;
        home[b] = &g_vram[k_bank_off[b]];
        lo[b] = 0;
        if (base == 0u || !window_holds(base, k_bank_size[b])) {
            continue;
        }
        for (c = 0; c < b; c++) {
            if (lo[c] != 0u && base < lo[c] + k_bank_size[c]
                && lo[c] < base + k_bank_size[b]) {
                break;
            }
        }
        if (c < b) {
            continue;               /* an earlier bank owns those bytes */
        }
        lo[b] = base;
        home[b] = (u8 *)(uintptr_t)base;
    }
    for (b = 0; b < BW_VRAM_BANKS; b++) {
        if (home[b] != g_bank_home[b]) {
            memcpy(&g_vram_move[k_bank_off[b]], g_bank_home[b], k_bank_size[b]);
        }
    }
    for (b = 0; b < BW_VRAM_BANKS; b++) {
        if (home[b] != g_bank_home[b]) {
            memcpy(home[b], &g_vram_move[k_bank_off[b]], k_bank_size[b]);
            g_bank_home[b] = home[b];
        }
    }
}

void bw_vram_remap(void)
{
    u8 cnt[BW_VRAM_BANKS];
    int b;

    for (b = 0; b < BW_VRAM_BANKS; b++) {
        cnt[b] = g_io_lo[k_bank_reg[b]];
    }
    if (g_vram_window) {
        vram_rehome(cnt);
    }
    memset(g_vpage, 0, sizeof(g_vpage));
    memset(g_vslot, 0, sizeof(g_vslot));
    for (b = 0; b < BW_VRAM_BANKS; b++) {
        g_vramcnt_built[b] = cnt[b];
        map_bank(b, g_vramcnt_built[b]);
    }
    mirror_window(VRAM_MAIN_BG, 0x80000u);
    mirror_window(VRAM_SUB_BG, 0x20000u);
    mirror_window(VRAM_MAIN_OBJ, 0x40000u);
    mirror_window(VRAM_SUB_OBJ, 0x20000u);
}

int bw_ds_vram_window(u32 addr)
{
    int i;

    if (!g_vram_window) {
        return 0;
    }
    for (i = 0; i < VRAM_WINDOW_PARTS; i++) {
        if (addr - k_window_base[i] < k_window_size[i]) {
            return 1;
        }
    }
    return 0;
}

/* Rebuild the tables if a VRAMCNT byte moved since they were built. */
static void vram_current(void)
{
    int b;

    for (b = 0; b < BW_VRAM_BANKS; b++) {
        if (g_io_lo[k_bank_reg[b]] != g_vramcnt_built[b]) {
            bw_vram_remap();
            return;
        }
    }
}

u8 *bw_vram_slot(int kind, u32 offset, u32 *contig_out)
{
    u8 *piece;

    if (contig_out != NULL) {
        *contig_out = 0;
    }
    if (kind < 0 || kind >= BW_VSLOT_KINDS || offset >= k_slot_size[kind]) {
        return NULL;
    }
    vram_current();
    piece = g_vslot[kind][offset >> VSLOT_SHIFT];
    if (piece == NULL) {
        return NULL;
    }
    if (contig_out != NULL) {
        /* Pieces of one bank are adjacent host bytes; count how far on. */
        u32 n = VSLOT_SIZE - (offset & (VSLOT_SIZE - 1u));
        u32 p = (offset >> VSLOT_SHIFT) + 1u;
        while (p < (k_slot_size[kind] >> VSLOT_SHIFT)
               && g_vslot[kind][p] == piece + (p - (offset >> VSLOT_SHIFT)) * VSLOT_SIZE) {
            n += VSLOT_SIZE;
            p++;
        }
        *contig_out = n;
    }
    return piece + (offset & (VSLOT_SIZE - 1u));
}

static void vramcnt_hook(u32 addr, u32 old_value, u32 new_value)
{
    (void)addr;
    (void)old_value;
    (void)new_value;
    bw_vram_remap();
}

/* ------------------------------------------------------------------ */
/* Translation                                                         */
/* ------------------------------------------------------------------ */

static u8 *main_ptr(u32 addr)
{
    u32 off = addr - BW_DS_MAIN_BASE;
    u32 piece = off >> MAIN_PIECE_SHIFT;
    u32 last = piece + 1u < MAIN_PIECES ? piece + 1u : piece;
    u32 p;

    if (g_identity == 1) {
        return g_main + off;        /* committed whole at reset */
    }
    for (p = piece; p <= last; p++) {
        if (!g_main_committed[p]) {
            if (bw_vm_commit(g_main + (size_t)p * MAIN_PIECE_SIZE,
                             MAIN_PIECE_SIZE) != 0) {
                return NULL;
            }
            g_main_committed[p] = 1;
        }
    }
    return g_main + off;
}

void *bw_ds_try_ptr(u32 addr)
{
    const struct RegEntry *e;
    long idx;

    if (!g_ready) {
        return NULL;
    }
    e = reg_find(addr);
    if (e != NULL) {
        return e->host + (addr - e->addr);
    }

    switch (addr >> 24) {
    case BW_DS_MAIN_BASE >> 24:
        return main_ptr(addr);
    case BW_DS_WRAM_BASE >> 24:
        return &g_wram[addr & (BW_DS_WRAM_SIZE - 1u)];
    case BW_DS_IO_BASE >> 24:
        idx = io_index(addr, 1);
        if (idx < 0) {
            return NULL;
        }
        if (g_access_count > 0) {
            io_access(addr);
        }
        return (void *)io_byte((u32)idx);
    case BW_DS_PAL_BASE >> 24:
        return &g_pal[addr & (BW_DS_PAL_SIZE - 1u)];
    case BW_DS_VRAM_BASE >> 24: {
        u8 *page;
        vram_current();
        page = g_vpage[(addr - BW_DS_VRAM_BASE) >> VRAM_PAGE_SHIFT];
        return page != NULL ? page + (addr & (VRAM_PAGE_SIZE - 1u)) : NULL;
    }
    case BW_DS_OAM_BASE >> 24:
        return &g_oam[addr & (BW_DS_OAM_SIZE - 1u)];
    default:
        return NULL;
    }
}

/*
 * tools/dsaddr.py declares this function in every rewritten matched file
 * as `void *bw_ds_ptr(unsigned int);`, because a matched translation unit
 * has its own u32 typedef and cannot include dsmem.h.  The two spellings
 * must name one type.  Repeating the tool's declaration here, after
 * dsmem.h's, makes every build of the port check it: a redeclaration with
 * an incompatible parameter type is a constraint violation, so a target
 * where uint32_t is not unsigned int fails to compile this file instead of
 * miscalling the function.  The size check states the ILP32 assumption
 * the declaration relies on in words a diagnostic can show.
 */
void *bw_ds_ptr(unsigned int addr);
typedef char bw_u32_is_unsigned_int[(sizeof(u32) == sizeof(unsigned int)
                                     && (u32)-1 == (unsigned int)-1) ? 1 : -1];

void *bw_ds_ptr(u32 addr)
{
    void *p;

    if (!g_ready) {
        bw_panic("bw_ds_ptr", addr, "bw_ds_reset has not been called");
    }
    p = bw_ds_try_ptr(addr);
    if (p == NULL && addr >> 24 == BW_DS_VRAM_BASE >> 24) {
        /* VRAM no bank is mapped at: the DS drops the writes and reads
         * nothing back.  The battle copies its sub OBJ extended palettes
         * to LCDC 0x068A0000 while bank I serves as sub OBJ VRAM, so the
         * copy goes nowhere on the console.  The port sends such an access
         * to a scratch block, cleared each time, so a copy that size never
         * leaves it. */
        static u8 *sink;
        static int told;
        const u32 SINK = 0x40000u;

        if (sink == NULL) {
            sink = (u8 *)calloc(2u, SINK);
        }
        if (sink != NULL) {
            if (!told) {
                told = 1;
                fprintf(stderr, "bw_ds_ptr: VRAM 0x%08lX has no bank mapped; "
                        "accesses there go nowhere, as on the DS\n",
                        (unsigned long)addr);
            }
            memset(sink, 0, 2u * SINK);
            return sink + (addr & (SINK - 1u));
        }
    }
    if (p == NULL) {
        const char *why = "address outside every backed DS region";
        if (addr >> 24 == BW_DS_VRAM_BASE >> 24) {
            why = "no VRAM bank is mapped there by VRAMCNT";
        } else if (addr >> 24 == BW_DS_MAIN_BASE >> 24) {
            why = "the host refused to commit main RAM";
        } else if (addr >> 24 == BW_DS_IO_BASE >> 24) {
            why = "I/O address outside the port's register file";
        }
        bw_panic("bw_ds_ptr", addr, why);
    }
    return p;
}

/* Offset of h in [base, base + size), or -1.  Through uintptr_t, because
 * relational comparison of pointers into different objects is undefined. */
static long host_offset(uintptr_t h, const void *base, size_t size)
{
    uintptr_t b = (uintptr_t)base;

    if (base != NULL && h >= b && h - b < size) {
        return (long)(h - b);
    }
    return -1;
}

u32 bw_ds_wire_addr(uintptr_t value)
{
    u32 address;
    if (value <= UINT32_MAX)
        return (u32)value;
    address = bw_ds_addr_of((const void *)value);
    if (!address)
        bw_panic("bw_ds_wire_addr", 0, "native pointer has no DS address");
    return address;
}

u32 bw_ds_addr_of(const void *host)
{
    uintptr_t h = (uintptr_t)host;
    long off;
    size_t i;

    if (host == NULL || !g_ready) {
        return 0;
    }
    /* Linear: the reverse direction is for the rare place the game keeps an
     * address as a number, not for every access. */
    for (i = 0; i < g_reg_count; i++) {
        off = host_offset(h, g_reg[i].host, g_reg[i].size);
        if (off >= 0) {
            return g_reg[i].addr + (u32)off;
        }
    }
    if ((off = host_offset(h, g_main, BW_DS_MAIN_SIZE)) >= 0) {
        return BW_DS_MAIN_BASE + (u32)off;
    }
    if ((off = host_offset(h, g_wram, BW_DS_WRAM_SIZE)) >= 0) {
        return BW_DS_WRAM_BASE + (u32)off;
    }
    if ((off = host_offset(h, g_io_lo, BW_DS_IO_SIZE)) >= 0) {
        return BW_DS_IO_BASE + (u32)off;
    }
    if ((off = host_offset(h, g_io_hi, IO_HI_SIZE)) >= 0) {
        return IO_HI_BASE + (u32)off;
    }
    if ((off = host_offset(h, g_pal, BW_DS_PAL_SIZE)) >= 0) {
        return BW_DS_PAL_BASE + (u32)off;
    }
    if ((off = host_offset(h, g_oam, BW_DS_OAM_SIZE)) >= 0) {
        return BW_DS_OAM_BASE + (u32)off;
    }
    if (host_offset(h, g_vram, sizeof(g_vram)) >= 0
        || (g_vram_window && h >= BW_DS_VRAM_BASE
            && h - BW_DS_VRAM_BASE < BW_DS_VRAM_SIZE)) {
        /* The first CPU address the bank byte is visible at: the engine
         * windows come before their mirrors and before LCDC. */
        u32 p;
        vram_current();
        for (p = 0; p < VRAM_PAGES; p++) {
            off = host_offset(h, g_vpage[p], VRAM_PAGE_SIZE);
            if (off >= 0) {
                return BW_DS_VRAM_BASE + (p << VRAM_PAGE_SHIFT) + (u32)off;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

/*
 * Identity mapping (docs/boot.md, "Identity mapping"): with 32-bit host
 * pointers, try to put each region at its own DS address in the host
 * process.  Main RAM decides: if 0x02000000-0x02FFFFFF cannot be had, the
 * whole address space stays on movable blocks (the fallback, and the only
 * mode on LP64 hosts, where a 32-bit word cannot hold a host pointer
 * anyway).  The other windows follow one by one; a window the host refuses
 * keeps its static array.  Each window is reserved whole (16 MiB) and only
 * its backed bytes are committed, so a stray access to a mirror or to VRAM
 * (whose banks move with VRAMCNT and stay behind bw_ds_ptr's page table)
 * faults at the DS address instead of reaching another allocation.
 * BW_DS_IDENTITY=0 in the environment forces the fallback.
 */
#define IDENTITY_WINDOW 0x01000000u

/* The windows, in the order they are reserved; main RAM first. */
static const u32 k_identity_base[] = {
    BW_DS_MAIN_BASE, BW_DS_WRAM_BASE, BW_DS_IO_BASE,
    BW_DS_PAL_BASE, BW_DS_VRAM_BASE, BW_DS_OAM_BASE
};
#define IDENTITY_WINDOWS ((int)(sizeof(k_identity_base) / sizeof(k_identity_base[0])))

static u8 *identity_window(u32 base, u32 commit_size)
{
    u8 *p = (u8 *)bw_vm_reserve_at((size_t)base, IDENTITY_WINDOW);

    if (p != NULL && commit_size != 0u && bw_vm_commit(p, commit_size) != 0) {
        return NULL;    /* reserved but unusable: keep the array */
    }
    return p;
}

static int identity_wanted(void)
{
    const char *env = getenv("BW_DS_IDENTITY");

    return sizeof(void *) == 4u && !(env != NULL && env[0] == '0');
}

static void identity_choose(void)
{
    u8 *p;

    g_identity = 0;
    if (!identity_wanted()) {
        return;
    }
    g_main = (u8 *)bw_vm_reserve_at((size_t)BW_DS_MAIN_BASE, BW_DS_MAIN_SIZE);
    if (g_main == NULL) {
        return;
    }
    g_identity = 1;
    if ((p = identity_window(BW_DS_WRAM_BASE, BW_DS_WRAM_SIZE)) != NULL) {
        g_wram = p;
    }
    if ((p = identity_window(BW_DS_IO_BASE, BW_DS_IO_SIZE)) != NULL) {
        g_io_lo = p;
        if (bw_vm_commit(p + (IO_HI_BASE - BW_DS_IO_BASE), IO_HI_SIZE) == 0) {
            g_io_hi = p + (IO_HI_BASE - BW_DS_IO_BASE);
        }
    }
    if ((p = identity_window(BW_DS_PAL_BASE, BW_DS_PAL_SIZE)) != NULL) {
        g_pal = p;
    }
    p = identity_window(BW_DS_VRAM_BASE, 0u);       /* a guard ...      */
    if (p != NULL && g_vram_window_wanted) {         /* ... or the banks */
        int i;
        g_vram_window = 1;
        for (i = 0; i < VRAM_WINDOW_PARTS; i++) {
            if (bw_vm_commit((u8 *)(uintptr_t)k_window_base[i], k_window_size[i]) != 0) {
                g_vram_window = 0;
            }
        }
    }
    if ((p = identity_window(BW_DS_OAM_BASE, BW_DS_OAM_SIZE)) != NULL) {
        g_oam = p;
    }
}

int bw_ds_identity_prepare(int *status)
{
    size_t base[IDENTITY_WINDOWS];
    size_t size[IDENTITY_WINDOWS];
    int i;

    g_vram_window_wanted = 1;
    if (g_identity < 0) {
        identity_choose();
    }
    if (g_identity == 1 || !identity_wanted()) {
        return 0;
    }
    for (i = 0; i < IDENTITY_WINDOWS; i++) {
        base[i] = k_identity_base[i];
        size[i] = IDENTITY_WINDOW;
    }
    return bw_vm_relaunch_reserved(base, size, IDENTITY_WINDOWS, status);
}

int bw_ds_identity(void)
{
    return g_identity == 1;
}

void bw_ds_reset(void)
{
    int b;

    if (g_identity < 0) {
        identity_choose();
    }
    if (g_main == NULL) {
        g_main = (u8 *)bw_vm_reserve(BW_DS_MAIN_SIZE);
        if (g_main == NULL) {
            bw_panic("bw_ds_reset", BW_DS_MAIN_BASE,
                     "cannot reserve 16 MiB for main RAM");
        }
    } else {
        bw_vm_discard(g_main, BW_DS_MAIN_SIZE);
    }
    memset(g_main_committed, 0, sizeof(g_main_committed));
    if (g_identity == 1) {
        /* Every byte is reachable without bw_ds_ptr (a DS address read out
         * of the data is dereferenced as it is), so all of it is backed. */
        if (bw_vm_commit(g_main, BW_DS_MAIN_SIZE) != 0) {
            bw_panic("bw_ds_reset", BW_DS_MAIN_BASE,
                     "cannot commit the identity-mapped main RAM");
        }
        memset(g_main_committed, 1, sizeof(g_main_committed));
    }

    memset(g_wram, 0, BW_DS_WRAM_SIZE);
    memset(g_pal, 0, BW_DS_PAL_SIZE);
    memset(g_oam, 0, BW_DS_OAM_SIZE);
    memset(g_vram, 0, sizeof(g_vram));
    for (b = 0; b < BW_VRAM_BANKS; b++) {
        g_bank_home[b] = &g_vram[k_bank_off[b]];
    }
    if (g_vram_window) {
        int i;
        for (i = 0; i < VRAM_WINDOW_PARTS; i++) {
            memset((u8 *)(uintptr_t)k_window_base[i], 0, k_window_size[i]);
        }
    }
    memset(g_io_lo, 0, BW_DS_IO_SIZE);
    memset(g_io_hi, 0, IO_HI_SIZE);
    /* KEYINPUT is active low: every button released. */
    bw_wr16(&g_io_lo[KEYINPUT_OFF], 0x03FFu);
    io_snapshot();

    g_hook_count = 0;
    g_access_count = 0;
    g_reg_count = 0;
    g_ready = 1;

    for (b = 0; b < BW_VRAM_BANKS; b++) {
        bw_io_hook(BW_DS_IO_BASE + k_bank_reg[b], 1, vramcnt_hook);
    }
    bw_vram_remap();

    bw_game_globals_register();
}
