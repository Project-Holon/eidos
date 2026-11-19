/*
 * engine/world/bmap_grass.h -- hand-made grass layers of the battle backdrop (research: BMAP_GRASS_FILE).
 *
 * A grass sheet of the map (a flat picture a tile: the tall grass, the long grass) is built in a battle of pieces
 * cut from its picture by hand: each leaf a piece, each piece stood in the tile where a "put" says.  bmap_grass.c
 * reads the layers file, makes each kind's atlas (a picture holding each piece alone) and builds a tile's pieces.
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_GRASS_H
#define EIDOS_ENGINE_WORLD_BMAP_GRASS_H

#include "bmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BmapGrassKind BmapGrassKind;

/* How many grass kinds the layers file holds (0: no file: the game as it is). */
int bmap_grass_count(void);
/* The kind for the grass sheet whose picture is `name`, or NULL. */
const BmapGrassKind *bmap_grass_find(const char *name);
/* Whether the layers file leaves the sheet `name` out (a fringe its pieces replace). */
int bmap_grass_hidden(const char *name);
/* Whether the kind's pieces are drawn again over a Pokemon standing behind them (BmapDraw.grass). */
int bmap_grass_over(const BmapGrassKind *g);
/* The name of the kind's atlas picture in a scene ("NAME~"). */
void bmap_grass_atlas_name(const BmapGrassKind *g, char out[17]);

/* Adds to a scene just built (bmap_build's own, not a copy) the atlas of each kind whose picture the scene holds.
 * 0, or -1 when out of memory (the scene stays as it was). */
int bmap_grass_atlases(BmapScene *s);

/* One triangle of a tile: corners (battle units), texture coordinates in the atlas, its light (of the sheet's). */
typedef void BmapGrassEmit(void *ctx, const float p[3][3], const float uv[3][2], float shade);

/* A leaf begins (the triangles emitted next are its own): its tip (battle units), its bush's heart (x, z) and how
 * far its ring's leaves reach from the heart (battle units). */
typedef void BmapGrassLeafFn(void *ctx, const float tip[3], const float heart[2], float reach);

/* A tile's pieces: the tile's middle (ox, oz) and ground y in battle units; mxv, mzv: the battle's vector for one
 * map unit east and one south; pk: battle units a map unit of height; hash: the tile's own number (each tile's
 * bushes differ); atlas: the kind's atlas picture in the scene; leaf (or NULL): told as each leaf begins. */
void bmap_grass_tile(const BmapGrassKind *g, const BmapTexture *atlas, float ox, float y, float oz, const float mxv[2],
                     const float mzv[2], float pk, unsigned hash, BmapGrassEmit *emit, BmapGrassLeafFn *leaf, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WORLD_BMAP_GRASS_H */
