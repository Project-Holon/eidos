/*
 * sdk/battle_handlers.h -- a mod's moves, abilities and items that work
 * like a built-in one, with numbers of their own (docs/battle-handlers.md).
 *
 * A mod's tables, eidos/tables/ability_handlers.txt, move_handlers.txt and
 * item_handlers.txt, one line an id:
 *
 *     <id> = <base id> [key=value ...]
 *
 * The battle runs the base id's handlers for the id (the game's handler
 * tables, hand_tokusei.c, hand_waza.c, hand_item.c), and the keys change
 * the numbers those handlers use: `165 = 22 stat=spatk stages=-2` is an
 * Intimidate that lowers Sp. Atk by 2.  `22 = 22 stages=-2` tunes the
 * built-in one.
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
    BW_BH_KEY_TYPE,         /* a type it tests, 0..16                     */
    BW_BH_KEY_RATIO,        /* a multiplier, in 4096ths                   */
    BW_BH_KEY_HP,           /* an HP fraction's divisor, 1..16            */
    BW_BH_KEY_WEATHER,      /* the weather it starts                      */
    BW_BH_KEY_COUNT
};

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

/* The id whose handlers run for `id` of `type`: its base, or itself. */
int  bw_bh_base(int type, int id);

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
