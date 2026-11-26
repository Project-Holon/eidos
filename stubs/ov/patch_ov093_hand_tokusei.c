/*
 * patches: src/game/ov093/hand_tokusei.c HAND_TOKUSEI_AddAbilityHandlers HAND_TOKUSEI_PinchBoostType HAND_TOKUSEI_AbsorbCheckMoveType
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
 * symbols:
 *   BTL_POKEPARAM_GetValue              0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId             0x021D5440  thumb  ov093
 *   BTL_EVENT_AddFactor                 0x021D6F0C  thumb  ov093
 *   gAbilityHandlerRegistry             0x021F0E74  data   ov093
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
static struct BtlEventFactor *add_joined_abilities(struct BtlPokeParam *bpp, u16 abilityId, int parts)
{
    const void *tables[4];
    u32 counts[4], total = 0, i;
    int k, priority = -1;
    const void *joined;

    for (k = 0; k < parts && k < 4; k++) {
        u32 base = (u32)bw_bh_part_base(BTL_EVENT_TYPE_ABILITY, abilityId, k);

        tables[k] = 0;
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
