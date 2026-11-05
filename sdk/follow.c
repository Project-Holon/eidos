/*
 * sdk/follow.c -- following Pokemon: the lead Pokemon walks behind the
 * player on the field (Extras, "Following Pokemon", off by default).
 *
 * The follower is a field model of the game's own, made the way the field
 * avatars are (sdk/mod_avatar.c): a record of the FLDMMDL pool with an
 * object id of the port's, no event and no hit checks, so the game draws
 * and animates it and never notices it.
 *
 * The sprite
 * ----------
 *
 *   - Species 1 to 493: the game's own walking sprite.  The Entralink's
 *     Pokemon walk with it, and the object code comes from the same table
 *     (archive 211, member 0: 8-byte records of species, sex, form and
 *     object code, read by TPOKE_DATA_GetRecordIndex in the game).
 *   - Species 494 to 649 have no walking sprite in Black and White.  A mod
 *     can supply one: follow/<species>.bin, through the data layer
 *     (docs/mods.md), 16 colours (BGR555) then six 32x32 4bpp frames (up
 *     twice, down twice, left twice).  The follower then borrows Unown's
 *     sprite and the port writes the mod's frames over Unown's texture.
 *     tools/follow_pack.py makes such a mod.  With no file there is no
 *     follower for that species.  An Unown on the same map shows the
 *     same frames while such a follower is out.
 *
 * The movement
 * ------------
 *
 * When the player starts a step the follower is sent onto the cell the
 * player left, at the player's speed.  More than a cell behind (a warp, a
 * turn on the spot into it) it is put there directly.  The follower goes
 * away on a rail map (FIELDMAP_GetMoveKind not 0), while the player surfs
 * and when the setting is off, and it is made again whenever the game has
 * removed it (a map change empties the pool).  During a field event (a
 * menu, a script, a warp) it is left as it is.
 *
 * The save
 * --------
 *
 * The game saves its whole pool, the follower with it.  A record with the
 * follower's id that is not the port's own is deleted, so a save made with
 * the setting on loads cleanly with it off.
 *
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py):
 *
 * symbols:
 *   FIELDMAP_GetFieldPlayer              0x02188CB4  thumb  ov021
 *   FIELDMAP_GetHeapId                   0x02188C84  thumb  ov021
 *   FIELDMAP_GetMoveKind                 0x02188CFC  thumb  ov021
 *   FIELD_PLAYER_GetFormCode             0x021A3098  thumb  ov021
 *   FLDMMDL_ACMD_ConvertCodeByDir        0x02197E7C  thumb  ov021
 *   FLDMMDL_AddMmdl                      0x0216CE60  thumb  ov010
 *   FLDMMDL_BLACT_FindRes                0x0219CBD4  thumb  ov021
 *   FLDMMDL_BLACT_GetBbActSys            0x0219C9DC  thumb  ov021
 *   FLDMMDL_ChangeObjCode                0x0216E204  thumb  ov010
 *   FLDMMDL_CheckEndAcmd                 0x0216D454  thumb  ov010
 *   FLDMMDL_CheckPossibleAcmd            0x0216D3CC  thumb  ov010
 *   FLDMMDL_CheckStatusBitVanish         0x0216DAB4  thumb  ov010
 *   FLDMMDL_DeleteMmdl                   0x0216CF2C  thumb  ov010
 *   FLDMMDL_EndAcmd                      0x0216D480  thumb  ov010
 *   FLDMMDL_GetAcmdCode                  0x0216D7C4  thumb  ov010
 *   FLDMMDL_GetDirDisp                   0x0216D5F8  thumb  ov010
 *   FLDMMDL_GetGridNowX                  0x0216D83C  thumb  ov010
 *   FLDMMDL_GetGridNowZ                  0x0216D86C  thumb  ov010
 *   FLDMMDL_GetGridOldX                  0x0216D804  thumb  ov010
 *   FLDMMDL_GetGridOldZ                  0x0216D81C  thumb  ov010
 *   FLDMMDL_GetPosY                      0x0216D8AC  thumb  ov010
 *   FLDMMDL_InitGridPosition             0x0216E084  thumb  ov010
 *   FLDMMDL_MOVE_HitCheckMoveDir         0x021639C0  thumb  ov010
 *   FLDMMDL_SetAcmd                      0x0216D410  thumb  ov010
 *   FLDMMDL_SetStatusBitFellowHit        0x0216DAE0  thumb  ov010
 *   FLDMMDL_SetStatusBitVanish           0x0216DAC8  thumb  ov010
 *   FLDMMDL_SetStatusBitZoneDeleteOff    0x0216DB80  thumb  ov010
 *   FLDMMDL_SysGetBlActCont              0x0216D520  thumb  ov010
 *   FLDMMDL_SysSearchObjId               0x0216DE94  thumb  ov010
 */

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "port.h"
#include "follow.h"

#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

struct Mmdl;
struct MmdlSys;
struct BlactSys;
struct BbActSys;
struct BbBoard;

/* engine/platform/settings.c; the number is settings.h's
 * EIDOS_OPT_FOLLOW_POKEMON */
extern int eidos_settings_get(int id) BW_PORT_WEAK;
#define OPT_FOLLOW_POKEMON 18

/* the game system's global, bound by sdk/armdata.c */
extern void *bw_dsref__GameSystemGlobal BW_PORT_WEAK;

extern void *GAMESYSTEM_GetGameData(const void *gameSystem) BW_PORT_WEAK;
extern void *GAMESYSTEM_GetEvent(const void *gameSystem) BW_PORT_WEAK;
extern void *GAMESYSTEM_GetFieldMap(const void *gameSystem) BW_PORT_WEAK;
extern void *GAME_DATA_GetOccupyInfo(const void *gameData) BW_PORT_WEAK;
extern void *GAME_DATA_GetMyParty(const void *gameData) BW_PORT_WEAK;
extern u32 POKEPARTY_GetCount(const void *party) BW_PORT_WEAK;
extern void *POKEPARTY_GetMember(void *party, u32 pos) BW_PORT_WEAK;
extern u32 POKE_TOOL_GetPartyValue(void *pp, u32 paramId, void *arg) BW_PORT_WEAK;
extern void *ARC_UTIL_LoadMemberWithSize(int archiveID, int memberIdx,
                                         int decompress, u16 heapID,
                                         u32 *out) BW_PORT_WEAK;
extern u32 GFL_HEAP_Free(void *p) BW_PORT_WEAK;
extern void *GFL_HEAP_AllocMemory(u32 heapId, u32 size) BW_PORT_WEAK;

extern u16 FIELDMAP_GetHeapId(void *fieldMap) BW_PORT_WEAK;
extern u32 FIELDMAP_GetMoveKind(void *fieldMap) BW_PORT_WEAK;
extern void *FIELDMAP_GetFieldPlayer(void *fieldMap) BW_PORT_WEAK;
extern u32 FIELD_PLAYER_GetFormCode(void *player) BW_PORT_WEAK;

extern struct Mmdl *FLDMMDL_AddMmdl(struct MmdlSys *sys, const void *header,
                                    int zoneId) BW_PORT_WEAK;
extern void FLDMMDL_DeleteMmdl(struct Mmdl *mmdl) BW_PORT_WEAK;
extern struct Mmdl *FLDMMDL_SysSearchObjId(struct MmdlSys *sys, u32 objId)
    BW_PORT_WEAK;
extern struct BlactSys *FLDMMDL_SysGetBlActCont(struct MmdlSys *sys)
    BW_PORT_WEAK;
extern void FLDMMDL_SetStatusBitZoneDeleteOff(struct Mmdl *mmdl, int flag)
    BW_PORT_WEAK;
extern void FLDMMDL_SetStatusBitFellowHit(struct Mmdl *mmdl, int flag)
    BW_PORT_WEAK;
extern int FLDMMDL_CheckStatusBitVanish(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern void FLDMMDL_SetStatusBitVanish(struct Mmdl *mmdl, int flag)
    BW_PORT_WEAK;
extern void FLDMMDL_ChangeObjCode(struct Mmdl *mmdl, u32 code) BW_PORT_WEAK;
extern u16 FLDMMDL_GetDirDisp(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern s16 FLDMMDL_GetGridNowX(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern s16 FLDMMDL_GetGridNowZ(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern int FLDMMDL_GetGridOldX(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern int FLDMMDL_GetGridOldZ(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern s32 FLDMMDL_GetPosY(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern void FLDMMDL_InitGridPosition(struct Mmdl *mmdl, unsigned int x,
                                     unsigned int y, unsigned int z,
                                     u16 dir) BW_PORT_WEAK;
extern u32 FLDMMDL_MOVE_HitCheckMoveDir(const struct Mmdl *mmdl, u16 dir)
    BW_PORT_WEAK;
extern int FLDMMDL_CheckPossibleAcmd(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern void FLDMMDL_SetAcmd(struct Mmdl *mmdl, u16 code) BW_PORT_WEAK;
extern int FLDMMDL_CheckEndAcmd(struct Mmdl *mmdl) BW_PORT_WEAK;
extern void FLDMMDL_EndAcmd(struct Mmdl *mmdl) BW_PORT_WEAK;
extern u16 FLDMMDL_GetAcmdCode(const struct Mmdl *mmdl) BW_PORT_WEAK;
extern int FLDMMDL_ACMD_ConvertCodeByDir(int dir, int code) BW_PORT_WEAK;

extern int FLDMMDL_BLACT_FindRes(struct BlactSys *cont, u32 code,
                                 u16 *slotIndex, u16 *flags) BW_PORT_WEAK;
extern struct BbActSys *FLDMMDL_BLACT_GetBbActSys(struct BlactSys *cont)
    BW_PORT_WEAK;
extern struct BbBoard *BILLBOARD_ACT_GetBoard(struct BbActSys *sys)
    BW_PORT_WEAK;
extern u16 BILLBOARD_ACT_GetSlotBoardId(struct BbActSys *sys, int slotIndex)
    BW_PORT_WEAK;
extern void BILLBOARD_GetImageVramAddr(struct BbBoard *board, int index,
                                       int *outTexVramOffset) BW_PORT_WEAK;
extern void BILLBOARD_GetImagePlttVramOffset(struct BbBoard *board, int index,
                                             int *outPlttVramOffset)
    BW_PORT_WEAK;
extern s32 NNS_GfdRegisterNewVramTransferTask(u32 type, u32 dstOffset,
                                              void *pSrc, s32 szByte)
    BW_PORT_WEAK;

/* The follower's object id: below the player's 0xFF and above the zone's
 * own, and a byte, so the copy a save keeps has the same id. */
#define FOLLOW_OBJ_ID       0xFDu
#define FOLLOW_EVENT_NONE   0xFFFFu
#define FOLLOW_SPECIES_MAX  650u
#define FOLLOW_GEN5_FIRST   494u
#define FOLLOW_CARRIER      201u        /* Unown: the Unova Pokemon's sprite */
#define FOLLOW_TABLE_ARC    211         /* a/2/1/1 */
#define FOLLOW_ACMD_WALK    0x0Cu
#define FOLLOW_TEX_SIZE     0x1000      /* eight 32x32 4bpp frames */
#define FOLLOW_MOD_TEX_SIZE 0xC00       /* the six a mod supplies */
#define FOLLOW_REUPLOAD     120         /* frames between texture uploads */
#define FOLLOW_FORM_SURF    2           /* FIELD_PLAYER_AVATAR_FORM_SURF */

#define PARA_SPECIES 5u
#define PARA_EGG     0x4Cu

/* The placement record, as in sdk/mod_avatar.c (fldmmdl_types.h's
 * MmdlHeader, 0x24 bytes, no pointer). */
typedef struct FollowHeader {
    u16 obj_id;
    u16 obj_code;
    u16 move_code;
    u16 event_type;
    u16 event_flag;
    u16 event_id;
    u16 dir_header;
    u16 param[3];
    s16 move_limit_x;
    s16 move_limit_z;
    int pos_type;
    u16 grid_x;
    u16 grid_z;
    int pos_y;
} FollowHeader;

typedef char follow_header_size[sizeof(FollowHeader) == 0x24u ? 1 : -1];

static struct {
    struct Mmdl *mmdl;
    struct MmdlSys *sys;
    u16 code;
    u16 species;
    s16 tx;
    s16 tz;
    int tex;            /* the carrier's VRAM address the frames went to */
    int ticks;
    u16 missing;        /* a Unova Pokemon no mod has frames for */
    u8 table_read;
} g_follow;

static u16 g_codes[FOLLOW_SPECIES_MAX];
static u8 g_tex[FOLLOW_TEX_SIZE];
static u16 g_pal[16];

/* The copy the VRAM transfer reads at the next vblank: game memory, since
 * the queue keeps a DS address.  From the field map's heap, and freed on
 * the frame after, once the transfer has run. */
static u8 *g_vram_src;
static void *g_vram_map;

static void *game_system(void)
{
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    return *(void **)bw_dsref__GameSystemGlobal;
}

/* The game's species -> object code table, read once: form 0, the male or
 * either-sex record, else the female one. */
static void read_table(void *map)
{
    const u8 *p;
    u32 size = 0;
    u32 i;
    int pass;
    void *buf;

    g_follow.table_read = 1;
    if (ARC_UTIL_LoadMemberWithSize == NULL || GFL_HEAP_Free == NULL ||
        FIELDMAP_GetHeapId == NULL) {
        return;
    }
    buf = ARC_UTIL_LoadMemberWithSize(FOLLOW_TABLE_ARC, 0, 0,
                                      FIELDMAP_GetHeapId(map), &size);
    if (buf == NULL) {
        return;
    }
    p = (const u8 *)buf;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i + 8u <= size; i += 8u) {
            u32 species = (u32)p[i] | ((u32)p[i + 1] << 8);
            u32 sex = p[i + 2];
            u32 form = p[i + 3];
            u16 code = (u16)(p[i + 6] | (p[i + 7] << 8));

            if (species > 0 && species < FOLLOW_SPECIES_MAX && form == 0 &&
                (pass == 1 || sex != 1) && g_codes[species] == 0) {
                g_codes[species] = code;
            }
        }
    }
    GFL_HEAP_Free(buf);
}

/* A mod's frames for a Unova Pokemon; 0 when no mod has them. */
static int read_sprite(u32 species)
{
    char rel[32];
    char path[1024];
    u8 buf[32 + FOLLOW_MOD_TEX_SIZE];
    FILE *f;
    size_t n;
    u32 i;

    snprintf(rel, sizeof(rel), "follow/%03u.bin", (unsigned)species);
    if (bw_data_path(path, sizeof(path), rel) != 0) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != sizeof(buf)) {
        fprintf(stderr, "follow: %s is %u bytes, not %u\n", rel,
                (unsigned)n, (unsigned)sizeof(buf));
        return 0;
    }
    for (i = 0; i < 16; i++) {
        g_pal[i] = (u16)(buf[i * 2] | (buf[i * 2 + 1] << 8));
    }
    memset(g_tex, 0, sizeof(g_tex));
    memcpy(g_tex, buf + 32, FOLLOW_MOD_TEX_SIZE);
    return 1;
}

static u32 lead_species(void *gd)
{
    void *party;
    u32 n;
    u32 i;

    if (GAME_DATA_GetMyParty == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0;
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL) {
        return 0;
    }
    n = POKEPARTY_GetCount(party);
    for (i = 0; i < n && i < 6; i++) {
        void *pp = POKEPARTY_GetMember(party, i);

        if (POKE_TOOL_GetPartyValue(pp, PARA_EGG, NULL) != 0) {
            continue;
        }
        return POKE_TOOL_GetPartyValue(pp, PARA_SPECIES, NULL);
    }
    return 0;
}

static void remove_follower(void)
{
    if (g_follow.mmdl != NULL && g_follow.sys != NULL &&
        FLDMMDL_SysSearchObjId(g_follow.sys, FOLLOW_OBJ_ID) == g_follow.mmdl) {
        FLDMMDL_DeleteMmdl(g_follow.mmdl);
    }
    g_follow.mmdl = NULL;
    g_follow.code = 0;
    g_follow.species = 0;
    g_follow.tex = 0;
}

/* A record with the follower's id that is not the port's own: the copy a
 * save kept. */
static void sweep(struct MmdlSys *sys)
{
    struct Mmdl *old = FLDMMDL_SysSearchObjId(sys, FOLLOW_OBJ_ID);

    if (old != NULL && old != g_follow.mmdl) {
        FLDMMDL_DeleteMmdl(old);
    }
}

static void create_follower(struct MmdlSys *sys, struct Mmdl *player, u16 code)
{
    static const s8 bx[4] = { 0, 0, 1, -1 };
    static const s8 bz[4] = { 1, -1, 0, 0 };
    static const u8 back[4] = { 1, 0, 3, 2 };
    FollowHeader h;
    u32 d = FLDMMDL_GetDirDisp(player) & 3u;
    int x = FLDMMDL_GetGridNowX(player);
    int z = FLDMMDL_GetGridNowZ(player);

    /* behind the player when that cell is free, else on the player */
    if (FLDMMDL_MOVE_HitCheckMoveDir(player, back[d]) == 0) {
        x += bx[d];
        z += bz[d];
    }
    memset(&h, 0, sizeof(h));
    h.obj_id = FOLLOW_OBJ_ID;
    h.obj_code = code;
    h.event_id = FOLLOW_EVENT_NONE;
    h.dir_header = (u16)d;
    h.grid_x = (u16)x;
    h.grid_z = (u16)z;
    h.pos_y = FLDMMDL_GetPosY(player);
    g_follow.mmdl = FLDMMDL_AddMmdl(sys, &h, 0);
    if (g_follow.mmdl == NULL) {
        return;
    }
    FLDMMDL_SetStatusBitZoneDeleteOff(g_follow.mmdl, 1);
    FLDMMDL_SetStatusBitFellowHit(g_follow.mmdl, 0);
    g_follow.sys = sys;
    g_follow.code = code;
    g_follow.tex = 0;
    g_follow.tx = FLDMMDL_GetGridNowX(player);
    g_follow.tz = FLDMMDL_GetGridNowZ(player);
}

/* The transfer has run: the copy goes back to the heap it came from (a
 * field map that has gone took its heap with it). */
static void free_vram_src(void *map)
{
    if (g_vram_src != NULL && map == g_vram_map) {
        GFL_HEAP_Free(g_vram_src);
    }
    g_vram_src = NULL;
}

/* The mod's frames over the carrier's texture, through the game's own
 * VRAM transfer queue; again now and then, in case the game reloads it. */
static void upload_sprite(void *map, struct MmdlSys *sys)
{
    struct BlactSys *blact = FLDMMDL_SysGetBlActCont(sys);
    struct BbActSys *bb;
    struct BbBoard *board;
    u16 slot = 0;
    u16 flags = 0;
    u16 id;
    int tex = 0;
    int pltt = 0;

    if (blact == NULL ||
        !FLDMMDL_BLACT_FindRes(blact, g_follow.code, &slot, &flags)) {
        return;
    }
    bb = FLDMMDL_BLACT_GetBbActSys(blact);
    board = bb != NULL ? BILLBOARD_ACT_GetBoard(bb) : NULL;
    if (board == NULL) {
        return;
    }
    id = BILLBOARD_ACT_GetSlotBoardId(bb, slot);
    BILLBOARD_GetImageVramAddr(board, id, &tex);
    BILLBOARD_GetImagePlttVramOffset(board, id, &pltt);
    if (tex == 0) {
        return;
    }
    if (tex == g_follow.tex && g_follow.ticks > 0) {
        g_follow.ticks--;
        return;
    }
    if (g_vram_src != NULL) {
        return;             /* the last one is still queued */
    }
    g_vram_src = (u8 *)GFL_HEAP_AllocMemory(FIELDMAP_GetHeapId(map),
                                            FOLLOW_TEX_SIZE + sizeof(g_pal));
    g_vram_map = map;
    if (g_vram_src == NULL) {
        return;
    }
    memcpy(g_vram_src, g_tex, FOLLOW_TEX_SIZE);
    memcpy(g_vram_src + FOLLOW_TEX_SIZE, g_pal, sizeof(g_pal));
    if (NNS_GfdRegisterNewVramTransferTask(0, (u32)tex, g_vram_src,
                                           FOLLOW_TEX_SIZE) &&
        NNS_GfdRegisterNewVramTransferTask(1, (u32)pltt,
                                           g_vram_src + FOLLOW_TEX_SIZE,
                                           (s32)sizeof(g_pal))) {
        g_follow.tex = tex;
        g_follow.ticks = FOLLOW_REUPLOAD;
    }
}

static void step(struct Mmdl *player)
{
    int nx = FLDMMDL_GetGridNowX(player);
    int nz = FLDMMDL_GetGridNowZ(player);
    int ox = FLDMMDL_GetGridOldX(player);
    int oz = FLDMMDL_GetGridOldZ(player);
    int dx = ox - FLDMMDL_GetGridNowX(g_follow.mmdl);
    int dz = oz - FLDMMDL_GetGridNowZ(g_follow.mmdl);
    int dist = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);

    if (nx == g_follow.tx && nz == g_follow.tz) {
        return;
    }
    g_follow.tx = (s16)nx;
    g_follow.tz = (s16)nz;
    if (dist == 1 && FLDMMDL_CheckEndAcmd(g_follow.mmdl)) {
        /* the player's own walk, run or bicycle speed */
        u32 c = FLDMMDL_GetAcmdCode(player);
        u32 base = (c >= 0x10u && c < 0x18u) ? (c & ~3u) : FOLLOW_ACMD_WALK;
        int dir = dz < 0 ? 0 : dz > 0 ? 1 : dx < 0 ? 2 : 3;

        FLDMMDL_EndAcmd(g_follow.mmdl);
        if (FLDMMDL_CheckPossibleAcmd(g_follow.mmdl)) {
            FLDMMDL_SetAcmd(g_follow.mmdl,
                            (u16)FLDMMDL_ACMD_ConvertCodeByDir(dir, (int)base));
        }
    } else if (dist != 0) {
        FLDMMDL_EndAcmd(g_follow.mmdl);
        FLDMMDL_InitGridPosition(g_follow.mmdl, (unsigned int)ox,
                                 (unsigned int)(FLDMMDL_GetPosY(player) >> 16),
                                 (unsigned int)oz, FLDMMDL_GetDirDisp(player));
    }
}

/* Surfing or diving: the follower waits in its Ball. */
static int surfing(void *map)
{
    void *player = FIELDMAP_GetFieldPlayer(map);

    return player != NULL && FIELD_PLAYER_GetFormCode(player) >= FOLLOW_FORM_SURF;
}

void bw_follow_frame(void)
{
    void *gs = game_system();
    void *map;
    void *gd;
    struct MmdlSys *sys;
    struct Mmdl *player;
    u32 species;
    u16 code;

    if (gs == NULL || GAMESYSTEM_GetFieldMap == NULL ||
        GAMESYSTEM_GetEvent == NULL || GAME_DATA_GetOccupyInfo == NULL ||
        FLDMMDL_SysSearchObjId == NULL) {
        return;
    }
    map = GAMESYSTEM_GetFieldMap(gs);
    if (g_vram_src != NULL) {
        free_vram_src(map);
    }
    if (map == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        return;             /* a warp, a menu, a script: leave it be */
    }
    gd = GAMESYSTEM_GetGameData(gs);
    sys = (struct MmdlSys *)GAME_DATA_GetOccupyInfo(gd);
    if (sys == NULL) {
        return;
    }
    if (sys != g_follow.sys ||
        FLDMMDL_SysSearchObjId(sys, FOLLOW_OBJ_ID) != g_follow.mmdl) {
        g_follow.mmdl = NULL;   /* the game removed it (a map change) */
        g_follow.sys = sys;
    }
    sweep(sys);
    player = FLDMMDL_SysSearchObjId(sys, 0xFFu);
    if (eidos_settings_get == NULL ||
        eidos_settings_get(OPT_FOLLOW_POKEMON) == 0 || player == NULL ||
        FIELDMAP_GetMoveKind(map) != 0 ||
        FLDMMDL_CheckStatusBitVanish(player) || surfing(map)) {
        remove_follower();
        return;
    }
    if (!g_follow.table_read) {
        read_table(map);
    }
    species = lead_species(gd);
    if (species == 0 || species >= FOLLOW_SPECIES_MAX) {
        remove_follower();
        return;
    }
    if (species >= FOLLOW_GEN5_FIRST) {
        if (species == g_follow.missing) {
            remove_follower();
            return;
        }
        if (species != g_follow.species && !read_sprite(species)) {
            remove_follower();
            g_follow.missing = (u16)species;
            return;
        }
        code = g_codes[FOLLOW_CARRIER];
    } else {
        code = g_codes[species];
    }
    if (code == 0) {
        remove_follower();
        return;
    }
    if (g_follow.mmdl == NULL) {
        create_follower(sys, player, code);
        if (g_follow.mmdl == NULL) {
            return;
        }
    }
    if (FLDMMDL_CheckStatusBitVanish(g_follow.mmdl)) {
        FLDMMDL_SetStatusBitVanish(g_follow.mmdl, 0);
    }
    if (code != g_follow.code) {
        FLDMMDL_ChangeObjCode(g_follow.mmdl, code);
        g_follow.code = code;
        g_follow.tex = 0;
    }
    if (species != g_follow.species) {
        g_follow.species = (u16)species;
        g_follow.tex = 0;
    }
    if (species >= FOLLOW_GEN5_FIRST) {
        upload_sprite(map, sys);
    }
    step(player);
}
