/*
 * sdk/mod_event.c -- the port's event dispatch (docs/mod-implementation.md,
 * section 1; docs/mod-events.md, sections 2 and 3).
 *
 * The layer holds three things:
 *
 *   - the binding table: the (region, address) pairs the port's own rows
 *     name, each with the rows it carries (a pair can hold a before row and
 *     an after row, as BTL_SERVER_FLOW_TryThrowBall does).  A pair with no
 *     binding costs one hash lookup and returns at once, so a raw-hook-only
 *     slot pays nothing for the layer.
 *   - the handler table: one list per event kind, in load order.  Every
 *     handler of an event runs, a refusal does not stop the later handlers,
 *     and the last write to a field wins.
 *   - the scope stack: the (region, address) of every slot that runs, so a
 *     binding scoped to a function can tell whether that function runs.
 *
 * The rules the dispatch keeps (docs/mod-events.md, section 3):
 *
 *   - every handler runs, in load order;
 *   - a refusal does not stop the later handlers;
 *   - modifications chain: a handler sees the payload as the handlers
 *     before it left it;
 *   - any refusal refuses; a refusal at an event that cannot be refused is
 *     ignored and counted.
 *
 * The two dispatch points of a pair: a row fires before the game function
 * when its point is BW_EVENT_BEFORE, and after it when the point is
 * BW_EVENT_AFTER.  The slot calls both, so a pair that carries both (as
 * BTL_SERVER_FLOW_TryThrowBall does, with catch.attempt before it and
 * catch.result after it) fires one event at each point and never the same
 * one twice.
 *
 * A refusal that runs replaces the applier: the refused outcome is what
 * the caller sees, so the modified fields are not written back.
 *
 * The builders, appliers and refusals live in one file per area
 * (sdk/mod_event_battle.c and its siblings); this file owns the dispatch
 * and nothing else.
 *
 * The port's own hook into the registration: a mod's handler is bound
 * through the host table's `on`, which the loader calls while the mod is
 * loading.  The loader tells the layer which mod is registering with
 * bw_mod_event_set_registering_state, so every handler receives the state
 * its own mod's start-up returned.  That call is the port's own and is not
 * part of the mod ABI.
 */
#include "mod_event.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* The event ids of sdk/mod_api.h go up to BW_EVENT_BATTLE_TURN (113). */
#define BW_MOD_EVENT_KINDS_MAX    128

/* How many handlers the table holds over every event kind together. */
#define BW_MOD_EVENT_HANDLERS_MAX 256

/* How many binding rows the port's own tables may hold. */
#define BW_MOD_EVENT_ROWS_MAX     256

/* How many (region, address) pairs those rows name. */
#define BW_MOD_EVENT_PAIRS_MAX    128

/* The hash over the pairs; a power of two. */
#define BW_MOD_EVENT_HASH_SIZE    256

/* How deep the scope stack goes. */
#define BW_MOD_EVENT_SCOPE_MAX    256

/* The index that means "none". */
#define BW_MOD_EVENT_NONE         0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/* The handler table                                                   */
/* ------------------------------------------------------------------ */

/*
 * One handler of one event kind.  The lists are per kind and run in load
 * order, so the record holds the next handler of its own kind.
 */
typedef struct BwEventHandlerRec {
    BwEventHandler fn;
    void         *state;   /* the mod's own state, the value it returned */
    u32           next;    /* the next handler of the kind, index + 1 */
    int           remote;  /* it takes the other players' events too */
} BwEventHandlerRec;

/* Multiplayer: the registering mod takes the other players' events; the
 * player whose event runs (0: this game's); where this game's events go. */
static int g_registering_remote;
static u32 g_event_player;
static void (*g_forward)(const void *payload, u32 size);

static BwEventHandlerRec g_handlers[BW_MOD_EVENT_HANDLERS_MAX];
static u32               g_handler_count;
static u32               g_kind_head[BW_MOD_EVENT_KINDS_MAX];  /* index + 1 */
static u32               g_kind_tail[BW_MOD_EVENT_KINDS_MAX];  /* index + 1 */

/* The state of the mod that is registering a handler right now. */
static void *g_registering_state;

/* ------------------------------------------------------------------ */
/* The binding table                                                   */
/* ------------------------------------------------------------------ */

/* One binding row: the port's own copy of a BwEventBinding. */
typedef struct BwEventRow {
    BwEventBinding b;
    u32            next;   /* the next row of the same pair */
} BwEventRow;

/* One (region, address) pair and the rows it carries. */
typedef struct BwEventPair {
    unsigned int region;
    unsigned int addr;
    u32          head;     /* the first row */
    u32          tail;     /* the last row */
    u32          next;     /* the next pair in the bucket, index + 1 */
} BwEventPair;

static BwEventRow  g_rows[BW_MOD_EVENT_ROWS_MAX];
static u32         g_row_count;
static BwEventPair g_pairs[BW_MOD_EVENT_PAIRS_MAX];
static u32         g_pair_count;
static u32         g_hash[BW_MOD_EVENT_HASH_SIZE];   /* index + 1 */

/* ------------------------------------------------------------------ */
/* The scope stack                                                     */
/* ------------------------------------------------------------------ */

typedef struct BwEventScope {
    unsigned int region;
    unsigned int addr;
} BwEventScope;

static BwEventScope g_scope[BW_MOD_EVENT_SCOPE_MAX];
static u32          g_scope_count;

/* How many pushes the stack could not take: their pops must not pop. */
static u32 g_scope_overflow;

/* ------------------------------------------------------------------ */
/* The refusals that were ignored                                      */
/* ------------------------------------------------------------------ */

/*
 * How many refusals were ignored because the event cannot be refused.
 * The count is a running statistic of the session and survives a reset;
 * the report below happens once, the first time it happens.
 */
static u32 g_ignored_refusals;
static int g_ignored_reported;

static void ignored_refusal(u32 kind)
{
    g_ignored_refusals++;
    if (!g_ignored_reported) {
        g_ignored_reported = 1;
        fprintf(stderr,
                "eidos: a mod refused event %u, which cannot be refused; "
                "the refusal was ignored\n", kind);
    }
}

/* ------------------------------------------------------------------ */
/* The payload sizes                                                   */
/* ------------------------------------------------------------------ */

/*
 * The size of the concrete payload of an event: the handler reads it to
 * know how much of the payload the port filled.  An id the port does not
 * know has no payload of its own, so it reads the header alone.
 */
static u32 payload_size(u32 kind)
{
    switch (kind) {
    case BW_EVENT_GAME_NEW:        return (u32)sizeof(BwGameNew);
    case BW_EVENT_SAVE_BEGIN:      return (u32)sizeof(BwSaveBegin);
    case BW_EVENT_SAVE_DONE:       return (u32)sizeof(BwSaveDone);
    case BW_EVENT_SAVE_LOAD:       return (u32)sizeof(BwSaveLoad);
    case BW_EVENT_GAME_CLEARED:    return (u32)sizeof(BwGameCleared);

    case BW_EVENT_ROUTE_CHANGE:    return (u32)sizeof(BwRouteChange);
    case BW_EVENT_MAP_WARP:        return (u32)sizeof(BwMapWarp);
    case BW_EVENT_PLAYER_STEP:     return (u32)sizeof(BwPlayerStep);
    case BW_EVENT_TRAINER_SPOTTED: return (u32)sizeof(BwTrainerSpotted);
    case BW_EVENT_NPC_TALK:        return (u32)sizeof(BwNpcTalk);
    case BW_EVENT_MENU_OPEN:       return (u32)sizeof(BwMenuOpen);
    case BW_EVENT_MENU_BUILD:      return (u32)sizeof(BwMenuBuild);
    case BW_EVENT_FIELD_MOVE:      return (u32)sizeof(BwFieldMove);
    case BW_EVENT_PC_OPEN:         return (u32)sizeof(BwPcOpen);

    case BW_EVENT_ENCOUNTER_WILD:    return (u32)sizeof(BwEncounterWild);
    case BW_EVENT_ENCOUNTER_STATIC:  return (u32)sizeof(BwEncounterStatic);
    case BW_EVENT_ENCOUNTER_TRAINER: return (u32)sizeof(BwEncounterTrainer);

    case BW_EVENT_MON_CREATE:    return (u32)sizeof(BwMonCreate);
    case BW_EVENT_MON_RECEIVE:   return (u32)sizeof(BwMonReceive);
    case BW_EVENT_MON_NICKNAME:  return (u32)sizeof(BwMonNickname);
    case BW_EVENT_MON_EVOLVE:    return (u32)sizeof(BwMonEvolve);
    case BW_EVENT_MON_EXP:       return (u32)sizeof(BwMonExp);
    case BW_EVENT_MON_LEVELUP:   return (u32)sizeof(BwMonLevelup);
    case BW_EVENT_MOVE_LEARN:    return (u32)sizeof(BwMoveLearn);
    case BW_EVENT_MON_RELEASE:   return (u32)sizeof(BwMonRelease);
    case BW_EVENT_BOX_DEPOSIT:   return (u32)sizeof(BwBoxDeposit);
    case BW_EVENT_BOX_WITHDRAW:  return (u32)sizeof(BwBoxWithdraw);
    case BW_EVENT_PARTY_SWAP:    return (u32)sizeof(BwPartySwap);
    case BW_EVENT_MON_HELDITEM:  return (u32)sizeof(BwMonHelditem);
    case BW_EVENT_EGG_HATCH:     return (u32)sizeof(BwEggHatch);
    case BW_EVENT_TRADE_INGAME:  return (u32)sizeof(BwTradeIngame);

    case BW_EVENT_BATTLE_START:  return (u32)sizeof(BwBattleStart);
    case BW_EVENT_BATTLE_ACTION: return (u32)sizeof(BwBattleAction);
    case BW_EVENT_BATTLE_ITEM:   return (u32)sizeof(BwBattleItem);
    case BW_EVENT_BATTLE_SWITCH: return (u32)sizeof(BwBattleSwitch);
    case BW_EVENT_BATTLE_ESCAPE: return (u32)sizeof(BwBattleEscape);
    case BW_EVENT_BATTLE_MOVE:   return (u32)sizeof(BwBattleMove);
    case BW_EVENT_BATTLE_DAMAGE: return (u32)sizeof(BwBattleDamage);
    case BW_EVENT_BATTLE_FAINT:  return (u32)sizeof(BwBattleFaint);
    case BW_EVENT_CATCH_ATTEMPT: return (u32)sizeof(BwCatchAttempt);
    case BW_EVENT_CATCH_RESULT:  return (u32)sizeof(BwCatchResult);
    case BW_EVENT_BATTLE_END:    return (u32)sizeof(BwBattleEnd);
    case BW_EVENT_WHITEOUT:       return (u32)sizeof(BwWhiteout);
    case BW_EVENT_BATTLE_TRAINER_MON: return (u32)sizeof(BwBattleTrainerMon);
    case BW_EVENT_BATTLE_EXP:     return (u32)sizeof(BwBattleExp);
    case BW_EVENT_BATTLE_AI:      return (u32)sizeof(BwBattleAi);
    case BW_EVENT_BATTLE_AI_REPLACE: return (u32)sizeof(BwBattleAiReplace);
    case BW_EVENT_BATTLE_ABILITY: return (u32)sizeof(BwBattleAbility);
    case BW_EVENT_BATTLE_STAT:    return (u32)sizeof(BwBattleStat);
    case BW_EVENT_BATTLE_HELD_ITEM: return (u32)sizeof(BwBattleHeldItem);
    case BW_EVENT_BATTLE_STATUS:  return (u32)sizeof(BwBattleStatus);
    case BW_EVENT_BATTLE_TURN:    return (u32)sizeof(BwBattleTurn);

    case BW_EVENT_ITEM_ADD:     return (u32)sizeof(BwItemAdd);
    case BW_EVENT_ITEM_USE:     return (u32)sizeof(BwItemUse);
    case BW_EVENT_SHOP_BUY:     return (u32)sizeof(BwShopBuy);
    case BW_EVENT_MONEY_CHANGE: return (u32)sizeof(BwMoneyChange);
    case BW_EVENT_HEAL:         return (u32)sizeof(BwHeal);
    case BW_EVENT_HEAL_HP:      return (u32)sizeof(BwHealHp);
    case BW_EVENT_MON_REVIVE:   return (u32)sizeof(BwMonRevive);
    case BW_EVENT_MON_CURE:     return (u32)sizeof(BwMonCure);
    case BW_EVENT_MON_PP:       return (u32)sizeof(BwMonPp);
    case BW_EVENT_MON_FORM:     return (u32)sizeof(BwMonForm);

    case BW_EVENT_BADGE_GET:    return (u32)sizeof(BwBadgeGet);
    case BW_EVENT_FLAG_SET:     return (u32)sizeof(BwFlagSet);
    case BW_EVENT_DEX_REGISTER: return (u32)sizeof(BwDexRegister);

    default:                     return (u32)sizeof(BwEvent);
    }
}

/* ------------------------------------------------------------------ */
/* The binding table                                                   */
/* ------------------------------------------------------------------ */

static u32 pair_hash(unsigned int region, unsigned int addr)
{
    u32 h = (u32)region * 0x9E3779B1u;

    h ^= (u32)addr + 0x85EBCA6Bu + (h << 6) + (h >> 2);
    h ^= h >> 13;
    return h & (BW_MOD_EVENT_HASH_SIZE - 1);
}

/* The pair of the (region, address), or NULL when it has no binding. */
static const BwEventPair *pair_find(unsigned int region, unsigned int addr)
{
    u32 i = g_hash[pair_hash(region, addr)];

    while (i != 0) {
        const BwEventPair *p = &g_pairs[i - 1];

        if (p->region == region && p->addr == addr) {
            return p;
        }
        i = p->next;
    }
    return NULL;
}

/* The pair of the (region, address), made when it is not there yet. */
static BwEventPair *pair_get(unsigned int region, unsigned int addr)
{
    u32 bucket = pair_hash(region, addr);
    u32 i = g_hash[bucket];
    BwEventPair *p;

    while (i != 0) {
        p = &g_pairs[i - 1];
        if (p->region == region && p->addr == addr) {
            return p;
        }
        i = p->next;
    }
    if (g_pair_count >= BW_MOD_EVENT_PAIRS_MAX) {
        return NULL;
    }
    p = &g_pairs[g_pair_count++];
    p->region = region;
    p->addr = addr;
    p->head = BW_MOD_EVENT_NONE;
    p->tail = BW_MOD_EVENT_NONE;
    p->next = g_hash[bucket];
    g_hash[bucket] = g_pair_count;
    return p;
}

int bw_mod_event_bind(const BwEventBinding *binding)
{
    BwEventPair *pair;
    BwEventRow  *row;

    if (binding == NULL) {
        return -1;
    }
    pair = pair_get(binding->region, binding->addr);
    if (pair == NULL) {
        fprintf(stderr, "eidos: the event binding table is full "
                "(%u pairs)\n", (unsigned int)BW_MOD_EVENT_PAIRS_MAX);
        return -1;
    }
    if (g_row_count >= BW_MOD_EVENT_ROWS_MAX) {
        fprintf(stderr, "eidos: the event binding table is full "
                "(%u rows)\n", (unsigned int)BW_MOD_EVENT_ROWS_MAX);
        return -1;
    }
    row = &g_rows[g_row_count];
    row->b = *binding;
    row->next = BW_MOD_EVENT_NONE;
    if (pair->tail != BW_MOD_EVENT_NONE) {
        g_rows[pair->tail].next = g_row_count;
    } else {
        pair->head = g_row_count;
    }
    pair->tail = g_row_count;
    g_row_count++;
    return 0;
}

void bw_mod_event_bind_area(const BwEventBinding *rows, u32 count)
{
    u32 i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        bw_mod_event_bind(&rows[i]);
    }
}

/* ------------------------------------------------------------------ */
/* The handler table                                                   */
/* ------------------------------------------------------------------ */

int bw_mod_event_register(u32 kind, BwEventHandler fn)
{
    BwEventHandlerRec *rec;

    if (fn == NULL || kind == 0 || kind >= BW_MOD_EVENT_KINDS_MAX) {
        return -1;
    }
    if (payload_size(kind) == 0) {
        /* The port does not have this event: every event it has carries a
         * payload of its own, and the two internal ids (the port's own
         * "continue" and "refuse") carry none.  A handler for an event the
         * port does not have would never fire, so it is refused here and
         * the mod is told (sdk/mod_loader.c). */
        return -1;
    }
    if (g_handler_count >= BW_MOD_EVENT_HANDLERS_MAX) {
        return -1;
    }
    rec = &g_handlers[g_handler_count];
    rec->fn = fn;
    rec->state = g_registering_state;
    rec->remote = g_registering_remote;
    rec->next = 0;
    g_handler_count++;
    if (g_kind_tail[kind] != 0) {
        g_handlers[g_kind_tail[kind] - 1].next = g_handler_count;
    } else {
        g_kind_head[kind] = g_handler_count;
    }
    g_kind_tail[kind] = g_handler_count;
    return 0;
}

u32 bw_mod_event_handler_count(u32 kind)
{
    u32 i;
    u32 n = 0;

    if (kind == 0 || kind >= BW_MOD_EVENT_KINDS_MAX) {
        return 0;
    }
    for (i = g_kind_head[kind]; i != 0; i = g_handlers[i - 1].next) {
        n++;
    }
    return n;
}

void bw_mod_event_set_registering_state(void *state)
{
    g_registering_state = state;
}

void bw_mod_event_set_registering_remote(int remote)
{
    g_registering_remote = remote != 0;
}

u32 bw_mod_event_player(void)
{
    return g_event_player;
}

void bw_mod_event_set_forward(void (*fn)(const void *payload, u32 size))
{
    g_forward = fn;
}

/* ------------------------------------------------------------------ */
/* The scope stack                                                     */
/* ------------------------------------------------------------------ */

void bw_mod_event_push(unsigned int region, unsigned int addr)
{
    if (g_scope_count >= BW_MOD_EVENT_SCOPE_MAX) {
        /* The frame is not on the stack, so the pop that closes it must
         * not pop one either: the stack stays balanced. */
        g_scope_overflow++;
        return;
    }
    g_scope[g_scope_count].region = region;
    g_scope[g_scope_count].addr = addr;
    g_scope_count++;
}

void bw_mod_event_pop(void)
{
    if (g_scope_overflow > 0) {
        g_scope_overflow--;
        return;
    }
    if (g_scope_count > 0) {
        g_scope_count--;
    }
}

int bw_mod_event_scope_matches(unsigned int scope_region,
                               unsigned int scope_addr, unsigned int scope)
{
    u32 i;
    int inside = 0;

    if (scope != BW_SCOPE_INSIDE && scope != BW_SCOPE_OUTSIDE) {
        return 1;   /* BW_SCOPE_ANY, and any scope the port does not know */
    }
    for (i = 0; i < g_scope_count; i++) {
        if (g_scope[i].region == scope_region &&
            g_scope[i].addr == scope_addr) {
            inside = 1;
            break;
        }
    }
    return (scope == BW_SCOPE_INSIDE) ? inside : !inside;
}

/* ------------------------------------------------------------------ */
/* The dispatch                                                        */
/* ------------------------------------------------------------------ */

/*
 * Every handler of the kind runs, in load order.  Each one sees the
 * payload as the ones before it left it, so the modifications chain.  A
 * refusal does not stop the later handlers, but it is remembered.
 */
static int run_handlers(u32 kind, BwEventPayload *payload)
{
    u32 i;
    int refused = 0;

    for (i = g_kind_head[kind]; i != 0; i = g_handlers[i - 1].next) {
        if (g_handlers[i - 1].fn(g_handlers[i - 1].state, &payload->h) ==
            BW_EVENT_REFUSE) {
            refused = 1;
        }
    }
    /* the event as this game's handlers left it goes to the other
     * players (multiplayer); a refused one did not happen */
    if (g_forward != NULL && !refused) {
        g_forward(payload, payload->h.size);
    }
    return refused;
}

int bw_mod_event_remote(u32 player, const void *buf, u32 size)
{
    BwEventPayload payload;
    u32 kind, i;

    if (buf == NULL || size < sizeof(BwEvent) || size > sizeof(payload)) {
        return -1;
    }
    memset(&payload, 0, sizeof(payload));
    memcpy(&payload, buf, size);
    kind = payload.h.kind;
    if (kind == 0 || kind >= BW_MOD_EVENT_KINDS_MAX ||
        payload_size(kind) == 0 || payload.h.size != size ||
        size != payload_size(kind)) {
        return -1;
    }
    g_event_player = player;
    for (i = g_kind_head[kind]; i != 0; i = g_handlers[i - 1].next) {
        if (g_handlers[i - 1].remote) {
            (void)g_handlers[i - 1].fn(g_handlers[i - 1].state, &payload.h);
        }
    }
    g_event_player = 0;
    return 0;
}

/*
 * The rows of the pair that fire at this point and whose scope matches
 * are run, one after the other.
 *
 * Returns 1 when a handler refused and the binding could refuse: the
 * refusal function has run and has set what the caller sees.
 */
static int dispatch(unsigned int region, unsigned int addr, BwEventCall *call,
                    BwEventPayload *payload, unsigned int point)
{
    const BwEventPair *pair = pair_find(region, addr);
    u32 idx;

    if (pair == NULL) {
        return 0;   /* no binding: the slot pays one lookup and goes on */
    }
    for (idx = pair->head; idx != BW_MOD_EVENT_NONE; idx = g_rows[idx].next) {
        const BwEventBinding *b = &g_rows[idx].b;

        if ((unsigned int)b->point != point) {
            continue;
        }
        if (!bw_mod_event_scope_matches(b->scope_region, b->scope_addr,
                                        b->scope)) {
            continue;
        }
        if (b->when != NULL && !b->when(call)) {
            continue;
        }
        if (b->kind == BW_EVENT_PORT) {
            if (b->apply != NULL) {
                memset(payload, 0, sizeof(*payload));
                b->apply(payload, call);
            }
            continue;
        }
        if (b->build != NULL) {
            b->build(call, payload);
        }
        payload->h.size = payload_size(b->kind);
        payload->h.kind = b->kind;

        if (run_handlers(b->kind, payload)) {
            if (b->refuse != NULL) {
                b->refuse(call);
                return 1;
            }
            ignored_refusal(b->kind);
        }
        if (b->apply != NULL) {
            b->apply(payload, call);
        }
    }
    return 0;
}

int bw_mod_event_enter(unsigned int region, unsigned int addr,
                       BwEventCall *call, BwEventPayload *payload)
{
    if (call == NULL || payload == NULL) {
        return 0;
    }
    return dispatch(region, addr, call, payload, BW_EVENT_BEFORE);
}

void bw_mod_event_leave(unsigned int region, unsigned int addr,
                        BwEventCall *call, BwEventPayload *payload)
{
    if (call == NULL || payload == NULL) {
        return;
    }
    /* A refusal at the end of the function has run its refusal function,
     * which has set the result the caller sees. */
    (void)dispatch(region, addr, call, payload, BW_EVENT_AFTER);
}

int bw_mod_event_fire(u32 kind, BwEventPayload *payload)
{
    if (payload == NULL || kind == 0 || kind >= BW_MOD_EVENT_KINDS_MAX ||
        payload_size(kind) == 0) {
        return 0;
    }
    payload->h.size = payload_size(kind);
    payload->h.kind = kind;
    return run_handlers(kind, payload);
}

u32 bw_mod_event_ignored_refusals(void)
{
    return g_ignored_refusals;
}

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_event_reset(void)
{
    memset(g_handlers, 0, sizeof(g_handlers));
    memset(g_kind_head, 0, sizeof(g_kind_head));
    memset(g_kind_tail, 0, sizeof(g_kind_tail));
    g_handler_count = 0;

    memset(g_rows, 0, sizeof(g_rows));
    memset(g_pairs, 0, sizeof(g_pairs));
    memset(g_hash, 0, sizeof(g_hash));
    g_row_count = 0;
    g_pair_count = 0;

    memset(g_scope, 0, sizeof(g_scope));
    g_scope_count = 0;
    g_scope_overflow = 0;

    g_registering_state = NULL;
    g_registering_remote = 0;
    g_event_player = 0;
}

/* ------------------------------------------------------------------ */
/* The port's own bindings                                             */
/* ------------------------------------------------------------------ */

/*
 * The area tables (sdk/mod_event_session.c and its siblings) publish the
 * rows of the stable events (docs/mod-events.md, section 1).  They are
 * bound here, once, at start-up and after every reset: the reset empties
 * the binding table, so a port that resets without rebinding would stop
 * firing its events.
 */
extern const BwEventBinding *bw_event_bindings_session(u32 *count);
extern const BwEventBinding *bw_event_bindings_items(u32 *count);
extern const BwEventBinding *bw_event_bindings_mon(u32 *count);
extern const BwEventBinding *bw_event_bindings_field(u32 *count);
extern const BwEventBinding *bw_event_bindings_battle(u32 *count);

void bw_mod_event_bind_all(void)
{
    u32 n = 0;
    const BwEventBinding *rows;

    rows = bw_event_bindings_session(&n);
    if (rows != NULL && n > 0) {
        bw_mod_event_bind_area(rows, n);
    }
    n = 0;
    rows = bw_event_bindings_items(&n);
    if (rows != NULL && n > 0) {
        bw_mod_event_bind_area(rows, n);
    }
    n = 0;
    rows = bw_event_bindings_mon(&n);
    if (rows != NULL && n > 0) {
        bw_mod_event_bind_area(rows, n);
    }
    n = 0;
    rows = bw_event_bindings_field(&n);
    if (rows != NULL && n > 0) {
        bw_mod_event_bind_area(rows, n);
    }
    n = 0;
    rows = bw_event_bindings_battle(&n);
    if (rows != NULL && n > 0) {
        bw_mod_event_bind_area(rows, n);
    }
}
