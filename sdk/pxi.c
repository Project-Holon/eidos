/*
 * sdk/pxi.c -- the IPC FIFO pair and IPCSYNC, and the two pxi_fifo.o
 * functions that cannot run as compiled on a host.
 *
 * sdk/pxi.h is the design.  This file is the hardware between the ARM9 and
 * the ARM7, plus the ARM7's PXI dispatch; the responders behind the
 * dispatch are in sdk/arm7/.
 *
 * Which pxi_fifo.o functions run as matched, and which are shims here
 * (docs/boot.md's rule: compile the matched source when its behaviour is
 * memory and register traffic, write a shim when it depends on the ARM7):
 *
 *   PXI_SendWordByFifo        matched: reads IPCFIFOCNT, stores the word.
 *   PXI_SetFifoRecvCallback   matched: a table store and a system-work bit.
 *   PXI_IsCallbackReady       matched (func_020885EC): reads system work.
 *   PXI_Init                  matched (func_02088498): calls the next one.
 *
 *   PXI_InitFifo (func_020884A4)          SHIM.  Its last loop is the
 *       IPCSYNC handshake with the ARM7: read the ARM7's nibble, echo it,
 *       spin until it changes.  The spin makes no call, so on a host the
 *       nibble can never change under it and the matched loop never ends.
 *       The shim is the same function with the ARM7's half of the
 *       handshake stepped inside the spin.
 *
 *   PXIi_HandlerRecvFifoNotEmpty (func_02088688)   SHIM.  It reads the
 *       receive register 0x04100000, whose read pops the other processor's
 *       FIFO, and which lies outside the register file's window
 *       (BW_DS_IO_SIZE).  The shim is the same loop over bw_pxi_arm9_recv.
 *
 * pxi_fifo.o's two statics, FifoCtrlInit (0x021513A4) and
 * FifoRecvCallbackTable (0x021513A8), are defined here because both shims
 * and the matched PXI_SetFifoRecvCallback share them and nothing else in
 * the game names them.
 *
 * The send register.  A matched store to 0x04000188 is an ordinary store
 * into the register file; nothing runs at the store.  The word is taken at
 * the next bw_pxi_service() (which reads the register directly) or the next
 * bw_io_sync() (through the write hook), whichever comes first, and the
 * register is reset to 0 so the same word stored again is seen again.  0
 * is never a real message: it is tag 0, which the SDK reserves and every
 * handler ignores.  Two stores with no service between them lose the first,
 * which is why the CPU stand-ins must call bw_arm7_step() from
 * OS_RestoreInterrupts (sdk/pxi.h).
 */

#include <stdio.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "pxi.h"

/*
 * pxi_fifo.o's .bss (0x021513A4, 0x021513A8). The FIFO payload and its
 * callback parameter stay 32 bits, including on LP64 hosts.
 */
typedef void (*BwPxiArm9Callback)(int tag, u32 data, BOOL err);
u16 FifoCtrlInit;
BwPxiArm9Callback FifoRecvCallbackTable[BW_PXI_TAGS];

/* The ARM9's interrupt plumbing, matched in aletheia.                      */
extern u32  OS_DisableInterrupts(void);
extern u32  OS_RestoreInterrupts(u32 state);
extern u32  OS_ResetRequestIrqMask(u32 intr);
extern void OS_SetIrqFunction(u32 intr, void (*function)(void));
extern u32  OS_EnableIrqMask(u32 intr);


void PXIi_HandlerRecvFifoNotEmpty(void);

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct BwFifo {
    u32 word[BW_PXI_FIFO_DEPTH];
    int head;
    int count;
} BwFifo;

/* The ARM7's software queue in front of its full hardware FIFO.            */
#define BW_ARM7_BACKLOG 256

static struct {
    BwFifo to_arm7;                     /* the ARM9's send FIFO             */
    BwFifo to_arm9;                     /* the ARM9's receive FIFO          */
    u32    backlog[BW_ARM7_BACKLOG];
    int    backlog_head;
    int    backlog_count;
    int    error;                       /* IPCFIFOCNT bit 14                */

    u32    arm7_sync_out;               /* ARM7 IPCSYNC bits 8-11           */
    u32    arm9_sync_out;               /* ARM9 IPCSYNC bits 8-11           */
    int    handshake_done;              /* the ARM7's PXI_InitFifo loop     */

    BwPxiArm7Callback arm7_cb[BW_PXI_TAGS];
    BwPxiIrqSink irq_sink;
    int    powered;                     /* bw_pxi_reset has run             */
    int    servicing;
    u32    unserved_logged;             /* one line per tag                 */
} g_pxi;

static int fifo_push(BwFifo *f, u32 w)
{
    if (f->count >= BW_PXI_FIFO_DEPTH)
        return -1;
    f->word[(f->head + f->count) % BW_PXI_FIFO_DEPTH] = w;
    f->count++;
    return 0;
}

static int fifo_pop(BwFifo *f, u32 *w)
{
    if (f->count == 0)
        return -1;
    *w = f->word[f->head];
    f->head = (f->head + 1) % BW_PXI_FIFO_DEPTH;
    f->count--;
    return 0;
}

static void raise_irq(u32 mask)
{
    if (g_pxi.irq_sink) {
        g_pxi.irq_sink(mask);
    } else {
        volatile u8 *p = bw_io(BW_REG_IF);
        u32 v = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
                ((u32)p[3] << 24);
        v |= mask;
        p[0] = (u8)v;
        p[1] = (u8)(v >> 8);
        p[2] = (u8)(v >> 16);
        p[3] = (u8)(v >> 24);
    }
}

/* Register-file access that bypasses the hooks (the hooks call these).     */
static u16 reg16(u32 addr)
{
    volatile u8 *p = bw_io(addr);
    return (u16)(p[0] | (p[1] << 8));
}

static void set_reg16(u32 addr, u16 v)
{
    volatile u8 *p = bw_io(addr);
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u32 reg32(u32 addr)
{
    volatile u8 *p = bw_io(addr);
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void set_reg32(u32 addr, u32 v)
{
    volatile u8 *p = bw_io(addr);
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/*
 * Keep IPCFIFOCNT's read-only bits current: the ARM9 polls them with no
 * call in between (PXI_SendWordByFifo tests SEND_FULL, the receive loop
 * tests RECV_EMPTY), so they must be right in memory at all times.
 */
static void update_fifocnt(void)
{
    u16 v = reg16(BW_REG_IPCFIFOCNT);

    v &= (u16)(BW_FIFOCNT_SEND_IRQ | BW_FIFOCNT_RECV_IRQ | BW_FIFOCNT_ENABLE);
    if (g_pxi.to_arm7.count == 0)
        v |= BW_FIFOCNT_SEND_EMPTY;
    if (g_pxi.to_arm7.count == BW_PXI_FIFO_DEPTH)
        v |= BW_FIFOCNT_SEND_FULL;
    if (g_pxi.to_arm9.count == 0)
        v |= BW_FIFOCNT_RECV_EMPTY;
    if (g_pxi.to_arm9.count == BW_PXI_FIFO_DEPTH)
        v |= BW_FIFOCNT_RECV_FULL;
    if (g_pxi.error)
        v |= BW_FIFOCNT_ERROR;
    set_reg16(BW_REG_IPCFIFOCNT, v);
}

/* IPCSYNC as the ARM9 reads it: its own output, the ARM7's as input.       */
static void update_ipcsync(void)
{
    u16 v = reg16(BW_REG_IPCSYNC);

    v &= (u16)(0x0F00u | 0x4000u);      /* output nibble, IRQ enable        */
    v |= (u16)(g_pxi.arm7_sync_out & 0xFu);
    set_reg16(BW_REG_IPCSYNC, v);
}

static void fill_from_backlog(void)
{
    while (g_pxi.backlog_count > 0 &&
           g_pxi.to_arm9.count < BW_PXI_FIFO_DEPTH) {
        (void)fifo_push(&g_pxi.to_arm9, g_pxi.backlog[g_pxi.backlog_head]);
        g_pxi.backlog_head = (g_pxi.backlog_head + 1) % BW_ARM7_BACKLOG;
        g_pxi.backlog_count--;
    }
}

/* ------------------------------------------------------------------ */
/* Register hooks                                                      */
/* ------------------------------------------------------------------ */

/* Take the word the ARM9 stored into the send register, if there is one.   */
static void take_send_register(void)
{
    u32 w = reg32(BW_REG_IPCFIFOSEND);

    if (w == 0)
        return;
    set_reg32(BW_REG_IPCFIFOSEND, 0);
    (void)bw_pxi_arm9_send(w);
}

static void hook_send(u32 addr, u32 old_value, u32 new_value)
{
    (void)addr;
    (void)old_value;
    (void)new_value;
    take_send_register();
    bw_pxi_service();
}

static void hook_fifocnt(u32 addr, u32 old_value, u32 new_value)
{
    (void)addr;
    (void)old_value;
    if (new_value & BW_FIFOCNT_SEND_CLEAR) {
        g_pxi.to_arm7.head = 0;
        g_pxi.to_arm7.count = 0;
    }
    if (new_value & BW_FIFOCNT_ERROR)    /* write 1 to acknowledge          */
        g_pxi.error = 0;
    update_fifocnt();
    if ((new_value & BW_FIFOCNT_RECV_IRQ) && g_pxi.to_arm9.count > 0)
        raise_irq(BW_IRQ_IPC_RECV);
}

static void hook_ipcsync(u32 addr, u32 old_value, u32 new_value)
{
    (void)addr;
    (void)old_value;
    g_pxi.arm9_sync_out = (new_value >> 8) & 0xFu;
    /* Bit 13 requests an IRQ on the ARM7; no ARM7 responder uses it.       */
    update_ipcsync();
}

/* ------------------------------------------------------------------ */
/* Life cycle and service                                              */
/* ------------------------------------------------------------------ */

void bw_pxi_reset(void)
{
    BwPxiIrqSink sink = g_pxi.irq_sink;

    memset(&g_pxi, 0, sizeof g_pxi);
    g_pxi.irq_sink = sink;
    g_pxi.powered = 1;
    FifoCtrlInit = 0;
    memset(FifoRecvCallbackTable, 0, sizeof FifoRecvCallbackTable);
    bw_arm7_reset();

    set_reg16(BW_REG_IPCSYNC, 0);
    set_reg32(BW_REG_IPCFIFOSEND, 0);
    set_reg16(BW_REG_IPCFIFOCNT, 0);
    update_fifocnt();

    bw_io_hook(BW_REG_IPCSYNC, 2, hook_ipcsync);
    bw_io_hook(BW_REG_IPCFIFOCNT, 2, hook_fifocnt);
    bw_io_hook(BW_REG_IPCFIFOSEND, 4, hook_send);
}

void bw_pxi_set_irq_sink(BwPxiIrqSink sink)
{
    g_pxi.irq_sink = sink;
}

/*
 * The ARM7's half of PXI_InitFifo's IPCSYNC handshake: whenever the ARM9
 * has echoed the ARM7's nibble, count it on by one; stop once the count has
 * come round to 0 again.  Sixteen echoes, so the ARM9's loop sees more than
 * four changes and then 0, which is its exit condition.
 */
static void step_handshake(void)
{
    if (g_pxi.handshake_done)
        return;
    if (g_pxi.arm9_sync_out != g_pxi.arm7_sync_out)
        return;
    g_pxi.arm7_sync_out = (g_pxi.arm7_sync_out + 1u) & 0xFu;
    if (g_pxi.arm7_sync_out == 0)
        g_pxi.handshake_done = 1;
    update_ipcsync();
}

/* The ARM7's PXIi_HandlerRecvFifoNotEmpty, over its own callback table.    */
static void arm7_dispatch(u32 w)
{
    int tag = BW_PXI_TAG_OF(w);
    BwPxiArm7Callback cb;

    if (tag == 0)
        return;
    cb = g_pxi.arm7_cb[tag];
    if (cb) {
        cb(tag, BW_PXI_DATA_OF(w), BW_PXI_ERR_OF(w));
        return;
    }
    if (!(g_pxi.unserved_logged & (1u << tag))) {
        g_pxi.unserved_logged |= 1u << tag;
        fprintf(stderr, "eidos: pxi: no ARM7 responder for tag %d "
                        "(word 0x%08lx); %s\n", tag, (unsigned long)w,
                BW_PXI_ERR_OF(w) ? "dropped"
                                 : "returned with the error bit");
    }
    if (!BW_PXI_ERR_OF(w))
        (void)bw_pxi_arm7_send(tag, BW_PXI_DATA_OF(w), 1);
}

void bw_pxi_service(void)
{
    u32 w;
    int drained = 0;

    if (g_pxi.servicing || !g_pxi.powered)
        return;
    g_pxi.servicing = 1;

    take_send_register();
    while (fifo_pop(&g_pxi.to_arm7, &w) == 0) {
        drained = 1;
        update_fifocnt();
        arm7_dispatch(w);
        take_send_register();
    }
    step_handshake();
    update_fifocnt();
    if (drained && (reg16(BW_REG_IPCFIFOCNT) & BW_FIFOCNT_SEND_IRQ))
        raise_irq(BW_IRQ_IPC_SEND_EMPTY);

    g_pxi.servicing = 0;
}

/* ------------------------------------------------------------------ */
/* The two sides                                                       */
/* ------------------------------------------------------------------ */

int bw_pxi_arm9_recv(u32 *word)
{
    if (fifo_pop(&g_pxi.to_arm9, word) != 0) {
        update_fifocnt();
        return -1;
    }
    fill_from_backlog();
    update_fifocnt();
    return 0;
}

int bw_pxi_arm9_send(u32 word)
{
    if (!(reg16(BW_REG_IPCFIFOCNT) & BW_FIFOCNT_ENABLE))
        return -1;
    if (fifo_push(&g_pxi.to_arm7, word) != 0) {
        g_pxi.error = 1;                /* GBATEK: send to a full FIFO      */
        update_fifocnt();
        return -2;
    }
    update_fifocnt();
    return 0;
}

int bw_pxi_powered(void)
{
    return g_pxi.powered;
}

int bw_pxi_arm9_pending(void)
{
    return g_pxi.to_arm9.count + g_pxi.backlog_count;
}

void bw_pxi_arm7_publish(void)
{
    volatile u32 *checker = (volatile u32 *)bw_ds_ptr(BW_PXI_HANDLE_CHECKER);
    u32 mask = 0;
    int tag;

    for (tag = 0; tag < BW_PXI_TAGS; tag++)
        if (g_pxi.arm7_cb[tag])
            mask |= 1u << tag;
    checker[1] = mask;
}

void bw_pxi_arm7_set_callback(int tag, BwPxiArm7Callback cb)
{
    if (tag <= 0 || tag >= BW_PXI_TAGS)
        return;
    g_pxi.arm7_cb[tag] = cb;
    bw_pxi_arm7_publish();
}

int bw_pxi_arm7_send(int tag, u32 data, int err)
{
    u32 w = BW_PXI_WORD(tag, data, err);

    if (g_pxi.backlog_count == 0 && fifo_push(&g_pxi.to_arm9, w) == 0) {
        /* went straight into the hardware FIFO */
    } else if (g_pxi.backlog_count < BW_ARM7_BACKLOG) {
        g_pxi.backlog[(g_pxi.backlog_head + g_pxi.backlog_count) %
                      BW_ARM7_BACKLOG] = w;
        g_pxi.backlog_count++;
    } else {
        fprintf(stderr, "eidos: pxi: ARM7 send queue overflow, tag %d "
                        "data 0x%08lx dropped\n", tag, (unsigned long)data);
        return -1;
    }
    update_fifocnt();
    if (reg16(BW_REG_IPCFIFOCNT) & BW_FIFOCNT_RECV_IRQ)
        raise_irq(BW_IRQ_IPC_RECV);
    return 0;
}

u32 bw_pxi_arm7_sync_out(void)
{
    return g_pxi.arm7_sync_out;
}

u32 bw_pxi_arm7_sync_in(void)
{
    return g_pxi.arm9_sync_out;
}

/* ------------------------------------------------------------------ */
/* The ARM9 shims                                                      */
/* ------------------------------------------------------------------ */

/*
 * PXI_InitFifo, 0x020884A4 (pxi_fifo.o), reached through PXI_Init's veneer
 * at 0x02088498.  The same steps as the matched source, in the same order;
 * IPCSYNC is reached through bw_io_read16/bw_io_write16 (the latter runs the
 * IPCSYNC hook, so the ARM7 sees each echo), and the spin steps the ARM7
 * where the DS's ARM7 would be running concurrently.
 */
void func_020884A4(void)
{
    u32 enabled = OS_DisableInterrupts();

    /* A port that never reset the PXI gets its power-on state here.       */
    if (!g_pxi.powered)
        bw_pxi_reset();

    if (!FifoCtrlInit) {
        volatile u32 *checker =
            (volatile u32 *)bw_ds_ptr(BW_PXI_HANDLE_CHECKER);
        int i;

        FifoCtrlInit = 1;
        checker[0] = 0;
        for (i = 0; i < BW_PXI_TAGS; i++)
            FifoRecvCallbackTable[i] = 0;

        bw_io_write16(BW_REG_IPCFIFOCNT,
                      (u16)(BW_FIFOCNT_ENABLE | BW_FIFOCNT_ERROR |
                            BW_FIFOCNT_RECV_IRQ | BW_FIFOCNT_SEND_CLEAR));

        (void)OS_ResetRequestIrqMask(BW_IRQ_IPC_RECV);
        OS_SetIrqFunction(BW_IRQ_IPC_RECV, PXIi_HandlerRecvFifoNotEmpty);
        (void)OS_EnableIrqMask(BW_IRQ_IPC_RECV);

        /* The DS's ARM7 has been up since power-on; this one starts here. */
        bw_arm7_boot();

        for (i = 0;; i++) {
            u32 c = bw_io_read16(BW_REG_IPCSYNC) & 15u;
            int timeout;

            bw_io_write16(BW_REG_IPCSYNC, (u16)(c << 8));
            if (c == 0 && i > 4)
                break;
            timeout = 1000;
            while ((bw_io_read16(BW_REG_IPCSYNC) & 15u) == c) {
                if (timeout <= 0) {
                    i = 0;
                    break;
                }
                timeout--;
                bw_pxi_service();
            }
        }
    }
    (void)OS_RestoreInterrupts(enabled);
}

/*
 * PXIi_HandlerRecvFifoNotEmpty, 0x02088688 (pxi_fifo.o): the IRQ 18
 * handler, also called directly by PMi_WaitBusy when interrupts are off.
 * The same loop as the matched source.  It services the ARM7 first, so a
 * caller polling it with interrupts disabled (PMi_WaitBusy) sees the reply
 * to what it has just sent.
 */
void PXIi_HandlerRecvFifoNotEmpty(void)
{
    u32 w;

    bw_pxi_service();
    while (bw_pxi_arm9_recv(&w) == 0) {
        int tag = BW_PXI_TAG_OF(w);

        if (tag == 0)
            continue;
        if (FifoRecvCallbackTable[tag]) {
            FifoRecvCallbackTable[tag](tag, BW_PXI_DATA_OF(w),
                                       (BOOL)BW_PXI_ERR_OF(w));
        } else if (!BW_PXI_ERR_OF(w)) {
            /* No ARM9 callback: return it to the sender with the error bit. */
            (void)bw_pxi_arm9_send(w | 0x20u);
        }
        bw_pxi_service();
    }
}
