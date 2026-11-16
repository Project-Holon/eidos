/*
 * tests/test_save_games.c -- the record of the saves the Multiplayer
 * screen's cards made (sdk/save_games.h): a record waits for the new
 * game's first save, a plain new game takes the slot back, and the list
 * comes newest first with saves that are still there only.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "save.h"
#include "save_slots.h"
#include "save_games.h"

#ifdef _WIN32
#include <direct.h>
#define BW_RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define BW_RMDIR(p) rmdir(p)
#endif

#define T_DIR "test_save_games_tmp"

/* a slot's file with some bytes */
static int put_slot(int n)
{
    char path[512];

    if (bw_slots_path(n, path, sizeof path) != 0) {
        return -1;
    }
    return bw_save_write_atomic(path, "save", 4);
}

static void clean(void)
{
    char path[512];
    int n;

    for (n = 1; n <= BW_SLOTS_MAX; n++) {
        if (bw_slots_path(n, path, sizeof path) == 0) {
            remove(path);
        }
    }
    remove(T_DIR "/games.ini");
    remove(T_DIR "/games.ini.tmp");
    BW_RMDIR(T_DIR "/slots");
    BW_RMDIR(T_DIR);
}

int main(void)
{
    BwGame g;

    bw_set_save_dir(T_DIR);
    clean();

    printf("a record waits for the first save\n");
    bw_games_pending(BW_GAME_WORLD, BW_GAME_HOST, "arkhipelagos", "multiworld", NULL);
    BW_CHECK_EQ("no slot is hidden yet", bw_games_hidden(2), 0);
    bw_games_world_code("ABC123");      /* the relay made the world before the save */
    BW_REQUIRE("the slot's file", put_slot(2) == 0);
    bw_games_saved(2);
    BW_CHECK_EQ("now its slot is a card's", bw_games_hidden(2), 1);
    BW_CHECK_EQ("one game", (long)bw_games_count(), 1);
    BW_REQUIRE("it reads back", bw_games_at(0, &g) == 0);
    BW_CHECK_EQ("its slot", (long)g.slot, 2);
    BW_CHECK_EQ("a world", (long)g.kind, BW_GAME_WORLD);
    BW_CHECK_EQ("made here", (long)g.role, BW_GAME_HOST);
    BW_CHECK_EQ("the code the relay gave", strcmp(g.code, "ABC123"), 0);
    BW_CHECK_EQ("the mode", strcmp(g.mode, "multiworld"), 0);

    printf("a joined world and a solo game\n");
    bw_games_pending(BW_GAME_WORLD, BW_GAME_JOINED, "arkhipelagos", "multiworld", "XYZ789");
    BW_REQUIRE("slot 3", put_slot(3) == 0);
    bw_games_saved(3);
    bw_games_pending(BW_GAME_SOLO, BW_GAME_HOST, "arkhipelagos", "solo", NULL);
    bw_games_pending_clear();           /* reset before its first save */
    BW_REQUIRE("slot 4, a plain game", put_slot(4) == 0);
    bw_games_saved(4);
    BW_CHECK_EQ("a dropped record names no slot", bw_games_hidden(4), 0);
    BW_CHECK_EQ("two games", (long)bw_games_count(), 2);
    {
        int i, joined = 0;

        for (i = 0; i < 2; i++) {
            if (bw_games_at(i, &g) == 0 && g.slot == 3) {
                joined = g.role == BW_GAME_JOINED && strcmp(g.code, "XYZ789") == 0;
            }
        }
        BW_CHECK_EQ("the joined one keeps its code", joined, 1);
    }

    printf("a plain new game takes a slot back\n");
    bw_games_saved(2);                  /* no record waits */
    BW_CHECK_EQ("slot 2 is the player's own again", bw_games_hidden(2), 0);
    BW_CHECK_EQ("one game left", (long)bw_games_count(), 1);

    printf("the record outlives the run\n");
    BW_CHECK_EQ("the file is there", bw_save_read(T_DIR "/games.ini", NULL, 0) > 0, 1);

    printf("a save that is gone is not listed\n");
    {
        char path[512];

        if (bw_slots_path(3, path, sizeof path) == 0) {
            remove(path);
        }
    }
    BW_CHECK_EQ("no game", (long)bw_games_count(), 0);
    BW_CHECK_EQ("no entry", bw_games_at(0, &g), -1);

    clean();
    BW_REPORT("test_save_games");
}
