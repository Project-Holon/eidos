/*
 * tests/test_shops.c -- the shop table's lines (sdk/shops.h): what reads,
 * what is refused, a later line winning, and the lists the shop's builders
 * get.  The shop itself is checked in a run of the game.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "shops.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_shops_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

int main(void)
{
    const u16 *items = NULL;
    const s32 *prices = NULL;

    bw_shops_clear();
    printf("lines\n");
    BW_CHECK_EQ("a mart list", line("mart.5 = 1 2 3 4   # balls"), 0);
    BW_CHECK_EQ("a BP list", line("bp.1 = 50:48 213:32"), 0);
    BW_CHECK_EQ("only a comment", line("# nothing"), 1);
    BW_CHECK_EQ("mart 26", line("mart.26 = 1"), -1);
    BW_CHECK_EQ("bp 3", line("bp.3 = 1:1"), -1);
    BW_CHECK_EQ("a price on a mart item", line("mart.0 = 1:200"), -1);
    BW_CHECK_EQ("no price on a BP item", line("bp.0 = 50"), -1);
    BW_CHECK_EQ("item 0", line("mart.0 = 0"), -1);
    BW_CHECK_EQ("an item past the limit", line("mart.0 = 9999"), -1);
    BW_CHECK_EQ("no items", line("mart.0 ="), -1);
    BW_CHECK_EQ("an unknown kind", line("shop.0 = 1"), -1);
    {
        char many[400] = "mart.2 =";
        int k;

        for (k = 0; k < 41; k++) {
            strcat(many, " 17");
        }
        BW_CHECK_EQ("41 items", line(many), -1);
    }

    printf("lists\n");
    BW_CHECK_EQ("mart 5: four items", bw_shop_mart(5, &items), 4);
    BW_CHECK_EQ("in order", items[0] == 1 && items[3] == 4, 1);
    BW_CHECK_EQ("mart 0: the game's own", bw_shop_mart(0, &items), -1);
    BW_CHECK_EQ("bp 1: two items", bw_shop_bp(1, &items, &prices), 2);
    BW_CHECK_EQ("with their prices", items[1] == 213 && prices[0] == 48 && prices[1] == 32, 1);
    BW_CHECK_EQ("bp 0: the game's own", bw_shop_bp(0, &items, &prices), -1);
    BW_CHECK_EQ("a later line wins", line("mart.5 = 17"), 0);
    BW_CHECK_EQ("mart 5: one item now", bw_shop_mart(5, &items), 1);
    bw_shops_clear();
    BW_CHECK_EQ("cleared", bw_shop_mart(5, &items), -1);
    BW_REPORT("test_shops");
}
