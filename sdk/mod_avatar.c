/*
 * sdk/mod_avatar.c -- the field avatars: the other player's figure in the
 * local field (docs/mod-services.md, "Field avatars"; docs/mod-coop.md,
 * sections 1 to 4).
 *
 * What this file is
 * -----------------
 *
 * The port's side of the field avatar service.  A mod asks for an avatar
 * (the other player's figure), moves it and deletes it; the port makes a
 * field entity in the game's own entity pool for it, the game draws and
 * animates it with its own code, and the local game never notices it.
 *
 * The rules it keeps
 * ------------------
 *
 *   - The avatar is a field entity of the game's own: a record of the
 *     FLDMMDL system (src/game/ov010/fldmmdl.c), made the way the game
 *     makes a person of a zone (FLDMMDL_AddMmdl 0x0216CE60 into
 *     FLDMMDL_AddMmdlCore 0x0216CDC4).  The game draws it and animates it
 *     with its own code, so the avatar looks like the game's own people.
 *   - The object id is outside the game's own range, and the record has no
 *     event.  The zone's people have the object ids of the zone's event
 *     data and the player has 0xFF; the port's ids start at 0x8000.  The
 *     talk check reads the event id (FLDMMDL_GetEventId) and acts only
 *     when it is a zone script id (SCRIPT_SYS_IsZoneScriptId), and the
 *     avatar's event id is the game's own "no event" 0xFFFF, so the player
 *     cannot talk to an avatar and no script ever runs for one.
 *   - The hit checks leave it alone: the port sets the fellow-hit bit
 *     (MMDL_STABIT_FELLOW_HIT_OFF), the bit the game sets on a model that
 *     has already moved, so the people of the zone walk through the avatar
 *     and it never blocks one of them.
 *   - The port moves it smoothly between the positions the mod sends.  The
 *     mod sends a cell a few times a second (docs/mod-coop.md, section 2);
 *     the port walks the avatar from the cell it is in to the cell the mod
 *     named, a step a frame (bw_mod_avatar_frame, the port's own frame
 *     call), and the game draws it where the port put it.
 *   - The avatars go away at every map change.  The field empties its pool
 *     at a map change and adds the people of the new zone afterwards, so
 *     the route.change event (the field area's row, sdk/mod_event_field.c)
 *     is where the port's reset (bw_mod_avatar_reset) belongs: the
 *     avatars the game has already deleted are only forgotten, and the mod
 *     recreates the ones in the new zone.
 *
 * The two open questions of docs/mod-coop.md, section 4
 * ------------------------------------------------------
 *
 * The pool's headroom and the sprite.  Both are answered from the game's
 * own code and both are in the report; the short answers are:
 *
 *   - The pool holds 64 records.  FLDMMDL_CreateSys 0x0216CC58 is called
 *     with 0x40 (src/game/arm9/game_data.c), and FLDMMDL_SysGetMmdlMax
 *     0x0216D4F0 answers the size at run time.  The game does not check
 *     the free slot before it takes it -- FLDMMDL_AddMmdlCore writes to
 *     the record FLDMMDL_SysGetFreeMmdl 0x0216DEEC answered, and that
 *     answer is 0 when the pool is full -- so the port asks for the free
 *     slot itself and answers 0 when there is no room.  The port also
 *     leaves a few slots free, so the zone's own people, which the game
 *     adds right after the route.change event, always fit.
 *
 *   - Two copies of the player's sprite can be resident at once.  The
 *     resource tables are keyed by the object code (FLDMMDL_BLACT_AddResIndex
 *     and FLDMMDL_G3DOBJ_AddResEntry), so one object code is one resident
 *     resource, and every model with that code is another instance of it
 *     (FLDMMDL_CheckSameObjCodeOther keeps the resource alive while a
 *     second model with the same code is in the pool).  The avatar is made
 *     with the player's own object code (1 the hero, 4 the heroine,
 *     FIELD_PLAYER_OBJ_CODE_HERO and FIELD_PLAYER_OBJ_CODE_HEROINE,
 *     src/game/ov021/field_player_types.h), and the map has already loaded
 *     that sprite (FIELDMAP_SetupMmdlSys calls FLDMMDL_BLACT_AddResByCode
 *     with the player's code), so the avatar is a second instance of a
 *     texture that is already there.
 *
 * The seam
 * -------
 *
 * The port needs the game data to reach the entity pool: the pool hangs
 * off it (GAME_DATA_GetOccupyInfo 0x02012964 answers the same object the
 * field map uses).  The port binds it wherever it binds it for the state
 * calls (bw_mod_state_set_game_data); the export service has the same
 * seam (sdk/mod_export.c).  Every game function below is declared weak,
 * the way sdk/mod_state.c declares them, so a link without the game still
 * succeeds and a call without the game reports nothing instead of calling
 * into nothing.
 *
 * The handles
 * -----------
 *
 * The handle of an avatar is the port's own number: the slot in the
 * port's table and the generation of that slot, so a handle the port has
 * given out is never mistaken for another one.  The record of the game is
 * not kept: the port finds it again through the object's id, so nothing
 * here assumes that a pointer fits in a u32, and a record the game has
 * deleted is never written to.
 */

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   FLDMMDL_ACMD_ConvertCodeByDir        0x02197E7C  thumb  ov021
 *   FLDMMDL_AddMmdl                      0x0216CE60  thumb  ov010
 *   FLDMMDL_CheckEndAcmd                 0x0216D454  thumb  ov010
 *   FLDMMDL_CheckStatusBit               0x0216D55C  thumb  ov010
 *   FLDMMDL_DeleteMmdl                   0x0216CF2C  thumb  ov010
 *   FLDMMDL_GetEventId                   0x0216D5B8  thumb  ov010
 *   FLDMMDL_GetObjCode                   0x0216D598  thumb  ov010
 *   FLDMMDL_GetObjId                     0x0216D588  thumb  ov010
 *   FLDMMDL_GetPos                       0x0216D88C  thumb  ov010
 *   FLDMMDL_OnMoveBitMovePause           0x0216DAF8  thumb  ov010
 *   FLDMMDL_SetAcmd                      0x0216D410  thumb  ov010
 *   FLDMMDL_SetDirAll                    0x0216D60C  thumb  ov010
 *   FLDMMDL_SetGridNowX                  0x0216D844  thumb  ov010
 *   FLDMMDL_SetGridNowY                  0x0216D85C  thumb  ov010
 *   FLDMMDL_SetGridNowZ                  0x0216D874  thumb  ov010
 *   FLDMMDL_SetPos                       0x0216D89C  thumb  ov010
 *   FLDMMDL_SetStatusBit                 0x0216D548  thumb  ov010
 *   FLDMMDL_SetStatusBitZoneDeleteOff    0x0216DB80  thumb  ov010
 *   FLDMMDL_SysGetFreeMmdl               0x0216DEEC  thumb  ov010
 *   FLDMMDL_SysGetMmdlMax                0x0216D4F0  thumb  ov010
 *   FLDMMDL_SysSearchObjId               0x0216DE94  thumb  ov010
 *   FLDMMDL_SysSearchUseMmdl             0x0216DD1C  thumb  ov010
 */

#include <stddef.h>
#include <string.h>

#include "types.h"
#include "mod_avatar.h"

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's structures, named only as the opaque types its accessors
 * take.  The port never looks inside them: every value comes back out
 * through another accessor call, and the one record the port builds (the
 * placement header) is a copy of the game's own, below.
 */
struct GameData;
struct PlayerWork;
struct MmdlSys;
struct Mmdl;
struct MmdlHeader;

/*
 * The vector the field models' position comes in.  The game's own is
 * VecFx32 (src/game/ov010/fldmmdl_types.h): three fx32, which the port's
 * types.h defines as 32-bit fixed point with twelve fractional bits.
 */
struct BwPortVecFx32 {
    fx32 x;
    fx32 y;
    fx32 z;
};

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; the port's
 * own seam (sdk/mod_state.c) does the same.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The game data and the zone the player is in. */
extern void *GAME_DATA_GetOccupyInfo(const struct GameData *gd)
    BW_PORT_WEAK;                                        /* 0x02012964 */
extern struct PlayerWork *GAME_DATA_GetMyPlayerWork(struct GameData *gd)
    BW_PORT_WEAK;                                        /* 0x020127A4 */
extern u16 GAME_DATA_GetPlayerZoneId(const struct PlayerWork *player)
    BW_PORT_WEAK;                                        /* 0x02012AC8 */

/* The entity pool. */
extern struct Mmdl *FLDMMDL_AddMmdl(struct MmdlSys *sys,
                                    const struct MmdlHeader *header,
                                    int zoneId)
    BW_PORT_WEAK;                                        /* 0x0216CE60 */
extern void FLDMMDL_DeleteMmdl(struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216CF2C */
extern struct Mmdl *FLDMMDL_SysSearchObjId(struct MmdlSys *sys, u32 objId)
    BW_PORT_WEAK;                                        /* 0x0216DE94 */
extern struct Mmdl *FLDMMDL_SysGetFreeMmdl(struct MmdlSys *sys)
    BW_PORT_WEAK;                                        /* 0x0216DEEC */
extern int FLDMMDL_SysSearchUseMmdl(const struct MmdlSys *sys,
                                    struct Mmdl **out, u32 *iter)
    BW_PORT_WEAK;                                        /* 0x0216DD1C */
extern u16 FLDMMDL_SysGetMmdlMax(const struct MmdlSys *sys)
    BW_PORT_WEAK;                                        /* 0x0216D4F0 */

/* One record. */
extern int FLDMMDL_CheckStatusBit(const struct Mmdl *mmdl, u32 bit)
    BW_PORT_WEAK;                                        /* 0x0216D55C */
extern u32 FLDMMDL_GetObjId(const struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216D588 */
extern u32 FLDMMDL_GetObjCode(const struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216D598 */
extern u32 FLDMMDL_GetEventId(const struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216D5B8 */
extern void FLDMMDL_GetPos(const struct Mmdl *mmdl,
                            struct BwPortVecFx32 *out)
    BW_PORT_WEAK;                                        /* 0x0216D88C */
extern void FLDMMDL_SetPos(struct Mmdl *mmdl,
                            const struct BwPortVecFx32 *pos)
    BW_PORT_WEAK;                                        /* 0x0216D89C */
extern void FLDMMDL_SetGridNowX(struct Mmdl *mmdl, short x)
    BW_PORT_WEAK;                                        /* 0x0216D844 */
extern void FLDMMDL_SetGridNowY(struct Mmdl *mmdl, int y)
    BW_PORT_WEAK;                                        /* 0x0216D85C */
extern void FLDMMDL_SetGridNowZ(struct Mmdl *mmdl, int z)
    BW_PORT_WEAK;                                        /* 0x0216D874 */
extern void FLDMMDL_SetDirAll(struct Mmdl *mmdl, u16 dir)
    BW_PORT_WEAK;                                        /* 0x0216D60C */
extern void FLDMMDL_SetStatusBit(struct Mmdl *mmdl, u32 bit)
    BW_PORT_WEAK;                                        /* 0x0216D548 */
extern void FLDMMDL_SetStatusBitZoneDeleteOff(struct Mmdl *mmdl, int flag)
    BW_PORT_WEAK;                                        /* 0x0216DB80 */
extern void FLDMMDL_OnMoveBitMovePause(struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216DAF8 */

/* The animation. */
extern void FLDMMDL_SetAcmd(struct Mmdl *mmdl, u16 code)
    BW_PORT_WEAK;                                        /* 0x0216D410 */
extern int FLDMMDL_CheckEndAcmd(struct Mmdl *mmdl)
    BW_PORT_WEAK;                                        /* 0x0216D454 */
extern u16 FLDMMDL_ACMD_ConvertCodeByDir(int index, int code)
    BW_PORT_WEAK;                                        /* 0x02197E7C (ov021) */

/* ------------------------------------------------------------------ */
/* The port's own view of the game's records                          */
/* ------------------------------------------------------------------ */

/*
 * The port's own check, in the shape sdk/os_thread.c uses: a condition
 * that must hold at every pointer width the port is built for.  The name
 * is the port's own, so a header that later grows one does not clash with
 * it.
 */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/*
 * The placement record of one field model, copied member for member from
 * the game's own header (aletheia src/game/ov010/fldmmdl_types.h): the
 * MmdlHeader, 0x24 bytes.  The copy is what makes the call correct at
 * either pointer width: the record holds no pointer, so the same members
 * in the same order give the same offsets on both.
 */
typedef struct BwPortMmdlHeader {
    u16 obj_id;        /* 0x00: the object's id */
    u16 obj_code;      /* 0x02: the sprite (the game's own object code) */
    u16 move_code;     /* 0x04: how the model moves by itself */
    u16 event_type;    /* 0x06: the kind of event the model carries */
    u16 event_flag;    /* 0x08: the flag of that event */
    u16 event_id;      /* 0x0A: the script of that event */
    u16 dir_header;    /* 0x0C: the direction the model faces */
    u16 param0;        /* 0x0E */
    u16 param1;        /* 0x10 */
    u16 param2;        /* 0x12 */
    s16 move_limit_x;  /* 0x14 */
    s16 move_limit_z;  /* 0x16 */
    int pos_type;      /* 0x18: 0 a grid cell, else a rail one */
    union {
        struct {
            u16 grid_x;   /* 0x1C */
            u16 grid_z;   /* 0x1E */
            int pos_y;    /* 0x20 */
        } grid;
        u16 raw[4];
    } pos;
} BwPortMmdlHeader;

BW_PORT_STATIC_ASSERT(header_size, sizeof(BwPortMmdlHeader) == 0x24u);
BW_PORT_STATIC_ASSERT(header_obj_id,
                      offsetof(BwPortMmdlHeader, obj_id) == 0x00u);
BW_PORT_STATIC_ASSERT(header_obj_code,
                      offsetof(BwPortMmdlHeader, obj_code) == 0x02u);
BW_PORT_STATIC_ASSERT(header_move_code,
                      offsetof(BwPortMmdlHeader, move_code) == 0x04u);
BW_PORT_STATIC_ASSERT(header_event_id,
                      offsetof(BwPortMmdlHeader, event_id) == 0x0Au);
BW_PORT_STATIC_ASSERT(header_dir_header,
                      offsetof(BwPortMmdlHeader, dir_header) == 0x0Cu);
BW_PORT_STATIC_ASSERT(header_pos_type,
                      offsetof(BwPortMmdlHeader, pos_type) == 0x18u);
BW_PORT_STATIC_ASSERT(header_pos,
                      offsetof(BwPortMmdlHeader, pos) == 0x1Cu);
BW_PORT_STATIC_ASSERT(header_grid_x,
                      offsetof(BwPortMmdlHeader, pos.grid.grid_x) == 0x1Cu);
BW_PORT_STATIC_ASSERT(header_grid_z,
                      offsetof(BwPortMmdlHeader, pos.grid.grid_z) == 0x1Eu);
BW_PORT_STATIC_ASSERT(header_pos_y,
                      offsetof(BwPortMmdlHeader, pos.grid.pos_y) == 0x20u);

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/* How many avatars the port holds at once.  A session has a handful of
 * other players and the pool has 64 records. */
#define BW_MOD_AVATAR_MAX 16

/*
 * The first object's id the port gives its avatars.  The game's own ids
 * are the ones of the zone's event data (small numbers) and the player has
 * 0xFF, so the port's range is above all of them and can never be one of
 * the game's.
 */
#define BW_MOD_AVATAR_OBJ_ID_BASE 0x8000u

/*
 * The free slots the port leaves in the pool.
 *
 * The game adds the people of the new zone right after the route.change
 * event, where a mod creates the avatars of the new zone, and the game
 * does not check the free slot before it takes one (FLDMMDL_AddMmdlCore
 * writes to the record FLDMMDL_SysGetFreeMmdl answered, and that answer
 * is 0 when the pool is full).  The port therefore keeps this many slots
 * free, so the zone's own people always fit, and the service reports that
 * there is no room rather than take a slot the game needs.
 */
#define BW_MOD_AVATAR_POOL_RESERVE 8u

/*
 * The object's ids the game takes from the zone's event data: the sprite
 * of the code is the one the zone's own data names
 * (FLDMMDL_GetEventWorkObjCode), so an avatar cannot use them.
 */
#define BW_MOD_AVATAR_SPRITE_ZONE_FIRST 162u
#define BW_MOD_AVATAR_SPRITE_ZONE_LAST  177u

/*
 * The event id of a model with no event: the game's own 0xFFFF
 * (FLDMMDL_IsAliasEventId answers 1 for it).  The talk check acts only on
 * a model whose event id is a zone script id, so this is what keeps the
 * avatar out of the local game's way.
 */
#define BW_MOD_AVATAR_EVENT_ID_NONE 0xFFFFu

/*
 * The status bits the port sets on an avatar:
 *
 *   - the fellow-hit bit: the model takes no part in the hit checks, so
 *     the people of the zone walk through it and it blocks none of them.
 *     The game sets the same bit on a model that has already moved
 *     (FLDMMDL_HasMovedPos).
 *   - the zone-delete bit: the model is kept when the zone changes inside
 *     the map (the player has it too).  A map change empties the whole
 *     pool, so the avatar goes away there either way.
 */
#define BW_MOD_AVATAR_STABIT_FELLOW_HIT_OFF 0x0080u
#define BW_MOD_AVATAR_STABIT_ZONE_DEL_OFF   0x0020u

/*
 * The animation commands the game uses for the player (src/game/ov021/
 * field_player_grid.c): the stand, the walk and the run.  The port asks
 * the game for the code of the direction it wants
 * (FLDMMDL_ACMD_ConvertCodeByDir), so the codes are the game's own.
 */
#define BW_MOD_AVATAR_ACMD_STAND 0x00u
#define BW_MOD_AVATAR_ACMD_WALK  0x0Cu
#define BW_MOD_AVATAR_ACMD_RUN   0x10u

/*
 * How the avatar moves: a sixteenth of a cell a frame is the speed of the
 * game's own walk (the player's walk acmd is 16 frames a cell).  The run
 * is twice that, the bicycle a little more, and surfing a little more than
 * a walk.  A cell is 0x10000 in the game's fixed point.  These are the
 * speed the other player is expected to have; the catch-up below makes up
 * for any error in them.
 */
#define BW_MOD_AVATAR_STEP_WALK  0x1000
#define BW_MOD_AVATAR_STEP_RUN   0x2000
#define BW_MOD_AVATAR_STEP_CYCLE 0x2800
#define BW_MOD_AVATAR_STEP_SURF  0x1800

/*
 * The other player's real place runs ahead of the avatar by a step or two
 * (a place arrives at most ten times a second).  Further behind than one
 * cell, the avatar speeds up in proportion to the lag (a sixth of it a
 * frame); more than two cells behind (the other player teleported, or
 * this game lagged) it does not walk the way at all: it is put on the
 * real place.
 */
#define BW_MOD_AVATAR_LAG_CATCH_UP 0x10000
#define BW_MOD_AVATAR_LAG_SNAP     0x20000

/* The modes the mod sends (the player.step payload's mode). */
#define BW_MOD_AVATAR_MODE_WALKING 0u
#define BW_MOD_AVATAR_MODE_RUNNING 1u
#define BW_MOD_AVATAR_MODE_CYCLING 2u
#define BW_MOD_AVATAR_MODE_SURFING 3u

/* The sprites of the player, and the player's object id. */
#define BW_MOD_AVATAR_OBJ_CODE_HERO    1u
#define BW_MOD_AVATAR_OBJ_CODE_HEROINE 4u

/* engine/avatar/avatar.h: the private codes of the other players' looks
 * (weak: the tests link without the engine) */
extern int avatar_field_is_code(u32 code) __attribute__((weak));
#define BW_MOD_AVATAR_OBJ_ID_PLAYER    0xFFu

/* The directions (the game's own: 0 up, 1 down, 2 left, 3 right). */
#define BW_MOD_AVATAR_DIR_COUNT 4u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/*
 * The game's own game data: the port's seam.  The entity pool hangs off
 * it (GAME_DATA_GetOccupyInfo).
 */
static void *g_game_data;

/*
 * One avatar: what the port knows about it.
 *
 * The record of the game is not kept here: the port finds it again
 * through the object's id, so nothing here assumes that a pointer fits in
 * a u32, and a record the game has deleted is never written to.
 */
typedef struct BwModAvatar {
    int  used;        /* the slot is in use                            */
    u32  generation;  /* the generation the handle carries             */
    u16  obj_id;      /* the object's id the port gave it              */
    u16  sprite;      /* the game's own object code                    */
    s16  cell_x;      /* the cell the avatar is in                     */
    s16  cell_y;
    s16  cell_z;
    fx32 pos_x;       /* the position, in the game's own fixed point   */
    fx32 pos_z;
    s16  target_x;    /* the cell the mod named                        */
    s16  target_y;
    s16  target_z;
    u8   dir;         /* the facing                                    */
    u8   mode;        /* how the other player moves                    */
    u8   moving;      /* the avatar is on its way                      */
    u8   acmd_base;   /* the animation the port last set               */
    u8   acmd_dir;    /* the direction it set it for                   */
} BwModAvatar;

static BwModAvatar g_avatars[BW_MOD_AVATAR_MAX];

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* The entity pool, or NULL when the game is not there. */
static struct MmdlSys *pool(void)
{
    if (g_game_data == NULL || GAME_DATA_GetOccupyInfo == NULL) {
        return NULL;
    }
    return (struct MmdlSys *)GAME_DATA_GetOccupyInfo(
        (const struct GameData *)g_game_data);
}

/* The zone the player is in: the zone the avatar belongs to. */
static u16 current_zone(void)
{
    struct PlayerWork *player;

    if (g_game_data == NULL || GAME_DATA_GetMyPlayerWork == NULL ||
        GAME_DATA_GetPlayerZoneId == NULL) {
        return 0u;
    }
    player = GAME_DATA_GetMyPlayerWork((struct GameData *)g_game_data);
    if (player == NULL) {
        return 0u;
    }
    return GAME_DATA_GetPlayerZoneId(player);
}

/* How many records of the pool are in use. */
static u32 pool_used(struct MmdlSys *sys)
{
    struct Mmdl *mmdl = NULL;
    u32 iter = 0;
    u32 used = 0;

    if (sys == NULL || FLDMMDL_SysSearchUseMmdl == NULL) {
        return 0u;
    }
    while (FLDMMDL_SysSearchUseMmdl(sys, &mmdl, &iter) == 1) {
        used++;
        if (used > 0xFFFFu) {
            break;      /* the pool cannot hold more than that */
        }
    }
    return used;
}

/* How many records of the pool are free. */
static u32 pool_free(struct MmdlSys *sys)
{
    u32 max;
    u32 used;

    if (sys == NULL || FLDMMDL_SysGetMmdlMax == NULL) {
        return 0u;
    }
    max = (u32)FLDMMDL_SysGetMmdlMax(sys);
    used = pool_used(sys);
    if (used >= max) {
        return 0u;
    }
    return max - used;
}

/*
 * Is there room for another avatar?  The game does not ask before it takes
 * a slot, so the port asks: the free slot has to be there, and the slots
 * the port keeps for the zone's own people have to be free too.
 */
static int pool_has_room(struct MmdlSys *sys)
{
    if (sys == NULL || FLDMMDL_SysGetFreeMmdl == NULL) {
        return 0;
    }
    if (FLDMMDL_SysGetFreeMmdl(sys) == NULL) {
        return 0;       /* the pool is full */
    }
    if (pool_free(sys) <= (u32)BW_MOD_AVATAR_POOL_RESERVE) {
        return 0;       /* the zone's own people need these */
    }
    return 1;
}

/* The first slot of the port's table that is free, or NULL. */
static BwModAvatar *free_slot(void)
{
    u32 i;

    for (i = 0; i < (u32)BW_MOD_AVATAR_MAX; i++) {
        if (!g_avatars[i].used) {
            return &g_avatars[i];
        }
    }
    return NULL;
}

/* The avatar a handle names, or NULL. */
static BwModAvatar *avatar_of(u32 handle)
{
    u32 index;
    u32 generation;

    if (handle == 0u) {
        return NULL;
    }
    index = handle & 0xFFu;
    generation = (handle >> 8) & 0x00FFFFFFu;
    if (index >= (u32)BW_MOD_AVATAR_MAX) {
        return NULL;
    }
    if (!g_avatars[index].used) {
        return NULL;
    }
    if (g_avatars[index].generation != generation) {
        return NULL;    /* the slot was used again since */
    }
    return &g_avatars[index];
}

/* The handle an avatar answers to.  The generation is never 0, so a handle
 * is never 0 (which is what "no avatar" answers). */
static u32 handle_of(const BwModAvatar *a, u32 index)
{
    u32 generation = a->generation & 0x00FFFFFFu;

    if (generation == 0u) {
        generation = 1u;
    }
    return (generation << 8) | (index & 0xFFu);
}

/* Forget an avatar: the slot is free again. */
static void forget(BwModAvatar *a)
{
    u32 generation = (a->generation + 1u) & 0x00FFFFFFu;

    if (generation == 0u) {
        generation = 1u;
    }
    memset(a, 0, sizeof(*a));
    a->generation = generation;
}

/*
 * The record of an avatar, found again through its object's id.  NULL
 * when the game has deleted it (the field empties the pool at a map
 * change) or when there is no game.
 */
static struct Mmdl *avatar_record(const BwModAvatar *a)
{
    struct MmdlSys *sys = pool();

    if (sys == NULL || FLDMMDL_SysSearchObjId == NULL) {
        return NULL;
    }
    return FLDMMDL_SysSearchObjId(sys, (u32)a->obj_id);
}

/* A cell as the game's own fixed point position: the middle of the cell,
 * the way the game places a model on a grid cell
 * (FLDMMDL_SetGridPosFromHeader). */
static fx32 cell_fx32(s16 cell)
{
    return (fx32)(((s32)cell << 16) + 0x8000);
}

/* A step of the walk towards the target. */
static fx32 step_towards(fx32 value, fx32 target, fx32 step)
{
    fx32 diff = target - value;

    if (diff > step) {
        return value + step;
    }
    if (diff < -step) {
        return value - step;
    }
    return target;
}

static fx32 fx_abs(fx32 v)
{
    return v < 0 ? -v : v;
}

/* How far the avatar is from the cell the mod named, in the larger of the
 * two axes. */
static fx32 lag_of(const BwModAvatar *a)
{
    fx32 dx = fx_abs(cell_fx32(a->target_x) - a->pos_x);
    fx32 dz = fx_abs(cell_fx32(a->target_z) - a->pos_z);

    return dx > dz ? dx : dz;
}

/* How far the avatar moves a frame: the speed of the other player's mode,
 * more when the avatar is a cell or more behind. */
static fx32 step_size(const BwModAvatar *a)
{
    fx32 step;
    fx32 lag;

    switch (a->mode) {
    case BW_MOD_AVATAR_MODE_RUNNING:
        step = (fx32)BW_MOD_AVATAR_STEP_RUN;
        break;
    case BW_MOD_AVATAR_MODE_CYCLING:
        step = (fx32)BW_MOD_AVATAR_STEP_CYCLE;
        break;
    case BW_MOD_AVATAR_MODE_SURFING:
        step = (fx32)BW_MOD_AVATAR_STEP_SURF;
        break;
    default:
        step = (fx32)BW_MOD_AVATAR_STEP_WALK;
        break;
    }
    lag = lag_of(a);
    if (lag > (fx32)BW_MOD_AVATAR_LAG_CATCH_UP && lag / 6 > step) {
        step = lag / 6;
    }
    return step;
}

/*
 * The animation of an avatar.
 *
 * The port asks the game for the code of the direction it wants
 * (FLDMMDL_ACMD_ConvertCodeByDir) and sets it (FLDMMDL_SetAcmd), so the
 * avatar is animated by the game's own commands.  The walk plays one cell
 * and stops, so the port starts it again when it has ended
 * (FLDMMDL_CheckEndAcmd); the stand is only set when the state changes.
 */
static void update_animation(BwModAvatar *a, struct Mmdl *mmdl, int moved)
{
    u32 base;
    u16 code;

    if (FLDMMDL_SetAcmd == NULL || FLDMMDL_ACMD_ConvertCodeByDir == NULL) {
        return;
    }
    if (moved) {
        base = (a->mode == BW_MOD_AVATAR_MODE_RUNNING ||
                a->mode == BW_MOD_AVATAR_MODE_CYCLING)
                   ? (u32)BW_MOD_AVATAR_ACMD_RUN
                   : (u32)BW_MOD_AVATAR_ACMD_WALK;
        if (FLDMMDL_CheckEndAcmd != NULL &&
            FLDMMDL_CheckEndAcmd(mmdl) == 0) {
            return;      /* the walk is still playing */
        }
    } else {
        base = (u32)BW_MOD_AVATAR_ACMD_STAND;
        if (a->acmd_base == base && a->acmd_dir == a->dir) {
            return;      /* the avatar already stands this way */
        }
    }
    code = (u16)FLDMMDL_ACMD_ConvertCodeByDir((int)a->dir, (int)base);
    FLDMMDL_SetAcmd(mmdl, code);
    a->acmd_base = (u8)base;
    a->acmd_dir = a->dir;
}

/*
 * One step of an avatar: it walks towards the cell the mod named, the
 * game is told where it is, and it faces the way the mod said.
 *
 * The height is the game's own: the game keeps the model on the ground
 * (FLDMMDL_MOVE_UpdateHeight), and the port does not fight it over the
 * height the mod sent.
 */
static void step_avatar(BwModAvatar *a, struct Mmdl *mmdl)
{
    struct BwPortVecFx32 pos;
    fx32 target_x;
    fx32 target_z;
    fx32 step;
    int moved = 0;

    target_x = cell_fx32(a->target_x);
    target_z = cell_fx32(a->target_z);
    if (lag_of(a) > (fx32)BW_MOD_AVATAR_LAG_SNAP) {
        a->pos_x = target_x;         /* too far behind: do not replay it */
        a->pos_z = target_z;
    }
    step = step_size(a);

    if (a->pos_x != target_x) {
        a->pos_x = step_towards(a->pos_x, target_x, step);
        moved = 1;
    }
    if (a->pos_z != target_z) {
        a->pos_z = step_towards(a->pos_z, target_z, step);
        moved = 1;
    }
    a->moving = moved ? 1u : 0u;

    pos.x = 0;
    pos.y = 0;
    pos.z = 0;
    if (FLDMMDL_GetPos != NULL) {
        FLDMMDL_GetPos(mmdl, &pos);
    }
    pos.x = a->pos_x;
    pos.z = a->pos_z;
    if (FLDMMDL_SetPos != NULL) {
        FLDMMDL_SetPos(mmdl, &pos);
    }

    /* The cell the avatar stands on: the game's own conversion of a fixed
     * point position ((pos >> 4) / 4096). */
    a->cell_x = (s16)(a->pos_x >> 16);
    a->cell_z = (s16)(a->pos_z >> 16);
    a->cell_y = (s16)(pos.y >> 16);
    if (FLDMMDL_SetGridNowX != NULL) {
        FLDMMDL_SetGridNowX(mmdl, (short)a->cell_x);
    }
    if (FLDMMDL_SetGridNowY != NULL) {
        FLDMMDL_SetGridNowY(mmdl, (int)a->cell_y);
    }
    if (FLDMMDL_SetGridNowZ != NULL) {
        FLDMMDL_SetGridNowZ(mmdl, (int)a->cell_z);
    }

    /* The facing. */
    if (FLDMMDL_SetDirAll != NULL) {
        FLDMMDL_SetDirAll(mmdl, (u16)a->dir);
    }

    update_animation(a, mmdl, moved);
}

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * The `avatar_create` call: an avatar appears.
 *
 * The sprite is the game's own object code.  The codes the game takes
 * from the zone's event data (162 to 177) are refused: the game replaces
 * those with the sprite the zone's own data names, so an avatar could not
 * use them.
 *
 * The answer is the avatar's handle, or 0 when there is no room.
 */
u32 bw_mod_avatar_create(u32 sprite, s16 x, s16 z, u8 dir)
{
    struct MmdlSys *sys;
    struct Mmdl *mmdl;
    BwModAvatar *a;
    BwPortMmdlHeader header;
    u32 index;

    sys = pool();
    if (sys == NULL || FLDMMDL_AddMmdl == NULL) {
        return 0u;
    }
    if (sprite == 0u || sprite > 0xFFFFu) {
        return 0u;       /* no sprite */
    }
    if (sprite >= (u32)BW_MOD_AVATAR_SPRITE_ZONE_FIRST &&
        sprite <= (u32)BW_MOD_AVATAR_SPRITE_ZONE_LAST) {
        return 0u;       /* the zone's own sprites */
    }
    if (!pool_has_room(sys)) {
        return 0u;       /* no room in the pool */
    }
    a = free_slot();
    if (a == NULL) {
        return 0u;       /* the port's own table is full */
    }
    index = (u32)(a - g_avatars);

    memset(&header, 0, sizeof(header));
    header.obj_id = (u16)(BW_MOD_AVATAR_OBJ_ID_BASE + index);
    header.obj_code = (u16)sprite;
    header.move_code = 0u;      /* the model does not move by itself */
    header.event_type = 0u;
    header.event_flag = 0u;
    header.event_id = (u16)BW_MOD_AVATAR_EVENT_ID_NONE;
    header.dir_header = (u16)(dir % (u32)BW_MOD_AVATAR_DIR_COUNT);
    header.pos_type = 0;        /* a grid cell */
    header.pos.grid.grid_x = (u16)x;
    header.pos.grid.grid_z = (u16)z;
    header.pos.grid.pos_y = 0;

    mmdl = FLDMMDL_AddMmdl(sys, (const struct MmdlHeader *)&header,
                           (int)current_zone());
    if (mmdl == NULL) {
        return 0u;       /* the pool had no room after all */
    }

    /* The hit checks leave the avatar alone, so the people of the zone
     * walk through it and it blocks none of them. */
    if (FLDMMDL_SetStatusBit != NULL) {
        FLDMMDL_SetStatusBit(mmdl, BW_MOD_AVATAR_STABIT_FELLOW_HIT_OFF);
    }
    /* The avatar is kept when the zone changes inside the map (the player
     * is kept too); a map change empties the pool either way. */
    if (FLDMMDL_SetStatusBitZoneDeleteOff != NULL) {
        FLDMMDL_SetStatusBitZoneDeleteOff(mmdl, 1);
    }
    /* The game's own move procedure never moves the avatar: the port
     * walks it. */
    if (FLDMMDL_OnMoveBitMovePause != NULL) {
        FLDMMDL_OnMoveBitMovePause(mmdl);
    }

    /* The slot's own state is written; the generation stays, so the handle
     * the port hands out names this avatar and not an older one. */
    {
        u32 generation = a->generation;

        if (generation == 0u) {
            generation = 1u;
        }
        memset(a, 0, sizeof(*a));
        a->generation = generation;
    }
    a->used = 1;
    a->obj_id = header.obj_id;
    a->sprite = (u16)sprite;
    a->cell_x = x;
    a->cell_y = 0;
    a->cell_z = z;
    a->pos_x = cell_fx32(x);
    a->pos_z = cell_fx32(z);
    a->target_x = x;
    a->target_y = 0;
    a->target_z = z;
    a->dir = (u8)(dir % (u32)BW_MOD_AVATAR_DIR_COUNT);
    a->mode = (u8)BW_MOD_AVATAR_MODE_WALKING;
    a->acmd_base = 0xFFu;      /* no animation has been set yet */

    /* The avatar stands where it was put. */
    step_avatar(a, mmdl);

    return handle_of(a, index);
}

/*
 * The `avatar_move` call: the avatar goes to the cell the mod named.
 *
 * The port walks it there; the walk itself happens in the port's frame.
 *
 * The height the mod sends is kept but not used: the game puts the model
 * on the ground itself (FLDMMDL_MOVE_UpdateHeight), and the port does not
 * fight it over a height the other player's map had.
 */
void bw_mod_avatar_move(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode)
{
    BwModAvatar *a = avatar_of(avatar);

    if (a == NULL) {
        return;
    }
    a->target_x = x;
    a->target_y = y;
    a->target_z = z;
    a->dir = (u8)(dir % (u32)BW_MOD_AVATAR_DIR_COUNT);
    a->mode = mode;
}

/*
 * The `avatar_delete` call: the avatar goes away.
 *
 * The record is found again through the object's id, so a record the game
 * has already deleted is not deleted twice.
 */
void bw_mod_avatar_delete(u32 avatar)
{
    BwModAvatar *a = avatar_of(avatar);
    struct Mmdl *mmdl;

    if (a == NULL) {
        return;
    }
    mmdl = avatar_record(a);
    if (mmdl != NULL && FLDMMDL_DeleteMmdl != NULL) {
        FLDMMDL_DeleteMmdl(mmdl);
    }
    forget(a);
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own seam: the game data, and with it the entity pool.
 */
void bw_mod_avatar_set_game_data(void *game_data)
{
    g_game_data = game_data;
}

/*
 * Every avatar goes away.
 *
 * The port calls this at the map change (the route.change row of the
 * field area calls it) and at its own reset.  The map change empties the
 * pool before the event fires, so most of the time there is nothing left
 * to delete and the avatars are only forgotten; the ones the game has
 * kept are deleted.
 */
void bw_mod_avatar_reset(void)
{
    u32 i;

    for (i = 0; i < (u32)BW_MOD_AVATAR_MAX; i++) {
        BwModAvatar *a = &g_avatars[i];
        struct Mmdl *mmdl;

        if (!a->used) {
            continue;
        }
        mmdl = avatar_record(a);
        if (mmdl != NULL && FLDMMDL_DeleteMmdl != NULL) {
            FLDMMDL_DeleteMmdl(mmdl);
        }
        forget(a);
    }
}

/*
 * The game saves its whole entity pool (GAME_DATA_WriteToSave stores it
 * through FLDMMDL_StoreSaveTable, block 0x29) and brings it back at a load
 * (FLDMMDL_LoadSaveTable), so a save made while an avatar stood in the
 * field holds the avatar, with its object id cut to a byte.  The port does
 * not know such a record: nobody moves it or deletes it, and it stands in
 * the field at the place of a player who may be gone.  This deletes them.
 * A stale avatar is a record that looks like one the port makes (the
 * hero's or heroine's sprite, no event, no hit checks) but has an object id
 * outside the port's range and is not the player (object id 0xFF).
 */
u32 bw_mod_avatar_sweep(void)
{
    struct MmdlSys *sys = pool();
    struct Mmdl *found[BW_MOD_AVATAR_MAX];
    struct Mmdl *mmdl = NULL;
    u32 iter = 0;
    u32 n = 0;
    u32 i;

    if (sys == NULL || FLDMMDL_SysSearchUseMmdl == NULL ||
        FLDMMDL_DeleteMmdl == NULL || FLDMMDL_GetObjId == NULL ||
        FLDMMDL_GetObjCode == NULL || FLDMMDL_GetEventId == NULL ||
        FLDMMDL_CheckStatusBit == NULL) {
        return 0u;
    }
    while (n < (u32)BW_MOD_AVATAR_MAX &&
           FLDMMDL_SysSearchUseMmdl(sys, &mmdl, &iter) == 1) {
        u32 code = FLDMMDL_GetObjCode(mmdl);
        u32 id = FLDMMDL_GetObjId(mmdl);

        if ((code == BW_MOD_AVATAR_OBJ_CODE_HERO ||
             code == BW_MOD_AVATAR_OBJ_CODE_HEROINE ||
             (avatar_field_is_code != NULL && avatar_field_is_code(code))) &&
            id != BW_MOD_AVATAR_OBJ_ID_PLAYER &&
            id < BW_MOD_AVATAR_OBJ_ID_BASE &&
            (FLDMMDL_GetEventId(mmdl) & 0xFFFFu) == BW_MOD_AVATAR_EVENT_ID_NONE &&
            FLDMMDL_CheckStatusBit(mmdl, BW_MOD_AVATAR_STABIT_FELLOW_HIT_OFF) != 0) {
            found[n++] = mmdl;
        }
    }
    for (i = 0; i < n; i++) {
        FLDMMDL_DeleteMmdl(found[i]);
    }
    return n;
}

/*
 * The port's own frame: every avatar walks one step towards the cell the
 * mod named.
 *
 * The port calls this once a frame, after the game's own frame work, so
 * the position the port writes is the one the game draws.
 */
void bw_mod_avatar_frame(void)
{
    u32 i;

    for (i = 0; i < (u32)BW_MOD_AVATAR_MAX; i++) {
        BwModAvatar *a = &g_avatars[i];
        struct Mmdl *mmdl;

        if (!a->used) {
            continue;
        }
        mmdl = avatar_record(a);
        if (mmdl == NULL) {
            /* The game has deleted it (the field empties the pool at a
             * map change): the port forgets it and does not write to a
             * record that is not there any more. */
            forget(a);
            continue;
        }
        step_avatar(a, mmdl);
    }
}

/* How many avatars the port holds. */
u32 bw_mod_avatar_count(void)
{
    u32 i;
    u32 count = 0;

    for (i = 0; i < (u32)BW_MOD_AVATAR_MAX; i++) {
        if (g_avatars[i].used) {
            count++;
        }
    }
    return count;
}

/* The entity pool's size, and how much of it is free. */
u32 bw_mod_avatar_pool_max(void)
{
    struct MmdlSys *sys = pool();

    if (sys == NULL || FLDMMDL_SysGetMmdlMax == NULL) {
        return 0u;
    }
    return (u32)FLDMMDL_SysGetMmdlMax(sys);
}

u32 bw_mod_avatar_pool_free(void)
{
    return pool_free(pool());
}

/* The object's id the port gave an avatar. */
u32 bw_mod_avatar_obj_id(u32 avatar)
{
    BwModAvatar *a = avatar_of(avatar);

    return (a == NULL) ? 0u : (u32)a->obj_id;
}

/* Where an avatar is, in cells. */
int bw_mod_avatar_pos(u32 avatar, s16 *x, s16 *y, s16 *z)
{
    BwModAvatar *a = avatar_of(avatar);

    if (a == NULL) {
        return -1;
    }
    if (x != NULL) {
        *x = a->cell_x;
    }
    if (y != NULL) {
        *y = a->cell_y;
    }
    if (z != NULL) {
        *z = a->cell_z;
    }
    return 0;
}

/* The facing of an avatar. */
u8 bw_mod_avatar_dir(u32 avatar)
{
    BwModAvatar *a = avatar_of(avatar);

    return (a == NULL) ? 0u : a->dir;
}
