/*
 * sdk/plat_save.h -- the host calls the save files need that standard C does
 * not have: where a platform keeps a player's saves, making that folder, and
 * replacing a file in one step.
 *
 * Why a file of its own: the save layer (sdk/save.h) writes a new image
 * beside the old one and swaps it in, so a crash or a power cut leaves
 * either the old save or the new one and never half of each.  C99's rename
 * may refuse to replace an existing file (it does on Windows), and it has
 * neither mkdir nor a way to push a file's bytes to the disk.  Those three
 * and the platform's save folder are the only OS-specific pieces, so they
 * live here behind #ifdefs for Win32 and POSIX, the arrangement of
 * sdk/plat_dir.c and sdk/plat_thread.c.  Paths are narrow strings, forward
 * slashes accepted everywhere, as the rest of the file layer passes them.
 */

#ifndef BW_PORT_PLAT_SAVE_H
#define BW_PORT_PLAT_SAVE_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The platform's folder for this player's saves (docs/installer.md, "Where
 * the saves go"), written into `out`.  0 on success, -1 when the platform
 * gives no answer (no HOME, no APPDATA: a mobile front end, which sets the
 * folder itself with bw_set_save_dir).
 */
int bw_plat_default_save_dir(char *out, size_t out_size);

/* Create `path` and every missing parent.  0 when it exists afterwards.   */
int bw_plat_mkdirs(const char *path);

/* Push an open file's written bytes to the disk (fflush + fsync/_commit). */
int bw_plat_sync_file(FILE *f);

/*
 * Replace `to` with `from` in one step, `to` existing or not.  0 on success.
 * Win32 MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH); POSIX rename, which
 * is atomic within one file system (the temporary is always a sibling).
 */
int bw_plat_replace_file(const char *from, const char *to);

/*
 * Move `from` to `to` only when nothing is at `to`: 0 moved, 1 `to` is
 * taken (nothing was done), -1 failed.  Never writes over a file.
 */
int bw_plat_move_new(const char *from, const char *to);

/*
 * The folder for this process alone while it puts it in order
 * (sdk/save_tidy.h): a handle, or NULL when another process has it.  The
 * system lets go when the process ends, even in a crash.
 */
void *bw_plat_lock_folder(const char *dir);
void  bw_plat_unlock_folder(void *lock);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PLAT_SAVE_H */
