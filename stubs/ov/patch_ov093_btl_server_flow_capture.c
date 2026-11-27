/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_CalcCaptureShakes
 *
 * The capture roll: a ball that works like the Master Ball (a mod's
 * eidos/tables/balls.txt line with base 1, sdk/balls.h) catches at once
 * with three shakes, as the Master Ball itself does.  Every other ball goes
 * the game's way.  With no table line this is the game's function.
 *
 * A file of its own, apart from BTL_SERVER_FLOW_GetBallCatchRate's patch:
 * the call to the catch rate then goes through that row's slot, so a mod's
 * hook on it and catch.attempt's scaling still apply.
 *
 * symbols:
 *   BTL_MAIN_CheckSetupStatusFlag        0x021BA1A8  thumb  ov093
 *   BTL_POKEPARAM_GetValue               0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_GetSpecies             0x021D5444  thumb  ov093
 *   BTL_POKEPARAM_GetMajorStatusIndex    0x021D62A8  thumb  ov093
 *   BTL_CALC_Random                      0x021D78AC  thumb  ov093
 *   BTL_SERVER_FLOW_GetCaptureDexFactor  0x021CBCF4  thumb  ov093
 *   BTL_SERVER_FLOW_GetBallCatchRate     0x021CBD48  thumb  ov093
 *   BTL_SERVER_FLOW_RollCriticalCapture  0x021CBEA8  thumb  ov093
 *   POKE_TOOL_GetPersonalParam           0x02019C90  thumb
 *   GPOWER_ApplyCaptureRate              0x020111BC  thumb
 *   FX_Div                               0x0207C768  arm
 *   FX_Sqrt                              0x0207C7B4  arm
 */

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef long long s64;
typedef int fx32;

struct BtlMain;
struct BtlPokeParam;

/* src/game/ov093/btl_server_flow_types.h: BtlServerFlow, its head */
typedef struct BtlServerFlowHead {
    void *server;
    struct BtlMain *mainModule;
} BtlServerFlowHead;

extern int BTL_MAIN_CheckSetupStatusFlag(struct BtlMain *main, int mask);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId);
extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_GetMajorStatusIndex(struct BtlPokeParam *bpp);
extern u32 BTL_CALC_Random(u32 range);
extern u32 BTL_SERVER_FLOW_GetCaptureDexFactor(BtlServerFlowHead *flow);
extern int BTL_SERVER_FLOW_GetBallCatchRate(BtlServerFlowHead *flow, void *unused, struct BtlPokeParam *target,
                                            int ballKind);
extern int BTL_SERVER_FLOW_RollCriticalCapture(BtlServerFlowHead *flow, int catchRate);
extern u32 POKE_TOOL_GetPersonalParam(u32 species, u32 form, u32 field);
extern int GPOWER_ApplyCaptureRate(int rate);
extern fx32 FX_Div(fx32 numer, fx32 denom);
extern fx32 FX_Sqrt(fx32 x);

/* sdk/balls.c */
extern int bw_ball_catch_base(int item);

#define ITEM_MASTER_BALL 1

int BTL_SERVER_FLOW_CalcCaptureShakes(BtlServerFlowHead *flow, struct BtlPokeParam *thrower, struct BtlPokeParam *target,
                                      u16 ballItemId, u8 *shakes, u8 *criticalCapture)
{
    u32 a, t;
    int rate, factor, v;
    u32 count, i;
    u16 species, form;
    *criticalCapture = 0;
    if (bw_ball_catch_base(ballItemId) == ITEM_MASTER_BALL) { *shakes = 3; return 1; }
    a = BTL_POKEPARAM_GetValue((void *)target, 14) * 3;
    t = a - BTL_POKEPARAM_GetValue((void *)target, 13) * 2;
    v = (int)(t != 0 ? 0.5f + (float)(t << 12) : (float)(t << 12) - 0.5f);
    if (BTL_MAIN_CheckSetupStatusFlag(flow->mainModule, 32)) {
        factor = BTL_SERVER_FLOW_GetCaptureDexFactor(flow);
        v = ((s64)v * factor + 0x800) >> 12;
    }
    species = BTL_POKEPARAM_GetSpecies((void *)target);
    form = BTL_POKEPARAM_GetValue((void *)target, 19);
    v *= (u16)POKE_TOOL_GetPersonalParam(species, form, 8);
    factor = BTL_SERVER_FLOW_GetBallCatchRate(flow, thrower, (void *)target, ballItemId);
    v = ((s64)v * factor + 0x800) >> 12;
    v = (u32)v / a;
    switch (BTL_POKEPARAM_GetMajorStatusIndex((void *)target)) {
    case 2: case 3: v = ((s64)v * 0x2800 + 0x800) >> 12; break;
    case 1: case 4: case 5: v = ((s64)v * 0x1800 + 0x800) >> 12; break;
    }
    rate = GPOWER_ApplyCaptureRate(v);
    *criticalCapture = BTL_SERVER_FLOW_RollCriticalCapture(flow, rate);
    if (rate >= 0xff000) { *shakes = *criticalCapture ? 1 : 3; return 1; }
    count = *criticalCapture ? 1 : 3;
    rate = FX_Div(0x10000000, FX_Sqrt(FX_Sqrt(FX_Div(0xff000, rate)))) >> 12;
    *shakes = 0;
    for (i = 0; i < count; i++) {
        if (BTL_CALC_Random(0x10000) < (u32)rate) (*shakes)++;
        else return 0;
    }
    return 1;
}
