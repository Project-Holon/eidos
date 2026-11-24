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
 * NAME is tower, cushion, box, lower (the box's triangles moved down: `by`), fold (a sheet's folded corner laid
 * on it and given sides) or settle (every piece set down
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
 * set down onto what is under it.  The room's own floor and walls (pieces wider than 100 units that are not flat)
 * stay; the others, the lowest first, go down onto the highest upward face under their foot (the floor, or a piece
 * already set down: a pillow onto its bed, a toy onto its cabinet) when that face lies at most M units (default 6)
 * under them: furniture drawn for the field's steep camera a few units over the floor.  A flat piece (a shadow sheet,
 * a rug) keeps 0.15 over what it lies on, and so does a sheet that is not flat (a piece with no steep face and a
 * level upward face at its lowest height: a rug with a corner turned up): set down exactly, that face and the floor's
 * lie in one plane and the two flicker through each other.  A flat piece is set down however wide it is: a shadow sheet over 100 units
 * wide is not the room's shell (a flat floor has nothing under it and stays by that).  Things hung higher (a picture,
 * a ceiling pipe's joint) stay.  Runs last: the parts the earlier builders made inside a moved piece's box go down
 * with it, each body whole (a body that lies only partly in the box goes with the piece it stands on). */
typedef struct {
    double lo[3], hi[3], shift;
    int big, flat, sheet, steep, foot, done, live;
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

/* `fold|rise=R,shade=S`: a sheet with a corner folded over itself (a rug knocked about).  The map draws the folded
 * corner as a sheet without thickness that hangs over the sheet (2 units up, joined to it at one end of the fold
 * line only): a picture for the field's steep camera, a sheet in the air for a battle's low one.  In the box:
 *   - a folded sheet is a piece (triangles of one draw joined at their corners) with a level upward face at the
 *     box's lowest height and corners higher than it: the faces at that height are the sheet, the others its folded
 *     corner (a shadow sheet in the box, a thing that stands on the sheet are pieces of their own and stay);
 *   - the folded corner is laid on the sheet: the height of each of its corners over the sheet is scaled so that
 *     the highest lies R over it (0, the default: the heights stay the map's);
 *   - every edge of the folded corner's upward faces that no other of them shares (the fold line, the free edges)
 *     gets a side down to the sheet, looking out, in the pixels along that edge stretched downward, S darker
 *     (default 0.8): a plate's rim (plate.c) under a face that is not level.
 * What it makes is its source triangle's: `settle` after it takes it down with the sheet. */
typedef struct {
    const RmTri *t;
    unsigned bat;
    double y[3];                /* its corners' heights as laid */
    int up, raised;
} FdTri;

static void rm_fold(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double rise = rm_optf(opt, "rise", 0.0), shade = rm_optf(opt, "shade", 0.8);
    double ymin = 1e30;
    unsigned total = 0, n = 0, i, j, k, *par;
    unsigned char *foot, *high;
    double *top;
    FdTri *f;

    (void)tex;
    (void)ntex;
    for (i = 0; i < nb; i++) {
        total += b[i].n;
    }
    if (total == 0) {
        return;
    }
    f = (FdTri *)calloc(total, sizeof *f);
    par = (unsigned *)calloc(total, sizeof *par);
    foot = (unsigned char *)calloc(total, 1);
    high = (unsigned char *)calloc(total, 1);
    top = (double *)calloc(total, sizeof *top);
    if (f == NULL || par == NULL || foot == NULL || high == NULL || top == NULL) {
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
                if (t->p[k][1] < ymin) {
                    ymin = t->p[k][1];
                }
            }
            rm_cross(e1, e2, nn);
            l = sqrt(rm_dot(nn, nn));
            f[n].t = t;
            f[n].bat = i;
            f[n].up = l > 1e-9 && nn[1] / l > 0.7;
            par[n] = n;
            n++;
        }
    }
    /* the pieces: triangles of one draw that share a corner */
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            int same = 0, a, c;

            if (f[i].bat != f[j].bat) {
                continue;
            }
            for (a = 0; a < 3 && !same; a++) {
                for (c = 0; c < 3 && !same; c++) {
                    same = fabs(f[i].t->p[a][0] - f[j].t->p[c][0]) < 0.05 && fabs(f[i].t->p[a][1] - f[j].t->p[c][1]) < 0.05 &&
                           fabs(f[i].t->p[a][2] - f[j].t->p[c][2]) < 0.05;
                }
            }
            if (same) {
                const unsigned ra = st_find(par, i), rb = st_find(par, j);

                if (ra != rb) {
                    par[ra] = rb;
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        const RmTri *t = f[i].t;
        const unsigned r = st_find(par, i);
        const double hi = t->p[0][1] > t->p[1][1] ? (t->p[0][1] > t->p[2][1] ? t->p[0][1] : t->p[2][1])
                                                  : (t->p[1][1] > t->p[2][1] ? t->p[1][1] : t->p[2][1]);

        f[i].raised = hi > ymin + 0.05;
        if (f[i].up && !f[i].raised) {
            foot[r] = 1;
        }
        if (f[i].raised) {
            high[r] = 1;
            if (hi - ymin > top[r]) {
                top[r] = hi - ymin;
            }
        }
    }
    /* the folded corner laid on the sheet */
    for (i = 0; i < n; i++) {
        const RmTri *t = f[i].t;
        const unsigned r = st_find(par, i);
        const double s = rise > 0.0 && top[r] > 0.0 ? rise / top[r] : 1.0;

        f[i].raised = f[i].raised && foot[r] && high[r];
        for (k = 0; k < 3; k++) {
            f[i].y[k] = f[i].raised ? ymin + (t->p[k][1] - ymin) * s : t->p[k][1];
        }
        if (f[i].raised && s != 1.0) {
            double q[3][3], uv[3][2];
            const unsigned first = out->ntri;
            int cull = b[f[i].bat].cull, a;

            for (k = 0; k < out->ntwo; k++) {
                if (out->two[k] == t->id) {
                    cull = 0;
                }
            }
            for (a = 0; a < 3; a++) {
                q[a][0] = t->p[a][0];
                q[a][1] = f[i].y[a];
                q[a][2] = t->p[a][2];
                uv[a][0] = t->uv[a][0];
                uv[a][1] = t->uv[a][1];
            }
            rm_tri(out, t->id, -1, cull, t->rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
            for (k = first; k < out->ntri; k++) {
                out->tri[k].lit = 1;
            }
            rm_drop(out, t->id);
        }
    }
    /* its sides: each edge of its upward faces that no other of them shares, down to the sheet */
    for (i = 0; i < n; i++) {
        const RmTri *t = f[i].t;
        int a;

        if (!f[i].raised || !f[i].up) {
            continue;
        }
        for (a = 0; a < 3; a++) {
            const int a1 = (a + 1) % 3;
            const float *p0 = t->p[a], *p1 = t->p[a1];
            double q[4][3], uv[4][2], out_n[3], c[3] = { 0, 0, 0 };
            unsigned char rgb[3];
            int shared = 0, m;

            if (f[i].y[a] <= ymin + 0.01 && f[i].y[a1] <= ymin + 0.01) {
                continue;               /* (on the sheet: no side) */
            }
            for (j = 0; j < n && !shared; j++) {
                int s0 = 0, s1 = 0;

                if (j == i || !f[j].raised || !f[j].up || st_find(par, j) != st_find(par, i)) {
                    continue;
                }
                for (m = 0; m < 3; m++) {
                    const float *o = f[j].t->p[m];

                    s0 |= fabs(o[0] - p0[0]) < 0.05 && fabs(o[1] - p0[1]) < 0.05 && fabs(o[2] - p0[2]) < 0.05;
                    s1 |= fabs(o[0] - p1[0]) < 0.05 && fabs(o[1] - p1[1]) < 0.05 && fabs(o[2] - p1[2]) < 0.05;
                }
                shared = s0 && s1;
            }
            if (shared) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                c[k] = ((double)t->p[0][k] + t->p[1][k] + t->p[2][k]) / 3.0;
                q[0][k] = p0[k];
                q[1][k] = p1[k];
                q[2][k] = p1[k];
                q[3][k] = p0[k];
                rgb[k] = (unsigned char)(t->rgb[k] * shade);
            }
            q[0][1] = f[i].y[a];
            q[1][1] = f[i].y[a1];
            q[2][1] = q[3][1] = ymin;
            uv[0][0] = uv[3][0] = t->uv[a][0];
            uv[0][1] = uv[3][1] = t->uv[a][1];
            uv[1][0] = uv[2][0] = t->uv[a1][0];
            uv[1][1] = uv[2][1] = t->uv[a1][1];
            /* (looking out of the face: across the edge, away from its middle) */
            out_n[0] = -((double)p1[2] - p0[2]);
            out_n[1] = 0.0;
            out_n[2] = (double)p1[0] - p0[0];
            if (((double)p0[0] - c[0]) * out_n[0] + ((double)p0[2] - c[2]) * out_n[2] < 0.0) {
                out_n[0] = -out_n[0];
                out_n[2] = -out_n[2];
            }
            /* (the corner of the quad that the side turns about: one whose edge end stands over the sheet, so
             * that an edge end on the sheet leaves one triangle and no empty one) */
            m = q[0][1] - ymin > 0.01 ? 0 : 1;
            for (k = 1; k <= 2; k++) {
                const int v1 = (m + k) % 4, v2 = (m + k + 1) % 4;
                double g1[3], g2[3], gn[3];
                int w;

                for (w = 0; w < 3; w++) {
                    g1[w] = q[v1][w] - q[m][w];
                    g2[w] = q[v2][w] - q[m][w];
                }
                rm_cross(g1, g2, gn);
                if (rm_dot(gn, gn) < 1e-10) {
                    continue;           /* (an edge end on the sheet: this half is empty) */
                }
                if (rm_dot(gn, out_n) >= 0.0) {
                    rm_tri(out, t->id, -1, 1, rgb, q[m], q[v1], q[v2], uv[m], uv[v1], uv[v2], 0, 0);
                } else {
                    rm_tri(out, t->id, -1, 1, rgb, q[m], q[v2], q[v1], uv[m], uv[v2], uv[v1], 0, 0);
                }
            }
        }
    }
done:
    free(f);
    free(par);
    free(foot);
    free(high);
    free(top);
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
    StKey *keys, *gkey = NULL;
    StPiece *pc;
    unsigned *gpick = NULL;

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
        pc[j].flat = pc[j].hi[1] - pc[j].lo[1] < 0.2;
        pc[j].big = !pc[j].flat && (pc[j].hi[0] - pc[j].lo[0] > 100.0 || pc[j].hi[2] - pc[j].lo[2] > 100.0);
        pc[j].done = pc[j].big || !pc[j].live;
    }
    for (i = 0; i < n; i++) {
        /* (a sheet that is not flat: a piece with no steep face (a body has sides) and a level upward face at its
         * lowest height, which would lie in the plane of what the piece is set down on) */
        const RmTri *t = tr[i];
        const double foot = pc[lab[i]].lo[1] + 0.05;
        double e1[3], e2[3], nn[3], l;

        if (gone[i]) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            e1[k] = t->p[1][k] - t->p[0][k];
            e2[k] = t->p[2][k] - t->p[0][k];
        }
        rm_cross(e1, e2, nn);
        l = sqrt(rm_dot(nn, nn));
        if (l > 1e-9 && fabs(nn[1] / l) <= 0.7) {
            pc[lab[i]].steep = 1;
        }
        if (upf[i] && t->p[0][1] <= foot && t->p[1][1] <= foot && t->p[2][1] <= foot) {
            pc[lab[i]].foot = 1;
        }
    }
    for (j = 0; j < np; j++) {
        pc[j].sheet = pc[j].foot && !pc[j].steep;
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
            const double target = pc[best].flat || pc[best].sheet ? sup + 0.15 : sup, gap = pc[best].lo[1] - target;

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
    if (getenv("RM_SETTLE_LOG") != NULL) {
        /* (a check, no rule: a piece whose foot lies at another's top in the map, its middle over that one, and that
         * did not go down as far as it or went down farther.  The map draws a body without its top where something
         * stands on it, so the body carries nothing here: a statue's cap stayed in the air over its base.  Such a
         * thing needs a `join` box) */
        for (j = 0; j < np; j++) {
            const double cx = (pc[j].lo[0] + pc[j].hi[0]) * 0.5, cz = (pc[j].lo[2] + pc[j].hi[2]) * 0.5;

            if (!pc[j].live || pc[j].big) {
                continue;
            }
            for (k = 0; k < np; k++) {
                if (k != j && pc[k].live && !pc[k].big && !pc[k].flat && fabs(pc[j].lo[1] - pc[k].hi[1]) <= 0.3 &&
                    cx >= pc[k].lo[0] && cx <= pc[k].hi[0] && cz >= pc[k].lo[2] && cz <= pc[k].hi[2] &&
                    fabs(pc[j].shift - pc[k].shift) > 0.3) {
                    fprintf(stderr, "settle: apart: the piece x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f (down %.2f) stood on the piece x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f (down %.2f)\n",
                            pc[j].lo[0], pc[j].hi[0], pc[j].lo[1], pc[j].hi[1], pc[j].lo[2], pc[j].hi[2], pc[j].shift,
                            pc[k].lo[0], pc[k].hi[0], pc[k].lo[1], pc[k].hi[1], pc[k].lo[2], pc[k].hi[2], pc[k].shift);
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
        /* (what a builder made from one triangle of the model is one body: a pouffe, a console.  Each of its
         * triangles finds the piece it goes down with as before; the body goes down with it when all of them found the
         * same piece, and with the piece it stands on otherwise.  Taken triangle by triangle, a body that reaches
         * out of the moved piece's box was torn: its top down with the piece, its foot left behind) */
        if (made > 0u) {
            gkey = (StKey *)calloc(made, sizeof *gkey);
            gpick = (unsigned *)calloc(made, sizeof *gpick);
            if (gkey == NULL || gpick == NULL) {
                out->fail = 1;
                goto done;
            }
        }
        for (i = 0; i < made; i++) {
            RmNew *t = &out->tri[i];
            double c[3] = { 0, 0, 0 };
            unsigned pick = np, w;
            double vol = 1e30;

            for (k = 0; k < 3; k++) {
                for (j = 0; j < 3; j++) {
                    c[j] += t->p[k][j] / 3.0;
                }
            }
            gkey[i].k = (long long)t->src;
            gkey[i].t = i;
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
                gpick[i] = np + 1u;     /* (done) */
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
            gpick[i] = pick;
        }
        if (made > 0u) {
            qsort(gkey, made, sizeof *gkey, st_key_cmp);
        }
        for (i = 0; i < made; i = j) {
            const unsigned first = gpick[gkey[i].t];
            int same = 1;
            unsigned w;

            for (j = i; j < made && gkey[j].k == gkey[i].k; j++) {
                same = same && gpick[gkey[j].t] == first;
            }
            if (first > np) {
                continue;               /* (with its source's piece: done above) */
            }
            if (!same) {
                /* (a body that lies only partly in a moved piece's box: it goes with what it stands on, the piece
                 * whose upward face is the highest under the body's middle at its foot's height or under it (a cup
                 * on a table that is set down goes down with the table; a pouffe on the room's floor stays)) */
                double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, sup = -1e30, cx, cz;
                unsigned sp = np, q;

                for (w = i; w < j; w++) {
                    const RmNew *t = &out->tri[gkey[w].t];

                    for (k = 0; k < 9; k++) {
                        const double v = t->p[k / 3][k % 3];

                        lo[k % 3] = v < lo[k % 3] ? v : lo[k % 3];
                        hi[k % 3] = v > hi[k % 3] ? v : hi[k % 3];
                    }
                }
                cx = (lo[0] + hi[0]) * 0.5;
                cz = (lo[2] + hi[2]) * 0.5;
                for (q = 0; q < n; q++) {
                    double h;

                    if (!upf[q] || gone[q]) {
                        continue;
                    }
                    h = st_up_at(tr[q], 0.0, cx, cz);
                    if (h <= lo[1] + 0.3 && h > sup) {
                        sup = h;
                        sp = lab[q];
                    }
                }
                if (getenv("RM_SETTLE_LOG") != NULL) {
                    fprintf(stderr, "settle: a body of %u triangles (x %.1f..%.1f y %.2f..%.2f z %.1f..%.1f) lies partly in a moved "
                                    "piece's box: it stands on %.2f (-1000: nothing) and goes down %.2f whole\n",
                            j - i, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], sp < np ? sup : -1000.0, sp < np ? pc[sp].shift : 0.0);
                }
                if (sp < np && pc[sp].shift != 0.0) {
                    for (w = i; w < j; w++) {
                        RmNew *t = &out->tri[gkey[w].t];

                        for (k = 0; k < 3; k++) {
                            t->p[k][1] = (float)(t->p[k][1] - pc[sp].shift);
                        }
                    }
                }
                continue;
            }
            if (first == np) {
                continue;
            }
            for (w = i; w < j; w++) {
                RmNew *t = &out->tri[gkey[w].t];

                for (k = 0; k < 3; k++) {
                    t->p[k][1] = (float)(t->p[k][1] - pc[first].shift);
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
    free(gkey);
    free(gpick);
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
    if (strcmp(name, "fold") == 0) {
        return rm_fold;
    }
    if (strcmp(name, "turned") == 0) {
        return rm_turned;
    }
    if (strcmp(name, "rock") == 0) {
        return rm_rock;
    }
    if (strcmp(name, "hedge") == 0) {
        return rm_hedge;
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
    if (strcmp(name, "skirt") == 0) {
        return rm_skirt;
    }
    if (strcmp(name, "stake") == 0) {
        return rm_stake;
    }
    if (strcmp(name, "driftwood") == 0) {
        return rm_driftwood;
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
