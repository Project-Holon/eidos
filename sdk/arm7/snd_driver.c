/*
 * sdk/arm7/snd_driver.c -- the ARM7's sound driver, the part that plays:
 * SND commands executed on the sequencer (sdk/arm7/snd_seq.c) and the sound
 * hardware model (sdk/arm7/snd_hw.c), and the shared work the ARM9 polls.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  sdk/arm7/snd.c hands this file one
 * command at a time (sdk/arm7/snd.h).  What each command's arguments are is
 * read off the ARM9 senders: the matched ones (src/matched/arm9_sdk/,
 * snd_interface.o) and, for the senders this game does not link, the
 * listings of the same object in libsnd.TWL.HYB.a through aletheia's
 * tools/sdkobj.py.  Nothing comes from ARM7 code.
 *
 *   START_SEQ / PREPARE_SEQ (player, seqBase, seqOffset, bank)  func_02089350.c
 *   START_PREPARED_SEQ (player), STOP_SEQ (player)              func_02089330.c
 *   PAUSE_SEQ (player, flag)                                    func_02089398.c
 *   SKIP_SEQ (player, ticks)                                    enum only
 *   PLAYER_PARAM (player, offset, data, size)                   func_020897B4.c
 *   TRACK_PARAM (player | size << 24, tracks, offset, data)     func_020897DC.c
 *   MUTE_TRACK (player, tracks, mode)                           func_02089580.c
 *   ALLOCATABLE_CHANNEL (player, tracks, channels)              func_0208948C.c
 *   PLAYER_LOCAL_VAR (player, var, value)            SND_SetPlayerLocalVariable
 *   PLAYER_GLOBAL_VAR (var, value)                  SND_SetPlayerGlobalVariable
 *   START_TIMER / STOP_TIMER (channels, captures, alarms, flags)
 *                                                 func_020894B4.c / func_020894DC.c
 *   SETUP_CHANNEL_PCM (ch | timer << 16, data, volume << 24 | shift << 22 |
 *                      loopLen, loop << 26 | format << 24 | pan << 16 |
 *                      loopStart)                               func_02089660.c
 *   SETUP_CHANNEL_PSG (ch, volume | shift << 8, pan | timer << 8, duty)
 *   SETUP_CHANNEL_NOISE (ch, volume | shift << 8, pan | timer << 8, 0)
 *   SETUP_CAPTURE (capture << 31 | format << 30 | loop << 29 | in << 28 |
 *                  out << 27, buffer, length, 0)                SND_SetupCapture
 *   SETUP_ALARM (alarmNo, tick, period, id)                     func_02089540.c
 *   CHANNEL_TIMER (channels, timer)                             func_02089618.c
 *   CHANNEL_VOLUME (channels, volume, shift)                    func_02089638.c
 *   CHANNEL_PAN (channels, pan)                                 SND_SetChannelPan
 *   SURROUND_DECAY (decay)                                      enum only
 *   MASTER_VOLUME (volume)                                      func_0208970C.c
 *   MASTER_PAN (pan, -1 to reset)                     func_02089754.c / 02089774.c
 *   OUTPUT_SELECTOR (left, right, channel1, channel3)           func_0208972C.c
 *   LOCK_CHANNEL / UNLOCK_CHANNEL (channels, flags)             func_020895D8.c / F8
 *   STOP_UNLOCKED_CHANNEL (channels, flags)               SND_StopUnlockedChannel
 *   SHARED_WORK (SNDSharedWork *)                               SND_CommandInit
 *   INVALIDATE_SEQ/BANK/WAVE (start, end)                       func_020896AC.c
 *   READ_DRIVER_INFO (SNDDriverInfo *)                          func_02089794.c
 *
 * SKIP_SEQ and SURROUND_DECAY have no sender in the library's ARM9 objects
 * that shows their arguments; the shapes above are the natural reading of
 * the command names and are labelled as such.
 *
 * ------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------
 * The driver's clock is the output: bw_snd_render() advances it by the
 * frames it mixes, at BW_AUDIO_RATE.  Every 64 * 2728 bus cycles of that
 * clock (BW_SNDSEQ_FRAME_CYCLES, counted exactly in a cycle accumulator)
 * the sequencer runs one frame, which is when notes start, envelopes move
 * and channel registers change; between frames the hardware model mixes.
 * An alarm's tick and period are OS ticks, the bus clock / 64
 * (sdk/os_tick.c), converted exactly from frames; a firing is the word
 * alarmNo | id << 8 on tag 7 (func_0208A010.c), sent at the next step on
 * the game thread.  START_TIMER starts its channels, captures and alarms
 * together, which is how a stream's buffer and its refill alarm stay in step.
 *
 * ------------------------------------------------------------------------
 * The shared work
 * ------------------------------------------------------------------------
 * SNDSharedWork is the matched code's own declaration (func_0208A1D0.c).
 * The driver keeps playerStatus, channelStatus (the hardware channels
 * busy: notes and direct channels alike, sixteen shared between every
 * player), captureStatus, each player's tickCounter and the sequence
 * variables, and writes them at bw_arm7_step and after every command list.
 *
 * ------------------------------------------------------------------------
 * Addresses in arguments
 * ------------------------------------------------------------------------
 * The matched senders store pointers as `(u32)pointer`, where their u32 is
 * unsigned long.  So an argument carries the whole host pointer wherever
 * unsigned long is at least as wide as a pointer: every ILP32 host (the
 * reference configuration) and LP64 Unix.  On LLP64 (64-bit Windows) the
 * cast drops the top half, so there an argument is read as a DS address,
 * which is what a port-side sender can supply.  How many bytes are readable
 * is known when the address lies in sdk/dsmem.h's space; otherwise the
 * driver reads what the file's own header says the game loaded (SSEQ, SBNK,
 * SWAR all carry their size at +8), and a direct channel what its registers
 * say.  bw_snd_set_resolver() replaces the whole rule.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arm7.h"
#include "audio.h"
#include "dsmem.h"
#include "plat_thread.h"
#include "pxi.h"
#include "snd.h"
#include "snd_hw.h"
#include "snd_seq.h"

#define PLAYERS BW_SNDSEQ_PLAYERS
#define ALARMS  8

/* Firings held for the next step.  The ARM9 drains them at every step, a
 * few per frame for a stream; a burst beyond this is counted and dropped. */
#define ALARM_QUEUE 256

/* struct SNDDriverInfo (snd_work.o DWARF), DS layout.                     */
#define DI_CHCTRL       0x1180
#define DI_WORK_ADDRESS 0x11C0
#define DI_LOCKED       0x11C4
#define DI_SIZE         0x11E0
#define DI_ALARM        0x0F80

/* The most of a header-less sequence the driver will read.               */
#define SEQ_FALLBACK_MAX ((size_t)256 * 1024)

/* Bus cycles per BW_AUDIO_RATE frame is 33513982 / 32768; both sides of
 * the comparisons below are scaled by 32768 to stay in integers.          */
#define BUS_HZ          33513982u
#define FRAME_UNITS     ((u64)BW_SNDSEQ_FRAME_CYCLES * BW_AUDIO_RATE)

/* OS ticks per BW_AUDIO_RATE frame, as a ratio: (bus / 64) / 32768.       */
#define TICK_NUM        BUS_HZ
#define TICK_DEN        (64u * 32768u)

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

/* 640 bytes, the DWARF's size, wherever unsigned long is four bytes.     */
typedef char bw_shared_work_size[(sizeof(unsigned long) != 4 || sizeof(SharedWork) == 640) ? 1 : -1];

typedef struct {
    int armed;
    u8  id;
    u64 tick, period;   /* as SETUP_ALARM gave them, in OS ticks */
    u64 fire_at;        /* driver clock, OS ticks                */
} Alarm;

static struct {
    BwMutex      *mtx;
    Alarm         alarm[ALARMS];
    u32           fired[ALARM_QUEUE];
    u32           n_fired;
    u64           seq_units;    /* toward the next sequencer frame       */
    SharedWork   *shared;
    BwSndResolver resolver;
    BwSndStats    stats;
} g;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void lock(void)   { if (g.mtx) bw_mutex_lock(g.mtx); }
static void unlock(void) { if (g.mtx) bw_mutex_unlock(g.mtx); }

static int bit(u64 mask, int n) { return (int)((mask >> n) & 1u); }

/* ------------------------------------------------------------------ */
/* Addresses                                                           */
/* ------------------------------------------------------------------ */

/* How far host memory from `h` keeps mapping to consecutive DS addresses. */
static size_t ds_extent(const u8 *h, u32 ds)
{
    size_t lo = 1, hi = (size_t)1 << 24;
    uintptr_t base = (uintptr_t)h;

    if (bw_ds_addr_of((const void *)(base + hi - 1)) == ds + (u32)(hi - 1))
        return hi;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (bw_ds_addr_of((const void *)(base + mid - 1)) == ds + (u32)(mid - 1))
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static void *default_resolve(u64 value, size_t *avail)
{
    u8 *h;
    u32 ds;

    *avail = 0;
    if (value == 0)
        return NULL;
    /* A wire record always stores DS addresses, even when command arguments
     * can carry full native pointers on LP64. Resolve those addresses first. */
    h = value <= UINT32_MAX ? bw_ds_try_ptr((u32)value) : NULL;
    if (h == NULL) {
        if (sizeof(unsigned long) < sizeof(void *) || (u64)(uintptr_t)value != value)
            return NULL;
        h = (u8 *)(uintptr_t)value;
    }
    ds = bw_ds_addr_of(h);
    if (ds != 0)
        *avail = ds_extent(h, ds);
    return h;
}

void bw_snd_set_resolver(BwSndResolver r)
{
    g.resolver = r;
}

void *bw_snd_resolve(u64 value, size_t *avail)
{
    size_t dummy;
    if (avail == NULL)
        avail = &dummy;
    return g.resolver ? g.resolver(value, avail) : default_resolve(value, avail);
}

/* Whether the `n` bytes before `h` can be read: inside the DS space they
 * must map contiguously; outside it they are the game's own memory, where
 * seqBase sits 0x1C into the SSEQ file the game loaded. */
static int readable_before(const u8 *h, u32 n)
{
    u32 ds = bw_ds_addr_of(h);
    if (ds == 0)
        return 1;
    return ds >= n && bw_ds_addr_of((const void *)((uintptr_t)h - n)) == ds - n;
}

/* A file with the NitroSDK header (magic, ..., u32 size at +8).            */
static int file_blob(u64 value, const char *magic, size_t min_size, BwBlob *out)
{
    size_t avail;
    const u8 *h = (const u8 *)bw_snd_resolve(value, &avail);
    u32 size;

    out->data = NULL;
    out->size = 0;
    if (h == NULL)
        return -1;
    if (avail != 0 && avail < min_size)
        return -1;
    if (memcmp(h, magic, 4) != 0)
        return -1;
    size = bw_rd32(h + 8);
    if (size < min_size)
        return -1;
    if (avail != 0 && size > avail)
        size = (u32)avail;
    out->data = h;
    out->size = size;
    return 0;
}

/* The bytes a seqBase names: the SSEQ the game loaded, when its header is
 * just before (seqBase = file + 0x1C, the u32 at +0x18 being that 0x1C),
 * else as far as the address space says is mapped. */
static int seq_blob(u64 value, BwBlob *out)
{
    size_t avail;
    const u8 *h = (const u8 *)bw_snd_resolve(value, &avail);

    out->data = NULL;
    out->size = 0;
    if (h == NULL)
        return -1;
    if (readable_before(h, 0x1C)) {
        const u8 *f = h - 0x1C;
        if (memcmp(f, "SSEQ", 4) == 0 && bw_rd32(f + 0x18) == 0x1C
            && bw_rd32(f + 8) > 0x1C) {
            u32 size = bw_rd32(f + 8) - 0x1C;
            if (avail != 0 && size > avail)
                size = (u32)avail;
            out->data = h;
            out->size = size;
            return 0;
        }
    }
    /* No header (a sequence inside an archive, or bytes a port placed):
     * as far as the address space maps, capped, and committed, since a
     * translation is what commits main RAM (sdk/dsmem.c). */
    if (avail == 0)
        return -1;
    if (avail > SEQ_FALLBACK_MAX)
        avail = SEQ_FALLBACK_MAX;
    {
        u32 ds = bw_ds_addr_of(h), off;
        if (ds != 0)
            for (off = 0; off < (u32)avail; off += 0x8000u)
                (void)bw_ds_try_ptr(ds + off);
    }
    out->data = h;
    out->size = avail;
    return 0;
}

/* The bank argument: the SBNK and the SWARs its waveArcLink[] names
 * (SND_AssignWaveArc, func_0208A2BC.c, links them). */
static int bank_of(u64 value, BwSndSeqBank *bank)
{
    int i;
    memset(bank, 0, sizeof(*bank));
    if (value == 0)
        return 0;
    if (file_blob(value, "SBNK", 0x3C, &bank->sbnk) != 0)
        return -1;
    for (i = 0; i < 4; i++) {
        /* waveArcLink[i].waveArc, a DS-width pointer slot. */
        u32 arc = bw_rd32(bank->sbnk.data + 0x18 + 8 * i);
        if (arc != 0)
            (void)file_blob(arc, "SWAR", 0x3C, &bank->arc[i]);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sequences                                                           */
/* ------------------------------------------------------------------ */

static int player_no(u64 v)
{
    return v < PLAYERS ? (int)v : -1;
}

static void cmd_prepare(const BwSndCommand *c, int start)
{
    int n = player_no(c->arg[0]);
    BwBlob seq;
    BwSndSeqBank bank;

    if (n < 0)
        return;
    if (seq_blob(c->arg[1], &seq) != 0 || c->arg[2] >= seq.size
        || bank_of(c->arg[3], &bank) != 0) {
        g.stats.bad_address++;
        bw_sndseq_stop(n);
        return;
    }
    bw_sndseq_prepare(n, seq, (u32)c->arg[2], &bank);
    if (start)
        bw_sndseq_start(n);
}

static void cmd_invalidate(const BwSndCommand *c)
{
    uintptr_t lo, hi;
    u8 *h;
    int kind = (int)(c->id - BW_SND_CMD_INVALIDATE_SEQ), ch;

    if (c->arg[1] <= c->arg[0])
        return;
    h = (u8 *)bw_snd_resolve(c->arg[0], NULL);
    if (h == NULL) {
        g.stats.bad_address++;
        return;
    }
    lo = (uintptr_t)h;
    hi = lo + (uintptr_t)(c->arg[1] - c->arg[0]);
    bw_sndseq_invalidate(kind, lo, hi);
    /* A direct channel reading wave data from there stops too. */
    if (kind == 2)
        for (ch = 0; ch < BW_SNDHW_CHANNELS; ch++) {
            size_t n;
            const u8 *src = (const u8 *)bw_sndhw_source(ch, &n);
            if (src != NULL && (uintptr_t)src < hi && lo < (uintptr_t)src + n
                && bit(bw_sndseq_locked(), ch))
                bw_sndhw_stop(ch);
        }
}

/* ------------------------------------------------------------------ */
/* Direct channels and captures                                        */
/* ------------------------------------------------------------------ */

static void cmd_setup_pcm(const BwSndCommand *c)
{
    int ch = (int)(c->arg[0] & 0xFFFFu);
    u32 timer = (u32)(c->arg[0] >> 16) & 0xFFFFu;
    u32 a2 = (u32)c->arg[2], a3 = (u32)c->arg[3];
    int format = (int)((a3 >> 24) & 3u), loop = (int)((a3 >> 26) & 3u);
    u32 pnt = a3 & 0xFFFFu, len = a2 & 0x3FFFFFu;
    size_t avail;
    const void *data = bw_snd_resolve(c->arg[1], &avail);

    if (ch >= BW_SNDHW_CHANNELS)
        return;
    if (data == NULL)
        g.stats.bad_address++;
    bw_sndhw_setup_pcm(ch, format, data, avail, loop, pnt, len,
                       (int)((a2 >> 24) & 0x7Fu), (int)((a2 >> 22) & 3u), timer,
                       (int)((a3 >> 16) & 0x7Fu));
}

static void cmd_setup_capture(const BwSndCommand *c)
{
    u32 a0 = (u32)c->arg[0];
    size_t avail;
    void *dst = bw_snd_resolve(c->arg[1], &avail);
    int cap = (int)(a0 >> 31);

    if (dst == NULL)
        g.stats.bad_address++;
    bw_sndhw_setup_capture(cap, (int)((a0 >> 30) & 1u), dst, avail, (u32)c->arg[2],
                           (int)((a0 >> 29) & 1u), (int)((a0 >> 28) & 1u),
                           (int)((a0 >> 27) & 1u));
}

static u64 now_ticks(void)
{
    return g.stats.frames * TICK_NUM / TICK_DEN;
}

static void cmd_timer(const BwSndCommand *c, int start)
{
    u32 mask = (u32)c->arg[2] & 0xFFu;
    int i;

    for (i = 0; i < BW_SNDHW_CHANNELS; i++)
        if (bit(c->arg[0], i)) {
            if (start)
                bw_sndhw_start(i);
            else
                bw_sndhw_stop(i);
        }
    for (i = 0; i < BW_SNDHW_CAPTURES; i++)
        if (bit(c->arg[1], i)) {
            if (start)
                bw_sndhw_capture_start(i);
            else
                bw_sndhw_capture_stop(i);
        }
    for (i = 0; i < ALARMS; i++) {
        if (!((mask >> i) & 1u))
            continue;
        g.alarm[i].armed = start;
        if (start)
            g.alarm[i].fire_at = now_ticks() + g.alarm[i].tick;
    }
}

static void alarms_run(void)
{
    u64 now = now_ticks();
    int i;

    for (i = 0; i < ALARMS; i++) {
        Alarm *a = &g.alarm[i];
        while (a->armed && a->fire_at <= now) {
            if (g.n_fired < ALARM_QUEUE)
                g.fired[g.n_fired++] = (u32)i | ((u32)a->id << 8);
            else
                g.stats.alarms_dropped++;
            g.stats.alarms_fired++;
            if (a->period == 0)
                a->armed = 0;
            else
                a->fire_at += a->period;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Driver info                                                         */
/* ------------------------------------------------------------------ */

static void cmd_driver_info(const BwSndCommand *c)
{
    size_t avail;
    u8 *info = (u8 *)bw_snd_resolve(c->arg[0], &avail);
    u32 addr;
    int i;

    /* SNDDriverInfo holds pointers, so its layout is the DS's only where
     * pointers are four bytes. */
    if (info == NULL || sizeof(void *) != 4 || (avail != 0 && avail < DI_SIZE)) {
        g.stats.bad_address++;
        return;
    }
    memset(info, 0, DI_SIZE);
    addr = (u32)(uintptr_t)info;
    bw_sndseq_write_work(info, addr);
    for (i = 0; i < ALARMS; i++) {
        u8 *a = info + DI_ALARM + (size_t)i * 0x40;
        a[0] = (u8)g.alarm[i].armed;
        a[1] = g.alarm[i].id;
    }
    for (i = 0; i < BW_SNDHW_CHANNELS; i++) {
        u32 v = bw_sndhw_cnt(i);
        u8 *p = info + DI_CHCTRL + (size_t)i * 4;
        p[0] = (u8)v;
        p[1] = (u8)(v >> 8);
        p[2] = (u8)(v >> 16);
        p[3] = (u8)(v >> 24);
    }
    info[DI_WORK_ADDRESS] = (u8)addr;
    info[DI_WORK_ADDRESS + 1] = (u8)(addr >> 8);
    info[DI_WORK_ADDRESS + 2] = (u8)(addr >> 16);
    info[DI_WORK_ADDRESS + 3] = (u8)(addr >> 24);
    {
        u32 l = bw_sndseq_locked();
        info[DI_LOCKED] = (u8)l;
        info[DI_LOCKED + 1] = (u8)(l >> 8);
    }
}

/* ------------------------------------------------------------------ */
/* The driver hooks                                                    */
/* ------------------------------------------------------------------ */

static void drv_reset(void)
{
    lock();
    bw_sndhw_reset();
    bw_sndseq_reset();
    memset(g.alarm, 0, sizeof(g.alarm));
    g.n_fired = 0;
    g.seq_units = FRAME_UNITS;     /* the first frame runs at once */
    g.shared = NULL;
    memset(&g.stats, 0, sizeof(g.stats));
    unlock();
}

static void execute(const BwSndCommand *c)
{
    int i;
    switch (c->id) {
    case BW_SND_CMD_START_SEQ:
        cmd_prepare(c, 1);
        break;
    case BW_SND_CMD_PREPARE_SEQ:
        cmd_prepare(c, 0);
        break;
    case BW_SND_CMD_START_PREPARED_SEQ:
        bw_sndseq_start(player_no(c->arg[0]));
        break;
    case BW_SND_CMD_STOP_SEQ:
        bw_sndseq_stop(player_no(c->arg[0]));
        break;
    case BW_SND_CMD_PAUSE_SEQ:
        bw_sndseq_pause(player_no(c->arg[0]), c->arg[1] != 0);
        break;
    case BW_SND_CMD_SKIP_SEQ:
        bw_sndseq_skip(player_no(c->arg[0]), (u32)c->arg[1]);
        break;
    case BW_SND_CMD_PLAYER_PARAM:
        bw_sndseq_player_param(player_no(c->arg[0]), (u32)c->arg[1], (u32)c->arg[2],
                               (u32)c->arg[3]);
        break;
    case BW_SND_CMD_TRACK_PARAM:
        bw_sndseq_track_param(player_no(c->arg[0] & 0xFFFFFFu), (u32)c->arg[1],
                              (u32)c->arg[2], (u32)c->arg[3],
                              (u32)(c->arg[0] >> 24) & 0xFFu);
        break;
    case BW_SND_CMD_MUTE_TRACK:
        bw_sndseq_mute(player_no(c->arg[0]), (u32)c->arg[1], (int)c->arg[2]);
        break;
    case BW_SND_CMD_ALLOCATABLE_CHANNEL:
        bw_sndseq_allocatable(player_no(c->arg[0]), (u32)c->arg[1], (u32)c->arg[2]);
        break;
    case BW_SND_CMD_PLAYER_LOCAL_VAR:
        if (c->arg[1] < 16)
            bw_sndseq_set_var(player_no(c->arg[0]), (int)c->arg[1], (s16)(u16)c->arg[2]);
        break;
    case BW_SND_CMD_PLAYER_GLOBAL_VAR:
        if (c->arg[0] < 16)
            bw_sndseq_set_var(0, 16 + (int)c->arg[0], (s16)(u16)c->arg[1]);
        break;
    case BW_SND_CMD_START_TIMER:
        cmd_timer(c, 1);
        break;
    case BW_SND_CMD_STOP_TIMER:
        cmd_timer(c, 0);
        break;
    case BW_SND_CMD_SETUP_CHANNEL_PCM:
        cmd_setup_pcm(c);
        break;
    case BW_SND_CMD_SETUP_CHANNEL_PSG:
        bw_sndhw_setup_psg((int)c->arg[0], (int)c->arg[3], (int)(c->arg[1] & 0x7Fu),
                           (int)((c->arg[1] >> 8) & 3u), (u32)(c->arg[2] >> 8) & 0xFFFFu,
                           (int)(c->arg[2] & 0x7Fu));
        break;
    case BW_SND_CMD_SETUP_CHANNEL_NOISE:
        bw_sndhw_setup_noise((int)c->arg[0], (int)(c->arg[1] & 0x7Fu),
                             (int)((c->arg[1] >> 8) & 3u), (u32)(c->arg[2] >> 8) & 0xFFFFu,
                             (int)(c->arg[2] & 0x7Fu));
        break;
    case BW_SND_CMD_SETUP_CAPTURE:
        cmd_setup_capture(c);
        break;
    case BW_SND_CMD_SETUP_ALARM: {
        u64 n = c->arg[0];
        if (n < ALARMS) {
            g.alarm[n].armed = 0;
            g.alarm[n].tick = (u32)c->arg[1];
            g.alarm[n].period = (u32)c->arg[2];
            g.alarm[n].id = (u8)c->arg[3];
        }
        break;
    }
    case BW_SND_CMD_CHANNEL_TIMER:
        for (i = 0; i < BW_SNDHW_CHANNELS; i++)
            if (bit(c->arg[0], i))
                bw_sndhw_set_timer(i, (u32)c->arg[1] & 0xFFFFu);
        break;
    case BW_SND_CMD_CHANNEL_VOLUME:
        for (i = 0; i < BW_SNDHW_CHANNELS; i++)
            if (bit(c->arg[0], i))
                bw_sndhw_set_volume(i, (int)(c->arg[1] & 0x7Fu), (int)(c->arg[2] & 3u));
        break;
    case BW_SND_CMD_CHANNEL_PAN:
        for (i = 0; i < BW_SNDHW_CHANNELS; i++)
            if (bit(c->arg[0], i))
                bw_sndhw_set_pan(i, (int)(c->arg[1] & 0x7Fu));
        break;
    case BW_SND_CMD_SURROUND_DECAY:
        bw_sndhw_set_surround_decay((int)(c->arg[0] & 0x7Fu));
        break;
    case BW_SND_CMD_MASTER_VOLUME:
        bw_sndhw_set_master_volume(c->arg[0] > 127 ? 127 : (int)c->arg[0]);
        break;
    case BW_SND_CMD_MASTER_PAN:
        bw_sndhw_set_master_pan((s32)(u32)c->arg[0]);
        break;
    case BW_SND_CMD_OUTPUT_SELECTOR:
        bw_sndhw_set_output((int)c->arg[0], (int)c->arg[1], c->arg[2] != 0, c->arg[3] != 0);
        break;
    case BW_SND_CMD_LOCK_CHANNEL:
        bw_sndseq_lock((u32)c->arg[0] & 0xFFFFu);
        break;
    case BW_SND_CMD_UNLOCK_CHANNEL:
        bw_sndseq_unlock((u32)c->arg[0] & 0xFFFFu);
        break;
    case BW_SND_CMD_STOP_UNLOCKED_CHANNEL:
        bw_sndseq_stop_unlocked((u32)c->arg[0] & 0xFFFFu);
        break;
    case BW_SND_CMD_SHARED_WORK: {
        size_t avail;
        void *w = bw_snd_resolve(c->arg[0], &avail);
        if (w == NULL || (avail != 0 && avail < sizeof(SharedWork)))
            g.stats.bad_address++;
        else
            g.shared = (SharedWork *)w;
        break;
    }
    case BW_SND_CMD_INVALIDATE_SEQ:
    case BW_SND_CMD_INVALIDATE_BANK:
    case BW_SND_CMD_INVALIDATE_WAVE:
        cmd_invalidate(c);
        break;
    case BW_SND_CMD_READ_DRIVER_INFO:
        cmd_driver_info(c);
        break;
    default:
        g.stats.ignored++;
        break;
    }
}

static void drv_command(const BwSndCommand *c)
{
    lock();
    g.stats.commands++;
    if (c->id < BW_SND_CMD_COUNT)
        g.stats.by_id[c->id]++;
    execute(c);
    unlock();
}

/* Write what the ARM9 polls and send fired alarms.  Game thread only. */
static void publish(void)
{
    u32 status, channels = 0, captures = 0, fired[ALARM_QUEUE], n_fired, i, v;
    u32 ticks[PLAYERS];
    s16 vars[PLAYERS][16], globals[16];
    SharedWork *sw;

    lock();
    status = bw_sndseq_status();
    for (i = 0; i < BW_SNDHW_CHANNELS; i++)
        if (bw_sndhw_busy((int)i))
            channels |= 1u << i;
    for (i = 0; i < BW_SNDHW_CAPTURES; i++)
        if (bw_sndhw_capture_busy((int)i))
            captures |= 1u << i;
    for (i = 0; i < PLAYERS; i++) {
        BwSndSeqPlayerInfo pi;
        bw_sndseq_player_info((int)i, &pi);
        ticks[i] = (u32)pi.tick;
        for (v = 0; v < 16; v++)
            vars[i][v] = bw_sndseq_var((int)i, (int)v);
    }
    for (v = 0; v < 16; v++)
        globals[v] = bw_sndseq_var(0, 16 + (int)v);
    n_fired = g.n_fired;
    memcpy(fired, g.fired, n_fired * sizeof(u32));
    g.n_fired = 0;
    sw = g.shared;
    unlock();

    if (sw != NULL) {
        sw->playerStatus = status;
        sw->channelStatus = (unsigned short)channels;
        sw->captureStatus = (unsigned short)captures;
        for (i = 0; i < PLAYERS; i++) {
            sw->player[i].tickCounter = ticks[i];
            for (v = 0; v < 16; v++)
                sw->player[i].variable[v] = vars[i][v];
        }
        for (v = 0; v < 16; v++)
            sw->globalVariable[v] = globals[v];
    }
    for (i = 0; i < n_fired; i++)
        (void)bw_pxi_arm7_send(BW_PXI_TAG_SOUND, fired[i], 0);
}

static void drv_list_done(u32 lists)
{
    publish();
    if (g.shared != NULL)
        g.shared->finishCommandTag = lists;
}

static void drv_step(void)
{
    publish();
}

static const BwArm7SndDriver g_ops = {
    drv_reset, drv_command, drv_list_done, drv_step
};

const BwArm7SndDriver *bw_snd_driver_ops(void)
{
    return &g_ops;
}

void bw_snd_driver_attach(void)
{
    if (g.mtx == NULL) {
        g.mtx = bw_mutex_create();
        bw_sndhw_reset();
        bw_sndseq_reset();
        g.seq_units = FRAME_UNITS;
    }
    bw_arm7_snd_set_driver(&g_ops);
}

void bw_snd_bind_shared(void *shared_work)
{
    lock();
    g.shared = (SharedWork *)shared_work;
    unlock();
}

/* ------------------------------------------------------------------ */
/* The mix                                                             */
/* ------------------------------------------------------------------ */

void bw_snd_render(s16 *out, u32 frames)
{
    u32 done = 0;

    lock();
    while (done < frames) {
        u64 need;
        u32 n;
        if (g.seq_units >= FRAME_UNITS) {
            g.seq_units -= FRAME_UNITS;
            bw_sndseq_frame();
        }
        /* Frames until the next sequencer frame is due. */
        need = (FRAME_UNITS - g.seq_units + BUS_HZ - 1) / BUS_HZ;
        n = frames - done;
        if ((u64)n > need)
            n = (u32)need;
        bw_sndhw_mix(out != NULL ? out + (size_t)done * 2 : NULL, n);
        g.seq_units += (u64)n * BUS_HZ;
        g.stats.frames += n;
        alarms_run();
        done += n;
    }
    unlock();
}

/* ------------------------------------------------------------------ */
/* Inspection                                                          */
/* ------------------------------------------------------------------ */

void bw_snd_stats(BwSndStats *out)
{
    lock();
    *out = g.stats;
    out->notes_dropped = bw_sndseq_dropped_notes();
    unlock();
}

int bw_snd_player_state(int player, BwSndPlayerState *out)
{
    BwSndSeqPlayerInfo pi;
    if (player < 0 || player >= PLAYERS)
        return -1;
    lock();
    bw_sndseq_player_info(player, &pi);
    unlock();
    out->active = pi.active;
    out->prepared = pi.prepared;
    out->paused = pi.paused;
    out->loops = pi.loops;
    out->tick = pi.tick;
    out->player_fader = (s16)bw_rd16(pi.reg + BW_SNDPL_EXT_FADER);
    out->tempo = bw_rd16(pi.reg + BW_SNDPL_TEMPO);
    out->tempo_ratio = bw_rd16(pi.reg + BW_SNDPL_TEMPO_RATIO);
    out->volume = pi.reg[BW_SNDPL_VOLUME];
    out->prio = pi.reg[BW_SNDPL_PRIO];
    out->tracks = pi.track_mask;
    out->channels = pi.channel_mask;
    return 0;
}

int bw_snd_master_volume(void)
{
    int v;
    lock();
    v = bw_sndhw_master_volume();
    unlock();
    return v;
}

int bw_snd_master_pan(void)
{
    int v;
    lock();
    v = bw_sndhw_master_pan();
    unlock();
    return v;
}

u32 bw_snd_locked_channels(void)
{
    u32 v;
    lock();
    v = bw_sndseq_locked();
    unlock();
    return v;
}

u32 bw_snd_channel_cnt(int ch)
{
    u32 v;
    lock();
    v = bw_sndhw_cnt(ch);
    unlock();
    return v;
}

u32 bw_snd_channel_timer(int ch)
{
    u32 v;
    lock();
    v = bw_sndhw_timer(ch);
    unlock();
    return v;
}
