/*
 * sdk/battle_moves.h -- the state eidos's own move bases keep for a battle
 * Pokemon (docs/battle-handlers.md, "eidos's own move bases").
 *
 * The game keeps a Pokemon's battle state in its BtlPokeParam, whose layout
 * the matched code fixes.  The later moves need a little more: the contact
 * effect of a guard used this turn (Spiky Shield, King's Shield, Baneful
 * Bunker) and a third type (Trick-or-Treat, Forest's Curse).  It is kept
 * here by the Pokemon's battle id (0..23).  The guard's note is read by the
 * server's flow only, and holds for the turn it was put up.  The third type
 * belongs to the server's copy of the Pokemon (the BtlPokeParam the move's
 * handler was given): the clients keep copies of their own that replay the
 * server's commands later, and a replayed reset must not clear a type the
 * server gave after it.  It is cleared when the server's copy leaves the
 * field, has its effects reset, changes type, or is freed
 * (stubs/ov/patch_ov093_btl_pokeparam_moves.c).
 */
#ifndef BW_PORT_BATTLE_MOVES_H
#define BW_PORT_BATTLE_MOVES_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_BM_POKE_COUNT 24     /* battle ids 0..23 (BTL_POKEID_MAX) */

/* What a guard does to a Pokemon whose contact move it stops (a value of
 * bw_bm_guard's `out`, in this order). */
enum {
    BW_BM_GUARD_DAMAGING_ONLY,  /* 1: status moves pass (King's Shield)    */
    BW_BM_GUARD_HP,             /* the divisor of the attacker's HP lost, 0 none */
    BW_BM_GUARD_STAT,           /* the stat lowered, 0 none                */
    BW_BM_GUARD_STAGES,         /* its stages (negative lowers)            */
    BW_BM_GUARD_STATUS,         /* the status added, 0 none                */
    BW_BM_GUARD_COUNT
};

/* The guard `pokeId` put up on turn `turn`. */
void bw_bm_guard_set(int pokeId, int turn, int damagingOnly, int hp, int stat, int stages, int status);
/* 1 and its values in out[BW_BM_GUARD_COUNT] when `pokeId` put one up on
 * `turn`, else 0. */
int  bw_bm_guard(int pokeId, int turn, int *out);
void bw_bm_guard_clear(int pokeId);

/* The third type of the copy `poke` of Pokemon `pokeId`: the type, or -1
 * for none (and for any other copy).  Setting -1 clears it. */
void bw_bm_third_type_set(int pokeId, const void *poke, int type);
int  bw_bm_third_type(int pokeId, const void *poke);

/* The third type a server copy of Pokemon `pokeId` has, whatever the copy
 * asking (-1 none): the trainer AI reads the client's copies. */
int  bw_bm_third_type_any(int pokeId);

/* A type effectiveness past four times or under a quarter (three types,
 * a second type): the server's last calculation for (attacker, defender,
 * move) keeps how many more doublings (1: eight times) or halvings (-1:
 * an eighth) the damage takes than its class gives. */
void bw_bm_affinity_extra_set(int attackerId, int defenderId, int move, int extra);
int  bw_bm_affinity_extra(int attackerId, int defenderId, int move);

/* Whether the last move the server's copy `poke` of `pokeId` used failed
 * (Stomping Tantrum). */
void bw_bm_move_result(int pokeId, const void *poke, int failed);
int  bw_bm_last_move_failed(int pokeId, const void *poke);

/* Laser Focus: the moves of the copy `poke` of `pokeId` are critical hits
 * up to turn `lastTurn`. */
void bw_bm_laser_focus_set(int pokeId, const void *poke, int lastTurn);
int  bw_bm_laser_focus(int pokeId, const void *poke, int turn);

/* The third type of the copy `poke`, the guard, the last move's result and
 * Laser Focus of Pokemon `pokeId` (the copy leaves the field, is reset or
 * freed). */
void bw_bm_poke_reset(int pokeId, const void *poke);
/* The same without the guard (the copy leaves the field or is reset). */
void bw_bm_poke_left(int pokeId, const void *poke);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_BATTLE_MOVES_H */
