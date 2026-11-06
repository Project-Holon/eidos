/*
 * sdk/mod_event_field.c -- the world area's event rows: the route change,
 * the map warp, the player's step, the trainer that sees the player, the
 * talk to a person, the field menu, the field move and the PC; and the
 * encounters' rows: the wild encounter, the roamer, the static encounter
 * and the trainer battle (docs/mod-events.md, section 1, "World" and
 * "Encounters"; docs/mod-implementation.md, section 1, "The binding table";
 * the payloads are docs/mod-payloads.md, section 2).
 *
 * One row per stable-event binding of this area.  A row names the game
 * function the event fires from (the region and the address of
 * mod/declared_set.txt), the event, the point, the scope, the builder that
 * fills the payload from the call, the applier that writes the modifiable
 * fields back and the refusal function that produces the refused outcome.
 * The dispatch core reads the rows from the table this file publishes
 * (bw_event_bindings_field) and registers them with bw_mod_event_bind_area.
 *
 * The rules the port keeps (docs/mod-payloads.md, section 0): the port owns
 * the payload; a field is filled from the call's arguments, from an accessor
 * call at the slot or from a context; a field the call cannot supply stays
 * 0 and says so in a comment; a field that points at memory the game frees
 * later is copied into the payload, never stored as a pointer.
 *
 * The game's own accessors are declared under the names src/game gives
 * them, the way sdk/mod_state.c does: the port's older address spellings
 * are redirected onto the same names by the build's gamespell header
 * (tools/gamespell.py), so one spelling links against either tree.  Every
 * one of them is weak, and the builders check the name before they call
 * it: a link without the game (the tests) has no such function, and the
 * call finds the symbol missing and reports nothing instead of calling
 * into nothing.
 */

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   EVENT_TRAINER_EYE_FindTrainer        0x021AE1FC  thumb  ov021
 *   FIELDMAP_GetZoneId                   0x02188C9C  thumb  ov021
 *   FIELD_ENCOUNT_CreateZoneWildMons     0x021A954C  thumb  ov021
 *   FIELD_PLAYER_GetFormCode             0x021A3098  thumb  ov021
 *   FIELD_PLAYER_GetPos                  0x021A3038  thumb  ov021
 *   FIELD_PLAYER_GetRideEffect           0x021A2F98  thumb  ov021
 *   SCRIPT_WORK_ReadWorkPointer          0x02159AE8  thumb  ov010
 */
#include "mod_event.h"
#include "mod_avatar.h"
#include "mod_context.h"
#include "mod_slot.h"
#include "mod_state.h"
#include "mp.h"

#include <stddef.h>
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
struct PlayerWork;
struct Party;
struct PokemonParam;
struct FieldMap;
struct FieldPlayer;
struct MovePokemonEntry;
struct BattleSetupParam;
struct ScriptVm;
struct ScrcmdWork;

/*
 * Two of the port's own names for the game's records, here because the
 * accessors below take them.  Both are copied from the game's own
 * headers; the views section below has the rest.
 */

/*
 * The vector the field player's position comes in.  The game's own is
 * VecFx32 (src/game/ov021/field_player.c): three fx32, which the port's
 * types.h defines as 32-bit fixed point with twelve fractional bits.
 */
struct BwVecFx32 {
    fx32 x;
    fx32 y;
    fx32 z;
};

/*
 * A trainer that sees the player, copied from the game's own header
 * (aletheia src/game/ov021/event_trainer_eye_types.h).  The game fills the
 * record in its own search and hands it to the script work, and it
 * publishes no accessor for the trainer id or the distance, so the port
 * reads them through a view of the record.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
struct BwPortTrainerEyeHit {
    void *trainer;      /* 0x00: the field model of the trainer */
    int   distance;     /* 0x04: the tiles between the trainer and the player */
    u32   direction;    /* 0x08: the direction the trainer sees along */
    u32   eventId;      /* 0x0C: the object event id of the trainer */
    u32   trainerId;    /* 0x10 */
    u32   hitKind;      /* 0x14 */
};

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

/* The game data and the player. */
extern u8 GAME_DATA_GetSeason(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012984 */
extern struct PlayerWork *GAME_DATA_GetMyPlayerWork(struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x020127A4 */
extern u16 GAME_DATA_GetPlayerZoneId(const struct PlayerWork *player)
    BW_PORT_WEAK;                                        /* 0x02012AC8 */
extern struct GameData *GAMESYSTEM_GetGameData(const struct GameSystem *gameSystem)
    BW_PORT_WEAK;                                        /* 0x020120F4 */

/* The party and a Pokemon's own fields. */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId,
                                   u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E1C */
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *mon, u32 paramId,
                                    u32 value)
    BW_PORT_WEAK;                                        /* 0x02017E40 */

/* The field map and the player on it. */
extern u16 FIELDMAP_GetZoneId(struct FieldMap *fieldMap)
    BW_PORT_WEAK;                                        /* 0x02188C9C */
extern void FIELD_PLAYER_GetPos(struct FieldPlayer *player,
                                struct BwVecFx32 *out)
    BW_PORT_WEAK;                                        /* 0x021A3038 */
extern u32 FIELD_PLAYER_GetFormCode(struct FieldPlayer *player)
    BW_PORT_WEAK;                                        /* 0x021A3098 */
extern u32 FIELD_PLAYER_GetRideEffect(struct FieldPlayer *player)
    BW_PORT_WEAK;                                        /* 0x021A2F98 */

/* The trainer that sees the player. */
extern int EVENT_TRAINER_EYE_FindTrainer(struct FieldMap *fieldMap,
                                          void *exclude,
                                          struct BwPortTrainerEyeHit *out)
    BW_PORT_WEAK;                                        /* 0x021AE1FC */

/* The field menu. */
extern void CLACT_SetActorDrawEnable(void *actor, int enable)
    BW_PORT_WEAK;                                        /* 0x0204B3DC */
extern void *BMP_WIN_GetBmp(void *bmpWin) BW_PORT_WEAK;   /* 0x02045770 */
extern void BMP_Clear(void *bmp, unsigned int color) BW_PORT_WEAK; /* 0x02043B5C */
extern void BMP_WIN_TransferChars(void *bmpWin) BW_PORT_WEAK;      /* 0x02045334 */

/* The script command that opens the PC. */
extern u16 *SCRIPT_WORK_ReadWorkPointer(struct ScriptVm *vm,
                                         struct ScrcmdWork *cmdWork)
    BW_PORT_WEAK;                                        /* 0x02159AE8 */

/* The battle setup. */
extern void *BTL_SETUP_GetParty(struct BattleSetupParam *param, int client)
    BW_PORT_WEAK;                                        /* 0x0201313C */

/* The roaming Pokemon, in the save's own block. */
extern u32 MOVE_POKEMON_SAVE_GetEntryField(const struct MovePokemonEntry *entry,
                                            u32 field)
    BW_PORT_WEAK;                                        /* 0x0200D4B8 */
extern void MOVE_POKEMON_SAVE_SetEntryField(struct MovePokemonEntry *entry,
                                            u32 field, u32 value)
    BW_PORT_WEAK;                                        /* 0x0200D50C */

/* ------------------------------------------------------------------ */
/* The port's own views of the game's records                         */
/* ------------------------------------------------------------------ */

/*
 * The port's own check, in the shape sdk/os_thread.c uses: a condition that
 * must hold at every pointer width the port is built for.  The name is the
 * port's own, so a header that later grows one does not clash with it.
 */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/*
 * The vector the field player's position comes in: the game's own is
 * VecFx32, and the port's copy is above, with the accessors that take it.
 */

/*
 * The field input the step and talk checks take, copied member for member
 * from the game's own header (aletheia src/game/ov021/field_event_check.c).
 * The game builds the record on the stack once per frame and hands it to
 * every check of the file, and it publishes no accessor for the player it
 * holds, so the port's only way to read the player is a view of the
 * record.
 *
 * The copy is what makes the read correct at either pointer width: the
 * game's ov021 code is compiled by the same host compiler as this file, so
 * the same member types in the same order give the same offsets on both.
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortFieldEventInput {
    u16   heapId;         /* 0x00: the field heap of the map */
    u16   heapIdPad;      /* 0x02 */
    void *gameSystem;     /* 0x04 */
    void *gameData;       /* 0x08 */
    void *eventData;      /* 0x0C: the zone's event data */
    u16   zoneId;         /* 0x10 */
    u16   zoneIdPad;      /* 0x12 */
    void *fieldMap;       /* 0x14 */
    void *player;         /* 0x18: the field player */
    u32   moveState;      /* 0x1C: 3 = the step finished */
    u32   moveMode;       /* 0x20: 1 walk, 2 other */
    u16   facingDir;      /* 0x24: the direction of the player model */
    u16   pendingScriptId;/* 0x26 */
    u32   keyTrg;         /* 0x28: the pad trigger */
    u32   keyCont;        /* 0x2C: the pad held */
    u16   heldDir;        /* 0x30 */
    u16   heldDirPad;     /* 0x32 */
    void *playerPos;      /* 0x34: the position of the player model */
    u32   tileAttr;       /* 0x38 */
    u32   flags;          /* 0x3C: the press and the step bits */
} BwPortFieldEventInput;

BW_PORT_STATIC_ASSERT(input_player, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, player) == 0x18u);
BW_PORT_STATIC_ASSERT(input_move_state, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, moveState) == 0x1Cu);
BW_PORT_STATIC_ASSERT(input_move_mode, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, moveMode) == 0x20u);
BW_PORT_STATIC_ASSERT(input_facing, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, facingDir) == 0x24u);
BW_PORT_STATIC_ASSERT(input_key_trg, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, keyTrg) == 0x28u);
BW_PORT_STATIC_ASSERT(input_key_cont, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, keyCont) == 0x2Cu);
BW_PORT_STATIC_ASSERT(input_held_dir, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, heldDir) == 0x30u);
BW_PORT_STATIC_ASSERT(input_tile_attr, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, tileAttr) == 0x38u);
BW_PORT_STATIC_ASSERT(input_flags, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldEventInput, flags) == 0x3Cu);
BW_PORT_STATIC_ASSERT(input_size, sizeof(BwPortFieldEventInput) ==
                      (sizeof(void *) == 4 ? 0x40u : 0x060u));

/*
 * A place on a map, copied from the game's own header (aletheia
 * src/game/ov020/event_mapchange_types.h).  The rail half of the position
 * union is a byte block here: the game's own RailLocation holds no
 * pointer, so the union is twelve bytes and four-byte aligned on every
 * host, and the view keeps the same size at either pointer width.
 */
typedef struct BwPortLocation {
    u32   type;           /* 0x00: 1 = a direct position, 2 = a rail one */
    u16   zoneId;         /* 0x04 */
    s16   exitId;         /* 0x06 */
    s16   dir;            /* 0x08 */
    u16   exitOffset;     /* 0x0A */
    u32   posType;        /* 0x0C */
    union {
        struct BwVecFx32 vec;
        u8              rail[8];
    } pos;                /* 0x10 */
} BwPortLocation;

/*
 * The work of one map change, copied from the same header.  The view stops
 * at the destination: the port reads the zone the change leaves and the
 * destination it applies, and nothing else of the record.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortMapChangeWork {
    void            *gameSystem;    /* 0x00 */
    void            *gameData;      /* 0x04 */
    void            *fieldMap;      /* 0x08 */
    u16              currentZoneId; /* 0x0C: the zone the player leaves */
    u8               pad0E[2];      /* 0x0E */
    BwPortLocation   destination;   /* 0x10 */
} BwPortMapChangeWork;

/* The work of the change field event: it points back at the map change. */
typedef struct BwPortChangeFieldWork {
    void *mapChange;
} BwPortChangeFieldWork;

BW_PORT_STATIC_ASSERT(change_destination, sizeof(void *) != 4 ||
                      offsetof(BwPortMapChangeWork, destination) == 0x10u);
BW_PORT_STATIC_ASSERT(change_current_zone, sizeof(void *) != 4 ||
                      offsetof(BwPortMapChangeWork, currentZoneId) == 0x00Cu);
BW_PORT_STATIC_ASSERT(change_size, sizeof(BwPortMapChangeWork) ==
                      (sizeof(void *) == 4 ? 0x2Cu : 0x038u));
BW_PORT_STATIC_ASSERT(change_field_map_change, sizeof(void *) != 4 ||
                      offsetof(BwPortChangeFieldWork, mapChange) == 0x000u);
BW_PORT_STATIC_ASSERT(change_field_work_size, sizeof(BwPortChangeFieldWork) ==
                      (sizeof(void *) == 4 ? 0x004u : 0x008u));
/*
 * The location holds no pointer, so its layout is the game's at every
 * pointer width and these hold at every width.
 */
BW_PORT_STATIC_ASSERT(location_size, sizeof(BwPortLocation) == 0x1Cu);
BW_PORT_STATIC_ASSERT(location_type, offsetof(BwPortLocation, type) == 0x000u);
BW_PORT_STATIC_ASSERT(location_zone_id, offsetof(BwPortLocation, zoneId) == 0x004u);
BW_PORT_STATIC_ASSERT(location_exit_id, offsetof(BwPortLocation, exitId) == 0x006u);
BW_PORT_STATIC_ASSERT(location_dir, offsetof(BwPortLocation, dir) == 0x008u);
BW_PORT_STATIC_ASSERT(location_exit_offset,
                      offsetof(BwPortLocation, exitOffset) == 0x00Au);
BW_PORT_STATIC_ASSERT(location_pos_type,
                      offsetof(BwPortLocation, posType) == 0x00Cu);
BW_PORT_STATIC_ASSERT(location_pos, offsetof(BwPortLocation, pos) == 0x10u);

/*
 * A trainer that sees the player: the game's own header, copied above
 * with the accessors that take it.  The offsets the game's own header
 * documents are the DS ones, so they are checked below only where the
 * pointers are four bytes wide.
 */
BW_PORT_STATIC_ASSERT(hit_trainer, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, trainer) == 0x00u);
BW_PORT_STATIC_ASSERT(hit_distance, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, distance) == 0x04u);
BW_PORT_STATIC_ASSERT(hit_direction, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, direction) == 0x08u);
BW_PORT_STATIC_ASSERT(hit_event_id, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, eventId) == 0x0Cu);
BW_PORT_STATIC_ASSERT(hit_trainer_id, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, trainerId) == 0x10u);
BW_PORT_STATIC_ASSERT(hit_kind, sizeof(void *) != 4 ||
                      offsetof(struct BwPortTrainerEyeHit, hitKind) == 0x14u);
BW_PORT_STATIC_ASSERT(hit_size, sizeof(struct BwPortTrainerEyeHit) ==
                      (sizeof(void *) == 4 ? 0x18u : 0x20u));

/*
 * One slot of the field menu, copied from the game's own header (aletheia
 * src/game/ov021/field_menu.c).  The slot holds the button it shows and
 * the actor that draws the icon; the port reads the button to name the
 * entries the menu shows.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortFieldMenuItem {
    int   inUse;         /* 0x00 */
    void *bmpWin;        /* 0x04 */
    int   buttonId;      /* 0x08 */
    u32   charRes;       /* 0x0C: the character resource id, not a pointer */
    u32   cellRes;       /* 0x10: the cell resource id, not a pointer */
    u8    cursorX;       /* 0x14 */
    u8    cursorY;       /* 0x15 */
    u8    reserved16[2]; /* 0x16 */
    void *actor;         /* 0x18 */
} BwPortFieldMenuItem;

/* The menu's own work (0x138 bytes in the game); the view stops at the
 * slots, which are the last of it the port reads.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide. */
typedef struct BwPortFieldMenu {
    u16                heapId;         /* 0x00 */
    u16                subHeapId;      /* 0x02 */
    void              *fieldMap;       /* 0x04 */
    void              *parent;         /* 0x08 */
    int                state;          /* 0x0C */
    u32                mapInfo;        /* 0x10 */
    void              *eventWork;      /* 0x14 */
    void              *myStatus;       /* 0x18 */
    u16                zoneId;         /* 0x1C */
    u8                 cursorCol;      /* 0x1E */
    u8                 cursorRow;      /* 0x1F */
    u8                 pressFrames;    /* 0x20 */
    u8                 layoutType;     /* 0x21 */
    u8                 reserved22[0x24 - 0x22];
    int                cursorDirty;    /* 0x24 */
    int                exitRequested;  /* 0x28 */
    int                keyCursorActive;/* 0x2C */
    int                chosenButton;   /* 0x30 */
    BwPortFieldMenuItem items[7];       /* 0x34 */
} BwPortFieldMenu;

BW_PORT_STATIC_ASSERT(menu_items, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenu, items) == 0x34u);
BW_PORT_STATIC_ASSERT(menu_zone_id, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenu, zoneId) == 0x1Cu);
BW_PORT_STATIC_ASSERT(menu_cursor_dirty, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenu, cursorDirty) == 0x24u);
BW_PORT_STATIC_ASSERT(menu_chosen_button, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenu, chosenButton) == 0x30u);
BW_PORT_STATIC_ASSERT(menu_size, sizeof(BwPortFieldMenu) ==
                      (sizeof(void *) == 4 ? 0x0F8u : 0x160u));
BW_PORT_STATIC_ASSERT(menu_item_in_use, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, inUse) == 0x00u);
BW_PORT_STATIC_ASSERT(menu_item_bmp_win, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, bmpWin) == 0x04u);
BW_PORT_STATIC_ASSERT(menu_item_button, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, buttonId) == 0x08u);
BW_PORT_STATIC_ASSERT(menu_item_char_res, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, charRes) == 0x0Cu);
BW_PORT_STATIC_ASSERT(menu_item_cell_res, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, cellRes) == 0x10u);
BW_PORT_STATIC_ASSERT(menu_item_cursor_x, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, cursorX) == 0x14u);
BW_PORT_STATIC_ASSERT(menu_item_cursor_y, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, cursorY) == 0x15u);
BW_PORT_STATIC_ASSERT(menu_item_actor, sizeof(void *) != 4 ||
                      offsetof(BwPortFieldMenuItem, actor) == 0x18u);
BW_PORT_STATIC_ASSERT(menu_item_size, sizeof(BwPortFieldMenuItem) ==
                      (sizeof(void *) == 4 ? 0x1Cu : 0x028u));

/*
 * One slot of a wild encounter table, copied from the game's own header
 * (aletheia src/game/ov021/field_encount_types.h): the species, the form,
 * and the level range the slot rolls in.
 *
 * The table holds no pointer, so its layout is the game's at every pointer
 * width and these hold at every width.
 */
typedef struct BwPortEncountSlot {
    u16 species : 11;
    u16 form : 5;
    u8  minLevel;
    u8  maxLevel;
} BwPortEncountSlot;

/*
 * The wild encounter table of one zone, copied from the same header.  The
 * ground tables hold the types 0 to 2, the water tables the types 3 and 4
 * and the fishing tables the types 5 and 6.
 *
 * The table holds no pointer, so its layout is the game's at every pointer
 * width and these hold at every width.
 */
typedef struct BwPortEncountData {
    u8                  rate[7];       /* 0x00: one rate per encounter type */
    u8                  effectKind : 7;/* 0x07 */
    u8                  enabled : 1;   /* 0x07 */
    BwPortEncountSlot   ground[3][12]; /* 0x08 */
    BwPortEncountSlot   water[2][5];   /* 0x98 */
    BwPortEncountSlot   fishing[2][5]; /* 0xC0 */
} BwPortEncountData;

BW_PORT_STATIC_ASSERT(encount_slot_size, sizeof(BwPortEncountSlot) == 4u);
BW_PORT_STATIC_ASSERT(encount_ground, offsetof(BwPortEncountData, ground) == 0x08u);
BW_PORT_STATIC_ASSERT(encount_water, offsetof(BwPortEncountData, water) == 0x98u);
BW_PORT_STATIC_ASSERT(encount_fishing, offsetof(BwPortEncountData, fishing) == 0xC0u);
BW_PORT_STATIC_ASSERT(encount_data_size, sizeof(BwPortEncountData) == 0xE8u);

/*
 * The encounter parameters, copied from the game's own source (aletheia
 * src/game/ov021/field_encount.c): the type of the encounter, the table
 * the slots came from and the flags the setup reads.
 *
 * The offsets the game's own source documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortEncountParam {
    void *gameData;       /* 0x00 */
    void *encountSave;    /* 0x04 */
    int   encountType;    /* 0x08: the rate index, 0xFF when scripted */
    int   triggerMode;    /* 0x0C: 0 walk, 1 a spot, 2 forced, 3 White Forest */
    u32   slotTableKind;  /* 0x10: 0 ground, 1 water, 2 fishing, 3 swarm */
    u8    slotCount;      /* 0x14 */
    u8    monCount;       /* 0x15 */
    u16   leadSpecies;    /* 0x16 */
    u16   leadItem;       /* 0x18 */
    u8    leadIsEgg;      /* 0x1A */
    u8    leadAbility;    /* 0x1B */
    u8    leadGender;     /* 0x1C */
    u8    leadNature;     /* 0x1D */
    u8    leadLevel;      /* 0x1E */
    u8    reserved1F;      /* 0x1F */
    u32   ownerId;        /* 0x20 */
    void *myStatus;       /* 0x24 */
    u32   swarmEnabled : 1;   /* 0x28 */
    u32   isFishing : 1;
    u32   repelActive : 1;
    u32   ignoreRepel : 1;
    u32   unusedFlag : 1;
    u32   isDoubleBattle : 1;
    u32   rateUp : 1;
    u32   rateDown : 1;
    u32   fishingRateUp : 1;
    u32   compoundEyes : 1;
    u32   syncNature : 1;
    u32   cuteCharm : 1;
    u32   magnetPull : 1;
    u32   staticElectric : 1;
    u32   highLevelBias : 1;
    u32   intimidate : 1;
} BwPortEncountParam;

BW_PORT_STATIC_ASSERT(encount_param_type, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, encountType) == 0x08u);
BW_PORT_STATIC_ASSERT(encount_param_trigger, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, triggerMode) == 0x0Cu);
BW_PORT_STATIC_ASSERT(encount_param_kind, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, slotTableKind) == 0x10u);
BW_PORT_STATIC_ASSERT(encount_param_slot_count, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, slotCount) == 0x14u);
BW_PORT_STATIC_ASSERT(encount_param_lead_species, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, leadSpecies) == 0x16u);
BW_PORT_STATIC_ASSERT(encount_param_owner, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, ownerId) == 0x20u);
BW_PORT_STATIC_ASSERT(encount_param_my_status, sizeof(void *) != 4 ||
                      offsetof(BwPortEncountParam, myStatus) == 0x24u);
BW_PORT_STATIC_ASSERT(encount_param_flags, sizeof(BwPortEncountParam) ==
                      (sizeof(void *) == 4 ? 0x2Cu : 0x040u));

/*
 * One generated wild Pokemon, copied from the game's own source (aletheia
 * src/game/ov021/field_encount.c): the species, the level and the shiny
 * mode the setup reads.
 *
 * The record holds no pointer, so its layout is the game's at every pointer
 * width and these hold at every width.
 */
typedef struct BwPortEncountMon {
    u16 species;        /* 0x00 */
    u16 heldItem;       /* 0x02 */
    u8  level;          /* 0x04 */
    u8  form;           /* 0x05 */
    u8  shinyMode;      /* 0x06: 0 the roll, 1 always shiny, 2 never */
    u8  hiddenAbility;  /* 0x07 */
    u8  reserved08[8];  /* 0x08 */
    u8  forcedGender;   /* 0x10 */
    u8  reserved11[3];  /* 0x11 */
} BwPortEncountMon;

BW_PORT_STATIC_ASSERT(encount_mon_species, offsetof(BwPortEncountMon, species) == 0x00u);
BW_PORT_STATIC_ASSERT(encount_mon_held_item, offsetof(BwPortEncountMon, heldItem) == 0x02u);
BW_PORT_STATIC_ASSERT(encount_mon_level, offsetof(BwPortEncountMon, level) == 0x04u);
BW_PORT_STATIC_ASSERT(encount_mon_shiny, offsetof(BwPortEncountMon, shinyMode) == 0x06u);
BW_PORT_STATIC_ASSERT(encount_mon_size, sizeof(BwPortEncountMon) == 0x14u);

/*
 * The head of the battle setup, copied from the game's own header
 * (aletheia src/game/arm9/btl_setup_types.h).  The port reads the
 * competitor to tell a wild battle from a trainer one; the party of the
 * opponent comes from the game's own accessor, so nothing else of the
 * record is needed.
 *
 * The head holds no pointer, so its layout is the game's at every pointer
 * width and these hold at every width.
 */
typedef struct BwPortBattleSetup {
    u32 competitor;     /* 0x00: 0 a wild battle, 1 a trainer one */
    u32 rule;           /* 0x04 */
} BwPortBattleSetup;

BW_PORT_STATIC_ASSERT(setup_competitor, offsetof(BwPortBattleSetup, competitor) == 0x00u);
BW_PORT_STATIC_ASSERT(setup_rule, offsetof(BwPortBattleSetup, rule) == 0x04u);
BW_PORT_STATIC_ASSERT(setup_size, sizeof(BwPortBattleSetup) == 8u);

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/* The field menu's seven slots (the game's FIELD_MENU_ITEM_MAX). */
#define BW_PORT_MENU_ITEMS 7u

/* The button id of a slot that shows nothing (the game's own). */
#define BW_PORT_MENU_BUTTON_EMPTY 7

/* The kinds of encounter.static the port names (the payload's `kind`). */
#define BW_PORT_STATIC_KIND_STATIC   0u  /* a wild Pokemon the game put there */
#define BW_PORT_STATIC_KIND_LEGENDARY 1u /* not told apart from a static */
#define BW_PORT_STATIC_KIND_SCRIPTED 2u  /* a battle a script started */

/* The player's move modes (the payload's `mode`). */
#define BW_PORT_MOVE_WALKING 0u
#define BW_PORT_MOVE_RUNNING 1u
#define BW_PORT_MOVE_CYCLING 2u
#define BW_PORT_MOVE_SURFING 3u

/* The player's form code of the surf form (the game's own). */
#define BW_PORT_FORM_CODE_SURF 2u
#define BW_PORT_FORM_CODE_DIVE 3u

/* The B button of the pad words (the game's own: B is 0x0002). */
#define BW_PORT_KEY_B 0x0002u

/* A location that names a cell directly (the game's own Location type). */
#define BW_PORT_LOCATION_DIRECT 1

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/*
 * The cell of a fixed point position, the way the game itself converts it
 * (src/game/ov021/field_player_core.c, FIELD_PLAYER_CORE_SetPos):
 * (pos >> 4) / 4096.  A cell is 0x10000 in the game's fixed point, which
 * is the unit the warp scripts use as well (a cell << 16).
 */
static s16 cell_of_fx32(fx32 value)
{
    return (s16)(((s32)value >> 4) / 4096);
}

/* A cell as the game's fixed point position. */
static fx32 fx32_of_cell(s16 cell)
{
    return (fx32)((s32)cell << 16);
}

/*
 * The player's position, in cells.  The game publishes the position in
 * fixed point and no cell of it, so the port converts it the way the game
 * does.
 */
static void player_cell(const void *player, s16 *x, s16 *y, s16 *z)
{
    struct BwVecFx32 pos;

    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    if (z != NULL) {
        *z = 0;
    }
    if (player == NULL || FIELD_PLAYER_GetPos == NULL) {
        return;
    }
    pos.x = 0;
    pos.y = 0;
    pos.z = 0;
    FIELD_PLAYER_GetPos((struct FieldPlayer *)player, &pos);
    if (x != NULL) {
        *x = cell_of_fx32(pos.x);
    }
    if (y != NULL) {
        *y = cell_of_fx32(pos.y);
    }
    if (z != NULL) {
        *z = cell_of_fx32(pos.z);
    }
}

/*
 * How the player moves: walking, running, cycling or surfing.  The form
 * code says that the player surfs, the ride effect that the player rides
 * the bike, and the pad that the player runs (the B button is the run
 * button while on foot).
 */
static u8 player_mode(const BwPortFieldEventInput *input)
{
    u32 form = 0u;
    u32 ride = 0u;

    if (input == NULL || input->player == NULL) {
        return (u8)BW_PORT_MOVE_WALKING;
    }
    if (FIELD_PLAYER_GetFormCode != NULL) {
        form = FIELD_PLAYER_GetFormCode((struct FieldPlayer *)input->player);
        if (form == BW_PORT_FORM_CODE_SURF || form == BW_PORT_FORM_CODE_DIVE) {
            return (u8)BW_PORT_MOVE_SURFING;
        }
    }
    if (FIELD_PLAYER_GetRideEffect != NULL) {
        ride = FIELD_PLAYER_GetRideEffect((struct FieldPlayer *)input->player);
        if (ride != 0) {
            return (u8)BW_PORT_MOVE_CYCLING;
        }
    }
    if ((input->keyCont & BW_PORT_KEY_B) != 0) {
        return (u8)BW_PORT_MOVE_RUNNING;
    }
    return (u8)BW_PORT_MOVE_WALKING;
}

/*
 * The way an encounter came, from the game's own encounter type (the index
 * of the zone's rate table: 0 grass, 1 dark grass, 2 the rustling grass
 * and the dust cloud, 3 surfing, 4 rippling water, 5 fishing, 6 fishing
 * in rippling water).
 *
 * The port's list names the rustling grass as the shaking method.  The
 * game tells the rustling grass and the dust cloud apart by the spot it
 * puts on the map, not by the encounter type, so the dust method has no
 * feeder at this call.
 */
static u8 encounter_method(int encountType)
{
    switch (encountType) {
    case 0:
        return (u8)BW_ENCOUNTER_GRASS;
    case 1:
        return (u8)BW_ENCOUNTER_DARK_GRASS;
    case 2:
        return (u8)BW_ENCOUNTER_SHAKING;
    case 3:
        return (u8)BW_ENCOUNTER_SURF;
    case 4:
        return (u8)BW_ENCOUNTER_RIPPLING;
    case 5:
    case 6:
        return (u8)BW_ENCOUNTER_FISHING;
    default:
        /* The scripted type (0xFF) is not one of the port's methods; the
         * port reads it as the grass method. */
        return (u8)BW_ENCOUNTER_GRASS;
    }
}

/*
 * The slot of the encounter table the game rolled.
 *
 * The game rolls a slot of the table it loaded and keeps neither the
 * index nor the slot in the wild Pokemon it builds, so the port finds the
 * slot again by the species and the level it rolled: the slot whose
 * species is the one the encounter got and whose level range holds the
 * level it got.  A table that holds the species twice gives the first of
 * the two; a level the high level bias raised above the slot's range
 * leaves the slot at 0.
 */
static u8 encounter_slot(const BwPortEncountData *data,
                         const BwPortEncountParam *param,
                         const BwPortEncountMon *mons)
{
    const BwPortEncountSlot *table;
    u32 count;
    u32 i;

    if (data == NULL || param == NULL || mons == NULL) {
        return 0u;
    }
    count = param->slotCount;
    switch (param->slotTableKind) {
    case 0:     /* the ground tables: the types 0 to 2 */
        if (param->encountType < 0 || param->encountType > 2) {
            return 0u;
        }
        table = data->ground[param->encountType];
        break;
    case 1:     /* the water tables: the types 3 and 4 */
        if (param->encountType < 3 || param->encountType > 4) {
            return 0u;
        }
        table = data->water[param->encountType - 3];
        break;
    case 2:     /* the fishing tables: the types 5 and 6 */
        if (param->encountType < 5 || param->encountType > 6) {
            return 0u;
        }
        table = data->fishing[param->encountType - 5];
        break;
    default:
        /* The swarm: its species comes from the save's own entry, not
         * from a table of the zone. */
        return 0u;
    }
    for (i = 0; i < count; i++) {
        if (table[i].species == mons[0].species &&
            mons[0].level >= table[i].minLevel &&
            mons[0].level <= table[i].maxLevel) {
            return (u8)i;
        }
    }
    for (i = 0; i < count; i++) {
        if (table[i].species == mons[0].species) {
            return (u8)i;
        }
    }
    return 0u;
}

/*
 * The battle format of a trainer battle, from the game's own rule: 0 a
 * single (or a tag) battle, 1 a double (or a multi) one, 2 a triple, 3 a
 * rotation.
 */
static u8 encounter_format(u32 rule, u32 partnerId)
{
    switch (rule) {
    case 1:
        return (partnerId != 0u) ? (u8)BW_FORMAT_MULTI : (u8)BW_FORMAT_DOUBLE;
    case 2:
        return (u8)BW_FORMAT_TRIPLE;
    case 3:
        return (u8)BW_FORMAT_ROTATION;
    default:
        return (u8)BW_FORMAT_SINGLE;
    }
}

/*
 * The opponent of a battle setup: the party the setup holds for the
 * second client, and its first member.
 */
static struct PokemonParam *opponent_lead(const BwPortBattleSetup *setup)
{
    struct Party *party;

    if (setup == NULL || BTL_SETUP_GetParty == NULL ||
        POKEPARTY_GetMember == NULL) {
        return NULL;
    }
    party = (struct Party *)BTL_SETUP_GetParty((struct BattleSetupParam *)setup,
                                               1);
    if (party == NULL) {
        return NULL;
    }
    return POKEPARTY_GetMember(party, 0u);
}

/* ------------------------------------------------------------------ */
/* The builders                                                        */
/* ------------------------------------------------------------------ */

/*
 * route.change on foot: FIELDMAP_ChangeZone (fieldMap) runs when the
 * player walks over a zone border on the same map; the event fires on its
 * return, when the player's zone is the new one.  The map stays, so the
 * field context keeps its position and the avatars stay.
 */
static void build_route_change_walk(const BwEventCall *call, BwEventPayload *out)
{
    const BwModState *st = bw_mod_state_table();
    u16 zone = (st != NULL && st->zone != NULL) ? st->zone() : 0u;

    (void)call;
    memset(&out->route_change, 0, sizeof(out->route_change));
    out->route_change.zone = zone;
    out->route_change.prev_zone = bw_mod_field_context.zone;
    out->route_change.season = bw_mod_field_context.season;
    out->route_change.how = (u8)BW_ZONE_WALK;
    bw_mod_field_context.prev_zone = bw_mod_field_context.zone;
    bw_mod_field_context.zone = zone;
}

/*
 * route.change: the player enters a new zone.  The new zone is the
 * argument, the zone left is the one the field context holds, and the
 * season is the game's own.
 *
 * The field context is reset here and filled again (docs/mod-payloads.md,
 * section 1): the zone the player was in is not the zone the player is in
 * now, and the position and the facing of the old zone are stale.
 */
static void build_route_change(const BwEventCall *call, BwEventPayload *out)
{
    struct GameData *gameData = (struct GameData *)call->a[0];
    u16 zone = (u16)call->a[1];
    u16 prev_zone = bw_mod_field_context.zone;
    u8 season = 0u;

    memset(&out->route_change, 0, sizeof(out->route_change));
    if (gameData != NULL && GAME_DATA_GetSeason != NULL) {
        season = GAME_DATA_GetSeason(gameData);
    }
    out->route_change.zone = zone;
    out->route_change.prev_zone = prev_zone;
    out->route_change.season = season;
    out->route_change.how = (u8)BW_ZONE_WARP;

    memset(&bw_mod_field_context, 0, sizeof(bw_mod_field_context));
    bw_mod_field_context.zone = zone;
    bw_mod_field_context.prev_zone = prev_zone;
    bw_mod_field_context.season = season;

    /* The avatars belong to the zone: the old ones are gone before the
     * mod's handlers run, so a mod recreates the ones it wants in the new
     * zone (docs/mod-services.md, "Field avatars"). */
    bw_mod_avatar_reset();
}

/*
 * map.warp: the player is warped to another place.  The zone the warp
 * starts in and the zone it leads to are the map change work's own, and
 * the arrival cell is the destination's own position.
 *
 * The change field event is a step machine: the game calls it once per
 * step of the map change, so the event fires once per step (seven times
 * for a normal change).  The destination the handler changes is the one
 * the game applies at the step that moves the player.
 *
 * The arrival cell reads 0 for a warp to a rail position: the game names
 * the rail cell of the destination in its own way, and the cell the port
 * reads is the one of a direct position.
 */
static void build_map_warp(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortChangeFieldWork *change =
        (const BwPortChangeFieldWork *)call->a[2];
    const BwPortMapChangeWork *work;

    memset(&out->map_warp, 0, sizeof(out->map_warp));
    if (change == NULL || change->mapChange == NULL) {
        return;
    }
    work = (const BwPortMapChangeWork *)change->mapChange;
    out->map_warp.zone = work->currentZoneId;
    out->map_warp.dest_zone = work->destination.zoneId;
    if (work->destination.type == BW_PORT_LOCATION_DIRECT) {
        out->map_warp.dest_x = cell_of_fx32(work->destination.pos.vec.x);
        out->map_warp.dest_z = cell_of_fx32(work->destination.pos.vec.z);
    }
}

/*
 * map.warp: the destination a handler changed is the one the game applies.
 * The zone is written into the destination record, and the arrival cell
 * into its position, but only for a direct position: a rail position
 * names its cell in the game's own way.
 */
static void apply_map_warp(const BwEventPayload *in, BwEventCall *call)
{
    const BwPortChangeFieldWork *change =
        (const BwPortChangeFieldWork *)call->a[2];
    BwPortMapChangeWork *work;

    if (change == NULL || change->mapChange == NULL) {
        return;
    }
    work = (BwPortMapChangeWork *)change->mapChange;
    work->destination.zoneId = in->map_warp.dest_zone;
    if (work->destination.type != BW_PORT_LOCATION_DIRECT &&
        (in->map_warp.dest_x != 0 || in->map_warp.dest_z != 0)) {
        /*
         * A door names its arrival by the exit it leads to, which the game
         * finds in the new zone; a handler that gives a cell gets a direct
         * position there instead (ZONEDATA_SetDirectLocation's form: no
         * exit, the height of the floor at 0, the facing kept), or the exit
         * of the old zone's door is looked up in the new one.
         */
        work->destination.type = BW_PORT_LOCATION_DIRECT;
        work->destination.exitId = -1;
        work->destination.exitOffset = 0;
        work->destination.posType = 0;
        work->destination.pos.vec.x = fx32_of_cell(in->map_warp.dest_x);
        work->destination.pos.vec.y = 0;
        work->destination.pos.vec.z = fx32_of_cell(in->map_warp.dest_z);
        return;
    }
    if (work->destination.type != BW_PORT_LOCATION_DIRECT) {
        return;
    }
    /*
     * The position is written only when the cell moved: a destination
     * that is not on a cell (a tile centre, say) keeps the place it has,
     * and a cell the handler moved to is the cell it names.
     */
    if (cell_of_fx32(work->destination.pos.vec.x) != in->map_warp.dest_x) {
        work->destination.pos.vec.x = fx32_of_cell(in->map_warp.dest_x);
    }
    if (cell_of_fx32(work->destination.pos.vec.z) != in->map_warp.dest_z) {
        work->destination.pos.vec.z = fx32_of_cell(in->map_warp.dest_z);
    }
}

/*
 * player.step: the player took a step.  The zone is the map's own, the
 * cell is the player's own position (the step is finished when the game
 * asks), the facing is the direction of the player model, and the mode is
 * how the player moves.
 *
 * The field context is fed here (docs/mod-payloads.md, section 1): the
 * player position and the facing are read at the step, and the payloads
 * that want them read the context.
 */
static void build_player_step(const BwEventCall *call, BwEventPayload *out)
{
    /* A step on the field is the quiet moment the state calls' queued
     * moves wait for (sdk/mod_state.h): no battle, no party screen and no
     * PC hold the party then.  The row is the port's own, so it runs with
     * or without a handler. */
    if (bw_mod_state_pending() > 0u) {
        bw_mod_state_apply_pending();
    }

    const BwPortFieldEventInput *input =
        (const BwPortFieldEventInput *)call->a[0];
    struct FieldMap *fieldMap = (struct FieldMap *)call->a[1];
    s16 x = 0;
    s16 y = 0;
    s16 z = 0;
    u8 dir = 0u;

    memset(&out->player_step, 0, sizeof(out->player_step));
    if (fieldMap != NULL && FIELDMAP_GetZoneId != NULL) {
        out->player_step.zone = FIELDMAP_GetZoneId(fieldMap);
    }
    if (input != NULL) {
        dir = (u8)input->facingDir;
        player_cell(input->player, &x, &y, &z);
    }
    out->player_step.x = x;
    out->player_step.y = y;
    out->player_step.z = z;
    out->player_step.dir = dir;
    out->player_step.mode = player_mode(input);

    bw_mod_field_context.pos_x = x;
    bw_mod_field_context.pos_y = y;
    bw_mod_field_context.pos_z = z;
    bw_mod_field_context.dir = dir;
}

/*
 * trainer.spotted: a trainer sees the player.  The trainer and the
 * distance come from the game's own search, the same one the bound
 * function runs: the port asks for the trainer that sees the player and
 * for the second one that sees it too, which is the partner of a pair
 * battle.
 *
 * A trainer that does not see the player leaves the payload empty: the
 * bound function finds no trainer and returns no event.
 */
static void build_trainer_spotted(const BwEventCall *call, BwEventPayload *out)
{
    struct FieldMap *fieldMap = (struct FieldMap *)call->a[0];
    struct BwPortTrainerEyeHit hit;
    struct BwPortTrainerEyeHit partner;

    memset(&out->trainer_spotted, 0, sizeof(out->trainer_spotted));
    if (fieldMap == NULL || EVENT_TRAINER_EYE_FindTrainer == NULL) {
        return;
    }
    memset(&hit, 0, sizeof(hit));
    if (EVENT_TRAINER_EYE_FindTrainer(fieldMap, NULL, &hit) != 1) {
        return;     /* nobody sees the player */
    }
    out->trainer_spotted.trainer_id = (u16)hit.trainerId;
    out->trainer_spotted.distance = (u8)hit.distance;

    memset(&partner, 0, sizeof(partner));
    if (EVENT_TRAINER_EYE_FindTrainer(fieldMap, hit.trainer, &partner) == 1) {
        out->trainer_spotted.partner_id = (u16)partner.trainerId;
    }
}

/*
 * trainer.spotted: the refused outcome is that the trainer does not see
 * the player: the function returns no event, and the field check that
 * asked for it goes on (docs/mod-implementation.md, section 1, "Refusal
 * functions").
 */
static void refuse_trainer_spotted(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * npc.talk: the player pressed A in front of a person or an object.  The
 * zone is the map's own.
 *
 * The object and the script read 0: the model in front of the player is
 * what the bound search returns, and the port does not run the search a
 * second time to name it; the script the talk runs is the event id of
 * that model, which the game reads when the search has answered.
 */
static void build_npc_talk(const BwEventCall *call, BwEventPayload *out)
{
    struct FieldMap *fieldMap = (struct FieldMap *)call->a[1];

    memset(&out->npc_talk, 0, sizeof(out->npc_talk));
    if (fieldMap != NULL && FIELDMAP_GetZoneId != NULL) {
        out->npc_talk.zone = FIELDMAP_GetZoneId(fieldMap);
    }
}

/*
 * npc.talk: the refused outcome is that no person is found ahead: the
 * search answers 0, and the A press goes on to the sign and the tile
 * checks (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_npc_talk(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * menu.open: the X-button menu opens.  The payload has no fields
 * (docs/mod-payloads.md, section 2): the event is the moment itself.
 */
static void build_menu_open(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->menu_open, 0, sizeof(out->menu_open));
}

/*
 * menu.open: the refused outcome is that the menu does not open: the
 * function returns no event, and the field check that asked for it goes
 * on (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_menu_open(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * menu.build: the field menu was built.  The entries are the slots of
 * the menu that show a button, one bit per slot.
 *
 * The event fires on the return of the menu's creation, not before it:
 * the slots and the buttons in them exist only once the menu is built,
 * and the entries a handler changes are written into the menu the game
 * then shows.  (The other rows of this area fire before their function;
 * the docs name the point only for the rows that read a result.)
 */
static void build_menu_build(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFieldMenu *menu = (const BwPortFieldMenu *)(uintptr_t)call->ret;
    u32 entries = 0u;
    u32 i;

    memset(&out->menu_build, 0, sizeof(out->menu_build));
    if (menu == NULL) {
        return;
    }
    for (i = 0; i < BW_PORT_MENU_ITEMS; i++) {
        if (menu->items[i].inUse != 0 &&
            menu->items[i].buttonId != BW_PORT_MENU_BUTTON_EMPTY) {
            entries |= 1u << i;
        }
    }
    out->menu_build.entries = entries;
}

/*
 * menu.build: an entry a handler cleared is hidden: the slot becomes the
 * empty slot the game itself shows (the button id of the empty one, and
 * the icon not drawn).
 *
 * An entry a handler added is not built: the game prints the label of a
 * slot and draws its icon from the message bank and the archive it opens
 * while it creates the menu, and it closes both again, so the port cannot
 * make a slot the game left empty.  The slot keeps the label the game
 * printed for it.
 */
static void apply_menu_build(const BwEventPayload *in, BwEventCall *call)
{
    BwPortFieldMenu *menu = (BwPortFieldMenu *)(uintptr_t)call->ret;
    u32 i;

    if (menu == NULL) {
        return;
    }
    for (i = 0; i < BW_PORT_MENU_ITEMS; i++) {
        if ((in->menu_build.entries & (1u << i)) != 0u) {
            continue;   /* the entry stays as the game made it */
        }
        menu->items[i].buttonId = BW_PORT_MENU_BUTTON_EMPTY;
        if (menu->items[i].actor != NULL && CLACT_SetActorDrawEnable != NULL) {
            CLACT_SetActorDrawEnable(menu->items[i].actor, 0);
        }
    }
}

/*
 * The port's own work at the field menu: in a multiplayer game only the
 * host saves, so a joiner's menu has no Save (the label stays, the button
 * does nothing and its icon is gone).  Button 5 is Save: it is the one
 * the game's own layouts leave out during a link.
 */
#define BW_PORT_MENU_BUTTON_SAVE 5

static void apply_menu_save(const BwEventPayload *in, BwEventCall *call)
{
    BwPortFieldMenu *menu = (BwPortFieldMenu *)(uintptr_t)call->ret;
    u32 i;

    (void)in;
    if (menu == NULL || !bw_mp_hide_save()) {
        return;
    }
    for (i = 0; i < BW_PORT_MENU_ITEMS; i++) {
        if (menu->items[i].inUse != 0 &&
            menu->items[i].buttonId == BW_PORT_MENU_BUTTON_SAVE) {
            menu->items[i].buttonId = BW_PORT_MENU_BUTTON_EMPTY;
            if (menu->items[i].actor != NULL && CLACT_SetActorDrawEnable != NULL) {
                CLACT_SetActorDrawEnable(menu->items[i].actor, 0);
            }
            /* the label goes too: the slot is empty, as in a link */
            if (menu->items[i].bmpWin != NULL && BMP_WIN_GetBmp != NULL &&
                BMP_Clear != NULL && BMP_WIN_TransferChars != NULL) {
                BMP_Clear(BMP_WIN_GetBmp(menu->items[i].bmpWin), 0);
                BMP_WIN_TransferChars(menu->items[i].bmpWin);
            }
        }
    }
}

/*
 * field.move: the game asks whether one of the ten field moves can be
 * used.  The move is the index the game asks for, which is its own
 * numbering of the field moves.
 *
 * The user reads 0: the Pokemon that uses the move is the one the game's
 * own handler for the move looks for in the party, and the port names
 * neither the move the index stands for nor the member that has it.
 */
static void build_field_move(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->field_move, 0, sizeof(out->field_move));
    out->field_move.move = (u16)call->a[0];
}

/*
 * field.move: the refused outcome is that the move shows as unusable:
 * the check answers 0, and the game leaves the move out of the choices
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_field_move(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * pc.open: the script opens the PC (or the battle box).  The payload has
 * no fields (docs/mod-payloads.md, section 2): the event is the moment
 * itself.
 */
static void build_pc_open(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->pc_open, 0, sizeof(out->pc_open));
}

/*
 * pc.open: the refused outcome is that the PC does not open: the
 * command's result variable gets the value a plain exit leaves and the
 * command answers 1, so the script goes on (docs/mod-implementation.md,
 * section 1, "Refusal functions").
 */
static void refuse_pc_open(BwEventCall *call)
{
    struct ScriptVm *vm = (struct ScriptVm *)call->a[0];
    struct ScrcmdWork *cmdWork = (struct ScrcmdWork *)call->a[1];
    u16 *resultVar = NULL;

    if (SCRIPT_WORK_ReadWorkPointer != NULL) {
        resultVar = SCRIPT_WORK_ReadWorkPointer(vm, cmdWork);
    }
    if (resultVar != NULL) {
        *resultVar = 0u;
    }
    call->ret = 1;
}

/*
 * encounter.wild: a wild Pokemon was rolled.  The event fires on the
 * return of the game's own function, because the Pokemon it fills are the
 * ones the function built.
 *
 * The zone is the argument, the method is the game's own encounter type,
 * the slot is the slot of the zone's table the game rolled, and the
 * species, the level and the shiny mode are the ones the function built.
 */
static void build_encounter_wild(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortEncountData *data = (const BwPortEncountData *)call->a[0];
    const BwPortEncountParam *param = (const BwPortEncountParam *)call->a[1];
    const BwPortEncountMon *mons = (const BwPortEncountMon *)call->a[3];

    memset(&out->encounter_wild, 0, sizeof(out->encounter_wild));
    bw_mod_state_set_battle_static(0);
    out->encounter_wild.zone = (u16)call->a[2];
    if (param != NULL) {
        out->encounter_wild.method = encounter_method(param->encountType);
        out->encounter_wild.double_ = param->isDoubleBattle ? 1u : 0u;
    }
    if (call->ret > 0) {
        /* The function counted the Pokemon it built; there is none when
         * the count is 0 (a repel, say), and then the payload names no
         * Pokemon and no slot. */
        if (mons != NULL) {
            out->encounter_wild.species = mons[0].species;
            out->encounter_wild.level = mons[0].level;
            out->encounter_wild.shiny = (mons[0].shinyMode == 1u) ? 1u : 0u;
            if (call->ret > 1) {
                out->encounter_wild.species2 = mons[1].species;
                out->encounter_wild.level2 = mons[1].level;
            }
        }
        out->encounter_wild.slot = encounter_slot(data, param, mons);
    }
}

/*
 * encounter.wild: the species, the level and the shiny a handler changed
 * are the ones the game builds the wild Pokemon from.  A species of 0 and
 * a level of 0 are not written: the game never rolls them, and the port
 * does not put a Pokemon without a species into the battle.
 */
static void write_encounter_wild(const BwEventPayload *in,
                                 const BwEventCall *call,
                                 BwPortEncountMon *mons)
{
    if (in->encounter_wild.species != 0u) {
        mons[0].species = in->encounter_wild.species;
    }
    if (in->encounter_wild.level != 0u) {
        mons[0].level = in->encounter_wild.level;
    }
    if (in->encounter_wild.shiny != 0u) {
        mons[0].shinyMode = 1u;   /* 1 is always shiny, 2 is never */
    }
    if (call->ret > 1) {
        if (in->encounter_wild.species2 != 0u) {
            mons[1].species = in->encounter_wild.species2;
        }
        if (in->encounter_wild.level2 != 0u) {
            mons[1].level = in->encounter_wild.level2;
        }
    }
}

/*
 * The most rolls one encounter takes: a handler that asks for a reroll
 * every time (a dupes clause on a route of one species) still ends.
 */
#define BW_ENCOUNTER_REROLL_MAX 16u

/*
 * A reroll runs the game's own roll again (FIELD_ENCOUNT_CreateZoneWildMons
 * loads the zone's slots and rolls the Pokemon; it keeps no state between
 * calls) and fires the event again with what it rolled.  After the last
 * roll the handlers' answer stands, reroll or not.  A refusal at a reroll
 * is the event's refused outcome, no encounter.
 */
extern int FIELD_ENCOUNT_CreateZoneWildMons(void *encountData, void *param,
                                            u16 zoneId, void *mons)
    __attribute__((weak));

static void apply_encounter_wild(const BwEventPayload *in, BwEventCall *call)
{
    BwPortEncountMon *mons = (BwPortEncountMon *)call->a[3];
    BwEventPayload again;
    u32 tries = 0;

    if (mons == NULL) {
        return;
    }
    while (in->encounter_wild.reroll != 0u && tries < BW_ENCOUNTER_REROLL_MAX &&
           FIELD_ENCOUNT_CreateZoneWildMons != NULL) {
        tries++;
        call->ret = FIELD_ENCOUNT_CreateZoneWildMons((void *)call->a[0],
                                                     (void *)call->a[1],
                                                     (u16)call->a[2], mons);
        if (call->ret <= 0) {
            return;     /* the roll found nothing: no encounter */
        }
        build_encounter_wild(call, &again);
        if (bw_mod_event_fire(BW_EVENT_ENCOUNTER_WILD, &again)) {
            call->ret = 0;
            return;
        }
        in = &again;
    }
    write_encounter_wild(in, call, mons);
}

/*
 * encounter.wild: the refused outcome is that no battle starts: the
 * count of wild Pokemon is 0, which the game already reads as no
 * encounter, so the player keeps walking (docs/mod-implementation.md,
 * section 1, "Refusal functions").
 */
static void refuse_encounter_wild(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * encounter.wild: a roamer.  The event fires on the return of the game's
 * own function, which picks the roamer of the zone.
 *
 * The species and the level are the roamer's own record in the save's
 * move Pokemon block.  The slot reads 0: a roamer comes from the save and
 * not from a table of the zone.  The shiny reads 0: the roamer's
 * personality is the save's own, and the game does not roll it.  The
 * double flag reads 0: the game sets the roamer's battle up alone.
 */
static void build_encounter_roamer(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortEncountParam *param = (const BwPortEncountParam *)call->a[1];
    const void *roamer = (const void *)(uintptr_t)call->ret;

    memset(&out->encounter_wild, 0, sizeof(out->encounter_wild));
    bw_mod_state_set_battle_static(0);
    out->encounter_wild.zone = (u16)call->a[2];
    if (param != NULL) {
        out->encounter_wild.method = encounter_method(param->encountType);
    }
    if (roamer != NULL && MOVE_POKEMON_SAVE_GetEntryField != NULL) {
        out->encounter_wild.species =
            (u16)MOVE_POKEMON_SAVE_GetEntryField(roamer, 4u);
        out->encounter_wild.level =
            (u8)MOVE_POKEMON_SAVE_GetEntryField(roamer, 6u);
    }
}

/*
 * encounter.wild: the species and the level a handler changed are the
 * roamer's own record, which the game reads when it builds the roamer's
 * Pokemon.  The record lives in the save's move Pokemon block, so the
 * change is kept until the save is written.
 */
static void apply_encounter_roamer(const BwEventPayload *in, BwEventCall *call)
{
    void *roamer = (void *)(uintptr_t)call->ret;

    if (roamer == NULL || MOVE_POKEMON_SAVE_SetEntryField == NULL) {
        return;
    }
    if (in->encounter_wild.species != 0u) {
        MOVE_POKEMON_SAVE_SetEntryField(roamer, 4u,
                                         in->encounter_wild.species);
    }
    if (in->encounter_wild.level != 0u) {
        MOVE_POKEMON_SAVE_SetEntryField(roamer, 6u, in->encounter_wild.level);
    }
}

/*
 * encounter.wild: the refused outcome is that no roamer appears: the
 * function answers that it found none, and the zone's normal encounter
 * follows and fires its own encounter.wild
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_encounter_roamer(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * encounter.static: a battle the game sets up with a Pokemon in it.  The
 * zone is the player's own zone, the species and the level are the ones
 * of the opponent the setup holds, and the kind says whether the battle
 * is a wild one or a battle a script started.
 *
 * The legendary kind has no feeder at this call: the game chooses the
 * species in the script that starts the battle, and the setup carries no
 * flag for it.
 */
static void build_encounter_static(const BwEventCall *call, BwEventPayload *out)
{
    struct GameSystem *gameSystem = (struct GameSystem *)call->a[0];
    const BwPortBattleSetup *setup = (const BwPortBattleSetup *)call->a[2];
    struct GameData *gameData;
    struct PokemonParam *lead;

    memset(&out->encounter_static, 0, sizeof(out->encounter_static));
    bw_mod_state_set_battle_static(1);
    if (gameSystem != NULL && GAMESYSTEM_GetGameData != NULL &&
        GAME_DATA_GetMyPlayerWork != NULL &&
        GAME_DATA_GetPlayerZoneId != NULL) {
        gameData = GAMESYSTEM_GetGameData(gameSystem);
        if (gameData != NULL) {
            out->encounter_static.zone =
                GAME_DATA_GetPlayerZoneId(GAME_DATA_GetMyPlayerWork(gameData));
        }
    }
    if (setup != NULL) {
        out->encounter_static.kind =
            (setup->competitor == 0u) ? (u8)BW_PORT_STATIC_KIND_STATIC
                                      : (u8)BW_PORT_STATIC_KIND_SCRIPTED;
    }
    lead = opponent_lead(setup);
    if (lead != NULL && POKE_TOOL_GetPartyValue != NULL) {
        out->encounter_static.species =
            (u16)POKE_TOOL_GetPartyValue(lead, BW_MON_FIELD_SPECIES, 0u);
        out->encounter_static.level =
            (u8)POKE_TOOL_GetPartyValue(lead, BW_MON_FIELD_LEVEL, 0u);
    }
}

/*
 * encounter.static: the species and the level a handler changed are the
 * ones of the Pokemon the battle is set up with.  The port writes the two
 * fields of the record and does not rebuild it: the personal data of the
 * new species is not recalculated, and a mod that wants that uses the
 * state calls.
 */
static void apply_encounter_static(const BwEventPayload *in, BwEventCall *call)
{
    const BwPortBattleSetup *setup = (const BwPortBattleSetup *)call->a[2];
    struct PokemonParam *lead;

    if (setup == NULL || POKE_TOOL_SetPartyValue == NULL) {
        return;
    }
    lead = opponent_lead(setup);
    if (lead == NULL) {
        return;
    }
    if (in->encounter_static.species != 0u) {
        POKE_TOOL_SetPartyValue(lead, BW_MON_FIELD_SPECIES,
                                in->encounter_static.species);
    }
    if (in->encounter_static.level != 0u) {
        POKE_TOOL_SetPartyValue(lead, BW_MON_FIELD_LEVEL,
                                in->encounter_static.level);
    }
}

/*
 * encounter.trainer: a trainer battle starts.  The trainer and its
 * partner are the arguments, and the format is the game's own rule.
 */
static void build_encounter_trainer(const BwEventCall *call,
                                    BwEventPayload *out)
{
    u32 rule = (u32)call->a[2];
    u32 partnerId = (u32)call->a[3];
    u32 trainerId1 = (u32)call->a[4];
    u32 trainerId2 = (u32)call->a[5];

    memset(&out->encounter_trainer, 0, sizeof(out->encounter_trainer));
    out->encounter_trainer.trainer_id = (u16)trainerId1;
    out->encounter_trainer.partner_id =
        (u16)((partnerId != 0u) ? partnerId : trainerId2);
    out->encounter_trainer.format = encounter_format(rule, partnerId);
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

/*
 * The rows of this area.  The scope of every row is BW_SCOPE_ANY: none of
 * these events only means something while a particular caller runs
 * (docs/mod-events.md, section 1).
 *
 * The refusals are the ones the documents name: a trainer that does not
 * see the player, a talk to nobody, a menu that does not open, a field
 * move that is unusable, a PC that does not open, and a wild encounter
 * that does not happen (docs/mod-implementation.md, section 1, "Refusal
 * functions").
 *
 * The two encounter.wild rows fire on the return of their function: the
 * payload names the Pokemon the function built, and the refused outcome is
 * the count of none (docs/mod-events.md, section 1, "Encounters").  The
 * menu.build row fires on the return of the menu's creation for the same
 * reason: the entries it names are the slots the menu has.
 */
static const BwEventBinding g_field_rows[] = {
    { BW_MOD_REGION_OVL(10), 0x021643E8u, BW_EVENT_ROUTE_CHANGE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_route_change, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021893C0u, BW_EVENT_ROUTE_CHANGE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_route_change_walk, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(20), 0x021841C0u, BW_EVENT_MAP_WARP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_map_warp, apply_map_warp, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x0218A9B0u, BW_EVENT_PLAYER_STEP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_player_step, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021AE0CCu, BW_EVENT_TRAINER_SPOTTED,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_trainer_spotted, NULL,
      refuse_trainer_spotted, NULL },

    { BW_MOD_REGION_OVL(21), 0x0218B4B0u, BW_EVENT_NPC_TALK, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_npc_talk, NULL, refuse_npc_talk, NULL },

    { BW_MOD_REGION_OVL(21), 0x0218B53Cu, BW_EVENT_NPC_TALK, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_npc_talk, NULL, refuse_npc_talk, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215FC40u, BW_EVENT_MENU_OPEN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_menu_open, NULL, refuse_menu_open, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215FCE0u, BW_EVENT_MENU_OPEN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_menu_open, NULL, refuse_menu_open, NULL },

    { BW_MOD_REGION_OVL(21), 0x021A836Cu, BW_EVENT_MENU_BUILD, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_menu_build, apply_menu_build, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021A836Cu, BW_EVENT_PORT, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_menu_save, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x021644C4u, BW_EVENT_FIELD_MOVE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_field_move, NULL, refuse_field_move, NULL },

    { BW_MOD_REGION_OVL(21), 0x021B4D60u, BW_EVENT_PC_OPEN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_pc_open, NULL, refuse_pc_open, NULL },

    { BW_MOD_REGION_OVL(21), 0x021A954Cu, BW_EVENT_ENCOUNTER_WILD,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_encounter_wild,
      apply_encounter_wild, refuse_encounter_wild, NULL },

    { BW_MOD_REGION_OVL(21), 0x021A957Cu, BW_EVENT_ENCOUNTER_WILD,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_encounter_roamer,
      apply_encounter_roamer, refuse_encounter_roamer, NULL },

    { BW_MOD_REGION_OVL(10), 0x0216E7C8u, BW_EVENT_ENCOUNTER_STATIC,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_encounter_static,
      apply_encounter_static, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021AA5CCu, BW_EVENT_ENCOUNTER_TRAINER,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_encounter_trainer, NULL,
      NULL, NULL }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_field(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_field_rows) / sizeof(g_field_rows[0]));
    }
    return g_field_rows;
}
