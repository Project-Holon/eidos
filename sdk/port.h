/*
 * sdk/port.h -- the port's own API surface: the data root, the archive
 * handles, and the panic path the stubs use.
 *
 * Nothing in this header is an SDK name.  The SDK-named shims live one file
 * per SDK module (fs.c, mi.c, os_alloc.c); this is the glue that tells them
 * where the player's extracted assets are.
 */

#ifndef BW_PORT_PORT_H
#define BW_PORT_PORT_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Data root, mod overlay, and path resolution                         */
/* ------------------------------------------------------------------ */

/*
 * The directory tools/ndsrom.py's `extract` wrote, i.e. the one holding
 * `a/0/0/2`.  Set once at start-up; BW_DATA_ROOT from the environment wins
 * over the compiled-in default, and an explicit call wins over both.
 */
void        bw_set_data_root(const char *path);
const char *bw_data_root(void);
/*
 * A number that changes each time the data root is set.  A run can go from
 * one game's data to the other's without ending (the boot's version switch,
 * demo/eidos_boot.c), so whatever keeps something read from the data root
 * for longer than one use (a table, a decoded picture, an open archive, a
 * pointer bw_arm9_rodata gave) keeps this number with it and reads again
 * when the number has changed.
 */
u32         bw_data_generation(void);

/*
 * Mods laid over the data root, in a load order where a later mod wins
 * (sdk/vfs.h has the list API, the manifest and the member-level rules).  At
 * file granularity a NitroFS-relative path resolves to `<mod_root>/<rel>`
 * for the latest mod holding a regular file there and to `<data_root>/<rel>`
 * otherwise, so a mod ships whole replacement files at the same layout as
 * the extract tree.  A mod may instead hold a directory at an archive's path
 * whose files `<index>[.<ext>]` replace or add single members; the FS shim
 * serves the merged archive (sdk/vfs_narc.c).  Off by default; BW_MOD_ROOT
 * in the environment turns it on (several roots separated by ';'), and an
 * explicit call wins over that.
 *
 * bw_set_mod_root replaces the whole list with the ';'-separated roots in
 * `path`; NULL or "" clears it.  bw_mod_root returns the root that wins
 * most (the last in load order), or "" with no mods.
 */
void        bw_set_mod_root(const char *path);
const char *bw_mod_root(void);

/*
 * Join the data root and a NitroFS-relative path such as "a/0/1/6" into
 * `out`, consulting the mods first (latest first, regular files only).  This
 * is the file-granular answer: an archive with member overrides is read
 * through bw_fs_open_rel, which applies them.  Returns 0 on success, -1 if it
 * would not fit.  The path written is the resolved one even when the file
 * is absent; `bw_data_exists` is the reader that tells the two apart.
 */
int bw_data_path(char *out, size_t out_size, const char *rel);

/* 1 if the resolved path (overlay or base) is a readable file, else 0. */
int bw_data_exists(const char *rel);

/*
 * The decompressed ARM9 image, extract/arm9_dec.bin, and the address it is
 * linked at.  Some of the matched code reads constant tables that live inside
 * the ARM9 binary rather than in a NitroFS file (the nature multiplier table
 * at 0x0209E324 is the one this lane needs).  Those are game data: they are
 * read out of the player's own extracted image at run time and never appear
 * in this repository.
 */
#define BW_ARM9_LOAD_ADDRESS 0x02004000u

/*
 * Map `size` bytes at ARM9 address `address` out of extract/arm9_dec.bin.
 * The image is loaded once and cached.  Returns NULL if the file is missing
 * or the range is out of bounds; the pointer stays valid for the process.
 */
const void *bw_arm9_rodata(u32 address, u32 size);

/*
 * Replace the ARM9 image with a copy of `size` bytes at `image` (loaded at
 * BW_ARM9_LOAD_ADDRESS), instead of reading extract/arm9_dec.bin.  For a
 * caller that already holds the image (the first-run extractor, a test with
 * a synthetic one).  NULL or 0 clears it.  Pointers bw_arm9_rodata returned
 * before the call are invalid after it.
 */
void bw_arm9_set_image(const void *image, u32 size);

/*
 * Forget the ARM9 image that was read: the next use reads
 * extract/arm9_dec.bin again, from the data root in force then, and sets
 * the cartridge's language and layout again (sdk/langmap.h).  For a data
 * root that changes while the program runs (the other version of the
 * game).  Pointers bw_arm9_rodata returned before the call are invalid
 * after it.
 */
void bw_arm9_forget(void);

/*
 * Install the game's archive name table, which is what the boot's
 * func_02048CEC(0x020A6C58, 0xEB) does: `count` ARM9 addresses of NitroFS
 * paths at ARM9 address `address`.  After it, the matched archive readers
 * that take an archive id (func_02048DDC and everything above it) open
 * files through the file layer, mod overlay included.  0, or -1 if the range
 * is not inside the ARM9 image.
 */
int bw_arc_install_table(u32 address, u32 count);
#define BW_ARC_TABLE_ADDRESS 0x020A6C58u
#define BW_ARC_TABLE_COUNT   0xEBu

/* ------------------------------------------------------------------ */
/* Archives                                                            */
/* ------------------------------------------------------------------ */

/*
 * A NARC opened for streaming, laid out so that the matched readers
 * (func_02049578, func_020495F4, func_0204944C in src/matched/) see exactly
 * the object they were compiled against:
 *
 *     struct ArcFile { u8 file[0x48]; u32 imageBase; u16 fatOffset; u16 count; };
 *
 * The first 0x48 bytes are the SDK's FSFile, which our fs.c shim owns; the
 * three fields after it are the game's own and the matched code reads them
 * directly.  Keeping the prefix at exactly 0x48 bytes is a hard requirement
 * of that layout and is asserted at start-up.
 */
#define BW_FSFILE_SIZE 0x48

typedef struct BwArcFile {
    u8  file[BW_FSFILE_SIZE];
    u32 imageBase;   /* byte offset of the GMIF chunk within the file  */
    u16 fatOffset;   /* byte offset of the BTAF chunk within the file  */
    u16 count;       /* number of sub-files                            */
} BwArcFile;

/*
 * The FS shim's own open/size, on a host path.  `file` points at the 0x48
 * bytes the SDK's FSFile occupies, i.e. at a BwArcFile's `file` member.
 */
int bw_fs_open(void *file, const char *host_path);
u32 bw_fs_size(void *file);

/*
 * Open NitroFS-relative path `rel` through the file layer: the merged
 * archive when a mod overrides members of it, else the file bw_data_path
 * names.  Every open the FS shim performs for a NitroFS path goes here.
 */
int bw_fs_open_rel(void *file, const char *rel);

/*
 * Open a NARC under the data root ("a/0/1/6"), through the mods and their
 * member overrides, and fill in imageBase, fatOffset and count from its
 * chunk table.  Returns 0 on success.
 */
int  bw_arc_open(BwArcFile *arc, const char *rel_path);
void bw_arc_close(BwArcFile *arc);

/*
 * Read one whole sub-file into a caller-supplied buffer.  This is the port's
 * own convenience reader for test fixtures and for asset loading that does
 * not go through a matched game path; the matched readers are used wherever
 * one exists.  Returns the number of bytes read, or -1.
 */
long bw_arc_read_member(BwArcFile *arc, u32 member, void *dst, u32 dst_size);

/* Size of one sub-file without reading it, or -1. */
long bw_arc_member_size(BwArcFile *arc, u32 member);

/* ------------------------------------------------------------------ */
/* Well-known archive handles the matched code reaches through globals  */
/* ------------------------------------------------------------------ */

/*
 * `g_0214652C` is the ARM9 global the matched personal-data reader
 * (src/matched/unit_0201ADA4.c, src/matched/unit_0201AFD4.c) streams the
 * 0x3C-byte personal record through.  On the DS some not-yet-matched
 * start-up code fills it in; here the port opens a/0/1/6 and publishes it.
 */
int  bw_open_personal_archive(void);
void bw_close_personal_archive(void);

/*
 * `func_02019B04_Table` is the 25 x 5 nature multiplier table at ARM9
 * 0x0209E324 that src/matched/unit_02019B04.c indexes.  It is game data and
 * is copied out of the player's own extract/arm9_dec.bin at run time.
 * Returns 0 on success, -1 if the ARM9 image is not readable.
 */
int bw_load_nature_table(void);

/*
 * `g_020A7820` is the signed watched-heap id src/matched/sub_02030A00.c
 * gates the allocation audit on, an ARM9 data word at 0x020A7820.  Loaded
 * out of the player's own extract/arm9_dec.bin; the port's own definition is
 * -1 ("watch nothing") so a run with no image behaves like the retail one.
 * Returns 0 on success, -1 if the ARM9 image is not readable.
 */
int bw_load_watch_id(void);

/* ------------------------------------------------------------------ */
/* The host's aligned blocks                                           */
/* ------------------------------------------------------------------ */

/*
 * malloc with an alignment, and its release.  The malloc base is stored in
 * the word immediately before the pointer handed out, so a block may be
 * freed knowing only the aligned pointer.  This is what sdk/os_alloc.c's SDK
 * pair OS_AllocFromHeap / OS_FreeToHeap goes through.
 */
void *bw_aligned_alloc(u32 size, u32 align);
void  bw_aligned_free(void *p);

/*
 * One block of the expanded heap the game's own allocator bottoms out in,
 * and its release.  `size` is the whole block including the 0x1c-byte header
 * src/matched/sub_02030098.c writes in front of the payload, and what comes
 * back is the block, not the payload; `align` applies to the *payload*, i.e.
 * to `block + 0x1c`, which is the one place the host and the DS have to be
 * told apart.  sdk/os_alloc.c has the reasoning.
 *
 * Callers, on an LP64 host only: stubs/reimpl_02060A08.c and
 * stubs/reimpl_02060ABC.c, the two halves src/matched/func_02060CA0.c
 * dispatches to, and stubs/reimpl_02030164.c, the raw free under
 * src/matched/sub_020307B0.c.  Under ILP32 the matched expanded heap takes
 * blocks from its own arena instead.
 */
void *bw_block_alloc(u32 size, u32 align);
void  bw_block_free(void *block);

/* ------------------------------------------------------------------ */
/* The heap environment                                                */
/* ------------------------------------------------------------------ */

/*
 * `g_EnvModule` at 0x02146954 is the game's heap manager, and every matched
 * function in the heap layer reads it: the heap count at +8 bounds the id,
 * the byte table at +4 maps a public heap id to a record index, the record
 * array at +0 holds the arena handle the raw allocator allocates out of and
 * the live-allocation count it increments, and the halfword at +0xe is the
 * result code each of them writes (1 = id out of range, 2 = no such heap,
 * 3 = exhausted, 0 = success).  src/matched/sub_02030098.c's header is the
 * authority on all of it.
 *
 * On the DS the heap constructor at 0x0202FC48 fills the manager in, and
 * where EIDOS_HEAP_MATCHED is on (the 32-bit reference configuration) it
 * does here too: src/matched/sub_0202FC48.c builds `heaps` NitroSystem
 * expanded heaps inside the port's arena (sdk/os_arena.c) and every
 * allocation walks a real free list.  On an LP64 host the port fills the
 * fields in itself and blocks come from the host allocator
 * (sdk/game_globals.c has both).
 *
 * bw_heap_init makes `heaps` heap ids valid (BW_HEAP_COUNT if 0 or out of
 * range), each BW_HEAP_DEFAULT_SIZE bytes.  bw_heap_init_sizes takes one
 * size per heap and the id-table length, which is the shape of the game's
 * own call (src/matched/sub_0200545C.c passes 2 heaps and 0xAD ids).  Both
 * return 0, or -1 if the constructor failed (bw_heap_error() then holds the
 * index of the heap that did not fit).  Calling either again starts over.
 */
#define BW_HEAP_COUNT        16u
#define BW_HEAP_MAX          64u
#define BW_HEAP_DEFAULT_SIZE 0x40000u

int  bw_heap_init(u32 heaps);
int  bw_heap_init_sizes(const u32 *sizes, u32 heaps, u32 id_count);

/* A heap carved out of another one: src/matched/sub_0202FDC4.c, which the
 * game's boot calls as func_0203064C(1, 7, 0x25000).  `child` may carry bit
 * 15, which takes the region from the parent's tail.  0 on success, -1 on
 * failure with the manager's result code in bw_heap_error().  Only where
 * EIDOS_HEAP_MATCHED is on; elsewhere it always fails. */
int  bw_heap_create_child(u32 parent, u32 child, u32 size);

/* The heap handle behind a public id (the expanded heap's head under
 * EIDOS_HEAP_MATCHED, a token otherwise), or NULL. */
void *bw_heap_handle(u32 heap_id);

/* The manager's result halfword at +0xe, as the last matched call left it. */
u32  bw_heap_error(void);

/* Live allocations on one heap, as the manager counts them.  -1 if the id is
 * out of range or names no heap.  The count is incremented by the matched raw
 * allocator and decremented by the raw free, so a balanced test ends at
 * zero. */
long bw_heap_live_blocks(u32 heap_id);

/* 0x02030524 (sdk/gx.c): bw_heap_error's halfword, as the game reads it. */
u16  sub_02030524(void);

/*
 * The arena the heaps are built in, EIDOS_HEAP_MATCHED only
 * (sdk/os_arena.c).  bw_arena_reset discards the old arena and allocates a
 * fresh one of `size` bytes (0 frees it); bw_heap_init_sizes calls it with
 * exactly what the constructor will take.
 */
int  bw_arena_reset(u32 size);
u32  bw_arena_used(void);
u32  bw_arena_size(void);
int  bw_arena_contains(const void *p);

/*
 * The SDK's arena table in the shared work area (sdk/os_arena.c), the
 * boot's arena: OS_GetArenaHi/Lo and OS_SetArenaHi/Lo by arena id, and
 * OS_AllocFromArenaLo on it (sub_02086788 uses it when no host arena is set
 * up).  32-bit pointers only, like the boot.
 */
void *OS_GetArenaHi(u32 id);
void *OS_GetArenaLo(u32 id);
void  OS_SetArenaHi(u32 id, void *hi);
void  OS_SetArenaLo(u32 id, void *lo);
void *bw_os_alloc_from_arena_lo(u32 id, u32 size, u32 align);

/* The release side of the counter the matched raw allocator increments.
 * Called by stubs/reimpl_02030164.c with the heap id it recovered from the
 * block header. */
void bw_heap_note_release(u32 heap_id);

/* ------------------------------------------------------------------ */
/* Panic                                                               */
/* ------------------------------------------------------------------ */

/*
 * Called by every stub in stubs/ and by the assert sink.  Prints the
 * address and the reason on stderr and aborts; a port that reaches an
 * unimplemented game function has a hole in its closure, and failing loudly
 * is the only way the closure script and the linker keep agreeing.
 */
void bw_panic(const char *what, u32 address, const char *detail);

/*
 * A stub reached: a function the build does not define yet (a boot stub, an
 * overlay's ARM9 stub, an unmatched overlay function, an armdata trap).
 * Normally bw_panic.  With keep-going on (EIDOS_KEEP_GOING=1 in the
 * environment, or bw_set_keep_going(1); eidos_boot's --keep-going), it
 * reports the stub once per address on stderr ("STUB (kept going): ...")
 * and returns, so one diagnostic run lists every stub on its way instead of
 * stopping at the first.  The stub's caller then sees an undefined result:
 * a run in this mode finds holes, it does not play the game correctly.
 */
void bw_stub_reached(const char *what, u32 address, const char *detail);
void bw_set_keep_going(int on);
int  bw_keep_going(void);

/* ------------------------------------------------------------------ */
/* Power-on                                                            */
/* ------------------------------------------------------------------ */

/*
 * Put the whole emulated machine back to power-on, in the one order that
 * works (sdk/port_reset.c says why each step is where it is):
 *
 *   bw_ds_reset    the address space; drops every I/O hook and registration
 *   bw_os_reset    threads, the interrupt latch, IF's write hook
 *   bw_pxi_reset   the FIFOs, the IPC register hooks, the ARM7 powered off
 *
 * then joins the layers: the PXI's interrupt requests go to the OS layer's
 * latch (bw_os_raise_irq) and the OS layer's interrupt restores step the
 * ARM7 (bw_arm7_step).  Every start-up of the game, demo/eidos_boot.c and
 * the tests that run game code, calls this rather than the three resets.
 * Call it on the thread that will run the game, before its OS_Init.
 */
void bw_port_reset(void);

/*
 * The port's restart: the emulated machine back to power-on, with the
 * calling thread left alone.  The old game stops (bw_os_stop_threads,
 * sdk/os.h), the machine is reset (bw_port_reset), and the caller carries
 * on to start the game again.
 *
 * It is callable from the frame thread, the one that runs the port's own
 * code while it holds the run token: the game's threads are stopped before
 * the memory they run in is cleared, and the frame thread is left running.
 * A caller that is not the token holder cannot be told apart from it, so
 * the port's own uses record the request and let the frame loop carry it
 * out.
 */
void bw_port_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PORT_H */
