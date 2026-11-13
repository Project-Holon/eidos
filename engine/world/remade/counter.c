/*
 * engine/world/remade/counter.c -- a kitchen counter: a top on cupboards,
 * with a sink and a stove (the L-shaped counter of the houses' kitchens).
 *
 * The map draws only the sides the field's camera sees: the cupboards'
 * fronts, with doors.  What is made:
 *   1. The sides under the top's outline where the model has none (its back,
 *      the far ends, and the corner of the L, whose top stands on nothing):
 *      the top's edge in its plain colour, and under it a plain panel in the
 *      cupboards' frame wood down to their plinth.  A back has no doors.
 *   2. A wall of the sink that is missing (it has the three the camera sees),
 *      in the picture of the wall across from it.
 *   3. What is painted on the top stands up from it: the tap, the stove's
 *      burner and its grate.  Each pixel of the picture that is not the top's
 *      own colour is a small block on the face it is painted on, its top in
 *      the pixel's colour, its sides darker.
 *
 * Settings:
 *   edge=U:V0:V1    the top's edge: the texture's column U, rows V0 (at the top) to V1
 *   panel=U:V0:V1   the panel under it, the same way (V1: at the floor)
 *   post=U:W        the panel's posts at its outer corners: the texture's column U, W map units wide
 *   lift=U0:V0:U1:V1:H[:CUT]+..  the pictures that stand up: a place of the texture
 *                   and the blocks' height in map units; CUT 1: a sloped end of the
 *                   board the picture is painted on is left out; CUT 2: and its part
 *                   of the picture goes on straight, level with the rest
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

/* A level or upright face of the model, in doubles. */
typedef struct {
    double p[3][3], n[3], uv[3][2];
    unsigned id;
    unsigned char rgb[3];
    int tex;
} Ct;

static double tri_area(const Ct *t)
{
    double e1[3], e2[3], c[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = t->p[1][k] - t->p[0][k];
        e2[k] = t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, c);
    return 0.5 * sqrt(rm_dot(c, c));
}

/* Is (x, z) inside the triangle seen from above? */
static int in_xz(const Ct *t, double x, double z)
{
    double ax = t->p[0][0], az = t->p[0][2], bx = t->p[1][0], bz = t->p[1][2], cx = t->p[2][0], cz = t->p[2][2];
    double den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz), w0, w1;

    if (fabs(den) < 1e-9) {
        return 0;
    }
    w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / den;
    w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / den;
    return w0 >= -1e-4 && w1 >= -1e-4 && 1.0 - w0 - w1 >= -1e-4;
}

/* Does an upright face of the model, looking along `n`, cover the point c (in its plane, to a third of a unit)? */
static int has_face(const Ct *t, unsigned nt, const double c[3], const double n[3])
{
    unsigned i;
    int k;

    for (i = 0; i < nt; i++) {
        double f1[3], f2[3], d[3], den, w1, w2;

        if (rm_dot(t[i].n, n) < 0.9) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            f1[k] = t[i].p[1][k] - t[i].p[0][k];
            f2[k] = t[i].p[2][k] - t[i].p[0][k];
            d[k] = c[k] - t[i].p[0][k];
        }
        if (fabs(rm_dot(d, t[i].n)) > 0.3) {
            continue;
        }
        den = rm_dot(f1, f1) * rm_dot(f2, f2) - rm_dot(f1, f2) * rm_dot(f1, f2);
        if (fabs(den) < 1e-9) {
            continue;
        }
        w1 = (rm_dot(d, f1) * rm_dot(f2, f2) - rm_dot(d, f2) * rm_dot(f1, f2)) / den;
        w2 = (rm_dot(d, f2) * rm_dot(f1, f1) - rm_dot(d, f1) * rm_dot(f1, f2)) / den;
        if (w1 >= -0.01 && w2 >= -0.01 && 1.0 - w1 - w2 >= -0.01) {
            return 1;
        }
    }
    return 0;
}

/* An upright rectangle from a to b along the ground, from y0 up to y1, looking along n; the texture's column u,
 * row v0 at the top to v1 at the bottom (one colour across: a plain board). */
static void board(RmOut *out, const Ct *src, const double a[2], const double b[2], double y0, double y1,
                  const double n[3], double u, double v0, double v1)
{
    double p0[3] = { a[0], y1, a[1] }, p1[3] = { b[0], y1, b[1] }, p2[3] = { b[0], y0, b[1] }, p3[3] = { a[0], y0, a[1] };
    double t0[2] = { u, v0 }, t1[2] = { u, v0 }, t2[2] = { u, v1 }, t3[2] = { u, v1 };
    double e1[3], e2[3], c[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p1[k] - p0[k];
        e2[k] = p2[k] - p0[k];
    }
    rm_cross(e1, e2, c);
    if (rm_dot(c, n) >= 0.0) {
        rm_tri(out, src->id, -1, 1, src->rgb, p0, p1, p2, t0, t1, t2, 0, 0);
        rm_tri(out, src->id, -1, 1, src->rgb, p0, p2, p3, t0, t2, t3, 0, 0);
    } else {
        rm_tri(out, src->id, -1, 1, src->rgb, p0, p2, p1, t0, t2, t1, 0, 0);
        rm_tri(out, src->id, -1, 1, src->rgb, p0, p3, p2, t0, t3, t2, 0, 0);
    }
}

static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return x < y ? -1 : x > y;
}

/* The distinct values of a sorted list (to a twentieth of a unit): their count. */
static unsigned uniq(double *v, unsigned n)
{
    unsigned i, m = 0;

    qsort(v, n, sizeof *v, dcmp);
    for (i = 0; i < n; i++) {
        if (m == 0 || v[i] - v[m - 1] > 0.05) {
            v[m++] = v[i];
        }
    }
    return m;
}

/* 1. the sides under the top's outline */
static void sides(const Ct *t, unsigned nt, const char *opt, RmOut *out)
{
    double ytop = 0.0, best = 0.0, yfloor = 1e30, yband = -1e30;
    double eu = 0.1, ev0 = 0.486, ev1 = 0.531, pu = 0.016, pv0 = 0.5625, pv1 = 1.0, pdu = 0.484, ppost = 0.8;
    double *xs, *zs;
    unsigned i, j, nx = 0, nz = 0, ix, iz;
    unsigned char *cov;
    const Ct *edge_src = NULL, *panel_src = NULL;
    char buf[64];

    if (rm_opt(opt, "edge", buf, sizeof buf) != NULL) {
        sscanf(buf, "%lf:%lf:%lf", &eu, &ev0, &ev1);
    }
    if (rm_opt(opt, "panel", buf, sizeof buf) != NULL) {
        sscanf(buf, "%lf:%lf:%lf", &pu, &pv0, &pv1);
    }
    if (rm_opt(opt, "post", buf, sizeof buf) != NULL) {
        sscanf(buf, "%lf:%lf", &pdu, &ppost);
    }
    /* the top: the height of the biggest level face */
    for (i = 0; i < nt; i++) {
        for (j = 0; j < 3u; j++) {
            yfloor = t[i].p[j][1] < yfloor ? t[i].p[j][1] : yfloor;
        }
        if (t[i].n[1] > 0.9 && tri_area(&t[i]) > best) {
            best = tri_area(&t[i]);
            ytop = t[i].p[0][1];
        }
    }
    /* the top's edge: the upright faces that reach the top; its foot is theirs.  The panel: those that reach the
     * floor.  (Their materials: a new side is drawn as they are.) */
    for (i = 0; i < nt; i++) {
        double lo = 1e30, hi = -1e30;

        if (fabs(t[i].n[1]) > 0.1 || t[i].tex < 0) {
            continue;
        }
        for (j = 0; j < 3u; j++) {
            lo = t[i].p[j][1] < lo ? t[i].p[j][1] : lo;
            hi = t[i].p[j][1] > hi ? t[i].p[j][1] : hi;
        }
        /* (the lowest foot of those: a sink's walls reach the top too, from higher up) */
        if (fabs(hi - ytop) < 0.1 && lo > yfloor + 1.0) {
            if (edge_src == NULL || lo < yband) {
                yband = lo;
                edge_src = &t[i];
            }
        } else if (lo < yfloor + 0.1) {
            panel_src = &t[i];
        }
    }
    if (edge_src == NULL || panel_src == NULL || ytop - yfloor < 1.0) {
        return;
    }
    /* the top's outline: a grid of the places where the top's corners are, a cell is the top's when a level face
     * near the top's height is over it (the sink's floor and the tap's board are the top's too) */
    xs = (double *)malloc(nt * 3u * sizeof *xs);
    zs = (double *)malloc(nt * 3u * sizeof *zs);
    if (xs == NULL || zs == NULL) {
        free(xs);
        free(zs);
        out->fail = 1;
        return;
    }
    for (i = 0; i < nt; i++) {
        if (t[i].n[1] > 0.9 && fabs(t[i].p[0][1] - ytop) < 3.0) {
            for (j = 0; j < 3u; j++) {
                xs[nx++] = t[i].p[j][0];
                zs[nz++] = t[i].p[j][2];
            }
        }
    }
    nx = uniq(xs, nx);
    nz = uniq(zs, nz);
    cov = (unsigned char *)calloc((size_t)(nx + 1) * (nz + 1), 1);
    if (cov == NULL || nx < 2 || nz < 2) {
        free(xs);
        free(zs);
        free(cov);
        out->fail = cov == NULL;
        return;
    }
    for (ix = 0; ix + 1 < nx; ix++) {
        for (iz = 0; iz + 1 < nz; iz++) {
            double cx = (xs[ix] + xs[ix + 1]) / 2.0, cz = (zs[iz] + zs[iz + 1]) / 2.0;

            for (i = 0; i < nt && !cov[ix * nz + iz]; i++) {
                cov[ix * nz + iz] = t[i].n[1] > 0.9 && fabs(t[i].p[0][1] - ytop) < 3.0 && in_xz(&t[i], cx, cz);
            }
        }
    }
#define COV(x_, z_) ((x_) >= 0 && (z_) >= 0 && (unsigned)(x_) + 1 < nx && (unsigned)(z_) + 1 < nz && cov[(unsigned)(x_) * nz + (unsigned)(z_)])
    /* each cell's side where the next cell is not the top's: a side there, unless the model has one */
    for (ix = 0; ix + 1 < nx; ix++) {
        for (iz = 0; iz + 1 < nz; iz++) {
            int d;

            if (!COV((int)ix, (int)iz)) {
                continue;
            }
            for (d = 0; d < 4; d++) {
                const int dx = d == 0 ? 1 : d == 1 ? -1 : 0, dz = d == 2 ? 1 : d == 3 ? -1 : 0;
                double n[3] = { (double)dx, 0.0, (double)dz }, a[2], b[2], mid[3];

                if (COV((int)ix + dx, (int)iz + dz)) {
                    continue;
                }
                if (dx != 0) {
                    a[0] = b[0] = dx > 0 ? xs[ix + 1] : xs[ix];
                    a[1] = zs[iz];
                    b[1] = zs[iz + 1];
                } else {
                    a[1] = b[1] = dz > 0 ? zs[iz + 1] : zs[iz];
                    a[0] = xs[ix];
                    b[0] = xs[ix + 1];
                }
                mid[0] = (a[0] + b[0]) / 2.0;
                mid[2] = (a[1] + b[1]) / 2.0;
                mid[1] = (ytop + yband) / 2.0;
                if (!has_face(t, nt, mid, n)) {
                    board(out, edge_src, a, b, yband, ytop, n, eu, ev0, ev1);
                }
                mid[1] = (yband + yfloor) / 2.0;
                if (!has_face(t, nt, mid, n)) {
                    /* (at an outer corner the panel ends in a post of the frame's dark, as the doors' fronts do:
                     * an end is a corner where the side does not go on along the next cell) */
                    double aa[2], bb[2], pa[2], pb[2], len = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
                    double ux = (b[0] - a[0]) / len, uz = (b[1] - a[1]) / len, w = ppost < len / 3.0 ? ppost : len / 3.0;
                    const int sx = dx != 0 ? 0 : 1, sz = dx != 0 ? 1 : 0;
                    int ca = !COV((int)ix - sx, (int)iz - sz) || COV((int)ix - sx + dx, (int)iz - sz + dz);
                    int cb = !COV((int)ix + sx, (int)iz + sz) || COV((int)ix + sx + dx, (int)iz + sz + dz);

                    memcpy(aa, a, sizeof aa);
                    memcpy(bb, b, sizeof bb);
                    if (ca) {
                        pa[0] = a[0] + ux * w; pa[1] = a[1] + uz * w;
                        board(out, panel_src, a, pa, yfloor, yband, n, pdu, pv0, pv1);
                        memcpy(aa, pa, sizeof aa);
                    }
                    if (cb) {
                        pb[0] = b[0] - ux * w; pb[1] = b[1] - uz * w;
                        board(out, panel_src, pb, b, yfloor, yband, n, pdu, pv0, pv1);
                        memcpy(bb, pb, sizeof bb);
                    }
                    board(out, panel_src, aa, bb, yfloor, yband, n, pu, pv0, pv1);
                }
            }
        }
    }
#undef COV
    free(xs);
    free(zs);
    free(cov);
}

/* 2. the sink's missing wall: the sink is a level face under the top; each of its four sides that has no wall
 * gets one from its floor to the top, in the picture of the wall across from it */
static void sink(const Ct *t, unsigned nt, RmOut *out)
{
    double ytop = 0.0, best = 0.0;
    unsigned i, j;

    for (i = 0; i < nt; i++) {
        if (t[i].n[1] > 0.9 && tri_area(&t[i]) > best) {
            best = tri_area(&t[i]);
            ytop = t[i].p[0][1];
        }
    }
    for (i = 0; i < nt; i++) {
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, yf;
        int d, k;

        if (!(t[i].n[1] > 0.9 && t[i].p[0][1] < ytop - 0.5 && t[i].p[0][1] > ytop - 6.0)) {
            continue;
        }
        /* (the floor's box: both its triangles together) */
        for (j = 0; j < nt; j++) {
            if (t[j].n[1] > 0.9 && fabs(t[j].p[0][1] - t[i].p[0][1]) < 0.05) {
                unsigned c;

                for (c = 0; c < 3u; c++) {
                    for (k = 0; k < 3; k++) {
                        lo[k] = t[j].p[c][k] < lo[k] ? t[j].p[c][k] : lo[k];
                        hi[k] = t[j].p[c][k] > hi[k] ? t[j].p[c][k] : hi[k];
                    }
                }
            }
        }
        yf = t[i].p[0][1];
        for (d = 0; d < 4; d++) {
            /* the wall at that side looks into the sink: away from the side */
            const int dx = d == 0 ? 1 : d == 1 ? -1 : 0, dz = d == 2 ? 1 : d == 3 ? -1 : 0;
            double n[3] = { (double)-dx, 0.0, (double)-dz }, na[3] = { (double)dx, 0.0, (double)dz }, mid[3], a[2], b[2];
            const Ct *across = NULL;

            if (dx != 0) {
                a[0] = b[0] = dx > 0 ? hi[0] : lo[0];
                a[1] = lo[2];
                b[1] = hi[2];
            } else {
                a[1] = b[1] = dz > 0 ? hi[2] : lo[2];
                a[0] = lo[0];
                b[0] = hi[0];
            }
            mid[0] = (a[0] + b[0]) / 2.0;
            mid[1] = (yf + ytop) / 2.0;
            mid[2] = (a[1] + b[1]) / 2.0;
            if (has_face(t, nt, mid, n)) {
                continue;
            }
            /* the wall across: it looks the other way, at the other side */
            for (j = 0; j < nt && across == NULL; j++) {
                double c2[3];

                if (rm_dot(t[j].n, na) < 0.9) {
                    continue;
                }
                c2[0] = dx != 0 ? (dx > 0 ? lo[0] : hi[0]) : mid[0];
                c2[1] = mid[1];
                c2[2] = dz != 0 ? (dz > 0 ? lo[2] : hi[2]) : mid[2];
                if (has_face(&t[j], 1, c2, na)) {
                    across = &t[j];
                }
            }
            if (across != NULL) {
                double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
                double p0[3] = { a[0], ytop, a[1] }, p1[3] = { b[0], ytop, b[1] }, p2[3] = { b[0], yf, b[1] }, p3[3] = { a[0], yf, a[1] };
                double t0[2], t1[2], t2[2], t3[2], e1[3], e2[3], c[3];

                for (j = 0; j < 3u; j++) {
                    u0 = across->uv[j][0] < u0 ? across->uv[j][0] : u0;
                    u1 = across->uv[j][0] > u1 ? across->uv[j][0] : u1;
                    v0 = across->uv[j][1] < v0 ? across->uv[j][1] : v0;
                    v1 = across->uv[j][1] > v1 ? across->uv[j][1] : v1;
                }
                t0[0] = u0; t0[1] = v0; t1[0] = u1; t1[1] = v0; t2[0] = u1; t2[1] = v1; t3[0] = u0; t3[1] = v1;
                for (k = 0; k < 3; k++) {
                    e1[k] = p1[k] - p0[k];
                    e2[k] = p2[k] - p0[k];
                }
                rm_cross(e1, e2, c);
                if (rm_dot(c, n) >= 0.0) {
                    rm_tri(out, across->id, -1, 1, across->rgb, p0, p1, p2, t0, t1, t2, 0, 0);
                    rm_tri(out, across->id, -1, 1, across->rgb, p0, p2, p3, t0, t2, t3, 0, 0);
                } else {
                    rm_tri(out, across->id, -1, 1, across->rgb, p0, p2, p1, t0, t2, t1, 0, 0);
                    rm_tri(out, across->id, -1, 1, across->rgb, p0, p3, p2, t0, t3, t2, 0, 0);
                }
            }
        }
        return;                 /* (one sink) */
    }
}

/* The place on triangle t (carried on past its edges) whose texture place is (u, v). */
static int uv_to_p(const Ct *t, double u, double v, double out[3])
{
    double a1 = t->uv[1][0] - t->uv[0][0], b1 = t->uv[2][0] - t->uv[0][0];
    double a2 = t->uv[1][1] - t->uv[0][1], b2 = t->uv[2][1] - t->uv[0][1];
    double det = a1 * b2 - b1 * a2, du = u - t->uv[0][0], dv = v - t->uv[0][1], s, r;
    int k;

    if (fabs(det) < 1e-12) {
        return 0;
    }
    s = (du * b2 - b1 * dv) / det;
    r = (a1 * dv - du * a2) / det;
    for (k = 0; k < 3; k++) {
        out[k] = t->p[0][k] + s * (t->p[1][k] - t->p[0][k]) + r * (t->p[2][k] - t->p[0][k]);
    }
    return 1;
}

static int uv_in(const Ct *t, double u, double v)
{
    double ax = t->uv[0][0], ay = t->uv[0][1], bx = t->uv[1][0], by = t->uv[1][1], cx = t->uv[2][0], cy = t->uv[2][1];
    double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy), w0, w1;

    if (fabs(den) < 1e-12) {
        return 0;
    }
    w0 = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / den;
    w1 = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / den;
    return w0 >= -1e-6 && w1 >= -1e-6 && 1.0 - w0 - w1 >= -1e-6;
}

static void quad(RmOut *out, const Ct *s, const unsigned char rgb[3], const double p[4][3], const double uv[2], const double n[3])
{
    double e1[3], e2[3], c[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    rm_cross(e1, e2, c);
    if (rm_dot(c, n) >= 0.0) {
        rm_tri(out, s->id, -1, 1, rgb, p[0], p[1], p[2], uv, uv, uv, 0, 0);
        rm_tri(out, s->id, -1, 1, rgb, p[0], p[2], p[3], uv, uv, uv, 0, 0);
    } else {
        rm_tri(out, s->id, -1, 1, rgb, p[0], p[2], p[1], uv, uv, uv, 0, 0);
        rm_tri(out, s->id, -1, 1, rgb, p[0], p[3], p[2], uv, uv, uv, 0, 0);
    }
}

/* 3. a painted picture standing up from its face: a block a pixel */
static void lift(const Ct *t, unsigned nt, const RmTex *tex, unsigned ntex, const double r[6], RmOut *out)
{
    const RmTex *st = NULL;
    int x0, x1, y0, y1, x, y, w;
    unsigned i, best = 0;
    short *owner;
    unsigned *cnt;
    unsigned bg = 0;

    for (i = 0; i < nt && st == NULL; i++) {
        if (t[i].tex >= 0 && (unsigned)t[i].tex < ntex && tex[t[i].tex].rgba != NULL) {
            st = &tex[t[i].tex];
        }
    }
    if (st == NULL) {
        return;
    }
    x0 = (int)floor(r[0] * st->w);
    y0 = (int)floor(r[1] * st->h);
    x1 = (int)ceil(r[2] * st->w);
    y1 = (int)ceil(r[3] * st->h);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > st->w ? st->w : x1;
    y1 = y1 > st->h ? st->h : y1;
    w = x1 - x0;
    if (w <= 0 || y1 <= y0) {
        return;
    }
    owner = (short *)malloc((size_t)w * (y1 - y0) * sizeof *owner);
    cnt = (unsigned *)calloc(4096u, sizeof *cnt);
    if (owner == NULL || cnt == NULL) {
        free(owner);
        free(cnt);
        out->fail = 1;
        return;
    }
    /* which face each pixel is painted on (a level or sloped face of the top that shows that place) */
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            double u = (x + 0.5) / st->w, v = (y + 0.5) / st->h;
            short o = -1;

            for (i = 0; i < nt && o < 0; i++) {
                if (t[i].n[1] > 0.5 && t[i].tex >= 0 && &tex[t[i].tex] == st && uv_in(&t[i], u, v)) {
                    o = (short)i;
                }
            }
            owner[(size_t)(y - y0) * w + (x - x0)] = o;
            if (o >= 0) {
                const unsigned char *px = st->rgba + ((size_t)y * st->w + x) * 4u;

                cnt[((px[0] >> 4) << 8) | ((px[1] >> 4) << 4) | (px[2] >> 4)]++;
            }
        }
    }
    /* the face's own colour: the commonest there */
    for (i = 0; i < 4096u; i++) {
        if (cnt[i] > best) {
            best = cnt[i];
            bg = i;
        }
    }
    free(cnt);
    /* (CUT: only what is painted on the picture's flattest face stands up; a sloped end of the board it is
     * painted on is left out, with its part of the picture: a tap's end bent down the slope) */
    if (r[5] != 0.0) {
        short flat = -1;
        size_t q;

        for (q = 0; q < (size_t)w * (y1 - y0); q++) {
            if (owner[q] >= 0 && (flat < 0 || t[owner[q]].n[1] > t[flat].n[1])) {
                flat = owner[q];
            }
        }
        for (q = 0; q < (size_t)w * (y1 - y0); q++) {
            if (owner[q] >= 0 && t[owner[q]].n[1] < t[flat].n[1] - 0.01) {
                rm_drop(out, t[owner[q]].id);
                /* (CUT 2: that part of the picture goes on straight, level with the rest) */
                owner[q] = r[5] == 2.0 ? flat : -1;
            }
        }
    }
#define PX(x_, y_) (st->rgba + ((size_t)(y_) * st->w + (size_t)(x_)) * 4u)
#define UP(x_, y_) ((x_) >= x0 && (x_) < x1 && (y_) >= y0 && (y_) < y1 && owner[(size_t)((y_) - y0) * w + ((x_) - x0)] >= 0 \
                    && PX(x_, y_)[3] >= 128 && (abs((PX(x_, y_)[0] >> 4) - (int)(bg >> 8)) + abs((PX(x_, y_)[1] >> 4) - (int)((bg >> 4) & 15)) \
                                                + abs((PX(x_, y_)[2] >> 4) - (int)(bg & 15))) >= 3)
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const Ct *f;
            double c[4][3], top[4][3], uv[2], side[4][3];
            unsigned char dark[3];
            int k, j, ok = 1;

            if (!UP(x, y)) {
                continue;
            }
            f = &t[owner[(size_t)(y - y0) * w + (x - x0)]];
            uv[0] = (x + 0.5) / st->w;
            uv[1] = (y + 0.5) / st->h;
            /* the pixel's corners on its face, and over them by the height */
            ok &= uv_to_p(f, (double)x / st->w, (double)y / st->h, c[0]);
            ok &= uv_to_p(f, (double)(x + 1) / st->w, (double)y / st->h, c[1]);
            ok &= uv_to_p(f, (double)(x + 1) / st->w, (double)(y + 1) / st->h, c[2]);
            ok &= uv_to_p(f, (double)x / st->w, (double)(y + 1) / st->h, c[3]);
            if (!ok) {
                continue;
            }
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++) {
                    top[j][k] = c[j][k] + f->n[k] * r[4];
                }
            }
            quad(out, f, f->rgb, top, uv, f->n);
            for (k = 0; k < 3; k++) {
                dark[k] = (unsigned char)(f->rgb[k] * 0.72);
            }
            /* a side where the pixel beside is lower (not standing up) */
            for (j = 0; j < 4; j++) {
                const int nx = x + (j == 1 ? 1 : j == 3 ? -1 : 0), ny = y + (j == 2 ? 1 : j == 0 ? -1 : 0);
                const int a = j, b = (j + 1) % 4;
                double sn[3], mid[3], cen[3];

                if (UP(nx, ny)) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    side[0][k] = c[a][k];
                    side[1][k] = c[b][k];
                    side[2][k] = top[b][k];
                    side[3][k] = top[a][k];
                    mid[k] = (c[a][k] + c[b][k]) / 2.0;
                    cen[k] = (c[0][k] + c[1][k] + c[2][k] + c[3][k]) / 4.0;
                    sn[k] = mid[k] - cen[k];
                }
                quad(out, f, dark, side, uv, sn);
            }
        }
    }
#undef UP
#undef PX
    free(owner);
}

void rm_counter(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi, i, nt = 0, cap = 0;
    Ct *t;
    char lb[256], *lp;
    int k;

    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].bb ? 0u : b[bi].n;
    }
    t = (Ct *)malloc((cap + 1u) * sizeof *t);
    if (t == NULL) {
        out->fail = 1;
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *s = &b[bi].tri[i];
            Ct *c = &t[nt];
            double e1[3], e2[3];
            unsigned j;

            for (j = 0; j < 3u; j++) {
                for (k = 0; k < 3; k++) {
                    c->p[j][k] = s->p[j][k];
                }
                c->uv[j][0] = s->uv[j][0];
                c->uv[j][1] = s->uv[j][1];
            }
            for (k = 0; k < 3; k++) {
                e1[k] = c->p[1][k] - c->p[0][k];
                e2[k] = c->p[2][k] - c->p[0][k];
            }
            rm_cross(e1, e2, c->n);
            if (rm_norm(c->n) <= 1e-9) {
                continue;
            }
            c->id = s->id;
            memcpy(c->rgb, s->rgb, 3);
            c->tex = b[bi].tex;
            nt++;
        }
    }
    sides(t, nt, opt, out);
    sink(t, nt, out);
    if (rm_opt(opt, "lift", lb, sizeof lb) != NULL) {
        for (lp = lb; lp != NULL && *lp != '\0'; ) {
            double r[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

            if (sscanf(lp, "%lf:%lf:%lf:%lf:%lf:%lf", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]) >= 5) {
                lift(t, nt, tex, ntex, r, out);
            }
            lp = strchr(lp, '+');
            lp = lp != NULL ? lp + 1 : NULL;
        }
    }
    free(t);
    rm_seal(b, nb, out, 0.7);
}
