/*
 * sdk/md5.h -- MD5 (RFC 1321), for the RetroAchievements hash of the game
 * in force (sdk/mod_v7.c, game_hash).  Not for anything that needs to be
 * secure: MD5 is broken for that, and the hash is only what that service
 * names a game by.
 */

#ifndef BW_PORT_MD5_H
#define BW_PORT_MD5_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BwMd5 {
    u32 state[4];
    u64 length;             /* bytes appended so far */
    u8  block[64];
    u32 used;               /* bytes of `block` held */
} BwMd5;

void bw_md5_init(BwMd5 *m);
void bw_md5_append(BwMd5 *m, const void *data, size_t size);
void bw_md5_final(BwMd5 *m, u8 digest[16]);
/* The digest as 32 lower-case hex characters and a 0. */
void bw_md5_hex(const u8 digest[16], char out[33]);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MD5_H */
