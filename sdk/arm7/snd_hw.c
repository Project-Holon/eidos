/*
 * sdk/arm7/snd_hw.c -- the DS sound hardware model (sdk/arm7/snd_hw.h).
 *
 * The arithmetic of one output frame, per GBATEK's description of the mixer:
 *
 *   channel  = sample (signed 16-bit; PCM8 << 8, PSG/noise +-0x7FFF)
 *              * volume (0-127) >> divider (0, 1, 2 or 4)
 *   left    += channel * (128 - pan),  right += channel * pan   (pan 0-127)
 *   output   = (source selected by SOUNDCNT) * master (0-127), scaled by
 *              2^-21 (the three /128 steps) and clipped to 16 bits.
 *
 * Integers throughout; an arithmetic shift is a floor, on every host.  A
 * channel's sample is held between timer ticks (the hardware does not
 * interpolate).  Thread safety is the caller's: sdk/arm7/snd_driver.c calls
 * every function here under its own lock.
 */

#include <string.h>

#include "audio.h"
#include "snd_hw.h"

#define PSG_HIGH  0x7FFF
#define PSG_LOW   (-0x7FFF)

typedef struct {
    /* registers */
    int volume, shift, pan, hold, duty, repeat, format, busy;
    const u8 *src;
    size_t avail;
    u32 timer, pnt, len;

    /* derived */
    u64 inc;            /* timer ticks per output frame, 32.32          */
    u64 acc;            /* fraction of the next tick, 32.32              */
    u32 n_samples;      /* PNT + LEN in samples (PCM/ADPCM)              */
    u32 loop_start;     /* PNT in samples                                */

    /* playback state */
    u32 pos;            /* the sample being output                       */
    s32 out;            /* its value                                     */
    int ended;          /* a one-shot ran out (hold keeps `out`)         */
    /* ADPCM: the decoder is one sample ahead of nothing: it has decoded
     * every sample before dec_pos and holds the last in dec_pcm.        */
    u32 dec_pos;
    s32 dec_pcm;
    int dec_index;
    s32 loop_pcm;
    int loop_index, loop_saved;
    /* PSG / noise */
    u32 step;
    u32 lfsr;
} Chan;

typedef struct {
    int busy, format, loop, in, out;
    u8 *dst;
    size_t avail;
    u32 length;         /* words                                         */
    u32 pos;            /* bytes written in this pass                    */
    u64 acc;
} Cap;

static struct {
    Chan ch[BW_SNDHW_CHANNELS];
    Cap  cap[BW_SNDHW_CAPTURES];
    int  master, master_pan, surround;
    int  out_left, out_right, bypass1, bypass3;
} g_hw;

/* ------------------------------------------------------------------ */
/* IMA-ADPCM (GBATEK; the standard IMA tables)                         */
/* ------------------------------------------------------------------ */

static const s16 k_adpcm_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767
};
static const s8 k_adpcm_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* ------------------------------------------------------------------ */
/* Channels                                                            */
/* ------------------------------------------------------------------ */

static Chan *chan(int ch)
{
    return (ch >= 0 && ch < BW_SNDHW_CHANNELS) ? &g_hw.ch[ch] : NULL;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void set_inc(Chan *c)
{
    if (c->timer == 0)
        c->inc = 0;
    else
        c->inc = ((u64)BW_SNDHW_CLOCK << 32) / ((u64)BW_AUDIO_RATE * c->timer);
}

static void derive_lengths(Chan *c)
{
    u32 words = c->pnt + c->len;
    switch (c->format) {
    case BW_SNDHW_FMT_PCM8:
        c->n_samples = words * 4;
        c->loop_start = c->pnt * 4;
        break;
    case BW_SNDHW_FMT_PCM16:
        c->n_samples = words * 2;
        c->loop_start = c->pnt * 2;
        break;
    case BW_SNDHW_FMT_ADPCM:
        /* The first word is the header (initial PCM16, step index). */
        c->n_samples = words > 0 ? (words - 1) * 8 : 0;
        c->loop_start = c->pnt > 0 ? (c->pnt - 1) * 8 : 0;
        break;
    default:
        c->n_samples = 0;
        c->loop_start = 0;
        break;
    }
    /* Never read past what the driver knows is there. */
    if (c->avail != 0 && c->format != BW_SNDHW_FMT_PSG) {
        size_t cap;
        if (c->format == BW_SNDHW_FMT_PCM8)
            cap = c->avail;
        else if (c->format == BW_SNDHW_FMT_PCM16)
            cap = c->avail / 2;
        else
            cap = c->avail > 4 ? (c->avail - 4) * 2 : 0;
        if (c->n_samples > cap)
            c->n_samples = (u32)cap;
    }
    if (c->loop_start >= c->n_samples)
        c->loop_start = 0;
}

static void reset_state(Chan *c)
{
    c->acc = 0;
    c->pos = 0;
    c->out = 0;
    c->ended = 0;
    c->dec_pos = 0;
    c->dec_pcm = 0;
    c->dec_index = 0;
    c->loop_saved = 0;
    c->step = 0;
    c->lfsr = 0x7FFF;
    if (c->format == BW_SNDHW_FMT_ADPCM && c->src != NULL
        && (c->avail == 0 || c->avail >= 4)) {
        c->dec_pcm = (s16)bw_rd16(c->src);
        c->dec_index = clampi(c->src[2], 0, 88);
    }
}

/* Decode ADPCM samples up to and including `pos`. */
static s32 adpcm_at(Chan *c, u32 pos)
{
    while (c->dec_pos <= pos) {
        u32 k = c->dec_pos;
        u8 b, nib;
        s32 step, diff;
        if (k == c->loop_start && !c->loop_saved) {
            c->loop_pcm = c->dec_pcm;
            c->loop_index = c->dec_index;
            c->loop_saved = 1;
        }
        b = c->src[4 + (k >> 1)];
        nib = (k & 1) ? (u8)(b >> 4) : (u8)(b & 0x0F);
        step = k_adpcm_step[c->dec_index];
        diff = step >> 3;
        if (nib & 1) diff += step >> 2;
        if (nib & 2) diff += step >> 1;
        if (nib & 4) diff += step;
        if (nib & 8) {
            c->dec_pcm -= diff;
            if (c->dec_pcm < -0x7FFF) c->dec_pcm = -0x7FFF;
        } else {
            c->dec_pcm += diff;
            if (c->dec_pcm > 0x7FFF) c->dec_pcm = 0x7FFF;
        }
        c->dec_index = clampi(c->dec_index + k_adpcm_index[nib & 7], 0, 88);
        c->dec_pos++;
    }
    return c->dec_pcm;
}

static s32 sample_at(Chan *c, u32 pos)
{
    switch (c->format) {
    case BW_SNDHW_FMT_PCM8:
        return (s32)(s8)c->src[pos] * 256;
    case BW_SNDHW_FMT_PCM16:
        return (s16)bw_rd16(c->src + (size_t)pos * 2);
    case BW_SNDHW_FMT_ADPCM:
        return adpcm_at(c, pos);
    default:
        return 0;
    }
}

static s32 psg_level(const Chan *c)
{
    if (c->duty >= 7)
        return PSG_LOW;
    return (int)c->step < c->duty + 1 ? PSG_HIGH : PSG_LOW;
}

/* Advance a busy channel by `ticks` timer ticks. */
static void chan_tick(Chan *c, u32 ticks)
{
    if (c->format == BW_SNDHW_FMT_PSG) {
        if (c->duty < 0) {                  /* noise */
            while (ticks-- > 0) {
                u32 carry = c->lfsr & 1u;
                c->lfsr >>= 1;
                if (carry) {
                    c->lfsr ^= 0x6000u;
                    c->out = PSG_LOW;
                } else {
                    c->out = PSG_HIGH;
                }
            }
        } else {
            c->step = (c->step + ticks) & 7u;
            c->out = psg_level(c);
        }
        return;
    }
    while (ticks-- > 0 && !c->ended) {
        u32 next = c->pos + 1;
        if (next >= c->n_samples) {
            if (c->repeat == BW_SNDHW_REPEAT_ONESHOT || c->n_samples == 0) {
                c->ended = 1;
                c->busy = 0;
                if (!c->hold)
                    c->out = 0;
                return;
            }
            next = c->loop_start;
            if (c->format == BW_SNDHW_FMT_ADPCM) {
                if (c->loop_saved) {
                    c->dec_pcm = c->loop_pcm;
                    c->dec_index = c->loop_index;
                }
                c->dec_pos = next;
            }
        }
        c->pos = next;
        c->out = sample_at(c, c->pos);
    }
}

void bw_sndhw_setup_pcm(int ch, int format, const void *src, size_t avail,
                        int repeat, u32 pnt, u32 len,
                        int volume, int shift, u32 timer, int pan)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    c->busy = 0;
    c->format = format & 3;
    c->src = (const u8 *)src;
    c->avail = avail;
    c->repeat = repeat & 3;
    c->pnt = pnt & 0xFFFFu;
    c->len = len & 0x3FFFFFu;
    c->volume = clampi(volume, 0, 127);
    c->shift = shift & 3;
    c->pan = clampi(pan, 0, 127);
    c->duty = 0;
    c->timer = timer & 0xFFFFu;
    if (c->format == BW_SNDHW_FMT_PSG)  /* not a PCM format: nothing to play */
        c->src = NULL;
    set_inc(c);
    derive_lengths(c);
}

void bw_sndhw_setup_psg(int ch, int duty, int volume, int shift, u32 timer, int pan)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    c->busy = 0;
    c->format = BW_SNDHW_FMT_PSG;
    c->src = NULL;
    c->avail = 0;
    c->repeat = 0;
    c->pnt = c->len = 0;
    c->duty = duty & 7;
    c->volume = clampi(volume, 0, 127);
    c->shift = shift & 3;
    c->pan = clampi(pan, 0, 127);
    c->timer = timer & 0xFFFFu;
    set_inc(c);
    derive_lengths(c);
}

void bw_sndhw_setup_noise(int ch, int volume, int shift, u32 timer, int pan)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    bw_sndhw_setup_psg(ch, 0, volume, shift, timer, pan);
    c->duty = -1;                           /* noise marker */
}

void bw_sndhw_set_timer(int ch, u32 timer)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    c->timer = timer & 0xFFFFu;
    set_inc(c);
}

void bw_sndhw_set_volume(int ch, int volume, int shift)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    c->volume = clampi(volume, 0, 127);
    c->shift = shift & 3;
}

void bw_sndhw_set_pan(int ch, int pan)
{
    Chan *c = chan(ch);
    if (c != NULL)
        c->pan = clampi(pan, 0, 127);
}

void bw_sndhw_set_hold(int ch, int hold)
{
    Chan *c = chan(ch);
    if (c != NULL)
        c->hold = hold != 0;
}

void bw_sndhw_start(int ch)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    reset_state(c);
    if (c->format == BW_SNDHW_FMT_PSG) {
        /* PSG only on 8-13, noise only on 14-15. */
        if ((c->duty >= 0 && (ch < 8 || ch > 13)) || (c->duty < 0 && ch < 14))
            return;
        c->out = c->duty < 0 ? PSG_HIGH : psg_level(c);
        c->busy = 1;
        return;
    }
    if (c->src == NULL || c->n_samples == 0)
        return;
    c->out = sample_at(c, 0);
    c->busy = 1;
}

void bw_sndhw_stop(int ch)
{
    Chan *c = chan(ch);
    if (c == NULL)
        return;
    c->busy = 0;
    c->ended = 0;
    c->out = 0;
}

int bw_sndhw_busy(int ch)
{
    Chan *c = chan(ch);
    return c != NULL && c->busy;
}

u32 bw_sndhw_cnt(int ch)
{
    Chan *c = chan(ch);
    u32 duty;
    if (c == NULL)
        return 0;
    duty = c->duty < 0 ? 0u : (u32)c->duty;
    return (u32)c->volume | ((u32)c->shift << 8) | ((u32)(c->hold != 0) << 15)
         | ((u32)c->pan << 16) | (duty << 24) | ((u32)c->repeat << 27)
         | ((u32)c->format << 29) | ((u32)(c->busy != 0) << 31);
}

u32 bw_sndhw_timer(int ch)
{
    Chan *c = chan(ch);
    return c != NULL ? c->timer : 0;
}

const void *bw_sndhw_source(int ch, size_t *bytes)
{
    Chan *c = chan(ch);
    if (c == NULL || c->src == NULL) {
        *bytes = 0;
        return NULL;
    }
    *bytes = (size_t)(c->pnt + c->len) * 4;
    return c->src;
}

/* ------------------------------------------------------------------ */
/* Captures                                                            */
/* ------------------------------------------------------------------ */

void bw_sndhw_setup_capture(int cap, int format, void *dst, size_t avail, u32 length,
                            int loop, int in, int out)
{
    Cap *c;
    if (cap < 0 || cap >= BW_SNDHW_CAPTURES)
        return;
    c = &g_hw.cap[cap];
    c->busy = 0;
    c->format = format & 1;
    c->dst = (u8 *)dst;
    c->length = length & 0xFFFFu;
    if (avail != 0 && (size_t)c->length * 4 > avail)
        c->length = (u32)(avail / 4);
    c->avail = avail;
    c->loop = loop != 0;
    c->in = in & 1;
    c->out = out & 1;
}

void bw_sndhw_capture_start(int cap)
{
    Cap *c;
    if (cap < 0 || cap >= BW_SNDHW_CAPTURES)
        return;
    c = &g_hw.cap[cap];
    c->pos = 0;
    c->acc = 0;
    c->busy = c->dst != NULL && c->length != 0;
}

void bw_sndhw_capture_stop(int cap)
{
    if (cap >= 0 && cap < BW_SNDHW_CAPTURES)
        g_hw.cap[cap].busy = 0;
}

int bw_sndhw_capture_busy(int cap)
{
    return cap >= 0 && cap < BW_SNDHW_CAPTURES && g_hw.cap[cap].busy;
}

static void capture_write(Cap *c, s32 v, u32 ticks)
{
    u32 bytes = c->length * 4;
    while (ticks-- > 0 && c->busy) {
        if (c->format == 1) {
            c->dst[c->pos] = (u8)(s8)(v >> 8);
            c->pos += 1;
        } else {
            c->dst[c->pos] = (u8)v;
            c->dst[c->pos + 1] = (u8)((u32)v >> 8);
            c->pos += 2;
        }
        if (c->pos >= bytes) {
            c->pos = 0;
            if (!c->loop)
                c->busy = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Master control and the mix                                          */
/* ------------------------------------------------------------------ */

void bw_sndhw_set_master_volume(int volume)
{
    g_hw.master = clampi(volume, 0, 127);
}

int bw_sndhw_master_volume(void)
{
    return g_hw.master;
}

void bw_sndhw_set_output(int left, int right, int ch1_bypass, int ch3_bypass)
{
    g_hw.out_left = left & 3;
    g_hw.out_right = right & 3;
    g_hw.bypass1 = ch1_bypass != 0;
    g_hw.bypass3 = ch3_bypass != 0;
}

void bw_sndhw_set_master_pan(int pan)
{
    g_hw.master_pan = pan < 0 ? -1 : clampi(pan, 0, 127);
}

int bw_sndhw_master_pan(void)
{
    return g_hw.master_pan;
}

void bw_sndhw_set_surround_decay(int decay)
{
    g_hw.surround = clampi(decay, 0, 127);
}

void bw_sndhw_reset(void)
{
    int i;
    memset(&g_hw, 0, sizeof(g_hw));
    for (i = 0; i < BW_SNDHW_CHANNELS; i++)
        g_hw.ch[i].lfsr = 0x7FFF;
    g_hw.master = 127;
    g_hw.master_pan = -1;
}

static s32 clip16(s64 v)
{
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : (s32)v);
}

void bw_sndhw_mix(s16 *out, u32 frames)
{
    static const int k_div[4] = { 0, 1, 2, 4 };
    /* The work arrays are outside the sample loop: every element is written
     * on every sample, and a local declared in the loop would be cleared on
     * every turn as well (-ftrivial-auto-var-init). */
    s32 level[BW_SNDHW_CHANNELS];
    int pan[BW_SNDHW_CHANNELS];
    s64 ch_l[BW_SNDHW_CHANNELS], ch_r[BW_SNDHW_CHANNELS];
    /* what no channel's clock changes during the call, read once */
    const int master_pan = g_hw.master_pan, surround = g_hw.surround;
    const int bypass1 = g_hw.bypass1, bypass3 = g_hw.bypass3;
    u32 f;
    int i;

    for (f = 0; f < frames; f++) {
        s64 mix_l = 0, mix_r = 0;
        s64 sel_l, sel_r;

        for (i = 0; i < BW_SNDHW_CHANNELS; i++) {
            Chan *c = &g_hw.ch[i];
            s32 s = (c->busy || (c->ended && c->hold)) ? c->out : 0;
            pan[i] = master_pan >= 0 ? master_pan : c->pan;
            if (s == 0) {
                level[i] = 0;           /* a silent channel: 0 whatever its volume */
                continue;
            }
            level[i] = (s * c->volume) >> k_div[c->shift];
            if (surround != 0) {
                int off = pan[i] >= 64 ? pan[i] - 64 : 64 - pan[i];
                level[i] = (s32)(((s64)level[i] * (8192 - surround * off)) >> 13);
            }
        }
        /* SNDCAPxCNT bit 0: channel 0 (2) added into channel 1 (3). */
        if (g_hw.cap[0].busy && g_hw.cap[0].out)
            level[1] += level[0];
        if (g_hw.cap[1].busy && g_hw.cap[1].out)
            level[3] += level[2];
        for (i = 0; i < BW_SNDHW_CHANNELS; i++) {
            if (level[i] == 0) {
                ch_l[i] = 0;
                ch_r[i] = 0;
                continue;
            }
            ch_l[i] = (s64)level[i] * (128 - pan[i]);
            ch_r[i] = (s64)level[i] * pan[i];
            if ((i == 1 && bypass1) || (i == 3 && bypass3))
                continue;
            mix_l += ch_l[i];
            mix_r += ch_r[i];
        }
        switch (g_hw.out_left) {
        case 1:  sel_l = ch_l[1]; break;
        case 2:  sel_l = ch_l[3]; break;
        case 3:  sel_l = ch_l[1] + ch_l[3]; break;
        default: sel_l = mix_l; break;
        }
        switch (g_hw.out_right) {
        case 1:  sel_r = ch_r[1]; break;
        case 2:  sel_r = ch_r[3]; break;
        case 3:  sel_r = ch_r[1] + ch_r[3]; break;
        default: sel_r = mix_r; break;
        }

        /* Captures sample their source at channel 1's / 3's timer. */
        for (i = 0; i < BW_SNDHW_CAPTURES; i++) {
            Cap *c = &g_hw.cap[i];
            Chan *clk = &g_hw.ch[i == 0 ? 1 : 3];
            s32 v;
            if (!c->busy || clk->inc == 0)
                continue;
            if (c->in)
                v = clip16(level[i == 0 ? 0 : 2] >> 7);
            else
                v = clip16((i == 0 ? mix_l : mix_r) >> 14);
            c->acc += clk->inc;
            capture_write(c, v, (u32)(c->acc >> 32));
            c->acc &= 0xFFFFFFFFu;
        }

        if (out != NULL) {
            out[(size_t)f * 2] = (s16)clip16((sel_l * g_hw.master) >> 21);
            out[(size_t)f * 2 + 1] = (s16)clip16((sel_r * g_hw.master) >> 21);
        }

        /* Then every busy channel's clock runs one frame. */
        for (i = 0; i < BW_SNDHW_CHANNELS; i++) {
            Chan *c = &g_hw.ch[i];
            u32 ticks;
            if (!c->busy || c->inc == 0)
                continue;
            c->acc += c->inc;
            ticks = (u32)(c->acc >> 32);
            c->acc &= 0xFFFFFFFFu;
            if (ticks != 0)
                chan_tick(c, ticks);
        }
    }
}
