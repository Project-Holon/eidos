/*
 * sdk/mod_wasm.c -- code mods built for WebAssembly (mod_wasm.h,
 * docs/mod-code-wasm.md).
 *
 * The runtime is WAMR's interpreter (tools/fetch_wamr.py, cmake/wamr/).  One
 * runtime serves every mod; each mod is one module instance with its own
 * linear memory and its own 64 KiB stack.  The port grows the module's memory
 * by one page right after instantiation and copies payloads there: the
 * pages the module declared and the pages it grows later stay its own.
 *
 * What crosses the boundary is values, never host pointers
 * (docs/mod-code-wasm.md, "Seams"): an event payload is copied into the
 * module's memory, the handler runs on the copy, and the copy comes back
 * (the event layer applies only the fields an event marks writable).  A
 * game address is a DS address, read and written through the bounds-checked
 * memory imports.
 *
 * Not in this first version (docs/mod-backlog.md): overrides and `original`
 * (the hooks of hooks.json), and the session, command and save-received
 * handlers.  The imports exist, answer -1 and say so once.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_wasm.h"

#if defined(EIDOS_HAVE_WAMR)

#include "wasm_export.h"

#include "dsmem.h"
#include "mod_frame.h"
#include "mod_state.h"

/* The stack of one instance, and the pages the port adds to its memory for
 * payload copies.  WAMR's own app heap is not used: WAMR places it at the
 * module's __heap_base, inside the memory the module's allocator owns. */
#define BW_WASM_STACK  (64u * 1024u)
#define BW_WASM_PAGE   65536u
#define BW_WASM_SCRATCH_PAGES 1u

/* The event kinds a handler index is kept for (the ids are below 256). */
#define BW_WASM_KINDS  256u

/* The script handlers one mod may bind. */
#define BW_WASM_SCRIPTS 64u

/* The mods that may hold a frame handler at once. */
#define BW_WASM_FRAME_SLOTS 16u

typedef struct BwWasmScript {
    u32 vm;
    u16 opcode;
    u32 index;
} BwWasmScript;

typedef struct BwWasmMod {
    char                label[96];
    u8                 *bytes;          /* the module file: WAMR keeps it */
    wasm_module_t       module;
    wasm_module_inst_t  inst;
    wasm_exec_env_t     env;
    const BwModHost    *host;
    s32                 handler[BW_WASM_KINDS];   /* table index, -1 none */
    BwWasmScript        scripts[BW_WASM_SCRIPTS];
    u32                 script_count;
    s32                 frame_slot;     /* the frame trampoline, -1 none */
    u32                 frame_index;
    u32                 session_index;
    u32                 command_index;
    int                 said_unsupported;
    u32                 scratch_base;   /* the port's page: payload copies */
    u32                 scratch_size;
    u32                 scratch_used;
} BwWasmMod;

static int g_runtime_ready;

int bw_mod_wasm_available(void)
{
    return 1;
}

static BwWasmMod *wasm_mod_of(wasm_exec_env_t env)
{
    return env != NULL ? (BwWasmMod *)wasm_runtime_get_user_data(env) : NULL;
}

/* A thread the port runs game code on that has not called into WAMR yet
 * gets WAMR's per-thread setup first. */
static void wasm_thread_ready(void)
{
    if (!wasm_runtime_thread_env_inited()) {
        (void)wasm_runtime_init_thread_env();
    }
}

static void say_unsupported(BwWasmMod *w, const char *what)
{
    if (w != NULL && !w->said_unsupported) {
        fprintf(stderr, "eidos: mod %s: %s is not available to wasm mods yet "
                "(docs/mod-backlog.md)\n", w->label, what);
        w->said_unsupported = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Calls into the module                                               */
/* ------------------------------------------------------------------ */

/* Call the function at table `index` with one argument (an address in
 * the module's memory).  The i32 it answers, or -1 when it trapped. */
static s32 wasm_call_index(BwWasmMod *w, u32 index, u32 arg)
{
    uint32_t argv[1];

    wasm_thread_ready();
    argv[0] = arg;
    if (!wasm_runtime_call_indirect(w->env, index, 1, argv)) {
        fprintf(stderr, "eidos: mod %s: %s\n", w->label,
                wasm_runtime_get_exception(w->inst));
        wasm_runtime_clear_exception(w->inst);
        return -1;
    }
    return (s32)argv[0];
}

/* Copy `size` bytes into the port's page, call the handler at `index` on
 * the copy, and copy the bytes back.  The handler's answer, or -1.  The
 * page is a stack: a handler that calls a service that fires an event back
 * into the same mod gets the next copy above its own. */
static s32 wasm_call_on_copy(BwWasmMod *w, u32 index, void *data, u32 size)
{
    u32 need = (size + 7u) & ~7u;
    u32 app;
    void *native;
    s32 rc;

    if (need > w->scratch_size - w->scratch_used) {
        fprintf(stderr, "eidos: mod %s: payloads nested too deep for the "
                "port's page\n", w->label);
        return -1;
    }
    app = w->scratch_base + w->scratch_used;
    native = wasm_runtime_addr_app_to_native(w->inst, app);
    if (native == NULL) {
        return -1;
    }
    memcpy(native, data, size);
    w->scratch_used += need;
    rc = wasm_call_index(w, index, app);
    w->scratch_used -= need;
    /* The memory may have moved if the module grew it. */
    native = wasm_runtime_addr_app_to_native(w->inst, app);
    if (native != NULL) {
        memcpy(data, native, size);
    }
    return rc;
}

/* The handler the event layer runs for every event a wasm mod bound. */
static int wasm_event(void *state, BwEvent *event)
{
    BwWasmMod *w = (BwWasmMod *)state;
    s32 rc;

    if (w == NULL || event == NULL || event->kind >= BW_WASM_KINDS ||
        w->handler[event->kind] < 0) {
        return BW_EVENT_CONTINUE;
    }
    rc = wasm_call_on_copy(w, (u32)w->handler[event->kind], event, event->size);
    return rc == BW_EVENT_REFUSE ? BW_EVENT_REFUSE : BW_EVENT_CONTINUE;
}

/* The handler the script registry runs for a command a wasm mod bound. */
static int wasm_script(void *state, BwScriptCommand *cmd)
{
    BwWasmMod *w = (BwWasmMod *)state;
    u32 i;

    if (w == NULL || cmd == NULL) {
        return BW_SCRIPT_CONTINUE;
    }
    for (i = 0; i < w->script_count; i++) {
        if (w->scripts[i].opcode == cmd->opcode) {
            s32 rc = wasm_call_on_copy(w, w->scripts[i].index, cmd,
                                       (u32)sizeof(*cmd));

            return rc == BW_SCRIPT_REPLACE ? BW_SCRIPT_REPLACE : BW_SCRIPT_CONTINUE;
        }
    }
    return BW_SCRIPT_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* The frame handlers: a fixed pool of trampolines                      */
/* ------------------------------------------------------------------ */

static BwWasmMod *g_frame_mod[BW_WASM_FRAME_SLOTS];

static void wasm_frame_run(u32 slot)
{
    BwWasmMod *w = g_frame_mod[slot];
    uint32_t argv[1];

    if (w == NULL) {
        return;
    }
    wasm_thread_ready();
    argv[0] = 0;
    if (!wasm_runtime_call_indirect(w->env, w->frame_index, 0, argv)) {
        fprintf(stderr, "eidos: mod %s: %s\n", w->label,
                wasm_runtime_get_exception(w->inst));
        wasm_runtime_clear_exception(w->inst);
    }
}

#define BW_WASM_FRAME(n) static void wasm_frame_##n(void) { wasm_frame_run(n); }
BW_WASM_FRAME(0) BW_WASM_FRAME(1) BW_WASM_FRAME(2) BW_WASM_FRAME(3)
BW_WASM_FRAME(4) BW_WASM_FRAME(5) BW_WASM_FRAME(6) BW_WASM_FRAME(7)
BW_WASM_FRAME(8) BW_WASM_FRAME(9) BW_WASM_FRAME(10) BW_WASM_FRAME(11)
BW_WASM_FRAME(12) BW_WASM_FRAME(13) BW_WASM_FRAME(14) BW_WASM_FRAME(15)
#undef BW_WASM_FRAME

static const BwFrameHandler g_frame_fn[BW_WASM_FRAME_SLOTS] = {
    wasm_frame_0, wasm_frame_1, wasm_frame_2, wasm_frame_3,
    wasm_frame_4, wasm_frame_5, wasm_frame_6, wasm_frame_7,
    wasm_frame_8, wasm_frame_9, wasm_frame_10, wasm_frame_11,
    wasm_frame_12, wasm_frame_13, wasm_frame_14, wasm_frame_15
};

/* services.on_frame from a wasm mod: one trampoline per mod. */
static void wasm_bind_on_frame(BwWasmMod *w, int32_t index)
{
    u32 i;

    if (w == NULL || w->host == NULL || w->host->services == NULL ||
        w->host->services->on_frame == NULL) {
        return;
    }
    if (w->frame_slot < 0) {
        for (i = 0; i < BW_WASM_FRAME_SLOTS; i++) {
            if (g_frame_mod[i] == NULL) {
                g_frame_mod[i] = w;
                w->frame_slot = (s32)i;
                break;
            }
        }
        if (w->frame_slot < 0) {
            fprintf(stderr, "eidos: mod %s: too many wasm mods hold a frame "
                    "handler\n", w->label);
            return;
        }
    }
    w->frame_index = (u32)index;
    w->host->services->on_frame(g_frame_fn[w->frame_slot]);
}

/* ------------------------------------------------------------------ */
/* The session and command handlers: the same kind of pool              */
/* ------------------------------------------------------------------ */

/* Call the function at table `index` with `argc` i32 arguments, `bytes`
 * copied into the port's page first (its address goes in argv[ptr_arg]). */
static void wasm_call_with_bytes(BwWasmMod *w, u32 index, uint32_t *argv,
                                 u32 argc, u32 ptr_arg, const void *bytes,
                                 u32 size)
{
    u32 need = (size + 8u) & ~7u;      /* one byte more: a text's NUL */
    u32 app = 0;

    if (size > 0 || bytes != NULL) {
        void *native;

        if (need > w->scratch_size - w->scratch_used) {
            fprintf(stderr, "eidos: mod %s: %u bytes do not fit the port's "
                    "page\n", w->label, (unsigned)size);
            return;
        }
        app = w->scratch_base + w->scratch_used;
        native = wasm_runtime_addr_app_to_native(w->inst, app);
        if (native == NULL) {
            return;
        }
        if (size > 0) {
            memcpy(native, bytes, size);
        }
        ((u8 *)native)[size] = 0;
        w->scratch_used += need;
    }
    argv[ptr_arg] = app;
    wasm_thread_ready();
    if (!wasm_runtime_call_indirect(w->env, index, argc, argv)) {
        fprintf(stderr, "eidos: mod %s: %s\n", w->label,
                wasm_runtime_get_exception(w->inst));
        wasm_runtime_clear_exception(w->inst);
    }
    if (app != 0) {
        w->scratch_used -= need;
    }
}

static BwWasmMod *g_session_mod[BW_WASM_FRAME_SLOTS];
static BwWasmMod *g_command_mod[BW_WASM_FRAME_SLOTS];

/* A session event: (peer, kind, bytes, size) for the module. */
static void wasm_session_run(u32 slot, u32 peer, int kind, const void *buf,
                             u32 size)
{
    BwWasmMod *w = g_session_mod[slot];
    uint32_t argv[4];

    if (w == NULL) {
        return;
    }
    argv[0] = peer;
    argv[1] = (uint32_t)kind;
    argv[3] = size;
    wasm_call_with_bytes(w, w->session_index, argv, 4, 2, buf, size);
}

/* A command line: (text, length) for the module; the text ends in a NUL. */
static void wasm_command_run(u32 slot, const char *line)
{
    BwWasmMod *w = g_command_mod[slot];
    uint32_t argv[2];
    u32 len = line != NULL ? (u32)strlen(line) : 0u;

    if (w == NULL) {
        return;
    }
    argv[1] = len;
    wasm_call_with_bytes(w, w->command_index, argv, 2, 0,
                         line != NULL ? line : "", len);
}

#define BW_WASM_SESSION(n) \
    static void wasm_session_##n(u32 peer, int kind, const void *buf, u32 size) \
    { wasm_session_run(n, peer, kind, buf, size); } \
    static void wasm_command_##n(const char *line) { wasm_command_run(n, line); }
BW_WASM_SESSION(0) BW_WASM_SESSION(1) BW_WASM_SESSION(2) BW_WASM_SESSION(3)
BW_WASM_SESSION(4) BW_WASM_SESSION(5) BW_WASM_SESSION(6) BW_WASM_SESSION(7)
BW_WASM_SESSION(8) BW_WASM_SESSION(9) BW_WASM_SESSION(10) BW_WASM_SESSION(11)
BW_WASM_SESSION(12) BW_WASM_SESSION(13) BW_WASM_SESSION(14) BW_WASM_SESSION(15)
#undef BW_WASM_SESSION

static const BwSessionHandler g_session_fn[BW_WASM_FRAME_SLOTS] = {
    wasm_session_0, wasm_session_1, wasm_session_2, wasm_session_3,
    wasm_session_4, wasm_session_5, wasm_session_6, wasm_session_7,
    wasm_session_8, wasm_session_9, wasm_session_10, wasm_session_11,
    wasm_session_12, wasm_session_13, wasm_session_14, wasm_session_15
};

static const BwCommandHandler g_command_fn[BW_WASM_FRAME_SLOTS] = {
    wasm_command_0, wasm_command_1, wasm_command_2, wasm_command_3,
    wasm_command_4, wasm_command_5, wasm_command_6, wasm_command_7,
    wasm_command_8, wasm_command_9, wasm_command_10, wasm_command_11,
    wasm_command_12, wasm_command_13, wasm_command_14, wasm_command_15
};

/* A slot of `pool` for `w`: the one it holds, or a free one; -1 when the
 * pool is full. */
static s32 pool_slot(BwWasmMod **pool, BwWasmMod *w)
{
    u32 i;

    for (i = 0; i < BW_WASM_FRAME_SLOTS; i++) {
        if (pool[i] == w) {
            return (s32)i;
        }
    }
    for (i = 0; i < BW_WASM_FRAME_SLOTS; i++) {
        if (pool[i] == NULL) {
            pool[i] = w;
            return (s32)i;
        }
    }
    return -1;
}

/* services.on_session from a wasm mod.  The handler is the module's table
 * entry (peer, kind, bytes, size); the bytes are a copy in the port's page. */
static void wasm_bind_on_session(BwWasmMod *w, int32_t index)
{
    s32 slot;

    if (w == NULL || w->host == NULL || w->host->services == NULL ||
        w->host->services->on_session == NULL) {
        return;
    }
    slot = pool_slot(g_session_mod, w);
    if (slot < 0) {
        fprintf(stderr, "eidos: mod %s: too many wasm mods hold a session "
                "handler\n", w->label);
        return;
    }
    w->session_index = (u32)index;
    w->host->services->on_session(g_session_fn[slot]);
}

/* services.on_command from a wasm mod.  The handler is the module's table
 * entry (text, length); the text is a copy in the port's page, NUL-ended. */
static void wasm_bind_on_command(BwWasmMod *w, int32_t index)
{
    s32 slot;

    if (w == NULL || w->host == NULL || w->host->services == NULL ||
        w->host->services->on_command == NULL) {
        return;
    }
    slot = pool_slot(g_command_mod, w);
    if (slot < 0) {
        fprintf(stderr, "eidos: mod %s: too many wasm mods hold a command "
                "handler\n", w->label);
        return;
    }
    w->command_index = (u32)index;
    w->host->services->on_command(g_command_fn[slot]);
}

/* The handler this version does not carry over yet: a save is larger than
 * the port's page. */

static void wasm_bind_on_save_received(BwWasmMod *w, int32_t index)
{
    (void)index;
    say_unsupported(w, "services.on_save_received");
}

/* ------------------------------------------------------------------ */
/* The core imports (sdk/api/v5/core.json)                              */
/* ------------------------------------------------------------------ */

static void (*g_log_tap)(const char *label, const char *text, int len);

static void wc_log(wasm_exec_env_t env, const char *text, int32_t len)
{
    BwWasmMod *w = wasm_mod_of(env);

    fprintf(stderr, "mod %s: %.*s\n", w != NULL ? w->label : "?", (int)len, text);
    if (g_log_tap != NULL) {
        g_log_tap(w != NULL ? w->label : "?", text, (int)len);
    }
}

static int32_t wc_on(wasm_exec_env_t env, int32_t kind, int32_t index)
{
    BwWasmMod *w = wasm_mod_of(env);

    if (w == NULL || w->host == NULL || w->host->on == NULL ||
        kind <= 0 || (u32)kind >= BW_WASM_KINDS) {
        return -1;
    }
    w->handler[kind] = index;
    return w->host->on((u32)kind, wasm_event);
}

static int32_t wc_script(wasm_exec_env_t env, int32_t vm, int32_t opcode,
                         int32_t index)
{
    BwWasmMod *w = wasm_mod_of(env);
    BwWasmScript *s;

    if (w == NULL || w->host == NULL || w->host->script == NULL ||
        w->script_count >= BW_WASM_SCRIPTS) {
        return -1;
    }
    s = &w->scripts[w->script_count];
    s->vm = (u32)vm;
    s->opcode = (u16)opcode;
    s->index = (u32)index;
    if (w->host->script((u32)vm, (u16)opcode, wasm_script) != 0) {
        return -1;
    }
    w->script_count++;
    return 0;
}

static int32_t wc_override(wasm_exec_env_t env, int32_t region, int32_t addr,
                           int32_t index)
{
    (void)region;
    (void)addr;
    (void)index;
    say_unsupported(wasm_mod_of(env), "override");
    return -1;
}

static int64_t wc_original(wasm_exec_env_t env, void *args, int32_t nargs)
{
    (void)args;
    (void)nargs;
    say_unsupported(wasm_mod_of(env), "original");
    return -1;
}

/* A host pointer to `len` bytes at DS address `addr`, or NULL when any of
 * them is outside a backed region or the bytes are not contiguous. */
static void *ds_range(u32 addr, u32 len)
{
    u8 *first, *last;

    if (len == 0 || addr + len < addr) {
        return NULL;
    }
    first = (u8 *)bw_ds_try_ptr(addr);
    last = (u8 *)bw_ds_try_ptr(addr + len - 1u);
    if (first == NULL || last == NULL || last - first != (ptrdiff_t)(len - 1u)) {
        return NULL;
    }
    return first;
}

static uint32_t wc_rd8(wasm_exec_env_t env, int32_t addr)
{
    u8 *p = (u8 *)ds_range((u32)addr, 1u);

    (void)env;
    return p != NULL ? *p : 0u;
}

static uint32_t wc_rd16(wasm_exec_env_t env, int32_t addr)
{
    u8 *p = (u8 *)ds_range((u32)addr, 2u);
    u16 v = 0;

    (void)env;
    if (p != NULL) {
        memcpy(&v, p, 2);
    }
    return v;
}

static uint32_t wc_rd32(wasm_exec_env_t env, int32_t addr)
{
    u8 *p = (u8 *)ds_range((u32)addr, 4u);
    u32 v = 0;

    (void)env;
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static void wc_wr8(wasm_exec_env_t env, int32_t addr, int32_t value)
{
    u8 *p = (u8 *)ds_range((u32)addr, 1u);

    (void)env;
    if (p != NULL) {
        *p = (u8)value;
    }
}

static void wc_wr16(wasm_exec_env_t env, int32_t addr, int32_t value)
{
    u8 *p = (u8 *)ds_range((u32)addr, 2u);
    u16 v = (u16)value;

    (void)env;
    if (p != NULL) {
        memcpy(p, &v, 2);
    }
}

static void wc_wr32(wasm_exec_env_t env, int32_t addr, int32_t value)
{
    u8 *p = (u8 *)ds_range((u32)addr, 4u);
    u32 v = (u32)value;

    (void)env;
    if (p != NULL) {
        memcpy(p, &v, 4);
    }
}

static int32_t wc_read(wasm_exec_env_t env, int32_t addr, void *dst, int32_t len)
{
    u8 *p = (u8 *)ds_range((u32)addr, (u32)len);

    (void)env;
    if (p == NULL) {
        return -1;
    }
    memcpy(dst, p, (size_t)len);
    return 0;
}

static int32_t wc_write(wasm_exec_env_t env, int32_t addr, void *src, int32_t len)
{
    u8 *p = (u8 *)ds_range((u32)addr, (u32)len);

    (void)env;
    if (p == NULL) {
        return -1;
    }
    memcpy(p, src, (size_t)len);
    return 0;
}

static NativeSymbol k_wasm_core_natives[] = {
    { "log", (void *)wc_log, "(*~)", NULL },
    { "on", (void *)wc_on, "(ii)i", NULL },
    { "script", (void *)wc_script, "(iii)i", NULL },
    { "override", (void *)wc_override, "(iii)i", NULL },
    { "original", (void *)wc_original, "(*i)I", NULL },
    { "rd8", (void *)wc_rd8, "(i)i", NULL },
    { "rd16", (void *)wc_rd16, "(i)i", NULL },
    { "rd32", (void *)wc_rd32, "(i)i", NULL },
    { "wr8", (void *)wc_wr8, "(ii)", NULL },
    { "wr16", (void *)wc_wr16, "(ii)", NULL },
    { "wr32", (void *)wc_wr32, "(ii)", NULL },
    { "read", (void *)wc_read, "(i*~)i", NULL },
    { "write", (void *)wc_write, "(i*~)i", NULL },
};

/* 1 when the `size` bytes at native `p` are inside the module's memory.
 * WAMR checks one byte of a bare pointer argument; the object behind it is
 * checked whole before the port writes or reads it. */
static int wasm_object_ok(wasm_exec_env_t env, void *p, u32 size)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);

    return p != NULL && wasm_runtime_validate_native_addr(inst, p, size);
}

/* The state calls and the services: generated from sdk/mod_state.h. */
#include "mod_wasm_natives.inc"

/* ------------------------------------------------------------------ */
/* The runtime                                                         */
/* ------------------------------------------------------------------ */

static int runtime_ready(void)
{
    RuntimeInitArgs args;

    if (g_runtime_ready) {
        return 1;
    }
    memset(&args, 0, sizeof args);
    args.mem_alloc_type = Alloc_With_System_Allocator;
    if (!wasm_runtime_full_init(&args)) {
        fprintf(stderr, "eidos: the wasm runtime did not start\n");
        return 0;
    }
    if (!wasm_runtime_register_natives("eidos", k_wasm_core_natives,
            (uint32_t)(sizeof k_wasm_core_natives / sizeof k_wasm_core_natives[0])) ||
        !wasm_runtime_register_natives("eidos", k_wasm_api_natives,
            (uint32_t)(sizeof k_wasm_api_natives / sizeof k_wasm_api_natives[0]))) {
        fprintf(stderr, "eidos: the wasm imports could not be registered\n");
        wasm_runtime_destroy();
        return 0;
    }
    g_runtime_ready = 1;
    return 1;
}

int bw_mod_wasm_is_module(const char *path)
{
    FILE *f;
    unsigned char magic[4];
    int ok;

    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    ok = fread(magic, 1, 4, f) == 4 && magic[0] == 0 && magic[1] == 'a' &&
         magic[2] == 's' && magic[3] == 'm';
    fclose(f);
    return ok;
}

static u8 *read_all(const char *path, u32 *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    u8 *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || n > (64L << 20) ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (u8 *)malloc((size_t)n);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (u32)n;
    return buf;
}

void *bw_mod_wasm_open(const char *path, const char *label, char *err,
                       size_t err_size)
{
    BwWasmMod *w;
    u32 size = 0;
    char why[128];
    u32 i;

    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    if (!runtime_ready()) {
        snprintf(err, err_size, "the wasm runtime did not start");
        return NULL;
    }
    wasm_thread_ready();
    w = (BwWasmMod *)calloc(1, sizeof *w);
    if (w == NULL) {
        snprintf(err, err_size, "no memory");
        return NULL;
    }
    snprintf(w->label, sizeof w->label, "%s", label != NULL ? label : "?");
    for (i = 0; i < BW_WASM_KINDS; i++) {
        w->handler[i] = -1;
    }
    w->frame_slot = -1;
    w->bytes = read_all(path, &size);
    if (w->bytes == NULL) {
        snprintf(err, err_size, "the module %s could not be read", path);
        free(w);
        return NULL;
    }
    why[0] = '\0';
    w->module = wasm_runtime_load(w->bytes, size, why, sizeof why);
    if (w->module == NULL) {
        snprintf(err, err_size, "the module did not load: %s", why);
        bw_mod_wasm_close(w);
        return NULL;
    }
    /* Every import must be one of the port's functions, with the port's
     * signature.  WAMR loads a module whose import it cannot link and traps
     * only when the import is called; the port refuses it here instead. */
    {
        int32_t n = wasm_runtime_get_import_count(w->module);
        int32_t k;

        for (k = 0; k < n; k++) {
            wasm_import_t imp;

            memset(&imp, 0, sizeof imp);
            wasm_runtime_get_import_type(w->module, k, &imp);
            if (imp.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !imp.linked) {
                snprintf(err, err_size, "the module imports %s.%s, which the "
                         "port does not provide with that signature",
                         imp.module_name != NULL ? imp.module_name : "?",
                         imp.name != NULL ? imp.name : "?");
                bw_mod_wasm_close(w);
                return NULL;
            }
        }
    }
    w->inst = wasm_runtime_instantiate(w->module, BW_WASM_STACK, 0u,
                                       why, sizeof why);
    if (w->inst == NULL) {
        snprintf(err, err_size, "the module did not start: %s", why);
        bw_mod_wasm_close(w);
        return NULL;
    }
    /* The port's page: added above the memory the module has now, so the
     * module's data, its heap and the pages it grows later never meet it. */
    {
        wasm_memory_inst_t mem = wasm_runtime_get_default_memory(w->inst);
        uint64_t pages = mem != NULL ? wasm_memory_get_cur_page_count(mem) : 0;
        uint64_t max = mem != NULL ? wasm_memory_get_max_page_count(mem) : 0;

        if (mem == NULL) {
            snprintf(err, err_size, "the module has no memory");
            bw_mod_wasm_close(w);
            return NULL;
        }
        /* WAMR folds the memory of a module that never grows it into one
         * page of its own size; tools/fetch_wamr.py turns that off, and a
         * runtime built without the change is caught here. */
        if (wasm_memory_get_bytes_per_page(mem) != BW_WASM_PAGE) {
            snprintf(err, err_size, "the wasm runtime fixed the module's "
                     "memory size (run tools/fetch_wamr.py again and rebuild)");
            bw_mod_wasm_close(w);
            return NULL;
        }
        if (pages + BW_WASM_SCRATCH_PAGES > max ||
            pages + BW_WASM_SCRATCH_PAGES > 65536u ||
            !wasm_runtime_enlarge_memory(w->inst, BW_WASM_SCRATCH_PAGES)) {
            snprintf(err, err_size, "the module's memory cannot grow by the "
                     "port's page (its maximum is %u pages)", (unsigned)max);
            bw_mod_wasm_close(w);
            return NULL;
        }
        w->scratch_base = (u32)(pages * BW_WASM_PAGE);
        w->scratch_size = BW_WASM_SCRATCH_PAGES * BW_WASM_PAGE;
    }
    w->env = wasm_runtime_create_exec_env(w->inst, BW_WASM_STACK);
    if (w->env == NULL) {
        snprintf(err, err_size, "no execution environment");
        bw_mod_wasm_close(w);
        return NULL;
    }
    wasm_runtime_set_user_data(w->env, w);
    if (wasm_runtime_lookup_function(w->inst, "eidos_mod_start") == NULL ||
        wasm_runtime_lookup_function(w->inst, "eidos_mod_stop") == NULL) {
        snprintf(err, err_size, "the module exports no eidos_mod_start or "
                 "eidos_mod_stop");
        bw_mod_wasm_close(w);
        return NULL;
    }
    return w;
}

int bw_mod_wasm_start(void *wasm, const BwModHost *host, void **state)
{
    BwWasmMod *w = (BwWasmMod *)wasm;
    wasm_function_inst_t fn;
    uint32_t argv[1];

    if (w == NULL) {
        return -1;
    }
    w->host = host;
    if (state != NULL) {
        *state = w;
    }
    wasm_thread_ready();
    fn = wasm_runtime_lookup_function(w->inst, "eidos_mod_start");
    argv[0] = 0;
    if (fn == NULL || !wasm_runtime_call_wasm(w->env, fn, 0, argv)) {
        fprintf(stderr, "eidos: mod %s: eidos_mod_start: %s\n", w->label,
                fn != NULL ? wasm_runtime_get_exception(w->inst) : "missing");
        wasm_runtime_clear_exception(w->inst);
        return -1;
    }
    return (int)(s32)argv[0];
}

void bw_mod_wasm_stop(void *wasm)
{
    BwWasmMod *w = (BwWasmMod *)wasm;
    wasm_function_inst_t fn;
    uint32_t argv[1];

    if (w == NULL || w->inst == NULL) {
        return;
    }
    wasm_thread_ready();
    fn = wasm_runtime_lookup_function(w->inst, "eidos_mod_stop");
    argv[0] = 0;
    if (fn != NULL && !wasm_runtime_call_wasm(w->env, fn, 0, argv)) {
        fprintf(stderr, "eidos: mod %s: eidos_mod_stop: %s\n", w->label,
                wasm_runtime_get_exception(w->inst));
        wasm_runtime_clear_exception(w->inst);
    }
}

void bw_mod_wasm_close(void *wasm)
{
    BwWasmMod *w = (BwWasmMod *)wasm;

    if (w == NULL) {
        return;
    }
    if (w->frame_slot >= 0) {
        g_frame_mod[w->frame_slot] = NULL;
    }
    /* A trampoline the network service still holds finds no mod and does
     * nothing. */
    {
        u32 i;

        for (i = 0; i < BW_WASM_FRAME_SLOTS; i++) {
            if (g_session_mod[i] == w) {
                g_session_mod[i] = NULL;
            }
            if (g_command_mod[i] == w) {
                g_command_mod[i] = NULL;
            }
        }
    }
    if (w->env != NULL) {
        wasm_runtime_destroy_exec_env(w->env);
    }
    if (w->inst != NULL) {
        wasm_runtime_deinstantiate(w->inst);
    }
    if (w->module != NULL) {
        wasm_runtime_unload(w->module);
    }
    free(w->bytes);
    free(w);
}

void bw_mod_wasm_set_log_tap(void (*tap)(const char *label, const char *text, int len))
{
    g_log_tap = tap;
}

#else  /* !EIDOS_HAVE_WAMR */

int bw_mod_wasm_available(void)
{
    return 0;
}

int bw_mod_wasm_is_module(const char *path)
{
    FILE *f;
    unsigned char magic[4];
    int ok;

    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    ok = fread(magic, 1, 4, f) == 4 && magic[0] == 0 && magic[1] == 'a' &&
         magic[2] == 's' && magic[3] == 'm';
    fclose(f);
    return ok;
}

void *bw_mod_wasm_open(const char *path, const char *label, char *err,
                       size_t err_size)
{
    (void)path;
    (void)label;
    snprintf(err, err_size, "this build of eidos has no wasm runtime "
             "(run tools/fetch_wamr.py and configure again)");
    return NULL;
}

int bw_mod_wasm_start(void *wasm, const BwModHost *host, void **state)
{
    (void)wasm;
    (void)host;
    (void)state;
    return -1;
}

void bw_mod_wasm_stop(void *wasm)
{
    (void)wasm;
}

void bw_mod_wasm_close(void *wasm)
{
    (void)wasm;
}

void bw_mod_wasm_set_log_tap(void (*tap)(const char *label, const char *text, int len))
{
    (void)tap;
}

#endif /* EIDOS_HAVE_WAMR */
