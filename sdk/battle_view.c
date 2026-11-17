/*
 * sdk/battle_view.c -- the one-screen layout's battle backdrop, game side.
 *
 * A battle draws its scenery with three G3D_SYSTEM_DrawObject calls a frame
 * (BTLV_STAGE_DrawLayers: the own and the foe's platform; BTLV_FIELD_Draw:
 * the disc and sky dome).  DrawObject is wrapped at link time (-Wl,--wrap,
 * CMakeLists.txt).  In the one-screen layout with the Real map backdrop:
 *
 *   - the stage draws are skipped unless Battle platforms is On;
 *   - the field draw is skipped, and the battle's camera and projection are
 *     read at that point (the game has loaded them already) for the frame's
 *     backdrop.  A frame in which the game does not draw the field (an
 *     effect hid it, BTLV_FIELD_SetHidden) has no backdrop either.
 *
 * At the first frame of a battle the place where it began (zone, position,
 * facing in GameData, season and time in the battle's setup) goes to a
 * worker thread that builds the map (bmap_build) and places it in battle
 * space (bmap_place).  Until it is ready the frame has a plain sky.  The
 * hook on the snapshot (g3d_hw_set_backdrop_hook) hands the frame's camera
 * and the scene to the renderer; a scene stays alive through the next
 * battle too, so a snapshot still in flight never reads freed memory.
 *
 * The game's memory is read as the port's recipes do: the DS address space is
 * identity mapped, so a game pointer is a host pointer.  Everything here runs
 * on the game's thread, except the worker, which touches no game memory.
 *
 * Linked into eidos_boot only, beside the wrap options.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "battle_view.h"
#include "bmap.h"
#include "bmap_place.h"
#include "bmap_spots.h"
#include "dsmem.h"
#include "hw3d.h"
#include "input.h"
#include "mod_state.h"
#include "overlay.h"
#include "plat_thread.h"
#include "settings.h"

#define WEAK __attribute__((weak))

extern void *bw_dsref__GameSystemGlobal WEAK;

void __real_G3D_SYSTEM_DrawObject(void *obj, void *status);

#define DS_GBTLVEFF 0x0220AF20u     /* ov094: the battle effect work        */

/* gBtlvEff (BtlvEff) */
#define EFF_MCSS    0x190u          /* BtlvMcssWork *                       */
#define EFF_STAGE   0x194u          /* BtlvStage *                          */
#define EFF_FIELD   0x198u          /* BtlvField *                          */
#define EFF_CAMERA  0x19Cu          /* BtlvCamera *                         */
#define EFF_MAIN    0x1E4u          /* setup.battle: BtlMain *              */
/* BtlvMcssWork: 14 sprites of 0x5C bytes from +0x08; a sprite has its
 * McssWork at +0x00 (NULL: free) and its position slot at +0x48 (0..7 the
 * Pokemon, 8.. the trainers); McssWork holds its fx32 position at +0xE0 */
#define MCSS_SPRITES 0x08u
#define MCSS_STRIDE  0x5Cu
#define MCSS_COUNT   14u
#define MSPR_POS     0x48u
#define MCSSW_POS    0xE0u
#define MCSSW_TEX    0x9Cu          /* texImage.vramAddr[0]                 */
#define MCSSW_PLTT   0xC8u          /* plttProxy.vramAddr[0]                */
/* BtlvCamera */
#define CAM_G3D     0x04u           /* the G3D camera object                */
/* the G3D camera object: a G3dProjection, then three vectors            */
#define G3C_TYPE    0x00u
#define G3C_SIN     0x04u
#define G3C_COS     0x08u
#define G3C_ASPECT  0x0Cu
#define G3C_NEAR    0x14u
#define G3C_FAR     0x18u
#define G3C_EYE     0x20u
#define G3C_UP      0x2Cu
#define G3C_TARGET  0x38u
/* BtlvStage, BtlvField */
#define STAGE_OBJ0  0x24u
#define STAGE_OBJ1  0x28u
#define FIELD_OBJ   0x18u
/* BtlMain +0 is the BattleSetupParam; its fieldSituation starts at +8 */
#define SIT_SEASON  17u             /* u8                                   */
#define SIT_ZONE    18u             /* u16                                  */
#define SIT_HOUR    20u             /* u8                                   */
#define SIT_MINUTE  21u             /* u8                                   */
/* BattleSetupParam.trainerData[1], the opponent's BattleTrainerData *
 * (after competitor, rule, the 16-byte fieldSituation, the two BGM ids,
 * netHandle, four bytes of net state, party[4], playerStatus[4] and
 * voiceFlags[4]); its first word is the trainer's number */
#define SETUP_TRAINER1 0x4Cu
/* GameData: the player's work (GAME_DATA_GetMyPlayerWork) */
#define GD_PLAYER   (4u + 4u * 0x44u)
#define PW_ZONE     0x00u           /* u16                                  */
#define PW_X        0x04u           /* fx32                                 */
#define PW_Y        0x08u           /* fx32                                 */
#define PW_Z        0x0Cu           /* fx32                                 */
#define PW_FACING   0x18u           /* u16                                  */

/* ov094's slot position tables (BTLV_MCSS_CalcSlotPosition): fx32 x, y, z
 * a slot.  Every sprite placement, move animation target and reset reads
 * them, so moving a slot here moves all of it.  Even positions are the
 * player's side, odd ones the foe's. */
static const struct {
    uint32_t addr;
    uint32_t count;
} k_slot_tables[] = {
    { 0x0220A050u, 2u },            /* single: positions 0, 1               */
    { 0x0220A080u, 4u },            /* double: 2 to 5                       */
    { 0x0220A158u, 6u },            /* triple: 2 to 7                       */
    { 0x0220A1A0u, 6u },            /* rotation: 2 to 7                     */
    { 0x0220A110u, 6u },            /* the trainers: 8 to 13                */
};
#define SLOT_TABLES (sizeof k_slot_tables / sizeof k_slot_tables[0])
#define SLOT_TRAINERS 4u            /* the trainers' table above */
#define SLOT_ENTRIES 24u

enum { OBJ_OTHER, OBJ_STAGE, OBJ_FIELD };

/* A built map, not placed: kept so a placement can be tried again (the
 * spot editor) without a new build.  Counted references: the newest base
 * (g_base), each placed scene made from it, a job placing from it. */
typedef struct BvBase {
    BmapScene s;
    unsigned zone;
    float cx, cz;                   /* built around (map units)            */
    int refs;
} BvBase;

/* A placed scene: a copy of a base's vertices, its textures the base's.
 * The scene comes first: the renderer is handed &p->s. */
typedef struct BvPlaced {
    BmapScene s;
    BvBase *base;
} BvPlaced;

/* One battle's scene, built and placed off the game's thread. */
typedef struct BvJob {
    BvBase *base;                   /* in: place from it; NULL: build one  */
    BvBase *built;                  /* out: the base built                 */
    BvPlaced *placed;               /* out                                 */
    unsigned zone, facing;
    float px, py, pz;
    int has_py;                     /* py is known (the ground nearest it) */
    int season, hour, minute;
    int trainer;                    /* a trainer battle (the setup's competitor) */
    int lesson;                     /* the catching lesson (competitor 4) */
    int trainer_id;                 /* the opponent's trainer number, 0 unknown */
    BmapPlaceSpec spec;
    int rc;
    volatile int done;
    BwThread *thread;
    int reach;                      /* EIDOS_DEBUG_BMAP_AT: 0 the table, < 0 the
                                       spot as given, > 0 a search that far */
    BmapWfbc wfbc;                  /* Black City / White Forest: the save's blocks */
    int has_wfbc;
    BmapFieldMatrix fmat;           /* the field's matrix, after the game's map replacement */
    int has_fmat;
} BvJob;

typedef struct BvFrame {
    int valid;                      /* the field was drawn this frame      */
    float eye[3], up[3], target[3];
    float fsin, fcos, aspect, znear, zfar;
    float poke[G3D_BACKDROP_SPRITES][4]; /* the Pokemon's and trainers' sprites:
                                       where, and their box's height       */
    uint32_t key[G3D_BACKDROP_SPRITES][2];  /* their texture and palette (G3dBackdrop.key) */
    uint32_t n_poke;
} BvFrame;

static BvFrame g_frame;
static void camera_clearance(G3dBackdrop *out);

/*
 * Where each Pokemon was drawn (bw_battle_view_mon_box): the frame's
 * polygons that carry a sprite's own texture and palette (its shadow has
 * the shadow palette), their extent in the 3D picture.  The sprites are
 * listed at the field draw (capture) and measured when the snapshot is
 * complete (snapshot_hook), both on the game's thread; the boxes are read
 * by the interface, a frame late at worst.
 */
typedef struct BvSprite {
    uint32_t pos, tex, pltt;
} BvSprite;

static BvSprite g_sprites[8];
static uint32_t g_n_sprites;
static float g_box[8][4];
static uint32_t g_box_frame[8];
static int g_cam_on;                /* the battle camera is known (G3D_CAMERA_Reset) */
static float g_cam_game[2][3], g_cam_drawn[2][3];
/* The map is shown from the battle's intro on: g_show 0 until the game's camera first leaves its place of rest (the
 * intro's first view), or SHOW_AFTER frames have passed since the battle began (a battle whose intro starts at rest). */
#define SHOW_AFTER 40u
static int g_show;
static uint32_t g_begin_frame;
static BvJob *g_job;                /* running or just finished            */
static BvPlaced *g_cur, *g_old;     /* placed scenes: current, the one before */
static BvBase *g_base;              /* the newest base                     */
static uint32_t g_gen;
static uint32_t g_last_frame;       /* the last frame a battle draw was seen */
static int g_in_battle;            /* a battle was seen (stays set: battle_on() is now) */
static int g_hooked;
/* the one-screen battle on the game's own backdrop: no map, only the
 * effects' 2D sprites go into the 3D (effect_sprites) */
static int g_sprites_only;
static int g_log = -1;

static int gptr_ok(uintptr_t a)
{
    return bw_ds_game_ptr((const void *)a) && (a & 3u) == 0;
}

static uint32_t rd32(uintptr_t base, uint32_t off)
{
    uint32_t v;

    memcpy(&v, (const void *)(base + off), sizeof(v));
    return v;
}

static uint32_t rd16(uintptr_t base, uint32_t off)
{
    uint16_t v;

    memcpy(&v, (const void *)(base + off), sizeof(v));
    return v;
}

static uint32_t rd8(uintptr_t base, uint32_t off)
{
    return *(const uint8_t *)(base + off);
}

static float rdfx(uintptr_t base, uint32_t off)
{
    return (float)(int32_t)rd32(base, off) / 4096.0f;
}

static int log_on(void)
{
    if (g_log < 0) {
        g_log = getenv("EIDOS_BATTLE_VIEW_LOG") != NULL;
    }
    return g_log;
}

extern uint8_t GAME_DATA_GetSeason(const void *gameData) __attribute__((weak));

static int real_map(void)
{
    return eidos_ui_active()
           && eidos_settings_get(EIDOS_OPT_BATTLE_BACKDROP) == 0;
}

/* A battle is on now: one of its draws in the last 8 frames.  (g_in_battle alone stays set from the first battle on:
 * the field's objects and maps were noted only until a session's first battle.) */
static int battle_on(void)
{
    return g_in_battle && bw_input_frame_number() - g_last_frame <= 8u;
}

int bw_battle_view_active(void)
{
    return battle_on() && real_map();
}

/* For scripted captures (EIDOS_UI_SCRIPT `wait battle_map`): 1 once this battle's map is built, placed and taken over
 * (the worker done, its scene current), so a picture never shows the battle before its map. */
int bw_battle_view_ready(void)
{
    return battle_on() && g_job == NULL && g_cur != NULL;
}

/*
 * The part of a sprite polygon its texture fills (alpha above 0), on screen
 * (NDC): the texels' box in s, t, carried to the screen by the affine map
 * the polygon's first three corners give (a sprite cell is a parallelogram,
 * so the map is exact).  0 when the polygon shows no texel.
 */
static int poly_opaque(const G3dSnapshot *s, uint32_t pi, float *x0, float *y0, float *x1, float *y1)
{
    const G3dPoly *p = &s->polys[pi];
    const G3dVertex *v[4];
    const G3dSnapTex *t;
    float px[3], py[3], ps[3], pt[3], det, ax, bx, ay, by;
    int smin, smax, tmin, tmax, os0 = 1 << 30, os1 = -1, ot0 = 1 << 30, ot1 = -1, x, y, k;
    uint32_t j;

    if (p->count < 3 || s->poly_tex == NULL || s->poly_tex[pi] < 0) {
        return 0;
    }
    t = &s->tex[s->poly_tex[pi]];
    for (j = 0; j < p->count && j < 4u; j++) {
        v[j] = &s->verts[s->idx[p->first + j]];
        if (v[j]->w <= 0) {
            return 0;
        }
    }
    for (k = 0; k < 3; k++) {
        px[k] = (float)v[k]->x / (float)v[k]->w;
        py[k] = (float)v[k]->y / (float)v[k]->w;
        ps[k] = (float)v[k]->s / 16.0f;
        pt[k] = (float)v[k]->t / 16.0f;
    }
    det = (ps[1] - ps[0]) * (pt[2] - pt[0]) - (ps[2] - ps[0]) * (pt[1] - pt[0]);
    if (det > -1e-6f && det < 1e-6f) {
        return 0;
    }
    smin = smax = (int)ps[0];
    tmin = tmax = (int)pt[0];
    for (j = 0; j < p->count && j < 4u; j++) {
        int sv = v[j]->s / 16, tv = v[j]->t / 16;

        smin = sv < smin ? sv : smin;
        smax = sv > smax ? sv : smax;
        tmin = tv < tmin ? tv : tmin;
        tmax = tv > tmax ? tv : tmax;
    }
    if (smin < 0 || tmin < 0 || smax > (int)t->w || tmax > (int)t->h || t->rgba == NULL) {
        return 0;
    }
    for (y = tmin; y < tmax; y++) {
        for (x = smin; x < smax; x++) {
            if (t->rgba[((size_t)y * t->w + (size_t)x) * 4u + 3u] != 0) {
                os0 = x < os0 ? x : os0;
                os1 = x > os1 ? x : os1;
                ot0 = y < ot0 ? y : ot0;
                ot1 = y > ot1 ? y : ot1;
            }
        }
    }
    if (os1 < 0) {
        return 0;
    }
    /* screen = P0 + a (s - s0) + b (t - t0), per axis */
    ax = ((px[1] - px[0]) * (pt[2] - pt[0]) - (px[2] - px[0]) * (pt[1] - pt[0])) / det;
    bx = ((ps[1] - ps[0]) * (px[2] - px[0]) - (ps[2] - ps[0]) * (px[1] - px[0])) / det;
    ay = ((py[1] - py[0]) * (pt[2] - pt[0]) - (py[2] - py[0]) * (pt[1] - pt[0])) / det;
    by = ((ps[1] - ps[0]) * (py[2] - py[0]) - (ps[2] - ps[0]) * (py[1] - py[0])) / det;
    for (k = 0; k < 4; k++) {
        float cs = (float)((k & 1) ? os1 + 1 : os0) - ps[0], ct = (float)((k & 2) ? ot1 + 1 : ot0) - pt[0];
        float sx = px[0] + ax * cs + bx * ct, sy = py[0] + ay * cs + by * ct;

        *x0 = sx < *x0 ? sx : *x0;
        *x1 = sx > *x1 ? sx : *x1;
        *y0 = sy < *y0 ? sy : *y0;
        *y1 = sy > *y1 ? sy : *y1;
    }
    return 1;
}

static void field_maps_frame(void);

static void snapshot_hook(const G3dSnapshot *s)
{
    uint32_t k, i, now = bw_input_frame_number();

    field_maps_frame();                 /* (the field's maps, every frame it draws: note_field_maps) */

    for (k = 0; k < g_n_sprites; k++) {
        const BvSprite *sp = &g_sprites[k];
        uint32_t tex = (sp->tex >> 3) & 0xFFFFu;
        float x0 = 2.0f, y0 = 2.0f, x1 = -2.0f, y1 = -2.0f;

        if (sp->tex == 0xFFFFFFFFu) {
            continue;
        }
        for (i = 0; i < s->n_polys; i++) {
            const G3dPoly *p = &s->polys[i];

            if ((p->teximage & 0xFFFFu) != tex ||
                (p->pltt_base != (sp->pltt >> 4) && p->pltt_base != (sp->pltt >> 3))) {
                continue;
            }
            (void)poly_opaque(s, i, &x0, &y0, &x1, &y1);
        }
        if (x1 > x0 && y1 > y0) {
            g_box[sp->pos][0] = (x0 + 1.0f) * 0.5f;
            g_box[sp->pos][1] = (1.0f - y1) * 0.5f;
            g_box[sp->pos][2] = (x1 + 1.0f) * 0.5f;
            g_box[sp->pos][3] = (1.0f - y0) * 0.5f;
            g_box_frame[sp->pos] = now;
        }
    }
}

int bw_battle_view_mon_box(int pos, float box[4])
{
    if (pos < 0 || pos > 7 || !bw_battle_view_active() ||
        bw_input_frame_number() - g_box_frame[pos] > 4u) {
        return 0;
    }
    memcpy(box, g_box[pos], sizeof g_box[pos]);
    return 1;
}

/*
 * The effects' 2D sprites (BTLV_CLACT: the ball that shakes on the ground
 * after a throw, a few moves' sprites).  The game draws them as OBJs at the
 * screen point it projects from a Pokemon's place; the one-screen battle
 * does not show the game's 2D (it draws the plates and the message itself),
 * so each one goes into the 3D instead, as a quad at its screen point and at
 * the depth of the Pokemon it stands by: it sits where the game puts it, and
 * the map in front of that Pokemon covers it as it covers the Pokemon.
 */
#define EFF_CLACT          0x1A0u   /* BtlvEffect.clact: BtlvClact *          */
#define CLACT_SLOT0        0x08u    /* BtlvClact.slot[7], the ClactActor first */
#define CLACT_SLOT_STRIDE  0x10u
#define CLACT_SLOT_COUNT   7u
#define EFFECT_SPRITE_BOX  32       /* the part of the OBJs taken around one  */

typedef struct BvPt16 { int16_t x, y; } BvPt16;
extern void CLACT_GetActorPos(const void *actor, BvPt16 *out) __attribute__((weak));
/* engine/g2d/hwcompose.c: a screen's OBJs alone, 256 x 192 RGBA */
int g2d_hw_objs_dsmem(int screen, uint8_t *out);

/* A point's clip coordinates under the frame's camera (the game's own
 * projection, the view's widening on x and y). */
static void project_clip(const float p[3], float kx, float ky, float out[4])
{
    float f[3], sd[3], u[3], d[3], l, cot, xv, yv, zf, n = g_frame.znear, fr = g_frame.zfar;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = g_frame.target[i] - g_frame.eye[i];
        d[i] = p[i] - g_frame.eye[i];
    }
    l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (i = 0; i < 3; i++) {
        f[i] /= l > 1e-6f ? l : 1.0f;
    }
    sd[0] = f[1] * g_frame.up[2] - f[2] * g_frame.up[1];
    sd[1] = f[2] * g_frame.up[0] - f[0] * g_frame.up[2];
    sd[2] = f[0] * g_frame.up[1] - f[1] * g_frame.up[0];
    l = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
    for (i = 0; i < 3; i++) {
        sd[i] /= l > 1e-6f ? l : 1.0f;
    }
    u[0] = sd[1] * f[2] - sd[2] * f[1];
    u[1] = sd[2] * f[0] - sd[0] * f[2];
    u[2] = sd[0] * f[1] - sd[1] * f[0];
    xv = d[0] * sd[0] + d[1] * sd[1] + d[2] * sd[2];
    yv = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    zf = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    cot = g_frame.fcos / g_frame.fsin;
    out[0] = cot / g_frame.aspect * xv * kx;
    out[1] = cot * yv * ky;
    out[2] = (fr + n) / (fr - n) * zf - 2.0f * fr * n / (fr - n);
    out[3] = zf;
}

static void effect_sprites(void)
{
    static uint8_t objs[256 * 192 * 4], img[EFFECT_SPRITE_BOX * EFFECT_SPRITE_BOX * 4];
    uint32_t *eslot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);
    uintptr_t eff = eslot != NULL ? *eslot : 0, cl;
    s32 kxf, kyf;
    float kx, ky;
    int have = 0;
    uint32_t i;

    if (!gptr_ok(eff) || CLACT_GetActorPos == NULL || g_frame.n_poke == 0) {
        return;
    }
    cl = rd32(eff, EFF_CLACT);
    if (!gptr_ok(cl)) {
        return;
    }
    g3d_hw_view_scale(&kxf, &kyf);
    kx = (float)kxf / 4096.0f;
    ky = (float)kyf / 4096.0f;
    for (i = 0; i < CLACT_SLOT_COUNT; i++) {
        uintptr_t actor = rd32(cl, CLACT_SLOT0 + CLACT_SLOT_STRIDE * i);
        BvPt16 pt;
        float sx, sy, best = 1e30f, at[4] = { 0, 0, 0, 1 };
        int x, y, any = 0, x0, y0;
        uint32_t k;
        s32 q[4][4];

        if (!gptr_ok(actor)) {
            continue;
        }
        if (!have) {
            if (g2d_hw_objs_dsmem(0, objs) != 0) {
                return;
            }
            have = 1;
        }
        CLACT_GetActorPos((const void *)actor, &pt);
        x0 = pt.x - EFFECT_SPRITE_BOX / 2;
        y0 = pt.y - EFFECT_SPRITE_BOX / 2;
        for (y = 0; y < EFFECT_SPRITE_BOX; y++) {
            for (x = 0; x < EFFECT_SPRITE_BOX; x++) {
                uint8_t *o = img + (y * EFFECT_SPRITE_BOX + x) * 4;
                int X = x0 + x, Y = y0 + y;

                if (X < 0 || Y < 0 || X >= 256 || Y >= 192 || objs[(Y * 256 + X) * 4 + 3] == 0) {
                    memset(o, 0, 4);
                    continue;
                }
                memcpy(o, objs + (Y * 256 + X) * 4, 4);
                o[3] = 255;
                any = 1;
            }
        }
        if (!any) {
            continue;
        }
        /* the depth: the Pokemon whose place is nearest on screen */
        sx = ((float)pt.x - 128.0f) / 128.0f * kx;
        sy = (96.0f - (float)pt.y) / 96.0f * ky;
        for (k = 0; k < g_frame.n_poke; k++) {
            float c[4], dx, dy;

            project_clip(g_frame.poke[k], kx, ky, c);
            if (c[3] <= 0.05f) {
                continue;
            }
            dx = c[0] / c[3] - sx;
            dy = c[1] / c[3] - sy;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                memcpy(at, c, sizeof at);
            }
        }
        if (best >= 1e30f) {
            continue;
        }
        for (k = 0; k < 4; k++) {
            float nx = ((float)(x0 + ((k == 1 || k == 2) ? EFFECT_SPRITE_BOX : 0)) - 128.0f) / 128.0f * kx;
            float ny = (96.0f - (float)(y0 + (k >= 2 ? EFFECT_SPRITE_BOX : 0))) / 96.0f * ky;

            q[k][0] = (s32)(nx * at[3] * 4096.0f);
            q[k][1] = (s32)(ny * at[3] * 4096.0f);
            q[k][2] = (s32)(at[2] * 4096.0f);
            q[k][3] = (s32)(at[3] * 4096.0f);
        }
        (void)g3d_hw_snapshot_add_quad(q, img, EFFECT_SPRITE_BOX, EFFECT_SPRITE_BOX);
    }
}

/* The renderer's side: the frame's camera and the scene. */
static void backdrop_hook(G3dBackdrop *out)
{
    if (!g_frame.valid) {
        return;
    }
    g_frame.valid = 0;
    if (g_sprites_only) {
        effect_sprites();               /* the game's own backdrop: no scene */
        return;
    }
    out->valid = 1;
    memcpy(out->eye, g_frame.eye, sizeof(out->eye));
    memcpy(out->up, g_frame.up, sizeof(out->up));
    memcpy(out->target, g_frame.target, sizeof(out->target));
    out->fovy_sin = g_frame.fsin;
    out->fovy_cos = g_frame.fcos;
    out->aspect = g_frame.aspect;
    out->znear = g_frame.znear;
    out->zfar = g_frame.zfar;
    out->scene = g_cur != NULL ? &g_cur->s : NULL;
    out->scene_gen = g_gen;
    if (!g_show && out->scene != NULL) {
        /* (the game's camera at rest: BTLV_CAMERA_GetDefaultEyeAndTarget, eye 6.7 6.7 17.3) */
        const int moved = g_cam_on && (fabsf(g_cam_game[0][0] - 6.7f) > 0.05f || fabsf(g_cam_game[0][1] - 6.7f) > 0.05f
                                       || fabsf(g_cam_game[0][2] - 17.3f) > 0.05f);

        if (moved || bw_input_frame_number() - g_begin_frame > SHOW_AFTER) {
            g_show = 1;
        } else {
            out->scene = NULL;          /* (the game's own dark comes next: nothing of the map before it) */
        }
    }
    memcpy(out->poke, g_frame.poke, sizeof(out->poke));
    memcpy(out->key, g_frame.key, sizeof(out->key));
    out->n_poke = g_frame.n_poke;
    camera_clearance(out);
    if (g_cur != NULL && out->scene != NULL) {
        memcpy(out->sky, g_cur->s.clear, sizeof(out->sky));
    }
    effect_sprites();
}

/*
 * The camera's clearance (G3dBackdrop.clear): a wall, a door or a curtain the
 * camera stands in or just behind is not drawn.  Each frame the camera's
 * place counts with a radius of half the nearest sprite's distance (the
 * Pokemon and the trainers on the field; the player's own trainer at the
 * send-out stands before the camera and is never covered anyway).  The
 * places the camera passed are kept for CLEAR_KEEP frames (a new one each
 * unit it moves), so a wall cleared while the camera was near it stays gone
 * as the camera pulls away from it, and comes back once the camera has left
 * it a while (grass the intro's camera brushed past is back for the command
 * camera); a cut (more than CLEAR_CUT units in a frame) drops them all.
 * EIDOS_NO_CAMCLEAR=1 leaves every wall drawn.
 *
 * A tree goes whole: a tree of the scene (BmapScene.trees) that one of those
 * places lies in or right beside is left out entirely (bmap_clear_trees: the
 * two nearest at most), as an entry of its own with a negative radius: its
 * place on the ground, its foot's height and its width.  A ball cut out of a
 * tree's crown left the rest of the tree hanging round the hole.
 */
#define CLEAR_CUT 3.0f
#define CLEAR_KEEP 120u
static float g_clr[G3D_BACKDROP_CLEAR][4], g_clr_eye[3];
static uint32_t g_clr_when[G3D_BACKDROP_CLEAR], g_clr_now;
/* The trees taken out so far this battle (bmap_clear_trees' entries: x, tag, z, -width): each stays out until the
 * battle ends, whichever camera first reached it. */
#define TREES_KEPT 26u
static float g_tree_out[TREES_KEPT][4];
static unsigned g_ntree_out;

static int g_tree_seeded;           /* the battle's trees are chosen (once a battle) */
static float dist3(const float *a, const float *b);
static unsigned g_nclr, g_clr_at;
static int g_clr_off = -1;

static float dist3(const float *a, const float *b)
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];

    return sqrtf(x * x + y * y + z * z);
}

static void camera_clearance(G3dBackdrop *out)
{
    float nearest = 1e30f, r;
    unsigned i, n = 0;

    if (g_clr_off < 0) {
        g_clr_off = getenv("EIDOS_NO_CAMCLEAR") != NULL;
    }
    if (g_clr_off) {
        return;
    }
    g_clr_now++;
    for (i = 0; i < out->n_poke && i < G3D_BACKDROP_SPRITES; i++) {
        if (out->poke[i][3] > 0.0f) {
            const float d = dist3(out->poke[i], out->eye);

            nearest = d < nearest ? d : nearest;
        }
    }
    if (g_nclr > 0 && dist3(out->eye, g_clr_eye) > CLEAR_CUT) {
        g_nclr = 0;                     /* (a cut: a new shot) */
    }
    memcpy(g_clr_eye, out->eye, sizeof g_clr_eye);
    if (nearest > 1e29f) {
        out->n_clear = 0;
        return;
    }
    r = 0.5f * nearest;
    /* the place now, always */
    memcpy(out->clear[n], out->eye, 3 * sizeof(float));
    out->clear[n][3] = r;
    n++;
    /* the places passed, while they are fresh (the newest first) */
    for (i = 0; i < g_nclr && n < G3D_BACKDROP_CLEAR; i++) {
        const unsigned k = (g_clr_at + G3D_BACKDROP_CLEAR - 1u - i) % G3D_BACKDROP_CLEAR;

        if (g_clr_now - g_clr_when[k] <= CLEAR_KEEP) {
            memcpy(out->clear[n], g_clr[k], 4 * sizeof(float));
            n++;
        }
    }
    {
        /* The trees.  The battle's own set, chosen once from the place's two still cameras (bmap_place_trees: what
         * the spot editor shows), stays out the whole battle; nothing joins it and nothing leaves it (a tree that
         * went only while a camera move passed through it popped out and back). */
        const BmapScene *sc = (const BmapScene *)out->scene;
        unsigned k;

        if (sc != NULL && !g_tree_seeded) {
            g_tree_seeded = 1;
            g_ntree_out = bmap_place_trees(sc, g_tree_out, TREES_KEPT);
            if (getenv("BMAP_TREE_LOG") != NULL) {
                fprintf(stderr, "trees: %u out for this battle (frame %u)\n", g_ntree_out, (unsigned)bw_input_frame_number());
            }
        }
        if (n + g_ntree_out > G3D_BACKDROP_CLEAR) {
            n = G3D_BACKDROP_CLEAR - g_ntree_out;       /* (the oldest places make room) */
        }
        for (k = 0; k < g_ntree_out; k++) {
            memcpy(out->clear[n++], g_tree_out[k], 4 * sizeof(float));
        }
    }
    out->n_clear = n;
    /* this place, remembered once the camera has moved a unit from the last one kept */
    if (g_nclr == 0 || dist3(out->eye, g_clr[(g_clr_at + G3D_BACKDROP_CLEAR - 1u) % G3D_BACKDROP_CLEAR]) > 1.0f) {
        memcpy(g_clr[g_clr_at], out->eye, 3 * sizeof(float));
        g_clr[g_clr_at][3] = r;
        g_clr_when[g_clr_at] = g_clr_now;
        g_clr_at = (g_clr_at + 1u) % G3D_BACKDROP_CLEAR;
        g_nclr = g_nclr < G3D_BACKDROP_CLEAR ? g_nclr + 1u : g_nclr;
    }
}

static void base_release(BvBase *b)
{
    if (b != NULL && --b->refs == 0) {
        bmap_free(&b->s);
        free(b);
    }
}

static void job_main(void *arg)
{
    BvJob *j = (BvJob *)arg;
    BvBase *b = j->base;
    BvPlaced *p;
    clock_t t_job = clock();       /* research: how long the battle waits for its map */

    j->rc = 0;
    if (b == NULL) {
        b = (BvBase *)calloc(1, sizeof *b);
        if (b == NULL) {
            j->rc = -1;
            j->done = 1;
            return;
        }
        bmap_set_wfbc(j->has_wfbc ? &j->wfbc : NULL);     /* else a full place */
        bmap_set_field_matrix(j->has_fmat ? &j->fmat : NULL);  /* else the zone's own from the files */
        j->rc = bmap_build(&b->s, j->zone, j->px, j->pz, j->season, j->hour, j->minute);
        if (j->rc != 0) {
            free(b);
            j->done = 1;
            return;
        }
        if (j->has_py) {
            bmap_set_ref(&b->s, j->py);
        }
        b->zone = j->zone;
        b->cx = j->px;
        b->cz = j->pz;
        j->built = b;
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: built in %.0f ms\n", 1000.0 * (double)(clock() - t_job) / CLOCKS_PER_SEC);
        }
    }
    p = (BvPlaced *)calloc(1, sizeof *p);
    if (p == NULL || bmap_copy(&p->s, &b->s) != 0) {
        free(p);
        j->rc = -1;
        j->done = 1;
        return;
    }
    p->base = b;
    bmap_place_spec(&p->s, &j->spec);
    j->placed = p;
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        fprintf(stderr, "bmap: ready in %.0f ms\n", 1000.0 * (double)(clock() - t_job) / CLOCKS_PER_SEC);
    }
    j->done = 1;
}

/* Black City and White Forest: the blocks the field built from the save
 * (FIELDMAP_SetupWfbc: the union info by the field's map mode; WfbcData
 * cityType +0x02 (0 Black City, 1 White Forest), playerId +0x04, ten residents of 0x18 bytes from +0x08: present in the
 * low nibble of +0x00, the record +0x01, mood +0x02, and when the high
 * nibble is set eight u16 ids from +0x04 of which one must be 0xFFFF,
 * EVENT_FIELD_PROCLINK_IsReturnEntryValid). */
extern void *GAME_DATA_GetMoveModelSys(const void *gd) WEAK;
extern uint32_t FIELD_STATUS_GetMapMode(void *fs) WEAK;
extern void *GAME_DATA_GetSecondUnionInfo(void *gd) WEAK;
extern void *GAME_DATA_GetUnionInfo(void *gd, uint32_t index) WEAK;

static int read_wfbc(uintptr_t gd, unsigned zone, BmapWfbc *out)
{
    unsigned char ids[10];
    uintptr_t data;
    void *fs;
    int i, n = 0;

    if ((zone != 0u && zone != 424u) || !gptr_ok(gd) || GAME_DATA_GetMoveModelSys == NULL
        || FIELD_STATUS_GetMapMode == NULL || GAME_DATA_GetSecondUnionInfo == NULL
        || GAME_DATA_GetUnionInfo == NULL) {
        return -1;
    }
    fs = GAME_DATA_GetMoveModelSys((const void *)gd);
    data = fs != NULL && FIELD_STATUS_GetMapMode(fs) == 0u ? (uintptr_t)GAME_DATA_GetSecondUnionInfo((void *)gd)
                                                           : (uintptr_t)GAME_DATA_GetUnionInfo((void *)gd, 0);
    if (!gptr_ok(data) || rd16(data, 2) != (zone == 0u ? 0u : 1u)) {
        return -1;                      /* another city's save: the full place */
    }
    for (i = 0; i < 10; i++) {
        uintptr_t p = data + 8u + (uintptr_t)i * 0x18u;
        unsigned head = rd8(p, 0), id = rd8(p, 1), mood = rd8(p, 2);
        int ok = (head & 15u) != 0 && id < 30u && mood <= 100u;

        if (ok && (head >> 4) != 0u) {
            int k, any = 0;

            for (k = 0; k < 8; k++) {
                any |= rd16(p, 4u + 2u * (unsigned)k) == 0xFFFFu;
            }
            ok = any;
        }
        if (ok) {
            ids[n++] = (unsigned char)id;
        }
    }
    return bmap_wfbc_grid(zone, ids, n, rd32(data, 4), out);
}

/* The maps the field draws, noted each field frame (field_note) for the battle: its mapper's grid (FIELDMAP_GetG3dMapper:
 * aletheia ov021 FieldG3dMapper, gridWidth +0x14, gridDepth +0x16, blockCount +0x18, blockGrid +0x1C, a map index a
 * cell), the copy the field made of its matrix when it was set up.  The matrix (GAME_DATA_GetFieldWork: ov010
 * MapMatrix, zoneId +0x02, matrixId +0x04, width +0x08, height +0x0A) can be ahead of it: a story script swaps the
 * matrix in before a battle (the bedroom, MAP_REPLACE flag 9, before the first battle) and the field shows the new
 * maps only when it is set up again after it.  The grid is what the player saw; the matrix names the file the
 * build reads the rest from (its zone grid), when it is the grid's size. */
extern void *GAME_DATA_GetFieldWork(const void *gd) WEAK;
extern void *ov021_FIELDMAP_GetG3dMapper(void *fieldMap) WEAK;

static BmapFieldMatrix g_fmap;          /* the last field frame's maps */
static int g_fmap_ok;
static uint32_t g_fmap_frame = 0xFFFFFFFFu;

static void note_field_maps(uintptr_t gd, uintptr_t fm, unsigned zone)
{
    uintptr_t mp, grid, mx;
    unsigned i, w, h, n;

    if (g_fmap_frame == bw_input_frame_number()) {
        return;                         /* (once a frame) */
    }
    g_fmap_frame = bw_input_frame_number();
    g_fmap_ok = 0;
    if (ov021_FIELDMAP_GetG3dMapper == NULL || GAME_DATA_GetFieldWork == NULL || !gptr_ok(gd)) {
        return;
    }
    mp = (uintptr_t)ov021_FIELDMAP_GetG3dMapper((void *)fm);
    if (!gptr_ok(mp)) {
        return;
    }
    w = rd16(mp, 0x14);
    h = rd16(mp, 0x16);
    n = rd32(mp, 0x18);
    grid = rd32(mp, 0x1C);
    if (w == 0u || h == 0u || n != w * h || n > 900u || !gptr_ok(grid)) {
        return;                         /* (no grid, or a layered one: the files) */
    }
    mx = (uintptr_t)GAME_DATA_GetFieldWork((const void *)gd);
    g_fmap.zone = zone;
    g_fmap.matrix = 0xFFFFFFFFu;
    if (gptr_ok(mx) && rd16(mx, 0x08) == w && rd16(mx, 0x0A) == h && rd32(mx, 0x04) <= 0xFFFFu) {
        g_fmap.matrix = rd32(mx, 0x04);
    }
    g_fmap.w = w;
    g_fmap.h = h;
    for (i = 0; i < n; i++) {
        g_fmap.grid[i] = rd32(grid, 4u * i);
    }
    {
        /* the maps the slots have loaded, over the grid: the grid is the layout the field wants, a slot keeps the
         * map it shows until it loads another (FieldG3dMapperSlot: map +0x00, its cell's grid index +0x18;
         * FieldG3dMap: memberIndex +0x14, dataReady +0xD0).  The bedroom's grid is the wrecked room's from the
         * start of the first battle's flash, its slot the tidy room's until the field is set up again after it. */
        uintptr_t slots = rd32(mp, 0x24);
        unsigned k, ns = rd16(mp, 0x2E);

        for (k = 0; gptr_ok(slots) && k < ns && k < 64u; k++) {
            uintptr_t sl = slots + 0x28u * k, map = rd32(sl, 0x00);
            unsigned cell = rd32(sl, 0x18);

            if (cell < n && gptr_ok(map) && rd32(map, 0xD0) != 0u) {
                g_fmap.grid[cell] = rd32(map, 0x14);
            }
        }
    }
    g_fmap_ok = 1;
    {
        static unsigned last_zone = 0xFFFFFFFFu, last_first = 0xFFFFFFFFu;

        if (log_on() && (zone != last_zone || g_fmap.grid[0] != last_first)) {
            fprintf(stderr, "battle_view: field maps zone %u: %ux%u, first cell %u (the grid %u), matrix %u (frame %u)\n",
                    zone, w, h, g_fmap.grid[0], (unsigned)rd32(grid, 0), g_fmap.matrix, (unsigned)bw_input_frame_number());
        }
        last_zone = zone;
        last_first = g_fmap.grid[0];
    }
}

/* A frame of the field (not of a battle): its maps noted.  The 3D's every frame (snapshot_hook), so a place with
 * no object the field draws (field_note) is noted too. */
static void field_maps_frame(void)
{
    uintptr_t gs, fm, gd;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL || battle_on()) {
        return;
    }
    gs = (uintptr_t)*(void **)bw_dsref__GameSystemGlobal;
    fm = gs != 0 ? (uintptr_t)*(void **)(gs + 0x20) : 0;
    gd = gs != 0 ? (uintptr_t)*(void **)(gs + 0x1C) : 0;
    if (fm == 0 || !gptr_ok(gd)) {
        return;                         /* (not in the field) */
    }
    note_field_maps(gd, fm, rd16(gd, 4u + 4u * 0x44u));
}

/* Where the battle began, from GameData (the player's work) and the setup. */
static int read_place(uintptr_t eff, BvJob *j)
{
    uintptr_t gs = 0, gd = 0, pw = 0, bm, setup;

    /* the player's place: the game system's; a battle with none (the spot
     * editor's battle only, engine/ui/ui.c) has its place in
     * EIDOS_DEBUG_BMAP_AT alone */
    if (&bw_dsref__GameSystemGlobal != NULL && bw_dsref__GameSystemGlobal != NULL) {
        gs = (uintptr_t)*(void **)bw_dsref__GameSystemGlobal;
    }
    gd = gptr_ok(gs) ? rd32(gs, 0x1C) : 0;
    if (gptr_ok(gd)) {
        pw = gd + GD_PLAYER;
    } else if (getenv("EIDOS_DEBUG_BMAP_AT") == NULL) {
        return -1;
    }
    bm = rd32(eff, EFF_MAIN);
    setup = gptr_ok(bm) ? rd32(bm, 0) : 0;
    {
        /* the setup's competitor (the game's btl_setup.c): 0 wild, 1 a trainer, 2 the Battle Subway's, 3 a link
         * partner, 4 the catching lesson (BTL_SETUP_SetupDemo, started by EVENT_BATTLE_CreateCatchTutorial: the
         * professor's Minccino against a wild Patrat).  The lesson is no trainer battle: it was read as one, and
         * took the row of whatever trainer stood nearest. */
        const u32 comp = gptr_ok(setup) ? rd32(setup, 0) : 0u;

        j->trainer = comp >= 1u && comp <= 3u;
        j->lesson = comp == 4u;
    }
    if (j->trainer) {
        /* the opponent: trainerData[1]->trainerId (BattleSetupParam) */
        uintptr_t td = rd32(setup, SETUP_TRAINER1);

        j->trainer_id = gptr_ok(td) ? (int)rd32(td, 0) : 0;
    }
    if (pw != 0) {
        j->zone = rd16(pw, PW_ZONE);
        j->px = rdfx(pw, PW_X);
        j->py = rdfx(pw, PW_Y);
        j->has_py = 1;
        j->pz = rdfx(pw, PW_Z);
        j->facing = rd16(pw, PW_FACING);
    }
    j->season = 0;
    j->hour = 12;
    j->minute = 0;
    if (gptr_ok(setup)) {
        j->season = (int)(rd8(setup, SIT_SEASON) & 3u);
        if (j->zone == 0) {
            j->zone = rd16(setup, SIT_ZONE);
        }
        j->hour = (int)rd8(setup, SIT_HOUR);
        j->minute = (int)rd8(setup, SIT_MINUTE);
    }
    j->has_wfbc = read_wfbc(gd, j->zone, &j->wfbc) == 0;
    j->has_fmat = g_fmap_ok;           /* the field's last frame's maps (note_field_maps) */
    if (g_fmap_ok) {
        j->fmat = g_fmap;
    }
    {
        /* The spot editor's Preview in game (and tests): EIDOS_DEBUG_BMAP_AT="zone x z facing season hour
         * [reach [y]]" stages the backdrop at another place */
        const char *at = getenv("EIDOS_DEBUG_BMAP_AT");
        unsigned z, f, se, h, re = 0;
        float x, zz, y = 0.0f;
        int n;

        if (at != NULL && (n = sscanf(at, "%u %f %f %u %u %u %u %f", &z, &x, &zz, &f, &se, &h, &re, &y)) >= 6) {
            j->reach = (int)re;
            j->py = y;
            j->has_py = n >= 8;
            j->zone = (int)z;
            j->px = x;
            j->pz = zz;
            j->facing = (int)f;
            j->season = (int)se;
            j->hour = (int)h;
        }
    }
    return 0;
}

static void placed_free(BvPlaced *p)
{
    if (p != NULL) {
        BvBase *b = p->base;

        bmap_free(&p->s);
        free(p);
        base_release(b);
    }
}

/* The move of one slot from the game's (fx32 x, y, z): the foe's side
 * (odd positions) up by dy and back by dz, and a trainer's slot (positions 8
 * to 13, the table `trainers`) by its side's own offset, right, up and back. */
typedef struct SlotMove {
    float dy, dz, foe[3], own[3], ally[3];
    int has_ally;
} SlotMove;

/* A trainer slot's own offsets (positions 8 to 13): the foe's side (odd),
 * the ally's (12, in a battle beside one), else yours. */
static const float *slot_offsets(const float *foe, const float *own, const float *ally, int has_ally,
                                 int pos)
{
    return (pos & 1) ? foe : has_ally && pos == 12 ? ally : own;
}

static void slot_delta(const SlotMove *m, int trainers, uint32_t i, int32_t out[3])
{
    out[0] = out[1] = out[2] = 0;
    if ((i & 1u) == 1u) {
        out[1] += (int32_t)(m->dy * 4096.0f);
        out[2] -= (int32_t)(m->dz * 4096.0f);
    }
    if (trainers) {
        const float *t = slot_offsets(m->foe, m->own, m->ally, m->has_ally, 8 + (int)i);

        out[0] += (int32_t)(t[0] * 4096.0f);
        out[1] += (int32_t)(t[1] * 4096.0f);
        out[2] -= (int32_t)(t[2] * 4096.0f);
    }
}

/* sdk/battle_fixed.c: the move for an effect script's fixed points */
void eidos_bv_fixed_move_set(void (*move)(int pos, int *xyz));

/* The place's slot move: the foe's height and distance, the trainers' offsets. */
static void slot_move_of(const BmapScene *s, SlotMove *m)
{
    memset(m, 0, sizeof *m);
    m->dy = s->foe_dy;
    m->dz = s->foe_dz;
    if (s->view_set) {
        memcpy(m->foe, s->trainer_foe, sizeof m->foe);
        memcpy(m->own, s->trainer_own, sizeof m->own);
        memcpy(m->ally, s->trainer_ally, sizeof m->ally);
        m->has_ally = s->has_ally;
    }
}

/* Move the foe's slots by (dy, dz) battle units from the game's: up, and
 * back (away from the player), and the trainers' by the place's offsets.
 * The tables keep the game's values while the overlay stays loaded; a fresh
 * load brings them back, which shows as values other than the ones written
 * last.  Sprites already standing move by the change, so one in the middle
 * of a move keeps its motion. */
/* An effect script's fixed point (fx32, battle position `pos`) moved for
 * the place: the warp the draw takes back off (bmap_battle_warp; a flat
 * move of the slots' size would size the sprite as if it stood elsewhere),
 * and a trainer's offset for its side. */
static void fixed_point_move(int pos, int *xyz)
{
    float p[3];
    int k;

    if (g_cur == NULL) {
        return;
    }
    for (k = 0; k < 3; k++) {
        p[k] = (float)xyz[k] / 4096.0f;
    }
    bmap_battle_warp(p, g_cur->s.foe_dy, g_cur->s.foe_dz);
    if (pos >= 8 && g_cur->s.view_set) {
        const float *t = slot_offsets(g_cur->s.trainer_foe, g_cur->s.trainer_own, g_cur->s.trainer_ally,
                                      g_cur->s.has_ally, pos);

        p[0] += t[0];
        p[1] += t[1];
        p[2] -= t[2];
    }
    for (k = 0; k < 3; k++) {
        xyz[k] = (int)(p[k] * 4096.0f);
    }
}

/* the game's own slot tables, as slot_shift last found them fresh */
static int32_t g_slot_base[SLOT_ENTRIES][3];
static int g_slot_base_have;

static void slot_shift(const SlotMove *now)
{
    static int32_t written[SLOT_ENTRIES][3];
    int32_t (*base)[3] = g_slot_base;
    static SlotMove cur;
    static int have;
    uint32_t t, i, e = 0;
    int fresh = !have;
    SlotMove old = cur;

    for (t = 0; t < SLOT_TABLES && !fresh; t++) {
        int32_t *tab = (int32_t *)bw_ds_try_ptr(k_slot_tables[t].addr);

        for (i = 0; tab != NULL && i < k_slot_tables[t].count; i++, e++) {
            if (memcmp(&tab[i * 3], written[e], sizeof written[e]) != 0) {
                fresh = 1;
            }
        }
    }
    if (fresh) {
        memset(&old, 0, sizeof old);
    }
    e = 0;
    for (t = 0; t < SLOT_TABLES; t++) {
        int32_t *tab = (int32_t *)bw_ds_try_ptr(k_slot_tables[t].addr);

        for (i = 0; i < k_slot_tables[t].count; i++, e++) {
            int32_t d[3];

            if (tab == NULL) {
                continue;
            }
            if (fresh) {
                memcpy(base[e], &tab[i * 3], sizeof base[e]);
            }
            memcpy(&tab[i * 3], base[e], sizeof base[e]);
            slot_delta(now, t == SLOT_TRAINERS, i, d);
            tab[i * 3] += d[0];
            tab[i * 3 + 1] += d[1];
            tab[i * 3 + 2] += d[2];
            memcpy(written[e], &tab[i * 3], sizeof written[e]);
        }
    }
    have = 1;
    g_slot_base_have = 1;
    cur = *now;
    eidos_bv_fixed_move_set(fixed_point_move);     /* the points the effect scripts give */
    {
        /* the sprites standing, by the change */
        uintptr_t eff, mw;
        uint32_t *slot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);

        eff = slot != NULL ? *slot : 0;
        mw = gptr_ok(eff) ? rd32(eff, EFF_MCSS) : 0;
        for (i = 0; gptr_ok(mw) && i < MCSS_COUNT; i++) {
            uintptr_t sp = mw + MCSS_SPRITES + MCSS_STRIDE * i;
            uintptr_t mc = rd32(sp, 0);
            uint32_t pos = rd32(sp, MSPR_POS);
            int32_t dn[3], dold[3], v;
            int k;

            if (!gptr_ok(mc) || pos > 13u) {
                continue;
            }
            slot_delta(now, pos >= 8u, pos >= 8u ? pos - 8u : pos, dn);
            slot_delta(&old, pos >= 8u, pos >= 8u ? pos - 8u : pos, dold);
            for (k = 0; k < 3; k++) {
                memcpy(&v, (const void *)(mc + MCSSW_POS + 4u * (uint32_t)k), 4);
                v += dn[k] - dold[k];
                memcpy((void *)(mc + MCSSW_POS + 4u * (uint32_t)k), &v, 4);
            }
        }
    }
}

/* The player's own trainer (positions 8, 10, 12) in the room: at its slot
 * (the place's offsets on it), as the foe's.  The send-out puts it at a
 * point of its own before the camera (-15, -11, 0, framed bottom left on
 * the DS); a point away from its slot is taken as a move from that one, so
 * the trainer stays where the place puts it and moves as the script moves
 * it.  Returns 1 when `out` differs from `raw`. */
static int own_trainer_point(int pos, const float raw[3], float out[3])
{
    static const float k_sendout[3] = { -15.0f, -11.0f, 0.0f };
    const int32_t *b;
    float spot[3], d = 0.0f, e;
    int k;

    memcpy(out, raw, 3 * sizeof(float));
    if (pos < 8 || pos > 12 || (pos & 1) || !g_slot_base_have || g_cur == NULL) {
        return 0;
    }
    b = g_slot_base[SLOT_ENTRIES - 6u + (uint32_t)(pos - 8)];
    for (k = 0; k < 3; k++) {
        spot[k] = (float)b[k] / 4096.0f;
    }
    if (g_cur->s.view_set) {
        const float *t = slot_offsets(g_cur->s.trainer_foe, g_cur->s.trainer_own, g_cur->s.trainer_ally,
                                      g_cur->s.has_ally, pos);

        spot[0] += t[0];
        spot[1] += t[1];
        spot[2] -= t[2];
    }
    for (k = 0; k < 3; k++) {
        e = raw[k] - spot[k];
        d += e * e;
    }
    if (d <= 0.25f) {
        return 0;                       /* at its slot already */
    }
    {
        /* the point as the script gave it, or with the place's offsets on
         * it already (when it came through eidos_bv_fixed_point), whichever
         * the trainer is at */
        float plain = 0.0f, offset = 0.0f;

        for (k = 0; k < 3; k++) {
            float o = spot[k] - (float)b[k] / 4096.0f;

            e = raw[k] - k_sendout[k];
            plain += e * e;
            e = raw[k] - (k_sendout[k] + o);
            offset += e * e;
        }
        if (offset < plain) {
            return 0;
        }
        for (k = 0; k < 3; k++) {
            out[k] = raw[k] + spot[k] - (float)b[k] / 4096.0f;
        }
    }
    return 1;
}

/* The scene on show goes to "the one before"; the one before is freed. */
static void retire(void)
{
    placed_free(g_old);
    g_old = g_cur;
    g_cur = NULL;
    g_gen++;
}

void __wrap_G3D_CAMERA_Reset(void *camera);

/* The battle camera committed again (a new placement changes its warp and
 * zoom; a camera standing still would keep the old view until it moves). */
static void camera_recommit(void)
{
    uint32_t *slot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);
    uintptr_t eff = slot != NULL ? *slot : 0, cam, gc;

    if (!gptr_ok(eff)) {
        return;
    }
    cam = rd32(eff, EFF_CAMERA);
    gc = gptr_ok(cam) ? rd32(cam, CAM_G3D) : 0;
    if (gptr_ok(gc)) {
        __wrap_G3D_CAMERA_Reset((void *)gc);
    }
}

static void job_finish(void)
{
    BvJob *j = g_job;
    SlotMove mv;

    bw_thread_join(j->thread);
    g_job = NULL;
    if (j->built != NULL) {
        /* a new base: held as the newest and by its placed scene */
        j->built->refs = 1 + (j->placed != NULL);
        base_release(g_base);
        g_base = j->built;
    } else if (j->placed == NULL) {
        base_release(j->base);          /* the job's reference */
    }
    if (j->placed != NULL) {
        if (g_cur != NULL) {
            retire();
        }
        g_cur = j->placed;
        g_gen++;
        slot_move_of(&g_cur->s, &mv);
        slot_shift(&mv);
        camera_recommit();
    }
    free(j);
}

/*
 * The area's models made ahead.  When the player comes into a zone (the field's first frame there), its battle map
 * is built once on a thread of its own and thrown away: what stays is its remade models, made (about 0.3 s each)
 * and written to the store (bmap_set_remade_store), so the first battle there finds them ready.  The map builder
 * works on one map at a time: a battle that starts while this runs waits for it first (warm_wait).
 * EIDOS_BMAP_NO_WARM=1 turns it off.
 */
typedef struct BvWarm {
    unsigned zone;
    float px, pz;
    int season;
    volatile int done;
    BwThread *thread;
} BvWarm;
static BvWarm *g_warm;
static unsigned g_warm_zone = 0xFFFFFFFFu;

static void warm_main(void *arg)
{
    BvWarm *w = (BvWarm *)arg;
    BmapScene sc;
    clock_t t0 = clock();

    memset(&sc, 0, sizeof sc);
    bmap_set_wfbc(NULL);
    bmap_set_field_matrix(NULL);
    if (bmap_build(&sc, w->zone, w->px, w->pz, w->season, 12, 0) == 0) {
        bmap_free(&sc);
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        fprintf(stderr, "bmap: zone %u made ahead in %.0f ms\n", w->zone, 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC);
    }
    w->done = 1;
}

/* The build ahead, when one runs: waited for and put away. */
static void warm_wait(void)
{
    if (g_warm != NULL) {
        bw_thread_join(g_warm->thread);
        free(g_warm);
        g_warm = NULL;
    }
}

/* Each field frame: a finished build ahead is put away; a new zone starts one. */
static void warm_field(uintptr_t gd, unsigned zone)
{
    static int off = -1;

    if (off < 0) {
        off = getenv("EIDOS_BMAP_NO_WARM") != NULL;
    }
    if (g_warm != NULL && g_warm->done) {
        warm_wait();
    }
    if (off || zone == g_warm_zone || g_warm != NULL || g_job != NULL || !real_map() || !gptr_ok(gd)) {
        return;
    }
    g_warm_zone = zone;
    g_warm = (BvWarm *)calloc(1, sizeof *g_warm);
    if (g_warm == NULL) {
        return;
    }
    g_warm->zone = zone;
    g_warm->px = rdfx(gd + GD_PLAYER, PW_X);
    g_warm->pz = rdfx(gd + GD_PLAYER, PW_Z);
    g_warm->season = GAME_DATA_GetSeason != NULL ? (int)(GAME_DATA_GetSeason((const void *)gd) & 3u) : 0;
    g_warm->thread = bw_thread_start(warm_main, g_warm);
}

/* A job that is running: wait for it and take what it made. */
static void job_drop(void)
{
    if (g_job != NULL) {
        bw_thread_join(g_job->thread);
        g_job->done = 1;
        job_finish();
    }
}

/* The first battle frame: read the place and start the build. */
static void sprite_size_reset(void);

static void field_hand_over(void);

static void battle_begin(uintptr_t eff)
{
    BvJob *j;
    int row;

    job_drop();
    warm_wait();                        /* (the area's build ahead, if it still runs: one map at a time) */
    g_ntree_out = 0;                    /* (no tree is out yet) */
    g_tree_seeded = 0;
    g_show = 0;                         /* (the map waits for the intro's first view) */
    g_begin_frame = bw_input_frame_number();
    sprite_size_reset();
    field_hand_over();
    /* a scene from an earlier battle must not show under this one's start */
    if (g_cur != NULL) {
        retire();
    }
    j = (BvJob *)calloc(1, sizeof(*j));
    if (j == NULL || read_place(eff, j) != 0) {
        free(j);
        return;
    }
    /* the battle's row of the spot table (bmap_spots.c) */
    /* a trainer by its number first (a story battle starts wherever the
     * player stands), then by the sight line, then the zone's nearest
     * story place */
    row = -1;
    if (j->reach == 0 && j->trainer && j->trainer_id > 0) {
        row = bmap_spots_find_trainer(j->zone, j->trainer_id, j->px, j->pz);
    }
    if (j->reach == 0 && j->lesson) {
        /* the catching lesson: its own row; with none, where the player stands (never a wild area's row: the
         * lesson is a scene of the story, at its own place) */
        row = bmap_spots_find_trainer(j->zone, BMAP_SPOT_CATCH_LESSON, j->px, j->pz);
    }
    if (j->reach == 0 && row < 0 && !j->lesson) {
        row = bmap_spots_find(j->zone, j->trainer, j->px, j->pz);
    }
    if (j->reach == 0 && row < 0 && j->trainer) {
        row = bmap_spots_find_trainer(j->zone, 0, j->px, j->pz);
    }
    if (j->reach == 0 && row < 0 && getenv("EIDOS_DEBUG_BMAP_AT") != NULL) {
        row = bmap_spots_find(j->zone, !j->trainer, j->px, j->pz);  /* the previewed place */
    }
    if (row >= 0) {                     /* the row's view nearest the player */
        const char *forced = getenv("EIDOS_DEBUG_VIEW");    /* the spot editor's Preview: its view */
        int v = forced != NULL ? bmap_spots_view_at(row, atoi(forced)) : -1;

        row = v >= 0 ? v : bmap_spots_pick_view(row, j->px, j->pz, j->facing);
    }
    bmap_spots_spec(bmap_spots_get(row), j->px, j->pz, j->facing, &j->spec);
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        const BmapSpot *r = bmap_spots_get(row);

        fprintf(stderr, "bmap: row %s mode %d view %d: start %.0f %.0f facing %04x reach %d\n", r != NULL ? r->id : "none",
                r != NULL ? r->mode : -1, r != NULL ? r->view : -1, (double)j->spec.px, (double)j->spec.pz,
                j->spec.facing, j->spec.reach);
    }
    if (j->reach != 0) {                /* EIDOS_DEBUG_BMAP_AT's own reach (tests) */
        j->spec.reach = j->reach;
        j->spec.turn = j->reach < 0 ? 0 : 1;
    }
    if (log_on()) {
        BmapSpot *r = bmap_spots_get(row);

        fprintf(stderr, "battle_view: zone %u pos (%.1f, %.1f) facing %04x "
                "season %d time %02d:%02d %s %d row %s\n", j->zone, (double)j->px,
                (double)j->pz, j->facing, j->season, j->hour, j->minute,
                j->trainer ? "trainer" : "wild", j->trainer_id, r != NULL ? r->id : "none");
    }
    j->thread = bw_thread_start(job_main, j);
    g_job = j;
}

/* EIDOS_CAMERA_RECORD=file (for the spot editor's Play): every battle frame,
 * the game's own camera (before any intro zoom) and its Pokemon, one line:
 *   frame ex ey ez tx ty tz n slot x y z ...
 * The spot editor plays such a file over any place.  It is made from the
 * player's own game and stays on their computer. */
static void record_camera(uintptr_t eff, uintptr_t gc)
{
    static FILE *f;
    static int tried;
    uintptr_t mw;
    uint32_t i;

    if (!tried) {
        const char *path = getenv("EIDOS_CAMERA_RECORD");

        tried = 1;
        f = path != NULL ? fopen(path, "a") : NULL;
    }
    if (f == NULL) {
        return;
    }
    fprintf(f, "%u", (unsigned)bw_input_frame_number());
    for (i = 0; i < 3; i++) {
        fprintf(f, " %.3f", (double)rdfx(gc, G3C_EYE + 4u * i));
    }
    for (i = 0; i < 3; i++) {
        fprintf(f, " %.3f", (double)rdfx(gc, G3C_TARGET + 4u * i));
    }
    mw = rd32(eff, EFF_MCSS);
    for (i = 0; gptr_ok(mw) && i < MCSS_COUNT; i++) {
        uintptr_t sp = mw + MCSS_SPRITES + MCSS_STRIDE * i;
        uintptr_t mc = rd32(sp, 0);

        if (gptr_ok(mc) && rd32(sp, MSPR_POS) <= 13u) {
            fprintf(f, " p%u %.3f %.3f %.3f", (unsigned)rd32(sp, MSPR_POS), (double)rdfx(mc, MCSSW_POS),
                    (double)rdfx(mc, MCSSW_POS + 4u), (double)rdfx(mc, MCSSW_POS + 8u));
        }
    }
    fputc('\n', f);
    fflush(f);
}

static void capture(uintptr_t eff, int warp)
{
    uintptr_t cam = rd32(eff, EFF_CAMERA), gc;
    uint32_t i;

    if (!gptr_ok(cam)) {
        return;
    }
    gc = rd32(cam, CAM_G3D);
    if (!gptr_ok(gc) || rd32(gc, G3C_TYPE) != 0) {
        return;                         /* not a perspective projection     */
    }
    for (i = 0; i < 3; i++) {
        g_frame.eye[i] = rdfx(gc, G3C_EYE + 4u * i);
        g_frame.up[i] = rdfx(gc, G3C_UP + 4u * i);
        g_frame.target[i] = rdfx(gc, G3C_TARGET + 4u * i);
    }
    record_camera(eff, gc);
    if (warp && g_cur != NULL) {
        bmap_battle_camera(g_frame.eye, g_frame.target, &g_cur->s);     /* as the game drew */
    }
    g_frame.fsin = rdfx(gc, G3C_SIN);
    g_frame.fcos = rdfx(gc, G3C_COS);
    g_frame.aspect = rdfx(gc, G3C_ASPECT);
    g_frame.znear = rdfx(gc, G3C_NEAR);
    g_frame.zfar = rdfx(gc, G3C_FAR);
    g_frame.n_poke = 0;
    g_n_sprites = 0;
    {
        uintptr_t mw = rd32(eff, EFF_MCSS);

        for (i = 0; gptr_ok(mw) && i < MCSS_COUNT && g_frame.n_poke < G3D_BACKDROP_SPRITES; i++) {
            uintptr_t sp = mw + MCSS_SPRITES + MCSS_STRIDE * i;
            uintptr_t mc = rd32(sp, 0);
            uint32_t k, pos = gptr_ok(mc) ? rd32(sp, MSPR_POS) : 0xFFu;

            if (pos > 13u) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                g_frame.poke[g_frame.n_poke][k] = rdfx(mc, MCSSW_POS + 4u * k);
            }
            /* how far around a sprite the map in front of it is drawn again:
             * only over the game's own pixels (plat_gl3d.c's stencil), so the
             * box may be loose: twice a Pokemon's size, a trainer taller */
            g_frame.poke[g_frame.n_poke][3] = pos <= 7u ? 12.0f : 18.0f;
            g_frame.key[g_frame.n_poke][0] = rd32(mc, MCSSW_TEX);
            g_frame.key[g_frame.n_poke][1] = rd32(mc, MCSSW_PLTT);
            if (pos <= 7u && g_n_sprites < 8u) {
                g_sprites[g_n_sprites].pos = pos;
                g_sprites[g_n_sprites].tex = rd32(mc, MCSSW_TEX);
                g_sprites[g_n_sprites].pltt = rd32(mc, MCSSW_PLTT);
                g_n_sprites++;
            }
            {
                /* the player's own trainer away from its slot (the
                 * send-out's point, below the ground before the camera, as
                 * the DS frames it) is drawn over the map, as over the
                 * game's own backdrop: no map in front of it */
                float in[3], at[3];

                memcpy(in, g_frame.poke[g_frame.n_poke], sizeof in);
                if (own_trainer_point((int)pos, in, at)) {
                    g_frame.poke[g_frame.n_poke][3] = -18.0f;   /* (never covered: G3dBackdrop.poke) */
                }
            }
            g_frame.n_poke++;
        }
    }
    g_frame.valid = g_frame.fcos > 0.0f && g_frame.zfar > g_frame.znear;
    if (log_on()) {
        static uint32_t n;

        if ((n++ % 120u) == 0) {
            fprintf(stderr, "battle_view: eye (%.2f %.2f %.2f) target "
                    "(%.2f %.2f %.2f) up (%.2f %.2f %.2f) fov %.3f/%.3f "
                    "aspect %.3f near %.1f far %.1f\n",
                    (double)g_frame.eye[0], (double)g_frame.eye[1],
                    (double)g_frame.eye[2], (double)g_frame.target[0],
                    (double)g_frame.target[1], (double)g_frame.target[2],
                    (double)g_frame.up[0], (double)g_frame.up[1],
                    (double)g_frame.up[2], (double)g_frame.fsin,
                    (double)g_frame.fcos, (double)g_frame.aspect,
                    (double)g_frame.znear, (double)g_frame.zfar);
            for (i = 0; i < g_frame.n_poke; i++) {
                fprintf(stderr, "battle_view: pokemon %u at (%.2f %.2f %.2f)\n", (unsigned)i,
                        (double)g_frame.poke[i][0], (double)g_frame.poke[i][1], (double)g_frame.poke[i][2]);
            }
        }
    }
}

/* Which of the battle's scenery objects `obj` is; the battle's work too. */
static int classify(const void *obj, uintptr_t *eff_out)
{
    uint32_t *slot;
    uintptr_t eff, stage, field, o = (uintptr_t)obj;

    if (!bw_overlay_is_loaded(93) || !bw_overlay_is_loaded(94)) {
        return OBJ_OTHER;
    }
    slot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);
    if (slot == NULL) {
        return OBJ_OTHER;
    }
    eff = *slot;
    if (!gptr_ok(eff)) {
        return OBJ_OTHER;
    }
    stage = rd32(eff, EFF_STAGE);
    field = rd32(eff, EFF_FIELD);
    *eff_out = eff;
    if (gptr_ok(stage) && (o == rd32(stage, STAGE_OBJ0) || o == rd32(stage, STAGE_OBJ1))) {
        return OBJ_STAGE;
    }
    if (gptr_ok(field) && o == rd32(field, FIELD_OBJ)) {
        return OBJ_FIELD;
    }
    return OBJ_OTHER;
}

/* The spot table reads the user's rows over its built-in ones: the file the
 * spot editor (tools/spot-editor) writes, beside the settings file. */
static void spots_user_file(void)
{
    static int done;
    const char *set, *slash, *bs;
    char path[600];

    if (done) {
        return;
    }
    done = 1;
    set = eidos_settings_path();
    slash = set != NULL ? strrchr(set, '/') : NULL;
    bs = set != NULL ? strrchr(set, '\\') : NULL;
    if (bs != NULL && (slash == NULL || bs > slash)) {
        slash = bs;
    }
    if (slash != NULL) {
        snprintf(path, sizeof path, "%.*s/battle_spots.tsv", (int)(slash - set), set);
        bmap_spots_set_user_path(path);
        /* the remade models' store, beside the settings too */
        snprintf(path, sizeof path, "%.*s/remade", (int)(slash - set), set);
        bmap_set_remade_store(path, "eidos");
    }
}

void __real_G3D_CAMERA_Reset(void *camera);

/* The battle's own camera (BtlvCamera's G3D camera), 0 outside a battle.
 * Other cameras commit through G3D_CAMERA_Reset too (one at the origin
 * after the send-out) and are left as they are. */
static uintptr_t battle_camera(void)
{
    uint32_t *slot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);
    uintptr_t eff = slot != NULL ? *slot : 0, cam;

    if (!gptr_ok(eff)) {
        return 0;
    }
    cam = rd32(eff, EFF_CAMERA);
    return gptr_ok(cam) ? rd32(cam, CAM_G3D) : 0;
}

/* The last battle camera committed: the game's own and the one drawn with
 * (see MCSS_Draw below); g_cam_on 0 before the first.  g_rest: the size at
 * rest of each sprite (McssSys's works, by index), which the scripts chose
 * for the camera (bmap_rest_sprite). */
#define SPRITES_MAX BMAP_REST_MAX
static float g_cam_game[2][3], g_cam_drawn[2][3];
static int g_cam_on;
static BmapRest g_rest;

static void sprite_size_reset(void)
{
    g_cam_on = 0;
    bmap_rest_reset(&g_rest);
}

/* The battle camera commits its eye and target here (btlv_camera.c, every
 * move step): with this place's intro zoom, a far camera is drawn closer.
 * The camera object keeps the game's own eye; only the view the game draws
 * with (G3D_SYSTEM_SetLookAt, computed in the call) and the backdrop's
 * (capture) use the zoomed one. */
void __wrap_G3D_CAMERA_Reset(void *camera)
{
    uintptr_t gc = (uintptr_t)camera;

    if (g_cur != NULL && gptr_ok(gc) && bw_battle_view_active() && gc == battle_camera()
        && (!bmap_intro_none(&g_cur->s.intro) || g_cur->s.foe_dy != 0.0f || g_cur->s.foe_dz != 0.0f
            || bmap_place_view_set(&g_cur->s))) {
        int32_t keep[6], v;
        float eye[3], target[3];
        uint32_t i;

        memcpy(keep, (const void *)(gc + G3C_EYE), 3 * sizeof keep[0]);
        memcpy(keep + 3, (const void *)(gc + G3C_TARGET), 3 * sizeof keep[0]);
        for (i = 0; i < 3; i++) {
            eye[i] = rdfx(gc, G3C_EYE + 4u * i);
            target[i] = rdfx(gc, G3C_TARGET + 4u * i);
        }
        if (log_on()) {
            fprintf(stderr, "battle_view: frame %u camera eye (%.2f %.2f %.2f) ", (unsigned)bw_input_frame_number(),
                    (double)eye[0], (double)eye[1], (double)eye[2]);
        }
        memcpy(g_cam_game[0], eye, sizeof eye);
        memcpy(g_cam_game[1], target, sizeof target);
        bmap_battle_camera(eye, target, &g_cur->s);
        memcpy(g_cam_drawn[0], eye, sizeof eye);
        memcpy(g_cam_drawn[1], target, sizeof target);
        g_cam_on = 1;
        if (log_on()) {
            fprintf(stderr, "zoomed (%.2f %.2f %.2f)\n", (double)eye[0], (double)eye[1], (double)eye[2]);
        }
        for (i = 0; i < 3; i++) {
            v = (int32_t)(eye[i] * 4096.0f);
            memcpy((void *)(gc + G3C_EYE + 4u * i), &v, 4);
            v = (int32_t)(target[i] * 4096.0f);
            memcpy((void *)(gc + G3C_TARGET + 4u * i), &v, 4);
        }
        __real_G3D_CAMERA_Reset(camera);
        memcpy((void *)(gc + G3C_EYE), keep, 3 * sizeof keep[0]);
        memcpy((void *)(gc + G3C_TARGET), keep + 3, 3 * sizeof keep[0]);
        return;
    }
    if (g_cur != NULL && gptr_ok(gc) && gc == battle_camera()) {
        uint32_t i;

        for (i = 0; i < 3; i++) {
            g_cam_game[0][i] = g_cam_drawn[0][i] = rdfx(gc, G3C_EYE + 4u * i);
            g_cam_game[1][i] = g_cam_drawn[1][i] = rdfx(gc, G3C_TARGET + 4u * i);
        }
        g_cam_on = 1;
    }
    __real_G3D_CAMERA_Reset(camera);
}

/* McssSys and McssWork (arm9 mcss.c) */
#define MSYS_MAX     0x10u          /* s32 maxCount                          */
#define MSYS_WORKS   0x14u          /* McssWork **works                      */
#define MSYS_FLAGS   0x2Cu          /* bit 0: orthoMode                      */
#define MCSSW_SCALE  0xECu          /* VecFx32 scale                         */
#define MCSSW_OFSPOS 0x11Cu         /* VecFx32 ofsPosition                   */
#define MCSSW_FLAGS  0x140u         /* bit 29: orthoBillboardFlag            */
#define MCSSW_SHADOWSCALE 0x128u    /* VecFx32 shadowScale (the script's size) */

void __real_MCSS_Draw(void *sys);

/* The battle position of the McssWork `w` (BtlvMcssWork's sprites), or -1. */
static int sprite_pos(uintptr_t w)
{
    uint32_t *slot = (uint32_t *)bw_ds_try_ptr(DS_GBTLVEFF);
    uintptr_t eff = slot != NULL ? *slot : 0, bm = gptr_ok(eff) ? rd32(eff, EFF_MCSS) : 0;
    uint32_t i;

    for (i = 0; gptr_ok(bm) && i < MCSS_COUNT; i++) {
        uintptr_t sp = bm + MCSS_SPRITES + MCSS_STRIDE * i;

        if (rd32(sp, 0) == w) {
            return (int)rd32(sp, MSPR_POS);
        }
    }
    return -1;
}

/* The battle draws its Pokemon and trainers flat (MCSS ortho billboards):
 * a sprite's size is its scale, never its distance, and the camera does
 * not change it.  The scripts fake a camera that goes back or comes in
 * instead: they tween the sprites' size (McssWork shadowScale) along with
 * the camera (the wild intro takes the foe to 0.5; the views 14 and 19 take
 * the sides to 0.65 and 0.725, ov094's camera command).
 *
 * A trainer is drawn the size the game gives it, as with the game's own
 * backdrop: only what the place changes changes it (its offsets, its
 * height, a camera the place moves), by its depth before the camera drawn
 * with over the game's own depth before the game's camera.
 *
 * A Pokemon has one size in the room, as a thing in it does
 * (bmap_mon_size: the game's own at the command camera), and is drawn at
 * that size over its depth before the camera drawn with, every frame: the
 * intro, the send-out, the command camera and its idle moves all show it
 * at the size its distance gives, so it never grows or shrinks against the
 * map and the two sides change together.  The scripts' fake (the size at
 * rest, bmap_rest_sprite) is taken out; a change with the camera still is
 * the script's own (a withdraw, a faint, a pop) and shows.  The scales are
 * put back after. */
void __wrap_MCSS_Draw(void *sys)
{
    uintptr_t ms = (uintptr_t)sys, works, w;
    int32_t keep[SPRITES_MAX][2], keep_pos[SPRITES_MAX][3], v;
    int32_t n, i;
    int pinned[SPRITES_MAX];

    if (!g_cam_on || g_cur == NULL || !gptr_ok(ms) || !bw_battle_view_active() || !(rd32(ms, MSYS_FLAGS) & 1u)) {
        __real_MCSS_Draw(sys);
        return;
    }
    works = rd32(ms, MSYS_WORKS);
    n = (int32_t)rd32(ms, MSYS_MAX);
    if (!gptr_ok(works) || n <= 0) {
        __real_MCSS_Draw(sys);
        return;
    }
    if (n > SPRITES_MAX) {
        n = SPRITES_MAX;
    }
    bmap_rest_frame(&g_rest, g_cam_game[0]);
    for (i = 0; i < n; i++) {
        float p[3], q[3], dg, dd, f, rest;
        uint32_t k;
        int pos;

        keep[i][0] = keep[i][1] = 0;
        pinned[i] = 0;
        w = rd32(works, 4u * (uint32_t)i);
        if (!gptr_ok(w)) {
            (void)bmap_rest_sprite(&g_rest, i, 0.0f);
            continue;
        }
        pos = sprite_pos(w);
        {
            /* the player's own trainer (positions 8, 10, 12) away from its
             * slot (the send-out's point, framed bottom left as on the DS)
             * takes the place's offsets for it, as at its slot */
            float raw[3], at[3];
            int kk;

            for (kk = 0; kk < 3; kk++) {
                raw[kk] = rdfx(w, MCSSW_POS + 4u * (uint32_t)kk);
            }
            if (own_trainer_point(pos, raw, at)) {
                for (kk = 0; kk < 3; kk++) {
                    keep_pos[i][kk] = (int32_t)rd32(w, MCSSW_POS + 4u * (uint32_t)kk);
                    v = (int32_t)(at[kk] * 4096.0f);
                    memcpy((void *)(w + MCSSW_POS + 4u * (uint32_t)kk), &v, 4);
                }
                pinned[i] = 1;          /* put back after the draw */
            }
        }
        rest = bmap_rest_sprite(&g_rest, i, rdfx(w, MCSSW_SHADOWSCALE));
        if (!(rd32(w, MCSSW_FLAGS) & (1u << 29))) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            p[k] = rdfx(w, MCSSW_POS + 4u * k) + rdfx(w, MCSSW_OFSPOS + 4u * k);
        }
        memcpy(q, p, sizeof q);
        if (pos < 8) {
            bmap_battle_unwarp(q, g_cur->s.foe_dy, g_cur->s.foe_dz);   /* where the game's battle has it */
        } else {
            /* a trainer: without the place's move of its slot */
            SlotMove m;
            int32_t d[3];

            slot_move_of(&g_cur->s, &m);
            slot_delta(&m, 1, (uint32_t)(pos - 8), d);
            for (k = 0; k < 3; k++) {
                q[k] -= (float)d[k] / 4096.0f;
            }
        }
        if (pos < 8) {
            dg = bmap_mon_size(q) / (rest > 0.05f ? rest : 1.0f);
        } else {
            dg = bmap_view_depth(g_cam_game[0], g_cam_game[1], q);
        }
        dd = bmap_view_depth(g_cam_drawn[0], g_cam_drawn[1], p);
        if (dg < 0.5f || dd < 0.5f) {
            continue;
        }
        f = dg / dd;
        if (pos >= 8 && pos <= 13) {
            if (pos & 1) {
                f *= g_cur->s.trainer_size > 0.0f ? g_cur->s.trainer_size : 1.0f;
            } else if (g_cur->s.has_ally && pos == 12) {
                f *= g_cur->s.trainer_ally_size > 0.0f ? g_cur->s.trainer_ally_size : 1.0f;
            } else {
                f *= g_cur->s.trainer_own_size > 0.0f ? g_cur->s.trainer_own_size : 1.0f;
            }
        }
        f = fminf(fmaxf(f, 0.25f), 6.0f);
        if (log_on()) {
            fprintf(stderr, "battle_view: frame %u sprite %d pos %d at (%.2f %.2f %.2f) game depth %.2f drawn %.2f scale x%.2f size %.3f game scale %.3f\n",
                    (unsigned)bw_input_frame_number(), (int)i, pos, (double)p[0], (double)p[1], (double)p[2],
                    (double)dg, (double)dd, (double)f, (double)rdfx(w, MCSSW_SHADOWSCALE),
                    (double)rdfx(w, MCSSW_SCALE));
        }
        keep[i][0] = (int32_t)rd32(w, MCSSW_SCALE);
        keep[i][1] = (int32_t)rd32(w, MCSSW_SCALE + 4u);
        v = (int32_t)((float)keep[i][0] * f);
        memcpy((void *)(w + MCSSW_SCALE), &v, 4);
        v = (int32_t)((float)keep[i][1] * f);
        memcpy((void *)(w + MCSSW_SCALE + 4u), &v, 4);
    }
    __real_MCSS_Draw(sys);
    for (i = 0; i < n; i++) {
        w = rd32(works, 4u * (uint32_t)i);
        if (gptr_ok(w) && pinned[i]) {
            memcpy((void *)(w + MCSSW_POS), keep_pos[i], sizeof keep_pos[i]);
        }
        if (gptr_ok(w) && (keep[i][0] != 0 || keep[i][1] != 0)) {
            memcpy((void *)(w + MCSSW_SCALE), &keep[i][0], 4);
            memcpy((void *)(w + MCSSW_SCALE + 4u), &keep[i][1], 4);
        }
    }
}

/*
 * What the field draws (research: the state carry).  Every 3D object of the field (a gym's curtain, a door, a
 * stone's effect) is drawn by G3D_SYSTEM_DrawObject or G3D_SYSTEM_DrawObjectCulling with the object (its
 * model resource, its texture resource, its animations with their present frames: aletheia arm9
 * g3d_system_types.h) and a status (place, scale, turn).  Noted here while the field runs.
 */
typedef struct { uint16_t magic; uint16_t type; void *file; } GResource;
typedef struct { uint16_t magic; void *rnd; GResource *mdl, *tex; } GRender;
typedef struct { uint16_t magic; void *anm; const GResource *res; } GAnime;
typedef struct { uint16_t magic; GRender *render; GAnime **anm; uint16_t nanm; } GObject;
extern int G3D_SYSTEM_GetObjectAnimeFrame(void *obj, uint32_t idx, int32_t *frame) WEAK;
int __real_G3D_SYSTEM_DrawObjectCulling(void *obj, void *status);

extern void *ov021_FIELDMAP_GetCamera(void *fieldMap) WEAK;
extern uint16_t ov021_FIELD_CAMERA_GetPitch(void *camera) WEAK;

/* The game's files copied out of its memory (the field frees them before the battle's first frame). */
typedef struct { const void *src; unsigned n; uint32_t sum; unsigned char *copy; } FileCopy;
static FileCopy g_fc[96];
static unsigned g_nfc;

static const unsigned char *file_copy(const void *src, unsigned *n_out)
{
    const uint8_t *f = (const uint8_t *)src;
    unsigned n, k;
    uint32_t sum = 0;

    *n_out = 0;
    if (f == NULL || f[0] != 'B' || f[3] != '0') {
        return NULL;
    }
    n = (unsigned)(f[8] | (f[9] << 8) | (f[10] << 16) | ((unsigned)f[11] << 24));
    if (n < 16u || n > 0x400000u) {
        return NULL;
    }
    for (k = 0; k < 64u && k < n; k++) {
        sum = sum * 31u + f[k];
    }
    for (k = 0; k < g_nfc; k++) {
        if (g_fc[k].src == src && g_fc[k].n == n && g_fc[k].sum == sum) {
            *n_out = n;
            return g_fc[k].copy;
        }
    }
    if (g_nfc >= 96u) {
        return NULL;
    }
    g_fc[g_nfc].copy = (unsigned char *)malloc(n);
    if (g_fc[g_nfc].copy == NULL) {
        return NULL;
    }
    memcpy(g_fc[g_nfc].copy, f, n);
    g_fc[g_nfc].src = src;
    g_fc[g_nfc].n = n;
    g_fc[g_nfc].sum = sum;
    *n_out = n;
    return g_fc[g_nfc++].copy;
}

typedef struct { const void *obj; uint32_t frame; BmapLive live; } FieldObj;
static FieldObj g_fo[160];
static unsigned g_nfo, g_fo_zone = 0xFFFFFFFFu;
static uint32_t g_fo_last;

static void field_note(void *obj_, const void *status_)
{
    const GObject *o = (const GObject *)obj_;
    const int32_t *st = (const int32_t *)status_;
    FieldObj *e = NULL;
    BmapLive *l;
    uintptr_t gs, fm;
    unsigned zone, k, a, b;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL || o == NULL || st == NULL) {
        return;
    }
    gs = (uintptr_t)*(void **)bw_dsref__GameSystemGlobal;
    fm = gs != 0 ? (uintptr_t)*(void **)(gs + 0x20) : 0;
    if (fm == 0 || battle_on() || o->render == NULL || o->render->mdl == NULL) {
        return;                         /* (not in the field) */
    }
    {
        const uint8_t *pw = *(uint8_t **)(gs + 0x1C) + 4 + 4 * 0x44;

        zone = (unsigned)(pw[0] | (pw[1] << 8));
    }
    warm_field(gs != 0 ? (uintptr_t)*(void **)(gs + 0x1C) : 0, zone);
    if (zone != g_fo_zone) {            /* another place: its objects are other objects */
        g_fo_zone = zone;
        g_nfo = 0;
        if (g_job == NULL) {            /* (no map is being built from the copies) */
            for (k = 0; k < g_nfc; k++) {
                free(g_fc[k].copy);
            }
            g_nfc = 0;
        }
    }
    for (k = 0; k < g_nfo; k++) {
        if (g_fo[k].obj == obj_) {
            e = &g_fo[k];
            break;
        }
    }
    if (e == NULL) {
        if (g_nfo >= 160u) {
            return;
        }
        e = &g_fo[g_nfo++];
        memset(e, 0, sizeof *e);
        e->obj = obj_;
        if (getenv("EIDOS_LIVE_LOG") != NULL) {
            fprintf(stderr, "eidos: field draws: zone %u object %p at (%.1f, %.1f, %.1f), %u animations\n", zone, obj_,
                    st[0] / 4096.0, st[1] / 4096.0, st[2] / 4096.0, (unsigned)o->nanm);
        }
    }
    l = &e->live;
    e->frame = bw_input_frame_number();
    g_fo_last = e->frame;
    l->mdl = file_copy(o->render->mdl->file, &l->mdl_n);
    l->tex = NULL;
    l->tex_n = 0;
    if (o->render->tex != NULL && o->render->tex != o->render->mdl) {
        l->tex = file_copy(o->render->tex->file, &l->tex_n);
    }
    l->nanim = 0;
    for (k = 0; k < o->nanm && l->nanim < BMAP_LIVE_ANIMS; k++) {
        int32_t fr = 0;

        if (o->anm == NULL || o->anm[k] == NULL || o->anm[k]->res == NULL) {
            continue;
        }
        l->anim[l->nanim].p = file_copy(o->anm[k]->res->file, &l->anim[l->nanim].n);
        if (l->anim[l->nanim].p == NULL) {
            continue;
        }
        if (G3D_SYSTEM_GetObjectAnimeFrame != NULL) {
            G3D_SYSTEM_GetObjectAnimeFrame(obj_, k, &fr);
        }
        l->anim[l->nanim].frame = fr / 4096;
        l->nanim++;
    }
    /* the status: place, size, turn (a row of the turn is where an axis of the model goes) */
    for (a = 0; a < 3u; a++) {
        l->t[a] = (float)st[a] / 4096.0f;
        for (b = 0; b < 3u; b++) {
            l->r[a][b] = (float)st[6 + a * 3 + b] / 4096.0f * ((float)st[3 + a] / 4096.0f);
        }
    }
    l->field_tan = 0.0f;
    if (ov021_FIELDMAP_GetCamera != NULL && ov021_FIELD_CAMERA_GetPitch != NULL && ov021_FIELDMAP_GetCamera((void *)fm) != NULL) {
        float ang = (float)ov021_FIELD_CAMERA_GetPitch(ov021_FIELDMAP_GetCamera((void *)fm)) * 6.2831853f / 65536.0f;

        if (ang > 0.1f && ang < 1.5f) {
            l->field_tan = tanf(ang);
        }
    }
}

/* The surf mount (bmap.h, bmap_set_mount_hidden): the Pokemon the player surfs on is the one the field's check
 * picked, the first that knows Surf (FIELD_EVENT_CHECK_FindPartyMemberWithMove, the port's copy, which asks the
 * mods: move.known).  Its personality is kept when a battle begins on the water; while the player's own Pokemon
 * in battle is that one, the mount is left out, as it stands there itself. */
extern void *GAME_DATA_GetMyParty(void *gameData) WEAK;
extern uint32_t POKEPARTY_GetCount(void *party) WEAK;
extern void *POKEPARTY_GetMember(void *party, uint32_t pos) WEAK;
extern uint32_t POKE_TOOL_GetPartyValue(void *pp, uint32_t paramId, uint32_t arg) WEAK;
extern int ov021_FIELD_EVENT_CHECK_FindPartyMemberWithMove(void *gameData, uint32_t move) WEAK;
extern const BwModState *bw_mod_state_table(void) WEAK;

#define MOVE_SURF 57u

static int g_surf_known;            /* a battle on the water: g_surf_pid is the Pokemon surfed on */
static uint32_t g_surf_pid;

static void mount_note(const BmapLive *list, unsigned n)
{
    unsigned k;
    int surfing = 0;
    void *gd, *party;
    int slot;

    g_surf_known = 0;
    bmap_set_mount_hidden(0);
    for (k = 0; k < n && !surfing; k++) {
        const unsigned char *p = list[k].mdl;
        unsigned i;

        for (i = 0; p != NULL && i + 8u <= list[k].mdl_n && i < 0x80u; i++) {
            if (memcmp(p + i, "sea_ride", 8) == 0) {
                surfing = 1;            /* (the model's name sits in its MDL0 dictionary, near the start) */
                break;
            }
        }
    }
    if (!surfing || &bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL ||
        GAME_DATA_GetMyParty == NULL || POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL ||
        ov021_FIELD_EVENT_CHECK_FindPartyMemberWithMove == NULL) {
        return;
    }
    gd = *(void **)((uintptr_t)*(void **)bw_dsref__GameSystemGlobal + 0x1C);
    if (gd == NULL || (party = GAME_DATA_GetMyParty(gd)) == NULL) {
        return;
    }
    slot = ov021_FIELD_EVENT_CHECK_FindPartyMemberWithMove(gd, MOVE_SURF);
    if (slot >= 0 && slot < 6 && (POKEPARTY_GetCount == NULL || (uint32_t)slot < POKEPARTY_GetCount(party))) {
        g_surf_pid = POKE_TOOL_GetPartyValue(POKEPARTY_GetMember(party, (uint32_t)slot), BW_MON_FIELD_PERSONALITY, 0);
        g_surf_known = 1;
    }
}

/* Each battle frame: is the player's own Pokemon (the first of its side's order: the battle keeps the Pokemon
 * on the field at the front) the one surfed on? */
static void mount_frame(void)
{
    const BwModState *st = bw_mod_state_table != NULL ? bw_mod_state_table() : NULL;
    BwMonRef ref;
    uint32_t pid = 0;

    if (!g_surf_known || st == NULL || st->battle_ref == NULL || st->battle_get == NULL) {
        bmap_set_mount_hidden(0);
        return;
    }
    bmap_set_mount_hidden(st->battle_ref(0u, 0u, &ref) == 0 && st->battle_get(ref, BW_MON_FIELD_PERSONALITY, &pid) == 0 &&
                          pid == g_surf_pid);
}

/* A battle begins: the objects the field drew in its last frames go to the map's builder. */
static void field_hand_over(void)
{
    static BmapLive list[160];
    unsigned k, n = 0;

    for (k = 0; k < g_nfo; k++) {
        if (g_fo_last - g_fo[k].frame <= 2u && g_fo[k].live.mdl != NULL) {
            list[n++] = g_fo[k].live;
        }
    }
    bmap_live_set(g_fo_zone, list, n);
    mount_note(list, n);
}

int __wrap_G3D_SYSTEM_DrawObjectCulling(void *obj, void *status)
{
    field_maps_frame();             /* (the field's maps, once a frame) */
    field_note(obj, status);
    return __real_G3D_SYSTEM_DrawObjectCulling(obj, status);
}

void __wrap_G3D_SYSTEM_DrawObject(void *obj, void *status)
{
    uintptr_t eff = 0;
    int kind;

    field_maps_frame();             /* (the field's maps, once a frame) */
    field_note(obj, status);

    spots_user_file();
    kind = classify(obj, &eff);

    if (kind != OBJ_OTHER && real_map()) {
        uint32_t now = bw_input_frame_number();

        if (!g_hooked) {
            g3d_hw_set_backdrop_hook(backdrop_hook);
            g3d_hw_set_snapshot_hook(snapshot_hook);
            g_hooked = 1;
        }
        if (!g_in_battle || now - g_last_frame > 8u) {
            eidos_bv_fixed_move_set(NULL);  /* the last battle's move is not this one's */
            battle_begin(eff);
            /* The battle's first frame waits for its map (job_finish joins the worker).  A big zone takes over a
             * second to build, and the battle ran on meanwhile: the foes faded in on black, the camera jumped when
             * the map came.  The screen is dark at this moment (the encounter's wipe has closed), so the wait
             * shows as a slightly longer dark.  EIDOS_BMAP_NO_WAIT=1: as before. */
            if (g_job != NULL && getenv("EIDOS_BMAP_NO_WAIT") == NULL) {
                job_finish();
            }
        }
        g_in_battle = 1;
        g_last_frame = now;
        mount_frame();
        if (g_job != NULL && g_job->done) {
            job_finish();
        }
        if (kind == OBJ_FIELD) {
            g_sprites_only = 0;
            capture(eff, 1);
            return;
        }
        if (eidos_settings_get(EIDOS_OPT_BATTLE_PLATFORMS) == 0) {
            return;
        }
    } else if (kind == OBJ_FIELD && eidos_ui_active()) {
        /* the one-screen battle on the game's own backdrop: the backdrop
         * is the game's, but its 2D is not shown either, so the frame's
         * camera is kept for the effects' sprites */
        if (!g_hooked) {
            g3d_hw_set_backdrop_hook(backdrop_hook);
            g3d_hw_set_snapshot_hook(snapshot_hook);
            g_hooked = 1;
        }
        g_sprites_only = 1;
        capture(eff, 0);
    }
    __real_G3D_SYSTEM_DrawObject(obj, status);
}
