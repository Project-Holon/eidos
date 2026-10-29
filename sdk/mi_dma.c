/*
 * sdk/mi_dma.c -- the SDK's DMA module (mi_dma.o) and the channel set-up it
 * bottoms out in (MIi_DmaSetParameters, ITCM), on a host with no DMA engine.
 *
 * MI_StopAllDma (0x02082658) is matched C that only calls MI_StopDma, and
 * is compiled as written.
 *
 * Why the module is a shim.  aletheia's matched C reaches a channel's
 * registers as `(vu32 *)(REG_DMA0SAD_ADDR + dmaNo * 12)` and similar: the DS
 * address is computed and then cast, which tools/dsaddr.py cannot rewrite
 * (docs/dsaddr-residue.md, "Constants with uses other than pointer
 * casts").  And the port's register file (sdk/dsmem.h) is plain memory with
 * no DMA engine behind it, so even correctly placed register stores would
 * move nothing.  So every function of the module is here, in the order the
 * console's code does things, with the transfer itself done by
 * MIi_DmaSetParameters when it sets the enable bit:
 *
 *   - the registers are still written (through bw_ds_ptr), so a reader of
 *     SAD/DAD/CNT sees what the DS would show, then the enable bit is
 *     cleared again because the transfer is already over;
 *   - the transfer runs immediately, whatever the start timing (vblank,
 *     hblank, display, GX FIFO): the port has no scanline to wait for, and
 *     the game waits for these with MI_WaitDma or a callback, which both
 *     see a finished transfer.  A repeating hblank transfer therefore runs
 *     once; that is the one behaviour the host does not reproduce;
 *   - source and destination are host addresses: the matched callers pass
 *     pointers converted to u32 (32-bit pointers, the boot's reference
 *     width), and a fill passes its value, which MIi_DmaSetParameters
 *     parks in the channel's fill register (0x040000E0 + 4 * dmaNo) and
 *     reads from there, as the DS does.
 *
 * Fill and copy order, the address controls (increment, decrement, fixed)
 * and the unit width (16 or 32 bits) follow the control word.  These are
 * the SDK's names; the boot reaches them by their address spellings through
 * tools/bootlink.py's renames, and cmake/boot.cmake lists the matched files
 * as replaced.
 */

#include <stdint.h>
#include <string.h>

#include "dsmem.h"
#include "port.h"

#define BW_REG_DMA0SAD  0x040000B0u
#define BW_REG_DMA0FILL 0x040000E0u
#define BW_DMA_ENABLE   0x80000000u
#define BW_DMA_32BIT    0x04000000u
#define BW_DMA_COUNT    0x001FFFFFu

typedef void (*BwMiDmaCallback)(void *arg);

u32  OS_DisableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);
void MIi_DmaSetParameters(u32 dmaNo, uintptr_t src, uintptr_t dest, u32 ctrl, u32 mode);
void func_01FF8020(u32 dmaNo, uintptr_t src, uintptr_t dest, u32 ctrl, u32 mode);
void MIi_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size, int dmaEnable);
void MIi_DmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size, int dmaEnable);
void MIi_DmaCopy16(u32 dmaNo, const void *src, void *dest, u32 size, int dmaEnable);
void MIi_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size,
                        BwMiDmaCallback callback, void *arg, int dmaEnable);
void MIi_DmaCopy32Async(u32 dmaNo, const void *src, void *dest, u32 size,
                        BwMiDmaCallback callback, void *arg, int dmaEnable);
void MI_WaitDma(u32 dmaNo);
void MI_StopDma(u32 dmaNo);
void MIi_CheckDma0SourceAddress(u32 dmaNo, u32 src, u32 size, u32 dir);

static volatile u32 *bw_dma_regs(u32 dmaNo)
{
    return (volatile u32 *)bw_ds_ptr(BW_REG_DMA0SAD + dmaNo * 12u);
}

/* Step for an address control: 0 increment, 1 decrement, 2 fixed,
 * 3 increment (reload applies to repeats only). */
static intptr_t bw_dma_step(u32 control, u32 unit)
{
    switch (control & 3u) {
    case 1:
        return -(intptr_t)unit;
    case 2:
        return 0;
    default:
        return (intptr_t)unit;
    }
}

/* MIi_Wait: the enable bit, which a finished transfer has cleared. */
static void bw_dma_wait(u32 dmaNo)
{
    volatile u32 *p = bw_dma_regs(dmaNo);

    while (p[2] & BW_DMA_ENABLE) {
    }
}

/* The transfer the channel's registers describe, done now. */
/*
 * The LCDC window of VRAM repeats every MiB: the ARM9 decodes 0x06800000
 * to 0x06FFFFFF as LCDC and keeps the low 20 bits, so 0x06A90000 is
 * 0x06890000 (melonDS does the same).  The host maps the banks only at
 * 0x06800000..0x068A3FFF, so a DMA to a mirror is folded back first.  The
 * trainer card's 3D palette load (ov111 VIEWER_ACTOR_LoadTexture) reaches
 * the palette bank through the mirror at +2 MiB.
 */
static uintptr_t bw_dma_lcdc_fold(uintptr_t a)
{
    if (a >= 0x06900000u && a < 0x07000000u) {
        return 0x06800000u + ((a - 0x06800000u) & 0xFFFFFu);
    }
    return a;
}

static void *bw_dma_pointer(uintptr_t address)
{
    address = bw_dma_lcdc_fold(address);
    if (address >= 0x01000000u && address < 0x0E000000u) {
        return bw_ds_ptr((u32)address);
    }
    return (void *)address;
}

static void bw_dma_run(uintptr_t src, uintptr_t dest, u32 ctrl)
{
    u32 unit = (ctrl & BW_DMA_32BIT) ? 4u : 2u;
    u32 count = ctrl & BW_DMA_COUNT;
    intptr_t ds = bw_dma_step(ctrl >> 21, unit);
    intptr_t ss = bw_dma_step(ctrl >> 23, unit);
    u8 *d;
    const u8 *s;
    u32 i;

    /* The addresses are raw: a VRAMCNT store since the last translation
     * (GX_BeginLoadTexPltt maps a bank to LCDC just before its DMA) must
     * have taken effect before the first byte moves. */
    bw_io_sync();
    d = (u8 *)bw_dma_pointer(dest);
    s = (const u8 *)bw_dma_pointer(src);

    if (count == 0) {
        count = 0x200000u;          /* 0 means the maximum */
    }
    for (i = 0; i < count; i++) {
        memmove(d, s, unit);
        d += ds;
        s += ss;
    }
}

/* MIi_DmaSetParameters at 0x01FF8020 (ITCM).  mode: bit 0 interrupts
 * already off, bit 1 wait and reset channel 0 afterwards (bit 2 skips the
 * reset), bit 4 a 32-bit fill value in src, bit 5 a 16-bit one. */
void MIi_DmaSetParameters(u32 dmaNo, uintptr_t src, uintptr_t dest, u32 ctrl, u32 mode)
{
    volatile u32 *p = bw_dma_regs(dmaNo);
    u32 enabled = 0;

    if (!(mode & 1u)) {
        enabled = OS_DisableInterrupts();
    }
    if (mode & 0x30u) {
        volatile u32 *fill = (volatile u32 *)bw_ds_ptr(BW_REG_DMA0FILL + dmaNo * 4u);

        if (mode & 0x10u) {
            *fill = src;
        } else {
            *(volatile u16 *)fill = (u16)src;
        }
        src = (uintptr_t)fill;
    }
    p[0] = src;
    p[1] = dest;
    p[2] = ctrl;
    if (ctrl & BW_DMA_ENABLE) {
        bw_dma_run(src, dest, ctrl);
        p[2] = ctrl & ~BW_DMA_ENABLE;
    }
    if ((mode & 2u) && !(mode & 4u) && dmaNo == 0) {
        p[0] = 0;
        p[1] = 0;
        p[2] = 0x81400001u & ~BW_DMA_ENABLE;
    }
    if (!(mode & 1u)) {
        (void)OS_RestoreInterrupts(enabled);
    }
}

void func_01FF8020(u32 dmaNo, uintptr_t src, uintptr_t dest, u32 ctrl, u32 mode)
{
    MIi_DmaSetParameters(dmaNo, src, dest, ctrl, mode);
}

/* 0x020821C0.  The front ends wait for the channel before and after; the
 * transfer is over when MIi_DmaSetParameters returns, so the waits are the
 * enable-bit tests, which pass. */
void MIi_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size, int dmaEnable)
{
    if (size == 0) {
        return;
    }
    bw_dma_wait(dmaNo);
    MIi_DmaSetParameters(dmaNo, data, (uintptr_t)dest,
                         (dmaEnable ? 0x85000000u : 0x05000000u) | (size / 4u),
                         dmaEnable ? 0x12u : 0x16u);
}

/* 0x02082244 */
void MIi_DmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size, int dmaEnable)
{
    if (size == 0) {
        return;
    }
    bw_dma_wait(dmaNo);
    MIi_DmaSetParameters(dmaNo, (uintptr_t)src, (uintptr_t)dest,
                         (dmaEnable ? 0x84000000u : 0x04000000u) | (size / 4u),
                         dmaEnable ? 0x2u : 0x6u);
}

/* 0x020822E8 */
void MIi_DmaCopy16(u32 dmaNo, const void *src, void *dest, u32 size, int dmaEnable)
{
    if (size == 0) {
        return;
    }
    bw_dma_wait(dmaNo);
    MIi_DmaSetParameters(dmaNo, (uintptr_t)src, (uintptr_t)dest,
                         (dmaEnable ? 0x80000000u : 0x00000000u) | (size / 2u),
                         dmaEnable ? 0x2u : 0x6u);
}

/* 0x02082390: the transfer, then the completion callback (the DMA
 * interrupt the DS would raise has nothing left to wait for). */
void MIi_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size,
                        BwMiDmaCallback callback, void *arg, int dmaEnable)
{
    MIi_DmaFill32(dmaNo, dest, data, size, dmaEnable);
    if (callback != NULL) {
        callback(arg);
    }
}

/* 0x02082498 */
void MIi_DmaCopy32Async(u32 dmaNo, const void *src, void *dest, u32 size,
                        BwMiDmaCallback callback, void *arg, int dmaEnable)
{
    MIi_DmaCopy32(dmaNo, src, dest, size, dmaEnable);
    if (callback != NULL) {
        callback(arg);
    }
}

static void bw_dma0_idle(void)
{
    volatile u32 *p = bw_dma_regs(0);

    p[0] = 0;
    p[1] = 0;
    p[2] = 0x81400001u & ~BW_DMA_ENABLE;
}

/* 0x02082594 */
void MI_WaitDma(u32 dmaNo)
{
    u32 enabled = OS_DisableInterrupts();
    volatile u32 *p = bw_dma_regs(dmaNo);

    while (p[2] & BW_DMA_ENABLE) {
    }
    if (dmaNo == 0) {
        bw_dma0_idle();
    }
    (void)OS_RestoreInterrupts(enabled);
}

/* 0x020825F0 */
void MI_StopDma(u32 dmaNo)
{
    u32 enabled = OS_DisableInterrupts();
    volatile u32 *p = bw_dma_regs(dmaNo);

    p[2] &= ~0x3A000000u;
    p[2] &= ~BW_DMA_ENABLE;
    if (dmaNo == 0) {
        bw_dma0_idle();
    }
    (void)OS_RestoreInterrupts(enabled);
}

/* 0x0208270C: channel 0 may not read the I/O area or the cartridge; the DS
 * stops in OS_Terminate.  The host's source is a host address the range
 * test cannot judge (docs/dsaddr-residue.md, "Range checks on converted
 * pointers"), so there is nothing to check. */
void MIi_CheckDma0SourceAddress(u32 dmaNo, u32 src, u32 size, u32 dir)
{
    (void)dmaNo;
    (void)src;
    (void)size;
    (void)dir;
}
