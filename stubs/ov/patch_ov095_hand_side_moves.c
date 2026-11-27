/*
 * patches: src/game/ov095/hand_side.c HAND_SIDE_StealthRockHurtOnEnter
 *
 * Stealth Rock's damage on entry follows Rock's effectiveness against the
 * Pokemon's types, a third type a move gave it included (eidos's move
 * bases add_type, trick_or_treat, forests_curse; sdk/battle_moves.h): the
 * product of the three is taken as the later games take it, and kept to
 * the game's range (a quarter to four times, 1/32 to 1/2 of the HP).
 * With no third type the function is the game's own.
 *
 * symbols:
 *   BTL_MAIN_IsPokeIdInSecondPair        0x021B9A40  thumb  ov093
 *   BTL_EVENT_GetVar                     0x021D775C  thumb  ov093
 *   BTL_POKEPARAM_HasSick                0x021D62C4  thumb  ov093
 *   BTL_POKEPARAM_HasType                0x021D5814  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord       0x021C88A4  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord        0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg   0x021C8860  thumb  ov093
 *   BTL_SERVER_FLOW_CommitCmdRecord      0x021C88C8  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeParam         0x021C7CF4  thumb  ov093
 *   BTL_POKEPARAM_GetTypePair            0x021D57FC  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId              0x021D5440  thumb  ov093
 *   BTL_CALC_GetTypeAffinityVsPair       0x021D799C  thumb  ov093
 *   BTL_CALC_GetTypeAffinity             0x021D794C  thumb  ov093
 *   BTL_CALC_DivideMaxHpMin1             0x021D7B74  thumb  ov093
 *   gBtlCalcAffinityScales               0x021F0427  data   ov093  a1
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlEventFactor;
struct BtlServerFlow;
struct BtlPokeParam;

typedef struct {
    u16 strId;
    u16 typeAndCount;
    u32 args[8];
} BtlStrParam;

/* hand_common_types.h: HANDEX_KIND_DAMAGE */
typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 flags;
    u32 pad08;
    BtlStrParam message;
} HandExDamage;

#define HANDEX_KIND_DAMAGE          7
#define BTL_STRTYPE_SET             2
#define BTL_MSG_STEALTH_ROCK_HURT   0x356
#define BTL_EVAR_POKE_ID            0x02
#define BTL_TYPE_ROCK               5
#define BTL_SICK_ROOST              0x18
#define POKETYPE_FLYING             2
#define BTL_AFF_HALF                2
#define BTL_AFF_NORMAL              3
#define BTL_AFF_DOUBLE              4
#define BTL_AFF_QUADRUPLE           5

extern const u8 gBtlCalcAffinityScales[];
extern u8 BTL_MAIN_IsPokeIdInSecondPair(u32 pokeId);
extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern int BTL_CALC_GetTypeAffinityVsPair(int atkType, int defTypes);
extern int BTL_CALC_GetTypeAffinity(int atkType, int defType);
extern u16 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, u32 divisor);

/* sdk/battle_moves.c */
extern int bw_bm_third_type(int pokeId, const void *poke);

/* Rock against the pair, and against a third type the pair does not have:
 * the product of the scales (4 neutral), kept to the classes' range. */
static int rock_affinity(struct BtlPokeParam *bpp, u8 pokeId)
{
    int a = BTL_CALC_GetTypeAffinityVsPair(BTL_TYPE_ROCK, BTL_POKEPARAM_GetTypePair(bpp));
    int third = bw_bm_third_type(pokeId, bpp);
    u32 s;

    if (third < 0 || a == 0)
        return a;
    if (third == POKETYPE_FLYING && BTL_POKEPARAM_HasSick(bpp, BTL_SICK_ROOST))
        return a;
    s = (u32)gBtlCalcAffinityScales[a] * gBtlCalcAffinityScales[BTL_CALC_GetTypeAffinity(BTL_TYPE_ROCK, third)];
    if (s == 0)
        return 0;
    if (s >= 64)
        return BTL_AFF_QUADRUPLE;
    if (s <= 4)
        return 1;
    return s / 4 == 2 ? BTL_AFF_HALF : s / 4 == 8 ? BTL_AFF_DOUBLE : BTL_AFF_NORMAL;
}

void HAND_SIDE_StealthRockHurtOnEnter(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int side, int *work)
{
    int divisor;
    u8 pokeId;
    struct BtlPokeParam *bpp;
    HandExDamage *cmd;

    (void)factor;
    (void)work;
    divisor = 2;
    pokeId = (u8)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    if (side == BTL_MAIN_IsPokeIdInSecondPair(pokeId)) {
        bpp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        switch (rock_affinity(bpp, pokeId)) {
        default:
            divisor = 0x20;
            break;
        case BTL_AFF_HALF:
            divisor = 0x10;
            break;
        case BTL_AFF_NORMAL:
            divisor = 8;
            break;
        case BTL_AFF_DOUBLE:
            divisor = 4;
            break;
        case BTL_AFF_QUADRUPLE:
            break;
        }
        cmd = (HandExDamage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_DAMAGE, 0x1f);
        cmd->pokeId = pokeId;
        cmd->amount = BTL_CALC_DivideMaxHpMin1(bpp, (u32)divisor);
        BTL_SERVER_FLOW_InitMsgRecord(&cmd->message, BTL_STRTYPE_SET, BTL_MSG_STEALTH_ROCK_HURT);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&cmd->message, pokeId);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
    }
}
