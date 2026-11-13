/*
 * engine/world/remade/disc.c -- a round plate with rings, drawn as one level
 * flat picture: the warp pads of the Pokemon League and of Opelucid Gym, as
 * a turned disc.
 *
 * The picture's middle row from its middle pixel outward gives the rings: a
 * pixel a ring one pixel wide, a true circle (48 sides), level, at its
 * colour's height, in that pixel's colour all round; between rings of
 * different heights a wall in the higher ring's colour.  The disc rests on
 * the floor under the picture.
 *
 * Settings: rise=RRGGBB:H+RRGGBB:H.. (the height in map units of each
 * colour; a colour not named: the lowest named), floor (0.3).
 */

#include <math.h>
#include <stdlib.h>

#include "remade.h"

#define N 48
#define PI 3.14159265358979323846
#define MAXRING 64

typedef struct {
    RmOut *o;
    unsigned src;
    unsigned char rgb[3];
    double mid[3], ex[3], ez[3], y0, unit;
} Di;

static void at(const Di *d, double rr, int k, double h, double out[3])
{
    double a = 2.0 * PI * k / N, c = cos(a) * rr * d->unit, s = sin(a) * rr * d->unit;

    out[0] = d->mid[0] + d->ex[0] * c + d->ez[0] * s;
    out[1] = d->y0 + h + d->ex[1] * c + d->ez[1] * s;
    out[2] = d->mid[2] + d->ex[2] * c + d->ez[2] * s;
}

static void face(const Di *d, const double a[3], const double b[3], const double c[3], const double uv[2],
                 const double outward[3])
{
    double e1[3], e2[3], n[3];

    rm_add(b, a, -1.0, e1);
    rm_add(c, a, -1.0, e2);
    rm_cross(e1, e2, n);
    if (sqrt(rm_dot(n, n)) < 1e-7) {
        return;
    }
    if (rm_dot(n, outward) < 0.0) {
        rm_tri(d->o, d->src, -1, 1, d->rgb, a, c, b, uv, uv, uv, 2, 0);
    } else {
        rm_tri(d->o, d->src, -1, 1, d->rgb, a, b, c, uv, uv, uv, 2, 0);
    }
}

void rm_disc(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double up[3] = { 0.0, 1.0, 0.0 };
    char rb[256];
    const char *rise = rm_opt(opt, "rise", rb, sizeof rb);
    unsigned col[16], ncol = 0, bi;
    double hgt[16], low = 1e30;

    while (rise != NULL && *rise != '\0' && ncol < 16) {
        char *end;

        col[ncol] = (unsigned)strtoul(rise, &end, 16);
        if (*end != ':') {
            break;
        }
        hgt[ncol] = strtod(end + 1, &end);
        low = hgt[ncol] < low ? hgt[ncol] : low;
        ncol++;
        rise = *end == '+' ? end + 1 : NULL;
    }
    if (ncol == 0) {
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        const RmTex *pic;
        double m[3][3], ringh[MAXRING], ymin, ymax;
        int ringc[MAXRING], nring = 0, W, H, row, c, i, k;
        Di d;

        if (bt->n != 2 || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL || rm_plane(&bt->tri[0], m) != 0) {
            continue;
        }
        ymin = ymax = bt->tri[0].p[0][1];
        for (i = 0; i < 2; i++) {
            for (k = 0; k < 3; k++) {
                double y = bt->tri[i].p[k][1];

                ymin = y < ymin ? y : ymin;
                ymax = y > ymax ? y : ymax;
            }
        }
        if (ymax - ymin > 0.01) {
            continue;                   /* not a level picture */
        }
        pic = &tex[bt->tex];
        W = pic->w;
        H = pic->h;
        row = H / 2;
        for (c = W / 2; c < W && nring < MAXRING; c++) {
            const unsigned char *p = pic->rgba + ((size_t)row * (size_t)W + (size_t)c) * 4u;
            unsigned v = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2], q;

            if (p[3] < 128) {
                break;
            }
            ringh[nring] = low;
            for (q = 0; q < ncol; q++) {
                if (col[q] == v) {
                    ringh[nring] = hgt[q];
                }
            }
            ringc[nring++] = c;
        }
        if (nring == 0) {
            continue;
        }
        d.o = out;
        d.src = bt->tri[0].id;
        d.rgb[0] = bt->rgb[0]; d.rgb[1] = bt->rgb[1]; d.rgb[2] = bt->rgb[2];
        rm_at(m, 0.5, 0.5, d.mid);
        for (k = 0; k < 3; k++) {
            d.ex[k] = m[0][k];
            d.ez[k] = m[1][k];
        }
        d.unit = rm_norm(d.ex) / W;
        rm_norm(d.ez);
        d.y0 = (double)bt->tri[0].p[0][1] - rm_optf(opt, "floor", 0.3);
        for (i = 0; i < nring; i++) {
            double uv[2] = { (ringc[i] + 0.5) / W, (row + 0.5) / H };
            double h = ringh[i], hn = i + 1 < nring ? ringh[i + 1] : 0.0, hp = i > 0 ? ringh[i - 1] : h;

            for (k = 0; k < N; k++) {
                double a0[3], a1[3], b0[3], b1[3], lo0[3], lo1[3], rad[3], in[3];
                double an = 2.0 * PI * (k + 0.5) / N;
                int q;

                at(&d, i, k, h, a0);
                at(&d, i, k + 1, h, a1);
                at(&d, i + 1, k, h, b0);
                at(&d, i + 1, k + 1, h, b1);
                face(&d, a0, a1, b1, uv, up);
                face(&d, a0, b1, b0, uv, up);
                for (q = 0; q < 3; q++) {
                    rad[q] = d.ex[q] * cos(an) + d.ez[q] * sin(an);
                    in[q] = -rad[q];
                }
                if (hn < h) {           /* the wall down to the ring outside (to the floor at the edge) */
                    at(&d, i + 1, k + 1, hn, lo1);
                    at(&d, i + 1, k, hn, lo0);
                    face(&d, b0, b1, lo1, uv, rad);
                    face(&d, b0, lo1, lo0, uv, rad);
                }
                if (hp < h) {           /* the wall down to the ring inside */
                    at(&d, i, k + 1, hp, lo1);
                    at(&d, i, k, hp, lo0);
                    face(&d, a0, a1, lo1, uv, in);
                    face(&d, a0, lo1, lo0, uv, in);
                }
            }
        }
        rm_drop_all(out, bt);
    }
}
