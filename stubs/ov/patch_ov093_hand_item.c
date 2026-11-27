/*
 * patches: src/game/ov093/hand_item.c HAND_ITEM_AddFactor HAND_ITEM_WeakenHitOfType HAND_ITEM_BoostMovePowerOfType HAND_ITEM_RecoverFixedHpOnEaten
 *
 * A mod's held item that works like a built-in one (sdk/battle_handlers.h,
 * eidos/tables/item_handlers.txt): the registry is searched for the base
 * item's handlers and the factor is added under the poke's own item id,
 * so the battle knows which item runs and the keys of that id apply.
 * HAND_ITEM_WeakenHitOfType (the berries that halve a super-effective hit
 * of one type) and HAND_ITEM_BoostMovePowerOfType (the plates, incenses and
 * the like that raise the power of one type's moves) are given the type
 * they test: a mod's `type` for the running item takes its place.
 * HAND_ITEM_RecoverFixedHpOnEaten (Oran Berry, Berry Juice) heals the item's
 * `held_hp` from a mod's eidos/tables/item_heal.txt (sdk/item_heal.h), at
 * most the HP the holder is missing.  With no table line the functions are
 * the game's own.
 *
 * symbols:
 *   gItemHandlerRegistry     0x021F1EA4  data  ov093
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlPokeParam;
struct BtlEventFactor;
struct BtlServerFlow;

typedef struct {
    int eventId;
    void (*handler)(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work);
} BtlEventHandler;

/* hand_tokusei_types.h */
typedef struct ItemHandlerEntry {
    u32 itemId;
    BtlEventHandler *(*getHandlers)(u32 *count);
} ItemHandlerEntry;

#define ITEM_HANDLER_COUNT 0xac
#define BTL_EVENT_TYPE_ITEM 5

typedef __INTPTR_TYPE__ sptr;   /* (types.h: an address-sized integer off the DS) */

#define BTL_EVAR_DEFENDER_ID                0x04
#define BTL_EVAR_MOVE_TYPE                  0x16
#define BTL_EVAR_RATIO                      0x35
#define BTL_EVAR_TYPE_AFFINITY              0x38
#define BTL_AFFINITY_SUPER_EFFECTIVE_ABOVE  3
#define BTL_RATIO_HALF                      0x800

extern ItemHandlerEntry gItemHandlerRegistry[ITEM_HANDLER_COUNT];

extern int BTL_POKEPARAM_GetStatRaw(struct BtlPokeParam *pp, int valueId);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern struct BtlEventFactor *BTL_EVENT_AddFactor(int factorType, u32 key, int priority, u32 subPriority, u8 ownerId,
                                                  const BtlEventHandler *handlers, u16 handlerCount);
extern sptr BTL_EVENT_GetVar(int key);
extern void BTL_EVENT_MultiplyVar(int key, int multiplier);
extern u32 BTL_POKEPARAM_HasSubstitute(struct BtlPokeParam *poke);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern u32 BTL_SERVER_FLOW_IsActionCounterSet(struct BtlServerFlow *flow);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);
extern const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                              u32 *total);
extern int bw_bh_type(int type);

/* An item that works like several (`1 = 234 + 161`): one factor whose
 * table is the bases' tables joined in line order. */
static struct BtlEventFactor *add_joined_items(struct BtlPokeParam *poke, u32 itemId, int parts)
{
    const void *tables[4];
    u32 counts[4], total = 0, i;
    int k, found = 0;
    const void *joined;

    for (k = 0; k < parts && k < 4; k++) {
        u32 base = (u32)bw_bh_part_base(BTL_EVENT_TYPE_ITEM, (int)itemId, k);

        tables[k] = 0;
        counts[k] = 0;
        for (i = 0; base != 0 && i < ITEM_HANDLER_COUNT; i++) {
            if (base == gItemHandlerRegistry[i].itemId) {
                tables[k] = gItemHandlerRegistry[i].getHandlers(&counts[k]);
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        return 0;
    }
    joined = bw_bh_join(BTL_EVENT_TYPE_ITEM, (int)itemId, k, tables, counts, sizeof(BtlEventHandler), &total);
    if (joined == 0 || total == 0) {
        return 0;
    }
    return BTL_EVENT_AddFactor(BTL_EVENT_TYPE_ITEM, itemId, 6, (u16)BTL_POKEPARAM_GetStatRaw(poke, 0xc),
                               BTL_POKEPARAM_GetPokeId(poke), (const BtlEventHandler *)joined, (u16)total);
}

/* Add the event factor of an item to the factor list; 0 if the item has no handler. */
struct BtlEventFactor *HAND_ITEM_AddFactor(struct BtlPokeParam *poke, u32 itemId)
{
    int parts = bw_bh_parts(BTL_EVENT_TYPE_ITEM, (int)itemId);
    u32 base = (u32)bw_bh_base(BTL_EVENT_TYPE_ITEM, (int)itemId);
    u32 i;
    u16 speed;
    u8 ownerId;
    BtlEventHandler *handlerTable;
    u32 handlerCount;

    if (parts > 1) {
        return add_joined_items(poke, itemId, parts);
    }
    for (i = 0; i < ITEM_HANDLER_COUNT; i++) {
        if (base == gItemHandlerRegistry[i].itemId) {
            speed = BTL_POKEPARAM_GetStatRaw(poke, 0xc);
            ownerId = BTL_POKEPARAM_GetPokeId(poke);
            handlerTable = gItemHandlerRegistry[i].getHandlers(&handlerCount);
            return BTL_EVENT_AddFactor(BTL_EVENT_TYPE_ITEM, itemId, 6, speed, ownerId, handlerTable, handlerCount);
        }
    }
    return 0;
}

int HAND_ITEM_WeakenHitOfType(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work, u8 moveType, int ignoreAffinity)
{
    (void)factor;
    moveType = (u8)bw_bh_type(moveType);    /* (a mod's `type` for the running item) */
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID) && moveType == BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE)
        && (ignoreAffinity != 0 || BTL_EVENT_GetVar(BTL_EVAR_TYPE_AFFINITY) > BTL_AFFINITY_SUPER_EFFECTIVE_ABOVE)
        && BTL_POKEPARAM_HasSubstitute(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId)) == 0) {
        BTL_EVENT_MultiplyVar(BTL_EVAR_RATIO, BTL_RATIO_HALF);
        if (BTL_SERVER_FLOW_IsActionCounterSet(flow) == 0)
            *work = 1;
        return 1;
    }
    return 0;
}

#define BTL_EVAR_ATTACKER_ID                0x03
#define BTL_EVAR_POWER_RATIO                0x31

int HAND_ITEM_GetBoostRatio(struct BtlEventFactor *factor);

/* The plates, incenses and the like: the item's power ratio on the holder's moves of one type. */
void HAND_ITEM_BoostMovePowerOfType(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int moveType)
{
    (void)flow;
    moveType = bw_bh_type(moveType);      /* (a mod's `type` for the running item) */
    if (pokeId != BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID))
        return;
    if (moveType != BTL_EVENT_GetVar(BTL_EVAR_MOVE_TYPE))
        return;
    BTL_EVENT_MultiplyVar(BTL_EVAR_POWER_RATIO, HAND_ITEM_GetBoostRatio(factor));
}

/* hand_common_types.h: HANDEX_KIND_RECOVER_HP, its head (the message follows) */
typedef struct {
    u32 header;
    u16 amount;
    u8 pokeId;
    u8 pad07;
    u8 message[0x24];
} HandExRecoverHp;

#define HANDEX_KIND_RECOVER_HP 5
#define BTL_EVAR_POKE_ID       0x02
#define BTL_STRTYPE_SET        2
#define BTL_VALUE_HP           13
#define BTL_VALUE_MAX_HP       14

extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern void BTL_SERVER_FLOW_InitMsgRecord(void *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(void *message, u32 arg);
extern u16 BTL_EVENT_GetFactorKey(struct BtlEventFactor *factor);
extern int HAND_ITEM_GetParam(struct BtlEventFactor *factor, int param);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId);

/* sdk/item_heal.c */
extern int bw_item_heal_held_hp(u32 item, u32 missing, u32 *amount);

/* Oran Berry and the like: a fixed amount of HP when eaten. */
void HAND_ITEM_RecoverFixedHpOnEaten(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    HandExRecoverHp *rec;
    u32 heal;

    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_POKE_ID)) {
        struct BtlPokeParam *bpp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);

        rec = (HandExRecoverHp *)BTL_SERVER_FLOW_AllocCmdRecord(flow, HANDEX_KIND_RECOVER_HP, pokeId);
        rec->pokeId = (u8)pokeId;
        if (bw_item_heal_held_hp(BTL_EVENT_GetFactorKey(factor),
                                 (u32)(BTL_POKEPARAM_GetValue(bpp, BTL_VALUE_MAX_HP) - BTL_POKEPARAM_GetValue(bpp, BTL_VALUE_HP)),
                                 &heal)) {
            rec->amount = (u16)heal;            /* (a mod's item_heal.txt) */
        } else {
            rec->amount = (u16)HAND_ITEM_GetParam(factor, 2);
        }
        BTL_SERVER_FLOW_InitMsgRecord(rec->message, BTL_STRTYPE_SET, 908);
        BTL_SERVER_FLOW_AppendMsgRecordArg(rec->message, (u32)pokeId);
        BTL_SERVER_FLOW_AppendMsgRecordArg(rec->message, BTL_EVENT_GetFactorKey(factor));
        BTL_SERVER_FLOW_CommitCmdRecord(flow, rec);
    }
}
