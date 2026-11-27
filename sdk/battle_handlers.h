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
    BW_BH_KEY_TURNS,        /* how many turns an effect lasts, 1..63      */
    BW_BH_KEY_DAMAGE,       /* a fixed damage, 1..999                     */
    BW_BH_KEY_POWER,        /* a power it sets, 1..255                    */
    BW_BH_KEY_ACCURACY,     /* an accuracy it sets, 1..100                */
    BW_BH_KEY_KIND,         /* a kind of move (eidos's ability bases)     */
    BW_BH_KEY_PRIORITY,     /* priority it adds, -6..6 (ability bases)    */
    BW_BH_KEY_FORM,         /* a form it takes, 0..31 (ability bases)     */
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
#define BW_BH_FELL_STINGER   0x10005    /* +3 Attack for the user when the move knocks a target out    */
#define BW_BH_NEEDS_STATUS   0x10006    /* the move fails on a target without the status (Venom Drench) */
#define BW_BH_SPIKY_SHIELD   0x10007    /* Protect; a contact move it stops costs the attacker 1/8 HP  */
#define BW_BH_KINGS_SHIELD   0x10008    /* Protect from damaging moves; contact lowers Attack by 2     */
#define BW_BH_BANEFUL_BUNKER 0x10009    /* Protect; contact poisons the attacker                       */
#define BW_BH_CONTACT_GUARD  0x1000A    /* Protect with what contact does from keys (hp, stat, status) */
#define BW_BH_SUPER_EFFECTIVE 0x1000B   /* the move is super effective against a type (key `type`)     */
#define BW_BH_FREEZE_DRY     0x1000C    /* the same, against Water                                     */
#define BW_BH_SECOND_TYPE    0x1000D    /* the move's effectiveness takes a second type (key `type`)   */
#define BW_BH_FLYING_PRESS   0x1000E    /* the same, Flying                                            */
#define BW_BH_ADD_TYPE       0x1000F    /* the target gets a third type (key `type`)                   */
#define BW_BH_TRICK_OR_TREAT 0x10010    /* the same, Ghost                                             */
#define BW_BH_FORESTS_CURSE  0x10011    /* the same, Grass                                             */
#define BW_BH_GRASSY_TERRAIN 0x10012    /* 5 turns: grounded Pokemon heal 1/16, Grass moves stronger   */
#define BW_BH_ELECTRIC_TERRAIN 0x10013  /* 5 turns: grounded Pokemon cannot sleep, Electric stronger   */
#define BW_BH_PSYCHIC_TERRAIN 0x10014   /* 5 turns: grounded Pokemon safe from priority, Psychic stronger */
#define BW_BH_HITS_FLOATING  0x10015    /* a Ground move hits Flying and floating Pokemon (Thousand Arrows) */
#define BW_BH_PARTING_SHOT   0x10016    /* two stats of the target fall, then the user switches out    */
#define BW_BH_TOPSY_TURVY    0x10017    /* the target's stat stages turn the other way                 */
#define BW_BH_NEEDS_BERRY    0x10018    /* the move fails unless the user ate a berry (Belch)          */
#define BW_BH_BELCH          0x10019    /* the same                                                    */
#define BW_BH_BURN_UP        0x1001A    /* fails unless the user is Fire; then it loses the type       */
#define BW_BH_SHORE_UP       0x1001B    /* heals half the HP, two thirds in a sandstorm                */
#define BW_BH_STRENGTH_SAP   0x1001C    /* heals by the target's Attack, then lowers it                */
#define BW_BH_ROTOTILLER     0x1001D    /* grounded Grass Pokemon: Attack and Sp. Atk +1               */
#define BW_BH_MAGNETIC_FLUX  0x1001E    /* the user's side with Plus or Minus: Defense and Sp. Def +1  */
#define BW_BH_SET_TYPE       0x1001F    /* the target becomes one type (Soak with a key `type`)        */
#define BW_BH_ELECTRIFY      0x10020    /* the target's moves this turn are Electric                   */
#define BW_BH_ION_DELUGE     0x10021    /* Normal moves this turn are Electric                         */
#define BW_BH_STICKY_WEB     0x10022    /* the foes' side: a grounded Pokemon coming in loses Speed     */
#define BW_BH_POWDER         0x10023    /* the target's Fire move this turn explodes on it              */
#define BW_BH_THROAT_CHOP    0x10024    /* the target cannot use sound moves for two turns              */
#define BW_BH_LASER_FOCUS    0x10025    /* the user's moves are critical hits this turn and the next    */
#define BW_BH_STOMPING_TANTRUM 0x10026  /* twice the power when the user's last move failed            */
#define BW_BH_SPECTRAL_THIEF 0x10027    /* takes the target's raised stat stages before the damage      */
#define BW_BH_POLLEN_PUFF    0x10028    /* aimed at an ally, heals half its HP in place of the damage   */
#define BW_BH_NAMED_END      0x10029

/* The name of an eidos base ("misty_terrain"), NULL for any other id. */
const char *bw_bh_named_name(int base);

/*
 * eidos's own ability bases, for later abilities and for building blocks a
 * modder sets with keys.  An ability line names them in place of a base id
 * (`165 = pixilate`, `166 = boost_kind kind=bite ratio=1.5`).  The handlers
 * are in stubs/ov/patch_ov093_hand_tokusei.c, which keeps the same numbers,
 * and read their keys with bw_bh_arg.
 */
#define BW_BH_ABILITY_NAMED_FIRST 0x10100
#define BW_BH_PIXILATE       0x10100    /* Normal moves take a type (fairy), 1.2 times the power       */
#define BW_BH_AERILATE       0x10101    /* the same, Flying                                           */
#define BW_BH_REFRIGERATE    0x10102    /* the same, Ice                                              */
#define BW_BH_GALVANIZE      0x10103    /* the same, Electric                                         */
#define BW_BH_BOOST_KIND     0x10104    /* moves of a kind have more power (contact, 1.3)             */
#define BW_BH_TOUGH_CLAWS    0x10105    /* contact moves, 1.3                                         */
#define BW_BH_STRONG_JAW     0x10106    /* bite moves, 1.5                                            */
#define BW_BH_MEGA_LAUNCHER  0x10107    /* pulse moves, 1.5                                           */
#define BW_BH_BOOST_TYPE     0x10108    /* moves of a type have more power (Steel, 1.5)               */
#define BW_BH_GUARD_KIND     0x10109    /* moves of a kind do less damage to it (contact, 0.5)        */
#define BW_BH_FUR_COAT       0x1010A    /* Defense times 2 against physical moves                     */
#define BW_BH_BLOCK_KIND     0x1010B    /* moves of a kind have no effect on it (ball and bomb)        */
#define BW_BH_BULLETPROOF    0x1010C    /* ball and bomb moves have no effect on it                   */
#define BW_BH_PRIORITY_TYPE  0x1010D    /* moves of a type go first (Flying, +1)                      */
#define BW_BH_GALE_WINGS     0x1010E    /* Flying moves +1 priority at full HP                        */
#define BW_BH_ENTRY_STAT     0x1010F    /* its own stat changes when it enters (Attack +1)            */
#define BW_BH_KO_STAT        0x10110    /* its own stat changes for each Pokemon it knocks out        */
#define BW_BH_HIT_TYPE_STAT  0x10111    /* its own stat changes when a move of a type hits it         */
#define BW_BH_SWEET_VEIL     0x10112    /* its side cannot fall asleep                                */
#define BW_BH_AROMA_VEIL     0x10113    /* its side is safe from Taunt, Torment, Encore, Disable, ... */
#define BW_BH_FLOWER_VEIL    0x10114    /* Grass Pokemon of its side keep their stats and status      */
#define BW_BH_CHEEK_POUCH    0x10115    /* eating a berry also heals a third of its HP                */
#define BW_BH_PROTEAN        0x10116    /* it takes the type of the move it uses                      */
#define BW_BH_MAGICIAN       0x10117    /* it takes the item of the Pokemon its move hits             */
#define BW_BH_SYMBIOSIS      0x10118    /* it passes its item to an ally that used up its own         */
#define BW_BH_PARENTAL_BOND  0x10119    /* its single-hit moves hit twice, the second at 1/4 power   */
#define BW_BH_STANCE_CHANGE  0x1011A    /* attack form for damaging moves, guard form for King's Shield */
#define BW_BH_GRASS_PELT     0x1011B    /* Defense times 1.5 while Grassy Terrain is up              */
#define BW_BH_ABILITY_NAMED_END 0x1011C

/* The name of an eidos ability base ("pixilate"), NULL for any other id. */
const char *bw_bh_ability_named_name(int base);

/* The running handler's value for `key` ("ratio", "type", "kind"; index 2
 * for `type.2`) from its id's line, or `fallback`: eidos's own ability
 * bases read their numbers so. */
int  bw_bh_arg(const char *key, int index, int fallback);

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
#define BW_BH_LINE_GRASSY_START  403u   /* bank 15 */
#define BW_BH_LINE_GRASSY_END    404u   /* bank 15 */
#define BW_BH_LINE_ELECTRIC_START 405u  /* bank 15 */
#define BW_BH_LINE_ELECTRIC_END  406u   /* bank 15 */
#define BW_BH_LINE_PSYCHIC_START 407u   /* bank 15 */
#define BW_BH_LINE_PSYCHIC_END   408u   /* bank 15 */
#define BW_BH_LINE_ELECTRIC_BLOCK 2012u /* bank 14, set of three */
#define BW_BH_LINE_PSYCHIC_BLOCK 2015u  /* bank 14, set of three */
#define BW_BH_LINE_TOPSY_TURVY   2018u  /* bank 14, set of three */
#define BW_BH_LINE_BURN_UP       2021u  /* bank 14, set of three */
#define BW_BH_LINE_ION_DELUGE    409u   /* bank 15 */
#define BW_BH_LINE_ELECTRIFY     2024u  /* bank 14, set of three */
#define BW_BH_LINE_STICKY_WEB_US 410u   /* bank 15 */
#define BW_BH_LINE_STICKY_WEB_FOE 411u  /* bank 15 */
#define BW_BH_LINE_STICKY_WEB_CAUGHT 2027u /* bank 14, set of three */
#define BW_BH_LINE_STICKY_WEB_GONE_US 412u /* bank 15: Defog or Rapid Spin took it away */
#define BW_BH_LINE_STICKY_WEB_GONE_FOE 413u /* bank 15 */
#define BW_BH_LINE_ADD_TYPE      2030u  /* bank 14, set of three: the type added (argument 1) */
#define BW_BH_LINE_POWDER        2033u  /* bank 14, set of three */
#define BW_BH_LINE_POWDER_BLAST  414u   /* bank 15 */
#define BW_BH_LINE_THROAT_CHOP   2036u  /* bank 14, set of three: the move it cannot use (argument 1) */
#define BW_BH_LINE_LASER_FOCUS   2039u  /* bank 14, set of three */
#define BW_BH_LINE_SPECTRAL_THIEF 2042u /* bank 14, set of three */
#define BW_BH_LINE_BELCH         2045u  /* bank 14, set of three: why Belch cannot be chosen */

/* Whether the game's language has battle line `line` of `bank` (eidos's
 * lines are left out of a language with no game line of their shape):
 * a base with no line in the language shows the game's own lines only. */
int  bw_bh_line_ok(u32 bank, u32 line);

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
    BW_BH_SITE_TURNS,       /* a duration word (BTL_SICKCONT_MakeKind2*,
                               BTL_CALC_MakeSickContTurns): turns         */
    BW_BH_SITE_DAMAGE,      /* the fixed damage var set: damage          */
    BW_BH_SITE_POWER,       /* the power var set: power                  */
    BW_BH_SITE_ACCURACY,    /* the accuracy var set: accuracy            */
    BW_BH_SITE_KIND,        /* eidos's ability bases only: kind          */
    BW_BH_SITE_PRIORITY,    /* eidos's ability bases only: priority      */
    BW_BH_SITE_FORM,        /* eidos's ability bases only: form          */
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
 * The game's checks of a move by its id outside the move's own handlers
 * (Protect's streak, Metronome's and Encore's lists, Pledge partners, a
 * bound Pokemon's message): whether `move` works like the built-in move
 * `builtin`, that is, a base of its line resolves to it.  A move with no
 * line is its own base, so with no table lines this is `move == builtin`.
 * A move whose line names another base (`182 = 33`) is not its old self.
 */
int  bw_bh_move_is(int move, int builtin);
/* The same against a list of `count` built-in moves (the game's u16 lists). */
int  bw_bh_move_in(int move, const u16 *list, int count);
/* Whether a part of `move`'s line is the eidos base `named`; then *value
 * takes that part's `key` (index `index`) when the line sets it, and is
 * left as it is when not.  `key` -1 asks only whether. */
int  bw_bh_move_named(int move, int named, int key, int index, int *value);

/*
 * The same for abilities: the game's checks of an ability by its id
 * outside the ability's own handlers (Mold Breaker's list, the Role Play,
 * Skill Swap and Trace refusals, trapping, Klutz, Illusion, the field lead
 * abilities, the trainer AI).  A line of several bases works like each.
 */
int  bw_bh_ability_is(int ability, int builtin);
int  bw_bh_ability_in(int ability, const u16 *list, int count);

/* EIDOS_DEBUG_LAUNCHER=ITEM (a test switch): the item every Bag action
 * uses, with the Wonder Launcher's rules; 0 when the switch is off. */
int  bw_bh_test_launcher_item(void);

/* The built-in id of the base whose row runs now for the factor (type, id):
 * a handler that tells abilities apart by its factor's key (Mold Breaker's
 * entry line for Turboblaze and Teravolt) asks this.  bw_bh_base outside a
 * handler of that factor. */
int  bw_bh_running_base(int type, int id);

/* A skip check (Mold Breaker's) asks about a factor before its row runs:
 * BTL_EVENT_DispatchToFactors says which row it is about to run, and the
 * check asks the base of that row's part (a line of several bases: Mold
 * Breaker skips its Levitate part and keeps its Rough Skin part). */
void bw_bh_checking(int type, int id, int row);
int  bw_bh_checked_base(int type, int id);

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
u32  bw_bh_turns(u32 turns);                /* a duration's turns                 */
/* A value a handler sets in an event variable (BTL_EVENT_OverwriteVar):
 * the fixed damage (0x37), the power (0x30), the accuracy (0x2B), an added
 * status's or effect's chance (0x26), a weather's turns (0x24); any other
 * variable keeps it. */
int  bw_bh_set_var(int var, int value);
/* The chance of a move's added stat change the server rolls (the move
 * record's): the move line's `chance` key, else `percent`. */
int  bw_bh_move_chance(int move, int percent);

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
