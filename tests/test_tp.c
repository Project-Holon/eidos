/*
 * tests/test_tp.c -- the touch panel: the ARM7 responder (sdk/arm7/tp.c)
 * answering the game's own matched ARM9 TP code, and the calibration that
 * turns a host point into the pixel the game computes.
 *
 * Three parts:
 *
 *  - The wire, at both pointer widths: words sent with the matched
 *    PXI_SendWordByFifo, replies taken by a recording ARM9 callback
 *    installed with the matched PXI_SetFifoRecvCallback, and the sample
 *    read out of HW_TOUCHPANEL_BUF.
 *  - The calibration, at both widths: the user settings the stand-in
 *    writes, read back by the matched TP_GetUserInfo, TP_CalcCalibrateParam
 *    and TP_SetCalibrateParam, then every pixel of the bottom screen carried
 *    through the panel model and the matched TP_GetCalibratedPoint, plus
 *    points worked out by hand below.
 *  - The round trip, on a 32-bit target: TP_Init, single sampling
 *    (TP_RequestSamplingAsync, TP_WaitRawResult), automatic sampling and
 *    its errors, through the matched TPi_TpCallback, and the game's own
 *    per-frame touch pass (src/matched/arm9/TOUCHPANEL_MainSys.c) reading the
 *    pixel back.  Those files view tp.o's .bss through layouts that agree
 *    with each other only with 4-byte pointers (TP_WaitBusy and
 *    TP_CheckError address it at fixed byte offsets 0x3a and 0x38), as
 *    pm.o's do in test_pxi.
 *
 * Hand-computed calibration, from sdk/arm7/tp.c's default points
 * (640, 800) at pixel (32, 32) and (3328, 3104) at (224, 160):
 *
 *   xDotSize = ((640 - 3328) << 8) / (32 - 224)  = -688128 / -192 = 3584
 *   x0       = (((640 + 3328) << 8) - 3584 * 256) >> 7 = 98304 >> 7 = 768
 *   yDotSize = ((800 - 3104) << 8) / (32 - 160)  = -589824 / -128 = 4608
 *   y0       = (((800 + 3104) << 8) - 4608 * 192) >> 7 = 114688 >> 7 = 896
 *   xDotSizeInv = 2^28 / 3584 = 74898,  yDotSizeInv = 2^28 / 4608 = 58254
 *
 * The panel gives the centre of pixel (x, y) as raw (192 + 14x + 7,
 * 224 + 18y + 9).  For (100, 50): raw (1599, 1133), and the game computes
 *   x = ((4 * 1599 - 768) * 74898) >> 22 = 421525944 >> 22 = 100
 *   y = ((4 * 1133 - 896) * 58254) >> 22 = 211811544 >> 22 = 50.
 * For (0, 0): raw (199, 233): x = (28 * 74898) >> 22 = 0, y = 0.
 * For (255, 191): raw (3769, 3671): x = (14308 * 74898) >> 22 = 255,
 * y = (13788 * 58254) >> 22 = 191.
 */

#include <stdio.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "input.h"
#include "os.h"
#include "port.h"
#include "port_test.h"
#include "pxi.h"

/* sdk/os_irq.c */
u32 OS_DisableInterrupts(void);
u32 OS_RestoreInterrupts(u32 state);

/* The matched files' own types.                                           */
typedef struct TPData {
    u16 x;
    u16 y;
    u16 touch;
    u16 validity;
} TPData;

typedef struct TPCalibrateParam {
    s16 x0;
    s16 y0;
    s16 xDotSize;
    s16 yDotSize;
} TPCalibrateParam;

typedef void (*TPRecvCallback)(int command, int result, u16 index);

/* tp.o's .bss as the full-layout files see it (TP_Init.c).          */
typedef struct TpBss {
    u16 initial;
    TPRecvCallback callback;
    TPData buf;
    u16 index;
    u16 frequence;
    TPData *samplingBufs;
    u16 bufSize;
    struct {
        long x0, xDotSize, xDotSizeInv, y0, yDotSize, yDotSizeInv;
    } calibrate;
    u16 calibrate_flg;
    u16 state;
    u16 err_flg;
    volatile u16 command_flg;
} TpBss;

TpBss g_02152FA4;

/* ------------------------------------------------------------------ */
/* Spellings the matched files call each other by                      */
/* ------------------------------------------------------------------ */

typedef void (*Arm9Cb)(int tag, u32 data, BOOL err);
extern void PXI_Init(void);
extern BOOL PXI_IsCallbackReady(int fifotag, int proc);
extern void func_020884A4(void);                    /* PXI_InitFifo, sdk/pxi.c */
extern int  PXI_SendWordByFifo(int fifotag, u32 data, BOOL err);
extern void PXI_SetFifoRecvCallback(int fifotag, Arm9Cb callback);

/* PXI_Init's matched callee, which sdk/pxi.c defines under its address name. */
void PXI_InitFifo(void);
void PXI_InitFifo(void)
{
    func_020884A4();
}

extern BOOL TP_GetUserInfo(TPCalibrateParam *calibrate);   /* TP_GetUserInfo */
extern void TP_SetCalibrateParam(const TPCalibrateParam *param); /* 0208AA2C */
extern u32 TP_CalcCalibrateParam(TPCalibrateParam *calibrate,
                                           u16 x1, u16 y1, u16 dx1, u16 dy1,
                                           u16 x2, u16 y2, u16 dx2, u16 dy2);
extern void TP_GetCalibratedPoint(TPData *disp, const TPData *raw);


#if BW_TEST_TP_MATCHED
extern void TP_Init(void);                          /* TP_Init      */
extern void TP_RequestSamplingAsync(void);                    /* RequestSampling    */
extern u32 TP_WaitRawResult(TPData *result); /* WaitRawResult      */
extern void TP_RequestAutoSamplingStartAsync(u16 vcount, u16 frequence,
                          TPData bufs[], u16 bufSize); /* TP_RequestAutoSamplingStartAsync   */
extern void TP_RequestAutoSamplingStopAsync(void);  /* TP_RequestAutoSamplingStopAsync      */
extern void TP_GetLatestRawPointInAuto(TPData *result);          /* LatestRawPointInAuto */
extern u16  TP_GetLatestIndexInAuto(void);          /* TP_GetLatestIndexInAuto      */
extern void TP_WaitBusy(unsigned int command_flgs); /* TP_WaitBusy      */
extern unsigned int TP_CheckError(unsigned int command_flgs); /* 0208B238  */
extern void TPi_TpCallback(int tag, u32 data, BOOL err);

/* The game's touch pass and its work-object resolver's double.             */
struct TouchCtx {
    u8 pad00[0x3e];
    u8 f3e;
};

struct TouchWork {
    u8  pad00[0x5c];
    u16 f5c, f5e, f60, f62, f64, f66, f68, f6a, f6c, f6e, f70, f72;
    u8  f74;
    u8  pad75[0x7c - 0x75];
};

static struct TouchWork g_work;

int TOUCHPANEL_GetWork(void);                       /* func_02035750      */
extern void TOUCHPANEL_MainSys(struct TouchCtx *ctx); /* TOUCHPANEL_MainSys   */

int TOUCHPANEL_GetWork(void)
{
    return (int)(uintptr_t)&g_work;
}
#endif

/* ------------------------------------------------------------------ */

#define TP_TAG 6

static struct { u32 data; int err; } g_rec[64];
static int g_nrec;

static void recorder(int tag, u32 data, BOOL err)
{
    (void)tag;
    if (g_nrec < 64) {
        g_rec[g_nrec].data = data;
        g_rec[g_nrec].err = err ? 1 : 0;
    }
    g_nrec++;
}

/* A safe point: what OS_RestoreInterrupts gives the ARM7 and the IRQs.    */
static void safe_point(void)
{
    u32 e = OS_DisableInterrupts();

    (void)OS_RestoreInterrupts(e);
}

static void touch_host(int down, int x, int y)
{
    BwInputState s;

    memset(&s, 0, sizeof s);
    s.touch = down;
    s.touch_x = x;
    s.touch_y = y;
    bw_input_set_host(&s);
    bw_input_publish();
}

static u32 raw_buf(void)
{
    volatile u16 *p = (volatile u16 *)bw_ds_ptr(0x02FFFFAAu);

    return (u32)p[0] | ((u32)p[1] << 16);
}

static void boot(void)
{
    bw_port_reset();
    bw_io_write16(BW_REG_IME, 1);       /* as OS_InitIrqTable leaves it     */
    bw_input_reset();
    memset(&g_02152FA4, 0, sizeof g_02152FA4);
}

/* ------------------------------------------------------------------ */
/* The wire                                                            */
/* ------------------------------------------------------------------ */

static void test_wire(void)
{
    const BwArm7TpState *st;

    boot();
    PXI_Init();
    BW_CHECK_EQ("wire: ARM7 ready on tag 6", PXI_IsCallbackReady(TP_TAG, 1), 1);
    PXI_SetFifoRecvCallback(TP_TAG, recorder);
    st = bw_arm7_tp_state();

    /* One sample with the stylus up: success, and an empty sample.        */
    g_nrec = 0;
    touch_host(0, 0, 0);
    BW_CHECK_EQ("wire: send", PXI_SendWordByFifo(TP_TAG, 0x03000000ul, 0), 0);
    safe_point();
    BW_CHECK_EQ("wire: one reply", g_nrec, 1);
    BW_CHECK_EQ("wire: reply start|end, cmd 0, ok", g_rec[0].data,
                0x03000000ul);
    BW_CHECK_EQ("wire: no touch", raw_buf(), 0);

    /* Stylus at (100, 50): raw (1599, 1133), touch bit 24, validity 0.    */
    touch_host(1, 100, 50);
    (void)PXI_SendWordByFifo(TP_TAG, 0x03000000ul, 0);
    safe_point();
    BW_CHECK_EQ("wire: raw word", raw_buf(),
                1599u | (1133u << 12) | (1u << 24));
    BW_CHECK_EQ("wire: requests", st->requests, 2);

    /* Automatic sampling: two words, 4 a frame from line 0.               */
    g_nrec = 0;
    (void)PXI_SendWordByFifo(TP_TAG, 0x02000100ul | 4, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul | 0, 0);
    safe_point();
    BW_CHECK_EQ("wire: auto on reply", g_rec[0].data, 0x03000100ul);
    BW_CHECK_EQ("wire: auto on", st->auto_on, 1);
    BW_CHECK_EQ("wire: frequence", st->frequence, 4);
    g_nrec = 0;
    bw_input_frame();
    safe_point();
    BW_CHECK_EQ("wire: four notifications", g_nrec, 4);
    BW_CHECK_EQ("wire: notification command 0x10",
                (g_rec[3].data >> 8) & 0x7F, 0x10);

    /* Errors: start while on, stop twice, bad frequency, bad line.        */
    g_nrec = 0;
    (void)PXI_SendWordByFifo(TP_TAG, 0x02000100ul | 1, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul | 0, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x03000200ul, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x03000200ul, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x02000100ul | 5, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul | 0, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x02000100ul | 1, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul | 263, 0);
    safe_point();
    BW_CHECK_EQ("wire: five replies", g_nrec, 5);
    BW_CHECK_EQ("wire: start while on: illegal status", g_rec[0].data,
                0x03000103ul);
    BW_CHECK_EQ("wire: stop: ok", g_rec[1].data, 0x03000200ul);
    BW_CHECK_EQ("wire: stop again: illegal status", g_rec[2].data,
                0x03000203ul);
    BW_CHECK_EQ("wire: frequency 5: invalid parameter", g_rec[3].data,
                0x03000102ul);
    BW_CHECK_EQ("wire: line 263: invalid parameter", g_rec[4].data,
                0x03000102ul);
    BW_CHECK_EQ("wire: off", st->auto_on, 0);
    g_nrec = 0;
    bw_input_frame();
    safe_point();
    BW_CHECK_EQ("wire: no samples when off", g_nrec, 0);

    /* A continuation with no command open is discarded, unanswered.       */
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul, 0);
    safe_point();
    BW_CHECK_EQ("wire: stray word unanswered", g_nrec, 0);
    BW_CHECK_EQ("wire: stray word counted", st->discarded, 1);

    /* The lid closed reads as no touch.                                   */
    {
        BwInputState s;

        memset(&s, 0, sizeof s);
        s.touch = 1;
        s.touch_x = 10;
        s.touch_y = 10;
        s.lid_closed = 1;
        bw_input_set_host(&s);
        bw_input_publish();
    }
    (void)PXI_SendWordByFifo(TP_TAG, 0x03000000ul, 0);
    safe_point();
    BW_CHECK_EQ("wire: lid closed, no touch", raw_buf(), 0);

    /* A power-on stops automatic sampling with the rest of the ARM7.       */
    (void)PXI_SendWordByFifo(TP_TAG, 0x02000100ul | 2, 0);
    (void)PXI_SendWordByFifo(TP_TAG, 0x01010000ul | 0, 0);
    safe_point();
    BW_CHECK_EQ("wire: on before the reset", st->auto_on, 1);
    boot();
    bw_input_frame();
    BW_CHECK_EQ("wire: off after the reset", st->auto_on, 0);
    BW_CHECK_EQ("wire: nothing sent after the reset", bw_pxi_arm9_pending(), 0);
}

/* ------------------------------------------------------------------ */
/* The calibration                                                     */
/* ------------------------------------------------------------------ */

static void calibrate_from_user_info(void)
{
    TPCalibrateParam param;

    memset(&param, 0x55, sizeof param);
    BW_CHECK_EQ("cal: TP_GetUserInfo", TP_GetUserInfo(&param), 1);
    BW_CHECK_EQ("cal: x0", param.x0, 768);
    BW_CHECK_EQ("cal: y0", param.y0, 896);
    BW_CHECK_EQ("cal: xDotSize", param.xDotSize, 3584);
    BW_CHECK_EQ("cal: yDotSize", param.yDotSize, 4608);
    TP_SetCalibrateParam(&param);
    BW_CHECK_EQ("cal: flag", g_02152FA4.calibrate_flg, 1);
    BW_CHECK_EQ("cal: xDotSizeInv", g_02152FA4.calibrate.xDotSizeInv, 74898);
    BW_CHECK_EQ("cal: yDotSizeInv", g_02152FA4.calibrate.yDotSizeInv, 58254);
}

static void point(const char *label, int x, int y, int want_x, int want_y)
{
    TPData raw, disp;
    u32 rx, ry;
    char l[80];

    bw_arm7_tp_raw_of(x, y, &rx, &ry);
    raw.x = (u16)rx;
    raw.y = (u16)ry;
    raw.touch = 1;
    raw.validity = 0;
    TP_GetCalibratedPoint(&disp, &raw);
    sprintf(l, "%s x", label);
    BW_CHECK_EQ(l, disp.x, want_x);
    sprintf(l, "%s y", label);
    BW_CHECK_EQ(l, disp.y, want_y);
}

static void test_calibration(void)
{
    const volatile u8 *info;
    u32 rx, ry;
    int x, y, wrong = 0;
    TPData raw, disp;

    boot();
    PXI_Init();                         /* powers the ARM7 on              */
    info = (const volatile u8 *)bw_ds_ptr(0x02FFFC80u + 0x58u);
    BW_CHECK_EQ("user info: raw_x1", info[0] | (info[1] << 8), 640);
    BW_CHECK_EQ("user info: raw_y1", info[2] | (info[3] << 8), 800);
    BW_CHECK_EQ("user info: dx1", info[4], 32);
    BW_CHECK_EQ("user info: dy1", info[5], 32);
    BW_CHECK_EQ("user info: raw_x2", info[6] | (info[7] << 8), 3328);
    BW_CHECK_EQ("user info: raw_y2", info[8] | (info[9] << 8), 3104);
    BW_CHECK_EQ("user info: dx2", info[10], 224);
    BW_CHECK_EQ("user info: dy2", info[11], 160);

    bw_arm7_tp_raw_of(100, 50, &rx, &ry);
    BW_CHECK_EQ("panel: raw x at 100", rx, 1599);
    BW_CHECK_EQ("panel: raw y at 50", ry, 1133);
    bw_arm7_tp_raw_of(255, 191, &rx, &ry);
    BW_CHECK_EQ("panel: raw x at 255", rx, 3769);
    BW_CHECK_EQ("panel: raw y at 191", ry, 3671);

    calibrate_from_user_info();
    point("cal (100, 50)", 100, 50, 100, 50);
    point("cal (0, 0)", 0, 0, 0, 0);
    point("cal (255, 191)", 255, 191, 255, 191);

    /* Every pixel of the bottom screen comes back as itself.               */
    for (y = 0; y < BW_TOUCH_HEIGHT; y++) {
        for (x = 0; x < BW_TOUCH_WIDTH; x++) {
            bw_arm7_tp_raw_of(x, y, &rx, &ry);
            raw.x = (u16)rx;
            raw.y = (u16)ry;
            raw.touch = 1;
            raw.validity = 0;
            TP_GetCalibratedPoint(&disp, &raw);
            if (disp.x != x || disp.y != y)
                wrong++;
        }
    }
    BW_CHECK_EQ("cal: every pixel round trips", wrong, 0);

    /* No touch: TP_GetCalibratedPoint reports (0, 0).                      */
    raw.touch = 0;
    TP_GetCalibratedPoint(&disp, &raw);
    BW_CHECK_EQ("cal: untouched x", disp.x, 0);

    /* Another panel: points taken the other way round, scales that are not
     * whole numbers.  TP_CalcCalibrateParam truncates the scale to 8.8 and
     * TP_SetCalibrateParam truncates its inverse, so a pixel may land one
     * off its centre's; the default points are chosen so that none does,
     * and any sensible calibration stays within one pixel.  Raw (700, 900)
     * at (32, 32), (3600, 3400) at (224, 160):
     *   xDotSize = (2900 << 8) / 192 = 742400 / 192 = 3866 (truncated).    */
    {
        BwArm7TpCalibration c;

        memset(&c, 0, sizeof c);
        c.raw_x1 = 3600; c.raw_y1 = 3400; c.dx1 = 224; c.dy1 = 160;
        c.raw_x2 = 700;  c.raw_y2 = 900;  c.dx2 = 32;  c.dy2 = 32;
        bw_arm7_tp_set_calibration(&c);
        boot();
        PXI_Init();
        info = (const volatile u8 *)bw_ds_ptr(0x02FFFC80u + 0x58u);
        BW_CHECK_EQ("custom: user info raw_x1", info[0] | (info[1] << 8),
                    3600);
        {
            TPCalibrateParam param;
            int worst = 0;

            BW_CHECK_EQ("custom: TP_GetUserInfo", TP_GetUserInfo(&param), 1);
            BW_CHECK_EQ("custom: xDotSize", param.xDotSize, 3866);
            TP_SetCalibrateParam(&param);
            for (y = 0; y < BW_TOUCH_HEIGHT; y++) {
                for (x = 0; x < BW_TOUCH_WIDTH; x++) {
                    int dx, dy;

                    bw_arm7_tp_raw_of(x, y, &rx, &ry);
                    raw.x = (u16)rx;
                    raw.y = (u16)ry;
                    raw.touch = 1;
                    raw.validity = 0;
                    TP_GetCalibratedPoint(&disp, &raw);
                    dx = (int)disp.x - x;
                    dy = (int)disp.y - y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    if (dx > worst) worst = dx;
                    if (dy > worst) worst = dy;
                }
            }
            BW_CHECK_EQ("custom: within one pixel", worst <= 1, 1);
        }
        bw_arm7_tp_set_calibration(NULL);
    }
}

/* ------------------------------------------------------------------ */
/* The round trip through the matched ARM9 TP library                  */
/* ------------------------------------------------------------------ */

#if BW_TEST_TP_MATCHED
static void test_round_trip(void)
{
    TPData raw, disp, bufs[9];
    struct TouchCtx ctx;

    boot();
    TP_Init();                                /* TP_Init            */
    BW_CHECK_EQ("tp: initialised", g_02152FA4.initial, 1);
    BW_CHECK_EQ("tp: callback installed", PXI_IsCallbackReady(TP_TAG, 0), 1);
    calibrate_from_user_info();

    /* Single sampling.                                                     */
    touch_host(1, 100, 50);
    TP_RequestSamplingAsync();                                /* RequestSampling    */
    BW_CHECK_EQ("tp: not busy after the reply", g_02152FA4.command_flg, 0);
    memset(&raw, 0, sizeof raw);
    BW_CHECK_EQ("tp: TP_WaitRawResult", TP_WaitRawResult(&raw), 0);
    BW_CHECK_EQ("tp: raw x", raw.x, 1599);
    BW_CHECK_EQ("tp: raw y", raw.y, 1133);
    BW_CHECK_EQ("tp: raw touch", raw.touch, 1);
    BW_CHECK_EQ("tp: raw validity", raw.validity, 0);
    TP_GetCalibratedPoint(&disp, &raw);
    BW_CHECK_EQ("tp: screen x", disp.x, 100);
    BW_CHECK_EQ("tp: screen y", disp.y, 50);
    touch_host(0, 0, 0);
    TP_RequestSamplingAsync();
    BW_CHECK_EQ("tp: untouched", TP_WaitRawResult(&raw) == 0 && raw.touch == 0,
                1);

    /* The game's touch pass: lid open, single-sampling mode (+0x74 = 0).   */
    memset(&ctx, 0, sizeof ctx);
    memset(&g_work, 0, sizeof g_work);
    touch_host(1, 37, 150);
    TOUCHPANEL_MainSys(&ctx);
    BW_CHECK_EQ("game: x", g_work.f5c, 37);
    BW_CHECK_EQ("game: y", g_work.f5e, 150);
    BW_CHECK_EQ("game: held", g_work.f62, 1);
    BW_CHECK_EQ("game: trigger", g_work.f60, 1);
    touch_host(1, 200, 9);
    TOUCHPANEL_MainSys(&ctx);
    BW_CHECK_EQ("game: drag x", g_work.f5c, 200);
    BW_CHECK_EQ("game: drag y", g_work.f5e, 9);
    BW_CHECK_EQ("game: no second trigger", g_work.f60, 0);
    touch_host(0, 0, 0);
    TOUCHPANEL_MainSys(&ctx);
    BW_CHECK_EQ("game: released", g_work.f62, 0);
    {
        BwInputState s;

        memset(&s, 0, sizeof s);
        s.touch = 1;
        s.touch_x = 5;
        s.touch_y = 5;
        s.lid_closed = 1;
        bw_input_set_host(&s);
        bw_input_publish();
    }
    g_work.f5c = 77;
    TOUCHPANEL_MainSys(&ctx);
    BW_CHECK_EQ("game: lid closed clears the work", g_work.f5c, 0);

    /* Automatic sampling, as unit_02035D5C.c starts it: line 0, 4 a frame,
     * a ring of 9.                                                         */
    touch_host(1, 200, 100);
    TP_RequestAutoSamplingStartAsync(0, 4, bufs, 9);
    TP_WaitBusy(2);
    BW_CHECK_EQ("auto: started", TP_CheckError(2), 0);
    BW_CHECK_EQ("auto: ARM9 state", g_02152FA4.state, 2);
    BW_CHECK_EQ("auto: ARM7 on", bw_arm7_tp_state()->auto_on, 1);
    bw_input_frame();
    safe_point();
    BW_CHECK_EQ("auto: four samples", TP_GetLatestIndexInAuto(), 4);
    BW_CHECK_EQ("auto: ring slot 4 x", bufs[4].x, 2999);  /* 192+14*200+7 */
    memset(&raw, 0, sizeof raw);
    TP_GetLatestRawPointInAuto(&raw);
    TP_GetCalibratedPoint(&disp, &raw);
    BW_CHECK_EQ("auto: latest touch", raw.touch, 1);
    BW_CHECK_EQ("auto: latest x", disp.x, 200);
    BW_CHECK_EQ("auto: latest y", disp.y, 100);
    bw_input_frame();
    bw_input_frame();
    safe_point();
    BW_CHECK_EQ("auto: ring wraps at 9", TP_GetLatestIndexInAuto(), 12 % 9);

    /* The game's pass in automatic mode.                                   */
    memset(&g_work, 0, sizeof g_work);
    g_work.f74 = 1;
    TOUCHPANEL_MainSys(&ctx);
    BW_CHECK_EQ("game auto: x", g_work.f5c, 200);
    BW_CHECK_EQ("game auto: y", g_work.f5e, 100);

    /* Errors come back through TPi_TpCallback into err_flg.                */
    TP_RequestAutoSamplingStartAsync(0, 4, bufs, 9);
    TP_WaitBusy(2);
    BW_CHECK_EQ("auto: start twice is an error", TP_CheckError(2), 2);
    TP_RequestAutoSamplingStopAsync();
    TP_WaitBusy(4);
    BW_CHECK_EQ("auto: stopped", TP_CheckError(4), 0);
    BW_CHECK_EQ("auto: ARM9 state ready", g_02152FA4.state, 0);
    BW_CHECK_EQ("auto: ARM7 off", bw_arm7_tp_state()->auto_on, 0);
    TP_RequestAutoSamplingStopAsync();
    TP_WaitBusy(4);
    BW_CHECK_EQ("auto: stop twice is an error", TP_CheckError(4), 4);
    TP_RequestAutoSamplingStartAsync(0, 5, bufs, 9);
    TP_WaitBusy(2);
    BW_CHECK_EQ("auto: frequency 5 is an error", TP_CheckError(2), 2);
    BW_CHECK_EQ("auto: still off", bw_arm7_tp_state()->auto_on, 0);
}
#endif

int main(void)
{
    test_wire();
    test_calibration();
#if BW_TEST_TP_MATCHED
    test_round_trip();
#else
    printf("round trip through the matched TP library: 32-bit targets only\n");
#endif
    BW_REPORT("test_tp");
}
