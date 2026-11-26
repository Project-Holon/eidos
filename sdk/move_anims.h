/*
 * sdk/move_anims.h -- the animation a move plays in battle, from a mod's
 * table, and the preview of one (docs/game-tables.md, move_anims.txt).
 *
 * The battle view plays a move's animation from the move script archive
 * (member = the move's id), for the game's moves only: an id from 0x231 on
 * is read as one of the other effects (ov094 btlv_effvm.c,
 * BTLV_EFFVM_StartScript).  A mod's eidos/tables/move_anims.txt gives a
 * move another move's animation:
 *
 *     <move id> = <the game's move whose animation it plays, 1..559>
 *
 * A move with no line plays its own when it is one of the game's, else the
 * one of the move it works like (move_handlers.txt), else Pound's.
 */
#ifndef BW_PORT_MOVE_ANIMS_H
#define BW_PORT_MOVE_ANIMS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's moves with an animation of their own: 1 .. BW_ANIM_LAST. */
#define BW_ANIM_LAST 559u

/* Read every enabled mod's table again; a line that does not read is said
 * on stderr and left out. */
void bw_move_anims_load(void);

/* The game's move whose animation `move` plays. */
u32 bw_move_anim(u32 move);

/*
 * The preview (eidos --preview-move ID [--preview-species A B]): a battle
 * that plays the move's animation over and over.  0 when no preview.
 */
void bw_preview_set(u32 move, u32 attacker, u32 defender);
u32  bw_preview_move(void);
u32  bw_preview_attacker(void);
u32  bw_preview_defender(void);

/* For the tests: parse one line (0 read, 1 nothing, -1 refused with
 * `why`), and forget every line. */
int  bw_move_anims_parse_line(const char *line, char *why, int why_size);
void bw_move_anims_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOVE_ANIMS_H */
