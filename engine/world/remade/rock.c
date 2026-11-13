/*
 * engine/world/remade/rock.c -- a rock (and a vase): a picture that the map
 * shows as one flat card, made a body of pixel-sized blocks.
 *
 * The picture is read row by row.  Each row is a round slice of the body: as
 * wide as the row, as deep as it is wide, centred on the row.  The body is
 * made of blocks one picture pixel across, each block one pixel of a picture
 * on all its faces, sharp.  Rules that follow from how the pictures are
 * drawn:
 *   - the picture shows the thing from above the ground, so under its widest
 *     row its outline turns in again: the body keeps every row, and sits in
 *     the ground by half of what it has under its widest row;
 *   - the picture's top two rows are its dark outline alone: the blocks
 *     there take the row under them, or the body would wear a dark cap;
 *   - colours named in `key` (a shadow painted round the foot, the water's
 *     ripple) are not the thing: the body does not wear them.
 * The body stands upright where the map turns the picture to the camera.
 *
 * A rock's four sides (wrap=sides, the rule): the south side is the game's
 * picture.  With `faces` the east, north and west sides are pictures drawn
 * by hand (rock_faces.c); without, every side wears the picture's middle.
 * A turned thing (wrap=lathe, the vase): the picture's middle repeated
 * `around` times round the body; `boss` colours stand `bump` pixels out of
 * the surface; `mouth` rows are its opening seen from above, laid flat on
 * the top; `lip` rows take the lip's plain colour.
 *
 * The block faces that look one way and lie in one plane are one layer; a
 * layer is a few rectangles, and its picture stands with the other layers'
 * in one texture of the body's own (512 wide).  Every other pixel of that
 * texture takes a neighbour's colour, outward step by step, so a smaller
 * copy of it mixes a face's edge with its own colours and not with black.
 *
 * The shadow: the body thrown on the ground as the game throws a Pokemon's
 * (MCSS_Draw's shadow pass with BTLV_MCSS_AddPoke's default numbers: the
 * sprite again, black and half see-through, leaning 11.25 degrees to the
 * left, laid back on the ground, moved by (-0.25, 0, -0.375) battle units):
 * one flat picture, a pixel a block, see-through (15 of 31), that keeps the
 * battle's own directions.  A rock in water (`ripple`): the picture's ripple
 * colours lie flat on the water round the body, a band a colour.
 *
 * Settings: key, faces, ripple, wrap, around, reach, boss, bump, fat, mouth,
 * rim, lip, rimw, sink, high, lean (a fixed leaning card, not a camera-facing
 * one), shadow (0: none).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"
#include "rock.h"

#define PI 3.14159265358979323846
#define MAXPX 64
#define AW 512
#define GUT 2

enum { SOUTH, NORTH, EAST, WEST, TOP, UNDER, SHADOW, RING };

typedef struct { unsigned char kind; short plane, a, b, pi, pj; } Cell;
typedef struct { int kind, plane, a0, b0, bw, bh, ox, oy; int *grid; } Layer;

typedef struct {
    int W, H, HH;               /* the picture; HH: its rows with the faces under it */
    unsigned char *pic;         /* HH * W * 4 */
    unsigned char mask[MAXPX][MAXPX], mmask[MAXPX][MAXPX];
    int has[MAXPX], sa[MAXPX], se[MAXPX];       /* a row's kept pixels: columns sa .. se - 1 */
    int jtop, jlast, rowlast;
    int topskip;                /* the picture's first rows that are its outline alone (2; `top`) */
    Cell *cell;
    unsigned ncell, ccell;
    int fail;
} Rk;

static const unsigned char *pp(const Rk *r, int c, int j)
{
    return r->pic + ((size_t)j * (size_t)r->W + (size_t)c) * 4u;
}

static unsigned rgb_of(const unsigned char *p)
{
    return ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
}

static void cell_add(Rk *r, int kind, int plane, int a, int b, int pi, int pj)
{
    Cell *c;

    if (r->ncell == r->ccell) {
        unsigned cap = r->ccell ? r->ccell * 2u : 4096u;
        Cell *nw = (Cell *)realloc(r->cell, cap * sizeof *nw);

        if (nw == NULL) {
            r->fail = 1;
            return;
        }
        r->cell = nw;
        r->ccell = cap;
    }
    c = &r->cell[r->ncell++];
    c->kind = (unsigned char)kind;
    c->plane = (short)plane;
    c->a = (short)a; c->b = (short)b;
    c->pi = (short)pi; c->pj = (short)pj;
}

/* The kept pixel nearest column i of row j (the top two rows: the row under them). */
static void pix(const Rk *r, int j, int i, int *pi, int *pj)
{
    int lim = r->jtop + r->topskip < r->jlast ? r->jtop + r->topskip : r->jlast, c, best = -1, bd = 1 << 30;

    j = j > lim ? j : lim;
    for (c = 0; c < r->W; c++) {
        if (r->mask[j][c]) {
            int d = abs(c - i);

            if (d < bd) {
                bd = d;
                best = c;
            }
        }
    }
    *pi = best < 0 ? 0 : best;
    *pj = j;
}

static double lum(unsigned c)
{
    return ((c >> 16) & 255u) * 0.299 + ((c >> 8) & 255u) * 0.587 + (c & 255u) * 0.114;
}

/* The three hand-drawn faces under the picture (rows H.., 2H.., 3H..: east,
 * north, west).  0 when the lines do not fit the rock. */
static int faces_load(Rk *r, const char *const *lines)
{
    unsigned full[256], nfull = 0, i, k;
    int H = r->H, W = r->W, cur = -1, j, c, n;
    unsigned char *E, *Nf, *Wf;

    /* the picture's colours, dark to light */
    for (j = 0; j < H; j++) {
        for (c = 0; c < W; c++) {
            const unsigned char *p = pp(r, c, j);
            unsigned v = rgb_of(p);

            if (p[3] < 128) {
                continue;
            }
            for (i = 0; i < nfull && full[i] != v; i++) {
            }
            if (i == nfull && nfull < 256u) {
                full[nfull++] = v;
            }
        }
    }
    for (i = 1; i < nfull; i++) {       /* by brightness, then by the colour's own number */
        unsigned v = full[i];

        for (k = i; k > 0 && (lum(full[k - 1]) > lum(v) || (lum(full[k - 1]) == lum(v) && full[k - 1] > v)); k--) {
            full[k] = full[k - 1];
        }
        full[k] = v;
    }
    for (n = 1; n < 4; n++) {
        memcpy(r->pic + (size_t)n * H * W * 4u, r->pic, (size_t)H * W * 4u);
    }
    for (; *lines != NULL; lines++) {
        const char *l = *lines, *band;
        int row, first, len;

        if (l[0] == '[') {
            cur = strncmp(l, "[east]", 6) == 0 ? 1 : strncmp(l, "[north]", 7) == 0 ? 2 : strncmp(l, "[west]", 6) == 0 ? 3 : -1;
            continue;
        }
        row = atoi(l);
        band = strchr(l, ' ');
        first = band != NULL ? atoi(band + 1) : -1;
        band = band != NULL ? strchr(band + 1, ' ') : NULL;
        if (cur < 0 || band == NULL || row < 0 || row >= H || first < 0) {
            return 0;
        }
        band++;
        len = (int)strlen(band);
        {
            int ca = -1, ce = -1;

            for (c = 0; c < W; c++) {
                if (r->mask[row][c]) {
                    ca = ca < 0 ? c : ca;
                    ce = c;
                }
            }
            if (ca < 0 || first != ca || first + len - 1 != ce) {
                return 0;               /* the row is not the rock's row */
            }
        }
        for (k = 0; k < (unsigned)len; k++) {
            int x = first + (int)k;
            unsigned ch = (unsigned char)band[k];
            unsigned char *q;

            if (ch == '.' || !r->mask[row][x] || ch < 97u || ch - 97u >= nfull) {
                continue;
            }
            q = r->pic + (((size_t)cur * H + (size_t)row) * W + (size_t)x) * 4u;
            q[0] = (unsigned char)(full[ch - 97u] >> 16);
            q[1] = (unsigned char)(full[ch - 97u] >> 8);
            q[2] = (unsigned char)full[ch - 97u];
        }
    }
    /* Where faces meet.  The body is a dome: a face shows the columns within 0.707 of a row's half width from
     * its middle, so the corner between two faces is another column on every row.  East | north and north |
     * west, at the back: the same pixels on both sides of the corner, and for three pixels past it.  (Next to
     * the south face the corners are drawn by hand in the faces, row by row against the game's pixels.) */
    E = r->pic + (size_t)1 * H * W * 4u;
    Nf = r->pic + (size_t)2 * H * W * 4u;
    Wf = r->pic + (size_t)3 * H * W * 4u;
    for (j = 0; j < H; j++) {
        int a = -1, e = -1, sl, sr, carry;
        double cen, rad;

        for (c = 0; c < W; c++) {
            if (r->mask[j][c]) {
                a = a < 0 ? c : a;
                e = c + 1;
            }
        }
        if (a < 0) {
            continue;
        }
        cen = (a + e) / 2.0;
        rad = (e - a) / 2.0;
        sl = (int)ceil(cen - 0.7071 * rad - 0.5);
        sr = (int)floor(cen + 0.7071 * rad - 0.5);
        carry = sr - sl >= 15 ? 3 : 0;
        for (n = 0; n <= carry; n++) {
            if (sl + n >= 0 && sl + n < W && r->mask[j][sl + n]) {
                memcpy(Nf + ((size_t)j * W + (size_t)(sl + n)) * 4u, E + ((size_t)j * W + (size_t)(sl + n)) * 4u, 4u);
            }
            if (sr - n >= 0 && sr - n < W && r->mask[j][sr - n]) {
                memcpy(Nf + ((size_t)j * W + (size_t)(sr - n)) * 4u, Wf + ((size_t)j * W + (size_t)(sr - n)) * 4u, 4u);
            }
        }
    }
    return 1;
}

typedef struct { float p[3]; } V3;

static int v3_cmp(const void *a, const void *b)
{
    const V3 *x = (const V3 *)a, *y = (const V3 *)b;
    int k;

    for (k = 0; k < 3; k++) {
        if (x->p[k] != y->p[k]) {
            return x->p[k] < y->p[k] ? -1 : 1;
        }
    }
    return 0;
}

/* An angle folded into one repeat of `per`: -per/2 up to per/2.  An angle on the seam between two repeats (a
 * block on the body's diagonal, with four repeats) is the start of a repeat, whatever the last bit of the
 * arithmetic says: the same block takes the same pixel on every machine. */
static double fold(double angle, double per)
{
    double t = (angle + per / 2.0) / per, f = t - floor(t);

    if (f < 1e-9 || f > 1.0 - 1e-9) {
        return -per / 2.0;
    }
    return f * per - per / 2.0;
}

/* One new triangle of the body. */
static void emit(RmOut *o, unsigned src, const unsigned char rgb[3], const double c0[3], const double c1[3],
                 const double c2[3], const double t0[2], const double t1[2], const double t2[2], int cast)
{
    static const unsigned char black[3] = { 0, 0, 0 };

    if (cast) {
        rm_tri(o, src, 0, 0, black, c0, c1, c2, t0, t1, t2, 1, 15);
    } else {
        rm_tri(o, src, 0, 1, rgb, c0, c1, c2, t0, t1, t2, 0, 0);
    }
}

static void face(RmOut *o, unsigned src, const unsigned char rgb[3], double x0, double y0, double z0, double x1,
                 double y1, double z1, double x2, double y2, double z2, double x3, double y3, double z3, double ua,
                 double va, double ub, double vb, double uc, double vc, double ud, double vd, int cast)
{
    double c0[3] = { x0, y0, z0 }, c1[3] = { x1, y1, z1 }, c2[3] = { x2, y2, z2 }, c3[3] = { x3, y3, z3 };
    double t0[2] = { ua, va }, t1[2] = { ub, vb }, t2[2] = { uc, vc }, t3[2] = { ud, vd };

    emit(o, src, rgb, c0, c1, c2, t0, t1, t2, cast);
    emit(o, src, rgb, c0, c2, c3, t0, t2, t3, cast);
}

static int build(const RmBatch *bt, const RmTex *tx, const char *opt, RmOut *out)
{
    static const int kinds[6] = { SOUTH, NORTH, EAST, WEST, TOP, UNDER };
    Rk *r = (Rk *)calloc(1, sizeof *r);
    char ob[256], wrapb[16], shb[32];
    const char *wrap, *shadow_opt, *facesn;
    unsigned keys[8], nkey, boss[8], nboss, i;
    unsigned char *vox = NULL, *atlas = NULL, *filled = NULL, *isnew = NULL;
    short *colp = NULL;
    Layer *lay = NULL;
    int *order = NULL;
    int nlay = 0, shadow_layer = -1, ring_layer = -1, made = 0;
    int w = tx->w, h = tx->h, W, H, i0, i1, j0, j1, flip_x, flip_y, j, k, c, n, R, D, rmax = 0, jwide, AH;
    int mouth_n, mouth_j0 = 0, lip_n, lip_px[MAXPX][2], nlip = 0, rim_n, lathe, has_reach, fat, has_bump;
    const int front = rm_optf(opt, "front", 0) != 0.0;
    int top_j0 = -1, top_j1 = -2;
    int face_row[4] = { 0, 0, 0, 0 };
    double ua, ub, va, vb, foot[3], width, stand, s, sv, sink, look, cen[MAXPX], rbase[MAXPX], rcap = 0.0, per, reach, bump;
    double dfac[MAXPX], doff[MAXPX];
    double shc = 0.0, lift;
    int planes[6][MAXPX * 2 + 8], nplane[6];
    unsigned first_tri = out->ntri, src = bt->tri[0].id;

    if (r == NULL) {
        return 0;
    }
#define VOX(jj, kk, ii) vox[((size_t)(jj) * (size_t)D + (size_t)(kk)) * (size_t)W + (size_t)(ii)]
#define FILLED(jj, kk, ii) ((jj) >= 0 && (jj) < H && (kk) >= 0 && (kk) < D && (ii) >= 0 && (ii) < W && VOX(jj, kk, ii))
    /* the part of the texture the card shows, the way the card shows it */
    {
        unsigned vi, nv = bt->n * 3u, xmin = 0, xmax = 0, ymin = 0, ymax = 0;

        for (vi = 1; vi < nv; vi++) {
            const float *p = bt->tri[vi / 3u].p[vi % 3u];

            if (p[0] < bt->tri[xmin / 3u].p[xmin % 3u][0]) xmin = vi;
            if (p[0] > bt->tri[xmax / 3u].p[xmax % 3u][0]) xmax = vi;
            if (p[1] < bt->tri[ymin / 3u].p[ymin % 3u][1]) ymin = vi;
            if (p[1] > bt->tri[ymax / 3u].p[ymax % 3u][1]) ymax = vi;
        }
        ua = bt->tri[xmin / 3u].uv[xmin % 3u][0];
        ub = bt->tri[xmax / 3u].uv[xmax % 3u][0];
        va = bt->tri[ymax / 3u].uv[ymax % 3u][1];      /* top */
        vb = bt->tri[ymin / 3u].uv[ymin % 3u][1];      /* bottom */
    }
    /* (a texture that repeats: a card may show a repeat other than the first; the same pixels) */
    {
        double fu = floor((ua < ub ? ua : ub) + 1e-6), fv = floor((va < vb ? va : vb) + 1e-6);

        ua -= fu; ub -= fu;
        va -= fv; vb -= fv;
    }
    i0 = (int)rint(ua * w); i1 = (int)rint(ub * w);
    j0 = (int)rint(va * h); j1 = (int)rint(vb * h);
    if (i0 > i1) { int t = i0; i0 = i1; i1 = t; }
    if (j0 > j1) { int t = j0; j0 = j1; j1 = t; }
    i0 = i0 < 0 ? 0 : i0; i1 = i1 > w ? w : i1;
    j0 = j0 < 0 ? 0 : j0; j1 = j1 > h ? h : j1;
    W = i1 - i0;
    H = j1 - j0;
    if (W < 2 || H < 2 || W > MAXPX || H > MAXPX) {
        goto done;
    }
    flip_x = ua > ub;
    flip_y = va > vb;
    r->W = W; r->H = H; r->HH = H;
    r->pic = (unsigned char *)malloc((size_t)4 * H * W * 4u);
    if (r->pic == NULL) {
        goto done;
    }
    nkey = rm_colours(rm_opt(opt, "key", ob, sizeof ob), keys, 8);
    for (j = 0; j < H; j++) {
        for (c = 0; c < W; c++) {
            const unsigned char *p = tx->rgba + ((size_t)(j0 + (flip_y ? H - 1 - j : j)) * (size_t)w + (size_t)(i0 + (flip_x ? W - 1 - c : c))) * 4u;

            memcpy(r->pic + ((size_t)j * W + (size_t)c) * 4u, p, 4u);
            r->mask[j][c] = (unsigned char)(p[3] >= 128 && !rm_has_colour(keys, nkey, p));
        }
    }
    /* `mouth=N`: the picture's first N rows are the thing's opening as the field's camera sees it from above
     * (the rim, and the dark inside).  They are not part of the body's side: they lie flat on top of it. */
    mouth_n = (int)rm_optf(opt, "mouth", 0);
    memcpy(r->mmask, r->mask, sizeof r->mask);
    if (mouth_n > 0) {
        for (j = 0, mouth_j0 = -1; j < H && mouth_j0 < 0; j++) {
            for (c = 0; c < W; c++) {
                if (r->mask[j][c]) {
                    mouth_j0 = j;
                }
            }
        }
        if (mouth_j0 < 0) {
            mouth_n = 0;
        }
        for (j = mouth_j0; mouth_n > 0 && j < mouth_j0 + mouth_n && j < H; j++) {
            memset(r->mask[j], 0, sizeof r->mask[j]);
        }
    }
    /* (`top=N`: the picture's first N rows are its dark outline alone, 2; a picture without an outline: 0) */
    r->topskip = (int)rm_optf(opt, "top", 2);
    r->jtop = -1;
    for (j = 0, n = 0; j < H; j++) {
        int a = -1, e = -1;

        for (c = 0; c < W; c++) {
            if (r->mask[j][c]) {
                a = a < 0 ? c : a;
                e = c + 1;
            }
        }
        r->has[j] = a >= 0;
        r->sa[j] = a;
        r->se[j] = e;
        if (a >= 0) {
            r->jtop = r->jtop < 0 ? j : r->jtop;
            r->rowlast = j;
            n++;
        }
    }
    if (n < 3) {
        goto done;
    }
    /* where it stands, how wide the card is, how much of its length it stands high */
    {
        V3 *pt = (V3 *)malloc((size_t)bt->n * 3u * sizeof *pt), lowp[2], highp[2];
        unsigned np = 0, vi, q;
        double top[3], len;

        if (pt == NULL) {
            goto done;
        }
        for (vi = 0; vi < bt->n * 3u; vi++) {
            for (q = 0; q < 3u; q++) {
                pt[np].p[q] = (float)(rint((double)bt->tri[vi / 3u].p[vi % 3u][q] * 1000.0) / 1000.0);
            }
            np++;
        }
        qsort(pt, np, sizeof *pt, v3_cmp);
        for (vi = 1, q = 1; vi < np; vi++) {
            if (v3_cmp(&pt[vi], &pt[q - 1]) != 0) {
                pt[q++] = pt[vi];
            }
        }
        np = q;
        if (np < 2) {
            free(pt);
            goto done;
        }
        /* the two lowest and the two highest */
        for (vi = 1; vi < np; vi++) {       /* by height, keeping the order of equals */
            V3 v = pt[vi];

            for (q = vi; q > 0 && pt[q - 1].p[1] > v.p[1]; q--) {
                pt[q] = pt[q - 1];
            }
            pt[q] = v;
        }
        lowp[0] = pt[0]; lowp[1] = pt[1];
        highp[0] = pt[np - 2]; highp[1] = pt[np - 1];
        free(pt);
        for (q = 0; q < 3u; q++) {
            foot[q] = (double)(float)((lowp[0].p[q] + lowp[1].p[q]) / 2.0f);
            top[q] = (double)(float)((highp[0].p[q] + highp[1].p[q]) / 2.0f);
        }
        len = sqrt((top[0] - foot[0]) * (top[0] - foot[0]) + (top[1] - foot[1]) * (top[1] - foot[1]) + (top[2] - foot[2]) * (top[2] - foot[2]));
        width = sqrt((double)(lowp[0].p[0] - lowp[1].p[0]) * (lowp[0].p[0] - lowp[1].p[0]) + (double)(lowp[0].p[1] - lowp[1].p[1]) * (lowp[0].p[1] - lowp[1].p[1])
                     + (double)(lowp[0].p[2] - lowp[1].p[2]) * (lowp[0].p[2] - lowp[1].p[2]));
        stand = len > 0 ? (top[1] - foot[1]) / len : 1.0;
    }
    if (bt->bb) {
        /* a camera-facing picture stands where the map turns it: on that point, on the ground (its own lower
         * edge is not that place: the picture leans back about its middle) */
        foot[0] = bt->pivot[0]; foot[1] = bt->pivot[1]; foot[2] = bt->pivot[2];
    }
    /* `at=X:Z` (given by rm_rock for `centre=1`): the body stands there, over the middle of what it stands on;
     * the card is set off it in the map only so the flat picture shows from the field's camera */
    if (rm_opt(opt, "at", ob, sizeof ob) != NULL) {
        double ax = 0.0, az = 0.0;

        if (sscanf(ob, "%lf:%lf", &ax, &az) == 2) {
            foot[0] = ax;
            foot[2] = az;
        }
    }
    /* `shift=X:Z`: the body stands that far from the card's foot (a plant's card leans from the pot's front
     * edge; its body stands over the pot's middle) */
    if (rm_opt(opt, "shift", ob, sizeof ob) != NULL) {
        double sx = 0.0, sz = 0.0;

        if (sscanf(ob, "%lf:%lf", &sx, &sz) == 2) {
            foot[0] += sx;
            foot[2] += sz;
        }
    }
    s = width / W;                      /* one picture pixel, in map units: a block's edge */
    /* A block is as high as a pixel of the leaning picture stands as the battle's command camera sees it: that
     * camera looks 12.5 degrees down, so a picture leaning at the angle a shows sin(a + 12.5) of its length.
     * (As high as it stands alone the rocks were pancakes; as long as the picture, eggs twice too tall.) */
    stand = stand < 0.0 ? 0.0 : stand > 1.0 ? 1.0 : stand;
    look = sin(asin(stand) + 12.5 * PI / 180.0);
    look = look > 1.0 ? 1.0 : look;
    sv = s * rm_optf(opt, "high", look);
    /* `lip=N`: the lip's N rows take the lip's plain colour; `rimw`: the lip is as wide as the opening */
    lip_n = (int)rm_optf(opt, "lip", 0);
    if (lip_n > 0 && mouth_n > 0) {
        int jl = r->jtop + 1 < r->rowlast ? r->jtop + 1 : r->rowlast, grp[MAXPX][3], ng = 0, g;

        if (rm_opt(opt, "rimw", ob, sizeof ob) != NULL) {
            int ba = 0, be = -1;

            for (j = mouth_j0; j < mouth_j0 + mouth_n && j < H; j++) {
                int a = -1, e = -1;

                for (c = 0; c < W; c++) {
                    if (r->mmask[j][c]) {
                        a = a < 0 ? c : a;
                        e = c + 1;
                    }
                }
                if (a >= 0 && e - a > be - ba) {
                    ba = a;
                    be = e;
                }
            }
            if (be > ba) {
                r->sa[r->jtop] = ba;
                r->se[r->jtop] = be;
            }
        }
        /* the lip's own colours: what most pixels of its front row have, then the next */
        for (c = 0; c < W; c++) {
            if (!r->mmask[jl][c]) {
                continue;
            }
            for (g = 0; g < ng && (unsigned)grp[g][0] != rgb_of(pp(r, c, jl)); g++) {
            }
            if (g == ng) {
                grp[ng][0] = (int)rgb_of(pp(r, c, jl));
                grp[ng][1] = c;
                grp[ng][2] = 0;
                ng++;
            }
            grp[g][2]++;
        }
        for (nlip = 0; nlip < ng; nlip++) {     /* most pixels first, the earlier of equals first */
            int best = -1;

            for (g = 0; g < ng; g++) {
                if (grp[g][2] >= 0 && (best < 0 || grp[g][2] > grp[best][2])) {
                    best = g;
                }
            }
            lip_px[nlip][0] = grp[best][1];
            lip_px[nlip][1] = jl;
            grp[best][2] = -1;
        }
    }
    for (j = 0; j < H; j++) {
        if (r->has[j] && r->se[j] - r->sa[j] > rmax) {
            rmax = r->se[j] - r->sa[j];
        }
    }
    r->jlast = r->rowlast;              /* every row as the picture has it */
    R = (W + 1) / 2 + 1;
    D = 2 * R;
    /* `depth=J0:J1:F:O+..`: rows J0..J1 are F as deep as they are wide, and O pixels south of the middle */
    for (j = 0; j < MAXPX; j++) {
        dfac[j] = 1.0;
        doff[j] = 0.0;
    }
    if (rm_opt(opt, "depth", ob, sizeof ob) != NULL) {
        const char *dp = ob;

        while (dp != NULL && *dp != '\0') {
            int ja, jb;
            double f, o;

            if (sscanf(dp, "%d:%d:%lf:%lf", &ja, &jb, &f, &o) == 4 && f > 0.05) {
                for (j = ja < 0 ? 0 : ja; j <= jb && j < MAXPX; j++) {
                    dfac[j] = f;
                    doff[j] = o;
                }
            }
            dp = strchr(dp, '+');
            dp = dp != NULL ? dp + 1 : NULL;
        }
    }
    vox = (unsigned char *)calloc((size_t)H * D * W, 1);
    colp = (short *)calloc((size_t)H * D * W * 2u, sizeof *colp);
    if (vox == NULL || colp == NULL) {
        goto done;
    }
    wrap = rm_opt(opt, "wrap", wrapb, sizeof wrapb);
    lathe = wrap != NULL && strcmp(wrap, "lathe") == 0;
    nboss = lathe ? rm_colours(rm_opt(opt, "boss", ob, sizeof ob), boss, 8) : 0;
    per = 2.0 * PI / rm_optf(opt, "around", 6);
    has_reach = rm_opt(opt, "reach", ob, sizeof ob) != NULL;
    reach = rm_optf(opt, "reach", 0);
    has_bump = rm_opt(opt, "bump", ob, sizeof ob) != NULL;
    bump = rm_optf(opt, "bump", 0);
    fat = rm_opt(opt, "fat", ob, sizeof ob) != NULL;
#define BOSS_END(jj, res) do { \
        int x_; \
        res = 0; \
        for (x_ = r->sa[jj]; x_ < r->sa[jj] + 3 && x_ < r->se[jj]; x_++) { \
            if (rm_has_colour(boss, nboss, pp(r, x_, jj))) { res = 1; } \
        } \
    } while (0)
    if (nboss > 0) {
        for (j = r->jtop; j <= r->jlast; j++) {
            int be;

            if (!r->has[j]) {
                continue;
            }
            BOSS_END(j, be);
            if (!be && (r->se[j] - r->sa[j]) / 2.0 > rcap) {
                rcap = (r->se[j] - r->sa[j]) / 2.0;
            }
        }
    }
    for (j = r->jtop; j <= r->jlast; j++) {
        int a = r->sa[j], e = r->se[j];
        double cc = (a + e) / 2.0, rr = (e - a) / 2.0;

        if (!r->has[j]) {
            continue;
        }
        cen[j] = cc;
        rbase[j] = rr;
        if (nboss > 0) {
            /* `boss` (a turned thing): the bosses stand out from the surface.  In the picture a boss at the
             * thing's side sticks out of its outline: the rows whose ends are boss-coloured are that much
             * wider than the body itself; where the wrapped picture has a boss the surface comes out (in
             * full at the boss's middle colour, less toward its rim: the colours are given rim to middle). */
            int be;
            double rb, outp;

            BOSS_END(j, be);
            rb = be ? (rr < rcap ? rr : rcap) : rr;
            if (has_bump && be) {
                rb = fat ? rr : (rr - bump > 1.0 ? rr - bump : 1.0);
            }
            rbase[j] = rb;
            outp = fat && be ? bump : rr - rb;
            for (k = 0; k < D; k++) {
                for (c = 0; c < W; c++) {
                    double ii = c + 0.5 - cc, kk = k + 0.5 - R, fo, off, wgt = 0.0, lim;
                    int colx;
                    unsigned q, v;

                    fo = fold(atan2(ii, kk), per);
                    off = rb * sin(fo);
                    if (has_reach) {
                        off = off < -reach ? -reach : off > reach ? reach : off;
                    }
                    colx = (int)floor(cc + off);
                    colx = colx < a ? a : colx > e - 1 ? e - 1 : colx;
                    v = rgb_of(pp(r, colx, j));
                    for (q = 0; q < nboss; q++) {
                        if (boss[q] == v) {
                            wgt = (q + 1.0) / nboss;
                        }
                    }
                    lim = rb + outp * wgt;
                    VOX(j, k, c) = (unsigned char)(ii * ii + kk * kk <= lim * lim + 0.25);
                }
            }
        } else {
            for (k = 0; k < D; k++) {
                for (c = a; c < e; c++) {
                    /* (`depth`: the slice is that much as deep as wide, and that far south: a statue's ears are
                     * thin, its head less deep than its body, its feet before it) */
                    double ii = c + 0.5 - cc, kk = (k + 0.5 - R - doff[j]) / dfac[j];

                    VOX(j, k, c) = (unsigned char)(ii * ii + kk * kk <= rr * rr + 0.25);
                }
            }
        }
    }
    /* the hand-drawn faces, under the picture */
    facesn = rm_opt(opt, "faces", ob, sizeof ob);
    if (facesn != NULL) {
        const char *const *lines = strcmp(facesn, "cave") == 0 ? k_rock_faces_cave : strcmp(facesn, "open") == 0 ? k_rock_faces_open
                                   : strcmp(facesn, "water") == 0 ? k_rock_faces_water
                                   : strcmp(facesn, "plant") == 0 ? k_rock_faces_plant : NULL;

        if (lines != NULL && faces_load(r, lines)) {
            r->HH = 4 * H;
            face_row[1] = H; face_row[2] = 2 * H; face_row[3] = 3 * H;
        }
    }
    /* each block's pixel */
    for (j = 0; j < H; j++) {
        for (k = 0; k < D; k++) {
            for (c = 0; c < W; c++) {
                double ii, kk, col;
                int side, pi, pj;
                short *cp = &colp[(((size_t)j * D + (size_t)k) * W + (size_t)c) * 2u];

                if (!VOX(j, k, c)) {
                    continue;
                }
                ii = c + 0.5 - cen[j];
                kk = lathe ? k + 0.5 - R : (k + 0.5 - R - doff[j]) / dfac[j];      /* (as a round slice: `depth`) */
                side = fabs(kk) >= fabs(ii) ? (kk > 0 ? 0 : 2) : (ii > 0 ? 1 : 3);
                if (lathe) {
                    /* a thing turned on a wheel: the same all round, its picture's middle repeated `around`
                     * times; a block at an angle takes the column the picture shows at that angle on a round
                     * body, with the angle folded into one repeat; `reach`: only the middle is repeated */
                    double fphi = fold(atan2(ii, kk), per), off;

                    off = rbase[j] * sin(fphi);
                    if (has_reach) {
                        off = off < -reach ? -reach : off > reach ? reach : off;
                    }
                    col = cen[j] + off;
                } else if (fabs(kk) >= fabs(ii)) {
                    /* one rock, fixed in the world: the south and north sides read as the picture ... */
                    col = cen[j] + (kk > 0 ? ii : -ii);
                } else {
                    col = cen[j] + (ii > 0 ? kk : -kk);         /* ... the east and west sides as its mirror */
                }
                pix(r, j, (int)floor(col), &pi, &pj);
                if (nlip > 0 && j < r->jtop + lip_n) {
                    int q = j - r->jtop < nlip - 1 ? j - r->jtop : nlip - 1;

                    pi = lip_px[q][0];
                    pj = lip_px[q][1];
                }
                if (face_row[1]) {
                    pj += face_row[side];
                }
                cp[0] = (short)pi;
                cp[1] = (short)pj;
            }
        }
    }
    /*
     * `bunch=flowers`: a bunch of flowers.  The body's blocks take their colours from blooms set on its surface
     * (rock_faces.c: where each is, its kind, its size), not from a flat picture a side: a bloom goes on round a
     * corner and over the top's edge, so the sides and the top are one surface.  A block is its nearest bloom's
     * (by the blooms' sizes): light where it lies over the bloom's middle, darker under it, the bloom's darkest
     * where two blooms meet; the body's last rows are leaves.  The colours are the picture's own (a kind's ramp,
     * light to dark).  With `front=1` the faces that look south stay the picture.
     */
    if (rm_opt(opt, "bunch", ob, sizeof ob) != NULL && strcmp(ob, "flowers") == 0) {
        static const unsigned ramp_o[4] = { 0xFFBD83, 0xFFA45A, 0xFF8B39, 0xDE7B31 };
        static const unsigned ramp_y[3] = { 0xFFE65A, 0xEED541, 0xDEBD29 };
        static const unsigned ramp_p[5] = { 0xFFC59C, 0xFFBD83, 0xFFAC6A, 0xFFA45A, 0xFF8B39 };
        static const unsigned leaf[2] = { 0x529431, 0x62AC39 };
        const RockBloom *bl;
        double bx[64], by[64], bz[64], brad[64];
        char bkind[64];
        int nbl = 0;

        for (bl = k_bunch_flowers; bl->kind != 0 && nbl < 64; bl++) {
            int jr = (int)floor(bl->row);
            double rr, a = bl->angle * PI / 180.0;

            jr = jr < r->jtop ? r->jtop : jr > r->jlast ? r->jlast : jr;
            rr = (r->se[jr] - r->sa[jr]) / 2.0 * bl->out;
            bx[nbl] = cen[jr] + rr * sin(a);
            bz[nbl] = R + rr * cos(a);            /* (+k is south: 0 degrees) */
            by[nbl] = bl->row;
            brad[nbl] = bl->rad;
            bkind[nbl] = bl->kind;
            nbl++;
        }
        for (j = 0; j < H && nbl > 0; j++) {
            for (k = 0; k < D; k++) {
                for (c = 0; c < W; c++) {
                    short *cp = &colp[(((size_t)j * D + (size_t)k) * W + (size_t)c) * 2u];
                    double d1 = 1e30, d2 = 1e30, t, up, v;
                    int i1 = 0, q, y2, x2, found = 0;
                    unsigned want;

                    if (!VOX(j, k, c)) {
                        continue;
                    }
                    for (q = 0; q < nbl; q++) {
                        const double dx = c + 0.5 - bx[q], dy = j + 0.5 - by[q], dz = k + 0.5 - bz[q];
                        const double dn = sqrt(dx * dx + dy * dy + dz * dz) / brad[q];

                        if (dn < d1) {
                            d2 = d1;
                            d1 = dn;
                            i1 = q;
                        } else if (dn < d2) {
                            d2 = dn;
                        }
                    }
                    t = d1;
                    up = (by[i1] - (j + 0.5)) / brad[i1];       /* (over the bloom's middle: lighter) */
                    v = t - 0.35 * up;
                    if (j >= r->jlast - 1 || (j == r->jlast - 2 && (c * 7 + k * 13) % 3 == 0)) {
                        want = leaf[(c + k) & 1];
                    } else if (bkind[i1] == 'Y') {
                        want = d2 - d1 < 0.12 ? ramp_y[2] : ramp_y[v < 0.5 ? 0 : v < 1.0 ? 1 : 2];
                    } else if (bkind[i1] == 'P') {
                        want = d2 - d1 < 0.14 ? ramp_p[4] : ramp_p[v < 0.3 ? 0 : v < 0.55 ? 1 : v < 0.8 ? 2 : 3];
                    } else {
                        want = d2 - d1 < 0.14 ? ramp_o[3] : ramp_o[v < 0.3 ? 0 : v < 0.6 ? 1 : 2];
                    }
                    for (y2 = 0; y2 < H && !found; y2++) {
                        for (x2 = 0; x2 < W; x2++) {
                            if (r->mask[y2][x2] && rgb_of(pp(r, x2, y2)) == want) {
                                cp[0] = (short)x2;
                                cp[1] = (short)y2;
                                found = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    /* The body sits in the ground by half of what it has under its widest row: standing on its narrow foot
     * alone it hung over its shadow.  (`sink`, in rows: a vase stands on its foot, 0.) */
    jwide = r->jtop;
    for (j = r->jtop; j <= r->jlast; j++) {
        if (r->has[j] && r->se[j] - r->sa[j] >= rmax - 1) {
            jwide = j;
        }
    }
    sink = 0.5 * (r->jlast - jwide) * sv;
    if (rm_opt(opt, "sink", ob, sizeof ob) != NULL) {
        sink = rm_optf(opt, "sink", 0) * sv;
    }
#define X_OF(ii) (foot[0] + ((ii) - W / 2.0) * s)
#define Y_OF(jj) (foot[1] + (r->jlast + 1 - (jj)) * sv - sink)       /* the top of row jj */
#define Z_OF(kk) (foot[2] + ((kk) - R) * s)                           /* +z is south */
    /* the faces of each of the six directions */
    rim_n = (int)rm_optf(opt, "rim", 2);
    if (rm_opt(opt, "toppic", ob, sizeof ob) != NULL) {
        sscanf(ob, "%d:%d", &top_j0, &top_j1);
    }
    memset(nplane, 0, sizeof nplane);
#define ADD(kind_, plane_, a_, b_, pi_, pj_) do { \
        int q_; \
        for (q_ = 0; q_ < nplane[kind_] && planes[kind_][q_] != (plane_); q_++) { } \
        if (q_ == nplane[kind_]) { planes[kind_][nplane[kind_]++] = (plane_); } \
        cell_add(r, kind_, plane_, a_, b_, pi_, pj_); \
    } while (0)
    for (j = 0; j < H; j++) {
        for (k = 0; k < D; k++) {
            for (c = 0; c < W; c++) {
                const short *cp = &colp[(((size_t)j * D + (size_t)k) * W + (size_t)c) * 2u];

                if (!VOX(j, k, c)) {
                    continue;
                }
                if (!FILLED(j, k + 1, c)) {
                    /* (`front=1`: a face that looks south is the picture's own pixel there, whichever side its
                     * block belongs to: seen from the south the body is the picture, to its last column) */
                    if (front && r->mask[j][c]) {
                        ADD(SOUTH, k, c, j, c, j);
                    } else {
                        ADD(SOUTH, k, c, j, cp[0], cp[1]);
                    }
                }
                if (!FILLED(j, k - 1, c)) ADD(NORTH, k, c, j, cp[0], cp[1]);
                if (!FILLED(j, k, c + 1)) ADD(EAST, c, k, j, cp[0], cp[1]);
                if (!FILLED(j, k, c - 1)) ADD(WEST, c, k, j, cp[0], cp[1]);
                if (!FILLED(j - 1, k, c)) {
                    if (mouth_n > 0 && j == r->jtop) {
                        /* the opening, flat on the top slice: the picture's mouth rows from the back (its first
                         * row) to the front, each row spread over the top's own width there */
                        double r_ = (r->se[j] - r->sa[j]) / 2.0, tt = (k + 0.5 - (R - r_)) / (2.0 * r_), half_, fr_;
                        int mj, mc[MAXPX], nmc = 0, x;

                        tt = tt < 0.0 ? 0.0 : tt > 0.999 ? 0.999 : tt;
                        mj = mouth_j0 + (int)(tt * (mouth_n + rim_n));
                        for (x = 0; x < W; x++) {
                            if (r->mmask[mj][x]) {
                                mc[nmc++] = x;
                            }
                        }
                        half_ = r_ * r_ - (k + 0.5 - R) * (k + 0.5 - R);
                        half_ = sqrt(half_ > 0.25 ? half_ : 0.25);
                        fr_ = ((c + 0.5 - cen[j]) + half_) / (2.0 * half_);
                        fr_ = fr_ < 0.0 ? 0.0 : fr_ > 0.999 ? 0.999 : fr_;
                        if (nmc > 0) {
                            ADD(TOP, j, c, k, mc[(int)(fr_ * nmc)], mj);
                        }
                    } else if (top_j1 >= top_j0 && top_j0 >= 0) {
                        /* `toppic=J0:J1`: seen from above the body is the picture too, laid flat: its rows J0..J1
                         * from the back to the front over the body's depth, a face its own column's pixel (the
                         * nearest of the row's) */
                        double tt = (k + 0.5 - (R - rmax / 2.0)) / (rmax > 0 ? rmax : 1);
                        int tj, tc, bd = 1 << 30, x;

                        tt = tt < 0.0 ? 0.0 : tt > 0.999 ? 0.999 : tt;
                        tj = top_j0 + (int)(tt * (top_j1 - top_j0 + 1));
                        tj = tj >= H ? H - 1 : tj;
                        for (x = 0, tc = -1; x < W; x++) {
                            if (r->mask[tj][x] && abs(x - c) < bd) {
                                bd = abs(x - c);
                                tc = x;
                            }
                        }
                        if (tc >= 0) {
                            ADD(TOP, j, c, k, tc, tj);
                        } else {
                            ADD(TOP, j, c, k, cp[0], cp[1]);
                        }
                    } else {
                        ADD(TOP, j, c, k, cp[0], cp[1]);
                    }
                }
                if (j != r->jlast && !FILLED(j + 1, k, c)) ADD(UNDER, j, c, k, cp[0], cp[1]);
            }
        }
    }
    if (r->fail || r->ncell == 0) {
        goto done;
    }
    lay = (Layer *)calloc((size_t)(nplane[0] + nplane[1] + nplane[2] + nplane[3] + nplane[4] + nplane[5] + 2), sizeof *lay);
    if (lay == NULL) {
        goto done;
    }
    for (n = 0; n < 6; n++) {
        for (k = 0; k < nplane[kinds[n]]; k++) {
            lay[nlay].kind = kinds[n];
            lay[nlay].plane = planes[kinds[n]][k];
            nlay++;
        }
    }
    /* the shadow, and a rock in water's ripple */
    shadow_opt = rm_opt(opt, "shadow", shb, sizeof shb);
    if (nkey > 0 && !(shadow_opt != NULL && strcmp(shadow_opt, "0") == 0)) {
        int spx = -1, spy = -1, wide = -1;

        for (j = 0; j < r->HH && spx < 0; j++) {
            for (c = 0; c < W && spx < 0; c++) {
                if (pp(r, c, j)[3] >= 128 && rgb_of(pp(r, c, j)) == keys[0]) {
                    spx = c;
                    spy = j;
                }
            }
        }
        if (spx >= 0) {
            const double K = 0.18;      /* battle units a map unit (BMAP_PLACE_K) */
            const double th = 11.25 * PI / 180.0, ox_s = -0.25 / K, oz_s = -0.375 / K;

            for (j = 0; j < H; j++) {
                if (r->has[j] && (wide < 0 || r->se[j] - r->sa[j] > r->se[wide] - r->sa[wide])) {
                    wide = j;
                }
            }
            shc = (r->sa[wide] + r->se[wide]) / 2.0;
            /* every block thrown on the ground, top and bottom, as the game throws a Pokemon's sprite */
            for (j = 0; j < H; j++) {
                for (k = 0; k < D; k++) {
                    for (c = 0; c < W; c++) {
                        int q;

                        if (!VOX(j, k, c)) {
                            continue;
                        }
                        for (q = 0; q < 2; q++) {
                            double hh = Y_OF(j + q) - foot[1];

                            hh = hh > 0.0 ? hh : 0.0;
                            cell_add(r, SHADOW, 0, (int)floor(c + 0.5 + (ox_s - hh * sin(th)) / s),
                                     (int)floor(k + 0.5 + (oz_s - hh * 0.995 * cos(th)) / s), spx, spy);
                        }
                    }
                }
            }
            shadow_layer = nlay;
            lay[nlay].kind = SHADOW;
            nlay++;
            if (rm_optf(opt, "ripple", 0) != 0.0) {
                /* the picture has the water's ripple painted round the rock's lower half (a line of foam, then
                 * the water's blues, the `key` colours from the rock outward): it lies flat on the water round
                 * the body, a band a colour, in the picture's own pixels; the foam also a block under the
                 * body's edge, so no water shows between the two */
                double bw = rm_optf(opt, "ripple", 1), rb = rmax / 2.0;
                int kp[8][2], rch = (int)(rb + bw * nkey) + 2;
                unsigned q;

                for (q = 0; q < nkey; q++) {
                    kp[q][0] = spx;
                    kp[q][1] = spy;
                    for (j = 0, n = 0; j < r->HH && !n; j++) {
                        for (c = 0; c < W && !n; c++) {
                            if (pp(r, c, j)[3] >= 128 && rgb_of(pp(r, c, j)) == keys[q]) {
                                kp[q][0] = c;
                                kp[q][1] = j;
                                n = 1;
                            }
                        }
                    }
                }
                for (k = R - rch; k < R + rch; k++) {
                    for (c = (int)shc - rch; c < (int)shc + rch + 1; c++) {
                        double dist = hypot(c + 0.5 - shc, k + 0.5 - R);
                        int band = (int)floor((dist - rb) / bw);

                        if (band >= -2 && band < (int)nkey) {
                            cell_add(r, RING, 0, c, k, kp[band > 0 ? band : 0][0], kp[band > 0 ? band : 0][1]);
                        }
                    }
                }
                ring_layer = nlay;
                lay[nlay].kind = RING;
                nlay++;
            }
        }
    }
    if (r->fail) {
        goto done;
    }
    /* each layer's box and its cells */
    for (n = 0; n < nlay; n++) {
        Layer *l = &lay[n];
        int a0 = 1 << 30, b0 = 1 << 30, a1 = -(1 << 30), b1 = -(1 << 30);

        for (i = 0; i < r->ncell; i++) {
            const Cell *q = &r->cell[i];

            if (q->kind == l->kind && q->plane == l->plane) {
                a0 = q->a < a0 ? q->a : a0; a1 = q->a > a1 ? q->a : a1;
                b0 = q->b < b0 ? q->b : b0; b1 = q->b > b1 ? q->b : b1;
            }
        }
        if (a1 < a0) {
            goto done;
        }
        l->a0 = a0; l->b0 = b0; l->bw = a1 + 1 - a0; l->bh = b1 + 1 - b0;
        l->grid = (int *)calloc((size_t)l->bw * (size_t)l->bh, sizeof(int));
        if (l->grid == NULL) {
            goto done;
        }
        for (i = 0; i < r->ncell; i++) {
            const Cell *q = &r->cell[i];

            if (q->kind == l->kind && q->plane == l->plane) {
                l->grid[(size_t)(q->b - b0) * (size_t)l->bw + (size_t)(q->a - a0)] = 1 + q->pi + (q->pj << 8);
            }
        }
    }
    /* the layers' pictures packed in rows, the tallest first, a gap between them */
    order = (int *)malloc((size_t)nlay * sizeof *order);
    if (order == NULL) {
        goto done;
    }
    for (n = 0; n < nlay; n++) {
        order[n] = n;
    }
    for (n = 1; n < nlay; n++) {        /* (equals keep their order) */
        int v = order[n];

        for (k = n; k > 0 && lay[order[k - 1]].bh < lay[v].bh; k--) {
            order[k] = order[k - 1];
        }
        order[k] = v;
    }
    {
        int x = GUT, y = GUT, rowh = 0;

        for (n = 0; n < nlay; n++) {
            Layer *l = &lay[order[n]];

            if (x + l->bw + GUT > AW) {
                x = GUT;
                y += rowh + GUT;
                rowh = 0;
            }
            l->ox = x;
            l->oy = y;
            x += l->bw + GUT;
            rowh = l->bh > rowh ? l->bh : rowh;
        }
        for (AH = 1; AH < y + rowh + GUT; AH *= 2) {
        }
    }
    if (AH > 2048 || out->ntex >= 8) {
        goto done;
    }
    atlas = (unsigned char *)calloc((size_t)AW * AH, 4);
    filled = (unsigned char *)calloc((size_t)AW * AH, 1);
    isnew = (unsigned char *)calloc((size_t)AW * AH, 4);
    if (atlas == NULL || filled == NULL || isnew == NULL) {
        goto done;
    }
    for (n = 0; n < nlay; n++) {
        const Layer *l = &lay[n];
        int a, b;

        for (b = 0; b < l->bh; b++) {
            for (a = 0; a < l->bw; a++) {
                int g = l->grid[(size_t)b * l->bw + a];

                if (g != 0 && l->oy + b < AH && l->ox + a < AW) {
                    const unsigned char *p = pp(r, (g - 1) & 255, (g - 1) >> 8);
                    size_t at = (size_t)(l->oy + b) * AW + (size_t)(l->ox + a);

                    atlas[at * 4u] = p[0]; atlas[at * 4u + 1u] = p[1]; atlas[at * 4u + 2u] = p[2];
                    filled[at] = 1;
                }
            }
        }
    }
    /* every other pixel takes its filled neighbours' colour, outward step by step */
    for (n = 0; n < AW; n++) {
        int any = 0, x, y;

        for (y = 0; y < AH; y++) {
            for (x = 0; x < AW; x++) {
                size_t at = (size_t)y * AW + (size_t)x;
                int acc[3] = { 0, 0, 0 }, cnt = 0, q;

                if (filled[at]) {
                    continue;
                }
                if (y > 0 && filled[at - AW]) { for (q = 0; q < 3; q++) acc[q] += atlas[(at - AW) * 4u + q]; cnt++; }
                if (y + 1 < AH && filled[at + AW]) { for (q = 0; q < 3; q++) acc[q] += atlas[(at + AW) * 4u + q]; cnt++; }
                if (x > 0 && filled[at - 1]) { for (q = 0; q < 3; q++) acc[q] += atlas[(at - 1) * 4u + q]; cnt++; }
                if (x + 1 < AW && filled[at + 1]) { for (q = 0; q < 3; q++) acc[q] += atlas[(at + 1) * 4u + q]; cnt++; }
                if (cnt > 0) {
                    isnew[at * 4u] = (unsigned char)(acc[0] / cnt);
                    isnew[at * 4u + 1u] = (unsigned char)(acc[1] / cnt);
                    isnew[at * 4u + 2u] = (unsigned char)(acc[2] / cnt);
                    isnew[at * 4u + 3u] = 1;
                    any = 1;
                }
            }
        }
        if (!any) {
            break;
        }
        for (i = 0; i < (unsigned)(AW * AH); i++) {
            if (isnew[(size_t)i * 4u + 3u]) {
                memcpy(&atlas[(size_t)i * 4u], &isnew[(size_t)i * 4u], 3u);
                isnew[(size_t)i * 4u + 3u] = 0;
                filled[i] = 1;
            }
        }
    }
    for (i = 0; i < (unsigned)(AW * AH); i++) {
        atlas[(size_t)i * 4u + 3u] = 255;
    }
    /* the shadow's and the ripple's pictures: clear where they are not (and a pixel round them) */
    for (n = 0; n < 2; n++) {
        int li = n == 0 ? shadow_layer : ring_layer, a, b;
        const Layer *l;

        if (li < 0) {
            continue;
        }
        l = &lay[li];
        if (n == 0 && l->bw * l->bh <= 1) {
            continue;
        }
        for (b = l->oy - 1; b < l->oy + l->bh + 1; b++) {
            for (a = l->ox - 1; a < l->ox + l->bw + 1; a++) {
                if (a >= 0 && b >= 0 && a < AW && b < AH) {
                    atlas[((size_t)b * AW + (size_t)a) * 4u + 3u] = 0;
                }
            }
        }
        for (b = 0; b < l->bh; b++) {
            for (a = 0; a < l->bw; a++) {
                if (l->grid[(size_t)b * l->bw + a] != 0) {
                    atlas[((size_t)(l->oy + b) * AW + (size_t)(l->ox + a)) * 4u + 3u] = 255;
                }
            }
        }
    }
    /* the faces: each layer as few rectangles that cover its cells exactly */
    lift = foot[1] + 0.3;               /* the height the battle lays flat pictures at (DECAL_LIFT) */
    for (n = 0; n < nlay && !out->fail; n++) {
        Layer *l = &lay[n];
        int a, b;

        if (l->kind == SHADOW || l->kind == RING) {
            /* one flat face: the shadow under the Pokemon's light, see-through, keeping the battle's
             * directions; the ripple on the water, its picture clear where the ripple is not */
            double ua2 = l->ox / (double)AW, ub2 = (l->ox + l->bw) / (double)AW;
            double va2 = l->oy / (double)AH, vb2 = (l->oy + l->bh) / (double)AH;
            double xa = X_OF(l->a0), xb = X_OF(l->a0 + l->bw), za = Z_OF(l->b0), zb = Z_OF(l->b0 + l->bh);
            double yy = l->kind == SHADOW ? lift + 0.15 : lift;

            face(out, src, bt->rgb, xa, yy, zb, xb, yy, zb, xb, yy, za, xa, yy, za, ua2, vb2, ub2, vb2, ub2, va2, ua2, va2,
                 l->kind == SHADOW);
            continue;
        }
        for (b = 0; b < l->bh; b++) {
            for (a = 0; a < l->bw; a++) {
                int a1 = a, b1 = b, x, y, all;
                double u0, u1, v0, v1;
                int qa, qb, ea, eb;

                if (l->grid[(size_t)b * l->bw + a] == 0) {
                    continue;
                }
                while (a1 + 1 < l->bw && l->grid[(size_t)b * l->bw + a1 + 1] != 0) {
                    a1++;
                }
                for (;;) {
                    all = b1 + 1 < l->bh;
                    for (x = a; all && x <= a1; x++) {
                        all = l->grid[(size_t)(b1 + 1) * l->bw + x] != 0;
                    }
                    if (!all) {
                        break;
                    }
                    b1++;
                }
                for (y = b; y <= b1; y++) {
                    for (x = a; x <= a1; x++) {
                        l->grid[(size_t)y * l->bw + x] = 0;
                    }
                }
                qa = l->a0 + a; qb = l->b0 + b; ea = l->a0 + a1 + 1; eb = l->b0 + b1 + 1;
                u0 = (l->ox + a) / (double)AW; u1 = (l->ox + a1 + 1) / (double)AW;
                v0 = (l->oy + b) / (double)AH; v1 = (l->oy + b1 + 1) / (double)AH;
                switch (l->kind) {
                case SOUTH: {
                    double z = Z_OF(l->plane + 1);

                    face(out, src, bt->rgb, X_OF(qa), Y_OF(eb), z, X_OF(ea), Y_OF(eb), z, X_OF(ea), Y_OF(qb), z, X_OF(qa), Y_OF(qb), z,
                         u0, v1, u1, v1, u1, v0, u0, v0, 0);
                    break;
                }
                case NORTH: {
                    double z = Z_OF(l->plane);

                    face(out, src, bt->rgb, X_OF(ea), Y_OF(eb), z, X_OF(qa), Y_OF(eb), z, X_OF(qa), Y_OF(qb), z, X_OF(ea), Y_OF(qb), z,
                         u1, v1, u0, v1, u0, v0, u1, v0, 0);
                    break;
                }
                case EAST: {
                    double xx = X_OF(l->plane + 1);

                    face(out, src, bt->rgb, xx, Y_OF(eb), Z_OF(ea), xx, Y_OF(eb), Z_OF(qa), xx, Y_OF(qb), Z_OF(qa), xx, Y_OF(qb), Z_OF(ea),
                         u1, v1, u0, v1, u0, v0, u1, v0, 0);
                    break;
                }
                case WEST: {
                    double xx = X_OF(l->plane);

                    face(out, src, bt->rgb, xx, Y_OF(eb), Z_OF(qa), xx, Y_OF(eb), Z_OF(ea), xx, Y_OF(qb), Z_OF(ea), xx, Y_OF(qb), Z_OF(qa),
                         u0, v1, u1, v1, u1, v0, u0, v0, 0);
                    break;
                }
                case TOP: {
                    double yy = Y_OF(l->plane);

                    face(out, src, bt->rgb, X_OF(qa), yy, Z_OF(eb), X_OF(ea), yy, Z_OF(eb), X_OF(ea), yy, Z_OF(qb), X_OF(qa), yy, Z_OF(qb),
                         u0, v1, u1, v1, u1, v0, u0, v0, 0);
                    break;
                }
                default: {
                    double yy = Y_OF(l->plane + 1);

                    face(out, src, bt->rgb, X_OF(qa), yy, Z_OF(qb), X_OF(ea), yy, Z_OF(qb), X_OF(ea), yy, Z_OF(eb), X_OF(qa), yy, Z_OF(eb),
                         u0, v0, u1, v0, u1, v1, u0, v1, 0);
                    break;
                }
                }
            }
        }
    }
    if (out->fail || out->ntri == first_tri) {
        goto done;
    }
    out->tex[out->ntex].w = AW;
    out->tex[out->ntex].h = AH;
    out->tex[out->ntex].rgba = atlas;
    atlas = NULL;
    /* (the new triangles name the texture by its place in the output) */
    for (i = first_tri; i < out->ntri; i++) {
        out->tri[i].tex = out->ntex;
    }
    out->ntex++;
    made = 1;
done:
    for (n = 0; lay != NULL && n < nlay; n++) {
        free(lay[n].grid);
    }
    free(lay);
    free(order);
    free(atlas);
    free(filled);
    free(isnew);
    free(vox);
    free(colp);
    free(r->pic);
    free(r->cell);
    free(r);
    return made;
#undef VOX
#undef FILLED
#undef ADD
#undef X_OF
#undef Y_OF
#undef Z_OF
#undef BOSS_END
}

/* Is corner p a corner of a triangle other than the pair (pb, pi), (pb, pi + 1)? */
static int shared(const RmBatch *b, unsigned nb, unsigned pb, unsigned pi, const float *p)
{
    unsigned bi, i;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; i < b[bi].n; i++) {
            if (bi == pb && (i == pi || i == pi + 1u)) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                const float *q = b[bi].tri[i].p[k];

                if (fabs(q[0] - p[0]) < 0.3 && fabs(q[1] - p[1]) < 0.3 && fabs(q[2] - p[2]) < 0.3) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void rm_rock(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    char lb[8], ob2[512];
    int lean = rm_opt(opt, "lean", lb, sizeof lb) != NULL, cards = rm_opt(opt, "cards", lb, sizeof lb) != NULL;
    unsigned bi;

    /* `centre=1`: a card's body stands over the middle of the rest of the model (a pot, a pedestal), seen
     * from above: the box of every triangle that is no card and no flat sheet on the ground */
    if (cards && rm_optf(opt, "centre", 0) != 0.0) {
        double lo[2] = { 1e30, 1e30 }, hi[2] = { -1e30, -1e30 }, yg = 1e30;
        unsigned i, k;

        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                for (k = 0; k < 3u; k++) {
                    yg = b[bi].tri[i].p[k][1] < yg ? b[bi].tri[i].p[k][1] : yg;
                }
            }
        }
        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                const RmTri *t = &b[bi].tri[i];
                int card = 1, card2 = i > 0, flat = 1;

                /* (a card's two triangles: alone apart from each other, this one and the next or the last) */
                for (k = 0; k < 3u; k++) {
                    card &= !shared(b, nb, bi, i, t->p[k]);
                    card2 &= i > 0 && !shared(b, nb, bi, i - 1u, t->p[k]);
                }
                card |= card2;
                for (k = 0; k < 3u; k++) {
                    flat &= t->p[k][1] < yg + 0.5;
                }
                if (card || flat) {
                    continue;
                }
                for (k = 0; k < 3u; k++) {
                    lo[0] = t->p[k][0] < lo[0] ? t->p[k][0] : lo[0];
                    hi[0] = t->p[k][0] > hi[0] ? t->p[k][0] : hi[0];
                    lo[1] = t->p[k][2] < lo[1] ? t->p[k][2] : lo[1];
                    hi[1] = t->p[k][2] > hi[1] ? t->p[k][2] : hi[1];
                }
            }
        }
        if (lo[0] < hi[0] && strlen(opt) + 64u < sizeof ob2) {
            snprintf(ob2, sizeof ob2, "%s,at=%.3f:%.3f", opt, (lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0);
            opt = ob2;
        }
    }
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];

        if (cards) {
            /* `cards=1`: only a picture card that stands by itself in the model (a plant's leaves over its pot:
             * two triangles that share no corner with the rest), each made a body */
            unsigned i;

            for (i = 0; !bt->bb && i + 1u < bt->n && bt->tex >= 0 && (unsigned)bt->tex < ntex && tex[bt->tex].rgba != NULL; i++) {
                RmBatch one = *bt;
                int k, alone = 1;

                for (k = 0; k < 3 && alone; k++) {
                    alone = !shared(b, nb, bi, i, bt->tri[i].p[k]) && !shared(b, nb, bi, i, bt->tri[i + 1u].p[k]);
                }
                if (!alone) {
                    continue;
                }
                one.tri = &bt->tri[i];
                one.n = 2;
                /* (the card's own colour, not its draw's mean: a pot drawn with it is shaded darker) */
                for (k = 0; k < 3; k++) {
                    one.rgb[k] = (unsigned char)(((unsigned)bt->tri[i].rgb[k] + bt->tri[i + 1u].rgb[k]) / 2u);
                }
                if (build(&one, &tex[bt->tex], opt, out)) {
                    rm_drop(out, bt->tri[i].id);
                    rm_drop(out, bt->tri[i + 1u].id);
                }
                i++;
            }
            continue;
        }
        if ((!bt->bb && !lean) || bt->n < 2 || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL) {
            continue;
        }
        if (build(bt, &tex[bt->tex], opt, out)) {
            rm_drop_all(out, bt);
        }
    }
}
