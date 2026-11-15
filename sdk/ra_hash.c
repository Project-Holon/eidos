/*
 * sdk/ra_hash.c -- the RetroAchievements hash from the extracted data root
 * (sdk/ra_hash.h).  The algorithm is rcheevos's rc_hash_nintendo_ds, and
 * tests/test_ra.c checks the two give the same answer on a synthetic ROM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_hash.h"
#include "rhash/md5.h"

#define HASH_HEADER   0x160u
#define HASH_BANNER   0xA00u
#define SANE_CODE_MAX (16u * 1024u * 1024u)    /* rcheevos's own limit */

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

int bw_ra_hash_parts(const unsigned char *header, size_t header_len,
                     const unsigned char *arm9, size_t arm9_len,
                     const unsigned char *arm7, size_t arm7_len,
                     const unsigned char *banner, size_t banner_len,
                     char out[33])
{
    md5_state_t md5;
    md5_byte_t digest[16];
    unsigned char pad[HASH_BANNER];
    unsigned arm9_size, arm7_size;
    size_t n;
    int i;

    if (header == NULL || header_len < HASH_HEADER) {
        return -1;
    }
    arm9_size = rd32(header + 0x2C);
    arm7_size = rd32(header + 0x3C);
    if ((unsigned long long)arm9_size + arm7_size > SANE_CODE_MAX ||
        arm9_len < arm9_size || arm7_len < arm7_size) {
        return -1;
    }
    md5_init(&md5);
    md5_append(&md5, header, HASH_HEADER);
    if (arm9_size != 0) {
        md5_append(&md5, arm9, arm9_size);
    }
    if (arm7_size != 0) {
        md5_append(&md5, arm7, arm7_size);
    }
    /* A short banner is padded with zeros, as rcheevos does. */
    memset(pad, 0, sizeof(pad));
    n = banner_len < HASH_BANNER ? banner_len : HASH_BANNER;
    if (n != 0 && banner != NULL) {
        memcpy(pad, banner, n);
    }
    md5_append(&md5, pad, HASH_BANNER);
    md5_finish(&md5, digest);
    for (i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    return 0;
}

/* The whole of a file, or NULL.  `*len` is its size. */
static unsigned char *slurp(const char *root, const char *name, size_t *len)
{
    char path[1024];
    FILE *f;
    unsigned char *buf;
    long size;

    snprintf(path, sizeof(path), "%s/%s", root, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || (unsigned long)size > SANE_CODE_MAX) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char *)malloc(size > 0 ? (size_t)size : 1u);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}

int bw_ra_hash_from_root(const char *data_root, char out[33],
                         char *err, size_t err_len)
{
    static const char *const names[4] = { "header.bin", "arm9.bin", "arm7.bin", "banner.bin" };
    unsigned char *part[4] = { NULL, NULL, NULL, NULL };
    size_t len[4] = { 0, 0, 0, 0 };
    int rc = -1;
    int i;

    /* The extractor writes the four files beside its data/ folder, and
     * the data root is that folder: they are one level up.  A root that
     * holds them itself (a test) is tried first. */
    for (i = 0; i < 4; i++) {
        char up[1100];

        part[i] = slurp(data_root, names[i], &len[i]);
        if (part[i] == NULL) {
            snprintf(up, sizeof(up), "%s/..", data_root);
            part[i] = slurp(up, names[i], &len[i]);
        }
        if (part[i] == NULL) {
            if (err != NULL && err_len > 0) {
                snprintf(err, err_len, "%s is missing from the data folder", names[i]);
            }
            goto done;
        }
    }
    rc = bw_ra_hash_parts(part[0], len[0], part[1], len[1], part[2], len[2],
                          part[3], len[3], out);
    if (rc != 0 && err != NULL && err_len > 0) {
        snprintf(err, err_len, "the game's files are shorter than its header says");
    }
done:
    for (i = 0; i < 4; i++) {
        free(part[i]);
    }
    return rc;
}
