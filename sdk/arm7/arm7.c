/*
 * sdk/arm7/arm7.c -- the ARM7 stand-in's power-on and step, and its OS
 * module: the OS PXI tag (12) and the OSi_SyncWithOtherProc handshake.
 *
 * Why a stand-in: sdk/arm7/arm7.h.
 *
 * OS tag 12.  The ARM9 sends `command << 8` through OSi_SendToPxi
 * (src/matched/arm9_sdk/OSi_SendToPxi.c): 0x10 from OS_ResetSystem
 * (func_02087BCC.c), 0x20 twice from OS_Terminate.  Its callback,
 * OSi_CommonCallback (func_02084A38.c), takes command 0x10 back as "reset
 * occurred" and treats anything else as a reason to terminate.  So the
 * ARM7 answers a reset request with the same command and answers nothing
 * else: a terminate needs no reply, and an echo of one would only run
 * OS_Terminate a second time.
 *
 * OSi_SyncWithOtherProc (func_02084AB0.c), called twice by the lock
 * initialiser (func_02084B90.c) on the four-byte window at 0x02FFFFF0
 * (HW_INIT_LOCK_BUF): first with 0 (the ARM9 sends: it counts p[0] through
 * 0x80|i and waits for the ARM7 to mirror it into p[1], until the ARM7 sets
 * p[2]; then it sets p[3]), then with 1 (the ARM9 receives: the mirror
 * image).  The ARM7 runs the same function the other way round, 1 then 0.
 * The ARM9's loops wait in SVC_WaitByLoop, which steps this file, so the
 * matched function runs as written.  The state machine below is that
 * function with each wait turned into "return until the next step".
 */

#include <stdint.h>

#include "arm7.h"
#include "wm.h"
#include "dsmem.h"
#include "pxi.h"
#include "snd.h"

#define BW_OS_SYNC_WINDOW   0x02FFFFF0u

enum {
    SYNC_RX_START, SYNC_RX_COPY, SYNC_RX_WAIT_ACK,
    SYNC_TX_START, SYNC_TX_SET, SYNC_TX_WAIT,
    SYNC_DONE
};

static struct {
    int booted;
    int stepping;
    int sync_state;
    int sync_sum;
    int sync_i;
    u32 os_last_command;
} g_arm7;

/* ------------------------------------------------------------------ */
/* OS                                                                  */
/* ------------------------------------------------------------------ */

static void os_callback(int tag, u32 data, int err)
{
    u32 command = (data & 0x7F00u) >> 8;

    (void)tag;
    if (err)
        return;
    g_arm7.os_last_command = command;
    if (command == 0x10)
        (void)bw_pxi_arm7_send(BW_PXI_TAG_OS, command << 8, 0);
}

void bw_arm7_os_init(void)
{
    g_arm7.os_last_command = 0;
    g_arm7.sync_state = SYNC_RX_START;
    g_arm7.sync_sum = 0;
    g_arm7.sync_i = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_OS, os_callback);
}

u32 bw_arm7_os_last_command(void)
{
    return g_arm7.os_last_command;
}

int bw_arm7_os_synced(void)
{
    return g_arm7.sync_state == SYNC_DONE;
}

/* Run the ARM7's half of OSi_SyncWithOtherProc until it would wait.        */
static void os_sync_step(void)
{
    volatile u8 *p;

    if (!g_arm7.booted || g_arm7.sync_state == SYNC_DONE)
        return;
    p = (volatile u8 *)bw_ds_ptr(BW_OS_SYNC_WINDOW);

    for (;;) {
        switch (g_arm7.sync_state) {
        case SYNC_RX_START:                     /* OSi_SyncWithOtherProc(1) */
            p[1] = 0;
            g_arm7.sync_sum = 0;
            g_arm7.sync_state = SYNC_RX_COPY;
            break;
        case SYNC_RX_COPY:
            if (g_arm7.sync_sum >= 0x300) {
                p[3] = 0;
                p[2] = 1;
                g_arm7.sync_state = SYNC_RX_WAIT_ACK;
                break;
            }
            if (p[1] == p[0])
                return;
            p[1] = p[0];
            g_arm7.sync_sum += p[1];
            break;
        case SYNC_RX_WAIT_ACK:
            if (p[3] == 0)
                return;
            g_arm7.sync_state = SYNC_TX_START;
            break;
        case SYNC_TX_START:                     /* OSi_SyncWithOtherProc(0) */
            g_arm7.sync_i = 0;
            p[2] = 0;
            g_arm7.sync_state = SYNC_TX_SET;
            break;
        case SYNC_TX_SET:
            p[0] = (u8)((g_arm7.sync_i & 0xF) | 0x80);
            g_arm7.sync_state = SYNC_TX_WAIT;
            break;
        case SYNC_TX_WAIT:
            if (p[0] != p[1] && p[2] == 0)
                return;
            g_arm7.sync_i++;
            if (p[2] == 0) {
                g_arm7.sync_state = SYNC_TX_SET;
            } else {
                p[3] = 1;
                g_arm7.sync_state = SYNC_DONE;
            }
            break;
        default:
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Power and step                                                      */
/* ------------------------------------------------------------------ */

void bw_arm7_boot(void)
{
    if (g_arm7.booted)
        return;
    g_arm7.booted = 1;
    bw_arm7_os_init();
    bw_arm7_pm_init();
    bw_arm7_rtc_init();
    bw_arm7_ctrdg_init();
    bw_arm7_card_init();
    bw_arm7_fs_init();
    bw_arm7_snd_init();
    bw_arm7_nvram_init();
    bw_arm7_mic_init();
    bw_arm7_wm_init();      /* wireless: registered only on an air (wm.h) */
    bw_arm7_tp_init();
    bw_arm7_pad_init();
}

void bw_arm7_reset(void)
{
    /* The files the chips stand for get what was written before power-off. */
    bw_arm7_fs_reset();
    bw_arm7_nvram_reset();
    bw_arm7_wm_reset();
    g_arm7.booted = 0;
    g_arm7.stepping = 0;
    g_arm7.sync_state = SYNC_RX_START;
    g_arm7.sync_sum = 0;
    g_arm7.sync_i = 0;
    g_arm7.os_last_command = 0;
    bw_arm7_tp_reset();
}

void bw_arm7_step(void)
{
    if (g_arm7.stepping || !bw_pxi_powered())
        return;
    g_arm7.stepping = 1;
    /* The DS's ARM7 is up before the ARM9 can wait on it.                 */
    bw_arm7_boot();
    bw_pxi_service();
    os_sync_step();
    bw_arm7_snd_step();     /* sound status and alarms, sdk/arm7/snd.h */
    bw_arm7_fs_step();      /* the save file's delayed write            */
    bw_arm7_wm_step();      /* the radio: the air, beacons, timeouts    */
    g_arm7.stepping = 0;
}

void *bw_arm7_resolve(u32 value, u32 size, void *known, u32 known_size)
{
    if (known != NULL) {
        u32 off = value - (u32)(uintptr_t)known;

        if (off < known_size && size <= known_size - off)
            return (u8 *)known + off;
    }
#if UINTPTR_MAX == 0xFFFFFFFFu
    return (void *)(uintptr_t)value;    /* a 32-bit host: it is the pointer */
#else
    return NULL;
#endif
}
