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

/*
 * Add the handler of a move to the Pokemon when the move is chosen. Looks the
 * move up in the registry; returns 1 when a handler was added.
 */
int HAND_WAZA_AddMoveHandler(struct BtlPokeParam *bpp, u16 moveId, u32 subPriority)
{
    u16 base = (u16)bw_bh_base(BTL_EVENT_TYPE_WAZA, moveId);
    u32 index;
    u8 ownerId;
    u8 alreadyAdded;
    int handlerCount;
    BtlEventHandler *handlers;

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
