/*
 * patches: src/game/ov093/btl_event.c BTL_EVENT_DispatchToFactors BTL_EVENT_MultiplyVar
 *
 * The battle tells sdk/battle_handlers.c which move, ability or item a
 * running handler belongs to (the factor's type and key, and the
 * handler's row in its table), around each handler call, so a mod's
 * numbers for that id (eidos/tables/*_handlers.txt) reach the places the
 * handler uses a number.  Calls nest: a handler's command can dispatch
 * more events.  BTL_EVENT_MultiplyVar is one such place (`ratio`).
 * The game runs the first row of a factor that answers an event; a
 * factor of several bases (`165 = 22 + 2`) runs the first row of each
 * base, in line order (bw_bh_row_end).
 * Otherwise both functions are the game's own.
 *
 * symbols:
 *   gBtlEventFactorList  0x021F3CB0  data  a4
 *   gBtlEventVarStack    0x021F3ECC  data  a4
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define BTL_EVENT_TYPE_ANY 7

struct BtlEventFactor;

typedef int (*BtlEventSkipCheck)(struct BtlEventFactor *other, void *context, int factorType, int eventId, u16 key,
                                 u8 ownerId);

typedef struct {
    int eventId;
    void (*handler)(struct BtlEventFactor *factor, void *context, u8 ownerId, void *work);
} BtlEventHandler;

/* btl_event_types.h: one event factor (0x3C bytes) */
struct BtlEventFactor {
    struct BtlEventFactor *prev, *next;
    const BtlEventHandler *handlers;
    BtlEventSkipCheck skipCheck;
    int factorType;
    u32 priority;
    u32 addDepth : 16;
    u32 handlerCount : 8;
    u32 running : 1;
    u32 suspended : 1;
    u32 answersSpecial : 1;
    u32 removePending : 1;
    u32 keepRunning : 1;
    u32 active : 1;
    u32 disabled : 1;
    u32 reserved31 : 1;
    u32 work[7];
    u16 key;
    u8 ownerId;
    u8 pokeId;
};

/* the head of btl_event_types.h's struct BtlEventFactorList */
struct BtlEventFactorList {
    u32 dispatchDepth;
    struct BtlEventFactor *head;
};

struct BtlServerFlow;

typedef int s32;
typedef long long s64;
typedef __INTPTR_TYPE__ sptr;   /* (types.h: an address-sized integer off the DS) */

#define BTL_EVENT_VAR_MAX 0x60

/* btl_event_types.h */
struct BtlEventVarStack {
    u32 frameTop;
    u16 keys[BTL_EVENT_VAR_MAX];
    sptr values[BTL_EVENT_VAR_MAX];
    u32 maxValues[BTL_EVENT_VAR_MAX];
    u32 minValues[BTL_EVENT_VAR_MAX];
    u8 modes[BTL_EVENT_VAR_MAX];
};

extern struct BtlEventFactorList gBtlEventFactorList;
extern struct BtlEventVarStack gBtlEventVarStack;

int BTL_EVENT_IsEventBlocked(struct BtlServerFlow *context, struct BtlEventFactor *target, int eventId);
void BTL_EVENT_RemoveFactor(struct BtlEventFactor *factor);
int BTL_EVENT_FindVar(struct BtlEventVarStack *stack, int key);
int BTL_EVENT_ClampVar(struct BtlEventVarStack *stack, int index, int value);

/* sdk/battle_handlers.c */
extern void bw_bh_enter(int type, int id, int handler);
extern void bw_bh_leave(void);
extern int bw_bh_row_end(int type, int id, int row);
extern int bw_bh_ratio(int ratio);

void BTL_EVENT_DispatchToFactors(struct BtlServerFlow *context, int eventId, int factorType, int checkSkip)
{
    struct BtlEventFactor *factor;
    struct BtlEventFactor *next;
    BtlEventHandler *handler;
    const BtlEventHandler *handlers;
    u32 i;
    int count;
    int typeMismatch;
    u32 ownerId;
    int end;
    for (factor = gBtlEventFactorList.head; factor != 0; ) {
        next = factor->next;
        if (factor->running && !factor->keepRunning) goto advance;
        if (factor->suspended || factor->disabled) goto advance;
        typeMismatch = (factorType != BTL_EVENT_TYPE_ANY) && (factor->factorType != factorType);
        if (typeMismatch) goto advance;
        if (factor->addDepth != 0 && factor->addDepth >= gBtlEventFactorList.dispatchDepth) goto advance;
        if (!factor->active) goto advance;
        if (eventId == 0x73 && !factor->answersSpecial) goto advance;
        count = factor->handlerCount;
        handlers = factor->handlers;
        i = 0;
        for (; i < count; i++) {
            handler = (BtlEventHandler *)&handlers[i];
            if (eventId != handler->eventId) continue;
            if (checkSkip && BTL_EVENT_IsEventBlocked(context, factor, eventId)) break;
            factor->running = 1;
            ownerId = factor->ownerId;
            bw_bh_enter(factor->factorType, factor->key, (int)i);
            handler->handler(factor, context, ownerId, factor->work);
            bw_bh_leave();
            if (factor->keepRunning) factor->keepRunning = 0;
            else factor->running = 0;
            if (factor->removePending) {
                if (next != 0 && !next->active) next = factor->next;
                BTL_EVENT_RemoveFactor(factor);
                break;
            }
            /* (a joined table: the next base's rows; otherwise the game's stop) */
            end = bw_bh_row_end(factor->factorType, factor->key, (int)i);
            if (end <= (int)i) break;
            i = (u32)end - 1;
        }
advance:
        if (next == 0) break;
        if (next->active) factor = next;
        else factor = factor->next;
    }
}

void BTL_EVENT_MultiplyVar(int key, s32 multiplier)
{
    struct BtlEventVarStack *stack = &gBtlEventVarStack;
    int i = BTL_EVENT_FindVar(stack, key);

    multiplier = bw_bh_ratio(multiplier);   /* (a mod's `ratio` for the running id) */
    if (i >= 0 && stack->modes[i] == 3) {
        s32 value = stack->values[i];
        s64 product = (s64)value * (s64)multiplier;
        stack->values[i] = BTL_EVENT_ClampVar(stack, i, (s32)((product + 0x800) >> 12));
    }
}
