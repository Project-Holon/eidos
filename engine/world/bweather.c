/*
 * engine/world/bweather.c -- the field's weather, replayed (bweather.h).
 *
 * What is mirrored, with the game's source beside each part (aletheia
 * src/game):
 *   - the weather task's particle ring and spawner: ov021/weather_task.c
 *     (WEATHER_TASK_AddParticle, _LinkParticle, _UnlinkParticle,
 *     _UpdateParticles, _InitSpawner, _TickSpawnTimer, _PrewarmParticles,
 *     _SetParticlePosWrapped);
 *   - snow: ov025/weather_snow_blizzard_downpour_diamond_dust.c
 *     (WEATHER_SNOW_Begin, _BeginPlain, _Run, _UpdateParticle,
 *     _SpawnParticles), its sway table gSnowWobbleOffsetX (overlay 25,
 *     0x021EF790, 337 signed bytes);
 *   - the sandstorm: ov027/weather_sandstorm.c (WEATHER_SANDSTORM_Begin,
 *     _BeginPlain, _Run, _UpdateParticle, _SpawnParticles), its gust tables
 *     gSandstormGustSpeedX / Y (overlay 27, 0x021EEB48 / 0x021EEB68, 8 ints
 *     each).
 * A battle begins with the weather already on, so a kind starts as the
 * field starts it on entering such a zone: begin, then "begin plain" (the
 * screen filled at once), then run.  No camera scroll: the field shifts the
 * sprites as its camera follows the player; a battle's camera is not that
 * one.
 *
 * The sprites: the kind's resources in a/0/5/6 (WeatherKindDef: character,
 * palette, cell bank and cell animation; snow 23 24 22 21, the sandstorm
 * 34 35 33 32).  A particle's "animation index" (CLACT_SetActorAnimIndex)
 * picks one element of the bank's one sequence: a cell of one 8 x 8 OBJ.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bweather.h"
#include "g2d.h"
#include "port.h"
#include "types.h"

#define P_MAX 32                /* WEATHER_PARTICLE_MAX */
#define IMG_MAX 8
#define OVL_BASE 0x021EE740u    /* overlays 24..28 load here */

enum { KIND_NONE, KIND_SNOW, KIND_SAND };

typedef struct Part {
    int linked, next, prev;     /* the ring, by index */
    s16 x, y;                   /* the actor's place on the screen */
    int image;                  /* its animation index */
    int w[6];                   /* the kind's own data */
} Part;

/* (snow: WeatherSnowFlake) */
enum { SN_FALL_ACCUM, SN_WOBBLE, SN_DYING, SN_DYING_FRAMES, SN_FALL_THRESHOLD };
/* (sandstorm: WeatherSandstormGrain) */
enum { SD_AGE, SD_LIFETIME, SD_SPEED_Y, SD_EXPIRED, SD_SPEED_X, SD_SPEED_X_STEP };

static struct {
    volatile int want;          /* the id asked for (bweather_want), -1 none */
    int id, kind;               /* what plays */
    Part p[P_MAX];
    int head, count;
    struct { int count, timer, interval; } sp;      /* the spawner (its run stage: no ramp) */
    int stagger, ending;        /* snow: WeatherSnowWork */
    int gust;                   /* the sandstorm: WeatherSandstormWork.gustCounter */
    u64 seed;
    unsigned last;              /* the field frame of the last tick */
    int ticked;
    signed char wobble[337];
    int gx[8], gy[8];
    struct { int w, h; u8 *rgba; } img[IMG_MAX];
    int nimg;
} W = { .want = -1, .id = -1 };

/* GFL_USE_GetRandom: a 64 bit linear congruential step, the high word scaled to the range (0: the whole word). */
static u32 rnd(u32 range)
{
    W.seed = W.seed * 0x5D588B656C078965ull + 0x269EC3ull;
    return range == 0u ? (u32)(W.seed >> 32) : (u32)(((W.seed >> 32) * range) >> 32);
}

/* ---- the game's data ---- */

static u8 *overlay(int id, size_t *n)
{
    static const char *const dirs[] = { "overlays_dec", "overlays" };
    u8 *d = NULL;
    int i;

    for (i = 0; i < 2 && d == NULL; i++) {
        char path[1024];
        FILE *f;
        long sz;

        snprintf(path, sizeof path, "%s/../%s/y9_%04d.bin", bw_data_root(), dirs[i], id);
        f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && (d = (u8 *)malloc((size_t)sz)) != NULL) {
            if (fread(d, 1, (size_t)sz, f) != (size_t)sz) {
                free(d);
                d = NULL;
            } else {
                *n = (size_t)sz;
            }
        }
        fclose(f);
    }
    return d;
}

static int rd_s32(const u8 *p)
{
    return (int)((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24));
}

static void images_free(void)
{
    int i;

    for (i = 0; i < IMG_MAX; i++) {
        free(W.img[i].rgba);
        W.img[i].rgba = NULL;
    }
    W.nimg = 0;
}

/* The kind's sprite pictures: each element of the cell animation's one sequence, its cell's OBJ in the palette. */
static int images_load(u32 m_char, u32 m_pal, u32 m_cell, u32 m_anim)
{
    BwArcFile arc;
    u8 *d[4] = { NULL, NULL, NULL, NULL };
    u32 n[4] = { 0, 0, 0, 0 };
    const u32 m[4] = { m_char, m_pal, m_cell, m_anim };
    G2dNcgr ncgr;
    G2dNcer ncer;
    G2dNanr nanr;
    G2dPalette pal;
    int ok = 0, have = 0, i;

    memset(&ncgr, 0, sizeof ncgr);
    memset(&ncer, 0, sizeof ncer);
    memset(&nanr, 0, sizeof nanr);
    memset(&pal, 0, sizeof pal);
    images_free();
    if (bw_arc_open(&arc, "a/0/5/6") != 0) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        d[i] = g2d_arc_member(&arc, m[i], &n[i]);
    }
    bw_arc_close(&arc);
    if (d[0] != NULL && d[1] != NULL && d[2] != NULL && d[3] != NULL && g2d_ncgr_parse(&ncgr, d[0], n[0]) == 0) {
        have |= 1;
        if (g2d_palette_append_nclr(&pal, d[1], n[1]) == 0 && g2d_ncer_parse(&ncer, d[2], n[2]) == 0) {
            have |= 2;
            if (g2d_nanr_parse(&nanr, d[3], n[3]) == 0) {
                have |= 4;
            }
        }
    }
    if (have == 7 && nanr.num_sequences >= 1u && ncgr.pixel_fmt == G2D_NCGR_FMT_4BPP && ncgr.tile_data != NULL) {
        const unsigned nf = nanr.sequences[0].num_frames;
        unsigned k;

        for (k = 0; k < nf && k < IMG_MAX; k++) {
            G2dFrame fr;
            const G2dOam *o;
            u32 x, y, block0;
            u8 *px;

            if (g2d_nanr_frame_data(&nanr, 0, k, &fr) != 0 || fr.cell_idx >= ncer.num_cells || ncer.cells[fr.cell_idx].num_oam < 1u) {
                break;
            }
            o = &ncer.cells[fr.cell_idx].oams[0];
            px = (u8 *)calloc((size_t)o->w * o->h, 4);
            if (px == NULL) {
                break;
            }
            /* (1D mapping: the OBJ's tile number counts units of 32 << mapping bytes; 8 x 8 blocks in order) */
            block0 = ncer.mapping_type < 4u ? (u32)o->tile_idx << ncer.mapping_type : o->tile_idx;
            for (y = 0; y < o->h; y++) {
                for (x = 0; x < o->w; x++) {
                    const u32 block = block0 + (y / 8u) * (o->w / 8u) + x / 8u;
                    const u32 off = block * 32u + (y % 8u) * 4u + (x % 8u) / 2u;
                    const u32 sx = o->hflip ? o->w - 1u - x : x, sy = o->vflip ? o->h - 1u - y : y;
                    u32 v, col;
                    u8 *q = px + ((size_t)sy * o->w + sx) * 4u;

                    if (off >= ncgr.tile_data_size) {
                        continue;
                    }
                    v = (x & 1u) ? (u32)(ncgr.tile_data[off] >> 4) : (u32)(ncgr.tile_data[off] & 15u);
                    col = (u32)o->pal * 16u + v;
                    if (v == 0u || col >= pal.count) {
                        continue;       /* (colour 0: clear) */
                    }
                    q[0] = pal.colors[col].r;
                    q[1] = pal.colors[col].g;
                    q[2] = pal.colors[col].b;
                    q[3] = 255;
                }
            }
            W.img[k].w = (int)o->w;
            W.img[k].h = (int)o->h;
            W.img[k].rgba = px;
            W.nimg = (int)k + 1;
        }
        ok = W.nimg > 0;
    }
    if (have & 4) {
        g2d_nanr_free(&nanr);
    }
    if (have & 2) {
        g2d_ncer_free(&ncer);
    }
    if (have & 1) {
        g2d_ncgr_free(&ncgr);
    }
    g2d_palette_free(&pal);
    for (i = 0; i < 4; i++) {
        free(d[i]);
    }
    return ok ? 0 : -1;
}

/* ---- the weather task's ring (weather_task.c) ---- */

static int part_add(void)
{
    int i;

    for (i = 0; i < P_MAX && W.p[i].linked; i++) {
    }
    if (i >= P_MAX) {
        return -1;                      /* (WEATHER_TASK_FindFreeParticle: none) */
    }
    memset(&W.p[i], 0, sizeof W.p[i]);
    W.p[i].linked = 1;
    W.count++;
    if (W.head < 0) {
        W.head = i;
        W.p[i].next = W.p[i].prev = i;
    } else {
        const int tail = W.p[W.head].prev;

        W.p[i].prev = tail;
        W.p[i].next = W.p[tail].next;
        W.p[W.p[tail].next].prev = i;
        W.p[tail].next = i;
    }
    return i;
}

static void part_remove(int i)
{
    if (!W.p[i].linked) {
        return;
    }
    W.count--;
    if (W.head == i) {
        W.head = W.p[i].next == i ? -1 : W.p[i].next;
    }
    W.p[W.p[i].next].prev = W.p[i].prev;
    W.p[W.p[i].prev].next = W.p[i].next;
    W.p[i].linked = 0;
}

/* WEATHER_TASK_SetParticlePosWrapped */
static void part_place(int i, int x, int y)
{
    s16 px = (s16)x, py = (s16)y;

    if (px > 320) {
        px = (s16)(px % 320);
    } else if (px < -64) {
        px = (s16)(px + 320);
    }
    if (py > 256) {
        py = (s16)(py % 256);
    } else if (py < -64) {
        py = (s16)(py + 256);
    }
    W.p[i].x = px;
    W.p[i].y = py;
}

/* ---- snow ---- */

static void snow_spawn(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const int k = part_add();
        int x, y;

        if (k < 0) {
            return;
        }
        W.p[k].image = (int)rnd(4);
        W.p[k].w[SN_FALL_ACCUM] = 0;
        W.p[k].w[SN_WOBBLE] = 0;
        W.p[k].w[SN_DYING] = 0;
        W.p[k].w[SN_DYING_FRAMES] = (int)rnd(0) % 30 + 2;
        W.p[k].w[SN_FALL_THRESHOLD] = (int)rnd(8) + 12;
        x = (int)rnd(0x19E) - 0x20;
        if (W.stagger == 1 && i >= count / 2) {
            y = -8 - 0x20 - (int)rnd(0x14);
        } else {
            y = -8 - (int)rnd(0x14);
        }
        part_place(k, x, y);
    }
}

static void snow_update(int i)
{
    Part *p = &W.p[i];
    int x, y;

    if (p->w[SN_DYING] == 0) {
        if (W.ending != 0) {
            p->w[SN_DYING] = 1;
        }
    } else {
        p->w[SN_DYING_FRAMES]--;
        if (p->w[SN_DYING_FRAMES] < 0) {
            part_remove(i);
            return;
        }
    }
    x = p->x + W.wobble[p->w[SN_WOBBLE]];
    y = p->y;
    if (p->w[SN_FALL_ACCUM] > p->w[SN_FALL_THRESHOLD]) {
        y = y + 1;
        p->w[SN_FALL_ACCUM] = p->w[SN_FALL_ACCUM] % p->w[SN_FALL_THRESHOLD];
    }
    part_place(i, x, y);
    p->w[SN_FALL_ACCUM] += 8;
    p->w[SN_WOBBLE] = (int)((unsigned)(p->w[SN_WOBBLE] + 1) % 337u);
    if ((y < -284 && y > -296) || (y > 212 && y < 232)) {
        W.stagger = 1;
        part_remove(i);
    }
}

/* ---- the sandstorm ---- */

static void sand_spawn(int count)
{
    int i, step;

    W.gust = (W.gust + 1) % 320;        /* GUST_CYCLE_FRAMES */
    step = W.gust / 40;                 /* GUST_STEP_FRAMES */
    for (i = 0; i < count; i++) {
        const int k = part_add();
        int size;

        if (k < 0) {
            return;
        }
        W.p[k].w[SD_AGE] = 0;
        W.p[k].w[SD_LIFETIME] = (int)rnd(20) + 15;
        size = 3 - (W.p[k].w[SD_LIFETIME] - 15) / 6;
        W.p[k].w[SD_SPEED_Y] = (size + 1) * W.gy[step];
        W.p[k].w[SD_SPEED_X] = (size + 1) * W.gx[step];
        W.p[k].w[SD_EXPIRED] = 0;
        W.p[k].w[SD_SPEED_X_STEP] = W.gx[step];
        if (rnd(1000) == 777u) {
            size = 4;
            W.p[k].w[SD_SPEED_Y] += W.p[k].w[SD_SPEED_Y] / 2;
        }
        W.p[k].image = size;
        part_place(k, -6 - (int)rnd(24), (int)rnd(192) - 64);
    }
}

static void sand_update(int i)
{
    Part *p = &W.p[i];

    if (p->w[SD_EXPIRED] == 0) {
        const int x = p->x + (s16)p->w[SD_SPEED_X], y = p->y + (s16)p->w[SD_SPEED_Y];

        if (p->w[SD_AGE] % 5 == 0) {
            p->w[SD_SPEED_X] += p->w[SD_SPEED_X_STEP];
        }
        if (p->w[SD_AGE]++ > p->w[SD_LIFETIME]) {
            p->w[SD_EXPIRED] = 1;
        }
        part_place(i, x, y);
    } else {
        part_remove(i);
    }
}

/* ---- the task ---- */

static void kind_spawn(int count)
{
    if (W.kind == KIND_SNOW) {
        snow_spawn(count);
    } else if (W.kind == KIND_SAND) {
        sand_spawn(count);
    }
}

static void kind_update(int i)
{
    if (W.kind == KIND_SNOW) {
        snow_update(i);
    } else if (W.kind == KIND_SAND) {
        sand_update(i);
    }
}

/* WEATHER_TASK_PrewarmParticles */
static void prewarm(int iterations, int grow_every, int grow_by)
{
    int cur, next, steps = 0, i = 0, j;

    kind_spawn(iterations);
    cur = W.head;
    if (cur < 0 || iterations <= 0) {
        return;
    }
    next = W.p[cur].next;
    do {
        for (j = 0; j < steps; j++) {
            kind_update(cur);
            if (!W.p[cur].linked) {
                break;
            }
        }
        cur = next;
        next = W.p[cur].next;
        if (i >= grow_every && i % grow_every == 0) {
            steps += grow_by;
        }
        if (cur == W.head) {
            break;
        }
        i++;
    } while (i < iterations);
}

/* WEATHER_TASK_UpdateParticles */
static void update_all(void)
{
    int cur = W.head, next;

    if (cur < 0) {
        return;
    }
    next = W.p[cur].next;
    do {
        kind_update(cur);
        cur = next;
        next = W.p[next].next;
    } while (cur != W.head && W.head >= 0);
}

/* One field frame of the run stage: the kind's run (the spawn timer), then the particles. */
static void step(void)
{
    W.sp.timer--;                       /* WEATHER_TASK_TickSpawnTimer */
    if (W.sp.timer <= 0) {
        kind_spawn(W.sp.count);
        W.sp.timer = W.sp.interval;
        /* (the sandstorm's run stage spawns one more when its gust speed is -6 or under: the table has none) */
    }
    update_all();
}

static void stop(void)
{
    memset(W.p, 0, sizeof W.p);
    W.head = -1;
    W.count = 0;
    W.kind = KIND_NONE;
    W.id = -1;
    images_free();
}

static void start(int id)
{
    size_t n = 0;
    u8 *ov;

    stop();
    W.seed = W.seed != 0u ? W.seed : 0x2F6E2B1ull;
    if (id == 1) {
        /* snow: WEATHER_SNOW_Begin, then _BeginPlain */
        ov = overlay(25, &n);
        if (ov == NULL || n < (size_t)(0x021EF790u - OVL_BASE) + 337u || images_load(23, 24, 22, 21) != 0) {
            free(ov);
            return;
        }
        memcpy(W.wobble, ov + (0x021EF790u - OVL_BASE), 337);
        free(ov);
        W.kind = KIND_SNOW;
        W.id = id;
        W.sp.count = 1;
        W.sp.interval = 0x10;
        W.sp.timer = 0;
        W.stagger = 1;
        W.ending = 0;
        prewarm(0x14, 1, 0x12);
    } else if (id == 3 || id == 12) {
        /* the sandstorm: WEATHER_SANDSTORM_Begin, then _BeginPlain */
        int k;

        ov = overlay(27, &n);
        if (ov == NULL || n < (size_t)(0x021EEB68u - OVL_BASE) + 32u || images_load(34, 35, 33, 32) != 0) {
            free(ov);
            return;
        }
        for (k = 0; k < 8; k++) {
            W.gx[k] = rd_s32(ov + (0x021EEB48u - OVL_BASE) + (size_t)k * 4u);
            W.gy[k] = rd_s32(ov + (0x021EEB68u - OVL_BASE) + (size_t)k * 4u);
        }
        free(ov);
        W.kind = KIND_SAND;
        W.id = id;
        W.gust = 0;
        W.sp.count = 1;
        W.sp.interval = 2;
        W.sp.timer = 0;
        prewarm(24, 2, 2);
    }
}

void bweather_want(int weather_id)
{
    W.want = weather_id;
}

void bweather_tick(unsigned frame60)
{
    const int want = W.want;
    const unsigned now = frame60 / 2u;
    unsigned steps, k;

    if (want != W.id && !(want < 0 && W.id < 0)) {
        if (want == 1 || want == 3 || want == 12) {
            start(want);
            if (W.id < 0) {
                W.want = -1;            /* (its data could not be read: not asked for again every frame) */
            }
        } else {
            stop();
            W.want = -1;
        }
        W.ticked = 0;
    }
    if (W.kind == KIND_NONE) {
        return;
    }
    steps = W.ticked ? now - W.last : 1u;
    if (steps > 4u) {
        steps = now < W.last ? 1u : 4u;
    }
    for (k = 0; k < steps; k++) {
        step();
    }
    W.last = now;
    W.ticked = 1;
}

unsigned bweather_sprites(BweatherSprite *out, unsigned max)
{
    unsigned n = 0;
    int cur = W.head;

    if (cur < 0 || W.kind == KIND_NONE) {
        return 0;
    }
    do {
        if (n < max && W.p[cur].image >= 0 && W.p[cur].image < W.nimg) {
            out[n].x = W.p[cur].x;
            out[n].y = W.p[cur].y;
            out[n].image = W.p[cur].image;
            n++;
        }
        cur = W.p[cur].next;
    } while (cur != W.head);
    return n;
}

const unsigned char *bweather_image(int image, int *w, int *h)
{
    if (image < 0 || image >= W.nimg || W.img[image].rgba == NULL) {
        return NULL;
    }
    *w = W.img[image].w;
    *h = W.img[image].h;
    return W.img[image].rgba;
}

int bweather_playing(void)
{
    return W.kind == KIND_NONE ? -1 : W.id;
}
