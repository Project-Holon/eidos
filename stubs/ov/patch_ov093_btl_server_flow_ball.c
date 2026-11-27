/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_GetBallCatchRate
 *
 * A thrown ball's catch-rate factor, by the ball it works like and with
 * that ball's numbers from a mod's eidos/tables/balls.txt (sdk/balls.h):
 * an added ball that works like the Net Ball tests its own types, one
 * like the Timer Ball adds its own amount a turn.  The game's rules, by
 * the base's item:
 *
 *   Ultra Ball, Great Ball   ratio always (2.0, 1.5)
 *   Net Ball                 ratio on a Pokemon of `type` or `type.2`
 *                            (3.0; Water, Bug)
 *   Dive Ball                ratio in a fishing encounter (setup flag 1) or
 *                            on ground attribute 6 (3.5)
 *   Nest Ball                below `level` (30): (41 - level) / 10, at
 *                            most ratio (4.0)
 *   Repeat Ball              ratio on a species the Pokedex has caught (3.0)
 *   Timer Ball               1 + per_turn a turn (0.3), at most ratio (4.0)
 *   Dusk Ball                ratio on backgrounds 4 and 5, or at night (time
 *                            zones 3 and 4) on the backgrounds
 *                            BTL_ID_IsBallWithCatchBonus lists (3.5)
 *   Quick Ball               ratio while the turn count is below `turns`
 *                            (5.0; the first turn)
 *   any other                ratio (1.0)
 *
 * The Master Ball's catch is not a factor: BTL_SERVER_FLOW_CalcCaptureShakes
 * (stubs/ov/patch_ov093_btl_server_flow_capture.c).  An item with no line
 * is its own base with the game's numbers: the game's function.
 *
 * This file defines the function alone and calls nothing that calls it, so
 * every call reaches it through its declared row's slot (a mod's hook on
 * the catch rate, catch.attempt's own scaling, sdk/mod_event_battle.c).
 *
 * symbols:
 *   BTL_MAIN_GetFieldSituation     0x021B8714  thumb  ov093
 *   BTL_MAIN_CheckZukanSeen        0x021B8724  thumb  ov093
 *   BTL_MAIN_CheckSetupStatusFlag  0x021BA1A8  thumb  ov093
 *   BTL_POKEPARAM_HasType          0x021D5814  thumb  ov093
 *   BTL_POKEPARAM_GetValue         0x021D59B4  thumb  ov093
 *   TIMEZONE_GetTimeZoneOfHour     0x020113F0  thumb
 *   BTL_ID_IsBallWithCatchBonus    0x0689C9A0  thumb  ov095
 */

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int u32;

struct BtlMain;
struct BtlPokeParam;

/* src/game/ov093/btl_server_flow_types.h: BtlServerFlow, its head */
typedef struct BtlServerFlowHead {
    void *server;
    struct BtlMain *mainModule;
    void *pokeCon;
    void *cmdQueue;
    u32 turnCount;
} BtlServerFlowHead;

/* src/game/arm9/btl_setup_types.h */
struct FieldSituation {
    int bgType;
    int bgAttr;
    unsigned char weather;
    unsigned char season;
    unsigned short zoneId;
    unsigned char hour;
    unsigned char minute;
};

extern struct FieldSituation *BTL_MAIN_GetFieldSituation(struct BtlMain *main);
extern int BTL_MAIN_CheckZukanSeen(struct BtlMain *main, void *mon);
extern int BTL_MAIN_CheckSetupStatusFlag(struct BtlMain *main, int mask);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId);
extern int TIMEZONE_GetTimeZoneOfHour(int season, int hour);
extern int BTL_ID_IsBallWithCatchBonus(u16 itemId);

/* sdk/balls.c */
extern int bw_ball_catch_base(int item);
extern int bw_ball_catch_value(int item, int which);

/* sdk/balls.h: the numbers of a catch rule */
#define BW_BALL_RATIO    0
#define BW_BALL_PER_TURN 1
#define BW_BALL_TYPE     2
#define BW_BALL_TYPE_2   3
#define BW_BALL_LEVEL    4
#define BW_BALL_TURNS    5

#define ITEM_ULTRA_BALL  2
#define ITEM_GREAT_BALL  3
#define ITEM_NET_BALL    6
#define ITEM_DIVE_BALL   7
#define ITEM_NEST_BALL   8
#define ITEM_REPEAT_BALL 9
#define ITEM_TIMER_BALL  10
#define ITEM_DUSK_BALL   13
#define ITEM_QUICK_BALL  15

#define BTL_VALUE_LEVEL  15

int BTL_SERVER_FLOW_GetBallCatchRate(BtlServerFlowHead *flow, void *unused, struct BtlPokeParam *target, int ballKind)
{
    u16 n;
    float f;
    int value;
    int limit;
    u8 flag;
    struct FieldSituation *t;

    (void)unused;
    switch (bw_ball_catch_base(ballKind)) {
    case ITEM_GREAT_BALL: return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
    case ITEM_ULTRA_BALL: return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
    case ITEM_NET_BALL:
        if (BTL_POKEPARAM_HasType(target, bw_ball_catch_value(ballKind, BW_BALL_TYPE))
            || BTL_POKEPARAM_HasType(target, bw_ball_catch_value(ballKind, BW_BALL_TYPE_2)))
            return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        break;
    case ITEM_DIVE_BALL:
        if (BTL_MAIN_CheckSetupStatusFlag(flow->mainModule, 1)) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        if (BTL_MAIN_GetFieldSituation(flow->mainModule)->bgAttr == 6) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        break;
    case ITEM_NEST_BALL:
        n = BTL_POKEPARAM_GetValue(target, BTL_VALUE_LEVEL);
        if (n < bw_ball_catch_value(ballKind, BW_BALL_LEVEL)) {
            n = 41 - n;
            if (n > 40) n = 40;
            if (n != 0) f = 0.5f + (float)(n << 12);
            else f = (float)(n << 12) - 0.5f;
            value = (int)f / 10;
            limit = bw_ball_catch_value(ballKind, BW_BALL_RATIO);
            return value > limit ? limit : value;
        }
        break;
    case ITEM_REPEAT_BALL:
        if (BTL_MAIN_CheckZukanSeen(flow->mainModule, target)) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        break;
    case ITEM_TIMER_BALL:
        value = flow->turnCount * bw_ball_catch_value(ballKind, BW_BALL_PER_TURN) + 0x1000;
        limit = bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        if (value > limit) value = limit;
        return value;
    case ITEM_DUSK_BALL:
        t = BTL_MAIN_GetFieldSituation(flow->mainModule);
        flag = TIMEZONE_GetTimeZoneOfHour(t->season, t->hour);
        if ((unsigned)(t->bgType - 4) <= 1) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        if (BTL_ID_IsBallWithCatchBonus(t->bgType)) {
            if ((u8)(flag + 253) <= 1) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        }
        break;
    case ITEM_QUICK_BALL:
        if (flow->turnCount < (u32)bw_ball_catch_value(ballKind, BW_BALL_TURNS)) return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
        break;
    default:
        return bw_ball_catch_value(ballKind, BW_BALL_RATIO);
    }
    return 0x1000;
}
