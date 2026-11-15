/*
 * engine/world/remade/mesh.c -- a map chunk's own mesh (a room: its floor, its
 * walls, the furniture drawn into it) remade piece by piece.
 *
 * A room's mesh is one model, made for the field's steep camera: a wall the
 * camera never faced is not there, a seat is a picture on the floor, a
 * console a card leaning back.  Each piece is the job of a builder the
 * standing models already have, so the settings are a list of builders, each
 * on a box of the room:
 *
 *     NAME[@X0:X1:Y0:Y1:Z0:Z1][|SETTINGS];NAME...
 *
 * NAME is tower, cushion, box, lower (the box's triangles moved down: `by`) or settle (every piece set down
 * onto what is under it; last); the box (the mesh's own space) takes the
 * triangles wholly inside it (no box: the whole room); SETTINGS are that
 * builder's own, as it takes them for a standing model.  Each builder sees
 * only its box, so a cushion's picture is a cushion's picture, not a room.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "remade.h"

#define MESH_PARTS 16

/* `lower|by=D`: the box's triangles moved down D units, each in its own picture and light (a table drawn into the
 * room standing on the floor the field showed, over a floor the mesh lays lower). */
static void rm_lower(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double by = rm_optf(opt, "by", 0.0);
    unsigned i, j;

    (void)tex;
    (void)ntex;
    if (by == 0.0) {
        return;
    }
    for (i = 0; i < nb; i++) {
        for (j = 0; j < b[i].n; j++) {
            const RmTri *t = &b[i].tri[j];
            double p[3][3], uv[3][2];
            unsigned first = out->ntri, k;
            int a;

            for (a = 0; a < 3; a++) {
                p[a][0] = t->p[a][0];
                p[a][1] = t->p[a][1] - by;
                p[a][2] = t->p[a][2];
                uv[a][0] = t->uv[a][0];
                uv[a][1] = t->uv[a][1];
            }
            {
                int cull = b[i].cull;

                for (k = 0; k < out->ntwo; k++) {
                    if (out->two[k] == t->id) {
                        cull = 0;       /* (shown from both sides: so is its copy) */
                    }
                }
                rm_tri(out, t->id, -1, cull, t->rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
            }
            for (k = first; k < out->ntri; k++) {
                out->tri[k].lit = 1;
            }
            rm_drop(out, t->id);
        }
    }
}

/* `settle|max=M`: every piece in the box (triangles joined at their corners: a cabinet, a pillow, a box on a shelf)
 * set down onto what is under it.  The room's own floor and walls (pieces wider than 100 units) stay; the others, the
 * lowest first, go down onto the highest upward face under their foot (the floor, or a piece already set down: a
 * pillow onto its bed, a toy onto its cabinet) when that face lies at most M units (default 6) under them: furniture
 * drawn for the field's steep camera a few units over the floor.  A flat piece (a shadow sheet, a rug) keeps 0.15
 * over what it lies on.  Things hung higher (a picture, a ceiling pipe's joint) stay.  Runs last: the parts the earlier
 * builders made inside a moved piece's box go down with it. */
typedef struct {
    double lo[3], hi[3], shift;
    int big, flat, done, live;
} StPiece;

static unsigned st_find(unsigned *par, unsigned a)
{
    while (par[a] != a) {
        par[a] = par[par[a]];
        a = par[a];
    }
    return a;
}

typedef struct { long long k; unsigned t; } StKey;

static int st_key_cmp(const void *a, const void *b)
{
    const StKey *x = (const StKey *)a, *y = (const StKey *)b;

    return x->k < y->k ? -1 : x->k > y->k;
}

/* the height of the upward face `t` (shifted down by `sh`) at (x, z), or -1e30 when (x, z) is not over it */
static double st_up_at(const RmTri *t, double sh, double x, double z)
{
    const double ax = t->p[0][0], az = t->p[0][2], bx = t->p[1][0], bz = t->p[1][2], cx = t->p[2][0], cz = t->p[2][2];
    const double den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    double w0, w1, w2;

    if (fabs(den) < 1e-9) {
        return -1e30;
    }
    w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / den;
    w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / den;
    w2 = 1.0 - w0 - w1;
    if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) {
        return -1e30;
    }
    return w0 * t->p[0][1] + w1 * t->p[1][1] + w2 * t->p[2][1] - sh;
}

static void rm_settle(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double maxd = rm_optf(opt, "max", 6.0);
    const int base = rm_optf(opt, "base", 0.0) != 0.0;
    double ymin = 1e30;
    unsigned total = 0, i, j, k, n = 0, np = 0, *par, *lab, *pidx;
    const RmTri **tr;
    unsigned char *upf, *gone;
    unsigned *bat;
    StKey *keys;
    StPiece *pc;

    (void)tex;
    (void)ntex;
    for (i = 0; i < nb; i++) {
        total += b[i].n;
    }
    if (total == 0) {
        return;
    }
    tr = (const RmTri **)calloc(total, sizeof *tr);
    bat = (unsigned *)calloc(total, sizeof *bat);
    upf = (unsigned char *)calloc(total, 1);
    gone = (unsigned char *)calloc(total, 1);
    par = (unsigned *)calloc(total, sizeof *par);
    lab = (unsigned *)calloc(total, sizeof *lab);
    pidx = (unsigned *)calloc(total, sizeof *pidx);
    keys = (StKey *)calloc((size_t)total * 3u, sizeof *keys);
    pc = (StPiece *)calloc(total, sizeof *pc);
    if (tr == NULL || bat == NULL || upf == NULL || gone == NULL || par == NULL || lab == NULL || pidx == NULL || keys == NULL || pc == NULL) {
        out->fail = 1;
        goto done;
    }
    for (i = 0; i < nb; i++) {
        for (j = 0; j < b[i].n; j++) {
            const RmTri *t = &b[i].tri[j];
            double e1[3], e2[3], nn[3], l;

            for (k = 0; k < 3; k++) {
                e1[k] = t->p[1][k] - t->p[0][k];
                e2[k] = t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, nn);
            l = sqrt(rm_dot(nn, nn));
            tr[n] = t;
            bat[n] = i;
            {
                unsigned w;

                for (w = 0; w < out->ndrop && !gone[n]; w++) {
                    gone[n] = out->drop[w] == t->id;
                }
            }
            upf[n] = l > 1e-9 && nn[1] / l > 0.7;
            par[n] = n;
            for (k = 0; k < 3; k++) {
                const long long qx = (long long)floor(t->p[k][0] * 4.0 + 0.5), qy = (long long)floor(t->p[k][1] * 4.0 + 0.5),
                                qz = (long long)floor(t->p[k][2] * 4.0 + 0.5);

                keys[n * 3u + k].k = ((qx & 0x1FFFFF) << 42) | ((qy & 0x1FFFFF) << 21) | (qz & 0x1FFFFF);
                keys[n * 3u + k].t = n;
            }
            n++;
        }
    }
    qsort(keys, (size_t)n * 3u, sizeof *keys, st_key_cmp);
    for (i = 1; i < n * 3u; i++) {
        if (keys[i].k == keys[i - 1].k) {
            const unsigned ra = st_find(par, keys[i].t), rb = st_find(par, keys[i - 1].t);

            if (ra != rb) {
                par[ra] = rb;
            }
        }
    }
    {
        /* `join=X0:X1:Y0:Y1:Z0:Z1+..`: the triangles inside each box are one piece (a chair whose legs and seat the map
         * draws apart: it goes down whole, its legs under its seat) */
        char jb[1024];
        const char *jp = rm_opt(opt, "join", jb, sizeof jb);

        while (jp != NULL && *jp != '\0') {
            double bx[6];
            unsigned first = n;

            if (sscanf(jp, "%lf:%lf:%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3], &bx[4], &bx[5]) == 6) {
                for (i = 0; i < n; i++) {
                    int in = 1;

                    for (k = 0; k < 9 && in; k++) {
                        in = tr[i]->p[k / 3][k % 3] >= bx[2 * (k % 3)] && tr[i]->p[k / 3][k % 3] <= bx[2 * (k % 3) + 1];
                    }
                    if (in && first == n) {
                        first = i;
                    } else if (in) {
                        const unsigned ra = st_find(par, i), rb = st_find(par, first);

                        if (ra != rb) {
                            par[ra] = rb;
                        }
                    }
                }
            }
            jp = strchr(jp, '+');
            jp = jp != NULL ? jp + 1 : NULL;
        }
    }
    for (i = 0; i < n; i++) {
        const unsigned r = st_find(par, i);

        for (j = 0; j < np && pidx[j] != r; j++) {
        }
        if (j == np) {
            pidx[np] = r;
            for (k = 0; k < 3; k++) {
                pc[np].lo[k] = 1e30;
                pc[np].hi[k] = -1e30;
            }
            np++;
        }
        lab[i] = j;
        pc[j].live |= !gone[i];
        for (k = 0; k < 9; k++) {
            const double v = tr[i]->p[k / 3][k % 3];

            pc[j].lo[k % 3] = v < pc[j].lo[k % 3] ? v : pc[j].lo[k % 3];
            pc[j].hi[k % 3] = v > pc[j].hi[k % 3] ? v : pc[j].hi[k % 3];
        }
    }
    for (j = 0; j < np; j++) {
        if (pc[j].live && pc[j].lo[1] < ymin) {
            ymin = pc[j].lo[1];
        }
    }
    for (j = 0; j < np; j++) {
        pc[j].big = pc[j].hi[0] - pc[j].lo[0] > 100.0 || pc[j].hi[2] - pc[j].lo[2] > 100.0;
        pc[j].flat = pc[j].hi[1] - pc[j].lo[1] < 0.2;
        pc[j].done = pc[j].big || !pc[j].live;
    }
    /* the lowest first: each onto the floor and the pieces set down before it */
    for (;;) {
        unsigned best = np;
        double sup = -1e30, sx[9], sz[9];
        int ns = 0;

        for (j = 0; j < np; j++) {
            if (!pc[j].done && (best == np || pc[j].lo[1] < pc[best].lo[1])) {
                best = j;
            }
        }
        if (best == np) {
            break;
        }
        pc[best].done = 1;
        sx[ns] = (pc[best].lo[0] + pc[best].hi[0]) * 0.5;
        sz[ns++] = (pc[best].lo[2] + pc[best].hi[2]) * 0.5;
        for (i = 0; i < n && ns < 9; i++) {
            if (lab[i] != best) {
                continue;
            }
            for (k = 0; k < 3 && ns < 9; k++) {
                if (tr[i]->p[k][1] <= pc[best].lo[1] + 0.3) {
                    sx[ns] = tr[i]->p[k][0] + (sx[0] - tr[i]->p[k][0]) * 0.05;
                    sz[ns++] = tr[i]->p[k][2] + (sz[0] - tr[i]->p[k][2]) * 0.05;
                }
            }
        }
        for (i = 0; i < n; i++) {
            const unsigned o = lab[i];
            int s_;

            if (!upf[i] || gone[i] || o == best || (!pc[o].big && !(pc[o].done && o != best))) {
                continue;
            }
            for (s_ = 0; s_ < ns; s_++) {
                const double h = st_up_at(tr[i], pc[o].shift, sx[s_], sz[s_]);

                if (h <= pc[best].lo[1] + 0.3 && h > sup) {
                    sup = h;
                }
            }
        }
        if (base && ymin <= pc[best].lo[1] + 0.3 && ymin > sup) {
            sup = ymin;         /* (the model's own foot: the floor it stands on) */
        }
        if (sup > -1e29) {
            const double target = pc[best].flat ? sup + 0.15 : sup, gap = pc[best].lo[1] - target;

            if (gap > 0.3 && gap <= maxd) {
                pc[best].shift = gap;
            }
        }
        if (pc[best].shift == 0.0 && !pc[best].flat) {
            /* (sunk into a piece already set down: its middle over that one, its foot between that one's foot and
             * top: they go down together) */
            const double cx = (pc[best].lo[0] + pc[best].hi[0]) * 0.5, cz = (pc[best].lo[2] + pc[best].hi[2]) * 0.5;

            for (j = 0; j < np; j++) {
                if (j != best && !pc[j].big && pc[j].live && pc[j].shift > pc[best].shift && cx >= pc[j].lo[0] &&
                    cx <= pc[j].hi[0] && cz >= pc[j].lo[2] && cz <= pc[j].hi[2] && pc[best].lo[1] > pc[j].lo[1] + 0.3 &&
                    pc[best].lo[1] < pc[j].hi[1] - 0.3) {
                    pc[best].shift = pc[j].shift;
                }
            }
        }
    }
    /* the moved pieces: their triangles again, lower; earlier builders' parts in their boxes go down with them */
    {
        const unsigned made = out->ntri;

        for (i = 0; i < n; i++) {
            const StPiece *p = &pc[lab[i]];
            const RmTri *t = tr[i];
            double q[3][3], uv[3][2];
            unsigned first = out->ntri, w;
            int cull = b[bat[i]].cull, a, dropped = 0;

            if (p->shift == 0.0) {
                continue;
            }
            for (w = 0; w < out->ndrop; w++) {
                dropped |= out->drop[w] == t->id;
            }
            if (dropped) {
                continue;
            }
            for (w = 0; w < out->ntwo; w++) {
                if (out->two[w] == t->id) {
                    cull = 0;
                }
            }
            for (a = 0; a < 3; a++) {
                q[a][0] = t->p[a][0];
                q[a][1] = t->p[a][1] - p->shift;
                q[a][2] = t->p[a][2];
                uv[a][0] = t->uv[a][0];
                uv[a][1] = t->uv[a][1];
            }
            rm_tri(out, t->id, -1, cull, t->rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
            /* (its turned copy, if an earlier builder turned it: the game draws a turned copy only beside the
             * model's own triangle, which is left out now, so the copy goes down as a triangle of its own, half
             * round about the same upright, the corners in the same order) */
            for (w = 0; w < out->nturn; w++) {
                if (out->turn[w].id == t->id) {
                    double r[3][3];

                    for (a = 0; a < 3; a++) {
                        r[a][0] = 2.0 * out->turn[w].cx - q[a][0];
                        r[a][1] = q[a][1];
                        r[a][2] = 2.0 * out->turn[w].cz - q[a][2];
                    }
                    rm_tri(out, t->id, -1, cull, t->rgb, r[0], r[1], r[2], uv[0], uv[1], uv[2], 0, 0);
                    break;
                }
            }
            for (w = first; w < out->ntri; w++) {
                out->tri[w].lit = 1;
            }
            rm_drop(out, t->id);
        }
        for (i = 0; i < made; i++) {
            RmNew *t = &out->tri[i];
            double c[3] = { 0, 0, 0 };

            for (k = 0; k < 3; k++) {
                for (j = 0; j < 3; j++) {
                    c[j] += t->p[k][j] / 3.0;
                }
            }
            {
                unsigned pick = np, w;
                double vol = 1e30;

                /* (made from a triangle of the model: with that triangle's piece) */
                for (w = 0; w < n; w++) {
                    if (tr[w]->id == t->src) {
                        break;
                    }
                }
                if (w < n && pc[lab[w]].live) {
                    if (pc[lab[w]].shift != 0.0) {
                        for (k = 0; k < 3; k++) {
                            t->p[k][1] = (float)(t->p[k][1] - pc[lab[w]].shift);
                        }
                    }
                    continue;
                }
                for (j = 0; j < np; j++) {
                    if (pc[j].shift != 0.0 && !pc[j].flat && c[0] >= pc[j].lo[0] - 0.5 && c[0] <= pc[j].hi[0] + 0.5 &&
                        c[1] >= pc[j].lo[1] - 0.5 && c[1] <= pc[j].hi[1] + 0.5 && c[2] >= pc[j].lo[2] - 0.5 &&
                        c[2] <= pc[j].hi[2] + 0.5) {
                        const double v = (pc[j].hi[0] - pc[j].lo[0] + 1) * (pc[j].hi[1] - pc[j].lo[1] + 1) * (pc[j].hi[2] - pc[j].lo[2] + 1);

                        if (v < vol) {
                            vol = v;
                            pick = j;
                        }
                    }
                }
                if (pick < np) {
                    for (k = 0; k < 3; k++) {
                        t->p[k][1] = (float)(t->p[k][1] - pc[pick].shift);
                    }
                }
            }
        }
    }
    if (getenv("RM_SETTLE_LOG") != NULL) {
        for (j = 0; j < np; j++) {
            if (pc[j].shift != 0.0) {
                fprintf(stderr, "settle: piece x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f down %.2f\n", pc[j].lo[0], pc[j].hi[0],
                        pc[j].lo[1], pc[j].hi[1], pc[j].lo[2], pc[j].hi[2], pc[j].shift);
            }
        }
    }
done:
    free(tr);
    free(bat);
    free(upf);
    free(gone);
    free(par);
    free(lab);
    free(pidx);
    free(keys);
    free(pc);
}

static RmBuild *mesh_builder(const char *name)
{
    if (strcmp(name, "tower") == 0) {
        return rm_tower;
    }
    if (strcmp(name, "cushion") == 0) {
        return rm_cushion;
    }
    if (strcmp(name, "box") == 0) {
        return rm_box;
    }
    if (strcmp(name, "lower") == 0) {
        return rm_lower;
    }
    if (strcmp(name, "settle") == 0) {
        return rm_settle;
    }
    if (strcmp(name, "turned") == 0) {
        return rm_turned;
    }
    if (strcmp(name, "rock") == 0) {
        return rm_rock;
    }
    if (strcmp(name, "cup") == 0) {
        return rm_cup;
    }
    if (strcmp(name, "plate") == 0) {
        return rm_plate;
    }
    if (strcmp(name, "candle") == 0) {
        return rm_candle;
    }
    return NULL;
}

/* v turned `deg` degrees about the unit axis a through c (Rodrigues); `dir`: a direction (no c). */
static void tip_point(const double a[3], double deg, const double c[3], const float in[3], float o[3], int dir)
{
    const double r = deg * 3.14159265358979 / 180.0, cs = cos(r), sn = sin(r);
    double v[3], kv[3], d;
    int k;

    for (k = 0; k < 3; k++) {
        v[k] = (double)in[k] - (dir ? 0.0 : c[k]);
    }
    rm_cross(a, v, kv);
    d = rm_dot(a, v);
    for (k = 0; k < 3; k++) {
        o[k] = (float)(v[k] * cs + kv[k] * sn + a[k] * d * (1.0 - cs) + (dir ? 0.0 : c[k]));
    }
}

/* One builder on the triangles of the box `bx` (NULL: all of them).  `tip=AX:AY:AZ:DEG:CX:CY:CZ` (a thing knocked
 * over, a builder made for it standing: the potted plant lying on its side): the triangles are turned -DEG about the
 * axis through (CX, CY, CZ) first, so the thing stands as the builder knows it, and what it makes is turned +DEG
 * back, so it lies where the thing lies. */
static void mesh_part(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, RmBuild *fn, const double *bx,
                      const char *opt, RmOut *out)
{
    RmBatch *sb;
    RmTri *st;
    unsigned i, j, k, total = 0, nsb = 0, at = 0;
    double tip[7];
    int tipped = 0;

    {
        char tb[160];

        if (rm_opt(opt, "tip", tb, sizeof tb) != NULL
            && sscanf(tb, "%lf:%lf:%lf:%lf:%lf:%lf:%lf", &tip[0], &tip[1], &tip[2], &tip[3], &tip[4], &tip[5], &tip[6]) == 7
            && rm_norm(tip) > 1e-9 && bx != NULL) {
            tipped = 1;
        }
    }
    if (bx == NULL) {
        fn(b, nb, tex, ntex, opt, out);
        return;
    }
    for (i = 0; i < nb; i++) {
        total += b[i].n;
    }
    sb = (RmBatch *)calloc(nb ? nb : 1u, sizeof *sb);
    st = (RmTri *)calloc(total ? total : 1u, sizeof *st);
    if (sb == NULL || st == NULL) {
        free(sb);
        free(st);
        out->fail = 1;
        return;
    }
    for (i = 0; i < nb; i++) {
        unsigned first = at;

        for (j = 0; j < b[i].n; j++) {
            const RmTri *t = &b[i].tri[j];
            int in = 1;

            for (k = 0; k < 3u && in; k++) {
                in = t->p[k][0] >= bx[0] - 0.05 && t->p[k][0] <= bx[1] + 0.05 && t->p[k][1] >= bx[2] - 0.05 &&
                     t->p[k][1] <= bx[3] + 0.05 && t->p[k][2] >= bx[4] - 0.05 && t->p[k][2] <= bx[5] + 0.05;
            }
            if (in) {
                st[at] = *t;
                if (tipped) {
                    for (k = 0; k < 3u; k++) {
                        tip_point(tip, -tip[3], &tip[4], t->p[k], st[at].p[k], 0);
                    }
                }
                at++;
            }
        }
        if (at > first) {
            sb[nsb] = b[i];
            sb[nsb].tri = &st[first];
            sb[nsb].n = at - first;
            nsb++;
        }
    }
    if (nsb > 0) {
        const unsigned made = out->ntri;

        fn(sb, nsb, tex, ntex, opt, out);
        if (tipped) {
            for (i = made; i < out->ntri; i++) {
                RmNew *t = &out->tri[i];
                float q[3];

                for (k = 0; k < 3u; k++) {
                    tip_point(tip, tip[3], &tip[4], t->p[k], q, 0);
                    t->p[k][0] = q[0]; t->p[k][1] = q[1]; t->p[k][2] = q[2];
                }
                tip_point(tip, tip[3], &tip[4], t->n, q, 1);
                t->n[0] = q[0]; t->n[1] = q[1]; t->n[2] = q[2];
            }
        }
    }
    free(sb);
    free(st);
}

void rm_mesh(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const char *p = opt;

    while (p != NULL && *p != '\0') {
        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        char part[4096], name[16], *set, *at;   /* (a room's walls: a tower part of many bands) */
        double bx[6];
        int has_box = 0;
        RmBuild *fn;

        if (len >= sizeof part) {
            len = sizeof part - 1u;
        }
        memcpy(part, p, len);
        part[len] = '\0';
        set = strchr(part, '|');
        if (set != NULL) {
            *set++ = '\0';
        }
        at = strchr(part, '@');
        if (at != NULL) {
            *at++ = '\0';
            has_box = sscanf(at, "%lf:%lf:%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3], &bx[4], &bx[5]) == 6;
        }
        snprintf(name, sizeof name, "%.15s", part);
        fn = mesh_builder(name);
        if (fn != NULL && (at == NULL || has_box)) {
            const unsigned made = out->ntri;
            const clock_t t0 = clock();

            out->seal_from = made;      /* (a part seals what it made itself only) */
            mesh_part(b, nb, tex, ntex, fn, has_box ? bx : NULL, set != NULL ? set : "", out);
            out->seal_from = 0;
            if (getenv("RM_MESH_TIME") != NULL) {         /* research: where the time goes */
                fprintf(stderr, "mesh: %s%s %.0f ms, %u new\n", name, has_box ? "@box" : "",
                        1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC, out->ntri - made);
            }
            if (fn == rm_lower && has_box) {
                /* (the parts the earlier builders made in the box go down with it: a bed's new side, its seal) */
                const double by = rm_optf(set != NULL ? set : "", "by", 0.0);
                unsigned i, k, a;

                for (i = 0; i < made; i++) {
                    RmNew *t = &out->tri[i];
                    int in = 1;

                    for (k = 0; k < 3u && in; k++) {
                        in = t->p[k][0] >= bx[0] - 0.05 && t->p[k][0] <= bx[1] + 0.05 && t->p[k][1] >= bx[2] - 0.05 &&
                             t->p[k][1] <= bx[3] + 0.05 && t->p[k][2] >= bx[4] - 0.05 && t->p[k][2] <= bx[5] + 0.05;
                    }
                    if (in) {
                        for (a = 0; a < 3u; a++) {
                            t->p[a][1] = (float)(t->p[a][1] - by);
                        }
                    }
                }
            }
        }
        p = end != NULL ? end + 1 : NULL;
    }
}
