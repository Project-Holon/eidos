/*
 * engine/world/remade/hall.c -- a box hall: straight side walls under a roof
 * that stands out over both ends (the glass-roofed warehouse).  The map has
 * its front, its sides and its roof; its back wall, the roof's back end and
 * the back of what leans on a side are not in the model.
 *
 * What is made, all from the model's own faces and texture pixels:
 *   1. The back wall, where the side walls end, from one side wall to the
 *      other, as high as they are.  It wears the side wall's own texture:
 *      each end takes the side's corner columns (the corner goes on round),
 *      the rest the side's plain column, row by row (the plinth, the bands):
 *      the same wall as the sides, without their doors.
 *   2. The roof's back end: the front's end faces over the wall's top (the
 *      roof's edge band and the gable wall in it), turned to the back, as far
 *      out from the back wall as they are from the front wall, and the
 *      underside between them.
 *   3. `tanks=B:N:GAP`: the lean-to on a side drawn by draw command B (a flat
 *      face with a repeated picture of a tank, a slope of pipe ends over it) is
 *      N tanks: boxes from that face to the wall, GAP apart, each the picture
 *      once; and over each a square pipe that rises from the tank and bends
 *      into the wall, in the picture's own pipe pixels.
 *
 * Settings: corner=W (map units of the side's corner columns, default 8),
 * tanks=B:N:GAP, pipe=W (the pipes' width, default 1.6).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

/* how a flat face's texture runs: u and v by two of the axes (a, b: 0 x, 1 y, 2 z) */
typedef struct {
    double u0, v0, ua, ub, va, vb, a0, b0;
    int a, b;
} Map;

static int map_of(const RmTri *t, int a, int b, Map *m)
{
    const double da1 = (double)t->p[1][a] - t->p[0][a], db1 = (double)t->p[1][b] - t->p[0][b];
    const double da2 = (double)t->p[2][a] - t->p[0][a], db2 = (double)t->p[2][b] - t->p[0][b];
    const double det = da1 * db2 - da2 * db1;
    const double du1 = (double)t->uv[1][0] - t->uv[0][0], du2 = (double)t->uv[2][0] - t->uv[0][0];
    const double dv1 = (double)t->uv[1][1] - t->uv[0][1], dv2 = (double)t->uv[2][1] - t->uv[0][1];

    if (fabs(det) < 1e-9) {
        return -1;
    }
    m->a = a;
    m->b = b;
    m->a0 = t->p[0][a];
    m->b0 = t->p[0][b];
    m->u0 = t->uv[0][0];
    m->v0 = t->uv[0][1];
    m->ua = (du1 * db2 - du2 * db1) / det;
    m->ub = (du2 * da1 - du1 * da2) / det;
    m->va = (dv1 * db2 - dv2 * db1) / det;
    m->vb = (dv2 * da1 - dv1 * da2) / det;
    return 0;
}

static void map_at(const Map *m, double pa, double pb, double uv[2])
{
    uv[0] = m->u0 + m->ua * (pa - m->a0) + m->ub * (pb - m->b0);
    uv[1] = m->v0 + m->va * (pa - m->a0) + m->vb * (pb - m->b0);
}

static double tri_n(const RmTri *t, int axis)
{
    double e1[3], e2[3], n[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = (double)t->p[1][k] - t->p[0][k];
        e2[k] = (double)t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) <= 0.0) {
        return 0.0;
    }
    return n[axis];
}

/* A four-cornered face, drawn from both sides, lit by the game; its corners turned so it looks along `out`. */
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

/* A box from lo to hi, its faces in one flat place of the texture (a pipe: one colour across, its shading the
 * game's light); `skip` the faces left out: bit 0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z (where it meets another) */
static void box(RmOut *o, unsigned src, const unsigned char rgb[3], const double lo[3], const double hi[3],
                const double uv0[2], const double uv1[2], unsigned skip)
{
    int f;

    for (f = 0; f < 6; f++) {
        const int ax = f / 2, s = f % 2, a = (ax + 1) % 3, b = (ax + 2) % 3;
        double p[4][3], t[4][2], n[3] = { 0, 0, 0 };
        int j;

        if (skip & (1u << f)) {
            continue;
        }
        n[ax] = s ? 1.0 : -1.0;
        for (j = 0; j < 4; j++) {
            p[j][ax] = s ? hi[ax] : lo[ax];
            p[j][a] = j == 0 || j == 3 ? lo[a] : hi[a];
            p[j][b] = j < 2 ? lo[b] : hi[b];
            t[j][0] = j == 0 || j == 3 ? uv0[0] : uv1[0];
            t[j][1] = j < 2 ? uv0[1] : uv1[1];
        }
        quad(o, src, rgb, p, t, n);
    }
}

void rm_hall(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double cw = rm_optf(opt, "corner", 8.0);
    const RmTri *se = NULL;              /* the east side wall's face at the back corner */
    const RmBatch *sb = NULL;
    double xw = 1e30, xe = -1e30, zb = 1e30, y0 = 1e30, y1 = -1e30, zr = 1e30, zf = -1e30;
    double rx0 = 1e30, rx1 = -1e30;
    unsigned bi, i;
    int tb = -1;
    char eb[64];
    Map sm;

    (void)tex;
    (void)ntex;
    if (rm_opt(opt, "tanks", eb, sizeof eb) != NULL) {
        tb = atoi(eb);
    }
    /* the side walls: tall upright faces that look east or west (not the lean-to's) */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && (int)bi != tb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            const double nx = tri_n(t, 0);
            double tlo = 1e30, thi = -1e30;
            int k;

            if (fabs(nx) < 0.9) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                tlo = t->p[k][1] < tlo ? t->p[k][1] : tlo;
                thi = t->p[k][1] > thi ? t->p[k][1] : thi;
            }
            if (thi - tlo < 20.0) {
                continue;               /* (the roof's eave boards) */
            }
            for (k = 0; k < 3; k++) {
                xw = nx < 0.0 && t->p[k][0] < xw ? t->p[k][0] : xw;
                xe = nx > 0.0 && t->p[k][0] > xe ? t->p[k][0] : xe;
                zb = t->p[k][2] < zb ? t->p[k][2] : zb;
                y0 = tlo < y0 ? tlo : y0;
                y1 = thi > y1 ? thi : y1;
            }
        }
    }
    /* the east wall's face that reaches the back corner: its texture goes on round the corner */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && (int)bi != tb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            int k, at = 0, east = 1;

            for (k = 0; k < 3; k++) {
                east &= fabs(t->p[k][0] - xe) < 0.05;
                at |= fabs(t->p[k][2] - zb) < 0.05;
            }
            if (east && at && se == NULL && tri_n(t, 0) > 0.9) {
                se = t;
                sb = &b[bi];
            }
        }
    }
    /* the roof: its slopes' ends and sides */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            int k;

            if (tri_n(t, 1) > 0.25) {
                for (k = 0; k < 3; k++) {
                    if (t->p[k][1] > y1 - 0.05) {
                        zr = t->p[k][2] < zr ? t->p[k][2] : zr;
                        zf = t->p[k][2] > zf ? t->p[k][2] : zf;
                        rx0 = t->p[k][0] < rx0 ? t->p[k][0] : rx0;
                        rx1 = t->p[k][0] > rx1 ? t->p[k][0] : rx1;
                    }
                }
            }
        }
    }
    if (se == NULL || xe - xw < 4.0 * cw || y1 - y0 < 1.0 || map_of(se, 2, 1, &sm) != 0) {
        return;
    }
    /* 1. the back wall: corner columns at each end, the plain column between */
    {
        const double outn[3] = { 0, 0, -1 };
        const double zp = zb + cw - 0.3;        /* (the side's plain column, just inside its corner) */
        double p[4][3], t[4][2];
        int e;

        for (e = 0; e < 2; e++) {
            const double xo = e == 0 ? xe : xw, xi = e == 0 ? xe - cw : xw + cw;

            p[0][0] = xo; p[0][1] = y0;
            p[1][0] = xi; p[1][1] = y0;
            p[2][0] = xi; p[2][1] = y1;
            p[3][0] = xo; p[3][1] = y1;
            for (i = 0; i < 4u; i++) {
                p[i][2] = zb;
                map_at(&sm, zb + fabs(p[i][0] - xo), p[i][1], t[i]);
            }
            quad(out, se->id, sb->rgb, p, t, outn);
        }
        p[0][0] = xe - cw; p[0][1] = y0;
        p[1][0] = xw + cw; p[1][1] = y0;
        p[2][0] = xw + cw; p[2][1] = y1;
        p[3][0] = xe - cw; p[3][1] = y1;
        for (i = 0; i < 4u; i++) {
            p[i][2] = zb;
            map_at(&sm, zp, p[i][1], t[i]);
        }
        quad(out, se->id, sb->rgb, p, t, outn);
    }
    /* 2. the roof's back end: the front's end faces over the wall's top, turned to the back; a later draw a
     * hair further out, as the front draws it over the earlier in the same plane */
    if (zf - zr > 1.0) {
        double zfw = -1e30;             /* the front wall */

        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && (int)bi != tb && i < b[bi].n; i++) {
                const RmTri *t = &b[bi].tri[i];
                double p[3][3], uv[3][2], ylo = 1e30;
                int k, j, end = 1;

                if (tri_n(t, 2) < 0.9) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    end &= fabs(t->p[k][2] - zf) < 0.3;
                    ylo = t->p[k][1] < ylo ? t->p[k][1] : ylo;
                    if (t->p[k][1] < y1 - 0.05 && t->p[k][2] > zfw) {
                        zfw = t->p[k][2];
                    }
                }
                if (!end || ylo < y1 - 0.05) {
                    continue;
                }
                for (j = 0; j < 3; j++) {
                    const int s = j == 0 ? 0 : 3 - j;

                    p[j][0] = t->p[s][0];
                    p[j][1] = t->p[s][1];
                    p[j][2] = zr + (zf - t->p[s][2]) - 0.02 * bi;
                    uv[j][0] = t->uv[s][0];
                    uv[j][1] = t->uv[s][1];
                }
                rm_tri(out, t->id, -1, 1, t->rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
            }
        }
        /* the underside of the roof's end where it stands out over the back wall, in the wall's top row */
        if (zb - zr > 0.2 && rx1 > rx0) {
            const double outn[3] = { 0, -1, 0 };
            double p[4][3], t[4][2];

            p[0][0] = rx0; p[0][2] = zr;
            p[1][0] = rx1; p[1][2] = zr;
            p[2][0] = rx1; p[2][2] = zb;
            p[3][0] = rx0; p[3][2] = zb;
            for (i = 0; i < 4u; i++) {
                p[i][1] = y1;
                map_at(&sm, zb + cw - 0.3, y1 - 0.3, t[i]);
            }
            quad(out, se->id, sb->rgb, p, t, outn);
        }
    }
    /* 3. the tanks */
    if (tb >= 0 && (unsigned)tb < nb) {
        const RmBatch *k0 = &b[tb];
        const RmTri *of = NULL;         /* the lean-to's outer face */
        double best = 0.0, xo = 0.0, tz0 = 1e30, tz1 = -1e30, ty0 = 1e30, ty1 = -1e30, ytop = -1e30, side = 0.0;
        double gap = 0.8, pw = rm_optf(opt, "pipe", 1.6);
        unsigned n = 6;
        const char *c2 = strchr(eb, ':');
        Map om;

        if (c2 != NULL) {
            sscanf(c2 + 1, "%u:%lf", &n, &gap);
        }
        for (i = 0; i < k0->n; i++) {
            const RmTri *t = &k0->tri[i];
            const double nx = tri_n(t, 0);
            double e1[3], e2[3], nn[3], len;
            int k;

            for (k = 0; k < 3; k++) {
                ytop = t->p[k][1] > ytop ? t->p[k][1] : ytop;
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, nn);
            len = rm_norm(nn);
            if (fabs(nx) > 0.9 && len > best) {
                best = len;
                of = t;
                side = nx;
            }
        }
        if (of == NULL || n < 1u || n > 16u || map_of(of, 2, 1, &om) != 0) {
            return;
        }
        xo = of->p[0][0];
        for (i = 0; i < 3u; i++) {
            tz0 = of->p[i][2] < tz0 ? of->p[i][2] : tz0;
            tz1 = of->p[i][2] > tz1 ? of->p[i][2] : tz1;
        }
        for (i = 0; i < k0->n; i++) {
            const RmTri *t = &k0->tri[i];
            int k;

            if (fabs(tri_n(t, 0)) > 0.9) {
                for (k = 0; k < 3; k++) {
                    ty0 = t->p[k][1] < ty0 ? t->p[k][1] : ty0;
                    ty1 = t->p[k][1] > ty1 ? t->p[k][1] : ty1;
                }
            }
            /* (its front end stays as the map has it: the field's camera sees it; the tank's end stands behind) */
            if (tri_n(t, 2) < 0.9) {
                rm_drop(out, t->id);
            }
        }
        {
            /* the wall the tanks stand against, and the picture's tank (the bottom part, as the face shows it)
             * and pipe (the top part's middle column) */
            const double xin = side < 0.0 ? xw : xe, dir = side < 0.0 ? 1.0 : -1.0;
            const double L = (tz1 - tz0) / n;
            double vt[2], vb[2];
            unsigned q;

            map_at(&om, tz0, ty1, vt);
            map_at(&om, tz0, ty0, vb);
            for (q = 0; q < n; q++) {
                const double za = tz0 + q * L + (q > 0 ? gap / 2 : 0.0), zz = tz0 + (q + 1) * L - (q + 1 < n ? gap / 2 : 0.0);
                const double xa = xo < xin ? xo : xin, xb = xo < xin ? xin : xo, zm = (za + zz) / 2.0;
                const double fl = floor(vt[0]);
                double p[4][3], t[4][2];
                const double n_out[3] = { side < 0.0 ? -1.0 : 1.0, 0, 0 }, n_up[3] = { 0, 1, 0 };
                int e;

                /* its face: the picture once, its middle bar in the tank's middle */
                p[0][0] = xo; p[0][1] = ty0; p[0][2] = za;
                p[1][0] = xo; p[1][1] = ty0; p[1][2] = zz;
                p[2][0] = xo; p[2][1] = ty1; p[2][2] = zz;
                p[3][0] = xo; p[3][1] = ty1; p[3][2] = za;
                for (i = 0; i < 4u; i++) {
                    t[i][0] = fl + (p[i][2] - za) / (zz - za);
                    t[i][1] = p[i][1] > ty0 + 0.01 ? vt[1] : vb[1];
                }
                quad(out, of->id, k0->rgb, p, t, n_out);
                /* its ends: the picture's middle bar, as the map's front end shows it */
                for (e = 0; e < 2; e++) {
                    const double ze = e == 0 ? za : q + 1 == n ? zz - 0.05 : zz, n_end[3] = { 0, 0, e == 0 ? -1.0 : 1.0 };

                    p[0][0] = xa; p[0][1] = ty0; p[0][2] = ze;
                    p[1][0] = xb; p[1][1] = ty0; p[1][2] = ze;
                    p[2][0] = xb; p[2][1] = ty1; p[2][2] = ze;
                    p[3][0] = xa; p[3][1] = ty1; p[3][2] = ze;
                    for (i = 0; i < 4u; i++) {
                        t[i][0] = fl + 0.42 + 0.16 * (p[i][0] - xa) / (xb - xa);
                        t[i][1] = p[i][1] > ty0 + 0.01 ? vt[1] : vb[1];
                    }
                    quad(out, of->id, k0->rgb, p, t, n_end);
                }
                /* its top: the picture's top row */
                p[0][0] = xa; p[0][1] = ty1; p[0][2] = za;
                p[1][0] = xb; p[1][1] = ty1; p[1][2] = za;
                p[2][0] = xb; p[2][1] = ty1; p[2][2] = zz;
                p[3][0] = xa; p[3][1] = ty1; p[3][2] = zz;
                for (i = 0; i < 4u; i++) {
                    t[i][0] = fl + (p[i][2] - za) / (zz - za);
                    t[i][1] = vt[1] + 0.02;
                }
                quad(out, of->id, k0->rgb, p, t, n_up);
                /* its pipe: up from the tank's outer half, then along into the wall */
                {
                    const double px = xo + dir * (pw / 2 + 0.8), yh = ytop - pw;
                    const double uv0[2] = { fl + 0.44, floor(vt[1]) + 0.05 }, uv1[2] = { fl + 0.56, floor(vt[1]) + 0.2 };
                    double lo[3], hi[3];

                    lo[0] = px - pw / 2; hi[0] = px + pw / 2;
                    lo[1] = ty1; hi[1] = yh + pw / 2;
                    lo[2] = zm - pw / 2; hi[2] = zm + pw / 2;
                    box(out, of->id, k0->rgb, lo, hi, uv0, uv1, 1u << 2);
                    if (dir > 0.0) {
                        lo[0] = px + pw / 2; hi[0] = xin;
                    } else {
                        lo[0] = xin; hi[0] = px - pw / 2;
                    }
                    lo[1] = yh - pw / 2; hi[1] = yh + pw / 2;
                    box(out, of->id, k0->rgb, lo, hi, uv0, uv1, 3u);
                }
            }
        }
    }
    /* (the model's own walls end a hair short of a corner here and there: their insides get a back) */
    rm_seal(b, nb, out, 0.7);
}
