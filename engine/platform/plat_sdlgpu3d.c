/*
 * engine/platform/plat_sdlgpu3d.c -- the DS 3D engine on the SDL3 GPU
 * backend: plat_gl3d.c's renderer on SDL's GPU API, rule for rule (its
 * file comment has the rules: the polygons, edge marking, fog, the battle
 * backdrop).
 *
 * What differs from the OpenGL renderer, and why:
 *
 *   - the polygon ID target is R8 UNORM (the ID over 255), not R8UI: an
 *     integer target's clear colour is not portable across SDL's drivers,
 *     and a UNORM one multisamples and samples everywhere;
 *   - the depth the edge and fog passes read is a copy the shaders write
 *     into an R32 float target beside the depth buffer (the target's write
 *     mask follows the depth write): a depth-stencil texture cannot be
 *     sampled on every driver (Metal has no D24);
 *   - clip z is taken from GL's -w..w to 0..w in the vertex shaders, so the
 *     depth values are GL's, and the w-buffer's w comes from a perspective-
 *     corrected varying rather than the fragment position's w, whose
 *     meaning differs between the drivers;
 *   - the frames are top-down (row 0 at the top), so a read needs no flip
 *     and the presenter's shaders sample them as they are;
 *   - anti-aliasing (DISP3DCNT bit 4) is the 2x render the OpenGL renderer
 *     uses where integer targets cannot multisample (Apple), everywhere;
 *   - each OpenGL state the draws switch between (the depth test and write,
 *     the stencil rule, the ID write, the culling, the blending) is one
 *     pipeline, made the first time it is needed and kept (pipe_get).
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_sdlgpu_int.h"
#include "shaders/shaders_gen.h"
#ifdef far
#undef far /* the windows headers define it; BmapDraw has a member of that name */
#endif
#include "bmap.h"
#include "bmap_place.h"
#include "g3d.h"
#include "hw3d.h"
#include "prof.h"

#define FLOATS_PER_VERTEX 9u
#define STENCIL_GAME 0x80u
#define STENCIL_IDS 0x7Fu
#define ATTR_DEFAULT (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))
#define AA_MAX_PIXELS (2048 * 1536)
#define BD_CORE_CUT 0.99f
#define BD_SOFT_ONLY(d) ((d)->alpha >= 31u)

static u8 c5to8(u32 v)
{
    v &= 31u;
    return (u8)((v << 3) | (v >> 2));
}

/* The uniforms, laid out as the shaders' cbuffers (16-byte rows). */
typedef struct G3dUniforms {
    int a[4];           /* mode, alpha5, textured, pass */
    int b[4];           /* texture w, h, alpha ref, ID  */
    int wrap[4];
    int c[4];           /* toon, highlight, front, back */
    int d[4];           /* w-buffer                     */
    float toon[32][4];
} G3dUniforms;

typedef struct EdgeUniforms {
    int p0[4];          /* step, variant, clear ID */
    float p1[4];        /* clear depth             */
    float edge[8][4];
} EdgeUniforms;

typedef struct FogUniforms {
    float fogc[4];
    float p0[4];        /* offset, step, alpha only */
    float clearc[4];
    float table[32][4];
} FogUniforms;

typedef struct BdVertexUniforms {
    float r[4][4];
    float eye[4];
    float pivot[4];
    int bb[4];
    float uv0[4];
    float uv1[4];
} BdVertexUniforms;

typedef struct BdFragmentUniforms {
    float eye[4];
    float atm[4];
    float sky[4];
    float fog[4];
    float fogc[4];
    int flags[4];       /* textured, front pass, Pokemon count */
    float cut[4];       /* alpha, cut, cut_hi                  */
    float poke[32][4];
} BdFragmentUniforms;

/* ------------------------------------------------------------------ */
/* Pipelines                                                           */
/* ------------------------------------------------------------------ */

/* A pipeline's key: what it is (PK_*), then its state bits. */
#define PK_GAME   0x01000000u   /* the game's polygons                      */
#define PK_BD     0x02000000u   /* the backdrop                             */
/* the game's: */
#define PKG_XLU   0x1u          /* the translucent stencil rule, no ID write */
#define PKG_DEQ   0x2u          /* the depth test passes equal              */
#define PKG_DW    0x4u          /* the depth is written                     */
/* the backdrop's: */
#define PKB_CULL  0x3u          /* 0 none, 1 back, 2 front                  */
#define PKB_BLEND 0x4u          /* the see-through pass                     */
#define PKB_STEN  0x8u          /* only where the game drew                 */
#define PKB_NOCOL 0x10u         /* no colour (only the front pass shows)    */

static void target3(SDL_GPUColorTargetDescription ct[3], SDL_GPUColorComponentFlags id_mask,
                    SDL_GPUColorComponentFlags depth_mask, int blend, int color)
{
    memset(ct, 0, 3 * sizeof *ct);
    ct[0].format = SG_RGBA;
    ct[0].blend_state.enable_color_write_mask = true;
    ct[0].blend_state.color_write_mask = color ? 0xF : 0;
    if (blend) {
        /* premultiplied colour over what is there */
        ct[0].blend_state.enable_blend = true;
        ct[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ct[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    ct[1].format = SG_ID;
    ct[1].blend_state.enable_color_write_mask = true;
    ct[1].blend_state.color_write_mask = id_mask;
    ct[2].format = SG_DEPTHCOPY;
    ct[2].blend_state.enable_color_write_mask = true;
    ct[2].blend_state.color_write_mask = depth_mask;
}

static SDL_GPUGraphicsPipeline *make_game(SdlGpu *s, Uint32 k)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct[3];
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[3];
    int xlu = (k & PKG_XLU) != 0, dw = (k & PKG_DW) != 0;

    memset(&ci, 0, sizeof ci);
    target3(ct, xlu ? 0 : SDL_GPU_COLORCOMPONENT_R, dw ? SDL_GPU_COLORCOMPONENT_R : 0, 1, 1);
    memset(&vb, 0, sizeof vb);
    vb.pitch = FLOATS_PER_VERTEX * sizeof(float);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    memset(at, 0, sizeof at);
    at[0].location = 0; at[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; at[0].offset = 0;
    at[1].location = 1; at[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; at[1].offset = 16;
    at[2].location = 2; at[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; at[2].offset = 24;
    ci.vertex_shader = s->vs_g3d;
    ci.fragment_shader = s->fs_g3d;
    ci.vertex_input_state.vertex_buffer_descriptions = &vb;
    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_attributes = at;
    ci.vertex_input_state.num_vertex_attributes = 3;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;   /* the shader culls */
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.depth_stencil_state.enable_depth_test = true;
    ci.depth_stencil_state.enable_depth_write = dw;
    ci.depth_stencil_state.compare_op = (k & PKG_DEQ) ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL
                                                      : SDL_GPU_COMPAREOP_LESS;
    /* The stencil holds 0x40 | the polygon ID of the translucent pixel last
     * drawn (0: none, or an opaque pixel since) and bit 7 for every pixel the
     * game drew: the hardware does not draw a translucent pixel over one of
     * the same ID.  The reference is set per draw. */
    ci.depth_stencil_state.enable_stencil_test = true;
    ci.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    ci.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    ci.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    ci.depth_stencil_state.front_stencil_state.compare_op = xlu ? SDL_GPU_COMPAREOP_NOT_EQUAL
                                                                : SDL_GPU_COMPAREOP_ALWAYS;
    ci.depth_stencil_state.back_stencil_state = ci.depth_stencil_state.front_stencil_state;
    ci.depth_stencil_state.compare_mask = xlu ? (Uint8)STENCIL_IDS : 0xFF;
    ci.depth_stencil_state.write_mask = 0xFF;
    ci.target_info.color_target_descriptions = ct;
    ci.target_info.num_color_targets = 3;
    ci.target_info.depth_stencil_format = s->ds_format;
    ci.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
}

static SDL_GPUGraphicsPipeline *make_bd(SdlGpu *s, Uint32 k)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct[3];
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[3];
    int blend = (k & PKB_BLEND) != 0;

    memset(&ci, 0, sizeof ci);
    target3(ct, 0, blend ? 0 : SDL_GPU_COLORCOMPONENT_R, blend, !(k & PKB_NOCOL));
    memset(&vb, 0, sizeof vb);
    vb.pitch = sizeof(BmapVertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    memset(at, 0, sizeof at);
    at[0].location = 0; at[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    at[0].offset = (Uint32)offsetof(BmapVertex, x);
    at[1].location = 1; at[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    at[1].offset = (Uint32)offsetof(BmapVertex, u);
    at[2].location = 2; at[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    at[2].offset = (Uint32)offsetof(BmapVertex, r);
    ci.vertex_shader = s->vs_bd;
    ci.fragment_shader = s->fs_bd;
    ci.vertex_input_state.vertex_buffer_descriptions = &vb;
    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_attributes = at;
    ci.vertex_input_state.num_vertex_attributes = 3;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = (k & PKB_CULL) == 1 ? SDL_GPU_CULLMODE_BACK
                                  : (k & PKB_CULL) == 2 ? SDL_GPU_CULLMODE_FRONT
                                                        : SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    /* no slope term: it pushed far, grazing triangles over the near map */
    ci.rasterizer_state.enable_depth_bias = true;
    ci.rasterizer_state.depth_bias_constant_factor = 1.0f;
    ci.depth_stencil_state.enable_depth_test = true;
    ci.depth_stencil_state.enable_depth_write = !blend;
    ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    if (k & PKB_STEN) {
        /* only where the game drew: the map in front of a sprite covers the
         * sprite and nothing else */
        ci.depth_stencil_state.enable_stencil_test = true;
        ci.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
        ci.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        ci.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
        ci.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_EQUAL;
        ci.depth_stencil_state.back_stencil_state = ci.depth_stencil_state.front_stencil_state;
        ci.depth_stencil_state.compare_mask = (Uint8)STENCIL_GAME;
        ci.depth_stencil_state.write_mask = 0;
    }
    ci.target_info.color_target_descriptions = ct;
    ci.target_info.num_color_targets = 3;
    ci.target_info.depth_stencil_format = s->ds_format;
    ci.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
}

/* The pipeline of a key, made the first time it is asked for. */
static SDL_GPUGraphicsPipeline *pipe_get(SdlGpu *s, Uint32 k)
{
    int i;
    SDL_GPUGraphicsPipeline *p;

    for (i = 0; i < s->n_pipe; i++) {
        if (s->pipe_key[i] == k) {
            return s->pipe[i];
        }
    }
    p = (k & PK_GAME) ? make_game(s, k) : make_bd(s, k);
    if (p != NULL && s->n_pipe < SG_PIPES) {
        s->pipe_key[s->n_pipe] = k;
        s->pipe[s->n_pipe++] = p;
    }
    return p;
}

/* A pipeline drawing a full-screen triangle (edge.hlsl's vertex shader) into
 * one RGBA8 target. */
static SDL_GPUGraphicsPipeline *make_post(SdlGpu *s, SDL_GPUShader *fs)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct;

    memset(&ci, 0, sizeof ci);
    memset(&ct, 0, sizeof ct);
    ct.format = SG_RGBA;
    ci.vertex_shader = s->vs_edge;
    ci.fragment_shader = fs;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.target_info.color_target_descriptions = &ct;
    ci.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
}

/* The depth set over a rectangle, nothing else written (depthclear.hlsl). */
static SDL_GPUGraphicsPipeline *make_dclear(SdlGpu *s)
{
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_GPUColorTargetDescription ct[3];

    memset(&ci, 0, sizeof ci);
    target3(ct, 0, SDL_GPU_COLORCOMPONENT_R, 0, 0);
    ci.vertex_shader = s->vs;
    ci.fragment_shader = s->fs_dclear;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.depth_stencil_state.enable_depth_test = true;
    ci.depth_stencil_state.enable_depth_write = true;
    ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    ci.target_info.color_target_descriptions = ct;
    ci.target_info.num_color_targets = 3;
    ci.target_info.depth_stencil_format = s->ds_format;
    ci.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(s->dev, &ci);
}

/* ------------------------------------------------------------------ */
/* Buffers and targets                                                 */
/* ------------------------------------------------------------------ */

/* Make room for `size` bytes in a GPU buffer and its transfer buffer. */
static int buf_reserve(SdlGpu *s, SgBuf *b, SDL_GPUBufferUsageFlags usage, Uint32 size)
{
    SDL_GPUBufferCreateInfo bi;
    SDL_GPUTransferBufferCreateInfo ti;
    Uint32 n;

    if (b->buf != NULL && b->size >= size) {
        return 0;
    }
    if (b->buf != NULL) SDL_ReleaseGPUBuffer(s->dev, b->buf);
    if (b->up != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, b->up);
    for (n = 4096; n < size; n *= 2) {
    }
    memset(&bi, 0, sizeof bi);
    bi.usage = usage;
    bi.size = n;
    b->buf = SDL_CreateGPUBuffer(s->dev, &bi);
    memset(&ti, 0, sizeof ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = n;
    b->up = SDL_CreateGPUTransferBuffer(s->dev, &ti);
    b->size = b->buf != NULL && b->up != NULL ? n : 0;
    return b->size >= size ? 0 : -1;
}

/* `bytes` from `src` into the buffer, in the copy pass. */
static void buf_upload(SdlGpu *s, SDL_GPUCopyPass *cp, SgBuf *b, const void *src, Uint32 bytes)
{
    SDL_GPUTransferBufferLocation from;
    SDL_GPUBufferRegion to;
    void *dst = SDL_MapGPUTransferBuffer(s->dev, b->up, true);

    if (dst == NULL) {
        return;
    }
    memcpy(dst, src, bytes);
    SDL_UnmapGPUTransferBuffer(s->dev, b->up);
    memset(&from, 0, sizeof from);
    memset(&to, 0, sizeof to);
    from.transfer_buffer = b->up;
    to.buffer = b->buf;
    to.size = bytes;
    SDL_UploadToGPUBuffer(cp, &from, &to, true);
}

static void buf_free(SdlGpu *s, SgBuf *b)
{
    if (b->buf != NULL) SDL_ReleaseGPUBuffer(s->dev, b->buf);
    if (b->up != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, b->up);
    memset(b, 0, sizeof *b);
}

static void fb_free(SdlGpu *s, SgFb *f)
{
    if (f->color != NULL) SDL_ReleaseGPUTexture(s->dev, f->color);
    if (f->color2 != NULL) SDL_ReleaseGPUTexture(s->dev, f->color2);
    if (f->id != NULL) SDL_ReleaseGPUTexture(s->dev, f->id);
    if (f->depth != NULL) SDL_ReleaseGPUTexture(s->dev, f->depth);
    if (f->ds != NULL) SDL_ReleaseGPUTexture(s->dev, f->ds);
    memset(f, 0, sizeof *f);
}

static int fb_resize(SdlGpu *s, SgFb *f, int w, int h)
{
    if (f->w == w && f->h == h && f->color != NULL) {
        return 0;
    }
    fb_free(s, f);
    f->color = sg_texture(s, SG_RGBA, w, h, 1, 1);
    f->color2 = sg_texture(s, SG_RGBA, w, h, 1, 1);
    f->id = sg_texture(s, SG_ID, w, h, 1, 1);
    f->depth = sg_texture(s, SG_DEPTHCOPY, w, h, 1, 1);
    {
        SDL_GPUTextureCreateInfo ci;

        memset(&ci, 0, sizeof ci);
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = s->ds_format;
        ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        ci.width = (Uint32)w;
        ci.height = (Uint32)h;
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        f->ds = SDL_CreateGPUTexture(s->dev, &ci);
    }
    if (f->color == NULL || f->color2 == NULL || f->id == NULL || f->depth == NULL || f->ds == NULL) {
        fb_free(s, f);
        return -1;
    }
    f->w = w;
    f->h = h;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The battle backdrop (plat_gl3d.c's, on SDL's GPU)                    */
/* ------------------------------------------------------------------ */

typedef struct BdPiece {
    unsigned draw, first, count;
    float mid[3];
} BdPiece;

typedef struct BdBound {
    float c[3], r;
} BdBound;

typedef struct SgBd {
    const void *scene;
    unsigned gen;
    float reach;
    SDL_GPUTexture **tex;
    unsigned char *wrap;        /* per texture: repeat s, t, flip s, t bits */
    unsigned n_tex, tex_gen;
    SDL_GPUSampler *smp[16];    /* by wrap bits                              */
    BdPiece *piece;
    unsigned n_piece;
    unsigned *order;
    float *dist;
    Uint32 *idx;
    unsigned n_idx;
    unsigned *at;
    float idx_eye[3];
    int idx_ok, idx_dirty;
    BdBound *bound;
    SgBuf vbuf, ibuf;
    int vdirty;                 /* the scene's vertices wait to go up        */
} SgBd;

static SgBd *bd_of(SdlGpu *s)
{
    if (s->bdrop == NULL) {
        s->bdrop = calloc(1, sizeof(SgBd));
    }
    return (SgBd *)s->bdrop;
}

static void bd_free_tex(SdlGpu *s, SgBd *b)
{
    unsigned i;

    for (i = 0; i < b->n_tex; i++) {
        if (b->tex[i] != NULL) SDL_ReleaseGPUTexture(s->dev, b->tex[i]);
    }
    free(b->tex);
    free(b->wrap);
    b->tex = NULL;
    b->wrap = NULL;
    b->n_tex = 0;
    b->tex_gen = 0;
}

static void bd_free_scene(SdlGpu *s, SgBd *b, int keep_tex)
{
    if (!keep_tex) {
        bd_free_tex(s, b);
    }
    free(b->piece);
    free(b->order);
    free(b->dist);
    free(b->idx);
    free(b->at);
    free(b->bound);
    b->piece = NULL;
    b->order = NULL;
    b->dist = NULL;
    b->idx = NULL;
    b->at = NULL;
    b->bound = NULL;
    b->idx_ok = 0;
    b->n_piece = 0;
    b->scene = NULL;
}

static void bd_pieces(SgBd *b, const BmapScene *sc)
{
    unsigned i, n = 0, cap = 0;

    for (i = 0; i < sc->nd; i++) {
        const BmapDraw *d = &sc->d[i];
        unsigned a, e;

        if (!d->translucent || d->count == 0 || d->first + d->count > sc->nv) {
            continue;
        }
        for (a = d->first; a < d->first + d->count; a = e) {
            BdPiece *pc;
            unsigned j;

            e = BD_SOFT_ONLY(d) ? d->first + d->count / 3u * 3u : a + 3;
            if (e > d->first + d->count || e == a) {
                break;
            }
            if (n == cap) {
                unsigned ncap = cap ? cap * 2 : 64;
                BdPiece *np = (BdPiece *)realloc(b->piece, ncap * sizeof *np);

                if (np == NULL) {
                    break;
                }
                b->piece = np;
                cap = ncap;
            }
            pc = &b->piece[n++];
            pc->draw = i;
            pc->first = a;
            pc->count = e - a;
            pc->mid[0] = pc->mid[1] = pc->mid[2] = 0.0f;
            for (j = a; j < e; j++) {
                pc->mid[0] += sc->v[j].x / (float)(e - a);
                pc->mid[1] += sc->v[j].y / (float)(e - a);
                pc->mid[2] += sc->v[j].z / (float)(e - a);
            }
        }
    }
    b->n_piece = n;
    b->order = (unsigned *)malloc((n > 0 ? n : 1) * sizeof *b->order);
    b->dist = (float *)malloc((n > 0 ? n : 1) * sizeof *b->dist);
    {
        unsigned k, verts = 0;

        for (k = 0; k < n; k++) {
            verts += b->piece[k].count;
        }
        b->idx = (Uint32 *)malloc((verts > 0 ? verts : 1) * sizeof *b->idx);
    }
    b->at = (unsigned *)malloc((n > 0 ? n : 1) * sizeof *b->at);
    b->idx_ok = 0;
    if (b->order == NULL || b->dist == NULL || b->idx == NULL || b->at == NULL) {
        b->n_piece = 0;
    }
}

static const float *g_sort_dist;

static int piece_far_first(const void *a, const void *b)
{
    float da = g_sort_dist[*(const unsigned *)a], db = g_sort_dist[*(const unsigned *)b];

    return da < db ? 1 : da > db ? -1 : 0;
}

/* A texture's box-filtered mip chain (plat_gl3d.c backdrop_mips: alpha-
 * weighted colour; a cut-out texture keeps its covered share), uploaded
 * level by level through the staging buffer, in the copy pass. */
static SDL_GPUTexture *bd_mips(SdlGpu *s, SDL_GPUCopyPass *cp, const BmapTexture *t)
{
    int w = t->w, h = t->h, lvl = 0, cut = 0, levels = 1, lw, lh;
    float cov = 0.0f;
    unsigned char *cur;
    SDL_GPUTexture *tex;

    for (lw = w, lh = h; lw > 1 || lh > 1; levels++) {
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    tex = sg_texture(s, SG_RGBA, w, h, levels, 0);
    cur = (unsigned char *)malloc((size_t)w * h * 4);
    if (tex == NULL || cur == NULL) {
        free(cur);
        return tex;
    }
    memcpy(cur, t->rgba, (size_t)w * h * 4);
    {
        int q, n = w * h, hard = 0, on = 0;

        for (q = 0; q < n; q++) {
            unsigned a = t->rgba[(size_t)q * 4 + 3];

            hard += a == 0u || a >= 248u;
            on += a >= 128u;
        }
        cut = hard * 10 >= n * 9 && on > 0 && on < n;
        cov = (float)on / (float)(n > 0 ? n : 1);
    }
    for (;;) {
        int nw, nh, x, y;
        unsigned char *nx;
        SDL_GPUTransferBufferCreateInfo ti;
        SDL_GPUTransferBuffer *tb;

        memset(&ti, 0, sizeof ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = (Uint32)(w * h * 4);
        tb = SDL_CreateGPUTransferBuffer(s->dev, &ti);
        if (tb != NULL) {
            void *dst = SDL_MapGPUTransferBuffer(s->dev, tb, false);

            if (dst != NULL) {
                SDL_GPUTextureTransferInfo from;
                SDL_GPUTextureRegion to;

                memcpy(dst, cur, (size_t)w * h * 4);
                SDL_UnmapGPUTransferBuffer(s->dev, tb);
                memset(&from, 0, sizeof from);
                memset(&to, 0, sizeof to);
                from.transfer_buffer = tb;
                from.pixels_per_row = (Uint32)w;
                from.rows_per_layer = (Uint32)h;
                to.texture = tex;
                to.mip_level = (Uint32)lvl;
                to.w = (Uint32)w;
                to.h = (Uint32)h;
                to.d = 1;
                SDL_UploadToGPUTexture(cp, &from, &to, false);
            }
            SDL_ReleaseGPUTransferBuffer(s->dev, tb);   /* freed once the pass is done */
        }
        if (w == 1 && h == 1) {
            break;
        }
        nw = w > 1 ? w / 2 : 1;
        nh = h > 1 ? h / 2 : 1;
        nx = (unsigned char *)malloc((size_t)nw * nh * 4);
        if (nx == NULL) {
            break;
        }
        for (y = 0; y < nh; y++) {
            for (x = 0; x < nw; x++) {
                unsigned sum[4] = { 0, 0, 0, 0 }, wa = 0, n = 0;
                int dx, dy, k;

                for (dy = 0; dy < 2; dy++) {
                    for (dx = 0; dx < 2; dx++) {
                        int sx = x * 2 + dx, sy = y * 2 + dy;
                        const unsigned char *p;

                        if (sx >= w) sx = w - 1;
                        if (sy >= h) sy = h - 1;
                        p = cur + ((size_t)sy * w + sx) * 4;
                        for (k = 0; k < 3; k++) {
                            sum[k] += (unsigned)p[k] * p[3];
                        }
                        sum[3] += p[3];
                        wa += p[3];
                        n++;
                    }
                }
                for (k = 0; k < 3; k++) {
                    nx[((size_t)y * nw + x) * 4 + k] = wa > 0 ? (unsigned char)(sum[k] / wa) : 0;
                }
                nx[((size_t)y * nw + x) * 4 + 3] = (unsigned char)(sum[3] / n);
            }
        }
        if (cut) {
            float lo = 1.0f, hi = 16.0f;
            int it, k2;

            for (it = 0; it < 12; it++) {
                float mid = 0.5f * (lo + hi);
                unsigned cnt = 0;

                for (k2 = 0; k2 < nw * nh; k2++) {
                    cnt += (float)nx[(size_t)k2 * 4 + 3] * mid >= 128.0f;
                }
                if ((float)cnt / (float)(nw * nh) < cov) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            for (k2 = 0; k2 < nw * nh; k2++) {
                float a2 = (float)nx[(size_t)k2 * 4 + 3] * hi;

                nx[(size_t)k2 * 4 + 3] = (unsigned char)(a2 > 255.0f ? 255.0f : a2);
            }
        }
        free(cur);
        cur = nx;
        w = nw;
        h = nh;
        lvl++;
    }
    free(cur);
    return tex;
}

/* A new scene on the GPU: its textures (unless the same build's), its
 * bounds, its see-through pieces; its vertices go up in the copy pass. */
static void bd_upload(SdlGpu *s, SgBd *b, SDL_GPUCopyPass *cp, const BmapScene *sc)
{
    unsigned i;
    int keep = sc->tex_gen != 0 && sc->tex_gen == b->tex_gen && sc->nt == b->n_tex && b->tex != NULL;

    bd_free_scene(s, b, keep);
    b->scene = sc;
    if (!keep && sc->nt > 0) {
        b->tex = (SDL_GPUTexture **)calloc(sc->nt, sizeof *b->tex);
        b->wrap = (unsigned char *)calloc(sc->nt, 1);
        if (b->tex == NULL || b->wrap == NULL) {
            b->scene = NULL;
            return;
        }
        b->n_tex = sc->nt;
        b->tex_gen = sc->tex_gen;
        for (i = 0; i < sc->nt; i++) {
            const BmapTexture *t = &sc->t[i];

            b->tex[i] = bd_mips(s, cp, t);
            b->wrap[i] = (unsigned char)((t->repeat_s ? 1 : 0) | (t->repeat_t ? 2 : 0) |
                                         (t->flip_s ? 4 : 0) | (t->flip_t ? 8 : 0));
        }
    }
    {
        float x0 = 0.0f, x1 = 0.0f, z0 = 7.0f, z1 = 7.0f;
        unsigned n;

        for (n = 0; n < sc->nv; n++) {
            const BmapVertex *v = &sc->v[n];

            x0 = v->x < x0 ? v->x : x0;
            x1 = v->x > x1 ? v->x : x1;
            z0 = v->z < z0 ? v->z : z0;
            z1 = v->z > z1 ? v->z : z1;
        }
        b->reach = fminf(fminf(-x0, x1), fminf(7.0f - z0, z1 - 7.0f));
    }
    b->bound = sc->nd > 0 ? (BdBound *)malloc(sc->nd * sizeof *b->bound) : NULL;
    if (b->bound != NULL) {
        unsigned d, k;

        for (d = 0; d < sc->nd; d++) {
            const BmapDraw *dr = &sc->d[d];
            BdBound *bb = &b->bound[d];
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            float r2 = 0.0f;

            for (k = 0; k < dr->count && dr->first + k < sc->nv; k++) {
                const BmapVertex *v = &sc->v[dr->first + k];

                lo[0] = fminf(lo[0], v->x); hi[0] = fmaxf(hi[0], v->x);
                lo[1] = fminf(lo[1], v->y); hi[1] = fmaxf(hi[1], v->y);
                lo[2] = fminf(lo[2], v->z); hi[2] = fmaxf(hi[2], v->z);
            }
            if (lo[0] > hi[0]) {
                bb->c[0] = bb->c[1] = bb->c[2] = 0.0f;
                bb->r = 1e30f;
                continue;
            }
            for (k = 0; k < 3; k++) {
                bb->c[k] = dr->billboard ? dr->pivot[k] : 0.5f * (lo[k] + hi[k]);
            }
            for (k = 0; k < dr->count && dr->first + k < sc->nv; k++) {
                const BmapVertex *v = &sc->v[dr->first + k];
                float dx = v->x - bb->c[0], dy = v->y - bb->c[1], dz = v->z - bb->c[2];

                r2 = fmaxf(r2, dx * dx + dy * dy + dz * dz);
            }
            bb->r = sqrtf(r2);
        }
    }
    bd_pieces(b, sc);
    if (buf_reserve(s, &b->vbuf, SDL_GPU_BUFFERUSAGE_VERTEX,
                    (Uint32)((size_t)sc->nv * sizeof(BmapVertex))) == 0) {
        buf_upload(s, cp, &b->vbuf, sc->v, (Uint32)((size_t)sc->nv * sizeof(BmapVertex)));
    }
}

static SDL_GPUSampler *bd_sampler(SdlGpu *s, SgBd *b, unsigned wrap)
{
    if (b->smp[wrap & 15u] == NULL) {
        SDL_GPUSamplerCreateInfo ci;

        memset(&ci, 0, sizeof ci);
        ci.min_filter = SDL_GPU_FILTER_NEAREST;
        ci.mag_filter = SDL_GPU_FILTER_NEAREST;
        ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        ci.address_mode_u = !(wrap & 1) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                          : (wrap & 4) ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
                                       : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        ci.address_mode_v = !(wrap & 2) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                          : (wrap & 8) ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
                                       : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.max_lod = 1000.0f;
        b->smp[wrap & 15u] = SDL_CreateGPUSampler(s->dev, &ci);
    }
    return b->smp[wrap & 15u];
}

static void vcross(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float vdot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void vnorm(float *v)
{
    float l = sqrtf(vdot(v, v));

    if (l > 1e-6f) {
        v[0] /= l; v[1] /= l; v[2] /= l;
    }
}

/* clip = the game's perspective times its look-at, x and y scaled by the
 * view scale (plat_gl3d.c backdrop_matrix). */
static void bd_matrix(const G3dBackdrop *b, float r[4][4])
{
    float f[3], rt[3], u[3], t, m0, m5, a, c, ex, ey, ef, kx, ky;
    fx32 kxf, kyf;
    int i;

    g3d_mtx_view_scale(&kxf, &kyf);
    kx = (float)kxf / 4096.0f;
    ky = (float)kyf / 4096.0f;
    for (i = 0; i < 3; i++) {
        f[i] = b->target[i] - b->eye[i];
    }
    vnorm(f);
    vcross(f, b->up, rt);
    vnorm(rt);
    vcross(rt, f, u);
    t = b->fovy_sin / b->fovy_cos;
    m5 = 1.0f / t;
    m0 = m5 / b->aspect;
    a = -(b->zfar + b->znear) / (b->zfar - b->znear);
    c = -2.0f * b->zfar * b->znear / (b->zfar - b->znear);
    ex = vdot(rt, b->eye);
    ey = vdot(u, b->eye);
    ef = vdot(f, b->eye);
    for (i = 0; i < 3; i++) {
        r[0][i] = m0 * kx * rt[i];
        r[1][i] = m5 * ky * u[i];
        r[2][i] = -a * f[i];
        r[3][i] = f[i];
    }
    r[0][3] = -m0 * kx * ex;
    r[1][3] = -m5 * ky * ey;
    r[2][3] = a * ef + c;
    r[3][3] = -ef;
}

/* The see-through pieces far to near from this camera, as one index buffer
 * (the back and the front pass share it), up in the copy pass. */
static void bd_sort(SdlGpu *s, SgBd *b, SDL_GPUCopyPass *cp, const G3dBackdrop *g)
{
    unsigned i;

    if (b->n_piece == 0 || (b->idx_ok && b->idx_eye[0] == g->eye[0] && b->idx_eye[1] == g->eye[1] &&
                            b->idx_eye[2] == g->eye[2])) {
        return;
    }
    for (i = 0; i < b->n_piece; i++) {
        const float *m = b->piece[i].mid;
        float dx = m[0] - g->eye[0], dy = m[1] - g->eye[1], dz = m[2] - g->eye[2];

        b->order[i] = i;
        b->dist[i] = dx * dx + dy * dy + dz * dz;
    }
    g_sort_dist = b->dist;
    qsort(b->order, b->n_piece, sizeof *b->order, piece_far_first);
    b->n_idx = 0;
    for (i = 0; i < b->n_piece; i++) {
        const BdPiece *pc = &b->piece[b->order[i]];
        unsigned k;

        b->at[i] = b->n_idx;
        for (k = 0; k < pc->count; k++) {
            b->idx[b->n_idx++] = pc->first + k;
        }
    }
    if (b->n_idx > 0 &&
        buf_reserve(s, &b->ibuf, SDL_GPU_BUFFERUSAGE_INDEX, b->n_idx * (Uint32)sizeof(Uint32)) == 0) {
        buf_upload(s, cp, &b->ibuf, b->idx, b->n_idx * (Uint32)sizeof(Uint32));
    }
    memcpy(b->idx_eye, g->eye, sizeof b->idx_eye);
    b->idx_ok = 1;
}

/* The depth reset to the clear plane's over the Pokemon's boxes, before the
 * front pass (plat_gl3d.c front_depth_clear: a scissored clear there). */
static void bd_front_clear(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                           const G3dBackdrop *b, const float r[4][4], int w, int h, float cdepth)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, rect[4], u[4];
    unsigned i, c;

    for (i = 0; i < b->n_poke && i < G3D_BACKDROP_SPRITES; i++) {
        float d[3], L, n[3], side[3], sl;

        for (c = 0; c < 3; c++) {
            d[c] = b->poke[i][c] - b->eye[c];
        }
        L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L < 1e-3f) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            n[c] = d[c] / L;
        }
        side[0] = -n[2];
        side[1] = 0.0f;
        side[2] = n[0];
        sl = sqrtf(side[0] * side[0] + side[2] * side[2]);
        if (sl < 1e-3f) {
            continue;
        }
        side[0] /= sl;
        side[2] /= sl;
        for (c = 0; c < 4; c++) {
            float p[3], cl[4];
            int k;
            float hgt = fabsf(b->poke[i][3]);
            float sx = (c & 1) ? 0.47f * hgt : -0.47f * hgt, uy = (c & 2) ? hgt : -1.0f;

            p[0] = b->poke[i][0] + side[0] * sx;
            p[1] = b->poke[i][1] + uy;
            p[2] = b->poke[i][2] + side[2] * sx;
            for (k = 0; k < 4; k++) {
                cl[k] = r[k][0] * p[0] + r[k][1] * p[1] + r[k][2] * p[2] + r[k][3];
            }
            if (cl[3] < 0.05f) {
                x0 = y0 = -1e9f;
                x1 = y1 = 1e9f;
                continue;
            }
            x0 = fminf(x0, cl[0] / cl[3]);
            x1 = fmaxf(x1, cl[0] / cl[3]);
            y0 = fminf(y0, cl[1] / cl[3]);
            y1 = fmaxf(y1, cl[1] / cl[3]);
        }
    }
    if (x1 < x0) {
        return;
    }
    /* two pixels more, as the scissor's rounding gave */
    x0 = fmaxf(x0, -1.0f);
    y0 = fmaxf(y0, -1.0f);
    x1 = fminf(x1 + 4.0f / (float)w, 1.0f);
    y1 = fminf(y1 + 4.0f / (float)h, 1.0f);
    if (x1 <= x0 || y1 <= y0 || s->p_dclear == NULL) {
        return;
    }
    rect[0] = x0;
    rect[1] = y0;
    rect[2] = x1;
    rect[3] = y1;
    memset(u, 0, sizeof u);
    u[0] = cdepth;
    SDL_BindGPUGraphicsPipeline(pass, s->p_dclear);
    SDL_PushGPUVertexUniformData(cmd, 0, rect, sizeof rect);
    SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof u);
    SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
}

static SDL_GPUTexture *g_dummy;   /* a 1x1 texture for an untextured draw */

static void bd_draw(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                    const G3dBackdrop *bk, int front, int w, int h, float cdepth)
{
    SgBd *b = bd_of(s);
    const BmapScene *sc = (const BmapScene *)bk->scene;
    float r[4][4], fwd[3];
    float atm[4] = { 0.0f, 7.0f, 0.0f, 0.0f };
    unsigned pass_i, i;
    unsigned uv_frame = (unsigned)(SDL_GetTicks() * 60u / 1000u);
    BdVertexUniforms vu;
    BdFragmentUniforms fu;
    SDL_GPUBufferBinding vb;
    Uint32 base = PK_BD;

    if (b == NULL || sc == NULL || sc->nv == 0 || b->scene != sc || b->vbuf.buf == NULL) {
        return;
    }
    bd_matrix(bk, r);
    for (i = 0; i < 3; i++) {
        fwd[i] = bk->target[i] - bk->eye[i];
    }
    vnorm(fwd);
    memset(&vu, 0, sizeof vu);
    memset(&fu, 0, sizeof fu);
    memcpy(vu.r, r, sizeof vu.r);
    for (i = 0; i < 3; i++) {
        vu.eye[i] = bk->eye[i];
        fu.eye[i] = bk->eye[i];
    }
    if (b->reach > 40.0f) {
        atm[2] = 0.45f * b->reach;
        atm[3] = 0.97f * b->reach;
    }
    memcpy(fu.atm, atm, sizeof fu.atm);
    fu.sky[0] = (float)bk->sky[0] / 255.0f;
    fu.sky[1] = (float)bk->sky[1] / 255.0f;
    fu.sky[2] = (float)bk->sky[2] / 255.0f;
    fu.flags[1] = front;
    fu.flags[2] = (int)(bk->n_poke > G3D_BACKDROP_SPRITES ? G3D_BACKDROP_SPRITES : bk->n_poke);
    memcpy(fu.poke, bk->poke, sizeof fu.poke);
    fu.fog[1] = 7.0f;
    fu.fog[3] = 1.0f;
    if (sc->fog_on && sc->fog_end > sc->fog_start) {
        float k = sc->place_k > 0.0f ? sc->place_k : BMAP_PLACE_K;

        fu.fog[2] = sc->fog_start * k + 1e-3f;
        fu.fog[3] = (sc->fog_end - sc->fog_start) * k;
        fu.fogc[0] = (float)sc->fog[0] / 255.0f;
        fu.fogc[1] = (float)sc->fog[1] / 255.0f;
        fu.fogc[2] = (float)sc->fog[2] / 255.0f;
    }
    if (front) {
        bd_front_clear(s, cmd, pass, bk, (const float (*)[4])r, w, h, cdepth);
        if (!bk->front_only) {
            base |= PKB_STEN;
            SDL_SetGPUStencilReference(pass, (Uint8)STENCIL_GAME);
        }
    }
    if (bk->front_only && !front) {
        base |= PKB_NOCOL;
    }
    vb.buffer = b->vbuf.buf;
    vb.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    for (pass_i = 0; pass_i < 2; pass_i++) {
        unsigned n = pass_i == 1 && b->n_piece > 0 ? b->n_piece : sc->nd;
        int sorted = pass_i == 1 && b->n_piece > 0 && b->ibuf.buf != NULL && b->idx_ok;

        if (sorted) {
            SDL_GPUBufferBinding ib;

            ib.buffer = b->ibuf.buf;
            ib.offset = 0;
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        }
        for (i = 0; i < n; i++) {
            const BmapDraw *d;
            unsigned first, count, run = 1, di;
            SDL_GPUGraphicsPipeline *p;
            SDL_GPUTextureSamplerBinding tb;

            if (sorted) {
                const BdPiece *pc = &b->piece[b->order[i]];

                di = pc->draw;
                d = &sc->d[di];
                first = pc->first;
                count = pc->count;
                while (i + run < n && b->piece[b->order[i + run]].draw == pc->draw) {
                    run++;
                }
            } else {
                di = i;
                d = &sc->d[i];
                first = d->first;
                count = d->count;
            }
            if (b->bound != NULL && di < sc->nd) {
                const BdBound *bb = &b->bound[di];
                float depth = fwd[0] * (bb->c[0] - bk->eye[0]) + fwd[1] * (bb->c[1] - bk->eye[1])
                              + fwd[2] * (bb->c[2] - bk->eye[2]);
                float ax = bb->c[0] - atm[0], az = bb->c[2] - atm[1];

                if (depth < -bb->r || (atm[3] > 0.0f && sqrtf(ax * ax + az * az) - bb->r > atm[3])) {
                    i += run - 1;
                    continue;
                }
            }
            if (((d->translucent != 0) != (pass_i == 1) &&
                 !(pass_i == 0 && d->translucent && BD_SOFT_ONLY(d)))
                || count == 0 || d->alpha == 0 || first + count > sc->nv) {
                i += run - 1;
                continue;
            }
            p = pipe_get(s, base | (pass_i == 1 ? PKB_BLEND : 0u) |
                                (d->cull == 0 ? 0u : d->cull == 1 ? 1u : 2u));
            if (p == NULL) {
                i += run - 1;
                continue;
            }
            SDL_BindGPUGraphicsPipeline(pass, p);
            if (d->tex >= 0 && (unsigned)d->tex < b->n_tex && b->tex[d->tex] != NULL) {
                tb.texture = b->tex[d->tex];
                tb.sampler = bd_sampler(s, b, b->wrap[d->tex]);
                fu.flags[0] = 1;
            } else {
                tb.texture = g_dummy;
                tb.sampler = s->nearest;
                fu.flags[0] = 0;
            }
            SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
            if (d->uvanim != 0 && d->uvanim <= sc->nuva && sc->uva[d->uvanim - 1].frames > 0) {
                const BmapUvAnim *ua = &sc->uva[d->uvanim - 1];
                const float *m = ua->m + (size_t)(uv_frame % ua->frames) * 6u;

                memcpy(vu.uv0, m, 3 * sizeof(float));
                memcpy(vu.uv1, m + 3, 3 * sizeof(float));
            } else {
                vu.uv0[0] = 1.0f; vu.uv0[1] = 0.0f; vu.uv0[2] = 0.0f;
                vu.uv1[0] = 0.0f; vu.uv1[1] = 1.0f; vu.uv1[2] = 0.0f;
            }
            vu.bb[0] = d->billboard;
            memcpy(vu.pivot, d->pivot, 3 * sizeof(float));
            fu.cut[0] = (float)d->alpha / 31.0f;
            if (pass_i == 0) {
                fu.cut[1] = d->translucent ? BD_CORE_CUT : 0.5f;
                fu.cut[2] = 2.0f;
            } else {
                fu.cut[1] = 0.02f;
                fu.cut[2] = BD_SOFT_ONLY(d) ? BD_CORE_CUT : 2.0f;
            }
            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
            SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
            if (sorted) {
                unsigned end = i + run < b->n_piece ? b->at[i + run] : b->n_idx;

                SDL_DrawGPUIndexedPrimitives(pass, end - b->at[i], 1, b->at[i], 0, 0);
                i += run - 1;
            } else {
                SDL_DrawGPUPrimitives(pass, count, 1, first, 0);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

/* What a snapshot texture holds (plat_gl3d.c tex_key: FNV-1a). */
static unsigned tex_key(const G3dSnapTex *t)
{
    unsigned h = 2166136261u;
    size_t i, n = (size_t)t->w * t->h * 4u;
    const u32 *w = (const u32 *)t->rgba;

    h = (h ^ t->w) * 16777619u;
    h = (h ^ t->h) * 16777619u;
    if (t->content != 0) {
        h = (h ^ (unsigned)t->content) * 16777619u;
        h = (h ^ (unsigned)(t->content >> 32)) * 16777619u;
        return h != 0 ? h : 1u;
    }
    for (i = 0; t->rgba != NULL && i < n / 4u; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h != 0 ? h : 1u;
}

/* The snapshot's textures that changed, up in the copy pass through one
 * staging buffer (offsets kept 512-aligned for Direct3D 12). */
static int upload_textures(SdlGpu *s, SDL_GPUCopyPass *cp, const G3dSnapshot *snap)
{
    unsigned i;
    Uint32 total = 0, at = 0;
    unsigned char *dst;
    int any = 0;

    while (s->n_gtex < snap->n_tex) {
        if (s->n_gtex == s->cap_gtex) {
            unsigned c = s->cap_gtex ? s->cap_gtex * 2u : 16u;
            void *a = realloc(s->gtex, c * sizeof *s->gtex);
            void *k, *w, *h;

            if (a == NULL) return -1;
            s->gtex = (SDL_GPUTexture **)a;
            k = realloc(s->gtex_key, c * sizeof *s->gtex_key);
            if (k == NULL) return -1;
            s->gtex_key = (unsigned *)k;
            w = realloc(s->gtex_w, c * sizeof *s->gtex_w);
            if (w == NULL) return -1;
            s->gtex_w = (int *)w;
            h = realloc(s->gtex_h, c * sizeof *s->gtex_h);
            if (h == NULL) return -1;
            s->gtex_h = (int *)h;
            s->cap_gtex = c;
        }
        s->gtex[s->n_gtex] = NULL;
        s->gtex_key[s->n_gtex] = 0;
        s->gtex_w[s->n_gtex] = s->gtex_h[s->n_gtex] = 0;
        s->n_gtex++;
    }
    if (s->tex_serial_valid && s->tex_serial == snap->serial) {
        return 0;
    }
    for (i = 0; i < snap->n_tex; i++) {
        unsigned key = tex_key(&snap->tex[i]);

        if (s->gtex_key[i] == key && key != 0 && s->gtex[i] != NULL) {
            continue;
        }
        if (snap->tex[i].rgba == NULL || snap->tex[i].w == 0 || snap->tex[i].h == 0) {
            continue;
        }
        total += ((Uint32)(snap->tex[i].w * snap->tex[i].h * 4u) + 511u) & ~511u;
    }
    if (total > 0) {
        if (s->stage == NULL || s->stage_size < total) {
            SDL_GPUTransferBufferCreateInfo ti;

            if (s->stage != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, s->stage);
            memset(&ti, 0, sizeof ti);
            ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            ti.size = total;
            s->stage = SDL_CreateGPUTransferBuffer(s->dev, &ti);
            s->stage_size = s->stage != NULL ? total : 0;
            if (s->stage == NULL) return -1;
        }
        /* first the texels into the staging buffer (where each goes kept),
         * then, unmapped, the uploads */
        Uint32 *where = (Uint32 *)malloc(snap->n_tex * sizeof *where);

        if (where == NULL) return -1;
        dst = (unsigned char *)SDL_MapGPUTransferBuffer(s->dev, s->stage, true);
        if (dst == NULL) {
            free(where);
            return -1;
        }
        for (i = 0; i < snap->n_tex; i++) {
            const G3dSnapTex *t = &snap->tex[i];
            unsigned key = tex_key(t);
            Uint32 bytes;

            where[i] = 0xFFFFFFFFu;
            if ((s->gtex_key[i] == key && key != 0 && s->gtex[i] != NULL) || t->rgba == NULL ||
                t->w == 0 || t->h == 0) {
                continue;
            }
            if (s->gtex[i] == NULL || s->gtex_w[i] != (int)t->w || s->gtex_h[i] != (int)t->h) {
                if (s->gtex[i] != NULL) SDL_ReleaseGPUTexture(s->dev, s->gtex[i]);
                s->gtex[i] = sg_texture(s, SG_RGBA, (int)t->w, (int)t->h, 1, 0);
                s->gtex_w[i] = (int)t->w;
                s->gtex_h[i] = (int)t->h;
            }
            if (s->gtex[i] == NULL) {
                continue;
            }
            bytes = t->w * t->h * 4u;
            memcpy(dst + at, t->rgba, bytes);
            where[i] = at;
            s->gtex_key[i] = key;
            at += (bytes + 511u) & ~511u;
        }
        SDL_UnmapGPUTransferBuffer(s->dev, s->stage);
        for (i = 0; i < snap->n_tex; i++) {
            SDL_GPUTextureTransferInfo from;
            SDL_GPUTextureRegion to;

            if (where[i] == 0xFFFFFFFFu) {
                continue;
            }
            memset(&from, 0, sizeof from);
            memset(&to, 0, sizeof to);
            from.transfer_buffer = s->stage;
            from.offset = where[i];
            from.pixels_per_row = snap->tex[i].w;
            from.rows_per_layer = snap->tex[i].h;
            to.texture = s->gtex[i];
            to.w = snap->tex[i].w;
            to.h = snap->tex[i].h;
            to.d = 1;
            SDL_UploadToGPUTexture(cp, &from, &to, false);
            any = 1;
        }
        free(where);
    }
    (void)any;
    s->tex_serial = snap->serial;
    s->tex_serial_valid = 1;
    return 0;
}

/* One run of polygons that share their state (plat_gl3d.c Gl3dRun). */
typedef struct Run {
    int deq, mask, xlu_depth, mode, toon, a5, front, back;
    int textured, slot, wrap, id, two_pass, fog;
} Run;

static void draw_run(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                     const G3dSnapshot *snap, const Run *d, unsigned first, unsigned verts,
                     G3dUniforms *u)
{
    Uint8 xid = (Uint8)(0x40 | d->id);
    int uid = 0x80 | (d->fog ? 0x40 : 0) | d->id;
    SDL_GPUTextureSamplerBinding tb;
    Uint32 deq = d->deq ? PKG_DEQ : 0u;

    u->a[0] = d->mode;
    u->a[1] = d->a5;
    u->c[0] = d->toon;
    u->c[2] = d->front;
    u->c[3] = d->back;
    u->b[3] = uid;
    if (d->textured && d->slot >= 0 && (unsigned)d->slot < s->n_gtex && s->gtex[d->slot] != NULL) {
        tb.texture = s->gtex[d->slot];
        u->a[2] = 1;
        u->b[0] = (int)snap->tex[d->slot].w;
        u->b[1] = (int)snap->tex[d->slot].h;
        u->wrap[0] = d->wrap & 1;
        u->wrap[1] = (d->wrap >> 1) & 1;
        u->wrap[2] = (d->wrap >> 2) & 1;
        u->wrap[3] = (d->wrap >> 3) & 1;
    } else {
        tb.texture = g_dummy;
        u->a[2] = 0;
    }
    tb.sampler = s->nearest;
    SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
    if (d->two_pass) {
        /* an opaque polygon whose texture has partly transparent texels:
         * the opaque texels write depth and the ID, the translucent ones
         * keep the ID beneath them (plat_gl3d.c draw_run) */
        SDL_GPUGraphicsPipeline *p1 = pipe_get(s, PK_GAME | deq | PKG_DW);
        SDL_GPUGraphicsPipeline *p2 = pipe_get(s, PK_GAME | deq | PKG_XLU |
                                                  (d->xlu_depth ? PKG_DW : 0u));

        if (p1 != NULL) {
            u->a[3] = 1;
            SDL_BindGPUGraphicsPipeline(pass, p1);
            SDL_SetGPUStencilReference(pass, (Uint8)STENCIL_GAME);
            SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof *u);
            SDL_DrawGPUPrimitives(pass, verts, 1, first, 0);
        }
        if (p2 != NULL) {
            u->a[3] = 2;
            SDL_BindGPUGraphicsPipeline(pass, p2);
            SDL_SetGPUStencilReference(pass, (Uint8)(xid | STENCIL_GAME));
            SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof *u);
            SDL_DrawGPUPrimitives(pass, verts, 1, first, 0);
        }
        u->a[3] = 0;
        return;
    }
    {
        int opaque = d->a5 >= 31;
        SDL_GPUGraphicsPipeline *p = pipe_get(s, PK_GAME | deq | (opaque ? 0u : PKG_XLU) |
                                                 (d->mask ? PKG_DW : 0u));

        if (p == NULL) {
            return;
        }
        u->a[3] = 0;
        SDL_BindGPUGraphicsPipeline(pass, p);
        SDL_SetGPUStencilReference(pass, opaque ? (Uint8)STENCIL_GAME : (Uint8)(xid | STENCIL_GAME));
        SDL_PushGPUFragmentUniformData(cmd, 0, u, sizeof *u);
        SDL_DrawGPUPrimitives(pass, verts, 1, first, 0);
    }
}

/* A full-screen pass (edge or fog) from the frame's targets into color2,
 * which then becomes the frame's colour. */
static void post_pass(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SgFb *f, SDL_GPUGraphicsPipeline *p,
                      const void *u, Uint32 usize)
{
    SDL_GPUColorTargetInfo ct;
    SDL_GPURenderPass *pass;
    SDL_GPUTextureSamplerBinding tb[3];
    SDL_GPUTexture *t;

    memset(&ct, 0, sizeof ct);
    ct.texture = f->color2;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    if (pass == NULL) {
        return;
    }
    tb[0].texture = f->color;
    tb[1].texture = f->id;
    tb[2].texture = f->depth;
    tb[0].sampler = tb[1].sampler = tb[2].sampler = s->nearest;
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_BindGPUFragmentSamplers(pass, 0, tb, 3);
    SDL_PushGPUFragmentUniformData(cmd, 0, u, usize);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    t = f->color;
    f->color = f->color2;
    f->color2 = t;
}

static int render_into(SdlGpu *s, SDL_GPUCommandBuffer *cmd, SgFb *fb, const G3dSnapshot *snap,
                       int w, int h)
{
    unsigned i, nv = 0;
    size_t need;
    u32 cc, alpha;
    u8 cr, cg, cb;
    int edges, fog, backdrop;
    float cdepth;
    SDL_GPUCopyPass *cp;
    SDL_GPURenderPass *pass;
    SDL_GPUColorTargetInfo ct[3];
    SDL_GPUDepthStencilTargetInfo dt;
    unsigned long long t_dr;
    SgBd *bd = bd_of(s);

    if (!s->have3d || snap == NULL || w < 1 || h < 1 || bd == NULL) {
        return -1;
    }
    backdrop = snap->backdrop.valid && !snap->wbuffer;
    if (fb_resize(s, fb, w, h) != 0) {
        return -1;
    }
    t_dr = eidos_prof_begin();
    /* the vertices: each polygon as a fan of triangles */
    need = (size_t)snap->n_polys * 6u * FLOATS_PER_VERTEX;
    if (need > s->cap_vtx) {
        float *n = (float *)realloc(s->vtx, need * sizeof(float));

        if (n == NULL) {
            return -1;
        }
        s->vtx = n;
        s->cap_vtx = need;
    }
    for (i = 0; i < snap->n_polys; i++) {
        const G3dPoly *p = &snap->polys[i];
        static const unsigned fan[6] = { 0, 1, 2, 0, 2, 3 };
        unsigned tri, ntri;

        if (p->count < 3u || p->count > 4u) {
            continue;
        }
        ntri = p->count - 2u;
        for (tri = 0; tri < ntri * 3u; tri++) {
            const G3dVertex *v = &snap->verts[snap->idx[p->first + fan[tri]]];
            float *o = s->vtx + (size_t)nv * FLOATS_PER_VERTEX;

            o[0] = (float)v->x; o[1] = (float)v->y;
            o[2] = (float)v->z; o[3] = (float)v->w;
            o[4] = (float)v->s; o[5] = (float)v->t;
            o[6] = (float)v->r; o[7] = (float)v->g; o[8] = (float)v->b;
            nv++;
        }
    }

    /* the copy pass: textures, vertices, the backdrop's scene and order */
    cp = SDL_BeginGPUCopyPass(cmd);
    if (upload_textures(s, cp, snap) != 0) {
        SDL_EndGPUCopyPass(cp);
        return -1;
    }
    if (nv > 0 && buf_reserve(s, &s->vbuf, SDL_GPU_BUFFERUSAGE_VERTEX,
                              (Uint32)(nv * FLOATS_PER_VERTEX * sizeof(float))) == 0) {
        buf_upload(s, cp, &s->vbuf, s->vtx, (Uint32)(nv * FLOATS_PER_VERTEX * sizeof(float)));
    } else if (nv > 0) {
        nv = 0;
    }
    if (backdrop && snap->backdrop.scene != NULL) {
        const BmapScene *sc = (const BmapScene *)snap->backdrop.scene;

        if (bd->scene != sc || bd->gen != snap->backdrop.scene_gen) {
            bd_upload(s, bd, cp, sc);
            bd->gen = snap->backdrop.scene_gen;
        }
        bd_sort(s, bd, cp, &snap->backdrop);
    }
    SDL_EndGPUCopyPass(cp);

    edges = (snap->disp3dcnt & 0x20u) != 0 && s->p_edge != NULL && s->edge_mode != 0 &&
            !(getenv("EIDOS_EDGE") != NULL && getenv("EIDOS_EDGE")[0] == '0');
    fog = (snap->disp3dcnt & 0x80u) != 0 && s->p_fog != NULL;

    /* the clear plane: premultiplied colour, alpha as set */
    cc = snap->clear_color;
    alpha = ((cc >> 16) & 0x1Fu) * 255u / 31u;
    cr = c5to8(cc);
    cg = c5to8(cc >> 5);
    cb = c5to8(cc >> 10);
    if (backdrop) {
        alpha = 255u;
        cr = snap->backdrop.sky[0];
        cg = snap->backdrop.sky[1];
        cb = snap->backdrop.sky[2];
    }
    if (backdrop && snap->backdrop.front_only) {
        alpha = 0u;
    }
    cdepth = snap->wbuffer ? (float)(snap->clear_depth & 0x7FFFu) / 32768.0f
                           : ((snap->clear_depth & 0x7FFFu) == 0x7FFFu
                              ? 1.0f : (float)(snap->clear_depth & 0x7FFFu) / 32767.0f);
    memset(ct, 0, sizeof ct);
    ct[0].texture = fb->color;
    ct[0].clear_color.r = (float)cr / 255.f * (float)alpha / 255.f;
    ct[0].clear_color.g = (float)cg / 255.f * (float)alpha / 255.f;
    ct[0].clear_color.b = (float)cb / 255.f * (float)alpha / 255.f;
    ct[0].clear_color.a = (float)alpha / 255.f;
    ct[1].texture = fb->id;
    ct[1].clear_color.r = (float)(((cc >> 24) & 0x3Fu) | ((cc & 0x8000u) ? 0x40u : 0u)) / 255.f;
    ct[2].texture = fb->depth;
    ct[2].clear_color.r = cdepth;
    for (i = 0; i < 3; i++) {
        ct[i].load_op = SDL_GPU_LOADOP_CLEAR;
        ct[i].store_op = SDL_GPU_STOREOP_STORE;
    }
    memset(&dt, 0, sizeof dt);
    dt.texture = fb->ds;
    dt.clear_depth = cdepth;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.clear_stencil = 0;
    pass = SDL_BeginGPURenderPass(cmd, ct, 3, &dt);
    if (pass == NULL) {
        return -1;
    }
    if (backdrop) {
        bd_draw(s, cmd, pass, &snap->backdrop, 0, w, h, cdepth);
    }
    if (nv > 0) {
        G3dUniforms u;
        SDL_GPUBufferBinding vb;
        Run run, cur;
        unsigned run_first = 0, run_verts = 0, first = 0;
        int have = 0;

        memset(&u, 0, sizeof u);
        for (i = 0; i < 32u; i++) {
            u.toon[i][0] = (float)snap->toon[i * 3 + 0];
            u.toon[i][1] = (float)snap->toon[i * 3 + 1];
            u.toon[i][2] = (float)snap->toon[i * 3 + 2];
        }
        u.b[2] = (int)snap->alpha_ref;
        u.c[1] = (int)snap->highlight;
        u.d[0] = (int)snap->wbuffer;
        vb.buffer = s->vbuf.buf;
        vb.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        memset(&run, 0, sizeof run);
        memset(&cur, 0, sizeof cur);
        for (i = 0; i < snap->n_polys; i++) {
            const G3dPoly *p = &snap->polys[i];
            u32 attr = p->attr, mode, a5, ti, verts;
            s32 slot = snap->poly_tex[i];

            if (p->count < 3u || p->count > 4u) {
                continue;
            }
            verts = (p->count - 2u) * 3u;
            if (attr == 0) {
                attr = ATTR_DEFAULT;
            }
            mode = G3D_ATTR_MODE(attr);
            a5 = G3D_ATTR_ALPHA(attr);
            if (a5 == 0 || mode == G3D_POLY_MODE_SHADOW) {
                first += verts;
                continue;
            }
            ti = p->teximage;
            cur.deq = G3D_ATTR_DEPTH_EQ(attr) != 0;
            cur.mask = (a5 >= 31u) || G3D_ATTR_XLU_DEPTH(attr);
            cur.xlu_depth = G3D_ATTR_XLU_DEPTH(attr) != 0;
            cur.mode = mode == G3D_POLY_MODE_DECAL ? 1 : mode == G3D_POLY_MODE_TOON ? 2 : 0;
            cur.toon = mode == G3D_POLY_MODE_TOON;
            cur.a5 = (int)a5;
            cur.front = G3D_ATTR_FRONT(attr) != 0;
            cur.back = G3D_ATTR_BACK(attr) != 0;
            cur.textured = slot >= 0 && (snap->disp3dcnt & 1u);
            cur.slot = cur.textured ? slot : -1;
            cur.wrap = cur.textured ? (int)(G3D_TEX_REPEAT_S(ti) | (G3D_TEX_REPEAT_T(ti) << 1)
                                            | (G3D_TEX_FLIP_S(ti) << 2) | (G3D_TEX_FLIP_T(ti) << 3)) : 0;
            cur.id = (int)G3D_ATTR_ID(attr);
            cur.two_pass = a5 >= 31u && cur.textured && snap->tex[slot].partial;
            cur.fog = G3D_ATTR_FOG(attr) != 0;
            if (have && run_first + run_verts == first && memcmp(&run, &cur, sizeof run) == 0) {
                run_verts += verts;
            } else {
                if (have) {
                    draw_run(s, cmd, pass, snap, &run, run_first, run_verts, &u);
                }
                run = cur;
                run_first = first;
                run_verts = verts;
                have = 1;
            }
            first += verts;
        }
        if (have) {
            draw_run(s, cmd, pass, snap, &run, run_first, run_verts, &u);
        }
    }
    if (backdrop && snap->backdrop.n_poke > 0) {
        bd_draw(s, cmd, pass, &snap->backdrop, 1, w, h, cdepth);
    }
    SDL_EndGPURenderPass(pass);

    if (edges) {
        EdgeUniforms eu;
        int k, step = w / 256, variant = s->edge_mode == 1 ? 1 : 3;
        const char *e = getenv("EIDOS_EDGE"), *ew = getenv("EIDOS_EDGE_WIDTH");

        if (e != NULL && e[0] >= '0' && e[0] <= '7') {
            variant = e[0] - '0';
        }
        if (variant == 5) {
            step = (step + 1) / 2;
        }
        if (ew != NULL && atoi(ew) > 0) {
            step = atoi(ew) + 1;
        }
        memset(&eu, 0, sizeof eu);
        eu.p0[0] = step < 1 ? 1 : step;
        eu.p0[1] = variant;
        eu.p0[2] = (int)((snap->clear_color >> 24) & 0x3Fu);
        eu.p1[0] = cdepth;
        for (k = 0; k < 8; k++) {
            u32 c = snap->edge[k];

            eu.edge[k][0] = (float)c5to8(c) / 255.f;
            eu.edge[k][1] = (float)c5to8(c >> 5) / 255.f;
            eu.edge[k][2] = (float)c5to8(c >> 10) / 255.f;
        }
        post_pass(s, cmd, fb, s->p_edge, &eu, sizeof eu);
    }
    if (fog) {
        FogUniforms fu;
        u32 fc = snap->fog_color, ccl = snap->clear_color;
        u32 shift = (snap->disp3dcnt >> 8) & 0xFu;
        int k;

        memset(&fu, 0, sizeof fu);
        fu.fogc[0] = (float)c5to8(fc) / 255.f;
        fu.fogc[1] = (float)c5to8(fc >> 5) / 255.f;
        fu.fogc[2] = (float)c5to8(fc >> 10) / 255.f;
        fu.fogc[3] = (float)((fc >> 16) & 0x1Fu) / 31.f;
        fu.p0[0] = (float)(snap->fog_offset & 0x7FFFu);
        fu.p0[1] = shift > 10u ? 0.f : (float)(0x400u >> shift);
        fu.p0[2] = (snap->disp3dcnt & 0x40u) != 0 ? 1.f : 0.f;
        fu.clearc[0] = (float)c5to8(ccl) / 255.f;
        fu.clearc[1] = (float)c5to8(ccl >> 5) / 255.f;
        fu.clearc[2] = (float)c5to8(ccl >> 10) / 255.f;
        for (k = 0; k < 32; k++) {
            fu.table[k][0] = (float)snap->fog_table[k];
        }
        post_pass(s, cmd, fb, s->p_fog, &fu, sizeof fu);
    }
    eidos_prof_end(EIDOS_PROF_GLDRAW, t_dr);
    return 0;
}

/* With DISP3DCNT bit 4 (the hardware's edge anti-aliasing): the frame at
 * twice the size, then shrunk (plat_gl3d.c render_with_aa's way for the
 * drivers that cannot multisample their ID target).  Not on a phone, where
 * four times its pixels cost more than a frame's time. */
int sg3d_render(SdlGpu *s, SDL_GPUCommandBuffer *cmd, int fbi, const G3dSnapshot *snap, int w, int h)
{
    SgFb *fb = &s->fb[fbi];

    if (snap != NULL && (snap->disp3dcnt & 0x10u) != 0 && (long)w * h <= AA_MAX_PIXELS &&
        getenv("EIDOS_GL_ES") == NULL) {
        G3dSnapshot sample = *snap;
        SDL_GPUBlitInfo bi;

        sample.disp3dcnt &= ~0x10u;
        if (render_into(s, cmd, &s->fb[2], &sample, w * 2, h * 2) != 0 || fb_resize(s, fb, w, h) != 0) {
            return -1;
        }
        memset(&bi, 0, sizeof bi);
        bi.source.texture = s->fb[2].color;
        bi.source.w = (Uint32)(w * 2);
        bi.source.h = (Uint32)(h * 2);
        bi.destination.texture = fb->color;
        bi.destination.w = (Uint32)w;
        bi.destination.h = (Uint32)h;
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(cmd, &bi);
        return 0;
    }
    return render_into(s, cmd, fb, snap, w, h);
}

int sg3d_render3d(SdlGpu *s, const G3dSnapshot *snap, int w, int h)
{
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    int rc;

    if (cmd == NULL) {
        return -1;
    }
    rc = sg3d_render(s, cmd, 0, snap, w, h);
    SDL_SubmitGPUCommandBuffer(cmd);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Reading a frame back                                                */
/* ------------------------------------------------------------------ */

static unsigned char g_unpremul[256][256];
static int g_unpremul_ready;

/* Premultiplied to straight RGBA, rows as they are (top-down already). */
static void unpremultiply(const unsigned char *raw, unsigned char *out, int w, int h)
{
    int i, n = w * h;

    if (!g_unpremul_ready) {
        unsigned a, c;

        for (a = 1; a < 256u; a++) {
            for (c = 0; c < 256u; c++) {
                unsigned v = (c * 255u + a / 2u) / a;

                g_unpremul[a][c] = (unsigned char)(v > 255u ? 255u : v);
            }
        }
        g_unpremul_ready = 1;
    }
    for (i = 0; i < n; i++) {
        const unsigned char *p = raw + (size_t)i * 4u;
        unsigned char *d = out + (size_t)i * 4u;
        unsigned a = p[3];

        if (a == 255u) {
            d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 255;
        } else if (a == 0) {
            d[0] = d[1] = d[2] = d[3] = 0;
        } else {
            d[0] = g_unpremul[a][p[0]];
            d[1] = g_unpremul[a][p[1]];
            d[2] = g_unpremul[a][p[2]];
            d[3] = (unsigned char)a;
        }
    }
}

/* The download of fb's colour into `down`, in its own copy pass. */
static int start_read(SdlGpu *s, SDL_GPUCommandBuffer *cmd, const SgFb *fb, int w, int h)
{
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureRegion from;
    SDL_GPUTextureTransferInfo to;

    if (s->down == NULL) {
        SDL_GPUTransferBufferCreateInfo ti;

        memset(&ti, 0, sizeof ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = (Uint32)(w * h * 4);
        s->down = SDL_CreateGPUTransferBuffer(s->dev, &ti);
        if (s->down == NULL) {
            return -1;
        }
    }
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&from, 0, sizeof from);
    memset(&to, 0, sizeof to);
    from.texture = fb->color;
    from.w = (Uint32)w;
    from.h = (Uint32)h;
    from.d = 1;
    to.transfer_buffer = s->down;
    to.pixels_per_row = (Uint32)w;
    to.rows_per_layer = (Uint32)h;
    SDL_DownloadFromGPUTexture(cp, &from, &to);
    SDL_EndGPUCopyPass(cp);
    return 0;
}

static int finish_read(SdlGpu *s, unsigned char *out, int w, int h)
{
    const unsigned char *raw;

    if (s->down_fence != NULL) {
        SDL_WaitForGPUFences(s->dev, true, &s->down_fence, 1);
        SDL_ReleaseGPUFence(s->dev, s->down_fence);
        s->down_fence = NULL;
    }
    raw = (const unsigned char *)SDL_MapGPUTransferBuffer(s->dev, s->down, false);
    if (raw == NULL) {
        return -1;
    }
    unpremultiply(raw, out, w, h);
    SDL_UnmapGPUTransferBuffer(s->dev, s->down);
    return 0;
}

/* Test aid: the last 3D frame (fb[0]) read back, waiting for the GPU. */
int sg3d_read3d(SdlGpu *s, unsigned char *out, int w, int h)
{
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_GPUTransferBuffer *keep = s->down;
    int rc;

    if (out == NULL || s->fb[0].w != w || s->fb[0].h != h || s->fb[0].color == NULL) {
        return -1;
    }
    memset(&ti, 0, sizeof ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = (Uint32)(w * h * 4);
    s->down = SDL_CreateGPUTransferBuffer(s->dev, &ti);
    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (s->down == NULL || cmd == NULL || start_read(s, cmd, &s->fb[0], w, h) != 0) {
        if (cmd != NULL) SDL_CancelGPUCommandBuffer(cmd);
        if (s->down != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, s->down);
        s->down = keep;
        return -1;
    }
    s->down_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    rc = finish_read(s, out, w, h);
    SDL_ReleaseGPUTransferBuffer(s->dev, s->down);
    s->down = keep;
    return rc;
}

/* The frame at DS size (plat_gl3d.c eidos_gl_read3d_1x): 1 unchanged since
 * the last call; with `async` the picture of one call ago (the first call
 * returns 1), so the CPU never waits for this frame's GPU work. */
int sg3d_read3d_1x(SdlGpu *s, const G3dSnapshot *snap, unsigned char *out, int async)
{
    SDL_GPUCommandBuffer *cmd;
    int got = 1;

    if (!s->have3d || snap == NULL || out == NULL) {
        return -1;
    }
    if (s->read_valid && snap->serial == s->read_serial) {
        return 1;
    }
    if (async && s->down_pending) {
        got = finish_read(s, out, 256, 192) == 0 ? 0 : -1;
        s->down_pending = 0;
    }
    cmd = SDL_AcquireGPUCommandBuffer(s->dev);
    if (cmd == NULL || sg3d_render(s, cmd, 1, snap, 256, 192) != 0 ||
        start_read(s, cmd, &s->fb[1], 256, 192) != 0) {
        if (cmd != NULL) SDL_SubmitGPUCommandBuffer(cmd);
        s->read_valid = 0;
        return -1;
    }
    s->down_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (async) {
        s->down_pending = 1;
    } else {
        if (finish_read(s, out, 256, 192) != 0) {
            s->read_valid = 0;
            return -1;
        }
        got = 0;
    }
    s->read_serial = snap->serial;
    s->read_valid = 1;
    return got;
}

/* ------------------------------------------------------------------ */
/* Set-up                                                              */
/* ------------------------------------------------------------------ */

int sg3d_init(SdlGpu *s)
{
    static const SDL_GPUTextureFormat ds[2] = {
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT
    };
    int i;

    s->ds_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    for (i = 0; i < 2; i++) {
        if (SDL_GPUTextureSupportsFormat(s->dev, ds[i], SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            s->ds_format = ds[i];
            break;
        }
    }
    if (s->ds_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        return -1;
    }
    s->vs_g3d = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_VERTEX, g3d_vs, G3D_VS);
    s->fs_g3d = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, g3d_fs, G3D_FS);
    s->vs_edge = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_VERTEX, edge_vs, EDGE_VS);
    s->fs_edge = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, edge_fs, EDGE_FS);
    s->fs_fog = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, fog_fs, FOG_FS);
    s->vs_bd = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_VERTEX, backdrop_vs, BACKDROP_VS);
    s->fs_bd = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, backdrop_fs, BACKDROP_FS);
    s->fs_dclear = SG_SHADER(s->dev, SDL_GPU_SHADERSTAGE_FRAGMENT, depthclear_fs, DEPTHCLEAR_FS);
    if (s->vs_g3d == NULL || s->fs_g3d == NULL) {
        return -1;
    }
    if (s->vs_edge != NULL && s->fs_edge != NULL) {
        s->p_edge = make_post(s, s->fs_edge);
    }
    if (s->vs_edge != NULL && s->fs_fog != NULL) {
        s->p_fog = make_post(s, s->fs_fog);
    }
    if (s->fs_dclear != NULL) {
        s->p_dclear = make_dclear(s);
    }
    if (pipe_get(s, PK_GAME) == NULL) {
        return -1;
    }
    if (g_dummy == NULL) {
        g_dummy = sg_texture(s, SG_RGBA, 1, 1, 1, 0);
    }
    s->have3d = 1;
    g3d_hw_gpu_features((s->p_edge != NULL ? G3D_SNAP_EDGE : 0u) |
                        (s->p_fog != NULL ? G3D_SNAP_FOG : 0u));
    return 0;
}

void sg3d_destroy(SdlGpu *s)
{
    int i;
    unsigned k;
    SgBd *b = (SgBd *)s->bdrop;

    if (s->have3d) {
        g3d_hw_gpu_features(0u);
    }
    for (i = 0; i < 3; i++) {
        fb_free(s, &s->fb[i]);
    }
    for (i = 0; i < s->n_pipe; i++) {
        SDL_ReleaseGPUGraphicsPipeline(s->dev, s->pipe[i]);
    }
    s->n_pipe = 0;
    if (s->p_edge != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_edge);
    if (s->p_fog != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_fog);
    if (s->p_dclear != NULL) SDL_ReleaseGPUGraphicsPipeline(s->dev, s->p_dclear);
    if (s->vs_g3d != NULL) SDL_ReleaseGPUShader(s->dev, s->vs_g3d);
    if (s->fs_g3d != NULL) SDL_ReleaseGPUShader(s->dev, s->fs_g3d);
    if (s->vs_edge != NULL) SDL_ReleaseGPUShader(s->dev, s->vs_edge);
    if (s->fs_edge != NULL) SDL_ReleaseGPUShader(s->dev, s->fs_edge);
    if (s->fs_fog != NULL) SDL_ReleaseGPUShader(s->dev, s->fs_fog);
    if (s->vs_bd != NULL) SDL_ReleaseGPUShader(s->dev, s->vs_bd);
    if (s->fs_bd != NULL) SDL_ReleaseGPUShader(s->dev, s->fs_bd);
    if (s->fs_dclear != NULL) SDL_ReleaseGPUShader(s->dev, s->fs_dclear);
    for (k = 0; k < s->n_gtex; k++) {
        if (s->gtex[k] != NULL) SDL_ReleaseGPUTexture(s->dev, s->gtex[k]);
    }
    free(s->gtex);
    free(s->gtex_key);
    free(s->gtex_w);
    free(s->gtex_h);
    free(s->vtx);
    if (s->stage != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, s->stage);
    if (s->down != NULL) SDL_ReleaseGPUTransferBuffer(s->dev, s->down);
    if (s->down_fence != NULL) SDL_ReleaseGPUFence(s->dev, s->down_fence);
    buf_free(s, &s->vbuf);
    if (b != NULL) {
        bd_free_scene(s, b, 0);
        buf_free(s, &b->vbuf);
        buf_free(s, &b->ibuf);
        for (k = 0; k < 16; k++) {
            if (b->smp[k] != NULL) SDL_ReleaseGPUSampler(s->dev, b->smp[k]);
        }
        free(b);
        s->bdrop = NULL;
    }
    if (g_dummy != NULL) {
        SDL_ReleaseGPUTexture(s->dev, g_dummy);
        g_dummy = NULL;
    }
}
