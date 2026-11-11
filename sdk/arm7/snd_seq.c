/*
 * sdk/arm7/snd_seq.c -- the ARM7 driver's sequencer (sdk/arm7/snd_seq.h).
 *
 * Sources, in the order they decide things:
 *
 *   1. The SDK's ARM9 objects, read with aletheia's tools/sdkobj.py: the
 *      DWARF of snd_work.o (SNDWork, SNDPlayer, SNDTrack, SNDExChannel,
 *      SNDLfo/SNDLfoParam, SNDWaveParam, SNDInstData/SNDInstParam,
 *      SNDKeySplit, SNDDrumSet, the SND_SEQ_MUTE_* and SNDEnvStatus enums),
 *      the listing of SND_CalcTimer and the tables of snd_util.o, and
 *      SND_ReadChannelInfo's reading of SNDExChannel.volume (volume | shift
 *      << 8, the SND_CalcChannelVolume result) and .pan.  The tables are
 *      generated here from their formulas, which reproduce the SDK's
 *      exactly (tests/test_snd.c holds them to sums of the SDK's tables):
 *        PitchTable[i]        = round(65536 * (2^(i / 768) - 1)), i < 768
 *        DecibelSquareTable[i] = round(400 * log10(i / 127)) floored at
 *                               -722, [0] = -32768
 *        VolumeTable[i]       = round(128 * m * 10^((i - 723) / 200)),
 *                               at most 127, m = 1, 2, 4, 16 over the four
 *                               shift bands of SND_CalcChannelVolume; the
 *                               three entries 602, 662 and 722 (the step
 *                               below each band top) are 126 in the SDK's
 *                               table where the formula gives 127.
 *   2. Public NDS sound driver documentation (the Nitro Composer SDAT/SSEQ
 *      and SBNK format documentation): the SSEQ command set and argument
 *      shapes, the prefix commands (0xA0 random, 0xA1 variable, 0xA2 if),
 *      the variable commands 0xB0-0xBD, a driver frame of 64 * 2728 bus
 *      cycles in which a player's tempo counter gains tempo * ratio / 256
 *      and spends 240 per tick, the SBNK envelope byte conversions (attack
 *      below 109 is 255 - attack, above it a 19-entry table; decay and
 *      release 127 -> 0xFFFF, 126 -> 0x3C00, below 50 -> 2v + 1, else
 *      0x1E00 / (126 - v); sustain is DecibelSquareTable[s] << 7), the
 *      envelope in 1/128 dB from -92544 (attack multiplies by rate / 256,
 *      decay and release subtract their rate), the channel level as the sum
 *      of decibels (velocity, envelope, track and player volumes and
 *      faders), the PSG base timer, and the LFO as a 128-step sine cycle.
 *   3. Where neither says, a stated choice, marked "Choice:" below.
 *
 * Nothing here is taken from ARM7 code or from any other decompilation,
 * port or emulator.
 */

#include <math.h>
#include <string.h>

#include "snd_hw.h"
#include "snd_seq.h"

#define PLAYERS   BW_SNDSEQ_PLAYERS
#define TRACKS    BW_SNDSEQ_TRACKS
#define CHANNELS  BW_SNDSEQ_CHANNELS

#define ENV_FLOOR      (-92544)     /* -723 dB/10 in 1/128 units         */
#define TICK_COST      240
#define MAX_COMMANDS_PER_TICK 4096  /* a track that never waits ends     */
#define CALL_DEPTH     3            /* SNDTrack.call_stack[3]            */

/* The PSG and noise base timer: the period of key 60 (261.63 Hz) at eight
 * timer steps per cycle, 33513982 / 2 / (8 * 261.63), as the PSG regions'
 * own original key is relative to. */
#define PSG_BASE_TIMER 8006

enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
enum { CH_PCM, CH_PSG, CH_NOISE };
enum { LFO_PITCH, LFO_VOLUME, LFO_PAN };

typedef struct {
    int active, start, auto_sweep;
    int type;
    int env_status;
    s32 env;
    int attack, sustain, decay, release;    /* rates, sustain raw byte  */
    int key, original_key, velocity;
    int prio;
    int init_pan, user_pan;
    int user_decay, user_decay2, user_pitch;
    s32 sweep_counter, sweep_length;
    int sweep_pitch;
    int lfo_target, lfo_speed, lfo_depth, lfo_range;
    u32 lfo_delay, lfo_delay_counter, lfo_counter;
    s32 length;                 /* ticks; -1 tie; 0 no gate              */
    /* wave */
    int format, loop;
    u32 wave_timer, pnt, len;
    const u8 *data;
    size_t avail;
    int duty;
    int looping_source;         /* never ends by itself                  */
    /* ownership */
    int track;                  /* pool index, -1 none                   */
    int next;                   /* next channel of the track, -1 none    */
    /* last hardware values, for SNDExChannel.volume/.timer/.pan          */
    u16 hw_volume, hw_timer;
    int hw_pan;
} ExChan;

typedef struct {
    int used, active;
    int note_wait, mute, tie, porta, cmp, channel_mask_flag;
    u8  r[BW_SNDTR_PARAMS];
    s32 wait;
    int player, no;
    u32 cur;
    u32 call_stack[CALL_DEPTH];
    u8  loop_count[CALL_DEPTH];
    int depth;
    int ch_list;                /* first channel, -1 none                */
} Track;

typedef struct {
    int active, prepared, paused, loops;
    u8  r[BW_SNDPL_SIZE];
    s8  tracks[16];             /* pool index by track number, -1 none   */
    u32 tempo_counter;
    BwBlob seq;
    BwSndSeqBank bank;
    u64 tick;
} Player;

static struct {
    ExChan ch[CHANNELS];
    Track  tr[TRACKS];
    Player pl[PLAYERS];
    s16    local_var[PLAYERS][16];
    s16    global_var[16];
    u32    locked;
    u32    rand;
    u32    dropped;
    int    tables_ready;
} g;

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */

static u16 t_pitch[768];
static s16 t_dbsq[128];
static u8  t_volume[724];
static s8  t_sin[33];

static int round_half_away(double x)
{
    return (int)floor(x + 0.5);
}

static void tables_init(void)
{
    int i;
    if (g.tables_ready)
        return;
    for (i = 0; i < 768; i++)
        t_pitch[i] = (u16)round_half_away(65536.0 * (pow(2.0, i / 768.0) - 1.0));
    t_dbsq[0] = -32768;
    for (i = 1; i < 128; i++) {
        int v = round_half_away(400.0 * log10(i / 127.0));
        t_dbsq[i] = (s16)(v < -722 ? -722 : v);
    }
    for (i = 0; i < 724; i++) {
        int db = i - 723;
        double m = db >= -60 ? 1.0 : db >= -120 ? 2.0 : db >= -240 ? 4.0 : 16.0;
        int v = round_half_away(128.0 * m * pow(10.0, db / 200.0));
        if (v > 127)
            v = 127;
        if (i == 602 || i == 662 || i == 722)
            v = 126;
        t_volume[i] = (u8)v;
    }
    for (i = 0; i <= 32; i++)
        t_sin[i] = (s8)round_half_away(127.0 * sin(i * 3.14159265358979323846 / 64.0));
    g.tables_ready = 1;
}

void bw_snd_table_sums(s64 sum[3], s64 weighted[3])
{
    int i;
    tables_init();
    sum[0] = sum[1] = sum[2] = 0;
    weighted[0] = weighted[1] = weighted[2] = 0;
    for (i = 0; i < 768; i++) {
        sum[0] += t_pitch[i];
        weighted[0] += (s64)(i + 1) * t_pitch[i];
    }
    for (i = 0; i < 128; i++) {
        sum[1] += t_dbsq[i];
        weighted[1] += (s64)(i + 1) * t_dbsq[i];
    }
    for (i = 0; i < 724; i++) {
        sum[2] += t_volume[i];
        weighted[2] += (s64)(i + 1) * t_volume[i];
    }
}

u16 bw_snd_calc_timer(int base_timer, int pitch_)
{
    int pitch = -pitch_, shift = 0;
    u64 timer;

    tables_init();
    while (pitch < 0) {
        shift--;
        pitch += 768;
    }
    while (pitch >= 768) {
        pitch -= 768;
        shift++;
    }
    timer = (u64)(u32)base_timer * ((u64)t_pitch[pitch] + 0x10000u);
    shift -= 16;
    if (shift <= 0) {
        timer >>= -shift;
    } else if (shift < 32) {
        if (timer >> (64 - shift))
            return 0xFFFF;
        timer <<= shift;
    } else {
        return 0xFFFF;
    }
    if (timer < 0x10)
        return 0x10;
    if (timer > 0xFFFF)
        return 0xFFFF;
    return (u16)timer;
}

u16 bw_snd_calc_channel_volume(int db)
{
    int shift;
    tables_init();
    if (db < -723)
        db = -723;
    else if (db > 0)
        db = 0;
    if (db < -240)
        shift = 3;
    else if (db < -120)
        shift = 2;
    else if (db < -60)
        shift = 1;
    else
        shift = 0;
    return (u16)(t_volume[db + 723] | (shift << 8));
}

int bw_snd_decibel_square(int i)
{
    tables_init();
    if (i < 0)
        i = 0;
    if (i > 127)
        i = 127;
    return t_dbsq[i];
}

int bw_snd_sin_idx(int i)
{
    tables_init();
    i &= 127;
    if (i < 32)
        return t_sin[i];
    if (i < 64)
        return t_sin[64 - i];
    if (i < 96)
        return -t_sin[i - 64];
    return -t_sin[128 - i];
}

int bw_snd_attack_rate(int attack)
{
    static const u8 k_attack[19] = {
        0, 1, 5, 14, 26, 38, 51, 63, 73, 84, 92, 100, 109, 116, 123, 127, 132, 137, 143
    };
    attack &= 127;
    if (attack >= 109)
        return k_attack[127 - attack];
    return 255 - attack;
}

int bw_snd_fall_rate(int v)
{
    v &= 127;
    if (v == 127)
        return 0xFFFF;
    if (v == 126)
        return 0x3C00;
    if (v < 50)
        return v * 2 + 1;
    return 0x1E00 / (126 - v);
}

/* ------------------------------------------------------------------ */
/* Byte images                                                         */
/* ------------------------------------------------------------------ */

static int  tr_u8(const Track *t, int o)  { return t->r[o]; }
static int  tr_s8(const Track *t, int o)  { return (s8)t->r[o]; }
static int  tr_s16(const Track *t, int o) { return (s16)bw_rd16(t->r + o); }
static int  tr_u16(const Track *t, int o) { return bw_rd16(t->r + o); }
static void tr_set8(Track *t, int o, int v) { t->r[o] = (u8)v; }
static void tr_set16(Track *t, int o, int v)
{
    t->r[o] = (u8)v;
    t->r[o + 1] = (u8)((u32)v >> 8);
}
static int  pl_u8(const Player *p, int o)  { return p->r[o]; }
static int  pl_s16(const Player *p, int o) { return (s16)bw_rd16(p->r + o); }
static int  pl_u16(const Player *p, int o) { return bw_rd16(p->r + o); }
static void pl_set16(Player *p, int o, int v)
{
    p->r[o] = (u8)v;
    p->r[o + 1] = (u8)((u32)v >> 8);
}

static void store(u8 *img, u32 size_of_img, u32 off, u32 value, u32 size)
{
    u32 i;
    if (!(size == 1 || size == 2 || size == 4) || off + size > size_of_img)
        return;
    for (i = 0; i < size; i++)
        img[off + i] = (u8)(value >> (8 * i));
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------ */
/* Channels                                                            */
/* ------------------------------------------------------------------ */

static void unlink_channel(int c)
{
    ExChan *e = &g.ch[c];
    if (e->track >= 0) {
        Track *t = &g.tr[e->track];
        int *link = &t->ch_list;
        while (*link >= 0) {
            if (*link == c) {
                *link = e->next;
                break;
            }
            link = &g.ch[*link].next;
        }
    }
    e->track = -1;
    e->next = -1;
}

/* Drop or finish: the channel stops now and is free. */
static void free_channel(int c)
{
    unlink_channel(c);
    g.ch[c].active = 0;
    g.ch[c].start = 0;
    g.ch[c].prio = 0;
    bw_sndhw_stop(c);
}

static void release_channel(int c)
{
    ExChan *e = &g.ch[c];
    if (!e->active)
        return;
    e->env_status = ENV_RELEASE;
    e->prio = 1;        /* Choice: a releasing note is the first to go */
}

static int channel_level(const ExChan *e)
{
    return bw_snd_decibel_square(e->velocity) + (e->env >> 7);
}

/* PCM may use any channel: 0-7 first, with 1 and 3 (the ones the output
 * selector and the captures can take) last among them, then the PSG and
 * noise channels.  PSG only on 8-13, noise only on 14-15 (GBATEK). */
static int alloc_channel(int type, int prio, u32 mask, int track)
{
    static const u8 k_pcm[16] = { 0, 2, 4, 5, 6, 7, 1, 3, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const u8 k_psg[6] = { 8, 9, 10, 11, 12, 13 };
    static const u8 k_noise[2] = { 14, 15 };
    const u8 *order;
    int n, i, best = -1;
    Track *t;

    if (type == CH_PSG) {
        order = k_psg;
        n = 6;
    } else if (type == CH_NOISE) {
        order = k_noise;
        n = 2;
    } else {
        order = k_pcm;
        n = 16;
    }
    mask &= ~g.locked;
    for (i = 0; i < n; i++) {
        int c = order[i];
        ExChan *e = &g.ch[c];
        if (!((mask >> c) & 1u))
            continue;
        if (!e->active) {
            best = c;
            break;
        }
        if (best < 0 || e->prio < g.ch[best].prio
            || (e->prio == g.ch[best].prio && channel_level(e) < channel_level(&g.ch[best])))
            best = c;
    }
    if (best < 0)
        return -1;
    if (g.ch[best].active) {
        if (g.ch[best].prio > prio)
            return -1;
        free_channel(best);
    }
    memset(&g.ch[best], 0, sizeof(ExChan));
    g.ch[best].track = track;
    g.ch[best].next = -1;
    if (track >= 0) {
        t = &g.tr[track];
        g.ch[best].next = t->ch_list;
        t->ch_list = best;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* The bank                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int type;               /* 1 PCM, 2 PSG, 3 noise                     */
    int wave, arc;          /* PCM: wave index and arc slot; PSG: duty   */
    int original_key, attack, decay, sustain, release, pan;
} Inst;

static int read_param(const BwBlob *b, size_t off, int type, Inst *out)
{
    const u8 *d;
    if (off + 10 > b->size || type < 1 || type > 3)
        return -1;
    d = b->data + off;
    out->type = type;
    out->wave = bw_rd16(d);
    out->arc = bw_rd16(d + 2);
    out->original_key = d[4];
    out->attack = d[5];
    out->decay = d[6];
    out->sustain = d[7];
    out->release = d[8];
    out->pan = d[9];
    return 0;
}

/* SNDBankData.instOffset[prg] = type | offset << 8; type 16 is an
 * SNDDrumSet (min, max, then one SNDInstData per key), type 17 an
 * SNDKeySplit (eight upper keys, then one SNDInstData per used split);
 * an SNDInstData is u8 type, u8 pad, SNDInstParam. */
static int resolve_inst(const BwSndSeqBank *bank, int prg, int key, Inst *out)
{
    const BwBlob *b = &bank->sbnk;
    u32 count, entry, type;
    size_t off;

    if (b->data == NULL || b->size < 0x3C)
        return -1;
    count = bw_rd32(b->data + 0x38);
    if (prg < 0 || (u32)prg >= count || 0x3C + ((size_t)prg + 1) * 4 > b->size)
        return -1;
    entry = bw_rd32(b->data + 0x3C + (size_t)prg * 4);
    type = entry & 0xFFu;
    off = entry >> 8;
    if (type >= 1 && type <= 3)
        return read_param(b, off, (int)type, out);
    if (type == 16) {
        int lo, hi;
        size_t e;
        if (off + 2 > b->size)
            return -1;
        lo = b->data[off];
        hi = b->data[off + 1];
        if (key < lo || key > hi)
            return -1;
        e = off + 2 + (size_t)(key - lo) * 12;
        if (e + 12 > b->size)
            return -1;
        return read_param(b, e + 2, b->data[e], out);
    }
    if (type == 17) {
        int i;
        if (off + 8 > b->size)
            return -1;
        for (i = 0; i < 8; i++) {
            int top = b->data[off + i];
            if (top == 0)
                return -1;
            if (key <= top) {
                size_t e = off + 8 + (size_t)i * 12;
                if (e + 12 > b->size)
                    return -1;
                return read_param(b, e + 2, b->data[e], out);
            }
        }
        return -1;
    }
    return -1;
}

/* The wave a PCM region names, from the SWAR in its slot. */
static int resolve_wave(const BwSndSeqBank *bank, const Inst *in, ExChan *e)
{
    BwSwar swar;
    BwSwav w;
    const BwBlob *arc;

    if (in->arc < 0 || in->arc >= 4)
        return -1;
    arc = &bank->arc[in->arc];
    if (arc->data == NULL || arc->size == 0 || bw_swar_open(&swar, *arc) != 0)
        return -1;
    if (bw_swar_wave(&swar, (u32)in->wave, &w) != 0)
        return -1;
    if (w.wave_type > 2)
        return -1;
    e->format = w.wave_type;
    e->loop = w.loop_flag != 0;
    e->wave_timer = w.timer_val;
    e->pnt = w.loop_offset_words;
    e->len = w.nonloop_length_words;
    e->data = w.sample_data;
    e->avail = (size_t)(arc->data + arc->size - w.sample_data);
    e->looping_source = e->loop;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tracks                                                              */
/* ------------------------------------------------------------------ */

static void track_defaults(Track *t)
{
    memset(t->r, 0, sizeof(t->r));
    tr_set8(t, BW_SNDTR_PAN_RANGE, 127);
    tr_set8(t, BW_SNDTR_VOLUME, 127);
    tr_set8(t, BW_SNDTR_VOLUME2, 127);
    tr_set8(t, BW_SNDTR_BEND_RANGE, 2);
    tr_set8(t, BW_SNDTR_ATTACK, 0xFF);
    tr_set8(t, BW_SNDTR_DECAY, 0xFF);
    tr_set8(t, BW_SNDTR_SUSTAIN, 0xFF);
    tr_set8(t, BW_SNDTR_RELEASE, 0xFF);
    tr_set8(t, BW_SNDTR_PRIO, 64);
    tr_set8(t, BW_SNDTR_PORTA_KEY, 60);
    tr_set8(t, BW_SNDTR_MOD_SPEED, 16);
    tr_set8(t, BW_SNDTR_MOD_RANGE, 1);
    tr_set16(t, BW_SNDTR_CHANNEL_MASK, 0xFFFF);
    t->note_wait = 1;
    t->mute = t->tie = t->porta = t->cmp = t->channel_mask_flag = 0;
    t->wait = 0;
    t->depth = 0;
    t->ch_list = -1;
}

static int track_alloc(int player, int no)
{
    int i;
    for (i = 0; i < TRACKS; i++) {
        Track *t = &g.tr[i];
        if (t->used)
            continue;
        memset(t, 0, sizeof(*t));
        t->used = 1;
        t->player = player;
        t->no = no;
        track_defaults(t);
        return i;
    }
    return -1;
}

/* Release every note of a track (they keep sounding out, ownerless). */
static void track_release_all(Track *t)
{
    while (t->ch_list >= 0) {
        int c = t->ch_list;
        release_channel(c);
        unlink_channel(c);
    }
}

static void track_stop_all(Track *t)
{
    while (t->ch_list >= 0)
        free_channel(t->ch_list);
}

static void track_free(int i)
{
    Track *t = &g.tr[i];
    track_release_all(t);
    t->used = 0;
    t->active = 0;
}

/* ------------------------------------------------------------------ */
/* Players                                                             */
/* ------------------------------------------------------------------ */

static void player_defaults(Player *p)
{
    memset(p->r, 0, sizeof(p->r));
    p->r[BW_SNDPL_PRIO] = 64;
    p->r[BW_SNDPL_VOLUME] = 127;
    memset(p->r + BW_SNDPL_TRACKS, 0xFF, 16);
    pl_set16(p, BW_SNDPL_TEMPO, 120);
    pl_set16(p, BW_SNDPL_TEMPO_RATIO, 256);
    memset(p->tracks, -1, sizeof(p->tracks));
    p->tempo_counter = TICK_COST;   /* Choice: the first frame plays a tick */
    p->tick = 0;
    p->loops = 0;
}

static Track *player_track(Player *p, int no)
{
    if (no < 0 || no >= 16 || p->tracks[no] < 0)
        return NULL;
    return &g.tr[(int)p->tracks[no]];
}

static void player_stop_now(Player *p, int release)
{
    int no;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        if (t == NULL)
            continue;
        if (!release)
            track_stop_all(t);
        track_free(p->tracks[no]);
        p->tracks[no] = -1;
    }
    p->active = p->prepared = p->paused = 0;
}

/* ------------------------------------------------------------------ */
/* Reading the sequence                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    Track  *t;
    Player *p;
    int     bad;
} Reader;

static int rd8(Reader *r)
{
    if (r->t->cur >= r->p->seq.size) {
        r->bad = 1;
        return 0;
    }
    return r->p->seq.data[r->t->cur++];
}

static int rd16(Reader *r)
{
    int lo = rd8(r);
    return lo | (rd8(r) << 8);
}

static u32 rd24(Reader *r)
{
    u32 lo = (u32)rd16(r);
    return lo | ((u32)rd8(r) << 16);
}

static u32 rdvlv(Reader *r)
{
    u32 v = 0;
    int i;
    for (i = 0; i < 5; i++) {
        int b = rd8(r);
        v = (v << 7) | (u32)(b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return v;
}

enum { ARG_U8, ARG_S8, ARG_U16, ARG_S16, ARG_VLV };
enum { MODE_PLAIN, MODE_RANDOM, MODE_VAR };

static s16 *var_slot(int player, int var)
{
    if (var >= 0 && var < 16 && player >= 0 && player < PLAYERS)
        return &g.local_var[player][var];
    if (var >= 16 && var < 32)
        return &g.global_var[var - 16];
    return NULL;
}

/* A linear congruential generator (Numerical Recipes' constants).
 * Choice: the SDK's own generator and seed are ARM7 state. */
static u32 next_rand(void)
{
    g.rand = g.rand * 1664525u + 1013904223u;
    return g.rand >> 16;
}

static s32 rand_range(s32 lo, s32 hi)
{
    if (hi < lo) {
        s32 t = lo;
        lo = hi;
        hi = t;
    }
    return lo + (s32)(((s64)next_rand() * (hi - lo + 1)) >> 16);
}

/* The last argument of a command, read plainly or replaced by a prefix:
 * 0xA0 puts s16 min and max in its place, 0xA1 a u8 variable number. */
static s32 arg(Reader *r, int kind, int mode, int player)
{
    if (mode == MODE_RANDOM) {
        s32 lo = (s16)rd16(r);
        s32 hi = (s16)rd16(r);
        return rand_range(lo, hi);
    }
    if (mode == MODE_VAR) {
        s16 *v = var_slot(player, rd8(r));
        return v ? *v : 0;
    }
    switch (kind) {
    case ARG_S8:  return (s8)rd8(r);
    case ARG_U16: return rd16(r);
    case ARG_S16: return (s16)rd16(r);
    case ARG_VLV: return (s32)rdvlv(r);
    default:      return rd8(r);
    }
}

/* ------------------------------------------------------------------ */
/* Notes                                                               */
/* ------------------------------------------------------------------ */

static void set_sweep(ExChan *e, const Track *t, int from_key, int key, s32 len)
{
    int sweep = tr_s16(t, BW_SNDTR_SWEEP_PITCH);
    int porta_time = tr_u8(t, BW_SNDTR_PORTA_TIME);
    if (t->porta)
        sweep += (from_key - key) * 64;
    e->sweep_pitch = sweep;
    e->sweep_counter = 0;
    if (porta_time == 0) {
        /* Over the note's length, counted in ticks. */
        e->sweep_length = len > 0 ? len : 0;
        e->auto_sweep = 0;
    } else {
        int mag = sweep < 0 ? -sweep : sweep;
        e->sweep_length = (s32)(((s64)porta_time * porta_time * mag) >> 11);
        e->auto_sweep = 1;
    }
}

static void note_on(Player *p, Track *t, int key, int velocity, s32 len)
{
    Inst in;
    ExChan *e;
    int c, type, prio, a, d, s, rl;
    u32 mask;
    int ti = (int)(t - g.tr);
    int from_key = tr_u8(t, BW_SNDTR_PORTA_KEY);

    if (t->tie && t->ch_list >= 0) {
        /* Tie: the sounding note changes key and velocity, no new attack. */
        e = &g.ch[t->ch_list];
        e->key = key;
        e->velocity = velocity;
        set_sweep(e, t, from_key, key, -1);
        return;
    }
    if (resolve_inst(&p->bank, tr_u16(t, BW_SNDTR_PRG), key, &in) != 0)
        return;
    type = in.type == 1 ? CH_PCM : in.type == 2 ? CH_PSG : CH_NOISE;
    prio = clampi(pl_u8(p, BW_SNDPL_PRIO) + tr_u8(t, BW_SNDTR_PRIO), 0, 255);
    mask = t->channel_mask_flag ? (u32)tr_u16(t, BW_SNDTR_CHANNEL_MASK) : 0xFFFFu;
    c = alloc_channel(type, prio, mask, ti);
    if (c < 0) {
        g.dropped++;
        return;
    }
    e = &g.ch[c];
    e->type = type;
    if (type == CH_PCM) {
        if (resolve_wave(&p->bank, &in, e) != 0) {
            free_channel(c);
            return;
        }
    } else {
        e->wave_timer = PSG_BASE_TIMER;
        e->duty = in.wave & 7;
        e->looping_source = 1;
    }
    a = tr_u8(t, BW_SNDTR_ATTACK);
    d = tr_u8(t, BW_SNDTR_DECAY);
    s = tr_u8(t, BW_SNDTR_SUSTAIN);
    rl = tr_u8(t, BW_SNDTR_RELEASE);
    e->attack = bw_snd_attack_rate(a == 0xFF ? in.attack : a);
    e->decay = bw_snd_fall_rate(d == 0xFF ? in.decay : d);
    e->sustain = s == 0xFF ? in.sustain : s;
    e->release = bw_snd_fall_rate(rl == 0xFF ? in.release : rl);
    e->env = ENV_FLOOR;
    e->env_status = ENV_ATTACK;
    e->key = key;
    e->original_key = in.original_key;
    e->velocity = velocity;
    e->init_pan = in.pan - 64;
    e->prio = prio;
    e->length = t->tie ? -1 : len;
    e->lfo_target = tr_u8(t, BW_SNDTR_MOD_TARGET);
    e->lfo_speed = tr_u8(t, BW_SNDTR_MOD_SPEED);
    e->lfo_depth = tr_u8(t, BW_SNDTR_MOD_DEPTH);
    e->lfo_range = tr_u8(t, BW_SNDTR_MOD_RANGE);
    e->lfo_delay = (u32)tr_u16(t, BW_SNDTR_MOD_DELAY);
    set_sweep(e, t, from_key, key, len);
    e->active = 1;
    e->start = 1;
}

/* ------------------------------------------------------------------ */
/* One command                                                         */
/* ------------------------------------------------------------------ */

static void var_op(int op, s16 *v, s32 x, Track *t)
{
    switch (op) {
    case 0xB0: *v = (s16)x; break;
    case 0xB1: *v = (s16)(*v + x); break;
    case 0xB2: *v = (s16)(*v - x); break;
    case 0xB3: *v = (s16)(*v * x); break;
    case 0xB4: if (x != 0) *v = (s16)(*v / x); break;
    case 0xB5:
        if (x >= 0)
            *v = (s16)(u16)((u32)(s32)*v << (x & 31));
        else
            *v = (s16)((s32)*v >> ((-x) & 31));
        break;
    case 0xB6: *v = (s16)(x >= 0 ? rand_range(0, x) : rand_range(x, 0)); break;
    case 0xB8: t->cmp = *v == x; break;
    case 0xB9: t->cmp = *v >= x; break;
    case 0xBA: t->cmp = *v > x; break;
    case 0xBB: t->cmp = *v <= x; break;
    case 0xBC: t->cmp = *v < x; break;
    case 0xBD: t->cmp = *v != x; break;
    default: break;
    }
}

/* Execute one command.  Returns 1 when the track ended. */
static int track_command(Player *p, Track *t, int pi, int noteon_ok)
{
    Reader rd;
    int cmd, exec = 1, mode = MODE_PLAIN;
    u32 at;

    rd.t = t;
    rd.p = p;
    rd.bad = 0;
    at = t->cur;
    cmd = rd8(&rd);
    if (cmd == 0xA2) {
        exec = t->cmp;
        cmd = rd8(&rd);
    }
    if (cmd == 0xA0) {
        mode = MODE_RANDOM;
        cmd = rd8(&rd);
    } else if (cmd == 0xA1) {
        mode = MODE_VAR;
        cmd = rd8(&rd);
    }
    if (rd.bad)
        return 1;

    if (cmd < 0x80) {
        int vel = rd8(&rd);
        s32 len = arg(&rd, ARG_VLV, mode, pi);
        int key = clampi(cmd + tr_s8(t, BW_SNDTR_TRANSPOSE), 0, 127);
        if (rd.bad)
            return 1;
        if (exec) {
            if (!t->mute && noteon_ok)
                note_on(p, t, key, vel, len);
            tr_set8(t, BW_SNDTR_PORTA_KEY, key);
            if (t->note_wait)
                t->wait = len;
        }
        return 0;
    }

    switch (cmd) {
    case 0x80: {                                        /* rest */
        s32 v = arg(&rd, ARG_VLV, mode, pi);
        if (exec)
            t->wait = v;
        break;
    }
    case 0x81: {                                        /* program */
        s32 v = arg(&rd, ARG_VLV, mode, pi);
        if (exec && v >= 0 && v <= 0xFFFF)
            tr_set16(t, BW_SNDTR_PRG, v);
        break;
    }
    case 0x93: {                                        /* open track */
        int no = rd8(&rd);
        u32 off = rd24(&rd);
        if (exec && !rd.bad && no >= 1 && no < 16) {
            Track *o = player_track(p, no);
            if (o == NULL) {
                int ti = track_alloc(pi, no);
                if (ti >= 0) {
                    p->tracks[no] = (s8)ti;
                    o = &g.tr[ti];
                }
            }
            if (o != NULL) {
                o->cur = off;
                o->active = 1;
            }
        }
        break;
    }
    case 0x94: {                                        /* jump */
        u32 off = rd24(&rd);
        if (exec && !rd.bad) {
            if (off <= at)
                p->loops = 1;
            t->cur = off;
        }
        break;
    }
    case 0x95: {                                        /* call */
        u32 off = rd24(&rd);
        if (exec && !rd.bad && t->depth < CALL_DEPTH) {
            t->call_stack[t->depth] = t->cur;
            t->loop_count[t->depth] = 0;
            t->depth++;
            t->cur = off;
        }
        break;
    }
    case 0xFD:                                          /* return */
        if (exec && t->depth > 0)
            t->cur = t->call_stack[--t->depth];
        break;
    case 0xD4: {                                        /* loop start */
        int n = arg(&rd, ARG_U8, mode, pi);
        if (exec && t->depth < CALL_DEPTH) {
            t->call_stack[t->depth] = t->cur;
            t->loop_count[t->depth] = (u8)n;
            t->depth++;
        }
        break;
    }
    case 0xFC:                                          /* loop end */
        if (exec && t->depth > 0) {
            int n = t->loop_count[t->depth - 1];
            if (n != 0 && --n == 0) {
                t->depth--;
            } else {
                t->loop_count[t->depth - 1] = (u8)n;
                t->cur = t->call_stack[t->depth - 1];
                p->loops = 1;
            }
        }
        break;
    case 0xFE: {                                        /* allocate tracks */
        int m = rd16(&rd), no;
        if (exec && !rd.bad)
            for (no = 1; no < 16; no++)
                if (((m >> no) & 1) && player_track(p, no) == NULL) {
                    int ti = track_alloc(pi, no);
                    if (ti >= 0)
                        p->tracks[no] = (s8)ti;
                }
        break;
    }
    case 0xFF:
        return 1;
    case 0xE0: case 0xE1: case 0xE3: {
        s32 v = arg(&rd, cmd == 0xE3 ? ARG_S16 : ARG_U16, mode, pi);
        if (!exec)
            break;
        if (cmd == 0xE0)
            tr_set16(t, BW_SNDTR_MOD_DELAY, v);
        else if (cmd == 0xE1)
            pl_set16(p, BW_SNDPL_TEMPO, v);
        else
            tr_set16(t, BW_SNDTR_SWEEP_PITCH, v);
        break;
    }
    default:
        if (cmd >= 0xB0 && cmd <= 0xBD && cmd != 0xB7) {
            int var = rd8(&rd);
            s32 x = arg(&rd, ARG_S16, mode, pi);
            s16 *v = var_slot(pi, var);
            if (exec && v != NULL)
                var_op(cmd, v, x, t);
            break;
        }
        if (cmd >= 0xC0 && cmd <= 0xD6) {
            s32 v = arg(&rd, (cmd == 0xC3 || cmd == 0xC4) ? ARG_S8 : ARG_U8, mode, pi);
            if (!exec)
                break;
            switch (cmd) {
            case 0xC0: tr_set8(t, BW_SNDTR_PAN, v - 64); break;
            case 0xC1: tr_set8(t, BW_SNDTR_VOLUME, v); break;
            case 0xC2: p->r[BW_SNDPL_VOLUME] = (u8)v; break;
            case 0xC3: tr_set8(t, BW_SNDTR_TRANSPOSE, v); break;
            case 0xC4: tr_set8(t, BW_SNDTR_PITCH_BEND, v); break;
            case 0xC5: tr_set8(t, BW_SNDTR_BEND_RANGE, v); break;
            case 0xC6: tr_set8(t, BW_SNDTR_PRIO, v); break;
            case 0xC7: t->note_wait = v != 0; break;
            case 0xC8:
                t->tie = v != 0;
                track_release_all(t);
                break;
            case 0xC9:
                tr_set8(t, BW_SNDTR_PORTA_KEY,
                        clampi(v + tr_s8(t, BW_SNDTR_TRANSPOSE), 0, 127));
                t->porta = 1;
                break;
            case 0xCA: tr_set8(t, BW_SNDTR_MOD_DEPTH, v); break;
            case 0xCB: tr_set8(t, BW_SNDTR_MOD_SPEED, v); break;
            case 0xCC: tr_set8(t, BW_SNDTR_MOD_TARGET, v); break;
            case 0xCD: tr_set8(t, BW_SNDTR_MOD_RANGE, v); break;
            case 0xCE: t->porta = v != 0; break;
            case 0xCF: tr_set8(t, BW_SNDTR_PORTA_TIME, v); break;
            case 0xD0: tr_set8(t, BW_SNDTR_ATTACK, v); break;
            case 0xD1: tr_set8(t, BW_SNDTR_DECAY, v); break;
            case 0xD2: tr_set8(t, BW_SNDTR_SUSTAIN, v); break;
            case 0xD3: tr_set8(t, BW_SNDTR_RELEASE, v); break;
            case 0xD5: tr_set8(t, BW_SNDTR_VOLUME2, v); break;
            default: break;                             /* 0xD6 print */
            }
            break;
        }
        return 1;           /* not a command: the track ends here */
    }
    return rd.bad ? 1 : 0;
}

/* The track's notes when it ends: the ones that would never end by
 * themselves (tied, or ungated on a looping wave or a PSG tone) are
 * released; the rest play out. */
static void track_end(Track *t)
{
    int c = t->ch_list;
    t->active = 0;
    while (c >= 0) {
        int next = g.ch[c].next;
        ExChan *e = &g.ch[c];
        if (e->length < 0 || (e->length == 0 && e->looping_source))
            release_channel(c);
        c = next;
    }
}

/* One tick of one track.  Returns 1 if the track is (now) inactive. */
static int track_tick(Player *p, Track *t, int pi, int noteon_ok)
{
    int c, n;

    for (c = t->ch_list; c >= 0; c = g.ch[c].next) {
        ExChan *e = &g.ch[c];
        if (e->length > 0 && --e->length == 0)
            release_channel(c);
        if (!e->auto_sweep && e->sweep_counter < e->sweep_length)
            e->sweep_counter++;
    }
    if (!t->active)
        return 1;
    if (t->wait > 0 && --t->wait > 0)
        return 0;
    for (n = 0; t->wait <= 0; n++) {
        if (n >= MAX_COMMANDS_PER_TICK || track_command(p, t, pi, noteon_ok)) {
            track_end(t);
            return 1;
        }
    }
    return 0;
}

/* One tick of a player.  Returns 1 when it has finished. */
static int player_tick(Player *p, int pi, int noteon_ok)
{
    int no, any = 0, held = 0;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        if (t == NULL)
            continue;
        if (!track_tick(p, t, pi, noteon_ok))
            any = 1;
        if (t->ch_list >= 0)
            held = 1;
    }
    return !any && !held;
}

static void player_update_channels(Player *p)
{
    int no;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        int vol, pitch, pan, c, prio;
        if (t == NULL || t->ch_list < 0)
            continue;
        vol = bw_snd_decibel_square(tr_u8(t, BW_SNDTR_VOLUME))
            + bw_snd_decibel_square(tr_u8(t, BW_SNDTR_VOLUME2))
            + bw_snd_decibel_square(pl_u8(p, BW_SNDPL_VOLUME))
            + tr_s16(t, BW_SNDTR_EXT_FADER) + pl_s16(p, BW_SNDPL_EXT_FADER);
        if (vol < -32768)
            vol = -32768;
        pitch = tr_s16(t, BW_SNDTR_EXT_PITCH)
              + ((tr_s8(t, BW_SNDTR_PITCH_BEND) * tr_u8(t, BW_SNDTR_BEND_RANGE) * 64) >> 7);
        pan = clampi(tr_s8(t, BW_SNDTR_PAN) + tr_s8(t, BW_SNDTR_EXT_PAN), -128, 127);
        if (tr_u8(t, BW_SNDTR_PAN_RANGE) != 127)
            pan = (pan * tr_u8(t, BW_SNDTR_PAN_RANGE) + 64) >> 7;
        prio = clampi(pl_u8(p, BW_SNDPL_PRIO) + tr_u8(t, BW_SNDTR_PRIO), 0, 255);
        for (c = t->ch_list; c >= 0; c = g.ch[c].next) {
            ExChan *e = &g.ch[c];
            e->user_decay = vol;
            e->user_pitch = pitch;
            e->user_pan = pan;
            e->lfo_target = tr_u8(t, BW_SNDTR_MOD_TARGET);
            e->lfo_speed = tr_u8(t, BW_SNDTR_MOD_SPEED);
            e->lfo_depth = tr_u8(t, BW_SNDTR_MOD_DEPTH);
            e->lfo_range = tr_u8(t, BW_SNDTR_MOD_RANGE);
            e->lfo_delay = (u32)tr_u16(t, BW_SNDTR_MOD_DELAY);
            if (e->env_status != ENV_RELEASE)
                e->prio = prio;
        }
    }
}

static void player_finish(Player *p)
{
    int no;
    for (no = 0; no < 16; no++) {
        if (p->tracks[no] >= 0) {
            track_free(p->tracks[no]);
            p->tracks[no] = -1;
        }
    }
    p->active = p->prepared = p->paused = 0;
}

static void player_frame(Player *p, int pi)
{
    u32 ticks = 0, i;
    while (p->tempo_counter >= TICK_COST) {
        p->tempo_counter -= TICK_COST;
        ticks++;
    }
    for (i = 0; i < ticks; i++) {
        p->tick++;
        if (player_tick(p, pi, 1)) {
            player_finish(p);
            return;
        }
    }
    p->tempo_counter += ((u32)pl_u16(p, BW_SNDPL_TEMPO) * (u32)pl_u16(p, BW_SNDPL_TEMPO_RATIO)) >> 8;
    pl_set16(p, BW_SNDPL_TEMPO_COUNTER, (int)(p->tempo_counter & 0xFFFFu));
    player_update_channels(p);
}

/* ------------------------------------------------------------------ */
/* Channels, per frame                                                 */
/* ------------------------------------------------------------------ */

static int lfo_value(ExChan *e)
{
    s64 v;
    if (e->lfo_depth == 0)
        return 0;
    if (e->lfo_delay_counter < e->lfo_delay) {
        e->lfo_delay_counter++;
        return 0;
    }
    v = (s64)bw_snd_sin_idx((int)(e->lfo_counter >> 8)) * e->lfo_depth * e->lfo_range;
    e->lfo_counter = (e->lfo_counter + ((u32)e->lfo_speed << 6)) & 0x7FFFu;
    if (e->lfo_target == LFO_VOLUME)
        return (int)((v * 60) >> 14);
    return (int)(v >> 8);
}

static void envelope(ExChan *e)
{
    switch (e->env_status) {
    case ENV_ATTACK:
        e->env = -(s32)(((s64)(-e->env) * e->attack) >> 8);
        if (e->env == 0)
            e->env_status = ENV_DECAY;
        break;
    case ENV_DECAY: {
        s32 level = (s32)bw_snd_decibel_square(e->sustain) * 128;
        e->env -= e->decay;
        if (e->env <= level) {
            e->env = level;
            e->env_status = ENV_SUSTAIN;
        }
        break;
    }
    case ENV_RELEASE:
        e->env -= e->release;
        break;
    default:
        break;
    }
}

static void channels_frame(void)
{
    int c;
    for (c = 0; c < CHANNELS; c++) {
        ExChan *e = &g.ch[c];
        int db, pitch, pan, lfo, sweep = 0;
        u16 vol;

        if (!e->active)
            continue;
        if (!e->start && !bw_sndhw_busy(c)) {
            free_channel(c);                /* a one-shot ran out */
            continue;
        }
        envelope(e);
        if (e->env_status == ENV_RELEASE && e->env <= ENV_FLOOR) {
            free_channel(c);
            continue;
        }
        if (e->auto_sweep && e->sweep_counter < e->sweep_length)
            e->sweep_counter++;
        if (e->sweep_length > 0 && e->sweep_counter < e->sweep_length)
            sweep = (int)((s64)e->sweep_pitch * (e->sweep_length - e->sweep_counter)
                          / e->sweep_length);
        lfo = lfo_value(e);

        db = bw_snd_decibel_square(e->velocity) + (e->env >> 7) + e->user_decay
           + e->user_decay2;
        pitch = (e->key - e->original_key) * 64 + e->user_pitch + sweep;
        pan = e->init_pan + e->user_pan;
        if (e->lfo_target == LFO_VOLUME)
            db += lfo;
        else if (e->lfo_target == LFO_PAN)
            pan += lfo;
        else
            pitch += lfo;
        pan = clampi(pan, -64, 63) + 64;

        vol = bw_snd_calc_channel_volume(db);
        e->hw_volume = vol;
        e->hw_timer = bw_snd_calc_timer((int)e->wave_timer, pitch);
        e->hw_pan = pan;

        if (e->start) {
            if (e->type == CH_PCM)
                bw_sndhw_setup_pcm(c, e->format, e->data, e->avail,
                                   e->loop ? BW_SNDHW_REPEAT_LOOP : BW_SNDHW_REPEAT_ONESHOT,
                                   e->pnt, e->len, vol & 0xFF, vol >> 8, e->hw_timer, pan);
            else if (e->type == CH_PSG)
                bw_sndhw_setup_psg(c, e->duty, vol & 0xFF, vol >> 8, e->hw_timer, pan);
            else
                bw_sndhw_setup_noise(c, vol & 0xFF, vol >> 8, e->hw_timer, pan);
            bw_sndhw_start(c);
            e->start = 0;
            if (!bw_sndhw_busy(c))
                free_channel(c);            /* nothing it could play */
        } else {
            bw_sndhw_set_volume(c, vol & 0xFF, vol >> 8);
            bw_sndhw_set_timer(c, e->hw_timer);
            bw_sndhw_set_pan(c, pan);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The interface                                                       */
/* ------------------------------------------------------------------ */

void bw_sndseq_reset(void)
{
    int i;
    tables_init();
    memset(g.ch, 0, sizeof(g.ch));
    memset(g.tr, 0, sizeof(g.tr));
    for (i = 0; i < CHANNELS; i++) {
        g.ch[i].track = -1;
        g.ch[i].next = -1;
    }
    for (i = 0; i < PLAYERS; i++) {
        memset(&g.pl[i], 0, sizeof(Player));
        player_defaults(&g.pl[i]);
    }
    memset(g.local_var, 0xFF, sizeof(g.local_var));     /* -1, as SNDi_InitSharedWork */
    memset(g.global_var, 0xFF, sizeof(g.global_var));
    g.locked = 0;
    g.rand = 0x12345678u;
    g.dropped = 0;
}

void bw_sndseq_frame(void)
{
    int i;
    for (i = 0; i < PLAYERS; i++) {
        Player *p = &g.pl[i];
        if (p->active && !p->paused)
            player_frame(p, i);
    }
    channels_frame();
}

static Player *player_of(int player)
{
    return (player >= 0 && player < PLAYERS) ? &g.pl[player] : NULL;
}

void bw_sndseq_prepare(int player, BwBlob seq, u32 offset, const BwSndSeqBank *bank)
{
    Player *p = player_of(player);
    int ti;
    if (p == NULL)
        return;
    player_stop_now(p, 0);
    player_defaults(p);
    p->seq = seq;
    if (bank != NULL)
        p->bank = *bank;
    else
        memset(&p->bank, 0, sizeof(p->bank));
    ti = track_alloc(player, 0);
    if (ti < 0)
        return;
    p->tracks[0] = (s8)ti;
    g.tr[ti].cur = offset;
    g.tr[ti].active = 1;
    p->prepared = 1;
    /* The header (AllocTrack, then OpenTrack per track) runs now, so the
     * tracks exist for the parameters and mutes the ARM9 sends between
     * PrepareSeq and the start. */
    while (g.tr[ti].cur < seq.size
           && (seq.data[g.tr[ti].cur] == 0xFE || seq.data[g.tr[ti].cur] == 0x93))
        if (track_command(p, &g.tr[ti], player, 0))
            break;
}

void bw_sndseq_start(int player)
{
    Player *p = player_of(player);
    if (p != NULL && p->prepared)
        p->active = 1;
}

void bw_sndseq_stop(int player)
{
    Player *p = player_of(player);
    if (p != NULL)
        player_stop_now(p, 1);
}

void bw_sndseq_pause(int player, int pause)
{
    Player *p = player_of(player);
    int no;
    if (p == NULL)
        return;
    if (pause && !p->paused)
        for (no = 0; no < 16; no++) {
            Track *t = player_track(p, no);
            if (t != NULL)
                track_release_all(t);
        }
    p->paused = pause != 0;
}

void bw_sndseq_skip(int player, u32 ticks)
{
    Player *p = player_of(player);
    u32 i;
    if (p == NULL || !p->prepared)
        return;
    for (i = 0; i < ticks; i++) {
        p->tick++;
        if (player_tick(p, player, 0)) {
            player_finish(p);
            return;
        }
    }
}

void bw_sndseq_player_param(int player, u32 offset, u32 value, u32 size)
{
    Player *p = player_of(player);
    if (p == NULL || offset < BW_SNDPL_PRIO || (offset < 0x18 && offset >= BW_SNDPL_TRACKS))
        return;     /* the flags, myNo and the track table are the driver's */
    store(p->r, BW_SNDPL_SIZE, offset, value, size);
    if (offset <= BW_SNDPL_TEMPO_COUNTER && offset + size > BW_SNDPL_TEMPO_COUNTER)
        p->tempo_counter = (u32)pl_u16(p, BW_SNDPL_TEMPO_COUNTER);
}

void bw_sndseq_track_param(int player, u32 tracks, u32 offset, u32 value, u32 size)
{
    Player *p = player_of(player);
    int no;
    if (p == NULL || offset == 0 || offset >= BW_SNDTR_PARAMS)
        return;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        if (t != NULL && ((tracks >> no) & 1u))
            store(t->r, BW_SNDTR_PARAMS, offset, value, size);
    }
}

void bw_sndseq_mute(int player, u32 tracks, int mode)
{
    Player *p = player_of(player);
    int no;
    if (p == NULL)
        return;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        if (t == NULL || !((tracks >> no) & 1u))
            continue;
        t->mute = mode != BW_SNDSEQ_MUTE_OFF;
        if (mode == BW_SNDSEQ_MUTE_RELEASE)
            track_release_all(t);
        else if (mode == BW_SNDSEQ_MUTE_STOP)
            track_stop_all(t);
    }
}

void bw_sndseq_allocatable(int player, u32 tracks, u32 channels)
{
    Player *p = player_of(player);
    int no;
    if (p == NULL)
        return;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        if (t == NULL || !((tracks >> no) & 1u))
            continue;
        tr_set16(t, BW_SNDTR_CHANNEL_MASK, (int)(channels & 0xFFFFu));
        t->channel_mask_flag = 1;
    }
}

void bw_sndseq_set_var(int player, int var, s16 value)
{
    s16 *v = var_slot(player, var);
    if (v != NULL)
        *v = value;
}

s16 bw_sndseq_var(int player, int var)
{
    s16 *v = var_slot(player, var);
    return v ? *v : -1;
}

void bw_sndseq_lock(u32 channels)
{
    int c;
    channels &= 0xFFFFu;
    for (c = 0; c < CHANNELS; c++)
        if (((channels >> c) & 1u) && g.ch[c].active)
            free_channel(c);
    g.locked |= channels;
}

void bw_sndseq_unlock(u32 channels)
{
    g.locked &= ~channels;
}

u32 bw_sndseq_locked(void)
{
    return g.locked;
}

void bw_sndseq_stop_unlocked(u32 channels)
{
    int c;
    for (c = 0; c < CHANNELS; c++)
        if (((channels >> c) & 1u) && !((g.locked >> c) & 1u)) {
            if (g.ch[c].active)
                free_channel(c);
            else
                bw_sndhw_stop(c);
        }
}

static int overlaps(const u8 *a, size_t n, uintptr_t lo, uintptr_t hi)
{
    uintptr_t x = (uintptr_t)a;
    return a != NULL && n != 0 && x < hi && lo < x + n;
}

void bw_sndseq_invalidate(int kind, uintptr_t lo, uintptr_t hi)
{
    int i;
    /* Sequence and bank data: the players that read it stop.  Wave data:
     * only the channels playing from it stop, as the SDK's wave
     * invalidation does; the player goes on.  The game relies on that: a
     * fanfare pauses the BGM, drops the BGM's wave archives from the sound
     * heap to make room, and loads them back before it resumes the BGM.
     * Stopping the paused player there left the BGM's handle on a sequence
     * that had ended, so the BGM never came back after the fanfare. */
    for (i = 0; kind != 2 && i < PLAYERS; i++) {
        Player *p = &g.pl[i];
        int hit = 0;
        if (!p->prepared && !p->active)
            continue;
        if (kind == 0)
            hit = overlaps(p->seq.data, p->seq.size, lo, hi);
        else
            hit = overlaps(p->bank.sbnk.data, p->bank.sbnk.size, lo, hi);
        if (hit)
            player_stop_now(p, 0);
    }
    if (kind == 2)
        for (i = 0; i < CHANNELS; i++) {
            ExChan *e = &g.ch[i];
            if (e->active && e->type == CH_PCM && overlaps(e->data, e->avail, lo, hi))
                free_channel(i);
        }
}

int bw_sndseq_player_info(int player, BwSndSeqPlayerInfo *out)
{
    Player *p = player_of(player);
    int no;
    if (p == NULL)
        return -1;
    memset(out, 0, sizeof(*out));
    out->active = p->active;
    out->prepared = p->prepared;
    out->paused = p->paused;
    out->loops = p->loops;
    out->tick = p->tick;
    memcpy(out->reg, p->r, BW_SNDPL_SIZE);
    out->reg[0] = (u8)((p->active ? 0x80 : 0) | (p->prepared ? 0x40 : 0)
                       | (p->paused ? 0x20 : 0));
    out->reg[1] = (u8)player;
    for (no = 0; no < 16; no++) {
        Track *t = player_track(p, no);
        int c;
        if (t == NULL)
            continue;
        out->track_mask |= 1u << no;
        for (c = t->ch_list; c >= 0; c = g.ch[c].next)
            out->channel_mask |= 1u << c;
    }
    out->seq = p->seq.data;
    out->bank = p->bank;
    return 0;
}

u32 bw_sndseq_status(void)
{
    u32 s = 0;
    int i;
    for (i = 0; i < PLAYERS; i++)
        if (g.pl[i].active || g.pl[i].prepared)
            s |= 1u << i;
    return s;
}

u32 bw_sndseq_channel_status(void)
{
    u32 s = 0;
    int i;
    for (i = 0; i < CHANNELS; i++)
        if (g.ch[i].active)
            s |= 1u << i;
    return s;
}

u32 bw_sndseq_dropped_notes(void)
{
    return g.dropped;
}

static void put16(u8 *p, int v)
{
    p[0] = (u8)v;
    p[1] = (u8)((u32)v >> 8);
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

#define WORK_CHANNEL 0x000
#define WORK_PLAYER  0x540
#define WORK_TRACK   0x780
#define EXCH_SIZE    0x54

void bw_sndseq_write_work(u8 *work, u32 work_address)
{
    int i, no;
    memset(work, 0, 0xF80);
    for (i = 0; i < CHANNELS; i++) {
        const ExChan *e = &g.ch[i];
        u8 *w = work + WORK_CHANNEL + (size_t)i * EXCH_SIZE;
        w[0] = (u8)i;
        if (!e->active)
            continue;
        w[1] = (u8)e->type;
        w[2] = (u8)e->env_status;
        w[3] = (u8)(0x80 | (e->start ? 0x40 : 0) | (e->auto_sweep ? 0x20 : 0));
        w[4] = 127;
        w[5] = (u8)e->original_key;
        put16(w + 0x06, e->user_decay2);
        w[8] = (u8)e->key;
        w[9] = (u8)e->velocity;
        w[0x0A] = (u8)e->init_pan;
        w[0x0B] = (u8)e->user_pan;
        put16(w + 0x0C, e->user_decay);
        put16(w + 0x0E, e->user_pitch);
        put32(w + 0x10, (u32)e->env);
        put32(w + 0x14, (u32)e->sweep_counter);
        put32(w + 0x18, (u32)e->sweep_length);
        w[0x1C] = (u8)e->attack;
        w[0x1D] = (u8)e->sustain;
        put16(w + 0x1E, e->decay);
        put16(w + 0x20, e->release);
        w[0x22] = (u8)e->prio;
        w[0x23] = (u8)e->hw_pan;
        put16(w + 0x24, e->hw_volume);
        put16(w + 0x26, e->hw_timer);
        w[0x28] = (u8)e->lfo_target;
        w[0x29] = (u8)e->lfo_speed;
        w[0x2A] = (u8)e->lfo_depth;
        w[0x2B] = (u8)e->lfo_range;
        put16(w + 0x2C, (int)e->lfo_delay);
        put16(w + 0x2E, (int)e->lfo_delay_counter);
        put16(w + 0x30, (int)e->lfo_counter);
        put16(w + 0x32, e->sweep_pitch);
        put32(w + 0x34, (u32)e->length);
        w[0x38] = (u8)e->format;
        w[0x39] = (u8)e->loop;
        put16(w + 0x3C, (int)e->wave_timer);
        put16(w + 0x3E, (int)e->pnt);
        put32(w + 0x40, e->len);
        if (e->next >= 0)
            put32(w + 0x50, work_address + WORK_CHANNEL + (u32)e->next * EXCH_SIZE);
    }
    for (i = 0; i < PLAYERS; i++) {
        const Player *p = &g.pl[i];
        u8 *w = work + WORK_PLAYER + (size_t)i * BW_SNDPL_SIZE;
        memcpy(w, p->r, BW_SNDPL_SIZE);
        w[0] = (u8)((p->active ? 0x80 : 0) | (p->prepared ? 0x40 : 0)
                    | (p->paused ? 0x20 : 0));
        w[1] = (u8)i;
        for (no = 0; no < 16; no++)
            w[BW_SNDPL_TRACKS + no] = (u8)p->tracks[no];
        put32(w + 0x20, 0);                    /* the ARM7's bank pointer */
    }
    for (i = 0; i < TRACKS; i++) {
        const Track *t = &g.tr[i];
        u8 *w = work + WORK_TRACK + (size_t)i * BW_SNDTR_SIZE;
        if (!t->used)
            continue;
        memcpy(w, t->r, BW_SNDTR_PARAMS);
        w[0] = (u8)((t->active ? 0x80 : 0) | (t->note_wait ? 0x40 : 0)
                    | (t->mute ? 0x20 : 0) | (t->tie ? 0x10 : 0)
                    | (t->porta ? 0x04 : 0) | (t->cmp ? 0x02 : 0)
                    | (t->channel_mask_flag ? 0x01 : 0));
        put32(w + 0x20, (u32)t->wait);
        w[0x38] = t->loop_count[0];
        w[0x39] = t->loop_count[1];
        w[0x3A] = t->loop_count[2];
        w[0x3B] = (u8)t->depth;
        if (t->ch_list >= 0)
            put32(w + 0x3C, work_address + WORK_CHANNEL + (u32)t->ch_list * EXCH_SIZE);
    }
}
