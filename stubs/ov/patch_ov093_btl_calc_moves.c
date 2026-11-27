/*
 * patches: src/game/ov093/btl_calc.c BTL_CALC_ContainsU16 BTL_CALC_MakeSickContTurns BTL_CALC_ApplyTypeAffinity
 *
 * BTL_CALC_ContainsU16's one caller is BTL_CALC_PickRandomMoveExcluding,
 * Metronome's pick: a mod's move that works like one Metronome cannot pick
 * (sdk/battle_handlers.h bw_bh_move_in, eidos/tables/move_handlers.txt) is
 * not picked either.  BTL_CALC_MakeSickContTurns, a duration a handler
 * makes (Rest's sleep), takes the running id's `turns` key where the
 * handler table lists the place.  BTL_CALC_ApplyTypeAffinity scales the
 * damage by the effectiveness class, and by the doublings past four times
 * or a quarter that three types or a second type give (8 and 1/8 times;
 * patch_ov093_btl_server_flow_moves.c keeps them).  With no table lines
 * all three are the game's.
 *
 * symbols:
 *   BTL_EVENT_GetVar        0x021D775C  thumb  ov093
 */

typedef unsigned short u16;
typedef unsigned int u32;

/* btl_pokeparam_types.h: union SickCont, byPart: kind 3 bits, the turns 6 bits */
union SickCont {
    u32 raw;
};

/* sdk/battle_handlers.c */
extern int bw_bh_move_in(int move, const u16 *list, int count);
extern u32 bw_bh_turns(u32 turns);

/* Record that lasts the given number of turns. */
union SickCont BTL_CALC_MakeSickContTurns(u32 turns)
{
    union SickCont sickCont;

    turns = bw_bh_turns(turns);
    sickCont.raw = 2 | ((turns & 0x3f) << 3);
    return sickCont;
}

/* True when the value is in the list of halfwords, or is a mod's move that
 * works like one of them. */
int BTL_CALC_ContainsU16(const u16 *list, u32 count, u32 value)
{
    return bw_bh_move_in((int)value, list, (int)count);
}

/* the event variables of BTL_SERVER_FLOW_CalcMoveDamage, the one caller */
#define BTL_EVAR_ATTACKER_ID    0x03
#define BTL_EVAR_DEFENDER_ID    0x04
#define BTL_EVAR_MOVE_ID        0x12

extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);

/* sdk/battle_moves.c */
extern int bw_bm_affinity_extra(int attackerId, int defenderId, int move);

/* Scales a damage value by an affinity class (none, quarter, half, normal,
 * double, quadruple), and by the doublings or halvings past the class the
 * effectiveness step kept for this attacker, defender and move. */
u32 BTL_CALC_ApplyTypeAffinity(u32 damage, u32 affinity)
{
    int extra;

    switch (affinity) {
    case 0: return 0;
    case 1: damage /= 4; break;
    case 2: damage /= 2; break;
    case 3: break;
    case 4: damage *= 2; break;
    case 5: damage *= 4; break;
    }
    if (affinity == 1 || affinity == 5) {
        extra = bw_bm_affinity_extra((int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID), (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID),
                                     (int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID));
        for (; extra > 0; extra--) {
            damage *= 2;
        }
        for (; extra < 0; extra++) {
            damage /= 2;
        }
    }
    return damage;
}
