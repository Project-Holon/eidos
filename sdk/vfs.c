/*
 * sdk/vfs.c -- the file layer: the data root, the mods laid over it in a
 * load order, and the resolution of a NitroFS-relative path to a host path.
 *
 * This is the first piece of the port's data pipeline.  It owns the layers:
 *
 *     data root   the directory tools/ndsrom.py's `extract` wrote, the one
 *                 holding `a/0/0/2` (the player's own extracted ROM)
 *     mod roots   zero or more overlays, laid at the same relative layout,
 *                 in a load order where a later mod wins (sdk/vfs.h)
 *
 * At file granularity a path resolves to the latest layer holding a regular
 * file there, so a mod ships whole replacement files and an absent file
 * falls through.  The lookup happens here and nowhere else; every loader
 * that opens a NitroFS path calls bw_data_path (or the FS shim, which calls
 * it), so there is one resolution.
 *
 * Member granularity is sdk/vfs_narc.c: a mod *directory* at an archive's
 * path supplies single members, and the FS shim reads the merged archive it
 * builds.  This file only keeps the list it walks.  No game data is in this
 * file and nothing here interprets a byte of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "vfs.h"
#include "plat_dir.h"
#include "plat_thread.h"

static void layer_cache_drop(void);     /* the kept bw_vfs_file_layer answers, below */
#include "mod_bw.h"

/* ------------------------------------------------------------------ */
/* Data root                                                           */
/* ------------------------------------------------------------------ */

static char g_data_root[1024] = "extract/data";
static int  g_data_root_set;
static u32  g_data_generation;      /* bw_data_generation */

/* Copy `path` (length n) to dst, dropping trailing separators. */
static void copy_root(char *dst, size_t cap, const char *path, size_t n)
{
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, path, n);
    dst[n] = '\0';
    while (n > 0 && (dst[n - 1] == '/' || dst[n - 1] == '\\')) {
        dst[--n] = '\0';
    }
}

void bw_set_data_root(const char *path)
{
    /* There is no sane empty data root, so an empty path is a no-op. */
    if (path == NULL || *path == '\0') {
        return;
    }
    copy_root(g_data_root, sizeof(g_data_root), path, strlen(path));
    g_data_root_set = 1;
    g_data_generation++;
    bw_vfs_flush();
    layer_cache_drop();
}

u32 bw_data_generation(void)
{
    return g_data_generation;
}

const char *bw_data_root(void)
{
    if (!g_data_root_set) {
        const char *env = getenv("BW_DATA_ROOT");

        g_data_root_set = 1;
        if (env != NULL && *env != '\0') {
            bw_set_data_root(env);
        }
    }
    return g_data_root;
}

/* ------------------------------------------------------------------ */
/* The mod list                                                        */
/* ------------------------------------------------------------------ */

#define BW_MOD_MANIFEST "eidos_mod.txt"

typedef struct BwMod {
    char *root;
    char *name;
    int   order;
    int   games;    /* BW_MOD_GAME_* bits: the games its manifest says it works on (0: its `game` line names none) */
    char  eidos[16];/* its `eidos` line: the oldest eidos it works on ("0.2.8"), "" for none */
} BwMod;

/* This build's version, the release it is or will be ("0.2.8": CMake's
 * project version, without the commit a test build adds). */
#ifndef EIDOS_RELEASE_VERSION
#define EIDOS_RELEASE_VERSION "0.0.0"
#endif

/* "X.Y.Z" as three numbers: 0, or -1 for anything else. */
static int version_parts(const char *s, unsigned long v[3])
{
    int k;
    char *end;

    for (k = 0; k < 3; k++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v[k] = strtoul(s, &end, 10);
        s = end;
        if (k < 2 && *s++ != '.') {
            return -1;
        }
    }
    return *s == '\0' ? 0 : -1;
}

/* Whether a mod's `eidos` value asks for a newer eidos than this one: 1, or
 * 0 (an empty value, or one that is not X.Y.Z, asks for nothing). */
int bw_mod_eidos_too_old(const char *need)
{
    unsigned long want[3], have[3];
    int k;

    if (need == NULL || need[0] == '\0' || version_parts(need, want) != 0 ||
        version_parts(EIDOS_RELEASE_VERSION, have) != 0) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        if (want[k] != have[k]) {
            return want[k] > have[k];
        }
    }
    return 0;
}

const char *bw_mod_eidos_version(void)
{
    return EIDOS_RELEASE_VERSION;
}

static BwMod *g_mods;
static u32    g_mod_count;
static u32    g_mod_cap;
static int    g_mods_set;

/*
 * The player's own order is in force: the list is the one the player
 * chose and the manifest's `order` key does not decide it.  A root added
 * while it holds goes at the end of the list instead of being sorted in
 * (bw_mod_set_order).  It is dropped with the list (bw_clear_mods) and
 * with the port's own list (bw_set_mod_root).
 */
static int g_player_order;

static char *dup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);

    if (d != NULL) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

static void free_mod(BwMod *m)
{
    free(m->root);
    free(m->name);
    m->root = NULL;
    m->name = NULL;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim [*s, *e) of surrounding blanks. */
static void trim(const char **s, const char **e)
{
    while (*s < *e && is_space(**s)) {
        (*s)++;
    }
    while (*e > *s && is_space((*e)[-1])) {
        (*e)--;
    }
}

/* ------------------------------------------------------------------ */
/* The game a mod is made for                                           */
/* ------------------------------------------------------------------ */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/*
 * A manifest's `game` value: `black` or `white` (any case), the mod works on
 * that game only, in every language.  A game code is read as its game, by
 * its third letter alone (IRBO, IRBF: Black; IRAO, IRAD: White): a mod made
 * with an English cartridge works on a French one.  An empty value: both.
 * 0 for anything else.
 */
int bw_mod_games_of(const char *value, size_t n)
{
    static const char black[] = "black", white[] = "white";
    size_t i;
    int b = n == 5, w = n == 5;

    if (value == NULL || n == 0) {
        return BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
    }
    for (i = 0; i < n && n == 5; i++) {
        b = b && lower((unsigned char)value[i]) == black[i];
        w = w && lower((unsigned char)value[i]) == white[i];
    }
    if (b || w) {
        return b ? BW_MOD_GAME_BLACK : BW_MOD_GAME_WHITE;
    }
    if (n == 4 && lower((unsigned char)value[0]) == 'i' && lower((unsigned char)value[1]) == 'r') {
        const int v = lower((unsigned char)value[2]);

        return v == 'b' ? BW_MOD_GAME_BLACK : v == 'a' ? BW_MOD_GAME_WHITE : 0;
    }
    return 0;
}

/*
 * The game the data root holds: the cartridge's code, at 0x0C of the
 * header.bin beside the data folder (sdk/langmap.c reads the same four
 * letters); its third letter is A on White.  The data root's own file, never
 * a mod's.  0 when it cannot be read: nothing is refused then.
 */
int bw_mod_game_loaded(void)
{
    char path[1100];
    char code[4];
    size_t n = strlen(g_data_root);
    FILE *fp;
    int ok;

    if (n == 0 || n + sizeof("header.bin") + 1 > sizeof(path)) {
        return 0;
    }
    memcpy(path, g_data_root, n + 1);
    while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
        n--;
    }
    strcpy(path + n, "header.bin");
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    ok = fseek(fp, 0x0CL, SEEK_SET) == 0 && fread(code, 1, 4, fp) == 4;
    fclose(fp);
    if (!ok) {
        return 0;
    }
    return code[2] == 'A' ? BW_MOD_GAME_WHITE : BW_MOD_GAME_BLACK;
}

static void read_manifest(BwMod *m);

/* The games the mod in the folder `root` says it works on (its manifest's
 * `game` line; both when it has none, or no manifest). */
int bw_mod_games_at(const char *root)
{
    BwMod m;

    if (root == NULL || root[0] == '\0') {
        return BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
    }
    m.root = (char *)root;
    m.name = NULL;
    m.order = 0;
    m.games = BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
    m.eidos[0] = '\0';
    read_manifest(&m);
    free(m.name);
    return m.games;
}

/* The oldest eidos the mod in the folder `root` says it works on (its
 * manifest's `eidos` line) when that is newer than this one: 1 with the
 * version in `out`, else 0. */
int bw_mod_needs_eidos_at(const char *root, char *out, size_t size)
{
    BwMod m;
    int too_old;

    if (root == NULL || root[0] == '\0') {
        return 0;
    }
    m.root = (char *)root;
    m.name = NULL;
    m.order = 0;
    m.games = BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
    m.eidos[0] = '\0';
    read_manifest(&m);
    free(m.name);
    too_old = bw_mod_eidos_too_old(m.eidos);
    if (too_old && out != NULL && size > 0) {
        snprintf(out, size, "%s", m.eidos);
    }
    return too_old;
}

/*
 * Read `<root>/eidos_mod.txt` if there is one.  A missing manifest is not an
 * error: the defaults (the directory name, order 0) apply.  A malformed
 * `order` value is reported and ignored rather than guessed at.
 */
static void read_manifest(BwMod *m)
{
    char path[1200];
    char line[512];
    FILE *fp;

    if (strlen(m->root) + 1 + sizeof(BW_MOD_MANIFEST) > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/%s", m->root, BW_MOD_MANIFEST);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        const char *ks = line;
        const char *ke;
        const char *vs;
        const char *ve;
        char *hash = strchr(line, '#');
        char *eq;

        if (hash != NULL) {
            *hash = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        ke = eq;
        vs = eq + 1;
        ve = line + strlen(line);
        trim(&ks, &ke);
        trim(&vs, &ve);
        if ((size_t)(ke - ks) == 4 && memcmp(ks, "name", 4) == 0 && ve > vs) {
            char *n = dup_n(vs, (size_t)(ve - vs));

            if (n != NULL) {
                free(m->name);
                m->name = n;
            }
        } else if ((size_t)(ke - ks) == 5 && memcmp(ks, "order", 5) == 0) {
            char num[32];
            char *end;
            long v;

            if (ve - vs <= 0 || (size_t)(ve - vs) >= sizeof(num)) {
                fprintf(stderr, "vfs: %s: bad order value, ignored\n", path);
                continue;
            }
            memcpy(num, vs, (size_t)(ve - vs));
            num[ve - vs] = '\0';
            v = strtol(num, &end, 10);
            if (*end != '\0' || v < -1000000L || v > 1000000L) {
                fprintf(stderr, "vfs: %s: bad order value '%s', ignored\n", path, num);
                continue;
            }
            m->order = (int)v;
        } else if ((size_t)(ke - ks) == 4 && memcmp(ks, "game", 4) == 0) {
            m->games = bw_mod_games_of(vs, (size_t)(ve - vs));
        } else if ((size_t)(ke - ks) == 5 && memcmp(ks, "eidos", 5) == 0) {
            if ((size_t)(ve - vs) < sizeof m->eidos) {
                memcpy(m->eidos, vs, (size_t)(ve - vs));
                m->eidos[ve - vs] = '\0';
            } else {
                m->eidos[0] = '\0';
            }
            {
                unsigned long v[3];

                if (m->eidos[0] != '\0' && version_parts(m->eidos, v) != 0) {
                    fprintf(stderr, "vfs: %s: eidos value '%s' is not X.Y.Z, ignored\n", path, m->eidos);
                    m->eidos[0] = '\0';
                }
            }
        }
    }
    fclose(fp);
}

void bw_clear_mods(void)
{
    u32 i;

    for (i = 0; i < g_mod_count; i++) {
        free_mod(&g_mods[i]);
    }
    free(g_mods);
    g_mods = NULL;
    g_mod_count = 0;
    g_mod_cap = 0;
    g_mods_set = 1;
    /* The list is gone, so the player's order is gone with it: the next
     * root added is sorted in by the manifest's key again. */
    g_player_order = 0;
    bw_vfs_flush();
    layer_cache_drop();
}

/* Append one root of length n (not terminated).  0, or -1. */
static int add_root_n(const char *path, size_t n)
{
    char root[1024];
    const char *base;
    BwMod m;
    u32 at;

    g_mods_set = 1;
    copy_root(root, sizeof(root), path, n);
    if (root[0] == '\0') {
        return -1;
    }
    {
        /* A packaged mod (`.bw`, sdk/mod_bw.h) is unpacked into its cache
         * folder, and that folder is the root from here on. */
        size_t rl = strlen(root);

        if (rl > 3 && (root[rl - 3] == '.') &&
            (root[rl - 2] == 'b' || root[rl - 2] == 'B') &&
            (root[rl - 1] == 'w' || root[rl - 1] == 'W') &&
            bw_plat_path_kind(root) == BW_PATH_FILE) {
            char cache[1024];
            char err[256] = "";

            if (bw_mod_bw_cache_path(root, cache, sizeof(cache)) != 0 ||
                bw_mod_bw_unpack(root, cache, err, sizeof(err)) != 0) {
                fprintf(stderr, "eidos: mod package %s refused: %s\n", root,
                        err[0] != '\0' ? err : "no cache folder");
                return -1;
            }
            copy_root(root, sizeof(root), cache, strlen(cache));
        }
    }
    m.root = dup_n(root, strlen(root));
    base = m.root == NULL ? NULL : m.root + strlen(m.root);
    while (base != NULL && base > m.root && base[-1] != '/' && base[-1] != '\\') {
        base--;
    }
    m.name = base == NULL ? NULL : dup_n(base, strlen(base));
    m.order = 0;
    m.games = BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
    m.eidos[0] = '\0';
    if (m.root == NULL || m.name == NULL) {
        free_mod(&m);
        return -1;
    }
    read_manifest(&m);
    {
        /* A mod made for the other game is not loaded: its files and its
         * code are that game's (a White mod's scripts over Black's maps).
         * One line says so. */
        const int have = bw_mod_game_loaded();

        if (m.games == 0) {
            fprintf(stderr, "eidos: mod %s: its game line names neither black nor white: not loaded\n", m.name);
            free_mod(&m);
            return -1;
        }
        if (have != 0 && (m.games & have) == 0) {
            fprintf(stderr, "eidos: mod %s is made for Pokemon %s, this game is Pokemon %s: not loaded\n", m.name,
                    m.games == BW_MOD_GAME_WHITE ? "White" : "Black", have == BW_MOD_GAME_WHITE ? "White" : "Black");
            free_mod(&m);
            return -1;
        }
    }
    if (bw_mod_eidos_too_old(m.eidos)) {
        /* A mod for a newer eidos is not loaded: this one would read part
         * of it and drop the rest line by line.  One line says so. */
        fprintf(stderr, "eidos: mod %s needs eidos %s or later, this is %s: not loaded\n", m.name, m.eidos,
                EIDOS_RELEASE_VERSION);
        free_mod(&m);
        return -1;
    }

    if (g_mod_count == g_mod_cap) {
        u32 cap = g_mod_cap == 0 ? 4 : g_mod_cap * 2;
        BwMod *grown = (BwMod *)realloc(g_mods, cap * sizeof(*grown));

        if (grown == NULL) {
            free_mod(&m);
            return -1;
        }
        g_mods = grown;
        g_mod_cap = cap;
    }

    /* Stable insertion: after every mod whose order is <= this one's.
     * The player's own order stands when it is in force: the root goes at
     * the end of the list, where the player put it. */
    at = g_mod_count;
    if (!g_player_order) {
        while (at > 0 && g_mods[at - 1].order > m.order) {
            g_mods[at] = g_mods[at - 1];
            at--;
        }
    }
    g_mods[at] = m;
    g_mod_count++;
    bw_vfs_flush();
    layer_cache_drop();
    return 0;
}

int bw_add_mod_root(const char *path)
{
    if (path == NULL) {
        g_mods_set = 1;
        return -1;
    }
    return add_root_n(path, strlen(path));
}

/*
 * The player's own order: this list, in this order.
 *
 * The whole list is replaced, so this is the way the roots are rebuilt at
 * the menu (docs/mod-state.md, section 2, constraint 1).  The roots are
 * taken in the order they are given and the manifest's `order` key does
 * not decide anything while the player's order is in force: the list is
 * the player's own.
 */
int bw_mod_set_order(const char *const *roots, u32 count)
{
    u32 i;
    int rc = 0;

    bw_clear_mods();
    if (roots == NULL) {
        return 0;
    }
    g_player_order = 1;
    for (i = 0; i < count; i++) {
        if (roots[i] == NULL || roots[i][0] == '\0') {
            continue;
        }
        if (add_root_n(roots[i], strlen(roots[i])) != 0) {
            fprintf(stderr, "vfs: the mod root %s could not be added\n",
                    roots[i]);
            rc = -1;
        }
    }
    return rc;
}

/* Replace the list with the ';'-separated roots in `list` (NULL clears).
 * This is the port's own list, so the manifest's `order` key decides the
 * order again (bw_clear_mods drops the player's order with the list). */
static void set_mod_list(const char *list)
{
    bw_clear_mods();
    while (list != NULL && *list != '\0') {
        const char *s = list;
        const char *e = strchr(list, ';');

        if (e == NULL) {
            e = list + strlen(list);
        }
        list = *e == ';' ? e + 1 : e;
        trim(&s, &e);
        if (e > s) {
            (void)add_root_n(s, (size_t)(e - s));
        }
    }
}

static void mods_init(void)
{
    if (!g_mods_set) {
        set_mod_list(getenv("BW_MOD_ROOT"));   /* marks the list set */
    }
}

void bw_set_mod_root(const char *path)
{
    /*
     * An empty or NULL path clears the overlay, which is what lets a caller
     * turn one off after a run.
     */
    set_mod_list(path);
}

const char *bw_mod_root(void)
{
    mods_init();
    return g_mod_count == 0 ? "" : g_mods[g_mod_count - 1].root;
}

u32 bw_mod_count(void)
{
    mods_init();
    return g_mod_count;
}

const char *bw_mod_root_at(u32 i)
{
    mods_init();
    return i < g_mod_count ? g_mods[i].root : NULL;
}

const char *bw_mod_name_at(u32 i)
{
    mods_init();
    return i < g_mod_count ? g_mods[i].name : NULL;
}

int bw_mod_order_at(u32 i)
{
    mods_init();
    return i < g_mod_count ? g_mods[i].order : 0;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

static int join(char *out, size_t out_size, const char *root, const char *rel)
{
    size_t need = strlen(root) + 1 + strlen(rel) + 1;

    if (need > out_size) {
        return -1;
    }
    /*
     * A forward slash is a valid separator on every host this targets,
     * Windows included, so the NitroFS spelling is used verbatim.
     */
    if (snprintf(out, out_size, "%s/%s", root, rel) >= (int)out_size) {
        return -1;
    }
    return 0;
}

/*
 * bw_vfs_file_layer's last answers.  The game opens the same few archives
 * again and again (every ARC_TOOL read opens its file), every open asks
 * twice, and an answer costs a file-system lookup per mod folder and one for
 * the data folder.  An answer is kept for LAYER_KEEP_NS and for one state of
 * the mod list and the data folder (g_layer_gen), so a file put into a mod
 * folder while the game runs is seen a second later at most.
 */
#define LAYER_CACHE   64u
#define LAYER_REL_MAX 96u
#define LAYER_KEEP_NS 1000000000ull

static struct LayerAnswer {
    char rel[LAYER_REL_MAX];
    int layer;
    unsigned long long at;
    u32 gen;                    /* 0: empty */
} g_layer[LAYER_CACHE];
static u32 g_layer_gen = 1;

static void layer_cache_drop(void)
{
    if (++g_layer_gen == 0) {
        g_layer_gen = 1;
    }
}

static int file_layer_look(const char *rel);

int bw_vfs_file_layer(const char *rel)
{
    struct LayerAnswer *a;
    unsigned long long now;
    size_t len;
    u32 h = 2166136261u, i;
    int layer;

    mods_init();
    len = strlen(rel);
    if (len >= LAYER_REL_MAX) {
        return file_layer_look(rel);
    }
    for (i = 0; i < len; i++) {
        h = (h ^ (u8)rel[i]) * 16777619u;
    }
    a = &g_layer[h % LAYER_CACHE];
    now = bw_clock_ns();
    /* (the answers are shared with the battle map's builder, a thread beside the game: an answer is read and
     * written whole) */
    bw_shared_lock();
    i = a->gen == g_layer_gen && now - a->at < LAYER_KEEP_NS && strcmp(a->rel, rel) == 0;
    layer = a->layer;
    bw_shared_unlock();
    if (i) {
        return layer;
    }
    layer = file_layer_look(rel);
    bw_shared_lock();
    memcpy(a->rel, rel, len + 1u);
    a->layer = layer;
    a->at = now;
    a->gen = g_layer_gen;
    bw_shared_unlock();
    return layer;
}

static int file_layer_look(const char *rel)
{
    char cand[1200];
    u32 i;

    /*
     * A regular file, not merely something fopen accepts: on POSIX fopen
     * opens a directory, and a directory at an archive's path in a mod is
     * a member override (sdk/vfs_narc.c), not a replacement file.
     */
    for (i = g_mod_count; i > 0; i--) {
        if (join(cand, sizeof(cand), g_mods[i - 1].root, rel) == 0 &&
            bw_plat_path_kind(cand) == BW_PATH_FILE) {
            return (int)(i - 1);
        }
    }
    if (join(cand, sizeof(cand), bw_data_root(), rel) == 0 &&
        bw_plat_path_kind(cand) == BW_PATH_FILE) {
        return -1;
    }
    return BW_LAYER_NONE;
}

int bw_data_path(char *out, size_t out_size, const char *rel)
{
    int layer = bw_vfs_file_layer(rel);

    if (layer >= 0) {
        return join(out, out_size, g_mods[layer].root, rel);
    }
    return join(out, out_size, bw_data_root(), rel);
}

int bw_data_exists(const char *rel)
{
    return bw_vfs_file_layer(rel) != BW_LAYER_NONE ? 1 : 0;
}
