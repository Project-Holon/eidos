/*
 * engine/world/bmap_gimmick.h -- the models a gym's field gimmick adds to
 * its map (bmap_gimmick.c), for the battle backdrop (docs/battle-backdrop.md).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_GIMMICK_H
#define EIDOS_ENGINE_WORLD_BMAP_GIMMICK_H

#ifdef __cplusplus
extern "C" {
#endif

/* One expanded object (FLD_EXP_OBJ) of a gimmick, in its initial state. */
typedef struct BmapGimmickModel {
    const char *arc;    /* archive path of the unit, e.g. "a/1/2/2"              */
    unsigned model;     /* member that holds the BMD0                            */
    int tex;            /* member with a separate BTX0; -1: the BMD0's own       */
    float x, y, z;      /* world position, map units (the game's fx32 / 4096)    */
    float turns;        /* yaw, full turns (0 unless the gimmick turns it)       */
    int anim;           /* member with a BCA0 pose for the node transforms, -1:
                           the model's own pose                                  */
    int frame;          /* frame of that pose, -1: its last frame                */
} BmapGimmickModel;

/* The models to add to `zone`'s scene.  Returns their count (0 for a zone
 * without a gimmick) and sets *out to the first. */
unsigned bmap_gimmick_models(unsigned zone, const BmapGimmickModel **out);

/* A floor a gimmick lays over its map (the mapper's extra heights): a rectangle of tiles at a height, or at one of
 * two (a lift: where it stands in each of its states). */
typedef struct BmapGimmickFloor {
    int tx, tz, w, d;       /* tiles */
    float y[2];             /* map units */
    int lift;               /* 1: a lift (two heights); 0: a fixed floor (y[1] = y[0]) */
} BmapGimmickFloor;

/* The floors `zone`'s gimmick lays over its map, read from the game's own tables (Driftveil Gym: overlay 43's
 * fixed areas and lift zones).  Returns their count (max at most): 0 for a zone without any, or when the overlay
 * cannot be read. */
unsigned bmap_gimmick_floors(unsigned zone, BmapGimmickFloor *out, unsigned max);

#ifdef __cplusplus
}
#endif

#endif
