/*
 * sdk/langmap.h -- playing another cartridge than English Black.
 *
 * The port is built once, with English Black's layout.  Pokemon White and
 * the French, German, Italian and Spanish cartridges hold the same program
 * with its code and data at other addresses.  When the player's game data
 * comes from one of them, its ARM9 image and overlay files are laid out as
 * English Black's when they are read (aletheia's address maps,
 * tools/langmap_c.py), and the game is told its language (g_pm_language,
 * src/game/version.h), so everything else stays as for English Black.
 */

#ifndef BW_PORT_LANGMAP_H
#define BW_PORT_LANGMAP_H

#include "types.h"

/* Reads the cartridge's game code from the game data (extract/header.bin)
 * and sets the language and the map.  English Black, or no header: the
 * English layout as built, no map. */
void bw_lang_init(void);

/* The cartridge's game code ("IRBO" when unknown). */
const char *bw_lang_gamecode(void);

/* Non-zero when the images are laid out from another cartridge. */
int bw_lang_active(void);

/* The ARM9 image of the cartridge laid out as English Black's: a new
 * buffer (the caller frees it) of *size bytes, or NULL with no map. */
u8 *bw_lang_relayout_arm9(const u8 *image, u32 image_size, u32 *size);

/* An overlay's file laid out as English Black's: `eng_base` and
 * `eng_size` its English place, the file as the cartridge has it. */
u8 *bw_lang_relayout_overlay(u32 id, u32 eng_base, u32 eng_size, const u8 *file,
                             u32 file_size, u32 *size);

/* Whether `word`, read from the cartridge's data, is the address the
 * English image holds there (`english`): the same object in the other
 * layout. */
int bw_lang_same_address(u32 english, u32 word);

/* The English Black address where the other cartridge's address `word`
 * was laid out: looked for first in `region` (-1 the ARM9, else that
 * overlay), then in the ARM9.  0 when no row holds it. */
int bw_lang_to_english(u32 word, int region, u32 *out);

/* The host address of the cartridge's own byte at its address `word`, in
 * the copy of its image kept as it was (the ARM9's, or the overlay
 * `region`'s), or NULL: for a word into data whose length is the
 * cartridge's own (a word list in its language), which has no English
 * place. */
void *bw_lang_target_ptr(u32 word, int region);

#endif
