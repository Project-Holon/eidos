/*
 * sdk/mod_event_session.c -- the session area's event rows: the new game,
 * the three save events, the Hall of Fame, the badge, the event flag and
 * the Pokedex (docs/mod-events.md, section 1, "Session" and "Progress";
 * docs/mod-implementation.md, section 1, "The binding table"; the payloads
 * are docs/mod-payloads.md, section 2).
 *
 * One row per stable-event binding of this area.  A row names the game
 * function the event fires from (the region and the address of
 * mod/declared_set.txt), the event, the point, the scope, the builder that
 * fills the payload from the call, the applier that writes the modifiable
 * fields back and the refusal function that produces the refused outcome.
 * The dispatch core reads the rows from the table this file publishes
 * (bw_event_bindings_session) and registers them with
 * bw_mod_event_bind_area.
 *
 * The rules the port keeps (docs/mod-payloads.md, section 0): the port owns
 * the payload; a field is filled from the call's arguments, from an accessor
 * call at the slot or from a context; a field the call cannot supply stays
 * 0 and says so in a comment; a field that points at memory the game frees
 * later is copied into the payload, never stored as a pointer.
 *
 * The two save rows are more than payloads: they are the port's own save
 * window (docs/mod-state.md, section 2).  The game's save.begin opens the
 * window, in which the port's own save state is read and its checksum
 * taken, and the game's save.done closes it, in which the mod's own files
 * are written with the checksum of the save the game has just written.  The
 * window is therefore the game's own save and not the menu's apply, and a
 * mod's own file is written only when the game saved.
 *
 * The game's own save can be cancelled, and its SAVE_CONTROL_CancelSave has
 * no row: the port hears nothing when the player cancels a save.  What it
 * hears is the next save.begin, and a window that is still open then
 * belongs to a save that never finished, so the window is closed and the
 * payload it held is dropped, the way a failed write drops it.
 *
 * The game's own accessors are declared under the names src/game gives
 * them, the way sdk/mod_state.c does: the port's older address spellings
 * are redirected onto the same names by the build's gamespell header
 * (tools/gamespell.py), so one spelling links against either tree.
 */
#include "mod_event.h"
#include "mod_context.h"
#include "mod_slot.h"
#include "mod_state.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's structures, named only as the opaque types its accessors
 * take.  The port never looks inside them: every value comes back out
 * through another accessor call.  The tags are the game's own
 * (src/game/arm9/game_data.h and its siblings), so each prototype below
 * can be matched against the corpus by name.
 */
struct GameData;
struct GameSystem;
struct SaveControl;
struct PlayTime;
struct PlayerLocationData;
struct Party;
struct PokemonParam;
struct MiscSave;

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; the port's
 * own seam, sdk/mod_state.c, does the same.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/*
 * The save side.  SAVE_CONTROL_StartSave and SAVE_CONTROL_MainSave take
 * the save control; the play time and the player's location live in the
 * save's own blocks, so both are read through the control:
 *
 *   SYSTEM_DATA_GetPlayTime   the play time block (hours, minutes, seconds)
 *   PLAYER_LOCATION_GetData   the location block (save block 0x1C)
 *   PLAYER_LOCATION_GetZoneId  the zone id of that block
 */
extern struct PlayTime *SYSTEM_DATA_GetPlayTime(void *save)
    BW_PORT_WEAK;                                        /* 0x02008748 */
extern u16 PLAYTIME_GetHours(const struct PlayTime *playTime)
    BW_PORT_WEAK;                                        /* 0x0200864C */
extern u8 PLAYTIME_GetMinutes(const struct PlayTime *playTime)
    BW_PORT_WEAK;                                        /* 0x02008650 */
extern u8 PLAYTIME_GetSeconds(const struct PlayTime *playTime)
    BW_PORT_WEAK;                                        /* 0x02008654 */
extern struct PlayerLocationData *PLAYER_LOCATION_GetData(void *save)
    BW_PORT_WEAK;                                        /* 0x02008854 */
extern u16 PLAYER_LOCATION_GetZoneId(const struct PlayerLocationData *location)
    BW_PORT_WEAK;                                        /* 0x02008998 */

/*
 * The game data.  GAME_DATA_GetSaveControl gives the save control of the
 * game data, GAME_DATA_GetPlayTime the play time of the running game and
 * GAMESYSTEM_GetGameData the game data of the game system.
 */
extern struct SaveControl *GAME_DATA_GetSaveControl(struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012EBC */
extern struct PlayTime *GAME_DATA_GetPlayTime(struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012FCC */
extern struct GameData *GAMESYSTEM_GetGameData(const struct GameSystem *gameSystem)
    BW_PORT_WEAK;                                        /* 0x020120F4 */
extern void *GAME_DATA_GetMyParty(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012934 */

/* The party. */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    BW_PORT_WEAK;                                        /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */

/* The misc save block: the badges, the money. */
extern u32 MISC_CountBadges(struct MiscSave *miscSave)
    BW_PORT_WEAK;                                        /* 0x0200C258 */

/* A Pokemon's own fields (the id space of docs/mod-events.md, section 4). */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId, u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E1C */

/*
 * The game's own save window (sdk/mod_savedata.c): the port's own save
 * state is read when the window opens and the mod's own files are
 * written with the checksum of the save the game has just written when
 * it closes.  The window is the game's own save, not the menu's apply
 * (docs/mod-state.md, section 2).
 *
 * The game's save.begin is bw_mod_savedata_save_start and not the plain
 * save_begin: a window that is still open when a save begins belongs to a
 * save that never finished (the game cancelled it, and its own
 * SAVE_CONTROL_CancelSave has no row), so the window is closed and the
 * payload it held is dropped, the way a failed write drops it.
 *
 * Weak, so that a link without the saved-data layer still succeeds: the
 * rows then only fill their payloads.
 */
extern void bw_mod_savedata_save_begin(void) BW_PORT_WEAK;
extern void bw_mod_savedata_save_start(void) BW_PORT_WEAK;
extern int  bw_mod_savedata_save_done(int ok, u32 check) BW_PORT_WEAK;
extern u32  bw_mod_savedata_save_check(void) BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/*
 * The play time of a save, in seconds.  The game keeps hours, minutes and
 * seconds in the play time block; the payload carries one number.
 */
static u32 play_seconds_of_save(const struct SaveControl *save)
{
    const struct PlayTime *time;

    if (save == NULL) {
        return 0;
    }
    time = SYSTEM_DATA_GetPlayTime((void *)save);
    if (time == NULL) {
        return 0;
    }
    return (u32)PLAYTIME_GetHours(time) * 3600u +
           (u32)PLAYTIME_GetMinutes(time) * 60u +
           (u32)PLAYTIME_GetSeconds(time);
}

/*
 * The zone a save is made in, and the zone a loaded save resumes in: the
 * zone id of the save's own location block.  The game writes the player's
 * location into that block before it starts a save (GAME_DATA_WriteToSave
 * copies the player work into it), so at save.begin and save.done it holds
 * the zone the player is in, and at save.load the zone the save was made
 * in.
 */
static u16 save_zone(const struct SaveControl *save)
{
    const struct PlayerLocationData *location;

    if (save == NULL) {
        return 0;
    }
    location = PLAYER_LOCATION_GetData((void *)save);
    if (location == NULL) {
        return 0;
    }
    return PLAYER_LOCATION_GetZoneId(location);
}

/* The play time of the running game, in seconds. */
static u32 play_seconds_of_game(const struct GameData *gameData)
{
    const struct PlayTime *time;

    if (gameData == NULL) {
        return 0;
    }
    time = GAME_DATA_GetPlayTime((struct GameData *)gameData);
    if (time == NULL) {
        return 0;
    }
    return (u32)PLAYTIME_GetHours(time) * 3600u +
           (u32)PLAYTIME_GetMinutes(time) * 60u +
           (u32)PLAYTIME_GetSeconds(time);
}

/* ------------------------------------------------------------------ */
/* The builders                                                        */
/* ------------------------------------------------------------------ */

/*
 * game.new: the new game start event.  The payload has no fields
 * (docs/mod-payloads.md, section 2): the event is the moment itself.
 */
extern void bw_dex_ext_new_game(void) __attribute__((weak));    /* sdk/dex_save.c */
extern void bw_dex_ext_save_loaded(void) __attribute__((weak)); /* sdk/dex_save.c */
extern void bw_tm_pocket_new_game(void) __attribute__((weak));    /* sdk/tm_pocket.c */
extern void bw_tm_pocket_save_loaded(void) __attribute__((weak)); /* sdk/tm_pocket.c */

static void build_game_new(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->game_new, 0, sizeof(out->game_new));
    /* the dex extension of a new game starts empty (sdk/dex_save.c) */
    if (bw_dex_ext_new_game != NULL) {
        bw_dex_ext_new_game();
    }
    /* and the TM pocket's (sdk/tm_pocket.c) */
    if (bw_tm_pocket_new_game != NULL) {
        bw_tm_pocket_new_game();
    }
}

/*
 * save.begin: the save starts.  The play time and the zone come from the
 * save control the call carries.
 *
 * The save window opens here: the port's own save state is read and its
 * checksum taken, and the payload a mod hands over in its save.begin
 * handler is held until the game's save is done, so the mod's own file
 * and the game's save are written in the same step
 * (docs/mod-state.md, section 2).
 *
 * A window that is still open when the game's save begins belongs to a
 * save that never finished: the game cancelled it, and its own
 * SAVE_CONTROL_CancelSave has no row, so the port hears nothing when that
 * happens.  The window is closed here and the payload it held is dropped,
 * the way a failed write drops it.
 */
extern void bw_dex_ext_save(void) __attribute__((weak));       /* sdk/dex_save.c */
extern void bw_tm_pocket_save(void *bagBlock) __attribute__((weak));   /* sdk/tm_pocket.c */
extern void *SAVE_CONTROL_GetBlock(struct SaveControl *saveControl, u32 blockId) __attribute__((weak));
#define SAVE_BLOCK_MY_ITEM 0x19     /* the bag (game_data.c: GAME_DATA_GetMyItem's block) */

static void build_save_begin(const BwEventCall *call, BwEventPayload *out)
{
    const struct SaveControl *save = (const struct SaveControl *)call->a[0];

    memset(&out->save_begin, 0, sizeof(out->save_begin));
    out->save_begin.play_seconds = play_seconds_of_save(save);
    out->save_begin.zone = save_zone(save);

    if (bw_mod_savedata_save_start != NULL) {
        bw_mod_savedata_save_start();
    } else if (bw_mod_savedata_save_begin != NULL) {
        bw_mod_savedata_save_begin();
    }
    /* the dex extension (sdk/dex_save.c) is stored with the save */
    if (bw_dex_ext_save != NULL) {
        bw_dex_ext_save();
    }
    /* and the TM pocket goes back into the bag block, its overflow into
     * the mods' files (sdk/tm_pocket.c), before the game writes the save */
    if (bw_tm_pocket_save != NULL && SAVE_CONTROL_GetBlock != NULL && save != NULL) {
        bw_tm_pocket_save(SAVE_CONTROL_GetBlock((struct SaveControl *)save, SAVE_BLOCK_MY_ITEM));
    }
}

/*
 * save.done: one step of the save sequence ran.  The event fires after the
 * function, because the payload's `ok` is the result the function returned:
 * 2 is the write the flash accepted, 1 is a write still in progress, 0 is
 * the early arm that only erased the outside save and 3 is a failed write.
 */
static void build_save_done(const BwEventCall *call, BwEventPayload *out)
{
    const struct SaveControl *save = (const struct SaveControl *)call->a[0];

    memset(&out->save_done, 0, sizeof(out->save_done));
    out->save_done.play_seconds = play_seconds_of_save(save);
    out->save_done.zone = save_zone(save);
    out->save_done.ok = (call->ret == 2) ? 1u : 0u;
}

/*
 * The save window closes: this is the port's own action after the
 * handlers of the event have run, so a mod that stores its payload in its
 * save.done handler is inside the window too.
 *
 * The game's answer decides what happens (docs/mod-state.md, section 2):
 *
 *   - 2, the write the flash accepted: the game's save is in the port's
 *     own save state now, and the mod's own files are written with its
 *     checksum, so a mod's file says which game save it was written
 *     with;
 *   - 3 and up, a failed write: the payloads the window held are dropped
 *     and the old state files stay, so the two never drift apart;
 *   - 0 (the early arm that only erased the outside save) and 1 (the
 *     write is still in progress): the save goes on, and the window
 *     stays open until the game says the save is done.
 */
static void apply_save_done(const BwEventPayload *in, BwEventCall *call)
{
    s64 ret;

    (void)in;
    if (call == NULL || bw_mod_savedata_save_done == NULL) {
        return;
    }
    ret = call->ret;
    if (ret == 2) {
        (void)bw_mod_savedata_save_done(1, bw_mod_savedata_save_check());
    } else if (ret >= 3) {
        (void)bw_mod_savedata_save_done(0, 0u);
    }
}

/*
 * save.load: the save is read into the game data.  The play time and the
 * zone are the loaded save's own (the game copies them out of the save
 * blocks at the end of the function).
 *
 * `state_in_step` reads 0: whether the mod's own saved data was written
 * with this save is the mod state layer's question (docs/mod-state.md,
 * section 2: the port compares the state file's save_check with the save
 * being loaded), and the save.load payload carries one byte for every mod
 * that cannot answer it (the answer is per mod, and the payload is built
 * once for all the handlers of the event).
 */
static void build_save_load(const BwEventCall *call, BwEventPayload *out)
{
    struct GameData *gameData = (struct GameData *)call->a[0];
    const struct SaveControl *save;

    memset(&out->save_load, 0, sizeof(out->save_load));
    /* the dex extension: the loaded save's own files (sdk/dex_save.c) */
    if (bw_dex_ext_save_loaded != NULL) {
        bw_dex_ext_save_loaded();
    }
    if (bw_tm_pocket_save_loaded != NULL) {
        bw_tm_pocket_save_loaded();     /* (and the TM pocket's, sdk/tm_pocket.c) */
    }
    save = GAME_DATA_GetSaveControl(gameData);
    out->save_load.play_seconds = play_seconds_of_save(save);
    out->save_load.zone = save_zone(save);

    /*
     * The field context is reset here (docs/mod-payloads.md, section 1):
     * the zone the player was in is not the zone the loaded save resumes
     * in, and the map change that follows the load fills the context
     * again.
     */
    memset(&bw_mod_field_context, 0, sizeof(bw_mod_field_context));
}

/*
 * save.load, where a continued game starts (mod API v6, sdk/mod_event_v6.c):
 * EVENT_MAPCHANGE_CreateByMode (game system, start request) with the mode
 * "continue".  The game data is the game system's, loaded from the save.
 */
void bw_event_build_save_load_start(const BwEventCall *call, BwEventPayload *out)
{
    BwEventCall c = *call;

    c.a[0] = (uintptr_t)GAMESYSTEM_GetGameData((const struct GameSystem *)call->a[0]);
    if (c.a[0] == 0) {
        memset(&out->save_load, 0, sizeof(out->save_load));
        return;
    }
    /* The state calls a handler makes now (work_set, flag_set, item_add)
     * reach the loaded game: the game data is bound here, before the field
     * that would bind it otherwise. */
    bw_mod_state_set_game_data((void *)c.a[0]);
    build_save_load(&c, out);
}

/*
 * game.cleared: the ending.  The payload names the team the player brings
 * to the Hall of Fame: the party of the game data, and the play time the
 * Hall of Fame screen is given.
 */
static void build_game_cleared(const BwEventCall *call, BwEventPayload *out)
{
    struct GameSystem *gameSystem = (struct GameSystem *)call->a[0];
    struct GameData *gameData;
    const void *party;
    u32 count;
    u32 i;

    memset(&out->game_cleared, 0, sizeof(out->game_cleared));
    if (gameSystem == NULL) {
        return;
    }
    gameData = GAMESYSTEM_GetGameData(gameSystem);
    out->game_cleared.play_seconds = play_seconds_of_game(gameData);
    if (gameData == NULL) {
        return;
    }
    party = GAME_DATA_GetMyParty(gameData);
    if (party == NULL) {
        return;
    }
    count = POKEPARTY_GetCount((const struct Party *)party);
    if (count > 6u) {
        count = 6u;
    }
    out->game_cleared.team_count = (u8)count;
    for (i = 0; i < count; i++) {
        struct PokemonParam *mon =
            POKEPARTY_GetMember((struct Party *)party, i);

        /*
         * The handle is the game's own record, as the ABI has room for it
         * (a u32).  On a host whose pointers are eight bytes the address
         * does not fit in it; the state layer (sdk/mod_state.c) owns what
         * the handle means, and the place and the slot name the Pokemon
         * either way.
         */
        out->game_cleared.team[i].handle = (u32)(uintptr_t)mon;
        out->game_cleared.team[i].place = (u8)BW_MON_PARTY;
        out->game_cleared.team[i].side = 0u;
        out->game_cleared.team[i].box = 0u;
        out->game_cleared.team[i].slot = (u8)i;
    }
}

/*
 * badge.get: a badge was awarded.  The event fires after the function,
 * because the payload's `count` is the number of badges held once the
 * badge is set (MISC_SetBadge only ever sets a bit, so a badge that was
 * already held does not raise the count).
 */
static void build_badge_get(const BwEventCall *call, BwEventPayload *out)
{
    struct MiscSave *misc = (struct MiscSave *)call->a[0];

    memset(&out->badge_get, 0, sizeof(out->badge_get));
    out->badge_get.badge = (u8)call->a[1];
    if (misc != NULL) {
        out->badge_get.count = (u8)MISC_CountBadges(misc);
    }
}

/*
 * flag.set: an event flag was set.  The flag id is the argument; the value
 * is 1 because the bound function is the setting one: the game clears a
 * flag with ZONEDATA_ResetEventFlag, which is not a declared row
 * (mod/declared_set.txt), so the port never sees a cleared flag.
 */
static void build_flag_set(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->flag_set, 0, sizeof(out->flag_set));
    out->flag_set.flag = (u16)call->a[1];
    out->flag_set.value = 1u;
}

/*
 * dex.register: a species was registered in the Pokedex.  The species is
 * read out of the Pokemon the game hands over (the party record's own
 * species field, 0x05); whether it was caught or only seen is what the
 * bound function is.
 */
static void build_dex_register_caught(const BwEventCall *call,
                                      BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[1];

    memset(&out->dex_register, 0, sizeof(out->dex_register));
    if (mon != NULL) {
        out->dex_register.species = (u16)POKE_TOOL_GetPartyValue(mon, 0x05u, 0u);
    }
    out->dex_register.caught = 1u;
}

static void build_dex_register_seen(const BwEventCall *call,
                                    BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[1];

    memset(&out->dex_register, 0, sizeof(out->dex_register));
    if (mon != NULL) {
        out->dex_register.species = (u16)POKE_TOOL_GetPartyValue(mon, 0x05u, 0u);
    }
    out->dex_register.caught = 0u;
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

/*
 * The rows of this area.  The scope of every row is BW_SCOPE_ANY: none of
 * these events only means something while a particular caller runs
 * (docs/mod-events.md, section 1).
 *
 * None of the session events can be refused, so no row carries a refusal
 * function: save.begin has no path for a save that does not start
 * (docs/mod-events.md, section 1, "Session"), and the rest are the moments
 * themselves.
 */
static const BwEventBinding g_session_rows[] = {
    { BW_MOD_REGION_OVL(20), 0x02183C9Cu, BW_EVENT_GAME_NEW, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_game_new, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02007150u, BW_EVENT_SAVE_BEGIN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_save_begin, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02007168u, BW_EVENT_SAVE_DONE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_save_done, apply_save_done, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02012C18u, BW_EVENT_SAVE_LOAD, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_save_load, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215F8A8u, BW_EVENT_GAME_CLEARED,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_game_cleared, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0200C248u, BW_EVENT_BADGE_GET, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_badge_get, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02014314u, BW_EVENT_FLAG_SET, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_flag_set, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0200CDE0u, BW_EVENT_DEX_REGISTER, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_dex_register_caught, NULL, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0200CFA0u, BW_EVENT_DEX_REGISTER, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_dex_register_seen, NULL, NULL, NULL }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_session(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_session_rows) / sizeof(g_session_rows[0]));
    }
    return g_session_rows;
}
