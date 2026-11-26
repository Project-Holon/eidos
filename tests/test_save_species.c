/*
 * tests/test_save_species.c -- sdk/save_species.c on a save image made
 * here: records masked and in the clear, each block order, a Bad Egg, the
 * newer of two copies, the Battle Box and the Day Care's slots.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_species.h"

#define COPY_SIZE   0x23F9Cu
#define SECOND      0x24000u
#define FOOTER      (COPY_SIZE - 0x10u)
#define IMAGE_SIZE  0x80000u

static int g_fail;
static u32 g_seen[64];
static int g_nseen;

static void put16(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

/* A record of `species` with personality `pid`, masked unless `clear`; a
 * wrong checksum when `bad`. */
static void make_record(u8 *r, u32 species, u32 pid, int clear, int bad)
{
    static const char *const orders[24] = {
        "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
        "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA"
    };
    const char *o = orders[((pid >> 13) & 0x1Fu) % 24u];
    u16 area[0x40];
    u32 i, sum = 0, s;

    for (i = 0; i < 0x40u; i++) {
        area[i] = (u16)(i * 37u + 5u);      /* (the other blocks: anything) */
    }
    area[(u32)(strchr(o, 'A') - o) * 16u] = (u16)species;
    for (i = 0; i < 0x40u; i++) {
        sum += area[i];
    }
    put32(r, pid);
    put16(r + 4, clear ? 3u : 0u);
    put16(r + 6, (sum + (bad ? 1u : 0u)) & 0xFFFFu);
    s = (sum + (bad ? 1u : 0u)) & 0xFFFFu;
    for (i = 0; i < 0x40u; i++) {
        u16 v = area[i];

        if (!clear) {
            s = s * 0x41C64E6Du + 0x6073u;
            v ^= (u16)(s >> 16);
        }
        put16(r + 8 + i * 2u, v);
    }
}

static void copy_footer(u8 *c, u32 count)
{
    put32(c + FOOTER, count);
    put32(c + FOOTER + 4, COPY_SIZE);
}

static void seen(void *ctx, u32 species)
{
    (void)ctx;
    if (g_nseen < 64) {
        g_seen[g_nseen++] = species;
    }
}

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        g_fail++;
    }
}

int main(void)
{
    u8 *img = (u8 *)calloc(1, IMAGE_SIZE);
    u8 *c = img + SECOND, *old = img;
    u32 pid;
    int n, k, found715 = 0, found716 = 0;

    if (img == NULL) {
        return 1;
    }
    /* the older copy holds a Pokemon the newer one does not */
    copy_footer(old, 10);
    make_record(old + 0x400, 999, 0x11111111u, 0, 0);
    copy_footer(c, 11);

    /* every block order once, in box 1 and box 2 */
    for (k = 0; k < 24; k++) {
        pid = (u32)k << 13 | 0x1234u;
        make_record(c + 0x400 + (u32)(k / 30) * 0x1000u + (u32)(k % 30) * 0x88u, 1u + (u32)k, pid, k & 1, 0);
    }
    make_record(c + 0x400 + 0x1000u + 0x88u * 29u, 25, 0x2222u, 0, 1);            /* a Bad Egg */
    put32(c + 0x18E00 + 4, 2);                                                   /* the party: two */
    make_record(c + 0x18E00 + 8, 715, 0xDEADBEEFu, 0, 0);
    make_record(c + 0x18E00 + 8 + 0xDC, 133, 0x0BADF00Du, 0, 0);
    make_record(c + 0x18E00 + 8 + 0xDC * 2, 150, 0x1u, 0, 0);                    /* past the count */
    make_record(c + 0x20A00 + 0x88 * 3, 716, 0x55555555u, 0, 0);                 /* the Battle Box */
    put32(c + 0x20E00 + 0xE4, 1);                                                /* Day Care slot 2 */
    make_record(c + 0x20E00 + 0xE4 + 4, 649, 0x77777777u, 0, 0);
    make_record(c + 0x20E00 + 4, 151, 0x99999999u, 0, 0);                        /* slot 1 not in use */

    n = bw_save_species_each(img, IMAGE_SIZE, seen, NULL);
    check(n == 24 + 2 + 1 + 1, "the count: 24 boxed, 2 in the party, 1 in the Battle Box, 1 in the Day Care");
    for (k = 0; k < 24; k++) {
        check(g_seen[k] == 1u + (u32)k, "each block order gives its species");
    }
    for (k = 0; k < g_nseen; k++) {
        found715 |= g_seen[k] == 715;
        found716 |= g_seen[k] == 716;
        check(g_seen[k] != 999 && g_seen[k] != 25 && g_seen[k] != 150 && g_seen[k] != 151,
              "the older copy, a Bad Egg, a slot past the party count and an empty Day Care slot are not read");
    }
    check(found715, "the party's species 715");
    check(found716, "the Battle Box's species 716");
    check(g_seen[g_nseen - 1] == 649, "the Day Care's second slot");
    check(bw_save_species_each(img, 0x1000, seen, NULL) == -1, "an image too short for a save");
    memset(img, 0, IMAGE_SIZE);
    check(bw_save_species_each(img, IMAGE_SIZE, seen, NULL) == -1, "an image with no save copy");
    free(img);
    printf("test_save_species: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail != 0;
}
