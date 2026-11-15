/*
 * sdk/ra_hash.h -- the RetroAchievements hash of the player's game, worked
 * out from the extracted data root (docs/retroachievements.md, "The hash").
 *
 * RetroAchievements identifies a Nintendo DS game by the MD5 of four parts
 * of the cartridge: the first 0x160 bytes of the header, the ARM9 binary,
 * the ARM7 binary and 0xA00 bytes of the banner (icon and titles) --
 * rcheevos's rc_hash_nintendo_ds.  The port keeps no copy of the ROM, but
 * the first-run extractor wrote exactly those parts (header.bin, arm9.bin,
 * arm7.bin, banner.bin), so the hash is the same one the ROM gives.
 */

#ifndef BW_PORT_RA_HASH_H
#define BW_PORT_RA_HASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The parts as bytes, for a caller that has them in memory (the test, and
 * ra_hash_from_root below).  `out` takes 32 hex digits and a NUL.  Returns
 * 0, or -1 when a part is too short for what the header says.
 */
int bw_ra_hash_parts(const unsigned char *header, size_t header_len,
                     const unsigned char *arm9, size_t arm9_len,
                     const unsigned char *arm7, size_t arm7_len,
                     const unsigned char *banner, size_t banner_len,
                     char out[33]);

/* The same from <data_root>/header.bin, arm9.bin, arm7.bin and banner.bin.
 * Returns 0, or -1 with a sentence in `err`. */
int bw_ra_hash_from_root(const char *data_root, char out[33],
                         char *err, size_t err_len);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_RA_HASH_H */
