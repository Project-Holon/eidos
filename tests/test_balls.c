/*
 * tests/test_balls.c -- the ball table's lines (sdk/balls.h): what reads,
 * what is refused, the ball ids both ways, the files and numbers an added
 * ball takes from its own line or from the ball it works like, and the
 * Poke Ball for an id the table does not know.  The throw, the catch and
 * the summary are checked in a run of the game.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "balls.h"
#include "dsmem.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_balls_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

/* The shape of the game's table (arm9 palanm.c, sItemBallIdTable at
 * 0x0209E904): items 1..16 are ball ids 1..16, 492..499 are 17..24, 576 is
 * 25.  A run of the game reads the player's own. */
static void game_table(void)
{
    u16 *t;
    u32 i;

    bw_ds_reset();
    t = (u16 *)bw_ds_ptr(0x0209E904u);
    for (i = 0; i < 25; i++) {
        t[i * 2] = (u16)(i < 16 ? i + 1 : i < 24 ? 492 + (i - 16) : 576);
        t[i * 2 + 1] = (u16)(i + 1);
    }
}

int main(void)
{
    game_table();
    bw_balls_clear();

    printf("lines\n");
    BW_CHECK_EQ("an added ball", line("630 = 3 ball=26"), 0);
    BW_CHECK_EQ("a Net Ball with its own types", line("631 = 6 ball=27 ratio=4 type=fire type.2=grass # fire"), 0);
    BW_CHECK_EQ("a Timer Ball", line("632 = 10 ball=28 per_turn=0.5 ratio=5"), 0);
    BW_CHECK_EQ("its own files", line("633 = 3 ball=29 particles=733,734,735,736,737,738 sprite=165,166 icon=172,173"), 0);
    BW_CHECK_EQ("a Master Ball", line("634 = 1 ball=30 heal=1 friendship=200 friendship_bonus=2"), 0);
    BW_CHECK_EQ("a game ball tuned", line("3 = 3 ratio=1.75"), 0);
    BW_CHECK_EQ("a comment", line("# balls"), 1);
    BW_CHECK_EQ("a ball id below 26", line("640 = 3 ball=25"), -1);
    BW_CHECK_EQ("a ball id past 255", line("640 = 3 ball=256"), -1);
    BW_CHECK_EQ("an unknown key", line("640 = 3 ball=40 power=3"), -1);
    BW_CHECK_EQ("a key the base has no place for", line("640 = 3 ball=40 per_turn=1"), -1);
    BW_CHECK_EQ("types on a Great Ball", line("640 = 3 ball=40 type=fire"), -1);
    BW_CHECK_EQ("a ratio on the Master Ball", line("640 = 1 ball=40 ratio=2"), -1);
    BW_CHECK_EQ("five particle files", line("640 = 3 ball=40 particles=1,2,3,4,5"), -1);
    BW_CHECK_EQ("no base", line("640 = ball=40"), -1);

    printf("checked against the game's balls\n");
    BW_CHECK_EQ("a base that is no ball (read)", line("641 = 17 ball=41"), 0);
    BW_CHECK_EQ("an added ball with no id (read)", line("642 = 4"), 0);
    BW_CHECK_EQ("a game ball given an id (read)", line("4 = 4 ball=42"), 0);
    BW_CHECK_EQ("a base that is no ball is left out", bw_ball_id_of_item(641), 0);
    BW_CHECK_EQ("an added ball with no id is no ball", bw_ball_id_of_item(642), 0);
    BW_CHECK_EQ("the Poke Ball keeps its id", bw_ball_id_of_item(4), 4);

    printf("ball ids\n");
    BW_CHECK_EQ("the Great Ball", bw_ball_id_of_item(3), 3);
    BW_CHECK_EQ("the Dream Ball", bw_ball_id_of_item(576), 25);
    BW_CHECK_EQ("the added ball", bw_ball_id_of_item(630), 26);
    BW_CHECK_EQ("no ball", bw_ball_id_of_item(17), 0);
    BW_CHECK_EQ("id to item", bw_ball_item_of_id(27), 631);
    BW_CHECK_EQ("a game id to its item", bw_ball_item_of_id(17), 492);
    BW_CHECK_EQ("an unknown id", bw_ball_item_of_id(99), 0);

    printf("shown\n");
    BW_CHECK_EQ("a game ball as itself", bw_ball_shown(3), 3);
    BW_CHECK_EQ("none stays none", bw_ball_shown(0), 0);
    BW_CHECK_EQ("an added ball as its base", bw_ball_shown(26), 3);
    BW_CHECK_EQ("the Net Ball style", bw_ball_shown(27), 6);
    BW_CHECK_EQ("an unknown id as the Poke Ball", bw_ball_shown(99), 4);

    printf("files\n");
    BW_CHECK_EQ("no own particles", bw_ball_particle(26, 3), -1);
    BW_CHECK_EQ("own throw particles", bw_ball_particle(29, 3), 733);
    BW_CHECK_EQ("own second particles", bw_ball_particle(29, 0x1D), 734);
    BW_CHECK_EQ("own fourth of four", bw_ball_particle(29, 0x31), 738);
    BW_CHECK_EQ("a key that is no ball's", bw_ball_particle(29, 0x141), -1);
    BW_CHECK_EQ("own sprite character", bw_ball_sprite(29, 0), 165);
    BW_CHECK_EQ("own sprite palette", bw_ball_sprite(29, 1), 166);
    BW_CHECK_EQ("own icon palette", bw_ball_icon(29, 0), 172);
    BW_CHECK_EQ("own icon character", bw_ball_icon(29, 1), 173);
    BW_CHECK_EQ("no own icon", bw_ball_icon(26, 0), -1);

    printf("catch\n");
    BW_CHECK_EQ("the base of an added ball", bw_ball_catch_base(630), 3);
    BW_CHECK_EQ("an item with no line is its own base", bw_ball_catch_base(2), 2);
    BW_CHECK_EQ("the base's ratio", bw_ball_catch_value(630, BW_BALL_RATIO), 0x1800);
    BW_CHECK_EQ("a tuned game ball", bw_ball_catch_value(3, BW_BALL_RATIO), 0x1C00);
    BW_CHECK_EQ("the Ultra Ball's own", bw_ball_catch_value(2, BW_BALL_RATIO), 0x2000);
    BW_CHECK_EQ("own types", bw_ball_catch_value(631, BW_BALL_TYPE), 9);
    BW_CHECK_EQ("own second type", bw_ball_catch_value(631, BW_BALL_TYPE_2), 11);
    BW_CHECK_EQ("the Net Ball's types", bw_ball_catch_value(6, BW_BALL_TYPE), 10);
    BW_CHECK_EQ("own per turn", bw_ball_catch_value(632, BW_BALL_PER_TURN), 0x800);
    BW_CHECK_EQ("the Timer Ball's per turn", bw_ball_catch_value(10, BW_BALL_PER_TURN), 0x4CD);
    BW_CHECK_EQ("the Nest Ball's level", bw_ball_catch_value(8, BW_BALL_LEVEL), 30);
    BW_CHECK_EQ("the Quick Ball's turns", bw_ball_catch_value(15, BW_BALL_TURNS), 1);
    BW_CHECK_EQ("a Master Ball style", bw_ball_catch_base(634), 1);

    printf("after the catch\n");
    BW_CHECK_EQ("the Heal Ball heals", bw_ball_heals(14), 1);
    BW_CHECK_EQ("the Poke Ball does not", bw_ball_heals(4), 0);
    BW_CHECK_EQ("heal=1", bw_ball_heals(30), 1);
    BW_CHECK_EQ("an unknown id does not", bw_ball_heals(99), 0);
    BW_CHECK_EQ("friendship=200", bw_ball_catch_friendship(30), 200);
    BW_CHECK_EQ("no friendship on catch", bw_ball_catch_friendship(26), -1);
    BW_CHECK_EQ("the Luxury Ball's bonus", bw_ball_friendship_bonus(11), 1);
    BW_CHECK_EQ("friendship_bonus=2", bw_ball_friendship_bonus(30), 2);
    BW_CHECK_EQ("no bonus", bw_ball_friendship_bonus(26), 0);

    printf("a later line\n");
    BW_CHECK_EQ("the same item again", line("630 = 2 ball=26"), 0);
    BW_CHECK_EQ("works like the Ultra Ball now", bw_ball_catch_base(630), 2);
    BW_CHECK_EQ("another item takes ball 26", line("635 = 4 ball=26"), 0);
    BW_CHECK_EQ("it has the id", bw_ball_item_of_id(26), 635);
    BW_CHECK_EQ("the first item lost its line", bw_ball_id_of_item(630), 0);

    bw_balls_clear();
    BW_CHECK_EQ("cleared: no added ball", bw_ball_id_of_item(631), 0);
    BW_REPORT("test_balls");
}
