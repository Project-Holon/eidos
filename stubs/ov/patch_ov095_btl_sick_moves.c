/*
 * patches: src/game/ov095/btl_sick.c BTL_SICK_ApplyBindDamage
 *
 * The end-of-turn hurt of a bound Pokemon plays the animation of the move
 * that bound it (Wrap, Bind, Fire Spin, Magma Storm, Clamp, Whirlpool, Sand
 * Tomb), picked by the move's id: a mod's move that works like one of them
 * (sdk/battle_handlers.h bw_bh_move_is, eidos/tables/move_handlers.txt)
 * plays that move's.  With no table lines it is the game's own function.
 *
 * symbols:
 *   BTL_POKEPARAM_IsFainted                0x021D5B68  thumb  ov093
 *   BTL_POKEPARAM_GetSickContRaw           0x021D6314  thumb  ov093
 *   BTL_SICKCONT_GetValue                  0x021E8B80  thumb  ov093
 *   BTL_SICKCONT_GetFlag                   0x021E8C24  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord         0x021C88A4  thumb  ov093
 *   BTL_CALC_DivideMaxHpMin1               0x021D7B74  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeViewPos         0x021C8010  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord          0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg     0x021C8860  thumb  ov093
 *   BTL_SERVER_FLOW_CommitCmdRecord        0x021C88C8  thumb  ov093
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlServerFlow;
struct BtlPokeParam;

/* btl_pokeparam_types.h: a status's duration word */
union SickCont {
    u32 raw;
};

/* hand_common_types.h: a message the flow shows */
typedef struct {
    u16 strId;
    u16 typeAndCount;
    u32 args[8];
} BtlStrParam;

/* btl_sick_types.h: command record kind 7, damage one Pokemon */
typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 flags;               /* bit 1: the effect fields are valid */
    u16 effectStringId;
    u8 effectArgA;
    u8 effectArgB;
    BtlStrParam message;
} BtlCmdDamage;

_Static_assert(sizeof(BtlStrParam) == 0x24, "BtlStrParam");
_Static_assert(__builtin_offsetof(BtlCmdDamage, message) == 0x0C, "BtlCmdDamage.message");

#define BTL_SICK_BIND              8
#define BTL_CMD_DAMAGE             7
#define BTL_CMD_DAMAGE_HAS_EFFECT  2
#define BTL_STRTYPE_SET            2

extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern union SickCont BTL_POKEPARAM_GetSickContRaw(struct BtlPokeParam *pp, int sick);
extern u32 BTL_SICKCONT_GetValue(union SickCont cont);
extern u32 BTL_SICKCONT_GetFlag(union SickCont cont);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern u32 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, u32 divisor);
extern u8 BTL_SERVER_FLOW_GetPokeViewPos(struct BtlServerFlow *flow, int pokeId);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);

/* sdk/battle_handlers.c */
extern int bw_bh_move_is(int move, int builtin);

/* the binding moves and the animation of each */
static const struct {
    u16 move;
    u16 effect;
} k_bind_effects[] = {
    { 0x23, 0x277 },        /* Wrap        */
    { 0x14, 0x276 },        /* Bind        */
    { 0x53, 0x278 },        /* Fire Spin   */
    { 0x1cf, 0x279 },       /* Magma Storm */
    { 0x80, 0x27a },        /* Clamp       */
    { 0xfa, 0x27b },        /* Whirlpool   */
    { 0x148, 0x27c },       /* Sand Tomb   */
};

void BTL_SICK_ApplyBindDamage(struct BtlServerFlow *flow, struct BtlPokeParam *bpp, int pokeId)
{
    union SickCont cont;
    BtlCmdDamage *damage;
    int moveId;
    int effectStringId;
    unsigned i;

    if (BTL_POKEPARAM_IsFainted(bpp) != 0)
        return;
    cont = BTL_POKEPARAM_GetSickContRaw(bpp, BTL_SICK_BIND);
    moveId = (int)BTL_SICKCONT_GetValue(cont);
    effectStringId = -1;
    for (i = 0; i < sizeof k_bind_effects / sizeof k_bind_effects[0]; i++) {
        if (bw_bh_move_is(moveId, k_bind_effects[i].move)) {
            effectStringId = k_bind_effects[i].effect;
            break;
        }
    }
    damage = (BtlCmdDamage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, BTL_CMD_DAMAGE, pokeId);
    damage->pokeId = (u8)pokeId;
    if (BTL_SICKCONT_GetFlag(cont) != 0)
        damage->amount = (u16)BTL_CALC_DivideMaxHpMin1(bpp, 8);
    else
        damage->amount = (u16)BTL_CALC_DivideMaxHpMin1(bpp, 16);
    if (effectStringId != -1) {
        damage->flags = (u8)(BTL_CMD_DAMAGE_HAS_EFFECT | damage->flags);
        damage->effectStringId = (u16)effectStringId;
        damage->effectArgA = BTL_SERVER_FLOW_GetPokeViewPos(flow, pokeId);
        damage->effectArgB = 6;
    }
    /* (bank 14 line 0x174: "<Pokemon> is hurt by <move>!") */
    BTL_SERVER_FLOW_InitMsgRecord(&damage->message, BTL_STRTYPE_SET, 0x174);
    BTL_SERVER_FLOW_AppendMsgRecordArg(&damage->message, (u32)pokeId);
    BTL_SERVER_FLOW_AppendMsgRecordArg(&damage->message, (u32)moveId);
    BTL_SERVER_FLOW_CommitCmdRecord(flow, damage);
}
