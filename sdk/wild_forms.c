/*
 * sdk/wild_forms.c -- the form of a wild encounter slot, from a mod's
 * table (wild_forms.h).
 *
 *     <table>.<slots>.<slot> = <form> [season=<season>]
 *
 * <table> is the encounter table (the a/1/2/6 member the zone names),
 * <slots> one of its seven slot groups (grass, doubleGrass, shakingGrass,
 * surf, surfRipple, fishing, fishingRipple, the order of the table and of
 * its encounter types), <slot> the slot in the group (0 to 11 for the grass
 * groups, 0 to 4 for the others, or * for every slot of the group), <form>
 * 0 to 255.  Without `season` the line holds in every season; `season` is
 * spring, summer, autumn, winter or 0 to 3 (GAME_DATA_GetSeason), for a
 * table of four seasons or one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"
#include "wild_forms.h"

/* The slot groups of a table (aletheia ov021 field_encount_types.h,
 * EncountData: ground[3][12], water[2][5], fishing[2][5]). */
static const struct {
    const char *name;
    u8 slots;
} k_groups[BW_WILD_TYPES] = {
    { "grass", 12 }, { "doubleGrass", 12 }, { "shakingGrass", 12 },
    { "surf", 5 }, { "surfRipple", 5 }, { "fishing", 5 }, { "fishingRipple", 5 },
};

static const char *const k_seasons[4] = { "spring", "summer", "autumn", "winter" };

#define SEASON_ANY 4u

typedef struct WildForm {
    u16 table;
    u8 season;          /* 0..3, SEASON_ANY for every season */
    u8 type;            /* the group */
    u8 slot;
    u8 form;
} WildForm;

static WildForm *g_forms;
static u32 g_count, g_cap;

static void say(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
}

static WildForm *find(u32 table, u32 season, u32 type, u32 slot)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        const WildForm *w = &g_forms[i];

        if (w->table == table && w->season == season && w->type == type && w->slot == slot) {
            return &g_forms[i];
        }
    }
    return NULL;
}

static int put(const WildForm *w)
{
    WildForm *have = find(w->table, w->season, w->type, w->slot);

    if (have != NULL) {
        *have = *w;                     /* a later mod's line wins */
        return 0;
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2 : 32;
        WildForm *grown = (WildForm *)realloc(g_forms, cap * sizeof *grown);

        if (grown == NULL) {
            return -1;
        }
        g_forms = grown;
        g_cap = cap;
    }
    g_forms[g_count++] = *w;
    return 0;
}

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

int bw_wild_forms_parse_line(const char *line, char *why, int why_size)
{
    const char *p = skip_space(line), *s;
    char *end;
    long table, slot = -1, form, season = SEASON_ANY;
    u32 type, n, i, first, last;
    WildForm w;

    if (*p == '\0' || *p == '#' || *p == '\r' || *p == '\n') {
        return 1;
    }
    table = strtol(p, &end, 10);
    if (end == p || *end != '.' || table < 0 || table > 0xFFFF) {
        say(why, why_size, "expected `<table>.<slots>.<slot> = <form> [season=<season>]`");
        return -1;
    }
    s = end + 1;
    for (p = s; *p != '\0' && *p != '.' && *p != ' ' && *p != '\t' && *p != '='; p++) {
    }
    n = (u32)(p - s);
    for (type = 0; type < BW_WILD_TYPES; type++) {
        if (strlen(k_groups[type].name) == n && strncmp(s, k_groups[type].name, n) == 0) {
            break;
        }
    }
    if (type == BW_WILD_TYPES) {
        say(why, why_size, "the slot groups are grass, doubleGrass, shakingGrass, surf, surfRipple, fishing and fishingRipple");
        return -1;
    }
    if (*p != '.') {
        say(why, why_size, "expected `.<slot>` after the slot group");
        return -1;
    }
    p++;
    if (*p == '*') {
        p++;
    } else {
        slot = strtol(p, &end, 10);
        if (end == p || slot < 0 || slot >= (long)k_groups[type].slots) {
            say(why, why_size, k_groups[type].slots == 12 ? "the slot is 0 to 11, or *" : "the slot is 0 to 4, or *");
            return -1;
        }
        p = end;
    }
    p = skip_space(p);
    if (*p != '=') {
        say(why, why_size, "expected `=` after the slot");
        return -1;
    }
    p = skip_space(p + 1);
    form = strtol(p, &end, 10);
    if (end == p || form < 0 || form > 255) {
        say(why, why_size, "the form is 0 to 255");
        return -1;
    }
    p = skip_space(end);
    if (strncmp(p, "season=", 7) == 0) {
        s = p + 7;
        for (p = s; *p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '#'; p++) {
        }
        n = (u32)(p - s);
        season = -1;
        for (i = 0; i < 4; i++) {
            if ((strlen(k_seasons[i]) == n && strncmp(s, k_seasons[i], n) == 0) || (n == 1 && *s == (char)('0' + i))) {
                season = (long)i;
            }
        }
        if (season < 0) {
            say(why, why_size, "the season is spring, summer, autumn, winter or 0 to 3");
            return -1;
        }
        p = skip_space(p);
    }
    if (*p != '\0' && *p != '#' && *p != '\r' && *p != '\n') {
        say(why, why_size, "the only key after the form is season");
        return -1;
    }
    first = slot < 0 ? 0u : (u32)slot;
    last = slot < 0 ? k_groups[type].slots - 1u : (u32)slot;
    for (i = first; i <= last; i++) {
        w.table = (u16)table;
        w.season = (u8)season;
        w.type = (u8)type;
        w.slot = (u8)i;
        w.form = (u8)form;
        if (put(&w) < 0) {
            say(why, why_size, "out of memory");
            return -1;
        }
    }
    return 0;
}

void bw_wild_forms_clear(void)
{
    g_count = 0;
}

void bw_wild_forms_load(void)
{
    u32 m;

    bw_wild_forms_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256], why[160];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        snprintf(path, sizeof path, "%s/eidos/tables/wild_forms.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_wild_forms_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

/* The slots copied last (they live in FIELD_ENCOUNT_CreateZoneWildMons's
 * frame, and the wild Pokemon are rolled from them before it returns) and
 * the whole form of each. */
#define SLOTS_MAX 12
#define SLOT_SIZE 4u                    /* (EncountSlot) */

static const u8 *g_slots;
static int g_slot_count;
static int g_slot_forms[SLOTS_MAX];

void bw_wild_slots_note(const void *slots, int count, int table, int season, int type, int swarm_form)
{
    int i;

    g_slots = (const u8 *)slots;
    g_slot_count = count < 0 ? 0 : count > SLOTS_MAX ? SLOTS_MAX : count;
    for (i = 0; i < g_slot_count; i++) {
        g_slot_forms[i] = swarm_form >= 0 ? (i == 0 ? swarm_form : BW_WILD_FORM_NONE)
                                          : bw_wild_form(table, season, type, i);
    }
}

int bw_wild_slot_form(const void *slot)
{
    const u8 *p = (const u8 *)slot;

    if (g_slots != NULL && p >= g_slots && p < g_slots + (u32)g_slot_count * SLOT_SIZE) {
        return g_slot_forms[(p - g_slots) / SLOT_SIZE];
    }
    return BW_WILD_FORM_NONE;
}

int bw_wild_form(int table, int season, int type, int slot)
{
    const WildForm *w;

    if (g_count == 0 || table < 0 || type < 0 || slot < 0) {
        return BW_WILD_FORM_NONE;
    }
    if (season >= 0 && season < 4 && (w = find((u32)table, (u32)season, (u32)type, (u32)slot)) != NULL) {
        return w->form;
    }
    if ((w = find((u32)table, SEASON_ANY, (u32)type, (u32)slot)) != NULL) {
        return w->form;
    }
    return BW_WILD_FORM_NONE;
}
