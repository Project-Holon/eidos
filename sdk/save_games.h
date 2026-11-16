/*
 * sdk/save_games.h -- the saves a Multiplayer screen card made (a mod's
 * mode: a solo randomizer, a world made or joined).
 *
 * Those saves are kept apart from the single-player ones: the title menu's
 * Continue card does not offer them (save_slots.c), and the Multiplayer
 * screen lists them under Your games.  The record is games.ini in the save
 * folder, one line a slot:
 *
 *     <slot> <kind> <mod namespace> <mode id> <role> <code>
 *
 * kind: solo, world or room (an Archipelago room); role: host (made here) or
 * joined; code: the world's code (a room's: AP and its port), "-" while
 * there is none.  A record is written when the new game's
 * first save reaches its slot, and a slot a plain new game takes loses its
 * record then, so a record never names a save that is not there.
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
    int  slot;
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

/* save_slots.c: the new game's first save reached slot n.  The pending
 * record becomes slot n's; without one, slot n loses any record. */
void bw_games_saved(int n);

/* A world's code became known for the game in play (the relay made it). */
void bw_games_world_code(const char *code);

/* Slot n holds a save a card made: 1. */
int  bw_games_hidden(int n);

/* The records, newest slot write first: how many, and one. */
int  bw_games_count(void);
int  bw_games_at(int i, BwGame *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_GAMES_H */
