/*
 * sdk/dex_data.h -- the Pokedex's data for species a mod adds (sdk/dex_data.c).
 */
#ifndef BW_DEX_DATA_H
#define BW_DEX_DATA_H

#include "types.h"

/* A species' Unova number (Victini 0), or -1 when it is not in the Unova
 * Pokedex: the game's table with the mods' eidos/tables/regional_dex.txt. */
int bw_dex_regional(u32 species);

/* 1 when the id is a species: 1..649, or a species a mod adds (715 up to
 * the species limit, with a stats record). */
int bw_dex_species_exists(u32 species);

/* The dex record of a species (its id) or of a mod's form (its stats
 * slot): the id below 715, 727 + (id - 715) from there. */
u32 bw_dex_record(u32 id);

/* A mod species' forms past its first (formCount - 1), the first one's
 * stats slot in *firstSlot; 0 when none are a mod's. */
u32 bw_dex_mod_forms(u32 species, u32 *firstSlot);

/* The form chain (a/0/9/8 member 0) with the added species' chains, on the
 * game's heap `heap` (GFL_HEAP_Free); *rows its length. */
u16 *bw_dex_links(u32 heap, u32 *rows);

/* A mod's body shape (0..13) of a species or form, and the letter group of
 * a species' name, from eidos/tables/pokedex.txt; -1 when not given. */
int bw_dex_shape(u32 species, u32 form);
int bw_dex_letter(u32 species);

/* The mod list or the limit changed: worked out again when next asked. */
void bw_dex_data_forget(void);

#endif
