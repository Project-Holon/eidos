/*
 * engine/world/remade/driftwood.c -- a piece of driftwood lying in a pond
 * (Route 8's and the Moor of Icirrus's), part of a map chunk's own mesh.
 *
 * The map draws it as one flat card two tiles wide, lying back toward the
 * field's steep camera: a 64 x 32 picture ("stake2") of a trunk lying across,
 * broken open at one end, a forked branch standing on it, the water's pale
 * ring under it.  A battle camera low over the water sees the card from its
 * edge: a thin slanted sheet.  Remade, from the card's place and the
 * picture's own pixels:
 *   - the card is left out;
 *   - the trunk is a body lying along the card's width, of eight straight
 *     sides, cut every four columns of the picture (columns 2, 6 .. 62).  A
 *     cut shows the field's camera (53.2 degrees down, the game's default
 *     field camera) the wood the picture has there: its rows of wood in that
 *     column, from the first to the last (the water's ring and the branch
 *     are not the trunk: under the branch, columns 37 to 47, the trunk's top
 *     is the straight line from column 36 to column 48).  The trunk is round
 *     where it is plain (column 50) and as high as that all along: where the
 *     picture has more wood (its forked end: two limbs spread on the water)
 *     the cut is broader, not higher.  It floats a fifth of its half height
 *     over the water (LG_FLOAT), where that camera sees it: each cut's
 *     middle lies on the camera's line through the card;
 *   - it wears the picture as that camera sees it: every point of the body
 *     takes the pixel the camera's line through it meets on the card, so from
 *     the south at the field's angle the body is the picture, and its far
 *     side is the same wood again.  The body has a texture of its own for
 *     this (its two ends, which the camera's line runs along, take one plain
 *     pixel each): the picture with every pixel that is not the trunk's wood (clear
 *     ones, the ring's three colours, the branch) given the nearest wood
 *     pixel of its row within eight columns, else of its column;
 *   - the branch (the wood over the trunk's top in columns 37 to 47) stands
 *     on the trunk leaning 45 degrees back, as long as the camera shows it:
 *     each run of its pixels in a row is a block 1.5 units thick, in the
 *     picture's own pixels front and back, an end pixel on its small faces;
 *   - the ring lies flat on the water round the trunk: two bands, in the
 *     ring's two pale colours from the trunk outward.
 *
 * The draw is the one whose picture is 64 x 32 with the wood at (5, 4),
 * (20, 20) and (43, 7) and the ring at (30, 25): nothing else of the chunk is
 * touched, and a chunk without that picture stays as it is.
 *
 * Settings: y (the water's height; default: the card's lowest corner), ring
 * (a band's width, map units; default 0.8; 0: no ring).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define PI 3.14159265358979323846
#define LG_PITCH 53.2           /* the game's default field camera looks this far down (degrees) */
#define LG_FLOAT 0.2            /* the trunk's middle this much of its radius over the water */
#define LG_W 64
#define LG_H 32
#define LG_TH 48                /* the body's own texture: the picture, then the ring's colours */
#define LG_C0 2                 /* the first cut's column, a cut every LG_STEP columns, LG_N cuts */
#define LG_STEP 4
#define LG_N 16
#define LG_BR0 36               /* the branch stands over the columns between these two */
#define LG_BR1 48
#define LG_BR_THICK 1.5         /* the branch's blocks, map units */
#define LG_PLAIN 50              /* the column of the trunk's plain round part: its height there is its height all along */

typedef struct {
    double o[3], u[3], v[3];    /* the card: a pixel (c, r) lies at o + c u + r v */
    double fwd[3], view[3], up_s[3];    /* toward the camera along the ground; the camera's line of sight; up on its screen */
    double rowseen;             /* a row of the picture as the camera sees it, map units */
    double yw;
    unsigned src;
    unsigned char rgb[3];
    int own;
    unsigned char wood[LG_H][LG_W];
    double top[LG_W], bot[LG_W];        /* a column's trunk: its first row and the row after its last (0, 0: none) */
} Log;

static int lg_is_log(const RmTex *t)
{
    static const struct {
        int x, y;
        unsigned char rgb[3];
    } sig[4] = { { 5, 4, { 0x9C, 0x83, 0x5A } }, { 20, 20, { 0xB4, 0x94, 0x73 } }, { 43, 7, { 0xC5, 0xAC, 0x8B } },
                 { 30, 25, { 0xAC, 0xEE, 0xF6 } } };
    int k;

    if (t == NULL || t->rgba == NULL || t->w != LG_W || t->h != LG_H) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        const unsigned char *px = &t->rgba[((size_t)sig[k].y * LG_W + (size_t)sig[k].x) * 4u];

        if (px[3] < 128 || px[0] != sig[k].rgb[0] || px[1] != sig[k].rgb[1] || px[2] != sig[k].rgb[2]) {
            return 0;
        }
    }
    return 1;
}

static const unsigned char *lg_px(const RmTex *t, int c, int r)
{
    return &t->rgba[((size_t)r * LG_W + (size_t)c) * 4u];
}

/* The picture's wood: a drawn pixel that is none of the ring's three colours. */
static int lg_wood_px(const RmTex *t, int c, int r)
{
    static const unsigned char ring[3][3] = { { 0xCD, 0xFF, 0xFF }, { 0xAC, 0xEE, 0xF6 }, { 0x9C, 0xAC, 0xA4 } };
    const unsigned char *p = lg_px(t, c, r);
    int k;

    if (p[3] < 128) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        if (p[0] == ring[k][0] && p[1] == ring[k][1] && p[2] == ring[k][2]) {
            return 0;
        }
    }
    return 1;
}

/* The trunk's wood, column by column (the branch left out), into g. */
static void lg_read(Log *g, const RmTex *t)
{
    int c, r;

    for (c = 0; c < LG_W; c++) {
        int a = -1, e = -1;

        for (r = 0; r < LG_H; r++) {
            g->wood[r][c] = (unsigned char)lg_wood_px(t, c, r);
            if (g->wood[r][c]) {
                a = a < 0 ? r : a;
                e = r + 1;
            }
        }
        g->top[c] = a < 0 ? 0.0 : (double)a;
        g->bot[c] = a < 0 ? 0.0 : (double)e;
    }
    for (c = LG_BR0 + 1; c < LG_BR1; c++) {
        const double line = g->top[LG_BR0] + (g->top[LG_BR1] - g->top[LG_BR0]) * (double)(c - LG_BR0) / (double)(LG_BR1 - LG_BR0);

        g->top[c] = line < g->top[c] ? g->top[c] : line;
        for (r = 0; r < LG_H && (double)r + 1.0 <= g->top[c]; r++) {
            g->wood[r][c] = (unsigned char)(g->wood[r][c] ? 2 : 0);     /* (2: the branch) */
        }
    }
}

/* The body's own texture (the rules above); NULL: out of memory. */
static unsigned char *lg_texture(const Log *g, const RmTex *t)
{
    unsigned char *tex = (unsigned char *)calloc((size_t)LG_W * LG_TH, 4u);
    int c, r, k;

    if (tex == NULL) {
        return NULL;
    }
    for (r = 0; r < LG_H; r++) {
        for (c = 0; c < LG_W; c++) {
            int sc = -1, sr = r, d;

            if (g->wood[r][c] == 1) {
                sc = c;
            }
            for (d = 1; sc < 0 && d <= 8; d++) {       /* (its row, within eight columns) */
                if (c + d < LG_W && g->wood[r][c + d] == 1) {
                    sc = c + d;
                } else if (c - d >= 0 && g->wood[r][c - d] == 1) {
                    sc = c - d;
                }
            }
            for (d = 1; sc < 0 && d < LG_H; d++) {      /* (its column) */
                if (r + d < LG_H && g->wood[r + d][c] == 1) {
                    sc = c;
                    sr = r + d;
                } else if (r - d >= 0 && g->wood[r - d][c] == 1) {
                    sc = c;
                    sr = r - d;
                }
            }
            if (sc >= 0) {
                unsigned char *d2 = &tex[((size_t)r * LG_W + (size_t)c) * 4u];

                memcpy(d2, lg_px(t, sc, sr), 3u);
                d2[3] = 255;
            }
        }
    }
    /* (a column without wood: its neighbour's; the rows under the picture: its last row's) */
    for (k = 0; k < LG_W; k++) {
        for (r = 0; r < LG_H; r++) {
            for (c = 0; c < LG_W; c++) {
                unsigned char *d2 = &tex[((size_t)r * LG_W + (size_t)c) * 4u];

                if (d2[3] == 0 && c + 1 < LG_W && d2[4 + 3] == 255) {
                    memcpy(d2, d2 + 4, 4u);
                } else if (d2[3] == 0 && c > 0 && d2[-4 + 3] == 255) {
                    memcpy(d2, d2 - 4, 4u);
                }
            }
        }
    }
    for (r = LG_H; r < LG_H + 4; r++) {
        memcpy(&tex[(size_t)r * LG_W * 4u], &tex[(size_t)(LG_H - 1) * LG_W * 4u], (size_t)LG_W * 4u);
    }
    /* the ring's colours: its inner one (20, 25), its outer one (20, 26): blocks 8 x 8 at rows 38 to 45 */
    for (k = 0; k < 2; k++) {
        for (r = 38; r < 46; r++) {
            for (c = 0; c < 8; c++) {
                unsigned char *d2 = &tex[((size_t)r * LG_W + (size_t)(k * 8 + c)) * 4u];

                memcpy(d2, lg_px(t, 20, 25 + k), 3u);
                d2[3] = 255;
            }
        }
    }
    return tex;
}

/* The pixel (c, r) of the picture the camera's line through p meets on the card. */
static void lg_seen(const Log *g, const double p[3], double *c, double *r)
{
    /* p + t view = o + c u + r v:  (-view, u, v) (t, c, r) = p - o */
    double m[3][3], d[3], det, dc, dr;
    int k;

    for (k = 0; k < 3; k++) {
        m[k][0] = -g->view[k];
        m[k][1] = g->u[k];
        m[k][2] = g->v[k];
        d[k] = p[k] - g->o[k];
    }
    det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
          + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-12) {
        *c = *r = 0.0;
        return;
    }
    dc = m[0][0] * (d[1] * m[2][2] - m[1][2] * d[2]) - d[0] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * d[2] - d[1] * m[2][0]);
    dr = m[0][0] * (m[1][1] * d[2] - d[1] * m[2][1]) - m[0][1] * (m[1][0] * d[2] - d[1] * m[2][0])
         + d[0] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    *c = dc / det;
    *r = dr / det;
}

/* One triangle that looks the way `want` points, a little darker by the way it looks (as cup.c and stake.c shade).
 * tex: the body's own texture, its corners taking what the camera sees there; or -1 with the picture's pixels uv. */
static void lg_tri(const Log *g, RmOut *out, int tex, const double a[3], const double b[3], const double c[3], const double uv[3][2],
                   const double want[3])
{
    double e1[3], e2[3], n[3], f, nf, ua[3][2];
    unsigned char rgb[3];
    int i;

    for (i = 0; i < 3; i++) {
        e1[i] = b[i] - a[i];
        e2[i] = c[i] - a[i];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) <= 1e-9) {
        return;
    }
    if (rm_dot(n, want) < 0.0) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }
    nf = rm_dot(n, g->fwd);
    f = n[1] > 0.7 ? 1.0 : n[1] < -0.7 ? 0.55 : nf > 0.5 ? 1.0 : nf < -0.5 ? 0.78 : 0.86;
    for (i = 0; i < 3; i++) {
        rgb[i] = (unsigned char)(g->rgb[i] * f);
    }
    if (uv != NULL) {
        memcpy(ua, uv, sizeof ua);
    } else {
        const double *p3[3] = { a, b, c };

        for (i = 0; i < 3; i++) {
            double cc, rr;

            lg_seen(g, p3[i], &cc, &rr);
            ua[i][0] = cc / LG_W;
            ua[i][1] = rr / LG_TH;
        }
    }
    e1[0] = b[0] - a[0]; e1[1] = b[1] - a[1]; e1[2] = b[2] - a[2];
    e2[0] = c[0] - a[0]; e2[1] = c[1] - a[1]; e2[2] = c[2] - a[2];
    rm_cross(e1, e2, n);
    if (rm_dot(n, want) < 0.0) {
        rm_tri(out, g->src, tex, 1, rgb, a, c, b, ua[0], ua[2], ua[1], 0, 0);
    } else {
        rm_tri(out, g->src, tex, 1, rgb, a, b, c, ua[0], ua[1], ua[2], 0, 0);
    }
}

/* What the camera shows of a cut half as deep as `a` and half as high as `b` that floats LG_FLOAT of b over the water:
 * from its top's edge to its near waterline, map units; *over: how far its middle lies under the middle of that. */
static double lg_extent(double a, double b, double *over)
{
    const double p = LG_PITCH * PI / 180.0;
    const double top = sqrt(a * a * sin(p) * sin(p) + b * b * cos(p) * cos(p));
    const double low = LG_FLOAT * b * cos(p) + a * sqrt(1.0 - LG_FLOAT * LG_FLOAT) * sin(p);

    if (over != NULL) {
        *over = 0.5 * (top - low);
    }
    return top + low;
}

/* The trunk's cut at the column c (it may lie between two columns): its middle, half its depth and half its height;
 * 0 where there is no wood.  hmax: no higher than this (0: a round cut). */
static int lg_cut(const Log *g, double c, double hmax, double mid[3], double *ra, double *rb)
{
    const int c0 = (int)floor(c), c1 = c0 + 1 < LG_W ? c0 + 1 : c0;
    const double w = c - c0, top = g->top[c0] + (g->top[c1] - g->top[c0]) * w, bot = g->bot[c0] + (g->bot[c1] - g->bot[c0]) * w;
    const double seen = (bot - top) * g->rowseen;
    double a, b, lo, hi, over = 0.0, row, on[3], t;
    int k;

    if (bot - top < 0.5 || g->bot[c0] <= g->top[c0]) {
        return 0;
    }
    /* round, as thick as the camera shows it ... */
    a = b = seen / lg_extent(1.0, 1.0, NULL);
    if (hmax > 0.0 && b > hmax) {
        /* ... or no higher than hmax, and as deep as shows the same wood */
        b = hmax;
        lo = b;
        hi = 4.0 * seen;
        for (k = 0; k < 40; k++) {
            a = 0.5 * (lo + hi);
            if (lg_extent(a, b, NULL) < seen) {
                lo = a;
            } else {
                hi = a;
            }
        }
    }
    (void)lg_extent(a, b, &over);
    /* (the row the cut's middle is seen at: under the wood's middle row by what the water hides) */
    row = 0.5 * (top + bot) + over / g->rowseen;
    for (k = 0; k < 3; k++) {
        on[k] = g->o[k] + (c + 0.5) * g->u[k] + row * g->v[k];
    }
    t = (g->yw + LG_FLOAT * b - on[1]) / g->view[1];
    for (k = 0; k < 3; k++) {
        mid[k] = on[k] + t * g->view[k];
    }
    *ra = a;
    *rb = b;
    return 1;
}

static void lg_ring_pt(const Log *g, const double mid[3], double ra, double rb, int i, double p[3])
{
    const double a = (22.5 + 45.0 * i) * PI / 180.0, k8 = 1.0 / cos(22.5 * PI / 180.0);
    int k;

    for (k = 0; k < 3; k++) {
        p[k] = mid[k] + ra * k8 * cos(a) * g->fwd[k];
    }
    p[1] += rb * k8 * sin(a);
}

static void lg_build(Log *g, const RmTex *t, const char *opt, RmOut *out)
{
    static const double up[3] = { 0.0, 1.0, 0.0 };
    const double bw = rm_optf(opt, "ring", 0.8);
    double mid[LG_N][3], rad[LG_N], rbh[LG_N], ulen, uh[3], hmax = 0.0, m0[3], a0 = 0.0;
    int have[LG_N], k, i, q, first = -1, last = -1;

    ulen = sqrt(rm_dot(g->u, g->u));
    for (q = 0; q < 3; q++) {
        uh[q] = g->u[q] / ulen;
    }
    /* (the trunk's own height: its plain round part's, the cut at column LG_PLAIN) */
    if (lg_cut(g, (double)LG_PLAIN, 0.0, m0, &a0, &hmax) == 0) {
        hmax = 0.0;
    }
    for (k = 0; k < LG_N; k++) {
        have[k] = lg_cut(g, (double)(LG_C0 + LG_STEP * k), hmax, mid[k], &rad[k], &rbh[k]);
        if (have[k]) {
            first = first < 0 ? k : first;
            last = k;
        }
    }
    if (first < 0 || last == first) {
        return;
    }
    for (k = first; k <= last; k++) {
        if (!have[k]) {         /* (a cut without wood between two that have it: its neighbour's) */
            memcpy(mid[k], mid[k - 1], sizeof mid[k]);
            for (q = 0; q < 3; q++) {
                mid[k][q] += LG_STEP * g->u[q];
            }
            rad[k] = rad[k - 1];
            rbh[k] = rbh[k - 1];
        }
    }
    /* the trunk's sides, cut to cut */
    for (k = first; k < last; k++) {
        for (i = 0; i < 8; i++) {
            const double am = (22.5 + 45.0 * (i + 0.5)) * PI / 180.0;
            double p[4][3], want[3];

            lg_ring_pt(g, mid[k], rad[k], rbh[k], i, p[0]);
            lg_ring_pt(g, mid[k], rad[k], rbh[k], i + 1, p[1]);
            lg_ring_pt(g, mid[k + 1], rad[k + 1], rbh[k + 1], i + 1, p[2]);
            lg_ring_pt(g, mid[k + 1], rad[k + 1], rbh[k + 1], i, p[3]);
            for (q = 0; q < 3; q++) {
                want[q] = cos(am) * g->fwd[q];
            }
            want[1] += sin(am);
            lg_tri(g, out, g->own, p[0], p[1], p[2], NULL, want);
            lg_tri(g, out, g->own, p[0], p[2], p[3], NULL, want);
        }
    }
    /* its two ends: one plain pixel each (the camera's line runs along an end: its picture there is streaks): the
     * broken end the hollow's dark wood (9, 12), the other the trunk's wood (50, 22) */
    for (k = 0; k < 2; k++) {
        const int s = k == 0 ? first : last;
        double want[3], uv[3][2];

        for (q = 0; q < 3; q++) {
            want[q] = (k == 0 ? -1.0 : 1.0) * uh[q];
            uv[q][0] = (k == 0 ? 9.5 : 50.5) / LG_W;
            uv[q][1] = (k == 0 ? 12.5 : 22.5) / LG_TH;
        }
        for (i = 0; i < 8; i++) {
            double a[3], b[3];

            lg_ring_pt(g, mid[s], rad[s], rbh[s], i, a);
            lg_ring_pt(g, mid[s], rad[s], rbh[s], i + 1, b);
            lg_tri(g, out, g->own, mid[s], a, b, (const double (*)[2])uv, want);
        }
    }
    /* the ring on the water: two bands along each side, and across each end; a little over the water (the height
     * the battle lays flat pictures at) */
    for (i = 0; bw > 0.0 && i < 2; i++) {
        const double yy = g->yw + 0.3;
        double uv[3][2];
        int side;

        for (q = 0; q < 3; q++) {
            uv[q][0] = (i * 8 + 4.0) / LG_W;
            uv[q][1] = 42.0 / LG_TH;
        }
        for (k = first; k < last; k++) {
            for (side = -1; side <= 1; side += 2) {
                double p[4][3];
                int e;

                for (e = 0; e < 2; e++) {
                    const int s = k + e;
                    const double hw = rad[s] * sqrt(1.0 - LG_FLOAT * LG_FLOAT);        /* (the trunk's half width at the water) */

                    for (q = 0; q < 3; q++) {
                        p[e][q] = mid[s][q] + side * (hw + bw * i) * g->fwd[q];
                        p[3 - e][q] = mid[s][q] + side * (hw + bw * (i + 1)) * g->fwd[q];
                    }
                    p[e][1] = p[3 - e][1] = yy;
                }
                lg_tri(g, out, g->own, p[0], p[1], p[2], (const double (*)[2])uv, up);
                lg_tri(g, out, g->own, p[0], p[2], p[3], (const double (*)[2])uv, up);
            }
        }
        for (k = 0; k < 2; k++) {
            const int s = k == 0 ? first : last;
            const double dir = k == 0 ? -1.0 : 1.0, hw = rad[s] * sqrt(1.0 - LG_FLOAT * LG_FLOAT) + bw * (i + 1);
            double p[4][3];

            for (q = 0; q < 3; q++) {
                p[0][q] = mid[s][q] + dir * bw * i * uh[q] - hw * g->fwd[q];
                p[1][q] = mid[s][q] + dir * bw * i * uh[q] + hw * g->fwd[q];
                p[2][q] = mid[s][q] + dir * bw * (i + 1) * uh[q] + hw * g->fwd[q];
                p[3][q] = mid[s][q] + dir * bw * (i + 1) * uh[q] - hw * g->fwd[q];
            }
            p[0][1] = p[1][1] = p[2][1] = p[3][1] = yy;
            lg_tri(g, out, g->own, p[0], p[1], p[2], (const double (*)[2])uv, up);
            lg_tri(g, out, g->own, p[0], p[2], p[3], (const double (*)[2])uv, up);
        }
    }
    /* the branch: each run of its pixels in a row a block, on the trunk's top, leaning back */
    {
        const double cb = 0.5 * (LG_BR0 + 1 + LG_BR1);         /* (its middle column) */
        const double s45 = sqrt(0.5);
        double bmid[3], brad, brb, base[3], lean[3], nb[3], row0, step;
        int r, c;

        if (!lg_cut(g, cb, hmax, bmid, &brad, &brb)) {
            return;
        }
        {
            const int c0 = (int)floor(cb);

            row0 = g->top[c0] + (g->top[c0 + 1] - g->top[c0]) * (cb - c0) + 1.0;      /* (a row inside the trunk) */
        }
        for (q = 0; q < 3; q++) {
            base[q] = bmid[q] - 0.4 * brad * g->fwd[q];       /* (on the trunk's top, behind its middle) */
            lean[q] = -s45 * g->fwd[q];
            nb[q] = s45 * g->fwd[q];
        }
        base[1] += 0.8 * brb;
        lean[1] += s45;
        nb[1] += s45;
        step = g->rowseen / rm_dot(lean, g->up_s);      /* (a row as long as the camera shows it) */
        for (r = 0; r < LG_H; r++) {
            for (c = LG_BR0 + 1; c < LG_BR1; c++) {
                int e = c;
                double p[8][3], uvf[4][2], uvs[3][2];
                static const int fc[6][4] = { { 4, 5, 7, 6 }, { 0, 2, 3, 1 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
                int fi;

                if (g->wood[r][c] != 2) {
                    continue;
                }
                while (e + 1 < LG_BR1 && g->wood[r][e + 1] == 2) {
                    e++;
                }
                /* (corner bits: 1 the run's far end, 2 the row's upper edge, 4 the side toward the camera) */
                for (i = 0; i < 8; i++) {
                    const double col = (i & 1) ? (double)e + 1.0 : (double)c, j = row0 - (double)r - ((i & 2) ? 0.0 : 1.0);
                    const double off = ((i & 4) ? 0.5 : -0.5) * LG_BR_THICK;

                    for (q = 0; q < 3; q++) {
                        p[i][q] = base[q] + (col - cb - 0.5) * g->u[q] + j * step * lean[q] + off * nb[q];
                    }
                }
                uvf[0][0] = (double)c / LG_W;         uvf[0][1] = ((double)r + 1.0) / LG_H;
                uvf[1][0] = ((double)e + 1.0) / LG_W; uvf[1][1] = ((double)r + 1.0) / LG_H;
                uvf[2][0] = ((double)e + 1.0) / LG_W; uvf[2][1] = (double)r / LG_H;
                uvf[3][0] = (double)c / LG_W;         uvf[3][1] = (double)r / LG_H;
                for (fi = 0; fi < 6; fi++) {
                    double want[3], ctr[3], bc[3], uva[3][2], uvb[3][2];

                    for (q = 0; q < 3; q++) {
                        ctr[q] = 0.25 * (p[fc[fi][0]][q] + p[fc[fi][1]][q] + p[fc[fi][2]][q] + p[fc[fi][3]][q]);
                        bc[q] = 0.125 * (p[0][q] + p[1][q] + p[2][q] + p[3][q] + p[4][q] + p[5][q] + p[6][q] + p[7][q]);
                        want[q] = ctr[q] - bc[q];
                    }
                    if (fi < 2) {
                        /* (front and back: the run's own pixels; corners 4 5 7 6 / 0 2 3 1 -> their place in the row) */
                        static const int at[2][4] = { { 0, 1, 2, 3 }, { 0, 3, 2, 1 } };
                        const int *m = at[fi];

                        uva[0][0] = uvf[m[0]][0]; uva[0][1] = uvf[m[0]][1];
                        uva[1][0] = uvf[m[1]][0]; uva[1][1] = uvf[m[1]][1];
                        uva[2][0] = uvf[m[2]][0]; uva[2][1] = uvf[m[2]][1];
                        uvb[0][0] = uvf[m[0]][0]; uvb[0][1] = uvf[m[0]][1];
                        uvb[1][0] = uvf[m[2]][0]; uvb[1][1] = uvf[m[2]][1];
                        uvb[2][0] = uvf[m[3]][0]; uvb[2][1] = uvf[m[3]][1];
                    } else {
                        /* (the small faces: one pixel, the run's end on that side, or its middle) */
                        const int pc = fi == 4 ? c : fi == 5 ? e : (c + e) / 2;

                        for (i = 0; i < 3; i++) {
                            uvs[i][0] = ((double)pc + 0.5) / LG_W;
                            uvs[i][1] = ((double)r + 0.5) / LG_H;
                        }
                        memcpy(uva, uvs, sizeof uva);
                        memcpy(uvb, uvs, sizeof uvb);
                    }
                    lg_tri(g, out, -1, p[fc[fi][0]], p[fc[fi][1]], p[fc[fi][2]], (const double (*)[2])uva, want);
                    lg_tri(g, out, -1, p[fc[fi][0]], p[fc[fi][2]], p[fc[fi][3]], (const double (*)[2])uvb, want);
                }
                c = e;
            }
        }
    }
    (void)t;
}

/* A card of the draw: the triangles t0 and t1.  The driftwood in its place; 0 when the two are no card. */
static int lg_card(const RmTri *t0, const RmTri *t1, const RmTex *tx, const char *opt, int own, RmOut *out)
{
    const double p = LG_PITCH * PI / 180.0;
    double d1[3], d2[3], du1, dv1, du2, dv2, det, n[3], ylo = 1e30, vp, vperp[3];
    Log *g = (Log *)calloc(1, sizeof *g);
    int k, j;

    if (g == NULL) {
        out->fail = 1;
        return 0;
    }
    /* the card's pixels: (u, v) in pixels to a place, from its first triangle */
    du1 = ((double)t0->uv[1][0] - t0->uv[0][0]) * LG_W; dv1 = ((double)t0->uv[1][1] - t0->uv[0][1]) * LG_H;
    du2 = ((double)t0->uv[2][0] - t0->uv[0][0]) * LG_W; dv2 = ((double)t0->uv[2][1] - t0->uv[0][1]) * LG_H;
    det = du1 * dv2 - du2 * dv1;
    if (fabs(det) < 1e-9) {
        free(g);
        return 0;
    }
    for (k = 0; k < 3; k++) {
        d1[k] = (double)t0->p[1][k] - t0->p[0][k];
        d2[k] = (double)t0->p[2][k] - t0->p[0][k];
        g->u[k] = (d1[k] * dv2 - d2[k] * dv1) / det;
        g->v[k] = (d2[k] * du1 - d1[k] * du2) / det;
    }
    for (k = 0; k < 3; k++) {
        g->o[k] = t0->p[0][k] - t0->uv[0][0] * LG_W * g->u[k] - t0->uv[0][1] * LG_H * g->v[k];
    }
    rm_cross(g->u, g->v, n);
    if (rm_norm(n) < 1e-9) {
        free(g);
        return 0;
    }
    if (n[1] < 0.0) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }
    n[1] = 0.0;
    if (rm_norm(n) < 1e-6 || fabs(g->u[1]) > 0.05 * sqrt(rm_dot(g->u, g->u))) {
        free(g);
        return 0;                       /* (a level card, or one whose rows do not lie level: not this one) */
    }
    memcpy(g->fwd, n, sizeof n);
    for (k = 0; k < 3; k++) {
        g->view[k] = -cos(p) * g->fwd[k];
        g->up_s[k] = -sin(p) * g->fwd[k];
    }
    g->view[1] = -sin(p);
    g->up_s[1] = cos(p);
    vp = rm_dot(g->v, g->view);
    for (k = 0; k < 3; k++) {
        vperp[k] = g->v[k] - vp * g->view[k];
    }
    g->rowseen = sqrt(rm_dot(vperp, vperp));
    for (j = 0; j < 3; j++) {
        ylo = t0->p[j][1] < ylo ? t0->p[j][1] : ylo;
        ylo = t1->p[j][1] < ylo ? t1->p[j][1] : ylo;
    }
    g->yw = rm_optf(opt, "y", ylo);
    g->src = t0->id;
    g->own = own;
    for (k = 0; k < 3; k++) {
        g->rgb[k] = (unsigned char)(((unsigned)t0->rgb[k] + t1->rgb[k]) / 2u);
    }
    if (g->rowseen < 1e-6) {
        free(g);
        return 0;
    }
    lg_read(g, tx);
    lg_build(g, tx, opt, out);
    free(g);
    return 1;
}

static int lg_same_card(const RmTri *a, const RmTri *b)
{
    double la[3] = { 1e30, 1e30, 1e30 }, ha[3] = { -1e30, -1e30, -1e30 }, lb[3] = { 1e30, 1e30, 1e30 }, hb[3] = { -1e30, -1e30, -1e30 };
    int j, q;

    for (j = 0; j < 3; j++) {
        for (q = 0; q < 3; q++) {
            la[q] = fmin(la[q], a->p[j][q]); ha[q] = fmax(ha[q], a->p[j][q]);
            lb[q] = fmin(lb[q], b->p[j][q]); hb[q] = fmax(hb[q], b->p[j][q]);
        }
    }
    for (q = 0; q < 3; q++) {
        if (fabs(la[q] - lb[q]) > 0.05 || fabs(ha[q] - hb[q]) > 0.05) {
            return 0;
        }
    }
    return 1;
}

void rm_driftwood(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi, i;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *B = &b[bi];
        Log *g;
        int own;

        if (B->bb || B->n < 2 || B->tex < 0 || (unsigned)B->tex >= ntex || !lg_is_log(&tex[B->tex]) || out->ntex >= 8) {
            continue;
        }
        g = (Log *)calloc(1, sizeof *g);
        if (g == NULL) {
            out->fail = 1;
            return;
        }
        lg_read(g, &tex[B->tex]);
        own = out->ntex;
        out->tex[own].rgba = lg_texture(g, &tex[B->tex]);
        free(g);
        if (out->tex[own].rgba == NULL) {
            out->fail = 1;
            return;
        }
        out->tex[own].w = LG_W;
        out->tex[own].h = LG_TH;
        out->ntex++;
        /* (a card: two triangles in a row on the same box) */
        for (i = 0; i + 1u < B->n; i++) {
            if (lg_same_card(&B->tri[i], &B->tri[i + 1u]) && lg_card(&B->tri[i], &B->tri[i + 1u], &tex[B->tex], opt, own, out)) {
                rm_drop(out, B->tri[i].id);
                rm_drop(out, B->tri[i + 1u].id);
                i++;
            }
        }
    }
}
