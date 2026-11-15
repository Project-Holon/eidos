/*
 * engine/world/remade/candle.c -- a candle on its dish drawn as a picture
 * card (the centrepieces on the Striaton Gym leader's tables) made a candle.
 *
 * The card is a 16-pixel picture seen from above the table: the dish, a
 * round gold rim round a white and yellow middle (rows 5..15: the dish's
 * round face, squashed to an ellipse by the field's camera), and the candle
 * standing out of it (columns 6..9, rows 1..4).  From the side the card is a
 * yellow streak over the table.  The candle: an eight-sided dish, low, its
 * top the picture's round face made round again (each corner takes the
 * picture's place over it), its rim the gold; out of its middle an
 * eight-sided candle in the picture's yellow, a white top, and a small
 * flame.  Sizes in picture pixels (the card's width over the picture's).
 *
 * Settings: at=X:Z (its middle; else under the card's lower half), y (the
 * height it stands on), rd (8), dh (0.8), rc (2), hc (5), fl (2.2: the
 * flame), row (10: the dish face's middle row), sq (0.69: how much the
 * picture squashes the dish front to back), col (8).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define PI 3.14159265358979323846

typedef struct {
    double c[3];                /* the dish's foot middle                       */
    double s;                   /* a picture pixel, in map units                */
    double fu, fv;
    int tw, th;
    unsigned src;
    unsigned char rgb[3];
} Cd;

static void cd_uv(const Cd *k, double col, double row, double uv[2])
{
    uv[0] = k->fu + col / k->tw;
    uv[1] = k->fv + row / k->th;
}

/* A triangle a, b, c (looking the way (b - a) x (c - a) points), its corners' pictures, a little darker by the way it
 * looks (a light: never darker than 0.86). */
static void cd_tri(const Cd *k, RmOut *out, const double a[3], const double b[3], const double c[3], const double ua[2],
                   const double ub[2], const double uc[2])
{
    double e1[3], e2[3], n[3], f;
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
    f = n[1] > 0.7 ? 1.0 : n[1] < -0.7 ? 0.55 : n[2] > 0.5 ? 1.0 : 0.86;
    for (i = 0; i < 3; i++) {
        rgb[i] = (unsigned char)(k->rgb[i] * f);
    }
    rm_tri(out, k->src, -1, 1, rgb, a, b, c, ua, ub, uc, 0, 0);
}

static void cd_at(const Cd *k, double r, double t, double up, double p[3])
{
    p[0] = k->c[0] + r * cos(t) * k->s;
    p[1] = k->c[1] + up * k->s;
    p[2] = k->c[2] + r * sin(t) * k->s;
}

/* An eight-sided ring band from (r0, y0) to (r1, y1), looking out, in one pixel. */
static void cd_band(const Cd *k, RmOut *out, double r0, double y0, double r1, double y1, const double uv[2])
{
    int i;

    for (i = 0; i < 8; i++) {
        const double t0 = (22.5 + 45.0 * i) * PI / 180.0, t1 = (22.5 + 45.0 * (i + 1)) * PI / 180.0;
        double a[3], b[3], c[3], d[3];

        cd_at(k, r0, t0, y0, a);
        cd_at(k, r0, t1, y0, b);
        cd_at(k, r1, t1, y1, c);
        cd_at(k, r1, t0, y1, d);
        cd_tri(k, out, a, d, c, uv, uv, uv);
        cd_tri(k, out, a, c, b, uv, uv, uv);
    }
}

static int build(const RmBatch *bt, const RmTex *tx, const char *opt, RmOut *out)
{
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, umin = 1e30, umax = -1e30, vmin = 1e30;
    double pw;
    unsigned i, j;
    Cd k;
    char ab[64];

    memset(&k, 0, sizeof k);
    for (i = 0; i < bt->n; i++) {
        for (j = 0; j < 3u; j++) {
            int a;

            for (a = 0; a < 3; a++) {
                lo[a] = fmin(lo[a], bt->tri[i].p[j][a]);
                hi[a] = fmax(hi[a], bt->tri[i].p[j][a]);
            }
            umin = fmin(umin, bt->tri[i].uv[j][0]);
            umax = fmax(umax, bt->tri[i].uv[j][0]);
            vmin = fmin(vmin, bt->tri[i].uv[j][1]);
        }
    }
    pw = (umax - umin) * tx->w;
    if (pw < 2.0 || hi[0] - lo[0] < 1e-3) {
        return 0;
    }
    k.tw = tx->w;
    k.th = tx->h;
    k.fu = floor(umin + 1e-6);
    k.fv = floor(vmin + 1e-6);
    k.s = (hi[0] - lo[0]) / pw;
    k.src = bt->tri[0].id;
    memcpy(k.rgb, bt->tri[0].rgb, 3u);
    k.c[0] = (lo[0] + hi[0]) / 2.0;
    k.c[1] = rm_optf(opt, "y", lo[1]);
    k.c[2] = hi[2] - 0.375 * (hi[2] - lo[2]);
    if (rm_opt(opt, "at", ab, sizeof ab) != NULL) {
        sscanf(ab, "%lf:%lf", &k.c[0], &k.c[2]);
    }
    {
        const double rd = rm_optf(opt, "rd", 8.0), dh = rm_optf(opt, "dh", 0.8), rc = rm_optf(opt, "rc", 2.0);
        const double hc = rm_optf(opt, "hc", 5.0), fl = rm_optf(opt, "fl", 2.2), row = rm_optf(opt, "row", 10.0);
        const double sq = rm_optf(opt, "sq", 0.69), col = rm_optf(opt, "col", 8.0);
        double uv_rim[2], uv_wax[2], uv_top[2], uv_fl[2], m[3], muv[2];
        int q;

        cd_uv(&k, 1.5, 7.5, uv_rim);            /* (the rim's gold) */
        cd_uv(&k, 7.5, 2.5, uv_wax);            /* (the candle's yellow) */
        cd_uv(&k, 7.5, 6.5, uv_top);            /* (white) */
        cd_uv(&k, 8.0, 8.5, uv_fl);
        /* the dish: its rim, and its round face, each corner the picture's place over it */
        cd_band(&k, out, rd, 0.0, rd, dh, uv_rim);
        cd_at(&k, 0.0, 0.0, dh, m);
        cd_uv(&k, col, row, muv);
        for (q = 0; q < 8; q++) {
            const double t0 = (22.5 + 45.0 * q) * PI / 180.0, t1 = (22.5 + 45.0 * (q + 1)) * PI / 180.0;
            double a[3], b[3], ua[2], ub[2];

            cd_at(&k, rd, t0, dh, a);
            cd_at(&k, rd, t1, dh, b);
            cd_uv(&k, col + rd * cos(t0), row + rd * sin(t0) * sq, ua);
            cd_uv(&k, col + rd * cos(t1), row + rd * sin(t1) * sq, ub);
            cd_tri(&k, out, m, b, a, muv, ub, ua);
        }
        /* the candle and its top */
        cd_band(&k, out, rc, dh, rc, dh + hc, uv_wax);
        cd_at(&k, 0.0, 0.0, dh + hc, m);
        for (q = 0; q < 8; q++) {
            const double t0 = (22.5 + 45.0 * q) * PI / 180.0, t1 = (22.5 + 45.0 * (q + 1)) * PI / 180.0;
            double a[3], b[3];

            cd_at(&k, rc, t0, dh + hc, a);
            cd_at(&k, rc, t1, dh + hc, b);
            cd_tri(&k, out, m, b, a, uv_top, uv_top, uv_top);
        }
        /* the flame: four sides to a point */
        {
            double tip[3], base[4][3];

            cd_at(&k, 0.0, 0.0, dh + hc + fl, tip);
            for (q = 0; q < 4; q++) {
                cd_at(&k, 0.6, (45.0 + 90.0 * q) * PI / 180.0, dh + hc, base[q]);
            }
            for (q = 0; q < 4; q++) {
                cd_tri(&k, out, base[q], tip, base[(q + 1) % 4], uv_fl, uv_fl, uv_fl);
            }
        }
    }
    return 1;
}

void rm_candle(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];

        if (bt->bb || bt->n < 2 || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL) {
            continue;
        }
        if (build(bt, &tex[bt->tex], opt, out)) {
            rm_drop_all(out, bt);
        }
    }
}
