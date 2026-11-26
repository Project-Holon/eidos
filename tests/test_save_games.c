/*
 * tests/test_save_games.c -- the saves of the Multiplayer screen's cards
 * (sdk/save_games.h) and the slots with no limit (sdk/save_slots.h): a
 * card's new game takes a file of games/, a plain one the next free slot
 * of the player's own past any count, a record waits for the new game's
 * first save, and the list comes newest first with saves that are still
 * there only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "save.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "save_slots.h"
#include "save_games.h"

#define T_DIR "test_save_games_tmp"

/* a slot's file with some bytes */
static int put_slot(int n)
{
    char path[512], dir[512];

    if (bw_slots_path(n, path, sizeof path) != 0) {
        return -1;
    }
    snprintf(dir, sizeof dir, "%s/%s", T_DIR, n < 0 ? "games" : "slots");
    (void)bw_plat_mkdirs(dir);
    return bw_save_write_atomic(path, "save", 4);
}

int main(void)
{
    BwGame g;
    int n;

    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    bw_set_save_dir(T_DIR);

    printf("slots and games have their own files\n");
    {
        char path[512];

        BW_REQUIRE("slot 1", bw_slots_path(1, path, sizeof path) == 0);
        BW_CHECK_EQ("is pokeblack.sav", strcmp(path, T_DIR "/pokeblack.sav"), 0);
        BW_REQUIRE("slot 12", bw_slots_path(12, path, sizeof path) == 0);
        BW_CHECK_EQ("past 8", strcmp(path, T_DIR "/slots/save12.sav"), 0);
        BW_REQUIRE("game 3", bw_slots_path(-3, path, sizeof path) == 0);
        BW_CHECK_EQ("in games/", strcmp(path, T_DIR "/games/game3.sav"), 0);
        BW_CHECK_EQ("slot 0 is none", bw_slots_path(0, path, sizeof path), -1);
    }

    printf("a plain new game takes the next slot, past any count\n");
    for (n = 1; n <= 10; n++) {
        BW_REQUIRE("a save", put_slot(n) == 0);
    }
    BW_CHECK_EQ("it moved to a free slot", bw_slots_new_game(), 1);
    BW_CHECK_EQ("slot 11", (long)bw_slots_current(), 11);
    bw_slots_dropped();                 /* never saved: the slot before again */

    printf("a card's new game takes a file of games/\n");
    bw_games_pending(BW_GAME_WORLD, BW_GAME_HOST, "arkhipelagos", "multiworld", NULL);
    BW_CHECK_EQ("it waits", bw_games_has_pending(), 1);
    BW_CHECK_EQ("it moved to a free file", bw_slots_new_game(), 1);
    BW_CHECK_EQ("games/game1.sav", (long)bw_slots_current(), -1);
    bw_games_world_code("ABC123");      /* the relay made the world before the save */
    BW_REQUIRE("its file", put_slot(-1) == 0);
    bw_games_saved(-1);
    BW_CHECK_EQ("no longer waiting", bw_games_has_pending(), 0);
    BW_CHECK_EQ("a card's game is hidden", bw_games_hidden(-1), 1);
    BW_CHECK_EQ("the player's own slots are not", bw_games_hidden(2), 0);
    BW_CHECK_EQ("one game", (long)bw_games_count(), 1);
    BW_REQUIRE("it reads back", bw_games_at(0, &g) == 0);
    BW_CHECK_EQ("its slot", (long)g.slot, -1);
    BW_CHECK_EQ("a world", (long)g.kind, BW_GAME_WORLD);
    BW_CHECK_EQ("made here", (long)g.role, BW_GAME_HOST);
    BW_CHECK_EQ("the code the relay gave", strcmp(g.code, "ABC123"), 0);
    BW_CHECK_EQ("the mode", strcmp(g.mode, "multiworld"), 0);
    BW_CHECK_EQ("the record is games/games.ini", bw_save_read(T_DIR "/games/games.ini", NULL, 0) > 0, 1);

    printf("the next card's game takes the next file\n");
    bw_games_pending(BW_GAME_WORLD, BW_GAME_JOINED, "arkhipelagos", "multiworld", "XYZ789");
    BW_CHECK_EQ("moved", bw_slots_new_game(), 1);
    BW_CHECK_EQ("games/game2.sav", (long)bw_slots_current(), -2);
    BW_REQUIRE("its file", put_slot(-2) == 0);
    bw_games_saved(-2);
    BW_CHECK_EQ("two games", (long)bw_games_count(), 2);
    {
        int i, joined = 0;

        for (i = 0; i < 2; i++) {
            if (bw_games_at(i, &g) == 0 && g.slot == -2) {
                joined = g.role == BW_GAME_JOINED && strcmp(g.code, "XYZ789") == 0;
            }
        }
        BW_CHECK_EQ("the joined one keeps its code", joined, 1);
    }

    printf("a plain new game leaves the records alone\n");
    bw_games_pending(BW_GAME_SOLO, BW_GAME_HOST, "arkhipelagos", "solo", NULL);
    bw_games_pending_clear();           /* reset before its first save */
    bw_games_saved(11);
    BW_CHECK_EQ("still two games", (long)bw_games_count(), 2);
    BW_CHECK_EQ("slot 11 is the player's own", bw_games_hidden(11), 0);

    printf("an older eidos's games.ini still hides its slot\n");
    BW_REQUIRE("games.ini", bw_save_write_atomic(T_DIR "/games.ini",
                                                 "# old\n5 solo arkhipelagos solo host -\n", 38) == 0);
    bw_set_save_dir(T_DIR "/../" T_DIR);  /* (another folder name: the list is read again) */
    (void)bw_games_count();
    bw_set_save_dir(T_DIR);
    BW_CHECK_EQ("slot 5 hidden", bw_games_hidden(5), 1);
    BW_CHECK_EQ("slot 6 not", bw_games_hidden(6), 0);

    printf("a save that is gone is not listed\n");
    {
        char path[512];

        if (bw_slots_path(-2, path, sizeof path) == 0) {
            remove(path);
        }
    }
    BW_CHECK_EQ("one game", (long)bw_games_count(), 1);
    BW_CHECK_EQ("no second entry", bw_games_at(1, &g), -1);

    (void)bw_plat_remove_tree(T_DIR);
    BW_REPORT("test_save_games");
}
