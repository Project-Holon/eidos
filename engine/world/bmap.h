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

#include <math.h>
#include <time.h>

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

/* A 3D object the field was drawing when the battle began (a gym's curtain, a door, a stone the player pushed),
 * in the state it was drawn in: the game's own model, texture and animation files as they lay in its memory
 * (copies the caller keeps alive), each animation at its frame, and the place, turn and size of the draw.
 * The game notes them while the field runs (sdk/battle_view.c) and hands them over when a battle begins
 * (bmap_live_set); the scene built next for that zone stands them in it, and leaves out the same objects in
 * their starting state from the game's files. */
#define BMAP_LIVE_ANIMS 4
typedef struct BmapLive {
    const unsigned char *mdl, *tex;     /* the model file; the texture file, or NULL: the model's own */
    unsigned mdl_n, tex_n;
    struct { const unsigned char *p; unsigned n; int frame; } anim[BMAP_LIVE_ANIMS];
    unsigned nanim;
    float t[3], r[3][3];                /* a corner v of the model goes to t + v[0] r[0] + v[1] r[1] + v[2] r[2] */
    float field_tan;                    /* the tangent of the field camera's pitch (for a model made for that camera) */
} BmapLive;
void bmap_live_set(unsigned zone, const BmapLive *list, unsigned n);

/* The surf mount (the `mount` draws) is left out while set: the player's own Pokemon in battle is the one the
 * player surfs on, so it stands there itself (sdk/battle_view.c sets it each frame). */
void bmap_set_mount_hidden(int hidden);
int bmap_mount_hidden(void);
/* The camera's clearance takes a tree whole.  `clear` holds n places the camera stands in or passed (x, y, z,
 * radius: balls, battle units); a tree of the placed scene (BmapScene.trees) whose box on the ground lies within
 * 1.5 units of one of those places (the camera in it or right beside it, not the ball's whole reach), or that
 * stands in the line of sight from the eye to one of the `naim` places of `aim` (the battle's middle: x, y, z and a
 * height, a place with a height of 0 or less left out) within 6 units of the eye, is added as an entry of its own with a negative radius (x, its tag, z, -its width), the nearest first, 3 at
 * most; the last balls
 * make room when the list (max entries) is full.  The renderers leave out the triangles that carry that tag within
 * that width.  Returns the new count. */
struct BmapScene;
/* The trees a battle at this placed scene leaves out, the whole battle long: those the rule below takes at the
 * place's two still cameras, the command menu's and the intro's (as the place draws them: bmap_battle_camera), toward
 * the middle between the two sides' places, and every tree one of those two cameras stands inside (its place within
 * 0.3 of the tree's box, at a height the tree has).  No other tree goes at any time: a camera move that passes
 * through a tree shows it (a tree that left for those frames only popped out and back).  Entries as
 * bmap_clear_trees makes them; returns their count (max at most).  The game and the spot editor both use it, so a
 * battle shows the trees the editor shows. */
unsigned bmap_place_trees(const struct BmapScene *s, float out[][4], unsigned max);
unsigned bmap_clear_trees(const struct BmapScene *s, const float eye[3], const float (*aim)[4], unsigned naim,
                          float clear[][4], unsigned n, unsigned max);

typedef struct BmapTexture {
    int w, h;
    unsigned char *rgba;    /* w*h*4, straight alpha                            */
    unsigned char repeat_s, repeat_t, flip_s, flip_t;
    char name[17];          /* the picture's own name in the game's files (the map makers': "ki02ax" a tree's side,
                               "kusa_ec1" tall grass); "" for a picture the port made */
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
    unsigned char mount;       /* 1: the surf mount (a live object): under the own Pokemon,
                                  left out while bmap_mount_hidden()                    */
    unsigned char bob;         /* 1: rises and falls by BmapScene.bob_y (a moving object
                                  of the game with its own animation)               */
    unsigned char leafy;       /* 1: hand-made grass leaves (bmap_grass): its triangles
                                  are listed leaf by leaf in BmapScene.leaf            */
    unsigned leaf0, nleaf;     /* ... its leaves there: the first, and how many       */
} BmapDraw;

/* One hand-made grass leaf of a placed scene (bmap_grass, research): the pass that lays the
 * grass over a Pokemon takes a leaf whole, and a ring of leaves together. */
typedef struct BmapLeaf {
    unsigned first, count;     /* its vertices in BmapScene.v                         */
    unsigned draw;             /* its draw (BmapScene.d)                              */
    float tip[3];              /* its tip (battle units)                              */
    float heart[2];            /* its bush's heart, x and z (battle units)            */
    float reach;               /* how far its ring's leaves reach from the heart      */
} BmapLeaf;

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

#define BMAP_BOB_MAX 128
typedef struct BmapScene {
    float bob_y[BMAP_BOB_MAX]; unsigned bob_n;  /* the rise of the `bob` draws at each frame of their animation
                                                   (map units; 30 frames a second, as the field plays it) */
    int has_mount;          /* the player surfed: the `mount` draws, the field's place of the mount and where its
                               front pointed (map units; bmap_place moves them under the own Pokemon) */
    float mount_t[3], mount_fwd[2];
    BmapVertex *v; unsigned nv;
    BmapDraw *d; unsigned nd;
    BmapTexture *t; unsigned nt;
    unsigned char clear[3]; /* the light keyframe's clear (sky) colour           */
    unsigned char fog[3]; int fog_on;
    float fog_pitch, fog_yaw; /* fog_on: the zone's field camera (radians): the game fogs by depth
                                 along that camera's line of sight, so the fog is measured along it */
    float fog_k[4];           /* bmap_place: the fog of a point p of the placed scene (battle units) is
                                 clamp(dot(p, fog_k.xyz) - fog_k.w, 0, 1): its depth past the player along the
                                 field camera's line of sight, between fog_start and fog_end.  A wall on the
                                 player's own floor is fogged by how far it is behind the player as that
                                 camera sees it (little), the floors below by how deep they lie (much): as
                                 the field shows the place.  (A flat distance from the player turned every
                                 far wall of a wide room into the fog's colour.) */
    float fog_start, fog_end; /* fog_on: distance from the player (map units) where the
                                 fog begins and where it is full: the zone's own fog, or the
                                 weather's (bmap_set_weather), which may begin in front of the
                                 player (a negative start)                         */
    float fog_curve;          /* fog_on: how the fog thickens past fog_start.  0: evenly, full at fog_end
                                 (the zone's fog).  More (the weather's fog): as the DS's depth buffer
                                 makes it, fast at first and slowly after, and not always to full: with
                                 t the depth past fog_start over (fog_end - fog_start), which is then
                                 the field camera's distance to fog_start, the fog is
                                 curve * t / (1 + t), at most 1. */
    int weather;              /* the weather the scene was built with (the game's weather id), -1 none */
    float fog_dist;           /* fog_on: the field camera's distance from the player (map units): fog_start +
                                 fog_dist is the depth from that camera where the fog begins */
    int fog_sprites;          /* the battle's own sprites (the Pokemon, the trainers) take the fog of their
                                 place (bmap_fog_at): the setting Extras > Weather haze, a weather's fog only */
    int fog_two;              /* a weather's fog: measured from the battle's own camera too, along its line of
                                 sight, and the thicker of the two measures holds (bmap_fog_k2).  The field
                                 camera's measure alone leaves a battle that looks across the field's line of
                                 sight clear where the field was hazed; the zone's own fog keeps that one */
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
    unsigned sky_first, sky_count; /* the cloud dome (bmap_data.c add_clouds): vertices of v that
                               no draw holds, drawn first by the renderers' sky pass; 0 none */
    int sky_tex;            /* ... its cloud picture (the zone's own pattern)  */
    float sky_h, sky_rep;   /* ... the layer's height over the camera and one repeat of the
                               picture (map units)                             */
    float sky_drift;        /* ... seconds for the clouds to drift one repeat  */
    float sky_u[3], sky_v[3]; /* bmap_place: the picture's coordinates of a layer point (x, z)
                               in battle space, u . (x, z, 1) and v . (x, z, 1), before the
                               drift (the layer fixed to the map)              */
    float sky_tilt;         /* ... how far its horizon lies under the camera's level (a sine):
                               the battle camera looks a little down, and the sky it sees is
                               mostly beyond the map's edge, under the level */
    float (*trees)[8];      /* bmap_place: the trees (groups of foliage pieces) in battle
                               units: x, z, radius, foot, top, the tag its vertices carry
                               (BmapVertex.a, 1..254; 0: no tree), and half its box's sides
                               along x and z; the camera's clearance leaves out whole the
                               one or two the camera stands in or right beside           */
    unsigned n_trees;
    BmapLeaf *leaf;         /* the hand-made grass leaves (bmap_place, research); owned */
    unsigned n_leaf;
    float (*bound)[4];      /* a sphere a draw (bmap_draw_bounds), or NULL: the renderer makes them; owned.
                               The battle's job makes them on its own thread (0.09 s of the frame the map
                               first showed, on a grassy route) */
    unsigned n_bound;       /* nd when they were made */
    int shared;             /* a copy (bmap_copy): textures, the animations and
                               the ground index belong to the original, which
                               must outlive it                                */
    unsigned tex_gen;       /* changes whenever t is made or grows (bmap_tex_gen):
                               copies share it, so a renderer keeps their
                               textures from one placing to the next          */
    int prepared;           /* bmap_place_prepare done: 1 battle, 2 field      */
    unsigned char *prep;    /* ... its grass: prep_nd0 bytes (a draw holds grass),
                               then prep_ntri (a triangle is grass); owned by
                               this scene, copied by bmap_copy                 */
    unsigned prep_nd0, prep_ntri;
} BmapScene;

/* A sphere around each draw of a placed scene, into out[nd][4]: its middle (x, y, z; a billboard's pivot, so it
 * holds the triangles whichever way they turn) and its radius, battle units; the radius 1e30 for a draw with no
 * vertex (never left out).  The renderers leave out a draw whose sphere is off the screen.  (In the header: the
 * renderers are linked into programs with no bmap_place.c.) */
static inline void bmap_draw_bounds(const BmapScene *s, float (*out)[4])
{
    unsigned d, k;

    for (d = 0; d < s->nd; d++) {
        const BmapDraw *dr = &s->d[d];
        float *bb = out[d];
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        float r2 = 0.0f;

        for (k = 0; k < dr->count && dr->first + k < s->nv; k++) {
            const BmapVertex *v = &s->v[dr->first + k];

            lo[0] = fminf(lo[0], v->x); hi[0] = fmaxf(hi[0], v->x);
            lo[1] = fminf(lo[1], v->y); hi[1] = fmaxf(hi[1], v->y);
            lo[2] = fminf(lo[2], v->z); hi[2] = fmaxf(hi[2], v->z);
        }
        if (lo[0] > hi[0]) {
            bb[0] = bb[1] = bb[2] = 0.0f;
            bb[3] = 1e30f;              /* no triangles: never left out */
            continue;
        }
        for (k = 0; k < 3; k++) {
            bb[k] = dr->billboard ? dr->pivot[k] : 0.5f * (lo[k] + hi[k]);
        }
        for (k = 0; k < dr->count && dr->first + k < s->nv; k++) {
            const BmapVertex *v = &s->v[dr->first + k];
            float dx = v->x - bb[0], dy = v->y - bb[1], dz = v->z - bb[2];

            r2 = fmaxf(r2, dx * dx + dy * dy + dz * dz);
        }
        bb[3] = sqrtf(r2);
    }
}

/* The fog's measure from the battle's own camera (eye and unit line of sight, battle units): the scene's two fog
 * depths from that camera, in fog_k's form. */
static inline void bmap_fog_k_eye(const BmapScene *s, const float eye[3], const float fwd[3], float out[4])
{
    const float span = (s->fog_end - s->fog_start) * s->place_k;

    out[0] = fwd[0] / span;
    out[1] = fwd[1] / span;
    out[2] = fwd[2] / span;
    out[3] = eye[0] * out[0] + eye[1] * out[1] + eye[2] * out[2] + (s->fog_start + s->fog_dist) * s->place_k / span;
}

/* The fog's measure for a frame of the battle's camera: out as fog_k, the fog of a point p being
 * clamp(dot(p, out.xyz) - out.w, 0, 1), or bent by fog_curve.  The placed scene's own fog_k (the field camera's line
 * of sight). */
static inline void bmap_fog_k(const BmapScene *s, const float eye[3], const float fwd[3], float out[4])
{
    (void)eye;
    (void)fwd;
    out[0] = s->fog_k[0]; out[1] = s->fog_k[1]; out[2] = s->fog_k[2]; out[3] = s->fog_k[3];
}

/* The second measure, from the battle's camera (fog_two: a weather's fog): the thicker of the two holds; zeros: none */
static inline void bmap_fog_k2(const BmapScene *s, const float eye[3], const float fwd[3], float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (s->fog_two && s->fog_on && s->fog_end > s->fog_start && s->place_k > 0.0f) {
        bmap_fog_k_eye(s, eye, fwd, out);
    }
}

/* The fog at the point p of the placed scene (battle units) for a frame of the battle's camera: 0 none to 1 full. */
static inline float bmap_fog_at(const BmapScene *s, const float eye[3], const float fwd[3], const float p[3])
{
    float k[4], f = 0.0f;
    int pass;

    if (!s->fog_on || s->fog_end <= s->fog_start || (s->fog_k[0] == 0.0f && s->fog_k[1] == 0.0f && s->fog_k[2] == 0.0f)) {
        return 0.0f;
    }
    for (pass = 0; pass < 2; pass++) {
        float t, g;

        if (pass == 0) {
            bmap_fog_k(s, eye, fwd, k);
        } else {
            bmap_fog_k2(s, eye, fwd, k);
            if (k[0] == 0.0f && k[1] == 0.0f && k[2] == 0.0f) {
                break;
            }
        }
        t = p[0] * k[0] + p[1] * k[1] + p[2] * k[2] - k[3];
        if (s->fog_curve > 0.0f) {
            t = t < 0.0f ? 0.0f : t;
            g = s->fog_curve * t / (1.0f + t);
        } else {
            g = t;
        }
        g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
        f = g > f ? g : f;
    }
    return f;
}

/* The cloud layer's picture coordinates this moment: a point (x, z) of the
 * layer in battle space reads the picture at (u . (x, z, 1), v . (x, z, 1)).
 * The layer is fixed to the map (sky_u, sky_v: the point turned back into map
 * units by the scene's placing) and drifts along u by the real clock, so every
 * battle sees the clouds where they are on the map at that time. */
static inline void bmap_sky_uv(const BmapScene *s, float u[3], float v[3])
{
    double t = 0.0, cu;
    struct timespec ts;
    int i;

    if (s->sky_drift > 0.0f && timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        const double period = (double)s->sky_drift;

        t = fmod((double)(ts.tv_sec % 86400) + (double)ts.tv_nsec * 1e-9, period) / period;
    }
    for (i = 0; i < 3; i++) {
        u[i] = s->sky_u[i];
        v[i] = s->sky_v[i];
    }
    cu = (double)u[2] + t;
    u[2] = (float)(cu - floor(cu));
}

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
/* A new BmapScene.tex_gen, never 0. */
unsigned bmap_tex_gen(void);
/* The ground height under (x, z) map units, or the player's tile height.
 * After bmap_set_ref, where several surfaces lie under (x, z) (a floor and
 * the wall tops, a road and a bridge), the one nearest that height. */
float bmap_ground_y(const BmapScene *s, float x, float z);
void  bmap_set_ref(BmapScene *s, float y);
/* The highest solid surface under (x, z) at height y or below (a sheet's
 * own ground); *hit 0 when there is none. */
float bmap_ground_below(const BmapScene *s, float x, float z, float y, int *hit);
/* The same, the floors of placed models counted too (White Forest's floor is one). */
float bmap_floor_below(const BmapScene *s, float x, float z, float y, int *hit);
/* Back to no player height: the lowest surface (with a step up allowed). */
void  bmap_clear_ref(BmapScene *s);

/* The game's walk grid (the map's collision tiles) near the player. */
enum { BMAP_TILE_NONE, BMAP_TILE_BLOCKED, BMAP_TILE_WATER, BMAP_TILE_OPEN };
int   bmap_tile(const BmapScene *s, float x, float z);
/* Whether wild Pokemon appear on the tile (grass, cave floor, sand, water). */
int   bmap_tile_encounter(const BmapScene *s, float x, float z);
/* The height the GAME gives the tile under (x, z), at its middle (map units): its collision tile's own plane, solved
 * as the field solves it for the player (FIELD_G3D_MAPPER_GetTilePlane).  Where the chunk has two plane sections (a
 * bridge over a path), the one where wild Pokemon appear, else the first.  Returns 0 when it is not known (no
 * tile there, or the game's two plane tables could not be read: they come from the extract's overlay 21). */
int   bmap_tile_height(const BmapScene *s, float x, float z, float *y);
/* Every height the game has for the tile under (x, z): its plane sections' (one, or two where floors lie over one
 * another), into y[0..1].  Returns their count (0: not known, as bmap_tile_height). */
int   bmap_tile_planes(const BmapScene *s, float x, float z, float y[2]);
#define BMAP_TILE_ENCOUNTER 0x10    /* bmap_data.c: the flag beside the class */

/* Black City (zone 0) and White Forest (zone 424): their buildings and
 * trees are not in their maps.  The field picks 8x8-tile blocks for an 8x8
 * grid over the place's 2x2 cells from the save (ov021 field_wfbc.c
 * FIELD_WFBC_BLOCK_BuildGrid: the residents, the player's id) and stamps each
 * block's buildings and walk tiles into the map (FIELD_WFBC_PatchLandData,
 * land_data_patch.c: a/1/5/6 member = block id). */
typedef struct BmapWfbc {
    unsigned char type[8][8];       /* 0xFF: no block on the cell             */
    unsigned char ox[8][8], oz[8][8];   /* the cell within its block (0 its origin) */
    unsigned short block[8][8];     /* the a/1/5/6 member                    */
} BmapWfbc;
/* The grid the game builds for these residents (their record numbers, the
 * a/1/5/4 members) and this player id.  0 on success; -1 for another zone. */
int  bmap_wfbc_grid(unsigned zone, const unsigned char *npc_ids, int n, unsigned player_id, BmapWfbc *out);
/* The grid of a place with ten residents, as the spot editor shows it
 * (bmap_npc_add_residents' records: Black City 0 to 9, White Forest 15 to 24). */
int  bmap_wfbc_full(unsigned zone, BmapWfbc *out);
/* The grid the next bmap_build of zone 0 or 424 stamps (the save's, from the
 * battle backdrop); NULL or none set: the full one.  Taken by that build. */
void bmap_set_wfbc(const BmapWfbc *w);
/* The maps the field draws (its mapper's grid, aletheia ov021 FieldG3dMapper, noted each field frame): after the
 * game's map replacement (MAP_MATRIX_ApplyReplace, a/0/1/0) as the player saw it, so the battle shows the maps the
 * field showed: the version's, a linked game's, and the ones story flags swap in (the bedroom after the first battle,
 * the League after N's Castle rises).  zone: the player's zone then; matrix: the field's matrix file (a/0/0/9
 * member, read for its zone grid), 0xFFFFFFFF when unknown; grid: the map index per cell, w * h of them. */
typedef struct BmapFieldMatrix {
    unsigned zone, matrix, w, h;
    unsigned grid[900];
} BmapFieldMatrix;
/* The next bmap_build takes its maps from this when it is the build zone's matrix (the same zone, or the zone's own
 * matrix id); NULL or none set: the zone's matrix from the files.  Taken by that build. */
void bmap_set_field_matrix(const BmapFieldMatrix *m);
/* Where the player stands (map units), when it is known: a lift of the zone's gimmick whose area holds (x, z) is
 * stood at the height y by the next bmap_build when y is one of the lift's own two heights (bmap_gimmick_floors:
 * the player rides it; the gimmick's models stand at their first state otherwise).  has 0: not known.  Taken by
 * that build.  (In play the field's own objects replace the gimmick's models: bmap_live_set.) */
void bmap_set_rider(float x, float z, float y, int has);
/* The weather the field showed (the game's weather id: 1 snow, 2 rain, 3 the sandstorm ...; 0 or negative: clear).
 * A weather kind changes two things of the place while it is on, and the next bmap_build makes them: the light
 * (WEATHER_TASK_SetLight: the weather's own light table, archive 0x3E, in place of the area's) and the fog
 * (WEATHER_TASK_InitFog: the kind's own slope and offset, in the fog colour of that light table, in place of the
 * zone's).  Taken by that build. */
void bmap_set_weather(int weather_id);
/* The remade models' store: a folder where each remade model (bmap_remade.h) is written once made and read back in
 * later sessions (making one takes about 0.3 s; a place's first battle waited for all of its own).  `stamp` names the
 * program that made them (its version and build): a file made by another is made again.  NULL or "": no store.
 * EIDOS_NO_REMADE_STORE=1 turns it off. */
void bmap_set_remade_store(const char *dir, const char *stamp);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WORLD_BMAP_H */
