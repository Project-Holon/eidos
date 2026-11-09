/*
 * tests/test_platform_input.c -- the platform layer's input tables and the
 * window-to-panel mapping, under SDL's dummy video driver.
 *
 * What a headless run can check without a person at the keyboard: the
 * default keyboard and gamepad tables bind every DS button and bind no
 * physical control twice, and a window point lands on the bottom-screen
 * pixel worked out by hand, in both layouts, at a scale, with a gap, and
 * after the window changes size.  A pump with nothing held hands the input
 * layer an idle state.  A pad in use holds the keyboard back (one device at
 * a time).
 */

#include <stdio.h>
#include <string.h>

#include "input.h"
#include "platform.h"
#include "port_test.h"

static void test_tables(void)
{
    const EidosKeyBinding *k;
    const EidosPadBinding *g;
    u32 nk, ng, i, j, keys = 0, pads = 0;
    int dup = 0;

    k = eidos_default_keymap(&nk);
    g = eidos_default_padmap(&ng);
    for (i = 0; i < nk; i++) {
        keys |= k[i].button;
        for (j = i + 1; j < nk; j++)
            dup += k[i].scancode == k[j].scancode;
    }
    for (i = 0; i < ng; i++) {
        pads |= g[i].button;
        for (j = i + 1; j < ng; j++)
            dup += g[i].kind == g[j].kind && g[i].index == g[j].index;
    }
    BW_CHECK_EQ("tables: keyboard binds all twelve", keys, 0x0FFF);
    BW_CHECK_EQ("tables: gamepad binds all twelve", pads, 0x0FFF);
    BW_CHECK_EQ("tables: no control bound twice", dup, 0);
    BW_CHECK_EQ("tables: the DS bit order",
                EIDOS_PAD_X == BW_KEY_X && EIDOS_PAD_L == BW_KEY_L &&
                EIDOS_PAD_DEBUG == BW_KEY_DEBUG, 1);
}

static void test_mapping(void)
{
    EidosPlatformConfig cfg;
    EidosPlatform *p = NULL;
    int x = -1, y = -1;

    memset(&cfg, 0, sizeof cfg);
    cfg.win_w = 512; cfg.win_h = 768;
    cfg.layout = EIDOS_LAYOUT_STACKED;
    if (eidos_platform_init(&p, &cfg) != 0) {
        printf("no SDL video (dummy driver unavailable): skipped\n");
        return;
    }
    /* Stacked at 2x: 512 x 768, the bottom screen from window y 384.
     * Window (201, 485) is layout (100, 242), bottom-screen (100, 50).     */
    BW_CHECK_EQ("stacked: on the panel",
                eidos_platform_window_to_touch(p, 512, 768, 201, 485, &x, &y),
                1);
    BW_CHECK_EQ("stacked: x", x, 100);
    BW_CHECK_EQ("stacked: y", y, 50);
    BW_CHECK_EQ("stacked: top screen is off the panel",
                eidos_platform_window_to_touch(p, 512, 768, 10, 383, &x, &y),
                0);
    BW_CHECK_EQ("stacked: held to the top edge", y, 0);
    BW_CHECK_EQ("stacked: past the right edge",
                eidos_platform_window_to_touch(p, 512, 768, 600, 700, &x, &y),
                0);
    BW_CHECK_EQ("stacked: held to the right edge", x, 255);
    BW_CHECK_EQ("stacked: left of the window is off",
                eidos_platform_window_to_touch(p, 512, 768, -1, 500, &x, &y),
                0);
    /* The window resized to 3x (768 x 1152): (300, 726) is layout
     * (100, 242) again.                                                    */
    BW_CHECK_EQ("resized: on the panel",
                eidos_platform_window_to_touch(p, 768, 1152, 300, 726, &x, &y),
                1);
    BW_CHECK_EQ("resized: x", x, 100);
    BW_CHECK_EQ("resized: y", y, 50);
    BW_CHECK_EQ("idle: pump", eidos_platform_pump(p), 1);
    BW_CHECK_EQ("idle: no stylus", eidos_platform_touch(p, &x, &y), 0);
    eidos_platform_quit(p);

    /* Side by side, 1x, a gap of 16: the bottom screen from x 272.         */
    memset(&cfg, 0, sizeof cfg);
    cfg.win_w = 528; cfg.win_h = 192;
    cfg.layout = EIDOS_LAYOUT_SIDE_BY_SIDE;
    cfg.gap = 16;
    p = NULL;
    if (eidos_platform_init(&p, &cfg) != 0) {
        BW_CHECK_EQ("side by side: init", 0, 1);
        return;
    }
    BW_CHECK_EQ("side: on the panel",
                eidos_platform_window_to_touch(p, 528, 192, 272, 191, &x, &y),
                1);
    BW_CHECK_EQ("side: x", x, 0);
    BW_CHECK_EQ("side: y", y, 191);
    BW_CHECK_EQ("side: the gap is off the panel",
                eidos_platform_window_to_touch(p, 528, 192, 265, 10, &x, &y),
                0);
    /* Swapped: the bottom screen is on the left, the top one on the right
     * (the old bottom position is now the top screen).                     */
    eidos_platform_set_layout(p, EIDOS_LAYOUT_SIDE_BY_SIDE, 1, 16);
    BW_CHECK_EQ("swapped side: bottom at x 0",
                eidos_platform_window_to_touch(p, 528, 192, 5, 7, &x, &y), 1);
    BW_CHECK_EQ("swapped side: x", x, 5);
    BW_CHECK_EQ("swapped side: y", y, 7);
    BW_CHECK_EQ("swapped side: old place is the top screen",
                eidos_platform_window_to_touch(p, 528, 192, 300, 10, &x, &y),
                0);
    /* A fullscreen-sized output: the picture fills the width (1920 x 698,
     * 528x192 scaled by 1920/528), centred vertically at y 191.            */
    BW_CHECK_EQ("letterboxed: bottom-left pixel",
                eidos_platform_window_to_touch(p, 1920, 1080, 0, 191, &x, &y),
                1);
    BW_CHECK_EQ("letterboxed: x", x, 0);
    BW_CHECK_EQ("letterboxed: y", y, 0);
    BW_CHECK_EQ("letterboxed: above the picture is off",
                eidos_platform_window_to_touch(p, 1920, 1080, 100, 100, &x, &y),
                0);
    /* Half way along the bottom screen's width (window x 0..969 maps 0..255
     * of the swapped-left bottom screen). */
    BW_CHECK_EQ("letterboxed: mid screen",
                eidos_platform_window_to_touch(p, 1920, 1080, 485, 191 + 349,
                                               &x, &y), 1);
    BW_CHECK_EQ("letterboxed: mid x", x == 133 || x == 132, 1);
    eidos_platform_quit(p);
}

/*
 * One device at a time (#30, #32).  A Steam Deck outside Steam's game mode
 * sends each pad press twice, as the pad's button and as a key from Steam's
 * desktop layout, a few milliseconds apart.  The game must see one press.
 */
static void test_one_device(void)
{
    const u32 A = 0x1u, UP = 0x40u;
    unsigned long long owns = 0;

    /* the key first, then the pad, the key let go first: one long press */
    BW_CHECK_EQ("key first", eidos_platform_merge_input(A, 0, 1000, &owns), A);
    BW_CHECK_EQ("then both", eidos_platform_merge_input(A, A, 1016, &owns), A);
    BW_CHECK_EQ("the key let go, the pad held", eidos_platform_merge_input(0, A, 1033, &owns), A);
    BW_CHECK_EQ("both let go", eidos_platform_merge_input(0, 0, 1050, &owns), 0u);
    /* the pad first, its key copy late, after the pad was let go: no second
     * press */
    BW_CHECK_EQ("pad", eidos_platform_merge_input(0, UP, 1100, &owns), UP);
    BW_CHECK_EQ("pad let go", eidos_platform_merge_input(0, 0, 1116, &owns), 0u);
    BW_CHECK_EQ("the late key copy is held back", eidos_platform_merge_input(UP, 0, 1133, &owns), 0u);
    BW_CHECK_EQ("still held back", eidos_platform_merge_input(UP, 0, 1499, &owns), 0u);
    /* the pad at rest: the keyboard is the player's again */
    BW_CHECK_EQ("keyboard alone again", eidos_platform_merge_input(A, 0, 1500, &owns), A);
    /* a keyboard with no pad at all is never held back */
    owns = 0;
    BW_CHECK_EQ("no pad: keys", eidos_platform_merge_input(UP | A, 0, 5, &owns), UP | A);
}

int main(void)
{
    test_tables();
    test_mapping();
    test_one_device();
    BW_REPORT("test_platform_input");
}
