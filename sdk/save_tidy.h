/*
 * sdk/save_tidy.h -- a saves folder put in order: the player's own saves
 * in slots 1, 2, 3 and on with no gap, the Multiplayer screen's games in
 * their own folder, and the files that hold no save set aside.
 *
 * The rules (sdk/save_slots.h has the folder):
 *
 * - A save of the Multiplayer screen's cards that an older eidos kept in a
 *   slot (a line of games.ini) moves to games/game<k>.sav, after the games
 *   already there, and its line to games/games.ini.
 * - A slot file that holds no save the game would load (bw_save_image_
 *   loadable: no copy whose checksums hold, or a new game that never saved)
 *   moves to unreadable/, under a name that says which slot it was.  It is
 *   kept, never deleted.
 * - The saves left close up in their order: the lowest goes to slot 1, the
 *   next to slot 2, and so on.  slot.txt follows the save it named.
 * - games.ini is kept as games.ini.moved once its lines are in games/.
 *
 * A file is never written over: each move fails when its new name is taken
 * (bw_plat_move_new), and one process at a time works on a folder
 * (bw_plat_lock_folder; another finds it locked and leaves it be).  The
 * whole plan is written to tidy.txt in
 * the folder before the first move, tidy.step counts the moves done, and a
 * start that finds tidy.txt goes on with it from that step, so a crash in
 * the middle loses nothing.  The step runs at the
 * game's start, before the save in force is given to the game, and for the
 * launcher with `eidos --tidy-saves <saves folder>`.
 */
#ifndef BW_PORT_SAVE_TIDY_H
#define BW_PORT_SAVE_TIDY_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether the backup image at `path` holds a save the game loads: 1 yes, 0
 * no (no copy whose footer and table checksums hold, or the newest copy's
 * system data still marked active: a new game left before its first
 * save), -1 when the file cannot be read.  A copy that holds but whose
 * system block does not is taken for a save: what is not sure stays.
 */
int bw_save_image_loadable(const char *path);
/* The same for an image in memory. */
int bw_save_bytes_loadable(const u8 *image, size_t size);

/*
 * Put the folder `saves` in order (the rules above).  0 when it was in
 * order already, 1 when files moved, -1 when a step failed: what was done
 * stays done, tidy.txt keeps the rest for the next start, and `note` says
 * why.  `note` (may be NULL) gets one line for the player.
 */
int bw_saves_tidy(const char *saves, char *note, size_t note_size);

/* The name a save's per-save mod files take (sdk/mod_savedata.c,
 * mods/<namespace>/<name>.bin), from the save file's path in the saves
 * folder: ".sav" dropped and "/" made "-" (pokeblack.sav "pokeblack",
 * slots/save2.sav "slots-save2", games/game3.sav "games-game3"); any other
 * character the layer does not take becomes '_'.  0, or -1 when it does not
 * fit. */
int bw_save_state_name(const char *rel, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_TIDY_H */
