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
    { "b1_00016", rm_building,                      /* gym front building (Striaton): its back wears its side's bands, no openings */
      "src=east,plain=-3,flat=1,near=20" },
    { "b1_00018", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00030", rm_building,                      /* kiln (Dreamyard): its back is its brick side, leaning as the sides do */
      "src=east,span=-17:17,near=30,far=30,fit=1,roof=0" },
    { "b1_00039", rm_building,                      /* warehouse (Cold Storage): its front without the two doors and the plate */
      "erase=-48:42:2:51,plain=46" },
    { "b1_00045", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00046", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00049", rm_building,                      /* house (Driftveil): its front without the door, its posts and the sign over it */
      "erase=-23:7:2:34+-20:20:34:44,plain=22" },
    { "b1_00090", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00100", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00122", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00146", rm_building,                      /* stone tower (Route 8): its back is its side */
      "src=east,span=-108:46,near=20,roof=0,twosided=1" },
    { "b1_00148", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00161", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00169", rm_building, "wall=0" },              /* no battle place behind it: its roof and small pieces only */
    { "b1_00177", rm_building,                      /* hut on posts (Abundant Shrine): its front with planks in place of the doors */
      "erase=-9.8:9.8:25:43,plain=-10.6,foot=20,roof=0,twosided=1" },
    { "b1_00243", rm_building,                      /* stone huts (Village Bridge): plain stone, no windows at the back */
      "at=100,erase=-33:33:2:37,plain=34.4,roof=0" },
    { "b0_00019", rm_plant, "" },                   /* potted plant */
    { "b1_00290", rm_turned, "rest=0" },            /* bell arch (Celestial Tower) */
    { "b0_00002", rm_turned, "rest=0,copies=0,plainback=1" },   /* television: the screen and the stand each closed with a plain back */
    { "b0_00011", rm_building, "src=east,flat=1,roof=0,small=0,shade=0.9" },   /* cupboard, wood: a plain back, its side's middle column */
    { "b0_00012", rm_backwall, "" },                /* cupboard, green: a plain back panel in its side's material */
    { "b0_00015", rm_backwall, "" },                /* bookshelf: a plain back panel in its side's material */
    { "b0_00018", rm_building, "src=east,flat=1,roof=0,small=0,shade=0.9" },   /* fridge: a plain back, its side's middle column */
    { "b0_00081", rm_backwall, "" },                /* server cabinet, black: a plain back panel in its side's material */
    { "b0_00082", rm_backwall, "" },                /* server cabinet, grey: a plain back panel in its side's material */
    { "b0_00086", rm_backwall, "" },                /* vending machine: a plain back panel in its side's material */
    { "b0_00126", rm_backwall, "" },                /* lockers: a plain back panel in its side's material */
    { "b1_00000", rm_building,                      /* Pokemon Center: its front with windows where the door and the sign are */
      "roof=0,near=40,far=40,plain=-45,erase=-24:0:2:29:-26+0:24:2:29:26+-60:60:29:45+-13:0:45:70:-26+0:13:45:70:26" },
    { "b1_00005", rm_building,                      /* house with a blue roof: its gable front without the door, its awning and the window */
      "roof=0,over=0,near=30,far=4,plain=7,fill=1,filltop=38,period=6,keep=-30:-23:2:40+21.5:28:2:40+-25.5:23.5:39.5:45+-14.5:-4:46:57.5" },
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
