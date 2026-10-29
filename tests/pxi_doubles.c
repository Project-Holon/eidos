/*
 * tests/pxi_doubles.c -- what tests/test_pxi.c's matched files name that
 * the eidos library does not define yet.
 *
 * test_pxi runs on the port's real address space (sdk/dsmem.c) and OS layer
 * (sdk/os_*.c), joined to the PXI by bw_port_reset.  What is left here is
 * not port code: the ARM9 data objects the matched PXI, PM, RTC, CTRDG and
 * CARD files under test name, the spelling aliases and asm veneers their
 * callers need, and SVC_WaitByLoop, the BIOS wait PM_Init spins in.  The
 * boot's own closure will bring the real definitions; when the library has
 * one, the copy here goes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "pxi.h"

/* SVC_WaitByLoop, an ARM9 BIOS call (asm in aletheia): the ARM9 waiting for
 * the other processor, so the ARM7 gets its turn (sdk/pxi.h).              */
void func_02004490(int count)
{
    (void)count;
    bw_arm7_step();
}

void SVC_WaitByLoop(int count)
{
    func_02004490(count);
}

/* ------------------------------------------------------------------ */
/* Data objects, aliases and veneers                                   */
/* ------------------------------------------------------------------ */

/* Storage for game objects; sized for either pointer width.                */
typedef union { u8 b[256]; void *p; u32 w; } TestObj;

TestObj g_02150FC8;                    /* os_pxi.o: reset/terminate flags  */
u16 OSi_IsInitReset;
char SDK_AUTOLOAD_DTCM_START[0x4000];
TestObj g_02153000;                    /* pm.o .bss                        */
int g_020A8724;                        /* PMi_WaitBusy's policy            */
TestObj PMi_BatteryLowCallbackInfo;
TestObj g_02151428;                    /* external.o (RTC) .bss            */
TestObj cardi_common[8];               /* CARDiCommon, 0x560+ bytes        */

void PMi_ProceedToExit(int factor) { (void)factor; }
u32 RTCEXi_CommonCallback(void) { return 4; }
int OS_IsRunOnTwl(void) { return 0; }
void CTRDGi_InitCommon(void) { }

/* PXI_Init, PXI_IsCallbackReady, OSi_CommonCallback, PMi_CommonCallback,
 * PMi_DummyCallback and CTRDGi_SendtoPxi are the matched files' own names now
 * (func_02088498.c, func_020885EC.c, func_02084A38.c, func_0208B950.c,
 * func_0208B840.c, func_02091150.c), so the forwarders that stood in for them
 * are gone; only PXI_Init's other callee still needs its spelling.         */
extern void func_020884A4(void);                    /* PXI_InitFifo, sdk/pxi.c */
void PXI_InitFifo(void) { func_020884A4(); }

extern int PXI_SendWordByFifo(int fifotag, u32 data, BOOL err);
int func_02088610(int fifotag, u32 data, BOOL err)
{
    return PXI_SendWordByFifo(fifotag, data, err);
}

/* RtcSendPxiCommand (0x02088F2C) is the matched file's own name since
 * aletheia's wave 19; this file only calls it. */
extern BOOL RtcSendPxiCommand(u32 command);

/* RTCi_ReadRawDateTimeAsync is a three-instruction asm veneer in aletheia:
 * `mov r0, #0x10` and a tail call of RtcSendPxiCommand.                    */
BOOL RTCi_ReadRawDateTimeAsync(void)
{
    return RtcSendPxiCommand(0x10);
}

extern void MSL_C_LIB_EmptyStub(void);
void CTRDGi_DummyCallback(int tag, u32 data, BOOL err)
{
    (void)tag;
    (void)data;
    (void)err;
    MSL_C_LIB_EmptyStub();
}

/* Addresses of the objects above, for the test's checks.                   */
void *test_cardi_common(void) { return cardi_common; }
void *test_os_pxi_bss(void) { return &g_02150FC8; }
void *test_pm_bss(void) { return &g_02153000; }
