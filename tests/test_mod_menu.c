/*
 * tests/test_mod_menu.c -- the mod menu's model: the installed mods, the
 * switch, the order, the clear action, the orphan report and a mod's own
 * settings.
 *
 * The model is sdk/mod_menu.c; the contract it keeps is the one of
 * docs/mod-state.md, section 2, and of docs/mod-services.md, "The mod
 * menu":
 *
 *   - the list: the installed mods in load order, each with the name, the
 *     namespace, the settings path and the code flag its own manifest
 *     gives, and a mod the loader never loaded is shown too;
 *   - the switch: the player's choice is held until the player leaves the
 *     menu, and the apply runs the documented order (the mods' state is
 *     flushed, the running mods are stopped, the event layer and the rules
 *     are reset, the roots are rebuilt, the wanted mods are loaded again
 *     and the record is written);
 *   - the move: the player can move a mod up, down and to a place, and the
 *     order the mods load in is the player's order (bw_mod_set_order,
 *     sdk/vfs.h);
 *   - the clear action: the mod's own state file is deleted and its folder
 *     stays (docs/mod-state.md, section 1, "Removal");
 *   - the orphan report: the folders under <save dir>/mods/ that no mod
 *     claims are named, and a folder that is a mod's is not;
 *   - the setting: the options the manifest's declaration names are read
 *     and written, and the choices are stored beside the mod's saved data;
 *   - the record: the player's list is written and read back, and with no
 *     record the port starts with no mods.
 *
 * Nothing here needs game data: the mod tree and the save folder are
 * directories in the test's own working directory, the way
 * tests/test_mod_manifest.c and tests/test_mod_savedata.c write theirs.
 *
 * The loader, the event layer and the rule layer are stood in for: the
 * model drives them in a fixed order and the test watches that order, and a
 * real loader would want a real library on the disk (tests/test_mod_loader.c
 * is the test that loads one).  The saved-data layer is the port's own
 * (sdk/mod_savedata.c): the clear action and the flush are checked against
 * the real files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_event.h"
#include "mod_loader.h"
#include "mod_menu.h"
#include "mod_rule.h"
#include "plat_dir.h"
#include "save.h"
#include "types.h"
#include "vfs.h"

#include "port_test.h"

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

/*
 * The saved-data layer's own calls (sdk/mod_savedata.c).  That file holds
 * its prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses; they are the ones
 * the file's own header comment names.
 */
void        bw_mod_savedata_set_namespace(const char *ns);
void        bw_mod_savedata_set_save(const char *name);
void        bw_mod_savedata_save_start(void);
u32         bw_mod_savedata_save_check(void);
int         bw_mod_savedata_save_done(int ok, u32 check);
s32         bw_mod_savedata_load(void *buf, u32 size);
int         bw_mod_savedata_store(const void *buf, u32 size);
int         bw_mod_savedata_clear(void);

/* ------------------------------------------------------------------ */
/* The tree the test writes                                            */
/* ------------------------------------------------------------------ */

#define T_ROOT   "mod_menu_tmp"
#define T_MOD_A  T_ROOT "/a"
#define T_MOD_B  T_ROOT "/b"
#define T_MOD_C  T_ROOT "/c"
#define T_MOD_D  T_ROOT "/d"
#define T_SAVE   T_ROOT "/save"

/* The folder the mods' own folders live in. */
#define T_MODS   T_SAVE "/mods"

/* The namespace of the mod the test watches. */
#define T_NS     "alpha"

/* The game save the mods' own files belong to. */
#define T_SAVE_NAME "pokeblack"

/* Make a folder and the folders above it. */
static int make_dirs(const char *path)
{
    char buf[512];
    int n;
    size_t i;

    n = snprintf(buf, sizeof buf, "%s", path);
    if (n < 0 || (size_t)n >= sizeof buf) {
        return -1;
    }
    for (i = 1; buf[i] != '\0'; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            (void)BW_MKDIR(buf);
            buf[i] = '/';
        }
    }
    (void)BW_MKDIR(buf);
    return bw_plat_path_kind(buf) == BW_PATH_DIR ? 0 : -1;
}

/* Write a file of text. */
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

/* Read a whole file into `buf`.  NULL when there is none. */
static char *read_file(const char *path, char *buf, size_t size)
{
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL) {
        return NULL;
    }
    n = fread(buf, 1, size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

/* Whether a file is there. */
static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Whether a folder is there. */
static int dir_exists(const char *path)
{
    return bw_plat_path_kind(path) == BW_PATH_DIR;
}

/* One mod's tree: its manifest. */
static int make_mod(const char *dir, const char *manifest)
{
    char path[512];
    int n;

    n = snprintf(path, sizeof path, "%s/eidos_mod.txt", dir);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    return write_file(path, manifest);
}

/* The tree the whole test works on. */
static int build_tree(void)
{
    char path[512];
    int n;

    if (make_dirs(T_MOD_A) != 0 || make_dirs(T_MOD_B) != 0 ||
        make_dirs(T_MOD_C) != 0 || make_dirs(T_MOD_D) != 0) {
        return -1;
    }
    if (make_dirs(T_SAVE) != 0 || make_dirs(T_MODS) != 0) {
        return -1;
    }
    /* The mod's own folder, and one that no mod claims. */
    if (make_dirs(T_MODS "/alpha") != 0 ||
        make_dirs(T_MODS "/left-over") != 0) {
        return -1;
    }
    if (make_mod(T_MOD_A,
                 "name = Alpha\n"
                 "order = 20\n"
                 "state = alpha\n"
                 "settings = alpha.ini\n"
                 "code = alpha.dll\n") != 0) {
        return -1;
    }
    if (make_mod(T_MOD_B,
                 "name = Beta\n"
                 "order = 10\n"
                 "code = beta.dll\n") != 0) {
        return -1;
    }
    /* The third mod ships no code: the loader never loads it. */
    if (make_mod(T_MOD_C,
                 "name = Gamma\n"
                 "order = 30\n"
                 "settings = gamma.ini\n") != 0) {
        return -1;
    }
    /* The fourth mod is never in the port's list: the loader never loads
     * it either. */
    if (make_mod(T_MOD_D,
                 "name = Delta\n"
                 "order = 15\n"
                 "state = delta\n"
                 "code = delta.dll\n") != 0) {
        return -1;
    }
    /* The settings declaration the first mod's manifest names. */
    n = snprintf(path, sizeof path, "%s/alpha.ini", T_MOD_A);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    if (write_file(path,
                   "option nuzlocke.dupes bool \"Dupes clause\" on\n"
                   "option nuzlocke.cap choice \"Level caps\" off|gym|ace\n"
                   "option nuzlocke.lives int \"Lives\" 0 10 3 # a comment\n"
                   "option nuzlocke.picks bool \"No #1 picks\" off\n"
                   "option nuzlocke.typo bool \"Typo\" ture\n"
                   "option nuzlocke.dupes bool \"Twice\" off\n"
                   "option bad=key bool \"Bad key\" on\n"
                   "mode solo \"Solo\" \"On your own.\"\n"
                   "mode world \"World\" \"With friends.\" joins\n"
                   "mode room \"Room\" \"A room's file.\" in=world export \"Game {version} (x)\"\n"
                   "heading \"Rules\"\n"
                   "option t.world choice \"World\" Make|Join @world\n"
                   "option t.code text \"Code\" 6 \"\" ?t.world=Join @world\n"
                   "option t.rule bool \"Rule\" on @solo,world,room ?t.world=make\n"
                   "help t.rule \"What the rule does.\"\n"
                   "yaml t.rule the_rule false|true\n"
                   "help t.nothing \"A help line for no option.\"\n")
        != 0) {
        return -1;
    }
    /* The third mod's declaration: version 7's lines (a tab of its own, a
     * one-word text, a password, buttons and the mod's own rows). */
    n = snprintf(path, sizeof path, "%s/gamma.ini", T_MOD_C);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    if (write_file(path,
                   "option g.under bool \"Under the mod\" on\n"
                   "tab \"Gamma Online\"\n"
                   "heading \"Sign in\"\n"
                   "option g.user text \"User name\" 20 \"\" word ?g.who=\n"
                   "option g.pass secret \"Password\" 64 ?g.who= @lobby\n"
                   "button g.signin \"Sign in\" ?g.who=\n"
                   "show g.who \"Signed in as\"\n"
                   "button g.signout \"Sign out\" ?g.who!=\n"
                   "help g.user \"Your user name.\"\n"
                   "tab \"Gamma Two\"\n"
                   "option g.two bool \"Second tab\" off\n"
                   "option g.bad secret \"Bad\" 8 \"default\"\n")
        != 0) {
        return -1;
    }
    /* A file in the mods folder is not a folder, so it is no orphan. */
    n = snprintf(path, sizeof path, "%s/loose.txt", T_MODS);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    return write_file(path, "not a folder\n");
}

/* Take the tree an earlier run left away. */
static void clean_tree(void)
{
    static const char *const files[] = {
        T_MOD_A "/eidos_mod.txt",
        T_MOD_A "/alpha.ini",
        T_MOD_B "/eidos_mod.txt",
        T_MOD_C "/eidos_mod.txt",
        T_MOD_D "/eidos_mod.txt",
        T_SAVE "/mods.ini",
        T_SAVE "/mods.ini.tmp",
        T_MODS "/loose.txt",
        T_MODS "/alpha/pokeblack.bin",
        T_MODS "/alpha/pokeblack.bin.tmp",
        T_MODS "/alpha/settings.ini",
        T_MODS "/alpha/settings.ini.tmp"
    };
    static const char *const dirs[] = {
        T_MODS "/alpha",
        T_MODS "/left-over",
        T_MODS,
        T_SAVE "/archipelago",
        T_SAVE,
        T_MOD_A,
        T_MOD_B,
        T_MOD_C,
        T_MOD_D,
        T_ROOT
    };
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        remove(files[i]);
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        BW_RMDIR(dirs[i]);
    }
}

/* ------------------------------------------------------------------ */
/* The paths the port's own files have                                 */
/* ------------------------------------------------------------------ */

/* <save dir>/mods.ini: the port's own record of the player's list. */
static int record_path(char *out, size_t size)
{
    return bw_save_path(out, size, "mods.ini");
}

/* <save dir>/mods/<ns>/<save>.bin: the mod's own state file. */
static int state_path(char *out, size_t size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/%s.bin", ns, T_SAVE_NAME);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* <save dir>/mods/<ns>/settings.ini: the player's choices for the mod. */
static int choices_path(char *out, size_t size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/settings.ini", ns);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* ------------------------------------------------------------------ */
/* The layers the model drives, stood in for                          */
/* ------------------------------------------------------------------ */

/*
 * What the model called, in the order it called it.  The apply's order is
 * the contract docs/mod-state.md, section 2, constraint 2, fixes, so the
 * test watches it.
 */
static char g_log[16][24];
static int  g_log_count;

/* The state file the stop must find: the flush comes before it. */
static char g_watch_path[1024];
static int  g_watch_on;
static int  g_flushed_before_stop;

/* How many roots the file layer held when the reload was called. */
static int  g_roots_at_load;

static void note(const char *what)
{
    if (g_log_count < (int)(sizeof g_log / sizeof g_log[0])) {
        int n = snprintf(g_log[g_log_count], sizeof g_log[0], "%s", what);

        if (n < 0 || (size_t)n >= sizeof g_log[0]) {
            g_log[g_log_count][0] = '\0';
        }
    }
    g_log_count++;
}

/* What the model called at `i`, or "". */
static const char *log_at(int i)
{
    if (i < 0 || i >= g_log_count ||
        i >= (int)(sizeof g_log / sizeof g_log[0])) {
        return "";
    }
    return g_log[i];
}

/* The loader: the mods are wanted again. */
int bw_mod_loader_load_all(void)
{
    note("load_all");
    g_roots_at_load = (int)bw_mod_count();
    return 0;
}

/* The loader: the running mods are stopped. */
void bw_mod_loader_stop_all(void)
{
    note("stop_all");
    if (g_watch_on) {
        g_flushed_before_stop = file_exists(g_watch_path);
    }
}

/* The event layer: the port's own rows are bound again. */
void bw_mod_event_reset(void)
{
    note("event_reset");
}

void bw_mod_event_bind_all(void)
{
    note("event_bind");
}

/* The rule layer: the rules are dropped. */
void bw_mod_rule_reset(void)
{
    note("rule_reset");
}

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* The index of the mod with the namespace `ns`, or -1. */
static int index_of_ns(const char *ns)
{
    u32 i;

    for (i = 0; i < bw_mod_menu_count(); i++) {
        if (strcmp(bw_mod_menu_namespace_at(i), ns) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    char path[1024];
    char text[1024];
    u32 index;
    int i;

    bw_set_save_dir(T_SAVE);
    bw_mod_savedata_set_save(T_SAVE_NAME);
    clean_tree();
    BW_REQUIRE("the tree is written", build_tree() == 0);

    printf("the list of the installed mods\n");
    bw_clear_mods();
    BW_REQUIRE("the third mod is added", bw_add_mod_root(T_MOD_C) == 0);
    BW_REQUIRE("the first mod is added", bw_add_mod_root(T_MOD_A) == 0);
    BW_REQUIRE("the second mod is added", bw_add_mod_root(T_MOD_B) == 0);
    bw_mod_menu_open();
    BW_CHECK_EQ("three mods are shown", (long)bw_mod_menu_count(), 3);
    /* The list is the load order, and the load order is the manifest's
     * order key while the player has not said otherwise. */
    BW_CHECK_EQ("the first is the mod with the lowest key",
                strcmp(bw_mod_menu_name_at(0), "Beta"), 0);
    BW_CHECK_EQ("its namespace is the one its name gives",
                strcmp(bw_mod_menu_namespace_at(0), "beta"), 0);
    BW_CHECK_EQ("it names no settings", bw_mod_menu_settings_at(0)[0], 0);
    BW_CHECK_EQ("but it ships code", bw_mod_menu_has_code_at(0), 1);
    BW_CHECK_EQ("the order key is the manifest's",
                (long)bw_mod_menu_order_at(0), 10);
    BW_CHECK_EQ("it is on", bw_mod_menu_enabled_at(0), 1);
    BW_CHECK_EQ("and it is running", bw_mod_menu_running_at(0), 1);
    BW_CHECK_EQ("its folder is there", bw_mod_menu_present_at(0), 1);
    BW_CHECK_EQ("and its root is the folder",
                strcmp(bw_mod_menu_root_at(0), T_MOD_B), 0);
    BW_CHECK_EQ("the second is the mod with the middle key",
                strcmp(bw_mod_menu_name_at(1), "Alpha"), 0);
    BW_CHECK_EQ("its namespace is the manifest's",
                strcmp(bw_mod_menu_namespace_at(1), "alpha"), 0);
    BW_CHECK_EQ("the settings path is the manifest's",
                strcmp(bw_mod_menu_settings_at(1), "alpha.ini"), 0);
    BW_CHECK_EQ("it ships code", bw_mod_menu_has_code_at(1), 1);
    BW_CHECK_EQ("the order key is the manifest's",
                (long)bw_mod_menu_order_at(1), 20);
    BW_CHECK_EQ("the third is the mod with the highest key",
                strcmp(bw_mod_menu_name_at(2), "Gamma"), 0);
    BW_CHECK_EQ("its namespace is the one its name gives",
                strcmp(bw_mod_menu_namespace_at(2), "gamma"), 0);
    BW_CHECK_EQ("the settings path is the manifest's",
                strcmp(bw_mod_menu_settings_at(2), "gamma.ini"), 0);
    BW_CHECK_EQ("a mod the loader never loaded is shown too",
                bw_mod_menu_has_code_at(2), 0);
    BW_CHECK_EQ("and it is in the list all the same",
                (long)bw_mod_menu_count(), 3);

    printf("the switch\n");
    BW_REQUIRE("the switch is set", bw_mod_menu_set_enabled(1, 0) == 0);
    BW_CHECK_EQ("the player's choice is held", bw_mod_menu_enabled_at(1), 0);
    BW_CHECK_EQ("the mod still runs", bw_mod_menu_running_at(1), 1);
    BW_CHECK_EQ("the port's list is untouched", (long)bw_mod_count(), 3);
    BW_CHECK_EQ("and the root is still there",
                strcmp(bw_mod_root_at(1), T_MOD_A), 0);
    BW_CHECK_EQ("there is something to apply", bw_mod_menu_dirty(), 1);
    BW_CHECK_EQ("a mod that is not there is refused",
                bw_mod_menu_set_enabled(9, 1), -1);

    printf("the apply\n");
    /* The mod's own state is held by the save window: the apply has to
     * flush it before it stops the running mods. */
    BW_REQUIRE("the state file's path is known",
               state_path(path, sizeof path, T_NS) == 0);
    bw_mod_savedata_set_namespace(T_NS);
    bw_mod_savedata_save_start();
    BW_REQUIRE("the payload is held",
               bw_mod_savedata_store("payload", 7) == 0);
    BW_CHECK_EQ("and it is not written yet", file_exists(path), 0);
    snprintf(g_watch_path, sizeof g_watch_path, "%s", path);
    g_watch_on = 1;
    g_flushed_before_stop = -1;
    g_roots_at_load = -1;
    g_log_count = 0;
    BW_REQUIRE("the apply succeeds", bw_mod_menu_apply() == 0);
    BW_CHECK_EQ("the state was flushed before the mods were stopped",
                g_flushed_before_stop, 1);
    BW_CHECK_EQ("and the state file is on the disk", file_exists(path), 1);
    BW_CHECK_EQ("the running mods are stopped first",
                strcmp(log_at(0), "stop_all"), 0);
    BW_CHECK_EQ("then the event layer is reset",
                strcmp(log_at(1), "event_reset"), 0);
    BW_CHECK_EQ("and the port's own rows are bound again",
                strcmp(log_at(2), "event_bind"), 0);
    BW_CHECK_EQ("then the rules are dropped",
                strcmp(log_at(3), "rule_reset"), 0);
    BW_CHECK_EQ("and the wanted mods are loaded again",
                strcmp(log_at(4), "load_all"), 0);
    BW_CHECK_EQ("the roots were rebuilt before the reload",
                (long)g_roots_at_load, 2);
    BW_CHECK_EQ("the list holds the mods that are on",
                (long)bw_mod_count(), 2);
    BW_CHECK_EQ("the first is the one that is on",
                strcmp(bw_mod_root_at(0), T_MOD_B), 0);
    BW_CHECK_EQ("and the second",
                strcmp(bw_mod_root_at(1), T_MOD_C), 0);
    BW_CHECK_EQ("the mod that is off is not running",
                bw_mod_menu_running_at(1), 0);
    BW_CHECK_EQ("and its switch is still off",
                bw_mod_menu_enabled_at(1), 0);
    BW_CHECK_EQ("there is nothing left to apply", bw_mod_menu_dirty(), 0);

    printf("the revert\n");
    BW_REQUIRE("the switch is set again", bw_mod_menu_set_enabled(1, 1) == 0);
    BW_REQUIRE("a mod is moved", bw_mod_menu_move_to(2, 0) == 0);
    BW_CHECK_EQ("the order is the player's",
                strcmp(bw_mod_menu_name_at(0), "Gamma"), 0);
    BW_CHECK_EQ("and the switch is the player's",
                bw_mod_menu_enabled_at(1), 1);
    bw_mod_menu_revert();
    BW_CHECK_EQ("the switch goes back to the port's list",
                bw_mod_menu_enabled_at(1), bw_mod_menu_running_at(1));
    BW_CHECK_EQ("the order goes back too",
                strcmp(bw_mod_menu_name_at(0), "Beta"), 0);
    BW_CHECK_EQ("the list is the one it was", (long)bw_mod_menu_count(), 3);
    BW_CHECK_EQ("and the mod that was off is off again",
                bw_mod_menu_enabled_at(1), 0);
    g_log_count = 0;
    BW_REQUIRE("the apply succeeds", bw_mod_menu_apply() == 0);
    BW_CHECK_EQ("but there was nothing to apply", (long)g_log_count, 0);

    printf("the move\n");
    BW_REQUIRE("a mod is moved up", bw_mod_menu_move_up(1) == 0);
    BW_CHECK_EQ("the mod is in front",
                strcmp(bw_mod_menu_name_at(0), "Alpha"), 0);
    BW_CHECK_EQ("and the one that was in front is behind it",
                strcmp(bw_mod_menu_name_at(1), "Beta"), 0);
    BW_CHECK_EQ("a mod that is already in front cannot move up",
                bw_mod_menu_move_up(0), -1);
    BW_REQUIRE("a mod is moved down", bw_mod_menu_move_down(0) == 0);
    BW_CHECK_EQ("the mod is behind the one it was in front of",
                strcmp(bw_mod_menu_name_at(0), "Beta"), 0);
    BW_CHECK_EQ("a mod that is already at the back cannot move down",
                bw_mod_menu_move_down(2), -1);
    BW_REQUIRE("a mod is moved to a place", bw_mod_menu_move_to(2, 0) == 0);
    BW_CHECK_EQ("the mod is in front",
                strcmp(bw_mod_menu_name_at(0), "Gamma"), 0);
    BW_CHECK_EQ("and the others moved up",
                strcmp(bw_mod_menu_name_at(2), "Alpha"), 0);
    BW_REQUIRE("a mod is moved past the end", bw_mod_menu_move_to(0, 99) == 0);
    BW_CHECK_EQ("it takes the last place",
                strcmp(bw_mod_menu_name_at(2), "Gamma"), 0);
    BW_CHECK_EQ("a mod that is not there cannot be moved",
                bw_mod_menu_move_to(9, 0), -1);
    BW_REQUIRE("a mod is moved to the front", bw_mod_menu_move_to(2, 0) == 0);
    BW_CHECK_EQ("the list is the player's order now",
                strcmp(bw_mod_menu_name_at(0), "Gamma"), 0);
    BW_CHECK_EQ("the order is the player's now", bw_mod_menu_dirty(), 1);
    BW_CHECK_EQ("and the port's list has not moved yet",
                strcmp(bw_mod_root_at(0), T_MOD_B), 0);

    printf("the order the mods load in\n");
    BW_REQUIRE("the mod that is off is turned on",
               bw_mod_menu_set_enabled(2, 1) == 0);
    BW_CHECK_EQ("the player's order is Gamma, Beta, Alpha",
                strcmp(bw_mod_menu_name_at(0), "Gamma"), 0);
    BW_REQUIRE("the apply succeeds", bw_mod_menu_apply() == 0);
    BW_CHECK_EQ("the list holds the three mods", (long)bw_mod_count(), 3);
    BW_CHECK_EQ("the first is the player's first",
                strcmp(bw_mod_root_at(0), T_MOD_C), 0);
    BW_CHECK_EQ("the second is the player's second",
                strcmp(bw_mod_root_at(1), T_MOD_B), 0);
    BW_CHECK_EQ("and the third is the player's third",
                strcmp(bw_mod_root_at(2), T_MOD_A), 0);
    BW_REQUIRE("the mod is turned off again",
               bw_mod_menu_set_enabled(2, 0) == 0);
    BW_REQUIRE("the apply succeeds", bw_mod_menu_apply() == 0);
    BW_CHECK_EQ("the list holds the mods that are on",
                (long)bw_mod_count(), 2);

    printf("the clear action\n");
    index = (u32)index_of_ns(T_NS);
    BW_REQUIRE("the mod is in the list", index < bw_mod_menu_count());
    bw_mod_savedata_set_namespace(T_NS);
    BW_REQUIRE("the state is stored", bw_mod_savedata_store("payload", 7) == 0);
    BW_REQUIRE("the state file's path is known",
               state_path(path, sizeof path, T_NS) == 0);
    BW_CHECK_EQ("the file is there", file_exists(path), 1);
    BW_REQUIRE("the clear succeeds", bw_mod_menu_clear(index) == 0);
    BW_CHECK_EQ("the mod's own state file is gone", file_exists(path), 0);
    BW_CHECK_EQ("and the folder stays", dir_exists(T_MODS "/alpha"), 1);

    printf("the orphan report\n");
    BW_CHECK_EQ("one folder is an orphan",
                (long)bw_mod_menu_orphan_count(), 1);
    BW_CHECK_EQ("and it is named",
                strcmp(bw_mod_menu_orphan_at(0), "left-over"), 0);
    BW_CHECK_EQ("a folder that is a mod's is not one",
                bw_mod_menu_orphan_at(1) == NULL, 1);
    BW_CHECK_EQ("and a file in the folder is not one either",
                (long)bw_mod_menu_orphan_count(), 1);

    printf("the setting\n");
    index = (u32)index_of_ns(T_NS);
    BW_REQUIRE("the mod is in the list", index < bw_mod_menu_count());
    BW_CHECK_EQ("the mod declares seven good options",
                (long)bw_mod_menu_option_count(index), 7);
    BW_CHECK_EQ("the third is an int",
                (long)bw_mod_menu_option_kind(index, 2), BW_MOD_SETTING_INT);
    BW_CHECK_EQ("its default", (long)bw_mod_menu_option_default(index, 2), 3);
    BW_CHECK_EQ("its bounds", (long)bw_mod_menu_option_max(index, 2), 10);
    BW_CHECK_EQ("a value past the bounds is refused",
                (long)(bw_mod_menu_option_set(index, 2, 11) != 0), 1);
    BW_CHECK_EQ("a '#' inside a label is not a comment",
                strcmp(bw_mod_menu_option_label(index, 3), "No #1 picks"), 0);
    BW_CHECK_EQ("the first is a bool",
                (long)bw_mod_menu_option_kind(index, 0), BW_MOD_SETTING_BOOL);
    BW_CHECK_EQ("its key is the one the mod asks for",
                strcmp(bw_mod_menu_option_key(index, 0), "nuzlocke.dupes"),
                0);
    BW_CHECK_EQ("its label is the text the screen shows",
                strcmp(bw_mod_menu_option_label(index, 0), "Dupes clause"), 0);
    BW_CHECK_EQ("its default is the declaration's",
                (long)bw_mod_menu_option_default(index, 0), 1);
    BW_CHECK_EQ("and the default stands until the player chooses",
                (long)bw_mod_menu_option_value(index, 0), 1);
    BW_CHECK_EQ("the second is a choice",
                (long)bw_mod_menu_option_kind(index, 1),
                BW_MOD_SETTING_CHOICE);
    BW_CHECK_EQ("it names three choices",
                (long)bw_mod_menu_option_choice_count(index, 1), 3);
    BW_CHECK_EQ("the first is off",
                strcmp(bw_mod_menu_option_choice(index, 1, 0), "off"), 0);
    BW_CHECK_EQ("the last is ace",
                strcmp(bw_mod_menu_option_choice(index, 1, 2), "ace"), 0);
    BW_CHECK_EQ("its default is the first",
                (long)bw_mod_menu_option_default(index, 1), 0);
    BW_CHECK_EQ("and its text is the name of the choice",
                strcmp(bw_mod_menu_option_text(index, 1, text, sizeof text),
                       "off"), 0);
    BW_REQUIRE("the player turns the bool off",
               bw_mod_menu_option_set(index, 0, 0) == 0);
    BW_CHECK_EQ("the choice is held", (long)bw_mod_menu_option_value(index, 0),
                0);
    BW_REQUIRE("the player picks a choice",
               bw_mod_menu_option_set(index, 1, 2) == 0);
    BW_CHECK_EQ("the choice is held too",
                (long)bw_mod_menu_option_value(index, 1), 2);
    BW_CHECK_EQ("and the text is the name of the choice",
                strcmp(bw_mod_menu_option_text(index, 1, text, sizeof text),
                       "ace"), 0);
    BW_CHECK_EQ("a bool takes only on or off",
                bw_mod_menu_option_set(index, 0, 2), -1);
    BW_CHECK_EQ("a choice takes only one of its names",
                bw_mod_menu_option_set(index, 1, 3), -1);
    BW_CHECK_EQ("an option that is not there is refused",
                bw_mod_menu_option_set(index, 9, 0), -1);

    printf("the mode words, headings, help and yaml lines\n");
    BW_CHECK_EQ("three modes", (long)bw_mod_menu_mode_count(index), 3);
    BW_CHECK_EQ("the world mode joins worlds", bw_mod_menu_mode_joins(index, 1), 1);
    BW_CHECK_EQ("the solo one does not", bw_mod_menu_mode_joins(index, 0), 0);
    BW_CHECK_EQ("the room is a type on the world's card", bw_mod_menu_mode_in(index, 2), 1);
    BW_CHECK_EQ("the world is a card of its own", bw_mod_menu_mode_in(index, 1), -1);
    BW_CHECK_EQ("the room saves a file for its game",
                strcmp(bw_mod_menu_mode_export(index, 2), "Game {version} (x)"), 0);
    BW_CHECK_EQ("a mode without export saves none", bw_mod_menu_mode_export(index, 1) == NULL, 1);
    BW_CHECK_EQ("a heading goes to the options under it",
                strcmp(bw_mod_menu_option_heading(index, 6), "Rules"), 0);
    BW_CHECK_EQ("an option above it has none", strcmp(bw_mod_menu_option_heading(index, 3), ""), 0);
    BW_CHECK_EQ("a help line goes to its option",
                strcmp(bw_mod_menu_option_help(index, 6), "What the rule does."), 0);
    BW_CHECK_EQ("a yaml line too", strcmp(bw_mod_menu_option_yaml(index, 6), "the_rule false|true"), 0);
    BW_CHECK_EQ("the room's screen shows the rule", bw_mod_menu_option_in_mode(index, 6, 2), 1);
    {
        u32 jm = 9, jk = 9;
        int found = bw_mod_menu_find_joining(&jm, &jk);

        /* only a mod that is on joins worlds (the switch is the earlier
         * checks' to set) */
        BW_CHECK_EQ("the world mode is the one that joins, while its mod is on",
                    bw_mod_menu_enabled_at(index) ? (found == 1 && jm == index && jk == 1) : found == 0, 1);
    }
    {
        char ypath[512], ytext[1024];

        BW_REQUIRE("the room's options file is written",
                   bw_mod_menu_mode_write_yaml(index, 2, "Ash K", 'W', ypath, sizeof ypath) == 0);
        BW_REQUIRE("and reads back", read_file(ypath, ytext, sizeof ytext) != NULL);
        BW_CHECK_EQ("it names the player", strstr(ytext, "name: Ash K\n") != NULL, 1);
        BW_CHECK_EQ("and the game, with the cartridge's version",
                    strstr(ytext, "game: \"Game White (x)\"\n") != NULL, 1);
        BW_CHECK_EQ("and the rule as the file names it",
                    strstr(ytext, "\"Game White (x)\":\n  the_rule: true\n") != NULL, 1);
        BW_CHECK_EQ("the file is named after the player", strstr(ypath, "Ash_K.yaml") != NULL, 1);
        remove(ypath);
    }

    printf("the rows a mode's screen shows\n");
    BW_CHECK_EQ("the condition is cut off the value",
                (long)bw_mod_menu_option_choice_count(index, 4), 2);
    BW_CHECK_EQ("the World row is on the world screen",
                bw_mod_menu_option_in_mode(index, 4, 1), 1);
    BW_CHECK_EQ("and not on the solo one",
                bw_mod_menu_option_in_mode(index, 4, 0), 0);
    BW_CHECK_EQ("the code is not asked while World is Make",
                bw_mod_menu_option_in_mode(index, 5, 1), 0);
    BW_CHECK_EQ("the rule is (the condition after the @ word, any case)",
                bw_mod_menu_option_in_mode(index, 6, 1), 1);
    BW_REQUIRE("the player chooses Join",
               bw_mod_menu_option_set(index, 4, 1) == 0);
    BW_CHECK_EQ("now the code is asked",
                bw_mod_menu_option_in_mode(index, 5, 1), 1);
    BW_CHECK_EQ("and the rule is not",
                bw_mod_menu_option_in_mode(index, 6, 1), 0);
    BW_CHECK_EQ("the solo screen has no World row, so its rule shows",
                bw_mod_menu_option_in_mode(index, 6, 0), 1);
    BW_REQUIRE("the player goes back to Make",
               bw_mod_menu_option_set(index, 4, 0) == 0);
    BW_REQUIRE("the choices file's path is known",
               choices_path(path, sizeof path, T_NS) == 0);
    BW_CHECK_EQ("the choices are stored beside the mod's saved data",
                file_exists(path), 1);
    BW_REQUIRE("the choices file reads back",
               read_file(path, text, sizeof text) != NULL);
    BW_CHECK_EQ("the player's choice is written down",
                strstr(text, "nuzlocke.dupes = 0") != NULL, 1);
    BW_CHECK_EQ("and the other one",
                strstr(text, "nuzlocke.cap = 2") != NULL, 1);
    BW_CHECK_EQ("the choices are in the mod's own folder",
                strncmp(path, T_MODS "/" T_NS "/",
                        strlen(T_MODS "/" T_NS "/")), 0);
    bw_mod_menu_set_namespace(T_NS);
    BW_CHECK_EQ("the mod asks for its own option",
                (long)bw_mod_menu_setting_get("nuzlocke.dupes"), 0);
    BW_CHECK_EQ("and for the other one",
                (long)bw_mod_menu_setting_get("nuzlocke.cap"), 2);
    BW_CHECK_EQ("a key the mod does not declare has no answer",
                (long)bw_mod_menu_setting_get("nope"), -1);
    bw_mod_menu_set_namespace(NULL);
    BW_CHECK_EQ("and with no mod calling there is no answer",
                (long)bw_mod_menu_setting_get("nuzlocke.dupes"), -1);
    bw_mod_menu_open();
    index = (u32)index_of_ns(T_NS);
    BW_REQUIRE("the mod is in the list", index < bw_mod_menu_count());
    BW_CHECK_EQ("the player's choice is read back",
                (long)bw_mod_menu_option_value(index, 0), 0);
    BW_CHECK_EQ("and the other one is read back too",
                (long)bw_mod_menu_option_value(index, 1), 2);

    printf("the record\n");
    BW_REQUIRE("the record's path is known", record_path(path, sizeof path) == 0);
    BW_CHECK_EQ("the player's list is written", file_exists(path), 1);
    BW_REQUIRE("the record reads back",
               read_file(path, text, sizeof text) != NULL);
    BW_CHECK_EQ("it names the mods the player knows about",
                strstr(text, T_MOD_A) != NULL, 1);
    BW_CHECK_EQ("and the others",
                strstr(text, T_MOD_B) != NULL, 1);
    BW_CHECK_EQ("it carries the switches",
                strstr(text, "enabled = 0") != NULL, 1);
    BW_CHECK_EQ("and the order",
                strstr(text, "order = 0") != NULL, 1);

    printf("the start-up restore\n");
    bw_mod_menu_reset();
    bw_mod_menu_start();
    BW_CHECK_EQ("the list is restored", (long)bw_mod_menu_count(), 3);
    BW_CHECK_EQ("the order is the player's",
                strcmp(bw_mod_menu_name_at(0), "Gamma"), 0);
    BW_CHECK_EQ("the second is the player's too",
                strcmp(bw_mod_menu_name_at(1), "Beta"), 0);
    BW_CHECK_EQ("and the third",
                strcmp(bw_mod_menu_name_at(2), "Alpha"), 0);
    BW_CHECK_EQ("the switches are the player's",
                bw_mod_menu_enabled_at(0), 1);
    BW_CHECK_EQ("and the mod the player turned off is off",
                bw_mod_menu_enabled_at(2), 0);
    BW_CHECK_EQ("and it is not running", bw_mod_menu_running_at(2), 0);
    BW_CHECK_EQ("the port's list is the one the player left",
                (long)bw_mod_count(), 2);

    printf("with no record the port starts with no mods\n");
    bw_mod_menu_reset();
    bw_clear_mods();
    BW_REQUIRE("the record is taken away", remove(path) == 0);
    bw_mod_menu_start();
    BW_CHECK_EQ("no mods are shown", (long)bw_mod_menu_count(), 0);
    BW_CHECK_EQ("and the port's list stands empty", (long)bw_mod_count(), 0);
    BW_CHECK_EQ("the state folder is untouched",
                dir_exists(T_MODS "/alpha"), 1);

    printf("a mod the loader never loaded\n");
    BW_REQUIRE("the record's path is known", record_path(path, sizeof path) == 0);
    BW_REQUIRE("a record naming a mod the port's list does not hold is "
               "written",
               write_file(path,
                          "[mod]\n"
                          "root = " T_MOD_A "\n"
                          "enabled = 1\n"
                          "order = -1\n"
                          "[mod]\n"
                          "root = " T_MOD_B "\n"
                          "enabled = 1\n"
                          "order = -1\n"
                          "[mod]\n"
                          "root = " T_MOD_C "\n"
                          "enabled = 1\n"
                          "order = -1\n"
                          "[mod]\n"
                          "root = " T_MOD_D "\n"
                          "enabled = 0\n"
                          "order = -1\n") == 0);
    bw_mod_menu_open();
    BW_CHECK_EQ("the mods the record holds are shown too",
                (long)bw_mod_menu_count(), 4);
    index = (u32)index_of_ns("delta");
    BW_REQUIRE("the mod the port's list does not hold is in the list",
               index < bw_mod_menu_count());
    BW_CHECK_EQ("it is named", strcmp(bw_mod_menu_name_at(index), "Delta"), 0);
    BW_CHECK_EQ("it ships code", bw_mod_menu_has_code_at(index), 1);
    BW_CHECK_EQ("but the loader never loaded it",
                bw_mod_menu_running_at(index), 0);
    BW_CHECK_EQ("and its folder is there", bw_mod_menu_present_at(index), 1);
    for (i = 0; i < 3; i++) {
        BW_CHECK_EQ("the other mods are in the list too",
                    bw_mod_menu_present_at(i), 1);
    }

    printf("the installed mods folder and the reload\n");
    {
        char folder[512];

        /* The installed folder is "mods" beside the save folder. */
        BW_REQUIRE("the installed folder has a path",
                   bw_mod_menu_installed_folder(folder, sizeof folder) == 0);
        BW_CHECK_EQ("it is beside the save folder",
                    strcmp(folder, T_ROOT "/mods"), 0);
        BW_REQUIRE("a mod is put in it",
                   make_dirs(T_ROOT "/mods/zeta") == 0 &&
                   write_file(T_ROOT "/mods/zeta/eidos_mod.txt",
                              "name = Zeta\nstate = zeta\n") == 0 &&
                   write_file(T_ROOT "/mods/notamod.txt", "x\n") == 0);
        BW_REQUIRE("the reload runs", bw_mod_menu_reload() == 0);
        index = (u32)index_of_ns("zeta");
        BW_REQUIRE("the new mod is in the list", index < bw_mod_menu_count());
        BW_CHECK_EQ("it joins switched on", bw_mod_menu_enabled_at(index), 1);
        BW_CHECK_EQ("and running", bw_mod_menu_running_at(index), 1);
        BW_CHECK_EQ("a loose file is not a mod", index_of_ns("notamod") < 0, 1);

        remove(T_ROOT "/mods/zeta/eidos_mod.txt");
        (void)BW_RMDIR(T_ROOT "/mods/zeta");
        BW_REQUIRE("the reload runs again", bw_mod_menu_reload() == 0);
        index = (u32)index_of_ns("zeta");
        if (index < bw_mod_menu_count()) {
            BW_CHECK_EQ("a removed mod shows as missing",
                        bw_mod_menu_present_at(index), 0);
        }
        remove(T_ROOT "/mods/notamod.txt");
        (void)BW_RMDIR(T_ROOT "/mods");
    }

    printf("version 7: a mod's own tabs, passwords, buttons and rows\n");
    {
        u32 g, o, user = 99, pass = 99, signin = 99, who = 99, out = 99, two = 99;
        char buf[160];

        bw_clear_mods();
        BW_REQUIRE("the third mod is added again", bw_add_mod_root(T_MOD_C) == 0);
        bw_mod_menu_open();
        g = (u32)index_of_ns("gamma");
        BW_REQUIRE("the third mod is listed", g < bw_mod_menu_count());
        BW_CHECK_EQ("it has two tabs", (long)bw_mod_menu_tab_count(g), 2);
        BW_CHECK_EQ("the first tab's title", strcmp(bw_mod_menu_tab_title(g, 0), "Gamma Online"), 0);
        BW_CHECK_EQ("the second tab's title", strcmp(bw_mod_menu_tab_title(g, 1), "Gamma Two"), 0);
        BW_CHECK_EQ("the bad password (a default) is refused", (long)bw_mod_menu_option_count(g), 7);
        for (o = 0; o < bw_mod_menu_option_count(g); o++) {
            const char *k = bw_mod_menu_option_key(g, o);

            if (strcmp(k, "g.user") == 0) user = o;
            if (strcmp(k, "g.pass") == 0) pass = o;
            if (strcmp(k, "g.signin") == 0) signin = o;
            if (strcmp(k, "g.who") == 0) who = o;
            if (strcmp(k, "g.signout") == 0) out = o;
            if (strcmp(k, "g.two") == 0) two = o;
        }
        BW_REQUIRE("every row is declared", user < 99 && pass < 99 && signin < 99 && who < 99 && out < 99 && two < 99);
        BW_CHECK_EQ("a line before any tab stays under the mod", (long)bw_mod_menu_option_tab(g, 0), -1);
        BW_CHECK_EQ("the user name is on the first tab", (long)bw_mod_menu_option_tab(g, user), 0);
        BW_CHECK_EQ("the second tab's option is on it", (long)bw_mod_menu_option_tab(g, two), 1);
        BW_CHECK_EQ("the password is a secret", (long)bw_mod_menu_option_kind(g, pass), BW_MOD_SETTING_SECRET);
        BW_CHECK_EQ("and never a lobby rule", (long)bw_mod_menu_option_in_lobby(g, pass), 0);
        BW_CHECK_EQ("the button's kind", (long)bw_mod_menu_option_kind(g, signin), BW_MOD_SETTING_BUTTON);
        BW_CHECK_EQ("the mod's row's kind", (long)bw_mod_menu_option_kind(g, who), BW_MOD_SETTING_SHOW);
        BW_CHECK_EQ("the user name is one word", (long)bw_mod_menu_option_word(g, user), 1);
        BW_CHECK_EQ("the heading is its own", strcmp(bw_mod_menu_option_heading(g, user), "Sign in"), 0);
        BW_CHECK_EQ("signed out: the user name shows", (long)bw_mod_menu_option_shown(g, user), 1);
        BW_CHECK_EQ("and Sign in", (long)bw_mod_menu_option_shown(g, signin), 1);
        BW_CHECK_EQ("the empty row is hidden", (long)bw_mod_menu_option_shown(g, who), 0);
        BW_CHECK_EQ("and Sign out", (long)bw_mod_menu_option_shown(g, out), 0);

        BW_CHECK_EQ("a password may hold a quote",
                    (long)bw_mod_menu_option_set_text(g, pass, "p\"ss word!"), 0);
        BW_CHECK_EQ("a text may not", (long)bw_mod_menu_option_set_text(g, user, "a\"b"), -1);
        BW_CHECK_EQ("the user name is set", (long)bw_mod_menu_option_set_text(g, user, "Player"), 0);
        bw_mod_menu_set_namespace("gamma");
        BW_CHECK_EQ("the mod reads its password", (long)bw_mod_menu_setting_text("g.pass", buf, sizeof buf), 10);
        BW_CHECK_EQ("as typed", strcmp(buf, "p\"ss word!"), 0);
        BW_CHECK_EQ("the mod clears it", (long)bw_mod_menu_setting_set_text("g.pass", ""), 0);
        BW_CHECK_EQ("it reads empty", (long)bw_mod_menu_setting_text("g.pass", buf, sizeof buf), 0);
        BW_CHECK_EQ("the mod sets its row", (long)bw_mod_menu_setting_show("g.who", "Player"), 0);
        BW_CHECK_EQ("a row it does not declare", (long)bw_mod_menu_setting_show("g.nope", "x"), -1);
        BW_CHECK_EQ("a line end is refused", (long)bw_mod_menu_setting_show("g.who", "a\nb"), -1);
        bw_mod_menu_set_namespace("alpha");
        BW_CHECK_EQ("another mod cannot set it", (long)bw_mod_menu_setting_show("g.who", "x"), -1);
        bw_mod_menu_set_namespace(NULL);
        BW_CHECK_EQ("signed in: the row shows", (long)bw_mod_menu_option_shown(g, who), 1);
        BW_CHECK_EQ("Sign out shows (!=)", (long)bw_mod_menu_option_shown(g, out), 1);
        BW_CHECK_EQ("the user name hides", (long)bw_mod_menu_option_shown(g, user), 0);
        BW_CHECK_EQ("the row's text", strcmp(bw_mod_menu_option_text(g, who, buf, sizeof buf), "Player"), 0);
        BW_CHECK_EQ("a button can be pressed", (long)bw_mod_menu_press(g, signin), 0);
        BW_CHECK_EQ("a row that is no button cannot", (long)bw_mod_menu_press(g, who), -1);

        BW_CHECK_EQ("the password is set once more",
                    (long)bw_mod_menu_option_set_text(g, pass, "secret1"), 0);
        BW_REQUIRE("the choices' path", choices_path(path, sizeof path, "gamma") == 0);
        BW_REQUIRE("the choices are written", read_file(path, text, sizeof text) != NULL);
        BW_CHECK_EQ("they keep the user name", strstr(text, "g.user = \"Player\"") != NULL, 1);
        BW_CHECK_EQ("never the password", strstr(text, "secret1") == NULL && strstr(text, "g.pass") == NULL, 1);
        BW_CHECK_EQ("nor a button or a mod's row",
                    strstr(text, "g.signin") == NULL && strstr(text, "g.who") == NULL, 1);
        bw_mod_menu_clear_secrets();
        BW_CHECK_EQ("the settings close: the password is forgotten",
                    bw_mod_menu_option_text(g, pass, buf, sizeof buf)[0], 0);
    }

    bw_mod_menu_reset();
    bw_clear_mods();
    clean_tree();
    BW_REPORT("test_mod_menu");
}
