/*
 * engine/world/bcam_sim.c -- the battle camera simulator (bcam_sim.h).
 *
 * The game moves the battle camera with effect scripts that a small virtual
 * machine runs one command per frame.  This file runs the same scripts on
 * the same data and ports the code that touches the camera, so the frames
 * it returns are the frames the game draws:
 *
 *   script       a/0/6/6 member = move number (moves), a/0/6/7 member =
 *                effect - 0x231 (intros, idle moves, resets).  Byte 0 holds
 *                the variant count; the first word of variant v at 4 + v*0x38
 *                is the offset of its commands, which are a u16 opcode and
 *                a fixed list of u32 arguments (btlv_effvm.c, table at
 *                0x02209E88 of overlay 94).
 *   camera       eye, target, eased moves, orbit, shake: btlv_camera.c,
 *                same fx32 arithmetic and rounding.  Presets and slot
 *                positions are read from the overlay 94 image.
 *   waits        a "wait" command holds the script until the camera, the
 *                Pokemon tweens, the particles, the background scroll and
 *                the stage tints are idle.  Tweens and tints are ported;
 *                the particles are stepped as the SPL emitter does
 *                (a/0/0/6 files: start delay, emitter life, emission
 *                interval, particle life, child particles) without
 *                positions, because only their death frame matters.
 *
 * One frame is the order of BTLV_EFFECT_Main: the script, the palette fade
 * task, the tweens, the sprite palette fades, the stage tints, the camera,
 * the particles.  A command that returns the result 1 ends the frame; a
 * wait costs one extra frame when it is released (VM_Control returns after
 * clearing it).
 *
 * Not modelled (they do not move the camera or the slots, and their length
 * is not in any file): sound waits (modes 10 to 16), the sprite animation
 * waits, the window fade of command 44, double and triple battle layouts.
 * Particle lives use the longest value the random attenuation allows.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcam_sim.h"
#include "port.h"

typedef struct { fx32 x, y, z; } V3;

#define OVL94_BASE      0x021F6560u
#define MOVE_END        0x231           /* first effect id that is not a move */
#define MAX_FRAMES      12000           /* a script that never ends is cut here */
#define MAX_TASKS       96
#define MAX_EMITTERS    96
#define EMITTER_LIMIT   0x18            /* PARTICLE_StartEffectWithCallback refuses past this */
#define NO_POKE         0xFF

/* ------------------------------------------------------------------ */
/* Game data: the overlay 94 image and the ARM9 tables                  */
/* ------------------------------------------------------------------ */

static int   g_trace;         /* BCAM_TRACE in the environment: log commands to stderr */
static u8    *g_ovl;
static size_t g_ovl_len;
static const s16 *g_sincos;     /* FX_SinCosTable_ 0x020A1B38, 4096 pairs (sin, cos) */
static const s16 *g_atan;       /* FX_AtanIdxTable_ 0x020A5B38, 129 entries */
static const u16 *g_codepair;   /* sPalAnmCodePairTable 0x0209E904, 25 pairs */

static int ovl_load(void)
{
    char path[1024];
    static const char *const dirs[] = { "overlays_dec", "overlays" };
    int i;

    free(g_ovl);
    g_ovl = NULL;
    for (i = 0; i < 2 && !g_ovl; i++) {
        FILE *f;
        long n;

        snprintf(path, sizeof(path), "%s/../%s/y9_0094.bin", bw_data_root(), dirs[i]);
        f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && (g_ovl = (u8 *)malloc((size_t)n)) != NULL) {
            if (fread(g_ovl, 1, (size_t)n, f) != (size_t)n) {
                free(g_ovl);
                g_ovl = NULL;
            } else {
                g_ovl_len = (size_t)n;
            }
        }
        fclose(f);
    }
    return g_ovl ? 0 : -1;
}

static int tables_load(void)
{
    g_sincos = (const s16 *)bw_arm9_rodata(0x020A1B38u, 0x4000u);
    g_atan = (const s16 *)bw_arm9_rodata(0x020A5B38u, 129u * 2u);
    g_codepair = (const u16 *)bw_arm9_rodata(0x0209E904u, 25u * 4u);
    if (!g_sincos || !g_atan || !g_codepair)
        return -1;
    return ovl_load();
}

/* A vector of three fx32 at an overlay 94 address. */
static V3 ovl_v3(u32 addr)
{
    V3 v = { 0, 0, 0 };

    if (addr >= OVL94_BASE && addr - OVL94_BASE + 12 <= g_ovl_len)
        memcpy(&v, g_ovl + (addr - OVL94_BASE), 12);
    return v;
}

/* ------------------------------------------------------------------ */
/* Fixed point (FX_Div, FX_Atan2Idx, VEC_Mag of the ARM9)               */
/* ------------------------------------------------------------------ */

/*
 * FX_Div feeds the divider n << 32 and takes the quotient back with FX_GetDivResult,
 * which adds half a unit (0x80000) and shifts by 20: the result is the quotient
 * rounded to the nearest fx32, not cut toward zero.
 */
static fx32 fx_div(fx32 n, fx32 d)
{
    s64 q;

    if (d == 0)
        return 0;
    q = (s64)((uint64_t)(s64)n << 32) / d;
    return (fx32)((q + 0x80000) >> 20);
}

#define FXM(a, b)    ((fx32)(((s64)(a) * (b) + 0x800) >> 12))
#define TO_FX(i)        ((fx32)((i) * 4096))

static u16 fx_atan2idx(fx32 y, fx32 x)
{
    fx32 a, b;
    int c, sgn;

    if (y > 0) {
        if (x > 0) {
            if (x > y)      { a = y; b = x; c = 0;       sgn = 1; }
            else if (x < y) { a = x; b = y; c = 0x4000;  sgn = 0; }
            else return 0x2000;
        } else if (x < 0) {
            x = -x;
            if (x < y)      { a = x; b = y; c = 0x4000;  sgn = 1; }
            else if (x > y) { a = y; b = x; c = 0x8000;  sgn = 0; }
            else return 0x6000;
        } else return 0x4000;
    } else if (y < 0) {
        y = -y;
        if (x < 0) {
            x = -x;
            if (x > y)      { a = y; b = x; c = -0x8000; sgn = 1; }
            else if (x < y) { a = x; b = y; c = -0x4000; sgn = 0; }
            else return 0xA000;
        } else if (x > 0) {
            if (x < y)      { a = x; b = y; c = -0x4000; sgn = 1; }
            else if (x > y) { a = y; b = x; c = 0;       sgn = 0; }
            else return 0xE000;
        } else return 0xC000;
    } else {
        return x >= 0 ? 0 : 0x8000;
    }
    if (b == 0)
        return 0;
    if (sgn)
        return (u16)(c + g_atan[fx_div(a, b) >> 5]);
    return (u16)(c - g_atan[fx_div(a, b) >> 5]);
}

static fx32 vec_mag(const V3 *v)
{
    uint64_t t = (uint64_t)((s64)v->x * v->x + (s64)v->y * v->y + (s64)v->z * v->z);
    uint64_t r;

    t <<= 2;
    r = (uint64_t)sqrt((double)t);
    while (r * r > t)
        r--;
    while ((r + 1) * (r + 1) <= t)
        r++;
    return ((fx32)(u32)r + 1) >> 1;
}

/* ------------------------------------------------------------------ */
/* Movers (BTLV_EFFECT_StepMover and its helpers)                       */
/* ------------------------------------------------------------------ */

typedef struct Mover {
    int mode;
    V3 start, end, step;
    int count, count_reset, wait, wait_reset, repeat;
} Mover;

static void step_scalar(fx32 *value, fx32 *step, fx32 *limit, int *not_done)
{
    fx32 n = *value + *step;

    *value = n;
    if (*step < 0) {
        if (n <= *limit)
            *value = *limit;
        else if (not_done)
            *not_done = 0;
    } else {
        if (n >= *limit)
            *value = *limit;
        else if (not_done)
            *not_done = 0;
    }
}

static void vec_step(const V3 *from, const V3 *to, V3 *step, fx32 frames)
{
    fx32 *s = &step->x;
    const fx32 *f = &from->x, *t = &to->x;
    int i;

    step->x = step->y = step->z = 0;
    if (frames == 0)
        frames = 0x1000;
    for (i = 0; i < 3; i++) {
        if (t[i] - f[i] != 0) {
            s[i] = fx_div(t[i] - f[i], frames);
            if (s[i] == 0)
                s[i] = t[i] > f[i] ? 1 : -1;
        }
    }
}

/* Returns 1 when the move is over. */
static int step_mover(Mover *m, V3 *out)
{
    int ret = 1;

    switch (m->mode) {
    case 0:
        *out = m->end;
        break;
    case 1:
    case 4:
        if (m->wait == 0) {
            m->wait = m->wait_reset;
            step_scalar(&out->x, &m->step.x, &m->end.x, &ret);
            step_scalar(&out->y, &m->step.y, &m->end.y, &ret);
            step_scalar(&out->z, &m->step.z, &m->end.z, &ret);
        } else {
            m->wait--;
            ret = 0;
        }
        break;
    case 2:
    case 3:
        if (m->wait == 0) {
            m->wait = m->wait_reset;
            out->x += m->step.x;
            out->y += m->step.y;
            out->z += m->step.z;
            if (--m->count == 0) {
                m->repeat--;
                m->count = m->count_reset;
                if (m->mode == 2 || (m->mode == 3 && (m->repeat & 1))) {
                    m->step.x *= -1;
                    m->step.y *= -1;
                    m->step.z *= -1;
                }
            }
        } else {
            m->wait--;
        }
        if (m->repeat != 0) {
            ret = 0;
        } else {
            *out = m->start;
        }
        break;
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* The battle camera (btlv_camera.c)                                    */
/* ------------------------------------------------------------------ */

#define CAM_EYE   1
#define CAM_TGT   2
#define CAM_SHAKE 4

typedef struct Camera {
    V3 g_eye, g_tgt;            /* what the G3D camera holds: what is drawn */
    int pitch, yaw, radius;     /* orbit */
    unsigned flags;
    V3 eye, target, eye_dest, target_dest, eye_step, target_step, shake_off;
    int move_wait, move_wait_reset, ease_count;
    int shake_done;             /* the shake mover finished: the flag goes with the next step */
    Mover shake;
} Camera;

static void cam_sync_orbit(Camera *c)
{
    V3 e;

    e.x = c->g_eye.x - c->g_tgt.x;
    e.y = c->g_eye.y - c->g_tgt.y;
    e.z = c->g_eye.z - c->g_tgt.z;
    c->pitch = fx_atan2idx(e.y, e.z);
    c->yaw = fx_atan2idx(e.z, e.x);
    c->radius = vec_mag(&e);
}

static void cam_default(V3 *eye, V3 *tgt)
{
    eye->x = 0x6B33; eye->y = 0x6B33; eye->z = 0x114CD;
    tgt->x = 0;      tgt->y = 0x299A; tgt->z = 0;
}

static void cam_init(Camera *c)
{
    memset(c, 0, sizeof(*c));
    cam_default(&c->g_eye, &c->g_tgt);
    cam_sync_orbit(c);
    cam_default(&c->eye, &c->target);
}

static void cam_set(Camera *c, const V3 *eye, const V3 *tgt)
{
    if (eye) {
        c->eye = *eye;
        c->g_eye = *eye;
    }
    if (tgt) {
        c->target = *tgt;
        c->g_tgt = *tgt;
    }
    cam_sync_orbit(c);
}

static fx32 sc(int idx)
{
    return g_sincos[(idx & 0xFFF) * 2];
}

static fx32 cs(int idx)
{
    return g_sincos[(idx & 0xFFF) * 2 + 1];
}

static void cam_advance_orbit(Camera *c, int d_pitch, int d_yaw, V3 *eye_out, V3 *tgt_base)
{
    *tgt_base = c->g_tgt;
    c->pitch += d_pitch;
    c->yaw += d_yaw;
    eye_out->x = FXM(cs(c->yaw >> 4), cs(c->pitch >> 4));
    eye_out->y = sc(c->pitch >> 4);
    eye_out->z = FXM(sc(c->yaw >> 4), cs(c->pitch >> 4));
    eye_out->x = FXM(eye_out->x, c->radius);
    eye_out->y = FXM(eye_out->y, c->radius);
    eye_out->z = FXM(eye_out->z, c->radius);
    eye_out->x += tgt_base->x;
    eye_out->y += tgt_base->y;
    eye_out->z += tgt_base->z;
}

static void cam_rotate_orbit(Camera *c, int d_pitch, int d_yaw)
{
    cam_advance_orbit(c, d_pitch, d_yaw, &c->eye, &c->target);
    c->g_eye = c->eye;
}

static void cam_start_move(Camera *c, const V3 *eye_dest, const V3 *tgt_dest, int frames, int delay, int ease)
{
    c->ease_count = ease;
    c->move_wait = delay;
    c->move_wait_reset = delay;
    if (eye_dest) {
        c->eye_dest = *eye_dest;
        vec_step(&c->eye, eye_dest, &c->eye_step, TO_FX(frames));
        c->flags |= CAM_EYE;
    }
    if (tgt_dest) {
        c->target_dest = *tgt_dest;
        vec_step(&c->target, tgt_dest, &c->target_step, TO_FX(frames));
        c->flags |= CAM_TGT;
    }
}

static void cam_start_shake(Camera *c, int axis, fx32 amplitude, int frames, int wait_ticks, int repeat_pairs)
{
    Mover *m = &c->shake;

    c->shake_done = 0;
    m->mode = 3;
    m->count = frames;
    m->count_reset = frames;
    m->wait = 0;
    m->wait_reset = wait_ticks;
    m->start.x = m->start.y = m->start.z = 0;
    m->repeat = repeat_pairs * 4;
    if (axis == 1) {
        m->end.x = amplitude;
        m->end.y = 0;
        m->step.x = fx_div(amplitude, TO_FX(frames));
        m->step.y = 0;
    } else {
        m->end.x = 0;
        m->end.y = amplitude;
        m->step.x = 0;
        m->step.y = fx_div(amplitude, TO_FX(frames));
    }
    m->end.z = 0;
    m->step.z = 0;
    c->flags |= CAM_SHAKE;
}

static void cam_step(Camera *c)
{
    int all_done = 1;
    int half;

    if (c->flags == 0)
        return;
    if (c->move_wait == 0) {
        c->move_wait = c->move_wait_reset;
        if (c->ease_count != 0 && --c->ease_count == 0) {
            fx32 *s[6] = { &c->eye_step.x, &c->eye_step.y, &c->eye_step.z,
                           &c->target_step.x, &c->target_step.y, &c->target_step.z };
            int i;

            for (i = 0; i < 6; i++) {
                half = *s[i] >> 1;
                if (half)
                    *s[i] = half;
            }
        }
        if (c->flags & CAM_EYE) {
            step_scalar(&c->eye.x, &c->eye_step.x, &c->eye_dest.x, &all_done);
            step_scalar(&c->eye.y, &c->eye_step.y, &c->eye_dest.y, &all_done);
            step_scalar(&c->eye.z, &c->eye_step.z, &c->eye_dest.z, &all_done);
            if (all_done == 1)
                c->flags &= ~(unsigned)CAM_EYE;
        }
        if (c->flags & CAM_TGT) {
            step_scalar(&c->target.x, &c->target_step.x, &c->target_dest.x, &all_done);
            step_scalar(&c->target.y, &c->target_step.y, &c->target_dest.y, &all_done);
            step_scalar(&c->target.z, &c->target_step.z, &c->target_dest.z, &all_done);
            if (all_done == 1)
                c->flags &= ~(unsigned)CAM_TGT;
        }
        if (c->flags & CAM_SHAKE) {
            /*
             * The recorded game keeps the shake flag set for one more frame after the
             * mover reports it is done (the camera waits end one frame later than the
             * shake itself), so the flag is cleared on the call after the done one.
             */
            if (c->shake_done) {
                c->flags &= ~(unsigned)CAM_SHAKE;
                c->shake_done = 0;
            } else if (step_mover(&c->shake, &c->shake_off) == 1) {
                c->shake_done = 1;
            }
        }
        c->g_eye.x = c->eye.x + c->shake_off.x;
        c->g_eye.y = c->eye.y + c->shake_off.y;
        c->g_eye.z = c->eye.z + c->shake_off.z;
        c->g_tgt.x = c->target.x + c->shake_off.x;
        c->g_tgt.y = c->target.y + c->shake_off.y;
        c->g_tgt.z = c->target.z + c->shake_off.z;
        cam_sync_orbit(c);
    } else {
        c->move_wait--;
    }
}

/* ------------------------------------------------------------------ */
/* Particle files (a/0/0/6) and the emitters                            */
/* ------------------------------------------------------------------ */

typedef struct PtcRes {
    int self_maintain;
    int start_delay, life_emitter, life_particle, interval;
    fx32 emit_count;
    int has_child, child_life, child_count, child_delay, child_interval;
} PtcRes;

typedef struct PtcFile {
    int key;
    int count;              /* resources */
    PtcRes res[32];
} PtcFile;

/* The sizes of the blocks that follow a resource header, by flag bit. */
static const int k_behavior_size[6] = { 8, 8, 16, 4, 8, 16 };

static u32 rd_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd_u16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static int ptc_parse(const u8 *d, size_t n, PtcFile *out)
{
    size_t p = 0x20;
    int i, b;
    int count;

    if (n < 0x20)
        return -1;
    count = rd_u16(d + 8);
    if (count > 32)
        count = 32;
    out->count = count;
    for (i = 0; i < count; i++) {
        PtcRes *r = &out->res[i];
        const u8 *h = d + p;
        u32 fl;

        if (p + 0x58 > n)
            return -1;
        fl = rd_u32(h);
        memset(r, 0, sizeof(*r));
        r->self_maintain = (fl >> 14) & 1;
        r->emit_count = (fx32)rd_u32(h + 16);
        r->start_delay = rd_u16(h + 50);
        r->life_emitter = rd_u16(h + 60);
        r->life_particle = rd_u16(h + 62);
        r->interval = h[68];
        p += 0x58;
        if (fl & (1u << 8))  p += 12;
        if (fl & (1u << 9))  p += 12;
        if (fl & (1u << 10)) p += 8;
        if (fl & (1u << 11)) p += 12;
        if (fl & (1u << 16)) {
            const u8 *c = d + p;

            if (p + 20 > n)
                return -1;
            r->has_child = 1;
            r->child_life = rd_u16(c + 6);
            r->child_count = c[12];
            r->child_delay = c[13];
            r->child_interval = c[14];
            p += 20;
        }
        for (b = 0; b < 6; b++)
            if (fl & (1u << (24 + b)))
                p += (size_t)k_behavior_size[b];
    }
    return 0;
}

typedef struct Emitter {
    int used;
    int slot;                   /* the script's particle slot */
    const PtcRes *r;
    int age, started, frac;
    int *pa, *pl, np, cap;      /* particle ages and lives */
    int *ca, *cl, nc, ccap;     /* child particles */
} Emitter;

static void list_add(int **a, int **l, int *n, int *cap, int life)
{
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 32;

        *a = (int *)realloc(*a, (size_t)nc * sizeof(int));
        *l = (int *)realloc(*l, (size_t)nc * sizeof(int));
        *cap = nc;
    }
    (*a)[*n] = 0;
    (*l)[*n] = life;
    (*n)++;
}

/* One frame of SPL_EMITTER_UpdateParticles plus the retire test.  Returns 1 when the emitter is gone. */
static int emitter_step(Emitter *e)
{
    const PtcRes *r = e->r;
    int i, j;

    if (!e->started && e->age >= r->start_delay) {
        e->started = 1;
        e->age = 0;
    }
    if (r->life_emitter == 0 || e->age < r->life_emitter) {
        int on = r->interval ? (e->age % r->interval == 0) : (e->age == 0);

        if (on && e->started) {
            int acc = r->emit_count + e->frac;
            int total = acc >> 12;
            int life = ((r->life_particle * 255) >> 8) + 1;

            e->frac = acc & 0xFFF;
            for (i = 0; i < total; i++)
                list_add(&e->pa, &e->pl, &e->np, &e->cap, life);
        }
    }
    for (i = 0; i < e->np; i++) {
        if (r->has_child && e->pa[i] <= e->pl[i]) {
            int delay = e->pl[i] * r->child_delay * 16;
            int diff = e->pa[i] * 4096 - delay;
            int ok = diff >= 0 && (r->child_interval ? ((diff >> 12) % r->child_interval == 0) : ((diff >> 12) == 0));

            if (ok)
                for (j = 0; j < r->child_count; j++)
                    list_add(&e->ca, &e->cl, &e->nc, &e->ccap, r->child_life);
        }
        e->pa[i]++;
    }
    for (i = 0, j = 0; i < e->np; i++) {
        if (e->pa[i] <= e->pl[i]) {
            e->pa[j] = e->pa[i];
            e->pl[j] = e->pl[i];
            j++;
        }
    }
    e->np = j;
    for (i = 0; i < e->nc; i++)
        e->ca[i]++;
    for (i = 0, j = 0; i < e->nc; i++) {
        if (e->ca[i] <= e->cl[i]) {
            e->ca[j] = e->ca[i];
            e->cl[j] = e->cl[i];
            j++;
        }
    }
    e->nc = j;
    e->age++;
    return r->self_maintain && r->life_emitter != 0 && e->started && e->age > r->life_emitter
        && e->np == 0 && e->nc == 0;
}

/* ------------------------------------------------------------------ */
/* Simulator state                                                      */
/* ------------------------------------------------------------------ */

enum { TK_POS, TK_SHADOW, TK_DRAW, TK_SCALE, TK_ROT, TK_ALPHA, TK_MOSAIC, TK_BLINK, TK_SHAKE, TK_BOUNCE, TK_BG, TK_COUNT };

typedef struct Task {
    int used, cls, slot;
    Mover m;
    V3 cur;
    int a, b, c, d, e, f;       /* blink, shake and bounce counters */
} Task;

typedef struct Fade {           /* PALANM_FadeTask for the object palettes */
    int active, cur, end, dir, speed, wait, wait_count;
} Fade;

typedef struct Tint {           /* BTLV_STAGE / BTLV_FIELD palette tint */
    int enabled, cur, target, wait, interval;
} Tint;

typedef struct SpriteFade {     /* MCSS_SetPaletteFade */
    int active, cur, end, step, wait, wait_set;
} SpriteFade;

typedef struct Script {
    u8 *buf;
    size_t n;
} Script;

enum { ST_STOP, ST_RUN, ST_WAIT };
enum { W_NONE, W_IDLE, W_FRAMES, W_TASK, W_PTC };

#define MAX_SCRIPTS 24

typedef struct Sim {
    /* the archives */
    BwArcFile arc_move, arc_effect, arc_ptc, arc_waza;
    int open_move, open_effect, open_ptc, open_waza;
    PtcFile *ptc_cache[800];
    Script scripts[MAX_SCRIPTS];
    int nscripts;

    /* the vm */
    int state, wait_kind;
    const u8 *pc;
    const u8 *stack[16];
    int depth;
    int result, wait_mode, wait_frames;
    int attacker, defender;
    int script_var, scratch_var;
    int kind, variant, option, side, arg_a, arg_b, type_code;
    int type_source;
    int cam_saved;
    V3 cam_eye, cam_target;
    int camera_override, camera_offset, camera_locked, ptc_load_pending;
    int call_depth;
    int saved_att[16], saved_def[16], saved_var[16];
    int slot_key[16];           /* particle key per script slot, -1 free */
    const PtcFile *slot_file[16];
    int scene_kind;             /* mode 0: single battle */
    int done;

    /* the scene */
    Camera cam;
    int loaded[14];
    V3 pos[14];
    Task tasks[MAX_TASKS];
    Emitter emitters[MAX_EMITTERS];
    int ptc_total;
    Fade fade;
    Tint stage, field;
    SpriteFade sfade[14];
    int rotating;

    /* output */
    BcamFrame *frames;
    int nframes, cap_frames;
} Sim;

static Sim S;

/* ------------------------------------------------------------------ */
/* Archives                                                             */
/* ------------------------------------------------------------------ */

/* Archive id 0x42 is the file a/0/6/6: the decimal digits of the id. */
static void arc_path(char *out, size_t n, int id)
{
    snprintf(out, n, "a/%d/%d/%d", id / 100 % 10, id / 10 % 10, id % 10);
}

static int arc_open_once(BwArcFile *a, int *open, int id)
{
    char p[32];

    if (*open)
        return 0;
    arc_path(p, sizeof(p), id);
    if (bw_arc_open(a, p) != 0)
        return -1;
    *open = 1;
    return 0;
}

static u8 *arc_member(BwArcFile *a, int *open, int arc_id, int member, size_t *n)
{
    long sz;
    u8 *buf;

    if (arc_open_once(a, open, arc_id) != 0)
        return NULL;
    sz = bw_arc_member_size(a, (u32)member);
    if (sz <= 0)
        return NULL;
    buf = (u8 *)malloc((size_t)sz);
    if (!buf)
        return NULL;
    if (bw_arc_read_member(a, (u32)member, buf, (u32)sz) != sz) {
        free(buf);
        return NULL;
    }
    *n = (size_t)sz;
    return buf;
}

/* The script of effect `id`, kept until the run ends.  NULL when absent. */
static Script *script_load(int id)
{
    Script *s;
    u8 *buf;
    size_t n = 0;

    if (S.nscripts >= MAX_SCRIPTS)
        return NULL;
    if (id < MOVE_END)
        buf = arc_member(&S.arc_move, &S.open_move, 0x42, id, &n);
    else
        buf = arc_member(&S.arc_effect, &S.open_effect, 0x43, id - MOVE_END, &n);
    if (!buf)
        return NULL;
    s = &S.scripts[S.nscripts++];
    s->buf = buf;
    s->n = n;
    return s;
}

static const PtcFile *ptc_load(int key)
{
    u8 *buf;
    size_t n = 0;
    PtcFile *f;

    if (key < 0 || key >= 800)
        return NULL;
    if (S.ptc_cache[key])
        return S.ptc_cache[key];
    buf = arc_member(&S.arc_ptc, &S.open_ptc, 6, key, &n);
    if (!buf)
        return NULL;
    f = (PtcFile *)calloc(1, sizeof(*f));
    if (!f || ptc_parse(buf, n, f) != 0) {
        free(f);
        free(buf);
        return NULL;
    }
    f->key = key;
    free(buf);
    S.ptc_cache[key] = f;
    return f;
}

/* The target-range byte of the move record (WAZA_TOOL_GetParam(move, 0x1B)). */
static int waza_target(int move)
{
    u8 rec[0x24];

    if (arc_open_once(&S.arc_waza, &S.open_waza, 0x15) != 0)
        return 0;
    if (bw_arc_member_size(&S.arc_waza, (u32)move) < 0x24)
        return 0;
    if (bw_arc_read_member(&S.arc_waza, (u32)move, rec, sizeof(rec)) < 0x24)
        return 0;
    return rec[20];
}

/* ------------------------------------------------------------------ */
/* The scene: sprites, tasks, tints                                     */
/* ------------------------------------------------------------------ */

static V3 slot_position(int pos)
{
    if (pos >= 0 && pos <= 1)
        return ovl_v3(0x0220A050u + (u32)pos * 12u);
    if (pos >= 2 && pos <= 5)
        return ovl_v3(0x0220A080u + (u32)(pos - 2) * 12u);
    if (pos >= 6 && pos <= 7)
        return ovl_v3(0x0220A158u + (u32)(pos - 6) * 12u);
    if (pos >= 8 && pos <= 13)
        return ovl_v3(0x0220A110u + (u32)(pos - 8) * 12u);
    return (V3){ 0, 0, 0 };
}

int bcam_sim_slot(int pos, float out[3])
{
    V3 v;

    if (pos < 0 || pos > 13 || (!g_ovl && tables_load() != 0))
        return -1;
    v = slot_position(pos);
    out[0] = v.x / 4096.0f;
    out[1] = v.y / 4096.0f;
    out[2] = v.z / 4096.0f;
    return 0;
}

static void sprite_add(int pos)
{
    if (pos < 0 || pos > 13)
        return;
    S.loaded[pos] = 1;
    S.pos[pos] = slot_position(pos);
}

static int is_loaded(int pos)
{
    return pos >= 0 && pos <= 13 && S.loaded[pos];
}

static Task *task_new(int cls, int slot)
{
    int i;

    for (i = 0; i < MAX_TASKS; i++) {
        if (!S.tasks[i].used) {
            memset(&S.tasks[i], 0, sizeof(Task));
            S.tasks[i].used = 1;
            S.tasks[i].cls = cls;
            S.tasks[i].slot = slot;
            return &S.tasks[i];
        }
    }
    return NULL;
}

/* A new task replaces the running one of its kind on the same slot (the serial counters). */
static void task_supersede(int cls, int slot)
{
    int i;

    for (i = 0; i < MAX_TASKS; i++) {
        Task *t = &S.tasks[i];

        if (!t->used || t->slot != slot)
            continue;
        if (t->cls == cls
            || ((cls == TK_POS || cls == TK_SHAKE || cls == TK_BOUNCE)
                && (t->cls == TK_POS || t->cls == TK_SHAKE || t->cls == TK_BOUNCE)))
            t->used = 0;
    }
}

/* BTLV_MCSS_CreateTweenTask */
static void tween_start(int cls, int slot, int kind, const V3 *from, const V3 *to, int frames, int wait_frames, int repeat, int mirror_odd)
{
    Task *t;
    Mover *m;

    task_supersede(cls, slot);
    t = task_new(cls, slot);
    if (!t)
        return;
    m = &t->m;
    m->mode = kind;
    m->count = frames;
    m->count_reset = frames;
    m->wait = 0;
    m->wait_reset = wait_frames;
    m->repeat = repeat * 2;
    m->start = *from;
    m->end = *to;
    t->cur = *from;
    switch (kind) {
    case 0:
        break;
    case 1:
    case 4:
        vec_step(&m->start, to, &m->step, TO_FX(frames));
        break;
    case 3:
        m->repeat *= 2;
        /* fall through */
    case 2:
        m->step.x = fx_div(to->x, TO_FX(frames));
        m->step.y = fx_div(to->y, TO_FX(frames));
        m->step.z = fx_div(to->z, TO_FX(frames));
        if ((slot & 1) && mirror_odd) {
            m->step.x *= -1;
            m->step.z *= -1;
        }
        break;
    }
}

/* BTLV_MCSS_StartPositionTween */
static void pos_tween(int slot, int kind, V3 target, int frames, int wait_frames, int repeat)
{
    V3 start;

    if (!is_loaded(slot))
        return;
    start = S.pos[slot];
    if (kind == 1) {
        target.x = (slot & 1) ? start.x - target.x : target.x + start.x;
        target.y += start.y;
        target.z += start.z;
    }
    if (kind == 5) {
        target = slot_position(slot);
        kind = 1;
    }
    if (kind == 6) {
        target = slot_position(slot);
        kind = 0;
    }
    if (kind == 0) {
        S.pos[slot] = target;
        return;
    }
    tween_start(TK_POS, slot, kind, &start, &target, frames, wait_frames, repeat, 1);
}

/* The tweens of the sprite's shadow, size, rotation, alpha and mosaic: only their length matters. */
static void other_tween(int cls, int slot, int kind, V3 from, V3 to, int frames, int wait_frames, int repeat)
{
    if (!is_loaded(slot))
        return;
    tween_start(cls, slot, kind, &from, &to, frames, wait_frames, repeat, 0);
}

static void bg_scroll(int flip, int mode, int x, int y, int frames, int phase, int cycle)
{
    Task *t = task_new(TK_BG, 0);
    V3 origin = { 0, 0, 0 };
    V3 amount;
    Mover *m;

    if (!t)
        return;
    amount.x = x * 4096;
    amount.y = y * 4096;
    amount.z = 0;
    if (mode == 4 && (flip & 1))
        amount.x *= -1;
    if (mode == 1) {
        amount.x += origin.x;
        amount.y += origin.y;
    }
    m = &t->m;
    m->mode = mode;
    m->count = frames;
    m->count_reset = frames;
    m->wait = 0;
    m->wait_reset = phase;
    m->repeat = cycle * 2;
    m->start = origin;
    m->end = amount;
    t->cur = origin;
    switch (mode) {
    case 1:
        vec_step(&m->start, &amount, &m->step, TO_FX(frames));
        break;
    case 3:
        m->count_reset *= 2;
        m->repeat *= 2;
        /* fall through */
    case 2:
        m->step.x = fx_div(amount.x, TO_FX(frames));
        m->step.y = fx_div(amount.y, TO_FX(frames));
        m->step.z = fx_div(amount.z, TO_FX(frames));
        break;
    }
}

static void blink_start(int slot, int kind, int interval, int toggles)
{
    Task *t;

    if (kind != 2)
        return;
    task_supersede(TK_BLINK, slot);
    t = task_new(TK_BLINK, slot);
    if (!t)
        return;
    t->a = 0;               /* wait */
    t->b = interval;        /* reload */
    t->c = toggles * 2;     /* count */
}

static void shake_start(int slot, int mode, int steps, int interval, int cycles, int pause)
{
    Task *t;

    if (!is_loaded(slot) || steps == 0)
        return;
    task_supersede(TK_SHAKE, slot);
    t = task_new(TK_SHAKE, slot);
    if (!t)
        return;
    t->a = 0;                               /* interval counter */
    t->b = 0;                               /* pause counter */
    t->c = (mode & 1) ? 0x10000 : 0;        /* phase */
    t->d = 0x10000 / steps;                 /* phase step */
    t->e = cycles;
    t->f = mode & 1;
    t->m.count = interval;                  /* stepInterval */
    t->m.count_reset = pause;               /* cyclePause */
    t->m.wait = steps;
}

static void bounce_start(int slot, int frames)
{
    Task *t;

    if (!is_loaded(slot))
        return;
    task_supersede(TK_BOUNCE, slot);
    t = task_new(TK_BOUNCE, slot);
    if (!t)
        return;
    t->a = frames;
}

static void tasks_run(void)
{
    int i;

    for (i = 0; i < MAX_TASKS; i++) {
        Task *t = &S.tasks[i];
        int done;

        if (!t->used)
            continue;
        switch (t->cls) {
        case TK_POS:
            if (!is_loaded(t->slot)) {
                t->used = 0;
                break;
            }
            done = step_mover(&t->m, &S.pos[t->slot]);
            if (done == 1)
                t->used = 0;
            break;
        case TK_SHADOW: case TK_DRAW: case TK_SCALE: case TK_ROT: case TK_ALPHA: case TK_MOSAIC:
            done = step_mover(&t->m, &t->cur);
            if (done == 1)
                t->used = 0;
            break;
        case TK_BG:
            if (t->m.mode == 4) {
                t->cur.x += t->m.end.x;
                t->cur.y += t->m.end.y;
                if (t->m.count == 0)
                    t->used = 0;
                else
                    t->m.count--;
            } else if (step_mover(&t->m, &t->cur) == 1) {
                t->used = 0;
            }
            break;
        case TK_BLINK:
            if (t->a == 0) {
                t->a = t->b;
                if (--t->c == 0)
                    t->used = 0;
            } else {
                t->a--;
            }
            break;
        case TK_SHAKE:
            if (t->b == 0) {
                if (t->a == t->m.count) {
                    t->a = 0;
                    t->c += t->f ? -t->d : t->d;
                    if (t->c & (int)0xFFFF0000) {
                        t->c = (u16)t->c;
                        t->e -= 1;
                        t->b = t->m.count_reset;
                    }
                } else {
                    t->a += 1;
                }
            } else {
                t->b -= 1;
            }
            if (t->e == 0)
                t->used = 0;
            break;
        case TK_BOUNCE:
            if (--t->a == 0)
                t->used = 0;
            break;
        }
    }
}

static int sprite_busy(int pos)
{
    int i;

    if (S.sfade[pos].active || S.rotating)
        return 1;
    for (i = 0; i < MAX_TASKS; i++)
        if (S.tasks[i].used && S.tasks[i].slot == pos && S.tasks[i].cls != TK_BG)
            return 1;
    return 0;
}

static int bg_busy(void)
{
    int i;

    for (i = 0; i < MAX_TASKS; i++)
        if (S.tasks[i].used && S.tasks[i].cls == TK_BG)
            return 1;
    return 0;
}

/* MCSS_SetPaletteFade and its update in MCSS_Main */
static void sprite_fade_start(int pos, int start, int end, int wait)
{
    SpriteFade *f = &S.sfade[pos];

    if (!is_loaded(pos))
        return;
    f->active = 1;
    f->cur = (s32)(signed char)start;
    f->end = (s32)(signed char)end;
    f->wait = 0;
    if (wait < 0) {
        f->wait_set = 0;
        f->step = -(wait - 1);
    } else {
        f->wait_set = wait;
        f->step = 1;
    }
    if (start > end)
        f->step = -f->step;
}

static void sprite_fades_run(void)
{
    int i;

    for (i = 0; i < 14; i++) {
        SpriteFade *f = &S.sfade[i];

        if (!f->active)
            continue;
        if (f->wait != 0) {
            f->wait--;
            continue;
        }
        if (f->cur == f->end) {
            f->active = 0;
        } else {
            f->cur += f->step;
            if (f->step < 0) {
                if (f->cur <= f->end)
                    f->cur = f->end;
            } else if (f->cur >= f->end) {
                f->cur = f->end;
            }
        }
        f->wait = f->wait_set;
    }
}

/* PALANM_RequestFade for the object palettes, and the task that follows it */
static void fade_apply(void)
{
    Fade *f = &S.fade;

    if (f->cur == f->end) {
        f->active = 0;
        return;
    }
    if (!f->dir) {
        f->cur += f->speed;
        if (f->cur > f->end)
            f->cur = f->end;
    } else {
        f->cur -= f->speed;
        if (f->cur < f->end)
            f->cur = f->end;
    }
}

static void fade_request(int step, int b, int c)
{
    Fade *f = &S.fade;

    if (f->active)
        return;
    f->active = 1;
    if (step < 0) {
        int n = -step + 2;

        f->speed = n > 0x10 ? 0x10 : n;
        f->wait = 0;
    } else {
        f->speed = 2;
        f->wait = step;
    }
    f->cur = b;
    f->end = c;
    f->wait_count = f->wait;
    f->dir = b < c ? 0 : 1;
    fade_apply();
}

static void fade_task(void)
{
    Fade *f = &S.fade;

    if (!f->active)
        return;
    if (f->wait_count < f->wait) {
        f->wait_count++;
    } else {
        f->wait_count = 0;
        fade_apply();
    }
}

static void tint_start(Tint *t, int start, int target, int interval)
{
    t->enabled = 1;
    t->cur = start;
    t->target = target;
    t->wait = 0;
    t->interval = interval;
}

static void tint_run(Tint *t)
{
    if (!t->enabled)
        return;
    if (t->wait == 0) {
        t->wait = t->interval;
        if (t->cur == t->target)
            t->enabled = 0;
        else if (t->cur > t->target)
            t->cur--;
        else
            t->cur++;
    } else {
        t->wait--;
    }
}

/* BTLV_EFFECT_SetStageFieldTint */
static void stage_tint(int scope, int start, int target, int interval)
{
    interval &= 0xFF;
    if (scope == 0 || scope == 2 || scope == 4)
        tint_start(&S.stage, start & 0xFF, target & 0xFF, interval);
    if (scope == 1 || scope == 2 || scope == 4)
        tint_start(&S.field, start & 0xFF, target & 0xFF, interval);
    if (scope == 3 || scope == 4)
        fade_request((signed char)interval, start & 0xFF, target & 0xFF);
}

static int stage_busy(int scope)
{
    int x = 0, y = 0, z = 0;

    if (scope == 0 || scope == 2 || scope == 4)
        x = S.stage.enabled;
    if (scope == 1 || scope == 2 || scope == 4)
        y = S.field.enabled;
    if (scope == 3 || scope == 4)
        z = S.fade.active;
    return x || y || z;
}

/* ------------------------------------------------------------------ */
/* Particle systems of the script                                       */
/* ------------------------------------------------------------------ */

static int emitter_count(int slot)
{
    int i, n = 0;

    for (i = 0; i < MAX_EMITTERS; i++)
        if (S.emitters[i].used && S.emitters[i].slot == slot)
            n++;
    return n;
}

static void emitter_free(Emitter *e)
{
    free(e->pa); free(e->pl); free(e->ca); free(e->cl);
    memset(e, 0, sizeof(*e));
}

static void system_delete(int slot)
{
    int i;

    for (i = 0; i < MAX_EMITTERS; i++)
        if (S.emitters[i].used && S.emitters[i].slot == slot)
            emitter_free(&S.emitters[i]);
    S.slot_file[slot] = NULL;
    S.slot_key[slot] = -1;
}

/* PARTICLE_StartEffectWithCallback */
static void effect_start(int slot, int res_index)
{
    int i;
    Emitter *e = NULL;

    if (slot < 0 || slot >= 16 || !S.slot_file[slot])
        return;
    if (res_index < 0 || res_index >= S.slot_file[slot]->count)
        return;
    if (emitter_count(slot) >= EMITTER_LIMIT)
        return;
    for (i = 0; i < MAX_EMITTERS; i++) {
        if (!S.emitters[i].used) {
            e = &S.emitters[i];
            break;
        }
    }
    if (!e)
        return;
    memset(e, 0, sizeof(*e));
    e->used = 1;
    e->slot = slot;
    e->r = &S.slot_file[slot]->res[res_index];
    if (g_trace)
        fprintf(stderr, "f%d emitter %d = key %d res %d delay %d life %d part %d every %d x%d child %d/%d\n", S.nframes,
                (int)(e - S.emitters), S.slot_file[slot]->key, res_index, e->r->start_delay, e->r->life_emitter,
                e->r->life_particle, e->r->interval, e->r->emit_count >> 12, e->r->has_child, e->r->child_life);
}

static void emitters_run(void)
{
    int i;

    for (i = 0; i < MAX_EMITTERS; i++)
        if (S.emitters[i].used && emitter_step(&S.emitters[i])) {
            if (g_trace)
                fprintf(stderr, "f%d emitter %d retired\n", S.nframes, i);
            emitter_free(&S.emitters[i]);
        }
}

static int emitters_active(void)
{
    int i;

    for (i = 0; i < MAX_EMITTERS; i++)
        if (S.emitters[i].used)
            return 1;
    return 0;
}

/* BTLV_EFFVM_ResolveParticleKey: some keys pick a file by the move type colour. */
static int ptc_key(int key)
{
    int mode = S.type_source;
    unsigned idx = 4;
    int i;

    if (mode == 8) {
        for (i = 0; i < 25; i++)
            if (g_codepair[i * 2] == S.type_code) {
                idx = g_codepair[i * 2 + 1];
                break;
            }
        if (i == 25)
            idx = 0;
    }
    if (idx == 0 || idx > 0x19)
        idx = 4;
    idx--;
    switch (key) {
    case 3:
        key += (int)idx;
        break;
    case 0x1D:
        if (idx + 1 == 0x19)
            idx = 0x10;
        else if (idx + 1 >= 0x10)
            idx = 3;
        key += (int)idx;
        break;
    case 0x2E: case 0x2F: case 0x30: case 0x31:
        key += (int)(idx << 2);
        break;
    }
    return key;
}

static int slot_of_key(int key)
{
    int s;

    for (s = 0; s < 16; s++)
        if (S.slot_key[s] == key)
            return s;
    return 16;
}

/* ------------------------------------------------------------------ */
/* Battler lists (BTLV_EFFVM_ResolveBattlerList, single battle)         */
/* ------------------------------------------------------------------ */

static int resolve_list(int cmd, int *out)
{
    int count = 1;
    int pos;

    out[0] = NO_POKE;
    switch (cmd) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 12: case 13:
        out[0] = cmd;
        break;
    case 14:
        out[0] = S.attacker;
        break;
    case 15:
        out[0] = S.attacker > 1 ? (S.attacker ^ 2) : NO_POKE;
        break;
    case 16:
        out[0] = S.defender;
        if (out[0] == NO_POKE) {
            count = 0;
            if (S.kind == 8 || S.kind == 10) {
                out[0] = S.attacker;
                count = 1;
                if (is_loaded(S.attacker ^ 1)) {
                    out[1] = S.attacker ^ 1;
                    count++;
                }
            } else if (is_loaded(S.attacker ^ 1)) {
                out[0] = S.attacker ^ 1;
                count = 1;
            }
        }
        break;
    case 17:
        out[0] = S.defender > 1 ? (S.defender ^ 2) : NO_POKE;
        break;
    case 18:
        count = 0;
        for (pos = 0; pos <= 5; pos++)
            if (is_loaded(pos))
                out[count++] = pos;
        break;
    case 19: case 20:
        count = 0;
        for (pos = cmd == 19 ? 0 : 1; pos <= 5; pos += 2)
            if (is_loaded(pos))
                out[count++] = pos;
        break;
    }
    if (out[0] != NO_POKE && out[0] < 8) {
        if (!is_loaded(out[0])) {
            out[0] = NO_POKE;
            count = 0;
        }
    }
    return count;
}

/* BTLV_EFFVM_ResolveSlotKey */
static int resolve_slot(int idx)
{
    int pos = idx;

    switch (idx) {
    case 14:
        pos = S.attacker;
        break;
    case 15:
        pos = S.attacker > 1 ? (S.attacker ^ 2) : NO_POKE;
        break;
    case 16:
        pos = S.defender;
        if (pos == NO_POKE)
            pos = (S.attacker & 1) ? 0 : 1;
        break;
    case 17:
        pos = S.defender > 1 ? (S.defender ^ 2) : NO_POKE;
        break;
    }
    return pos;
}

/* The weight of the Pokemon in slots 0 and 1 (tenths of a kilogram) picks the strength of some shakes. */
static int g_weight[2] = { 1000, 1000 };

void bcam_sim_set_weight(int slot, int weight)
{
    if (slot >= 0 && slot < 2)
        g_weight[slot] = weight;
}

/* BTLV_EFFVM_EvalCondition */
static int eval_condition(unsigned key)
{
    switch (key) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        return key < 2 ? g_weight[key] : 0;
    case 8:
        return S.attacker < 2 ? g_weight[S.attacker] : 0;
    case 9:  return S.kind;
    case 10: return S.variant;
    case 11: return S.option;
    case 12: return S.side;
    case 13: return S.arg_a;
    case 14: return S.arg_b;
    case 15: return S.type_code;
    case 16: return S.script_var;
    case 17: return S.attacker;
    case 18: return 0;
    case 19: return S.attacker & 1;
    case 38: return 0;
    case 39: return S.scene_kind;
    case 53: return S.camera_offset;
    case 54: return S.cam_saved;
    case 55: return S.scratch_var;
    case 56: return S.camera_locked;
    case 57: return S.defender;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The effect VM                                                        */
/* ------------------------------------------------------------------ */

static const unsigned char k_nargs[78] = {
     5, 10,  6,  6,  2,  0,  1, 11, 15, 11, 10,  1, 11, 13, 11, 13,
    10, 10,  7,  9,  6,  7,  6,  6,  6,  4,  2,  5,  2,  2,  7,  1,
     5,  8,  2,  1,  1,  6,  4,  5,  1,  6,  5,  2,  7,  7,  7,  7,
     2,  5,  1,  2,  9,  1,  8,  9,  1,  1,  1,  4,  4,  3,  1,  1,
     2,  3,  0,  7,  1,  6,  3,  0,  1,  0,  1,  1,  1,  0
};

enum {
    OP_CAM = 0, OP_CAM_TO, OP_ORBIT, OP_EASED, OP_BILLBOARD, OP_SAVE_CAM, OP_LOAD_PTC, OP_PTC, OP_PTC_FIXED,
    OP_PTC_TGT, OP_PTC_EACH, OP_DEL_PTC, OP_PTC_OFF, OP_PTC_CONST_OFF, OP_PTC_TGT_OFF, OP_PTC_TGT_CONST_OFF,
    OP_CB_SRC, OP_CB_TGT, OP_POKE_POS, OP_SHAKE, OP_BOUNCE, OP_SHADOW_SCALE, OP_ROT, OP_ALPHA, OP_MOSAIC,
    OP_BLINK, OP_ANIM_STOP, OP_PAL_FADE, OP_VANISH, OP_SHADOW_VANISH, OP_SHADOW_SCALE2, OP_DEL_POKE,
    OP_ADD_POKE, OP_SET_POKE_POS, OP_ANIM_IDX, OP_REL_POKE, OP_LOAD_BG_PAL, OP_BG_SCROLL, OP_NOP4, OP_NOP5,
    OP_BG_LAYER, OP_BLEND, OP_STAGE_TINT, OP_STAGE_HIDE, OP_FADE, OP_CLACT_AT, OP_CLACT_POS, OP_CLACT_SCALE,
    OP_CLACT_ANIM, OP_CLACT_PAL, OP_REL_CLACT, OP_GAUGE, OP_PLAY_SE, OP_STOP_SE, OP_SE_VOL_TWEEN, OP_SE_VOL_TASK,
    OP_WAIT, OP_WAIT_FRAMES, OP_SET_RESULT, OP_JUMP_IF, OP_JUMP_IF_COND, OP_JUMP_IF_LOADED, OP_SET_VAR,
    OP_EVAL, OP_SET_PARAM, OP_CELL_FLAG, OP_COPY_SPRITE, OP_CRY, OP_TYPE_SRC, OP_CLACT_FROM_POKE, OP_CALL,
    OP_RETURN, OP_JUMP, OP_WAIT_TASK, OP_JUMP_ARCHIVE, OP_START_WAIT_TASK, OP_ANIM_MODE, OP_END
};


static int in_script(const u8 *p, size_t n)
{
    int i;

    for (i = 0; i < S.nscripts; i++)
        if (p >= S.scripts[i].buf && p + n <= S.scripts[i].buf + S.scripts[i].n)
            return 1;
    return 0;
}

/* Arguments are read as u32 and passed on as int: the game stores them in int variables. */
static int vm_arg(void)
{
    int v = 0;

    if (!in_script(S.pc, 4)) {
        S.state = ST_STOP;
        return 0;
    }
    v = (int)rd_u32(S.pc);
    S.pc += 4;
    return v;
}

/* Copies the arguments of the command that is skipped. */
static void vm_skip(int n)
{
    while (n-- > 0)
        (void)vm_arg();
}

static void vm_jump_rel(int off)
{
    S.pc += off;
}

/* The first command of variant `variant` of script `s`. */
static const u8 *script_entry(const Script *s, int variant)
{
    size_t at = 4 + (size_t)variant * 0x38;
    u32 off;

    if (s->n < at + 4)
        return NULL;
    if (variant >= s->buf[0])
        at = 4;
    off = rd_u32(s->buf + at);
    if (off >= s->n)
        return NULL;
    return s->buf + off;
}

static void vm_end(void)
{
    int i;

    for (i = 0; i < 16; i++)
        if (S.slot_file[i] || S.slot_key[i] != -1)
            system_delete(i);
    /* BTLV_EFFVM_ResetPokePositionIfLoaded: every sprite goes back to its slot */
    for (i = 0; i < 14; i++) {
        if (S.loaded[i] && i < 8)
            S.pos[i] = slot_position(i);
    }
    S.state = ST_STOP;
    S.pc = NULL;
    S.result = 1;
}

/* Preset eye and target of SetupCamera view `view` in a single battle. */
static void cam_view(int view, V3 *eye, V3 *tgt)
{
    switch (view) {
    case 0: case 1:
        *eye = ovl_v3(0x0220ACE8u + (u32)view * 12u);
        *tgt = ovl_v3(0x0220AD00u + (u32)view * 12u);
        break;
    case 2: case 3: case 4: case 5:
        *eye = ovl_v3(0x0220ADD8u + (u32)(view - 2) * 12u);
        *tgt = ovl_v3(0x0220AE20u + (u32)(view - 2) * 12u);
        break;
    case 6: case 7:
        *eye = ovl_v3(0x0220ADD8u + (u32)(view - 2) * 12u);
        *tgt = ovl_v3(0x0220AE20u + (u32)(view - 2) * 12u);
        break;
    case 13:
        *eye = S.cam_eye;
        *tgt = S.cam_target;
        break;
    case 14:
        *eye = ovl_v3(0x0220ACA0u);
        *tgt = ovl_v3(0x0220ACA0u + 24u);
        break;
    case 19:
        *eye = ovl_v3(0x0220ACA0u + 36u);
        *tgt = ovl_v3(0x0220ACA0u + 12u);
        break;
    case 18:
        *eye = ovl_v3(0x0220ACA0u + 60u);
        *tgt = ovl_v3(0x0220ACA0u + 48u);
        break;
    default:
        cam_default(eye, tgt);
        break;
    }
}

/* BTLV_EFFVM_CmdSetupCamera */
static void op_setup_camera(void)
{
    int move_type = vm_arg();
    int view = vm_arg();
    int frames = vm_arg();
    int ease_in = vm_arg();
    int ease_mode = vm_arg();
    V3 eye, tgt;

    if (S.camera_locked)
        return;
    if (view == 13 && S.cam_saved == 0)
        view = 8;
    if (view == 8 || view == 15 || view == 16 || view == 14 || view == 17)
        S.camera_override = 1;
    switch (view) {
    case 15: case 16:
        return;                             /* single battle: nothing to show */
    case 14:
        return;
    case 20:
        return;
    case 21:
        view = S.attacker;
        break;
    case 17:
        view = 8;
        break;
    case 9: case 10:
        view = S.attacker;
        break;
    case 11: case 12:
        switch (S.kind) {
        case 0: case 1: case 2: case 3:
            view = S.defender;
            if (view == NO_POKE)
                view = (S.attacker & 1) ^ 1;
            break;
        case 5: case 7: case 9: case 13:
            view = S.defender;
            if (view != NO_POKE)
                break;
            /* fall through */
        case 11:
            view = (S.attacker & 1) ^ 1;
            break;
        case 6: case 12:
            view = S.attacker & 1;
            break;
        case 4: case 8: case 10:
            view = (S.attacker & 1) ^ 1;
            break;
        }
        break;
    }
    cam_view(view, &eye, &tgt);
    if (move_type == 0)
        cam_set(&S.cam, &eye, &tgt);
    else if (move_type == 1)
        cam_start_move(&S.cam, &eye, &tgt, frames, ease_in, ease_mode);
}

static void op_camera_to(void)
{
    int move_type = vm_arg();
    V3 eye, tgt;
    int frames;
    int ease_in, ease_mode;

    eye.x = vm_arg(); eye.y = vm_arg(); eye.z = vm_arg();
    tgt.x = vm_arg(); tgt.y = vm_arg(); tgt.z = vm_arg();
    frames = vm_arg();
    ease_in = vm_arg();
    ease_mode = vm_arg();
    switch (move_type) {
    case 0:
        cam_set(&S.cam, &eye, &tgt);
        break;
    case 1:
        cam_start_move(&S.cam, &eye, &tgt, frames, ease_in, ease_mode);
        break;
    case 2:
        eye.x += S.cam.g_eye.x; eye.y += S.cam.g_eye.y; eye.z += S.cam.g_eye.z;
        tgt.x += S.cam.g_tgt.x; tgt.y += S.cam.g_tgt.y; tgt.z += S.cam.g_tgt.z;
        cam_start_move(&S.cam, &eye, &tgt, frames, ease_in, ease_mode);
        break;
    }
}

static void op_orbit(void)
{
    int move_type = vm_arg();
    int yaw = vm_arg();
    int pitch = vm_arg();
    int frames = vm_arg();
    int ease_in = vm_arg();
    int ease_mode = vm_arg();
    V3 eye, tgt;

    if (move_type == 0) {
        cam_rotate_orbit(&S.cam, yaw, pitch);
    } else if (move_type == 1) {
        cam_advance_orbit(&S.cam, yaw, pitch, &eye, &tgt);
        cam_start_move(&S.cam, &eye, &tgt, frames, ease_in, ease_mode);
    }
}

static void op_eased(void)
{
    int mode = vm_arg();
    int amount = vm_arg();
    int unused = vm_arg();
    int frames = vm_arg();
    int ease_in = vm_arg();
    int speed = vm_arg();

    (void)unused;
    cam_start_shake(&S.cam, mode, amount, frames, ease_in, speed);
}

/* Starts `n` emitters of resource `res` in the system of key `key`. */
static void ptc_start(int key_arg, int res, int count)
{
    int slot = slot_of_key(ptc_key(key_arg));
    int i;

    if (slot == 16)
        return;
    for (i = 0; i < count; i++)
        effect_start(slot, res);
}

/* How many emitters a source/destination pair of "all targets" keys starts in a single battle. */
static int ptc_instances(int src, int dst)
{
    if (dst == src && (src == 11 || src == 12)) {
        if (S.defender != NO_POKE)
            return 1;
        if (S.kind == 8 || S.kind == 10)
            return 1 + (is_loaded(S.attacker ^ 1) ? 1 : 0);
        return is_loaded(S.attacker ^ 1) ? 1 : 0;
    }
    return 1;
}

static void op_tween_args(int op)
{
    int ids[8];
    int n = resolve_list(vm_arg(), ids);
    int i;
    int kind, frames, wait, rep;
    V3 t = { 0, 0, 0 };
    V3 zero = { 0, 0, 0 };

    switch (op) {
    case OP_POKE_POS:
        kind = vm_arg(); t.x = vm_arg(); t.y = vm_arg(); t.z = 0;
        frames = vm_arg(); wait = vm_arg(); rep = vm_arg();
        for (i = 0; i < n; i++)
            pos_tween(ids[i], kind, t, frames, wait, rep);
        break;
    case OP_SHADOW_SCALE: case OP_SHADOW_SCALE2: {
        V3 one = { 0x1000, 0x1000, 0x1000 };

        kind = vm_arg(); t.x = vm_arg(); t.y = vm_arg(); t.z = 0x1000;
        frames = vm_arg(); wait = vm_arg(); rep = vm_arg();
        for (i = 0; i < n; i++)
            other_tween(op == OP_SHADOW_SCALE ? TK_SHADOW : TK_DRAW, ids[i], kind, one, t, frames, wait, rep);
        break;
    }
    case OP_ROT:
        kind = vm_arg(); t.z = vm_arg();
        frames = vm_arg(); wait = vm_arg(); rep = vm_arg();
        for (i = 0; i < n; i++)
            other_tween(TK_ROT, ids[i], kind, zero, t, frames, wait, rep);
        break;
    case OP_ALPHA: case OP_MOSAIC: {
        V3 from, to;
        int v;

        kind = vm_arg(); v = vm_arg();
        frames = vm_arg(); wait = vm_arg(); rep = vm_arg();
        from.x = (op == OP_ALPHA ? 31 : 0) * 4096; from.y = from.z = 0;
        to.x = v * 4096; to.y = to.z = 0;
        for (i = 0; i < n; i++)
            other_tween(op == OP_ALPHA ? TK_ALPHA : TK_MOSAIC, ids[i], kind, from, to, frames, wait, rep);
        break;
    }
    }
}

/* One command.  Returns 1 when the frame ends. */
static int vm_exec(int op)
{
    int a, b, c, d, i;

    switch (op) {
    case OP_CAM:        op_setup_camera(); return S.result;
    case OP_CAM_TO:     op_camera_to();    return S.result;
    case OP_ORBIT:      op_orbit();        return S.result;
    case OP_EASED:      op_eased();        return S.result;
    case OP_SAVE_CAM:
        S.cam_saved = 1;
        S.cam_eye = S.cam.g_eye;
        S.cam_target = S.cam.g_tgt;
        return S.result;
    case OP_LOAD_PTC: {
        int key = ptc_key(vm_arg());
        int slot = slot_of_key(key);
        int isnew = slot == 16;

        if (isnew) {
            for (slot = 0; slot < 16; slot++)
                if (S.slot_key[slot] == -1) {
                    S.slot_key[slot] = key;
                    break;
                }
            if (slot < 16) {
                S.slot_file[slot] = ptc_load(key);
                if (!S.slot_file[slot])
                    S.slot_key[slot] = -1;
                else
                    S.ptc_load_pending = 1;
            }
        }
        S.state = ST_WAIT;
        S.wait_kind = W_PTC;
        return 1;
    }
    case OP_PTC:
        a = vm_arg(); b = vm_arg(); c = vm_arg(); d = vm_arg(); vm_skip(7);
        if (d == 8)
            d = c;
        ptc_start(a, b, ptc_instances(c, d));
        return S.result;
    case OP_PTC_FIXED:
        a = vm_arg(); b = vm_arg(); vm_skip(13);
        ptc_start(a, b, 1);
        return S.result;
    case OP_PTC_TGT:
        a = vm_arg(); b = vm_arg(); c = vm_arg(); d = vm_arg(); vm_skip(7);
        if (c == 13)
            c = 9;
        if (d == 8)
            d = c;
        ptc_start(a, b, ptc_instances(c, d));
        return S.result;
    case OP_PTC_EACH: {
        int slot;

        a = vm_arg(); c = vm_arg(); d = vm_arg(); vm_skip(7);
        slot = slot_of_key(ptc_key(a));
        if (slot != 16 && S.slot_file[slot]) {
            if (d == 8)
                d = c;
            for (i = 0; i < S.slot_file[slot]->count; i++)
                ptc_start(a, i, ptc_instances(c, d));
        }
        return S.result;
    }
    case OP_DEL_PTC: {
        int slot = slot_of_key(ptc_key(vm_arg()));

        if (slot != 16)
            system_delete(slot);
        return S.result;
    }
    case OP_PTC_OFF: case OP_PTC_TGT_OFF:
        a = vm_arg(); b = vm_arg(); vm_skip(9);
        ptc_start(a, b, 1);
        return S.result;
    case OP_PTC_CONST_OFF: case OP_PTC_TGT_CONST_OFF:
        a = vm_arg(); b = vm_arg(); vm_skip(11);
        ptc_start(a, b, 1);
        return S.result;
    case OP_CB_SRC: case OP_CB_TGT:
        a = vm_arg(); b = vm_arg(); vm_skip(8);
        ptc_start(a, b, 1);
        return S.result;
    case OP_POKE_POS: case OP_SHADOW_SCALE: case OP_SHADOW_SCALE2: case OP_ROT: case OP_ALPHA: case OP_MOSAIC:
        op_tween_args(op);
        return S.result;
    case OP_SHAKE: {
        int ids[8];
        int n = resolve_list(vm_arg(), ids);
        int mode = vm_arg(), wave = vm_arg(), ax = vm_arg(), ay = vm_arg();
        int steps = vm_arg() >> 12, interval = vm_arg() >> 12, cycles = vm_arg() >> 12, pause = vm_arg();

        (void)wave; (void)ax; (void)ay;
        if (n != 0 && cycles != 0)
            for (i = 0; i < n; i++)
                shake_start(ids[i], mode, steps, interval, cycles, pause);
        return S.result;
    }
    case OP_BOUNCE: {
        int ids[8];
        int n = resolve_list(vm_arg(), ids);

        vm_skip(4);
        a = vm_arg();
        for (i = 0; i < n; i++)
            bounce_start(ids[i], a);
        return S.result;
    }
    case OP_BLINK: {
        int ids[8];
        int n = resolve_list(vm_arg(), ids);
        int kind = vm_arg(), interval = vm_arg(), toggles = vm_arg();

        if (n != 0) {
            if (toggles == 0)
                toggles = 1;
            for (i = 0; i < n; i++)
                if (is_loaded(ids[i]))
                    blink_start(ids[i], kind, interval, toggles);
        }
        return S.result;
    }
    case OP_PAL_FADE: {
        int ids[8];
        int n = resolve_list(vm_arg(), ids);
        int cur = vm_arg(), target = vm_arg(), step = vm_arg();

        (void)vm_arg();
        for (i = 0; i < n; i++)
            sprite_fade_start(ids[i], cur & 0xFF, target & 0xFF, (signed char)step);
        return S.result;
    }
    case OP_DEL_POKE: {
        int ids[8];
        int n = resolve_list(vm_arg(), ids);

        for (i = 0; i < n; i++)
            if (ids[i] >= 0 && ids[i] < 14)
                S.loaded[ids[i]] = 0;
        return S.result;
    }
    case OP_ADD_POKE: {
        vm_skip(1);
        a = resolve_slot(vm_arg());
        b = vm_arg(); c = vm_arg(); d = vm_arg();
        sprite_add(a);
        if (a >= 0 && a < 14 && (b || c || d)) {
            S.pos[a].x = b;
            S.pos[a].y = c;
            S.pos[a].z = d;
        }
        return S.result;
    }
    case OP_SET_POKE_POS: {
        V3 t;
        int pos = vm_arg();
        int kind = vm_arg();
        int frames, wait, rep;

        t.x = vm_arg(); t.y = vm_arg(); t.z = vm_arg();
        frames = vm_arg(); wait = vm_arg(); rep = vm_arg();
        if (g_trace)
            fprintf(stderr, "  set pos %d kind %d to %d %d %d in %d\n", pos, kind, t.x, t.y, t.z, frames);
        if (pos >= 8 && pos < 14 && is_loaded(pos))
            pos_tween(pos, kind, t, frames, wait, rep);
        return S.result;
    }
    case OP_REL_POKE:
        a = vm_arg();
        if (a >= 8 && a < 14)
            S.loaded[a] = 0;
        return S.result;
    case OP_BG_SCROLL: {
        int mode = vm_arg(), x = vm_arg(), y = vm_arg(), frames = vm_arg(), phase = vm_arg(), cycle = vm_arg();

        bg_scroll(resolve_slot(14), mode, x, y, frames, phase, cycle);
        return S.result;
    }
    case OP_STAGE_TINT: {
        int layer = vm_arg(), s = vm_arg(), t = vm_arg(), step = vm_arg();

        (void)vm_arg();
        stage_tint(layer, s, t, step);
        return S.result;
    }
    case OP_WAIT:
        S.wait_mode = vm_arg();
        S.state = ST_WAIT;
        S.wait_kind = W_IDLE;
        S.result = 1;
        return 1;
    case OP_WAIT_FRAMES:
        S.wait_frames = vm_arg();
        S.state = ST_WAIT;
        S.wait_kind = W_FRAMES;
        S.result = 1;
        return 1;
    case OP_SET_RESULT:
        S.result = vm_arg();
        return S.result;
    case OP_JUMP_IF: case OP_JUMP_IF_COND: {
        int lhs = eval_condition((unsigned)vm_arg());
        int opc = vm_arg();
        int rhs = op == OP_JUMP_IF ? vm_arg() : eval_condition((unsigned)vm_arg());
        int off = vm_arg();
        int cond = 0;

        switch (opc) {
        case 0: cond = lhs == rhs; break;
        case 1: cond = lhs != rhs; break;
        case 2: cond = lhs < rhs; break;
        case 3: cond = lhs > rhs; break;
        case 4: cond = lhs <= rhs; break;
        case 5: cond = lhs >= rhs; break;
        }
        if (cond)
            vm_jump_rel(off);
        return S.result;
    }
    case OP_JUMP_IF_LOADED:
        a = vm_arg(); b = vm_arg(); c = vm_arg();
        if (b == (is_loaded(a) ? 1 : 0))
            vm_jump_rel(c);
        return S.result;
    case OP_SET_VAR:
        S.script_var = vm_arg();
        return S.result;
    case OP_EVAL:
        S.script_var = eval_condition((unsigned)vm_arg());
        return S.result;
    case OP_SET_PARAM:
        a = vm_arg(); b = vm_arg();
        switch (a) {
        case 9:  S.kind = b; break;
        case 10: S.variant = b; break;
        case 11: S.option = b; break;
        case 12: S.side = b; break;
        case 13: S.arg_a = b; break;
        case 14: S.arg_b = b; break;
        case 15: S.type_code = b; break;
        case 0x35: S.camera_offset = b & 1; break;
        case 0x36: S.cam_saved = b; break;
        case 0x37: S.scratch_var = b; break;
        case 0x38: S.camera_locked = b & 1; break;
        }
        return S.result;
    case OP_TYPE_SRC:
        S.type_source = vm_arg();
        return S.result;
    case OP_CALL: {
        int id = vm_arg(), natt = vm_arg(), ndef = vm_arg();
        Script *s;
        const u8 *entry;

        if (S.call_depth >= 16 || S.depth + 1 >= 16 || id < MOVE_END)
            return S.result;
        S.saved_att[S.call_depth] = S.attacker;
        S.saved_def[S.call_depth] = S.defender;
        S.saved_var[S.call_depth] = S.script_var;
        if (natt != 14)
            S.attacker = natt;
        if (ndef != 16)
            S.defender = ndef;
        S.script_var = 0;
        s = script_load(id);
        entry = s ? script_entry(s, 0) : NULL;
        S.call_depth++;
        S.stack[S.depth++] = S.pc;
        S.pc = entry;
        if (!entry)
            S.state = ST_STOP;
        return S.result;
    }
    case OP_RETURN:
        if (S.call_depth > 0) {
            S.call_depth--;
            S.attacker = S.saved_att[S.call_depth];
            S.defender = S.saved_def[S.call_depth];
            S.script_var = S.saved_var[S.call_depth];
        }
        S.pc = S.depth > 0 ? S.stack[--S.depth] : NULL;
        if (!S.pc)
            S.state = ST_STOP;
        return S.result;
    case OP_JUMP:
        vm_jump_rel(vm_arg());
        return S.result;
    case OP_JUMP_ARCHIVE: {
        int id = vm_arg();
        Script *s = script_load(id);
        const u8 *entry = s ? script_entry(s, 0) : NULL;

        S.script_var = 0;
        S.pc = entry;
        if (!entry)
            S.state = ST_STOP;
        return S.result;
    }
    case OP_WAIT_TASK:
        S.state = ST_WAIT;
        S.wait_kind = W_TASK;
        S.result = 1;
        return 1;
    case OP_END:
        vm_end();
        return 1;
    default:
        vm_skip(k_nargs[op]);
        return S.result;
    }
}

/* The wait callbacks (BTLV_EFFVM_IsAllIdle and friends).  Returns 1 when the script may go on. */
static int wait_done(void)
{
    int pos;

    switch (S.wait_kind) {
    case W_FRAMES:
        return --S.wait_frames <= 0;
    case W_PTC:
        return !S.ptc_load_pending;
    case W_TASK:
        return 1;
    case W_IDLE:
        break;
    default:
        return 1;
    }
    if (S.wait_mode <= 1) {
        if (S.cam.flags != 0)
            return 0;
        if (S.camera_override)
            S.camera_override = 0;
    }
    if (S.wait_mode == 0 || S.wait_mode == 3)
        for (pos = 0; pos < 14; pos++)
            if (sprite_busy(pos))
                return 0;
    if (S.wait_mode == 0 || S.wait_mode == 5)
        if (bg_busy())
            return 0;
    if (S.wait_mode == 0 || S.wait_mode == 2)
        if (emitters_active())
            return 0;
    if (S.wait_mode == 0 || S.wait_mode == 6 || S.wait_mode == 8)
        if (stage_busy(0))
            return 0;
    if (S.wait_mode == 0 || S.wait_mode == 7 || S.wait_mode == 8)
        if (stage_busy(1))
            return 0;
    if (S.wait_mode == 0 || S.wait_mode == 9)
        if (stage_busy(3))
            return 0;
    return 1;
}

/* VM_Control */
static void vm_control(void)
{
    if (S.state == ST_STOP)
        return;
    if (S.state == ST_WAIT) {
        if (wait_done()) {
            S.wait_kind = W_NONE;
            S.state = ST_RUN;
        }
        return;
    }
    for (;;) {
        unsigned op;

        if (!S.pc || !in_script(S.pc, 2)) {
            S.state = ST_STOP;
            return;
        }
        op = rd_u16(S.pc);
        S.pc += 2;
        if (op >= 78) {
            S.state = ST_STOP;
            return;
        }
        if (g_trace)
            fprintf(stderr, "f%d op %u\n", S.nframes, op);
        if (vm_exec((int)op) == 1)
            return;
        if (S.state == ST_STOP)
            return;
    }
}

/* ------------------------------------------------------------------ */
/* Frames                                                               */
/* ------------------------------------------------------------------ */

static void frame_record(void)
{
    BcamFrame *f;
    int i;

    if (S.nframes == S.cap_frames) {
        int nc = S.cap_frames ? S.cap_frames * 2 : 512;
        BcamFrame *nf = (BcamFrame *)realloc(S.frames, (size_t)nc * sizeof(BcamFrame));

        if (!nf)
            return;
        S.frames = nf;
        S.cap_frames = nc;
    }
    f = &S.frames[S.nframes++];
    memset(f, 0, sizeof(*f));
    f->eye[0] = S.cam.g_eye.x / 4096.0f;
    f->eye[1] = S.cam.g_eye.y / 4096.0f;
    f->eye[2] = S.cam.g_eye.z / 4096.0f;
    f->target[0] = S.cam.g_tgt.x / 4096.0f;
    f->target[1] = S.cam.g_tgt.y / 4096.0f;
    f->target[2] = S.cam.g_tgt.z / 4096.0f;
    for (i = 0; i < 8; i++) {
        if (S.loaded[i]) {
            f->poke[i][0] = S.pos[i].x / 4096.0f;
            f->poke[i][1] = S.pos[i].y / 4096.0f;
            f->poke[i][2] = S.pos[i].z / 4096.0f;
            f->poke_mask |= 1u << i;
        }
    }
    for (i = 0; i < 6; i++) {
        if (S.loaded[8 + i]) {
            f->trainer[i][0] = S.pos[8 + i].x / 4096.0f;
            f->trainer[i][1] = S.pos[8 + i].y / 4096.0f;
            f->trainer[i][2] = S.pos[8 + i].z / 4096.0f;
            f->trainer_mask |= 1u << i;
        }
    }
}

/* One BTLV_EFFECT_Main. */
static void frame_run(void)
{
    vm_control();
    if (S.ptc_load_pending)
        S.ptc_load_pending = 0;     /* BTLV_EFFVM_ParticleLoadTaskMain finishes in the frame it is made */
    fade_task();
    tasks_run();
    sprite_fades_run();
    tint_run(&S.stage);
    tint_run(&S.field);
    cam_step(&S.cam);
    emitters_run();
    frame_record();
}

static void sim_reset(void)
{
    int i;

    for (i = 0; i < S.nscripts; i++)
        free(S.scripts[i].buf);
    for (i = 0; i < MAX_EMITTERS; i++)
        if (S.emitters[i].used)
            emitter_free(&S.emitters[i]);
    for (i = 0; i < 800; i++) {
        free(S.ptc_cache[i]);
        S.ptc_cache[i] = NULL;
    }
    if (S.open_move)   bw_arc_close(&S.arc_move);
    if (S.open_effect) bw_arc_close(&S.arc_effect);
    if (S.open_ptc)    bw_arc_close(&S.arc_ptc);
    if (S.open_waza)   bw_arc_close(&S.arc_waza);
    free(S.frames);
    memset(&S, 0, sizeof(S));
    for (i = 0; i < 16; i++)
        S.slot_key[i] = -1;
}

/* Starts script `id` (BTLV_EFFVM_StartScript).  Returns 0 on success. */
static int sim_start(int id, int attacker, int defender, int kind)
{
    Script *s = script_load(id);
    const u8 *entry;

    if (!s)
        return -1;
    entry = script_entry(s, S.variant);
    if (!entry)
        return -1;
    S.attacker = attacker;
    S.defender = defender;
    S.kind = kind;
    S.script_var = 0;
    S.type_source = 8;
    S.result = 1;
    S.depth = 0;
    S.call_depth = 0;
    S.pc = entry;
    S.state = ST_RUN;
    S.wait_kind = W_NONE;
    return 0;
}

/* Runs until the script ends (plus the frame that notices it). */
static void sim_play(void)
{
    while (S.state != ST_STOP && S.nframes < MAX_FRAMES)
        frame_run();
}

static void sim_scene_init(void)
{
    cam_init(&S.cam);
    sprite_add(0);
    sprite_add(1);
}

/*
 * The entry scenes are a chain of effects the battle scene unit starts one after the
 * other (btlv_scu.c, BTLV_SCU_ProcEntrySingleWild and ...Trainer).  Between two effects
 * the game shows a message; the camera holds, and the simulator adds no frames for it.
 * `loaded` is the set of slots on the field when the effect starts.  With id >= 0 the
 * steps before id play first, so the camera is where the game leaves it, and the run
 * returns the last frame before step id followed by the frames of step id.
 */
typedef struct EntryStep {
    int effect;             /* offset from the first non-move effect */
    int attacker;           /* 0xFF: the effect has no user */
    unsigned loaded;        /* slot bit mask: bit 9 the foe trainer, bit 1 the foe, bit 0 the player's Pokemon */
} EntryStep;

static const EntryStep k_wild[] = {
    { 0, 1, 1u << 1 },
    { 1, NO_POKE, 1u << 1 },
    { 3, NO_POKE, (1u << 1) | (1u << 0) },
};

static const EntryStep k_trainer[] = {
    { 6, 1, 1u << 9 },
    { 7, 1, 1u << 9 },
    { 8, 1, 1u << 9 },
    { 1, NO_POKE, 1u << 1 },
    { 3, NO_POKE, (1u << 1) | (1u << 0) },
};

/* a trainer's message mid-battle (btl_client.c): the trainer slides in
 * (effect 63), speaks, slides out (64); both Pokemon stay on the field */
static const EntryStep k_mid[] = {
    { 63, NO_POKE, (1u << 1) | (1u << 0) },
    { 64, NO_POKE, (1u << 1) | (1u << 0) | (1u << 9) },
};

/* gBtlvIntroPickTableB: the six camera moves of the command wait (BTLV_EFFECT_UpdateIntroInput) */
#define IDLE_TABLE_B 0x0220AC78u
#define IDLE_MOVES   6

static int play_effect(int effect, int attacker, int defender, int kind)
{
    int rc = sim_start(effect, attacker, defender, kind);

    if (rc == 0)
        sim_play();
    return rc;
}

static void scene_set(unsigned loaded)
{
    int i;

    for (i = 0; i < 14; i++) {
        int want = (loaded >> i) & 1;

        if (want && !S.loaded[i])
            sprite_add(i);
        else if (!want)
            S.loaded[i] = 0;
    }
}

static int play_entry(const EntryStep *steps, int n, int id)
{
    int i, rc = 0;

    for (i = 0; i < n && rc == 0; i++) {
        if (id >= 0 && i > id)
            break;
        if (id >= 0 && i == id && S.nframes > 1) {
            /* the steps before only bring the camera to where this one starts: keep their last frame */
            S.frames[0] = S.frames[S.nframes - 1];
            S.nframes = 1;
        }
        scene_set(steps[i].loaded);
        rc = play_effect(MOVE_END + steps[i].effect, steps[i].attacker, NO_POKE, 0);
    }
    return rc;
}

int bcam_sim_run(int kind, int id, int attacker, int defender, BcamFrame **frames, int *count)
{
    int rc = 0;
    int i;

    g_trace = getenv("BCAM_TRACE") != NULL;
    *frames = NULL;
    *count = 0;
    if (!g_ovl || !g_sincos) {
        if (tables_load() != 0)
            return -1;
    }
    sim_reset();
    sim_scene_init();
    switch (kind) {
    case BCAM_MOVE:
        if (id <= 0 || id >= MOVE_END)
            return -2;
        rc = play_effect(id, attacker & 1, defender, waza_target(id));
        break;
    case BCAM_WILD_INTRO:
        rc = play_entry(k_wild, (int)(sizeof(k_wild) / sizeof(k_wild[0])), id);
        break;
    case BCAM_TRAINER_INTRO:
        rc = play_entry(k_trainer, (int)(sizeof(k_trainer) / sizeof(k_trainer[0])), id);
        break;
    case BCAM_TRAINER_MID:
        rc = play_entry(k_mid, (int)(sizeof(k_mid) / sizeof(k_mid[0])), id);
        break;
    case BCAM_IDLE:
        for (i = 0; i < IDLE_MOVES && rc == 0; i++) {
            u32 effect;

            if (id >= 0 && id != i)
                continue;
            memcpy(&effect, g_ovl + (IDLE_TABLE_B - OVL94_BASE + (u32)i * 4u), 4);
            rc = play_effect((int)effect, NO_POKE, NO_POKE, 0);
        }
        break;
    default:
        rc = -3;
        break;
    }
    if (rc == 0) {
        *frames = S.frames;
        *count = S.nframes;
        S.frames = NULL;
        S.nframes = S.cap_frames = 0;
    }
    return rc;
}

/*
 * The move names are message bank 203 of a/0/0/2.  A bank is a header (language
 * count, string count, size, 0, one offset per language), then 8 bytes per string
 * (offset from the language block, length in 16-bit units) and the strings, each
 * unit XORed with a key that starts at (index + 3) * 0x2983 and rotates left by 3
 * after every unit (MSGDATA_DecryptUnits).  The names are plain characters.
 */
#define NAME_MAX_MOVES 700
#define NAME_LEN       24

static char g_names[NAME_MAX_MOVES][NAME_LEN];
static int  g_names_count = -1;

static void names_load(void)
{
    BwArcFile arc;
    long sz;
    u8 *d;
    u32 base;
    int cnt, i;

    g_names_count = 0;
    if (bw_arc_open(&arc, "a/0/0/2") != 0)
        return;
    sz = bw_arc_member_size(&arc, 203);
    d = sz > 16 ? (u8 *)malloc((size_t)sz) : NULL;
    if (d && bw_arc_read_member(&arc, 203, d, (u32)sz) == sz) {
        cnt = rd_u16(d + 2);
        base = rd_u32(d + 12);
        if (cnt > NAME_MAX_MOVES)
            cnt = NAME_MAX_MOVES;
        for (i = 0; i < cnt && (size_t)base + 12 + (size_t)i * 8 <= (size_t)sz; i++) {
            const u8 *e = d + base + 4 + i * 8;
            u32 off = rd_u32(e);
            int len = rd_u16(e + 4), j, n = 0;
            u16 key = (u16)((i + 3) * 0x2983);

            for (j = 0; j < len && (size_t)base + off + (size_t)j * 2 + 2 <= (size_t)sz; j++) {
                u16 c = (u16)(rd_u16(d + base + off + j * 2) ^ key);

                key = (u16)(((key & 0xE000) >> 13) | (u16)(key << 3));
                if (c == 0xFFFF)
                    break;
                if (n < NAME_LEN - 1)
                    g_names[i][n++] = c == 0x2019 ? 0x27 : (c >= 32 && c < 127 ? (char)c : '?');
            }
            g_names[i][n] = 0;
        }
        g_names_count = cnt;
    }
    free(d);
    bw_arc_close(&arc);
}

const char *bcam_sim_move_name(int id)
{
    if (g_names_count < 0)
        names_load();
    if (id <= 0 || id >= g_names_count || !g_names[id][0])
        return NULL;
    return g_names[id];
}
