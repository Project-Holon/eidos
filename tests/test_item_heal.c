/*
 * tests/test_item_heal.c -- the item heal table's lines (sdk/item_heal.h):
 * what reads, what is refused, a later line winning, and the HP an item
 * heals (never more than the Pokemon is missing).  The heals themselves are
 * checked in a run of the game.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "item_heal.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_item_heal_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

int main(void)
{
    u32 a = 0;

    bw_item_heal_clear();
    printf("lines\n");
    BW_CHECK_EQ("a fixed amount", line("600 = hp=500"), 0);
    BW_CHECK_EQ("a fraction", line("601 = hp=1/3   # a third"), 0);
    BW_CHECK_EQ("full", line("602 = hp=full"), 0);
    BW_CHECK_EQ("a held amount", line("155 = held_hp=300"), 0);
    BW_CHECK_EQ("both", line("603 = hp=300 held_hp=40\r\n"), 0);
    BW_CHECK_EQ("a comment", line("# heals"), 1);
    BW_CHECK_EQ("no key", line("604 ="), -1);
    BW_CHECK_EQ("an unknown key", line("604 = pp=10"), -1);
    BW_CHECK_EQ("past 65535", line("604 = hp=70000"), -1);
    BW_CHECK_EQ("a fraction past 1/16", line("604 = hp=1/17"), -1);
    BW_CHECK_EQ("zero", line("604 = held_hp=0"), -1);

    printf("amounts\n");
    BW_CHECK_EQ("fixed, room for it", bw_item_heal_hp(600, 900, 700, &a) && a == 500, 1);
    BW_CHECK_EQ("fixed, only what is missing", bw_item_heal_hp(600, 900, 120, &a) && a == 120, 1);
    BW_CHECK_EQ("a third of 300", bw_item_heal_hp(601, 300, 300, &a) && a == 100, 1);
    BW_CHECK_EQ("a third of 2 is at least 1", bw_item_heal_hp(601, 2, 2, &a) && a == 1, 1);
    BW_CHECK_EQ("full", bw_item_heal_hp(602, 250, 40, &a) && a == 40, 1);
    BW_CHECK_EQ("no hp on a held-only line", bw_item_heal_hp(155, 250, 40, &a), 0);
    BW_CHECK_EQ("held", bw_item_heal_held_hp(155, 1000, &a) && a == 300, 1);
    BW_CHECK_EQ("held, only what is missing", bw_item_heal_held_hp(155, 7, &a) && a == 7, 1);
    BW_CHECK_EQ("no line: the record's", bw_item_heal_hp(17, 250, 40, &a), 0);
    BW_CHECK_EQ("a later line wins", line("600 = hp=20"), 0);
    BW_CHECK_EQ("its amount", bw_item_heal_hp(600, 900, 700, &a) && a == 20, 1);
    bw_item_heal_clear();
    BW_CHECK_EQ("cleared", bw_item_heal_hp(600, 900, 700, &a), 0);
    BW_REPORT("test_item_heal");
}
