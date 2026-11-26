/*
 * sdk/shops.h -- the items shops sell, from a mod's table (docs/game-tables.md,
 * shops.txt).
 *
 * A mod's eidos/tables/shops.txt, one line per list:
 *
 *     mart.<n> = <item> <item> ...           (the game's lists 0..25)
 *     bp.<n>   = <item>:<price> ...          (the BP lists 0..2)
 *
 * A mart list's prices are the items' own (with the Pass Power discount, as
 * the game does); a BP list gives each item's price in Battle Points.  At
 * most 40 items a list (the shop's own room).  A later mod's line for the
 * same list replaces an earlier one; a list with no line is the game's.
 *
 * The shop's list builders (overlay 21, stubs/ov/patch_ov021_scrcmd_proc_fld.c)
 * ask here first.
 */
#ifndef BW_PORT_SHOPS_H
#define BW_PORT_SHOPS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_SHOP_MARTS 26
#define BW_SHOP_BP    3
#define BW_SHOP_ITEMS 40

/* Read every enabled mod's table again; a line that does not read is said
 * on stderr and left out. */
void bw_shops_load(void);

/* A mart list's items when a mod gives the list (*items, the count
 * returned), else -1. */
int bw_shop_mart(int list, const u16 **items);

/* A BP list's items and prices when a mod gives the list, else -1. */
int bw_shop_bp(int list, const u16 **items, const s32 **prices);

/* For the tests: parse one line (0 read, 1 nothing, -1 refused with
 * `why`), and forget every line. */
int  bw_shops_parse_line(const char *line, char *why, int why_size);
void bw_shops_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SHOPS_H */
