/*
 * tests/test_pxi.c -- the PXI FIFO model and the ARM7 responders, driven by
 * the game's own matched ARM9 SDK code.
 *
 * Every round trip starts in matched ARM9 code (aletheia src/matched/,
 * compiled with its fixed addresses routed through bw_ds_ptr), crosses the
 * FIFO model in sdk/pxi.c, is answered by a responder in sdk/arm7/, and
 * ends in the ARM9 callback the matched code installed, taken as an
 * interrupt by the port's own OS layer.  The machine is the boot's:
 * bw_port_reset (sdk/port.h) resets the address space, the OS layer and the
 * PXI and joins them, so the ARM7 runs at every OS_RestoreInterrupts and
 * its replies are latched by bw_os_raise_irq.
 *
 * The PM round trip through the matched PMi_CommonCallback runs only on a
 * 32-bit target: that file and PM_Init view pm.o's .bss through byte
 * offsets that are only right with 4-byte pointers (docs/pointer-width.md).
 * On a 64-bit target the same requests are checked at the FIFO.
 */

#include <stdio.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "os.h"
#include "port.h"
#include "port_test.h"
#include "pxi.h"

/* tests/pxi_doubles.c */
void *test_cardi_common(void);
void *test_os_pxi_bss(void);
void *test_pm_bss(void);

/* sdk/os_irq.c */
u32  OS_DisableInterrupts(void);
u32  OS_EnableInterrupts(void);
u32  OS_ResetRequestIrqMask(u32 intr);    /* sdk/os_interrupt.c */

/* Matched ARM9 SDK code under test, declared as the matched files do.      */
typedef void (*Arm9Cb)(int tag, u32 data, BOOL err);
extern int  PXI_SendWordByFifo(int fifotag, u32 data, BOOL err);
extern void PXI_SetFifoRecvCallback(int fifotag, Arm9Cb callback);
extern BOOL PXI_IsCallbackReady(int fifotag, int proc);
extern void PXIi_HandlerRecvFifoNotEmpty(void);
extern void OS_InitReset(void);                /* OS: PXI init + tag 12   */
extern void OSi_SendToPxi(u16 data);
extern void OS_InitLock(void);                /* OS_InitLock             */
extern void PMi_SendPxiData(u32 data);
extern u32 PM_SendUtilityCommandAsync(u32 number,
        u16 parameter, u16 *retValue,
        void (*callback)(u32, void *), void *arg);
extern void PMi_CallCallbackAndUnlock(u32 result);
#if BW_TEST_PM_MATCHED
extern void PM_Init(void);                /* PM_Init                 */
extern u32 PM_SendUtilityCommand(u32 number,
        u16 parameter, u16 *retValue);
extern u32 PM_GetAmp(int *swBuf);
#endif
extern void RTC_Init(void);                /* RTC_Init                */
extern int  RTCi_WriteRawStatus2Async(void);
extern BOOL RtcSendPxiCommand(u32 command);
typedef struct { u32 year, month, day; int week; } RTCDate;
typedef struct { u32 hour, minute, second; } RTCTime;
extern int RTC_GetDateTimeAsync(RTCDate *date, RTCTime *time,
                         void (*cb)(int result, void *arg), void *arg);
extern void CTRDG_DummyInit(void);                /* CTRDG_DummyInit         */
extern void CARDi_OnFifoRecv(int tag, u32 data, BOOL err);
extern void RequestCommandProc(void);                /* SND RequestCommandProc  */

/* A recording ARM9 callback.                                              */
#define REC_MAX 32
static struct { int tag; u32 data; int err; } g_rec[REC_MAX];
static int g_nrec;

static void recorder(int tag, u32 data, BOOL err)
{
    if (g_nrec < REC_MAX) {
        g_rec[g_nrec].tag = tag;
        g_rec[g_nrec].data = data;
        g_rec[g_nrec].err = err ? 1 : 0;
    }
    g_nrec++;
}

static u32 rd32(u32 addr) { return *(volatile u32 *)bw_ds_ptr(addr); }
static u16 rd16(u32 addr) { return *(volatile u16 *)bw_ds_ptr(addr); }

/* IRQ 18 as the matched PXI init installs it, counted.                     */
static int g_irq18_taken;

static void counted_recv_handler(void)
{
    g_irq18_taken++;
    PXIi_HandlerRecvFifoNotEmpty();
}

static void boot(void)
{
    bw_port_reset();
    bw_io_write16(BW_REG_IME, 1);       /* as OS_InitIrqTable leaves it     */
}

/* ------------------------------------------------------------------ */

/* What bw_port_reset leaves joined (sdk/port_reset.c).                     */
static void test_port(void)
{
    /* IF's hook is the OS layer's and survived the PXI reset after it.  A
     * store is seen when it changes IF's bytes (sdk/os_interrupt.c), so two
     * requests are showing and the store acknowledges one.                 */
    bw_os_raise_irq(OS_IE_H_BLANK | OS_IE_V_COUNT);
    BW_CHECK_EQ("port: requests latched", bw_os_pending_irqs(),
                OS_IE_H_BLANK | OS_IE_V_COUNT);
    bw_os_poll();
    BW_CHECK_EQ("port: IF mirrors the latch", bw_io_read32(BW_REG_IF),
                OS_IE_H_BLANK | OS_IE_V_COUNT);
    *(volatile u32 *)bw_ds_ptr(BW_REG_IF) = OS_IE_H_BLANK;   /* acknowledge */
    bw_io_sync();
    BW_CHECK_EQ("port: a store to IF acknowledges", bw_os_pending_irqs(),
                OS_IE_V_COUNT);
    (void)OS_ResetRequestIrqMask(OS_IE_V_COUNT);

    /* A PXI request goes to the latch, not into IF's bytes.                */
    (void)OS_DisableInterrupts();
    (void)bw_pxi_arm7_send(BW_PXI_TAG_NVRAM, 1, 0);
    BW_CHECK_EQ("port: PXI request latched",
                bw_os_pending_irqs() & BW_IRQ_IPC_RECV, BW_IRQ_IPC_RECV);
    {
        u32 w;
        BW_CHECK_EQ("port: drain", bw_pxi_arm9_recv(&w), 0);
    }
    (void)OS_ResetRequestIrqMask(BW_IRQ_IPC_RECV);
    (void)OS_EnableInterrupts();
    BW_CHECK_EQ("port: nothing left", bw_os_pending_irqs(), 0);
}

static void test_init(void)
{
    static const int served[] = { 4, 5, 7, 8, 9, 11, 12, 13, 14 };
    unsigned i;

    BW_CHECK_EQ("init: ARM7 not ready before PXI_Init",
                PXI_IsCallbackReady(12, 1), 0);
    OS_InitReset();

    BW_CHECK_EQ("init: IPCFIFOCNT", bw_io_read16(BW_REG_IPCFIFOCNT),
                BW_FIFOCNT_ENABLE | BW_FIFOCNT_RECV_IRQ |
                BW_FIFOCNT_RECV_EMPTY | BW_FIFOCNT_SEND_EMPTY);
    BW_CHECK_EQ("init: IPCSYNC back to 0", bw_io_read16(BW_REG_IPCSYNC), 0);
    BW_CHECK_EQ("init: ARM7 sync nibble", bw_pxi_arm7_sync_out(), 0);
    BW_CHECK_EQ("init: IRQ 18 is the receive handler",
                g_02FE0020[18] == PXIi_HandlerRecvFifoNotEmpty, 1);
    BW_CHECK_EQ("init: IE bit 18", (bw_io_read32(0x04000210u) >> 18) & 1, 1);
    for (i = 0; i < sizeof served / sizeof served[0]; i++) {
        char label[48];

        sprintf(label, "init: ARM7 ready on tag %d", served[i]);
        BW_CHECK_EQ(label, PXI_IsCallbackReady(served[i], 1), 1);
    }
    /* Wireless is unserved on purpose (sdk/arm7/arm7.h).                   */
    BW_CHECK_EQ("init: no wireless responder", PXI_IsCallbackReady(10, 1), 0);
    BW_CHECK_EQ("init: ARM9 has tag 12", PXI_IsCallbackReady(12, 0), 1);
    BW_CHECK_EQ("init: Slot-2 module id", rd16(0x02FFFC30u), 0xFFFF);
}

static void test_os(void)
{
    u16 *flags = (u16 *)test_os_pxi_bss();

    /* Count the receive interrupts from here on; test_init has checked the
     * table entry is the matched install's.                                */
    g_02FE0020[18] = counted_recv_handler;
    OSi_SendToPxi(0x10);
    BW_CHECK_EQ("os: ARM7 saw reset request", bw_arm7_os_last_command(), 0x10);
    BW_CHECK_EQ("os: reply delivered by IRQ", g_irq18_taken, 1);
    BW_CHECK_EQ("os: OSi_IsResetOccurred", flags[0], 1);
    BW_CHECK_EQ("os: FIFO drained", bw_pxi_arm9_pending(), 0);

    OSi_SendToPxi(0x20);
    BW_CHECK_EQ("os: ARM7 saw terminate", bw_arm7_os_last_command(), 0x20);
    BW_CHECK_EQ("os: terminate is not answered", flags[1], 0);

    /* The hook path: a store with no restore after it, then bw_io_sync.   */
    flags[0] = 0;
    *(volatile u32 *)bw_ds_ptr(BW_REG_IPCFIFOSEND) =
        BW_PXI_WORD(BW_PXI_TAG_OS, 0x10u << 8, 0);
    bw_io_sync();
    BW_CHECK_EQ("os: sync hook took the word", bw_arm7_os_last_command(), 0x10);
    BW_CHECK_EQ("os: reply waiting", bw_pxi_arm9_pending(), 1);
    BW_CHECK_EQ("os: IF 18 latched by the OS layer",
                (bw_os_pending_irqs() & BW_IRQ_IPC_RECV) != 0, 1);
    BW_CHECK_EQ("os: not taken before a safe point", flags[0], 0);
    (void)OS_EnableInterrupts();
    BW_CHECK_EQ("os: delivered at enable", flags[0], 1);
    BW_CHECK_EQ("os: IF 18 acknowledged",
                (bw_os_pending_irqs() & BW_IRQ_IPC_RECV) != 0, 0);
}

static void test_os_sync(void)
{
    volatile u8 *p = (volatile u8 *)bw_ds_ptr(0x02FFFFF0u);

    /* OSi_SyncWithOtherProc is the OS layer's shim (sdk/os_system.c): it
     * leaves the window as both sides leave it once the handshake is done. */
    OS_InitLock();
    BW_CHECK_EQ("sync: tokens equal", p[0] == p[1] && p[0] != 0, 1);
    BW_CHECK_EQ("sync: p[2]", p[2], 1);
    BW_CHECK_EQ("sync: p[3]", p[3], 1);
    BW_CHECK_EQ("sync: lock word", rd32(0x02FFFFB0u), 0xFFFFFFFFu);
}

static int g_pm_result_calls;
static u32 g_pm_result;

static void pm_done(u32 result, void *arg)
{
    (void)arg;
    g_pm_result_calls++;
    g_pm_result = result;
}

#if !BW_TEST_PM_MATCHED
static void pm_unlock(int tag, u32 data, BOOL err)
{
    recorder(tag, data, err);
    PMi_CallCallbackAndUnlock(0);
}
#endif

static void test_pm(void)
{
#if BW_TEST_PM_MATCHED
    u16 value = 0;
    int sw = -1;

    PM_Init();
    BW_CHECK_EQ("pm: set amp", PM_SendUtilityCommand(0x10, 1, NULL), 0);
    BW_CHECK_EQ("pm: ARM7 amp on", bw_arm7_pm_state()->amp, 1);
    BW_CHECK_EQ("pm: PM_GetAmp", PM_GetAmp(&sw), 0);
    BW_CHECK_EQ("pm: amp reads back", sw, 1);
    BW_CHECK_EQ("pm: backlights", PM_SendUtilityCommand(0xF, 3, &value), 0);
    BW_CHECK_EQ("pm: both backlights on", value, 12);
    BW_CHECK_EQ("pm: async", PM_SendUtilityCommandAsync(7, 0, NULL, pm_done,
                                                          NULL), 0);
    BW_CHECK_EQ("pm: async callback", g_pm_result_calls, 1);
    BW_CHECK_EQ("pm: async result", g_pm_result, 0);
    BW_CHECK_EQ("pm: top backlight off", bw_arm7_pm_state()->backlight, 4);
#else
    PXI_SetFifoRecvCallback(BW_PXI_TAG_PM, pm_unlock);
    g_nrec = 0;
    BW_CHECK_EQ("pm: set amp", PM_SendUtilityCommandAsync(0x10, 1, NULL,
                                                           pm_done, NULL), 0);
    BW_CHECK_EQ("pm: one reply", g_nrec, 1);
    BW_CHECK_EQ("pm: reply word", g_rec[0].data, 0x6100);
    BW_CHECK_EQ("pm: ARM7 amp on", bw_arm7_pm_state()->amp, 1);
    BW_CHECK_EQ("pm: callback after unlock", g_pm_result_calls, 1);
    BW_CHECK_EQ("pm: get amp", PM_SendUtilityCommandAsync(0xF, 6, NULL,
                                                           pm_done, NULL), 0);
    BW_CHECK_EQ("pm: amp value in reply", g_rec[1].data, 0x6101);
#endif

    /* Sleep: the start is acknowledged and the wake-up follows.            */
    PXI_SetFifoRecvCallback(BW_PXI_TAG_PM, recorder);
    g_nrec = 0;
    PMi_SendPxiData(0x02000000u | 0x6200u | 0x05u);
    PMi_SendPxiData(0x01010000u | 0x1234u);
    BW_CHECK_EQ("pm: sleep replies", g_nrec, 2);
    BW_CHECK_EQ("pm: sleep acknowledged", g_rec[0].data, 0x6200);
    BW_CHECK_EQ("pm: woke up", g_rec[1].data, 0x6300);
}

static int g_rtc_calls;
static int g_rtc_result;

static void rtc_done(int result, void *arg)
{
    (void)arg;
    g_rtc_calls++;
    g_rtc_result = result;
}

static void test_rtc(void)
{
    static const BwArm7Time t = { 2026, 10, 1, 4, 13, 45, 30 };
    RTCDate date;
    RTCTime time;

    bw_arm7_rtc_fix_time(&t);
    RTC_Init();
    memset(&date, 0, sizeof date);
    memset(&time, 0, sizeof time);
    BW_CHECK_EQ("rtc: request", RTC_GetDateTimeAsync(&date, &time, rtc_done, NULL), 0);
    BW_CHECK_EQ("rtc: callback", g_rtc_calls, 1);
    BW_CHECK_EQ("rtc: result", g_rtc_result, 0);
    BW_CHECK_EQ("rtc: raw date", rd32(0x02FFFDE8u), 0x04011026u);
    BW_CHECK_EQ("rtc: raw time", rd32(0x02FFFDECu), 0x00304553u);
    BW_CHECK_EQ("rtc: year", date.year, 26);
    BW_CHECK_EQ("rtc: month", date.month, 10);
    BW_CHECK_EQ("rtc: day", date.day, 1);
    BW_CHECK_EQ("rtc: weekday", date.week, 4);
    BW_CHECK_EQ("rtc: hour", time.hour, 13);
    BW_CHECK_EQ("rtc: minute", time.minute, 45);
    BW_CHECK_EQ("rtc: second", time.second, 30);

    /* Status 2 is kept by the ARM7 and read back.                          */
    *(volatile u16 *)bw_ds_ptr(0x02FFFDEAu) = 0x0044;
    BW_CHECK_EQ("rtc: write status 2", RTCi_WriteRawStatus2Async(), 1);
    *(volatile u16 *)bw_ds_ptr(0x02FFFDEAu) = 0;
    BW_CHECK_EQ("rtc: read status 2", RtcSendPxiCommand(0x17), 1);
    BW_CHECK_EQ("rtc: status 2 read back", rd16(0x02FFFDEAu), 0x0044);

    /* An unknown command is refused with the SDK's invalid-command code.   */
    PXI_SetFifoRecvCallback(BW_PXI_TAG_RTC, recorder);
    g_nrec = 0;
    BW_CHECK_EQ("rtc: unknown command", RtcSendPxiCommand(0x7E), 1);
    BW_CHECK_EQ("rtc: refused", g_nrec == 1 ? g_rec[0].data : 0, 0x7E01);
    bw_arm7_rtc_fix_time(NULL);
}

static void test_ctrdg(void)
{
    CTRDG_DummyInit();
    BW_CHECK_EQ("ctrdg: ARM7 took the init word", bw_arm7_ctrdg_words(), 1);
    BW_CHECK_EQ("ctrdg: no reply", bw_pxi_arm9_pending(), 0);
    BW_CHECK_EQ("ctrdg: ARM9 callback installed", PXI_IsCallbackReady(13, 0), 1);
}

/*
 * CARDiCommon as src/matched/arm9_sdk/CARDi_OnFifoRecv.c declares it, for the
 * offset of the thread CARDi_OnFifoRecv wakes. DS scalars stay 32 bits.
 */
typedef struct {
    void *cmd;
    u32 flag;
    u32 priority;
    u32 flush_threshold_ic;
    u32 flush_threshold_dc;
    s32 lock_owner;
    int lock_ref;
    OSThreadQueue lock_queue[1];
    int lock_target;
    u8 thread[1216];
    void (*task_func)(void *);
    void (*callback)(void *);
    void *callback_arg;
    OSThreadQueue busy_q[1];
    u32 src, dst, len, dma;
    void *DmaCall;
    int req_type, req_retry, req_mode;
    OSThread *current_thread_9;
} TestCardiCommon;

static void test_fs(void)
{
    TestCardiCommon *card = (TestCardiCommon *)test_cardi_common();
    static OSThread requester;

    memset(&requester, 0, sizeof requester);
    requester.state = OS_THREAD_STATE_WAITING;
    card->current_thread_9 = &requester;

    PXI_SetFifoRecvCallback(BW_PXI_TAG_FS, CARDi_OnFifoRecv);
    card->flag |= 0x20;
    BW_CHECK_EQ("fs: init request", PXI_SendWordByFifo(11, 0, 1), 0);
    BW_CHECK_EQ("fs: waits for the block address", requester.state,
                OS_THREAD_STATE_WAITING);
    BW_CHECK_EQ("fs: block address", PXI_SendWordByFifo(11, 0x0215u, 1), 0);
    BW_CHECK_EQ("fs: CARDi_OnFifoRecv woke the requester", requester.state,
                OS_THREAD_STATE_READY);
    BW_CHECK_EQ("fs: busy flag cleared", card->flag & 0x20, 0);
    requester.state = OS_THREAD_STATE_WAITING;
    card->flag |= 0x20;
    BW_CHECK_EQ("fs: request 6", PXI_SendWordByFifo(11, 6, 1), 0);
    BW_CHECK_EQ("fs: second wake", requester.state, OS_THREAD_STATE_READY);
    card->current_thread_9 = NULL;
    BW_CHECK_EQ("fs: requests", bw_arm7_fs_requests(), 2);
    BW_CHECK_EQ("fs: last request", bw_arm7_fs_last_request(), 6);
}

static void test_card_snd(void)
{
    volatile u32 finish = 0;

    BW_CHECK_EQ("card: ARM9 word on tag 14", PXI_SendWordByFifo(14, 0x11, 0), 0);
    BW_CHECK_EQ("card: no reply", bw_pxi_arm9_pending(), 0);

    bw_arm7_snd_bind(&finish);
    BW_CHECK_EQ("snd: command list", PXI_SendWordByFifo(7, 0x123456, 0), 0);
    BW_CHECK_EQ("snd: finish tag advanced", finish, 1);
    RequestCommandProc();
    BW_CHECK_EQ("snd: process request is not a list", finish, 1);
    BW_CHECK_EQ("snd: lists", bw_arm7_snd_lists(), 1);
    bw_arm7_snd_bind(NULL);
}

static void test_unserved_and_depth(void)
{
    int i, ordered = 1;

    /* No ARM7 responder: the word comes back with the error bit.           */
    PXI_SetFifoRecvCallback(BW_PXI_TAG_WM, recorder);
    g_nrec = 0;
    BW_CHECK_EQ("unserved: send", PXI_SendWordByFifo(10, 0x55, 0), 0);
    BW_CHECK_EQ("unserved: echoed", g_nrec, 1);
    BW_CHECK_EQ("unserved: data", g_rec[0].data, 0x55);
    BW_CHECK_EQ("unserved: error bit", g_rec[0].err, 1);

    /* No ARM9 callback: the ARM9 returns it, and that ends there.          */
    (void)bw_pxi_arm7_send(9, 0x77, 0);
    (void)OS_EnableInterrupts();
    BW_CHECK_EQ("unclaimed: drained", bw_pxi_arm9_pending(), 0);

    /* Twenty words: sixteen in the FIFO, four behind it, all in order.     */
    PXI_SetFifoRecvCallback(BW_PXI_TAG_NVRAM, recorder);
    g_nrec = 0;
    (void)OS_DisableInterrupts();
    for (i = 0; i < 20; i++)
        (void)bw_pxi_arm7_send(BW_PXI_TAG_NVRAM, (u32)i, 0);
    BW_CHECK_EQ("depth: pending", bw_pxi_arm9_pending(), 20);
    BW_CHECK_EQ("depth: receive FIFO full",
                (bw_io_read16(BW_REG_IPCFIFOCNT) & BW_FIFOCNT_RECV_FULL) != 0, 1);
    (void)OS_EnableInterrupts();
    BW_CHECK_EQ("depth: all delivered", g_nrec, 20);
    for (i = 0; i < 20 && i < REC_MAX; i++)
        if (g_rec[i].data != (u32)i)
            ordered = 0;
    BW_CHECK_EQ("depth: in order", ordered, 1);
    BW_CHECK_EQ("depth: receive FIFO empty",
                (bw_io_read16(BW_REG_IPCFIFOCNT) & BW_FIFOCNT_RECV_EMPTY) != 0, 1);
}

int main(void)
{
    /* Before the port resets the PXI, the ARM7 does not run (the address
     * space is up, so PXI_IsCallbackReady can read the system work).      */
    bw_ds_reset();
    bw_arm7_step();
    BW_CHECK_EQ("power: not powered before reset", bw_pxi_powered(), 0);
    BW_CHECK_EQ("power: ARM7 not booted", PXI_IsCallbackReady(12, 1), 0);

    boot();
    test_init();
    test_port();
    test_os();
    test_os_sync();
    test_pm();
    test_rtc();
    test_ctrdg();
    test_fs();
    test_card_snd();
    test_unserved_and_depth();
    BW_REPORT("test_pxi");
}
