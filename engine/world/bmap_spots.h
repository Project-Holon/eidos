/*
 * engine/world/bmap_spots.h -- where each battle stands in the one-screen
 * battle backdrop (bmap_spots.c, docs/battle-backdrop.md).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_SPOTS_H
#define EIDOS_ENGINE_WORLD_BMAP_SPOTS_H

#include "bmap_place.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BMAP_SPOT_AUTO,     /* the search from where the player stands          */
    BMAP_SPOT_FIXED,    /* one spot and angle, whatever the player's place   */
    BMAP_SPOT_ZONE      /* the search kept in a safe rectangle; the angle the
                           player's facing or a fixed one                    */
};

/* One row of the table: a trainer ('T') or a wild area ('A'). */
typedef struct BmapSpot {
    char id[16];            /* T<zone>.<npc> or A<zone>.<n>                  */
    char kind;
    unsigned zone;
    char label[80];
    int tx, tz, dir, range; /* T: the trainer's tile, its facing (0 up, 1 down,
                               2 left, 3 right) and sight range (0: talk only) */
    int rx0, rz0, rx1, rz1; /* A: the encounter tiles' box (tiles, inclusive)  */
    float px, pz;           /* where a player naturally is when the battle
                               starts (map units) ...                          */
    int pf;                 /* ... and the way they face (degrees: 0 north,
                               90 west, 180 south, 270 east)                   */
    int mode;               /* BMAP_SPOT_*                                     */
    float sx, sz;           /* FIXED: the own Pokemon's place (map units)      */
    float angle;            /* FIXED, ZONE: the facing in degrees; ZONE -1: the
                               player's                                        */
    int ax0, az0, ax1, az1; /* ZONE: the safe rectangle (tiles, inclusive)     */
    int ok;                 /* checked by eye in the spot editor               */
    int foe_level;          /* 1: the ground levelled between the two Pokemon;
                               0: each on its own ground (the foe's slot moves) */
    float foe_dz;           /* the foe pushed back (battle units; negative:
                               closer); the move animations follow             */
    BmapIntro intro;        /* the intro's camera, apart from the command
                               camera (bmap_camera_intro; zoom 1 none)         */
    int trainer_id;         /* T: the trainer's number (its script - 3000), -1 when
                               it has none (a leader's scripted battle)        */
    BmapIntro cam;          /* the command camera's own settings (zoom 1 none;
                               bmap_camera_view)                               */
    float trainer_foe[3];   /* the foe trainer's sprite moved: right, up, back
                               (battle units)                                  */
    float trainer_own[3];   /* the player's own trainer sprite, the same        */
    int view;               /* 0 the row's first view; 1 and on its others (rows
                               of the same id: each has its own spot, angle,
                               cameras and trainer offsets); -1 deleted         */
    int trainer_n;          /* T: how many numbers from trainer_id on are the same fight
                               (the versions by the player's starter), 1 when one   */
    int pick_angle;         /* view 0: the view is chosen by the angle first
                               (else by the spot, the angle breaking a tie)     */
    float trainer_size;     /* the foe trainer's height: 1 the base, BMAP_TRAINER_INDOOR
                               the spot editor's "Indoor" (a room's furniture
                               makes the base look short)                      */
    float trainer_own_size; /* the player's own trainer's height, the same way  */
    int doubles;            /* 1: the battle here is a double one (the trainer's
                               own, two trainers who see the player together,
                               or grass whose wild battles can be double;
                               tools/battle_spots.py), from the built-in table */
    int ally_id;            /* a battle beside a character of the story (0x86):
                               the ally's trainer number, -1 none; from the
                               built-in table                                  */
    float trainer_ally[3];  /* the ally's sprite moved: right, up, back        */
    float trainer_ally_size; /* the ally's height (1 the base)                  */
} BmapSpot;
#define BMAP_TRAINER_INDOOR 1.15f
#define BMAP_VIEWS_MAX 8    /* views of a row */

/* The table: the one built in (battle_spots.tsv), then the rows of the
 * user's file over it (the spot editor writes it).  Loaded on first use. */
void      bmap_spots_set_user_path(const char *path);
const char *bmap_spots_user_path(void);
int       bmap_spots_count(void);
BmapSpot *bmap_spots_get(int i);
/* The row for a battle that starts at (px, pz) map units in `zone`: a
 * trainer row whose sight line or neighbouring tiles hold the player, or a
 * wild area row whose box holds them.  -1 when none. */
int       bmap_spots_find(unsigned zone, int trainer, float px, float pz);
int       bmap_spots_find_trainer(unsigned zone, int trainer_id, float px, float pz);
/* The views of the row at index i (any of its views): their count, the
 * table index of view v (0 is the first; -1 when there is none), and the one
 * a battle with the player at (px, pz) facing the game's 16-bit angle takes:
 * the view whose spot (a fixed spot, a safe area's middle, else the player's
 * usual place) is nearest, the one whose angle is nearest the facing when two
 * are about as near (less than a tile apart) or when the row asks for the angle
 * first.  Returns the table index of that view. */
int       bmap_spots_view_count(int i);
int       bmap_spots_view_at(int i, int v);
int       bmap_spots_pick_view(int i, float px, float pz, unsigned facing);
/* A new view of row i, a copy of view `from`; returns its table index, or
 * -1 when the row has BMAP_VIEWS_MAX.  The table keeps every index. */
int       bmap_spots_view_add(int i, int from);
/* Delete view v of the row (the last one stays); returns 0 when done. */
int       bmap_spots_view_delete(int i, int v);
/* The placement for row e (NULL: none) and the player at (px, pz), facing
 * the game's 16-bit angle. */
void      bmap_spots_spec(const BmapSpot *e, float px, float pz, unsigned facing,
                          BmapPlaceSpec *out);
/* The built-in row `id` (before the user's file), into *out.  Returns 0
 * when there is one. */
int       bmap_spots_default(const char *id, BmapSpot *out);
/* Write the whole table to the user's file.  Returns 0 on success. */
int       bmap_spots_save(void);

#ifdef __cplusplus
}
#endif

#endif
