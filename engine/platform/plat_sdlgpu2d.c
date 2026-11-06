/*
 * engine/platform/plat_sdlgpu2d.c -- the DS 2D compositor on SDL's GPU
 * (docs/gpu-2d.md): plat_gl2d.c for the SDL3 GPU backend.
 *
 * One pass of shaders/g2d.hlsl per screen.  The CPU fills a G2dGpuFrame
 * (engine/g2d/g2d_gpu.h); this file sends it up and composes the screen
 * into the presenter's own picture of it, as plat_gl2d.c does into GL's
 * screen texture:
 *
 *   - a screen without the GPU's 3D: the pass at DS size into the screen's
 *     picture;
 *   - the screen whose 3D the 2D shader takes (s3d2, docs/gpu-2d.md phase
 *     3): the pass at DS size for the readers, then again at the size the
 *     screen is shown at, the 3D filtered up to it and the 2D pixels sharp;
 *   - the one-screen world's screen (s3d_from2d): the screen with the
 *     sentinel, then with the 3D off into `behind` and over a black 3D into
 *     `zero`, the pictures the world composite takes from the CPU otherwise.
 *
 * Every pass's DS-size result goes back to the CPU a frame late
 * (sg2d_pixels) for the readers of the pixels: the one-screen interface and
 * the held screens.  All textures are top-down, as everywhere in this
 * backend.
 */
#include <stdlib.h>
#include <string.h>

#include "plat_sdlgpu_int.h"
#include "g2d_gpu.h"
#include "shaders/shaders_gen.h"

/* The uniform layout g2d.hlsl reads (its comment). */
enum {
    U_PAL_COUNT = 0, U_TEXT = 18, U_AFF = 58, U_RAS = 84, U_L3 = 96, U_WIN = 100, U_BLD = 115,
    U_VRAM_SIZE = 121, U_OBJ_ON = 122, U_MB = 123, U_G3M = 124, U_G3SZ = 125, U_OSZ = 127,
    U_COUNT = 132
};

/* The offsets in the staging buffer: each part on its own 512-byte
 * boundary (Direct3D 12's rule for a texture copy's source). */
#define ALIGN512(x) (((x) + 511u) & ~511u)
#define PAL_BYTES (256u * 18u * 4u)
#define PIC_BYTES (256u * 192u * 4u)

typedef struct Sg2d {
    SDL_GPUGraphicsPipeline *pipe;
    SDL_GPUTexture *vram[2], *pal, *obj, *l3, *rast, *out1x;
    const void *vram_from[2];   /* the frame and the bytes each one holds */
    u32 vram_gen[2];
    int vram_h[2];
    SDL_GPUTransferBuffer *stage;
    Uint32 stage_size;
    SDL_GPUTransferBuffer *dl[2];   /* a screen's pixels on their way back */
    SDL_GPUFence *dl_fence[2];
    int dl_full[2];
    int failed;
} Sg2d;

static Sg2d *state(SdlGpu *s) { return (Sg2d *)s->g2d; }

static SDL_GPUTexture *tex(SdlGpu *s, SDL_GPUTextureFormat f, int w, int h, int target)
{
    return sg_texture(s, f, w, h, 1, target);
}

static int init(SdlGpu *s)
{
    Sg2d *t = state(s);
    SDL_GPUShader *fs;
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;

    if (t != NULL) {
        return t->failed ? -1 : 0;
    }
    t = (Sg2d *)calloc(1, sizeof *t);
    if (t == NULL) {
        return -1;
    }
    s->g2d = t;
    if (getenv("EIDOS_GPU_NO2D") != NULL || s->vs == NULL) {
        t->failed = 1;
        return -1;
    }
    fs = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, g2d_fs, G2D_FS);
    if (fs == NULL) {
        t->failed = 1;
        return -1;
    }
    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    ct.format = SG_RGBA;
    ci.vertex_shader = s->vs;
    ci.fragment_shader = fs;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    t->pipe = SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
    SDL_ReleaseGPUShader(s->dev, fs);
    t->pal = tex(s, SG_RGBA, 256, 18, 0);
    t->obj = tex(s, SG_RGBA, 256, 192, 0);
    t->l3 = tex(s, SG_RGBA, 256, 192, 0);
    t->rast = tex(s, SG_RGBA, 256, 192 * 4, 0);
    t->out1x = tex(s, SG_RGBA, 256, 192, 1);
    if (t->pipe == NULL || t->pal == NULL || t->obj == NULL || t->l3 == NULL || t->rast == NULL ||
        t->out1x == NULL) {
        t->failed = 1;
        return -1;
    }
    return 0;
}

int sg2d_ready(SdlGpu *s)
{
    return s != NULL && init(s) == 0;
}

void sg2d_destroy(SdlGpu *s)
{
    Sg2d *t = state(s);
    int i;

    if (t == NULL) {
        return;
    }
    for (i = 0; i < 2; i++) {
        if (t->vram[i] != NULL) SDL_ReleaseGPUTexture(s->dev, t->vram[i]);
        if (t->dl[i] != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, t->dl[i]);
        if (t->dl_fence[i] != NULL) SDL_ReleaseGPUFence(s->dev, t->dl_fence[i]);
    }
    if (t->pal != NULL) SDL_ReleaseGPUTexture(s->dev, t->pal);
    if (t->obj != NULL) SDL_ReleaseGPUTexture(s->dev, t->obj);
    if (t->l3 != NULL) SDL_ReleaseGPUTexture(s->dev, t->l3);
    if (t->rast != NULL) SDL_ReleaseGPUTexture(s->dev, t->rast);
    if (t->out1x != NULL) SDL_ReleaseGPUTexture(s->dev, t->out1x);
    if (t->stage != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, t->stage);
    if (t->pipe != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, t->pipe);
    free(t);
    s->g2d = NULL;
}

/* A region of the staging buffer into a texture. */
static void up(SDL_GPUCopyPass *cp, SDL_GPUTransferBuffer *tb, Uint32 offset, SDL_GPUTexture *dst,
               int x, int y, int w, int h, int row_px)
{
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion r;

    memset(&src, 0, sizeof src);
    memset(&r, 0, sizeof r);
    src.transfer_buffer = tb;
    src.offset = offset;
    src.pixels_per_row = (Uint32)row_px;
    src.rows_per_layer = (Uint32)h;
    r.texture = dst;
    r.x = (Uint32)x;
    r.y = (Uint32)y;
    r.w = (Uint32)w;
    r.h = (Uint32)h;
    r.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &r, false);
}

/* The frame's parameters as g2d.hlsl reads them. */
static void fill_uniforms(const G2dGpuFrame *f, int *v)
{
    int i;

    memset(v, 0, sizeof(int) * U_COUNT);
    for (i = 0; i < 18; i++) {
        v[U_PAL_COUNT + i] = (int)f->pal_count[i];
    }
    for (i = 0; i < 4; i++) {
        const G2dGpuText *t = &f->text[i];
        int *p = v + U_TEXT + i * 10;

        p[0] = t->enabled; p[1] = (int)t->priority; p[2] = t->hofs; p[3] = t->vofs;
        p[4] = (int)t->tiles_wide; p[5] = (int)t->tiles_high; p[6] = (int)t->size_code;
        p[7] = (int)t->map_ofs; p[8] = (int)t->char_ofs; p[9] = t->bpp8;
    }
    for (i = 0; i < 2; i++) {
        const G2dGpuAffine *a = &f->affine[i];
        int *p = v + U_AFF + i * 13;

        p[0] = a->enabled; p[1] = (int)a->priority; p[2] = (int)a->bg_index;
        p[3] = (int)a->tiles; p[4] = (int)a->map_ofs; p[5] = (int)a->char_ofs;
        p[6] = a->wrap; p[7] = a->ref_x; p[8] = a->ref_y;
        p[9] = a->pa; p[10] = a->pb; p[11] = a->pc; p[12] = a->pd;
    }
    for (i = 0; i < 4; i++) {
        v[U_RAS + i * 3] = f->raster[i].enabled;
        v[U_RAS + i * 3 + 1] = (int)f->raster[i].priority;
        v[U_RAS + i * 3 + 2] = (int)f->raster[i].bg_index;
    }
    v[U_L3] = f->l3_on;
    v[U_L3 + 1] = (int)f->l3_priority;
    v[U_L3 + 2] = (int)f->l3_bg_index;
    v[U_L3 + 3] = f->l3_hofs;
    {
        const G2dWindow *w[2];
        int k;

        w[0] = &f->windows.win0;
        w[1] = &f->windows.win1;
        for (k = 0; k < 2; k++) {
            v[U_WIN + k * 6] = w[k]->enabled;
            v[U_WIN + k * 6 + 1] = (int)w[k]->x1;
            v[U_WIN + k * 6 + 2] = (int)w[k]->x2;
            v[U_WIN + k * 6 + 3] = (int)w[k]->y1;
            v[U_WIN + k * 6 + 4] = (int)w[k]->y2;
            v[U_WIN + k * 6 + 5] = (int)w[k]->layers;
        }
        v[U_WIN + 12] = f->windows.obj_enabled;
        v[U_WIN + 13] = (int)f->windows.obj_layers;
        v[U_WIN + 14] = (int)f->windows.outside_layers;
    }
    v[U_BLD] = (int)f->blend.effect;
    v[U_BLD + 1] = (int)f->blend.first;
    v[U_BLD + 2] = (int)f->blend.second;
    v[U_BLD + 3] = (int)f->blend.eva;
    v[U_BLD + 4] = (int)f->blend.evb;
    v[U_BLD + 5] = (int)f->blend.evy;
    v[U_VRAM_SIZE] = (int)f->bgv_size;
    v[U_OBJ_ON] = f->obj_on;
    v[U_MB] = (int)f->master_bright;
}

/* One pass of the shader into all of `dst` (w x h). */
static void pass(SdlGpu *s, SDL_GPUCommandBuffer *cmd, const SDL_GPUTextureSamplerBinding *tb,
                 int *v, int g3m, SDL_GPUTexture *dst, int w, int h)
{
    Sg2d *t = state(s);
    SDL_GPUColorTargetInfo ct;
    SDL_GPURenderPass *rp;
    static const float full[4] = { -1.f, -1.f, 1.f, 1.f };

    memset(&ct, 0, sizeof ct);
    ct.texture = dst;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    v[U_G3M] = g3m;
    v[U_OSZ] = w;
    v[U_OSZ + 1] = h;
    rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_BindGPUGraphicsPipeline(rp, t->pipe);
    SDL_BindGPUFragmentSamplers(rp, 0, tb, 6);
    SDL_PushGPUVertexUniformData(cmd, 0, full, sizeof full);
    SDL_PushGPUFragmentUniformData(cmd, 0, v, sizeof(int) * U_COUNT);
    SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
}

/* The DS-size result of `src` down into `tb`. */
static void download(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *src, SDL_GPUTransferBuffer *tb)
{
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion r;
    SDL_GPUTextureTransferInfo to;

    memset(&r, 0, sizeof r);
    memset(&to, 0, sizeof to);
    r.texture = src;
    r.w = 256;
    r.h = 192;
    r.d = 1;
    to.transfer_buffer = tb;
    to.pixels_per_row = 256;
    to.rows_per_layer = 192;
    SDL_DownloadFromGPUTexture(cp, &r, &to);
    SDL_EndGPUCopyPass(cp);
}

static SDL_GPUTransferBuffer *download_buffer(SdlGpu *s)
{
    SDL_GPUTransferBufferCreateInfo ci;

    memset(&ci, 0, sizeof ci);
    ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci.size = PIC_BYTES;
    return SDL_CreateGPUTransferBuffer(s->dev, &ci);
}

int sg2d_pixels(SdlGpu *s, int screen, unsigned char *out)
{
    Sg2d *t = state(s);
    const unsigned char *px;

    if (t == NULL || screen < 0 || screen > 1 || !t->dl_full[screen] || out == NULL) {
        return -1;
    }
    if (t->dl_fence[screen] != NULL) {
        SDL_WaitForGPUFences(s->dev, true, &t->dl_fence[screen], 1);
        SDL_ReleaseGPUFence(s->dev, t->dl_fence[screen]);
        t->dl_fence[screen] = NULL;
    }
    t->dl_full[screen] = 0;
    px = (const unsigned char *)SDL_MapGPUTransferBuffer(s->dev, t->dl[screen], false);
    if (px == NULL) {
        return -1;
    }
    memcpy(out, px, PIC_BYTES);
    SDL_UnmapGPUTransferBuffer(s->dev, t->dl[screen]);
    return 0;
}

/*
 * Compose `f`: into `readback` (256 x 192 RGBA, top-down) when it is not
 * NULL, and into screen `to_screen`'s picture when that is 0 or 1 (with its
 * pixels on their way back for sg2d_pixels).  plat_gl2d.c
 * eidos_gl2d_compose, the same cases.
 */
int sg2d_compose(SdlGpu *s, const G2dGpuFrame *f, unsigned char *readback, int to_screen)
{
    Sg2d *t;
    static int v[U_COUNT];
    SDL_GPUTextureSamplerBinding tb[6];
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    unsigned char *map;
    Uint32 vram_off = 0, pal_off, obj_off, l3_off, rast_off, need;
    int i, rows, vi, same, g3, from2d, shown;
    SDL_GPUTexture *first;

    if (f == NULL || !f->valid || init(s) != 0) {
        return -1;
    }
    t = state(s);
    shown = to_screen == 0 || to_screen == 1;
    /* phase 3: the screen whose 3D the GPU drew takes it from there */
    g3 = shown && to_screen == s->s3d2 && f->l3_on && s->have3d && s->fb[0].color != NULL &&
         s->fb[0].w > 0 && s->fb[0].h > 0;
    from2d = shown && to_screen == s->s3d && s->s3d_from2d && f->l3_on;

    rows = (int)((f->bgv_size + 1023u) / 1024u);
    if (rows < 1) {
        rows = 1;
    }
    vi = f->screen & 1;             /* one BG window texture per screen */
    if (rows > t->vram_h[vi]) {
        if (t->vram[vi] != NULL) {
            SDL_ReleaseGPUTexture(s->dev, t->vram[vi]);
        }
        t->vram[vi] = tex(s, SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1024, rows, 0);
        t->vram_h[vi] = t->vram[vi] != NULL ? rows : 0;
        t->vram_from[vi] = NULL;
    }
    if (t->vram[vi] == NULL) {
        return -1;
    }
    same = t->vram_from[vi] == (const void *)f && t->vram_gen[vi] == f->bgv_gen;

    /* the staging buffer: the BG window (when it changed), the palettes,
     * the OBJ line buffer, the CPU's 3D layer, the rasters in use */
    pal_off = ALIGN512(same ? 0u : f->bgv_size);
    obj_off = ALIGN512(pal_off + PAL_BYTES);
    l3_off = ALIGN512(obj_off + PIC_BYTES);
    rast_off = ALIGN512(l3_off + PIC_BYTES);
    need = rast_off + 4u * PIC_BYTES;
    if (t->stage == NULL || t->stage_size < need) {
        SDL_GPUTransferBufferCreateInfo ci;

        if (t->stage != NULL) {
            SDL_ReleaseGPUTransferBuffer(s->dev, t->stage);
        }
        memset(&ci, 0, sizeof ci);
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ci.size = need;
        t->stage = SDL_CreateGPUTransferBuffer(s->dev, &ci);
        t->stage_size = t->stage != NULL ? need : 0;
        if (t->stage == NULL) {
            return -1;
        }
    }
    map = (unsigned char *)SDL_MapGPUTransferBuffer(s->dev, t->stage, true);
    if (map == NULL) {
        return -1;
    }
    if (!same && f->bgv_size > 0u) {
        memcpy(map + vram_off, f->bgv, f->bgv_size);
    }
    for (i = 0; i < 18 * 256; i++) {
        unsigned char *p = map + pal_off + (Uint32)i * 4u;

        p[0] = f->pal[i / 256][i % 256][0];
        p[1] = f->pal[i / 256][i % 256][1];
        p[2] = f->pal[i / 256][i % 256][2];
        p[3] = 255;
    }
    memcpy(map + obj_off, f->obj, PIC_BYTES);
    if (f->l3_on && !g3) {
        memcpy(map + l3_off, f->l3, PIC_BYTES);
    }
    for (i = 0; i < 4; i++) {
        if (f->raster[i].enabled) {
            memcpy(map + rast_off + (Uint32)i * PIC_BYTES, f->raster[i].rgba, PIC_BYTES);
        }
    }
    SDL_UnmapGPUTransferBuffer(s->dev, t->stage);

    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (cmd == NULL) {
        return -1;
    }
    cp = SDL_BeginGPUCopyPass(cmd);
    if (!same && f->bgv_size >= 1024u) {
        up(cp, t->stage, vram_off, t->vram[vi], 0, 0, 1024, (int)(f->bgv_size / 1024u), 1024);
    }
    if (!same && f->bgv_size % 1024u != 0u) {
        /* the last part row: its own copy, from its own aligned place */
        Uint32 tail = (f->bgv_size / 1024u) * 1024u;

        if (tail % 512u == 0u) {
            up(cp, t->stage, vram_off + tail, t->vram[vi], 0, (int)(f->bgv_size / 1024u),
               (int)(f->bgv_size % 1024u), 1, 1024);
        }
    }
    t->vram_from[vi] = f;
    t->vram_gen[vi] = f->bgv_gen;
    up(cp, t->stage, pal_off, t->pal, 0, 0, 256, 18, 256);
    up(cp, t->stage, obj_off, t->obj, 0, 0, 256, 192, 256);
    if (f->l3_on && !g3) {
        up(cp, t->stage, l3_off, t->l3, 0, 0, 256, 192, 256);
    }
    for (i = 0; i < 4; i++) {
        if (f->raster[i].enabled) {
            up(cp, t->stage, rast_off + (Uint32)i * PIC_BYTES, t->rast, 0, i * 192, 256, 192, 256);
        }
    }
    SDL_EndGPUCopyPass(cp);

    fill_uniforms(f, v);
    v[U_G3SZ] = s->fb[0].w;
    v[U_G3SZ + 1] = s->fb[0].h;
    tb[0].texture = t->vram[vi];
    tb[1].texture = t->pal;
    tb[2].texture = t->obj;
    tb[3].texture = t->l3;
    tb[4].texture = t->rast;
    tb[5].texture = g3 ? s->fb[0].color : t->pal;   /* bound, never read */
    for (i = 0; i < 5; i++) {
        tb[i].sampler = s->nearest;
    }
    tb[5].sampler = s->linear;

    /* the pass at DS size: into the screen's picture, or (phase 3, or no
     * screen) into out1x for the readers */
    if (shown && !g3) {
        if (sg_pic_target(s, &s->screen[to_screen], 256, 192) != 0) {
            SDL_CancelGPUCommandBuffer(cmd);
            return -1;
        }
        first = s->screen[to_screen].tex;
    } else {
        first = t->out1x;
    }
    pass(s, cmd, tb, v, g3 ? 1 : from2d ? 3 : 0, first, 256, 192);

    if (readback != NULL) {
        SDL_GPUTransferBuffer *db = download_buffer(s);
        SDL_GPUFence *fence;
        const unsigned char *px;

        if (db == NULL) {
            SDL_CancelGPUCommandBuffer(cmd);
            return -1;
        }
        download(cmd, first, db);
        if (!shown) {
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence == NULL) {
                SDL_ReleaseGPUTransferBuffer(s->dev, db);
                return -1;
            }
            SDL_WaitForGPUFences(s->dev, true, &fence, 1);
            SDL_ReleaseGPUFence(s->dev, fence);
            px = (const unsigned char *)SDL_MapGPUTransferBuffer(s->dev, db, false);
            if (px != NULL) {
                memcpy(readback, px, PIC_BYTES);
                SDL_UnmapGPUTransferBuffer(s->dev, db);
            }
            SDL_ReleaseGPUTransferBuffer(s->dev, db);
            return px != NULL ? 0 : -1;
        }
        /* a shown screen and a readback together: the plain readback path
         * is the only caller of either, so keep it simple and wait */
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence != NULL) {
            SDL_WaitForGPUFences(s->dev, true, &fence, 1);
            SDL_ReleaseGPUFence(s->dev, fence);
            px = (const unsigned char *)SDL_MapGPUTransferBuffer(s->dev, db, false);
            if (px != NULL) {
                memcpy(readback, px, PIC_BYTES);
                SDL_UnmapGPUTransferBuffer(s->dev, db);
            }
        }
        SDL_ReleaseGPUTransferBuffer(s->dev, db);
        cmd = SDL_AcquireGPUCommandBuffer(s->dev);
        if (cmd == NULL) {
            return -1;
        }
    }
    if (!shown) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return 0;
    }

    if (g3) {
        /* the screen again at the 3D's size (at least the size it is shown
         * at), each pixel with the 3D pixel under it: what the window shows */
        int ow = s->fb[0].w > s->s3d2_w ? s->fb[0].w : s->s3d2_w;
        int oh = s->fb[0].h > s->s3d2_h ? s->fb[0].h : s->s3d2_h;

        ow = ow > 4096 ? 4096 : ow;
        oh = oh > 4096 ? 4096 : oh;
        if (sg_pic_target(s, &s->screen[to_screen], ow, oh) != 0) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return -1;
        }
        pass(s, cmd, tb, v, 2, s->screen[to_screen].tex, ow, oh);
    }
    /* and back to the CPU without waiting (sg2d_pixels) */
    if (t->dl[to_screen] == NULL) {
        t->dl[to_screen] = download_buffer(s);
    }
    if (t->dl_fence[to_screen] != NULL) {
        /* the last one was never read: its pixels are out of date */
        SDL_ReleaseGPUFence(s->dev, t->dl_fence[to_screen]);
        t->dl_fence[to_screen] = NULL;
    }
    t->dl_full[to_screen] = 0;
    if (t->dl[to_screen] != NULL) {
        download(cmd, first, t->dl[to_screen]);
    }
    if (from2d) {
        /* the world composite's other two screens: the 3D off, and (where a
         * 2D layer can blend over the 3D) a black 3D */
        if (sg_pic_target(s, &s->behind, 256, 192) == 0) {
            pass(s, cmd, tb, v, 5, s->behind.tex, 256, 192);
        }
        s->has_zero = 0;
        if ((f->blend.second & 1u) != 0u && sg_pic_target(s, &s->zero, 256, 192) == 0) {
            pass(s, cmd, tb, v, 4, s->zero.tex, 256, 192);
            s->has_zero = 1;
        }
    }
    if (t->dl[to_screen] != NULL) {
        t->dl_fence[to_screen] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        t->dl_full[to_screen] = t->dl_fence[to_screen] != NULL;
    } else {
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    return 0;
}
