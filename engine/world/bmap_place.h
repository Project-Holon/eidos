/*
 * engine/world/bmap_place.h -- the map in battle space (bmap_place.c).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_PLACE_H
#define EIDOS_ENGINE_WORLD_BMAP_PLACE_H

#include "bmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Battle units per map unit: one tile (16 map units) is 2.88 battle units,
 * so the world is at about the size of the Pokemon (chosen by eye). */
#define BMAP_PLACE_K 0.18f

/* Turn a built scene into battle space, in place.  (px, pz) is the player's
 * position (map units) and `facing` the game's 16-bit angle (0 north, 0x4000
 * west, 0x8000 south, 0xC000 east).  The player's tile centre lands on the
 * battle's own slot, the ground under it on the sprites' ground height, and
 * the map's forward is the foe's side. */
void bmap_place(BmapScene *s, float px, float pz, unsigned facing);
/* The same, searching up to `reach` tiles around (px, pz) and up to 45
 * degrees either side of the facing for a spot with clear ground and camera
 * space; with reach < 0 the spot and facing are used as given. */
void bmap_place_search(BmapScene *s, float px, float pz, unsigned facing, int reach);
/* the search distance of a real battle, in tiles */
#define BMAP_PLACE_REACH 4
/* the same for a trainer battle: the player stands right in front of the
 * trainer (a leader on a stage), and the battle needs about six tiles */
#define BMAP_PLACE_REACH_TRAINER 6

/* A placement with every choice spelled out (bmap_spots.c). */
typedef struct BmapPlaceSpec {
    float px, pz;           /* where the search starts: the own Pokemon's place
                               (map units)                                    */
    unsigned facing;        /* the game's 16-bit angle; any angle             */
    int reach;              /* tiles the spot may move; < 0: none             */
    int turn;               /* 45-degree steps the angle may turn either way:
                               0 keeps it, 1 at most (the foe stays where the
                               player looks)                                  */
    int has_rect;           /* the spot stays in [x0, x1] x [z0, z1] (map units) */
    int has_area;           /* a wild area: both Pokemon stand on its grass, in
                               [area[0], area[2]] x [area[1], area[3]] (map
                               units), when any spot in reach allows it      */
    float area[4];
    float x0, z0, x1, z1;
    float foe_dz;           /* the foe this far further back (battle units;
                               negative: closer)                              */
    BmapIntro intro;        /* the far cameras' own settings (zoom 0 counts
                               as 1; bmap_camera_intro)                       */
    int field;              /* a field view (eidos_spots' overworld): the grass
                               sheets stay as the field draws them, no tufts  */
    int foe_level;          /* 1: the ground levelled between the two (the foe
                               keeps the game's height); 0: each Pokemon on its
                               own ground, the foe's slot raised or lowered    */
    BmapIntro cam;          /* the command camera's own settings, the same for
                               every battle camera (zoom 0 counts as 1;
                               bmap_camera_view)                              */
    float trainer_foe[3];   /* the foe trainer's sprite moved: right, up, back
                               (battle units)                                  */
    float trainer_own[3];   /* the same for the player's own trainer sprite     */
    float trainer_size;     /* the foe trainer's height (0 or 1: the base)      */
    float trainer_own_size; /* the player's own trainer's height, the same      */
    int has_ally;           /* a battle beside an ally (its trainer at slot 12) */
    float trainer_ally[3];  /* the ally's sprite moved, and ...                 */
    float trainer_ally_size; /* ... its height                                  */
} BmapPlaceSpec;
void bmap_place_spec(BmapScene *s, const BmapPlaceSpec *sp);
/* The height of a wild area's grass (its leafy sheets over the encounter
 * tiles in [x0, x1] x [z0, z1], map units, the lowest): the area's floor is
 * just under it.  0 when there is no grass there (water, a cave). */
int bmap_wild_height(const BmapScene *s, float x0, float z0, float x1, float z1, float *y);

/* The battle camera with a place's intro settings.  A camera on the way
 * from the command camera's eye to the intro's (the intro's, the
 * send-out's) gets them in the part it has gone, in this order: it moves
 * toward what the intro looks at (at the intro's eye, its distance to the
 * target divided by `zoom`), turns `yaw` degrees about the upright line
 * through the battle's centre (its target with it), rises `lift`, slides
 * `side` to its right, then turns its look `aim` degrees to the right and
 * `tilt` degrees up from where it stands.  The command camera and the
 * close-ups are not changed, and the intro slides into the command camera
 * along one line, without a jump or a dip. */
void bmap_camera_intro(float eye[3], float target[3], const BmapIntro *in);
/* The command camera's settings (BmapPlaceSpec.cam) on a camera, in the part
 * `w` (0 to 1): the command camera and the close-ups get all of them (w 1),
 * the intro's camera none (w 0), a camera between the two the part of the
 * way it is from the intro's.  zoom comes closer to what the camera itself
 * looks at; the rest as bmap_camera_intro. */
void bmap_camera_view(float eye[3], float target[3], const BmapIntro *cam, float w);
/* How far a battle camera's eye is on the way from the command camera's
 * eye to the intro's: 0 at the command camera, 1 at the intro's and beyond
 * (the eye projected on the line between the two). */
float bmap_intro_progress(const float eye[3]);
/* How deep a battle point lies in front of the game's command camera. */
float bmap_cmd_depth(const float p[3]);
/* No change at all (zoom 1 or 0, everything else 0). */
int bmap_intro_none(const BmapIntro *in);
/* A battle point in the place's battle: the foe's side (z < 0) stretched so
 * the game's foe slot (z -10) lands where the place puts the foe (pushed
 * back by `dz`, raised by `dy`).  The game's cameras aim at its own slots;
 * warped the same way they frame the Pokemon where they now stand. */
void bmap_battle_warp(float p[3], float dy, float dz);
/* The other way: a point of the place's battle back where the game's own
 * battle has it. */
void bmap_battle_unwarp(float p[3], float dy, float dz);
/* The place's camera: the game's eye and target warped, then the intro
 * zoom (what the game draws with and what the backdrop uses). */
void bmap_battle_camera(float eye[3], float target[3], const BmapScene *s);
/* Change the command camera and the trainer offsets of an already placed
 * scene (the spot editor, while a drag goes on).  bmap_place_spec keeps what
 * it was given in the scene (BmapScene.cam, trainer_foe, trainer_own). */
void bmap_place_view_live(BmapScene *s, const BmapIntro *cam, const float trainer_foe[3],
                          const float trainer_own[3]);
/* True when the place changes the command camera or moves a trainer. */
int bmap_place_view_set(const BmapScene *s);

#ifdef __cplusplus
}
#endif

#endif
