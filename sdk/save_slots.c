/*
 * sdk/save_slots.c -- several single-player saves (save_slots.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"
#include "plat_save.h"
#include "save_slots.h"

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
extern int SAVE_CONTROL_Reload(void *saveControl) BW_SLOTS_WEAK;

/* The game's SaveControl (src/game/arm9/save_control.c): isNewGame at 0,
 * hasFlashSave at 1. */
#define SAVE_CONTROL_IS_NEW_GAME 0
#define SAVE_CONTROL_HAS_SAVE    1

static int g_current;

int bw_slots_path(int n, char *out, u32 size)
{
    const char *dir = bw_save_dir();
    int w;

    if (n < 1 || n > BW_SLOTS_MAX || dir == NULL || dir[0] == '\0') {
        return -1;
    }
    if (n == 1) {
        w = snprintf(out, size, "%s/pokeblack.sav", dir);
    } else {
        w = snprintf(out, size, "%s/slots/save%d.sav", dir, n);
    }
    return (w < 0 || (u32)w >= size) ? -1 : 0;
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
        if (fscanf(f, "%d", &n) != 1 || n < 1 || n > BW_SLOTS_MAX) {
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
static void apply(int live)
{
    void (*set)(const char *) = live ? bw_arm7_backup_switch_path
                                     : bw_arm7_backup_set_path;
    char path[512];
    char dir[512];

    if (set == NULL) {
        return;
    }
    if (bw_slots_current() == 1) {
        set(NULL);                          /* the port's own save file */
        return;
    }
    if (bw_slots_path(bw_slots_current(), path, sizeof(path)) == 0) {
        const char *sd = bw_save_dir();

        snprintf(dir, sizeof(dir), "%s/slots", sd != NULL ? sd : ".");
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
    }
    sc[SAVE_CONTROL_HAS_SAVE] = (unsigned char)(has_save ? 1 : 0);
    sc[SAVE_CONTROL_IS_NEW_GAME] = (unsigned char)(has_save ? 0 : 1);
    return has_save;
}

/* A slot the game could not load this run (a new game left before its
 * first save writes a file the game does not take as a save): it is not
 * offered, counted or stepped onto. */
static unsigned char g_bad[BW_SLOTS_MAX + 1];

static int offered(int n)
{
    return bw_slots_used(n) && !g_bad[n];
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

void bw_slots_settle(void)
{
    unsigned char *sc;
    int cur = bw_slots_current(), i;

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
    g_bad[cur] = 1;
    for (i = 1; i < BW_SLOTS_MAX; i++) {
        int n, best = 0;
        long best_t = -1;

        for (n = 1; n <= BW_SLOTS_MAX; n++) {
            long t;

            if (n == cur || !offered(n)) {
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
        g_bad[best] = 1;
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
    int cur = bw_slots_current();
    int i;

    for (i = 1; i < BW_SLOTS_MAX; i++) {
        int n = ((cur - 1 + d * i) % BW_SLOTS_MAX + BW_SLOTS_MAX) % BW_SLOTS_MAX + 1;

        if (offered(n)) {
            if (n == cur) {
                return 0;
            }
            set_current(n);
            apply(1);
            if (!reload(1)) {
                g_bad[n] = 1;       /* not a save after all: the next one */
                continue;
            }
            return 1;
        }
    }
    return 0;
}

int bw_slots_new_game(void)
{
    int n;

    if (!offered(bw_slots_current())) {
        return 0;                   /* the slot in force is free already */
    }
    for (n = 1; n <= BW_SLOTS_MAX; n++) {
        if (!offered(n)) {
            set_current(n);
            apply(1);
            reload(0);
            return 1;
        }
    }
    return 0;                       /* every slot is used: the game warns */
}

/* New Game with every slot used: the player chooses the save it replaces
 * on the Continue card. */
static int g_replace;

void bw_slots_label(char *out, u32 size)
{
    int used = 0, at = 0, n;

    for (n = 1; n <= BW_SLOTS_MAX; n++) {
        if (offered(n)) {
            used++;
            if (n == bw_slots_current()) {
                at = used;
            }
        }
    }
    if (used == 0 || at == 0) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, g_replace ? "Replace %d/%d?" : "< %d/%d >", at, used);
}

int bw_slots_replacing(void)
{
    return g_replace;
}

void bw_slots_set_replacing(int on)
{
    g_replace = on != 0;
}
