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
    { "b0_00035", rm_plant, "centre=1,bunch=flowers,front=1,top=0,roundpot=1" },   /* flowers in a pot: a round bunch of blooms, its south the picture */
    { "b0_00079", rm_sprite,                        /* statue: the sprite a little thick, its back the creature's stone back */
      "split=52526A,soften=7B7B7B,back=one,slab=2,dome=2,relief=statue,keep=DEE6E6:2:4,recolor=FFFFFF:B4B4A4+DEE6E6:B4B4A4+7B7B7B:B4B4A4,"
      "part=6:9:0.5:0+10:4:0.5:1+10:15:0.5:1+15:6:0.4:1+15:13:0.4:1+13:9:0.4:0" },
    { "b1_00290", rm_turned, "rest=0" },            /* bell arch (Celestial Tower) */
    { "b0_00002", rm_turned, "rest=0,copies=0,plainback=1" },   /* television: the screen and the stand each closed with a plain back */
    { "b0_00011", rm_building, "src=east,flat=1,roof=0,small=0,shade=0.9" },   /* cupboard, wood: a plain back, its side's middle column */
    { "b0_00012", rm_backwall, "" },                /* cupboard, green: a plain back panel in its side's material */
    { "b0_00015", rm_backwall, "" },                /* bookshelf: a plain back panel in its side's material */
    { "b0_00017", rm_counter,                       /* kitchen counter: back and corner, the sink's wall, the tap and the stove standing up */
      "lift=0:0:0.35:0.43:1.2:2+0.55:0.07:1:0.49:0.5" },
    { "b0_00018", rm_building, "src=east,flat=1,roof=0,small=0,shade=0.9" },   /* fridge: a plain back, its side's middle column */
    { "b0_00081", rm_backwall, "" },                /* server cabinet, black: a plain back panel in its side's material */
    { "b0_00082", rm_backwall, "" },                /* server cabinet, grey: a plain back panel in its side's material */
    { "b0_00086", rm_backwall, "" },                /* vending machine: a plain back panel in its side's material */
    { "b0_00126", rm_backwall, "" },                /* lockers: a plain back panel in its side's material */
    { "b1_00000", rm_building,                      /* Pokemon Center: its front with windows where the door and the sign are */
      "roof=0,near=40,far=40,plain=-45,erase=-24:0:2:29:-26+0:24:2:29:26+-60:60:29:45+-13:0:45:70:-26+0:13:45:70:26" },
    { "b1_00005", rm_building,                      /* house with a blue roof: its gable front without the door, its awning and the window */
      "roof=0,over=0,rim=1,valley=1,near=30,far=4,plain=7,fill=1,filltop=38,period=6,keep=-30:-23:2:40+21.5:28:2:40+-25.5:23.5:39.5:45+-14.5:-4:46:57.5" },
    { "b1_00011", rm_building, "src=east,atleast=45,roof=0,over=0,rim=1,rimat=21,groundwalls=1,ledge=1,gable=-17:-13:13:17:93.6:95.2:2,fill=1,plain=-9,filltop=74,period=8,keep=-7:11.5:12:66+-39:21:74:82,spans=-46:-17:17:-12+17:46:17:-12+-17:17:19.5:-14.5" },   /* tan stone hall, blue roof: its side's stone and windows at the back */
    { "b1_00013", rm_building, "src=east,span=20:-46,roof=0,over=0,rim=1,rimat=22,rimdrop=6,groundwalls=1,twin=38:40,plain=0,erase=-35:5:28:72,door=20:-34:-12:39:58:0.083:0.002:0.155:0.65" },   /* brick warehouse with an outside stair: its side's brick at the back */
    { "b1_00014", rm_building, "src=east,span=18:-43,roof=0,over=0,rim=1,rimat=22,rimdrop=6,groundwalls=1,pipes=2.4:55:0.18:0.5" },   /* brick warehouse, blue doors: its side's brick at the back */
    { "b1_00015", rm_building, "src=east,span=18:-43,roof=0,over=0,rim=1,rimat=22,rimdrop=6,groundwalls=1,twin=38:40" },   /* brick warehouse, green doors: its side's brick at the back */
    { "b1_00040", rm_hall, "corner=8.4,tanks=0:6:0.8,pipe=2.2" },   /* warehouse, glass roof: its sides' wall at the back, the roof's end, six copper tanks */
    { "b1_00058", rm_tower, "corner=0,mirror=-40:40:40:70:0+-31:-26:16:35:0+26:31:16:35:0+23:31:3:46:0+-31:-23:3:46:0,wall=28:-23.7:23.7:-28:4:40.3" },   /* wooden house, red roof: its back in its sides' boards, the gable and the roof's end turned round */
    { "b1_00112", rm_tower, "mirror=-47:-9:0:45:0:15:30+9:32:0:45:0:15:30+-47:-9:0:45:0:12:14+9:32:0:45:0:12:14+-9:9:33.9:45:0+-23:-6:33.9:45:0,panelz=22:-15:12:-9:9:-22:4:20+21:-15:27:-9:9:-22:20:34:-20" },   /* caravan: its shell turned round, the door column at the back in its front panels, no second door */
    { "b1_00129", rm_tower, "seal=0" },   /* (in progress) */
    { "b1_00105", rm_tower, "snap=-20:32:-58:-54,props=2.5:110:1" },   /* pergola with vines: each wooden frame and slat closed, the vines as they are (both sides) */
    { "b1_00113", rm_tower, "corner=6,clipz=-32,wall=32:-32:32:-32:5:56,mirror=-32:32:49:57:-1:20:30,props=50.5:40" },   /* light blue shop, roof terrace: its back wall in its sides, the parapet inside, the furniture closed */
    { "b1_00114", rm_tower, "corner=6,clipz=-36:-48:48,wall=48:-48:48:-36:4:96,mirror=-40:40:84:97:-2:20:30,rimwalls=84:-20:26:-22:12,two=46:61.5:54:71+-43:-19:107:120,props=54:30" },   /* grey-green building, roof pool: plain back in its sides, the parapet inside, railing and parasol both sides, balcony pots closed */
    { "b1_00119", rm_tower, "corner=4,wall=36:-36:36:-42:3:44.6+-36:-36:2.3:-42:44.6:50.5+36:2.3:36:-42:44.6:59+38:2.3:38:-42:59:65.5,under=59.9,reside=36:-36:3:50.5" },   /* beige two-block building: its back in its sides, block by block */
    { "b1_00120", rm_tower, "corner=4,halfturn=-36.8:6:70:90:135,mirror=-120:-16:0:70:6+-24:105:104:114:12.1:30:60+62:137:62:72:17.3:60:82,twoback=-20,clipz=-32.8:-16.1:-15.9,two=-112:-16:20:63.5+-24:105:105:114+62:129:57:72,wall=136:16.2:136:-46.4:2:71.4+-16:-16:96.4:-32.8:71.4:105.4+16.2:16.2:24.2:-46.8:71.4:92+63:63:68.3:-43.4:71.4:88.6" },   /* white round-ended building: round wing closed by mirror and half turn, back walls in its sides, inside-only walls both ways */
    { "b1_00126", rm_tower, "corner=6,wall=24:-24:24:-24:4:48,mirror=-24:24:41:49:0:15:22,props=42.5:40" },   /* pale green building, roof deck chair: plain back in its sides, the parapet inside, the chair closed */
    { "b1_00144", rm_tower, "corner=0,wall=-80:-80:0:-48:8:64+-72:-72:-8:-40:64:96+80:8:80:-40:8:48+72:16:72:-32:48:96,mirror=-78:-2:10:62:0+-70:-10:68:94:0+10:78:16:46:0+18:70:48:94:0" },   /* two glass office blocks: each part's back in its sides' window grid */
    { "b1_00155", rm_tower, "corner=2,wall=12:-12:12:-8:3:40" },   /* vending machine: a plain back panel in its sides' metal */
    { "b1_00157", rm_tower, "corner=4,mirror=-56:72:59.9:84.5:-0.25:46:53+-56:-44:3:61:-2.75:50:53+60:72:3:61:-2.75:50:53+-79:-55:3:46:-2:40:50+71:95:3:46:-2:40:50,wall=72:-44:60:-48.6:4:60" },   /* station hall, glass vault: the arch and the wings turned round, a plain back below the arch (no doors) */
    { "b1_00160", rm_tower, "corner=4,panel=-30:-10:85:-42.1:56.4:-22:10:49+-30:-10:85:-42.1:56.4:-22:49:74+-30:-10:85:-30:56.3:-22:74:96+-30:-10:85:-42.1:-30:-22:74:85+63.3:0:7:-63.4:63.4:-23:4:10+63.9:-10:60:-41.4:-30:21.2:49:70.9,panelx=63.9:-10:60:-41.4:21.2:27.2:49:70.9:-1+-30:15:30:-38.3:21.2:27.3:4:34:-1+-30:15:30:38.3:21.2:27.3:4:34:1+-30:15:30:-30.8:21.2:27.3:34:46.1:-1+-30:15:30:30.8:21.2:27.3:34:46.1:1,two=-47:-37:84.9:106+59:64.5:70.7:74.1,wall=64.4:-34.2:64.4:-25.1:96:100,mirror=-46:64.5:48.9:74.1:-0.4:15:28+-39:-8.9:3.9:34.1:33.65:39:41+8.9:39:3.9:34.1:33.65:39:41+-9.1:9.1:33.9:46.2:33.65:39:41" },   /* orange and blue round-cornered building: back panels per storey, the band capped, porch and plinth closed */
    { "b1_00246", rm_tower, "panelz=24:-40:20:-52:52:-24:9:35+24:-40:6:-52:52:-24:4:9+24:-40:36.5:-52:52:-24:35:38,mirror=-59:59:37.9:63.1:0:0:25.5+-59:59:37.9:63.1:0:26.5:31+-49:-42:2:60:0:24.5:27.5+42:49:2:60:0:24.5:27.5" },   /* cream building, green hipped roof: its back slope turned round, a back wall in its sides' corrugation */
    { "b1_00057", rm_tower, "corner=6,mirror=-30:30:84:120.5:20+-52:-43:58:72:20,wall=52.8:-52.9:52.8:-34.7:4:44+59.2:-52.5:52.5:-40:44:56+20:-20:20:-4:50.2:84,under=44+84" },   /* control tower: its north faces, back walls and undersides */
    { "b0_00119", rm_chandelier, "candle=3,flame=2,glow=1" },   /* candle chandelier: a round frame, round candles, flames from any side */
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
