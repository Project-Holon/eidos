/*
 * engine/world/bmap_remade.h -- which building models of the map the battle
 * backdrop remakes, and with which builder (engine/world/remade/remade.h,
 * docs/battle-backdrop.md).  bmap_data.c asks for a model when it adds it to
 * a scene, runs the builder once on the model standing alone, and applies
 * what it made to every copy.
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_REMADE_H
#define EIDOS_ENGINE_WORLD_BMAP_REMADE_H

#include "remade/remade.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BmapRemade {
    const char *name;           /* "b<the area's indoor flag>_<model id, five digits>" */
    RmBuild *build;
    const char *opt;            /* the builder's settings for this model            */
} BmapRemade;

/* The entry of model `name`, or NULL when the model is not remade. */
const BmapRemade *bmap_remade_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WORLD_BMAP_REMADE_H */
