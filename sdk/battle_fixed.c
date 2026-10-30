/*
 * sdk/battle_fixed.c -- the place's move for an effect script's fixed
 * points (stubs/ov/patch_ov094_btlv_effvm.c).
 *
 * The real map moves the battle for the place (sdk/battle_view.c): the
 * foe's side back or forward and up (bmap_battle_warp, which bends with the
 * depth), the trainers by the place's offsets.  The slots get it through
 * the slot tables.  An effect script that puts a sprite at a point of its
 * own (a trainer back mid-battle, effect 63) gets it here, from the same
 * warp, so the sprite stands where the place puts that point and the draw
 * (which takes the warp back off to size the sprite) sizes it right.
 *
 * sdk/battle_view.c hands the move in (eidos_bv_fixed_move_set) and takes
 * it back at each battle's start.  It is not built on every platform, and
 * it is not in the overlays' link, so the overlay calls this file, which
 * is.  With no move set, with the original backdrop on show (the classic
 * look) or with the hook "battle_fixed_points" off, a point stays the
 * game's own.
 */

#include <stddef.h>

#include "mod_hooks.h"

/* sdk/battle_view.c: the real map is on show (absent where it is not built) */
extern int bw_battle_view_active(void) __attribute__((weak));

static void (*g_move)(int pos, int *xyz);

void eidos_bv_fixed_move_set(void (*move)(int pos, int *xyz))
{
    g_move = move;
}

int eidos_bv_fixed_point(int pos, int *xyz)
{
    if (g_move == NULL || xyz == NULL || pos < 0 || pos > 13 || bw_battle_view_active == NULL
        || !bw_battle_view_active() || !bw_hook_enabled("battle_fixed_points")) {
        return 0;
    }
    g_move(pos, xyz);
    return 1;
}
