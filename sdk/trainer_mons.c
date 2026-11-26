/*
 * sdk/trainer_mons.c -- per-member numbers for a trainer's Pokemon from the
 * mods' eidos/tables/trainer_mons.txt (sdk/trainer_mons.h,
 * docs/trainer-mons.md).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trainer_mons.h"
#include "vfs.h"

struct PokemonParam;

/* The game's own (arm9 poke_tool.c), with the addresses the corpus gives
 * them; weak, so a link without the game (the tests) still succeeds. */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId, u32 arg) BW_PORT_WEAK;  /* 0x02017E1C */
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *mon, u32 paramId, u32 value) BW_PORT_WEAK;
extern u32 POKE_TOOL_GetPersonalParam(u32 species, u16 form, u32 field) BW_PORT_WEAK;
extern void POKE_TOOL_RecalcParam(struct PokemonParam *pp) BW_PORT_WEAK;

/* The fields (src/game/arm9/poke_tool_param.h) and the personal data's
 * abilities (POKE_TOOL_SetBoxAbility). */
#define F_PERSONALITY  0x00u
#define F_SPECIES      0x05u
#define F_OT_ID        0x07u
#define F_FRIENDSHIP   0x09u
#define F_ABILITY      0x0Au
#define F_FORM         0x6Fu
#define F_NATURE       0x70u
#define F_HIDDEN       0x71u
#define P_ABILITY_1    0x1Au
#define P_ABILITY_2    0x1Bu
#define P_ABILITY_H    0x1Cu

/* HP, Atk, Def, Sp. Atk, Sp. Def, Speed: the line's order (the one players
 * write sets in), and the game's field for each. */
static const u32 k_iv_field[6] = { 0x46u, 0x47u, 0x48u, 0x4Au, 0x4Bu, 0x49u };
static const u32 k_ev_field[6] = { 0x0Du, 0x0Eu, 0x0Fu, 0x11u, 0x12u, 0x10u };

static const char *const k_natures[25] = {
    "hardy", "lonely", "brave", "adamant", "naughty", "bold", "docile", "relaxed", "impish",
    "lax", "timid", "hasty", "serious", "jolly", "naive", "modest", "mild", "quiet",
    "bashful", "rash", "calm", "gentle", "sassy", "careful", "quirky",
};

static BwTrMon *g_lines;
static u32 g_count, g_cap;

void bw_trmons_clear(void)
{
    free(g_lines);
    g_lines = NULL;
    g_count = g_cap = 0;
}

const BwTrMon *bw_trmons_find(u32 trainer, u32 slot)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_lines[i].trainer == trainer && g_lines[i].slot == slot) {
            return &g_lines[i];
        }
    }
    return NULL;
}

static int fail(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
    return -1;
}

/* A number 0..max from `s` up to a comma, a space or the end; the end in *end. */
static int number(const char *s, const char **end, long max, long *out)
{
    char *e;
    long v;

    if (!isdigit((unsigned char)*s)) {
        return -1;
    }
    v = strtol(s, &e, 10);
    if (v < 0 || v > max) {
        return -1;
    }
    *out = v;
    *end = e;
    return 0;
}

static int six(const char *s, const char **end, long max, u8 out[6])
{
    int k;
    long v;

    for (k = 0; k < 6; k++) {
        if (number(s, &s, max, &v) != 0) {
            return -1;
        }
        out[k] = (u8)v;
        if (k < 5) {
            if (*s != ',') {
                return -1;
            }
            s++;
        }
    }
    *end = s;
    return 0;
}

int bw_trmons_parse_line(const char *text, char *why, int why_size)
{
    char line[512], *hash, *p;
    long trainer, slot, v;
    const char *s, *e;
    BwTrMon m;
    u32 i;

    snprintf(line, sizeof line, "%s", text);
    if ((hash = strchr(line, '#')) != NULL) {
        *hash = '\0';
    }
    for (p = line; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return 1;
    }
    memset(&m, 0, sizeof m);
    if (number(s, &s, 65535, &trainer) != 0 || *s != '.') {
        return fail(why, why_size, "expected `<trainer id>.<slot> = key=value ...`");
    }
    if (number(s + 1, &s, 5, &slot) != 0) {
        return fail(why, why_size, "the slot is 0 to 5");
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '=') {
        return fail(why, why_size, "expected `=` after the slot");
    }
    s++;
    m.trainer = (u16)trainer;
    m.slot = (u8)slot;
    for (;;) {
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        if (strncmp(s, "ivs=", 4) == 0) {
            if (six(s + 4, &e, 31, m.ivs) != 0) {
                return fail(why, why_size, "ivs are six numbers 0 to 31: HP, Atk, Def, Sp. Atk, Sp. Def, Speed");
            }
            m.has |= BW_TRMON_HAS_IVS;
        } else if (strncmp(s, "evs=", 4) == 0) {
            int total = 0, k;

            if (six(s + 4, &e, 255, m.evs) != 0) {
                return fail(why, why_size, "evs are six numbers 0 to 255: HP, Atk, Def, Sp. Atk, Sp. Def, Speed");
            }
            for (k = 0; k < 6; k++) {
                total += m.evs[k];
            }
            if (total > 510) {
                return fail(why, why_size, "evs add up to more than 510");
            }
            m.has |= BW_TRMON_HAS_EVS;
        } else if (strncmp(s, "nature=", 7) == 0) {
            int k, found = -1;

            for (e = s + 7; *e && !isspace((unsigned char)*e); e++) {
            }
            for (k = 0; k < 25; k++) {
                size_t n = strlen(k_natures[k]);

                if ((size_t)(e - (s + 7)) == n && strncmp(s + 7, k_natures[k], n) == 0) {
                    found = k;
                }
            }
            if (found < 0 && number(s + 7, &e, 24, &v) == 0) {
                found = (int)v;
            }
            if (found < 0) {
                return fail(why, why_size, "nature is a nature's name (adamant) or 0 to 24");
            }
            m.nature = (u8)found;
            m.has |= BW_TRMON_HAS_NATURE;
        } else if (strncmp(s, "shiny=", 6) == 0) {
            if (number(s + 6, &e, 1, &v) != 0) {
                return fail(why, why_size, "shiny is 0 or 1");
            }
            m.shiny = (u8)v;
            m.has |= BW_TRMON_HAS_SHINY;
        } else if (strncmp(s, "friendship=", 11) == 0) {
            if (number(s + 11, &e, 255, &v) != 0) {
                return fail(why, why_size, "friendship is 0 to 255");
            }
            m.friendship = (u8)v;
            m.has |= BW_TRMON_HAS_FRIENDSHIP;
        } else if (strncmp(s, "ability=", 8) == 0) {
            if (strncmp(s + 8, "hidden", 6) == 0) {
                m.ability = 3;
                e = s + 14;
            } else if (number(s + 8, &e, 3, &v) == 0 && v >= 1) {
                m.ability = (u8)v;
            } else {
                return fail(why, why_size, "ability is 1, 2 or hidden");
            }
            m.has |= BW_TRMON_HAS_ABILITY;
        } else {
            return fail(why, why_size, "unknown key (ivs, evs, nature, shiny, friendship, ability)");
        }
        if (*e != '\0' && !isspace((unsigned char)*e)) {
            return fail(why, why_size, "expected a space between keys");
        }
        s = e;
    }
    for (i = 0; i < g_count; i++) {
        if (g_lines[i].trainer == m.trainer && g_lines[i].slot == m.slot) {
            g_lines[i] = m;     /* a later line (a later mod's) wins */
            return 0;
        }
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2 : 64;
        BwTrMon *grown = (BwTrMon *)realloc(g_lines, cap * sizeof *grown);

        if (grown == NULL) {
            return fail(why, why_size, "out of memory");
        }
        g_lines = grown;
        g_cap = cap;
    }
    g_lines[g_count++] = m;
    return 0;
}

void bw_trmons_load(void)
{
    u32 m;

    bw_trmons_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[512], why[200];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/trainer_mons.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_trmons_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

int bw_trmons_apply(u32 trainer, u32 slot, struct PokemonParam *mon)
{
    const BwTrMon *m = bw_trmons_find(trainer, slot);
    int k;

    if (m == NULL || mon == NULL || POKE_TOOL_GetPartyValue == NULL || POKE_TOOL_SetPartyValue == NULL) {
        return 0;
    }
    if (m->has & BW_TRMON_HAS_IVS) {
        for (k = 0; k < 6; k++) {
            POKE_TOOL_SetPartyValue(mon, k_iv_field[k], m->ivs[k]);
        }
    }
    if (m->has & BW_TRMON_HAS_EVS) {
        for (k = 0; k < 6; k++) {
            POKE_TOOL_SetPartyValue(mon, k_ev_field[k], m->evs[k]);
        }
    }
    if (m->has & BW_TRMON_HAS_NATURE) {
        POKE_TOOL_SetPartyValue(mon, F_NATURE, m->nature);
    }
    if (m->has & BW_TRMON_HAS_FRIENDSHIP) {
        POKE_TOOL_SetPartyValue(mon, F_FRIENDSHIP, m->friendship);
    }
    if (m->has & BW_TRMON_HAS_SHINY) {
        /* Shiny when the trainer id's halves xor the personality's below 8
         * (POKE_TOOL_IsShinyOtIdPersonality): the id is picked to match,
         * so the personality (gender, ability slot) stays the game's. */
        u32 pid = POKE_TOOL_GetPartyValue(mon, F_PERSONALITY, 0);
        u32 x = ((pid >> 16) ^ (pid & 0xFFFFu)) & 0xFFFFu;

        POKE_TOOL_SetPartyValue(mon, F_OT_ID, m->shiny ? x : (x ^ 8u));
    }
    if ((m->has & BW_TRMON_HAS_ABILITY) && POKE_TOOL_GetPersonalParam != NULL) {
        u32 species = POKE_TOOL_GetPartyValue(mon, F_SPECIES, 0);
        u16 form = (u16)POKE_TOOL_GetPartyValue(mon, F_FORM, 0);
        u32 a1 = POKE_TOOL_GetPersonalParam(species, form, P_ABILITY_1);
        u32 a2 = POKE_TOOL_GetPersonalParam(species, form, P_ABILITY_2);
        u32 ah = POKE_TOOL_GetPersonalParam(species, form, P_ABILITY_H);

        /* (a species without a second or hidden ability keeps its first) */
        if (m->ability == 3 && ah != 0) {
            POKE_TOOL_SetPartyValue(mon, F_ABILITY, ah);
            POKE_TOOL_SetPartyValue(mon, F_HIDDEN, 1);
        } else {
            POKE_TOOL_SetPartyValue(mon, F_ABILITY, m->ability == 2 && a2 != 0 ? a2 : a1);
            POKE_TOOL_SetPartyValue(mon, F_HIDDEN, 0);
        }
    }
    if (POKE_TOOL_RecalcParam != NULL && (m->has & (BW_TRMON_HAS_IVS | BW_TRMON_HAS_EVS | BW_TRMON_HAS_NATURE))) {
        POKE_TOOL_RecalcParam(mon);     /* the stats from the new numbers */
    }
    fprintf(stderr, "eidos: trainer %u slot %u: %s%s%s%s%s%s(eidos/tables)\n", (unsigned)trainer, (unsigned)slot,
            m->has & BW_TRMON_HAS_IVS ? "ivs " : "", m->has & BW_TRMON_HAS_EVS ? "evs " : "",
            m->has & BW_TRMON_HAS_NATURE ? "nature " : "", m->has & BW_TRMON_HAS_SHINY ? "shiny " : "",
            m->has & BW_TRMON_HAS_FRIENDSHIP ? "friendship " : "", m->has & BW_TRMON_HAS_ABILITY ? "ability " : "");
    return 1;
}
