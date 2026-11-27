/*
 * patches: src/game/ov093/hand_waza.c HAND_WAZA_AddMoveHandler HAND_WAZA_JudgmentOnWazaTypeCheck HAND_WAZA_TechnoBlastOnWazaTypeCheck HAND_WAZA_ProtectResetStreakAfterOtherMove HAND_WAZA_TrapOnAddSickMessage HAND_WAZA_UTurnSwitchOut HAND_WAZA_CircleThrowForceSwitch HAND_WAZA_FreezeShockIceBurnShowChargeMessage HAND_WAZA_FusionDoublePower HAND_WAZA_FindPledgeCombo HAND_WAZA_LastResortOnMoveFailCheck HAND_WAZA_FollowMeRedirectTarget HAND_WAZA_SketchLearnTargetMove HAND_WAZA_SynchronoiseFailWithoutSharedType HAND_WAZA_ReflectTypeCopyTargetType HAND_WAZA_CamouflageChangeType
 *
 * The game's handlers that test a move by its id (Protect's streak, the
 * bind messages, U-turn's and Circle Throw's lines, Freeze Shock's charge
 * line, the Fusion and Pledge partners, Last Resort's own slot, Follow Me
 * and Sky Drop, Sketch's refusals) take a mod's move that works like the
 * move (sdk/battle_handlers.h bw_bh_move_is) as that move.  With no table
 * lines each is the game's own handler.
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
 * The terrains (Misty, Grassy, Electric, Psychic) share one field factor
 * of their own and Crafty Shield adds a side factor, with owners no game
 * factor has, and end them at the turn check.  The guards (Spiky Shield,
 * King's Shield, Baneful Bunker, contact_guard) run Protect's rows and note
 * their contact effect for the flow (sdk/battle_moves.h); add_type gives a
 * third type the same way.  The later moves of one handler each (Topsy-
 * Turvy, Strength Sap, Shore Up, Belch, Burn Up, Rototiller, Magnetic
 * Flux, Parting Shot, set_type) follow, then Electrify and Ion Deluge,
 * which add a field factor of their own for the turn, and Sticky Web, a
 * side factor of its own that Defog and Rapid Spin take away.  Powder and
 * Throat Chop add a field factor for their target; Laser Focus and
 * Stomping Tantrum keep their state in sdk/battle_moves.c; Spectral Thief
 * and Pollen Puff are one move factor each.
 *
 * Reflect Type copies the target's third type with its two; Camouflage
 * changes a Pokemon that has a third type even when its two are the
 * type's.
 *
 * A line of eidos's that the game's language has none of (sdk/
 * battle_handlers.h bw_bh_line_ok) is not shown: the game's own lines
 * (the stat change, the HP) still are.
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
 *   gProtectMoveIds                      0x021F284C  data   a2
 *   gProtectHandlers                     0x021F2D58  data   ov093  a4
 *   gPledgeComboTable                    0x021F2CF0  data   a4
 *   BTL_POKEPARAM_GetLastUsedMoveId      0x021D67A8  thumb  ov093
 *   BTL_POKEPARAM_GetLastSelectedMoveId  0x021D67B8  thumb  ov093
 *   BTL_SERVER_FLOW_CheckClientHasBackupPoke 0x021C7F24  thumb  ov093
 *   BTL_SERVER_FLOW_TrySetOnceFlag       0x021C7F4C  thumb  ov093
 *   BTL_MAIN_PokeIdToClientIdThunk       0x021B8DDC  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeViewPos       0x021C8010  thumb  ov093
 *   BTL_EVENT_GetFactorKey               0x021D70A8  thumb  ov093
 *   BTL_SERVER_FLOW_GetTurnRecord        0x021C812C  thumb  ov093
 *   BTL_WAZAREC_GetEffectiveWaza         0x0689D0C8  thumb  ov095
 *   BTL_SERVER_FLOW_GetTurnCount         0x021C8138  thumb  ov093
 *   BTL_POKEPARAM_GetMoveCount           0x021D5448  thumb  ov093
 *   BTL_POKEPARAM_GetMoveId              0x021D54C0  thumb  ov093
 *   BTL_POKEPARAM_GetMoveUsedFlag        0x021D54E0  thumb  ov093
 *   BTL_SERVER_FLOW_GetRule              0x021C815C  thumb  ov093
 *   BTL_MAIN_IsPokeSamePair              0x021B9A24  thumb  ov093
 *   BTL_SERVER_FLOW_TestStateFlagBit0    0x021C830C  thumb  ov093
 *   BTL_SERVER_FLOW_CheckAllyMoveAutoHitById 0x021C8290  thumb  ov093
 *   BTL_POKEPARAM_HasMove                0x021D5750  thumb  ov093
 *   BTL_POKEPARAM_IsTransformed          0x021D6D54  thumb  ov093
 *   BTL_POKEPARAM_FindMoveSlot           0x021D5778  thumb  ov093
 *   BTL_POKEPARAM_GetTypePair            0x021D57FC  thumb  ov093
 *   BTL_TYPEPAIR_GetHigh                 0x021E8CFC  thumb  ov093
 *   BTL_TYPEPAIR_GetLow                  0x021E8D04  thumb  ov093
 *   ITEM_IsBerry                         0x020213C0  thumb
 *   WAZA_TOOL_GetFlag                    0x0201BEB8  thumb
 *   WAZA_TOOL_GetType                    0x0201C1C8  thumb
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

#define BTL_SICK_SLEEP          2
#define BTL_SICK_CONFUSION      6
#define BTL_SICK_YAWN           0x0E
#define BTL_SICK_NO_ESCAPE      0x16
#define BTL_RANK_DEFENSE        2
#define BTL_RATIO_HALF          0x800
#define POKETYPE_GRASS          11
#define POKETYPE_DRAGON         15
#define POKETYPE_ELECTRIC       12
#define POKETYPE_PSYCHIC        13
#define WAZA_PARAM_PRIORITY     6
#define BTL_EVAR_POWER_RATIO    0x31
#define BTL_MOVE_EARTHQUAKE     89
#define BTL_MOVE_MAGNITUDE      222
#define BTL_MOVE_BULLDOZE       523
#define HANDEX_KIND_RECOVER_HP  5
#define BTL_STR_HP_RESTORED     387     /* bank 14: "<Pokemon>'s HP was restored." */

/* HANDEX_KIND_RECOVER_HP */
typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 pad07;
    BtlStrParam message;
} HandExRecoverHp;

extern u32 BTL_EVENT_GetFactorWork(struct BtlEventFactor *factor, int index);
extern int BTL_POKEPARAM_IsHpFull(struct BtlPokeParam *pp);
extern u16 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, u32 divisor);
extern int bw_bh_move_in(int move, const u16 *list, int count);
extern int bw_bh_running_base(int type, int id);
extern int bw_bh_arg(const char *key, int index, int fallback);
extern u16 BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern int BTL_MAIN_IsPokeSamePair(int pokeA, int pokeB);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
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
/* a status's duration word, returned as the union the game declares (the
 * wasm32 build passes a union by its own rule) */
union SickCont {
    u32 raw;
};
extern union SickCont BTL_CALC_MakeSickContTurns(u32 turns);
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
#define NAMED_FELL_STINGER   0x10005
#define NAMED_NEEDS_STATUS   0x10006
#define NAMED_SUPER_EFFECTIVE 0x1000B
#define NAMED_FREEZE_DRY     0x1000C
#define NAMED_SECOND_TYPE    0x1000D
#define NAMED_FLYING_PRESS   0x1000E
#define NAMED_HITS_FLOATING  0x10015

/* their lines (sdk/battle_handlers.h BW_BH_LINE_*) */
#define LINE_MISTY_START     400
#define LINE_MISTY_END       401
#define LINE_FAIRY_LOCK      402
#define LINE_GEOMANCY        2000
#define LINE_MISTY_BLOCK     2003
#define LINE_CRAFTY_START    2006
#define LINE_CRAFTY_BLOCK    2009
#define LINE_GRASSY_START    403
#define LINE_GRASSY_END      404
#define LINE_ELECTRIC_START  405
#define LINE_ELECTRIC_END    406
#define LINE_PSYCHIC_START   407
#define LINE_PSYCHIC_END     408
#define LINE_ELECTRIC_BLOCK  2012
#define LINE_PSYCHIC_BLOCK   2015

/* The owners of the factors they add.  The game's field factors have
 * owner 0 and its side factors the side (0, 1); a move factor's owner is
 * a Pokemon (0..23).  No game factor has these. */
#define OWNER_TERRAIN        0x40       /* the one terrain up (Misty, Grassy, Electric, Psychic) */
#define OWNER_CRAFTY_SHIELD  0x41       /* + the side */

#define NAMED_GRASSY_TERRAIN   0x10012
#define NAMED_ELECTRIC_TERRAIN 0x10013
#define NAMED_PSYCHIC_TERRAIN  0x10014

/* the terrain's kind (the field factor's WORK_KIND) */
#define TERRAIN_MISTY        0
#define TERRAIN_GRASSY       1
#define TERRAIN_ELECTRIC     2
#define TERRAIN_PSYCHIC      3

#define TERRAIN_TURNS        5
#define TERRAIN_POWER_RATIO  0x1800     /* 1.5 for the terrain's type (Generation VII; 1.3 for VIII) */
#define GRASSY_HEAL_HP       16         /* 1/16 of the HP at each turn end */

/* the factor's work slots (BtlEventFactor.work) */
#define WORK_BLOCKED  0                 /* a status or move was just blocked: the notice is owed */
#define WORK_TURNS    1                 /* the turns left */
#define WORK_KIND     2                 /* TERRAIN_* */
#define WORK_RATIO    3                 /* Misty: Dragon damage; the others: their type's power, in 4096ths */
#define WORK_HP       4                 /* Grassy: the heal's divisor */

/* each kind's lines: start and end (bank 15), notice (bank 14, set of three; 0 none) */
static const u16 k_terrain_lines[4][3] = {
    { LINE_MISTY_START, LINE_MISTY_END, LINE_MISTY_BLOCK },
    { LINE_GRASSY_START, LINE_GRASSY_END, 0 },
    { LINE_ELECTRIC_START, LINE_ELECTRIC_END, LINE_ELECTRIC_BLOCK },
    { LINE_PSYCHIC_START, LINE_PSYCHIC_END, LINE_PSYCHIC_BLOCK },
};

/* the type each kind strengthens, and Misty's that it weakens */
static const u8 k_terrain_type[4] = { POKETYPE_DRAGON, POKETYPE_GRASS, POKETYPE_ELECTRIC, POKETYPE_PSYCHIC };

/* sdk/battle_handlers.c: whether the game's language has eidos's line */
extern int bw_bh_line_ok(u32 bank, u32 line);

#define LINE_BANK_SET   14
#define LINE_BANK_STD   15

static int line_ok(int kind, int line)
{
    return bw_bh_line_ok(kind == BTL_STRTYPE_STD ? LINE_BANK_STD : LINE_BANK_SET, (u32)line);
}

/* One of eidos's lines (kind: bank 15 or a set of bank 14), with the
 * Pokemon as its argument.  In a language that has no such line the
 * record goes in empty (the allocator zeroes it, and an empty message
 * shows nothing): the move still did something, so the flow does not say
 * "But it failed!" (BTL_SERVER_FLOW_DispatchUncategorizedMoveEvent counts
 * the records). */
static void show_message(struct BtlServerFlow *flow, int pokeId, int kind, int line, int hasArg)
{
    HandExMessage *msg;

    msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);
    if (msg != 0 && !line_ok(kind, line)) {
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
        return;
    }
    if (msg != 0) {
        BTL_SERVER_FLOW_InitMsgRecord(&msg->message, kind, line);
        if (hasArg) {
            BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)pokeId);
        }
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
    }
}

/* The same with the move's name as argument 1 (the `$` of the line: the
 * move's own name from bank 203, so a mod's name for its move shows).
 * Argument 0 is the Pokemon, or 0 in a bank 15 line that names none. */
static void show_move_message(struct BtlServerFlow *flow, int pokeId, int kind, int line, u32 move)
{
    HandExMessage *msg;

    msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);
    if (msg != 0 && !line_ok(kind, line)) {
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);      /* (empty: see show_message) */
        return;
    }
    if (msg != 0) {
        BTL_SERVER_FLOW_InitMsgRecord(&msg->message, kind, line);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, kind == BTL_STRTYPE_STD ? 0u : (u32)pokeId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, move);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
    }
}

static int is_grounded(struct BtlServerFlow *flow, int pokeId)
{
    return BTL_SERVER_FLOW_IsPokeFloatingById(flow, pokeId) == 0;
}

/* --- The terrains: Misty, Grassy, Electric, Psychic ---------------- */

/* 0x65: Misty: a grounded Pokemon gets no major status, no confusion and
 * no Yawn, from any source (its own Rest and rampage included).
 * Electric: a grounded Pokemon cannot fall asleep or get drowsy. */
static void terrain_block_sick(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    int sickId = (int)BTL_EVENT_GetVar(BTL_EVAR_SICK_ID);
    int blocked;

    (void)factor;
    (void)ownerId;
    switch (w[WORK_KIND]) {
    case TERRAIN_MISTY:
        blocked = BTL_CALC_HasDefaultSickCont(sickId) != 0 || sickId == BTL_SICK_CONFUSION || sickId == BTL_SICK_YAWN;
        break;
    case TERRAIN_ELECTRIC:
        blocked = sickId == BTL_SICK_SLEEP || sickId == BTL_SICK_YAWN;
        break;
    default:
        return;
    }
    if (blocked && is_grounded((struct BtlServerFlow *)context, defenderId)) {
        w[WORK_BLOCKED] = (u32)BTL_EVENT_OverwriteVar(BTL_EVAR_PREVENT_FLAG, 1);
    }
}

/* 0x67: the notice, once, after a status was blocked (Safeguard's). */
static void terrain_notice(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)ownerId;
    if (w[WORK_BLOCKED] != 0 && w[WORK_KIND] < 4 && k_terrain_lines[w[WORK_KIND]][2] != 0) {
        show_move_message((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID),
                          BTL_STRTYPE_SET, k_terrain_lines[w[WORK_KIND]][2], BTL_EVENT_GetFactorKey(factor));
    }
    w[WORK_BLOCKED] = 0;
}

/* 0x0E: Rest fails for a grounded Pokemon under Misty and Electric
 * Terrain (Leaf Guard's). */
static void terrain_block_rest(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    (void)ownerId;
    if ((w[WORK_KIND] == TERRAIN_MISTY || w[WORK_KIND] == TERRAIN_ELECTRIC) &&
        is_grounded((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID))) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_PREVENT_FLAG, 1);
    }
}

/* the moves Grassy Terrain weakens against a grounded target */
static const u16 k_grassy_weakened[] = { BTL_MOVE_EARTHQUAKE, BTL_MOVE_MAGNITUDE, BTL_MOVE_BULLDOZE };

/* 0x47 (the screens' step): Misty: a Dragon move does half damage to a
 * grounded Pokemon; Grassy: Earthquake, Magnitude and Bulldoze (and the
 * moves that work like them) do half damage to one. */
static void terrain_damage_ratio(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;
    int hit;

    (void)factor;
    (void)ownerId;
    switch (w[WORK_KIND]) {
    case TERRAIN_MISTY:
        hit = BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == k_terrain_type[TERRAIN_MISTY];
        break;
    case TERRAIN_GRASSY:
        hit = bw_bh_move_in((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), k_grassy_weakened,
                            (int)(sizeof k_grassy_weakened / sizeof k_grassy_weakened[0]));
        break;
    default:
        return;
    }
    if (hit && is_grounded((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID))) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, w[WORK_KIND] == TERRAIN_MISTY ? (int)w[WORK_RATIO] : BTL_RATIO_HALF);
    }
}

/* 0x38 (Charge's power step): Grassy, Electric and Psychic Terrain give
 * a grounded Pokemon's moves of their type more power (`ratio`). */
static void terrain_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    (void)ownerId;
    if (w[WORK_KIND] != TERRAIN_MISTY && w[WORK_KIND] < 4 &&
        BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == k_terrain_type[w[WORK_KIND]] &&
        is_grounded((struct BtlServerFlow *)context, (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID))) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_POWER_RATIO, (int)w[WORK_RATIO]);
    }
}

/* the priority the flow gives a Pokemon's move (the record's, plus 7, with
 * the abilities' raise: Prankster) */
extern int BTL_SERVER_FLOW_CalcMovePriority(struct BtlServerFlow *flow, u32 moveId, struct BtlPokeParam *pokeParam);
#define BTL_PRIORITY_ZERO       7

/* 0x2C, the no-effect check (Quick Guard's): Psychic Terrain keeps a
 * grounded Pokemon safe from another side's moves of raised priority, the
 * move record's or an ability's (Prankster). */
static void terrain_block_priority(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    int attackerId = (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID);
    u16 move = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    BtlStrParam *message;

    (void)ownerId;
    if (w[WORK_KIND] != TERRAIN_PSYCHIC || BTL_MAIN_IsPokeSamePair(attackerId, defenderId) != 0 ||
        BTL_SERVER_FLOW_CalcMovePriority((struct BtlServerFlow *)context, move,
                                         BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context, attackerId))
            <= BTL_PRIORITY_ZERO ||
        !is_grounded((struct BtlServerFlow *)context, defenderId)) {
        return;
    }
    if (BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1) != 0 && line_ok(BTL_STRTYPE_SET, LINE_PSYCHIC_BLOCK)) {
        message = (BtlStrParam *)BTL_EVENT_GetVar(BTL_EVAR_MESSAGE);
        BTL_SERVER_FLOW_InitMsgRecord(message, BTL_STRTYPE_SET, LINE_PSYCHIC_BLOCK);
        BTL_SERVER_FLOW_AppendMsgRecordArg(message, (u32)defenderId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(message, BTL_EVENT_GetFactorKey(factor));
    }
}

/* 0x77, the turn check: Grassy heals each grounded Pokemon (`hp`: 1/16,
 * the line of a healed Pokemon); the field's pass counts one turn gone,
 * and after the last the end line (the game's field effects end in the
 * turn check's step before). */
static void terrain_turn_end(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 *w = (u32 *)work;
    int pokeId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);

    (void)ownerId;
    if (pokeId != BTL_TURN_CHECK_FIELD) {
        struct BtlPokeParam *pp;
        HandExRecoverHp *heal;

        if (w[WORK_KIND] != TERRAIN_GRASSY || !is_grounded(flow, pokeId)) {
            return;
        }
        pp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        if (BTL_POKEPARAM_IsFainted(pp) || BTL_POKEPARAM_IsHpFull(pp)) {
            return;
        }
        if ((heal = (HandExRecoverHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RECOVER_HP, pokeId)) != 0) {
            heal->pokeId = (u8)pokeId;
            heal->amount = BTL_CALC_DivideMaxHpMin1(pp, w[WORK_HP]);
            BTL_SERVER_FLOW_InitMsgRecord(&heal->message, BTL_STRTYPE_SET, BTL_STR_HP_RESTORED);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&heal->message, (u32)pokeId);
            BTL_SERVER_FLOW_CommitCmdRecord(flow, heal);
        }
        return;
    }
    if (w[WORK_TURNS] > 0) {
        w[WORK_TURNS]--;
    }
    if (w[WORK_TURNS] == 0) {
        show_move_message(flow, 0, BTL_STRTYPE_STD, w[WORK_KIND] < 4 ? k_terrain_lines[w[WORK_KIND]][1] : LINE_MISTY_END,
                          BTL_EVENT_GetFactorKey(factor));
        BTL_EVENT_RemoveFactor(factor);
    }
}

static const BtlEventHandler k_terrain_field[] = {
    { 0x65, terrain_block_sick },
    { 0x67, terrain_notice },
    { 0x0E, terrain_block_rest },
    { 0x47, terrain_damage_ratio },
    { 0x38, terrain_power },
    { 0x2C, terrain_block_priority },
    { 0x77, terrain_turn_end },
};

/* 0x9E: the terrain for five turns (`turns`).  A terrain of another kind
 * that is up goes (the later games' terrains replace each other); the same
 * kind up already means nothing is committed, and the game says the move
 * failed. */
static void terrain_start(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlEventFactor *field;
    int kind, ratio;

    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    switch (bw_bh_running_base(BTL_EVENT_TYPE_WAZA, BTL_EVENT_GetFactorKey(factor))) {
    case NAMED_GRASSY_TERRAIN:   kind = TERRAIN_GRASSY; break;
    case NAMED_ELECTRIC_TERRAIN: kind = TERRAIN_ELECTRIC; break;
    case NAMED_PSYCHIC_TERRAIN:  kind = TERRAIN_PSYCHIC; break;
    default:                     kind = TERRAIN_MISTY; break;
    }
    if ((field = BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_TERRAIN)) != 0) {
        if (BTL_EVENT_GetFactorWork(field, WORK_KIND) == (u32)kind) {
            return;
        }
        BTL_EVENT_RemoveFactor(field);
    }
    field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0, OWNER_TERRAIN,
                                k_terrain_field, (u16)(sizeof k_terrain_field / sizeof k_terrain_field[0]));
    if (field != 0) {
        ratio = kind == TERRAIN_MISTY ? BTL_RATIO_HALF : TERRAIN_POWER_RATIO;
        BTL_EVENT_SetFactorWork(field, WORK_TURNS, (u32)bw_bh_arg("turns", 1, TERRAIN_TURNS));
        BTL_EVENT_SetFactorWork(field, WORK_KIND, (u32)kind);
        BTL_EVENT_SetFactorWork(field, WORK_RATIO, (u32)bw_bh_arg("ratio", 1, ratio));
        BTL_EVENT_SetFactorWork(field, WORK_HP, (u32)bw_bh_arg("hp", 1, GRASSY_HEAL_HP));
        show_move_message(flow, ownerId, BTL_STRTYPE_STD, k_terrain_lines[kind][0], BTL_EVENT_GetFactorKey(field));
    }
}

static const BtlEventHandler k_misty_terrain[] = {
    { 0x9E, terrain_start },
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

    (void)context;
    (void)work;
    if (ownerId - OWNER_CRAFTY_SHIELD == BTL_MAIN_IsPokeIdInSecondPair((u32)defenderId) &&
        defenderId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && WAZA_TOOL_IsDamaging(move) == 0 &&
        WAZA_TOOL_GetParam(move, WAZA_PARAM_TARGET_RANGE) != WAZA_RANGE_ALL &&
        BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1) != 0 && line_ok(BTL_STRTYPE_SET, LINE_CRAFTY_BLOCK)) {
        message = (BtlStrParam *)BTL_EVENT_GetVar(BTL_EVAR_MESSAGE);
        BTL_SERVER_FLOW_InitMsgRecord(message, BTL_STRTYPE_SET, LINE_CRAFTY_BLOCK);
        BTL_SERVER_FLOW_AppendMsgRecordArg(message, (u32)defenderId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(message, BTL_EVENT_GetFactorKey(factor));    /* (the guard's move) */
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
        show_move_message((struct BtlServerFlow *)context, ownerId, BTL_STRTYPE_SET, LINE_CRAFTY_START,
                          (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID));
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
        cmd->sickCont = BTL_CALC_MakeSickContTurns(2).raw;
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

/* --- Fell Stinger ------------------------------------------------- */

#define BTL_EVAR_DEFENDER_COUNT 0x05
#define BTL_EVAR_DEFENDER_FIRST 0x06
#define BTL_RANK_ATTACK         1
#define FELL_STINGER_STAGES     3       /* (Generation VII's; `stages=2` for VI's) */

extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);

/* 0x83, after the damage (Moxie's event): when a target fainted, the user's
 * Attack rises by 3 (`stat`, `stages`), in one stat change with its line. */
static void fell_stinger_on_ko(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    HandExRankChange *cmd;
    int n, i;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        if (BTL_POKEPARAM_IsFainted(BTL_SERVER_FLOW_GetPokeParam(flow, (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i)))) {
            break;
        }
    }
    if (i == n || (cmd = (HandExRankChange *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RANK_EFFECT, ownerId)) == 0) {
        return;
    }
    cmd->rankKind = BTL_RANK_ATTACK;
    cmd->volume = FELL_STINGER_STAGES;
    cmd->showMessage = 1;
    cmd->targetCount = 1;
    cmd->targets[0] = ownerId;
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

static const BtlEventHandler k_fell_stinger[] = {
    { 0x83, fell_stinger_on_ko },
};

/* --- needs_status ------------------------------------------------- */

#define BTL_EVAR_MOVE_FAIL_FLAG 0x40
#define BTL_SICK_POISON         5

/* sdk/battle_handlers.c: the status a key names */
extern int bw_bh_sick(u32 *kind);

/* 0x2C, the no-effect check (Dream Eater's): the move fails on a target
 * without the status (`status`, poison: Venom Drench).  The move record
 * gives what it does to one that has it. */
static void needs_status_check(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 status = BTL_SICK_POISON;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    (void)bw_bh_sick(&status);
    if (BTL_POKEPARAM_HasSick(BTL_SERVER_FLOW_GetPokeParam(flow, (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID)),
                              (int)status) == 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_FAIL_FLAG, 1);
    }
}

static const BtlEventHandler k_needs_status[] = {
    { 0x2C, needs_status_check },
};

/* --- The guards: Spiky Shield, King's Shield, Baneful Bunker ------- */

#define NAMED_SPIKY_SHIELD    0x10007
#define NAMED_KINGS_SHIELD    0x10008
#define NAMED_BANEFUL_BUNKER  0x10009
#define NAMED_CONTACT_GUARD   0x1000A

#define SPIKY_SHIELD_HP       8         /* 1/8 of the attacker's HP (Rough Skin's) */
#define KINGS_SHIELD_STAGES   (-2)      /* (Generations VI and VII; `stages=-1` for VIII) */

/* Protect's handler table: rows 0 to 2 are the streak (the rows Endure and
 * Quick Guard share), row 3 the move itself (ProtectActivate) */
extern const BtlEventHandler gProtectHandlers[4];
extern void HAND_WAZA_ProtectActivate(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work);
extern u16 BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern u32 BTL_SERVER_FLOW_GetTurnCount(struct BtlServerFlow *flow);
extern int bw_bh_running_base(int type, int id);
extern int bw_bh_arg(const char *key, int index, int fallback);

/* sdk/battle_moves.c */
extern void bw_bm_guard_set(int pokeId, int turn, int damagingOnly, int hp, int stat, int stages, int status);
extern void bw_bm_guard_clear(int pokeId);
extern void bw_bm_third_type_set(int pokeId, const void *poke, int type);

/* Row 3: Protect's own (the turn flag, the streak, "protected itself!"),
 * then the note BTL_SERVER_FLOW_ProcNoEffectChecks reads when the guard
 * stops a contact move this turn: what it does to the attacker, from the
 * base and its keys (`hp`, `stat`, `stages`, `status`). */
static void guard_activate(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    int base, hp = 0, stat = 0, stages = 0, status = 0;

    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    HAND_WAZA_ProtectActivate(factor, flow, ownerId, (int *)work);
    base = bw_bh_running_base(BTL_EVENT_TYPE_WAZA, BTL_EVENT_GetFactorKey(factor));
    switch (base) {
    case NAMED_SPIKY_SHIELD:   hp = SPIKY_SHIELD_HP; break;
    case NAMED_KINGS_SHIELD:   stat = BTL_RANK_ATTACK; stages = KINGS_SHIELD_STAGES; break;
    case NAMED_BANEFUL_BUNKER: status = BTL_SICK_POISON; break;
    }
    hp = bw_bh_arg("hp", 1, hp);
    stat = bw_bh_arg("stat", 1, stat);
    stages = bw_bh_arg("stages", 1, stages);
    status = bw_bh_arg("status", 1, status);
    if (stat == 0 && stages != 0) {
        stat = BTL_RANK_ATTACK;         /* (`stages` alone: Attack, as King's Shield) */
    }
    bw_bm_guard_set(ownerId, (int)BTL_SERVER_FLOW_GetTurnCount(flow), base == NAMED_KINGS_SHIELD, hp, stat, stages,
                    status);
}

static BtlEventHandler k_guard[4];

/* The guards' table: Protect's streak rows, made once from the game's own
 * table (their event ids are its), and guard_activate on its move event. */
static const BtlEventHandler *guard_handlers(void)
{
    int i;

    if (k_guard[3].handler == 0) {
        for (i = 0; i < 3; i++) {
            k_guard[i] = gProtectHandlers[i];
        }
        k_guard[3].eventId = gProtectHandlers[3].eventId;
        k_guard[3].handler = guard_activate;
    }
    return k_guard;
}

/* --- add_type: Trick-or-Treat, Forest's Curse ---------------------- */

#define NAMED_ADD_TYPE        0x1000F
#define NAMED_TRICK_OR_TREAT  0x10010
#define NAMED_FORESTS_CURSE   0x10011
#define POKETYPE_GHOST        7
#define LINE_ADD_TYPE         2030      /* bank 14, set of three: the Pokemon, the type */

/* 0xA0 (the move, Soak's event): each target gets the type as a third type
 * (`type`: Ghost, Grass), in place of a third type it had; a target that
 * has the type already is left as it is, and with no target changed the
 * move fails.  The line says the type was added (sdk/battle_handlers.c:
 * the game's Soak line says a type replaced the others). */
static void add_type_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    int n, i, type, target, base;

    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    base = bw_bh_running_base(BTL_EVENT_TYPE_WAZA, BTL_EVENT_GetFactorKey(factor));
    type = base == NAMED_FORESTS_CURSE ? POKETYPE_GRASS : POKETYPE_GHOST;
    type = bw_bh_arg("type", 1, type);
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        HandExMessage *msg;

        struct BtlPokeParam *pp;

        target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
        pp = BTL_SERVER_FLOW_GetPokeParam(flow, target);
        if (BTL_POKEPARAM_HasType(pp, type)) {
            continue;
        }
        bw_bm_third_type_set(target, pp, type);
        /* (in a language with no line the record goes in empty: see show_message) */
        if ((msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, ownerId)) != 0) {
            if (line_ok(BTL_STRTYPE_SET, LINE_ADD_TYPE)) {
                BTL_SERVER_FLOW_InitMsgRecord(&msg->message, BTL_STRTYPE_SET, LINE_ADD_TYPE);
                BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)target);
                BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)type);
            }
            BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
        }
    }
}

static const BtlEventHandler k_add_type[] = {
    { 0xA0, add_type_apply },
};

/* --- The later moves: one handler each ---------------------------- */

#define NAMED_PARTING_SHOT    0x10016
#define NAMED_TOPSY_TURVY     0x10017
#define NAMED_NEEDS_BERRY     0x10018
#define NAMED_BELCH           0x10019
#define NAMED_BURN_UP         0x1001A
#define NAMED_SHORE_UP        0x1001B
#define NAMED_STRENGTH_SAP    0x1001C
#define NAMED_ROTOTILLER      0x1001D
#define NAMED_MAGNETIC_FLUX   0x1001E
#define NAMED_SET_TYPE        0x1001F

#define LINE_TOPSY_TURVY      2018      /* bank 14, set of three */
#define LINE_BURN_UP          2021      /* bank 14, set of three */

#define HANDEX_KIND_SET_RANKS   0x0F
#define BTL_EVAR_FAIL_REASON    0x22
#define BTL_FAIL_REASON_MOVE_FAILED 0x1a
#define HAND_WAZA_KIND_CHANGE_TYPE 0x14
#define BTL_EVAR_HEAL_RATIO     0x35
#define BTL_VALUE_ATTACK        8       /* the Attack stat with its stages (BTL_POKEPARAM_GetValue) */
#define BTL_VALUE_ABILITY       0x11
#define BTL_RANK_NEUTRAL        6       /* a stage as BtlPokeParam keeps it: 0..12 */
#define BTL_RANK_KINDS          7       /* Attack .. evasion */
#define BTL_RANK_SP_ATTACK      3
#define BTL_RANK_SP_DEFENSE     4
#define BTL_WEATHER_SAND        4
#define POKETYPE_FIRE           9
#define POKETYPE_WATER          10
#define POKETYPE_NONE           31      /* (the port's "no type", arm9 poke_type.h) */
#define ABILITY_PLUS            57
#define ABILITY_MINUS           58
#define SHORE_UP_RATIO          0x800   /* half the HP */
#define SHORE_UP_SAND_RATIO     0xAAB   /* two thirds in a sandstorm (Generation VII) */

/* HANDEX_KIND_SET_RANKS: the seven stages of a Pokemon (Psych Up's) */
typedef struct {
    u32 header;
    u8 pokeId;
    u8 ranks[BTL_RANK_KINDS];
} HandExSetRanks;

/* HAND_WAZA_KIND_CHANGE_TYPE (the flow's set-types command: noLine 1 keeps
 * its "transformed into the ... type!" line from showing) */
typedef struct {
    u32 header;
    u16 typePair;
    u8 pokeId;
    u8 noLine;
} HandExChangeType;

extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern u8 BTL_TYPEPAIR_GetHigh(u16 pair);
extern u8 BTL_TYPEPAIR_GetLow(u16 pair);
extern u16 BTL_POKEPARAM_GetConsumedItem(struct BtlPokeParam *pp);
extern int ITEM_IsBerry(u32 itemId);
extern int BTL_SERVER_FLOW_GetEffectiveWeatherThunk(struct BtlServerFlow *flow);
extern u16 BTL_TYPEPAIR_Make(u32 high, u32 low);
extern int BTL_TYPEPAIR_MakeSingle(int type);
extern int bw_bh_ability_is(int ability, int builtin);
extern void HAND_WAZA_UTurnSwitchOut(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId);

/* One stat change of `stages` on `count` Pokemon, with the game's lines. */
static void raise_stat(struct BtlServerFlow *flow, int ownerId, int stat, int stages, const u8 *ids, int count)
{
    HandExRankChange *cmd;
    int i;

    if (count == 0 || stages == 0 ||
        (cmd = (HandExRankChange *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RANK_EFFECT, ownerId)) == 0) {
        return;
    }
    cmd->rankKind = (u32)stat;
    cmd->volume = (s8)stages;
    cmd->showMessage = 1;
    cmd->targetCount = (u8)count;
    for (i = 0; i < count && i < 8; i++) {
        cmd->targets[i] = ids[i];
    }
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

/* 0xA0 (Psych Up's event): Topsy-Turvy: each target's stat stages turn
 * the other way, in one record (Psych Up's), with one line.  A target
 * with no stage changed is left alone, and with none changed the move
 * fails. */
static void topsy_turvy_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    int n, i, k, target, changed;
    HandExSetRanks *ranks;
    u8 now[BTL_RANK_KINDS];

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        struct BtlPokeParam *pp;

        target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
        pp = BTL_SERVER_FLOW_GetPokeParam(flow, target);
        for (k = 0, changed = 0; k < BTL_RANK_KINDS; k++) {
            now[k] = (u8)BTL_POKEPARAM_GetValue(pp, 1 + k);
            changed |= now[k] != BTL_RANK_NEUTRAL;
        }
        if (!changed || (ranks = (HandExSetRanks *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_SET_RANKS,
                                                                                  ownerId)) == 0) {
            continue;
        }
        ranks->pokeId = (u8)target;
        for (k = 0; k < BTL_RANK_KINDS; k++) {
            ranks->ranks[k] = (u8)(2 * BTL_RANK_NEUTRAL - now[k]);
        }
        BTL_SERVER_FLOW_CommitCmdRecord(flow, ranks);
        show_message(flow, target, BTL_STRTYPE_SET, LINE_TOPSY_TURVY, 1);
    }
}

static const BtlEventHandler k_topsy_turvy[] = {
    { 0xA0, topsy_turvy_apply },
};

/* 0xA0: Strength Sap: the user heals as much HP as the target's Attack
 * (its stages counted), then the target's Attack falls (`stat`, `stages`:
 * Attack, -1).  It fails on a target whose Attack is at its lowest. */
static void strength_sap_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlPokeParam *pp, *user;
    HandExRecoverHp *heal;
    int target, amount;
    u8 id;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT) == 0) {
        return;
    }
    target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST);
    pp = BTL_SERVER_FLOW_GetPokeParam(flow, target);
    if (BTL_POKEPARAM_GetValue(pp, BTL_RANK_ATTACK) == 0) {
        return;
    }
    amount = BTL_POKEPARAM_GetValue(pp, BTL_VALUE_ATTACK);
    id = (u8)target;
    raise_stat(flow, ownerId, bw_bh_arg("stat", 1, BTL_RANK_ATTACK), bw_bh_arg("stages", 1, -1), &id, 1);
    user = BTL_SERVER_FLOW_GetPokeParam(flow, ownerId);
    if (BTL_POKEPARAM_IsHpFull(user) == 0 && amount > 0 &&
        (heal = (HandExRecoverHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RECOVER_HP, ownerId)) != 0) {
        heal->pokeId = ownerId;
        heal->amount = (u16)(amount > 0xFFFF ? 0xFFFF : amount);
        BTL_SERVER_FLOW_InitMsgRecord(&heal->message, BTL_STRTYPE_SET, BTL_STR_HP_RESTORED);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&heal->message, ownerId);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, heal);
    }
}

static const BtlEventHandler k_strength_sap[] = {
    { 0xA0, strength_sap_apply },
};

/* 0x8F (Morning Sun's event): Shore Up heals half the HP (`ratio`), two
 * thirds in a sandstorm (`ratio.2`). */
static void shore_up_ratio(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int ratio;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        return;
    }
    if (BTL_SERVER_FLOW_GetEffectiveWeatherThunk((struct BtlServerFlow *)context) == BTL_WEATHER_SAND) {
        ratio = bw_bh_arg("ratio", 2, SHORE_UP_SAND_RATIO);
    } else {
        ratio = bw_bh_arg("ratio", 1, SHORE_UP_RATIO);
    }
    BTL_EVENT_OverwriteVar(BTL_EVAR_HEAL_RATIO, (__UINTPTR_TYPE__)ratio);
}

static const BtlEventHandler k_shore_up[] = {
    { 0x8F, shore_up_ratio },
};

/* 0x1F (Last Resort's event): needs_berry (Belch): the move fails unless
 * the user has eaten a berry since it came in (the item it used up). */
static void needs_berry_check(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) &&
        ITEM_IsBerry(BTL_POKEPARAM_GetConsumedItem(BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context,
                                                                                ownerId))) == 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_MOVE_FAILED);
    }
}

static const BtlEventHandler k_needs_berry[] = {
    { 0x1F, needs_berry_check },
};

/* Burn Up: 0x1F, the move fails unless the user has the type (`type`:
 * Fire); 0x83, after the damage, the user loses the type, with its own
 * line.  A Pokemon of that type alone is left with no type, as in the
 * later games: the game's own "no type" (POKETYPE_NONE), which every move
 * hits for neutral damage (BTL_CALC_GetTypeAffinity), gives no same-type
 * power and that HasType never matches.  In a language with no Burn Up
 * line the game's own type-change line names the type that is left. */
static void burn_up_check(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) &&
        BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context, ownerId),
                              bw_bh_arg("type", 1, POKETYPE_FIRE)) == 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_MOVE_FAILED);
    }
}

static void burn_up_lose_type(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    HandExChangeType *cmd;
    u16 pair;
    int type, high, low;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    type = bw_bh_arg("type", 1, POKETYPE_FIRE);
    pair = BTL_POKEPARAM_GetTypePair(BTL_SERVER_FLOW_GetPokeParam(flow, ownerId));
    high = BTL_TYPEPAIR_GetHigh(pair);
    low = BTL_TYPEPAIR_GetLow(pair);
    if (high != type && low != type) {
        return;
    }
    high = high == type ? low : high;
    low = low == type ? high : low;
    if (high == type) {
        high = low = POKETYPE_NONE;
    }
    if ((cmd = (HandExChangeType *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_CHANGE_TYPE, ownerId)) != 0) {
        cmd->typePair = high == low ? (u16)BTL_TYPEPAIR_MakeSingle(high) : BTL_TYPEPAIR_Make((u32)high, (u32)low);
        cmd->pokeId = ownerId;
        cmd->noLine = (u8)(line_ok(BTL_STRTYPE_SET, LINE_BURN_UP) || high == POKETYPE_NONE);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
        show_message(flow, ownerId, BTL_STRTYPE_SET, LINE_BURN_UP, 1);
    }
}

static const BtlEventHandler k_burn_up[] = {
    { 0x1F, burn_up_check },
    { 0x83, burn_up_lose_type },
};

/* 0x9E: Rototiller: every grounded Pokemon of the type (`type`: Grass) on
 * the field has two stats raised (`stat`, `stages`, `stat.2`,
 * `stages.2`: Attack and Sp. Atk, +1).  With none, the move fails. */
static void rototiller_raise(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u8 all[8], kept[8];
    int n, i, k = 0, type;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    type = bw_bh_arg("type", 1, POKETYPE_GRASS);
    n = BTL_SERVER_FLOW_CollectAlivePokeIds(flow, BTL_COLLECT_ALL | BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, ownerId), all);
    for (i = 0; i < n && i < 8; i++) {
        if (BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam(flow, all[i]), type) && is_grounded(flow, all[i])) {
            kept[k++] = all[i];
        }
    }
    raise_stat(flow, ownerId, bw_bh_arg("stat", 1, BTL_RANK_ATTACK), bw_bh_arg("stages", 1, 1), kept, k);
    raise_stat(flow, ownerId, bw_bh_arg("stat", 2, BTL_RANK_SP_ATTACK), bw_bh_arg("stages", 2, 1), kept, k);
}

static const BtlEventHandler k_rototiller[] = {
    { 0x9E, rototiller_raise },
};

/* 0xA1 (a move for the user's side, Quick Guard's event): Magnetic Flux:
 * every Pokemon of the user's side with Plus or Minus (or an ability that
 * works like one) has two stats raised (`stat`, `stages`, `stat.2`,
 * `stages.2`: Defense and Sp. Def, +1).  With none, the move fails. */
static void magnetic_flux_raise(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u8 all[8], kept[8];
    int n, i, k = 0, ability;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = BTL_SERVER_FLOW_CollectAlivePokeIds(flow, BTL_COLLECT_ALL | BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, ownerId), all);
    for (i = 0; i < n && i < 8; i++) {
        if (all[i] != ownerId && BTL_MAIN_IsPokeSamePair(ownerId, all[i]) == 0) {
            continue;
        }
        ability = BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, all[i]), BTL_VALUE_ABILITY);
        if (bw_bh_ability_is(ability, ABILITY_PLUS) || bw_bh_ability_is(ability, ABILITY_MINUS)) {
            kept[k++] = all[i];
        }
    }
    raise_stat(flow, ownerId, bw_bh_arg("stat", 1, BTL_RANK_DEFENSE), bw_bh_arg("stages", 1, 1), kept, k);
    raise_stat(flow, ownerId, bw_bh_arg("stat", 2, BTL_RANK_SP_DEFENSE), bw_bh_arg("stages", 2, 1), kept, k);
}

static const BtlEventHandler k_magnetic_flux[] = {
    { 0xA1, magnetic_flux_raise },
};

/* 0xA0 (Soak's event): set_type: each target becomes the type alone
 * (`type`: Water), with the game's line. */
static void set_type_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    HandExChangeType *cmd;
    int n, i, type;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    type = bw_bh_arg("type", 1, POKETYPE_WATER);
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        if ((cmd = (HandExChangeType *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_CHANGE_TYPE, ownerId)) != 0) {
            cmd->typePair = (u16)BTL_TYPEPAIR_MakeSingle(type);
            cmd->pokeId = (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
            BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
        }
    }
}

static const BtlEventHandler k_set_type[] = {
    { 0xA0, set_type_apply },
};

/* 0xA0: Parting Shot: the target's stats fall (`stat`, `stages`,
 * `stat.2`, `stages.2`: Attack and Sp. Atk, -1), then the user goes back
 * to its ball as after U-turn (Volt Switch's line), when it has a
 * Pokemon to send out.  The move record is a status move of no category
 * (as Soak), so the stat changes are the handler's. */
static void parting_shot_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u8 id;

    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT) == 0) {
        return;
    }
    id = (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST);
    raise_stat(flow, ownerId, bw_bh_arg("stat", 1, BTL_RANK_ATTACK), bw_bh_arg("stages", 1, -1), &id, 1);
    raise_stat(flow, ownerId, bw_bh_arg("stat", 2, BTL_RANK_SP_ATTACK), bw_bh_arg("stages", 2, -1), &id, 1);
    HAND_WAZA_UTurnSwitchOut(factor, flow, ownerId);
}

static const BtlEventHandler k_parting_shot[] = {
    { 0xA0, parting_shot_apply },
};

/* --- Electrify and Ion Deluge: a move's type for the turn ---------- */

#define NAMED_ELECTRIFY       0x10020
#define NAMED_ION_DELUGE      0x10021
#define OWNER_ION_DELUGE      0x45
#define OWNER_ELECTRIFY       0x50      /* + the target (0..23) */
#define LINE_ION_DELUGE       409       /* bank 15 */
#define LINE_ELECTRIFY        2024      /* bank 14, set of three */
#define POKETYPE_NORMAL       0
#define WORK_TARGET           0         /* Electrify: the target */
#define WORK_FROM             0         /* Ion Deluge: the type it changes (Normal) */
#define WORK_TO               1         /* the type moves take (Electric) */

/* 0x28 (Weather Ball's event, the move's type): Electrify: the target's
 * moves are of the type (`type`: Electric) for the rest of the turn. */
static void electrify_type(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    (void)context;
    (void)ownerId;
    if (BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) == (sptr)w[WORK_TARGET]) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, w[WORK_TO]);
    }
}

/* 0x28: Ion Deluge: Normal moves (`type.2`) are of the type (`type`:
 * Electric) for the rest of the turn. */
static void ion_deluge_type(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    (void)context;
    (void)ownerId;
    if (BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == (sptr)w[WORK_FROM]) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, w[WORK_TO]);
    }
}

/* 0x76, the field's pass: they end with the turn (Crafty Shield's). */
static const BtlEventHandler k_electrify_field[] = {
    { 0x28, electrify_type },
    { 0x76, crafty_turn_end },
};

static const BtlEventHandler k_ion_deluge_field[] = {
    { 0x28, ion_deluge_type },
    { 0x76, crafty_turn_end },
};

/* 0xA0: Electrify on each target that has none yet; with none, the move
 * fails. */
static void electrify_start(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlEventFactor *field;
    int n, i, target;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
        if (BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_ELECTRIFY + target) != 0) {
            continue;
        }
        field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0,
                                    (u8)(OWNER_ELECTRIFY + target), k_electrify_field,
                                    (u16)(sizeof k_electrify_field / sizeof k_electrify_field[0]));
        if (field != 0) {
            BTL_EVENT_SetFactorWork(field, WORK_TARGET, (u32)target);
            BTL_EVENT_SetFactorWork(field, WORK_TO, (u32)bw_bh_arg("type", 1, POKETYPE_ELECTRIC));
            show_message((struct BtlServerFlow *)context, target, BTL_STRTYPE_SET, LINE_ELECTRIFY, 1);
        }
    }
}

static const BtlEventHandler k_electrify[] = {
    { 0xA0, electrify_start },
};

/* 0x9E: Ion Deluge for the turn, unless it is up. */
static void ion_deluge_start(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlEventFactor *field;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) ||
        BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_ION_DELUGE) != 0) {
        return;
    }
    field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0, OWNER_ION_DELUGE,
                                k_ion_deluge_field, (u16)(sizeof k_ion_deluge_field / sizeof k_ion_deluge_field[0]));
    if (field != 0) {
        BTL_EVENT_SetFactorWork(field, WORK_FROM, (u32)bw_bh_arg("type", 2, POKETYPE_NORMAL));
        BTL_EVENT_SetFactorWork(field, WORK_TO, (u32)bw_bh_arg("type", 1, POKETYPE_ELECTRIC));
        show_move_message((struct BtlServerFlow *)context, ownerId, BTL_STRTYPE_STD, LINE_ION_DELUGE,
                          BTL_EVENT_GetFactorKey(field));
    }
}

static const BtlEventHandler k_ion_deluge[] = {
    { 0x9E, ion_deluge_start },
};

/* --- Sticky Web: a side factor of its own -------------------------- */

#define NAMED_STICKY_WEB      0x10022
#define OWNER_STICKY_WEB      0x43      /* + the side */
#define LINE_STICKY_WEB_US    410       /* bank 15: laid on the player's side */
#define LINE_STICKY_WEB_FOE   411       /* bank 15: laid on the foe's side */
#define LINE_STICKY_WEB_CAUGHT 2027     /* bank 14, set of three */
#define BTL_RANK_SPEED        5
#define WORK_WEB_STAT         0
#define WORK_WEB_STAGES       1

/* 0x55 (Spikes' and Stealth Rock's event, a Pokemon comes in): a grounded
 * Pokemon of the side has its stat lowered (`stat`, `stages`: Speed, -1). */
static void sticky_web_on_enter(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 *w = (u32 *)work;
    int pokeId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    u8 id = (u8)pokeId;

    (void)factor;
    if (ownerId - OWNER_STICKY_WEB != BTL_MAIN_IsPokeIdInSecondPair((u32)pokeId) || !is_grounded(flow, pokeId)) {
        return;
    }
    show_message(flow, pokeId, BTL_STRTYPE_SET, LINE_STICKY_WEB_CAUGHT, 1);
    raise_stat(flow, pokeId, (int)w[WORK_WEB_STAT], (int)w[WORK_WEB_STAGES], &id, 1);
}

#define BTL_MOVE_RAPID_SPIN   229
#define BTL_MOVE_DEFOG        432
#define LINE_STICKY_WEB_GONE_US  412    /* bank 15 */
#define LINE_STICKY_WEB_GONE_FOE 413    /* bank 15 */

extern int bw_bh_move_is(int move, int builtin);

/* The web goes from its side, with Spikes' kind of line. */
static void sticky_web_clear(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int ownerId, int userId)
{
    show_move_message(flow, userId, BTL_STRTYPE_STD,
                      ownerId - OWNER_STICKY_WEB == 0 ? LINE_STICKY_WEB_GONE_US : LINE_STICKY_WEB_GONE_FOE,
                      BTL_EVENT_GetFactorKey(factor));
    BTL_EVENT_RemoveFactor(factor);
}

/* 0xA0 (Defog's event): Defog, or a move that works like it, takes the web
 * off its target's side, as it takes the game's side effects. */
static void sticky_web_defog(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int targetId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST);     /* (var 6: Defog's target) */

    (void)work;
    if (bw_bh_move_is((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), BTL_MOVE_DEFOG) &&
        ownerId - OWNER_STICKY_WEB == BTL_MAIN_IsPokeIdInSecondPair((u32)targetId)) {
        sticky_web_clear(factor, (struct BtlServerFlow *)context, ownerId, (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID));
    }
}

/* 0x4D (Rapid Spin's event, the damage dealt): Rapid Spin, or a move that
 * works like it, takes the web off its user's side. */
static void sticky_web_rapid_spin(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int attackerId = (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID);

    (void)work;
    if (bw_bh_move_is((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), BTL_MOVE_RAPID_SPIN) &&
        ownerId - OWNER_STICKY_WEB == BTL_MAIN_IsPokeIdInSecondPair((u32)attackerId)) {
        sticky_web_clear(factor, (struct BtlServerFlow *)context, ownerId, attackerId);
    }
}

static const BtlEventHandler k_sticky_web_side[] = {
    { 0x55, sticky_web_on_enter },
    { 0xA0, sticky_web_defog },
    { 0x4D, sticky_web_rapid_spin },
};

/* 0xA1: the web on the foes' side, unless one is up there (then the move
 * fails). */
static void sticky_web_lay(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlEventFactor *side;
    int foeSide = BTL_MAIN_IsPokeIdInSecondPair(ownerId) ? 0 : 1;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) ||
        BTL_EVENT_FindFactor(BTL_EVENT_TYPE_SIDE, OWNER_STICKY_WEB + foeSide) != 0) {
        return;
    }
    side = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_SIDE, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 2, 0,
                               (u8)(OWNER_STICKY_WEB + foeSide), k_sticky_web_side,
                               (u16)(sizeof k_sticky_web_side / sizeof k_sticky_web_side[0]));
    if (side != 0) {
        BTL_EVENT_SetFactorWork(side, WORK_WEB_STAT, (u32)bw_bh_arg("stat", 1, BTL_RANK_SPEED));
        BTL_EVENT_SetFactorWork(side, WORK_WEB_STAGES, (u32)bw_bh_arg("stages", 1, -1));
        show_move_message((struct BtlServerFlow *)context, ownerId, BTL_STRTYPE_STD,
                          foeSide == 0 ? LINE_STICKY_WEB_US : LINE_STICKY_WEB_FOE, BTL_EVENT_GetFactorKey(side));
    }
}

static const BtlEventHandler k_sticky_web[] = {
    { 0xA1, sticky_web_lay },
};

/* --- Powder: a Fire move of the target explodes ------------------- */

#define NAMED_POWDER          0x10023
#define OWNER_POWDER          0x80      /* + the target (0..23) */
#define LINE_POWDER           2033      /* bank 14, set of three */
#define LINE_POWDER_BLAST     414       /* bank 15 */
#define POWDER_HP             4         /* a quarter of the user's HP */
#define BTL_FAIL_REASON_SILENT 25       /* BTL_SERVER_FLOW_PushMoveFailMessage shows no line for it */
#define HANDEX_KIND_DAMAGE    7
#define WORK_POWDER_TARGET    0
#define WORK_POWDER_TYPE      1
#define WORK_POWDER_HP        2
#define FLOW_CMD_LOCK(flow)   ((u32 *)((u8 *)(flow) + 0x1D74))  /* btl_server_flow_types.h cmdLock */

/* btl_server_types.h struct BtlMoveParam, its head */
typedef struct {
    u16 moveId;
    u16 origMoveId;
    u16 typePair;
    u8 type;                            /* after the type events 0x28 and 0xA2 */
} PowderMoveParam;

/* btl_server_flow_types.h: the flow up to curMoveParam (+0x1AB0, the DS
 * layout on every build, as patch_ov093_hand_tokusei.c reads it) */
typedef struct {
    u8 fields000[0x1AB0];
    PowderMoveParam *curMoveParam;
} PowderFlowHead;

extern u32 WAZA_TOOL_GetType(u16 move);

/* The type the move has in this battle: the flow's move parameters for
 * the move it runs (BTL_SERVER_FLOW_ProcMoveExecute sets the type, after
 * Electrify, Ion Deluge, Weather Ball, Normalize and the -ate bases,
 * before the fail check), else the record's. */
static u32 battle_move_type(struct BtlServerFlow *flow, u16 move)
{
    const PowderMoveParam *mp = ((const PowderFlowHead *)flow)->curMoveParam;

    return mp != 0 && mp->moveId == move ? mp->type : WAZA_TOOL_GetType(move);
}

/* HANDEX_KIND_DAMAGE */
typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 flags;
    u32 pad08;
    BtlStrParam message;
} HandExDamage;

_Static_assert(__builtin_offsetof(HandExDamage, message) == 0x0C, "HandExDamage.message");

extern u32 BTL_SERVER_FLOW_BeginHandExScope(void *scope, int callerLine);
extern void BTL_SERVER_FLOW_EndHandExScope(u32 *scope, u32 saved, int callerLine);
extern u32 WAZA_TOOL_GetType(u16 move);
extern u32 WAZA_TOOL_GetFlag(u16 move, int flag);

/* 0x1F (the move's fail check): the target's move of the type (`type`:
 * Fire, the type it has in the battle) fails, the powder explodes with its line, and the user loses an HP
 * fraction (`hp`: a quarter). */
static void powder_blast(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 *w = (u32 *)work;
    int pokeId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    HandExDamage *d;
    u32 scope;

    (void)factor;
    (void)ownerId;
    if (pokeId != (int)w[WORK_POWDER_TARGET] ||
        battle_move_type(flow, (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID)) != w[WORK_POWDER_TYPE] ||
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_SILENT) == 0) {
        return;
    }
    scope = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 1);
    show_message(flow, pokeId, BTL_STRTYPE_STD, LINE_POWDER_BLAST, 0);
    if ((d = (HandExDamage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_DAMAGE, pokeId)) != 0) {
        d->pokeId = (u8)pokeId;
        d->amount = BTL_CALC_DivideMaxHpMin1(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId), w[WORK_POWDER_HP]);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, d);
    }
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), scope, 2);
}

static const BtlEventHandler k_powder_field[] = {
    { 0x1F, powder_blast },
    { 0x76, crafty_turn_end },          /* (the powder goes with the turn) */
};

/* 0xA0: each target is covered in powder for the turn; Grass Pokemon are
 * not (a powder move, as Generation VI on).  With none covered the move
 * fails. */
static void powder_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlEventFactor *field;
    int n, i, target;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
        if (BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam(flow, target), POKETYPE_GRASS) ||
            BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_POWDER + target) != 0) {
            continue;
        }
        field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0,
                                    (u8)(OWNER_POWDER + target), k_powder_field,
                                    (u16)(sizeof k_powder_field / sizeof k_powder_field[0]));
        if (field != 0) {
            BTL_EVENT_SetFactorWork(field, WORK_POWDER_TARGET, (u32)target);
            BTL_EVENT_SetFactorWork(field, WORK_POWDER_TYPE, (u32)bw_bh_arg("type", 1, POKETYPE_FIRE));
            BTL_EVENT_SetFactorWork(field, WORK_POWDER_HP, (u32)bw_bh_arg("hp", 1, POWDER_HP));
            show_message(flow, target, BTL_STRTYPE_SET, LINE_POWDER, 1);
        }
    }
}

static const BtlEventHandler k_powder[] = {
    { 0xA0, powder_apply },
};

/* --- Throat Chop: no sound moves for two turns -------------------- */

#define NAMED_THROAT_CHOP     0x10024
#define OWNER_THROAT_CHOP     0x68      /* + the target (patch_ov093_btl_client_moves.c reads it) */
#define LINE_THROAT_CHOP      2036      /* bank 14, set of three: the Pokemon, the move */
#define THROAT_CHOP_TURNS     2
#define WAZA_FLAG_SOUND       8
#define WORK_CHOP_TARGET      0
#define WORK_CHOP_TURNS       1

/* 0x1F: the target's sound move fails, with Heal Block's kind of line
 * (or "But it failed!" in a language with none). */
static void throat_chop_block(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 *w = (u32 *)work;
    int pokeId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    u16 move = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    HandExMessage *msg;
    u32 scope;

    (void)factor;
    (void)ownerId;
    if (pokeId != (int)w[WORK_CHOP_TARGET] || WAZA_TOOL_GetFlag(move, WAZA_FLAG_SOUND) == 0) {
        return;
    }
    if (!line_ok(BTL_STRTYPE_SET, LINE_THROAT_CHOP)) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_MOVE_FAILED);
        return;
    }
    if (BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_SILENT) == 0) {
        return;
    }
    scope = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 3);
    if ((msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId)) != 0) {
        BTL_SERVER_FLOW_InitMsgRecord(&msg->message, BTL_STRTYPE_SET, LINE_THROAT_CHOP);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)pokeId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, move);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
    }
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), scope, 4);
}

/* 0x76, the field's pass: one turn gone; after the last the hold ends. */
static void throat_chop_turn_end(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)context;
    (void)ownerId;
    if (BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) != BTL_TURN_CHECK_FIELD) {
        return;
    }
    if (w[WORK_CHOP_TURNS] > 0) {
        w[WORK_CHOP_TURNS]--;
    }
    if (w[WORK_CHOP_TURNS] == 0) {
        BTL_EVENT_RemoveFactor(factor);
    }
}

static const BtlEventHandler k_throat_chop_field[] = {
    { 0x1F, throat_chop_block },
    { 0x76, throat_chop_turn_end },
};

/* 0x83, after the damage: each target hit that is still up cannot use
 * sound moves for two turns (`turns`), this one and the next. */
static void throat_chop_hit(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlEventFactor *field;
    int n, i, target;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    n = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
    for (i = 0; i < n; i++) {
        target = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
        if (BTL_POKEPARAM_IsFainted(BTL_SERVER_FLOW_GetPokeParam(flow, target)) ||
            BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_THROAT_CHOP + target) != 0) {
            continue;
        }
        field = BTL_EVENT_AddFactor(BTL_EVENT_TYPE_FIELD, (u32)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), 3, 0,
                                    (u8)(OWNER_THROAT_CHOP + target), k_throat_chop_field,
                                    (u16)(sizeof k_throat_chop_field / sizeof k_throat_chop_field[0]));
        if (field != 0) {
            BTL_EVENT_SetFactorWork(field, WORK_CHOP_TARGET, (u32)target);
            BTL_EVENT_SetFactorWork(field, WORK_CHOP_TURNS, (u32)bw_bh_arg("turns", 1, THROAT_CHOP_TURNS));
        }
    }
}

static const BtlEventHandler k_throat_chop[] = {
    { 0x83, throat_chop_hit },
};

/* --- Laser Focus, Stomping Tantrum ------------------------------- */

#define NAMED_LASER_FOCUS     0x10025
#define NAMED_STOMPING_TANTRUM 0x10026
#define LINE_LASER_FOCUS      2039      /* bank 14, set of three */
#define STOMPING_TANTRUM_RATIO 0x2000   /* twice the power */

extern void bw_bm_laser_focus_set(int pokeId, const void *poke, int lastTurn);
extern int bw_bm_last_move_failed(int pokeId, const void *poke);

/* 0xA0 (Focus Energy's event): the user's moves are critical hits for the
 * rest of this turn and the next (BTL_SERVER_FLOW_CheckCriticalHit,
 * patch_ov093_btl_server_flow_moves.c). */
static void laser_focus_apply(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    bw_bm_laser_focus_set(ownerId, BTL_SERVER_FLOW_GetPokeParam(flow, ownerId),
                          (int)BTL_SERVER_FLOW_GetTurnCount(flow) + 1);
    show_message(flow, ownerId, BTL_STRTYPE_SET, LINE_LASER_FOCUS, 1);
}

static const BtlEventHandler k_laser_focus[] = {
    { 0xA0, laser_focus_apply },
};

/* 0x38 (the power step): twice the power (`ratio`) when the user's last
 * move failed (missed, had no effect or did nothing). */
static void stomping_tantrum_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) &&
        bw_bm_last_move_failed(ownerId, BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context, ownerId))) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_POWER_RATIO, bw_bh_arg("ratio", 1, STOMPING_TANTRUM_RATIO));
    }
}

static const BtlEventHandler k_stomping_tantrum[] = {
    { 0x38, stomping_tantrum_power },
};

/* --- Spectral Thief, Pollen Puff --------------------------------- */

#define NAMED_SPECTRAL_THIEF  0x10027
#define NAMED_POLLEN_PUFF     0x10028
#define LINE_SPECTRAL_THIEF   2042      /* bank 14, set of three */
#define BTL_RANK_MAX          12
#define BTL_EVAR_REPLACED_FLAG 0x51     /* the damage-replaced query's answer (Present's heal) */
#define POLLEN_PUFF_HP        2         /* half the ally's HP */
#define BTL_STR_HP_RESTORED_PRESENT 0x183 /* bank 14: Present's "<Pokemon>'s HP was restored." */
#define BTL_STR_HP_FULL       0xD2      /* bank 14: "It doesn't affect <Pokemon>..." (Present at full HP) */

/* 0x30 (the damage-replaced query, Present's: once for each target the
 * move hits, after the misses are taken out, before the damage): the user
 * takes the target's raised stat stages for its own, and the target's go
 * back to neutral.  The damage is not replaced. */
static void spectral_thief_steal(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlPokeParam *user, *target;
    HandExSetRanks *ranks;
    u8 mine[BTL_RANK_KINDS], theirs[BTL_RANK_KINDS];
    int k, targetId, stolen = 0;
    u32 scope;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    targetId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    user = BTL_SERVER_FLOW_GetPokeParam(flow, ownerId);
    target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
    for (k = 0; k < BTL_RANK_KINDS; k++) {
        int t = BTL_POKEPARAM_GetValue(target, 1 + k), u = BTL_POKEPARAM_GetValue(user, 1 + k);

        if (t > BTL_RANK_NEUTRAL) {
            u += t - BTL_RANK_NEUTRAL;
            t = BTL_RANK_NEUTRAL;
            stolen = 1;
        }
        mine[k] = (u8)(u > BTL_RANK_MAX ? BTL_RANK_MAX : u);
        theirs[k] = (u8)t;
    }
    if (!stolen) {
        return;
    }
    scope = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 5);
    if ((ranks = (HandExSetRanks *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_SET_RANKS, ownerId)) != 0) {
        ranks->pokeId = (u8)targetId;
        for (k = 0; k < BTL_RANK_KINDS; k++) {
            ranks->ranks[k] = theirs[k];
        }
        BTL_SERVER_FLOW_CommitCmdRecord(flow, ranks);
    }
    if ((ranks = (HandExSetRanks *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_SET_RANKS, ownerId)) != 0) {
        ranks->pokeId = ownerId;
        for (k = 0; k < BTL_RANK_KINDS; k++) {
            ranks->ranks[k] = mine[k];
        }
        BTL_SERVER_FLOW_CommitCmdRecord(flow, ranks);
    }
    show_message(flow, ownerId, BTL_STRTYPE_SET, LINE_SPECTRAL_THIEF, 1);
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), scope, 6);
}

static const BtlEventHandler k_spectral_thief[] = {
    { 0x30, spectral_thief_steal },
};

/* 0x30 (Present's): aimed at an ally, the move heals in place of the
 * damage. */
static void pollen_puff_variant(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);

    (void)factor;
    (void)context;
    *(u32 *)work = 0;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && defenderId != ownerId &&
        BTL_MAIN_IsPokeSamePair(ownerId, defenderId) != 0) {
        *(u32 *)work = (u32)BTL_EVENT_OverwriteVar(BTL_EVAR_REPLACED_FLAG, 1);
    }
}

/* 0x31 (Present's heal): the ally heals an HP fraction (`hp`: half), or
 * the game's line for a Pokemon at full HP. */
static void pollen_puff_heal(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    int targetId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    struct BtlPokeParam *target;
    HandExRecoverHp *heal;

    (void)factor;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || *(u32 *)work == 0) {
        return;
    }
    *(u32 *)work = 0;
    target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
    if (BTL_POKEPARAM_IsHpFull(target) == 0) {
        if ((heal = (HandExRecoverHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RECOVER_HP, ownerId)) != 0) {
            heal->pokeId = (u8)targetId;
            heal->amount = BTL_CALC_DivideMaxHpMin1(target, (u32)bw_bh_arg("hp", 1, POLLEN_PUFF_HP));
            BTL_SERVER_FLOW_InitMsgRecord(&heal->message, BTL_STRTYPE_SET, BTL_STR_HP_RESTORED_PRESENT);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&heal->message, (u32)targetId);
            BTL_SERVER_FLOW_CommitCmdRecord(flow, heal);
        }
    } else {
        HandExMessage *msg = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, ownerId);

        if (msg != 0) {
            BTL_SERVER_FLOW_InitMsgRecord(&msg->message, BTL_STRTYPE_SET, BTL_STR_HP_FULL);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&msg->message, (u32)targetId);
            BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
        }
    }
}

static const BtlEventHandler k_pollen_puff[] = {
    { 0x30, pollen_puff_variant },
    { 0x31, pollen_puff_heal },
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
            /* the animation's variant is the type: a mod's script 449 may have a
             * look a type (the game's has one, which every type plays) */
            BTL_SERVER_FLOW_SetCurTurnStateFlag(flow, type);
        }
    }
}

/* Techno Blast: the type and the animation variant follow the drive that
 * the user holds.  An added type's drive plays the variant of its type, as
 * Judgment does (types from 17 never meet the game's drive variants 1 to
 * 4); the game's 546 has variants 0 to 4, so it plays the plain one unless
 * a mod's 546 has a look for the type. */
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
                    variant = (int)added;
                }
                break;
            }
            BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, (__UINTPTR_TYPE__)type);
            BTL_SERVER_FLOW_SetCurTurnStateFlag(flow, variant);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The game's handlers that test a move id                             */
/* ------------------------------------------------------------------ */

/* hand_waza_types.h, hand_common_types.h: the records these handlers commit */
typedef struct {
    u32 header;
    u8 pokeId;
    u8 counterId;
    u8 value;
} HandExSetCounter;

struct HandExMessageAnim {
    u32 header;
    BtlStrParam message;
    u8 pad28[0x24];
    u8 pokeId;
    u8 animMode;
};

struct HandExSwitchOut {
    u32 header;
    u16 messageId;
    u8 position;
    u8 replacementSlot;
};

struct HandWazaCmdForceSwitch {
    u32 header;
    u16 strId;
    u8 pokeId;
};

typedef struct {
    u32 header;
    u8 pokeId;
    u8 slot;
    u8 fullPp;
    u8 permanent;
    u16 moveId;
} BtlReplaceMoveCmd;

/* one row of the Pledge combination table */
struct PledgeCombo {
    u16 firstMove;
    u16 secondMove;
    int combo;
};

#define HANDEX_KIND_SET_COUNTER        0x26
#define HAND_WAZA_KIND_MESSAGE_ANIM    0x29
#define HAND_WAZA_KIND_SWITCH_OUT      0x37
#define HAND_WAZA_KIND_FORCE_SWITCH    0x2e
#define BTL_CMD_KIND_REPLACE_MOVE      0x25
#define HANDEX_FLAG_NO_WAIT            (1u << 24)
#define BTL_SLOT_NONE                  6
#define BTL_COUNTER_PROTECT_STREAK     3
#define BTL_SICK_TRAP                  8
#define BTL_SICK_SKY_DROP              0x21
#define BTL_EVAR_TARGET_ID             0x06
#define BTL_EVAR_FAIL_REASON           0x22
#define BTL_FAIL_REASON_MOVE_FAILED    0x1a
#define BTL_RATIO_DOUBLE               0x2000
#define BTL_RULE_SINGLE                0
#define BTL_RULE_ROTATION              3
#define BTL_POKEPARAM_MOVE_COUNT       4
#define PLEDGE_COMBO_NONE              0

/* the move ids the handlers test */
#define BTL_MOVE_BIND           0x14
#define BTL_MOVE_WRAP           0x23
#define BTL_MOVE_FIRE_SPIN      0x53
#define BTL_MOVE_CLAMP          0x80
#define BTL_MOVE_SKETCH         166
#define BTL_MOVE_STRUGGLE       165
#define BTL_MOVE_WHIRLPOOL      0xfa
#define BTL_MOVE_SAND_TOMB      0x148
#define BTL_MOVE_U_TURN         0x171
#define BTL_MOVE_LAST_RESORT    0x183
#define BTL_MOVE_CHATTER        448
#define BTL_MOVE_MAGMA_STORM    0x1cf
#define BTL_MOVE_CIRCLE_THROW   0x1fd
#define BTL_MOVE_SKY_DROP       507
#define BTL_MOVE_FREEZE_SHOCK   553
#define BTL_MOVE_FUSION_FLARE   558
#define BTL_MOVE_FUSION_BOLT    559

#define HAND_WAZA_STR_FREEZE_SHOCK_CHARGE 0x35f
#define HAND_WAZA_STR_ICE_BURN_CHARGE     0x362

extern const u16 gProtectMoveIds[];
extern struct PledgeCombo gPledgeComboTable[3];

extern u16 BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern u16 BTL_POKEPARAM_GetLastUsedMoveId(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetLastSelectedMoveId(struct BtlPokeParam *pp);
extern int BTL_SERVER_FLOW_CheckClientHasBackupPoke(struct BtlServerFlow *flow, int pokeId);
extern int BTL_SERVER_FLOW_TrySetOnceFlag(void *flag);
extern int BTL_MAIN_PokeIdToClientIdThunk(int pokeId);
extern u8 BTL_SERVER_FLOW_GetPokeViewPos(struct BtlServerFlow *flow, int pokeId);
extern void *BTL_SERVER_FLOW_GetTurnRecord(struct BtlServerFlow *flow);
extern u16 BTL_WAZAREC_GetEffectiveWaza(void *record, u32 turn);
extern u32 BTL_SERVER_FLOW_GetTurnCount(struct BtlServerFlow *flow);
extern u8 BTL_POKEPARAM_GetMoveCount(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetMoveId(struct BtlPokeParam *pp, u8 slot);
extern int BTL_POKEPARAM_GetMoveUsedFlag(struct BtlPokeParam *pp, u8 slot);
extern int BTL_SERVER_FLOW_GetRule(struct BtlServerFlow *flow);
extern int BTL_MAIN_IsPokeSamePair(int pokeA, int pokeB);
extern int BTL_SERVER_FLOW_TestStateFlagBit0(unsigned char *flow);
extern int BTL_SERVER_FLOW_CheckAllyMoveAutoHitById(struct BtlServerFlow *flow, int attackerId, int pokeId, u16 moveId);
extern int BTL_POKEPARAM_HasMove(struct BtlPokeParam *pp, int moveId);
extern int BTL_POKEPARAM_IsTransformed(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_FindMoveSlot(struct BtlPokeParam *pp, int moveId);

/* sdk/battle_handlers.c */
extern int bw_bh_move_is(int move, int builtin);
extern int bw_bh_move_in(int move, const u16 *list, int count);

/* Protect, Detect and Endure, event 3: when the user last used a move
 * outside the protect family (or a move that works like none of it), the
 * streak restarts. */
void HAND_WAZA_ProtectResetStreakAfterOtherMove(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    int lastMoveId;
    HandExSetCounter *record;

    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        bw_bm_guard_clear(pokeId);      /* (a guard's note is put again by its own row) */
        lastMoveId = BTL_POKEPARAM_GetLastUsedMoveId(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId));
        if (bw_bh_move_in(lastMoveId, gProtectMoveIds, 5))
            return;
        record = (HandExSetCounter *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_SET_COUNTER, pokeId);
        record->pokeId = (u8)pokeId;
        record->counterId = BTL_COUNTER_PROTECT_STREAK;
        record->value = 0;
        BTL_SERVER_FLOW_CommitCmdRecord(flow, record);
    }
}

/* The built-in move of `list` that `move` works like, 0 for none. */
static int works_like(int move, const u16 *list, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (bw_bh_move_is(move, list[i]))
            return list[i];
    }
    return 0;
}

static const u16 k_trap_moves[] = {
    BTL_MOVE_WRAP, BTL_MOVE_BIND, BTL_MOVE_FIRE_SPIN, BTL_MOVE_CLAMP, BTL_MOVE_SAND_TOMB, BTL_MOVE_MAGMA_STORM,
    BTL_MOVE_WHIRLPOOL
};

/* Trap moves, event ADD_SICK_MESSAGE: shows the message of the move that trapped the target. */
void HAND_WAZA_TrapOnAddSickMessage(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, void *work)
{
    BtlStrParam *message;
    u8 defenderId;
    int messageId;

    (void)flow;
    (void)work;
    if (pokeId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID))
        return;
    if (BTL_EVENT_GetVar(BTL_EVAR_SICK_ID) != BTL_SICK_TRAP)
        return;
    message = (BtlStrParam *)BTL_EVENT_GetVar(BTL_EVAR_MESSAGE);
    defenderId = (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
    switch (works_like(BTL_EVENT_GetFactorKey(factor), k_trap_moves,
                       (int)(sizeof k_trap_moves / sizeof k_trap_moves[0]))) {
    case BTL_MOVE_WRAP:
        messageId = 0x32a;
        break;
    case BTL_MOVE_BIND:
        messageId = 0x323;
        break;
    case BTL_MOVE_FIRE_SPIN:
        messageId = 0x33b;
        break;
    case BTL_MOVE_CLAMP:
        messageId = 0x331;
        break;
    case BTL_MOVE_SAND_TOMB:
        messageId = 0x341;
        break;
    case BTL_MOVE_MAGMA_STORM:
        messageId = 0x33e;
        break;
    case BTL_MOVE_WHIRLPOOL:
        messageId = 0x338;
        break;
    default:
        return;
    }
    BTL_SERVER_FLOW_InitMsgRecord(message, BTL_STRTYPE_SET, messageId);
    BTL_SERVER_FLOW_AppendMsgRecordArg(message, defenderId);
    BTL_SERVER_FLOW_AppendMsgRecordArg(message, (u32)pokeId);
}

/* U-turn and Volt Switch: announce the switch, then send the user back to its ball. */
void HAND_WAZA_UTurnSwitchOut(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    struct HandExMessageAnim *messageAnim;
    struct HandExSwitchOut *switchOut;
    int trainerId;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && BTL_SERVER_FLOW_CheckClientHasBackupPoke(flow, pokeId) != 0 &&
        BTL_SERVER_FLOW_TrySetOnceFlag((void *)flow) != 0) {
        trainerId = BTL_MAIN_PokeIdToClientIdThunk(pokeId);
        messageAnim = (struct HandExMessageAnim *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_MESSAGE_ANIM, pokeId);
        messageAnim->pokeId = (u8)pokeId;
        BTL_SERVER_FLOW_InitMsgRecord(&messageAnim->message, BTL_STRTYPE_SET, 0x302);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&messageAnim->message, (u32)pokeId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&messageAnim->message, (u32)trainerId);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, messageAnim);
        switchOut = (struct HandExSwitchOut *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_SWITCH_OUT, pokeId);
        switchOut->messageId = bw_bh_move_is(BTL_EVENT_GetFactorKey(factor), BTL_MOVE_U_TURN) ? 0x283 : 0x284;
        switchOut->position = BTL_SERVER_FLOW_GetPokeViewPos(flow, pokeId);
        switchOut->replacementSlot = BTL_SLOT_NONE;
        switchOut->header |= HANDEX_FLAG_NO_WAIT;
        BTL_SERVER_FLOW_CommitCmdRecord(flow, switchOut);
    }
}

/* Circle Throw and Dragon Tail: force the target out of the battle. */
void HAND_WAZA_CircleThrowForceSwitch(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    struct HandWazaCmdForceSwitch *cmd;

    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        cmd = (struct HandWazaCmdForceSwitch *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_FORCE_SWITCH, pokeId);
        cmd->pokeId = (u8)BTL_EVENT_GetVar(BTL_EVAR_TARGET_ID);
        /* (the game's lines: Circle Throw's 0x28b, Dragon Tail's 0x28c) */
        cmd->strId = (u16)(BTL_MOVE_CIRCLE_THROW +
                           (bw_bh_move_is(BTL_EVENT_GetFactorKey(factor), BTL_MOVE_CIRCLE_THROW) ? 0x8e : 0x8f));
        BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
    }
}

/* Freeze Shock and Ice Burn, event 0x95 (charge turn): shows the message of the move being charged. */
void HAND_WAZA_FreezeShockIceBurnShowChargeMessage(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    HandExMessage *record;
    int strId;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        if (bw_bh_move_is(BTL_EVENT_GetFactorKey(factor), BTL_MOVE_FREEZE_SHOCK))
            strId = HAND_WAZA_STR_FREEZE_SHOCK_CHARGE;
        else
            strId = HAND_WAZA_STR_ICE_BURN_CHARGE;
        record = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);
        BTL_SERVER_FLOW_InitMsgRecord(&record->message, BTL_STRTYPE_SET, (u16)strId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&record->message, (u32)pokeId);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, record);
    }
}

/* Fusion Flare and Fusion Bolt, event 0x38 (power ratio): doubles the power when the other Fusion move (or a
 * move that works like it) was the last one used this turn. */
void HAND_WAZA_FusionDoublePower(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    void *turnRecord;
    int otherMove;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        turnRecord = BTL_SERVER_FLOW_GetTurnRecord(flow);
        if (bw_bh_move_is(BTL_EVENT_GetFactorKey(factor), BTL_MOVE_FUSION_FLARE))
            otherMove = BTL_MOVE_FUSION_BOLT;
        else if (bw_bh_move_is(BTL_EVENT_GetFactorKey(factor), BTL_MOVE_FUSION_BOLT))
            otherMove = BTL_MOVE_FUSION_FLARE;
        else
            otherMove = 0;
        if (otherMove != 0 &&
            bw_bh_move_is(BTL_WAZAREC_GetEffectiveWaza(turnRecord, BTL_SERVER_FLOW_GetTurnCount(flow)), otherMove)) {
            BTL_EVENT_MultiplyVar(BTL_EVAR_POWER_RATIO, BTL_RATIO_DOUBLE);
            BTL_SERVER_FLOW_SetCurTurnStateFlag(flow, 1);
        }
    }
}

/* The combination that two Pledge moves (or moves that work like them) make, in either order;
 * PLEDGE_COMBO_NONE when they do not combine. */
int HAND_WAZA_FindPledgeCombo(u32 firstMove, u32 secondMove)
{
    u32 i;

    for (i = 0; i < 3; i++) {
        if ((bw_bh_move_is((int)firstMove, gPledgeComboTable[i].firstMove) &&
             bw_bh_move_is((int)secondMove, gPledgeComboTable[i].secondMove)) ||
            (bw_bh_move_is((int)secondMove, gPledgeComboTable[i].firstMove) &&
             bw_bh_move_is((int)firstMove, gPledgeComboTable[i].secondMove)))
            return gPledgeComboTable[i].combo;
    }
    return PLEDGE_COMBO_NONE;
}

/* Last Resort, event MOVE_FAIL_CHECK: fails unless the user knows Last Resort (this move), knows at least two
 * moves and has used every other move. */
void HAND_WAZA_LastResortOnMoveFailCheck(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, void *work)
{
    struct BtlPokeParam *poke;
    u8 moveCount;
    u8 slot;
    u8 usedCount;
    int hasLastResort;
    int moveId;

    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        poke = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        moveCount = BTL_POKEPARAM_GetMoveCount(poke);
        usedCount = 0;
        hasLastResort = 0;
        for (slot = 0; slot < moveCount; slot++) {
            moveId = BTL_POKEPARAM_GetMoveId(poke, slot);
            if (moveId == BTL_EVENT_GetFactorKey(factor) || bw_bh_move_is(moveId, BTL_MOVE_LAST_RESORT)) {
                hasLastResort = 1;
            } else if (BTL_POKEPARAM_GetMoveUsedFlag(poke, slot)) {
                usedCount++;
            }
        }
        if (hasLastResort == 0 || moveCount < 2 || usedCount < moveCount - 1)
            BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_MOVE_FAILED);
    }
}

/* Follow Me: in a double or triple battle, make the user the defender of a move from an enemy, unless the move is
 * Sky Drop (or works like it) or the user is blocked. */
void HAND_WAZA_FollowMeRedirectTarget(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    struct BtlPokeParam *user;
    u8 attackerId;
    u16 moveId;

    (void)factor;
    if (BTL_SERVER_FLOW_GetRule(flow) == BTL_RULE_SINGLE)
        return;
    if (BTL_SERVER_FLOW_GetRule(flow) == BTL_RULE_ROTATION)
        return;
    attackerId = (u8)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID);
    user = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
    if (BTL_MAIN_IsPokeSamePair(pokeId, attackerId) != 0)
        return;
    if (bw_bh_move_is((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), BTL_MOVE_SKY_DROP))
        return;
    if (BTL_POKEPARAM_HasSick(user, BTL_SICK_SKY_DROP) != 0)
        return;
    if (BTL_SERVER_FLOW_TestStateFlagBit0((unsigned char *)flow) != 0)
        return;
    moveId = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    if (BTL_SERVER_FLOW_CheckAllyMoveAutoHitById(flow, attackerId, pokeId, moveId) != 0)
        return;
    BTL_EVENT_OverwriteVar(BTL_EVAR_DEFENDER_ID, (__UINTPTR_TYPE__)pokeId);
}

/* Sketch: learn the last move of the target for good.  It is refused for no move, Struggle, Sketch, Chatter (or a
 * move that works like Sketch or Chatter), a move the user knows, and when the user is transformed.  Struggle stays
 * a test of its own id: the game refuses it as the move a Pokemon falls back on, not for its effect. */
void HAND_WAZA_SketchLearnTargetMove(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    struct BtlPokeParam *user;
    struct BtlPokeParam *target;
    int move;
    int selectedMove;
    int slot;
    BtlReplaceMoveCmd *replaceCmd;
    HandExMessage *messageCmd;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        user = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        target = BTL_SERVER_FLOW_GetPokeParam(flow, (u8)BTL_EVENT_GetVar(BTL_EVAR_TARGET_ID));
        move = BTL_POKEPARAM_GetLastUsedMoveId(target);
        selectedMove = BTL_POKEPARAM_GetLastSelectedMoveId(target);
        if (selectedMove != move)
            move = selectedMove;
        if (move == 0)
            return;
        if (bw_bh_move_is(move, BTL_MOVE_SKETCH))
            return;
        if (move == BTL_MOVE_STRUGGLE)
            return;
        if (bw_bh_move_is(move, BTL_MOVE_CHATTER) || BTL_POKEPARAM_HasMove(user, move) != 0 ||
            BTL_POKEPARAM_IsTransformed(user) != 0)
            return;
        slot = BTL_POKEPARAM_FindMoveSlot(user, BTL_EVENT_GetFactorKey(factor));
        if (slot == BTL_POKEPARAM_MOVE_COUNT)
            return;
        replaceCmd = (BtlReplaceMoveCmd *)BTL_SERVER_FLOW_AllocCmdRecord(flow, BTL_CMD_KIND_REPLACE_MOVE, pokeId);
        replaceCmd->pokeId = (u8)pokeId;
        replaceCmd->slot = (u8)slot;
        replaceCmd->moveId = (u16)move;
        replaceCmd->fullPp = 0;
        replaceCmd->permanent = 1;
        BTL_SERVER_FLOW_CommitCmdRecord(flow, replaceCmd);
        messageCmd = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);
        /* (the game's line: Chatter's id + 240, bank 14 line 688) */
        BTL_SERVER_FLOW_InitMsgRecord(&messageCmd->message, BTL_STRTYPE_SET, BTL_MOVE_CHATTER + 240);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&messageCmd->message, (u32)pokeId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(&messageCmd->message, (u32)move);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, messageCmd);
    }
}

extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern int BTL_TYPEPAIR_ShareType(u32 pairA, u32 pairB);
extern u8 BTL_TYPEPAIR_GetHigh(u16 pair);
extern u8 BTL_TYPEPAIR_GetLow(u16 pair);
extern int bw_bm_third_type(int pokeId, const void *poke);

/* Synchronoise: the move has no effect on a target that shares no type with the user, a third type a move gave
 * either of them included (add_type, sdk/battle_moves.h). */
void HAND_WAZA_SynchronoiseFailWithoutSharedType(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    u8 targetId;
    struct BtlPokeParam *target, *user;
    u32 userTypes;
    int third;

    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        targetId = (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
        target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
        user = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        userTypes = BTL_POKEPARAM_GetTypePair(user);
        if (BTL_TYPEPAIR_ShareType(userTypes, BTL_POKEPARAM_GetTypePair(target)) != 0)
            return;
        third = bw_bm_third_type(pokeId, user);         /* the user's third type, on the target */
        if (third >= 0 && BTL_POKEPARAM_HasType(target, third))
            return;
        third = bw_bm_third_type(targetId, target);     /* the target's, on the user */
        if (third >= 0 && BTL_POKEPARAM_HasType(user, third))
            return;
        BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1);
    }
}

#define BTL_STR_REFLECT_TYPE   0x441    /* bank 14: "<Pokemon>'s type changed to match <target>'s!" */
#define POKETYPE_GROUND        4
#define POKETYPE_ROCK          5
#define POKETYPE_ICE           14

extern int BTL_SERVER_FLOW_GetFieldSituationType(struct BtlServerFlow *flow);

/* Reflect Type: the user takes the target's types, its third type too
 * (sdk/battle_moves.h: the flow's type change takes the user's own third
 * type away, then the target's is given). */
void HAND_WAZA_ReflectTypeCopyTargetType(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    u8 targetId;
    struct BtlPokeParam *target;
    HandExChangeType *typeCmd;
    HandExMessage *messageCmd;
    int third;

    (void)factor;
    (void)work;
    if (pokeId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID))
        return;
    targetId = (u8)BTL_EVENT_GetVar(BTL_EVAR_TARGET_ID);
    target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
    typeCmd = (HandExChangeType *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_CHANGE_TYPE, pokeId);
    typeCmd->pokeId = (u8)pokeId;
    typeCmd->typePair = BTL_POKEPARAM_GetTypePair(target);
    typeCmd->noLine = 1;
    BTL_SERVER_FLOW_CommitCmdRecord(flow, typeCmd);
    third = bw_bm_third_type(targetId, target);
    if (third >= 0)
        bw_bm_third_type_set(pokeId, BTL_SERVER_FLOW_GetPokeParam(flow, pokeId), third);
    messageCmd = (HandExMessage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_MESSAGE, pokeId);
    messageCmd->header |= 4 << 22;
    BTL_SERVER_FLOW_InitMsgRecord(&messageCmd->message, BTL_STRTYPE_SET, BTL_STR_REFLECT_TYPE);
    BTL_SERVER_FLOW_AppendMsgRecordArg(&messageCmd->message, (u32)pokeId);
    BTL_SERVER_FLOW_AppendMsgRecordArg(&messageCmd->message, targetId);
    BTL_SERVER_FLOW_CommitCmdRecord(flow, messageCmd);
}

/* Camouflage: the user takes the type that matches the battle ground.  A
 * Pokemon of that type alone with a third type changes too (it loses the
 * third). */
void HAND_WAZA_CamouflageChangeType(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    int type;
    u16 typePair;
    HandExChangeType *cmd;
    struct BtlPokeParam *pp;

    (void)factor;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        switch (BTL_SERVER_FLOW_GetFieldSituationType(flow)) {
        case 1: case 2: case 3: case 8: case 9: case 15:
            type = POKETYPE_GROUND;
            break;
        case 4: case 14:
        default:
            type = POKETYPE_NORMAL;
            break;
        case 10:
            type = POKETYPE_ROCK;
            break;
        case 0: case 5:
            type = POKETYPE_GRASS;
            break;
        case 6: case 11: case 12:
            type = POKETYPE_WATER;
            break;
        case 7: case 13:
            type = POKETYPE_ICE;
            break;
        }
        typePair = (u16)BTL_TYPEPAIR_MakeSingle(type);
        pp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        if (typePair != BTL_POKEPARAM_GetTypePair(pp) || bw_bm_third_type(pokeId, pp) >= 0) {
            cmd = (HandExChangeType *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_CHANGE_TYPE, pokeId);
            cmd->typePair = typePair;
            cmd->pokeId = (u8)pokeId;
            BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
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
    case NAMED_MISTY_TERRAIN:
    case NAMED_GRASSY_TERRAIN:
    case NAMED_ELECTRIC_TERRAIN:
    case NAMED_PSYCHIC_TERRAIN: *count = ROWS(k_misty_terrain); return k_misty_terrain;
    case NAMED_FLOWER_SHIELD: *count = ROWS(k_flower_shield); return k_flower_shield;
    case NAMED_CRAFTY_SHIELD: *count = ROWS(k_crafty_shield); return k_crafty_shield;
    case NAMED_FAIRY_LOCK:    *count = ROWS(k_fairy_lock);    return k_fairy_lock;
    case NAMED_GEOMANCY:      *count = ROWS(k_geomancy);      return k_geomancy;
    case NAMED_FELL_STINGER:  *count = ROWS(k_fell_stinger);  return k_fell_stinger;
    case NAMED_NEEDS_STATUS:  *count = ROWS(k_needs_status);  return k_needs_status;
    case NAMED_SPIKY_SHIELD:
    case NAMED_KINGS_SHIELD:
    case NAMED_BANEFUL_BUNKER:
    case NAMED_CONTACT_GUARD: *count = ROWS(k_guard);         return guard_handlers();
    case NAMED_ADD_TYPE:
    case NAMED_TRICK_OR_TREAT:
    case NAMED_FORESTS_CURSE: *count = ROWS(k_add_type);      return k_add_type;
    case NAMED_TOPSY_TURVY:   *count = ROWS(k_topsy_turvy);   return k_topsy_turvy;
    case NAMED_STRENGTH_SAP:  *count = ROWS(k_strength_sap);  return k_strength_sap;
    case NAMED_SHORE_UP:      *count = ROWS(k_shore_up);      return k_shore_up;
    case NAMED_BELCH:
    case NAMED_NEEDS_BERRY:   *count = ROWS(k_needs_berry);   return k_needs_berry;
    case NAMED_BURN_UP:       *count = ROWS(k_burn_up);       return k_burn_up;
    case NAMED_ROTOTILLER:    *count = ROWS(k_rototiller);    return k_rototiller;
    case NAMED_MAGNETIC_FLUX: *count = ROWS(k_magnetic_flux); return k_magnetic_flux;
    case NAMED_SET_TYPE:      *count = ROWS(k_set_type);      return k_set_type;
    case NAMED_PARTING_SHOT:  *count = ROWS(k_parting_shot);  return k_parting_shot;
    case NAMED_ELECTRIFY:     *count = ROWS(k_electrify);     return k_electrify;
    case NAMED_ION_DELUGE:    *count = ROWS(k_ion_deluge);    return k_ion_deluge;
    case NAMED_STICKY_WEB:    *count = ROWS(k_sticky_web);    return k_sticky_web;
    case NAMED_POWDER:        *count = ROWS(k_powder);        return k_powder;
    case NAMED_THROAT_CHOP:   *count = ROWS(k_throat_chop);   return k_throat_chop;
    case NAMED_LASER_FOCUS:   *count = ROWS(k_laser_focus);   return k_laser_focus;
    case NAMED_STOMPING_TANTRUM: *count = ROWS(k_stomping_tantrum); return k_stomping_tantrum;
    case NAMED_SPECTRAL_THIEF: *count = ROWS(k_spectral_thief); return k_spectral_thief;
    case NAMED_POLLEN_PUFF:   *count = ROWS(k_pollen_puff);   return k_pollen_puff;
    case NAMED_HITS_FLOATING:
    case NAMED_SUPER_EFFECTIVE:
    case NAMED_FREEZE_DRY:
    case NAMED_SECOND_TYPE:
    case NAMED_FLYING_PRESS:
        return 0;               /* (no handlers: BTL_SERVER_FLOW_CalcTypeEffectiveness reads the line) */
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
