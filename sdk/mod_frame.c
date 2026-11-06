/*
 * sdk/mod_frame.c -- the frame tick: the mods' own frame handlers and the
 * network's, once a game frame (docs/mod-services.md, "Frame tick",
 * "Peer sessions" and "External control"; docs/mod-implementation.md,
 * section 3, "What is still to build": the port's own frame hook).
 *
 * What this file owns
 * -------------------
 *
 * The port's frame hook, and nothing else: one call, once a game frame, in
 * the game's thread.  It does two things, in this order:
 *
 *   1. the mods' own frame handlers, in the order the mods bound them;
 *   2. the network service's frame (bw_mod_net_frame), which is where the
 *      session events and the commands the network's threads queued reach
 *      the mods' handlers.
 *
 * The rules it keeps
 * ------------------
 *
 *   - "Called once per game frame, after the game's own frame work"
 *     (docs/mod-services.md, "Frame tick").  The boot's halt hook calls
 *     this right after the frame loop's own pass (the game's step, the
 *     vblank and the render), so a mod's handler runs after the game's
 *     frame and never before it.
 *   - "The handler runs on the game's frame, never on a network thread"
 *     (the same document, "Peer sessions" and "External control").  The
 *     network's threads only queue what they read; the frame drains the
 *     queue and calls the handlers.  So a mod's handler and a network
 *     handler both run in the game's thread, and the frame is the only
 *     place either of them is ever called from.
 *   - The mod's own state calls reach the mod's own file: the mod's
 *     namespace is in force while its handler runs (docs/mod-state.md,
 *     section 2), the way the event path does it.
 *
 * Who is calling
 * --------------
 *
 * The services table is filled per mod (sdk/mod_loader.c gives every mod a
 * table of its own) and the loader binds the same functions into every one
 * of them, so the state cannot be one set of variables: the port says which
 * mod is calling the way it says so everywhere else, with the loader's
 * namespace (bw_mod_savedata_namespace), and this file keys its handlers
 * on it.  The network service works the same way (sdk/mod_net.c, "Who is
 * calling"), and so does the tick's own rule: a handler runs with its own
 * mod's namespace in force.
 *
 * The loader's own way in
 * -----------------------
 *
 * The loader may bind the services call (bw_mod_frame_on_frame) into every
 * mod's table, one line, like the other services; the mod is then the
 * namespace in force.  Or it may bind a call of its own per mod and
 * register the handler here, with the mod's namespace and the mod's state
 * (the event path's way: bw_mod_frame_register and
 * bw_mod_frame_set_registering_state).  Both reach the same table.
 */

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "mod_frame.h"
#include "mod_net.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The saved-data layer (sdk/mod_savedata.c): the namespace of the mod that
 * is calling, and the setter that puts one in force.  The layer has no
 * header of its own yet, so the prototypes are here.  They are weak, so a
 * link without the layer still succeeds: the tick then runs the handlers
 * without a namespace in force.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

extern void        bw_mod_savedata_set_namespace(const char *ns) BW_PORT_WEAK;
extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;
extern void        bw_mod_net_frame(void) BW_PORT_WEAK;
extern void        bw_mp_frame(void) BW_PORT_WEAK;
extern void        bw_link_frame(void) BW_PORT_WEAK;
extern void        bw_slots_frame(void) BW_PORT_WEAK;
extern void        bw_mod_timer_frame(void) BW_PORT_WEAK;
extern void        bw_mod_avatar_frame(void) BW_PORT_WEAK;
extern void        bw_mod_handoff_frame(void) BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many frame handlers the table holds. */
#define BW_MOD_FRAME_HANDLERS_MAX 64

/* The longest namespace the table keeps (the loader's own limit). */
#define BW_MOD_FRAME_NS_MAX 96

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

/*
 * One bound handler: the mod's own function, the mod's own state (the
 * value its start-up entry returned) and the mod's state namespace.  The
 * handlers run in the order they were bound, which is the load order.
 */
typedef struct BwModFrameHandlerRec {
    BwFrameHandler fn;
    void          *state;   /* the mod's own state, the value it returned */
    char           ns[BW_MOD_FRAME_NS_MAX + 1];
} BwModFrameHandlerRec;

static BwModFrameHandlerRec g_handlers[BW_MOD_FRAME_HANDLERS_MAX];
static u32                  g_handler_count;

/* The state of the mod that is registering a handler right now. */
static void *g_registering_state;

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* Copy `src` into `dst`, cutting it at the buffer's end. */
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t n;

    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* The namespace of the mod that is calling, or "" when there is none. */
static const char *current_namespace(void)
{
    const char *ns;

    if (bw_mod_savedata_namespace == NULL) {
        return "";
    }
    ns = bw_mod_savedata_namespace();
    return (ns != NULL) ? ns : "";
}

/* ------------------------------------------------------------------ */
/* The registration                                                   */
/* ------------------------------------------------------------------ */

int bw_mod_frame_register(BwFrameHandler fn, const char *ns)
{
    BwModFrameHandlerRec *rec;

    if (fn == NULL) {
        return -1;
    }
    if (g_handler_count >= BW_MOD_FRAME_HANDLERS_MAX) {
        fprintf(stderr, "eidos: the frame handler table is full "
                "(%u handlers)\n", (unsigned int)BW_MOD_FRAME_HANDLERS_MAX);
        return -1;
    }
    if (ns == NULL) {
        ns = current_namespace();
    }
    rec = &g_handlers[g_handler_count];
    rec->fn = fn;
    rec->state = g_registering_state;
    copy_text(rec->ns, sizeof(rec->ns), ns);
    g_handler_count++;
    return 0;
}

void bw_mod_frame_set_registering_state(void *state)
{
    g_registering_state = state;
}

/*
 * The services table's call: the mod binds its frame handler.  The mod is
 * the one the namespace in force names, so the same call is bound into
 * every mod's table.
 */
void bw_mod_frame_on_frame(BwFrameHandler fn)
{
    if (fn == NULL) {
        return;
    }
    (void)bw_mod_frame_register(fn, current_namespace());
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

/*
 * The frame tick: the mods' own frame handlers, then the network's.
 *
 * Each handler runs with its own mod's namespace in force, so the mod's
 * state calls reach its own file (docs/mod-state.md, section 2), and the
 * namespace is put back afterwards: the next handler, and the network's
 * own frame, see the port's namespace, not this mod's.
 *
 * The count is taken before the first handler runs: a handler that binds
 * another handler (a mod that changes its mind) does not make the new one
 * run in the same frame.
 */
void bw_mod_frame_tick(void)
{
    u32 count = g_handler_count;
    u32 i;

    for (i = 0; i < count; i++) {
        const char *saved = NULL;

        if (bw_mod_savedata_set_namespace != NULL) {
            saved = bw_mod_savedata_namespace();
            bw_mod_savedata_set_namespace(g_handlers[i].ns);
        }
        g_handlers[i].fn();
        if (saved != NULL) {
            bw_mod_savedata_set_namespace(saved);
        }
    }

    /* The network's own frame: the session events and the commands the
     * network's threads queued reach the mods' handlers here, on the
     * game's thread (bw_mod_net_frame). */
    bw_mod_net_frame();

    /* Multiplayer's timers: the grace after a drop, the host's return, the
     * rejoin attempts (sdk/mp.h). */
    bw_mp_frame();

    /* A Link session's end turns the radio off (sdk/link.h). */
    if (bw_slots_frame != NULL) {
        bw_slots_frame();               /* a new game's first save (sdk/save_slots.c) */
    }
    if (bw_link_frame != NULL) {
        bw_link_frame();
    }

    /* The timer's frame: the mods' timers advance with the game's frames,
     * so the pause while the port's own menu is open is exact to the
     * frame (sdk/mod_timer.c). */
    bw_mod_timer_frame();

    /* The avatars move with the frames (sdk/mod_avatar.c), and the save
     * handoff's confirmation is asked on the frame (sdk/mod_handoff.c). */
    bw_mod_avatar_frame();
    bw_mod_handoff_frame();

    /* A mod's whiteout starts on a quiet field frame (sdk/mod_state.c). */
    bw_mod_state_frame();
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

void bw_mod_frame_reset(void)
{
    memset(g_handlers, 0, sizeof(g_handlers));
    g_handler_count = 0;
    g_registering_state = NULL;
}

/*
 * Take one mod's handlers out: the table holds the mod's own function, so a
 * handler left behind after the mod's library is unloaded would be a call
 * into a library that is gone.  The loader calls this when it stops a mod.
 * Returns how many handlers it took.
 */
u32 bw_mod_frame_remove_for(const char *ns)
{
    u32 removed = 0;
    u32 i;
    u32 keep = 0;

    if (ns == NULL || *ns == '\0') {
        return 0;
    }
    for (i = 0; i < g_handler_count; i++) {
        if (strcmp(g_handlers[i].ns, ns) == 0) {
            removed++;
            continue;
        }
        if (keep != i) {
            g_handlers[keep] = g_handlers[i];
        }
        keep++;
    }
    if (keep < g_handler_count) {
        memset(&g_handlers[keep], 0,
               (g_handler_count - keep) * sizeof(g_handlers[0]));
        g_handler_count = keep;
    }
    return removed;
}

u32 bw_mod_frame_handler_count(void)
{
    return g_handler_count;
}
