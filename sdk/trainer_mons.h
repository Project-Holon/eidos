/*
 * sdk/trainer_mons.h -- per-member numbers for a trainer's Pokemon, from a
 * mod's table (docs/trainer-mons.md).
 *
 * The game's trainer data holds one strength byte per member (all six IVs
 * at once) and no EVs, nature, shininess or friendship of its own.  A mod's
 * eidos/tables/trainer_mons.txt sets them per member:
 *
 *     <trainer id>.<slot> = [ivs=h,a,d,sa,sd,s] [evs=h,a,d,sa,sd,s]
 *                           [nature=adamant] [shiny=0|1] [friendship=0..255]
 *                           [ability=1|2|hidden]
 *
 * The slot is the member's place in the party, 0 to 5.  Every key is
 * optional; a member with no line is the game's own.  A later mod's line
 * for the same member replaces an earlier one.
 *
 * The numbers apply once the battle's setup holds the trainer's party
 * (sdk/mod_event_battle.c, before the battle.trainer_mon event).
 */
#ifndef BW_PORT_TRAINER_MONS_H
#define BW_PORT_TRAINER_MONS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PokemonParam;

/* Read every enabled mod's table again (at start-up, and when the mod list
 * changes).  A line that does not read is said on stderr and left out. */
void bw_trmons_load(void);

/* Apply the line for member `slot` of trainer `trainer`, if there is one;
 * 1 when one applied. */
int  bw_trmons_apply(u32 trainer, u32 slot, struct PokemonParam *mon);

/* For the tests: parse one line (0 read, 1 nothing on the line, -1 refused
 * with `why`), forget every line, and read a line's values back. */
int  bw_trmons_parse_line(const char *line, char *why, int why_size);
void bw_trmons_clear(void);

typedef struct BwTrMon {
    u16 trainer;
    u8  slot;
    u8  has;            /* BW_TRMON_HAS_* bits */
    u8  ivs[6];         /* HP, Atk, Def, Sp. Atk, Sp. Def, Speed */
    u8  evs[6];         /* the same order */
    u8  nature;         /* 0 Hardy .. 24 Quirky */
    u8  shiny;
    u8  friendship;
    u8  ability;        /* 1, 2, or 3 for the hidden ability */
} BwTrMon;

enum {
    BW_TRMON_HAS_IVS = 1 << 0,
    BW_TRMON_HAS_EVS = 1 << 1,
    BW_TRMON_HAS_NATURE = 1 << 2,
    BW_TRMON_HAS_SHINY = 1 << 3,
    BW_TRMON_HAS_FRIENDSHIP = 1 << 4,
    BW_TRMON_HAS_ABILITY = 1 << 5
};

const BwTrMon *bw_trmons_find(u32 trainer, u32 slot);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_TRAINER_MONS_H */
