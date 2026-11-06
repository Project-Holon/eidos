/*
 * sdk/mod_rule.c -- the rule interpreter: the small language a mod's rule
 * files are written in (docs/mod-rules-language.md).
 *
 * What this file is
 * -----------------
 *
 * A rule is a guarded action at one event: "on a faint, subtract 1000
 * money", "refuse a second catch on a route".  This file is the whole of
 * the rule layer: it reads a rule file, parses it, checks every name and
 * type against the port's own tables, compiles it, and runs it when its
 * event fires.
 *
 * The language is docs/mod-rules-language.md; the reasons are in
 * docs/mod-rules.md; the events are in docs/mod-events.md, section 1, the
 * payload fields in docs/mod-payloads.md, and the state values a rule may
 * read and write in docs/mod-rules-language.md, section 3.  The manifest key
 * that binds a rule to its event is `rule = <event> <path>`
 * (docs/mod-state.md, section 3; docs/mod-interface.md, section 3).
 *
 * What this file owns
 * -------------------
 *
 *   - the name tables: the events, the payload fields and the state fields;
 *   - the parser and the checker: the grammar of the language document's
 *     section 2, and the load check that every name, type and target is
 *     known before the rule ever runs;
 *   - the interpreter: the guards, the actions and the step budget
 *     (section 4);
 *   - the registry: the rules of an event, in load order (section 4).
 *
 * What it needs from the other layers
 * -----------------------------------
 *
 *   - the event dispatch (sdk/mod_event.h): a rule is a handler like any
 *     other, so the rules are registered with the dispatch and run with the
 *     mods' own handlers, in load order; a rule's refusal counts like a
 *     handler's refusal;
 *   - the state calls (sdk/mod_state.h): every state value a rule reads or
 *     writes goes through the port's own table, never around it;
 *   - the message service (sdk/mod_message.h): the `message` action;
 *   - the peer sessions (sdk/mod_net.h): the `send` action.  The rule
 *     layer reaches the session service through the seam the port
 *     installs (bw_mod_rule_set_send); with no seam it calls the session
 *     service's own call, so the action reaches the service either way;
 *   - the saved-data layer's namespace (sdk/mod_savedata.c): the mod that
 *     is calling.  The session service keys a message by it, so the rule
 *     layer sets it around the send.
 *
 * The port-internal API
 * ---------------------
 *
 * The calls below are the port's own; a mod never sees them.  They are at
 * the top of this file because this wave's claim covers one file: a header
 * (sdk/mod_rule.h) should carry them, and the wiring and the test need it.
 * The shape is the one of sdk/mod_event.h: a bind call, a run call, a
 * reset and a count.
 *
 *   u32         bw_mod_rule_event_kind(const char *name);
 *   const char *bw_mod_rule_event_name(u32 kind);
 *   int         bw_mod_rule_bind(u32 kind, const char *path,
 *                                const char *ns, const char *label);
 *   int         bw_mod_rule_bind_manifest(const char *root,
 *                                         const char *dir_name,
 *                                         const char *event_name,
 *                                         const char *rel_path,
 *                                         const char *ns,
 *                                         const char *label);
 *   int         bw_mod_rule_run(u32 kind, BwEvent *event);
 *   void        bw_mod_rule_reset(void);
 *   u32         bw_mod_rule_count(u32 kind);
 *   void        bw_mod_rule_set_send(int (*send)(const char *value));
 *   void        bw_mod_rule_set_cells_io(
 *                   s32 (*load)(const char *ns, void *buf, u32 size),
 *                   int (*store)(const char *ns, const void *buf, u32 size));
 *
 * A rule is a handler like any other: the dispatch holds one handler for
 * every event that has rules, and that handler runs the rules of the
 * event, in load order.  A rule's refusal counts like a handler's
 * refusal, and a refusal does not stop the later rules.
 *
 * The wiring the port still owes this layer
 * ------------------------------------------
 *
 *   - the manifest's `rule` lines are bound at load, in the mod's load
 *     order (bw_mod_rule_bind_manifest);
 *   - the reset drops the rules with the dispatch's own reset: the rules
 *     are registered as handlers, so a reset of the dispatch empties them
 *     too, and the rules are bound again when the mods are loaded again;
 *   - the mod's cells are stored through the saved-data layer: the port
 *     installs the two calls of bw_mod_rule_set_cells_io, and they reach
 *     the cells' own file, under a namespace derived from the mod's own;
 *   - the peer send: the port installs its own call with
 *     bw_mod_rule_set_send when it wants its own policy; the rule layer
 *     calls the session service itself when there is none.
 */
#include "mod_api.h"
#include "mod_event.h"
#include "mod_menu.h"
#include "mod_message.h"
#include "mod_net.h"
#include "mod_rule.h"
#include "mod_state.h"
#include "mod_table.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The saved-data layer's namespace seam.
 *
 * The layer has no header of its own yet, so the prototypes are here (the
 * session service does the same, sdk/mod_net.c).  The namespace is the mod
 * that is calling: the loader sets it around every call it makes into a
 * mod, and the session service keys a message by it.
 */
extern void        bw_mod_savedata_set_namespace(const char *ns);
extern const char *bw_mod_savedata_namespace(void);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/*
 * The bounds the port keeps.  A rule that is over one of them is refused
 * at load, with the reason in the report.  Every bound is a load-time
 * bound, so a loaded rule cannot run long: the language has no loop, and
 * the interpreter's own budget (the step budget of section 4) is the
 * backstop.
 */

/* The largest rule file the port reads. */
#define BW_RULE_FILE_MAX        (64u * 1024u)

/* How many rules the port holds at once, over every event together. */
#define BW_RULE_MAX             64u

/* The event ids of sdk/mod_api.h go up to BW_EVENT_BATTLE_TURN (113). */
#define BW_RULE_KINDS_MAX       128u

/*
 * How many nodes one rule's expressions may hold together.  The bound is
 * above the step budget, so a rule that is over it compiles and stops at
 * the budget when it runs (the language document, section 4).
 */
#define BW_RULE_NODES_MAX       1024u

/* How many guards one rule may have. */
#define BW_RULE_GUARDS_MAX      32u

/* How many actions one rule may have. */
#define BW_RULE_ACTIONS_MAX     32u

/* How deep an expression may nest. */
#define BW_RULE_DEPTH_MAX       32u

/* The length of a name, a rule's name and a namespace. */
#define BW_RULE_NAME_MAX        64u

/* The length of a text literal. */
#define BW_RULE_TEXT_MAX        256u

/* The length of one line of a rule file. */
#define BW_RULE_LINE_MAX        4096u

/* How many cells one mod may keep. */
#define BW_RULE_CELLS_MAX       32u

/* The length of a cell's name. */
#define BW_RULE_CELL_NAME_MAX   32u

/* How many keys one keyed cell holds (the language document, section 5). */
#define BW_RULE_CELL_KEYS_MAX   1024u

/* How many mods' cells the port holds at once. */
#define BW_RULE_OWNERS_MAX      16u

/*
 * The longest namespace the saved-data layer accepts (sdk/mod_savedata.c):
 * one path component, and at most 64 characters.
 */
#define BW_RULE_NS_MAX          64u

/*
 * The suffix the cells' own namespace carries.  The cells live in a file of
 * their own, under a namespace derived from the mod's, so that a rule that
 * writes a cell never writes over the mod's own payload.
 */
#define BW_RULE_CELLS_SUFFIX    "-rules"

/*
 * The step budget of the language document, section 4: a step is one
 * operator or one action.
 */
#define BW_RULE_STEPS_MAX       256u

/* The version of the rule language this port reads. */
#define BW_RULE_LANGUAGE_VERSION 1u

/* The version of the compiled form; it never leaves the port. */
#define BW_RULE_FORM_VERSION    1u

/* ------------------------------------------------------------------ */
/* The port-internal API                                              */
/* ------------------------------------------------------------------ */

/* The API is in sdk/mod_rule.h: the event names, the bind, run, reset and
 * count calls, and the two seams the port installs (the peer send and the
 * cells' file).
 */

/* ------------------------------------------------------------------ */
/* The value types                                                     */
/* ------------------------------------------------------------------ */

/*
 * The four types of the language document, section 2:
 *
 *   int   a 32-bit signed value: every numeric payload and state field
 *   bool  true or false: the literals, the comparisons and the flags
 *   text  a string literal (the payload carries no text field yet)
 *   mon   a Pokemon reference: a BwMonRef payload field
 */
enum {
    BW_RULE_T_INT = 0,
    BW_RULE_T_BOOL = 1,
    BW_RULE_T_TEXT = 2,
    BW_RULE_T_MON = 3
};

/* The operators of the expression grammar. */
enum {
    BW_RULE_OP_NUM = 0,     /* a number */
    BW_RULE_OP_BOOL,        /* true or false */
    BW_RULE_OP_TEXT,        /* a string literal */
    BW_RULE_OP_EVENT,       /* event.<field> */
    BW_RULE_OP_STATE,       /* a state field */
    BW_RULE_OP_CELL,        /* state.<cell> */
    BW_RULE_OP_SETTING,     /* setting.<key> */
    BW_RULE_OP_TABLE,       /* table.<name>[key] */
    BW_RULE_OP_NEG,         /* - */
    BW_RULE_OP_NOT,         /* ! */
    BW_RULE_OP_ADD,         /* + */
    BW_RULE_OP_SUB,         /* - */
    BW_RULE_OP_MUL,         /* * */
    BW_RULE_OP_DIV,         /* / */
    BW_RULE_OP_MOD,         /* % */
    BW_RULE_OP_EQ,          /* == */
    BW_RULE_OP_NE,          /* != */
    BW_RULE_OP_LT,          /* < */
    BW_RULE_OP_LE,          /* <= */
    BW_RULE_OP_GT,          /* > */
    BW_RULE_OP_GE,          /* >= */
    BW_RULE_OP_AND,         /* && */
    BW_RULE_OP_OR           /* || */
};

/* The actions of the language document, section 2. */
enum {
    BW_RULE_ACT_REFUSE = 0, /* refuse */
    BW_RULE_ACT_SET,        /* set <target> <expr> */
    BW_RULE_ACT_ADD,        /* add <target> <expr> */
    BW_RULE_ACT_SUB,        /* sub <target> <expr> */
    BW_RULE_ACT_MESSAGE,    /* message <expr> */
    BW_RULE_ACT_SEND,       /* send <expr> */
    BW_RULE_ACT_RELEASE,    /* release <mon or party position> */
    BW_RULE_ACT_GIVE,       /* give <item> <count> */
    BW_RULE_ACT_BOX,        /* box <mon or party position> <box> */
    BW_RULE_ACT_WITHDRAW    /* withdraw <box> <slot> */
};

/* The kinds of a target. */
enum {
    BW_RULE_TGT_EVENT = 0,  /* event.<field> */
    BW_RULE_TGT_STATE,      /* a state field */
    BW_RULE_TGT_CELL        /* state.<cell> */
};

/* The kinds of a payload field. */
enum {
    BW_RULE_FK_INT = 0,     /* a number */
    BW_RULE_FK_MON,         /* a BwMonRef */
    BW_RULE_FK_ARRAY        /* an array: a rule cannot read it */
};

/* The flags of a payload field. */
#define BW_RULE_FF_MOD     0x01u   /* the field is marked *mod: writable */
#define BW_RULE_FF_SIGNED  0x02u   /* the field is a signed number */

/* The index that means "no node". */
#define BW_RULE_NO_NODE    0xFFFFu

/* ------------------------------------------------------------------ */
/* The name tables                                                     */
/* ------------------------------------------------------------------ */

/*
 * One payload field: its name, where it sits in the payload and what it
 * is.  The offsets come from the payload structs of sdk/mod_api.h, so a
 * field that moves in the ABI moves here too, and the compiler says so.
 */
typedef struct BwRuleField {
    const char *name;
    u16         offset;
    u8          size;
    u8          kind;    /* BW_RULE_FK_* */
    u8          flags;   /* BW_RULE_FF_* */
} BwRuleField;

#define BW_RULE_FIELD(STRUCT, FIELD, KIND, FLAGS)                      \
    {                                                                   \
        .name = #FIELD,                                                 \
        .offset = (u16)offsetof(STRUCT, FIELD),                         \
        .size = (u8)sizeof(((STRUCT *)0)->FIELD),                       \
        .kind = (u8)(KIND),                                             \
        .flags = (u8)(FLAGS)                                            \
    }

/* session */

static const BwRuleField g_f_game_new[] = {
    { .name = "", .offset = 0, .size = 0, .kind = BW_RULE_FK_INT, .flags = 0 }
};

static const BwRuleField g_f_save_begin[] = {
    BW_RULE_FIELD(BwSaveBegin, play_seconds, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwSaveBegin, zone, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_save_done[] = {
    BW_RULE_FIELD(BwSaveDone, play_seconds, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwSaveDone, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwSaveDone, ok, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_save_load[] = {
    BW_RULE_FIELD(BwSaveLoad, play_seconds, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwSaveLoad, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwSaveLoad, state_in_step, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_game_cleared[] = {
    BW_RULE_FIELD(BwGameCleared, play_seconds, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwGameCleared, team_count, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwGameCleared, team, BW_RULE_FK_ARRAY, 0)
};

/* world */

static const BwRuleField g_f_route_change[] = {
    BW_RULE_FIELD(BwRouteChange, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwRouteChange, prev_zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwRouteChange, season, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwRouteChange, how, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_map_warp[] = {
    BW_RULE_FIELD(BwMapWarp, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMapWarp, dest_zone, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMapWarp, dest_x, BW_RULE_FK_INT,
                  BW_RULE_FF_MOD | BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwMapWarp, dest_z, BW_RULE_FK_INT,
                  BW_RULE_FF_MOD | BW_RULE_FF_SIGNED)
};

static const BwRuleField g_f_player_step[] = {
    BW_RULE_FIELD(BwPlayerStep, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwPlayerStep, x, BW_RULE_FK_INT, BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwPlayerStep, y, BW_RULE_FK_INT, BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwPlayerStep, z, BW_RULE_FK_INT, BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwPlayerStep, dir, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwPlayerStep, mode, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_trainer_spotted[] = {
    BW_RULE_FIELD(BwTrainerSpotted, trainer_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwTrainerSpotted, partner_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwTrainerSpotted, distance, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_npc_talk[] = {
    BW_RULE_FIELD(BwNpcTalk, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwNpcTalk, object_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwNpcTalk, script_id, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_menu_open[] = {
    { .name = "", .offset = 0, .size = 0, .kind = BW_RULE_FK_INT, .flags = 0 }
};

static const BwRuleField g_f_menu_build[] = {
    BW_RULE_FIELD(BwMenuBuild, entries, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_field_move[] = {
    BW_RULE_FIELD(BwFieldMove, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwFieldMove, user, BW_RULE_FK_MON, 0)
};

static const BwRuleField g_f_pc_open[] = {
    { .name = "", .offset = 0, .size = 0, .kind = BW_RULE_FK_INT, .flags = 0 }
};

/* encounters */

static const BwRuleField g_f_encounter_wild[] = {
    BW_RULE_FIELD(BwEncounterWild, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterWild, method, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterWild, slot, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterWild, species, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterWild, level, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterWild, shiny, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterWild, double_, BW_RULE_FK_INT, 0),
    /* The payload document calls the field `double`; the C struct spells
     * it `double_`, because `double` is a keyword.  Both names reach
     * it. */
    { .name = "double", .offset = (u16)offsetof(BwEncounterWild, double_),
      .size = (u8)sizeof(((BwEncounterWild *)0)->double_),
      .kind = BW_RULE_FK_INT, .flags = 0 },
    BW_RULE_FIELD(BwEncounterWild, reroll, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterWild, species2, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterWild, level2, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_encounter_static[] = {
    BW_RULE_FIELD(BwEncounterStatic, zone, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterStatic, species, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterStatic, level, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwEncounterStatic, kind, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_encounter_trainer[] = {
    BW_RULE_FIELD(BwEncounterTrainer, trainer_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterTrainer, partner_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwEncounterTrainer, format, BW_RULE_FK_INT, 0)
};

/* pokemon */

static const BwRuleField g_f_mon_create[] = {
    BW_RULE_FIELD(BwMonCreate, species, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonCreate, level, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonCreate, shiny, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonCreate, nature, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonCreate, personality, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_receive[] = {
    BW_RULE_FIELD(BwMonReceive, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonReceive, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonReceive, to_box, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonReceive, species, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonReceive, level, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonReceive, shiny, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonReceive, zone, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_nickname[] = {
    BW_RULE_FIELD(BwMonNickname, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonNickname, force, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_mon_evolve[] = {
    BW_RULE_FIELD(BwMonEvolve, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonEvolve, from, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonEvolve, to, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonEvolve, method, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_exp[] = {
    BW_RULE_FIELD(BwMonExp, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonExp, amount, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonExp, exp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonExp, level, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonExp, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonExp, wild, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_levelup[] = {
    BW_RULE_FIELD(BwMonLevelup, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonLevelup, from, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonLevelup, to, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonLevelup, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonLevelup, item, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_move_learn[] = {
    BW_RULE_FIELD(BwMoveLearn, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMoveLearn, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMoveLearn, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMoveLearn, slot, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMoveLearn, replaced, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_release[] = {
    BW_RULE_FIELD(BwMonRelease, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonRelease, species, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_box_deposit[] = {
    BW_RULE_FIELD(BwBoxDeposit, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBoxDeposit, box, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBoxDeposit, slot, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_box_withdraw[] = {
    BW_RULE_FIELD(BwBoxWithdraw, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBoxWithdraw, box, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBoxWithdraw, slot, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_party_swap[] = {
    BW_RULE_FIELD(BwPartySwap, slot_a, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwPartySwap, slot_b, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_helditem[] = {
    BW_RULE_FIELD(BwMonHelditem, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonHelditem, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonHelditem, previous, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_egg_hatch[] = {
    BW_RULE_FIELD(BwEggHatch, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwEggHatch, species, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_trade_ingame[] = {
    BW_RULE_FIELD(BwTradeIngame, trade_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwTradeIngame, given, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwTradeIngame, received_species, BW_RULE_FK_INT, 0)
};

/* battle */

static const BwRuleField g_f_battle_start[] = {
    BW_RULE_FIELD(BwBattleStart, wild, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleStart, format, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleStart, trainer_id, BW_RULE_FK_ARRAY, 0),
    BW_RULE_FIELD(BwBattleStart, level_adjust, BW_RULE_FK_INT,
                  BW_RULE_FF_MOD | BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwBattleStart, opponent_party, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_action[] = {
    BW_RULE_FIELD(BwBattleAction, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleAction, side, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAction, action, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAction, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAction, switch_to, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_switch[] = {
    BW_RULE_FIELD(BwBattleSwitch, out, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleSwitch, in, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleSwitch, forced, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_escape[] = {
    BW_RULE_FIELD(BwBattleEscape, mon, BW_RULE_FK_MON, 0)
};

static const BwRuleField g_f_battle_move[] = {
    BW_RULE_FIELD(BwBattleMove, user, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleMove, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleMove, target, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleMove, turn, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_damage[] = {
    BW_RULE_FIELD(BwBattleDamage, attacker, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleDamage, target, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleDamage, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleDamage, amount, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleDamage, target_hp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleDamage, target_max_hp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleDamage, critical, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleDamage, effectiveness, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_faint[] = {
    BW_RULE_FIELD(BwBattleFaint, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleFaint, side, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleFaint, cause, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleFaint, cause_move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleFaint, cause_damage, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleFaint, turn, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_item[] = {
    BW_RULE_FIELD(BwBattleItem, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleItem, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleItem, ball, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleItem, revive, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_catch_attempt[] = {
    BW_RULE_FIELD(BwCatchAttempt, target, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwCatchAttempt, species, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchAttempt, level, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchAttempt, shiny, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchAttempt, ball, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchAttempt, rate, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwCatchAttempt, static_, BW_RULE_FK_INT, 0),
    /* `static` is a C keyword, so the struct spells it `static_`; both
     * names reach it. */
    { .name = "static", .offset = (u16)offsetof(BwCatchAttempt, static_),
      .size = (u8)sizeof(((BwCatchAttempt *)0)->static_),
      .kind = BW_RULE_FK_INT, .flags = 0 }
};

static const BwRuleField g_f_catch_result[] = {
    BW_RULE_FIELD(BwCatchResult, target, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwCatchResult, species, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchResult, ball, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchResult, caught, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwCatchResult, shakes, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_end[] = {
    BW_RULE_FIELD(BwBattleEnd, outcome, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleEnd, wild, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleEnd, trainer_id, BW_RULE_FK_ARRAY, 0),
    BW_RULE_FIELD(BwBattleEnd, turns, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_whiteout[] = {
    BW_RULE_FIELD(BwWhiteout, in_battle, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwWhiteout, money_lost, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_exp[] = {
    BW_RULE_FIELD(BwBattleExp, members, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleExp, wild, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleExp, scale, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleExp, total, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleExp, share, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_battle_ai[] = {
    BW_RULE_FIELD(BwBattleAi, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleAi, target, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleAi, stage, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAi, switch_to, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleAi, move, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleAi, target_pos, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleAi, item, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleAi, scores, BW_RULE_FK_ARRAY, 0)
};

static const BwRuleField g_f_battle_ai_replace[] = {
    BW_RULE_FIELD(BwBattleAiReplace, foe, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleAiReplace, count, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAiReplace, order, BW_RULE_FK_ARRAY, 0)
};

static const BwRuleField g_f_battle_ability[] = {
    BW_RULE_FIELD(BwBattleAbility, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleAbility, ability, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleAbility, side, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_stat[] = {
    BW_RULE_FIELD(BwBattleStat, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleStat, stat, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleStat, delta, BW_RULE_FK_INT, BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwBattleStat, side, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_held_item[] = {
    BW_RULE_FIELD(BwBattleHeldItem, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleHeldItem, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleHeldItem, side, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_status[] = {
    BW_RULE_FIELD(BwBattleStatus, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleStatus, source, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwBattleStatus, status, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleStatus, applied, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleStatus, side, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_turn[] = {
    BW_RULE_FIELD(BwBattleTurn, turn, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleTurn, weather, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_battle_trainer_mon[] = {
    BW_RULE_FIELD(BwBattleTrainerMon, trainer_id, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleTrainerMon, index, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBattleTrainerMon, level, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, species, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, item, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, move1, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, move2, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, move3, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, move4, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, ability, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwBattleTrainerMon, form, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

/* items, money and healing */

static const BwRuleField g_f_item_add[] = {
    BW_RULE_FIELD(BwItemAdd, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwItemAdd, count, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwItemAdd, pocket, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwItemAdd, source, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_item_use[] = {
    BW_RULE_FIELD(BwItemUse, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwItemUse, target, BW_RULE_FK_MON, 0)
};

static const BwRuleField g_f_shop_buy[] = {
    BW_RULE_FIELD(BwShopBuy, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwShopBuy, price, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_money_change[] = {
    BW_RULE_FIELD(BwMoneyChange, delta, BW_RULE_FK_INT,
                  BW_RULE_FF_MOD | BW_RULE_FF_SIGNED),
    BW_RULE_FIELD(BwMoneyChange, before, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMoneyChange, source, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_heal[] = {
    BW_RULE_FIELD(BwHeal, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwHeal, mask, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwHeal, script_id, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_cure[] = {
    BW_RULE_FIELD(BwMonCure, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonCure, status, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonCure, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonCure, item, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_pp[] = {
    BW_RULE_FIELD(BwMonPp, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonPp, slot, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonPp, source, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonPp, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonPp, amount, BW_RULE_FK_INT, BW_RULE_FF_MOD)
};

static const BwRuleField g_f_mon_form[] = {
    BW_RULE_FIELD(BwMonForm, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonForm, species, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonForm, from, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonForm, to, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonForm, source, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_mon_revive[] = {
    BW_RULE_FIELD(BwMonRevive, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwMonRevive, hp, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwMonRevive, max_hp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonRevive, item, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonRevive, move, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwMonRevive, source, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_heal_hp[] = {
    BW_RULE_FIELD(BwHealHp, mon, BW_RULE_FK_MON, 0),
    BW_RULE_FIELD(BwHealHp, hp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwHealHp, max_hp, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwHealHp, amount, BW_RULE_FK_INT, BW_RULE_FF_MOD),
    BW_RULE_FIELD(BwHealHp, source, BW_RULE_FK_INT, 0)
};

/* progress */

static const BwRuleField g_f_badge_get[] = {
    BW_RULE_FIELD(BwBadgeGet, badge, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwBadgeGet, count, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_flag_set[] = {
    BW_RULE_FIELD(BwFlagSet, flag, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwFlagSet, value, BW_RULE_FK_INT, 0)
};

static const BwRuleField g_f_dex_register[] = {
    BW_RULE_FIELD(BwDexRegister, species, BW_RULE_FK_INT, 0),
    BW_RULE_FIELD(BwDexRegister, caught, BW_RULE_FK_INT, 0)
};

/*
 * One event: its name, whether it can be refused (the "Refuse" column of
 * docs/mod-events.md, section 1) and its payload fields.
 */
typedef struct BwRuleEvent {
    u32                kind;
    const char        *name;
    int                refusable;
    const BwRuleField *fields;
    u16                field_count;
} BwRuleEvent;

static const BwRuleEvent g_events[] = {
    { .kind = BW_EVENT_GAME_NEW, .name = "game.new", .refusable = 0,
      .fields = g_f_game_new, .field_count = 0 },
    { .kind = BW_EVENT_SAVE_BEGIN, .name = "save.begin", .refusable = 0,
      .fields = g_f_save_begin, .field_count = 2 },
    { .kind = BW_EVENT_SAVE_DONE, .name = "save.done", .refusable = 0,
      .fields = g_f_save_done, .field_count = 3 },
    { .kind = BW_EVENT_SAVE_LOAD, .name = "save.load", .refusable = 0,
      .fields = g_f_save_load, .field_count = 3 },
    { .kind = BW_EVENT_GAME_CLEARED, .name = "game.cleared", .refusable = 0,
      .fields = g_f_game_cleared, .field_count = 3 },

    { .kind = BW_EVENT_ROUTE_CHANGE, .name = "route.change", .refusable = 0,
      .fields = g_f_route_change, .field_count = 4 },
    { .kind = BW_EVENT_MAP_WARP, .name = "map.warp", .refusable = 0,
      .fields = g_f_map_warp, .field_count = 4 },
    { .kind = BW_EVENT_PLAYER_STEP, .name = "player.step", .refusable = 0,
      .fields = g_f_player_step, .field_count = 6 },
    { .kind = BW_EVENT_TRAINER_SPOTTED, .name = "trainer.spotted",
      .refusable = 1, .fields = g_f_trainer_spotted, .field_count = 3 },
    { .kind = BW_EVENT_NPC_TALK, .name = "npc.talk", .refusable = 1,
      .fields = g_f_npc_talk, .field_count = 3 },
    { .kind = BW_EVENT_MENU_OPEN, .name = "menu.open", .refusable = 1,
      .fields = g_f_menu_open, .field_count = 0 },
    { .kind = BW_EVENT_MENU_BUILD, .name = "menu.build", .refusable = 0,
      .fields = g_f_menu_build, .field_count = 1 },
    { .kind = BW_EVENT_FIELD_MOVE, .name = "field.move", .refusable = 1,
      .fields = g_f_field_move, .field_count = 2 },
    { .kind = BW_EVENT_PC_OPEN, .name = "pc.open", .refusable = 1,
      .fields = g_f_pc_open, .field_count = 0 },

    { .kind = BW_EVENT_ENCOUNTER_WILD, .name = "encounter.wild",
      .refusable = 1, .fields = g_f_encounter_wild, .field_count = 11 },
    { .kind = BW_EVENT_ENCOUNTER_STATIC, .name = "encounter.static",
      .refusable = 0, .fields = g_f_encounter_static, .field_count = 4 },
    { .kind = BW_EVENT_ENCOUNTER_TRAINER, .name = "encounter.trainer",
      .refusable = 0, .fields = g_f_encounter_trainer, .field_count = 3 },

    { .kind = BW_EVENT_MON_CREATE, .name = "mon.create", .refusable = 0,
      .fields = g_f_mon_create, .field_count = 5 },
    { .kind = BW_EVENT_MON_RECEIVE, .name = "mon.receive", .refusable = 0,
      .fields = g_f_mon_receive, .field_count = 7 },
    { .kind = BW_EVENT_MON_NICKNAME, .name = "mon.nickname", .refusable = 0,
      .fields = g_f_mon_nickname, .field_count = 2 },
    { .kind = BW_EVENT_MON_EVOLVE, .name = "mon.evolve", .refusable = 1,
      .fields = g_f_mon_evolve, .field_count = 4 },
    { .kind = BW_EVENT_MON_EXP, .name = "mon.exp", .refusable = 0,
      .fields = g_f_mon_exp, .field_count = 6 },
    { .kind = BW_EVENT_MON_LEVELUP, .name = "mon.levelup", .refusable = 0,
      .fields = g_f_mon_levelup, .field_count = 5 },
    { .kind = BW_EVENT_MOVE_LEARN, .name = "move.learn", .refusable = 1,
      .fields = g_f_move_learn, .field_count = 5 },
    { .kind = BW_EVENT_MON_RELEASE, .name = "mon.release", .refusable = 1,
      .fields = g_f_mon_release, .field_count = 2 },
    { .kind = BW_EVENT_BOX_DEPOSIT, .name = "box.deposit", .refusable = 1,
      .fields = g_f_box_deposit, .field_count = 3 },
    { .kind = BW_EVENT_BOX_WITHDRAW, .name = "box.withdraw", .refusable = 1,
      .fields = g_f_box_withdraw, .field_count = 3 },
    { .kind = BW_EVENT_PARTY_SWAP, .name = "party.swap", .refusable = 1,
      .fields = g_f_party_swap, .field_count = 2 },
    { .kind = BW_EVENT_MON_HELDITEM, .name = "mon.helditem", .refusable = 0,
      .fields = g_f_mon_helditem, .field_count = 3 },
    { .kind = BW_EVENT_EGG_HATCH, .name = "egg.hatch", .refusable = 0,
      .fields = g_f_egg_hatch, .field_count = 2 },
    { .kind = BW_EVENT_TRADE_INGAME, .name = "trade.ingame", .refusable = 1,
      .fields = g_f_trade_ingame, .field_count = 3 },

    { .kind = BW_EVENT_BATTLE_START, .name = "battle.start", .refusable = 0,
      .fields = g_f_battle_start, .field_count = 5 },
    { .kind = BW_EVENT_BATTLE_ACTION, .name = "battle.action",
      .refusable = 1, .fields = g_f_battle_action, .field_count = 5 },
    { .kind = BW_EVENT_BATTLE_ITEM, .name = "battle.item", .refusable = 1,
      .fields = g_f_battle_item, .field_count = 4 },
    { .kind = BW_EVENT_BATTLE_SWITCH, .name = "battle.switch",
      .refusable = 0, .fields = g_f_battle_switch, .field_count = 3 },
    { .kind = BW_EVENT_BATTLE_ESCAPE, .name = "battle.escape",
      .refusable = 1, .fields = g_f_battle_escape, .field_count = 1 },
    { .kind = BW_EVENT_BATTLE_MOVE, .name = "battle.move", .refusable = 0,
      .fields = g_f_battle_move, .field_count = 4 },
    { .kind = BW_EVENT_BATTLE_DAMAGE, .name = "battle.damage",
      .refusable = 0, .fields = g_f_battle_damage, .field_count = 8 },
    { .kind = BW_EVENT_BATTLE_FAINT, .name = "battle.faint", .refusable = 0,
      .fields = g_f_battle_faint, .field_count = 6 },
    { .kind = BW_EVENT_CATCH_ATTEMPT, .name = "catch.attempt",
      .refusable = 1, .fields = g_f_catch_attempt, .field_count = 8 },
    { .kind = BW_EVENT_CATCH_RESULT, .name = "catch.result", .refusable = 0,
      .fields = g_f_catch_result, .field_count = 5 },
    { .kind = BW_EVENT_BATTLE_END, .name = "battle.end", .refusable = 0,
      .fields = g_f_battle_end, .field_count = 4 },
    { .kind = BW_EVENT_WHITEOUT, .name = "whiteout", .refusable = 0,
      .fields = g_f_whiteout, .field_count = 2 },
    { .kind = BW_EVENT_BATTLE_EXP, .name = "battle.exp", .refusable = 1,
      .fields = g_f_battle_exp, .field_count = 5 },
    { .kind = BW_EVENT_BATTLE_AI, .name = "battle.ai", .refusable = 0,
      .fields = g_f_battle_ai, .field_count = 8 },
    { .kind = BW_EVENT_BATTLE_AI_REPLACE, .name = "battle.ai_replace",
      .refusable = 0, .fields = g_f_battle_ai_replace, .field_count = 3 },
    { .kind = BW_EVENT_BATTLE_ABILITY, .name = "battle.ability", .refusable = 0,
      .fields = g_f_battle_ability, .field_count = 3 },
    { .kind = BW_EVENT_BATTLE_STAT, .name = "battle.stat", .refusable = 0,
      .fields = g_f_battle_stat, .field_count = 4 },
    { .kind = BW_EVENT_BATTLE_HELD_ITEM, .name = "battle.held_item",
      .refusable = 0, .fields = g_f_battle_held_item, .field_count = 3 },
    { .kind = BW_EVENT_BATTLE_STATUS, .name = "battle.status", .refusable = 0,
      .fields = g_f_battle_status, .field_count = 5 },
    { .kind = BW_EVENT_BATTLE_TURN, .name = "battle.turn", .refusable = 0,
      .fields = g_f_battle_turn, .field_count = 2 },
    { .kind = BW_EVENT_BATTLE_TRAINER_MON, .name = "battle.trainer_mon",
      .refusable = 1, .fields = g_f_battle_trainer_mon, .field_count = 11 },

    { .kind = BW_EVENT_ITEM_ADD, .name = "item.add", .refusable = 0,
      .fields = g_f_item_add, .field_count = 3 },
    { .kind = BW_EVENT_ITEM_USE, .name = "item.use", .refusable = 1,
      .fields = g_f_item_use, .field_count = 2 },
    { .kind = BW_EVENT_SHOP_BUY, .name = "shop.buy", .refusable = 1,
      .fields = g_f_shop_buy, .field_count = 2 },
    { .kind = BW_EVENT_MONEY_CHANGE, .name = "money.change", .refusable = 0,
      .fields = g_f_money_change, .field_count = 3 },
    { .kind = BW_EVENT_HEAL, .name = "heal", .refusable = 1,
      .fields = g_f_heal, .field_count = 3 },
    { .kind = BW_EVENT_HEAL_HP, .name = "heal.hp", .refusable = 1,
      .fields = g_f_heal_hp, .field_count = 5 },
    { .kind = BW_EVENT_MON_REVIVE, .name = "mon.revive", .refusable = 1,
      .fields = g_f_mon_revive, .field_count = 6 },
    { .kind = BW_EVENT_MON_CURE, .name = "mon.cure", .refusable = 1,
      .fields = g_f_mon_cure, .field_count = 4 },
    { .kind = BW_EVENT_MON_PP, .name = "mon.pp", .refusable = 1,
      .fields = g_f_mon_pp, .field_count = 5 },
    { .kind = BW_EVENT_MON_FORM, .name = "mon.form", .refusable = 1,
      .fields = g_f_mon_form, .field_count = 5 },

    { .kind = BW_EVENT_BADGE_GET, .name = "badge.get", .refusable = 0,
      .fields = g_f_badge_get, .field_count = 2 },
    { .kind = BW_EVENT_FLAG_SET, .name = "flag.set", .refusable = 0,
      .fields = g_f_flag_set, .field_count = 2 },
    { .kind = BW_EVENT_DEX_REGISTER, .name = "dex.register", .refusable = 0,
      .fields = g_f_dex_register, .field_count = 2 }
};

#define BW_RULE_EVENT_COUNT \
    ((u32)(sizeof(g_events) / sizeof(g_events[0])))

/* ------------------------------------------------------------------ */
/* The state fields                                                    */
/* ------------------------------------------------------------------ */

/*
 * The state fields of the language document, section 3.  Each one is
 * reached through the port's own state calls (sdk/mod_state.h), never
 * around them.
 *
 *   kind       what the subscript means
 *   writable   the direction column says the field may be written
 *   readable   the port can answer the field today
 *   writable_ok the port can write the field today
 *   mon_field  the game's field id, for the fields that name one Pokemon
 */
enum {
    BW_RULE_SK_PLAIN = 0,   /* no subscript */
    BW_RULE_SK_MON,         /* a Pokemon: the subscript is a mon */
    BW_RULE_SK_INT          /* the subscript is a number */
};

typedef struct BwRuleStateField {
    const char *name;
    u8          kind;
    u8          writable;
    u8          readable;
    u8          writable_ok;
    u32         mon_field;  /* the game's field id, or 0 */
} BwRuleStateField;

/*
 * The game's own field ids (POKE_TOOL_GetPartyValue), the ones
 * docs/mod-events.md, section 4, names.  The personality is not in the
 * ABI's enum, but it is the game's own id for the same accessor, and the
 * shiny test needs it.
 */
#define BW_RULE_MON_PERSONALITY  0x00u

static const BwRuleStateField g_state_fields[] = {
    { .name = "money", .kind = BW_RULE_SK_PLAIN, .writable = 1,
      .readable = 1, .writable_ok = 1, .mon_field = 0 },
    { .name = "badges", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "badge", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "zone", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "player_x", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "player_z", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "party_count", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "species", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_SPECIES },
    { .name = "level", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_LEVEL },
    { .name = "hp", .kind = BW_RULE_SK_MON, .writable = 1,
      .readable = 1, .writable_ok = 1, .mon_field = BW_MON_FIELD_HP },
    { .name = "max_hp", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MAX_HP },
    { .name = "type1", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_TYPE1 },
    { .name = "type2", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_TYPE2 },
    { .name = "shiny", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "held_item", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_ITEM },
    { .name = "species_type1", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "species_type2", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "species_bst", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "caught", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "item_count", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },

    /* Interface version 4: more of a Pokemon's own fields. */
    { .name = "personality", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_PERSONALITY },
    { .name = "ot_id", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_OT_ID },
    { .name = "exp", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_EXP },
    { .name = "friendship", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_FRIENDSHIP },
    { .name = "ability", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_ABILITY },
    { .name = "move1", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MOVE1 },
    { .name = "move2", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MOVE2 },
    { .name = "move3", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MOVE3 },
    { .name = "move4", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MOVE4 },
    { .name = "is_egg", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_IS_EGG },
    { .name = "gender", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_GENDER },
    { .name = "form", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_FORM },
    { .name = "nature", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_NATURE },
    { .name = "has_nickname", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_HAS_NICKNAME },
    { .name = "fateful", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_FATEFUL },
    { .name = "origin_game", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_ORIGIN_GAME },
    { .name = "met_location", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MET_LOCATION },
    { .name = "ball", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_BALL },
    { .name = "met_level", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = BW_MON_FIELD_MET_LEVEL },
    /* Where a reference points: its place (BW_MON_*) and, in a battle,
     * its side (0 the player's). */
    { .name = "place", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "side", .kind = BW_RULE_SK_MON, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },

    /* Interface version 4: the species data by name. */
    { .name = "species_stage", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "gender_ratio", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "egg_group1", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "egg_group2", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "growth", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "catch_rate", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "species_base", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },

    /* Interface version 4: the world, the battle, the party as a whole,
     * item and move data. */
    { .name = "area", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "area_of", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "wild_battle", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "battle_trainer", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "battle_trainer2", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "play_seconds", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "owned", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "party_max_level", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "party_alive", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "party_has_type", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "item_pocket", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "move_type", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "move_power", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "move_class", .kind = BW_RULE_SK_INT, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "battle_style", .kind = BW_RULE_SK_PLAIN, .writable = 1,
      .readable = 1, .writable_ok = 0, .mon_field = 0 },
    { .name = "in_battle", .kind = BW_RULE_SK_PLAIN, .writable = 0,
      .readable = 1, .writable_ok = 0, .mon_field = 0 }
};

#define BW_RULE_STATE_COUNT \
    ((u32)(sizeof(g_state_fields) / sizeof(g_state_fields[0])))

/* ------------------------------------------------------------------ */
/* The compiled form                                                   */
/* ------------------------------------------------------------------ */

/* One node of an expression. */
typedef struct BwRuleNode {
    u8          op;      /* BW_RULE_OP_* */
    u8          type;    /* the type of the value */
    u16         left;    /* the left operand */
    u16         right;   /* the right operand */
    u16         sub;     /* the subscript, or BW_RULE_NO_NODE */
    s32         ival;    /* a number or a flag */
    const char *text;    /* a literal's text, a field's name, a cell's name */
    u8          field;   /* the payload field index */
    u8          state;   /* the state field index */
} BwRuleNode;

/* One action of a rule. */
typedef struct BwRuleAction {
    u8          op;      /* BW_RULE_ACT_* */
    u8          tkind;   /* BW_RULE_TGT_* */
    u8          field;   /* the payload field index */
    u8          state;   /* the state field index */
    u16         expr;    /* the expression */
    u16         sub;     /* the subscript, or BW_RULE_NO_NODE */
    const char *cell;    /* the cell's name */
} BwRuleAction;

/* One bound rule. */
typedef struct BwRule {
    u32           kind;       /* the event it binds */
    const BwRuleEvent *event;  /* the event's row */
    char          name[BW_RULE_NAME_MAX];
    char          label[BW_RULE_NAME_MAX];
    char          ns[BW_RULE_NS_MAX + 1u];   /* the mod's namespace */
    char         *text;       /* the file's text: the names point into it */
    char        **strings;    /* the copies the rule keeps */
    u32           string_count;
    u32           string_cap;
    BwRuleNode   *nodes;
    u32           node_count;
    u32           node_cap;
    u16           guards[BW_RULE_GUARDS_MAX];
    u32           guard_count;
    BwRuleAction  actions[BW_RULE_ACTIONS_MAX];
    u32           action_count;
    int           uses_cells;    /* the rule reads or writes a cell */
    int           budget_reported;
    int           runtime_reported;
    int           send_reported;  /* the send's own report is done */
} BwRule;

/* The bound rules, in load order. */
static BwRule *g_rules[BW_RULE_MAX];
static u32     g_rule_count;

/* Which events the dispatch holds a rule handler for. */
static int g_kind_registered[BW_RULE_KINDS_MAX];

/* ------------------------------------------------------------------ */
/* The mod's own cells                                                 */
/* ------------------------------------------------------------------ */

/*
 * A cell is the mod's own data: `state.<cell>` is a number and
 * `state.<cell>[<key>]` is a table of numbers (the language document,
 * section 3).  The cells live in the mod's saved data, so they follow the
 * game save (docs/mod-state.md).
 */
typedef struct BwRuleCell {
    char name[BW_RULE_CELL_NAME_MAX];
    int  keyed;      /* 1 when the cell is a table */
    s32  value;      /* the number of a plain cell */
    s32 *keys;       /* the keys of a table */
    s32 *values;     /* the values of a table */
    u32  count;
    u32  cap;
} BwRuleCell;

/* The cells of one mod, under its state namespace. */
typedef struct BwRuleOwner {
    char       ns[BW_RULE_NS_MAX + 1u];         /* the mod's namespace */
    char       file_ns[BW_RULE_NS_MAX + 1u];     /* the cells' own namespace */
    BwRuleCell cells[BW_RULE_CELLS_MAX];
    u32        cell_count;
    int        loaded;
    int        own_file;    /* the cells' file is the rule layer's own */
    int        reported;
} BwRuleOwner;

static BwRuleOwner g_owners[BW_RULE_OWNERS_MAX];
static u32          g_owner_count;

/* The cells' own file, through the port's saved-data layer. */
static s32 (*g_cells_load)(const char *ns, void *buf, u32 size);
static int (*g_cells_store)(const char *ns, const void *buf, u32 size);

/* The report that the port has no store for the cells: once a session. */
static int g_cells_reported;

/* The peer session send, when the service is built. */
static int (*g_send)(const char *value);

/* The cells' payload: the magic, the version and the cells. */
#define BW_RULE_CELLS_MAGIC   "BWRC"
#define BW_RULE_CELLS_VERSION 1u
#define BW_RULE_CELLS_BUF_MAX (288u * 1024u)

/* ------------------------------------------------------------------ */
/* The reports                                                         */
/* ------------------------------------------------------------------ */

/*
 * The shape the port uses for every manifest problem: one line on the
 * error stream, naming the mod, the rule and the line.
 */
static void rule_report(const char *label, const char *name, int line,
                         const char *fmt, ...)
{
    va_list ap;

    fputs("eidos: ", stderr);
    if (label != NULL && *label != '\0') {
        fprintf(stderr, "mod %s: ", label);
    }
    if (name != NULL && *name != '\0') {
        fprintf(stderr, "rule %s: ", name);
    }
    if (line > 0) {
        fprintf(stderr, "line %d: ", line);
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* The bits of a u32 read as a signed number, without a cast that is
 * implementation defined. */
static s32 as_s32(u32 v)
{
    s32 r;

    memcpy(&r, &v, sizeof r);
    return r;
}

/* Copy a string into a fixed buffer, cutting it at the buffer's end. */
static void copy_str(char *dst, size_t size, const char *src)
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

/*
 * A copy of a string the rule keeps: a cell's name or a text literal.
 * The parser's own buffers are gone when the rule runs, so the strings
 * the compiled form points at are copied into the rule.
 */
static const char *rule_string(BwRule *r, const char *text)
{
    char *copy;
    char **grown;
    u32 cap;

    if (text == NULL) {
        return NULL;
    }
    copy = (char *)malloc(strlen(text) + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, strlen(text) + 1u);
    if (r->string_count >= r->string_cap) {
        cap = r->string_cap == 0 ? 8u : r->string_cap * 2u;
        grown = (char **)realloc(r->strings, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            free(copy);
            return NULL;
        }
        r->strings = grown;
        r->string_cap = cap;
    }
    r->strings[r->string_count++] = copy;
    return copy;
}

/* ------------------------------------------------------------------ */
/* The name lookups                                                   */
/* ------------------------------------------------------------------ */

/* The event with this name, or NULL. */
static const BwRuleEvent *event_by_name(const char *name)
{
    u32 i;

    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (i = 0; i < BW_RULE_EVENT_COUNT; i++) {
        if (strcmp(g_events[i].name, name) == 0) {
            return &g_events[i];
        }
    }
    return NULL;
}

/* The event with this id, or NULL. */
static const BwRuleEvent *event_by_kind(u32 kind)
{
    u32 i;

    for (i = 0; i < BW_RULE_EVENT_COUNT; i++) {
        if (g_events[i].kind == kind) {
            return &g_events[i];
        }
    }
    return NULL;
}

u32 bw_mod_rule_event_kind(const char *name)
{
    const BwRuleEvent *e = event_by_name(name);

    return e != NULL ? e->kind : 0u;
}

const char *bw_mod_rule_event_name(u32 kind)
{
    const BwRuleEvent *e = event_by_kind(kind);

    return e != NULL ? e->name : NULL;
}

/*
 * The payload field with this name in this event, or NULL.
 */
static const BwRuleField *field_by_name(const BwRuleEvent *event,
                                        const char *name)
{
    u16 i;

    if (event == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < event->field_count; i++) {
        if (strcmp(event->fields[i].name, name) == 0) {
            return &event->fields[i];
        }
    }
    return NULL;
}

/* The state field with this name, or NULL. */
static const BwRuleStateField *state_by_name(const char *name)
{
    u32 i;

    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (i = 0; i < BW_RULE_STATE_COUNT; i++) {
        if (strcmp(g_state_fields[i].name, name) == 0) {
            return &g_state_fields[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The scanner                                                         */
/* ------------------------------------------------------------------ */

/* The tokens of one line. */
enum {
    BW_RULE_TK_END = 0,
    BW_RULE_TK_IDENT,
    BW_RULE_TK_NUMBER,
    BW_RULE_TK_STRING,
    BW_RULE_TK_LPAREN,
    BW_RULE_TK_RPAREN,
    BW_RULE_TK_LBRACKET,
    BW_RULE_TK_RBRACKET,
    BW_RULE_TK_DOT,
    BW_RULE_TK_OP
};

typedef struct BwRuleToken {
    int    kind;
    s32    ival;
    char   text[BW_RULE_TEXT_MAX + 1u];
} BwRuleToken;

typedef struct BwRuleScan {
    const char *p;
    const char *end;
} BwRuleScan;

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(int c)
{
    return is_ident_start(c) || is_digit(c);
}

/*
 * The next token of the line.  Returns 0, or -1 with the reason in
 * `err` (the caller adds the line number).
 */
static int scan_next(BwRuleScan *s, BwRuleToken *t, char *err, size_t err_size)
{
    int c;

    t->kind = BW_RULE_TK_END;
    t->ival = 0;
    t->text[0] = '\0';

    while (s->p < s->end && is_space((unsigned char)*s->p)) {
        s->p++;
    }
    if (s->p >= s->end) {
        return 0;
    }
    c = (unsigned char)*s->p;

    if (is_ident_start(c)) {
        size_t n = 0;

        while (s->p < s->end && is_ident_char((unsigned char)*s->p)) {
            if (n + 1u >= sizeof t->text) {
                snprintf(err, err_size, "the name is too long");
                return -1;
            }
            t->text[n++] = *s->p++;
        }
        t->text[n] = '\0';
        t->kind = BW_RULE_TK_IDENT;
        return 0;
    }

    if (is_digit(c)) {
        unsigned long v = 0;

        while (s->p < s->end && is_digit((unsigned char)*s->p)) {
            v = v * 10ul + (unsigned long)(*s->p - '0');
            if (v > 2147483647ul) {
                snprintf(err, err_size, "the number is out of range");
                return -1;
            }
            s->p++;
        }
        t->ival = (s32)v;
        t->kind = BW_RULE_TK_NUMBER;
        return 0;
    }

    if (c == '"') {
        size_t n = 0;

        s->p++;
        while (s->p < s->end && *s->p != '"') {
            char ch = *s->p++;

            if (ch == '\\' && s->p < s->end) {
                char esc = *s->p++;

                switch (esc) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                default: ch = esc; break;
                }
            }
            if (n + 1u >= sizeof t->text) {
                snprintf(err, err_size, "the text is too long");
                return -1;
            }
            t->text[n++] = ch;
        }
        if (s->p >= s->end) {
            snprintf(err, err_size, "the text is not closed");
            return -1;
        }
        s->p++;    /* the closing quote */
        t->text[n] = '\0';
        t->kind = BW_RULE_TK_STRING;
        return 0;
    }

    switch (c) {
    case '(': s->p++; t->kind = BW_RULE_TK_LPAREN; return 0;
    case ')': s->p++; t->kind = BW_RULE_TK_RPAREN; return 0;
    case '[': s->p++; t->kind = BW_RULE_TK_LBRACKET; return 0;
    case ']': s->p++; t->kind = BW_RULE_TK_RBRACKET; return 0;
    case '.': s->p++; t->kind = BW_RULE_TK_DOT; return 0;
    default:
        break;
    }

    /* The operators. */
    {
        char op[3];
        size_t n = 0;

        while (s->p < s->end && n + 1u < sizeof op) {
            char ch = *s->p;

            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' ||
                ch == '<' || ch == '>' || ch == '=' || ch == '!' || ch == '&' ||
                ch == '|') {
                op[n++] = ch;
                s->p++;
                continue;
            }
            break;
        }
        op[n] = '\0';
        if (n == 0) {
            snprintf(err, err_size, "the character '%c' is not in the language",
                     c);
            return -1;
        }
        if (n == 2) {
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                t->kind = BW_RULE_TK_OP;
                memcpy(t->text, op, n + 1u);
                return 0;
            }
            /* Not a pair the language has: the first character is an
             * operator on its own and the second starts the next token, so
             * `x*-1` and `a<-1` read as they look. */
            s->p--;
            n = 1u;
            op[1] = '\0';
        }
        if (strcmp(op, "=") == 0) {
            snprintf(err, err_size,
                     "'=' is not an operator; the language has '=='");
            return -1;
        }
        if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0) {
            snprintf(err, err_size,
                     "'%s' is not an operator; the language has '%s%s'",
                     op, op, op);
            return -1;
        }
        t->kind = BW_RULE_TK_OP;
        memcpy(t->text, op, n + 1u);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* The parser                                                          */
/* ------------------------------------------------------------------ */

/* The parser's state. */
typedef struct BwRuleParse {
    BwRule      *rule;
    const BwRuleEvent *event;   /* the event the rule binds */
    BwRuleScan   scan;
    BwRuleToken  tok;           /* the current token */
    char         err[192];
    int          err_line;
    int          depth;
} BwRuleParse;

static void parse_error(BwRuleParse *p, int line, const char *fmt, ...)
{
    va_list ap;

    if (p->err[0] != '\0') {
        return;     /* the first error is the one that counts */
    }
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    p->err_line = line;
}

/* A new node of the rule's tree. */
static int new_node(BwRuleParse *p, int op, int type)
{
    BwRule *r = p->rule;
    BwRuleNode *n;
    u32 cap;
    BwRuleNode *grown;

    if (r->node_count >= BW_RULE_NODES_MAX) {
        parse_error(p, p->err_line, "the rule has too many expressions");
        return -1;
    }
    if (r->node_count >= r->node_cap) {
        cap = r->node_cap == 0 ? 32u : r->node_cap * 2u;
        if (cap > BW_RULE_NODES_MAX) {
            cap = BW_RULE_NODES_MAX;
        }
        grown = (BwRuleNode *)realloc(r->nodes, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            parse_error(p, p->err_line, "no room for the rule's expressions");
            return -1;
        }
        r->nodes = grown;
        r->node_cap = cap;
    }
    n = &r->nodes[r->node_count];
    memset(n, 0, sizeof(*n));
    n->op = (u8)op;
    n->type = (u8)type;
    n->left = BW_RULE_NO_NODE;
    n->right = BW_RULE_NO_NODE;
    n->sub = BW_RULE_NO_NODE;
    n->text = NULL;
    return (int)r->node_count++;
}

/* The next token of the line. */
static int p_advance(BwRuleParse *p)
{
    if (scan_next(&p->scan, &p->tok, p->err, sizeof p->err) != 0) {
        if (p->err[0] == '\0') {
            snprintf(p->err, sizeof p->err, "the line could not be read");
        }
        p->err_line = p->err_line > 0 ? p->err_line : 0;
        return -1;
    }
    return 0;
}

/* The current token is an operator with this text. */
static int tok_is_op(const BwRuleParse *p, const char *op)
{
    return p->tok.kind == BW_RULE_TK_OP && strcmp(p->tok.text, op) == 0;
}

/* The current token is this identifier. */
static int tok_is_ident(const BwRuleParse *p, const char *id)
{
    return p->tok.kind == BW_RULE_TK_IDENT && strcmp(p->tok.text, id) == 0;
}

/*
 * The types that may be compared with each other.  The language document
 * says a comparison needs the same type on both sides; a bool reads as
 * its number, so a bool and a number may be compared too.
 */
static int types_comparable(int a, int b)
{
    if (a == b) {
        return 1;
    }
    if ((a == BW_RULE_T_BOOL && b == BW_RULE_T_INT) ||
        (a == BW_RULE_T_INT && b == BW_RULE_T_BOOL)) {
        return 1;
    }
    return 0;
}

/* Whether the type reads as a number. */
static int type_is_number(int t)
{
    return t == BW_RULE_T_INT || t == BW_RULE_T_BOOL;
}

static int parse_expr(BwRuleParse *p);

/*
 * The subscript of a name: `[<expr>]`.  Returns the node, or
 * BW_RULE_NO_NODE when there is none.
 */
static int parse_subscript(BwRuleParse *p)
{
    int node;

    if (p->tok.kind != BW_RULE_TK_LBRACKET) {
        return BW_RULE_NO_NODE;
    }
    if (p_advance(p) != 0) {
        return BW_RULE_NO_NODE;
    }
    node = parse_expr(p);
    if (node < 0) {
        return BW_RULE_NO_NODE;
    }
    if (p->tok.kind != BW_RULE_TK_RBRACKET) {
        parse_error(p, p->err_line, "the subscript is not closed");
        return BW_RULE_NO_NODE;
    }
    if (p_advance(p) != 0) {
        return BW_RULE_NO_NODE;
    }
    return node;
}

/*
 * A name: `event.<field>`, `state.<cell>` or a state field, each with an
 * optional subscript.  Returns the node, or -1.
 */
static int parse_name(BwRuleParse *p)
{
    const BwRuleStateField *sf;
    const BwRuleField *f;
    int node;
    int sub;

    if (p->tok.kind != BW_RULE_TK_IDENT) {
        parse_error(p, p->err_line, "a name was expected");
        return -1;
    }

    if (tok_is_ident(p, "event")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'event'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a payload field's name was expected");
            return -1;
        }
        f = field_by_name(p->event, p->tok.text);
        if (f == NULL) {
            parse_error(p, p->err_line,
                        "the event %s has no field '%s'",
                        p->event->name, p->tok.text);
            return -1;
        }
        if (f->kind == BW_RULE_FK_ARRAY) {
            parse_error(p, p->err_line,
                        "the field '%s' is a list, and a rule cannot read it",
                        f->name);
            return -1;
        }
        node = new_node(p, BW_RULE_OP_EVENT,
                        f->kind == BW_RULE_FK_MON ? BW_RULE_T_MON
                                                  : BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].field = (u8)(f - p->event->fields);
        p->rule->nodes[node].text = f->name;
        if (p_advance(p) != 0) {
            return -1;
        }
        return node;
    }

    /* table.<name>[key]: a value of one of the mod's own data tables
     * (sdk/mod_table.h); a key the table does not have reads its
     * default. */
    if (tok_is_ident(p, "table")) {
        int sub;

        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'table'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a table's name was expected");
            return -1;
        }
        if (!bw_mod_table_exists(p->rule->ns, p->tok.text)) {
            parse_error(p, p->err_line, "the mod has no table '%s' (a "
                        "`table` line in its manifest)", p->tok.text);
            return -1;
        }
        node = new_node(p, BW_RULE_OP_TABLE, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].text = rule_string(p->rule, p->tok.text);
        if (p->rule->nodes[node].text == NULL) {
            parse_error(p, p->err_line, "no room for the table's name");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        sub = parse_subscript(p);
        if (p->err[0] != '\0') {
            return -1;
        }
        if (sub == BW_RULE_NO_NODE) {
            parse_error(p, p->err_line, "a table is read with a key: "
                        "table.<name>[key]");
            return -1;
        }
        if (!type_is_number(p->rule->nodes[sub].type)) {
            parse_error(p, p->err_line, "a table's key must be a number");
            return -1;
        }
        p->rule->nodes[node].sub = (u16)sub;
        return node;
    }

    /* setting.<key>: the value of one of the mod's own settings (the
     * settings declaration in its manifest), read when the rule runs. */
    if (tok_is_ident(p, "setting")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'setting'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a setting's key was expected");
            return -1;
        }
        if (p->rule->ns[0] == '\0') {
            parse_error(p, p->err_line,
                        "the rule has no namespace, so it has no settings");
            return -1;
        }
        node = new_node(p, BW_RULE_OP_SETTING, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        {
            /* A key may hold dots (`setting.nuzlocke.dupes`): the words
             * after the first are joined back with them. */
            char key[512];

            snprintf(key, sizeof key, "%s", p->tok.text);
            if (p_advance(p) != 0) {
                return -1;
            }
            while (p->tok.kind == BW_RULE_TK_DOT) {
                size_t n = strlen(key);

                if (p_advance(p) != 0) {
                    return -1;
                }
                if (p->tok.kind != BW_RULE_TK_IDENT) {
                    parse_error(p, p->err_line, "a word was expected after "
                                "'.' in the setting's key");
                    return -1;
                }
                snprintf(key + n, sizeof key - n, ".%s", p->tok.text);
                if (p_advance(p) != 0) {
                    return -1;
                }
            }
            p->rule->nodes[node].text = rule_string(p->rule, key);
        }
        if (p->rule->nodes[node].text == NULL) {
            parse_error(p, p->err_line, "no room for the setting's key");
            return -1;
        }
        return node;
    }

    if (tok_is_ident(p, "state")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'state'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a cell's name was expected");
            return -1;
        }
        if (p->rule->ns[0] == '\0') {
            parse_error(p, p->err_line,
                        "the rule has no state namespace, so it has no cells");
            return -1;
        }
        node = new_node(p, BW_RULE_OP_CELL, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->uses_cells = 1;
        p->rule->nodes[node].text = rule_string(p->rule, p->tok.text);
        if (p->rule->nodes[node].text == NULL) {
            parse_error(p, p->err_line, "no room for the cell's name");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        sub = parse_subscript(p);
        if (p->err[0] != '\0') {
            return -1;
        }
        if (sub != BW_RULE_NO_NODE) {
            if (p->rule->nodes[sub].type != BW_RULE_T_INT) {
                parse_error(p, p->err_line,
                            "a cell's key must be a number");
                return -1;
            }
            p->rule->nodes[node].sub = (u16)sub;
        }
        return node;
    }

    /* A state field. */
    sf = state_by_name(p->tok.text);
    if (sf == NULL) {
        parse_error(p, p->err_line, "'%s' is not a name of the language",
                    p->tok.text);
        return -1;
    }
    if (sf->readable == 0) {
        /* The name is one of the language's, but the port cannot answer it
         * yet, so no rule may use it. */
        parse_error(p, p->err_line,
                    "the port cannot read '%s' yet, so no rule may use it",
                    sf->name);
        return -1;
    }
    if (sf->kind == BW_RULE_SK_PLAIN) {
        if (p->tok.kind == BW_RULE_TK_LBRACKET) {
            parse_error(p, p->err_line, "'%s' takes no subscript", sf->name);
            return -1;
        }
    }
    node = new_node(p, BW_RULE_OP_STATE, BW_RULE_T_INT);
    if (node < 0) {
        return -1;
    }
    p->rule->nodes[node].state = (u8)(sf - g_state_fields);
    p->rule->nodes[node].text = sf->name;
    if (p_advance(p) != 0) {
        return -1;
    }
    sub = parse_subscript(p);
    if (p->err[0] != '\0') {
        return -1;
    }
    if (sf->kind == BW_RULE_SK_PLAIN) {
        if (sub != BW_RULE_NO_NODE) {
            parse_error(p, p->err_line, "'%s' takes no subscript", sf->name);
            return -1;
        }
    } else {
        if (sub == BW_RULE_NO_NODE) {
            parse_error(p, p->err_line, "'%s' needs a subscript", sf->name);
            return -1;
        }
        if (sf->kind == BW_RULE_SK_MON) {
            if (p->rule->nodes[sub].type != BW_RULE_T_MON &&
                p->rule->nodes[sub].type != BW_RULE_T_INT) {
                parse_error(p, p->err_line,
                            "'%s' takes a Pokemon or a party position",
                            sf->name);
                return -1;
            }
        } else if (p->rule->nodes[sub].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "'%s' takes a number", sf->name);
            return -1;
        }
        p->rule->nodes[node].sub = (u16)sub;
    }
    return node;
}

/* <atom> */
static int parse_atom(BwRuleParse *p)
{
    int node;

    switch (p->tok.kind) {
    case BW_RULE_TK_NUMBER:
        node = new_node(p, BW_RULE_OP_NUM, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].ival = p->tok.ival;
        if (p_advance(p) != 0) {
            return -1;
        }
        return node;

    case BW_RULE_TK_STRING:
        node = new_node(p, BW_RULE_OP_TEXT, BW_RULE_T_TEXT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].text = rule_string(p->rule, p->tok.text);
        if (p->rule->nodes[node].text == NULL) {
            parse_error(p, p->err_line, "no room for the text");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        return node;

    case BW_RULE_TK_IDENT:
        if (strcmp(p->tok.text, "true") == 0) {
            node = new_node(p, BW_RULE_OP_BOOL, BW_RULE_T_BOOL);
            if (node < 0) {
                return -1;
            }
            p->rule->nodes[node].ival = 1;
            if (p_advance(p) != 0) {
                return -1;
            }
            return node;
        }
        if (strcmp(p->tok.text, "false") == 0) {
            node = new_node(p, BW_RULE_OP_BOOL, BW_RULE_T_BOOL);
            if (node < 0) {
                return -1;
            }
            p->rule->nodes[node].ival = 0;
            if (p_advance(p) != 0) {
                return -1;
            }
            return node;
        }
        return parse_name(p);

    case BW_RULE_TK_LPAREN:
        if (p_advance(p) != 0) {
            return -1;
        }
        node = parse_expr(p);
        if (node < 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_RPAREN) {
            parse_error(p, p->err_line, "the parenthesis is not closed");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        return node;

    case BW_RULE_TK_END:
        parse_error(p, p->err_line, "a value was expected");
        return -1;

    default:
        parse_error(p, p->err_line, "a value was expected");
        return -1;
    }
}

/* <unary> */
static int parse_unary(BwRuleParse *p)
{
    int operand;
    int node;

    if (tok_is_op(p, "-")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        operand = parse_atom(p);
        if (operand < 0) {
            return -1;
        }
        if (p->rule->nodes[operand].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "'-' needs a number");
            return -1;
        }
        node = new_node(p, BW_RULE_OP_NEG, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].left = (u16)operand;
        return node;
    }
    return parse_atom(p);
}

/* <product> */
static int parse_product(BwRuleParse *p)
{
    int left;
    int right;
    int node;
    int op;

    left = parse_unary(p);
    if (left < 0) {
        return -1;
    }
    for (;;) {
        if (tok_is_op(p, "*")) {
            op = BW_RULE_OP_MUL;
        } else if (tok_is_op(p, "/")) {
            op = BW_RULE_OP_DIV;
        } else if (tok_is_op(p, "%")) {
            op = BW_RULE_OP_MOD;
        } else {
            break;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        right = parse_unary(p);
        if (right < 0) {
            return -1;
        }
        if (p->rule->nodes[left].type != BW_RULE_T_INT ||
            p->rule->nodes[right].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "arithmetic needs numbers");
            return -1;
        }
        node = new_node(p, op, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].left = (u16)left;
        p->rule->nodes[node].right = (u16)right;
        left = node;
    }
    return left;
}

/* <sum> */
static int parse_sum(BwRuleParse *p)
{
    int left;
    int right;
    int node;
    int op;

    left = parse_product(p);
    if (left < 0) {
        return -1;
    }
    for (;;) {
        if (tok_is_op(p, "+")) {
            op = BW_RULE_OP_ADD;
        } else if (tok_is_op(p, "-")) {
            op = BW_RULE_OP_SUB;
        } else {
            break;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        right = parse_product(p);
        if (right < 0) {
            return -1;
        }
        if (p->rule->nodes[left].type != BW_RULE_T_INT ||
            p->rule->nodes[right].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "arithmetic needs numbers");
            return -1;
        }
        node = new_node(p, op, BW_RULE_T_INT);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].left = (u16)left;
        p->rule->nodes[node].right = (u16)right;
        left = node;
    }
    return left;
}

/* <cmp> */
static int parse_cmp(BwRuleParse *p)
{
    int left;
    int right;
    int node;
    int op;

    left = parse_sum(p);
    if (left < 0) {
        return -1;
    }
    if (tok_is_op(p, "==")) {
        op = BW_RULE_OP_EQ;
    } else if (tok_is_op(p, "!=")) {
        op = BW_RULE_OP_NE;
    } else if (tok_is_op(p, "<")) {
        op = BW_RULE_OP_LT;
    } else if (tok_is_op(p, "<=")) {
        op = BW_RULE_OP_LE;
    } else if (tok_is_op(p, ">")) {
        op = BW_RULE_OP_GT;
    } else if (tok_is_op(p, ">=")) {
        op = BW_RULE_OP_GE;
    } else {
        return left;
    }
    if (p_advance(p) != 0) {
        return -1;
    }
    right = parse_sum(p);
    if (right < 0) {
        return -1;
    }
    if (!types_comparable(p->rule->nodes[left].type,
                          p->rule->nodes[right].type)) {
        parse_error(p, p->err_line,
                    "the two sides of the comparison have different types");
        return -1;
    }
    node = new_node(p, op, BW_RULE_T_BOOL);
    if (node < 0) {
        return -1;
    }
    p->rule->nodes[node].left = (u16)left;
    p->rule->nodes[node].right = (u16)right;
    return node;
}

/* <not> */
static int parse_not(BwRuleParse *p)
{
    int operand;
    int node;

    if (!tok_is_op(p, "!")) {
        return parse_cmp(p);
    }
    if (p_advance(p) != 0) {
        return -1;
    }
    operand = parse_cmp(p);
    if (operand < 0) {
        return -1;
    }
    if (!type_is_number(p->rule->nodes[operand].type)) {
        parse_error(p, p->err_line, "'!' needs true or false");
        return -1;
    }
    node = new_node(p, BW_RULE_OP_NOT, BW_RULE_T_BOOL);
    if (node < 0) {
        return -1;
    }
    p->rule->nodes[node].left = (u16)operand;
    return node;
}

/* <and> */
static int parse_and(BwRuleParse *p)
{
    int left;
    int right;
    int node;

    left = parse_not(p);
    if (left < 0) {
        return -1;
    }
    while (tok_is_op(p, "&&")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        right = parse_not(p);
        if (right < 0) {
            return -1;
        }
        if (!type_is_number(p->rule->nodes[left].type) ||
            !type_is_number(p->rule->nodes[right].type)) {
            parse_error(p, p->err_line, "'&&' needs true or false");
            return -1;
        }
        node = new_node(p, BW_RULE_OP_AND, BW_RULE_T_BOOL);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].left = (u16)left;
        p->rule->nodes[node].right = (u16)right;
        left = node;
    }
    return left;
}

/* <or> */
static int parse_or(BwRuleParse *p)
{
    int left;
    int right;
    int node;

    left = parse_and(p);
    if (left < 0) {
        return -1;
    }
    while (tok_is_op(p, "||")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        right = parse_and(p);
        if (right < 0) {
            return -1;
        }
        if (!type_is_number(p->rule->nodes[left].type) ||
            !type_is_number(p->rule->nodes[right].type)) {
            parse_error(p, p->err_line, "'||' needs true or false");
            return -1;
        }
        node = new_node(p, BW_RULE_OP_OR, BW_RULE_T_BOOL);
        if (node < 0) {
            return -1;
        }
        p->rule->nodes[node].left = (u16)left;
        p->rule->nodes[node].right = (u16)right;
        left = node;
    }
    return left;
}

/* <expr> */
static int parse_expr(BwRuleParse *p)
{
    int node;

    if (p->depth >= (int)BW_RULE_DEPTH_MAX) {
        parse_error(p, p->err_line, "the expression is too deep");
        return -1;
    }
    p->depth++;
    node = parse_or(p);
    p->depth--;
    return node;
}

/* ------------------------------------------------------------------ */
/* The targets                                                        */
/* ------------------------------------------------------------------ */

/* A target of an action. */
typedef struct BwRuleTarget {
    u8          kind;    /* BW_RULE_TGT_* */
    u8          field;   /* the payload field index */
    u8          state;   /* the state field index */
    u16         sub;     /* the subscript, or BW_RULE_NO_NODE */
    const char *cell;    /* the cell's name */
} BwRuleTarget;

/*
 * A target: `event.<field>`, `state.<cell>` or a state field, each with an
 * optional subscript.  The target must be one a rule may write.
 */
static int parse_target(BwRuleParse *p, BwRuleTarget *t)
{
    const BwRuleStateField *sf;
    const BwRuleField *f;
    int sub;

    memset(t, 0, sizeof(*t));
    t->sub = BW_RULE_NO_NODE;

    if (p->tok.kind != BW_RULE_TK_IDENT) {
        parse_error(p, p->err_line, "a target was expected");
        return -1;
    }

    if (tok_is_ident(p, "event")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'event'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a payload field's name was expected");
            return -1;
        }
        f = field_by_name(p->event, p->tok.text);
        if (f == NULL) {
            parse_error(p, p->err_line, "the event %s has no field '%s'",
                        p->event->name, p->tok.text);
            return -1;
        }
        if ((f->flags & BW_RULE_FF_MOD) == 0) {
            parse_error(p, p->err_line,
                        "the field '%s' cannot be changed by a rule",
                        f->name);
            return -1;
        }
        t->kind = BW_RULE_TGT_EVENT;
        t->field = (u8)(f - p->event->fields);
        if (p_advance(p) != 0) {
            return -1;
        }
        return 0;
    }

    if (tok_is_ident(p, "state")) {
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_DOT) {
            parse_error(p, p->err_line, "a '.' was expected after 'state'");
            return -1;
        }
        if (p_advance(p) != 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_IDENT) {
            parse_error(p, p->err_line, "a cell's name was expected");
            return -1;
        }
        if (p->rule->ns[0] == '\0') {
            parse_error(p, p->err_line,
                        "the rule has no state namespace, so it has no cells");
            return -1;
        }
        t->kind = BW_RULE_TGT_CELL;
        t->cell = rule_string(p->rule, p->tok.text);
        if (t->cell == NULL) {
            parse_error(p, p->err_line, "no room for the cell's name");
            return -1;
        }
        p->rule->uses_cells = 1;
        if (p_advance(p) != 0) {
            return -1;
        }
        sub = parse_subscript(p);
        if (p->err[0] != '\0') {
            return -1;
        }
        if (sub != BW_RULE_NO_NODE) {
            if (p->rule->nodes[sub].type != BW_RULE_T_INT) {
                parse_error(p, p->err_line, "a cell's key must be a number");
                return -1;
            }
            t->sub = (u16)sub;
        }
        return 0;
    }

    sf = state_by_name(p->tok.text);
    if (sf == NULL) {
        parse_error(p, p->err_line, "'%s' is not a name of the language",
                    p->tok.text);
        return -1;
    }
    if (sf->writable == 0) {
        parse_error(p, p->err_line, "'%s' cannot be changed by a rule",
                    sf->name);
        return -1;
    }
    if (sf->writable_ok == 0) {
        parse_error(p, p->err_line,
                    "the port cannot write '%s' yet, so no rule may write it",
                    sf->name);
        return -1;
    }
    t->kind = BW_RULE_TGT_STATE;
    t->state = (u8)(sf - g_state_fields);
    if (p_advance(p) != 0) {
        return -1;
    }
    sub = parse_subscript(p);
    if (p->err[0] != '\0') {
        return -1;
    }
    if (sf->kind == BW_RULE_SK_PLAIN) {
        if (sub != BW_RULE_NO_NODE) {
            parse_error(p, p->err_line, "'%s' takes no subscript", sf->name);
            return -1;
        }
    } else {
        if (sub == BW_RULE_NO_NODE) {
            parse_error(p, p->err_line, "'%s' needs a subscript", sf->name);
            return -1;
        }
        if (sf->kind == BW_RULE_SK_MON) {
            if (p->rule->nodes[sub].type != BW_RULE_T_MON &&
                p->rule->nodes[sub].type != BW_RULE_T_INT) {
                parse_error(p, p->err_line,
                            "'%s' takes a Pokemon or a party position",
                            sf->name);
                return -1;
            }
        } else if (p->rule->nodes[sub].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "'%s' takes a number", sf->name);
            return -1;
        }
        t->sub = (u16)sub;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The actions                                                        */
/* ------------------------------------------------------------------ */

/*
 * One action of a rule: `refuse`, `set`, `add`, `sub`, `message` or
 * `send`.  The line's text starts after the keyword.
 */
static int parse_action(BwRuleParse *p, const char *keyword)
{
    BwRule *r = p->rule;
    BwRuleAction *a;
    BwRuleTarget t;
    int expr;

    if (r->action_count >= BW_RULE_ACTIONS_MAX) {
        parse_error(p, p->err_line, "the rule has too many actions");
        return -1;
    }
    a = &r->actions[r->action_count];
    memset(a, 0, sizeof(*a));
    a->expr = BW_RULE_NO_NODE;
    a->sub = BW_RULE_NO_NODE;

    if (strcmp(keyword, "refuse") == 0) {
        if (p->tok.kind != BW_RULE_TK_END) {
            parse_error(p, p->err_line, "'refuse' takes nothing");
            return -1;
        }
        if (p->event->refusable == 0) {
            parse_error(p, p->err_line,
                        "the event %s cannot be refused", p->event->name);
            return -1;
        }
        a->op = BW_RULE_ACT_REFUSE;
        r->action_count++;
        return 0;
    }

    if (strcmp(keyword, "set") == 0 || strcmp(keyword, "add") == 0 ||
        strcmp(keyword, "sub") == 0) {
        if (parse_target(p, &t) != 0) {
            return -1;
        }
        expr = parse_expr(p);
        if (expr < 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_END) {
            parse_error(p, p->err_line, "the action has more than it needs");
            return -1;
        }
        if (!type_is_number(r->nodes[expr].type)) {
            parse_error(p, p->err_line, "the value must be a number");
            return -1;
        }
        a->op = strcmp(keyword, "set") == 0 ? BW_RULE_ACT_SET :
                (strcmp(keyword, "add") == 0 ? BW_RULE_ACT_ADD
                                             : BW_RULE_ACT_SUB);
        a->tkind = t.kind;
        a->field = t.field;
        a->state = t.state;
        a->sub = t.sub;
        a->cell = t.cell;
        a->expr = (u16)expr;
        r->action_count++;
        return 0;
    }

    if (strcmp(keyword, "message") == 0 || strcmp(keyword, "send") == 0) {
        expr = parse_expr(p);
        if (expr < 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_END) {
            parse_error(p, p->err_line, "the action has more than it needs");
            return -1;
        }
        if (r->nodes[expr].type != BW_RULE_T_TEXT &&
            r->nodes[expr].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "the value must be a text or a "
                        "number");
            return -1;
        }
        a->op = strcmp(keyword, "message") == 0 ? BW_RULE_ACT_MESSAGE
                                                 : BW_RULE_ACT_SEND;
        a->expr = (u16)expr;
        r->action_count++;
        return 0;
    }

    if (strcmp(keyword, "release") == 0) {
        expr = parse_expr(p);
        if (expr < 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_END) {
            parse_error(p, p->err_line, "the action has more than it needs");
            return -1;
        }
        if (r->nodes[expr].type != BW_RULE_T_MON &&
            r->nodes[expr].type != BW_RULE_T_INT) {
            parse_error(p, p->err_line, "the value must be a Pokemon or a "
                        "party position");
            return -1;
        }
        a->op = BW_RULE_ACT_RELEASE;
        a->expr = (u16)expr;
        r->action_count++;
        return 0;
    }

    /* The two-value actions: give <item> <count>, box <mon> <box>,
     * withdraw <box> <slot>. */
    if (strcmp(keyword, "give") == 0 || strcmp(keyword, "box") == 0 ||
        strcmp(keyword, "withdraw") == 0) {
        int count;
        int first_mon = strcmp(keyword, "box") == 0;

        expr = parse_expr(p);
        if (expr < 0) {
            return -1;
        }
        count = parse_expr(p);
        if (count < 0) {
            return -1;
        }
        if (p->tok.kind != BW_RULE_TK_END) {
            parse_error(p, p->err_line, "the action has more than it needs");
            return -1;
        }
        if ((first_mon ? (r->nodes[expr].type != BW_RULE_T_MON &&
                          r->nodes[expr].type != BW_RULE_T_INT)
                       : !type_is_number(r->nodes[expr].type)) ||
            !type_is_number(r->nodes[count].type)) {
            parse_error(p, p->err_line, first_mon
                        ? "'box' takes a Pokemon or a party position, then a "
                          "box number"
                        : "the action's two values must be numbers");
            return -1;
        }
        a->op = strcmp(keyword, "give") == 0 ? BW_RULE_ACT_GIVE :
                (first_mon ? BW_RULE_ACT_BOX : BW_RULE_ACT_WITHDRAW);
        a->expr = (u16)expr;
        a->sub = (u16)count;    /* the count rides in the subscript slot */
        r->action_count++;
        return 0;
    }

    parse_error(p, p->err_line, "'%s' is not an action of the language",
                keyword);
    return -1;
}

/* ------------------------------------------------------------------ */
/* The file                                                           */
/* ------------------------------------------------------------------ */

/* Read a file into memory.  The caller frees it. */
static char *read_file(const char *path, u32 *out_len)
{
    FILE *f;
    long len;
    char *buf;
    size_t got;

    *out_len = 0;
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0 || (u32)len > BW_RULE_FILE_MAX) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)len + 1u);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *out_len = (u32)got;
    return buf;
}

/* The next line of the text: its start, its end and its number. */
typedef struct BwRuleLines {
    const char *p;
    const char *end;
    int         line;
} BwRuleLines;

/* The start of the next line, or NULL at the end of the text. */
static const char *line_next(BwRuleLines *l, const char **out_end)
{
    const char *start;
    const char *q;

    if (l->p >= l->end) {
        return NULL;
    }
    start = l->p;
    q = start;
    while (q < l->end && *q != '\n') {
        q++;
    }
    *out_end = q;
    l->p = (q < l->end) ? q + 1 : l->end;
    l->line++;
    return start;
}

/*
 * The text of a line without its comment and without its spaces.  Returns
 * 0, or -1 when the line does not fit the buffer.
 */
static int line_trim(const char *start, const char *end, char *out,
                     size_t out_size)
{
    size_t n = 0;
    const char *q = start;

    while (q < end && *q != '#') {
        q++;
    }
    end = q;
    while (start < end && (*start == ' ' || *start == '\t')) {
        start++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r')) {
        end--;
    }
    if ((size_t)(end - start) + 1u > out_size) {
        return -1;
    }
    while (start < end) {
        out[n++] = *start++;
    }
    out[n] = '\0';
    return 0;
}

/* The word at the start of the line, and the rest of it. */
static int line_word(const char *line, char *word, size_t word_size,
                     const char **rest)
{
    size_t n = 0;

    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return 0;
    }
    while (line[n] != '\0' && line[n] != ' ' && line[n] != '\t') {
        n++;
    }
    if (n + 1u > word_size) {
        return -1;
    }
    memcpy(word, line, n);
    word[n] = '\0';
    *rest = line + n;
    return 1;
}

/*
 * The dotted name at the start of the text.  The text is left after the
 * name, so the caller can read what follows.
 */
static int dotted_name(const char *text, char *out, size_t out_size,
                       const char **rest)
{
    size_t n = 0;

    while (*text == ' ' || *text == '\t') {
        text++;
    }
    while (*text != '\0' && *text != ' ' && *text != '\t') {
        if (n + 1u >= out_size) {
            return -1;
        }
        out[n++] = *text++;
    }
    out[n] = '\0';
    *rest = text;
    return n > 0 ? 0 : -1;
}

/*
 * Parse a rule file.  The rule is filled in: its name, its event, its
 * guards and its actions.  Returns 0, or -1 with the reason in the
 * parser's error.
 */
static int parse_rule_file(BwRule *rule, const char *text, u32 len,
                           const BwRuleEvent *event, int *out_line,
                           char *out_err, size_t out_err_size)
{
    BwRuleParse p;
    BwRuleLines lines;
    const char *line;
    const char *end;
    char buf[BW_RULE_LINE_MAX];
    char word[64];
    char name[64];
    const char *rest;
    int have_language = 0;
    int have_rule = 0;
    int rc;

    memset(&p, 0, sizeof p);
    p.rule = rule;
    p.event = event;
    p.err[0] = '\0';
    p.err_line = 0;
    *out_line = 0;

    lines.p = text;
    lines.end = text + len;
    lines.line = 0;

    while ((line = line_next(&lines, &end)) != NULL) {
        if (line_trim(line, end, buf, sizeof buf) != 0) {
            snprintf(p.err, sizeof p.err, "the line is too long");
            p.err_line = lines.line;
            goto fail;
        }
        if (buf[0] == '\0') {
            continue;
        }
        rc = line_word(buf, word, sizeof word, &rest);
        if (rc < 0) {
            snprintf(p.err, sizeof p.err, "the line is too long");
            p.err_line = lines.line;
            goto fail;
        }
        if (rc == 0) {
            continue;
        }

        if (!have_language) {
            long version;

            if (strcmp(word, "language") != 0) {
                snprintf(p.err, sizeof p.err,
                         "the file must start with 'language %u'",
                         (unsigned int)BW_RULE_LANGUAGE_VERSION);
                p.err_line = lines.line;
                goto fail;
            }
            if (sscanf(rest, "%ld", &version) != 1) {
                snprintf(p.err, sizeof p.err,
                         "the language version is not a number");
                p.err_line = lines.line;
                goto fail;
            }
            if (version != (long)BW_RULE_LANGUAGE_VERSION) {
                snprintf(p.err, sizeof p.err,
                         "the rule is written for the language version %ld, "
                         "and the port reads version %u", version,
                         (unsigned int)BW_RULE_LANGUAGE_VERSION);
                p.err_line = lines.line;
                goto fail;
            }
            have_language = 1;
            continue;
        }

        if (!have_rule) {
            char ename[64];
            const char *after;

            if (strcmp(word, "rule") != 0) {
                snprintf(p.err, sizeof p.err,
                         "the file must name its rule with 'rule <name> on "
                         "<event>'");
                p.err_line = lines.line;
                goto fail;
            }
            if (dotted_name(rest, name, sizeof name, &after) != 0) {
                snprintf(p.err, sizeof p.err, "the rule has no name");
                p.err_line = lines.line;
                goto fail;
            }
            rest = after;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            if (strncmp(rest, "on", 2) != 0 ||
                (rest[2] != '\0' && rest[2] != ' ' && rest[2] != '\t')) {
                snprintf(p.err, sizeof p.err,
                         "the rule must name its event: 'rule <name> on "
                         "<event>'");
                p.err_line = lines.line;
                goto fail;
            }
            rest += 2;
            if (dotted_name(rest, ename, sizeof ename, &after) != 0) {
                snprintf(p.err, sizeof p.err, "the rule has no event");
                p.err_line = lines.line;
                goto fail;
            }
            if (event_by_name(ename) == NULL) {
                snprintf(p.err, sizeof p.err,
                         "'%s' is not one of the port's events", ename);
                p.err_line = lines.line;
                goto fail;
            }
            if (strcmp(ename, event->name) != 0) {
                snprintf(p.err, sizeof p.err,
                         "the rule is written for the event %s, and the "
                         "manifest names %s", ename, event->name);
                p.err_line = lines.line;
                goto fail;
            }
            copy_str(rule->name, sizeof rule->name, name);
            have_rule = 1;
            continue;
        }

        if (strcmp(word, "when") == 0) {
            int node;

            if (rule->guard_count >= BW_RULE_GUARDS_MAX) {
                snprintf(p.err, sizeof p.err, "the rule has too many guards");
                p.err_line = lines.line;
                goto fail;
            }
            p.err_line = lines.line;
            p.scan.p = rest;
            p.scan.end = rest + strlen(rest);
            p.depth = 0;
            if (p_advance(&p) != 0) {
                goto fail;
            }
            node = parse_expr(&p);
            if (node < 0) {
                goto fail;
            }
            if (p.tok.kind != BW_RULE_TK_END) {
                snprintf(p.err, sizeof p.err,
                         "the guard has more than it needs");
                p.err_line = lines.line;
                goto fail;
            }
            if (!type_is_number(rule->nodes[node].type)) {
                snprintf(p.err, sizeof p.err,
                         "a guard must be true or false");
                p.err_line = lines.line;
                goto fail;
            }
            rule->guards[rule->guard_count++] = (u16)node;
            continue;
        }

        if (strcmp(word, "do") == 0) {
            char action[32];
            const char *rest2;

            p.err_line = lines.line;
            if (line_word(rest, action, sizeof action, &rest2) <= 0) {
                snprintf(p.err, sizeof p.err, "the action is missing");
                p.err_line = lines.line;
                goto fail;
            }
            p.scan.p = rest2;
            p.scan.end = rest2 + strlen(rest2);
            p.depth = 0;
            if (p_advance(&p) != 0) {
                goto fail;
            }
            if (parse_action(&p, action) != 0) {
                goto fail;
            }
            continue;
        }

        snprintf(p.err, sizeof p.err,
                 "'%s' is not a line of a rule", word);
        p.err_line = lines.line;
        goto fail;
    }

    if (!have_language) {
        snprintf(p.err, sizeof p.err, "the file is empty");
        goto fail;
    }
    if (!have_rule) {
        snprintf(p.err, sizeof p.err, "the file names no rule");
        goto fail;
    }
    if (rule->action_count == 0) {
        snprintf(p.err, sizeof p.err, "the rule has no action");
        goto fail;
    }
    return 0;

fail:
    *out_line = p.err_line;
    copy_str(out_err, out_err_size, p.err);
    return -1;
}

/* ------------------------------------------------------------------ */
/* The cells                                                          */
/* ------------------------------------------------------------------ */

/*
 * The namespace the cells' own file lives under.
 *
 * The mod's own file holds the mod's own payload (docs/mod-state.md,
 * section 1), and the rule layer must not write over it: one file per mod
 * is the only isolation the port can give, and a rule that wrote a cell
 * would take the mod's own data with it.  So the cells live in a file of
 * their own, under a namespace derived from the mod's, which the
 * saved-data layer turns into its own folder and its own file.  That file
 * is named after the game save, so the cells still follow the game save
 * (the language document, section 5).
 *
 * The name is the mod's namespace with the suffix, cut so that it stays
 * inside the length the saved-data layer accepts.
 */
static void cells_namespace(char *out, size_t size, const char *ns)
{
    size_t n;
    size_t keep;

    if (size == 0) {
        return;
    }
    n = strlen(ns);
    keep = (size_t)BW_RULE_NS_MAX - (sizeof(BW_RULE_CELLS_SUFFIX) - 1u);
    if (n > keep) {
        n = keep;
    }
    if (n + sizeof(BW_RULE_CELLS_SUFFIX) > size) {
        n = (size > sizeof(BW_RULE_CELLS_SUFFIX))
                ? size - sizeof(BW_RULE_CELLS_SUFFIX) : 0u;
    }
    memcpy(out, ns, n);
    memcpy(out + n, BW_RULE_CELLS_SUFFIX, sizeof(BW_RULE_CELLS_SUFFIX));
}

/* The owner of the cells: the mod with this namespace. */
static BwRuleOwner *owner_get(const char *ns)
{
    u32 i;
    BwRuleOwner *o;

    for (i = 0; i < g_owner_count; i++) {
        if (strcmp(g_owners[i].ns, ns) == 0) {
            return &g_owners[i];
        }
    }
    if (g_owner_count >= BW_RULE_OWNERS_MAX) {
        return NULL;
    }
    o = &g_owners[g_owner_count++];
    memset(o, 0, sizeof(*o));
    copy_str(o->ns, sizeof o->ns, ns);
    cells_namespace(o->file_ns, sizeof o->file_ns, ns);
    return o;
}

/* The cell with this name in this owner, or NULL. */
static BwRuleCell *cell_find(BwRuleOwner *owner, const char *name)
{
    u32 i;

    if (owner == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < owner->cell_count; i++) {
        if (strcmp(owner->cells[i].name, name) == 0) {
            return &owner->cells[i];
        }
    }
    return NULL;
}

/* The cell with this name, made when it is not there. */
static BwRuleCell *cell_make(BwRuleOwner *owner, const char *name)
{
    BwRuleCell *c = cell_find(owner, name);

    if (c != NULL) {
        return c;
    }
    if (owner == NULL || owner->cell_count >= BW_RULE_CELLS_MAX) {
        return NULL;
    }
    c = &owner->cells[owner->cell_count++];
    memset(c, 0, sizeof(*c));
    copy_str(c->name, sizeof c->name, name);
    return c;
}

/* The value of a cell.  An unset cell reads 0 (the language document,
 * section 4). */
static s32 cell_read(BwRuleOwner *owner, const char *name, int keyed, s32 key)
{
    BwRuleCell *c = cell_find(owner, name);
    u32 i;

    if (c == NULL) {
        return 0;
    }
    if (!keyed) {
        return c->value;
    }
    for (i = 0; i < c->count; i++) {
        if (c->keys[i] == key) {
            return c->values[i];
        }
    }
    return 0;
}

/* Write a cell.  Returns 0, or -1 when the cell could not be made. */
static int cell_write(BwRuleOwner *owner, const char *name, int keyed,
                      s32 key, s32 value)
{
    BwRuleCell *c = cell_make(owner, name);
    u32 i;

    if (c == NULL) {
        return -1;
    }
    if (!keyed) {
        c->keyed = 0;
        c->value = value;
        return 0;
    }
    c->keyed = 1;
    for (i = 0; i < c->count; i++) {
        if (c->keys[i] == key) {
            c->values[i] = value;
            return 0;
        }
    }
    if (c->count >= BW_RULE_CELL_KEYS_MAX) {
        return -1;   /* the cell holds at most 1024 keys */
    }
    if (c->count >= c->cap) {
        u32 cap = c->cap == 0 ? 8u : c->cap * 2u;
        s32 *keys;
        s32 *values;

        if (cap > BW_RULE_CELL_KEYS_MAX) {
            cap = BW_RULE_CELL_KEYS_MAX;
        }
        keys = (s32 *)realloc(c->keys, (size_t)cap * sizeof(s32));
        if (keys == NULL) {
            return -1;
        }
        c->keys = keys;
        values = (s32 *)realloc(c->values, (size_t)cap * sizeof(s32));
        if (values == NULL) {
            return -1;
        }
        c->values = values;
        c->cap = cap;
    }
    c->keys[c->count] = key;
    c->values[c->count] = value;
    c->count++;
    return 0;
}

/* The cells' own file: the payload the port stores under the cells'
 * namespace. */
static void cells_store(BwRuleOwner *owner)
{
    u8 *buf;
    u32 size = 12u;
    u32 off;
    u32 i;

    if (owner == NULL || g_cells_store == NULL) {
        return;
    }
    if (!owner->own_file) {
        /* The file holds something that is not the rule layer's, so the
         * cells are not written over it (docs/mod-state.md, section 1:
         * the file boundary is the only isolation the port can give). */
        return;
    }
    for (i = 0; i < owner->cell_count; i++) {
        size += BW_RULE_CELL_NAME_MAX + 12u +
                owner->cells[i].count * 8u;
    }
    buf = (u8 *)malloc(size);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, BW_RULE_CELLS_MAGIC, 4);
    bw_wr32(buf + 4, BW_RULE_CELLS_VERSION);
    bw_wr32(buf + 8, owner->cell_count);
    off = 12u;
    for (i = 0; i < owner->cell_count; i++) {
        const BwRuleCell *c = &owner->cells[i];
        u32 j;

        memset(buf + off, 0, BW_RULE_CELL_NAME_MAX);
        memcpy(buf + off, c->name, strlen(c->name));
        off += BW_RULE_CELL_NAME_MAX;
        bw_wr32(buf + off, c->keyed ? 1u : 0u);
        off += 4u;
        bw_wr32(buf + off, (u32)c->value);
        off += 4u;
        bw_wr32(buf + off, c->count);
        off += 4u;
        for (j = 0; j < c->count; j++) {
            bw_wr32(buf + off, (u32)c->keys[j]);
            off += 4u;
            bw_wr32(buf + off, (u32)c->values[j]);
            off += 4u;
        }
    }
    (void)g_cells_store(owner->file_ns, buf, off);
    free(buf);
}

/* The cells' own file, read back. */
static void cells_load(BwRuleOwner *owner)
{
    u8 *buf;
    s32 got;
    u32 off;
    u32 count;
    u32 i;

    if (owner == NULL || owner->loaded) {
        return;
    }
    owner->loaded = 1;
    if (g_cells_load == NULL) {
        /* The port has given the layer no file for the cells, so the
         * cells live in memory for the run and the file is not ours to
         * write. */
        owner->own_file = 0;
        if (!g_cells_reported) {
            g_cells_reported = 1;
            rule_report(owner->ns, NULL, 0,
                        "the mod's cells are not saved: the port has no file "
                        "for them");
        }
        return;
    }
    buf = (u8 *)malloc(BW_RULE_CELLS_BUF_MAX);
    if (buf == NULL) {
        return;
    }
    got = g_cells_load(owner->file_ns, buf, BW_RULE_CELLS_BUF_MAX);
    if (got <= 0 || (u32)got < 12u) {
        /* No file yet: the cells' file is ours to make. */
        owner->own_file = 1;
        free(buf);
        return;
    }
    if (memcmp(buf, BW_RULE_CELLS_MAGIC, 4) != 0) {
        /* The payload is not the rule layer's: the cells stay in memory,
         * the file is not ours, and the port is told once. */
        owner->own_file = 0;
        if (!owner->reported) {
            owner->reported = 1;
            rule_report(owner->ns, NULL, 0,
                        "the cells' file holds data that is not the rule "
                        "layer's, so the mod's cells are not saved");
        }
        free(buf);
        return;
    }
    owner->own_file = 1;
    if (bw_rd32(buf + 4) != BW_RULE_CELLS_VERSION) {
        free(buf);
        return;
    }
    count = bw_rd32(buf + 8);
    if (count > BW_RULE_CELLS_MAX) {
        free(buf);
        return;
    }
    off = 12u;
    owner->cell_count = 0;
    for (i = 0; i < count; i++) {
        char cname[BW_RULE_CELL_NAME_MAX + 1u];
        u32 keyed;
        u32 value;
        u32 keys;
        u32 j;

        if ((u32)got < off + BW_RULE_CELL_NAME_MAX + 12u) {
            break;
        }
        memcpy(cname, buf + off, BW_RULE_CELL_NAME_MAX);
        cname[BW_RULE_CELL_NAME_MAX] = '\0';
        off += BW_RULE_CELL_NAME_MAX;
        keyed = bw_rd32(buf + off);
        off += 4u;
        value = bw_rd32(buf + off);
        off += 4u;
        keys = bw_rd32(buf + off);
        off += 4u;
        if (keys > BW_RULE_CELL_KEYS_MAX ||
            (u32)got < off + keys * 8u) {
            break;
        }
        for (j = 0; j < keys; j++) {
            s32 k = as_s32(bw_rd32(buf + off));
            s32 v = as_s32(bw_rd32(buf + off + 4u));

            off += 8u;
            if (keyed != 0) {
                (void)cell_write(owner, cname, 1, k, v);
            } else {
                (void)cell_write(owner, cname, 0, 0, (s32)v);
            }
        }
        if (keyed == 0) {
            (void)cell_write(owner, cname, 0, 0, as_s32(value));
        }
    }
    free(buf);
}

/* ------------------------------------------------------------------ */
/* The interpreter                                                     */
/* ------------------------------------------------------------------ */

/* One value of the interpreter. */
typedef struct BwRuleValue {
    u8          type;
    s32         i;
    BwMonRef    mon;
    const char *text;
} BwRuleValue;

/* The interpreter's state, one firing of one rule. */
typedef struct BwRuleVm {
    BwRule      *rule;
    BwEvent     *event;
    BwRuleOwner *owner;
    u32          steps;
    int          budget;    /* the step budget was reached */
    int          refused;
} BwRuleVm;

/* One step of the budget.  Returns 0, or -1 when the budget is gone. */
static int vm_step(BwRuleVm *vm)
{
    if (vm->steps >= BW_RULE_STEPS_MAX) {
        vm->budget = 1;
        return -1;
    }
    vm->steps++;
    return 0;
}

/* The value's truth: a number is true when it is not zero. */
static int value_truth(const BwRuleValue *v)
{
    switch (v->type) {
    case BW_RULE_T_BOOL: return v->i != 0;
    case BW_RULE_T_INT:  return v->i != 0;
    default:             return 0;
    }
}

/* The value's number: a bool reads as its number. */
static s32 value_number(const BwRuleValue *v)
{
    return v->i;
}

/*
 * A problem the port had while the rule ran.  It is reported once per
 * rule: the rule cannot fail while it runs, so the port says what it
 * could not do and the rule stops.
 */
static void runtime_problem(BwRuleVm *vm, const char *what)
{
    if (vm->rule->runtime_reported) {
        return;
    }
    vm->rule->runtime_reported = 1;
    rule_report(vm->rule->label, vm->rule->name, 0, "%s", what);
}

/* ------------------------------------------------------------------ */
/* The state calls                                                     */
/* ------------------------------------------------------------------ */

/* The mon a subscript names: a mon reference, or a party position. */
static int state_mon(const BwRuleValue *v, BwMonRef *out)
{
    const BwModState *st = bw_mod_state_table();

    if (st == NULL) {
        return -1;
    }
    if (v->type == BW_RULE_T_MON) {
        *out = v->mon;
        return 0;
    }
    if (st->party_ref == NULL) {
        return -1;
    }
    if (st->party_ref((u32)(v->i < 0 ? 0 : v->i), out) != 0) {
        return -1;
    }
    return 0;
}

/* A mon's field, through the call that matches where the mon is. */
static int mon_get(BwMonRef mon, u32 field, u32 *out)
{
    const BwModState *st = bw_mod_state_table();

    if (st == NULL || out == NULL) {
        return -1;
    }
    switch (mon.place) {
    case BW_MON_PARTY:
        if (st->party_get != NULL) {
            return st->party_get(mon, field, out);
        }
        break;
    case BW_MON_BOX:
        if (st->box_get != NULL) {
            return st->box_get(mon, field, out);
        }
        break;
    case BW_MON_BATTLE:
        if (st->battle_get != NULL) {
            return st->battle_get(mon, field, out);
        }
        break;
    default:
        break;
    }
    return -1;
}

/* A mon's field, written. */
static int mon_set(BwMonRef mon, u32 field, u32 value)
{
    const BwModState *st = bw_mod_state_table();

    if (st == NULL) {
        return -1;
    }
    switch (mon.place) {
    case BW_MON_PARTY:
        if (st->party_set != NULL) {
            return st->party_set(mon, field, value);
        }
        break;
    case BW_MON_BOX:
        if (st->box_set != NULL) {
            return st->box_set(mon, field, value);
        }
        break;
    case BW_MON_BATTLE:
        if (st->battle_set != NULL) {
            return st->battle_set(mon, field, value);
        }
        break;
    default:
        break;
    }
    return -1;
}

/*
 * The species' own data.  The field ids are POKE_PERSONAL_GetParam's own
 * (src/game/arm9/poke_personal.c), the ones the port's state layer
 * passes straight through: 0 base hp, 1 base attack, 2 base defence,
 * 3 base speed, 4 base special attack, 5 base special defence, 6 type 1,
 * 7 type 2.
 */
#define BW_RULE_PERSONAL_HP    0u
#define BW_RULE_PERSONAL_ATK   1u
#define BW_RULE_PERSONAL_DEF   2u
#define BW_RULE_PERSONAL_SPD   3u
#define BW_RULE_PERSONAL_SPATK 4u
#define BW_RULE_PERSONAL_SPDEF 5u
#define BW_RULE_PERSONAL_TYPE1 6u
#define BW_RULE_PERSONAL_TYPE2 7u

/* Whether a battle is running: the port has a battle record for the
 * player's side only while a battle runs. */
static int state_in_battle(void)
{
    const BwModState *st = bw_mod_state_table();
    BwMonRef ref;
    u32 slot;

    if (st == NULL || st->battle_ref == NULL) {
        return 0;
    }
    for (slot = 0; slot < 6u; slot++) {
        if (st->battle_ref(0u, slot, &ref) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The species data ids the rules name (POKE_PERSONAL_GetParam). */
static const struct {
    const char *name;
    u32         id;
} k_species_fields[] = {
    { "catch_rate", 8u },
    { "gender_ratio", 20u },
    { "growth", 23u },
    { "egg_group1", 24u },
    { "egg_group2", 25u },
    { "species_stage", 36u }
};

/* species_base[s]: the first stage of the line (the state call). */

/* The item and move data ids the rules name (FILE_UTIL_GetItemParam,
 * WAZA_TOOL_GetParam). */
#define BW_RULE_ITEM_POCKET 5u
#define BW_RULE_MOVE_TYPE   0u
#define BW_RULE_MOVE_CLASS  2u
#define BW_RULE_MOVE_POWER  3u

/* A party member's field, by position. */
static int party_field(const BwModState *st, u32 slot, u32 field, u32 *out)
{
    BwMonRef ref;

    if (st->party_ref == NULL || st->party_get == NULL ||
        st->party_ref(slot, &ref) != 0) {
        return -1;
    }
    return st->party_get(ref, field, out);
}

/* Whether the player has a Pokemon of this species, in the party or a
 * box.  An egg does not count. */
static int state_owned(const BwModState *st, u32 species)
{
    u32 n, i, box, slot, v, egg;
    BwMonRef ref;

    if (st->party_count != NULL) {
        n = st->party_count();
        for (i = 0; i < n; i++) {
            if (party_field(st, i, BW_MON_FIELD_SPECIES, &v) == 0 && v == species &&
                party_field(st, i, BW_MON_FIELD_IS_EGG, &egg) == 0 && egg == 0u) {
                return 1;
            }
        }
    }
    if (st->box_count == NULL || st->box_ref == NULL || st->box_get == NULL) {
        return 0;
    }
    n = st->box_count();
    for (box = 0; box < n; box++) {
        for (slot = 0; slot < 30u; slot++) {
            if (st->box_ref(box, slot, &ref) != 0) {
                continue;
            }
            if (st->box_get(ref, BW_MON_FIELD_SPECIES, &v) == 0 && v == species &&
                st->box_get(ref, BW_MON_FIELD_IS_EGG, &egg) == 0 && egg == 0u) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * The state fields interface version 4 added.  Returns 1 when `f` is one
 * of them (the answer is in *out and the result in *rc), 0 otherwise.
 */
static int state_read_v4(const BwModState *st, const BwRuleStateField *f,
                         const BwRuleValue *sub, s32 *out, int *rc)
{
    u32 i, v = 0;

    *rc = 0;
    for (i = 0; i < (u32)(sizeof(k_species_fields) / sizeof(k_species_fields[0])); i++) {
        if (strcmp(f->name, k_species_fields[i].name) == 0) {
            if (st->species_value == NULL || sub == NULL ||
                st->species_value((u16)sub->i, k_species_fields[i].id, &v) != 0) {
                *rc = -1;
                return 1;
            }
            *out = (s32)v;
            return 1;
        }
    }
    if (strcmp(f->name, "area") == 0 || strcmp(f->name, "area_of") == 0) {
        s32 a;
        u32 zone;

        if (st->area_of == NULL) {
            *rc = -1;
            return 1;
        }
        if (strcmp(f->name, "area") == 0) {
            if (st->zone == NULL) {
                *rc = -1;
                return 1;
            }
            zone = st->zone();
        } else {
            if (sub == NULL) {
                *rc = -1;
                return 1;
            }
            zone = (u32)sub->i;
        }
        a = st->area_of((u16)zone);
        if (a < 0) {
            *rc = -1;
            return 1;
        }
        *out = a;
        return 1;
    }
    if (strcmp(f->name, "species_base") == 0) {
        s32 b;

        if (st->species_base == NULL || sub == NULL ||
            (b = st->species_base((u16)sub->i)) < 0) {
            *rc = -1;
            return 1;
        }
        *out = b;
        return 1;
    }
    if (strcmp(f->name, "battle_trainer") == 0 ||
        strcmp(f->name, "battle_trainer2") == 0) {
        s32 id = st->battle_trainer != NULL
                     ? st->battle_trainer(f->name[14] == '2' ? 1u : 0u) : -1;

        *out = id < 0 ? 0 : id;
        return 1;
    }
    if (strcmp(f->name, "wild_battle") == 0) {
        *out = (st->battle_wild != NULL && st->battle_wild() == 1) ? 1 : 0;
        return 1;
    }
    if (strcmp(f->name, "play_seconds") == 0) {
        *out = st->play_seconds != NULL ? (s32)(st->play_seconds() & 0x7FFFFFFFu) : 0;
        return 1;
    }
    if (strcmp(f->name, "owned") == 0) {
        if (sub == NULL) {
            *rc = -1;
            return 1;
        }
        *out = state_owned(st, (u32)sub->i);
        return 1;
    }
    if (strcmp(f->name, "party_max_level") == 0 ||
        strcmp(f->name, "party_alive") == 0 ||
        strcmp(f->name, "party_has_type") == 0) {
        u32 n = st->party_count != NULL ? st->party_count() : 0u;
        s32 best = 0;

        for (i = 0; i < n; i++) {
            u32 egg = 0, x = 0, t1 = 0, t2 = 0;

            if (party_field(st, i, BW_MON_FIELD_IS_EGG, &egg) != 0 || egg != 0u) {
                continue;
            }
            if (strcmp(f->name, "party_max_level") == 0) {
                if (party_field(st, i, BW_MON_FIELD_LEVEL, &x) == 0 && (s32)x > best) {
                    best = (s32)x;
                }
            } else if (strcmp(f->name, "party_alive") == 0) {
                if (party_field(st, i, BW_MON_FIELD_HP, &x) == 0 && x > 0u) {
                    best++;
                }
            } else if (sub != NULL &&
                       party_field(st, i, BW_MON_FIELD_TYPE1, &t1) == 0 &&
                       party_field(st, i, BW_MON_FIELD_TYPE2, &t2) == 0 &&
                       (t1 == (u32)sub->i || t2 == (u32)sub->i)) {
                best = 1;
            }
        }
        *out = best;
        return 1;
    }
    if (strcmp(f->name, "item_pocket") == 0) {
        if (st->item_value == NULL || sub == NULL || sub->i < 0 ||
            st->item_value((u16)sub->i, BW_RULE_ITEM_POCKET, &v) != 0) {
            *rc = -1;
            return 1;
        }
        *out = (s32)v;
        return 1;
    }
    if (strcmp(f->name, "move_type") == 0 || strcmp(f->name, "move_power") == 0 ||
        strcmp(f->name, "move_class") == 0) {
        u32 id = strcmp(f->name, "move_type") == 0 ? BW_RULE_MOVE_TYPE :
                 (strcmp(f->name, "move_power") == 0 ? BW_RULE_MOVE_POWER
                                                     : BW_RULE_MOVE_CLASS);

        if (st->move_value == NULL || sub == NULL || sub->i < 0 ||
            st->move_value((u16)sub->i, id, &v) != 0) {
            *rc = -1;
            return 1;
        }
        *out = (s32)v;
        return 1;
    }
    if (strcmp(f->name, "place") == 0 || strcmp(f->name, "side") == 0) {
        BwMonRef mon;

        if (sub == NULL || state_mon(sub, &mon) != 0) {
            *rc = -1;
            return 1;
        }
        *out = strcmp(f->name, "place") == 0 ? (s32)mon.place : (s32)mon.side;
        return 1;
    }
    return 0;
}

/*
 * Read a state field.  Returns 0, or -1 when the port cannot answer the
 * field; the caller reads 0 then, because a loaded rule cannot fail.
 */
static int state_read(const BwRuleStateField *f, const BwRuleValue *sub,
                      s32 *out)
{
    const BwModState *st = bw_mod_state_table();
    BwMonRef mon;
    u32 value = 0;

    *out = 0;
    if (st == NULL) {
        return -1;
    }

    if (strcmp(f->name, "money") == 0) {
        if (st->money == NULL) {
            return -1;
        }
        *out = (s32)st->money();
        return 0;
    }
    if (strcmp(f->name, "badges") == 0) {
        if (st->badge_count == NULL) {
            return -1;
        }
        *out = (s32)st->badge_count();
        return 0;
    }
    if (strcmp(f->name, "zone") == 0) {
        if (st->zone == NULL) {
            return -1;
        }
        *out = (s32)st->zone();
        return 0;
    }
    if (strcmp(f->name, "player_x") == 0 || strcmp(f->name, "player_z") == 0) {
        s16 x = 0, y = 0, z = 0;

        if (st->player_pos == NULL || st->player_pos(&x, &y, &z) != 0) {
            return -1;
        }
        *out = f->name[7] == 'x' ? (s32)x : (s32)z;
        return 0;
    }
    if (strcmp(f->name, "party_count") == 0) {
        if (st->party_count == NULL) {
            return -1;
        }
        *out = (s32)st->party_count();
        return 0;
    }
    if (strcmp(f->name, "battle_style") == 0) {
        if (st->battle_style == NULL) {
            return -1;
        }
        *out = (s32)st->battle_style();
        return 0;
    }
    if (strcmp(f->name, "in_battle") == 0) {
        *out = state_in_battle();
        return 0;
    }
    {
        int rc;

        if (state_read_v4(st, f, sub, out, &rc)) {
            return rc;
        }
    }
    if (strcmp(f->name, "badge") == 0) {
        if (st->badge_has == NULL || sub == NULL) {
            return -1;
        }
        value = st->badge_has((u32)sub->i);
        *out = value > 0 ? 1 : 0;
        return 0;
    }
    if (strcmp(f->name, "caught") == 0) {
        if (st->dex_caught == NULL || sub == NULL) {
            return -1;
        }
        value = st->dex_caught((u16)sub->i);
        *out = value > 0 ? 1 : 0;
        return 0;
    }
    if (strcmp(f->name, "species_type1") == 0 ||
        strcmp(f->name, "species_type2") == 0 ||
        strcmp(f->name, "species_bst") == 0) {
        u32 field;
        u32 i;

        if (st->species_value == NULL || sub == NULL) {
            return -1;
        }
        if (strcmp(f->name, "species_type1") == 0) {
            field = BW_RULE_PERSONAL_TYPE1;
        } else if (strcmp(f->name, "species_type2") == 0) {
            field = BW_RULE_PERSONAL_TYPE2;
        } else {
            /* The base stat total is the sum of the six base stats. */
            u32 sum = 0;

            for (i = BW_RULE_PERSONAL_HP; i <= BW_RULE_PERSONAL_SPDEF; i++) {
                u32 part = 0;

                if (st->species_value((u16)sub->i, i, &part) != 0) {
                    return -1;
                }
                sum += part;
            }
            *out = (s32)sum;
            return 0;
        }
        if (st->species_value((u16)sub->i, field, &value) != 0) {
            return -1;
        }
        *out = (s32)value;
        return 0;
    }
    if (strcmp(f->name, "item_count") == 0) {
        s32 count;

        if (st->item_count == NULL || sub == NULL || sub->i <= 0 ||
            sub->i > 0xFFFF) {
            return -1;
        }
        count = st->item_count((u16)sub->i);
        if (count < 0) {
            return -1;
        }
        *out = count;
        return 0;
    }

    /* The fields that name one Pokemon. */
    if (sub == NULL) {
        return -1;
    }
    if (state_mon(sub, &mon) != 0) {
        return -1;
    }
    if (strcmp(f->name, "shiny") == 0) {
        u32 personality = 0;
        u32 species = 0;

        if (st->species_shiny == NULL) {
            return -1;
        }
        if (mon_get(mon, BW_RULE_MON_PERSONALITY, &personality) != 0) {
            return -1;
        }
        if (mon_get(mon, BW_MON_FIELD_SPECIES, &species) != 0) {
            return -1;
        }
        value = st->species_shiny(personality, (u16)species);
        *out = value > 0 ? 1 : 0;
        return 0;
    }
    if (f->mon_field == 0) {
        return -1;
    }
    if (mon_get(mon, f->mon_field, &value) != 0) {
        return -1;
    }
    *out = (s32)value;
    return 0;
}

/* Write a state field.  Returns 0, or -1 when the port cannot write it. */
static int state_write(const BwRuleStateField *f, const BwRuleValue *sub,
                       s32 value)
{
    const BwModState *st = bw_mod_state_table();
    BwMonRef mon;

    if (st == NULL) {
        return -1;
    }
    if (strcmp(f->name, "money") == 0) {
        if (st->money_set == NULL) {
            return -1;
        }
        return st->money_set((u32)value);
    }
    if (strcmp(f->name, "hp") == 0) {
        if (sub == NULL) {
            return -1;
        }
        if (state_mon(sub, &mon) != 0) {
            return -1;
        }
        return mon_set(mon, BW_MON_FIELD_HP, (u32)value);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* The values                                                         */
/* ------------------------------------------------------------------ */

/* Read a payload field. */
static void event_read(const BwRuleEvent *event, u16 index, BwEvent *payload,
                       BwRuleValue *out)
{
    const BwRuleField *f = &event->fields[index];
    const u8 *base = (const u8 *)payload;
    u32 raw = 0;

    out->type = BW_RULE_T_INT;
    out->i = 0;
    out->text = NULL;
    out->mon.handle = 0;
    out->mon.place = (u8)BW_MON_PARTY;
    out->mon.side = 0;
    out->mon.box = 0;
    out->mon.slot = 0;

    if (f->kind == BW_RULE_FK_MON) {
        memcpy(&out->mon, base + f->offset, sizeof(BwMonRef));
        out->type = BW_RULE_T_MON;
        return;
    }
    switch (f->size) {
    case 1: {
        u8 v;

        memcpy(&v, base + f->offset, 1u);
        raw = (u32)v;
        break;
    }
    case 2: {
        u16 v;

        memcpy(&v, base + f->offset, 2u);
        raw = (u32)v;
        break;
    }
    default: {
        u32 v;

        memcpy(&v, base + f->offset, 4u);
        raw = v;
        break;
    }
    }
    if ((f->flags & BW_RULE_FF_SIGNED) != 0) {
        if (f->size == 1) {
            out->i = (s32)(s8)(u8)raw;
        } else if (f->size == 2) {
            out->i = (s32)(s16)(u16)raw;
        } else {
            out->i = as_s32(raw);
        }
    } else {
        out->i = as_s32(raw);
    }
}

/* Write a payload field.  Only a field marked *mod may be written. */
static int event_write(const BwRuleEvent *event, u16 index, BwEvent *payload,
                       s32 value)
{
    const BwRuleField *f = &event->fields[index];
    u8 *base = (u8 *)payload;

    if ((f->flags & BW_RULE_FF_MOD) == 0) {
        return -1;
    }
    if (f->kind == BW_RULE_FK_MON) {
        return -1;
    }
    switch (f->size) {
    case 1: {
        u8 v = (u8)value;

        memcpy(base + f->offset, &v, 1u);
        break;
    }
    case 2: {
        u16 v = (u16)value;

        memcpy(base + f->offset, &v, 2u);
        break;
    }
    default: {
        u32 v = (u32)value;

        memcpy(base + f->offset, &v, 4u);
        break;
    }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The evaluation                                                      */
/* ------------------------------------------------------------------ */

static int eval_node(BwRuleVm *vm, u32 index, BwRuleValue *out);

/* Evaluate a node's subscript. */
static int eval_sub(BwRuleVm *vm, const BwRuleNode *n, BwRuleValue *out)
{
    if (n->sub == BW_RULE_NO_NODE) {
        memset(out, 0, sizeof(*out));
        out->type = BW_RULE_T_INT;
        return 0;
    }
    return eval_node(vm, n->sub, out);
}

/* Compare two values. */
static int values_equal(const BwRuleValue *a, const BwRuleValue *b)
{
    if (a->type == BW_RULE_T_TEXT || b->type == BW_RULE_T_TEXT) {
        const char *x = a->type == BW_RULE_T_TEXT ? a->text : "";
        const char *y = b->type == BW_RULE_T_TEXT ? b->text : "";

        return strcmp(x, y) == 0;
    }
    if (a->type == BW_RULE_T_MON || b->type == BW_RULE_T_MON) {
        if (a->type != b->type) {
            return 0;
        }
        return a->mon.handle == b->mon.handle &&
               a->mon.place == b->mon.place &&
               a->mon.side == b->mon.side &&
               a->mon.box == b->mon.box &&
               a->mon.slot == b->mon.slot;
    }
    return value_number(a) == value_number(b);
}

/* The order of two numbers. */
static int values_compare(const BwRuleValue *a, const BwRuleValue *b)
{
    s32 x = value_number(a);
    s32 y = value_number(b);

    if (x < y) {
        return -1;
    }
    if (x > y) {
        return 1;
    }
    return 0;
}

/* The arithmetic, without an overflow that is undefined. */
static s32 arith_add(s32 a, s32 b)
{
    return as_s32((u32)a + (u32)b);
}

static s32 arith_sub(s32 a, s32 b)
{
    return as_s32((u32)a - (u32)b);
}

static s32 arith_mul(s32 a, s32 b)
{
    return as_s32((u32)a * (u32)b);
}

static s32 arith_div(s32 a, s32 b)
{
    if (b == 0) {
        return 0;       /* a division by zero gives 0 */
    }
    if (b == -1) {
        return as_s32(0u - (u32)a);
    }
    return a / b;
}

static s32 arith_mod(s32 a, s32 b)
{
    if (b == 0) {
        return 0;       /* a remainder by zero gives 0 */
    }
    if (b == -1) {
        return 0;
    }
    return a % b;
}

/* The value of one node. */
static int eval_node(BwRuleVm *vm, u32 index, BwRuleValue *out)
{
    const BwRuleNode *n;
    BwRuleValue a;
    BwRuleValue b;
    s32 x;
    s32 y;

    if (index >= vm->rule->node_count) {
        return -1;
    }
    if (vm_step(vm) != 0) {
        return -1;
    }
    n = &vm->rule->nodes[index];

    memset(out, 0, sizeof(*out));
    out->type = n->type;

    switch (n->op) {
    case BW_RULE_OP_NUM:
        out->i = n->ival;
        return 0;

    case BW_RULE_OP_BOOL:
        out->i = n->ival;
        return 0;

    case BW_RULE_OP_TEXT:
        out->text = n->text;
        return 0;

    case BW_RULE_OP_EVENT: {
        const BwRuleEvent *event = vm->rule->event;

        if (event == NULL || vm->event == NULL) {
            return -1;
        }
        event_read(event, n->field, vm->event, out);
        return 0;
    }

    case BW_RULE_OP_STATE: {
        const BwRuleStateField *f = &g_state_fields[n->state];
        BwRuleValue sub;

        if (eval_sub(vm, n, &sub) != 0) {
            return -1;
        }
        if (f->readable == 0) {
            /* The port cannot answer the field yet; the rule reads 0. */
            out->i = 0;
            return 0;
        }
        (void)state_read(f, f->kind == BW_RULE_SK_PLAIN ? NULL : &sub, &x);
        out->i = x;
        return 0;
    }

    case BW_RULE_OP_CELL: {
        BwRuleValue sub;
        int keyed;

        if (eval_sub(vm, n, &sub) != 0) {
            return -1;
        }
        keyed = n->sub != BW_RULE_NO_NODE;
        out->i = cell_read(vm->owner, n->text, keyed, sub.i);
        return 0;
    }

    case BW_RULE_OP_TABLE: {
        BwRuleValue key;
        s32 v = 0;

        if (eval_sub(vm, n, &key) != 0) {
            return -1;
        }
        (void)bw_mod_table_get(vm->rule->ns, n->text, value_number(&key), &v);
        out->i = v;
        return 0;
    }

    case BW_RULE_OP_SETTING: {
        /* The settings model answers for the mod whose namespace is in
         * force; the rule's own is put in for the one read. */
        char saved[64];

        snprintf(saved, sizeof saved, "%s", bw_mod_menu_namespace());
        bw_mod_menu_set_namespace(vm->rule->ns);
        out->i = bw_mod_menu_setting_get(n->text);
        bw_mod_menu_set_namespace(saved);
        return 0;
    }

    case BW_RULE_OP_NEG:
        if (eval_node(vm, n->left, &a) != 0) {
            return -1;
        }
        out->i = arith_sub(0, value_number(&a));
        return 0;

    case BW_RULE_OP_NOT:
        if (eval_node(vm, n->left, &a) != 0) {
            return -1;
        }
        out->i = value_truth(&a) ? 0 : 1;
        return 0;

    case BW_RULE_OP_ADD:
    case BW_RULE_OP_SUB:
    case BW_RULE_OP_MUL:
    case BW_RULE_OP_DIV:
    case BW_RULE_OP_MOD:
        if (eval_node(vm, n->left, &a) != 0 ||
            eval_node(vm, n->right, &b) != 0) {
            return -1;
        }
        x = value_number(&a);
        y = value_number(&b);
        switch (n->op) {
        case BW_RULE_OP_ADD: out->i = arith_add(x, y); break;
        case BW_RULE_OP_SUB: out->i = arith_sub(x, y); break;
        case BW_RULE_OP_MUL: out->i = arith_mul(x, y); break;
        case BW_RULE_OP_DIV: out->i = arith_div(x, y); break;
        default:            out->i = arith_mod(x, y); break;
        }
        return 0;

    case BW_RULE_OP_EQ:
    case BW_RULE_OP_NE:
    case BW_RULE_OP_LT:
    case BW_RULE_OP_LE:
    case BW_RULE_OP_GT:
    case BW_RULE_OP_GE:
        if (eval_node(vm, n->left, &a) != 0 ||
            eval_node(vm, n->right, &b) != 0) {
            return -1;
        }
        switch (n->op) {
        case BW_RULE_OP_EQ:
            out->i = values_equal(&a, &b) ? 1 : 0;
            break;
        case BW_RULE_OP_NE:
            out->i = values_equal(&a, &b) ? 0 : 1;
            break;
        case BW_RULE_OP_LT:
            out->i = values_compare(&a, &b) < 0 ? 1 : 0;
            break;
        case BW_RULE_OP_LE:
            out->i = values_compare(&a, &b) <= 0 ? 1 : 0;
            break;
        case BW_RULE_OP_GT:
            out->i = values_compare(&a, &b) > 0 ? 1 : 0;
            break;
        default:
            out->i = values_compare(&a, &b) >= 0 ? 1 : 0;
            break;
        }
        return 0;

    case BW_RULE_OP_AND:
        if (eval_node(vm, n->left, &a) != 0) {
            return -1;
        }
        if (!value_truth(&a)) {
            out->i = 0;
            return 0;
        }
        if (eval_node(vm, n->right, &b) != 0) {
            return -1;
        }
        out->i = value_truth(&b) ? 1 : 0;
        return 0;

    case BW_RULE_OP_OR:
        if (eval_node(vm, n->left, &a) != 0) {
            return -1;
        }
        if (value_truth(&a)) {
            out->i = 1;
            return 0;
        }
        if (eval_node(vm, n->right, &b) != 0) {
            return -1;
        }
        out->i = value_truth(&b) ? 1 : 0;
        return 0;

    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* The actions                                                        */
/* ------------------------------------------------------------------ */

/* Read a target's value. */
static int target_read(BwRuleVm *vm, const BwRuleTarget *t,
                       const BwRuleValue *sub, BwRuleValue *out)
{
    memset(out, 0, sizeof(*out));
    out->type = BW_RULE_T_INT;
    switch (t->kind) {
    case BW_RULE_TGT_EVENT: {
        const BwRuleEvent *event = vm->rule->event;

        if (event == NULL || vm->event == NULL) {
            return -1;
        }
        event_read(event, t->field, vm->event, out);
        return 0;
    }
    case BW_RULE_TGT_STATE: {
        const BwRuleStateField *f = &g_state_fields[t->state];
        s32 value = 0;

        if (f->readable == 0) {
            return -1;
        }
        (void)state_read(f, f->kind == BW_RULE_SK_PLAIN ? NULL : sub, &value);
        out->i = value;
        return 0;
    }
    default: {
        int keyed = t->sub != BW_RULE_NO_NODE;

        out->i = cell_read(vm->owner, t->cell, keyed, sub->i);
        return 0;
    }
    }
}

/* Write a target's value. */
static int target_write(BwRuleVm *vm, const BwRuleTarget *t,
                        const BwRuleValue *sub, s32 value)
{
    switch (t->kind) {
    case BW_RULE_TGT_EVENT: {
        const BwRuleEvent *event = vm->rule->event;

        if (event == NULL || vm->event == NULL) {
            return -1;
        }
        return event_write(event, t->field, vm->event, value);
    }
    case BW_RULE_TGT_STATE: {
        const BwRuleStateField *f = &g_state_fields[t->state];

        if (f->writable_ok == 0) {
            return -1;
        }
        return state_write(f, f->kind == BW_RULE_SK_PLAIN ? NULL : sub, value);
    }
    default: {
        int keyed = t->sub != BW_RULE_NO_NODE;

        if (cell_write(vm->owner, t->cell, keyed, sub->i, value) != 0) {
            return -1;
        }
        cells_store(vm->owner);
        return 0;
    }
    }
}

/* The text of a value, for the message and the send. */
static const char *value_text(const BwRuleValue *v, char *buf, size_t size)
{
    if (v->type == BW_RULE_T_TEXT) {
        return v->text;
    }
    snprintf(buf, size, "%ld", (long)v->i);
    return buf;
}

/* ------------------------------------------------------------------ */
/* The peer send                                                       */
/* ------------------------------------------------------------------ */

/*
 * The `send` action: the value goes to the peers of the current session
 * (the language document, section 2; docs/mod-services.md, "Peer
 * sessions").
 *
 * The key is the mod's namespace.  The session service keys a message by
 * the mod that is calling (sdk/mod_net.c reads the namespace the
 * saved-data layer has in force), so the rule layer sets that namespace
 * to the rule's own around the send and puts back what was there.  The
 * mod with the same namespace on the other side is then the one that
 * receives the message, so the two sides agree as long as they use the
 * same namespace for the mod.
 *
 * The value is the text of the expression: a text goes as it is, and a
 * number as its digits.  A message is up to 4 KB of the mod's own bytes
 * (docs/mod-services.md), so a value that is longer is not sent.
 *
 * The action never fails the rule: a rule cannot fail while it runs (the
 * language document, section 4), so the port says what it could not do,
 * once, and the rule goes on.
 */

/* The send call the rule layer uses when the port has installed none: the
 * session service's own call, the one the services table carries
 * (sdk/mod_net.h). */
static int rule_send_default(const char *value)
{
    return bw_mod_net_session_send(0u, value, (u32)strlen(value));
}

/* The send's own report: once per rule, and the rule goes on. */
static void send_problem(BwRuleVm *vm, const char *what)
{
    if (vm->rule->send_reported) {
        return;
    }
    vm->rule->send_reported = 1;
    rule_report(vm->rule->label, vm->rule->name, 0, "%s", what);
}

/* The send action. */
static void action_send(BwRuleVm *vm, const BwRuleValue *value)
{
    int (*send)(const char *value);
    const char *text;
    const char *saved;
    char buf[32];
    size_t len;
    int rc;

    text = value_text(value, buf, sizeof buf);
    len = strlen(text);
    if (len > (size_t)BW_MOD_NET_MESSAGE_MAX) {
        send_problem(vm, "the value is longer than a session message, so it "
                         "was not sent");
        return;
    }
    if (vm->rule->ns[0] == '\0') {
        send_problem(vm, "the rule has no namespace, so the value could not "
                         "be keyed for the peers");
        return;
    }
    send = (g_send != NULL) ? g_send : rule_send_default;

    /* The key: the namespace of the mod the rule belongs to, for the time
     * the send takes. */
    saved = bw_mod_savedata_namespace();
    bw_mod_savedata_set_namespace(vm->rule->ns);
    if (strcmp(bw_mod_savedata_namespace(), vm->rule->ns) != 0) {
        /* The layer refused the name, so the message would carry no key
         * and no mod on the other side would take it. */
        bw_mod_savedata_set_namespace(saved);
        send_problem(vm, "the mod's namespace is not usable, so the value "
                         "could not be keyed for the peers");
        return;
    }
    rc = send(text);
    bw_mod_savedata_set_namespace(saved);
    if (rc != 0) {
        if (rc == BW_MOD_NET_E_NO_SESSION) {
            send_problem(vm, "there is no peer session, so the value was not "
                             "sent");
        } else {
            send_problem(vm, "the peer session did not take the value");
        }
    }
}

/* Run one action. */
static int do_action(BwRuleVm *vm, const BwRuleAction *a)
{
    BwRuleTarget t;
    BwRuleValue sub;
    BwRuleValue value;
    char buf[32];

    memset(&t, 0, sizeof t);
    t.kind = a->tkind;
    t.field = a->field;
    t.state = a->state;
    t.sub = a->sub;
    t.cell = a->cell;

    memset(&sub, 0, sizeof sub);
    sub.type = BW_RULE_T_INT;
    if (a->sub != BW_RULE_NO_NODE) {
        if (eval_node(vm, a->sub, &sub) != 0) {
            return -1;
        }
    }

    switch (a->op) {
    case BW_RULE_ACT_REFUSE:
        vm->refused = 1;
        return 0;

    case BW_RULE_ACT_SET:
    case BW_RULE_ACT_ADD:
    case BW_RULE_ACT_SUB:
        if (eval_node(vm, a->expr, &value) != 0) {
            return -1;
        }
        if (a->op == BW_RULE_ACT_SET) {
            if (target_write(vm, &t, &sub, value_number(&value)) != 0) {
                return -1;
            }
            return 0;
        }
        {
            BwRuleValue current;
            s32 sum;

            if (target_read(vm, &t, &sub, &current) != 0) {
                return -1;
            }
            sum = a->op == BW_RULE_ACT_ADD
                      ? arith_add(value_number(&current), value_number(&value))
                      : arith_sub(value_number(&current), value_number(&value));
            if (target_write(vm, &t, &sub, sum) != 0) {
                return -1;
            }
        }
        return 0;

    case BW_RULE_ACT_MESSAGE:
        if (eval_node(vm, a->expr, &value) != 0) {
            return -1;
        }
        bw_mod_message_show(value_text(&value, buf, sizeof buf));
        return 0;

    case BW_RULE_ACT_SEND:
        if (eval_node(vm, a->expr, &value) != 0) {
            return -1;
        }
        action_send(vm, &value);
        return 0;

    case BW_RULE_ACT_RELEASE:
        {
            const BwModState *st = bw_mod_state_table();
            BwMonRef mon;

            if (eval_node(vm, a->expr, &value) != 0) {
                return -1;
            }
            if (st == NULL || st->mon_release == NULL ||
                state_mon(&value, &mon) != 0) {
                return -1;
            }
            return st->mon_release(mon) == 0 ? 0 : -1;
        }

    case BW_RULE_ACT_GIVE:
        {
            const BwModState *st = bw_mod_state_table();
            s32 item, count;

            if (eval_node(vm, a->expr, &value) != 0) {
                return -1;
            }
            item = value_number(&value);
            count = value_number(&sub);     /* evaluated above */
            if (st == NULL || st->item_add == NULL || item <= 0 ||
                item > 0xFFFF || count <= 0 || count > 0xFFFF) {
                return -1;
            }
            return st->item_add((u16)item, (u16)count) == 0 ? 0 : -1;
        }

    case BW_RULE_ACT_BOX:
    case BW_RULE_ACT_WITHDRAW:
        {
            const BwModState *st = bw_mod_state_table();
            s32 second = value_number(&sub);    /* evaluated above */

            if (st == NULL || eval_node(vm, a->expr, &value) != 0 || second < 0) {
                return -1;
            }
            if (a->op == BW_RULE_ACT_BOX) {
                BwMonRef mon;

                if (st->mon_to_box_n == NULL || state_mon(&value, &mon) != 0) {
                    return -1;
                }
                return st->mon_to_box_n(mon, (u32)second) == 0 ? 0 : -1;
            }
            if (st->mon_from_box == NULL || value_number(&value) < 0) {
                return -1;
            }
            return st->mon_from_box((u32)value_number(&value), (u32)second) == 0
                       ? 0 : -1;
        }

    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* The registry                                                       */
/* ------------------------------------------------------------------ */

/*
 * The handler the dispatch holds for an event with rules.  The event's
 * kind is in the payload's header, so one handler serves every event.
 */
static int rule_handler(void *mod_state, BwEvent *event)
{
    (void)mod_state;
    if (event == NULL) {
        return BW_EVENT_CONTINUE;
    }
    return bw_mod_rule_run(event->kind, event);
}

/* Run one rule.  Returns 1 when the rule refused. */
static int run_rule(BwRule *rule, BwEvent *event)
{
    BwRuleVm vm;
    u32 i;
    int failed = 0;

    vm.rule = rule;
    vm.event = event;
    vm.owner = rule->ns[0] != '\0' ? owner_get(rule->ns) : NULL;
    vm.steps = 0;
    vm.budget = 0;
    vm.refused = 0;

    /* The cells are read before the rule looks at them: the load happens
     * at bind time, and this is the backstop for a rule bound without
     * one. */
    if (vm.owner != NULL && rule->uses_cells) {
        cells_load(vm.owner);
    }

    for (i = 0; i < rule->guard_count; i++) {
        BwRuleValue v;

        if (eval_node(&vm, rule->guards[i], &v) != 0) {
            failed = 1;
            break;
        }
        if (!value_truth(&v)) {
            return 0;   /* a guard that is false stops the rule */
        }
    }
    if (failed && !vm.budget) {
        runtime_problem(&vm, "the guard could not be read");
        return 0;
    }
    for (i = 0; i < rule->action_count && !vm.budget; i++) {
        if (vm_step(&vm) != 0) {
            break;
        }
        if (do_action(&vm, &rule->actions[i]) != 0) {
            failed = 1;
            break;
        }
    }
    if (vm.budget) {
        /* The rule stops for this firing, and it does not refuse
         * (the language document, section 4). */
        if (!rule->budget_reported) {
            rule->budget_reported = 1;
            rule_report(rule->label, rule->name, 0,
                        "the rule reached the step budget of %u steps and "
                        "stopped", (unsigned int)BW_RULE_STEPS_MAX);
        }
        return 0;
    }
    if (failed) {
        runtime_problem(&vm, "an action could not be carried out");
        return 0;
    }
    return vm.refused;
}

/* Run the rules of an event, in the order they were bound. */
int bw_mod_rule_run(u32 kind, BwEvent *event)
{
    u32 i;
    int refused = 0;

    for (i = 0; i < g_rule_count; i++) {
        if (g_rules[i]->kind != kind) {
            continue;
        }
        if (run_rule(g_rules[i], event)) {
            refused = 1;
        }
    }
    return refused;
}

u32 bw_mod_rule_count(u32 kind)
{
    u32 i;
    u32 n = 0;

    for (i = 0; i < g_rule_count; i++) {
        if (g_rules[i]->kind == kind) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The compiled rule                                                  */
/* ------------------------------------------------------------------ */

/* Release a compiled rule and everything it holds. */
static void rule_release(BwRule *r)
{
    u32 i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->string_count; i++) {
        free(r->strings[i]);
    }
    free(r->strings);
    free(r->nodes);
    free(r->text);
    free(r);
}

/*
 * Read a rule file, parse it and check it.  The rule is made when it
 * passes; the caller owns it.  NULL when it does not, with the reason in
 * the report.
 */
static BwRule *rule_compile(const char *path, const BwRuleEvent *event,
                             const char *ns, const char *label)
{
    BwRule *rule;
    char *text;
    u32 len = 0;
    int rc;
    int err_line = 0;
    char err[192];

    if (path == NULL || *path == '\0') {
        rule_report(label, NULL, 0, "the rule has no file");
        return NULL;
    }
    text = read_file(path, &len);
    if (text == NULL) {
        rule_report(label, NULL, 0, "the rule file %s could not be read",
                    path);
        return NULL;
    }
    rule = (BwRule *)malloc(sizeof *rule);
    if (rule == NULL) {
        free(text);
        rule_report(label, NULL, 0, "no room for the rule");
        return NULL;
    }
    memset(rule, 0, sizeof *rule);
    rule->kind = event->kind;
    rule->event = event;
    rule->text = text;
    copy_str(rule->label, sizeof rule->label, label);
    copy_str(rule->ns, sizeof rule->ns, ns);

    rc = parse_rule_file(rule, text, len, event, &err_line, err, sizeof err);
    if (rc != 0) {
        rule_report(label, rule->name[0] != '\0' ? rule->name : NULL,
                    err_line, "%s", err);
        rule_release(rule);
        return NULL;
    }
    return rule;
}

/* ------------------------------------------------------------------ */
/* The binding                                                        */
/* ------------------------------------------------------------------ */

/* The rules the dispatch holds a handler for. */
static int kind_register(u32 kind)
{
    if (kind == 0 || kind >= BW_RULE_KINDS_MAX) {
        return -1;
    }
    if (g_kind_registered[kind]) {
        return 0;
    }
    if (bw_mod_event_register(kind, rule_handler) != 0) {
        fprintf(stderr, "eidos: the rule handler for the event %u was not "
                "bound\n", (unsigned int)kind);
        return -1;
    }
    g_kind_registered[kind] = 1;
    return 0;
}

int bw_mod_rule_bind(u32 kind, const char *path, const char *ns,
                     const char *label)
{
    const BwRuleEvent *event = event_by_kind(kind);
    BwRule *rule;

    if (event == NULL) {
        rule_report(label, NULL, 0, "the event %u is not one of the port's "
                    "events", (unsigned int)kind);
        return -1;
    }
    if (g_rule_count >= BW_RULE_MAX) {
        rule_report(label, NULL, 0, "too many rules");
        return -1;
    }
    rule = rule_compile(path, event, ns, label);
    if (rule == NULL) {
        return -1;
    }

    /* The rule's own cells: they are read once, when the rule is bound.
     * Only a rule that uses a cell touches the cells' file. */
    if (rule->uses_cells && rule->ns[0] != '\0') {
        BwRuleOwner *owner = owner_get(rule->ns);

        if (owner != NULL) {
            cells_load(owner);
        }
    }

    g_rules[g_rule_count] = rule;
    g_rule_count++;
    kind_register(kind);
    return 0;
}

int bw_mod_rule_bind_manifest(const char *root, const char *dir_name,
                              const char *event_name, const char *rel_path,
                              const char *ns, const char *label)
{
    const BwRuleEvent *event = event_by_name(event_name);
    char path[1024];
    int n;

    if (event == NULL) {
        rule_report(label, NULL, 0, "'%s' is not one of the port's events",
                    event_name != NULL ? event_name : "");
        return -1;
    }
    if (rel_path == NULL || *rel_path == '\0') {
        rule_report(label, NULL, 0, "the rule has no file");
        return -1;
    }
    if (root != NULL && *root != '\0') {
        /* The root the mod list holds is the mod's own folder (the vfs
         * splits it there: bw_add_mod_root takes the folder), so the rule's
         * path is relative to it, the way the library's is (the loader
         * joins the root and the manifest's `code`). */
        n = snprintf(path, sizeof path, "%s/%s", root, rel_path);
    } else if (dir_name != NULL && *dir_name != '\0') {
        n = snprintf(path, sizeof path, "%s/%s", dir_name, rel_path);
    } else {
        n = snprintf(path, sizeof path, "%s", rel_path);
    }
    if (n < 0 || (size_t)n >= sizeof path) {
        rule_report(label, NULL, 0, "the rule's path is too long");
        return -1;
    }
    return bw_mod_rule_bind(event->kind, path, ns, label);
}

void bw_mod_rule_reset(void)
{
    u32 i;
    u32 j;

    for (i = 0; i < g_rule_count; i++) {
        rule_release(g_rules[i]);
        g_rules[i] = NULL;
    }
    g_rule_count = 0;
    memset(g_kind_registered, 0, sizeof g_kind_registered);

    for (i = 0; i < g_owner_count; i++) {
        for (j = 0; j < g_owners[i].cell_count; j++) {
            free(g_owners[i].cells[j].keys);
            free(g_owners[i].cells[j].values);
        }
    }
    memset(g_owners, 0, sizeof g_owners);
    g_owner_count = 0;
}

void bw_mod_rule_set_send(int (*send)(const char *value))
{
    g_send = send;
}

void bw_mod_rule_set_cells_io(s32 (*load)(const char *ns, void *buf, u32 size),
                              int (*store)(const char *ns, const void *buf,
                                           u32 size))
{
    g_cells_load = load;
    g_cells_store = store;
}
