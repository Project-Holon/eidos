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

#include "battle_view.h"
#include "bmap.h"
#include "bmap_place.h"
#include "bmap_spots.h"
#include "dsmem.h"
#include "hw3d.h"
#include "input.h"
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
    int trainer_id;                 /* the opponent's trainer number, 0 unknown */
    BmapPlaceSpec spec;
    int rc;
    volatile int done;
    BwThread *thread;
    int reach;                      /* EIDOS_DEBUG_BMAP_AT: 0 the table, < 0 the
                                       spot as given, > 0 a search that far */
} BvJob;

typedef struct BvFrame {
    int valid;                      /* the field was drawn this frame      */
    float eye[3], up[3], target[3];
    float fsin, fcos, aspect, znear, zfar;
    float poke[G3D_BACKDROP_SPRITES][4]; /* the Pokemon's and trainers' sprites:
                                       where, and their box's height       */
    uint32_t n_poke;
} BvFrame;

static BvFrame g_frame;

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
static void pin_point(const float in[3], float out[3]);
static BvJob *g_job;                /* running or just finished            */
static BvPlaced *g_cur, *g_old;     /* placed scenes: current, the one before */
static BvBase *g_base;              /* the newest base                     */
static uint32_t g_gen;
static uint32_t g_last_frame;       /* the last frame a battle draw was seen */
static int g_in_battle;
static int g_hooked;
static int g_log = -1;

static int gptr_ok(uintptr_t a)
{
    return a >= 0x02000000u && a < 0x02400000u && (a & 3u) == 0;
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

static int real_map(void)
{
    return eidos_ui_active()
           && eidos_settings_get(EIDOS_OPT_BATTLE_BACKDROP) == 0;
}

int bw_battle_view_active(void)
{
    return g_in_battle && bw_input_frame_number() - g_last_frame <= 8u
           && real_map();
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

static void snapshot_hook(const G3dSnapshot *s)
{
    uint32_t k, i, now = bw_input_frame_number();

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

/* The renderer's side: the frame's camera and the scene. */
static void backdrop_hook(G3dBackdrop *out)
{
    if (!g_frame.valid) {
        return;
    }
    g_frame.valid = 0;
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
    memcpy(out->poke, g_frame.poke, sizeof(out->poke));
    out->n_poke = g_frame.n_poke;
    if (g_cur != NULL) {
        memcpy(out->sky, g_cur->s.clear, sizeof(out->sky));
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

    j->rc = 0;
    if (b == NULL) {
        b = (BvBase *)calloc(1, sizeof *b);
        if (b == NULL) {
            j->rc = -1;
            j->done = 1;
            return;
        }
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
    j->done = 1;
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
    j->trainer = gptr_ok(setup) && rd32(setup, 0) != 0;     /* competitor: 0 wild */
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
    float dy, dz, foe[3], own[3];
} SlotMove;

static void slot_delta(const SlotMove *m, int trainers, uint32_t i, int32_t out[3])
{
    out[0] = out[1] = out[2] = 0;
    if ((i & 1u) == 1u) {
        out[1] += (int32_t)(m->dy * 4096.0f);
        out[2] -= (int32_t)(m->dz * 4096.0f);
    }
    if (trainers) {
        const float *t = (i & 1u) == 1u ? m->foe : m->own;

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
        const float *t = (pos & 1) ? g_cur->s.trainer_foe : g_cur->s.trainer_own;

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

static void battle_begin(uintptr_t eff)
{
    BvJob *j;
    int row;

    job_drop();
    sprite_size_reset();
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
    if (j->reach == 0 && row < 0) {
        row = bmap_spots_find(j->zone, j->trainer, j->px, j->pz);
    }
    if (j->reach == 0 && row < 0 && j->trainer) {
        row = bmap_spots_find_trainer(j->zone, 0, j->px, j->pz);
    }
    if (j->reach == 0 && row < 0 && getenv("EIDOS_DEBUG_BMAP_AT") != NULL) {
        row = bmap_spots_find(j->zone, !j->trainer, j->px, j->pz);  /* the previewed place */
    }
    if (row >= 0) {                     /* the row's view nearest the player */
        row = bmap_spots_pick_view(row, j->px, j->pz, j->facing);
    }
    bmap_spots_spec(bmap_spots_get(row), j->px, j->pz, j->facing, &j->spec);
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

static void capture(uintptr_t eff)
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
    if (g_cur != NULL) {
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
             * box may be loose: twice a Pokemon's size, a trainer taller.
             * The player's own trainer is pinned to the camera (MCSS_Draw):
             * where it is drawn, and a shield (a negative height): nothing is
             * drawn over it */
            g_frame.poke[g_frame.n_poke][3] = pos <= 7u ? 12.0f : 18.0f;
            if (pos <= 7u && g_n_sprites < 8u) {
                g_sprites[g_n_sprites].pos = pos;
                g_sprites[g_n_sprites].tex = rd32(mc, MCSSW_TEX);
                g_sprites[g_n_sprites].pltt = rd32(mc, MCSSW_PLTT);
                g_n_sprites++;
            }
            if (pos >= 8u && (pos & 1u) == 0u && g_cam_on) {
                float in[3];

                memcpy(in, g_frame.poke[g_frame.n_poke], sizeof in);
                pin_point(in, g_frame.poke[g_frame.n_poke]);
                g_frame.poke[g_frame.n_poke][3] = -18.0f;
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
 * (see MCSS_Draw below); g_cam_on 0 before the first.  For each sprite
 * (McssSys's works, by index) g_rest, its size at rest, which the scripts
 * chose for the camera; g_far, the size at rest they choose with the camera
 * all the way back (0.5 in the scripts read so far; it is learnt from what
 * they do); g_last_eye and g_still: the game's eye the last draw, and the
 * draws since it last moved. */
#define SPRITES_MAX 16
#define STILL_DRAWS 6               /* a size tween ends a step after its camera */
static float g_cam_game[2][3], g_cam_drawn[2][3];
static int g_cam_on;
static float g_rest[SPRITES_MAX], g_far = 0.5f, g_last_eye[3];
static int g_still, g_far_still;

static void sprite_size_reset(void)
{
    int i;

    g_cam_on = 0;
    for (i = 0; i < SPRITES_MAX; i++) {
        g_rest[i] = 0.0f;
    }
    g_still = g_far_still = 0;
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

/* How deep a point lies in front of a camera (along its line of sight). */
static float view_depth(const float cam[2][3], const float p[3])
{
    float f[3], len = 0.0f, d = 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = cam[1][i] - cam[0][i];
        len += f[i] * f[i];
    }
    len = sqrtf(len);
    for (i = 0; i < 3; i++) {
        d += (p[i] - cam[0][i]) * f[i] / len;
    }
    return d;
}

void __real_MCSS_Draw(void *sys);

/* A camera's own frame: f the way it looks, r its right, u its up. */
static void cam_frame(const float cam[2][3], float f[3], float r[3], float u[3])
{
    float len;
    int k;

    for (k = 0; k < 3; k++) {
        f[k] = cam[1][k] - cam[0][k];
    }
    len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (k = 0; k < 3; k++) {
        f[k] /= len > 1e-6f ? len : 1.0f;
    }
    r[0] = -f[2];
    r[1] = 0.0f;
    r[2] = f[0];
    len = sqrtf(r[0] * r[0] + r[2] * r[2]);
    r[0] /= len > 1e-6f ? len : 1.0f;
    r[2] /= len > 1e-6f ? len : 1.0f;
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
}

/* A point before the game's camera carried to the same place before the
 * camera drawn with (the player's own trainer, see MCSS_Draw). */
static void pin_point(const float in[3], float out[3])
{
    float fg[3], rg[3], ug[3], fd[3], rd[3], ud[3], o[3], c[3];
    int k;

    cam_frame((const float(*)[3])g_cam_game, fg, rg, ug);
    cam_frame((const float(*)[3])g_cam_drawn, fd, rd, ud);
    for (k = 0; k < 3; k++) {
        o[k] = in[k] - g_cam_game[0][k];
    }
    c[0] = o[0] * rg[0] + o[1] * rg[1] + o[2] * rg[2];
    c[1] = o[0] * ug[0] + o[1] * ug[1] + o[2] * ug[2];
    c[2] = o[0] * fg[0] + o[1] * fg[1] + o[2] * fg[2];
    for (k = 0; k < 3; k++) {
        out[k] = g_cam_drawn[0][k] + c[0] * rd[k] + c[1] * ud[k] + c[2] * fd[k];
    }
}

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
 * the camera, over the same frames (the wild intro takes the foe to 0.5;
 * the views 14 and 19 take the sides to 0.65 and 0.725, ov094's camera
 * command).  Those are chosen sizes, not the distances (the foe's would be
 * 0.62 for the intro), so against the map, which is real 3D, the Pokemon
 * grow and shrink as the camera moves.
 *
 * Each flat sprite is drawn here at the size its distance gives: its size
 * over its size at rest (the fake), times its depth before the game's
 * command camera (where the sizes are right) over its depth before the
 * camera drawn with.  The size at rest follows the size while the camera
 * moves (and a few draws after: the tween's last step comes late); with the
 * camera still a change is the script's own (a withdraw, a faint, a pop)
 * and stays.  A sprite that comes from nothing
 * (the send-out) grows toward the size the scripts give at this camera:
 * 1 at the command camera, g_far all the way back, in between in the part
 * the camera has gone back.  The scales are put back after. */
void __wrap_MCSS_Draw(void *sys)
{
    uintptr_t ms = (uintptr_t)sys, works, w;
    int32_t keep[SPRITES_MAX][2], keep_pos[SPRITES_MAX][3], v;
    int32_t n, i;
    int moving, pinned[SPRITES_MAX];
    float prog;

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
    {
        /* did the game's camera move lately: a size change now is the
         * scripts' fake */
        float dx = g_cam_game[0][0] - g_last_eye[0], dy = g_cam_game[0][1] - g_last_eye[1];
        float dz = g_cam_game[0][2] - g_last_eye[2];

        g_still = dx * dx + dy * dy + dz * dz > 1e-8f ? 0 : g_still + 1;
        moving = g_still < STILL_DRAWS;
        memcpy(g_last_eye, g_cam_game[0], sizeof g_last_eye);
        prog = bmap_intro_progress(g_cam_game[0]);
        g_far_still = prog > 0.99f && !moving ? g_far_still + 1 : 0;
    }
    for (i = 0; i < n; i++) {
        float p[3], q[3], dg, dd, f;
        uint32_t k;

        keep[i][0] = keep[i][1] = 0;
        pinned[i] = 0;
        w = rd32(works, 4u * (uint32_t)i);
        if (!gptr_ok(w)) {
            g_rest[i] = 0.0f;
            continue;
        }
        {
            /* the player's own trainer (positions 8, 10: the player's side)
             * is framed by the camera (the send-out's throw, bottom left):
             * it keeps the place and the size it has before the game's
             * camera, before the camera drawn with, whatever the place's
             * zoom, turn or slide (else a near intro camera passes it) */
            int pos = sprite_pos(w);

            if (pos >= 8 && (pos & 1) == 0) {
                float o[3], c[3];
                int kk;

                for (kk = 0; kk < 3; kk++) {
                    keep_pos[i][kk] = (int32_t)rd32(w, MCSSW_POS + 4u * (uint32_t)kk);
                    o[kk] = rdfx(w, MCSSW_POS + 4u * (uint32_t)kk);
                }
                pin_point(o, c);
                for (kk = 0; kk < 3; kk++) {
                    v = (int32_t)(c[kk] * 4096.0f);
                    memcpy((void *)(w + MCSSW_POS + 4u * (uint32_t)kk), &v, 4);
                }
                pinned[i] = 1;
                continue;               /* its own size, as the game frames it */
            }
        }
        {
            float sz = rdfx(w, MCSSW_SHADOWSCALE);

            if (sz <= 0.0f) {
                g_rest[i] = 0.0f;           /* gone: it comes back from nothing */
            } else if (moving) {
                g_rest[i] = sz;
            } else if (g_rest[i] <= 0.0f) {
                /* growing from nothing: toward the scripts' size here (a
                 * pop above it and back is the script's own) */
                g_rest[i] = 1.0f + prog * (g_far - 1.0f);
            }
            if (g_far_still > 30 && sz > 0.05f && sz == g_rest[i]) {
                g_far = sz;                 /* learnt: the size at rest all the way back */
            }
        }
        if (!(rd32(w, MCSSW_FLAGS) & (1u << 29))) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            p[k] = rdfx(w, MCSSW_POS + 4u * k) + rdfx(w, MCSSW_OFSPOS + 4u * k);
        }
        memcpy(q, p, sizeof q);
        bmap_battle_unwarp(q, g_cur->s.foe_dy, g_cur->s.foe_dz);   /* where the game's battle has it */
        dg = bmap_cmd_depth(q);
        {
            /* a foe's trainer is as tall as at its own slot (the game's,
             * before the place's move) wherever a script puts it: the game
             * draws it one size anywhere, so at the point a trainer comes
             * back to mid-battle (closer than its slot) the distance would
             * make it a third shorter than in the intro */
            int pos = sprite_pos(w);

            if (pos >= 9 && pos <= 13 && (pos & 1) && g_slot_base_have) {
                const int32_t *b = g_slot_base[SLOT_ENTRIES - 6u + (uint32_t)(pos - 8)];
                float slot[3];

                slot[0] = (float)b[0] / 4096.0f;
                slot[1] = (float)b[1] / 4096.0f;
                slot[2] = (float)b[2] / 4096.0f;
                dg = bmap_cmd_depth(slot) * (g_cur->s.trainer_size > 0.0f ? g_cur->s.trainer_size : 1.0f);
            }
        }
        dd = view_depth((const float(*)[3])g_cam_drawn, p);
        if (dg < 0.5f || dd < 0.5f) {
            continue;
        }
        f = fminf(fmaxf(dg / (dd * (g_rest[i] > 0.05f ? g_rest[i] : 1.0f)), 0.25f), 6.0f);
        if (log_on()) {
            fprintf(stderr, "battle_view: frame %u sprite %d pos %d at (%.2f %.2f %.2f) command depth %.2f drawn %.2f scale x%.2f size %.3f rest %.3f game scale %.3f\n",
                    (unsigned)bw_input_frame_number(), (int)i, sprite_pos(w), (double)p[0], (double)p[1], (double)p[2],
                    (double)dg, (double)dd, (double)f, (double)rdfx(w, MCSSW_SHADOWSCALE), (double)g_rest[i],
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

void __wrap_G3D_SYSTEM_DrawObject(void *obj, void *status)
{
    uintptr_t eff = 0;
    int kind;

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
        }
        g_in_battle = 1;
        g_last_frame = now;
        if (g_job != NULL && g_job->done) {
            job_finish();
        }
        if (kind == OBJ_FIELD) {
            capture(eff);
            return;
        }
        if (eidos_settings_get(EIDOS_OPT_BATTLE_PLATFORMS) == 0) {
            return;
        }
    }
    __real_G3D_SYSTEM_DrawObject(obj, status);
}
