/*
 * sdk/battle_handlers.c -- moves, abilities and items that work like a
 * built-in one, with numbers of their own (battle_handlers.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "vfs.h"

#define MAX_PARAMS  12
#define MAX_DEPTH   32
#define MAX_CHAIN   16

typedef struct Param {
    u8  key, index;         /* index 1 for `stat`, 2 for `stat.2` */
    s32 value;
} Param;

typedef struct Entry {
    int   type, id, base;
    int   nparams;
    Param params[MAX_PARAMS];
} Entry;

static Entry *g_entries;
static int    g_count, g_cap;

/* ------------------------------------------------------------------ */
/* The words                                                           */
/* ------------------------------------------------------------------ */

typedef struct Word {
    const char *name;
    int value;
} Word;

static const Word k_stats[] = {
    { "atk", 1 }, { "def", 2 }, { "spatk", 3 }, { "spdef", 4 }, { "speed", 5 },
    { "accuracy", 6 }, { "evasion", 7 }, { NULL, 0 }
};

/* the game's sick ids (btl_sickcont.c): the major ones and two others */
static const Word k_status[] = {
    { "paralysis", 1 }, { "sleep", 2 }, { "freeze", 3 }, { "burn", 4 }, { "poison", 5 },
    { "confusion", 6 }, { "infatuation", 7 }, { NULL, 0 }
};

static const Word k_types[] = {
    { "normal", 0 }, { "fighting", 1 }, { "flying", 2 }, { "poison", 3 }, { "ground", 4 },
    { "rock", 5 }, { "bug", 6 }, { "ghost", 7 }, { "steel", 8 }, { "fire", 9 },
    { "water", 10 }, { "grass", 11 }, { "electric", 12 }, { "psychic", 13 }, { "ice", 14 },
    { "dragon", 15 }, { "dark", 16 }, { NULL, 0 }
};

static const Word k_weather[] = {
    { "sun", 1 }, { "rain", 2 }, { "hail", 3 }, { "sand", 4 }, { NULL, 0 }
};

static const char *const k_keys[BW_BH_KEY_COUNT] = {
    "stat", "stages", "status", "chance", "type", "ratio", "hp", "weather"
};

static int word_value(const Word *w, const char *s, size_t n, int *out)
{
    for (; w->name != NULL; w++) {
        if (strlen(w->name) == n && strncmp(w->name, s, n) == 0) {
            *out = w->value;
            return 0;
        }
    }
    return -1;
}

static int int_value(const char *s, size_t n, int lo, int hi, int *out)
{
    char buf[16];
    char *end;
    long v;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtol(buf, &end, 10);
    if (*end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/* "1.5" in 4096ths, 0.1 to 4.0 */
static int ratio_value(const char *s, size_t n, int *out)
{
    char buf[16];
    char *end;
    double v;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtod(buf, &end);
    if (*end != '\0' || v < 0.1 || v > 4.0) {
        return -1;
    }
    *out = (int)(v * 4096.0 + 0.5);
    return 0;
}

/* "1/8" */
static int hp_value(const char *s, size_t n, int *out)
{
    if (n < 3 || s[0] != '1' || s[1] != '/') {
        return -1;
    }
    return int_value(s + 2, n - 2, 1, 16, out);
}

static int key_value(int key, const char *s, size_t n, int *out)
{
    switch (key) {
    case BW_BH_KEY_STAT:    return word_value(k_stats, s, n, out);
    case BW_BH_KEY_STAGES:  return int_value(s, n, -6, 6, out);
    case BW_BH_KEY_STATUS:  return word_value(k_status, s, n, out);
    case BW_BH_KEY_CHANCE:  return int_value(s, n, 0, 100, out);
    case BW_BH_KEY_TYPE:    return word_value(k_types, s, n, out);
    case BW_BH_KEY_RATIO:   return ratio_value(s, n, out);
    case BW_BH_KEY_HP:      return hp_value(s, n, out);
    case BW_BH_KEY_WEATHER: return word_value(k_weather, s, n, out);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* The lines                                                           */
/* ------------------------------------------------------------------ */

static Entry *find(int type, int id)
{
    int i;

    for (i = 0; i < g_count; i++) {
        if (g_entries[i].type == type && g_entries[i].id == id) {
            return &g_entries[i];
        }
    }
    return NULL;
}

static void say(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int bw_bh_parse_line(int type, const char *line, char *why, int why_size)
{
    Entry e;
    const char *p = line, *s;
    char msg[160];
    long v;
    char *end;
    Entry *have;

    memset(&e, 0, sizeof e);
    e.type = type;
    while (is_space(*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#') {
        return 1;                   /* nothing on the line */
    }
    v = strtol(p, &end, 10);
    if (end == p || v < 1 || v > 0xFFFF) {
        say(why, why_size, "expected `<id> = <base id> [key=value ...]`");
        return -1;
    }
    e.id = (int)v;
    p = end;
    while (is_space(*p)) {
        p++;
    }
    if (*p != '=') {
        say(why, why_size, "expected `=` after the id");
        return -1;
    }
    p++;
    v = strtol(p, &end, 10);
    if (end == p || v < 1 || v > 0xFFFF) {
        say(why, why_size, "expected the base id after `=`");
        return -1;
    }
    e.base = (int)v;
    p = end;
    for (;;) {
        const char *eq, *dot, *ks;
        int key, index = 1, value;

        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        s = p;
        while (*p != '\0' && !is_space(*p) && *p != '#') {
            p++;
        }
        eq = memchr(s, '=', (size_t)(p - s));
        if (eq == NULL) {
            snprintf(msg, sizeof msg, "expected key=value, found `%.*s`", (int)(p - s), s);
            say(why, why_size, msg);
            return -1;
        }
        dot = memchr(s, '.', (size_t)(eq - s));
        ks = dot != NULL ? dot : eq;
        for (key = 0; key < BW_BH_KEY_COUNT; key++) {
            if ((size_t)(ks - s) == strlen(k_keys[key]) && strncmp(k_keys[key], s, (size_t)(ks - s)) == 0) {
                break;
            }
        }
        if (key == BW_BH_KEY_COUNT) {
            snprintf(msg, sizeof msg, "unknown key `%.*s`", (int)(eq - s), s);
            say(why, why_size, msg);
            return -1;
        }
        if (dot != NULL && int_value(dot + 1, (size_t)(eq - dot - 1), 1, 8, &index) != 0) {
            snprintf(msg, sizeof msg, "the index of `%.*s` is not 1 to 8", (int)(eq - s), s);
            say(why, why_size, msg);
            return -1;
        }
        if (key_value(key, eq + 1, (size_t)(p - eq - 1), &value) != 0) {
            snprintf(msg, sizeof msg, "`%.*s` is not a value of %s", (int)(p - eq - 1), eq + 1, k_keys[key]);
            say(why, why_size, msg);
            return -1;
        }
        if (e.nparams == MAX_PARAMS) {
            say(why, why_size, "too many keys on one line");
            return -1;
        }
        e.params[e.nparams].key = (u8)key;
        e.params[e.nparams].index = (u8)index;
        e.params[e.nparams].value = value;
        e.nparams++;
    }
    have = find(type, e.id);
    if (have != NULL) {
        *have = e;                  /* a later mod's line wins */
        return 0;
    }
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 32;
        Entry *grown = (Entry *)realloc(g_entries, (size_t)cap * sizeof *grown);

        if (grown == NULL) {
            say(why, why_size, "out of memory");
            return -1;
        }
        g_entries = grown;
        g_cap = cap;
    }
    g_entries[g_count++] = e;
    return 0;
}

void bw_bh_clear(void)
{
    g_count = 0;
}

static void load_file(const char *root, const char *name, int type)
{
    char path[1200], line[512], why[200];
    FILE *f;
    int n = 0;

    if (strlen(root) + strlen(name) + 24 > sizeof path) {
        return;
    }
    sprintf(path, "%s/eidos/tables/%s", root, name);
    if ((f = fopen(path, "rb")) == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        n++;
        if (bw_bh_parse_line(type, line, why, sizeof why) < 0) {
            fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* The places the game's handlers use a number                         */
/* ------------------------------------------------------------------ */

/* One row a place a handler uses a number eidos can change, written by
 * tools/handler_params.py from the game's own handler tables: the
 * factor's type, the built-in id, the handler's row in its table, the
 * kind of place and its number among the id's places of that kind (the
 * key's index: `stat` 1, `stat.2` 2). */
typedef struct Site {
    u8  type;
    u16 id;
    u8  handler;
    u8  site;
    u8  index;
} Site;

static const Site k_sites[] = {
#include "battle_handler_sites.inc"
};

#define SITE_COUNT ((int)(sizeof(k_sites) / sizeof(k_sites[0])))

/* the keys of each kind of place */
static int site_of_key(int key)
{
    switch (key) {
    case BW_BH_KEY_STAT:
    case BW_BH_KEY_STAGES:  return BW_BH_SITE_RANK;
    case BW_BH_KEY_STATUS:  return BW_BH_SITE_SICK;
    case BW_BH_KEY_WEATHER: return BW_BH_SITE_WEATHER;
    case BW_BH_KEY_CHANCE:  return BW_BH_SITE_CHANCE;
    case BW_BH_KEY_HP:      return BW_BH_SITE_HP;
    case BW_BH_KEY_RATIO:   return BW_BH_SITE_RATIO;
    case BW_BH_KEY_TYPE:    return BW_BH_SITE_TYPE;
    }
    return -1;
}

/* The index of the place (type, id, handler, site), 0 when it is not one. */
static int site_index(int type, int id, int handler, int site)
{
    int i;

    for (i = 0; i < SITE_COUNT; i++) {
        if (k_sites[i].type == type && k_sites[i].id == id && k_sites[i].handler == handler
            && k_sites[i].site == site) {
            return k_sites[i].index;
        }
    }
    return 0;
}

/* Whether an id of `type` has a place of `site` with that index. */
static int site_exists(int type, int id, int site, int index)
{
    int i;

    for (i = 0; i < SITE_COUNT; i++) {
        if (k_sites[i].type == type && k_sites[i].id == id && k_sites[i].site == site
            && k_sites[i].index == index) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The bases                                                           */
/* ------------------------------------------------------------------ */

/* The end of `id`'s chain of bases, or -1 for a chain that comes back. */
static int chain_end(int type, int id)
{
    int hops;
    Entry *e;

    for (hops = 0; hops < MAX_CHAIN; hops++) {
        e = find(type, id);
        if (e == NULL || e->base == id) {
            return id;
        }
        id = e->base;
    }
    return -1;
}

int bw_bh_base(int type, int id)
{
    int end = chain_end(type, id);

    return end < 0 ? id : end;
}

static const char *type_name(int type)
{
    return type == BW_BH_ABILITY ? "ability" : type == BW_BH_ITEM ? "item" : "move";
}

/* After every mod's lines are read: a chain that comes back, or a key the
 * base's handlers have no place for, is said and left out. */
static void check_lines(void)
{
    int i, k;

    for (i = 0; i < g_count; i++) {
        Entry *e = &g_entries[i];
        int base = chain_end(e->type, e->id);

        if (base < 0) {
            fprintf(stderr, "eidos: battle tables: %s %d: its bases come back to it; the line is left out\n",
                    type_name(e->type), e->id);
            g_entries[i] = g_entries[--g_count];
            i--;
            continue;
        }
        for (k = 0; k < e->nparams; k++) {
            int site = site_of_key(e->params[k].key);

            if (!site_exists(e->type, base, site, e->params[k].index)) {
                fprintf(stderr, "eidos: battle tables: %s %d (works like %d): %s%s%d has no place in its handlers "
                        "(tools/handler_params.py lists the keys); left out\n", type_name(e->type), e->id, base,
                        k_keys[e->params[k].key], e->params[k].index > 1 ? "." : " ", e->params[k].index);
                e->params[k] = e->params[--e->nparams];
                k--;
            }
        }
    }
}

void bw_bh_load(void)
{
    u32 m;

    bw_bh_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);

        load_file(root, "ability_handlers.txt", BW_BH_ABILITY);
        load_file(root, "move_handlers.txt", BW_BH_MOVE);
        load_file(root, "item_handlers.txt", BW_BH_ITEM);
    }
    check_lines();
}

void bw_bh_check(void)
{
    check_lines();
}

int bw_bh_key(int type, int id, int key, int index, int *value)
{
    Entry *e = find(type, id);
    int i;

    for (i = 0; e != NULL && i < e->nparams; i++) {
        if (e->params[i].key == key && e->params[i].index == index) {
            *value = e->params[i].value;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The running handler                                                 */
/* ------------------------------------------------------------------ */

typedef struct Frame {
    int type, id, handler;
    unsigned used;          /* the kinds of place already met in this call */
} Frame;

static Frame g_stack[MAX_DEPTH];
static int   g_depth, g_lost;   /* (g_lost: calls past MAX_DEPTH, not kept) */

void bw_bh_enter(int type, int id, int handler)
{
    if (g_depth == MAX_DEPTH) {
        g_lost++;
        return;
    }
    g_stack[g_depth].type = type;
    g_stack[g_depth].id = id;
    g_stack[g_depth].handler = handler;
    g_stack[g_depth].used = 0;
    g_depth++;
}

void bw_bh_leave(void)
{
    if (g_lost > 0) {
        g_lost--;
    } else if (g_depth > 0) {
        g_depth--;
    }
}

/*
 * The index of the place the running handler has just reached, 0 for none:
 * the first place of the kind in this call is the handler's own (the
 * generator offers a place only where it runs once, before any command
 * whose engine work could reach another; later ones are the engine's).
 */
static int reached(int site, Frame **out)
{
    Frame *f;
    unsigned bit = 1u << site;

    if (g_count == 0 || g_depth == 0 || g_lost != 0) {
        return 0;
    }
    f = &g_stack[g_depth - 1];
    if (f->used & bit) {
        return 0;
    }
    f->used |= bit;
    *out = f;
    return site_index(f->type, bw_bh_base(f->type, f->id), f->handler, site);
}

static void said(const Frame *f, const char *what, long was, long now)
{
    if (was != now) {
        fprintf(stderr, "eidos: battle: %s %d: %s %ld -> %ld (eidos/tables)\n", type_name(f->type), f->id, what,
                was, now);
    }
}

void bw_bh_rank(u32 *stat, s8 *amount)
{
    Frame *f;
    int n = reached(BW_BH_SITE_RANK, &f), v;

    if (n == 0) {
        return;
    }
    if (bw_bh_key(f->type, f->id, BW_BH_KEY_STAT, n, &v)) {
        said(f, "stat", (long)*stat, v);
        *stat = (u32)v;
    }
    if (bw_bh_key(f->type, f->id, BW_BH_KEY_STAGES, n, &v)) {
        said(f, "stages", *amount, v);
        *amount = (s8)v;
    }
}

int bw_bh_sick(u32 *kind)
{
    Frame *f;
    int n = reached(BW_BH_SITE_SICK, &f), v;

    if (n == 0 || !bw_bh_key(f->type, f->id, BW_BH_KEY_STATUS, n, &v) || (u32)v == *kind) {
        return 0;
    }
    said(f, "status", (long)*kind, v);
    *kind = (u32)v;
    return 1;
}

void bw_bh_weather(u8 *weather)
{
    Frame *f;
    int n = reached(BW_BH_SITE_WEATHER, &f), v;

    if (n != 0 && bw_bh_key(f->type, f->id, BW_BH_KEY_WEATHER, n, &v)) {
        said(f, "weather", *weather, v);
        *weather = (u8)v;
    }
}

static int one_value(int site, int key, int value)
{
    Frame *f;
    int n = reached(site, &f), v;

    if (n != 0 && bw_bh_key(f->type, f->id, key, n, &v)) {
        said(f, k_keys[key], value, v);
        return v;
    }
    return value;
}

u32 bw_bh_chance(u32 percent)  { return (u32)one_value(BW_BH_SITE_CHANCE, BW_BH_KEY_CHANCE, (int)percent); }
u32 bw_bh_hp(u32 divisor)      { return (u32)one_value(BW_BH_SITE_HP, BW_BH_KEY_HP, (int)divisor); }
s32 bw_bh_ratio(s32 ratio)     { return one_value(BW_BH_SITE_RATIO, BW_BH_KEY_RATIO, ratio); }
int bw_bh_type(int type)       { return one_value(BW_BH_SITE_TYPE, BW_BH_KEY_TYPE, type); }
