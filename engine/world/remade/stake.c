/*
 * engine/world/remade/stake.c -- a wooden stake standing in a pond (Route 8's
 * and the Moor of Icirrus's), part of a map chunk's own mesh.
 *
 * The map draws each stake as one flat card, a tile wide, lying back so far
 * that it nearly faces the field's steep camera: a 16 x 32 picture of a short
 * round post seen from above (rows 0 to 11 its cut top, rows 12 to 16 its
 * bark, rows 17 to 19 the water's ring round its foot, the rest clear).  A
 * battle camera low over the water sees the card from its edge: a thin
 * slanted stick in the air.  Remade, from the card's place and the picture's
 * own pixels:
 *   - the card is left out;
 *   - a round post of twelve straight sides stands in the water, as wide as
 *     the picture draws it (columns 1 to 14) and as deep as it is wide.  It
 *     stands where the field shows it: its south side meets the water where
 *     the field's camera (53.2 degrees down, the game's default field camera)
 *     sees the foot of the bark (the top of row 17), and it is as high as that
 *     camera shows five rows of bark;
 *   - its top wears the picture's top (rows 0 to 11); its sides wear the bark
 *     (rows 12 to 16), the south half as the picture shows it, and on round
 *     the post the bark's 14 columns again, forward then backward, so no side
 *     repeats its neighbour; it goes on under the water to the card's foot;
 *   - the ring lies flat on the water round the foot: three bands, in the
 *     ring's three colours from the post outward, the last one ending at the
 *     card's edge (stakes stand on tiles side by side: their rings meet there
 *     and never lie over each other).
 *
 * The post has a texture of its own, made from the picture by these rules:
 * the top's clear pixels take the nearest drawn pixel of their row, and its
 * rows 8 to 11 take row 7's rim colour in columns 1 and 14 (there the picture
 * draws the post's dark side line, which is a line round the drawing, not a
 * part of the top); in the bark, the columns of each row that the picture
 * gives to the side line and to the water (the waterline curves up toward the
 * sides) take that row's nearest bark pixel.
 *
 * The stake's draw is the one whose picture is 16 x 32 with the top's light
 * wood at (7, 6), the bark's light grain at (7, 14), the wet foot at (7, 17)
 * and the ring at (0, 12): nothing else of the chunk is touched, and a chunk
 * without that picture stays as it is.
 *
 * Settings: y (the water's height; default: the card's foot), ring (a band's
 * width, picture pixels; default: a third of what the card leaves beside the
 * post; 0: no ring).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define PI 3.14159265358979323846
#define ST_TW 64                /* the post's own texture */
#define ST_TH 32
#define ST_SIDE_ROW 16          /* the bark's strip: rows 16 to 20, columns 0 to 47 (its middle, column 24, is the south side's middle) */
#define ST_RING_ROW 24          /* the ring's three colours: 4 x 4 blocks at columns 0, 4 and 8 */
#define ST_FIELD_PITCH 53.2     /* the game's default field camera looks this far down (degrees) */
#define ST_N 12                 /* the post's sides */
#define ST_HALF (180.0 / ST_N)  /* half of a side's turn, degrees */
#define ST_R 7.0                /* the post: half of its width, picture pixels (columns 1 to 14) */
#define ST_TOP_ROWS 12
#define ST_BARK_ROW 12
#define ST_BARK_ROWS 5

static int st_is_stake(const RmTex *t)
{
    static const struct {
        int x, y;
        unsigned char rgb[3];
    } sig[4] = { { 7, 6, { 0xFF, 0xD5, 0x9C } }, { 7, 14, { 0xBD, 0x94, 0x5A } }, { 7, 17, { 0x9C, 0xAC, 0x9C } },
                 { 0, 12, { 0xCD, 0xFF, 0xFF } } };
    int k;

    if (t == NULL || t->rgba == NULL || t->w != 16 || t->h != 32) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        const unsigned char *px = &t->rgba[((size_t)sig[k].y * 16u + (size_t)sig[k].x) * 4u];

        if (px[3] < 128 || px[0] != sig[k].rgb[0] || px[1] != sig[k].rgb[1] || px[2] != sig[k].rgb[2]) {
            return 0;
        }
    }
    return 1;
}

static const unsigned char *st_px(const RmTex *t, int c, int r)
{
    return &t->rgba[((size_t)r * 16u + (size_t)c) * 4u];
}

static void st_put(unsigned char *tex, int c, int r, const unsigned char *px)
{
    unsigned char *d = &tex[((size_t)r * ST_TW + (size_t)c) * 4u];

    d[0] = px[0]; d[1] = px[1]; d[2] = px[2]; d[3] = 255;
}

/* The bark's columns of a row (12 to 16) that are bark: the rest of the row is the post's side line or the water. */
static const int k_bark[ST_BARK_ROWS][2] = { { 2, 13 }, { 2, 13 }, { 2, 13 }, { 3, 12 }, { 4, 11 } };

/* The bark's pixel of column c (1 to 14) in row r (0 to 4 of the bark). */
static const unsigned char *st_bark(const RmTex *t, int c, int r)
{
    c = c < k_bark[r][0] ? k_bark[r][0] : c > k_bark[r][1] ? k_bark[r][1] : c;
    return st_px(t, c, ST_BARK_ROW + r);
}

/* The post's own texture (the rules above); NULL: out of memory. */
static unsigned char *st_texture(const RmTex *t)
{
    unsigned char *tex = (unsigned char *)calloc((size_t)ST_TW * ST_TH, 4u);
    int c, r, k;

    if (tex == NULL) {
        return NULL;
    }
    /* the top: rows 0 to 11, columns 0 to 15 */
    for (r = 0; r < ST_TOP_ROWS; r++) {
        int a = 16, e = -1;

        /* (the row's drawn pixels of the top: columns 1 to 14; column 0 and 15 are the water's ring) */
        for (c = 1; c <= 14; c++) {
            if (st_px(t, c, r)[3] >= 128) {
                a = c < a ? c : a;
                e = c;
            }
        }
        for (c = 0; c < 16; c++) {
            int cc = c < a ? a : c > e ? e : c, rr = r;

            if (r >= 8 && (cc == 1 || cc == 14)) {
                rr = 7;                 /* (the side line: the rim's colour) */
            }
            st_put(tex, c, r, st_px(t, cc, rr));
            if (r == ST_TOP_ROWS - 1) {
                st_put(tex, c, r + 1, st_px(t, cc, rr));        /* (a row more: no clear pixel at the top's near edge) */
            }
        }
    }
    /* the bark's strip: from the south side's middle (between the picture's columns 7 and 8) round the post both
     * ways, the columns out to the picture's edge, then all 14 back, then forward again */
    for (k = 0; k < 48; k++) {
        const int d = k < 24 ? 23 - k : k - 24;         /* pixels from the south middle */
        int col;

        if (k < 24) {
            col = d < 7 ? 7 - d : d < 21 ? 1 + (d - 7) : 14 - (d - 21);
        } else {
            col = d < 7 ? 8 + d : d < 21 ? 14 - (d - 7) : 1 + (d - 21);
        }
        for (r = 0; r < ST_BARK_ROWS; r++) {
            st_put(tex, k, ST_SIDE_ROW + r, st_bark(t, col, r));
        }
        st_put(tex, k, ST_SIDE_ROW + ST_BARK_ROWS, st_bark(t, col, ST_BARK_ROWS - 1));      /* (a row more: no clear pixel under the strip) */
        st_put(tex, k, ST_SIDE_ROW - 1, st_bark(t, col, 0));
    }
    /* the ring: the wet foot (7, 17), the ring's inner (7, 18) and outer colour (7, 19) */
    for (k = 0; k < 3; k++) {
        for (r = 0; r < 4; r++) {
            for (c = 0; c < 4; c++) {
                st_put(tex, k * 4 + c, ST_RING_ROW + r, st_px(t, 7, 17 + k));
            }
        }
    }
    /* every other pixel takes a drawn neighbour's colour, outward step by step (it stays clear), so a smaller copy
     * of the texture mixes a face's edge with its own colours and not with black (as rock.c fills its own) */
    for (k = 0; k < ST_TW; k++) {
        int any = 0;

        for (r = 0; r < ST_TH; r++) {
            for (c = 0; c < ST_TW; c++) {
                static const int dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
                unsigned char *d = &tex[((size_t)r * ST_TW + (size_t)c) * 4u];
                int q;

                if (d[3] != 0) {
                    continue;
                }
                for (q = 0; q < 4; q++) {
                    const int c2 = c + dc[q], r2 = r + dr[q];
                    const unsigned char *s2;

                    if (c2 < 0 || r2 < 0 || c2 >= ST_TW || r2 >= ST_TH) {
                        continue;
                    }
                    s2 = &tex[((size_t)r2 * ST_TW + (size_t)c2) * 4u];
                    if (s2[3] == 255 || s2[3] == (unsigned char)(k + 1)) {
                        d[0] = s2[0]; d[1] = s2[1]; d[2] = s2[2];
                        d[3] = (unsigned char)(k + 2);          /* (taken in this step: the next step's neighbour) */
                        any = 1;
                        break;
                    }
                }
            }
        }
        if (!any) {
            break;
        }
    }
    for (r = 0; r < ST_TH; r++) {
        for (c = 0; c < ST_TW; c++) {
            unsigned char *d = &tex[((size_t)r * ST_TW + (size_t)c) * 4u];

            if (d[3] != 255) {
                d[3] = 0;
            }
        }
    }
    return tex;
}

typedef struct {
    double c[3];                /* the post's middle on the water               */
    double right[3], fwd[3];    /* across (the picture's right), toward the field camera */
    double s;                   /* a picture pixel, in map units                */
    unsigned src;
    unsigned char rgb[3];
    int tex;
} Stake;

/* A point of the post: a across (pixels, the picture's right), y its height (map units), f toward the field
 * camera (pixels). */
static void st_at(const Stake *k, double a, double y, double f, double p[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        p[i] = k->c[i] + (a * k->right[i] + f * k->fwd[i]) * k->s;
    }
    p[1] = y;
}

/* One triangle that looks the way `want` points, a little darker by the way it looks (as cup.c and turned.c shade). */
static void st_tri(const Stake *k, RmOut *out, const double a[3], const double b[3], const double c[3], const double ua[2],
                   const double ub[2], const double uc[2], const double want[3])
{
    double e1[3], e2[3], n[3], f, nf;
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
    nf = (rm_dot(n, want) < 0.0 ? -1.0 : 1.0) * rm_dot(n, k->fwd);
    f = fabs(n[1]) > 0.7 ? 1.0 : nf > 0.5 ? 1.0 : nf < -0.5 ? 0.78 : 0.86;
    for (i = 0; i < 3; i++) {
        rgb[i] = (unsigned char)(k->rgb[i] * f);
    }
    if (rm_dot(n, want) < 0.0) {
        rm_tri(out, k->src, k->tex, 1, rgb, a, c, b, ua, uc, ub, 0, 0);
    } else {
        rm_tri(out, k->src, k->tex, 1, rgb, a, b, c, ua, ub, uc, 0, 0);
    }
}

/* The post on its place: top at y1, the water at yw, its foot at y0 (under the water); the ring's bands `ring` wide. */
static void st_post(const Stake *k, RmOut *out, double yw, double y1, double y0, double ring)
{
    static const double up[3] = { 0.0, 1.0, 0.0 };
    const double rc = ST_R / cos(ST_HALF * PI / 180.0), side = 2.0 * ST_R * tan(ST_HALF * PI / 180.0);
    const double v_top = (double)ST_SIDE_ROW / ST_TH, v_water = (double)(ST_SIDE_ROW + ST_BARK_ROWS) / ST_TH;
    const double v_under = (ST_SIDE_ROW + ST_BARK_ROWS - 0.5) / ST_TH;
    double m[3], um[2];
    int i, h, b;

    /* the top: the picture's columns 1 to 15 across, its rows 0 to 12 from the far side to the near one */
    st_at(k, 0.0, y1, 0.0, m);
    um[0] = 8.0 / ST_TW;
    um[1] = 6.0 / ST_TH;
    for (i = 0; i < ST_N; i++) {
        const double t0 = ST_HALF * (1 + 2 * i) * PI / 180.0, t1 = ST_HALF * (3 + 2 * i) * PI / 180.0;
        double p0[3], p1[3], u0[2], u1[2];

        st_at(k, rc * cos(t0), y1, rc * sin(t0), p0);
        st_at(k, rc * cos(t1), y1, rc * sin(t1), p1);
        u0[0] = (8.0 + rc * cos(t0)) / ST_TW;
        u0[1] = (6.0 + rc * sin(t0) * 6.0 / ST_R) / ST_TH;
        u1[0] = (8.0 + rc * cos(t1)) / ST_TW;
        u1[1] = (6.0 + rc * sin(t1) * 6.0 / ST_R) / ST_TH;
        st_tri(k, out, m, p0, p1, um, u0, u1, up);
    }
    /* the sides, each in two halves (the strip's two ends meet in the middle of the far side) */
    for (i = 0; i < ST_N; i++) {
        const double tc = 90.0 - 2.0 * ST_HALF * i;              /* the side's middle: 90 the near side, then round by the right */
        const double cm = cos(tc * PI / 180.0), sm = sin(tc * PI / 180.0);
        double want[3];
        int q;

        for (q = 0; q < 3; q++) {
            want[q] = cm * k->right[q] + sm * k->fwd[q];
        }
        for (h = 0; h < 2; h++) {
            /* (this half: from the side's middle to its corner; along the strip, the right half of the post is
             * the picture's right) */
            const double te = tc + (h == 0 ? ST_HALF : -ST_HALF);
            const double am = side * i, ae = am + (h == 0 ? -0.5 : 0.5) * side;        /* pixels round from the near middle, by the right */
            const double far_m = ST_N / 2 * side, far_e = far_m - 0.5 * side;          /* (the far side: the strip's two ends) */
            const double sm_a = i == ST_N / 2 ? (h == 0 ? far_m : -far_m) : i > ST_N / 2 ? am - ST_N * side : am;
            const double se_a = i == ST_N / 2 ? (h == 0 ? far_e : -far_e) : i > ST_N / 2 ? ae - ST_N * side : ae;
            double pm1[3], pe1[3], pmw[3], pew[3], pm0[3], pe0[3], a[2], bq[2], c2[2], d[2];

            st_at(k, ST_R * cm, y1, ST_R * sm, pm1);
            st_at(k, rc * cos(te * PI / 180.0), y1, rc * sin(te * PI / 180.0), pe1);
            memcpy(pmw, pm1, sizeof pmw); pmw[1] = yw;
            memcpy(pew, pe1, sizeof pew); pew[1] = yw;
            memcpy(pm0, pm1, sizeof pm0); pm0[1] = y0;
            memcpy(pe0, pe1, sizeof pe0); pe0[1] = y0;
            a[0] = (24.0 + sm_a) / ST_TW;  a[1] = v_top;
            bq[0] = (24.0 + se_a) / ST_TW; bq[1] = v_top;
            c2[0] = bq[0];                 c2[1] = v_water;
            d[0] = a[0];                   d[1] = v_water;
            st_tri(k, out, pm1, pe1, pew, a, bq, c2, want);
            st_tri(k, out, pm1, pew, pmw, a, c2, d, want);
            if (y0 < yw - 1e-6) {
                /* under the water: the bark's last row */
                a[1] = bq[1] = c2[1] = d[1] = v_under;
                st_tri(k, out, pmw, pew, pe0, a, bq, c2, want);
                st_tri(k, out, pmw, pe0, pm0, a, c2, d, want);
            }
        }
    }
    /* the ring: flat on the water, a little over it (the height the battle lays flat pictures at) */
    for (b = 0; ring > 0.0 && b < 3; b++) {
        const double r0 = (ST_R + ring * b) / cos(ST_HALF * PI / 180.0), r1 = (ST_R + ring * (b + 1)) / cos(ST_HALF * PI / 180.0);
        const double uv[2] = { (b * 4 + 2.0) / ST_TW, (ST_RING_ROW + 2.0) / ST_TH };

        for (i = 0; i < ST_N; i++) {
            const double t0 = ST_HALF * (1 + 2 * i) * PI / 180.0, t1 = ST_HALF * (3 + 2 * i) * PI / 180.0;
            double p[4][3];

            st_at(k, r0 * cos(t0), yw + 0.3, r0 * sin(t0), p[0]);
            st_at(k, r0 * cos(t1), yw + 0.3, r0 * sin(t1), p[1]);
            st_at(k, r1 * cos(t1), yw + 0.3, r1 * sin(t1), p[2]);
            st_at(k, r1 * cos(t0), yw + 0.3, r1 * sin(t0), p[3]);
            st_tri(k, out, p[0], p[1], p[2], uv, uv, uv, up);
            st_tri(k, out, p[0], p[2], p[3], uv, uv, uv, up);
        }
    }
}

/* A card of the stake's draw: the triangles t0 and t1.  The post in its place; 0 when the two are no card. */
static int st_card(const RmTri *t0, const RmTri *t1, const char *opt, int tex, RmOut *out)
{
    const double pitch = ST_FIELD_PITCH * PI / 180.0;
    double e1[3], e2[3], n[3], top[3] = { 0, 0, 0 }, foot[3] = { 0, 0, 0 }, ylo = 1e30, yhi = -1e30, aL = 1e30, aR = -1e30, uL = 0, uR = 0;
    double v_top = 0, v_foot = 0, len, tilt, row, yw, p17[3], rowv, pw, high;
    const RmTri *tt[2];
    unsigned ntop = 0, nfoot = 0;
    Stake k;
    int i, j, q;

    tt[0] = t0;
    tt[1] = t1;
    memset(&k, 0, sizeof k);
    for (j = 0; j < 3; j++) {
        e1[j] = (double)t0->p[1][j] - t0->p[0][j];
        e2[j] = (double)t0->p[2][j] - t0->p[0][j];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) < 1e-9) {
        return 0;
    }
    if (n[1] < 0.0) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }
    tilt = acos(n[1] > 1.0 ? 1.0 : n[1]);              /* how far the card stands up from level */
    n[1] = 0.0;
    if (rm_norm(n) < 1e-6) {
        return 0;                                       /* (a level card: not a stake's) */
    }
    memcpy(k.fwd, n, sizeof n);
    k.right[0] = n[2];
    k.right[1] = 0.0;
    k.right[2] = -n[0];
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            ylo = tt[i]->p[j][1] < ylo ? tt[i]->p[j][1] : ylo;
            yhi = tt[i]->p[j][1] > yhi ? tt[i]->p[j][1] : yhi;
        }
    }
    if (yhi - ylo < 0.5) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            const float *p = tt[i]->p[j];
            const double a = p[0] * k.right[0] + p[2] * k.right[2];

            if (a < aL) { aL = a; uL = tt[i]->uv[j][0]; }
            if (a > aR) { aR = a; uR = tt[i]->uv[j][0]; }
            if (p[1] > yhi - 0.01) {
                for (q = 0; q < 3; q++) { top[q] += p[q]; }
                v_top += tt[i]->uv[j][1];
                ntop++;
            } else if (p[1] < ylo + 0.01) {
                for (q = 0; q < 3; q++) { foot[q] += p[q]; }
                v_foot += tt[i]->uv[j][1];
                nfoot++;
            }
        }
    }
    if (ntop == 0 || nfoot == 0 || aR - aL < 1e-3) {
        return 0;
    }
    for (q = 0; q < 3; q++) {
        top[q] /= ntop;
        foot[q] /= nfoot;
    }
    v_top /= ntop;
    v_foot /= nfoot;
    pw = fabs(uR - uL) * 16.0;                          /* the picture's columns the card shows */
    if (pw < 2.0 || fabs(v_foot - v_top) < 0.5) {
        return 0;
    }
    k.s = (aR - aL) / pw;
    if (uR < uL) {
        /* (the card shows the picture turned over: its right is the card's left) */
        k.right[0] = -k.right[0];
        k.right[2] = -k.right[2];
    }
    len = sqrt((top[0] - foot[0]) * (top[0] - foot[0]) + (top[1] - foot[1]) * (top[1] - foot[1]) + (top[2] - foot[2]) * (top[2] - foot[2]));
    row = len / (fabs(v_foot - v_top) * 32.0);          /* a row of the picture along the card, map units */
    yw = rm_optf(opt, "y", foot[1]);
    /* the foot of the bark on the card (the top of row 17), and where the field's camera sees it on the water */
    rowv = (17.0 / 32.0 - v_top) / (v_foot - v_top);
    for (q = 0; q < 3; q++) {
        p17[q] = top[q] + (foot[q] - top[q]) * rowv;
    }
    for (q = 0; q < 3; q++) {
        k.c[q] = p17[q] - k.fwd[q] * ((p17[1] - yw) / tan(pitch) + ST_R * k.s);
    }
    k.c[1] = yw;
    /* as high as the field's camera shows five rows of bark: a row of the card shows sin(pitch + tilt) of its
     * length, an upright side cos(pitch) of its height */
    high = ST_BARK_ROWS * row * sin(pitch + tilt) / cos(pitch);
    k.src = t0->id;
    k.tex = tex;
    for (q = 0; q < 3; q++) {
        k.rgb[q] = (unsigned char)(((unsigned)t0->rgb[q] + t1->rgb[q]) / 2u);
    }
    /* the ring's three bands end at the card's edge: the map keeps a stake and its ring inside its tile, and stakes
     * stand on tiles side by side, so two rings meet there and never lie over each other */
    st_post(&k, out, yw, yw + high, ylo < yw ? ylo : yw, rm_optf(opt, "ring", pw / 2.0 > ST_R ? (pw / 2.0 - ST_R) / 3.0 : 0.0));
    return 1;
}

static int st_same_card(const RmTri *a, const RmTri *b)
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

void rm_stake(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi, i;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *B = &b[bi];
        int own;

        if (B->bb || B->n < 2 || B->tex < 0 || (unsigned)B->tex >= ntex || !st_is_stake(&tex[B->tex]) || out->ntex >= 8) {
            continue;
        }
        own = out->ntex;
        out->tex[own].rgba = st_texture(&tex[B->tex]);
        if (out->tex[own].rgba == NULL) {
            out->fail = 1;
            return;
        }
        out->tex[own].w = ST_TW;
        out->tex[own].h = ST_TH;
        out->ntex++;
        /* (a card: two triangles in a row on the same box) */
        for (i = 0; i + 1u < B->n; i++) {
            if (st_same_card(&B->tri[i], &B->tri[i + 1u]) && st_card(&B->tri[i], &B->tri[i + 1u], opt, own, out)) {
                rm_drop(out, B->tri[i].id);
                rm_drop(out, B->tri[i + 1u].id);
                i++;
            }
        }
    }
}
