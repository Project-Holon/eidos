/*
 * sdk/battle_moves.c -- the state eidos's own move bases keep for a battle
 * Pokemon (battle_moves.h).
 */

#include <string.h>

#include "battle_moves.h"

typedef struct Guard {
    int set;
    int turn;
    int values[BW_BM_GUARD_COUNT];
} Guard;

typedef struct Third {
    const void *poke;           /* the server's copy that has it, NULL for none */
    int type;
} Third;

static Guard g_guard[BW_BM_POKE_COUNT];
static Third g_third[BW_BM_POKE_COUNT];

static int known(int pokeId)
{
    return pokeId >= 0 && pokeId < BW_BM_POKE_COUNT;
}

void bw_bm_guard_set(int pokeId, int turn, int damagingOnly, int hp, int stat, int stages, int status)
{
    Guard *g;

    if (!known(pokeId)) {
        return;
    }
    g = &g_guard[pokeId];
    g->set = 1;
    g->turn = turn;
    g->values[BW_BM_GUARD_DAMAGING_ONLY] = damagingOnly;
    g->values[BW_BM_GUARD_HP] = hp;
    g->values[BW_BM_GUARD_STAT] = stat;
    g->values[BW_BM_GUARD_STAGES] = stages;
    g->values[BW_BM_GUARD_STATUS] = status;
}

int bw_bm_guard(int pokeId, int turn, int *out)
{
    if (!known(pokeId) || !g_guard[pokeId].set || g_guard[pokeId].turn != turn) {
        return 0;
    }
    memcpy(out, g_guard[pokeId].values, sizeof g_guard[pokeId].values);
    return 1;
}

void bw_bm_guard_clear(int pokeId)
{
    if (known(pokeId)) {
        g_guard[pokeId].set = 0;
    }
}

void bw_bm_third_type_set(int pokeId, const void *poke, int type)
{
    if (!known(pokeId) || poke == NULL) {
        return;
    }
    if (type >= 0) {
        g_third[pokeId].poke = poke;
        g_third[pokeId].type = type;
    } else if (g_third[pokeId].poke == poke) {
        g_third[pokeId].poke = NULL;
    }
}

int bw_bm_third_type(int pokeId, const void *poke)
{
    return known(pokeId) && poke != NULL && g_third[pokeId].poke == poke ? g_third[pokeId].type : -1;
}

int bw_bm_third_type_any(int pokeId)
{
    return known(pokeId) && g_third[pokeId].poke != NULL ? g_third[pokeId].type : -1;
}

/* The last effectiveness past the classes, by defender: a move with
 * several targets takes the effectiveness of each before any damage. */
static struct {
    int attacker, move, extra;
} g_extra[BW_BM_POKE_COUNT];

void bw_bm_affinity_extra_set(int attackerId, int defenderId, int move, int extra)
{
    if (known(defenderId)) {
        g_extra[defenderId].attacker = attackerId;
        g_extra[defenderId].move = move;
        g_extra[defenderId].extra = extra;
    }
}

int bw_bm_affinity_extra(int attackerId, int defenderId, int move)
{
    return known(defenderId) && g_extra[defenderId].attacker == attackerId && g_extra[defenderId].move == move
        ? g_extra[defenderId].extra : 0;
}

/* the last move's result and Laser Focus, of the server's copy */
static struct {
    const void *poke;           /* the copy they belong to, NULL none */
    u8 failed;
    int laserEnd;               /* the last turn of Laser Focus plus one, 0 none */
} g_state[BW_BM_POKE_COUNT];

static void own(int pokeId, const void *poke)
{
    if (g_state[pokeId].poke != poke) {
        g_state[pokeId].poke = poke;
        g_state[pokeId].failed = 0;
        g_state[pokeId].laserEnd = 0;
    }
}

void bw_bm_move_result(int pokeId, const void *poke, int failed)
{
    if (known(pokeId) && poke != NULL) {
        own(pokeId, poke);
        g_state[pokeId].failed = (u8)(failed != 0);
    }
}

int bw_bm_last_move_failed(int pokeId, const void *poke)
{
    return known(pokeId) && poke != NULL && g_state[pokeId].poke == poke && g_state[pokeId].failed;
}

void bw_bm_laser_focus_set(int pokeId, const void *poke, int lastTurn)
{
    if (known(pokeId) && poke != NULL) {
        own(pokeId, poke);
        g_state[pokeId].laserEnd = lastTurn + 1;
    }
}

int bw_bm_laser_focus(int pokeId, const void *poke, int turn)
{
    return known(pokeId) && poke != NULL && g_state[pokeId].poke == poke && turn < g_state[pokeId].laserEnd;
}

void bw_bm_poke_reset(int pokeId, const void *poke)
{
    bw_bm_guard_clear(pokeId);
    bw_bm_poke_left(pokeId, poke);
}

void bw_bm_poke_left(int pokeId, const void *poke)
{
    bw_bm_third_type_set(pokeId, poke, -1);
    if (known(pokeId) && poke != NULL && g_state[pokeId].poke == poke) {
        g_state[pokeId].poke = NULL;
    }
}
