/*
 * sdk/boot/math.c -- MATHi_SHA1Fill (dgt_sha1.o, 0x0208130C) for the boot.
 *
 * MATH_SHA1GetHash (matched, src/matched/arm9_sdk/MATH_SHA1GetHash.c) pads
 * the last block through MATHi_SHA1Fill, which the vendor object keeps
 * static and aletheia matches static (src/matched/arm9_sdk/MATHi_SHA1Fill.c,
 * a file nothing else in the boot compiles).  The port supplies it with
 * external linkage (sdk/boot/gx.c says why sdk/boot/ exists): fill the
 * pending block with `input` up to `length` bytes, and every time the block
 * is full run the block function and count it.  The block function is the
 * pointer the SDK keeps at MATHi_SHA1ProcessMessageBlockFunc (0x020A8688),
 * read from the ARM9 image's data like the matched caller reads it
 * (docs/armdata.md: on the 32-bit reference host the word holds the host
 * function after relocation).
 */

#include "../dsmem.h"
#include "../port.h"

#define MATH_SHA1_BLOCK_SIZE 64u
#define SHA1_BLOCK_FUNC_PTR  0x020A8688u

typedef struct BwSha1Context {
    u32 h[5];
    u8  block[MATH_SHA1_BLOCK_SIZE];
    u32 pool;
    u32 blocks_low;
    u32 blocks_high;
} BwSha1Context;

typedef void (*BwSha1BlockFn)(BwSha1Context *context);

void func_02081E7C(void *context);
static void sha1_default_block(BwSha1Context *context)
{
    func_02081E7C(context);
}

/* A host function pointer cannot live in the ROM's four-byte cell. The
 * SDK's mode setter and both padding/update paths share this native cell. */
BwSha1BlockFn MATHi_SHA1ProcessMessageBlockFunc = sha1_default_block;

void MI_CpuFill8(void *dstp, u8 data, u32 size);
void MATHi_SHA1Fill(BwSha1Context *context, u8 input, u32 length);

void MATHi_SHA1Fill(BwSha1Context *context, u8 input, u32 length)
{
    while (length > 0) {
        u32 rest = MATH_SHA1_BLOCK_SIZE - context->pool;

        if (rest > length) {
            rest = length;
        }
        MI_CpuFill8(context->block + context->pool, input, rest);
        length -= rest;
        context->pool += rest;
        if (context->pool >= MATH_SHA1_BLOCK_SIZE) {
            MATHi_SHA1ProcessMessageBlockFunc(context);
            context->pool = 0;
            ++context->blocks_low;
            if (context->blocks_low == 0) {
                ++context->blocks_high;
            }
        }
    }
}
