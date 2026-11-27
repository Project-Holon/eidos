/*
 * patches: src/game/ov093/hand_item.c HAND_ITEM_PowerHerb_CheckSkipCharge HAND_ITEM_BlackSludge_HealOrDamage
 *
 * Power Herb skips the charge turn of every two-turn move except Sky Drop,
 * tested by the move's id: a mod's move that works like Sky Drop
 * (sdk/battle_handlers.h bw_bh_move_is, eidos/tables/move_handlers.txt) is
 * not skipped either.
 *
 * Black Sludge heals a Poison-type holder and hurts any other: a third type
 * a move gave the holder counts (add_type, sdk/battle_moves.h), as the
 * later games count it.  With no table lines both are the game's handlers.
 *
 * symbols:
 *   BTL_SERVER_FLOW_CheckShowdownThunk   0x021C83D4  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeParam         0x021C7CF4  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord       0x021C88A4  thumb  ov093
 *   BTL_SERVER_FLOW_CommitCmdRecord      0x021C88C8  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord        0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg   0x021C8860  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeViewPos       0x021C8010  thumb  ov093
 *   BTL_POKEPARAM_HasType                0x021D5814  thumb  ov093
 *   BTL_CALC_DivideMaxHpMin1             0x021D7B74  thumb  ov093
 *   BTL_EVENT_GetFactorKey               0x021D70A8  thumb  ov093
 *   HAND_ITEM_GetParam                   0x021DCFF8  thumb  ov093
 */

struct BtlEventFactor;

#define BTL_EVAR_ATTACKER_ID         0x03
#define BTL_EVAR_MOVE_ID             0x12
#define BTL_EVAR_FORCE_FLOATING_FLAG 0x51   /* (the charge flow's "skip" flag) */
#define BTL_MOVE_SKY_DROP            507

extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);
extern int BTL_EVENT_OverwriteVar(int key, __UINTPTR_TYPE__ value);

/* sdk/battle_handlers.c */
extern int bw_bh_move_is(int move, int builtin);

/* Power Herb: a two-turn move (except Sky Drop) skips its charge turn. */
void HAND_ITEM_PowerHerb_CheckSkipCharge(struct BtlEventFactor *factor, void *flow, int pokeId, int *work)
{
    (void)factor;
    (void)flow;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        if (!bw_bh_move_is((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), BTL_MOVE_SKY_DROP)) {
            if (BTL_EVENT_OverwriteVar(BTL_EVAR_FORCE_FLOATING_FLAG, 1))
                *work = 1;
        }
    }
}

/* hand_common_types.h: a message the flow shows */
typedef struct {
    unsigned short strId;
    unsigned short typeAndCount;
    unsigned int args[8];
} BtlStrParam;

/* HANDEX_KIND_RECOVER_HP */
typedef struct {
    unsigned int header;
    unsigned short amount;
    unsigned char pokeId;
    unsigned char pad07;
    BtlStrParam message;
} HandExRecoverHp;

/* kind 7: lose hit points (hand_item_types.h HandExLoseHp) */
typedef struct {
    unsigned int header;
    unsigned short amount;
    unsigned char pokeId;
    unsigned char flags;
    unsigned short effectId;
    unsigned char effectParam;
    unsigned char effectKind;
    BtlStrParam message;
} HandExLoseHp;

_Static_assert(__builtin_offsetof(HandExLoseHp, message) == 0x0C, "HandExLoseHp.message");

#define BTL_EVAR_POKE_ID          0x02
#define HANDEX_KIND_RECOVER_HP    5
#define HANDEX_KIND_LOSE_HP       7
#define BTL_STRTYPE_SET           2
#define POKETYPE_POISON           3
/* port_limits.h BW_LIMIT_BTLV_EFFECT_MOVE_END: the first effect after the
 * moves' animations (the port answers it at run time) */
#define BTLV_EFFECT_MOVE_END      bw_limit_get(2)

struct BtlPokeParam;

extern unsigned int bw_limit_get(int id);
extern int BTL_SERVER_FLOW_CheckShowdownThunk(void *flow);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(void *flow, int pokeId);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(void *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_CommitCmdRecord(void *flow, void *record);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, unsigned int arg);
extern unsigned char BTL_SERVER_FLOW_GetPokeViewPos(void *flow, int pokeId);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern unsigned short BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, unsigned int divisor);
extern unsigned short BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern int HAND_ITEM_GetParam(struct BtlEventFactor *factor, int paramIndex);

/*
 * Black Sludge: a Poison type holder recovers (item parameter) of its maximum
 * HP at the end of the turn; any other holder loses an eighth.
 */
void HAND_ITEM_BlackSludge_HealOrDamage(struct BtlEventFactor *factor, void *flow, int pokeId)
{
    struct BtlPokeParam *holder;
    HandExRecoverHp *heal;
    HandExLoseHp *hurt;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) && BTL_SERVER_FLOW_CheckShowdownThunk(flow) == 0) {
        holder = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        if (BTL_POKEPARAM_HasType(holder, POKETYPE_POISON)) {
            heal = (HandExRecoverHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RECOVER_HP, pokeId);
            heal->pokeId = (unsigned char)pokeId;
            heal->amount = BTL_CALC_DivideMaxHpMin1(holder, (unsigned int)HAND_ITEM_GetParam(factor, 2));
            BTL_SERVER_FLOW_InitMsgRecord(&heal->message, BTL_STRTYPE_SET, 0x392);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&heal->message, (unsigned int)pokeId);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&heal->message, BTL_EVENT_GetFactorKey(factor));
            BTL_SERVER_FLOW_CommitCmdRecord(flow, heal);
        } else {
            hurt = (HandExLoseHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_LOSE_HP, pokeId);
            hurt->pokeId = (unsigned char)pokeId;
            hurt->amount = BTL_CALC_DivideMaxHpMin1(holder, 8);
            hurt->flags |= 2;
            hurt->effectId = (unsigned short)(BTLV_EFFECT_MOVE_END + 46);
            hurt->effectParam = BTL_SERVER_FLOW_GetPokeViewPos(flow, pokeId);
            hurt->effectKind = 6;
            BTL_SERVER_FLOW_InitMsgRecord(&hurt->message, BTL_STRTYPE_SET, 0x40e);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&hurt->message, (unsigned int)pokeId);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&hurt->message, BTL_EVENT_GetFactorKey(factor));
            BTL_SERVER_FLOW_CommitCmdRecord(flow, hurt);
        }
    }
}
