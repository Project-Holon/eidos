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
    /* Version 6. */
    /* The script work variables: the saved ones (0x4000 to 0x40FF) at any
     * time with a game loaded, the script's own ones (0x8000 to 0x80FF)
     * only while a field script runs a command (a flag.set handler, a
     * script command handler).  0, or -1 for a work the port cannot reach
     * now. */
    int (*work_get)(u16 id, u16 *out);
    int (*work_set)(u16 id, u16 value);
    /* Give a badge (0 to 7) through the game's own call, so badge.get
     * fires as for a badge won in a gym.  0, or -1. */
    int (*badge_set)(u32 bit);
    /* The cartridge's 4-letter game code ("IRBO" English Black, "IRAO"
     * English White; the last letter is the language), copied to `out`
     * (up to `size` bytes, ended with a NUL when there is room).  0. */
    int (*game_code)(char *out, u32 size);
    /* Version 7. */
    /* Whether the Pokedex has its National mode: 1 or 0, a negative
     * number with no game loaded. */
    s32 (*national_dex)(void);
    /* Turn the Pokedex's National mode on, as the game's own script
     * command does.  The game has no call that turns it off.  0, or -1
     * with no game loaded. */
    int (*national_dex_set)(void);
    /* Put a Wonder Card (the 204 bytes of a .pgf file) in the Mystery
     * Gift album, once per save: the card is dated today, and the
     * delivery man in a Pokemon Centre hands its gift over, as for a card
     * received from Mystery Gift.  Returns the album slot (0 to 11).
     * -1: not a card (the size, the gift type, a card id past 2047), or no
     * game loaded.  -2: the save received a card of this id before, or the
     * album holds one: nothing changes.  -3: the album is full. */
    s32 (*mystery_card_add)(const void *pgf, u32 size);
} BwModState;

/* Version 6: the type chart, as the battle keeps it: 17 rows of 17, the
 * cell of an attacking type against a defending type at
 * cell[attacking * 17 + defending], in the game's codes 0 (no effect),
 * 2 (half), 4 (normal) and 8 (double). */
#define BW_TYPE_CHART_TYPES 17
typedef struct BwTypeChart {
    u8 cell[289];
} BwTypeChart;

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
    /* Version 6 (docs/mod-services.md). */
    /* Members in memory: a NARC member changed without a file, in the
     * mod's own layer.  `archive` is the NitroFS path ("a/0/5/7").  set:
     * 0, or -1 with a message (16 MiB per mod, 32 MiB for all; a member
     * past the end of an archive the game holds open).  clear: how many it
     * removed; index 0xFFFFFFFF all of the archive's, an empty archive
     * name all of the mod's.  get: the member as the next read sees it,
     * copied up to `size`; its whole size, or -1. */
    int  (*vfs_member_set)(const char *archive, u32 index, const void *data, u32 size);
    int  (*vfs_member_clear)(const char *archive, u32 index);
    int  (*vfs_member_get)(const char *archive, u32 index, void *out, u32 size);
    /* Text entries: archive 2 (system text, a/0/0/2) or 3 (story text,
     * a/0/0/3), the bank, the entry, the block (Japanese banks have two:
     * kana and kanji).  The units are the game's code units, control codes
     * included, with no end code.  get: the entry's length (copied up to
     * max_units), or -1.  set: 0 or -1; an entry past the end grows the
     * bank.  text_language: the game's language id (1 Japanese, 2
     * English, 3 French, 4 Italian, 5 German, 7 Spanish, 8 Korean). */
    int  (*text_get)(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
    int  (*text_set)(u32 archive, u32 file, u32 entry, u32 block, const u16 *units, u32 count);
    u32  (*text_language)(void);
    /* Start a field script by number when the field is idle (no script,
     * menu, battle or fade).  0 with *ticket set (script.done carries
     * it), or -1. */
    int  (*script_run)(u16 script_id, u32 *ticket);
    /* The type chart: written over the battle's table each time the
     * battle overlay loads (the last mod in the load order wins).  get:
     * the chart in force (a mod's, or the game's own). */
    int  (*type_chart_set)(const BwTypeChart *chart);
    int  (*type_chart_get)(BwTypeChart *out);
    /* Ask the player for a text in the game's own name entry screen:
     * `title` (UTF-8, as for message), up to max_units code units (at most
     * BW_TEXT_INPUT_MAX), what it accepts (BW_TEXT_INPUT_*).  0 with
     * *ticket set (text_input.done carries it), or -1. */
    int  (*text_input)(const char *title, u32 max_units, u32 kind, u32 *ticket);
    /* The mod's own files, shared across saves: plain names, 16 MiB for
     * all of them.  write: 0, or -1 (and the old file stays whole).  read:
     * the file's size (copied up to `size`, *got the bytes copied), or -1.
     * list: "used <bytes> quota <bytes>\n" then "<name> <size>\n" per
     * file, cut at `size`; the length the whole list needs.  delete: 0 or
     * -1. */
    int  (*mod_file_write)(const char *name, const void *data, u32 size);
    int  (*mod_file_read)(const char *name, void *out, u32 size, u32 *got);
    int  (*mod_file_list)(char *out, u32 size);
    int  (*mod_file_delete)(const char *name);
    /* metaxy world sessions (docs/mod-services.md, "World sessions").  The
     * answers come as world events; world_read gives the bytes of the one
     * being handled.  create: generate a world from a configuration (JSON).
     * owner: BW_WORLD_OWNER_* with the owner key.  join: by player name,
     * or by the player key of an earlier join.  checks: the locations
     * checked (sent again on every connect); location 0 alone says the
     * goal is reached.  0 or -1. */
    int  (*world_create)(const char *config_json, u32 *ticket);
    int  (*world_owner)(const char *code, const char *owner_key, u32 action);
    int  (*world_join)(const char *code, const char *name_or_key, u32 *ticket);
    int  (*world_checks)(const u32 *location_ids, u32 count);
    void (*world_leave)(void);
    int  (*world_read)(void *out, u32 size);
    /* The Archipelago link through metaxy: `room` is an archipelago.gg room
     * link or host:port (a bare port uses the Archipelago server setting).
     * ap_send sends one Archipelago JSON message; the answers come as ap
     * events and ap_read gives their bytes.  0 or -1. */
    int  (*ap_link)(const char *room, const char *slot, const char *password, u32 *ticket);
    int  (*ap_send)(const char *json, u32 size);
    int  (*ap_read)(void *out, u32 size);
    /* Other players of the world session: on 1 sends this game's zone and
     * tile and draws the other players in the same zone as field avatars.
     * The Town Map markers: one per id, at a zone's place on the map. */
    int  (*peer_position)(int on);
    int  (*townmap_marker_set)(u32 id, u16 zone, u32 icon, const char *name);
    int  (*townmap_marker_clear)(u32 id);
    /* The way to play the player chose for this new game (a `mode` line of
     * the mod's settings file, chosen on the Multiplayer screen): its id,
     * or "" for a plain new game.  Answers during game.new.  The id's
     * length, or -1. */
    int  (*mode_get)(char *out, u32 size);
    /* A `text` option's value (docs/mod-services.md, "Mod settings"): its
     * length, or -1 when the key names no text option of the mod. */
    int  (*setting_text)(const char *key, char *out, u32 size);
    /* The player's multiplayer name (the Multiplayer screen's name card,
     * the name the other players see): its length, 0 when the player has
     * not chosen one, or -1. */
    int  (*player_name)(char *out, u32 size);
    /* The world this new game joins: the code the player typed in the
     * Multiplayer screen's Join box, for a mode marked `joins`; "" for a
     * new world.  Answers during game.new.  Its length, or -1. */
    int  (*mode_code)(char *out, u32 size);
    /* The mod's whole Tracker as text (sdk/mod_tracker.h): areas, their
     * checks (found, open, locked), items and the count; the field's top
     * bar shows a Tracker button while a mod has one.  0, or -1 when it is
     * longer than 128 KB. */
    int  (*tracker_set)(const char *text, u32 size);
    /* Version 7 (docs/mod-services.md, "Version 7: the web, the clock, the
     * game's hash and the pop-up").  web_request: an https address; a POST
     * of `body_size` bytes with that Content-Type when there is a body or a
     * type, a GET when `body_size` is 0 and the type is "" or NULL (the
     * type of a POST with none: application/x-www-form-urlencoded).  0 with
     * *ticket set, the answer
     * comes as the event `web`; -1 when refused (not https, too long, four
     * requests already waiting).  web_read: inside the `web` handler of the
     * mod that made the request, copies up to `size` bytes of the answer
     * and answers its whole size; -1 elsewhere. */
    int  (*web_request)(const char *url, const void *body, u32 body_size, const char *content_type,
                        u32 *ticket);
    int  (*web_read)(void *out, u32 size);
    /* The clock: the seconds since 1970 (UTC), and a millisecond count
     * that only goes up. */
    s64  (*time_utc)(void);
    u64  (*ticks_ms)(void);
    /* The game in force's hash of `kind` (BW_GAME_HASH_*) as 32 hex
     * characters and a 0 (`size` at least 33): 32, or -1. */
    int  (*game_hash)(u32 kind, char *out, u32 size);
    /* A pop-up in the interface's style, over the game for a few seconds;
     * several wait their turn.  `icon` is BW_POPUP_*.  0, or -1 when
     * eight already wait. */
    int  (*popup)(const char *title, const char *text, u32 icon);
    /* Version 7, the mod's settings (docs/mod-services.md, "Mod settings"):
     * setting_show sets a `show` row's text (at most 63 bytes, no line
     * end; "" hides the row); setting_set_text sets a text or secret
     * option (the password, cleared at once after reading it).  0, or -1
     * when the mod declares no such row.  The choice of a `button` row is
     * the event setting.button. */
    int  (*setting_show)(const char *key, const char *text);
    int  (*setting_set_text)(const char *key, const char *text);
    /* RetroAchievements' hardcore: 1 when nothing in force changes the
     * game but the calling mod; 0 with the reason in `why` (a short line,
     * cut to `size`) while another mod is on or a test switch that changes
     * the game is set. */
    int  (*hardcore_allowed)(char *why, u32 size);
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