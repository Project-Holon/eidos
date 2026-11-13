/*
 * engine/world/remade/plant.c -- the potted plant (building model 19 of the
 * open-air pack flag 0: 69 copies, in houses, shops and gates).
 *
 * The map has a pot without a back, and over it one leaning card with the
 * plant's picture.  Remade:
 *   - the foliage and the trunk are a body of pixel-sized blocks, as the
 *     rocks are (rock.c): its south side is the game's picture, its east,
 *     north and west sides are pictures drawn by hand (rock_faces.c, the
 *     plant's), and it stands upright over the middle of the pot's rim (the
 *     card leans back from the pot's front edge);
 *   - the pot is closed: it has every side but the one straight north,
 *     which is its south side's twin (turned.c, step 2).
 */

#include "remade.h"

void rm_plant(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    (void)opt;
    rm_rock(b, nb, tex, ntex, "cards=1,sink=0,high=1,faces=plant,shift=-0.15:-2.3", out);
    rm_turned(b, nb, tex, ntex, "rest=0,copies=0", out);
}
