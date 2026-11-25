/*
 * sdk/mod_v7.h -- the port side of the state calls mod API version 7 added
 * (docs/mod-services.md, "Version 7: the National Pokedex and Wonder
 * Cards").
 */

#ifndef BW_PORT_MOD_V7_H
#define BW_PORT_MOD_V7_H

#include "types.h"
#include "mod_api.h"       /* (BW_GAME_HASH_*) */

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

/* The services table's clock: the seconds since 1970 (UTC) and a
 * millisecond count that only goes up (not a date). */
s64 bw_mod_v7_time_utc(void);
u64 bw_mod_v7_ticks_ms(void);

/* The game in force's hash of `kind` into `out` (32 lower-case hex
 * characters and a 0, so `size` at least 33): 32, or -1 when the kind is
 * unknown or the game's files do not read.  The RetroAchievements hash is
 * rcheevos' rc_hash_nintendo_ds over the files the extraction keeps
 * (header.bin, arm9.bin, arm7.bin, banner.bin), the same bytes it reads in
 * the ROM. */
int bw_mod_v7_game_hash(u32 kind, char *out, u32 size);
/* The same from one extraction folder: 0, or -1. */
int bw_mod_v7_ra_hash_dir(const char *extract_dir, char out[33]);

/* The services table's hardcore_allowed: 1 when nothing in force changes
 * the game but the calling mod, else 0 and the reason in `why`. */
int bw_mod_v7_hardcore_allowed(char *why, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_V7_H */
