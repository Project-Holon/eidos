/*
 * sdk/mod_state.h -- the state and service calls a mod binds to
 * (docs/mod-abi.md, section 1; docs/mod-events.md, section 4;
 * docs/mod-services.md).
 *
 * A mod reads and writes game state through accessor functions the port
 * wraps, never through the game's structs, so the game's layout can change
 * behind them.  The port services are the things that are not game
 * functions at all: text on screen, peer sessions, a timer.
 *
 * Both tables are frozen within an interface version, like the rest of
 * sdk/mod_api.h.
 */

#ifndef BW_PORT_MOD_STATE_H
#define BW_PORT_MOD_STATE_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The state calls                                                     */
/* ------------------------------------------------------------------ */

/*
 * The field ids a Pokemon's fields are read and written by.  They are the
 * ids the game's own accessors take (POKE_TOOL_GetPartyValue 0x02017E1C and
 * POKE_TOOL_SetPartyValue 0x02017E40), so a mod asks for the same values
 * the game asks for.
 */
enum {
    BW_MON_FIELD_SPECIES   = 0x05,
    BW_MON_FIELD_ITEM      = 0x06,
    BW_MON_FIELD_EXP       = 0x08,
    BW_MON_FIELD_LEVEL     = 0x9E,
    BW_MON_FIELD_HP        = 0xA0,
    BW_MON_FIELD_MAX_HP    = 0xA1,
    BW_MON_FIELD_ATK       = 0xA2,
    BW_MON_FIELD_DEF       = 0xA3,
    BW_MON_FIELD_SPEED     = 0xA4,
    BW_MON_FIELD_SPATK     = 0xA5,
    BW_MON_FIELD_SPDEF     = 0xA6,
    BW_MON_FIELD_TYPE1     = 0xAE,
    BW_MON_FIELD_TYPE2     = 0xAF,

    /* Interface version 4: more of the game's own ids by name.  The party
     * and box calls always took any id; these are the ones the rules name.
     * For a Pokemon in battle, an id the battle keeps no value for is read
     * from the party record the battle copied. */
    BW_MON_FIELD_PERSONALITY  = 0x00,
    BW_MON_FIELD_OT_ID        = 0x07,
    BW_MON_FIELD_FRIENDSHIP   = 0x09,
    BW_MON_FIELD_ABILITY      = 0x0A,
    BW_MON_FIELD_MOVE1        = 0x36,
    BW_MON_FIELD_MOVE2        = 0x37,
    BW_MON_FIELD_MOVE3        = 0x38,
    BW_MON_FIELD_MOVE4        = 0x39,
    BW_MON_FIELD_IS_EGG       = 0x4C,
    BW_MON_FIELD_GENDER       = 0x6E,
    BW_MON_FIELD_FORM         = 0x6F,
    BW_MON_FIELD_NATURE       = 0x70,
    BW_MON_FIELD_HAS_NICKNAME = 0x75,
    BW_MON_FIELD_FATEFUL      = 0x6D,
    BW_MON_FIELD_ORIGIN_GAME  = 0x77,
    BW_MON_FIELD_MET_LOCATION = 0x96,
    BW_MON_FIELD_BALL         = 0x98,
    BW_MON_FIELD_MET_LEVEL    = 0x99,

    /* Interface version 5: values only a Pokemon in battle has
     * (battle_get).  A stat stage reads 0 to 12, 6 being unchanged; the
     * status is the major condition (0 none, 1 paralysis, 2 sleep,
     * 3 freeze, 4 burn, 5 poison); the battle ability is the one in force
     * (Skill Swap, Mummy and the like change it). */
    BW_MON_FIELD_STAGE_ATK     = 0x101,
    BW_MON_FIELD_STAGE_DEF     = 0x102,
    BW_MON_FIELD_STAGE_SPATK   = 0x103,
    BW_MON_FIELD_STAGE_SPDEF   = 0x104,
    BW_MON_FIELD_STAGE_SPEED   = 0x105,
    BW_MON_FIELD_STAGE_ACCURACY = 0x106,
    BW_MON_FIELD_STAGE_EVASION = 0x107,
    BW_MON_FIELD_STATUS        = 0x108,
    BW_MON_FIELD_BATTLE_ABILITY = 0x109,

    /* The PP left of each move slot: the party record's own ids.  For a
     * Pokemon in battle, battle_get reads the battle's count (the one the
     * fight spends) and MOVE1 to MOVE4 the battle's moves (Mimic, Sketch and
     * Transform change them). */
    BW_MON_FIELD_PP1          = 0x3A,
    BW_MON_FIELD_PP2          = 0x3B,
    BW_MON_FIELD_PP3          = 0x3C,
    BW_MON_FIELD_PP4          = 0x3D
};

/*
 * move_value's own ids past the move data's fields: the stat changes a
 * move makes, all three of them (WAZA_TOOL_GetRankEffect 0x0201C0B4; the
 * data's params 16 and 17 reach only the first).  A type is the game's
 * stat id (1 attack, 2 defense, 3 special attack, 4 special defense,
 * 5 speed, 6 accuracy, 7 evasion, 8 all five stats), 0 when the move makes
 * fewer changes; a value is the signed number of stages as a u32
 * (0xFFFFFFFF is -1).
 */
enum {
    BW_MOVE_FIELD_RANK_COUNT  = 0x100,
    BW_MOVE_FIELD_RANK_TYPE1  = 0x101,
    BW_MOVE_FIELD_RANK_TYPE2  = 0x102,
    BW_MOVE_FIELD_RANK_TYPE3  = 0x103,
    BW_MOVE_FIELD_RANK_VALUE1 = 0x111,
    BW_MOVE_FIELD_RANK_VALUE2 = 0x112,
    BW_MOVE_FIELD_RANK_VALUE3 = 0x113
};

/*
 * The state table.  Every call returns 0 on success and a non-zero value on
 * failure, except the ones that answer with a value: they return the value
 * itself and a negative number for "no such thing".
 *
 * The rules a mod relies on (docs/mod-events.md, section 4):
 *
 * - The battle works on a copy.  A write to the party record has no effect
 *   during a battle and is overwritten when the battle writes its results
 *   back; a mod that changes a Pokemon in battle writes the battle record.
 * - The game's later write wins.  The port does not arbitrate a write.
 * - Overlay data is reloaded.  A save-block value persists; an overlay's own
 *   data does not.
 * - Moves wait for a quiet moment.  mon_to_box and mon_take_item change the
 *   party, which a battle, the party screen and the PC hold open; the port
 *   queues them and applies them when none of those is running.
 */
typedef struct BwModState {
    /* The party. */
    u32 (*party_count)(void);
    int (*party_ref)(u32 slot, BwMonRef *out);          /* a reference to a party slot */
    int (*party_get)(BwMonRef mon, u32 field, u32 *out);
    int (*party_set)(BwMonRef mon, u32 field, u32 value);

    /* The boxes. */
    u32 (*box_count)(void);
    int (*box_ref)(u32 box, u32 slot, BwMonRef *out);
    int (*box_get)(BwMonRef mon, u32 field, u32 *out);
    int (*box_set)(BwMonRef mon, u32 field, u32 value);

    /* A Pokemon in battle. */
    int (*battle_ref)(u32 side, u32 slot, BwMonRef *out);
    int (*battle_get)(BwMonRef mon, u32 field, u32 *out);
    int (*battle_set)(BwMonRef mon, u32 field, u32 value);

    /* Species data: the personal parameters of a species. */
    int (*species_value)(u16 species, u32 field, u32 *out);
    int (*species_shiny)(u32 personality, u16 species);

    /* The dex. */
    int (*dex_caught)(u16 species);

    /* Badges, money and flags. */
    u32 (*badge_count)(void);
    int (*badge_has)(u32 badge);
    u32 (*money)(void);
    int  (*money_set)(u32 amount);
    int  (*flag)(u32 flag, u32 *out);
    int  (*flag_set)(u32 flag, u32 value);

    /* The map and the player. */
    u16 (*zone)(void);
    int (*player_pos)(s16 *x, s16 *y, s16 *z);
    u8  (*battle_style)(void);

    /* Moves between places. */
    int (*mon_to_box)(BwMonRef mon);
    int (*mon_take_item)(BwMonRef mon);

    /* Interface version 4. */
    /* Release a Pokemon of the player's party for good: a party reference,
     * or a reference to one of the player's Pokemon in battle.  Queued like
     * the moves above and applied at the next quiet moment (the first step
     * on the field after a battle).  The Pokemon is found by its
     * personality then, not by its slot.  Refused when it would leave the
     * party with no Pokemon that is not an egg. */
    int (*mon_release)(BwMonRef mon);

    /* The bag.  item_count answers how many of an item the bag holds (a
     * negative number when there is no bag yet).  item_add puts `count`
     * of an item in the bag at once and answers 0, or non-zero when the
     * item is past the item limit, the count is 0 or the pocket is full;
     * a refused add puts nothing in. */
    s32 (*item_count)(u16 item);
    int (*item_add)(u16 item, u16 count);

    /* The area a zone belongs to: the place name the game shows on entry
     * (ZONEDATA_GetPlaceNameId).  Every floor of a cave and every building
     * of a town share their place's area; a season does not change a
     * zone.  A negative number when the zone data is not loaded. */
    s32 (*area_of)(u16 zone);

    /* The battle in progress: 1 wild, 0 a trainer, -1 no battle. */
    s32 (*battle_wild)(void);

    /* The play time of the save in force, in seconds. */
    u32 (*play_seconds)(void);

    /* An item's or a move's own data, by the game's field ids
     * (FILE_UTIL_GetItemParam, WAZA_TOOL_GetParam; docs/mod-state.md).
     * 0, or non-zero past the item or move limit. */
    int (*item_value)(u16 item, u32 field, u32 *out);
    int (*move_value)(u16 move, u32 field, u32 *out);

    /* Moves between the party and a named box, queued like mon_to_box
     * (applied at the next field step, found by personality then).
     * mon_to_box_n puts a party Pokemon in box `box` (0 is the first) and
     * fails when that box is full; mon_from_box puts the Pokemon at
     * (box, slot) in the party and fails when the party is full or the
     * slot is empty.  Neither leaves the party without a Pokemon that is
     * not an egg. */
    int (*mon_to_box_n)(BwMonRef mon, u32 box);
    int (*mon_from_box)(u32 box, u32 slot);

    /* Work out a party Pokemon's level and stats again from its species,
     * experience, IVs and EVs, the way the game does after a level up.
     * Call it after party_set changes any of those.  Not for a box or a
     * battle reference. */
    int (*mon_recalc)(BwMonRef mon);

    /* The first stage of a species' evolution line: the species no other
     * species evolves into, found by walking the evolution data backwards
     * (a/0/1/9).  Two species are in the same line when their bases are
     * equal.  A negative number for a species past the data. */
    s32 (*species_base)(u16 species);

    /* The opposing trainers of the battle in progress: n 0 the first, 1
     * the second of a multi battle.  0 in a wild battle or for no second
     * trainer, a negative number when no battle runs. */
    s32 (*battle_trainer)(u32 n);

    /* Version 5, for an AI and other battle mods. */

    /* How well an attacking type hits a Pokemon of one or two types: the
     * game's own type chart (BTL_CALC_GetTypeAffinity), in quarters: 0 no
     * effect, 1 a quarter, 2 half, 4 normal, 8 double, 16 four times.
     * def_type2 equal to def_type1 (or 255) for a single type.  A negative
     * number for a type past the chart, or when no battle runs (the
     * chart is the battle's own data). */
    s32 (*type_effect)(u32 atk_type, u32 def_type1, u32 def_type2);

    /* The weather of the battle in progress (the game's own id, 0 none),
     * a negative number when no battle runs. */
    s32 (*battle_weather)(void);

    /* How a species can learn a move, as the game's own data says:
     * bit 0 its level-up moves up to `level` (0 for every level), bit 1 a
     * TM or HM it can use, bit 2 an egg move.  0 when it cannot; a negative
     * number for a species or move past the data. */
    s32 (*species_learns)(u16 species, u16 form, u16 move, u8 level);

    /* The player whites out as after a lost battle: the game's own blackout
     * screen, the arrival at the Pokemon Centre they last healed at and the
     * nurse's script, at the next field moment with no event running.  The
     * party is not touched first: a mod that wants it fainted sets the HP.
     * 0 when queued; a non-zero number with no field. */
    int (*whiteout)(void);
} BwModState;

/* ------------------------------------------------------------------ */
/* The services                                                        */
/* ------------------------------------------------------------------ */

/* What a session handler is told about. */
enum {
    BW_SESSION_MESSAGE = 0,   /* a message from the peer */
    BW_SESSION_JOIN    = 1,   /* a peer joined */
    BW_SESSION_LEAVE   = 2    /* a peer left */
};

/* A message from a peer (the payload is the peer's own bytes). */
typedef void (*BwSessionHandler)(u32 peer, int kind, const void *buf, u32 size);

/* A text command from an external tool. */
typedef void (*BwCommandHandler)(const char *line);

/* Once per game frame, after the game's own frame work. */
typedef void (*BwFrameHandler)(void);

/* A save sent by a peer. */
typedef void (*BwSaveHandler)(u32 peer, const void *buf, u32 size);

/* The flags of a run restart. */
enum {
    BW_RESTART_NEW_GAME    = 0x1u,
    BW_RESTART_DELETE_SAVE = 0x2u
};

/*
 * The port's own state file header version.  It is bumped when the header's
 * layout changes; a file with an older version is read with that version's
 * layout and written in this one at the next store (docs/mod-state.md,
 * section 5).
 */
#define BW_MOD_STATE_VERSION 1u

/*
 * The state file header's size on the disk, and the largest payload a mod
 * may store.  A bigger store is refused with a report.
 */
#define BW_MOD_STATE_HEADER_SIZE 24u
#define BW_MOD_SAVEDATA_MAX_PAYLOAD (4u * 1024u * 1024u)

/*
 * The header of the port's own state file (docs/mod-state.md, section 1).
 * The port owns it and bumps `version` when the layout changes; the payload
 * after it belongs to the mod.
 */
typedef struct BwModStateHeader {
    u32 magic;         /* BWMS */
    u16 version;       /* the port's header version */
    u16 flags;         /* zero */
    u32 payload_size;  /* the bytes after the header */
    u32 payload_crc;   /* the payload's checksum */
    u32 api_written;   /* the manifest api in force */
    u32 save_check;    /* a checksum of the game save written in the same step */
} BwModStateHeader;

#define BW_MOD_STATE_MAGIC 0x534D5742u   /* "BWMS" little-endian */

/*
 * The services table.  The calls the port has not built yet are NULL, and a
 * mod that calls one gets nothing rather than a crash.
 */
typedef struct BwModServices {
    /* Messages. */
    void (*message)(const char *text);
    int  (*confirm)(const char *text);

    /* Peer sessions. */
    int  (*session_host)(char *code_out, u32 size);
    int  (*session_join)(const char *code);
    void (*session_leave)(void);
    int  (*session_send)(u32 peer, const void *buf, u32 size);
    void (*on_session)(BwSessionHandler fn);

    /* Field avatars. */
    u32  (*avatar_create)(u32 sprite, s16 x, s16 z, u8 dir);
    void (*avatar_move)(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);
    void (*avatar_delete)(u32 avatar);

    /* Pokemon export and import. */
    int (*mon_export)(BwMonRef mon, void *buf, u32 size);
    int (*mon_import)(const void *buf, u32 size, BwMonRef *out);

    /* The run-status panel. */
    void (*panel_set)(u32 line, const char *text);
    void (*panel_clear)(void);

    /* The timer and the splits. */
    void (*timer_start)(void);
    void (*timer_stop)(void);
    u64  (*timer_ms)(void);
    void (*split)(const char *name);

    /* A run restart. */
    void (*run_restart)(u32 flags);

    /* The mod's own random source. */
    void (*rand_seed)(u64 seed);
    u32  (*rand_next)(void);

    /* External control. */
    void (*on_command)(BwCommandHandler fn);
    void (*publish)(const char *topic, const char *json);

    /* Input injection. */
    void (*input_press)(u32 buttons, u32 frames);

    /* The frame tick. */
    void (*on_frame)(BwFrameHandler fn);

    /* The save handoff. */
    int  (*save_send)(u32 peer);
    void (*on_save_received)(BwSaveHandler fn);

    /* The mod's own settings. */
    int  (*setting_get)(const char *key);

    /* The mod's own saved data (docs/mod-state.md, section 2).  The count
     * is s32 and not long: long is four bytes on Windows and eight on a
     * 64-bit Unix, and the ABI is fixed-width. */
    s32  (*state_load)(void *buf, u32 size);    /* bytes read, -1 on damage */
    int  (*state_store)(const void *buf, u32 size);
    int  (*state_clear)(void);
    int  (*state_info)(BwModStateHeader *out);
    int  (*state_dir)(char *out, u32 out_size);

    /* Whether the mod's own saved data was written with the game save in
     * force (docs/mod-state.md, section 2, "Loaded").  The save.load
     * payload's byte cannot answer it: the payload is built once for every
     * handler of the event and the answer is per mod, so one mod's file
     * may be in step while another's is not.  This call answers for the
     * mod that calls it.
     *
     * 1 when the mod's own file was written with the save in force, 0 when
     * it was not (the game save was written without the mod), and -1 when
     * the question cannot be answered (no state file, or no save in
     * force). */
    int  (*state_in_step)(void);

    /* Interface version 4. */
    /* The mod's own data tables (manifest `table = <name> <path>`,
     * docs/mod-tables.md).  0 when the key is in the table, 1 when it is
     * not and *out holds the table's default, -1 when the mod has no such
     * table. */
    int  (*table_get)(const char *name, s32 key, s32 *out);

    /* Interface version 5: multiplayer (docs/multiplayer.md).  A mod with
     * `multiplayer = <n>` in its manifest receives the other players'
     * events too, through its own handlers; event_player says whose game
     * fired the event the handler runs for: 0 for this game, else the
     * player's number in the session.  Another player's event cannot be
     * refused or changed: its game has already acted on it. */
    u32  (*event_player)(void);
    /* The shared table: numbers every player sees, kept by the host (its
     * copy is the real one; the others send their changes to it, and it
     * sends every change to everyone).  Keys are the mod's own.  get: 0
     * when the key is set, 1 when it is not (*out is 0), -1 outside a
     * multiplayer game.  set, add and cas: 0 when the change was made
     * (the host) or sent to the host (a joiner), -1 outside a multiplayer
     * game or for a key that cannot be sent.  The host makes add and cas
     * itself, in the order the changes come, so two players' changes at
     * the same moment are both kept: add adds `delta` to the value (an
     * unset key is 0), cas sets `value` only when the value is `expected`
     * then.  Every change reaches every player; a joiner reads its own
     * once the host has sent it back. */
    int  (*shared_get)(const char *key, s32 *out);
    int  (*shared_set)(const char *key, s32 value);
    int  (*shared_add)(const char *key, s32 delta);
    int  (*shared_cas)(const char *key, s32 expected, s32 value);
} BwModServices;

/* ------------------------------------------------------------------ */
/* The port's own side of the state calls                              */
/* ------------------------------------------------------------------ */

/*
 * The game's live objects cannot be named from outside the game: the game
 * data lives in a heap only GAMESYSTEM_GetGameData reaches, and the
 * battle's main object and the field player are the game's own.  The port
 * binds them where it learns them (a save load, a battle, the field map),
 * and the state calls answer from them.
 *
 * Until an object is bound, the calls that need it report a failure; they
 * never read through an unbound pointer.
 *
 * These are the port's own calls, not part of the mod ABI: a mod reaches
 * the state through the BwModState table above.
 */
void bw_mod_state_set_game_data(void *game_data);
void bw_mod_state_set_battle(void *battle_main);
void bw_mod_state_set_field_player(void *field_player);

/* Whether the battle that is starting is a wild one (1) or a trainer's
 * (0); the battle's end sets -1 (bw_mod_state_set_battle(NULL)). */
void bw_mod_state_set_battle_wild(s32 wild);

/* Whether the battle that is starting came from encounter.static (1) or a
 * wild roll (0); read back by catch.attempt.  The battle's end clears it. */
void bw_mod_state_set_battle_static(int is_static);

/* The opposing trainers' ids, set when the battle starts. */
void bw_mod_state_set_battle_trainers(u16 first, u16 second);
int  bw_mod_state_battle_static(void);

/* The table the host table hands to a mod. */
const BwModState *bw_mod_state_table(void);

/* Once a frame (sdk/mod_frame.c): starts a queued whiteout once
 * the field runs with no event. */
void bw_mod_state_frame(void);

/* The party position of a game Pokemon record, or -1 when it is not in the
 * player's party (or no game data is bound).  For the event builders. */
int bw_mod_state_party_slot(const void *mon);

/* Drop the bound objects and the queued moves: at power-on and at every
 * reset (bw_port_reset). */
void bw_mod_state_reset(void);

/* The moves the state calls queued (mon_to_box, mon_take_item).  They are
 * applied when no battle, party screen or PC holds the party open, in the
 * order the mod made them; the caller knows when that is. */
void   bw_mod_state_apply_pending(void);
void   bw_mod_state_drop_moves(void);
u32    bw_mod_state_pending(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_STATE_H */