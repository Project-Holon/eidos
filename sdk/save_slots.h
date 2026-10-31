/*
 * sdk/save_slots.h -- several single-player saves (docs/multiplayer.md,
 * "Save slots"; metaxy docs/SPEC.md section 11).
 *
 * The DS game knows one save.  The port keeps up to BW_SLOTS_MAX save files
 * and hands the game one of them (the backup chip's file, sdk/arm7/fs.c).
 * Slot 1 is the save the port always had (pokeblack.sav in the save
 * folder); slots 2 to 8 are slots/save<n>.sav.  The slot in force is kept
 * in slot.txt, so the game opens on it again.
 *
 * The title menu flips the slots on its Continue card (left and right),
 * and New Game takes a free slot.  With every slot used, New Game asks
 * which save it replaces, then the game's own "this deletes your save"
 * warning comes.
 */
#ifndef BW_PORT_SAVE_SLOTS_H
#define BW_PORT_SAVE_SLOTS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_SLOTS_MAX 8

/* The file of slot n (1 to BW_SLOTS_MAX).  0, or -1 without a save folder. */
int  bw_slots_path(int n, char *out, u32 size);

/* Whether slot n holds a save. */
int  bw_slots_used(int n);

/* The slot in force (1 to BW_SLOTS_MAX). */
int  bw_slots_current(void);

/* Make the slot in force the backup chip's file (at start-up, and when a
 * multiplayer game gives the player's own save back). */
void bw_slots_apply(void);

/*
 * The title menu's Continue card: the next used slot after the one in
 * force (d = 1) or before it (d = -1).  The game's save is read again from
 * it.  1 when the slot changed.
 */
int  bw_slots_flip(int d);

/* The title menu is about to list its rows: when the slot in force holds a
 * file the game could not load (a new game left before its first save),
 * the last slot with a save takes its place, so that save shows on the
 * Continue card instead of seeming gone. */
void bw_slots_settle(void);

/*
 * The title menu's New Game: when the slot in force holds a save and a
 * slot is free, the free slot is taken, so the new game does not touch the
 * save.  1 when it moved to a free slot.
 */
int  bw_slots_new_game(void);

/*
 * Another save file for the running game, without a restart: the chip
 * reads it at once and the game's save control loads it (or starts empty
 * when the file has no save).  1 when the file holds a save.
 */
int  bw_slots_load_file(const char *path);

/* The Continue card's line: "< 2/3 >" (the slot among the used ones), or
 * "" when there is no save. */
void bw_slots_label(char *out, u32 size);

/* New Game with every slot used: the Continue card asks which save the
 * new game replaces ("Replace 3/8?"); left and right choose, A takes it, B
 * gives up.  Whether that choice is up, and setting it. */
int  bw_slots_replacing(void);
void bw_slots_set_replacing(int on);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_SLOTS_H */
