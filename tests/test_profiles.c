/*
 * tests/test_profiles.c -- one install with both games (install/setup.c)
 * and the two game profiles of a start without the launcher
 * (install/profiles.c).
 *
 *     test_profiles <work_dir>
 *
 * Everything happens under <work_dir>, which is the eidos home
 * (bw_setup_set_home) and holds the user folders the cases make, so the
 * player's own folders are never read or written.  The extractions are
 * hand-made trees: a manifest with a known dump's hash, a header.bin with
 * the game code, data/a and arm9_dec.bin, which is all the setup looks at.
 *
 *   1. the two games: names, game codes, the game a folder holds, the
 *      folder a cartridge's data goes to, the folder of a game, the game
 *      played last, the config file's lines for them;
 *   2. the options --game and --rom=PATH, and the resolution with --game
 *      (found, missing, a named folder of the other game);
 *   3. a profile's folder; a plain start's old saves copied into an empty
 *      profile, once, the originals left, another game's saves not taken;
 *   4. the old saves beside a profile's own: the free slots in order, the
 *      Your games lines under their new numbers, the multiplayer games,
 *      a file of no length that is never written over, nothing of the
 *      profile changed, nothing copied twice;
 *   5. no limit on the slots: old saves past slot 8 when the profile has
 *      no free slot below it, for either game;
 *   6. a copy that cannot be made leaves the profile and the old folder
 *      as they were and is tried again;
 *   7. a new profile's settings: the other game's, never over its own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "profiles.h"
#include "setup.h"

#define WHITE_SHA1 "bc696a0dfb448c7b3a8a206f0f8214411a039208"   /* English White, install/ndsrom.c */

static char g_work[512];

static void path_of(char *out, size_t n, const char *a, const char *b)
{
    if (snprintf(out, n, "%s/%s", a, b) >= (int)n && n > 0) {
        out[0] = '\0';
    }
}

/* A file with `text` in it, its folder made first. */
static int put(const char *dir, const char *rel, const char *text)
{
    char p[1100], folder[1100];
    char *cut;
    FILE *fp;

    path_of(p, sizeof(p), dir, rel);
    snprintf(folder, sizeof(folder), "%s", p);
    cut = strrchr(folder, '/');
    if (cut != NULL) {
        *cut = '\0';
        if (bw_mkdirs_utf8(folder) != 0) {
            return -1;
        }
    }
    fp = bw_fopen_utf8(p, "wb");
    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    return fclose(fp);
}

/* Whether the file holds exactly `text` (NULL: whether it is not there). */
static int holds(const char *dir, const char *rel, const char *text)
{
    char p[1100], buf[4096];
    FILE *fp;
    size_t got;

    path_of(p, sizeof(p), dir, rel);
    if (text == NULL) {
        return bw_path_kind_utf8(p) == BW_PATH_NONE;
    }
    fp = bw_fopen_utf8(p, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[got] = '\0';
    return strcmp(buf, text) == 0;
}

/* A playable extraction of the cartridge `code`. */
static int fake_game(const char *dir, const char *code, const char *sha, int version)
{
    char header[0x10], text[512], p[1100];
    FILE *fp;

    snprintf(text, sizeof(text), "{\n \"tool\": \"eidos install\",\n \"version\": %d,\n"
             " \"rom_sha1\": \"%s\",\n \"title\": \"POKEMON\"\n}\n", version, sha);
    if (put(dir, "eidos_manifest.json", text) != 0 || put(dir, "arm9_dec.bin", "x") != 0) {
        return -1;
    }
    path_of(p, sizeof(p), dir, "data/a");
    if (bw_mkdirs_utf8(p) != 0) {
        return -1;
    }
    memset(header, 0, sizeof(header));
    memcpy(header, "POKEMON", 7);
    memcpy(header + 0x0C, code, 4);
    path_of(p, sizeof(p), dir, "header.bin");
    fp = bw_fopen_utf8(p, "wb");
    if (fp == NULL) {
        return -1;
    }
    fwrite(header, 1, sizeof(header), fp);
    return fclose(fp);
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* ------------------------------------------------------------------ */
/* 1, 2: the two games of one install                                  */
/* ------------------------------------------------------------------ */

static int test_games(void)
{
    char black[600], white[600], french[600], p[700], code[5];
    int version = (int)strtol(BW_INSTALL_VERSION, NULL, 10);
    BwSetupConfig c;
    BwSetupRequest r;
    BwSetupResolved out;
    char *argv[] = { "prog", "--game", "white", "--game=BLACK", "--game", "red", "--rom=R.nds", "--rom=", "--game" };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    int i;

    BW_CHECK_EQ("black by name", bw_setup_game_named("black"), BW_GAME_BLACK);
    BW_CHECK_EQ("white by name, any case", bw_setup_game_named("WhItE"), BW_GAME_WHITE);
    BW_CHECK_EQ("another name", bw_setup_game_named("blackk"), BW_GAME_UNKNOWN);
    BW_CHECK_EQ("no name", bw_setup_game_named(NULL), BW_GAME_UNKNOWN);
    BW_CHECK_EQ("IRBO is Black", bw_setup_game_of_code("IRBO"), BW_GAME_BLACK);
    BW_CHECK_EQ("IRAF is White", bw_setup_game_of_code("IRAF"), BW_GAME_WHITE);
    BW_CHECK_EQ("another game's code", bw_setup_game_of_code("ADAE"), BW_GAME_UNKNOWN);
    BW_CHECK_EQ("a short code", bw_setup_game_of_code("IRB"), BW_GAME_UNKNOWN);
    BW_CHECK_EQ("the titles", strcmp(bw_setup_game_title(BW_GAME_WHITE), "White") == 0
                              && strcmp(bw_setup_game_title(BW_GAME_BLACK), "Black") == 0
                              && bw_setup_game_title(BW_GAME_UNKNOWN)[0] == '\0', 1);

    /* a fresh install: where each cartridge's data goes */
    BW_CHECK_EQ("English Black's place", bw_setup_place_for("IRBO", p, sizeof(p)), 0);
    BW_CHECK_EQ("is <home>/extract", ends_with(p, BW_SETUP_SEP "extract"), 1);
    BW_CHECK_EQ("English White's place", bw_setup_place_for("IRAO", p, sizeof(p)), 0);
    BW_CHECK_EQ("is <home>/extract-IRAO", ends_with(p, BW_SETUP_SEP "extract-IRAO"), 1);
    BW_CHECK_EQ("French Black's place", bw_setup_place_for("IRBF", p, sizeof(p)), 0);
    BW_CHECK_EQ("is <home>/extract-IRBF", ends_with(p, BW_SETUP_SEP "extract-IRBF"), 1);
    BW_CHECK_EQ("no game yet", bw_setup_find_game(BW_GAME_BLACK, p, sizeof(p)), -1);

    /* an install from before the two games, whose one folder holds White */
    path_of(black, sizeof(black), g_work, "extract");
    BW_CHECK_EQ("White in <home>/extract", fake_game(black, "IRAO", WHITE_SHA1, version), 0);
    BW_CHECK_EQ("the folder's game", bw_setup_game_of(black, code), BW_GAME_WHITE);
    BW_CHECK_EQ("and its code", strcmp(code, "IRAO"), 0);
    BW_CHECK_EQ("by its data folder too", (path_of(p, sizeof(p), black, "data"), bw_setup_game_of(p, NULL)),
                BW_GAME_WHITE);
    BW_CHECK_EQ("White is found there", bw_setup_find_game(BW_GAME_WHITE, p, sizeof(p)), 0);
    BW_CHECK_EQ("in <home>/extract", ends_with(p, "extract"), 1);
    BW_CHECK_EQ("White's data is made again in place", bw_setup_place_for("IRAO", p, sizeof(p)) == 0
                                                       && ends_with(p, BW_SETUP_SEP "extract"), 1);
    BW_CHECK_EQ("English Black never replaces it", bw_setup_place_for("IRBO", p, sizeof(p)) == 0
                                                   && ends_with(p, BW_SETUP_SEP "extract-IRBO"), 1);
    bw_remove_tree_utf8(black);

    /* both games, and a second language of Black */
    path_of(white, sizeof(white), g_work, "extract-IRAO");
    path_of(french, sizeof(french), g_work, "extract-IRBF");
    BW_CHECK_EQ("Black", fake_game(black, "IRBO", BW_BLACK_SHA1, version), 0);
    BW_CHECK_EQ("White", fake_game(white, "IRAO", WHITE_SHA1, version), 0);
    BW_CHECK_EQ("French Black", fake_game(french, "IRBF", "a559da54839663d8f8f3f03382e5665a61873d22", version), 0);
    BW_CHECK_EQ("Black is found", bw_setup_find_game(BW_GAME_BLACK, p, sizeof(p)), 0);
    BW_CHECK_EQ("the English one first", ends_with(p, BW_SETUP_SEP "extract"), 1);
    BW_CHECK_EQ("White is found", bw_setup_find_game(BW_GAME_WHITE, p, sizeof(p)) == 0
                                  && ends_with(p, "extract-IRAO"), 1);
    BW_CHECK_EQ("each cartridge has its folder", bw_setup_place_for("IRBF", p, sizeof(p)) == 0
                                                 && ends_with(p, "extract-IRBF"), 1);

    /* the game played last, and its lines in the config file */
    BW_CHECK_EQ("remember Black and its ROM", bw_setup_remember(black, "b.nds"), 0);
    BW_CHECK_EQ("remember White and its ROM", bw_setup_remember(white, "w.nds"), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&c), 0);
    BW_CHECK_EQ("the current folder is White's", ends_with(c.data_root, "extract-IRAO"), 1);
    BW_CHECK_EQ("the current ROM is White's", ends_with(c.rom, "w.nds"), 1);
    BW_CHECK_EQ("Black's folder is kept", ends_with(c.game_root[0], "extract"), 1);
    BW_CHECK_EQ("White's folder is kept", ends_with(c.game_root[1], "extract-IRAO"), 1);
    BW_CHECK_EQ("two ROMs by cartridge", c.nroms, 2);
    BW_CHECK_EQ("nothing was played yet", c.played[0], 0);
    BW_CHECK_EQ("French Black becomes the game played last", bw_setup_set_current(french), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&c), 0);
    BW_CHECK_EQ("the folder played last", ends_with(c.played, "extract-IRBF"), 1);
    BW_CHECK_EQ("the first line keeps its folder", ends_with(c.data_root, "extract-IRAO"), 1);
    BW_CHECK_EQ("and its ROM", ends_with(c.rom, "w.nds"), 1);
    BW_CHECK_EQ("Black is found where it was played last", bw_setup_find_game(BW_GAME_BLACK, p, sizeof(p)) == 0
                                                           && ends_with(p, "extract-IRBF"), 1);
    /* a start that names nothing (the launcher's Black profile) is not moved by it; a standalone game is */
    memset(&r, 0, sizeof(r));
    BW_CHECK_EQ("resolve, nothing named", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("the first line's folder", out.source == BW_SOURCE_CONFIG && out.state == BW_ROOT_OK
                                           && ends_with(out.extract_dir, "extract-IRAO"), 1);
    BW_CHECK_EQ("with its ROM", ends_with(out.rom, "w.nds"), 1);
    r.follow_last = 1;
    BW_CHECK_EQ("resolve, the game played last", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("French Black", out.source == BW_SOURCE_LAST && out.state == BW_ROOT_OK
                                && ends_with(out.extract_dir, "extract-IRBF"), 1);
    BW_CHECK_EQ("no ROM is known for it", out.rom[0], 0);
    r.data_root = black;
    BW_CHECK_EQ("a folder named wins over it", bw_setup_resolve(&r, &out) == 0 && out.source == BW_SOURCE_FLAG
                                               && ends_with(out.extract_dir, "extract"), 1);
    r.data_root = NULL;
    r.game = BW_GAME_WHITE;
    BW_CHECK_EQ("a game named wins over it", bw_setup_resolve(&r, &out) == 0 && out.source == BW_SOURCE_GAME
                                             && ends_with(out.extract_dir, "extract-IRAO"), 1);
    /* what an install into a cartridge's folder writes */
    BW_CHECK_EQ("French Black made from its ROM", bw_setup_remember_game(french, "f.nds"), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&c), 0);
    BW_CHECK_EQ("three ROMs by cartridge", c.nroms, 3);
    BW_CHECK_EQ("the first two lines are as they were", ends_with(c.data_root, "extract-IRAO")
                                                        && ends_with(c.rom, "w.nds"), 1);
    memset(&r, 0, sizeof(r));
    r.follow_last = 1;
    BW_CHECK_EQ("the game played last has its ROM now", bw_setup_resolve(&r, &out) == 0
                                                        && ends_with(out.rom, "f.nds"), 1);
    BW_CHECK_EQ("White made again from another ROM", bw_setup_remember_game(white, "w2.nds"), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&c), 0);
    BW_CHECK_EQ("the first line's ROM follows its own folder", ends_with(c.rom, "w2.nds"), 1);
    BW_CHECK_EQ("and back", bw_setup_remember_game(white, "w.nds"), 0);
    BW_CHECK_EQ("French Black again", bw_setup_set_current(french), 0);
    BW_CHECK_EQ("back to White", bw_setup_set_current(white), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&c), 0);
    BW_CHECK_EQ("White is the game played last", ends_with(c.played, "extract-IRAO"), 1);
    BW_CHECK_EQ("the same again writes nothing new", bw_setup_set_current(white), 0);
    /* the folder played last is gone: the first line serves */
    BW_CHECK_EQ("a folder that is not there", (path_of(p, sizeof(p), g_work, "extract-IRAF"),
                                               bw_setup_set_current(p)), 0);
    BW_CHECK_EQ("is not followed", bw_setup_resolve(&r, &out) == 0 && out.source == BW_SOURCE_CONFIG, 1);
    BW_CHECK_EQ("White again", bw_setup_set_current(white), 0);

    /* the options */
    memset(&r, 0, sizeof(r));
    i = 1;
    BW_CHECK_EQ("--game white", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    BW_CHECK_EQ("its value is taken", i == 2 && r.game == BW_GAME_WHITE, 1);
    i = 3;
    BW_CHECK_EQ("--game=BLACK", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    BW_CHECK_EQ("in one argument", i == 3 && r.game == BW_GAME_BLACK, 1);
    i = 4;
    BW_CHECK_EQ("--game red is refused", bw_setup_parse_arg(&r, argc, argv, &i), -1);
    i = 6;
    BW_CHECK_EQ("--rom=R.nds", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    BW_CHECK_EQ("in one argument", i == 6 && strcmp(r.rom, "R.nds") == 0, 1);
    i = 7;
    BW_CHECK_EQ("--rom= with no file is not ours", bw_setup_parse_arg(&r, argc, argv, &i), 0);
    i = 8;
    BW_CHECK_EQ("--game with no value", bw_setup_parse_arg(&r, argc, argv, &i), -1);
    BW_CHECK_EQ("the usage names --game", strstr(bw_setup_usage(), "--game") != NULL, 1);

    /* the resolution */
    memset(&r, 0, sizeof(r));
    r.game = BW_GAME_BLACK;
    BW_CHECK_EQ("resolve --game black", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("its source", out.source, BW_SOURCE_GAME);
    BW_CHECK_EQ("playable", out.state, BW_ROOT_OK);
    BW_CHECK_EQ("the folder Black was played from last", ends_with(out.extract_dir, "extract-IRBF"), 1);
    r.game = BW_GAME_WHITE;
    BW_CHECK_EQ("resolve --game white", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("White's folder", out.state == BW_ROOT_OK && ends_with(out.extract_dir, "extract-IRAO"), 1);
    BW_CHECK_EQ("with White's ROM", ends_with(out.rom, "w.nds"), 1);
    r.data_root = black;
    BW_CHECK_EQ("--game white with Black's folder named", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("is the other game", out.state, BW_ROOT_OTHER_GAME);
    BW_CHECK_EQ("and the sentence says so", strstr(out.why, "Pokemon Black") != NULL
                                            && strstr(out.why, "Pokemon White") != NULL, 1);
    BW_CHECK_EQ("prepare refuses it", bw_setup_prepare(&r, &out), BW_PREPARE_FAILED);
    r.data_root = white;
    BW_CHECK_EQ("--game white with White's folder named", bw_setup_resolve(&r, &out) == 0
                                                         && out.state == BW_ROOT_OK, 1);
    /* White's data made by an older installer: found, to be made again from its ROM */
    BW_CHECK_EQ("an older White", fake_game(white, "IRAO", WHITE_SHA1, version - 1), 0);
    memset(&r, 0, sizeof(r));
    r.game = BW_GAME_WHITE;
    BW_CHECK_EQ("resolve", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("old, in its own folder", out.state == BW_ROOT_OLD && ends_with(out.extract_dir, "extract-IRAO"), 1);
    BW_CHECK_EQ("its ROM is still known", ends_with(out.rom, "w.nds"), 1);
    /* no White at all */
    bw_remove_tree_utf8(white);
    BW_CHECK_EQ("resolve with no White", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("missing", out.state, BW_ROOT_MISSING);
    BW_CHECK_EQ("and the sentence names the game", strstr(out.why, "Pokemon White") != NULL, 1);
    r.no_window = 1;
    BW_CHECK_EQ("prepare without a window fails", bw_setup_prepare(&r, &out), BW_PREPARE_FAILED);
    /* a ROM of the other game is refused before anything is read */
    {
        char rom[700], msg[512], dir[700];
        BwSetupOutcome oc;
        char head[0x10];
        FILE *fp;

        memset(head, 0, sizeof(head));
        memcpy(head + 0x0C, "IRBO", 4);
        path_of(rom, sizeof(rom), g_work, "black.nds");
        fp = bw_fopen_utf8(rom, "wb");
        BW_CHECK_EQ("a file with Black's game code", fp != NULL, 1);
        fwrite(head, 1, sizeof(head), fp);
        fclose(fp);
        BW_CHECK_EQ("its code is read", bw_rom_peek_code(rom, code) == 0 && strcmp(code, "IRBO") == 0, 1);
        BW_CHECK_EQ("asked for White: refused", bw_setup_install_game(rom, BW_GAME_WHITE, 0, 0, NULL, NULL, &oc,
                                                                      dir, sizeof(dir), msg, sizeof(msg)), -1);
        BW_CHECK_EQ("as the other game", oc.other_game, 1);
        BW_CHECK_EQ("the sentence names both", strstr(msg, "Pokemon Black") != NULL
                                               && strstr(msg, "Pokemon White") != NULL, 1);
        BW_CHECK_EQ("no folder was named", dir[0], 0);
        BW_CHECK_EQ("a file too short for a code", (put(g_work, "tiny.nds", "abc"),
                                                    path_of(rom, sizeof(rom), g_work, "tiny.nds"),
                                                    bw_rom_peek_code(rom, code)), -1);
    }
    bw_remove_tree_utf8(black);
    bw_remove_tree_utf8(french);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 3 to 7: the profiles                                                */
/* ------------------------------------------------------------------ */

/* A user folder of its own for a case; `old` gets its saves folder. */
static void user_of(const char *name, char *user, size_t nu, char *old_saves, size_t no)
{
    path_of(user, nu, g_work, name);
    bw_remove_tree_utf8(user);
    bw_mkdirs_utf8(user);
    path_of(old_saves, no, user, "saves");
}

static int test_profile_copy(void)
{
    char user[600], saves[700], mods[700], settings[700], dir[700], other[700], note[2400], p[900];
    char record[3600], moved[3600];
    BwProfileOld old;
    int rc;

    BW_CHECK_EQ("Black's folder", bw_profile_dir("U", BW_GAME_BLACK, dir, sizeof(dir)) == 0
                                  && strcmp(dir, "U/profiles/Black") == 0, 1);
    BW_CHECK_EQ("White's folder", bw_profile_dir("U", BW_GAME_WHITE, dir, sizeof(dir)) == 0
                                  && strcmp(dir, "U/profiles/White") == 0, 1);
    BW_CHECK_EQ("no folder for no game", bw_profile_dir("U", BW_GAME_UNKNOWN, dir, sizeof(dir)), -1);
    BW_CHECK_EQ("no folder with no user folder", bw_profile_dir("", BW_GAME_BLACK, dir, sizeof(dir)), -1);

    /* a user folder with a name outside ASCII, as a Windows user name can be */
    user_of("user \xc3\xa9", user, sizeof(user), saves, sizeof(saves));
    path_of(mods, sizeof(mods), user, "mods");
    path_of(settings, sizeof(settings), g_work, "home-settings.ini");
    memset(&old, 0, sizeof(old));
    old.saves = saves;
    old.mods = mods;
    old.settings = settings;
    old.game = BW_GAME_BLACK;

    /* nothing old at all: the profile is made, nothing is said */
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("a first start: ready", rc, BW_PROFILE_READY);
    BW_CHECK_EQ("nothing to say", note[0], 0);
    BW_CHECK_EQ("saves/ is made", (path_of(p, sizeof(p), dir, "saves"), bw_path_kind_utf8(p)), BW_PATH_DIR);
    BW_CHECK_EQ("mods/ is made", (path_of(p, sizeof(p), dir, "mods"), bw_path_kind_utf8(p)), BW_PATH_DIR);
    BW_CHECK_EQ("no save yet", (path_of(p, sizeof(p), dir, "saves"), bw_profile_has_saves(p)), 0);
    BW_CHECK_EQ("without the old places too", bw_profile_prepare(user, BW_GAME_BLACK, NULL, dir, sizeof(dir),
                                                                 note, sizeof(note)), BW_PROFILE_READY);

    /* the old saves of a plain start: slots, a multiplayer game, mod data, a mod, the settings */
    BW_CHECK_EQ("old slot 1", put(saves, "pokeblack.sav", "SAVE-ONE"), 0);
    BW_CHECK_EQ("old slot 3", put(saves, "slots/save3.sav", "SAVE-THREE"), 0);
    BW_CHECK_EQ("old slot record", put(saves, "slot.txt", "3\n"), 0);
    BW_CHECK_EQ("old Your games", put(saves, "games.ini", "3 solo arkhipelagos solo host -\n"), 0);
    BW_CHECK_EQ("old multiplayer game", put(saves, "multiplayer/Friends/host.sav", "MP"), 0);
    BW_CHECK_EQ("old mod data", put(saves, "mods/nuzlocke/pokeblack.bin", "STATE"), 0);
    BW_CHECK_EQ("an old mod", put(mods, "Nuzlocke/mod.bw", "MOD"), 0);
    /* the game's record of the mods: two in the old mods folder (one spelled
     * with the other separator and no spaces), one in a folder whose name
     * only starts the same way, one somewhere else */
    snprintf(record, sizeof(record),
             "# eidos mod record\n\n[mod]\nroot = %s/Nuzlocke\nenabled = 0\norder = 0\n"
             "\n[mod]\nroot=%s\\Sub\\Deep\nenabled = 1\norder = 1\n"
             "\n[mod]\nroot = %s-other/X\nenabled = 1\norder = 2\n"
             "\n[mod]\nroot = /elsewhere/Y\nenabled = 1\norder = 3\n", mods, mods, mods);
    BW_CHECK_EQ("the old mod record", put(saves, "mods.ini", record), 0);
    BW_CHECK_EQ("old settings", put(g_work, "home-settings.ini", "volume = 37\n"), 0);
    BW_CHECK_EQ("the old folder has saves", bw_profile_has_saves(saves), 1);

    /* they are Black's: White does not take them */
    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, other, sizeof(other), note, sizeof(note));
    BW_CHECK_EQ("White: ready, nothing copied", rc, BW_PROFILE_READY);
    BW_CHECK_EQ("White has no save", holds(other, "saves/pokeblack.sav", NULL), 1);
    BW_CHECK_EQ("no mark yet", holds(saves, BW_PROFILE_COPIED_NAME, NULL), 1);

    /* Black takes all of it */
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("Black: copied", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("the note says so", strstr(note, "were copied") != NULL, 1);
    BW_CHECK_EQ("slot 1", holds(dir, "saves/pokeblack.sav", "SAVE-ONE"), 1);
    BW_CHECK_EQ("slot 3 keeps its number", holds(dir, "saves/slots/save3.sav", "SAVE-THREE"), 1);
    BW_CHECK_EQ("the slot in force", holds(dir, "saves/slot.txt", "3\n"), 1);
    BW_CHECK_EQ("Your games as it was", holds(dir, "saves/games.ini", "3 solo arkhipelagos solo host -\n"), 1);
    BW_CHECK_EQ("the multiplayer game", holds(dir, "saves/multiplayer/Friends/host.sav", "MP"), 1);
    BW_CHECK_EQ("the mod data", holds(dir, "saves/mods/nuzlocke/pokeblack.bin", "STATE"), 1);
    BW_CHECK_EQ("the mod", holds(dir, "mods/Nuzlocke/mod.bw", "MOD"), 1);
    /* the record names the copies of the old folder's mods, each switch as
     * it was, and the other mods as before */
    snprintf(moved, sizeof(moved),
             "# eidos mod record\n\n[mod]\nroot = %s/mods/Nuzlocke\nenabled = 0\norder = 0\n"
             "\n[mod]\nroot = %s/mods/Sub\\Deep\nenabled = 1\norder = 1\n"
             "\n[mod]\nroot = %s-other/X\nenabled = 1\norder = 2\n"
             "\n[mod]\nroot = /elsewhere/Y\nenabled = 1\norder = 3\n", dir, dir, mods);
    BW_CHECK_EQ("the mod record names the copied mods", holds(dir, "saves/mods.ini", moved), 1);
    BW_CHECK_EQ("no half-written record is left", holds(dir, "saves/mods.ini.new", NULL), 1);
    BW_CHECK_EQ("the old record is as it was", holds(saves, "mods.ini", record), 1);
    BW_CHECK_EQ("the settings", holds(dir, "settings.ini", "volume = 37\n"), 1);
    BW_CHECK_EQ("the originals are left", holds(saves, "pokeblack.sav", "SAVE-ONE")
                                          && holds(saves, "slots/save3.sav", "SAVE-THREE")
                                          && holds(mods, "Nuzlocke/mod.bw", "MOD")
                                          && holds(g_work, "home-settings.ini", "volume = 37\n"), 1);
    BW_CHECK_EQ("the old folder is marked", holds(saves, BW_PROFILE_COPIED_NAME, NULL), 0);
    BW_CHECK_EQ("the mark is not copied into the profile", holds(dir, "saves/" BW_PROFILE_COPIED_NAME, NULL), 1);

    /* later starts copy nothing: a save played on since stays as it is */
    BW_CHECK_EQ("the player saves in the profile", put(dir, "saves/pokeblack.sav", "PLAYED-ON"), 0);
    BW_CHECK_EQ("a file added to the old folder later", put(saves, "slots/save5.sav", "LATE"), 0);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("the next start: ready", rc, BW_PROFILE_READY);
    BW_CHECK_EQ("nothing to say", note[0], 0);
    BW_CHECK_EQ("the save is the player's", holds(dir, "saves/pokeblack.sav", "PLAYED-ON"), 1);
    BW_CHECK_EQ("nothing more came over", holds(dir, "saves/slots/save5.sav", NULL), 1);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("and the one after", rc == BW_PROFILE_READY && holds(dir, "saves/pokeblack.sav", "PLAYED-ON"), 1);

    /* 7: a profile with no settings takes the other game's at its start; its own are never replaced */
    BW_CHECK_EQ("White had none while Black had none", holds(other, "settings.ini", NULL), 1);
    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, other, sizeof(other), note, sizeof(note));
    BW_CHECK_EQ("White's next start takes Black's settings", rc == BW_PROFILE_READY
                                                             && holds(other, "settings.ini", "volume = 37\n"), 1);
    BW_CHECK_EQ("and still none of Black's saves", holds(other, "saves/pokeblack.sav", NULL), 1);
    BW_CHECK_EQ("White changes its settings", put(other, "settings.ini", "volume = 5\n"), 0);
    BW_CHECK_EQ("Black changes its settings", put(dir, "settings.ini", "volume = 90\n"), 0);
    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, other, sizeof(other), note, sizeof(note));
    BW_CHECK_EQ("White keeps its own", rc == BW_PROFILE_READY && holds(other, "settings.ini", "volume = 5\n"), 1);

    /* saves of a game nothing names go to the first game that starts */
    user_of("user-unknown", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    old.mods = NULL;
    old.settings = NULL;
    old.game = BW_GAME_UNKNOWN;
    BW_CHECK_EQ("an old save", put(saves, "pokeblack.sav", "WHOSE"), 0);
    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("White starts first and takes it", rc == BW_PROFILE_COPIED
                                                   && holds(dir, "saves/pokeblack.sav", "WHOSE"), 1);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, dir, sizeof(dir), note, sizeof(note));
    BW_CHECK_EQ("Black does not get a second copy", rc == BW_PROFILE_READY
                                                    && holds(dir, "saves/pokeblack.sav", NULL), 1);

    /* a multiplayer game alone counts as saves */
    user_of("user-mp", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    old.game = BW_GAME_BLACK;
    BW_CHECK_EQ("only a multiplayer game", put(saves, "multiplayer/Solo/host.sav", "MP"), 0);
    BW_CHECK_EQ("counts", bw_profile_has_saves(saves), 1);
    BW_CHECK_EQ("an empty slots folder does not", (user_of("user-empty", user, sizeof(user), saves, sizeof(saves)),
                                                   path_of(p, sizeof(p), saves, "slots"), bw_mkdirs_utf8(p),
                                                   bw_profile_has_saves(saves)), 0);
    return 0;
}

static int test_profile_slots(void)
{
    char user[600], saves[700], mods[700], dir[700], own[800], note[2400], p[900];
    BwProfileOld old;
    int rc;

    user_of("user-slots", user, sizeof(user), saves, sizeof(saves));
    path_of(mods, sizeof(mods), user, "mods");
    memset(&old, 0, sizeof(old));
    old.saves = saves;
    old.mods = mods;
    old.game = BW_GAME_BLACK;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_BLACK, dir, sizeof(dir)), 0);

    /* the profile: slots 1, 2 and 5 used, slot 3 a file of no length, one of Your games, a multiplayer game */
    BW_CHECK_EQ("own 1", put(dir, "saves/pokeblack.sav", "OWN-1"), 0);
    BW_CHECK_EQ("own 2", put(dir, "saves/slots/save2.sav", "OWN-2"), 0);
    BW_CHECK_EQ("own 3, empty", put(dir, "saves/slots/save3.sav", ""), 0);
    BW_CHECK_EQ("own 5", put(dir, "saves/slots/save5.sav", "OWN-5"), 0);
    BW_CHECK_EQ("own slot record", put(dir, "saves/slot.txt", "2\n"), 0);
    BW_CHECK_EQ("own Your games", put(dir, "saves/games.ini", "5 world soul_link coop host ABC234\n"), 0);
    BW_CHECK_EQ("own multiplayer game", put(dir, "saves/multiplayer/Friends/host.sav", "OWN-MP"), 0);
    BW_CHECK_EQ("own settings", put(dir, "settings.ini", "volume = 11\n"), 0);
    BW_CHECK_EQ("own mod data", put(dir, "saves/mods/nuzlocke/pokeblack.bin", "OWN-STATE"), 0);
    /* the old folder: slots 1, 2, 4 and 8; 2 and 8 of Your games, a comment, a line for a slot with no save;
     * a multiplayer game of the same name and another; mod data, a mod and settings that are not taken */
    BW_CHECK_EQ("old 1", put(saves, "pokeblack.sav", "OLD-1"), 0);
    BW_CHECK_EQ("old 2", put(saves, "slots/save2.sav", "OLD-2"), 0);
    BW_CHECK_EQ("old 4", put(saves, "slots/save4.sav", "OLD-4"), 0);
    BW_CHECK_EQ("old 8", put(saves, "slots/save8.sav", "OLD-8"), 0);
    BW_CHECK_EQ("old slot record", put(saves, "slot.txt", "4\n"), 0);
    BW_CHECK_EQ("old Your games", put(saves, "games.ini", "# the games\n2 solo arkhipelagos solo host -\n"
                                                           "6 solo gone solo host -\n8 room arkhipelagos ap joined AP38281"), 0);
    BW_CHECK_EQ("old multiplayer, same name", put(saves, "multiplayer/Friends/host.sav", "OLD-MP"), 0);
    BW_CHECK_EQ("old multiplayer, another", put(saves, "multiplayer/Rivals/host.sav", "OLD-MP-2"), 0);
    BW_CHECK_EQ("old multiplayer, a file", put(saves, "multiplayer/secrets.txt", "OLD-SECRET"), 0);
    BW_CHECK_EQ("old mod data", put(saves, "mods/nuzlocke/pokeblack.bin", "OLD-STATE"), 0);
    BW_CHECK_EQ("an old mod", put(mods, "Old/mod.bw", "OLD-MOD"), 0);

    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("copied", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("into free save slots", strstr(note, "free save slots") != NULL, 1);
    BW_CHECK_EQ("and one multiplayer game stayed behind", strstr(note, "1 multiplayer game ") != NULL, 1);
    /* the free slots are 4, 6, 7, 8 (3 has a file, if one of no length): the old saves take them in order */
    BW_CHECK_EQ("the file of no length stays", holds(dir, "saves/slots/save3.sav", ""), 1);
    BW_CHECK_EQ("old 1 is slot 4", holds(dir, "saves/slots/save4.sav", "OLD-1"), 1);
    BW_CHECK_EQ("old 2 is slot 6", holds(dir, "saves/slots/save6.sav", "OLD-2"), 1);
    BW_CHECK_EQ("old 4 is slot 7", holds(dir, "saves/slots/save7.sav", "OLD-4"), 1);
    BW_CHECK_EQ("old 8 is slot 8", holds(dir, "saves/slots/save8.sav", "OLD-8"), 1);
    BW_CHECK_EQ("slot 9 stays free", holds(dir, "saves/slots/save9.sav", NULL), 1);
    BW_CHECK_EQ("the profile's saves are as they were", holds(dir, "saves/pokeblack.sav", "OWN-1")
                                                        && holds(dir, "saves/slots/save2.sav", "OWN-2")
                                                        && holds(dir, "saves/slots/save5.sav", "OWN-5"), 1);
    BW_CHECK_EQ("its slot in force too", holds(dir, "saves/slot.txt", "2\n"), 1);
    BW_CHECK_EQ("Your games: its line, then the old ones under their new slots",
                holds(dir, "saves/games.ini", "5 world soul_link coop host ABC234\n"
                                              "6 solo arkhipelagos solo host -\n"
                                              "8 room arkhipelagos ap joined AP38281\n"), 1);
    BW_CHECK_EQ("its multiplayer game is its own", holds(dir, "saves/multiplayer/Friends/host.sav", "OWN-MP"), 1);
    BW_CHECK_EQ("the other one came", holds(dir, "saves/multiplayer/Rivals/host.sav", "OLD-MP-2"), 1);
    BW_CHECK_EQ("and the file", holds(dir, "saves/multiplayer/secrets.txt", "OLD-SECRET"), 1);
    BW_CHECK_EQ("its mod data stands", holds(dir, "saves/mods/nuzlocke/pokeblack.bin", "OWN-STATE"), 1);
    BW_CHECK_EQ("the old mod is not taken", holds(dir, "mods/Old/mod.bw", NULL), 1);
    BW_CHECK_EQ("its settings stand", holds(dir, "settings.ini", "volume = 11\n"), 1);
    BW_CHECK_EQ("no staging folder is left", holds(dir, "saves/import-old", NULL), 1);
    BW_CHECK_EQ("the old folder is as it was", holds(saves, "pokeblack.sav", "OLD-1")
                                               && holds(saves, "slots/save8.sav", "OLD-8")
                                               && holds(saves, "multiplayer/Friends/host.sav", "OLD-MP"), 1);
    BW_CHECK_EQ("and marked", holds(saves, BW_PROFILE_COPIED_NAME, NULL), 0);
    BW_CHECK_EQ("the folder handed back is the profile", strcmp(own, dir), 0);

    /* nothing twice */
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("the next start: ready", rc == BW_PROFILE_READY && note[0] == '\0', 1);
    BW_CHECK_EQ("slot 9 is still free", holds(dir, "saves/slots/save9.sav", NULL), 1);
    BW_CHECK_EQ("Your games has three lines", holds(dir, "saves/games.ini", "5 world soul_link coop host ABC234\n"
                                                                             "6 solo arkhipelagos solo host -\n"
                                                                             "8 room arkhipelagos ap joined AP38281\n"), 1);

    /* the profile's slot 1 free: an old save takes it */
    user_of("user-slot1", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    old.mods = NULL;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_BLACK, dir, sizeof(dir)), 0);
    BW_CHECK_EQ("own 2 only", put(dir, "saves/slots/save2.sav", "OWN-2"), 0);
    BW_CHECK_EQ("old 5 only", put(saves, "slots/save5.sav", "OLD-5"), 0);
    BW_CHECK_EQ("old Your games", put(saves, "games.ini", "5 solo arkhipelagos solo host -\n"), 0);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("copied", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("into slot 1", holds(dir, "saves/pokeblack.sav", "OLD-5"), 1);
    BW_CHECK_EQ("with its line", holds(dir, "saves/games.ini", "1 solo arkhipelagos solo host -\n"), 1);
    BW_CHECK_EQ("no multiplayer note", strstr(note, "multiplayer") == NULL, 1);

    /* eight old saves into a profile that has only a multiplayer game */
    user_of("user-eight", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_BLACK, dir, sizeof(dir)), 0);
    BW_CHECK_EQ("own multiplayer game", put(dir, "saves/multiplayer/Mine/host.sav", "OWN-MP"), 0);
    BW_CHECK_EQ("old 1", put(saves, "pokeblack.sav", "OLD-1"), 0);
    {
        int n;

        for (n = 2; n <= 8; n++) {
            char rel[32], text[16];

            snprintf(rel, sizeof(rel), "slots/save%d.sav", n);
            snprintf(text, sizeof(text), "OLD-%d", n);
            BW_CHECK_EQ("old slot", put(saves, rel, text), 0);
        }
    }
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("all eight fit", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("each in its own number", holds(dir, "saves/pokeblack.sav", "OLD-1")
                                          && holds(dir, "saves/slots/save2.sav", "OLD-2")
                                          && holds(dir, "saves/slots/save8.sav", "OLD-8"), 1);
    (void)p;
    return 0;
}

static int test_profile_full(void)
{
    char user[600], saves[700], mods[700], settings[700], dir[700], own[800], own2[800], note[2400];
    BwProfileOld old;
    int rc, n;

    user_of("user-full", user, sizeof(user), saves, sizeof(saves));
    path_of(mods, sizeof(mods), user, "mods");
    path_of(settings, sizeof(settings), g_work, "full-settings.ini");
    memset(&old, 0, sizeof(old));
    old.saves = saves;
    old.mods = mods;
    old.settings = settings;
    old.game = BW_GAME_WHITE;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_WHITE, dir, sizeof(dir)), 0);
    BW_CHECK_EQ("own 1", put(dir, "saves/pokeblack.sav", "OWN-1"), 0);
    for (n = 2; n <= 7; n++) {
        char rel[40];

        snprintf(rel, sizeof(rel), "saves/slots/save%d.sav", n);
        BW_CHECK_EQ("own slot", put(dir, rel, "OWN"), 0);
    }
    /* one slot free below 9, two old saves: the second goes past 8 */
    BW_CHECK_EQ("old 1", put(saves, "pokeblack.sav", "OLD-1"), 0);
    BW_CHECK_EQ("old 2", put(saves, "slots/save2.sav", "OLD-2"), 0);
    BW_CHECK_EQ("old Your games", put(saves, "games.ini", "2 solo arkhipelagos solo host -\n"), 0);
    BW_CHECK_EQ("an old mod", put(mods, "Old/mod.bw", "OLD-MOD"), 0);
    BW_CHECK_EQ("old settings", put(g_work, "full-settings.ini", "volume = 37\n"), 0);

    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("copied", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("into free save slots", strstr(note, "free save slots") != NULL, 1);
    BW_CHECK_EQ("old 1 is slot 8", holds(dir, "saves/slots/save8.sav", "OLD-1"), 1);
    BW_CHECK_EQ("old 2 is slot 9", holds(dir, "saves/slots/save9.sav", "OLD-2"), 1);
    BW_CHECK_EQ("its line under slot 9", holds(dir, "saves/games.ini", "9 solo arkhipelagos solo host -\n"), 1);
    BW_CHECK_EQ("the profile's own saves stand", holds(dir, "saves/pokeblack.sav", "OWN-1")
                                                 && holds(dir, "saves/slots/save7.sav", "OWN"), 1);
    path_of(own2, sizeof(own2), user, "profiles/Standalone White");
    BW_CHECK_EQ("no profile of their own", holds(own2, "saves/pokeblack.sav", NULL), 1);
    BW_CHECK_EQ("the run plays from the game's profile", strcmp(own, dir), 0);
    BW_CHECK_EQ("the old folder is marked", holds(saves, BW_PROFILE_COPIED_NAME, NULL), 0);
    rc = bw_profile_prepare(user, BW_GAME_WHITE, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("the next start: ready, nothing to say", rc == BW_PROFILE_READY && note[0] == '\0', 1);

    /* Black, every slot to 8 used: the old save is slot 9 */
    user_of("user-full-b", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    old.mods = NULL;
    old.settings = NULL;
    old.game = BW_GAME_BLACK;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_BLACK, dir, sizeof(dir)), 0);
    BW_CHECK_EQ("own 1", put(dir, "saves/pokeblack.sav", "OWN-1"), 0);
    for (n = 2; n <= 8; n++) {
        char rel[40];

        snprintf(rel, sizeof(rel), "saves/slots/save%d.sav", n);
        BW_CHECK_EQ("own slot", put(dir, rel, "OWN"), 0);
    }
    BW_CHECK_EQ("old 1", put(saves, "pokeblack.sav", "OLD-1"), 0);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("copied", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("slot 9", holds(dir, "saves/slots/save9.sav", "OLD-1"), 1);
    BW_CHECK_EQ("slot 8 as it was", holds(dir, "saves/slots/save8.sav", "OWN"), 1);
    return 0;
}

static int test_profile_failed(void)
{
    char user[600], saves[700], dir[700], own[800], note[2400], p[900];
    BwProfileOld old;
    int rc;

    /* the profile's slots folder cannot be made (a file has its name): nothing is copied or marked */
    user_of("user-fail", user, sizeof(user), saves, sizeof(saves));
    memset(&old, 0, sizeof(old));
    old.saves = saves;
    old.game = BW_GAME_BLACK;
    BW_CHECK_EQ("the profile's folder", bw_profile_dir(user, BW_GAME_BLACK, dir, sizeof(dir)), 0);
    BW_CHECK_EQ("own 1", put(dir, "saves/pokeblack.sav", "OWN-1"), 0);
    BW_CHECK_EQ("a file named slots", put(dir, "saves/slots", "in the way"), 0);
    BW_CHECK_EQ("old 1", put(saves, "pokeblack.sav", "OLD-1"), 0);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("the profile is still the run's", rc, BW_PROFILE_READY);
    BW_CHECK_EQ("the note says it could not", strstr(note, "could not be copied") != NULL, 1);
    BW_CHECK_EQ("the profile's save is as it was", holds(dir, "saves/pokeblack.sav", "OWN-1"), 1);
    BW_CHECK_EQ("no staging folder is left", holds(dir, "saves/import-old", NULL), 1);
    BW_CHECK_EQ("the old save is as it was", holds(saves, "pokeblack.sav", "OLD-1"), 1);
    BW_CHECK_EQ("no mark", holds(saves, BW_PROFILE_COPIED_NAME, NULL), 1);
    /* the way is cleared: the next start does it */
    path_of(p, sizeof(p), dir, "saves/slots");
    bw_remove_tree_utf8(p);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("the next start copies", rc, BW_PROFILE_COPIED);
    BW_CHECK_EQ("into slot 2", holds(dir, "saves/slots/save2.sav", "OLD-1"), 1);

    /* a profile folder that cannot be made at all: the caller keeps the old places */
    user_of("user-fail2", user, sizeof(user), saves, sizeof(saves));
    old.saves = saves;
    BW_CHECK_EQ("a file named profiles", put(user, "profiles", "in the way"), 0);
    rc = bw_profile_prepare(user, BW_GAME_BLACK, &old, own, sizeof(own), note, sizeof(note));
    BW_CHECK_EQ("failed", rc, BW_PROFILE_FAILED);
    BW_CHECK_EQ("and says so", note[0] != '\0', 1);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_profiles <work_dir>\n");
        return 2;
    }
    snprintf(g_work, sizeof(g_work), "%s", argv[1]);
    bw_remove_tree_utf8(g_work);
    if (bw_mkdirs_utf8(g_work) != 0) {
        printf("cannot create %s\n", g_work);
        return 1;
    }
    bw_setup_set_home(g_work);
    if (test_games() || test_profile_copy() || test_profile_slots() || test_profile_full()
        || test_profile_failed()) {
        BW_REPORT("test_profiles");
    }
    if (bw_failures == 0) {
        bw_remove_tree_utf8(g_work);
    }
    BW_REPORT("test_profiles");
}
