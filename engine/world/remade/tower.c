/*
 * engine/world/remade/tower.c -- a tower on a block (the control tower): the
 * map has its south half; most of its parts go on round to the north, but
 * the faces that look north are not in the model, so from the sides and the
 * back it is open.
 *
 * What is made, from the model's own faces and texture pixels:
 *   mirror=X0:X1:Y0:Y1:ZC+..  the parts within that box are round about
 *       z = ZC (a balcony, a glass cab, its roof, lamps): their faces that
 *       look south are turned to look north, where the model has none (a face
 *       that lands on one of the model's own is left out).
 *   wall=SX:X0:X1:Z:Y0:Y1+..  a back wall at z = Z from X0 to X1, Y0 to Y1
 *       (a block's, a parapet's, a shaft's), in the texture of the side face
 *       at x = SX that reaches furthest back: its corner columns at each end,
 *       its plain column between, row by row, as the sides are.
 *   under=Y+..  the underside at the height Y (an overhang, a balcony): the
 *       outline of the faces that end there, closed, looking down, in the
 *       colour of the face over it.
 *   props=Y0:SIZE  the small things standing above Y0 (roof furniture), each
 *       closed about its own middle; a single flat card (a leaf) both sides.
 *   clipz=Z  the model's faces that reach behind z = Z are cut there.
 *   mirror's box may end :Z0:Z1, the faces whose middle lies between.
 *   corner=W  the side's corner columns (map units, default 6); 0: no corner
 *       columns, the side's picture runs on along the back wall (boards).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    const RmTri *t;
    const RmBatch *b;
    double n[3];
} Ft;

static void normal_of(const RmTri *t, double n[3])
{
    double e1[3], e2[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = (double)t->p[1][k] - t->p[0][k];
        e2[k] = (double)t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) <= 0.0) {
        n[0] = n[1] = n[2] = 0.0;
    }
}

/* Is the point on one of the model's faces that looks the same way (within 0.1 of its plane)? */
static int on_face(const Ft *f, unsigned nf, const double p[3], const double n[3])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double d[3], e1[3], e2[3], w[3], c1[3], c2[3], c3[3], a0[3], a1[3], a2[3];
        int k;

        if (rm_dot(f[i].n, n) < 0.9) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            d[k] = p[k] - t->p[0][k];
        }
        if (fabs(rm_dot(d, f[i].n)) > 0.1) {
            continue;
        }
        /* (inside: the point is on the inner side of all three edges) */
        for (k = 0; k < 3; k++) {
            e1[k] = (double)t->p[1][k] - t->p[0][k];
            e2[k] = (double)t->p[2][k] - t->p[1][k];
            w[k] = (double)t->p[0][k] - t->p[2][k];
            a0[k] = p[k] - t->p[0][k];
            a1[k] = p[k] - t->p[1][k];
            a2[k] = p[k] - t->p[2][k];
        }
        rm_cross(e1, a0, c1);
        rm_cross(e2, a1, c2);
        rm_cross(w, a2, c3);
        if (rm_dot(c1, f[i].n) >= -1e-3 && rm_dot(c2, f[i].n) >= -1e-3 && rm_dot(c3, f[i].n) >= -1e-3) {
            return 1;
        }
    }
    return 0;
}

/* how a face's texture runs by z (a) and y (b) */
typedef struct { double u0, v0, ua, ub, va, vb, a0, b0; } Map;

static int map_of(const RmTri *t, Map *m)
{
    const double da1 = (double)t->p[1][2] - t->p[0][2], db1 = (double)t->p[1][1] - t->p[0][1];
    const double da2 = (double)t->p[2][2] - t->p[0][2], db2 = (double)t->p[2][1] - t->p[0][1];
    const double det = da1 * db2 - da2 * db1;
    const double du1 = (double)t->uv[1][0] - t->uv[0][0], du2 = (double)t->uv[2][0] - t->uv[0][0];
    const double dv1 = (double)t->uv[1][1] - t->uv[0][1], dv2 = (double)t->uv[2][1] - t->uv[0][1];

    if (fabs(det) < 1e-9) {
        return -1;
    }
    m->a0 = t->p[0][2];
    m->b0 = t->p[0][1];
    m->u0 = t->uv[0][0];
    m->v0 = t->uv[0][1];
    m->ua = (du1 * db2 - du2 * db1) / det;
    m->ub = (du2 * da1 - du1 * da2) / det;
    m->va = (dv1 * db2 - dv2 * db1) / det;
    m->vb = (dv2 * da1 - dv1 * da2) / det;
    return 0;
}

static void map_at(const Map *m, double z, double y, double uv[2])
{
    uv[0] = m->u0 + m->ua * (z - m->a0) + m->ub * (y - m->b0);
    uv[1] = m->v0 + m->va * (z - m->a0) + m->vb * (y - m->b0);
}

/* a four-cornered face drawn from both sides, lit by the game, its corners turned to look along `outn` */
static void quad(RmOut *o, unsigned src, const unsigned char rgb[3], const double p[4][3], const double t[4][2],
                 const double outn[3])
{
    double e1[3], e2[3], n[3];
    unsigned first = o->ntri, i;
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    rm_cross(e1, e2, n);
    if (rm_dot(n, outn) >= 0.0) {
        rm_tri(o, src, -1, 0, rgb, p[0], p[1], p[2], t[0], t[1], t[2], 0, 0);
        rm_tri(o, src, -1, 0, rgb, p[0], p[2], p[3], t[0], t[2], t[3], 0, 0);
    } else {
        rm_tri(o, src, -1, 0, rgb, p[0], p[2], p[1], t[0], t[2], t[1], 0, 0);
        rm_tri(o, src, -1, 0, rgb, p[0], p[3], p[2], t[0], t[3], t[2], 0, 0);
    }
    for (i = first; i < o->ntri; i++) {
        o->tri[i].lit = 1;
    }
}

static void do_mirror(const Ft *f, unsigned nf, RmOut *out, const double bx[7])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double p[3][3], uv[3][2], c[3] = { 0, 0, 0 }, mn[3];
        int j, k, in = 1;

        /* (a face that looks south; or any face of a part that lies in the south half: a beam's top) */
        for (j = 0; j < 3; j++) {
            c[2] += t->p[j][2] / 3.0;
        }
        if (f[i].n[2] < 0.3 && c[2] <= bx[4] + 0.5) {
            continue;
        }
        c[2] = 0.0;
        for (j = 0; j < 3; j++) {
            in &= t->p[j][0] >= bx[0] - 0.05 && t->p[j][0] <= bx[1] + 0.05 && t->p[j][1] >= bx[2] - 0.05
                  && t->p[j][1] <= bx[3] + 0.05;
        }
        if (in && bx[6] > bx[5]) {
            /* (`:Z0:Z1`: only faces whose middle lies between) */
            const double cz = ((double)t->p[0][2] + t->p[1][2] + t->p[2][2]) / 3.0;

            in = cz >= bx[5] && cz <= bx[6];
        }
        if (!in) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            const int s = j == 0 ? 0 : 3 - j;     /* (mirrored: corners the other way round) */

            p[j][0] = t->p[s][0];
            p[j][1] = t->p[s][1];
            p[j][2] = 2.0 * bx[4] - t->p[s][2];
            uv[j][0] = t->uv[s][0];
            uv[j][1] = t->uv[s][1];
            for (k = 0; k < 3; k++) {
                c[k] += p[j][k] / 3.0;
            }
        }
        mn[0] = f[i].n[0];
        mn[1] = f[i].n[1];
        mn[2] = -f[i].n[2];
        if (on_face(f, nf, c, mn)) {
            continue;           /* (the model has that face already) */
        }
        rm_tri(out, t->id, -1, f[i].b->cull, t->rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
    }
}

/* the texture place of the side at x = sx, at (z, y): from the side's own triangle there, among those over the heights
 * y0..y1 (a quad's two triangles may map the picture differently; one's mapping carried past its edge is wrong) */
static int side_uv(const Ft *f, unsigned nf, double sx, double z, double y, double y0, double y1, double uv[2])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double ylo = 1e30, yhi = -1e30, den, w0, w1, w2;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - sx) < 0.1;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (!at || ylo < y0 - 0.05 || yhi > y1 + 0.05) {
            continue;
        }
        den = ((double)t->p[1][1] - t->p[2][1]) * ((double)t->p[0][2] - t->p[2][2])
              + ((double)t->p[2][2] - t->p[1][2]) * ((double)t->p[0][1] - t->p[2][1]);
        if (fabs(den) < 1e-9) {
            continue;
        }
        w0 = (((double)t->p[1][1] - t->p[2][1]) * (z - t->p[2][2]) + ((double)t->p[2][2] - t->p[1][2]) * (y - t->p[2][1])) / den;
        w1 = (((double)t->p[2][1] - t->p[0][1]) * (z - t->p[2][2]) + ((double)t->p[0][2] - t->p[2][2]) * (y - t->p[2][1])) / den;
        w2 = 1.0 - w0 - w1;
        if (w0 >= -1e-3 && w1 >= -1e-3 && w2 >= -1e-3) {
            uv[0] = w0 * t->uv[0][0] + w1 * t->uv[1][0] + w2 * t->uv[2][0];
            uv[1] = w0 * t->uv[0][1] + w1 * t->uv[1][1] + w2 * t->uv[2][1];
            return 1;
        }
    }
    return 0;
}

/* the back wall over the heights y0..y1 in one side face's texture */
static void wall_strip(RmOut *out, const Ft *f, unsigned nf, const Ft *sf, const double w[6], double cw, double zend,
                       double y0, double y1);

static void do_wall(const Ft *f, unsigned nf, RmOut *out, const double w[6], double cw)
{
    double best = 1e30;
    unsigned i;

    /* how far back the side at x = SX reaches */
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - w[0]) < 0.1;
        }
        for (j = 0; at && j < 3; j++) {
            best = t->p[j][2] < best ? t->p[j][2] : best;
        }
    }
    /* each of its faces that reaches that far, band by band (a side made of bands, each in its own place of the
     * texture): the wall over that band's heights in that face's texture */
    for (i = 0; best < 1e29 && i < nf; i++) {
        const RmTri *t = f[i].t;
        double ylo = 1e30, yhi = -1e30, zlo = 1e30;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - w[0]) < 0.1;
            zlo = t->p[j][2] < zlo ? t->p[j][2] : zlo;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        ylo = ylo < w[4] ? w[4] : ylo;
        yhi = yhi > w[5] ? w[5] : yhi;
        if (!at || zlo > best + 0.05 || yhi - ylo < 0.05) {
            continue;
        }
        /* (the band's two triangles both reach back: one strip a band) */
        {
            unsigned q;
            int dup = 0;

            for (q = 0; q < i && !dup; q++) {
                double qlo = 1e30, qhi = -1e30, qz = 1e30;
                int at2 = 1;

                if (fabs(f[q].n[0]) < 0.9 || f[q].b->tex < 0) {
                    continue;
                }
                for (j = 0; j < 3; j++) {
                    at2 &= fabs(f[q].t->p[j][0] - w[0]) < 0.1;
                    qz = f[q].t->p[j][2] < qz ? f[q].t->p[j][2] : qz;
                    qlo = f[q].t->p[j][1] < qlo ? f[q].t->p[j][1] : qlo;
                    qhi = f[q].t->p[j][1] > qhi ? f[q].t->p[j][1] : qhi;
                }
                qlo = qlo < w[4] ? w[4] : qlo;
                qhi = qhi > w[5] ? w[5] : qhi;
                dup = at2 && qz <= best + 0.05 && fabs(qlo - ylo) < 0.05 && fabs(qhi - yhi) < 0.05;
            }
            if (!dup) {
                wall_strip(out, f, nf, &f[i], w, cw, best, ylo, yhi);
            }
        }
    }
}

static void wall_strip(RmOut *out, const Ft *f, unsigned nf, const Ft *sf, const double w[6], double cw, double zend,
                       double y0, double y1)
{
    Map m;

    if (map_of(sf->t, &m) != 0) {
        return;
    }
    {
        const double outn[3] = { 0, 0, -1 };
        const double c = w[2] - w[1] > 3.0 * cw ? cw : (w[2] - w[1]) / 3.0;
        double p[4][3], t[4][2];
        int e;

        for (e = cw <= 0.0 ? 2 : 0; e < 3; e++) {
            /* (e 0, 1: the corner pieces at X1 and X0; e 2: the plain wall between) */
            const double xa = e == 0 ? w[2] : e == 1 ? w[1] : w[1] + c, xb = e == 0 ? w[2] - c : e == 1 ? w[1] + c : w[2] - c;
            unsigned q;

            p[0][0] = xa; p[0][1] = y0;
            p[1][0] = xb; p[1][1] = y0;
            p[2][0] = xb; p[2][1] = y1;
            p[3][0] = xa; p[3][1] = y1;
            for (q = 0; q < 4u; q++) {
                p[q][2] = w[3];
                /* (`corner=0`: the side's picture runs on along the wall, at its own scale: boards, their
                 * joints where the side has them) */
                const double zs = cw <= 0.0 ? zend + (p[q][0] - w[1]) : e < 2 ? zend + fabs(p[q][0] - xa) : zend + c - 0.3;
                const double ys = p[q][1] + (q < 2u ? 0.01 : -0.01);     /* (just inside the band) */

                if (!side_uv(f, nf, w[0], zs, ys, y0, y1, t[q])) {
                    map_at(&m, zs, p[q][1], t[q]);
                }
            }
            quad(out, sf->t->id, sf->b->rgb, p, t, outn);
        }
    }
}

static void do_under(const Ft *f, unsigned nf, RmOut *out, double y)
{
    double pt[256][2], hull[257][2], uv[2] = { 0, 0 };
    unsigned np = 0, nh = 0, i, src = 0;
    const RmBatch *sb = NULL;
    int j, k;

    /* the corners at height y of the upright faces that end there (their feet) */
    for (i = 0; i < nf && np < 250u; i++) {
        const RmTri *t = f[i].t;

        if (fabs(f[i].n[1]) > 0.3) {
            continue;
        }
        for (j = 0; j < 3 && np < 250u; j++) {
            int low = 1;

            for (k = 0; k < 3; k++) {
                low &= t->p[k][1] >= t->p[j][1] - 0.05;
            }
            if (low && fabs(t->p[j][1] - y) < 0.05) {
                pt[np][0] = t->p[j][0];
                pt[np][1] = t->p[j][2];
                np++;
                if (sb == NULL) {
                    sb = f[i].b;
                    src = t->id;
                    uv[0] = t->uv[j][0];
                    uv[1] = t->uv[j][1];
                }
            }
        }
    }
    if (np < 3u || sb == NULL) {
        return;
    }
    /* their outline: the convex hull (monotone chain) */
    for (i = 1; i < np; i++) {
        double a0 = pt[i][0], a1 = pt[i][1];

        for (j = (int)i - 1; j >= 0 && (pt[j][0] > a0 || (pt[j][0] == a0 && pt[j][1] > a1)); j--) {
            pt[j + 1][0] = pt[j][0];
            pt[j + 1][1] = pt[j][1];
        }
        pt[j + 1][0] = a0;
        pt[j + 1][1] = a1;
    }
    for (k = 0; k < 2; k++) {
        const unsigned base = nh;

        for (i = 0; i < np; i++) {
            const unsigned q = k == 0 ? i : np - 1 - i;

            while (nh >= base + 2u) {
                const double cr = (hull[nh - 1][0] - hull[nh - 2][0]) * (pt[q][1] - hull[nh - 2][1])
                                  - (hull[nh - 1][1] - hull[nh - 2][1]) * (pt[q][0] - hull[nh - 2][0]);

                if (cr > 1e-6) {
                    break;
                }
                nh--;
            }
            hull[nh][0] = pt[q][0];
            hull[nh][1] = pt[q][1];
            nh++;
        }
        nh--;
    }
    {
        const double outn[3] = { 0, -1, 0 };
        unsigned first = out->ntri;

        for (i = 1; i + 1 < nh; i++) {
            const double a[3] = { hull[0][0], y, hull[0][1] }, b[3] = { hull[i][0], y, hull[i][1] };
            const double c[3] = { hull[i + 1][0], y, hull[i + 1][1] };
            double e1[3], e2[3], n[3];

            for (k = 0; k < 3; k++) {
                e1[k] = b[k] - a[k];
                e2[k] = c[k] - a[k];
            }
            rm_cross(e1, e2, n);
            if (rm_dot(n, outn) >= 0.0) {
                rm_tri(out, src, -1, 0, sb->rgb, a, b, c, uv, uv, uv, 0, 0);
            } else {
                rm_tri(out, src, -1, 0, sb->rgb, a, c, b, uv, uv, uv, 0, 0);
            }
        }
        for (i = first; i < out->ntri; i++) {
            out->tri[i].lit = 1;
        }
    }
}

/* `clipz=Z`: the model's own faces that reach behind the back line z = Z (a flat picture inside a wall that runs on
 * past the house's back: the field never saw its end) are cut there; wholly behind, left out */
static void do_clip(const Ft *f, unsigned nf, RmOut *out, double zc)
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double poly[6][5], pz[4][5];
        int j, k, n = 0, behind = 0;

        for (j = 0; j < 3; j++) {
            behind += t->p[j][2] < zc - 0.01;
            pz[j][0] = t->p[j][0];
            pz[j][1] = t->p[j][1];
            pz[j][2] = t->p[j][2];
            pz[j][3] = t->uv[j][0];
            pz[j][4] = t->uv[j][1];
        }
        if (behind == 0) {
            continue;
        }
        rm_drop(out, t->id);
        if (behind == 3) {
            continue;
        }
        /* (Sutherland-Hodgman against z >= zc) */
        for (j = 0; j < 3; j++) {
            const double *a = pz[j], *b = pz[(j + 1) % 3];
            const int ain = a[2] >= zc - 0.01, bin = b[2] >= zc - 0.01;

            if (ain) {
                memcpy(poly[n++], a, sizeof poly[0]);
            }
            if (ain != bin) {
                const double s = (zc - a[2]) / (b[2] - a[2]);

                for (k = 0; k < 5; k++) {
                    poly[n][k] = a[k] + (b[k] - a[k]) * s;
                }
                n++;
            }
        }
        for (j = 1; j + 1 < n; j++) {
            double pa[3], pb[3], pc[3], ua[2], ub[2], uc[2];

            for (k = 0; k < 3; k++) {
                pa[k] = poly[0][k];
                pb[k] = poly[j][k];
                pc[k] = poly[j + 1][k];
            }
            ua[0] = poly[0][3]; ua[1] = poly[0][4];
            ub[0] = poly[j][3]; ub[1] = poly[j][4];
            uc[0] = poly[j + 1][3]; uc[1] = poly[j + 1][4];
            rm_tri(out, t->id, -1, f[i].b->cull, t->rgb, pa, pb, pc, ua, ub, uc, 0, 0);
        }
    }
}

/* `props=Y0:SIZE`: the small things standing above Y0 (a stool, a chair, a planter; pieces of triangles that share
 * corners, smaller than SIZE across): each is closed about its own middle as `mirror` closes a part; a piece that
 * is one flat card (a leaf) is shown from both sides. */
static void do_props(const Ft *f, unsigned nf, RmOut *out, double y0, double size)
{
    unsigned *grp = (unsigned *)malloc((size_t)(nf ? nf : 1) * sizeof *grp);
    unsigned i, j, g;

    if (grp == NULL) {
        out->fail = 1;
        return;
    }
    for (i = 0; i < nf; i++) {
        grp[i] = i;
    }
    /* (pieces: triangles of one draw that share a corner) */
    for (i = 0; i < nf; i++) {
        for (j = i + 1; j < nf; j++) {
            int a, c, touch = 0;

            if (f[i].b != f[j].b) {
                continue;
            }
            for (a = 0; a < 3 && !touch; a++) {
                for (c = 0; c < 3 && !touch; c++) {
                    touch = fabs(f[i].t->p[a][0] - f[j].t->p[c][0]) < 0.01 && fabs(f[i].t->p[a][1] - f[j].t->p[c][1]) < 0.01
                            && fabs(f[i].t->p[a][2] - f[j].t->p[c][2]) < 0.01;
                }
            }
            if (touch) {
                unsigned ri = i, rj = j;

                while (grp[ri] != ri) {
                    ri = grp[ri];
                }
                while (grp[rj] != rj) {
                    rj = grp[rj];
                }
                grp[rj > ri ? rj : ri] = rj > ri ? ri : rj;
            }
        }
    }
    for (i = 0; i < nf; i++) {
        while (grp[grp[i]] != grp[i]) {
            grp[i] = grp[grp[i]];
        }
        grp[i] = grp[grp[i]];
    }
    for (g = 0; g < nf; g++) {
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, bx[7];
        unsigned cnt = 0;
        int k, flat = 1;

        for (i = 0; i < nf; i++) {
            if (grp[i] != g) {
                continue;
            }
            for (j = 0; j < 3u; j++) {
                for (k = 0; k < 3; k++) {
                    lo[k] = f[i].t->p[j][k] < lo[k] ? f[i].t->p[j][k] : lo[k];
                    hi[k] = f[i].t->p[j][k] > hi[k] ? f[i].t->p[j][k] : hi[k];
                }
            }
            flat &= rm_dot(f[i].n, f[g].n) > 0.99;
            cnt++;
        }
        if (cnt == 0 || lo[1] < y0 || (hi[0] - lo[0]) + (hi[2] - lo[2]) > size) {
            continue;
        }
        if (flat && cnt <= 4u) {
            for (i = 0; i < nf; i++) {
                if (grp[i] == g) {
                    rm_two(out, f[i].t->id);
                }
            }
            continue;
        }
        /* (its own faces only: mirror the piece about its own middle) */
        {
            Ft *pf = (Ft *)malloc((size_t)cnt * sizeof *pf);
            unsigned np = 0;

            if (pf == NULL) {
                out->fail = 1;
                break;
            }
            for (i = 0; i < nf; i++) {
                if (grp[i] == g) {
                    pf[np++] = f[i];
                }
            }
            bx[0] = lo[0]; bx[1] = hi[0]; bx[2] = lo[1]; bx[3] = hi[1];
            bx[4] = (lo[2] + hi[2]) / 2.0;
            bx[5] = bx[6] = 0.0;
            do_mirror(pf, np, out, bx);
            free(pf);
        }
    }
    free(grp);
}

void rm_tower(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double cw = rm_optf(opt, "corner", 6.0);
    unsigned bi, i, nf = 0, cap = 0;
    Ft *f;
    char buf[512];
    const char *ep;

    (void)tex;
    (void)ntex;
    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].n;
    }
    f = (Ft *)malloc((size_t)(cap ? cap : 1) * sizeof *f);
    if (f == NULL) {
        out->fail = 1;
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            f[nf].t = &b[bi].tri[i];
            f[nf].b = &b[bi];
            normal_of(f[nf].t, f[nf].n);
            nf++;
        }
    }
    if (rm_optf(opt, "clipz", 1e30) < 1e29) {
        do_clip(f, nf, out, rm_optf(opt, "clipz", 0.0));
    }
    if (rm_opt(opt, "mirror", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double bx[7] = { 0, 0, 0, 0, 0, 0, 0 };

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3], &bx[4], &bx[5], &bx[6]) >= 5) {
                do_mirror(f, nf, out, bx);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "wall", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double w[6];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf", &w[0], &w[1], &w[2], &w[3], &w[4], &w[5]) == 6) {
                do_wall(f, nf, out, w, cw);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "props", buf, sizeof buf) != NULL) {
        double y0 = 0.0, size = 0.0;

        if (sscanf(buf, "%lf:%lf", &y0, &size) == 2) {
            do_props(f, nf, out, y0, size);
        }
    }
    if (rm_opt(opt, "under", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            do_under(f, nf, out, atof(ep));
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    free(f);
    rm_seal(b, nb, out, 0.7);
}
