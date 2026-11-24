/*
 * sdk/ui_dressup.c -- what the Musical's dress-up (overlay 133,
 * src/game/ov133/dup_fitting.c) needs to know of the one-screen interface
 * (engine/ui/recipe_musical.c), for stubs/ov/patch_ov133_dup_fitting.c.
 *
 * The one-screen dress-up is a list of props beside the Pokemon: the DS's
 * ring of props round the Pokemon and its stylus are not part of it.  While
 * that screen draws (it says so every frame, bw_ui_dressup_mark), the
 * game's dress-up keeps its ring outside the picture and reads no stylus.
 * When the screen stops drawing (the two-screen layout, a skipped recipe),
 * the game's dress-up is the DS's again within a few frames.
 */

#include <stddef.h>
#include <stdlib.h>

#include "types.h"
#include "input.h"

static volatile int g_marked;
static volatile u32 g_marked_at;

/* The one-screen dress-up drew this frame. */
void bw_ui_dressup_mark(void)
{
    g_marked_at = bw_input_frame_number();
    g_marked = 1;
}

/* 1 while the one-screen dress-up draws (it drew within the last 4 input
 * frames). */
int bw_ui_dressup_own(void)
{
    return g_marked && bw_input_frame_number() - g_marked_at <= 4u;
}

/* Tests (docs/test-switches.md, EIDOS_DEBUG_DRESSUP_DEMO=1): 1 once, so the
 * dress-up starts the game's first-visit demo on a save that has seen it. */
int bw_ui_dressup_test_demo(void)
{
    static int asked;

    if (asked || getenv("EIDOS_DEBUG_DRESSUP_DEMO") == NULL) {
        return 0;
    }
    asked = 1;
    return 1;
}
