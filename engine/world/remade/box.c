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

void rm_box(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double deep = rm_optf(opt, "deep", 6.0), drop = rm_optf(opt, "floor", 0.0), edge = rm_optf(opt, "edge", 1.5);
    unsigned bi;

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
