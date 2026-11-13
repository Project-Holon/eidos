/*
 * engine/world/remade/remade.h -- the remade models of the battle backdrop
 * (docs/battle-backdrop.md).
 *
 * The map draws many small things as one flat picture: a rock, a seat, a
 * bottle, a gate.  The field's steep camera never shows that; a battle
 * camera low over the ground does.  A remade model is such a picture built
 * as a body, by a builder of this folder, from the model's own triangles and
 * pixels, when the battle's map is built.  One file a kind of thing; the
 * table of which model takes which builder is ../bmap_remade.c.
 *
 * A builder gets the model as the map builder makes it standing alone (its
 * triangles in the model's own space, by draw, and its textures) and adds to
 * an RmOut: the triangles it leaves out, the new ones, and textures of its
 * own.  A new triangle takes the material of the draw command of the
 * triangle `src` it stands in for.
 */

#ifndef EIDOS_ENGINE_WORLD_REMADE_H
#define EIDOS_ENGINE_WORLD_REMADE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One triangle of the model.  `id` names it: ((draw command + 1) << 16) |
 * its place in that command's list. */
typedef struct RmTri {
    float p[3][3];              /* corners, map units, the model's own space    */
    float uv[3][2];             /* texture coordinates, 0..1 a repeat           */
    unsigned id;
    unsigned char rgb[3];       /* the mean of its corners' lit colours         */
} RmTri;

/* The triangles of one draw of the model. */
typedef struct RmBatch {
    const RmTri *tri;
    unsigned n;
    int tex;                    /* index into the textures, -1: none            */
    unsigned char cull;         /* 0 none, 1 back, 2 front                      */
    unsigned char bb;           /* a camera-facing picture (the map turns it)   */
    float pivot[3];
    unsigned char rgb[3];       /* the mean of its corners' lit colours         */
} RmBatch;

typedef struct RmTex {
    int w, h;
    const unsigned char *rgba;  /* w*h*4, straight alpha; NULL: not decoded     */
} RmTex;

/* A new triangle.  kind 1: it keeps the battle's own directions (a shadow
 * that falls as the Pokemon's do); kind 2: its body rests on the floor under
 * it.  alpha 1..30: see-through (of 31); 0: solid. */
typedef struct RmNew {
    unsigned src;
    int tex;                    /* -1: the texture of `src`'s material, else one of the output's own */
    unsigned char cull, lit, rgb[3], kind, alpha;
    float p[3][3], uv[3][2], n[3];
} RmNew;

#define RM_FLAG_REST 1u         /* the model as a whole rests on the floor under it */

/* A copy of a triangle of the model, turned half round about the upright through (cx, cz). */
typedef struct RmTurn {
    unsigned id;
    float cx, cz;
} RmTurn;

typedef struct RmOut {
    unsigned *drop;             /* the model's triangles left out               */
    unsigned ndrop, cdrop;
    unsigned *two;              /* the model's triangles shown from both sides  */
    unsigned ntwo, ctwo;
    RmTurn *turn;               /* turned copies, lit as the game lights the model: one a triangle */
    unsigned nturn, cturn;
    RmNew *tri;
    unsigned ntri, ctri;
    struct { int w, h; unsigned char *rgba; } tex[8];
    int ntex;
    unsigned flags;
    int fail;                   /* out of memory: the model stays as it is      */
} RmOut;

/* A builder.  `opt`: its settings for this model, "name=value,name=value".
 * Returns nothing: what it does not add stays as the map has it. */
typedef void RmBuild(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out);

RmBuild rm_cushion;             /* cushion.c: a pouffe                          */
RmBuild rm_bottles;             /* bottles.c: a standing and a lying bottle     */
RmBuild rm_disc;                /* disc.c: a round plate with rings (a warp pad) */
RmBuild rm_gate;                /* gate.c: a cut-out picture given its depth    */
RmBuild rm_floor;               /* floor.c: the model as it is, on the floor    */
RmBuild rm_rock;                /* rock.c: a rock, a vase: a body of pixel-sized blocks */
RmBuild rm_turned;              /* turned.c: a thing alike front and back, its missing back as its front turned */
RmBuild rm_backwall;            /* backwall.c: a flat back for a long box, in its side wall's pixels */
RmBuild rm_plant;               /* plant.c: the potted plant: foliage a body, the pot closed */
RmBuild rm_building;            /* building.c: a building's back wall and its roof's far slope */
RmBuild rm_tower;               /* tower.c: a tower on a block: its north faces, back walls and undersides */
RmBuild rm_hall;               /* hall.c: a box hall: its back wall in its sides' texture, its roof's back end, tanks on a side */
RmBuild rm_sprite;              /* sprite.c: a picture card over a stand: a body that is the picture from the front */
RmBuild rm_chandelier;          /* chandelier.c: a candle chandelier: a round frame, round candles, flames from any side */
RmBuild rm_counter;           /* counter.c: a kitchen counter: its back and corner, the sink's wall, tap and stove standing up */

/* --- helpers (common.c) --- */

void rm_drop(RmOut *o, unsigned id);
void rm_drop_all(RmOut *o, const RmBatch *b);
/* No face is seen from its undrawn side: every one-sided triangle of the remade model (the model's own that
 * are kept, their turned copies, the new ones without a texture of their own) gets a back in its own pixels,
 * `shade` darker.  Called last by a builder of a thing the map left open. */
void rm_seal(const RmBatch *b, unsigned nb, RmOut *out, double shade);
void rm_two(RmOut *o, unsigned id);
void rm_turn(RmOut *o, unsigned id, double cx, double cz);
/* Adds a triangle to the output; its normal is its corners' own. */
void rm_tri(RmOut *o, unsigned src, int tex, int cull, const unsigned char rgb[3], const double a[3],
            const double b[3], const double c[3], const double uva[2], const double uvb[2], const double uvc[2],
            int kind, int alpha);

/* A setting of `opt` copied to buf, or NULL when it is not set. */
const char *rm_opt(const char *opt, const char *name, char *buf, size_t n);
double rm_optf(const char *opt, const char *name, double def);
/* A list of colours "RRGGBB+RRGGBB": up to `max` of them as 0xRRGGBB; their count. */
unsigned rm_colours(const char *list, unsigned *out, unsigned max);
int rm_has_colour(const unsigned *list, unsigned n, const unsigned char *px);

/* The plane of a flat piece: position = u * m[0] + v * m[1] + m[2], from the
 * corners of its first triangle.  0 on success. */
int rm_plane(const RmTri *t, double m[3][3]);
void rm_at(const double m[3][3], double u, double v, double out[3]);

/* A wave from -1 to 1 and back over a turn (`turns`: any number): 0 at 0,
 * 1 at a quarter turn, 0 at a half, -1 at three quarters. */
double rm_wave(double turns);

/* small vector helpers */
void rm_add(const double a[3], const double b[3], double s, double out[3]);    /* a + s * b */
void rm_cross(const double a[3], const double b[3], double out[3]);
double rm_dot(const double a[3], const double b[3]);
double rm_norm(double v[3]);                                                   /* makes v a unit vector; its length */

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WORLD_REMADE_H */
