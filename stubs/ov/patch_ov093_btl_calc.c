/*
 * patches: src/game/ov093/btl_calc.c BTL_CALC_RollPercent BTL_CALC_DivideMaxHp BTL_CALC_DivideMaxHpMin1
 *
 * Two of the places a running handler uses a number a mod can change for
 * its move, ability or item (sdk/battle_handlers.h, eidos/tables/
 * *_handlers.txt): a percent roll (`chance`) and an HP fraction (`hp`, the
 * divisor of the Pokemon's maximum HP).  Outside a handler, and for an id
 * with no table line, the functions are the game's own.
 */

typedef unsigned int u32;

struct BtlPokeParam;

#define BTL_BPP_VALUE_MAX_HP 14

u32 BTL_CALC_Random(u32 range);
int BTL_POKEPARAM_GetValue(struct BtlPokeParam *bpp, int which);

/* sdk/battle_handlers.c */
extern u32 bw_bh_chance(u32 percent);
extern u32 bw_bh_hp(u32 divisor);

int BTL_CALC_RollPercent(u32 percent)
{
    percent = bw_bh_chance(percent);
    if (BTL_CALC_Random(100) < percent) return 1;
    return 0;
}

u32 BTL_CALC_DivideMaxHp(struct BtlPokeParam *bpp, u32 divisor)
{
    divisor = bw_bh_hp(divisor);
    return (u32)BTL_POKEPARAM_GetValue(bpp, BTL_BPP_VALUE_MAX_HP) / divisor;
}

int BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *bpp, u32 divisor)
{
    u32 result = BTL_POKEPARAM_GetValue(bpp, BTL_BPP_VALUE_MAX_HP);

    divisor = bw_bh_hp(divisor);
    result = result / divisor;
    if (result == 0)
        result = 1;
    return result;
}
