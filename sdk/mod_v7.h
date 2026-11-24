/*
 * sdk/mod_v7.h -- the port side of the state calls mod API version 7 added
 * (docs/mod-services.md, "Version 7: the National Pokedex and Wonder
 * Cards").
 */

#ifndef BW_PORT_MOD_V7_H
#define BW_PORT_MOD_V7_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Wonder Card: the 204 bytes of a .pgf file, and where the port reads
 * it. */
#define BW_MYSTERY_CARD_SIZE    0xCCu
#define BW_MYSTERY_CARD_DATE    0xACu   /* u32: year << 16 | month << 8 | day */
#define BW_MYSTERY_CARD_ID      0xB0u   /* u16: the card's id, its "received" flag's number */
#define BW_MYSTERY_CARD_TYPE    0xB3u   /* u8: 1 to 3 for a card */
#define BW_MYSTERY_CARD_FLAGS   0xB4u   /* u8: bit 1, the gift was handed over */
/* The save keeps one "received" flag per card id below this. */
#define BW_MYSTERY_CARD_ID_MAX  0x800u

/* What mystery_card_add answers when it adds nothing. */
#define BW_MYSTERY_ADD_INVALID  (-1)
#define BW_MYSTERY_ADD_KNOWN    (-2)
#define BW_MYSTERY_ADD_FULL     (-3)

s32 bw_mod_v7_national_dex(void);
int bw_mod_v7_national_dex_set(void);
s32 bw_mod_v7_mystery_card_add(const void *pgf, u32 size);

/* Whether `size` bytes at `pgf` have a card's shape (the size, the gift
 * type, the id's range): 1 or 0.  The rule language checks a `gift`
 * action's file with it when the rule loads. */
int bw_mod_v7_mystery_card_ok(const void *pgf, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_V7_H */
