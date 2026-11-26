/*
 * sdk/mod_v6.c -- the game-side services of mod API version 6: work
 * variables, badges, field scripts started by a mod, the type chart and
 * text input (docs/mod-services.md).
 *
 * Work variables
 * --------------
 *
 * The saved works (0x4000 to 0x40FF) live in the save's event work
 * (ZONEDATA_GetEventWorkAddress).  The script's own works (0x8000 and up)
 * live in the running script's work (SCRIPT_WORK_GetWorkPtr), so they are
 * reached only while a field script runs a command: the field VM's
 * dispatch (stubs/reimpl_vm_control.c) tells this file which command work
 * is running, and a flag.set handler (the flag is set by a command) reads
 * the script's works through it.
 *
 * Field scripts
 * -------------
 *
 * script_run queues the script.  On a field frame with no event, no battle
 * and no menu (the condition the whiteout waits for, sdk/mod_state.c,
 * held for 10 frames so a fade has ended), the port starts a game event of
 * its own that calls the script as its child, the way the game starts one
 * (SCRIPT_SYS_CallScriptEvent), and fires script.done when the child ends.
 *
 * symbols:
 *   SCRIPT_SYS_CallScriptEvent        0x02158780  thumb  ov010
 *   SCRIPT_WORK_GetWorkPtr            0x02158FE0  thumb  ov010
 *   SCRCMD_WORK_GetScriptWork         0x0215A5F4  thumb  ov010
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "mod_event.h"
#include "vfs.h"
#include "mod_v6.h"
#include "type_chart.h"
#include "type_icons.h"

extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;
extern void *bw_mod_state_game_data(void);
extern void *bw_mod_state_quiet_field(void);

struct GameData;
struct GameSystem;
struct GameEvent;
struct EventWork;
struct MiscSave;

extern u8 *GAME_DATA_GetEventWork(struct GameData *gd) __attribute__((weak));
extern u16 *ZONEDATA_GetEventWorkAddress(struct EventWork *ev, u32 workId) __attribute__((weak));
extern void *GAME_DATA_GetMiscSave(struct GameData *gameData) __attribute__((weak));
/* MISC_SetBadge through its slot, so badge.get fires (mod/declared_set.txt) */
extern void bw_slot_arm9_0200C248(void *a0, unsigned int a1) __attribute__((weak));
extern void MISC_SetBadge(struct MiscSave *beacon, u32 bit) __attribute__((weak));

typedef int (*BwV6EventFunc)(struct GameEvent *event, int *seq, void *work);
extern struct GameEvent *GAME_EVENT_Create(struct GameSystem *gameSystem, struct GameEvent *parent,
                                           BwV6EventFunc func, u32 workSize) __attribute__((weak));
extern void *GAME_EVENT_GetWork(struct GameEvent *event) __attribute__((weak));
extern void GAMESYSTEM_SetEvent(struct GameSystem *gameSystem, struct GameEvent *event)
    __attribute__((weak));
extern void *SCRIPT_SYS_CallScriptEvent(void *parentEvent, u16 scriptId, void *target,
                                        void *parentWork, u16 reserved) __attribute__((weak));
extern u16 *SCRIPT_WORK_GetWorkPtr(void *work, void *gameData, unsigned int id)
    __attribute__((weak));
extern void *SCRCMD_WORK_GetScriptWork(void *cmdWork) __attribute__((weak));

static const char *calling_ns(void)
{
    return bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : "";
}

static u32 g_ticket;

static u32 next_ticket(void)
{
    if (++g_ticket == 0) {
        g_ticket = 1;
    }
    return g_ticket;
}

/* ------------------------------------------------------------------ */
/* Work variables                                                      */
/* ------------------------------------------------------------------ */

static void *g_field_cmd_work;   /* the command work of the field command running */

void *bw_mod_v6_set_field_cmd_work(void *cmd_work)
{
    void *was = g_field_cmd_work;

    g_field_cmd_work = cmd_work;
    return was;
}

static u16 *work_ptr(u16 id)
{
    void *gd = bw_mod_state_game_data();

    if (gd == NULL) {
        return NULL;
    }
    if (id >= 0x4000u && id < 0x4100u) {
        if (GAME_DATA_GetEventWork == NULL || ZONEDATA_GetEventWorkAddress == NULL) {
            return NULL;
        }
        return ZONEDATA_GetEventWorkAddress(
            (struct EventWork *)GAME_DATA_GetEventWork((struct GameData *)gd), id);
    }
    if (id >= 0x8000u && id < 0x8100u) {
        void *sw;

        if (g_field_cmd_work == NULL || SCRCMD_WORK_GetScriptWork == NULL ||
            SCRIPT_WORK_GetWorkPtr == NULL) {
            return NULL;
        }
        sw = SCRCMD_WORK_GetScriptWork(g_field_cmd_work);
        return sw != NULL ? SCRIPT_WORK_GetWorkPtr(sw, gd, id) : NULL;
    }
    return NULL;
}

int bw_mod_v6_work_get(u16 id, u16 *out)
{
    u16 *p = work_ptr(id);

    if (p == NULL || out == NULL) {
        return -1;
    }
    *out = *p;
    return 0;
}

int bw_mod_v6_work_set(u16 id, u16 value)
{
    u16 *p = work_ptr(id);

    if (p == NULL) {
        return -1;
    }
    *p = value;
    return 0;
}

int bw_mod_v6_badge_set(u32 bit)
{
    void *gd = bw_mod_state_game_data();
    void *misc;

    if (bit > 7u || gd == NULL || GAME_DATA_GetMiscSave == NULL) {
        return -1;
    }
    misc = GAME_DATA_GetMiscSave((struct GameData *)gd);
    if (misc == NULL) {
        return -1;
    }
    if (bw_slot_arm9_0200C248 != NULL) {
        bw_slot_arm9_0200C248(misc, bit);
    } else if (MISC_SetBadge != NULL) {
        MISC_SetBadge((struct MiscSave *)misc, bit);
    } else {
        return -1;
    }
    return 0;
}

/* The cartridge: sdk/langmap.h. */
extern const char *bw_lang_gamecode(void) __attribute__((weak));

int bw_mod_v6_game_code(char *out, u32 size)
{
    const char *code = bw_lang_gamecode != NULL ? bw_lang_gamecode() : "IRBO";
    u32 i;

    if (out == NULL || size == 0) {
        return -1;
    }
    for (i = 0; i < size && i < 4u; i++) {
        out[i] = code != NULL && code[0] != '\0' ? code[i] : "IRBO"[i];
    }
    if (size > 4u) {
        out[4] = '\0';
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Field scripts                                                       */
/* ------------------------------------------------------------------ */

#define SCRIPT_QUEUE_MAX 32
#define QUIET_FRAMES     10

typedef struct ScriptJob {
    char ns[64];
    u16  script_id;
    u32  ticket;
} ScriptJob;

static ScriptJob g_jobs[SCRIPT_QUEUE_MAX];
static u32       g_job_count;
static u32       g_quiet;          /* quiet field frames in a row */
static int       g_job_running;    /* a job's event is running */

/* The event's work: the job it runs. */
typedef struct ScriptRunWork {
    u32 ticket;
    u16 script_id;
    u16 pad;
} ScriptRunWork;

static int script_run_event(struct GameEvent *event, int *seq, void *work)
{
    ScriptRunWork *w = (ScriptRunWork *)work;

    switch (*seq) {
    case 0:
        (void)SCRIPT_SYS_CallScriptEvent(event, w->script_id, NULL, NULL, 0);
        (*seq)++;
        return 0;
    default: {
        BwEventPayload p;

        memset(&p, 0, sizeof(p));
        p.script_done.ticket = w->ticket;
        p.script_done.script_id = w->script_id;
        g_job_running = 0;
        (void)bw_mod_event_fire(BW_EVENT_SCRIPT_DONE, &p);
        return 1;
    }
    }
}

int bw_mod_v6_script_run_for(const char *ns, u16 script_id, u32 *ticket)
{
    ScriptJob *j;

    if (g_job_count == SCRIPT_QUEUE_MAX || GAME_EVENT_Create == NULL ||
        GAMESYSTEM_SetEvent == NULL) {
        return -1;
    }
    j = &g_jobs[g_job_count++];
    snprintf(j->ns, sizeof(j->ns), "%s", ns != NULL ? ns : "");
    j->script_id = script_id;
    j->ticket = next_ticket();
    if (ticket != NULL) {
        *ticket = j->ticket;
    }
    return 0;
}

int bw_mod_v6_script_run(u16 script_id, u32 *ticket)
{
    return bw_mod_v6_script_run_for(calling_ns(), script_id, ticket);
}

u32 bw_mod_v6_script_pending(void)
{
    return g_job_count + (g_job_running ? 1u : 0u);
}

static void script_frame(void)
{
    struct GameSystem *gs;
    struct GameEvent *ev;
    ScriptRunWork *w;

    if (g_job_count == 0) {
        g_quiet = 0;
        return;
    }
    gs = (struct GameSystem *)bw_mod_state_quiet_field();
    if (gs == NULL || g_job_running || SCRIPT_SYS_CallScriptEvent == NULL ||
        GAME_EVENT_GetWork == NULL) {
        g_quiet = 0;
        return;
    }
    if (++g_quiet < QUIET_FRAMES) {
        return;
    }
    g_quiet = 0;
    ev = GAME_EVENT_Create(gs, NULL, script_run_event, sizeof(ScriptRunWork));
    if (ev == NULL) {
        return;
    }
    w = GAME_EVENT_GetWork != NULL ? (ScriptRunWork *)GAME_EVENT_GetWork(ev) : NULL;
    if (w == NULL) {
        return;
    }
    w->ticket = g_jobs[0].ticket;
    w->script_id = g_jobs[0].script_id;
    memmove(&g_jobs[0], &g_jobs[1], (g_job_count - 1) * sizeof(g_jobs[0]));
    g_job_count--;
    g_job_running = 1;
    GAMESYSTEM_SetEvent(gs, ev);
}

/* ------------------------------------------------------------------ */
/* Text input                                                          */
/* ------------------------------------------------------------------ */

/*
 * text_input opens the game's own name entry screen (overlay 194, the
 * NameInArgs form NAMEIN_SETUP_CreateArgs makes) the way the field's
 * scripts do: a sub-process event under a game event of the port's own,
 * on a quiet field.  The screen works with the pad, the touch screen and
 * the port's keyboard input, as for any name.
 *
 * - The kind is the box name's (2): it can be left with B (the answer is
 *   then `cancelled`) and it writes nothing to the save.
 * - The screen's title is entry `kind` of the system text bank 81
 *   (a/0/0/2).  The mod's title goes there for the screen's life, in the
 *   port's own memory layer (sdk/vfs.h, root ""), and goes again after.
 * - The screen's own limit is 10 code units (the name buffer of
 *   NAMEIN_SETUP_CreateArgs holds 11 with the end code), so max_units is
 *   cut to 10.  A code answer keeps only the letters and the digits.
 *
 * symbols:
 *   NAMEIN_SETUP_CreateArgs           0x0216BFD4  thumb  ov010
 *   NAMEIN_SETUP_FreeArgs             0x0216C0C0  thumb  ov010
 *   NAMEIN_SETUP_IsCanceled           0x0216C0E4  thumb  ov010
 */
#define INPUT_KIND        2       /* the box name's screen */
#define INPUT_TITLE_BANK  0x51u   /* a/0/0/2 bank 81: the screen's titles */
#define INPUT_LIMIT       10u
#define INPUT_HEAP        4u
#define INPUT_OVERLAY     194u    /* the name entry screen's overlay */
#define INPUT_QUEUE_MAX   8

struct Strbuf;
struct FieldMap;
extern void *NAMEIN_SETUP_CreateArgs(unsigned short heapId, int kind, short species, short traits,
                                     int maxLen, struct Strbuf *initialName, void *pokedex)
    __attribute__((weak));
extern void NAMEIN_SETUP_FreeArgs(void *args) __attribute__((weak));
extern int NAMEIN_SETUP_IsCanceled(void *args) __attribute__((weak));
extern struct GameEvent *FIELD_STATUS_CreateSubProcEvent(struct GameSystem *gameSystem,
                                                         struct FieldMap *fieldMap, u32 overlayId,
                                                         void *procData, void *procWork)
    __attribute__((weak));
extern void GAME_EVENT_CallEvent(struct GameEvent *parent, struct GameEvent *child)
    __attribute__((weak));
extern struct GameSystem *GAME_EVENT_GetGameSystem(struct GameEvent *event) __attribute__((weak));
extern u16 STRBUF_GetLength(struct Strbuf *strbuf) __attribute__((weak));
extern u16 *STRBUF_GetChars(struct Strbuf *strbuf) __attribute__((weak));
/* the screen's process table: field data, bound while ov021 is loaded */
extern void *bw_dsref_ov021_gNameInProcData __attribute__((weak));
extern u32 bw_mod_message_encode(const char *utf8, u16 *out, u32 max_units) __attribute__((weak));

/* The game's NameInArgs (src/game/ov010/namein_args.h), field for field,
 * so the host compiler lays it out as the game's own code sees it. */
typedef struct PortNameInArgs {
    int kind;
    u16 species;
    u16 traits;
    void *mon;
    struct Strbuf *boxName;
    void *boxManager;
    int boxIndex;
    int maxLen;
    int canceled;
    struct Strbuf *name;
    void *saveIntr;
    void *pokedex;
} PortNameInArgs;

typedef struct InputJob {
    char ns[64];
    u16  title[64];
    u32  title_units;
    u32  max_units;
    u32  kind;
    u32  ticket;
} InputJob;

static InputJob g_inputs[INPUT_QUEUE_MAX];
static u32      g_input_count;
static int      g_input_phase;     /* 0 waiting, 1 title in, 2 screen open */
static u32      g_input_quiet;

typedef struct InputWork {
    void *args;
    u32   job;     /* always 0: the job at the queue's head */
} InputWork;

static void title_clear(void)
{
    (void)bw_mod_vfs_member_clear_for("", "a/0/0/2", INPUT_TITLE_BANK);
}

static void input_done(void *args)
{
    BwEventPayload p;
    InputJob *j = &g_inputs[0];
    u32 i, n = 0;

    memset(&p, 0, sizeof(p));
    p.text_input_done.ticket = j->ticket;
    if (args != NULL && NAMEIN_SETUP_IsCanceled != NULL && NAMEIN_SETUP_IsCanceled(args)) {
        p.text_input_done.cancelled = 1;
    } else if (args != NULL) {
        struct Strbuf *s = ((PortNameInArgs *)args)->name;
        u16 *units = s != NULL && STRBUF_GetChars != NULL ? STRBUF_GetChars(s) : NULL;
        u32 len = s != NULL && STRBUF_GetLength != NULL ? STRBUF_GetLength(s) : 0;

        for (i = 0; units != NULL && i < len && n < j->max_units; i++) {
            u16 u = units[i];

            if (j->kind == BW_TEXT_INPUT_CODE &&
                !((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z'))) {
                continue;
            }
            p.text_input_done.units[n++] = u;
        }
        p.text_input_done.cancelled = n == 0 ? 1u : 0u;
    } else {
        p.text_input_done.cancelled = 1;
    }
    p.text_input_done.count = (u16)n;
    if (NAMEIN_SETUP_FreeArgs != NULL && args != NULL) {
        NAMEIN_SETUP_FreeArgs(args);
    }
    title_clear();
    memmove(&g_inputs[0], &g_inputs[1], (g_input_count - 1) * sizeof(g_inputs[0]));
    g_input_count--;
    g_input_phase = 0;
    (void)bw_mod_event_fire(BW_EVENT_TEXT_INPUT_DONE, &p);
}

static int input_event(struct GameEvent *event, int *seq, void *work)
{
    InputWork *w = (InputWork *)work;

    switch (*seq) {
    case 0: {
        struct GameSystem *gs = GAME_EVENT_GetGameSystem(event);
        struct GameEvent *child;

        w->args = NAMEIN_SETUP_CreateArgs((unsigned short)INPUT_HEAP, INPUT_KIND, 0, 0,
                                          (int)g_inputs[0].max_units, NULL, NULL);
        child = w->args == NULL ? NULL
            : FIELD_STATUS_CreateSubProcEvent(gs, *(struct FieldMap **)((u8 *)gs + 0x20),
                                              INPUT_OVERLAY, bw_dsref_ov021_gNameInProcData,
                                              w->args);
        if (child == NULL) {
            input_done(w->args);
            return 1;
        }
        GAME_EVENT_CallEvent(event, child);
        (*seq)++;
        return 0;
    }
    default:
        input_done(w->args);
        return 1;
    }
}

int bw_mod_v6_text_input_for(const char *ns, const char *title, u32 max_units, u32 kind,
                             u32 *ticket)
{
    InputJob *j;

    if (g_input_count == INPUT_QUEUE_MAX || kind > BW_TEXT_INPUT_LINK || max_units == 0 ||
        NAMEIN_SETUP_CreateArgs == NULL || FIELD_STATUS_CreateSubProcEvent == NULL ||
        GAME_EVENT_Create == NULL || GAMESYSTEM_SetEvent == NULL) {
        return -1;
    }
    j = &g_inputs[g_input_count++];
    memset(j, 0, sizeof(*j));
    snprintf(j->ns, sizeof(j->ns), "%s", ns != NULL ? ns : "");
    if (title != NULL && bw_mod_message_encode != NULL) {
        j->title_units = bw_mod_message_encode(title, j->title, 63u);
    }
    j->max_units = max_units > INPUT_LIMIT ? INPUT_LIMIT : max_units;
    j->kind = kind;
    j->ticket = next_ticket();
    if (ticket != NULL) {
        *ticket = j->ticket;
    }
    return 0;
}

int bw_mod_v6_text_input(const char *title, u32 max_units, u32 kind, u32 *ticket)
{
    return bw_mod_v6_text_input_for(calling_ns(), title, max_units, kind, ticket);
}

static void input_frame(void)
{
    struct GameSystem *gs;
    struct GameEvent *ev;
    InputWork *w;

    if (g_input_count == 0 || g_input_phase == 2) {
        return;
    }
    gs = (struct GameSystem *)bw_mod_state_quiet_field();
    if (gs == NULL || g_job_running || &bw_dsref_ov021_gNameInProcData == NULL ||
        bw_dsref_ov021_gNameInProcData == NULL) {
        g_input_quiet = 0;
        return;
    }
    if (g_input_phase == 0) {
        if (++g_input_quiet < QUIET_FRAMES) {
            return;
        }
        g_input_quiet = 0;
        /* the title, in every block of the bank: it is in place for the
         * screen once this frame's swaps are done */
        if (g_inputs[0].title_units > 0) {
            (void)bw_mod_text_set_for("", 2, INPUT_TITLE_BANK, INPUT_KIND, 0,
                                      g_inputs[0].title, g_inputs[0].title_units);
            (void)bw_mod_text_set_for("", 2, INPUT_TITLE_BANK, INPUT_KIND, 1,
                                      g_inputs[0].title, g_inputs[0].title_units);
        }
        g_input_phase = 1;
        return;
    }
    ev = GAME_EVENT_Create(gs, NULL, input_event, sizeof(InputWork));
    if (ev == NULL || GAME_EVENT_GetWork == NULL) {
        return;
    }
    w = (InputWork *)GAME_EVENT_GetWork(ev);
    memset(w, 0, sizeof(*w));
    g_input_phase = 2;
    GAMESYSTEM_SetEvent(gs, ev);
}

u32 bw_mod_v6_text_input_pending(void)
{
    return g_input_count;
}

/* ------------------------------------------------------------------ */
/* The type chart                                                      */
/* ------------------------------------------------------------------ */

/*
 * The chart is the battle overlay's data (gBtlCalcTypeAffinityTable,
 * u8[17][17] in ov093), found by its name: the overlay link binds the
 * name to the table's place while ov093 is loaded.  The overlay's data is
 * copied in again at every load, so the chart in force is written over it
 * each time ov093 loads (bw_mod_v6_overlay_loaded) and at once when a mod
 * sets it during a battle.  The game's own chart is kept from the first
 * load, for type_chart_get and for a mod that clears its chart.
 *
 * With the types mods add, the game reads a chart of count x count
 * (sdk/type_chart.c), and a chart of this service is its first 17 rows
 * and columns: it is written and read row by row.
 */
extern void *bw_dsref_ov093_gBtlCalcTypeAffinityTable __attribute__((weak));
extern void bw_event_fire_starter_choices(void);
extern int bw_overlay_is_loaded(u32 id) __attribute__((weak));
extern const char *bw_mod_loader_namespace_at(u32 index) __attribute__((weak));
extern u32 bw_mod_loader_count(void) __attribute__((weak));

#define CHART_MODS_MAX 16

typedef struct ChartMod {
    char        ns[64];
    BwTypeChart chart;
} ChartMod;

static ChartMod   *g_charts;     /* allocated at the first set: the image limit */
static u32         g_chart_count;
static BwTypeChart g_game_chart;
static int         g_have_game_chart;

/* Copy the 17 x 17 block between a chart of `stride` columns and a
 * service chart. */
static void block_out(u8 *t, u32 stride, const u8 *cells)
{
    u32 r;

    for (r = 0; r < BW_TYPE_CHART_TYPES; r++) {
        memcpy(t + r * stride, cells + r * BW_TYPE_CHART_TYPES, BW_TYPE_CHART_TYPES);
    }
}

static void block_in(u8 *cells, const u8 *t, u32 stride)
{
    u32 r;

    for (r = 0; r < BW_TYPE_CHART_TYPES; r++) {
        memcpy(cells + r * BW_TYPE_CHART_TYPES, t + r * stride, BW_TYPE_CHART_TYPES);
    }
}

/* Set while ov093's names are bound and the overlay not yet counted as
 * loaded (bw_mod_v6_overlay_loaded runs inside the load). */
static int g_ov093_binding;

static u8 *chart_place(void)
{
    if (&bw_dsref_ov093_gBtlCalcTypeAffinityTable == NULL ||
        bw_dsref_ov093_gBtlCalcTypeAffinityTable == NULL ||
        (!g_ov093_binding && bw_overlay_is_loaded != NULL && !bw_overlay_is_loaded(93u))) {
        return NULL;
    }
    return (u8 *)bw_dsref_ov093_gBtlCalcTypeAffinityTable;
}

/* The chart in force: the one of the mod latest in the load order. */
static const BwTypeChart *chart_in_force(void)
{
    const BwTypeChart *pick = NULL;
    u32 i, k, n;

    if (g_chart_count == 0) {
        return NULL;
    }
    n = bw_mod_loader_count != NULL ? bw_mod_loader_count() : 0;
    for (i = 0; i < n; i++) {
        const char *ns = bw_mod_loader_namespace_at(i);

        for (k = 0; ns != NULL && k < g_chart_count; k++) {
            if (strcmp(g_charts[k].ns, ns) == 0) {
                pick = &g_charts[k].chart;
            }
        }
    }
    /* a chart set outside the loader (the tests): the last one set */
    return pick != NULL ? pick : &g_charts[g_chart_count - 1].chart;
}

static void chart_write(void)
{
    u8 *t = chart_place();
    const BwTypeChart *c;

    if (t == NULL) {
        return;
    }
    if (!g_have_game_chart) {
        block_in(g_game_chart.cell, t, bw_type_count());
        g_have_game_chart = 1;
    }
    c = chart_in_force();
    if (c != NULL) {
        block_out(t, bw_type_count(), c->cell);
    } else if (bw_type_chart_built() != NULL) {
        /* the chart with the mods' types and rows */
        memcpy(t, bw_type_chart_built(), bw_type_count() * bw_type_count());
    } else {
        block_out(t, BW_TYPE_CHART_TYPES, g_game_chart.cell);
    }
}

void bw_mod_v6_overlay_loaded(u32 id)
{
    /* the starter screen's overlay: its three species are in place
     * (sdk/mod_event_v6.c) */
    if (id == 223u) {
        bw_event_fire_starter_choices();
    }
    /* the per-type tables of the battle's buttons and the Hall of Fame
     * take the added types (sdk/type_icons.c) */
    bw_type_icons_overlay_loaded(id);
    if (id == 93u) {
        /* the copy just made is the game's own chart */
        u8 *t;

        g_ov093_binding = 1;
        t = chart_place();
        if (t != NULL) {
            memcpy(g_game_chart.cell, t, sizeof(g_game_chart.cell));
            g_have_game_chart = 1;
            /* the types and rows mods add: the game reads that chart now */
            bw_type_chart_loaded(g_game_chart.cell);
        }
        if (g_chart_count > 0) {
            chart_write();
        }
        g_ov093_binding = 0;
    }
}

int bw_mod_v6_type_chart_set_for(const char *ns, const BwTypeChart *chart)
{
    u32 i;

    if (chart == NULL) {
        /* NULL: the mod's chart goes, the game's comes back */
        for (i = 0; i < g_chart_count; i++) {
            if (strcmp(g_charts[i].ns, ns != NULL ? ns : "") == 0) {
                g_charts[i] = g_charts[--g_chart_count];
                chart_write();
                return 0;
            }
        }
        return 0;
    }
    for (i = 0; i < 289u; i++) {
        u8 v = chart->cell[i];

        if (v != 0 && v != 2 && v != 4 && v != 8) {
            fprintf(stderr, "eidos: mod %s: type_chart_set: cell %u holds %u, not 0, 2, 4 or 8\n",
                    ns != NULL ? ns : "?", i, v);
            return -1;
        }
    }
    for (i = 0; i < g_chart_count; i++) {
        if (strcmp(g_charts[i].ns, ns != NULL ? ns : "") == 0) {
            break;
        }
    }
    if (i == g_chart_count) {
        if (g_charts == NULL) {
            g_charts = (ChartMod *)calloc(CHART_MODS_MAX, sizeof(ChartMod));
        }
        if (g_charts == NULL || g_chart_count == CHART_MODS_MAX) {
            return -1;
        }
        snprintf(g_charts[i].ns, sizeof(g_charts[i].ns), "%s", ns != NULL ? ns : "");
        g_chart_count++;
    }
    g_charts[i].chart = *chart;
    chart_write();
    return 0;
}

int bw_mod_v6_type_chart_set(const BwTypeChart *chart)
{
    return bw_mod_v6_type_chart_set_for(calling_ns(), chart);
}

int bw_mod_v6_type_chart_get(BwTypeChart *out)
{
    const BwTypeChart *c = chart_in_force();
    u8 *t;

    if (out == NULL) {
        return -1;
    }
    if (c != NULL) {
        *out = *c;
        return 0;
    }
    t = chart_place();
    if (t != NULL) {
        block_in(out->cell, t, bw_type_count());
        return 0;
    }
    if (g_have_game_chart) {
        *out = g_game_chart;
        return 0;
    }
    return -1;      /* no battle has loaded the game's chart yet */
}

/* ------------------------------------------------------------------ */
/* The loader's way down, and the frame                                */
/* ------------------------------------------------------------------ */

void bw_mod_v6_remove_for(const char *ns, const char *root)
{
    u32 i = 0;

    if (root != NULL && root[0] != '\0') {
        (void)bw_mod_vfs_member_clear_for(root, "", BW_VFS_MEM_ALL);
    }
    if (ns != NULL) {
        u32 k;

        for (k = 0; k < g_chart_count; k++) {
            if (strcmp(g_charts[k].ns, ns) == 0) {
                g_charts[k] = g_charts[--g_chart_count];
                chart_write();
                break;
            }
        }
    }
    while (ns != NULL && i < g_job_count) {
        if (strcmp(g_jobs[i].ns, ns) == 0) {
            memmove(&g_jobs[i], &g_jobs[i + 1], (g_job_count - i - 1) * sizeof(g_jobs[0]));
            g_job_count--;
            continue;
        }
        i++;
    }
}

void bw_mod_v6_frame(void)
{
    bw_mod_vfs_frame();
    script_frame();
    input_frame();
    bw_mod_world_frame();
}

/* ------------------------------------------------------------------ */
/* The player's multiplayer name                                       */
/* ------------------------------------------------------------------ */

extern const char *bw_mod_net_name(void);   /* sdk/mod_net.h */

/*
 * player_name: the name the player chose on the Multiplayer screen (the
 * name the other players see), so a mod asks for it nowhere else.  The
 * network layer answers "player" while none is chosen: that is 0 here.
 */
int bw_mod_v6_player_name(char *out, u32 size)
{
    const char *n = bw_mod_net_name();

    if (out == NULL || size == 0) {
        return -1;
    }
    if (n == NULL || strcmp(n, "player") == 0 || strcmp(n, "Player") == 0) {
        n = "";
    }
    snprintf(out, size, "%s", n);
    return (int)strlen(out);
}
