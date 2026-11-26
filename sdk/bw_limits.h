/*
 * sdk/bw_limits.h -- the game's content limits, raised by mods.
 *
 * aletheia names each limit the game tests against (src/game/port_limits.h:
 * `ITEM_LAST` is `BW_LIMIT(ITEM_LAST, 626)`).  On the DS the name is the
 * original number; in the port it calls bw_limit_get, answered here.
 *
 * A limit is the vanilla value, raised to the highest value any enabled mod
 * declares in its manifest (`limit.items = 700`), capped at the ceiling the
 * game's storage allows.  Where new content goes is the mod author's choice;
 * the port only raises the limit (docs/mods.md, "Adding content").
 *
 * Tables the game holds with one row per id (the item icon table) cannot
 * grow by raising a number.  The port replaces them with copies that have
 * a row for every id up to the limit: the vanilla rows, then the rows mods
 * give in `<mod>/eidos/tables/<table>.txt` (`<id> = <value> <value> ...`),
 * a later mod winning per row.
 */

#ifndef BW_PORT_LIMITS_H
#define BW_PORT_LIMITS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The ids: the same numbers as aletheia's enum BwLimitId. */
enum {
    BW_PORT_LIMIT_ITEM_LAST = 0,
    BW_PORT_LIMIT_MOVE_LAST = 1,
    BW_PORT_LIMIT_BTLV_EFFECT_MOVE_END = 2,
    BW_PORT_LIMIT_FLDMMDL_OBJCODE_TABLE_COUNT = 3,
    BW_PORT_LIMIT_SPECIES_LAST = 4,
    BW_PORT_LIMIT_TYPE_LAST = 5,
    BW_PORT_LIMIT_COUNT
};

/* The value a named limit has in this run. */
u32 bw_limit_get(int id);

/*
 * Read the limits and the table rows from the enabled mods (their
 * manifests and eidos/tables/ files) and put the extended tables in place
 * of the game's own.  Call after the game's data is installed and bound
 * (bw_data_install) and after the mod list is final, before the game runs;
 * again after a restart.  Returns 0, or -1 when a table could not be built
 * (the vanilla table then stands).
 */
int bw_limits_apply(void);

/* The manifest key a limit is declared with ("limit.items"), or NULL. */
const char *bw_limit_key(int id);

/*
 * The table the game reads now, for code outside the game that draws from
 * the same data (the one-screen interface's icons): the mods' copy when
 * one is in place, else the game's own.  NULL before the game's data is
 * bound.  The item icon rows (two u16 per item, ids 0..ITEM_LAST) and the
 * Pokemon icon palettes (one byte per icon index; *rows of them).
 */
const u16 *bw_item_icon_table(void);
const u8  *bw_icon_palette_table(u32 *rows);

#ifdef __cplusplus
}
#endif
/* The machine move table in force (128 rows with mods' TMs, else the
 * game's 101), the added TM an item is (its bit, TM96 = 101) or -1, and
 * the item of an added TM's bit or 0 (sdk/bw_limits.c). */
const u16 *bw_machine_moves(void);
int bw_machine_bit_of_item(u32 item);
u32 bw_machine_item_of_bit(u32 bit);

#endif /* BW_PORT_LIMITS_H */
