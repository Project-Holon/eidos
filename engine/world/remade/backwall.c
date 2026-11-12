/*
 * engine/world/remade/backwall.c -- a long box whose far end is missing: the
 * Cold Storage's container (its north end is not in the model: the field's
 * camera looks into its open south end and never sees the other).
 *
 * The end is closed with one flat wall at the model's north end, cut to the
 * outline the model has seen from the north (filled), in the pixels of its own side
 * wall: the side wall's picture goes on round the corner, the same ribs at
 * the same heights, so the wall reads as one more side of the same box.
 * The wall is lit as the game lights the model.
 *
 * The side wall: the biggest triangle that faces east or west.  Its corners
 * say how its texture runs along the box and up it (any way round); a pixel
 * of the new wall takes the texture's pixel of the side wall at the same
 * height, as far along as the pixel is from the new wall's edge.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

void rm_backwall(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const RmBatch *rb = NULL;
    const RmTri *rt = NULL;
    double best = 0.0, xa = 1e30, xb = -1e30, ya = 1e30, yb = -1e30, z0 = 1e30;
    double dz1, dy1, dz2, dy2, det, uz, uy, vz, vy, zmin, ppu;
    unsigned bi, i;
    int k, Wm, Hm, x, y;
    unsigned char *px;
    const RmTex *st;

    (void)opt;
    /* the walls' box (everything that is not level) */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], len;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            len = rm_norm(n);
            if (len <= 0.0) {
                continue;
            }
            if (fabs(n[1]) <= 0.6) {
                for (k = 0; k < 3; k++) {
                    xa = t->p[k][0] < xa ? t->p[k][0] : xa;
                    xb = t->p[k][0] > xb ? t->p[k][0] : xb;
                    ya = t->p[k][1] < ya ? t->p[k][1] : ya;
                    yb = t->p[k][1] > yb ? t->p[k][1] : yb;
                    z0 = t->p[k][2] < z0 ? t->p[k][2] : z0;
                }
            }
        }
    }
    /* the side wall: the biggest triangle on the box's east or west end that looks outward (a box with an
     * open end has inner walls too, in the dark of its inside) */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], n[3], len, tx = ((double)t->p[0][0] + t->p[1][0] + t->p[2][0]) / 3.0;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, n);
            len = rm_norm(n);
            if (len > best && fabs(n[1]) < 0.3 && ((n[0] > 0.7 && tx > xb - 1.0) || (n[0] < -0.7 && tx < xa + 1.0))
                && b[bi].tex >= 0 && (unsigned)b[bi].tex < ntex && tex[b[bi].tex].rgba != NULL) {
                best = len;
                rb = &b[bi];
                rt = t;
            }
        }
    }
    if (rt == NULL || xb - xa < 1.0 || yb - ya < 1.0 || out->ntex >= 8) {
        return;
    }
    st = &tex[rb->tex];
    /* how the side wall's picture runs: (u, v) by the place along the box (z) and the height (y) */
    dz1 = (double)rt->p[1][2] - rt->p[0][2]; dy1 = (double)rt->p[1][1] - rt->p[0][1];
    dz2 = (double)rt->p[2][2] - rt->p[0][2]; dy2 = (double)rt->p[2][1] - rt->p[0][1];
    det = dz1 * dy2 - dz2 * dy1;
    if (fabs(det) < 1e-9) {
        return;
    }
    uz = (((double)rt->uv[1][0] - rt->uv[0][0]) * dy2 - ((double)rt->uv[2][0] - rt->uv[0][0]) * dy1) / det;
    uy = (((double)rt->uv[2][0] - rt->uv[0][0]) * dz1 - ((double)rt->uv[1][0] - rt->uv[0][0]) * dz2) / det;
    vz = (((double)rt->uv[1][1] - rt->uv[0][1]) * dy2 - ((double)rt->uv[2][1] - rt->uv[0][1]) * dy1) / det;
    vy = (((double)rt->uv[2][1] - rt->uv[0][1]) * dz1 - ((double)rt->uv[1][1] - rt->uv[0][1]) * dz2) / det;
    zmin = rt->p[0][2] < rt->p[1][2] ? rt->p[0][2] : rt->p[1][2];
    zmin = rt->p[2][2] < zmin ? rt->p[2][2] : zmin;
    ppu = sqrt(uz * st->w * uz * st->w + vz * st->h * vz * st->h);      /* the side wall's own pixels a map unit */
    ppu = ppu < 1.0 ? 1.0 : ppu > 4.0 ? 4.0 : ppu;
    Wm = (int)rint((xb - xa) * ppu);
    Hm = (int)rint((yb - ya) * ppu);
    Wm = Wm < 2 ? 2 : Wm;
    Hm = Hm < 2 ? 2 : Hm;
    if (Wm > 1024 || Hm > 1024) {
        return;
    }
    px = (unsigned char *)calloc((size_t)Wm * Hm, 4);
    if (px == NULL) {
        out->fail = 1;
        return;
    }
    for (y = 0; y < Hm; y++) {
        double wy = yb - (y + 0.5) / ppu;

        for (x = 0; x < Wm; x++) {
            double wx = xa + (x + 0.5) / ppu, sz = zmin + (x + 0.5) / ppu;
            double su = rt->uv[0][0] + uz * (sz - rt->p[0][2]) + uy * (wy - rt->p[0][1]);
            double sv = rt->uv[0][1] + vz * (sz - rt->p[0][2]) + vy * (wy - rt->p[0][1]);
            int sx = (int)floor(su * st->w), sy = (int)floor(sv * st->h), in = 0;
            unsigned char *o = px + ((size_t)y * Wm + (size_t)x) * 4u;

            sx = ((sx % st->w) + st->w) % st->w;
            sy = ((sy % st->h) + st->h) % st->h;
            memcpy(o, st->rgba + ((size_t)sy * st->w + (size_t)sx) * 4u, 3u);
            /* inside the model's outline seen from the north? (its triangles, flat on the x-y plane) */
            for (bi = 0; bi < nb && !in; bi++) {
                for (i = 0; !b[bi].bb && i < b[bi].n && !in; i++) {
                    const RmTri *t = &b[bi].tri[i];
                    double ax = t->p[0][0], ay = t->p[0][1], bx = t->p[1][0], by = t->p[1][1], cx = t->p[2][0], cy = t->p[2][1];
                    double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy), w0, w1;

                    if (fabs(den) < 1e-9) {
                        continue;
                    }
                    w0 = ((by - cy) * (wx - cx) + (cx - bx) * (wy - cy)) / den;
                    w1 = ((cy - ay) * (wx - cx) + (ax - cx) * (wy - cy)) / den;
                    in = w0 >= -1e-3 && w1 >= -1e-3 && 1.0 - w0 - w1 >= -1e-3;
                }
            }
            o[3] = in ? 255 : 0;
        }
    }
    /* the outline is filled: a row is solid from its first to its last pixel (seen from the north, an open box
     * is only its rim: the hole in the middle is the end this wall closes) */
    for (y = 0; y < Hm; y++) {
        int first = -1, last = -1;

        for (x = 0; x < Wm; x++) {
            if (px[((size_t)y * Wm + (size_t)x) * 4u + 3u]) {
                first = first < 0 ? x : first;
                last = x;
            }
        }
        for (x = first; first >= 0 && x <= last; x++) {
            px[((size_t)y * Wm + (size_t)x) * 4u + 3u] = 255;
        }
    }
    {
        /* (a quarter unit out from the end: the box's inside has its own dark end wall right there, shown
         * from both sides, and two walls in one plane would flicker) */
        const double zw = z0 - 0.25;
        const double q[4][3] = { { xb, yb, zw }, { xb, ya, zw }, { xa, yb, zw }, { xa, ya, zw } };
        const double t[4][2] = { { 1, 0 }, { 1, 1 }, { 0, 0 }, { 0, 1 } };
        unsigned first = out->ntri;

        /* (shown from both sides: over the inside's own end wall a slit is open to the far end) */
        rm_tri(out, rt->id, out->ntex, 0, rb->rgb, q[0], q[1], q[2], t[0], t[1], t[2], 0, 0);
        rm_tri(out, rt->id, out->ntex, 0, rb->rgb, q[2], q[1], q[3], t[2], t[1], t[3], 0, 0);
        for (i = first; i < out->ntri; i++) {
            out->tri[i].lit = 1;        /* lit by the game with its normal, as the model's own walls */
        }
    }
    if (out->fail) {
        free(px);
        return;
    }
    out->tex[out->ntex].w = Wm;
    out->tex[out->ntex].h = Hm;
    out->tex[out->ntex].rgba = px;
    out->ntex++;
}
