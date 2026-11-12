/*
 * engine/world/remade/cushion.c -- a pouffe (the low seats of the living
 * rooms: building models 94 and 95 of the open-air pack flag 0).
 *
 * The map has it as one flat picture on the floor, 32 pixels square, drawn
 * as the field's steep camera sees the seat: inside a dark outline, rows
 * 4..17 are the seat's top (a button in its middle, a lighter rim north and
 * west, a darker one south and east), rows 18..27 its front side (three
 * tones, light at the top), rows 28..29 the outline under it.
 *
 * The cushion: 14 units square with round corners (radius 3.5), 6 high.
 * Its side bulges: a thin foot, then the side's three tones in bands all
 * round, then the edge rolls over (three rings, 1.5 units in) to the top.
 * The top is the picture's top, pixel for pixel, cut to the round outline;
 * the button keeps its square shape (the rows north and south of it are
 * drawn out).  The picture's dark outline is a line round a flat drawing:
 * on the body its place, the foot, is the side's own darkest tone a little
 * darker, and the top's corners take the pixel beside the outline.  The
 * cushion rests on the floor under the picture (the map holds the picture
 * at another height over the floor in every room).
 *
 * Settings: high (6), floor (0.3: how far the picture's sheet lies over the
 * floor it was drawn for), foot (0.78: the foot's shade).
 */

#include <math.h>

#include "remade.h"

#define LOOP_N 28               /* corners of an outline: 7 a round corner */
#define RINGS 8
#define PI 3.14159265358979323846

typedef struct { double x, z; } P2;

typedef struct {
    RmOut *o;
    unsigned src;
    unsigned char rgb[3];
    double mid[3], ex[3], ez[3], y0;
} Cu;

static void loop(double half, double rad, double inset, P2 *out)
{
    static const int sx[4] = { 1, -1, -1, 1 }, sz[4] = { 1, 1, -1, -1 };
    double h = half - inset, r = rad - inset > 0.4 ? rad - inset : 0.4;
    int q, k;

    for (q = 0; q < 4; q++) {
        for (k = 0; k < 7; k++) {
            double t = (q * 90.0 + k * 15.0) * PI / 180.0;

            out[q * 7 + k].x = sx[q] * (h - r) + r * cos(t);
            out[q * 7 + k].z = sz[q] * (h - r) + r * sin(t);
        }
    }
}

static void pos(const Cu *c, double x, double z, double y, double out[3])
{
    out[0] = c->mid[0] + c->ex[0] * x + c->ez[0] * z;
    out[1] = c->y0 + y;
    out[2] = c->mid[2] + c->ex[2] * x + c->ez[2] * z;
}

/* A triangle facing `outward`, in the picture's pixel (col, row), shaded. */
static void face(const Cu *c, const double a[3], const double b[3], const double d[3], int col, int row,
                 const double outward[3], double shade)
{
    double e1[3], e2[3], n[3], uv[2] = { (col + 0.5) / 32.0, (row + 0.5) / 32.0 };
    unsigned char rgb[3];
    int k;

    rm_add(b, a, -1.0, e1);
    rm_add(d, a, -1.0, e2);
    rm_cross(e1, e2, n);
    if (sqrt(rm_dot(n, n)) < 1e-7) {
        return;
    }
    for (k = 0; k < 3; k++) {
        rgb[k] = (unsigned char)(int)(c->rgb[k] * shade);
    }
    if (rm_dot(n, outward) < 0.0) {
        rm_tri(c->o, c->src, -1, 1, rgb, a, d, b, uv, uv, uv, 2, 0);
    } else {
        rm_tri(c->o, c->src, -1, 1, rgb, a, b, d, uv, uv, uv, 2, 0);
    }
}

/* The part of a convex polygon left of the line a -> b. */
static int clip(const P2 *poly, int n, P2 a, P2 b, P2 *out)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        P2 c = poly[i], d = poly[(i + 1) % n];
        double sc = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
        double sd = (b.x - a.x) * (d.z - a.z) - (b.z - a.z) * (d.x - a.x);

        if (sc >= 0) {
            out[m++] = c;
        }
        if ((sc >= 0) != (sd >= 0)) {
            double t = sc / (sc - sd);

            out[m].x = c.x + (d.x - c.x) * t;
            out[m].z = c.z + (d.z - c.z) * t;
            m++;
        }
    }
    return m;
}

void rm_cushion(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    /* the side, from the floor up: its height, how far in, the picture's row for the band that ends there */
    static const double ring_y[RINGS] = { 0.0, 0.5, 1.6, 3.6, 4.6, 5.3, 5.75, 6.0 };
    static const double ring_in[RINGS] = { 0.5, 0.15, 0.0, 0.0, 0.25, 0.6, 1.0, 1.5 };
    static const int ring_row[RINGS] = { 0, 26, 26, 22, 20, 18, 18, 17 };
    const double half = 7.0, rad = 3.5, high = rm_optf(opt, "high", 6.0);
    const double up[3] = { 0.0, 1.0, 0.0 };
    unsigned bi;

    (void)tex;
    (void)ntex;
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        P2 loops[RINGS][LOOP_N];
        double m[3][3], xs[25], zs[15], th, zcut, ymin, ymax;
        Cu c;
        int i, k, r, col;

        if (bt->n != 2) {
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
        if (ymax - ymin > 0.01 || rm_plane(&bt->tri[0], m) != 0) {
            continue;                   /* not a level picture */
        }
        c.o = out;
        c.src = bt->tri[0].id;
        c.rgb[0] = bt->rgb[0]; c.rgb[1] = bt->rgb[1]; c.rgb[2] = bt->rgb[2];
        rm_at(m, 0.5, 0.5, c.mid);
        for (k = 0; k < 3; k++) {       /* the picture's across and down */
            c.ex[k] = m[0][k];
            c.ez[k] = m[1][k];
        }
        rm_norm(c.ex);
        rm_norm(c.ez);
        c.y0 = (double)bt->tri[0].p[0][1] - rm_optf(opt, "floor", 0.3);
        for (i = 0; i < RINGS; i++) {
            loop(half, rad, ring_in[i], loops[i]);
        }
        for (i = 1; i < RINGS; i++) {
            double ya = ring_y[i - 1] * high / 6.0, yb = ring_y[i] * high / 6.0;
            double shade = i == 1 ? rm_optf(opt, "foot", 0.78) : 1.0;

            for (k = 0; k < LOOP_N; k++) {
                int k2 = (k + 1) % LOOP_N;
                double a0[3], a1[3], b0[3], b1[3], outward[3];
                double ox = loops[i][k].x + loops[i][k2].x, oz = loops[i][k].z + loops[i][k2].z;

                pos(&c, loops[i - 1][k].x, loops[i - 1][k].z, ya, a0);
                pos(&c, loops[i - 1][k2].x, loops[i - 1][k2].z, ya, a1);
                pos(&c, loops[i][k].x, loops[i][k].z, yb, b0);
                pos(&c, loops[i][k2].x, loops[i][k2].z, yb, b1);
                outward[0] = c.ex[0] * ox + c.ez[0] * oz;
                outward[1] = c.ex[1] * ox + c.ez[1] * oz + (i >= 5 ? 0.5 : 0.0);
                outward[2] = c.ex[2] * ox + c.ez[2] * oz;
                face(&c, a0, a1, b1, 16, ring_row[i], outward, shade);
                face(&c, a0, b1, b0, 16, ring_row[i], outward, shade);
            }
        }
        /* the top: the picture's pixels (columns 4..27, rows 4..17), each cut to the round outline */
        th = half - ring_in[RINGS - 1];
        zcut = th * 0.4 < 2.0 ? th * 0.4 : 2.0;
        for (i = 0; i < 25; i++) {
            xs[i] = -th + 2.0 * th * i / 24.0;
        }
        for (i = 0; i < 4; i++) {
            zs[i] = -th + (th - zcut) * i / 4.0;
        }
        for (i = 0; i < 8; i++) {
            zs[4 + i] = -zcut + 2.0 * zcut * i / 8.0;
        }
        for (i = 0; i < 3; i++) {
            zs[12 + i] = zcut + (th - zcut) * i / 2.0;
        }
        for (r = 0; r < 14; r++) {
            for (col = 0; col < 24; col++) {
                P2 pa[40], pb[40], *poly = pa, *next = pb;
                int n = 4, pc = r < 4 ? (col < 4 ? 4 : col > 19 ? 19 : col) : col;

                poly[0].x = xs[col];     poly[0].z = zs[r];
                poly[1].x = xs[col + 1]; poly[1].z = zs[r];
                poly[2].x = xs[col + 1]; poly[2].z = zs[r + 1];
                poly[3].x = xs[col];     poly[3].z = zs[r + 1];
                for (k = 0; k < LOOP_N && n >= 3; k++) {
                    P2 *t;

                    n = clip(poly, n, loops[RINGS - 1][k], loops[RINGS - 1][(k + 1) % LOOP_N], next);
                    t = poly; poly = next; next = t;
                }
                for (i = 1; i + 1 < n; i++) {
                    double q0[3], q1[3], q2[3];

                    pos(&c, poly[0].x, poly[0].z, high, q0);
                    pos(&c, poly[i].x, poly[i].z, high, q1);
                    pos(&c, poly[i + 1].x, poly[i + 1].z, high, q2);
                    face(&c, q0, q1, q2, 4 + pc, 4 + r, up, 1.0);
                }
            }
        }
        rm_drop_all(out, bt);
    }
}
