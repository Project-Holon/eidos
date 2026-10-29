/*
 * engine/world/bmap_place.c -- the map in battle space.
 *
 * The battle is a small world (1 unit = 1.0 in fx32): the own side is at
 * +z, the foe's at -z, the sprites stand on y = 0.4.  A map point M maps to
 *
 *     u  = M - P                                   (P: the player)
 *     bx = k * (cos a * u.x - sin a * u.z) + sx
 *     bz = k * (sin a * u.x + cos a * u.z) + sz
 *     by = 0.4 + k * (M.y - ground(P)) - lift
 *
 * with a the player's facing as an angle, k = BMAP_PLACE_K and (sx, sz) =
 * (0, 7), the own slot's z.  That is a rotation about y by the facing: the
 * direction the player faces becomes -z, toward the foe.  Facing south
 * (0x8000) turns map +z into battle -z, facing north keeps it.  There is no
 * mirroring: +x stays to the right of the camera.
 *
 * The ground is lowered by `lift` so the shadow quad, which lies flat at the
 * sprite's own height, is not hidden by it.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmap_place.h"

#define PLACE_SX 0.0f
#define PLACE_SZ 7.0f
#define GROUND_Y 0.4f
#define GROUND_LIFT 0.02f
/* Tall grass: the field draws it as a stack of flat leafy sheets a little
 * above the ground, made for its high camera; from the battle's low camera
 * the sheets float.  In battle the sheets are replaced by upright tufts: on
 * every grass tile the same three crossed tufts of the sheet's own texture,
 * standing on the ground.  A grass sheet is a flat, cut-out or translucent
 * draw less than GRASS_MAX_H map units above the ground. */
#define GRASS_MAX_H 20.0f
#define TUFT_SPAN   160             /* tiles per side around the battle (the
                                       editor's field view loads a wide area) */
/* the single battle's foe slot; the ground is levelled between it and the
 * own slot so a slope or a step puts neither one far in or above the ground */
#define FOE_SX 0.3f
#define FOE_SZ_GAME -10.0f
/* The foe's slot as this placement has it: the game's, pushed back by the
 * row's distance (bmap_place_spec sets it before the search; one placement
 * runs at a time, on the worker). */
static float g_foe_z = FOE_SZ_GAME;
#define FOE_SZ g_foe_z
/* the most the ground under the two Pokemon may differ (map units, a
 * little over a tile): a leader one step up on a stage is fine; more and
 * the levelled ground leaves one in the air or in the floor */
#define STEP_MAX 20.0f
/* how far (tiles) the centring looks for a wall on each side of the line */
#define CENTRE_TILES 12

/* Append a quad (two triangles) to the scene's vertex array. */
static void quad(BmapVertex *v, const float p[4][3], const BmapVertex *src)
{
    static const int ix[6] = { 0, 1, 2, 0, 2, 3 };
    static const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
    int k;

    for (k = 0; k < 6; k++) {
        v[k] = *src;
        v[k].x = p[ix[k]][0];
        v[k].y = p[ix[k]][1];
        v[k].z = p[ix[k]][2];
        v[k].u = uv[ix[k]][0];
        v[k].v = uv[ix[k]][1];
        v[k].a = 255;
    }
}

/* Whether a grass sheet is a leafy one (its opaque texels are green), not
 * the soft shadow sheet under the leaves. */
static int leafy(const BmapScene *s, const BmapDraw *g)
{
    const BmapTexture *t;
    unsigned long sr = 0, sg = 0, sb = 0, cnt = 0;
    int q;

    if (g->translucent || g->tex < 0 || (unsigned)g->tex >= s->nt) {
        return 0;
    }
    t = &s->t[g->tex];
    for (q = 0; q < t->w * t->h; q++) {
        if (t->rgba[q * 4 + 3] > 128) {
            sr += t->rgba[q * 4];
            sg += t->rgba[q * 4 + 1];
            sb += t->rgba[q * 4 + 2];
            cnt++;
        }
    }
    return cnt > 0 && sg >= sr + cnt * 16 && sg >= sb + cnt * 16;   /* green, not a blue rug */
}

/* The tufts, in battle space (the camera looks down -z), one set per tile
 * cell of `tile` units: the same three tufts, the same height, on every
 * cell, the whole grass picture on each; the grass triangles are dropped
 * after. */
static unsigned char *g_tmask;      /* the grass triangles (bmap_place_spec), by index */

static void grass_grow_fixed(BmapScene *s, const unsigned char *grass, unsigned nd0, float tile)
{
    const unsigned char *mask = g_tmask;
    static const float spot[3][2] = { { -0.25f, -0.2f }, { 0.25f, -0.2f }, { 0.0f, 0.25f } };
    const float w = 0.3f * tile * 0.7071f, hh = 0.5f * tile;
    unsigned i, need = 0, nv, nd;
    BmapVertex *v;
    BmapDraw *d;
    unsigned char *used;

    for (i = 0; i < nd0; i++) {
        if (grass[i]) {
            need += s->d[i].count / 3 * 8 * 36;
        }
    }
    if (need > 0) {
        v = (BmapVertex *)realloc(s->v, (s->nv + need) * sizeof *v);
        d = v != NULL ? (BmapDraw *)realloc(s->d, (s->nd + nd0) * sizeof *d) : NULL;
        used = d != NULL ? (unsigned char *)calloc((size_t)TUFT_SPAN * TUFT_SPAN, 1) : NULL;
        if (v != NULL) s->v = v;
        if (d != NULL) s->d = d;
    } else {
        used = NULL;
    }
    nv = s->nv;
    nd = s->nd;
    for (i = 0; used != NULL && i < nd0; i++) {
        BmapDraw *g = &s->d[i];
        BmapDraw n;
        unsigned j, start = nv;

        if (!grass[i]) {
            continue;
        }
        for (j = g->first; j + 2 < g->first + g->count; j += 3) {
            const BmapVertex *a = &s->v[j], *b = &s->v[j + 1], *c = &s->v[j + 2];
            float x0, x1;

            if (mask != NULL && !mask[j / 3]) {
                continue;               /* not grass (a rug, a path in the same draw) */
            }
            x0 = fminf(a->x, fminf(b->x, c->x));
            x1 = fmaxf(a->x, fmaxf(b->x, c->x));
            {
            float z0 = fminf(a->z, fminf(b->z, c->z)), z1 = fmaxf(a->z, fmaxf(b->z, c->z));
            int cx, cz;

            for (cx = (int)floorf(x0 / tile); (float)cx * tile <= x1; cx++) {
                for (cz = (int)floorf(z0 / tile); (float)cz * tile <= z1; cz++) {
                    float mx = ((float)cx + 0.5f) * tile, mz = ((float)cz + 0.5f) * tile;
                    float d1 = (b->x - a->x) * (mz - a->z) - (b->z - a->z) * (mx - a->x);
                    float d2 = (c->x - b->x) * (mz - b->z) - (c->z - b->z) * (mx - b->x);
                    float d3 = (a->x - c->x) * (mz - c->z) - (a->z - c->z) * (mx - c->x);
                    int ux = cx + TUFT_SPAN / 2, uz = cz + TUFT_SPAN / 2 + 8, t;
                    float y = a->y;

                    if (!((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))) continue;
                    if (ux < 0 || uz < 0 || ux >= TUFT_SPAN || uz >= TUFT_SPAN || used[uz * TUFT_SPAN + ux]) continue;
                    used[uz * TUFT_SPAN + ux] = 1;
                    for (t = 0; t < 3 && nv + 12 <= s->nv + need; t++) {
                        float ox = mx + spot[t][0] * tile, oz = mz + spot[t][1] * tile;
                        float q1[4][3] = { { ox - w, y, oz - w }, { ox + w, y, oz + w }, { ox + w, y + hh, oz + w }, { ox - w, y + hh, oz - w } };
                        float q2[4][3] = { { ox - w, y, oz + w }, { ox + w, y, oz - w }, { ox + w, y + hh, oz - w }, { ox - w, y + hh, oz + w } };

                        quad(&s->v[nv], (const float (*)[3])q1, a);
                        nv += 6;
                        quad(&s->v[nv], (const float (*)[3])q2, a);
                        nv += 6;
                    }
                }
            }
            }
        }
        if (nv > start) {
            n = *g;
            n.first = start;
            n.count = nv - start;
            n.cull = 0;
            n.billboard = 0;
            n.grass = 1;
            s->d[nd++] = n;
        }
    }
    free(used);
    for (i = 0; i < nd0; i++) {
        unsigned j;

        if (!grass[i]) {
            continue;
        }
        /* the grass triangles: the tufts stand in their place (folded to
         * nothing; the rest of the draw stays) */
        for (j = s->d[i].first; j + 2 < s->d[i].first + s->d[i].count; j += 3) {
            if (mask == NULL || mask[j / 3]) {
                s->v[j + 1] = s->v[j];
                s->v[j + 2] = s->v[j];
            }
        }
    }
    s->nv = nv;
    s->nd = nd;
}

/* The battle's spaces, in battle units: each Pokemon's footprint (a disc
 * of SLOT_R around its slot) and the camera's space (the box the camera
 * and its view of the Pokemon move through in ordinary moves).  Anything
 * rising more than CLEAR_H above the ground in them is in the way. */
#define SLOT_R   2.2f
#define CLEAR_H  1.0f
#define CAM_X0 -17.0f
#define CAM_X1  17.0f
#define CAM_Z0 -13.0f
#define CAM_Z1  34.0f
#define CAM_TOP 14.0f

/* A map point in battle space for the placement (px, pz, angle). */
static void to_battle(float mx, float my, float mz, float px, float pz, float gy, float ca,
                      float sa, float k, float out[3])
{
    float ux = mx - px, uz = mz - pz;

    out[0] = k * (ca * ux - sa * uz) + PLACE_SX;
    out[2] = k * (sa * ux + ca * uz) + PLACE_SZ;
    out[1] = GROUND_Y + k * (my - gy);
}

/* A double battle's four places (ov094 gBtlvMcssSlotPosDouble): the
 * player's two at +z, the foe's two at -z (pushed back with the foe's
 * slot); they reach about 2.5 units either side of the single's line. */
static const float k_double[4][2] = {
    { -1.69f, 6.38f }, { 2.5f, 7.62f }, { 2.43f, -10.38f }, { -2.2f, -11.56f },
};
#define DOUBLE_R 1.6f

static int in_slot(float x, float z)
{
    float ax = x - 0.5f, az = z - PLACE_SZ, fx = x - FOE_SX, fz = z - FOE_SZ;
    int i;

    if (ax * ax + az * az < SLOT_R * SLOT_R || fx * fx + fz * fz < SLOT_R * SLOT_R) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        float dx = x - k_double[i][0], dz = z - k_double[i][1] - (i >= 2 ? FOE_SZ - FOE_SZ_GAME : 0.0f);

        if (dx * dx + dz * dz < DOUBLE_R * DOUBLE_R) {
            return 1;
        }
    }
    return 0;
}

/* The battle's cameras that hold longest (battle units, read from the
 * game's camera in battles): the command menu's and the one the intro and
 * the send-out start from, far behind the player's side. */
#define N_EYE 2
static float k_eyes[N_EYE][2][3] = {
    { { 6.7f, 6.7f, 17.3f }, { 0.0f, 2.6f, 0.0f } },
    { { 16.7f, 7.7f, 33.3f }, { 6.0f, 4.6f, 16.0f } },
};
static const float k_intro_eye[3] = { 16.7f, 7.7f, 33.3f };
static const float k_intro_target[3] = { 6.0f, 4.6f, 16.0f };
/* The sight lines: from each camera to the feet, the middle and the head
 * of each Pokemon, and the box they span. */
#define N_SIGHT 6
static float k_sight[N_SIGHT][3] = {
    { 0.5f, 0.6f, 7.0f }, { 0.5f, 1.8f, 7.0f }, { 0.5f, 3.2f, 7.0f },
    { FOE_SX, 0.6f, FOE_SZ_GAME }, { FOE_SX, 1.8f, FOE_SZ_GAME }, { FOE_SX, 3.2f, FOE_SZ_GAME },
};
#define SIGHT_X0 -0.5f
#define SIGHT_X1 17.5f
#define SIGHT_Z0 -26.0f
#define SIGHT_Z1 34.0f
/* In front of a camera's lens: closer than LENS_NEAR along its view and
 * inside its picture, something fills the screen. */
#define LENS_NEAR 5.0f

static int near_lens(int e, const float p[3])
{
    const float *eye = k_eyes[e][0], *at = k_eyes[e][1];
    float f[3], v[3], len = 0.0f, d = 0.0f, side2 = 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = at[i] - eye[i];
        len += f[i] * f[i];
    }
    len = sqrtf(len);
    for (i = 0; i < 3; i++) {
        f[i] /= len;
        v[i] = p[i] - eye[i];
        d += v[i] * f[i];
    }
    if (d < 0.3f || d > LENS_NEAR) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        float o = v[i] - f[i] * d;

        side2 += o * o;
    }
    return side2 < 0.36f * d * d;       /* inside the picture, about */
}

/* Whether the segment a..b crosses the triangle (p0, p1, p2), strictly
 * between its ends (Moller and Trumbore). */
static int seg_hits_tri(const float a[3], const float b[3], const float p0[3], const float p1[3],
                        const float p2[3])
{
    float d[3], e1[3], e2[3], h[3], s[3], qv[3], det, f, u, v, t;
    int i;

    for (i = 0; i < 3; i++) {
        d[i] = b[i] - a[i];
        e1[i] = p1[i] - p0[i];
        e2[i] = p2[i] - p0[i];
    }
    h[0] = d[1] * e2[2] - d[2] * e2[1];
    h[1] = d[2] * e2[0] - d[0] * e2[2];
    h[2] = d[0] * e2[1] - d[1] * e2[0];
    det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (fabsf(det) < 1e-6f) {
        return 0;
    }
    f = 1.0f / det;
    for (i = 0; i < 3; i++) {
        s[i] = a[i] - p0[i];
    }
    u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
    if (u < 0.0f || u > 1.0f) {
        return 0;
    }
    qv[0] = s[1] * e1[2] - s[2] * e1[1];
    qv[1] = s[2] * e1[0] - s[0] * e1[2];
    qv[2] = s[0] * e1[1] - s[1] * e1[0];
    v = f * (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]);
    if (v < 0.0f || u + v > 1.0f) {
        return 0;
    }
    t = f * (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]);
    return t > 0.02f && t < 0.97f;
}

/* A map point for a battle point under the placement (px, pz, angle, k):
 * to_battle inverted. */
static void to_map(float bx, float bz, float px, float pz, float ca, float sa, float k, float *mx,
                   float *mz)
{
    float x = (bx - PLACE_SX) / k, z = (bz - PLACE_SZ) / k;

    *mx = px + ca * x + sa * z;
    *mz = pz - sa * x + ca * z;
}

/* What a walk-grid tile costs: under a Pokemon, on the field between the
 * two, in the camera's space near the Pokemon and further out.  Open floor
 * is free; a blocked tile (a wall, a table, a tree, a rock) costs; a tile
 * outside the map (beyond a room's walls) costs most.  Water is open for a
 * battle on water and nearly so for the camera.  `home` is the player's own
 * tile: on a map without a walk grid (rail maps) no tile has data, and none
 * costs. */
enum { AT_SLOT, AT_FIELD, AT_NEAR, AT_FAR };

static float tile_cost(int cls, int where, int home)
{
    static const float cost[4][4] = {
        /*            NONE   BLOCKED WATER  OPEN */
        /* slot  */ { 40.0f, 30.0f,  8.0f,  0.0f },
        /* field */ { 8.0f,  4.0f,   1.5f,  0.0f },
        /* near  */ { 1.0f,  0.4f,   0.0f,  0.0f },
        /* far   */ { 1.0f,  0.4f,   0.0f,  0.0f },
    };

    if ((cls == BMAP_TILE_WATER && home == BMAP_TILE_WATER) || (cls == BMAP_TILE_NONE && home == BMAP_TILE_NONE)) {
        return 0.0f;    /* a battle on water; a map with no walk grid (rails) */
    }
    return cost[where][cls];
}

/* Tests (BMAP_PLACE_LOG): the running score after each part of place_score,
 * printed with BMAP_PLACE_LOG for a spot used as given */
static float g_part[7];

/* How much is in the way for one placement (lower is better):
 *   - the game's walk grid under each Pokemon, on the field between them and
 *     over the camera's space (the camera's share is a fraction, so a larger
 *     scale, which covers fewer tiles, gains nothing by it);
 *   - triangles rising out of the ground in a Pokemon's footprint (a tree,
 *     a sign), between the camera and the Pokemon (nothing is left out of
 *     the view: what stands there hides the battle) and, a little, in the
 *     rest of the camera's space;
 *   - a slope or a step between the two Pokemon, and the own Pokemon's
 *     ground away from the player's own. */
static float place_score(const BmapScene *s, const unsigned char *grass, float px, float pz,
                         double a, float k, float gp, int home)
{
    float ca = (float)cos(a), sa = (float)sin(a);
    float g0, g1, ox, oz, fx, fz, score = 0.0f, tris = 0.0f, near2, step;
    float bad = 0.0f, wsum = 0.0f, bx, bz;
    unsigned char hidden[N_EYE][N_SIGHT] = { { 0 } }, lens[N_EYE] = { 0 };
    unsigned i, j;
    int q;

    to_map(0.5f, PLACE_SZ, px, pz, ca, sa, k, &ox, &oz);
    to_map(FOE_SX, FOE_SZ, px, pz, ca, sa, k, &fx, &fz);
    g0 = bmap_ground_y(s, ox, oz);
    g1 = bmap_ground_y(s, fx, fz);
    score += 4.0f * fabsf(g1 - g0) * k;                 /* a slope or a step */
    if (fabsf(g1 - g0) > STEP_MAX) {
        score += 60.0f;     /* the two on different levels: one floats or sinks */
    }
    if (bmap_tile_encounter(s, px, pz) && !bmap_tile_encounter(s, fx, fz)) {
        score += 3.0f;      /* a wild Pokemon comes out of the grass ahead      */
    }
    score += 2.0f * fabsf(g0 - gp) * k;                 /* off the player's level */
    g_part[0] = score;
    /* the footprints: the centre and four points around it */
    for (q = 0; q < 5; q++) {
        static const float o[5][2] = { { 0, 0 }, { 0.7f, 0 }, { -0.7f, 0 }, { 0, 0.7f }, { 0, -0.7f } };
        float mx, mz;

        to_map(0.5f + o[q][0] * SLOT_R, PLACE_SZ + o[q][1] * SLOT_R, px, pz, ca, sa, k, &mx, &mz);
        score += tile_cost(bmap_tile(s, mx, mz), AT_SLOT, home) * (q == 0 ? 1.0f : 0.5f);
        to_map(FOE_SX + o[q][0] * SLOT_R, FOE_SZ + o[q][1] * SLOT_R, px, pz, ca, sa, k, &mx, &mz);
        score += tile_cost(bmap_tile(s, mx, mz), AT_SLOT, home) * (q == 0 ? 1.0f : 0.5f);
    }
    /* a double battle's four places: half the weight (most battles are
     * single), but a wall there shows in every double battle here */
    for (q = 0; q < 4; q++) {
        float mx, mz;

        to_map(k_double[q][0], k_double[q][1] + (q >= 2 ? FOE_SZ - FOE_SZ_GAME : 0.0f), px, pz, ca, sa, k, &mx, &mz);
        score += 0.5f * tile_cost(bmap_tile(s, mx, mz), AT_SLOT, home);
    }
    g_part[1] = score;
    /* the space behind the foe: the backdrop of the usual view, a wall right
     * behind it costs most */
    for (q = 0; q < 3; q++) {
        static const float bzq[3] = { -13.0f, -17.0f, -21.0f }, wq[3] = { 1.5f, 0.75f, 0.4f };
        float mx, mz;

        to_map(FOE_SX, bzq[q] + FOE_SZ - FOE_SZ_GAME, px, pz, ca, sa, k, &mx, &mz);
        score += wq[q] * tile_cost(bmap_tile(s, mx, mz), AT_FIELD, home);
    }
    g_part[2] = score;
    /* In a room or a corridor (walls on both sides of the battle line), the
     * line runs down the middle: at the own slot, the foe's slot and
     * between them, the floor reaching left and right of the line should
     * be the same.  Open on a side (outdoors), there is no middle. */
    for (q = 0; q < 3; q++) {
        const float lz[3] = { PLACE_SZ, 0.5f * (PLACE_SZ + FOE_SZ), FOE_SZ };
        static const float lx[3] = { 0.5f, 0.4f, FOE_SX };
        int side, reach_t[2];

        for (side = 0; side < 2; side++) {
            int d;

            reach_t[side] = -1;
            for (d = 1; d <= CENTRE_TILES; d++) {
                float mx, mz;
                int t;

                to_map(lx[q] + (side ? 1.0f : -1.0f) * (float)d * 16.0f * k, lz[q], px, pz, ca, sa, k, &mx,
                       &mz);
                t = bmap_tile(s, mx, mz);
                if (t == BMAP_TILE_BLOCKED || (t == BMAP_TILE_NONE && home != BMAP_TILE_NONE)) {
                    reach_t[side] = d;
                    break;
                }
            }
        }
        if (reach_t[0] > 0 && reach_t[1] > 0) {
            score += 1.5f * (float)abs(reach_t[0] - reach_t[1]);
        }
    }
    g_part[3] = score;
    /* the field between them */
    for (q = 1; q < 8; q++) {
        float t = (float)q / 8.0f, mx, mz;

        to_map(0.5f + (FOE_SX - 0.5f) * t, PLACE_SZ + (FOE_SZ - PLACE_SZ) * t, px, pz, ca, sa, k, &mx, &mz);
        score += tile_cost(bmap_tile(s, mx, mz), AT_FIELD, home);
    }
    g_part[4] = score;
    /* the camera's space, one sample a tile */
    step = 16.0f * k;
    for (bz = CAM_Z0 + 0.5f * step; bz < CAM_Z1; bz += step) {
        for (bx = CAM_X0 + 0.5f * step; bx < CAM_X1; bx += step) {
            int nearby = bx > -10.0f && bx < 10.0f && bz < 14.0f;
            float w = nearby ? 1.0f : 0.3f, mx, mz;

            to_map(bx, bz, px, pz, ca, sa, k, &mx, &mz);
            bad += w * tile_cost(bmap_tile(s, mx, mz), nearby ? AT_NEAR : AT_FAR, home);
            wsum += w;
        }
    }
    if (wsum > 0.0f) {
        score += 30.0f * bad / wsum;
    }
    g_part[5] = score;
    /* what rises out of the ground */
    near2 = (40.0f / k) * (40.0f / k);
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->far || d->count == 0 || (grass != NULL && grass[i]) || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            const BmapVertex *v = &s->v[j];
            float p[3][3], lo[3], hi[3];

            if ((v->x - px) * (v->x - px) + (v->z - pz) * (v->z - pz) > near2) {
                continue;
            }
            {
                /* a floor (a stage, a raised walkway) is ground to stand on,
                 * not something in the way, however high it is */
                float ux = v[1].x - v[0].x, uy = v[1].y - v[0].y, uz = v[1].z - v[0].z;
                float wx = v[2].x - v[0].x, wy = v[2].y - v[0].y, wz = v[2].z - v[0].z;
                float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;

                if (ny * ny > 0.5f * (nx * nx + ny * ny + nz * nz)) {
                    continue;
                }
            }
            for (q = 0; q < 3; q++) {
                to_battle(v[q].x, v[q].y, v[q].z, px, pz, g0, ca, sa, k, p[q]);
            }
            for (q = 0; q < 3; q++) {
                lo[q] = fminf(p[0][q], fminf(p[1][q], p[2][q]));
                hi[q] = fmaxf(p[0][q], fmaxf(p[1][q], p[2][q]));
            }
            if (hi[1] < GROUND_Y + CLEAR_H || lo[1] > GROUND_Y + CAM_TOP) {
                continue;               /* the ground, or high above        */
            }
            if (hi[0] < CAM_X0 || lo[0] > CAM_X1 || hi[2] < CAM_Z0 || lo[2] > CAM_Z1) {
                continue;
            }
            for (q = 0; q < 3; q++) {
                if (in_slot(p[q][0], p[q][2])) {
                    score += 20.0f;     /* in a Pokemon's footprint         */
                    if (getenv("BMAP_FOOT_LOG")) fprintf(stderr, "foot obj %u draw %u map (%.0f %.0f %.0f) battle (%.1f %.1f %.1f)-(%.1f %.1f)\n", v->obj, i, v->x, v->y, v->z, lo[0], lo[1], lo[2], hi[1], hi[2]); /*DBG*/
                    break;
                }
            }
            tris += 0.03f;
            /* the camera's sight of each Pokemon: whatever crosses a line from
             * the camera to its feet, middle or head hides it (a wall near
             * the camera hides the foe however far from the foe it stands) */
            if (lo[2] < SIGHT_Z1 && hi[2] > SIGHT_Z0 && lo[0] < SIGHT_X1 && hi[0] > SIGHT_X0) {
                int e;

                for (e = 0; e < N_EYE; e++) {
                    for (q = 0; q < N_SIGHT; q++) {
                        if (!hidden[e][q] && seg_hits_tri(k_eyes[e][0], k_sight[q], p[0], p[1], p[2])) {
                            hidden[e][q] = 1;
                        }
                    }
                    if (!lens[e] && (near_lens(e, p[0]) || near_lens(e, p[1]) || near_lens(e, p[2]))) {
                        lens[e] = 1;
                    }
                }
            }
        }
    }
    for (q = 0; q < N_SIGHT; q++) {
        score += hidden[0][q] ? 15.0f : 0.0f;   /* the command camera: most of the battle */
        score += hidden[1][q] ? 6.0f : 0.0f;
    }
    score += lens[0] ? 30.0f : 0.0f;
    score += lens[1] ? 20.0f : 0.0f;
    g_part[6] = fminf(tris, 40.0f);
    return score + fminf(tris, 40.0f);
}

/* Tests (BMAP_TOPVIEW=file): the scene in map units, the player's
 * spot and the chosen placement, for tools/topview.py.  Called with
 * gone == 0 before the placement moves anything, then with gone == 1 after
 * clear_camera_space to append one byte per triangle (1 = left out). */
static void topview_dump(const BmapScene *s, unsigned nd0, int gone, const float hdr[6])
{
    const char *path = getenv("BMAP_TOPVIEW");
    FILE *f;
    unsigned i, j, n = 0;

    if (path == NULL || (f = fopen(path, gone ? "ab" : "wb")) == NULL) {
        return;
    }
    for (i = 0; i < nd0; i++) {
        if (s->d[i].first + s->d[i].count <= s->nv) n += s->d[i].count / 3;
    }
    if (!gone) {
        fwrite("BTV2", 1, 4, f);
        fwrite(hdr, sizeof(float), 6, f);
        fwrite(&n, 4, 1, f);
    }
    for (i = 0; i < nd0; i++) {
        const BmapDraw *d = &s->d[i];
        const BmapTexture *t = d->tex >= 0 && (unsigned)d->tex < s->nt ? &s->t[d->tex] : NULL;
        float tc[3] = { 255.0f, 255.0f, 255.0f };

        if (d->first + d->count > s->nv) continue;
        if (!gone && t != NULL) {
            double sum[3] = { 0, 0, 0 }, m = 0;

            for (j = 0; j < (unsigned)(t->w * t->h); j++) {
                if (t->rgba[j * 4 + 3] < 128) continue;
                sum[0] += t->rgba[j * 4]; sum[1] += t->rgba[j * 4 + 1]; sum[2] += t->rgba[j * 4 + 2];
                m += 1;
            }
            if (m > 0) {
                tc[0] = (float)(sum[0] / m); tc[1] = (float)(sum[1] / m); tc[2] = (float)(sum[2] / m);
            }
        }
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            const BmapVertex *v = &s->v[j];
            unsigned char c[4];
            float p[9];
            int q;

            if (gone) {
                c[0] = v->obj != 0 && v->y <= -999.0f;
                fwrite(c, 1, 1, f);
                continue;
            }
            for (q = 0; q < 3; q++) {
                p[q * 3] = v[q].x; p[q * 3 + 1] = v[q].y; p[q * 3 + 2] = v[q].z;
            }
            c[0] = (unsigned char)(tc[0] * v->r / 255.0f);
            c[1] = (unsigned char)(tc[1] * v->g / 255.0f);
            c[2] = (unsigned char)(tc[2] * v->b / 255.0f);
            c[3] = (unsigned char)((d->billboard ? 1 : 0) | (d->far ? 4 : 0) | (v->obj == 0 ? 8 : 0));
            if (d->billboard) {             /* a billboard: its pivot, as a point */
                for (q = 0; q < 3; q++) {
                    p[q * 3] = d->pivot[0]; p[q * 3 + 1] = d->pivot[1]; p[q * 3 + 2] = d->pivot[2];
                }
            }
            fwrite(p, sizeof(float), 9, f);
            fwrite(c, 1, 4, f);
        }
    }
    if (!gone) {                        /* the walk grid, 64 x 64 tiles */
        int t0[2], x, z;

        t0[0] = (int)floorf(hdr[0] / 16.0f) - 32;
        t0[1] = (int)floorf(hdr[1] / 16.0f) - 32;
        fwrite(t0, sizeof(int), 2, f);
        for (z = 0; z < 64; z++) {
            for (x = 0; x < 64; x++) {
                unsigned char t = (unsigned char)bmap_tile(s, (float)(t0[0] + x) * 16.0f + 8.0f,
                                                           (float)(t0[1] + z) * 16.0f + 8.0f);

                fwrite(&t, 1, 1, f);
            }
        }
    }
    fclose(f);
}

int bmap_copy(BmapScene *dst, const BmapScene *src)
{
    *dst = *src;
    dst->shared = 1;
    dst->v = (BmapVertex *)malloc((src->nv > 0 ? src->nv : 1) * sizeof *dst->v);
    dst->d = (BmapDraw *)malloc((src->nd > 0 ? src->nd : 1) * sizeof *dst->d);
    if (dst->v == NULL || dst->d == NULL) {
        free(dst->v);
        free(dst->d);
        dst->v = NULL;
        dst->d = NULL;
        return -1;
    }
    memcpy(dst->v, src->v, src->nv * sizeof *dst->v);
    memcpy(dst->d, src->d, src->nd * sizeof *dst->d);
    return 0;
}

/* the command camera's eye (where the battle settles) and target */
static const float k_cmd_eye[3] = { 6.7f, 6.7f, 17.3f };
static const float k_cmd_target[3] = { 0.0f, 2.6f, 0.0f };

int bmap_intro_none(const BmapIntro *in)
{
    return in == NULL
           || ((in->zoom == 1.0f || in->zoom <= 0.0f) && in->yaw == 0.0f && in->lift == 0.0f && in->side == 0.0f
               && in->tilt == 0.0f && in->aim == 0.0f);
}

float bmap_intro_progress(const float eye[3])
{
    float d[3], dd = 0.0f, w = 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        d[i] = k_intro_eye[i] - k_cmd_eye[i];
        dd += d[i] * d[i];
        w += (eye[i] - k_cmd_eye[i]) * d[i];
    }
    w /= dd;
    return w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
}

float bmap_cmd_depth(const float p[3])
{
    static const float tgt[3] = { 0.0f, 2.6f, 0.0f };
    float f[3], len = 0.0f, d = 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = tgt[i] - k_cmd_eye[i];
        len += f[i] * f[i];
    }
    len = sqrtf(len);
    for (i = 0; i < 3; i++) {
        d += (p[i] - k_cmd_eye[i]) * f[i] / len;
    }
    return d;
}

/* A camera moved by the settings `in`, in the part `w` (0 to 1).  The zoom
 * moves the eye by (1 - 1 / zoom) of `toward`, a vector chosen by the
 * caller; the camera then turns about the upright line through the
 * battle's centre, rises, slides to its right and turns its look. */
static void camera_shift(float eye[3], float target[3], const BmapIntro *in, float w, const float toward[3])
{
    const double pi = 3.14159265358979323846;
    float ca, sa, x, zoom = in->zoom > 0.0f ? in->zoom : 1.0f;
    int i;

    if (zoom != 1.0f) {
        float k = w * (1.0f - 1.0f / zoom);     /* below 1: further out */

        for (i = 0; i < 3; i++) {
            eye[i] += k * toward[i];
        }
    }
    /* turned about the upright line through the battle's centre, eye and
     * target together, and raised */
    if (in->yaw != 0.0f) {
        ca = (float)cos(in->yaw * w * pi / 180.0);
        sa = (float)sin(in->yaw * w * pi / 180.0);
        x = eye[0];
        eye[0] = x * ca + eye[2] * sa;
        eye[2] = eye[2] * ca - x * sa;
        x = target[0];
        target[0] = x * ca + target[2] * sa;
        target[2] = target[2] * ca - x * sa;
    }
    eye[1] += in->lift * w;
    target[1] += in->lift * w;
    {
        /* its own frame: f the way it looks, r its right (level) */
        float f[3], h, r[2], L, pitch, head;

        for (i = 0; i < 3; i++) {
            f[i] = target[i] - eye[i];
        }
        h = sqrtf(f[0] * f[0] + f[2] * f[2]);
        if (h < 1e-4f) {
            return;
        }
        r[0] = -f[2] / h;
        r[1] = f[0] / h;
        /* slid sideways, eye and target */
        eye[0] += r[0] * in->side * w;
        eye[2] += r[1] * in->side * w;
        target[0] += r[0] * in->side * w;
        target[2] += r[1] * in->side * w;
        /* the look turned and tilted about the eye, its length kept */
        L = sqrtf(h * h + f[1] * f[1]);
        pitch = atan2f(f[1], h) + (float)(in->tilt * w * pi / 180.0);
        head = atan2f(f[0], f[2]) - (float)(in->aim * w * pi / 180.0);
        if (pitch > 1.5f) {
            pitch = 1.5f;
        } else if (pitch < -1.5f) {
            pitch = -1.5f;
        }
        target[0] = eye[0] + L * cosf(pitch) * sinf(head);
        target[1] = eye[1] + L * sinf(pitch);
        target[2] = eye[2] + L * cosf(pitch) * cosf(head);
    }
}

/* The intro's change on a camera at progress w along the intro's line.
 * Closer: the eye moves the intro's eye's way to its target, by the part
 * `w` of what the full zoom moves the intro's eye.  The same move for every
 * camera, not one toward each camera's own target: the target travels out
 * with the eye during the pull, and a move toward it would take the eye in
 * front of the command camera and back. */
static void intro_shift(float eye[3], float target[3], const BmapIntro *in, float w)
{
    float toward[3];
    int i;

    if (bmap_intro_none(in) || w <= 0.0f) {
        return;
    }
    for (i = 0; i < 3; i++) {
        toward[i] = k_intro_target[i] - k_intro_eye[i];
    }
    camera_shift(eye, target, in, w, toward);
}

void bmap_camera_view(float eye[3], float target[3], const BmapIntro *cam, float w)
{
    float toward[3];
    int i;

    if (bmap_intro_none(cam) || w <= 0.0f) {
        return;
    }
    for (i = 0; i < 3; i++) {
        toward[i] = target[i] - eye[i];
    }
    camera_shift(eye, target, cam, w, toward);
}

/* how far the camera is on the way from the command camera's eye to the
 * intro's (bmap_intro_progress).  The game's intro pulls the camera back
 * along that line, so the place's change grows with the pull and the
 * camera moves one way only. */
void bmap_camera_intro(float eye[3], float target[3], const BmapIntro *in)
{
    intro_shift(eye, target, in, bmap_intro_progress(eye));
}

/* The foe brought in or pushed back by dz: the foe's side (z < 0) is
 * stretched, so its slot (z -10) lands at -10 - dz.  That reaches down to
 * NEAR_DZ (9 apart, the foe's slot at -2); closer still, the stretch stops
 * there and everything in front of the player's Pokemon (z < 7) is drawn in
 * toward it as well, down to 5 apart (the foe in front of the middle).  The
 * two agree at NEAR_DZ, so the distance changes without a jump. */
#define NEAR_DZ -8.0f

void bmap_battle_warp(float p[3], float dy, float dz)
{
    float t, d1 = dz < NEAR_DZ ? NEAR_DZ : dz;

    if (dy == 0.0f && dz == 0.0f) {
        return;
    }
    if (p[2] < 0.0f) {
        t = -p[2] / -FOE_SZ_GAME;       /* 0 at the line between the sides, 1 at the foe */
        p[1] += dy * fminf(t, 1.0f);
        p[2] += -d1 * t;
    }
    if (dz < NEAR_DZ && p[2] < PLACE_SZ) {
        float s = (PLACE_SZ - FOE_SZ_GAME + dz) / (PLACE_SZ - FOE_SZ_GAME + NEAR_DZ);

        p[2] = PLACE_SZ - (PLACE_SZ - p[2]) * s;
    }
}

void bmap_battle_unwarp(float p[3], float dy, float dz)
{
    float t, d1 = dz < NEAR_DZ ? NEAR_DZ : dz, k = 1.0f + d1 / -FOE_SZ_GAME;  /* z' = z * k on the foe's side */

    if (dy == 0.0f && dz == 0.0f) {
        return;
    }
    if (dz < NEAR_DZ && p[2] < PLACE_SZ) {
        float s = (PLACE_SZ - FOE_SZ_GAME + dz) / (PLACE_SZ - FOE_SZ_GAME + NEAR_DZ);

        if (s > 0.0f) {
            p[2] = PLACE_SZ - (PLACE_SZ - p[2]) / s;
        }
    }
    if (p[2] >= 0.0f || k <= 0.0f) {
        return;
    }
    p[2] /= k;
    t = -p[2] / -FOE_SZ_GAME;
    p[1] -= dy * fminf(t, 1.0f);
}

void bmap_place_view_live(BmapScene *s, const BmapIntro *cam, const float trainer_foe[3],
                          const float trainer_own[3])
{
    if (s != NULL && s->view_set) {
        s->cam = *cam;
        memcpy(s->trainer_foe, trainer_foe, sizeof s->trainer_foe);
        memcpy(s->trainer_own, trainer_own, sizeof s->trainer_own);
    }
}

int bmap_place_view_set(const BmapScene *s)
{
    int i;

    if (s == NULL || !s->view_set) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (s->trainer_foe[i] != 0.0f || s->trainer_own[i] != 0.0f) {
            return 1;
        }
    }
    return !bmap_intro_none(&s->cam);
}

void bmap_battle_camera(float eye[3], float target[3], const BmapScene *s)
{
    float w;

    if (s == NULL) {
        return;
    }

    bmap_battle_warp(eye, s->foe_dy, s->foe_dz);
    bmap_battle_warp(target, s->foe_dy, s->foe_dz);
    w = bmap_intro_progress(eye);               /* before the command camera's change */
    if (s->view_set) {
        bmap_camera_view(eye, target, &s->cam, 1.0f - w);   /* the intro's camera is its own */
    }
    intro_shift(eye, target, &s->intro, w);
}

void bmap_place(BmapScene *s, float px, float pz, unsigned facing)
{
    bmap_place_search(s, px, pz, facing, BMAP_PLACE_REACH);
}

void bmap_place_search(BmapScene *s, float px, float pz, unsigned facing, int reach)
{
    BmapPlaceSpec sp;

    memset(&sp, 0, sizeof sp);
    sp.px = px;
    sp.pz = pz;
    sp.facing = facing;
    sp.reach = reach;
    sp.turn = 1;
    sp.foe_level = 1;
    bmap_place_spec(s, &sp);
}

/* The field's cut-out cards: a flat picture on a placed object (a potted
 * plant's leaves, a sign) that the map leans toward the field's camera,
 * which never turns.  From a battle camera beside it the card shows edge on
 * and leaning.  Such a card is stood up (laid out flat as its own picture,
 * its foot where it stood) and crossed the way the grass tufts are: two
 * more copies turned 60 and 120 degrees about the upright through its foot,
 * so it has body from every side.
 *
 * A card: two or more triangles of one placed object that lie in one plane
 * and share no corner with the object's other triangles (a sheet set loose
 * in it; a bevel, a ring's segment are joined to their neighbours), the
 * plane leaning (not flat, not upright already: an upright side stays), at least a quarter tile high and at most three
 * tiles across, the part of the texture they show at least a fifth clear
 * (a pot's sides in the same texture are solid and stay as they are). */
#define CARD_COPIES 3
#define CARD_MAX    48.0f           /* map units                            */
#define CARD_MIN_H  4.0f

typedef struct Card {
    unsigned draw;              /* the draw its triangles are in           */
    unsigned *tri, ntri;        /* its triangles' first vertices           */
    float foot[3], ux, uz;      /* its foot (map units), its across (level) */
} Card;

/* The share of the texels a set of triangles shows that are clear. */
static float clear_share(const BmapScene *s, const BmapTexture *t, const unsigned *tri, unsigned ntri)
{
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
    long clear = 0, all = 0;
    int x, y, x0, x1, y0, y1;
    unsigned i, j;

    if (t->rgba == NULL || t->w <= 0 || t->h <= 0) {
        return 0.0f;
    }
    for (i = 0; i < ntri; i++) {
        for (j = 0; j < 3; j++) {
            const BmapVertex *q = &s->v[tri[i] + j];

            u0 = fminf(u0, q->u);
            u1 = fmaxf(u1, q->u);
            v0 = fminf(v0, q->v);
            v1 = fmaxf(v1, q->v);
        }
    }
    x0 = (int)floorf(u0 * (float)t->w);
    x1 = (int)ceilf(u1 * (float)t->w);
    y0 = (int)floorf(v0 * (float)t->h);
    y1 = (int)ceilf(v1 * (float)t->h);
    if (x1 - x0 > 4 * t->w || y1 - y0 > 4 * t->h) {
        return 0.0f;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            int tx = ((x % t->w) + t->w) % t->w, ty = ((y % t->h) + t->h) % t->h;

            clear += t->rgba[((size_t)ty * (size_t)t->w + (size_t)tx) * 4u + 3u] < 128;
            all++;
        }
    }
    return all > 0 ? (float)clear / (float)all : 0.0f;
}

/* A triangle's plane: unit normal (its face toward the field's camera,
 * south) and distance; 0 when it has no area. */
static int tri_plane(const BmapScene *s, unsigned t, float n[3], float *dist)
{
    const BmapVertex *v = &s->v[t];
    float a[3] = { v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z };
    float b[3] = { v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z };
    float len;

    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-4f) {
        return 0;
    }
    n[0] /= len;
    n[1] /= len;
    n[2] /= len;
    if (n[2] < 0.0f || (n[2] == 0.0f && n[0] < 0.0f)) {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
    }
    *dist = n[0] * v[0].x + n[1] * v[0].y + n[2] * v[0].z;
    return 1;
}

/* Stand up one card's triangles in their plane n; fills its foot and
 * across.  0 when they are not a card after all. */
static int stand_card(BmapScene *s, const BmapTexture *t, Card *c, const float n[3])
{
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f }, m[3] = { 0.0f, 0.0f, 0.0f };
    float u[3], up[3], len, t0 = 1e9f;
    unsigned i, j, nv = c->ntri * 3;
    int k;

    if (fabsf(n[1]) > 0.94f || fabsf(n[1]) < 0.1f) {
        return 0;                       /* lying flat (a rug), or already upright (a side) */
    }
    for (i = 0; i < c->ntri; i++) {
        for (j = 0; j < 3; j++) {
            const BmapVertex *q = &s->v[c->tri[i] + j];
            float p[3] = { q->x, q->y, q->z };

            for (k = 0; k < 3; k++) {
                lo[k] = fminf(lo[k], p[k]);
                hi[k] = fmaxf(hi[k], p[k]);
                m[k] += p[k] / (float)nv;
            }
        }
    }
    if (hi[0] - lo[0] > CARD_MAX || hi[1] - lo[1] > CARD_MAX || hi[2] - lo[2] > CARD_MAX
        || hi[1] - lo[1] < CARD_MIN_H || clear_share(s, t, c->tri, c->ntri) < 0.2f) {
        return 0;
    }
    /* its own frame: u across (level), up along it */
    len = sqrtf(n[2] * n[2] + n[0] * n[0]);
    u[0] = n[2] / len;
    u[1] = 0.0f;
    u[2] = -n[0] / len;
    up[0] = n[1] * u[2] - n[2] * u[1];
    up[1] = n[2] * u[0] - n[0] * u[2];
    up[2] = n[0] * u[1] - n[1] * u[0];
    if (up[1] < 0.0f) {
        for (k = 0; k < 3; k++) {
            up[k] = -up[k];
        }
    }
    /* the foot: the middle, taken down the card to its lowest edge */
    for (i = 0; i < c->ntri; i++) {
        for (j = 0; j < 3; j++) {
            const BmapVertex *q = &s->v[c->tri[i] + j];

            t0 = fminf(t0, (q->x - m[0]) * up[0] + (q->y - m[1]) * up[1] + (q->z - m[2]) * up[2]);
        }
    }
    for (k = 0; k < 3; k++) {
        c->foot[k] = m[k] + up[k] * t0;
    }
    for (i = 0; i < c->ntri; i++) {
        for (j = 0; j < 3; j++) {
            BmapVertex *q = &s->v[c->tri[i] + j];
            float o[3] = { q->x - c->foot[0], q->y - c->foot[1], q->z - c->foot[2] };
            float ox = o[0] * u[0] + o[2] * u[2];
            float oy = o[0] * up[0] + o[1] * up[1] + o[2] * up[2];

            /* the picture kept as it was (its own across, its own height),
             * stood up straight where it was */
            q->x = c->foot[0] + ox * u[0];
            q->y = c->foot[1] + oy;
            q->z = c->foot[2] + ox * u[2];
        }
    }
    c->ux = u[0];
    c->uz = u[2];
    return 1;
}

/* No corner of the card's triangles is a corner of another triangle of its
 * object, in any draw (the map's triangles are whole, three vertices each
 * from the start of the vertex array). */
static int loose(const BmapScene *s, const Card *c, unsigned first, unsigned end)
{
    unsigned obj = s->v[c->tri[0]].obj, i, j, k;

    (void)first;
    (void)end;
    for (k = 0; k + 2 < s->nv; k += 3) {
        int mine = 0;
        unsigned q;

        if (s->v[k].obj != obj) {
            continue;
        }
        for (i = 0; i < c->ntri && !mine; i++) {
            mine = c->tri[i] == k;
        }
        if (mine) {
            continue;
        }
        for (q = 0; q < 3; q++) {
            const BmapVertex *o = &s->v[k + q];

            for (i = 0; i < c->ntri; i++) {
                for (j = 0; j < 3; j++) {
                    const BmapVertex *m = &s->v[c->tri[i] + j];

                    if (fabsf(m->x - o->x) < 0.05f && fabsf(m->y - o->y) < 0.05f && fabsf(m->z - o->z) < 0.05f) {
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

static void stand_cards(BmapScene *s)
{
    unsigned i, j, nd0 = s->nd, ncard = 0, cap = 0, nvx = 0, nt_all;
    Card *card = NULL;
    unsigned *pool;
    unsigned char *used;

    /* room for every triangle of the textured draws once */
    nt_all = s->nv / 3 + 1;
    pool = (unsigned *)malloc(nt_all * sizeof *pool);
    used = (unsigned char *)calloc(nt_all, 1);
    if (pool == NULL || used == NULL) {
        free(pool);
        free(used);
        return;
    }
    for (i = 0; i < nd0; i++) {
        BmapDraw *d = &s->d[i];
        const BmapTexture *t;
        unsigned first;

        if (d->billboard != 0 || d->count < 3 || d->first + d->count > s->nv || d->tex < 0 || (unsigned)d->tex >= s->nt
            || d->uvanim != 0) {
            continue;                   /* (an animated sheet: stand_drift) */
        }
        t = &s->t[d->tex];
        /* the draw holds every object with this material: one object a run
         * of triangles; in it, the triangles in one plane */
        for (first = d->first; first + 3 <= d->first + d->count;) {
            unsigned end = first + 3, a;

            while (end + 3 <= d->first + d->count && s->v[end].obj == s->v[first].obj) {
                end += 3;
            }
            for (a = first; s->v[first].obj != 0 && a < end; a += 3) {
                float n[3], dist, n2[3], d2;
                unsigned b, base = 0;
                Card c;

                if (used[a / 3] || !tri_plane(s, a, n, &dist)) {
                    continue;
                }
                c.tri = &pool[0];
                c.ntri = 0;
                /* this triangle's plane mates, gathered at the pool's end */
                for (b = 0; b < ncard; b++) {
                    base += card[b].ntri;
                }
                c.tri = &pool[base];
                for (b = a; b < end; b += 3) {
                    if (!used[b / 3] && tri_plane(s, b, n2, &d2) && n2[0] * n[0] + n2[1] * n[1] + n2[2] * n[2] > 0.995f
                        && fabsf(d2 - dist) < 0.5f && base + c.ntri < nt_all) {
                        c.tri[c.ntri++] = b;
                    }
                }
                if (c.ntri < 2 || !loose(s, &c, first, end)) {
                    used[a / 3] = 1;
                    continue;
                }
                if (ncard == cap) {
                    unsigned ncap = cap ? cap * 2 : 16;
                    Card *nc = (Card *)realloc(card, ncap * sizeof *card);

                    if (nc == NULL) {
                        break;
                    }
                    card = nc;
                    cap = ncap;
                }
                if (stand_card(s, t, &c, n)) {
                    for (b = 0; b < c.ntri; b++) {
                        used[c.tri[b] / 3] = 1;
                    }
                    c.draw = i;
                    d->cull = 0;
                    nvx += c.ntri * 3 * (CARD_COPIES - 1);
                    card[ncard++] = c;
                } else {
                    used[a / 3] = 1;    /* not a card: never looked at again */
                }
            }
            first = end;
        }
    }
    if (ncard > 0) {
        /* the crossed copies */
        BmapVertex *v = (BmapVertex *)realloc(s->v, (s->nv + nvx) * sizeof *v);
        BmapDraw *dd = v != NULL ? (BmapDraw *)realloc(s->d, (s->nd + ncard * (CARD_COPIES - 1)) * sizeof *dd) : NULL;

        if (v != NULL) {
            s->v = v;
        }
        if (dd != NULL) {
            s->d = dd;
            for (i = 0; i < ncard; i++) {
                const Card *c = &card[i];
                unsigned k, b;

                for (k = 1; k < CARD_COPIES; k++) {
                    BmapDraw *cp = &s->d[s->nd];
                    float ang = (float)k * 3.14159265f / (float)CARD_COPIES;
                    float ca = cosf(ang), sa = sinf(ang);
                    /* the card's across turned about the upright */
                    float ux = c->ux * ca - c->uz * sa, uz = c->ux * sa + c->uz * ca;

                    *cp = s->d[c->draw];
                    cp->first = s->nv;
                    cp->count = c->ntri * 3;
                    for (b = 0; b < c->ntri; b++) {
                        for (j = 0; j < 3; j++) {
                            BmapVertex q = s->v[c->tri[b] + j];
                            float ox = (q.x - c->foot[0]) * c->ux + (q.z - c->foot[2]) * c->uz;

                            q.x = c->foot[0] + ox * ux;
                            q.z = c->foot[2] + ox * uz;
                            s->v[s->nv++] = q;
                        }
                    }
                    s->nd++;
                }
            }
        }
    }
    free(card);
    free(pool);
    free(used);
}

/* The field's floor pictures: a gym's emblem, a button's icon, a flower,
 * a flat sheet the map lays a few units over the surface under it.  The
 * field's camera looks almost straight down and never sees the gap; a
 * battle camera low over the floor sees them float.  Each is laid on the
 * surface under it (a hair above, so it still draws over it).
 *
 * A floor picture: a level triangle, textured and solid (a see-through
 * sheet, water over its bed, a shadow, keeps its height), none of its corners a
 * corner of a triangle that is not level (a platform's top is joined to its
 * sides and stays), with a level surface under its middle at most
 * DECAL_MAX_H below it (the highest such: an icon lies on its button's top,
 * not on the floor under the button). */
#define DECAL_MAX_H 6.0f            /* map units                            */
#define DECAL_LIFT  0.3f
#define DECAL_CELL  32.0f

static int level_tri(const BmapVertex *v)
{
    float a = (v[1].x - v[0].x) * (v[2].z - v[0].z) - (v[1].z - v[0].z) * (v[2].x - v[0].x);

    return fabsf(v[1].y - v[0].y) < 0.01f && fabsf(v[2].y - v[0].y) < 0.01f && fabsf(a) > 1e-3f;
}

static int key_cmp(const void *a, const void *b)
{
    const long long *x = (const long long *)a, *y = (const long long *)b;

    return *x < *y ? -1 : *x > *y;
}

static long long pos_key(const BmapVertex *v)
{
    long long qx = (long long)lroundf(v->x * 4.0f) & 0xFFFFF, qy = (long long)lroundf(v->y * 4.0f) & 0xFFFFF;
    long long qz = (long long)lroundf(v->z * 4.0f) & 0xFFFFF;

    return (qx << 40) | (qy << 20) | qz;
}

static int in_tri_xz(const BmapVertex *v, float x, float z)
{
    float d1 = (x - v[1].x) * (v[0].z - v[1].z) - (v[0].x - v[1].x) * (z - v[1].z);
    float d2 = (x - v[2].x) * (v[1].z - v[2].z) - (v[1].x - v[2].x) * (z - v[2].z);
    float d3 = (x - v[0].x) * (v[2].z - v[0].z) - (v[2].x - v[0].x) * (z - v[0].z);
    int neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f, pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;

    return !(neg && pos);
}

static const BmapVertex *g_sort_v;

static int tri_y_cmp(const void *a, const void *b)
{
    float ya = g_sort_v[*(const unsigned *)a * 3].y, yb = g_sort_v[*(const unsigned *)b * 3].y;

    return ya < yb ? -1 : ya > yb;
}

/*
 * Falling petals and leaves (Route 1's, a placed model's BTA0): the field
 * draws them as level sheets stacked in the air, a petal picture sliding
 * across each, seen from above.  From the battle's low camera a level sheet
 * is a line.  Each sheet (a run of level triangles of one object at one
 * height) is stood up after the placement, turned about the battle's x axis
 * so it faces +z, its lower edge where it lay: the way its picture slides
 * toward +z becomes downward, so the petals fall.  Each sheet is then a draw
 * of its own that turns about y to the camera every frame (billboard 2,
 * pivot at its centre), so no camera sees it edge on.  In battle space.
 */
static void stand_drift(BmapScene *s)
{
    unsigned i, nd0 = s->nd, nsheet = 0, cap = 0;
    struct { unsigned draw, first, count; float pivot[3]; } *sheet = NULL;

    for (i = 0; i < nd0; i++) {
        BmapDraw *d = &s->d[i];
        const BmapUvAnim *ua;
        float du, dv;
        unsigned a, e;

        if (d->uvanim == 0 || d->uvanim > s->nuva || d->billboard || d->first % 3 || d->first + d->count > s->nv) {
            continue;
        }
        ua = &s->uva[d->uvanim - 1];
        if (ua->frames < 2) {
            continue;
        }
        {
            /* how far the picture slides a frame (over the first few) */
            unsigned f1 = ua->frames > 8 ? 8 : ua->frames - 1;

            du = (ua->m[f1 * 6 + 2] - ua->m[2]) / (float)f1;
            dv = (ua->m[f1 * 6 + 5] - ua->m[5]) / (float)f1;
        }
        for (a = d->first; a + 3 <= d->first + d->count; a = e) {
            float cy = s->v[a].y, cz = 0.0f, mz = 0.0f, lo = 1e30f, sgn;
            unsigned j, n = 0;

            /* the sheet: the level triangles of this object at this height */
            for (e = a; e + 3 <= d->first + d->count; e += 3) {
                const BmapVertex *p = &s->v[e];

                if (p[0].obj != s->v[a].obj || fabsf(p[0].y - cy) > 0.2f || fabsf(p[1].y - cy) > 0.2f ||
                    fabsf(p[2].y - cy) > 0.2f) {
                    break;
                }
                {
                    /* the world motion of the picture: minus the slide, in
                     * the triangle's own uv frame */
                    float e1x = p[1].x - p[0].x, e1z = p[1].z - p[0].z, e2x = p[2].x - p[0].x, e2z = p[2].z - p[0].z;
                    float u1 = p[1].u - p[0].u, v1 = p[1].v - p[0].v, u2 = p[2].u - p[0].u, v2 = p[2].v - p[0].v;
                    float det = u1 * v2 - u2 * v1;

                    if (fabsf(det) > 1e-8f) {
                        float dzdu = (e1z * v2 - e2z * v1) / det, dzdv = (e2z * u1 - e1z * u2) / det;

                        mz -= du * dzdu + dv * dzdv;
                    }
                    (void)e1x;
                    (void)e2x;
                }
                for (j = 0; j < 3; j++) {
                    cz += p[j].z;
                }
                n += 3;
            }
            if (e == a) {
                e = a + 3;              /* not level: left as it is */
                continue;
            }
            cz /= (float)n;
            sgn = mz >= 0.0f ? 1.0f : -1.0f;    /* toward the camera: downward */
            for (j = a; j < e; j++) {
                float y = cy - sgn * (s->v[j].z - cz);

                lo = fminf(lo, y);
            }
            {
                float x0 = 1e30f, x1 = -1e30f, y1 = -1e30f;

                for (j = a; j < e; j++) {
                    BmapVertex *v = &s->v[j];

                    v->y = cy + (cy - sgn * (v->z - cz)) - lo;
                    v->z = cz;
                    x0 = fminf(x0, v->x);
                    x1 = fmaxf(x1, v->x);
                    y1 = fmaxf(y1, v->y);
                }
                if (nsheet == cap) {
                    unsigned ncap = cap ? cap * 2 : 16;
                    void *ns = realloc(sheet, ncap * sizeof *sheet);

                    if (ns == NULL) {
                        continue;
                    }
                    sheet = ns;
                    cap = ncap;
                }
                sheet[nsheet].draw = i;
                sheet[nsheet].first = a;
                sheet[nsheet].count = e - a;
                sheet[nsheet].pivot[0] = 0.5f * (x0 + x1);
                sheet[nsheet].pivot[1] = 0.5f * (cy + y1);
                sheet[nsheet].pivot[2] = cz;
                nsheet++;
            }
        }
    }
    if (nsheet > 0) {
        /* the sheets' draws (turned to the camera), and the rest of each
         * draw they came from as draws of its own (the old draw is emptied) */
        BmapDraw *nd = (BmapDraw *)realloc(s->d, (s->nd + 2 * nsheet + nd0) * sizeof *nd);

        if (nd != NULL) {
            unsigned k = 0;

            s->d = nd;
            while (k < nsheet) {
                unsigned di = sheet[k].draw, at = s->d[di].first, end = s->d[di].first + s->d[di].count;
                BmapDraw o = s->d[di];

                for (; k < nsheet && sheet[k].draw == di; k++) {
                    BmapDraw *n;

                    if (sheet[k].first > at) {
                        n = &s->d[s->nd++];
                        *n = o;
                        n->first = at;
                        n->count = sheet[k].first - at;
                    }
                    n = &s->d[s->nd++];
                    *n = o;
                    n->first = sheet[k].first;
                    n->count = sheet[k].count;
                    n->billboard = 2;
                    memcpy(n->pivot, sheet[k].pivot, sizeof n->pivot);
                    at = sheet[k].first + sheet[k].count;
                }
                if (end > at) {
                    BmapDraw *n = &s->d[s->nd++];

                    *n = o;
                    n->first = at;
                    n->count = end - at;
                }
                s->d[di].count = 0;
            }
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        for (i = 0; i < nsheet; i++) {
            fprintf(stderr, "bmap: drift sheet %u draw %u tris %u at %.1f %.1f %.1f\n", i, sheet[i].draw, sheet[i].count / 3, sheet[i].pivot[0], sheet[i].pivot[1], sheet[i].pivot[2]);
        }
    }
    free(sheet);
}

static void lay_decals(BmapScene *s, const unsigned char *grass, unsigned ngrass)
{
    unsigned ntri = s->nv / 3, t, nk = 0, i, nlaid = 0, norder = 0, *order = NULL;
    long long *keys = (long long *)malloc((s->nv > 0 ? s->nv : 1) * sizeof *keys);
    unsigned char *level = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
    unsigned char *textured = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    int gw, gh;
    unsigned *start = NULL, *idx = NULL, *fill = NULL;

    if (keys == NULL || level == NULL || textured == NULL) {
        goto out;
    }
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard == 0 && d->tex >= 0 && !d->translucent && d->uvanim == 0 && d->first % 3 == 0 && d->first + d->count <= s->nv
            && !(grass != NULL && i < ngrass && grass[i])) {
            for (t = d->first / 3; t < (d->first + d->count) / 3; t++) {
                textured[t] = 1;
            }
        }
    }
    for (t = 0; t < ntri; t++) {
        const BmapVertex *v = &s->v[t * 3];
        int k;

        level[t] = (unsigned char)level_tri(v);
        if (!level[t]) {
            for (k = 0; k < 3; k++) {
                keys[nk++] = pos_key(&v[k]);    /* a corner of a wall or a slope */
            }
        } else {
            for (k = 0; k < 3; k++) {
                x0 = fminf(x0, v[k].x);
                x1 = fmaxf(x1, v[k].x);
                z0 = fminf(z0, v[k].z);
                z1 = fmaxf(z1, v[k].z);
            }
        }
    }
    if (x1 < x0) {
        goto out;
    }
    qsort(keys, nk, sizeof *keys, key_cmp);
    /* the level triangles binned by the cells they cover */
    gw = (int)((x1 - x0) / DECAL_CELL) + 1;
    gh = (int)((z1 - z0) / DECAL_CELL) + 1;
    start = (unsigned *)calloc((size_t)gw * gh + 1, sizeof *start);
    fill = (unsigned *)calloc((size_t)gw * gh + 1, sizeof *fill);
    if (start == NULL || fill == NULL) {
        goto out;
    }
    {
        int pass;

        for (pass = 0; pass < 2; pass++) {
            for (t = 0; t < ntri; t++) {
                const BmapVertex *v = &s->v[t * 3];
                int cx0, cx1, cz0, cz1, cx, cz;

                if (!level[t]) {
                    continue;
                }
                cx0 = (int)((fminf(v[0].x, fminf(v[1].x, v[2].x)) - x0) / DECAL_CELL);
                cx1 = (int)((fmaxf(v[0].x, fmaxf(v[1].x, v[2].x)) - x0) / DECAL_CELL);
                cz0 = (int)((fminf(v[0].z, fminf(v[1].z, v[2].z)) - z0) / DECAL_CELL);
                cz1 = (int)((fmaxf(v[0].z, fmaxf(v[1].z, v[2].z)) - z0) / DECAL_CELL);
                for (cz = cz0; cz <= cz1; cz++) {
                    for (cx = cx0; cx <= cx1; cx++) {
                        unsigned c = (unsigned)(cz * gw + cx);

                        if (pass == 0) {
                            start[c + 1]++;
                        } else {
                            idx[start[c] + fill[c]++] = t;
                        }
                    }
                }
            }
            if (pass == 0) {
                unsigned c;

                for (c = 0; c < (unsigned)(gw * gh); c++) {
                    start[c + 1] += start[c];
                }
                idx = (unsigned *)malloc((start[gw * gh] > 0 ? start[gw * gh] : 1) * sizeof *idx);
                if (idx == NULL) {
                    goto out;
                }
            }
        }
    }
    /* lowest first: a picture laid on another picture finds it laid already */
    order = (unsigned *)malloc((ntri > 0 ? ntri : 1) * sizeof *order);
    if (order == NULL) {
        goto out;
    }
    for (t = 0; t < ntri; t++) {
        if (level[t] && textured[t]) {
            order[norder++] = t;
        }
    }
    g_sort_v = s->v;
    qsort(order, norder, sizeof *order, tri_y_cmp);
    for (i = 0; i < norder; i++) {
        BmapVertex *v;
        float cx, cz, y, best = -1e30f;
        unsigned c, j;
        int k, joined = 0;

        t = order[i];
        v = &s->v[t * 3];
        if (bmap_tile_encounter(s, (v[0].x + v[1].x + v[2].x) / 3.0f, (v[0].z + v[1].z + v[2].z) / 3.0f)) {
            continue;                   /* the grass's stacked layers: their height is their volume */
        }
        for (k = 0; k < 3 && !joined; k++) {
            long long key = pos_key(&v[k]);

            joined = bsearch(&key, keys, nk, sizeof *keys, key_cmp) != NULL;
        }
        if (joined) {
            continue;
        }
        cx = (v[0].x + v[1].x + v[2].x) / 3.0f;
        cz = (v[0].z + v[1].z + v[2].z) / 3.0f;
        y = v[0].y;
        c = (unsigned)((int)((cz - z0) / DECAL_CELL) * gw + (int)((cx - x0) / DECAL_CELL));
        for (j = start[c]; j < start[c + 1]; j++) {
            const BmapVertex *u = &s->v[idx[j] * 3];

            if (idx[j] != t && u[0].y < y - 0.05f && u[0].y >= y - DECAL_MAX_H && u[0].y > best && in_tri_xz(u, cx, cz)) {
                best = u[0].y;
            }
        }
        if (best > -1e29f) {
            for (k = 0; k < 3; k++) {
                v[k].y = best + DECAL_LIFT;
            }
            nlaid++;
        }
    }
    (void)nlaid;
out:
    free(keys);
    free(level);
    free(textured);
    free(start);
    free(fill);
    free(idx);
    free(order);
}

int bmap_wild_height(const BmapScene *s, float x0, float z0, float x1, float z1, float *y)
{
    float best = 1e30f;
    unsigned i, j;

    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];
        const BmapVertex *v = &s->v[d->first];

        if (d->count < 3 || d->billboard || d->first + d->count > s->nv || !leafy(s, d)) {
            continue;
        }
        for (j = 0; j + 2 < d->count; j += 3) {
            float cx = (v[j].x + v[j + 1].x + v[j + 2].x) / 3.0f, cz = (v[j].z + v[j + 1].z + v[j + 2].z) / 3.0f;

            if (cx < x0 || cx > x1 || cz < z0 || cz > z1 || fabsf(v[j].y - v[j + 1].y) > 0.01f
                || fabsf(v[j].y - v[j + 2].y) > 0.01f || !bmap_tile_encounter(s, cx, cz)) {
                continue;
            }
            best = fminf(best, v[j].y);
        }
    }
    if (best > 1e29f) {
        return 0;
    }
    *y = best;
    return 1;
}

/* A cave mouth's light: a white cut-out sheet of the map's own ground,
 * standing up at the exit (the dust lit by the daylight outside).  Seen from
 * the battle's low camera it is a white blot in the air, so the battle
 * leaves it out (the field view keeps it).  Told by its picture: nearly all
 * white, solid where drawn, a sixth to three quarters clear (snow is solid
 * through, a translucent mist sheet is blended), and by not lying flat. */
static void drop_cave_light(BmapScene *s)
{
    unsigned i, j;

    for (i = 0; i < s->nd; i++) {
        BmapDraw *d = &s->d[i];
        const BmapTexture *t = d->tex >= 0 && (unsigned)d->tex < s->nt ? &s->t[d->tex] : NULL;
        unsigned long r = 0, g = 0, b = 0, n = 0, clear = 0, all, part = 0;
        int level = 1;

        if (t == NULL || t->rgba == NULL || d->translucent || d->billboard || d->count < 3
            || d->first + d->count > s->nv || s->v[d->first].obj != 0) {
            continue;
        }
        all = (unsigned long)t->w * (unsigned long)t->h;
        for (j = 0; j < all; j++) {
            unsigned a = t->rgba[j * 4 + 3];

            if (a == 0) {
                clear++;
            } else if (a < 255) {
                part++;
            } else {
                r += t->rgba[j * 4];
                g += t->rgba[j * 4 + 1];
                b += t->rgba[j * 4 + 2];
                n++;
            }
        }
        if (n == 0 || part != 0 || r < n * 225 || g < n * 225 || b < n * 225 || clear * 6 < all || clear * 4 > all * 3) {
            continue;
        }
        for (j = d->first; j + 2 < d->first + d->count && level; j += 3) {
            level = fabsf(s->v[j + 1].y - s->v[j].y) < 0.01f && fabsf(s->v[j + 2].y - s->v[j].y) < 0.01f;
        }
        if (!level) {
            d->count = 0;
        }
    }
}

void bmap_place_spec(BmapScene *s, const BmapPlaceSpec *sp)
{
    const double pi = 3.14159265358979323846;
    double a = (double)(sp->facing & 0xFFFFu) * 2.0 * pi / 65536.0;
    float px = sp->px, pz = sp->pz;
    int reach = sp->reach, q;

    g_foe_z = FOE_SZ_GAME - sp->foe_dz;
    for (q = 3; q < N_SIGHT; q++) {
        k_sight[q][2] = g_foe_z;
    }
    /* the intro camera as this place turns, raises and zooms it */
    memcpy(k_eyes[1][0], k_intro_eye, sizeof k_intro_eye);
    memcpy(k_eyes[1][1], k_intro_target, sizeof k_intro_target);
    memcpy(k_eyes[0][0], k_cmd_eye, sizeof k_cmd_eye);
    memcpy(k_eyes[0][1], k_cmd_target, sizeof k_cmd_target);
    bmap_camera_view(k_eyes[0][0], k_eyes[0][1], &sp->cam, 1.0f);
    bmap_camera_intro(k_eyes[1][0], k_eyes[1][1], &sp->intro);
    s->view_set = !sp->field;
    if (s->view_set) {
        s->cam = sp->cam;
        memcpy(s->trainer_foe, sp->trainer_foe, sizeof s->trainer_foe);
        memcpy(s->trainer_own, sp->trainer_own, sizeof s->trainer_own);
    }
    s->intro = sp->intro;
    if (s->intro.zoom <= 0.0f) {
        s->intro.zoom = 1.0f;
    }
    float ca, sa;
    float gy, k = BMAP_PLACE_K, th[6];
    unsigned i, nd0;
    unsigned char *done, *grass;

    if (s == NULL) {
        return;
    }
    if (getenv("EIDOS_DEBUG_BMAP_K") != NULL) {     /* tests: another scale */
        k = (float)atof(getenv("EIDOS_DEBUG_BMAP_K"));
    }
    /* the same map for the battle and the editor's free field view */
    stand_cards(s);
    if (!sp->field) {
        drop_cave_light(s);
    }
    nd0 = s->nd;
    grass = (unsigned char *)calloc(nd0 > 0 ? nd0 : 1, 1);
    /* The grass: each level triangle of a cut-out sheet a little above the
     * solid ground right under it, leafy or over the tiles where wild
     * Pokemon appear (dark grass is not green enough to tell by its colour;
     * a rug never lies on those tiles; the soft shadow sheet under the
     * leaves is translucent).  Triangle by triangle: one draw holds the
     * grass of every height (a hill's terraces).  Laid on the ground, and
     * the tufts grow from them (g_tmask). */
    g_tmask = (unsigned char *)calloc(s->nv / 3 + 1, 1);
    for (i = 0; g_tmask != NULL && !sp->field && i < s->nd; i++) {
        BmapDraw *d = &s->d[i];
        const BmapTexture *t = d->tex >= 0 && (unsigned)d->tex < s->nt ? &s->t[d->tex] : NULL;
        unsigned j;
        int cut = d->translucent != 0, lf;

        if (d->billboard != 0 || d->count < 3 || d->first % 3 != 0 || d->first + d->count > s->nv || t == NULL
            || s->v[d->first].obj != 0) {
            continue;                   /* a placed object (a gym button's leaf) stays */
        }
        for (j = 0; !cut && j < (unsigned)(t->w * t->h); j++) {
            cut = t->rgba[j * 4 + 3] == 0;
        }
        if (!cut) {
            continue;
        }
        lf = leafy(s, d);
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            BmapVertex *v = &s->v[j];
            float y0 = v[0].y, cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f, g, dh;
            int hit, k2;

            if (fabsf(v[1].y - y0) > 0.01f || fabsf(v[2].y - y0) > 0.01f || v[0].obj != 0) {
                continue;
            }
            g = bmap_ground_below(s, cx, cz, y0 - 0.01f, &hit);
            if (!hit) {
                continue;
            }
            dh = y0 - g;
            if (dh <= 0.0f || dh > GRASS_MAX_H) {
                continue;
            }
            if (!lf && (d->translucent || !bmap_tile_encounter(s, cx, cz))) {
                continue;
            }
            g_tmask[j / 3] = 1;
            if (grass != NULL) {
                grass[i] = 1;
            }
            for (k2 = 0; k2 < 3; k2++) {
                v[k2].y = g + dh * 0.05f;
            }
        }
    }
    /* after the grass: a grass sheet is told by lying flat as a whole, and
     * is laid down (and grown into tufts) by its own rule */
    lay_decals(s, grass, nd0);
    if (sp->has_rect) {                 /* the start inside the rectangle */
        px = fminf(fmaxf(px, sp->x0), sp->x1);
        pz = fminf(fmaxf(pz, sp->z0), sp->z1);
    }
    /* Where the battle stands: every tile within `reach` of the start, the
     * facing and up to `turn` 45-degree steps either side of it (the foe is
     * where the player looks, never beside or behind them).  The world
     * keeps one scale everywhere: a room is not shown larger, so the floor
     * around the battle stays in view.  The lowest place_score wins;
     * distance and turning each cost a little, so the start wins whenever
     * it is as good.  With reach < 0 the start and facing are used as
     * given. */
    {
        clock_t t_start = clock();
        float best = 1e30f, bx = px, bz = pz, gp = bmap_ground_y(s, px, pz);
        float best_in = 1e30f, ibx = px, ibz = pz;
        double ba = a, iba = a;
        int dx, dz, n, home = bmap_tile(s, px, pz);

        if (reach < 0) {
            best = place_score(s, grass, px, pz, a, k, gp, home);
        }
        for (dz = -reach; dz <= reach; dz++) {
            for (dx = -reach; dx <= reach; dx++) {
                float dist = sqrtf((float)(dx * dx + dz * dz));

                if (dist > (float)reach + 0.5f) {
                    continue;
                }
                for (n = 0; n <= 2 * sp->turn && n < 8; n++) {
                    double an = a + (double)((n + 1) / 2) * ((n & 1) ? 1.0 : -1.0) * pi / 4.0;
                    float cx = px + (float)dx * 16.0f, cz = pz + (float)dz * 16.0f;
                    float sc;

                    if (bmap_tile(s, cx, cz) == BMAP_TILE_BLOCKED) {
                        continue;           /* never on a wall or a table */
                    }
                    if (sp->has_rect && (cx < sp->x0 || cx > sp->x1 || cz < sp->z0 || cz > sp->z1)) {
                        continue;
                    }
                    sc = place_score(s, grass, cx, cz, an, k, gp, home)
                         + 0.8f * dist + 0.6f * (float)((n + 1) / 2);
                    if (sc < best) {
                        best = sc;
                        bx = cx;
                        bz = cz;
                        ba = an;
                    }

                }
            }
        }
        if (sp->has_area) {
            /* A wild area: both Pokemon in it, wherever in it (any spot of
             * the area and a few tiles round, any of the eight angles); the
             * most of the battle in it first (a patch too small for both
             * keeps one), then the usual score.  In it: inside its
             * rectangle, on its wild tiles (water: the rectangle alone). */
            int wild = 0, best_n = -1, step;
            float x, z;

            for (z = sp->area[1] + 8.0f; z < sp->area[3] && !wild; z += 16.0f) {
                for (x = sp->area[0] + 8.0f; x < sp->area[2] && !wild; x += 16.0f) {
                    wild = bmap_tile_encounter(s, x, z) != 0;
                }
            }
            step = (sp->area[2] - sp->area[0]) * (sp->area[3] - sp->area[1]) > 400.0f * 256.0f ? 32 : 16;
            for (z = sp->area[1] - 96.0f + 8.0f; z < sp->area[3] + 96.0f; z += (float)step) {
                for (x = sp->area[0] - 96.0f + 8.0f; x < sp->area[2] + 96.0f; x += (float)step) {
                    int n2;

                    if (bmap_tile(s, x, z) == BMAP_TILE_BLOCKED) {
                        continue;
                    }
                    for (n2 = 0; n2 < 8; n2++) {
                        double an = (double)n2 * pi / 4.0;
                        float cca = (float)cos(an), csa = (float)sin(an), ox, oz, fx, fz, sc;
                        int in = 0;

                        to_map(0.5f, PLACE_SZ, x, z, cca, csa, k, &ox, &oz);
                        to_map(FOE_SX, FOE_SZ, x, z, cca, csa, k, &fx, &fz);
                        in += ox >= sp->area[0] && ox <= sp->area[2] && oz >= sp->area[1] && oz <= sp->area[3]
                              && (!wild || bmap_tile_encounter(s, ox, oz));
                        in += fx >= sp->area[0] && fx <= sp->area[2] && fz >= sp->area[1] && fz <= sp->area[3]
                              && (!wild || bmap_tile_encounter(s, fx, fz));
                        if (in < best_n) {
                            continue;
                        }
                        sc = place_score(s, grass, x, z, an, k, gp, home);
                        if (in > best_n || sc < best_in) {
                            best_n = in;
                            best_in = sc;
                            ibx = x;
                            ibz = z;
                            iba = an;
                        }
                    }
                }
            }
            if (best_n > 0) {
                bx = ibx;
                bz = ibz;
                ba = iba;
            }
        }
        s->place_k = k;
        th[0] = px; th[1] = pz; th[2] = bx; th[3] = bz; th[4] = (float)ba; th[5] = k;
        topview_dump(s, nd0, 0, th);
        px = bx;
        pz = bz;
        a = ba;
        if (getenv("BMAP_PLACE_LOG") != NULL && reach < 0) {
            fprintf(stderr, "bmap: parts step %.1f slots %.1f behind %.1f centre %.1f field %.1f camera %.1f objects %.1f\n",
                    g_part[0], g_part[1] - g_part[0], g_part[2] - g_part[1], g_part[3] - g_part[2],
                    g_part[4] - g_part[3], g_part[5] - g_part[4], g_part[6]);
        }
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: placed at %.0f %.0f angle %.0f scale %.2f score %.1f (%.0f ms)\n", px, pz,
                    a * 180.0 / pi, k, best, 1000.0 * (double)(clock() - t_start) / CLOCKS_PER_SEC);
        }
    }
    ca = (float)cos(a);
    sa = (float)sin(a);
    gy = bmap_ground_y(s, px, pz);
    {
        /* the map point under the foe slot: invert the rotation */
        float bx = (FOE_SX - PLACE_SX) / k, bz = (FOE_SZ - PLACE_SZ) / k;
        float fx = px + ca * bx + sa * bz, fz = pz - sa * bx + ca * bz;

        s->place_x = px;
        s->place_z = pz;
        s->place_a = (float)a;
        s->place_g[0] = gy;
        s->place_g[1] = bmap_ground_y(s, fx, fz);
        s->foe_dz = sp->foe_dz;
        /* each Pokemon on its own ground: the own one where the map is
         * placed, the foe raised or lowered onto its own (battle_view.c
         * moves the foe's slot by foe_dy, and the cameras follow it,
         * bmap_battle_warp).  (A levelled ground left both half a step off
         * on uneven ground, the foe in it or above it.) */
        s->foe_dy = k * (s->place_g[1] - gy);
    }
    /* Billboard draws keep their +z-facing shape: the pivot is placed, the
     * offsets from it are only scaled, and the GL pass turns them toward the
     * camera every frame. */
    done = (unsigned char *)calloc(s->nv > 0 ? s->nv : 1, 1);
    for (i = 0; done != NULL && i < s->nd; i++) {
        BmapDraw *d = &s->d[i];
        float pv[3], ux, uz;
        unsigned j;

        if (d->billboard == 0 || d->first + d->count > s->nv) {
            continue;
        }
        ux = d->pivot[0] - px;
        uz = d->pivot[2] - pz;
        pv[0] = k * (ca * ux - sa * uz) + PLACE_SX;
        pv[2] = k * (sa * ux + ca * uz) + PLACE_SZ;
        pv[1] = GROUND_Y + k * (d->pivot[1] - gy) - GROUND_LIFT;
        for (j = d->first; j < d->first + d->count; j++) {
            BmapVertex *v = &s->v[j];

            if (done[j]) {
                continue;
            }
            v->x = pv[0] + k * (v->x - d->pivot[0]);
            v->y = pv[1] + k * (v->y - d->pivot[1]);
            v->z = pv[2] + k * (v->z - d->pivot[2]);
            done[j] = 1;
        }
        d->pivot[0] = pv[0];
        d->pivot[1] = pv[1];
        d->pivot[2] = pv[2];
    }
    for (i = 0; i < s->nv; i++) {
        BmapVertex *v = &s->v[i];
        float ux = v->x - px, uz = v->z - pz, my = v->y;

        if (done != NULL && done[i]) {
            continue;
        }
        v->x = k * (ca * ux - sa * uz) + PLACE_SX;
        v->z = k * (sa * ux + ca * uz) + PLACE_SZ;
        v->y = GROUND_Y + k * (my - gy) - GROUND_LIFT;
    }
    free(done);
    if (!sp->field) {
        stand_drift(s);
    }
    topview_dump(s, nd0, 1, th);
    if (grass != NULL) {
        grass_grow_fixed(s, grass, nd0, 16.0f * k);
        free(grass);
    }
    free(g_tmask);
    g_tmask = NULL;
}
