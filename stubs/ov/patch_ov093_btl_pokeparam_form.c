/*
 * patches: src/game/ov093/btl_pokeparam.c BTL_POKEPARAM_Create BTL_POKEPARAM_ChangeForm BTL_POKEPARAM_WriteBackToParam
 *
 * symbols:
 *   GFL_HEAP_Alloc                 0x02030734  arm
 *   MI_CpuFill8                    0x02082BCC  arm
 *   POKE_TOOL_GetPartyValue        0x02017E1C  thumb
 *   POKE_TOOL_SetPartyValue        0x02017E40  thumb
 *   POKE_TOOL_GetPersonalParam     0x02019C90  thumb
 *   POKE_TOOL_GetStatusCondition   0x02017F90  thumb
 *   POKE_TOOL_SetStatusCondition   0x02017F9C  thumb
 *   POKE_TOOL_DecodePartyParam     0x02017D30  thumb
 *   POKE_TOOL_EncodePartyParam     0x02017D70  thumb
 *   POKE_TOOL_ChangeForm           0x02017988  thumb
 *   POKE_TOOL_RecalcParam          0x020185C4  thumb
 *   BTL_CALC_MakeDefaultSickCont   0x021D7D18  thumb  ov093
 *   gBtlPokeparamFileName          0x021F3AA4  data  a1
 *
 * The form a Pokemon goes back to after the battle.  The battle keeps it in
 * BtlPokeParam.baseForm, a 5-bit field, so a Pokemon in a form a mod adds
 * past 31 (sdk/mon_form.c) came back in the form of the low five bits.  The
 * battle's own form (BtlPokeParam.form) is a byte and holds it.  Create
 * notes the whole base form beside the field (bw_bpp_base_form_set), and
 * WriteBackToParam reads it from there (bw_bpp_base_form), as do the two
 * effect resets (stubs/ov/patch_ov093_btl_pokeparam_moves.c).  Shaymin's
 * change back in ChangeForm sets the base form to 0 there too.  Create's
 * gender ratio is the real form's.  For a form below 32 the three are the
 * game's own.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef __UINTPTR_TYPE__ PokeToolArg;   /* (arm9 poke_tool_shared_types.h: an address-sized value off the DS) */

struct PokemonParam;

/* btl_pokeparam_types.h */
#define BTL_POKEPARAM_FOUGHT_MAX 0x18
#define BTL_POKEPARAM_HIT_TURNS 3
#define BTL_POKEPARAM_HIT_MAX 6
#define BTL_POKEPARAM_MOVE_COUNT 4

union SickCont {
    u32 raw;
    struct { u32 kind : 3; u32 paramLow : 6; u32 paramHigh : 6; u32 flags : 17; } byPart;
};

struct MoveInfo {
    u16 id;
    u8 pp;
    u8 ppMax;
    u8 ppUp;
    u8 counters;
};

struct BattleMoveSlot {
    struct MoveInfo cur;
    struct MoveInfo saved;
    u8 savedActive;
    u8 savedSpare;
};

struct StatStages {
    u8 stage[7];
};

struct BtlHitRecord {
    u16 moveId;
    u16 damage;
    u16 typeAndAttacker;
    u16 hitFlags;
};

struct BtlPokeParam {
    struct PokemonParam *mon;           /* 0x00 */
    struct PokemonParam *illusionMon;   /* 0x04 */
    u32 exp;                            /* 0x08 */
    u16 species;                        /* 0x0c */
    u16 maxHp;                          /* 0x0e */
    u16 hp;                             /* 0x10 */
    u16 heldItem;                       /* 0x12 */
    u16 consumedItem;                   /* 0x14 */
    u16 baseAbility;                    /* 0x16 */
    u8 level;                           /* 0x18 */
    u8 partyIndex;                      /* 0x19 */
    u8 genderRatio;                     /* 0x1a */
    u8 baseForm : 5;                    /* 0x1b form to restore after the battle */
    u8 isTransformed : 1;
    u8 illusionActive : 1;
    u8 thresholdArmed : 1;
    union SickCont sickCont[36];        /* 0x1c */
    u8 sickTurn[36];                    /* 0xac */
    u8 foughtCount;                     /* 0xd0 */
    u8 foughtIds[BTL_POKEPARAM_FOUGHT_MAX]; /* 0xd1 */
    u8 padE9[0xee - 0xe9];
    u16 attack;                         /* 0xee */
    u16 defense;                        /* 0xf0 */
    u16 spAttack;                       /* 0xf2 */
    u16 spDefense;                      /* 0xf4 */
    u16 speed;                          /* 0xf6 */
    u8 type1;                           /* 0xf8 */
    u8 type2;                           /* 0xf9 */
    u8 gender;                          /* 0xfa */
    u8 padFB;
    struct StatStages stages;           /* 0xfc */
    u8 pad103[1];
    struct BattleMoveSlot moves[BTL_POKEPARAM_MOVE_COUNT]; /* 0x104 */
    u16 ability;                        /* 0x13c */
    u16 weight;                         /* 0x13e */
    u8 moveCount;                       /* 0x140 */
    u8 form;                            /* 0x141 */
    u8 critStage;                       /* 0x142 */
    u8 usedMoveCount;                   /* 0x143 */
    u8 lastMoveType;                    /* 0x144 */
    u8 pad145[1];
    u16 turnsOnField;                   /* 0x146 */
    u16 turnThreshold;                  /* 0x148 */
    u16 lastSelectedMoveId;             /* 0x14a */
    u16 lastUsedMoveId;                 /* 0x14c */
    u16 moveStreak;                     /* 0x14e */
    u8 pad150[2];
    u8 lastMoveSlot;                    /* 0x152 */
    u8 turnFlags[2];                    /* 0x153 */
    u8 contFlags[2];                    /* 0x155 */
    u8 counters[5];                     /* 0x157 */
    struct BtlHitRecord hitHistory[BTL_POKEPARAM_HIT_TURNS][BTL_POKEPARAM_HIT_MAX]; /* 0x15c */
    u8 hitCount[BTL_POKEPARAM_HIT_TURNS]; /* 0x1ec */
    u8 hitTurnIndex;                    /* 0x1ef */
    u8 hitHistorySpare;                 /* 0x1f0 */
    u8 pad1F1[1];
    u16 substituteHp;                   /* 0x1f2 */
    u16 storedMoveId;                   /* 0x1f4 */
    u8 storedPokeId;                    /* 0x1f6 */
    u8 pad1F7;
};

typedef char BtlPokeParamSizeCheck[(sizeof(void *) != 4 || (sizeof(struct BtlPokeParam) == 0x1f8)) ? 1 : -1];
typedef char BtlPokeParamFormCheck[(sizeof(void *) != 4 || __builtin_offsetof(struct BtlPokeParam, form) == 0x141) ? 1 : -1];

typedef struct BtlPokeParam BtlPokeParam;

#define POKETYPE_NONE        31         /* (the port's "no type", arm9 poke_type.h) */
#define SPECIES_SHAYMIN      0x1EC
#define POKE_PARAM_SPECIES   5
#define POKE_PARAM_HELD_ITEM 6
#define POKE_PARAM_EXP       8
#define POKE_PARAM_ABILITY   0xA
#define POKE_PARAM_FORM      0x6F
#define POKE_PARAM_LEVEL     0x9E
#define POKE_PARAM_HP        0xA0
#define POKE_PARAM_MAX_HP    0xA1
#define PERSONAL_GENDER_RATIO 1
#define BW_LIMIT_ITEM_LAST   0          /* (src/game/port_limits.h) */

extern void *GFL_HEAP_Alloc(u32 heapId, u32 size, int clear, const char *site, u16 tag);
extern void MI_CpuFill8(void *dest, u8 value, u32 size);
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg arg);
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg value);
extern u32 POKE_TOOL_GetPersonalParam(u16 species, u16 form, u32 field);
extern u32 POKE_TOOL_GetStatusCondition(struct PokemonParam *pp);
extern void POKE_TOOL_SetStatusCondition(struct PokemonParam *pp, u32 statusCondition);
extern u32 POKE_TOOL_DecodePartyParam(struct PokemonParam *pp);
extern u32 POKE_TOOL_EncodePartyParam(struct PokemonParam *pp, u32 didDecode);
extern u32 POKE_TOOL_ChangeForm(struct PokemonParam *pp, u32 form);
extern void POKE_TOOL_RecalcParam(struct PokemonParam *pp);
extern union SickCont BTL_CALC_MakeDefaultSickCont(int kind);
extern const char gBtlPokeparamFileName[];
extern u32 bw_limit_get(int id);

extern void BTL_POKEPARAM_ReloadFromMon(BtlPokeParam *pp, struct PokemonParam *mon, u32 withHp, u32 withAbility);
extern int BTL_POKEPARAM_LoadMoves(BtlPokeParam *pp, struct PokemonParam *mon, int reset);
extern void BTL_POKEPARAM_SaveMovesToMon(BtlPokeParam *pp);
extern void BTL_POKEPARAM_LoadStats(BtlPokeParam *pp, struct PokemonParam *mon, u32 withTypes);
extern void BTL_POKEPARAM_InitStatStages(unsigned char *stages);
extern void BTL_POKEPARAM_ClearSickConts(BtlPokeParam *pp, int all);
extern void BTL_POKEPARAM_ClearHitHistory(BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearFoughtList(BtlPokeParam *pp);
extern int BTL_POKEPARAM_GetMajorStatusIndex(struct BtlPokeParam *bpp);

/* sdk/mon_form.c */
extern void bw_bpp_base_form_set(const void *bpp, u32 form);
extern u32 bw_bpp_base_form(const void *bpp, u32 bits);

BtlPokeParam *BTL_POKEPARAM_Create(struct PokemonParam *mon, u8 pokeId, u32 heapId)
{
    BtlPokeParam *pp;
    u32 statusCondition;
    u32 baseForm;

    pp = GFL_HEAP_Alloc(heapId, sizeof(*pp), 1, gBtlPokeparamFileName, 0xe3);
    pp->partyIndex = pokeId;
    pp->species = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_SPECIES, 0);
    pp->mon = mon;
    pp->hp = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_HP, 0);
    pp->maxHp = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_MAX_HP, 0);
    pp->heldItem = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_HELD_ITEM, 0);
    if (pp->heldItem > bw_limit_get(BW_LIMIT_ITEM_LAST)) pp->heldItem = 0;
    pp->consumedItem = 0;
    pp->isTransformed = 0;
    pp->illusionMon = 0;
    pp->illusionActive = 0;
    pp->thresholdArmed = 0;
    baseForm = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_FORM, 0);
    pp->baseForm = baseForm;
    bw_bpp_base_form_set(pp, baseForm);
    pp->baseAbility = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_ABILITY, 0);
    pp->level = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_LEVEL, 0);
    pp->genderRatio = POKE_TOOL_GetPersonalParam(pp->species, (u16)baseForm, PERSONAL_GENDER_RATIO);
    BTL_POKEPARAM_ReloadFromMon(pp, mon, 1, 1);
    pp->moveCount = BTL_POKEPARAM_LoadMoves(pp, mon, 1);
    pp->usedMoveCount = 0;
    BTL_POKEPARAM_InitStatStages(pp->stages.stage);
    BTL_POKEPARAM_ClearSickConts(pp, 1);
    statusCondition = POKE_TOOL_GetStatusCondition(mon);
    if (statusCondition != 0) {
        pp->sickCont[statusCondition] = BTL_CALC_MakeDefaultSickCont(statusCondition);
        pp->sickTurn[statusCondition] = 0;
    }
    pp->turnThreshold = 10000;
    pp->turnsOnField = 0;
    pp->substituteHp = 0;
    pp->lastSelectedMoveId = 0;
    pp->lastUsedMoveId = 0;
    pp->lastMoveType = POKETYPE_NONE;
    pp->moveStreak = 0;
    pp->storedMoveId = 0;
    pp->storedPokeId = 0x1f;
    pp->critStage = 0;
    MI_CpuFill8(pp->turnFlags, 0, sizeof(pp->turnFlags));
    MI_CpuFill8(pp->contFlags, 0, sizeof(pp->contFlags));
    BTL_POKEPARAM_ClearHitHistory(pp);
    BTL_POKEPARAM_ClearFoughtList(pp);
    return pp;
}

void BTL_POKEPARAM_ChangeForm(struct BtlPokeParam *poke, u32 form)
{
    u32 savedState;
    poke->form = form;
    savedState = POKE_TOOL_DecodePartyParam(poke->mon);
    POKE_TOOL_ChangeForm(poke->mon, form);
    BTL_POKEPARAM_LoadStats(poke, poke->mon, 1);
    if (poke->species == SPECIES_SHAYMIN && form == 0) {
        poke->baseForm = 0;
        bw_bpp_base_form_set(poke, 0);
        poke->ability = POKE_TOOL_GetPartyValue(poke->mon, POKE_PARAM_ABILITY, 0);
    }
    POKE_TOOL_EncodePartyParam(poke->mon, savedState);
}

/* Writes exp, HP, status, held item and form back into the party Pokemon. */
void BTL_POKEPARAM_WriteBackToParam(struct BtlPokeParam *poke, int useOriginalForm)
{
    struct PokemonParam *pokeParam = poke->mon;
    u32 form;

    POKE_TOOL_SetPartyValue(pokeParam, POKE_PARAM_EXP, poke->exp);
    POKE_TOOL_SetPartyValue(pokeParam, POKE_PARAM_HP, poke->hp);
    if (poke->hp != 0)
        POKE_TOOL_SetStatusCondition(pokeParam, BTL_POKEPARAM_GetMajorStatusIndex(poke));
    else
        POKE_TOOL_SetStatusCondition(pokeParam, 0);
    BTL_POKEPARAM_SaveMovesToMon(poke);
    if (poke->isTransformed || useOriginalForm != 0)
        form = bw_bpp_base_form(poke, poke->baseForm);
    else
        form = poke->form;
    POKE_TOOL_SetPartyValue(pokeParam, POKE_PARAM_FORM, (u8)form);
    POKE_TOOL_RecalcParam(pokeParam);
    POKE_TOOL_SetPartyValue(pokeParam, POKE_PARAM_HELD_ITEM, poke->heldItem);
}
