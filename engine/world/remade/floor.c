/*
 * engine/world/remade/floor.c -- a model that the map holds over the floor,
 * stood on it as it is: Cold Storage's boxes (2 units up), Chargestone
 * Cave's floor glows (8 units up).  The field's steep camera never shows the
 * gap; a battle camera low over the floor does.  Nothing of the model is
 * changed: the map builder moves it down as a whole (bmap_place.c
 * rest_bodies).
 */

#include "remade.h"

void rm_floor(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    (void)b;
    (void)nb;
    (void)tex;
    (void)ntex;
    (void)opt;
    out->flags |= RM_FLAG_REST;
}
