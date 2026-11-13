/*
 * engine/world/remade/sprite.c -- a picture card over a stand made a body
 * that is the picture: the flowers in a pot, a statue on its pedestal.
 *
 * Seen from the front the body is the sprite, pixel for pixel: every pixel
 * of the picture is a column standing out of it, its front face that pixel.
 * What the picture does not show, its depth, is taken from the picture's own
 * drawing:
 *   - parts: the picture's areas, as its drawer cut them up.  `split`
 *     colours are the lines between parts (a statue's outline); `group`
 *     names colours that make up one kind of part (a bloom's oranges, its
 *     yellows, the leaves' greens), and a part is a connected patch of one
 *     kind.  Without `group` every colour but the lines is one kind.
 *   - each part is round by its own shape: thick at its middle, thin at its
 *     rim (a pillow: the distance from the part's rim, on a circle), so an ear
 *     is thin and a head is round; `deep` scales it.
 *   - a line pixel lies between parts, a groove: lower than the parts by it.
 *   - the back: `back=fill`: each part in its own commonest colour (a
 *     statue's stone, no face at its back); otherwise the picture again.
 * The body stands over the middle of the stand (the card leans off it in the
 * map only so the flat picture shows from the field's camera), with its
 * lowest row on the card's foot.  The stand is closed as turned.c closes a
 * thing alike front and back (`stand=1`).
 *
 * Settings: split, group, deep (1), back (fill), stand (1).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define SPX 64

typedef struct {
    int W, H;
    unsigned col[SPX][SPX];             /* a pixel's colour, 0xRRGGBB */
    unsigned char in[SPX][SPX];         /* in the picture */
    unsigned char line[SPX][SPX];       /* a line between parts */
    unsigned char level[SPX][SPX];      /* a line filled level at the back: no crease there */
    short part[SPX][SPX];               /* its part, -1: none */
    double zf[SPX][SPX], zb[SPX][SPX];  /* how far it stands out in front, and behind (pixels) */
    double uf[SPX][SPX][2], ub[SPX][SPX][2];    /* the texture's place for its front and its back */
    int dist[SPX][SPX];                 /* a part's pixel: its distance from the part's rim */
    double zw[SPX][SPX];                /* the whole figure's roundness at a pixel */
    double dist2[SPX][SPX];             /* (`smooth`: one pass's sums) */
    double vb[SPX + 1][SPX + 1];        /* the back's corners: how far out behind */
    double vf[SPX + 1][SPX + 1];        /* the front's corners: how far out in front */
    short bj[SPX][SPX], bi[SPX][SPX];   /* the back's colour of a pixel: that pixel of the picture */
    unsigned char kept[SPX][SPX];       /* a `keep` colour at the back */
    /* `around`: where the body stands, a pixel's size, and each row's ends and middle */
    int around, collar, ra[SPX], re[SPX], wrapn;
    double pdeep[SPX * SPX], pfwd[SPX * SPX];   /* `part`: a part's own roundness (-1: none) and how far forward */
    double cx, cz, s;
    int stack[SPX * SPX][2];            /* (the parts' flood fill) */
} Sp;

static int alone(const RmBatch *b, unsigned nb, unsigned pb, unsigned pi, const float *p)
{
    unsigned bi, i;
    int k;

    for (bi = 0; bi < nb; bi++) {
        for (i = 0; i < b[bi].n; i++) {
            if (bi == pb && (i == pi || i == pi + 1u)) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                const float *q = b[bi].tri[i].p[k];

                if (fabs(q[0] - p[0]) < 0.3 && fabs(q[1] - p[1]) < 0.3 && fabs(q[2] - p[2]) < 0.3) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void face(RmOut *out, unsigned src, const unsigned char rgb[3], double shade, const double p[4][3],
                 const double uv[2], const double n[3])
{
    double e1[3], e2[3], c[3];
    unsigned char col[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
        col[k] = (unsigned char)(rgb[k] * shade);
    }
    rm_cross(e1, e2, c);
    if (rm_dot(c, n) >= 0.0) {
        rm_tri(out, src, -1, 1, col, p[0], p[1], p[2], uv, uv, uv, 0, 0);
        rm_tri(out, src, -1, 1, col, p[0], p[2], p[3], uv, uv, uv, 0, 0);
    } else {
        rm_tri(out, src, -1, 1, col, p[0], p[2], p[1], uv, uv, uv, 0, 0);
        rm_tri(out, src, -1, 1, col, p[0], p[3], p[2], uv, uv, uv, 0, 0);
    }
}

/* The statue's front (model b0_00079), carved by hand: "ROW FIRST DIGITS".  0: its rim and its ears, set back;
 * 1: the head, the arms, the chest's sides; 2: the snout down to the chin, the hands, the belly; 3: the feet,
 * nearest (it sits). */
static const char *const k_relief_statue[] = {
    "2 5 00.0000.00",
    "3 4 000011110000",
    "4 4 000111111000",
    "5 5 0111111110",
    "6 5 0111111110",
    "7 4 011112211110",
    "8 3 01111222211110",
    "9 2 0111122222111110",
    "10 2 0111112222111110",
    "11 2 0111222112221110",
    "12 2 0111222222221110",
    "13 3 011222222221110",
    "14 4 0122222222210",
    "15 4 233322223332",
    "16 4 233311113332",
    "17 5 2222..2222",
    NULL
};

/* The group a colour belongs to (`group=RRGGBB+RRGGBB/RRGGBB+..`): its number; without groups 0; a colour in no
 * group: a group of its own. */
static int group_of(const char *g, unsigned v)
{
    int n = 0;
    const char *p = g;

    if (g == NULL || *g == '\0') {
        return 0;
    }
    while (*p != '\0') {
        unsigned c = (unsigned)strtoul(p, NULL, 16);

        if (c == v) {
            return n;
        }
        p += strspn(p, "0123456789abcdefABCDEF");
        if (*p == '/') {
            n++;
        }
        if (*p != '\0') {
            p++;
        }
    }
    return 1000 + (int)(v & 0xFFFFFFu) % 30000;
}

void rm_sprite(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const RmBatch *cb = NULL;
    const RmTri *ct = NULL;
    const RmTex *tx;
    Sp *sp;
    char sb[256], gb[512];
    unsigned split[16], nsplit, bi, i;
    int W, H, w, h, i0, i1, j0, j1, fx, fy, c, j, k, np = 0, jlast = -1;
    double ua, ub, va, vb, foot[3], s, cx, cz, deep = rm_optf(opt, "deep", 0.5), whole = rm_optf(opt, "whole", 0.8);
    const int backone = rm_opt(opt, "back", sb, sizeof sb) != NULL && strcmp(sb, "one") == 0;
    const int backfill = backone || rm_opt(opt, "back", sb, sizeof sb) == NULL || strcmp(sb, "fill") == 0;
    double (*zw)[SPX];
    const char *groups = rm_opt(opt, "group", gb, sizeof gb);

    /* the card: two triangles that share no corner with the rest */
    for (bi = 0; bi < nb && ct == NULL; bi++) {
        for (i = 0; !b[bi].bb && i + 1u < b[bi].n && ct == NULL; i++) {
            int ok = b[bi].tex >= 0 && (unsigned)b[bi].tex < ntex && tex[b[bi].tex].rgba != NULL;

            for (k = 0; k < 3 && ok; k++) {
                ok = alone(b, nb, bi, i, b[bi].tri[i].p[k]) && alone(b, nb, bi, i, b[bi].tri[i + 1u].p[k]);
            }
            if (ok) {
                cb = &b[bi];
                ct = &b[bi].tri[i];
            }
        }
    }
    if (ct == NULL) {
        return;
    }
    tx = &tex[cb->tex];
    w = tx->w;
    h = tx->h;
    /* the part of the texture the card shows, the way it shows it (as rock.c reads a card) */
    {
        int vi, xmin = 0, xmax = 0, ymin = 0, ymax = 0;
        double fu, fv;

        for (vi = 1; vi < 6; vi++) {
            const float *p = ct[vi / 3].p[vi % 3];

            if (p[0] < ct[xmin / 3].p[xmin % 3][0]) xmin = vi;
            if (p[0] > ct[xmax / 3].p[xmax % 3][0]) xmax = vi;
            if (p[1] < ct[ymin / 3].p[ymin % 3][1]) ymin = vi;
            if (p[1] > ct[ymax / 3].p[ymax % 3][1]) ymax = vi;
        }
        ua = ct[xmin / 3].uv[xmin % 3][0];
        ub = ct[xmax / 3].uv[xmax % 3][0];
        va = ct[ymax / 3].uv[ymax % 3][1];
        vb = ct[ymin / 3].uv[ymin % 3][1];
        fu = floor((ua < ub ? ua : ub) + 1e-6);
        fv = floor((va < vb ? va : vb) + 1e-6);
        ua -= fu; ub -= fu; va -= fv; vb -= fv;
        /* where it stands: the middle of its two lowest corners; how wide: between them */
        {
            int lo1 = -1, lo2 = -1;

            for (vi = 0; vi < 6; vi++) {
                const float *p = ct[vi / 3].p[vi % 3];

                if (lo1 < 0 || p[1] < ct[lo1 / 3].p[lo1 % 3][1]) {
                    lo2 = lo1;
                    lo1 = vi;
                } else if ((lo2 < 0 || p[1] < ct[lo2 / 3].p[lo2 % 3][1])
                           && fabs(p[0] - ct[lo1 / 3].p[lo1 % 3][0]) + fabs(p[2] - ct[lo1 / 3].p[lo1 % 3][2]) > 0.01) {
                    lo2 = vi;
                }
            }
            if (lo2 < 0) {
                return;
            }
            for (k = 0; k < 3; k++) {
                foot[k] = (ct[lo1 / 3].p[lo1 % 3][k] + ct[lo2 / 3].p[lo2 % 3][k]) / 2.0;
            }
            s = sqrt(pow(ct[lo1 / 3].p[lo1 % 3][0] - ct[lo2 / 3].p[lo2 % 3][0], 2.0)
                     + pow(ct[lo1 / 3].p[lo1 % 3][2] - ct[lo2 / 3].p[lo2 % 3][2], 2.0));
        }
    }
    i0 = (int)rint(ua * w); i1 = (int)rint(ub * w);
    j0 = (int)rint(va * h); j1 = (int)rint(vb * h);
    fx = i0 > i1;
    fy = j0 > j1;
    if (fx) { int t = i0; i0 = i1; i1 = t; }
    if (fy) { int t = j0; j0 = j1; j1 = t; }
    W = i1 - i0;
    H = j1 - j0;
    if (W < 2 || H < 2 || W > SPX || H > SPX || i0 < 0 || j0 < 0 || i1 > w || j1 > h) {
        return;
    }
    s /= W;                             /* a pixel, in map units */
    sp = (Sp *)calloc(1, sizeof *sp);
    if (sp == NULL) {
        out->fail = 1;
        return;
    }
    sp->W = W;
    sp->H = H;
    zw = sp->zw;
    sp->wrapn = (int)rm_optf(opt, "wrapn", 999);
    nsplit = rm_colours(rm_opt(opt, "split", sb, sizeof sb), split, 16);
    for (j = 0; j < H; j++) {
        for (c = 0; c < W; c++) {
            const int tx0 = i0 + (fx ? W - 1 - c : c), ty0 = j0 + (fy ? H - 1 - j : j);
            const unsigned char *p = tx->rgba + ((size_t)ty0 * w + (size_t)tx0) * 4u;
            unsigned q;

            sp->in[j][c] = p[3] >= 128;
            sp->col[j][c] = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
            sp->part[j][c] = -1;
            sp->uf[j][c][0] = sp->ub[j][c][0] = (tx0 + 0.5) / w;
            sp->uf[j][c][1] = sp->ub[j][c][1] = (ty0 + 0.5) / h;
            for (q = 0; q < nsplit && sp->in[j][c]; q++) {
                sp->line[j][c] |= split[q] == sp->col[j][c];
            }
            jlast = sp->in[j][c] ? j : jlast;
        }
    }
    /* the parts: connected patches of one group, the lines left out */
    for (j = 0; j < H; j++) {
        for (c = 0; c < W; c++) {
            int (*stack)[2] = sp->stack, ns = 0, g;

            if (!sp->in[j][c] || sp->line[j][c] || sp->part[j][c] >= 0) {
                continue;
            }
            g = group_of(groups, sp->col[j][c]);
            sp->part[j][c] = (short)np;
            stack[ns][0] = j; stack[ns][1] = c; ns++;
            while (ns > 0) {
                int y, x, d;

                ns--;
                y = stack[ns][0];
                x = stack[ns][1];
                for (d = 0; d < 4; d++) {
                    const int ny = y + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = x + (d == 2 ? -1 : d == 3 ? 1 : 0);

                    if (ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx] || sp->line[ny][nx] || sp->part[ny][nx] >= 0
                        || group_of(groups, sp->col[ny][nx]) != g) {
                        continue;
                    }
                    sp->part[ny][nx] = (short)np;
                    stack[ns][0] = ny; stack[ns][1] = nx; ns++;
                }
            }
            np++;
        }
    }
    /* `part=ROW:COL:DEEP:FWD+..`: the part with that pixel is DEEP round by its own shape (front and back alike: a
     * limb), and stands FWD pixels further forward (a creature's feet, its arms) */
    {
        char pb[256], *pp;

        for (k = 0; k < SPX * SPX; k++) {
            sp->pdeep[k] = -1.0;
            sp->pfwd[k] = 0.0;
        }
        if (rm_opt(opt, "part", pb, sizeof pb) != NULL) {
            for (pp = pb; pp != NULL && *pp != '\0'; ) {
                int pj, pc;
                double pd, pf;

                if (sscanf(pp, "%d:%d:%lf:%lf", &pj, &pc, &pd, &pf) == 4 && pj >= 0 && pj < H && pc >= 0 && pc < W
                    && sp->part[pj][pc] >= 0) {
                    sp->pdeep[sp->part[pj][pc]] = pd;
                    sp->pfwd[sp->part[pj][pc]] = pf;
                }
                pp = strchr(pp, '+');
                pp = pp != NULL ? pp + 1 : NULL;
            }
        }
    }
    /* the whole figure round first, by its outline (`whole`): a pixel's distance from the figure's rim, on a
     * circle; the parts stand out of that by their own shapes */
    {
        int (*dist)[SPX] = sp->dist, changed = 1, dmax = 1;

        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                dist[j][c] = sp->in[j][c] ? 1000 : 0;
            }
        }
        while (changed) {
            changed = 0;
            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    int d, m = 1000;

                    if (!sp->in[j][c]) {
                        continue;
                    }
                    for (d = 0; d < 4; d++) {
                        const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);
                        const int nd = ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx] ? 0 : dist[ny][nx];

                        m = nd < m ? nd : m;
                    }
                    if (m + 1 < dist[j][c]) {
                        dist[j][c] = m + 1;
                        changed = 1;
                    }
                }
            }
        }
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                dmax = sp->in[j][c] && dist[j][c] > dmax ? dist[j][c] : dmax;
            }
        }
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                double t = (dist[j][c] - 0.5) / dmax;

                t = t > 1.0 ? 1.0 : t;
                zw[j][c] = sp->in[j][c] ? 0.5 + whole * dmax * sqrt(1.0 - (1.0 - t) * (1.0 - t)) : 0.0;
                sp->zf[j][c] = sp->zb[j][c] = zw[j][c];
            }
        }
    }
    /* each part round by its own shape: a pixel's distance from the part's rim, on a circle as wide as the part's
     * middle is deep */
    {
        int (*dist)[SPX] = sp->dist, changed = 1, pt;

        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                dist[j][c] = sp->part[j][c] >= 0 ? 1000 : 0;
            }
        }
        while (changed) {
            changed = 0;
            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    int d, m = 1000;

                    if (sp->part[j][c] < 0) {
                        continue;
                    }
                    for (d = 0; d < 4; d++) {
                        const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);
                        const int nd = ny < 0 || ny >= H || nx < 0 || nx >= W || sp->part[ny][nx] != sp->part[j][c] ? 0 : dist[ny][nx];

                        m = nd < m ? nd : m;
                    }
                    if (m + 1 < dist[j][c]) {
                        dist[j][c] = m + 1;
                        changed = 1;
                    }
                }
            }
        }
        for (pt = 0; pt < np; pt++) {
            int dmax = 1;
            unsigned cnt[64], vals[64], nv = 0, best = 0, q;
            int bj = -1, bc = -1;

            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    if (sp->part[j][c] == pt) {
                        dmax = dist[j][c] > dmax ? dist[j][c] : dmax;
                        for (q = 0; q < nv && vals[q] != sp->col[j][c]; q++) {
                        }
                        if (q == nv && nv < 64u) {
                            vals[nv] = sp->col[j][c];
                            cnt[nv++] = 0;
                        }
                        if (q < 64u) {
                            cnt[q]++;
                            if (cnt[q] > best) {
                                best = cnt[q];
                                bj = j;
                                bc = c;
                            }
                        }
                    }
                }
            }
            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    double t, z;

                    if (sp->part[j][c] != pt) {
                        continue;
                    }
                    t = (dist[j][c] - 0.5) / dmax;
                    t = t > 1.0 ? 1.0 : t;
                    {
                        const int own = sp->pdeep[pt] >= 0.0;
                        const double fwd = sp->pfwd[pt];

                        z = (own ? sp->pdeep[pt] : deep) * dmax * sqrt(1.0 - (1.0 - t) * (1.0 - t));
                        sp->zf[j][c] = zw[j][c] + z + fwd;
                        sp->zb[j][c] = zw[j][c] + (backone && !own ? 0.0 : z) - fwd;
                        if (sp->zf[j][c] + sp->zb[j][c] < 1.0) {
                            sp->zb[j][c] = 1.0 - sp->zf[j][c];      /* (a column a pixel deep at least) */
                        }
                    }
                    if (backfill && bj >= 0) {
                        sp->ub[j][c][0] = sp->uf[bj][bc][0];
                        sp->ub[j][c][1] = sp->uf[bj][bc][1];
                    }
                }
            }
        }
    }
    /* `back=one`: the whole back in the figure's commonest colour (the lines' left out): a statue's stone */
    if (backone) {
        unsigned vals[64], cnt[64], nv = 0, best = 0, q;
        int bj = -1, bc = -1;

        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                if (!sp->in[j][c] || sp->line[j][c]) {
                    continue;
                }
                for (q = 0; q < nv && vals[q] != sp->col[j][c]; q++) {
                }
                if (q == nv && nv < 64u) {
                    vals[nv] = sp->col[j][c];
                    cnt[nv++] = 0;
                }
                if (q < 64u && ++cnt[q] > best) {
                    best = cnt[q];
                    bj = j;
                    bc = c;
                }
            }
        }
        for (j = 0; j < H && bj >= 0; j++) {
            for (c = 0; c < W; c++) {
                sp->ub[j][c][0] = sp->uf[bj][bc][0];
                sp->ub[j][c][1] = sp->uf[bj][bc][1];
            }
        }
    }
    /* a line: a groove between the parts beside it, lower than the lowest of them */
    for (j = 0; j < H; j++) {
        for (c = 0; c < W; c++) {
            int d, have = 0;
            double m = 1e30;

            if (!sp->line[j][c]) {
                continue;
            }
            for (d = 0; d < 8; d++) {
                static const int dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 }, dx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
                const int ny = j + dy[d], nx = c + dx[d];

                if (ny >= 0 && ny < H && nx >= 0 && nx < W && sp->part[ny][nx] >= 0) {
                    m = sp->zf[ny][nx] < m ? sp->zf[ny][nx] : m;
                    if (!have && backfill) {
                        sp->ub[j][c][0] = sp->ub[ny][nx][0];
                        sp->ub[j][c][1] = sp->ub[ny][nx][1];
                    }
                    have = 1;
                }
            }
            (void)m;
            /* (on the figure's own round, under the parts that stand out of it; at its back, where nothing is
             * drawn, no groove) */
            sp->zf[j][c] = zw[j][c] - 0.4 > 0.5 ? zw[j][c] - 0.4 : 0.5;
            sp->zb[j][c] = backone ? zw[j][c] : sp->zf[j][c];
        }
    }
    /* `back=one`, the back's colours as the material is: `keep=RRGGBB:J0:J1+..`, a colour that is the thing's own
     * all round in those rows (an ear's pale tip), stays on the back; `crease=RRGGBB`: where the back dips between
     * the round of its neighbours (under the head, between an arm and the body) it is in shade */
    if (backone) {
        /* (a small part with no roundness of its own, a cheek's highlight, is no dip at the back: level with the
         * highest beside it) */
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                int d;

                if (!sp->in[j][c] || sp->line[j][c] || sp->part[j][c] < 0 || sp->pdeep[sp->part[j][c]] >= 0.0) {
                    continue;
                }
                for (d = 0; d < 4; d++) {
                    const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);

                    if (ny >= 0 && ny < H && nx >= 0 && nx < W && sp->in[ny][nx] && !sp->line[ny][nx]
                        && sp->zb[ny][nx] > sp->zb[j][c]) {
                        sp->zb[j][c] = sp->zb[ny][nx];
                    }
                }
            }
        }
    }
    /* (a line that is no meeting of two parts each shaped of its own, an eye's ring, is drawn on the front only:
     * at the back it is level with the highest beside it, and no crease) */
    if (backone) {
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                int d, pa = -1, two = 0;
                double hi = sp->zb[j][c];

                if (!sp->in[j][c] || !sp->line[j][c]) {
                    continue;
                }
                for (d = 0; d < 8; d++) {
                    static const int dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 }, dx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
                    const int ny = j + dy[d], nx = c + dx[d];
                    int q;

                    if (ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx]) {
                        continue;
                    }
                    hi = sp->zb[ny][nx] > hi ? sp->zb[ny][nx] : hi;
                    q = sp->part[ny][nx];
                    if (q >= 0 && sp->pdeep[q] >= 0.0) {
                        two |= pa >= 0 && pa != q;
                        pa = pa < 0 ? q : pa;
                    }
                }
                if (!two) {
                    sp->zb[j][c] = hi;
                    sp->level[j][c] = 1;
                }
            }
        }
    }
    /* `soften=RRGGBB`: the dark outline round the figure is the 2D picture's, to set it off any ground; on a body
     * its rim takes that colour (the material's shade), the lines within the figure stay as drawn */
    {
        char sfb[16];
        unsigned sfc[1];
        int sj2 = -1, sc2 = -1;

        if (rm_colours(rm_opt(opt, "soften", sfb, sizeof sfb), sfc, 1) == 1) {
            for (j = 0; j < H && sj2 < 0; j++) {
                for (c = 0; c < W; c++) {
                    if (sp->in[j][c] && sp->col[j][c] == sfc[0]) {
                        sj2 = j;
                        sc2 = c;
                        break;
                    }
                }
            }
        }
        for (j = 0; j < H && sj2 >= 0; j++) {
            for (c = 0; c < W; c++) {
                int d, rim = 0;

                for (d = 0; d < 4 && sp->in[j][c] && sp->line[j][c]; d++) {
                    const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);

                    rim |= ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx];
                }
                if (rim) {
                    sp->uf[j][c][0] = sp->uf[sj2][sc2][0];
                    sp->uf[j][c][1] = sp->uf[sj2][sc2][1];
                }
            }
        }
    }
    /* `smooth=N` (with back=one): the back's depth averaged with its neighbours' N times: one carved surface, not
     * a column's step at every pixel */
    if (backone) {
        int pass, npass = (int)rm_optf(opt, "smooth", 0);

        for (pass = 0; pass < npass; pass++) {
            double (*t2)[SPX] = sp->dist2;

            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    double sum = 0.0;
                    int nn = 0, dy, dx;

                    t2[j][c] = sp->zb[j][c];
                    if (!sp->in[j][c]) {
                        continue;
                    }
                    for (dy = -1; dy <= 1; dy++) {
                        for (dx = -1; dx <= 1; dx++) {
                            if (j + dy >= 0 && j + dy < H && c + dx >= 0 && c + dx < W && sp->in[j + dy][c + dx]) {
                                sum += sp->zb[j + dy][c + dx];
                                nn++;
                            }
                        }
                    }
                    t2[j][c] = sum / nn;
                }
            }
            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    if (sp->in[j][c]) {
                        sp->zb[j][c] = t2[j][c] > 1.0 - sp->zf[j][c] ? t2[j][c] : 1.0 - sp->zf[j][c];
                    }
                }
            }
        }
    }
    if (backone) {
        char kb[256], cbuf[16], *kp;
        unsigned crease[1];
        int crj = -1, crc = -1;

        if (rm_colours(rm_opt(opt, "crease", cbuf, sizeof cbuf), crease, 1) == 1) {
            for (j = 0; j < H && crj < 0; j++) {
                for (c = 0; c < W; c++) {
                    if (sp->in[j][c] && sp->col[j][c] == crease[0]) {
                        crj = j;
                        crc = c;
                        break;
                    }
                }
            }
        }
        if (rm_opt(opt, "keep", kb, sizeof kb) != NULL) {
            for (kp = kb; kp != NULL && *kp != '\0'; ) {
                unsigned kc;
                int ka, ke;

                if (sscanf(kp, "%x:%d:%d", &kc, &ka, &ke) == 3) {
                    for (j = ka < 0 ? 0 : ka; j <= ke && j < H; j++) {
                        for (c = 0; c < W; c++) {
                            if (sp->in[j][c] && sp->col[j][c] == kc) {
                                sp->ub[j][c][0] = sp->uf[j][c][0];
                                sp->ub[j][c][1] = sp->uf[j][c][1];
                                sp->kept[j][c] = 1;
                            }
                        }
                    }
                }
                kp = strchr(kp, '+');
                kp = kp != NULL ? kp + 1 : NULL;
            }
        }
        if (crj >= 0) {
            static const int dy[4] = { -1, 1, 0, 0 }, dx[4] = { 0, 0, -1, 1 };

            for (j = 0; j < H; j++) {
                for (c = 0; c < W; c++) {
                    int d, up = 0;

                    if (!sp->in[j][c] || !sp->line[j][c] || sp->level[j][c]) {
                        continue;           /* (a crease is between parts: on a line of the drawing) */
                    }
                    /* (a dip: lower than the back on both sides of it, along a row or a column) */
                    for (d = 0; d < 4; d += 2) {
                        const int y1 = j + dy[d], x1 = c + dx[d], y2 = j + dy[d + 1], x2 = c + dx[d + 1];

                        if (y1 >= 0 && y2 < H && x1 >= 0 && x2 < W && sp->in[y1][x1] && sp->in[y2][x2]
                            && sp->zb[y1][x1] > sp->zb[j][c] + 0.4 && sp->zb[y2][x2] > sp->zb[j][c] + 0.4) {
                            up = 1;
                        }
                    }
                    if (up) {
                        sp->ub[j][c][0] = sp->uf[crj][crc][0];
                        sp->ub[j][c][1] = sp->uf[crj][crc][1];
                    }
                }
            }
        }
    }
    /* where it stands: over the middle of the stand (the box of every triangle that is no card and no sheet on the
     * ground), its lowest row on the card's foot */
    {
        double lo[2] = { 1e30, 1e30 }, hi[2] = { -1e30, -1e30 }, yg = 1e30;

        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                for (k = 0; k < 3; k++) {
                    yg = b[bi].tri[i].p[k][1] < yg ? b[bi].tri[i].p[k][1] : yg;
                }
            }
        }
        for (bi = 0; bi < nb; bi++) {
            for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
                const RmTri *t = &b[bi].tri[i];
                int flat = 1;

                if (t == ct || t == ct + 1) {
                    continue;
                }
                for (k = 0; k < 3; k++) {
                    flat &= t->p[k][1] < yg + 0.5;
                }
                for (k = 0; k < 3 && !flat; k++) {
                    lo[0] = t->p[k][0] < lo[0] ? t->p[k][0] : lo[0];
                    hi[0] = t->p[k][0] > hi[0] ? t->p[k][0] : hi[0];
                    lo[1] = t->p[k][2] < lo[1] ? t->p[k][2] : lo[1];
                    hi[1] = t->p[k][2] > hi[1] ? t->p[k][2] : hi[1];
                }
            }
        }
        cx = lo[0] < hi[0] ? (lo[0] + hi[0]) / 2.0 : foot[0];
        cz = lo[1] < hi[1] ? (lo[1] + hi[1]) / 2.0 : foot[2];
    }
    /*
     * The body: the sprite extruded a little (`slab`: half its thickness, in pixels), its back the same pixels seen
     * from behind.  The front is flat: from the south it is the sprite.  The back is domed a little (`dome`:
     * pixels more in the middle, smoothly less toward the rim, by the figure's own outline) and wears what a back
     * has: `recolor=FROM:TO+..` changes a colour there (eyes, a mouth, teeth: the material); a line of the drawing
     * becomes the material, but where two parts each shaped of their own meet (an arm and the body) the `soften`
     * colour, a crease; the rim's line the `soften` colour; a `keep` colour stays (an ear's pale tip).  The sides
     * are walls round the outline, in the back's colours.
     */
    {
        double zmax = 0.5;
        const double slab = rm_optf(opt, "slab", 1.5), dome = rm_optf(opt, "dome", 2.0), fdome = rm_optf(opt, "fdome", 0.0);
        char rb[256], *rp, sfb[16];
        unsigned sfc[1];
        int sfj = -1, sfi = -1;

        /* the back's colour of each pixel, as a pixel of the picture: (bj, bi) */
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                sp->bj[j][c] = (short)j;
                sp->bi[j][c] = (short)c;
            }
        }
        if (rm_colours(rm_opt(opt, "soften", sfb, sizeof sfb), sfc, 1) == 1) {
            for (j = 0; j < H && sfj < 0; j++) {
                for (c = 0; c < W; c++) {
                    if (sp->in[j][c] && sp->col[j][c] == sfc[0]) {
                        sfj = j;
                        sfi = c;
                        break;
                    }
                }
            }
        }
        if (rm_opt(opt, "recolor", rb, sizeof rb) != NULL) {
            for (rp = rb; rp != NULL && *rp != '\0'; ) {
                unsigned fc, tc;

                if (sscanf(rp, "%x:%x", &fc, &tc) == 2) {
                    int tj = -1, ti = -1;

                    for (j = 0; j < H && tj < 0; j++) {
                        for (c = 0; c < W; c++) {
                            if (sp->in[j][c] && sp->col[j][c] == tc) {
                                tj = j;
                                ti = c;
                                break;
                            }
                        }
                    }
                    for (j = 0; j < H && tj >= 0; j++) {
                        for (c = 0; c < W; c++) {
                            if (sp->in[j][c] && sp->col[j][c] == fc && !sp->kept[j][c]) {
                                sp->bj[j][c] = (short)tj;
                                sp->bi[j][c] = (short)ti;
                            }
                        }
                    }
                }
                rp = strchr(rp, '+');
                rp = rp != NULL ? rp + 1 : NULL;
            }
        }
        /* the lines at the back: the material, a crease, or the rim */
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                int d, rim = 0, pa = -1, two = 0, mj = -1, mi = -1;

                if (!sp->in[j][c] || !sp->line[j][c]) {
                    continue;
                }
                for (d = 0; d < 8; d++) {
                    static const int dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 }, dx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
                    const int ny = j + dy[d], nx = c + dx[d];
                    int q;

                    if (ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx]) {
                        rim |= d < 4;
                        continue;
                    }
                    q = sp->part[ny][nx];
                    if (q >= 0) {
                        if (mj < 0) {
                            mj = sp->bj[ny][nx];
                            mi = sp->bi[ny][nx];
                        }
                        if (sp->pdeep[q] >= 0.0) {
                            two |= pa >= 0 && pa != q;
                            pa = pa < 0 ? q : pa;
                        }
                    }
                }
                if ((rim || two) && sfj >= 0) {
                    sp->bj[j][c] = (short)sfj;
                    sp->bi[j][c] = (short)sfi;
                } else {
                    sp->bj[j][c] = -1;          /* (the material: the back's own colour) */
                }
                (void)mi;
            }
        }
        /*
         * Its depth, in whole pixels: every face is square to an axis, a pixel's column.  In the middle the slab
         * and the domes (the figure's own roundness, zw, as a share of its deepest, rounded to whole pixels: the
         * back `dome` pixels more at most, the front `fdome`); the pixels on the outline one pixel shallower front
         * and back (a pixel-art edge, rounded by a step).
         */
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                zmax = sp->in[j][c] && zw[j][c] > zmax ? zw[j][c] : zmax;
            }
        }
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                const double share = (zw[j][c] - 0.5) / (zmax - 0.5 > 0.1 ? zmax - 0.5 : 0.1);
                int d, rim = 0;

                if (!sp->in[j][c]) {
                    continue;
                }
                for (d = 0; d < 4; d++) {
                    const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);

                    rim |= ny < 0 || ny >= H || nx < 0 || nx >= W || !sp->in[ny][nx];
                }
                /* (in front the body's parts stand out by their own shapes, `part`, in whole pixels; a line of
                 * the drawing between them lies a pixel deeper) */
                {
                    const double rel = sp->line[j][c] ? -1.0 : sp->zf[j][c] - zw[j][c];
                    const double f = slab + rint(fdome * share + rel);

                    sp->zf[j][c] = rim ? slab - 1.0 : (f > slab - 1.0 ? f : slab - 1.0);
                }
                sp->zb[j][c] = rim ? slab - 1.0 : slab + rint(dome * share);
            }
        }
        /* `relief=NAME`: the front's depth drawn by hand, a digit a pixel (k_relief_*): 0 the rim's level, a pixel
         * under the slab's front; each more a pixel further out.  A figure's parts as its maker would carve them:
         * few levels, each part one level, no groove at a line */
        {
            char nb2[24];
            const char *const *rl = rm_opt(opt, "relief", nb2, sizeof nb2) == NULL ? NULL
                                    : strcmp(nb2, "statue") == 0 ? k_relief_statue : NULL;

            for (; rl != NULL && *rl != NULL; rl++) {
                int row = 0, first = 0, at = 0;

                if (sscanf(*rl, "%d %d %n", &row, &first, &at) < 2 || row < 0 || row >= H) {
                    continue;
                }
                for (c = first; (*rl)[at + c - first] != '\0' && c < W; c++) {
                    const char ch = (*rl)[at + c - first];

                    if (ch >= '0' && ch <= '9' && c >= 0 && sp->in[row][c]) {
                        sp->zf[row][c] = slab - 1.0 + (ch - '0');
                    }
                }
            }
        }
        for (j = 0; j < H; j++) {
            for (c = 0; c < W; c++) {
                const double x0 = cx + (c - W / 2.0) * s, x1 = x0 + s;
                const double y1 = foot[1] + (jlast + 1 - j) * s, y0 = y1 - s;
                const double zF = cz + sp->zf[j][c] * s, zB = cz - sp->zb[j][c] * s;
                const double *ubk = sp->kept[j][c] || sp->bj[j][c] < 0 ? sp->ub[j][c] : sp->uf[sp->bj[j][c]][sp->bi[j][c]];
                int d;

                if (!sp->in[j][c]) {
                    continue;
                }
                {
                    const double pf[4][3] = { { x0, y1, zF }, { x1, y1, zF }, { x1, y0, zF }, { x0, y0, zF } };
                    const double pb[4][3] = { { x0, y1, zB }, { x1, y1, zB }, { x1, y0, zB }, { x0, y0, zB } };
                    const double nf[3] = { 0, 0, 1 }, nbk[3] = { 0, 0, -1 };

                    face(out, ct->id, ct->rgb, 1.0, pf, sp->uf[j][c], nf);
                    face(out, ct->id, ct->rgb, 0.92, pb, ubk, nbk);
                }
                /* its sides: on the outline the whole column, in the material; where a neighbour is shallower, the
                 * step, in the pixel's own colour in front and its back's colour behind */
                for (d = 0; d < 4; d++) {
                    const int ny = j + (d == 0 ? -1 : d == 1 ? 1 : 0), nx = c + (d == 2 ? -1 : d == 3 ? 1 : 0);
                    const int has = ny >= 0 && ny < H && nx >= 0 && nx < W && sp->in[ny][nx];
                    const double shade = d == 0 ? 1.0 : d == 1 ? 0.72 : 0.86;
                    double n[3] = { 0, 0, 0 }, ea[2], eb[2];

                    if (d == 0) { ea[0] = x0; ea[1] = y1; eb[0] = x1; eb[1] = y1; n[1] = 1; }
                    else if (d == 1) { ea[0] = x0; ea[1] = y0; eb[0] = x1; eb[1] = y0; n[1] = -1; }
                    else if (d == 2) { ea[0] = x0; ea[1] = y0; eb[0] = x0; eb[1] = y1; n[0] = -1; }
                    else { ea[0] = x1; ea[1] = y0; eb[0] = x1; eb[1] = y1; n[0] = 1; }
#define WALL(za_, zb_, uv_, sh_) do { \
        const double pw_[4][3] = { { ea[0], ea[1], (za_) }, { eb[0], eb[1], (za_) }, { eb[0], eb[1], (zb_) }, { ea[0], ea[1], (zb_) } }; \
        face(out, ct->id, ct->rgb, (sh_), pw_, (uv_), n); \
    } while (0)
                    if (!has) {
                        WALL(zB, zF, sp->ub[j][c], shade);          /* (the material; a kept tip) */
                        continue;
                    }
                    /* (a block is its pixel's colour one pixel deep; under that the material) */
                    if (sp->zf[ny][nx] < sp->zf[j][c]) {
                        WALL(zF - s, zF, sp->uf[j][c], shade);
                        if (sp->zf[ny][nx] < sp->zf[j][c] - 1.0) {
                            WALL(cz + sp->zf[ny][nx] * s, zF - s, sp->ub[j][c], shade);
                        }
                    }
                    if (sp->zb[ny][nx] < sp->zb[j][c]) {
                        WALL(zB, cz - sp->zb[ny][nx] * s, ubk, shade * 0.95);
                    }
#undef WALL
                }
            }
        }
    }
    free(sp);
    rm_drop(out, ct[0].id);
    rm_drop(out, ct[1].id);
    if (rm_optf(opt, "stand", 1) != 0.0) {
        rm_turned(b, nb, tex, ntex, "rest=0,copies=0", out);
    }
}
