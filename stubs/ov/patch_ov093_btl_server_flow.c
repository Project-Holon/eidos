/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_CommitCmdRecord BTL_SERVER_FLOW_ItemEffectRecoverHp BTL_SERVER_FLOW_ItemEffectRevive
 *
 * A command a running handler commits takes the numbers a mod gives its
 * move, ability or item (sdk/battle_handlers.h, eidos/tables/
 * *_handlers.txt): a stat change (kind 0xE) its stat and amount, a status
 * (kind 0xC) its status (with that status's own default duration, as the
 * game's handlers make it: BTL_CALC_MakeDefaultSickCont), a weather (kind
 * 0x1D) its weather.  The record is changed in place before the game runs
 * it; with no table line for the running id nothing changes, and the
 * function is the game's own.
 *
 * The bag's HP items in battle (ItemEffectRecoverHp, ItemEffectRevive) heal
 * what a mod's eidos/tables/item_heal.txt gives the item (sdk/item_heal.h),
 * at most the HP the Pokemon is missing; an item with no line heals the
 * record's byte, as the game does.
 *
 * symbols:
 *   BTL_CALC_MakeDefaultSickCont        0x021D7D18  thumb  ov093
 *   BTL_POKEPARAM_IsHpFull              0x021D5B48  thumb  ov093
 *   BTL_POKEPARAM_IsFainted             0x021D5B68  thumb  ov093
 *   BTL_POKEPARAM_GetValue              0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId             0x021D5440  thumb  ov093
 *   BTL_CALC_GetItemParam               0x021D805C  thumb  ov093
 *   BTL_CALC_DivideMaxHpMin1            0x021D7B74  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord      0x021C88A4  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord       0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg  0x021C8860  thumb  ov093
 */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u32;

struct BtlServerFlow;

/* btl_server_flow.c: the head word of every command record */
typedef struct {
    u32 id : 8;
    u32 client : 5;
    u32 pad13 : 11;
    u32 ignoreIfBusy : 1;
    u32 checkFainted : 1;
    u32 pad26 : 6;
} BtlCmd;

/* hand_common_types.h: a stat stage change (HANDEX_KIND_RANK_EFFECT) */
typedef struct {
    u32 header;
    u32 statId;
    u32 pad08;
    s8 amount;
    u8 pad0D;
    u8 showMessage;
    u8 targetCount;
    u8 targets[1];
} BtlRankCmd;

/* btl_pokeparam_types.h: a status's duration word (only its whole word here) */
union SickCont {
    u32 raw;
};

/* hand_common_types.h: a status to add (HANDEX_KIND_ADD_SICK), its head */
typedef struct {
    u32 header;
    u32 sickKind;
    union SickCont sickCont;
} HandExAddSick;

/* hand_tokusei_types.h: a weather change (kind 0x1d), its head */
typedef struct {
    u32 flags;
    u8 weather;
    u8 turns;
} TokuseiWeatherCmd;

#define HANDEX_KIND_ADD_SICK    12
#define HANDEX_KIND_RANK_EFFECT 14
#define HANDEX_KIND_WEATHER     0x1D

typedef struct { unsigned low : 10, size : 9, high : 13; } QueueA;
typedef struct { unsigned low : 13, size : 10, high : 9; } QueueB;

/* the server flow's cmdLock word (btl_server_flow_types.h, 0x1D74) */
#define FLOW_CMD_LOCK(flow) ((u8 *)(flow) + 0x1D74)

void BTL_SERVER_FLOW_ExecCmd(struct BtlServerFlow *flow, void *cmd);
void BTL_SERVER_FLOW_FreeLastHandExRecord(QueueA *pack, QueueB *record);
union SickCont BTL_CALC_MakeDefaultSickCont(int kind);

/* sdk/battle_handlers.c */
extern void bw_bh_rank(u32 *stat, s8 *amount);
extern int bw_bh_sick(u32 *kind);
extern void bw_bh_weather(u8 *weather);

void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record)
{
    switch (((BtlCmd *)record)->id) {
    case HANDEX_KIND_RANK_EFFECT: {
        BtlRankCmd *rank = (BtlRankCmd *)record;

        bw_bh_rank(&rank->statId, &rank->amount);
        break;
    }
    case HANDEX_KIND_ADD_SICK: {
        HandExAddSick *sick = (HandExAddSick *)record;

        if (bw_bh_sick(&sick->sickKind)) {
            sick->sickCont = BTL_CALC_MakeDefaultSickCont((int)sick->sickKind);
        }
        break;
    }
    case HANDEX_KIND_WEATHER:
        bw_bh_weather(&((TokuseiWeatherCmd *)record)->weather);
        break;
    }
    BTL_SERVER_FLOW_ExecCmd(flow, record);
    BTL_SERVER_FLOW_FreeLastHandExRecord((QueueA *)FLOW_CMD_LOCK(flow), (QueueB *)record);
}

/* ------------------------------------------------------------------ */
/* The bag's HP items in battle                                        */
/* ------------------------------------------------------------------ */

struct BtlPokeParam;

/* btl_server_flow.c: kind 5 (HP change) and kind 0x2C (revive) records */
struct CmdValuePoke { u32 header; unsigned short value; u8 pokeId; u8 showMessage; };
struct CmdRevive { u32 header; u8 pokeId; u8 pad5; unsigned short amount; };

#define BTL_VALUE_HP     13
#define BTL_VALUE_MAX_HP 14

extern int BTL_POKEPARAM_IsHpFull(struct BtlPokeParam *poke);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *poke);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *poke, int which);
extern int BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *poke);
extern u32 BTL_CALC_GetItemParam(u32 itemId, int param);
extern u32 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *poke, u32 divisor);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_InitMsgRecord(void *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(void *message, u32 arg);
extern void BTL_SERVER_FLOW_AddSeenPokeThunk(struct BtlServerFlow *flow, int pokeId);

/* sdk/item_heal.c */
extern int bw_item_heal_hp(u32 item, u32 max_hp, u32 missing, u32 *amount);

int BTL_SERVER_FLOW_ItemEffectRecoverHp(struct BtlServerFlow *flow, struct BtlPokeParam *poke, u32 itemId)
{
    int id;
    u8 *r;
    u32 k, heal;

    if (BTL_POKEPARAM_IsHpFull(poke) == 0 && BTL_POKEPARAM_IsFainted(poke) == 0) {
        id = BTL_POKEPARAM_GetPokeId(poke);
        r = (u8 *)BTL_SERVER_FLOW_AllocCmdRecord(flow, 5, id);
        r[6] = id;
        r[7] = 1;
        if (bw_item_heal_hp(itemId, (u32)BTL_POKEPARAM_GetValue(poke, BTL_VALUE_MAX_HP),
                            (u32)(BTL_POKEPARAM_GetValue(poke, BTL_VALUE_MAX_HP) - BTL_POKEPARAM_GetValue(poke, BTL_VALUE_HP)),
                            &heal)) {
            ((struct CmdValuePoke *)r)->value = (unsigned short)heal;     /* (a mod's item_heal.txt) */
        } else {
            k = BTL_CALC_GetItemParam(itemId, 0x3a);
            switch (k) {
            case 0xff:
                ((struct CmdValuePoke *)r)->value = BTL_POKEPARAM_GetValue(poke, 14);
                break;
            case 0xfe:
                ((struct CmdValuePoke *)r)->value = BTL_CALC_DivideMaxHpMin1(poke, 2);
                break;
            case 0xfd:
                ((struct CmdValuePoke *)r)->value = BTL_CALC_DivideMaxHpMin1(poke, 4);
                break;
            default:
                ((struct CmdValuePoke *)r)->value = k;
                break;
            }
        }
        BTL_SERVER_FLOW_InitMsgRecord(r + 8, 2, 387);
        BTL_SERVER_FLOW_AppendMsgRecordArg(r + 8, (u32)id);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, r);
        return 1;
    }
    return 0;
}

int BTL_SERVER_FLOW_ItemEffectRevive(struct BtlServerFlow *flow, struct BtlPokeParam *poke, unsigned short itemId)
{
    int id;
    u8 *w;
    u32 r, heal;

    if (BTL_POKEPARAM_IsFainted(poke)) {
        id = BTL_POKEPARAM_GetPokeId(poke);
        w = (u8 *)BTL_SERVER_FLOW_AllocCmdRecord(flow, 0x2c, id);
        w[4] = id;
        if (bw_item_heal_hp(itemId, (u32)BTL_POKEPARAM_GetValue(poke, BTL_VALUE_MAX_HP),
                            (u32)BTL_POKEPARAM_GetValue(poke, BTL_VALUE_MAX_HP), &heal)) {
            ((struct CmdRevive *)w)->amount = (unsigned short)heal;      /* (a mod's item_heal.txt) */
        } else {
            r = BTL_CALC_GetItemParam(itemId, 0x3a);
            switch (r) {
            case 0xff:
                ((struct CmdRevive *)w)->amount = BTL_POKEPARAM_GetValue(poke, 0xe);
                break;
            case 0xfe:
                ((struct CmdRevive *)w)->amount = BTL_CALC_DivideMaxHpMin1(poke, 2);
                break;
            case 0xfd:
                ((struct CmdRevive *)w)->amount = BTL_CALC_DivideMaxHpMin1(poke, 4);
                break;
            default:
                ((struct CmdRevive *)w)->amount = BTL_CALC_GetItemParam(itemId, 0x3a);
                break;
            }
        }
        BTL_SERVER_FLOW_InitMsgRecord(w + 8, 2, 3);
        BTL_SERVER_FLOW_AppendMsgRecordArg(w + 8, (u32)id);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, w);
        BTL_SERVER_FLOW_AddSeenPokeThunk(flow, id);
        return 1;
    }
    return 0;
}
