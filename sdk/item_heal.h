/*
 * sdk/item_heal.h -- how much HP an item heals, from a mod's table
 * (docs/game-tables.md, item_heal.txt).
 *
 * The item record keeps the HP an item restores in one byte: 1..252 HP, and
 * 253, 254, 255 for a quarter, a half and all of the Pokemon's HP (bag and
 * party use, record param 0x3A); a held berry's fixed heal is its hold
 * effect parameter, 1..255.  A mod's eidos/tables/item_heal.txt, one line an
 * item, gives more:
 *
 *     <item> = [hp=<1..65535 | 1/2..1/16 | full>] [held_hp=<1..65535>]
 *
 * `hp` is the bag and party heal (and a revive's HP), `held_hp` the HP a held
 * item that heals a fixed amount gives when eaten (Oran Berry's handler).  A
 * later mod's line for the same item replaces an earlier one; an item with
 * no line keeps the record's byte.  The game's heal code asks here first
 * (stubs/ov/patch_ov093_btl_server_flow.c, patch_ov091_plist_demo.c,
 * patch_ov093_hand_item.c), and the amount is never more than the HP the
 * Pokemon is missing.
 */
#ifndef BW_PORT_ITEM_HEAL_H
#define BW_PORT_ITEM_HEAL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read every enabled mod's table again; a line that does not read is said
 * on stderr and left out. */
void bw_item_heal_load(void);

/* The HP `item` heals from the bag or the party for a Pokemon of `max_hp`
 * missing `missing`: 1 with *amount (at most `missing`), or 0 when the item
 * has no `hp` (the record's byte applies). */
int bw_item_heal_hp(u32 item, u32 max_hp, u32 missing, u32 *amount);

/* The HP `item` heals when eaten as a held fixed heal: 1 with *amount (at
 * most `missing`), or 0 when it has no `held_hp`. */
int bw_item_heal_held_hp(u32 item, u32 missing, u32 *amount);

/* Parse one table line (for the tests): 0 read, 1 nothing on the line, -1
 * refused with `why`. */
int bw_item_heal_parse_line(const char *line, char *why, int why_size);
void bw_item_heal_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ITEM_HEAL_H */
