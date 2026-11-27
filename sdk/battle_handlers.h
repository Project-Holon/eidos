/*
 * sdk/battle_handlers.h -- a mod's moves, abilities and items that work
 * like a built-in one, with numbers of their own (docs/battle-handlers.md).
 *
 * A mod's tables, eidos/tables/ability_handlers.txt, move_handlers.txt and
 * item_handlers.txt, one line an id:
 *
 *     <id> = <base id> [key=value ...] [+ <base id> [key=value ...] ...]
 *
 * A move's base may also be one of eidos's own, by name (BW_BH_NAMED_*).
 *
 * The battle runs the base id's handlers for the id (the game's handler
 * tables, hand_tokusei.c, hand_waza.c, hand_item.c), and the keys change
 * the numbers those handlers use: `165 = 22 stat=spatk stages=-2` is an
 * Intimidate that lowers Sp. Atk by 2.  `22 = 22 stages=-2` tunes the
 * built-in one.  With `+` the id works like up to four (docs/battle-handlers.md).
 *
 * The battle code reaches this file through the patches in stubs/ov/
 * (patch_ov093_hand_tokusei.c, patch_ov093_btl_event.c,
 * patch_ov093_btl_server_flow.c).
 */
#ifndef BW_PORT_BATTLE_HANDLERS_H
#define BW_PORT_BATTLE_HANDLERS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the game's event factor types (btl_event_types.h) */
#define BW_BH_MOVE    0
#define BW_BH_ABILITY 4
#define BW_BH_ITEM    5

/* The keys. */
enum {
    BW_BH_KEY_STAT,         /* stat of a stat change: 1 atk .. 7 evasion  */
    BW_BH_KEY_STAGES,       /* amount of a stat change: -6..6             */
    BW_BH_KEY_STATUS,       /* the status it adds (the game's sick id)    */
    BW_BH_KEY_CHANCE,       /* a percent roll, 0..100                     */
    BW_BH_KEY_TYPE,         /* a type it tests, 0 up to the type limit    */
    BW_BH_KEY_RATIO,        /* a multiplier, in 4096ths                   */
    BW_BH_KEY_HP,           /* an HP fraction's divisor, 1..16            */
    BW_BH_KEY_WEATHER,      /* the weather it starts                      */
    BW_BH_KEY_COUNT
};

/*
 * eidos's own move bases, for moves Black and White have no handlers for.
 * A line names them in place of a base id (`600 = misty_terrain`).  Their
 * ids are past the u16 range, so no move id is one; the handlers are in
 * stubs/ov/patch_ov093_hand_waza.c, which keeps the same numbers.
 */
#define BW_BH_NAMED_FIRST    0x10000
#define BW_BH_MISTY_TERRAIN  0x10000    /* 5 turns: no status for grounded Pokemon, Dragon moves half */
#define BW_BH_FLOWER_SHIELD  0x10001    /* +1 Defense for every Grass-type Pokemon on the field      */
#define BW_BH_CRAFTY_SHIELD  0x10002    /* the user's side is safe from status moves this turn       */
#define BW_BH_FAIRY_LOCK     0x10003    /* no Pokemon on the field can flee or switch next turn       */
#define BW_BH_GEOMANCY       0x10004    /* Razor Wind's charge with its own line; stats from the move */
#define BW_BH_NAMED_END      0x10005

/* The name of an eidos base ("misty_terrain"), NULL for any other id. */
const char *bw_bh_named_name(int base);

/*
 * The battle lines eidos's bases show (a/0/0/2): a mod's own line of the
 * same bank and index wins (`<mod>/a/0/0/2/14.msg`).  Bank 15 lines are
 * one line each; bank 14 lines are sets of three (the player's Pokemon, a
 * wild one, a foe's), as the game's are.
 */
#define BW_BH_LINE_BANK_STD      15u
#define BW_BH_LINE_BANK_SET      14u
#define BW_BH_LINE_MISTY_START   400u   /* bank 15 */
#define BW_BH_LINE_MISTY_END     401u   /* bank 15 */
#define BW_BH_LINE_FAIRY_LOCK    402u   /* bank 15 */
#define BW_BH_LINE_GEOMANCY      2000u  /* bank 14, set of three */
#define BW_BH_LINE_MISTY_BLOCK   2003u  /* bank 14, set of three */
#define BW_BH_LINE_CRAFTY_START  2006u  /* bank 14, set of three */
#define BW_BH_LINE_CRAFTY_BLOCK  2009u  /* bank 14, set of three */

/* The kinds of place a handler uses a number (tools/handler_params.py
 * writes battle_handler_sites.inc with them). */
enum {
    BW_BH_SITE_RANK,        /* a stat change record: stat, stages        */
    BW_BH_SITE_SICK,        /* a status record: status                   */
    BW_BH_SITE_WEATHER,     /* a weather record: weather                 */
    BW_BH_SITE_CHANCE,      /* BTL_CALC_RollPercent: chance              */
    BW_BH_SITE_HP,          /* BTL_CALC_DivideMaxHp(Min1): hp            */
    BW_BH_SITE_RATIO,       /* BTL_EVENT_MultiplyVar: ratio              */
    BW_BH_SITE_TYPE,        /* a helper given the type it tests: type    */
    BW_BH_SITE_COUNT
};

/* Read every enabled mod's tables again (at start-up, and when the mod
 * list changes).  A line that does not read, a chain of bases that comes
 * back, or a key the base's handlers have no place for is said on stderr
 * and left out. */
void bw_bh_load(void);
/* The same checks on the lines bw_bh_parse_line read (for the tests). */
void bw_bh_check(void);

/* The id whose handlers run for `id` of `type`: its base, or itself (for
 * a line of several bases, the first). */
int  bw_bh_base(int type, int id);

/*
 * A line of several bases (`165 = 22 stat=spatk + 26`): how many (1 for
 * an id with no line or one base), the built-in id of base `part` (0 for
 * a part whose base does not resolve), and its keys.
 */
int  bw_bh_parts(int type, int id);
int  bw_bh_part_base(int type, int id, int part);
int  bw_bh_part_key(int type, int id, int part, int key, int index, int *value);

/*
 * The handler tables of the bases of `id`, joined in line order into one
 * table the factor runs (rows of `row_size` bytes; tables[k] has counts[k]
 * rows, 0 for a base with no handlers).  Made once and kept for the run;
 * the running frames know which base each row came from.  NULL when `id`
 * has no line of `n` bases.
 */
const void *bw_bh_join(int type, int id, int n, const void *const *tables, const u32 *counts, u32 row_size,
                       u32 *total);

/* The game's dispatch runs the first row of a factor that answers an event,
 * and no more.  In a joined table that is each base's first row: after row
 * `row` ran, the dispatch goes on from the row this gives (where the base
 * holding `row` ends).  0 for a factor that is not a joined table: the
 * dispatch stops, as the game's does. */
int  bw_bh_row_end(int type, int id, int row);

/* The battle runs the `handler`-th handler of the factor (type, id), and is
 * done with it.  They nest: a handler's command can run other handlers. */
void bw_bh_enter(int type, int id, int handler);
void bw_bh_leave(void);

/*
 * The places, from the patches: each changes the number in place, or gives
 * the number to use, by the running id's keys.  Only the first place of a
 * kind in one handler call is the handler's own; the generator lists only
 * places that run once, before any command whose engine work could reach
 * another.
 */
void bw_bh_rank(u32 *stat, s8 *amount);     /* a stat change record              */
int  bw_bh_sick(u32 *kind);                 /* a status record: 1 when changed    */
void bw_bh_weather(u8 *weather);            /* a weather record                   */
u32  bw_bh_chance(u32 percent);             /* a percent roll                     */
u32  bw_bh_hp(u32 divisor);                 /* an HP fraction's divisor           */
s32  bw_bh_ratio(s32 ratio);                /* a multiplier, in 4096ths           */
int  bw_bh_type(int type);                  /* a type a helper tests              */

/* Parse one table line (for the tests): 0 read, 1 nothing on the line,
 * -1 refused with `why`. */
int  bw_bh_parse_line(int type, const char *line, char *why, int why_size);
/* Forget every line (for the tests). */
void bw_bh_clear(void);
/* The key value set for `id` (0 none): for the tests and the reference. */
int  bw_bh_key(int type, int id, int key, int index, int *value);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_BATTLE_HANDLERS_H */
