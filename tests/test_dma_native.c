/* DMA must retain host pointer bits and translate raw DS addresses. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "dsmem.h"
#include "port_test.h"

void MIi_DmaFill32(u32 channel, void *dest, u32 data, u32 size, int enable);
void MIi_DmaCopy32(u32 channel, const void *src, void *dest, u32 size, int enable);
void MIi_DmaCopy16(u32 channel, const void *src, void *dest, u32 size, int enable);
void MIi_DmaSetParameters(u32 channel, uintptr_t src, uintptr_t dest, u32 ctrl, u32 mode);

int main(void)
{
    u32 *source = malloc(64), *dest = malloc(64);
    unsigned i;
    if (source == NULL || dest == NULL) return 2;
    bw_port_reset();
    for (i = 0; i < 16; i++) source[i] = 0x12340000u + i;
    MIi_DmaFill32(1, dest, 0x89ABCDEFu, 64, 1);
    for (i = 0; i < 16; i++) BW_CHECK_EQ("native fill", dest[i], 0x89ABCDEFu);
    MIi_DmaCopy32(1, source, dest, 64, 1);
    BW_CHECK_EQ("native copy32", memcmp(source, dest, 64), 0);
    memset(dest, 0, 64);
    MIi_DmaCopy16(1, source, dest, 62, 1);
    BW_CHECK_EQ("native copy16", memcmp(source, dest, 62), 0);
    BW_CHECK_EQ("copy16 bounds", ((u8 *)dest)[62], 0);
    memcpy(bw_ds_ptr(0x02010000u), source, 64);
    MIi_DmaSetParameters(1, 0x02010000u, 0x02020000u, 0x84000010u, 2);
    BW_CHECK_EQ("raw DS addresses", memcmp(source, bw_ds_ptr(0x02020000u), 64), 0);
    free(source);
    free(dest);
    BW_REPORT("test_dma_native");
}
