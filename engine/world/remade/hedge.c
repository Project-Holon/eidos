/*
 * engine/world/remade/hedge.c -- a hedge drawn as a hollow box: its sides a
 * picture with a ragged, see-through edge (Undella Town's flower hedges).
 * The field looks down on it and never sees the gaps; a battle camera low
 * over the ground sees through them into the empty inside.
 *
 * The hedge's faces in the rule's box are split into hedges by shared
 * corners.  Each gets a solid core: four walls looking out, `inset` units
 * inside its sides (0.5), from its foot to the top of its upright sides,
 * and a lid a quarter unit under its own top (whose picture has gaps too,
 * where the ground behind showed through).  The core is painted with one pixel of the
 * hedge's own side picture: the solid pixel nearest the mean colour of the
 * solid pixels its sides show, so the gaps show the hedge's green.
 *
 * Settings: inset (0.5).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    const RmTri *t;
    const RmBatch *b;
} HgTri;

static unsigned hg_root(unsigned *up, unsigned i)
{
    while (up[i] != i) {
        up[i] = up[up[i]];
        i = up[i];
    }
    return i;
}

static int hg_touch(const RmTri *a, const RmTri *b)
{
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (fabsf(a->p[i][0] - b->p[j][0]) < 0.05f && fabsf(a->p[i][1] - b->p[j][1]) < 0.05f
                && fabsf(a->p[i][2] - b->p[j][2]) < 0.05f) {
                return 1;
            }
        }
    }
    return 0;
}

static int hg_upright(const RmTri *t)
{
    double e1[3], e2[3], n[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = t->p[1][k] - t->p[0][k];
        e2[k] = t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, n);
    return rm_norm(n) > 1e-9 && fabs(n[1]) < 0.3;
}

/* The solid pixel of the upright faces' pictures nearest the mean colour of the solid pixels they show; its uv. */
static int hg_pixel(const HgTri *h, unsigned n, const unsigned *up, unsigned root, const RmTex *tex, unsigned ntex,
                    double uv[2], unsigned *src)
{
    double sum[3] = { 0, 0, 0 }, cnt = 0, best = 1e30;
    int pass, found = 0;
    unsigned i;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n; i++) {
            const RmTri *t = h[i].t;
            const RmTex *x;
            double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
            int px, py, k;

            if (hg_root((unsigned *)up, i) != root || !hg_upright(t) || h[i].b->tex < 0 || (unsigned)h[i].b->tex >= ntex) {
                continue;
            }
            x = &tex[h[i].b->tex];
            if (x->rgba == NULL || x->w <= 0 || x->h <= 0) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                u0 = t->uv[k][0] < u0 ? t->uv[k][0] : u0;
                u1 = t->uv[k][0] > u1 ? t->uv[k][0] : u1;
                v0 = t->uv[k][1] < v0 ? t->uv[k][1] : v0;
                v1 = t->uv[k][1] > v1 ? t->uv[k][1] : v1;
            }
            for (py = (int)floor(v0 * x->h); py < (int)ceil(v1 * x->h); py++) {
                for (px = (int)floor(u0 * x->w); px < (int)ceil(u1 * x->w); px++) {
                    const int qx = ((px % x->w) + x->w) % x->w, qy = ((py % x->h) + x->h) % x->h;
                    const unsigned char *c = &x->rgba[((size_t)qy * (size_t)x->w + (size_t)qx) * 4u];

                    if (c[3] < 255) {
                        continue;
                    }
                    if (pass == 0) {
                        sum[0] += c[0];
                        sum[1] += c[1];
                        sum[2] += c[2];
                        cnt += 1;
                    } else {
                        const double d = (c[0] - sum[0]) * (c[0] - sum[0]) + (c[1] - sum[1]) * (c[1] - sum[1])
                                         + (c[2] - sum[2]) * (c[2] - sum[2]);

                        if (d < best) {
                            best = d;
                            uv[0] = (px + 0.5) / x->w;
                            uv[1] = (py + 0.5) / x->h;
                            *src = t->id;
                            found = 1;
                        }
                    }
                }
            }
        }
        if (pass == 0) {
            if (cnt == 0) {
                return 0;
            }
            sum[0] /= cnt;
            sum[1] /= cnt;
            sum[2] /= cnt;
        }
    }
    return found;
}

void rm_hedge(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double inset = rm_optf(opt, "inset", 0.5);
    HgTri *h;
    unsigned *up, n = 0, i, j, bi;

    for (bi = 0; bi < nb; bi++) {
        n += b[bi].bb ? 0u : b[bi].n;
    }
    h = (HgTri *)calloc(n ? n : 1u, sizeof *h);
    up = (unsigned *)calloc(n ? n : 1u, sizeof *up);
    if (h == NULL || up == NULL) {
        free(h);
        free(up);
        out->fail = 1;
        return;
    }
    {
        /* (the ground the hedges stand on, level at the lowest height in the box, is no part of a hedge) */
        float floor_y = 1e30f;
        int k;

        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                for (k = 0; k < 3; k++) {
                    floor_y = b[bi].tri[i].p[k][1] < floor_y ? b[bi].tri[i].p[k][1] : floor_y;
                }
            }
        }
        n = 0;
        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                const RmTri *t = &b[bi].tri[i];

                if (fabsf(t->p[0][1] - floor_y) < 0.1f && fabsf(t->p[1][1] - floor_y) < 0.1f
                    && fabsf(t->p[2][1] - floor_y) < 0.1f) {
                    continue;
                }
                h[n].t = t;
                h[n].b = &b[bi];
                up[n] = n;
                n++;
            }
        }
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (hg_touch(h[i].t, h[j].t)) {
                up[hg_root(up, i)] = hg_root(up, j);
            }
        }
    }
    for (i = 0; i < n; i++) {
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, top = -1e30, uv[2] = { 0, 0 };
        unsigned src = 0, k;
        int c;

        if (hg_root(up, i) != i) {
            continue;
        }
        for (j = 0; j < n; j++) {
            if (hg_root(up, j) != i) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                for (c = 0; c < 3; c++) {
                    lo[c] = h[j].t->p[k][c] < lo[c] ? h[j].t->p[k][c] : lo[c];
                    hi[c] = h[j].t->p[k][c] > hi[c] ? h[j].t->p[k][c] : hi[c];
                }
                if (hg_upright(h[j].t)) {
                    top = h[j].t->p[k][1] > top ? h[j].t->p[k][1] : top;
                }
            }
        }
        if (top < -1e29 || hi[0] - lo[0] < 4 * inset || hi[2] - lo[2] < 4 * inset
            || !hg_pixel(h, n, up, i, tex, ntex, uv, &src)) {
            continue;
        }
        {
            const double x0 = lo[0] + inset, x1 = hi[0] - inset, z0 = lo[2] + inset, z1 = hi[2] - inset;
            const double y0 = lo[1], y1 = top;
            /* each wall: two corners along the foot, looking out (n) */
            const double w[4][2][2] = { { { x0, z1 }, { x1, z1 } }, { { x1, z0 }, { x0, z0 } },
                                        { { x1, z1 }, { x1, z0 } }, { { x0, z0 }, { x0, z1 } } };
            const double nn[4][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 } };
            const unsigned char *lit = NULL;
            int s;

            for (j = 0; j < n && lit == NULL; j++) {
                lit = h[j].t->id == src ? h[j].t->rgb : NULL;        /* (the core lit as the face its pixel is from) */
            }

            {
                /* the lid: a quarter unit under the hedge's own top (two faces in one plane would flicker), whose
                 * picture is see-through in places too */
                const double ly = y1 - 0.25;
                double a[3] = { x0, ly, z0 }, bb[3] = { x1, ly, z0 }, cc[3] = { x1, ly, z1 }, d[3] = { x0, ly, z1 };
                double e1[3], e2[3], fn[3];
                const double upn[3] = { 0, 1, 0 };

                rm_add(bb, a, -1.0, e1);
                rm_add(cc, a, -1.0, e2);
                rm_cross(e1, e2, fn);
                if (rm_dot(fn, upn) >= 0.0) {
                    rm_tri(out, src, -1, 1, lit, a, bb, cc, uv, uv, uv, 0, 0);
                    rm_tri(out, src, -1, 1, lit, a, cc, d, uv, uv, uv, 0, 0);
                } else {
                    rm_tri(out, src, -1, 1, lit, a, cc, bb, uv, uv, uv, 0, 0);
                    rm_tri(out, src, -1, 1, lit, a, d, cc, uv, uv, uv, 0, 0);
                }
            }
            for (s = 0; s < 4; s++) {
                double a[3] = { w[s][0][0], y0, w[s][0][1] }, bb[3] = { w[s][1][0], y0, w[s][1][1] };
                double cc[3] = { w[s][1][0], y1, w[s][1][1] }, d[3] = { w[s][0][0], y1, w[s][0][1] };
                double e1[3], e2[3], fn[3];

                rm_add(bb, a, -1.0, e1);
                rm_add(cc, a, -1.0, e2);
                rm_cross(e1, e2, fn);
                if (rm_dot(fn, nn[s]) >= 0.0) {
                    rm_tri(out, src, -1, 1, lit, a, bb, cc, uv, uv, uv, 0, 0);
                    rm_tri(out, src, -1, 1, lit, a, cc, d, uv, uv, uv, 0, 0);
                } else {
                    rm_tri(out, src, -1, 1, lit, a, cc, bb, uv, uv, uv, 0, 0);
                    rm_tri(out, src, -1, 1, lit, a, d, cc, uv, uv, uv, 0, 0);
                }
            }
        }
    }
    free(h);
    free(up);
}
