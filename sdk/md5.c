/*
 * sdk/md5.c -- MD5 as RFC 1321 gives it: four rounds of sixteen steps on
 * each 64-byte block, the message padded with one 1 bit, zeros and its
 * length in bits.  tests/test_md5.c checks it against the RFC's own test
 * values.
 */

#include <string.h>

#include "md5.h"

static u32 rd32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 rotl(u32 x, unsigned n)
{
    return (x << n) | (x >> (32u - n));
}

/* T[i] = floor(2^32 * abs(sin(i + 1))), the RFC's table */
static const u32 k_t[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

/* the shifts of each round's four steps */
static const unsigned k_s[4][4] = { { 7, 12, 17, 22 }, { 5, 9, 14, 20 }, { 4, 11, 16, 23 }, { 6, 10, 15, 21 } };

static void block(BwMd5 *m, const u8 *p)
{
    u32 x[16], a = m->state[0], b = m->state[1], c = m->state[2], d = m->state[3];
    unsigned i;

    for (i = 0; i < 16u; i++) {
        x[i] = rd32le(p + 4u * i);
    }
    for (i = 0; i < 64u; i++) {
        u32 f, t;
        unsigned g;

        switch (i / 16u) {
        case 0:  f = (b & c) | (~b & d); g = i; break;
        case 1:  f = (b & d) | (c & ~d); g = (5u * i + 1u) & 15u; break;
        case 2:  f = b ^ c ^ d; g = (3u * i + 5u) & 15u; break;
        default: f = c ^ (b | ~d); g = (7u * i) & 15u; break;
        }
        t = d;
        d = c;
        c = b;
        b = b + rotl(a + f + k_t[i] + x[g], k_s[i / 16u][i & 3u]);
        a = t;
    }
    m->state[0] += a;
    m->state[1] += b;
    m->state[2] += c;
    m->state[3] += d;
}

void bw_md5_init(BwMd5 *m)
{
    m->state[0] = 0x67452301u;
    m->state[1] = 0xefcdab89u;
    m->state[2] = 0x98badcfeu;
    m->state[3] = 0x10325476u;
    m->length = 0;
    m->used = 0;
}

void bw_md5_append(BwMd5 *m, const void *data, size_t size)
{
    const u8 *p = (const u8 *)data;

    m->length += (u64)size;
    while (size > 0) {
        size_t take = 64u - m->used < size ? 64u - m->used : size;

        memcpy(m->block + m->used, p, take);
        m->used += (u32)take;
        p += take;
        size -= take;
        if (m->used == 64u) {
            block(m, m->block);
            m->used = 0;
        }
    }
}

void bw_md5_final(BwMd5 *m, u8 digest[16])
{
    static const u8 one = 0x80u, zero = 0;
    u64 bits = m->length * 8u;
    u8 len[8];
    unsigned i;

    bw_md5_append(m, &one, 1);
    while (m->used != 56u) {
        bw_md5_append(m, &zero, 1);
    }
    for (i = 0; i < 8u; i++) {
        len[i] = (u8)(bits >> (8u * i));
    }
    bw_md5_append(m, len, 8);
    for (i = 0; i < 16u; i++) {
        digest[i] = (u8)(m->state[i / 4u] >> (8u * (i & 3u)));
    }
}

void bw_md5_hex(const u8 digest[16], char out[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned i;

    for (i = 0; i < 16u; i++) {
        out[2u * i] = hex[digest[i] >> 4];
        out[2u * i + 1u] = hex[digest[i] & 15u];
    }
    out[32] = '\0';
}
