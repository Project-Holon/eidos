/*
 * engine/world/bmap_leaf.h -- the hand-made grass over a battle's sprites, leaf by leaf (bmap_grass.c's leaves,
 * BmapScene.leaf; research switch BMAP_GRASS_LEAFRULE).
 *
 * The game hides a sprite behind the map by depth: the sprite's polygons write the depth of its place.  A leaf of
 * grass that crosses that depth would be cut along it.  The rule keeps a leaf whole: it is over a sprite or under
 * it, never both.  While the sprite stands outside the reach of the leaf's ring, the whole ring goes by its bush's
 * heart (nearer the eye than the sprite: over it).  Inside the reach, a leaf is over the sprite when its tip is
 * nearer the eye than the sprite's feet.
 *
 * Both 3D renderers lay the map's depth again before the game's sprites with the leaves the rule concerns moved in
 * depth as a whole: a leaf over a sprite is laid a second time just before the sprite's depth; a leaf under a sprite
 * is laid just behind it, and back at its own depth after the sprites (their shadows stay under it).  This file
 * decides which leaves and at which depth; the renderers only draw.
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_LEAF_H
#define EIDOS_ENGINE_WORLD_BMAP_LEAF_H

#include <math.h>

#include "bmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMAP_LEAF_SPRITES 32

/* A camera and the sprites it shows (battle units). */
typedef struct BmapLeafView {
    float eye[3], target[3], znear;
    unsigned np;
    float poke[BMAP_LEAF_SPRITES][4];           /* a sprite's place and its box's height */
    unsigned char covered[BMAP_LEAF_SPRITES];   /* 0: a sprite the map never covers (not listed, a shield) */
} BmapLeafView;

/* A leaf the rule moves in depth. */
typedef struct BmapLeafItem {
    unsigned leaf;              /* its index in BmapScene.leaf */
    float at;                   /* the depth (the depth buffer's, 0..1) it is laid at as a whole */
    unsigned char under;        /* 1: laid there in place of its own depth (under a sprite, part of it nearer);
                                   0: laid there as well as at its own depth (over a sprite, part of it farther) */
} BmapLeafItem;

/* Whether the rule is on (BMAP_GRASS_LEAFRULE). */
int bmap_leaf_rule(void);

/* The leaves of the scene the rule moves for this view, by leaf index ascending; `r` is the view's matrix (clip =
 * r * (x, y, z, 1), depth = 0.5 * z / w + 0.5); `gen` tells one build of the scene from the next.  The answer is
 * kept while the scene, the view and the matrix stay the same.  NULL with *n 0 when there are none. */
const BmapLeafItem *bmap_leaf_plan(const BmapScene *sc, unsigned gen, const BmapLeafView *v, const float r[4][4], unsigned *n);

/* Whether a sphere (centre c, radius rad, battle units) lies wholly outside the part of a view that shows in the
 * rectangle box (x0, y0, x1, y1 in the view's -1..1 coordinates; the whole screen is -1, -1, 1, 1): outside one of
 * the four planes through the eye along the rectangle's sides.  `r` is the view's matrix (clip = r * (x, y, z, 1)).
 * An empty rectangle (x0 > x1) misses everything.  Never says so of a sphere any part of which could show there. */
static inline int bmap_view_misses(const float r[4][4], const float box[4], const float c[3], float rad)
{
    int k;

    if (box[0] > box[2] || box[1] > box[3]) {
        return 1;
    }
    for (k = 0; k < 4; k++) {
        /* (x >= x0 w, x <= x1 w, y >= y0 w, y <= y1 w) */
        const float *ra = r[k < 2 ? 0 : 1];
        const float e = box[k == 0 ? 0 : k == 1 ? 2 : k == 2 ? 1 : 3], sg = (k & 1) ? -1.0f : 1.0f;
        const float a = sg * (ra[0] - e * r[3][0]), b = sg * (ra[1] - e * r[3][1]), cc = sg * (ra[2] - e * r[3][2]),
                    d = sg * (ra[3] - e * r[3][3]);
        const float len = sqrtf(a * a + b * b + cc * cc);

        if (len > 1e-12f && (a * c[0] + b * c[1] + cc * c[2] + d) / len < -rad) {
            return 1;
        }
    }
    return 0;
}

/* The first of `n` items whose leaf is `leaf` or later. */
unsigned bmap_leaf_seek(const BmapLeafItem *it, unsigned n, unsigned leaf);

#ifdef __cplusplus
}
#endif

#endif
