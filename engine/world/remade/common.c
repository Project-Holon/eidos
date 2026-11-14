/*
 * engine/world/remade/common.c -- what the builders of remade models share
 * (remade.h): the output lists, settings, the plane of a flat piece.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

void rm_drop(RmOut *o, unsigned id)
{
    if (o->ndrop == o->cdrop) {
        unsigned cap = o->cdrop ? o->cdrop * 2u : 16u;
        unsigned *nw = (unsigned *)realloc(o->drop, cap * sizeof *nw);

        if (nw == NULL) {
            o->fail = 1;
            return;
        }
        o->drop = nw;
        o->cdrop = cap;
    }
    o->drop[o->ndrop++] = id;
}

void rm_two(RmOut *o, unsigned id)
{
    if (o->ntwo == o->ctwo) {
        unsigned cap = o->ctwo ? o->ctwo * 2u : 16u;
        unsigned *nw = (unsigned *)realloc(o->two, cap * sizeof *nw);

        if (nw == NULL) {
            o->fail = 1;
            return;
        }
        o->two = nw;
        o->ctwo = cap;
    }
    o->two[o->ntwo++] = id;
}

void rm_turn(RmOut *o, unsigned id, double cx, double cz)
{
    if (o->nturn == o->cturn) {
        unsigned cap = o->cturn ? o->cturn * 2u : 16u;
        RmTurn *nw = (RmTurn *)realloc(o->turn, cap * sizeof *nw);

        if (nw == NULL) {
            o->fail = 1;
            return;
        }
        o->turn = nw;
        o->cturn = cap;
    }
    o->turn[o->nturn].id = id;
    o->turn[o->nturn].cx = (float)cx;
    o->turn[o->nturn].cz = (float)cz;
    o->nturn++;
}

void rm_drop_all(RmOut *o, const RmBatch *b)
{
    unsigned i;

    for (i = 0; i < b->n; i++) {
        rm_drop(o, b->tri[i].id);
    }
}

void rm_tri(RmOut *o, unsigned src, int tex, int cull, const unsigned char rgb[3], const double a[3],
            const double b[3], const double c[3], const double uva[2], const double uvb[2], const double uvc[2],
            int kind, int alpha)
{
    RmNew *t;
    float e1[3], e2[3], n[3], len;
    int k;

    if (o->ntri == o->ctri) {
        unsigned cap = o->ctri ? o->ctri * 2u : 256u;
        RmNew *nw = (RmNew *)realloc(o->tri, cap * sizeof *nw);

        if (nw == NULL) {
            o->fail = 1;
            return;
        }
        o->tri = nw;
        o->ctri = cap;
    }
    t = &o->tri[o->ntri];
    memset(t, 0, sizeof *t);
    t->src = src;
    t->tex = tex;
    t->cull = (unsigned char)cull;
    t->rgb[0] = rgb[0]; t->rgb[1] = rgb[1]; t->rgb[2] = rgb[2];
    t->kind = (unsigned char)kind;
    t->alpha = (unsigned char)alpha;
    for (k = 0; k < 3; k++) {
        t->p[0][k] = (float)a[k];
        t->p[1][k] = (float)b[k];
        t->p[2][k] = (float)c[k];
    }
    t->uv[0][0] = (float)uva[0]; t->uv[0][1] = (float)uva[1];
    t->uv[1][0] = (float)uvb[0]; t->uv[1][1] = (float)uvb[1];
    t->uv[2][0] = (float)uvc[0]; t->uv[2][1] = (float)uvc[1];
    for (k = 0; k < 3; k++) {
        e1[k] = t->p[1][k] - t->p[0][k];
        e2[k] = t->p[2][k] - t->p[0][k];
    }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-9f) {
        len = 1e-9f;
    }
    t->n[0] = n[0] / len; t->n[1] = n[1] / len; t->n[2] = n[2] / len;
    o->ntri++;
}

const char *rm_opt(const char *opt, const char *name, char *buf, size_t n)
{
    size_t ln = strlen(name);
    const char *p = opt;

    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, ',');
        size_t len = e != NULL ? (size_t)(e - p) : strlen(p);

        if (len > ln && strncmp(p, name, ln) == 0 && p[ln] == '=') {
            size_t vl = len - ln - 1u;

            if (vl >= n) {
                vl = n - 1u;
            }
            memcpy(buf, p + ln + 1u, vl);
            buf[vl] = '\0';
            return buf;
        }
        p = e != NULL ? e + 1 : NULL;
    }
    return NULL;
}

double rm_optf(const char *opt, const char *name, double def)
{
    char b[48];

    return rm_opt(opt, name, b, sizeof b) != NULL ? atof(b) : def;
}

unsigned rm_colours(const char *list, unsigned *out, unsigned max)
{
    unsigned n = 0;

    while (list != NULL && *list != '\0' && n < max) {
        char *end;
        unsigned long v = strtoul(list, &end, 16);

        if (end == list) {
            break;
        }
        out[n++] = (unsigned)v;
        list = *end == '+' ? end + 1 : end;
        if (*end != '+') {
            break;
        }
    }
    return n;
}

int rm_has_colour(const unsigned *list, unsigned n, const unsigned char *px)
{
    unsigned c = ((unsigned)px[0] << 16) | ((unsigned)px[1] << 8) | px[2], i;

    for (i = 0; i < n; i++) {
        if (list[i] == c) {
            return 1;
        }
    }
    return 0;
}

int rm_plane(const RmTri *t, double m[3][3])
{
    double u1 = (double)t->uv[1][0] - t->uv[0][0], v1 = (double)t->uv[1][1] - t->uv[0][1];
    double u2 = (double)t->uv[2][0] - t->uv[0][0], v2 = (double)t->uv[2][1] - t->uv[0][1];
    double det = u1 * v2 - u2 * v1;
    int k;

    if (fabs(det) < 1e-12) {
        return -1;
    }
    for (k = 0; k < 3; k++) {
        double p1 = (double)t->p[1][k] - t->p[0][k], p2 = (double)t->p[2][k] - t->p[0][k];

        m[0][k] = (p1 * v2 - p2 * v1) / det;
        m[1][k] = (p2 * u1 - p1 * u2) / det;
        m[2][k] = (double)t->p[0][k] - t->uv[0][0] * m[0][k] - t->uv[0][1] * m[1][k];
    }
    return 0;
}

void rm_at(const double m[3][3], double u, double v, double out[3])
{
    int k;

    for (k = 0; k < 3; k++) {
        out[k] = u * m[0][k] + v * m[1][k] + m[2][k];
    }
}

double rm_wave(double turns)
{
    double t = turns - floor(turns);

    return t < 0.25 ? 4.0 * t : t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0;
}

void rm_add(const double a[3], const double b[3], double s, double out[3])
{
    out[0] = a[0] + s * b[0];
    out[1] = a[1] + s * b[1];
    out[2] = a[2] + s * b[2];
}

void rm_cross(const double a[3], const double b[3], double out[3])
{
    double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];

    out[0] = x; out[1] = y; out[2] = z;
}

double rm_dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double rm_norm(double v[3])
{
    double len = sqrt(rm_dot(v, v));

    if (len > 0.0) {
        v[0] /= len; v[1] /= len; v[2] /= len;
    }
    return len;
}

typedef struct { double p[3][3], c[3], r2; float uv[3][2]; unsigned src; int tex, one, kind; unsigned char rgb[3]; } SealTri;

static int seal_u32(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;

    return x < y ? -1 : x > y;
}

void rm_seal_ex(const RmBatch *b, unsigned nb, RmOut *out, double shade, int skip_opposite)
{
    SealTri *g;
    unsigned bi, i, j, n = 0, cap = out->ntri, ng, *drop = NULL, *two = NULL;
    unsigned char *add;
    int k;

    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].bb ? 0u : b[bi].n * 2u;
    }
    g = (SealTri *)calloc(cap ? cap : 1u, sizeof *g);
    add = (unsigned char *)calloc(cap ? cap : 1u, 1);
    drop = (unsigned *)malloc((out->ndrop + 1u) * sizeof *drop);
    two = (unsigned *)malloc((out->ntwo + 1u) * sizeof *two);
    if (g == NULL || add == NULL || drop == NULL || two == NULL) {
        free(g); free(add); free(drop); free(two);
        out->fail = 1;
        return;
    }
    if (out->ndrop) {
        memcpy(drop, out->drop, out->ndrop * sizeof *drop);
    }
    if (out->ntwo) {
        memcpy(two, out->two, out->ntwo * sizeof *two);
    }
    qsort(drop, out->ndrop, sizeof *drop, seal_u32);
    qsort(two, out->ntwo, sizeof *two, seal_u32);
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            SealTri *q;

            if (bsearch(&t->id, drop, out->ndrop, sizeof *drop, seal_u32) != NULL) {
                continue;
            }
            q = &g[n++];
            for (j = 0; j < 3u; j++) {
                for (k = 0; k < 3; k++) {
                    q->p[j][k] = t->p[j][k];
                }
            }
            memcpy(q->uv, t->uv, sizeof q->uv);
            q->src = t->id;
            q->tex = -1;
            q->one = b[bi].cull == 1 && bsearch(&t->id, two, out->ntwo, sizeof *two, seal_u32) == NULL;
            memcpy(q->rgb, t->rgb, 3u);
            /* its turned copy */
            for (j = 0; j < out->nturn; j++) {
                if (out->turn[j].id == t->id && n < cap) {
                    SealTri *c = &g[n++];
                    unsigned m;

                    *c = *q;
                    for (m = 0; m < 3u; m++) {
                        c->p[m][0] = 2.0 * out->turn[j].cx - q->p[m][0];
                        c->p[m][2] = 2.0 * out->turn[j].cz - q->p[m][2];
                    }
                    break;
                }
            }
        }
    }
    for (i = out->seal_from; i < out->ntri && n < cap; i++) {
        const RmNew *t = &out->tri[i];
        SealTri *q;

        if (t->alpha != 0 || t->kind == 1) {
            continue;                   /* (a shadow) */
        }
        q = &g[n++];
        for (j = 0; j < 3u; j++) {
            for (k = 0; k < 3; k++) {
                q->p[j][k] = t->p[j][k];
            }
        }
        memcpy(q->uv, t->uv, sizeof q->uv);
        q->src = t->src;
        q->tex = t->tex;
        q->one = t->cull == 1 && t->tex < 0;        /* (a body with a texture of its own is closed by its making) */
        q->kind = t->kind;
        memcpy(q->rgb, t->rgb, 3u);
    }
    ng = n;
    for (i = 0; i < ng; i++) {
        add[i] = (unsigned char)g[i].one;
    }
    /* (a face drawn both ways already: the model's other face in that plane looks the way the back would, over the
     * face's middle) */
    for (i = 0; i < ng && skip_opposite; i++) {
        double ni[3], e1[3], e2[3], c[3], l;

        if (!add[i]) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            e1[k] = g[i].p[1][k] - g[i].p[0][k];
            e2[k] = g[i].p[2][k] - g[i].p[0][k];
            c[k] = (g[i].p[0][k] + g[i].p[1][k] + g[i].p[2][k]) / 3.0;
        }
        rm_cross(e1, e2, ni);
        l = sqrt(rm_dot(ni, ni));
        if (l < 1e-9) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            ni[k] /= l;
        }
        for (j = 0; j < ng && add[i]; j++) {
            double nj[3], a0[3], a1[3], a2[3], d[3], w0, w1, w2;

            if (j == i) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                e1[k] = g[j].p[1][k] - g[j].p[0][k];
                e2[k] = g[j].p[2][k] - g[j].p[0][k];
                d[k] = c[k] - g[j].p[0][k];
            }
            rm_cross(e1, e2, nj);
            l = sqrt(rm_dot(nj, nj));
            if (l < 1e-9 || rm_dot(nj, ni) / l > -0.95 || fabs(rm_dot(d, nj) / l) > 0.05) {
                continue;
            }
            /* (is the face's middle inside j: same side of j's three edges) */
            for (k = 0; k < 3; k++) {
                a0[k] = g[j].p[1][k] - g[j].p[0][k];
                a1[k] = g[j].p[2][k] - g[j].p[1][k];
                a2[k] = g[j].p[0][k] - g[j].p[2][k];
            }
            {
                double q0[3], q1[3], q2[3], r0[3], r1[3], r2[3];

                for (k = 0; k < 3; k++) {
                    q0[k] = c[k] - g[j].p[0][k];
                    q1[k] = c[k] - g[j].p[1][k];
                    q2[k] = c[k] - g[j].p[2][k];
                }
                rm_cross(a0, q0, r0);
                rm_cross(a1, q1, r1);
                rm_cross(a2, q2, r2);
                w0 = rm_dot(r0, nj);
                w1 = rm_dot(r1, nj);
                w2 = rm_dot(r2, nj);
            }
            if ((w0 >= -1e-6 && w1 >= -1e-6 && w2 >= -1e-6) || (w0 <= 1e-6 && w1 <= 1e-6 && w2 <= 1e-6)) {
                add[i] = 0;
            }
        }
    }
    for (i = 0; i < ng && !out->fail; i++) {
        unsigned char rgb[3];
        double t0[2], t1[2], t2[2];

        if (!add[i]) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            rgb[k] = (unsigned char)(g[i].rgb[k] * shade);
        }
        t0[0] = g[i].uv[0][0]; t0[1] = g[i].uv[0][1];
        t1[0] = g[i].uv[1][0]; t1[1] = g[i].uv[1][1];
        t2[0] = g[i].uv[2][0]; t2[1] = g[i].uv[2][1];
        rm_tri(out, g[i].src, g[i].tex, 1, rgb, g[i].p[0], g[i].p[2], g[i].p[1], t0, t2, t1, g[i].kind, 0);
    }
    free(g);
    free(add);
    free(drop);
    free(two);
}

void rm_seal(const RmBatch *b, unsigned nb, RmOut *out, double shade)
{
    rm_seal_ex(b, nb, out, shade, 0);
}
