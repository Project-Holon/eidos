/*
 * sdk/mod_state.c -- the state calls: how a mod reads and writes the
 * game's own state (docs/mod-events.md, section 4; docs/mod-implementation.md,
 * section 4).
 *
 * A mod never sees a game struct.  Every value it asks for comes through
 * one of the calls of the BwModState table (sdk/mod_state.h), and each of
 * those calls is a thin wrapper over the game's own accessor: the port
 * names the game's function, the game keeps the layout of its own records
 * behind it, and aletheia can restructure those records without the mod's
 * ABI moving.
 *
 * The rules the calls keep (docs/mod-events.md, section 4):
 *
 *   - The battle works on a copy.  A write to the party record has no
 *     effect during a battle and is overwritten when the battle writes its
 *     results back, so the battle calls read and write the battle record
 *     (BTL_POKEPARAM_*) and not the party record.
 *   - The game's later write wins.  The port does not arbitrate a write.
 *   - Moves wait for a quiet moment.  mon_to_box and mon_take_item change
 *     the party, which a battle, the party screen and the PC hold open;
 *     the port queues them and applies them when none of those runs, in
 *     the order the mod made them.
 *
 * Every accessor below is declared weak.  The library is linked by the
 * tests, which have no game code in them, and a weak declaration lets the
 * link succeed there: the call finds the symbol missing and reports a
 * failure instead of calling into nothing.  A build that carries the game
 * (the boot, EIDOS_BOOT=ON) resolves the same names to the game's own
 * functions.
 *
 * The weak declaration is not the whole of the answer, and the state layer
 * does not rely on it alone.  The port ships a panicking stub for the
 * game's Pokemon record accessors (stubs/stub_02017E1C.c and
 * stubs/stub_02017E40.c), and those stubs are real definitions: a weak
 * reference to one of them is never absent from the link.  So the state
 * layer recognises the missing game itself: a call never reaches an
 * accessor before the port has bound the game's own objects, and a call
 * with nothing bound reports a failure.  (A weak undefined reference does
 * not pull an archive member out of the archive, so in a link that has
 * nothing else to pull the stub in, the stub stays out and the symbol is
 * NULL as well.)
 *
 * The names are the port's own spelling, which is the name the corpus
 * (src/game) gives the function.  Five of them are spelled differently in
 * docs/mod-events.md, section 4, where the table was written against an
 * older aletheia; the corpus's spelling is the one that links:
 *
 *   docs/mod-events.md      src/game
 *   BEACON_GetFlag          MISC_GetBadge      0x0200C234
 *   BEACON_SetFlag          MISC_SetBadge      0x0200C248
 *   BEACON_CountFlags       MISC_CountBadges   0x0200C258
 *   BEACON_GetPoints        MISC_GetMoney      0x0200C274
 *   POKE_TOOL_GetPersonalGender
 *                           POKE_TOOL_GetPersonalAbilityCount
 *                                                0x02019C7C
 *
 * The last one is not a rename but a different function: the address the
 * document names is the personal record's ability count, and the gender
 * ratio is POKE_PERSONAL_GetParam's field 20.  The species calls below
 * use that field, so a mod reads the gender ratio where the document
 * says it should.
 */

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   BTL_POKEPARAM_AddHp                  0x021D5FA4  thumb  ov093
 *   BTL_POKEPARAM_GetItem                0x021D5B40  thumb  ov093
 *   BTL_POKEPARAM_GetMon                 0x021D5844  thumb  ov093
 *   BTL_POKEPARAM_GetSpecies             0x021D5444  thumb  ov093
 *   BTL_POKEPARAM_GetTypes               0x021D57A8  thumb  ov093
 *   BTL_POKEPARAM_GetValue               0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_SetItem                0x021D5B44  thumb  ov093
 *   BTL_POKEPARAM_SetStat                0x021D5970  thumb  ov093
 *   BTL_POKEPARAM_SubHp                  0x021D5F90  thumb  ov093
 *   BTL_POKEPARAM_ZeroHp                 0x021D5FB8  thumb  ov093
 *   BTL_SIDE_GetPokeAtPos                0x021B98AC  thumb  ov093
 *   FIELD_PLAYER_GetPos                  0x021A3038  thumb  ov021
 */

#include "types.h"
#include "mod_api.h"
#include "mod_state.h"
#include "bw_limits.h"
#include "mod_context.h"
#include "overlay.h"

#include <string.h>

/* The heap the bag, item and withdraw calls take: the game's main heap,
 * the one GAMESYSTEM_ProcInit creates as heap 3 (src/game/arm9/
 * gamesystem.c; see state_mon_take_item). */
#define BW_STATE_BAG_HEAP 3u

/* ------------------------------------------------------------------ */
/* The port's own seam                                                 */
/* ------------------------------------------------------------------ */

/*
 * The table the host hands to a mod, and the calls the port itself makes
 * on it.  These are the port's own functions: a mod never sees them (it
 * sees the table through BwModHost, sdk/mod_api.h), and the port's other
 * files declare the ones they use.
 */
extern const BwModState bw_mod_state;

const BwModState *bw_mod_state_table(void);
void bw_mod_state_set_game_data(void *game_data);
void bw_mod_state_set_battle(void *battle_main);
void bw_mod_state_set_field_player(void *field_player);
void bw_mod_state_reset(void);
void bw_mod_state_apply_pending(void);
void bw_mod_state_drop_moves(void);
u32  bw_mod_state_pending(void);

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
struct Party;
struct PokemonParam;
struct BoxPokemon;
struct PersonalData;
struct MyItem;
struct MiscSave;
struct ZukanSave;
struct EventWork;
struct Config;
struct PlayerWork;
struct MyStatus;
struct SaveControl;
struct FieldPlayer;
struct BtlPokeParam;
struct BtlMain;

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

/* ------------------------------------------------------------------ */
/* The game's accessors                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; see the
 * file header.
 */

/* The party: src/game/arm9/game_data.c and src/game/arm9/pokeparty.c. */
extern void *GAME_DATA_GetMyParty(const struct GameData *gd)
    __attribute__((weak));                                    /* 0x02012934 */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    __attribute__((weak));                                    /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    __attribute__((weak));                                    /* 0x0201AB28 */
extern u32 POKEPARTY_RemoveMember(struct Party *party, s32 pos)
    __attribute__((weak));                                    /* 0x0201A9C8 */

/* A Pokemon's own fields: src/game/arm9/poke_tool.c. */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId,
                                   u32 arg)
    __attribute__((weak));                                    /* 0x02017E1C */
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId,
                                    u32 value)
    __attribute__((weak));                                    /* 0x02017E40 */
extern u32 POKE_TOOL_GetBoxValue(struct BoxPokemon *box, u32 paramId, u32 arg)
    __attribute__((weak));                                    /* 0x02017E88 */
extern void POKE_TOOL_SetBoxValue(struct BoxPokemon *box, u32 paramId,
                                  u32 value)
    __attribute__((weak));                                    /* 0x02017E64 */
extern struct BoxPokemon *POKE_TOOL_CastToBoxParam(struct PokemonParam *mon)
    __attribute__((weak));                                    /* 0x020185F0 */
extern u32 POKE_TOOL_IsShiny(struct PokemonParam *pp)
    __attribute__((weak));                                    /* 0x02017EAC */
extern u32 POKE_TOOL_IsShinyOtIdPersonality(u32 otId, u32 personality)
    __attribute__((weak));                                    /* 0x02017ED8 */
extern struct PersonalData *POKE_TOOL_GetPersonalData(u32 species, u16 form)
    __attribute__((weak));                                    /* 0x02019C54 */

/* The player's own status: src/game/arm9/mystatus.c and game_data.c. */
extern struct MyStatus *GAME_DATA_GetMyStatus(struct GameData *gd)
    __attribute__((weak));                                    /* 0x02012944 */
extern u32 MYSTATUS_GetId(const struct MyStatus *mystatus)
    __attribute__((weak));                                    /* 0x02008530 */

/* The species data: src/game/arm9/poke_personal.c. */
extern u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field)
    __attribute__((weak));                                    /* 0x0201AE1C */

/* The boxes: src/game/arm9/box_savedata.c. */
extern struct BoxPokemon *BOX_SAVEDATA_GetPokemon(struct GameData *gameData,
                                                   u32 box, u32 slot)
    __attribute__((weak));                                    /* 0x02007A18 */
extern int BOX_SAVEDATA_AddPokemonToBox(struct GameData *gameData, u32 box,
                                         const struct BoxPokemon *mon)
    __attribute__((weak));                                    /* 0x0200752C */
extern void BOX_SAVEDATA_ClearPokemon(struct GameData *gameData, u32 box,
                                       u32 slot)
    __attribute__((weak));                                    /* 0x02007664 */
extern int BOX_SAVEDATA_AddPokemon(struct GameData *gameData,
                                   const struct BoxPokemon *mon)
    __attribute__((weak));                                    /* 0x020074D8 */
extern u8 BOX_SAVEDATA_GetUsableBoxCount(struct GameData *gameData)
    __attribute__((weak));                                    /* 0x020076A4 */

/* The battle record: src/game/ov093/btl_pokeparam.c. */
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId)
    __attribute__((weak));                                    /* 0x021D59B4 */
extern void BTL_POKEPARAM_ZeroHp(struct BtlPokeParam *bpp)
    __attribute__((weak));                                    /* 0x021D5FB8 */
extern void BTL_POKEPARAM_SetStat(struct BtlPokeParam *pp, int valueId,
                                  u16 value)
    __attribute__((weak));                                    /* 0x021D5970 */
extern void BTL_POKEPARAM_AddHp(struct BtlPokeParam *bpp, u16 amount)
    __attribute__((weak));                                    /* 0x021D5FA4 */
extern void BTL_POKEPARAM_SubHp(struct BtlPokeParam *bpp, u16 damage)
    __attribute__((weak));                                    /* 0x021D5F90 */
extern void BTL_POKEPARAM_SetItem(struct BtlPokeParam *pp, u16 item)
    __attribute__((weak));                                    /* 0x021D5B44 */
extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp)
    __attribute__((weak));                                    /* 0x021D5B40 */
extern struct PokemonParam *BTL_POKEPARAM_GetMon(struct BtlPokeParam *pp)
    __attribute__((weak));
extern int ov093_BTL_POKEPARAM_GetMajorStatusIndex(struct BtlPokeParam *bpp)
    __attribute__((weak));
extern u16 ov093_BTL_POKEPARAM_GetMoveId(struct BtlPokeParam *pp, int slot)
    __attribute__((weak));
extern u8 ov093_BTL_POKEPARAM_GetMovePPChecked(struct BtlPokeParam *pp, u32 slot)
    __attribute__((weak));
extern u8 ov093_BTL_POKEPARAM_GetMoveCount(struct BtlPokeParam *pp)
    __attribute__((weak));
/* The move data's stat changes: src/game/arm9/waza_tool.c. */
extern u8 WAZA_TOOL_GetRankEffectCount(u16 move) __attribute__((weak));
extern u8 WAZA_TOOL_GetRankEffect(u16 move, u32 index, s32 *value)
    __attribute__((weak));
/* The whiteout after a lost battle (src/game/ov010/scrcmd_network.c) and
 * the game system's running event (src/game/arm9). */
struct GameSystem;
struct GameEvent;
extern struct GameEvent *ov010_SCRCMD_NETWORK_CreateWhiteOutEvent(struct GameSystem *gameSystem)
    __attribute__((weak));
/* The whiteouts the run has seen (sdk/mod_event_battle.c). */
extern u32 bw_debug_whiteouts;
extern void GAMESYSTEM_SetEvent(struct GameSystem *gameSystem, struct GameEvent *event)
    __attribute__((weak));
struct FieldMap;
typedef int (*BwGameEventFunc)(struct GameEvent *event, int *seq, void *work);
extern struct GameEvent *GAME_EVENT_Create(struct GameSystem *gameSystem, struct GameEvent *parent,
                                           BwGameEventFunc func, u32 workSize) __attribute__((weak));
extern void GAME_EVENT_CallEvent(struct GameEvent *parent, struct GameEvent *child) __attribute__((weak));
extern void GAME_EVENT_ChangeEvent(struct GameEvent *now, struct GameEvent *next) __attribute__((weak));
extern struct GameSystem *GAME_EVENT_GetGameSystem(struct GameEvent *event) __attribute__((weak));
extern struct FieldMap *GAMESYSTEM_GetFieldMap(struct GameSystem *gameSystem) __attribute__((weak));
extern struct GameEvent *FIELD_STATUS_CreateFieldCloseEvent(struct GameSystem *gameSystem,
                                                            struct FieldMap *fieldMap) __attribute__((weak));
extern struct GameEvent *GAMESYSTEM_GetEvent(const struct GameSystem *gameSystem)
    __attribute__((weak));
extern void *bw_dsref__GameSystemGlobal __attribute__((weak));
extern void *GAMESYSTEM_GetGameData(struct GameSystem *gameSystem) __attribute__((weak));
extern void bw_mod_export_set_game_data(void *game_data) __attribute__((weak));
extern void *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) __attribute__((weak));
extern struct GameEvent *FIELD_SOUND_SYSTEM_StartFadeOutAndResetTask(struct GameSystem *gameSystem, u16 fadeOutFrames) __attribute__((weak));
/* POKE_TOOL_PARAM_IS_EGG (src/game/arm9/poke_tool_param.h). */
#define BW_PARAM_IS_EGG 0x4Cu
extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp)
    __attribute__((weak));                                    /* 0x021D5444 */
extern void BTL_POKEPARAM_GetTypes(struct BtlPokeParam *pp, u8 *type1,
                                   u8 *type2)
    __attribute__((weak));                                    /* 0x021D57A8 */

/* The battle's sides: src/game/ov093/btl_main.c. */
extern u8 *BTL_SIDE_GetPokeAtPos(struct BtlMain *const *side, u8 position)
    __attribute__((weak));                                    /* 0x021B98AC */
struct BtlSide;
extern void *ov093_BTL_SIDE_GetClientPokeParam(struct BtlSide *side, unsigned int clientId,
                                         unsigned int nth) __attribute__((weak));

/* The dex: src/game/arm9/zukan_save.c. */
extern struct ZukanSave *ZUKAN_SAVE_GetFromGameData(void *gameData)
    __attribute__((weak));                                    /* 0x0200CA1C */
extern u32 ZUKAN_SAVE_IsCaught(struct ZukanSave *pZukan, u16 species)
    __attribute__((weak));                                    /* 0x0200CED4 */

/* The badges and the money: src/game/arm9/beacon.c. */
extern void *GAME_DATA_GetMiscSave(struct GameData *gameData)
    __attribute__((weak));                                    /* 0x02012F08 */
extern u32 MISC_GetBadge(struct MiscSave *beacon, u32 bit)
    __attribute__((weak));                                    /* 0x0200C234 */
extern void MISC_SetBadge(struct MiscSave *beacon, u32 bit)
    __attribute__((weak));                                    /* 0x0200C248 */
extern u32 MISC_CountBadges(struct MiscSave *beacon)
    __attribute__((weak));                                    /* 0x0200C258 */
extern u32 MISC_GetMoney(struct MiscSave *beacon)
    __attribute__((weak));                                    /* 0x0200C274 */
extern u32 MISC_AddMoney(struct MiscSave *beacon, u32 add)
    __attribute__((weak));                                    /* 0x0200C278 */
extern u32 MISC_SubMoney(struct MiscSave *beacon, u32 sub)
    __attribute__((weak));                                    /* 0x0200C29C */

/* The event flags: src/game/arm9/game_data.c and src/game/arm9/zonedata.c. */
extern u8 *GAME_DATA_GetEventWork(struct GameData *gd)
    __attribute__((weak));                                    /* 0x0201296C */
extern u32 ZONEDATA_CheckEventFlag(struct EventWork *ev, s32 flagId)
    __attribute__((weak));                                    /* 0x020142E8 */
extern void ZONEDATA_SetEventFlag(struct EventWork *ev, int flagId)
    __attribute__((weak));                                    /* 0x02014314 */
extern void ZONEDATA_ResetEventFlag(struct EventWork *ev, int flagId)
    __attribute__((weak));                                    /* 0x0201433C */

/* The battle style: src/game/arm9/system_data.c and config.c. */
extern struct SaveControl *GAME_DATA_GetSaveControl(struct GameData *gd)
    __attribute__((weak));                                    /* 0x02012EBC */
extern struct Config *SYSTEM_DATA_GetConfig(struct SaveControl *save)
    __attribute__((weak));                                    /* 0x0200873C */
extern unsigned int CONFIG_GetBattleStyle(const struct Config *config)
    __attribute__((weak));                                    /* 0x020083C8 */

/* The zone and the player: src/game/arm9/game_data.c, src/game/ov021/. */
extern struct PlayerWork *GAME_DATA_GetMyPlayerWork(struct GameData *gameData)
    __attribute__((weak));                                    /* 0x020127A4 */
extern u16 GAME_DATA_GetPlayerZoneId(const struct PlayerWork *player)
    __attribute__((weak));                                    /* 0x02012AC8 */
extern void FIELD_PLAYER_GetPos(struct FieldPlayer *player,
                                struct BwVecFx32 *out)
    __attribute__((weak));                                    /* 0x021A3038 */

/* The bag: src/game/arm9/game_data.c and src/game/arm9/myitem_savedata.c. */
extern void *GAME_DATA_GetMyItem(const struct GameData *gd)
    __attribute__((weak));                                    /* 0x0201292C */
extern u32 MYITEM_SAVEDATA_AddItem(struct MyItem *bag, u16 itemId,
                                   u16 quantity, u16 heapId)
    __attribute__((weak));                                    /* 0x02007E50 */
/* The zones, the play time, the item and move data: src/game/arm9/
 * zonedata.c, playtime.c, file_util.c and waza_tool.c. */
extern u32 ZONEDATA_GetPlaceNameId(u32 id) __attribute__((weak));
extern struct PlayTime *GAME_DATA_GetPlayTime(struct GameData *gd)
    __attribute__((weak));
extern u16 PLAYTIME_GetHours(const struct PlayTime *playtime)
    __attribute__((weak));
extern u8 PLAYTIME_GetMinutes(const struct PlayTime *playtime)
    __attribute__((weak));
extern u8 PLAYTIME_GetSeconds(const struct PlayTime *playtime)
    __attribute__((weak));
extern u32 FILE_UTIL_GetItemParam(u32 item, u32 sel, u32 heap)
    __attribute__((weak));
extern u32 WAZA_TOOL_GetParam(u32 move, u32 param) __attribute__((weak));
extern void ARC_TOOL_ReadMember(void *dst, u32 archiveID, u32 memberIdx)
    __attribute__((weak));

/* The box and the withdraw: src/game/arm9/box_savedata.c, pokeparty.c and
 * poke_tool.c. */
extern int BOX_SAVEDATA_AddPokemonToBox(struct GameData *gameData, u32 box,
                                        const struct BoxPokemon *mon)
    __attribute__((weak));
extern void BOX_SAVEDATA_ClearPokemon(struct GameData *gameData, u32 box,
                                      u32 slot) __attribute__((weak));
extern int BOX_SAVEDATA_GetPokemonValue(struct GameData *gameData, u32 box,
                                        u32 slot, int field, int arg)
    __attribute__((weak));
extern u32 POKEPARTY_Add(struct Party *party, const struct PokemonParam *member)
    __attribute__((weak));
extern struct PokemonParam *POKE_TOOL_CreateFromBoxParam(struct PokemonParam *src,
                                                         u16 heapId)
    __attribute__((weak));
extern void POKE_TOOL_RecalcParam(struct PokemonParam *pp) __attribute__((weak));
extern void GFL_HEAP_Free(void *memory) __attribute__((weak));

/* The battle in progress is wild (1), a trainer's (0), or none (-1). */
static s32 g_battle_wild = -1;

/* The battle in progress came from encounter.static. */
static int g_battle_static;

/* The opposing trainers of the battle in progress. */
static u16 g_battle_trainers[2];

extern u32 MYITEM_SAVEDATA_GetItemQuantity(struct MyItem *bag, u16 itemId,
                                           u16 heapId)
    __attribute__((weak));

/* ------------------------------------------------------------------ */
/* The live objects                                                    */
/* ------------------------------------------------------------------ */

/*
 * The three objects the game's accessors take and that only the game's
 * own code can name:
 *
 *   - the game data: the heap object the game creates in
 *     GAMESYSTEM_ProcInit (src/game/arm9/gamesystem.c) and publishes
 *     through GAMESYSTEM_GetGameData.  The game system itself is a
 *     static of gamesystem.c that no other translation unit can name, so
 *     the game data cannot be reached from outside the game.
 *   - the battle's main object: the battle task's work, which holds the
 *     two sides and their battle records.
 *   - the field player: the player of the field map.
 *
 * The port binds them when it learns them (the save.load slot hands it
 * the game data, the battle.start slot the battle), and the state calls
 * work on what is bound.  A call with nothing bound reports a failure,
 * and no call reaches an accessor before the binding is there: the binding
 * is how the state layer knows that the game is there at all.
 */
static void *g_game_data;
static void *g_battle_main;
static void *g_field_player;

/* Bind the game's game data (a struct GameData *). */
void bw_mod_state_set_game_data(void *game_data)
{
    g_game_data = game_data;
}

/* The game data bound, or NULL (multiplayer saves a joiner's game). */
void *bw_mod_state_game_data(void)
{
    return g_game_data;
}

/* Bind the battle's main object (a struct BtlMain *), or NULL when the
 * battle is over. */
void bw_mod_state_set_battle(void *battle_main)
{
    g_battle_main = battle_main;
    if (battle_main == NULL) {
        g_battle_wild = -1;
        g_battle_static = 0;
        g_battle_trainers[0] = 0u;
        g_battle_trainers[1] = 0u;
    }
}

void bw_mod_state_set_battle_wild(s32 wild)
{
    g_battle_wild = wild;
}

void bw_mod_state_set_battle_trainers(u16 first, u16 second)
{
    g_battle_trainers[0] = first;
    g_battle_trainers[1] = second;
}

void bw_mod_state_set_battle_static(int is_static)
{
    g_battle_static = is_static != 0;
}

int bw_mod_state_battle_static(void)
{
    return g_battle_static;
}

/* Bind the field player (a struct FieldPlayer *). */
void bw_mod_state_set_field_player(void *field_player)
{
    g_field_player = field_player;
}

/* A whiteout waiting for a quiet field frame (state_whiteout). */
static int g_whiteout_pending;
static int g_whiteout_wait;

/* Forget every binding and every queued move: at power-on and at every
 * reset (bw_port_reset). */
void bw_mod_state_reset(void)
{
    g_game_data = NULL;
    g_battle_main = NULL;
    g_battle_wild = -1;
    g_field_player = NULL;
    g_whiteout_pending = 0;
    g_whiteout_wait = 0;
    bw_mod_state_drop_moves();
}

/* ------------------------------------------------------------------ */
/* The handles                                                         */
/* ------------------------------------------------------------------ */

/*
 * A reference's handle is the record's index inside its own place: the
 * place, the side in a battle, the box and the slot, one byte each.
 *
 * It is not a pointer.  The ABI carries it in a u32 and the port is built
 * at both pointer widths, so on a 64-bit host a pointer to a game record
 * does not fit in it; a token does, and the record is found again through
 * the reference's own place, side, box and slot, which say the same
 * thing.  A reference whose handle was filled by something other than
 * this file therefore still works.
 */
static u32 mon_handle(u8 place, u8 side, u8 box, u8 slot)
{
    return ((u32)place << 24) | ((u32)side << 16) | ((u32)box << 8) | (u32)slot;
}

/* ------------------------------------------------------------------ */
/* The live objects, reached through the game's accessors              */
/* ------------------------------------------------------------------ */

/* The game's game data, or NULL. */
static struct GameData *live_game_data(void)
{
    return (struct GameData *)g_game_data;
}

/* The party, or NULL. */
static struct Party *live_party(void)
{
    if (GAME_DATA_GetMyParty == NULL || g_game_data == NULL) {
        return NULL;
    }
    return (struct Party *)GAME_DATA_GetMyParty(live_game_data());
}

/* A party member, or NULL when the slot is empty or past the party. */
static struct PokemonParam *live_member(u32 slot)
{
    struct Party *party = live_party();

    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL) {
        return NULL;
    }
    if (slot >= POKEPARTY_GetCount(party)) {
        return NULL;
    }
    return POKEPARTY_GetMember(party, slot);
}

/* A box record, or NULL. */
static struct BoxPokemon *live_box_pokemon(u32 box, u32 slot)
{
    if (BOX_SAVEDATA_GetPokemon == NULL || g_game_data == NULL) {
        return NULL;
    }
    return BOX_SAVEDATA_GetPokemon(live_game_data(), box, slot);
}

/* The badges and the money record, or NULL. */
static struct MiscSave *live_misc_save(void)
{
    if (GAME_DATA_GetMiscSave == NULL || g_game_data == NULL) {
        return NULL;
    }
    return (struct MiscSave *)GAME_DATA_GetMiscSave(live_game_data());
}

/* The event flag block, or NULL. */
static struct EventWork *live_event_work(void)
{
    if (GAME_DATA_GetEventWork == NULL || g_game_data == NULL) {
        return NULL;
    }
    return (struct EventWork *)GAME_DATA_GetEventWork(live_game_data());
}

/* The bag, or NULL. */
static struct MyItem *live_bag(void)
{
    if (GAME_DATA_GetMyItem == NULL || g_game_data == NULL) {
        return NULL;
    }
    return (struct MyItem *)GAME_DATA_GetMyItem(live_game_data());
}

/* ------------------------------------------------------------------ */
/* The battle                                                          */
/* ------------------------------------------------------------------ */

/*
 * The battle's two sides sit in the battle's main object: the first at
 * +0xC8 and each 0xE8 bytes (src/game/ov093/btl_main_types.h).  This is
 * the one place the port mirrors a game offset; the game's own
 * accessors do the rest.
 */
#define BW_BTL_SIDE_OFFSET  0xC8u
#define BW_BTL_SIDE_STRIDE  0xE8u

/* The battle's side, or NULL.  The side's first member is the main
 * object itself, which is what BTL_SIDE_GetPokeAtPos reads. */
static void *live_battle_side(u32 side)
{
    if (g_battle_main == NULL || side > 1u) {
        return NULL;
    }
    return (char *)g_battle_main + BW_BTL_SIDE_OFFSET +
           side * BW_BTL_SIDE_STRIDE;
}

/*
 * The battle record of one member of one side's party, or NULL.
 *
 * Each of the main object's two BtlSide blocks holds every client's party
 * (slots[clientId]); in a single or double battle the player is client 0
 * and the opponent client 1, so the side is the client.  The slot is the
 * member's place in that party (BTL_SIDE_GetClientPokeParam's nth), the
 * same place the battle events name (BTL_MAIN_GetTrainerSlotOfPoke).
 *
 * BTL_SIDE_GetPokeAtPos, used here before, takes a battle position
 * instead (0 the player's front, 1 the opponent's front in a single
 * battle) and ignores the block's side, so every side read the field's
 * front Pokemon and no read reached the bench.
 */
static struct BtlPokeParam *battle_record_in(u32 copy, u32 side, u32 slot)
{
    void *side_block;

    if (ov093_BTL_SIDE_GetClientPokeParam == NULL || slot > 5u || side > 1u) {
        return NULL;
    }
    side_block = live_battle_side(copy);
    if (side_block == NULL) {
        return NULL;
    }
    return (struct BtlPokeParam *)ov093_BTL_SIDE_GetClientPokeParam(
        (struct BtlSide *)side_block, side, slot);
}

/*
 * The two blocks are two copies of the same Pokemon (src/game/ov093/
 * btl_main.c, the battle's setup): the server works on sides[1], and the
 * clients (the player's screen and the AI) on sides[0], which the server's
 * messages keep in step.  A read takes the server's copy, which is current
 * even inside the server's own steps (where the battle events fire); a
 * write goes to both, so the server acts on it (a Pokemon at 0 HP counts
 * as fainted for the battle's end) and the screen shows it.
 */
#define BW_BTL_SERVER_COPY 1u
#define BW_BTL_CLIENT_COPY 0u

static struct BtlPokeParam *live_battle_record(u32 side, u32 slot)
{
    return battle_record_in(BW_BTL_SERVER_COPY, side, slot);
}

/*
 * The battle record's own field ids, which BTL_POKEPARAM_GetValue and
 * BTL_POKEPARAM_SetStat take (src/game/ov093/btl_pokeparam.c): 8
 * attack, 9 defence, 10 special attack, 11 special defence, 12 speed,
 * 13 hp, 14 max hp, 15 level, 16 ability, 18 gender, 19 form, 20 exp.
 *
 * The table's field ids are the party's own (sdk/mod_state.h), so the
 * port translates between the two.
 */
static int battle_value_id(u32 field)
{
    switch (field) {
    case BW_MON_FIELD_HP:
        return 13;
    case BW_MON_FIELD_MAX_HP:
        return 14;
    case BW_MON_FIELD_LEVEL:
        return 15;
    case BW_MON_FIELD_ATK:
        return 8;
    case BW_MON_FIELD_DEF:
        return 9;
    case BW_MON_FIELD_SPEED:
        return 12;
    case BW_MON_FIELD_SPATK:
        return 10;
    case BW_MON_FIELD_SPDEF:
        return 11;
    case BW_MON_FIELD_EXP:
        return 20;
    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* The party                                                           */
/* ------------------------------------------------------------------ */

static u32 state_party_count(void)
{
    struct Party *party = live_party();

    if (party == NULL || POKEPARTY_GetCount == NULL) {
        return 0;
    }
    return POKEPARTY_GetCount(party);
}

static int state_party_ref(u32 slot, BwMonRef *out)
{
    if (out == NULL || live_member(slot) == NULL) {
        return -1;
    }
    out->handle = mon_handle((u8)BW_MON_PARTY, 0, 0, (u8)slot);
    out->place = (u8)BW_MON_PARTY;
    out->side = 0;
    out->box = 0;
    out->slot = (u8)slot;
    return 0;
}

static int state_party_get(BwMonRef mon, u32 field, u32 *out)
{
    struct PokemonParam *member;

    if (out == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return -1;
    }
    member = live_member(mon.slot);
    if (member == NULL) {
        return -1;
    }
    *out = POKE_TOOL_GetPartyValue(member, field, 0);
    return 0;
}

static int state_party_set(BwMonRef mon, u32 field, u32 value)
{
    struct PokemonParam *member;

    if (POKE_TOOL_SetPartyValue == NULL) {
        return -1;
    }
    member = live_member(mon.slot);
    if (member == NULL) {
        return -1;
    }
    POKE_TOOL_SetPartyValue(member, field, value);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The boxes                                                           */
/* ------------------------------------------------------------------ */

static u32 state_box_count(void)
{
    if (BOX_SAVEDATA_GetUsableBoxCount == NULL || g_game_data == NULL) {
        return 0;
    }
    return BOX_SAVEDATA_GetUsableBoxCount(live_game_data());
}

static int state_box_ref(u32 box, u32 slot, BwMonRef *out)
{
    struct BoxPokemon *mon;

    if (out == NULL || POKE_TOOL_GetBoxValue == NULL) {
        return -1;
    }
    mon = live_box_pokemon(box, slot);
    if (mon == NULL) {
        return -1;
    }
    /* An empty slot holds a cleared record: its species is 0. */
    if (POKE_TOOL_GetBoxValue(mon, BW_MON_FIELD_SPECIES, 0) == 0u) {
        return -1;
    }
    out->handle = mon_handle((u8)BW_MON_BOX, 0, (u8)box, (u8)slot);
    out->place = (u8)BW_MON_BOX;
    out->side = 0;
    out->box = (u8)box;
    out->slot = (u8)slot;
    return 0;
}

static int state_box_get(BwMonRef mon, u32 field, u32 *out)
{
    struct BoxPokemon *box_mon;

    if (out == NULL || POKE_TOOL_GetBoxValue == NULL) {
        return -1;
    }
    box_mon = live_box_pokemon(mon.box, mon.slot);
    if (box_mon == NULL) {
        return -1;
    }
    *out = POKE_TOOL_GetBoxValue(box_mon, field, 0);
    return 0;
}

static int state_box_set(BwMonRef mon, u32 field, u32 value)
{
    struct BoxPokemon *box_mon;

    if (POKE_TOOL_SetBoxValue == NULL) {
        return -1;
    }
    box_mon = live_box_pokemon(mon.box, mon.slot);
    if (box_mon == NULL) {
        return -1;
    }
    POKE_TOOL_SetBoxValue(box_mon, field, value);
    return 0;
}

/* ------------------------------------------------------------------ */
/* A Pokemon in battle                                                 */
/* ------------------------------------------------------------------ */

static int state_battle_ref(u32 side, u32 slot, BwMonRef *out)
{
    if (out == NULL || live_battle_record(side, slot) == NULL) {
        return -1;
    }
    out->handle = mon_handle((u8)BW_MON_BATTLE, (u8)side, 0, (u8)slot);
    out->place = (u8)BW_MON_BATTLE;
    out->side = (u8)side;
    out->box = 0;
    out->slot = (u8)slot;
    return 0;
}

static int state_battle_get(BwMonRef mon, u32 field, u32 *out)
{
    struct BtlPokeParam *record;
    int id;

    if (out == NULL) {
        return -1;
    }
    record = live_battle_record(mon.side, mon.slot);
    if (record == NULL) {
        return -1;
    }
    switch (field) {
    case BW_MON_FIELD_SPECIES:
        if (BTL_POKEPARAM_GetSpecies == NULL) {
            return -1;
        }
        *out = BTL_POKEPARAM_GetSpecies(record);
        return 0;
    case BW_MON_FIELD_ITEM:
        if (BTL_POKEPARAM_GetItem == NULL) {
            return -1;
        }
        *out = BTL_POKEPARAM_GetItem(record);
        return 0;
    case BW_MON_FIELD_TYPE1:
    case BW_MON_FIELD_TYPE2: {
        u8 type1 = 0;
        u8 type2 = 0;

        if (BTL_POKEPARAM_GetTypes == NULL) {
            return -1;
        }
        BTL_POKEPARAM_GetTypes(record, &type1, &type2);
        *out = (field == BW_MON_FIELD_TYPE1) ? (u32)type1 : (u32)type2;
        return 0;
    }
    case BW_MON_FIELD_STAGE_ATK:
    case BW_MON_FIELD_STAGE_DEF:
    case BW_MON_FIELD_STAGE_SPATK:
    case BW_MON_FIELD_STAGE_SPDEF:
    case BW_MON_FIELD_STAGE_SPEED:
    case BW_MON_FIELD_STAGE_ACCURACY:
    case BW_MON_FIELD_STAGE_EVASION:
        /* the game's value ids 1 to 7 are the stages, 6 unchanged */
        if (BTL_POKEPARAM_GetValue == NULL) {
            return -1;
        }
        *out = (u32)(u8)BTL_POKEPARAM_GetValue(record,
                                               (int)(field - BW_MON_FIELD_STAGE_ATK + 1u));
        return 0;
    case BW_MON_FIELD_STATUS:
        if (ov093_BTL_POKEPARAM_GetMajorStatusIndex == NULL) {
            return -1;
        }
        *out = (u32)ov093_BTL_POKEPARAM_GetMajorStatusIndex(record);
        return 0;
    case BW_MON_FIELD_BATTLE_ABILITY:
        if (BTL_POKEPARAM_GetValue == NULL) {
            return -1;
        }
        *out = (u32)BTL_POKEPARAM_GetValue(record, 16);
        return 0;
    case BW_MON_FIELD_MOVE1:
    case BW_MON_FIELD_MOVE2:
    case BW_MON_FIELD_MOVE3:
    case BW_MON_FIELD_MOVE4: {
        /* the battle's own moves: a slot past its count holds none */
        u32 slot = field - BW_MON_FIELD_MOVE1;

        if (ov093_BTL_POKEPARAM_GetMoveId == NULL || ov093_BTL_POKEPARAM_GetMoveCount == NULL) {
            return -1;
        }
        *out = slot < ov093_BTL_POKEPARAM_GetMoveCount(record)
             ? (u32)ov093_BTL_POKEPARAM_GetMoveId(record, (int)slot) : 0u;
        return 0;
    }
    case BW_MON_FIELD_PP1:
    case BW_MON_FIELD_PP2:
    case BW_MON_FIELD_PP3:
    case BW_MON_FIELD_PP4: {
        u32 slot = field - BW_MON_FIELD_PP1;

        if (ov093_BTL_POKEPARAM_GetMovePPChecked == NULL || ov093_BTL_POKEPARAM_GetMoveCount == NULL) {
            return -1;
        }
        *out = slot < ov093_BTL_POKEPARAM_GetMoveCount(record)
             ? (u32)ov093_BTL_POKEPARAM_GetMovePPChecked(record, slot) : 0u;
        return 0;
    }
    default:
        break;
    }
    id = battle_value_id(field);
    if (id < 0) {
        /* The battle keeps no value of its own for this id: the party
         * record the battle copied holds it (the personality, the trainer
         * id, the nature and the like do not change in a battle). */
        struct PokemonParam *source;

        if (BTL_POKEPARAM_GetMon == NULL || POKE_TOOL_GetPartyValue == NULL) {
            return -1;
        }
        source = BTL_POKEPARAM_GetMon(record);
        if (source == NULL) {
            return -1;
        }
        *out = POKE_TOOL_GetPartyValue(source, field, 0);
        return 0;
    }
    if (BTL_POKEPARAM_GetValue == NULL) {
        return -1;
    }
    *out = (u32)BTL_POKEPARAM_GetValue(record, id);
    return 0;
}

static int set_battle_record(struct BtlPokeParam *record, u32 field, u32 value);

static int state_battle_set(BwMonRef mon, u32 field, u32 value)
{
    struct BtlPokeParam *server = battle_record_in(BW_BTL_SERVER_COPY, mon.side, mon.slot);
    struct BtlPokeParam *client = NULL;
    int rc;
    u32 i;

    if (server == NULL) {
        return -1;
    }
    /* the client's copy of the same Pokemon: both copies were built from
     * the same party record (BTL_POKEPARAM_GetMon), and the two orders can
     * differ for a moment after a switch */
    if (BTL_POKEPARAM_GetMon != NULL) {
        for (i = 0; i < 6u; i++) {
            struct BtlPokeParam *c = battle_record_in(BW_BTL_CLIENT_COPY, mon.side, i);

            if (c != NULL && c != server && BTL_POKEPARAM_GetMon(c) == BTL_POKEPARAM_GetMon(server)) {
                client = c;
                break;
            }
        }
    }
    rc = set_battle_record(server, field, value);
    if (rc == 0 && client != NULL) {
        (void)set_battle_record(client, field, value);
    }
    return rc;
}

static int set_battle_record(struct BtlPokeParam *record, u32 field, u32 value)
{
    int id;

    if (field == BW_MON_FIELD_ITEM) {
        if (BTL_POKEPARAM_SetItem == NULL) {
            return -1;
        }
        BTL_POKEPARAM_SetItem(record, (u16)value);
        return 0;
    }
    id = battle_value_id(field);
    if (id < 0) {
        return -1;
    }
    if (field == BW_MON_FIELD_HP) {
        /*
         * The battle record's hp is a plain field the game changes with
         * BTL_POKEPARAM_AddHp, BTL_POKEPARAM_SubHp and
         * BTL_POKEPARAM_ZeroHp; there is no setter that takes a value.
         */
        u32 current;

        if (BTL_POKEPARAM_GetValue == NULL) {
            return -1;
        }
        current = (u32)BTL_POKEPARAM_GetValue(record, id);
        if (value == 0u) {
            if (BTL_POKEPARAM_ZeroHp == NULL) {
                return -1;
            }
            BTL_POKEPARAM_ZeroHp(record);
            return 0;
        }
        if (value > current) {
            if (BTL_POKEPARAM_AddHp == NULL) {
                return -1;
            }
            BTL_POKEPARAM_AddHp(record, (u16)(value - current));
        } else if (value < current) {
            if (BTL_POKEPARAM_SubHp == NULL) {
                return -1;
            }
            BTL_POKEPARAM_SubHp(record, (u16)(current - value));
        }
        return 0;
    }
    if (BTL_POKEPARAM_SetStat == NULL) {
        return -1;
    }
    BTL_POKEPARAM_SetStat(record, id, (u16)value);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The species data                                                    */
/* ------------------------------------------------------------------ */

/*
 * The personal record's field ids are POKE_PERSONAL_GetParam's own
 * (src/game/arm9/poke_personal.c): 0 base hp, 1 base attack, 2 base
 * defence, 3 base speed, 4 base special attack, 5 base special defence,
 * 6 type 1, 7 type 2, 8 catch rate, 9 base experience, 20 the gender
 * ratio, 26 ability 1, 27 ability 2, 28 the hidden ability, 37 height,
 * 38 weight.  The species call passes the field straight through, so a
 * mod asks for the same field the game asks for.
 */

static int state_species_value(u16 species, u32 field, u32 *out)
{
    const struct PersonalData *data;

    if (out == NULL || POKE_TOOL_GetPersonalData == NULL ||
        POKE_PERSONAL_GetParam == NULL || g_game_data == NULL) {
        return -1;
    }
    data = POKE_TOOL_GetPersonalData((u32)species, 0);
    if (data == NULL) {
        return -1;
    }
    *out = POKE_PERSONAL_GetParam(data, field);
    return 0;
}

static int state_species_shiny(u32 personality, u16 species)
{
    struct MyStatus *status;
    u32 ot_id;

    (void)species;

    /*
     * The game's test (POKE_TOOL_IsShiny, 0x02017EAC, and the rule it
     * applies, POKE_TOOL_IsShinyOtIdPersonality, 0x02017ED8) mixes the
     * personality with the original trainer's id, which the table's call
     * does not carry; the species does not enter the test at all.  The
     * port answers with the player's own trainer id, which is the id the
     * game stamps on the Pokemon the player creates (src/game/arm9/
     * scrcmd_proc.c gives POKE_TOOL_CreateEx the trainer's id), and says
     * so.
     */
    if (POKE_TOOL_IsShinyOtIdPersonality == NULL ||
        GAME_DATA_GetMyStatus == NULL || MYSTATUS_GetId == NULL ||
        g_game_data == NULL) {
        return -1;
    }
    status = GAME_DATA_GetMyStatus(live_game_data());
    if (status == NULL) {
        return -1;
    }
    ot_id = MYSTATUS_GetId(status);
    return POKE_TOOL_IsShinyOtIdPersonality(ot_id, personality) != 0u ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The dex                                                             */
/* ------------------------------------------------------------------ */

static int state_dex_caught(u16 species)
{
    struct ZukanSave *dex;

    if (ZUKAN_SAVE_GetFromGameData == NULL || ZUKAN_SAVE_IsCaught == NULL ||
        g_game_data == NULL) {
        return -1;
    }
    dex = ZUKAN_SAVE_GetFromGameData(g_game_data);
    if (dex == NULL) {
        return -1;
    }
    return ZUKAN_SAVE_IsCaught(dex, species) != 0u ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The badges, the money and the flags                                */
/* ------------------------------------------------------------------ */

static u32 state_badge_count(void)
{
    struct MiscSave *misc = live_misc_save();

    if (misc == NULL || MISC_CountBadges == NULL) {
        return 0;
    }
    return MISC_CountBadges(misc);
}

static int state_badge_has(u32 badge)
{
    struct MiscSave *misc = live_misc_save();

    if (misc == NULL || MISC_GetBadge == NULL) {
        return -1;
    }
    return MISC_GetBadge(misc, badge) != 0u ? 1 : 0;
}

static u32 state_money(void)
{
    struct MiscSave *misc = live_misc_save();

    if (misc == NULL || MISC_GetMoney == NULL) {
        return 0;
    }
    return MISC_GetMoney(misc);
}

static int state_money_set(u32 amount)
{
    struct MiscSave *misc = live_misc_save();
    u32 current;

    if (misc == NULL || MISC_GetMoney == NULL || MISC_AddMoney == NULL ||
        MISC_SubMoney == NULL) {
        return -1;
    }
    current = MISC_GetMoney(misc);
    if (amount > current) {
        MISC_AddMoney(misc, amount - current);
    } else if (amount < current) {
        MISC_SubMoney(misc, current - amount);
    }
    /* The game clamps the money to 0 and 9999999; a target outside that
     * range is a failure, not a different amount. */
    return MISC_GetMoney(misc) == amount ? 0 : -1;
}

static int state_flag(u32 flag, u32 *out)
{
    struct EventWork *work = live_event_work();

    if (out == NULL || ZONEDATA_CheckEventFlag == NULL) {
        return -1;
    }
    if (work == NULL) {
        return -1;
    }
    *out = ZONEDATA_CheckEventFlag(work, (s32)flag);
    return 0;
}

static int state_flag_set(u32 flag, u32 value)
{
    struct EventWork *work = live_event_work();

    if (work == NULL) {
        return -1;
    }
    if (value != 0u) {
        if (ZONEDATA_SetEventFlag == NULL) {
            return -1;
        }
        ZONEDATA_SetEventFlag(work, (int)flag);
    } else {
        if (ZONEDATA_ResetEventFlag == NULL) {
            return -1;
        }
        ZONEDATA_ResetEventFlag(work, (int)flag);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The map and the player                                              */
/* ------------------------------------------------------------------ */

static u16 state_zone(void)
{
    struct PlayerWork *player;

    if (GAME_DATA_GetMyPlayerWork == NULL ||
        GAME_DATA_GetPlayerZoneId == NULL || g_game_data == NULL) {
        return 0;
    }
    player = GAME_DATA_GetMyPlayerWork(live_game_data());
    if (player == NULL) {
        return 0;
    }
    return GAME_DATA_GetPlayerZoneId(player);
}

static int state_player_pos(s16 *x, s16 *y, s16 *z)
{
    struct BwVecFx32 pos;

    if (x == NULL || y == NULL || z == NULL || FIELD_PLAYER_GetPos == NULL ||
        g_field_player == NULL) {
        return -1;
    }
    pos.x = 0;
    pos.y = 0;
    pos.z = 0;
    FIELD_PLAYER_GetPos((struct FieldPlayer *)g_field_player, &pos);
    /*
     * The cell the way the game itself converts it (src/game/ov021/
     * field_player_core.c, FIELD_PLAYER_CORE_SetPos): (pos >> 4) / 4096.
     */
    *x = (s16)(((s32)pos.x >> 4) / 4096);
    *y = (s16)(((s32)pos.y >> 4) / 4096);
    *z = (s16)(((s32)pos.z >> 4) / 4096);
    return 0;
}

static u8 state_battle_style(void)
{
    struct SaveControl *save;
    struct Config *config;

    if (GAME_DATA_GetSaveControl == NULL || SYSTEM_DATA_GetConfig == NULL ||
        CONFIG_GetBattleStyle == NULL || g_game_data == NULL) {
        return 0;
    }
    save = GAME_DATA_GetSaveControl(live_game_data());
    if (save == NULL) {
        return 0;
    }
    config = SYSTEM_DATA_GetConfig(save);
    if (config == NULL) {
        return 0;
    }
    return (u8)CONFIG_GetBattleStyle(config);
}

/* ------------------------------------------------------------------ */
/* The moves between places                                            */
/* ------------------------------------------------------------------ */

/*
 * The two moves change the party, which a battle, the party screen and
 * the PC hold open, so the port queues them and applies them when none
 * of those runs (docs/mod-events.md, section 4).  The queue is here; the
 * quiet moment is the port's: it calls bw_mod_state_apply_pending at a
 * moment it knows is quiet.  The one thing the queue itself knows is the
 * battle, and it refuses to move a Pokemon while a battle is bound.
 *
 * A queued move names the party slot it was made for and the Pokemon's
 * personality, so a party that changed under it does not lose the wrong
 * Pokemon.
 */

#define BW_MOD_MOVE_QUEUE_MAX 16

/* The move kinds. */
#define BW_MOVE_TO_BOX    0u
#define BW_MOVE_TAKE_ITEM 1u
#define BW_MOVE_RELEASE   2u      /* found by personality, not by slot */
#define BW_MOVE_TO_BOX_N  3u      /* to the box the move names */
#define BW_MOVE_FROM_BOX  4u      /* from (box, box_slot) to the party */

typedef struct BwModMove {
    u8  kind;
    u8  slot;         /* the party slot */
    u8  box;          /* BW_MOVE_TO_BOX_N, BW_MOVE_FROM_BOX: the box */
    u8  box_slot;     /* BW_MOVE_FROM_BOX: the slot in it */
    u32 personality;  /* the Pokemon's personality when it was queued */
} BwModMove;

static BwModMove g_moves[BW_MOD_MOVE_QUEUE_MAX];
static u32        g_move_count;

void bw_mod_state_drop_moves(void)
{
    g_move_count = 0;
}

/* How many moves are waiting. */
u32 bw_mod_state_pending(void)
{
    return g_move_count;
}

/* Queue a move.  Returns 0, or -1 when the queue is full. */
static int queue_move(u8 kind, u32 slot)
{
    struct PokemonParam *member;
    BwModMove *move;

    if (g_move_count >= BW_MOD_MOVE_QUEUE_MAX) {
        return -1;
    }
    member = live_member(slot);
    if (member == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return -1;
    }
    move = &g_moves[g_move_count];
    move->kind = kind;
    move->slot = (u8)slot;
    move->personality = POKE_TOOL_GetPartyValue(member, 0x00u, 0);
    g_move_count++;
    return 0;
}

/* Release the party Pokemon with this personality, unless it is the last
 * one that is not an egg. */
static int release_by_personality(u32 personality)
{
    struct Party *party = live_party();
    u32 count, i, found = 0xFFFFFFFFu, others = 0;

    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_RemoveMember == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return -1;
    }
    count = POKEPARTY_GetCount(party);
    for (i = 0; i < count; i++) {
        struct PokemonParam *member = live_member(i);

        if (member == NULL) {
            continue;
        }
        if (found == 0xFFFFFFFFu &&
            POKE_TOOL_GetPartyValue(member, 0x00u, 0) == personality) {
            found = i;
        } else if (POKE_TOOL_GetPartyValue(member, BW_PARAM_IS_EGG, 0) == 0u) {
            others++;
        }
    }
    if (found == 0xFFFFFFFFu || others == 0u) {
        return -1;      /* gone already, or the last one: refused */
    }
    POKEPARTY_RemoveMember(party, (s32)found);
    return 0;
}

/* Move a party Pokemon to the first free box slot. */
static int move_to_box(u32 slot)
{
    struct Party *party = live_party();
    struct PokemonParam *member;
    struct BoxPokemon *box_mon;

    if (party == NULL || BOX_SAVEDATA_AddPokemon == NULL ||
        POKEPARTY_RemoveMember == NULL || POKE_TOOL_CastToBoxParam == NULL ||
        g_game_data == NULL) {
        return -1;
    }
    member = live_member(slot);
    if (member == NULL) {
        return -1;
    }
    box_mon = POKE_TOOL_CastToBoxParam(member);
    if (box_mon == NULL) {
        return -1;
    }
    /* BOX_SAVEDATA_AddPokemon puts the Pokemon in the first free slot,
     * starting at the current box, and unlocks more boxes when it has
     * to; it answers 0 when every box is full. */
    if (BOX_SAVEDATA_AddPokemon(live_game_data(), box_mon) == 0) {
        return -1;
    }
    POKEPARTY_RemoveMember(party, (s32)slot);
    return 0;
}

/* How many party Pokemon are not eggs, leaving out `skip` (-1 for none). */
static u32 party_non_eggs(s32 skip)
{
    struct Party *party = live_party();
    u32 count, i, n = 0;

    if (party == NULL || POKEPARTY_GetCount == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0;
    }
    count = POKEPARTY_GetCount(party);
    for (i = 0; i < count; i++) {
        struct PokemonParam *member = live_member(i);

        if ((s32)i == skip || member == NULL) {
            continue;
        }
        if (POKE_TOOL_GetPartyValue(member, BW_PARAM_IS_EGG, 0) == 0u) {
            n++;
        }
    }
    return n;
}

/* Move a party Pokemon to the first free slot of one box. */
static int move_to_box_n(u32 slot, u32 box)
{
    struct Party *party = live_party();
    struct PokemonParam *member;
    struct BoxPokemon *box_mon;

    if (party == NULL || BOX_SAVEDATA_AddPokemonToBox == NULL ||
        POKEPARTY_RemoveMember == NULL || POKE_TOOL_CastToBoxParam == NULL ||
        g_game_data == NULL || party_non_eggs((s32)slot) == 0u) {
        return -1;
    }
    member = live_member(slot);
    if (member == NULL) {
        return -1;
    }
    box_mon = POKE_TOOL_CastToBoxParam(member);
    if (box_mon == NULL ||
        BOX_SAVEDATA_AddPokemonToBox(live_game_data(), box, box_mon) == 0) {
        return -1;      /* the box is full */
    }
    POKEPARTY_RemoveMember(party, (s32)slot);
    return 0;
}

/* Move the Pokemon at (box, slot) to the end of the party. */
static int move_from_box(u32 box, u32 slot, u32 personality)
{
    struct Party *party = live_party();
    struct BoxPokemon *box_mon = live_box_pokemon(box, slot);
    struct PokemonParam *copy;

    if (party == NULL || box_mon == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_Add == NULL || POKE_TOOL_CreateFromBoxParam == NULL ||
        BOX_SAVEDATA_ClearPokemon == NULL || GFL_HEAP_Free == NULL ||
        BOX_SAVEDATA_GetPokemonValue == NULL) {
        return -1;
    }
    if (POKEPARTY_GetCount(party) >= 6u) {
        return -1;      /* the party is full */
    }
    if ((u32)BOX_SAVEDATA_GetPokemonValue(live_game_data(), box, slot, 0x00, 0) !=
        personality) {
        return -1;      /* the slot holds another Pokemon now */
    }
    /* The game's own withdraw: a party record built from the box record
     * (it works out the level and the stats), added to the party, and the
     * box slot cleared.  The record the game built is the caller's to
     * free; the party keeps its own copy. */
    copy = POKE_TOOL_CreateFromBoxParam((struct PokemonParam *)box_mon,
                                        (u16)BW_STATE_BAG_HEAP);
    if (copy == NULL) {
        return -1;
    }
    if (POKEPARTY_Add(party, copy) == 0u) {
        GFL_HEAP_Free(copy);
        return -1;
    }
    GFL_HEAP_Free(copy);
    BOX_SAVEDATA_ClearPokemon(live_game_data(), box, slot);
    return 0;
}

/* Move a party Pokemon's held item back to the bag. */
static int move_take_item(u32 slot)
{
    struct PokemonParam *member;
    struct MyItem *bag;
    u32 item;

    if (POKE_TOOL_GetPartyValue == NULL || POKE_TOOL_SetPartyValue == NULL ||
        MYITEM_SAVEDATA_AddItem == NULL) {
        return -1;
    }
    member = live_member(slot);
    if (member == NULL) {
        return -1;
    }
    item = POKE_TOOL_GetPartyValue(member, BW_MON_FIELD_ITEM, 0);
    if (item == 0u) {
        return -1;      /* it holds nothing */
    }
    bag = live_bag();
    if (bag == NULL) {
        return -1;
    }
    /* The order the document gives: clear the field, then put the item
     * in the bag.  A bag that will not take it gets the item back.
     *
     * The heap is the game's own main heap, the one
     * GAMESYSTEM_ProcInit creates as heap 3 (src/game/arm9/gamesystem.c);
     * the game uses the battle's heap here, and the call only needs a
     * heap for the slot it grows while it works. */
    POKE_TOOL_SetPartyValue(member, BW_MON_FIELD_ITEM, 0);
    if (MYITEM_SAVEDATA_AddItem(bag, (u16)item, 1, 3u) == 0u) {
        POKE_TOOL_SetPartyValue(member, BW_MON_FIELD_ITEM, item);
        return -1;
    }
    return 0;
}

/*
 * Apply the queued moves, in the order the mod made them.  The port
 * calls this at a moment it knows is quiet; the battle is the one thing
 * the queue itself can see, so a battle stops the moves from being
 * applied and they stay queued.
 */
void bw_mod_state_apply_pending(void)
{
    u32 i;

    if (g_battle_main != NULL) {
        return;     /* a battle holds the party open */
    }
    for (i = 0; i < g_move_count; i++) {
        struct PokemonParam *member;
        int done;

        if (g_moves[i].kind == BW_MOVE_RELEASE) {
            (void)release_by_personality(g_moves[i].personality);
            continue;
        }
        if (g_moves[i].kind == BW_MOVE_FROM_BOX) {
            (void)move_from_box(g_moves[i].box, g_moves[i].box_slot,
                                g_moves[i].personality);
            continue;
        }
        member = live_member(g_moves[i].slot);
        if (member == NULL || POKE_TOOL_GetPartyValue == NULL) {
            continue;   /* the slot is gone: the move has nothing to do */
        }
        if (POKE_TOOL_GetPartyValue(member, 0x00u, 0) !=
            g_moves[i].personality) {
            continue;   /* the slot holds another Pokemon now */
        }
        if (g_moves[i].kind == BW_MOVE_TO_BOX) {
            done = move_to_box(g_moves[i].slot);
        } else if (g_moves[i].kind == BW_MOVE_TO_BOX_N) {
            done = move_to_box_n(g_moves[i].slot, g_moves[i].box);
        } else {
            done = move_take_item(g_moves[i].slot);
        }
        if (done != 0) {
            continue;   /* it failed: the move is dropped */
        }
    }
    g_move_count = 0;
}

static int state_mon_to_box(BwMonRef mon)
{
    if (mon.place != (u8)BW_MON_PARTY) {
        return -1;
    }
    return queue_move(BW_MOVE_TO_BOX, mon.slot);
}

static int state_mon_take_item(BwMonRef mon)
{
    if (mon.place != (u8)BW_MON_PARTY) {
        return -1;
    }
    return queue_move(BW_MOVE_TAKE_ITEM, mon.slot);
}

/*
 * A party reference, or one of the player's Pokemon in battle: the battle
 * works on its own copy of the party, whose Pokemon carry the same
 * personality, and switching reorders the battle's copy, not the party.
 */
static int state_mon_release(BwMonRef mon)
{
    struct PokemonParam *member = NULL;
    BwModMove *move;

    if (g_move_count >= BW_MOD_MOVE_QUEUE_MAX || POKE_TOOL_GetPartyValue == NULL) {
        return -1;
    }
    if (mon.place == (u8)BW_MON_PARTY) {
        member = live_member(mon.slot);
    } else if (mon.place == (u8)BW_MON_BATTLE && mon.side == 0u &&
               BTL_POKEPARAM_GetMon != NULL) {
        struct BtlPokeParam *record = live_battle_record(mon.side, mon.slot);

        if (record != NULL) {
            member = BTL_POKEPARAM_GetMon(record);
        }
    }
    if (member == NULL) {
        return -1;
    }
    move = &g_moves[g_move_count];
    move->kind = (u8)BW_MOVE_RELEASE;
    move->slot = mon.slot;
    move->personality = POKE_TOOL_GetPartyValue(member, 0x00u, 0);
    g_move_count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

static int state_mon_to_box_n(BwMonRef mon, u32 box)
{
    if (mon.place != (u8)BW_MON_PARTY || box >= 24u) {
        return -1;
    }
    if (queue_move(BW_MOVE_TO_BOX_N, mon.slot) != 0) {
        return -1;
    }
    g_moves[g_move_count - 1u].box = (u8)box;
    return 0;
}

static int state_mon_from_box(u32 box, u32 slot)
{
    BwModMove *move;

    if (box >= 24u || slot >= 30u || g_move_count >= BW_MOD_MOVE_QUEUE_MAX ||
        live_box_pokemon(box, slot) == NULL || BOX_SAVEDATA_GetPokemonValue == NULL) {
        return -1;
    }
    move = &g_moves[g_move_count];
    memset(move, 0, sizeof(*move));
    move->kind = (u8)BW_MOVE_FROM_BOX;
    move->box = (u8)box;
    move->box_slot = (u8)slot;
    move->personality = (u32)BOX_SAVEDATA_GetPokemonValue(live_game_data(), box,
                                                          slot, 0x00, 0);
    g_move_count++;
    return 0;
}

/*
 * The evolution lines.  The evolution archive (0x13, a/0/1/9) holds one
 * 0x2C byte record per species: seven rows of (method, argument, species
 * it evolves into).  The port reads every record once and keeps, for each
 * species, the species that evolves into it.
 */
#define BW_EVO_ARCHIVE   0x13u
#define BW_EVO_SPECIES   649u     /* the national dex of Black and White */

static u16 g_evo_parent[BW_EVO_SPECIES + 1u];
static int g_evo_ready;

static void evo_build(void)
{
    u16 rows[0x2C / 2];
    u32 s, r;

    memset(g_evo_parent, 0, sizeof g_evo_parent);
    for (s = 1; s <= BW_EVO_SPECIES; s++) {
        memset(rows, 0, sizeof rows);
        ARC_TOOL_ReadMember(rows, BW_EVO_ARCHIVE, s);
        for (r = 0; r < 7u; r++) {
            u16 method = rows[r * 3u];
            u16 target = rows[r * 3u + 2u];

            if (method != 0u && target != 0u && target <= BW_EVO_SPECIES &&
                g_evo_parent[target] == 0u) {
                g_evo_parent[target] = (u16)s;
            }
        }
    }
    g_evo_ready = 1;
}

static s32 state_species_base(u16 species)
{
    u32 steps;

    if (species == 0u || species > BW_EVO_SPECIES || ARC_TOOL_ReadMember == NULL) {
        return -1;
    }
    if (!g_evo_ready) {
        evo_build();
    }
    /* A line is at most three stages; the bound guards a loop in modded
     * data. */
    for (steps = 0; steps < 8u && g_evo_parent[species] != 0u; steps++) {
        species = g_evo_parent[species];
    }
    return (s32)species;
}

static int state_mon_recalc(BwMonRef mon)
{
    struct PokemonParam *member;

    if (mon.place != (u8)BW_MON_PARTY || POKE_TOOL_RecalcParam == NULL) {
        return -1;
    }
    member = live_member(mon.slot);
    if (member == NULL) {
        return -1;
    }
    POKE_TOOL_RecalcParam(member);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The bag                                                             */
/* ------------------------------------------------------------------ */


static s32 state_item_count(u16 item)
{
    struct MyItem *bag = live_bag();

    if (bag == NULL || MYITEM_SAVEDATA_GetItemQuantity == NULL) {
        return -1;
    }
    return (s32)MYITEM_SAVEDATA_GetItemQuantity(bag, item, BW_STATE_BAG_HEAP);
}

/*
 * The bag is save data, not a screen's copy, so the add is not queued:
 * the bag screen reads the pockets again when it opens.  The game's own
 * add takes all of the count or none of it.
 */
static int state_item_add(u16 item, u16 count)
{
    struct MyItem *bag = live_bag();

    if (item == 0u || count == 0u ||
        (u32)item > bw_limit_get(BW_PORT_LIMIT_ITEM_LAST)) {
        return -1;
    }
    if (bag == NULL || MYITEM_SAVEDATA_AddItem == NULL) {
        return -1;
    }
    return MYITEM_SAVEDATA_AddItem(bag, item, count, BW_STATE_BAG_HEAP) != 0u
               ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* The area, the battle kind, the play time, item and move data        */
/* ------------------------------------------------------------------ */

static s32 state_area_of(u16 zone)
{
    if (ZONEDATA_GetPlaceNameId == NULL || g_game_data == NULL) {
        return -1;
    }
    return (s32)ZONEDATA_GetPlaceNameId((u32)zone);
}

static s32 state_battle_wild(void)
{
    return g_battle_wild;
}

static s32 state_battle_trainer(u32 n)
{
    if (g_battle_wild < 0 || n > 1u) {
        return -1;
    }
    return (s32)g_battle_trainers[n];
}

/* ------------------------------------------------------------------ */
/* Version 5: the type chart, the weather, what a species can learn     */
/* ------------------------------------------------------------------ */

/* The game's type chart (src/game/ov093/btl_calc.c), by its linked name:
 * an affinity 0 none, 1 a quarter, 2 half, 3 normal, 4 double, 5 four
 * times; Combine puts two of them together. */
extern int ov093_BTL_CALC_GetTypeAffinity(int atkType, int defType)
    __attribute__((weak));                                    /* 0x021D794C */
extern int ov093_BTL_CALC_CombineTypeAffinity(int a, int b) __attribute__((weak));

#define BW_TYPE_COUNT 17u

static s32 state_type_effect(u32 atk, u32 def1, u32 def2)
{
    static const s32 quarters[6] = { 0, 1, 2, 4, 8, 16 };
    int a;

    /* the chart is the battle overlay's own data: it is there while a
     * battle runs (ov093 loaded), and nowhere else */
    if (ov093_BTL_CALC_GetTypeAffinity == NULL || ov093_BTL_CALC_CombineTypeAffinity == NULL ||
        atk >= BW_TYPE_COUNT || def1 >= BW_TYPE_COUNT || !bw_overlay_is_loaded(93u)) {
        return -1;
    }
    a = ov093_BTL_CALC_GetTypeAffinity((int)atk, (int)def1);
    if (def2 != def1 && def2 < BW_TYPE_COUNT) {
        a = ov093_BTL_CALC_CombineTypeAffinity(a, ov093_BTL_CALC_GetTypeAffinity((int)atk, (int)def2));
    }
    return (a >= 0 && a <= 5) ? quarters[a] : -1;
}

static s32 state_battle_weather(void)
{
    if (g_battle_wild < 0) {
        return -1;
    }
    return (s32)bw_mod_battle_context.weather;
}

/* The game's own learning data (src/game/arm9/poke_tool.c, file_util.c,
 * ov021 sodateya.c): the level-up table, the TM and HM machines and the egg
 * moves (archive 0x7B: a count, then the moves). */
extern u32 POKE_TOOL_CheckSpeciesTmCompat(u16 species, u16 form, s32 tmIndex)
    __attribute__((weak));
extern u16 FILE_UTIL_GetMachineMove(u16 machineId) __attribute__((weak)); /* 0x020212B0 */
extern u32 FILE_UTIL_GetId(u32 machineId) __attribute__((weak));
extern void *ARC_TOOL_LoadMember(u32 archiveID, u32 memberIdx, u16 heapID)
    __attribute__((weak));
extern void POKE_PERSONAL_LoadDataDirect(u32 species, int form, void *dst)
    __attribute__((weak));                                    /* 0x0201ADD0 */

#define BW_SPECIES_MAX      649u
#define BW_MOVE_MAX         559u
#define BW_LEARNSET_BYTES   0x68u
#define BW_EGG_MOVE_ARC     0x7Bu

static s32 state_species_learns(u16 species, u16 form, u16 move, u8 level)
{
    s32 how = 0;

    /* the species data opens with the game (as state_species_value) */
    if (species == 0u || species > BW_SPECIES_MAX || move == 0u || move > BW_MOVE_MAX ||
        g_game_data == NULL) {
        return -1;
    }
    /* the level-up table: (move, level) pairs ending with 0xFFFF */
    if (POKE_PERSONAL_LoadDataDirect != NULL) {
        u16 table[BW_LEARNSET_BYTES / 2u];
        u32 i;

        memset(table, 0xFF, sizeof(table));
        POKE_PERSONAL_LoadDataDirect(species, (int)form, table);
        for (i = 0; i + 1u < BW_LEARNSET_BYTES / 2u && table[i] != 0xFFFFu; i += 2u) {
            if (table[i] == move && (level == 0u || table[i + 1u] <= level)) {
                how |= 1;
                break;
            }
        }
    }
    /* the machines: TM01 to TM92 and HM01 to HM06 are items 0x148 to
     * 0x1A9, TM93 to TM95 items 0x26A to 0x26C */
    if (POKE_TOOL_CheckSpeciesTmCompat != NULL && FILE_UTIL_GetMachineMove != NULL &&
        FILE_UTIL_GetId != NULL) {
        u32 item;

        for (item = 0x148u; item <= 0x26Cu; item++) {
            if (item == 0x1AAu) {
                item = 0x26Au;
            }
            if (FILE_UTIL_GetMachineMove((u16)item) == move &&
                POKE_TOOL_CheckSpeciesTmCompat(species, form, (s32)FILE_UTIL_GetId(item)) != 0u) {
                how |= 2;
                break;
            }
        }
    }
    /* the egg moves */
    if (ARC_TOOL_LoadMember != NULL && GFL_HEAP_Free != NULL) {
        u16 *egg = (u16 *)ARC_TOOL_LoadMember(BW_EGG_MOVE_ARC, species, (u16)BW_STATE_BAG_HEAP);

        if (egg != NULL) {
            u32 n = egg[0], i;

            for (i = 1; i <= n && i < 64u; i++) {
                if (egg[i] == move) {
                    how |= 4;
                    break;
                }
            }
            GFL_HEAP_Free(egg);
        }
    }
    return how;
}

static u32 state_play_seconds(void)
{
    struct PlayTime *t;

    if (GAME_DATA_GetPlayTime == NULL || PLAYTIME_GetHours == NULL ||
        PLAYTIME_GetMinutes == NULL || PLAYTIME_GetSeconds == NULL ||
        g_game_data == NULL) {
        return 0;
    }
    t = GAME_DATA_GetPlayTime(live_game_data());
    if (t == NULL) {
        return 0;
    }
    return (u32)PLAYTIME_GetHours(t) * 3600u +
           (u32)PLAYTIME_GetMinutes(t) * 60u + (u32)PLAYTIME_GetSeconds(t);
}

static int state_item_value(u16 item, u32 field, u32 *out)
{
    if (out == NULL || FILE_UTIL_GetItemParam == NULL || g_game_data == NULL ||
        (u32)item > bw_limit_get(BW_PORT_LIMIT_ITEM_LAST)) {
        return -1;
    }
    *out = FILE_UTIL_GetItemParam((u32)item, field, BW_STATE_BAG_HEAP);
    return 0;
}

static int state_move_value(u16 move, u32 field, u32 *out)
{
    /* the move archive opens with the game (as state_species_value): a
     * read from a mod's start, before that, crashed the game */
    if (out == NULL || WAZA_TOOL_GetParam == NULL || g_game_data == NULL ||
        (u32)move > bw_limit_get(BW_PORT_LIMIT_MOVE_LAST)) {
        return -1;
    }
    if (field == BW_MOVE_FIELD_RANK_COUNT) {
        if (WAZA_TOOL_GetRankEffectCount == NULL) {
            return -1;
        }
        *out = WAZA_TOOL_GetRankEffectCount(move);
        return 0;
    }
    if ((field >= BW_MOVE_FIELD_RANK_TYPE1 && field <= BW_MOVE_FIELD_RANK_TYPE3) ||
        (field >= BW_MOVE_FIELD_RANK_VALUE1 && field <= BW_MOVE_FIELD_RANK_VALUE3)) {
        u32 index = (field & 0x0Fu) - 1u;
        s32 value = 0;
        u8 type;

        if (WAZA_TOOL_GetRankEffect == NULL) {
            return -1;
        }
        type = WAZA_TOOL_GetRankEffect(move, index, &value);
        *out = field <= BW_MOVE_FIELD_RANK_TYPE3 ? (u32)type : (u32)value;
        return 0;
    }
    *out = WAZA_TOOL_GetParam((u32)move, field);
    return 0;
}

/*
 * whiteout: the event the game runs after a lost battle
 * (SCRCMD_NETWORK_CreateWhiteOutEvent, from EVENT_BATTLE_ProcBattleTask):
 * the blackout screen, the arrival at the last Pokemon Centre and the
 * nurse's script.  A mod asks from inside a handler, where the game is in
 * the middle of its own work, so the event waits for a frame where the
 * field runs with no event; a few frames of that, so the field is settled.
 */
/* The lost battle reaches the whiteout with the field already closed
 * (EVENT_BATTLE_ProcBattleTask closes it for the battle and does not open
 * it again), so from the field the event closes it first. */
static int whiteout_event(struct GameEvent *event, int *seq, void *work)
{
    struct GameSystem *gs = GAME_EVENT_GetGameSystem(event);

    (void)work;
    switch (*seq) {
    case 0:
        GAME_EVENT_CallEvent(event, FIELD_STATUS_CreateFieldCloseEvent(gs, GAMESYSTEM_GetFieldMap(gs)));
        (*seq)++;
        return 0;
    case 1:
        /* the music: the lost battle fades it out and resets it the same
         * way before the whiteout */
        GAME_EVENT_CallEvent(event, FIELD_SOUND_SYSTEM_StartFadeOutAndResetTask(gs, 0x1E));
        (*seq)++;
        return 0;
    case 2:
        /* the game's whiteout by its own name, not the hooked one: this
         * whiteout is the mod's, not a lost battle, so it raises no
         * whiteout event; the test switches still count it */
        bw_debug_whiteouts++;
        GAME_EVENT_ChangeEvent(event, ov010_SCRCMD_NETWORK_CreateWhiteOutEvent(gs));
        return 0;
    default:
        return 1;
    }
}

static int state_whiteout(void)
{
    if (g_game_data == NULL || ov010_SCRCMD_NETWORK_CreateWhiteOutEvent == NULL ||
        GAME_EVENT_Create == NULL || GAME_EVENT_CallEvent == NULL || GAME_EVENT_ChangeEvent == NULL ||
        GAME_EVENT_GetGameSystem == NULL || GAMESYSTEM_GetFieldMap == NULL ||
        FIELD_STATUS_CreateFieldCloseEvent == NULL || FIELD_SOUND_SYSTEM_StartFadeOutAndResetTask == NULL ||
        GAMESYSTEM_SetEvent == NULL || GAMESYSTEM_GetEvent == NULL ||
        &bw_dsref__GameSystemGlobal == NULL) {
        return -1;
    }
    g_whiteout_pending = 1;
    g_whiteout_wait = 0;
    return 0;
}

void bw_mod_state_frame(void)
{
    struct GameSystem *gs;
    struct GameEvent *ev;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return;
    }
    gs = *(struct GameSystem **)bw_dsref__GameSystemGlobal;
    /* The game data: the battle and the mon events bind it, so before the
     * first of those (a game loaded and walked in) every state call failed.
     * The game system holds it once the field runs (GAMESYSTEM_GetGameData). */
    if (g_game_data == NULL && gs != NULL && *(void **)((u8 *)gs + 0x20) != NULL &&
        GAMESYSTEM_GetGameData != NULL) {
        g_game_data = GAMESYSTEM_GetGameData(gs);
        if (g_game_data != NULL && bw_mod_export_set_game_data != NULL) {
            bw_mod_export_set_game_data(g_game_data);   /* as the events bind it */
        }
    }
    /* The field player: nothing bound it, so player_pos always failed.  It
     * lives as long as the field map does. */
    if (gs != NULL && ov021_FIELDMAP_GetFieldPlayer != NULL) {
        void *fm = *(void **)((u8 *)gs + 0x20);

        g_field_player = fm != NULL ? ov021_FIELDMAP_GetFieldPlayer(fm) : NULL;
    }
    if (!g_whiteout_pending) {
        return;
    }
    /* the field map sits at +0x20 of the game system (engine/ui/ui.c's
     * debug warp reads it the same way) */
    if (gs == NULL || *(void **)((u8 *)gs + 0x20) == NULL || g_battle_main != NULL ||
        GAMESYSTEM_GetEvent(gs) != NULL) {
        g_whiteout_wait = 0;
        return;
    }
    if (++g_whiteout_wait < 10) {
        return;
    }
    g_whiteout_pending = 0;
    ev = GAME_EVENT_Create(gs, NULL, whiteout_event, 4u);
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
    }
}

/*
 * The table the host hands to a mod (sdk/mod_api.h, BwModHost).  The
 * entries are in the order of the struct (sdk/mod_state.h); that struct
 * is frozen within an interface version, so the order does not move.
 *
 * Every entry is filled: a call that cannot answer says so (a negative
 * number for the calls that answer with a value, a non-zero number for
 * the others) rather than being left NULL.
 */
const BwModState bw_mod_state = {
    state_party_count,
    state_party_ref,
    state_party_get,
    state_party_set,

    state_box_count,
    state_box_ref,
    state_box_get,
    state_box_set,

    state_battle_ref,
    state_battle_get,
    state_battle_set,

    state_species_value,
    state_species_shiny,

    state_dex_caught,

    state_badge_count,
    state_badge_has,
    state_money,
    state_money_set,
    state_flag,
    state_flag_set,

    state_zone,
    state_player_pos,
    state_battle_style,

    state_mon_to_box,
    state_mon_take_item,

    state_mon_release,

    state_item_count,
    state_item_add,

    state_area_of,
    state_battle_wild,
    state_play_seconds,
    state_item_value,
    state_move_value,

    state_mon_to_box_n,
    state_mon_from_box,
    state_mon_recalc,

    state_species_base,
    state_battle_trainer,

    state_type_effect,
    state_battle_weather,
    state_species_learns,
    state_whiteout
};

int bw_mod_state_party_slot(const void *mon)
{
    struct Party *party = live_party();
    u32 count, i;

    if (mon == NULL || party == NULL || POKEPARTY_GetCount == NULL) {
        return -1;
    }
    count = POKEPARTY_GetCount(party);
    for (i = 0; i < count; i++) {
        if ((const void *)live_member(i) == mon) {
            return (int)i;
        }
    }
    return -1;
}

/* The table, for the host table to point at. */
const BwModState *bw_mod_state_table(void)
{
    return &bw_mod_state;
}
