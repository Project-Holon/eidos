/*
 * sdk/mon_form.c -- a Pokemon's form past the box record's room
 * (mon_form.h).
 *
 * The box record keeps the form in 5 bits (arm9 poke_tool_types.h,
 * BoxPokemonBlockB.form), so the game's setter cut a form of 32 or more to
 * its low bits: form 40 became form 8, another form.  Here the record of
 * such a Pokemon holds form 0 and the real form is kept in a table, keyed
 * by the Pokemon's personality and trainer id (both are in every copy of
 * the Pokemon: the party, a box, the day care, the battle's copies).  A
 * record of the table counts only while the Pokemon still has the species
 * and the box form it had when the form was set, so a Pokemon made again
 * with the same numbers is not taken for it.
 *
 * The game's accessors take their place here under the game's own names
 * (the boot plan cuts the game's copies): POKE_TOOL_SetBoxValue and
 * POKE_TOOL_SetPartyValueRaw (the only callers of the raw setter), and
 * sdk/poke_value.c's two getters ask bw_mon_form_get.  Five values of the
 * raw accessors read the box's form field: the form itself, the level (the
 * species' growth rate by form), the gender (the gender ratio by form) and
 * the two types.  They are answered with the real form.  With no record
 * each function does what the game's does.
 *
 * The table is saved in the dex extension beside the save (sdk/dex_save.c,
 * the file of the mod that ships the species' stats record): with that mod
 * off its records are not read, and the Pokemon has form 0, a form the game
 * always has.
 */
#include <stdlib.h>
#include <string.h>

#include "mon_form.h"
#include "types.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

/* ------------------------------------------------------------------ */
/* The game's record (arm9 poke_tool_types.h)                          */
/* ------------------------------------------------------------------ */

struct BoxPokemon {
    u32 personality;
    u16 partyDecoded : 1;
    u16 boxDecoded : 1;
    u16 checksumFailed : 1;
    u16 spareFlags : 13;
    u16 checksum;
    u8 blocks[0x80];
};

/* Block B of the box area, the fields up to the form. */
struct BoxPokemonBlockB {
    u16 moves[4];
    u8 pp[4];
    u8 ppUps[4];
    u32 ivs;
    u32 hoennRibbons;
    u8 fatefulEncounter : 1;
    u8 gender : 2;
    u8 form : 5;
};

struct PokemonParam {
    struct BoxPokemon box;
    u32 statusCondition;
    u8 level;
    u8 capsuleIndex;
    u16 hp;
    u16 maxHp;
    u16 attack;
    u16 defense;
    u16 speed;
    u16 spAttack;
    u16 spDefense;
    u8 mail[0x38];                  /* struct Mail */
    u8 pad[8];
};

_Static_assert(sizeof(struct BoxPokemon) == 0x88, "struct BoxPokemon");
_Static_assert(sizeof(struct PokemonParam) == 0xDC, "struct PokemonParam");

struct Mail;

/* arm9/poke_tool_param.h */
#define POKE_TOOL_PARAM_PERSONALITY       0x00
#define POKE_TOOL_PARAM_SPECIES           0x05
#define POKE_TOOL_PARAM_OT_ID             0x07
#define POKE_TOOL_PARAM_EXP               0x08
#define POKE_TOOL_PARAM_ABILITY           0x0A
#define POKE_TOOL_PARAM_GENDER            0x6E
#define POKE_TOOL_PARAM_FORM              0x6F
#define POKE_TOOL_PARAM_STATUS_CONDITION  0x9D
#define POKE_TOOL_PARAM_LEVEL             0x9E
#define POKE_TOOL_PARAM_CAPSULE_INDEX     0x9F
#define POKE_TOOL_PARAM_HP                0xA0
#define POKE_TOOL_PARAM_MAX_HP            0xA1
#define POKE_TOOL_PARAM_ATTACK            0xA2
#define POKE_TOOL_PARAM_DEFENSE           0xA3
#define POKE_TOOL_PARAM_SPEED             0xA4
#define POKE_TOOL_PARAM_SP_ATTACK         0xA5
#define POKE_TOOL_PARAM_SP_DEFENSE        0xA6
#define POKE_TOOL_PARAM_MAIL              0xA7
#define POKE_TOOL_PARAM_NO_DATA           0xA8
#define POKE_TOOL_PARAM_TYPE1             0xAE
#define POKE_TOOL_PARAM_TYPE2             0xAF

#define POKE_TOOL_BLOCK_B                 1      /* arm9 poke_tool.c */
#define PERSONAL_FIELD_FORM_COUNT         0x20   /* arm9 poke_personal.c, data->formCount */
#define SPECIES_ARCEUS                    493
#define ABILITY_MULTITYPE                 121

/* (arm9/poke_tool.h: PokeToolArg is a pointer-sized word) */
typedef __UINTPTR_TYPE__ PokeToolArg;

extern u32 POKE_TOOL_GetBoxValueRaw(struct BoxPokemon *box, u32 paramId, PokeToolArg arg) GAME_FN;
extern void POKE_TOOL_SetBoxValueRaw(struct BoxPokemon *box, u32 paramId, PokeToolArg value) GAME_FN;
extern u32 POKE_TOOL_DecodeBoxBlock(struct BoxPokemon *box) GAME_FN;
extern void POKE_TOOL_EncodeBoxBlock(struct BoxPokemon *box) GAME_FN;
extern void *POKE_TOOL_GetBoxSubstruct(struct BoxPokemon *box, u32 personality, u32 block) GAME_FN;
extern u16 POKE_CRYPTO_Checksum(const u16 *data, u32 size) GAME_FN;
extern u32 POKE_TOOL_CalcLevelFromExp(u16 species, u16 form, u32 exp) GAME_FN;
extern u8 POKE_TOOL_CalcGender(u32 species, u16 form, u32 personality) GAME_FN;
extern u32 POKE_TOOL_GetPersonalParam(u16 species, u16 form, u32 field) GAME_FN;
extern void MAIL_Copy(const struct Mail *src, struct Mail *dest) GAME_FN;

/* sdk/dex_save.c: every dex extension of the save in force read (each
 * merges its records here) */
extern void bw_dex_ext_read_all(void);

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

/* Sorted by personality, then trainer id: one record a Pokemon. */
static BwMonForm *g_recs;
static u32 g_count, g_cap;
static int g_ready;             /* the save's files were read since the last clear */

u32 bw_mon_form_count(void)
{
    return g_count;
}

const BwMonForm *bw_mon_form_at(u32 i)
{
    return i < g_count ? &g_recs[i] : NULL;
}

/* The first record at or after (personality, otId). */
static u32 lower_bound(u32 personality, u32 otId)
{
    u32 lo = 0, hi = g_count;

    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        const BwMonForm *r = &g_recs[mid];

        if (r->personality < personality || (r->personality == personality && r->otId < otId)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static BwMonForm *find(u32 personality, u32 otId)
{
    u32 i = lower_bound(personality, otId);

    if (i < g_count && g_recs[i].personality == personality && g_recs[i].otId == otId) {
        return &g_recs[i];
    }
    return NULL;
}

static void drop(u32 personality, u32 otId)
{
    BwMonForm *r = find(personality, otId);

    if (r != NULL) {
        u32 i = (u32)(r - g_recs);

        memmove(&g_recs[i], &g_recs[i + 1], (g_count - i - 1) * sizeof *g_recs);
        g_count--;
    }
}

/* Puts a record in (one already there for the Pokemon is replaced). */
static void put(const BwMonForm *rec)
{
    u32 i = lower_bound(rec->personality, rec->otId);

    if (i < g_count && g_recs[i].personality == rec->personality && g_recs[i].otId == rec->otId) {
        g_recs[i] = *rec;
        return;
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2u : 16u;
        BwMonForm *grown = (BwMonForm *)realloc(g_recs, cap * sizeof *grown);

        if (grown == NULL) {
            return;
        }
        g_recs = grown;
        g_cap = cap;
    }
    memmove(&g_recs[i + 1], &g_recs[i], (g_count - i) * sizeof *g_recs);
    g_recs[i] = *rec;
    g_count++;
}

void bw_mon_form_merge(const BwMonForm *recs, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        if (recs[i].form >= BW_MON_BOX_FORMS && find(recs[i].personality, recs[i].otId) == NULL) {
            put(&recs[i]);
        }
    }
}

void bw_mon_form_clear(void)
{
    g_count = 0;
    g_ready = 0;
}

void bw_mon_form_unready(void)
{
    g_ready = 0;
}

static void ready(void)
{
    if (!g_ready) {
        g_ready = 1;
        bw_dex_ext_read_all();
    }
}

/* The main save's words: a record stays while some word of the save is
 * its personality (a Pokemon's record starts with it, unmasked, wherever
 * the save keeps one).  A word that only looks like one keeps a record
 * that is no longer needed, never the other way round. */
static int cmp_u32(const void *a, const void *b)
{
    u32 x = *(const u32 *)a, y = *(const u32 *)b;

    return x < y ? -1 : x > y;
}

void bw_mon_form_keep_saved(const void *save, u32 size)
{
    const u8 *bytes = (const u8 *)save;
    u8 *kept;
    u32 *pids, i, n = 0, at;

    if (save == NULL || g_count == 0) {
        return;
    }
    pids = (u32 *)malloc(g_count * sizeof *pids);
    kept = (u8 *)calloc(g_count, 1);
    if (pids == NULL || kept == NULL) {
        free(pids);
        free(kept);
        return;
    }
    for (i = 0; i < g_count; i++) {
        pids[i] = g_recs[i].personality;
    }
    qsort(pids, g_count, sizeof *pids, cmp_u32);
    for (at = 0; at + 4u <= size; at += 4u) {
        u32 w;

        memcpy(&w, bytes + at, 4);
        if (bsearch(&w, pids, g_count, sizeof *pids, cmp_u32) != NULL) {
            /* (the table is sorted by personality: mark each record of it) */
            u32 k = lower_bound(w, 0);

            for (; k < g_count && g_recs[k].personality == w; k++) {
                kept[k] = 1;
            }
        }
    }
    for (i = 0; i < g_count; i++) {
        if (kept[i]) {
            g_recs[n++] = g_recs[i];
        }
    }
    g_count = n;
    free(pids);
    free(kept);
}

/* ------------------------------------------------------------------ */
/* A Pokemon's real form                                               */
/* ------------------------------------------------------------------ */

/* The form count of a species (its stats record's). */
static u32 form_count(u32 species)
{
    return POKE_TOOL_GetPersonalParam((u16)species, 0, PERSONAL_FIELD_FORM_COUNT);
}

/* The record of a decoded box when it counts for it, else NULL. */
static BwMonForm *record_of(struct BoxPokemon *box)
{
    BwMonForm *r;

    ready();
    if (g_count == 0 || box->checksumFailed) {
        return NULL;
    }
    r = find(box->personality, POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_OT_ID, 0));
    if (r == NULL || r->species != POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_SPECIES, 0)
        || r->boxForm != POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_FORM, 0)) {
        return NULL;
    }
    return r;
}

/* The real form of a decoded box past the box's room: 1 with *form set
 * (a form the species has: with the mod that adds it off, the box's own
 * form stands). */
static int real_form(struct BoxPokemon *box, u32 *form)
{
    BwMonForm *r = record_of(box);

    if (r == NULL || r->form >= form_count(r->species)) {
        return 0;
    }
    *form = r->form;
    return 1;
}

static struct BoxPokemonBlockB *block_b(struct BoxPokemon *box)
{
    return (struct BoxPokemonBlockB *)POKE_TOOL_GetBoxSubstruct(box, box->personality, POKE_TOOL_BLOCK_B);
}

/* The getters' question (sdk/poke_value.c): the five values of a decoded
 * box the raw getter works out from the box's form field, worked out with
 * the real form.  1 with *value set; 0 for the raw getter's own. */
int bw_mon_form_get(struct BoxPokemon *box, u32 paramId, u32 *value)
{
    u32 form, species;

    switch (paramId) {
    case POKE_TOOL_PARAM_FORM:
    case POKE_TOOL_PARAM_LEVEL:
    case POKE_TOOL_PARAM_GENDER:
    case POKE_TOOL_PARAM_TYPE1:
    case POKE_TOOL_PARAM_TYPE2:
        break;
    default:
        return 0;
    }
    if (box == NULL || !real_form(box, &form)) {
        return 0;
    }
    species = POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_SPECIES, 0);
    switch (paramId) {
    case POKE_TOOL_PARAM_FORM:
        *value = form;
        return 1;
    case POKE_TOOL_PARAM_LEVEL:
        *value = POKE_TOOL_CalcLevelFromExp((u16)species, (u16)form,
                                            POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_EXP, 0));
        return 1;
    case POKE_TOOL_PARAM_GENDER:
        /* (the game's getter keeps the gender it works out in the record) */
        *value = POKE_TOOL_CalcGender(species, (u16)form, box->personality);
        block_b(box)->gender = (u8)*value;
        box->checksum = POKE_CRYPTO_Checksum((const u16 *)box->blocks, 0x80);
        return 1;
    default:
        /* (Arceus with Multitype takes its plate's type: the game's own) */
        if (species == SPECIES_ARCEUS && POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_ABILITY, 0) == ABILITY_MULTITYPE) {
            return 0;
        }
        *value = POKE_TOOL_GetPersonalParam((u16)species, (u16)form, paramId - 0xA8);
        return 1;
    }
}

/* The setters' side, on a decoded box: 1 when the value was set here. */
static int form_set(struct BoxPokemon *box, u32 paramId, PokeToolArg value)
{
    BwMonForm *r, rec;
    u32 form;

    switch (paramId) {
    case POKE_TOOL_PARAM_FORM:
        ready();
        rec.personality = box->personality;
        rec.otId = POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_OT_ID, 0);
        drop(rec.personality, rec.otId);
        if ((u32)value < BW_MON_BOX_FORMS) {
            return 0;
        }
        /* past the field's room: the record holds form 0, the table the form */
        rec.species = (u16)POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_SPECIES, 0);
        rec.form = (u8)((u32)value > 0xFFu ? 0xFFu : (u32)value);
        rec.boxForm = 0;
        put(&rec);
        POKE_TOOL_SetBoxValueRaw(box, POKE_TOOL_PARAM_FORM, 0);
        return 1;
    case POKE_TOOL_PARAM_GENDER:
        /* (the game's setter works the gender out from the box's form) */
        if (!real_form(box, &form)) {
            return 0;
        }
        block_b(box)->gender = POKE_TOOL_CalcGender(POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_SPECIES, 0),
                                                    (u16)form, box->personality);
        return 1;
    case POKE_TOOL_PARAM_SPECIES:
        /* an evolution keeps the form, as the box's form field does */
        if ((r = record_of(box)) == NULL) {
            return 0;
        }
        r->species = (u16)value;
        POKE_TOOL_SetBoxValueRaw(box, paramId, value);
        return 1;
    case POKE_TOOL_PARAM_OT_ID:
        /* the record follows the Pokemon to its new trainer id */
        if ((r = record_of(box)) == NULL) {
            return 0;
        }
        rec = *r;
        drop(rec.personality, rec.otId);
        rec.otId = (u32)value;
        put(&rec);
        POKE_TOOL_SetBoxValueRaw(box, paramId, value);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The game's setters (arm9 poke_tool.c)                               */
/* ------------------------------------------------------------------ */

void POKE_TOOL_SetBoxValue(struct BoxPokemon *box, u32 paramId, PokeToolArg value)
{
    if (POKE_TOOL_DecodeBoxBlock(box) == 1) {
        if (!form_set(box, paramId, value)) {
            POKE_TOOL_SetBoxValueRaw(box, paramId, value);
        }
    }
    POKE_TOOL_EncodeBoxBlock(box);
}

void POKE_TOOL_SetPartyValueRaw(struct PokemonParam *pp, u32 paramId, PokeToolArg value)
{
    switch (paramId) {
    case POKE_TOOL_PARAM_STATUS_CONDITION: pp->statusCondition = (u32)value; return;
    case POKE_TOOL_PARAM_LEVEL: pp->level = (u8)value; return;
    case POKE_TOOL_PARAM_CAPSULE_INDEX: pp->capsuleIndex = (u8)value; return;
    case POKE_TOOL_PARAM_HP: pp->hp = (u16)value; return;
    case POKE_TOOL_PARAM_MAX_HP: pp->maxHp = (u16)value; return;
    case POKE_TOOL_PARAM_ATTACK: pp->attack = (u16)value; return;
    case POKE_TOOL_PARAM_DEFENSE: pp->defense = (u16)value; return;
    case POKE_TOOL_PARAM_SPEED: pp->speed = (u16)value; return;
    case POKE_TOOL_PARAM_SP_ATTACK: pp->spAttack = (u16)value; return;
    case POKE_TOOL_PARAM_SP_DEFENSE: pp->spDefense = (u16)value; return;
    case POKE_TOOL_PARAM_MAIL:
        MAIL_Copy((const struct Mail *)value, (struct Mail *)pp->mail);
        return;
    case POKE_TOOL_PARAM_NO_DATA:
        break;
    default:
        if (!form_set(&pp->box, paramId, value)) {
            POKE_TOOL_SetBoxValueRaw(&pp->box, paramId, value);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The battle's base form                                              */
/* ------------------------------------------------------------------ */

/* A battle holds at most 24 Pokemon (four clients of six); the slots are
 * keyed by the BtlPokeParam's address, written again when the battle
 * takes a Pokemon in at that address. */
#define BPP_SLOTS 32

static struct {
    const void *bpp;
    u8 form;
} g_bpp[BPP_SLOTS];
static u32 g_bpp_next;

void bw_bpp_base_form_set(const void *bpp, u32 form)
{
    u32 i, free_at = BPP_SLOTS;

    for (i = 0; i < BPP_SLOTS; i++) {
        if (g_bpp[i].bpp == bpp) {
            break;
        }
        if (g_bpp[i].bpp == NULL && free_at == BPP_SLOTS) {
            free_at = i;
        }
    }
    if (i == BPP_SLOTS) {
        if (form < BW_MON_BOX_FORMS) {
            return;             /* (the field holds it) */
        }
        i = free_at != BPP_SLOTS ? free_at : (g_bpp_next++ % BPP_SLOTS);
    }
    if (form < BW_MON_BOX_FORMS) {
        g_bpp[i].bpp = NULL;
        return;
    }
    g_bpp[i].bpp = bpp;
    g_bpp[i].form = (u8)(form > 0xFFu ? 0xFFu : form);
}

u32 bw_bpp_base_form(const void *bpp, u32 bits)
{
    u32 i;

    for (i = 0; i < BPP_SLOTS; i++) {
        if (g_bpp[i].bpp == bpp && bpp != NULL) {
            /* (the field holds the form's low five bits) */
            return (g_bpp[i].form & 0x1Fu) == (bits & 0x1Fu) ? g_bpp[i].form : bits;
        }
    }
    return bits;
}
