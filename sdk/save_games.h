/*
 * sdk/save_games.h -- the saves a Multiplayer screen card made (a mod's
 * mode: a solo randomizer, a world made or joined).
 *
 * Those saves are kept apart from the single-player ones, in their own
 * folder: games/game<k>.sav, which save_slots.c calls slot -k.  The title
 * menu's Continue card does not offer them, and the Multiplayer screen
 * lists them under Your games.  The record is games/games.ini, one line a
 * game:
 *
 *     <k> <kind> <mod namespace> <mode id> <role> <code>
 *
 * kind: solo, world or room (an Archipelago room); role: host (made here) or
 * joined; code: the world's code (a room's: AP and its port), "-" while
 * there is none.  A record is written when the new game's first save
 * reaches its file, so a record never names a save that is not there.
 *
 * An older eidos kept these saves among the player's own slots, with the
 * record in games.ini; the folder's tidy step (sdk/save_tidy.h) moves them
 * here at the next start.  Until then such a line still keeps its slot off
 * the Continue card.  Nothing here limits how many games there are.
 */
#ifndef BW_PORT_SAVE_GAMES_H
#define BW_PORT_SAVE_GAMES_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_GAMES_NS_MAX   32
#define BW_GAMES_MODE_MAX 24
#define BW_GAMES_CODE_MAX 17

enum { BW_GAME_SOLO, BW_GAME_WORLD, BW_GAME_ROOM };
enum { BW_GAME_HOST, BW_GAME_JOINED };

typedef struct BwGame {
    int  slot;                      /* -k: games/game<k>.sav */
    int  kind;                      /* BW_GAME_SOLO, BW_GAME_WORLD */
    int  role;                      /* BW_GAME_HOST, BW_GAME_JOINED */
    char ns[BW_GAMES_NS_MAX];
    char mode[BW_GAMES_MODE_MAX];
    char code[BW_GAMES_CODE_MAX];   /* "" while there is none */
} BwGame;

/* The next new game is a mode's: its record waits for the first save.
 * code: the world joined, or NULL (a world made here learns its code later,
 * bw_games_world_code). */
void bw_games_pending(int kind, int role, const char *ns, const char *mode, const char *code);
/* The pending record is dropped (a plain new game, or none was saved). */
void bw_games_pending_clear(void);
/* 1 while a mode's new game waits for its first save (save_slots.c gives
 * it a file in games/). */
int  bw_games_has_pending(void);

/* save_slots.c: the new game's first save reached slot n.  The pending
 * record becomes slot n's. */
void bw_games_saved(int n);

/* A world's code became known for the game in play (the relay made it). */
void bw_games_world_code(const char *code);

/* Slot n holds a save a card made: 1 (every slot of games/, and a slot an
 * older eidos's games.ini names). */
int  bw_games_hidden(int n);

/* The folder's files moved (sdk/save_tidy.h): the records are read again. */
void bw_games_forget(void);

/* The records, newest slot write first: how many, and one. */
int  bw_games_count(void);
int  bw_games_at(int i, BwGame *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_GAMES_H */
