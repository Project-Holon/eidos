/*
 * sdk/battle_handlers.c -- moves, abilities and items that work like one
 * or more built-in ones, with numbers of their own (battle_handlers.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "move_kinds.h"
#include "vfs.h"

#define MAX_PARAMS  12
#define MAX_PARTS   4           /* bases on one line: `a + b + c + d` */
#define MAX_DEPTH   32
#define MAX_CHAIN   16

typedef struct Param {
    u8  key, index;         /* index 1 for `stat`, 2 for `stat.2` */
    s32 value;
} Param;

/* One base of a line and the keys after it. */
typedef struct Part {
    int   base;             /* as written; its built-in end: chain_end */
    int   nparams;
    Param params[MAX_PARAMS];
} Part;

typedef struct Entry {
    int   type, id;
    int   root;             /* the mod whose line it is (bw_mod_root_at), -1 for a test line */
    int   nparts;
    Part  parts[MAX_PARTS];
    /* the joined handler table (bw_bh_join), made at the first battle that
     * needs it and kept: its rows and where each part's rows start */
    void *joined;
    u32   joined_total;
    u32   part_rows[MAX_PARTS];
} Entry;

static Entry *g_entries;
static int    g_count, g_cap;
static int    g_loading_root = -1;  /* the mod whose tables load_file reads */

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
    "stat", "stages", "status", "chance", "type", "ratio", "hp", "weather", "turns", "damage", "power", "accuracy",
    "kind", "priority", "form"
};

/* the kinds of move eidos's ability bases test (patch_ov093_hand_tokusei.c
 * KIND_*): a flag of the move record, its damage kind, or a kind of
 * sdk/move_kinds.c */
static const Word k_kinds[] = {
    { "contact", 1 }, { "punch", 2 }, { "sound", 3 }, { "bite", 4 }, { "pulse", 5 }, { "ball", 6 },
    { "powder", 7 }, { "physical", 8 }, { "special", 9 }, { "status", 10 }, { "one_hit", 11 }, { NULL, 0 }
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

extern int bw_type_by_name(const char *s, size_t n) __attribute__((weak));     /* sdk/type_icons.c */
extern u32 bw_type_count(void) __attribute__((weak));                           /* sdk/type_chart.c */

/* A type: the game's name, an added type's types.txt `name`, or its number
 * (0 up to the mods' type limit). */
static int type_value(const char *s, size_t n, int *out)
{
    u32 count = bw_type_count != NULL ? bw_type_count() : 17u;

    if (word_value(k_types, s, n, out) == 0) {
        return 0;
    }
    if (bw_type_by_name != NULL && (*out = bw_type_by_name(s, n)) >= 0) {
        return 0;
    }
    return int_value(s, n, 0, (int)count - 1, out);
}

static int key_value(int key, const char *s, size_t n, int *out)
{
    switch (key) {
    case BW_BH_KEY_STAT:    return word_value(k_stats, s, n, out);
    case BW_BH_KEY_STAGES:  return int_value(s, n, -6, 6, out);
    case BW_BH_KEY_STATUS:  return word_value(k_status, s, n, out);
    case BW_BH_KEY_CHANCE:  return int_value(s, n, 0, 100, out);
    case BW_BH_KEY_TYPE:    return type_value(s, n, out);
    case BW_BH_KEY_RATIO:   return ratio_value(s, n, out);
    case BW_BH_KEY_HP:      return hp_value(s, n, out);
    case BW_BH_KEY_WEATHER: return word_value(k_weather, s, n, out);
    case BW_BH_KEY_TURNS:   return int_value(s, n, 1, 63, out);     /* (SickCont's 6-bit count) */
    case BW_BH_KEY_DAMAGE:  return int_value(s, n, 1, 999, out);
    case BW_BH_KEY_POWER:   return int_value(s, n, 1, 255, out);    /* (the move record's byte) */
    case BW_BH_KEY_ACCURACY: return int_value(s, n, 1, 100, out);
    case BW_BH_KEY_KIND:    return word_value(k_kinds, s, n, out);
    case BW_BH_KEY_PRIORITY: return int_value(s, n, -6, 6, out);
    case BW_BH_KEY_FORM:    return int_value(s, n, 0, 31, out);
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

/* `key=value` from s to e into the part: 0, or -1 with `why`. */
static int parse_param(Part *part, const char *s, const char *e, char *why, int why_size)
{
    const char *eq = memchr(s, '=', (size_t)(e - s)), *dot, *ks;
    char msg[160];
    int key, index = 1, value;

    if (eq == NULL) {
        snprintf(msg, sizeof msg, "expected key=value, found `%.*s`", (int)(e - s), s);
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
    if (key_value(key, eq + 1, (size_t)(e - eq - 1), &value) != 0) {
        snprintf(msg, sizeof msg, "`%.*s` is not a value of %s", (int)(e - eq - 1), eq + 1, k_keys[key]);
        say(why, why_size, msg);
        return -1;
    }
    if (part->nparams == MAX_PARAMS) {
        say(why, why_size, "too many keys for one base");
        return -1;
    }
    part->params[part->nparams].key = (u8)key;
    part->params[part->nparams].index = (u8)index;
    part->params[part->nparams].value = value;
    part->nparams++;
    return 0;
}

/* eidos's own move bases, by name (battle_handlers.h) */
static const char *const k_named[BW_BH_NAMED_END - BW_BH_NAMED_FIRST] = {
    "misty_terrain", "flower_shield", "crafty_shield", "fairy_lock", "geomancy", "fell_stinger", "needs_status",
    "spiky_shield", "kings_shield", "baneful_bunker", "contact_guard", "super_effective", "freeze_dry", "second_type",
    "flying_press", "add_type", "trick_or_treat", "forests_curse", "grassy_terrain", "electric_terrain",
    "psychic_terrain", "hits_floating", "parting_shot", "topsy_turvy", "needs_berry", "belch", "burn_up", "shore_up",
    "strength_sap", "rototiller", "magnetic_flux", "set_type", "electrify", "ion_deluge",
    "sticky_web", "powder", "throat_chop", "laser_focus", "stomping_tantrum", "spectral_thief", "pollen_puff"
};

const char *bw_bh_named_name(int base)
{
    return base >= BW_BH_NAMED_FIRST && base < BW_BH_NAMED_END ? k_named[base - BW_BH_NAMED_FIRST] : NULL;
}

/* eidos's own ability bases, by name (battle_handlers.h) */
static const char *const k_named_abilities[BW_BH_ABILITY_NAMED_END - BW_BH_ABILITY_NAMED_FIRST] = {
    "pixilate", "aerilate", "refrigerate", "galvanize", "boost_kind", "tough_claws", "strong_jaw", "mega_launcher",
    "boost_type", "guard_kind", "fur_coat", "block_kind", "bulletproof", "priority_type", "gale_wings", "entry_stat",
    "ko_stat", "hit_type_stat", "sweet_veil", "aroma_veil", "flower_veil", "cheek_pouch", "protean", "magician",
    "symbiosis", "parental_bond", "stance_change", "grass_pelt"
};

const char *bw_bh_ability_named_name(int base)
{
    return base >= BW_BH_ABILITY_NAMED_FIRST && base < BW_BH_ABILITY_NAMED_END
        ? k_named_abilities[base - BW_BH_ABILITY_NAMED_FIRST] : NULL;
}

/* A base at p, a move's eidos base by name too: its value and the end, or -1. */
static int parse_base(int type, const char **p, int *base)
{
    char *end;
    long v;

    while (is_space(**p)) {
        (*p)++;
    }
    if ((**p >= 'a' && **p <= 'z') || **p == '_') {
        const char *s = *p;
        int k;

        while ((**p >= 'a' && **p <= 'z') || (**p >= '0' && **p <= '9') || **p == '_') {
            (*p)++;
        }
        for (k = 0; type == BW_BH_MOVE && k < BW_BH_NAMED_END - BW_BH_NAMED_FIRST; k++) {
            if ((size_t)(*p - s) == strlen(k_named[k]) && strncmp(k_named[k], s, (size_t)(*p - s)) == 0) {
                *base = BW_BH_NAMED_FIRST + k;
                return 0;
            }
        }
        for (k = 0; type == BW_BH_ABILITY && k < BW_BH_ABILITY_NAMED_END - BW_BH_ABILITY_NAMED_FIRST; k++) {
            if ((size_t)(*p - s) == strlen(k_named_abilities[k])
                && strncmp(k_named_abilities[k], s, (size_t)(*p - s)) == 0) {
                *base = BW_BH_ABILITY_NAMED_FIRST + k;
                return 0;
            }
        }
        *p = s;
        return -1;
    }
    v = strtol(*p, &end, 10);
    if (end == *p || v < 1 || v > 0xFFFF) {
        return -1;
    }
    *base = (int)v;
    *p = end;
    return 0;
}

int bw_bh_parse_line(int type, const char *line, char *why, int why_size)
{
    Entry e;
    const char *p = line, *s;
    long v;
    char *end;
    Entry *have;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.root = g_loading_root;
    while (is_space(*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#') {
        return 1;                   /* nothing on the line */
    }
    v = strtol(p, &end, 10);
    if (end == p || v < 1 || v > 0xFFFF) {
        say(why, why_size, "expected `<id> = <base id> [key=value ...] [+ <base id> ...]`");
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
    if (parse_base(type, &p, &e.parts[0].base) != 0) {
        say(why, why_size, type == BW_BH_MOVE ? "expected a base id or an eidos base name after `=`"
                                               : type == BW_BH_ABILITY
                                               ? "expected a base id or an eidos ability base name after `=`"
                                               : "expected the base id after `=`");
        return -1;
    }
    e.nparts = 1;
    for (;;) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        if (*p == '+') {
            p++;
            if (e.nparts == MAX_PARTS) {
                say(why, why_size, "more than 4 bases on one line");
                return -1;
            }
            if (parse_base(type, &p, &e.parts[e.nparts].base) != 0) {
                say(why, why_size, type == BW_BH_MOVE ? "expected a base id or an eidos base name after `+`"
                                                       : "expected a base id after `+`");
                return -1;
            }
            e.nparts++;
            continue;
        }
        s = p;
        while (*p != '\0' && !is_space(*p) && *p != '#' && *p != '+') {
            p++;
        }
        if (parse_param(&e.parts[e.nparts - 1], s, p, why, why_size) != 0) {
            return -1;
        }
    }
    have = find(type, e.id);
    if (have != NULL) {
        free(have->joined);
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
    int i;

    for (i = 0; i < g_count; i++) {
        free(g_entries[i].joined);
    }
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
/* What the game's handlers use                                        */
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

/* The factor work slots a built-in id's handlers keep state in, as a mask
 * (battle_handler_work.inc, the same generator): two bases of one line
 * may not share one. */
typedef struct Work {
    u8  type;
    u16 id;
    u8  mask;
} Work;

static const Work k_work[] = {
#include "battle_handler_work.inc"
};

#define WORK_COUNT ((int)(sizeof(k_work) / sizeof(k_work[0])))

/* The places of eidos's own bases (stubs/ov/patch_ov093_hand_waza.c),
 * by hand: Flower Shield's one handler (row 0, event 0x9E) tests a type
 * (bw_bh_type) and commits one stat change; Fell Stinger's commits one
 * stat change; needs_status asks bw_bh_sick for the status it tests.  The
 * others use fixed numbers, as the game's own field and side effects do.
 * None keeps state
 * in a work slot of its move factor. */
typedef struct NamedSite {
    int id;
    u8  handler;
    u8  site;
    u8  index;
} NamedSite;

static const NamedSite k_named_sites[] = {
    { BW_BH_FLOWER_SHIELD, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_FLOWER_SHIELD, 0, BW_BH_SITE_RANK, 1 },
    { BW_BH_FELL_STINGER, 0, BW_BH_SITE_RANK, 1 },       /* the stat raised on a knock-out */
    { BW_BH_NEEDS_STATUS, 0, BW_BH_SITE_SICK, 1 },       /* the status the target must have */
    /* the guards: what a contact move they stop does to the attacker (row 3,
     * after Protect's three rows; read with bw_bh_arg) */
    { BW_BH_SPIKY_SHIELD, 3, BW_BH_SITE_HP, 1 },
    { BW_BH_KINGS_SHIELD, 3, BW_BH_SITE_RANK, 1 },
    { BW_BH_BANEFUL_BUNKER, 3, BW_BH_SITE_SICK, 1 },
    { BW_BH_CONTACT_GUARD, 3, BW_BH_SITE_HP, 1 },
    { BW_BH_CONTACT_GUARD, 3, BW_BH_SITE_RANK, 1 },
    { BW_BH_CONTACT_GUARD, 3, BW_BH_SITE_SICK, 1 },
    /* the type-effectiveness bases (no handlers: BTL_SERVER_FLOW_CalcTypeEffectiveness
     * reads the line, bw_bh_move_named) and the third type */
    { BW_BH_SUPER_EFFECTIVE, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_FREEZE_DRY, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_SECOND_TYPE, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_FLYING_PRESS, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_ADD_TYPE, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_TRICK_OR_TREAT, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_FORESTS_CURSE, 0, BW_BH_SITE_TYPE, 1 },
    /* the terrains: how long, Misty's Dragon damage and the others' type
     * power, Grassy's heal (read at the start, kept in the field factor) */
    { BW_BH_MISTY_TERRAIN, 0, BW_BH_SITE_TURNS, 1 },
    { BW_BH_MISTY_TERRAIN, 0, BW_BH_SITE_RATIO, 1 },
    { BW_BH_GRASSY_TERRAIN, 0, BW_BH_SITE_TURNS, 1 },
    { BW_BH_GRASSY_TERRAIN, 0, BW_BH_SITE_RATIO, 1 },
    { BW_BH_GRASSY_TERRAIN, 0, BW_BH_SITE_HP, 1 },
    { BW_BH_ELECTRIC_TERRAIN, 0, BW_BH_SITE_TURNS, 1 },
    { BW_BH_ELECTRIC_TERRAIN, 0, BW_BH_SITE_RATIO, 1 },
    { BW_BH_PSYCHIC_TERRAIN, 0, BW_BH_SITE_TURNS, 1 },
    { BW_BH_PSYCHIC_TERRAIN, 0, BW_BH_SITE_RATIO, 1 },
    /* the later moves of one handler (read with bw_bh_arg) */
    { BW_BH_PARTING_SHOT, 0, BW_BH_SITE_RANK, 1 },
    { BW_BH_PARTING_SHOT, 0, BW_BH_SITE_RANK, 2 },
    { BW_BH_STRENGTH_SAP, 0, BW_BH_SITE_RANK, 1 },
    { BW_BH_BURN_UP, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_SHORE_UP, 0, BW_BH_SITE_RATIO, 1 },
    { BW_BH_SHORE_UP, 0, BW_BH_SITE_RATIO, 2 },
    { BW_BH_ROTOTILLER, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_ROTOTILLER, 0, BW_BH_SITE_RANK, 1 },
    { BW_BH_ROTOTILLER, 0, BW_BH_SITE_RANK, 2 },
    { BW_BH_MAGNETIC_FLUX, 0, BW_BH_SITE_RANK, 1 },
    { BW_BH_MAGNETIC_FLUX, 0, BW_BH_SITE_RANK, 2 },
    { BW_BH_SET_TYPE, 0, BW_BH_SITE_TYPE, 1 },
    { BW_BH_ELECTRIFY, 0, BW_BH_SITE_TYPE, 1 },         /* the type the target's moves take */
    { BW_BH_ION_DELUGE, 0, BW_BH_SITE_TYPE, 1 },        /* the type moves take */
    { BW_BH_ION_DELUGE, 0, BW_BH_SITE_TYPE, 2 },        /* the type they had (Normal) */
    { BW_BH_STICKY_WEB, 0, BW_BH_SITE_RANK, 1 },        /* the stat a Pokemon coming in loses */
    { BW_BH_POWDER, 0, BW_BH_SITE_TYPE, 1 },            /* the type of move that explodes (Fire) */
    { BW_BH_POWDER, 0, BW_BH_SITE_HP, 1 },              /* the user's HP it costs (1/4) */
    { BW_BH_THROAT_CHOP, 0, BW_BH_SITE_TURNS, 1 },      /* how long (2) */
    { BW_BH_STOMPING_TANTRUM, 0, BW_BH_SITE_RATIO, 1 }, /* the power after a failed move (2.0) */
    { BW_BH_POLLEN_PUFF, 1, BW_BH_SITE_HP, 1 },         /* the ally's HP it heals (1/2) */
};

#define NAMED_SITE_COUNT ((int)(sizeof(k_named_sites) / sizeof(k_named_sites[0])))

/* The keys of eidos's own ability bases (stubs/ov/patch_ov093_hand_tokusei.c
 * reads them with bw_bh_arg, by hand: the code is eidos's), as the kind of
 * place each key is for and its index (`type.2`), and the factor work slots
 * each base keeps state in. */
typedef struct NamedKey {
    int id;
    u8  site;
    u8  index;
} NamedKey;

static const NamedKey k_named_ability_keys[] = {
    { BW_BH_PIXILATE, BW_BH_SITE_TYPE, 1 }, { BW_BH_PIXILATE, BW_BH_SITE_RATIO, 1 },
    { BW_BH_AERILATE, BW_BH_SITE_TYPE, 1 }, { BW_BH_AERILATE, BW_BH_SITE_RATIO, 1 },
    { BW_BH_REFRIGERATE, BW_BH_SITE_TYPE, 1 }, { BW_BH_REFRIGERATE, BW_BH_SITE_RATIO, 1 },
    { BW_BH_GALVANIZE, BW_BH_SITE_TYPE, 1 }, { BW_BH_GALVANIZE, BW_BH_SITE_RATIO, 1 },
    { BW_BH_BOOST_KIND, BW_BH_SITE_KIND, 1 }, { BW_BH_BOOST_KIND, BW_BH_SITE_RATIO, 1 },
    { BW_BH_TOUGH_CLAWS, BW_BH_SITE_RATIO, 1 },
    { BW_BH_STRONG_JAW, BW_BH_SITE_RATIO, 1 },
    { BW_BH_MEGA_LAUNCHER, BW_BH_SITE_RATIO, 1 },
    { BW_BH_BOOST_TYPE, BW_BH_SITE_TYPE, 1 }, { BW_BH_BOOST_TYPE, BW_BH_SITE_RATIO, 1 },
    { BW_BH_GUARD_KIND, BW_BH_SITE_KIND, 1 }, { BW_BH_GUARD_KIND, BW_BH_SITE_RATIO, 1 },
    { BW_BH_FUR_COAT, BW_BH_SITE_RATIO, 1 },
    { BW_BH_BLOCK_KIND, BW_BH_SITE_KIND, 1 },
    { BW_BH_PRIORITY_TYPE, BW_BH_SITE_TYPE, 1 }, { BW_BH_PRIORITY_TYPE, BW_BH_SITE_PRIORITY, 1 },
    { BW_BH_PRIORITY_TYPE, BW_BH_SITE_HP, 1 },
    { BW_BH_GALE_WINGS, BW_BH_SITE_PRIORITY, 1 }, { BW_BH_GALE_WINGS, BW_BH_SITE_HP, 1 },
    { BW_BH_ENTRY_STAT, BW_BH_SITE_RANK, 1 },
    { BW_BH_KO_STAT, BW_BH_SITE_RANK, 1 },
    { BW_BH_HIT_TYPE_STAT, BW_BH_SITE_TYPE, 1 }, { BW_BH_HIT_TYPE_STAT, BW_BH_SITE_TYPE, 2 },
    { BW_BH_HIT_TYPE_STAT, BW_BH_SITE_RANK, 1 },
    { BW_BH_FLOWER_VEIL, BW_BH_SITE_TYPE, 1 },
    { BW_BH_CHEEK_POUCH, BW_BH_SITE_HP, 1 },
    { BW_BH_PARENTAL_BOND, BW_BH_SITE_RATIO, 1 }, { BW_BH_PARENTAL_BOND, BW_BH_SITE_KIND, 1 },
    { BW_BH_STANCE_CHANGE, BW_BH_SITE_FORM, 1 }, { BW_BH_STANCE_CHANGE, BW_BH_SITE_FORM, 2 },
    { BW_BH_GRASS_PELT, BW_BH_SITE_RATIO, 1 },
};

#define NAMED_ABILITY_KEY_COUNT ((int)(sizeof(k_named_ability_keys) / sizeof(k_named_ability_keys[0])))

static int named_ability_work(int id)
{
    switch (id) {
    case BW_BH_SWEET_VEIL:   return 0x03;   /* WORK_BLOCKED, WORK_TARGET: each veil its own pair */
    case BW_BH_AROMA_VEIL:   return 0x0C;
    case BW_BH_FLOWER_VEIL:  return 0x30;
    case BW_BH_CHEEK_POUCH:  return 0x01;   /* WORK_ATE_BERRY */
    case BW_BH_PARENTAL_BOND: return 0x01;  /* WORK_SECOND_HIT */
    }
    return 0;
}

static int work_mask(int type, int id)
{
    int i;

    if (type == BW_BH_ABILITY && id >= BW_BH_ABILITY_NAMED_FIRST) {
        return named_ability_work(id);
    }
    for (i = 0; i < WORK_COUNT; i++) {
        if (k_work[i].type == type && k_work[i].id == id) {
            return k_work[i].mask;
        }
    }
    return 0;
}

/* the keys of each kind of place */
static int site_of_key(int key)
{
    switch (key) {
    case BW_BH_KEY_STAT:
    case BW_BH_KEY_STAGES:  return BW_BH_SITE_RANK;
    case BW_BH_KEY_STATUS:  return BW_BH_SITE_SICK;
    case BW_BH_KEY_WEATHER: return BW_BH_SITE_WEATHER;
    case BW_BH_KEY_TURNS:   return BW_BH_SITE_TURNS;
    case BW_BH_KEY_DAMAGE:  return BW_BH_SITE_DAMAGE;
    case BW_BH_KEY_POWER:   return BW_BH_SITE_POWER;
    case BW_BH_KEY_ACCURACY: return BW_BH_SITE_ACCURACY;
    case BW_BH_KEY_CHANCE:  return BW_BH_SITE_CHANCE;
    case BW_BH_KEY_HP:      return BW_BH_SITE_HP;
    case BW_BH_KEY_RATIO:   return BW_BH_SITE_RATIO;
    case BW_BH_KEY_TYPE:    return BW_BH_SITE_TYPE;
    case BW_BH_KEY_KIND:    return BW_BH_SITE_KIND;
    case BW_BH_KEY_PRIORITY: return BW_BH_SITE_PRIORITY;
    case BW_BH_KEY_FORM:    return BW_BH_SITE_FORM;
    }
    return -1;
}

/* The index of the place (type, id, handler, site), 0 when it is not one. */
static int site_index(int type, int id, int handler, int site)
{
    int i;

    if (type == BW_BH_MOVE && id >= BW_BH_NAMED_FIRST) {
        for (i = 0; i < NAMED_SITE_COUNT; i++) {
            if (k_named_sites[i].id == id && k_named_sites[i].handler == handler && k_named_sites[i].site == site) {
                return k_named_sites[i].index;
            }
        }
        return 0;
    }
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

    if (type == BW_BH_ABILITY && id >= BW_BH_ABILITY_NAMED_FIRST) {
        for (i = 0; i < NAMED_ABILITY_KEY_COUNT; i++) {
            if (k_named_ability_keys[i].id == id && k_named_ability_keys[i].site == site
                && k_named_ability_keys[i].index == index) {
                return 1;
            }
        }
        return 0;
    }
    if (type == BW_BH_MOVE && id >= BW_BH_NAMED_FIRST) {
        for (i = 0; i < NAMED_SITE_COUNT; i++) {
            if (k_named_sites[i].id == id && k_named_sites[i].site == site && k_named_sites[i].index == index) {
                return 1;
            }
        }
        return 0;
    }
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

/* The built-in end of a base: followed through lines of one base (`b =
 * c`); -1 for a chain that comes back, or a line of several bases on the
 * way (a base is one id's handlers). */
static int chain_end(int type, int id)
{
    int hops;
    Entry *e;

    for (hops = 0; hops < MAX_CHAIN; hops++) {
        e = find(type, id);
        if (e == NULL || (e->nparts == 1 && e->parts[0].base == id)) {
            return id;
        }
        if (e->nparts != 1) {
            return -1;
        }
        id = e->parts[0].base;
    }
    return -1;
}

/* The built-in end of base `base` on the line of `id`: a part that names
 * the line's own id is the built-in one (`22 = 22 stages=-2 + 2` tunes
 * Intimidate and adds Drizzle), as on a line of one base. */
static int part_end(int type, int id, int base)
{
    return base == id ? id : chain_end(type, base);
}

static const char *type_name(int type)
{
    return type == BW_BH_ABILITY ? "ability" : type == BW_BH_ITEM ? "item" : "move";
}

int bw_bh_parts(int type, int id)
{
    Entry *e = find(type, id);

    return e != NULL ? e->nparts : 1;
}

int bw_bh_part_base(int type, int id, int part)
{
    Entry *e = find(type, id);
    int end;

    if (e == NULL) {
        return id;
    }
    if (part < 0 || part >= e->nparts) {
        return 0;
    }
    if (e->nparts == 1) {
        end = chain_end(type, id);
        return end < 0 ? id : end;
    }
    end = part_end(type, id, e->parts[part].base);
    return end < 0 ? 0 : end;
}

int bw_bh_base(int type, int id)
{
    return bw_bh_part_base(type, id, 0);
}

/* The built-in move an eidos base counts as where the game tests a move by
 * its id: the guards are Protect to the streak, the AI and the lists of
 * moves Metronome, Assist and the like leave alone (as the later games list
 * Spiky Shield, King's Shield and Baneful Bunker with Protect).  0 for none. */
static int named_counts_as(int base)
{
    switch (base) {
    case BW_BH_SPIKY_SHIELD:
    case BW_BH_KINGS_SHIELD:
    case BW_BH_BANEFUL_BUNKER:
    case BW_BH_CONTACT_GUARD:
        return 182;         /* Protect */
    }
    return 0;
}

int bw_bh_move_is(int move, int builtin)
{
    int k, n, b;

    if (g_count == 0 || find(BW_BH_MOVE, move) == NULL) {
        return move == builtin;
    }
    n = bw_bh_parts(BW_BH_MOVE, move);
    for (k = 0; k < n; k++) {
        b = bw_bh_part_base(BW_BH_MOVE, move, k);
        if (b == builtin || (b >= BW_BH_NAMED_FIRST && named_counts_as(b) != 0 && named_counts_as(b) == builtin)) {
            return 1;
        }
    }
    return 0;
}

int bw_bh_move_named(int move, int named, int key, int index, int *value)
{
    int k, n;

    if (g_count == 0 || find(BW_BH_MOVE, move) == NULL) {
        return 0;
    }
    n = bw_bh_parts(BW_BH_MOVE, move);
    for (k = 0; k < n; k++) {
        if (bw_bh_part_base(BW_BH_MOVE, move, k) == named) {
            if (key >= 0 && value != NULL) {
                (void)bw_bh_part_key(BW_BH_MOVE, move, k, key, index, value);
            }
            return 1;
        }
    }
    return 0;
}

int bw_bh_move_in(int move, const u16 *list, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (bw_bh_move_is(move, list[i])) {
            return 1;
        }
    }
    return 0;
}

/* With EIDOS_ABILITY_LOG=1 (docs/test-switches.md), said once a run for
 * each pair: the log shows a test by id took a mod's ability as the
 * built-in one. */
static void said_ability_is(int ability, int builtin)
{
    static int seen[32][2];
    static int nseen;
    static int on = -1;
    int i;

    if (on < 0) {
        on = getenv("EIDOS_ABILITY_LOG") != NULL;
    }
    if (!on || ability == builtin) {
        return;
    }
    for (i = 0; i < nseen; i++) {
        if (seen[i][0] == ability && seen[i][1] == builtin) {
            return;
        }
    }
    if (nseen < 32) {
        seen[nseen][0] = ability;
        seen[nseen][1] = builtin;
        nseen++;
    }
    fprintf(stderr, "eidos: battle tables: ability %d is taken as %d where the game tests that id\n", ability, builtin);
}

/* EIDOS_DEBUG_LAUNCHER=ITEM (docs/test-switches.md), a test of the Wonder
 * Launcher's items outside a link battle: the item every Bag action uses
 * (0: the switch is off).  stubs/ov/patch_ov093_btl_action_word_ability.c
 * and patch_ov093_btl_server_flow_ability.c read it. */
int bw_bh_test_launcher_item(void)
{
    static int item = -1;

    if (item < 0) {
        const char *e = getenv("EIDOS_DEBUG_LAUNCHER");

        item = e != NULL ? atoi(e) : 0;
        if (item < 0 || item > 0xFFFF) {
            item = 0;
        }
    }
    return item;
}

int bw_bh_ability_is(int ability, int builtin)
{
    int k, n;

    if (g_count == 0 || find(BW_BH_ABILITY, ability) == NULL) {
        return ability == builtin;
    }
    n = bw_bh_parts(BW_BH_ABILITY, ability);
    for (k = 0; k < n; k++) {
        if (bw_bh_part_base(BW_BH_ABILITY, ability, k) == builtin) {
            said_ability_is(ability, builtin);
            return 1;
        }
    }
    return 0;
}

int bw_bh_ability_in(int ability, const u16 *list, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (bw_bh_ability_is(ability, list[i])) {
            return 1;
        }
    }
    return 0;
}

/* After every mod's lines are read: a line whose bases come back to it,
 * that names one base twice, or whose bases' handlers keep state in the
 * same factor work slot is said and left out; so is a key a base's
 * handlers have no place for. */
static void check_lines(void)
{
    int i, k, j;

    for (i = 0; i < g_count; i++) {
        Entry *e = &g_entries[i];
        int bases[MAX_PARTS], masks = 0, bad = 0;

        for (k = 0; k < e->nparts && !bad; k++) {
            bases[k] = e->nparts == 1 ? chain_end(e->type, e->id) : part_end(e->type, e->id, e->parts[k].base);
            if (bases[k] < 0) {
                fprintf(stderr, "eidos: battle tables: %s %d: base %d comes back to it or has several bases "
                        "itself; the line is left out\n", type_name(e->type), e->id, e->parts[k].base);
                bad = 1;
                break;
            }
            for (j = 0; j < k; j++) {
                if (bases[j] == bases[k]) {
                    fprintf(stderr, "eidos: battle tables: %s %d: base %d twice; the line is left out\n",
                            type_name(e->type), e->id, bases[k]);
                    bad = 1;
                }
            }
            if (!bad && (masks & work_mask(e->type, bases[k])) != 0) {
                fprintf(stderr, "eidos: battle tables: %s %d: base %d keeps state in a factor slot another "
                        "base of the line uses (handler_params.json, \"work\"); the line is left out\n",
                        type_name(e->type), e->id, bases[k]);
                bad = 1;
            }
            masks |= work_mask(e->type, bases[k]);
        }
        if (bad) {
            free(e->joined);
            g_entries[i] = g_entries[--g_count];
            i--;
            continue;
        }
        for (k = 0; k < e->nparts; k++) {
            Part *pt = &e->parts[k];

            for (j = 0; j < pt->nparams; j++) {
                int site = site_of_key(pt->params[j].key);

                if (!site_exists(e->type, bases[k], site, pt->params[j].index)) {
                    char base[24];

                    if (bw_bh_named_name(bases[k]) != NULL) {
                        snprintf(base, sizeof base, "%s", bw_bh_named_name(bases[k]));
                    } else if (e->type == BW_BH_ABILITY && bw_bh_ability_named_name(bases[k]) != NULL) {
                        snprintf(base, sizeof base, "%s", bw_bh_ability_named_name(bases[k]));
                    } else {
                        snprintf(base, sizeof base, "%d", bases[k]);
                    }
                    fprintf(stderr, "eidos: battle tables: %s %d (works like %s): %s%s%d has no place in its "
                            "handlers (handler_params.json lists the keys); left out\n", type_name(e->type),
                            e->id, base, k_keys[pt->params[j].key], pt->params[j].index > 1 ? "." : " ",
                            pt->params[j].index);
                    pt->params[j] = pt->params[--pt->nparams];
                    j--;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The lines eidos's bases show                                        */
/* ------------------------------------------------------------------ */

extern int bw_mod_text_get(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
extern int bw_mod_text_set_for(const char *root, u32 archive, u32 file, u32 entry, u32 block,
                               const u16 *units, u32 count);

/* `@` is the Pokemon's name (control code F000, kind 0x0102, argument 0),
 * `#` a type's and `$` a move's name (kinds 0x0103 and 0x0107, argument 1),
 * `|` a line break (0xFFFE), anything else UTF-8 (the Western games' text
 * is Unicode: é 0xE9, ¡ 0xA1).  In French, German, Italian and Spanish a
 * line is the game's own line of the same shape (its bank and row in the
 * comment) with the move's name as `$`, filled from bank 203 at run time,
 * so a mod's name for its move (203.<lang>.msg) shows.  The name is
 * swapped only where the game's line holds a move's bare name: a line whose
 * article or agreement follows the name, or that names a common noun (the
 * mist, la gravità), has no such line, and the language gets NULL (none:
 * the handler shows nothing, bw_bh_line_ok).  English names moves with `$`
 * too and keeps the later games' wording where it names none. */
typedef struct NamedLine {
    int         base;
    u32         bank, line;
    const char *text[5];        /* English, French, German, Italian, Spanish */
} NamedLine;

static const NamedLine k_named_lines[] = {
    /* Misty Terrain's start: Gravity's (bank 15 row 117); only German
     * names it bare ("Erdanziehung wurde verstärkt!"); its end (row 118)
     * has an article or a common noun in every language */
    { BW_BH_MISTY_TERRAIN, BW_BH_LINE_BANK_STD, BW_BH_LINE_MISTY_START, {
        "Mist swirled about the battlefield!", NULL, "$ wurde verstärkt!", NULL, NULL } },
    { BW_BH_MISTY_TERRAIN, BW_BH_LINE_BANK_STD, BW_BH_LINE_MISTY_END, {
        "The mist disappeared|from the battlefield.", NULL, NULL, NULL, NULL } },
    /* Fairy Lock: the game has no line for the whole field that says no
     * one can flee (Mean Look's, bank 14 rows 872..877, is one Pokemon's) */
    { BW_BH_FAIRY_LOCK, BW_BH_LINE_BANK_STD, BW_BH_LINE_FAIRY_LOCK, {
        "No one will be able to run away|during the next turn!", NULL, NULL, NULL, NULL } },
    /* Geomancy's charge: Charge's (bank 14 rows 664..666) */
    { BW_BH_GEOMANCY, BW_BH_LINE_BANK_SET, BW_BH_LINE_GEOMANCY, {
        "@ is absorbing|power!",
        "@ se met à charger son énergie!",
        "@ lädt sich auf!",
        "@ si carica!",
        "¡@ carga energía!" } },
    { BW_BH_GEOMANCY, BW_BH_LINE_BANK_SET, BW_BH_LINE_GEOMANCY + 1, {
        "The wild @ is absorbing|power!",
        "Le @ sauvage se met à charger\nson énergie!",
        "@ (Wild) lädt sich auf!",
        "@ selvatico si carica!",
        "¡El @ salvaje carga energía!" } },
    { BW_BH_GEOMANCY, BW_BH_LINE_BANK_SET, BW_BH_LINE_GEOMANCY + 2, {
        "The foe's @ is absorbing|power!",
        "Le @ ennemi se met à charger\nson énergie!",
        "@ (Gegner) lädt sich auf!",
        "@ nemico si carica!",
        "¡El @ enemigo carga energía!" } },
    /* the terrains' notices (a status or move held off): Wide Guard's
     * (bank 14 rows 797..799); English: the later games' Misty line */
#define GUARD_NOTICE(base, line, en0, en1, en2)                                                                 \
    { base, BW_BH_LINE_BANK_SET, line, { en0, "@ est protégé par $!", "@ wird durch\n$ geschützt!",              \
                                         "@ è protetto da $!", "¡@ está protegido\npor $!" } },                 \
    { base, BW_BH_LINE_BANK_SET, line + 1, { en1, "Le @ sauvage est protégé\npar $!",                            \
                                             "@ (Wild) wird durch\n$ geschützt!", "@ selvatico è protetto da\n$!", \
                                             "¡El @ salvaje está protegido\npor $!" } },                          \
    { base, BW_BH_LINE_BANK_SET, line + 2, { en2, "Le @ ennemi est protégé\npar $!",                             \
                                             "@ (Gegner) wird durch\n$ geschützt!", "@ nemico è protetto da $!",  \
                                             "¡El @ enemigo está protegido\npor $!" } }
    GUARD_NOTICE(BW_BH_MISTY_TERRAIN, BW_BH_LINE_MISTY_BLOCK, "@ surrounds itself|with a protective mist!",
                 "The wild @ surrounds itself|with a protective mist!",
                 "The foe's @ surrounds itself|with a protective mist!"),
    /* Crafty Shield: Quick Guard's (bank 15 rows 162, 163; bank 14 rows
     * 800..802) */
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_START, {
        "$ protected|your team!",
        "Votre équipe est protégée par $!",
        "Die Pokémon auf deiner Seite werden\nvon $ behütet!",
        "La tua squadra è protetta da $!",
        "¡$ protege a tu equipo!" } },
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_START + 1, {
        "$ protected|the foe's team!",
        "L'équipe ennemie est protégée par\n$!",
        "Die Pokémon auf der gegnerischen Seite\nwerden von $ behütet!",
        "La squadra avversaria è protetta da\n$!",
        "¡$ protege al equipo rival!" } },
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_START + 2, {
        "$ protected|the foe's team!",
        "L'équipe ennemie est protégée par\n$!",
        "Die Pokémon auf der gegnerischen Seite\nwerden von $ behütet!",
        "La squadra avversaria è protetta da\n$!",
        "¡$ protege al equipo rival!" } },
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_BLOCK, {
        "$ protected|@!",
        "@ est protégé par $!",
        "@ wird durch\n$ beschützt!",
        "@ è protetto da $!",
        "¡@ está protegido por $!" } },
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_BLOCK + 1, {
        "$ protected|the wild @!",
        "Le @ sauvage est protégé\npar $!",
        "@ (Wild) wird durch\n$ beschützt!",
        "@ selvatico è protetto da\n$!",
        "¡El @ salvaje está protegido\npor $!" } },
    { BW_BH_CRAFTY_SHIELD, BW_BH_LINE_BANK_SET, BW_BH_LINE_CRAFTY_BLOCK + 2, {
        "$ protected|the foe's @!",
        "Le @ ennemi est protégé\npar $!",
        "@ (Gegner) wird durch\n$ beschützt!",
        "@ nemico è protetto da $!",
        "¡El @ enemigo está protegido\npor $!" } },
    /* The later moves' lines, by the same rule.  The terrains' start and
     * end: Misty's (Gravity's, German start only); the other notices: Wide
     * Guard's.  NULL: the move shows the game's own lines only (a stat
     * change), and none of its own. */
#define EN_ONLY(s) { s, NULL, NULL, NULL, NULL }
#define TERRAIN_LINES(base, startLine, endLine, en_start, en_end)                                                \
    { base, BW_BH_LINE_BANK_STD, startLine, { en_start, NULL, "$ wurde verstärkt!", NULL, NULL } },              \
    { base, BW_BH_LINE_BANK_STD, endLine, EN_ONLY(en_end) }
    TERRAIN_LINES(BW_BH_GRASSY_TERRAIN, BW_BH_LINE_GRASSY_START, BW_BH_LINE_GRASSY_END,
                  "Grass grew to cover|the battlefield!", "The grass disappeared|from the battlefield."),
    TERRAIN_LINES(BW_BH_ELECTRIC_TERRAIN, BW_BH_LINE_ELECTRIC_START, BW_BH_LINE_ELECTRIC_END,
                  "An electric current runs|across the battlefield!", "The electricity disappeared|from the battlefield."),
    TERRAIN_LINES(BW_BH_PSYCHIC_TERRAIN, BW_BH_LINE_PSYCHIC_START, BW_BH_LINE_PSYCHIC_END,
                  "The battlefield got weird!", "The weirdness disappeared|from the battlefield!"),
    GUARD_NOTICE(BW_BH_ELECTRIC_TERRAIN, BW_BH_LINE_ELECTRIC_BLOCK, "$ protected|@!", "$ protected|the wild @!",
                 "$ protected|the foe's @!"),
    GUARD_NOTICE(BW_BH_PSYCHIC_TERRAIN, BW_BH_LINE_PSYCHIC_BLOCK, "$ protected|@!", "$ protected|the wild @!",
                 "$ protected|the foe's @!"),
    /* Topsy-Turvy, Burn Up, Electrify: no game line of their shape */
    { BW_BH_TOPSY_TURVY, BW_BH_LINE_BANK_SET, BW_BH_LINE_TOPSY_TURVY,
      EN_ONLY("@'s stat changes were|all reversed!") },
    { BW_BH_TOPSY_TURVY, BW_BH_LINE_BANK_SET, BW_BH_LINE_TOPSY_TURVY + 1,
      EN_ONLY("The wild @'s stat changes|were all reversed!") },
    { BW_BH_TOPSY_TURVY, BW_BH_LINE_BANK_SET, BW_BH_LINE_TOPSY_TURVY + 2,
      EN_ONLY("The foe's @'s stat changes|were all reversed!") },
    { BW_BH_BURN_UP, BW_BH_LINE_BANK_SET, BW_BH_LINE_BURN_UP,
      EN_ONLY("@ burned itself out!") },
    { BW_BH_BURN_UP, BW_BH_LINE_BANK_SET, BW_BH_LINE_BURN_UP + 1,
      EN_ONLY("The wild @ burned itself out!") },
    { BW_BH_BURN_UP, BW_BH_LINE_BANK_SET, BW_BH_LINE_BURN_UP + 2,
      EN_ONLY("The foe's @ burned itself out!") },
    /* Ion Deluge: Gravity's start line (bank 15 row 117, German only) */
    { BW_BH_ION_DELUGE, BW_BH_LINE_BANK_STD, BW_BH_LINE_ION_DELUGE, {
        "A deluge of ions showers|the battlefield!", NULL, "$ wurde verstärkt!", NULL, NULL } },
    { BW_BH_ELECTRIFY, BW_BH_LINE_BANK_SET, BW_BH_LINE_ELECTRIFY,
      EN_ONLY("@'s moves have|been electrified!") },
    { BW_BH_ELECTRIFY, BW_BH_LINE_BANK_SET, BW_BH_LINE_ELECTRIFY + 1,
      EN_ONLY("The wild @'s moves have|been electrified!") },
    { BW_BH_ELECTRIFY, BW_BH_LINE_BANK_SET, BW_BH_LINE_ELECTRIFY + 2,
      EN_ONLY("The foe's @'s moves have|been electrified!") },
    /* Sticky Web: laid, Spikes' (bank 15 rows 148, 149: only the Italian
     * one names the move bare, "trappola di Punte"); gone, Spikes' (rows
     * 150, 151: French "plus de Picots", Italian); a Pokemon caught: no
     * line of the shape (the Speed line shows) */
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_STD, BW_BH_LINE_STICKY_WEB_US, {
        "A sticky web has been laid out|beneath your team's feet!", NULL, NULL,
        "Ai piedi della tua squadra c'è una trappola\ndi $!", NULL } },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_STD, BW_BH_LINE_STICKY_WEB_FOE, {
        "A sticky web has been laid out|beneath the foe's team's feet!", NULL, NULL,
        "Ai piedi della squadra nemica c'è una\ntrappola di $!", NULL } },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_STD, BW_BH_LINE_STICKY_WEB_GONE_US, {
        "The sticky web disappeared from|around your team's feet!",
        "Il n'y a plus de $ autour de votre\néquipe!", NULL,
        "Ai piedi della tua squadra non c'è più\nla trappola di $!", NULL } },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_STD, BW_BH_LINE_STICKY_WEB_GONE_FOE, {
        "The sticky web disappeared from|around the foe's team's feet!",
        "Il n'y a plus de $ autour de l'équipe\nennemie!", NULL,
        "Ai piedi della squadra nemica non c'è più\nla trappola di $!", NULL } },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_SET, BW_BH_LINE_STICKY_WEB_CAUGHT,
      EN_ONLY("@ was caught|in a sticky web!") },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_SET, BW_BH_LINE_STICKY_WEB_CAUGHT + 1,
      EN_ONLY("The wild @ was caught|in a sticky web!") },
    { BW_BH_STICKY_WEB, BW_BH_LINE_BANK_SET, BW_BH_LINE_STICKY_WEB_CAUGHT + 2,
      EN_ONLY("The foe's @ was caught|in a sticky web!") },
    /* add_type (`#` the type): the game's only line that names a type
     * is Soak's (bank 14 line 896), and only the French one ("prend le
     * type") says a type was taken, not swapped for; English keeps the
     * later games' wording, the others have none */
#define ADD_TYPE_LINE(base)                                                                                       \
    { base, BW_BH_LINE_BANK_SET, BW_BH_LINE_ADD_TYPE, { "The # type was added|to @!", "@ prend le type #!",         \
                                                        NULL, NULL, NULL } },                                     \
    { base, BW_BH_LINE_BANK_SET, BW_BH_LINE_ADD_TYPE + 1, { "The # type was added|to the wild @!",                 \
                                                            "Le @ sauvage prend\nle type #!", NULL, NULL, NULL } }, \
    { base, BW_BH_LINE_BANK_SET, BW_BH_LINE_ADD_TYPE + 2, { "The # type was added|to the foe's @!",                \
                                                            "Le @ ennemi prend\nle type #!", NULL, NULL, NULL } }
    ADD_TYPE_LINE(BW_BH_ADD_TYPE),
    ADD_TYPE_LINE(BW_BH_TRICK_OR_TREAT),
    ADD_TYPE_LINE(BW_BH_FORESTS_CURSE),
#undef ADD_TYPE_LINE
    /* Powder, Laser Focus, Spectral Thief, Belch: no game line of their
     * shape */
#define SET_EN(base, line, en0, en1, en2)                                                                \
    { base, BW_BH_LINE_BANK_SET, line, EN_ONLY(en0) }, { base, BW_BH_LINE_BANK_SET, line + 1, EN_ONLY(en1) }, \
    { base, BW_BH_LINE_BANK_SET, line + 2, EN_ONLY(en2) }
    SET_EN(BW_BH_POWDER, BW_BH_LINE_POWDER, "@ is covered|in powder!", "The wild @ is covered|in powder!",
           "The foe's @ is covered|in powder!"),
    { BW_BH_POWDER, BW_BH_LINE_BANK_STD, BW_BH_LINE_POWDER_BLAST,
      EN_ONLY("When the flame touched the powder|on the Pokémon, it exploded!") },
    /* Throat Chop (`$` the move it holds back): the game's "cannot use"
     * (bank 14 rows 905..907), which names no cause */
    { BW_BH_THROAT_CHOP, BW_BH_LINE_BANK_SET, BW_BH_LINE_THROAT_CHOP, {
        "@|cannot use $!",
        "Impossible pour @ d'utiliser la\ncapacité $!",
        "@ kann\n$ nicht einsetzen!",
        "@ non può utilizzare\n$!",
        "¡@ no puede usar $!" } },
    { BW_BH_THROAT_CHOP, BW_BH_LINE_BANK_SET, BW_BH_LINE_THROAT_CHOP + 1, {
        "The wild @|cannot use $!",
        "Impossible pour le @ sauvage\nd'utiliser la capacité $!",
        "@ (Wild) kann\n$ nicht einsetzen!",
        "@ selvatico non può utilizzare\n$!",
        "¡El @ salvaje no puede usar\n$!" } },
    { BW_BH_THROAT_CHOP, BW_BH_LINE_BANK_SET, BW_BH_LINE_THROAT_CHOP + 2, {
        "The foe's @|cannot use $!",
        "Impossible pour le @ ennemi\nd'utiliser la capacité $!",
        "@ (Gegner) kann\n$ nicht einsetzen!",
        "@ nemico non può utilizzare\n$!",
        "¡El @ enemigo no puede usar\n$!" } },
    SET_EN(BW_BH_LASER_FOCUS, BW_BH_LINE_LASER_FOCUS, "@ concentrated|intensely!",
           "The wild @ concentrated|intensely!", "The foe's @ concentrated|intensely!"),
    SET_EN(BW_BH_SPECTRAL_THIEF, BW_BH_LINE_SPECTRAL_THIEF, "@ stole the target's|boosted stats!",
           "The wild @ stole the target's|boosted stats!", "The foe's @ stole the target's|boosted stats!"),
    SET_EN(BW_BH_BELCH, BW_BH_LINE_BELCH, "@ hasn't eaten any held item,|so it can't possibly belch!",
           "The wild @ hasn't eaten any held item,|so it can't possibly belch!",
           "The foe's @ hasn't eaten any held item,|so it can't possibly belch!"),
    SET_EN(BW_BH_NEEDS_BERRY, BW_BH_LINE_BELCH, "@ hasn't eaten any held item,|so it can't possibly belch!",
           "The wild @ hasn't eaten any held item,|so it can't possibly belch!",
           "The foe's @ hasn't eaten any held item,|so it can't possibly belch!"),
#undef SET_EN
#undef TERRAIN_LINES
#undef GUARD_NOTICE
#undef EN_ONLY
};

#define NAMED_LINE_COUNT ((int)(sizeof(k_named_lines) / sizeof(k_named_lines[0])))

extern u32 bw_mod_text_language(void);

/* The column of the game's language (src/game/version.h: 2 English, 3
 * French, 4 Italian, 5 German, 7 Spanish); English for any other. */
static int line_column(void)
{
    switch (bw_mod_text_language()) {
    case 3: return 1;
    case 5: return 2;
    case 4: return 3;
    case 7: return 4;
    }
    return 0;
}

static u32 line_units(const char *s, u16 *out, u32 max)
{
    const unsigned char *p = (const unsigned char *)s;
    u32 n = 0;

    while (*p != '\0' && n + 4 <= max) {
        if (*p == '@' || *p == '#' || *p == '$') {
            out[n++] = 0xF000;                                      /* a tag with one argument: */
            out[n++] = *p == '@' ? 0x0102 : *p == '#' ? 0x0103 : 0x0107; /* a Pokemon, a type, a move */
            out[n++] = 0x0001;
            out[n++] = *p == '@' ? 0x0000 : 0x0001;                 /* the message's argument 0 or 1 */
            p++;
        } else if (*p == '|' || *p == '\n') {
            out[n++] = 0xFFFE;
            p++;
        } else if (*p < 0x80) {
            out[n++] = *p++;
        } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            out[n++] = (u16)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            out[n++] = (u16)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
            p += 3;
        } else {
            p++;                        /* (not UTF-8 the table writes) */
        }
    }
    return n;
}

/* Which of k_named_lines a mod or eidos gave the game's language
 * (add_named_lines). */
static u8 g_named_line_ok[NAMED_LINE_COUNT];

int bw_bh_line_ok(u32 bank, u32 line)
{
    int j;

    for (j = 0; j < NAMED_LINE_COUNT; j++) {
        if (k_named_lines[j].bank == bank && k_named_lines[j].line == line && g_named_line_ok[j]) {
            return 1;
        }
    }
    return 0;
}

/* Every line an eidos base of a mod's line shows: put in for that mod,
 * unless some mod already has a line there (its own wording wins). */
static void add_named_lines(void)
{
    int i, k, j;

    for (j = 0; j < NAMED_LINE_COUNT; j++) {
        const NamedLine *l = &k_named_lines[j];
        int root = -1;
        u16 units[96];
        u32 n;

        g_named_line_ok[j] = 0;
        for (i = 0; i < g_count && root < 0; i++) {
            for (k = 0; k < g_entries[i].nparts; k++) {
                if (g_entries[i].type == BW_BH_MOVE && g_entries[i].parts[k].base == l->base &&
                    g_entries[i].root >= 0) {
                    root = g_entries[i].root;
                    break;
                }
            }
        }
        if (root < 0) {
            continue;
        }
        if (bw_mod_text_get(2, l->bank, l->line, 0, units, 96) > 0) {
            g_named_line_ok[j] = 1;     /* (a mod's own line) */
            continue;
        }
        if (l->text[line_column()] == NULL) {
            continue;                   /* (no game line of the shape in this language) */
        }
        n = line_units(l->text[line_column()], units, 96);
        if (bw_mod_text_set_for(bw_mod_root_at((u32)root), 2, l->bank, l->line, 0, units, n) != 0) {
            fprintf(stderr, "eidos: battle tables: %s: bank %u line %u could not be added\n",
                    bw_bh_named_name(l->base), (unsigned)l->bank, (unsigned)l->line);
        } else {
            g_named_line_ok[j] = 1;
        }
    }
}

void bw_bh_load(void)
{
    u32 m;

    bw_bh_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);

        g_loading_root = (int)m;
        load_file(root, "ability_handlers.txt", BW_BH_ABILITY);
        load_file(root, "move_handlers.txt", BW_BH_MOVE);
        load_file(root, "item_handlers.txt", BW_BH_ITEM);
    }
    g_loading_root = -1;
    check_lines();
    add_named_lines();
    bw_move_kinds_load();           /* (the kinds eidos's ability bases test) */
}

void bw_bh_check(void)
{
    check_lines();
}

int bw_bh_part_key(int type, int id, int part, int key, int index, int *value)
{
    Entry *e = find(type, id);
    int i;

    if (e == NULL || part < 0 || part >= e->nparts) {
        return 0;
    }
    for (i = 0; i < e->parts[part].nparams; i++) {
        if (e->parts[part].params[i].key == key && e->parts[part].params[i].index == index) {
            *value = e->parts[part].params[i].value;
            return 1;
        }
    }
    return 0;
}

int bw_bh_key(int type, int id, int key, int index, int *value)
{
    return bw_bh_part_key(type, id, 0, key, index, value);
}

const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                       u32 *total)
{
    Entry *e = find(type, id);
    u32 rows = 0, at = 0;
    int k;
    unsigned char *out;

    if (e == NULL || n != e->nparts || n < 1) {
        return NULL;
    }
    if (e->joined != NULL) {
        *total = e->joined_total;
        return e->joined;
    }
    for (k = 0; k < n; k++) {
        rows += counts[k];
    }
    if (rows == 0 || (out = (unsigned char *)malloc((size_t)rows * row_size)) == NULL) {
        return NULL;
    }
    for (k = 0; k < n; k++) {
        if (counts[k] > 0) {
            memcpy(out + (size_t)at * row_size, tables[k], (size_t)counts[k] * row_size);
        }
        e->part_rows[k] = counts[k];
        at += counts[k];
    }
    e->joined = out;
    e->joined_total = rows;
    *total = rows;
    return out;
}

/* ------------------------------------------------------------------ */
/* The running handler                                                 */
/* ------------------------------------------------------------------ */

typedef struct Frame {
    int type, id;
    int part, row;          /* the part its row came from, the row in that base's table */
    unsigned used;          /* the kinds of place already met in this call */
} Frame;

static Frame g_stack[MAX_DEPTH];
static int   g_depth, g_lost;   /* (g_lost: calls past MAX_DEPTH, not kept) */

int bw_bh_row_end(int type, int id, int row)
{
    Entry *e;
    int k, end = 0;

    if (g_count == 0 || (e = find(type, id)) == NULL || e->nparts < 2 || e->joined == NULL) {
        return 0;
    }
    for (k = 0; k < e->nparts; k++) {
        end += (int)e->part_rows[k];
        if (row < end) {
            return end;
        }
    }
    return 0;
}

/* The part of a joined table that holds `row` (0 for a factor of one base). */
static int part_of_row(const Entry *e, int row)
{
    int k;

    if (e->nparts < 2 || e->joined == NULL) {
        return 0;
    }
    for (k = 0; k < e->nparts - 1 && row >= (int)e->part_rows[k]; k++) {
        row -= (int)e->part_rows[k];
    }
    return k;
}

int bw_bh_running_base(int type, int id)
{
    const Frame *f;

    if (g_count == 0 || g_depth == 0 || g_lost != 0) {
        return bw_bh_base(type, id);
    }
    f = &g_stack[g_depth - 1];
    if (f->type != type || f->id != id) {
        return bw_bh_base(type, id);
    }
    return bw_bh_part_base(type, id, f->part);
}

static int g_check_type = -1, g_check_id, g_check_row;

void bw_bh_checking(int type, int id, int row)
{
    g_check_type = type;
    g_check_id = id;
    g_check_row = row;
}

int bw_bh_checked_base(int type, int id)
{
    Entry *e;

    if (g_count == 0 || (e = find(type, id)) == NULL) {
        return id;
    }
    if (type != g_check_type || id != g_check_id) {
        return bw_bh_base(type, id);
    }
    return bw_bh_part_base(type, id, part_of_row(e, g_check_row));
}

void bw_bh_enter(int type, int id, int handler)
{
    Frame *f;
    Entry *e;

    if (g_depth == MAX_DEPTH) {
        g_lost++;
        return;
    }
    f = &g_stack[g_depth++];
    f->type = type;
    f->id = id;
    f->part = 0;
    f->row = handler;
    f->used = 0;
    /* a joined table: which base's rows the handler is in */
    if (g_count > 0 && (e = find(type, id)) != NULL && e->nparts > 1 && e->joined != NULL) {
        int k;

        for (k = 0; k < e->nparts && f->row >= (int)e->part_rows[k]; k++) {
            f->row -= (int)e->part_rows[k];
        }
        f->part = k < e->nparts ? k : e->nparts - 1;
    }
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
    return site_index(f->type, bw_bh_part_base(f->type, f->id, f->part), f->row, site);
}

static int frame_key(const Frame *f, int key, int index, int *value)
{
    return bw_bh_part_key(f->type, f->id, f->part, key, index, value);
}

int bw_bh_arg(const char *key, int index, int fallback)
{
    const Frame *f;
    int k, v;

    if (g_count == 0 || g_depth == 0 || g_lost != 0) {
        return fallback;
    }
    for (k = 0; k < BW_BH_KEY_COUNT && strcmp(k_keys[k], key) != 0; k++) {
    }
    f = &g_stack[g_depth - 1];
    return k < BW_BH_KEY_COUNT && frame_key(f, k, index, &v) ? v : fallback;
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
    if (frame_key(f, BW_BH_KEY_STAT, n, &v)) {
        said(f, "stat", (long)*stat, v);
        *stat = (u32)v;
    }
    if (frame_key(f, BW_BH_KEY_STAGES, n, &v)) {
        said(f, "stages", *amount, v);
        *amount = (s8)v;
    }
}

int bw_bh_sick(u32 *kind)
{
    Frame *f;
    int n = reached(BW_BH_SITE_SICK, &f), v;

    if (n == 0 || !frame_key(f, BW_BH_KEY_STATUS, n, &v) || (u32)v == *kind) {
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

    if (n != 0 && frame_key(f, BW_BH_KEY_WEATHER, n, &v)) {
        said(f, "weather", *weather, v);
        *weather = (u8)v;
    }
}

static int one_value(int site, int key, int value)
{
    Frame *f;
    int n = reached(site, &f), v;

    if (n != 0 && frame_key(f, key, n, &v)) {
        said(f, k_keys[key], value, v);
        return v;
    }
    return value;
}

u32 bw_bh_chance(u32 percent)  { return (u32)one_value(BW_BH_SITE_CHANCE, BW_BH_KEY_CHANCE, (int)percent); }
u32 bw_bh_hp(u32 divisor)      { return (u32)one_value(BW_BH_SITE_HP, BW_BH_KEY_HP, (int)divisor); }
s32 bw_bh_ratio(s32 ratio)     { return one_value(BW_BH_SITE_RATIO, BW_BH_KEY_RATIO, ratio); }
int bw_bh_type(int type)       { return one_value(BW_BH_SITE_TYPE, BW_BH_KEY_TYPE, type); }
u32 bw_bh_turns(u32 turns)     { return (u32)one_value(BW_BH_SITE_TURNS, BW_BH_KEY_TURNS, (int)turns); }

/* the event variables a handler sets that have a key (hand_common_types.h, hand_waza_types.h).
 * Var 0x1F (aletheia's WAZAVAR_RANK_EFFECT_STAT) is the stat of a move's stat change at event 0x59
 * (BTL_SERVER_FLOW_QueryMoveRankEffectEvent): Ancient Power's handler, its only writer, sets 10,
 * every stat but accuracy and evasion.  It is no chance and keeps its value; the `chance` key of
 * that place is the move record's chance of the change, at the roll (bw_bh_move_chance). */
#define EVAR_SICK_CHANCE    0x26    /* HANDVAR_ADD_SICK_PERCENT */
#define EVAR_ACCURACY       0x2B
#define EVAR_POWER          0x30
#define EVAR_FIXED_DAMAGE   0x37
#define EVAR_WEATHER_TURNS  0x24

int bw_bh_set_var(int var, int value)
{
    if (g_count == 0) {
        return value;
    }
    switch (var) {
    case EVAR_FIXED_DAMAGE: return one_value(BW_BH_SITE_DAMAGE, BW_BH_KEY_DAMAGE, value);
    case EVAR_POWER:        return one_value(BW_BH_SITE_POWER, BW_BH_KEY_POWER, value);
    case EVAR_ACCURACY:     return one_value(BW_BH_SITE_ACCURACY, BW_BH_KEY_ACCURACY, value);
    case EVAR_WEATHER_TURNS: return one_value(BW_BH_SITE_TURNS, BW_BH_KEY_TURNS, value);
    case EVAR_SICK_CHANCE:  return one_value(BW_BH_SITE_CHANCE, BW_BH_KEY_CHANCE, value);
    }
    return value;
}

extern u32 WAZA_TOOL_GetParam(u32 move, u32 param) __attribute__((weak));

#define WAZA_PARAM_RANK_PERCENT 0x12    /* the record's chance of its first stat change */

/* The chance the server rolls for a move's added stat change (the move
 * record's, BTL_SERVER_FLOW_RollMoveAddedEffectChance): the `chance` key
 * of the part of the move's line whose base rolls one (a game move whose
 * record has the chance: Ancient Power, Silver Wind, Ominous Wind, the
 * only ones with a chance place there), else `percent`.  Another part's
 * `chance` (Tri Attack's status roll) is its own. */
int bw_bh_move_chance(int move, int percent)
{
    int k, v, base;

    if (g_count == 0 || WAZA_TOOL_GetParam == NULL) {
        return percent;
    }
    for (k = 0; k < bw_bh_parts(BW_BH_MOVE, move); k++) {
        base = bw_bh_part_base(BW_BH_MOVE, move, k);
        if (base > 0 && base < BW_BH_NAMED_FIRST && WAZA_TOOL_GetParam((u32)base, WAZA_PARAM_RANK_PERCENT) != 0 &&
            bw_bh_part_key(BW_BH_MOVE, move, k, BW_BH_KEY_CHANCE, 1, &v)) {
            return v;
        }
    }
    return percent;
}
