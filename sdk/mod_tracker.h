/*
 * sdk/mod_tracker.h -- what a mod shows on the field's Tracker
 * (docs/mod-services.md, "The Tracker").
 *
 * A mod hands over its whole tracker as text with tracker_set (a service,
 * version 6), each time something on it changes; the last one set is the
 * one shown.  The text is lines of tab-separated words:
 *
 *     area<TAB><name><TAB><zone>[,<zone>...]
 *     check<TAB>found|open|locked<TAB><name>
 *     item<TAB><group><TAB><count><TAB><name>
 *     count<TAB><found><TAB><total>
 *
 * A check belongs to the area above it.  `open`: it can be reached with
 * what the player has; `locked`: not yet.  An item's count 0 is an item not
 * held yet.  A new game and a loaded save clear the tracker, so a game
 * without the mod's data shows none.
 */
#ifndef BW_MOD_TRACKER_H
#define BW_MOD_TRACKER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_MOD_TRACKER_MAX (128u * 1024u)

/* The services table's tracker_set: the calling mod's tracker (size bytes,
 * no NUL needed); 0, or -1 when it is too long. */
int  bw_mod_tracker_set(const char *text, u32 size);
/* Nothing to show (a new game, a loaded save). */
void bw_mod_tracker_clear(void);
/* The text in force ("" for none, NUL-ended), the name of the mod that set
 * it, and a number that changes with each set or clear. */
const char *bw_mod_tracker_text(void);
const char *bw_mod_tracker_owner(void);
u32  bw_mod_tracker_stamp(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_MOD_TRACKER_H */
