/*
 * sdk/vfs.h -- the file layer's mod list and member-level NARC overrides.
 *
 * sdk/port.h declares the two roots and the path resolution every loader
 * uses (bw_set_data_root, bw_set_mod_root, bw_data_path); this header is the
 * rest of the file layer: several mods in a load order, and the merged view
 * of an archive some of whose members a mod replaces or adds.  docs/files.md,
 * "The file layer", is the user-facing description of the mod tree.
 *
 * The model, in one paragraph.  There is one base layer (the data root) and
 * zero or more mod roots in a load order; a later mod wins over an earlier
 * one.  A NitroFS path `rel` resolves, at file granularity, to the latest
 * layer holding a regular file at `rel` (bw_data_path).  On top of that file,
 * every later mod that holds a *directory* at `rel` contributes members: a
 * file `<mod>/<rel>/<index>[.<ext>]` replaces member `index`, or adds it if
 * the archive is shorter, and later mods win over earlier ones for the same
 * index.  The result is synthesised as a whole NARC in memory and every
 * reader (the FS shim, the matched readers, sdk/narc.c) sees only that.
 */

#ifndef BW_PORT_VFS_H
#define BW_PORT_VFS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The mod list                                                        */
/* ------------------------------------------------------------------ */

/*
 * A mod root is a directory laid out like the data root.  It may hold a
 * manifest, `eidos_mod.txt`, of `key = value` lines (`#` starts a comment,
 * unknown keys are ignored so later versions can add some):
 *
 *     name  = Fairy type        # shown by tools; default: the directory name
 *     order = 10                # load order key, a signed integer; default 0
 *
 * Mods load in ascending `order`; mods with equal `order` keep the order
 * they were added in.  A later mod wins.
 *
 * bw_set_mod_root (sdk/port.h) replaces the whole list with the roots in its
 * argument, which may name several separated by ';' (the separator on every
 * host, since a Windows drive letter uses ':').  BW_MOD_ROOT is parsed the
 * same way the first time the list is consulted, unless a call came first.
 * bw_add_mod_root appends one root and re-sorts.  Every change drops the
 * cached merged archives (bw_vfs_flush).
 *
 * The order above is the mod author's: it is the manifest's `order` key.
 * The player may want another one, because the order the mods load in is
 * the order their overrides and their rules take effect.  bw_mod_set_order
 * is the seam for that: "this list, in this order".
 */
void        bw_clear_mods(void);
int         bw_add_mod_root(const char *path);   /* 0, or -1 (empty, no memory, made for the other game) */

/*
 * The game a mod is made for: the manifest's `game` line, `black` or
 * `white`.  No line: both.  A game code (IRBO, IRAF) is read as its game by
 * its third letter, so a mod made with an English cartridge works on a
 * French one.  A mod made for the game that is not loaded is not added to
 * the list (bw_add_mod_root, bw_mod_set_order, bw_set_mod_root): its files
 * and code are the other game's.  One line of the log says so.
 */
#define BW_MOD_GAME_BLACK 1
#define BW_MOD_GAME_WHITE 2
int         bw_mod_games_of(const char *value, size_t n);  /* the bits a `game` value names; 0: neither */
int         bw_mod_games_at(const char *root);             /* ... the mod in this folder says */
int         bw_mod_game_loaded(void);                      /* the data root's game; 0: not known */
/* A mod's `eidos` line, the oldest eidos it works on (X.Y.Z): a mod that asks
 * for a newer one than this is not loaded. */
int         bw_mod_eidos_too_old(const char *need);        /* 1: `need` is newer than this eidos */
int         bw_mod_needs_eidos_at(const char *root, char *out, size_t size);   /* 1: the mod in `root` is, with its value */
const char *bw_mod_eidos_version(void);                    /* this eidos's release version ("0.2.8") */
u32         bw_mod_count(void);
const char *bw_mod_root_at(u32 i);   /* load order: 0 loads first, wins least */
const char *bw_mod_name_at(u32 i);
int         bw_mod_order_at(u32 i);

/*
 * The player's own order: this list, in this order.
 *
 * The list is replaced, not added to, so this is the way the roots are
 * rebuilt at the menu (docs/mod-state.md, section 2, constraint 1): the
 * caller hands over the whole list in the order the player chose.  The
 * manifest's `order` key does not decide anything while the player's order
 * is in force, and a root added later (bw_add_mod_root) goes at the end of
 * the list instead of being sorted in.  bw_clear_mods drops the player's
 * order with the list, and bw_set_mod_root (sdk/port.h) puts the port's own
 * list back, which the manifest's key decides again.
 *
 * `roots` holds `count` paths, in load order: 0 loads first and wins
 * least.  A NULL or empty path is skipped.  0 on success, -1 when a root
 * could not be added (the list holds the ones that could).
 */
int         bw_mod_set_order(const char *const *roots, u32 count);

/*
 * The layer `rel` resolves to at file granularity: -1 for the data root,
 * 0..bw_mod_count()-1 for a mod, or BW_LAYER_NONE when no layer holds a
 * regular file there.  bw_data_path is this plus a join.
 */
#define BW_LAYER_NONE (-2)
int bw_vfs_file_layer(const char *rel);

/* ------------------------------------------------------------------ */
/* Member-level NARC overrides                                         */
/* ------------------------------------------------------------------ */

/*
 * The NARC format's limit: BTAF's count is a u16 and the matched readers
 * keep it in a u16, so an archive holds at most 65535 members (indices
 * 0..65534).  This is the cartridge format's limit, not one the port adds.
 */
#define BW_NARC_MAX_MEMBERS 0xFFFFu

/*
 * Pack `count` members into a NARC laid out the way the game's archives are
 * (header, BTAF, BTNF, GMIF; every member starts on a 4-byte boundary and is
 * padded with 0xFF, the last one included).  `btnf` is the whole BTNF chunk
 * to copy (NULL for the minimal 16-byte nameless one).  `data[i]` may be NULL
 * when `sizes[i]` is 0.  On success *out is a malloc'd image the caller
 * frees.  Returns 0, or -1 (too many members, too large, no memory, a bad
 * BTNF).  Repacking a real archive's members with its own BTNF reproduces it
 * byte for byte.
 */
int bw_narc_pack(const u8 *const *data, const u32 *sizes, u32 count,
                 const u8 *btnf, u32 btnf_size, u8 **out, u32 *out_size);

/*
 * Number of member files the mods contribute to `rel` (counting an index
 * once however many mods supply it), 0 when none do, -1 when a mod
 * directory is malformed (two files naming the same index in one mod).
 */
int bw_vfs_member_overrides(const char *rel);

/*
 * Build the merged archive for `rel` from scratch: the file-granular winner
 * with every later mod's members applied.  Returns 1 and a malloc'd image
 * in *out (the caller frees), 0 when no mod contributes a member (read the
 * file bw_data_path names instead), or -1 on an error, which is also
 * reported on stderr: the winning file is not a NARC, a member file cannot
 * be read, the result would exceed the format.
 */
int bw_vfs_build_merged(const char *rel, u8 **out, u32 *out_size);

/*
 * The cached form the FS shim uses.  Returns 1 with the merged image in
 * *data and *size and a handle to release when the file closes, 0 when there is
 * nothing to merge, -1 on an error.  The image stays valid until released,
 * even across a mod list change; an image nobody holds is rebuilt after one.
 */
int  bw_vfs_acquire(const char *rel, const u8 **data, u32 *size, u32 *handle);
void bw_vfs_release(u32 handle);

/*
 * Forget every cached merged archive (images still held stay valid until
 * released).  The mod list calls it on every change; a tool that rewrites a
 * mod's files while the game runs calls it too.
 */
void bw_vfs_flush(void);

/* The same for one archive path only. */
void bw_vfs_flush_path(const char *rel);

/*
 * Members in memory (mod API v6, docs/mods.md, "Members in memory").  A
 * mod root's memory layer sits where that mod's folder sits in the load
 * order and wins over the folder's member files.  The bytes are copied.
 * A root not in the mod list is kept but applies to nothing.
 *
 *   bw_vfs_mem_set    0, or -1 (bad arguments, no memory)
 *   bw_vfs_mem_clear  how many members it removed; NULL root or rel, or
 *                     BW_VFS_MEM_ALL as the index, match every one
 *   bw_vfs_mem_bytes  the bytes a root holds (NULL: all roots)
 *   bw_vfs_mem_size   one member's size, or -1 when the layer has none
 *   bw_vfs_mem_list   the n-th member of any root: 1, or 0 past the end
 *
 * None of these flushes the cache: the caller drops the path it changed.
 */
#define BW_VFS_MEM_ALL 0xFFFFFFFFu
int bw_vfs_mem_set(const char *root, const char *rel, u32 index,
                   const void *data, u32 size);
u32 bw_vfs_mem_clear(const char *root, const char *rel, u32 index);
u32 bw_vfs_mem_bytes(const char *root);
int bw_vfs_mem_size(const char *root, const char *rel, u32 index);
u32 bw_vfs_mem_list(u32 n, const char **rel, u32 *index);

/*
 * An archive that is open (sdk/fs.c): the cache takes over an image held
 * by `refs` open files, freed at the last close (0 when it cannot).
 */
u32 bw_vfs_adopt(const char *rel, u8 *img, u32 size, u32 refs);

/*
 * NARC helpers.  bw_narc_count: the member count, or 0xFFFFFFFF for an
 * image that is not a NARC.  bw_narc_member: one member's bytes inside the
 * image, 0 or -1.  bw_narc_restamp: the layout-stable image of `old` with
 * the members of `fresh`: the same header, name table, member count and
 * image base, and each member that differs written after the end, with
 * only its FAT entry changed.  A member past fresh's count becomes empty.
 * 1 with a malloc'd *out, 0 when nothing differs, -1 on an error.
 */
u32 bw_narc_count(const u8 *img, u32 size);
int bw_narc_member(const u8 *img, u32 size, u32 index, const u8 **data, u32 *len);
int bw_narc_restamp(const u8 *old, u32 old_size, const u8 *fresh, u32 fresh_size,
                    u8 **out, u32 *out_size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_VFS_H */
