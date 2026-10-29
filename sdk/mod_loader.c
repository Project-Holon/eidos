/*
 * sdk/mod_loader.c -- the loader: the mod's library, the host table, the
 * handlers it binds, the overrides it installs and the way back down
 * (docs/mod-implementation.md, section 3; docs/mod-abi.md, sections 1 and 3;
 * docs/mod-overrides.md; docs/mod-state.md, section 2).
 *
 * What this file owns
 * -------------------
 *
 * The loader opens a mod's library, checks the mod's versions, hands the
 * mod the host table and takes it back down again.  The seven steps of
 * docs/mod-implementation.md, section 3, are the steps of
 * bw_mod_loader_load_all(), one mod at a time, in load order:
 *
 *   1. read the manifest and apply it: the api (and the port version, for
 *      a mod with raw hooks) is checked there, and a mod that does not
 *      match is refused and never loaded;
 *   2. open the library: LoadLibraryW on Windows, dlopen elsewhere;
 *   3. find eidos_mod_start and eidos_mod_stop in it;
 *   4. call eidos_mod_start with the host table, and keep the state
 *      pointer it returns;
 *   5. bind the handlers the mod asked for, and install the manifest's
 *      override lines.
 *
 * The way down (bw_mod_loader_stop_all, and bw_mod_loader_stop for one
 * mod) is the order docs/mod-state.md, section 2, constraint 2, fixes: the
 * mod's state is stored, the slots the mod installed are reverted to their
 * originals, eidos_mod_stop runs, and the library is unloaded last.  The
 * library is last because a code pointer into it is still live until then.
 *
 * The port calls
 * --------------
 *
 *   bw_mod_loader_load_all()       load every mod with a `code` key
 *   bw_mod_loader_stop_all()       stop and unload every loaded mod
 *   bw_mod_loader_stop(i)          stop and unload one loaded mod
 *   bw_mod_loader_count()          how many mods are loaded
 *   bw_mod_loader_name_at(i)       a loaded mod's name
 *   bw_mod_loader_namespace_at(i)  a loaded mod's state namespace
 *
 * The load point: the loader runs after the save is chosen and before the
 * game runs (docs/mod-implementation.md, section 3).  The save matters,
 * because a mod's own data lives in its state namespace, one file per
 * save (docs/mod-state.md, section 1).  On exit, and when the player
 * disables a mod, the port calls bw_mod_loader_stop_all() (or
 * bw_mod_loader_stop()).
 *
 * The mod's own data and the way down
 * -----------------------------------
 *
 * The mod owns its payload and stores it through the services table; the
 * saved-data layer writes it at once when the mod stores it, and holds it
 * until the game save ends when a save handoff is in progress.  The mod
 * ABI has no flush call, so the port's save handoff
 * (bw_mod_savedata_save_begin and bw_mod_savedata_save_done) is what
 * writes a mod's payload together with the game save: the port runs it
 * before it stops the mods.  The loader puts the mod's namespace in force
 * around the calls it makes into the mod, so a state call the mod makes
 * from its stop entry point reaches the mod's own file.
 *
 * A disabled mod and the event layer
 * ----------------------------------
 *
 * The event layer holds the handlers a mod bound, and it has no call to
 * drop the handlers of one mod.  A handler of a stopped mod still points
 * into the library the loader has unloaded, so the port must reset the
 * event layer (bw_mod_event_reset) and bind the port's own rows again
 * (bw_mod_event_bind_all) before any event fires, and then load the mods
 * that are still wanted (bw_mod_loader_load_all).  That is the rebuild the
 * port does at the menu anyway (docs/mod-state.md, section 2, constraint
 * 1); the loader does not reset the event layer itself, because the reset
 * empties the binding table as well.
 *
 * What the loader needs from the other layers
 * -------------------------------------------
 *
 * The manifest layer (sdk/mod_manifest.c) owns the manifest records.  The
 * loader creates one per mod, parses the manifest into it and applies it,
 * in that order, and reads the pieces it needs through the calls
 * declared below.  The apply is where the versions are checked
 * (docs/mod-state.md, section 3): a mod whose api does not match the
 * port's, or whose port pin does not match, is refused there, with a
 * message naming both numbers, and the loader does not load it.  The
 * apply also leaves in the record the overrides whose (region, address)
 * pair is a declared address; the ones that are not are reported there
 * and skipped.
 *
 * The saved data (sdk/mod_savedata.c) owns the mod's own file.  The
 * loader tells it which mod is calling (bw_mod_savedata_set_namespace)
 * and which api is in force (bw_mod_savedata_set_api) around every call
 * the port makes into a mod, and binds the five calls of the services
 * table (bw_mod_savedata_load and its four siblings) into the mod's own
 * table.  A service the port has not built is NULL, and a mod that calls
 * one gets nothing rather than a crash.
 *
 * The event layer (sdk/mod_event.c) owns the dispatch.  The loader tells
 * it which mod is registering, with bw_mod_event_set_registering_state,
 * so a handler receives the state of its own mod and not another mod's.
 * A mod binds its handlers from eidos_mod_start, before that call has
 * returned its state pointer, so the loader holds the bindings until the
 * state is known and registers them then, in the order the mod bound them.
 *
 * The script layer (sdk/mod_script.c) owns the script command hooks, and it
 * has the same registering-state hook (bw_mod_script_set_registering_state).
 * A mod binds a command handler through the host table's `script`, so the
 * script path holds its bindings until the start has returned the state
 * pointer as well, and registers them then.  A handler bound after the
 * start (from a frame handler, say) is registered at once, with the state
 * the start returned.
 *
 * The slot registry (sdk/mod_slot.c) holds a chain of hooks per (region,
 * address) pair.  The loader installs with bw_mod_slot_install_mode, which
 * carries the row's mode: a wrap nests in front of the hooks that are
 * already there, and a replace takes the slot for itself.  The way down is
 * bw_mod_slot_remove: the mod takes its own hook out of the chain, and the
 * hooks of the other mods stay where they are, so a wrap that was under
 * this one runs again (docs/mod-overrides.md, "Several mods on one
 * function").
 */
#include "armdata.h"
#include "mod_api.h"
#include "mod_avatar.h"
#include "mod_event.h"
#include "mod_export.h"
#include "mod_frame.h"
#include "mod_handoff.h"
#include "mod_input.h"
#include "mod_loader.h"
#include "mod_menu.h"
#include "mod_manifest.h"
#include "mod_message.h"
#include "mod_net.h"
#include "mod_panel.h"
#include "mod_rule.h"
#include "mod_table.h"
#include "mod_script.h"
#include "mod_slot.h"
#include "mod_state.h"
#include "mod_text.h"
#include "mp.h"
#include "mod_timer.h"
#include "mod_wasm.h"
#include "overlay_image.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif !defined(__wasi__)
#include <dlfcn.h>
#endif

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The manifest layer (sdk/mod_manifest.c).  The record is opaque here:
 * the loader creates one per mod, parses the manifest into it, applies
 * it, and reads the pieces it needs through these calls.  The manifest
 * layer has no header yet, so the record is carried as a void *.
 */
extern void        bw_mod_manifest_set_port_version(const char *version);
extern void       *bw_mod_manifest_new(const char *dir_name);
extern void        bw_mod_manifest_delete(void *manifest);
extern int         bw_mod_manifest_parse_dir(void *manifest, const char *root,
                                              const char *dir_name);
extern int         bw_mod_manifest_apply(void *manifest);
extern u32         bw_mod_manifest_override_count(const void *manifest);
extern const void *bw_mod_manifest_override_at(const void *manifest, u32 i);
extern u32         bw_mod_manifest_rule_count(const void *manifest);
extern const void *bw_mod_manifest_rule_at(const void *manifest, u32 i);
extern u32         bw_mod_manifest_table_count(const void *manifest);
extern const void *bw_mod_manifest_table_at(const void *manifest, u32 i);
extern const char *bw_mod_manifest_name(const void *manifest);
extern const char *bw_mod_manifest_code(const void *manifest);
extern const char *bw_mod_manifest_state(const void *manifest);
extern int         bw_mod_manifest_has_api(const void *manifest);
extern void        bw_mod_manifest_applied_clear(void);

/*
 * The slot registry (sdk/mod_slot.c).  The header (sdk/mod_slot.h) carries
 * the install without a mode, and the chain needs the mode and a way to take
 * one link out again, so the two calls are declared here the way the
 * manifest layer's calls are.
 *
 * The mode is the row's own (sdk/mod_manifest.h): BW_MOD_OVERRIDE_REPLACE or
 * BW_MOD_OVERRIDE_WRAP.
 */
extern int         bw_mod_slot_install_mode(unsigned int region,
                                            unsigned int addr, void *override,
                                            const char *owner, int mode);
extern int         bw_mod_slot_remove(unsigned int region, unsigned int addr,
                                      void *override, const char *owner);

/*
 * The saved data (sdk/mod_savedata.c).  The five calls are the ones the
 * services table carries (sdk/mod_state.h); the two setters tell the
 * layer whose file a call is about.  The layer has no header yet, so the
 * prototypes are here.
 */
extern void        bw_mod_savedata_set_namespace(const char *ns);
extern const char *bw_mod_savedata_namespace(void);
extern void        bw_mod_savedata_set_api(u32 api);
extern s32         bw_mod_savedata_load(void *buf, u32 size);
extern int         bw_mod_savedata_store(const void *buf, u32 size);
extern int         bw_mod_savedata_clear(void);
extern int         bw_mod_savedata_info(BwModStateHeader *out);
extern int         bw_mod_savedata_dir(char *out, u32 out_size);
extern int         bw_mod_savedata_in_step(void);

/* The run restart (sdk/mod_restart.c). */
extern void        bw_mod_restart(u32 flags);

/*
 * One override row: sdk/mod_manifest.h carries it, and the manifest layer
 * fills it.  The loader reads the region, the address and the symbol out of
 * it to install the override.
 */

/*
 * The port's own version, the one a mod with raw hooks is pinned to
 * (docs/mod-overrides.md, "Version pinning").  The port has no version
 * string of its own yet, so the loader carries one and hands it to the
 * manifest layer, which does the check; the build may set another with
 * -DBW_MOD_PORT_VERSION="...".
 */
#ifndef BW_MOD_PORT_VERSION
#define BW_MOD_PORT_VERSION "0.0.0-dev"
#endif

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many mods with code the loader holds at once. */
#define BW_MOD_LOADER_MAX 16

/* How many manifest records the loader keeps (one per mod it applied). */
#define BW_MOD_LOADER_APPLIED_MAX 32

/* How many handlers one mod may bind. */
#define BW_MOD_LOADER_BINDINGS_MAX 64

/* How many events one mod may bind a handler to. */
#define BW_MOD_LOADER_KINDS_MAX 64

/* How many script commands one mod may bind a handler to. */
#define BW_MOD_LOADER_SCRIPT_BINDINGS_MAX 64

/* How many overrides one mod may install. */
#define BW_MOD_LOADER_OVERRIDES_MAX 64

/* The lengths of the port's own buffers. */
#define BW_MOD_LOADER_NAME_MAX 96
#define BW_MOD_LOADER_NS_MAX 96
#define BW_MOD_LOADER_PATH_MAX 1200

/* ------------------------------------------------------------------ */
/* One loaded mod                                                      */
/* ------------------------------------------------------------------ */

/* The entry points a mod library exports (docs/mod-abi.md, section 1). */
typedef int  (*BwModStartFn)(const BwModHost *host, void **mod_state);
typedef void (*BwModStopFn)(void *mod_state);

/* A handler a mod asked for. */
typedef struct BwModLoaderBinding {
    u32            kind;
    BwEventHandler fn;
} BwModLoaderBinding;

/* An event a mod bound a handler to. */
typedef struct BwModLoaderKind {
    u32 kind;
    int registered;   /* the dispatch holds the mod's entry for it */
} BwModLoaderKind;

/*
 * A script command a mod bound a handler to.  The binding waits until the
 * mod's start has returned its state pointer, because the state is what the
 * handler is handed and the start is what produces it.  The event path
 * holds its bindings the same way (the kinds above).
 */
typedef struct BwModLoaderScriptBinding {
    u32             vm;
    u16             opcode;
    BwScriptHandler fn;
    int             registered;   /* the registry holds the mod's handler */
} BwModLoaderScriptBinding;

/* A pair the mod installed, kept so the way down can revert it. */
typedef struct BwModLoaderSlot {
    unsigned int region;
    unsigned int addr;
    void        *fn;         /* the hook the mod installed */
} BwModLoaderSlot;

typedef struct BwModLoaderMod {
    int           loaded;      /* the slot holds a mod with code */
    u32           slot;        /* the slot's number */
    char          name[BW_MOD_LOADER_NAME_MAX];
    char          ns[BW_MOD_LOADER_NS_MAX];
    char          path[BW_MOD_LOADER_PATH_MAX];
    void         *manifest;    /* the manifest record (the loader keeps it) */
    void         *library;     /* the handle: HMODULE or a dlopen handle */
    void         *wasm;        /* or the wasm module's handle (sdk/mod_wasm.h) */
    BwModStartFn  start;
    BwModStopFn   stop;
    void         *state;       /* the state the start returned */
    int           started;     /* the start ran and returned 0 */
    u32           api;         /* the api in force for the mod */
    BwModHost     host;
    BwModServices services;
    BwModLoaderBinding bindings[BW_MOD_LOADER_BINDINGS_MAX];
    u32           binding_count;
    BwModLoaderKind kinds[BW_MOD_LOADER_KINDS_MAX];
    u32           kind_count;
    BwModLoaderScriptBinding scripts[BW_MOD_LOADER_SCRIPT_BINDINGS_MAX];
    u32           script_binding_count;
    BwModLoaderSlot overrides[BW_MOD_LOADER_OVERRIDES_MAX];
    u32           override_count;
} BwModLoaderMod;

static BwModLoaderMod g_mods[BW_MOD_LOADER_MAX];
static u32            g_count;      /* the loaded mods */

/*
 * The manifest records the loader keeps: one per mod it applied.  The
 * manifest layer's applied list holds them as well (the interpreter and
 * the settings surface read them), so they live until the loader
 * releases them.
 */
static void *g_applied[BW_MOD_LOADER_APPLIED_MAX];
static u32   g_applied_count;

/*
 * The api the saved-data layer was last told about.  The layer has no
 * getter for it, so the loader keeps its own copy to put back.
 */
static u32 g_api_in_force = BW_MOD_API_VERSION;

/* ------------------------------------------------------------------ */
/* The per-mod calls                                                   */
/* ------------------------------------------------------------------ */

/*
 * The helpers the per-mod calls use; they are defined further down.
 */
static int loader_bind(int slot, u32 kind, BwEventHandler fn);
static int loader_bind_script(int slot, u32 vm, u16 opcode, BwScriptHandler fn);
static int loader_run(int slot, void *state, BwEvent *event);

/*
 * C has no closures, so each mod gets its own functions: the port tells
 * which mod is calling by the function the mod called.  The slot number
 * is the mod's place in the table, and it never moves: a mod that is
 * stopped keeps its slot, so the functions it holds stay its own.
 */
#define BW_MOD_LOADER_SLOT(n)                                          \
    static int loader_on_##n(u32 kind, BwEventHandler fn)              \
    {                                                                   \
        return loader_bind(n, kind, fn);                               \
    }                                                                   \
    static int loader_script_##n(u32 vm, u16 opcode, BwScriptHandler fn) \
    {                                                                   \
        return loader_bind_script(n, vm, opcode, fn);                  \
    }                                                                   \
    static int loader_handler_##n(void *state, BwEvent *event)          \
    {                                                                   \
        return loader_run(n, state, event);                            \
    }

BW_MOD_LOADER_SLOT(0)
BW_MOD_LOADER_SLOT(1)
BW_MOD_LOADER_SLOT(2)
BW_MOD_LOADER_SLOT(3)
BW_MOD_LOADER_SLOT(4)
BW_MOD_LOADER_SLOT(5)
BW_MOD_LOADER_SLOT(6)
BW_MOD_LOADER_SLOT(7)
BW_MOD_LOADER_SLOT(8)
BW_MOD_LOADER_SLOT(9)
BW_MOD_LOADER_SLOT(10)
BW_MOD_LOADER_SLOT(11)
BW_MOD_LOADER_SLOT(12)
BW_MOD_LOADER_SLOT(13)
BW_MOD_LOADER_SLOT(14)
BW_MOD_LOADER_SLOT(15)

/* The registration call of each slot. */
static int (*const g_on_table[BW_MOD_LOADER_MAX])(u32, BwEventHandler) = {
    loader_on_0,  loader_on_1,  loader_on_2,  loader_on_3,
    loader_on_4,  loader_on_5,  loader_on_6,  loader_on_7,
    loader_on_8,  loader_on_9,  loader_on_10, loader_on_11,
    loader_on_12, loader_on_13, loader_on_14, loader_on_15
};

/* The script registration call of each slot. */
static int (*const g_script_table[BW_MOD_LOADER_MAX])(u32, u16, BwScriptHandler) = {
    loader_script_0,  loader_script_1,  loader_script_2,  loader_script_3,
    loader_script_4,  loader_script_5,  loader_script_6,  loader_script_7,
    loader_script_8,  loader_script_9,  loader_script_10, loader_script_11,
    loader_script_12, loader_script_13, loader_script_14, loader_script_15
};

/* The entry the dispatch holds for each slot. */
static int (*const g_handler_table[BW_MOD_LOADER_MAX])(void *, BwEvent *) = {
    loader_handler_0,  loader_handler_1,  loader_handler_2,
    loader_handler_3,  loader_handler_4,  loader_handler_5,
    loader_handler_6,  loader_handler_7,  loader_handler_8,
    loader_handler_9,  loader_handler_10, loader_handler_11,
    loader_handler_12, loader_handler_13, loader_handler_14,
    loader_handler_15
};

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* Copy `src` into `dst`, cutting it at the buffer's end. */
static void copy_str(char *dst, size_t size, const char *src)
{
    size_t n;

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

/* The mod's name for the reports: its name, or a stand-in. */
static const char *mod_label(const char *name, const char *root)
{
    if (name != NULL && *name != '\0') {
        return name;
    }
    if (root != NULL && *root != '\0') {
        return root;
    }
    return "a mod";
}

/* ------------------------------------------------------------------ */
/* The library                                                         */
/* ------------------------------------------------------------------ */

/* The handle: HMODULE on Windows, a dlopen handle elsewhere. */
typedef void *BwModLibrary;

/*
 * Open the library.  The path is the port's own UTF-8 text, so it is
 * converted to the wide string the Windows loader wants; a path that is
 * not UTF-8 is read in the ANSI code page instead.
 */
static BwModLibrary library_open(const char *path)
{
#ifdef __wasi__
    /* A host dylib cannot exchange game pointers or callbacks with wasm32.
     * Data-only mods still work; executable mods need the mod wasm runtime. */
    fprintf(stderr, "eidos: native mod libraries are unavailable in the wasm32 game (%s)\n", path);
    return NULL;
#elif defined(_WIN32)
    wchar_t wide[BW_MOD_LOADER_PATH_MAX];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide,
                            (int)(sizeof(wide) / sizeof(wide[0]))) == 0) {
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, wide,
                                (int)(sizeof(wide) / sizeof(wide[0]))) == 0) {
            return NULL;
        }
    }
    return (BwModLibrary)LoadLibraryW(wide);
#else
    int flags = RTLD_NOW;

#ifdef RTLD_LOCAL
    flags |= RTLD_LOCAL;
#endif
    return dlopen(path, flags);
#endif
}

/* Close the library. */
static void library_close(BwModLibrary library)
{
    if (library == NULL) {
        return;
    }
#ifdef __wasi__
    (void)library;
#elif defined(_WIN32)
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

/*
 * The address of an exported symbol, or NULL.  The result is a function
 * pointer; it is carried as a void * and cast where it is used, because
 * that is the one conversion every host allows.
 */
static void *library_symbol(BwModLibrary library, const char *name)
{
#ifdef __wasi__
    (void)library;
    (void)name;
    return NULL;
#elif defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)library, name);
#else
    return dlsym(library, name);
#endif
}

/* ------------------------------------------------------------------ */
/* The calls into a mod                                                */
/* ------------------------------------------------------------------ */

/*
 * The saved-data layer knows whose file a state call is about by the
 * namespace the port sets around every call it makes into a mod.  These
 * two set it for one call and put back what was there.
 */
static void loader_enter_mod(BwModLoaderMod *m, const char **saved_ns,
                             u32 *saved_api)
{
    *saved_ns = bw_mod_savedata_namespace();
    *saved_api = g_api_in_force;
    bw_mod_savedata_set_namespace(m->ns);
    bw_mod_savedata_set_api(m->api);
    g_api_in_force = m->api;
}

static void loader_leave_mod(const char *saved_ns, u32 saved_api)
{
    bw_mod_savedata_set_namespace(saved_ns);
    bw_mod_savedata_set_api(saved_api);
    g_api_in_force = saved_api;
}

/* ------------------------------------------------------------------ */
/* The registration                                                    */
/* ------------------------------------------------------------------ */

/* The event a mod bound, or NULL when it did not bind that one yet. */
static BwModLoaderKind *kind_find(BwModLoaderMod *m, u32 kind)
{
    u32 i;

    for (i = 0; i < m->kind_count; i++) {
        if (m->kinds[i].kind == kind) {
            return &m->kinds[i];
        }
    }
    return NULL;
}

/*
 * Hand the dispatch the mod's entry for one event.  The registering state
 * is set around the call, so the handler the dispatch runs receives the
 * state of this mod and not another mod's.
 *
 * The state is only known once the mod's start has returned, so a mod
 * that binds its handlers from the start waits here until then.
 */
static void loader_register_kind(BwModLoaderMod *m, BwModLoaderKind *k)
{
    if (k->registered) {
        return;
    }
    bw_mod_event_set_registering_state(m->state);
    /* a multiplayer mod's handlers take the other players' events too */
    bw_mod_event_set_registering_remote(
        m->manifest != NULL &&
        ((const BwModManifest *)m->manifest)->multiplayer > 0);
    if (bw_mod_event_register(k->kind, g_handler_table[m->slot]) != 0) {
        fprintf(stderr,
                "eidos: mod %s: the handler for event %u was not bound\n",
                m->name, (unsigned int)k->kind);
    } else {
        k->registered = 1;
    }
    bw_mod_event_set_registering_state(NULL);
    bw_mod_event_set_registering_remote(0);
}

/*
 * The mod's own cells: the rule layer reads and writes them, and they live
 * in the mod's own file under its namespace (docs/mod-state.md).  The port
 * installs the two calls, and they reach the saved-data layer with that
 * namespace in force.
 */
static s32 loader_cells_load(const char *ns, void *buf, u32 size)
{
    const char *saved = bw_mod_savedata_namespace();
    s32 n;

    bw_mod_savedata_set_namespace(ns);
    n = bw_mod_savedata_load(buf, size);
    bw_mod_savedata_set_namespace(saved);
    return n;
}

static int loader_cells_store(const char *ns, const void *buf, u32 size)
{
    const char *saved = bw_mod_savedata_namespace();
    int rc;

    bw_mod_savedata_set_namespace(ns);
    rc = bw_mod_savedata_store(buf, size);
    bw_mod_savedata_set_namespace(saved);
    return rc;
}

/*
 * Hand the script registry the mod's handler for one command.  The
 * registering state is set around the call, so the handler the registry runs
 * receives the state of this mod and not another mod's.
 *
 * The state is only known once the mod's start has returned, so a mod that
 * binds its handlers from the start waits here until then (the event path
 * does the same: loader_register_kind).
 */
static int loader_register_script(BwModLoaderMod *m,
                                  BwModLoaderScriptBinding *b)
{
    int rc;

    if (b->registered) {
        return 0;
    }
    bw_mod_script_set_registering_state(m->state);
    rc = bw_mod_script_register(b->vm, b->opcode, b->fn, m->ns);
    bw_mod_script_set_registering_state(NULL);
    if (rc != 0) {
        fprintf(stderr,
                "eidos: mod %s: the handler for command %u of vm %u was not "
                "bound\n", m->name, (unsigned int)b->opcode,
                (unsigned int)b->vm);
        return -1;
    }
    b->registered = 1;
    return 0;
}

/*
 * Whether the VM and the opcode name a command the port can hook.  The
 * script layer knows the two VMs it hooks and the width of every command of
 * both tables; a width that does not fit the view is the layer's marker for
 * a command the game's table does not have.
 */
static int loader_script_command_exists(u32 vm, u16 opcode)
{
    u32 count = bw_mod_script_opcode_count(vm);
    u32 width;

    if (count == 0u) {
        return 0;               /* not one of the port's two VMs */
    }
    if ((u32)opcode >= count) {
        return 0;               /* the game's table has no such command */
    }
    width = bw_mod_script_operand_width(vm, opcode);
    return width <= (u32)BW_MOD_SCRIPT_ARGS_MAX;
}

/*
 * A mod binds a handler to a script command through the host table's
 * `script`.
 *
 * The registry holds the handler with the mod's own state, so a handler
 * receives the state its mod's start returned, the same rule the event
 * handlers follow.  The state does not exist while the start is running, so
 * a binding made from the start waits in the mod's own table until the
 * start has returned it, and is registered then.
 */
static int loader_bind_script(int slot, u32 vm, u16 opcode, BwScriptHandler fn)
{
    BwModLoaderMod *m = &g_mods[slot];
    BwModLoaderScriptBinding *b;

    if (m->loaded == 0 || fn == NULL) {
        return -1;
    }
    if (!loader_script_command_exists(vm, opcode)) {
        fprintf(stderr,
                "eidos: mod %s: the handler for command %u of vm %u was not "
                "bound\n", m->name, (unsigned int)opcode, (unsigned int)vm);
        return -1;
    }
    if (m->script_binding_count >= BW_MOD_LOADER_SCRIPT_BINDINGS_MAX) {
        fprintf(stderr, "eidos: mod %s: too many script handlers\n", m->name);
        return -1;
    }
    b = &m->scripts[m->script_binding_count++];
    b->vm = vm;
    b->opcode = opcode;
    b->fn = fn;
    b->registered = 0;
    if (m->started) {
        /* The state is known: the registry can take the handler now. */
        return loader_register_script(m, b);
    }
    /* The start has not returned yet, so the state is not known: the
     * binding waits in the mod's own table, and the loader registers it
     * once the start is through. */
    return 0;
}

/*
 * A mod binds a handler to an event through the host table's `on`.
 *
 * The dispatch holds one entry per mod and per event, and that entry runs
 * the mod's own handlers for the event.  The entry is the mod's, so the
 * port knows which mod is calling: it tells the saved-data layer whose
 * file a state call is about, and the handler receives the state the
 * mod's start returned.
 */
static int loader_bind(int slot, u32 kind, BwEventHandler fn)
{
    BwModLoaderMod *m = &g_mods[slot];
    BwModLoaderKind *k;

    if (m->loaded == 0 || fn == NULL) {
        return -1;
    }
    if (m->binding_count >= BW_MOD_LOADER_BINDINGS_MAX) {
        fprintf(stderr, "eidos: mod %s: too many handlers\n", m->name);
        return -1;
    }
    m->bindings[m->binding_count].kind = kind;
    m->bindings[m->binding_count].fn = fn;
    m->binding_count++;

    k = kind_find(m, kind);
    if (k == NULL) {
        if (m->kind_count >= BW_MOD_LOADER_KINDS_MAX) {
            fprintf(stderr, "eidos: mod %s: too many events\n", m->name);
            return 0;
        }
        k = &m->kinds[m->kind_count++];
        k->kind = kind;
        k->registered = 0;
    }
    if (m->started) {
        /* the state is known: the dispatch can take the entry now. */
        loader_register_kind(m, k);
    }
    return 0;
}

/*
 * The dispatch calls this: it runs the mod's own handlers for the event.
 * A refusal from any of them refuses the event, and the others still run.
 */
static int loader_run(int slot, void *state, BwEvent *event)
{
    BwModLoaderMod *m = &g_mods[slot];
    const char *saved_ns;
    u32 saved_api;
    int refused = 0;
    u32 i;

    if (event == NULL || m->loaded == 0) {
        return 0;
    }
    /* A record of an earlier run of this slot that the event layer still
     * holds carries that run's state, which is freed by now (a wasm mod's
     * state is heap memory).  The port resets the event layer before it
     * loads again (see "A disabled mod and the event layer"); a caller that
     * does not must not get a call on that state. */
    if (state != m->state) {
        return 0;
    }
    loader_enter_mod(m, &saved_ns, &saved_api);
    for (i = 0; i < m->binding_count; i++) {
        if (m->bindings[i].kind != event->kind) {
            continue;
        }
        if (m->bindings[i].fn(state, event) == BW_EVENT_REFUSE) {
            refused = 1;
        }
    }
    loader_leave_mod(saved_ns, saved_api);
    return refused;
}

/* ------------------------------------------------------------------ */
/* The overrides                                                       */
/* ------------------------------------------------------------------ */

/*
 * Install the manifest's override lines.  The symbol is resolved in the
 * loaded library, and the hook goes into the registry under the mod's name
 * and with the row's mode, so a later mod's hook on the same address is
 * reported as shadowing this one (docs/mod-overrides.md, "Several mods on
 * one function").
 *
 * The mode is what the two modes mean (docs/mod-overrides.md, "Modes"): a
 * wrap nests in front of the hooks that are already there, so two wraps on
 * one function run one after the other, and a replace takes the slot for
 * itself, so the hooks under it do not run.  The hooks are installed in
 * load order, and the registry puts each new hook in front, so the
 * last-loaded wrap runs first and each wrap's original is the hook that was
 * loaded before it.
 *
 * The apply has already left in the record only the pairs whose address
 * is declared; the ones that are not were reported there and skipped
 * (docs/mod-state.md, section 4).
 */
static void loader_install_overrides(BwModLoaderMod *m)
{
    u32 count = bw_mod_manifest_override_count(m->manifest);
    u32 i;

    for (i = 0; i < count; i++) {
        const BwModOverride *o;
        void *fn;

        o = (const BwModOverride *)
            bw_mod_manifest_override_at(m->manifest, i);
        if (o == NULL || o->symbol == NULL || *o->symbol == '\0') {
            continue;
        }
        fn = library_symbol(m->library, o->symbol);
        if (fn == NULL) {
            fprintf(stderr, "eidos: mod %s: the library exports no %s\n",
                    m->name, o->symbol);
            continue;
        }
        if (m->override_count >= BW_MOD_LOADER_OVERRIDES_MAX) {
            fprintf(stderr, "eidos: mod %s: too many overrides\n", m->name);
            break;
        }
        if (bw_mod_slot_install_mode(o->region, o->addr, fn, m->name,
                                     o->mode) != 0) {
            fprintf(stderr,
                    "eidos: mod %s: the override %u:0x%08X could not be "
                    "installed\n", m->name, o->region, o->addr);
            continue;
        }
        m->overrides[m->override_count].region = o->region;
        m->overrides[m->override_count].addr = o->addr;
        m->overrides[m->override_count].fn = fn;
        m->override_count++;
    }
    /* The ARM9's stored code pointers were written at the reset, before any
     * mod was loaded, so they hold the build's own functions.  Now that this
     * mod's hooks are in the registry, put them there (sdk/armdata.c).  An
     * overlay's own stored pointers are the overlay layer's business, and
     * they are asked too: an overlay may have loaded before this mod. */
    bw_arm9_resolve_code_pointers();
    bw_ovl_resolve_code_pointers();
}

/* ------------------------------------------------------------------ */
/* The load                                                            */
/* ------------------------------------------------------------------ */

/* The free slot for a new mod, or -1. */
static int loader_free_slot(void)
{
    u32 i;

    for (i = 0; i < BW_MOD_LOADER_MAX; i++) {
        if (g_mods[i].loaded == 0) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * Keep a manifest record.  The manifest layer's applied list holds it as
 * well, so it lives until the loader releases every record.
 * Returns 0, or -1 when there is no room (the record is released and
 * the applied list emptied with it).
 */
static int loader_keep_manifest(void *manifest)
{
    if (g_applied_count >= BW_MOD_LOADER_APPLIED_MAX) {
        fprintf(stderr, "eidos: too many mods\n");
        bw_mod_manifest_delete(manifest);
        bw_mod_manifest_applied_clear();
        return -1;
    }
    g_applied[g_applied_count++] = manifest;
    return 0;
}

/*
 * Load one mod: the mod at `index` of the mod list (sdk/vfs.h), which is
 * the load order.  Returns 1 when the mod is loaded, 0 when it is not: a
 * mod without a `code` key runs no native code, and a mod the manifest
 * layer refused is not loaded.
 */
static int loader_load(u32 index)
{
    const char *root = bw_mod_root_at(index);
    const char *dir_name = bw_mod_name_at(index);
    const char *label;
    const char *name;
    const char *ns;
    const char *code;
    BwModLoaderMod *m;
    BwModLibrary library;
    void *manifest;
    int slot;
    int rc;

    if (root == NULL) {
        return 0;
    }
    label = mod_label(dir_name, root);
    slot = loader_free_slot();
    if (slot < 0) {
        fprintf(stderr, "eidos: mod %s: too many mods with code\n", label);
        return 0;
    }
    m = &g_mods[slot];
    memset(m, 0, sizeof(*m));
    m->slot = (u32)slot;

    /* 1. the manifest: read it, and apply it.  The apply checks the
     *    versions and refuses a mod that does not match the port's. */
    manifest = bw_mod_manifest_new(dir_name);
    if (manifest == NULL) {
        fprintf(stderr, "eidos: mod %s: no room for the manifest\n", label);
        return 0;
    }
    m->manifest = manifest;
    if (bw_mod_manifest_parse_dir(manifest, root, dir_name) != 0) {
        fprintf(stderr, "eidos: mod %s: the manifest could not be read\n",
                label);
    }
    if (bw_mod_manifest_apply(manifest) != 0) {
        /* The mod is refused: the manifest layer has reported it, with
         * both numbers, and the mod is not loaded. */
        bw_mod_manifest_delete(manifest);
        m->manifest = NULL;
        return 0;
    }
    if (loader_keep_manifest(manifest) != 0) {
        m->manifest = NULL;
        return 0;
    }
    name = bw_mod_manifest_name(manifest);
    ns = bw_mod_manifest_state(manifest);
    code = bw_mod_manifest_code(manifest);
    copy_str(m->name, sizeof(m->name), mod_label(name, label));
    copy_str(m->ns, sizeof(m->ns), ns != NULL ? ns : label);

    /* The tables, before the rules: a rule that reads a table is checked
     * against the tables the mod has (sdk/mod_table.h).  The path is
     * relative to the mod's folder, like a rule's. */
    {
        u32 tcount = bw_mod_manifest_table_count(manifest);
        u32 t;

        for (t = 0; t < tcount; t++) {
            const BwModRule *table =
                (const BwModRule *)bw_mod_manifest_table_at(manifest, t);
            char path[1024];
            int n;

            if (table == NULL || table->event == NULL || table->path == NULL) {
                continue;
            }
            if (root != NULL && *root != '\0') {
                n = snprintf(path, sizeof path, "%s/%s", root, table->path);
            } else {
                n = snprintf(path, sizeof path, "%s/%s", dir_name, table->path);
            }
            if (n > 0 && (size_t)n < sizeof path) {
                (void)bw_mod_table_load(m->ns, table->event, path, m->name);
            }
        }
    }

    /* The rules: the manifest's rule lines, bound to their events.  A rule
     * that is refused is reported by the rule layer and the mod keeps the
     * rest of its manifest. */
    {
        u32 rcount = bw_mod_manifest_rule_count(manifest);
        u32 r;

        for (r = 0; r < rcount; r++) {
            const BwModRule *rule =
                (const BwModRule *)bw_mod_manifest_rule_at(manifest, r);

            if (rule != NULL && rule->event != NULL && rule->path != NULL) {
                bw_mod_rule_bind_manifest(root, dir_name, rule->event,
                                         rule->path, m->ns, m->name);
            }
        }
    }
    m->api = BW_MOD_API_VERSION;
    if (code == NULL || *code == '\0') {
        /* A mod without a `code` key runs no native code; its rules and
         * its data are the manifest layer's business. */
        return 0;
    }
    if (!bw_mod_manifest_has_api(manifest)) {
        /* A mod with code states the version it was built against, and the
         * port refuses it when the number does not match.  A manifest with
         * no api cannot be checked, and the rule is that the number cannot
         * be forgotten (docs/mod-abi.md, section 3). */
        fprintf(stderr, "eidos: mod %s: the manifest states no api version\n",
                label);
        bw_mod_manifest_delete(manifest);
        m->manifest = NULL;
        return 0;
    }
    if (root != NULL && *root != '\0') {
        snprintf(m->path, sizeof(m->path), "%s/%s", root, code);
    } else {
        snprintf(m->path, sizeof(m->path), "%s", code);
    }

    /* 2. open the library, or the wasm module: the file's first bytes tell
     *    them apart (docs/mod-code-wasm.md). */
    if (bw_mod_wasm_is_module(m->path)) {
        char why[256];

        m->wasm = bw_mod_wasm_open(m->path, m->name, why, sizeof(why));
        if (m->wasm == NULL) {
            fprintf(stderr, "eidos: mod %s: %s\n", m->name, why);
            return 0;
        }
        library = NULL;
    } else {
        library = library_open(m->path);
    }
    if (m->wasm == NULL && library == NULL) {
        fprintf(stderr, "eidos: mod %s: the library %s could not be opened\n",
                m->name, m->path);
        return 0;
    }

    /* 3. the entry points. */
    m->library = library;
    if (m->wasm == NULL) {
        m->start = (BwModStartFn)library_symbol(library, "eidos_mod_start");
        m->stop = (BwModStopFn)library_symbol(library, "eidos_mod_stop");
    }
    if (m->wasm == NULL && (m->start == NULL || m->stop == NULL)) {
        fprintf(stderr,
                "eidos: mod %s: the library exports no %s, so the mod is not "
                "loaded\n", m->name,
                m->start == NULL ? "eidos_mod_start" : "eidos_mod_stop");
        library_close(library);
        m->library = NULL;
        m->start = NULL;
        m->stop = NULL;
        return 0;
    }

    /* The host table the mod is handed.  The services the port has not
     * built are NULL: a mod that calls one gets nothing rather than a
     * crash. */
    m->loaded = 1;
    m->host.api = BW_MOD_API_VERSION;
    m->host.on = g_on_table[slot];
    m->host.script = g_script_table[slot];
    m->host.state = bw_mod_state_table();
    m->host.services = &m->services;
    /* The port's own text path: the sink the message service hands a text
     * to, and the font's own answer to "does the font lack this character"
     * (sdk/mod_text.h). */
    bw_mod_text_install();
    m->services.state_load = bw_mod_savedata_load;
    m->services.state_store = bw_mod_savedata_store;
    m->services.state_clear = bw_mod_savedata_clear;
    m->services.state_info = bw_mod_savedata_info;
    m->services.state_dir = bw_mod_savedata_dir;
    /* Is the mod's own file in step with the save in force?  The answer is
     * per mod, so it cannot be in the save.load payload (the payload is
     * built once for all the handlers of the event): the mod asks. */
    m->services.state_in_step = bw_mod_savedata_in_step;
    /* The messages: the first of the port's services, and the one every
     * refusal needs (docs/mod-services.md, "Messages"). */
    m->services.message = bw_mod_message_show;
    m->services.confirm = bw_mod_message_confirm;
    /* The timer and the splits, the mod's own random source, and a
     * Pokemon's export and import (docs/mod-services.md). */
    m->services.timer_start = bw_mod_timer_start;
    m->services.timer_stop  = bw_mod_timer_stop;
    m->services.timer_ms    = bw_mod_timer_ms;
    m->services.split       = bw_mod_timer_split;
    m->services.rand_seed   = bw_mod_rand_seed;
    m->services.rand_next   = bw_mod_rand_next;
    m->services.mon_export  = bw_mod_mon_export;
    m->services.mon_import  = bw_mod_mon_import;
    /* The peer sessions and the external control (docs/mod-services.md).
     * The transport and the relay's address are the port's own: the loader
     * does not set them. */
    m->services.session_host  = bw_mod_net_session_host;
    m->services.session_join  = bw_mod_net_session_join;
    m->services.session_leave = bw_mod_net_session_leave;
    m->services.session_send  = bw_mod_net_session_send;
    m->services.on_session    = bw_mod_net_on_session;
    m->services.on_command    = bw_mod_net_on_command;
    m->services.publish       = bw_mod_net_publish;
    /* The frame tick: the mod that binds a frame handler is the one whose
     * namespace is in force, the way the network's calls find their
     * caller. */
    m->services.on_frame      = bw_mod_frame_on_frame;
    /* The mod's own settings: the menu holds the player's choices, and the
     * mod reads them (sdk/mod_menu.h). */
    m->services.setting_get    = bw_mod_menu_setting_get;
    /* The mod's own data tables (sdk/mod_table.h), for the calling mod. */
    m->services.table_get      = bw_mod_table_get_calling;
    /* Multiplayer (sdk/mp.h): whose event runs, and the shared table. */
    m->services.event_player   = bw_mod_event_player;
    m->services.shared_get     = bw_mp_shared_get_calling;
    m->services.shared_set     = bw_mp_shared_set_calling;
    m->services.shared_add     = bw_mp_shared_add_calling;
    m->services.shared_cas     = bw_mp_shared_cas_calling;
    /* The run-status panel: the model's sixteen lines (sdk/mod_panel.h). */
    m->services.panel_set      = bw_mod_panel_set;
    m->services.panel_clear    = bw_mod_panel_clear;
    /* The input injection: the port's own press, held for its frames. */
    m->services.input_press    = bw_mod_input_press;
    /* The field avatars, the run restart and the save handoff. */
    m->services.avatar_create = bw_mod_avatar_create;
    m->services.avatar_move   = bw_mod_avatar_move;
    m->services.avatar_delete = bw_mod_avatar_delete;
    m->services.run_restart   = bw_mod_restart;
    m->services.save_send     = bw_mod_handoff_send;
    m->services.on_save_received = bw_mod_handoff_on_save;
    /* The rules' cells: the mod's own file under its namespace. */
    bw_mod_rule_set_cells_io(loader_cells_load, loader_cells_store);

    /* 4. the start: the mod binds its handlers through the host table. */
    {
        const char *saved_ns;
        u32 saved_api;

        loader_enter_mod(m, &saved_ns, &saved_api);
        rc = m->wasm != NULL ? bw_mod_wasm_start(m->wasm, &m->host, &m->state)
                             : m->start(&m->host, &m->state);
        loader_leave_mod(saved_ns, saved_api);
    }
    if (rc != 0) {
        fprintf(stderr,
                "eidos: mod %s: the start entry point returned %d, so the mod "
                "is not loaded\n", m->name, rc);
        m->loaded = 0;
        if (m->wasm != NULL) {
            bw_mod_frame_remove_for(m->ns);
            bw_mod_net_remove_for(m->ns);
            bw_mod_wasm_close(m->wasm);
            m->wasm = NULL;
        } else {
            library_close(library);
        }
        m->library = NULL;
        m->start = NULL;
        m->stop = NULL;
        m->state = NULL;
        return 0;
    }
    m->started = 1;
    {
        u32 i;

        /* The state is known now: the handlers the mod bound during its
         * start go to the dispatch, with the registering state set
         * around each call. */
        for (i = 0; i < m->kind_count; i++) {
            loader_register_kind(m, &m->kinds[i]);
        }
        /* The script handlers the mod bound during its start go to the
         * script registry the same way: the state is what the handler is
         * handed, so the registration waits for the start to be through. */
        for (i = 0; i < m->script_binding_count; i++) {
            (void)loader_register_script(m, &m->scripts[i]);
        }
    }

    /* 5. the overrides. */
    loader_install_overrides(m);

    g_count++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The way down                                                        */
/* ------------------------------------------------------------------ */

/*
 * Stop one mod and unload its library, in the order
 * docs/mod-state.md, section 2, constraint 2, fixes: the slots the mod
 * installed are reverted to their originals, the mod's stop entry point
 * runs, and the library goes last.
 */
static void loader_stop(BwModLoaderMod *m)
{
    const char *saved_ns;
    u32 saved_api;
    u32 i;

    /* 1. the slots the mod installed go back to their originals.  The
     *    registry holds a chain of hooks for one pair, so the mod takes
     *    its own hook out of the chain: the hooks of the other mods stay
     *    where they are, and a wrap that was under this one runs again
     *    (docs/mod-overrides.md, "Several mods on one function").  The
     *    hook goes out even when a later mod has taken the slot over,
     *    because the library is unloaded below and a hook left behind
     *    would point into it.  The pairs are reverted in the reverse order
     *    they were installed, so a mod that shadowed an earlier one gives
     *    the pair back before the earlier one does. */
    for (i = m->override_count; i > 0; i--) {
        bw_mod_slot_remove(m->overrides[i - 1].region,
                           m->overrides[i - 1].addr,
                           m->overrides[i - 1].fn, m->name);
    }
    m->override_count = 0;
    /* The stored code pointers that reached this mod's hook must reach the
     * game's own function again: the library is unloaded below, and a word
     * left holding the hook's address would jump into it. */
    bw_arm9_resolve_code_pointers();
    bw_ovl_resolve_code_pointers();

    /* 1b. the mod's own script and frame handlers go out too.  The script
     *     registry and the frame table hold the mod's own function, so a
     *     handler left behind after the library is unloaded would be a call
     *     into a library that is gone. */
    bw_mod_script_remove_for(m->ns);
    bw_mod_frame_remove_for(m->ns);
    bw_mod_net_remove_for(m->ns);

    /* 2. the mod's stop entry point.  The namespace is in force, so a
     *    state call the mod makes here reaches the mod's own file: this
     *    is where a mod flushes its data. */
    if (m->started) {
        loader_enter_mod(m, &saved_ns, &saved_api);
        if (m->wasm != NULL) {
            bw_mod_wasm_stop(m->wasm);
        } else if (m->stop != NULL) {
            m->stop(m->state);
        }
        loader_leave_mod(saved_ns, saved_api);
        m->state = NULL;
        m->started = 0;
    }

    /* 3. the library goes last: a code pointer into it is still live
     *    until now. */
    if (m->library != NULL) {
        library_close(m->library);
        m->library = NULL;
    }
    if (m->wasm != NULL) {
        bw_mod_wasm_close(m->wasm);
        m->wasm = NULL;
    }
    m->start = NULL;
    m->stop = NULL;
    m->loaded = 0;
    m->binding_count = 0;
    m->kind_count = 0;
    m->script_binding_count = 0;
}

/* Release every manifest record the loader keeps. */
static void loader_release_manifests(void)
{
    u32 i;

    for (i = 0; i < g_applied_count; i++) {
        bw_mod_manifest_delete(g_applied[i]);
        g_applied[i] = NULL;
    }
    g_applied_count = 0;
    /* The applied manifests are the records the loader released. */
    bw_mod_manifest_applied_clear();
}

/* ------------------------------------------------------------------ */
/* The port's calls                                                    */
/* ------------------------------------------------------------------ */

/*
 * Load every mod with a `code` key, in load order.  The port calls this
 * after the save is chosen and before the game runs.  Returns how many
 * mods were loaded.
 */
int bw_mod_loader_load_all(void)
{
    u32 count = bw_mod_count();
    u32 i;
    int loaded = 0;

    /* The port's own version, for the pin a mod with raw hooks carries. */
    bw_mod_manifest_set_port_version(BW_MOD_PORT_VERSION);

    for (i = 0; i < count; i++) {
        loaded += loader_load(i);
    }
    return loaded;
}

/*
 * Stop and unload every loaded mod, the last loaded first.  The port
 * calls this on exit, and after the player disables a mod.
 */
void bw_mod_loader_stop_all(void)
{
    u32 i;

    for (i = BW_MOD_LOADER_MAX; i > 0; i--) {
        if (g_mods[i - 1].loaded) {
            loader_stop(&g_mods[i - 1]);
        }
    }
    loader_release_manifests();
    g_count = 0;
}

/*
 * Stop and unload one loaded mod, the one at `index` in load order.  This
 * is the path the player takes when they disable a mod; the port resets
 * the event layer and loads the mods that are still wanted afterwards.
 * Returns 0, or -1 when there is no such mod.
 */
int bw_mod_loader_stop(u32 index)
{
    u32 i;
    u32 seen = 0;

    for (i = 0; i < BW_MOD_LOADER_MAX; i++) {
        if (!g_mods[i].loaded) {
            continue;
        }
        if (seen == index) {
            loader_stop(&g_mods[i]);
            loader_release_manifests();
            g_count--;
            return 0;
        }
        seen++;
    }
    return -1;
}

/* How many mods are loaded. */
u32 bw_mod_loader_count(void)
{
    return g_count;
}

/* The name of the loaded mod at `index`, in load order, or NULL. */
const char *bw_mod_loader_name_at(u32 index)
{
    u32 i;
    u32 seen = 0;

    for (i = 0; i < BW_MOD_LOADER_MAX; i++) {
        if (!g_mods[i].loaded) {
            continue;
        }
        if (seen == index) {
            return g_mods[i].name;
        }
        seen++;
    }
    return NULL;
}

/* The state namespace of the loaded mod at `index`, or NULL. */
int bw_mod_loader_multiplayer_at(u32 index)
{
    u32 i;
    u32 seen = 0;

    for (i = 0; i < BW_MOD_LOADER_MAX; i++) {
        if (!g_mods[i].loaded) {
            continue;
        }
        if (seen == index) {
            const BwModManifest *mf = (const BwModManifest *)g_mods[i].manifest;

            return mf != NULL ? mf->multiplayer : 0;
        }
        seen++;
    }
    return 0;
}

const char *bw_mod_loader_namespace_at(u32 index)
{
    u32 i;
    u32 seen = 0;

    for (i = 0; i < BW_MOD_LOADER_MAX; i++) {
        if (!g_mods[i].loaded) {
            continue;
        }
        if (seen == index) {
            return g_mods[i].ns;
        }
        seen++;
    }
    return NULL;
}
