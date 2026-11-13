/*
 * engine/world/remade/rock.h -- the rocks' hand-drawn faces (rock_faces.c),
 * for the rock builder (rock.c).
 */

#ifndef EIDOS_ENGINE_WORLD_REMADE_ROCK_H
#define EIDOS_ENGINE_WORLD_REMADE_ROCK_H

/* Each: lines "[east]", "[north]", "[west]" and rows "ROW FIRST LETTERS", then NULL. */
extern const char *const k_rock_faces_cave[];
extern const char *const k_rock_faces_open[];
extern const char *const k_rock_faces_water[];
extern const char *const k_rock_faces_plant[];

/* A bloom of a bunch of flowers, set on the body: `angle` degrees round it (0 south, 90 east), at the picture's row
 * `row`, `out` of the body's radius there from its middle (1: on its side; less: on its top); its kind ('O' orange,
 * 'Y' yellow, 'P' peach) and its size in pixels.  The list ends with kind 0. */
typedef struct RockBloom {
    short angle;
    float row, out;
    char kind;
    float rad;
} RockBloom;

extern const RockBloom k_bunch_flowers[];

#endif /* EIDOS_ENGINE_WORLD_REMADE_ROCK_H */
