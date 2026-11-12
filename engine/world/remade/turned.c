/*
 * engine/world/remade/turned.c -- a thing that is alike front and back and
 * has no back: a table (its back edge and its two back legs are not in the
 * model, and a leg has no inner side), a cardboard box (its north side is
 * not).  The field's camera never shows those, so the model leaves them out.
 *
 * Two steps:
 *   1. The back is the front turned half round about the upright through
 *      the thing's middle: every triangle gets a turned copy, lit as the
 *      game lights the model, unless a triangle is already there (a table's
 *      top turned is its top again; its east side turned is its west side).
 *      So a table gets its back edge and its two other legs.
 *   2. Each piece is closed: a side that looks south gets its twin that
 *      looks north on the other end of the same piece (a leg's inner side),
 *      and the other way round, unless something is there.  These are new
 *      triangles in the same pixels.
 *   3. A face its makers painted black is repainted.  The model paints
 *      the sides the field's camera never shows plain black (a table
 *      leg's east and west sides), a cheap shadow.  From a battle camera
 *      that is a black block.  Such a face, and every face made in steps
 *      1 and 2 from one, takes the colour of the brightest face of its
 *      own piece, a little darker by the way it looks (sides, back,
 *      underside), in its own pixels.
 * The middle is the middle of the thing's box, without its flat pieces: a
 * shadow sheet on the floor under it is no part of the thing.
 *
 * The thing rests on the floor (the map holds furniture a few units over a
 * room's floor, and its shadow sheet with it).
 *
 * Right for a thing with two alike halves only.  A building is never made
 * this way: its back is not its front.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    float p[3][3];              /* the triangle's corners                        */
    float uv[3][2];
    unsigned id;                /* the model's triangle it is, or comes from     */
    const RmBatch *bt;
    int piece, flat;
    double pz;                  /* the middle of its piece, north to south       */
    unsigned char rgb[3];       /* its colour as the model paints it             */
    unsigned char ref[3];       /* the brightest colour of its piece             */
    int black;                  /* painted black by the model                    */
} Tr;

static int find(int *parent, int a)
{
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}

/* Two corners at one place (to half a map unit, as the pieces are told apart). */
static int same_pt(const float *a, const float *b)
{
    return lround(a[0] * 2.0) == lround(b[0] * 2.0) && lround(a[1] * 2.0) == lround(b[1] * 2.0) && lround(a[2] * 2.0) == lround(b[2] * 2.0);
}

static void normal_of(const float p[3][3], double n[3])
{
    double e1[3], e2[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = (double)p[1][k] - p[0][k];
        e2[k] = (double)p[2][k] - p[0][k];
    }
    rm_cross(e1, e2, n);
}

/* A triangle in its piece's colour, shaded by the way it looks, in its own pixels. */
static void painted(RmOut *out, const Tr *q)
{
    double c0[3], c1[3], c2[3], t0[2], t1[2], t2[2], n[3], f;
    unsigned char rgb[3];
    int k;

    normal_of(q->p, n);
    rm_norm(n);
    f = n[1] > 0.7 ? 1.0 : n[1] < -0.7 ? 0.55 : n[2] > 0.5 ? 1.0 : n[2] < -0.5 ? 0.78 : 0.86;
    for (k = 0; k < 3; k++) {
        c0[k] = q->p[0][k]; c1[k] = q->p[1][k]; c2[k] = q->p[2][k];
        rgb[k] = (unsigned char)(q->ref[k] * f);
    }
    t0[0] = q->uv[0][0]; t0[1] = q->uv[0][1];
    t1[0] = q->uv[1][0]; t1[1] = q->uv[1][1];
    t2[0] = q->uv[2][0]; t2[1] = q->uv[2][1];
    rm_tri(out, q->id, -1, 1, rgb, c0, c1, c2, t0, t1, t2, 0, 0);
}

/* Is the triangle q a triangle of the list already: the same three corners, or lying on one that faces the
 * same way (a wall's two triangles may be cut along the other diagonal on the other side)? */
static int there(const Tr *tr, unsigned n, const float q[3][3])
{
    double nq[3], c[3];
    unsigned j;
    int x;

    normal_of(q, nq);
    if (rm_norm(nq) <= 1e-6) {
        return 1;                       /* (no area: nothing to add) */
    }
    for (x = 0; x < 3; x++) {
        c[x] = ((double)q[0][x] + q[1][x] + q[2][x]) / 3.0;
    }
    for (j = 0; j < n; j++) {
        double nj[3], f1[3], f2[3], d[3], den, w1, w2;
        int m0 = 0, m1 = 0, m2 = 0, p;

        if (tr[j].flat) {
            continue;
        }
        for (p = 0; p < 3; p++) {
            m0 |= same_pt(q[0], tr[j].p[p]);
            m1 |= same_pt(q[1], tr[j].p[p]);
            m2 |= same_pt(q[2], tr[j].p[p]);
        }
        normal_of(tr[j].p, nj);
        if (rm_norm(nj) <= 1e-6 || rm_dot(nq, nj) <= 0.98) {
            continue;
        }
        if (m0 && m1 && m2) {
            return 1;
        }
        for (x = 0; x < 3; x++) {
            f1[x] = (double)tr[j].p[1][x] - tr[j].p[0][x];
            f2[x] = (double)tr[j].p[2][x] - tr[j].p[0][x];
            d[x] = c[x] - tr[j].p[0][x];
        }
        if (fabs(rm_dot(d, nj)) >= 0.25) {
            continue;
        }
        den = rm_dot(f1, f1) * rm_dot(f2, f2) - rm_dot(f1, f2) * rm_dot(f1, f2);
        if (fabs(den) <= 1e-9) {
            continue;
        }
        w1 = (rm_dot(d, f1) * rm_dot(f2, f2) - rm_dot(d, f2) * rm_dot(f1, f2)) / den;
        w2 = (rm_dot(d, f2) * rm_dot(f1, f1) - rm_dot(d, f1) * rm_dot(f1, f2)) / den;
        if (1.0 - w1 - w2 >= -0.02 && w1 >= -0.02 && w2 >= -0.02) {
            return 1;
        }
    }
    return 0;
}

void rm_turned(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi, i, j, n = 0, n0, n1, k;
    Tr *tr;
    int *parent;
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, cx, cz;
    int any = 0;

    (void)tex;
    (void)ntex;
    for (bi = 0; bi < nb; bi++) {
        n += b[bi].bb ? 0u : b[bi].n;
    }
    if (n == 0) {
        return;
    }
    tr = (Tr *)calloc((size_t)n * 4u, sizeof *tr);      /* the model's, their turned copies, the twins of both */
    parent = (int *)malloc(n * sizeof *parent);
    if (tr == NULL || parent == NULL) {
        free(tr);
        free(parent);
        out->fail = 1;
        return;
    }
    for (bi = 0, n = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            memcpy(tr[n].p, b[bi].tri[i].p, sizeof tr[n].p);
            memcpy(tr[n].uv, b[bi].tri[i].uv, sizeof tr[n].uv);
            tr[n].id = b[bi].tri[i].id;
            tr[n].bt = &b[bi];
            memcpy(tr[n].rgb, b[bi].tri[i].rgb, 3u);
            tr[n].black = tr[n].rgb[0] < 16 && tr[n].rgb[1] < 16 && tr[n].rgb[2] < 16;
            parent[n] = (int)n;
            n++;
        }
    }
    n0 = n;
    /* the pieces: triangles that share a corner */
    for (i = 0; i < n0; i++) {
        for (j = 0; j < i; j++) {
            int share = 0, p, q;

            for (p = 0; p < 3 && !share; p++) {
                for (q = 0; q < 3 && !share; q++) {
                    share = same_pt(tr[i].p[p], tr[j].p[q]);
                }
            }
            if (share) {
                int ra = find(parent, (int)i), rb = find(parent, (int)j);

                if (ra != rb) {
                    parent[ra] = rb;
                }
            }
        }
    }
    /* a flat piece: all its triangles in one plane's direction; a piece's middle */
    for (i = 0; i < n0; i++) {
        tr[i].piece = find(parent, (int)i);
        tr[i].flat = 1;
    }
    for (i = 0; i < n0; i++) {
        double n1v[3], n2v[3], zlo = 1e30, zhi = -1e30;
        int first = -1;

        for (j = 0; j < n0; j++) {
            if (tr[j].piece == tr[i].piece) {
                first = first < 0 ? (int)j : first;
                for (k = 0; k < 3; k++) {
                    zlo = tr[j].p[k][2] < zlo ? tr[j].p[k][2] : zlo;
                    zhi = tr[j].p[k][2] > zhi ? tr[j].p[k][2] : zhi;
                }
            }
        }
        tr[i].pz = (zlo + zhi) / 2.0;
        normal_of(tr[i].p, n1v);
        normal_of(tr[first].p, n2v);
        if (rm_norm(n1v) > 1e-6 && rm_norm(n2v) > 1e-6 && fabs(rm_dot(n1v, n2v)) <= 0.98) {
            for (j = 0; j < n0; j++) {
                if (tr[j].piece == tr[i].piece) {
                    tr[j].flat = 0;
                }
            }
        }
    }
    for (i = 0; i < n0; i++) {
        if (tr[i].flat) {
            continue;
        }
        any = 1;
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) {
                double v = tr[i].p[j][k];

                lo[k] = v < lo[k] ? v : lo[k];
                hi[k] = v > hi[k] ? v : hi[k];
            }
        }
    }
    if (!any) {
        free(tr);
        free(parent);
        return;
    }
    cx = (lo[0] + hi[0]) / 2.0;
    cz = (lo[2] + hi[2]) / 2.0;
    if (rm_optf(opt, "turn", 1) == 0.0) {
        goto cards;                     /* (`turn=0`: only the cards from both sides) */
    }
    /* each piece's brightest colour (a piece painted black all over: the thing's brightest) */
    {
        unsigned char top[3] = { 0, 0, 0 };

        for (i = 0; i < n0; i++) {
            if (!tr[i].flat && !tr[i].black && tr[i].rgb[0] + tr[i].rgb[1] + tr[i].rgb[2] > top[0] + top[1] + top[2]) {
                memcpy(top, tr[i].rgb, 3u);
            }
        }
        for (i = 0; i < n0; i++) {
            int best = -1;

            memcpy(tr[i].ref, top, 3u);
            for (j = 0; j < n0; j++) {
                if (tr[j].piece == tr[i].piece && !tr[j].black && tr[j].rgb[0] + tr[j].rgb[1] + tr[j].rgb[2] > best) {
                    best = tr[j].rgb[0] + tr[j].rgb[1] + tr[j].rgb[2];
                    memcpy(tr[i].ref, tr[j].rgb, 3u);
                }
            }
        }
    }
    /* 3. (first, for the model's own triangles) the faces painted black, repainted */
    for (i = 0; i < n0; i++) {
        if (!tr[i].flat && tr[i].black) {
            rm_drop(out, tr[i].id);
            painted(out, &tr[i]);
        }
    }
    /* 1. the turned copies */
    for (i = 0; i < n0; i++) {
        Tr q = tr[i];

        if (tr[i].flat) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            q.p[j][0] = (float)(2.0 * cx - tr[i].p[j][0]);
            q.p[j][2] = (float)(2.0 * cz - tr[i].p[j][2]);
        }
        q.pz = 2.0 * cz - tr[i].pz;
        if (!there(tr, n, q.p)) {
            if (q.black) {
                painted(out, &q);
            } else {
                rm_turn(out, tr[i].id, cx, cz);
            }
            tr[n++] = q;
        }
    }
    n1 = n;
    /* 2. each piece closed: a side's twin on the other end of its piece, looking the other way */
    for (i = 0; i < n1; i++) {
        Tr q = tr[i];
        double nv[3];

        if (tr[i].flat) {
            continue;
        }
        normal_of(tr[i].p, nv);
        if (rm_norm(nv) <= 1e-6 || fabs(nv[2]) < 0.7) {
            continue;                   /* (only the sides that look south or north) */
        }
        /* mirrored through the piece's middle, corners in the other order so it looks outward */
        for (j = 0; j < 3; j++) {
            unsigned s = j == 0 ? 0u : 3u - j;

            q.p[j][0] = tr[i].p[s][0];
            q.p[j][1] = tr[i].p[s][1];
            q.p[j][2] = (float)(2.0 * tr[i].pz - tr[i].p[s][2]);
            q.uv[j][0] = tr[i].uv[s][0];
            q.uv[j][1] = tr[i].uv[s][1];
        }
        if (!there(tr, n, q.p)) {
            painted(out, &q);
            tr[n++] = q;
        }
    }
cards:
    /* an upright flat piece (a plant's leaves drawn on a card) is shown from both sides */
    for (i = 0; i < n0; i++) {
        double nv[3];

        normal_of(tr[i].p, nv);
        if (tr[i].flat && rm_norm(nv) > 1e-6 && fabs(nv[1]) < 0.9) {
            rm_two(out, tr[i].id);
        }
    }
    if (rm_optf(opt, "rest", 1) != 0.0) {
        out->flags |= RM_FLAG_REST;
    }
    free(tr);
    free(parent);
}
