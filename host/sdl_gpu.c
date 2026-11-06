/*
 * host/sdl_gpu.c -- the module's SDL3 GPU imports, answered with this
 * program's SDL3 (the SDL3 GPU backend, engine/platform/plat_sdlgpu*.c,
 * calls SDL's GPU API as every build does; docs/graphics-backends.md).
 *
 * The translations, as host/sdl.c's:
 *
 *   handles     every GPU object (the device, a texture, a pipeline, a
 *               command buffer, a pass, a fence) is a small number in the
 *               module (host_handle_of); command buffers and passes are
 *               dropped when they end
 *   structs     the create-info and binding structs the module passes are
 *               wasm32's layout: the ones with a pointer or an object in
 *               them are read through the W* mirrors below (4-byte
 *               pointers and handles) and rebuilt for SDL; the others are
 *               only 4- and 1-byte fields, the same bytes on both sides
 *   mapping     a transfer buffer's memory is this program's, out of the
 *               module's reach: the module maps a block of its own memory
 *               (made once per transfer buffer), and the block is copied
 *               into the real buffer when an upload buffer is unmapped, or
 *               from it when a download buffer is mapped
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "host.h"

#define H(h) host_handle_ptr(h)
#define OBJ(p) host_handle_of((void *)(p))

static void *M(u32 addr, u32 len) { return addr == 0 ? NULL : host_ptr(addr, len); }

/* ------------------------------------------------------------------ */
/* The module's layouts                                                */
/* ------------------------------------------------------------------ */

typedef struct WShaderCI {
    u32 code_size, code, entrypoint, format, stage;
    u32 num_samplers, num_storage_textures, num_storage_buffers, num_uniform_buffers, props;
} WShaderCI;

typedef struct WVertexInputState {
    u32 vertex_buffer_descriptions, num_vertex_buffers, vertex_attributes, num_vertex_attributes;
} WVertexInputState;

typedef struct WTargetInfo {
    u32 color_target_descriptions, num_color_targets, depth_stencil_format;
    u8 has_depth_stencil_target, p1, p2, p3;
} WTargetInfo;

typedef struct WPipelineCI {
    u32 vertex_shader, fragment_shader;
    WVertexInputState vertex_input_state;
    u32 primitive_type;
    SDL_GPURasterizerState rasterizer_state;
    SDL_GPUMultisampleState multisample_state;
    SDL_GPUDepthStencilState depth_stencil_state;
    WTargetInfo target_info;
    u32 props;
} WPipelineCI;

typedef struct WColorTargetInfo {
    u32 texture, mip_level, layer_or_depth_plane;
    SDL_FColor clear_color;
    u32 load_op, store_op, resolve_texture, resolve_mip_level, resolve_layer;
    u8 cycle, cycle_resolve_texture, p1, p2;
} WColorTargetInfo;

typedef struct WDepthStencilTargetInfo {
    u32 texture;
    float clear_depth;
    u32 load_op, store_op, stencil_load_op, stencil_store_op;
    u8 cycle, clear_stencil, mip_level, layer;
} WDepthStencilTargetInfo;

typedef struct WBlitRegion {
    u32 texture, mip_level, layer_or_depth_plane, x, y, w, h;
} WBlitRegion;

typedef struct WBlitInfo {
    WBlitRegion source, destination;
    u32 load_op;
    SDL_FColor clear_color;
    u32 flip_mode, filter;
    u8 cycle, p1, p2, p3;
} WBlitInfo;

typedef struct WBufferBinding { u32 buffer, offset; } WBufferBinding;
typedef struct WTextureSamplerBinding { u32 texture, sampler; } WTextureSamplerBinding;
typedef struct WTextureTransferInfo { u32 transfer_buffer, offset, pixels_per_row, rows_per_layer; } WTextureTransferInfo;
typedef struct WTransferBufferLocation { u32 transfer_buffer, offset; } WTransferBufferLocation;
typedef struct WTextureRegion { u32 texture, mip_level, layer, x, y, z, w, h, d; } WTextureRegion;
typedef struct WBufferRegion { u32 buffer, offset, size; } WBufferRegion;

/* the module's sizes (wasm32), checked here: a mirror that drifted from
 * SDL's header fails the build, not a frame */
#define SIZE_IS(T, n) typedef char T##_size[(sizeof(T) == (n)) ? 1 : -1]
SIZE_IS(WShaderCI, 40);
SIZE_IS(WVertexInputState, 16);
SIZE_IS(WTargetInfo, 16);
SIZE_IS(WPipelineCI, 132);
SIZE_IS(WColorTargetInfo, 52);
SIZE_IS(WDepthStencilTargetInfo, 28);
SIZE_IS(WBlitInfo, 88);
/* the structs passed as they are: no pointer in them, the same bytes */
SIZE_IS(SDL_GPUTextureCreateInfo, 36);
SIZE_IS(SDL_GPUSamplerCreateInfo, 52);
SIZE_IS(SDL_GPUBufferCreateInfo, 12);
SIZE_IS(SDL_GPUTransferBufferCreateInfo, 12);
SIZE_IS(SDL_GPUVertexBufferDescription, 16);
SIZE_IS(SDL_GPUVertexAttribute, 16);
SIZE_IS(SDL_GPUColorTargetDescription, 36);
SIZE_IS(SDL_GPURasterizerState, 28);
SIZE_IS(SDL_GPUMultisampleState, 12);
SIZE_IS(SDL_GPUDepthStencilState, 44);

static void blit_region(SDL_GPUBlitRegion *o, const WBlitRegion *w)
{
    o->texture = (SDL_GPUTexture *)H(w->texture);
    o->mip_level = w->mip_level;
    o->layer_or_depth_plane = w->layer_or_depth_plane;
    o->x = w->x;
    o->y = w->y;
    o->w = w->w;
    o->h = w->h;
}

/* ------------------------------------------------------------------ */
/* The device and the window                                           */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_CreateGPUDevice(struct w2c_env *e, u32 formats, u32 debug, u32 name)
{
    (void)e;
    return OBJ(SDL_CreateGPUDevice(formats, debug != 0, (const char *)M(name, 1)));
}

void w2c_env_SDL_DestroyGPUDevice(struct w2c_env *e, u32 dev)
{
    (void)e;
    SDL_DestroyGPUDevice((SDL_GPUDevice *)H(dev));
    host_handle_drop(dev);
}

u32 w2c_env_SDL_GetGPUDeviceDriver(struct w2c_env *e, u32 dev)
{
    static u32 at;
    const char *s = SDL_GetGPUDeviceDriver((SDL_GPUDevice *)H(dev));
    size_t n;

    (void)e;
    if (s == NULL) {
        return 0;
    }
    n = strlen(s) + 1;
    if (at == 0) {
        at = host_malloc(64);
    }
    if (at == 0 || n > 64) {
        return 0;
    }
    memcpy(host_ptr(at, (u32)n), s, n);
    return at;
}

u32 w2c_env_SDL_GetGPUShaderFormats(struct w2c_env *e, u32 dev)
{
    (void)e;
    return SDL_GetGPUShaderFormats((SDL_GPUDevice *)H(dev));
}

u32 w2c_env_SDL_ClaimWindowForGPUDevice(struct w2c_env *e, u32 dev, u32 win)
{
    (void)e;
    return SDL_ClaimWindowForGPUDevice((SDL_GPUDevice *)H(dev), (SDL_Window *)H(win));
}

void w2c_env_SDL_ReleaseWindowFromGPUDevice(struct w2c_env *e, u32 dev, u32 win)
{
    (void)e;
    SDL_ReleaseWindowFromGPUDevice((SDL_GPUDevice *)H(dev), (SDL_Window *)H(win));
}

u32 w2c_env_SDL_WindowSupportsGPUPresentMode(struct w2c_env *e, u32 dev, u32 win, u32 mode)
{
    (void)e;
    return SDL_WindowSupportsGPUPresentMode((SDL_GPUDevice *)H(dev), (SDL_Window *)H(win),
                                            (SDL_GPUPresentMode)mode);
}

u32 w2c_env_SDL_SetGPUSwapchainParameters(struct w2c_env *e, u32 dev, u32 win, u32 comp, u32 mode)
{
    (void)e;
    return SDL_SetGPUSwapchainParameters((SDL_GPUDevice *)H(dev), (SDL_Window *)H(win),
                                         (SDL_GPUSwapchainComposition)comp, (SDL_GPUPresentMode)mode);
}

u32 w2c_env_SDL_SetGPUAllowedFramesInFlight(struct w2c_env *e, u32 dev, u32 n)
{
    (void)e;
    return SDL_SetGPUAllowedFramesInFlight((SDL_GPUDevice *)H(dev), n);
}

u32 w2c_env_SDL_GPUTextureSupportsFormat(struct w2c_env *e, u32 dev, u32 fmt, u32 type, u32 usage)
{
    (void)e;
    return SDL_GPUTextureSupportsFormat((SDL_GPUDevice *)H(dev), (SDL_GPUTextureFormat)fmt,
                                        (SDL_GPUTextureType)type, usage);
}

u32 w2c_env_SDL_WaitForGPUIdle(struct w2c_env *e, u32 dev)
{
    (void)e;
    return SDL_WaitForGPUIdle((SDL_GPUDevice *)H(dev));
}

/* ------------------------------------------------------------------ */
/* Objects                                                             */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_CreateGPUShader(struct w2c_env *e, u32 dev, u32 info)
{
    const WShaderCI *w = (const WShaderCI *)M(info, sizeof *w);
    SDL_GPUShaderCreateInfo ci;

    (void)e;
    if (w == NULL) {
        return 0;
    }
    memset(&ci, 0, sizeof ci);
    ci.code_size = w->code_size;
    ci.code = (const Uint8 *)M(w->code, w->code_size);
    ci.entrypoint = (const char *)M(w->entrypoint, 1);
    ci.format = w->format;
    ci.stage = (SDL_GPUShaderStage)w->stage;
    ci.num_samplers = w->num_samplers;
    ci.num_storage_textures = w->num_storage_textures;
    ci.num_storage_buffers = w->num_storage_buffers;
    ci.num_uniform_buffers = w->num_uniform_buffers;
    ci.props = w->props;
    return OBJ(SDL_CreateGPUShader((SDL_GPUDevice *)H(dev), &ci));
}

u32 w2c_env_SDL_CreateGPUGraphicsPipeline(struct w2c_env *e, u32 dev, u32 info)
{
    const WPipelineCI *w = (const WPipelineCI *)M(info, sizeof *w);
    SDL_GPUGraphicsPipelineCreateInfo ci;

    (void)e;
    if (w == NULL) {
        return 0;
    }
    memset(&ci, 0, sizeof ci);
    ci.vertex_shader = (SDL_GPUShader *)H(w->vertex_shader);
    ci.fragment_shader = (SDL_GPUShader *)H(w->fragment_shader);
    ci.vertex_input_state.num_vertex_buffers = w->vertex_input_state.num_vertex_buffers;
    ci.vertex_input_state.vertex_buffer_descriptions = (const SDL_GPUVertexBufferDescription *)
        M(w->vertex_input_state.vertex_buffer_descriptions,
          w->vertex_input_state.num_vertex_buffers * (u32)sizeof(SDL_GPUVertexBufferDescription));
    ci.vertex_input_state.num_vertex_attributes = w->vertex_input_state.num_vertex_attributes;
    ci.vertex_input_state.vertex_attributes = (const SDL_GPUVertexAttribute *)
        M(w->vertex_input_state.vertex_attributes,
          w->vertex_input_state.num_vertex_attributes * (u32)sizeof(SDL_GPUVertexAttribute));
    ci.primitive_type = (SDL_GPUPrimitiveType)w->primitive_type;
    ci.rasterizer_state = w->rasterizer_state;
    ci.multisample_state = w->multisample_state;
    ci.depth_stencil_state = w->depth_stencil_state;
    ci.target_info.num_color_targets = w->target_info.num_color_targets;
    ci.target_info.color_target_descriptions = (const SDL_GPUColorTargetDescription *)
        M(w->target_info.color_target_descriptions,
          w->target_info.num_color_targets * (u32)sizeof(SDL_GPUColorTargetDescription));
    ci.target_info.depth_stencil_format = (SDL_GPUTextureFormat)w->target_info.depth_stencil_format;
    ci.target_info.has_depth_stencil_target = w->target_info.has_depth_stencil_target != 0;
    ci.props = w->props;
    return OBJ(SDL_CreateGPUGraphicsPipeline((SDL_GPUDevice *)H(dev), &ci));
}

u32 w2c_env_SDL_CreateGPUTexture(struct w2c_env *e, u32 dev, u32 info)
{
    (void)e;
    return OBJ(SDL_CreateGPUTexture((SDL_GPUDevice *)H(dev),
                                    (const SDL_GPUTextureCreateInfo *)M(info, sizeof(SDL_GPUTextureCreateInfo))));
}

u32 w2c_env_SDL_CreateGPUSampler(struct w2c_env *e, u32 dev, u32 info)
{
    (void)e;
    return OBJ(SDL_CreateGPUSampler((SDL_GPUDevice *)H(dev),
                                    (const SDL_GPUSamplerCreateInfo *)M(info, sizeof(SDL_GPUSamplerCreateInfo))));
}

u32 w2c_env_SDL_CreateGPUBuffer(struct w2c_env *e, u32 dev, u32 info)
{
    (void)e;
    return OBJ(SDL_CreateGPUBuffer((SDL_GPUDevice *)H(dev),
                                   (const SDL_GPUBufferCreateInfo *)M(info, sizeof(SDL_GPUBufferCreateInfo))));
}

/* A transfer buffer's module block, and what mapping it means. */
#define TB_MAX 256
static struct {
    u32 h;              /* the transfer buffer's handle, 0: free slot */
    u32 usage, size;
    u32 block;          /* the module's block, made on the first map  */
    int cycle;
} g_tb[TB_MAX];

static int tb_find(u32 h)
{
    int i;

    for (i = 0; i < TB_MAX; i++) {
        if (g_tb[i].h == h && h != 0) {
            return i;
        }
    }
    return -1;
}

u32 w2c_env_SDL_CreateGPUTransferBuffer(struct w2c_env *e, u32 dev, u32 info)
{
    const SDL_GPUTransferBufferCreateInfo *ci =
        (const SDL_GPUTransferBufferCreateInfo *)M(info, sizeof(SDL_GPUTransferBufferCreateInfo));
    u32 h;
    int i;

    (void)e;
    if (ci == NULL) {
        return 0;
    }
    h = OBJ(SDL_CreateGPUTransferBuffer((SDL_GPUDevice *)H(dev), ci));
    if (h == 0) {
        return 0;
    }
    for (i = 0; i < TB_MAX && g_tb[i].h != 0; i++) {
    }
    if (i == TB_MAX) {
        fprintf(stderr, "eidos: out of GPU transfer buffer slots\n");
        abort();
    }
    g_tb[i].h = h;
    g_tb[i].usage = ci->usage;
    g_tb[i].size = ci->size;
    g_tb[i].block = 0;
    return h;
}

u32 w2c_env_SDL_MapGPUTransferBuffer(struct w2c_env *e, u32 dev, u32 tb, u32 cycle)
{
    int i = tb_find(tb);

    (void)e;
    if (i < 0) {
        return 0;
    }
    if (g_tb[i].block == 0) {
        g_tb[i].block = host_malloc(g_tb[i].size);
        if (g_tb[i].block == 0) {
            return 0;
        }
    }
    g_tb[i].cycle = cycle != 0;
    if (g_tb[i].usage == SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD) {
        /* a download's bytes, copied now into the module's block */
        void *src = SDL_MapGPUTransferBuffer((SDL_GPUDevice *)H(dev), (SDL_GPUTransferBuffer *)H(tb),
                                             cycle != 0);

        if (src == NULL) {
            return 0;
        }
        memcpy(host_ptr(g_tb[i].block, g_tb[i].size), src, g_tb[i].size);
        SDL_UnmapGPUTransferBuffer((SDL_GPUDevice *)H(dev), (SDL_GPUTransferBuffer *)H(tb));
    }
    return g_tb[i].block;
}

void w2c_env_SDL_UnmapGPUTransferBuffer(struct w2c_env *e, u32 dev, u32 tb)
{
    int i = tb_find(tb);
    void *dst;

    (void)e;
    if (i < 0 || g_tb[i].block == 0 || g_tb[i].usage != SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD) {
        return;
    }
    /* an upload's bytes, from the module's block into the real buffer */
    dst = SDL_MapGPUTransferBuffer((SDL_GPUDevice *)H(dev), (SDL_GPUTransferBuffer *)H(tb),
                                   g_tb[i].cycle != 0);
    if (dst == NULL) {
        return;
    }
    memcpy(dst, host_ptr(g_tb[i].block, g_tb[i].size), g_tb[i].size);
    SDL_UnmapGPUTransferBuffer((SDL_GPUDevice *)H(dev), (SDL_GPUTransferBuffer *)H(tb));
}

void w2c_env_SDL_ReleaseGPUTransferBuffer(struct w2c_env *e, u32 dev, u32 tb)
{
    int i = tb_find(tb);

    (void)e;
    SDL_ReleaseGPUTransferBuffer((SDL_GPUDevice *)H(dev), (SDL_GPUTransferBuffer *)H(tb));
    if (i >= 0) {
        if (g_tb[i].block != 0) {
            host_free(g_tb[i].block);
        }
        memset(&g_tb[i], 0, sizeof g_tb[i]);
    }
    host_handle_drop(tb);
}

#define RELEASE(Name, Type)                                                   \
    void w2c_env_SDL_ReleaseGPU##Name(struct w2c_env *e, u32 dev, u32 obj)   \
    {                                                                         \
        (void)e;                                                              \
        SDL_ReleaseGPU##Name((SDL_GPUDevice *)H(dev), (Type *)H(obj));        \
        host_handle_drop(obj);                                                \
    }
RELEASE(Texture, SDL_GPUTexture)
RELEASE(Shader, SDL_GPUShader)
RELEASE(GraphicsPipeline, SDL_GPUGraphicsPipeline)
RELEASE(Sampler, SDL_GPUSampler)
RELEASE(Buffer, SDL_GPUBuffer)
RELEASE(Fence, SDL_GPUFence)

/* ------------------------------------------------------------------ */
/* Command buffers and their passes                                    */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_AcquireGPUCommandBuffer(struct w2c_env *e, u32 dev)
{
    (void)e;
    return OBJ(SDL_AcquireGPUCommandBuffer((SDL_GPUDevice *)H(dev)));
}

u32 w2c_env_SDL_SubmitGPUCommandBuffer(struct w2c_env *e, u32 cmd)
{
    u32 rc;

    (void)e;
    rc = SDL_SubmitGPUCommandBuffer((SDL_GPUCommandBuffer *)H(cmd));
    host_handle_drop(cmd);
    return rc;
}

u32 w2c_env_SDL_SubmitGPUCommandBufferAndAcquireFence(struct w2c_env *e, u32 cmd)
{
    u32 f;

    (void)e;
    f = OBJ(SDL_SubmitGPUCommandBufferAndAcquireFence((SDL_GPUCommandBuffer *)H(cmd)));
    host_handle_drop(cmd);
    return f;
}

u32 w2c_env_SDL_CancelGPUCommandBuffer(struct w2c_env *e, u32 cmd)
{
    u32 rc;

    (void)e;
    rc = SDL_CancelGPUCommandBuffer((SDL_GPUCommandBuffer *)H(cmd));
    host_handle_drop(cmd);
    return rc;
}

u32 w2c_env_SDL_WaitForGPUFences(struct w2c_env *e, u32 dev, u32 all, u32 fences, u32 n)
{
    const u32 *w = (const u32 *)M(fences, n * 4u);
    SDL_GPUFence *f[16];
    u32 i;

    (void)e;
    if (w == NULL || n > 16) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        f[i] = (SDL_GPUFence *)H(w[i]);
    }
    return SDL_WaitForGPUFences((SDL_GPUDevice *)H(dev), all != 0, f, n);
}

u32 w2c_env_SDL_WaitAndAcquireGPUSwapchainTexture(struct w2c_env *e, u32 cmd, u32 win, u32 tex,
                                                   u32 w, u32 h)
{
    SDL_GPUTexture *t = NULL;
    Uint32 tw = 0, th = 0;
    u32 rc;

    (void)e;
    rc = SDL_WaitAndAcquireGPUSwapchainTexture((SDL_GPUCommandBuffer *)H(cmd), (SDL_Window *)H(win),
                                               &t, &tw, &th);
    if (tex != 0) {
        /* the swapchain's few textures come back again and again: their
         * handles stay */
        u32 ht = t != NULL ? OBJ(t) : 0;

        memcpy(M(tex, 4), &ht, 4);
    }
    if (w != 0) memcpy(M(w, 4), &tw, 4);
    if (h != 0) memcpy(M(h, 4), &th, 4);
    return rc;
}

u32 w2c_env_SDL_BeginGPUCopyPass(struct w2c_env *e, u32 cmd)
{
    (void)e;
    return OBJ(SDL_BeginGPUCopyPass((SDL_GPUCommandBuffer *)H(cmd)));
}

void w2c_env_SDL_EndGPUCopyPass(struct w2c_env *e, u32 cp)
{
    (void)e;
    SDL_EndGPUCopyPass((SDL_GPUCopyPass *)H(cp));
    host_handle_drop(cp);
}

static void tex_transfer(SDL_GPUTextureTransferInfo *o, const WTextureTransferInfo *w)
{
    o->transfer_buffer = (SDL_GPUTransferBuffer *)H(w->transfer_buffer);
    o->offset = w->offset;
    o->pixels_per_row = w->pixels_per_row;
    o->rows_per_layer = w->rows_per_layer;
}

static void tex_region(SDL_GPUTextureRegion *o, const WTextureRegion *w)
{
    o->texture = (SDL_GPUTexture *)H(w->texture);
    o->mip_level = w->mip_level;
    o->layer = w->layer;
    o->x = w->x;
    o->y = w->y;
    o->z = w->z;
    o->w = w->w;
    o->h = w->h;
    o->d = w->d;
}

void w2c_env_SDL_UploadToGPUTexture(struct w2c_env *e, u32 cp, u32 src, u32 dst, u32 cycle)
{
    const WTextureTransferInfo *ws = (const WTextureTransferInfo *)M(src, sizeof *ws);
    const WTextureRegion *wd = (const WTextureRegion *)M(dst, sizeof *wd);
    SDL_GPUTextureTransferInfo s;
    SDL_GPUTextureRegion d;

    (void)e;
    if (ws == NULL || wd == NULL) {
        return;
    }
    tex_transfer(&s, ws);
    tex_region(&d, wd);
    SDL_UploadToGPUTexture((SDL_GPUCopyPass *)H(cp), &s, &d, cycle != 0);
}

void w2c_env_SDL_DownloadFromGPUTexture(struct w2c_env *e, u32 cp, u32 src, u32 dst)
{
    const WTextureRegion *ws = (const WTextureRegion *)M(src, sizeof *ws);
    const WTextureTransferInfo *wd = (const WTextureTransferInfo *)M(dst, sizeof *wd);
    SDL_GPUTextureRegion s;
    SDL_GPUTextureTransferInfo d;

    (void)e;
    if (ws == NULL || wd == NULL) {
        return;
    }
    tex_region(&s, ws);
    tex_transfer(&d, wd);
    SDL_DownloadFromGPUTexture((SDL_GPUCopyPass *)H(cp), &s, &d);
}

void w2c_env_SDL_UploadToGPUBuffer(struct w2c_env *e, u32 cp, u32 src, u32 dst, u32 cycle)
{
    const WTransferBufferLocation *ws = (const WTransferBufferLocation *)M(src, sizeof *ws);
    const WBufferRegion *wd = (const WBufferRegion *)M(dst, sizeof *wd);
    SDL_GPUTransferBufferLocation s;
    SDL_GPUBufferRegion d;

    (void)e;
    if (ws == NULL || wd == NULL) {
        return;
    }
    s.transfer_buffer = (SDL_GPUTransferBuffer *)H(ws->transfer_buffer);
    s.offset = ws->offset;
    d.buffer = (SDL_GPUBuffer *)H(wd->buffer);
    d.offset = wd->offset;
    d.size = wd->size;
    SDL_UploadToGPUBuffer((SDL_GPUCopyPass *)H(cp), &s, &d, cycle != 0);
}

u32 w2c_env_SDL_BeginGPURenderPass(struct w2c_env *e, u32 cmd, u32 targets, u32 n, u32 depth)
{
    const WColorTargetInfo *wt = (const WColorTargetInfo *)M(targets, n * (u32)sizeof *wt);
    const WDepthStencilTargetInfo *wd =
        depth != 0 ? (const WDepthStencilTargetInfo *)M(depth, sizeof *wd) : NULL;
    SDL_GPUColorTargetInfo ct[8];
    SDL_GPUDepthStencilTargetInfo dt;
    u32 i;

    (void)e;
    if ((n > 0 && wt == NULL) || n > 8) {
        return 0;
    }
    memset(ct, 0, sizeof ct);
    for (i = 0; i < n; i++) {
        ct[i].texture = (SDL_GPUTexture *)H(wt[i].texture);
        ct[i].mip_level = wt[i].mip_level;
        ct[i].layer_or_depth_plane = wt[i].layer_or_depth_plane;
        ct[i].clear_color = wt[i].clear_color;
        ct[i].load_op = (SDL_GPULoadOp)wt[i].load_op;
        ct[i].store_op = (SDL_GPUStoreOp)wt[i].store_op;
        ct[i].resolve_texture = (SDL_GPUTexture *)H(wt[i].resolve_texture);
        ct[i].resolve_mip_level = wt[i].resolve_mip_level;
        ct[i].resolve_layer = wt[i].resolve_layer;
        ct[i].cycle = wt[i].cycle != 0;
        ct[i].cycle_resolve_texture = wt[i].cycle_resolve_texture != 0;
    }
    if (wd != NULL) {
        memset(&dt, 0, sizeof dt);
        dt.texture = (SDL_GPUTexture *)H(wd->texture);
        dt.clear_depth = wd->clear_depth;
        dt.load_op = (SDL_GPULoadOp)wd->load_op;
        dt.store_op = (SDL_GPUStoreOp)wd->store_op;
        dt.stencil_load_op = (SDL_GPULoadOp)wd->stencil_load_op;
        dt.stencil_store_op = (SDL_GPUStoreOp)wd->stencil_store_op;
        dt.cycle = wd->cycle != 0;
        dt.clear_stencil = wd->clear_stencil;
        dt.mip_level = wd->mip_level;
        dt.layer = wd->layer;
    }
    return OBJ(SDL_BeginGPURenderPass((SDL_GPUCommandBuffer *)H(cmd), ct, n, wd != NULL ? &dt : NULL));
}

void w2c_env_SDL_EndGPURenderPass(struct w2c_env *e, u32 pass)
{
    (void)e;
    SDL_EndGPURenderPass((SDL_GPURenderPass *)H(pass));
    host_handle_drop(pass);
}

void w2c_env_SDL_BindGPUGraphicsPipeline(struct w2c_env *e, u32 pass, u32 p)
{
    (void)e;
    SDL_BindGPUGraphicsPipeline((SDL_GPURenderPass *)H(pass), (SDL_GPUGraphicsPipeline *)H(p));
}

void w2c_env_SDL_BindGPUFragmentSamplers(struct w2c_env *e, u32 pass, u32 first, u32 b, u32 n)
{
    const WTextureSamplerBinding *w = (const WTextureSamplerBinding *)M(b, n * (u32)sizeof *w);
    SDL_GPUTextureSamplerBinding tb[8];
    u32 i;

    (void)e;
    if (w == NULL || n > 8) {
        return;
    }
    for (i = 0; i < n; i++) {
        tb[i].texture = (SDL_GPUTexture *)H(w[i].texture);
        tb[i].sampler = (SDL_GPUSampler *)H(w[i].sampler);
    }
    SDL_BindGPUFragmentSamplers((SDL_GPURenderPass *)H(pass), first, tb, n);
}

void w2c_env_SDL_BindGPUVertexBuffers(struct w2c_env *e, u32 pass, u32 first, u32 b, u32 n)
{
    const WBufferBinding *w = (const WBufferBinding *)M(b, n * (u32)sizeof *w);
    SDL_GPUBufferBinding bb[8];
    u32 i;

    (void)e;
    if (w == NULL || n > 8) {
        return;
    }
    for (i = 0; i < n; i++) {
        bb[i].buffer = (SDL_GPUBuffer *)H(w[i].buffer);
        bb[i].offset = w[i].offset;
    }
    SDL_BindGPUVertexBuffers((SDL_GPURenderPass *)H(pass), first, bb, n);
}

void w2c_env_SDL_BindGPUIndexBuffer(struct w2c_env *e, u32 pass, u32 b, u32 size)
{
    const WBufferBinding *w = (const WBufferBinding *)M(b, sizeof *w);
    SDL_GPUBufferBinding bb;

    (void)e;
    if (w == NULL) {
        return;
    }
    bb.buffer = (SDL_GPUBuffer *)H(w->buffer);
    bb.offset = w->offset;
    SDL_BindGPUIndexBuffer((SDL_GPURenderPass *)H(pass), &bb, (SDL_GPUIndexElementSize)size);
}

void w2c_env_SDL_PushGPUVertexUniformData(struct w2c_env *e, u32 cmd, u32 slot, u32 data, u32 len)
{
    (void)e;
    SDL_PushGPUVertexUniformData((SDL_GPUCommandBuffer *)H(cmd), slot, M(data, len), len);
}

void w2c_env_SDL_PushGPUFragmentUniformData(struct w2c_env *e, u32 cmd, u32 slot, u32 data, u32 len)
{
    (void)e;
    SDL_PushGPUFragmentUniformData((SDL_GPUCommandBuffer *)H(cmd), slot, M(data, len), len);
}

void w2c_env_SDL_DrawGPUPrimitives(struct w2c_env *e, u32 pass, u32 nv, u32 ni, u32 fv, u32 fi)
{
    (void)e;
    SDL_DrawGPUPrimitives((SDL_GPURenderPass *)H(pass), nv, ni, fv, fi);
}

void w2c_env_SDL_DrawGPUIndexedPrimitives(struct w2c_env *e, u32 pass, u32 n, u32 ni, u32 fi, u32 vo,
                                          u32 finst)
{
    (void)e;
    SDL_DrawGPUIndexedPrimitives((SDL_GPURenderPass *)H(pass), n, ni, fi, (Sint32)vo, finst);
}

void w2c_env_SDL_SetGPUStencilReference(struct w2c_env *e, u32 pass, u32 ref)
{
    (void)e;
    SDL_SetGPUStencilReference((SDL_GPURenderPass *)H(pass), (Uint8)ref);
}

/* SDL_FColor by value: wasm32's C ABI passes it as the address of a copy */
void w2c_env_SDL_SetGPUBlendConstants(struct w2c_env *e, u32 pass, u32 color)
{
    SDL_FColor c;

    (void)e;
    if (M(color, sizeof c) == NULL) {
        return;
    }
    memcpy(&c, M(color, sizeof c), sizeof c);
    SDL_SetGPUBlendConstants((SDL_GPURenderPass *)H(pass), c);
}

void w2c_env_SDL_BlitGPUTexture(struct w2c_env *e, u32 cmd, u32 info)
{
    const WBlitInfo *w = (const WBlitInfo *)M(info, sizeof *w);
    SDL_GPUBlitInfo bi;

    (void)e;
    if (w == NULL) {
        return;
    }
    memset(&bi, 0, sizeof bi);
    blit_region(&bi.source, &w->source);
    blit_region(&bi.destination, &w->destination);
    bi.load_op = (SDL_GPULoadOp)w->load_op;
    bi.clear_color = w->clear_color;
    bi.flip_mode = (SDL_FlipMode)w->flip_mode;
    bi.filter = (SDL_GPUFilter)w->filter;
    bi.cycle = w->cycle != 0;
    SDL_BlitGPUTexture((SDL_GPUCommandBuffer *)H(cmd), &bi);
}
