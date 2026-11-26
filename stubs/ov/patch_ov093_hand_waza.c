/*
 * patches: src/game/ov093/hand_waza.c HAND_WAZA_AddMoveHandler
 *
 * A mod's move that works like a built-in one (sdk/battle_handlers.h,
 * eidos/tables/move_handlers.txt): the registry is searched for the base
 * move's handlers, and the factor is added under the move's own id, so the
 * battle knows which move runs and the keys of that id apply.  A move with
 * no table line is its own base: the function then does what the game's
 * does.
 *
 * symbols:
 *   gWazaHandlerRegistry  0x021F3030  data
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

/* hand_common_types.h */
typedef struct {
    u16 moveId;
    BtlEventHandler *(*getHandlers)(int *handlerCount);
} WazaHandlerEntry;

#define WAZA_HANDLER_REGISTRY_COUNT 0x102
#define BTL_EVENT_TYPE_WAZA 0

extern const WazaHandlerEntry gWazaHandlerRegistry[WAZA_HANDLER_REGISTRY_COUNT];

extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern struct BtlEventFactor *BTL_EVENT_AddFactor(int factorType, u32 key, int priority, u32 subPriority, u8 ownerId,
                                                  const BtlEventHandler *handlers, u16 handlerCount);
int HAND_WAZA_CanAddMoveHandler(int ownerId, int moveId, u8 *alreadyAdded);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);
extern const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                              u32 *total);

/*
 * Add the handler of a move to the Pokemon when the move is chosen. Looks the
 * move up in the registry; returns 1 when a handler was added.
 */
/* A move that works like several (`600 = 85 + 26`): one factor whose
 * table is the bases' tables joined in line order. */
static int add_joined_moves(struct BtlPokeParam *bpp, u16 moveId, u32 subPriority, int parts)
{
    const void *tables[4];
    u32 counts[4], total = 0, index;
    int k, found = 0;
    u8 ownerId, alreadyAdded = 0;
    const void *joined;

    ownerId = BTL_POKEPARAM_GetPokeId(bpp);
    if (HAND_WAZA_CanAddMoveHandler(ownerId, moveId, &alreadyAdded) == 0) {
        return 0;
    }
    for (k = 0; k < parts && k < 4; k++) {
        u16 base = (u16)bw_bh_part_base(BTL_EVENT_TYPE_WAZA, moveId, k);

        tables[k] = 0;
        counts[k] = 0;
        for (index = 0; base != 0 && index < WAZA_HANDLER_REGISTRY_COUNT; index++) {
            if (base == gWazaHandlerRegistry[index].moveId) {
                int n = 0;

                tables[k] = gWazaHandlerRegistry[index].getHandlers(&n);
                counts[k] = tables[k] != 0 && n > 0 ? (u32)n : 0;
                found |= counts[k] != 0;
                break;
            }
        }
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
    u16 base = (u16)bw_bh_base(BTL_EVENT_TYPE_WAZA, moveId);
    u32 index;
    u8 ownerId;
    u8 alreadyAdded;
    int handlerCount;
    BtlEventHandler *handlers;

    if (parts > 1) {
        return add_joined_moves(bpp, moveId, subPriority, parts);
    }
    for (index = 0; index < WAZA_HANDLER_REGISTRY_COUNT; index++) {
        if (base == gWazaHandlerRegistry[index].moveId) {
            ownerId = BTL_POKEPARAM_GetPokeId(bpp);
            alreadyAdded = 0;
            if (HAND_WAZA_CanAddMoveHandler(ownerId, moveId, &alreadyAdded) != 0) {
                handlers = gWazaHandlerRegistry[index].getHandlers(&handlerCount);
                if (handlers != 0) {
                    BTL_EVENT_AddFactor(BTL_EVENT_TYPE_WAZA, moveId, 0, subPriority, ownerId, handlers, handlerCount);
                    return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}
