/*
 * engine/world/remade/boardwalk.c -- a plank walkway on posts over long grass
 * (Route 7's), part of a map chunk's own mesh.
 *
 * The map draws the walkway for the field's steep camera, over the long
 * grass's block (a lid 16 units up): its planks are level sheets with no
 * thickness, 32 up; its short posts have three sides only and end 18 up, just
 * above the lid, their feet painted with a tuft of grass.  A battle camera low
 * over the ground sees sheets on stumps that hang in the air.  Remade, from
 * the walkway's own faces and picture:
 *   - each short post (found by its cap: the level square on top of it) is
 *     left out and built again on all four sides, from its cap down to the
 *     ground (`foot`, default 0), in the picture of the walkway's own long
 *     posts (the ones at its ramps, drawn from top to ground): columns 1 to 8,
 *     rows 9 to 32 of the walkway's picture;
 *   - the planks get a thickness (`thick`, default 3 units): a side under
 *     each open edge of the walk's level faces, in the pixels along that edge,
 *     a little darker, and an underside, darker still.
 * The ramps, their rails and the long posts stay as the map has them.
 *
 * The walkway's draw is the one whose picture is 32 x 32 with a post cap's
 * light ring at (4, 4) and (3, 3) and its wood at (28, 9): nothing else of
 * the chunk is touched, and a chunk without that picture stays as it is.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define BW_MAX 1024

static int bw_is_walk(const RmTex *t)
{
    static const struct {
        int x, y;
        unsigned char rgb[3];
    } sig[3] = { { 4, 4, { 0xE6, 0xBD, 0x7B } }, { 3, 3, { 0xDE, 0xB4, 0x73 } }, { 28, 9, { 0xBD, 0x9C, 0x6A } } };
    int k;

    if (t == NULL || t->rgba == NULL || t->w != 32 || t->h != 32) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        const unsigned char *px = &t->rgba[((size_t)sig[k].y * 32u + (size_t)sig[k].x) * 4u];

        if (px[0] != sig[k].rgb[0] || px[1] != sig[k].rgb[1] || px[2] != sig[k].rgb[2]) {
            return 0;
        }
    }
    return 1;
}

/* A triangle's box, its picture's box in pixels (of 32), and how far its normal points up (-1..1). */
typedef struct {
    double lo[3], hi[3], u0, u1, v0, v1, ny;
} BwBox;

static void bw_box(const RmTri *t, BwBox *q)
{
    double e1[3], e2[3], n[3];
    int a, k;

    for (k = 0; k < 3; k++) {
        q->lo[k] = q->hi[k] = t->p[0][k];
        e1[k] = (double)t->p[1][k] - t->p[0][k];
        e2[k] = (double)t->p[2][k] - t->p[0][k];
    }
    q->u0 = q->u1 = t->uv[0][0] * 32.0;
    q->v0 = q->v1 = t->uv[0][1] * 32.0;
    for (a = 1; a < 3; a++) {
        for (k = 0; k < 3; k++) {
            q->lo[k] = t->p[a][k] < q->lo[k] ? t->p[a][k] : q->lo[k];
            q->hi[k] = t->p[a][k] > q->hi[k] ? t->p[a][k] : q->hi[k];
        }
        q->u0 = t->uv[a][0] * 32.0 < q->u0 ? t->uv[a][0] * 32.0 : q->u0;
        q->u1 = t->uv[a][0] * 32.0 > q->u1 ? t->uv[a][0] * 32.0 : q->u1;
        q->v0 = t->uv[a][1] * 32.0 < q->v0 ? t->uv[a][1] * 32.0 : q->v0;
        q->v1 = t->uv[a][1] * 32.0 > q->v1 ? t->uv[a][1] * 32.0 : q->v1;
    }
    rm_cross(e1, e2, n);
    q->ny = rm_norm(n) > 0.0 ? n[1] : 0.0;
}

static void bw_shade(const unsigned char in[3], double f, unsigned char out[3])
{
    int k;

    for (k = 0; k < 3; k++) {
        const double v = (double)in[k] * f;

        out[k] = (unsigned char)(v > 255.0 ? 255.0 : v);
    }
}

static void bw_quad(RmOut *out, unsigned src, const unsigned char rgb[3], const double p[4][3], const double uv[4][2])
{
    rm_tri(out, src, -1, 0, rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
    rm_tri(out, src, -1, 0, rgb, p[0], p[2], p[3], uv[0], uv[2], uv[3], 0, 0);
}

static int bw_same(const float a[3], const float b[3])
{
    return fabs((double)a[0] - b[0]) < 0.05 && fabs((double)a[1] - b[1]) < 0.05 && fabs((double)a[2] - b[2]) < 0.05;
}

void rm_boardwalk(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double foot = rm_optf(opt, "foot", 0.0), thick = rm_optf(opt, "thick", 3.0);
    /* (the long post's picture: columns 1 to 8, rows 9 to 32) */
    const double pu0 = 1.0 / 32.0, pu1 = 8.0 / 32.0, pv0 = 9.0 / 32.0, pv1 = 1.0;
    unsigned bi, j, k;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *B = &b[bi];
        static unsigned char kind[BW_MAX];      /* 1 a plank, 2 a post's cap, 3 a short post's side */
        static double post[BW_MAX][5];          /* a short post: x0, x1, z0, z1, its top */
        unsigned npost = 0, cap_src = 0;
        unsigned char side_rgb[4][3], have_side[4] = { 0, 0, 0, 0 };
        unsigned side_n[4] = { 0, 0, 0, 0 };
        double side_sum[4][3];

        if (B->tex < 0 || (unsigned)B->tex >= ntex || !bw_is_walk(&tex[B->tex]) || B->n > BW_MAX) {
            continue;
        }
        memset(side_sum, 0, sizeof side_sum);
        for (j = 0; j < B->n; j++) {
            const RmTri *t = &B->tri[j];
            BwBox q;

            bw_box(t, &q);
            kind[j] = 0;
            if (q.ny > 0.7 && q.hi[1] - q.lo[1] < 0.01) {
                if (q.u0 >= 18.5 && q.u1 <= 24.5 && q.v1 <= 6.5) {
                    kind[j] = 1;
                } else if (q.u0 >= 9.5 && q.u1 <= 17.5 && q.v1 <= 8.5) {
                    kind[j] = 2;
                    for (k = 0; k < npost; k++) {
                        if (fabs(post[k][0] - q.lo[0]) < 0.05 && fabs(post[k][1] - q.hi[0]) < 0.05
                            && fabs(post[k][2] - q.lo[2]) < 0.05 && fabs(post[k][3] - q.hi[2]) < 0.05) {
                            break;
                        }
                    }
                    if (k == npost && npost < BW_MAX) {
                        post[npost][0] = q.lo[0];
                        post[npost][1] = q.hi[0];
                        post[npost][2] = q.lo[2];
                        post[npost][3] = q.hi[2];
                        post[npost][4] = q.hi[1];
                        npost++;
                        cap_src = t->id;
                    }
                }
            } else if (fabs(q.ny) < 0.3 && q.u0 >= 9.5 && q.u1 <= 17.5 && q.v0 >= 8.5 && q.v1 <= 23.5 && q.lo[1] > foot + 1.0) {
                /* a short post's side: its light kept for the new side that looks the same way
                 * (0 south, 1 east, 2 north, 3 west) */
                double e1[3], e2[3], n[3];
                int a, d;

                for (a = 0; a < 3; a++) {
                    e1[a] = (double)t->p[1][a] - t->p[0][a];
                    e2[a] = (double)t->p[2][a] - t->p[0][a];
                }
                rm_cross(e1, e2, n);
                (void)rm_norm(n);
                d = fabs(n[2]) > fabs(n[0]) ? (n[2] > 0.0 ? 0 : 2) : (n[0] > 0.0 ? 1 : 3);
                for (a = 0; a < 3; a++) {
                    side_sum[d][a] += t->rgb[a];
                }
                side_n[d]++;
                kind[j] = 3;
            }
        }
        if (npost == 0) {
            continue;
        }
        for (k = 0; k < 4; k++) {
            if (side_n[k] > 0) {
                int a;

                for (a = 0; a < 3; a++) {
                    side_rgb[k][a] = (unsigned char)(side_sum[k][a] / (double)side_n[k]);
                }
                have_side[k] = 1;
            }
        }
        /* (a side the map never drew, the north one: the darkest of the sides it has, a little darker) */
        for (k = 0; k < 4; k++) {
            if (!have_side[k]) {
                int best = -1, d;

                for (d = 0; d < 4; d++) {
                    if (have_side[d] && (best < 0 || side_rgb[d][0] + side_rgb[d][1] + side_rgb[d][2]
                                                     < side_rgb[best][0] + side_rgb[best][1] + side_rgb[best][2])) {
                        best = d;
                    }
                }
                if (best >= 0) {
                    bw_shade(side_rgb[best], 0.85, side_rgb[k]);
                } else {
                    side_rgb[k][0] = side_rgb[k][1] = side_rgb[k][2] = 160;
                }
            }
        }
        /* the short posts: their sides out, four new ones from the cap to the ground */
        for (j = 0; j < B->n; j++) {
            if (kind[j] == 3) {
                rm_drop(out, B->tri[j].id);
            }
        }
        for (k = 0; k < npost; k++) {
            const double x0 = post[k][0], x1 = post[k][1], z0 = post[k][2], z1 = post[k][3], top = post[k][4];
            /* (each side seen from outside, its left corner first: south, east, north, west) */
            const double c[4][2][2] = { { { x0, z1 }, { x1, z1 } }, { { x1, z1 }, { x1, z0 } },
                                        { { x1, z0 }, { x0, z0 } }, { { x0, z0 }, { x0, z1 } } };
            int d;

            if (top - foot < 1.0) {
                continue;
            }
            for (d = 0; d < 4; d++) {
                const double p[4][3] = { { c[d][0][0], top, c[d][0][1] }, { c[d][1][0], top, c[d][1][1] },
                                         { c[d][1][0], foot, c[d][1][1] }, { c[d][0][0], foot, c[d][0][1] } };
                const double uv[4][2] = { { pu0, pv0 }, { pu1, pv0 }, { pu1, pv1 }, { pu0, pv1 } };

                bw_quad(out, cap_src, side_rgb[d], p, uv);
            }
        }
        /* the planks: a side under each open edge of the walk's level faces, and an underside */
        if (thick > 0.0) {
            for (j = 0; j < B->n; j++) {
                const RmTri *t = &B->tri[j];
                unsigned char rgb[3];
                int a;

                if (kind[j] != 1 && kind[j] != 2) {
                    continue;
                }
                for (a = 0; a < 3; a++) {
                    const float *pa = t->p[a], *pb = t->p[(a + 1) % 3];
                    int shared = 0;
                    unsigned m;

                    for (m = 0; m < B->n && !shared; m++) {
                        int e;

                        if (m == j || (kind[m] != 1 && kind[m] != 2)) {
                            continue;
                        }
                        for (e = 0; e < 3; e++) {
                            const float *qa = B->tri[m].p[e], *qb = B->tri[m].p[(e + 1) % 3];

                            if ((bw_same(pa, qa) && bw_same(pb, qb)) || (bw_same(pa, qb) && bw_same(pb, qa))) {
                                shared = 1;
                                break;
                            }
                        }
                    }
                    if (!shared) {
                        const double p[4][3] = { { pa[0], pa[1], pa[2] }, { pb[0], pb[1], pb[2] },
                                                 { pb[0], pb[1] - thick, pb[2] }, { pa[0], pa[1] - thick, pa[2] } };
                        const double uv[4][2] = { { t->uv[a][0], t->uv[a][1] }, { t->uv[(a + 1) % 3][0], t->uv[(a + 1) % 3][1] },
                                                  { t->uv[(a + 1) % 3][0], t->uv[(a + 1) % 3][1] }, { t->uv[a][0], t->uv[a][1] } };

                        bw_shade(t->rgb, 0.8, rgb);
                        bw_quad(out, t->id, rgb, p, uv);
                    }
                }
                if (kind[j] == 1) {
                    const double pa[3] = { t->p[0][0], t->p[0][1] - thick, t->p[0][2] },
                                 pb[3] = { t->p[1][0], t->p[1][1] - thick, t->p[1][2] },
                                 pc[3] = { t->p[2][0], t->p[2][1] - thick, t->p[2][2] };
                    const double ua[2] = { t->uv[0][0], t->uv[0][1] }, ub[2] = { t->uv[1][0], t->uv[1][1] },
                                 uc[2] = { t->uv[2][0], t->uv[2][1] };

                    bw_shade(t->rgb, 0.6, rgb);
                    rm_tri(out, t->id, -1, 0, rgb, pa, pc, pb, ua, uc, ub, 0, 0);
                }
            }
        }
    }
}
