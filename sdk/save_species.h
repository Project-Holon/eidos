/*
 * sdk/save_species.h -- the species a save holds, read from its image.
 */
#ifndef BW_SAVE_SPECIES_H
#define BW_SAVE_SPECIES_H

#include <stddef.h>

#include "types.h"

/* Calls fn with the species of each Pokemon in the save image's newer
 * copy: the boxes, the party, the Battle Box and the Day Care (a Bad Egg
 * is skipped).  The number of Pokemon, or -1 when the image holds no save
 * copy. */
int bw_save_species_each(const u8 *image, size_t size, void (*fn)(void *ctx, u32 species), void *ctx);

#endif
