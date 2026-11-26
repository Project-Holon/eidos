/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_CommitCmdRecord
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
 * symbols:
 *   BTL_CALC_MakeDefaultSickCont  0x021D7D18  thumb  ov093
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
