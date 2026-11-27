/*
 * patches: src/game/ov093/btl_pokeparam.c BTL_POKEPARAM_HasType BTL_POKEPARAM_Delete BTL_POKEPARAM_ResetAllEffects BTL_POKEPARAM_ResetEffectsOnSwitchOut BTL_POKEPARAM_ReloadAndResetEffects
 *
 * A third type (eidos's move bases add_type, trick_or_treat,
 * forests_curse; sdk/battle_moves.h): HasType counts it, as the later
 * games count the type Trick-or-Treat and Forest's Curse add (the weather
 * damage and its immunities, the Poison and Steel types against poison,
 * same-type power, the abilities and moves that test a type).  It goes
 * when the Pokemon's effects are reset (it leaves the field, or a new one
 * comes in) and when the battle frees it (the guards' notes go then too).
 * With no move of those bases used each function is the game's own.
 *
 * symbols:
 *   BTL_POKEPARAM_HasSick                0x021D62C4  thumb  ov093
 *   BTL_POKEPARAM_ClearSubstitute        0x021D6D70  thumb  ov093
 *   BTL_POKEPARAM_ClearSickConts         0x021D63B8  thumb  ov093
 *   BTL_POKEPARAM_ClearIllusion          0x021D6C2C  thumb  ov093
 *   GFL_HEAP_Free                        0x020307B0  arm
 *   MI_CpuFill8                          0x02082BCC  arm
 *   POKE_TOOL_SetPartyValue              0x02017E40  thumb
 *   POKE_TOOL_RecalcParam                0x020185C4  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct PokemonParam;
struct BtlPokeParam;

/* btl_pokeparam_types.h: the fields these functions use (pointers are 4
 * bytes on both builds: the 64-bit one runs the game as wasm32) */
#define BPP_MON(bpp)            (*(struct PokemonParam **)((u8 *)(bpp) + 0x00))
#define BPP_BASE_ABILITY(bpp)   (*(u16 *)((u8 *)(bpp) + 0x16))
/* baseForm : 5, with a form past 31 from sdk/mon_form.c (patch_ov093_btl_pokeparam_form.c) */
extern unsigned int bw_bpp_base_form(const void *bpp, unsigned int bits);
#define BPP_BASE_FORM(bpp)      ((u8)bw_bpp_base_form((bpp), *((u8 *)(bpp) + 0x1B) & 0x1F))
#define BPP_TYPE1(bpp)          (*((u8 *)(bpp) + 0xF8))
#define BPP_TYPE2(bpp)          (*((u8 *)(bpp) + 0xF9))
#define BPP_STAGES(bpp)         ((u8 *)(bpp) + 0xFC)
#define BPP_ABILITY(bpp)        (*(u16 *)((u8 *)(bpp) + 0x13C))
#define BPP_FORM(bpp)           (*((u8 *)(bpp) + 0x141))
#define BPP_TURN_FLAGS(bpp)     ((u8 *)(bpp) + 0x153)
#define BPP_CONT_FLAGS(bpp)     ((u8 *)(bpp) + 0x155)

#define POKETYPE_FLYING         2
#define POKETYPE_NONE           31      /* (the port's "no type", arm9 poke_type.h) */
#define BTL_SICK_ROOST          0x18    /* the Flying type is hidden this turn (BTL_POKEPARAM_GetTypes) */
#define BTL_CONT_FLAG_BATON_PASS 0x0E   /* the switch-out keeps conditions, stages and flags */
#define POKE_PARAM_FORM         0x6F

extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_GetTypes(struct BtlPokeParam *pp, u8 *type1, u8 *type2);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern int BTL_POKEPARAM_TestContFlag(struct BtlPokeParam *pp, u32 flag);
extern void BTL_POKEPARAM_RestoreIfNeeded(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ResetBattleMoves(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ResetMoveHistory(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearMoveFlags(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearStoredPokeMove(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearIllusion(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearSubstitute(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_ClearSickConts(struct BtlPokeParam *pp, int clearMajor);
extern void BTL_POKEPARAM_InitStatStages(u8 *stages);
extern void BTL_POKEPARAM_ReloadFromMon(struct BtlPokeParam *pp, struct PokemonParam *mon, u32 withHp, u32 withAbility);
extern void GFL_HEAP_Free(void *p);
extern void MI_CpuFill8(void *dest, u8 data, u32 size);
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId, u32 value);
extern void POKE_TOOL_RecalcParam(struct PokemonParam *pp);

/* sdk/battle_moves.c */
extern int bw_bm_third_type(int pokeId, const void *poke);
extern void bw_bm_poke_reset(int pokeId, const void *poke);
extern void bw_bm_poke_left(int pokeId, const void *poke);

int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type)
{
    u8 type1;
    u8 type2;
    int third;

    if (type != POKETYPE_NONE) {
        BTL_POKEPARAM_GetTypes(pp, &type1, &type2);
        if (type1 == type || type2 == type) return 1;
        third = bw_bm_third_type(BTL_POKEPARAM_GetPokeId(pp), pp);
        if (third == type && !(third == POKETYPE_FLYING && BTL_POKEPARAM_HasSick(pp, BTL_SICK_ROOST))) return 1;
    }
    return 0;
}

void BTL_POKEPARAM_Delete(struct BtlPokeParam *pp)
{
    bw_bm_poke_reset(BTL_POKEPARAM_GetPokeId(pp), pp);
    GFL_HEAP_Free(pp);
}

void BTL_POKEPARAM_ResetAllEffects(struct BtlPokeParam *bpp)
{
    bw_bm_poke_left(BTL_POKEPARAM_GetPokeId(bpp), bpp);
    MI_CpuFill8(BPP_TURN_FLAGS(bpp), 0, 2);
    MI_CpuFill8(BPP_CONT_FLAGS(bpp), 0, 2);
    BTL_POKEPARAM_RestoreIfNeeded(bpp);
    BTL_POKEPARAM_ResetBattleMoves(bpp);
    BTL_POKEPARAM_ResetMoveHistory(bpp);
    BTL_POKEPARAM_ClearMoveFlags(bpp);
    BTL_POKEPARAM_ClearStoredPokeMove(bpp);
    BTL_POKEPARAM_ClearIllusion(bpp);
    BTL_POKEPARAM_ClearSubstitute(bpp);
    BTL_POKEPARAM_ClearSickConts(bpp, 1);
    BTL_POKEPARAM_InitStatStages(BPP_STAGES(bpp));
    BPP_FORM(bpp) = BPP_BASE_FORM(bpp);
    BPP_ABILITY(bpp) = BPP_BASE_ABILITY(bpp);
    POKE_TOOL_SetPartyValue(BPP_MON(bpp), POKE_PARAM_FORM, BPP_FORM(bpp));
    POKE_TOOL_RecalcParam(BPP_MON(bpp));
}

/* (the third type goes even when Baton Pass keeps the rest: it is the
 * Pokemon's own, not one Baton Pass passes) */
void BTL_POKEPARAM_ResetEffectsOnSwitchOut(struct BtlPokeParam *bpp)
{
    bw_bm_poke_left(BTL_POKEPARAM_GetPokeId(bpp), bpp);
    MI_CpuFill8(BPP_TURN_FLAGS(bpp), 0, 2);
    BTL_POKEPARAM_RestoreIfNeeded(bpp);
    BTL_POKEPARAM_ResetBattleMoves(bpp);
    BTL_POKEPARAM_ResetMoveHistory(bpp);
    BTL_POKEPARAM_ClearMoveFlags(bpp);
    BTL_POKEPARAM_ClearStoredPokeMove(bpp);
    BTL_POKEPARAM_ClearIllusion(bpp);
    if (BTL_POKEPARAM_TestContFlag(bpp, BTL_CONT_FLAG_BATON_PASS) == 0) {
        BTL_POKEPARAM_ClearSubstitute(bpp);
        BTL_POKEPARAM_ClearSickConts(bpp, 0);
        BTL_POKEPARAM_InitStatStages(BPP_STAGES(bpp));
        MI_CpuFill8(BPP_CONT_FLAGS(bpp), 0, 2);
    }
    BPP_FORM(bpp) = BPP_BASE_FORM(bpp);
    BPP_ABILITY(bpp) = BPP_BASE_ABILITY(bpp);
    POKE_TOOL_SetPartyValue(BPP_MON(bpp), POKE_PARAM_FORM, BPP_FORM(bpp));
    POKE_TOOL_RecalcParam(BPP_MON(bpp));
}

void BTL_POKEPARAM_ReloadAndResetEffects(struct BtlPokeParam *bpp)
{
    bw_bm_poke_left(BTL_POKEPARAM_GetPokeId(bpp), bpp);
    BTL_POKEPARAM_ReloadFromMon(bpp, BPP_MON(bpp), 0, 1);
    MI_CpuFill8(BPP_CONT_FLAGS(bpp), 0, 2);
    MI_CpuFill8(BPP_TURN_FLAGS(bpp), 0, 2);
    BTL_POKEPARAM_ClearSubstitute(bpp);
    BTL_POKEPARAM_ResetMoveHistory(bpp);
    BTL_POKEPARAM_ClearSickConts(bpp, 0);
    BTL_POKEPARAM_InitStatStages(BPP_STAGES(bpp));
}
