/*
 * patches: src/game/ov093/hand_waza.c HAND_WAZA_AddMoveHandler HAND_WAZA_JudgmentOnWazaTypeCheck HAND_WAZA_TechnoBlastOnWazaTypeCheck
 *
 * Judgment and Techno Blast take the plates and drives of the types mods
 * add (types.txt `plate` and `drive`, sdk/type_items.c) after the game's
 * own; with none they are the game's.
 *
 * A mod's move that works like a built-in one (sdk/battle_handlers.h,
 * eidos/tables/move_handlers.txt): the registry is searched for the base
 * move's handlers, and the factor is added under the move's own id, so the
 * battle knows which move runs and the keys of that id apply.  A move with
 * no table line is its own base: the function then does what the game's
 * does.
 *
 * A base may also be one of eidos's own (`600 = misty_terrain`), for moves
 * Black and White have none for: their handlers are below, made of the
 * game's own event machinery as the game's handlers of the closest moves
 * are (Gravity, Safeguard, Quick Guard, Haze, Mean Look, Razor Wind).
 * Misty Terrain and Crafty Shield add a field and a side factor of their
 * own, with owners no game factor has, and end them at the turn check.
 *
 * symbols:
 *   gWazaHandlerRegistry                 0x021F3030  data
 *   BTL_POKEPARAM_GetPokeId              0x021D5440  thumb  ov093
 *   BTL_POKEPARAM_HasType                0x021D5814  thumb  ov093
 *   BTL_POKEPARAM_HasSick                0x021D62C4  thumb  ov093
 *   BTL_EVENT_AddFactor                  0x021D6F0C  thumb  ov093
 *   BTL_EVENT_RemoveFactor               0x021D7040  thumb  ov093
 *   BTL_EVENT_FindFactor                 0x021D732C  thumb  ov093
 *   BTL_EVENT_SetFactorWork              0x021D70DC  thumb  ov093
 *   BTL_EVENT_GetVar                     0x021D775C  thumb  ov093
 *   BTL_EVENT_OverwriteVar               0x021D76BC  thumb  ov093
 *   BTL_EVENT_MultiplyVar                0x021D7704  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord       0x021C88A4  thumb  ov093
 *   BTL_SERVER_FLOW_CommitCmdRecord      0x021C88C8  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord        0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg   0x021C8860  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeParam         0x021C7CF4  thumb  ov093
 *   BTL_SERVER_FLOW_GetFrontPosOfPoke    0x021C7D00  thumb  ov093
 *   BTL_SERVER_FLOW_CollectAlivePokeIds  0x021C7F68  thumb  ov093
 *   BTL_SERVER_FLOW_IsPokeFloatingById   0x021C8234  thumb  ov093
 *   BTL_CALC_HasDefaultSickCont          0x021D7D6C  thumb  ov093
 *   BTL_CALC_MakeSickContTurns           0x021D7D78  thumb  ov093
 *   BTL_MAIN_IsPokeIdInSecondPair        0x021B9A40  thumb  ov093
 *   WAZA_TOOL_GetParam                   0x0201BD28  thumb
 *   WAZA_TOOL_IsDamaging                 0x0201C314  thumb
 *   BTL_SERVER_FLOW_CanUseHeldItem       0x021C824C  thumb  ov093
 *   BTL_POKEPARAM_GetItem                0x021D5B40  thumb  ov093
 *   BTL_SERVER_FLOW_SetCurTurnStateFlag  0x021C83DC  thumb  ov093
 */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __INTPTR_TYPE__ sptr;   /* (types.h: an address-sized integer off the DS) */

struct BtlPokeParam;
struct BtlEventFactor;
struct BtlServerFlow;

typedef struct {
    int eventId;
    void (*handler)(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work);
} BtlEventHandler;

/* hand_common_types.h */
typedef struct {
    u16 moveId;
    BtlEventHandler *(*getHandlers)(int *handlerCount);
} WazaHandlerEntry;

#define WAZA_HANDLER_REGISTRY_COUNT 0x102
#define BTL_EVENT_TYPE_WAZA 0
#define BTL_EVENT_TYPE_SIDE 2
#define BTL_EVENT_TYPE_FIELD 3

/* hand_common_types.h: a message the flow shows */
typedef struct BtlStrParam {
    u16 strId;
    u16 typeAndCount;       /* strType, argCount, argFlag */
    u32 args[8];
} BtlStrParam;

/* HANDEX_KIND_MESSAGE */
typedef struct {
    u32 header;
    BtlStrParam message;
} HandExMessage;

/* HANDEX_KIND_ADD_SICK */
typedef struct {
    u32 header;
    u32 sickKind;
    u32 sickCont;           /* union SickCont, its whole word */
    u8 suppressMessage;
    u8 fromMove;
    u8 ignoreImmunity;
    u8 pokeId;
    u8 useOwnMessage;
    u8 pad11[3];
    BtlStrParam message;
} HandExAddSick;

/* HANDEX_KIND_RANK_EFFECT */
typedef struct {
    u32 header;
    u32 rankKind;
    u32 serialNumber;
    s8 volume;
    u8 fromMove;
    u8 showMessage;
    u8 targetCount;
    u8 targets[8];
    BtlStrParam message;
} HandExRankChange;

_Static_assert(sizeof(BtlStrParam) == 0x24, "BtlStrParam");
_Static_assert(__builtin_offsetof(HandExAddSick, pokeId) == 0x0F, "HandExAddSick.pokeId");
_Static_assert(__builtin_offsetof(HandExAddSick, message) == 0x14, "HandExAddSick.message");
_Static_assert(__builtin_offsetof(HandExRankChange, targets) == 0x10, "HandExRankChange.targets");

#define HANDEX_KIND_MESSAGE     4
#define HANDEX_KIND_ADD_SICK    12
#define HANDEX_KIND_RANK_EFFECT 14

#define BTL_STRTYPE_STD 1       /* one line of bank 15 */
#define BTL_STRTYPE_SET 2       /* a set of three in bank 14 */

/* hand_common_types.h: event variables */
#define BTL_EVAR_POKE_ID        0x02
#define BTL_EVAR_ATTACKER_ID    0x03
#define BTL_EVAR_DEFENDER_ID    0x04
#define BTL_EVAR_MOVE_ID        0x12
#define BTL_EVAR_MOVE_TYPE      0x16
#define BTL_EVAR_SICK_ID        0x1D
#define BTL_EVAR_RATIO          0x35
#define BTL_EVAR_MESSAGE        0x3F
#define BTL_EVAR_NO_EFFECT_FLAG 0x40
#define BTL_EVAR_PREVENT_FLAG   0x41

/* the turn check's first pass of a phase is for the field, not a Pokemon
 * (BTL_SERVER_FLOW_ProcTurnCheckPhase) */
#define BTL_TURN_CHECK_FIELD    0x1F

#define BTL_SICK_CONFUSION      6
#define BTL_SICK_YAWN           0x0E
#define BTL_SICK_NO_ESCAPE      0x16
#define BTL_RANK_DEFENSE        2
#define BTL_RATIO_HALF          0x800
#define POKETYPE_GRASS          11
#define POKETYPE_DRAGON         15
#define WAZA_PARAM_TARGET_RANGE 0x1B
#define WAZA_RANGE_ALL          8   /* every Pokemon (Perish Song) */

/* the code of every Pokemon on the field (BTL_SERVER_FLOW_CollectAlivePokeIds) */
#define BTL_COLLECT_ALL         0x800

extern const WazaHandlerEntry gWazaHandlerRegistry[WAZA_HANDLER_REGISTRY_COUNT];

extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sickIndex);
extern struct BtlEventFactor *BTL_EVENT_AddFactor(int factorType, u32 key, int priority, u32 subPriority, u8 ownerId,
                                                  const BtlEventHandler *handlers, u16 handlerCount);
extern void BTL_EVENT_RemoveFactor(struct BtlEventFactor *factor);
extern void BTL_EVENT_SetFactorWork(struct BtlEventFactor *factor, int index, u32 value);
extern struct BtlEventFactor *BTL_EVENT_FindFactor(int factorType, int ownerId);
extern sptr BTL_EVENT_GetVar(int key);
extern int BTL_EVENT_OverwriteVar(int key, __UINTPTR_TYPE__ value);
extern void BTL_EVENT_MultiplyVar(int key, int multiplier);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern int BTL_SERVER_FLOW_GetFrontPosOfPoke(struct BtlServerFlow *flow, int pokeId);
extern u8 BTL_SERVER_FLOW_CollectAlivePokeIds(struct BtlServerFlow *flow, int code, u8 *pokeIds);
extern int BTL_SERVER_FLOW_IsPokeFloatingById(struct BtlServerFlow *flow, int pokeId);
extern int BTL_CALC_HasDefaultSickCont(int kind);
extern u32 BTL_CALC_MakeSickContTurns(u32 turns);        /* union SickCont, one word */
extern u8 BTL_MAIN_IsPokeIdInSecondPair(u32 pokeId);
extern u32 WAZA_TOOL_GetParam(u16 move, int param);
extern u32 WAZA_TOOL_IsDamaging(u16 move);
int HAND_WAZA_CanAddMoveHandler(int ownerId, int moveId, u8 *alreadyAdded);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);
extern const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                              u32 *total);
extern int bw_bh_type(int type);

/* ------------------------------------------------------------------ */
/* eidos's own bases (sdk/battle_handlers.h: the same numbers)         */
/* ------------------------------------------------------------------ */

#define NAMED_FIRST          0x10000
#define NAMED_MISTY_TERRAIN  0x10000
#define NAMED_FLOWER_SHIELD  0x10001
#define NAMED_CRAFTY_SHIELD  0x10002
#define NAMED_FAIRY_LOCK     0x10003
#define NAMED_GEOMANCY       0x10004

/* their lines (sdk/battle_handlers.h BW_BH_LINE_*) */
#define LINE_MISTY_START     400
#define LINE_MISTY_END       401
#define LINE_FAIRY_LOCK      402
#define LINE_GEOMANCY        2000
#define LINE_MISTY_BLOCK     2003
#define LINE_CRAFTY_START    2006
#define LINE_CRAFTY_BLOCK    2009

/* The owners of the factors they add.  The game's field factors have
 * owner 0 and its side factors the side (0, 1); a move factor's owner is
 * a Pokemon (0..23).  No game factor has these. */
#define OWNER_MISTY_TERRAIN  0x40
#define OWNER_CRAFTY_SHIELD  0x41       /* + the side */

#define MISTY_TERRAIN_TURNS  5

/* the factor's work slots (BtlEventFactor.work) */
#define WORK_BLOCKED  0                 /* a status was just blocked: the notice is owed */
#define WORK_TURNS    1                 /* the turns left */

static void show_message(struct BtlServerFlow *flow, int pokeId, int kind, int line, int hasArg)
{
    HandExMessage *msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);

    if (msg != 0) {
        BTL_SERVER_FLOW_InitMsgRecord(&msg->message, kind, line);
        if (hasArg) {
            BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)pokeId);
        }
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
    }
}

static int is_grounded(struct BtlServerFlow *flow, int pokeId)
{
    return BTL_SERVER_FLOW_IsPokeFloatingById(flow, pokeId) == 0;
}

/* --- Misty Terrain ------------------------------------------------ */

/* 0x65: a grounded Pokemon gets no major status, no confusion and no
 * Yawn, from any source (its own Rest and rampage included). */
static void misty_block_sick(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    int sickId = (int)BTL_EVENT_GetVar(BTL_EVAR_SICK_ID);

    (void)factor;
    (void)ownerId;
    if (is_grounded((struct BtlServerFlow *)context, defenderId) &&
        (BTL_CALC_HasDefaultSickCont(sickId) != 0 || sickId == BTL_SICK_CONFUSION || sickId == BTL_SICK_YAWN)) {
        w[WORK_BLOCKED] = (u32)BTL_EVENT_OverwriteVar(BTL_EVAR_PREVENT_FLAG, 1);
    }
}

/* 0x67: the notice, once, after a status was blocked (Safeguard's). */
static void misty_notice(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    (void)ownerId;
    if (w[WORK_BLOCKED] != 0) {
        show_message((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID), BTL_STRTYPE_SET,
                     LINE_MISTY_BLOCK, 1);
        w[WORK_BLOCKED] = 0;
    }
}

/* 0x0E: Rest fails for a grounded Pokemon (Leaf Guard's). */
static void misty_block_rest(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)ownerId;
    (void)work;
    if (is_grounded((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID))) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_PREVENT_FLAG, 1);
    }
}

/* 0x47: a Dragon move does half damage to a grounded Pokemon (the
 * screens' step). */
static void misty_weaken_dragon(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)ownerId;
    (void)work;
    if (BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == POKETYPE_DRAGON &&
        is_grounded((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID))) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, BTL_RATIO_HALF);
    }
}

/* 0x77, the field's pass: one turn gone; after the last, the end line
 * (the game's field effects end in the turn check's step before). */
static void misty_turn_end(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)ownerId;
    if (BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) != BTL_TURN_CHECK_FIELD) {
        return;
    }
    if (w[WORK_TURNS] > 0) {
        w[WORK_TURNS]--;
    }
    if (w[WORK_TURNS] == 0) {
        show_message((struct BtlServerFlow *)context, 0, BTL_STRTYPE_STD, LINE_MISTY_END, 0);
        BTL_EVENT_RemoveFactor(factor);
    }
}

static const BtlEventHandler k_misty_field[] = {
    { 0x65, misty_block_sick },
    { 0x67, misty_notice },
    { 0x0E, misty_block_rest },
    { 0x47, misty_weaken_dragon },
    { 0x77, misty_turn_end },
};

/* 0x9E: the field effect for five turns, unless it is up (then nothing
 * is committed, and the game says the move failed). */
static void misty_start(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlEventFactor *field;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_MISTY_TERRAIN) != 0) {
        return;
    }
    field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0, OWNER_MISTY_TERRAIN,
                                k_misty_field, (u16)(sizeof k_misty_field / sizeof k_misty_field[0]));
    if (field != 0) {
        BTL_EVENT_SetFactorWork(field, WORK_TURNS, MISTY_TERRAIN_TURNS);
        show_message(flow, ownerId, BTL_STRTYPE_STD, LINE_MISTY_START, 0);
    }
}

static const BtlEventHandler k_misty_terrain[] = {
    { 0x9E, misty_start },
};

/* --- Flower Shield ------------------------------------------------ */

/* 0x9E: +1 Defense for every Grass-type Pokemon on the field, in one
 * stat change (Intimidate's); with none, the move fails. */
static void flower_shield_raise(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    HandExRankChange *cmd;
    u8 all[8], kept[8];
    int n, i, k = 0, type;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    type = bw_bh_type(POKETYPE_GRASS);
    n = BTL_SERVER_FLOW_CollectAlivePokeIds(flow, BTL_COLLECT_ALL | BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, ownerId), all);
    for (i = 0; i < n && i < 8; i++) {
        if (BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam(flow, all[i]), type)) {
            kept[k++] = all[i];
        }
    }
    if (k == 0 || (cmd = (HandExRankChange *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RANK_EFFECT, ownerId)) == 0) {
        return;
    }
    cmd->rankKind = BTL_RANK_DEFENSE;
    cmd->volume = 1;
    cmd->showMessage = 1;
    cmd->targetCount = (u8)k;
    for (i = 0; i < k; i++) {
        cmd->targets[i] = kept[i];
    }
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

static const BtlEventHandler k_flower_shield[] = {
    { 0x9E, flower_shield_raise },
};

/* --- Crafty Shield ------------------------------------------------ */

/* 0x2C: a status move another Pokemon aims at the side does nothing
 * (Quick Guard's test, on the move's category).  A move aimed at every
 * Pokemon (Perish Song) is not held off. */
static void crafty_block(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    u16 move = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    BtlStrParam *message;

    (void)factor;
    (void)context;
    (void)work;
    if (ownerId - OWNER_CRAFTY_SHIELD == BTL_MAIN_IsPokeIdInSecondPair((u32)defenderId) &&
        defenderId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && WAZA_TOOL_IsDamaging(move) == 0 &&
        WAZA_TOOL_GetParam(move, WAZA_PARAM_TARGET_RANGE) != WAZA_RANGE_ALL &&
        BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1) != 0) {
        message = (BtlStrParam *)BTL_EVENT_GetVar(BTL_EVAR_MESSAGE);
        BTL_SERVER_FLOW_InitMsgRecord(message, BTL_STRTYPE_SET, LINE_CRAFTY_BLOCK);
        BTL_SERVER_FLOW_AppendMsgRecordArg(message, (u32)defenderId);
    }
}

/* 0x76, the field's pass: the guard ends with the turn. */
static void crafty_turn_end(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)context;
    (void)ownerId;
    (void)work;
    if (BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) == BTL_TURN_CHECK_FIELD) {
        BTL_EVENT_RemoveFactor(factor);
    }
}

static const BtlEventHandler k_crafty_side[] = {
    { 0x2C, crafty_block },
    { 0x76, crafty_turn_end },
};

/* 0xA1: the guard on the user's side for this turn, unless it is up. */
static void crafty_start(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int owner = OWNER_CRAFTY_SHIELD + BTL_MAIN_IsPokeIdInSecondPair(ownerId);

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || BTL_EVENT_FindFactor(BTL_EVENT_TYPE_SIDE, owner) != 0) {
        return;
    }
    if (BTL_EVENT_AddFactor(BTL_EVENT_TYPE_SIDE, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 2, 0, (u8)owner,
                            k_crafty_side, (u16)(sizeof k_crafty_side / sizeof k_crafty_side[0])) != 0) {
        show_message((struct BtlServerFlow *)context, ownerId, BTL_STRTYPE_SET, LINE_CRAFTY_START, 1);
    }
}

static const BtlEventHandler k_crafty_shield[] = {
    { 0xA1, crafty_start },
};

/* --- Fairy Lock --------------------------------------------------- */

/* 0x9E: every Pokemon on the field can neither flee nor switch until the
 * second turn check from now (Mean Look's status, for two turns, put in
 * with no line of its own); one line for all.  With every Pokemon
 * already held, the move fails. */
static void fairy_lock_all(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    HandExAddSick *cmd;
    u8 all[8];
    int n, i, added = 0;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = BTL_SERVER_FLOW_CollectAlivePokeIds(flow, BTL_COLLECT_ALL | BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, ownerId), all);
    for (i = 0; i < n && i < 8; i++) {
        if (BTL_POKEPARAM_HasSick(BTL_SERVER_FLOW_GetPokeParam(flow, all[i]), BTL_SICK_NO_ESCAPE) != 0) {
            continue;
        }
        if ((cmd = (HandExAddSick *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_ADD_SICK, ownerId)) == 0) {
            break;
        }
        cmd->sickKind = BTL_SICK_NO_ESCAPE;
        cmd->sickCont = BTL_CALC_MakeSickContTurns(2);
        cmd->pokeId = all[i];
        BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
        added++;
    }
    if (added > 0) {
        show_message(flow, ownerId, BTL_STRTYPE_STD, LINE_FAIRY_LOCK, 0);
    }
}

static const BtlEventHandler k_fairy_lock[] = {
    { 0x9E, fairy_lock_all },
};

/* --- Geomancy ----------------------------------------------------- */

/* 0x95: the charge turn's line (Razor Wind's handler with its own line).
 * The stat rises on the second turn come from the move's record. */
static void geomancy_charge(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        show_message((struct BtlServerFlow *)context, ownerId, BTL_STRTYPE_SET, LINE_GEOMANCY, 1);
    }
}

static const BtlEventHandler k_geomancy[] = {
    { 0x95, geomancy_charge },
};

/* ------------------------------------------------------------------ */
/* Judgment and Techno Blast                                           */
/* ------------------------------------------------------------------ */

extern int BTL_SERVER_FLOW_CanUseHeldItem(struct BtlServerFlow *flow, int pokeId);
extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern void BTL_SERVER_FLOW_SetCurTurnStateFlag(struct BtlServerFlow *flow, int flag);

/* sdk/type_icons.c: the added types' plates and drives */
extern u32 bw_type_of_plate(u32 item);
extern int bw_type_of_drive(u32 item, u32 *type, u32 *form);

/* Judgment: the type follows the plate that the user holds. */
void HAND_WAZA_JudgmentOnWazaTypeCheck(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        if (BTL_SERVER_FLOW_CanUseHeldItem(flow, pokeId) != 0) {
            u16 itemId = (u16)(int)BTL_POKEPARAM_GetItem(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId));
            int type = 0;               /* (Normal) */

            switch (itemId) {
            case 0x12A: type = 9; break;    /* Flame Plate: Fire   */
            case 0x12B: type = 10; break;   /* Splash: Water       */
            case 0x12C: type = 12; break;   /* Zap: Electric       */
            case 0x12D: type = 11; break;   /* Meadow: Grass       */
            case 0x12E: type = 14; break;   /* Icicle: Ice         */
            case 0x12F: type = 1; break;    /* Fist: Fighting      */
            case 0x130: type = 3; break;    /* Toxic: Poison       */
            case 0x131: type = 4; break;    /* Earth: Ground       */
            case 0x132: type = 2; break;    /* Sky: Flying         */
            case 0x133: type = 13; break;   /* Mind: Psychic       */
            case 0x134: type = 6; break;    /* Insect: Bug         */
            case 0x135: type = 5; break;    /* Stone: Rock         */
            case 0x136: type = 7; break;    /* Spooky: Ghost       */
            case 0x137: type = 15; break;   /* Draco: Dragon       */
            case 0x138: type = 16; break;   /* Dread: Dark         */
            case 0x139: type = 8; break;    /* Iron: Steel         */
            default: type = (int)bw_type_of_plate(itemId); break;   /* an added type's plate */
            }
            BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, (__UINTPTR_TYPE__)type);
        }
    }
}

/* Techno Blast: the type and the animation variant follow the drive that
 * the user holds (an added type's drive: the plain animation). */
void HAND_WAZA_TechnoBlastOnWazaTypeCheck(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        if (BTL_SERVER_FLOW_CanUseHeldItem(flow, pokeId) != 0) {
            u16 itemId = (u16)(int)BTL_POKEPARAM_GetItem(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId));
            int type = 0;               /* (Normal) */
            int variant = 0;
            u32 added, form;

            switch (itemId) {
            case 0x74: type = 10; variant = 1; break;   /* Douse Drive: Water   */
            case 0x75: type = 12; variant = 2; break;   /* Shock: Electric      */
            case 0x76: type = 9; variant = 3; break;    /* Burn: Fire           */
            case 0x77: type = 14; variant = 4; break;   /* Chill: Ice           */
            default:
                if (bw_type_of_drive(itemId, &added, &form)) {
                    type = (int)added;
                }
                break;
            }
            BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, (__UINTPTR_TYPE__)type);
            BTL_SERVER_FLOW_SetCurTurnStateFlag(flow, variant);
        }
    }
}

#define ROWS(t) ((int)(sizeof(t) / sizeof((t)[0])))

/* The handler table of a base: the game's registry, or eidos's own.
 * Returns 0 for a base with none (*found 0 for a base nothing knows). */
static const BtlEventHandler *base_handlers(int base, int *count, int *found)
{
    u32 index;

    *count = 0;
    *found = 1;
    switch (base) {
    case NAMED_MISTY_TERRAIN: *count = ROWS(k_misty_terrain); return k_misty_terrain;
    case NAMED_FLOWER_SHIELD: *count = ROWS(k_flower_shield); return k_flower_shield;
    case NAMED_CRAFTY_SHIELD: *count = ROWS(k_crafty_shield); return k_crafty_shield;
    case NAMED_FAIRY_LOCK:    *count = ROWS(k_fairy_lock);    return k_fairy_lock;
    case NAMED_GEOMANCY:      *count = ROWS(k_geomancy);      return k_geomancy;
    }
    for (index = 0; base > 0 && base < NAMED_FIRST && index < WAZA_HANDLER_REGISTRY_COUNT; index++) {
        if (base == gWazaHandlerRegistry[index].moveId) {
            return gWazaHandlerRegistry[index].getHandlers(count);
        }
    }
    *found = 0;
    return 0;
}

/*
 * Add the handler of a move to the Pokemon when the move is chosen. Looks the
 * move up in the registry; returns 1 when a handler was added.
 */
/* A move that works like several (`600 = 85 + 26`): one factor whose
 * table is the bases' tables joined in line order. */
static int add_joined_moves(struct BtlPokeParam *bpp, u16 moveId, u32 subPriority, int parts)
{
    const void *tables[4];
    u32 counts[4], total = 0;
    int k, found = 0;
    u8 ownerId, alreadyAdded = 0;
    const void *joined;

    ownerId = BTL_POKEPARAM_GetPokeId(bpp);
    if (HAND_WAZA_CanAddMoveHandler(ownerId, moveId, &alreadyAdded) == 0) {
        return 0;
    }
    for (k = 0; k < parts && k < 4; k++) {
        int base = bw_bh_part_base(BTL_EVENT_TYPE_WAZA, moveId, k);
        int n = 0, known;

        tables[k] = base != 0 ? base_handlers(base, &n, &known) : 0;
        counts[k] = tables[k] != 0 && n > 0 ? (u32)n : 0;
        found |= counts[k] != 0;
    }
    if (!found) {
        return 0;
    }
    joined = bw_bh_join(BTL_EVENT_TYPE_WAZA, moveId, k, tables, counts, sizeof(BtlEventHandler), &total);
    if (joined == 0 || total == 0) {
        return 0;
    }
    BTL_EVENT_AddFactor(BTL_EVENT_TYPE_WAZA, moveId, 0, subPriority, ownerId, (const BtlEventHandler *)joined,
                        (u16)total);
    return 1;
}

int HAND_WAZA_AddMoveHandler(struct BtlPokeParam *bpp, u16 moveId, u32 subPriority)
{
    int parts = bw_bh_parts(BTL_EVENT_TYPE_WAZA, moveId);
    int base = bw_bh_base(BTL_EVENT_TYPE_WAZA, moveId);
    u8 ownerId;
    u8 alreadyAdded;
    int handlerCount = 0, found;
    const BtlEventHandler *handlers;

    if (parts > 1) {
        return add_joined_moves(bpp, moveId, subPriority, parts);
    }
    if (base >= NAMED_FIRST) {
        /* (eidos's own: no getter to call before the test) */
        handlers = base_handlers(base, &handlerCount, &found);
        ownerId = BTL_POKEPARAM_GetPokeId(bpp);
        alreadyAdded = 0;
        if (HAND_WAZA_CanAddMoveHandler(ownerId, moveId, &alreadyAdded) != 0 && handlers != 0) {
            BTL_EVENT_AddFactor(BTL_EVENT_TYPE_WAZA, moveId, 0, subPriority, ownerId, handlers, (u16)handlerCount);
            return 1;
        }
        return 0;
    }
    {
        u32 index;

        for (index = 0; index < WAZA_HANDLER_REGISTRY_COUNT; index++) {
            if ((u16)base == gWazaHandlerRegistry[index].moveId) {
                ownerId = BTL_POKEPARAM_GetPokeId(bpp);
                alreadyAdded = 0;
                if (HAND_WAZA_CanAddMoveHandler(ownerId, moveId, &alreadyAdded) != 0) {
                    handlers = gWazaHandlerRegistry[index].getHandlers(&handlerCount);
                    if (handlers != 0) {
                        BTL_EVENT_AddFactor(BTL_EVENT_TYPE_WAZA, moveId, 0, subPriority, ownerId, handlers,
                                            (u16)handlerCount);
                        return 1;
                    }
                }
                return 0;
            }
        }
    }
    return 0;
}
