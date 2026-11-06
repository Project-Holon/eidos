/*
 * engine/platform/plat_sdlgpu_int.h -- the SDL3 GPU backend's own state,
 * shared by its presenter (plat_sdlgpu.c) and its 3D renderer
 * (plat_sdlgpu3d.c).  The backend as a whole is described in plat_sdlgpu.c.
 */
#ifndef EIDOS_PLAT_SDLGPU_INT_H
#define EIDOS_PLAT_SDLGPU_INT_H

#include <SDL3/SDL.h>

#include "plat_gpu.h"

/* Every colour target the backend draws to and every picture it shows. */
#define SG_RGBA SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
/* The 3D frame's polygon IDs (the value over 255) and depth copy. */
#define SG_ID SDL_GPU_TEXTUREFORMAT_R8_UNORM
#define SG_DEPTHCOPY SDL_GPU_TEXTUREFORMAT_R32_FLOAT

/* A picture the CPU makes and the GPU shows: its texture, its size and the
 * transfer buffer it goes up through. */
typedef struct SgPic {
    SDL_GPUTexture *tex;
    SDL_GPUTransferBuffer *up;
    int w, h;                   /* the texture's size, 0: none           */
    int up_size;                /* the transfer buffer's bytes           */
    int dirty;                  /* new pixels wait in `up`               */
    int target;                 /* the texture is a colour target too: the
                                   2D compositor draws into it            */
} SgPic;

/* A 3D frame's targets (plat_gl3d.c's GlFb): the premultiplied colour, the
 * second colour the edge and fog passes write into (then swapped), the
 * polygon IDs, the depth copy and the depth-stencil. */
typedef struct SgFb {
    SDL_GPUTexture *color, *color2, *id, *depth, *ds;
    int w, h;
} SgFb;

/* A growing GPU buffer and the transfer buffer that fills it. */
typedef struct SgBuf {
    SDL_GPUBuffer *buf;
    SDL_GPUTransferBuffer *up;
    Uint32 size;
} SgBuf;

#define SG_PIPES 64

typedef struct SdlGpu {
    EidosGpu base;              /* first: the backend's head (plat_gpu.h) */
    SDL_Window *window;
    SDL_GPUDevice *dev;
    char driver[32];
    SDL_GPUTextureFormat ds_format;

    /* the presenter */
    SDL_GPUShader *vs;          /* present.hlsl's: a quad in a rectangle */
    SDL_GPUGraphicsPipeline *p_present, *p_blur, *p_canvas, *p_comp3d, *p_world, *p_capture;
    SDL_GPUSampler *linear, *nearest;
    SgPic screen[2], canvas, panel;
    SDL_GPUTexture *scene, *blur[2], *frozen;
    int scene_w, scene_h, blur_w, blur_h, blur1_w, blur1_h, frozen_w, frozen_h;

    /* the one-screen layout (plat_gl.c's g_one) */
    int on, world, which, hide2d, crop, blur_amount, no_fade, hold, keep, spare, dark, mix;
    int under;                  /* beyond the DS screen: the kept picture  */
    int frozen_ok;
    SDL_Rect ds;                /* the DS screen's rectangle             */
    SDL_Rect crect;             /* the canvas's rectangle, w 0: none     */
    int bottom;                 /* window pixels the world leaves free   */
    float bd[3];                /* the backdrop beyond the DS screen     */
    SDL_Rect prect;             /* the panel's rectangle, w 0: none      */

    /* the 3D layer over a screen (plat_gl.c's s3d and its pictures) */
    int s3d, bmode, dilate, has_zero;
    float bfac;
    SgPic behind, zero;
    /* the 2D compositor's composites (plat_gl.c's s3d2, s3d_from2d): the
     * screen whose 3D the 2D shader composes and the size it is shown at;
     * s3d's screens made by the shader (the one-screen world) */
    int s3d2, s3d2_w, s3d2_h, s3d_from2d;
    void *g2d;                  /* the 2D compositor (plat_sdlgpu2d.c)   */

    /* the display capture */
    SgPic cap_src[3];
    SDL_GPUTexture *capa, *cap_tex[2];
    int cap_w, cap_h, cap_cur, cap_valid, cap_screen, cap_bmode;
    float cap_bfac;

    /* the 3D renderer (plat_sdlgpu3d.c) */
    int have3d, edge_mode;
    SgFb fb[3];                 /* the window's size, DS size, 2x for AA */
    SDL_GPUShader *vs_g3d, *fs_g3d, *vs_edge, *fs_edge, *fs_fog, *vs_bd, *fs_bd, *fs_dclear;
    SDL_GPUGraphicsPipeline *p_edge, *p_fog, *p_dclear;
    Uint32 pipe_key[SG_PIPES];
    SDL_GPUGraphicsPipeline *pipe[SG_PIPES];
    int n_pipe;
    SDL_GPUTexture **gtex;      /* one per snapshot texture              */
    unsigned *gtex_key;
    int *gtex_w, *gtex_h;
    unsigned n_gtex, cap_gtex;
    unsigned tex_serial;
    int tex_serial_valid;
    SDL_GPUTransferBuffer *stage;   /* the frame's texture uploads       */
    Uint32 stage_size;
    SgBuf vbuf;                 /* the polygons' vertices                */
    float *vtx;
    size_t cap_vtx;
    /* the frame at DS size read back (read3d_1x) */
    SDL_GPUTransferBuffer *down;
    SDL_GPUFence *down_fence;
    int down_pending, read_valid;
    unsigned read_serial;
    void *bdrop;                /* the battle backdrop (plat_sdlgpu3d.c)  */
} SdlGpu;

/* plat_sdlgpu.c */
SDL_GPUTexture *sg_texture(SdlGpu *s, SDL_GPUTextureFormat f, int w, int h, int levels, int target);
void sg_ensure_target(SdlGpu *s, SDL_GPUTexture **tex, int *tw, int *th, int w, int h);
void sg_pic_set(SdlGpu *s, SgPic *p, const unsigned char *rgba, int w, int h);
void sg_pic_flush(SDL_GPUCopyPass *cp, SgPic *p);
int sg_pic_target(SdlGpu *s, SgPic *p, int w, int h);
void sg_free_pic(SDL_GPUDevice *dev, SgPic *p);
SDL_GPUShader *sg_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
                         const unsigned char *dxil, size_t dxil_n,
                         const unsigned char *spirv, size_t spirv_n,
                         const unsigned char *msl, size_t msl_n,
                         const char *entry, int samplers, int uniforms);
#define SG_SHADER(dev, stage, name, NAME) \
    sg_shader(dev, stage, name##_dxil, sizeof name##_dxil, name##_spirv, sizeof name##_spirv, \
              name##_msl, sizeof name##_msl, NAME##_ENTRY, NAME##_SAMPLERS, NAME##_UNIFORMS)

/* plat_sdlgpu3d.c */
int sg3d_init(SdlGpu *s);
void sg3d_destroy(SdlGpu *s);
int sg3d_render(SdlGpu *s, SDL_GPUCommandBuffer *cmd, int fbi, const struct G3dSnapshot *snap,
                int w, int h);
int sg3d_render3d(SdlGpu *s, const struct G3dSnapshot *snap, int w, int h);
int sg3d_read3d(SdlGpu *s, unsigned char *out, int w, int h);
int sg3d_read3d_1x(SdlGpu *s, const struct G3dSnapshot *snap, unsigned char *out, int async);

/* plat_sdlgpu2d.c */
struct G2dGpuFrame;
int sg2d_ready(SdlGpu *s);
int sg2d_compose(SdlGpu *s, const struct G2dGpuFrame *f, unsigned char *readback, int to_screen);
int sg2d_pixels(SdlGpu *s, int screen, unsigned char *out);
void sg2d_destroy(SdlGpu *s);

#endif
