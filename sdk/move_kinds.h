/*
 * sdk/move_kinds.h -- the kinds of move later games tell apart that Black
 * and White's move records have no flag for: bite, pulse, ball and bomb,
 * powder, and one_hit (a move whose effect a second hit would repeat:
 * docs/battle-handlers.md, "Move kinds").
 *
 * eidos's own ability bases ask them (Strong Jaw: bite moves, Mega
 * Launcher: pulse moves, Bulletproof: ball and bomb moves, Parental Bond:
 * one_hit moves it does not hit twice with).  The game's
 * moves of each kind are built in, from the later games' lists; a mod's
 * eidos/tables/move_kinds.txt gives a move its kinds:
 *
 *     <move id> = <kind> [<kind> ...]       # or `none`
 *
 *     560 = bite               # an added move that bites
 *     44 = none                # Bite is not a bite move any more
 *
 * A line replaces the move's kinds.  A later mod's line for the same move
 * wins.  The kinds the move record has a flag for (contact, punch, sound)
 * stay the record's.
 */
#ifndef BW_PORT_MOVE_KINDS_H
#define BW_PORT_MOVE_KINDS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the kinds, as bits */
#define BW_MOVE_KIND_BITE    0x01u
#define BW_MOVE_KIND_PULSE   0x02u
#define BW_MOVE_KIND_BALL    0x04u
#define BW_MOVE_KIND_POWDER  0x08u
#define BW_MOVE_KIND_ONE_HIT 0x10u      /* hits once by its nature: Parental Bond does not repeat it */

/* The kinds of `move` (BW_MOVE_KIND_* bits, 0 for none). */
u32  bw_move_kinds(u32 move);

/* Read every enabled mod's move_kinds.txt again (sdk/battle_handlers.c
 * bw_bh_load calls it). */
void bw_move_kinds_load(void);

/* One table line (for the tests): 0 read, 1 nothing on the line, -1
 * refused with `why`. */
int  bw_move_kinds_parse_line(const char *line, char *why, int why_size);
void bw_move_kinds_clear(void);

/* The kind's bit for its word ("bite"), 0 for a word that is no kind. */
u32  bw_move_kind_by_name(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOVE_KINDS_H */
