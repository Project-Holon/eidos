/*
 * sdk/save_slots.c -- several single-player saves (save_slots.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "save_slots.h"
#include "save_tidy.h"

#if defined(__GNUC__) || defined(__clang__)
#define BW_SLOTS_WEAK __attribute__((weak))
#else
#define BW_SLOTS_WEAK
#endif

/* The backup chip and the game's save control (weak: a test links
 * without the game). */
extern void bw_arm7_backup_set_path(const char *path) BW_SLOTS_WEAK;
extern void bw_arm7_backup_switch_path(const char *path) BW_SLOTS_WEAK;
extern void *SAVE_CONTROL_Get(void) BW_SLOTS_WEAK;
/* Where the game keeps its save control pointer (_pSaveControl):
 * SAVE_CONTROL_Get halts the game when there is none (between a soft reset
 * and the next boot). */
extern void *bw_dsref__pSaveControl BW_SLOTS_WEAK;
extern int SAVE_CONTROL_Reload(void *saveControl) BW_SLOTS_WEAK;
extern int SYSTEM_DATA_IsActive(void *saveControl) BW_SLOTS_WEAK;
extern void bw_arm7_backup_hold(int on) BW_SLOTS_WEAK;
extern void bw_arm7_backup_set_memory(const unsigned char *data, u32 size, int live) BW_SLOTS_WEAK;
/* The preview's save, packed (sdk/preview_save.c: in this file's own source
 * list; plain names, a weak one would not bring its object into the
 * program), and its unpacker. */
extern const unsigned char bw_preview_save_z[];
extern const unsigned int bw_preview_save_z_size;
extern const unsigned int bw_preview_save_size;
extern const unsigned int bw_preview_save_crc;
extern int bw_inflate(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_len,
                      size_t *src_used) BW_SLOTS_WEAK;
extern u32 bw_crc32(u32 crc, const unsigned char *data, size_t len) BW_SLOTS_WEAK;
extern u32  bw_arm7_backup_writes(void) BW_SLOTS_WEAK;
extern int  bw_arm7_backup_dirty(void) BW_SLOTS_WEAK;
/* sdk/save_games.c: the saves the Multiplayer screen made (weak: a test
 * links without it) */
extern int  bw_games_hidden(int n) BW_SLOTS_WEAK;
extern void bw_games_saved(int n) BW_SLOTS_WEAK;
extern void bw_games_pending_clear(void) BW_SLOTS_WEAK;
extern int  bw_games_has_pending(void) BW_SLOTS_WEAK;
/* sdk/mod_title.c: the title goes straight on with the save in force */
extern int  bw_mod_title_autostart_pending(void) BW_SLOTS_WEAK;
/* sdk/mod_savedata.c: the save the per-save mod files belong to */
extern void bw_mod_savedata_set_save(const char *name) BW_SLOTS_WEAK;

/* The game's SaveControl (src/game/arm9/save_control.c): isNewGame at 0,
 * hasFlashSave at 1. */
#define SAVE_CONTROL_IS_NEW_GAME 0
#define SAVE_CONTROL_HAS_SAVE    1

static int g_current;

int bw_slots_path(int n, char *out, u32 size)
{
    const char *dir = bw_save_dir();
    int w;

    if (n == 0 || dir == NULL || dir[0] == '\0') {
        return -1;
    }
    if (n == 1) {
        w = snprintf(out, size, "%s/pokeblack.sav", dir);
    } else if (n > 1) {
        w = snprintf(out, size, "%s/slots/save%d.sav", dir, n);
    } else {
        w = snprintf(out, size, "%s/games/game%d.sav", dir, -n);
    }
    return (w < 0 || (u32)w >= size) ? -1 : 0;
}

/* The numbers n of the files "<prefix><n><suffix>" of a folder of the save
 * folder: the highest (0 when there is none). */
typedef struct SlotScan {
    const char *prefix, *suffix;
    int high;
} SlotScan;

static int scan_one(void *ctx, const char *name)
{
    SlotScan *s = (SlotScan *)ctx;
    size_t lp = strlen(s->prefix), ls = strlen(s->suffix), ln = strlen(name);
    const char *p;
    long v = 0;

    if (ln <= lp + ls || strncmp(name, s->prefix, lp) != 0 || strcmp(name + ln - ls, s->suffix) != 0
        || name[lp] == '0') {
        return 0;
    }
    for (p = name + lp; p < name + ln - ls; p++) {
        if (*p < '0' || *p > '9' || v > 100000000L) {
            return 0;
        }
        v = v * 10 + (*p - '0');
    }
    if (v > s->high) {
        s->high = (int)v;
    }
    return 0;
}

static int highest_in(const char *sub, const char *prefix, const char *suffix)
{
    const char *dir = bw_save_dir();
    char path[512];
    SlotScan s;

    s.prefix = prefix;
    s.suffix = suffix;
    s.high = 0;
    if (dir != NULL && dir[0] != '\0' && snprintf(path, sizeof path, "%s/%s", dir, sub) < (int)sizeof path
        && bw_plat_path_kind(path) == BW_PATH_DIR) {
        (void)bw_plat_list_dir(path, scan_one, &s);
    }
    return s.high;
}

/* The highest slot of the player's own that holds a file (0: none).  There
 * is no limit: the loops below go to it, and New Game takes the next. */
static int last_slot(void)
{
    int high = highest_in("slots", "save", ".sav");

    return high >= 2 ? high : (bw_slots_used(1) ? 1 : 0);
}

/* The lowest slot of the player's own with no file: a gap (which the tidy
 * step closes at the start), or the one after the last. */
static int first_free(void)
{
    int n, last = last_slot();

    for (n = 1; n <= last; n++) {
        if (!bw_slots_used(n)) {
            return n;
        }
    }
    return last + 1;
}

/* A file for a Multiplayer screen game: the next game of games/ (-k). */
static int next_game(void)
{
    return -(highest_in("games", "game", ".sav") + 1);
}

int bw_slots_used(int n)
{
    char path[512];
    FILE *f;
    long size = 0;

    if (bw_slots_path(n, path, sizeof(path)) != 0 ||
        (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
    }
    fclose(f);
    return size > 0;
}

static void current_file(char *out, u32 size)
{
    const char *dir = bw_save_dir();

    snprintf(out, size, "%s/slot.txt", dir != NULL ? dir : ".");
}

int bw_slots_current(void)
{
    char path[512];
    FILE *f;
    int n = 1;

    if (g_current != 0) {
        return g_current;
    }
    current_file(path, sizeof(path));
    if ((f = fopen(path, "rb")) != NULL) {
        /* a slot of the player's own (1 and up) or a game of games/ (-k) */
        if (fscanf(f, "%d", &n) != 1 || n == 0) {
            n = 1;
        }
        fclose(f);
    }
    g_current = n;
    return n;
}

static void set_current(int n)
{
    char path[512];
    char text[8];
    int w;

    g_current = n;
    current_file(path, sizeof(path));
    w = snprintf(text, sizeof(text), "%d\n", n);
    (void)bw_save_write_atomic(path, text, (u32)w);
}

/* The slot in force as the chip's file: at start-up (the next identify
 * reads it), or live (the chip keeps its type and reads the file again). */
/* The per-save layer's name for the save in force (save_tidy.h): its
 * mods' files beside it are its own. */
static void tell_state_layer(void)
{
    char rel[64], name[64];
    int n = bw_slots_current();

    if (bw_mod_savedata_set_save == NULL) {
        return;
    }
    if (n == 1) {
        snprintf(rel, sizeof rel, "pokeblack.sav");
    } else if (n > 1) {
        snprintf(rel, sizeof rel, "slots/save%d.sav", n);
    } else {
        snprintf(rel, sizeof rel, "games/game%d.sav", -n);
    }
    if (bw_save_state_name(rel, name, sizeof name) == 0) {
        bw_mod_savedata_set_save(name);
    }
}

static void apply(int live)
{
    void (*set)(const char *) = live ? bw_arm7_backup_switch_path
                                     : bw_arm7_backup_set_path;
    char path[512];
    char dir[512];

    tell_state_layer();
    if (set == NULL) {
        return;
    }
    if (bw_slots_current() == 1) {
        set(NULL);                          /* the port's own save file */
        return;
    }
    if (bw_slots_path(bw_slots_current(), path, sizeof(path)) == 0) {
        const char *sd = bw_save_dir();

        snprintf(dir, sizeof(dir), "%s/%s", sd != NULL ? sd : ".", bw_slots_current() < 0 ? "games" : "slots");
        (void)bw_plat_mkdirs(dir);
        set(path);
    }
}

void bw_slots_apply(void)
{
    apply(0);
}

/* The game reads its save again from the slot in force.  Returns whether
 * it found one it can load. */
static int reload(int has_save)
{
    unsigned char *sc;

    if (SAVE_CONTROL_Get == NULL) {
        return has_save;
    }
    sc = (unsigned char *)SAVE_CONTROL_Get();
    if (sc == NULL) {
        fprintf(stderr, "eidos: save: the game has no save control yet; "
                "the save is read at its start-up\n");
        return has_save;
    }
    if (has_save && SAVE_CONTROL_Reload != NULL) {
        int r = SAVE_CONTROL_Reload(sc);

        /* 1 and 2: loaded (clean or recovered) */
        has_save = (r == 1 || r == 2);
        fprintf(stderr, "eidos: save: read again: %s (answer %d)\n",
                has_save ? "loaded" : "not loaded", r);
        /* The game's own start-up load (SAVE_CONTROL_Init) takes a save
         * whose system data is still marked active for none; its reload
         * does not ask, and the game then played that file as blank data
         * (zone 0, a panic in the field).  The same rule here. */
        if (has_save && SYSTEM_DATA_IsActive != NULL && SYSTEM_DATA_IsActive(sc) == 1) {
            has_save = 0;
            fprintf(stderr, "eidos: save: that save is marked as still being played; "
                    "the game takes it for none\n");
        }
    }
    sc[SAVE_CONTROL_HAS_SAVE] = (unsigned char)(has_save ? 1 : 0);
    sc[SAVE_CONTROL_IS_NEW_GAME] = (unsigned char)(has_save ? 0 : 1);
    return has_save;
}

/* A slot the game could not load this run (a new game left before its
 * first save writes a file the game does not take as a save): it is not
 * offered, counted or stepped onto.  The verdicts hold for the file as it
 * was read: its time stamp then (seen: read by the game's loader at that
 * stamp, good or bad).  One entry a slot of the player's own, grown as
 * slots come. */
typedef struct Verdict {
    unsigned char bad;
    long seen;
} Verdict;
static Verdict *g_verdict;
static int g_verdicts;
static Verdict g_no_verdict;    /* (a game of games/, or no room: no verdict kept) */

static Verdict *verdict(int n)
{
    if (n < 1) {
        memset(&g_no_verdict, 0, sizeof g_no_verdict);
        return &g_no_verdict;
    }
    if (n >= g_verdicts) {
        int cap = n + 16;
        Verdict *v = (Verdict *)realloc(g_verdict, (size_t)cap * sizeof *v);

        if (v == NULL) {
            memset(&g_no_verdict, 0, sizeof g_no_verdict);
            return &g_no_verdict;
        }
        memset(v + g_verdicts, 0, (size_t)(cap - g_verdicts) * sizeof *v);
        g_verdict = v;
        g_verdicts = cap;
    }
    return &g_verdict[n];
}

/* When the slot's file was last written (0 when it cannot be read). */
static long slot_time(int n)
{
    char path[512];
    struct stat st;

    if (bw_slots_path(n, path, sizeof(path)) != 0 || stat(path, &st) != 0) {
        return 0;
    }
    return (long)st.st_mtime;
}

static void mark_bad(int n)
{
    Verdict *v = verdict(n);

    v->bad = 1;
    v->seen = slot_time(n);
}

void bw_slots_folder_changed(void)
{
    if (g_verdict != NULL) {
        memset(g_verdict, 0, (size_t)g_verdicts * sizeof *g_verdict);
    }
}

void bw_slots_forget(void)
{
    bw_slots_folder_changed();
    g_current = 0;                  /* slot.txt read again */
}

static int offered(int n)
{
    Verdict *v = verdict(n);

    if (v->bad && v->seen != slot_time(n)) {
        v->bad = 0;                 /* written since: to be read again */
        v->seen = 0;
    }
    return bw_slots_used(n) && !v->bad;
}

/* A save a Multiplayer screen card made: the Continue card leaves it to
 * the Multiplayer screen's Your games (sdk/save_games.h). */
static int hidden(int n)
{
    return bw_games_hidden != NULL && bw_games_hidden(n);
}

int bw_slots_current_hidden(void)
{
    return g_current != 0 && hidden(g_current);
}

/* What the Continue card offers: a save that loads and is the player's own. */
static int shown(int n)
{
    return offered(n) && !hidden(n);
}

static int g_pending;       /* a new game not saved yet (below) */

/* Every slot's file not judged yet is read by the game's loader, so the
 * Continue card counts and offers only saves that load (it counted every
 * file, and a file that is no save dropped out only when the player
 * stepped onto it).  The slot in force is read back afterwards. */
static void judge_all(void)
{
    unsigned char *sc;
    int cur = bw_slots_current(), n, had, moved = 0, last = last_slot();

    if (g_pending != 0 || SAVE_CONTROL_Get == NULL || SAVE_CONTROL_Reload == NULL
        || (sc = (unsigned char *)SAVE_CONTROL_Get()) == NULL) {
        return;
    }
    had = sc[SAVE_CONTROL_HAS_SAVE];
    for (n = 1; n <= last; n++) {
        Verdict *v;
        long t;

        if (n == cur || !bw_slots_used(n)) {
            continue;
        }
        t = slot_time(n);
        v = verdict(n);
        if (v->seen == t && t != 0) {
            continue;               /* judged as it is now */
        }
        g_current = n;              /* (slot.txt is not written for a look) */
        apply(1);
        v = verdict(n);
        v->bad = reload(1) ? 0 : 1;
        v->seen = t;
        moved = 1;
        if (v->bad) {
            fprintf(stderr, "eidos: save: slot %d holds a file that is no save; it is not offered\n", n);
        }
    }
    if (moved) {
        g_current = cur;
        apply(1);
        (void)reload(had);
    }
}

static int g_try;               /* a session on another save, in memory (bw_slots_try_begin): 1 a file's, 2 the built-in */
static char g_try_path[600];
static unsigned char *g_try_mem;    /* the built-in save, unpacked */

int bw_slots_try_embedded(void)
{
    return bw_preview_save_z_size != 0u && bw_inflate != NULL && bw_arm7_backup_set_memory != NULL;
}

/* The built-in save in memory: unpacked once, checked against its sum. */
static int try_unpack(void)
{
    if (g_try_mem != NULL) {
        return 1;
    }
    if (!bw_slots_try_embedded()) {
        return 0;
    }
    g_try_mem = (unsigned char *)malloc(bw_preview_save_size);
    if (g_try_mem == NULL) {
        return 0;
    }
    if (bw_inflate(bw_preview_save_z, bw_preview_save_z_size, g_try_mem, bw_preview_save_size, NULL) != 0
        || (bw_crc32 != NULL && bw_crc32(0, g_try_mem, bw_preview_save_size) != bw_preview_save_crc)) {
        fprintf(stderr, "eidos: save: the built-in preview save does not unpack\n");
        free(g_try_mem);
        g_try_mem = NULL;
        return 0;
    }
    return 1;
}

int bw_slots_try_begin(const char *path)
{
    FILE *f;
    int have = 0;

    if (path == NULL) {
        /* the built-in save: from memory, no file at all */
        if (!try_unpack()) {
            return 0;
        }
        bw_arm7_backup_set_memory(g_try_mem, bw_preview_save_size, 1);
        g_try = 2;
        if (!reload(1)) {
            bw_slots_try_end();
            return 0;
        }
        fprintf(stderr, "eidos: save: a session on the built-in preview save, in memory only\n");
        return 1;
    }
    if (strlen(path) >= sizeof(g_try_path) || bw_arm7_backup_switch_path == NULL
        || bw_arm7_backup_set_path == NULL || bw_arm7_backup_hold == NULL || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    have = fseek(f, 0, SEEK_END) == 0 && ftell(f) > 0;
    fclose(f);
    if (!have) {
        return 0;
    }
    bw_arm7_backup_switch_path(path);
    bw_arm7_backup_hold(1);         /* from here nothing reaches a file */
    g_try = 1;
    strcpy(g_try_path, path);
    if (!reload(1)) {
        bw_slots_try_end();
        return 0;
    }
    fprintf(stderr, "eidos: save: a session on %s, kept in memory\n", path);
    return 1;
}

int bw_slots_try_active(void)
{
    return g_try;
}

/* The run's start-up while the session lasts: the chip's file is the
 * session's (read at the next identify), held again (a reset lets go). */
int bw_slots_try_boot(void)
{
    if (!g_try) {
        return 0;
    }
    if (g_try == 2) {
        bw_arm7_backup_set_memory(g_try_mem, bw_preview_save_size, 0);
        return 1;
    }
    bw_arm7_backup_set_path(g_try_path);
    bw_arm7_backup_hold(1);
    return 1;
}

int bw_slots_try_reload(void)
{
    return g_try ? reload(1) : 0;
}

void bw_slots_try_leave(void)
{
    if (g_try) {
        g_try = 0;
        fprintf(stderr, "eidos: save: the session in memory is over (a restart follows)\n");
    }
}

void bw_slots_try_end(void)
{
    if (!g_try) {
        return;
    }
    g_try = 0;
    apply(1);                       /* the slot in force again: a path change drops what was held */
    (void)reload(bw_slots_used(bw_slots_current()));
    fprintf(stderr, "eidos: save: the session in memory is over; slot %d is the game's again\n", bw_slots_current());
}

void bw_slots_settle(void)
{
    unsigned char *sc;
    int cur = bw_slots_current(), i;

    bw_slots_try_end();             /* back at the title menu: the player's own save */
    judge_all();
    if (hidden(cur) && bw_slots_used(cur) && g_pending == 0 &&
        !(bw_mod_title_autostart_pending != NULL && bw_mod_title_autostart_pending())) {
        /* (a Your games entry the title is about to go on with stays) */
        /* the slot in force is a Multiplayer screen game (played last): the
         * Continue card shows the player's own newest save, or none */
        int n, best = 0, last = last_slot();
        long best_t = -1;

        for (n = 1; n <= last; n++) {
            if (n != cur && shown(n) && slot_time(n) > best_t) {
                best = n;
                best_t = slot_time(n);
            }
        }
        if (best == 0) {
            best = first_free();    /* no save of its own: a free slot, New Game alone */
        }
        {
            set_current(best);
            apply(1);
            if (!reload(bw_slots_used(best))) {
                mark_bad(best);
            }
            fprintf(stderr, "eidos: save: slot %d is a Multiplayer screen game; the title shows slot %d\n",
                    cur, best);
            return;
        }
    }
    if (SAVE_CONTROL_Get == NULL || (sc = (unsigned char *)SAVE_CONTROL_Get()) == NULL
        || sc[SAVE_CONTROL_HAS_SAVE]) {
        return;
    }
    if (!bw_slots_used(cur)) {
        return;                     /* a free slot: a new game, as it should */
    }
    /* the slot in force holds a file but no save: the save played last
     * (the newest file that loads), so a new game left before it was saved
     * does not hide the game before it */
    mark_bad(cur);
    for (i = 1; i <= last_slot(); i++) {
        int n, best = 0, last = last_slot();
        long best_t = -1;

        for (n = 1; n <= last; n++) {
            long t;

            if (n == cur || !shown(n)) {
                continue;
            }
            t = slot_time(n);
            if (t > best_t) {
                best = n;
                best_t = t;
            }
        }
        if (best == 0) {
            break;
        }
        set_current(best);
        apply(1);
        if (reload(1)) {
            fprintf(stderr, "eidos: save: slot %d has no save; slot %d is shown\n", cur, best);
            return;
        }
        mark_bad(best);
    }
    set_current(cur);               /* none loads: as it was */
    apply(1);
    (void)reload(0);
}

int bw_slots_load_file(const char *path)
{
    FILE *f;
    int have = 0;

    if (path != NULL && (f = fopen(path, "rb")) != NULL) {
        have = fseek(f, 0, SEEK_END) == 0 && ftell(f) > 0;
        fclose(f);
    }
    if (bw_arm7_backup_switch_path != NULL) {
        bw_arm7_backup_switch_path(path);
    }
    reload(have);
    return have;
}

int bw_slots_flip(int d)
{
    int cur = bw_slots_current(), last = last_slot();
    int i;

    if (last < 1) {
        return 0;
    }
    if (cur < 1) {
        cur = d > 0 ? last : 1;     /* (a game of games/: the player's own from the end) */
    }
    for (i = 1; i <= last; i++) {
        int n = ((cur - 1 + d * i) % last + last) % last + 1;

        if (shown(n)) {
            if (n == cur) {
                return 0;
            }
            set_current(n);
            apply(1);
            if (!reload(1)) {
                mark_bad(n);        /* not a save after all: the next one */
                continue;
            }
            return 1;
        }
    }
    return 0;
}

/* A new game not saved yet: its slot (0: none) and the slot before it.
 * Until the game's first completed save the chip holds its writes in
 * memory, so New Game makes no file and slot.txt keeps the save before
 * (bw_slots_frame commits, bw_slots_dropped goes back). */
static int g_prev;
static int g_released;          /* the hold is off; waiting for the file   */
static u32 g_writes_before;

static void hold_new_game(int n, int prev)
{
    g_pending = n;
    g_prev = prev;
    if (bw_arm7_backup_hold != NULL) {
        bw_arm7_backup_hold(1);
    }
}

int bw_slots_new_game(void)
{
    int n, cur = bw_slots_current();
    int game = bw_games_has_pending != NULL && bw_games_has_pending();

    /* Free means no file: a file the game would not load (marked active,
     * a new game left unsaved) is kept, for recovery, and never written
     * over by a new game.  A Multiplayer screen card's game takes a file of
     * games/, a plain one a slot of the player's own; there is always a
     * free one. */
    if (!bw_slots_used(cur) && (game ? cur < 0 : cur > 0)) {
        hold_new_game(cur, cur);
        return 0;                   /* the slot in force is free already */
    }
    n = game ? next_game() : first_free();
    g_current = n;                  /* slot.txt waits for the first save */
    apply(1);
    reload(0);
    hold_new_game(n, cur);
    return 1;
}

void bw_slots_frame(void)
{
    unsigned char *sc;

    if (g_pending == 0) {
        return;
    }
    sc = &bw_dsref__pSaveControl != NULL && bw_dsref__pSaveControl != NULL
         ? *(unsigned char **)bw_dsref__pSaveControl : NULL;
    if (!g_released && sc != NULL && sc[SAVE_CONTROL_HAS_SAVE] && !sc[SAVE_CONTROL_IS_NEW_GAME]) {
        /* the first completed save: the held writes go to the slot's file */
        g_released = 1;
        g_writes_before = bw_arm7_backup_writes != NULL ? bw_arm7_backup_writes() : 0;
        if (bw_arm7_backup_hold != NULL) {
            bw_arm7_backup_hold(0);
        }
    }
    /* slot.txt names the slot only once its file is on the disk: while the
     * write keeps failing the slot in force stays as it was */
    if (g_released && bw_arm7_backup_writes != NULL && bw_arm7_backup_writes() != g_writes_before
        && (bw_arm7_backup_dirty == NULL || !bw_arm7_backup_dirty())) {
        set_current(g_pending);
        g_pending = 0;
        g_released = 0;
        fprintf(stderr, "eidos: save: the new game is saved in slot %d\n", g_current);
        if (bw_games_saved != NULL) {
            bw_games_saved(g_current);  /* a Multiplayer screen game, or none any more */
        }
    }
}

int bw_slots_continue(int n)
{
    if (n == 0 || !offered(n) || g_pending != 0) {
        return 0;
    }
    if (n != bw_slots_current()) {
        set_current(n);
        apply(1);
    }
    if (!reload(1)) {
        mark_bad(n);
        return 0;
    }
    return 1;
}

void bw_slots_dropped(void)
{
    if (g_pending != 0 && g_released && bw_arm7_backup_writes != NULL
        && bw_arm7_backup_writes() != g_writes_before) {
        set_current(g_pending);     /* its file reached the disk after all */
        g_pending = 0;
        g_released = 0;
        if (bw_games_saved != NULL) {
            bw_games_saved(g_current);
        }
        return;
    }
    if (g_pending != 0) {
        if (bw_games_pending_clear != NULL) {
            bw_games_pending_clear();   /* that game was never saved */
        }
        g_current = g_prev;         /* the save in force before New Game */
        g_pending = 0;
        g_released = 0;
        apply(0);
    }
}

int bw_slots_count(void)
{
    int used = 0, n, last = last_slot();

    for (n = 1; n <= last; n++) {
        used += shown(n) ? 1 : 0;
    }
    return used;
}

void bw_slots_label(char *out, u32 size)
{
    int used = 0, at = 0, n, last = last_slot(), cur = bw_slots_current();

    for (n = 1; n <= last; n++) {
        if (shown(n)) {
            used++;
            if (n == cur) {
                at = used;
            }
        }
    }
    if (used == 0 || at == 0) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, "< %d/%d >", at, used);
}
