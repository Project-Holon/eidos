/*
 * engine/world/bmap_leaf.c -- which leaves of the hand-made grass a view's sprites move in depth (bmap_leaf.h).
 */

#include "bmap_leaf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bmap_leaf_rule(void)
{
    static int on = -1;

    if (on < 0) {
        const char *e = getenv("BMAP_GRASS_LEAFRULE");

        on = e == NULL || (e[0] != '0' && e[0] != '\0');     /* (on; BMAP_GRASS_LEAFRULE=0: the flat cut) */
    }
    return on;
}

/* The depth of the point `off` units nearer the camera than sprite i's place, along the camera's line of sight (the
 * sprite's own polygons are written 0.3 nearer than its place); 0 for a sprite the map never covers. */
static float place_depth(const BmapLeafView *v, const float r[4][4], unsigned i, float off)
{
    float fwd[3], l, d, p[3], c[4], z;
    unsigned k;

    for (k = 0; k < 3; k++) {
        fwd[k] = v->target[k] - v->eye[k];
    }
    l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (i >= v->np || l < 1e-6f || !v->covered[i] || v->poke[i][3] <= 0.0f) {
        return 0.0f;
    }
    d = ((v->poke[i][0] - v->eye[0]) * fwd[0] + (v->poke[i][1] - v->eye[1]) * fwd[1] + (v->poke[i][2] - v->eye[2]) * fwd[2]) / l;
    if (d - 0.3f < v->znear * 1.01f || d - off < v->znear * 1.01f) {
        return 0.0f;
    }
    for (k = 0; k < 3; k++) {
        p[k] = v->eye[k] + fwd[k] / l * (d - off);
    }
    for (k = 0; k < 4; k++) {
        c[k] = r[k][0] * p[0] + r[k][1] * p[1] + r[k][2] * p[2] + r[k][3];
    }
    z = c[3] > 1e-6f ? 0.5f * (c[2] / c[3]) + 0.5f : 0.0f;
    return z < 1e-6f ? 0.0f : z > 1.0f ? 1.0f : z;
}

static struct {
    const BmapScene *sc;
    const BmapLeaf *leaf;
    unsigned gen, n_leaf, n, cap;
    int valid;
    BmapLeafView v;
    float r[4][4];
    BmapLeafItem *it;
} g_plan;

const BmapLeafItem *bmap_leaf_plan(const BmapScene *sc, unsigned gen, const BmapLeafView *v, const float r[4][4], unsigned *n)
{
    float before[BMAP_LEAF_SPRITES], behind[BMAP_LEAF_SPRITES], cx[BMAP_LEAF_SPRITES], cz[BMAP_LEAF_SPRITES];
    const unsigned np = v->np < BMAP_LEAF_SPRITES ? v->np : BMAP_LEAF_SPRITES;
    unsigned li, k, live = 0;

    *n = 0;
    if (sc == NULL || sc->leaf == NULL || sc->n_leaf == 0u || np == 0u) {
        return NULL;
    }
    if (g_plan.valid && g_plan.sc == sc && g_plan.leaf == sc->leaf && g_plan.gen == gen && g_plan.n_leaf == sc->n_leaf
        && g_plan.v.np == v->np && g_plan.v.znear == v->znear && memcmp(g_plan.v.eye, v->eye, sizeof v->eye) == 0
        && memcmp(g_plan.v.target, v->target, sizeof v->target) == 0
        && memcmp(g_plan.v.poke, v->poke, np * sizeof v->poke[0]) == 0 && memcmp(g_plan.v.covered, v->covered, np) == 0
        && memcmp(g_plan.r, r, sizeof g_plan.r) == 0) {
        *n = g_plan.n;
        return g_plan.n > 0u ? g_plan.it : NULL;
    }
    g_plan.valid = 0;
    g_plan.n = 0;
    for (k = 0; k < np; k++) {
        const float dx = v->eye[0] - v->poke[k][0], dz = v->eye[2] - v->poke[k][2];
        const float cl = sqrtf(dx * dx + dz * dz);

        /* (a leaf over the sprite must lie before 0.6 nearer than its place; a leaf under it behind 0.05 nearer:
         * still before the ground at its feet) */
        before[k] = place_depth(v, r, k, 0.6f);
        behind[k] = place_depth(v, r, k, 0.05f);
        if (before[k] <= 0.0f || behind[k] <= 0.0f || cl < 1e-3f) {
            before[k] = behind[k] = 0.0f;       /* (a sprite the map never covers) */
            cx[k] = cz[k] = 0.0f;
        } else {
            cx[k] = dx / cl;
            cz[k] = dz / cl;
            live++;
        }
    }
    for (li = 0; live > 0u && li < sc->n_leaf; li++) {
        const BmapLeaf *lf = &sc->leaf[li];
        float lo = 0.0f, hi = 1.0f, dmin = 1.0f, dmax = 0.0f;
        unsigned q;

        if (lf->count == 0u || lf->first + lf->count > sc->nv) {
            continue;
        }
        for (k = 0; k < np; k++) {
            const float *pp = v->poke[k];
            const float hx = lf->heart[0] - pp[0], hz = lf->heart[1] - pp[2];
            float val;

            if (before[k] <= 0.0f) {
                continue;
            }
            /* (too far to the side of the line from the eye to stand over or under its picture) */
            if (fabsf(hz * cx[k] - hx * cz[k]) > pp[3] + lf->reach + 1.0f) {
                continue;
            }
            val = sqrtf(hx * hx + hz * hz) > lf->reach ? hx * cx[k] + hz * cz[k]
                                                       : (lf->tip[0] - pp[0]) * cx[k] + (lf->tip[2] - pp[2]) * cz[k];
            if (val > 0.0f) {
                hi = before[k] < hi ? before[k] : hi;
            } else {
                lo = behind[k] > lo ? behind[k] : lo;
            }
        }
        if (lo <= 0.0f && hi >= 1.0f) {
            continue;
        }
        for (q = lf->first; q < lf->first + lf->count; q++) {
            const BmapVertex *p = &sc->v[q];
            const float c2 = r[2][0] * p->x + r[2][1] * p->y + r[2][2] * p->z + r[2][3];
            const float c3 = r[3][0] * p->x + r[3][1] * p->y + r[3][2] * p->z + r[3][3];
            const float z = c3 > 1e-6f ? 0.5f * (c2 / c3) + 0.5f : 0.0f;

            dmin = z < dmin ? z : dmin;
            dmax = z > dmax ? z : dmax;
        }
        if (dmin >= lo && dmax <= hi) {
            continue;                   /* (its own depth already agrees with the rule) */
        }
        if (g_plan.n == g_plan.cap) {
            const unsigned nc = g_plan.cap ? g_plan.cap * 2u : 256u;
            BmapLeafItem *ni = (BmapLeafItem *)realloc(g_plan.it, nc * sizeof *ni);

            if (ni == NULL) {
                break;
            }
            g_plan.it = ni;
            g_plan.cap = nc;
        }
        g_plan.it[g_plan.n].leaf = li;
        if (dmin < lo && lo < hi) {
            /* under a sprite, part of it nearer than that sprite: whole just behind the sprite (and so still before
             * a farther sprite it is over) */
            g_plan.it[g_plan.n].under = 1;
            g_plan.it[g_plan.n].at = lo;
        } else {
            /* over a sprite, part of it farther than that sprite: whole just before the sprite, as well as at its
             * own depth; the nearer of the two holds, so the part that reaches far before the sprite still hides
             * what lies under it (the sprite's shadow) */
            g_plan.it[g_plan.n].under = 0;
            g_plan.it[g_plan.n].at = hi;
        }
        g_plan.n++;
    }
    if (getenv("BMAP_LEAF_LOG") != NULL) {
        /* (research: this view's plan in a line: how many leaves the rule moves, how many of them under a sprite,
         * and a sum that changes when a leaf changes sides; a line only when one of the three changes) */
        static unsigned last_n = ~0u, last_under, last_sum;
        unsigned under = 0, sum = 0, q;

        for (q = 0; q < g_plan.n; q++) {
            under += g_plan.it[q].under;
            sum = sum * 31u + g_plan.it[q].leaf * 2u + g_plan.it[q].under;
        }
        if (g_plan.n != last_n || under != last_under || sum != last_sum) {
            fprintf(stderr, "bmap_leaf: plan %u leaves, %u under a sprite, sum %08X; sprites %u, first at %.3f %.3f %.3f\n", g_plan.n,
                    under, sum, v->np, v->np > 0u ? (double)v->poke[0][0] : 0.0, v->np > 0u ? (double)v->poke[0][1] : 0.0,
                    v->np > 0u ? (double)v->poke[0][2] : 0.0);
            last_n = g_plan.n;
            last_under = under;
            last_sum = sum;
        }
    }
    g_plan.sc = sc;
    g_plan.leaf = sc->leaf;
    g_plan.gen = gen;
    g_plan.n_leaf = sc->n_leaf;
    g_plan.v = *v;
    memcpy(g_plan.r, r, sizeof g_plan.r);
    g_plan.valid = 1;
    *n = g_plan.n;
    return g_plan.n > 0u ? g_plan.it : NULL;
}

unsigned bmap_leaf_seek(const BmapLeafItem *it, unsigned n, unsigned leaf)
{
    unsigned lo = 0, hi = n;

    while (lo < hi) {
        const unsigned mid = lo + (hi - lo) / 2u;

        if (it[mid].leaf < leaf) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}
