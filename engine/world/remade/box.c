/*
 * engine/world/remade/box.c -- a picture card made the box it shows (a games
 * console beside the television).
 *
 * The map has the thing as one card leaning back from its foot, so the
 * field's steep camera sees its front as a flat picture.  The box stands
 * upright on the card's foot line, as wide as the card and as tall as the
 * card is long (the picture keeps square pixels); `deep` units deep, behind
 * the front.  Its front is the card's picture, pixel for pixel; each side is
 * the picture's own edge column (the light and dark bands of the front run
 * round the corner); the top is the picture's top row; the back the side's
 * column again.  It rests on the floor `floor` units under the card's foot.
 *
 * Settings: deep (6), floor (0: the card's foot is the floor), edge (1.5: how
 * many pixels in from the picture's edge the sides' column is read).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    RmOut *o;
    unsigned src;
    unsigned char rgb[3];
} Bx;

/* A quad a b c d (round its edge) that looks along `n`, each corner its uv. */
static void bx_quad(const Bx *x, const double p[4][3], const double uv[4][2], const double n[3], double shade)
{
    double e1[3], e2[3], fn[3];
    unsigned char rgb[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
        rgb[k] = (unsigned char)(int)(x->rgb[k] * shade);
    }
    rm_cross(e1, e2, fn);
    if (rm_dot(fn, n) >= 0.0) {
        rm_tri(x->o, x->src, -1, 1, rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 2, 0);
        rm_tri(x->o, x->src, -1, 1, rgb, p[0], p[2], p[3], uv[0], uv[2], uv[3], 2, 0);
    } else {
        rm_tri(x->o, x->src, -1, 1, rgb, p[0], p[2], p[1], uv[0], uv[2], uv[1], 2, 0);
        rm_tri(x->o, x->src, -1, 1, rgb, p[0], p[3], p[2], uv[0], uv[3], uv[2], 2, 0);
    }
}

/* A card lying on the floor (a remote control): each pair of its triangles a picture seen from above.  The box is
 * the picture's opaque outline, `high` units tall, its foot `floor` under the card; its top the picture, each side
 * the outline's own edge pixels along that edge. */
static void box_flat(const RmBatch *bt, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double high = rm_optf(opt, "high", 1.5), drop = rm_optf(opt, "floor", 0.0);
    int tw = 16, th = 32;
    unsigned q;

    if (bt->tex >= 0 && (unsigned)bt->tex < ntex) {
        tw = tex[bt->tex].w;
        th = tex[bt->tex].h;
    }
    for (q = 0; q + 1 < bt->n; q += 2) {
        const RmTri *t0 = &bt->tri[q], *t1 = &bt->tri[q + 1];
        double m[3][3], ua = 1e30, ub = -1e30, va = 1e30, vb = -1e30, y, c[3], uv[4][2], p[4][3], pc[4][2];
        double cen[3] = { 0, 0, 0 };
        int k, j;
        Bx x;

        if (rm_plane(t0, m) != 0) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            const RmTri *tt[2] = { t0, t1 };

            for (j = 0; j < 2; j++) {
                ua = tt[j]->uv[k][0] < ua ? tt[j]->uv[k][0] : ua;
                ub = tt[j]->uv[k][0] > ub ? tt[j]->uv[k][0] : ub;
                va = tt[j]->uv[k][1] < va ? tt[j]->uv[k][1] : va;
                vb = tt[j]->uv[k][1] > vb ? tt[j]->uv[k][1] : vb;
            }
        }
        /* the opaque outline inside the card's picture */
        if (bt->tex >= 0 && (unsigned)bt->tex < ntex && tex[bt->tex].rgba != NULL) {
            const unsigned char *px = tex[bt->tex].rgba;
            int c0 = (int)floor(ua * tw + 1e-6), c1 = (int)ceil(ub * tw - 1e-6) - 1, r0 = (int)floor(va * th + 1e-6), r1 = (int)ceil(vb * th - 1e-6) - 1;
            int oc0 = 1 << 20, oc1 = -1, or0 = 1 << 20, or1 = -1, cc, rr;

            for (rr = r0; rr <= r1; rr++) {
                for (cc = c0; cc <= c1; cc++) {
                    int x_ = ((cc % tw) + tw) % tw, y_ = ((rr % th) + th) % th;

                    if (px[(y_ * tw + x_) * 4 + 3] >= 128) {
                        oc0 = cc < oc0 ? cc : oc0;
                        oc1 = cc > oc1 ? cc : oc1;
                        or0 = rr < or0 ? rr : or0;
                        or1 = rr > or1 ? rr : or1;
                    }
                }
            }
            if (oc1 < oc0) {
                continue;
            }
            ua = (double)oc0 / tw;
            ub = (double)(oc1 + 1) / tw;
            va = (double)or0 / th;
            vb = (double)(or1 + 1) / th;
        }
        y = t0->p[0][1] - drop;
        pc[0][0] = ua; pc[0][1] = va;
        pc[1][0] = ub; pc[1][1] = va;
        pc[2][0] = ub; pc[2][1] = vb;
        pc[3][0] = ua; pc[3][1] = vb;
        for (j = 0; j < 4; j++) {
            rm_at(m, pc[j][0], pc[j][1], c);
            p[j][0] = c[0];
            p[j][1] = y + high;
            p[j][2] = c[2];
            uv[j][0] = pc[j][0];
            uv[j][1] = pc[j][1];
            for (k = 0; k < 3; k++) {
                cen[k] += p[j][k] / 4.0;
            }
        }
        x.o = out;
        x.src = t0->id;
        x.rgb[0] = bt->rgb[0]; x.rgb[1] = bt->rgb[1]; x.rgb[2] = bt->rgb[2];
        {
            const double nu[3] = { 0, 1, 0 };

            bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, nu, 1.0);
        }
        /* the sides: edge j from corner j to j+1, in the outline's pixels half a pixel in from that edge */
        for (j = 0; j < 4; j++) {
            const int j2 = (j + 1) % 4;
            double sq[4][3], suv[4][2], n[3], inu, inv_;

            /* (the inward step in the picture: toward the middle of the outline) */
            inu = ((ua + ub) * 0.5 - (pc[j][0] + pc[j2][0]) * 0.5);
            inv_ = ((va + vb) * 0.5 - (pc[j][1] + pc[j2][1]) * 0.5);
            inu = fabs(inu) > 1e-9 ? (inu > 0 ? 0.5 / tw : -0.5 / tw) : 0.0;
            inv_ = fabs(inv_) > 1e-9 ? (inv_ > 0 ? 0.5 / th : -0.5 / th) : 0.0;
            for (k = 0; k < 3; k++) {
                sq[0][k] = p[j][k];
                sq[1][k] = p[j2][k];
                sq[2][k] = p[j2][k];
                sq[3][k] = p[j][k];
            }
            sq[2][1] = y;
            sq[3][1] = y;
            suv[0][0] = pc[j][0] + inu;  suv[0][1] = pc[j][1] + inv_;
            suv[1][0] = pc[j2][0] + inu; suv[1][1] = pc[j2][1] + inv_;
            suv[2][0] = suv[1][0];       suv[2][1] = suv[1][1];
            suv[3][0] = suv[0][0];       suv[3][1] = suv[0][1];
            for (k = 0; k < 3; k++) {
                n[k] = (p[j][k] + p[j2][k]) * 0.5 - cen[k];
            }
            n[1] = 0.0;
            bx_quad(&x, (const double (*)[3])sq, (const double (*)[2])suv, n, 0.85);
        }
        rm_drop(out, t0->id);
        rm_drop(out, t1->id);
    }
}

/* `lie=H`: a card leaning back over a bed or a seat (a pillow: the field's camera sees its top as a slanted picture)
 * made the low body it shows.  The body lies on the card's foot height over the card's footprint, H units tall, its
 * top edge bevelled in by `bevel` (one bevel, no rounding); the top is the card's picture laid flat (the card's foot
 * edge at the body's front, its top edge at the back: seen from the field's camera it reads as the card did); the
 * front band, the sides and the back carry the picture's own edge pixels (a row, a column in from each edge). */
static void box_lie(const RmBatch *bt, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double high = rm_optf(opt, "lie", 4.0), bev = rm_optf(opt, "bevel", 1.0);
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, m[3][3], a00, a01, a11, det;
    double fz, bz, y0, ytop, x0, x1, inv_;
    int th = 16, k, j;
    unsigned q;
    Bx x;

    if (bt->n == 0 || rm_plane(&bt->tri[0], m) != 0) {
        return;
    }
    if (bt->tex >= 0 && (unsigned)bt->tex < ntex) {
        th = tex[bt->tex].h;
    }
    for (q = 0; q < bt->n; q++) {
        for (k = 0; k < 3; k++) {
            for (j = 0; j < 3; j++) {
                lo[j] = bt->tri[q].p[k][j] < lo[j] ? bt->tri[q].p[k][j] : lo[j];
                hi[j] = bt->tri[q].p[k][j] > hi[j] ? bt->tri[q].p[k][j] : hi[j];
            }
        }
    }
    /* the foot edge: the card's low corners; which z end is it */
    {
        double zl = 0.0;
        int nl = 0;

        for (q = 0; q < bt->n; q++) {
            for (k = 0; k < 3; k++) {
                if (bt->tri[q].p[k][1] <= lo[1] + 0.05) {
                    zl += bt->tri[q].p[k][2];
                    nl++;
                }
            }
        }
        zl = nl ? zl / nl : hi[2];
        fz = fabs(zl - hi[2]) < fabs(zl - lo[2]) ? hi[2] : lo[2];
        bz = fz == hi[2] ? lo[2] : hi[2];
    }
    a00 = rm_dot(m[0], m[0]);
    a01 = rm_dot(m[0], m[1]);
    a11 = rm_dot(m[1], m[1]);
    det = a00 * a11 - a01 * a01;
    if (fabs(det) < 1e-12) {
        return;
    }
    x0 = lo[0];
    x1 = hi[0];
    y0 = lo[1];
    ytop = y0 + high;

    inv_ = 1.0 / th;
    x.o = out;
    x.src = bt->tri[0].id;
    x.rgb[0] = bt->rgb[0]; x.rgb[1] = bt->rgb[1]; x.rgb[2] = bt->rgb[2];
    {
        /* the picture's uv at a point of the card over (x, z): the card's point at that x on the line from its foot
         * edge (z = fz, y lo) to its top edge (z = bz, y hi), the share of the way t from the foot */
#define CARD_UV(xx, t, uv) do { \
            double p_[3], d_[3]; \
            p_[0] = (xx); p_[1] = lo[1] + (t) * (hi[1] - lo[1]); p_[2] = fz + (t) * (bz - fz); \
            for (k = 0; k < 3; k++) d_[k] = p_[k] - m[2][k]; \
            (uv)[0] = (rm_dot(d_, m[0]) * a11 - rm_dot(d_, m[1]) * a01) / det; \
            (uv)[1] = (rm_dot(d_, m[1]) * a00 - rm_dot(d_, m[0]) * a01) / det; \
        } while (0)
        const double s = bz < fz ? -1.0 : 1.0;          /* toward the back along z */
        const double nf[3] = { 0, 0, -s }, nbk[3] = { 0, 0, s }, nl[3] = { -1, 0, 0 }, nr[3] = { 1, 0, 0 }, nu[3] = { 0, 1, 0 };
        const double yb = ytop - bev * 0.8;             /* the bevel's foot */
        const double ix0 = x0 + bev, ix1 = x1 - bev, ifz = fz + s * bev, ibz = bz - s * bev;
        double p[4][3], uv[4][2], u0[2], u1[2], w0[2], w1[2];

        /* top: the picture over the inner square */
        CARD_UV(ix0, bev / fabs(bz - fz), u0);
        CARD_UV(ix1, bev / fabs(bz - fz), u1);
        CARD_UV(ix1, 1.0 - bev / fabs(bz - fz), w1);
        CARD_UV(ix0, 1.0 - bev / fabs(bz - fz), w0);
        p[0][0] = ix0; p[0][1] = ytop; p[0][2] = ifz; uv[0][0] = u0[0]; uv[0][1] = u0[1];
        p[1][0] = ix1; p[1][1] = ytop; p[1][2] = ifz; uv[1][0] = u1[0]; uv[1][1] = u1[1];
        p[2][0] = ix1; p[2][1] = ytop; p[2][2] = ibz; uv[2][0] = w1[0]; uv[2][1] = w1[1];
        p[3][0] = ix0; p[3][1] = ytop; p[3][2] = ibz; uv[3][0] = w0[0]; uv[3][1] = w0[1];
        bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, nu, 1.0);
        /* bevel, front: from the outline at yb to the top's front edge, the picture's rows near the foot */
        CARD_UV(x0, 0.0, u0);
        CARD_UV(x1, 0.0, u1);
        p[0][0] = x0; p[0][1] = yb; p[0][2] = fz; uv[0][0] = u0[0]; uv[0][1] = u0[1];
        p[1][0] = x1; p[1][1] = yb; p[1][2] = fz; uv[1][0] = u1[0]; uv[1][1] = u1[1];
        CARD_UV(ix1, bev / fabs(bz - fz), w1);
        CARD_UV(ix0, bev / fabs(bz - fz), w0);
        p[2][0] = ix1; p[2][1] = ytop; p[2][2] = ifz; uv[2][0] = w1[0]; uv[2][1] = w1[1];
        p[3][0] = ix0; p[3][1] = ytop; p[3][2] = ifz; uv[3][0] = w0[0]; uv[3][1] = w0[1];
        {
            const double n[3] = { 0, 0.7, -s * 0.7 };

            bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, n, 1.0);
        }
        /* bevel, back */
        CARD_UV(x0, 1.0, u0);
        CARD_UV(x1, 1.0, u1);
        p[0][0] = x0; p[0][1] = yb; p[0][2] = bz; uv[0][0] = u0[0]; uv[0][1] = u0[1];
        p[1][0] = x1; p[1][1] = yb; p[1][2] = bz; uv[1][0] = u1[0]; uv[1][1] = u1[1];
        CARD_UV(ix1, 1.0 - bev / fabs(bz - fz), w1);
        CARD_UV(ix0, 1.0 - bev / fabs(bz - fz), w0);
        p[2][0] = ix1; p[2][1] = ytop; p[2][2] = ibz; uv[2][0] = w1[0]; uv[2][1] = w1[1];
        p[3][0] = ix0; p[3][1] = ytop; p[3][2] = ibz; uv[3][0] = w0[0]; uv[3][1] = w0[1];
        {
            const double n[3] = { 0, 0.7, s * 0.7 };

            bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, n, 0.9);
        }
        /* bevels, sides */
        for (j = 0; j < 2; j++) {
            const double xs = j == 0 ? x0 : x1, ixs = j == 0 ? ix0 : ix1;
            const double n[3] = { j == 0 ? -0.7 : 0.7, 0.7, 0 };

            CARD_UV(xs, 0.0, u0);
            CARD_UV(xs, 1.0, u1);
            CARD_UV(ixs, 1.0 - bev / fabs(bz - fz), w1);
            CARD_UV(ixs, bev / fabs(bz - fz), w0);
            p[0][0] = xs;  p[0][1] = yb;   p[0][2] = fz;  uv[0][0] = u0[0]; uv[0][1] = u0[1];
            p[1][0] = xs;  p[1][1] = yb;   p[1][2] = bz;  uv[1][0] = u1[0]; uv[1][1] = u1[1];
            p[2][0] = ixs; p[2][1] = ytop; p[2][2] = ibz; uv[2][0] = w1[0]; uv[2][1] = w1[1];
            p[3][0] = ixs; p[3][1] = ytop; p[3][2] = ifz; uv[3][0] = w0[0]; uv[3][1] = w0[1];
            bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, n, 0.95);
        }
        /* the straight sides, foot to yb: each the picture's edge pixels along that edge, a pixel in */
        {
            const double ys[2] = { y0, yb };
            const double ex[4][2] = { { x0, x1 }, { x1, x1 }, { x1, x0 }, { x0, x0 } };
            const double ez[4][2] = { { fz, fz }, { fz, bz }, { bz, bz }, { bz, fz } };
            const double et[4][2] = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 0.0 } };
            const double *nn[4] = { nf, nr, nbk, nl };
            int e;

            for (e = 0; e < 4; e++) {
                double ua[2], ub[2];
                const double ti = e == 0 ? inv_ * 1.5 / fabs(hi[1] - lo[1] + 1e-9) : e == 2 ? -inv_ * 1.5 : 0.0;

                CARD_UV(ex[e][0] + (e == 1 ? -0.3 : e == 3 ? 0.3 : 0.0), et[e][0] + ti, ua);
                CARD_UV(ex[e][1] + (e == 1 ? -0.3 : e == 3 ? 0.3 : 0.0), et[e][1] + ti, ub);
                p[0][0] = ex[e][0]; p[0][1] = ys[0]; p[0][2] = ez[e][0]; uv[0][0] = ua[0]; uv[0][1] = ua[1];
                p[1][0] = ex[e][1]; p[1][1] = ys[0]; p[1][2] = ez[e][1]; uv[1][0] = ub[0]; uv[1][1] = ub[1];
                p[2][0] = ex[e][1]; p[2][1] = ys[1]; p[2][2] = ez[e][1]; uv[2][0] = ub[0]; uv[2][1] = ub[1];
                p[3][0] = ex[e][0]; p[3][1] = ys[1]; p[3][2] = ez[e][0]; uv[3][0] = ua[0]; uv[3][1] = ua[1];
                bx_quad(&x, (const double (*)[3])p, (const double (*)[2])uv, nn[e], e == 0 ? 0.9 : 0.8);
            }
        }
#undef CARD_UV
    }
    for (q = 0; q < bt->n; q++) {
        rm_drop(out, bt->tri[q].id);
    }
}

/* `slab=T`: a card leaning back over a bed (a pillow against the headboard) kept as it leans and given its thickness:
 * the card is the front, a back face T units behind it along its own normal (the pillow's back, its picture again),
 * and the four edges closed in the picture's own edge pixels.  The card stays as the map has it.
 * `outline=1` with it: a cut-out picture (a crest, a plaque): the rim follows the picture's own outline instead, a
 * strip from front to back along each edge of an opaque pixel that has a see-through pixel (or the picture's end)
 * beside it, in that pixel's colour. */
static void box_slab(const RmBatch *bt, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double thick = rm_optf(opt, "slab", 3.0);
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, m[3][3], a00, a01, a11, det, fz = 0, bz = 0;
    double c[4][3], uv[4][2], n[3], e1[3], e2[3], l;
    int k, j, th = 16;
    unsigned q;
    Bx x;

    if (bt->n == 0 || rm_plane(&bt->tri[0], m) != 0) {
        return;
    }
    if (bt->tex >= 0 && (unsigned)bt->tex < ntex) {
        th = tex[bt->tex].h;
    }
    for (q = 0; q < bt->n; q++) {
        for (k = 0; k < 3; k++) {
            for (j = 0; j < 3; j++) {
                lo[j] = bt->tri[q].p[k][j] < lo[j] ? bt->tri[q].p[k][j] : lo[j];
                hi[j] = bt->tri[q].p[k][j] > hi[j] ? bt->tri[q].p[k][j] : hi[j];
            }
        }
    }
    for (q = 0; q < bt->n; q++) {
        for (k = 0; k < 3; k++) {
            if (bt->tri[q].p[k][1] <= lo[1] + 0.05) {
                fz = bt->tri[q].p[k][2];
            } else if (bt->tri[q].p[k][1] >= hi[1] - 0.05) {
                bz = bt->tri[q].p[k][2];
            }
        }
    }
    a00 = rm_dot(m[0], m[0]);
    a01 = rm_dot(m[0], m[1]);
    a11 = rm_dot(m[1], m[1]);
    det = a00 * a11 - a01 * a01;
    if (fabs(det) < 1e-12) {
        return;
    }
    /* the card's corners: foot left, foot right, top right, top left */
    c[0][0] = lo[0]; c[0][1] = lo[1]; c[0][2] = fz;
    c[1][0] = hi[0]; c[1][1] = lo[1]; c[1][2] = fz;
    c[2][0] = hi[0]; c[2][1] = hi[1]; c[2][2] = bz;
    c[3][0] = lo[0]; c[3][1] = hi[1]; c[3][2] = bz;
    for (j = 0; j < 4; j++) {
        double d[3];

        for (k = 0; k < 3; k++) {
            d[k] = c[j][k] - m[2][k];
        }
        uv[j][0] = (rm_dot(d, m[0]) * a11 - rm_dot(d, m[1]) * a01) / det;
        uv[j][1] = (rm_dot(d, m[1]) * a00 - rm_dot(d, m[0]) * a01) / det;
    }
    for (k = 0; k < 3; k++) {
        e1[k] = c[1][k] - c[0][k];
        e2[k] = c[3][k] - c[0][k];
    }
    rm_cross(e1, e2, n);
    l = sqrt(rm_dot(n, n));
    if (l < 1e-9) {
        return;
    }
    for (k = 0; k < 3; k++) {
        n[k] /= l;
    }
    if (n[1] < 0.0) {
        for (k = 0; k < 3; k++) {
            n[k] = -n[k];
        }
    }
    x.o = out;
    x.src = bt->tri[0].id;
    x.rgb[0] = bt->rgb[0]; x.rgb[1] = bt->rgb[1]; x.rgb[2] = bt->rgb[2];
    {
        double b[4][3], p[4][3], u[4][2], nb[3];
        const double dv = (uv[3][1] > uv[0][1] ? 1.0 : -1.0) * 1.5 / th;   /* (a pixel and a half in, foot to top) */

        for (j = 0; j < 4; j++) {
            for (k = 0; k < 3; k++) {
                b[j][k] = c[j][k] - n[k] * thick;
            }
        }
        /* the back: the picture again, seen from behind */
        for (k = 0; k < 3; k++) {
            nb[k] = -n[k];
        }
        bx_quad(&x, (const double (*)[3])b, (const double (*)[2])uv, nb, 0.85);
        if (rm_optf(opt, "outline", 0.0) > 0.0 && bt->tex >= 0 && (unsigned)bt->tex < ntex && tex[bt->tex].rgba != NULL
            && fabs(uv[1][0] - uv[0][0]) > 1e-9 && fabs(uv[3][1] - uv[0][1]) > 1e-9 && fabs(uv[1][1] - uv[0][1]) < 1e-6
            && fabs(uv[3][0] - uv[0][0]) < 1e-6) {
            /* the rim along the picture's outline (the card's foot runs along the picture's u, its side along v) */
            const RmTex *tx = &tex[bt->tex];
            const double u0 = uv[0][0], du = uv[1][0] - uv[0][0], v0 = uv[0][1], dv2 = uv[3][1] - uv[0][1];
            const int ix0 = (int)floor((du > 0.0 ? u0 : u0 + du) * tx->w + 0.5), ix1 = (int)floor((du > 0.0 ? u0 + du : u0) * tx->w + 0.5);
            const int iy0 = (int)floor((dv2 > 0.0 ? v0 : v0 + dv2) * tx->h + 0.5), iy1 = (int)floor((dv2 > 0.0 ? v0 + dv2 : v0) * tx->h + 0.5);
            static const int side[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            int ix, iy, sd;

#define SLAB_OPAQUE(X, Y) ((X) >= ix0 && (X) < ix1 && (Y) >= iy0 && (Y) < iy1 \
    && tx->rgba[((size_t)((((Y) % tx->h) + tx->h) % tx->h) * (size_t)tx->w + (size_t)((((X) % tx->w) + tx->w) % tx->w)) * 4u + 3u] >= 128)
#define SLAB_AT(U, V, OUT) for (k = 0; k < 3; k++) { \
        (OUT)[k] = c[0][k] + (c[1][k] - c[0][k]) * (((U) - u0) / du) + (c[3][k] - c[0][k]) * (((V) - v0) / dv2); }
            for (iy = iy0; iy < iy1; iy++) {
                for (ix = ix0; ix < ix1; ix++) {
                    if (!SLAB_OPAQUE(ix, iy)) {
                        continue;
                    }
                    for (sd = 0; sd < 4; sd++) {
                        const int dx = side[sd][0], dy = side[sd][1];
                        double ua, va, ub, vb, mid0[3], mid1[3], en[3];

                        if (SLAB_OPAQUE(ix + dx, iy + dy)) {
                            continue;
                        }
                        /* (the pixel's edge on that side, in the picture's own measure) */
                        ua = ((double)ix + (dx > 0 ? 1.0 : 0.0)) / tx->w;
                        ub = ((double)ix + (dx < 0 ? 0.0 : 1.0)) / tx->w;
                        va = ((double)iy + (dy > 0 ? 1.0 : 0.0)) / tx->h;
                        vb = ((double)iy + (dy < 0 ? 0.0 : 1.0)) / tx->h;
                        SLAB_AT(ua, va, p[0]);
                        SLAB_AT(ub, vb, p[1]);
                        for (k = 0; k < 3; k++) {
                            p[2][k] = p[1][k] - n[k] * thick;
                            p[3][k] = p[0][k] - n[k] * thick;
                        }
                        SLAB_AT(((double)ix + 0.5) / tx->w, ((double)iy + 0.5) / tx->h, mid0);
                        SLAB_AT(((double)ix + 0.5 + dx) / tx->w, ((double)iy + 0.5 + dy) / tx->h, mid1);
                        for (k = 0; k < 3; k++) {
                            en[k] = mid1[k] - mid0[k];
                        }
                        for (j = 0; j < 4; j++) {
                            u[j][0] = ((double)ix + 0.5) / tx->w;
                            u[j][1] = ((double)iy + 0.5) / tx->h;
                        }
                        bx_quad(&x, (const double (*)[3])p, (const double (*)[2])u, en, 0.9);
                    }
                }
            }
#undef SLAB_OPAQUE
#undef SLAB_AT
            (void)dv;
            return;
        }
        /* the four edges, each in the picture's pixels just inside that edge */
        for (j = 0; j < 4; j++) {
            const int j2 = (j + 1) % 4;
            double en[3], mid[3], cen[3] = { 0, 0, 0 };
            int s_;

            for (s_ = 0; s_ < 4; s_++) {
                for (k = 0; k < 3; k++) {
                    cen[k] += c[s_][k] / 4.0;
                }
            }
            for (k = 0; k < 3; k++) {
                p[0][k] = c[j][k];
                p[1][k] = c[j2][k];
                p[2][k] = b[j2][k];
                p[3][k] = b[j][k];
                mid[k] = (c[j][k] + c[j2][k]) * 0.5;
                en[k] = mid[k] - cen[k];
            }
            for (s_ = 0; s_ < 2; s_++) {
                const int cj = s_ == 0 ? j : j2;
                /* (toward the middle of the picture by a pixel and a half) */
                u[s_][0] = uv[cj][0] + ((uv[0][0] + uv[2][0]) * 0.5 - uv[cj][0]) * 0.12;
                u[s_][1] = uv[cj][1] + ((uv[0][1] + uv[2][1]) * 0.5 - uv[cj][1]) * 0.12;
            }
            (void)dv;
            u[2][0] = u[1][0]; u[2][1] = u[1][1];
            u[3][0] = u[0][0]; u[3][1] = u[0][1];
            bx_quad(&x, (const double (*)[3])p, (const double (*)[2])u, en, 0.9);
        }
    }
}

/* `close=1`: a box the map draws with some of its sides only (a low wooden partition by a doorway: its top and one
 * long side): each side it lacks (west, east, north, south, top; the bottom stands on the floor) is added over the
 * box's whole side, in the picture of a side it has, the opposite side's if it has it, else a neighbour's, carried
 * on along the side's own across (as `bandw` carries a wall's picture round a corner), in that side's light. */
/* close=2: the chair's plain frame (`on`: a pixel of it found), for the faces made at its back and over its open
 * ends; `ax`, `front`: the axis the chair faces along and its front's sign (from the back rest toward the seat);
 * `caps`: the faces box_close_side makes now are such ends. */
static struct {
    int on, ax, front, caps;
    double uv[2];
    unsigned char rgb[3];
} g_plain;

/* close=1 `cover=C`: a side is drawn over only while its faces cover less than C of it (0.9; 0.1: only a side that is
 * missing outright, so no new face lies on a face the model has) */
static double g_close_cover = 0.9;

/* The box lo..hi of these faces: its side `only` (0 west, 1 east, 2 north, 3 south, 4 top; -1: every side) drawn over
 * when its faces do not cover it. */
static void box_close_side(const RmBatch *b, unsigned nb, const double lo[3], const double hi[3], int only, RmOut *out)
{
    static const int sax[5] = { 0, 0, 2, 2, 1 }, ssg[5] = { -1, 1, -1, 1, 1 };
    double cover[5] = { 0, 0, 0, 0, 0 }, best[5] = { 0, 0, 0, 0, 0 };
    const RmTri *src[5] = { NULL, NULL, NULL, NULL, NULL };
    unsigned bi, i;
    int s, k;

    /* the sides it has: their faces' area, and the biggest face (its picture) */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && b[bi].tex >= 0 && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], a;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            a = rm_norm(n) / 2.0;
            for (s = 0; s < 5 && a > 1e-6; s++) {
                const double plane = ssg[s] > 0 ? hi[sax[s]] : lo[sax[s]];

                if (n[sax[s]] * ssg[s] > 0.98 && fabs(t->p[0][sax[s]] - plane) < 0.2 && fabs(t->p[1][sax[s]] - plane) < 0.2 &&
                    fabs(t->p[2][sax[s]] - plane) < 0.2) {
                    cover[s] += a;
                    if (a > best[s]) {
                        best[s] = a;
                        src[s] = t;
                    }
                }
            }
        }
    }
    for (s = 0; s < 5; s++) {
        const int ax = sax[s], h = ax == 0 ? 2 : 0, o2 = ax == 1 ? 2 : 1;    /* (the side's across, and its up or depth) */
        const double side = (hi[h] - lo[h]) * (hi[o2] - lo[o2]);
        const RmTri *t = NULL;
        int sh, so;

        if ((only >= 0 && s != only) || side < 1.0 || cover[s] >= g_close_cover * side) {
            continue;
        }
        /* its picture: the opposite side's, else the biggest neighbour's (a top only from a top) */
        if (ax != 1 && src[s ^ 1] != NULL) {
            t = src[s ^ 1];
        }
        if (ax != 1 && t == NULL) {
            double big = 0.0;

            for (k = 0; k < 4; k++) {
                if (src[k] != NULL && best[k] > big) {
                    big = best[k];
                    t = src[k];
                }
            }
        }
        if (t == NULL) {
            continue;
        }
        {
            /* the picture's place as a function of (across, up) on the source face: its own across (z for a west or
             * east face, x for a north or south one), up y; a top: (x, z) */
            double sn[3], e1[3], e2[3], m[2][3], q[4][3], uv[4][2], out_n[3] = { 0, 0, 0 };
            double x0, y0, x1, y1, x2, y2, det;
            int j;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, sn);
            rm_norm(sn);
            sh = fabs(sn[1]) > 0.9 ? 0 : fabs(sn[0]) > 0.9 ? 2 : 0;
            so = fabs(sn[1]) > 0.9 ? 2 : 1;
            x0 = t->p[0][sh]; y0 = t->p[0][so];
            x1 = t->p[1][sh] - x0; y1 = t->p[1][so] - y0;
            x2 = t->p[2][sh] - x0; y2 = t->p[2][so] - y0;
            det = x1 * y2 - x2 * y1;
            if (fabs(det) < 1e-9) {
                continue;
            }
            for (k = 0; k < 2; k++) {
                const double d1 = (double)t->uv[1][k] - t->uv[0][k], d2 = (double)t->uv[2][k] - t->uv[0][k];

                m[k][0] = (d1 * y2 - d2 * y1) / det;
                m[k][1] = (d2 * x1 - d1 * x2) / det;
                m[k][2] = t->uv[0][k] - m[k][0] * x0 - m[k][1] * y0;
            }
            for (j = 0; j < 4; j++) {
                const double a = (j == 1 || j == 2) ? hi[h] : lo[h], u = j >= 2 ? hi[o2] : lo[o2];

                q[j][ax] = ssg[s] > 0 ? hi[ax] : lo[ax];
                q[j][h] = a;
                q[j][o2] = u;
                uv[j][0] = m[0][0] * a + m[0][1] * u + m[0][2];
                uv[j][1] = m[1][0] * a + m[1][1] * u + m[1][2];
            }
            out_n[ax] = ssg[s];
            {
                double f1[3], f2[3], fn[3];

                for (k = 0; k < 3; k++) {
                    f1[k] = q[1][k] - q[0][k];
                    f2[k] = q[2][k] - q[0][k];
                }
                rm_cross(f1, f2, fn);
                const unsigned char *rgb = g_plain.caps && g_plain.on ? g_plain.rgb : t->rgb;

                if (g_plain.caps && g_plain.on) {
                    for (j = 0; j < 4; j++) {
                        uv[j][0] = g_plain.uv[0];
                        uv[j][1] = g_plain.uv[1];
                    }
                }
                if (rm_dot(fn, out_n) >= 0.0) {
                    rm_tri(out, t->id, -1, 1, rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
                    rm_tri(out, t->id, -1, 1, rgb, q[0], q[2], q[3], uv[0], uv[2], uv[3], 0, 0);
                } else {
                    rm_tri(out, t->id, -1, 1, rgb, q[0], q[2], q[1], uv[0], uv[2], uv[1], 0, 0);
                    rm_tri(out, t->id, -1, 1, rgb, q[0], q[3], q[2], uv[0], uv[3], uv[2], 0, 0);
                }
            }
        }
    }
}

static void box_close(const RmBatch *b, unsigned nb, RmOut *out)
{
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
    unsigned bi, i;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            for (k = 0; k < 9; k++) {
                const double v = b[bi].tri[i].p[k / 3][k % 3];

                lo[k % 3] = v < lo[k % 3] ? v : lo[k % 3];
                hi[k % 3] = v > hi[k % 3] ? v : hi[k % 3];
            }
        }
    }
    if (lo[0] <= hi[0]) {
        box_close_side(b, nb, lo, hi, -1, out);
    }
}

/*
 * `close=2`: a thing built of boxes (a chair: its back, its seat, four legs) that the map draws from the field's side
 * only: each box has its faces toward the field camera and the light, the others are not there.  The boxes are read
 * from the faces: a face along an axis and a face across it that meets its edge give a box (the face, and the other
 * face's depth).  For each box, a pair of its opposite sides (west and east, north and south) of which one is drawn
 * and the other is not: the drawn side's faces, its slanted corners with them, turned over through the box's middle
 * (a chair is alike left and right; a leg front and back).  A side still open that is not inside another box (a
 * seat's front edge) is drawn over in the picture of the box's side next to it.
 */
#define CB_MAX 64

typedef struct { double lo[3], hi[3]; } CBox;

static double cb_cover(const RmBatch *b, unsigned nb, const CBox *x, int ax, int sg)
{
    const int h = ax == 0 ? 2 : 0, o = ax == 1 ? 2 : 1;
    const double plane = sg > 0 ? x->hi[ax] : x->lo[ax], side = (x->hi[h] - x->lo[h]) * (x->hi[o] - x->lo[o]);
    double a = 0.0;
    unsigned bi, i;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], ar;
            int in = 1;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            ar = rm_norm(n) / 2.0;
            if (ar < 1e-6 || n[ax] * sg < 0.98) {
                continue;
            }
            for (k = 0; k < 3 && in; k++) {
                in = fabs(t->p[k][ax] - plane) < 0.2 && t->p[k][h] >= x->lo[h] - 0.2 && t->p[k][h] <= x->hi[h] + 0.2 &&
                     t->p[k][o] >= x->lo[o] - 0.2 && t->p[k][o] <= x->hi[o] + 0.2;
            }
            a += in ? ar : 0.0;
        }
    }
    return side > 1e-6 ? a / side : 1.0;
}

/* close=2: the faces of `b` inside box x leaning to side sg of axis ax (on that side, or slanted out from it) turned
 * over through the box's middle along ax; `onside`: an axis face only on the box's side itself.  A face already turned
 * along ax is not turned again (`done`: id, axis pairs). */
/* Is there a face of the model at p looking along axis ax the way sg (its plane through p, p within its box)? */
static int pieces_has(const RmBatch *b, unsigned nb, const double p[3], int ax, int sg)
{
    unsigned bi, i;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3];
            int in = 1;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            if (rm_norm(n) < 1e-9 || n[ax] * sg < 0.98 || fabs(t->p[0][ax] - p[ax]) > 0.2) {
                continue;
            }
            for (k = 0; k < 3 && in; k++) {
                if (k != ax) {
                    in = p[k] >= fmin(fmin(t->p[0][k], t->p[1][k]), t->p[2][k]) - 0.05 &&
                         p[k] <= fmax(fmax(t->p[0][k], t->p[1][k]), t->p[2][k]) + 0.05;
                }
            }
            if (in) {
                return 1;
            }
        }
    }
    return 0;
}

static void pieces_turn(const RmBatch *b, unsigned nb, const CBox *x, int ax, int sg, int onside, unsigned *done,
                        unsigned *ndone, RmOut *out)
{
    const double mid = (x->lo[ax] + x->hi[ax]) / 2.0;
    unsigned bi, i, j, w;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], p[3][3], uv[3][2];
            int in = 1, seen = 0;

            for (k = 0; k < 9 && in; k++) {
                in = t->p[k / 3][k % 3] >= x->lo[k % 3] - 0.2 && t->p[k / 3][k % 3] <= x->hi[k % 3] + 0.2;
            }
            if (!in) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            if (rm_norm(n) < 1e-9 || n[ax] * sg < 0.3) {
                continue;
            }
            if (onside && n[ax] * sg < 0.98 * rm_norm(n)) {
                continue;               /* (box by box: straight faces only; a slanted corner is the thing's own) */
            }
            if (onside && n[ax] * sg > 0.98 && fabs(t->p[0][ax] - (sg > 0 ? x->hi[ax] : x->lo[ax])) > 0.2) {
                continue;               /* (an axis face inside the box, not on its side) */
            }
            for (w = 0; w < *ndone && !seen; w++) {
                seen = done[2u * w] == t->id && done[2u * w + 1u] == (unsigned)ax;
            }
            if (seen) {
                continue;
            }
            {
                /* (only where the other side is not drawn) */
                double c[3];

                for (k = 0; k < 3; k++) {
                    c[k] = ((double)t->p[0][k] + t->p[1][k] + t->p[2][k]) / 3.0;
                }
                c[ax] = 2.0 * mid - c[ax];
                if (fabs(n[ax]) > 0.98 * rm_norm(n) && pieces_has(b, nb, c, ax, -sg)) {
                    continue;
                }
            }
            done[2u * *ndone] = t->id;
            done[2u * *ndone + 1u] = (unsigned)ax;
            (*ndone)++;
            {
                /* (a face made at the chair's back: its plain frame) */
                const int back = g_plain.on && ax == g_plain.ax && -sg == -g_plain.front;

                for (j = 0; j < 3u; j++) {
                    const unsigned s0 = j == 0 ? 0u : 3u - j;     /* (corners the other way round: it looks out) */

                    for (k = 0; k < 3; k++) {
                        p[j][k] = t->p[s0][k];
                    }
                    p[j][ax] = 2.0 * mid - t->p[s0][ax];
                    uv[j][0] = back ? g_plain.uv[0] : t->uv[s0][0];
                    uv[j][1] = back ? g_plain.uv[1] : t->uv[s0][1];
                }
                rm_tri(out, t->id, -1, 1, back ? g_plain.rgb : t->rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
            }
        }
    }
}

/* close=2: is there a face not lying level (upright, or a slanted corner; the model's in b, or one made from `from` on in out) through point q, its plane
 * within 0.15 of q and q within its corners' box? */
static int pieces_wall_at(const RmBatch *b, unsigned nb, const RmOut *out, unsigned from, const double q[3])
{
    unsigned bi, i;
    int k;

    for (bi = 0; bi <= nb; bi++) {
        const unsigned n = bi < nb ? (b[bi].bb ? 0u : b[bi].n) : out->ntri - from;

        for (i = 0; i < n; i++) {
            const float (*pp)[3] = bi < nb ? b[bi].tri[i].p : (const float (*)[3])out->tri[from + i].p;
            double e1[3], e2[3], nn[3], d[3], len;
            int in = 1;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)pp[1][k] - pp[0][k];
                e2[k] = (double)pp[2][k] - pp[0][k];
                d[k] = q[k] - pp[0][k];
            }
            rm_cross(e1, e2, nn);
            len = rm_norm(nn);
            if (len < 1e-9 || fabs(nn[1]) > 0.9 || fabs(rm_dot(d, nn)) > 0.15) {   /* (upright or slanted: not a top) */
                continue;
            }
            for (k = 0; k < 3 && in; k++) {
                in = q[k] >= fmin(fmin(pp[0][k], pp[1][k]), pp[2][k]) - 0.1 && q[k] <= fmax(fmax(pp[0][k], pp[1][k]), pp[2][k]) + 0.1;
            }
            if (in) {
                return 1;
            }
        }
    }
    return 0;
}

/* close=2: the top sheets' open edges closed down to their boxes' bottoms */
static void pieces_skirt(const RmBatch *b, unsigned nb, const CBox *bx, unsigned nbx, RmOut *out, unsigned from)
{
    typedef struct { double a[3], c[3], mid[3]; unsigned src; unsigned char rgb[3]; int uses; } Edge;
    Edge *ed;
    unsigned ne = 0, cap = 0, bi, i, j, q;
    int k;

    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].bb ? 0u : b[bi].n * 3u;
    }
    ed = (Edge *)calloc(cap + 1u, sizeof *ed);
    if (ed == NULL) {
        out->fail = 1;
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], cen[3];

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
                cen[k] = ((double)t->p[0][k] + t->p[1][k] + t->p[2][k]) / 3.0;
            }
            rm_cross(e1, e2, n);
            if (rm_norm(n) < 1e-9 || n[1] < 0.98) {
                continue;
            }
            for (j = 0; j < 3u; j++) {
                const float *pa = t->p[j], *pc = t->p[(j + 1u) % 3u];
                unsigned w;
                int found = 0;

                for (w = 0; w < ne && !found; w++) {
                    if ((fabs(ed[w].a[0] - pc[0]) < 0.05 && fabs(ed[w].a[1] - pc[1]) < 0.05 && fabs(ed[w].a[2] - pc[2]) < 0.05 &&
                         fabs(ed[w].c[0] - pa[0]) < 0.05 && fabs(ed[w].c[1] - pa[1]) < 0.05 && fabs(ed[w].c[2] - pa[2]) < 0.05) ||
                        (fabs(ed[w].a[0] - pa[0]) < 0.05 && fabs(ed[w].a[1] - pa[1]) < 0.05 && fabs(ed[w].a[2] - pa[2]) < 0.05 &&
                         fabs(ed[w].c[0] - pc[0]) < 0.05 && fabs(ed[w].c[1] - pc[1]) < 0.05 && fabs(ed[w].c[2] - pc[2]) < 0.05)) {
                        ed[w].uses++;
                        found = 1;
                    }
                }
                if (!found && ne < cap) {
                    for (k = 0; k < 3; k++) {
                        ed[ne].a[k] = pa[k];
                        ed[ne].c[k] = pc[k];
                        ed[ne].mid[k] = cen[k];
                    }
                    ed[ne].src = t->id;
                    memcpy(ed[ne].rgb, t->rgb, 3u);
                    ed[ne].uses = 1;
                    ne++;
                }
            }
        }
    }
    for (i = 0; i < ne; i++) {
        double m[3], qd[3], out_n[3], bottom = 1e30, p4[4][3], uv[2];
        const Edge *e = &ed[i];

        if (e->uses != 1) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            m[k] = (e->a[k] + e->c[k]) / 2.0;
        }
        /* (looking out: across the edge, away from its triangle's middle) */
        out_n[0] = -(e->c[2] - e->a[2]);
        out_n[1] = 0.0;
        out_n[2] = e->c[0] - e->a[0];
        if (rm_norm(out_n) < 1e-6) {
            continue;
        }
        if ((m[0] - e->mid[0]) * out_n[0] + (m[2] - e->mid[2]) * out_n[2] < 0.0) {
            out_n[0] = -out_n[0];
            out_n[2] = -out_n[2];
        }
        qd[0] = m[0] - out_n[0] * 0.05;
        qd[1] = m[1] - 0.6;
        qd[2] = m[2] - out_n[2] * 0.05;
        if (pieces_wall_at(b, nb, out, from, qd)) {
            continue;
        }
        /* (down to the bottom of the box under the top here) */
        for (q = 0; q < nbx; q++) {
            if (fabs(bx[q].hi[1] - m[1]) < 0.3 && m[0] >= bx[q].lo[0] - 3.0 && m[0] <= bx[q].hi[0] + 3.0 &&
                m[2] >= bx[q].lo[2] - 3.0 && m[2] <= bx[q].hi[2] + 3.0) {
                bottom = fmin(bottom, bx[q].lo[1]);
            }
        }
        if (bottom > m[1] - 0.2 || bottom > 1e29) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            p4[0][k] = e->a[k];
            p4[1][k] = e->c[k];
            p4[2][k] = e->c[k];
            p4[3][k] = e->a[k];
        }
        p4[2][1] = bottom;
        p4[3][1] = bottom;
        uv[0] = g_plain.on ? g_plain.uv[0] : 0.0;
        uv[1] = g_plain.on ? g_plain.uv[1] : 0.0;
        {
            double f1[3], f2[3], fn[3];
            const unsigned char *rgb = g_plain.on ? g_plain.rgb : e->rgb;

            for (k = 0; k < 3; k++) {
                f1[k] = p4[1][k] - p4[0][k];
                f2[k] = p4[2][k] - p4[0][k];
            }
            rm_cross(f1, f2, fn);
            if (rm_dot(fn, out_n) >= 0.0) {
                rm_tri(out, e->src, -1, 1, rgb, p4[0], p4[1], p4[2], uv, uv, uv, 0, 0);
                rm_tri(out, e->src, -1, 1, rgb, p4[0], p4[2], p4[3], uv, uv, uv, 0, 0);
            } else {
                rm_tri(out, e->src, -1, 1, rgb, p4[0], p4[2], p4[1], uv, uv, uv, 0, 0);
                rm_tri(out, e->src, -1, 1, rgb, p4[0], p4[3], p4[2], uv, uv, uv, 0, 0);
            }
        }
    }
    free(ed);
}

static void box_pieces(const RmBatch *b, unsigned nb, int sym, RmOut *out)
{
    CBox bx[CB_MAX];
    unsigned nbx = 0, bi, i, bj, j, q, *done = NULL, ndone = 0, total = 0;
    const unsigned made0 = out->ntri;
    int k, ax;
    double all_lo[3] = { 1e30, 1e30, 1e30 }, all_hi[3] = { -1e30, -1e30, -1e30 };

    /* 1. the boxes: an axis face, and a face across it meeting one of its edges, whose span along the first face's
     *    axis starts at the first face's plane */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], lo[3], hi[3];
            int a = -1;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
                lo[k] = fmin(fmin(t->p[0][k], t->p[1][k]), t->p[2][k]);
                hi[k] = fmax(fmax(t->p[0][k], t->p[1][k]), t->p[2][k]);
            }
            rm_cross(e1, e2, n);
            if (rm_norm(n) < 1e-9) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                a = fabs(n[k]) > 0.98 ? k : a;
            }
            if (a < 0) {
                continue;
            }
            for (bj = 0; bj < nb; bj++) {
                for (j = 0; !b[bj].bb && j < b[bj].n; j++) {
                    const RmTri *u = &b[bj].tri[j];
                    double f1[3], f2[3], m[3], ulo[3], uhi[3], far;
                    int c = -1, edge, other;

                    for (k = 0; k < 3; k++) {
                        f1[k] = (double)u->p[1][k] - u->p[0][k];
                        f2[k] = (double)u->p[2][k] - u->p[0][k];
                        ulo[k] = fmin(fmin(u->p[0][k], u->p[1][k]), u->p[2][k]);
                        uhi[k] = fmax(fmax(u->p[0][k], u->p[1][k]), u->p[2][k]);
                    }
                    rm_cross(f1, f2, m);
                    if (rm_norm(m) < 1e-9) {
                        continue;
                    }
                    for (k = 0; k < 3; k++) {
                        c = fabs(m[k]) > 0.98 ? k : c;
                    }
                    if (c < 0 || c == a) {
                        continue;
                    }
                    /* (u's plane is an edge of t, across c; u's span along a starts at t's plane) */
                    edge = fabs(ulo[c] - lo[c]) < 0.2 || fabs(ulo[c] - hi[c]) < 0.2;
                    if (!edge) {
                        continue;
                    }
                    other = 3 - a - c;
                    if (uhi[other] < lo[other] + 0.2 || ulo[other] > hi[other] - 0.2) {
                        continue;
                    }
                    if (fabs(ulo[a] - lo[a]) < 0.2 && uhi[a] > lo[a] + 0.5) {
                        far = uhi[a];
                    } else if (fabs(uhi[a] - lo[a]) < 0.2 && ulo[a] < lo[a] - 0.5) {
                        far = ulo[a];
                    } else {
                        continue;
                    }
                    {
                        CBox nbox;
                        int dup = 0;

                        for (k = 0; k < 3; k++) {
                            nbox.lo[k] = k == a ? fmin(lo[a], far) : k == c ? fmin(lo[c], ulo[c]) : lo[k];
                            nbox.hi[k] = k == a ? fmax(lo[a], far) : k == c ? fmax(hi[c], uhi[c]) : hi[k];
                        }
                        /* (t is one triangle of its face: the box takes the face's whole span along the third axis
                         * from u, which runs along it) */
                        nbox.lo[other] = fmin(nbox.lo[other], ulo[other]);
                        nbox.hi[other] = fmax(nbox.hi[other], uhi[other]);
                        for (q = 0; q < nbx && !dup; q++) {
                            int same = 1;

                            for (k = 0; k < 3; k++) {
                                same &= fabs(bx[q].lo[k] - nbox.lo[k]) < 0.3 && fabs(bx[q].hi[k] - nbox.hi[k]) < 0.3;
                            }
                            dup = same;
                        }
                        if (!dup && nbx < CB_MAX) {
                            bx[nbx++] = nbox;
                        }
                    }
                }
            }
        }
    }
    /* (a box inside a bigger one is a piece of it: one triangle's pairing) */
    for (q = 0; q < nbx; q++) {
        for (i = 0; i < nbx; i++) {
            int inside = i != q;

            for (k = 0; k < 3 && inside; k++) {
                inside = bx[q].lo[k] >= bx[i].lo[k] - 0.3 && bx[q].hi[k] <= bx[i].hi[k] + 0.3;
            }
            if (inside && !(fabs(bx[q].lo[0] - bx[i].lo[0]) < 0.3 && fabs(bx[q].hi[0] - bx[i].hi[0]) < 0.3 &&
                            fabs(bx[q].lo[1] - bx[i].lo[1]) < 0.3 && fabs(bx[q].hi[1] - bx[i].hi[1]) < 0.3 &&
                            fabs(bx[q].lo[2] - bx[i].lo[2]) < 0.3 && fabs(bx[q].hi[2] - bx[i].hi[2]) < 0.3)) {
                bx[q] = bx[--nbx];
                q--;
                break;
            }
        }
    }
    if (getenv("RM_DEBUG") != NULL) {
        for (q = 0; q < nbx; q++) {
            fprintf(stderr, "box: %.1f..%.1f %.1f..%.1f %.1f..%.1f\n", bx[q].lo[0], bx[q].hi[0], bx[q].lo[1], bx[q].hi[1],
                    bx[q].lo[2], bx[q].hi[2]);
        }
    }
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            for (k = 0; k < 9; k++) {
                all_lo[k % 3] = fmin(all_lo[k % 3], b[bi].tri[i].p[k / 3][k % 3]);
                all_hi[k % 3] = fmax(all_hi[k % 3], b[bi].tri[i].p[k / 3][k % 3]);
            }
            total++;
        }
    }
    done = (unsigned *)malloc((total + 1u) * 2u * sizeof *done);
    if (done == NULL) {
        out->fail = 1;
        return;
    }
    /* the chair's front (from its back rest, the highest box, toward the rest) and its plain frame (a pixel of the
     * biggest face on its left or right side) */
    memset(&g_plain, 0, sizeof g_plain);
    if ((sym == 0 || sym == 2) && nbx > 0) {
        const int oth = 2 - sym;
        double best = 0.0, top = -1e30, bmid = 0.0;

        for (q = 0; q < nbx; q++) {
            if (bx[q].hi[1] > top + 0.3 || (fabs(bx[q].hi[1] - top) <= 0.3 && bx[q].hi[oth] - bx[q].lo[oth] < 0.0)) {
                top = bx[q].hi[1];
                bmid = (bx[q].lo[oth] + bx[q].hi[oth]) / 2.0;
            }
        }
        g_plain.ax = oth;
        g_plain.front = (all_lo[oth] + all_hi[oth]) / 2.0 >= bmid ? 1 : -1;
        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                const RmTri *t = &b[bi].tri[i];
                double e1[3], e2[3], n[3], a;

                for (k = 0; k < 3; k++) {
                    e1[k] = (double)t->p[1][k] - t->p[0][k];
                    e2[k] = (double)t->p[2][k] - t->p[0][k];
                }
                rm_cross(e1, e2, n);
                a = rm_norm(n);
                if (a > best && fabs(n[sym]) > 0.98) {
                    best = a;
                    g_plain.on = 1;
                    g_plain.uv[0] = ((double)t->uv[0][0] + t->uv[1][0] + t->uv[2][0]) / 3.0;
                    g_plain.uv[1] = ((double)t->uv[0][1] + t->uv[1][1] + t->uv[2][1]) / 3.0;
                    memcpy(g_plain.rgb, t->rgb, 3u);
                }
            }
        }
    }
    /* 2a. across its symmetry: the whole thing is one box, its drawn side turned over through its middle */
    if (sym == 0 || sym == 2) {
        CBox whole;
        double ap = 0.0, an = 0.0;

        memcpy(whole.lo, all_lo, sizeof whole.lo);
        memcpy(whole.hi, all_hi, sizeof whole.hi);
        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                double e1[3], e2[3], n[3];

                for (k = 0; k < 3; k++) {
                    e1[k] = (double)b[bi].tri[i].p[1][k] - b[bi].tri[i].p[0][k];
                    e2[k] = (double)b[bi].tri[i].p[2][k] - b[bi].tri[i].p[0][k];
                }
                rm_cross(e1, e2, n);
                ap += n[sym] > 0.98 * rm_norm(n) ? 1.0 : 0.0;
                an += n[sym] < -0.98 * rm_norm(n) ? 1.0 : 0.0;
            }
        }
        /* (every face leaning to the side drawn the more, wherever it is: a left leg's outer and inner sides make the
         * right leg's) */
        pieces_turn(b, nb, &whole, sym, ap >= an ? 1 : -1, 0, done, &ndone, out);
        pieces_turn(b, nb, &whole, sym, ap >= an ? -1 : 1, 0, done, &ndone, out);
    }
    /* 2b. along the other axis, box by box: a side drawn and its opposite not, the drawn side's faces (and the
     *     slanted ones leaning that way) turned over through the box's middle */
    for (ax = 0; ax < 3; ax += 2) {
        CBox mg[CB_MAX];
        unsigned nmg = nbx, r, changed = 1;

        if (ax == sym) {
            continue;
        }
        memcpy(mg, bx, nbx * sizeof *mg);
        while (changed) {
            changed = 0;
            for (q = 0; q < nmg && !changed; q++) {
                for (r = q + 1; r < nmg && !changed; r++) {
                    int join = fabs(mg[q].lo[ax] - mg[r].lo[ax]) < 0.3 && fabs(mg[q].hi[ax] - mg[r].hi[ax]) < 0.3;

                    for (k = 0; k < 3 && join; k++) {
                        join = k == ax || (mg[q].lo[k] <= mg[r].hi[k] + 0.3 && mg[r].lo[k] <= mg[q].hi[k] + 0.3);
                    }
                    if (join) {
                        for (k = 0; k < 3; k++) {
                            mg[q].lo[k] = fmin(mg[q].lo[k], mg[r].lo[k]);
                            mg[q].hi[k] = fmax(mg[q].hi[k], mg[r].hi[k]);
                        }
                        mg[r] = mg[--nmg];
                        changed = 1;
                    }
                }
            }
        }
        for (q = 0; q < nmg; q++) {
            const double cp = cb_cover(b, nb, &mg[q], ax, 1), cn = cb_cover(b, nb, &mg[q], ax, -1);
            const int sg = cp > 0.2 && cp > 2.0 * cn ? 1 : cn > 0.2 && cn > 2.0 * cp ? -1 : 0;

            if (getenv("RM_DEBUG") != NULL) {
                fprintf(stderr, "box: axis %d: %.1f..%.1f %.1f..%.1f %.1f..%.1f cover %.2f %.2f\n", ax, mg[q].lo[0], mg[q].hi[0],
                        mg[q].lo[1], mg[q].hi[1], mg[q].lo[2], mg[q].hi[2], cp, cn);
            }
            if (sg != 0) {
                pieces_turn(b, nb, &mg[q], ax, sg, 1, done, &ndone, out);
            }
        }
    }
    /* 3. a side still open and not against another box: drawn over in its neighbour's picture (a chair's: its plain
     *    frame) */
    g_plain.caps = 1;
    for (q = 0; q < nbx; q++) {
        RmBatch one;
        CBox wide = bx[q];
        const CBox *x = &wide;
        static const int sax[4] = { 0, 0, 2, 2 }, ssg[4] = { -1, 1, -1, 1 };
        int s;

        if (sym == 0 || sym == 2) {
            const double mid = (all_lo[sym] + all_hi[sym]) / 2.0, l = wide.lo[sym], h = wide.hi[sym];

            wide.lo[sym] = fmin(l, 2.0 * mid - h);
            wide.hi[sym] = fmax(h, 2.0 * mid - l);
        }

        for (s = 0; s < 4; s++) {
            const int a = sax[s], h = a == 0 ? 2 : 0;
            double c[3];
            int covered = 0;
            unsigned r;

            if (a == sym || cb_cover(b, nb, x, a, ssg[s]) >= 0.1 || cb_cover(b, nb, x, a, -ssg[s]) >= 0.1) {
                continue;               /* (drawn, or its opposite drawn: turned over in step 2) */
            }
            c[a] = ssg[s] > 0 ? x->hi[a] + 0.3 : x->lo[a] - 0.3;
            c[h] = (x->lo[h] + x->hi[h]) / 2.0;
            c[1] = (x->lo[1] + x->hi[1]) / 2.0;
            for (r = 0; r < nbx && !covered; r++) {
                covered = r != q && c[0] >= bx[r].lo[0] && c[0] <= bx[r].hi[0] && c[1] >= bx[r].lo[1] && c[1] <= bx[r].hi[1] &&
                          c[2] >= bx[r].lo[2] && c[2] <= bx[r].hi[2];
            }
            if (covered) {
                continue;
            }
            {
                /* (box_close on this box alone: its own faces, bounds the box) */
                RmTri *own = (RmTri *)malloc(4096u * sizeof *own);
                unsigned nown = 0;

                if (own == NULL) {
                    out->fail = 1;
                    return;
                }
                one = b[0];
                for (bi = 0; bi < nb; bi++) {
                    for (i = 0; !b[bi].bb && i < b[bi].n && nown < 4096u; i++) {
                        const RmTri *t = &b[bi].tri[i];
                        int in = 1;

                        for (k = 0; k < 9 && in; k++) {
                            in = t->p[k / 3][k % 3] >= x->lo[k % 3] - 0.2 && t->p[k / 3][k % 3] <= x->hi[k % 3] + 0.2;
                        }
                        if (in) {
                            own[nown++] = *t;
                            one.tex = b[bi].tex;
                        }
                    }
                }
                one.tri = own;
                one.n = nown;
                one.bb = 0;
                box_close_side(&one, 1, x->lo, x->hi, s, out);
                free(own);
            }
        }
    }
    pieces_skirt(b, nb, bx, nbx, out, made0);
    free(done);
    memset(&g_plain, 0, sizeof g_plain);
}

void rm_box(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double deep = rm_optf(opt, "deep", 6.0), drop = rm_optf(opt, "floor", 0.0), edge = rm_optf(opt, "edge", 1.5);
    unsigned bi;

    if (rm_optf(opt, "close", 0.0) == 2.0) {
        box_pieces(b, nb, (int)rm_optf(opt, "sym", -1.0), out);
        return;
    }
    g_close_cover = rm_optf(opt, "cover", 0.9);
    if (rm_optf(opt, "close", 0.0) != 0.0) {
        char ib[96];
        double in[6];

        /* `in=X0:X1:Y0:Y1:Z0:Z1`: only the triangles wholly inside that box are the box (a shelf model that also
         * holds its shadow sheet and the things on its top) */
        if (rm_opt(opt, "in", ib, sizeof ib) != NULL &&
            sscanf(ib, "%lf:%lf:%lf:%lf:%lf:%lf", &in[0], &in[1], &in[2], &in[3], &in[4], &in[5]) == 6) {
            RmBatch *fb = (RmBatch *)calloc(nb ? nb : 1u, sizeof *fb);
            RmTri **ft = (RmTri **)calloc(nb ? nb : 1u, sizeof *ft);
            unsigned i, k, nfb = 0;

            for (bi = 0; fb != NULL && ft != NULL && bi < nb; bi++) {
                unsigned n = 0;

                ft[bi] = (RmTri *)malloc((b[bi].n + 1u) * sizeof **ft);
                if (ft[bi] == NULL) {
                    continue;
                }
                for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                    int ok = 1;

                    for (k = 0; k < 9u && ok; k++) {
                        const double v = b[bi].tri[i].p[k / 3u][k % 3u];

                        ok = v >= in[2 * (k % 3u)] && v <= in[2 * (k % 3u) + 1u];
                    }
                    if (ok) {
                        ft[bi][n++] = b[bi].tri[i];
                    }
                }
                if (n > 0) {
                    fb[nfb] = b[bi];
                    fb[nfb].tri = ft[bi];
                    fb[nfb].n = n;
                    nfb++;
                }
            }
            if (fb != NULL && ft != NULL) {
                box_close(fb, nfb, out);
            } else {
                out->fail = 1;
            }
            for (bi = 0; ft != NULL && bi < nb; bi++) {
                free(ft[bi]);
            }
            free(ft);
            free(fb);
            return;
        }
        box_close(b, nb, out);
        return;
    }

    if (rm_optf(opt, "slab", 0.0) > 0.0) {
        for (bi = 0; bi < nb; bi++) {
            box_slab(&b[bi], tex, ntex, opt, out);
        }
        return;
    }
    if (rm_optf(opt, "lie", 0.0) > 0.0) {
        for (bi = 0; bi < nb; bi++) {
            box_lie(&b[bi], tex, ntex, opt, out);
        }
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, m[3][3];
        double foot_z = 0.0, top_z = 0.0, len, u0, u1, vf, vt, du, dv, y0, z0, z1, x0, x1;
        int tw = 16, th = 32, j, k;
        Bx x;

        {
            /* (cards lying level: remotes on the floor) */
            double yl = bt->tri[0].p[0][1], yh = yl;
            unsigned q;

            for (q = 0; q < bt->n; q++) {
                for (k = 0; k < 3; k++) {
                    yl = bt->tri[q].p[k][1] < yl ? bt->tri[q].p[k][1] : yl;
                    yh = bt->tri[q].p[k][1] > yh ? bt->tri[q].p[k][1] : yh;
                }
            }
            if (yh - yl < 0.01) {
                box_flat(bt, tex, ntex, opt, out);
                continue;
            }
        }
        if (bt->n != 2 || rm_plane(&bt->tri[0], m) != 0) {
            continue;
        }
        if (bt->tex >= 0 && (unsigned)bt->tex < ntex) {
            tw = tex[bt->tex].w;
            th = tex[bt->tex].h;
        }
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 3; k++) {
                int a;

                for (a = 0; a < 3; a++) {
                    lo[a] = bt->tri[j].p[k][a] < lo[a] ? bt->tri[j].p[k][a] : lo[a];
                    hi[a] = bt->tri[j].p[k][a] > hi[a] ? bt->tri[j].p[k][a] : hi[a];
                }
            }
        }
        /* the foot: the card's low edge; the top: its high edge */
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 3; k++) {
                if (fabs(bt->tri[j].p[k][1] - lo[1]) < 0.01) {
                    foot_z = bt->tri[j].p[k][2];
                } else if (fabs(bt->tri[j].p[k][1] - hi[1]) < 0.01) {
                    top_z = bt->tri[j].p[k][2];
                }
            }
        }
        len = sqrt((hi[1] - lo[1]) * (hi[1] - lo[1]) + (foot_z - top_z) * (foot_z - top_z));
        /* the picture's uv at the card's corners: u across x, v from the foot (vf) to the top (vt) */
        {
            double pa[3], pb[3], uva[2], uvb[2];

            pa[0] = lo[0]; pa[1] = lo[1]; pa[2] = foot_z;
            pb[0] = hi[0]; pb[1] = hi[1]; pb[2] = top_z;
            /* (solve uv from the plane: p = u m0 + v m1 + m2) */
            for (j = 0; j < 2; j++) {
                const double *p = j == 0 ? pa : pb;
                double *uv = j == 0 ? uva : uvb;
                double d[3], a00 = rm_dot(m[0], m[0]), a01 = rm_dot(m[0], m[1]), a11 = rm_dot(m[1], m[1]), det;

                for (k = 0; k < 3; k++) {
                    d[k] = p[k] - m[2][k];
                }
                det = a00 * a11 - a01 * a01;
                uv[0] = (rm_dot(d, m[0]) * a11 - rm_dot(d, m[1]) * a01) / det;
                uv[1] = (rm_dot(d, m[1]) * a00 - rm_dot(d, m[0]) * a01) / det;
            }
            u0 = uva[0]; vf = uva[1];
            u1 = uvb[0]; vt = uvb[1];
        }
        /* the thing's own outline in the picture: the card is wider than what is painted on it (its edges are
         * see-through); the box takes the opaque pixels' columns and rows, and its sides read the outline's own
         * edge pixels */
        if (bt->tex >= 0 && (unsigned)bt->tex < ntex && tex[bt->tex].rgba != NULL) {
            const unsigned char *px = tex[bt->tex].rgba;
            const double ulo = u0 < u1 ? u0 : u1, uhi = u0 < u1 ? u1 : u0, vlo = vf < vt ? vf : vt, vhi = vf < vt ? vt : vf;
            int c0 = (int)floor(ulo * tw), c1 = (int)ceil(uhi * tw) - 1, r0 = (int)floor(vlo * th), r1 = (int)ceil(vhi * th) - 1;
            int oc0 = 1 << 20, oc1 = -1, or0 = 1 << 20, or1 = -1, cc, rr;

            for (rr = r0; rr <= r1; rr++) {
                for (cc = c0; cc <= c1; cc++) {
                    int x_ = ((cc % tw) + tw) % tw, y_ = ((rr % th) + th) % th;

                    if (px[(y_ * tw + x_) * 4 + 3] >= 128) {
                        oc0 = cc < oc0 ? cc : oc0;
                        oc1 = cc > oc1 ? cc : oc1;
                        or0 = rr < or0 ? rr : or0;
                        or1 = rr > or1 ? rr : or1;
                    }
                }
            }
            if (oc1 >= oc0 && or1 >= or0) {
                /* the new uv corners and the box's x span and height, scaled from the card's */
                const double nu0 = (double)oc0 / tw, nu1 = (double)(oc1 + 1) / tw, nv0 = (double)or0 / th, nv1 = (double)(or1 + 1) / th;
                const double fx0 = (nu0 - ulo) / (uhi - ulo), fx1 = (nu1 - ulo) / (uhi - ulo);
                const double fy_foot = vf > vt ? (vf - nv1) / (vf - vt) : (nv0 - vf) / (vt - vf);
                const double fy_top = vf > vt ? (vf - nv0) / (vf - vt) : (nv1 - vf) / (vt - vf);
                const double w = hi[0] - lo[0];
                double nlo0 = lo[0], nhi0 = hi[0];

                if (u0 < u1) {
                    nlo0 = lo[0] + w * fx0;
                    nhi0 = lo[0] + w * fx1;
                    u0 = nu0;
                    u1 = nu1;
                } else {
                    nlo0 = lo[0] + w * (1.0 - fx1);
                    nhi0 = lo[0] + w * (1.0 - fx0);
                    u0 = nu1;
                    u1 = nu0;
                }
                lo[0] = nlo0;
                hi[0] = nhi0;
                if (vf > vt) {
                    vf = nv1;
                    vt = nv0;
                } else {
                    vf = nv0;
                    vt = nv1;
                }
                len = len * (fy_top - fy_foot);
            }
        }
        du = edge / tw * (u1 > u0 ? 1.0 : -1.0);
        dv = edge / th * (vf > vt ? 1.0 : -1.0);   /* (inward from the top row, toward the foot) */
        x.o = out;
        x.src = bt->tri[0].id;
        x.rgb[0] = bt->rgb[0]; x.rgb[1] = bt->rgb[1]; x.rgb[2] = bt->rgb[2];
        y0 = lo[1] - drop;
        x0 = lo[0];
        x1 = hi[0];
        /* the front looks the way the card's foot faces: toward the side the card leans away from */
        {
            const double s = top_z < foot_z ? -1.0 : 1.0;   /* the back lies this way along z */
            const double nf[3] = { 0, 0, -s }, nb_[3] = { 0, 0, s }, nl[3] = { -1, 0, 0 }, nr[3] = { 1, 0, 0 }, nu[3] = { 0, 1, 0 };
            double q[4][3], uv[4][2];

            z0 = foot_z;
            z1 = foot_z + s * deep;
            /* front */
            q[0][0] = x0; q[0][1] = y0;       q[0][2] = z0; uv[0][0] = u0; uv[0][1] = vf;
            q[1][0] = x1; q[1][1] = y0;       q[1][2] = z0; uv[1][0] = u1; uv[1][1] = vf;
            q[2][0] = x1; q[2][1] = y0 + len; q[2][2] = z0; uv[2][0] = u1; uv[2][1] = vt;
            q[3][0] = x0; q[3][1] = y0 + len; q[3][2] = z0; uv[3][0] = u0; uv[3][1] = vt;
            bx_quad(&x, q, uv, nf, 1.0);
            /* sides: the picture's edge column, its bands by height */
            for (j = 0; j < 2; j++) {
                const double xs = j == 0 ? x0 : x1, uc = j == 0 ? u0 + du : u1 - du;

                q[0][0] = xs; q[0][1] = y0;       q[0][2] = z0; uv[0][0] = uc; uv[0][1] = vf;
                q[1][0] = xs; q[1][1] = y0;       q[1][2] = z1; uv[1][0] = uc; uv[1][1] = vf;
                q[2][0] = xs; q[2][1] = y0 + len; q[2][2] = z1; uv[2][0] = uc; uv[2][1] = vt;
                q[3][0] = xs; q[3][1] = y0 + len; q[3][2] = z0; uv[3][0] = uc; uv[3][1] = vt;
                bx_quad(&x, q, uv, j == 0 ? nl : nr, 0.9);
            }
            /* back: the side's column across */
            q[0][0] = x0; q[0][1] = y0;       q[0][2] = z1; uv[0][0] = u0 + du; uv[0][1] = vf;
            q[1][0] = x1; q[1][1] = y0;       q[1][2] = z1; uv[1][0] = u0 + du; uv[1][1] = vf;
            q[2][0] = x1; q[2][1] = y0 + len; q[2][2] = z1; uv[2][0] = u0 + du; uv[2][1] = vt;
            q[3][0] = x0; q[3][1] = y0 + len; q[3][2] = z1; uv[3][0] = u0 + du; uv[3][1] = vt;
            bx_quad(&x, q, uv, nb_, 0.8);
            /* top: the picture's top row across */
            q[0][0] = x0; q[0][1] = y0 + len; q[0][2] = z0; uv[0][0] = u0; uv[0][1] = vt + dv;
            q[1][0] = x1; q[1][1] = y0 + len; q[1][2] = z0; uv[1][0] = u1; uv[1][1] = vt + dv;
            q[2][0] = x1; q[2][1] = y0 + len; q[2][2] = z1; uv[2][0] = u1; uv[2][1] = vt + dv;
            q[3][0] = x0; q[3][1] = y0 + len; q[3][2] = z1; uv[3][0] = u0; uv[3][1] = vt + dv;
            bx_quad(&x, q, uv, nu, 1.0);
        }
        rm_drop_all(out, bt);
    }
}
