/*
 * patches: src/game/ov093/hand_tokusei.c HAND_TOKUSEI_AddAbilityHandlers HAND_TOKUSEI_PinchBoostType HAND_TOKUSEI_AbsorbCheckMoveType HAND_TOKUSEI_CheckAllyHasAbility HAND_TOKUSEI_MoldBreaker_ShowAbilityMessage HAND_TOKUSEI_MoldBreaker_SkipCheck HAND_TOKUSEI_ShadowTag_PreventEscape HAND_TOKUSEI_MummySpreadAbility
 * keeps declarations
 *
 * (The unit passes HAND_TOKUSEI_MoldBreaker_SkipCheck to
 * BTL_EVENT_SetFactorSkipCheck, so its prototypes stay in the unit.)
 *
 * The handlers that test another Pokemon's ability by its id ask whether
 * it works like that ability (bw_bh_ability_is): Plus and Minus look for a
 * partner, Shadow Tag spares a foe of its own kind, Mummy does not spread
 * to a Mummy, Mold Breaker skips the abilities on its list.  An added
 * Mummy spreads itself.  Mold Breaker's entry line follows the base of the
 * running row (an added Turboblaze says Turboblaze's line).
 *
 * HAND_TOKUSEI_PinchBoostType (Blaze, Torrent, Overgrow, Swarm) and
 * HAND_TOKUSEI_AbsorbCheckMoveType (Volt Absorb, Water Absorb and the
 * like) are given the type they test: a mod's `type` for the running
 * ability takes its place (bw_bh_type).  Otherwise they are the game's own.
 *
 * A mod's ability that works like a built-in one (sdk/battle_handlers.h,
 * eidos/tables/ability_handlers.txt): the registry is searched for the
 * base ability's handlers, and the factor is added under the poke's own
 * ability id, so the battle knows which ability runs and the keys of that
 * id apply (bw_bh_rank and the others).  An ability with no table line is
 * its own base: the function then does what the game's does.
 *
 * A base may also be one of eidos's own (`165 = pixilate`), for later
 * abilities and for building blocks a modder sets with keys: their
 * handlers are at the end of this file, made of the game's own event
 * machinery as the handlers of the closest abilities are.
 *
 * symbols:
 *   BTL_POKEPARAM_GetValue              0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId             0x021D5440  thumb  ov093
 *   BTL_EVENT_AddFactor                 0x021D6F0C  thumb  ov093
 *   gAbilityHandlerRegistry             0x021F0E74  data   ov093
 *   BTL_ID_IsAbilityIgnoredByMoldBreaker 0x0689C9B0 thumb  ov095
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlPokeParam;
struct BtlEventFactor;

typedef struct {
    int eventId;
    void (*handler)(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work);
} BtlEventHandler;

/* btl_action_word_types.h */
typedef struct {
    u32 abilityId;
    BtlEventHandler *(*getHandlers)(u32 *packedInfo);
} AbilityHandlerEntry;

#define ABILITY_REGISTRY_COUNT 0x9E
#define BTL_EVENT_TYPE_ABILITY 4

extern AbilityHandlerEntry gAbilityHandlerRegistry[ABILITY_REGISTRY_COUNT];

extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *bpp, int which);
extern int BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *bpp);
extern struct BtlEventFactor *BTL_EVENT_AddFactor(int factorType, u32 key, int priority, u32 subPriority, u8 ownerId,
                                                  const BtlEventHandler *handlers, u16 handlerCount);
u32 HAND_TOKUSEI_GetPokeSpeed(struct BtlPokeParam *bpp);
int HAND_TOKUSEI_UnpackAbilityPriority(u32 *packedInfo);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);
extern const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                              u32 *total);
extern int bw_bh_type(int type);

struct BtlServerFlow;
typedef __INTPTR_TYPE__ sptr;   /* (types.h: an address-sized integer off the DS) */

/* btl_event_types.h, hand_tokusei_types.h */
#define BTL_EVAR_ATTACKER_ID     0x03
#define BTL_EVAR_DEFENDER_ID     0x04
#define BTL_EVAR_MOVE_TYPE       0x16
#define BTL_EVAR_RATIO           0x35
#define BTL_EVAR_NO_EFFECT_FLAG  0x40
#define BTL_RATIO_1_5            0x1800

extern sptr BTL_EVENT_GetVar(int key);
extern int BTL_EVENT_OverwriteVar(int key, __UINTPTR_TYPE__ value);
extern void BTL_EVENT_MultiplyVar(int key, int multiplier);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern u32 BTL_CALC_DivideMaxHp(struct BtlPokeParam *bpp, u32 divisor);

/* An ability that works like several (`165 = 22 + 26`): one factor whose
 * table is the bases' tables joined in line order (sdk/battle_handlers.c
 * keeps it and knows which base each row came from), with the first
 * found base's priority. */
static const BtlEventHandler *named_ability_handlers(u32 base, u32 *count);

#define NAMED_ABILITY_PRIORITY 5        /* (ABILITY_DEFAULT_PRIORITY: what an ability with none packed gets) */

static struct BtlEventFactor *add_joined_abilities(struct BtlPokeParam *bpp, u16 abilityId, int parts)
{
    const void *tables[4];
    u32 counts[4], total = 0, i;
    int k, priority = -1;
    const void *joined;

    for (k = 0; k < parts && k < 4; k++) {
        u32 base = (u32)bw_bh_part_base(BTL_EVENT_TYPE_ABILITY, abilityId, k);

        tables[k] = named_ability_handlers(base, &counts[k]);
        if (tables[k] != 0) {
            if (priority < 0) {
                priority = NAMED_ABILITY_PRIORITY;
            }
            continue;
        }
        counts[k] = 0;
        for (i = 0; base != 0 && i < ABILITY_REGISTRY_COUNT; i++) {
            if (base == gAbilityHandlerRegistry[i].abilityId) {
                u32 packed;
                int p;

                tables[k] = gAbilityHandlerRegistry[i].getHandlers(&packed);
                p = HAND_TOKUSEI_UnpackAbilityPriority(&packed);
                counts[k] = packed;
                if (priority < 0) {
                    priority = p;
                }
                break;
            }
        }
    }
    if (priority < 0) {
        return 0;
    }
    joined = bw_bh_join(BTL_EVENT_TYPE_ABILITY, abilityId, k, tables, counts, sizeof(BtlEventHandler), &total);
    if (joined == 0 || total == 0) {
        return 0;
    }
    return BTL_EVENT_AddFactor(BTL_EVENT_TYPE_ABILITY, abilityId, priority, HAND_TOKUSEI_GetPokeSpeed(bpp),
                               BTL_POKEPARAM_GetPokeId(bpp), (const BtlEventHandler *)joined, (u16)total);
}

/* Adds the handler table of the ability that the poke holds to the event factor list. */
struct BtlEventFactor *HAND_TOKUSEI_AddAbilityHandlers(struct BtlPokeParam *bpp)
{
    u16 abilityId = BTL_POKEPARAM_GetValue(bpp, 0x10);
    int parts = bw_bh_parts(BTL_EVENT_TYPE_ABILITY, abilityId);
    u32 base = (u32)bw_bh_base(BTL_EVENT_TYPE_ABILITY, abilityId);
    u32 i;
    u32 speed;
    u8 pokeId;
    int priority;
    u32 handlerCount;
    BtlEventHandler *handlers;

    if (parts > 1) {
        return add_joined_abilities(bpp, abilityId, parts);
    }
    if ((handlers = (BtlEventHandler *)named_ability_handlers(base, &handlerCount)) != 0) {
        /* (eidos's own: no getter, the default priority) */
        return BTL_EVENT_AddFactor(BTL_EVENT_TYPE_ABILITY, abilityId, NAMED_ABILITY_PRIORITY, HAND_TOKUSEI_GetPokeSpeed(bpp),
                                   BTL_POKEPARAM_GetPokeId(bpp), handlers, (u16)handlerCount);
    }
    for (i = 0; i < ABILITY_REGISTRY_COUNT; i++) {
        if (base == gAbilityHandlerRegistry[i].abilityId) {
            speed = HAND_TOKUSEI_GetPokeSpeed(bpp);
            pokeId = BTL_POKEPARAM_GetPokeId(bpp);
            handlers = gAbilityHandlerRegistry[i].getHandlers(&handlerCount);
            priority = HAND_TOKUSEI_UnpackAbilityPriority(&handlerCount);
            return BTL_EVENT_AddFactor(BTL_EVENT_TYPE_ABILITY, abilityId, priority, speed, pokeId, handlers, handlerCount);
        }
    }
    return 0;
}

/* Blaze, Torrent, Overgrow and Swarm: moves of the given type do 1.5 times damage when the owner has a third of its HP or less. */
void HAND_TOKUSEI_PinchBoostType(struct BtlServerFlow *flow, int pokeId, int moveType)
{
    struct BtlPokeParam *bpp;
    u32 hpThreshold;

    moveType = bw_bh_type(moveType);    /* (a mod's `type` for the running ability) */
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        bpp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
        hpThreshold = BTL_CALC_DivideMaxHp(bpp, 3);
        if (BTL_POKEPARAM_GetValue(bpp, 13) <= hpThreshold) {
            if (moveType == BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE))
                BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, BTL_RATIO_1_5);
        }
    }
}

int HAND_TOKUSEI_AbsorbCheckMoveType(struct BtlServerFlow *flow, int pokeId, int moveType)
{
    moveType = bw_bh_type(moveType);    /* (a mod's `type` for the running ability) */
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && pokeId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && moveType == BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE))
        return BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The game's tests of an ability by number                            */
/* ------------------------------------------------------------------ */

/* sdk/battle_handlers.c: whether an ability works like a built-in one, and
 * the base of the running or checked row of a factor */
extern int bw_bh_ability_is(int ability, int builtin);
extern int bw_bh_running_base(int type, int id);
extern int bw_bh_checked_base(int type, int id);

/* hand_common_types.h: a message the flow shows */
typedef struct BtlStrParam {
    u16 strId;
    u16 typeAndCount;       /* strType, argCount, argFlag */
    u32 args[8];
} BtlStrParam;

extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern int BTL_SERVER_FLOW_GetPokeViewPos(struct BtlServerFlow *flow, int pokeId);
extern u8 BTL_SERVER_FLOW_CollectAlivePokeIds(struct BtlServerFlow *flow, int code, u8 *pokeIds);
extern int BTL_SERVER_FLOW_GetFrontPosOfPoke(struct BtlServerFlow *flow, int pokeId);
extern int BTL_SERVER_FLOW_CheckShowdownThunk(struct BtlServerFlow *flow);
extern void BTL_SERVER_FLOW_PushCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern u16 BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern int BTL_MAIN_IsPokeSamePair(u8 a, u8 b);
extern u32 WAZA_TOOL_GetFlag(u16 moveId, u32 flagBit);
extern int BTL_ID_IsAbilityIgnoredByMoldBreaker(unsigned abilityId);

/* hand_tokusei_types.h, hand_common_types.h */
#define BTL_EVAR_POKE_ID              0x02
#define BTL_EVAR_MOVE_ID              0x12
#define BTL_EVAR_FORCE_GROUNDED_FLAG  0x41
#define BTL_EVAR_HIT_SUBSTITUTE_FLAG  0x46
#define BPP_VALUE_ABILITY             0x10
#define BPP_VALUE_CURRENT_ABILITY     0x11      /* the ability, 0 while Gastro Acid holds it */
#define HANDEX_TOKWIN_IN              2
#define HANDEX_TOKWIN_OUT             3
#define HANDEX_MESSAGE                4
#define BTL_CMD_KIND_ABILITY_CHANGE   0x1f
#define BTL_CMD_FLAG_FROM_ABILITY     (1u << 23)
#define BTL_STR_TYPE_WITH_POKE_NAME   2
#define BTL_STR_MUMMY_SPREAD          0x1cf
#define WAZA_FLAG_CONTACT             0
#define OPPONENT_ID_BUFFER_SIZE       4

#define ABILITY_SHADOW_TAG            0x17
#define ABILITY_PLUS                  0x39
#define ABILITY_MINUS                 0x3a
#define ABILITY_MUMMY                 152
#define ABILITY_TURBOBLAZE            0xa3
#define ABILITY_TERAVOLT              0xa4

/* hand_common_types.h: HANDEX_KIND_MESSAGE */
typedef struct HandExMessage {
    u32 header;
    BtlStrParam message;
} HandExMessage;

/* hand_tokusei_types.h: command kind 0x1f, give a Pokemon a new ability */
typedef struct {
    u32 header;
    u16 abilityId;
    u8 pokeId;
    u8 pad07[5];
    BtlStrParam strParam;
} BtlAbilityChangeCmd;

/* Collects the pokemon on the side of pokeId into idBuf; true if another one has ability abilityId
 * (Plus and Minus: one that works like it). */
int HAND_TOKUSEI_CheckAllyHasAbility(void *server, int pokeId, u8 *idBuf, int abilityId)
{
    int side;
    u8 count;
    u8 i;

    side = BTL_SERVER_FLOW_GetPokeViewPos(server, pokeId);
    if (side != 6) {
        count = BTL_SERVER_FLOW_CollectAlivePokeIds(server, (u16)((7 << 8) | side), idBuf);
        for (i = 0; i < count; i++) {
            if (pokeId != idBuf[i]) {
                if (bw_bh_ability_is(BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(server, idBuf[i]), BPP_VALUE_CURRENT_ABILITY),
                             abilityId))
                    return 1;
            }
        }
    }
    return 0;
}

/* Mold Breaker, Turboblaze, Teravolt: when the poke enters, show the message of its own ability (the
 * one its row came from). */
void HAND_TOKUSEI_MoldBreaker_ShowAbilityMessage(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    HandExMessage *msg;
    int strId;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        switch (bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor))) {
        case ABILITY_TURBOBLAZE:
            strId = 0x1f9;
            break;
        case ABILITY_TERAVOLT:
            strId = 0x1f6;
            break;
        default:
            strId = 0x1ba;
            break;
        }
        BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_IN, pokeId);
        msg = BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_MESSAGE, pokeId);
        BTL_SERVER_FLOW_InitMsgRecord(&msg->message, 2, strId);
        BTL_SERVER_FLOW_AppendMsgRecordArg((&msg->message), pokeId);
        BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
        BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_OUT, pokeId);
    }
}

/* Skip check of the holder: ability factors listed as breakable are skipped.  For an ability of several
 * bases, the base whose row the dispatch is about to run (BTL_EVENT_DispatchToFactors).  eidos's own
 * bases that guard the holder are breakable too, as in the later games. */
static int named_ability_breakable(unsigned base);

int HAND_TOKUSEI_MoldBreaker_SkipCheck(struct BtlEventFactor *other, void *context, int factorType, int eventId, u16 key, u8 ownerId)
{
    unsigned base;

    (void)other;
    (void)context;
    (void)eventId;
    (void)ownerId;

    if (factorType != BTL_EVENT_TYPE_ABILITY)
        return 0;
    base = (unsigned)bw_bh_checked_base(BTL_EVENT_TYPE_ABILITY, key);
    if (named_ability_breakable(base) || BTL_ID_IsAbilityIgnoredByMoldBreaker(base))
        return 1;
    return 0;
}

/* Shadow Tag: opposing pokes cannot escape, unless one of them has Shadow Tag too (or one that works like it). */
void HAND_TOKUSEI_ShadowTag_PreventEscape(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    u8 opponentIds[OPPONENT_ID_BUFFER_SIZE];
    u32 opponentCount;
    u8 i;

    (void)factor;
    if (BTL_MAIN_IsPokeSamePair((u8)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID), (u8)pokeId) == 0) {
        opponentCount = BTL_SERVER_FLOW_CollectAlivePokeIds(flow, (u16)(BTL_SERVER_FLOW_GetFrontPosOfPoke(flow, pokeId) | 0x100), opponentIds);
        for (i = 0; i < opponentCount; i++) {
            if (bw_bh_ability_is(BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, opponentIds[i]), BPP_VALUE_CURRENT_ABILITY),
                         ABILITY_SHADOW_TAG))
                return;
        }
        BTL_EVENT_OverwriteVar(BTL_EVAR_FORCE_GROUNDED_FLAG, 1);
    }
}

/*
 * Mummy: when a contact move hits the owner, the attacker's ability becomes
 * Mummy, unless it already works like Mummy or one side has no Pokemon left.
 * The new ability is the owner's own (an added Mummy spreads itself).
 */
void HAND_TOKUSEI_MummySpreadAbility(struct BtlEventFactor *factor, struct BtlServerFlow *flow, u8 ownerId, void *work)
{
    BtlAbilityChangeCmd *cmd;
    u8 attackerId;

    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && BTL_EVENT_GetVar(BTL_EVAR_HIT_SUBSTITUTE_FLAG) == 0 && BTL_SERVER_FLOW_CheckShowdownThunk(flow) == 0
        && WAZA_TOOL_GetFlag((u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), WAZA_FLAG_CONTACT) != 0) {
        attackerId = BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID);
        if (!bw_bh_ability_is(BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, attackerId), BPP_VALUE_ABILITY),
                      ABILITY_MUMMY)) {
            cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, BTL_CMD_KIND_ABILITY_CHANGE, ownerId);
            /* (an added Mummy spreads itself; one of several bases spreads
             * Mummy alone, not the line's other bases) */
            cmd->abilityId = bw_bh_parts(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor)) == 1
                ? BTL_EVENT_GetFactorKey(factor) : ABILITY_MUMMY;
            cmd->pokeId = attackerId;
            BTL_SERVER_FLOW_InitMsgRecord(&cmd->strParam, BTL_STR_TYPE_WITH_POKE_NAME, BTL_STR_MUMMY_SPREAD);
            BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->strParam), cmd->pokeId);
            if (BTL_MAIN_IsPokeSamePair(ownerId, attackerId) == 0)
                cmd->header |= BTL_CMD_FLAG_FROM_ABILITY;
            BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
        }
    }
}

/* ------------------------------------------------------------------ */
/* eidos's own ability bases (sdk/battle_handlers.h BW_BH_ABILITY_*)   */
/* ------------------------------------------------------------------ */

/*
 * Later abilities Black and White have no handlers for, made of the game's
 * own event machinery as the handlers of the closest abilities are
 * (Normalize, Technician, Iron Fist, Marvel Scale, Multiscale, Soundproof,
 * Prankster, Moxie, Justified, Insomnia, Clear Body, Rain Dish, Color
 * Change, Pickpocket).  Their numbers are keys (bw_bh_arg), with the later
 * games' values as the defaults below.  Their lines are the game's own:
 * the ability pop-up and the line the closest ability shows.
 */

#define NAMED_ABILITY_FIRST  0x10100
#define NAMED_PIXILATE       0x10100
#define NAMED_AERILATE       0x10101
#define NAMED_REFRIGERATE    0x10102
#define NAMED_GALVANIZE      0x10103
#define NAMED_BOOST_KIND     0x10104
#define NAMED_TOUGH_CLAWS    0x10105
#define NAMED_STRONG_JAW     0x10106
#define NAMED_MEGA_LAUNCHER  0x10107
#define NAMED_BOOST_TYPE     0x10108
#define NAMED_GUARD_KIND     0x10109
#define NAMED_FUR_COAT       0x1010A
#define NAMED_BLOCK_KIND     0x1010B
#define NAMED_BULLETPROOF    0x1010C
#define NAMED_PRIORITY_TYPE  0x1010D
#define NAMED_GALE_WINGS     0x1010E
#define NAMED_ENTRY_STAT     0x1010F
#define NAMED_KO_STAT        0x10110
#define NAMED_HIT_TYPE_STAT  0x10111
#define NAMED_SWEET_VEIL     0x10112
#define NAMED_AROMA_VEIL     0x10113
#define NAMED_FLOWER_VEIL    0x10114
#define NAMED_CHEEK_POUCH    0x10115
#define NAMED_PROTEAN        0x10116
#define NAMED_MAGICIAN       0x10117
#define NAMED_SYMBIOSIS      0x10118
#define NAMED_PARENTAL_BOND  0x10119
#define NAMED_STANCE_CHANGE  0x1011A
#define NAMED_GRASS_PELT     0x1011B
#define NAMED_ABILITY_END    0x1011C

/* the kinds of move a `kind` key names (sdk/battle_handlers.c k_kinds) */
#define KIND_CONTACT   1
#define KIND_PUNCH     2
#define KIND_SOUND     3
#define KIND_BITE      4
#define KIND_PULSE     5
#define KIND_BALL      6
#define KIND_POWDER    7
#define KIND_PHYSICAL  8
#define KIND_SPECIAL   9
#define KIND_STATUS    10
#define KIND_ONE_HIT   11

/* sdk/move_kinds.h */
#define MOVE_KIND_BITE    0x01u
#define MOVE_KIND_PULSE   0x02u
#define MOVE_KIND_BALL    0x04u
#define MOVE_KIND_POWDER  0x08u
#define MOVE_KIND_ONE_HIT 0x10u

/* the move record's flags (WAZA_TOOL_GetFlag) and damage kinds */
#define WAZA_FLAG_CHARGE  1             /* a charge turn first (Sleep Talk does not call these) */
#define WAZA_FLAG_PUNCH   7
#define WAZA_FLAG_SOUND   8
#define WAZA_DAMAGE_STATUS   0
#define WAZA_DAMAGE_PHYSICAL 1
#define WAZA_DAMAGE_SPECIAL  2

/* the event variables these handlers use (hand_common_types.h) */
#define EVAR_DEFENDER_COUNT  0x05
#define EVAR_DEFENDER_FIRST  0x06
#define EVAR_MOVE_PRIORITY   0x18
#define EVAR_DAMAGE_TYPE     0x1A
#define EVAR_SICK_ID         0x1D
#define EVAR_VOLUME          0x20
#define EVAR_NEW_ITEM_ID     0x2D
#define EVAR_POWER_RATIO     0x31
#define EVAR_SHEER_FORCE     0x4D

/* the commands (hand_common_types.h, hand_tokusei_types.h) */
#define CMD_RECOVER_HP       5
#define CMD_RANK_CHANGE      14
#define CMD_CHANGE_TYPE      0x14
#define CMD_STEAL_ITEM       0x24
#define CMD_FLAG_ABILITY     (1u << 23)     /* the ability's pop-up goes with it */

#define POKETYPE_NORMAL      0
#define POKETYPE_STEEL       8
#define POKETYPE_WATER       10
#define POKETYPE_GRASS       11
#define POKETYPE_ICE         14
#define POKETYPE_ELECTRIC    12
#define POKETYPE_FLYING      2
#define POKETYPE_NONE        31             /* arm9/poke_type.h, off the DS */

#define SICK_SLEEP           2
#define SICK_INFATUATION     7
#define SICK_TAUNT           0x0B
#define SICK_TORMENT         0x0C
#define SICK_DISABLE         0x0D
#define SICK_YAWN            0x0E
#define SICK_HEAL_BLOCK      0x0F
#define SICK_ENCORE          0x17
#define SICK_MAJOR_LAST      5              /* paralysis, sleep, freeze, burn, poison */
#define POKE_ID_NONE         0x1F           /* a status from no Pokemon (BTL_SERVER_FLOW_QuerySickBlockedEvent) */

#define RATIO(x)             ((int)((x) * 4096.0 + 0.5))

/* the lines (a/0/0/2 bank 14, sets of three) */
#define MSG_NO_EFFECT        0xD2           /* "It doesn't affect X..." (Insomnia, Soundproof) */
#define MSG_STATS_KEPT       0xC6           /* Clear Body's line */
#define MSG_STOLE            0x1CC          /* "It stole X's Y!" (Pickpocket) */

typedef struct {
    u32 header;
    u32 statId;
    u32 pad08;
    signed char amount;
    u8 pad0D;
    u8 showMessage;
    u8 targetCount;
    u8 targets[1];
} RankCmd;

typedef struct {
    u32 flags;
    u16 amount;
    u8 pokeId;
} HpCmd;

typedef struct {
    u32 header;
    u16 typePair;
    u8 pokeId;
    u8 showMessage;
} ChangeTypeCmd;

typedef struct {
    u32 flags;
    u8 victimId;
    u8 pad05[3];
    BtlStrParam strParam;
} StealItemCmd;

extern int bw_bh_arg(const char *key, int index, int fallback);
extern u32 bw_move_kinds(u32 move);
extern int bw_type_by_name(const char *s, __SIZE_TYPE__ n);

extern u32 WAZA_TOOL_GetType(u16 moveId);
extern int WAZA_TOOL_GetDamageType(u16 moveId);
extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern u16 BTL_TYPEPAIR_MakeSingle(int type);
extern int ITEM_IsBerry(u32 item);
extern u32 BTL_CALC_DivideMaxHpMin1(struct BtlPokeParam *pp, u32 divisor);

/* Whether the move is of `kind`: a flag of its record, its damage kind, or
 * a kind of sdk/move_kinds.c. */
static int move_is_kind(u16 move, int kind)
{
    switch (kind) {
    case KIND_CONTACT:  return WAZA_TOOL_GetFlag(move, WAZA_FLAG_CONTACT) != 0;
    case KIND_PUNCH:    return WAZA_TOOL_GetFlag(move, WAZA_FLAG_PUNCH) != 0;
    case KIND_SOUND:    return WAZA_TOOL_GetFlag(move, WAZA_FLAG_SOUND) != 0;
    case KIND_BITE:     return (bw_move_kinds(move) & MOVE_KIND_BITE) != 0;
    case KIND_PULSE:    return (bw_move_kinds(move) & MOVE_KIND_PULSE) != 0;
    case KIND_BALL:     return (bw_move_kinds(move) & MOVE_KIND_BALL) != 0;
    case KIND_POWDER:   return (bw_move_kinds(move) & MOVE_KIND_POWDER) != 0;
    case KIND_ONE_HIT:  return (bw_move_kinds(move) & MOVE_KIND_ONE_HIT) != 0;
    case KIND_PHYSICAL: return WAZA_TOOL_GetDamageType(move) == WAZA_DAMAGE_PHYSICAL;
    case KIND_SPECIAL:  return WAZA_TOOL_GetDamageType(move) == WAZA_DAMAGE_SPECIAL;
    case KIND_STATUS:   return WAZA_TOOL_GetDamageType(move) == WAZA_DAMAGE_STATUS;
    }
    return 0;
}

/* The ability's pop-up, a line of a set (the Pokemon it names) and the pop-up's end. */
static void popup_line(struct BtlServerFlow *flow, int ownerId, int line, int pokeId)
{
    HandExMessage *msg;

    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_IN, ownerId);
    msg = BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_MESSAGE, ownerId);
    BTL_SERVER_FLOW_InitMsgRecord(&msg->message, BTL_STR_TYPE_WITH_POKE_NAME, line);
    BTL_SERVER_FLOW_AppendMsgRecordArg((&msg->message), (u32)pokeId);
    BTL_SERVER_FLOW_CommitCmdRecord(flow, msg);
    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_OUT, ownerId);
}

/* A stat change of the owner's own, with the pop-up (Moxie's command). */
static void raise_own_stat(struct BtlServerFlow *flow, u8 ownerId, int stat, int stages)
{
    RankCmd *cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_RANK_CHANGE, ownerId);

    cmd->header |= CMD_FLAG_ABILITY;
    cmd->targetCount = 1;
    cmd->targets[0] = ownerId;
    cmd->statId = (u32)stat;
    cmd->amount = (signed char)stages;
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

/* Whether `pokeId` is the owner or on the owner's side. */
static int is_own_side(u8 ownerId, int pokeId)
{
    return pokeId == ownerId || BTL_MAIN_IsPokeSamePair(ownerId, (u8)pokeId) != 0;
}

/* --- Pixilate, Aerilate, Refrigerate, Galvanize ------------------ */

/* The type the owner's Normal moves take: the key, else the base's own
 * (Pixilate's is the added type named "fairy" in types.txt; with none, the
 * moves keep their type). */
static int ate_type(int base)
{
    int fallback;

    switch (base) {
    case NAMED_AERILATE:    fallback = POKETYPE_FLYING; break;
    case NAMED_REFRIGERATE: fallback = POKETYPE_ICE; break;
    case NAMED_GALVANIZE:   fallback = POKETYPE_ELECTRIC; break;
    default:                fallback = bw_type_by_name("fairy", 5); break;
    }
    return bw_bh_arg("type", 1, fallback < 0 ? POKETYPE_NORMAL : fallback);
}

/* 0x28, the move's type: a Normal move of the owner's takes the type. */
static void ate_change_type(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int type = ate_type(bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor)));

    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) && BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == POKETYPE_NORMAL
        && type != POKETYPE_NORMAL) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_TYPE, (__UINTPTR_TYPE__)type);
    }
}

/* 0x38, the move's power: a move that was Normal and took the type has
 * `ratio` more power (1.2 as in the later games). */
static void ate_boost_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int type = ate_type(bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor)));

    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && type != POKETYPE_NORMAL
        && WAZA_TOOL_GetType((u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID)) == POKETYPE_NORMAL
        && BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == type) {
        BTL_EVENT_MultiplyVar(EVAR_POWER_RATIO, bw_bh_arg("ratio", 1, RATIO(1.2)));
    }
}

static const BtlEventHandler k_ate[] = {
    { 0x28, ate_change_type },
    { 0x38, ate_boost_power },
};

/* --- moves of a kind or a type hit harder ------------------------ */

static int boost_kind_default(int base)
{
    switch (base) {
    case NAMED_STRONG_JAW:    return KIND_BITE;
    case NAMED_MEGA_LAUNCHER: return KIND_PULSE;
    }
    return KIND_CONTACT;
}

/* 0x38: the owner's moves of the kind have `ratio` more power (Tough
 * Claws 1.3 for contact moves, Strong Jaw 1.5 for bites, Mega Launcher 1.5
 * for pulses). */
static void boost_kind_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int base = bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor));
    int kind = bw_bh_arg("kind", 1, boost_kind_default(base));

    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && move_is_kind((u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), kind)) {
        BTL_EVENT_MultiplyVar(EVAR_POWER_RATIO,
                              bw_bh_arg("ratio", 1, kind == KIND_CONTACT ? RATIO(1.3) : RATIO(1.5)));
    }
}

static const BtlEventHandler k_boost_kind[] = {
    { 0x38, boost_kind_power },
};

/* 0x38: the owner's moves of the type have `ratio` more power (Steelworker:
 * Steel, 1.5). */
static void boost_type_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)
        && BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE) == bw_bh_arg("type", 1, POKETYPE_STEEL)) {
        BTL_EVENT_MultiplyVar(EVAR_POWER_RATIO, bw_bh_arg("ratio", 1, RATIO(1.5)));
    }
}

static const BtlEventHandler k_boost_type[] = {
    { 0x38, boost_type_power },
};

/* --- the owner takes less damage --------------------------------- */

/* 0x47, the damage (Multiscale's step): moves of the kind do `ratio` of
 * their damage to the owner (Fluffy's contact half by default; Ice Scales
 * is kind=special). */
static void guard_kind_damage(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID)
        && move_is_kind((u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), bw_bh_arg("kind", 1, KIND_CONTACT))) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, bw_bh_arg("ratio", 1, RATIO(0.5)));
    }
}

static const BtlEventHandler k_guard_kind[] = {
    { 0x47, guard_kind_damage },
};

/* 0x3C, the defending stat (Marvel Scale's step): the owner's Defense is
 * `ratio` times as high against physical moves (2.0). */
static void fur_coat_defense(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && BTL_EVENT_GetVar(EVAR_DAMAGE_TYPE) == WAZA_DAMAGE_PHYSICAL) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, bw_bh_arg("ratio", 1, RATIO(2.0)));
    }
}

static const BtlEventHandler k_fur_coat[] = {
    { 0x3C, fur_coat_defense },
};

/* 0x2D (Soundproof's step): moves of the kind have no effect on the owner,
 * with the pop-up and Soundproof's line (Bulletproof: ball and bomb moves). */
static void block_kind_move(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID)
        && move_is_kind((u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), bw_bh_arg("kind", 1, KIND_BALL))
        && BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1) != 0) {
        popup_line((struct BtlServerFlow *)context, ownerId, MSG_NO_EFFECT, ownerId);
    }
}

static const BtlEventHandler k_block_kind[] = {
    { 0x2D, block_kind_move },
};

/* --- moves of a type go first ------------------------------------ */

/* 0x11, the move's priority (Prankster's step): the owner's moves of the
 * type get `priority` more (1); with `hp`, only while its HP is at least
 * that fraction of its maximum (Gale Wings: hp=1/1, full HP). */
static void priority_type_raise(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int base = bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor));
    int hp = bw_bh_arg("hp", 1, base == NAMED_GALE_WINGS ? 1 : 0);
    struct BtlPokeParam *owner;
    u16 move;

    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    move = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    if ((int)WAZA_TOOL_GetType(move) != bw_bh_arg("type", 1, POKETYPE_FLYING)) {
        return;
    }
    if (hp > 0) {
        owner = BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context, ownerId);
        if ((u32)BTL_POKEPARAM_GetValue(owner, 13) * (u32)hp < (u32)BTL_POKEPARAM_GetValue(owner, 14)) {
            return;                     /* (13 the HP, 14 its maximum) */
        }
    }
    BTL_EVENT_OverwriteVar(EVAR_MOVE_PRIORITY, (__UINTPTR_TYPE__)(BTL_EVENT_GetVar(EVAR_MOVE_PRIORITY)
                                                                  + bw_bh_arg("priority", 1, 1)));
}

static const BtlEventHandler k_priority_type[] = {
    { 0x11, priority_type_raise },
};

/* --- stat changes of the owner's own ----------------------------- */

/* 0x55 and 0x8A, the owner enters the battle (Mold Breaker's line, Klutz):
 * its stat changes by `stages` (Intrepid Sword: Attack +1). */
static void entry_stat_change(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        raise_own_stat((struct BtlServerFlow *)context, ownerId, bw_bh_arg("stat", 1, 1), bw_bh_arg("stages", 1, 1));
    }
}

static const BtlEventHandler k_entry_stat[] = {
    { 0x55, entry_stat_change },
    { 0x8A, entry_stat_change },
};

/* 0x83 (Moxie's step): the owner's stat changes by `stages` for each
 * Pokemon its move knocked out (Chilling Neigh: Attack +1). */
static void ko_stat_change(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 count, i;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    count = (u32)BTL_EVENT_GetVar(EVAR_DEFENDER_COUNT);
    for (i = 0; i < count; i++) {
        if (BTL_POKEPARAM_IsFainted(BTL_SERVER_FLOW_GetPokeParam(flow, (u8)BTL_EVENT_GetVar(EVAR_DEFENDER_FIRST + (int)i)))) {
            raise_own_stat(flow, ownerId, bw_bh_arg("stat", 1, 1), bw_bh_arg("stages", 1, 1));
        }
    }
}

static const BtlEventHandler k_ko_stat[] = {
    { 0x83, ko_stat_change },
};

/* 0x4B (Justified's step): hit by a move of the type (or `type.2`), the
 * owner's stat changes by `stages` (Water Compaction: Water, Defense +2). */
static void hit_type_stat_change(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int type = (int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE);

    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && BTL_EVENT_GetVar(BTL_EVAR_HIT_SUBSTITUTE_FLAG) == 0
        && (type == bw_bh_arg("type", 1, POKETYPE_WATER) || type == bw_bh_arg("type", 2, -1))) {
        raise_own_stat((struct BtlServerFlow *)context, ownerId, bw_bh_arg("stat", 1, 2), bw_bh_arg("stages", 1, 2));
    }
}

static const BtlEventHandler k_hit_type_stat[] = {
    { 0x4B, hit_type_stat_change },
};

/* --- veils: the owner and its side are safe from some statuses ---- */

/* the factor's work slots (BtlEventFactor.work), a pair for each veil, so
 * that one line may join two veils: Sweet Veil 0 and 1, Aroma Veil 2 and 3,
 * Flower Veil 4 and 5 */
#define WORK_BLOCKED   0                /* a status was just held off: the line is owed */
#define WORK_TARGET    1                /* the Pokemon it was held off */

/* The running veil's pair of work slots. */
static u32 *veil_work(struct BtlEventFactor *factor, void *work)
{
    switch (bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor))) {
    case NAMED_AROMA_VEIL:  return (u32 *)work + 2;
    case NAMED_FLOWER_VEIL: return (u32 *)work + 4;
    }
    return (u32 *)work;
}

/* Whether the veil holds off `sick` for `pokeId`. */
static int veil_holds(int base, struct BtlServerFlow *flow, u8 ownerId, int pokeId, int sick, int attackerId)
{
    if (!is_own_side(ownerId, pokeId)) {
        return 0;
    }
    switch (base) {
    case NAMED_SWEET_VEIL:
        return sick == SICK_SLEEP || sick == SICK_YAWN;
    case NAMED_AROMA_VEIL:
        return sick == SICK_INFATUATION || sick == SICK_TAUNT || sick == SICK_TORMENT || sick == SICK_DISABLE
            || sick == SICK_HEAL_BLOCK || sick == SICK_ENCORE;
    case NAMED_FLOWER_VEIL:
        return sick >= 1 && sick <= SICK_MAJOR_LAST && attackerId != POKE_ID_NONE && !is_own_side(ownerId, attackerId)
            && BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId), bw_bh_arg("type", 1, POKETYPE_GRASS));
    }
    return 0;
}

/* 0x65, a status is about to be added (Insomnia's step). */
static void veil_block_sick(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = veil_work(factor, work);
    int base = bw_bh_running_base(BTL_EVENT_TYPE_ABILITY, BTL_EVENT_GetFactorKey(factor));
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);

    if (veil_holds(base, (struct BtlServerFlow *)context, ownerId, defenderId, (int)BTL_EVENT_GetVar(EVAR_SICK_ID),
                   (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID))
        && BTL_EVENT_OverwriteVar(BTL_EVAR_FORCE_GROUNDED_FLAG, 1) != 0) {
        w[WORK_BLOCKED] = 1;
        w[WORK_TARGET] = (u32)defenderId;
    }
}

/* 0x67, after a status was held off: the pop-up and Insomnia's line, once. */
static void veil_notice(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = veil_work(factor, work);

    if (w[WORK_BLOCKED] == 1) {
        popup_line((struct BtlServerFlow *)context, ownerId, MSG_NO_EFFECT, (int)w[WORK_TARGET]);
        w[WORK_BLOCKED] = 0;
    }
}

/* 0x0E, a Pokemon's own status move on itself (Insomnia's Rest step):
 * Sweet Veil keeps its side from Rest. */
static void sweet_veil_block_rest(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)context;
    (void)work;
    if (is_own_side(ownerId, (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID))) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FORCE_GROUNDED_FLAG, 1);
    }
}

static const BtlEventHandler k_sweet_veil[] = {
    { 0x65, veil_block_sick },
    { 0x67, veil_notice },
    { 0x0E, sweet_veil_block_rest },
};

/* the moves that give the conditions Aroma Veil holds off */
static const u16 k_aroma_veil_moves[] = {
    50,     /* Disable */
    213,    /* Attract */
    227,    /* Encore */
    259,    /* Torment */
    269,    /* Taunt */
    377,    /* Heal Block */
};

extern int bw_bh_move_in(int move, const u16 *list, int count);

/* 0x2D, the move's no-effect check (Oblivious's step against Captivate):
 * one of those moves (or a mod's copy) aimed at the owner's side has no
 * effect, with the pop-up and "It doesn't affect X...".  The status check
 * (0x65) still holds off the same conditions from other sources. */
static void aroma_veil_block_move(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    int defenderId = (int)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);

    (void)factor;
    (void)work;
    if (is_own_side(ownerId, defenderId)
        && bw_bh_move_in((int)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID), k_aroma_veil_moves,
                         (int)(sizeof k_aroma_veil_moves / sizeof k_aroma_veil_moves[0]))
        && BTL_EVENT_OverwriteVar(BTL_EVAR_NO_EFFECT_FLAG, 1) != 0) {
        popup_line((struct BtlServerFlow *)context, ownerId, MSG_NO_EFFECT, defenderId);
    }
}

static const BtlEventHandler k_aroma_veil[] = {
    { 0x65, veil_block_sick },
    { 0x67, veil_notice },
    { 0x2D, aroma_veil_block_move },
};

/* 0x5B, a stat is about to drop (Clear Body's step): a Pokemon of the
 * type on the owner's side keeps it, unless it lowers its own. */
static void flower_veil_block_drop(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = veil_work(factor, work);
    int pokeId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    int attackerId = (int)BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID);

    if (is_own_side(ownerId, pokeId) && !is_own_side(ownerId, attackerId) && BTL_EVENT_GetVar(EVAR_VOLUME) < 0
        && BTL_POKEPARAM_HasType(BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context, pokeId),
                                 bw_bh_arg("type", 1, POKETYPE_GRASS))
        && BTL_EVENT_OverwriteVar(BTL_EVAR_FORCE_GROUNDED_FLAG, 1) != 0) {
        w[WORK_BLOCKED] = 2;
        w[WORK_TARGET] = (u32)pokeId;
    }
}

/* 0x5C, after a stat drop was held off: the pop-up and Clear Body's line. */
static void flower_veil_drop_notice(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = veil_work(factor, work);

    if (w[WORK_BLOCKED] == 2) {
        popup_line((struct BtlServerFlow *)context, ownerId, MSG_STATS_KEPT, (int)w[WORK_TARGET]);
        w[WORK_BLOCKED] = 0;
    }
}

static const BtlEventHandler k_flower_veil[] = {
    { 0x65, veil_block_sick },
    { 0x67, veil_notice },
    { 0x5B, flower_veil_block_drop },
    { 0x5C, flower_veil_drop_notice },
};

/* --- items ------------------------------------------------------- */

#define WORK_ATE_BERRY 0                /* the owner is eating a berry */

/* 0x72, the owner uses its held item: a berry, remembered. */
static void cheek_pouch_note(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        w[WORK_ATE_BERRY] = ITEM_IsBerry(BTL_POKEPARAM_GetItem(BTL_SERVER_FLOW_GetPokeParam((struct BtlServerFlow *)context,
                                                                                              ownerId))) != 0;
    }
}

/* 0x9D, the owner's held item is gone: after a berry, it heals `hp` of its
 * maximum HP (1/3), with the pop-up (Rain Dish's command). */
static void cheek_pouch_heal(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    u32 *w = (u32 *)work;
    struct BtlPokeParam *owner;
    HpCmd *cmd;

    (void)factor;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_POKE_ID) || w[WORK_ATE_BERRY] == 0) {
        return;
    }
    w[WORK_ATE_BERRY] = 0;
    owner = BTL_SERVER_FLOW_GetPokeParam(flow, ownerId);
    if (BTL_POKEPARAM_GetItem(owner) != 0 || BTL_POKEPARAM_IsFainted(owner)) {
        return;
    }
    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_RECOVER_HP, ownerId);
    cmd->flags |= CMD_FLAG_ABILITY;
    cmd->pokeId = ownerId;
    cmd->amount = (u16)BTL_CALC_DivideMaxHpMin1(owner, (u32)bw_bh_arg("hp", 1, 3));
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

static const BtlEventHandler k_cheek_pouch[] = {
    { 0x72, cheek_pouch_note },
    { 0x9D, cheek_pouch_heal },
};

/* 0x87, after the owner's move hit (Pickpocket's step): with no held item,
 * the owner takes the item of the first Pokemon it hit that holds one,
 * with the pop-up and Pickpocket's line.  Not for a move Sheer Force
 * powered. */
static void magician_steal(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    struct BtlPokeParam *target;
    StealItemCmd *cmd;
    u32 count, i;
    u8 targetId;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || BTL_EVENT_GetVar(EVAR_SHEER_FORCE) != 0
        || BTL_POKEPARAM_GetItem(BTL_SERVER_FLOW_GetPokeParam(flow, ownerId)) != 0) {
        return;
    }
    count = (u32)BTL_EVENT_GetVar(EVAR_DEFENDER_COUNT);
    for (i = 0; i < count; i++) {
        targetId = (u8)BTL_EVENT_GetVar(EVAR_DEFENDER_FIRST + (int)i);
        target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
        if (targetId == ownerId || BTL_POKEPARAM_GetItem(target) == 0) {
            continue;
        }
        cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_STEAL_ITEM, ownerId);
        cmd->flags |= CMD_FLAG_ABILITY;
        cmd->victimId = targetId;
        BTL_SERVER_FLOW_InitMsgRecord(&cmd->strParam, BTL_STR_TYPE_WITH_POKE_NAME, MSG_STOLE);
        BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->strParam), targetId);
        BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->strParam), BTL_POKEPARAM_GetItem(target));
        BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
        return;
    }
}

static const BtlEventHandler k_magician[] = {
    { 0x87, magician_steal },
};

#define EVAR_USED_UP_ITEM 0x100         /* eidos's: the item used up (BTL_SERVER_FLOW_SetHeldItem's patch) */
#define CMD_SET_ITEM   0x20
#define MSG_RECEIVED   0x457            /* "X received Y from Z!" (Bestow) */

/* hand_common_types.h: HANDEX_KIND_SET_ITEM */
typedef struct {
    u32 header;
    u16 itemId;
    u8 pokeId;
    u8 announce;
    u32 pad08;
    BtlStrParam message;
} SetItemCmd;

extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp);
extern int FILE_UTIL_IsMailItem(u32 item);
extern int HAND_COMMON_IsHeldItemBoundToOwnSpecies(struct BtlServerFlow *flow, int pokeId);
extern int HAND_COMMON_IsItemBoundToSpecies(u16 species, u32 item);

/* 0x9D, a held item is gone: when an ally used its own up (a berry, a
 * herb, a gem; not one taken from it), the owner passes it its item, with
 * the pop-up and Bestow's commands and line (not Mail, nor an item bound to
 * its holder's species). */
static void symbiosis_pass(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    int allyId = (int)BTL_EVENT_GetVar(BTL_EVAR_POKE_ID);
    struct BtlPokeParam *ally;
    SetItemCmd *cmd;
    u16 item;

    (void)factor;
    (void)work;
    if (allyId == ownerId || !is_own_side(ownerId, allyId) || BTL_EVENT_GetVar(EVAR_USED_UP_ITEM) == 0) {
        return;
    }
    ally = BTL_SERVER_FLOW_GetPokeParam(flow, allyId);
    item = BTL_POKEPARAM_GetItem(BTL_SERVER_FLOW_GetPokeParam(flow, ownerId));
    if (item == 0 || BTL_POKEPARAM_GetItem(ally) != 0 || BTL_POKEPARAM_IsFainted(ally) || FILE_UTIL_IsMailItem(item)
        || HAND_COMMON_IsHeldItemBoundToOwnSpecies(flow, ownerId)
        || HAND_COMMON_IsItemBoundToSpecies(BTL_POKEPARAM_GetSpecies(ally), item)) {
        return;
    }
    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_IN, ownerId);
    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_SET_ITEM, ownerId);
    cmd->pokeId = ownerId;
    cmd->itemId = 0;
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_SET_ITEM, ownerId);
    cmd->pokeId = (u8)allyId;
    cmd->itemId = item;
    BTL_SERVER_FLOW_InitMsgRecord(&cmd->message, BTL_STR_TYPE_WITH_POKE_NAME, MSG_RECEIVED);
    BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), (u32)allyId);
    BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), ownerId);
    BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), item);
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_OUT, ownerId);
}

static const BtlEventHandler k_symbiosis[] = {
    { 0x9D, symbiosis_pass },
};

/* --- The move about to be used (Protean, Stance Change) ----------- */

/* btl_server_types.h struct BtlMoveParam, its head */
typedef struct {
    u16 moveId;
    u16 origMoveId;
    u16 typePair;
    u8 type;                                /* after the type event 0x28 (Normalize, the -ate bases) */
} MoveParamHead;

/* btl_server_flow_types.h: the flow up to curMoveParam (+0x1AB0, the DS
 * layout on every build, docs/lp64.md) */
typedef struct {
    u8 fields000[0x1AB0];
    MoveParamHead *curMoveParam;
} FlowMoveHead;

#define WAZANO_STRUGGLE      165

/* The move the attacker is about to use, at event 0x22 (BTL_SERVER_FLOW_
 * ProcFight dispatches it with the chosen move, before "X used Y!"; the
 * Choice items lock the move there, Struggle says it has no moves left).
 * The flow's curMoveParam already holds the move that goes ahead: a move
 * Metronome or Sleep Talk calls (whose own "X used Y!" comes after the
 * event, the caller's before it), else the chosen one. */
static const MoveParamHead *move_going_ahead(void *context)
{
    return ((const FlowMoveHead *)context)->curMoveParam;
}

/* --- Protean ----------------------------------------------------- */

/* 0x22, the owner's move goes ahead, before "X used Y!" as in the later
 * games (after the checks that stop it: sleep, paralysis, flinch): the
 * owner takes the move's type, the one after the type event (Normalize,
 * the -ate bases), unless it has only that type, with the pop-up and Color
 * Change's command and line.  Not for Struggle (the later games). */
static void protean_change_type(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlServerFlow *flow = (struct BtlServerFlow *)context;
    const MoveParamHead *move = move_going_ahead(context);
    struct BtlPokeParam *owner;
    ChangeTypeCmd *cmd;
    int type;
    u16 pair;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || move == 0 || move->moveId == WAZANO_STRUGGLE) {
        return;
    }
    owner = BTL_SERVER_FLOW_GetPokeParam(flow, ownerId);
    type = move->type;
    if (type == POKETYPE_NONE || BTL_POKEPARAM_IsFainted(owner)) {
        return;
    }
    pair = BTL_TYPEPAIR_MakeSingle(type);
    if (BTL_POKEPARAM_GetTypePair(owner) == pair) {
        return;
    }
    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_IN, ownerId);
    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_CHANGE_TYPE, ownerId);
    cmd->typePair = pair;
    cmd->pokeId = ownerId;
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
    BTL_SERVER_FLOW_PushCmdRecord(flow, HANDEX_TOKWIN_OUT, ownerId);
}

static const BtlEventHandler k_protean[] = {
    { 0x22, protean_change_type },
};

/* --- Parental Bond ----------------------------------------------- */

/* the event variables of the hit count (BTL_SERVER_FLOW_CalcMultiHitCount) */
#define EVAR_HIT_MAX         0x29           /* the move record's most hits */
#define EVAR_HIT_COUNT       0x2A           /* the hits it makes */
#define WORK_SECOND_HIT      0              /* this move hits twice by the ability */
/* btl_server_flow_types.h: BtlServerFlow.curHitInfo, its byte 1 the hits made */
#define FLOW_CUR_HIT_INFO(flow) (*(u8 **)((u8 *)(flow) + 0x4B4))

/* 0x35, the hit count (Skill Link's step; BTL_SERVER_FLOW_CalcMultiHitCount
 * asks it for the owner's single-hit moves too): a damaging move that hits
 * once hits twice.  Not the moves that hit once by their nature: those of
 * several hits (the record's most hits past 1), those with a charge turn
 * (the record's flag), and those of `kind` (one_hit: the user faints, the
 * target's HP is set or the item is thrown, sdk/move_kinds.c; a mod's
 * move_kinds.txt changes the list). */
static void parental_bond_hits(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;
    u16 move;

    (void)factor;
    (void)context;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        return;
    }
    w[WORK_SECOND_HIT] = 0;
    move = (u16)BTL_EVENT_GetVar(BTL_EVAR_MOVE_ID);
    if (BTL_EVENT_GetVar(EVAR_HIT_MAX) == 1 && WAZA_TOOL_GetFlag(move, WAZA_FLAG_CHARGE) == 0
        && !move_is_kind(move, bw_bh_arg("kind", 1, KIND_ONE_HIT))) {
        BTL_EVENT_OverwriteVar(EVAR_HIT_COUNT, 2);
        w[WORK_SECOND_HIT] = 1;
    }
}

/* 0x38, the move's power: the second hit has `ratio` of it (1/4,
 * Generation VII; 1/2 for Generation VI). */
static void parental_bond_power(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    u32 *w = (u32 *)work;

    (void)factor;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) && w[WORK_SECOND_HIT] != 0
        && FLOW_CUR_HIT_INFO((struct BtlServerFlow *)context)[1] == 1) {
        BTL_EVENT_MultiplyVar(EVAR_POWER_RATIO, bw_bh_arg("ratio", 1, RATIO(0.25)));
        w[WORK_SECOND_HIT] = 0;
    }
}

/* 0x88, the move's damage is done (the gems' step): the mark goes, also
 * when the move hit several targets and so only once. */
static void parental_bond_end(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)context;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        ((u32 *)work)[WORK_SECOND_HIT] = 0;
    }
}

static const BtlEventHandler k_parental_bond[] = {
    { 0x35, parental_bond_hits },
    { 0x38, parental_bond_power },
    { 0x88, parental_bond_end },
};

/* --- Stance Change ----------------------------------------------- */

#define CMD_FORM_CHANGE      0x39
#define BPP_VALUE_FORM       0x13
#define MSG_TRANSFORMED      0xDE           /* "X transformed!" (Forecast, Relic Song) */
#define FORM_BLADE           1              /* Aegislash's forms in the later games */
#define FORM_SHIELD          0

/* hand_tokusei_types.h BtlFormChangeCmd */
typedef struct {
    u32 header;
    u8 pokeId;
    u8 form;
    u8 pad06[2];
    BtlStrParam strParam;
} FormChangeCmd;

extern int bw_bh_move_named(int move, int named, int key, int index, int *value);
#define NAMED_KINGS_SHIELD   0x10008        /* sdk/battle_handlers.h BW_BH_KINGS_SHIELD */

static void change_form(struct BtlServerFlow *flow, u8 ownerId, int form, int announce)
{
    FormChangeCmd *cmd;

    if (BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, ownerId), BPP_VALUE_FORM) == form) {
        return;
    }
    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, CMD_FORM_CHANGE, ownerId);
    cmd->pokeId = ownerId;
    cmd->form = (u8)form;
    if (announce) {
        cmd->header |= CMD_FLAG_ABILITY;
        BTL_SERVER_FLOW_InitMsgRecord(&cmd->strParam, BTL_STR_TYPE_WITH_POKE_NAME, MSG_TRANSFORMED);
        BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->strParam), ownerId);
    }
    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
}

/* 0x22, the owner's move goes ahead, before "X used Y!" (Protean's step,
 * move_going_ahead): a damaging move takes it to its attack form (`form`,
 * 1), King's Shield (a move whose line names kings_shield) to its guard
 * form (`form.2`, 0), with the pop-up and "X transformed!". */
static void stance_change_form(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    const MoveParamHead *ahead = move_going_ahead(context);
    u16 move;

    (void)factor;
    (void)work;
    if (ownerId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID) || ahead == 0) {
        return;
    }
    move = ahead->moveId;
    if (bw_bh_move_named(move, NAMED_KINGS_SHIELD, -1, 0, 0)) {
        change_form((struct BtlServerFlow *)context, ownerId, bw_bh_arg("form", 2, FORM_SHIELD), 1);
    } else if (WAZA_TOOL_GetDamageType(move) != WAZA_DAMAGE_STATUS) {
        change_form((struct BtlServerFlow *)context, ownerId, bw_bh_arg("form", 1, FORM_BLADE), 1);
    }
}

/* 0x6A and 0x89 (Zen Mode's steps): the owner leaves the battle or loses
 * the ability, and takes its guard form again with no line. */
static void stance_change_revert(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    (void)factor;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        change_form((struct BtlServerFlow *)context, ownerId, bw_bh_arg("form", 2, FORM_SHIELD), 0);
    }
}

static const BtlEventHandler k_stance_change[] = {
    { 0x22, stance_change_form },
    { 0x6A, stance_change_revert },
    { 0x89, stance_change_revert },
};

/* --- Grass Pelt -------------------------------------------------- */

/* patch_ov093_hand_waza.c: the terrain is a field factor of owner 0x40,
 * its kind in work slot 2 (1 Grassy) */
#define BTL_EVENT_TYPE_FIELD 3
#define OWNER_TERRAIN        0x40
#define TERRAIN_WORK_KIND    2
#define TERRAIN_GRASSY       1

extern struct BtlEventFactor *BTL_EVENT_FindFactor(int factorType, int ownerId);
extern u32 BTL_EVENT_GetFactorWork(struct BtlEventFactor *factor, int index);

/* 0x3C, the defending stat (Marvel Scale's step): while Grassy Terrain is
 * up, the owner's Defense is `ratio` times as high (1.5). */
static void grass_pelt_defense(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work)
{
    struct BtlEventFactor *terrain;

    (void)factor;
    (void)context;
    (void)work;
    if (ownerId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && BTL_EVENT_GetVar(EVAR_DAMAGE_TYPE) == WAZA_DAMAGE_PHYSICAL
        && (terrain = BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_TERRAIN)) != 0
        && BTL_EVENT_GetFactorWork(terrain, TERRAIN_WORK_KIND) == TERRAIN_GRASSY) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, bw_bh_arg("ratio", 1, RATIO(1.5)));
    }
}

static const BtlEventHandler k_grass_pelt[] = {
    { 0x3C, grass_pelt_defense },
};

#define ROWS(t) ((u32)(sizeof(t) / sizeof((t)[0])))

/* The bases Mold Breaker ignores, as the later games' Mold Breaker ignores
 * Bulletproof, Fur Coat, Fluffy, Ice Scales, Grass Pelt and the three veils. */
static int named_ability_breakable(unsigned base)
{
    switch (base) {
    case NAMED_GUARD_KIND:
    case NAMED_FUR_COAT:
    case NAMED_BLOCK_KIND:
    case NAMED_BULLETPROOF:
    case NAMED_SWEET_VEIL:
    case NAMED_AROMA_VEIL:
    case NAMED_FLOWER_VEIL:
    case NAMED_GRASS_PELT:
        return 1;
    }
    return 0;
}

/* The handler table of an eidos ability base, 0 for any other id. */
static const BtlEventHandler *named_ability_handlers(u32 base, u32 *count)
{
    switch (base) {
    case NAMED_PIXILATE:
    case NAMED_AERILATE:
    case NAMED_REFRIGERATE:
    case NAMED_GALVANIZE:      *count = ROWS(k_ate); return k_ate;
    case NAMED_BOOST_KIND:
    case NAMED_TOUGH_CLAWS:
    case NAMED_STRONG_JAW:
    case NAMED_MEGA_LAUNCHER:  *count = ROWS(k_boost_kind); return k_boost_kind;
    case NAMED_BOOST_TYPE:     *count = ROWS(k_boost_type); return k_boost_type;
    case NAMED_GUARD_KIND:     *count = ROWS(k_guard_kind); return k_guard_kind;
    case NAMED_FUR_COAT:       *count = ROWS(k_fur_coat); return k_fur_coat;
    case NAMED_BLOCK_KIND:
    case NAMED_BULLETPROOF:    *count = ROWS(k_block_kind); return k_block_kind;
    case NAMED_PRIORITY_TYPE:
    case NAMED_GALE_WINGS:     *count = ROWS(k_priority_type); return k_priority_type;
    case NAMED_ENTRY_STAT:     *count = ROWS(k_entry_stat); return k_entry_stat;
    case NAMED_KO_STAT:        *count = ROWS(k_ko_stat); return k_ko_stat;
    case NAMED_HIT_TYPE_STAT:  *count = ROWS(k_hit_type_stat); return k_hit_type_stat;
    case NAMED_SWEET_VEIL:     *count = ROWS(k_sweet_veil); return k_sweet_veil;
    case NAMED_AROMA_VEIL:     *count = ROWS(k_aroma_veil); return k_aroma_veil;
    case NAMED_FLOWER_VEIL:    *count = ROWS(k_flower_veil); return k_flower_veil;
    case NAMED_CHEEK_POUCH:    *count = ROWS(k_cheek_pouch); return k_cheek_pouch;
    case NAMED_PROTEAN:        *count = ROWS(k_protean); return k_protean;
    case NAMED_MAGICIAN:       *count = ROWS(k_magician); return k_magician;
    case NAMED_SYMBIOSIS:      *count = ROWS(k_symbiosis); return k_symbiosis;
    case NAMED_PARENTAL_BOND:  *count = ROWS(k_parental_bond); return k_parental_bond;
    case NAMED_STANCE_CHANGE:  *count = ROWS(k_stance_change); return k_stance_change;
    case NAMED_GRASS_PELT:     *count = ROWS(k_grass_pelt); return k_grass_pelt;
    }
    *count = 0;
    return 0;
}
