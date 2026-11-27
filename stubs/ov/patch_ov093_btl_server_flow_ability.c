/*
 * patches: src/game/ov093/btl_server_flow.c BTL_SERVER_FLOW_CheckNoGuardHit BTL_SERVER_FLOW_CalcMoveDamage BTL_SERVER_FLOW_CheckSickImmunity BTL_SERVER_FLOW_ProcPokeLeaveField BTL_SERVER_FLOW_CanUseHeldItem BTL_SERVER_FLOW_ItemEffectTriggerEntryEvent BTL_SERVER_FLOW_CalcMultiHitCount BTL_SERVER_FLOW_SetHeldItem BTL_SERVER_FLOW_RunBagModeSetup
 *
 * The server's own tests of an ability by its id: No Guard's sure hit, a
 * burn that does not halve the damage of a Pokemon with Guts, Multitype
 * that Gastro Acid cannot stop, Unnerve leaving the field (the foes' berries
 * are checked again), Klutz's unused held item, the Ability Urge that does
 * not run Imposter's entry again.  A mod's ability that works
 * like the tested one (sdk/battle_handlers.h bw_bh_ability_is,
 * eidos/tables/ability_handlers.txt) counts as that ability.  With no
 * table lines each function does what the game's does.
 *
 * Two functions serve eidos's own ability bases: CalcMultiHitCount asks
 * the hit count of a single-hit move too when the attacker's ability is
 * parental_bond, and SetHeldItem puts the item a Pokemon used up in the
 * item-gone event (eidos's event variable 0x100, for symbiosis; one more
 * variable in that event's frame, read by no game handler).  CheckSickImmunity
 * tests the Pokemon's types with BTL_POKEPARAM_HasType, so a third type a
 * move added counts (sdk/battle_moves.c).
 *
 * CheckNoGuardHit asks BTL_SERVER_FLOW_GetFrontPosOfPoke whether a Pokemon
 * stands at a front position, the same test the game's
 * BTL_POSPOKE_GetFieldPos makes on the flow's position table (6 is
 * BTL_POSPOKE_NO_POS, which no rule counts as a front position).
 *
 * RunBagModeSetup serves a test switch (EIDOS_DEBUG_LAUNCHER,
 * docs/test-switches.md): a battle that is no link battle uses items with
 * the Wonder Launcher's rules (bagMode 1, which only a link battle has),
 * so the Ability Urge's effect can be tested.  Off, it is the game's.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __UINTPTR_TYPE__ uptr;

struct BtlServer;
struct BtlMain;
struct BtlSide;
struct BtlPokeParam;

/* btl_server_flow_types.h: the head of struct BtlServerFlow, up to cmdLock
 * (the DS layout: both builds have 4-byte pointers, docs/lp64.md) */
typedef struct BtlServerFlow {
    struct BtlServer *server;
    struct BtlMain *mainModule;
    struct BtlSide *pokeCon;
    void *cmdQueue;
    u8 fields010[0x018 - 0x010];           /* the fields these functions do not use */
    int bagMode;                           /* 0x018: 1 in a link battle, the Wonder Launcher's rules */
    u8 fields01C[0x1D74 - 0x01C];
    u32 cmdLock;                           /* 0x1D74 */
} BtlServerFlow;

_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(BtlServerFlow, cmdQueue) == 0x00C, "BtlServerFlow.cmdQueue");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(BtlServerFlow, bagMode) == 0x018, "BtlServerFlow.bagMode");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(BtlServerFlow, cmdLock) == 0x1D74, "BtlServerFlow.cmdLock");

/* btl_server_types.h */
struct BtlMoveParam {
    u16 moveId;
    u16 origMoveId;
    u16 typePair;
    u8 type;
    u8 pad07;
    u32 damageType;
    u32 range;
    u32 flags;
};

/* btl_server_flow_types.h: the walk over the living pokes of the field */
typedef struct BtlFieldPokeIter {
    u8 client;
    u8 slot;
    u8 done;
    u8 triple;
} BtlFieldPokeIter;

union SickCont {
    u32 raw;
};

/* arm9/poke_type.h: "no type" off the DS */
#define POKETYPE_NONE 31

#define BPP_VALUE_ABILITY          0x10
#define BPP_VALUE_CURRENT_ABILITY  0x11  /* the ability, 0 while Gastro Acid holds it */
#define BTL_POSPOKE_NO_POS         6

#define ABILITY_GUTS       62
#define ABILITY_NO_GUARD   0x63
#define ABILITY_KLUTZ      103
#define ABILITY_MULTITYPE  121
#define ABILITY_UNNERVE    0x7f
#define ABILITY_IMPOSTER   150
#define NAMED_PARENTAL_BOND 0x10119      /* sdk/battle_handlers.h BW_BH_PARENTAL_BOND */

/* version.h PM_SRC_LINE: the English cartridge's line */
#define SRC_LINE(english) (english)

extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern u16 BTL_POKEPARAM_GetSickParam(struct BtlPokeParam *pp, int sick);
extern int BTL_POKEPARAM_GetMajorStatusIndex(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_CureSickBySource(struct BtlPokeParam *pp, int sourceId);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern int BTL_SERVER_FLOW_GetFrontPosOfPoke(BtlServerFlow *flow, int pokeId);
extern int BTL_SERVER_FLOW_GetEffectiveWeather(BtlServerFlow *flow);
extern u32 BTL_SERVER_FLOW_CalcMovePower(BtlServerFlow *flow, struct BtlPokeParam *attacker, struct BtlPokeParam *defender, const struct BtlMoveParam *move);
extern u32 BTL_SERVER_FLOW_CalcAttackStat(BtlServerFlow *flow, struct BtlPokeParam *attacker, struct BtlPokeParam *defender, const struct BtlMoveParam *move, u32 isCritical);
extern u32 BTL_SERVER_FLOW_CalcDefenseStat(BtlServerFlow *flow, struct BtlPokeParam *attacker, struct BtlPokeParam *defender, const struct BtlMoveParam *move, u32 isCritical);
extern u32 BTL_SERVER_FLOW_CalcTypeMatchMultiplier(BtlServerFlow *flow, struct BtlPokeParam *attacker, int type);
extern void BTL_SERVER_FLOW_DispatchPokeLeaveEvent(BtlServerFlow *flow, struct BtlPokeParam *poke);
extern void BTL_SERVER_FLOW_SkyDropReleaseTarget(BtlServerFlow *flow, struct BtlPokeParam *poke, int flag);
extern void BTL_SERVER_FLOW_InitFieldPokeIter(BtlFieldPokeIter *it, BtlServerFlow *flow);
extern void BTL_SERVER_FLOW_SetFieldPokeIterRule(BtlFieldPokeIter *it, BtlServerFlow *flow);
extern int BTL_SERVER_FLOW_NextFieldPokeIter(BtlFieldPokeIter *it, BtlServerFlow *flow, struct BtlPokeParam **out);
extern int BTL_SERVER_FLOW_CheckShowdown(BtlServerFlow *flow);
extern void BTL_SERVER_FLOW_RecheckOpponentAbilityEffects(BtlServerFlow *flow, struct BtlPokeParam *poke);
extern void BTL_SERVER_CMD_Push(void *queue, int cmd, ...);
extern struct BtlPokeParam *BTL_SIDE_GetPokeByIdThunk(struct BtlSide *side, int pokeId);
extern int BTL_FIELD_IsStaticSlotInUse(int slot);
extern void BTL_FIELD_RemoveHolderFromStaticSlots(int pokeId);
extern void HAND_TOKUSEI_RemoveAbilityHandlers(struct BtlPokeParam *poke);
extern void HAND_TOKUSEI_DisableAbilityHandlers(struct BtlPokeParam *poke);
extern void HAND_ITEM_RemoveUnforcedFactors(struct BtlPokeParam *poke);
extern void HAND_ITEM_DisablePokeFactors(struct BtlPokeParam *poke);
extern void HAND_WAZA_RemoveAllMoveHandlers(struct BtlPokeParam *poke);
extern int WAZA_TOOL_GetDamageType(u16 moveId);
extern u32 BTL_CALC_CalcBaseDamage(u32 power, u32 attack, u8 level, u32 defense);
extern u32 BTL_CALC_MulRatio(u32 value, u32 ratio);
extern u32 BTL_CALC_GetWeatherMoveRatio(int weather, int type);
extern u32 BTL_CALC_Random(u32 range);
extern u32 BTL_CALC_ApplyTypeAffinity(u32 value, u32 affinity);
extern int BTL_MAIN_ReturnZero(struct BtlMain *main, int which);
extern void BTL_EVENT_PushVarFrame(int line);
extern void BTL_EVENT_PopVarFrame(int line);
extern void BTL_EVENT_SetVar(int key, uptr value);
extern void BTL_EVENT_SetVarReadOnly(int key, uptr value);
extern void BTL_EVENT_SetVarRanged(int key, uptr value, u32 minValue, u32 maxValue);
extern void BTL_EVENT_DispatchEventChecked(BtlServerFlow *flow, int eventId);
extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);
extern u32 WAZA_TOOL_GetParam(u32 move, u32 param);
extern int BTL_CALC_RollHitCount(int maxHits);
extern void BTL_EVENT_SetVarOnce(int key, uptr value);
extern u32 BTL_SERVER_FLOW_BeginHandExScope(void *scope, int callerLine);
extern void BTL_SERVER_FLOW_EndHandExScope(u32 *scope, u32 saved, int callerLine);
extern void BTL_SERVER_FLOW_DispatchPokeEntryEventToType4Thunk(BtlServerFlow *flow, struct BtlPokeParam *poke);
extern int BTL_SERVER_FLOW_GetLockStateThunk(BtlServerFlow *flow);
extern void BTL_SERVER_FLOW_PushBagModePositions(BtlServerFlow *flow);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);
extern int bw_bh_test_launcher_item(void);

static int has_ability(struct BtlPokeParam *poke, int builtin)
{
    return bw_bh_ability_is(BTL_POKEPARAM_GetValue(poke, BPP_VALUE_CURRENT_ABILITY), builtin);
}

int BTL_SERVER_FLOW_CheckNoGuardHit(BtlServerFlow *flow, struct BtlPokeParam *userPoke, struct BtlPokeParam *targetPoke)
{
    u8 t;
    if (BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, BTL_POKEPARAM_GetPokeId(userPoke)) != BTL_POSPOKE_NO_POS) {
        if (has_ability(userPoke, ABILITY_NO_GUARD))
            return 1;
    }
    if (BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, BTL_POKEPARAM_GetPokeId(targetPoke)) != BTL_POSPOKE_NO_POS) {
        if (has_ability(targetPoke, ABILITY_NO_GUARD))
            return 1;
    }
    if (BTL_POKEPARAM_HasSick(userPoke, 0x1c) != 0)
        return 1;
    if (BTL_POKEPARAM_HasSick(userPoke, 0x1d) != 0) {
        t = BTL_POKEPARAM_GetSickParam(userPoke, 0x1d);
        if (t == BTL_POKEPARAM_GetPokeId(targetPoke))
            return 1;
    }
    return 0;
}

int BTL_SERVER_FLOW_CalcMoveDamage(BtlServerFlow *flow, struct BtlPokeParam *attacker, struct BtlPokeParam *defender, const struct BtlMoveParam *moveRecord, u32 typeMatchup, u32 rangeModifier, u32 isCritical, int fixedRoll, u16 *out)
{
    int category = WAZA_TOOL_GetDamageType(moveRecord->moveId);
    int flag = 0;
    u32 result;
    u32 x;
    u32 y;
    u32 value;
    u32 factor;
    u16 scale;
    u8 level;
    BTL_EVENT_PushVarFrame(SRC_LINE(0x1c21));
    BTL_EVENT_SetVarReadOnly(0x38, typeMatchup);
    BTL_EVENT_SetVarReadOnly(3, BTL_POKEPARAM_GetPokeId(attacker));
    BTL_EVENT_SetVarReadOnly(4, BTL_POKEPARAM_GetPokeId(defender));
    BTL_EVENT_SetVarReadOnly(0x45, isCritical);
    BTL_EVENT_SetVarReadOnly(0x16, moveRecord->type);
    BTL_EVENT_SetVarReadOnly(0x12, moveRecord->moveId);
    BTL_EVENT_SetVarReadOnly(0x1a, category);
    BTL_EVENT_SetVar(0x37, 0);
    BTL_EVENT_DispatchEventChecked(flow, 0x46);
    result = (u32)BTL_EVENT_GetVar(0x37);
    if (result) {
        flag = 1;
    } else {
        x = BTL_SERVER_FLOW_CalcMovePower(flow, attacker, defender, moveRecord);
        y = BTL_SERVER_FLOW_CalcAttackStat(flow, attacker, defender, moveRecord, isCritical);
        value = BTL_SERVER_FLOW_CalcDefenseStat(flow, attacker, defender, moveRecord, isCritical);
        level = BTL_POKEPARAM_GetValue(attacker, 15);
        value = BTL_CALC_CalcBaseDamage(x, y, level, value);
        if (rangeModifier != 0x1000) value = BTL_CALC_MulRatio(value, rangeModifier);
        factor = BTL_CALC_GetWeatherMoveRatio(BTL_SERVER_FLOW_GetEffectiveWeather(flow), moveRecord->type);
        if (factor != 0x1000) value = BTL_CALC_MulRatio(value, factor);
        if (isCritical) value *= 2;
        if (!BTL_MAIN_ReturnZero(flow->mainModule, 7)) {
            if (fixedRoll) scale = 85;
            else scale = 100 - BTL_CALC_Random(16);
            value = (u32)(value * scale) / 100u;
        }
        if (moveRecord->type != POKETYPE_NONE) value = BTL_CALC_MulRatio(value, BTL_SERVER_FLOW_CalcTypeMatchMultiplier(flow, attacker, moveRecord->type));
        value = BTL_CALC_ApplyTypeAffinity(value, typeMatchup);
        if (category == 1 && BTL_POKEPARAM_GetMajorStatusIndex(attacker) == 4 && !has_ability(attacker, ABILITY_GUTS)) {
            value = (u32)(50u * value) / 100u;
        }
        if (!value) value = 1;
        BTL_EVENT_SetVarRanged(0x35, 0x1000, 0x29, 0x20000);
        BTL_EVENT_SetVar(0x32, value);
        BTL_EVENT_DispatchEventChecked(flow, 0x47);
        factor = (u32)BTL_EVENT_GetVar(0x35);
        result = BTL_CALC_MulRatio((u32)BTL_EVENT_GetVar(0x32), factor);
    }
    BTL_EVENT_DispatchEventChecked(flow, 0x48);
    BTL_EVENT_PopVarFrame(SRC_LINE(0x1c8d));
    *out = result;
    return flag;
}

int BTL_SERVER_FLOW_CheckSickImmunity(BtlServerFlow *flow, struct BtlPokeParam *target, int sickId, union SickCont sickCont, u8 mode)
{
    (void)sickCont;
    if (BTL_POKEPARAM_HasSick(target, sickId) && mode != 2) return 1;
    if (sickId < 6 && BTL_POKEPARAM_GetMajorStatusIndex(target) && mode == 0) return 3;
    if (BTL_SERVER_FLOW_GetEffectiveWeather(flow) == 1 && sickId == 3) return 3;
    /* (BTL_POKEPARAM_HasType: the pair and a third type a move added,
     * sdk/battle_moves.c; the game tests the pair) */
    if (sickId == 5) {
        if (BTL_POKEPARAM_HasType(target, 8) || BTL_POKEPARAM_HasType(target, 3)) return 2;
    }
    if (sickId == 4 && BTL_POKEPARAM_HasType(target, 9)) return 2;
    if (sickId == 3 && BTL_POKEPARAM_HasType(target, 14)) return 2;
    if (sickId == 18 && BTL_POKEPARAM_HasType(target, 11)) return 2;
    if (sickId == 14 && BTL_POKEPARAM_GetMajorStatusIndex(target)) return 3;
    if (sickId == 16 && bw_bh_ability_is(BTL_POKEPARAM_GetValue(target, BPP_VALUE_ABILITY), ABILITY_MULTITYPE)) return 3;
    return 0;
}

void BTL_SERVER_FLOW_ProcPokeLeaveField(BtlServerFlow *flow, struct BtlPokeParam *poke, int disableOnly)
{
    BtlFieldPokeIter it;
    struct BtlPokeParam *cur;
    int k = BTL_POKEPARAM_GetPokeId(poke);
    BTL_SERVER_FLOW_DispatchPokeLeaveEvent(flow, poke);
    BTL_SERVER_FLOW_SkyDropReleaseTarget(flow, poke, 0);
    if (disableOnly == 0) {
        HAND_TOKUSEI_RemoveAbilityHandlers(poke);
        HAND_ITEM_RemoveUnforcedFactors(poke);
    } else {
        HAND_TOKUSEI_DisableAbilityHandlers(poke);
        HAND_ITEM_DisablePokeFactors(poke);
    }
    HAND_WAZA_RemoveAllMoveHandlers(poke);
    BTL_SERVER_FLOW_InitFieldPokeIter(&it, flow);
    BTL_SERVER_FLOW_SetFieldPokeIterRule(&it, flow);
    while (BTL_SERVER_FLOW_NextFieldPokeIter(&it, flow, &cur)) {
        BTL_POKEPARAM_CureSickBySource(cur, k);
        BTL_SERVER_CMD_Push(flow->cmdQueue, 0x2c, BTL_POKEPARAM_GetPokeId(cur), k);
    }
    BTL_FIELD_RemoveHolderFromStaticSlots(k);
    BTL_SERVER_CMD_Push(flow->cmdQueue, 0x23, k);
    if (!BTL_SERVER_FLOW_CheckShowdown(flow) && has_ability(poke, ABILITY_UNNERVE))
        BTL_SERVER_FLOW_RecheckOpponentAbilityEffects(flow, poke);
}

int BTL_SERVER_FLOW_CanUseHeldItem(BtlServerFlow *flow, int pokeId)
{
    struct BtlPokeParam *m;

    m = BTL_SIDE_GetPokeByIdThunk(flow->pokeCon, pokeId);
    if (has_ability(m, ABILITY_KLUTZ)) return 0;
    if (BTL_POKEPARAM_HasSick(m, 19)) return 0;
    if (BTL_FIELD_IsStaticSlotInUse(7) == 0) return 1;
    return 0;
}

/* The Ability Urge (item 611) runs the Pokemon's entry abilities again,
 * but not Imposter's (the game reads the ability itself, value 0x10). */
int BTL_SERVER_FLOW_ItemEffectTriggerEntryEvent(BtlServerFlow *flow, struct BtlPokeParam *poke)
{
    u32 s;
    int r;

    if (!bw_bh_ability_is(BTL_POKEPARAM_GetValue(poke, BPP_VALUE_ABILITY), ABILITY_IMPOSTER)) {
        s = BTL_SERVER_FLOW_BeginHandExScope(&flow->cmdLock, SRC_LINE(1921));
        BTL_SERVER_FLOW_DispatchPokeEntryEventToType4Thunk(flow, poke);
        r = BTL_SERVER_FLOW_GetLockStateThunk(flow);
        BTL_SERVER_FLOW_EndHandExScope((u32 *)&flow->cmdLock, s, SRC_LINE(1926));
        if (r == 2)
            return 1;
    }
    return 0;
}

/* Before the turn's choices: the points each side gets for the Wonder
 * Launcher (bagMode 1).  With the test switch EIDOS_DEBUG_LAUNCHER on, any
 * battle takes the launcher's rules from its first turn. */
int BTL_SERVER_FLOW_RunBagModeSetup(BtlServerFlow *flow)
{
    int result = 0;
    u32 *out = flow->cmdQueue;

    if (bw_bh_test_launcher_item() != 0) {
        flow->bagMode = 1;
    }
    out[0] = 0;
    out[1] = 0;
    BTL_SERVER_FLOW_PushBagModePositions(flow);
    if (*(u32 *)flow->cmdQueue)
        result = 1;
    return result;
}

/* The hits a move makes.  A move that hits once asks too when the
 * attacker's ability works like the eidos base parental_bond (its handler
 * makes it two; patch_ov093_hand_tokusei.c); with no such ability this is
 * the game's. */
int BTL_SERVER_FLOW_CalcMultiHitCount(BtlServerFlow *flow, struct BtlPokeParam *attacker, u32 moveId, u8 *hitInfo)
{
    u8 id = WAZA_TOOL_GetParam(moveId, 8);
    u32 r;

    hitInfo[1] = 0;
    hitInfo[5] = 3;
    if (id > 1) {
        r = BTL_CALC_RollHitCount(id);
        BTL_EVENT_PushVarFrame(SRC_LINE(12432));
        BTL_EVENT_SetVarReadOnly(3, BTL_POKEPARAM_GetPokeId(attacker));
        BTL_EVENT_SetVarReadOnly(0x29, id);
        BTL_EVENT_SetVar(0x2a, r);
        BTL_EVENT_SetVarOnce(0x51, 0);
        BTL_EVENT_SetVarOnce(0x42, 0);
        BTL_EVENT_DispatchEventChecked(flow, 0x35);
        if (id <= 5 && BTL_EVENT_GetVar(0x51) != 0) {
            hitInfo[0] = id;
            hitInfo[2] = 0;
        } else {
            hitInfo[0] = BTL_EVENT_GetVar(0x2a);
            hitInfo[2] = BTL_EVENT_GetVar(0x42);
        }
        hitInfo[3] = 1;
        BTL_EVENT_PopVarFrame(SRC_LINE(12455));
        return 1;
    }
    if (has_ability(attacker, NAMED_PARENTAL_BOND)) {
        BTL_EVENT_PushVarFrame(SRC_LINE(12432));
        BTL_EVENT_SetVarReadOnly(3, BTL_POKEPARAM_GetPokeId(attacker));
        BTL_EVENT_SetVarReadOnly(0x12, moveId);
        BTL_EVENT_SetVarReadOnly(0x29, 1);
        BTL_EVENT_SetVar(0x2a, 1);
        BTL_EVENT_DispatchEventChecked(flow, 0x35);
        r = (u32)BTL_EVENT_GetVar(0x2a);
        BTL_EVENT_PopVarFrame(SRC_LINE(12455));
        if (r > 1) {
            hitInfo[0] = (u8)r;
            hitInfo[2] = 0;             /* (no accuracy roll for the later hits) */
            hitInfo[3] = 1;
            return 1;
        }
    }
    hitInfo[0] = 1;
    hitInfo[2] = 0;
    hitInfo[3] = 0;
    return 0;
}

/* eidos's own event variable (the game's keys end below 0x60): the item a
 * Pokemon used up, in the frame of the item-gone event (0x9D); 0 when its
 * item was taken or changed.  The eidos ability base symbiosis reads it
 * (patch_ov093_hand_tokusei.c). */
#define EVAR_USED_UP_ITEM 0x100

extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern void BTL_POKEPARAM_SetItem(struct BtlPokeParam *pp, u16 item);
extern void BTL_POKEPARAM_ConsumeHeldItem(struct BtlPokeParam *poke, u16 itemId);
extern void BTL_SERVER_FLOW_NotifyHeldItemSetEvent(BtlServerFlow *flow, struct BtlPokeParam *poke, u32 itemId);
extern void BTL_SERVER_FLOW_SetContFlag(BtlServerFlow *flow, struct BtlPokeParam *poke, u32 flag);
extern void BTL_SERVER_FLOW_SetTurnFlag(BtlServerFlow *flow, struct BtlPokeParam *poke, unsigned int flag);
extern struct BtlEventFactor *HAND_ITEM_AddHeldItemFactor(struct BtlPokeParam *poke);

/* BTL_SERVER_FLOW_NotifyHeldItemClearedEvent with the used-up item. */
static void notify_held_item_cleared(BtlServerFlow *flow, struct BtlPokeParam *poke, u16 usedUp)
{
    int line = SRC_LINE(13050);

    BTL_EVENT_PushVarFrame(line);
    BTL_EVENT_SetVarReadOnly(2, BTL_POKEPARAM_GetPokeId(poke));
    BTL_EVENT_SetVarReadOnly(EVAR_USED_UP_ITEM, usedUp);
    BTL_EVENT_DispatchEventChecked(flow, 157);
    BTL_EVENT_PopVarFrame(line + 3);
}

/* A Pokemon's held item changes (`consumeOld`: it used its old one up).
 * The game's, with the used-up item in the item-gone event. */
void BTL_SERVER_FLOW_SetHeldItem(BtlServerFlow *flow, struct BtlPokeParam *pokeParam, u16 itemId, u32 consumeOld)
{
    u32 s;
    u16 t;
    int name;
    name = BTL_POKEPARAM_GetPokeId(pokeParam);
    t = (u16)(int)BTL_POKEPARAM_GetItem(pokeParam);
    s = BTL_SERVER_FLOW_BeginHandExScope(&flow->cmdLock, SRC_LINE(7469));
    BTL_SERVER_FLOW_NotifyHeldItemSetEvent(flow, pokeParam, itemId);
    BTL_SERVER_FLOW_EndHandExScope(&flow->cmdLock, s, SRC_LINE(7471));
    if (itemId == 0)
        BTL_SERVER_FLOW_SetContFlag(flow, pokeParam, 15);
    HAND_ITEM_RemoveUnforcedFactors(pokeParam);
    BTL_SERVER_CMD_Push(flow->cmdQueue, 0x1E, name, itemId);
    BTL_POKEPARAM_SetItem(pokeParam, itemId);
    if (itemId != 0)
        HAND_ITEM_AddHeldItemFactor(pokeParam);
    s = BTL_SERVER_FLOW_BeginHandExScope(&flow->cmdLock, SRC_LINE(7485));
    notify_held_item_cleared(flow, pokeParam, consumeOld != 0 ? t : 0);
    BTL_SERVER_FLOW_EndHandExScope(&flow->cmdLock, s, SRC_LINE(7487));
    if (consumeOld != 0) {
        BTL_POKEPARAM_ConsumeHeldItem(pokeParam, t);
        BTL_SERVER_CMD_Push(flow->cmdQueue, 0x17, name, t);
        BTL_SERVER_FLOW_SetTurnFlag(flow, pokeParam, 8);
    }
}
