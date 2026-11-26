/*
 * tests/test_move_anims.c -- the move animation table (sdk/move_anims.h):
 * its lines, and the animation each move plays with and without one.
 */
#include <stdio.h>

#include "battle_handlers.h"
#include "move_anims.h"
#include "port_test.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_move_anims_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

int main(void)
{
    bw_move_anims_clear();
    bw_bh_clear();
    printf("lines\n");
    BW_CHECK_EQ("a built-in move", line("33 = 1   # Tackle plays Pound's"), 0);
    BW_CHECK_EQ("only a comment", line("# nothing"), 1);
    BW_CHECK_EQ("an animation past 559", line("33 = 560"), -1);
    BW_CHECK_EQ("animation 0", line("33 = 0"), -1);
    BW_CHECK_EQ("move 0", line("0 = 1"), -1);
    BW_CHECK_EQ("a move past the limit", line("700 = 1"), -1);
    BW_CHECK_EQ("two animations", line("33 = 1 2"), -1);

    printf("animations\n");
    BW_CHECK_EQ("a line", (long)bw_move_anim(33), 1);
    BW_CHECK_EQ("a game's move without one: its own", (long)bw_move_anim(53), 53);
    BW_CHECK_EQ("a later line wins", line("33 = 34"), 0);
    BW_CHECK_EQ("the later one", (long)bw_move_anim(33), 34);
    BW_CHECK_EQ("a new move: Pound's", (long)bw_move_anim(600), 1);
    BW_CHECK_EQ("works like Thunderbolt", bw_bh_parse_line(BW_BH_MOVE, "600 = 85", why, sizeof why), 0);
    BW_CHECK_EQ("a new move: its base's", (long)bw_move_anim(600), 85);
    BW_CHECK_EQ("a line over its base", line("600 = 87"), -1);  /* (600 is past the vanilla move limit here) */

    printf("preview\n");
    BW_CHECK_EQ("none", (long)bw_preview_move(), 0);
    bw_preview_set(53, 25, 504);
    BW_CHECK_EQ("the move", (long)bw_preview_move(), 53);
    BW_CHECK_EQ("the species", bw_preview_attacker() == 25 && bw_preview_defender() == 504, 1);
    bw_preview_set(0, 0, 0);

    bw_move_anims_clear();
    bw_bh_clear();
    BW_REPORT("test_move_anims");
}
