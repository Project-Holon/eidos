/*
 * engine/world/remade/chandelier.c -- a candle chandelier (and the candles
 * standing on the floor under it): a gold ring on arms round a turned column,
 * hung from a chain, a candle on a dish at each arm.
 *
 * The map draws for the field's camera, from the front and above: the frame's
 * far side is missing, and each candle is three cards leaning back toward
 * that camera (the wax on its dish, the flame, a glow round the flame).
 * Remade:
 *   - the frame, the column, the dishes, the chain: each piece is round about
 *     its own upright axis, its far side its near side turned (turned.c,
 *     round=2);
 *   - each wax card is a candle of pixel blocks, its picture wrapped round it
 *     (rock.c, as a vase is turned on a wheel);
 *   - each flame stands upright on its foot, seen from both sides, and a
 *     second flame crossed with it at a right angle: from any side a flame;
 *   - each glow stands upright on its foot, seen from both sides.
 *
 * Settings: candle=TEX, flame=TEX, glow=TEX: the textures of the three kinds
 * of card (the model's own numbers).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

/* The pieces of a draw: triangles that share a corner.  A piece's number for each triangle; how many. */
static unsigned pieces_of(const RmBatch *bt, int *piece)
{
    unsigned i, j, np = 0;
    int k, l;

    for (i = 0; i < bt->n; i++) {
        piece[i] = -1;
    }
    for (i = 0; i < bt->n; i++) {
        int changed = 1;

        if (piece[i] >= 0) {
            continue;
        }
        piece[i] = (int)np;
        while (changed) {
            changed = 0;
            for (j = 0; j < bt->n; j++) {
                unsigned m;

                if (piece[j] >= 0) {
                    continue;
                }
                for (m = 0; m < bt->n && piece[j] < 0; m++) {
                    if (piece[m] != (int)np) {
                        continue;
                    }
                    for (k = 0; k < 3 && piece[j] < 0; k++) {
                        for (l = 0; l < 3; l++) {
                            if (fabs(bt->tri[j].p[k][0] - bt->tri[m].p[l][0]) < 0.05 && fabs(bt->tri[j].p[k][1] - bt->tri[m].p[l][1]) < 0.05
                                && fabs(bt->tri[j].p[k][2] - bt->tri[m].p[l][2]) < 0.05) {
                                piece[j] = (int)np;
                                changed = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
        np++;
    }
    return np;
}

/* A card stood upright about its foot (its two lowest corners): each corner as far along the foot and as far
 * from it as it was, straight up.  `cross`: then turned a right angle about the upright through the card's middle. */
static void upright(const RmTri *t, const double f0[3], const double f1[3], int cross, double cxm, double czm, double out3[3][3])
{
    double e[3] = { f1[0] - f0[0], f1[1] - f0[1], f1[2] - f0[2] };
    int k, v;

    rm_norm(e);
    for (v = 0; v < 3; v++) {
        double d[3], along, perp[3], up;

        for (k = 0; k < 3; k++) {
            d[k] = t->p[v][k] - f0[k];
        }
        along = rm_dot(d, e);
        for (k = 0; k < 3; k++) {
            perp[k] = d[k] - along * e[k];
        }
        up = sqrt(rm_dot(perp, perp));
        for (k = 0; k < 3; k++) {
            out3[v][k] = f0[k] + along * e[k];
        }
        out3[v][1] += up;
        if (cross) {
            const double x = out3[v][0] - cxm, z = out3[v][2] - czm;

            out3[v][0] = cxm - z;
            out3[v][2] = czm + x;
        }
    }
}

void rm_chandelier(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const int tc = (int)rm_optf(opt, "candle", -1), tf = (int)rm_optf(opt, "flame", -1), tg = (int)rm_optf(opt, "glow", -1);
    RmBatch *rest = (RmBatch *)calloc(nb + 1u, sizeof *rest);
    unsigned bi, nr = 0, made0 = 0, made1 = 0, nc = 0;
    double foot0[3] = { 0.0, 0.0, 0.0 }, h0 = 0.0;
    /* each candle: where it stands (the middle of its dish), its top, and how far its flame was moved */
    double cand[64][4];
    int pass, moved[64];

    if (rest == NULL) {
        out->fail = 1;
        return;
    }
    /* the frame and all else that is no card: each piece round about its own axis */
    for (bi = 0; bi < nb; bi++) {
        if (b[bi].tex != tc && b[bi].tex != tf && b[bi].tex != tg) {
            rest[nr++] = b[bi];
        }
    }
    rm_turned(rest, nr, tex, ntex, "rest=0,round=2", out);
    free(rest);
    /* (the candles first: a flame and a glow are set on the candle under them) */
    for (pass = 0; pass < 2; pass++)
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        int *piece;
        RmTri *one;
        unsigned np, p, pi, tp, i;

        if (bt->bb || (pass == 0 ? bt->tex != tc : bt->tex != tf && bt->tex != tg)) {
            continue;
        }
        piece = (int *)malloc((bt->n + 1u) * sizeof *piece);
        one = (RmTri *)malloc((bt->n + 1u) * sizeof *one);
        if (piece == NULL || one == NULL) {
            free(piece);
            free(one);
            out->fail = 1;
            return;
        }
        np = pieces_of(bt, piece);
        /* (the candles: the first made is a whole card, the piece of the most triangles: a card cut where it meets
         * the column is a part of the picture only) */
        {
            unsigned best = 0, cnt;

            tp = 0;
            for (p = 0; bt->tex == tc && p < np; p++) {
                for (i = 0, cnt = 0; i < bt->n; i++) {
                    cnt += piece[i] == (int)p;
                }
                if (cnt > best) {
                    best = cnt;
                    tp = p;
                }
            }
        }
        for (pi = 0; pi < np; pi++) {
            unsigned n1 = 0;

            p = pi == 0 ? tp : pi <= tp ? pi - 1 : pi;
            double f0[3], f1[3], lo1 = 1e30, cxm = 0.0, czm = 0.0, czu = 0.0, shift[3] = { 0.0, 0.0, 0.0 };
            int lo = -1, lo2 = -1, cn[4] = { -1, -1, -1, -1 };     /* bottom left, bottom right, top right, top left */

            for (i = 0; i < bt->n; i++) {
                if (piece[i] == (int)p) {
                    one[n1++] = bt->tri[i];
                }
            }
            /* the card's four corners: the ends of its bottom edge and of its top edge (a card may be cut into
             * many triangles: its two lowest points are then neighbours on the bottom edge, not its ends); its
             * foot is its bottom edge, its middle the middle of its corners */
            {
                unsigned v;
                double hi1 = -1e30;

                for (v = 0; v < n1 * 3u; v++) {
                    const float *q = one[v / 3u].p[v % 3u];

                    lo1 = q[1] < lo1 ? q[1] : lo1;
                    hi1 = q[1] > hi1 ? q[1] : hi1;
                }
                for (v = 0; v < n1 * 3u; v++) {
                    const float *q = one[v / 3u].p[v % 3u];
                    const int bot = q[1] < lo1 + 0.3, top = q[1] > hi1 - 0.3;

                    if (bot && (cn[0] < 0 || q[0] < one[cn[0] / 3].p[cn[0] % 3][0])) cn[0] = (int)v;
                    if (bot && (cn[1] < 0 || q[0] > one[cn[1] / 3].p[cn[1] % 3][0])) cn[1] = (int)v;
                    if (top && (cn[2] < 0 || q[0] > one[cn[2] / 3].p[cn[2] % 3][0])) cn[2] = (int)v;
                    if (top && (cn[3] < 0 || q[0] < one[cn[3] / 3].p[cn[3] % 3][0])) cn[3] = (int)v;
                }
                if (cn[0] < 0 || cn[1] < 0 || cn[2] < 0 || cn[3] < 0 || cn[0] == cn[1]) {
                    continue;
                }
                for (v = 0; v < 3u; v++) {
                    f0[v] = one[cn[0] / 3].p[cn[0] % 3][v];
                    f1[v] = one[cn[1] / 3].p[cn[1] % 3][v];
                }
                for (v = 0; v < 4u; v++) {
                    cxm += one[cn[v] / 3].p[cn[v] % 3][0] / 4.0;
                    czm += one[cn[v] / 3].p[cn[v] % 3][2] / 4.0;
                }
                f1[1] = f0[1] = (f0[1] + f1[1]) / 2.0;
                (void)lo;
                (void)lo2;
            }
            if (bt->tex == tc) {
                /* a candle: a body turned on a wheel, standing in the middle of its dish (under the card's middle:
                 * the card leans back from the dish's front lip).  The candles are one picture: the first is made,
                 * the others are copies of it at their own places (a remade model has eight textures of its own,
                 * a body of the rock builder one) */
                const double foot[3] = { cxm, f0[1], czm };

                if (made1 == made0) {
                    /* (the card as one quad of its four corners, for the rock builder: it reads a card's foot and
                     * width from its lowest corners) */
                    RmBatch cb = *bt;
                    RmTri quad[2];
                    static const int qi[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
                    int t2, v2;

                    for (t2 = 0; t2 < 2; t2++) {
                        quad[t2] = one[0];
                        for (v2 = 0; v2 < 3; v2++) {
                            const int src = cn[qi[t2][v2]];

                            memcpy(quad[t2].p[v2], one[src / 3].p[src % 3], sizeof quad[t2].p[v2]);
                            memcpy(quad[t2].uv[v2], one[src / 3].uv[src % 3], sizeof quad[t2].uv[v2]);
                        }
                    }
                    cb.tri = quad;
                    cb.n = 2;
                    for (i = 0; i < n1; i++) {
                        rm_drop(out, one[i].id);
                    }
                    made0 = out->ntri;
                    rm_rock(&cb, 1, tex, ntex, "lean=1,wrap=lathe,around=4,sink=0,shadow=0,top=0", out);
                    made1 = out->ntri;
                    /* (the rock builder stands it at the card's foot: moved to the dish's middle; its height) */
                    {
                        const double dx = cxm - (f0[0] + f1[0]) / 2.0, dz = czm - (f0[2] + f1[2]) / 2.0;
                        unsigned q;
                        int v;

                        for (q = made0; q < made1; q++) {
                            for (v = 0; v < 3; v++) {
                                out->tri[q].p[v][0] += (float)dx;
                                out->tri[q].p[v][2] += (float)dz;
                                h0 = out->tri[q].p[v][1] - f0[1] > h0 ? out->tri[q].p[v][1] - f0[1] : h0;
                            }
                        }
                    }
                    memcpy(foot0, foot, sizeof foot0);
                } else {
                    unsigned q;

                    for (q = made0; q < made1; q++) {
                        const RmNew t = out->tri[q];
                        double c3[3][3], uv[3][2];
                        int v, k;

                        for (v = 0; v < 3; v++) {
                            for (k = 0; k < 3; k++) {
                                c3[v][k] = t.p[v][k] + (foot[k] - foot0[k]);
                            }
                            uv[v][0] = t.uv[v][0];
                            uv[v][1] = t.uv[v][1];
                        }
                        rm_tri(out, t.src, t.tex, t.cull, t.rgb, c3[0], c3[1], c3[2], uv[0], uv[1], uv[2], t.kind, t.alpha);
                        if (!out->fail) {
                            out->tri[out->ntri - 1].lit = t.lit;
                        }
                    }
                    for (i = 0; i < n1; i++) {
                        rm_drop(out, one[i].id);
                    }
                }
                if (made1 > made0 && nc < 64u) {
                    cand[nc][0] = foot[0];
                    cand[nc][1] = foot[1] + h0;
                    cand[nc][2] = foot[2];
                    cand[nc][3] = 0.0;
                    moved[nc] = 0;
                    nc++;
                }
                continue;
            }
            /* a flame, a glow: on the candle under it (the nearest), centred on it; a flame's foot at the candle's
             * top, a glow moved up as its flame was */
            {
                unsigned q, k = 0;
                double bd = 1e30;

                for (q = 0; q < nc; q++) {
                    const double d = (cand[q][0] - cxm) * (cand[q][0] - cxm) + (cand[q][2] - czm) * (cand[q][2] - czm);

                    if (d < bd) {
                        bd = d;
                        k = q;
                    }
                }
                /* (stood upright, a card stands on its foot: its middle from north to south is its foot's) */
                czu = (f0[2] + f1[2]) / 2.0;
                if (nc > 0) {
                    shift[0] = cand[k][0] - cxm;
                    shift[2] = cand[k][2] - czu;
                    if (bt->tex == tf || !moved[k]) {
                        shift[1] = cand[k][1] - 0.5 - f0[1];
                        if (bt->tex == tf) {
                            cand[k][3] = shift[1];
                            moved[k] = 1;
                        }
                    } else {
                        shift[1] = cand[k][3];
                    }
                }
            }
            for (i = 0; i < n1; i++) {
                double c3[3][3], uv[3][2];
                int v, cross;

                for (v = 0; v < 3; v++) {
                    uv[v][0] = one[i].uv[v][0];
                    uv[v][1] = one[i].uv[v][1];
                }
                rm_drop(out, one[i].id);
                for (cross = 0; cross <= (bt->tex == tf ? 1 : 0); cross++) {
                    upright(&one[i], f0, f1, cross, cxm, czu, c3);
                    for (v = 0; v < 3; v++) {
                        c3[v][0] += shift[0];
                        c3[v][1] += shift[1];
                        c3[v][2] += shift[2];
                    }
                    /* (shown from both sides: a flame and a glow are alike front and back) */
                    rm_tri(out, one[i].id, -1, 0, one[i].rgb, c3[0], c3[1], c3[2], uv[0], uv[1], uv[2], 0, 0);
                }
            }
        }
        free(piece);
        free(one);
    }
}
