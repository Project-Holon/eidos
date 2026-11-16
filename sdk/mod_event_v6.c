/*
 * sdk/mod_event_v6.c -- the event rows mod API version 6 added
 * (docs/mod-events.md, section 1; docs/mod-payloads.md): move.forget,
 * mon.shiny, item.usable, season.change, encounter.slot,
 * battle.type_effect and starter.choices.  script.done, text_input.done,
 * world and ap are fired by the port's own services (sdk/mod_v6.c,
 * sdk/mod_world.c), not by a game function.
 *
 * Each row is bound to a function of mod/declared_set.txt.  The writable
 * field goes back as the function's answer (an AFTER row whose apply
 * changes call->ret) or as its argument (a BEFORE row whose apply changes
 * call->a[]); a value out of range is not applied and the game's own
 * answer stands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_api.h"
#include "mod_event.h"
#include "mod_slot.h"
#include "mod_state.h"
#include "mod_menu.h"
#include "mod_tracker.h"

extern int bw_mod_state_party_slot(const void *mon);
extern void *GAMESYSTEM_GetGameData(void *gameSystem) __attribute__((weak));

/* The starter screen's three species: ov223's data, by its name. */
extern void *bw_dsref_ov223_sPselIds __attribute__((weak));

/* ------------------------------------------------------------------ */
/* move.forget                                                         */
/* ------------------------------------------------------------------ */

/* FILE_UTIL_IsHmMove (move): 1 for an HM.  The forget-move screens and
 * the release and deposit checks ask it. */
static void build_move_forget(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->move_forget, 0, sizeof(out->move_forget));
    out->move_forget.move = (u16)call->a[0];
    out->move_forget.allowed = call->ret == 0 ? 1u : 0u;
}

static void apply_move_forget(const BwEventPayload *in, BwEventCall *call)
{
    call->ret = in->move_forget.allowed != 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* move.known                                                          */
/* ------------------------------------------------------------------ */

extern u32 POKE_TOOL_GetPartyValue(void *pp, u32 paramId, u32 arg) __attribute__((weak));
extern int bw_hook_enabled(const char *name) __attribute__((weak));

/* EIDOS_DEBUG_MOVE_KNOWN=<slot>: that party slot knows every move it is
 * asked about (a test switch: the answer a mod would give, before the
 * mods are asked). */
static int debug_known(int slot)
{
    const char *e = getenv("EIDOS_DEBUG_MOVE_KNOWN");

    return e != NULL && e[0] != '\0' && atoi(e) == slot;
}

/* move.known: asked by the port's copies of the game's two checks, one
 * Pokemon at a time: the field's A check (site front tile,
 * stubs/ov/patch_ov021_field_event_check.c) and the scripts' check (site
 * script, stubs/ov/patch_ov010_scrcmd_proc.c).  Both are patches, not slot
 * rows: their callers sit in their own units, where no slot is passed.
 * The mods' answer, the game's own when none.  A mod
 * holds no state between asks, so the same rule answers alike everywhere
 * (the field's check, then the script that names the Pokemon). */
int bw_mod_move_known_ask(int slot, int species, int move, int site, int known)
{
    BwEventPayload p;

    if (bw_hook_enabled != NULL && !bw_hook_enabled("move_known")) {
        return known;
    }
    if (debug_known(slot)) {
        known = 1;
    }
    memset(&p, 0, sizeof p);
    p.move_known.slot = (u8)slot;
    p.move_known.site = (u8)site;
    p.move_known.species = (u16)species;
    p.move_known.move = (u16)move;
    p.move_known.known = known ? 1u : 0u;
    (void)bw_mod_event_fire(BW_EVENT_MOVE_KNOWN, &p);
    if (getenv("EIDOS_DEBUG_MOVE_KNOWN") != NULL) {
        fprintf(stderr, "eidos: move.known: slot %d species %d move %d site %d: %s\n", slot, species, move, site,
                p.move_known.known ? "known" : "not known");
    }
    return p.move_known.known != 0;
}

/* ------------------------------------------------------------------ */
/* mon.shiny                                                           */
/* ------------------------------------------------------------------ */

#define SHINY_THRESHOLD 8u

static u32 shiny_value(u32 ot_id, u32 personality)
{
    return ((ot_id >> 16) ^ (ot_id & 0xFFFFu) ^ (personality >> 16) ^
            (personality & 0xFFFFu)) & 0xFFFFu;
}

/* POKE_TOOL_IsShinyOtIdPersonality (trainer id, personality). */
static void build_mon_shiny(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->mon_shiny, 0, sizeof(out->mon_shiny));
    out->mon_shiny.ot_id = (u32)call->a[0];
    out->mon_shiny.personality = (u32)call->a[1];
    out->mon_shiny.value = shiny_value((u32)call->a[0], (u32)call->a[1]);
    out->mon_shiny.threshold = SHINY_THRESHOLD;
}

static void apply_mon_shiny(const BwEventPayload *in, BwEventCall *call)
{
    if (in->mon_shiny.threshold == SHINY_THRESHOLD) {
        return;     /* unchanged: the game's own answer */
    }
    call->ret = in->mon_shiny.value < in->mon_shiny.threshold ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* item.usable                                                         */
/* ------------------------------------------------------------------ */

#define ITEM_GRACIDEA 466u

/* PLIST_DEMO_CanUseGracidea (party screen, Pokemon): 1 when it can. */
static void build_item_usable(const BwEventCall *call, BwEventPayload *out)
{
    int slot = bw_mod_state_party_slot((const void *)call->a[1]);

    memset(&out->item_usable, 0, sizeof(out->item_usable));
    out->item_usable.item = (u16)ITEM_GRACIDEA;
    out->item_usable.slot = slot >= 0 ? (u8)slot : 0xFFu;
    out->item_usable.usable = call->ret == 1 ? 1u : 0u;
}

static void apply_item_usable(const BwEventPayload *in, BwEventCall *call)
{
    call->ret = in->item_usable.usable != 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* season.change                                                       */
/* ------------------------------------------------------------------ */

/* COMM_PLAYER_SUPPORT_SetSeason (game data, season): before it runs. */
static void build_season_change(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->season_change, 0, sizeof(out->season_change));
    out->season_change.clock_season = (u8)call->a[1];
    out->season_change.season = (u8)call->a[1];
}

static void apply_season_change(const BwEventPayload *in, BwEventCall *call)
{
    if (in->season_change.season <= 3u) {
        call->a[1] = (uintptr_t)in->season_change.season;
    }
}

/* ------------------------------------------------------------------ */
/* encounter.slot                                                      */
/* ------------------------------------------------------------------ */

/* The roll the pick used: FIELD_ENCOUNT_RandPercent's answer, kept by a
 * port row (the pick functions take no roll; they draw it). */
static u32 g_last_roll;

static void keep_roll(const BwEventPayload *in, BwEventCall *call)
{
    (void)in;
    g_last_roll = (u32)call->ret;
}

static void build_slot(const BwEventCall *call, BwEventPayload *out, u8 method, u8 count)
{
    memset(&out->encounter_slot, 0, sizeof(out->encounter_slot));
    out->encounter_slot.method = method;
    out->encounter_slot.roll = (u8)(g_last_roll > 99u ? 99u : g_last_roll);
    out->encounter_slot.slot = (u8)call->ret;
    out->encounter_slot.slot_count = count;
}

static void build_slot_ground(const BwEventCall *call, BwEventPayload *out)
{
    build_slot(call, out, BW_ENCOUNTER_SLOT_GROUND, 12u);
}

static void build_slot_water(const BwEventCall *call, BwEventPayload *out)
{
    build_slot(call, out, BW_ENCOUNTER_SLOT_WATER, 5u);
}

static void build_slot_fishing(const BwEventCall *call, BwEventPayload *out)
{
    build_slot(call, out, BW_ENCOUNTER_SLOT_FISHING, 5u);
}

static void apply_slot(const BwEventPayload *in, BwEventCall *call)
{
    if (in->encounter_slot.slot < in->encounter_slot.slot_count) {
        call->ret = in->encounter_slot.slot;
    }
}

/* ------------------------------------------------------------------ */
/* battle.type_effect                                                  */
/* ------------------------------------------------------------------ */

/* BTL_CALC_GetTypeAffinity (attacking type, defending type): the class. */
static void build_type_effect(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->battle_type_effect, 0, sizeof(out->battle_type_effect));
    out->battle_type_effect.atk_type = (u8)call->a[0];
    out->battle_type_effect.def_type = (u8)call->a[1];
    out->battle_type_effect.effect = (u8)call->ret;
}

static void apply_type_effect(const BwEventPayload *in, BwEventCall *call)
{
    if (in->battle_type_effect.effect <= BW_TYPE_EFFECT_QUADRUPLE) {
        call->ret = in->battle_type_effect.effect;
    }
}

/* ------------------------------------------------------------------ */
/* starter.choices                                                     */
/* ------------------------------------------------------------------ */

#define SPECIES_MAX 649u

/* PSEL_Init: the screen reads its three species from sPselIds while it
 * sets up.  The overlay's data is copied in again at each load, so a
 * change lasts for this screen only. */
static u16 *starter_ids(void)
{
    if (&bw_dsref_ov223_sPselIds == NULL) {
        return NULL;
    }
    return (u16 *)bw_dsref_ov223_sPselIds;
}

static void build_starter_choices(const BwEventCall *call, BwEventPayload *out)
{
    u16 *ids = starter_ids();
    u32 i;

    (void)call;
    memset(&out->starter_choices, 0, sizeof(out->starter_choices));
    for (i = 0; ids != NULL && i < 3u; i++) {
        out->starter_choices.species[i] = ids[i];
    }
}

static void apply_starter_choices(const BwEventPayload *in, BwEventCall *call)
{
    u16 *ids = starter_ids();
    u32 i;

    (void)call;
    for (i = 0; ids != NULL && i < 3u; i++) {
        u16 s = in->starter_choices.species[i];

        if (s >= 1u && s <= SPECIES_MAX) {
            ids[i] = s;
        }
    }
}

/*
 * The event fires when the starter screen's overlay (ov223) has loaded its
 * data, before the screen sets up (bw_mod_v6_overlay_loaded).  PSEL_Init is
 * reached through the screen's process table (a stored pointer, which
 * reaches a hook but not the slot's events) and PSEL_CreateUnits, which
 * reads the species, is called only inside its own unit, so neither call
 * passes a slot.  The overlay is loaded for this screen only.
 */
void bw_event_fire_starter_choices(void)
{
    BwEventCall call;
    BwEventPayload p;

    if (starter_ids() == NULL) {
        return;
    }
    memset(&call, 0, sizeof(call));
    build_starter_choices(&call, &p);
    (void)bw_mod_event_fire(BW_EVENT_STARTER_CHOICES, &p);
    apply_starter_choices(&p, &call);
}

/* ------------------------------------------------------------------ */
/* game.new, where it reaches the game                                 */
/* ------------------------------------------------------------------ */

/*
 * game.new's own row (EVENT_MAPCHANGE_NewGameStartEvent, sdk/
 * mod_event_session.c) never fires in the game: that function is a game
 * event's step, created and called only inside its own unit
 * (event_mapchange.c), so no call passes its slot.  The game starts every
 * run through EVENT_MAPCHANGE_CreateByMode (called from ov010's
 * event_wifi_proc.c, which passes the slot), and a new game is its mode 0
 * (GAMEINIT_MODE_FIRST): game.new fires there, before the new game's
 * events are made.  Found with the arkhipelagos mod's in-game check.
 */
#define GAMEINIT_MODE_FIRST 0

static int is_first_start(const BwEventCall *call)
{
    const int *init = (const int *)call->a[1];

    return init != NULL && init[0] == GAMEINIT_MODE_FIRST;
}

/* save.load has the same trouble: GAME_DATA_LoadFromSave is called only
 * inside its own unit, while the game data is made.  A continued game is
 * CreateByMode's mode 1 (GAMEINIT_MODE_CONTINUE): save.load fires there,
 * with the save loaded and before the field. */
#define GAMEINIT_MODE_CONTINUE 1

extern void bw_event_build_save_load_start(const BwEventCall *call, BwEventPayload *out);

static int is_continue(const BwEventCall *call)
{
    const int *init = (const int *)call->a[1];

    if (init != NULL && init[0] == GAMEINIT_MODE_CONTINUE) {
        bw_mod_menu_mode_end();     /* a chosen mode is a new game's only */
        bw_mod_tracker_clear();     /* a mod sets it again for this save */
        return 1;
    }
    return 0;
}

static void build_game_new(const BwEventCall *call, BwEventPayload *out)
{
    void *gd = GAMESYSTEM_GetGameData != NULL ? GAMESYSTEM_GetGameData((void *)call->a[0]) : NULL;

    memset(&out->game_new, 0, sizeof(out->game_new));
    /* The mode the Multiplayer screen's Start chose is this new game's
     * (mode_get answers it in the handlers); a later new game has none. */
    bw_mod_menu_mode_new_game();
    bw_mod_tracker_clear();         /* the last game's tracker is not this one's */
    /* The state calls a handler makes now reach the new game's data (the
     * field binds it only later). */
    if (gd != NULL) {
        bw_mod_state_set_game_data(gd);
    }
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

static const BwEventBinding g_v6_rows[] = {
    { BW_MOD_REGION_OVL(20), 0x02183B6Cu, BW_EVENT_GAME_NEW,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_game_new, NULL, NULL, is_first_start },
    { BW_MOD_REGION_OVL(20), 0x02183B6Cu, BW_EVENT_SAVE_LOAD,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, bw_event_build_save_load_start, NULL, NULL,
      is_continue },
    { BW_MOD_REGION_ARM9, 0x020212E4u, BW_EVENT_MOVE_FORGET,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_move_forget, apply_move_forget, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02017ED8u, BW_EVENT_MON_SHINY,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_mon_shiny, apply_mon_shiny, NULL, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BDCC0u, BW_EVENT_ITEM_USABLE,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_item_usable, apply_item_usable, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0202E8CCu, BW_EVENT_SEASON_CHANGE,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_season_change, apply_season_change, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021A9DC8u, BW_EVENT_PORT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, NULL, keep_roll, NULL, NULL },
    { BW_MOD_REGION_OVL(21), 0x021A9DE0u, BW_EVENT_ENCOUNTER_SLOT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_slot_ground, apply_slot, NULL, NULL },
    { BW_MOD_REGION_OVL(21), 0x021A9E64u, BW_EVENT_ENCOUNTER_SLOT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_slot_water, apply_slot, NULL, NULL },
    { BW_MOD_REGION_OVL(21), 0x021A9E98u, BW_EVENT_ENCOUNTER_SLOT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_slot_fishing, apply_slot, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021D794Cu, BW_EVENT_BATTLE_TYPE_EFFECT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_type_effect, apply_type_effect, NULL, NULL }
};

const BwEventBinding *bw_event_bindings_v6(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_v6_rows) / sizeof(g_v6_rows[0]));
    }
    return g_v6_rows;
}
