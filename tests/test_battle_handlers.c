/*
 * tests/test_battle_handlers.c -- a mod's moves, abilities and items that
 * work like a built-in one (sdk/battle_handlers.h): the table lines, the
 * chains of bases, the keys a base has places for (the generated
 * battle_handler_sites.inc), and the numbers the places get as the battle
 * runs handlers (the first place of a kind in a handler call, nested calls).
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "battle_handlers.h"
#include "move_kinds.h"

/* ability 22 Intimidate (handler 0: one stat change: atk -1), ability 66
 * Blaze (handler 0: type fire, hp 1/3, ratio 1.5), ability 9 Static
 * (status paralysis, chance 30): their rows in battle_handler_sites.inc */
#define INTIMIDATE 22
#define BLAZE      66
#define STATIC     9

static int line(int type, const char *text)
{
    char why[200];
    int r = bw_bh_parse_line(type, text, why, sizeof why);

    if (r < 0) {
        printf("  (refused: %s)\n", why);
    }
    return r;
}

int main(void)
{
    char why[200];
    u32 stat;
    s8 amount;
    int v;

    printf("lines\n");
    bw_bh_clear();
    BW_CHECK_EQ("a copy with keys", line(BW_BH_ABILITY, "165 = 22 stat=spatk stages=-2"), 0);
    BW_CHECK_EQ("a comment", line(BW_BH_ABILITY, "  # Super Intimidate"), 1);
    BW_CHECK_EQ("a blank line", line(BW_BH_ABILITY, "\n"), 1);
    BW_CHECK_EQ("a trailing comment", line(BW_BH_ABILITY, "166 = 66 ratio=2.0 type=water # a Torrent x2"), 0);
    BW_CHECK_EQ("an indexed key", line(BW_BH_ABILITY, "167 = 22 stat.1=def"), 0);
    BW_CHECK_EQ("an unknown key", line(BW_BH_ABILITY, "168 = 22 luck=3"), -1);
    BW_CHECK_EQ("a stat that is none", line(BW_BH_ABILITY, "168 = 22 stat=luck"), -1);
    BW_CHECK_EQ("stages past 6", line(BW_BH_ABILITY, "168 = 22 stages=-7"), -1);
    BW_CHECK_EQ("a ratio past 4", line(BW_BH_ABILITY, "168 = 66 ratio=5"), -1);
    BW_CHECK_EQ("an hp that is no fraction", line(BW_BH_ABILITY, "168 = 24 hp=8"), -1);
    BW_CHECK_EQ("no base", line(BW_BH_ABILITY, "168 ="), -1);
    BW_CHECK_EQ("no =", line(BW_BH_ABILITY, "168 22"), -1);
    BW_CHECK_EQ("a key with no =", line(BW_BH_ABILITY, "168 = 22 stat"), -1);
    BW_CHECK_EQ("an id of 0", line(BW_BH_ABILITY, "0 = 22"), -1);
    BW_CHECK_EQ("the parsed value", bw_bh_key(BW_BH_ABILITY, 165, BW_BH_KEY_STAT, 1, &v) && v == 3, 1);
    BW_CHECK_EQ("a ratio in 4096ths", bw_bh_key(BW_BH_ABILITY, 166, BW_BH_KEY_RATIO, 1, &v) && v == 8192, 1);
    BW_CHECK_EQ("a type by name", bw_bh_key(BW_BH_ABILITY, 166, BW_BH_KEY_TYPE, 1, &v) && v == 10, 1);
    BW_CHECK_EQ("a type by number", line(BW_BH_ABILITY, "169 = 66 type=12"), 0);
    BW_CHECK_EQ("its value", bw_bh_key(BW_BH_ABILITY, 169, BW_BH_KEY_TYPE, 1, &v) && v == 12, 1);
    BW_CHECK_EQ("a type past the limit (no mod adds one)", line(BW_BH_ABILITY, "168 = 66 type=17"), -1);
    BW_CHECK_EQ("a type no table names", line(BW_BH_ABILITY, "168 = 66 type=fairy"), -1);
    BW_CHECK_EQ("a later line wins", line(BW_BH_ABILITY, "165 = 22 stat=spdef stages=-2"), 0);
    BW_CHECK_EQ("its value", bw_bh_key(BW_BH_ABILITY, 165, BW_BH_KEY_STAT, 1, &v) && v == 4, 1);

    printf("bases\n");
    BW_CHECK_EQ("a copy's base", bw_bh_base(BW_BH_ABILITY, 165), INTIMIDATE);
    BW_CHECK_EQ("a built-in is its own", bw_bh_base(BW_BH_ABILITY, INTIMIDATE), INTIMIDATE);
    BW_CHECK_EQ("other kinds are apart", bw_bh_base(BW_BH_ITEM, 165), 165);
    line(BW_BH_ABILITY, "170 = 165 stages=-3");
    BW_CHECK_EQ("a chain ends at the built-in", bw_bh_base(BW_BH_ABILITY, 170), INTIMIDATE);
    line(BW_BH_ABILITY, "180 = 181");
    line(BW_BH_ABILITY, "181 = 180");
    bw_bh_check();
    BW_CHECK_EQ("a cycle is left out", bw_bh_base(BW_BH_ABILITY, 180), 180);

    printf("eidos's own move bases, by name\n");
    bw_bh_clear();
    BW_CHECK_EQ("a named base", line(BW_BH_MOVE, "571 = misty_terrain"), 0);
    BW_CHECK_EQ("its id", bw_bh_base(BW_BH_MOVE, 571), BW_BH_MISTY_TERRAIN);
    BW_CHECK_EQ("its name", strcmp(bw_bh_named_name(BW_BH_MISTY_TERRAIN), "misty_terrain"), 0);
    BW_CHECK_EQ("a named base with keys", line(BW_BH_MOVE, "570 = flower_shield stat=spdef stages=2 # x"), 0);
    BW_CHECK_EQ("a name in a line of several", line(BW_BH_MOVE, "572 = 182 + crafty_shield"), 0);
    BW_CHECK_EQ("an unknown name", line(BW_BH_MOVE, "573 = fairy_locks"), -1);
    BW_CHECK_EQ("names are for moves", line(BW_BH_ABILITY, "165 = geomancy"), -1);
    line(BW_BH_MOVE, "574 = misty_terrain type=fire");
    bw_bh_check();
    BW_CHECK_EQ("the names' keys", bw_bh_key(BW_BH_MOVE, 570, BW_BH_KEY_STAGES, 1, &v) && v == 2, 1);
    BW_CHECK_EQ("the second part's base", bw_bh_part_base(BW_BH_MOVE, 572, 1), BW_BH_CRAFTY_SHIELD);
    BW_CHECK_EQ("a key a named base has no place for", bw_bh_key(BW_BH_MOVE, 574, BW_BH_KEY_TYPE, 1, &v), 0);
    stat = 2;
    amount = 1;
    bw_bh_enter(BW_BH_MOVE, 570, 0);
    bw_bh_rank(&stat, &amount);
    bw_bh_leave();
    BW_CHECK_EQ("Flower Shield's stat change takes the keys", stat == 4 && amount == 2, 1);

    printf("keys the base has no place for are left out\n");
    bw_bh_clear();
    line(BW_BH_ABILITY, "165 = 22 stat=spdef stages=-2");
    line(BW_BH_ABILITY, "171 = 22 ratio=2.0 stat=spatk");
    line(BW_BH_ABILITY, "172 = 22 stat.2=def");
    bw_bh_check();
    BW_CHECK_EQ("Intimidate has no ratio", bw_bh_key(BW_BH_ABILITY, 171, BW_BH_KEY_RATIO, 1, &v), 0);
    BW_CHECK_EQ("its stat stays", bw_bh_key(BW_BH_ABILITY, 171, BW_BH_KEY_STAT, 1, &v), 1);
    BW_CHECK_EQ("no second stat change", bw_bh_key(BW_BH_ABILITY, 172, BW_BH_KEY_STAT, 2, &v), 0);

    printf("a running handler's places\n");
    bw_bh_clear();
    line(BW_BH_ABILITY, "165 = 22 stat=spatk stages=-2");
    line(BW_BH_ABILITY, "166 = 66 ratio=2.0 type=water hp=1/2");
    line(BW_BH_ABILITY, "9 = 9 chance=100 status=burn");
    bw_bh_check();
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("outside a handler: as it was", stat == 1 && amount == -1, 1);
    bw_bh_enter(BW_BH_ABILITY, 165, 0);
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("Super Intimidate: Sp. Atk", (long)stat, 3);
    BW_CHECK_EQ("by 2", (long)amount, -2);
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("a second stat change in the call is the engine's", stat == 1 && amount == -1, 1);
    bw_bh_leave();
    bw_bh_enter(BW_BH_ABILITY, INTIMIDATE, 0);
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("the built-in one is untouched", stat == 1 && amount == -1, 1);
    bw_bh_leave();

    bw_bh_enter(BW_BH_ABILITY, 166, 0);
    BW_CHECK_EQ("the type it tests", bw_bh_type(9), 10);
    BW_CHECK_EQ("its HP fraction", (long)bw_bh_hp(3), 2);
    BW_CHECK_EQ("its multiplier", (long)bw_bh_ratio(6144), 8192);
    BW_CHECK_EQ("a second multiplier is the engine's", (long)bw_bh_ratio(6144), 6144);
    /* a command of the handler runs another factor's handler: its own frame */
    bw_bh_enter(BW_BH_ABILITY, STATIC, 0);
    BW_CHECK_EQ("the nested Static's chance", (long)bw_bh_chance(30), 100);
    {
        u32 kind = 1;

        BW_CHECK_EQ("its status changes", bw_bh_sick(&kind), 1);
        BW_CHECK_EQ("to burn", (long)kind, 4);
    }
    bw_bh_leave();
    BW_CHECK_EQ("back in Blaze: no chance of its own", (long)bw_bh_chance(30), 30);
    bw_bh_leave();
    BW_CHECK_EQ("after: the game's numbers", (long)bw_bh_ratio(6144), 6144);

    printf("a handler row with no place\n");
    bw_bh_enter(BW_BH_ABILITY, 165, 5);
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("a row the table does not list: as it was", stat == 1 && amount == -1, 1);
    bw_bh_leave();

    printf("several bases\n");
    bw_bh_clear();
    BW_CHECK_EQ("Intimidate + Levitate", line(BW_BH_ABILITY, "190 = 22 stat=spatk stages=-2 + 26"), 0);
    BW_CHECK_EQ("Levitate + Truant (both keep state in slot 0)", line(BW_BH_ABILITY, "191 = 26 + 54"), 0);
    BW_CHECK_EQ("one base twice", line(BW_BH_ABILITY, "192 = 22 + 22"), 0);
    BW_CHECK_EQ("five bases", line(BW_BH_ABILITY, "193 = 22 + 26 + 66 + 9 + 24"), -1);
    BW_CHECK_EQ("a + with no base", line(BW_BH_ABILITY, "193 = 22 +"), -1);
    BW_CHECK_EQ("Blaze + Static, keys on each", line(BW_BH_ABILITY, "194 = 66 ratio=2.0 + 9 chance=50"), 0);
    bw_bh_check();
    BW_CHECK_EQ("the line of two", (long)bw_bh_parts(BW_BH_ABILITY, 190), 2);
    BW_CHECK_EQ("its first base", (long)bw_bh_part_base(BW_BH_ABILITY, 190, 0), INTIMIDATE);
    BW_CHECK_EQ("its second base", (long)bw_bh_part_base(BW_BH_ABILITY, 190, 1), 26);
    BW_CHECK_EQ("the first base's key", bw_bh_part_key(BW_BH_ABILITY, 190, 0, BW_BH_KEY_STAT, 1, &v) && v == 3, 1);
    BW_CHECK_EQ("a shared work slot: left out", (long)bw_bh_parts(BW_BH_ABILITY, 191), 1);
    BW_CHECK_EQ("the left-out line is its own base", (long)bw_bh_base(BW_BH_ABILITY, 191), 191);
    BW_CHECK_EQ("one base twice: left out", (long)bw_bh_parts(BW_BH_ABILITY, 192), 1);
    {
        /* the joined table: Intimidate's 2 rows, then Levitate's 3 (rows here are ints) */
        static const int intim[2] = { 100, 101 }, lev[3] = { 200, 201, 202 };
        const void *tables[2] = { intim, lev };
        const u32 counts[2] = { 2, 3 };
        u32 total = 0;
        const int *joined = (const int *)bw_bh_join(BW_BH_ABILITY, 190, 2, tables, counts, sizeof(int), &total);

        BW_REQUIRE("joined", joined != NULL);
        BW_CHECK_EQ("five rows", (long)total, 5);
        BW_CHECK_EQ("in line order", joined[0] == 100 && joined[1] == 101 && joined[2] == 200 && joined[4] == 202, 1);
        BW_CHECK_EQ("made once", bw_bh_join(BW_BH_ABILITY, 190, 2, tables, counts, sizeof(int), &total) == joined, 1);
        /* the dispatch runs each base's first answering row: after a row, Levitate's rows start at 2 */
        BW_CHECK_EQ("after Intimidate's row 0: row 2", (long)bw_bh_row_end(BW_BH_ABILITY, 190, 0), 2);
        BW_CHECK_EQ("after Intimidate's row 1: row 2", (long)bw_bh_row_end(BW_BH_ABILITY, 190, 1), 2);
        BW_CHECK_EQ("after Levitate's row 3: the end", (long)bw_bh_row_end(BW_BH_ABILITY, 190, 3), 5);
        BW_CHECK_EQ("one base: the game's stop", (long)bw_bh_row_end(BW_BH_ABILITY, 22, 0), 0);
    }
    /* row 0 is Intimidate's: its key; row 2 is Levitate's first: Intimidate's key does not reach it */
    bw_bh_enter(BW_BH_ABILITY, 190, 0);
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("Intimidate's row: Sp. Atk -2", stat == 3 && amount == -2, 1);
    bw_bh_leave();
    bw_bh_enter(BW_BH_ABILITY, 190, 2);
    stat = 1;
    amount = -1;
    bw_bh_rank(&stat, &amount);
    BW_CHECK_EQ("Levitate's row: not Intimidate's key", stat == 1 && amount == -1, 1);
    bw_bh_leave();
    {
        static const int blaze[1] = { 1 }, stat_[1] = { 2 };
        const void *tables[2] = { blaze, stat_ };
        const u32 counts[2] = { 1, 1 };
        u32 total = 0;

        (void)bw_bh_join(BW_BH_ABILITY, 194, 2, tables, counts, sizeof(int), &total);
        bw_bh_enter(BW_BH_ABILITY, 194, 0);
        BW_CHECK_EQ("Blaze's row: its ratio", (long)bw_bh_ratio(6144), 8192);
        BW_CHECK_EQ("Blaze's row: not Static's chance", (long)bw_bh_chance(30), 30);
        bw_bh_leave();
        bw_bh_enter(BW_BH_ABILITY, 194, 1);
        BW_CHECK_EQ("Static's row: its chance", (long)bw_bh_chance(30), 50);
        BW_CHECK_EQ("Static's row: not Blaze's ratio", (long)bw_bh_ratio(6144), 6144);
        bw_bh_leave();
    }
    bw_bh_clear();
    BW_CHECK_EQ("the built-in one tuned, and one more", line(BW_BH_ABILITY, "22 = 22 stages=-2 + 2"), 0);
    bw_bh_check();
    BW_CHECK_EQ("still two bases", (long)bw_bh_parts(BW_BH_ABILITY, 22), 2);
    BW_CHECK_EQ("its own id is the built-in one", (long)bw_bh_part_base(BW_BH_ABILITY, 22, 0), INTIMIDATE);
    BW_CHECK_EQ("and Drizzle", (long)bw_bh_part_base(BW_BH_ABILITY, 22, 1), 2);

    printf("the game's move-id checks follow the base\n");
    bw_bh_clear();
    BW_CHECK_EQ("no lines: a move is itself", bw_bh_move_is(182, 182) && !bw_bh_move_is(574, 182), 1);
    line(BW_BH_MOVE, "574 = 182");
    line(BW_BH_MOVE, "575 = 574");
    line(BW_BH_MOVE, "576 = 85 + 182");
    line(BW_BH_MOVE, "197 = 33");
    line(BW_BH_MOVE, "577 = misty_terrain");
    bw_bh_check();
    BW_CHECK_EQ("a copy of Protect is Protect", bw_bh_move_is(574, 182), 1);
    BW_CHECK_EQ("through a chain", bw_bh_move_is(575, 182), 1);
    BW_CHECK_EQ("a second part counts", bw_bh_move_is(576, 182) && bw_bh_move_is(576, 85), 1);
    BW_CHECK_EQ("Detect working like Tackle is not Detect", bw_bh_move_is(197, 197), 0);
    BW_CHECK_EQ("a built-in with no line is itself", bw_bh_move_is(182, 182) && !bw_bh_move_is(182, 197), 1);
    BW_CHECK_EQ("an eidos base is no game move", bw_bh_move_is(577, 0), 0);
    {
        static const u16 protects[5] = { 182, 197, 203, 469, 501 };

        BW_CHECK_EQ("a list takes the copy", bw_bh_move_in(574, protects, 5), 1);
        BW_CHECK_EQ("and not Detect working like Tackle", bw_bh_move_in(197, protects, 5), 0);
        BW_CHECK_EQ("nor an unrelated move", bw_bh_move_in(33, protects, 5), 0);
    }

    printf("the game's ability-id checks follow the base\n");
    bw_bh_clear();
    BW_CHECK_EQ("no lines: an ability is itself", bw_bh_ability_is(104, 104) && !bw_bh_ability_is(165, 104), 1);
    line(BW_BH_ABILITY, "165 = 104");
    line(BW_BH_ABILITY, "166 = 26 + 24");
    line(BW_BH_ABILITY, "23 = 22");
    bw_bh_check();
    BW_CHECK_EQ("a copy of Mold Breaker is Mold Breaker", bw_bh_ability_is(165, 104), 1);
    BW_CHECK_EQ("each part of a line counts", bw_bh_ability_is(166, 26) && bw_bh_ability_is(166, 24), 1);
    BW_CHECK_EQ("Shadow Tag working like Intimidate is not Shadow Tag", bw_bh_ability_is(23, 23), 0);
    {
        static const u16 rough[2] = { 160, 24 };

        BW_CHECK_EQ("a list takes the second part", bw_bh_ability_in(166, rough, 2), 1);
    }
    {
        static const int levitate[3] = { 1, 2, 3 }, rough_skin[1] = { 4 };
        const void *tables[2] = { levitate, rough_skin };
        const u32 counts[2] = { 3, 1 };
        u32 total = 0;

        (void)bw_bh_join(BW_BH_ABILITY, 166, 2, tables, counts, sizeof(int), &total);
        bw_bh_checking(BW_BH_ABILITY, 166, 2);
        BW_CHECK_EQ("a skip check on Levitate's row sees Levitate", (long)bw_bh_checked_base(BW_BH_ABILITY, 166), 26);
        bw_bh_checking(BW_BH_ABILITY, 166, 3);
        BW_CHECK_EQ("on Rough Skin's row, Rough Skin", (long)bw_bh_checked_base(BW_BH_ABILITY, 166), 24);
        BW_CHECK_EQ("the row after Levitate's goes on at Rough Skin's", (long)bw_bh_row_end(BW_BH_ABILITY, 166, 1), 3);
        bw_bh_enter(BW_BH_ABILITY, 166, 3);
        BW_CHECK_EQ("the running base is Rough Skin", (long)bw_bh_running_base(BW_BH_ABILITY, 166), 24);
        bw_bh_leave();
        BW_CHECK_EQ("outside a handler, the first base", (long)bw_bh_running_base(BW_BH_ABILITY, 166), 26);
    }

    printf("eidos's own ability bases\n");
    bw_bh_clear();
    BW_CHECK_EQ("a named base", line(BW_BH_ABILITY, "170 = pixilate ratio=1.3"), 0);
    BW_CHECK_EQ("a building block with a kind", line(BW_BH_ABILITY, "171 = boost_kind kind=bite ratio=1.5"), 0);
    BW_CHECK_EQ("a kind that is none", line(BW_BH_ABILITY, "172 = boost_kind kind=claw"), -1);
    BW_CHECK_EQ("several named bases", line(BW_BH_ABILITY, "172 = protean + magician + 26"), 0);
    BW_CHECK_EQ("a move name is no ability base", line(BW_BH_ABILITY, "173 = misty_terrain"), -1);
    BW_CHECK_EQ("an ability name is no move base", line(BW_BH_MOVE, "600 = pixilate"), -1);
    BW_CHECK_EQ("a priority", line(BW_BH_ABILITY, "174 = priority_type type=fire priority=2"), 0);
    BW_CHECK_EQ("two veils, each with its own slots", line(BW_BH_ABILITY, "175 = sweet_veil + aroma_veil"), 0);
    BW_CHECK_EQ("two bases of one slot", line(BW_BH_ABILITY, "177 = cheek_pouch + parental_bond"), 0);
    BW_CHECK_EQ("two forms", line(BW_BH_ABILITY, "178 = stance_change form=2 form.2=1"), 0);
    BW_CHECK_EQ("a form past 31", line(BW_BH_ABILITY, "179 = stance_change form=32"), -1);
    BW_CHECK_EQ("a key the base has no place for", line(BW_BH_ABILITY, "176 = protean ratio=2.0"), 0);
    bw_bh_check();
    BW_CHECK_EQ("the named base", (long)bw_bh_base(BW_BH_ABILITY, 170), BW_BH_PIXILATE);
    BW_CHECK_EQ("its name", strcmp(bw_bh_ability_named_name(BW_BH_PIXILATE), "pixilate"), 0);
    BW_CHECK_EQ("the kind's value", bw_bh_key(BW_BH_ABILITY, 171, BW_BH_KEY_KIND, 1, &v) && v == 4, 1);
    BW_CHECK_EQ("the line of two veils is kept", (long)bw_bh_parts(BW_BH_ABILITY, 175), 2);
    BW_CHECK_EQ("the line of two bases of one slot is left out", (long)bw_bh_parts(BW_BH_ABILITY, 177), 1);
    BW_CHECK_EQ("the attack form", bw_bh_key(BW_BH_ABILITY, 178, BW_BH_KEY_FORM, 1, &v) && v == 2, 1);
    BW_CHECK_EQ("the guard form", bw_bh_key(BW_BH_ABILITY, 178, BW_BH_KEY_FORM, 2, &v) && v == 1, 1);
    BW_CHECK_EQ("the ratio protean has no place for is left out",
                bw_bh_key(BW_BH_ABILITY, 176, BW_BH_KEY_RATIO, 1, &v), 0);
    BW_CHECK_EQ("a named base is no game ability", bw_bh_ability_is(170, 0), 0);
    bw_bh_enter(BW_BH_ABILITY, 170, 1);
    BW_CHECK_EQ("the running handler reads its ratio", (long)bw_bh_arg("ratio", 1, 4915), 5325);
    BW_CHECK_EQ("and the default of a key the line has not", (long)bw_bh_arg("type", 1, 18), 18);
    bw_bh_leave();
    BW_CHECK_EQ("outside a handler, the default", (long)bw_bh_arg("ratio", 1, 4915), 4915);

    printf("move kinds\n");
    bw_move_kinds_clear();
    BW_CHECK_EQ("Crunch bites", (bw_move_kinds(242) & BW_MOVE_KIND_BITE) != 0, 1);
    BW_CHECK_EQ("Aura Sphere is a pulse and a ball", bw_move_kinds(396), BW_MOVE_KIND_PULSE | BW_MOVE_KIND_BALL);
    BW_CHECK_EQ("Tackle is none", bw_move_kinds(33), 0);
    BW_CHECK_EQ("an added move that bites", bw_move_kinds_parse_line("560 = bite", why, sizeof why), 0);
    BW_CHECK_EQ("Bite that is none", bw_move_kinds_parse_line("44 = none  # not a bite", why, sizeof why), 0);
    BW_CHECK_EQ("a kind that is none", bw_move_kinds_parse_line("33 = claw", why, sizeof why), -1);
    BW_CHECK_EQ("no kind", bw_move_kinds_parse_line("33 =", why, sizeof why), -1);
    BW_CHECK_EQ("the line's kind", bw_move_kinds(560), BW_MOVE_KIND_BITE);
    BW_CHECK_EQ("the line wins over the game's", bw_move_kinds(44), 0);
    bw_move_kinds_clear();
    BW_CHECK_EQ("cleared: the game's again", bw_move_kinds(44), BW_MOVE_KIND_BITE);
    BW_CHECK_EQ("Explosion hits once", bw_move_kinds(153), BW_MOVE_KIND_ONE_HIT);
    BW_CHECK_EQ("an added move that hits once", bw_move_kinds_parse_line("561 = one_hit ball", why, sizeof why), 0);
    BW_CHECK_EQ("its kinds", bw_move_kinds(561), BW_MOVE_KIND_ONE_HIT | BW_MOVE_KIND_BALL);
    BW_CHECK_EQ("Fling that is none", bw_move_kinds_parse_line("374 = none", why, sizeof why), 0);
    BW_CHECK_EQ("Fling hits twice then", bw_move_kinds(374), 0);
    bw_move_kinds_clear();

    (void)why;
    bw_bh_clear();
    BW_REPORT("test_battle_handlers");
}
