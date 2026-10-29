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
 *     built-in row, and the built-in row stays available as the default.
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

    /* auto: from the player, facing the trainer (west of the player), no turn */
    r->mode = BMAP_SPOT_AUTO;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("auto: the player's x", (long)sp.px, (long)tile_centre(11));
    BW_CHECK_EQ("auto: faces the trainer", sp.facing, 0x4000);
    BW_CHECK_EQ("auto: no turn", sp.turn, 0);
    BW_CHECK_EQ("auto: a trainer's reach", sp.reach, BMAP_PLACE_REACH_TRAINER);
    BW_CHECK_EQ("auto: levelled", sp.foe_level, 1);

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
    BW_CHECK_EQ("the nearest spot: third", bmap_spots_pick_view(waiter, 600.0f, 100.0f, 0x0000u), v2);
    /* two spots as near: the angle decides (facing 180 degrees: 0x8000) */
    bmap_spots_get(v2)->sx = 105.0f;
    BW_CHECK_EQ("a tie: the angle", bmap_spots_pick_view(waiter, 100.0f, 100.0f, 0x8000u), v2);
    BW_CHECK_EQ("a tie: the other angle", bmap_spots_pick_view(waiter, 100.0f, 100.0f, 0x0000u), waiter);
    bmap_spots_get(v2)->sx = 500.0f;
    /* the angle first: east of the player (facing 90 degrees, 0x4000) takes view 2 from afar */
    r->pick_angle = 1;
    BW_CHECK_EQ("the angle first", bmap_spots_pick_view(waiter, 100.0f, 100.0f, 0x4000u), v1);
    r->pick_angle = 0;

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
    remove(path);
    BW_REPORT("test_bmap_spots");
}
