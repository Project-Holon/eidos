/*
 * engine/platform/plat_sdlgpu.c -- the SDL3 GPU backend (plat_gpu.h,
 * docs/graphics-backends.md): Direct3D 12 on Windows, Metal on Apple,
 * Vulkan elsewhere, through SDL's GPU API.
 *
 * This first part is the presenter: the two DS screens, the one-screen
 * layout (the DS screen in its rectangle, the blur behind a modal, the
 * interface canvas over it) and the run-status panel.  The 2D compositor
 * and the 3D are not here yet: their operations say "not available", and
 * the platform layer then composes and rasterises on the CPU, as it does
 * when the OpenGL backend has no 3D.
 *
 * Every frame is drawn into `scene`, an RGBA8 texture of the window's size,
 * then blitted to the swapchain: every pipeline has one target format
 * whatever the swapchain's is, and a grab reads the scene back.  Uploads go
 * through one transfer buffer per destination (cycled), flushed in one copy
 * pass at the start of the frame.
 *
 * The shaders are the .hlsl files in engine/platform/shaders, compiled by
 * tools/shaders.py into shaders_gen.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "plat_gpu.h"
#include "shaders/shaders_gen.h"

#define SCENE_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM

/* A picture the CPU makes and the GPU shows: its texture, its size and the
 * transfer buffer it goes up through. */
typedef struct Pic {
    SDL_GPUTexture *tex;
    SDL_GPUTransferBuffer *up;
    int w, h;                   /* the texture's size, 0: none           */
    int up_size;                /* the transfer buffer's bytes           */
    int dirty;                  /* new pixels wait in `up`               */
} Pic;

typedef struct SdlGpu {
    EidosGpu base;              /* first: the backend's head (plat_gpu.h) */
    SDL_Window *window;
    SDL_GPUDevice *dev;
    SDL_GPUShader *vs;          /* present.hlsl's: a quad in a rectangle */
    SDL_GPUGraphicsPipeline *p_present, *p_blur, *p_canvas;
    SDL_GPUSampler *linear, *nearest;
    Pic screen[2], canvas, panel;
    SDL_GPUTexture *scene, *blur[2];
    int scene_w, scene_h, blur_w, blur_h, blur1_w, blur1_h;
    SDL_GPUTexture *grab_tex;   /* the scene's copy a grab reads         */
    char driver[32];

    /* the one-screen layout (plat_gl.c's g_one, the part this backend
     * draws) */
    int on, world, which, hide2d, blur_amount, no_fade, hold;
    SDL_Rect ds;                /* the DS screen's rectangle             */
    SDL_Rect crect;             /* the canvas's rectangle, w 0: none     */
    int bottom;                 /* window pixels the world leaves free   */
    float bd[3];                /* the backdrop beyond the DS screen     */
    SDL_Rect prect;             /* the panel's rectangle, w 0: none      */
} SdlGpu;

static SdlGpu *sg(EidosGpu *g) { return (SdlGpu *)g; }

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static SDL_GPUShader *make_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
                                  const unsigned char *dxil, size_t dxil_n,
                                  const unsigned char *spirv, size_t spirv_n,
                                  const unsigned char *msl, size_t msl_n,
                                  const char *entry, int samplers, int uniforms)
{
    SDL_GPUShaderCreateInfo ci;
    SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(dev);

    memset(&ci, 0, sizeof ci);
    ci.entrypoint = entry;
    ci.stage = stage;
    ci.num_samplers = (Uint32)samplers;
    ci.num_uniform_buffers = (Uint32)uniforms;
    if (have & SDL_GPU_SHADERFORMAT_DXIL) {
        ci.format = SDL_GPU_SHADERFORMAT_DXIL;
        ci.code = dxil;
        ci.code_size = dxil_n;
    } else if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.code = spirv;
        ci.code_size = spirv_n;
    } else if (have & SDL_GPU_SHADERFORMAT_MSL) {
        ci.format = SDL_GPU_SHADERFORMAT_MSL;
        ci.code = msl;
        ci.code_size = msl_n - 1;   /* the text, not its NUL */
    } else {
        return NULL;
    }
    return SDL_CreateGPUShader(dev, &ci);
}

#define SHADER(dev, stage, name, NAME) \
    make_shader(dev, stage, name##_dxil, sizeof name##_dxil, name##_spirv, sizeof name##_spirv, \
                name##_msl, sizeof name##_msl, NAME##_ENTRY, NAME##_SAMPLERS, NAME##_UNIFORMS)

/* A pipeline drawing a strip of four vertices (the quad) into an RGBA8
 * target; `blend` weighs a straight-alpha picture over what is there. */
static SDL_GPUGraphicsPipeline *make_pipeline(SdlGpu *s, SDL_GPUShader *fs, int blend)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;

    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    ct.format = SCENE_FORMAT;
    if (blend) {
        /* straight RGBA (engine/ui/kit.c): weighed by its alpha, so a
         * fading panel does not brighten toward white (plat_gl.c) */
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    ci.vertex_shader = s->vs;
    ci.fragment_shader = fs;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
}

static SDL_GPUSampler *make_sampler(SDL_GPUDevice *dev, SDL_GPUFilter f)
{
    SDL_GPUSamplerCreateInfo ci;

    memset(&ci, 0, sizeof ci);
    ci.min_filter = f;
    ci.mag_filter = f;
    ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(dev, &ci);
}

static SDL_GPUTexture *make_texture(SDL_GPUDevice *dev, int w, int h, int target)
{
    SDL_GPUTextureCreateInfo ci;

    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SCENE_FORMAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    return SDL_CreateGPUTexture(dev, &ci);
}

static void free_pic(SDL_GPUDevice *dev, Pic *p)
{
    if (p->tex != NULL) SDL_ReleaseGPUTexture(dev, p->tex);
    if (p->up != NULL) SDL_ReleaseGPUTransferBuffer(dev, p->up);
    memset(p, 0, sizeof *p);
}

static void op_destroy(EidosGpu *g)
{
    SdlGpu *s = sg(g);
    int i;

    if (s->dev != NULL) {
        SDL_WaitForGPUIdle(s->dev);
        for (i = 0; i < 2; i++) {
            free_pic(s->dev, &s->screen[i]);
            if (s->blur[i] != NULL) SDL_ReleaseGPUTexture(s->dev, s->blur[i]);
        }
        free_pic(s->dev, &s->canvas);
        free_pic(s->dev, &s->panel);
        if (s->scene != NULL) SDL_ReleaseGPUTexture(s->dev, s->scene);
        if (s->p_present != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_present);
        if (s->p_blur != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_blur);
        if (s->p_canvas != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_canvas);
        if (s->vs != NULL) SDL_ReleaseGPUShader(s->dev, s->vs);
        if (s->linear != NULL) SDL_ReleaseGPUSampler(s->dev, s->linear);
        if (s->nearest != NULL) SDL_ReleaseGPUSampler(s->dev, s->nearest);
        SDL_ReleaseWindowFromGPUDevice(s->dev, s->window);
        SDL_DestroyGPUDevice(s->dev);
    }
    free(s);
}

/* ------------------------------------------------------------------ */
/* Pictures                                                            */
/* ------------------------------------------------------------------ */

/* New pixels for a picture: the texture is made (again) at their size and
 * the pixels wait in its transfer buffer for the next frame's copy pass. */
static void pic_set(SdlGpu *s, Pic *p, const unsigned char *rgba, int w, int h)
{
    int bytes = w * h * 4;
    void *dst;

    if (rgba == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (p->tex == NULL || p->w != w || p->h != h) {
        if (p->tex != NULL) {
            SDL_ReleaseGPUTexture(s->dev, p->tex);
        }
        p->tex = make_texture(s->dev, w, h, 0);
        p->w = w;
        p->h = h;
    }
    if (p->up == NULL || p->up_size < bytes) {
        SDL_GPUTransferBufferCreateInfo ci;

        if (p->up != NULL) {
            SDL_ReleaseGPUTransferBuffer(s->dev, p->up);
        }
        memset(&ci, 0, sizeof ci);
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ci.size = (Uint32)bytes;
        p->up = SDL_CreateGPUTransferBuffer(s->dev, &ci);
        p->up_size = p->up != NULL ? bytes : 0;
    }
    if (p->up == NULL || p->tex == NULL) {
        return;
    }
    dst = SDL_MapGPUTransferBuffer(s->dev, p->up, true);
    if (dst == NULL) {
        return;
    }
    memcpy(dst, rgba, (size_t)bytes);
    SDL_UnmapGPUTransferBuffer(s->dev, p->up);
    p->dirty = 1;
}

static void pic_flush(SDL_GPUCopyPass *cp, Pic *p)
{
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;

    if (!p->dirty || p->tex == NULL || p->up == NULL) {
        return;
    }
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = p->up;
    src.pixels_per_row = (Uint32)p->w;
    src.rows_per_layer = (Uint32)p->h;
    dst.texture = p->tex;
    dst.w = (Uint32)p->w;
    dst.h = (Uint32)p->h;
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    p->dirty = 0;
}

static void op_upload(EidosGpu *g, int i, const unsigned char *rgba, int w, int h)
{
    if (i >= 0 && i < 2) {
        pic_set(sg(g), &sg(g)->screen[i], rgba, w, h);
    }
}

static void op_upload_panel(EidosGpu *g, const unsigned char *rgba, int w, int h)
{
    pic_set(sg(g), &sg(g)->panel, rgba, w, h);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

typedef struct Target {
    SDL_GPUTexture *tex;
    int w, h;
} Target;

/* A render pass into `t`, cleared to `rgb` when clear is set. */
static SDL_GPURenderPass *begin(SDL_GPUCommandBuffer *cmd, Target t, int clear, float r, float g,
                                float b)
{
    SDL_GPUColorTargetInfo ct;

    memset(&ct, 0, sizeof ct);
    ct.texture = t.tex;
    ct.clear_color.r = r;
    ct.clear_color.g = g;
    ct.clear_color.b = b;
    ct.clear_color.a = 1.f;
    ct.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    return SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
}

/* The picture `tex` (tw x th) through pipeline `p` into the window
 * rectangle `r` of a target out_w x out_h (y down); `fs` the fragment
 * uniforms. */
static void quad(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, SDL_GPUGraphicsPipeline *p,
                 SDL_GPUTexture *tex, SDL_GPUSampler *smp, SDL_Rect r, int out_w, int out_h,
                 const void *fs, Uint32 fs_size)
{
    float rect[4];
    SDL_GPUTextureSamplerBinding tb;

    rect[0] = 2.f * (float)r.x / (float)out_w - 1.f;
    rect[1] = 1.f - 2.f * (float)(r.y + r.h) / (float)out_h;
    rect[2] = 2.f * (float)(r.x + r.w) / (float)out_w - 1.f;
    rect[3] = 1.f - 2.f * (float)r.y / (float)out_h;
    tb.texture = tex;
    tb.sampler = smp;
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, rect, sizeof rect);
    if (fs != NULL) {
        SDL_PushGPUFragmentUniformData(cmd, 0, fs, fs_size);
    }
    SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
}

/* A screen with the sharp scaling (present.hlsl). */
static void draw_pic(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, const Pic *p,
                     SDL_Rect r, int out_w, int out_h)
{
    float u[4];

    if (p->tex == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    u[0] = (float)p->w;
    u[1] = (float)p->h;
    u[2] = (float)r.w;
    u[3] = (float)r.h;
    quad(cmd, pass, s->p_present, p->tex, s->linear, r, out_w, out_h, u, sizeof u);
}

/* The two screens into their rectangles over `clear`. */
static void draw_screens(SdlGpu *s, SDL_GPUCommandBuffer *cmd, Target t, const SDL_Rect dst[2],
                         float clear)
{
    SDL_GPURenderPass *pass = begin(cmd, t, 1, clear, clear, clear);
    int i;

    for (i = 0; i < 2; i++) {
        draw_pic(s, cmd, pass, &s->screen[i], dst[i], t.w, t.h);
    }
    if (!s->on && s->prect.w > 0 && s->panel.tex != NULL) {
        draw_pic(s, cmd, pass, &s->panel, s->prect, t.w, t.h);
    }
    SDL_EndGPURenderPass(pass);
}

static void ensure_target(SdlGpu *s, SDL_GPUTexture **tex, int *tw, int *th, int w, int h)
{
    if (*tex == NULL || *tw != w || *th != h) {
        if (*tex != NULL) {
            SDL_ReleaseGPUTexture(s->dev, *tex);
        }
        *tex = make_texture(s->dev, w, h, 1);
        *tw = w;
        *th = h;
    }
}

/* One blur pass (blur.hlsl): `src` through the blur into `t`'s rectangle. */
static void blur_pass(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *src, Target t,
                      SDL_Rect r, float dx, float dy, float tint)
{
    SDL_GPURenderPass *pass = begin(cmd, t, 0, 0, 0, 0);
    float u[8];

    memset(u, 0, sizeof u);
    u[0] = (float)r.w;
    u[1] = (float)r.h;
    u[2] = dx;
    u[3] = dy;
    u[4] = tint;
    quad(cmd, pass, s->p_blur, src, s->linear, r, t.w, t.h, u, sizeof u);
    SDL_EndGPURenderPass(pass);
}

/* The one-screen world without the GPU 3D: the DS screen in its
 * rectangle, the backdrop around it, blurred behind a modal; the world
 * leaves `bottom` window pixels free at the bottom. */
static void draw_world(SdlGpu *s, SDL_GPUCommandBuffer *cmd, Target t)
{
    int wh = t.h - (s->bottom > 0 && s->bottom < t.h / 2 ? s->bottom : 0);
    SDL_Rect d[2], world;
    float clear;

    memset(d, 0, sizeof d);
    world.x = 0;
    world.y = 0;
    world.w = t.w;
    world.h = wh;
    if (s->hold) {
        /* an app over the field with no picture of it kept: a plain dark
         * ground (the kept picture comes with the GPU 3D) */
        SDL_GPURenderPass *pass = begin(cmd, t, 1, 0.04f, 0.04f, 0.05f);

        SDL_EndGPURenderPass(pass);
        return;
    }
    d[s->which] = s->ds;
    clear = s->no_fade ? 0.f : (s->bd[0] + s->bd[1] + s->bd[2]) / 3.f;
    if (s->blur_amount <= 0 || s->p_blur == NULL) {
        draw_screens(s, cmd, t, d, clear);
        return;
    }
    {
        /* the strength eases in and out: the taps spread and the tint
         * darkens with it (plat_gl.c one_blur) */
        float a = (float)s->blur_amount / 16.f;
        float step = (float)wh / 180.f * a;
        Target b0, b1;
        SDL_Rect full;

        ensure_target(s, &s->blur[0], &s->blur_w, &s->blur_h, t.w, wh);
        ensure_target(s, &s->blur[1], &s->blur1_w, &s->blur1_h, t.w, wh);
        b0.tex = s->blur[0];
        b1.tex = s->blur[1];
        b0.w = b1.w = t.w;
        b0.h = b1.h = wh;
        full.x = full.y = 0;
        full.w = t.w;
        full.h = wh;
        draw_screens(s, cmd, b0, d, clear);
        blur_pass(s, cmd, s->blur[0], b1, full, step, 0.f, 1.f);
        {
            SDL_GPURenderPass *pass = begin(cmd, t, 1, 0, 0, 0);

            SDL_EndGPURenderPass(pass);
        }
        blur_pass(s, cmd, s->blur[1], t, world, 0.f, step, 1.f - 0.55f * a);
    }
}

/* The interface canvas over the scene. */
static void draw_canvas(SdlGpu *s, SDL_GPUCommandBuffer *cmd, Target t)
{
    SDL_GPURenderPass *pass;

    if (s->canvas.tex == NULL || s->crect.w <= 0 || s->crect.h <= 0 || s->p_canvas == NULL) {
        return;
    }
    pass = begin(cmd, t, 0, 0, 0, 0);
    quad(cmd, pass, s->p_canvas, s->canvas.tex, s->nearest, s->crect, t.w, t.h, NULL, 0);
    SDL_EndGPURenderPass(pass);
}

/* The frame into the scene texture: the uploads first, then the layout's
 * picture, or the two screens. */
static Target draw_scene(SdlGpu *s, SDL_GPUCommandBuffer *cmd, int out_w, int out_h,
                         const SDL_Rect dst[2])
{
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    Target t;
    int i;

    for (i = 0; i < 2; i++) {
        pic_flush(cp, &s->screen[i]);
    }
    pic_flush(cp, &s->canvas);
    pic_flush(cp, &s->panel);
    SDL_EndGPUCopyPass(cp);
    ensure_target(s, &s->scene, &s->scene_w, &s->scene_h, out_w, out_h);
    t.tex = s->scene;
    t.w = out_w;
    t.h = out_h;
    if (s->on && s->world) {
        draw_world(s, cmd, t);
    } else {
        draw_screens(s, cmd, t, dst, 0.f);
    }
    if (s->on) {
        draw_canvas(s, cmd, t);
    }
    return t;
}

static void op_present_panel(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2],
                             const SDL_Rect *panel)
{
    SdlGpu *s = sg(g);
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *swap = NULL;
    Uint32 sw = 0, shh = 0;
    Target t;

    if (out_w <= 0 || out_h <= 0) {
        return;
    }
    if (panel != NULL) {
        s->prect = *panel;
    } else {
        s->prect.w = s->prect.h = 0;
    }
    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (cmd == NULL) {
        return;
    }
    t = draw_scene(s, cmd, out_w, out_h, dst);
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, s->window, &swap, &sw, &shh) || swap == NULL) {
        SDL_SubmitGPUCommandBuffer(cmd);   /* the uploads still happen */
        return;
    }
    {
        SDL_GPUBlitInfo bi;

        memset(&bi, 0, sizeof bi);
        bi.source.texture = t.tex;
        bi.source.w = (Uint32)t.w;
        bi.source.h = (Uint32)t.h;
        bi.destination.texture = swap;
        bi.destination.w = sw;
        bi.destination.h = shh;
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &bi);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

static void op_present(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2])
{
    op_present_panel(g, out_w, out_h, dst, NULL);
}

/* The window's picture read back: the scene drawn as present draws it,
 * downloaded instead of shown.  rgb is out_w x out_h x 3, top-down. */
static int op_grab(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2], unsigned char *rgb)
{
    SdlGpu *s = sg(g);
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTransferBuffer *down;
    SDL_GPUTransferBufferCreateInfo ci;
    SDL_GPUFence *fence;
    const unsigned char *px;
    Target t;
    int i, n;

    if (rgb == NULL || out_w <= 0 || out_h <= 0) {
        return -1;
    }
    memset(&ci, 0, sizeof ci);
    ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci.size = (Uint32)(out_w * out_h * 4);
    down = SDL_CreateGPUTransferBuffer(s->dev, &ci);
    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (down == NULL || cmd == NULL) {
        if (down != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, down);
        if (cmd != NULL) SDL_CancelGPUCommandBuffer(cmd);
        return -1;
    }
    t = draw_scene(s, cmd, out_w, out_h, dst);
    {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion src;
        SDL_GPUTextureTransferInfo to;

        memset(&src, 0, sizeof src);
        memset(&to, 0, sizeof to);
        src.texture = t.tex;
        src.w = (Uint32)t.w;
        src.h = (Uint32)t.h;
        src.d = 1;
        to.transfer_buffer = down;
        to.pixels_per_row = (Uint32)t.w;
        to.rows_per_layer = (Uint32)t.h;
        SDL_DownloadFromGPUTexture(cp, &src, &to);
        SDL_EndGPUCopyPass(cp);
    }
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        SDL_ReleaseGPUTransferBuffer(s->dev, down);
        return -1;
    }
    SDL_WaitForGPUFences(s->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(s->dev, fence);
    px = (const unsigned char *)SDL_MapGPUTransferBuffer(s->dev, down, false);
    if (px == NULL) {
        SDL_ReleaseGPUTransferBuffer(s->dev, down);
        return -1;
    }
    n = out_w * out_h;
    for (i = 0; i < n; i++) {
        rgb[i * 3 + 0] = px[i * 4 + 0];
        rgb[i * 3 + 1] = px[i * 4 + 1];
        rgb[i * 3 + 2] = px[i * 4 + 2];
    }
    SDL_UnmapGPUTransferBuffer(s->dev, down);
    SDL_ReleaseGPUTransferBuffer(s->dev, down);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The one-screen layout                                               */
/* ------------------------------------------------------------------ */

static void op_one_set(EidosGpu *g, int on, int world, int screen, const SDL_Rect *ds, int hide_2d,
                       int blur, int no_fade)
{
    SdlGpu *s = sg(g);

    s->on = on != 0;
    s->world = on && world && ds != NULL && screen >= 0 && screen < 2;
    s->which = screen >= 0 && screen < 2 ? screen : 0;
    s->hide2d = hide_2d != 0;
    s->blur_amount = blur < 0 ? 0 : blur > 16 ? 16 : blur;
    s->no_fade = (no_fade & 1) != 0;
    s->hold = (no_fade & 2) != 0;
    if (ds != NULL) {
        s->ds = *ds;
    }
}

static void op_one_canvas(EidosGpu *g, const unsigned char *rgba, int w, int h,
                          const SDL_Rect *rect)
{
    SdlGpu *s = sg(g);

    if (rect == NULL || w <= 0 || h <= 0) {
        s->crect.w = s->crect.h = 0;
        return;
    }
    s->crect = *rect;
    pic_set(s, &s->canvas, rgba, w, h);
}

static void op_one_bottom(EidosGpu *g, int px) { sg(g)->bottom = px > 0 ? px : 0; }

static void op_set_world_backdrop(EidosGpu *g, unsigned rgb)
{
    SdlGpu *s = sg(g);

    s->bd[0] = (float)((rgb >> 16) & 0xFFu) / 255.f;
    s->bd[1] = (float)((rgb >> 8) & 0xFFu) / 255.f;
    s->bd[2] = (float)(rgb & 0xFFu) / 255.f;
}

/* ------------------------------------------------------------------ */
/* Not in this backend yet: the platform layer takes the CPU's path     */
/* ------------------------------------------------------------------ */

static int op_matches_3d(const EidosGpu *g, int screen, int out_w, int out_h)
{ (void)g; (void)screen; (void)out_w; (void)out_h; return 0; }
static int op_compose_2d(EidosGpu *g, const struct G2dGpuFrame *f, unsigned char *readback,
                         int to_screen)
{ (void)g; (void)f; (void)readback; (void)to_screen; return -1; }
static int op_ready_2d(EidosGpu *g) { (void)g; return 0; }
static int op_pixels_2d(EidosGpu *g, int screen, unsigned char *out)
{ (void)g; (void)screen; (void)out; return -1; }
static unsigned op_view_keep3d(EidosGpu *g, unsigned tex, int w, int h)
{ (void)g; (void)tex; (void)w; (void)h; return 0; }
static unsigned op_view_upload(EidosGpu *g, unsigned tex, const unsigned char *rgba, int w, int h,
                               int same_size)
{ (void)g; (void)tex; (void)rgba; (void)w; (void)h; (void)same_size; return 0; }
static void op_view_free(EidosGpu *g, unsigned tex) { (void)g; (void)tex; }
static void op_view_begin(EidosGpu *g, int win_w, int win_h, unsigned rgb)
{ (void)g; (void)win_w; (void)win_h; (void)rgb; }
static void op_view_draw(EidosGpu *g, unsigned tex, int mode, int x, int y, int w, int h, int flip,
                         const int *clip)
{ (void)g; (void)tex; (void)mode; (void)x; (void)y; (void)w; (void)h; (void)flip; (void)clip; }
static void op_view_draw_fx(EidosGpu *g, unsigned tex, int x, int y, int w, int h, const int *clip,
                            const float fade[4], float alpha)
{ (void)g; (void)tex; (void)x; (void)y; (void)w; (void)h; (void)clip; (void)fade; (void)alpha; }
static int op_view_read(EidosGpu *g, unsigned char *rgba) { (void)g; (void)rgba; return -1; }
static void op_view_end(EidosGpu *g) { (void)g; }
static int op_has3d(const EidosGpu *g) { (void)g; return 0; }
static int op_capture(EidosGpu *g, const unsigned char *a2d, const unsigned char *behind,
                      const struct G3dSnapshot *snap, int scale, int blend, int eva, int evb,
                      const unsigned char *b1x)
{
    (void)g; (void)a2d; (void)behind; (void)snap; (void)scale; (void)blend; (void)eva; (void)evb;
    (void)b1x;
    return -1;
}
static void op_show_capture(EidosGpu *g, int screen, int bmode, float bfac)
{ (void)g; (void)screen; (void)bmode; (void)bfac; }
static int op_render3d(EidosGpu *g, const struct G3dSnapshot *snap, int w, int h)
{ (void)g; (void)snap; (void)w; (void)h; return -1; }
static void op_set_edges(EidosGpu *g, int mode) { (void)g; (void)mode; }
static void op_set_3d(EidosGpu *g, int screen, const unsigned char *behind, int w, int h, int bmode,
                      float bfac, int dilate)
{ (void)g; (void)screen; (void)behind; (void)w; (void)h; (void)bmode; (void)bfac; (void)dilate; }
static void op_set_3d2d(EidosGpu *g, int screen) { (void)g; (void)screen; }
static void op_set_3d_from2d(EidosGpu *g, int screen, int bmode, float bfac)
{ (void)g; (void)screen; (void)bmode; (void)bfac; }
static void op_set_3d2d_shown(EidosGpu *g, int w, int h) { (void)g; (void)w; (void)h; }
static void op_set_3d_zero(EidosGpu *g, const unsigned char *zero, int w, int h)
{ (void)g; (void)zero; (void)w; (void)h; }
static int op_read3d(EidosGpu *g, unsigned char *out, int w, int h)
{ (void)g; (void)out; (void)w; (void)h; return -1; }
static int op_read3d_1x(EidosGpu *g, const struct G3dSnapshot *snap, unsigned char *out, int async)
{ (void)g; (void)snap; (void)out; (void)async; return -1; }

static EidosGpuOps g_sdlgpu_ops = {
    "SDL3 GPU",
    op_destroy,
    op_upload, op_present, op_grab, op_upload_panel, op_present_panel,
    op_one_set, op_one_canvas, op_one_bottom, op_matches_3d,
    op_compose_2d, op_ready_2d, op_pixels_2d,
    op_view_keep3d, op_view_upload, op_view_free, op_view_begin, op_view_draw, op_view_draw_fx,
    op_view_read, op_view_end,
    op_has3d, op_capture, op_show_capture, op_render3d, op_set_edges, op_set_3d, op_set_3d2d,
    op_set_3d_from2d, op_set_3d2d_shown, op_set_world_backdrop, op_set_3d_zero, op_read3d,
    op_read3d_1x,
};

/*
 * The backend over `window` (made without SDL_WINDOW_OPENGL).  `driver`
 * picks SDL's GPU driver ("direct3d12", "vulkan", "metal"), NULL for SDL's
 * own choice.  The swapchain presents at once, as the OpenGL backend's swap
 * interval 0: the game's pacer is the timebase.  NULL on any failure.
 */
EidosGpu *eidos_gpu_create_sdl(SDL_Window *window, const char *driver)
{
    SdlGpu *s = (SdlGpu *)calloc(1, sizeof *s);
    SDL_GPUShader *fs_present, *fs_blur, *fs_canvas;
    static const SDL_GPUPresentMode modes[3] = {
        SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_VSYNC
    };
    int i;

    if (s == NULL) {
        return NULL;
    }
    s->window = window;
    s->dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
                                     SDL_GPU_SHADERFORMAT_MSL,
                                 getenv("EIDOS_GPU_DEBUG") != NULL, driver);
    if (s->dev == NULL) {
        fprintf(stderr, "eidos: SDL GPU: no device (%s)\n", SDL_GetError());
        free(s);
        return NULL;
    }
    if (!SDL_ClaimWindowForGPUDevice(s->dev, window)) {
        fprintf(stderr, "eidos: SDL GPU: the window is not usable (%s)\n", SDL_GetError());
        SDL_DestroyGPUDevice(s->dev);
        free(s);
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        if (SDL_WindowSupportsGPUPresentMode(s->dev, window, modes[i])) {
            SDL_SetGPUSwapchainParameters(s->dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          modes[i]);
            break;
        }
    }
    snprintf(s->driver, sizeof s->driver, "%s", SDL_GetGPUDeviceDriver(s->dev));
    s->vs = SHADER(s->dev, SDL_GPU_SHADERSTAGE_VERTEX, present_vs, PRESENT_VS);
    fs_present = SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, present_fs, PRESENT_FS);
    fs_blur = SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, blur_fs, BLUR_FS);
    fs_canvas = SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, canvas_fs, CANVAS_FS);
    if (s->vs != NULL && fs_present != NULL && fs_blur != NULL && fs_canvas != NULL) {
        s->p_present = make_pipeline(s, fs_present, 0);
        s->p_blur = make_pipeline(s, fs_blur, 0);
        s->p_canvas = make_pipeline(s, fs_canvas, 1);
    }
    if (fs_present != NULL) SDL_ReleaseGPUShader(s->dev, fs_present);
    if (fs_blur != NULL) SDL_ReleaseGPUShader(s->dev, fs_blur);
    if (fs_canvas != NULL) SDL_ReleaseGPUShader(s->dev, fs_canvas);
    s->linear = make_sampler(s->dev, SDL_GPU_FILTER_LINEAR);
    s->nearest = make_sampler(s->dev, SDL_GPU_FILTER_NEAREST);
    s->base.ops = &g_sdlgpu_ops;
    if (s->p_present == NULL || s->p_blur == NULL || s->p_canvas == NULL || s->linear == NULL ||
        s->nearest == NULL) {
        fprintf(stderr, "eidos: SDL GPU: the shaders or pipelines failed (%s)\n", SDL_GetError());
        op_destroy(&s->base);
        return NULL;
    }
    fprintf(stderr, "eidos: SDL GPU backend on %s\n", s->driver);
    return &s->base;
}

/* The GPU driver the backend runs on ("direct3d12", "vulkan", "metal"). */
const char *eidos_gpu_sdl_driver(const EidosGpu *g)
{
    return ((const SdlGpu *)g)->driver;
}
