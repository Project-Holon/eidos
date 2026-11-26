/*
 * sdk/type_icons.h -- the pictures of the types mods add (type_icons.c).
 */

#ifndef BW_PORT_TYPE_ICONS_H
#define BW_PORT_TYPE_ICONS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The mod list changed (sdk/bw_limits.c): read types.txt again and make the
 * added types' icon variants and battle button palettes. */
void bw_type_icons_apply(void);

/* An overlay's names were just bound (sdk/mod_v6.c): the per-type tables of
 * ov094 (battle move buttons) and ov180 (Hall of Fame) take rows for the
 * added types. */
void bw_type_icons_overlay_loaded(u32 id);

/*
 * A screen that shows type icons and takes added types.  Its type palette
 * (a/0/8/3 member 0x21) sits at hardware OBJ row `base` of `engine` (0 main,
 * 1 sub), and the added types' colours go to row `row` there, which an icon
 * reaches with palette offset `row - base` (4 bits, wrapping).  `palanm` and
 * `bank` are the screen's palette animator and its bank for that engine's
 * OBJ palette (NULL when the screen has none): the colours go to its two
 * buffers too, so a fade keeps them.  `type_rows` is the row mask (in the
 * animator's terms) of the screen's three type rows: a fade over all of them
 * covers the added types' row as well (sdk/palanm_fade.c).
 *
 * `owner` names the screen; pushing an owner already on the stack starts it
 * over (its row was loaded again).  The screen on top gets the icons.
 */
void bw_type_screen_push(const void *owner, int engine, int base, int row,
                         void *palanm, int bank, u16 type_rows);
void bw_type_screen_pop(const void *owner);

/* An added type's colour on the one-screen interface (types.txt `color`,
 * else its first `colors`): 1 and *rgb (0xRRGGBB), or 0. */
int bw_type_ui_color(u32 type, u32 *rgb);

/* The row a fade over `rows` of `bank` of `palanm` also covers (palanm_fade.c):
 * the bit to add, or 0. */
u16 bw_type_screen_fade_extra(const void *palanm, int bank, u16 rows);

#ifdef __cplusplus
}
#endif

#endif
