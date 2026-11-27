/*
 * patches: src/game/ov093/hand_item.c HAND_ITEM_IsHpAtOrBelowFraction
 *
 * Gluttony outside its handlers: a pinch berry's HP fraction above a half
 * is halved for a Pokemon with Gluttony (a quarter-HP berry is eaten at
 * half HP).  A mod's ability that works like Gluttony
 * (sdk/battle_handlers.h bw_bh_ability_is, eidos/tables/ability_handlers.txt)
 * counts as Gluttony.  With no table lines the function is the game's.
 */

typedef unsigned int u32;

struct BtlServerFlow;
struct BtlPokeParam;

/* hand_item_types.h, btl_pokeparam value ids */
#define BTL_VALUE_HP          13
#define BTL_VALUE_MAX_HP      14
#define BTL_VALUE_ABILITY     0x11      /* the ability, 0 while Gastro Acid holds it */
#define BTL_ABILITY_GLUTTONY  0x52

extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern u32 BTL_CALC_DivideMaxHp(struct BtlPokeParam *bpp, u32 divisor);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

/* True when the Pokemon's HP is at or below max HP / fraction (Gluttony halves a fraction above 2). */
int HAND_ITEM_IsHpAtOrBelowFraction(struct BtlServerFlow *flow, int pokeId, u32 fraction)
{
    struct BtlPokeParam *poke;
    u32 hp;

    poke = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
    if (bw_bh_ability_is(BTL_POKEPARAM_GetValue(poke, BTL_VALUE_ABILITY), BTL_ABILITY_GLUTTONY) && fraction > 2)
        fraction >>= 1;
    if (fraction == 0)
        fraction = 1;
    if ((u32)BTL_POKEPARAM_GetValue(poke, BTL_VALUE_MAX_HP) > 1) {
        hp = BTL_POKEPARAM_GetValue(poke, BTL_VALUE_HP);
        if (hp <= BTL_CALC_DivideMaxHp(poke, fraction))
            return 1;
    }
    return 0;
}
