/*
 * tests/test_snd.c -- the ARM7 sound driver (sdk/arm7/snd.h) through the
 * PXI wire: the sequencer, the shared channels and the hardware model.
 *
 * Usage: test_snd               the synthetic checks
 *        test_snd <data root>   also the real-data checks
 *
 * Synthetic: small SSEQs, an SBNK with a looping PCM8 wave, a PSG square,
 * noise and a one-shot PCM16 wave, and a SWAR with the two waves are
 * written into DS main RAM the way the game's loader leaves them, and
 * command lists built as the matched ARM9 senders build them
 * (BwSndCommandWire, the same declaration) are sent over the FIFO.  One
 * check per feature: the snd_util.o tables, sequences ending and looping
 * from their loop point, LoopStart/LoopEnd, Call, the prefix and variable
 * commands, SKIP_SEQ, the tempo ratio, 16 channels shared by priority,
 * ALLOCATABLE_CHANNEL and locks, track pitch, pitch bend, portamento, the
 * modulation LFO, Tie, mute modes, master pan, the output selector, direct
 * PCM/PSG/noise channels, a capture, a stream (a ring buffer refilled on
 * the driver's alarm, which must equal the same samples played in one
 * piece), driver info.
 *
 * Real data: SEQ_SE_DECIDE2 and SEQ_BGM_GYM through the command path from
 * copies of their SSEQ, SBNK and SWARs in main RAM; GYM fast-forwarded past
 * its end must still be playing (it loops from its loop point); and one of
 * GYM's own waves streamed through a ring buffer and an alarm, as the
 * game's stream object does it (src/matched/arm9/func_020738C0.c), equal
 * to the same wave in one piece.  The SDAT has no STRM entries, so a real
 * wave stands in for a stream file.
 *
 * Pointer arguments are what the matched senders write, `(u32)pointer` in
 * an unsigned long, wherever that keeps the whole pointer; elsewhere (LLP64)
 * the DS address, which is what the driver reads there.
 */

#include "port_test.h"
#include "port.h"
#include "audio.h"
#include "dsmem.h"
#include "pxi.h"
#include "arm7/arm7.h"
#include "arm7/snd.h"
#include "arm7/snd_seq.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BW_REQUIRE for the void test functions: count the failure and stop.    */
#define REQ(label, cond)                                                   \
    do {                                                                   \
        bw_checks++;                                                       \
        if (!(cond)) {                                                     \
            bw_failures++;                                                 \
            printf("  FAIL %-40s (%s)\n", (label), #cond);                 \
            return;                                                        \
        }                                                                  \
    } while (0)

/* A check that prints the two values when they differ.                    */
#define CHECK_RANGE(label, v, lo, hi)                                      \
    do {                                                                   \
        long v_ = (long)(v);                                               \
        bw_checks++;                                                       \
        if (v_ < (long)(lo) || v_ > (long)(hi)) {                          \
            bw_failures++;                                                 \
            printf("  FAIL %-40s (%ld not in %ld..%ld)\n", (label), v_,     \
                   (long)(lo), (long)(hi));                                \
        }                                                                  \
    } while (0)

/* A 64-bit equality, for values a 32-bit long cannot hold.               */
static void CHECK_EQ64(const char *label, s64 got, s64 want)
{
    bw_checks++;
    if (got != want) {
        bw_failures++;
        printf("  FAIL %-40s got %lld, want %lld\n", label, (long long)got,
               (long long)want);
    }
}

/* sdk/pxi.c's PXI_InitFifo shim names these two matched OS functions; the
 * boot links them from aletheia, and this test never runs PXI_InitFifo.    */
void OS_SetIrqFunction(u32 intr, void (*function)(void));
u32  OS_EnableIrqMask(u32 intr);
void OS_SetIrqFunction(u32 intr, void (*function)(void)) { (void)intr; (void)function; }
u32  OS_EnableIrqMask(u32 intr) { (void)intr; return 0; }

/* SNDSharedWork as the matched code declares it (func_0208A1D0.c).        */
typedef struct {
    unsigned long finishCommandTag;
    unsigned long playerStatus;
    unsigned short channelStatus;
    unsigned short captureStatus;
    unsigned long padding[5];
    struct {
        short variable[16];
        unsigned long tickCounter;
    } player[16];
    short globalVariable[16];
} SharedWork;

/* Where the test puts things in DS main RAM, clear of the ARM9 globals.   */
#define DS_SHARED   0x02400000u
#define DS_INFO     0x02401000u
#define DS_CAPTURE  0x02403000u
#define DS_STRM     0x02404000u
#define DS_SEQ      0x02410000u     /* one 4 KiB slot per player */
#define DS_BANK     0x02420000u
#define DS_WAVE     0x02430000u
#define DS_ONESHOT  0x02440000u
#define DS_REAL     0x02500000u

/* Output frames per driver frame, rounded up: 174592 * 32768 / 33513982. */
#define FRAME_OUT   171u

/* ------------------------------------------------------------------ */
/* DS memory and the wire                                              */
/* ------------------------------------------------------------------ */

/* Host memory for [addr, addr + size), every 32 KiB piece committed.      */
static u8 *ds_mem(u32 addr, u32 size)
{
    u32 off;
    for (off = 0; off < size; off += 0x8000u)
        (void)bw_ds_ptr(addr + off);
    if (size != 0)
        (void)bw_ds_ptr(addr + size - 1);
    return (u8 *)bw_ds_ptr(addr);
}

static unsigned long ptr_arg(const void *host, u32 ds)
{
    if (sizeof(unsigned long) >= sizeof(void *))
        return (unsigned long)(uintptr_t)host;
    return (unsigned long)ds;
}

static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static BwSndCommandWire g_pool[64];
static int g_used;

static void list_begin(void)
{
    g_used = 0;
}

static void push(int id, unsigned long a0, unsigned long a1, unsigned long a2,
                 unsigned long a3)
{
    BwSndCommandWire *c = &g_pool[g_used];
    c->next = NULL;
    c->id = id;
    c->arg[0] = a0;
    c->arg[1] = a1;
    c->arg[2] = a2;
    c->arg[3] = a3;
    if (g_used > 0)
        g_pool[g_used - 1].next = c;
    g_used++;
}

/* SND_FlushCommand's one FIFO word, then the ARM7 runs.                   */
static int flush(void)
{
    u32 w = BW_PXI_WORD(BW_PXI_TAG_SOUND,
                        (u32)(uintptr_t)&g_pool[0] & 0x03FFFFFFu, 0);
    int r = bw_pxi_arm9_send(w);
    bw_arm7_step();
    return r;
}

static void send1(int id, unsigned long a0, unsigned long a1, unsigned long a2,
                  unsigned long a3)
{
    list_begin();
    push(id, a0, a1, a2, a3);
    flush();
}

static void boot(void)
{
    bw_ds_reset();
    bw_pxi_reset();
    /* PXI_InitFifo's enable, without its IPCSYNC handshake. */
    bw_io_write16(BW_REG_IPCFIFOCNT,
                  (u16)(BW_FIFOCNT_ENABLE | BW_FIFOCNT_ERROR | BW_FIFOCNT_SEND_CLEAR));
    bw_snd_driver_attach();
    bw_arm7_boot();
    bw_arm7_snd_bind_pool(g_pool, 64, 0);
}

static SharedWork *g_sw;

static void bind_shared(void)
{
    g_sw = (SharedWork *)(void *)ds_mem(DS_SHARED, sizeof(SharedWork));
    memset(g_sw, 0, sizeof(*g_sw));
    send1(BW_SND_CMD_SHARED_WORK, ptr_arg(g_sw, DS_SHARED), 0, 0, 0);
}

/* Run the driver for `frames` output frames, then let it publish.          */
static void run(u32 frames)
{
    bw_snd_render(NULL, frames);
    bw_arm7_step();
}

static void run_driver_frames(u32 n)
{
    run(n * FRAME_OUT);
}

static int popcount16(u32 v)
{
    int n = 0;
    for (v &= 0xFFFFu; v; v &= v - 1)
        n++;
    return n;
}

static int lowest_bit(u32 v)
{
    int i;
    for (i = 0; i < 32; i++)
        if ((v >> i) & 1u)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Synthetic data                                                      */
/* ------------------------------------------------------------------ */

static void nitro_header(u8 *f, const char *magic, u32 size)
{
    memcpy(f, magic, 4);
    wr16(f + 4, 0xFEFF);
    wr16(f + 6, 0x0100);
    wr32(f + 8, size);
    wr16(f + 12, 0x10);
    wr16(f + 14, 1);
    memcpy(f + 0x10, "DATA", 4);
    wr32(f + 0x14, size - 0x10);
}

static void region(u8 *r, u16 wave, u16 arc, u8 key, u8 pan)
{
    wr16(r, wave);
    wr16(r + 2, arc);
    r[4] = key;
    r[5] = 127;   /* attack: at once      */
    r[6] = 127;   /* decay: at once       */
    r[7] = 127;   /* sustain: full        */
    r[8] = 127;   /* release: at once     */
    r[9] = pan;
}

/* Four instruments: 0 the looping PCM8 wave, 1 a PSG square (duty 3),
 * 2 noise, 3 the one-shot PCM16 wave.  All with key 60 as their own. */
static u32 make_sbnk(u8 *f)
{
    u32 size = 0x3C + 4 * 4 + 4 * 10, i;
    static const u8 types[4] = { 1, 2, 3, 1 };
    static const u16 waves[4] = { 0, 3, 0, 1 };
    memset(f, 0, size);
    nitro_header(f, "SBNK", size);
    wr32(f + 0x38, 4);
    for (i = 0; i < 4; i++) {
        wr32(f + 0x3C + 4 * i, types[i] | ((0x4Cu + 10 * i) << 8));
        region(f + 0x4C + 10 * i, waves[i], 0, 60, 64);
    }
    return size;
}

/* Wave 0: looping PCM8, 64 samples of a coarse triangle, timer 1023.
 * Wave 1: one-shot PCM16, 256 samples, timer 1023. */
static u32 make_swar(u8 *f)
{
    u32 w0 = 0x44, w1 = w0 + 12 + 64, size = w1 + 12 + 512, i;
    memset(f, 0, size);
    nitro_header(f, "SWAR", size);
    wr32(f + 0x38, 2);
    wr32(f + 0x3C, w0);
    wr32(f + 0x40, w1);
    f[w0] = 0;                          /* PCM8                           */
    f[w0 + 1] = 1;                      /* loop                           */
    wr16(f + w0 + 2, 16384);
    wr16(f + w0 + 4, 1023);
    wr16(f + w0 + 6, 0);                /* loop start, words              */
    wr32(f + w0 + 8, 16);               /* loop length, words             */
    for (i = 0; i < 64; i++) {
        int v = i < 32 ? (int)i * 7 - 112 : (int)(63 - i) * 7 - 112;
        f[w0 + 12 + i] = (u8)(s8)v;
    }
    f[w1] = 1;                          /* PCM16                          */
    f[w1 + 1] = 0;                      /* one-shot                       */
    wr16(f + w1 + 2, 16384);
    wr16(f + w1 + 4, 1023);
    wr16(f + w1 + 6, 0);
    wr32(f + w1 + 8, 128);
    for (i = 0; i < 256; i++)
        wr16(f + w1 + 12 + 2 * i, (u32)(u16)(s16)((int)(i % 32) * 900 - 14000));
    return size;
}

typedef struct {
    u8 *bank, *wave;
    u32 bank_size, wave_size;
} Bank;

static Bank g_bank;

static void load_bank(void)
{
    g_bank.bank = ds_mem(DS_BANK, 0x1000);
    g_bank.wave = ds_mem(DS_WAVE, 0x1000);
    g_bank.bank_size = make_sbnk(g_bank.bank);
    g_bank.wave_size = make_swar(g_bank.wave);
    /* SND_AssignWaveArc's effect on the bank: waveArcLink[0].waveArc. */
    wr32(g_bank.bank + 0x18, DS_WAVE);
}

/* Write an SSEQ around `body` in player `player`'s slot.  Returns the DS
 * address of seqBase (file + 0x1C). */
static u32 load_seq(int player, const u8 *body, u32 n)
{
    u32 ds = DS_SEQ + (u32)player * 0x1000u, size = 0x1C + n;
    u8 *f = ds_mem(ds, 0x1000);
    memset(f, 0, 0x1000);
    nitro_header(f, "SSEQ", size);
    wr32(f + 0x18, 0x1C);
    memcpy(f + 0x1C, body, n);
    return ds + 0x1C;
}

static void send_seq(int id, int player, const u8 *body, u32 n)
{
    u32 base = load_seq(player, body, n);
    list_begin();
    push(id, (unsigned long)player, ptr_arg(bw_ds_ptr(base), base), 0,
         ptr_arg(g_bank.bank, DS_BANK));
    flush();
}

static void start_seq(int player, const u8 *body, u32 n)
{
    send_seq(BW_SND_CMD_START_SEQ, player, body, n);
}

static void fresh(void)
{
    boot();
    bind_shared();
    load_bank();
}

static short var_of(int player, int v)
{
    return g_sw->player[player].variable[v];
}

/* ------------------------------------------------------------------ */
/* Sequences                                                           */
/* ------------------------------------------------------------------ */

/* Two tracks.  Track 0: tempo 150, program 0, two notes.  Track 1, opened
 * at payload offset 0x18: program 1 (the square), a rest, a note. */
static const u8 k_basic[] = {
    0xFE, 0x03, 0x00,                   /* AllocTrack 0,1                 */
    0x93, 0x01, 0x18, 0x00, 0x00,       /* OpenTrack 1 at +0x18           */
    0xE1, 0x96, 0x00,                   /* Tempo 150                      */
    0x81, 0x00,                         /* Program 0                      */
    0x3C, 0x7F, 0x30,                   /* Note 60 vel 127 dur 48         */
    0x40, 0x60, 0x18,                   /* Note 64 vel 96 dur 24          */
    0xFF, 0x00, 0x00, 0x00, 0x00,       /* End, padding up to +0x18       */
    0x81, 0x01,                         /* +0x18 track 1: Program 1       */
    0x80, 0x0C,                         /* Rest 12                        */
    0xC1, 0x50,                         /* Volume 80                      */
    0x43, 0x70, 0x24,                   /* Note 67 vel 112 dur 36         */
    0xFF
};

/* One long note on program 0, key 60 (the wave's own key). */
static const u8 k_one[] = { 0x81, 0x00, 0x3C, 0x7F, 0x83, 0x00, 0xFF };

/* var1 counts the prelude, var0 the passes through a loop that jumps back
 * to its loop point at +12, not to the top. */
static const u8 k_loop[] = {
    0xB0, 0x00, 0x00, 0x00,             /* @0  var0 = 0                   */
    0xB0, 0x01, 0x00, 0x00,             /* @4  var1 = 0                   */
    0xB1, 0x01, 0x01, 0x00,             /* @8  var1 += 1                  */
    0xB1, 0x00, 0x01, 0x00,             /* @12 var0 += 1                  */
    0x80, 0x30,                         /* @16 rest 48                    */
    0x94, 0x0C, 0x00, 0x00              /* @18 jump @12                   */
};

static const u8 k_loopcount[] = {
    0xB0, 0x02, 0x00, 0x00,             /* var2 = 0                       */
    0xD4, 0x03,                         /* LoopStart 3                    */
    0xB1, 0x02, 0x01, 0x00,             /* var2 += 1                      */
    0x80, 0x01,                         /* rest 1                         */
    0xFC,                               /* LoopEnd                        */
    0xB0, 0x03, 0x07, 0x00,             /* var3 = 7                       */
    0xFF
};

static const u8 k_call[] = {
    0xB0, 0x04, 0x00, 0x00,             /* @0  var4 = 0                   */
    0x95, 0x0F, 0x00, 0x00,             /* @4  call @15                   */
    0x95, 0x0F, 0x00, 0x00,             /* @8  call @15                   */
    0x80, 0x01,                         /* @12 rest 1                     */
    0xFF,                               /* @14                            */
    0xB1, 0x04, 0x01, 0x00,             /* @15 var4 += 1                  */
    0xFD                                /* @19 return                     */
};

static const u8 k_prefix[] = {
    0xA0, 0xB0, 0x05, 0x05, 0x00, 0x09, 0x00,   /* var5 = random 5..9     */
    0xB0, 0x06, 0x07, 0x00,                     /* var6 = 7               */
    0xB8, 0x06, 0x07, 0x00,                     /* cmp var6 == 7: true    */
    0xA2, 0xB0, 0x07, 0x01, 0x00,               /* if: var7 = 1           */
    0xB8, 0x06, 0x08, 0x00,                     /* cmp var6 == 8: false   */
    0xA2, 0xB0, 0x08, 0x01, 0x00,               /* if: var8 = 1 (skipped) */
    0xB0, 0x09, 0x30, 0x00,                     /* var9 = 48              */
    0xA1, 0x80, 0x09,                           /* rest var9              */
    0xB0, 0x0A, 0x01, 0x00,                     /* var10 = 1              */
    0xFF
};

static const u8 k_skip[] = {
    0xB0, 0x00, 0x00, 0x00,             /* @0  var0 = 0                   */
    0x81, 0x00,                         /* @4  program 0                  */
    0xB1, 0x00, 0x01, 0x00,             /* @6  var0 += 1                  */
    0x3C, 0x7F, 0x0A,                   /* @10 note 60, 10 ticks          */
    0x94, 0x06, 0x00, 0x00              /* @13 jump @6                    */
};

static u8 g_chord[2 + 2 + 17 * 3 + 3];

static u32 make_chord(void)
{
    u32 n = 0, i;
    g_chord[n++] = 0xC7;                /* note wait off: one chord        */
    g_chord[n++] = 0x00;
    g_chord[n++] = 0x81;
    g_chord[n++] = 0x00;
    for (i = 0; i < 17; i++) {
        g_chord[n++] = (u8)(48 + i);
        g_chord[n++] = 0x7F;
        g_chord[n++] = 0x60;
    }
    g_chord[n++] = 0x80;
    g_chord[n++] = 0x60;
    g_chord[n++] = 0xFF;
    return n;
}

static const u8 k_bend[] = { 0xC4, 0x40, 0x81, 0x00, 0x3C, 0x7F, 0x83, 0x00, 0xFF };
static const u8 k_porta[] = {
    0xC9, 0x48, 0xCF, 0x10,             /* porta from key 72, time 16     */
    0x81, 0x00, 0x3C, 0x7F, 0x83, 0x00, 0xFF
};
static const u8 k_mod[] = {
    0xCA, 0x7F, 0xCB, 0x10,             /* mod depth 127, speed 16        */
    0x81, 0x00, 0x3C, 0x7F, 0x83, 0x00, 0xFF
};
static const u8 k_tie[] = {
    0xC8, 0x01,                         /* tie on                         */
    0x81, 0x00,
    0x3C, 0x7F, 0x30,                   /* note 60, 48 ticks              */
    0x48, 0x7F, 0x30,                   /* note 72, 48 ticks: the same one */
    0xC8, 0x00,                         /* tie off: released              */
    0x80, 0x30, 0xFF
};

/* ------------------------------------------------------------------ */
/* Checks                                                              */
/* ------------------------------------------------------------------ */

static void test_tables(void)
{
    s64 sum[3], weighted[3];

    /* Sums of snd_util.o's PitchTable, SNDi_DecibelSquareTable and
     * VolumeTable from libsnd.TWL.HYB.a (measured with tools/sdkobj.py's
     * readers): the generated tables are the SDK's. */
    bw_snd_table_sums(sum, weighted);
    CHECK_EQ64("tables: pitch sum", sum[0], 22248808LL);
    CHECK_EQ64("tables: pitch weighted", weighted[0], 11748945286LL);
    CHECK_EQ64("tables: decibel-square sum", sum[1], -54129LL);
    CHECK_EQ64("tables: decibel-square weighted", weighted[1], -754298LL);
    CHECK_EQ64("tables: volume sum", sum[2], 30610LL);
    CHECK_EQ64("tables: volume weighted", weighted[2], 16515296LL);

    BW_CHECK_EQ("timer: unchanged at pitch 0", bw_snd_calc_timer(1023, 0), 1023);
    BW_CHECK_EQ("timer: an octave up halves", bw_snd_calc_timer(1000, 768), 500);
    BW_CHECK_EQ("timer: an octave down doubles", bw_snd_calc_timer(1000, -768), 2000);
    BW_CHECK_EQ("timer: clamped high", bw_snd_calc_timer(0x8000, -1536), 0xFFFF);
    BW_CHECK_EQ("timer: clamped low", bw_snd_calc_timer(16, 768), 0x10);
    BW_CHECK_EQ("volume: 0 dB", bw_snd_calc_channel_volume(0), 127);
    BW_CHECK_EQ("volume: -6.0 dB", bw_snd_calc_channel_volume(-60), 64);
    BW_CHECK_EQ("volume: -6.1 dB", bw_snd_calc_channel_volume(-61), 126 | (1 << 8));
    BW_CHECK_EQ("volume: floor", bw_snd_calc_channel_volume(-1000), 0 | (3 << 8));
    BW_CHECK_EQ("dB square: 127", bw_snd_decibel_square(127), 0);
    BW_CHECK_EQ("dB square: 64", bw_snd_decibel_square(64), -119);
    BW_CHECK_EQ("sine: quarter", bw_snd_sin_idx(32), 127);
    BW_CHECK_EQ("sine: three quarters", bw_snd_sin_idx(96), -127);
    BW_CHECK_EQ("attack: 127 is at once", bw_snd_attack_rate(127), 0);
    BW_CHECK_EQ("attack: 0 is slowest", bw_snd_attack_rate(0), 255);
    BW_CHECK_EQ("attack: 109", bw_snd_attack_rate(109), 143);
    BW_CHECK_EQ("fall: 127", bw_snd_fall_rate(127), 0xFFFF);
    BW_CHECK_EQ("fall: 126", bw_snd_fall_rate(126), 0x3C00);
    BW_CHECK_EQ("fall: 10", bw_snd_fall_rate(10), 21);
    BW_CHECK_EQ("fall: 100", bw_snd_fall_rate(100), 295);
}

static void test_wire(void)
{
    BwSndStats st;

    boot();
    bind_shared();
    BW_CHECK_EQ("wire: one list completed", bw_arm7_snd_lists(), 1);
    BW_CHECK_EQ("wire: finish tag in shared work", g_sw->finishCommandTag, 1);
    BW_CHECK_EQ("wire: nothing unresolved", bw_arm7_snd_unresolved(), 0);

    /* RequestCommandProc's 0 is not a list. */
    bw_pxi_arm9_send(BW_PXI_WORD(BW_PXI_TAG_SOUND, 0, 0));
    bw_arm7_step();
    BW_CHECK_EQ("wire: process request is not a list", g_sw->finishCommandTag, 1);

    /* A word that names no pool element completes unexecuted. */
    bw_pxi_arm9_send(BW_PXI_WORD(BW_PXI_TAG_SOUND,
                                 ((u32)(uintptr_t)&g_pool[0] + 2u) & 0x03FFFFFFu, 0));
    bw_arm7_step();
    BW_CHECK_EQ("wire: unresolved list still completes", g_sw->finishCommandTag, 2);
    BW_CHECK_EQ("wire: counted unresolved", bw_arm7_snd_unresolved(), 1);

    /* Three commands in one list run in order. */
    list_begin();
    push(BW_SND_CMD_MASTER_VOLUME, 100, 0, 0, 0);
    push(BW_SND_CMD_LOCK_CHANNEL, 0x0003, 0, 0, 0);
    push(BW_SND_CMD_UNLOCK_CHANNEL, 0x0001, 0, 0, 0);
    flush();
    BW_CHECK_EQ("wire: master volume", bw_snd_master_volume(), 100);
    BW_CHECK_EQ("wire: locked channels", bw_snd_locked_channels(), 2);
    bw_snd_stats(&st);
    BW_CHECK_EQ("wire: commands executed", st.commands, 4);
    BW_CHECK_EQ("wire: finish tag", g_sw->finishCommandTag, 3);
    /* SNDi_InitSharedWork's -1 for every variable, kept by the driver. */
    BW_CHECK_EQ("wire: variables start at -1", var_of(3, 7), -1);
    BW_CHECK_EQ("wire: globals start at -1", g_sw->globalVariable[2], -1);
}

static void test_sequence(void)
{
    BwSndPlayerState ps;
    BwSndStats st;
    s16 *buf;
    u32 k, frames = 0, seen = 0;
    s16 peak = 0;

    fresh();
    send_seq(BW_SND_CMD_PREPARE_SEQ, 3, k_basic, sizeof(k_basic));
    bw_snd_player_state(3, &ps);
    BW_CHECK_EQ("seq: prepared", ps.prepared, 1);
    BW_CHECK_EQ("seq: not active before START_PREPARED", ps.active, 0);
    BW_CHECK_EQ("seq: status before start", g_sw->playerStatus, 0);
    run(100);                           /* time passes; nothing plays */
    BW_CHECK_EQ("seq: silent while prepared", g_sw->channelStatus, 0);

    send1(BW_SND_CMD_START_PREPARED_SEQ, 3, 0, 0, 0);
    BW_CHECK_EQ("seq: status bit 3", g_sw->playerStatus, 1u << 3);

    buf = (s16 *)malloc(1024 * 2 * sizeof(s16));
    REQ("seq: buffer", buf != NULL);
    for (frames = 0; frames < 32768; frames += 1024) {
        bw_snd_render(buf, 1024);
        bw_arm7_step();
        seen |= g_sw->channelStatus;
        for (k = 0; k < 2048; k++)
            if (buf[k] > peak)
                peak = buf[k];
        if (frames + 1024 == 16384) {
            /* 150 BPM, a tick per 240 of tempo counter: 96 driver frames
             * in 0.5 s, the first spending the initial 240, so
             * (240 + 95 * 150) / 240 ticks. */
            BW_CHECK_EQ("seq: tick counter after 0.5 s", g_sw->player[3].tickCounter, 60);
            bw_snd_player_state(3, &ps);
            BW_CHECK_EQ("seq: tick in state", (long)ps.tick, 60);
            BW_CHECK_EQ("seq: not looping", ps.loops, 0);
        }
    }
    free(buf);
    REQ("seq: it sounds", peak > 1000);
    BW_CHECK_EQ("seq: the wave note on channel 0", (seen >> 0) & 1u, 1);
    BW_CHECK_EQ("seq: the square on channel 8", (seen >> 8) & 1u, 1);

    /* Play to the end: the status bit drops, the channels go quiet. */
    while (frames < 32768u * 8 && bw_snd_player_state(3, &ps) == 0 && ps.active) {
        run(1024);
        frames += 1024;
    }
    run(FRAME_OUT * 4);
    BW_CHECK_EQ("seq: ended", ps.active, 0);
    BW_CHECK_EQ("seq: status cleared", g_sw->playerStatus, 0);
    BW_CHECK_EQ("seq: channels quiet", g_sw->channelStatus, 0);
    bw_snd_stats(&st);
    BW_CHECK_EQ("seq: no bad addresses", st.bad_address, 0);
    BW_CHECK_EQ("seq: no dropped notes", st.notes_dropped, 0);

    /* START_SEQ is prepare and start in one; STOP_SEQ ends it. */
    start_seq(0, k_one, sizeof(k_one));
    BW_CHECK_EQ("seq: START_SEQ status", g_sw->playerStatus, 1u);
    send1(BW_SND_CMD_STOP_SEQ, 0, 0, 0, 0);
    BW_CHECK_EQ("seq: STOP_SEQ status", g_sw->playerStatus, 0);
}

static void test_control_flow(void)
{
    BwSndPlayerState ps;

    /* A loop restarts at its loop point: the prelude ran once. */
    fresh();
    start_seq(0, k_loop, sizeof(k_loop));
    run(32768);
    bw_snd_player_state(0, &ps);
    BW_CHECK_EQ("loop: prelude once", var_of(0, 1), 1);
    BW_CHECK_EQ("loop: two passes in 96 ticks", var_of(0, 0), 2);
    BW_CHECK_EQ("loop: flagged as looping", ps.loops, 1);
    BW_CHECK_EQ("loop: still playing", ps.active, 1);
    run(32768 * 3);
    BW_CHECK_EQ("loop: prelude still once", var_of(0, 1), 1);
    BW_CHECK_EQ("loop: 4 s, 8 passes", var_of(0, 0), 8);

    /* LoopStart 3 / LoopEnd, then the code after it. */
    start_seq(1, k_loopcount, sizeof(k_loopcount));
    run(8192);
    BW_CHECK_EQ("loopcount: body three times", var_of(1, 2), 3);
    BW_CHECK_EQ("loopcount: then on", var_of(1, 3), 7);

    /* Call twice, Return each time. */
    start_seq(2, k_call, sizeof(k_call));
    run(8192);
    BW_CHECK_EQ("call: subroutine twice", var_of(2, 4), 2);

    /* Prefixes: random, if, variable. */
    start_seq(3, k_prefix, sizeof(k_prefix));
    run(8192);                          /* 0.25 s: 24 ticks of 48 */
    BW_CHECK_EQ("prefix: rest var9 not over yet", var_of(3, 10), -1);
    CHECK_RANGE("prefix: random in 5..9", var_of(3, 5), 5, 9);
    BW_CHECK_EQ("prefix: if true ran", var_of(3, 7), 1);
    BW_CHECK_EQ("prefix: if false skipped", var_of(3, 8), -1);
    run(32768);
    BW_CHECK_EQ("prefix: rest var9 over", var_of(3, 10), 1);

    /* PLAYER_LOCAL_VAR and PLAYER_GLOBAL_VAR reach the shared work. */
    send1(BW_SND_CMD_PLAYER_LOCAL_VAR, 4, 3, (unsigned long)(u16)(s16)-5, 0);
    send1(BW_SND_CMD_PLAYER_GLOBAL_VAR, 2, 77, 0, 0);
    BW_CHECK_EQ("var: local set by the ARM9", var_of(4, 3), -5);
    BW_CHECK_EQ("var: global set by the ARM9", g_sw->globalVariable[2], 77);

    /* SKIP_SEQ: 100 ticks without a note, before the start. */
    send_seq(BW_SND_CMD_PREPARE_SEQ, 5, k_skip, sizeof(k_skip));
    send1(BW_SND_CMD_SKIP_SEQ, 5, 100, 0, 0);
    bw_arm7_step();
    BW_CHECK_EQ("skip: ten passes in 100 ticks", var_of(5, 0), 10);
    BW_CHECK_EQ("skip: tick counter", g_sw->player[5].tickCounter, 100);
    BW_CHECK_EQ("skip: no notes sounded", g_sw->channelStatus, 0);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 5, 0, 0, 0);
    run_driver_frames(2);
    REQ("skip: notes once started", g_sw->channelStatus != 0);
}

static void test_tempo(void)
{
    fresh();
    send_seq(BW_SND_CMD_PREPARE_SEQ, 0, k_loop, sizeof(k_loop));
    /* SND_SetPlayerTempoRatio(0, 512): 120 BPM played at 240. */
    send1(BW_SND_CMD_PLAYER_PARAM, 0, 0x1A, 512, 2);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 0, 0, 0, 0);
    run(32768);
    BW_CHECK_EQ("tempo ratio 2: a tick per frame", g_sw->player[0].tickCounter, 192);
    BW_CHECK_EQ("tempo ratio 2: four passes", var_of(0, 0), 4);
}

static void test_channels(void)
{
    u32 n = make_chord();
    BwSndStats st;
    BwSndPlayerState ps;

    /* Seventeen notes at once: sixteen channels.  At equal priority the
     * newest note takes the quietest channel, so none is refused. */
    fresh();
    start_seq(0, g_chord, n);
    run_driver_frames(2);
    BW_CHECK_EQ("alloc: all sixteen channels", g_sw->channelStatus, 0xFFFF);
    bw_snd_stats(&st);
    BW_CHECK_EQ("alloc: equal priority takes over", st.notes_dropped, 0);

    /* A louder priority takes a channel; a lower one does not. */
    send_seq(BW_SND_CMD_PREPARE_SEQ, 1, k_one, sizeof(k_one));
    send1(BW_SND_CMD_PLAYER_PARAM, 1, 4, 100, 1);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 1, 0, 0, 0);
    send_seq(BW_SND_CMD_PREPARE_SEQ, 2, k_one, sizeof(k_one));
    send1(BW_SND_CMD_PLAYER_PARAM, 2, 4, 0, 1);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 2, 0, 0, 0);
    run_driver_frames(2);
    bw_snd_player_state(1, &ps);
    BW_CHECK_EQ("prio: higher player got a channel", popcount16(ps.channels), 1);
    bw_snd_player_state(0, &ps);
    BW_CHECK_EQ("prio: the chord lost it", popcount16(ps.channels), 15);
    bw_snd_player_state(2, &ps);
    BW_CHECK_EQ("prio: lower player got none", ps.channels, 0);
    bw_snd_stats(&st);
    BW_CHECK_EQ("prio: its note counted dropped", st.notes_dropped, 1);

    /* STOP_UNLOCKED_CHANNEL silences them all. */
    send1(BW_SND_CMD_STOP_UNLOCKED_CHANNEL, 0xFFFF, 0, 0, 0);
    run(1);
    BW_CHECK_EQ("stop unlocked: quiet", g_sw->channelStatus, 0);

    /* ALLOCATABLE_CHANNEL: track 0 may use channel 5 only. */
    fresh();
    send_seq(BW_SND_CMD_PREPARE_SEQ, 0, k_one, sizeof(k_one));
    send1(BW_SND_CMD_ALLOCATABLE_CHANNEL, 0, 1u << 0, 1u << 5, 0);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 0, 0, 0, 0);
    run_driver_frames(2);
    bw_snd_player_state(0, &ps);
    BW_CHECK_EQ("allocatable: on channel 5", ps.channels, 1u << 5);

    /* Locked channels are never allocated, and locking drops a note. */
    send1(BW_SND_CMD_LOCK_CHANNEL, 0xFFFFu & ~(1u << 6), 0, 0, 0);
    run(1);
    BW_CHECK_EQ("lock: the note on 5 dropped", g_sw->channelStatus, 0);
    start_seq(1, k_one, sizeof(k_one));
    run_driver_frames(2);
    bw_snd_player_state(1, &ps);
    BW_CHECK_EQ("lock: the only free one, 6", ps.channels, 1u << 6);
    send1(BW_SND_CMD_UNLOCK_CHANNEL, 0xFFFF, 0, 0, 0);
    BW_CHECK_EQ("unlock", bw_snd_locked_channels(), 0);

    /* The header runs at PrepareSeq, so a mute sent before the start finds
     * track 1: its square never sounds, track 0's wave does. */
    fresh();
    send_seq(BW_SND_CMD_PREPARE_SEQ, 0, k_basic, sizeof(k_basic));
    send1(BW_SND_CMD_MUTE_TRACK, 0, 1u << 1, 3, 0);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 0, 0, 0, 0);
    {
        u32 seen = 0, i;
        for (i = 0; i < 16; i++) {
            run(1024);
            seen |= g_sw->channelStatus;
        }
        BW_CHECK_EQ("prepare: track 0 sounds", seen & 1u, 1);
        BW_CHECK_EQ("prepare: muted track 1 never does", (seen >> 8) & 1u, 0);
    }

    /* PSG notes go to 8-13, at the PSG base timer for their own key. */
    fresh();
    {
        static const u8 psg[] = { 0x81, 0x01, 0x3C, 0x7F, 0x83, 0x00, 0xFF };
        static const u8 noise[] = { 0x81, 0x02, 0x48, 0x7F, 0x83, 0x00, 0xFF };
        start_seq(0, psg, sizeof(psg));
        start_seq(1, noise, sizeof(noise));
        run_driver_frames(2);
        bw_snd_player_state(0, &ps);
        BW_CHECK_EQ("psg: on channel 8", ps.channels, 1u << 8);
        BW_CHECK_EQ("psg: key 60 at timer 8006", bw_snd_channel_timer(8), 8006);
        bw_snd_player_state(1, &ps);
        BW_CHECK_EQ("noise: on channel 14", ps.channels, 1u << 14);
        BW_CHECK_EQ("noise: key 72 an octave up", bw_snd_channel_timer(14), 4003);
    }
}

static void test_pitch(void)
{
    u32 t, lo = 0xFFFF, hi = 0;
    int i, ch;
    BwSndPlayerState ps;

    /* SND_SetTrackPitch: TRACK_PARAM ext_pitch (0x0C, s16) of 768. */
    fresh();
    start_seq(0, k_one, sizeof(k_one));
    run_driver_frames(2);
    BW_CHECK_EQ("pitch: the wave's own timer", bw_snd_channel_timer(0), 1023);
    send1(BW_SND_CMD_TRACK_PARAM, 0u | (2u << 24), 1u << 0, 0x0C, 768);
    run_driver_frames(2);
    BW_CHECK_EQ("pitch: an octave up", bw_snd_channel_timer(0), bw_snd_calc_timer(1023, 768));

    /* Pitch bend 64 with the default range of 2: one semitone (64/64). */
    start_seq(1, k_bend, sizeof(k_bend));
    run_driver_frames(2);
    bw_snd_player_state(1, &ps);
    ch = lowest_bit(ps.channels);
    REQ("bend: a channel", ch >= 0);
    BW_CHECK_EQ("bend: +64", bw_snd_channel_timer(ch), bw_snd_calc_timer(1023, 64));

    /* Portamento from key 72 to 60 over (16 * 16 * 768) >> 11 = 96 frames. */
    fresh();
    start_seq(0, k_porta, sizeof(k_porta));
    run_driver_frames(1);
    t = bw_snd_channel_timer(0);
    CHECK_RANGE("porta: starts near key 72", t, bw_snd_calc_timer(1023, 768),
                bw_snd_calc_timer(1023, 700));
    run_driver_frames(48);
    t = bw_snd_channel_timer(0);
    CHECK_RANGE("porta: half way", t, bw_snd_calc_timer(1023, 420), bw_snd_calc_timer(1023, 350));
    run_driver_frames(60);
    BW_CHECK_EQ("porta: arrived at key 60", bw_snd_channel_timer(0), 1023);

    /* Modulation: the timer swings both sides of 1023. */
    fresh();
    start_seq(0, k_mod, sizeof(k_mod));
    for (i = 0; i < 64; i++) {
        run_driver_frames(1);
        t = bw_snd_channel_timer(0);
        if (t < lo) lo = t;
        if (t > hi) hi = t;
    }
    REQ("mod: below", lo < 1023);
    REQ("mod: above", hi > 1023);
    CHECK_RANGE("mod: about a semitone down", lo, 960, 1000);

    /* Tie: the second key reuses the sounding channel. */
    fresh();
    start_seq(0, k_tie, sizeof(k_tie));
    run(8192);
    bw_snd_player_state(0, &ps);
    BW_CHECK_EQ("tie: one channel", ps.channels, 1u << 0);
    BW_CHECK_EQ("tie: first key", bw_snd_channel_timer(0), 1023);
    run(16384);
    bw_snd_player_state(0, &ps);
    BW_CHECK_EQ("tie: still that channel", ps.channels, 1u << 0);
    BW_CHECK_EQ("tie: second key", bw_snd_channel_timer(0), bw_snd_calc_timer(1023, 768));
    run(16384);
    BW_CHECK_EQ("tie: released when tie goes off", g_sw->channelStatus, 0);
}

static void test_levels(void)
{
    static s16 a[4096 * 2], b[4096 * 2];
    double sa = 0.0, sb = 0.0;
    long diff;
    u32 k;

    /* SND_SetPlayerVolume -60 (6 dB down) halves the level: the same note
     * through one channel, with and without. */
    fresh();
    start_seq(0, k_one, sizeof(k_one));
    bw_snd_render(a, 4096);
    fresh();
    start_seq(0, k_one, sizeof(k_one));
    send1(BW_SND_CMD_PLAYER_PARAM, 0, 6, (unsigned long)(u16)(s16)-60, 2);
    bw_snd_render(b, 4096);
    for (k = 2048; k < 4096 * 2; k++) {
        sa += fabs((double)a[k]);
        sb += fabs((double)b[k]);
    }
    REQ("fader: sounds", sa > 1e5);
    CHECK_RANGE("fader: -6 dB is half (permille)", (long)(1000.0 * sb / sa), 490, 515);

    /* MASTER_VOLUME 0 is silence. */
    send1(BW_SND_CMD_MASTER_VOLUME, 0, 0, 0, 0);
    bw_snd_render(a, 2048);
    for (k = 0, diff = 0; k < 2048 * 2; k++)
        diff += a[k] != 0;
    BW_CHECK_EQ("master volume 0: silence", diff, 0);
    send1(BW_SND_CMD_MASTER_VOLUME, 127, 0, 0, 0);

    /* MASTER_PAN 0: everything hard left; -1 (SND_ResetMasterPan) back. */
    send1(BW_SND_CMD_MASTER_PAN, 0, 0, 0, 0);
    BW_CHECK_EQ("master pan: set", bw_snd_master_pan(), 0);
    bw_snd_render(a, 2048);
    for (k = 0, diff = 0, sa = 0.0; k < 2048; k++) {
        diff += a[2 * k + 1] != 0;
        sa += fabs((double)a[2 * k]);
    }
    BW_CHECK_EQ("master pan 0: right silent", diff, 0);
    REQ("master pan 0: left sounds", sa > 1e4);
    send1(BW_SND_CMD_MASTER_PAN, (unsigned long)(u32)-1, 0, 0, 0);
    BW_CHECK_EQ("master pan: reset", bw_snd_master_pan(), -1);
    bw_snd_render(a, 2048);
    for (k = 0, sb = 0.0; k < 2048; k++)
        sb += fabs((double)a[2 * k + 1]);
    REQ("master pan reset: right sounds", sb > 1e4);

    /* OUTPUT_SELECTOR: both sides from channel 1, where nothing plays. */
    send1(BW_SND_CMD_OUTPUT_SELECTOR, 1, 1, 0, 0);
    bw_snd_render(a, 2048);
    for (k = 0, diff = 0; k < 2048 * 2; k++)
        diff += a[k] != 0;
    BW_CHECK_EQ("output selector: channel 1 only, silent", diff, 0);
    send1(BW_SND_CMD_OUTPUT_SELECTOR, 0, 0, 0, 0);

    /* Mute: NO_STOP keeps the note, STOP ends it at once. */
    send1(BW_SND_CMD_MUTE_TRACK, 0, 1u << 0, 1, 0);
    run_driver_frames(2);
    BW_CHECK_EQ("mute no-stop: still sounding", g_sw->channelStatus, 1u << 0);
    send1(BW_SND_CMD_MUTE_TRACK, 0, 1u << 0, 3, 0);
    run(1);
    BW_CHECK_EQ("mute stop: quiet", g_sw->channelStatus, 0);
}

static void test_pause_invalidate(void)
{
    unsigned long t0;

    fresh();
    start_seq(5, k_loop, sizeof(k_loop));
    run(8192);
    t0 = g_sw->player[5].tickCounter;
    send1(BW_SND_CMD_PAUSE_SEQ, 5, 1, 0, 0);
    run(8192);
    BW_CHECK_EQ("pause: no ticks", g_sw->player[5].tickCounter, t0);
    BW_CHECK_EQ("pause: still active", g_sw->playerStatus, 1u << 5);
    send1(BW_SND_CMD_PAUSE_SEQ, 5, 0, 0, 0);
    run(8192);
    REQ("pause: resumed, ticks move", g_sw->player[5].tickCounter > t0);

    /* SND_InvalidateBankData over the bank stops the player using it. */
    send1(BW_SND_CMD_INVALIDATE_BANK, ptr_arg(g_bank.bank, DS_BANK),
          ptr_arg(g_bank.bank + 0x100, DS_BANK + 0x100), 0, 0);
    BW_CHECK_EQ("invalidate: bank user stopped", g_sw->playerStatus, 0);
}

static void test_alarms(void)
{
    u32 w, words = 0, last = 0;
    BwSndStats st;
    int i;

    boot();
    bind_shared();
    /* SND_SetupAlarm(2, tick, period, ...) with alarm id 7: first after
     * 1000 OS ticks, then every 2000; SND_StartTimer(0, 0, 1 << 2, 0). */
    send1(BW_SND_CMD_SETUP_ALARM, 2, 1000, 2000, 7);
    send1(BW_SND_CMD_START_TIMER, 0, 0, 1u << 2, 0);
    while (bw_pxi_arm9_recv(&w) == 0) {
    }
    /* 32768 frames = 1 s = 523655 OS ticks: fires at 1000, 3000, ...; the
     * ARM9 takes the words as an audio callback's worth is mixed. */
    for (i = 0; i < 32; i++) {
        bw_snd_render(NULL, 1024);
        bw_arm7_step();
        while (bw_pxi_arm9_recv(&w) == 0) {
            if (BW_PXI_TAG_OF(w) == BW_PXI_TAG_SOUND) {
                words++;
                last = BW_PXI_DATA_OF(w);
            }
            bw_arm7_step();
        }
    }
    bw_snd_stats(&st);
    BW_CHECK_EQ("alarm: firings in one second", st.alarms_fired, 262);
    BW_CHECK_EQ("alarm: words delivered to the ARM9", words, 262);
    BW_CHECK_EQ("alarm: word is alarmNo | id << 8", last, 2u | (7u << 8));

    send1(BW_SND_CMD_STOP_TIMER, 0, 0, 1u << 2, 0);
    bw_snd_render(NULL, 32768);
    bw_snd_stats(&st);
    BW_CHECK_EQ("alarm: stopped", st.alarms_fired, 262);
}

/* ------------------------------------------------------------------ */
/* Direct channels                                                     */
/* ------------------------------------------------------------------ */

static void setup_pcm(int ch, int format, const void *data, u32 ds, int loop,
                      u32 pnt, u32 len, u32 timer, int pan)
{
    send1(BW_SND_CMD_SETUP_CHANNEL_PCM, (unsigned long)ch | ((unsigned long)timer << 16),
          ptr_arg(data, ds), (127ul << 24) | len,
          ((unsigned long)loop << 26) | ((unsigned long)format << 24)
          | ((unsigned long)pan << 16) | pnt);
}

/*
 * A stream as the game's stream object runs one: lock a channel, point it
 * at a looping ring of NBLK blocks, arm an alarm whose period is one
 * block's playing time, start both with one START_TIMER, and on each alarm
 * write the next block into the slot just played.  The output must equal
 * the whole signal played in one piece from a flat buffer.
 */
#define BLK  512u
#define NBLK 4u
#define STRM_TIMER 512u
/* One block in OS ticks: BLK samples * STRM_TIMER timer clocks * 2 bus
 * cycles per timer clock / 64 bus cycles per OS tick. */
#define BLK_TICKS (BLK * STRM_TIMER * 2u / 64u)

static long stream_vs_flat(const s16 *sig, u32 n_samples, u32 frames, s16 *peak_out)
{
    s16 *a = (s16 *)malloc((size_t)frames * 2 * sizeof(s16));
    s16 *b = (s16 *)malloc((size_t)frames * 2 * sizeof(s16));
    u8 *ring, *flat;
    u32 next = NBLK, done, i, k;
    long diff = 0;
    s16 peak = 0;

    if (a == NULL || b == NULL) {
        free(a);
        free(b);
        return -1;
    }

    /* In one piece. */
    boot();
    bind_shared();
    flat = ds_mem(DS_ONESHOT, n_samples * 2);
    for (i = 0; i < n_samples; i++)
        wr16(flat + 2 * i, (u32)(u16)sig[i]);
    send1(BW_SND_CMD_LOCK_CHANNEL, 1u << 0, 0, 0, 0);
    setup_pcm(0, 1, flat, DS_ONESHOT, 2, 0, n_samples / 2, STRM_TIMER, 64);
    send1(BW_SND_CMD_START_TIMER, 1u << 0, 0, 0, 0);
    bw_snd_render(b, frames);

    /* Streamed. */
    boot();
    bind_shared();
    ring = ds_mem(DS_STRM, BLK * NBLK * 2);
    for (i = 0; i < BLK * NBLK; i++)
        wr16(ring + 2 * i, (u32)(u16)sig[i]);
    send1(BW_SND_CMD_LOCK_CHANNEL, 1u << 0, 0, 0, 0);
    setup_pcm(0, 1, ring, DS_STRM, 1, 0, BLK * NBLK / 2, STRM_TIMER, 64);
    send1(BW_SND_CMD_SETUP_ALARM, 0, BLK_TICKS, BLK_TICKS, 5);
    list_begin();
    push(BW_SND_CMD_START_TIMER, 1u << 0, 0, 1u << 0, 0);
    flush();
    for (done = 0; done < frames; done += 64) {
        u32 w, chunk = frames - done < 64 ? frames - done : 64;
        bw_snd_render(a + (size_t)done * 2, chunk);
        bw_arm7_step();
        while (bw_pxi_arm9_recv(&w) == 0) {
            if (BW_PXI_TAG_OF(w) != BW_PXI_TAG_SOUND || BW_PXI_DATA_OF(w) != (0u | (5u << 8)))
                continue;
            /* The refill: block `next` into the slot just played. */
            for (k = 0; k < BLK; k++) {
                u32 at = next * BLK + k;
                wr16(ring + 2 * ((next % NBLK) * BLK + k),
                     at < n_samples ? (u32)(u16)sig[at] : 0u);
            }
            next++;
        }
    }
    for (k = 0; k < frames * 2; k++) {
        diff += a[k] != b[k];
        if (a[k] > peak)
            peak = a[k];
    }
    free(a);
    free(b);
    if (peak_out)
        *peak_out = peak;
    return diff;
}

static void test_direct(void)
{
    static s16 out[32768 * 2];
    static s16 sig[BLK * 16];
    u32 i, n_flip = 0, n_pos = 0, n_neg = 0;
    long diff;
    s16 peak;
    u8 *cap;

    /* PSG square, duty 3 (half), timer 1024: 16756991 / (8 * 1024) Hz,
     * so twice that many sign changes in one second. */
    fresh();
    send1(BW_SND_CMD_LOCK_CHANNEL, (1u << 8) | (1u << 14) | (1u << 0), 0, 0, 0);
    send1(BW_SND_CMD_SETUP_CHANNEL_PSG, 8, 127, 64ul | (1024ul << 8), 3);
    send1(BW_SND_CMD_START_TIMER, 1u << 8, 0, 0, 0);
    bw_snd_render(out, 32768);
    for (i = 1; i < 32768; i++)
        n_flip += (out[2 * i] > 0) != (out[2 * (i - 1)] > 0);
    CHECK_RANGE("psg: square frequency", n_flip, 4086, 4096);
    bw_arm7_step();
    BW_CHECK_EQ("psg: channel 8 busy", g_sw->channelStatus, 1u << 8);
    BW_CHECK_EQ("psg: SOUNDxCNT volume and pan",
                bw_snd_channel_cnt(8) & 0x007F007Fu, 127u | (64u << 16));
    send1(BW_SND_CMD_STOP_TIMER, 1u << 8, 0, 0, 0);

    /* Noise on 14: both levels. */
    send1(BW_SND_CMD_SETUP_CHANNEL_NOISE, 14, 127, 64ul | (64ul << 8), 0);
    send1(BW_SND_CMD_START_TIMER, 1u << 14, 0, 0, 0);
    bw_snd_render(out, 4096);
    for (i = 0; i < 4096; i++) {
        n_pos += out[2 * i] > 0;
        n_neg += out[2 * i] < 0;
    }
    REQ("noise: high levels", n_pos > 500);
    REQ("noise: low levels", n_neg > 500);
    send1(BW_SND_CMD_STOP_TIMER, 1u << 14, 0, 0, 0);

    /* A PSG setup on channel 0 makes no sound: PSG is 8-13 only. */
    send1(BW_SND_CMD_SETUP_CHANNEL_PSG, 0, 127, 64ul | (1024ul << 8), 3);
    send1(BW_SND_CMD_START_TIMER, 1u << 0, 0, 0, 0);
    bw_snd_render(out, 1024);
    for (i = 0, n_flip = 0; i < 2048; i++)
        n_flip += out[i] != 0;
    BW_CHECK_EQ("psg: not on channel 0", n_flip, 0);

    /* CHANNEL_VOLUME and CHANNEL_PAN reach SOUNDxCNT. */
    send1(BW_SND_CMD_CHANNEL_VOLUME, 1u << 8, 40, 2, 0);
    send1(BW_SND_CMD_CHANNEL_PAN, 1u << 8, 10, 0, 0);
    send1(BW_SND_CMD_CHANNEL_TIMER, 1u << 8, 2048, 0, 0);
    BW_CHECK_EQ("channel volume, shift, pan",
                bw_snd_channel_cnt(8) & 0x007F037Fu, 40u | (2u << 8) | (10u << 16));
    BW_CHECK_EQ("channel timer", bw_snd_channel_timer(8), 2048);

    /* Capture 0 from the mixer, PCM16, one-shot, 64 words, clocked by
     * channel 1's timer, while the square plays. */
    fresh();
    cap = ds_mem(DS_CAPTURE, 256);
    memset(cap, 0, 256);
    send1(BW_SND_CMD_LOCK_CHANNEL, (1u << 8) | (1u << 1), 0, 0, 0);
    send1(BW_SND_CMD_SETUP_CHANNEL_PSG, 8, 127, 64ul | (1024ul << 8), 3);
    setup_pcm(1, 1, cap, DS_CAPTURE, 2, 0, 1, 512, 64);     /* only its timer */
    send1(BW_SND_CMD_SETUP_CAPTURE, (0ul << 31) | (0ul << 30) | (0ul << 29),
          ptr_arg(cap, DS_CAPTURE), 64, 0);
    send1(BW_SND_CMD_START_TIMER, 1u << 8, 1u << 0, 0, 0);
    run(32);
    BW_CHECK_EQ("capture: running", g_sw->captureStatus, 1);
    run(256);
    BW_CHECK_EQ("capture: one-shot done", g_sw->captureStatus, 0);
    for (i = 0, n_pos = 0, n_neg = 0; i < 128; i++) {
        s16 v = (s16)bw_rd16(cap + 2 * i);
        n_pos += v > 0;
        n_neg += v < 0;
    }
    REQ("capture: recorded the square's highs", n_pos > 20);
    REQ("capture: and its lows", n_neg > 20);

    /* A stream through a ring buffer and the alarm == the same samples in
     * one piece. */
    for (i = 0; i < BLK * 16; i++)
        sig[i] = (s16)(((i * 37u) % 2000u) * 16u - 16000u);
    diff = stream_vs_flat(sig, BLK * 16, BLK * 14, &peak);
    printf("  synthetic stream: %u frames, peak %d, %ld samples differ\n",
           BLK * 14, peak, diff);
    BW_CHECK_EQ("stream: ring + alarm == one piece", diff, 0);
    REQ("stream: sounds", peak > 1000);
}

static void test_driver_info(void)
{
    u8 *info;
    int c;
    u32 link;

    if (sizeof(void *) != 4) {
        printf("  (driver info: DS layout only with 4-byte pointers, skipped)\n");
        return;
    }
    fresh();
    start_seq(2, k_basic, sizeof(k_basic));
    send1(BW_SND_CMD_TRACK_PARAM, 2u | (2u << 24), 1u << 1, 0x0A,
          (unsigned long)(u16)(s16)-120);
    send1(BW_SND_CMD_LOCK_CHANNEL, 0x8000, 0, 0, 0);
    run_driver_frames(2);
    info = ds_mem(DS_INFO, 0x11E0);
    memset(info, 0xCC, 0x11E0);
    send1(BW_SND_CMD_READ_DRIVER_INFO, ptr_arg(info, DS_INFO), 0, 0, 0);
    /* work.player[2].tracks[0..1] are pool ids, then 0xFF. */
    BW_CHECK_EQ("info: player 2 track 0 id", info[0x540 + 2 * 0x24 + 8], 0);
    BW_CHECK_EQ("info: player 2 track 1 id", info[0x540 + 2 * 0x24 + 9], 1);
    BW_CHECK_EQ("info: player 2 track 2 none", info[0x540 + 2 * 0x24 + 10], 0xFF);
    BW_CHECK_EQ("info: player 0 no tracks", info[0x540 + 8], 0xFF);
    BW_CHECK_EQ("info: player 2 active flag", info[0x540 + 2 * 0x24] & 0x80, 0x80);
    BW_CHECK_EQ("info: track 1 extFader",
                (s16)bw_rd16(info + 0x780 + 1 * 0x40 + 0x0A), -120);
    BW_CHECK_EQ("info: track 0 volume", info[0x780 + 4], 127);
    BW_CHECK_EQ("info: locked channels", bw_rd32(info + 0x11C4), 0x8000);
    BW_CHECK_EQ("info: work address", bw_rd32(info + 0x11C0), (u32)(uintptr_t)info);
    /* Track 0's channel list names the channel playing its note. */
    link = bw_rd32(info + 0x780 + 0x3C);
    REQ("info: track 0 has a channel", link != 0);
    c = (int)((link - (u32)(uintptr_t)info) / 0x54);
    CHECK_RANGE("info: a channel slot", c, 0, 15);
    BW_CHECK_EQ("info: its myNo", info[(size_t)c * 0x54], c);
    BW_CHECK_EQ("info: its key", info[(size_t)c * 0x54 + 8], 60);
    BW_CHECK_EQ("info: chCtrl busy", bw_rd32(info + 0x1180 + 4 * (u32)c) >> 31, 1);
}

/* ------------------------------------------------------------------ */
/* Real data                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    u8 *seq_file, *bank;
    u32 seq_size, bank_size;
    u32 ds_seq, ds_bank;
} Loaded;

/* Copy SDAT file `file_id` into main RAM at `ds`; returns host and size. */
static u8 *load_file(const BwSdat *sd, u32 file_id, u32 ds, u32 *size)
{
    BwBlob b;
    u8 *h;
    if (bw_sdat_raw_file(sd, file_id, &b) != 0)
        return NULL;
    h = ds_mem(ds, (u32)b.size);
    memcpy(h, b.data, b.size);
    *size = (u32)b.size;
    return h;
}

/* The game's loader, roughly: the SSEQ, its SBNK and the bank's SWARs
 * into main RAM, then SND_AssignWaveArc for each slot. */
static int load_real(const BwSdat *sd, u32 seq, Loaded *l)
{
    BwSdatSeqRecord rec;
    BwSdatBankRecord brec;
    u32 at = DS_REAL, size, file_id;
    int i;

    memset(l, 0, sizeof(*l));
    if (bw_sdat_seq_record(sd, seq, &rec) != 0 || bw_sdat_bank_record(sd, rec.bank, &brec) != 0)
        return -1;
    l->ds_seq = at;
    l->seq_file = load_file(sd, rec.file_id, at, &l->seq_size);
    at += (l->seq_size + 0xFFFu) & ~0xFFFu;
    l->ds_bank = at;
    l->bank = load_file(sd, brec.file_id, at, &l->bank_size);
    at += (l->bank_size + 0xFFFu) & ~0xFFFu;
    if (l->seq_file == NULL || l->bank == NULL)
        return -1;
    for (i = 0; i < 4; i++) {
        u8 *arc;
        wr32(l->bank + 0x18 + 8 * i, 0);
        wr32(l->bank + 0x1C + 8 * i, 0);
        if (brec.wave_arc[i] == 0xFFFF || bw_sdat_wavarc_fileid(sd, brec.wave_arc[i], &file_id) != 0)
            continue;
        arc = load_file(sd, file_id, at, &size);
        if (arc == NULL)
            continue;
        wr32(l->bank + 0x18 + 8 * i, at);
        at += (size + 0xFFFu) & ~0xFFFu;
    }
    return 0;
}

static void send_real(int id, int player, const Loaded *l)
{
    list_begin();
    push(id, (unsigned long)player, ptr_arg(l->seq_file + 0x1C, l->ds_seq + 0x1C), 0,
         ptr_arg(l->bank, l->ds_bank));
    flush();
}

static s16 render_peak(u32 frames, u32 *channels_seen, int *max_busy)
{
    static s16 buf[1024 * 2];
    u32 done, k;
    s16 peak = 0;
    for (done = 0; done < frames; done += 1024) {
        bw_snd_render(buf, 1024);
        bw_arm7_step();
        if (channels_seen)
            *channels_seen |= g_sw->channelStatus;
        if (max_busy && popcount16(g_sw->channelStatus) > *max_busy)
            *max_busy = popcount16(g_sw->channelStatus);
        for (k = 0; k < 2048; k++)
            if (buf[k] > peak)
                peak = buf[k];
    }
    return peak;
}

static void test_real(const char *data_root)
{
    char path[1024];
    BwSdat sd;
    Loaded l;
    long seq;
    BwSndPlayerState ps;
    BwSndStats st;
    s16 peak;
    u32 seen = 0, waited;
    int max_busy = 0;

    sprintf(path, "%s/wb_sound_data.sdat", data_root);
    if (bw_sdat_load(&sd, path) != 0) {
        printf("  FAIL cannot load %s\n", path);
        bw_failures++;
        return;
    }
    printf("  SDAT: %u STRM entries\n", bw_sdat_count(&sd, BW_SDAT_STRM));

    /* 1. A one-track sound effect: it sounds and ends. */
    seq = bw_sdat_find(&sd, BW_SDAT_SEQ, "SEQ_SE_DECIDE2");
    BW_CHECK_EQ("real: SEQ_SE_DECIDE2 is SEQ 1357", seq, 1357);
    if (seq >= 0) {
        boot();
        bind_shared();
        if (load_real(&sd, (u32)seq, &l) == 0) {
            send_real(BW_SND_CMD_START_SEQ, 1, &l);
            BW_CHECK_EQ("real: SE playing", g_sw->playerStatus, 2);
            peak = render_peak(32768, &seen, NULL);
            for (waited = 0; waited < 32768 * 4 && g_sw->playerStatus != 0; waited += 1024)
                run(1024);
            printf("  SEQ_SE_DECIDE2: peak %d, channels 0x%04X, ended after %.2f s\n",
                   peak, seen, (32768 + waited) / 32768.0);
            REQ("real: SE sounds", peak > 1000);
            BW_CHECK_EQ("real: SE ended", g_sw->playerStatus, 0);
            run(FRAME_OUT * 8);
            BW_CHECK_EQ("real: SE channels free", g_sw->channelStatus, 0);
            bw_snd_stats(&st);
            BW_CHECK_EQ("real: SE no bad address", st.bad_address, 0);
        } else {
            BW_CHECK_EQ("real: SE loaded", 0, 1);
        }
    }

    /* 2. A multi-track looping BGM: it sounds within 16 channels, and
     * fast-forwarded far past its length it is still playing, from its
     * loop point. */
    seq = bw_sdat_find(&sd, BW_SDAT_SEQ, "SEQ_BGM_GYM");
    if (seq >= 0) {
        boot();
        bind_shared();
        if (load_real(&sd, (u32)seq, &l) == 0) {
            send_real(BW_SND_CMD_START_SEQ, 0, &l);
            seen = 0;
            peak = render_peak(32768 * 10, &seen, &max_busy);
            bw_snd_player_state(0, &ps);
            bw_snd_stats(&st);
            printf("  SEQ_BGM_GYM: 10 s, peak %d, %d channels at once, %u notes "
                   "dropped, %llu ticks\n", peak, max_busy, st.notes_dropped,
                   (unsigned long long)ps.tick);
            REQ("real: BGM sounds", peak > 1000);
            BW_CHECK_EQ("real: BGM playing", ps.active, 1);
            CHECK_RANGE("real: BGM channels", max_busy, 4, 16);

            send_real(BW_SND_CMD_PREPARE_SEQ, 0, &l);
            send1(BW_SND_CMD_SKIP_SEQ, 0, 48 * 4 * 400, 0, 0);
            bw_snd_player_state(0, &ps);
            BW_CHECK_EQ("real: BGM past 400 bars still prepared", ps.prepared, 1);
            BW_CHECK_EQ("real: BGM jumped back to its loop", ps.loops, 1);
            send1(BW_SND_CMD_START_PREPARED_SEQ, 0, 0, 0, 0);
            peak = render_peak(32768 * 2, NULL, NULL);
            REQ("real: BGM sounds from inside its loop", peak > 1000);
            bw_snd_stats(&st);
            BW_CHECK_EQ("real: BGM no bad address", st.bad_address, 0);
        } else {
            BW_CHECK_EQ("real: BGM loaded", 0, 1);
        }

        /* 3. One of GYM's own waves, streamed. */
        {
            BwSdatSeqRecord rec;
            BwSdatBankRecord brec;
            u32 file_id;
            BwBlob blob;
            BwSwar swar;
            BwSwav w;
            u32 idx, n;
            s16 *pcm = NULL;
            long diff;

            if (bw_sdat_seq_record(&sd, (u32)seq, &rec) == 0
                && bw_sdat_bank_record(&sd, rec.bank, &brec) == 0
                && bw_sdat_wavarc_fileid(&sd, brec.wave_arc[0], &file_id) == 0
                && bw_sdat_raw_file(&sd, file_id, &blob) == 0
                && bw_swar_open(&swar, blob) == 0) {
                /* The longest wave in the first archive. */
                u32 best = 0;
                size_t best_n = 0;
                for (idx = 0; idx < swar.wave_count; idx++)
                    if (bw_swar_wave(&swar, idx, &w) == 0
                        && bw_swav_decoded_count(&w) > best_n) {
                        best_n = bw_swav_decoded_count(&w);
                        best = idx;
                    }
                if (bw_swar_wave(&swar, best, &w) == 0) {
                    n = (u32)bw_swav_decoded_count(&w);
                    if (n > BLK * 64)
                        n = BLK * 64;
                    n -= n % BLK;
                    pcm = (s16 *)malloc((size_t)bw_swav_decoded_count(&w) * sizeof(s16));
                    if (pcm != NULL && n >= BLK * 6) {
                        bw_swav_decode(&w, pcm, bw_swav_decoded_count(&w));
                        diff = stream_vs_flat(pcm, n, n - BLK * 2, &peak);
                        printf("  GYM wave %u streamed: %u samples, peak %d, %ld "
                               "samples differ from one piece\n", best, n, peak, diff);
                        BW_CHECK_EQ("real: streamed wave == one piece", diff, 0);
                        REQ("real: streamed wave sounds", peak > 100);
                    } else {
                        BW_CHECK_EQ("real: a wave long enough to stream", 0, 1);
                    }
                    free(pcm);
                }
            } else {
                BW_CHECK_EQ("real: GYM's first wave archive", 0, 1);
            }
        }
    }
    bw_sdat_close(&sd);
}

int main(int argc, char **argv)
{
    test_tables();
    test_wire();
    test_sequence();
    test_control_flow();
    test_tempo();
    test_channels();
    test_pitch();
    test_levels();
    test_pause_invalidate();
    test_alarms();
    test_direct();
    test_driver_info();
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        bw_set_data_root(argv[1]);
        printf("data root: %s\n", bw_data_root());
        test_real(bw_data_root());
    } else {
        printf("  (no data root: the real-data checks are skipped)\n");
    }
    BW_REPORT("test_snd");
}
