/*
 * engine/world/remade/building.c -- a building without a back.  The map's
 * houses have a front, two sides and a roof; the wall away from the field's
 * camera, and often the roof's far slope, are not in the model.
 *
 * A building's back is made, never copied from its front: the front has the
 * door.  What is made:
 *   1. The roof's far slope, where it is missing: a slope that looks south
 *      gets its twin on the other side of the ridge (a roof is alike on
 *      both sides of its ridge), unless a slope is there.
 *   2. The back wall: under every edge where the model ends at its north
 *      side (the roof's far eave, a gable's slope, a flat roof's rim) a
 *      wall goes down to the ground, where no wall stands.  It follows that
 *      edge in plan, so a round or an L-shaped house gets a back of its own
 *      shape, set in from the eave as far as the front wall is set in from
 *      the front eave, and it reaches up to the roof.
 *   The wall wears the building's own front, seen square on, without what
 *   belongs to the entrance: `erase` names those places for each building
 *   (a door, its posts, a sign), and each row there is plain wall from the
 *   front's own plain place `plain`.  Windows stay: a back has windows.
 *   Its inner side is dark, as a house is behind an open door.
 *
 * Settings: roof=0 (no roof twin), wall=0 (no back wall), shade (0.85: the
 * back wall's shade of the front's colours: it stands in the shade),
 * erase=X0:X1:Y0:Y1+.. and plain=X (map units of the model's own space),
 * foot=Y (where the wall ends at the bottom: a house on posts), src=east
 * with span=Z0:Z1 (the picture is the east side's, that stretch of it laid
 * across the back: a tower's back is its side), lean=D (the wall's foot
 * stands D out, as the sides' feet do), flare=D (and D wider at each end),
 * twosided=1 (every upright wall from both sides: an open basin), small=0
 * (no closing of small pieces: chimneys, posts).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    double p[3][3], n[3];
    float uv[3][2];
    unsigned id;
    unsigned char rgb[3];
    const RmBatch *bt;
    int added;                  /* a twin made here, not the model's own */
} Tr;

typedef struct { int a[3], b[3]; int owner, count, third; } Edge;

static void tri_normal(Tr *t)
{
    double e1[3], e2[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = t->p[1][k] - t->p[0][k];
        e2[k] = t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, t->n);
    rm_norm(t->n);
}

/* Where the ray from o along d (a unit vector) meets the triangle: the distance, or -1. */
static double ray_hit(const Tr *t, const double o[3], const double d[3])
{
    double e1[3], e2[3], h[3], s[3], q[3], a, f, u, v, dist;
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = t->p[1][k] - t->p[0][k];
        e2[k] = t->p[2][k] - t->p[0][k];
        s[k] = o[k] - t->p[0][k];
    }
    rm_cross(d, e2, h);
    a = rm_dot(e1, h);
    if (fabs(a) < 1e-9) {
        return -1.0;
    }
    f = 1.0 / a;
    u = f * rm_dot(s, h);
    if (u < -1e-6 || u > 1.0 + 1e-6) {
        return -1.0;
    }
    rm_cross(s, e1, q);
    v = f * rm_dot(d, q);
    if (v < -1e-6 || u + v > 1.0 + 1e-6) {
        return -1.0;
    }
    dist = f * rm_dot(e2, q);
    return dist > 1e-6 ? dist : -1.0;
}

/* Does a triangle that looks the way of `n` cover the point c (lying in its plane, to a quarter unit)? */
static int covered(const Tr *tr, unsigned nt, const double c[3], const double n[3])
{
    unsigned j;
    int x;

    for (j = 0; j < nt; j++) {
        double f1[3], f2[3], d[3], den, w1, w2;

        if (rm_dot(n, tr[j].n) <= 0.9) {
            continue;
        }
        for (x = 0; x < 3; x++) {
            f1[x] = tr[j].p[1][x] - tr[j].p[0][x];
            f2[x] = tr[j].p[2][x] - tr[j].p[0][x];
            d[x] = c[x] - tr[j].p[0][x];
        }
        if (fabs(rm_dot(d, tr[j].n)) >= 0.6) {
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

/* A wall's pixel at the point (x, y, z) on triangle t: its texture's pixel there (by the corners' weights) in
 * the triangle's own colour, or the colour alone without a texture.  0 at a hole of a cut-out picture. */
static int wall_px(const Tr *t, const RmTex *tex, unsigned ntex, double x, double y, double z, unsigned char *out4)
{
    const RmTex *st = t->bt->tex >= 0 && (unsigned)t->bt->tex < ntex && tex[t->bt->tex].rgba != NULL ? &tex[t->bt->tex] : NULL;
    double f1[3], f2[3], dd[3], hp[3] = { x, y, z }, den, w1, w2, su, sv;
    const unsigned char *sp;
    int sx, sy, k;

    if (st == NULL) {
        if (out4 != NULL) {
            out4[0] = t->rgb[0]; out4[1] = t->rgb[1]; out4[2] = t->rgb[2]; out4[3] = 255;
        }
        return 1;
    }
    for (k = 0; k < 3; k++) {
        f1[k] = t->p[1][k] - t->p[0][k];
        f2[k] = t->p[2][k] - t->p[0][k];
        dd[k] = hp[k] - t->p[0][k];
    }
    den = rm_dot(f1, f1) * rm_dot(f2, f2) - rm_dot(f1, f2) * rm_dot(f1, f2);
    if (fabs(den) < 1e-12) {
        return 0;
    }
    w1 = (rm_dot(dd, f1) * rm_dot(f2, f2) - rm_dot(dd, f2) * rm_dot(f1, f2)) / den;
    w2 = (rm_dot(dd, f2) * rm_dot(f1, f1) - rm_dot(dd, f1) * rm_dot(f1, f2)) / den;
    su = t->uv[0][0] + w1 * ((double)t->uv[1][0] - t->uv[0][0]) + w2 * ((double)t->uv[2][0] - t->uv[0][0]);
    sv = t->uv[0][1] + w1 * ((double)t->uv[1][1] - t->uv[0][1]) + w2 * ((double)t->uv[2][1] - t->uv[0][1]);
    sx = (int)floor(su * st->w);
    sy = (int)floor(sv * st->h);
    sx = ((sx % st->w) + st->w) % st->w;
    sy = ((sy % st->h) + st->h) % st->h;
    sp = st->rgba + ((size_t)sy * st->w + (size_t)sx) * 4u;
    if (sp[3] < 128) {
        return 0;
    }
    if (out4 != NULL) {
        for (k = 0; k < 3; k++) {
            out4[k] = (unsigned char)((unsigned)sp[k] * t->rgb[k] / 255u);
        }
        out4[3] = 255;
    }
    return 1;
}

static int dbl_cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return x < y ? -1 : x > y;
}

void rm_building(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double shade = rm_optf(opt, "shade", 0.85);
    const int do_roof = rm_optf(opt, "roof", 1) != 0.0, do_wall = rm_optf(opt, "wall", 1) != 0.0, do_small = rm_optf(opt, "small", 1) != 0.0;
    unsigned bi, i, j, n = 0, n0, ne = 0, nq = 0;
    Tr *tr;
    Edge *ed = NULL;
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, y0, over = 0.0, wx0 = 1e30, wx1 = -1e30, wzn = 1e30;
    double (*quad)[4][3] = NULL;
    int fitted = 0;
    unsigned char *qside = NULL;
    double *minz = NULL;
    int k, nbin;

    for (bi = 0; bi < nb; bi++) {
        n += b[bi].bb ? 0u : b[bi].n;
    }
    if (n == 0) {
        return;
    }
    tr = (Tr *)calloc((size_t)n * 3u, sizeof *tr);
    if (tr == NULL) {
        out->fail = 1;
        return;
    }
    for (bi = 0, n = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];

            for (j = 0; j < 3; j++) {
                for (k = 0; k < 3; k++) {
                    tr[n].p[j][k] = t->p[j][k];
                    lo[k] = t->p[j][k] < lo[k] ? t->p[j][k] : lo[k];
                    hi[k] = t->p[j][k] > hi[k] ? t->p[j][k] : hi[k];
                }
            }
            memcpy(tr[n].uv, t->uv, sizeof tr[n].uv);
            tr[n].id = t->id;
            memcpy(tr[n].rgb, t->rgb, 3u);
            tr[n].bt = &b[bi];
            tri_normal(&tr[n]);
            n++;
        }
    }
    n0 = n;
    y0 = lo[1];
    /* a flat board that stands or leans by itself (a lid against a kiln, a sign): shown from both sides */
    {
        int *par = (int *)malloc((size_t)n0 * sizeof *par);

        for (i = 0; par != NULL && i < n0; i++) {
            par[i] = (int)i;
        }
        for (i = 0; par != NULL && i < n0; i++) {
            for (j = 0; j < i; j++) {
                int share = 0, p1, p2, ra, rb;

                for (p1 = 0; p1 < 3 && !share; p1++) {
                    for (p2 = 0; p2 < 3 && !share; p2++) {
                        share = fabs(tr[i].p[p1][0] - tr[j].p[p2][0]) < 0.3 && fabs(tr[i].p[p1][1] - tr[j].p[p2][1]) < 0.3
                                && fabs(tr[i].p[p1][2] - tr[j].p[p2][2]) < 0.3;
                    }
                }
                if (!share) {
                    continue;
                }
                for (ra = (int)i; par[ra] != ra; ra = par[ra]) {
                }
                for (rb = (int)j; par[rb] != rb; rb = par[rb]) {
                }
                par[ra] = rb;
            }
        }
        for (i = 0; par != NULL && i < n0; i++) {
            int root, flat = 1, cnt = 0;

            for (root = (int)i; par[root] != root; root = par[root]) {
            }
            if (root != (int)i) {
                continue;
            }
            for (j = 0; j < n0; j++) {
                int r2;

                for (r2 = (int)j; par[r2] != r2; r2 = par[r2]) {
                }
                if (r2 == root) {
                    cnt++;
                    flat = flat && fabs(rm_dot(tr[j].n, tr[i].n)) > 0.98;
                }
            }
            if (!flat) {
                /* a small piece (a chimney, a post) is closed: a side that looks south gets its twin that looks
                 * north on the piece's other end, in the same pixel and colour, unless something is there */
                double plo[3] = { 1e30, 1e30, 1e30 }, phi[3] = { -1e30, -1e30, -1e30 };

                for (j = 0; j < n0; j++) {
                    int r2, q3;

                    for (r2 = (int)j; par[r2] != r2; r2 = par[r2]) {
                    }
                    for (q3 = 0; r2 == root && q3 < 3; q3++) {
                        for (k = 0; k < 3; k++) {
                            plo[k] = tr[j].p[q3][k] < plo[k] ? tr[j].p[q3][k] : plo[k];
                            phi[k] = tr[j].p[q3][k] > phi[k] ? tr[j].p[q3][k] : phi[k];
                        }
                    }
                }
                if (phi[0] - plo[0] <= 20.0 && phi[2] - plo[2] <= 20.0 && do_small) {
                    for (j = 0; j < n0; j++) {
                        Tr q = tr[j];
                        double c3[3] = { 0, 0, 0 }, t0[2], t1[2], t2[2];
                        int r2, q3;

                        for (r2 = (int)j; par[r2] != r2; r2 = par[r2]) {
                        }
                        if (r2 != root || tr[j].n[2] < 0.7) {
                            continue;
                        }
                        for (q3 = 0; q3 < 3; q3++) {
                            int sidx = q3 == 0 ? 0 : 3 - q3;

                            q.p[q3][0] = tr[j].p[sidx][0];
                            q.p[q3][1] = tr[j].p[sidx][1];
                            q.p[q3][2] = plo[2] + phi[2] - tr[j].p[sidx][2];
                            q.uv[q3][0] = tr[j].uv[sidx][0];
                            q.uv[q3][1] = tr[j].uv[sidx][1];
                            for (k = 0; k < 3; k++) {
                                c3[k] += q.p[q3][k] / 3.0;
                            }
                        }
                        tri_normal(&q);
                        if (covered(tr, n, c3, q.n)) {
                            continue;
                        }
                        t0[0] = q.uv[0][0]; t0[1] = q.uv[0][1];
                        t1[0] = q.uv[1][0]; t1[1] = q.uv[1][1];
                        t2[0] = q.uv[2][0]; t2[1] = q.uv[2][1];
                        {
                            unsigned char rgb2[3] = { (unsigned char)(q.rgb[0] * 0.8), (unsigned char)(q.rgb[1] * 0.8), (unsigned char)(q.rgb[2] * 0.8) };

                            rm_tri(out, q.id, -1, 1, rgb2, q.p[0], q.p[1], q.p[2], t0, t1, t2, 0, 0);
                        }
                        q.added = 1;
                        tr[n++] = q;
                    }
                }
            }
            if (flat && cnt <= 8 && fabs(tr[i].n[1]) < 0.9) {
                for (j = 0; j < n0; j++) {
                    int r2;

                    for (r2 = (int)j; par[r2] != r2; r2 = par[r2]) {
                    }
                    if (r2 == root) {
                        rm_two(out, tr[j].id);
                    }
                }
            }
        }
        free(par);
    }
    /* `twosided=1`: every upright wall is shown from both sides (an open basin, whose walls one looks at from inside) */
    if (rm_optf(opt, "twosided", 0) != 0.0) {
        for (i = 0; i < n0; i++) {
            if (fabs(tr[i].n[1]) < 0.3) {
                rm_two(out, tr[i].id);
            }
        }
    }
    /* 1. the roof's far slope: the twin of each slope that looks south, on the other side of the ridge */
    if (do_roof) {
        double ytop = -1e30, zr = 0.0;
        unsigned cnt = 0;

        for (i = 0; i < n0; i++) {
            if (tr[i].n[1] > 0.25 && tr[i].n[1] < 0.98) {
                for (j = 0; j < 3; j++) {
                    ytop = tr[i].p[j][1] > ytop ? tr[i].p[j][1] : ytop;
                }
            }
        }
        for (i = 0; i < n0; i++) {
            if (tr[i].n[1] > 0.25 && tr[i].n[1] < 0.98) {
                for (j = 0; j < 3; j++) {
                    if (tr[i].p[j][1] >= ytop - 0.25) {
                        zr += tr[i].p[j][2];
                        cnt++;
                    }
                }
            }
        }
        zr = cnt ? zr / cnt : 0.0;
        for (i = 0; cnt && i < n0; i++) {
            Tr q = tr[i];
            double c[3] = { 0, 0, 0 };
            double low = tr[i].p[0][1] < tr[i].p[1][1] ? tr[i].p[0][1] : tr[i].p[1][1];

            low = tr[i].p[2][1] < low ? tr[i].p[2][1] : low;
            if (!(tr[i].n[1] > 0.25 && tr[i].n[1] < 0.98 && tr[i].n[2] > 0.3) || low < y0 + 0.45 * (hi[1] - y0)) {
                continue;               /* (not a slope that looks south, or low on the house: a window's sill) */
            }
            for (j = 0; j < 3; j++) {       /* mirrored through the ridge, corners the other way round */
                unsigned s = j == 0 ? 0u : 3u - j;

                q.p[j][0] = tr[i].p[s][0];
                q.p[j][1] = tr[i].p[s][1];
                q.p[j][2] = 2.0 * zr - tr[i].p[s][2];
                q.uv[j][0] = tr[i].uv[s][0];
                q.uv[j][1] = tr[i].uv[s][1];
            }
            tri_normal(&q);
            for (j = 0; j < 3; j++) {
                for (k = 0; k < 3; k++) {
                    c[k] += q.p[j][k] / 3.0;
                }
            }
            if (c[2] < lo[2] - 1.0 || c[2] > zr || covered(tr, n, c, q.n)) {
                continue;               /* beyond the building, on the same side, or a slope is there */
            }
            {
                double t0[2] = { q.uv[0][0], q.uv[0][1] }, t1[2] = { q.uv[1][0], q.uv[1][1] }, t2[2] = { q.uv[2][0], q.uv[2][1] };

                rm_tri(out, q.id, -1, 1, q.rgb, q.p[0], q.p[1], q.p[2], t0, t1, t2, 0, 0);
            }
            q.added = 1;
            tr[n++] = q;
        }
    }
    if (!do_wall) {
        goto done;
    }
    /* 2. the edges where the model ends: an edge of one triangle only */
    ed = (Edge *)calloc((size_t)n * 3u, sizeof *ed);
    nbin = (int)(hi[0] - lo[0]) + 3;
    minz = (double *)malloc((size_t)nbin * sizeof *minz);
    quad = (double (*)[4][3])malloc((size_t)n * 3u * sizeof *quad);
    qside = (unsigned char *)calloc((size_t)n * 3u, 1);
    if (ed == NULL || minz == NULL || quad == NULL || qside == NULL) {
        out->fail = 1;
        goto done;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < 3; j++) {
            int qa[3], qb[3], sw = 0;
            unsigned e;

            for (k = 0; k < 3; k++) {
                qa[k] = (int)lround(tr[i].p[j][k] * 4.0);
                qb[k] = (int)lround(tr[i].p[(j + 1u) % 3u][k] * 4.0);
            }
            for (k = 0; k < 3 && !sw; k++) {
                if (qa[k] != qb[k]) {
                    sw = qa[k] > qb[k] ? 1 : -1;
                }
            }
            if (sw == 0) {
                continue;
            }
            if (sw > 0) {
                int t3[3];

                memcpy(t3, qa, sizeof t3); memcpy(qa, qb, sizeof qa); memcpy(qb, t3, sizeof qb);
            }
            for (e = 0; e < ne; e++) {
                if (memcmp(ed[e].a, qa, sizeof qa) == 0 && memcmp(ed[e].b, qb, sizeof qb) == 0) {
                    break;
                }
            }
            if (e == ne) {
                memcpy(ed[ne].a, qa, sizeof qa);
                memcpy(ed[ne].b, qb, sizeof qb);
                ed[ne].owner = (int)i;
                ed[ne].third = (int)((j + 2u) % 3u);
                ne++;
            }
            ed[e].count++;
        }
    }
    /* how far the front wall is set in from the front eave */
    {
        double *smp = (double *)malloc((size_t)ne * sizeof *smp + 8u);
        unsigned ns = 0, e;

        for (e = 0; smp != NULL && e < ne; e++) {
            const Tr *t = &tr[ed[e].owner];
            double a[3], bb2[3], c[3], ex, ez, len, ox, oz, o[3], d[3], best = -1.0;

            if (ed[e].count != 1) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                a[k] = ed[e].a[k] / 4.0; bb2[k] = ed[e].b[k] / 4.0; c[k] = t->p[ed[e].third][k];
            }
            ex = bb2[0] - a[0]; ez = bb2[2] - a[2];
            len = sqrt(ex * ex + ez * ez);
            if (len < 0.5 || fabs(bb2[1] - a[1]) > 1.2 * len || (a[1] + bb2[1]) / 2.0 - y0 < 4.0) {
                continue;
            }
            ox = -ez / len; oz = ex / len;
            if (fabs(t->n[1]) < 0.3) {
                if (c[1] < (a[1] + bb2[1]) / 2.0 + 0.25) {
                    continue;           /* (the top of a wall: see below) */
                }
                if (ox * t->n[0] + oz * t->n[2] < 0.0) {
                    ox = -ox; oz = -oz;
                }
            } else if (ox * (c[0] - a[0]) + oz * (c[2] - a[2]) > 0.0) {
                ox = -ox; oz = -oz;
            }
            if (oz < 0.7) {
                continue;               /* (the front's edges only) */
            }
            o[0] = (a[0] + bb2[0]) / 2.0 + ox * 3.0; o[1] = (y0 + (a[1] + bb2[1]) / 2.0) / 2.0; o[2] = (a[2] + bb2[2]) / 2.0 + oz * 3.0;
            d[0] = -ox; d[1] = 0.0; d[2] = -oz;
            for (i = 0; i < n0; i++) {
                double hd = tr[i].n[0] * ox + tr[i].n[2] * oz > 0.3 ? ray_hit(&tr[i], o, d) : -1.0;

                if (hd > 0.0 && (best < 0.0 || hd < best)) {
                    best = hd;
                }
            }
            if (best >= 2.5 && best <= 11.0) {
                smp[ns++] = best - 3.0;
            }
        }
        if (ns > 0) {
            qsort(smp, ns, sizeof *smp, dbl_cmp);
            over = smp[ns / 2u];
            over = over < 0.0 ? 0.0 : over;
        }
        free(smp);
    }
    /* the edges at the north side, high over the ground, with no wall under them: the northmost at each place */
    for (k = 0; k < nbin; k++) {
        minz[k] = 1e30;
    }
    /* where the side walls stand: the big upright triangles that look east and west */
    for (i = 0; i < n0; i++) {
        double e1[3], e2[3], c3[3];

        if (fabs(tr[i].n[0]) < 0.7 || fabs(tr[i].n[1]) > 0.3) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            e1[k] = tr[i].p[1][k] - tr[i].p[0][k];
            e2[k] = tr[i].p[2][k] - tr[i].p[0][k];
        }
        rm_cross(e1, e2, c3);
        if (sqrt(rm_dot(c3, c3)) < 0.02 * (hi[1] - y0) * (hi[2] - lo[2])) {
            continue;                   /* (a small one: a chimney's side, a post's) */
        }
        for (j = 0; j < 3u; j++) {
            wx0 = tr[i].p[j][0] < wx0 ? tr[i].p[j][0] : wx0;
            wx1 = tr[i].p[j][0] > wx1 ? tr[i].p[j][0] : wx1;
            wzn = tr[i].p[j][2] < wzn ? tr[i].p[j][2] : wzn;
        }
    }
    {
        unsigned e, pass;

        for (pass = 0; pass < 2u; pass++) {
            for (e = 0; e < ne; e++) {
                const Tr *t = &tr[ed[e].owner];
                double a[3], bb2[3], c[3], ex, ez, len, ox, oz, o[3], d[3], mid[3];
                int wall = 0, side;

                if (ed[e].count != 1) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    a[k] = ed[e].a[k] / 4.0; bb2[k] = ed[e].b[k] / 4.0; c[k] = t->p[ed[e].third][k];
                    mid[k] = (a[k] + bb2[k]) / 2.0;
                }
                ex = bb2[0] - a[0]; ez = bb2[2] - a[2];
                len = sqrt(ex * ex + ez * ez);
                if (len < 0.5 || fabs(bb2[1] - a[1]) > 1.2 * len || mid[1] - y0 < 4.0) {
                    continue;           /* (short, steep, or at the ground) */
                }
                ox = -ez / len; oz = ex / len;
                if (fabs(t->n[1]) < 0.3) {
                    /* an upright triangle: only its lower edge (an eave's board; a wall's top has the wall under
                     * it), and the wall under it looks the way the board looks */
                    if (c[1] < mid[1] + 0.25) {
                        continue;
                    }
                    if (ox * t->n[0] + oz * t->n[2] < 0.0) {
                        ox = -ox; oz = -oz;
                    }
                } else if (ox * (c[0] - a[0]) + oz * (c[2] - a[2]) > 0.0) {
                    ox = -ox; oz = -oz;
                }
                if (oz > 0.35) {
                    continue;           /* (the front has its wall) */
                }
                side = oz > -0.35;      /* looks east or west: a side wall */
                /* an edge over the roof (a chimney's foot) is no place for a wall: is a roof under the air beside it? */
                {
                    double po[3] = { mid[0] + ox * 2.0, mid[1] + 0.5, mid[2] + oz * 2.0 }, dn[3] = { 0.0, -1.0, 0.0 };
                    int roof = 0;

                    for (i = 0; i < n && !roof; i++) {
                        double hd = tr[i].n[1] > 0.25 ? ray_hit(&tr[i], po, dn) : -1.0;

                        roof = hd > 0.0 && hd < 0.6 * (mid[1] + 0.5 - y0);   /* (not the building's own base slab on the ground) */
                    }
                    if (roof) {
                        continue;
                    }
                }
                if (pass == 0) {
                    continue;
                }
                /* an edge inside the model (a courtyard's rim) is no place for a wall: does something stand close
                 * outside it? */
                {
                    double po[3] = { mid[0], mid[1] - 0.5, mid[2] }, dd[3] = { ox, 0.0, oz };
                    int near = 0;

                    for (i = 0; i < n0 && !near; i++) {
                        double hd = ray_hit(&tr[i], po, dd);

                        near = hd > 0.5 && hd < 12.0;
                    }
                    /* (and half way down: the far wall of a basin or a yard the edge looks into) */
                    po[1] = (y0 + mid[1]) / 2.0;
                    for (i = 0; i < n0 && !near; i++) {
                        double hd = ray_hit(&tr[i], po, dd);

                        near = hd > 0.5 && hd < 120.0;
                    }
                    if (near) {
                        continue;
                    }
                }
                /* does a wall stand under it already? */
                o[0] = mid[0] + ox * 3.0; o[1] = (y0 + mid[1]) / 2.0; o[2] = mid[2] + oz * 3.0;
                d[0] = -ox; d[1] = 0.0; d[2] = -oz;
                for (i = 0; i < n0 && !wall; i++) {
                    double hd = tr[i].n[0] * ox + tr[i].n[2] * oz > 0.3 ? ray_hit(&tr[i], o, d) : -1.0;

                    wall = hd > 0.0 && hd < over + 11.0;
                }
                if (wall) {
                    continue;
                }
                /* the wall: set in from the edge, up to the roof over it, down to the ground */
                for (j = 0; j < 2u; j++) {
                    const double *src = j == 0 ? a : bb2;
                    /* (a back wall is set in straight south, so two pieces of it share their corners exactly
                     * and no crack opens between them; a side wall along its own way in) */
                    double top[3] = { side ? src[0] - ox * over : src[0], src[1], side ? src[2] - oz * over : src[2] + over };
                    double upd[3] = { 0.0, 1.0, 0.0 }, ro[3], best = -1.0;

                    if (over > 0.0) {
                        ro[0] = top[0]; ro[1] = top[1] - 0.2; ro[2] = top[2];
                        for (i = 0; i < n; i++) {
                            double hd = tr[i].n[1] > 0.1 || tr[i].n[1] < -0.1 ? ray_hit(&tr[i], ro, upd) : -1.0;

                            if (hd > 0.0 && hd < 12.0 && (best < 0.0 || hd < best)) {
                                best = hd;
                            }
                        }
                        if (best > 0.0) {
                            top[1] = ro[1] + best;
                        }
                    }
                    memcpy(quad[nq][j == 0 ? 0 : 1], top, sizeof top);
                    top[1] = y0;
                    memcpy(quad[nq][j == 0 ? 3 : 2], top, sizeof top);
                }
                /* a back wall ends where the side walls stand (a roof's edge runs on past them) */
                if (!side && wx1 > wx0) {
                    double x0 = quad[nq][0][0], x1 = quad[nq][1][0];
                    int skip = 0;

                    for (j = 0; j < 2u && !skip; j++) {
                        double xv = quad[nq][j][0], xo = quad[nq][1u - j][0], cl = xv < wx0 ? wx0 : xv > wx1 ? wx1 : xv, f;

                        if (cl == xv) {
                            continue;
                        }
                        if ((xo <= wx0 && xv <= wx0) || (xo >= wx1 && xv >= wx1) || fabs(xo - xv) < 1e-9) {
                            skip = 1;
                            break;
                        }
                        f = (cl - xo) / (xv - xo);
                        for (k = 0; k < 3; k++) {
                            quad[nq][j][k] = quad[nq][1u - j][k] + (quad[nq][j][k] - quad[nq][1u - j][k]) * f;
                            quad[nq][3u - j][k] = k == 1 ? quad[nq][3u - j][k]
                                                  : quad[nq][2u + j][k] + (quad[nq][3u - j][k] - quad[nq][2u + j][k]) * f;
                        }
                    }
                    (void)x0;
                    (void)x1;
                    if (skip) {
                        continue;
                    }
                }
                /* (kept facing outward: its corners counter-clockwise seen from outside) */
                {
                    double e1[3], e2[3], nn[3];

                    for (k = 0; k < 3; k++) {
                        e1[k] = quad[nq][1][k] - quad[nq][0][k];
                        e2[k] = quad[nq][2][k] - quad[nq][0][k];
                    }
                    rm_cross(e1, e2, nn);
                    if (nn[0] * ox + nn[2] * oz < 0.0) {
                        double t3[3];

                        memcpy(t3, quad[nq][0], sizeof t3); memcpy(quad[nq][0], quad[nq][1], sizeof t3); memcpy(quad[nq][1], t3, sizeof t3);
                        memcpy(t3, quad[nq][3], sizeof t3); memcpy(quad[nq][3], quad[nq][2], sizeof t3); memcpy(quad[nq][2], t3, sizeof t3);
                    }
                }
                qside[nq] = (unsigned char)side;
                nq++;
            }
        }
    }
    /* `fit=1`: the back is one wall from the west side wall's north edge to the east side wall's (a kiln, whose
     * sides lean: a wall under the roof's edge met neither side) */
    if (nq > 0 && rm_optf(opt, "fit", 0) != 0.0) {
        double c[2][2][3], ytop = -1e30, cxm = (lo[0] + hi[0]) / 2.0;      /* [west, east][foot, top] */
        int have[2][2] = { { 0, 0 }, { 0, 0 } }, sd, lv;
/* a side wall of the body: looks east or west and reaches the ground (a chimney's side does not) */
#define BIG_SIDE(i_) (fabs(tr[i_].n[0]) > 0.5 && fabs(tr[i_].n[1]) < 0.45 && \
                      (tr[i_].p[0][1] < y0 + 1.0 || tr[i_].p[1][1] < y0 + 1.0 || tr[i_].p[2][1] < y0 + 1.0))

        for (i = 0; i < n0; i++) {
            if (BIG_SIDE(i)) {
                for (j = 0; j < 3u; j++) {
                    ytop = tr[i].p[j][1] > ytop ? tr[i].p[j][1] : ytop;
                }
            }
        }
        for (i = 0; i < n0; i++) {
            if (!BIG_SIDE(i)) {
                continue;
            }
            for (j = 0; j < 3u; j++) {
                const double *v = tr[i].p[j];

                sd = v[0] > cxm;
                lv = v[1] > ytop - 1.0 ? 1 : v[1] < y0 + 1.0 ? 0 : -1;
                if (lv >= 0 && (!have[sd][lv] || v[2] < c[sd][lv][2])) {
                    memcpy(c[sd][lv], v, sizeof c[sd][lv]);
                    have[sd][lv] = 1;
                }
            }
        }
        if (have[0][0] && have[0][1] && have[1][0] && have[1][1]) {
            /* (corners counter-clockwise seen from the north: outside) */
            memcpy(quad[0][0], c[1][1], sizeof quad[0][0]);
            memcpy(quad[0][1], c[0][1], sizeof quad[0][1]);
            memcpy(quad[0][2], c[0][0], sizeof quad[0][2]);
            memcpy(quad[0][3], c[1][0], sizeof quad[0][3]);
            qside[0] = 0;
            nq = 1;
            fitted = 1;
        }
    }
#undef BIG_SIDE
    /* back wall pieces that stand within 2 units of the northmost are one plane: no slit between them */
    {
        double zn = 1e30;
        unsigned q2;

        for (q2 = 0; q2 < nq; q2++) {
            for (j = 0; !qside[q2] && j < 4u; j++) {
                zn = quad[q2][j][2] < zn ? quad[q2][j][2] : zn;
            }
        }
        if (wzn < 1e29 && fabs(zn - wzn) < 8.0) {
            zn = wzn;                   /* where the side walls end */
        }
        for (q2 = 0; !fitted && q2 < nq; q2++) {
            for (j = 0; !qside[q2] && j < 4u; j++) {
                if (fabs(quad[q2][j][2] - zn) < 8.0) {
                    quad[q2][j][2] = zn;
                }
            }
        }
    }
    /* The back wall's picture: the building's own front as it is seen square on from the south (its walls,
     * not its roof), a pixel for each half map unit; what belongs to the entrance (`erase`) is plain wall, each
     * row taken from the front's plain place `plain` at the same height; above the front wall's top (a gable)
     * the wall's highest row goes on up.  The back wall takes the pixel at its own place (x, y). */
    if (nq > 0 && out->ntex < 8) {
        const double ppu = 2.0;
        char eb[256], *ep;
        char sb[16];
        const int east = rm_opt(opt, "src", sb, sizeof sb) != NULL && strcmp(sb, "east") == 0;
        const int ax = east ? 2 : 0, dp = east ? 0 : 2;     /* the picture's across, and the way it is looked at */
        double er[8][4], plain = rm_optf(opt, "plain", 1e30), xa = lo[ax], xb = hi[ax], yb = hi[1], wz = hi[dp];
        const int flatw = rm_optf(opt, "flat", 0) != 0.0;     /* (`flat=1`: the plain place's column all across: bands, no openings) */
        const double at_most = rm_optf(opt, "at", 1e30);      /* (`at=Z`: the front wall that stands at most there: a model of two huts) */
        double sp0 = xa, sp1 = xb, qx0 = 1e30, qx1 = -1e30, lean = rm_optf(opt, "lean", 0.0), flare = rm_optf(opt, "flare", 0.0);
        unsigned char *px, *ok, white[3], dark[3] = { 0, 0, 0 };
        unsigned q, first = out->ntri, ner = 0, src = tr[0].id;
        int Wm = (int)ceil((xb - xa) * ppu), Hm = (int)ceil((yb - y0) * ppu), x, y, tx = -1;

        if (rm_opt(opt, "erase", eb, sizeof eb) != NULL) {
            for (ep = eb; ep != NULL && *ep != '\0' && ner < 8u; ) {
                if (sscanf(ep, "%lf:%lf:%lf:%lf", &er[ner][0], &er[ner][1], &er[ner][2], &er[ner][3]) == 4) {
                    ner++;
                }
                ep = strchr(ep, '+');
                ep = ep != NULL ? ep + 1 : NULL;
            }
        }
        if (Wm < 2 || Hm < 2 || Wm > 1024 || Hm > 1024) {
            goto done;
        }
        /* the front wall's plane: where the biggest upright triangle that looks south stands */
        {
            double big = 0.0;

            for (i = 0; i < n0; i++) {
                double e1[3], e2[3], c3[3], area;

                if (tr[i].n[dp] < 0.9 || tr[i].p[0][dp] > at_most) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    e1[k] = tr[i].p[1][k] - tr[i].p[0][k];
                    e2[k] = tr[i].p[2][k] - tr[i].p[0][k];
                }
                rm_cross(e1, e2, c3);
                area = sqrt(rm_dot(c3, c3));
                if (area > big) {
                    big = area;
                    wz = tr[i].p[0][dp];
                }
            }
        }
        px = (unsigned char *)calloc((size_t)Wm * Hm, 4);
        ok = (unsigned char *)calloc((size_t)Wm * Hm, 1);
        if (px == NULL || ok == NULL) {
            free(px);
            free(ok);
            out->fail = 1;
            goto done;
        }
        for (y = 0; y < Hm; y++) {
            for (x = 0; x < Wm; x++) {
                double o[3], d[3] = { 0.0, 0.0, 0.0 }, best = -1.0, cz3;
                const Tr *bt2 = NULL;

                o[ax] = xa + (x + 0.5) / ppu;
                o[1] = yb - (y + 0.5) / ppu;
                o[dp] = hi[dp] + 8.0;
                d[dp] = -1.0;
                for (i = 0; i < n0; i++) {
                    double hd;

                    if (tr[i].n[dp] < 0.2 || tr[i].n[1] > 0.25) {
                        continue;           /* (not a wall that looks that way) */
                    }
                    cz3 = (tr[i].p[0][dp] + tr[i].p[1][dp] + tr[i].p[2][dp]) / 3.0;
                    if (cz3 > wz + rm_optf(opt, "near", 1.5) || cz3 < wz - rm_optf(opt, "far", 8.0)) {
                        continue;           /* (the eave's board in front of the wall, a chimney far behind it) */
                    }
                    hd = ray_hit(&tr[i], o, d);
                    if (hd > 0.0 && (best < 0.0 || hd < best)
                        && wall_px(&tr[i], tex, ntex, o[0] + d[0] * hd, o[1], o[2] + d[2] * hd, NULL)) {
                        best = hd;
                        bt2 = &tr[i];
                    }
                }
                if (bt2 != NULL) {
                    wall_px(bt2, tex, ntex, o[0] + d[0] * best, o[1], o[2] + d[2] * best, px + ((size_t)y * Wm + (size_t)x) * 4u);
                    ok[(size_t)y * Wm + (size_t)x] = 1;
                }
            }
        }
        /* the entrance taken out: plain wall, row by row from the plain place */
        if (plain < 1e29) {
            tx = (int)((plain - xa) * ppu);
            tx = tx < 0 ? 0 : tx >= Wm ? Wm - 1 : tx;
        }
        for (q = 0; q < ner && tx >= 0; q++) {
            int x0 = (int)floor((er[q][0] - xa) * ppu), x1 = (int)ceil((er[q][1] - xa) * ppu);
            int y1 = (int)ceil((yb - er[q][2]) * ppu), y0p = (int)floor((yb - er[q][3]) * ppu);

            for (y = y0p < 0 ? 0 : y0p; y < y1 && y < Hm; y++) {
                for (x = x0 < 0 ? 0 : x0; x < x1 && x < Wm; x++) {
                    memcpy(px + ((size_t)y * Wm + (size_t)x) * 4u, px + ((size_t)y * Wm + (size_t)tx) * 4u, 4u);
                    ok[(size_t)y * Wm + (size_t)x] = ok[(size_t)y * Wm + (size_t)tx];
                }
            }
        }
        /* where the front shows no wall (over its top, beside it): the nearest wall pixel under, then beside */
        for (x = 0; x < Wm; x++) {
            int last = -1;

            for (y = Hm - 1; y >= 0; y--) {
                if (ok[(size_t)y * Wm + (size_t)x]) {
                    last = y;
                } else if (last >= 0) {
                    memcpy(px + ((size_t)y * Wm + (size_t)x) * 4u, px + ((size_t)last * Wm + (size_t)x) * 4u, 4u);
                }
            }
            for (y = 0, last = -1; y < Hm; y++) {       /* (under the wall's foot: the lowest row) */
                if (ok[(size_t)y * Wm + (size_t)x]) {
                    last = y;
                } else if (last >= 0 && px[((size_t)y * Wm + (size_t)x) * 4u + 3u] == 0) {
                    memcpy(px + ((size_t)y * Wm + (size_t)x) * 4u, px + ((size_t)last * Wm + (size_t)x) * 4u, 4u);
                }
            }
        }
        for (y = 0; y < Hm; y++) {
            int last = -1;

            for (x = 0; x < Wm; x++) {
                if (px[((size_t)y * Wm + (size_t)x) * 4u + 3u]) {
                    last = x;
                } else if (last >= 0) {
                    memcpy(px + ((size_t)y * Wm + (size_t)x) * 4u, px + ((size_t)y * Wm + (size_t)last) * 4u, 4u);
                }
            }
            for (x = Wm - 1, last = -1; x >= 0; x--) {
                if (px[((size_t)y * Wm + (size_t)x) * 4u + 3u]) {
                    last = x;
                } else if (last >= 0) {
                    memcpy(px + ((size_t)y * Wm + (size_t)x) * 4u, px + ((size_t)y * Wm + (size_t)last) * 4u, 4u);
                }
            }
        }
        for (i = 0; i < (unsigned)(Wm * Hm); i++) {
            px[(size_t)i * 4u + 3u] = 255;
        }
        free(ok);
        for (k = 0; k < 3; k++) {
            white[k] = (unsigned char)(255.0 * shade);
        }
        /* (`span=A:B`, with the east side as the picture: that stretch of the side lies across the back) */
        if (rm_opt(opt, "span", eb, sizeof eb) != NULL) {
            sscanf(eb, "%lf:%lf", &sp0, &sp1);
        }
        for (q = 0; q < nq; q++) {
            for (j = 0; j < 4u; j++) {
                qx0 = quad[q][j][0] < qx0 ? quad[q][j][0] : qx0;
                qx1 = quad[q][j][0] > qx1 ? quad[q][j][0] : qx1;
            }
        }
        for (q = 0; q < nq; q++) {
            double t[4][2], in[4][3], foot = rm_optf(opt, "foot", y0), ux[4];

            for (j = 0; j < 4u; j++) {
                ux[j] = quad[q][j][0];
            }
            for (j = 0; j < 4u; j++) {
                if (j >= 2u && !fitted) {
                    quad[q][j][1] = foot;       /* (a house on posts: its wall ends at its floor) */
                    quad[q][j][2] -= lean;      /* (`lean`: the foot stands out, as the sides' feet do) */
                    if (flare != 0.0 && qx1 > qx0) {
                        quad[q][j][0] = (qx0 + qx1) / 2.0 + (quad[q][j][0] - (qx0 + qx1) / 2.0) * (1.0 + 2.0 * flare / (qx1 - qx0));
                    }
                }
                /* (a side wall runs along the house: every place on it is the front's plain place) */
                if (east) {
                    double zz = qx1 > qx0 ? sp1 + (ux[j] - qx0) / (qx1 - qx0) * (sp0 - sp1) : sp0;

                    t[j][0] = (((qside[q] || flatw) && plain < 1e29 ? plain : zz) - xa) / (xb - xa);
                } else
                t[j][0] = (((qside[q] || flatw) && plain < 1e29 ? plain : ux[j]) - xa) / (xb - xa);
                t[j][1] = (yb - quad[q][j][1]) / (yb - y0);
                in[j][0] = quad[q][j][0];
                in[j][1] = quad[q][j][1];
                in[j][2] = quad[q][j][2];
            }
            {
                /* (the inner side a quarter unit inside: two walls in one plane would flicker) */
                double e1[3], e2[3], nn[3];

                for (k = 0; k < 3; k++) {
                    e1[k] = quad[q][1][k] - quad[q][0][k];
                    e2[k] = quad[q][2][k] - quad[q][0][k];
                }
                rm_cross(e1, e2, nn);
                rm_norm(nn);
                for (j = 0; j < 4u; j++) {
                    for (k = 0; k < 3; k++) {
                        in[j][k] -= nn[k] * 0.0;    /* (in the wall's own plane: the two look opposite ways, one is drawn) */
                    }
                }
            }
            rm_tri(out, src, out->ntex, 1, white, quad[q][0], quad[q][1], quad[q][2], t[0], t[1], t[2], 0, 0);
            rm_tri(out, src, out->ntex, 1, white, quad[q][0], quad[q][2], quad[q][3], t[0], t[2], t[3], 0, 0);
            /* its inner side, dark: through an open door or a window the house is dark inside, as the field shows it */
            rm_tri(out, src, out->ntex, 1, dark, in[0], in[2], in[1], t[0], t[2], t[1], 0, 0);
            rm_tri(out, src, out->ntex, 1, dark, in[0], in[3], in[2], t[0], t[3], t[2], 0, 0);
        }
        if (out->fail || out->ntri == first) {
            free(px);
            goto done;
        }
        out->tex[out->ntex].w = Wm;
        out->tex[out->ntex].h = Hm;
        out->tex[out->ntex].rgba = px;
        out->ntex++;
    }
done:
    if (!out->fail) {
        rm_seal(b, nb, out, 0.7);
    }
    free(tr);
    free(ed);
    free(minz);
    free(quad);
    free(qside);
}
