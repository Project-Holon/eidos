/*
 * engine/world/bweather.c -- the field's weather, replayed (bweather.h).
 *
 * What is mirrored, with the game's source beside each part (aletheia
 * src/game):
 *   - the weather task's particle ring and spawner: ov021/weather_task.c
 *     (WEATHER_TASK_AddParticle, _LinkParticle, _UnlinkParticle,
 *     _UpdateParticles, _InitSpawner, _TickSpawnTimer, _PrewarmParticles,
 *     _SetParticlePos, _SetParticlePosWrapped);
 *   - the table of the kinds, gWeatherTable (overlay 21, 0x021D8868: a
 *     pointer to the kind's WeatherKindDef and its overlay), and each
 *     WeatherKindDef (in overlays 24 to 28, which all load at 0x021EE740):
 *     whether the kind has sprites, its sprite resources (character,
 *     palette, cell bank, cell animation: members of a/0/5/6), how many
 *     sheets it has and each sheet's texture (a BTX0 member of a/0/5/6);
 *   - snow, blizzard, downpour, diamond dust:
 *     ov025/weather_snow_blizzard_downpour_diamond_dust.c, with its tables
 *     gSnowWobbleOffsetX (0x021EF790, 337 signed bytes), gBlizzardWindSpeedX
 *     and Y (0x021EF770, 0x021EF780: 4 ints each), gBlizzardFogScrollStepX
 *     and Y (0x021EF765, 0x021EF75C: 9 signed bytes each);
 *   - rain, thunderstorm, storm: ov026/weather_rain_thunderstorm_storm.c,
 *     with gWeatherThunderstormSpeedScale (0x021EFD08, 5 ints),
 *     gWeatherStormGustWindX and Y (0x021EFD74, 0x021EFD78: the first and
 *     second int of 6 records of 12 bytes), gWeatherStormGustLayerScale
 *     (0x021EFD3C, 14 ints);
 *   - the sandstorm: ov027/weather_sandstorm.c, with gSandstormGustSpeedX
 *     and Y (0x021EEB48, 0x021EEB68: 8 ints each);
 *   - the sheets: ov021/field_3dbg.c (a billboard: one quad that fills the
 *     screen in front of the map, textured with a repeating picture whose
 *     texture coordinates are the screen's pixels plus an offset, times a
 *     scale) and gWeatherBillboardDescs (overlay 21, 0x021D8850: the texels
 *     across the screen, 256 x 192 for both sheets).
 * For each kind: its Begin and BeginPlain stages (the spawner's numbers, the
 * screen filled at once, the sheets shown), its Run stage, its particles'
 * spawn and update functions.
 *
 * A battle begins with the weather already on, so a kind starts as the
 * field starts it on entering such a zone: begin, then "begin plain", then
 * run.  No camera scroll: the field shifts the sprites and the sheets as its
 * camera follows the player; a battle's camera is not that one.
 *
 * Kept as the game has them:
 *   - the first particle linked into an empty ring is not drawn
 *     (WEATHER_TASK_LinkParticle returns before it turns the actor's drawing
 *     on; unlinking turns it off);
 *   - a diamond dust sparkle never moves (its update works its new place out
 *     and does not write it back), and its frame number can be past the
 *     sequence's end (a remainder of a negative random number): the actor
 *     then keeps the frame it had (NNSi_G2dSetCellAnimationCurrentFrame
 *     changes nothing and returns 0).
 *
 * The sprites: a particle's "animation index" (CLACT_SetActorAnimIndex) is
 * a frame of the cell animation's current sequence, and each frame names a
 * cell: one or more OBJs at their own places around the actor's point.  No
 * kind turns the animation's own clock on over more than one frame (the
 * storm's three sequences have one frame each).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bweather.h"
#include "g2d.h"
#include "g3d.h"
#include "port.h"
#include "types.h"

#define P_MAX 32                /* WEATHER_PARTICLE_MAX */
#define IMG_MAX 40
#define SEQ_MAX 4
#define OVL_BASE 0x021EE740u    /* overlays 24..28 load here */
#define OV21_BASE 0x02187F00u   /* overlay 21 */
#define OV21_KINDS 0x021D8868u  /* gWeatherTable: { kind def, overlay id } a weather id */
#define OV21_SHEETS 0x021D8850u /* gWeatherBillboardDescs: { u16 width, u16 height, u32 distance, u8 id } x 2 */

enum { KIND_NONE, KIND_SNOW, KIND_SAND, KIND_RAIN, KIND_BLIZZARD, KIND_DOWNPOUR, KIND_THUNDER, KIND_STORM, KIND_DUST };

typedef struct Part {
    int linked, next, prev;     /* the ring, by index */
    int drawn;                  /* the actor's drawing is on */
    s16 x, y;                   /* the actor's place on the screen */
    int image;                  /* its frame: an index into W.img (kept by the slot's actor from use to use) */
    int w[8];                   /* the kind's own data */
} Part;

/* (snow: WeatherSnowFlake) */
enum { SN_FALL_ACCUM, SN_WOBBLE, SN_DYING, SN_DYING_FRAMES, SN_FALL_THRESHOLD };
/* (sandstorm: WeatherSandstormGrain) */
enum { SD_AGE, SD_LIFETIME, SD_SPEED_Y, SD_EXPIRED, SD_SPEED_X, SD_SPEED_X_STEP };
/* (rain: WeatherRainDrop) */
enum { RN_AGE, RN_LIFETIME, RN_SPEED_Y, RN_STATE, RN_SPEED_X, RN_SPLASH };
/* (blizzard: WeatherBlizzardFlake) */
enum { BZ_AGE, BZ_LIFETIME, BZ_SPEED_Y, BZ_STATE, BZ_SPEED_X, BZ_ACCEL_X };
/* (downpour: WeatherDownpourDrop) */
enum { DP_AGE, DP_FALL, DP_SPEED_Y, DP_STATE, DP_SPEED_X, DP_SEED };
/* (thunderstorm: WeatherThunderstormDrop) */
enum { TH_SPEED_Y, TH_STATE, TH_SPEED_X };
/* (storm: WeatherStormParticle, its drop in 1/8 pixels) */
enum { ST_TYPE, ST_POS_X, ST_POS_Y, ST_SPEED_X, ST_SPEED_Y, ST_LAST_X, ST_LAST_Y };
/* (diamond dust: WeatherDiamondDustSparkle) */
enum { DD_AGE, DD_LIFETIME };

/* (the storm's gust phases, and its drop types) */
enum { GUST_NONE, GUST_CALM, GUST_GUST, GUST_RISE, GUST_FADE };
enum { DROP_SIDE_SLOW, DROP_FALL, DROP_SIDE_FAST };

typedef struct Sheet {
    int w, h;                   /* the texture, 0: none */
    u8 *rgba;
    int visible;
    int off_x, off_y;           /* FIELD_3DBG_SetBillboardTexOffsetX / Y (texels) */
    int scale_x, scale_y;       /* FIELD_3DBG_SetBillboardScaleX / Y (20.12) */
} Sheet;

static struct {
    volatile int want;          /* the id asked for (bweather_want), -1 none */
    int id, kind;               /* what plays */
    Part p[P_MAX];
    int head, count;
    struct { int count, timer, interval; } sp;      /* the spawner (its run stage: no ramp) */
    int stagger, ending;        /* snow: WeatherSnowWork */
    int gust, sand_scroll;      /* the sandstorm: WeatherSandstormWork.gustCounter, .scrollPhase */
    int wind_phase, bz_x, bz_y, bz_step;            /* the blizzard: WeatherBlizzardWork */
    int intensity, th_scroll;   /* the thunderstorm: WeatherThunderstormWork.intensityTimer, .scrollPhase */
    struct {                    /* the storm: WeatherStormWork */
        int x0, y0, x1, y1, timer, duration, phase, wind_x, wind_y, layer_scale;
    } st;
    u64 seed;
    unsigned last;              /* the field frame of the last tick */
    int ticked;
    signed char wobble[337];
    int gx[8], gy[8];
    int bwx[4], bwy[4];
    signed char bsx[9], bsy[9];
    int th_scale[5];
    int st_wx[6], st_wy[6], st_scale[14];
    struct { int w, h, ox, oy; u8 *rgba; } img[IMG_MAX];
    int nimg, seq_base[SEQ_MAX], seq_n[SEQ_MAX];
    Sheet sheet[2];
    int sheet_w, sheet_h;       /* texels across the screen */
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

static unsigned rd_u16(const u8 *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void images_free(void)
{
    int i;

    for (i = 0; i < IMG_MAX; i++) {
        free(W.img[i].rgba);
        W.img[i].rgba = NULL;
    }
    W.nimg = 0;
    memset(W.seq_base, 0, sizeof W.seq_base);
    memset(W.seq_n, 0, sizeof W.seq_n);
    for (i = 0; i < 2; i++) {
        free(W.sheet[i].rgba);
        memset(&W.sheet[i], 0, sizeof W.sheet[i]);
    }
}

/* One cell as a picture: its OBJs at their places, the first on top (as the hardware orders them), in the palette.
 * (ox, oy): the picture's top left corner from the actor's point. */
static int cell_image(const G2dNcgr *ncgr, const G2dNcer *ncer, const G2dPalette *pal, u32 cell, int k)
{
    const G2dCell *c = &ncer->cells[cell];
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), w, h, i;
    u8 *px;

    for (i = 0; i < (int)c->num_oam; i++) {
        const G2dOam *o = &c->oams[i];

        x0 = o->x < x0 ? o->x : x0;
        y0 = o->y < y0 ? o->y : y0;
        x1 = o->x + (int)o->w > x1 ? o->x + (int)o->w : x1;
        y1 = o->y + (int)o->h > y1 ? o->y + (int)o->h : y1;
    }
    w = x1 - x0;
    h = y1 - y0;
    if (c->num_oam < 1u || w <= 0 || h <= 0 || w > 256 || h > 256 || (px = (u8 *)calloc((size_t)w * (size_t)h, 4)) == NULL) {
        return -1;
    }
    for (i = (int)c->num_oam - 1; i >= 0; i--) {
        const G2dOam *o = &c->oams[i];
        /* (1D mapping: the OBJ's tile number counts units of 32 << mapping bytes; 8 x 8 blocks in order) */
        const u32 block0 = ncer->mapping_type < 4u ? (u32)o->tile_idx << ncer->mapping_type : o->tile_idx;
        u32 x, y;

        for (y = 0; y < o->h; y++) {
            for (x = 0; x < o->w; x++) {
                const u32 block = block0 + (y / 8u) * (o->w / 8u) + x / 8u;
                const u32 off = block * 32u + (y % 8u) * 4u + (x % 8u) / 2u;
                const u32 sx = o->hflip ? o->w - 1u - x : x, sy = o->vflip ? o->h - 1u - y : y;
                u32 v, col;
                u8 *q = px + ((size_t)(o->y - y0 + (int)sy) * (size_t)w + (size_t)(o->x - x0 + (int)sx)) * 4u;

                if (off >= ncgr->tile_data_size) {
                    continue;
                }
                v = (x & 1u) ? (u32)(ncgr->tile_data[off] >> 4) : (u32)(ncgr->tile_data[off] & 15u);
                col = (u32)o->pal * 16u + v;
                if (v == 0u || col >= pal->count) {
                    continue;       /* (colour 0: clear) */
                }
                q[0] = pal->colors[col].r;
                q[1] = pal->colors[col].g;
                q[2] = pal->colors[col].b;
                q[3] = 255;
            }
        }
    }
    W.img[k].w = w;
    W.img[k].h = h;
    W.img[k].ox = x0;
    W.img[k].oy = y0;
    W.img[k].rgba = px;
    return 0;
}

/* The kind's sprite pictures: every frame of every sequence of its cell animation, its cell in the palette. */
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
        u32 s, k;

        ok = 1;
        for (s = 0; ok && s < nanr.num_sequences && s < SEQ_MAX; s++) {
            W.seq_base[s] = W.nimg;
            for (k = 0; ok && k < nanr.sequences[s].num_frames && W.nimg < IMG_MAX; k++) {
                G2dFrame fr;

                ok = g2d_nanr_frame_data(&nanr, s, k, &fr) == 0 && fr.cell_idx < ncer.num_cells
                     && cell_image(&ncgr, &ncer, &pal, fr.cell_idx, W.nimg) == 0;
                if (ok) {
                    W.nimg++;
                    W.seq_n[s]++;
                }
            }
        }
        ok = ok && W.nimg > 0;
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

/* A sheet's texture (WEATHER_TASK_InitBillboard, FIELD_3DBG_LoadTexture): the BTX0 member's one texture, decoded in
 * the descriptor's format with the descriptor's "colour 0 is clear" (FIELD_3DBG_SetTextureParams hands the
 * descriptor's values to the hardware, not the file's).  desc: fileId, sizeS, sizeT, repeat, flip, format,
 * plttColor0, alpha (the game forces the alpha to 31: opaque where a texel is not clear). */
static int sheet_load(Sheet *sh, const u8 *desc)
{
    BwArcFile arc;
    u8 *d;
    u32 n = 0, bn = 0;
    const u8 *blk;
    G3dTex0 t;
    G3dTexRecord tr;
    G3dPalRecord pr;
    G3dTexture out;
    int ok = 0;

    if (bw_arc_open(&arc, "a/0/5/6") != 0) {
        return -1;
    }
    d = g2d_arc_member(&arc, rd_u16(desc), &n);
    bw_arc_close(&arc);
    if (d == NULL) {
        return -1;
    }
    memset(&out, 0, sizeof out);
    blk = g3d_container_block(d, n, "TEX0", &bn);
    if (blk != NULL && g3d_tex0_parse(&t, blk, bn) == 0 && g3d_tex0_texture(&t, 0, &tr) == 0 && g3d_tex0_palette(&t, 0, &pr) == 0
        && tr.offset < t.texel_bytes && pr.offset < t.pal_bytes
        && g3d_texture_decode(&out, desc[6], 8u << desc[2], 8u << desc[3], t.texel + tr.offset, t.texel_bytes - tr.offset,
                              t.pal + pr.offset, t.pal_bytes - pr.offset, desc[7] != 0) == 0) {
        sh->w = (int)out.width;
        sh->h = (int)out.height;
        sh->rgba = out.rgba;            /* (taken) */
        sh->visible = 0;                /* WEATHER_TASK_InitBillboard */
        sh->off_x = sh->off_y = 0;
        sh->scale_x = sh->scale_y = 0x1000;     /* FIELD_3DBG_LoadBillboardTexture */
        ok = 1;
    }
    free(d);
    return ok ? 0 : -1;
}

/* ---- the weather task's ring (weather_task.c) ---- */

static int part_add(void)
{
    int i, image;

    for (i = 0; i < P_MAX && W.p[i].linked; i++) {
    }
    if (i >= P_MAX) {
        return -1;                      /* (WEATHER_TASK_FindFreeParticle: none) */
    }
    image = W.p[i].image;
    memset(&W.p[i], 0, sizeof W.p[i]);
    W.p[i].image = image;               /* (the slot's actor keeps its frame) */
    W.p[i].linked = 1;
    W.count++;
    if (W.head < 0) {
        W.head = i;
        W.p[i].next = W.p[i].prev = i;
        /* (WEATHER_TASK_LinkParticle returns here: this actor's drawing stays off) */
    } else {
        const int tail = W.p[W.head].prev;

        W.p[i].prev = tail;
        W.p[i].next = W.p[tail].next;
        W.p[W.p[tail].next].prev = i;
        W.p[tail].next = i;
        W.p[i].drawn = 1;
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
    W.p[i].drawn = 0;
}

/* WEATHER_TASK_SetParticlePos */
static void part_set(int i, int x, int y)
{
    W.p[i].x = (s16)x;
    W.p[i].y = (s16)y;
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

/* CLACT_SetActorAnimIndex: frame `index` of the current sequence (the first); past its end: the frame stays */
static void part_frame(int i, unsigned index)
{
    if ((int)index < W.seq_n[0] && index < 0x10000u) {
        W.p[i].image = W.seq_base[0] + (int)index;
    }
}

/* CLACT_SetActorAnimSeq: the sequence's first frame */
static void part_seq(int i, int seq)
{
    if (seq < SEQ_MAX && W.seq_n[seq] > 0) {
        W.p[i].image = W.seq_base[seq];
    }
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
        part_frame(k, rnd(4));
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
        part_frame(k, (unsigned)size);
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

/* ---- rain ---- */

static void rain_spawn(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const int k = part_add();
        Part *p;
        u32 r, size;
        int jitter;

        if (k < 0) {
            return;
        }
        p = &W.p[k];
        r = rnd(0);
        p->w[RN_AGE] = 0;
        size = r % 3u;
        part_frame(k, size);
        jitter = (int)(r % 20u);
        p->w[RN_SPEED_Y] = jitter + ((int)size + 1) * 16;
        if (size == 2u) {
            p->w[RN_SPEED_Y] += 16;
        }
        p->w[RN_SPEED_X] = jitter / 8 + ((int)size + 1) * 8;
        if (size == 2u) {
            p->w[RN_SPEED_X] += 8;
        }
        p->w[RN_STATE] = 0;
        p->w[RN_LIFETIME] = (int)(r & 1u) + 1;
        part_set(k, (int)(u8)r + ((int)size * 15 - 0x48), -0x60);
        p->w[RN_SPEED_X] *= 2;
        p->w[RN_SPEED_Y] *= 2;
        p->w[RN_SPLASH] = (int)rnd(10);
    }
}

static void rain_update(int i)
{
    Part *p = &W.p[i];

    switch (p->w[RN_STATE]) {
    case 0: {                           /* falling */
        const int x = p->x + (s16)p->w[RN_SPEED_X], y = p->y + (s16)p->w[RN_SPEED_Y];

        p->w[RN_AGE] += 2;
        if (p->w[RN_AGE] > p->w[RN_LIFETIME]) {
            if (p->w[RN_SPLASH] < 7) {
                p->w[RN_STATE] = 2;
            } else {
                p->w[RN_STATE] = 1;
                p->w[RN_AGE] = 4;
                part_frame(i, 3);
            }
        }
        part_set(i, x, y);
        break;
    }
    case 1: {                           /* splashing */
        const int left = p->w[RN_AGE];

        p->w[RN_AGE] = left - 1;
        if (left <= 0) {
            p->w[RN_STATE] = 2;
        }
        break;
    }
    default:
        part_remove(i);
        break;
    }
}

/* ---- the blizzard ---- */

static void blizzard_spawn(int count)
{
    int i, wind;

    if (++W.wind_phase >= 0x400) {
        W.wind_phase = 0;
    }
    wind = W.wind_phase / 256;
    for (i = 0; i < count; i++) {
        const int k = part_add();
        Part *p;
        int size, x, y;

        if (k < 0) {
            return;
        }
        p = &W.p[k];
        p->w[BZ_AGE] = 0;
        p->w[BZ_LIFETIME] = (int)rnd(3) + 9;
        size = (int)rnd(4);
        part_frame(k, (unsigned)size);
        p->w[BZ_SPEED_X] = (size + 1) * W.bwx[wind];
        p->w[BZ_SPEED_Y] = W.bwy[wind] * (size + 1);
        p->w[BZ_STATE] = 0;
        if (size == 3) {
            p->w[BZ_SPEED_X] += W.bwx[wind];
            p->w[BZ_SPEED_Y] += W.bwy[wind];
        }
        p->w[BZ_ACCEL_X] = W.bwx[wind];
        x = -(int)rnd(0x18);
        y = (int)rnd(0xA8) - 0x20;
        part_set(k, x, y);
    }
}

static void blizzard_update(int i)
{
    Part *p = &W.p[i];

    if (p->w[BZ_STATE] == 0) {
        const int x = p->x + (s16)p->w[BZ_SPEED_X], y = p->y + (s16)p->w[BZ_SPEED_Y];

        if (p->w[BZ_AGE]++ > p->w[BZ_LIFETIME]) {
            p->w[BZ_STATE] = 1;
        }
        if (p->w[BZ_AGE] % 4 == 0) {
            p->w[BZ_SPEED_X] += p->w[BZ_ACCEL_X];
            if (p->w[BZ_SPEED_Y] > 1) {
                p->w[BZ_SPEED_Y]--;
            }
        }
        part_set(i, x, y);
    } else {
        part_remove(i);
    }
}

/* ---- the downpour ---- */

static void downpour_spawn(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const int k = part_add();
        Part *p;
        u32 r, roll;
        int size = 0, jitter;

        if (k < 0) {
            return;
        }
        p = &W.p[k];
        r = rnd(0);
        p->w[DP_AGE] = 0;
        roll = r % 100u;
        if (roll < 30u) {
            size = 0;
        } else if (roll < 95u) {
            size = 1;
        } else {
            size = 2;
        }
        part_frame(k, (unsigned)(2 - size));
        jitter = (int)(r % 23u);
        p->w[DP_SPEED_Y] = jitter + (size + 1) * 10;
        if (size == 2) {
            p->w[DP_SPEED_Y] += 3;
        }
        p->w[DP_SPEED_X] = jitter / 4 + (size + 1) * 4;
        if (size == 2) {
            p->w[DP_SPEED_X] -= 2;
        }
        p->w[DP_STATE] = 0;
        p->w[DP_FALL] = (int)(r & 3u) + 1;
        part_place(k, (int)(r % 270u) + (size * 15 - 0x40), (int)(r & 0xFu) - 8);
        p->w[DP_SEED] = (int)r;
    }
}

static void downpour_update(int i)
{
    Part *p = &W.p[i];

    switch (p->w[DP_STATE]) {
    case 0:                             /* falling */
        if (++p->w[DP_AGE] > p->w[DP_FALL]) {
            p->w[DP_STATE] = 1;
            p->w[DP_AGE] = (p->w[DP_SEED] % 4) * 2 + 8;
            p->w[DP_SPEED_X] = 2;
            p->w[DP_SPEED_Y] = 2;
            part_frame(i, (unsigned)(p->image - W.seq_base[0] + 3));    /* (the frame it shows, 3 on) */
            return;
        }
        part_place(i, p->x + (s16)p->w[DP_SPEED_X], p->y + (s16)p->w[DP_SPEED_Y]);
        break;
    case 1: {                           /* splashing */
        const int x = p->x + (s16)p->w[DP_SPEED_X];
        const int y = p->w[DP_AGE] % 2 == 0 ? p->y + (s16)p->w[DP_SPEED_Y] : p->y - (s16)p->w[DP_SPEED_Y];

        if (p->w[DP_AGE] % 4 == 0) {
            if (p->w[DP_SPEED_Y] - 1 >= 0) {
                p->w[DP_SPEED_Y]--;
            }
            if (p->w[DP_SPEED_X] - 1 >= 0) {
                p->w[DP_SPEED_X]--;
            }
        }
        part_set(i, x, y);
        if (p->w[DP_AGE]-- <= 0) {
            p->w[DP_STATE] = 2;
        }
        break;
    }
    default:
        part_remove(i);
        break;
    }
}

/* ---- diamond dust ---- */

static void dust_spawn(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const int k = part_add();
        Part *p;
        int r, r2, frame, x, y, top, span, bottom;

        if (k < 0) {
            return;
        }
        p = &W.p[k];
        r = (int)rnd(0);                /* (the game keeps both in signed ints: their remainders can be negative) */
        r2 = (int)rnd(0);
        p->w[DD_AGE] = 0;
        p->w[DD_LIFETIME] = r % 5 + 7;
        frame = r % 20;
        x = r % 384 - 64;
        y = r2 % 256 - 8;
        part_place(k, x, y);
        top = 50 - x / 3;
        span = 206 - x / 3;
        if (span < 0) {
            bottom = top - r2 % -span;
        } else {
            bottom = span != 0 ? top + r2 % span : top;
        }
        if (top <= (s16)y && bottom >= (s16)y) {
            p->w[DD_LIFETIME] *= 2;
        } else {
            frame = r % 4;
        }
        part_frame(k, (unsigned)(u16)frame);
    }
}

static void dust_update(int i)
{
    Part *p = &W.p[i];

    /* (the game works a drifted place out here and never writes it back: a sparkle stays where it appeared) */
    p->w[DD_AGE]++;
    if (p->w[DD_AGE] >= p->w[DD_LIFETIME]) {
        part_remove(i);
    }
}

/* ---- the thunderstorm ---- */

static void thunder_spawn(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const int k = part_add();
        Part *p;
        u32 r, size;
        int speed;

        if (k < 0) {
            return;
        }
        p = &W.p[k];
        r = rnd(0);
        size = r % 3u;
        part_frame(k, size);
        speed = (int)(r & 15u) + ((int)size + 1) * 14;
        p->w[TH_SPEED_X] = W.th_scale[W.intensity / 60] * speed / 10;
        p->w[TH_SPEED_Y] = W.th_scale[W.intensity / 60] * speed / 10;
        p->w[TH_STATE] = 0;
        part_set(k, (int)(r % 0x180u) - 0x80, (int)(r % 0x28u) - 0x50);
    }
}

static void thunder_update(int i)
{
    Part *p = &W.p[i];

    if (p->w[TH_STATE] == 0) {
        const int x = p->x + (s16)p->w[TH_SPEED_X], y = p->y + (s16)p->w[TH_SPEED_Y];

        if ((s16)y >= 0xC0) {
            p->w[TH_STATE] = 1;
        }
        part_set(i, x, y);
    } else {
        part_remove(i);
    }
}

/* ---- the storm ---- */

static void storm_init(int k, int type)
{
    Part *p = &W.p[k];
    const u32 r = rnd(0);
    int x, y;

    p->w[ST_TYPE] = type;
    if (type == DROP_FALL) {
        p->w[ST_SPEED_X] = (int)(r % 43u) + 0x56;
        p->w[ST_SPEED_Y] = (int)(r % 43u) * 2 + 0x102;
        x = (int)(r % 336u) - 0x50;
        y = (int)(r % 40u) - 0x50;
    } else {
        p->w[ST_SPEED_X] = (int)(r % 24u) + (type == DROP_SIDE_SLOW ? 0x10 : 0x50);
        p->w[ST_SPEED_Y] = (int)(r % 24u) + (type == DROP_SIDE_SLOW ? 8 : 0xE);
        if (type == DROP_SIDE_SLOW) {
            p->w[ST_LAST_X] = W.st.wind_x;
            p->w[ST_LAST_Y] = W.st.wind_y;
        } else {
            p->w[ST_LAST_X] = (s16)(p->w[ST_SPEED_X] + W.st.wind_x);
            p->w[ST_LAST_Y] = (s16)(p->w[ST_SPEED_Y] + W.st.wind_y);
        }
        x = (int)(r & 7u) - 8;
        y = (int)(r % 160u) - 16;
    }
    part_set(k, x, y);
    p->w[ST_POS_X] = (s16)(x << 3);
    p->w[ST_POS_Y] = (s16)(y << 3);
    part_seq(k, type);                  /* (the drop types' sequences: 0 slow, 1 falling, 2 fast) */
}

static void storm_spawn(int count)
{
    const u32 r = rnd(0);
    u32 percent;
    int i;

    if (W.st.phase == GUST_NONE || (W.st.phase == GUST_GUST && (r & 1u)) || count <= 0) {
        return;
    }
    percent = r % 100u;
    for (i = 0; i < count; i++) {
        const int k = part_add();
        int type = DROP_SIDE_SLOW;

        if (k < 0) {
            return;
        }
        switch (W.st.phase) {
        case GUST_CALM:
            type = DROP_FALL;
            break;
        case GUST_GUST:
            type = percent <= 0x32u ? DROP_SIDE_FAST : DROP_SIDE_SLOW;
            break;
        default:                        /* rise, fade */
            type = percent <= 0x50u ? DROP_FALL : DROP_SIDE_FAST;
            break;
        }
        storm_init(k, type);
    }
}

static void storm_update(int i)
{
    Part *p = &W.p[i];
    int x, y, limit_y = 192;

    switch (p->w[ST_TYPE]) {
    case DROP_SIDE_SLOW: {
        int wx = W.st.wind_x, wy;

        if (wx == 0) {
            wx = p->w[ST_LAST_X];
            wy = p->w[ST_LAST_Y];
        } else {
            wy = W.st.wind_y;
            p->w[ST_LAST_X] = wx;
            p->w[ST_LAST_Y] = wy;
        }
        p->w[ST_POS_X] = (s16)(p->w[ST_POS_X] + (s16)(p->w[ST_SPEED_X] + wx));
        p->w[ST_POS_Y] = (s16)(p->w[ST_POS_Y] + (s16)(p->w[ST_SPEED_Y] + wy));
        break;
    }
    case DROP_FALL:
        p->w[ST_POS_X] = (s16)(p->w[ST_POS_X] + (s16)(p->w[ST_SPEED_X] + W.st.wind_x));
        p->w[ST_POS_Y] = (s16)(p->w[ST_POS_Y] + (s16)(p->w[ST_SPEED_Y] + W.st.wind_y));
        limit_y = 160;
        break;
    default:                            /* a fast sideways streak */
        if (W.st.wind_x != 0) {
            p->w[ST_POS_X] = (s16)(p->w[ST_POS_X] + (s16)(p->w[ST_SPEED_X] + W.st.wind_x));
            p->w[ST_POS_Y] = (s16)(p->w[ST_POS_Y] + (s16)(p->w[ST_SPEED_Y] + W.st.wind_y));
            p->w[ST_LAST_X] = (s16)(p->w[ST_SPEED_X] + W.st.wind_x);
            p->w[ST_LAST_Y] = (s16)(p->w[ST_SPEED_Y] + W.st.wind_y);
        } else {
            p->w[ST_POS_X] = (s16)(p->w[ST_POS_X] + p->w[ST_LAST_X]);
            p->w[ST_POS_Y] = (s16)(p->w[ST_POS_Y] + p->w[ST_LAST_Y]);
        }
        break;
    }
    x = p->w[ST_POS_X] / 8;
    y = p->w[ST_POS_Y] / 8;
    if (x >= 256 || y >= limit_y) {
        part_remove(i);
        return;
    }
    part_set(i, x, y);
}

/* WEATHER_STORM_StartCalm (its fog change and its sound are not replayed) */
static void storm_calm(void)
{
    const u32 r = rnd(0);

    W.st.phase = GUST_CALM;
    W.st.duration = (int)(r % 400u) + 0x3C;
    W.st.timer = 0;
    W.sheet[1].visible = 0;
}

/* WEATHER_STORM_UpdateGust: calm, rise, gust, fade, calm; the wind and the second sheet's scale of the phase */
static void storm_gust(void)
{
    switch (W.st.phase) {
    case GUST_CALM:
        if (++W.st.timer >= W.st.duration) {
            W.st.phase = GUST_RISE;
            W.st.duration = 0x10;
            W.st.timer = 0;
        }
        break;
    case GUST_GUST:
        if (++W.st.timer >= W.st.duration) {
            W.st.phase = GUST_FADE;
            W.st.duration = 0x10;
            W.st.timer = 0;
            W.sheet[1].visible = 0;
            W.sheet[0].scale_x = 0x1000;
            W.sheet[0].scale_y = 0x1000;
        }
        break;
    case GUST_RISE:
        if (++W.st.timer >= W.st.duration) {
            const u32 r = rnd(0);

            W.st.phase = GUST_GUST;
            W.st.duration = (int)(r % 400u) + 0x3C;
            W.st.timer = 0;
            W.sheet[1].visible = 1;
            W.sheet[0].scale_x = 0x800;
            W.sheet[0].scale_y = 0x1800;
        }
        break;
    case GUST_FADE:
        if (++W.st.timer >= W.st.duration) {
            storm_calm();
        }
        break;
    default:
        break;
    }
    switch (W.st.phase) {
    case GUST_CALM:
        if (W.st.duration - W.st.timer >= 9) {
            W.st.wind_x = 0;
            W.st.wind_y = 0;
        } else {
            W.st.wind_x = 0xA0;
            W.st.wind_y = 0x10;
        }
        break;
    case GUST_GUST: {
        const int k = W.st.timer % 0x48 * 6 / 0x48;

        W.st.wind_x = W.st_wx[k];
        W.st.wind_y = W.st_wy[k];
        W.st.layer_scale = W.st_scale[((unsigned)W.st.timer % 0xE0u) * 14u / 0xE0u];
        break;
    }
    case GUST_RISE:
    case GUST_FADE:
        W.st.wind_x = 0xA0;
        W.st.wind_y = 0x10;
        W.st.layer_scale = 0x800;
        break;
    default:
        break;
    }
}

/* ---- the task ---- */

static void kind_spawn(int count)
{
    switch (W.kind) {
    case KIND_SNOW: snow_spawn(count); break;
    case KIND_SAND: sand_spawn(count); break;
    case KIND_RAIN: rain_spawn(count); break;
    case KIND_BLIZZARD: blizzard_spawn(count); break;
    case KIND_DOWNPOUR: downpour_spawn(count); break;
    case KIND_THUNDER: thunder_spawn(count); break;
    case KIND_STORM: storm_spawn(count); break;
    case KIND_DUST: dust_spawn(count); break;
    default: break;
    }
}

static void kind_update(int i)
{
    switch (W.kind) {
    case KIND_SNOW: snow_update(i); break;
    case KIND_SAND: sand_update(i); break;
    case KIND_RAIN: rain_update(i); break;
    case KIND_BLIZZARD: blizzard_update(i); break;
    case KIND_DOWNPOUR: downpour_update(i); break;
    case KIND_THUNDER: thunder_update(i); break;
    case KIND_STORM: storm_update(i); break;
    case KIND_DUST: dust_update(i); break;
    default: break;
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

/* WEATHER_TASK_StepSpawnTimer: 1 when it fired */
static int spawn_timer(void)
{
    W.sp.timer--;
    if (W.sp.timer <= 0) {
        kind_spawn(W.sp.count);
        W.sp.timer = W.sp.interval;
        return 1;
    }
    return 0;
}

/* One field frame of the run stage: the kind's run, then the particles. */
static void step(void)
{
    int fired;

    if (W.kind == KIND_THUNDER) {
        W.intensity = (W.intensity + 1) % 300;          /* WEATHER_THUNDERSTORM_Run: before its spawn timer */
    }
    fired = spawn_timer();
    switch (W.kind) {
    case KIND_SAND:                     /* WEATHER_SANDSTORM_Run, _ScrollBillboards */
        if (fired && W.gx[W.gust / 40] <= -6) {
            sand_spawn(1);
        }
        W.sand_scroll = (W.sand_scroll + 6) % 256;      /* GUST_SCROLL_STEP, GUST_SCROLL_PERIOD */
        W.sheet[0].off_x = -W.sand_scroll * 2;
        W.sheet[0].off_y = -W.sand_scroll * 2;
        break;
    case KIND_BLIZZARD: {               /* WEATHER_BLIZZARD_Run, _ScrollFogLayer */
        const int index = W.bz_step / 28;

        if (fired && W.bwx[W.wind_phase / 256] <= -8) {
            blizzard_spawn(1);
        }
        W.bz_step = (int)((unsigned)(W.bz_step + 1) % 252u);
        W.bz_x += W.bsx[index];
        W.bz_y += W.bsy[index];
        if (W.bz_x < 0) {
            W.bz_x += 256;
        }
        if (W.bz_y < 0) {
            W.bz_y += 256;
        }
        W.bz_x %= 256;
        W.bz_y %= 256;
        W.sheet[0].off_x = -W.bz_x;
        W.sheet[0].off_y = -W.bz_y;
        break;
    }
    case KIND_THUNDER:                  /* WEATHER_THUNDERSTORM_Run, _ScrollLayers (the lightning is not replayed) */
        W.th_scroll = (W.th_scroll + 9) % 256;
        W.sheet[0].off_x = -W.th_scroll;
        W.sheet[0].off_y = -W.th_scroll;
        break;
    case KIND_STORM:                    /* WEATHER_STORM_Run, _ScrollLayers */
        storm_gust();
        if (W.st.wind_x != 0) {
            W.st.x0 = (W.st.x0 + 9) % 256;
            W.st.y0 = (W.st.y0 + 3) % 256;
            W.st.x1 = (W.st.x1 + 9) % 256;
            W.st.y1 = (W.st.y1 + 2) % 256;
        } else {
            W.st.x0 = (W.st.x0 + 3) % 256;
            W.st.y0 = (W.st.y0 + 8) % 256;
        }
        W.sheet[0].off_x = -W.st.x0;
        W.sheet[0].off_y = -W.st.y0;
        W.sheet[1].off_x = -W.st.x1;
        W.sheet[1].off_y = -W.st.y1;
        W.sheet[1].scale_x = W.sheet[1].scale_y = W.st.layer_scale;
        break;
    default:
        break;
    }
    update_all();
}

static void stop(void)
{
    int i;

    memset(W.p, 0, sizeof W.p);
    W.head = -1;
    W.count = 0;
    W.kind = KIND_NONE;
    W.id = -1;
    images_free();
    for (i = 0; i < P_MAX; i++) {
        W.p[i].image = 0;               /* (a new actor shows its sequence's first frame) */
    }
}

static void spawner(int count, int interval)
{
    W.sp.count = count;
    W.sp.interval = interval;
    W.sp.timer = 0;                     /* WEATHER_TASK_InitSpawner */
}

/* Whether `n` bytes at the game address `a` lie in an overlay image of `size` bytes loaded at `base`. */
static int in_image(u32 a, u32 n, u32 base, size_t size)
{
    return a >= base && (size_t)(a - base) + n <= size;
}

static void start(int id)
{
    static const struct { int id, kind; } kinds[] = {
        { 1, KIND_SNOW }, { 2, KIND_RAIN }, { 3, KIND_SAND }, { 12, KIND_SAND }, { 4, KIND_BLIZZARD },
        { 5, KIND_DOWNPOUR }, { 6, KIND_THUNDER }, { 7, KIND_STORM }, { 8, KIND_DUST },
    };
    size_t n21 = 0, n = 0;
    u8 *ov21, *ov = NULL;
    const u8 *def;
    u32 def_at;
    int kind = KIND_NONE, ok = 0, ovl, k, sheets;

    stop();
    W.seed = W.seed != 0u ? W.seed : 0x2F6E2B1ull;
    for (k = 0; k < (int)(sizeof kinds / sizeof kinds[0]); k++) {
        if (kinds[k].id == id) {
            kind = kinds[k].kind;
        }
    }
    if (kind == KIND_NONE) {
        return;                         /* (clear, the fogs: no sprites, no sheets) */
    }
    /* the kind's definition: gWeatherTable[id], then the WeatherKindDef in the kind's overlay */
    ov21 = overlay(21, &n21);
    if (ov21 == NULL || !in_image(OV21_KINDS + (u32)id * 8u, 8u, OV21_BASE, n21) || !in_image(OV21_SHEETS, 24u, OV21_BASE, n21)) {
        free(ov21);
        return;
    }
    def_at = (u32)rd_s32(ov21 + (OV21_KINDS - OV21_BASE) + (size_t)id * 8u);
    ovl = rd_s32(ov21 + (OV21_KINDS - OV21_BASE) + (size_t)id * 8u + 4u);
    W.sheet_w = (int)rd_u16(ov21 + (OV21_SHEETS - OV21_BASE));
    W.sheet_h = (int)rd_u16(ov21 + (OV21_SHEETS - OV21_BASE) + 2u);
    free(ov21);
    if (ovl < 24 || ovl > 28 || (ov = overlay(ovl, &n)) == NULL || !in_image(def_at, 0x4Cu, OVL_BASE, n)) {
        free(ov);
        return;
    }
    def = ov + (def_at - OVL_BASE);
    /* (WeatherKindDef: archive, hasSprites, billboardCount, the four sprite members, two 12-byte sheet descriptors) */
    ok = rd_u16(def + 2) != 0u && images_load(rd_u16(def + 6), rd_u16(def + 8), rd_u16(def + 0xA), rd_u16(def + 0xC)) == 0;
    sheets = (int)rd_u16(def + 4);
    for (k = 0; ok && k < sheets && k < 2; k++) {
        (void)sheet_load(&W.sheet[k], def + 0x0E + (size_t)k * 12u);
    }
#define TABLE(a, len) (in_image((a), (len), OVL_BASE, n) ? ov + ((a) - OVL_BASE) : NULL)
    if (ok) {
        const u8 *t, *u, *v;

        W.kind = kind;
        switch (kind) {
        case KIND_SNOW:                 /* WEATHER_SNOW_Begin, then _BeginPlain */
            if ((t = TABLE(0x021EF790u, 337u)) == NULL) {
                ok = 0;
                break;
            }
            memcpy(W.wobble, t, 337);
            spawner(1, 0x10);
            W.stagger = 1;
            W.ending = 0;
            prewarm(0x14, 1, 0x12);
            break;
        case KIND_SAND:                 /* WEATHER_SANDSTORM_Begin, then _BeginPlain */
            t = TABLE(0x021EEB48u, 32u);
            u = TABLE(0x021EEB68u, 32u);
            if (t == NULL || u == NULL) {
                ok = 0;
                break;
            }
            for (k = 0; k < 8; k++) {
                W.gx[k] = rd_s32(t + (size_t)k * 4u);
                W.gy[k] = rd_s32(u + (size_t)k * 4u);
            }
            W.gust = 0;
            W.sand_scroll = 0;
            spawner(1, 2);
            prewarm(24, 2, 2);
            W.sheet[0].visible = 1;
            break;
        case KIND_RAIN:                 /* WEATHER_RAIN_Begin, then _BeginPlain */
            spawner(2, 0);
            prewarm(0x14, 0xA, 1);
            break;
        case KIND_BLIZZARD:             /* WEATHER_BLIZZARD_Begin, then _BeginPlain */
            t = TABLE(0x021EF770u, 16u);
            u = TABLE(0x021EF780u, 16u);
            v = TABLE(0x021EF75Cu, 18u);        /* (the Y steps, then the X steps at 0x021EF765) */
            if (t == NULL || u == NULL || v == NULL) {
                ok = 0;
                break;
            }
            for (k = 0; k < 4; k++) {
                W.bwx[k] = rd_s32(t + (size_t)k * 4u);
                W.bwy[k] = rd_s32(u + (size_t)k * 4u);
            }
            memcpy(W.bsy, v, 9);
            memcpy(W.bsx, v + 9, 9);
            W.wind_phase = 0;
            W.bz_x = W.bz_y = W.bz_step = 0;
            spawner(1, 2);
            prewarm(0x14, 2, 2);
            W.sheet[0].visible = 1;
            break;
        case KIND_DOWNPOUR:             /* WEATHER_DOWNPOUR_Begin, then _BeginPlain */
            spawner(1, 1);
            prewarm(0x14, 10, 1);
            break;
        case KIND_DUST:                 /* WEATHER_DIAMOND_DUST_Begin, then _BeginPlain */
            spawner(0x14, 2);
            prewarm(0x14, 10, 1);
            break;
        case KIND_THUNDER:              /* WEATHER_THUNDERSTORM_Begin, then _BeginPlain */
            if ((t = TABLE(0x021EFD08u, 20u)) == NULL) {
                ok = 0;
                break;
            }
            for (k = 0; k < 5; k++) {
                W.th_scale[k] = rd_s32(t + (size_t)k * 4u);
            }
            W.intensity = 0;
            W.th_scroll = 0;
            spawner(2, 0);
            prewarm(0x14, 5, 1);
            W.sheet[0].visible = 1;
            break;
        case KIND_STORM:                /* WEATHER_STORM_Begin, then _BeginPlain */
            t = TABLE(0x021EFD74u, 6u * 12u);
            u = TABLE(0x021EFD3Cu, 14u * 4u);
            if (t == NULL || u == NULL) {
                ok = 0;
                break;
            }
            for (k = 0; k < 6; k++) {
                W.st_wx[k] = rd_s32(t + (size_t)k * 12u);
                W.st_wy[k] = rd_s32(t + (size_t)k * 12u + 4u);
            }
            for (k = 0; k < 14; k++) {
                W.st_scale[k] = rd_s32(u + (size_t)k * 4u);
            }
            memset(&W.st, 0, sizeof W.st);
            (void)rnd(16);              /* WEATHER_STORM_ResetThunderTimer (the thunder is a sound) */
            spawner(1, 1);
            prewarm(0x14, 5, 1);        /* (no drops yet: the gust phase is "none" until the first calm) */
            storm_calm();
            W.sheet[0].visible = 1;
            break;
        default:
            break;
        }
    }
#undef TABLE
    free(ov);
    if (!ok) {
        stop();
        return;
    }
    W.id = id;
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
        if (want > 0) {
            start(want);
            if (W.id < 0) {
                W.want = -1;            /* (no sprites, or its data could not be read: not asked for again every frame) */
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
        const Part *p = &W.p[cur];

        if (n < max && p->drawn && p->image >= 0 && p->image < W.nimg && W.img[p->image].rgba != NULL) {
            out[n].x = p->x + W.img[p->image].ox;
            out[n].y = p->y + W.img[p->image].oy;
            out[n].image = p->image;
            n++;
        }
        cur = p->next;
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

unsigned bweather_sheets(BweatherSheet *out, unsigned max)
{
    unsigned n = 0;
    int i;

    if (W.kind == KIND_NONE) {
        return 0;
    }
    for (i = 0; i < 2 && n < max; i++) {
        const Sheet *s = &W.sheet[i];

        if (s->rgba == NULL || !s->visible || s->w <= 0 || s->h <= 0) {
            continue;
        }
        out[n].rgba = s->rgba;
        out[n].w = s->w;
        out[n].h = s->h;
        out[n].off_x = s->off_x;
        out[n].off_y = s->off_y;
        out[n].scale_x = s->scale_x;
        out[n].scale_y = s->scale_y;
        out[n].across = W.sheet_w > 0 ? W.sheet_w : 256;
        out[n].down = W.sheet_h > 0 ? W.sheet_h : 192;
        n++;
    }
    return n;
}

int bweather_playing(void)
{
    return W.kind == KIND_NONE ? -1 : W.id;
}
