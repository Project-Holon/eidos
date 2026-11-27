/*
 * patches: src/game/ov093/hand_tokusei.c HAND_TOKUSEI_AnticipationFoeIsThreat
 *
 * Anticipation shudders when a foe knows a move super effective against
 * the holder: a third type a move gave the holder counts (add_type,
 * Trick-or-Treat, Forest's Curse: sdk/battle_moves.h), as it counts for
 * every other effectiveness.  With no third type the function is the
 * game's own.
 *
 * symbols:
 *   BTL_POKEPARAM_GetTypePair       0x021D57FC  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId         0x021D5440  thumb  ov093
 *   BTL_POKEPARAM_GetMoveCount      0x021D5448  thumb  ov093
 *   BTL_POKEPARAM_GetMoveId         0x021D54C0  thumb  ov093
 *   BTL_CALC_GetTypeAffinity        0x021D794C  thumb  ov093
 *   BTL_CALC_GetTypeAffinityVsPair  0x021D799C  thumb  ov093
 *   WAZA_TOOL_GetCategory           0x0201C1BC  thumb
 *   WAZA_TOOL_GetType               0x0201C1C8  thumb
 *   WAZA_TOOL_IsDamaging            0x0201C314  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlPokeParam;

#define MOVE_CATEGORY_OHKO      9       /* (hand_tokusei.c) */
#define AFFINITY_NONE           0
#define AFFINITY_NORMAL         3

extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetMoveCount(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetMoveId(struct BtlPokeParam *pp, u8 slot);
extern int BTL_CALC_GetTypeAffinity(int atkType, int defType);
extern int BTL_CALC_GetTypeAffinityVsPair(int atkType, int defTypes);
extern u32 WAZA_TOOL_GetCategory(u16 move);
extern u32 WAZA_TOOL_GetType(u16 move);
extern u32 WAZA_TOOL_IsDamaging(u16 move);

/* sdk/battle_moves.c */
extern int bw_bm_third_type(int pokeId, const void *poke);

/* Whether the foe has a one-hit KO move or a damaging move super effective against myPoke. */
int HAND_TOKUSEI_AnticipationFoeIsThreat(struct BtlPokeParam *myPoke, struct BtlPokeParam *foe)
{
    u16 myTypes;
    u8 moveCount;
    u8 i;
    u16 moveId;
    int third, affinity;

    myTypes = BTL_POKEPARAM_GetTypePair(myPoke);
    third = bw_bm_third_type(BTL_POKEPARAM_GetPokeId(myPoke), myPoke);
    moveCount = BTL_POKEPARAM_GetMoveCount(foe);
    for (i = 0; i < moveCount; i++) {
        moveId = BTL_POKEPARAM_GetMoveId(foe, i);
        if (WAZA_TOOL_GetCategory(moveId) == MOVE_CATEGORY_OHKO)
            return 1;
        if (WAZA_TOOL_IsDamaging(moveId) != 0) {
            affinity = BTL_CALC_GetTypeAffinityVsPair((int)WAZA_TOOL_GetType(moveId), myTypes);
            if (third >= 0 && affinity != AFFINITY_NONE) {
                int t = BTL_CALC_GetTypeAffinity((int)WAZA_TOOL_GetType(moveId), third);

                /* (doublings add up: the classes' combine stops at four times) */
                affinity = t == AFFINITY_NONE ? AFFINITY_NONE : affinity + t - AFFINITY_NORMAL;
            }
            if (affinity > AFFINITY_NORMAL)
                return 1;
        }
    }
    return 0;
}
