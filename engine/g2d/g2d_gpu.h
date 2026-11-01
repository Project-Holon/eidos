/*
 * engine/g2d/g2d_gpu.h -- one engine's frame as the GPU compositor takes it
 * (docs/gpu-2d.md).
 *
 * The CPU builds the layer descriptions as always (hwcompose.c); when a
 * frame is asked for, compose_graphics also copies what the per-pixel
 * compose reads into a G2dGpuFrame: the BG VRAM window, the palettes, the
 * BG parameters, the rasters, the OBJ line buffer, the 3D layer, the
 * windows, the colour effects and the master brightness.  The GPU shader
 * (engine/platform/plat_gl2d.c) then does what bg.c's compose_px does, per
 * pixel, with the same integer rules.
 */

#ifndef EIDOS_G2D_GPU_H
#define EIDOS_G2D_GPU_H

#include "g2d.h"

typedef struct G2dGpuText {
    int enabled;
    u32 priority;
    s32 hofs, vofs;
    u32 tiles_wide, tiles_high;   /* 32 or 64                              */
    u32 size_code;                /* G2D_BG_SIZE_*: the VRAM entry order    */
    u32 map_ofs;                  /* in the BG window                       */
    u32 char_ofs;
    int bpp8;
} G2dGpuText;

typedef struct G2dGpuAffine {
    int enabled;
    u32 priority, bg_index;
    u32 tiles;                    /* map side in tiles                      */
    u32 map_ofs, char_ofs;
    int wrap;
    s32 ref_x, ref_y;             /* sign-extended 20.8                     */
    s32 pa, pb, pc, pd;
} G2dGpuAffine;

typedef struct G2dGpuRaster {
    int enabled;
    u32 priority, bg_index;
    u8 *rgba;                     /* 256 x 192 x 4, owned                   */
} G2dGpuRaster;

typedef struct G2dGpuFrame {
    int valid;                    /* filled by the last compose             */
    int screen;                   /* 0 top, 1 bottom                        */
    u8 *bgv;                      /* the BG window, owned                   */
    u32 bgv_size;                 /* bg_size + the guard                    */
    u32 bgv_cap;
    u32 bgv_gen;                  /* changes when bgv's bytes change: the
                                     GPU sends the window only then         */
    u8 pal[18][256][3];           /* BG, OBJ, the 16 OBJ extended slots     */
    u32 pal_count[18];
    G2dGpuText text[4];
    G2dGpuAffine affine[2];
    G2dGpuRaster raster[4];
    int obj_on;
    u8 *obj;                      /* 256 x 192 x 4: index, priority, source,
                                     bit 0 semi + bit 1 OBJ window          */
    int l3_on;
    u32 l3_priority, l3_bg_index;
    s32 l3_hofs;
    u8 *l3;                       /* 256 x 192 x 4, owned                   */
    G2dWindows windows;           /* obj_mask points nowhere: see obj       */
    G2dBlend blend;
    u32 master_bright;            /* MASTER_BRIGHT                          */
} G2dGpuFrame;

/* Ask the next display compose of each engine (slot 0 main, 1 sub) to fill
 * its frame; g2d_gpu_frame gives it back.  Off by default. */
void g2d_gpu_export(int on);
const G2dGpuFrame *g2d_gpu_frame(int slot);

/* Phase 4: with `on`, a screen whose frame the GPU composes is not also
 * composed on the CPU (its pixels in the caller's buffer stay as they
 * were).  The CPU still composes a screen that shows the 3D layer, one
 * whose 6-bit stage is read (the display capture), a frame with the
 * capture on, any frame the GPU cannot take, and every render but the
 * display's own (g2d_hw_render_dsmem_adaptive_ex: a screenshot composes).  g2d_gpu_cpu_skipped
 * says whether the last compose of a slot left its pixels out. */
void g2d_gpu_skip_cpu(int on);
int g2d_gpu_cpu_skipped(int slot);
/* Phase 3: the main engine's 3D layer comes from the GPU's own 3D frame
 * (the shader composes it), so a main screen showing the 3D can be left
 * to the GPU too, and its 3D layer is not copied. */
void g2d_gpu_main_3d(int on);

#endif
