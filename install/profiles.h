/*
 * install/profiles.h -- the two game profiles of a game started without
 * the launcher.
 *
 * The launcher keeps one profile folder per game in the user folder,
 * profiles/Black and profiles/White, each with its saves/, its mods/ and
 * its settings.ini (launcher/src-tauri/src/profile.rs).  A game started
 * without the launcher (a plain start of the program, or another front
 * end) plays from the same two folders: Black from profiles/Black, White
 * from profiles/White.  So the two games never share a save, a player who
 * used the launcher finds the same saves and mods either way, and the game
 * can go from one version to the other while it runs.
 *
 * Before the two profiles a plain start kept everything in the user folder
 * itself: saves/, mods/, and settings.ini in the eidos home.  Those are
 * copied into their game's profile once, the first time that game starts
 * here, and left where they are.  A file named
 * BW_PROFILE_COPIED_NAME in the old saves folder says where the copy went,
 * so a later start does not copy again and a player who opens the old
 * folder knows it is no longer read.
 *
 * A profile that has saves of its own (the player used the launcher too)
 * keeps them: the old saves go into its free save slots (sdk/save_slots.h:
 * slot 1 is pokeblack.sav, slots 2 and on are slots/save<n>.sav, with no
 * limit; a free slot is one with no file, so nothing is written over),
 * each with its line of the Your games record under its new slot number
 * (games.ini; the game's tidy step, sdk/save_tidy.h, then moves those
 * games to games/), and the old multiplayer games beside the profile's
 * (one of a name the profile has already stays behind).  The old folder's
 * mods, settings and mod data are not taken then: the profile's own stand.
 *
 * Plain C99 over install.h's files (UTF-8 paths); nothing here knows the
 * user folder, which only the caller can name (sdk/plat_save.h's rule, or
 * the wasm host's).
 */

#ifndef EIDOS_PROFILES_H
#define EIDOS_PROFILES_H

#include <stddef.h>

#include "install.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_PROFILE_PATH_MAX 1024
#define BW_PROFILE_COPIED_NAME "copied-to-profiles.txt"

/* <user_dir>/profiles/Black or <user_dir>/profiles/White.  0 on success,
 * -1 for another game or a path too long. */
int bw_profile_dir(const char *user_dir, BwGame game, char *out, size_t n);

/* Whether a saves folder holds a game: slot 1's file (pokeblack.sav),
 * another slot's (slots/), or a multiplayer game (multiplayer/). */
int bw_profile_has_saves(const char *saves_dir);

/* What a plain start kept before the profiles; any of them may be NULL. */
typedef struct BwProfileOld {
    const char *saves;      /* <user folder>/saves */
    const char *mods;       /* <user folder>/mods */
    const char *settings;   /* <eidos home>/settings.ini */
    BwGame game;            /* the game those saves are of: the one the
                               install played before this start;
                               BW_GAME_UNKNOWN when nothing says */
} BwProfileOld;

enum {
    BW_PROFILE_READY = 0,   /* the profile is there */
    BW_PROFILE_COPIED,      /* and the old saves were copied now (into it,
                               into its free slots, or into a profile of
                               their own: `note` says which) */
    BW_PROFILE_FAILED = -1  /* a folder could not be made, or the copy did
                               not finish: the caller keeps the old places
                               for this run (nothing was removed) */
};

/*
 * Make the profile of `game` ready to play from: its folder, saves/ and
 * mods/; the old saves, mods and settings copied in when they are this
 * game's, the profile has no save of its own yet and they were not copied
 * before; and, for a profile with no settings.ini, the other game's
 * settings.ini, so a second game starts with the settings the player has.
 * `out` gets the profile folder.  `note` gets one sentence for the log
 * when something was copied, was not (the profile had saves already), or
 * failed; "" otherwise.
 */
int bw_profile_prepare(const char *user_dir, BwGame game, const BwProfileOld *old,
                       char *out, size_t n, char *note, size_t note_len);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_PROFILES_H */
