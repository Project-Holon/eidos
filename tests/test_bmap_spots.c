/*
 * tests/test_bmap_spots.c -- the battle spot table (engine/world/bmap_spots.c).
 *
 * No game data: the table is built into the program.  The checks:
 *
 *   - the built-in table loads, with rows for trainers and wild areas;
 *   - a battle finds its row: a trainer's from a tile of its sight line or
 *     beside it (not past its range), a wild area's from a tile in its box;
 *   - each mode gives its placement: auto faces the trainer with no turn,
 *     fixed takes the row's spot and angle, a safe area keeps the search in
 *     its rectangle and follows the player's facing unless an angle is set;
 *   - the foe's distance, its ground and the intro zoom reach the placement;
 *   - the command camera's settings and the trainers' offsets reach the
 *     placement;
 *   - a row holds several views: the game takes the one nearest the player
 *     (the angle breaks a tie, or comes first when the row says so), views
 *     are added and deleted;
 *   - a change is written to the user's file and read back over the
 *     built-in row, and the built-in row stays available as the default;
 *   - a field left out takes its default (the foe's distance by kind, the
 *     own trainer, the intro), a file from before the defaults takes them
 *     only on rows nobody placed, and the ally's columns are read.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap_spots.h"
#include "port_test.h"

static float tile_centre(int t)
{
    return (float)t * 16.0f + 8.0f;
}

static int row_of(const char *id)
{
    int i;

    for (i = 0; i < bmap_spots_count(); i++) {
        if (strcmp(bmap_spots_get(i)->id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *found(unsigned zone, int trainer, int tx, int tz)
{
    BmapSpot *r = bmap_spots_get(bmap_spots_find(zone, trainer, tile_centre(tx), tile_centre(tz)));

    return r != NULL ? r->id : "none";
}

int main(void)
{
    const char *path = "test_bmap_spots_user.tsv";
    int waiter, area, trainers = 0, wild = 0, i;
    BmapSpot *r, d;
    BmapPlaceSpec sp;
    FILE *f;
    char line[512];
    int seen = 0, v1, v2;

    remove(path);
    bmap_spots_set_user_path(path);

    /* the table */
    for (i = 0; i < bmap_spots_count(); i++) {
        trainers += bmap_spots_get(i)->kind == 'T';
        wild += bmap_spots_get(i)->kind == 'A';
    }
    BW_REQUIRE("rows for trainers", trainers > 300);
    BW_REQUIRE("rows for wild areas", wild > 300);
    waiter = row_of("T7.7");            /* Striaton Gym's waiter: tile (9, 30), facing right, sight 6 */
    area = row_of("A317.4");            /* Route 1, area 4: tiles (784, 715) to (791, 723) */
    BW_REQUIRE("T7.7 is in the table", waiter >= 0);
    BW_REQUIRE("A317.4 is in the table", area >= 0);
    r = bmap_spots_get(waiter);
    BW_CHECK_EQ("T7.7 tile x", r->tx, 9);
    BW_CHECK_EQ("T7.7 sight", r->range, 6);

    /* finding a battle's row */
    BW_CHECK_EQ("sight line, 2 tiles", strcmp(found(7, 1, 11, 30), "T7.7"), 0);
    BW_CHECK_EQ("sight line, 6 tiles", strcmp(found(7, 1, 15, 30), "T7.7"), 0);
    BW_CHECK_EQ("past the range", strcmp(found(7, 1, 16, 30), "T7.7") != 0, 1);
    BW_CHECK_EQ("beside it (talk)", strcmp(found(7, 1, 9, 29), "T7.7"), 0);
    BW_CHECK_EQ("off the line", strcmp(found(7, 1, 12, 32), "T7.7") != 0, 1);
    BW_CHECK_EQ("wild, in the box", strcmp(found(317, 0, 786, 718), "A317.4"), 0);
    BW_CHECK_EQ("wild, other zone", strcmp(found(318, 0, 786, 718), "none"), 0);
    BW_CHECK_EQ("a trainer row is not a wild one", strcmp(found(7, 0, 11, 30), "T7.7") != 0, 1);

    /* auto: from the player, facing the trainer (west of the player), no turn
     * (the row as the table had it before anyone placed or checked it) */
    r->mode = BMAP_SPOT_AUTO;
    r->ok = 0;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("auto: the player's x", (long)sp.px, (long)tile_centre(11));
    BW_CHECK_EQ("auto: faces the trainer", sp.facing, 0x4000);
    BW_CHECK_EQ("auto: no turn", sp.turn, 0);
    BW_CHECK_EQ("auto: a trainer's reach", sp.reach, BMAP_PLACE_REACH_TRAINER);
    BW_CHECK_EQ("auto: levelled", sp.foe_level, 1);
    BW_CHECK_EQ("auto: never toward the field camera", sp.no_back, 1);
    r->ok = 1;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("auto, checked: as it is", sp.no_back, 0);
    r->ok = 0;

    /* fixed */
    r->mode = BMAP_SPOT_FIXED;
    r->sx = 200.0f;
    r->sz = 400.0f;
    r->angle = 180.0f;
    r->foe_dz = 2.5f;
    r->foe_level = 0;
    r->intro.zoom = 1.5f;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("fixed: spot x", (long)sp.px, 200);
    BW_CHECK_EQ("fixed: spot z", (long)sp.pz, 400);
    BW_CHECK_EQ("fixed: angle", sp.facing, 0x8000);
    BW_CHECK_EQ("fixed: as given", sp.reach, -1);
    BW_CHECK_EQ("fixed: foe distance", (long)(sp.foe_dz * 10.0f), 25);
    BW_CHECK_EQ("fixed: foe on its ground", sp.foe_level, 0);
    BW_CHECK_EQ("fixed: intro zoom", (long)(sp.intro.zoom * 10.0f), 15);
    BW_CHECK_EQ("fixed: its own direction", sp.no_back, 0);

    /* a safe area: the rectangle, the player's facing, then a set angle */
    r = bmap_spots_get(area);
    r->mode = BMAP_SPOT_ZONE;
    r->ax0 = 785;
    r->az0 = 716;
    r->ax1 = 788;
    r->az1 = 719;
    r->angle = -1.0f;
    bmap_spots_spec(r, tile_centre(800), tile_centre(700), 0xC000u, &sp);
    BW_CHECK_EQ("area: a rectangle", sp.has_rect, 1);
    BW_CHECK_EQ("area: rectangle x0", (long)sp.x0, (long)tile_centre(785));
    BW_CHECK_EQ("area: rectangle z1", (long)sp.z1, (long)tile_centre(719));
    BW_CHECK_EQ("area: the player's facing", sp.facing, 0xC000);
    BW_CHECK_EQ("area: may turn", sp.turn, 1);
    r->angle = 90.0f;
    bmap_spots_spec(r, tile_centre(800), tile_centre(700), 0xC000u, &sp);
    BW_CHECK_EQ("area: a set angle", sp.facing, 0x4000);
    BW_CHECK_EQ("area: no turn", sp.turn, 0);

    /* the command camera and the trainers */
    r = bmap_spots_get(waiter);
    r->cam.zoom = 1.25f;
    r->cam.lift = 0.5f;
    r->trainer_foe[0] = 1.5f;
    r->trainer_foe[2] = -2.0f;
    r->trainer_own[1] = 0.25f;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("camera zoom", (long)(sp.cam.zoom * 100.0f), 125);
    BW_CHECK_EQ("camera height", (long)(sp.cam.lift * 10.0f), 5);
    BW_CHECK_EQ("foe trainer right", (long)(sp.trainer_foe[0] * 10.0f), 15);
    BW_CHECK_EQ("foe trainer back", (long)(sp.trainer_foe[2] * 10.0f), -20);
    BW_CHECK_EQ("own trainer up", (long)(sp.trainer_own[1] * 100.0f), 25);

    /* views: a first view and two more, each its own spot and angle */
    r->mode = BMAP_SPOT_FIXED;
    r->sx = 100.0f;
    r->sz = 100.0f;
    r->angle = 0.0f;
    BW_CHECK_EQ("one view", bmap_spots_view_count(waiter), 1);
    v1 = bmap_spots_view_add(waiter, 0);
    BW_REQUIRE("a second view", v1 >= 0 && v1 != waiter);
    bmap_spots_get(v1)->sx = 300.0f;
    bmap_spots_get(v1)->angle = 90.0f;
    bmap_spots_get(v1)->cam.zoom = 2.0f;
    v2 = bmap_spots_view_add(waiter, 1);
    BW_REQUIRE("a third view", v2 >= 0);
    bmap_spots_get(v2)->sx = 500.0f;
    bmap_spots_get(v2)->angle = 180.0f;
    BW_CHECK_EQ("three views", bmap_spots_view_count(waiter), 3);
    BW_CHECK_EQ("view 2 is the added one", bmap_spots_view_at(v2, 1), v1);
    BW_CHECK_EQ("a view's row is not a place of its own", bmap_spots_find(7, 1, tile_centre(11), tile_centre(30)), waiter);
    BW_CHECK_EQ("the nearest spot: first", bmap_spots_pick_view(waiter, 110.0f, 100.0f, 0x0000u), waiter);
    BW_CHECK_EQ("the nearest spot: second", bmap_spots_pick_view(waiter, 290.0f, 100.0f, 0x0000u), v1);
    /* the angle counts more: near view 3 but facing view 1's way, view 1 */
    BW_CHECK_EQ("the angle over the place", bmap_spots_pick_view(waiter, 600.0f, 100.0f, 0x0000u), waiter);
    BW_CHECK_EQ("the nearest spot: third", bmap_spots_pick_view(waiter, 600.0f, 100.0f, 0x8000u), v2);
    /* two spots as near: the angle decides (facing 180 degrees: 0x8000) */
    bmap_spots_get(v2)->sx = 105.0f;
    BW_CHECK_EQ("a tie: the angle", bmap_spots_pick_view(waiter, 100.0f, 100.0f, 0x8000u), v2);
    BW_CHECK_EQ("a tie: the other angle", bmap_spots_pick_view(waiter, 100.0f, 100.0f, 0x0000u), waiter);
    bmap_spots_get(v2)->sx = 500.0f;
    /* half way between views 1 and 2, facing 90 degrees (0x4000): view 2's angle */
    BW_CHECK_EQ("the angle decides between two as near", bmap_spots_pick_view(waiter, 200.0f, 100.0f, 0x4000u), v1);

    /* a story fight has one row for all the versions by the player's starter */
    {
        int a = bmap_spots_find_trainer(391, 59, tile_centre(5), tile_centre(9));
        int b = bmap_spots_find_trainer(391, 53, tile_centre(5), tile_centre(9));

        BW_REQUIRE("Nuvema's two fights have rows", a >= 0 && b >= 0 && a != b);
        BW_CHECK_EQ("fight 1: second version", bmap_spots_find_trainer(391, 60, tile_centre(5), tile_centre(9)), a);
        BW_CHECK_EQ("fight 1: third version", bmap_spots_find_trainer(391, 61, tile_centre(5), tile_centre(9)), a);
        BW_CHECK_EQ("fight 2: second version", bmap_spots_find_trainer(391, 54, tile_centre(5), tile_centre(9)), b);
        BW_CHECK_EQ("fight 2: third version", bmap_spots_find_trainer(391, 55, tile_centre(5), tile_centre(9)), b);
        BW_CHECK_EQ("no row for a number beside the range", bmap_spots_find_trainer(391, 62, tile_centre(5), tile_centre(9)), -1);
    }

    /* saving: the user's file has the changed rows, the default stays */
    bmap_spots_spec(NULL, 10.0f, 20.0f, 0x8000u, &sp);
    BW_CHECK_EQ("no row: facing", sp.facing, 0x8000);
    BW_CHECK_EQ("no row: reach", sp.reach, BMAP_PLACE_REACH);
    BW_CHECK_EQ("no row: no rectangle", sp.has_rect, 0);
    BW_CHECK_EQ("no row: never toward the field camera", sp.no_back, 1);

    BW_REQUIRE("save", bmap_spots_save() == 0);
    f = fopen(path, "r");
    BW_REQUIRE("the user's file", f != NULL);
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "T7.7\t", 5) == 0) {
            seen |= strstr(line, "\tfixed\t100\t100\t0\t") != NULL;
        }
    }
    fclose(f);
    BW_CHECK_EQ("the saved row", seen, 1);
    /* read back: the views and the new settings are in the table, and the
     * second view deleted stays deleted */
    BW_REQUIRE("delete the second view", bmap_spots_view_delete(waiter, 1) == 0);
    BW_CHECK_EQ("two views", bmap_spots_view_count(waiter), 2);
    BW_REQUIRE("save again", bmap_spots_save() == 0);
    bmap_spots_set_user_path(path);
    waiter = row_of("T7.7");
    r = bmap_spots_get(waiter);
    BW_CHECK_EQ("read back: two views", bmap_spots_view_count(waiter), 2);
    BW_CHECK_EQ("read back: first view", (long)r->sx, 100);
    BW_CHECK_EQ("read back: camera zoom", (long)(r->cam.zoom * 100.0f), 125);
    BW_CHECK_EQ("read back: foe trainer", (long)(r->trainer_foe[0] * 10.0f), 15);
    r = bmap_spots_get(bmap_spots_view_at(waiter, 1));
    BW_CHECK_EQ("read back: second view", (long)r->sx, 500);
    BW_CHECK_EQ("read back: its angle", (long)r->angle, 180);
    BW_CHECK_EQ("the other trainers have one view", bmap_spots_view_count(row_of("T7.1")), 1);
    BW_CHECK_EQ("a file from before: no camera", (long)(bmap_spots_get(row_of("T7.1"))->cam.zoom * 100.0f), 100);
    BW_REQUIRE("delete the first view", bmap_spots_view_delete(waiter, 0) == 0);
    BW_CHECK_EQ("the next view is the first now", (long)bmap_spots_get(waiter)->sx, 500);
    BW_CHECK_EQ("one view left", bmap_spots_view_count(waiter), 1);
    BW_CHECK_EQ("the last view stays", bmap_spots_view_delete(waiter, 0), -1);
    BW_REQUIRE("the default row", bmap_spots_default("T7.7", &d) == 0);
    BW_CHECK_EQ("the default is untouched", d.sx == 100.0f && d.sz == 100.0f, 0);

    /* the defaults: what a row that leaves the fields out takes (built
     * here: the built-in table carries hand-placed rows) */
    memset(&d, 0, sizeof d);
    d.kind = 'T';
    bmap_spot_defaults(&d);
    BW_CHECK_EQ("trainer: foe closer", (long)(d.foe_dz * 10.0f), (long)(BMAP_SPOT_FOE_DZ_TRAINER * 10.0f));
    BW_CHECK_EQ("trainer: own trainer up", (long)(d.trainer_own[1] * 10.0f), (long)(BMAP_SPOT_OWN_UP * 10.0f));
    BW_CHECK_EQ("trainer: intro side", (long)(d.intro.side * 10.0f), (long)(BMAP_SPOT_INTRO_SIDE * 10.0f));
    BW_CHECK_EQ("trainer: intro tilt", (long)(d.intro.tilt * 10.0f), (long)(BMAP_SPOT_INTRO_TILT * 10.0f));
    memset(&d, 0, sizeof d);
    d.kind = 'A';
    bmap_spot_defaults(&d);
    BW_CHECK_EQ("wild: foe closer", (long)(d.foe_dz * 10.0f), (long)(BMAP_SPOT_FOE_DZ_WILD * 10.0f));
    BW_CHECK_EQ("wild: intro zoom", (long)(d.intro.zoom * 10.0f), (long)(BMAP_SPOT_INTRO_ZOOM * 10.0f));
    d.kind = 'T';
    bmap_spot_defaults(&d);
    BW_CHECK_EQ("trainer: intro zoom", (long)(d.intro.zoom * 10.0f), (long)(BMAP_SPOT_INTRO_ZOOM * 10.0f));

    /* a file the editor wrote before the defaults: 0 for "not set" */
    f = fopen(path, "w");
    BW_REQUIRE("an old user's file", f != NULL);
    fputs("# battle spots written by the spot editor: tools/battle_spots.py merge\n", f);
    /* nobody placed it: the zeros take the defaults; the ally's columns */
    fputs("T7.1\tT\t7\tleader\t1\t1\t1\t0\t-\t-\t-\t-\t0\t0\t0\tauto\t-\t-\t-\t-\t-\t-\t-"
          "\t0\t1\t0\t1\t-1\t0\t0\t0\t0\t0\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t1\t1\t1\t0"
          "\t7\t0.5\t1.5\t2.5\t1.25\n", f);
    /* placed by hand: its zeros are its own */
    fputs("T7.8\tT\t7\twaitress\t1\t1\t1\t0\t-\t-\t-\t-\t0\t0\t0\tfixed\t10\t20\t90\t-\t-\t-\t-"
          "\t1\t1\t0\t1\t-1\t0\t0\t0\t0\t0\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t1\t1\t1\t0\n", f);
    fclose(f);
    bmap_spots_set_user_path(path);
    r = bmap_spots_get(row_of("T7.1"));
    BW_CHECK_EQ("old file, not placed: foe", (long)(r->foe_dz * 10.0f), (long)(BMAP_SPOT_FOE_DZ_TRAINER * 10.0f));
    BW_CHECK_EQ("old file, not placed: own trainer", (long)(r->trainer_own[1] * 10.0f), (long)(BMAP_SPOT_OWN_UP * 10.0f));
    BW_CHECK_EQ("old file, not placed: intro tilt", (long)(r->intro.tilt * 10.0f), (long)(BMAP_SPOT_INTRO_TILT * 10.0f));
    BW_CHECK_EQ("old file, not placed: intro zoom", (long)(r->intro.zoom * 10.0f), (long)(BMAP_SPOT_INTRO_ZOOM * 10.0f));
    BW_CHECK_EQ("the ally's place is read", (long)(r->trainer_ally[1] * 10.0f), 15);
    BW_CHECK_EQ("the ally's size is read", (long)(r->trainer_ally_size * 100.0f), 125);
    r = bmap_spots_get(row_of("T7.8"));
    BW_CHECK_EQ("old file, placed: foe kept", (long)(r->foe_dz * 10.0f), 0);
    BW_CHECK_EQ("old file, placed: own trainer kept", (long)(r->trainer_own[1] * 10.0f), 0);
    BW_CHECK_EQ("old file, placed: intro kept", (long)(r->intro.side * 10.0f), 0);

    /* written again: the defaults as '-', the hand-set zeros written out,
     * and both read back the same */
    BW_REQUIRE("save over the old file", bmap_spots_save() == 0);
    f = fopen(path, "r");
    BW_REQUIRE("the new file", f != NULL);
    seen = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "T7.1\t", 5) == 0) {
            seen |= strstr(line, "\tauto\t-\t-\t-\t-\t-\t-\t-\t0\t1\t-\t") != NULL;
        }
        if (strncmp(line, "T7.8\t", 5) == 0) {
            seen |= (strstr(line, "\t1\t1\t0\t1\t") != NULL) << 1;
        }
    }
    fclose(f);
    BW_CHECK_EQ("a default written as -, a set 0 as 0", seen, 3);
    bmap_spots_set_user_path(path);
    BW_CHECK_EQ("read back: the default", (long)(bmap_spots_get(row_of("T7.1"))->foe_dz * 10.0f),
                (long)(BMAP_SPOT_FOE_DZ_TRAINER * 10.0f));
    BW_CHECK_EQ("read back: the set 0", (long)(bmap_spots_get(row_of("T7.8"))->foe_dz * 10.0f), 0);

    /* a file from before the trainers' intro zoom default: 1 written out */
    f = fopen(path, "w");
    BW_REQUIRE("a format 2 file", f != NULL);
    fputs("# battle spots written by the spot editor (- takes the default): tools/battle_spots.py merge\n", f);
    fputs("T7.1\tT\t7\tleader\t1\t1\t1\t0\t-\t-\t-\t-\t0\t0\t0\tauto\t-\t-\t-\t-\t-\t-\t-"
          "\t0\t1\t-\t1\t-1\t0\t0\t-\t-\t0\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t-\t0\t0\t0\t1\t1\t1\t0\n", f);
    fputs("T7.8\tT\t7\twaitress\t1\t1\t1\t0\t-\t-\t-\t-\t0\t0\t0\tfixed\t10\t20\t90\t-\t-\t-\t-"
          "\t1\t1\t-\t1\t-1\t0\t0\t-\t-\t0\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t-\t0\t0\t0\t1\t1\t1\t0\n", f);
    fclose(f);
    bmap_spots_set_user_path(path);
    BW_CHECK_EQ("format 2, not placed: intro zoom default", (long)(bmap_spots_get(row_of("T7.1"))->intro.zoom * 10.0f),
                (long)(BMAP_SPOT_INTRO_ZOOM * 10.0f));
    BW_CHECK_EQ("format 2, placed: its 1 kept", (long)(bmap_spots_get(row_of("T7.8"))->intro.zoom * 10.0f), 10);
    remove(path);
    BW_REPORT("test_bmap_spots");
}
