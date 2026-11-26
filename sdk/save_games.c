/*
 * sdk/save_games.c -- the saves a Multiplayer screen card made
 * (save_games.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"
#include "plat_save.h"
#include "save_slots.h"
#include "save_games.h"

static BwGame *g_games;         /* games/games.ini's records (slot -k) */
static int    g_count, g_cap;
static int   *g_old;            /* the slots an older eidos's games.ini names */
static int    g_old_count, g_old_cap;
static int    g_loaded;
static BwGame g_pending;
static int    g_has_pending;

static void games_file(char *out, size_t size, const char *name)
{
    const char *dir = bw_save_dir();

    snprintf(out, size, "%s/%s", dir != NULL ? dir : ".", name);
}

static void copy_word(char *out, size_t size, const char *s)
{
    size_t k = 0;

    for (; s != NULL && s[k] != '\0' && s[k] != ' ' && s[k] != '\n' && k + 1 < size; k++) {
        out[k] = s[k];
    }
    out[k] = '\0';
}

static int add_game(const BwGame *g)
{
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 16;
        BwGame *v = (BwGame *)realloc(g_games, (size_t)cap * sizeof *v);

        if (v == NULL) {
            return -1;
        }
        g_games = v;
        g_cap = cap;
    }
    g_games[g_count++] = *g;
    return g_count - 1;
}

static void add_old(int n)
{
    if (g_old_count == g_old_cap) {
        int cap = g_old_cap ? g_old_cap * 2 : 16;
        int *v = (int *)realloc(g_old, (size_t)cap * sizeof *v);

        if (v == NULL) {
            return;
        }
        g_old = v;
        g_old_cap = cap;
    }
    g_old[g_old_count++] = n;
}

/* One line of a record file into `g`: 1 read, 0 not a record. */
static int parse(const char *line, BwGame *g)
{
    char kind[8], role[8], ns[BW_GAMES_NS_MAX], mode[BW_GAMES_MODE_MAX], code[BW_GAMES_CODE_MAX];

    if (line[0] == '#' ||
        sscanf(line, "%d %7s %31s %23s %7s %16s", &g->slot, kind, ns, mode, role, code) != 6 || g->slot < 1) {
        return 0;
    }
    g->kind = strcmp(kind, "world") == 0 ? BW_GAME_WORLD : strcmp(kind, "room") == 0 ? BW_GAME_ROOM : BW_GAME_SOLO;
    g->role = strcmp(role, "joined") == 0 ? BW_GAME_JOINED : BW_GAME_HOST;
    copy_word(g->ns, sizeof g->ns, ns);
    copy_word(g->mode, sizeof g->mode, mode);
    copy_word(g->code, sizeof g->code, strcmp(code, "-") == 0 ? "" : code);
    return 1;
}

static void load(void)
{
    static char from[512];      /* the folder the list was read from */
    char path[512], line[160];
    FILE *f;

    /* The save folder can change within a run (a standalone game that goes
     * to the other version plays from that version's profile): the list is
     * the folder's, so it is read again from the folder in force, and the
     * old folder's list is never written into the new one. */
    games_file(path, sizeof path, "");
    if (g_loaded && strcmp(from, path) != 0) {
        g_loaded = 0;
        g_has_pending = 0;
    }
    if (g_loaded) {
        return;
    }
    g_loaded = 1;
    g_count = 0;
    g_old_count = 0;
    snprintf(from, sizeof from, "%s", path);
    games_file(path, sizeof path, "games/games.ini");
    if ((f = fopen(path, "rb")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            BwGame g;
            int i, dup = 0;

            if (!parse(line, &g)) {
                continue;
            }
            g.slot = -g.slot;
            for (i = 0; i < g_count; i++) {
                dup |= g_games[i].slot == g.slot;
            }
            if (!dup) {
                (void)add_game(&g);
            }
        }
        fclose(f);
    }
    /* an older eidos's record, until the tidy step moves its saves */
    games_file(path, sizeof path, "games.ini");
    if ((f = fopen(path, "rb")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            BwGame g;

            if (parse(line, &g)) {
                add_old(g.slot);
            }
        }
        fclose(f);
    }
}

static void store(void)
{
    static const char head[] = "# the saves the Multiplayer screen made (sdk/save_games.h)\n";
    char path[512];
    size_t cap = sizeof head + (size_t)g_count * 128, n;
    char *text = (char *)malloc(cap);
    int i;

    if (text == NULL) {
        return;
    }
    n = (size_t)snprintf(text, cap, "%s", head);
    for (i = 0; i < g_count && n < cap; i++) {
        const BwGame *g = &g_games[i];

        n += (size_t)snprintf(text + n, cap - n, "%d %s %s %s %s %s\n", -g->slot,
                              g->kind == BW_GAME_WORLD ? "world" : g->kind == BW_GAME_ROOM ? "room" : "solo", g->ns,
                              g->mode, g->role == BW_GAME_JOINED ? "joined" : "host",
                              g->code[0] != '\0' ? g->code : "-");
    }
    if (n < cap) {
        games_file(path, sizeof path, "games");
        (void)bw_plat_mkdirs(path);
        games_file(path, sizeof path, "games/games.ini");
        (void)bw_save_write_atomic(path, text, (u32)n);
    }
    free(text);
}

/* A word the file can hold: no space, at least one character. */
static void clean(char *s)
{
    char *p;

    for (p = s; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
            *p = '_';
        }
    }
    if (s[0] == '\0') {
        s[0] = '-';
        s[1] = '\0';
    }
}

void bw_games_pending(int kind, int role, const char *ns, const char *mode, const char *code)
{
    memset(&g_pending, 0, sizeof g_pending);
    g_pending.kind = kind;
    g_pending.role = role;
    copy_word(g_pending.ns, sizeof g_pending.ns, ns);
    copy_word(g_pending.mode, sizeof g_pending.mode, mode);
    copy_word(g_pending.code, sizeof g_pending.code, code != NULL ? code : "");
    clean(g_pending.ns);
    clean(g_pending.mode);
    g_has_pending = 1;
}

void bw_games_pending_clear(void)
{
    g_has_pending = 0;
}

void bw_games_forget(void)
{
    g_loaded = 0;                   /* the records read again at the next ask */
}

int bw_games_has_pending(void)
{
    return g_has_pending;
}

static int find(int n)
{
    int i;

    for (i = 0; i < g_count; i++) {
        if (g_games[i].slot == n) {
            return i;
        }
    }
    return -1;
}

void bw_games_saved(int n)
{
    int i;

    load();
    if (!g_has_pending) {
        return;                     /* a plain new game: a slot of the player's own */
    }
    g_pending.slot = n;
    i = find(n);
    if (i >= 0) {
        g_games[i] = g_pending;
    } else if (add_game(&g_pending) < 0) {
        return;
    }
    g_has_pending = 0;
    fprintf(stderr, "eidos: save: game %d holds a %s game (%s), listed under Your games\n", -n,
            g_pending.kind == BW_GAME_WORLD ? "world" : g_pending.kind == BW_GAME_ROOM ? "room" : "solo",
            g_pending.mode);
    store();
}

void bw_games_world_code(const char *code)
{
    int i;

    if (code == NULL || code[0] == '\0') {
        return;
    }
    if (g_has_pending) {
        copy_word(g_pending.code, sizeof g_pending.code, code);
        return;
    }
    load();
    i = find(bw_slots_current());
    if (i >= 0 && strcmp(g_games[i].code, code) != 0) {
        copy_word(g_games[i].code, sizeof g_games[i].code, code);
        store();
    }
}

int bw_games_hidden(int n)
{
    int i;

    if (n < 0) {
        return 1;
    }
    load();
    for (i = 0; i < g_old_count; i++) {
        if (g_old[i] == n) {
            return 1;
        }
    }
    return 0;
}

static long slot_time(int n)
{
    char path[512];
    struct stat st;

    return bw_slots_path(n, path, sizeof path) == 0 && stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

int bw_games_count(void)
{
    int i, n = 0;

    load();
    for (i = 0; i < g_count; i++) {
        n += bw_slots_used(g_games[i].slot) != 0;
    }
    return n;
}

int bw_games_at(int i, BwGame *out)
{
    int *order, n = 0, k, j, r = -1;

    load();
    if (g_count == 0 || (order = (int *)malloc((size_t)g_count * sizeof *order)) == NULL) {
        return -1;
    }
    for (k = 0; k < g_count; k++) {
        if (bw_slots_used(g_games[k].slot)) {
            order[n++] = k;
        }
    }
    /* newest first */
    for (k = 1; k < n; k++) {
        for (j = k; j > 0 && slot_time(g_games[order[j]].slot) > slot_time(g_games[order[j - 1]].slot); j--) {
            int t = order[j];

            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    }
    if (i >= 0 && i < n && out != NULL) {
        *out = g_games[order[i]];
        r = 0;
    }
    free(order);
    return r;
}
