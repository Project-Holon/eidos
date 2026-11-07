/*
 * sdk/mod_timer.c -- the timer and the splits, and the mod's own random
 * source (docs/mod-services.md, "Timer and splits" and "Seeded random
 * source"; docs/mod-implementation.md, section 4, step 8).
 *
 * What this file is
 * -----------------
 *
 * Two of the port's services, and the port's own list of splits:
 *
 *   - the timer: the elapsed time of a run, and the splits the run records
 *     along the way;
 *   - the random source: a generator of the mod's own, separate from the
 *     game's, so that a mod's choices do not change the game's rolls.
 *
 * The rules the services keep
 * ---------------------------
 *
 *   - The timer counts real time while the game runs, and it is paused
 *     while the port's own menu is open (docs/mod-services.md, "Timer and
 *     splits").  The port's own menu is the settings screen, the one the
 *     player enables, disables and orders the mods in (docs/mod-rules.md,
 *     section 1); the title-menu hooks say when that screen is up
 *     (sdk/mod_title.h).
 *   - A split records a name and the time.  The port shows the splits in
 *     the run-status panel and writes them to a file at the end of the run.
 *     The panel is here now (sdk/mod_panel.c), so the splits live in this
 *     file's own list and the port's own calls put them in the panel of
 *     the mod whose run they are.
 *   - The random source is the mod's own, and there is one per mod: two
 *     players who share a seed get the same sequence, so a draft or a
 *     random team is the same on both sides.
 *
 * One timer and one generator per mod
 * ------------------------------------
 *
 * The services table is filled per mod (sdk/mod_loader.c gives every mod a
 * table of its own) and the loader binds the same functions into every one
 * of them, so the state cannot be one set of variables: two mods that both
 * time their runs would share one timer, and two mods that both draw would
 * share one generator.  The port says which mod is calling the way it says
 * so everywhere else: the loader sets the mod's state namespace around
 * every call it makes into a mod (loader_enter_mod), and the namespace is
 * the mod's own name.  This file keys its states on it.
 *
 * The clock
 * ---------
 *
 * The port's clock is the frame loop's tick (engine/frame/frame.h): the
 * SDK's tick, the system clock divided by 64, 523655.96875 Hz, which is
 * what OS_GetTick() counts on the DS.  The timer measures the port's tick,
 * so the time it reports is the time the game ran, at the DS's rate, and a
 * headless run that steps frames without sleeping measures the same as a
 * paced one does.
 *
 * The pause
 * ---------
 *
 * The timer is paused while the port's own menu is open.  The pause is
 * exact to the sampling interval: the port calls bw_mod_timer_frame() once
 * a frame (the frame hook, where the on_frame service will run too), and
 * the time the menu was open for is dropped when the timer is sampled
 * again.  A port that never calls the frame hook still gets a timer,
 * sampled at the times the mod asks it.
 *
 * The file
 * --------
 *
 * The port's own calls (the ones below the services) are not part of the
 * mod ABI: a mod reaches the timer and the random source through the
 * services table (sdk/mod_state.h), and the loader binds the six calls
 * below into it.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "mod_panel.h"
#include "mod_title.h"
#include "save.h"

/* A label built from values in a mod's words (engine/ui/ui_lang.h ui_trf);
 * the English when the interface is not linked in. */
extern const char *ui_trf(const char *fmt) __attribute__((weak, format_arg(1)));
#define TRF(s) (ui_trf != NULL ? ui_trf(s) : (s))

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The mod that is calling: the loader sets the namespace around every call
 * it makes into a mod, so a service call always knows whose it is (the
 * loader says the same about the saved-data layer, sdk/mod_loader.c).
 * The saved-data layer has no header yet, so the prototype is here.
 */
extern const char *bw_mod_savedata_namespace(void);

/*
 * The port's own clock: the frame loop's tick and the conversion to
 * milliseconds (engine/frame/frame.h).  The header is not included here so
 * that this file needs nothing but the sdk's own include path; the loop's
 * own layout is none of this file's business, so the loop is an opaque
 * pointer here.  The names and the units are the header's: the tick is the
 * SDK's tick (OS_GetTick's unit, EIDOS_TICKS_PER_SECOND Hz) and
 * eidos_ticks_to_ms converts it the way the SDK's macros do.
 */
extern u64  eidos_frame_tick(const void *loop);
extern void *eidos_frame_current(void);
extern u64  eidos_ticks_to_ms(u64 ticks);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many mods may hold a timer and a generator at once.  The loader
 * holds sixteen mods with code, so sixteen states are enough. */
#define BW_MOD_TIMER_MAX 16

/* The longest name a split may carry, and the longest namespace a state
 * may carry (the mod's own name). */
#define BW_MOD_SPLIT_NAME_MAX 48
#define BW_MOD_TIMER_NS_MAX   64

/* How many splits one run may record.  The run-status panel shows sixteen
 * lines, so this is more than the panel can show and the file keeps all. */
#define BW_MOD_SPLITS_MAX 32

/* The path buffers of the splits file. */
#define BW_MOD_SPLITS_PATH_MAX 1200
#define BW_MOD_SPLITS_NAME_MAX  160

/* The buffer the splits file is built in.  A header and thirty-two lines of
 * a name and a time fit in it. */
#define BW_MOD_SPLITS_TEXT_MAX 4096

/*
 * The seed a generator that was never seeded starts from: a fixed one, so
 * that the sequence a mod draws without seeding is the same on every
 * platform and in every run (the reason is under "The random source").
 */
#define BW_MOD_RAND_DEFAULT_SEED UINT64_C(0x2545F4914F6CDD1D)

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* One split: the name and the time it was taken at. */
typedef struct BwModSplit {
    char name[BW_MOD_SPLIT_NAME_MAX + 1];
    u64  ms;
} BwModSplit;

/*
 * One mod's state: its timer, its splits and its generator.  The key is
 * the namespace, the mod's own name; the port's own calls (the panel, the
 * run's end) name a mod by it.
 */
typedef struct BwModTimerState {
    char ns[BW_MOD_TIMER_NS_MAX + 1];
    int  used;

    /* The timer. */
    int  running;        /* the timer counts                        */
    int  sampled;        /* the last sample is a real one           */
    int  menu_open;      /* the port's own menu was open at it      */
    int  started;        /* the timer was started at least once     */
    u64  elapsed_ticks;  /* the ticks counted so far                */
    u64  last_tick;      /* the tick at the last sample             */
    u32  split_count;    /* the splits of this run                  */
    u32  panel_splits;   /* the split lines the panel holds         */
    BwModSplit splits[BW_MOD_SPLITS_MAX];

    /* The generator. */
    u64  rand_state;
} BwModTimerState;

static BwModTimerState g_states[BW_MOD_TIMER_MAX];

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

/* The namespace of the mod that is calling, or "" when there is none (the
 * port itself is calling). */
static const char *current_namespace(void)
{
    const char *ns = bw_mod_savedata_namespace();

    return (ns != NULL) ? ns : "";
}

/* The state of the mod named by `ns`, created when it is not there yet.
 * NULL when the table is full. */
static BwModTimerState *state_for(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        ns = "";
    }
    for (i = 0; i < BW_MOD_TIMER_MAX; i++) {
        if (g_states[i].used && strcmp(g_states[i].ns, ns) == 0) {
            return &g_states[i];
        }
    }
    for (i = 0; i < BW_MOD_TIMER_MAX; i++) {
        if (g_states[i].used) {
            continue;
        }
        memset(&g_states[i], 0, sizeof(g_states[i]));
        copy_text(g_states[i].ns, sizeof(g_states[i].ns), ns);
        g_states[i].used = 1;
        g_states[i].rand_state = BW_MOD_RAND_DEFAULT_SEED;
        return &g_states[i];
    }
    return NULL;
}

/* The port's tick now. */
static u64 now_tick(void)
{
    return eidos_frame_tick(eidos_frame_current());
}

/* Is the port's own menu open? */
static int menu_open(void)
{
    return bw_mod_title_in_settings() ? 1 : 0;
}

/*
 * Fold the time that has passed into the timer.
 *
 * The interval between two samples counts when the timer runs and when the
 * port's own menu was closed at both ends of it: an interval that touches
 * the menu is dropped whole, so a run's time is never made shorter by the
 * port's own menu being open.
 *
 * The port calls bw_mod_timer_frame() once a frame, so the pause is exact
 * to the frame.  A port that does not call it, and a mod that asks the
 * timer rarely, get an interval that is dropped whole whenever the menu
 * was open at one of its ends.
 */
static void sample(BwModTimerState *t)
{
    u64 now = now_tick();
    int open = menu_open();

    if (!t->sampled) {
        t->sampled = 1;
        t->last_tick = now;
        t->menu_open = open;
        return;
    }
    if (now != t->last_tick) {
        if (t->running && !open && !t->menu_open && now > t->last_tick) {
            t->elapsed_ticks += now - t->last_tick;
        }
        t->last_tick = now;
    }
    t->menu_open = open;
}

/* ------------------------------------------------------------------ */
/* The timer                                                           */
/* ------------------------------------------------------------------ */

/*
 * The `timer_start` call of the services table: a run begins.  The elapsed
 * time goes back to zero, the clock counts from here, and the splits of the
 * run before it go with it.
 */
void bw_mod_timer_start(void)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL) {
        return;
    }
    t->running = 1;
    t->started = 1;
    t->elapsed_ticks = 0;
    t->split_count = 0;
    t->sampled = 1;
    t->last_tick = now_tick();
    t->menu_open = menu_open();
}

/*
 * The `timer_stop` call: the run is over.  The elapsed time keeps the value
 * it has and the timer stops counting.
 */
void bw_mod_timer_stop(void)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL) {
        return;
    }
    sample(t);
    t->running = 0;
}

/*
 * The `timer_ms` call: the elapsed time of the run in milliseconds.  It is
 * zero before the first start.
 */
u64 bw_mod_timer_ms(void)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL) {
        return 0;
    }
    sample(t);
    return eidos_ticks_to_ms(t->elapsed_ticks);
}

/*
 * The `split` call: record a name and the time it was taken at.  A split
 * that comes when the list is full is dropped; the run keeps its time.
 */
void bw_mod_timer_split(const char *name)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL || name == NULL) {
        return;
    }
    sample(t);
    if (t->split_count >= BW_MOD_SPLITS_MAX) {
        return;
    }
    copy_text(t->splits[t->split_count].name,
              sizeof(t->splits[t->split_count].name), name);
    t->splits[t->split_count].ms = eidos_ticks_to_ms(t->elapsed_ticks);
    t->split_count++;
}

/* ------------------------------------------------------------------ */
/* The run-status panel                                                */
/* ------------------------------------------------------------------ */

/*
 * The time as the panel shows it: hours, minutes, seconds and hundredths,
 * "H:MM:SS.mm".  The hours are always there, so every line of the panel
 * is the same width and the times line up.
 */
static void format_time(char *out, size_t size, u64 ms)
{
    u64 secs = ms / 1000u;
    unsigned hours = (unsigned)(secs / 3600u);
    unsigned mins = (unsigned)((secs / 60u) % 60u);
    unsigned secs_left = (unsigned)(secs % 60u);
    unsigned cents = (unsigned)((ms % 1000u) / 10u);

    snprintf(out, size, "%u:%02u:%02u.%02u", hours, mins, secs_left, cents);
}

/*
 * The run's own lines in the run-status panel: the running time on the
 * port's own line zero, and the splits on the lines after it.
 *
 * The panel's lines are the mod's own, so the port's lines are the
 * port's: the model keeps the two apart (sdk/mod_panel.c) and draws the
 * port's own line on a line the mod has not used.  The panel of the
 * mod the run belongs to is the panel of that mod's namespace; the port's
 * own run, the one the port itself times, is under the port's own
 * namespace, the empty one (BW_MOD_PANEL_PORT_NS).
 *
 * The splits are shown in the order they were taken.  A panel has
 * sixteen lines, so a run with more splits than that shows the ones that
 * fit; the file the port writes at the end of the run keeps all of them
 * (docs/mod-services.md, "Timer and splits").
 */
static void panel_update(BwModTimerState *t)
{
    /*
     * The line the panel shows.  The buffer is longer than the model's
     * own line, which cuts the text at its own length
     * (BW_MOD_PANEL_TEXT_MAX): the time comes first, so a name that is
     * too long is the part that is cut.
     */
    char text[BW_MOD_PANEL_TEXT_MAX + 64];
    char stamp[32];
    u32 i;

    if (!t->started) {
        return;                     /* the run never began */
    }

    /* The running time. */
    format_time(stamp, sizeof(stamp), eidos_ticks_to_ms(t->elapsed_ticks));
    snprintf(text, sizeof(text), TRF("Time %s"), stamp);
    bw_mod_panel_set_port(t->ns, 0, text);

    /* The splits. */
    for (i = 0; i < t->split_count; i++) {
        format_time(stamp, sizeof(stamp), t->splits[i].ms);
        snprintf(text, sizeof(text), "%s %s", stamp, t->splits[i].name);
        bw_mod_panel_set_port(t->ns, 1u + i, text);
    }
    /* The splits of the run before this one are gone: the lines they were
     * on are cleared. */
    for (i = t->split_count + 1u; i <= t->panel_splits; i++) {
        bw_mod_panel_set_port(t->ns, i, "");
    }
    t->panel_splits = t->split_count;
}

/* ------------------------------------------------------------------ */
/* The random source                                                   */
/* ------------------------------------------------------------------ */

/*
 * The generator: SplitMix64.
 *
 * The state is one 64-bit word.  A draw adds the golden ratio's odd
 * multiple to the state and mixes the new state with two multiplications
 * and three shifts:
 *
 *     state += 0x9E3779B97F4A7C15
 *     z = state
 *     z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
 *     z = (z ^ (z >> 27)) * 0x94D049BB133111EB
 *     output = z ^ (z >> 31)
 *
 * It is Steele, Lea and Flood's SplitMix64 ("Fast Splittable Pseudorandom
 * Number Generators", OOPSLA 2014), the generator the reference
 * implementation of the name ships.  It has a period of 2^64, it passes
 * BigCrush, and it is eight lines long, which is what a port wants: the
 * whole algorithm is here, so two players who share a seed get the same
 * sequence on every platform, whatever the host's C library does.
 *
 * The arithmetic is unsigned 64-bit, so it wraps the way the standard says
 * it does, and the constants are written as they are in the paper.  The
 * value rand_next() hands out is the high half of the 64-bit output.
 *
 * A mod that never seeds draws from a generator started at the fixed seed
 * BW_MOD_RAND_DEFAULT_SEED: the port does not guess a seed for a mod,
 * because a seed that changes from run to run is not a seed two players
 * can share.
 */

/* One step of the generator. */
static u64 splitmix64(u64 *state)
{
    u64 z;

    *state += UINT64_C(0x9E3779B97F4A7C15);
    z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/*
 * The `rand_seed` call: the mod's generator starts again from `seed`.  Two
 * players who share a seed and draw the same number of times get the same
 * numbers.
 */
void bw_mod_rand_seed(u64 seed)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL) {
        return;
    }
    t->rand_state = seed;
}

/*
 * The `rand_next` call: the next number of the mod's own generator.  It is
 * the high half of the 64-bit output of one step of SplitMix64.
 */
u32 bw_mod_rand_next(void)
{
    BwModTimerState *t = state_for(current_namespace());

    if (t == NULL) {
        return 0;
    }
    return (u32)(splitmix64(&t->rand_state) >> 32);
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own calls below are not part of the mod ABI.  A mod reaches
 * the timer through the services table (sdk/mod_state.h) and the loader
 * binds the six calls above into it; the calls here are the port's, and
 * they name a mod by its namespace.
 */

/* How many splits the mod has recorded. */
u32 bw_mod_timer_splits_count(const char *ns)
{
    BwModTimerState *t = state_for(ns);

    return (t == NULL) ? 0u : t->split_count;
}

/*
 * One split: its name and the time it was taken at.  0 on success, -1 when
 * there is no such split.
 */
int bw_mod_timer_split_at(const char *ns, u32 index, char *name, u32 name_size,
                          u64 *ms)
{
    BwModTimerState *t = state_for(ns);

    if (t == NULL || index >= t->split_count) {
        return -1;
    }
    if (name != NULL && name_size > 0) {
        copy_text(name, (size_t)name_size, t->splits[index].name);
    }
    if (ms != NULL) {
        *ms = t->splits[index].ms;
    }
    return 0;
}

/* The elapsed time of a mod's run, in milliseconds. */
u64 bw_mod_timer_elapsed_ms(const char *ns)
{
    BwModTimerState *t = state_for(ns);

    if (t == NULL) {
        return 0;
    }
    sample(t);
    return eidos_ticks_to_ms(t->elapsed_ticks);
}

/*
 * The port's frame tick: the port calls this once a frame, where the
 * on_frame service runs, so that the pause while the port's own menu is
 * open is exact to the frame.  Every mod's timer is sampled; a timer that
 * no mod asks for is sampled anyway.
 *
 * The run-status panel is filled here too: the running time and the
 * splits of every run that has begun are on the panel of the mod the run
 * belongs to, and the port's own run is on the panel of the port's own
 * namespace.
 */
void bw_mod_timer_frame(void)
{
    u32 i;

    for (i = 0; i < BW_MOD_TIMER_MAX; i++) {
        if (g_states[i].used) {
            sample(&g_states[i]);
            panel_update(&g_states[i]);
        }
    }
}

/*
 * The splits of a run, written to a file: the whole file, atomically
 * (bw_save_write_atomic, sdk/save.h), so a crash leaves the old file or
 * the new one and never a torn one.
 *
 * The file is plain text: a header line, the mod's namespace, and one line
 * per split, the time in milliseconds and the name.
 */
int bw_mod_timer_splits_write(const char *ns, const char *path)
{
    static char text[BW_MOD_SPLITS_TEXT_MAX];
    BwModTimerState *t = state_for(ns);
    size_t used = 0;
    u32 i;
    int n;

    if (t == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }
    n = snprintf(text, sizeof(text), "eidos-splits 1\nmod %s\n", t->ns);
    if (n < 0 || (size_t)n >= sizeof(text)) {
        return -1;
    }
    used = (size_t)n;
    for (i = 0; i < t->split_count; i++) {
        n = snprintf(text + used, sizeof(text) - used, "%llu %s\n",
                     (unsigned long long)t->splits[i].ms,
                     t->splits[i].name);
        if (n < 0 || (size_t)n >= sizeof(text) - used) {
            return -1;
        }
        used += (size_t)n;
    }
    return bw_save_write_atomic(path, text, (u32)used);
}

/*
 * A namespace is one path component of the file name, so it has to be a
 * name a file may carry: letters, digits, dots, dashes and underscores,
 * and not "." or "..".  A namespace that is not one of those is reported
 * and the file is called "default.txt".
 */
static void splits_file_name(char *out, size_t size, const char *ns)
{
    const char *p;
    size_t n = 0;

    if (ns == NULL || *ns == '\0') {
        copy_text(out, size, "default");
        return;
    }
    for (p = ns; *p != '\0'; p++) {
        char c = *p;

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
            copy_text(out, size, "default");
            return;
        }
        if (n + 1 >= size) {
            break;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    if (n == 0 || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) {
        copy_text(out, size, "default");
    }
}

/*
 * The same, into the save folder: <save dir>/splits/<namespace>.txt.  The
 * port calls this at the end of a run, which is where the splits file is
 * written (docs/mod-services.md, "Timer and splits").
 */
int bw_mod_timer_splits_write_default(const char *ns)
{
    char file[BW_MOD_SPLITS_NAME_MAX];
    char path[BW_MOD_SPLITS_PATH_MAX];
    char full[BW_MOD_SPLITS_PATH_MAX];
    int n;

    splits_file_name(file, sizeof(file), ns);
    n = snprintf(path, sizeof(path), "splits/%s.txt", file);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }
    if (bw_save_path(full, sizeof(full), path) != 0) {
        return -1;
    }
    return bw_mod_timer_splits_write(ns, full);
}

/*
 * Forget every timer, every split and every generator: at power-on and at
 * every reset (bw_port_reset).  The files on the disk are untouched.
 */
void bw_mod_timer_reset(void)
{
    memset(g_states, 0, sizeof(g_states));
}
