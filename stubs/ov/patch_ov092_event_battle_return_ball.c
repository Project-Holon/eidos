/*
 * patches: src/game/ov092/event_battle_return.c EVENT_BATTLE_RETURN_ApplyHealBall
 *
 * What the ball does to the Pokemon just caught, by the ball id it keeps
 * (a mod's eidos/tables/balls.txt, sdk/balls.h): a Heal Ball, or a ball
 * that works like it or has `heal=1`, gives it full HP and no status, as
 * the game does for the Heal Ball; a ball with `friendship=N` sets its
 * friendship to N (the game has no such ball: eidos's own, for a Friend
 * Ball style).  With no table line this is the game's function.
 *
 * symbols:
 *   POKE_TOOL_GetPartyValue       0x02017E1C  thumb
 *   POKE_TOOL_SetPartyValue       0x02017E40  thumb
 *   POKE_TOOL_SetStatusCondition  0x02017F9C  thumb
 *   POKE_TOOL_DecodePartyParam    0x02017D30  thumb  arm9
 *   POKE_TOOL_EncodePartyParam    0x02017D70  thumb  arm9
 */

typedef unsigned int u32;
typedef __UINTPTR_TYPE__ PokeToolArg;   /* (arm9 poke_tool_shared_types.h: an address-sized value off the DS) */

struct PokemonParam;

extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg arg);
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg value);
extern void POKE_TOOL_SetStatusCondition(struct PokemonParam *pp, u32 statusCondition);
extern u32 POKE_TOOL_DecodePartyParam(struct PokemonParam *pp);
extern u32 POKE_TOOL_EncodePartyParam(struct PokemonParam *pp, u32 didDecode);

/* sdk/balls.c */
extern int bw_ball_heals(int ballId);
extern int bw_ball_catch_friendship(int ballId);

/* Parameter ids of the get/set party-value accessors. */
#define MON_PARAM_FRIENDSHIP 0x09
#define MON_PARAM_BALL       0x98
#define MON_PARAM_HP         0xa0
#define MON_PARAM_MAX_HP     0xa1

void EVENT_BATTLE_RETURN_ApplyHealBall(struct PokemonParam *mon)
{
    u32 prevState;
    int ball;
    int friendship;

    prevState = POKE_TOOL_DecodePartyParam(mon);
    ball = (int)POKE_TOOL_GetPartyValue(mon, MON_PARAM_BALL, 0);
    if (bw_ball_heals(ball)) {
        POKE_TOOL_SetPartyValue(mon, MON_PARAM_HP, POKE_TOOL_GetPartyValue(mon, MON_PARAM_MAX_HP, 0));
        POKE_TOOL_SetStatusCondition(mon, 0);
    }
    friendship = bw_ball_catch_friendship(ball);
    if (friendship >= 0) {
        POKE_TOOL_SetPartyValue(mon, MON_PARAM_FRIENDSHIP, (PokeToolArg)friendship);
    }
    POKE_TOOL_EncodePartyParam(mon, prevState);
}
