/*
 * tests/test_save_tidy.c -- a saves folder put in order (sdk/save_tidy.h),
 * on save images made here the way the game writes them.
 *
 * What it must never do: write over a file or lose one.  Every file has a
 * mark of its own at the end of its image, and after each step the test
 * finds every mark exactly once, in the place the rules say, with every
 * byte as it was.
 */
/* setenv, fork and waitpid are POSIX, not C99 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "save.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "save_tidy.h"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define T_DIR "test_save_tidy_tmp"

static const char *g_self;      /* this program, for the four-at-once case */

#define IMAGE       0x80000u
#define COPY_SIZE   0x23F9Cu
#define SECOND      0x24000u
#define TABLE       0x23F00u
#define FOOTER      (COPY_SIZE - 0x10u)
#define SYS_AT      0x19400u
#define SYS_SIZE    0x68u
#define MARK_AT     0x7FF00u     /* past both copies: a file's own mark */

static void put16(u8 *p, unsigned v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    put16(p, v & 0xFFFFu);
    put16(p + 2, v >> 16);
}

static unsigned crc16(const u8 *p, size_t n)
{
    unsigned c = 0xFFFFu;
    size_t i;
    int b;

    for (i = 0; i < n; i++) {
        c ^= (unsigned)p[i] << 8;
        for (b = 0; b < 8; b++) {
            c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) : (c << 1);
        }
        c &= 0xFFFFu;
    }
    return c;
}

/* One copy of the save data: the system block with its active flag, the
 * table and the footer. */
static void make_copy(u8 *c, u32 count, int active, int broken_system)
{
    unsigned sys;

    c[SYS_AT + 0x2C] = (u8)active;
    sys = crc16(c + SYS_AT, SYS_SIZE);
    put16(c + SYS_AT + SYS_SIZE + 2, sys);
    put16(c + TABLE + 0x1B * 2, broken_system ? sys ^ 1u : sys);
    put32(c + FOOTER, count);
    put32(c + FOOTER + 4, COPY_SIZE);
    put16(c + FOOTER + 0xE, crc16(c + TABLE, FOOTER - TABLE));
}

enum { SAVE, ACTIVE, GARBAGE, EMPTY, SHORT, NO_SYSTEM, OLD_ACTIVE };

/* An image of the kind asked for, marked `mark`.  Its size, or 0. */
static u8 *image(int kind, const char *mark, u32 *size)
{
    u8 *img = (u8 *)calloc(1, IMAGE);
    u32 i;

    *size = IMAGE;
    switch (kind) {
    case SAVE:
        make_copy(img, 5, 0, 0);
        make_copy(img + SECOND, 4, 0, 0);
        break;
    case ACTIVE:                        /* a new game that never saved */
        make_copy(img, 1, 1, 0);
        break;
    case OLD_ACTIVE:                    /* the newer copy saved: a save */
        make_copy(img, 1, 1, 0);
        make_copy(img + SECOND, 2, 0, 0);
        break;
    case NO_SYSTEM:                     /* a copy holds, its system block not */
        make_copy(img, 1, 0, 1);
        break;
    case GARBAGE:
        for (i = 0; i < IMAGE; i++) {
            img[i] = (u8)(i * 7u + 3u);
        }
        break;
    case EMPTY:
        *size = 0;
        break;
    case SHORT:
        *size = 0x1000;
        break;
    }
    if (*size > MARK_AT + 32) {
        snprintf((char *)img + MARK_AT, 32, "%s", mark);
    } else if (*size > 0) {
        snprintf((char *)img, 32, "%s", mark);
    }
    return img;
}

static int put_file(const char *rel, int kind, const char *mark)
{
    char path[512], dir[512];
    u32 size;
    u8 *img = image(kind, mark, &size);
    const char *slash = strrchr(rel, '/');
    FILE *f;
    int ok;

    if (slash != NULL) {
        snprintf(dir, sizeof dir, "%s/%.*s", T_DIR, (int)(slash - rel), rel);
        (void)bw_plat_mkdirs(dir);
    }
    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    f = fopen(path, "wb");
    ok = f != NULL && (size == 0 || fwrite(img, 1, size, f) == size);
    if (f != NULL) {
        fclose(f);
    }
    free(img);
    return ok ? 0 : -1;
}

static int put_text(const char *rel, const char *text)
{
    char path[512];

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    return bw_save_write_atomic(path, text, (u32)strlen(text));
}

/* Whether `rel` is the file made with `kind` and `mark`, byte for byte. */
static int is_file(const char *rel, int kind, const char *mark)
{
    char path[512];
    u32 size;
    u8 *want = image(kind, mark, &size), *have = NULL;
    FILE *f;
    long n = -1;
    int same = 0;

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    if ((f = fopen(path, "rb")) != NULL) {
        if (fseek(f, 0, SEEK_END) == 0) {
            n = ftell(f);
        }
        rewind(f);
        if (n == (long)size) {
            have = (u8 *)malloc(size ? size : 1);
            same = have != NULL && (size == 0 || fread(have, 1, size, f) == size) && memcmp(have, want, size) == 0;
        }
        fclose(f);
    }
    free(want);
    free(have);
    return same;
}

static int gone(const char *rel)
{
    char path[512];

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    return bw_plat_path_kind(path) == BW_PATH_NONE;
}

static int text_is(const char *rel, const char *want)
{
    char path[512], buf[1024];
    long n;

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    n = bw_save_read(path, (u8 *)buf, sizeof buf - 1);
    if (n < 0) {
        return 0;
    }
    buf[n] = '\0';
    return strcmp(buf, want) == 0;
}

static int count_entry(void *ctx, const char *name)
{
    (void)name;
    (*(int *)ctx)++;
    return 0;
}

static int count_in(const char *rel)
{
    char path[512];
    int n = 0;

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    (void)bw_plat_list_dir(path, count_entry, &n);
    return n;
}

/* The folder of the report: slots 1 to 8, three of them no save, slot 5 an
 * older eidos's Multiplayer screen game, slot 7 in force. */
static void make_report_folder(void)
{
    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    put_file("pokeblack.sav", SAVE, "one");
    put_file("slots/save2.sav", GARBAGE, "two");
    put_file("slots/save3.sav", SAVE, "three");
    put_file("slots/save4.sav", ACTIVE, "four");
    put_file("slots/save5.sav", SAVE, "five");
    put_file("slots/save6.sav", SAVE, "six");
    put_file("slots/save7.sav", OLD_ACTIVE, "seven");
    put_file("slots/save8.sav", EMPTY, "eight");
    put_text("games.ini", "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                          "5 solo arkhipelagos solo host -\n");
    put_text("slot.txt", "7\n");
    put_file("slots/notes.txt", SHORT, "not a slot");  /* not a slot's name: left alone */
}

/* A mod's state file for a save (sdk/mod_savedata.c: the header's magic
 * "BWMS" and save_check at 20), marked. */
extern u32 bw_mod_savedata_crc(const void *data, u32 size);

static int put_state(const char *rel, u32 save_check, const char *mark)
{
    u8 blob[64];
    char path[512], dir[512];
    const char *slash = strrchr(rel, '/');

    memset(blob, 0, sizeof blob);
    put32(blob, 0x534D5742u);
    put32(blob + 20, save_check);
    snprintf((char *)blob + 32, 32, "%s", mark);
    snprintf(dir, sizeof dir, "%s/%.*s", T_DIR, (int)(slash - rel), rel);
    (void)bw_plat_mkdirs(dir);
    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    return bw_save_write_atomic(path, blob, sizeof blob);
}

static int state_is(const char *rel, const char *mark)
{
    char path[512];
    u8 blob[64];

    snprintf(path, sizeof path, "%s/%s", T_DIR, rel);
    return bw_save_read(path, blob, sizeof blob) == (long)sizeof blob && strcmp((char *)blob + 32, mark) == 0;
}

/* The checksum the per-save layer writes with a save: CRC-32 of the 512 KB
 * chip (an image made here is the whole chip). */
static u32 save_check_of(int kind, const char *mark)
{
    u32 size;
    u8 *img = image(kind, mark, &size);
    u32 c = bw_mod_savedata_crc(img, size);

    free(img);
    return c;
}

static void check_report_folder_done(const char *when)
{
    char label[96];

#define AT(rel, kind, mark) \
    snprintf(label, sizeof label, "%s: %s holds %s", when, rel, mark); \
    BW_CHECK_EQ(label, is_file(rel, kind, mark), 1)
    AT("pokeblack.sav", SAVE, "one");
    AT("slots/save2.sav", SAVE, "three");
    AT("slots/save3.sav", SAVE, "six");
    AT("slots/save4.sav", OLD_ACTIVE, "seven");
    AT("games/game1.sav", SAVE, "five");
    AT("unreadable/slot2.sav", GARBAGE, "two");
    AT("unreadable/slot4.sav", ACTIVE, "four");
    AT("unreadable/slot8.sav", EMPTY, "eight");
    AT("slots/notes.txt", SHORT, "not a slot");
#undef AT
    snprintf(label, sizeof label, "%s: no fifth slot", when);
    BW_CHECK_EQ(label, gone("slots/save5.sav"), 1);
    snprintf(label, sizeof label, "%s: files in slots/", when);
    BW_CHECK_EQ(label, (long)count_in("slots"), 4);  /* save2..4 and notes.txt */
    snprintf(label, sizeof label, "%s: slot.txt follows its save", when);
    BW_CHECK_EQ(label, text_is("slot.txt", "4\n"), 1);
    snprintf(label, sizeof label, "%s: the game's line in games/", when);
    BW_CHECK_EQ(label, text_is("games/games.ini", "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                                                  "1 solo arkhipelagos solo host -\n"), 1);
    snprintf(label, sizeof label, "%s: games.ini kept, not deleted", when);
    BW_CHECK_EQ(label, text_is("games.ini.moved", "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                                                  "5 solo arkhipelagos solo host -\n"), 1);
    snprintf(label, sizeof label, "%s: games.ini itself gone", when);
    BW_CHECK_EQ(label, gone("games.ini"), 1);
    snprintf(label, sizeof label, "%s: no plan left", when);
    BW_CHECK_EQ(label, gone("tidy.txt") && gone("tidy.step"), 1);
}

int main(int argc, char **argv)
{
    char note[400];
    u32 size;
    u8 *img;

    /* a child of the four-at-once case: one tidy of the folder */
    if (argc == 3 && strcmp(argv[1], "--child") == 0) {
        return bw_saves_tidy(argv[2], note, sizeof note) < 0 ? 1 : 0;
    }
    g_self = argv[0];

    printf("which images hold a save\n");
    img = image(SAVE, "x", &size);
    BW_CHECK_EQ("two good copies", bw_save_bytes_loadable(img, size), 1);
    free(img);
    img = image(ACTIVE, "x", &size);
    BW_CHECK_EQ("a new game never saved", bw_save_bytes_loadable(img, size), 0);
    free(img);
    img = image(OLD_ACTIVE, "x", &size);
    BW_CHECK_EQ("the newer copy saved", bw_save_bytes_loadable(img, size), 1);
    free(img);
    img = image(NO_SYSTEM, "x", &size);
    BW_CHECK_EQ("not sure: a save", bw_save_bytes_loadable(img, size), 1);
    free(img);
    img = image(GARBAGE, "x", &size);
    BW_CHECK_EQ("no copy holds", bw_save_bytes_loadable(img, size), 0);
    free(img);
    BW_CHECK_EQ("cut short", bw_save_bytes_loadable((const u8 *)"abc", 3), 0);
    BW_CHECK_EQ("a file not there", bw_save_image_loadable(T_DIR "/nothing.sav"), -1);

    printf("the report's folder\n");
    make_report_folder();
    BW_CHECK_EQ("files moved", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    printf("  %s\n", note);
    check_report_folder_done("first start");
    BW_CHECK_EQ("in order now: nothing to do", bw_saves_tidy(T_DIR, note, sizeof note), 0);
    check_report_folder_done("second start");

    printf("the saves' mod files move with them\n");
    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    put_file("pokeblack.sav", SAVE, "one");
    put_file("slots/save2.sav", GARBAGE, "two");
    put_file("slots/save3.sav", SAVE, "three");
    put_text("slot.txt", "3\n");
    /* written with save three while every save used "pokeblack" */
    put_state("mods/arkh/pokeblack.bin", save_check_of(SAVE, "three"), "arkh of three");
    put_state("mods/arkh/slots-save3.bin.v2", 0, "arkh aside of three");
    /* slot 1's own: stays */
    put_state("mods/other/pokeblack.bin", save_check_of(SAVE, "one"), "other of one");
    /* the file of slot 2, which holds no save: with it to unreadable/ */
    put_state("mods/m.dex/slots-save2.bin", 0, "dex of two");
    /* slot 3's own: with it to slot 2, once slot 2's has gone to unreadable/ */
    put_state("mods/m.dex/slots-save3.bin", 0, "dex of three");
    BW_CHECK_EQ("files moved", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    BW_CHECK_EQ("save three is slot 2", is_file("slots/save2.sav", SAVE, "three"), 1);
    BW_CHECK_EQ("the shared file went to save three, then with it", state_is("mods/arkh/slots-save2.bin", "arkh of three"), 1);
    BW_CHECK_EQ("its aside file with it", state_is("mods/arkh/slots-save2.bin.v2", "arkh aside of three"), 1);
    BW_CHECK_EQ("no shared file left", gone("mods/arkh/pokeblack.bin"), 1);
    BW_CHECK_EQ("slot 1's own stays", state_is("mods/other/pokeblack.bin", "other of one"), 1);
    BW_CHECK_EQ("slot 2's to unreadable/ with its file", state_is("mods/m.dex/unreadable-slot2.bin", "dex of two"), 1);
    BW_CHECK_EQ("slot 3's file to slot 2", state_is("mods/m.dex/slots-save2.bin", "dex of three"), 1);
    BW_CHECK_EQ("nothing left at the old names", gone("mods/m.dex/slots-save3.bin") && gone("mods/arkh/slots-save3.bin.v2"), 1);
    BW_CHECK_EQ("in order now: nothing to do", bw_saves_tidy(T_DIR, note, sizeof note), 0);
    {
        char name[64];

        BW_CHECK_EQ("the name of slot 1", bw_save_state_name("pokeblack.sav", name, sizeof name) == 0
                    && strcmp(name, "pokeblack") == 0, 1);
        BW_CHECK_EQ("the name of a game", bw_save_state_name("games/game3.sav", name, sizeof name) == 0
                    && strcmp(name, "games-game3") == 0, 1);
    }

    printf("a crash after each move, then the next start\n");
    {
        int stop;
        char env[48], label[64];

        /* the plan has 7 moves: 1 to games/, 3 set aside, 3 renumbered */
        for (stop = 0; stop < 7; stop++) {
            make_report_folder();
            snprintf(env, sizeof env, "%d", stop);
#ifdef _WIN32
            _putenv_s("EIDOS_TIDY_STOP_AFTER", env);
#else
            setenv("EIDOS_TIDY_STOP_AFTER", env, 1);
#endif
            snprintf(label, sizeof label, "stopped after %d moves", stop);
            BW_CHECK_EQ(label, bw_saves_tidy(T_DIR, note, sizeof note), -1);
            BW_CHECK_EQ("the plan stays", gone("tidy.txt"), 0);
#ifdef _WIN32
            _putenv_s("EIDOS_TIDY_STOP_AFTER", "");
#else
            unsetenv("EIDOS_TIDY_STOP_AFTER");
#endif
            BW_CHECK_EQ("the next start goes on", bw_saves_tidy(T_DIR, note, sizeof note), 1);
            snprintf(label, sizeof label, "after a stop at %d", stop);
            check_report_folder_done(label);
        }
        /* a crash after a move but before its step was written: the next
         * start does that move again, and finds it done */
        for (stop = 1; stop < 7; stop++) {
            char step[16];

            make_report_folder();
            snprintf(env, sizeof env, "%d", stop);
#ifdef _WIN32
            _putenv_s("EIDOS_TIDY_STOP_AFTER", env);
#else
            setenv("EIDOS_TIDY_STOP_AFTER", env, 1);
#endif
            BW_CHECK_EQ("stopped", bw_saves_tidy(T_DIR, note, sizeof note), -1);
#ifdef _WIN32
            _putenv_s("EIDOS_TIDY_STOP_AFTER", "");
#else
            unsetenv("EIDOS_TIDY_STOP_AFTER");
#endif
            snprintf(step, sizeof step, "%d\n", stop - 1);
            put_text("tidy.step", step);
            BW_CHECK_EQ("the next start goes on from the step before", bw_saves_tidy(T_DIR, note, sizeof note), 1);
            snprintf(label, sizeof label, "after a lost step at %d", stop);
            check_report_folder_done(label);
        }
    }

    printf("a file in the way is never written over\n");
    make_report_folder();
#ifdef _WIN32
    _putenv_s("EIDOS_TIDY_STOP_AFTER", "0");
#else
    setenv("EIDOS_TIDY_STOP_AFTER", "0", 1);
#endif
    BW_CHECK_EQ("the plan is made", bw_saves_tidy(T_DIR, note, sizeof note), -1);
#ifdef _WIN32
    _putenv_s("EIDOS_TIDY_STOP_AFTER", "");
#else
    unsetenv("EIDOS_TIDY_STOP_AFTER");
#endif
    put_file("games/game1.sav", SAVE, "someone else's");   /* appears before the move */
    BW_CHECK_EQ("the step fails", bw_saves_tidy(T_DIR, note, sizeof note), -1);
    printf("  %s\n", note);
    BW_CHECK_EQ("the file in the way stays", is_file("games/game1.sav", SAVE, "someone else's"), 1);
    BW_CHECK_EQ("the save that would go there stays", is_file("slots/save5.sav", SAVE, "five"), 1);
    BW_CHECK_EQ("nothing else moved", is_file("slots/save2.sav", GARBAGE, "two"), 1);
    BW_CHECK_EQ("slot.txt as it was", text_is("slot.txt", "7\n"), 1);
    BW_CHECK_EQ("the plan stays for later", gone("tidy.txt"), 0);

    printf("a plan cut short is not followed\n");
    make_report_folder();
    put_text("tidy.txt", "move slots/save3.sav slots/save2.sav\n");     /* no "end" */
    BW_CHECK_EQ("refused", bw_saves_tidy(T_DIR, note, sizeof note), -1);
    BW_CHECK_EQ("slot 3 where it was", is_file("slots/save3.sav", SAVE, "three"), 1);
    BW_CHECK_EQ("slot 2 where it was", is_file("slots/save2.sav", GARBAGE, "two"), 1);

    printf("names that are taken stay taken\n");
    make_report_folder();
    put_file("unreadable/slot2.sav", GARBAGE, "set aside before");
    put_file("games/game1.sav", SAVE, "a game of games/");
    put_text("games/games.ini", "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                                "1 world arkhipelagos multiworld host ABC123\n");
    put_text("games.ini.moved", "an older one\n");
    BW_CHECK_EQ("moved", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    BW_CHECK_EQ("the old set-aside file stays", is_file("unreadable/slot2.sav", GARBAGE, "set aside before"), 1);
    BW_CHECK_EQ("slot 2's goes beside it", is_file("unreadable/slot2-2.sav", GARBAGE, "two"), 1);
    BW_CHECK_EQ("the game there stays", is_file("games/game1.sav", SAVE, "a game of games/"), 1);
    BW_CHECK_EQ("slot 5 is the next game", is_file("games/game2.sav", SAVE, "five"), 1);
    BW_CHECK_EQ("both lines", text_is("games/games.ini", "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                                                         "1 world arkhipelagos multiworld host ABC123\n"
                                                         "2 solo arkhipelagos solo host -\n"), 1);
    BW_CHECK_EQ("the older games.ini.moved stays", text_is("games.ini.moved", "an older one\n"), 1);
    BW_CHECK_EQ("this one beside it", text_is("games-2.ini.moved",
                                              "# the saves the Multiplayer screen made (sdk/save_games.h)\n"
                                              "5 solo arkhipelagos solo host -\n"), 1);

    printf("slot.txt on a file set aside: the newest save\n");
    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    put_file("pokeblack.sav", SAVE, "a");
    put_file("slots/save2.sav", ACTIVE, "b");
    put_file("slots/save3.sav", SAVE, "c");
    put_text("slot.txt", "2\n");
    BW_CHECK_EQ("moved", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    BW_CHECK_EQ("slot 3 closes up", is_file("slots/save2.sav", SAVE, "c"), 1);
    BW_CHECK_EQ("a save is in force", text_is("slot.txt", "1\n") || text_is("slot.txt", "2\n"), 1);

    printf("slot 1 missing: the next save takes its place\n");
    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    put_file("slots/save2.sav", SAVE, "x");
    put_file("slots/save9.sav", SAVE, "y");
    put_text("slot.txt", "9\n");
    BW_CHECK_EQ("moved", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    BW_CHECK_EQ("slot 1", is_file("pokeblack.sav", SAVE, "x"), 1);
    BW_CHECK_EQ("slot 2", is_file("slots/save2.sav", SAVE, "y"), 1);
    BW_CHECK_EQ("slot.txt", text_is("slot.txt", "2\n"), 1);

    printf("a folder in order is left alone\n");
    (void)bw_plat_remove_tree(T_DIR);
    (void)bw_plat_mkdirs(T_DIR);
    put_file("pokeblack.sav", SAVE, "x");
    put_file("slots/save2.sav", SAVE, "y");
    BW_CHECK_EQ("nothing to do", bw_saves_tidy(T_DIR, note, sizeof note), 0);
    BW_CHECK_EQ("no plan", gone("tidy.txt"), 1);
    BW_CHECK_EQ("no unreadable/", gone("unreadable"), 1);
    BW_CHECK_EQ("no slot.txt made", gone("slot.txt"), 1);
    BW_CHECK_EQ("an empty folder", bw_saves_tidy(T_DIR "/none", note, sizeof note), 0);

    printf("a move never writes over a file\n");
    put_file("a.sav", SAVE, "mine");
    put_file("b.sav", SAVE, "theirs");
    BW_CHECK_EQ("refused: the name is taken", bw_plat_move_new(T_DIR "/a.sav", T_DIR "/b.sav"), 1);
    BW_CHECK_EQ("the one moved stays", is_file("a.sav", SAVE, "mine"), 1);
    BW_CHECK_EQ("the one there stays", is_file("b.sav", SAVE, "theirs"), 1);
    BW_CHECK_EQ("to a free name", bw_plat_move_new(T_DIR "/a.sav", T_DIR "/c.sav"), 0);
    BW_CHECK_EQ("moved whole", is_file("c.sav", SAVE, "mine") && gone("a.sav"), 1);

#ifdef _WIN32
    /* (a POSIX record lock does not keep out its own process: there the
     * four-at-once case below is the test) */
    printf("a folder another holder works on is left alone\n");
    make_report_folder();
    {
        void *lock = bw_plat_lock_folder(T_DIR);

        BW_REQUIRE("the lock", lock != NULL);
        BW_CHECK_EQ("a second lock is refused", bw_plat_lock_folder(T_DIR) == NULL, 1);
        BW_CHECK_EQ("locked: nothing done", bw_saves_tidy(T_DIR, note, sizeof note), 0);
        BW_CHECK_EQ("slot 2 where it was", is_file("slots/save2.sav", GARBAGE, "two"), 1);
        BW_CHECK_EQ("no plan made", gone("tidy.txt"), 1);
        bw_plat_unlock_folder(lock);
    }
    BW_CHECK_EQ("free again: done", bw_saves_tidy(T_DIR, note, sizeof note), 1);
    check_report_folder_done("after the lock");
#endif

    printf("four processes at once on one folder\n");
    make_report_folder();
    {
        int k, done = 1;
#ifdef _WIN32
        intptr_t kids[4];
        const char *args[4] = { g_self, "--child", T_DIR, NULL };

        for (k = 0; k < 4; k++) {
            kids[k] = _spawnv(_P_NOWAIT, g_self, args);
        }
        for (k = 0; k < 4; k++) {
            int code = -1;

            done &= kids[k] != -1 && _cwait(&code, kids[k], 0) != -1 && code == 0;
        }
#else
        pid_t kids[4];

        for (k = 0; k < 4; k++) {
            kids[k] = fork();
            if (kids[k] == 0) {
                char n[400];

                _exit(bw_saves_tidy(T_DIR, n, sizeof n) < 0 ? 1 : 0);
            }
        }
        for (k = 0; k < 4; k++) {
            int st = 0;

            done &= kids[k] > 0 && waitpid(kids[k], &st, 0) == kids[k] && WIFEXITED(st) && WEXITSTATUS(st) == 0;
        }
#endif
        BW_CHECK_EQ("all four ran and none failed", done, 1);
        /* one of them did the work, or it is left for the next start */
        (void)bw_saves_tidy(T_DIR, note, sizeof note);
        check_report_folder_done("after four at once");
    }

    (void)bw_plat_remove_tree(T_DIR);
    BW_REPORT("test_save_tidy");
}
