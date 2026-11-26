/*
 * tests/test_trainer_mons.c -- the per-member table's lines
 * (sdk/trainer_mons.h): what reads, what is refused, and that a later
 * line for the same member wins.  The numbers reaching a battle are
 * checked in a run of the game.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "trainer_mons.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_trmons_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

int main(void)
{
    const BwTrMon *m;

    printf("lines\n");
    BW_CHECK_EQ("a full line", line("164.0 = ivs=31,31,31,31,31,31 evs=252,0,4,0,0,252 nature=jolly shiny=1 "
                                     "friendship=0 ability=hidden"), 0);
    BW_CHECK_EQ("one key", line("164.1 = nature=modest   # a comment"), 0);
    BW_CHECK_EQ("a number for the nature", line("164.2 = nature=24"), 0);
    BW_CHECK_EQ("ability 2", line("12.5 = ability=2"), 0);
    BW_CHECK_EQ("only a comment", line("  # nothing"), 1);
    BW_CHECK_EQ("an empty line", line("\n"), 1);
    BW_CHECK_EQ("a slot past 5", line("164.6 = shiny=1"), -1);
    BW_CHECK_EQ("no slot", line("164 = shiny=1"), -1);
    BW_CHECK_EQ("an IV of 32", line("1.0 = ivs=32,0,0,0,0,0"), -1);
    BW_CHECK_EQ("five IVs", line("1.0 = ivs=1,2,3,4,5"), -1);
    BW_CHECK_EQ("EVs over 510", line("1.0 = evs=252,252,252,0,0,0"), -1);
    BW_CHECK_EQ("an unknown nature", line("1.0 = nature=grumpy"), -1);
    BW_CHECK_EQ("ability 0", line("1.0 = ability=0"), -1);
    BW_CHECK_EQ("an unknown key", line("1.0 = level=50"), -1);
    BW_CHECK_EQ("no space between keys", line("1.0 = shiny=1nature=bold"), -1);

    printf("values\n");
    m = bw_trmons_find(164, 0);
    BW_REQUIRE("164.0", m != NULL);
    BW_CHECK_EQ("every key", m->has, BW_TRMON_HAS_IVS | BW_TRMON_HAS_EVS | BW_TRMON_HAS_NATURE | BW_TRMON_HAS_SHINY |
                                         BW_TRMON_HAS_FRIENDSHIP | BW_TRMON_HAS_ABILITY);
    BW_CHECK_EQ("Speed EV (the line's sixth)", m->evs[5], 252);
    BW_CHECK_EQ("Def EV (the third)", m->evs[2], 4);
    BW_CHECK_EQ("Jolly", m->nature, 13);
    BW_CHECK_EQ("shiny", m->shiny, 1);
    BW_CHECK_EQ("friendship 0", m->friendship, 0);
    BW_CHECK_EQ("hidden ability", m->ability, 3);
    m = bw_trmons_find(164, 1);
    BW_REQUIRE("164.1", m != NULL);
    BW_CHECK_EQ("only the nature", m->has, BW_TRMON_HAS_NATURE);
    BW_CHECK_EQ("Modest", m->nature, 15);
    BW_CHECK_EQ("Quirky by number", bw_trmons_find(164, 2)->nature, 24);
    BW_CHECK_EQ("no line: no member", bw_trmons_find(164, 3) == NULL, 1);
    BW_CHECK_EQ("a refused line is not kept", bw_trmons_find(1, 0) == NULL, 1);

    printf("a later line wins\n");
    BW_CHECK_EQ("the same member again", line("164.1 = shiny=1"), 0);
    m = bw_trmons_find(164, 1);
    BW_CHECK_EQ("its keys only", m->has, BW_TRMON_HAS_SHINY);

    bw_trmons_clear();
    BW_CHECK_EQ("cleared", bw_trmons_find(164, 0) == NULL, 1);
    BW_REPORT("test_trainer_mons");
}
