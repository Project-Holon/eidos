/*
 * sdk/ui_catch.c -- what the Pokedex registration after a catch (overlay
 * 207, src/game/ov207/zukan_toroku_graphic.c) needs to know of the
 * one-screen interface (engine/ui/recipe_menus.c, the scene "catch"), for
 * stubs/ov/patch_ov207_zukan_toroku_graphic.c.
 *
 * The one-screen card is the registration's two DS screens at once.  While
 * it draws (it says so every frame, bw_ui_catch_mark), the game does not
 * fade to black and back between those two screens.  When the card stops
 * drawing (the two-screen layout, a skipped recipe), the registration is the
 * DS's again within a few frames.
 */

#include <stddef.h>

#include "types.h"
#include "input.h"

static volatile int g_marked;
static volatile u32 g_marked_at;

/* The one-screen card drew this frame. */
void bw_ui_catch_mark(void)
{
    g_marked_at = bw_input_frame_number();
    g_marked = 1;
}

/* 1 while the one-screen card draws (it drew within the last 4 input
 * frames). */
int bw_ui_catch_own(void)
{
    return g_marked && bw_input_frame_number() - g_marked_at <= 4u;
}

static volatile int g_early;

/* A or B came while the registration's screen faded in (its step 0, 23
 * frames in which the game reads no key and the one-screen card already
 * stands). */
void bw_ui_catch_early_note(void)
{
    g_early = 1;
}

/* 1 when such a press came since the last call; the note is gone after. */
int bw_ui_catch_early_take(void)
{
    const int e = g_early;

    g_early = 0;
    return e;
}
