/*
 * engine/g3d/hw3d.h -- the DS 3D hardware interface, driven from the DS
 * address space.
 *
 * engine/g3d's display-list interpreter and rasteriser draw what a parsed
 * NSBMD says.  Once the game's own code runs, the picture is what that code
 * writes into the 3D hardware: command words into the geometry FIFO at
 * 0x04000400 or the direct command ports 0x04000440..0x040005FF, the
 * control registers (DISP3DCNT, the clear colour and depth, the alpha-test
 * reference, the toon table), and texture images and palettes in the VRAM
 * banks VRAMCNT gives the texture slots.  engine/g3d/hw3d.c is that
 * hardware: it runs every command through the same interpreter, and at
 * SWAP_BUFFERS rasterises the frame into the RGBA buffer
 * engine/g2d/hwcompose.c takes as the 3D layer
 * (g2d_hw_render_dsmem(g3d_hw_frame(), ...)).  The file header of hw3d.c
 * says precisely which of the game's write paths reach it and which do not.
 */

#ifndef EIDOS_ENGINE_G3D_HW3D_H
#define EIDOS_ENGINE_G3D_HW3D_H

#include "g3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 3D hardware's register addresses this module owns or reads. */
#define G3D_HW_DISP3DCNT      0x04000060u
#define G3D_HW_EDGE_COLOR     0x04000330u   /* 8 x u16                     */
#define G3D_HW_ALPHA_TEST_REF 0x04000340u
#define G3D_HW_CLEAR_COLOR    0x04000350u
#define G3D_HW_CLEAR_DEPTH    0x04000354u
#define G3D_HW_CLRIMAGE_OFS   0x04000356u
#define G3D_HW_FOG_COLOR      0x04000358u
#define G3D_HW_FOG_OFFSET     0x0400035Cu
#define G3D_HW_FOG_TABLE      0x04000360u   /* 32 x u8                     */
#define G3D_HW_TOON_TABLE     0x04000380u   /* 32 x u16                    */
#define G3D_HW_GXFIFO         0x04000400u   /* ..0x0400043F, all mirrors   */
#define G3D_HW_PORTS          0x04000440u   /* direct ports, op * 4 + 0x400 */
#define G3D_HW_SWAP_BUFFERS   0x04000540u
#define G3D_HW_VIEWPORT       0x04000580u
#define G3D_HW_GXSTAT         0x04000600u
#define G3D_HW_RAM_COUNT      0x04000604u
#define G3D_HW_DISP_1DOT      0x04000610u
#define G3D_HW_POS_RESULT     0x04000620u   /* 4 x s32                     */
#define G3D_HW_VEC_RESULT     0x04000630u   /* 3 x s16                     */
#define G3D_HW_CLIPMTX_RESULT 0x04000640u   /* 16 x s32                    */
#define G3D_HW_VECMTX_RESULT  0x04000680u   /* 9 x s32                     */
#define G3D_HW_WINDOW_END     0x040006A4u   /* end of the hooked window    */

/* Register features the rasteriser does not implement, reported per frame
 * by g3d_hw_unsupported() when the game uses them.  Fog, edge marking,
 * anti-aliasing, the rear-plane bitmap, the w-buffer and lighting are
 * implemented (hw3d.c's header); their old flags 0x01..0x10 and 0x40 are
 * retired. */
#define G3D_HW_UNSUP_SHADOW     0x20u   /* a shadow polygon was drawn   */

/*
 * Power-on state, and the access hook over 0x04000400..0x040006A3.  Call it
 * after every bw_ds_reset (which clears access hooks); the boot does
 * bw_ds_reset, bw_os_reset, g3d_hw_reset, then the game.
 */
void g3d_hw_reset(void);

/* Commit the store the last access left pending.  bw_io_sync does this
 * through the access hook; port code that reads the 3D state between
 * synchronisation points calls it directly. */
void g3d_hw_flush(void);

/*
 * The paths into the FIFO that do not go through a translated pointer:
 * port code, the copy and DMA shims (sdk/g3_fifo.c, sdk/mi_gx.c), and a
 * future DMA engine whose destination is 0x04000400.  `fifo_send` reads
 * `bytes / 4` little-endian words from host memory.
 */
void g3d_hw_fifo_write(u32 word);
void g3d_hw_fifo_send(const void *src, u32 bytes);
/* One 32-bit store to `addr` in 0x04000400..0x040005FF. */
void g3d_hw_port_write(u32 addr, u32 value);

/*
 * The shims' test for "this destination is the geometry FIFO": a host
 * pointer into the register file at 0x04000400..0x0400043F (what a
 * rewritten `&reg_G3X_GXFIFO` is), or the raw DS value itself, which is
 * what a matched call site passes when its address is a macro or an
 * integer rather than a literal cast (docs/building.md: dsaddr's residue).  A shim
 * that answers yes calls g3d_hw_claim(dst) before sending, so the access
 * that produced the pointer is not mistaken for a store.
 */
int  g3d_hw_is_fifo(const volatile void *dst);
void g3d_hw_claim(const volatile void *dst);
/* As g3d_hw_is_fifo, for the read-only result window: returns the DS
 * address `src` names in 0x04000600..0x040006A3, or 0. */
u32  g3d_hw_result_addr(const volatile void *src);

/* Write every readable register (GXSTAT, RAM_COUNT, the test results, the
 * clip and vector matrices) into the register file. */
void g3d_hw_refresh(void);

/*
 * The last frame SWAP_BUFFERS produced: 256x192 RGBA in the byte order
 * engine/g2d composes, alpha 0 where nothing was drawn and the clear alpha
 * was 0, a translucent pixel's own alpha where it was drawn over that.
 * engine/g2d/hwcompose.c composes it with that alpha.  NULL before the
 * first swap.
 */
const u8 *g3d_hw_frame(void);
/*
 * Internal resolution.  g3d_hw_set_scale(N) makes every following SWAP_BUFFERS
 * render at (256*N) x (192*N): the viewport and screen coordinates scale
 * with the target (20.4 subpixels kept), texture, colour, depth, alpha,
 * fog, edge marking and anti-aliasing run per high-resolution pixel (edge
 * marking and anti-aliasing keep a one-pixel width, so thinner on screen).
 * g3d_hw_frame() then returns that buffer, g3d_hw_frame_scale() its N (a
 * frame already rendered keeps its own N until the next swap).  The default
 * is 1, bit-identical to the 256x192 output.  Pass the N the 2D side uses
 * (g2d_hw_render_dsmem_scaled).  The setting survives g3d_hw_reset.
 */
void g3d_hw_set_scale(u32 scale);
u32  g3d_hw_scale(void);
/*
 * The view scale (g3d_mtx_set_view_scale): kx and ky are fx32 factors on
 * the clip matrix's x and y, so the same widening reaches the vertices the
 * snapshot carries and the game's BOX_TEST / POS_TEST culling.  The default
 * is FX32_ONE for both, bit-identical to the DS.  Like g3d_hw_set_scale it
 * survives g3d_hw_reset, and it takes effect from the next vertex or test
 * the game sends (a command already queued keeps the matrix it was made
 * with, so a change shows from the next frame).
 */
void g3d_hw_set_view_scale(s32 kx, s32 ky);
void g3d_hw_view_scale(s32 *kx, s32 *ky);
u32  g3d_hw_frame_scale(void);
u32       g3d_hw_frame_count(void);

/*
 * The frame as a GPU renderer needs it.  With the snapshot enabled, every
 * SWAP_BUFFERS also copies what the rasteriser was given (the polygons after
 * the per-frame attribute fix-ups, their vertices and decoded textures, and
 * the register state that shades them) into a buffer this module owns.  The
 * copy is made on the game's thread; the renderer reads it later on the
 * thread that owns the graphics context.  The CPU rasteriser still renders
 * the frame at g3d_hw_scale() as usual (the capture and the 2D layer
 * priorities need it).  NULL before the first swap or while disabled.
 */
typedef struct G3dSnapTex {
    u32 w, h;
    u8 *rgba;               /* owned copy, w*h*4 */
    u8 partial;             /* some texel's 5-bit alpha is 1..30          */
    u64 content;            /* what the texels were decoded from (hash),
                             * the same from frame to frame while the
                             * texture is; 0 when unknown              */
} G3dSnapTex;

/*
 * A scene a GPU renderer draws behind the game's own polygons (the
 * one-screen layout's battle backdrop, sdk/battle_view.c).  The camera and
 * the projection are the game's own at the frame, in the units its vertices
 * use; `scene` is an immutable placed scene (engine/world/bmap.h) that the
 * producer keeps alive for at least two battles, identified by `scene_gen`
 * (the renderer uploads it when the number changes).  `valid` is 0 on every
 * frame that has no backdrop.
 */
typedef struct G3dBackdrop {
    u32 valid;
    float eye[3], target[3], up[3];
    float fovy_sin, fovy_cos, aspect, znear, zfar;
    const void *scene;      /* a BmapScene, or NULL: only the sky colour   */
    u32 scene_gen;
    u8  sky[3];
    /* the Pokemon and the trainers on the field (battle space: x, y, z and
     * how tall the sprite's box is, its half-width a 0.47th of that): the map
     * in front of one is drawn again over it, the sprites being drawn
     * without the map's depth (what stands behind one never covers it, a
     * wall it reaches into included; a negative height: nothing is drawn
     * over it at all, the player's own trainer) */
#define G3D_BACKDROP_SPRITES 32
    float poke[G3D_BACKDROP_SPRITES][4];
    u32 n_poke;
    /* the spot editor (tools/spot-editor): draw only what stands in front of
     * the Pokemon, over a transparent clear, to lay over its own sprites */
    u32 front_only;
} G3dBackdrop;

/* Called on the game's thread while a snapshot is built; fills `out`
 * (already zeroed).  NULL (the default) means no backdrop. */
typedef void (*G3dBackdropHook)(G3dBackdrop *out);
void g3d_hw_set_backdrop_hook(G3dBackdropHook hook);
/* From a backdrop hook only (the snapshot is being built): one more
 * polygon after the game's, a quad with an image of its own, drawn like
 * the game's (so the map in front of a Pokemon's box covers it too).  The
 * corners are clip coordinates as the game's vertices carry them (fx32 x,
 * y, z, w), top left, top right, bottom right, bottom left; the RGBA image
 * (w x h, alpha 0 or 255) is copied.  0 when added. */
int g3d_hw_snapshot_add_quad(const s32 clip[4][4], const u8 *rgba, u32 w, u32 h);

typedef struct G3dSnapshot {
    u32 serial;             /* g3d_hw_frame_count() when it was taken     */
    G3dVertex *verts;  u32 n_verts;
    u32 *idx;          u32 n_idx;
    G3dPoly *polys;    u32 n_polys;
    s32 *poly_tex;          /* per polygon: index into tex, -1 untextured  */
    G3dSnapTex *tex;   u32 n_tex;
    u32 cap_verts, cap_idx, cap_polys, cap_tex;
    u32 disp3dcnt;
    u32 wbuffer;            /* depth is w                                 */
    u32 alpha_ref;          /* 0 when the alpha test is off               */
    u32 highlight;          /* DISP3DCNT bit 1                            */
    u32 clear_color;        /* CLEAR_COLOR                                */
    u32 clear_depth;        /* CLEAR_DEPTH, 15 bits                       */
    u32 unsupported;        /* G3D_SNAP_* the GPU path cannot honour      */
    u8  toon[32 * 3];       /* R,G,B per entry                            */
    u16 edge[8];            /* EDGE_COLOR, BGR555, one per 8 polygon IDs  */
    u32 fog_color;          /* FOG_COLOR: BGR555, alpha in bits 16-20     */
    u32 fog_offset;         /* FOG_OFFSET, 15 bits                        */
    u8  fog_table[32];      /* FOG_TABLE, 7-bit densities                 */
    G3dBackdrop backdrop;   /* what to draw behind the polygons           */
} G3dSnapshot;

/* Frame features the GPU renderer does not do yet (a frame using one is
 * shown from the CPU rasteriser instead). */
#define G3D_SNAP_EDGE    0x1u   /* edge marking                        */
#define G3D_SNAP_FOG     0x2u   /* fog                                 */
#define G3D_SNAP_REARBMP 0x4u   /* rear-plane bitmap                   */
#define G3D_SNAP_SHADOW  0x8u   /* shadow polygons                     */

/* The G3D_SNAP_* features the GPU renderer draws itself (its platform
 * layer says so once its shaders are up).  A frame that uses a feature
 * outside this set is marked unsupported and drawn by the CPU rasteriser. */
void g3d_hw_gpu_features(u32 snap_mask);

/* With the snapshot on, skip the CPU rasteriser for the following frames: a
 * GPU renderer draws them from the snapshot and g3d_hw_frame() goes stale.
 * Frames the snapshot marks unsupported (G3D_SNAP_*) need it back. */
void g3d_hw_skip_raster(int on);
void g3d_hw_snapshot_enable(int on);
const G3dSnapshot *g3d_hw_snapshot(void);
/* Called on the game's thread when a snapshot is complete (after the
 * backdrop hook): a reader of the frame's polygons, such as the one-screen
 * battle finding where each Pokemon's sprite is.  NULL (the default): none. */
typedef void (*G3dSnapshotHook)(const G3dSnapshot *s);
void g3d_hw_set_snapshot_hook(G3dSnapshotHook hook);
u32       g3d_hw_unsupported(void);

/* GXSTAT as the hardware holds it, bit 15 (the matrix stack error) included;
 * the register file's copy reads that bit as 0 (see hw3d.c). */
u32 g3d_hw_gxstat(void);

/* Diagnostics: g3d_hw_trace(1) starts collecting the command stream
 * (commands per opcode, the write path of each word, vertices, polygons,
 * what the rasteriser rejected, textures bound, matrices, viewport,
 * DISP3DCNT) and prints one summary to stderr at each SWAP_BUFFERS while on;
 * g3d_hw_trace(0) stops. */
void g3d_hw_trace(int on);

/* The interpreter state, for tests and tools. */
const G3dGx *g3d_hw_gx(void);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_G3D_HW3D_H */
