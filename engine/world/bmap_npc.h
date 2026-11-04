/*
 * engine/world/bmap_npc.h -- the people a zone's event data places, as the
 * field draws them: each NPC's billboard picture, standing, facing the
 * direction its header gives, at its position (the battle spot editor shows
 * them over the map).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_NPC_H
#define EIDOS_ENGINE_WORLD_BMAP_NPC_H

#include "bmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BmapNpc {
    float x, y, z;          /* map units (fx32 / 4096), the game's own axes: the feet,
                               with the object code's own offset added, as the field
                               places the billboard                             */
    int dir;                /* 0 up, 1 down, 2 left, 3 right (the header's)     */
    int w, h;               /* the picture in pixels (0 when there is none)    */
    unsigned char *rgba;    /* w*h*4, straight alpha, the picture facing `dir`
                               (already mirrored where the field mirrors it);
                               NULL for a 3D model, an invisible object or an
                               event-variable alias; owned                      */
    float world_w, world_h; /* the billboard's size in map units: it is centred on
                               x and stands on y                                */
    int model_file, model_tex; /* a drawn 3D object (rgba is NULL): the a/0/4/9 members
                               of its BMD0 and its BTX0 (-1: textures inside the
                               model); both -1 for a billboard or nothing        */
    int gx, gz;             /* the header's tile                                */
    unsigned obj_code;
    int is_trainer;         /* event type 1 */
    int resident;           /* a Black City or White Forest resident (the save
                               places them, bmap_npc_add_residents): y is not
                               known, the ground under it is               */
} BmapNpc;

/* The NPCs of `zone` (grid placed ones; rail placed ones are left out).  On
 * success returns 0 and *out is malloc'd (NULL when *count is 0). */
int  bmap_npc_load(unsigned zone, BmapNpc **out, int *count);
/* One standing picture of any object code facing `dir` (the player's, 1 hero
 * and 4 heroine, too): *rgba is malloc'd, w*h*4, and the billboard is
 * world_w x world_h map units, standing on its position.  0 on success; -1
 * for a 3D object, an invisible one or an event-variable alias. */
int  bmap_npc_picture(unsigned obj_code, int dir, unsigned char **rgba, int *w, int *h,
                      float *world_w, float *world_h);
/* Adds the 3D objects of `npcs` (those with model_file >= 0) to a scene just
 * built by bmap_build (not a bmap_copy): vertices, draws and textures are
 * appended and belong to the scene.  Call it before the scene is copied or
 * placed.  Returns 0 when every model was added. */
int  bmap_npc_add_models(BmapScene *s, const BmapNpc *npcs, int n);
void bmap_npc_free(BmapNpc *npcs, int count);
/* Black City (zone 0) and White Forest (zone 424) have no people in their
 * event files: the save's residents are placed by the game (ov020
 * field_wfbc_data_local.c) on the ten tiles of a table in a/1/5/4 (member
 * 0x22 Black City, 0x23 White Forest), each with the object code of its
 * record (members 0 to 29).  This appends ten residents on those tiles, as
 * the place looks once it is full: one record each, in order (Black City
 * records 0 to 9, White Forest 15 to 24), for a tool that has no save (the
 * spot editor).  0 on success, also for any other zone (nothing added). */
int  bmap_npc_add_residents(unsigned zone, BmapNpc **npcs, int *count);

#ifdef __cplusplus
}
#endif

#endif
