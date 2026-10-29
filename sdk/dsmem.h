/*
 * sdk/dsmem.h -- the DS address space the matched code's fixed addresses
 * land in.  docs/boot.md, "The DS address space", is the design; this is the
 * contract the rewrite (tools/dsaddr.py), the shims and the renderer share.
 *
 * Every function here takes and returns DS addresses as u32 and host memory
 * as pointers.  Nothing in this header stores a host pointer in a DS-shaped
 * u32 (portability rule 5).
 */

#ifndef BW_PORT_DSMEM_H
#define BW_PORT_DSMEM_H

#include <stddef.h>
#include <stdint.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Region boundaries                                                   */
/* ------------------------------------------------------------------ */

#define BW_DS_MAIN_BASE     0x02000000u
#define BW_DS_MAIN_SIZE     0x01000000u   /* 16 MiB, TWL main RAM        */
#define BW_DS_WRAM_BASE     0x03000000u
#define BW_DS_WRAM_SIZE     0x00040000u   /* shared WRAM, mirrored        */
#define BW_DS_IO_BASE       0x04000000u
#define BW_DS_IO_SIZE       0x00010000u   /* ARM9 + TWL register window   */
#define BW_DS_PAL_BASE      0x05000000u
#define BW_DS_PAL_SIZE      0x00000800u
#define BW_DS_VRAM_BASE     0x06000000u
#define BW_DS_VRAM_SIZE     0x01000000u
#define BW_DS_OAM_BASE      0x07000000u
#define BW_DS_OAM_SIZE      0x00000800u

/* ------------------------------------------------------------------ */
/* Translation                                                         */
/* ------------------------------------------------------------------ */

/*
 * The host address standing for DS address `addr`.  This is what
 * tools/dsaddr.py puts in place of an integer-literal cast:
 *     *(vu16 *)0x04000208   ->   *((vu16 *)bw_ds_ptr(0x04000208u))
 * Never returns NULL: an address outside every backed region panics with
 * the address and the caller's DS-facing description, because a matched
 * store that silently went nowhere is the worst kind of port bug.
 */
void *bw_ds_ptr(u32 addr);

/* As bw_ds_ptr, but returns NULL instead of panicking (for probes). */
void *bw_ds_try_ptr(u32 addr);

/*
 * The reverse, for the few places the game stores an address as a number
 * and later reads through it: the DS address of a host pointer that lies in
 * a backed region or a registered object, or 0 if it does not.
 */
u32 bw_ds_addr_of(const void *host);

/* Encode a pointer or an already numeric offset for a 32-bit DS protocol.
 * A native pointer must name backed or registered memory; never truncate it. */
u32 bw_ds_wire_addr(uintptr_t value);

/*
 * Register a host object as the backing of DS range [addr, addr + size).
 * sdk/game_globals.c and the generated data definitions call this for every
 * ARM9 global they define, so a literal cast to a global's address and an
 * `extern` reference to the same global reach the same bytes.  Later
 * registrations of an overlapping range are a programming error and panic.
 */
void bw_ds_register(u32 addr, void *host, u32 size, const char *name);

/*
 * Withdraw the registration that starts at `addr`; 1 if there was one.
 * For objects whose DS range belongs to them only for a while: an
 * overlay's host objects are registered when it loads and withdrawn when
 * its window passes to another overlay (sdk/overlay_image.c).  The bytes at
 * the address are the flat region's again afterwards; copying the object's
 * contents back first is the caller's choice.
 */
int bw_ds_unregister(u32 addr);

/* 1 if a registered object covers any byte of [addr, addr + size). */
int bw_ds_registered(u32 addr, u32 size);

/*
 * 1 when the address space is identity-mapped: main RAM (and, where the
 * host allowed, WRAM, the register file, palette and OAM) lives at its own
 * DS address in the host process, so a DS address stored in the game's
 * data is the host address of its bytes and bw_ds_ptr(a) == (void *)a
 * outside registered objects and VRAM.  Decided once, at the first
 * bw_ds_reset; 0 means movable blocks (LP64 hosts, a host that refused the
 * range, or BW_DS_IDENTITY=0 in the environment).  docs/boot.md, "Identity
 * mapping".
 */
int bw_ds_identity(void);

/*
 * Non-zero when `addr` is in the VRAM window of an identity-mapped run:
 * the host bytes at that address exist whatever the banks are mapped to
 * now (they hold the bank when one is mapped there).  A name the game
 * keeps for VRAM-resident data (overlay 95 runs from LCDC bank H) binds to
 * that address even while nothing is mapped there.
 */
int bw_ds_vram_window(u32 addr);

/*
 * Called first thing by a program that wants the identity mapping (before
 * it creates a window, a thread or anything else that maps memory): decide
 * the mapping now, and where the host process has already put something in
 * the ranges (a Win32 loader does; sdk/host_vm.c says why), run the
 * program again as a child with them reserved from its start.  Returns 1
 * when it did, with the child's exit status in *status, which the caller
 * returns from main; 0 when this process should go on (identity-mapped, or
 * on the fallback).
 */
int bw_ds_identity_prepare(int *status);

/* ------------------------------------------------------------------ */
/* I/O register file                                                   */
/* ------------------------------------------------------------------ */

/*
 * The register file is plain memory: a matched store through bw_ds_ptr
 * writes it, and the renderer and the shims read it.  Registers with side
 * effects get a hook.  A write hook runs after the matched code's store has
 * landed (the hook sees the new value and may change it, e.g. IF's
 * write-one-to-clear); a read hook runs before the value is returned.
 *
 * Because the rewrite turns a register access into an ordinary host memory
 * access, hooks cannot fire on the access itself.  They fire at the port's
 * synchronisation points instead: bw_io_sync() is called by every shim that
 * hands control to or from the game (end of frame, interrupt delivery,
 * thread switch, any SDK shim that reads hardware state), and it runs the
 * write hook of every register whose bytes changed since the last sync.
 * The registers that need an immediate answer on read (VCOUNT, KEYINPUT,
 * the IPC FIFO status) are kept current by the shims that change them.
 */
typedef void (*BwIoWriteHook)(u32 addr, u32 old_value, u32 new_value);

/* Byte offset into the register file; addr must be in the I/O window.     */
volatile u8 *bw_io(u32 addr);

/* Typed accessors for port code (matched code goes through bw_ds_ptr).    */
u16  bw_io_read16(u32 addr);
u32  bw_io_read32(u32 addr);
void bw_io_write16(u32 addr, u16 v);   /* runs the hook immediately        */
void bw_io_write32(u32 addr, u32 v);   /* runs the hook immediately        */

/* `width` is 1, 2 or 4.  One hook per register address.                   */
void bw_io_hook(u32 addr, u32 width, BwIoWriteHook hook);

/* Compare the register file with its last-synced copy, run changed hooks. */
void bw_io_sync(void);

/* ------------------------------------------------------------------ */
/* VRAM banks                                                          */
/* ------------------------------------------------------------------ */

/*
 * The nine banks, A..I, as host memory.  VRAMCNT_A..I (0x04000240..0x04000249)
 * decide where each is visible in the 0x06xxxxxx window; bw_ds_ptr resolves
 * a VRAM address through the current mapping, and the renderer reads banks
 * through the same mapping (BG, OBJ, extended palettes, texture slots).
 */
enum { BW_VRAM_A, BW_VRAM_B, BW_VRAM_C, BW_VRAM_D, BW_VRAM_E,
       BW_VRAM_F, BW_VRAM_G, BW_VRAM_H, BW_VRAM_I, BW_VRAM_BANKS };

u8  *bw_vram_bank(int bank, u32 *size_out);
/* Re-read every VRAMCNT and rebuild the mapping (the VRAMCNT write hook). */
void bw_vram_remap(void);

/*
 * The VRAM spaces the CPU cannot see, by GBATEK's names: the texture image
 * and texture palette slots, the four extended-palette areas, the ARM7's
 * two slots.  VRAMCNT maps banks into them exactly as it maps banks into
 * the 0x06xxxxxx window, and the renderer reads them through that mapping.
 * Sizes are the whole space; every space is resolved in 8 KiB pieces, the
 * smallest unit VRAMCNT maps into any of them (the OBJ extended palette).
 */
enum {
    BW_VSLOT_TEX,          /* texture image, slots 0-3, 512 KiB           */
    BW_VSLOT_TEXPAL,       /* texture palette, slots 0-5, 96 KiB          */
    BW_VSLOT_BGEXT_MAIN,   /* main BG extended palette, slots 0-3, 32 KiB */
    BW_VSLOT_OBJEXT_MAIN,  /* main OBJ extended palette, 8 KiB            */
    BW_VSLOT_BGEXT_SUB,    /* sub BG extended palette, slots 0-3, 32 KiB  */
    BW_VSLOT_OBJEXT_SUB,   /* sub OBJ extended palette, 8 KiB             */
    BW_VSLOT_ARM7,         /* the ARM7's VRAM, two 128 KiB slots          */
    BW_VSLOT_KINDS
};

/*
 * The host byte at `offset` into space `kind`, or NULL if no bank is mapped
 * there.  `contig_out` (may be NULL) receives how many bytes from there on
 * are the same bank, i.e. how far the caller may index from the pointer.
 */
u8 *bw_vram_slot(int kind, u32 offset, u32 *contig_out);

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

/*
 * Reset every region and the register file to power-on state: main RAM
 * discarded, the register file cleared (KEYINPUT reads all released), every
 * I/O hook and every registration dropped, the VRAMCNT hooks reinstalled
 * (the only hooks this file owns; IF's belongs to the OS layer), and
 * bw_game_globals_register() called.  A port start-up calls bw_port_reset
 * (sdk/port.h) rather than this alone, because the OS layer and the PXI
 * reinstall their hooks after it.
 */
void bw_ds_reset(void);

/*
 * Defined by sdk/game_globals.c: bw_ds_register every ARM9 global the port
 * defines.  bw_ds_reset clears the registry and calls this, so the bindings
 * are part of power-on state; any other registration has to be repeated
 * after a reset.
 */
void bw_game_globals_register(void);

/*
 * Access hooks: a notification on every *translation* of an I/O address.
 *
 * A write hook (dsmem.h) sees a register's value change at the next sync,
 * which is enough for a latch like VRAMCNT and useless for a port: the
 * geometry FIFO at 0x04000400 and the direct command ports behind it take a
 * stream of stores to one address, and plain memory keeps only the last.
 * What the rewrite does give is one bw_ds_ptr call per access, because
 * tools/dsaddr.py turns each literal cast into its own call, so the call is
 * where a port can see each store coming.  `fn(addr, 0)` runs inside
 * bw_ds_ptr / bw_ds_try_ptr for every address in [lo, hi), before the
 * pointer is returned (so before the matched code's store through it lands);
 * `fn(0, 1)` runs at the top of every bw_io_sync, so whatever the last
 * access left pending is drained at the synchronisation points.  At most
 * four ranges, never overlapping; bw_ds_reset clears them like the write
 * hooks.  engine/g3d/hw3d.c is the user and documents the protocol.
 */
typedef void (*BwIoAccessHook)(u32 addr, int sync);

void bw_io_access_hook(u32 lo, u32 hi, BwIoAccessHook fn);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_DSMEM_H */
