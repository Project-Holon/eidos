/*
 * sdk/fs.h -- the port side of the FS shim (sdk/fs.c): the NitroFS tables
 * and the hooks a test or the first-run extractor uses.
 *
 * The SDK surface itself (FS_OpenFile, FS_ReadFile, ...) is declared by the
 * matched sources that call it, each in its own spelling; this header is for
 * port code.  docs/boot.md and docs/files.md, "The file layer", are the design.
 */

#ifndef BW_PORT_FS_H
#define BW_PORT_FS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The ROM file system's two tables, exactly as the cartridge holds them:
 * the FAT (8 bytes per file id: u32 top, u32 bottom, ROM offsets) and the
 * FNT (the directory tree).  tools/ndsrom.py's `extract` writes them as
 * fat.bin and fnt.bin beside the data root (extract/data/.. is extract/),
 * and the shim loads them from there the first time it needs one.
 *
 * bw_fs_set_tables installs tables from memory instead (a test, or a caller
 * that already holds them); the bytes are copied.  Passing NULL for both
 * forgets any installed tables and lets the next use load from disk again.
 * Returns 0, or -1 if a copy could not be allocated.
 */
int bw_fs_set_tables(const void *fat, u32 fat_size, const void *fnt, u32 fnt_size);

/* 1 if a FAT and an FNT are available (installed or loaded), else 0. */
int bw_fs_tables_available(void);

/* Number of file ids the FAT describes (0 without tables). */
u32 bw_fs_file_count(void);

/* ROM extent [top, bottom) of a file id, from the FAT.  0, or -1. */
int bw_fs_file_extent(u32 file_id, u32 *top, u32 *bottom);

/*
 * NitroFS path of a file id ("a/0/0/2"), without a leading slash, into
 * `out`.  Returns 0, or -1 if the id has no name (an overlay file, an id
 * past the FAT) or the path does not fit.
 */
int bw_fs_path_of_id(u32 file_id, char *out, size_t out_size);

/*
 * File id of a NitroFS path.  Accepts the spellings the SDK accepts for the
 * ROM archive: "a/0/0/2", "/a/0/0/2", "rom:/a/0/0/2"; "." and ".." segments
 * are honoured and names compare without regard to ASCII case, as
 * FSi_ROMFAT_FindPath does.  Returns 0 and stores the id, or -1 if no file
 * has that path (a directory is not a file).
 */
int bw_fs_id_of_path(const char *path, u32 *file_id);

/*
 * The FS shim's file object lives in the 0x48 bytes of the game's FSFile.
 * bw_fs_open opens a host path; bw_fs_open_id a NitroFS file id through the
 * FNT and the file layer (mod overlay first); bw_fs_open_memory a host
 * buffer, which must outlive the open file.  Each returns 0 or -1.
 */
int bw_fs_open(void *file, const char *host_path);
int bw_fs_open_id(void *file, u32 file_id);
int bw_fs_open_rel(void *file, const char *rel);   /* see sdk/port.h */
int bw_fs_open_memory(void *file, const void *data, u32 size);
u32 bw_fs_size(void *file);

/*
 * Archives that are open (mod API v6; docs/mods.md, "Members in memory").
 * bw_fs_archive_open: how many files have the whole archive `rel` open, and
 * the member count of the image the first one reads.  bw_fs_archive_swap:
 * give every such file the layout-stable image of the archive as the
 * layers make it now (sdk/vfs.h, bw_narc_restamp); the game's handle keeps
 * every offset it read at the open.  Call it between frames, on the game's
 * thread.  Returns how many files it moved to a new image, or -1.
 */
int bw_fs_archive_open(const char *rel, u32 *count);
int bw_fs_archive_swap(const char *rel);

/* The DMA channel FS_Init / FS_SetDefaultDMA last set (the shim copies with
 * the CPU whatever it is; the value is kept because the game reads it back). */
u32 bw_fs_default_dma(void);

/* 1 once FS_Init has run. */
int bw_fs_initialised(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_FS_H */
