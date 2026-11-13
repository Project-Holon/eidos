/*
 * engine/world/remade/plant.c -- a picture card over a stand: the potted
 * plant (building model 19 of the open-air pack flag 0: 69 copies, in houses,
 * shops and gates), the flowers in a pot, the statue on its pedestal.
 *
 * The map has a stand without a back, and over it one leaning card with the
 * picture.  Remade:
 *   - the picture is a body of pixel-sized blocks, as the rocks are (rock.c):
 *     its south side is the game's picture, its east, north and west sides
 *     are pictures drawn by hand (rock_faces.c), and it stands upright over
 *     the middle of the stand (the card leans back from the stand's front
 *     edge only so the flat picture shows from the field's camera);
 *   - the stand is closed: it has every side but the one straight north,
 *     which is its south side's twin (turned.c, step 2).
 *
 * Settings: passed on to the rock builder (faces, centre, ...); without any,
 * the potted plant's.
 */

#include <stdio.h>
#include <string.h>

#include "remade.h"

void rm_plant(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    char ro[256];

    snprintf(ro, sizeof ro, "cards=1,sink=0,high=1,%s", opt != NULL && opt[0] != '\0' ? opt : "faces=plant,shift=-0.15:-2.3");
    rm_rock(b, nb, tex, ntex, ro, out);
    /* (`roundpot=1`: the stand is a round pot: its far side is its near side turned about its axis, lit as the
     * game lights it; without, each of its pieces is closed with a twin) */
    rm_turned(b, nb, tex, ntex, rm_optf(opt, "roundpot", 0) != 0.0 ? "rest=0,round=1" : "rest=0,copies=0", out);
}
