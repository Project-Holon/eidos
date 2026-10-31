/*
 * engine/world/bmap.h -- the real map around the player, for the one-screen
 * layout's battle backdrop (docs/battle-backdrop.md).
 *
 * bmap_data.c builds a scene: world-space triangles with lit vertex colours,
 * RGBA textures and the sky colour.  bmap_place.c turns it into battle space
 * (the units the game's battle camera uses) once per battle.  plat_gl3d.c
 * draws it behind the game's polygons.
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_H
#define EIDOS_ENGINE_WORLD_BMAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BmapVertex {
    float x, y, z;          /* map world units (fx32 / 4096), the game's own axes */
    float u, v;             /* texel units / texture size (0..1 per repeat)      */
    unsigned char r, g, b, a; /* lit vertex colour (material + the zone's light at
                               the battle's time), alpha from the material     */
    unsigned obj;           /* the placed object the triangle belongs to; 0 for
                               the ground chunks (never removed)                */
} BmapVertex;

typedef struct BmapTexture {
    int w, h;
    unsigned char *rgba;    /* w*h*4, straight alpha                            */
    unsigned char repeat_s, repeat_t, flip_s, flip_t;
} BmapTexture;

/* A texture-coordinate animation (a placed model's BTA0 texture SRT
 * animation: Route 1's falling petals and leaves, a fountain's water), played
 * the way the field plays it, one frame a field frame, looping: per frame,
 * u' = m[0] u + m[1] v + m[2], v' = m[3] u + m[4] v + m[5]. */
typedef struct BmapUvAnim {
    unsigned frames;
    float *m;               /* 6 floats a frame                                  */
} BmapUvAnim;

typedef struct BmapDraw {
    int tex;                /* index into textures, -1 untextured                */
    unsigned first, count;  /* triangles: vertices [first, first+count), 3 per tri */
    unsigned char alpha;    /* 0..31 material alpha (31 opaque)                 */
    unsigned char cull;     /* 0 none, 1 back, 2 front                           */
    unsigned char translucent; /* drawn after the opaque ones                   */
    unsigned char billboard;   /* 0 none, 1 faces the camera, 2 turns about y only;
                                  the triangles are built facing +z around `pivot` */
    float pivot[3];            /* the billboard's centre (map units)                */
    unsigned char far;         /* from a cell beyond about 640 units of the player:
                                  the GL pass may fog it                            */
    unsigned char grass;       /* a tall-grass tuft (bmap_place): drawn again over the
                                  Pokemon it stands in front of                     */
    unsigned short uvanim;     /* 1 + its index in BmapScene.uva; 0 none            */
} BmapDraw;

/* A place's own intro camera (the intro's and the send-out's, the cameras
 * further out than the command camera's), apart from the command camera:
 * bmap_camera_intro. */
typedef struct BmapIntro {
    float zoom;             /* above 0 (0 counts as 1): closer to what it looks
                               at; below 1 further out                        */
    float yaw;              /* degrees: turned about the battle's centre,
                               positive to the right                          */
    float lift;             /* battle units: raised                           */
    float side;             /* battle units: slid to its right (eye and target) */
    float tilt;             /* degrees: looks up (positive) or down            */
    float aim;              /* degrees: looks to its right (positive) or left,
                               from where it stands                           */
} BmapIntro;

typedef struct BmapScene {
    BmapVertex *v; unsigned nv;
    BmapDraw *d; unsigned nd;
    BmapTexture *t; unsigned nt;
    unsigned char clear[3]; /* the light keyframe's clear (sky) colour           */
    unsigned char fog[3]; int fog_on;
    float fog_start, fog_end; /* fog_on: distance from the player (map units) where the
                                 fog begins and where it is full; the zone's own
                                 fog only, not the weather's                       */
    void *priv;             /* the ground index (bmap_ground_y); owned by the scene */
    float place_k;          /* bmap_place: the scale chosen, battle units per map unit */
    float place_x, place_z; /* bmap_place: the spot chosen (map units: the own
                               Pokemon's place) and the angle (radians, the
                               game's facing: 0 north, pi/2 west)             */
    float place_a;
    float place_g[2];       /* bmap_place: the ground under the own and the foe's
                               Pokemon (map units)                            */
    float foe_dy, foe_dz;   /* bmap_place: how far the foe's slot moves from the
                               game's (battle units: up, back)                */
    BmapIntro intro;        /* bmap_place: the far cameras' own settings
                               (bmap_camera_intro)                             */
    BmapIntro cam;          /* bmap_place: the command camera's settings and ... */
    float trainer_size;     /* the foe trainer's height (1 the base; BmapSpot.trainer_size) */
    float trainer_own_size; /* the player's own trainer's, the same            */
    int has_ally;           /* a battle beside an ally: its trainer at slot 12 */
    float trainer_ally[3];  /* ... moved (right, up, back) ...                 */
    float trainer_ally_size; /* ... and its height                             */
    float trainer_foe[3], trainer_own[3]; /* ... the trainers' offsets (battle
                               units: right, up, back)                         */
    int view_set;           /* bmap_place: cam and the offsets are a battle's  */
    BmapUvAnim *uva; unsigned nuva; /* the texture-coordinate animations     */
    int shared;             /* a copy (bmap_copy): textures, the animations and
                               the ground index belong to the original, which
                               must outlive it                                */
} BmapScene;

/* Build the map around (px, pz) (map world units, matrix-absolute, as GameData's
 * playerWork holds them) for zone `zone`, season 0..3, time hour:minute. Loads a
 * window of chunks around the player (3x3, 5x5 near a chunk edge), each cell with
 * its own zone's area textures, plus buildings and placed objects. Returns 0 on
 * success. Thread: called once at battle start on the frame thread; must not touch
 * game memory beyond reading the archives through the port's file layer. */
int  bmap_build(BmapScene *s, unsigned zone, float px, float pz, int season,
                int hour, int minute);
void bmap_free(BmapScene *s);
/* A copy of an unplaced scene to place (the spot editor places one build
 * many times): its own vertices and draws, the original's textures and
 * ground index.  Returns 0 on success. */
int  bmap_copy(BmapScene *dst, const BmapScene *src);
/* The ground height under (x, z) map units, or the player's tile height.
 * After bmap_set_ref, where several surfaces lie under (x, z) (a floor and
 * the wall tops, a road and a bridge), the one nearest that height. */
float bmap_ground_y(const BmapScene *s, float x, float z);
void  bmap_set_ref(BmapScene *s, float y);
/* The highest solid surface under (x, z) at height y or below (a sheet's
 * own ground); *hit 0 when there is none. */
float bmap_ground_below(const BmapScene *s, float x, float z, float y, int *hit);
/* Back to no player height: the lowest surface (with a step up allowed). */
void  bmap_clear_ref(BmapScene *s);

/* The game's walk grid (the map's collision tiles) near the player. */
enum { BMAP_TILE_NONE, BMAP_TILE_BLOCKED, BMAP_TILE_WATER, BMAP_TILE_OPEN };
int   bmap_tile(const BmapScene *s, float x, float z);
/* Whether wild Pokemon appear on the tile (grass, cave floor, sand, water). */
int   bmap_tile_encounter(const BmapScene *s, float x, float z);
#define BMAP_TILE_ENCOUNTER 0x10    /* bmap_data.c: the flag beside the class */

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WORLD_BMAP_H */
