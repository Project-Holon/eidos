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
 * and the four edges closed in the picture's own edge pixels.  The card stays as the map has it. */
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

void rm_box(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double deep = rm_optf(opt, "deep", 6.0), drop = rm_optf(opt, "floor", 0.0), edge = rm_optf(opt, "edge", 1.5);
    unsigned bi;

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
