/*
 * engine/platform/plat_sdlgpu.c -- the SDL3 GPU backend (plat_gpu.h,
 * docs/graphics-backends.md): Direct3D 12 on Windows, Metal on Apple,
 * Vulkan elsewhere, through SDL's GPU API.  This file is its presenter,
 * plat_gl.c's on SDL's GPU: the two DS screens with their 3D layer, the
 * one-screen layout (the world, the blur behind a modal, the interface
 * canvas), the display capture and the run-status panel.  Its 3D renderer
 * is plat_sdlgpu3d.c, its 2D compositor plat_sdlgpu2d.c.
 *
 * Every frame is drawn into `scene`, an RGBA8 texture of the window's size,
 * then blitted to the swapchain: every pipeline has one target format
 * whatever the swapchain's is, and a grab reads the scene back.  Pictures
 * from the CPU go up through one transfer buffer each (cycled), flushed in
 * one copy pass at the start of the frame.  Every picture and frame here is
 * top-down (GL's frames were bottom-up), so the shaders sample them as they
 * are.
 *
 * The shaders are the .hlsl files in engine/platform/shaders, compiled by
 * tools/shaders.py into shaders_gen.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_sdlgpu_int.h"
#include "shaders/shaders_gen.h"

/* The sentinel colour the 2D screens are composed with in place of the 3D
 * layer (demo/eidos_boot.c's boot_sentinel; plat_gl.c's `sent`). */
static const float k_sent[3] = { 84.f / 255.f, 168.f / 255.f, 52.f / 255.f };

static SdlGpu *sg(EidosGpu *g) { return (SdlGpu *)g; }
static const SdlGpu *sgc(const EidosGpu *g) { return (const SdlGpu *)g; }

/* ------------------------------------------------------------------ */
/* Shared helpers (plat_sdlgpu_int.h)                                  */
/* ------------------------------------------------------------------ */

SDL_GPUShader *sg_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
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

SDL_GPUTexture *sg_texture(SdlGpu *s, SDL_GPUTextureFormat f, int w, int h, int levels, int target)
{
    SDL_GPUTextureCreateInfo ci;

    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = f;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = (Uint32)(levels > 0 ? levels : 1);
    return SDL_CreateGPUTexture(s->dev, &ci);
}

void sg_ensure_target(SdlGpu *s, SDL_GPUTexture **tex, int *tw, int *th, int w, int h)
{
    if (*tex == NULL || *tw != w || *th != h) {
        if (*tex != NULL) {
            SDL_ReleaseGPUTexture(s->dev, *tex);
        }
        *tex = sg_texture(s, SG_RGBA, w, h, 1, 1);
        *tw = w;
        *th = h;
    }
}

void sg_free_pic(SDL_GPUDevice *dev, SgPic *p)
{
    if (p->tex != NULL) SDL_ReleaseGPUTexture(dev, p->tex);
    if (p->up != NULL) SDL_ReleaseGPUTransferBuffer(dev, p->up);
    memset(p, 0, sizeof *p);
}

/* New pixels for a picture: the texture is made (again) at their size and
 * the pixels wait in its transfer buffer for the next copy pass. */
void sg_pic_set(SdlGpu *s, SgPic *p, const unsigned char *rgba, int w, int h)
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
        p->tex = sg_texture(s, SG_RGBA, w, h, 1, p->target);
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

/* A picture the GPU draws instead (the 2D compositor): its texture at w x h,
 * a colour target, and no upload waiting to overwrite what is drawn. */
int sg_pic_target(SdlGpu *s, SgPic *p, int w, int h)
{
    p->target = 1;
    if (p->tex == NULL || p->w != w || p->h != h) {
        if (p->tex != NULL) {
            SDL_ReleaseGPUTexture(s->dev, p->tex);
        }
        p->tex = sg_texture(s, SG_RGBA, w, h, 1, 1);
        p->w = p->tex != NULL ? w : 0;
        p->h = p->tex != NULL ? h : 0;
    }
    p->dirty = 0;
    return p->tex != NULL ? 0 : -1;
}

void sg_pic_flush(SDL_GPUCopyPass *cp, SgPic *p)
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

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

/* A pipeline drawing a strip of four vertices (the quad) into an RGBA8
 * target.  blend 1 weighs a straight-alpha picture over what is there; 2
 * mixes by the blend constant (a scene change's old picture). */
static SDL_GPUGraphicsPipeline *make_pipeline(SdlGpu *s, SDL_GPUShader *fs, int blend)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;

    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    ct.format = SG_RGBA;
    if (blend == 1) {
        /* straight RGBA (engine/ui/kit.c): weighed by its alpha, so a
         * fading panel does not brighten toward white (plat_gl.c) */
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else if (blend == 2) {
        ct.blend_state.enable_blend = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
        ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
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

static SDL_GPUGraphicsPipeline *g_p_mix;   /* blur.hlsl by the blend constant */

static void op_destroy(EidosGpu *g)
{
    SdlGpu *s = sg(g);
    int i;

    if (s->dev != NULL) {
        SDL_WaitForGPUIdle(s->dev);
        sg2d_destroy(s);
        sg3d_destroy(s);
        for (i = 0; i < 2; i++) {
            sg_free_pic(s->dev, &s->screen[i]);
            if (s->blur[i] != NULL) SDL_ReleaseGPUTexture(s->dev, s->blur[i]);
            if (s->cap_tex[i] != NULL) SDL_ReleaseGPUTexture(s->dev, s->cap_tex[i]);
        }
        for (i = 0; i < 3; i++) {
            sg_free_pic(s->dev, &s->cap_src[i]);
        }
        sg_free_pic(s->dev, &s->canvas);
        sg_free_pic(s->dev, &s->panel);
        sg_free_pic(s->dev, &s->behind);
        sg_free_pic(s->dev, &s->zero);
        if (s->capa != NULL) SDL_ReleaseGPUTexture(s->dev, s->capa);
        if (s->scene != NULL) SDL_ReleaseGPUTexture(s->dev, s->scene);
        if (s->frozen != NULL) SDL_ReleaseGPUTexture(s->dev, s->frozen);
        if (s->p_present != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_present);
        if (s->p_blur != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_blur);
        if (s->p_canvas != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_canvas);
        if (s->p_comp3d != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_comp3d);
        if (s->p_world != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_world);
        if (s->p_capture != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_capture);
        if (g_p_mix != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(s->dev, g_p_mix);
            g_p_mix = NULL;
        }
        if (s->vs != NULL) SDL_ReleaseGPUShader(s->dev, s->vs);
        if (s->linear != NULL) SDL_ReleaseGPUSampler(s->dev, s->linear);
        if (s->nearest != NULL) SDL_ReleaseGPUSampler(s->dev, s->nearest);
        SDL_ReleaseWindowFromGPUDevice(s->dev, s->window);
        SDL_DestroyGPUDevice(s->dev);
    }
    free(s);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

typedef struct Target {
    SDL_GPUTexture *tex;
    int w, h;
} Target;

/* A render pass into `t`, cleared to (r, g, b) when clear is set. */
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

/* The rectangle `r` (target pixels, y down) of an out_w x out_h target in
 * NDC, as present.hlsl's vertex shader takes it. */
static void ndc_rect(SDL_Rect r, int out_w, int out_h, float rect[4])
{
    rect[0] = 2.f * (float)r.x / (float)out_w - 1.f;
    rect[1] = 1.f - 2.f * (float)(r.y + r.h) / (float)out_h;
    rect[2] = 2.f * (float)(r.x + r.w) / (float)out_w - 1.f;
    rect[3] = 1.f - 2.f * (float)r.y / (float)out_h;
}

/* A quad through pipeline `p` with `n` pictures bound, into rectangle `r`. */
static void quad_n(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, SDL_GPUGraphicsPipeline *p,
                   const SDL_GPUTextureSamplerBinding *tb, int n, SDL_Rect r, int out_w, int out_h,
                   const void *fs, Uint32 fs_size)
{
    float rect[4];

    ndc_rect(r, out_w, out_h, rect);
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_BindGPUFragmentSamplers(pass, 0, tb, (Uint32)n);
    SDL_PushGPUVertexUniformData(cmd, 0, rect, sizeof rect);
    if (fs != NULL) {
        SDL_PushGPUFragmentUniformData(cmd, 0, fs, fs_size);
    }
    SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
}

static void quad(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, SDL_GPUGraphicsPipeline *p,
                 SDL_GPUTexture *tex, SDL_GPUSampler *smp, SDL_Rect r, int out_w, int out_h,
                 const void *fs, Uint32 fs_size)
{
    SDL_GPUTextureSamplerBinding tb;

    tb.texture = tex;
    tb.sampler = smp;
    quad_n(cmd, pass, p, &tb, 1, r, out_w, out_h, fs, fs_size);
}

/* A picture with the sharp scaling (present.hlsl). */
static void draw_pic(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                     SDL_GPUTexture *tex, int tw, int th, SDL_Rect r, int out_w, int out_h)
{
    float u[4];

    if (tex == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    u[0] = (float)tw;
    u[1] = (float)th;
    u[2] = (float)r.w;
    u[3] = (float)r.h;
    quad(cmd, pass, s->p_present, tex, s->linear, r, out_w, out_h, u, sizeof u);
}

/* Screen i's 3D frame is in fb[0] and its pictures match (plat_gl.c
 * draw_frame's is3d). */
static int screen_has_3d(const SdlGpu *s, int i)
{
    return i == s->s3d && s->have3d && s->p_comp3d != NULL && s->fb[0].color != NULL &&
           s->behind.tex != NULL && s->behind.w == s->screen[i].w && s->behind.h == s->screen[i].h;
}

/* The two screens into their rectangles over `clear`: each as it is, with
 * its GPU 3D layer (comp3d.hlsl), or as the capture shows it. */
static void draw_screens(SdlGpu *s, SDL_GPUCommandBuffer *cmd, Target t, const SDL_Rect dst[2],
                         float clear)
{
    SDL_GPURenderPass *pass = begin(cmd, t, 1, clear, clear, clear);
    int i;

    for (i = 0; i < 2; i++) {
        const SgPic *p = &s->screen[i];

        if (dst[i].w <= 0 || dst[i].h <= 0 || p->tex == NULL) {
            continue;
        }
        if (i == s->cap_screen && s->cap_valid && s->p_capture != NULL) {
            SDL_GPUTextureSamplerBinding tb[2];
            float u[8];

            memset(u, 0, sizeof u);
            u[2] = s->cap_bfac;
            ((int *)u)[4] = 1;              /* mode 1: a frame, its brightness */
            ((int *)u)[5] = s->cap_bmode;
            tb[0].texture = tb[1].texture = s->cap_tex[s->cap_cur];
            tb[0].sampler = tb[1].sampler = s->linear;
            quad_n(cmd, pass, s->p_capture, tb, 2, dst[i], t.w, t.h, u, sizeof u);
            continue;
        }
        if (screen_has_3d(s, i)) {
            SDL_GPUTextureSamplerBinding tb[4];
            float u[12];
            int *m = (int *)&u[4];

            tb[0].texture = p->tex;
            tb[1].texture = s->behind.tex;
            tb[2].texture = s->fb[0].color;
            tb[3].texture = s->has_zero && s->zero.tex != NULL ? s->zero.tex : s->behind.tex;
            tb[0].sampler = tb[1].sampler = tb[2].sampler = tb[3].sampler = s->linear;
            u[0] = (float)p->w;
            u[1] = (float)p->h;
            u[2] = (float)dst[i].w;
            u[3] = (float)dst[i].h;
            m[0] = s->bmode;
            m[1] = s->dilate;
            m[2] = s->has_zero;
            m[3] = 0;
            u[8] = s->bfac;
            u[9] = k_sent[0];
            u[10] = k_sent[1];
            u[11] = k_sent[2];
            quad_n(cmd, pass, s->p_comp3d, tb, 4, dst[i], t.w, t.h, u, sizeof u);
            continue;
        }
        draw_pic(s, cmd, pass, p->tex, p->w, p->h, dst[i], t.w, t.h);
    }
    if (!s->on && s->prect.w > 0 && s->panel.tex != NULL) {
        draw_pic(s, cmd, pass, s->panel.tex, s->panel.w, s->panel.h, s->prect, t.w, t.h);
    }
    SDL_EndGPURenderPass(pass);
}

/* One blur pass (blur.hlsl): `src` through the blur into `t`'s rectangle.
 * dir 0 copies (with the tint). */
static void blur_pass(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *src, Target t,
                      SDL_Rect r, float dx, float dy, float tint, int clear)
{
    SDL_GPURenderPass *pass = begin(cmd, t, clear, 0, 0, 0);
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

/* Last 3D frame matches screen and size (plat_gl.c eidos_gl_3d_matches). */
static int matches_3d(const SdlGpu *s, int screen, int out_w, int out_h)
{
    long fw, fh, err;

    if (s->s3d != screen || s->fb[0].color == NULL) {
        return 0;
    }
    fw = s->fb[0].w;
    fh = s->fb[0].h;
    err = fw * out_h - fh * out_w;
    if (err < 0) {
        err = -err;
    }
    return fw > 0 && fh > 0 && fw <= out_w && fh <= out_h && err <= (out_w > out_h ? out_w : out_h);
}

/* The one-screen world (plat_gl.c one_world): into the window's top wh
 * rows, or into the blur target when a modal blurs it. */
static void draw_world(SdlGpu *s, SDL_GPUCommandBuffer *cmd, Target t)
{
    const int sc = s->which;
    int wh = t.h - (s->bottom > 0 && s->bottom < t.h / 2 ? s->bottom : 0);
    int use_w = s->p_world != NULL && s->screen[sc].tex != NULL && sc != s->cap_screen &&
                s->behind.tex != NULL && s->behind.w == s->screen[sc].w &&
                s->behind.h == s->screen[sc].h &&
                matches_3d(s, sc, s->crop ? s->ds.w : t.w, s->crop ? s->ds.h : wh);
    int blur = s->blur_amount > 0 && s->p_blur != NULL;
    int frozen = s->frozen_ok && s->frozen != NULL && s->frozen_w == t.w && s->frozen_h == wh;
    int dark = s->bmode != 0 && s->bfac > 0.f;
    SDL_Rect world;
    Target dst, b0;

    world.x = 0;
    world.y = 0;
    world.w = t.w;
    world.h = wh;
    if ((s->hold || (!use_w && s->no_fade)) && !blur && frozen) {
        /* an app opening or closing over the world: its last picture stands */
        blur_pass(s, cmd, s->frozen, t, world, 0.f, 0.f, 1.f, 1);
        return;
    }
    if (s->hold && !frozen) {
        /* an app over the field with no picture of it kept: a plain dark
         * ground, not the app's own DS screen behind its modal */
        SDL_GPURenderPass *pass = begin(cmd, t, 1, 0.04f, 0.04f, 0.05f);

        SDL_EndGPURenderPass(pass);
        return;
    }
    if (blur) {
        sg_ensure_target(s, &s->blur[0], &s->blur_w, &s->blur_h, t.w, wh);
        sg_ensure_target(s, &s->blur[1], &s->blur1_w, &s->blur1_h, t.w, wh);
        if (s->blur[0] == NULL || s->blur[1] == NULL) {
            blur = 0;
        }
    }
    b0.tex = s->blur[0];
    b0.w = t.w;
    b0.h = wh;
    dst = blur ? b0 : t;
    if (blur && (s->hold || !use_w) && frozen) {
        /* a modal over an app that closed the world: the kept picture */
        blur_pass(s, cmd, s->frozen, b0, world, 0.f, 0.f, 1.f, 1);
    } else {
        if (use_w && !blur && !s->keep && !s->hold && !s->no_fade && !s->spare && !s->under) {
            /* a live world that is not the field: the kept picture is out
             * of date from here */
            s->frozen_ok = 0;
        }
        if (use_w) {
            SDL_GPURenderPass *pass = begin(cmd, dst, 1, 0, 0, 0);
            SDL_GPUTextureSamplerBinding tb[5];
            float u[24];
            int *m = (int *)&u[8], *m2 = (int *)&u[12];

            memset(u, 0, sizeof u);
            tb[0].texture = s->screen[sc].tex;
            tb[1].texture = s->behind.tex;
            tb[2].texture = s->fb[0].color;
            tb[3].texture = s->has_zero && s->zero.tex != NULL ? s->zero.tex : s->behind.tex;
            /* the kept picture beyond the DS screen (a cut-in: the VS) */
            tb[4].texture = frozen ? s->frozen : s->behind.tex;
            tb[0].sampler = tb[1].sampler = tb[2].sampler = tb[3].sampler = tb[4].sampler = s->linear;
            m2[1] = s->under && frozen;
            u[0] = (float)s->ds.x;
            u[1] = (float)s->ds.y;
            u[2] = (float)s->ds.w;
            u[3] = (float)s->ds.h;
            u[4] = (float)s->screen[sc].w;
            u[5] = (float)s->screen[sc].h;
            u[6] = (float)t.w;
            u[7] = (float)wh;
            m[0] = s->no_fade ? 0 : s->bmode;
            m[1] = s->dilate;
            m[2] = s->has_zero;
            m[3] = s->hide2d;
            m2[0] = s->crop;
            if (!s->no_fade) {
                u[16] = s->bd[0];
                u[17] = s->bd[1];
                u[18] = s->bd[2];
            }
            u[19] = s->bfac;
            u[20] = k_sent[0];
            u[21] = k_sent[1];
            u[22] = k_sent[2];
            quad_n(cmd, pass, s->p_world, tb, 5, world, dst.w, dst.h, u, sizeof u);
            if (s->mix > 0 && frozen && !blur && g_p_mix != NULL) {
                /* a scene change blends the old picture out over the new
                 * world: the kept wide picture at the game's weight */
                float a = s->mix >= 16 ? 1.f : (float)s->mix / 16.f;
                SDL_FColor k;
                float bu[8];

                k.r = k.g = k.b = k.a = a;
                memset(bu, 0, sizeof bu);
                bu[0] = (float)t.w;
                bu[1] = (float)wh;
                bu[4] = 1.f;
                SDL_SetGPUBlendConstants(pass, k);
                quad(cmd, pass, g_p_mix, s->frozen, s->linear, world, dst.w, dst.h, bu, sizeof bu);
            }
            SDL_EndGPURenderPass(pass);
            if (!(s->mix > 0 && frozen && !blur) && !blur && s->keep && !s->under && (!dark || s->no_fade)) {
                /* keep this live world for a later modal */
                SDL_GPUBlitInfo bi;

                sg_ensure_target(s, &s->frozen, &s->frozen_w, &s->frozen_h, t.w, wh);
                if (s->frozen != NULL) {
                    memset(&bi, 0, sizeof bi);
                    bi.source.texture = t.tex;
                    bi.source.w = (Uint32)t.w;
                    bi.source.h = (Uint32)wh;
                    bi.destination.texture = s->frozen;
                    bi.destination.w = (Uint32)t.w;
                    bi.destination.h = (Uint32)wh;
                    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
                    bi.filter = SDL_GPU_FILTER_NEAREST;
                    SDL_BlitGPUTexture(cmd, &bi);
                    s->frozen_ok = 1;
                }
            }
        } else if (s->keep && frozen && !blur) {
            /* the field without window-size 3D for a while (a door): the
             * last wide picture, faded the way the game fades the screen */
            int bm = sc == s->cap_screen ? s->cap_bmode : s->bmode;
            float bf = sc == s->cap_screen ? s->cap_bfac : s->bfac;
            float tint = bm == 2 ? 1.f - bf : 1.f;

            if (1.f - (float)s->dark / 16.f < tint) {
                tint = 1.f - (float)s->dark / 16.f;
            }
            blur_pass(s, cmd, s->frozen, t, world, 0.f, 0.f, tint, 1);
        } else {
            /* no window-size 3D this frame: the screen as it is, at the DS
             * rectangle, around it the backdrop as the brightness has it */
            SDL_Rect d[2];
            float clear = s->no_fade ? 0.f : (s->bd[0] + s->bd[1] + s->bd[2]) / 3.f;

            memset(d, 0, sizeof d);
            d[sc] = s->ds;
            draw_screens(s, cmd, dst, d, clear);
        }
    }
    if (blur) {
        /* the strength eases in and out: the taps spread and the tint
         * darkens with it (plat_gl.c one_blur) */
        float a = (float)s->blur_amount / 16.f;
        float step = (float)wh / 180.f * a;
        Target b1;

        b1.tex = s->blur[1];
        b1.w = t.w;
        b1.h = wh;
        blur_pass(s, cmd, s->blur[0], b1, world, step, 0.f, 1.f, 1);
        {
            SDL_GPURenderPass *pass = begin(cmd, t, 1, 0, 0, 0);

            SDL_EndGPURenderPass(pass);
        }
        blur_pass(s, cmd, s->blur[1], t, world, 0.f, step, 1.f - 0.55f * a, 0);
    } else if (wh < t.h) {
        /* the strip under the world, which the hint bar covers, stays
         * black: the world's passes cleared the whole target already */
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
        sg_pic_flush(cp, &s->screen[i]);
    }
    sg_pic_flush(cp, &s->canvas);
    sg_pic_flush(cp, &s->panel);
    sg_pic_flush(cp, &s->behind);
    sg_pic_flush(cp, &s->zero);
    SDL_EndGPUCopyPass(cp);
    sg_ensure_target(s, &s->scene, &s->scene_w, &s->scene_h, out_w, out_h);
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
/* The pictures and the layout's state                                 */
/* ------------------------------------------------------------------ */

static void op_upload(EidosGpu *g, int i, const unsigned char *rgba, int w, int h)
{
    if (i >= 0 && i < 2) {
        sg_pic_set(sg(g), &sg(g)->screen[i], rgba, w, h);
    }
}

static void op_upload_panel(EidosGpu *g, const unsigned char *rgba, int w, int h)
{
    sg_pic_set(sg(g), &sg(g)->panel, rgba, w, h);
}

static void op_one_set(EidosGpu *g, int on, int world, int screen, const SDL_Rect *ds, int hide_2d,
                       int blur, int no_fade)
{
    SdlGpu *s = sg(g);

    s->on = on != 0;
    s->world = on && world && ds != NULL && screen >= 0 && screen < 2;
    s->which = screen >= 0 && screen < 2 ? screen : 0;
    s->hide2d = hide_2d != 0;
    s->crop = (no_fade & (1 << 18)) != 0;
    s->blur_amount = blur < 0 ? 0 : blur > 16 ? 16 : blur;
    s->no_fade = (no_fade & 1) != 0;
    s->hold = (no_fade & 2) != 0;
    s->keep = (no_fade & 4) != 0;
    s->spare = (no_fade & (1 << 19)) != 0;
    s->under = (no_fade & (1 << 20)) != 0;
    s->dark = (no_fade >> 8) & 31;
    s->mix = (no_fade >> 13) & 31;
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
    sg_pic_set(s, &s->canvas, rgba, w, h);
}

static void op_one_bottom(EidosGpu *g, int px) { sg(g)->bottom = px > 0 ? px : 0; }

static int op_matches_3d(const EidosGpu *g, int screen, int out_w, int out_h)
{
    return matches_3d(sgc(g), screen, out_w, out_h);
}

static void op_set_world_backdrop(EidosGpu *g, unsigned rgb)
{
    SdlGpu *s = sg(g);

    s->bd[0] = (float)((rgb >> 16) & 0xFFu) / 255.f;
    s->bd[1] = (float)((rgb >> 8) & 0xFFu) / 255.f;
    s->bd[2] = (float)(rgb & 0xFFu) / 255.f;
}

static void op_set_3d_zero(EidosGpu *g, const unsigned char *zero, int w, int h)
{
    SdlGpu *s = sg(g);

    s->has_zero = 0;
    if (zero == NULL || w != 256 || h != 192) {
        return;
    }
    sg_pic_set(s, &s->zero, zero, w, h);
    s->has_zero = s->zero.tex != NULL;
}

static void op_set_3d(EidosGpu *g, int screen, const unsigned char *behind, int w, int h, int bmode,
                      float bfac, int dilate)
{
    SdlGpu *s = sg(g);

    s->s3d2 = -1;
    s->s3d_from2d = 0;
    if (screen < 0 || behind == NULL || w <= 0 || h <= 0) {
        s->s3d = -1;
        return;
    }
    sg_pic_set(s, &s->behind, behind, w, h);
    s->s3d = screen;
    s->bmode = bmode;
    s->bfac = bfac;
    s->dilate = dilate;
}

/* The 2D compositor's composites (plat_gl.c eidos_gl_set_3d2d and the two
 * after it).  3d2d: the 2D shader composes `screen` with the GPU's 3D frame
 * in it, so the presenter shows that screen as a picture.  from2d: the
 * one-screen world's screens (the sentinel one, the 3D off, the black 3D)
 * come from the shader, and s3d's composite uses them as the CPU's. */
static void op_set_3d2d(EidosGpu *g, int screen)
{
    SdlGpu *s = sg(g);

    s->s3d_from2d = 0;
    s->s3d = -1;
    s->s3d2 = screen == 0 || screen == 1 ? screen : -1;
}
static void op_set_3d_from2d(EidosGpu *g, int screen, int bmode, float bfac)
{
    SdlGpu *s = sg(g);

    s->s3d2 = -1;
    s->s3d = screen;
    s->s3d_from2d = 1;
    s->has_zero = 0;
    s->bmode = bmode;
    s->bfac = bfac;
    s->dilate = 0;
}
static void op_set_3d2d_shown(EidosGpu *g, int w, int h)
{
    sg(g)->s3d2_w = w > 0 ? w : 0;
    sg(g)->s3d2_h = h > 0 ? h : 0;
}

/* ------------------------------------------------------------------ */
/* The 3D and the capture                                              */
/* ------------------------------------------------------------------ */

static int op_has3d(const EidosGpu *g) { return sgc(g)->have3d; }
static void op_set_edges(EidosGpu *g, int mode) { sg(g)->edge_mode = mode < 0 ? 0 : mode > 2 ? 2 : mode; }
static int op_render3d(EidosGpu *g, const struct G3dSnapshot *snap, int w, int h)
{
    return sg3d_render3d(sg(g), snap, w, h);
}
static int op_read3d(EidosGpu *g, unsigned char *out, int w, int h) { return sg3d_read3d(sg(g), out, w, h); }

/* The swapchain: VSYNC with one frame in flight (the least latency the
 * vblank allows), or the first of IMMEDIATE and MAILBOX the window takes. */
static void op_set_vsync(EidosGpu *g, int on)
{
    SdlGpu *s = sg(g);
    static const SDL_GPUPresentMode modes[3] = {
        SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_VSYNC
    };
    int i;

    if (on) {
        SDL_SetGPUSwapchainParameters(s->dev, s->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_VSYNC);
        SDL_SetGPUAllowedFramesInFlight(s->dev, 1);
        return;
    }
    SDL_SetGPUAllowedFramesInFlight(s->dev, 2);
    for (i = 0; i < 3; i++) {
        if (SDL_WindowSupportsGPUPresentMode(s->dev, s->window, modes[i])) {
            SDL_SetGPUSwapchainParameters(s->dev, s->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, modes[i]);
            break;
        }
    }
}
static int op_read3d_1x(EidosGpu *g, const struct G3dSnapshot *snap, unsigned char *out, int async)
{
    return sg3d_read3d_1x(sg(g), snap, out, async);
}

/*
 * One frame of the display capture, kept at the 3D's resolution (plat_gl.c
 * eidos_gl_capture: the rule is there).  Source A, the screen with the
 * GPU's 3D layer, is made at the 3D's size; the capture is A alone, or A
 * mixed with the capture before it (or with `b1x`, the bank's content).
 */
static int op_capture(EidosGpu *g, const unsigned char *a2d, const unsigned char *behind,
                      const struct G3dSnapshot *snap, int scale, int blend, int eva, int evb,
                      const unsigned char *b1x)
{
    SdlGpu *s = sg(g);
    int w = 256 * scale, h = 192 * scale, next, i;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    Target ta, tc;
    SDL_Rect full;

    if (!s->have3d || s->p_comp3d == NULL || s->p_capture == NULL || a2d == NULL ||
        behind == NULL || snap == NULL || scale < 1) {
        return -1;
    }
    if (s->cap_w != w || s->cap_h != h) {
        int aw = 0, ah = 0, bw = 0, bh = 0, cw = 0, ch = 0;

        if (s->capa != NULL) SDL_ReleaseGPUTexture(s->dev, s->capa);
        if (s->cap_tex[0] != NULL) SDL_ReleaseGPUTexture(s->dev, s->cap_tex[0]);
        if (s->cap_tex[1] != NULL) SDL_ReleaseGPUTexture(s->dev, s->cap_tex[1]);
        s->capa = s->cap_tex[0] = s->cap_tex[1] = NULL;
        sg_ensure_target(s, &s->capa, &aw, &ah, w, h);
        sg_ensure_target(s, &s->cap_tex[0], &bw, &bh, w, h);
        sg_ensure_target(s, &s->cap_tex[1], &cw, &ch, w, h);
        s->cap_w = w;
        s->cap_h = h;
        s->cap_valid = 0;
    }
    if (s->capa == NULL || s->cap_tex[0] == NULL || s->cap_tex[1] == NULL) {
        return -1;
    }
    if (blend && !s->cap_valid && b1x == NULL) {
        return -1;
    }
    sg_pic_set(s, &s->cap_src[0], a2d, 256, 192);
    sg_pic_set(s, &s->cap_src[1], behind, 256, 192);
    if (b1x != NULL) {
        sg_pic_set(s, &s->cap_src[2], b1x, 256, 192);
    }
    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (cmd == NULL) {
        return -1;
    }
    cp = SDL_BeginGPUCopyPass(cmd);
    for (i = 0; i < 3; i++) {
        sg_pic_flush(cp, &s->cap_src[i]);
    }
    SDL_EndGPUCopyPass(cp);
    if (sg3d_render(s, cmd, 0, snap, w, h) != 0) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return -1;
    }
    full.x = full.y = 0;
    full.w = w;
    full.h = h;
    ta.tex = s->capa;
    ta.w = w;
    ta.h = h;
    {
        /* source A: the screen with the GPU's 3D layer (comp3d.hlsl) */
        SDL_GPURenderPass *pass = begin(cmd, ta, 1, 0, 0, 0);
        SDL_GPUTextureSamplerBinding tb[4];
        float u[12];
        int *m = (int *)&u[4];

        memset(u, 0, sizeof u);
        tb[0].texture = s->cap_src[0].tex;
        tb[1].texture = s->cap_src[1].tex;
        tb[2].texture = s->fb[0].color;
        tb[3].texture = s->cap_src[1].tex;
        tb[0].sampler = tb[1].sampler = tb[2].sampler = tb[3].sampler = s->linear;
        u[0] = 256.f;
        u[1] = 192.f;
        u[2] = (float)w;
        u[3] = (float)h;
        m[1] = 1;               /* dilate */
        u[9] = k_sent[0];
        u[10] = k_sent[1];
        u[11] = k_sent[2];
        quad_n(cmd, pass, s->p_comp3d, tb, 4, full, w, h, u, sizeof u);
        SDL_EndGPURenderPass(pass);
    }
    next = s->cap_cur ^ 1;
    tc.tex = s->cap_tex[next];
    tc.w = w;
    tc.h = h;
    {
        /* the capture: A alone, or mixed with the capture before it */
        SDL_GPURenderPass *pass = begin(cmd, tc, 1, 0, 0, 0);
        SDL_GPUTextureSamplerBinding tb[2];
        float u[8];

        memset(u, 0, sizeof u);
        u[0] = blend ? (float)eva / 16.f : 1.f;
        u[1] = blend ? (float)evb / 16.f : 0.f;
        tb[0].texture = s->capa;
        tb[1].texture = b1x != NULL ? s->cap_src[2].tex : s->cap_tex[s->cap_cur];
        tb[0].sampler = tb[1].sampler = s->linear;
        quad_n(cmd, pass, s->p_capture, tb, 2, full, w, h, u, sizeof u);
        SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    s->cap_cur = next;
    s->cap_valid = 1;
    return 0;
}

static void op_show_capture(EidosGpu *g, int screen, int bmode, float bfac)
{
    SdlGpu *s = sg(g);

    s->cap_screen = (screen >= 0 && s->cap_valid) ? screen : -1;
    s->cap_bmode = bmode;
    s->cap_bfac = bfac;
}

/* ------------------------------------------------------------------ */
/* The 2D compositor (plat_sdlgpu2d.c)                                 */
/* ------------------------------------------------------------------ */

static int op_compose_2d(EidosGpu *g, const struct G2dGpuFrame *f, unsigned char *readback,
                         int to_screen)
{
    return sg2d_compose(sg(g), f, readback, to_screen);
}
static int op_ready_2d(EidosGpu *g) { return sg2d_ready(sg(g)); }
static int op_pixels_2d(EidosGpu *g, int screen, unsigned char *out)
{
    return sg2d_pixels(sg(g), screen, out);
}

/* ------------------------------------------------------------------ */
/* Not in this backend: a tool's window (the spot editor) draws with    */
/* the OpenGL backend                                                   */
/* ------------------------------------------------------------------ */

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

static const EidosGpuOps g_sdlgpu_ops = {
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
    op_set_vsync,
};

/*
 * The backend over `window` (made without SDL_WINDOW_OPENGL).  `driver`
 * picks SDL's GPU driver ("direct3d12", "vulkan", "metal"), NULL for SDL's
 * own choice.  The swapchain presents at once, as the OpenGL backend's swap
 * interval 0: the game's pacer is the timebase.  NULL on any failure; a 3D
 * renderer that does not come up leaves the presenter (the platform layer
 * then draws the 3D on the CPU).
 */
EidosGpu *eidos_gpu_create_sdl(SDL_Window *window, const char *driver)
{
    SdlGpu *s = (SdlGpu *)calloc(1, sizeof *s);
    SDL_GPUShader *fs_present, *fs_blur, *fs_canvas, *fs_comp3d, *fs_world, *fs_capture;
    static const SDL_GPUPresentMode modes[3] = {
        SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_VSYNC
    };
    int i;

    if (s == NULL) {
        return NULL;
    }
    s->window = window;
    s->s3d = -1;
    s->s3d2 = -1;
    s->cap_screen = -1;
    /* the 2D compositor draws into these (plat_sdlgpu2d.c) */
    s->screen[0].target = s->screen[1].target = 1;
    s->behind.target = s->zero.target = 1;
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
            SDL_SetGPUSwapchainParameters(s->dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, modes[i]);
            break;
        }
    }
    snprintf(s->driver, sizeof s->driver, "%s", SDL_GetGPUDeviceDriver(s->dev));
    s->vs = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_VERTEX, present_vs, PRESENT_VS);
    fs_present = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, present_fs, PRESENT_FS);
    fs_blur = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, blur_fs, BLUR_FS);
    fs_canvas = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, canvas_fs, CANVAS_FS);
    fs_comp3d = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, comp3d_fs, COMP3D_FS);
    fs_world = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, world_fs, WORLD_FS);
    fs_capture = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, capture_fs, CAPTURE_FS);
    if (s->vs != NULL && fs_present != NULL && fs_blur != NULL && fs_canvas != NULL) {
        s->p_present = make_pipeline(s, fs_present, 0);
        s->p_blur = make_pipeline(s, fs_blur, 0);
        s->p_canvas = make_pipeline(s, fs_canvas, 1);
        g_p_mix = make_pipeline(s, fs_blur, 2);
    }
    if (s->vs != NULL && fs_comp3d != NULL && fs_world != NULL && fs_capture != NULL) {
        s->p_comp3d = make_pipeline(s, fs_comp3d, 0);
        s->p_world = make_pipeline(s, fs_world, 0);
        s->p_capture = make_pipeline(s, fs_capture, 0);
    }
    if (fs_present != NULL) SDL_ReleaseGPUShader(s->dev, fs_present);
    if (fs_blur != NULL) SDL_ReleaseGPUShader(s->dev, fs_blur);
    if (fs_canvas != NULL) SDL_ReleaseGPUShader(s->dev, fs_canvas);
    if (fs_comp3d != NULL) SDL_ReleaseGPUShader(s->dev, fs_comp3d);
    if (fs_world != NULL) SDL_ReleaseGPUShader(s->dev, fs_world);
    if (fs_capture != NULL) SDL_ReleaseGPUShader(s->dev, fs_capture);
    s->linear = make_sampler(s->dev, SDL_GPU_FILTER_LINEAR);
    s->nearest = make_sampler(s->dev, SDL_GPU_FILTER_NEAREST);
    s->base.ops = &g_sdlgpu_ops;
    if (s->p_present == NULL || s->p_blur == NULL || s->p_canvas == NULL || s->linear == NULL ||
        s->nearest == NULL) {
        fprintf(stderr, "eidos: SDL GPU: the shaders or pipelines failed (%s)\n", SDL_GetError());
        op_destroy(&s->base);
        return NULL;
    }
    if (getenv("EIDOS_GPU_NO3D") == NULL && s->p_comp3d != NULL && s->p_world != NULL &&
        sg3d_init(s) != 0) {
        fprintf(stderr, "eidos: SDL GPU: no 3D renderer (%s); the CPU draws the 3D\n",
                SDL_GetError());
        s->have3d = 0;
    }
    fprintf(stderr, "eidos: SDL GPU backend on %s%s\n", s->driver, s->have3d ? ", 3D on the GPU" : "");
    return &s->base;
}

/* The GPU driver the backend runs on ("direct3d12", "vulkan", "metal"). */
const char *eidos_gpu_sdl_driver(const EidosGpu *g)
{
    return ((const SdlGpu *)g)->driver;
}
