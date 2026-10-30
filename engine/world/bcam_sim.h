/*
 * engine/world/bcam_sim.h -- the battle camera simulator.
 *
 * Plays the camera of a move, of the wild or trainer battle intro or of the
 * idle drift from the game's own data: the effect scripts (a/0/6/6 for
 * moves, a/0/6/7 for the other effects), the camera presets and slot
 * positions of overlay 94, and the particle files for the length of the
 * waits.  Nothing is recorded; the result is one frame per game frame.
 *
 * Units are battle units (fx32 / 4096), before any eidos zoom.  The caller
 * has set the data root (bw_set_data_root); the overlay image and the ARM9
 * tables are read from the extract next to it.
 */

#ifndef BCAM_SIM_H
#define BCAM_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* One game frame: the camera and the Pokemon the script moves. */
typedef struct BcamFrame {
    float eye[3], target[3];
    float poke[8][3];       /* battle units, slot 0..7 (0 the player's, 1 the foe's) */
    unsigned poke_mask;     /* bit n set: poke[n] is valid (the slot is on the field) */
    float trainer[6][3];    /* the trainers, slots 8..13 (9 the foe's, 8 the player's) */
    unsigned trainer_mask;  /* bit n set: trainer[n] is on the field */
} BcamFrame;

enum {
    BCAM_MOVE,              /* id = move number */
    BCAM_WILD_INTRO,        /* the wild battle entry (id unused) */
    BCAM_TRAINER_INTRO,     /* the trainer battle entry (id unused) */
    BCAM_IDLE,              /* the idle drift: id 0..5 one of the six camera moves, -1 all in turn */
    BCAM_TRAINER_MID        /* a trainer coming back mid-battle to speak (a leader's last
                               Pokemon): its slide in and out (id unused) */
};

/*
 * Run one camera scene.  attacker is the slot of the user (0 the player's,
 * 1 the foe's), defender the slot of the target.  On success (0) *frames is
 * a malloc'd array of *count frames, one per game frame from the first frame
 * the script runs to the frame after its last command; free it with free().
 * A negative return means the data could not be read or the id has no script.
 */
int bcam_sim_run(int kind, int id, int attacker, int defender, BcamFrame **frames, int *count);

/*
 * The weight of the Pokemon in slot 0 or 1, in tenths of a kilogram (default 1000).
 * Some scripts shake the camera harder for a heavy Pokemon.
 */
void bcam_sim_set_weight(int slot, int weight);

/* The game's place of battle slot `pos` (0..13) in a single or double battle,
 * battle units, into out.  Returns 0, or -1 when the tables cannot be read. */
int bcam_sim_slot(int pos, float out[3]);

/* The name of move `id` from the game's text, or NULL when it cannot be read. */
const char *bcam_sim_move_name(int id);

#ifdef __cplusplus
}
#endif

#endif
