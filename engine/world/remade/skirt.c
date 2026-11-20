/*
 * engine/world/remade/skirt.c -- a room of walkways over a pit (Dragonspiral
 * Tower's floors): a wall under every edge of a walk surface that ends in
 * the air.
 *
 * The room is paths on tall blocks, joined by ramps.  The map gives a block
 * its sides, but a ramp is a sloped sheet alone and a door's landing a level
 * sheet alone: the field's steep camera sees their tops only.  A battle's low
 * camera sees sheets that hang in the air between the blocks.  So:
 *   - a walk surface is a face of the room that looks up (level or sloped);
 *   - its outline is every edge that belongs to one such face only;
 *     (a face laid on another walk surface, within `lay` units over it, is
 *     a picture on the floor, an emblem's tile the map hangs 4 over the
 *     floor and the battle lays on it: it has no edges of its own);
 *   - an edge of the outline is open when no walk surface goes on beside it
 *     (within `step` units under the point just outside the edge: a ramp's
 *     end on its landing) and the room's own upright faces under it, in the
 *     edge's upright plane and looking the edge's way out (a block's rim and
 *     side, a ramp's rim), stop over the pit's foot;
 *   - an open edge gets a wall from where those faces stop (the edge itself
 *     when there are none) down to the pit's foot (`foot`; default: the
 *     lowest corner of the room's upright faces), looking out,
 *     in the picture of the room's own block sides (the upright faces that
 *     hang under a walk surface's edge already; a room with none, walkways
 *     that hang in the air by design, gets no walls): the one that reaches
 *     lowest and is the largest gives the picture and how it is laid
 *     (a picture pixel a map unit along the wall; down the wall as that face
 *     lays it by the height, so the new wall's bands lie at the heights of the
 *     blocks' beside it), fading by the height as the block sides do (their
 *     colour at their top and at their foot, in six bands).
 *
 * Settings: foot (the pit's height), step (default 1), lay (default 6: the
 * height the battle lays a floor's picture from), min (an edge shorter than
 * this is left; default 2), minh (a wall lower than this is not made; 8).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define SK_BANDS 6              /* a new wall's bands of colour, top to foot */

typedef struct {
    const RmTri *t;
    unsigned batch;
    double n[3];
} SkFace;

static int sk_same(const float a[3], const float b[3])
{
    return fabs((double)a[0] - b[0]) < 0.05 && fabs((double)a[1] - b[1]) < 0.05 && fabs((double)a[2] - b[2]) < 0.05;
}

/* Is (x, z) inside the triangle seen from above?  *y: the triangle's height there. */
static int sk_over(const RmTri *t, double x, double z, double *y)
{
    const double ax = t->p[0][0], az = t->p[0][2], bx = t->p[1][0], bz = t->p[1][2], cx = t->p[2][0], cz = t->p[2][2];
    const double d = (bx - ax) * (cz - az) - (cx - ax) * (bz - az);
    double w1, w2;

    if (fabs(d) < 1e-9) {
        return 0;
    }
    w1 = ((x - ax) * (cz - az) - (cx - ax) * (z - az)) / d;
    w2 = ((bx - ax) * (z - az) - (x - ax) * (bz - az)) / d;
    if (w1 < -1e-6 || w2 < -1e-6 || w1 + w2 > 1.0 + 1e-6) {
        return 0;
    }
    *y = t->p[0][1] + w1 * ((double)t->p[1][1] - t->p[0][1]) + w2 * ((double)t->p[2][1] - t->p[0][1]);
    return 1;
}

/* How far down the room's own upright faces go under the point (x, z) of an edge, from the height y: the faces in
 * the upright plane through the point along (dx, dz) that look the way (ox, oz), each taken where the upright line
 * through the point crosses it; the lowest height reached without a gap.  mark: the faces that took it down. */
static double sk_cover(const SkFace *f, unsigned nf, double x, double z, double dx, double dz, double ox, double oz, double y,
                       unsigned char *mark)
{
    unsigned j;
    int again = 1, turns = 0, k;

    while (again && turns++ < 16) {
        again = 0;
        for (j = 0; j < nf; j++) {
            const RmTri *u = f[j].t;
            double lo = 1e30, hi = -1e30;
            int n = 0, in_plane = 1;

            if (fabs(f[j].n[1]) >= 0.3 || f[j].n[0] * ox + f[j].n[2] * oz < 0.7) {
                continue;
            }
            for (k = 0; k < 3 && in_plane; k++) {
                in_plane = fabs(((double)u->p[k][0] - x) * ox + ((double)u->p[k][2] - z) * oz) < 0.1;
            }
            if (!in_plane) {
                continue;
            }
            /* (the upright line through the point: where it crosses the face's edges) */
            for (k = 0; k < 3; k++) {
                const float *p = u->p[k], *q = u->p[(k + 1) % 3];
                const double a0 = ((double)p[0] - x) * dx + ((double)p[2] - z) * dz, a1 = ((double)q[0] - x) * dx + ((double)q[2] - z) * dz;

                if ((a0 <= 0.0 && a1 >= 0.0) || (a1 <= 0.0 && a0 >= 0.0)) {
                    const double t = fabs(a1 - a0) < 1e-9 ? 0.0 : -a0 / (a1 - a0);
                    const double yy = p[1] + t * ((double)q[1] - p[1]);

                    lo = yy < lo ? yy : lo;
                    hi = yy > hi ? yy : hi;
                    n++;
                    if (fabs(a1 - a0) < 1e-9) {
                        lo = fmin(lo, fmin(p[1], q[1]));
                        hi = fmax(hi, fmax(p[1], q[1]));
                    }
                }
            }
            if (n >= 2 && hi >= y - 0.3 && lo < y - 0.05) {
                y = lo;
                again = 1;
                if (mark != NULL) {
                    mark[j] = 1;        /* (a face that hangs under the edge) */
                }
            }
        }
    }
    return y;
}

void rm_skirt(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double step = rm_optf(opt, "step", 1.0), min_len = rm_optf(opt, "min", 2.0), lay = rm_optf(opt, "lay", 6.0);
    const double min_h = rm_optf(opt, "minh", 8.0);
    SkFace *f = NULL;
    unsigned char *under = NULL;
    unsigned side_batch = 0;
    double fade_lo = 0.0, fade_hi = 0.0, col_lo[3] = { 0, 0, 0 }, col_hi[3] = { 0, 0, 0 };
    unsigned nf = 0, i, j, bi;
    const SkFace *src = NULL;
    double low = 1e30, foot, best_area = 0.0, vy0 = 0.0, vdy = 0.0;
    int k, e;

    (void)tex;
    (void)ntex;
    for (bi = 0; bi < nb; bi++) {
        nf += b[bi].bb ? 0u : b[bi].n;
    }
    f = (SkFace *)malloc((nf > 0 ? nf : 1) * sizeof *f);
    if (f == NULL) {
        out->fail = 1;
        return;
    }
    nf = 0;
    for (bi = 0; bi < nb; bi++) {
        for (j = 0; !b[bi].bb && j < b[bi].n; j++) {
            const RmTri *t = &b[bi].tri[j];
            double e1[3], e2[3];

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, f[nf].n);
            if (rm_norm(f[nf].n) < 1e-9) {
                continue;
            }
            f[nf].t = t;
            f[nf].batch = bi;
            nf++;
        }
    }
    /* the block sides: the upright faces that hang under an edge of a walk surface already */
    under = (unsigned char *)calloc(nf > 0 ? nf : 1, 1);
    if (under == NULL) {
        free(f);
        out->fail = 1;
        return;
    }
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;

        if (f[i].n[1] < 0.3) {
            continue;
        }
        for (e = 0; e < 3; e++) {
            const float *pa = t->p[e], *pb = t->p[(e + 1) % 3], *pc = t->p[(e + 2) % 3];
            double dx = (double)pb[0] - pa[0], dz = (double)pb[2] - pa[2], len = sqrt(dx * dx + dz * dz), ox, oz;

            if (len < min_len) {
                continue;
            }
            ox = dz / len;
            oz = -dx / len;
            if (((double)pc[0] - pa[0]) * ox + ((double)pc[2] - pa[2]) * oz > 0.0) {
                ox = -ox;
                oz = -oz;
            }
            (void)sk_cover(f, nf, 0.5 * ((double)pa[0] + pb[0]), 0.5 * ((double)pa[2] + pb[2]), dx / len, dz / len, ox, oz,
                           0.5 * ((double)pa[1] + pb[1]), under);
        }
    }
    /* the pit's foot: the lowest corner of the block sides; and the one that gives the picture: the largest of those
     * that reach it */
    {
        /* (of the draw that has the most of them: a door's landing can lie against the tower's own wall, which then
         * hangs under it too, larger and lower than any block) */
        unsigned best_n = 0, bj;

        for (bj = 0; bj < nb; bj++) {
            unsigned cnt = 0;

            for (i = 0; i < nf; i++) {
                cnt += under[i] && f[i].batch == bj && fabs(f[i].n[1]) < 0.1;
            }
            if (cnt > best_n) {
                best_n = cnt;
                side_batch = bj;
            }
        }
        for (i = 0; i < nf; i++) {
            if (under[i] && f[i].batch != side_batch) {
                under[i] = 0;
            }
        }
    }
    for (i = 0; i < nf; i++) {
        if (under[i]) {
            for (k = 0; k < 3; k++) {
                low = f[i].t->p[k][1] < low ? f[i].t->p[k][1] : low;
            }
        }
    }
    foot = rm_optf(opt, "foot", low);
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double lo = 1e30, hi = -1e30, e1[3], e2[3], c[3], area;

        if (!under[i] || fabs(f[i].n[1]) >= 0.1 || b[f[i].batch].tex < 0) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            lo = t->p[k][1] < lo ? t->p[k][1] : lo;
            hi = t->p[k][1] > hi ? t->p[k][1] : hi;
            e1[k] = (double)t->p[1][k] - t->p[0][k];
            e2[k] = (double)t->p[2][k] - t->p[0][k];
        }
        rm_cross(e1, e2, c);
        area = rm_norm(c);
        if (lo < low + 1.0 && hi - lo > 16.0 && area > best_area) {
            /* (how it lays its picture by the height: v at a height, and v a unit down) */
            int a0 = 0, a1 = 1;

            if (fabs((double)t->p[1][1] - t->p[0][1]) < 1.0) {
                a1 = 2;
            }
            if (fabs((double)t->p[a1][1] - t->p[a0][1]) < 1.0) {
                a0 = 1;
                a1 = 2;
            }
            if (fabs((double)t->p[a1][1] - t->p[a0][1]) >= 1.0) {
                best_area = area;
                src = &f[i];
                vdy = ((double)t->uv[a1][1] - t->uv[a0][1]) / ((double)t->p[a1][1] - t->p[a0][1]);
                vy0 = t->uv[a0][1] - vdy * t->p[a0][1];
            }
        }
    }
    if (src == NULL || !(low < 1e29)) {
        free(under);            /* (a room without block sides: walkways that hang, left as they are) */
        free(f);
        return;
    }
    {
        /* The block sides' colours at their top and at their foot.  A side is two triangles, one with two corners at
         * the top and one with two at the foot; a triangle's colour here is the mean of its corners', so with the
         * means m2 (two at the top) and m1 (one at the top): top = 2 m2 - m1, foot = 2 m1 - m2. */
        const SkFace *other = NULL;
        double s_lo = 1e30, s_hi = -1e30;
        int s_top = 0, o_top = 0;

        for (k = 0; k < 3; k++) {
            s_lo = fmin(s_lo, src->t->p[k][1]);
            s_hi = fmax(s_hi, src->t->p[k][1]);
        }
        for (k = 0; k < 3; k++) {
            s_top += src->t->p[k][1] > 0.5 * (s_lo + s_hi);
        }
        for (i = 0; i < nf && other == NULL; i++) {
            double lo = 1e30, hi = -1e30;
            int ntop = 0;

            if (!under[i] || &f[i] == src) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                lo = fmin(lo, f[i].t->p[k][1]);
                hi = fmax(hi, f[i].t->p[k][1]);
            }
            for (k = 0; k < 3; k++) {
                ntop += f[i].t->p[k][1] > 0.5 * (lo + hi);
            }
            if (fabs(lo - s_lo) < 0.5 && fabs(hi - s_hi) < 0.5 && ntop != s_top && (ntop == 1 || ntop == 2)) {
                other = &f[i];
                o_top = ntop;
            }
        }
        fade_lo = s_lo;
        fade_hi = s_hi;
        for (k = 0; k < 3; k++) {
            const double m2 = other == NULL ? src->t->rgb[k] : s_top == 2 ? src->t->rgb[k] : other->t->rgb[k];
            const double m1 = other == NULL ? src->t->rgb[k] : s_top == 2 ? other->t->rgb[k] : src->t->rgb[k];

            (void)o_top;
            col_hi[k] = fmin(255.0, fmax(0.0, 2.0 * m2 - m1));
            col_lo[k] = fmin(255.0, fmax(0.0, 2.0 * m1 - m2));
        }
    }
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;

        if (f[i].n[1] < 0.3) {
            continue;                   /* (no walk surface) */
        }
        {
            /* (laid on another walk surface: a picture on the floor) */
            const double cx = ((double)t->p[0][0] + t->p[1][0] + t->p[2][0]) / 3.0, cz = ((double)t->p[0][2] + t->p[1][2] + t->p[2][2]) / 3.0;
            const double cy = ((double)t->p[0][1] + t->p[1][1] + t->p[2][1]) / 3.0;
            double yy = 0.0;
            int laid = 0;

            for (j = 0; j < nf && !laid; j++) {
                laid = j != i && f[j].n[1] >= 0.3 && sk_over(f[j].t, cx, cz, &yy) && yy < cy - 0.01 && yy >= cy - lay;
            }
            if (laid) {
                continue;
            }
        }
        for (e = 0; e < 3; e++) {
            const float *pa = t->p[e], *pb = t->p[(e + 1) % 3], *pc = t->p[(e + 2) % 3];
            double dx = (double)pb[0] - pa[0], dz = (double)pb[2] - pa[2], len = sqrt(dx * dx + dz * dz), ox, oz, mx, mz, yy;
            int shared = 0, on = 0;
            double ya, yb;

            if (len < min_len) {
                continue;
            }
            /* (the way out: across the edge, away from the face's third corner) */
            ox = dz / len;
            oz = -dx / len;
            if (((double)pc[0] - pa[0]) * ox + ((double)pc[2] - pa[2]) * oz > 0.0) {
                ox = -ox;
                oz = -oz;
            }
            mx = 0.5 * ((double)pa[0] + pb[0]) + 0.5 * ox;
            mz = 0.5 * ((double)pa[2] + pb[2]) + 0.5 * oz;
            for (j = 0; j < nf && !shared && !on; j++) {
                const RmTri *u = f[j].t;
                int ca = 0, cb = 0;

                if (j == i || f[j].n[1] < 0.3) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    ca |= sk_same(u->p[k], pa);
                    cb |= sk_same(u->p[k], pb);
                }
                /* another walk surface: on the same edge, or going on just outside it */
                shared = ca && cb;
                if (!shared && sk_over(u, mx, mz, &yy)) {
                    const double ey = 0.5 * ((double)pa[1] + pb[1]);

                    on = yy <= ey + 0.05 && yy >= ey - step;
                }
            }
            if (shared || on) {
                continue;
            }
            /* where the room's own faces under the edge stop, a little inside each of its ends */
            ya = sk_cover(f, nf, pa[0] + 0.25 * dx / len, pa[2] + 0.25 * dz / len, dx / len, dz / len, ox, oz, pa[1], NULL);
            yb = sk_cover(f, nf, pb[0] - 0.25 * dx / len, pb[2] - 0.25 * dz / len, dx / len, dz / len, ox, oz, pb[1], NULL);
            if (ya <= foot + min_h && yb <= foot + min_h) {
                continue;       /* (the room's own faces reach the foot, or nearly: a tower wall's skirting) */
            }
            ya = ya < foot ? foot : ya;
            yb = yb < foot ? foot : yb;
            {
                /* the wall, in SK_BANDS bands from where the faces stop down to the foot, each in the block sides'
                 * colour at its height; looking out */
                int bi2;

                for (bi2 = 0; bi2 < SK_BANDS; bi2++) {
                    const double t0 = (double)bi2 / SK_BANDS, t1 = (double)(bi2 + 1) / SK_BANDS;
                    const double ya0 = ya + (foot - ya) * t0, ya1 = ya + (foot - ya) * t1;
                    const double yb0 = yb + (foot - yb) * t0, yb1 = yb + (foot - yb) * t1;
                    const double a[3] = { pa[0], ya0, pa[2] }, bq[3] = { pb[0], yb0, pb[2] };
                    const double af[3] = { pa[0], ya1, pa[2] }, bf[3] = { pb[0], yb1, pb[2] };
                    const double ua[2] = { 0.0, vy0 + vdy * ya0 }, ub[2] = { len / 32.0, vy0 + vdy * yb0 };
                    const double uaf[2] = { 0.0, vy0 + vdy * ya1 }, ubf[2] = { len / 32.0, vy0 + vdy * yb1 };
                    const double ym = 0.25 * (ya0 + ya1 + yb0 + yb1);
                    const double w = fade_hi > fade_lo ? fmin(1.0, fmax(0.0, (ym - fade_lo) / (fade_hi - fade_lo))) : 1.0;
                    unsigned char rgb[3];
                    double e1[3], e2[3], nn[3];

                    if (ya0 - ya1 < 1e-6 && yb0 - yb1 < 1e-6) {
                        continue;
                    }
                    for (k = 0; k < 3; k++) {
                        rgb[k] = (unsigned char)(col_lo[k] + (col_hi[k] - col_lo[k]) * w);
                        e1[k] = bq[k] - a[k];
                        e2[k] = bf[k] - a[k];
                    }
                    rm_cross(e1, e2, nn);
                    if (nn[0] * ox + nn[2] * oz >= 0.0) {
                        rm_tri(out, src->t->id, -1, 1, rgb, a, bq, bf, ua, ub, ubf, 0, 0);
                        rm_tri(out, src->t->id, -1, 1, rgb, a, bf, af, ua, ubf, uaf, 0, 0);
                    } else {
                        rm_tri(out, src->t->id, -1, 1, rgb, a, bf, bq, ua, ubf, ub, 0, 0);
                        rm_tri(out, src->t->id, -1, 1, rgb, a, af, bf, ua, uaf, ubf, 0, 0);
                    }
                }
            }
        }
    }
    free(under);
    free(f);
}
