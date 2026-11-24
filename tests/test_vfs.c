/*
 * tests/test_vfs.c -- the file layer, synthetically.
 *
 * No game data and no oracle: a small tree is written under the current
 * directory (the ctest build directory), with a base root and a mod root
 * that disagree about one file, and every expectation is about which copy
 * the resolver picks and whether the resolved path really reads back.
 *
 * What is checked:
 *
 *   - with no mod root, a path resolves under the base root;
 *   - with a mod root, a file present in both resolves to the overlay copy
 *     and reads back as the overlay's bytes;
 *   - a file present only in the base falls through to the base;
 *   - a file present only in the overlay resolves to the overlay;
 *   - bw_data_exists agrees with the resolution, absent file included;
 *   - clearing the mod root restores base-only resolution;
 *   - a trailing separator on a root is stripped, so resolution does not
 *     produce a doubled slash.
 *
 * Member-level overrides and the multi-mod load order are
 * tests/test_vfs_members.c.
 */

#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_test.h"
#include "vfs.h"

#ifdef _WIN32
#include <direct.h>
#define BW_MKDIR(p) _mkdir(p)
#define BW_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BW_MKDIR(p) mkdir((p), 0777)
#define BW_RMDIR(p) rmdir(p)
#endif

#define T_ROOT "vfs_test_tmp"
#define T_BASE T_ROOT "/base"
#define T_MOD  T_ROOT "/mod"
#define T_A    T_ROOT "/a"
#define T_B    T_ROOT "/b"
#define T_C    T_ROOT "/c"

static int write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    fclose(fp);
    return 0;
}

/* Reads a whole file and returns 1 if it is exactly `text`, else 0. */
static int file_is(const char *path, const char *text)
{
    char buf[64];
    size_t n;
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void build_tree(void)
{
    BW_RMDIR(T_BASE);
    BW_RMDIR(T_MOD);
    BW_RMDIR(T_ROOT);

    BW_MKDIR(T_ROOT);
    BW_MKDIR(T_BASE);
    BW_MKDIR(T_MOD);

    write_file(T_BASE "/shared.txt", "base");
    write_file(T_BASE "/only_base.txt", "b");
    write_file(T_MOD "/shared.txt", "mod");
    write_file(T_MOD "/only_mod.txt", "m");
}
static void clean_tree(void)
{
    remove(T_BASE "/shared.txt");
    remove(T_BASE "/only_base.txt");
    remove(T_MOD "/shared.txt");
    remove(T_MOD "/only_mod.txt");
    remove(T_A "/eidos_mod.txt");
    remove(T_B "/eidos_mod.txt");
    remove(T_C "/eidos_mod.txt");
    BW_RMDIR(T_BASE);
    BW_RMDIR(T_MOD);
    BW_RMDIR(T_A);
    BW_RMDIR(T_B);
    BW_RMDIR(T_C);
    BW_RMDIR(T_ROOT);
}

int main(void)
{
    char path[1200];

    build_tree();

    printf("base only (no mod root)\n");
    bw_set_data_root(T_BASE);
    bw_set_mod_root("");
    BW_REQUIRE("mod root is empty", bw_mod_root()[0] == '\0');

    BW_CHECK_EQ("resolve succeeds", bw_data_path(path, sizeof(path), "shared.txt"), 0);
    BW_CHECK_EQ("resolves under the base root", starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("the base copy reads back", file_is(path, "base"), 1);
    BW_CHECK_EQ("exists: base file", bw_data_exists("shared.txt"), 1);
    BW_CHECK_EQ("exists: absent file", bw_data_exists("absent.txt"), 0);

    printf("mod overlay on\n");
    bw_set_mod_root(T_MOD);
    BW_REQUIRE("mod root is set", bw_mod_root()[0] != '\0');

    BW_CHECK_EQ("shared resolves", bw_data_path(path, sizeof(path), "shared.txt"), 0);
    BW_CHECK_EQ("overlay copy wins", starts_with(path, T_MOD), 1);
    BW_CHECK_EQ("the overlay copy reads back", file_is(path, "mod"), 1);

    BW_CHECK_EQ("base-only resolves", bw_data_path(path, sizeof(path), "only_base.txt"), 0);
    BW_CHECK_EQ("base-only falls through", starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("the base-only copy reads back", file_is(path, "b"), 1);

    BW_CHECK_EQ("overlay-only resolves", bw_data_path(path, sizeof(path), "only_mod.txt"), 0);
    BW_CHECK_EQ("overlay-only uses the overlay", starts_with(path, T_MOD), 1);
    BW_CHECK_EQ("exists: overlay file", bw_data_exists("only_mod.txt"), 1);
    BW_CHECK_EQ("exists: base file still found", bw_data_exists("only_base.txt"), 1);

    printf("trailing separator on a root\n");
    bw_set_mod_root(T_MOD "/");
    BW_CHECK_EQ("shared still resolves to the overlay",
                bw_data_path(path, sizeof(path), "shared.txt") == 0
                && starts_with(path, T_MOD), 1);

    printf("mod root cleared\n");
    bw_set_mod_root("");
    BW_CHECK_EQ("only_mod now resolves under the base",
                bw_data_path(path, sizeof(path), "only_mod.txt") == 0
                && starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("and it no longer exists", bw_data_exists("only_mod.txt"), 0);

    printf("the order the player chose\n");
    /* Three mods, each with a manifest whose order key says where it
     * belongs: the port's own list is sorted by that key. */
    BW_MKDIR(T_A);
    BW_MKDIR(T_B);
    BW_MKDIR(T_C);
    write_file(T_A "/eidos_mod.txt", "name = A\norder = 30\n");
    write_file(T_B "/eidos_mod.txt", "name = B\norder = 10\n");
    write_file(T_C "/eidos_mod.txt", "name = C\norder = 20\n");
    bw_set_mod_root("");
    bw_add_mod_root(T_A);
    bw_add_mod_root(T_B);
    bw_add_mod_root(T_C);
    BW_CHECK_EQ("the list is in the manifest's order",
                bw_mod_count(), 3);
    BW_CHECK_EQ("the first is the one with the lowest key",
                strcmp(bw_mod_root_at(0), T_B), 0);
    BW_CHECK_EQ("the last is the one with the highest key",
                strcmp(bw_mod_root_at(2), T_A), 0);
    {
        const char *order[3];

        order[0] = T_C;
        order[1] = T_A;
        order[2] = T_B;
        BW_CHECK_EQ("the player's order is taken",
                    bw_mod_set_order(order, 3), 0);
        BW_CHECK_EQ("and the list is in it",
                    strcmp(bw_mod_root_at(0), T_C), 0);
        BW_CHECK_EQ("the second is the player's too",
                    strcmp(bw_mod_root_at(1), T_A), 0);
        BW_CHECK_EQ("and the third",
                    strcmp(bw_mod_root_at(2), T_B), 0);
    }
    bw_set_mod_root("");
    BW_CHECK_EQ("clearing the list drops the player's order",
                bw_mod_count(), 0);
    bw_add_mod_root(T_A);
    bw_add_mod_root(T_B);
    bw_add_mod_root(T_C);
    BW_CHECK_EQ("and the manifest's key is back in charge",
                strcmp(bw_mod_root_at(0), T_B), 0);
    bw_set_mod_root("");

    /* The game a mod is made for (the manifest's `game` line): a mod for the
     * other game is not added.  The game loaded is the code in the header.bin
     * beside the data folder; with none, nothing is refused. */
    BW_CHECK_EQ("black names Black", bw_mod_games_of("black", 5), BW_MOD_GAME_BLACK);
    BW_CHECK_EQ("White, in any case, names White", bw_mod_games_of("White", 5), BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("an empty value names both", bw_mod_games_of("", 0), BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("a Black code of another language names Black", bw_mod_games_of("IRBF", 4), BW_MOD_GAME_BLACK);
    BW_CHECK_EQ("a White code names White", bw_mod_games_of("IRAO", 4), BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("another word names neither", bw_mod_games_of("banana", 6), 0);
    BW_CHECK_EQ("nor does a code of another game", bw_mod_games_of("IREO", 4), 0);
    write_file(T_A "/eidos_mod.txt", "name = A\ngame = white\n");
    write_file(T_B "/eidos_mod.txt", "name = B\ngame = IRBF\n");
    write_file(T_C "/eidos_mod.txt", "name = C\n");
    BW_CHECK_EQ("with no header the game is not known", bw_mod_game_loaded(), 0);
    BW_CHECK_EQ("and a White mod is added", bw_add_mod_root(T_A), 0);
    bw_set_mod_root("");
    {
        static const char head[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'I', 'R', 'B', 'O' };
        FILE *fp = fopen(T_ROOT "/header.bin", "wb");

        BW_REQUIRE("the header is written", fp != NULL && fwrite(head, 1, sizeof head, fp) == sizeof head);
        fclose(fp);
    }
    BW_CHECK_EQ("the header says Black", bw_mod_game_loaded(), BW_MOD_GAME_BLACK);
    BW_CHECK_EQ("the manifest's games are read", bw_mod_games_at(T_A), BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("a mod with no game line works on both", bw_mod_games_at(T_C), BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("on Black a White mod is refused", bw_add_mod_root(T_A), -1);
    BW_CHECK_EQ("and is not in the list", bw_mod_count(), 0);
    BW_CHECK_EQ("a mod made with French Black is added", bw_add_mod_root(T_B), 0);
    BW_CHECK_EQ("a mod with no game line is added", bw_add_mod_root(T_C), 0);
    BW_CHECK_EQ("two mods are in the list", bw_mod_count(), 2);
    bw_set_mod_root("");
    write_file(T_C "/eidos_mod.txt", "name = C\ngame = banana\n");
    BW_CHECK_EQ("a game line that names neither refuses the mod", bw_add_mod_root(T_C), -1);
    {
        const char *order[2];

        order[0] = T_A;
        order[1] = T_B;
        BW_CHECK_EQ("the player's order with a White mod in it reports it", bw_mod_set_order(order, 2), -1);
        BW_CHECK_EQ("and holds the other mod alone", bw_mod_count(), 1);
    }
    bw_set_mod_root("");
    {
        static const char head[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'I', 'R', 'A', 'D' };
        FILE *fp = fopen(T_ROOT "/header.bin", "wb");

        BW_REQUIRE("the header is written again", fp != NULL && fwrite(head, 1, sizeof head, fp) == sizeof head);
        fclose(fp);
    }
    BW_CHECK_EQ("German White is White", bw_mod_game_loaded(), BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("on White the White mod is added", bw_add_mod_root(T_A), 0);
    BW_CHECK_EQ("and the Black mod is refused", bw_add_mod_root(T_B), -1);
    bw_set_mod_root("");
    remove(T_ROOT "/header.bin");

    clean_tree();
    BW_REPORT("test_vfs");
}
