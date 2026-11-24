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
#include "bmap_grass.h"

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
/* BmapPlaceSpec.no_back: a facing is "back" when it is more than about 105
 * degrees from north (the south half and the two diagonals beside it). */
#define BACK_COS (-0.25)
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

/* What a picture of the map is, by the name its makers gave it in the game's files (BmapTexture.name: the
 * model's own texture name, the same in every season; a season only changes the pixels).  Exact names, each
 * looked at beside its picture; a name not listed is none of these.
 *
 * k_tuft_names: the sheets that stand as tufts where they lie just over the ground: the tall grass and the dark
 * tall grass (kusa_ec1, kusa_ec2 and their "s"), the flowers (hana01.1, the season's kisetu_hana), the loose grass
 * blades and weeds (ue_grass00, ue_grass01, ou40_kusa01, t02kusa), the flower bushes (plant01, out40_pla01..03).
 * k_tree_names: a tree's pictures: the card trees' side, crown and top (ki00, ki02, ki03: a/a2/ax the side, b the
 * crown from above, d the top), White Forest's trees (wf_tree_*), and the forest's dead trunks and stumps
 * (dtree_*, stump_*: placed objects the camera's ball would bite into).
 * k_tree_shadow_names: the shadow a card tree lays on the ground (ki00c2, ki02c). */
static const char *const k_tuft_names[] = {
    "kusa_ec1", "kusa_ec1s", "kusa_ec2", "kusa_ec2s", "hana01.1", "kisetu_hana", "ue_grass00", "ue_grass01",
    "ou40_kusa01", "t02kusa", "plant01", "out40_pla01", "out40_pla02", "out40_pla03",
};
static const char *const k_tree_names[] = {
    "ki00a2", "ki00b2", "ki00d2", "ki02ax", "ki02bx", "ki02dx", "ki03ax", "ki03bx", "ki03dx",
    "wf_tree_a", "wf_tree_a2", "wf_tree_b", "wf_tree_c", "wf_tree_k",
    "dtree_01_01", "dtree_02_01", "dtree_02_02", "stump_01_01", "stump_01_02", "stump_02_01", "stump_02_02",
};
static const char *const k_tree_shadow_names[] = { "ki00c2", "ki02c" };

static int name_in(const BmapScene *s, const BmapDraw *g, const char *const *names, unsigned n)
{
    unsigned i;

    if (g->tex < 0 || (unsigned)g->tex >= s->nt || s->t[g->tex].name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(s->t[g->tex].name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
#define NAMED(s, g, table) name_in(s, g, table, (unsigned)(sizeof table / sizeof table[0]))

/* Whether a sheet is one of the grass and flower sheets that stand as tufts (by its picture's name). */
static int leafy(const BmapScene *s, const BmapDraw *g)
{
    return !g->translucent && NAMED(s, g, k_tuft_names);
}

/* The tufts, one set on each tile of the MAP that a grass triangle covers
 * (the map's own 16-unit tiles, found by undoing the placement: the tufts
 * of a patch stand at the same places of the map whatever the battle's
 * spot and angle): the same three tufts, the same height (`tile` is a
 * tile in battle units), on every tile, the whole grass picture on each;
 * the grass triangles are dropped after. */
static const unsigned char *g_tmask;      /* the grass triangles (bmap_place_spec), by index */

/* ---- The hand-made grass (bmap_grass.c): a buffer of triangles a grass sheet, and its leaves ---- */
/* A tile's part of a buffer: its first vertex and leaf, where they end, and its block (4 x 4 map tiles). */
typedef struct { unsigned v0, v1, l0, l1; int key; } GmTile;
typedef struct {
    BmapVertex *v;
    unsigned n, cap;
    int fail;
    BmapLeaf *lf;
    unsigned nlf, lcap;
    GmTile *tile;               /* a tile's part, in the order the tiles were built */
    unsigned ntile, tcap;
    unsigned *blk;              /* gm_blocks: nblk + 1 pairs (first vertex, first leaf): each block's start, and the end */
    unsigned nblk;
} GmBuf;
static float g_gm_shade = 1.0f;     /* (a new triangle's light, of its source's) */

typedef struct { GmBuf *b; const BmapVertex *src; } GlCtx;

static void gm_tri(GmBuf *b, const BmapVertex *src, const float p[3][3], const float uv[3][2]);

static void gl_emit(void *ctx, const float p[3][3], const float uv[3][2], float shade)
{
    GlCtx *c = (GlCtx *)ctx;

    g_gm_shade = shade > 1.0f ? 1.0f : shade;
    gm_tri(c->b, c->src, p, uv);
    g_gm_shade = 1.0f;
    if (c->b->nlf > 0u) {
        c->b->lf[c->b->nlf - 1u].count = c->b->n - c->b->lf[c->b->nlf - 1u].first;
    }
}

/* (research) a hand-made grass leaf begins: listed, for the pass that lays the grass over a Pokemon leaf by leaf */
static void gl_leaf(void *ctx, const float tip[3], const float heart[2], float reach)
{
    GmBuf *b = ((GlCtx *)ctx)->b;
    BmapLeaf *lf;

    if (b->nlf == b->lcap) {
        const unsigned nc = b->lcap ? b->lcap * 2u : 256u;
        BmapLeaf *nl = (BmapLeaf *)realloc(b->lf, nc * sizeof *nl);

        if (nl == NULL) {
            return;
        }
        b->lf = nl;
        b->lcap = nc;
    }
    lf = &b->lf[b->nlf++];
    lf->first = b->n;
    lf->count = 0u;
    lf->draw = 0u;
    memcpy(lf->tip, tip, sizeof lf->tip);
    memcpy(lf->heart, heart, sizeof lf->heart);
    lf->reach = reach;
}

static void gm_tri(GmBuf *b, const BmapVertex *src, const float p[3][3], const float uv[3][2])
{
    int k;

    if (b->fail) {
        return;
    }
    if (b->n + 3 > b->cap) {
        unsigned nc = b->cap ? b->cap * 2 : 4096;
        BmapVertex *nv = (BmapVertex *)realloc(b->v, nc * sizeof *nv);

        if (nv == NULL) {
            b->fail = 1;
            return;
        }
        b->v = nv;
        b->cap = nc;
    }
    for (k = 0; k < 3; k++) {
        BmapVertex *o = &b->v[b->n++];

        *o = *src;
        o->x = p[k][0];
        o->y = p[k][1];
        o->z = p[k][2];
        o->u = uv[k][0];
        o->v = uv[k][1];
        o->a = 255;
        if (g_gm_shade != 1.0f) {
            o->r = (unsigned char)(o->r * g_gm_shade);
            o->g = (unsigned char)(o->g * g_gm_shade);
            o->b = (unsigned char)(o->b * g_gm_shade);
        }
    }
}

/* A tile's grass begins in the buffer (block `key`). */
static void gm_tile_begin(GmBuf *b, int key)
{
    if (b->ntile == b->tcap) {
        const unsigned nc = b->tcap ? b->tcap * 2u : 256u;
        GmTile *nt = (GmTile *)realloc(b->tile, nc * sizeof *nt);

        if (nt == NULL) {
            return;
        }
        b->tile = nt;
        b->tcap = nc;
    }
    b->tile[b->ntile].v0 = b->n;
    b->tile[b->ntile].l0 = b->nlf;
    b->tile[b->ntile].key = key;
    b->ntile++;
}

static int gm_tile_cmp(const void *pa, const void *pb)
{
    const GmTile *a = (const GmTile *)pa, *b = (const GmTile *)pb;

    return a->key != b->key ? (a->key < b->key ? -1 : 1) : a->v0 < b->v0 ? -1 : a->v0 > b->v0;
}

/* The buffer's tiles put in the order of their blocks, a block's tiles together: its vertices and leaves moved, and
 * b->blk filled.  A buffer with no tile list, or with no memory for the move, is one block as it is. */
static void gm_blocks(GmBuf *b)
{
    BmapVertex *nv = NULL;
    BmapLeaf *nl = NULL;
    unsigned *blk = NULL;
    unsigned k, q, vn = 0, ln = 0, nb = 0;

    free(b->blk);
    b->blk = NULL;
    b->nblk = 0;
    if (b->n == 0u || b->fail) {
        return;
    }
    for (k = 0; k < b->ntile; k++) {
        b->tile[k].v1 = k + 1u < b->ntile ? b->tile[k + 1u].v0 : b->n;
        b->tile[k].l1 = k + 1u < b->ntile ? b->tile[k + 1u].l0 : b->nlf;
    }
    if (b->ntile > 0u && b->tile[0].v0 == 0u && b->tile[0].l0 == 0u) {
        nv = (BmapVertex *)malloc((size_t)b->n * sizeof *nv);
        nl = (BmapLeaf *)malloc((size_t)(b->nlf > 0u ? b->nlf : 1u) * sizeof *nl);
        blk = (unsigned *)malloc(((size_t)b->ntile + 1u) * 2u * sizeof *blk);
    }
    if (nv == NULL || nl == NULL || blk == NULL) {
        free(nv);
        free(nl);
        free(blk);
        b->blk = (unsigned *)malloc(4u * sizeof *b->blk);
        if (b->blk != NULL) {
            b->blk[0] = b->blk[1] = 0u;
            b->blk[2] = b->n;
            b->blk[3] = b->nlf;
            b->nblk = 1;
        }
        return;
    }
    qsort(b->tile, b->ntile, sizeof *b->tile, gm_tile_cmp);
    for (k = 0; k < b->ntile; k++) {
        const GmTile *t = &b->tile[k];

        if (k == 0u || t->key != b->tile[k - 1u].key) {
            blk[nb * 2u] = vn;
            blk[nb * 2u + 1u] = ln;
            nb++;
        }
        memcpy(&nv[vn], &b->v[t->v0], (size_t)(t->v1 - t->v0) * sizeof *nv);
        for (q = t->l0; q < t->l1; q++) {
            nl[ln] = b->lf[q];
            nl[ln].first = b->lf[q].first - t->v0 + vn;
            ln++;
        }
        vn += t->v1 - t->v0;
    }
    blk[nb * 2u] = vn;
    blk[nb * 2u + 1u] = ln;
    free(b->v);
    free(b->lf);
    b->v = nv;
    b->lf = nl;
    b->cap = b->n;
    b->lcap = b->nlf > 0u ? b->nlf : 1u;
    b->blk = blk;
    b->nblk = nb;
}

static int g_place_doubles;     /* the rule of the battle being placed (BmapPlaceSpec.doubles): 0 single, 1 double, 2 triple, 3 rotation */
static const float k_double[4][2];
/* A triple battle's and a rotation battle's six places (ov094 gBtlvMcssSlotPosTriple, gBtlvMcssSlotPosRotation):
 * the player's three, then the foe's three. */
static const float k_triple[6][2] = {
    { -4.0f, 7.0f }, { 0.75f, 4.75f }, { 5.19f, 6.19f }, { 4.5f, -12.0f }, { 0.74f, -9.0f }, { -4.39f, -12.0f },
};
static const float k_rotation[6][2] = {
    { 0.0f, 7.0f }, { -4.33f, 14.5f }, { 4.33f, 14.5f }, { 0.0f, -8.0f }, { 4.33f, -15.5f }, { -4.33f, -15.5f },
};

/* The places of the battle's Pokemon by its rule (battle units, x and z): the player's side first, then the foe's
 * (pushed back with the foe's slot).  Returns how many. */
static int battle_places(float at[6][2])
{
    const float (*t)[2] = g_place_doubles == 1 ? k_double : g_place_doubles == 2 ? k_triple : g_place_doubles == 3 ? k_rotation : NULL;
    const int n = g_place_doubles == 1 ? 4 : 6;
    int k;

    if (t == NULL) {
        at[0][0] = 0.5f;
        at[0][1] = PLACE_SZ;
        at[1][0] = FOE_SX;
        at[1][1] = FOE_SZ;
        return 2;
    }
    for (k = 0; k < n; k++) {
        at[k][0] = t[k][0];
        at[k][1] = t[k][1] + (k >= n / 2 ? FOE_SZ - FOE_SZ_GAME : 0.0f);
    }
    return n;
}

/* Where the hand-made bush of the map tile (cx, cz) stands, map units: the tile's middle, or, when one of the
 * battle's Pokemon stands on the tile, that Pokemon's place: it stands in the bush, not beside it.  A tile has one
 * bush: with two Pokemon on the tile it stays at the tile's middle.  ca, sa, pk: the placement's turn and scale.
 * BMAP_GRASS_FEET=0: every bush at its tile's middle. */
static void grass_bush_place(const BmapScene *s, float ca, float sa, float pk, int cx, int cz, float *mx, float *mz)
{
    static int on = -1;
    float at[6][2];
    int k, n = 0, hit = -1, hits = 0;

    if (on < 0) {
        const char *e = getenv("BMAP_GRASS_FEET");

        on = e == NULL || e[0] != '0';
    }
    if (!on) {
        return;
    }
    if (g_place_doubles != 0) {
        float bt[6][2];
        const int nb = battle_places(bt);

        for (k = 0; k < nb; k++) {
            const float fx = (bt[k][0] - PLACE_SX) / pk, fz = (bt[k][1] - PLACE_SZ) / pk;

            at[n][0] = s->place_x + ca * fx + sa * fz;
            at[n][1] = s->place_z - sa * fx + ca * fz;
            n++;
        }
    } else {
        const float fx = (FOE_SX - PLACE_SX) / pk, fz = (FOE_SZ - PLACE_SZ) / pk;

        at[0][0] = s->place_x;
        at[0][1] = s->place_z;
        at[1][0] = s->place_x + ca * fx + sa * fz;
        at[1][1] = s->place_z - sa * fx + ca * fz;
        n = 2;
    }
    for (k = 0; k < n; k++) {
        if ((int)floorf(at[k][0] / 16.0f) == cx && (int)floorf(at[k][1] / 16.0f) == cz) {
            hit = k;
            hits++;
        }
    }
    if (hits == 1) {
        *mx = at[hit][0];
        *mz = at[hit][1];
    }
}

static void grass_grow_fixed(BmapScene *s, const unsigned char *grass, unsigned nd0, float tile)
{
    const unsigned char *mask = g_tmask;
    static const float spot[3][2] = { { -0.25f, -0.2f }, { 0.25f, -0.2f }, { 0.0f, 0.25f } };
    const float w = 0.3f * tile * 0.7071f, hh = 0.5f * tile;
    unsigned i, need = 0, nv, nd;
    BmapVertex *v;
    BmapDraw *d;
    unsigned char *used;
    /* (the placement: the map turned by place_a and scaled by place_k about the own Pokemon's place) */
    const float ca = cosf(s->place_a), sa = sinf(s->place_a), pk = s->place_k > 0.0f ? s->place_k : tile / 16.0f;
    const int tx0 = (int)floorf(s->place_x / 16.0f), tz0 = (int)floorf(s->place_z / 16.0f);
    GmBuf *gbuf = NULL;             /* (the hand-made grass: a buffer a draw) */
    int *gdraw_m = NULL;
    unsigned tufts_all = 0, tufts_ok = 0;

    free(s->leaf);                  /* (the leaves of an earlier placement) */
    s->leaf = NULL;
    s->n_leaf = 0u;

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
    {
        if (bmap_grass_count() > 0 && nd0 > 0) {
            /* (the hand-made grass that is pressed down where a Pokemon stands: the places, battle units) */
            float at[6][2];
            const int nat = battle_places(at);

            bmap_grass_press((const float (*)[2])at, nat);
            gbuf = (GmBuf *)calloc(nd0, sizeof *gbuf);
            gdraw_m = (int *)calloc(nd0, sizeof *gdraw_m);
            for (i = 0; gbuf != NULL && gdraw_m != NULL && i < nd0; i++) {
                const BmapTexture *gt = grass[i] && s->d[i].tex >= 0 && (unsigned)s->d[i].tex < s->nt ? &s->t[s->d[i].tex] : NULL;

                if (gt == NULL) {
                    continue;
                }
                if (getenv("BMAP_GRASS_LOG") != NULL) {
                    fprintf(stderr, "bmap grass sheet %u: picture %s, kind %d\n", i, gt->name, bmap_grass_find(gt->name) != NULL);
                }
                if (bmap_grass_find(gt->name) != NULL) {
                    gdraw_m[i] = 30;         /* (a hand-made kind) */
                }
            }
            if (gdraw_m == NULL || gbuf == NULL) {
                free(gbuf);
                free(gdraw_m);
                gbuf = NULL;
                gdraw_m = NULL;
            }
        }
    }
    for (i = 0; used != NULL && i < nd0; i++) {
        BmapDraw *g = &s->d[i];
        BmapDraw n;
        unsigned j, start = nv;

        if (!grass[i]) {
            continue;
        }
        if (gdraw_m != NULL && gdraw_m[i] == 30) {      /* (a hand-made sheet begins: no tile is its own yet) */
            unsigned q2;

            for (q2 = 0; q2 < (unsigned)TUFT_SPAN * TUFT_SPAN; q2++) {
                used[q2] &= (unsigned char)~2u;
            }
        }
        for (j = g->first; j + 2 < g->first + g->count; j += 3) {
            const BmapVertex *a = &s->v[j], *b = &s->v[j + 1], *c = &s->v[j + 2];
            float m[3][2], x0, x1, z0, z1;
            int cx, cz, q;

            if (mask != NULL && !mask[j / 3]) {
                continue;               /* not grass (a rug, a path in the same draw) */
            }
            /* (the triangle in map units: the placement undone) */
            for (q = 0; q < 3; q++) {
                const BmapVertex *vq = &s->v[j + (unsigned)q];
                const float bx = (vq->x - PLACE_SX) / pk, bz = (vq->z - PLACE_SZ) / pk;

                m[q][0] = s->place_x + ca * bx + sa * bz;
                m[q][1] = s->place_z - sa * bx + ca * bz;
            }
            x0 = fminf(m[0][0], fminf(m[1][0], m[2][0]));
            x1 = fmaxf(m[0][0], fmaxf(m[1][0], m[2][0]));
            z0 = fminf(m[0][1], fminf(m[1][1], m[2][1]));
            z1 = fmaxf(m[0][1], fmaxf(m[1][1], m[2][1]));
            for (cx = (int)floorf(x0 / 16.0f); (float)cx * 16.0f <= x1; cx++) {
                for (cz = (int)floorf(z0 / 16.0f); (float)cz * 16.0f <= z1; cz++) {
                    /* (the map's own tile: its middle) */
                    const float mx = ((float)cx + 0.5f) * 16.0f, mz = ((float)cz + 0.5f) * 16.0f;
                    const float d1 = (m[1][0] - m[0][0]) * (mz - m[0][1]) - (m[1][1] - m[0][1]) * (mx - m[0][0]);
                    const float d2 = (m[2][0] - m[1][0]) * (mz - m[1][1]) - (m[2][1] - m[1][1]) * (mx - m[1][0]);
                    const float d3 = (m[0][0] - m[2][0]) * (mz - m[2][1]) - (m[0][1] - m[2][1]) * (mx - m[2][0]);
                    const int ux = cx - tx0 + TUFT_SPAN / 2, uz = cz - tz0 + TUFT_SPAN / 2;
                    const float y = a->y;
                    int t;

                    if (!((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))) continue;
                    if (ux < 0 || uz < 0 || ux >= TUFT_SPAN || uz >= TUFT_SPAN) continue;
                    if (gdraw_m != NULL && gdraw_m[i] == 30) {
                        /* (a hand-made grass: each of a tile's sheets is built, the light top and the dark body;
                         * bit 1: this sheet has this tile) */
                        if (used[uz * TUFT_SPAN + ux] & 2u) continue;
                        used[uz * TUFT_SPAN + ux] |= 2u;
                    } else {
                        if (used[uz * TUFT_SPAN + ux] & 1u) continue;
                        used[uz * TUFT_SPAN + ux] |= 1u;
                    }
                    {
                        /* research (BMAP_TUFT_LOG): the tile's middle put into the battle must lie in the triangle
                         * as the battle has it (the way there and the way back agree) */
                        const float rx = mx - s->place_x, rz = mz - s->place_z;
                        const float tx = PLACE_SX + pk * (ca * rx - sa * rz), tz = PLACE_SZ + pk * (sa * rx + ca * rz);
                        const float e1 = (b->x - a->x) * (tz - a->z) - (b->z - a->z) * (tx - a->x);
                        const float e2 = (c->x - b->x) * (tz - b->z) - (c->z - b->z) * (tx - b->x);
                        const float e3 = (a->x - c->x) * (tz - c->z) - (a->z - c->z) * (tx - c->x);
                        const float tol = 1e-3f;

                        tufts_ok += (e1 >= -tol && e2 >= -tol && e3 >= -tol) || (e1 <= tol && e2 <= tol && e3 <= tol);
                        tufts_all++;
                    }
                    if (gbuf != NULL && gdraw_m[i] != 0) {
                        float bmx = mx, bmz = mz;       /* (where the tile's bush stands) */
                        const BmapTexture *ft = g->tex >= 0 && (unsigned)g->tex < s->nt ? &s->t[g->tex] : NULL;
                        const BmapGrassKind *fk = gdraw_m[i] == 30 && ft != NULL ? bmap_grass_find(ft->name) : NULL;
                        /* (the bush at the feet: not for a kind that is pressed down there, its tile holds several) */
                        const float rx = ((fk != NULL && !bmap_grass_parts(fk) ? grass_bush_place(s, ca, sa, pk, cx, cz, &bmx, &bmz) : (void)0), bmx)
                                         - s->place_x,
                                    rz = bmz - s->place_z;
                        const float bx = PLACE_SX + pk * (ca * rx - sa * rz), bz = PLACE_SZ + pk * (sa * rx + ca * rz);
                        const float mxv[2] = { pk * ca, pk * sa }, mzv[2] = { -pk * sa, pk * ca };
                        const BmapTexture *gt = g->tex >= 0 && (unsigned)g->tex < s->nt ? &s->t[g->tex] : NULL;

                        if (gdraw_m[i] == 30 && gt != NULL) {
                            const BmapGrassKind *gg = bmap_grass_find(gt->name);
                            const BmapTexture *at = NULL;
                            GlCtx gc;
                            char an[17];
                            unsigned k3;

                            bmap_grass_atlas_name(gg, an);
                            for (k3 = 0; k3 < s->nt; k3++) {
                                if (strcmp(s->t[k3].name, an) == 0) {
                                    at = &s->t[k3];
                                    break;
                                }
                            }
                            if (at == NULL) {
                                goto old_tufts;         /* (a scene built without the atlas) */
                            }
                            gc.b = &gbuf[i];
                            gc.src = a;
                            gm_tile_begin(&gbuf[i], (uz / 4) * (TUFT_SPAN / 4 + 1) + ux / 4);
                            bmap_grass_tile(gg, at, bx, y, bz, mxv, mzv, pk, ((unsigned)(cx * 73856093) ^ (unsigned)(cz * 19349663)),
                                            gl_emit, gl_leaf, &gc);
                            continue;
                        }
                        goto old_tufts;
                    }
old_tufts:
                    for (t = 0; t < 3 && nv + 12 <= s->nv + need; t++) {
                        /* (the tuft's place on the map, then in the battle; its two crossed cards along the map's
                         * diagonals, turned with the map) */
                        const float tmx = mx + spot[t][0] * 16.0f - s->place_x, tmz = mz + spot[t][1] * 16.0f - s->place_z;
                        const float ox = PLACE_SX + pk * (ca * tmx - sa * tmz), oz = PLACE_SZ + pk * (sa * tmx + ca * tmz);
                        const float ax = w * (ca - sa), az = w * (sa + ca);     /* the map's (1, 1) way */
                        const float cxw = w * (ca + sa), czw = w * (sa - ca);   /* the map's (1, -1) way */
                        float q1[4][3] = { { ox - ax, y, oz - az }, { ox + ax, y, oz + az }, { ox + ax, y + hh, oz + az }, { ox - ax, y + hh, oz - az } };
                        float q2[4][3] = { { ox - cxw, y, oz - czw }, { ox + cxw, y, oz + czw }, { ox + cxw, y + hh, oz + czw }, { ox - cxw, y + hh, oz - czw } };

                        quad(&s->v[nv], (const float (*)[3])q1, a);
                        nv += 6;
                        quad(&s->v[nv], (const float (*)[3])q2, a);
                        nv += 6;
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
    if (gbuf != NULL) {
        /* (each sheet's hand-made triangles: a draw a block of 4 x 4 map tiles, so a renderer leaves out the blocks
         * that are off the screen) */
        unsigned extra = 0, nblocks = 0;

        for (i = 0; i < nd0; i++) {
            gm_blocks(&gbuf[i]);
            extra += gbuf[i].blk != NULL ? gbuf[i].n : 0u;
            nblocks += gbuf[i].nblk;
        }
        if (extra > 0) {
            BmapVertex *v2 = (BmapVertex *)realloc(s->v, (nv + extra) * sizeof *v2);
            BmapDraw *d2 = v2 != NULL ? (BmapDraw *)realloc(s->d, (nd + nblocks) * sizeof *d2) : NULL;

            if (v2 != NULL) s->v = v2;
            if (d2 != NULL) s->d = d2;
            for (i = 0; v2 != NULL && d2 != NULL && i < nd0; i++) {
                BmapDraw n2;
                unsigned lf0, bk, q;
                int listed = 0;

                if (gbuf[i].n == 0 || gbuf[i].blk == NULL) {
                    continue;
                }
                lf0 = s->n_leaf;
                memcpy(&s->v[nv], gbuf[i].v, gbuf[i].n * sizeof *gbuf[i].v);
                if (gbuf[i].nlf > 0u) {         /* (its leaves: listed in the scene) */
                    BmapLeaf *nl = (BmapLeaf *)realloc(s->leaf, (s->n_leaf + gbuf[i].nlf) * sizeof *nl);

                    for (q = 0; nl != NULL && q < gbuf[i].nlf; q++) {
                        nl[s->n_leaf + q] = gbuf[i].lf[q];
                        nl[s->n_leaf + q].first += nv;
                    }
                    if (nl != NULL) {
                        s->leaf = nl;
                        s->n_leaf += gbuf[i].nlf;
                        listed = 1;
                    }
                }
                n2 = s->d[i];
                if (getenv("BMAP_GRASS_LOG") != NULL) {
                    fprintf(stderr, "bmap grass draw %u: %u triangles in %u blocks, picture %s\n", i, gbuf[i].n / 3u, gbuf[i].nblk,
                            s->d[i].tex >= 0 ? s->t[s->d[i].tex].name : "-");
                }
                n2.cull = 0;
                n2.billboard = 0;
                n2.grass = 0;
                if (gdraw_m[i] == 30 && n2.tex >= 0 && (unsigned)n2.tex < s->nt) {
                    const BmapGrassKind *gg = bmap_grass_find(s->t[n2.tex].name);
                    char an[17];
                    unsigned k3;

                    if (gg != NULL) {
                        n2.grass = (unsigned char)bmap_grass_over(gg);
                        bmap_grass_atlas_name(gg, an);
                        for (k3 = 0; k3 < s->nt; k3++) {
                            if (strcmp(s->t[k3].name, an) == 0) {
                                n2.tex = (int)k3;
                                break;
                            }
                        }
                    }
                }
                for (bk = 0; bk < gbuf[i].nblk; bk++) {
                    const unsigned *b0 = &gbuf[i].blk[bk * 2u], *b1 = b0 + 2;

                    if (b1[0] == b0[0]) {
                        continue;
                    }
                    n2.first = nv + b0[0];
                    n2.count = b1[0] - b0[0];
                    n2.leafy = listed && b1[1] > b0[1];
                    n2.leaf0 = listed ? lf0 + b0[1] : 0u;
                    n2.nleaf = listed ? b1[1] - b0[1] : 0u;
                    for (q = b0[1]; listed && q < b1[1]; q++) {
                        s->leaf[lf0 + q].draw = nd;
                    }
                    s->d[nd++] = n2;
                }
                nv += gbuf[i].n;
            }
        }
        for (i = 0; i < nd0; i++) {
            free(gbuf[i].v);
            free(gbuf[i].lf);
            free(gbuf[i].tile);
            free(gbuf[i].blk);
        }
        free(gbuf);
        free(gdraw_m);
    }
    if (getenv("BMAP_TUFT_LOG") != NULL) {
        fprintf(stderr, "tufts: %u tiles, %u of them in their triangle in battle space; place %.0f %.0f angle %.0f\n", tufts_all,
                tufts_ok, (double)s->place_x, (double)s->place_z, (double)(s->place_a * 57.29578f));
    }
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
        if ((s->v[d->first].obj & 0x80000000u) && (s->v[d->first + d->count - 3u].obj & 0x80000000u)) {
            /* research: a draw of completed bodies' faces, passed over as a whole (the search runs this loop
             * for every spot and angle it tries: face by face, thousands of faces a rock made it ten times
             * slower, and the battle's map came on screen a second late) */
            continue;
        }
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            const BmapVertex *v = &s->v[j];
            float p[3][3], lo[3], hi[3];

            if ((v->x - px) * (v->x - px) + (v->z - pz) * (v->z - pz) > near2) {
                continue;
            }
            if (v->obj & 0x80000000u) {
                /* research: a completed body's small faces (thousands a rock) are not weighed one by one;
                 * the search does not see such a body yet (to do: a stand-in of a few faces) */
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

/* Research (BMAP_FIX_BACKS=1): a north side for the placed objects that have
 * none.  The map's buildings are made for the field camera, which looks
 * north: their south, east and west faces are there, their north faces are
 * not.  An object whose faces toward the north are under a fifth of its
 * faces toward the south gets those south faces again, turned half round
 * about the upright through its middle: the front as its back, a little
 * darker.  In map units, before the placement. */
static void fix_backs(BmapScene *s)
{
    typedef struct { float lo[3], hi[3], south, north; int any; } Ob;
    unsigned i, j, nd0 = s->nd, maxobj = 0, extra = 0, at, made = 0;
    Ob *ob;
    BmapVertex *v;
    BmapDraw *d;

    if (getenv("BMAP_FIX_BACKS") == NULL) {
        return;
    }
    for (i = 0; i < s->nv; i++) {
        if (s->v[i].obj > maxobj) {
            maxobj = s->v[i].obj;
        }
    }
    if (maxobj == 0 || maxobj > 100000u) {
        return;
    }
    ob = (Ob *)calloc(maxobj + 1u, sizeof *ob);
    if (ob == NULL) {
        return;
    }
#define TRI_N(p, n, len) do { \
        float e1[3] = { (p)[1].x - (p)[0].x, (p)[1].y - (p)[0].y, (p)[1].z - (p)[0].z }; \
        float e2[3] = { (p)[2].x - (p)[0].x, (p)[2].y - (p)[0].y, (p)[2].z - (p)[0].z }; \
        (n)[0] = e1[1] * e2[2] - e1[2] * e2[1]; (n)[1] = e1[2] * e2[0] - e1[0] * e2[2]; (n)[2] = e1[0] * e2[1] - e1[1] * e2[0]; \
        (len) = sqrtf((n)[0] * (n)[0] + (n)[1] * (n)[1] + (n)[2] * (n)[2]); \
    } while (0)
    for (i = 0; i < nd0; i++) {
        const BmapDraw *dr = &s->d[i];

        if (dr->cull != 1 || dr->billboard || dr->first + dr->count > s->nv) {
            continue;
        }
        for (j = 0; j + 2 < dr->count; j += 3) {
            const BmapVertex *p = &s->v[dr->first + j];
            float n[3], len;
            Ob *o = &ob[p->obj];
            int q;

            if (p->obj == 0) {
                continue;
            }
            TRI_N(p, n, len);
            for (q = 0; q < 3; q++) {
                float c[3] = { p[q].x, p[q].y, p[q].z };
                int a;

                for (a = 0; a < 3; a++) {
                    if (!o->any || c[a] < o->lo[a]) o->lo[a] = c[a];
                    if (!o->any || c[a] > o->hi[a]) o->hi[a] = c[a];
                }
                o->any = 1;
            }
            if (n[2] > 0.0f) {
                o->south += n[2] * 0.5f;        /* the area it shows a camera to its south */
            } else {
                o->north -= n[2] * 0.5f;
            }
            (void)len;
        }
    }
    for (i = 0; i < nd0; i++) {
        const BmapDraw *dr = &s->d[i];

        if (dr->cull != 1 || dr->billboard || dr->first + dr->count > s->nv) {
            continue;
        }
        for (j = 0; j + 2 < dr->count; j += 3) {
            const BmapVertex *p = &s->v[dr->first + j];
            const Ob *o = &ob[p->obj];
            float n[3], len;

            if (p->obj == 0 || o->hi[1] - o->lo[1] < 8.0f || o->south < 64.0f || o->north > 0.2f * o->south) {
                continue;
            }
            TRI_N(p, n, len);
            if (len > 0.0f && n[2] / len > 0.3f) {
                extra += 3;
            }
        }
    }
    v = extra > 0 ? (BmapVertex *)realloc(s->v, (s->nv + extra + 1u) * sizeof *v) : NULL;
    d = v != NULL ? (BmapDraw *)realloc(s->d, (s->nd * 2u + 1u) * sizeof *d) : NULL;
    if (v != NULL) {
        s->v = v;
    }
    if (d == NULL) {
        free(ob);
        return;
    }
    s->d = d;
    at = s->nv;
    for (i = 0; i < nd0; i++) {
        BmapDraw nw = s->d[i];
        unsigned first = at;

        if (nw.cull != 1 || nw.billboard || nw.first + nw.count > s->nv) {
            continue;
        }
        for (j = 0; j + 2 < nw.count; j += 3) {
            const BmapVertex *p = &s->v[nw.first + j];
            const Ob *o = &ob[p->obj];
            float n[3], len, cx, cz;
            int q;

            if (p->obj == 0 || o->hi[1] - o->lo[1] < 8.0f || o->south < 64.0f || o->north > 0.2f * o->south) {
                continue;
            }
            TRI_N(p, n, len);
            if (!(len > 0.0f && n[2] / len > 0.3f)) {
                continue;
            }
            cx = o->lo[0] + o->hi[0];
            cz = o->lo[2] + o->hi[2];
            for (q = 0; q < 3; q++) {
                BmapVertex *w = &s->v[at + (unsigned)q];

                *w = p[q];
                w->x = cx - p[q].x;
                w->z = cz - p[q].z;
                w->r = (unsigned char)(p[q].r * 85 / 100);
                w->g = (unsigned char)(p[q].g * 85 / 100);
                w->b = (unsigned char)(p[q].b * 85 / 100);
            }
            at += 3;
        }
        if (at > first) {
            nw.first = first;
            nw.count = at - first;
            nw.grass = 0;
            s->d[s->nd++] = nw;
            made++;
        }
    }
    s->nv = at;
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        unsigned k2, nobj = 0;

        for (k2 = 1; k2 <= maxobj; k2++) {
            const Ob *o = &ob[k2];

            nobj += o->any && o->hi[1] - o->lo[1] >= 8.0f && o->south >= 64.0f && o->north <= 0.2f * o->south;
        }
        fprintf(stderr, "bmap: backs made for %u objects, %u triangles in %u draws\n", nobj, extra / 3, made);
    }
#undef TRI_N
    free(ob);
}

/* Research (BMAP_DEBUG_IDS=1): every draw in a flat colour of its own, one a
 * draw and placed object, untextured, and the colours listed on stderr
 * ("bmap: id RRGGBB draw D texture T object O"), so a pixel of a picture says
 * what the battle drew there. */
static void debug_ids(BmapScene *s)
{
    unsigned i, j;

    if (getenv("BMAP_DEBUG_IDS") == NULL) {
        return;
    }
    for (i = 0; i < s->nd; i++) {
        BmapDraw *d = &s->d[i];
        unsigned last = 0xFFFFFFFFu;

        if (d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j < d->first + d->count; j++) {
            BmapVertex *v = &s->v[j];
            const unsigned key = (i * 2654435761u) ^ ((v->obj & 0x3FFFFFFFu) * 40503u) ^ (v->obj >> 30) * 977u;
            const unsigned char r = (unsigned char)(40 + (key >> 3) % 216), g = (unsigned char)(40 + (key >> 11) % 216),
                                b = (unsigned char)(40 + (key >> 19) % 216);

            if (v->obj != last) {
                fprintf(stderr, "bmap: id %02X%02X%02X draw %u texture %d object %08X\n", r, g, b, i, d->tex, v->obj);
                last = v->obj;
            }
            v->r = r;
            v->g = g;
            v->b = b;
            v->a = 255;
        }
        d->tex = -1;
        d->alpha = 31;
        d->translucent = 0;
        d->uvanim = 0;
    }
}

/* Research (BMAP_DEBUG_BACK=1): the other side of every one-sided draw (the
 * map culls its back faces: a wall seen from behind is not drawn at all) as
 * plain magenta, so a picture shows where a camera looks at a model from the
 * side its makers never drew. */
static void debug_backs(BmapScene *s)
{
    unsigned i, j, nd0 = s->nd, extra = 0, at;
    BmapVertex *v;
    BmapDraw *d;

    if (getenv("BMAP_DEBUG_BACK") == NULL) {
        return;
    }
    for (i = 0; i < nd0; i++) {
        if (s->d[i].cull == 1 && !s->d[i].billboard && s->d[i].first + s->d[i].count <= s->nv) {
            extra += s->d[i].count;
        }
    }
    v = (BmapVertex *)realloc(s->v, (s->nv + extra + 1u) * sizeof *v);
    if (v == NULL) {
        return;
    }
    s->v = v;
    d = (BmapDraw *)realloc(s->d, (s->nd * 2u + 1u) * sizeof *d);
    if (d == NULL) {
        return;
    }
    s->d = d;
    at = s->nv;
    for (i = 0; i < nd0; i++) {
        BmapDraw n = s->d[i];

        if (n.cull != 1 || n.billboard || n.first + n.count > s->nv || n.count < 3) {
            continue;
        }
        for (j = 0; j + 2 < n.count; j += 3) {
            const BmapVertex *o = &s->v[n.first + j];
            int q;

            s->v[at + j] = o[0];
            s->v[at + j + 1] = o[2];
            s->v[at + j + 2] = o[1];
            for (q = 0; q < 3; q++) {
                s->v[at + j + q].r = 255;
                s->v[at + j + q].g = 0;
                s->v[at + j + q].b = 255;
                s->v[at + j + q].a = 255;
            }
        }
        n.first = at;
        n.count = n.count / 3 * 3;
        n.tex = -1;
        n.alpha = 31;
        n.translucent = 0;
        n.grass = 0;
        n.uvanim = 0;
        s->d[s->nd++] = n;
        at += n.count;
    }
    s->nv = at;
}

/* Research (BMAP_DUMP=file): the whole scene in map units as the battle is
 * about to place it: vertices, draws and textures, with the placement chosen. */
static void scene_dump(const BmapScene *s, const float hdr[6])
{
    const char *path = getenv("BMAP_DUMP");
    FILE *f;
    unsigned i;

    if (path == NULL || (f = fopen(path, "wb")) == NULL) {
        return;
    }
    fwrite("BMD1", 1, 4, f);
    fwrite(hdr, sizeof(float), 6, f);
    fwrite(&s->nv, 4, 1, f);
    fwrite(&s->nd, 4, 1, f);
    fwrite(&s->nt, 4, 1, f);
    fwrite(s->clear, 1, 3, f);
    fwrite(s->fog, 1, 3, f);
    {
        unsigned char on = (unsigned char)s->fog_on, pad = 0;

        fwrite(&on, 1, 1, f);
        fwrite(&pad, 1, 1, f);
    }
    fwrite(&s->fog_start, sizeof(float), 1, f);
    fwrite(&s->fog_end, sizeof(float), 1, f);
    for (i = 0; i < s->nv; i++) {
        const BmapVertex *v = &s->v[i];

        fwrite(&v->x, sizeof(float), 5, f);
        fwrite(&v->r, 1, 4, f);
        fwrite(&v->obj, 4, 1, f);
    }
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];
        unsigned char b[8] = { d->alpha, d->cull, d->translucent, d->billboard, d->far, d->grass, 0, 0 };

        fwrite(&d->tex, 4, 1, f);
        fwrite(&d->first, 4, 1, f);
        fwrite(&d->count, 4, 1, f);
        fwrite(b, 1, 8, f);
        fwrite(d->pivot, sizeof(float), 3, f);
    }
    for (i = 0; i < s->nt; i++) {
        const BmapTexture *t = &s->t[i];
        int wh[2] = { t->rgba != NULL ? t->w : 0, t->rgba != NULL ? t->h : 0 };
        unsigned char b[4] = { t->repeat_s, t->repeat_t, t->flip_s, t->flip_t };

        fwrite(wh, 4, 2, f);
        fwrite(b, 1, 4, f);
        if (wh[0] > 0 && wh[1] > 0) {
            fwrite(t->rgba, 4, (size_t)wh[0] * (size_t)wh[1], f);
        }
    }
    {                                   /* the walk grid, 128 x 128 tiles around the start */
        int t0[2], x, z;

        t0[0] = (int)floorf(hdr[0] / 16.0f) - 64;
        t0[1] = (int)floorf(hdr[1] / 16.0f) - 64;
        fwrite(t0, sizeof(int), 2, f);
        for (z = 0; z < 128; z++) {
            for (x = 0; x < 128; x++) {
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
    dst->bound = NULL;                  /* (made again when the copy is placed) */
    dst->n_bound = 0;
    dst->trees = NULL;                  /* (found again when the copy is placed) */
    dst->n_trees = 0;
    dst->leaf = NULL;                   /* (listed again when the copy is placed) */
    dst->n_leaf = 0;
    dst->v = (BmapVertex *)malloc((src->nv > 0 ? src->nv : 1) * sizeof *dst->v);
    dst->d = (BmapDraw *)malloc((src->nd > 0 ? src->nd : 1) * sizeof *dst->d);
    dst->prep = NULL;
    if (src->prep != NULL) {
        dst->prep = (unsigned char *)malloc(src->prep_nd0 + src->prep_ntri + 1u);
    }
    if (dst->v == NULL || dst->d == NULL || (src->prep != NULL && dst->prep == NULL)) {
        free(dst->v);
        free(dst->d);
        free(dst->prep);
        dst->v = NULL;
        dst->d = NULL;
        dst->prep = NULL;
        return -1;
    }
    memcpy(dst->v, src->v, src->nv * sizeof *dst->v);
    memcpy(dst->d, src->d, src->nd * sizeof *dst->d);
    if (src->prep != NULL) {
        memcpy(dst->prep, src->prep, src->prep_nd0 + src->prep_ntri);
    }
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

float bmap_view_depth(const float eye[3], const float target[3], const float p[3])
{
    float f[3], len = 0.0f, d = 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = target[i] - eye[i];
        len += f[i] * f[i];
    }
    len = sqrtf(len);
    if (len <= 0.0f) {
        return 0.0f;
    }
    for (i = 0; i < 3; i++) {
        d += (p[i] - eye[i]) * f[i] / len;
    }
    return d;
}

float bmap_cmd_depth(const float p[3])
{
    return bmap_view_depth(k_cmd_eye, k_cmd_target, p);
}

float bmap_mon_size(const float p[3])
{
    return bmap_cmd_depth(p);
}

void bmap_rest_reset(BmapRest *r)
{
    int i;

    for (i = 0; i < BMAP_REST_MAX; i++) {
        r->rest[i] = 0.0f;
    }
    if (r->far <= 0.0f) {
        r->far = 0.5f;
    }
    r->still = r->far_still = 0;
}

void bmap_rest_frame(BmapRest *r, const float game_eye[3])
{
    float dx = game_eye[0] - r->last_eye[0], dy = game_eye[1] - r->last_eye[1], dz = game_eye[2] - r->last_eye[2];

    if (r->far <= 0.0f) {
        r->far = 0.5f;
    }
    /* did the game's camera move lately: a size change now is the scripts'
     * fake */
    r->still = dx * dx + dy * dy + dz * dz > 1e-8f ? 0 : r->still + 1;
    r->moving = r->still < BMAP_REST_STILL;
    memcpy(r->last_eye, game_eye, sizeof r->last_eye);
    r->prog = bmap_intro_progress(game_eye);
    r->far_still = r->prog > 0.99f && !r->moving ? r->far_still + 1 : 0;
}

float bmap_rest_sprite(BmapRest *r, int i, float size)
{
    float *rest;

    if (i < 0 || i >= BMAP_REST_MAX) {
        return size;
    }
    rest = &r->rest[i];
    if (size <= 0.0f) {
        *rest = 0.0f;               /* gone: it comes back from nothing */
    } else if (r->moving) {
        *rest = size;
    } else if (*rest <= 0.0f) {
        /* growing from nothing: toward the scripts' size here (a pop above
         * it and back is the script's own) */
        *rest = 1.0f + r->prog * (r->far - 1.0f);
    }
    if (r->far_still > 30 && size > 0.05f && size == *rest) {
        r->far = size;              /* learnt: the size at rest all the way back */
    }
    return *rest;
}

/* A camera moved by the settings `in`, in the part `w` (0 to 1).  The zoom
 * moves the eye by (1 - 1 / zoom) of `toward`, a vector chosen by the
 * caller; the camera then turns about the upright line through the
 * battle's centre, rises, slides to its right and turns its look. */
static void camera_shift(float eye[3], float target[3], const BmapIntro *in, float w, const float toward[3],
                         int keep_look)
{
    const double pi = 3.14159265358979323846;
    float ca, sa, x, zoom = in->zoom > 0.0f ? in->zoom : 1.0f;
    int i;

    if (zoom != 1.0f) {
        float k = w * (1.0f - 1.0f / zoom);     /* below 1: further out */

        for (i = 0; i < 3; i++) {
            float look = target[i] - eye[i];

            eye[i] += k * toward[i];
            if (keep_look) {
                /* the eye's own way still: a target that moves while the
                 * eye is near it (a strong zoom) would swing the view */
                target[i] = eye[i] + look;
            }
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
 * front of the command camera and back.  The camera keeps looking the way
 * the game's does: with a strong zoom the eye ends near the intro's target,
 * and the game's target still moving there swung the view (a jump where
 * the pull ends). */
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
    camera_shift(eye, target, in, w, toward, 1);
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
    camera_shift(eye, target, cam, w, toward, 0);
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

/* The map's placed-object corners in a grid of LOOSE_CELL cubes (a hash of
 * the cube to a chain of vertex indices): loose() looks at the corners near
 * a card's corners only, not at every triangle of the map. */
#define LOOSE_CELL 0.05f
typedef struct CornerGrid {
    unsigned *head;     /* per bucket: the first vertex + 1, 0 none */
    unsigned *next;     /* per vertex: the next in its bucket + 1   */
    unsigned mask;
    unsigned char *moved;   /* per vertex: a stood card's, no longer where the grid has it */
    unsigned *mlist, nm;    /* those vertices                        */
} CornerGrid;

static unsigned corner_bucket(int cx, int cy, int cz, unsigned mask)
{
    return ((unsigned)cx * 73856093u ^ (unsigned)cy * 19349663u ^ (unsigned)cz * 83492791u) & mask;
}

static int corner_cell(float v)
{
    return (int)floorf(v / LOOSE_CELL);
}

static void corner_grid_make(const BmapScene *s, CornerGrid *g)
{
    unsigned n = 1, k;

    while (n < s->nv * 2u) {
        n <<= 1;
    }
    g->mask = n - 1u;
    g->nm = 0;
    g->head = (unsigned *)calloc(n, sizeof *g->head);
    g->next = (unsigned *)calloc(s->nv + 1u, sizeof *g->next);
    g->moved = (unsigned char *)calloc(s->nv + 1u, 1);
    g->mlist = (unsigned *)malloc((s->nv + 1u) * sizeof *g->mlist);
    if (g->head == NULL || g->next == NULL || g->moved == NULL || g->mlist == NULL) {
        free(g->head);
        free(g->next);
        free(g->moved);
        free(g->mlist);
        g->head = g->next = NULL;
        return;
    }
    for (k = 0; k < s->nv; k++) {
        const BmapVertex *v = &s->v[k];
        unsigned b;

        if (v->obj == 0 || (v->obj & 0x80000000u) != 0u) {     /* (a completed body: not a card, nor joined to one) */
            continue;
        }
        b = corner_bucket(corner_cell(v->x), corner_cell(v->y), corner_cell(v->z), g->mask);
        g->next[k] = g->head[b];
        g->head[b] = k + 1u;
    }
}

/* No corner of the card's triangles is a corner of another triangle of its
 * object, in any draw (the map's triangles are whole, three vertices each
 * from the start of the vertex array): the corners within LOOSE_CELL of
 * each of the card's, through the grid (the cubes around it). */
static int loose(const BmapScene *s, const Card *c, const CornerGrid *g)
{
    unsigned obj = s->v[c->tri[0]].obj, i, j, q2;

    for (i = 0; i < c->ntri; i++) {
        for (j = 0; j < 3; j++) {
            const BmapVertex *m = &s->v[c->tri[i] + j];
            int cx = corner_cell(m->x), cy = corner_cell(m->y), cz = corner_cell(m->z), dx, dy, dz;

            for (dx = -1; dx <= 1; dx++) {
                for (dy = -1; dy <= 1; dy++) {
                    for (dz = -1; dz <= 1; dz++) {
                        unsigned at = g->head[corner_bucket(cx + dx, cy + dy, cz + dz, g->mask)];

                        for (; at != 0; at = g->next[at - 1u]) {
                            unsigned k = at - 1u, t = k - k % 3u, q;
                            const BmapVertex *o = &s->v[k];
                            int mine = 0;

                            if (o->obj != obj || t + 2 >= s->nv || g->moved[k]) {
                                continue;
                            }
                            for (q = 0; q < c->ntri && !mine; q++) {
                                mine = c->tri[q] == t;
                            }
                            if (!mine && fabsf(m->x - o->x) < 0.05f && fabsf(m->y - o->y) < 0.05f
                                && fabsf(m->z - o->z) < 0.05f) {
                                return 0;
                            }
                        }
                    }
                }
            }
            for (q2 = 0; q2 < g->nm; q2++) {
                unsigned k = g->mlist[q2], t = k - k % 3u, q;
                const BmapVertex *o = &s->v[k];
                int mine = 0;

                if (o->obj != obj || t + 2 >= s->nv) {
                    continue;
                }
                for (q = 0; q < c->ntri && !mine; q++) {
                    mine = c->tri[q] == t;
                }
                if (!mine && fabsf(m->x - o->x) < 0.05f && fabsf(m->y - o->y) < 0.05f
                    && fabsf(m->z - o->z) < 0.05f) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* A draw whose picture does not move: no texture animation, or one held at a single frame (a live object's, as the
 * field had it: a switch's pad). */
static int still_pic(const BmapScene *s, const BmapDraw *d)
{
    return d->uvanim == 0 || (d->uvanim <= s->nuva && s->uva[d->uvanim - 1].frames <= 1u);
}

static void stand_cards(BmapScene *s)
{
    unsigned i, j, nd0 = s->nd, ncard = 0, cap = 0, nvx = 0, nt_all;
    Card *card = NULL;
    unsigned *pool;
    unsigned char *used;
    unsigned pool_used = 0;
    CornerGrid grid;

    /* room for every triangle of the textured draws once */
    nt_all = s->nv / 3 + 1;
    pool = (unsigned *)malloc(nt_all * sizeof *pool);
    used = (unsigned char *)calloc(nt_all, 1);
    if (pool == NULL || used == NULL) {
        free(pool);
        free(used);
        return;
    }
    corner_grid_make(s, &grid);
    for (i = 0; i < nd0; i++) {
        BmapDraw *d = &s->d[i];
        const BmapTexture *t;
        unsigned first;

        if (d->billboard != 0 || d->count < 3 || d->first + d->count > s->nv || d->tex < 0 || (unsigned)d->tex >= s->nt
            || !still_pic(s, d)) {
            continue;                   /* (an animated sheet: stand_drift, plants_both_sides) */
        }
        t = &s->t[d->tex];
        /* the draw holds every object with this material: one object a run
         * of triangles; in it, the triangles in one plane */
        for (first = d->first; first + 3 <= d->first + d->count;) {
            unsigned end = first + 3, a;

            while (end + 3 <= d->first + d->count && s->v[end].obj == s->v[first].obj) {
                end += 3;
            }
            for (a = first; s->v[first].obj != 0 && (s->v[first].obj & 0x80000000u) == 0 && a < end; a += 3) {
                float n[3], dist, n2[3], d2;
                unsigned b, base = 0;
                Card c;

                if (used[a / 3] || !tri_plane(s, a, n, &dist)) {
                    continue;
                }
                c.ntri = 0;
                /* this triangle's plane mates, gathered at the pool's end */
                base = pool_used;
                c.tri = &pool[base];
                for (b = a; b < end; b += 3) {
                    if (!used[b / 3] && tri_plane(s, b, n2, &d2) && n2[0] * n[0] + n2[1] * n[1] + n2[2] * n[2] > 0.995f
                        && fabsf(d2 - dist) < 0.5f && base + c.ntri < nt_all) {
                        c.tri[c.ntri++] = b;
                    }
                }
                if (c.ntri < 2 || grid.head == NULL || !loose(s, &c, &grid)) {
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
                    pool_used += c.ntri;
                    for (b = 0; grid.head != NULL && b < c.ntri; b++) {
                        unsigned q3;

                        for (q3 = 0; q3 < 3; q3++) {
                            /* stood up: compared where it is now */
                            if (!grid.moved[c.tri[b] + q3]) {
                                grid.moved[c.tri[b] + q3] = 1;
                                grid.mlist[grid.nm++] = c.tri[b] + q3;
                            }
                        }
                    }
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
    free(grid.head);
    free(grid.next);
    free(grid.moved);
    free(grid.mlist);
}

/* Route 1's seasonal plant (a placed model of Routes 1, 2, 3, 17 and 18 and the towns round them: spring's flower
 * spike, summer's flower, autumn's stalk, winter's dry tuft; the pictures r01_<season>_01, each looked at beside
 * its picture).  Rows of flat sheets that lean 24 degrees back from upright and show their south face only: made
 * for the field's camera, in the south.  A battle camera north of a row saw nothing of it.  Both faces are drawn;
 * the sheets stay where and as the map has them (not stood up, not crossed: their picture sways, stand_cards
 * leaves them).  For these plants only, by their pictures' names.  BMAP_PLANTS_ONE_SIDE=1 (research): as before. */
static const char *const k_season_plant_names[] = { "r01_sp_01", "r01_su_01", "r01_au_01", "r01_wi_01" };

static void plants_both_sides(BmapScene *s)
{
    unsigned i;

    if (getenv("BMAP_PLANTS_ONE_SIDE") != NULL) {
        return;
    }
    for (i = 0; i < s->nd; i++) {
        BmapDraw *d = &s->d[i];

        if (d->billboard == 0 && d->cull != 0 && NAMED(s, d, k_season_plant_names)) {
            d->cull = 0;
        }
    }
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

/* (a triangle whose corners turn so that its front looks up: the ground's own winding) */
static int up_tri(const BmapVertex *v)
{
    return (v[1].z - v[0].z) * (v[2].x - v[0].x) - (v[1].x - v[0].x) * (v[2].z - v[0].z) > 0.0f;
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
        if (d->count > 0 && s->v[d->first].obj == 0u) {
            continue;                   /* the ground's own animation (its water, its shore): it lies where it is */
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

static const unsigned *g_sort_root;
static unsigned tree_root(unsigned *par, unsigned a);

static int tri_root_cmp(const void *a, const void *b)
{
    const unsigned ra = g_sort_root[*(const unsigned *)a], rb = g_sort_root[*(const unsigned *)b];

    return ra < rb ? -1 : ra > rb;
}

static int corner_cmp(const void *a, const void *b)
{
    const long long x = *(const long long *)a, y = *(const long long *)b;     /* (the key is the record's first field) */

    return x < y ? -1 : x > y;
}

/* (research) the name of the picture of triangle t's draw */
static const char *decal_name(const BmapScene *s, unsigned t)
{
    unsigned q;

    for (q = 0; q < s->nd; q++) {
        if (t * 3u >= s->d[q].first && t * 3u < s->d[q].first + s->d[q].count) {
            return s->d[q].tex >= 0 && (unsigned)s->d[q].tex < s->nt && s->t[s->d[q].tex].name[0] ? s->t[s->d[q].tex].name : "-";
        }
    }
    return "-";
}

/* Whether one candidate triangle may be laid, and on what: 1 with *best the height of the level surface under its
 * middle (the highest at most DECAL_MAX_H under it) and *water set when that surface is a water sheet; 0 when it
 * stays where the map has it. */
static int decal_target(const BmapScene *s, unsigned t, const unsigned char *textured, const unsigned char *wsheet,
                        const long long *keys, unsigned nk, const unsigned *start, const unsigned *idx, float x0, float z0, int gw,
                        float *best_out, int *water)
{
    const BmapVertex *v = &s->v[t * 3u];
    float cx, cz, y, best = -1e30f;
    unsigned c, j;
    int k, on_water = 0;

    if (v[0].obj & 0x80000000u) {
        /* research: a completed body's own flat pieces (its shadow) keep the height they were made at */
        return 0;
    }
    cx = (v[0].x + v[1].x + v[2].x) / 3.0f;
    cz = (v[0].z + v[1].z + v[2].z) / 3.0f;
    /* the grass's stacked layers stay: their height is their volume.  That is for the tiles where wild Pokemon
     * appear on land; on a water tile (the game's own terrain of the tile) there is no grass, and what the map
     * holds a little over the water (floating leaves, lily pads) is laid on the water like any other picture on
     * its surface: it hung in the air over a pond with wild Pokemon, and lay on the water of a pond without */
    if (bmap_tile_encounter(s, cx, cz) && (bmap_tile(s, cx, cz) != BMAP_TILE_WATER || getenv("BMAP_NO_WATER_LAY") != NULL)) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        const long long key = pos_key(&v[k]);

        if (bsearch(&key, keys, nk, sizeof *keys, key_cmp) != NULL) {
            return 0;                   /* (a corner of a wall or a slope: part of the ground's own shape) */
        }
    }
    y = v[0].y;
    c = (unsigned)((int)((cz - z0) / DECAL_CELL) * gw + (int)((cx - x0) / DECAL_CELL));
    for (j = start[c]; j < start[c + 1]; j++) {
        const BmapVertex *u = &s->v[idx[j] * 3u];

        if (idx[j] != t && u[0].y < y - 0.05f && u[0].y >= y - DECAL_MAX_H && u[0].y > best && in_tri_xz(u, cx, cz)) {
            best = u[0].y;
            on_water = wsheet[idx[j]];
        }
    }
    if (best < -1e29f) {
        return 0;
    }
    if (textured[t] == 2 && (!on_water || getenv("BMAP_NO_WATER_LAY") != NULL)) {
        return 0;                       /* (a moving picture: only onto a water sheet) */
    }
    *best_out = best;
    *water = on_water;
    return 1;
}

static void lay_decals(BmapScene *s, const unsigned char *grass, unsigned ngrass)
{
    unsigned ntri = s->nv / 3, t, nk = 0, i, nlaid = 0, norder = 0, *order = NULL;
    long long *keys = (long long *)malloc((s->nv > 0 ? s->nv : 1) * sizeof *keys);
    unsigned char *level = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
    unsigned char *textured = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
    unsigned char *wsheet = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);    /* a triangle of a water sheet */
    unsigned char *cutc = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);      /* a triangle of a cut-out picture */
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    int gw, gh;
    unsigned *start = NULL, *idx = NULL, *fill = NULL;

    if (keys == NULL || level == NULL || textured == NULL || wsheet == NULL || cutc == NULL) {
        goto out;
    }
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard == 0 && d->tex >= 0 && (unsigned)d->tex < s->nt && d->count >= 3u && d->first % 3 == 0
            && d->first + d->count <= s->nv) {
            /* (a cut-out picture: one with a see-through texel) */
            const BmapTexture *tx = &s->t[d->tex];
            int cut = 0, q;

            for (q = 0; !cut && q < tx->w * tx->h; q++) {
                cut = tx->rgba[q * 4 + 3] == 0;
            }
            for (t = d->first / 3; cut && t < (d->first + d->count) / 3; t++) {
                cutc[t] = 1;
            }
        }

        if (d->billboard == 0 && d->translucent && !still_pic(s, d) && d->count >= 3u && d->first % 3 == 0
            && d->first + d->count <= s->nv && s->v[d->first].obj == 0u) {
            /* (the map's water as it draws it: a see-through, moving sheet of the ground's own mesh) */
            for (t = d->first / 3; t < (d->first + d->count) / 3; t++) {
                wsheet[t] = 1;
            }
        }

        if (d->billboard == 0 && d->tex >= 0 && !d->translucent && still_pic(s, d) && d->first % 3 == 0 && d->first + d->count <= s->nv
            && !(grass != NULL && i < ngrass && grass[i])) {
            for (t = d->first / 3; t < (d->first + d->count) / 3; t++) {
                textured[t] = 1;
            }
        } else if (d->billboard == 0 && d->tex >= 0 && !d->translucent && d->count >= 3u && d->first % 3 == 0
                   && d->first + d->count <= s->nv && !(grass != NULL && i < ngrass && grass[i]) && s->v[d->first].obj == 0u) {
            /* (a moving picture of the ground's own mesh, what floats on a pond: laid only on a water sheet, below) */
            for (t = d->first / 3; t < (d->first + d->count) / 3; t++) {
                textured[t] = 2;
            }
        } else if (getenv("BMAP_NAME_LOG") != NULL && d->tex >= 0 && (unsigned)d->tex < s->nt && d->count >= 3u) {    /* research */
            fprintf(stderr, "bmap: name no-decal %s billboard %d see-through %d moving %d (uvanim %u) grass %d\n", s->t[d->tex].name,
                    d->billboard, d->translucent, !still_pic(s, d), (unsigned)d->uvanim, grass != NULL && i < ngrass && grass[i]);
        }
    }
    for (t = 0; t < ntri; t++) {
        const BmapVertex *v = &s->v[t * 3];
        int k;

        if (v->obj & 0x80000000u) {    /* (a completed body: nothing is laid on it, and it has thousands of faces) */
            level[t] = 0;
            continue;
        }
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
    /* (the candidates, the lowest first) */
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
    {
        /* The sheets: candidates joined by shared corners (the triangles of one flat piece of the mesh: a path's edge
         * strip, a rug).  A sheet of a solid picture is laid as one piece or not at all: it is ground or a floor,
         * and laid a triangle at a time, each on what lay under its own middle, the triangles of one piece ended at
         * different heights and the far ground showed through the slit between them (Route 5's path by the gate).
         * A sheet of a cut-out picture is laid triangle by triangle (below). */
        typedef struct { long long key; unsigned tri; } CornerOf;
        CornerOf *co = (CornerOf *)malloc((norder * 3u + 1u) * sizeof *co);
        unsigned *par = (unsigned *)malloc((ntri > 0 ? ntri : 1) * sizeof *par);
        unsigned *mem = (unsigned *)malloc((norder + 1u) * sizeof *mem);
        unsigned *mstart = (unsigned *)malloc((ntri > 0 ? ntri : 1) * sizeof *mstart);
        unsigned char *done = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
        unsigned q, nco = 0;

        if (co != NULL && par != NULL && mem != NULL && mstart != NULL && done != NULL) {
            for (t = 0; t < ntri; t++) {
                par[t] = t;
                mstart[t] = 0xFFFFFFFFu;
            }
            for (i = 0; i < norder; i++) {
                int k;

                for (k = 0; k < 3; k++) {
                    co[nco].key = pos_key(&s->v[order[i] * 3u + (unsigned)k]);
                    co[nco++].tri = order[i];
                }
            }
            qsort(co, nco, sizeof *co, corner_cmp);
            for (q = 1; q < nco; q++) {
                if (co[q].key == co[q - 1u].key) {
                    par[tree_root(par, co[q].tri)] = tree_root(par, co[q - 1u].tri);
                }
            }
            for (i = 0; i < norder; i++) {
                (void)tree_root(par, order[i]);     /* (every path flat: par[t] is the sheet from here on) */
                mem[i] = order[i];
            }
            for (i = 0; i < norder; i++) {
                par[order[i]] = tree_root(par, order[i]);
            }
            g_sort_root = par;
            qsort(mem, norder, sizeof *mem, tri_root_cmp);
            for (i = 0; i < norder; i++) {
                if (mstart[par[mem[i]]] == 0xFFFFFFFFu) {
                    mstart[par[mem[i]]] = i;
                }
            }
            /* lowest first: a picture laid on another picture finds it laid already */
            for (i = 0; i < norder; i++) {
                const unsigned r = par[order[i]];
                unsigned m0, m1, nok = 0;
                float lo = 1e30f, hi = -1e30f;
                int water = 0;

                if (done[r]) {
                    continue;
                }
                done[r] = 1;
                m0 = mstart[r];
                for (m1 = m0; m1 < norder && par[mem[m1]] == r; m1++) {
                    float best;
                    int w = 0;

                    if (decal_target(s, mem[m1], textured, wsheet, keys, nk, start, idx, x0, z0, gw, &best, &w)) {
                        nok++;
                        lo = fminf(lo, best);
                        hi = fmaxf(hi, best);
                        water |= w;
                    }
                }
                if (cutc[mem[m0]]) {
                    /* a cut-out picture (flowers, blades, the shore's foam, a shadow): each triangle on the ground
                     * under it, as it lies over ground of several heights; a gap between two of them does not show */
                    for (q = m0; q < m1; q++) {
                        float best;
                        int w = 0, k;

                        if (!decal_target(s, mem[q], textured, wsheet, keys, nk, start, idx, x0, z0, gw, &best, &w)) {
                            continue;
                        }
                        if (getenv("BMAP_NAME_LOG") != NULL && w) {     /* research */
                            fprintf(stderr, "bmap: name on-water %s class %d tris 1 from %.1f onto %.1f\n", decal_name(s, mem[q]),
                                    textured[mem[q]], (double)s->v[mem[q] * 3u].y, (double)best);
                        }
                        for (k = 0; k < 3; k++) {
                            s->v[mem[q] * 3u + (unsigned)k].y = best + DECAL_LIFT;
                        }
                        nlaid++;
                    }
                } else if (nok == m1 - m0 && hi - lo <= 0.05f) {
                    if (getenv("BMAP_NAME_LOG") != NULL && water) {     /* research */
                        fprintf(stderr, "bmap: name on-water %s class %d tris %u from %.1f onto %.1f\n", decal_name(s, mem[m0]),
                                textured[mem[m0]], m1 - m0, (double)s->v[mem[m0] * 3u].y, (double)lo);
                    }
                    for (q = m0; q < m1; q++) {
                        int k;

                        for (k = 0; k < 3; k++) {
                            s->v[mem[q] * 3u + (unsigned)k].y = lo + DECAL_LIFT;
                        }
                        nlaid++;
                    }
                } else if (nok > 0u && getenv("BMAP_NAME_LOG") != NULL) {   /* research: a sheet the old rule tore */
                    fprintf(stderr, "bmap: name sheet-kept %s tris %u of them %u had a surface under them (%.1f..%.1f), at %.0f %.1f %.0f\n",
                            decal_name(s, mem[m0]), m1 - m0, nok, (double)lo, (double)hi, (double)s->v[mem[m0] * 3u].x,
                            (double)s->v[mem[m0] * 3u].y, (double)s->v[mem[m0] * 3u].z);
                }
            }
        }
        free(co);
        free(par);
        free(mem);
        free(mstart);
        free(done);
    }
    if (start != NULL && idx != NULL) {
        /* A placed model's floor at the ground's own height (Route 5's gate: the tunnel's floor runs out over the
         * lawn, both at height 0).  The field shows the model's floor there, whole; here the two took the same
         * depth and the lawn came through the floor in stripes.  Each level face of a placed model that looks up,
         * with the map's own solid ground under its middle at its height, is laid over that ground as a picture on
         * the ground is (DECAL_LIFT): its solid faces, and its see-through ones (the gate's shadow, a ring round its
         * floor at the lawn's height: it came through in streaks). */
        unsigned char *solid = (unsigned char *)calloc(ntri > 0 ? ntri : 1, 1);
        unsigned nfloor = 0;

        for (i = 0; solid != NULL && i < s->nd; i++) {
            const BmapDraw *d = &s->d[i];

            if (d->billboard == 0 && d->count >= 3u && d->first % 3 == 0 && d->first + d->count <= s->nv) {
                for (t = d->first / 3; t < (d->first + d->count) / 3; t++) {
                    solid[t] = !d->translucent && d->alpha >= 31u ? 1 : 2;     /* (2: see-through) */
                }
            }
        }
        for (t = 0; solid != NULL && t < ntri; t++) {
            BmapVertex *v = &s->v[t * 3];
            const float cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f;
            unsigned c, j;
            int k, on = 0;

            if (!level[t] || !solid[t] || (v[0].obj & 0x3FFFFFFFu) == 0u || (v[0].obj & 0x80000000u) || !up_tri(v)) {
                continue;
            }
            c = (unsigned)((int)((cz - z0) / DECAL_CELL) * gw + (int)((cx - x0) / DECAL_CELL));
            for (j = start[c]; !on && j < start[c + 1]; j++) {
                const BmapVertex *u = &s->v[idx[j] * 3u];

                on = idx[j] != t && solid[idx[j]] == 1 && u[0].obj == 0u && fabsf(u[0].y - v[0].y) <= 0.05f && up_tri(u)
                     && in_tri_xz(u, cx, cz);
            }
            if (on) {
                for (k = 0; k < 3; k++) {
                    v[k].y += DECAL_LIFT;
                }
                nfloor++;
            }
        }
        if (nfloor > 0u && getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: %u floor triangles of placed models at the ground's own height laid over it\n", nfloor);
        }
        free(solid);
    }
    (void)nlaid;
    if (getenv("BMAP_NAME_LOG") != NULL) {
        /* research: what still lies a little over a water tile's surface (0.5 to 6 units over the game's height
         * of the tile), by its picture's name */
        for (t = 0; t < ntri; t++) {
            const BmapVertex *v = &s->v[t * 3];
            const float mx = (v[0].x + v[1].x + v[2].x) / 3.0f, mz = (v[0].z + v[1].z + v[2].z) / 3.0f;
            float ty;
            unsigned q;

            if (!level[t] || (v[0].obj & 0x80000000u) || bmap_tile(s, mx, mz) != BMAP_TILE_WATER || !bmap_tile_height(s, mx, mz, &ty)
                || v[0].y < ty + 0.5f || v[0].y > ty + DECAL_MAX_H) {
                continue;
            }
            for (q = 0; q < s->nd; q++) {
                if (t * 3u >= s->d[q].first && t * 3u < s->d[q].first + s->d[q].count) {
                    fprintf(stderr, "bmap: name over-water %s %.1f over, see-through %d object %d candidate %d\n",
                            s->d[q].tex >= 0 && (unsigned)s->d[q].tex < s->nt && s->t[s->d[q].tex].name[0] ? s->t[s->d[q].tex].name : "-",
                            (double)(v[0].y - ty), s->d[q].translucent, (v[0].obj & 0x3FFFFFFFu) != 0u, textured[t]);
                    break;
                }
            }
        }
    }
out:
    free(keys);
    free(level);
    free(textured);
    free(wsheet);
    free(cutc);
    free(start);
    free(fill);
    free(idx);
    free(order);
}

/*
 * research: a completed body that rests on the floor (a completion piece of
 * kind 2: bit 30 of its corners' object number).  The map holds its flat
 * picture at some height over the floor, another in every room; the body is
 * made from that picture and so hangs as high.  Each such body (the pieces of
 * one object) is moved down, or up, as a whole until its lowest corner is on
 * the highest level surface of the map under its middle: one at most
 * REST_MAX_H under its foot, and under its top.  In map units, before the
 * placement.
 */
#define REST_MAX_H 10.0f

static void rest_bodies(BmapScene *s)
{
    typedef struct { unsigned obj; float lo, hi, x0, x1, z0, z1, best; int sheet; } Body;
    Body *b = NULL;
    unsigned nb = 0, cap = 0, i, j, last = 0;

    for (i = 0; i < s->nv; i++) {
        const BmapVertex *v = &s->v[i];

        if ((v->obj & 0xC0000000u) != 0xC0000000u) {
            continue;
        }
        if (nb == 0 || b[last].obj != v->obj) {
            for (j = 0; j < nb && b[j].obj != v->obj; j++) {
            }
            if (j == nb) {
                if (nb == cap) {
                    Body *nw = (Body *)realloc(b, (cap = cap ? cap * 2 : 16) * sizeof *b);

                    if (nw == NULL) {
                        free(b);
                        return;
                    }
                    b = nw;
                }
                b[nb].obj = v->obj;
                b[nb].lo = b[nb].x0 = b[nb].z0 = 1e30f;
                b[nb].hi = b[nb].x1 = b[nb].z1 = b[nb].best = -1e30f;
                b[nb].sheet = 0;        /* (it was left as the memory held it: a body lifted a hair over the floor or not, by chance) */
                nb++;
            }
            last = j;
        }
        b[last].lo = fminf(b[last].lo, v->y);
        b[last].hi = fmaxf(b[last].hi, v->y);
        b[last].x0 = fminf(b[last].x0, v->x);
        b[last].x1 = fmaxf(b[last].x1, v->x);
        b[last].z0 = fminf(b[last].z0, v->z);
        b[last].z1 = fmaxf(b[last].z1, v->z);
    }
    if (nb == 0) {
        free(b);
        return;
    }
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];
        unsigned t;

        if (d->billboard || d->translucent || d->first + d->count > s->nv) {
            continue;                   /* (a picture, a see-through sheet, a shadow: nothing rests on it) */
        }
        for (t = d->first; t + 2 < d->first + d->count; t += 3) {
            const BmapVertex *u = &s->v[t];

            if ((u->obj & 0x80000000u) || !level_tri(u)) {
                continue;
            }
            for (j = 0; j < nb; j++) {
                float cx = (b[j].x0 + b[j].x1) / 2.0f, cz = (b[j].z0 + b[j].z1) / 2.0f;

                if (u->y > b[j].best && u->y < b[j].hi - 0.05f && u->y >= b[j].lo - REST_MAX_H && in_tri_xz(u, cx, cz)) {
                    b[j].best = u->y;
                }
            }
        }
    }
    /* a body whose foot is a level sheet (a table's shadow on the floor): it lies a hair over the floor */
    last = 0;
    for (i = 0; i + 2 < s->nv; i += 3) {
        const BmapVertex *v = &s->v[i];

        if ((v->obj & 0xC0000000u) != 0xC0000000u || !level_tri(v)) {
            continue;
        }
        if (b[last].obj != v->obj) {
            for (last = 0; last < nb && b[last].obj != v->obj; last++) {
            }
            if (last == nb) {
                last = 0;
                continue;
            }
        }
        if (v->y < b[last].lo + 0.01f) {
            b[last].sheet = 1;
        }
    }
    last = 0;
    for (i = 0; i < s->nv; i++) {
        BmapVertex *v = &s->v[i];

        if ((v->obj & 0xC0000000u) != 0xC0000000u) {
            continue;
        }
        if (b[last].obj != v->obj) {
            for (last = 0; b[last].obj != v->obj; last++) {
            }
        }
        if (b[last].best > -1e29f) {
            /* (a flat sheet lies a hair over the floor, as a floor picture does, so it still draws over it) */
            v->y += b[last].best - b[last].lo + (b[last].sheet || b[last].hi - b[last].lo < 0.01f ? DECAL_LIFT : 0.0f);
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        for (j = 0; j < nb; j++) {
            fprintf(stderr, "bmap: resting body %u: foot %.2f, floor %.2f\n", b[j].obj & 0x3FFFFFFFu, b[j].lo, b[j].best);
        }
    }
    free(b);
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

/* White Forest's great trees (a placed model: a block of the forest with its own floor).  The model's floor lies 8
 * units under the height the game walks the player at; its trees' roots begin 4 over that floor and their shadow
 * lies 2 over it, under the roots.  The field's steep camera never shows the difference; a battle stands on the
 * drawn floor and sees the roots hang in the air over their own shadow.  So each block's trees (every triangle of
 * its tree pictures but the shadow) go down by what its lowest root corner stands over the floor under it, and the
 * shadow (the see-through level triangles of those pictures near the floor) is laid on the floor, at the height
 * the battle lays a thing's shadow (DECAL_LIFT + 0.15).  Nothing moves where the gap is over FOREST_GAP. */
static const char *const k_forest_tree_names[] = { "wf_tree_a", "wf_tree_a2", "wf_tree_b", "wf_tree_c", "wf_tree_k" };
#define FOREST_GAP 8.0f

static int forest_shadow_tri(const BmapScene *s, const BmapDraw *d, const BmapVertex *v, float *floor_y)
{
    int hit = 0;
    float fl;

    if (!d->translucent || !level_tri(v)) {
        return 0;
    }
    fl = bmap_floor_below(s, (v[0].x + v[1].x + v[2].x) / 3.0f, (v[0].z + v[1].z + v[2].z) / 3.0f, v[0].y - 1.0f, &hit);
    if (!hit || v[0].y - fl > FOREST_GAP) {
        return 0;
    }
    *floor_y = fl;
    return 1;
}

static void forest_set_down(BmapScene *s)
{
    struct ForestTree {
        unsigned obj;
        float lo, x, z, gap;
    } *tr = NULL;
    unsigned n = 0, cap = 0, i, j, k, q, moved = 0, laid = 0;

    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (!NAMED(s, d, k_forest_tree_names) || d->billboard != 0 || d->count < 3 || d->first % 3 != 0 || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            const BmapVertex *v = &s->v[j];
            float fl;

            if (v->obj == 0u || (v->obj & 0x80000000u) != 0u || forest_shadow_tri(s, d, v, &fl)) {
                continue;
            }
            for (q = 0; q < n && tr[q].obj != v->obj; q++) {
            }
            if (q == n) {
                if (n == cap) {
                    const unsigned nc = cap ? cap * 2u : 32u;
                    struct ForestTree *nt = (struct ForestTree *)realloc(tr, nc * sizeof *nt);

                    if (nt == NULL) {
                        free(tr);
                        return;
                    }
                    tr = nt;
                    cap = nc;
                }
                tr[n].obj = v->obj;
                tr[n].lo = 1e30f;
                tr[n].x = tr[n].z = tr[n].gap = 0.0f;
                n++;
            }
            for (k = 0; k < 3; k++) {
                if (v[k].y < tr[q].lo) {
                    tr[q].lo = v[k].y;
                    tr[q].x = v[k].x;
                    tr[q].z = v[k].z;
                }
            }
        }
    }
    for (q = 0; q < n; q++) {
        int hit = 0;
        const float fl = bmap_floor_below(s, tr[q].x, tr[q].z, tr[q].lo - 1.0f, &hit);

        tr[q].gap = hit && tr[q].lo - fl > 0.05f && tr[q].lo - fl <= FOREST_GAP ? tr[q].lo - fl : 0.0f;
    }
    for (i = 0; n > 0 && i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (!NAMED(s, d, k_forest_tree_names) || d->billboard != 0 || d->count < 3 || d->first % 3 != 0 || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            BmapVertex *v = &s->v[j];
            float fl = 0.0f;

            if (v->obj == 0u || (v->obj & 0x80000000u) != 0u) {
                continue;
            }
            if (forest_shadow_tri(s, d, v, &fl)) {
                for (k = 0; k < 3; k++) {
                    v[k].y = fl + DECAL_LIFT + 0.15f;
                }
                laid++;
                continue;
            }
            for (q = 0; q < n && tr[q].obj != v->obj; q++) {
            }
            if (q < n && tr[q].gap > 0.0f) {
                for (k = 0; k < 3; k++) {
                    v[k].y -= tr[q].gap;
                }
                moved++;
            }
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL && n > 0) {
        fprintf(stderr, "bmap: forest: %u blocks of great trees, %u triangles set down, %u shadow triangles laid on the floor\n", n, moved, laid);
        for (q = 0; q < n && q < 4; q++) {
            fprintf(stderr, "bmap: forest: block %08X lowest corner %.2f, set down by %.2f\n", tr[q].obj, (double)tr[q].lo, (double)tr[q].gap);
        }
    }
    free(tr);
}

void bmap_place_prepare(BmapScene *s, int field)
{
    unsigned i, nd0, ntri;
    unsigned char *grass, *tmask;

    if (s == NULL) {
        return;
    }
    /* the same map for the battle and the editor's free field view */
    clock_t t0 = clock(), t1, t2;       /* research: where the time goes */

    if (!field) {
        forest_set_down(s);             /* (before anything reads where a thing stands) */
    }
    stand_cards(s);
    t1 = clock();
    if (!field) {
        plants_both_sides(s);
        drop_cave_light(s);
        fix_backs(s);                   /* research: before the search, which then keeps clear of them */
    }
    t2 = clock();
    nd0 = s->nd;
    ntri = s->nv / 3 + 1;
    free(s->prep);
    s->prep = (unsigned char *)calloc(nd0 + ntri + 1u, 1);
    s->prep_nd0 = nd0;
    s->prep_ntri = s->prep != NULL ? ntri : 0;
    s->prepared = field ? 2 : 1;
    grass = s->prep;
    /* The grass: each level triangle of a cut-out sheet a little above the
     * solid ground right under it, leafy or over the tiles where wild
     * Pokemon appear (dark grass is not green enough to tell by its colour;
     * a rug never lies on those tiles; the soft shadow sheet under the
     * leaves is translucent).  Triangle by triangle: one draw holds the
     * grass of every height (a hill's terraces).  Laid on the ground, and
     * the tufts grow from them (the triangle marks). */
    tmask = s->prep != NULL ? s->prep + nd0 : NULL;
    for (i = 0; tmask != NULL && !field && i < s->nd; i++) {
        BmapDraw *d = &s->d[i];
        const BmapTexture *t = d->tex >= 0 && (unsigned)d->tex < s->nt ? &s->t[d->tex] : NULL;
        unsigned j;
        int cut = d->translucent != 0, lf;

        /* (a sheet with a hand-made kind; White Forest's and the long grass's lid are grass by their kind alone) */
        const int glh = t != NULL && bmap_grass_find(t->name) != NULL;
        const int wfg = glh && strcmp(t->name, "kusa1_wf") == 0;
        const int lgt = glh && (strcmp(t->name, "nagakusa01a") == 0 || strcmp(t->name, "nagakusa02a") == 0);

        if (t != NULL && strcmp(t->name, "kusa1_wf") == 0 && d->billboard == 0 && d->count >= 3 && d->first % 3 == 0
            && d->first + d->count <= s->nv) {
            /* White Forest's grass: the game draws the forest floor (a placed model) 8 units under the height it
             * walks the player at, and the grass sheet at that walk height.  A battle stands on the drawn floor,
             * so each level triangle of the sheet is set down onto the floor under it (within GRASS_MAX_H): the
             * grass lies on the ground the Pokemon stand on, as a flat sheet or as what grows from it. */
            for (j = d->first; j + 2 < d->first + d->count; j += 3) {
                BmapVertex *v = &s->v[j];
                const float y0 = v[0].y, cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f;
                int hit = 0, k2;
                float fl;

                if (fabsf(v[1].y - y0) > 0.01f || fabsf(v[2].y - y0) > 0.01f) {
                    continue;
                }
                fl = bmap_floor_below(s, cx, cz, y0 - 1.0f, &hit);
                if (hit && y0 - fl <= GRASS_MAX_H) {
                    for (k2 = 0; k2 < 3; k2++) {
                        v[k2].y = fl + 0.05f;
                    }
                }
            }
        }
        if (d->billboard != 0 || d->count < 3 || d->first % 3 != 0 || d->first + d->count > s->nv || t == NULL
            || (s->v[d->first].obj != 0 && !wfg)) {
            continue;                   /* a placed object (a gym button's leaf) stays */
        }
        if (t != NULL && bmap_grass_hidden(t->name)) {
            for (j = d->first; j + 2 < d->first + d->count; j += 3) {      /* (a fringe the hand-made grass replaces) */
                s->v[j + 1] = s->v[j];
                s->v[j + 2] = s->v[j];
            }
            continue;
        }
        for (j = 0; !cut && j < (unsigned)(t->w * t->h); j++) {
            cut = t->rgba[j * 4 + 3] == 0;
        }
        cut |= wfg || lgt;
        if (getenv("BMAP_ENC_LOG") != NULL) {
            /* research: a level picture lying on a tile where wild Pokemon appear, once a draw: its name, whether it
             * has see-through pixels, how high over the ground under it, how many of its triangles lie there */
            unsigned on = 0;
            float hmax = -1e30f;

            for (j = d->first; j + 2 < d->first + d->count; j += 3) {
                const BmapVertex *v = &s->v[j];
                const float cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f;
                int hit = 0;
                float g;

                if (fabsf(v[1].y - v[0].y) > 0.01f || fabsf(v[2].y - v[0].y) > 0.01f || !bmap_tile_encounter(s, cx, cz)) {
                    continue;
                }
                g = bmap_ground_below(s, cx, cz, v[0].y - 0.6f, &hit);
                on++;
                hmax = hit && v[0].y - g > hmax ? v[0].y - g : hmax;
            }
            if (on > 0) {
                fprintf(stderr, "bmap: enc %s cut %d over %.1f tris %u\n", t->name[0] ? t->name : "-", cut, (double)hmax, on);
            }
        }
        if (!cut) {
            continue;
        }
        lf = leafy(s, d);
        for (j = d->first; j + 2 < d->first + d->count; j += 3) {
            BmapVertex *v = &s->v[j];
            float y0 = v[0].y, cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f, g, dh;
            int hit, k2;

            if (fabsf(v[1].y - y0) > 0.01f || fabsf(v[2].y - y0) > 0.01f || (v[0].obj != 0 && !wfg)) {
                continue;
            }
            /* (a White Forest sheet's ground is the forest floor, a placed model: it was set down onto it above) */
            g = wfg ? bmap_floor_below(s, cx, cz, y0 + 0.05f, &hit)
                    : bmap_ground_below(s, cx, cz, lgt ? y0 - 0.6f : y0 - 0.01f, &hit);
            if (wfg && (!hit || y0 - g > GRASS_MAX_H)) {
                /* (White Forest's floor is a placed model too, no ground of the map: the grass lies on the walk
                 * tile's own height, the map's ground 44 under it) */
                float pl[2];
                const int npl = bmap_tile_planes(s, cx, cz, pl);

                for (k2 = 0; k2 < npl; k2++) {
                    if (fabsf(pl[k2] - y0) < 2.0f) {
                        g = y0;
                        hit = 1;
                    }
                }
            }
            if (!hit) {
                continue;
            }
            dh = y0 - g;
            if ((wfg ? dh < -0.05f : dh <= 0.0f) || dh > GRASS_MAX_H) {
                continue;
            }
            dh = dh < 0.0f ? 0.0f : dh;
            if (!lf && !wfg && !lgt && (d->translucent || !bmap_tile_encounter(s, cx, cz))) {
                continue;
            }
            {
                /* a sheet the game walks the player on (a walk tile's height at it) is a floor, not grass: Route
                 * 7's plank footbridges over the long grass, whose gaps between the planks are see-through */
                float pl[2];
                const int npl = bmap_tile_planes(s, cx, cz, pl);
                int walked = 0;

                /* (never a sheet named as grass: White Forest's grass lies at the walk tile's height, the map's
                 * ground 2 under it) */
                for (k2 = 0; !lf && !wfg && !lgt && k2 < npl && !walked; k2++) {
                    walked = fabsf(pl[k2] - y0) < 2.0f && fabsf(pl[k2] - g) >= 2.0f;
                }
                if (walked) {
                    continue;
                }
            }
            if (getenv("BMAP_NAME_LOG") != NULL && (grass == NULL || !grass[i])) {     /* research */
                fprintf(stderr, "bmap: name grass %s leafy %d encounter %d see-through %d\n", t->name[0] ? t->name : "-", lf,
                        bmap_tile_encounter(s, cx, cz), d->translucent != 0);
            }
            tmask[j / 3] = 1;
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
    {
        clock_t t3 = clock();

        lay_decals(s, grass, nd0);
        rest_bodies(s);
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: prepare: cards %.0f, light and backs %.0f, grass %.0f, decals %.0f ms\n",
                    1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC, 1000.0 * (double)(t2 - t1) / CLOCKS_PER_SEC,
                    1000.0 * (double)(t3 - t2) / CLOCKS_PER_SEC, 1000.0 * (double)(clock() - t3) / CLOCKS_PER_SEC);
        }
    }
}

/* Research (BMAP_CAM_YAW="command,intro" degrees): the battle's cameras
 * turned about the battle's centre, added to the row's own turn (a camera
 * on the other side of the battle line: about -40,-51). */
static BmapPlaceSpec research_spec(const BmapPlaceSpec *sp0)
{
    BmapPlaceSpec sp = *sp0;
    const char *e = getenv("BMAP_CAM_YAW");
    float c = 0.0f, i = 0.0f;

    if (e != NULL && sscanf(e, "%f,%f", &c, &i) >= 1) {
        sp.cam.yaw += c;
        sp.intro.yaw += i;
    }
    return sp;
}

/* The trees of the placed scene, for the camera's clearance (battle_view.c).
 *
 * The scene's triangles are joined into pieces: a placed object (BmapVertex.obj) is one piece; the map's own mesh
 * falls into the pieces its triangles make by shared corners within one draw.  Most outdoor maps build their trees
 * into the ground chunk, and a forest's tree is not one body: upright picture cards (32 or 64 map units wide), flat
 * leaf layers over them at two or three heights, a shadow on the ground under them, each a piece of its own.
 *
 * The foliage pieces: a piece with a green picture, no wider than 24 battle units, that is cut out of a card
 * (see-through pixels), a placed object, or a body at least 2 high (not a lawn patch).  Foliage pieces that stand in
 * one another's place (their boxes on the ground overlap by a quarter of the smaller one) are one tree: a card with
 * the layers over it, a crown with its trunk.  A tree at least 2 high and no wider than 44 gets a tag (1..254, its
 * number round again past that), written into the alpha byte of the vertex colour of every one of its triangles
 * (the renderers never read that byte as a colour), and an entry of BmapScene.trees.  The clearance then leaves out
 * the triangles with the tags of the trees it reaches: nothing else is cut, whatever stands in the same place.
 * A forest's long wall of joined cards (one piece over a thousand units wide) and its wide canopy sheets are split
 * into their panels first, each a foliage piece of its own (up to 40 wide), so the panel the camera stands in goes
 * without the rest of the wall; a tree may then be up to 44 wide. */
static unsigned tree_root(unsigned *par, unsigned a)
{
    while (par[a] != a) {
        par[a] = par[par[a]];
        a = par[a];
    }
    return a;
}

static void find_trees(BmapScene *s)
{
    typedef struct { float lo[3], hi[3]; unsigned char leaf, cut, isobj, cand, tag, shade, wide, canopy; } Pc;
    unsigned *par, *slot, *objv, *gpar, *cidx, cap = 1u, i, j, top = 0, npc = 0, npc0, pcap, nc = 0, n = 0;
    int *pcof, *vpc;
    Pc *pc;
    signed char *green, *cutout, *dark;

    free(s->trees);
    s->trees = NULL;
    s->n_trees = 0;
    for (i = 0; i < s->nv; i++) {
        s->v[i].a = 0;                  /* (no tree) */
    }
    if (s->nv < 3u) {
        return;
    }
    while (cap < 2u * s->nv) {
        cap <<= 1;
    }
    for (i = 0; i < s->nv; i++) {
        const unsigned o = s->v[i].obj & 0x3FFFFFFFu;

        top = o > top && o < 0x100000u ? o : top;
    }
    par = (unsigned *)malloc((size_t)s->nv * sizeof *par);
    slot = (unsigned *)malloc((size_t)cap * sizeof *slot);
    objv = (unsigned *)malloc(((size_t)top + 1u) * sizeof *objv);
    pcof = (int *)malloc((size_t)s->nv * sizeof *pcof);
    green = (signed char *)calloc(s->nt > 0 ? s->nt : 1u, 1);
    cutout = (signed char *)calloc(s->nt > 0 ? s->nt : 1u, 1);
    dark = (signed char *)calloc(s->nt > 0 ? s->nt : 1u, 1);
    if (par == NULL || slot == NULL || objv == NULL || pcof == NULL || green == NULL || cutout == NULL || dark == NULL) {
        free(par); free(slot); free(objv); free(pcof); free(green); free(cutout); free(dark);
        return;
    }
    memset(slot, 0xFF, (size_t)cap * sizeof *slot);
    memset(objv, 0xFF, ((size_t)top + 1u) * sizeof *objv);
    for (i = 0; i < s->nv; i++) {
        par[i] = i;
        pcof[i] = -1;
    }
    /* each corner joined to the first corner at its place in its draw (the map's mesh) or to its object's first */
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j < d->first + d->count; j++) {
            const BmapVertex *v = &s->v[j];
            const unsigned o = v->obj & 0x3FFFFFFFu;

            if (o != 0u && o <= top) {
                if (objv[o] == 0xFFFFFFFFu) {
                    objv[o] = j;
                }
                par[tree_root(par, j)] = tree_root(par, objv[o]);
            } else {
                const int qx = (int)lroundf(v->x * 32.0f), qy = (int)lroundf(v->y * 32.0f), qz = (int)lroundf(v->z * 32.0f);
                unsigned h = ((unsigned)qx * 73856093u ^ (unsigned)qy * 19349663u ^ (unsigned)qz * 83492791u ^ i * 2654435761u) & (cap - 1u);

                for (;;) {
                    const unsigned k = slot[h];

                    if (k == 0xFFFFFFFFu) {
                        slot[h] = j;
                        break;
                    }
                    if (k >= d->first && k < d->first + d->count && (s->v[k].obj & 0x3FFFFFFFu) == 0u
                        && (int)lroundf(s->v[k].x * 32.0f) == qx && (int)lroundf(s->v[k].y * 32.0f) == qy
                        && (int)lroundf(s->v[k].z * 32.0f) == qz) {
                        par[tree_root(par, j)] = tree_root(par, k);
                        break;
                    }
                    h = (h + 1u) & (cap - 1u);
                }
            }
            if ((j - d->first) % 3u != 0u) {
                par[tree_root(par, j)] = tree_root(par, j - (j - d->first) % 3u);   /* (its triangle's first corner) */
            }
        }
    }
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j < d->first + d->count; j++) {
            const unsigned r = tree_root(par, j);

            if (pcof[r] < 0) {
                pcof[r] = (int)npc++;
            }
        }
    }
    npc0 = npc;
    pcap = npc + s->nv / 3u + 1u;       /* (room for the panels the wide pieces are split into) */
    pc = (Pc *)calloc(pcap, sizeof *pc);
    gpar = (unsigned *)malloc((size_t)pcap * sizeof *gpar);
    cidx = (unsigned *)malloc((size_t)pcap * sizeof *cidx);
    vpc = (int *)malloc((size_t)s->nv * sizeof *vpc);
    if (pc == NULL || gpar == NULL || cidx == NULL || vpc == NULL) {
        free(par); free(slot); free(objv); free(pcof); free(green); free(cutout); free(pc); free(gpar); free(cidx);
        free(vpc); free(dark);
        return;
    }
    for (i = 0; i < pcap; i++) {
        pc[i].lo[0] = pc[i].lo[1] = pc[i].lo[2] = 1e30f;
        pc[i].hi[0] = pc[i].hi[1] = pc[i].hi[2] = -1e30f;
        gpar[i] = i;
    }
    for (i = 0; i < s->nv; i++) {
        vpc[i] = -1;
    }
    /* the pieces: their boxes, their foliage */
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];
        int leaf = 0, cut = 0, shade = 0;

        if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
            continue;
        }
        if (d->tex >= 0 && (unsigned)d->tex < s->nt) {
            if (green[d->tex] == 0) {
                const BmapTexture *t = &s->t[d->tex];
                unsigned long clear = 0;
                int q;

                /* (a tree's picture and a tree's shadow by their names; whether the picture has see-through
                 * texels is counted: over 2 in 100) */
                green[d->tex] = !d->translucent && NAMED(s, d, k_tree_names) ? 1 : -1;
                for (q = 0; q < t->w * t->h; q++) {
                    clear += t->rgba[q * 4 + 3] < 128;
                }
                cutout[d->tex] = clear * 50ul > (unsigned long)(t->w * t->h) ? 1 : -1;
                dark[d->tex] = NAMED(s, d, k_tree_shadow_names) ? 1 : -1;
                if (getenv("BMAP_NAME_LOG") != NULL) {  /* research: what the colour tests take, by name */
                    fprintf(stderr, "bmap: name tree %s green %d cutout %d dark %d object %d (picture %d, %dx%d, draw %u)\n",
                            t->name[0] ? t->name : "-", green[d->tex] > 0, cutout[d->tex] > 0, dark[d->tex] > 0,
                            (s->v[d->first].obj & 0x3FFFFFFFu) != 0u, d->tex, t->w, t->h, i);
                }
            }
            leaf = green[d->tex] > 0;
            cut = cutout[d->tex] > 0;
            shade = dark[d->tex] > 0;
        }
        for (j = d->first; j < d->first + d->count; j++) {
            const BmapVertex *v = &s->v[j];
            Pc *b = &pc[pcof[tree_root(par, j)]];

            vpc[j] = pcof[tree_root(par, j)];
            if (v->y <= -900.0f) {
                continue;
            }
            b->lo[0] = fminf(b->lo[0], v->x); b->hi[0] = fmaxf(b->hi[0], v->x);
            b->lo[1] = fminf(b->lo[1], v->y); b->hi[1] = fmaxf(b->hi[1], v->y);
            b->lo[2] = fminf(b->lo[2], v->z); b->hi[2] = fmaxf(b->hi[2], v->z);
            b->leaf |= (unsigned char)leaf;
            b->cut |= (unsigned char)(leaf && cut);
            b->isobj |= (unsigned char)((v->obj & 0x3FFFFFFFu) != 0u);
            b->shade |= (unsigned char)shade;
        }
    }
    /* A wide piece of foliage cut out of cards (a forest's wall of joined pictures, its canopy sheets: wider than 24)
     * is split into its panels: a triangle and the next one of its draw when they share two corners (a card's two
     * halves), else the triangle alone.  Each panel a piece of its own from here on. */
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j + 2u < d->first + d->count; j += 3u) {
            const int pi = vpc[j];
            const Pc *b;
            unsigned np, q;

            if (pi < 0 || (unsigned)pi >= npc0) {
                continue;               /* (a panel already) */
            }
            b = &pc[pi];
            if (!b->cut || b->isobj || fmaxf(b->hi[0] - b->lo[0], b->hi[2] - b->lo[2]) <= 24.0f || npc >= pcap) {
                continue;
            }
            np = npc++;
            pc[np].leaf = 1;
            pc[np].cut = 1;
            for (q = 0; q < 3u; q++) {
                vpc[j + q] = (int)np;
            }
            if (j + 5u < d->first + d->count && vpc[j + 3u] == pi) {
                unsigned shared = 0, a, c;

                for (a = 0; a < 3u; a++) {
                    for (c = 0; c < 3u; c++) {
                        shared += fabsf(s->v[j + a].x - s->v[j + 3u + c].x) < 0.03f
                                  && fabsf(s->v[j + a].y - s->v[j + 3u + c].y) < 0.03f
                                  && fabsf(s->v[j + a].z - s->v[j + 3u + c].z) < 0.03f;
                    }
                }
                if (shared >= 2u) {
                    for (q = 3u; q < 6u; q++) {
                        vpc[j + q] = (int)np;
                    }
                }
            }
        }
    }
    for (i = 0; i < s->nv; i++) {
        if (vpc[i] >= (int)npc0 && s->v[i].y > -900.0f) {
            Pc *b = &pc[vpc[i]];
            const BmapVertex *v = &s->v[i];

            b->lo[0] = fminf(b->lo[0], v->x); b->hi[0] = fmaxf(b->hi[0], v->x);
            b->lo[1] = fminf(b->lo[1], v->y); b->hi[1] = fmaxf(b->hi[1], v->y);
            b->lo[2] = fminf(b->lo[2], v->z); b->hi[2] = fmaxf(b->hi[2], v->z);
        }
    }
    /* the foliage pieces, and those that stand in one another's place joined into trees */
    for (i = 0; i < npc; i++) {
        Pc *b = &pc[i];

        const float wd = fmaxf(b->hi[0] - b->lo[0], b->hi[2] - b->lo[2]);

        /* (a panel of a split wall may be of any length: a wide one, over 40, is a tree of its own when it stands
         * upright, and the canopy when it lies level) */
        b->cand = b->leaf && b->hi[0] >= b->lo[0] && wd <= (i >= npc0 ? 1e9f : 24.0f)
                  && (b->cut || b->isobj || b->hi[1] - b->lo[1] >= 2.0f);
        b->wide = (unsigned char)(b->cand && i >= npc0 && wd > 40.0f);
        if (b->wide && b->hi[1] - b->lo[1] < 2.0f) {
            b->cand = 0;
            b->wide = 0;
            b->canopy = 1;
        }
        /* (a tree's shadow: a flat dark sheet cut out of a card, no green in it; it joins the tree over it and
         * makes no tree alone) */
        if (!b->cand && b->shade && !b->leaf && b->hi[0] >= b->lo[0] && b->hi[1] - b->lo[1] < 0.5f
            && fmaxf(b->hi[0] - b->lo[0], b->hi[2] - b->lo[2]) <= 24.0f) {
            b->cand = 1;
        }
        if (b->cand) {
            cidx[nc++] = i;
        }
    }
    for (i = 0; i < nc; i++) {
        const Pc *a = &pc[cidx[i]];
        const float ax0 = a->lo[0] - 0.25f, ax1 = a->hi[0] + 0.25f, az0 = a->lo[2] - 0.25f, az1 = a->hi[2] + 0.25f;

        for (j = i + 1u; j < nc; j++) {
            const Pc *b = &pc[cidx[j]];
            const float bx0 = b->lo[0] - 0.25f, bx1 = b->hi[0] + 0.25f, bz0 = b->lo[2] - 0.25f, bz1 = b->hi[2] + 0.25f;
            const float ox = fminf(ax1, bx1) - fmaxf(ax0, bx0), oz = fminf(az1, bz1) - fmaxf(az0, bz0);

            if (!a->wide && !b->wide && ox > 0.0f && oz > 0.0f
                && ox * oz >= 0.25f * fminf((ax1 - ax0) * (az1 - az0), (bx1 - bx0) * (bz1 - bz0))) {
                /* (joined while the tree they make stays no wider than 44: a wall's panel, the sheets over it, the
                 * next panel and so on would chain a whole forest's edge into one; each tree's box is kept on its
                 * first piece) */
                const unsigned ra = tree_root(gpar, cidx[i]), rb = tree_root(gpar, cidx[j]);
                Pc *ga = &pc[ra];
                const Pc *gb = &pc[rb];

                if (ra != rb && fmaxf(ga->hi[0], gb->hi[0]) - fminf(ga->lo[0], gb->lo[0]) <= 44.0f
                    && fmaxf(ga->hi[2], gb->hi[2]) - fminf(ga->lo[2], gb->lo[2]) <= 44.0f) {
                    int k;

                    for (k = 0; k < 3; k++) {
                        ga->lo[k] = fminf(ga->lo[k], gb->lo[k]);
                        ga->hi[k] = fmaxf(ga->hi[k], gb->hi[k]);
                    }
                    ga->leaf |= gb->leaf;       /* (the tree has foliage when one of its pieces has) */
                    gpar[rb] = ra;
                }
            }
        }
    }
    for (i = 0; i < nc; i++) {
        n += tree_root(gpar, cidx[i]) == cidx[i];
    }
    s->trees = n > 0u ? (float (*)[8])malloc((size_t)n * sizeof *s->trees) : NULL;
    for (i = 0; i < nc && s->trees != NULL; i++) {
        Pc *g = &pc[cidx[i]];
        const float r = 0.5f * fmaxf(g->hi[0] - g->lo[0], g->hi[2] - g->lo[2]);

        if (tree_root(gpar, cidx[i]) != cidx[i] || !g->leaf || g->hi[1] - g->lo[1] < 2.0f || (r > 40.0f && !g->wide)) {
            continue;
        }
        if (getenv("BMAP_TREE_LOG") != NULL && r > 40.0f) {    /* research: the wide ones, and what they are made of */
            unsigned nt3 = 0, q;

            for (q = 0; q + 2u < s->nv; q += 3u) {
                nt3 += vpc[q] >= 0 && tree_root(gpar, (unsigned)vpc[q]) == cidx[i];
            }
            fprintf(stderr, "trees: wide %u piece %u (%s) box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f, %u triangles\n", s->n_trees,
                    cidx[i], cidx[i] >= npc0 ? "panel" : "piece", (double)g->lo[0], (double)g->hi[0], (double)g->lo[1],
                    (double)g->hi[1], (double)g->lo[2], (double)g->hi[2], nt3);
            for (q = 0; q + 2u < s->nv && nt3 <= 4u; q += 3u) {
                if (vpc[q] >= 0 && tree_root(gpar, (unsigned)vpc[q]) == cidx[i]) {
                    fprintf(stderr, "    (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f)\n", (double)s->v[q].x, (double)s->v[q].y,
                            (double)s->v[q].z, (double)s->v[q + 1u].x, (double)s->v[q + 1u].y, (double)s->v[q + 1u].z,
                            (double)s->v[q + 2u].x, (double)s->v[q + 2u].y, (double)s->v[q + 2u].z);
                }
            }
        }
        g->tag = (unsigned char)(s->n_trees % 254u + 1u);
        s->trees[s->n_trees][0] = 0.5f * (g->lo[0] + g->hi[0]);
        s->trees[s->n_trees][1] = 0.5f * (g->lo[2] + g->hi[2]);
        s->trees[s->n_trees][2] = r;
        s->trees[s->n_trees][3] = g->lo[1];
        s->trees[s->n_trees][4] = g->hi[1];
        s->trees[s->n_trees][5] = (float)g->tag;
        s->trees[s->n_trees][6] = 0.5f * (g->hi[0] - g->lo[0]);
        s->trees[s->n_trees][7] = 0.5f * (g->hi[2] - g->lo[2]);
        s->n_trees++;
    }
    /* the tag on every triangle of a tree */
    for (i = 0; i < s->nd; i++) {
        const BmapDraw *d = &s->d[i];

        if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
            continue;
        }
        for (j = d->first; j < d->first + d->count; j++) {
            const unsigned pi = (unsigned)vpc[j];

            if (vpc[j] >= 0 && pc[pi].cand) {
                s->v[j].a = pc[tree_root(gpar, pi)].tag;
            } else if (vpc[j] >= 0 && pc[pi].canopy) {
                s->v[j].a = 255;        /* (the canopy: out round each tree that is out) */
            }
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        fprintf(stderr, "bmap: %u trees for the camera's clearance (%u foliage pieces of %u pieces)\n", s->n_trees, nc, npc);
    }
    if (getenv("BMAP_DEBUG_TREES") != NULL) {
        /* research: each tree in a colour of its own (by its tag), foliage with no tag red, the rest dark grey,
         * all without their pictures */
        for (i = 0; i < s->nd; i++) {
            BmapDraw *d = &s->d[i];

            if (d->billboard || d->grass || d->count < 3u || d->first + d->count > s->nv) {
                continue;
            }
            d->tex = -1;
            for (j = d->first; j < d->first + d->count; j++) {
                BmapVertex *v = &s->v[j];
                const unsigned t = v->a, lf = vpc[j] >= 0 && pc[vpc[j]].leaf;

                v->r = (unsigned char)(t ? 60u + (t * 97u) % 196u : lf ? 255u : 50u);
                v->g = (unsigned char)(t ? 60u + (t * 57u) % 196u : lf ? 0u : 50u);
                v->b = (unsigned char)(t ? 60u + (t * 151u) % 196u : lf ? 0u : 50u);
            }
        }
    }
    free(pc); free(gpar); free(cidx); free(par); free(slot); free(objv); free(pcof); free(green); free(cutout);
    free(vpc); free(dark);
}

#define CLEAR_TREES 3u
#define TREE_SIGHT 6.0f                 /* how far along the line of sight a tree in the way goes */
#define TREE_WIDE 7.0f                  /* a tree wider than twice this loses only this much round the camera */
#define TREE_NEAR 1.5f                  /* how near the camera stands to a tree's box for the tree to go */
unsigned bmap_clear_trees(const struct BmapScene *sc, const float eye[3], const float (*aim)[4], unsigned naim,
                          float clear[][4], unsigned n, unsigned max)
{
    float tr[CLEAR_TREES][5];
    unsigned nt = 0, ti, i, k, ai;

    for (ti = 0; sc != NULL && sc->trees != NULL && ti < sc->n_trees; ti++) {
        const float *t = sc->trees[ti];
        float de = 1e30f;               /* (the nearest of the camera's places to the tree's box: 0 inside it) */
        float cutx = eye[0], cutz = eye[2];     /* (a wide tree: the middle of the stretch that goes) */
        int hit = 0;

        for (ai = 0; aim != NULL && ai < naim; ai++) {
            /* in the line of sight: the eye's way to an aim (the battle's middle), on the ground, through the
             * tree's box (a little widened) within TREE_SIGHT of the eye, at a height the tree has */
            const float *mid = aim[ai];
            const float sx = mid[0] - eye[0], sz = mid[2] - eye[2], len = sqrtf(sx * sx + sz * sz);

            if (mid[3] > 0.0f && len > 1e-3f) {
                const float ux = sx / len, uz = sz / len, x0 = t[0] - t[6] - 0.3f, x1 = t[0] + t[6] + 0.3f;
                const float z0 = t[1] - t[7] - 0.3f, z1 = t[1] + t[7] + 0.3f;
                float ta = 0.0f, tb = fminf(len, TREE_SIGHT);
                int ok = 1;

                if (fabsf(ux) < 1e-6f) {
                    ok = eye[0] >= x0 && eye[0] <= x1;
                } else {
                    const float a = (x0 - eye[0]) / ux, b = (x1 - eye[0]) / ux;

                    ta = fmaxf(ta, fminf(a, b));
                    tb = fminf(tb, fmaxf(a, b));
                }
                if (ok && fabsf(uz) < 1e-6f) {
                    ok = eye[2] >= z0 && eye[2] <= z1;
                } else if (ok) {
                    const float a = (z0 - eye[2]) / uz, b = (z1 - eye[2]) / uz;

                    ta = fmaxf(ta, fminf(a, b));
                    tb = fminf(tb, fmaxf(a, b));
                }
                if (ok && ta <= tb) {
                    const float ya = eye[1] + (mid[1] - eye[1]) * (ta / len);

                    if (ya > t[3] - 1.0f && ya < t[4] + 1.0f && (!hit || ta < de)) {
                        hit = 1;
                        de = ta;
                        cutx = eye[0] + ux * (ta + 0.5f * TREE_WIDE);   /* (where the view passes through it) */
                        cutz = eye[2] + uz * (ta + 0.5f * TREE_WIDE);
                    }
                }
            }
        }
        for (i = 0; i < n; i++) {
            const float dx = fmaxf(0.0f, fabsf(t[0] - clear[i][0]) - t[6]), dz = fmaxf(0.0f, fabsf(t[1] - clear[i][2]) - t[7]);
            const float dd = sqrtf(dx * dx + dz * dz);

            if (clear[i][3] > 0.0f && clear[i][1] > t[3] - 1.0f && clear[i][1] < t[4] + 1.0f && dd < TREE_NEAR) {
                /* (a wide tree: the stretch round the place that reached it, not round where the eye is now) */
                if (!hit || dd < de) {
                    cutx = clear[i][0];
                    cutz = clear[i][2];
                }
                hit = 1;
                de = fminf(de, dd);
            }
        }
        if (!hit) {
            continue;
        }
        for (k = nt < CLEAR_TREES ? nt : CLEAR_TREES - 1u; k > 0u && tr[k - 1u][4] > de; k--) {
            memcpy(tr[k], tr[k - 1u], sizeof tr[k]);
        }
        if (nt < CLEAR_TREES || k < CLEAR_TREES - 1u || tr[k][4] > de) {    /* (a full list: only a nearer one) */
            tr[k][0] = t[0]; tr[k][1] = t[5]; tr[k][2] = t[1]; tr[k][3] = -(t[2] + 0.5f); tr[k][4] = de;
            if (t[2] > TREE_WIDE) {
                /* (a wide piece, a forest wall's panel with several trees painted on it, a wide leaf sheet: only
                 * its stretch round the camera goes, TREE_WIDE to either side of the point of it nearest the eye) */
                tr[k][0] = fminf(fmaxf(cutx, t[0] - t[6]), t[0] + t[6]);
                tr[k][2] = fminf(fmaxf(cutz, t[1] - t[7]), t[1] + t[7]);
                tr[k][3] = -TREE_WIDE;
            }
            nt = nt < CLEAR_TREES ? nt + 1u : nt;
        }
    }
    if (getenv("BMAP_TREE_LOG") != NULL && sc != NULL) {    /* research: what the rule sees */
        float best[3] = { 1e30f, 1e30f, 1e30f };
        unsigned bi[3] = { 0, 0, 0 };

        for (ti = 0; sc->trees != NULL && ti < sc->n_trees; ti++) {
            const float ex = sc->trees[ti][0] - eye[0], ez = sc->trees[ti][1] - eye[2], de = sqrtf(ex * ex + ez * ez);

            for (k = 0; k < 3u; k++) {
                if (de < best[k]) {
                    for (i = 2u; i > k; i--) { best[i] = best[i - 1u]; bi[i] = bi[i - 1u]; }
                    best[k] = de; bi[k] = ti;
                    break;
                }
            }
        }
        fprintf(stderr, "trees: %u known, %u taken; eye %.1f %.1f %.1f; first ball r %.1f\n", sc->n_trees, nt,
                (double)eye[0], (double)eye[1], (double)eye[2], n > 0u ? (double)clear[0][3] : 0.0);
        for (k = 0; k < 3u && sc->n_trees > k; k++) {
            const float *t = sc->trees[bi[k]];

            fprintf(stderr, "  tree at %.1f away: r %.1f, foot %.1f, top %.1f\n", (double)best[k], (double)t[2], (double)t[3], (double)t[4]);
        }
        for (ai = 0; aim != NULL && ai < naim; ai++) {
            fprintf(stderr, "  aim %u: %.1f %.1f %.1f height %.1f\n", ai, (double)aim[ai][0], (double)aim[ai][1],
                    (double)aim[ai][2], (double)aim[ai][3]);
        }
        for (k = 0; k < nt; k++) {
            fprintf(stderr, "  taken: at %.1f %.1f tag %.0f width %.1f, %.1f along\n", (double)tr[k][0], (double)tr[k][2],
                    (double)tr[k][1], (double)-tr[k][3], (double)tr[k][4]);
        }
    }
    if (nt > max) {
        nt = max;
    }
    if (n + nt > max) {
        n = max - nt;                   /* (the oldest places make room) */
    }
    for (ti = 0; ti < nt; ti++) {
        memcpy(clear[n], tr[ti], 4 * sizeof(float));
        n++;
    }
    return n;
}

/* The trees the eye stands inside (its place within 0.3 of the tree's box, at a height the tree has).  Entries as
 * bmap_clear_trees makes them; returns their count. */
static unsigned trees_inside(const BmapScene *s, const float eye[3], float out[][4], unsigned max)
{
    unsigned n = 0, ti;

    for (ti = 0; s != NULL && s->trees != NULL && ti < s->n_trees && n < max; ti++) {
        const float *t = s->trees[ti];

        if (fabsf(eye[0] - t[0]) > t[6] + 0.3f || fabsf(eye[2] - t[1]) > t[7] + 0.3f || eye[1] < t[3] - 0.3f
            || eye[1] > t[4] + 0.3f) {
            continue;
        }
        out[n][0] = t[0];
        out[n][1] = t[5];
        out[n][2] = t[1];
        out[n][3] = -(t[2] + 0.5f);
        if (t[2] > TREE_WIDE) {         /* (a wide one: its stretch round the eye) */
            out[n][0] = fminf(fmaxf(eye[0], t[0] - t[6]), t[0] + t[6]);
            out[n][2] = fminf(fmaxf(eye[2], t[1] - t[7]), t[1] + t[7]);
            out[n][3] = -TREE_WIDE;
        }
        n++;
    }
    return n;
}

#define TREES_IN 16u                    /* the trees one still camera may stand inside */
unsigned bmap_place_trees(const BmapScene *s, float out[][4], unsigned max)
{
    /* (the middle between the two sides' places: the own Pokemon's and the foe's) */
    const float aim[1][4] = { { 0.5f * (PLACE_SX + FOE_SX), GROUND_Y, 0.5f * (PLACE_SZ + FOE_SZ_GAME), 1.0f } };
    unsigned n = 0, e, k, q, m, rule;

    if (s == NULL) {
        return 0;
    }
    for (e = 0; e < 2u; e++) {
        float eye[3], at[3], c[8u + TREES_IN][4];

        memcpy(eye, e ? k_intro_eye : k_cmd_eye, sizeof eye);
        memcpy(at, e ? k_intro_target : k_cmd_target, sizeof at);
        bmap_battle_camera(eye, at, s);
        memcpy(c[0], eye, sizeof eye);
        c[0][3] = 1.0f;
        rule = bmap_clear_trees(s, eye, aim, 1u, c, 1u, 8u);
        /* (and every tree this camera stands inside, whatever the rule's count: it would fill the view) */
        m = rule + trees_inside(s, eye, &c[rule], TREES_IN);
        for (k = 0; k < m; k++) {
            int have = 0;

            if (c[k][3] >= 0.0f) {
                continue;
            }
            for (q = 0; q < n && !have; q++) {
                have = out[q][1] == c[k][1] && fabsf(out[q][0] - c[k][0]) < 0.5f && fabsf(out[q][2] - c[k][2]) < 0.5f;
            }
            if (!have && n < max) {
                if (getenv("BMAP_TREE_LOG") != NULL) {  /* research: what the set is made of */
                    fprintf(stderr, "trees: set %u: %s camera, %s: tag %.0f at %.1f %.1f width %.1f\n", n, e ? "intro" : "command",
                            k < rule ? "by the rule" : "stands inside", (double)c[k][1], (double)c[k][0], (double)c[k][2],
                            (double)-c[k][3]);
                }
                memcpy(out[n++], c[k], 4 * sizeof(float));
            }
        }
    }
    return n;
}

/* bmap_place_where: the search's answer goes here, and the scene stays as it is */
static float *g_where;

void bmap_place_views(BmapScene *s, const BmapPlaceSpec *sp)
{
    s->view_set = !sp->field;
    s->trainer_size = sp->trainer_size > 0.0f ? sp->trainer_size : 1.0f;
    s->trainer_own_size = sp->trainer_own_size > 0.0f ? sp->trainer_own_size : 1.0f;
    s->has_ally = sp->has_ally;
    s->trainer_ally_size = sp->trainer_ally_size > 0.0f ? sp->trainer_ally_size : 1.0f;
    memset(s->trainer_ally, 0, sizeof s->trainer_ally);
    if (s->view_set) {
        s->cam = sp->cam;
        memcpy(s->trainer_foe, sp->trainer_foe, sizeof s->trainer_foe);
        memcpy(s->trainer_own, sp->trainer_own, sizeof s->trainer_own);
        memcpy(s->trainer_ally, sp->trainer_ally, sizeof s->trainer_ally);
    }
    s->intro = sp->intro;
    if (s->intro.zoom <= 0.0f) {
        s->intro.zoom = 1.0f;
    }
}

void bmap_place_where(BmapScene *s, const BmapPlaceSpec *sp, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    g_where = out;
    bmap_place_spec(s, sp);
    g_where = NULL;
}

void bmap_place_spec(BmapScene *s, const BmapPlaceSpec *sp0)
{
    const BmapPlaceSpec loc = research_spec(sp0), *sp = &loc;
    const double pi = 3.14159265358979323846;
    double a = (double)(sp->facing & 0xFFFFu) * 2.0 * pi / 65536.0;
    float px = sp->px, pz = sp->pz;
    int reach = sp->reach, q;
    /* research (BMAP_FREE_ANGLE=1): the facing exactly as given, with the
     * search for a clear spot still on: no turn, no turning round */
    int free_angle = getenv("BMAP_FREE_ANGLE") != NULL;
    int no_back = sp->no_back && !free_angle, turn = free_angle ? 0 : sp->turn;

    if (free_angle && getenv("BMAP_FORCE_FACING") != NULL) {
        /* ... and that facing, whatever the row says (a trainer's row looks at the trainer) */
        a = (double)((unsigned)atoi(getenv("BMAP_FORCE_FACING")) & 0xFFFFu) * 2.0 * pi / 65536.0;
    }

    g_foe_z = FOE_SZ_GAME - sp->foe_dz;
    g_place_doubles = sp->doubles;
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
    bmap_place_views(s, sp);
    float ca, sa;
    float gy, k = BMAP_PLACE_K, th[6];
    unsigned i, nd0;
    unsigned char *done;
    const unsigned char *grass;

    if (s == NULL) {
        return;
    }
    if (getenv("EIDOS_DEBUG_BMAP_K") != NULL) {     /* tests: another scale */
        k = (float)atof(getenv("EIDOS_DEBUG_BMAP_K"));
    }
    if (!s->prepared) {
        clock_t t_prep = clock();

        bmap_place_prepare(s, sp->field);
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: prepared in %.0f ms (%u triangles)\n", 1000.0 * (double)(clock() - t_prep) / CLOCKS_PER_SEC,
                    s->nv / 3u);
        }
    }
    nd0 = s->prep_nd0;
    grass = s->prep;                    /* NULL when its memory ran out */
    g_tmask = s->prep != NULL ? s->prep + s->prep_nd0 : NULL;
    if (sp->has_rect) {                 /* the start inside the rectangle */
        px = fminf(fmaxf(px, sp->x0), sp->x1);
        pz = fminf(fmaxf(pz, sp->z0), sp->z1);
    }
    if (no_back && reach >= 0 && cos(a) < BACK_COS) {
        /* the battle would look toward the field camera: the same line from
         * its other end, the own Pokemon where the foe would stand */
        float fx, fz;

        to_map(FOE_SX, FOE_SZ, px, pz, (float)cos(a), (float)sin(a), k, &fx, &fz);
        px = fx;
        pz = fz;
        a += pi;
    }
    /* Where the battle stands: every tile within `reach` of the start, the
     * facing and up to `turn` 45-degree steps either side of it (the foe is
     * where the player looks, never beside or behind them).  The world
     * keeps one scale everywhere: a room is not shown larger, so the floor
     * around the battle stays in view.  The lowest place_score wins;
     * distance and turning each cost a little, so the start wins whenever
     * it is as good.  With reach < 0 the start and facing are used as
     * given.  When no tile within reach can hold the battle (the start in a
     * tree or on a wall, a trainer's row turned round to look away from the
     * field camera), the reach grows a tile at a time, up to 6 more, until
     * one can: the battle was left standing in the tree.  Where even that
     * finds none (ground the game's tiles close off in this season:
     * Dragonspiral Tower's garden outside winter), the closed tiles within
     * the row's own reach are scored as the open ones are. */
    {
        clock_t t_start = clock();
        float best = 1e30f, bx = px, bz = pz, gp = bmap_ground_y(s, px, pz);
        float best_in = 1e30f, ibx = px, ibz = pz;
        double ba = a, iba = a;
        int dx, dz, n, home = bmap_tile(s, px, pz), reach0 = reach, closed;

        if (reach < 0) {
            best = place_score(s, grass, px, pz, a, k, gp, home);
        }
        for (closed = 0; closed < 2 && best > 1e29f; closed++)
        for (reach = reach0; reach >= 0 && reach <= reach0 + (closed ? 0 : 6) && best > 1e29f; reach++)
        for (dz = -reach; dz <= reach; dz++) {
            for (dx = -reach; dx <= reach; dx++) {
                float dist = sqrtf((float)(dx * dx + dz * dz));

                if (dist > (float)reach + 0.5f) {
                    continue;
                }
                for (n = 0; n <= 2 * turn && n < 8; n++) {
                    double an = a + (double)((n + 1) / 2) * ((n & 1) ? 1.0 : -1.0) * pi / 4.0;
                    float cx = px + (float)dx * 16.0f, cz = pz + (float)dz * 16.0f;
                    float sc;

                    if (no_back && cos(an) < BACK_COS) {
                        continue;           /* a turn toward the field camera */
                    }

                    if (!closed && bmap_tile(s, cx, cz) == BMAP_TILE_BLOCKED) {
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
        reach = reach0;
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

                        if (no_back && cos(an) < BACK_COS) {
                            continue;       /* toward the field camera */
                        }
                        if (free_angle && getenv("BMAP_FORCE_FACING") != NULL && cos(an - a) < 0.99) {
                            continue;       /* research: the forced facing alone */
                        }

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
        scene_dump(s, th);
        debug_backs(s);
        debug_ids(s);
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
            /* research: the sky colour and the fog, for the picture measurements */
            fprintf(stderr, "bmap: sky %u %u %u fog %d %u %u %u %.0f %.0f draws %u verts %u\n", s->clear[0], s->clear[1],
                    s->clear[2], s->fog_on, s->fog[0], s->fog[1], s->fog[2], (double)s->fog_start, (double)s->fog_end,
                    s->nd, s->nv);
        }
    }
    if (g_where != NULL) {
        /* (bmap_place_where: the place alone) */
        g_where[0] = px;
        g_where[1] = pz;
        g_where[2] = (float)a;
        g_where[3] = k;
        g_tmask = NULL;
        return;
    }
    ca = (float)cos(a);
    sa = (float)sin(a);
    if (sp->spot_floor) {
        /* a fixed view of a wild area: the floor is the game's own at the spot (bmap_place.h) */
        float ty;

        if (bmap_tile_height(s, px, pz, &ty)) {
            if (getenv("BMAP_PLACE_LOG") != NULL) {
                fprintf(stderr, "bmap: the view's own floor: the game's tile height %.1f at the spot (the ground by the player's height: %.1f)\n",
                        (double)ty, (double)bmap_ground_y(s, px, pz));
            }
            bmap_set_ref(s, ty);
        }
    }
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

        /* The zone's fog, along the field camera's line of sight from the
         * battle's own place (bmap.h, fog_k).  That camera looks at the
         * player from behind and above: its line of sight in the map is
         * v = (-cos p sin y, -sin p, -cos p cos y).  A point m of the map
         * lies dot(m - at, v) deeper than the player; the placed scene is
         * the map turned by the place's angle and scaled by k about the
         * own Pokemon's place, so in battle units the depth is
         * dot(b - o, R v) / k. */
        memset(s->fog_k, 0, sizeof s->fog_k);
        if (s->fog_on && s->fog_end > s->fog_start) {
            const float cp = cosf(s->fog_pitch), sp2 = sinf(s->fog_pitch);
            const float vx = -cp * sinf(s->fog_yaw), vy = -sp2, vz = -cp * cosf(s->fog_yaw);
            const float span = (s->fog_end - s->fog_start) * k;
            const float o[3] = { PLACE_SX, GROUND_Y - GROUND_LIFT, PLACE_SZ };

            s->fog_k[0] = (ca * vx - sa * vz) / span;
            s->fog_k[1] = vy / span;
            s->fog_k[2] = (sa * vx + ca * vz) / span;
            s->fog_k[3] = o[0] * s->fog_k[0] + o[1] * s->fog_k[1] + o[2] * s->fog_k[2] + s->fog_start * k / span;
        }

        /* The surf mount stood under the player in the field, wherever the
         * battle's slots now fall: it moves under the own Pokemon's place,
         * turned to face the foe as that Pokemon does, at the same height
         * over its ground as it had over the field's. */
        if (s->has_mount) {
            float fdx = fx - px, fdz = fz - pz;
            float th = atan2f(fdz, fdx) - atan2f(s->mount_fwd[1], s->mount_fwd[0]);
            float ct = cosf(th), st = sinf(th);
            float dy = gy - bmap_ground_y(s, s->mount_t[0], s->mount_t[2]);
            unsigned di, j;

            for (di = 0; di < s->nd; di++) {
                const BmapDraw *d = &s->d[di];

                if (!d->mount || d->first + d->count > s->nv) {
                    continue;
                }
                for (j = d->first; j < d->first + d->count; j++) {
                    BmapVertex *v = &s->v[j];
                    float ux = v->x - s->mount_t[0], uz = v->z - s->mount_t[2];

                    v->x = px + ct * ux - st * uz;
                    v->z = pz + st * ux + ct * uz;
                    v->y += dy;
                }
            }
            if (getenv("BMAP_PLACE_LOG") != NULL) {
                fprintf(stderr, "bmap: surf mount moved from %.0f %.0f to %.0f %.0f, turned %.0f degrees\n",
                        (double)s->mount_t[0], (double)s->mount_t[2], (double)px, (double)pz, (double)(th * 57.29578f));
            }
        }
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
    if (s->sky_count > 0u && s->sky_first + s->sky_count <= s->nv) {
        /* the cloud dome: round the battle, inside the camera's far limit wherever the camera stands
         * (a ball of 300 battle units about the own slot's ground; only its directions count) */
        float mc[3] = { 0.0f, 0.0f, 0.0f };

        for (i = s->sky_first; i < s->sky_first + s->sky_count; i++) {
            mc[0] += s->v[i].x;
            mc[1] += s->v[i].y;
            mc[2] += s->v[i].z;
        }
        mc[0] /= (float)s->sky_count;
        mc[1] /= (float)s->sky_count;
        mc[2] /= (float)s->sky_count;
        for (i = s->sky_first; i < s->sky_first + s->sky_count; i++) {
            BmapVertex *v = &s->v[i];
            float dx = v->x - mc[0], dy = v->y - mc[1], dz = v->z - mc[2];
            float l = sqrtf(dx * dx + dy * dy + dz * dz);

            if (l > 1e-6f) {
                v->x = 300.0f * dx / l;
                v->y = GROUND_Y + 300.0f * dy / l;
                v->z = PLACE_SZ + 300.0f * dz / l;
            }
        }
    }
    if (s->sky_count > 0u) {
        /* the cloud layer fixed to the map: battle (x, z) -> map, ux = (x - PLACE_SX) / k and uz = (z - PLACE_SZ) / k
         * turned back by the placing's angle, plus the place; then one picture repeat a sky_rep */
        const double kk = k, r = s->sky_rep > 0.0f ? s->sky_rep : 1.0, c0 = cos(a), s0 = sin(a);
        const double cu = ((-c0 * PLACE_SX - s0 * PLACE_SZ) / kk + px) / r;
        const double cv = ((s0 * PLACE_SX - c0 * PLACE_SZ) / kk + pz) / r;

        s->sky_u[0] = (float)(c0 / kk / r);
        s->sky_u[1] = (float)(s0 / kk / r);
        s->sky_u[2] = (float)(cu - floor(cu));
        s->sky_v[0] = (float)(-s0 / kk / r);
        s->sky_v[1] = (float)(c0 / kk / r);
        s->sky_v[2] = (float)(cv - floor(cv));
    }
    if (!sp->field) {
        stand_drift(s);
    }
    topview_dump(s, nd0, 1, th);
    {
        /* research (BMAP_PLACE_LOG): what the steps after the search cost */
        const clock_t tg0 = clock();
        const unsigned nv_map = s->nv;
        clock_t tg1;

        /* the trees first, among the map's own triangles: the grass that grows next is no tree, and its hundreds of
         * thousands of corners made this step several times as long */
        find_trees(s);
        tg1 = clock();
        if (grass != NULL) {
            grass_grow_fixed(s, grass, nd0, 16.0f * k);
        }
        g_tmask = NULL;                 /* the scene's own (prep) */
        for (i = nv_map; i < s->nv; i++) {
            s->v[i].a = 0;              /* (no tree: find_trees' mark, as it left the grass when it ran after it) */
        }
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: after the search: the trees %.0f ms, the grass %.0f ms (%u draws, %u vertices)\n",
                    1000.0 * (double)(tg1 - tg0) / CLOCKS_PER_SEC, 1000.0 * (double)(clock() - tg1) / CLOCKS_PER_SEC, s->nd, s->nv);
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        /* research: how far the placed map's four edges lie from the own slot */
        float x0 = 0.0f, x1 = 0.0f, z0 = PLACE_SZ, z1 = PLACE_SZ;

        for (i = 0; i < s->nv; i++) {
            const BmapVertex *v = &s->v[i];

            if (v->y <= -900.0f) {
                continue;
            }
            x0 = fminf(x0, v->x); x1 = fmaxf(x1, v->x); z0 = fminf(z0, v->z); z1 = fmaxf(z1, v->z);
        }
        fprintf(stderr, "bmap: edges left %.0f right %.0f far %.0f near %.0f (battle units from the own slot)\n",
                -x0, x1, PLACE_SZ - z0, z1 - PLACE_SZ);
    }
}
