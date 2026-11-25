/*
 * sdk/sha256.h -- SHA-256 (FIPS 180-4), for the check of a downloaded mod
 * package against the hash its mod.json states (sdk/mod_catalog.c).
 *
 * The game's own, so the check does not depend on a build having TLS.
 */

#ifndef BW_PORT_SHA256_H
#define BW_PORT_SHA256_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BwSha256 {
    u32 h[8];
    u8  block[64];
    u32 held;                           /* bytes waiting in block */
    unsigned long long size;            /* bytes taken so far (the end's own padding too, after bw_sha256_end) */
} BwSha256;

void bw_sha256_start(BwSha256 *s);
void bw_sha256_add(BwSha256 *s, const void *data, size_t size);
void bw_sha256_end(BwSha256 *s, u8 out[32]);

/* The hash as 64 lower-case hexadecimal digits and a NUL. */
void bw_sha256_hex(const u8 hash[32], char out[65]);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SHA256_H */
