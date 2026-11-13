/*
 * engine/world/remade/gate.c -- an upright flat cut-out picture given its
 * depth: the barred gates of the Pokemon League (flat, they were a line
 * from the side and nothing from behind).
 *
 * Each solid pixel of the picture where the model shows it (the picture may
 * repeat along the model, and each of the model's rectangles has its own
 * stretch of it) is a block `depth` map units deep about the picture's
 * plane: its front and its back show the pixel, and where the pixel beside
 * it is clear, or the rectangle ends, a side wall closes it in the same
 * pixel.  So a gate's bars are square posts and its rail a square beam.
 * A level piece of the model (the strip the posts stand in) stays as it is.
 *
 * Settings: depth (2).
 */

#include <math.h>

#include "remade.h"

typedef struct {
    RmOut *o;
    unsigned src;
    unsigned char rgb[3];
} Ga;

static void quad(const Ga *g, const double a[3], const double b[3], const double c[3], const double d[3],
                 const double uv[2], const double outward[3])
{
    double e1[3], e2[3], n[3];

    rm_add(b, a, -1.0, e1);
    rm_add(c, a, -1.0, e2);
    rm_cross(e1, e2, n);
    if (rm_dot(n, outward) < 0.0) {
        rm_tri(g->o, g->src, -1, 1, g->rgb, d, c, b, uv, uv, uv, 0, 0);
        rm_tri(g->o, g->src, -1, 1, g->rgb, d, b, a, uv, uv, uv, 0, 0);
    } else {
        rm_tri(g->o, g->src, -1, 1, g->rgb, a, b, c, uv, uv, uv, 0, 0);
        rm_tri(g->o, g->src, -1, 1, g->rgb, a, c, d, uv, uv, uv, 0, 0);
    }
}

static int solid_px(const RmTex *t, int c, int r)
{
    c = ((c % t->w) + t->w) % t->w;
    r = ((r % t->h) + t->h) % t->h;
    return t->rgba[((size_t)r * (size_t)t->w + (size_t)c) * 4u + 3u] >= 128;
}

void rm_gate(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    static const int side[4][4] = { { 0, 1, 0, -1 }, { 1, 2, 1, 0 }, { 2, 3, 0, 1 }, { 3, 0, -1, 0 } };
    const double depth = rm_optf(opt, "depth", 2.0);
    unsigned bi;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        const RmTex *pic;
        unsigned q0, first = 0;
        int W, H, cut = 0, have = 0;
        size_t i;
        Ga g;

        if (bt->n < 2 || bt->bb || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL) {
            continue;
        }
        pic = &tex[bt->tex];
        W = pic->w;
        H = pic->h;
        for (i = 0; i < (size_t)W * (size_t)H && !cut; i++) {
            cut = pic->rgba[i * 4u + 3u] < 128;
        }
        if (!cut) {
            continue;                   /* not a cut-out picture */
        }
        g.o = out;
        g.rgb[0] = bt->rgb[0]; g.rgb[1] = bt->rgb[1]; g.rgb[2] = bt->rgb[2];
        /* the model's rectangles, two triangles each */
        for (q0 = 0; q0 + 1 < bt->n; q0 += 2) {
            const RmTri *t0 = &bt->tri[q0], *t1 = &bt->tri[q0 + 1];
            double m[3][3], n[3], n2[3], e1[3], e2[3], off[3], p0[3], far = 0.0;
            double cu0 = 1e30, cu1 = -1e30, rv0 = 1e30, rv1 = -1e30;
            int c0, c1, r0, r1, c, r, k, j;

            for (k = 0; k < 3; k++) {
                p0[k] = t0->p[0][k];
                e1[k] = (double)t0->p[1][k] - t0->p[0][k];
                e2[k] = (double)t0->p[2][k] - t0->p[0][k];
            }
            rm_cross(e1, e2, n);
            for (k = 0; k < 3; k++) {
                e1[k] = (double)t1->p[1][k] - t1->p[0][k];
                e2[k] = (double)t1->p[2][k] - t1->p[0][k];
            }
            rm_cross(e1, e2, n2);
            if (rm_norm(n) <= 0.0 || rm_dot(n2, n2) <= 0.0) {
                continue;
            }
            for (j = 0; j < 2; j++) {
                const RmTri *t = j == 0 ? t0 : t1;

                for (k = 0; k < 3; k++) {
                    double v[3] = { t->p[k][0] - p0[0], t->p[k][1] - p0[1], t->p[k][2] - p0[2] };
                    double dd = fabs(rm_dot(v, n)), u = (double)t->uv[k][0] * W, w = (double)t->uv[k][1] * H;

                    far = dd > far ? dd : far;
                    cu0 = u < cu0 ? u : cu0;
                    cu1 = u > cu1 ? u : cu1;
                    rv0 = w < rv0 ? w : rv0;
                    rv1 = w > rv1 ? w : rv1;
                }
            }
            if (fabs(n[1]) >= 0.3 || far > 0.05 || rm_plane(t0, m) != 0) {
                continue;               /* level, or not one plane: left as it is */
            }
            c0 = (int)floor(cu0 + 1e-4);
            c1 = (int)ceil(cu1 - 1e-4);
            r0 = (int)floor(rv0 + 1e-4);
            r1 = (int)ceil(rv1 - 1e-4);
            for (k = 0; k < 3; k++) {
                off[k] = n[k] * depth / 2.0;
            }
            if (!have) {                /* the first rectangle made gives the new triangles their material */
                first = t0->id;
                have = 1;
            }
            g.src = first;
            rm_drop(out, t0->id);
            rm_drop(out, t1->id);
#define SOLID(cc, rr) ((cc) >= c0 && (cc) < c1 && (rr) >= r0 && (rr) < r1 && solid_px(pic, cc, rr))
            for (r = r0; r < r1; r++) {
                for (c = c0; c < c1; c++) {
                    double uv[2] = { (c + 0.5) / W, (r + 0.5) / H }, kk[4][3], mid[3], f[4][3], bk[4][3], mn[3];
                    double ca = c > cu0 ? c : cu0, cb = c + 1 < cu1 ? c + 1 : cu1;
                    double ra = r > rv0 ? r : rv0, rb = r + 1 < rv1 ? r + 1 : rv1;

                    if (!SOLID(c, r)) {
                        continue;
                    }
                    rm_at(m, ca / W, ra / H, kk[0]);
                    rm_at(m, cb / W, ra / H, kk[1]);
                    rm_at(m, cb / W, rb / H, kk[2]);
                    rm_at(m, ca / W, rb / H, kk[3]);
                    for (k = 0; k < 3; k++) {
                        mid[k] = (kk[0][k] + kk[2][k]) / 2.0;
                        mn[k] = -n[k];
                    }
                    for (j = 0; j < 4; j++) {
                        rm_add(kk[j], off, 1.0, f[j]);
                        rm_add(kk[j], off, -1.0, bk[j]);
                    }
                    quad(&g, f[0], f[1], f[2], f[3], uv, n);
                    quad(&g, bk[0], bk[1], bk[2], bk[3], uv, mn);
                    for (j = 0; j < 4; j++) {
                        if (!SOLID(c + side[j][2], r + side[j][3])) {
                            int i0 = side[j][0], i1 = side[j][1];
                            double outward[3];

                            for (k = 0; k < 3; k++) {
                                outward[k] = (kk[i0][k] + kk[i1][k]) / 2.0 - mid[k];
                            }
                            quad(&g, f[i0], f[i1], bk[i1], bk[i0], uv, outward);
                        }
                    }
                }
            }
#undef SOLID
        }
    }
}
