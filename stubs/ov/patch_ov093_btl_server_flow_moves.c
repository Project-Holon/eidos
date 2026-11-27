/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_FindPendingActionByMove BTL_SERVER_FLOW_ProcPledgeCombo BTL_SERVER_FLOW_ProcPursuitBeforeSwitch BTL_SERVER_FLOW_ProcMoveSpecialEffect BTL_SERVER_FLOW_ProcNoEffectChecks BTL_SERVER_FLOW_CalcTypeEffectiveness BTL_SERVER_FLOW_ExecCmdSetPokeTypes BTL_SERVER_FLOW_NotifyMoveSucceeded BTL_SERVER_FLOW_NotifyMoveFailed BTL_SERVER_FLOW_CheckCriticalHit BTL_SERVER_FLOW_CheckMoveExecuteFailByEvent BTL_SERVER_FLOW_RollMoveAddedEffectChance
 *
 * The server flow's tests of a move by its id take a mod's move that works
 * like the move (sdk/battle_handlers.h bw_bh_move_is, eidos/tables/
 * move_handlers.txt) as that move:
 *
 *  - FindPendingActionByMove (the Pledge partner search, Round's
 *    promotion): an action whose move works like the one asked for is
 *    found too;
 *  - ProcPledgeCombo: a move that works like a Pledge waits for a partner
 *    with another Pledge;
 *  - ProcPursuitBeforeSwitch: a move that works like Pursuit hits a
 *    Pokemon that switches out;
 *  - ProcMoveSpecialEffect: a move that works like Skill Swap or
 *    Substitute does what they do (the flow's own code, not a handler).
 *
 * And eidos's own move bases that the flow's code serves:
 *
 *  - ProcNoEffectChecks: the guards (spiky_shield, kings_shield,
 *    baneful_bunker, contact_guard) stop moves as Protect does, and a
 *    contact move they stop costs the attacker HP, a stat or a status;
 *  - CalcTypeEffectiveness: a third type (add_type), a move super effective
 *    against a type (freeze_dry, super_effective), a move whose
 *    effectiveness takes a second type (flying_press, second_type) and a
 *    Ground move that hits floating Pokemon (hits_floating);
 *  - ExecCmdSetPokeTypes: a type change takes the third type away.
 *  - NotifyMoveSucceeded, NotifyMoveFailed, CheckMoveExecuteFailByEvent:
 *    the move's result is kept for Stomping Tantrum (sdk/battle_moves.h),
 *    a move that fails its own check ("But it failed!") included;
 *  - CheckCriticalHit: Laser Focus makes the user's moves critical hits.
 *  - RollMoveAddedEffectChance: a move line's `chance` key is the chance
 *    of the move record's stat change (Ancient Power and its kind).
 *
 * With no table lines each is the game's own function.
 *
 * symbols:
 *   gPledgeMoveIds                           0x021EFDB8  data   a2
 *   BTL_ACTION_GetKind                       0x021D8454  thumb  ov093
 *   BTL_ACTION_GetFightMove                  0x021D8340  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId                  0x021D5440  thumb  ov093
 *   BTL_POKEPARAM_IsFainted                  0x021D5B68  thumb  ov093
 *   BTL_SERVER_FLOW_GetActionIndex           0x021BCB68  thumb  ov093
 *   BTL_POKEPARAM_TestTurnFlag               0x021D5BB0  thumb  ov093
 *   BTL_POKEPARAM_HasStoredPokeMove          0x021D6E74  thumb  ov093
 *   BTL_POKEPARAM_SetTurnFlag                0x021D5FC0  thumb  ov093
 *   BTL_POKEPARAM_SetStoredPokeMove          0x021D6E40  thumb  ov093
 *   BTL_SERVER_CMD_PushVariable              0x021CD64C  thumb  ov093
 *   BTL_SERVER_FLOW_PromoteActionOfPoke      0x021BD148  thumb  ov093
 *   HAND_WAZA_AddMoveHandler                 0x021E02DC  thumb  ov093
 *   BTL_POKEPARAM_GetValue                   0x021D59B4  thumb  ov093
 *   BTL_EVENT_PushVarFrame                   0x021D74F8  thumb  ov093
 *   BTL_EVENT_SetVarReadOnly                 0x021D75EC  thumb  ov093
 *   BTL_EVENT_DispatchEventChecked           0x021D70F0  thumb  ov093
 *   BTL_EVENT_PopVarFrame                    0x021D7540  thumb  ov093
 *   HAND_WAZA_RemoveMoveHandlerOfPoke        0x021E03D8  thumb  ov093
 *   BTL_POKESET_IsRemovedAll                 0x0689CD50  thumb  ov095
 *   BTL_SERVER_FLOW_ProcSkillSwap            0x021C3E0C  thumb  ov093
 *   BTL_SERVER_FLOW_ProcSubstituteMove       0x021C4034  thumb  ov093
 *   BTL_SERVER_FLOW_BeginHandExScope         0x021CC8D8  thumb  ov093
 *   BTL_SERVER_FLOW_GetLockState             0x021C88E4  thumb  ov093
 *   BTL_SERVER_FLOW_DispatchUncategorizedMoveEvent 0x021C4304  thumb  ov093
 *   BTL_SERVER_FLOW_EndHandExScope           0x021CC95C  thumb  ov093
 *   BTL_SERVER_FLOW_FindPendingMoveActionForPoke 0x021BCB14  thumb  ov093
 *   BTL_MAIN_IsPokeSamePair                  0x021B9A24  thumb  ov093
 *   BTL_POKESET_SeekStart                    0x0689CC84  thumb  ov095
 *   BTL_POKESET_SeekNext                     0x0689CC8C  thumb  ov095
 *   BTL_POKESET_Remove                       0x0689CC14  thumb  ov095
 *   WAZA_TOOL_GetFlag                        0x0201BEB8  thumb
 *   WAZA_TOOL_IsDamaging                     0x0201C314  thumb
 *   BTL_SERVER_FLOW_GetTurnCount             0x021C8138  thumb  ov093
 *   BTL_SERVER_FLOW_AllocCmdRecord           0x021C88A4  thumb  ov093
 *   BTL_SERVER_FLOW_CommitCmdRecord          0x021C88C8  thumb  ov093
 *   BTL_SERVER_FLOW_InitMsgRecord            0x021C8838  thumb  ov093
 *   BTL_SERVER_FLOW_AppendMsgRecordArg       0x021C8860  thumb  ov093
 *   BTL_CALC_DivideMaxHpMin1                 0x021D7B74  thumb  ov093
 *   BTL_CALC_MakeDefaultSickCont             0x021D7D18  thumb  ov093
 *   BTL_POKEPARAM_GetTypePair                0x021D57FC  thumb  ov093
 *   BTL_POKEPARAM_HasSick                    0x021D62C4  thumb  ov093
 *   BTL_TYPEPAIR_GetHigh                     0x021E8CFC  thumb  ov093
 *   BTL_TYPEPAIR_GetLow                      0x021E8D04  thumb  ov093
 *   BTL_TYPEPAIR_IsSingleType                0x021E8DA8  thumb  ov093
 *   BTL_ID_IsArceusSpecies                   0x0689C9C0  thumb  ov095
 *   BTL_POSPOKE_IsPokeInside                 0x0689D588  thumb  ov095
 *   BTL_SERVER_FLOW_DispatchMoveResultEvent  0x021BF2EC  thumb  ov093
 *   BTL_SERVER_FLOW_GetPokeParam             0x021C7CF4  thumb  ov093
 *   BTL_EVENT_SetVar                         0x021D75A8  thumb  ov093
 *   BTL_EVENT_SetVarOnce                     0x021D7630  thumb  ov093
 *   BTL_EVENT_GetVar                         0x021D775C  thumb  ov093
 *   BTL_POKEPARAM_GetCritStage               0x021D5F14  thumb  ov093
 *   BTL_CALC_RollCriticalHit                 0x021D7930  thumb  ov093
 *   BTL_MAIN_ReturnZero                      0x021B911C  thumb  ov093
 *   WAZA_TOOL_GetParam                       0x0201BD28  thumb
 *   BTL_CALC_Random                          0x021D78AC  thumb  ov093
 *   WAZA_TOOL_IsAlwaysCritical               0x0201C284  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlServerFlow;
struct BtlPokeParam;
struct BtlPokeSet;

/* btl_server_types.h: one entry of the action order (0x10 bytes) */
typedef struct {
    struct BtlPokeParam *pokeParam;
    u32 action;                 /* union BtlAction */
    u32 orderKey;
    u8 clientId;
    u8 done;
    u8 flag;                    /* the pursuit mark */
    u8 pad;
} BtlServerEntry;

/* btl_server_types.h: the move being used (its head) */
struct BtlMoveParam {
    u16 moveId;
    u16 origMoveId;
};

_Static_assert(sizeof(BtlServerEntry) == 0x10, "BtlServerEntry");

/* btl_server_flow_types.h: the fields of BtlServerFlow these functions use */
#define FLOW_CMD_QUEUE(flow)        (*(void **)((u8 *)(flow) + 0x00C))
#define FLOW_CUR_MOVE_TURN(flow)    (*(u8 **)((u8 *)(flow) + 0x4A8))
#define FLOW_NUM_ACT_ORDER(flow)    (*((u8 *)(flow) + 0x782))
#define FLOW_FRONT_ORDER_COUNT(flow) (*((u8 *)(flow) + 0x788))
#define FLOW_STATE_FLAGS(flow)      (*((u8 *)(flow) + 0x78A))
#define FLOW_ACT_ORDER(flow)        ((BtlServerEntry *)((u8 *)(flow) + 0x7E0))
#define FLOW_CMD_LOCK(flow)         ((u32 *)((u8 *)(flow) + 0x1D74))

#define STATE_FAIL_MESSAGE_SHOWN    0x10    /* stateFlags.bits.failMessageShown */

/* curMoveTurnState byte 5: bit 0 `a`, bit 1 `b` (btl_server_flow.c Flag_td) */
#define TURN_STATE_FLAGS(state)     ((state)[5])

#define BTL_ACTION_FIGHT            1
#define BTL_TURN_FLAG_PLEDGE_WAIT   10
#define BTL_VALUE_SPEED             12
#define BTL_EVENT_PURSUIT           0x53
#define BTL_EVAR_SWITCHING_POKE_ID  6

#define BTL_MOVE_SUBSTITUTE         0xa4
#define BTL_MOVE_PURSUIT            228
#define BTL_MOVE_SKILL_SWAP         0x11d

extern const u16 gPledgeMoveIds[3];

extern u32 BTL_ACTION_GetKind(const void *action);
extern u32 BTL_ACTION_GetFightMove(const void *action);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern int BTL_SERVER_FLOW_GetActionIndex(struct BtlServerFlow *flow, BtlServerEntry *entry);
extern int BTL_POKEPARAM_TestTurnFlag(struct BtlPokeParam *pp, int flag);
extern int BTL_POKEPARAM_HasStoredPokeMove(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_SetTurnFlag(struct BtlPokeParam *pp, int flag);
extern void BTL_POKEPARAM_SetStoredPokeMove(struct BtlPokeParam *pp, u8 pokeId, u16 moveId);
extern void BTL_SERVER_CMD_PushVariable(void *queue, u8 id, ...);
extern int BTL_SERVER_FLOW_PromoteActionOfPoke(struct BtlServerFlow *flow, u8 pokeId);
extern BtlServerEntry *BTL_SERVER_FLOW_FindPendingMoveActionForPoke(struct BtlServerFlow *flow, u16 moveId, u8 pokeId,
                                                                    u32 unused);
extern int BTL_MAIN_IsPokeSamePair(int pokeA, int pokeB);
extern int HAND_WAZA_AddMoveHandler(struct BtlPokeParam *pp, u16 moveId, u32 subPriority);
extern void HAND_WAZA_RemoveMoveHandlerOfPoke(struct BtlPokeParam *pp, int moveId);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern void BTL_EVENT_PushVarFrame(int line);
extern void BTL_EVENT_SetVarReadOnly(int key, __UINTPTR_TYPE__ value);
extern void BTL_EVENT_DispatchEventChecked(struct BtlServerFlow *flow, int eventId);
extern void BTL_EVENT_PopVarFrame(int line);
extern int BTL_POKESET_IsRemovedAll(struct BtlPokeSet *set);
extern void BTL_SERVER_FLOW_ProcSkillSwap(struct BtlServerFlow *flow, struct BtlPokeParam *attacker, struct BtlPokeSet *targets);
extern int BTL_SERVER_FLOW_ProcSubstituteMove(struct BtlServerFlow *flow, struct BtlPokeParam *attacker);
extern u32 BTL_SERVER_FLOW_BeginHandExScope(void *scope, int callerLine);
extern void BTL_SERVER_FLOW_EndHandExScope(u32 *scope, u32 saved, int callerLine);
extern int BTL_SERVER_FLOW_GetLockState(struct BtlServerFlow *flow);
extern int BTL_SERVER_FLOW_DispatchUncategorizedMoveEvent(struct BtlServerFlow *flow, struct BtlMoveParam *moveParam,
                                                          struct BtlPokeParam *attacker, struct BtlPokeSet *targets,
                                                          int *eventResult);

/* sdk/battle_handlers.c */
extern int bw_bh_move_is(int move, int builtin);
extern int bw_bh_move_named(int move, int named, int key, int index, int *value);

/* sdk/battle_moves.c */
extern int bw_bm_guard(int pokeId, int turn, int *out);
extern int bw_bm_third_type(int pokeId, const void *poke);
extern void bw_bm_third_type_set(int pokeId, const void *poke, int type);
extern void bw_bm_affinity_extra_set(int attackerId, int defenderId, int move, int extra);
extern void bw_bm_move_result(int pokeId, const void *poke, int failed);
extern int bw_bm_laser_focus(int pokeId, const void *poke, int turn);

static u16 entry_move(BtlServerEntry *e)
{
    return BTL_ACTION_GetKind(&e->action) == BTL_ACTION_FIGHT ? (u16)BTL_ACTION_GetFightMove(&e->action) : 0;
}

BtlServerEntry *BTL_SERVER_FLOW_FindPendingActionByMove(struct BtlServerFlow *flow, u16 moveId, u32 startIndex)
{
    BtlServerEntry *order = FLOW_ACT_ORDER(flow);
    u16 move;

    for (; startIndex < FLOW_NUM_ACT_ORDER(flow); startIndex++) {
        if (order[startIndex].done == 0 && BTL_ACTION_GetKind(&order[startIndex].action) == BTL_ACTION_FIGHT) {
            move = (u16)BTL_ACTION_GetFightMove(&order[startIndex].action);
            if (moveId == move || bw_bh_move_is(move, moveId))
                return &order[startIndex];
        }
    }
    return 0;
}

int BTL_SERVER_FLOW_ProcPledgeCombo(struct BtlServerFlow *flow, struct BtlPokeParam *pokeParam, u16 moveId, u32 unused)
{
    u32 i;
    u32 n;
    u8 id;
    struct BtlPokeParam *chosen;
    u32 best;
    u8 chosenId;
    BtlServerEntry *found[2];
    u32 v;

    for (i = 0; i < 3; i++) {
        if (bw_bh_move_is(moveId, gPledgeMoveIds[i]))
            break;
    }
    if (i == 3)
        return 0;
    if (BTL_POKEPARAM_TestTurnFlag(pokeParam, BTL_TURN_FLAG_PLEDGE_WAIT) != 0)
        return 0;
    if (BTL_POKEPARAM_HasStoredPokeMove(pokeParam) != 0)
        return 0;
    n = 0;
    id = BTL_POKEPARAM_GetPokeId(pokeParam);
    for (i = 0; i < 3; i++) {
        if (!bw_bh_move_is(moveId, gPledgeMoveIds[i])) {
            found[n] = BTL_SERVER_FLOW_FindPendingMoveActionForPoke(flow, gPledgeMoveIds[i], id, unused);
            if (found[n] != 0) {
                n++;
                if (n >= 2)
                    break;
            }
        }
    }
    if (n == 0)
        return 0;
    best = 6;
    chosen = found[0]->pokeParam;
    for (i = 0; i < n; i++) {
        v = (u32)BTL_SERVER_FLOW_GetActionIndex(flow, found[i]);
        if (v < best) {
            best = v;
            chosen = found[i]->pokeParam;
        }
    }
    chosenId = BTL_POKEPARAM_GetPokeId(chosen);
    BTL_POKEPARAM_SetTurnFlag(pokeParam, BTL_TURN_FLAG_PLEDGE_WAIT);
    BTL_POKEPARAM_SetStoredPokeMove(chosen, id, moveId);
    /* (bank 15 line 0x47a: "<user> is waiting for <partner>'s move...") */
    BTL_SERVER_CMD_PushVariable(FLOW_CMD_QUEUE(flow), 91, 0x47a, id, chosenId, 0xffff0000);
    BTL_SERVER_FLOW_PromoteActionOfPoke(flow, chosenId);
    return 1;
}

int BTL_SERVER_FLOW_ProcPursuitBeforeSwitch(struct BtlServerFlow *flow, struct BtlPokeParam *poke)
{
    BtlServerEntry *order = FLOW_ACT_ORDER(flow);
    u32 i;
    u16 key;

    for (i = 0; i < FLOW_NUM_ACT_ORDER(flow); i++) {
        if (order[i].pokeParam == poke)
            continue;
        if (order[i].done)
            continue;
        key = entry_move(&order[i]);
        if (key != 0 && bw_bh_move_is(key, BTL_MOVE_PURSUIT)) {
            if (HAND_WAZA_AddMoveHandler(order[i].pokeParam, key, (u32)BTL_POKEPARAM_GetValue(order[i].pokeParam,
                                                                                            BTL_VALUE_SPEED)))
                order[i].flag = 1;
        }
    }
    FLOW_FRONT_ORDER_COUNT(flow) = 0;
    BTL_EVENT_PushVarFrame(11288);
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_SWITCHING_POKE_ID, BTL_POKEPARAM_GetPokeId(poke));
    BTL_EVENT_DispatchEventChecked(flow, BTL_EVENT_PURSUIT);
    BTL_EVENT_PopVarFrame(11291);
    for (i = 0; i < FLOW_NUM_ACT_ORDER(flow); i++) {
        if (order[i].flag) {
            key = entry_move(&order[i]);
            if (key != 0 && bw_bh_move_is(key, BTL_MOVE_PURSUIT))
                HAND_WAZA_RemoveMoveHandlerOfPoke(order[i].pokeParam, key);
            order[i].flag = 0;
        }
    }
    return FLOW_FRONT_ORDER_COUNT(flow);
}

void BTL_SERVER_FLOW_ProcMoveSpecialEffect(struct BtlServerFlow *flow, struct BtlMoveParam *moveParam,
                                           struct BtlPokeParam *attacker, struct BtlPokeSet *targets)
{
    int out;
    u32 s, r;
    u8 *state;

    if (BTL_POKESET_IsRemovedAll(targets) != 0)
        return;
    if (bw_bh_move_is(moveParam->moveId, BTL_MOVE_SKILL_SWAP)) {
        BTL_SERVER_FLOW_ProcSkillSwap(flow, attacker, targets);
        return;
    }
    if (bw_bh_move_is(moveParam->moveId, BTL_MOVE_SUBSTITUTE)) {
        if (BTL_SERVER_FLOW_ProcSubstituteMove(flow, attacker)) {
            state = FLOW_CUR_MOVE_TURN(flow);
            if ((TURN_STATE_FLAGS(state) & 1) == 0)
                TURN_STATE_FLAGS(state) |= 3;           /* (a = 1, b = 1) */
        }
        return;
    }
    s = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 9087);
    r = (u32)BTL_SERVER_FLOW_GetLockState(flow);
    if (BTL_SERVER_FLOW_DispatchUncategorizedMoveEvent(flow, moveParam, attacker, targets, &out)) {
        r = (u32)BTL_SERVER_FLOW_GetLockState(flow);
        if (r == 2) {
            state = FLOW_CUR_MOVE_TURN(flow);
            if ((TURN_STATE_FLAGS(state) & 1) == 0)
                TURN_STATE_FLAGS(state) |= 1;
        }
    }
    if (r <= 1 && out != 0 && (FLOW_STATE_FLAGS(flow) & STATE_FAIL_MESSAGE_SHOWN) == 0) {
        /* (bank 15 line 0x47: "But it failed!") */
        BTL_SERVER_CMD_PushVariable(FLOW_CMD_QUEUE(flow), 0x5a, 0x47, 0xffff0000);
    }
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), s, 9113);
}

/* ------------------------------------------------------------------ */
/* The guards' contact effects                                         */
/* ------------------------------------------------------------------ */

/* sdk/battle_moves.h BW_BM_GUARD_* */
#define GUARD_DAMAGING_ONLY     0
#define GUARD_HP                1
#define GUARD_STAT              2
#define GUARD_STAGES            3
#define GUARD_STATUS            4
#define GUARD_COUNT             5

#define BTL_TURN_FLAG_PROTECTED 7
#define WAZA_FLAG_CONTACT       0
#define WAZA_FLAG_PROTECTABLE   3
#define BTL_STR_PROTECTED       523     /* bank 14: "<Pokemon> protected itself!" */
#define BTL_STR_WAS_HURT        0x1ae   /* bank 14: "<Pokemon> was hurt!" (Rough Skin's) */
#define BTL_STRTYPE_SET         2

#define HANDEX_KIND_DAMAGE      7
#define HANDEX_KIND_ADD_SICK    12
#define HANDEX_KIND_RANK_EFFECT 14

/* hand_common_types.h */
typedef struct {
    u16 strId;
    u16 typeAndCount;
    u32 args[8];
} BtlStrParam;

typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 flags;
    u32 pad08;
    BtlStrParam message;
} HandExDamage;

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

typedef struct {
    u32 header;
    u32 rankKind;
    u32 serialNumber;
    signed char volume;
    u8 fromMove;
    u8 showMessage;
    u8 targetCount;
    u8 targets[8];
    BtlStrParam message;
} HandExRankChange;

_Static_assert(__builtin_offsetof(HandExDamage, message) == 0x0C, "HandExDamage.message");
_Static_assert(__builtin_offsetof(HandExAddSick, pokeId) == 0x0F, "HandExAddSick.pokeId");
_Static_assert(__builtin_offsetof(HandExRankChange, targets) == 0x10, "HandExRankChange.targets");

extern void BTL_POKESET_SeekStart(struct BtlPokeSet *set);
extern struct BtlPokeParam *BTL_POKESET_SeekNext(struct BtlPokeSet *set);
extern void BTL_POKESET_Remove(struct BtlPokeSet *set, struct BtlPokeParam *pp);
extern int BTL_SERVER_FLOW_CheckNoGuardHit(struct BtlServerFlow *flow, struct BtlPokeParam *user, struct BtlPokeParam *target);
extern int BTL_SERVER_FLOW_ProcNoEffectCheck(struct BtlServerFlow *flow, struct BtlMoveParam *moveParam,
                                             struct BtlPokeParam *user, struct BtlPokeParam *target, u32 *hitTable,
                                             int eventId);
extern int BTL_SERVER_FLOW_QueryActionCancelEvent(struct BtlServerFlow *flow, struct BtlPokeParam *poke,
                                                  struct BtlPokeParam *target, u16 moveId);
extern u32 WAZA_TOOL_GetFlag(u16 move, int flag);
extern u32 WAZA_TOOL_IsDamaging(u16 move);
extern u32 BTL_SERVER_FLOW_GetTurnCount(struct BtlServerFlow *flow);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern u16 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, u32 divisor);
/* btl_pokeparam_types.h: a status's duration word (returned as the union,
 * as the game declares it: the wasm32 build passes a union by its own rule) */
union SickCont {
    u32 raw;
};

extern union SickCont BTL_CALC_MakeDefaultSickCont(int kind);

/* What a guard put up this turn does to the attacker whose contact move
 * it stopped: lose HP (Spiky Shield, with Rough Skin's line), a stat
 * lowered (King's Shield), a status (Baneful Bunker).  The records are
 * the game's own, so Magic Guard, Clear Body and the status immunities
 * apply as to Rough Skin, Intimidate and Poison Point. */
static void guard_punish(struct BtlServerFlow *flow, struct BtlPokeParam *guard, struct BtlPokeParam *attacker,
                         const int *g)
{
    u8 guardId = BTL_POKEPARAM_GetPokeId(guard), attackerId = BTL_POKEPARAM_GetPokeId(attacker);
    u32 s;

    if (BTL_POKEPARAM_IsFainted(attacker)) {
        return;
    }
    s = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 4750);
    if (g[GUARD_HP] > 0) {
        HandExDamage *d = (HandExDamage *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_DAMAGE, guardId);

        if (d != 0) {
            d->pokeId = attackerId;
            d->amount = BTL_CALC_DivideMaxHpMin1(attacker, (u32)g[GUARD_HP]);
            BTL_SERVER_FLOW_InitMsgRecord(&d->message, BTL_STRTYPE_SET, BTL_STR_WAS_HURT);
            BTL_SERVER_FLOW_AppendMsgRecordArg(&d->message, attackerId);
            BTL_SERVER_FLOW_CommitCmdRecord(flow, d);
        }
    }
    if (g[GUARD_STAT] > 0 && g[GUARD_STAGES] != 0 && !BTL_POKEPARAM_IsFainted(attacker)) {
        HandExRankChange *r = (HandExRankChange *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RANK_EFFECT, guardId);

        if (r != 0) {
            r->rankKind = (u32)g[GUARD_STAT];
            r->volume = (signed char)g[GUARD_STAGES];
            r->showMessage = 1;
            r->targetCount = 1;
            r->targets[0] = attackerId;
            BTL_SERVER_FLOW_CommitCmdRecord(flow, r);
        }
    }
    if (g[GUARD_STATUS] > 0 && !BTL_POKEPARAM_IsFainted(attacker)) {
        HandExAddSick *k = (HandExAddSick *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_ADD_SICK, guardId);

        if (k != 0) {
            k->sickKind = (u32)g[GUARD_STATUS];
            k->sickCont = BTL_CALC_MakeDefaultSickCont(g[GUARD_STATUS]).raw;
            k->pokeId = attackerId;
            BTL_SERVER_FLOW_CommitCmdRecord(flow, k);
        }
    }
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), s, 4780);
}

/* The targets a move cannot reach (the game's three no-effect checks and
 * Protect between them).  A guard of eidos's (spiky_shield, kings_shield,
 * baneful_bunker, contact_guard: the note sdk/battle_moves.c keeps for the
 * turn) is Protect, and then: King's Shield lets status moves through, and
 * a contact move it stops costs the attacker what the guard's keys say.
 * With no guard up the function is the game's own. */
void BTL_SERVER_FLOW_ProcNoEffectChecks(struct BtlServerFlow *flow, struct BtlMoveParam *moveParam,
                                        struct BtlPokeParam *userPoke, struct BtlPokeSet *targetSet, u32 *hitTable)
{
    struct BtlPokeParam *q;
    int g[GUARD_COUNT], guarded;

    BTL_POKESET_SeekStart(targetSet);
    q = BTL_POKESET_SeekNext(targetSet);
    while (q) {
        if (BTL_SERVER_FLOW_CheckNoGuardHit(flow, userPoke, q) == 0 &&
            BTL_SERVER_FLOW_ProcNoEffectCheck(flow, moveParam, userPoke, q, hitTable, 43))
            BTL_POKESET_Remove(targetSet, q);
        q = BTL_POKESET_SeekNext(targetSet);
    }
    BTL_POKESET_SeekStart(targetSet);
    q = BTL_POKESET_SeekNext(targetSet);
    while (q) {
        if (BTL_SERVER_FLOW_ProcNoEffectCheck(flow, moveParam, userPoke, q, hitTable, 44))
            BTL_POKESET_Remove(targetSet, q);
        q = BTL_POKESET_SeekNext(targetSet);
    }
    if (WAZA_TOOL_GetFlag(moveParam->moveId, WAZA_FLAG_PROTECTABLE)) {
        BTL_POKESET_SeekStart(targetSet);
        q = BTL_POKESET_SeekNext(targetSet);
        while (q) {
            guarded = bw_bm_guard(BTL_POKEPARAM_GetPokeId(q), (int)BTL_SERVER_FLOW_GetTurnCount(flow), g);
            if (BTL_POKEPARAM_TestTurnFlag(q, BTL_TURN_FLAG_PROTECTED) &&
                !(guarded && g[GUARD_DAMAGING_ONLY] && WAZA_TOOL_IsDamaging(moveParam->moveId) == 0) &&
                BTL_SERVER_FLOW_QueryActionCancelEvent(flow, userPoke, q, moveParam->moveId) == 0) {
                BTL_POKESET_Remove(targetSet, q);
                BTL_SERVER_CMD_PushVariable(FLOW_CMD_QUEUE(flow), 91, BTL_STR_PROTECTED, BTL_POKEPARAM_GetPokeId(q),
                                            0xffff0000);
                if (guarded && q != userPoke && WAZA_TOOL_GetFlag(moveParam->moveId, WAZA_FLAG_CONTACT))
                    guard_punish(flow, q, userPoke, g);
            }
            q = BTL_POKESET_SeekNext(targetSet);
        }
    }
    BTL_POKESET_SeekStart(targetSet);
    q = BTL_POKESET_SeekNext(targetSet);
    while (q) {
        if (BTL_SERVER_FLOW_ProcNoEffectCheck(flow, moveParam, userPoke, q, hitTable, 45))
            BTL_POKESET_Remove(targetSet, q);
        q = BTL_POKESET_SeekNext(targetSet);
    }
}

/* ------------------------------------------------------------------ */
/* Type effectiveness: a third type, Freeze-Dry, Flying Press           */
/* ------------------------------------------------------------------ */

#define POKETYPE_FLYING         2
#define POKETYPE_GROUND         4
#define POKETYPE_WATER          10
#define POKETYPE_NONE           31      /* (the port's "no type", arm9 poke_type.h) */
#define BTL_SICK_ROOST          0x18    /* the Flying type is hidden this turn (BTL_POKEPARAM_GetTypes) */

#define BTL_AFF_NONE            0
#define BTL_AFF_QUARTER         1
#define BTL_AFF_HALF            2
#define BTL_AFF_NORMAL          3
#define BTL_AFF_DOUBLE          4
#define BTL_AFF_QUADRUPLE       5

#define NAMED_SUPER_EFFECTIVE   0x1000B     /* sdk/battle_handlers.h */
#define NAMED_FREEZE_DRY        0x1000C
#define NAMED_SECOND_TYPE       0x1000D
#define NAMED_FLYING_PRESS      0x1000E
#define NAMED_HITS_FLOATING     0x10015
#define KEY_TYPE                4           /* BW_BH_KEY_TYPE */

extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern u8 BTL_TYPEPAIR_GetHigh(u16 pair);
extern u8 BTL_TYPEPAIR_GetLow(u16 pair);
extern int BTL_TYPEPAIR_IsSingleType(u16 pair);
extern int BTL_SERVER_FLOW_CalcSingleTypeAffinity(struct BtlServerFlow *flow, struct BtlPokeParam *attacker,
                                                  struct BtlPokeParam *defender, u32 moveType, u8 defenderType);
extern int BTL_SERVER_FLOW_IsPokeFloating(struct BtlServerFlow *flow, struct BtlPokeParam *poke, int checkFlying);
extern void BTL_SERVER_FLOW_PushNoEffectMessage(struct BtlServerFlow *flow, struct BtlPokeParam *poke);
extern void BTL_SERVER_FLOW_ProcImmunityOverrideNotice(struct BtlServerFlow *flow, struct BtlPokeParam *poke);

/* An affinity as doublings: quarter -2, half -1, neutral 0, double 1,
 * quadruple 2 (BTL_CALC_CombineTypeAffinity multiplies the scales of
 * gBtlCalcAffinityScales; adding doublings is the same product, kept
 * exact past the classes for three types and a second type). */
static int affinity_doublings(int a)
{
    return a - BTL_AFF_NORMAL;
}

/* The class of `doublings`: four times and a quarter at most, as the
 * game's classes stop there; the rest is kept for the damage step
 * (bw_bm_affinity_extra, BTL_CALC_ApplyTypeAffinity: 8 and 1/8 times, as
 * the later games). */
static int affinity_class(int doublings, int *extra)
{
    int d = doublings < -2 ? -2 : doublings > 2 ? 2 : doublings;

    *extra = doublings - d;
    return BTL_AFF_NORMAL + d;
}

/* The defender's types: the pair (with the Flying type Roost hides
 * hidden), and a third type a move gave it (sdk/battle_moves.c). */
static int defender_types(struct BtlPokeParam *defender, u8 *types)
{
    u16 pair = BTL_POKEPARAM_GetTypePair(defender);
    int n = 0, third = bw_bm_third_type(BTL_POKEPARAM_GetPokeId(defender), defender);

    types[n++] = BTL_TYPEPAIR_GetHigh(pair);
    if (BTL_TYPEPAIR_IsSingleType(pair) == 0)
        types[n++] = BTL_TYPEPAIR_GetLow(pair);
    if (third >= 0 && third != types[0] && (n < 2 || third != types[1]) &&
        !(third == POKETYPE_FLYING && BTL_POKEPARAM_HasSick(defender, BTL_SICK_ROOST)))
        types[n++] = (u8)third;
    return n;
}

int BTL_SERVER_FLOW_CalcTypeEffectiveness(struct BtlServerFlow *flow, struct BtlPokeParam *attacker,
                                          struct BtlPokeParam *defender, struct BtlMoveParam *moveParam, int showMessage)
{
    u8 types[3];
    int n, k, a, r = BTL_AFF_NORMAL, changed = 0, superType = -1, secondType = -1, hitsFloating;
    int doublings = 0, none = 0, extra = 0;
    u8 moveType = ((u8 *)moveParam)[6];         /* BtlMoveParam.type */

    bw_bm_affinity_extra_set(BTL_POKEPARAM_GetPokeId(attacker), BTL_POKEPARAM_GetPokeId(defender),
                             moveParam->moveId, 0);
    if (moveType == POKETYPE_NONE)
        return BTL_AFF_NORMAL;
    /* the move's line: super effective against a type (Freeze-Dry: Water),
     * a second type its effectiveness takes (Flying Press: Flying) */
    if (bw_bh_move_named(moveParam->moveId, NAMED_FREEZE_DRY, -1, 0, 0)) {
        superType = POKETYPE_WATER;
        (void)bw_bh_move_named(moveParam->moveId, NAMED_FREEZE_DRY, KEY_TYPE, 1, &superType);
    } else {
        (void)bw_bh_move_named(moveParam->moveId, NAMED_SUPER_EFFECTIVE, KEY_TYPE, 1, &superType);
    }
    if (bw_bh_move_named(moveParam->moveId, NAMED_FLYING_PRESS, -1, 0, 0)) {
        secondType = POKETYPE_FLYING;
        (void)bw_bh_move_named(moveParam->moveId, NAMED_FLYING_PRESS, KEY_TYPE, 1, &secondType);
    } else {
        (void)bw_bh_move_named(moveParam->moveId, NAMED_SECOND_TYPE, KEY_TYPE, 1, &secondType);
    }
    /* a Ground move that hits Flying and floating Pokemon (Thousand Arrows) */
    hitsFloating = moveType == POKETYPE_GROUND && bw_bh_move_named(moveParam->moveId, NAMED_HITS_FLOATING, -1, 0, 0);
    n = defender_types(defender, types);
    for (k = 0; k < n; k++) {
        int d;

        a = BTL_SERVER_FLOW_CalcSingleTypeAffinity(flow, attacker, defender, moveType, types[k]);
        d = affinity_doublings(a);
        if (secondType >= 0) {
            int b = BTL_SERVER_FLOW_CalcSingleTypeAffinity(flow, attacker, defender, (u32)secondType, types[k]);

            if (b == BTL_AFF_NONE)
                a = BTL_AFF_NONE;
            d += affinity_doublings(b);
        }
        if (types[k] == superType) {
            a = BTL_AFF_DOUBLE;
            d = 1;
        }
        if (hitsFloating && types[k] == POKETYPE_FLYING && a == BTL_AFF_NONE) {
            a = BTL_AFF_NORMAL;
            d = 0;
        }
        if (a == BTL_AFF_NONE)
            none = 1;
        doublings += d;
    }
    r = none ? BTL_AFF_NONE : affinity_class(doublings, &extra);
    if (extra != 0)
        bw_bm_affinity_extra_set(BTL_POKEPARAM_GetPokeId(attacker), BTL_POKEPARAM_GetPokeId(defender),
                                 moveParam->moveId, extra);
    if (r != 0) {
        if (moveType != POKETYPE_GROUND || hitsFloating)
            return r;
        changed = BTL_SERVER_FLOW_IsPokeFloating(flow, defender, 0);
        if (changed == 0)
            return r;
        r = 0;
    } else if (moveType == POKETYPE_GROUND) {
        if (BTL_SERVER_FLOW_IsPokeFloating(flow, defender, 1) == 0)
            return BTL_AFF_NORMAL;
    }
    if (r == 0 && showMessage != 0) {
        if (changed == 0)
            BTL_SERVER_FLOW_PushNoEffectMessage(flow, defender);
        else
            BTL_SERVER_FLOW_ProcImmunityOverrideNotice(flow, defender);
    }
    return r;
}

/* A type change the flow runs (Soak, Conversion, Reflect Type, Camouflage,
 * Color Change, Burn Up): a third type a move gave the Pokemon goes with
 * its old types, as in the later games.  Here, on the server only: the
 * clients replay SetTypes later (btl_client.c), when a third type given
 * after the change in the same turn must stay. */
extern int BTL_POSPOKE_IsPokeInside(void *posPoke, int pokeId);
extern struct BtlPokeParam *BTL_SIDE_GetPokeByIdThunk(void *pokeCon, int pokeId);
extern u32 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp);
extern int BTL_ID_IsArceusSpecies(u32 species);
extern void BTL_SERVER_CMD_Push(void *queue, int cmd, ...);
extern void BTL_POKEPARAM_SetTypes(struct BtlPokeParam *poke, int typePair);

/* btl_server_flow.c: the set-types command (BtlServerCmdSetPokeTypes) and the flow fields it uses */
typedef struct {
    u32 header;
    u16 h4;                     /* the type pair */
    u8 b6;                      /* the Pokemon */
    u8 b7;                      /* 1: no line */
} BtlServerCmdSetPokeTypes;

#define FLOW_OFS_POKE_CON       0x008
#define FLOW_OFS_POS_POKE       0x1AB8
#define FLOW_POKE_CON(flow)     (*(void **)((u8 *)(flow) + FLOW_OFS_POKE_CON))
#define FLOW_POS_POKE(flow)     ((void *)((u8 *)(flow) + FLOW_OFS_POS_POKE))

u8 BTL_SERVER_FLOW_ExecCmdSetPokeTypes(struct BtlServerFlow *flow, const BtlServerCmdSetPokeTypes *cmd)
{
    struct BtlPokeParam *a;

    if (BTL_POSPOKE_IsPokeInside(FLOW_POS_POKE(flow), cmd->b6)) {
        a = BTL_SIDE_GetPokeByIdThunk(FLOW_POKE_CON(flow), cmd->b6);
        if (BTL_POKEPARAM_IsFainted(a) == 0 && BTL_ID_IsArceusSpecies(BTL_POKEPARAM_GetSpecies(a)) == 0) {
            BTL_SERVER_CMD_Push(FLOW_CMD_QUEUE(flow), 0x16, cmd->b6, cmd->h4);
            BTL_POKEPARAM_SetTypes(a, cmd->h4);
            bw_bm_third_type_set(cmd->b6, a, -1);
            if (cmd->b7 == 0 && BTL_TYPEPAIR_IsSingleType(cmd->h4))
                BTL_SERVER_CMD_PushVariable(FLOW_CMD_QUEUE(flow), 0x5b, 0x380, cmd->b6, BTL_TYPEPAIR_GetHigh(cmd->h4),
                                            0xffff0000);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Stomping Tantrum and Laser Focus                                    */
/* ------------------------------------------------------------------ */

#define BTL_EVENT_MOVE_SUCCEEDED    0x25
#define BTL_EVENT_MOVE_FAILED       0x26
#define BTL_EVENT_CRITICAL_CHECK    0x36
#define BTL_EVAR_ATTACKER_ID        0x03
#define BTL_EVAR_DEFENDER_ID        0x04
#define BTL_EVAR_MOVE_ID            0x12
#define BTL_EVAR_CRITICAL_RANK      0x2C
#define BTL_EVAR_PREVENT_FLAG       0x41
#define WAZA_PARAM_CRITICAL_RANK    7
#define BTL_CRITICAL_RANK_MAX       4
#define FLOW_MAIN(flow)             (*(void **)((u8 *)(flow) + 0x004))

extern void BTL_SERVER_FLOW_DispatchMoveResultEvent(struct BtlServerFlow *flow, u32 pokeId, u32 moveId, int eventId);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern void BTL_EVENT_SetVar(int key, __UINTPTR_TYPE__ value);
extern void BTL_EVENT_SetVarOnce(int key, __UINTPTR_TYPE__ value);
extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);
extern u8 BTL_POKEPARAM_GetCritStage(struct BtlPokeParam *pp);
extern int BTL_CALC_RollCriticalHit(u8 rank);
extern int BTL_MAIN_ReturnZero(void *main, int which);
extern u32 WAZA_TOOL_GetParam(u16 move, int param);
extern u32 WAZA_TOOL_IsAlwaysCritical(u16 move);

/* The move worked (and the result is kept: Stomping Tantrum). */
void BTL_SERVER_FLOW_NotifyMoveSucceeded(struct BtlServerFlow *flow, u32 pokeId, u32 moveId)
{
    u32 old = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 0x11e7);

    bw_bm_move_result((int)pokeId, BTL_SERVER_FLOW_GetPokeParam(flow, (int)pokeId), 0);
    BTL_SERVER_FLOW_DispatchMoveResultEvent(flow, pokeId, moveId, BTL_EVENT_MOVE_SUCCEEDED);
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), old, 0x11e7 + 2);
}

/* The move failed (missed, had no effect, or did nothing). */
void BTL_SERVER_FLOW_NotifyMoveFailed(struct BtlServerFlow *flow, u32 pokeId, u32 moveId)
{
    u32 old = BTL_SERVER_FLOW_BeginHandExScope(FLOW_CMD_LOCK(flow), 0x11f6);

    bw_bm_move_result((int)pokeId, BTL_SERVER_FLOW_GetPokeParam(flow, (int)pokeId), 1);
    BTL_SERVER_FLOW_DispatchMoveResultEvent(flow, pokeId, moveId, BTL_EVENT_MOVE_FAILED);
    BTL_SERVER_FLOW_EndHandExScope(FLOW_CMD_LOCK(flow), old, 0x11f6 + 2);
}

/* Whether the move is a critical hit: the rank's roll, the moves that are
 * always critical (Frost Breath), and the user under Laser Focus.  What
 * prevents one (Battle Armor, Lucky Chant) prevents these too. */
int BTL_SERVER_FLOW_CheckCriticalHit(struct BtlServerFlow *flow, struct BtlPokeParam *attacker,
                                     struct BtlPokeParam *defender, u16 moveId)
{
    int r, n;
    u16 h;

    h = (u16)WAZA_TOOL_GetParam(moveId, WAZA_PARAM_CRITICAL_RANK);
    h = (u16)(h + BTL_POKEPARAM_GetCritStage(attacker));
    BTL_EVENT_PushVarFrame(11952);
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_DEFENDER_ID, BTL_POKEPARAM_GetPokeId(defender));
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_ATTACKER_ID, BTL_POKEPARAM_GetPokeId(attacker));
    BTL_EVENT_SetVar(BTL_EVAR_CRITICAL_RANK, h);
    r = 0;
    BTL_EVENT_SetVarOnce(BTL_EVAR_PREVENT_FLAG, 0);
    BTL_EVENT_DispatchEventChecked(flow, BTL_EVENT_CRITICAL_CHECK);
    if (BTL_EVENT_GetVar(BTL_EVAR_PREVENT_FLAG) == 0) {
        if (WAZA_TOOL_IsAlwaysCritical(moveId) ||
            bw_bm_laser_focus(BTL_POKEPARAM_GetPokeId(attacker), attacker, (int)BTL_SERVER_FLOW_GetTurnCount(flow))) {
            r = 1;
        } else {
            n = (int)BTL_EVENT_GetVar(BTL_EVAR_CRITICAL_RANK);
            if (n > BTL_CRITICAL_RANK_MAX) n = BTL_CRITICAL_RANK_MAX;
            r = BTL_CALC_RollCriticalHit((u8)n);
        }
        if (BTL_MAIN_ReturnZero(FLOW_MAIN(flow), 3)) r = 1;
    }
    BTL_EVENT_PopVarFrame(11975);
    return r;
}

#define WAZA_PARAM_RANK_PERCENT     0x12        /* the record's chance of its first stat change */
#define BTL_EVAR_EFFECT_CHANCE      0x26
#define BTL_EVAR_EFFECT_BLOCKED     0x41
#define BTL_EVENT_ADDED_EFFECT_ROLL 0x51

extern u32 BTL_CALC_Random(u32 range);
extern int bw_bh_move_chance(int move, int percent);

/* The roll for a move's added stat change: the record's chance, or the
 * move line's `chance` key (an Ancient Power copy with chance=100 always
 * raises its stats); the handlers of event 0x51 then change it as in the
 * game. */
int BTL_SERVER_FLOW_RollMoveAddedEffectChance(struct BtlServerFlow *flow, struct BtlMoveParam *moveParam,
                                             struct BtlPokeParam *attacker, struct BtlPokeParam *target)
{
    u8 chance, blocked, rolled;
    u32 hit;

    chance = (u8)bw_bh_move_chance(moveParam->moveId,
                                   (int)(u8)WAZA_TOOL_GetParam(moveParam->moveId, WAZA_PARAM_RANK_PERCENT));
    BTL_EVENT_PushVarFrame(8142);
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_ATTACKER_ID, BTL_POKEPARAM_GetPokeId(attacker));
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_DEFENDER_ID, BTL_POKEPARAM_GetPokeId(target));
    BTL_EVENT_SetVarReadOnly(BTL_EVAR_MOVE_ID, moveParam->moveId);
    hit = 0;
    BTL_EVENT_SetVarOnce(BTL_EVAR_EFFECT_BLOCKED, 0);
    BTL_EVENT_SetVar(BTL_EVAR_EFFECT_CHANCE, chance);
    BTL_EVENT_DispatchEventChecked(flow, BTL_EVENT_ADDED_EFFECT_ROLL);
    blocked = (u8)BTL_EVENT_GetVar(BTL_EVAR_EFFECT_BLOCKED);
    rolled = (u8)BTL_EVENT_GetVar(BTL_EVAR_EFFECT_CHANCE);
    BTL_EVENT_PopVarFrame(8152);
    if (blocked == 0) {
        if (BTL_CALC_Random(100) < rolled)
            hit = 1;
        if (hit)
            return 1;
        if (BTL_MAIN_ReturnZero(FLOW_MAIN(flow), 0) != 0)
            return 1;
    }
    return 0;
}

#define BTL_MOVE_STRUGGLE_ID        0xa5
#define BTL_EVENT_MOVE_FAIL_CHECK   0x1f

extern int BTL_SERVER_FLOW_QueryMoveFailReasonEvent(struct BtlServerFlow *flow, struct BtlPokeParam *pokeParam, u32 moveId,
                                                    int eventId);
extern void BTL_SERVER_FLOW_ProcMoveExecuteFail(struct BtlServerFlow *flow, struct BtlPokeParam *pokeParam, u16 moveId,
                                                int failReason);

/* The move's own check (Last Resort, Belch, Burn Up): when it fails, the
 * failure is the move's result too. */
int BTL_SERVER_FLOW_CheckMoveExecuteFailByEvent(struct BtlServerFlow *flow, struct BtlPokeParam *pokeParam, u16 moveId)
{
    int result;

    if (moveId != BTL_MOVE_STRUGGLE_ID) {
        result = BTL_SERVER_FLOW_QueryMoveFailReasonEvent(flow, pokeParam, moveId, BTL_EVENT_MOVE_FAIL_CHECK);
        if (result != 0) {
            bw_bm_move_result(BTL_POKEPARAM_GetPokeId(pokeParam), pokeParam, 1);
            BTL_SERVER_FLOW_ProcMoveExecuteFail(flow, pokeParam, moveId, result);
            return 1;
        }
    }
    return 0;
}
