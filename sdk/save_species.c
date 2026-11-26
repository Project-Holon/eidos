/*
 * sdk/save_species.c -- the species a save holds (save_species.h).
 *
 * Read from the save image itself, with no game running: the newer of the
 * image's two copies (sdk/save_tidy.c has the layout), then each Pokemon
 * record of the party, the boxes, the Battle Box and the Day Care.  The
 * places are the game's save blocks (aletheia src/game/arm9/savedata.c and
 * the blocks' own files; each checked against the copy's CRC table on
 * real saves):
 *
 *   boxes       0x00400, 24 boxes of 0x1000: 30 records of 0x88
 *   party       0x18E00 (block 0x1A): u32, u32 count, 6 records of 0xDC
 *   Battle Box  0x20A00 (block 0x31, battle_box.c): 6 records of 0x88
 *   Day Care    0x20E00 (block 0x32, daycare.c): 2 slots of s32 inUse,
 *               a record of 0xDC, u32 steps
 *
 * A record (src/game/arm9/poke_tool_types.h): u32 personality, u16 flags,
 * u16 checksum, then a block area of 0x80 bytes: four blocks of 0x20 in an
 * order the personality picks ((pid >> 13) & 31) % 24, masked unless the
 * flags say it is decoded (the stream s = s * 0x41C64E6D + 0x6073 from the
 * checksum, the state's top halfword XORed in; sdk/mod_export.c writes out
 * the same).  The species is the first halfword of block A.  A record whose
 * checksum does not match is the game's Bad Egg and is skipped.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_species.h"

#define COPY_SIZE    0x23F9Cu
#define SECOND_COPY  0x24000u
#define FOOTER       (COPY_SIZE - 0x10u)

#define BOXES_AT     0x00400u
#define BOX_STRIDE   0x01000u
#define BOX_COUNT    24u
#define BOX_SLOTS    30u
#define PARTY_AT     0x18E00u
#define BATTLE_BOX_AT 0x20A00u
#define DAYCARE_AT   0x20E00u
#define BOX_RECORD   0x88u
#define PARTY_RECORD 0xDCu
#define DAYCARE_SLOT (4u + PARTY_RECORD + 4u)

static u32 le16(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 le32(const u8 *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

/* The four blocks' order a personality picks: block A's place in it. */
static u32 block_a_place(u32 pid)
{
    /* the 24 orders of A, B, C, D (ABCD, ABDC, ACBD, ...): where A is */
    static const u8 a_at[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3
    };

    return a_at[((pid >> 13) & 0x1Fu) % 24u];
}

/* A record's species, 0 for an empty slot or a Bad Egg. */
static u32 record_species(const u8 *r)
{
    u32 pid = le32(r), flags = le16(r + 4), check = le16(r + 6), sum = 0, s = check, i;
    u16 area[0x40];

    if (pid == 0 && check == 0) {
        return 0;
    }
    for (i = 0; i < 0x40u; i++) {
        area[i] = (u16)le16(r + 8 + i * 2u);
        if ((flags & 2u) == 0) {
            s = s * 0x41C64E6Du + 0x6073u;
            area[i] ^= (u16)(s >> 16);
        }
        sum += area[i];
    }
    if ((sum & 0xFFFFu) != check) {
        return 0;
    }
    return area[block_a_place(pid) * 16u];
}

/* The newer copy the game would take, or NULL. */
static const u8 *newer_copy(const u8 *image, size_t size)
{
    const u8 *a, *b;
    int ha, hb;

    if (image == NULL || size < SECOND_COPY + COPY_SIZE) {
        return NULL;
    }
    a = image;
    b = image + SECOND_COPY;
    ha = le32(a + FOOTER + 4) == COPY_SIZE;
    hb = le32(b + FOOTER + 4) == COPY_SIZE;
    if (ha && hb) {
        return le32(b + FOOTER) > le32(a + FOOTER) ? b : a;
    }
    return ha ? a : hb ? b : NULL;
}

int bw_save_species_each(const u8 *image, size_t size, void (*fn)(void *ctx, u32 species), void *ctx)
{
    const u8 *c = newer_copy(image, size);
    u32 i, k, sp, n = 0, party;

    if (c == NULL) {
        return -1;
    }
#define SEEN(rec) do { if ((sp = record_species(rec)) != 0) { fn(ctx, sp); n++; } } while (0)
    for (i = 0; i < BOX_COUNT; i++) {
        for (k = 0; k < BOX_SLOTS; k++) {
            SEEN(c + BOXES_AT + i * BOX_STRIDE + k * BOX_RECORD);
        }
    }
    party = le32(c + PARTY_AT + 4);
    for (k = 0; k < party && k < 6u; k++) {
        SEEN(c + PARTY_AT + 8u + k * PARTY_RECORD);
    }
    for (k = 0; k < 6u; k++) {
        SEEN(c + BATTLE_BOX_AT + k * BOX_RECORD);
    }
    for (k = 0; k < 2u; k++) {
        if (le32(c + DAYCARE_AT + k * DAYCARE_SLOT) != 0) {
            SEEN(c + DAYCARE_AT + k * DAYCARE_SLOT + 4u);
        }
    }
#undef SEEN
    return (int)n;
}
