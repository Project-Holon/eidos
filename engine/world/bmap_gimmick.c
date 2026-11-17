/*
 * engine/world/bmap_gimmick.c -- the gym gimmick models of the battle
 * backdrop.
 *
 * A gym puzzle adds models to its map at run time (FLD_EXP_OBJ units: the
 * curtains of Striaton, the doors of Castelia, ...), so the map files alone
 * show an open room.  The tables below are read from the gimmick overlays
 * of the game: each gimmick's G3dUtilSetup (which members of which archive
 * make the unit), its position tables and its Init, which says what shows
 * in the first state.  The zone to gimmick id comes from archive 101
 * (a/1/0/1) member 0; the id to overlay from gFieldGimmickOverlayIds.
 *
 * The state is the initial one (closed doors, switches not pressed, no
 * animation played).  It is static on purpose: it works in any battle and in
 * a spot editor that previews another zone, and needs no save.
 */

#include <stddef.h>
#include <stdlib.h>

#include <stdio.h>

#include "bmap_gimmick.h"
#include "port.h"

/* Striaton (zone 7, gimmick 4, ov042 button states).  Models 0 to 8 are the
 * curtains in three columns and three rows, models 9 to 11 the one-shot
 * ones; unit a/1/2/2 holds a BMD0 with its own textures every third member. */
static const BmapGimmickModel k_striaton[] = {
    { "a/1/2/2",  0, -1, 200.0f, 0.0f, 264.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  3, -1, 248.0f, 0.0f, 584.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  6, -1, 152.0f, 0.0f, 424.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  0, -1, 200.0f, 0.0f, 584.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  3, -1, 248.0f, 0.0f, 424.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  6, -1, 152.0f, 0.0f, 584.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  0, -1, 200.0f, 0.0f, 424.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  3, -1, 248.0f, 0.0f, 264.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  6, -1, 152.0f, 0.0f, 264.0f, 0.0f, -1, 0 },
    { "a/1/2/2",  9, -1, 200.0f, 0.0f, 552.0f, 0.0f, -1, 0 },
    { "a/1/2/2", 11, -1, 200.0f, 0.0f, 392.0f, 0.0f, -1, 0 },
    { "a/1/2/2", 13, -1, 200.0f, 0.0f, 232.0f, 0.0f, -1, 0 },
};

/* Nacrene (zone 18, gimmick 3, ov040 flag anime): one model at (208, 152),
 * a/1/1/0 member 0.  Zone 19 has no gimmick. */
static const BmapGimmickModel k_nacrene[] = {
    { "a/1/1/0",  0, -1, 208.0f, 0.0f, 152.0f, 0.0f, -1, 0 },
};

/* Castelia (zone 29, gimmick 6, ov038 plate doors): 8 switches, 10 plates and
 * the 7 doors in their closed state (the open door models stay hidden while
 * the save says closed).  Archive a/1/3/5: member 3 the switch, 0 and 1 the
 * plates, 26 the closed door. */
static const BmapGimmickModel k_castelia[] = {
    { "a/1/3/5",  3, -1, 424.0f, 0.0f, 280.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 616.0f, 0.0f, 280.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 328.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 520.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 712.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 424.0f, 0.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 520.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  3, -1, 712.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  0, -1, 488.0f, 0.0f, 280.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 552.0f, 0.0f, 280.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  0, -1, 392.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 456.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  0, -1, 584.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 648.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 360.0f, 0.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 456.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  0, -1, 584.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5",  1, -1, 648.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 520.0f, 0.0f, 280.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 424.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 616.0f, 0.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 328.0f, 0.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 424.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 616.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/3/5", 26, -1, 520.0f, 0.0f, 536.0f, 0.0f, -1, 0 },
};

/* Driftveil (zone 97, gimmick 7, ov043 lift elevator): the six lifts at the
 * height of their first state, the breakable obstacle and the elevator.
 * Archive a/1/3/8. */
static const BmapGimmickModel k_driftveil[] = {
    { "a/1/3/8",  0, -1, 328.0f, 800.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  0, -1, 328.0f, 640.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  0, -1, 168.0f, 640.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  0, -1, 248.0f, 480.0f, 408.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  0, -1, 168.0f, 480.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  1, -1, 248.0f, 800.0f, 344.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  2, -1, 248.0f, -640.0f, 392.0f, 0.0f, -1, 0 },
    { "a/1/3/8",  0, -1, 248.0f, 800.0f, 408.0f, 0.0f, -1, 0 },
};

/* Mistralton (zone 108, gimmick 5, ov041 cannons): the ten cannons, turned by
 * their quarter turns.  Archive a/1/2/1, member 0. */
static const BmapGimmickModel k_mistralton[] = {
    { "a/1/2/1",  0, -1, 248.0f, 0.0f, 456.0f, 0.0f, -1, 0 },
    { "a/1/2/1",  0, -1, 168.0f, 32.0f, 312.0f, 0.0f, -1, 0 },
    { "a/1/2/1",  0, -1, 328.0f, 64.0f, 472.0f, 0.25f, -1, 0 },
    { "a/1/2/1",  0, -1, 168.0f, 64.0f, 408.0f, 0.75f, -1, 0 },
    { "a/1/2/1",  0, -1, 248.0f, 64.0f, 216.0f, 0.25f, -1, 0 },
    { "a/1/2/1",  0, -1, 88.0f, 96.0f, 136.0f, 0.5f, -1, 0 },
    { "a/1/2/1",  0, -1, 120.0f, 96.0f, 584.0f, 0.75f, -1, 0 },
    { "a/1/2/1",  0, -1, 424.0f, 96.0f, 552.0f, 0.0f, -1, 0 },
    { "a/1/2/1",  0, -1, 392.0f, 96.0f, 104.0f, 0.25f, -1, 0 },
    { "a/1/2/1",  0, -1, 248.0f, 160.0f, 56.0f, 0.5f, -1, 0 },
};

/* Icirrus (zone 114, gimmick 9, ov045 switch toggle): three switches and their
 * three secondary models, switched off.  Archive a/1/5/3. */
static const BmapGimmickModel k_icirrus[] = {
    { "a/1/5/3",  1, -1, 296.0f, 0.0f, 872.0f, 0.0f, -1, 0 },
    { "a/1/5/3",  0, -1, 888.0f, 48.0f, 696.0f, 0.0f, -1, 0 },
    { "a/1/5/3",  1, -1, 280.0f, 96.0f, 536.0f, 0.0f, -1, 0 },
    { "a/1/5/3",  2, -1, 376.0f, 0.0f, 856.0f, 0.0f, -1, 0 },
    { "a/1/5/3",  2, -1, 952.0f, 48.0f, 664.0f, 0.0f, -1, 0 },
    { "a/1/5/3",  2, -1, 248.0f, 96.0f, 472.0f, 0.0f, -1, 0 },
};

/* Nimbasa (zone 63, gimmick 2, ov039 cart lanes): the four carts at the start
 * of their lane's first path (frame 0; the save's frame is not read) and the
 * eight lever models at the last frame of their first animation.  Archive
 * a/1/0/7; each row names the member of its animation. */
static const BmapGimmickModel k_nimbasa[] = {
    { "a/1/0/7",  0, -1, 360.0f, 32.0f, 600.0f, 0.0f, 13, 0 },
    { "a/1/0/7",  0, -1, 664.0f, 32.0f, 616.0f, 0.0f, 15, 0 },
    { "a/1/0/7",  0, -1, 552.0f, 32.0f, 104.0f, 0.0f, 17, 0 },
    { "a/1/0/7",  0, -1, 88.0f, 32.0f, 600.0f, 0.0f, 11, 0 },
    { "a/1/0/7",  2, -1, 472.0f, 32.0f, 536.0f, 0.0f, 3, -1 },
    { "a/1/0/7",  2, -1, 216.0f, 32.0f, 264.0f, 0.0f, 5, -1 },
    { "a/1/0/7",  2, -1, 744.0f, 32.0f, 664.0f, 0.0f, 7, -1 },
    { "a/1/0/7",  2, -1, 408.0f, 32.0f, 152.0f, 0.0f, 9, -1 },
    { "a/1/0/7", 20, -1, 360.0f, 32.0f, 600.0f, 0.0f, 25, -1 },
    { "a/1/0/7", 21, -1, 664.0f, 32.0f, 616.0f, 0.0f, 27, -1 },
    { "a/1/0/7", 22, -1, 552.0f, 32.0f, 104.0f, 0.0f, 29, -1 },
    { "a/1/0/7", 19, -1, 88.0f, 32.0f, 600.0f, 0.0f, 23, -1 },
};

/* Opelucid (zone 121, gimmick 10, ov046 rotating room): three units of a room
 * body, two levers and two buttons, then the extra model.  Init leaves each
 * body in pose 0 (animation 1 at its last frame) and each lever off (its
 * animation 0 at the last frame); bodies and levers face the unit's angle.
 * Archive a/1/6/2. */
static const BmapGimmickModel k_opelucid[] = {
    { "a/1/6/2",  0, -1, 200.0f, 0.0f, 496.0f, 0.5f, 5, -1 },
    { "a/1/6/2",  1, -1, 200.0f, 0.0f, 464.0f, 0.5f, 14, -1 },
    { "a/1/6/2",  2, -1, 200.0f, 0.0f, 464.0f, 0.5f, 12, -1 },
    { "a/1/6/2", 16, -1, 280.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/6/2", 16, -1, 120.0f, 0.0f, 472.0f, 0.0f, -1, 0 },
    { "a/1/6/2",  0, -1, 680.0f, 0.0f, 336.0f, 0.0f, 5, -1 },
    { "a/1/6/2",  1, -1, 680.0f, 0.0f, 368.0f, 0.0f, 14, -1 },
    { "a/1/6/2",  2, -1, 680.0f, 0.0f, 368.0f, 0.0f, 12, -1 },
    { "a/1/6/2", 16, -1, 600.0f, 0.0f, 360.0f, 0.0f, -1, 0 },
    { "a/1/6/2", 16, -1, 760.0f, 0.0f, 360.0f, 0.0f, -1, 0 },
    { "a/1/6/2",  0, -1, 440.0f, 96.0f, 416.0f, 0.0f, 5, -1 },
    { "a/1/6/2",  1, -1, 440.0f, 96.0f, 448.0f, 0.0f, 14, -1 },
    { "a/1/6/2",  2, -1, 440.0f, 96.0f, 448.0f, 0.0f, 12, -1 },
    { "a/1/6/2", 16, -1, 360.0f, 96.0f, 440.0f, 0.0f, -1, 0 },
    { "a/1/6/2", 16, -1, 520.0f, 96.0f, 440.0f, 0.0f, -1, 0 },
    { "a/1/6/2",  3, -1, 512.0f, 0.0f, 512.0f, 0.0f, 18, 0 },
};

typedef struct {
    unsigned zone;
    const BmapGimmickModel *models;
    unsigned count;
} ZoneGimmick;

#define N(a) (unsigned)(sizeof(a) / sizeof((a)[0]))

static const ZoneGimmick k_zones[] = {
    { 7, k_striaton, N(k_striaton) },
    { 18, k_nacrene, N(k_nacrene) },
    { 63, k_nimbasa, N(k_nimbasa) },
    { 29, k_castelia, N(k_castelia) },
    { 97, k_driftveil, N(k_driftveil) },
    { 108, k_mistralton, N(k_mistralton) },
    { 114, k_icirrus, N(k_icirrus) },
    { 121, k_opelucid, N(k_opelucid) },
};

unsigned bmap_gimmick_models(unsigned zone, const BmapGimmickModel **out)
{
    size_t i;

    *out = NULL;
    /* Debug: EIDOS_DEBUG_BMAP_NOGIMMICK=1 leaves the models out (before/after pictures). */
    if (getenv("EIDOS_DEBUG_BMAP_NOGIMMICK") != NULL) {
        return 0;
    }
    for (i = 0; i < sizeof k_zones / sizeof k_zones[0]; i++) {
        if (k_zones[i].zone == zone) {
            *out = k_zones[i].models;
            return k_zones[i].count;
        }
    }
    return 0;
}

/* Driftveil Gym's floors (ov043, FIELD_GIMMICK_LIFT_ELEVATOR_Init): overlay 43 loads at 0x021F36C0. */
#define OV43_BASE 0x021F36C0u
#define OV43_FIXED (0x021F4510u - OV43_BASE)    /* sLiftElevatorFixedHeightAreas: 8 of 5 ints */
#define OV43_LIFTS (0x021F4480u - OV43_BASE)    /* sLiftElevatorLiftZones: 6 of 6 ints */

static int gf_s32(const unsigned char *p)
{
    return (int)((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24));
}

unsigned bmap_gimmick_floors(unsigned zone, BmapGimmickFloor *out, unsigned max)
{
    static const char *const dirs[] = { "overlays_dec", "overlays" };
    unsigned char buf[0xF00];
    size_t n = 0;
    unsigned k, count = 0;
    int i;

    if (zone != 97u) {
        return 0;
    }
    for (i = 0; i < 2 && n == 0; i++) {
        char path[1024];
        FILE *f;

        snprintf(path, sizeof path, "%s/../%s/y9_0043.bin", bw_data_root(), dirs[i]);
        f = fopen(path, "rb");
        if (f != NULL) {
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
        }
    }
    if (n < OV43_FIXED + 8u * 20u) {
        return 0;
    }
    for (k = 0; k < 8u && count < max; k++) {
        const unsigned char *r = buf + OV43_FIXED + k * 20u;

        out[count].tx = gf_s32(r);
        out[count].tz = gf_s32(r + 4);
        out[count].w = gf_s32(r + 8);
        out[count].d = gf_s32(r + 12);
        out[count].y[0] = out[count].y[1] = (float)gf_s32(r + 16) / 4096.0f;
        out[count].lift = 0;
        count++;
    }
    for (k = 0; k < 6u && count < max; k++) {
        const unsigned char *r = buf + OV43_LIFTS + k * 24u;

        out[count].tx = gf_s32(r);
        out[count].tz = gf_s32(r + 4);
        out[count].w = gf_s32(r + 8);
        out[count].d = gf_s32(r + 12);
        out[count].y[0] = (float)gf_s32(r + 16) / 4096.0f;
        out[count].y[1] = (float)gf_s32(r + 20) / 4096.0f;
        out[count].lift = 1;
        count++;
    }
    return count;
}

/* Driftveil Gym's lifts (ov043): sLiftElevatorLiftAnimeFlip (0x021F4328, an int a lift) and the animation members
 * of a lift's object: model 0's at 0x021F4308, model 1's at 0x021F4310 (two u16 pairs each: the member, 0).
 * FIELD_GIMMICK_LIFT_ELEVATOR_ShowLiftAnime shows the animation numbered (state XOR flip). */
int bmap_gimmick_lift_anim(unsigned zone, unsigned lift, unsigned model, int state)
{
    static const char *const dirs[] = { "overlays_dec", "overlays" };
    const unsigned at_flip = 0x021F4328u - OV43_BASE, at_m0 = 0x021F4308u - OV43_BASE, at_m1 = 0x021F4310u - OV43_BASE;
    unsigned char buf[0xF00];
    size_t n = 0;
    unsigned at;
    int i, idx;

    if (zone != 97u || lift >= 6u || model > 1u) {
        return -1;
    }
    for (i = 0; i < 2 && n == 0; i++) {
        char path[1024];
        FILE *f;

        snprintf(path, sizeof path, "%s/../%s/y9_0043.bin", bw_data_root(), dirs[i]);
        f = fopen(path, "rb");
        if (f != NULL) {
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
        }
    }
    if (n < at_flip + 6u * 4u) {
        return -1;
    }
    idx = (state != 0) != (gf_s32(buf + at_flip + lift * 4u) != 0);
    at = (model == 0u ? at_m0 : at_m1) + (unsigned)idx * 4u;
    return (int)((unsigned)buf[at] | ((unsigned)buf[at + 1u] << 8));
}
