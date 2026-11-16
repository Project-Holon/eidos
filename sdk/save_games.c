/*
 * sdk/save_games.c -- the saves a Multiplayer screen card made
 * (save_games.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"
#include "save_slots.h"
#include "save_games.h"

#define GAMES_MAX BW_SLOTS_MAX

static BwGame g_games[GAMES_MAX];
static int    g_count;
static int    g_loaded;
static BwGame g_pending;
static int    g_has_pending;

static void games_file(char *out, size_t size)
{
    const char *dir = bw_save_dir();

    snprintf(out, size, "%s/games.ini", dir != NULL ? dir : ".");
}

static void copy_word(char *out, size_t size, const char *s)
{
    size_t k = 0;

    for (; s != NULL && s[k] != '\0' && s[k] != ' ' && s[k] != '\n' && k + 1 < size; k++) {
        out[k] = s[k];
    }
    out[k] = '\0';
}

static void load(void)
{
    char path[512], line[160];
    FILE *f;

    if (g_loaded) {
        return;
    }
    g_loaded = 1;
    g_count = 0;
    games_file(path, sizeof path);
    if ((f = fopen(path, "rb")) == NULL) {
        return;
    }
    while (g_count < GAMES_MAX && fgets(line, sizeof line, f) != NULL) {
        BwGame g;
        char kind[8], role[8], ns[BW_GAMES_NS_MAX], mode[BW_GAMES_MODE_MAX], code[BW_GAMES_CODE_MAX];
        int i, dup = 0;

        if (line[0] == '#' ||
            sscanf(line, "%d %7s %31s %23s %7s %16s", &g.slot, kind, ns, mode, role, code) != 6 ||
            g.slot < 1 || g.slot > BW_SLOTS_MAX) {
            continue;
        }
        g.kind = strcmp(kind, "world") == 0 ? BW_GAME_WORLD : strcmp(kind, "room") == 0 ? BW_GAME_ROOM : BW_GAME_SOLO;
        g.role = strcmp(role, "joined") == 0 ? BW_GAME_JOINED : BW_GAME_HOST;
        copy_word(g.ns, sizeof g.ns, ns);
        copy_word(g.mode, sizeof g.mode, mode);
        copy_word(g.code, sizeof g.code, strcmp(code, "-") == 0 ? "" : code);
        for (i = 0; i < g_count; i++) {
            dup |= g_games[i].slot == g.slot;
        }
        if (!dup) {
            g_games[g_count++] = g;
        }
    }
    fclose(f);
}

static void store(void)
{
    char path[512], text[GAMES_MAX * 128 + 64];
    int i, n;

    n = snprintf(text, sizeof text, "# the saves the Multiplayer screen made (sdk/save_games.h)\n");
    for (i = 0; i < g_count && n > 0 && (size_t)n < sizeof text; i++) {
        const BwGame *g = &g_games[i];

        n += snprintf(text + n, sizeof text - (size_t)n, "%d %s %s %s %s %s\n", g->slot,
                      g->kind == BW_GAME_WORLD ? "world" : g->kind == BW_GAME_ROOM ? "room" : "solo", g->ns, g->mode,
                      g->role == BW_GAME_JOINED ? "joined" : "host", g->code[0] != '\0' ? g->code : "-");
    }
    if (n > 0 && (size_t)n < sizeof text) {
        games_file(path, sizeof path);
        (void)bw_save_write_atomic(path, text, (u32)n);
    }
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
    i = find(n);
    if (g_has_pending) {
        g_pending.slot = n;
        if (i < 0 && g_count < GAMES_MAX) {
            i = g_count++;
        }
        if (i >= 0) {
            g_games[i] = g_pending;
        }
        g_has_pending = 0;
        fprintf(stderr, "eidos: save: slot %d holds a %s game (%s), listed under Your games\n", n,
                g_pending.kind == BW_GAME_WORLD ? "world" : g_pending.kind == BW_GAME_ROOM ? "room" : "solo",
                g_pending.mode);
    } else if (i >= 0) {
        /* a plain new game took the slot */
        g_games[i] = g_games[--g_count];
    } else {
        return;
    }
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
    load();
    return find(n) >= 0;
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
    int order[GAMES_MAX], n = 0, k, j;

    load();
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
    if (i < 0 || i >= n || out == NULL) {
        return -1;
    }
    *out = g_games[order[i]];
    return 0;
}
