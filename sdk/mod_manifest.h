/*
 * sdk/mod_manifest.h -- the manifest record (docs/mod-state.md, section 3;
 * docs/mod-interface.md, section 3).
 *
 * A mod's manifest is a file of `key = value` lines, `eidos_mod.txt` in the
 * mod's folder.  The port parses it into one record per mod and turns the
 * record into registrations: the api check, the state namespace, the
 * override list, the rules and the settings path.
 *
 * The record and the two rows it holds are the port's own (a mod never sees
 * them), but they are in a header because the loader and the manifest layer
 * have to agree on them: the loader reads an override's region, address and
 * symbol out of the record to install it.
 */

#ifndef BW_PORT_MOD_MANIFEST_H
#define BW_PORT_MOD_MANIFEST_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The override modes (docs/mod-overrides.md). */
enum {
    BW_MOD_OVERRIDE_REPLACE = 0,  /* the mod's function replaces the game's */
    BW_MOD_OVERRIDE_WRAP    = 1   /* the mod's function calls the game's */
};

/*
 * One override line, parsed (docs/mod-interface.md, section 3):
 *
 *     <region>:<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
 *
 * The strings are the record's own: bw_mod_manifest_free releases them.
 */
typedef struct BwModOverride {
    unsigned int region;       /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;         /* the even function start */
    unsigned int addr_tagged;  /* the Thumb-tagged form, addr | 1 */
    int          mode;         /* BW_MOD_OVERRIDE_REPLACE or _WRAP */
    char        *symbol;       /* the C symbol the mod's library exports */
    char        *sig;          /* the stated prototype, or NULL */
    char        *switch_name;  /* the switch name, or NULL for the row's own */
} BwModOverride;

/*
 * One of the game's own type names and the type it resolves to, published
 * by the build (tools/ovlink.py's table, the dispatcher's constructor
 * calls bw_mod_manifest_publish_types).  The manifest's normaliser asks it
 * for a name it cannot read otherwise, so a stated prototype that names a
 * game type is compared instead of reported (docs/mod-overrides.md).
 */
typedef struct BwModType {
    const char *name;          /* the game's own name, "WMErrCode" */
    const char *type;          /* the type it resolves to, "int"    */
} BwModType;

void bw_mod_manifest_publish_types(const BwModType *rows, unsigned int count);

/* One rule line, parsed: `<event> <path>`. */
typedef struct BwModRule {
    char *event;   /* the stable event name */
    char *path;    /* the rule file in the mod */
} BwModRule;

/*
 * One mod's manifest.  The strings and the two lists are the record's own:
 * bw_mod_manifest_free releases them.
 */
typedef struct BwModManifest {
    u32            magic;           /* BW_MOD_MANIFEST_MAGIC, the port's own */
    char          *name;           /* the display name */
    int            order;          /* the load order, ascending */
    int            api;            /* the interface version stated */
    int            has_api;        /* the manifest stated an api at all */
    char          *port;           /* the port version stated, or NULL */
    char          *code;           /* the library path, or NULL */
    char          *state;          /* the state namespace */
    BwModOverride *overrides;      /* the override lines */
    u32            override_count;
    u32            override_cap;
    BwModRule     *rules;          /* the rule lines */
    u32            rule_count;
    u32            rule_cap;
    char          *settings;       /* the settings declaration, or NULL */
    char          *ui_text;        /* `ui_text = <file>`: the interface's words
                                    * in the mod's language, or NULL
                                    * (docs/mod-ui-text.md) */
    BwModRule     *tables;         /* the table lines: event = the name */
    u32            table_count;
    u32            table_cap;
    int            multiplayer;    /* `multiplayer = <n>`: the mod's multiplayer
                                    * compatibility number, 0 when it states
                                    * none (docs/multiplayer.md) */
} BwModManifest;

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_MANIFEST_H */
