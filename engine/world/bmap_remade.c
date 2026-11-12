/*
 * engine/world/bmap_remade.c -- the table of remade models (bmap_remade.h):
 * one line a model, its builder and the builder's settings for it.
 */

#include <string.h>

#include "bmap_remade.h"

/* the warp pads' rings: the height of each grey of the picture, the rim lowest and the bright ring highest */
#define PAD_RISE "rise=BDC5CD:0.6+949494:1.2+9C9C9C:1.5+ACACAC:1.8+BDBDBD:2.1+CDCDD5:2.4+CDCDCD:2.4"

/* the rocks: `key` is the shadow painted round the foot (in water: the ripple's colours, from the rock outward) */
#define ROCK_CAVE  "key=313131,faces=cave"
#define ROCK_OPEN  "key=6A6A7B,faces=open"
#define ROCK_WATER "key=ACE6FF+6AA4C5+52839C+41627B,ripple=1,faces=water"

static const BmapRemade k_remade[] = {
    { "b0_00040", rm_rock, ROCK_CAVE },             /* small cave rock */
    { "b0_00039", rm_rock, ROCK_CAVE },             /* cave rock */
    { "b1_00258", rm_rock, ROCK_OPEN },             /* small open-air rock */
    { "b1_00021", rm_rock, ROCK_OPEN },             /* open-air rock */
    { "b1_00055", rm_rock, ROCK_WATER },            /* small rock in water */
    { "b1_00054", rm_rock, ROCK_WATER },            /* rock in water */
    { "b0_00043", rm_rock,                          /* vase on a pedestal (Relic Castle): four bosses round it */
      "lean=1,mouth=8,sink=0,high=1.7,bump=1.5,fat=1,rimw=1,lip=2,wrap=lathe,around=4,reach=6,boss=C5AC94+DEBDA4+E6CDB4+F6DECD" },
    { "b0_00094", rm_cushion, "" },                 /* pouffe, orange (living rooms) */
    { "b0_00095", rm_cushion, "" },                 /* pouffe, blue (living rooms) */
    { "b0_00029", rm_bottles,                       /* bottles, one standing and one lying behind it (Dreamyard) */
      "stand=4:0:1+1+1+1+2+3+3+3+3+3+3+3+3+3,lie=8:8:15:3,own=523939+9C7B52+6A5252,line=414141+393939+313131+395A5A" },
    { "b1_00192", rm_disc, PAD_RISE },              /* warp pad (Pokemon League) */
    { "b0_00108", rm_disc, PAD_RISE },              /* warp pad (Opelucid Gym) */
    { "b1_00294", rm_gate, "depth=2" },             /* barred gate, grey (Pokemon League plaza) */
    { "b0_00005", rm_turned, "" },                  /* table (living rooms, laboratories): its back edge and back legs */
    { "b0_00092", rm_turned, "" },                  /* cardboard box: its north side */
    { "b0_00060", rm_backwall, "" },                /* container (Cold Storage): its far end */
    { "b0_00061", rm_floor, "" },                   /* boxes (Cold Storage) */
    { "b0_00111", rm_floor, "" },                   /* floor glow (Chargestone Cave) */
    { "b0_00112", rm_floor, "" },                   /* floor glow (Chargestone Cave) */
};

const BmapRemade *bmap_remade_find(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof k_remade / sizeof k_remade[0]; i++) {
        if (strcmp(k_remade[i].name, name) == 0) {
            return &k_remade[i];
        }
    }
    return NULL;
}
