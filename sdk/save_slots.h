/*
 * sdk/save_slots.h -- several single-player saves (docs/multiplayer.md,
 * "Save slots"; metaxy docs/SPEC.md section 11).
 *
 * The DS game knows one save.  The port keeps as many save files as the
 * player makes and hands the game one of them (the backup chip's file,
 * sdk/arm7/fs.c).  Slot 1 is the save the port always had (pokeblack.sav in
 * the save folder); slots 2 and on are slots/save<n>.sav.  The saves the
 * Multiplayer screen's cards made are slots -1, -2 and on:
 * games/game<k>.sav (sdk/save_games.h).  The slot in force is kept in
 * slot.txt, so the game opens on it again.
 *
 * The title menu flips the player's own slots on its Continue card (left
 * and right), and New Game takes a free slot: there is always one.  The
 * folder's tidy step (sdk/save_tidy.h) closes the gaps at each start.
 */
#ifndef BW_PORT_SAVE_SLOTS_H
#define BW_PORT_SAVE_SLOTS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The file of slot n (1 and up, or -k for games/game<k>.sav).  0, or -1
 * without a save folder. */
int  bw_slots_path(int n, char *out, u32 size);

/* Whether slot n holds a save. */
int  bw_slots_used(int n);

/* The slot in force (1 and up, or -k). */
int  bw_slots_current(void);
/* 1 when the save in force is one of Your games (sdk/save_games.h). */
int  bw_slots_current_hidden(void);

/* Make the slot in force the backup chip's file (at start-up, and when a
 * multiplayer game gives the player's own save back). */
void bw_slots_apply(void);
/* The save folder is another one from here on (a standalone game that went
 * to the other version of the game plays from that version's profile):
 * what was found out about the old folder's slot files is forgotten. */
void bw_slots_folder_changed(void);
/* The folder's files moved (sdk/save_tidy.h): all that was found out is
 * forgotten, slot.txt too. */
void bw_slots_forget(void);

/*
 * The title menu's Continue card: the next used slot after the one in
 * force (d = 1) or before it (d = -1).  The game's save is read again from
 * it.  1 when the slot changed.
 */
int  bw_slots_flip(int d);

/* Slot n becomes the slot in force and the game reads its save (the
 * Multiplayer screen's Your games, before the title goes on with it): 1
 * when it loads.  The Continue card does not offer the saves the
 * Multiplayer screen made (sdk/save_games.h). */
int  bw_slots_continue(int n);

/* The title menu is about to list its rows: when the slot in force holds a
 * file the game could not load (a new game left before its first save),
 * the last slot with a save takes its place, so that save shows on the
 * Continue card instead of seeming gone. */
void bw_slots_settle(void);

/*
 * The title menu's New Game: when the slot in force holds a save, a free
 * slot is taken, so the new game does not touch the save (a Multiplayer
 * screen card's new game takes the next file of games/).  1 when it moved
 * to a free slot, 0 when the slot in force was free already.
 */
int  bw_slots_new_game(void);
/* Each frame: a held new game is written out at its first completed save. */
void bw_slots_frame(void);
/* The chip dropped a held new game (a reset before its save): the slot in
 * force goes back to the save before. */
void bw_slots_dropped(void);

/*
 * Another save file for the running game, without a restart: the chip
 * reads it at once and the game's save control loads it (or starts empty
 * when the file has no save).  1 when the file holds a save.
 */
int  bw_slots_load_file(const char *path);

/* The Continue card's line: "< 2/3 >" (the slot among the used ones), or
 * "" when there is no save. */
void bw_slots_label(char *out, u32 size);
/* How many saves of the player's own the Continue card offers. */
int  bw_slots_count(void);

/*
 * A session on another save that leaves no trace (the style chooser's
 * preview): the file is read into the chip, the chip's writes are held in
 * memory from then on, and the game's save control loads it.  1 when the
 * file holds a save the game loads.  The session lasts through the port's
 * restarts (bw_slots_try_boot, in the place of bw_slots_apply: the chip is
 * that file again, held).  It ends with bw_slots_try_leave before a
 * restart (the next start-up reads the slot in force), or, in a running
 * game, when the title menu next lists its rows (bw_slots_settle) or with
 * bw_slots_try_end: the slot in force is the chip's file again and what
 * the session wrote is dropped.
 */
int  bw_slots_try_begin(const char *path);
/* path NULL: the save built into the program (sdk/preview_save.c), unpacked
 * into memory and given to the chip from there, with no file at all.
 * bw_slots_try_embedded: 1 when the program has it. */
int  bw_slots_try_embedded(void);
int  bw_slots_try_active(void);
int  bw_slots_try_boot(void);
int  bw_slots_try_reload(void);     /* the game reads the session's save again: 1 loaded */
void bw_slots_try_leave(void);
void bw_slots_try_end(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_SLOTS_H */
