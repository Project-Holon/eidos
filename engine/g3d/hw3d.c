/*
 * engine/g3d/hw3d.c -- the DS 3D hardware, driven from the DS address space.
 *
 * engine/g2d/hwcompose.c draws both screens from what the game wrote into
 * the 2D registers, VRAM, palette RAM and OAM.  This file is the same idea
 * for the 3D engine: the geometry command FIFO and the direct command ports,
 * the 3D registers, and the texture slots of VRAM, feeding the display-list
 * interpreter (gx.c) and the rasteriser (raster.c) that already exist.  At
 * SWAP_BUFFERS it renders the frame into a 256x192 RGBA buffer, which is the
 * `rgba3d` argument engine/g2d/hwcompose.c composes as BG0.
 *
 * The register layouts and command semantics are public NDS hardware
 * documentation.  Nothing here is derived from any other decompilation,
 * port or emulator.
 *
 * ONE INTERPRETER.  Every command, whichever way it arrived, is framed as a
 * one-command display list (the opcode in a packed word's low byte, then its
 * parameter words) and handed to g3d_gx_run.  So the FIFO and an NSBMD
 * display list are decoded by literally the same code, and the test that
 * pushes a list through the hardware path and compares the frame with the
 * interpreter's own rendering of it is a comparison of the transport, which
 * is the part this file adds.  What the hardware does that a static display
 * list never needs is done here, around the interpreter:
 *
 *   SWAP_BUFFERS, VIEWPORT, BOX_TEST, POS_TEST, VEC_TEST   here entirely;
 *   MTX_PUSH / MTX_POP in projection mode   the one-bit projection stack
 *       pointer GXSTAT bit 13 reports (mtx.c keeps the saved matrix but not
 *       the bit);
 *   MTX_POP with a count of 0 or a negative count   the hardware moves the
 *       pointer by the signed count and reloads from the new slot; mtx.c
 *       refuses those (no NSBMD list uses them), and G3X_InitMtxStack issues
 *       MTX_POP with the current level, which is 0 on a fresh stack;
 *   BEGIN_VTXS with a list still open   END_VTXS is a no-op on hardware, so
 *       a new BEGIN (or the swap) closes the open list and its polygons are
 *       drawn; the interpreter drops an unterminated list, because the asset
 *       lane's decoder does and the OBJ oracle depends on it.
 *
 * HOW A STORE REACHES THE FIFO.  The register file is plain memory
 * (sdk/dsmem.h): a matched `reg_G3X_GXFIFO = w` is a host store into
 * g_io[0x400], and ten stores in a row leave only the last.  Write hooks
 * cannot help, they compare against the last sync.  What tools/dsaddr.py's
 * rewrite does give is one bw_ds_ptr call per access, because every literal
 * cast becomes its own call, and the call happens before the store.  So
 * this file installs an *access hook* (sdk/dsmem_ext.h) over
 * 0x04000400..0x040006A3, and the protocol is:
 *
 *   1. bw_ds_ptr(A) calls the hook before returning.  The hook first
 *      resolves the access before it (step 3), then, for a command port A,
 *      writes a sentinel into A's word and remembers A as pending, and
 *      returns; the matched code then stores through the pointer.
 *   2. For A in the status/result window (0x04000600 up), the hook writes
 *      the live register values first, so the read that follows sees them.
 *      GXSTAT is also remembered as pending, because it is writable.
 *   3. The pending access is resolved by the next translation of any
 *      address in the window, by bw_io_sync (the access hook runs at the top
 *      of every sync), by g3d_hw_flush, and by every entry point below.  A
 *      pending port whose word no longer holds the sentinel was stored to,
 *      and the stored word is the command data; a pending GXSTAT whose value
 *      changed was written.
 *
 * The sentinel is G3D_HW_SENTINEL.  A store of exactly that value is
 * indistinguishable from no store and is dropped; as a command word it
 * names opcodes 0x00/0x5E/0xED/0xFA... none of which exist, and as a
 * parameter it is a coordinate near 400000.0, so a real command stream
 * does not produce it.  That is the one heuristic in the protocol, and it
 * buys the property that an address taken without a store (a pointer handed
 * to a copy routine, a probe) is never mistaken for a command.
 *
 * WHICH WRITE PATHS ARE CAPTURED, against aletheia's matched corpus (every
 * literal 0x040004xx/0x040005xx/0x040006xx in src/matched, 90 files):
 *
 *   captured, through the access hook: every store through a rewritten
 *     literal cast, which is every G3_* / reg_G3_* register macro the SDK's
 *     g3imm/g3x/g3_util objects use (G3X_Init, G3X_Reset, G3X_InitMtxStack,
 *     G3X_ResetMtxStack, G3X_SetFog, G3X_SetClearColor, G3X_InitTable,
 *     G3i_PerspectiveW_, G3i_FrustumW_, G3i_LookAt_, G3_RotX/Y/Z,
 *     G3_MultTransMtx33, G3_LoadMtx43/44, G3_MultMtx33/43/44's opcode
 *     word), and the game's own inline FIFO writers (func_0206C524,
 *     func_0206C82C, func_0206DDA4 and the overlay functions that write
 *     reg_G3_* directly).  Also every read of GXSTAT, RAM_COUNT and the
 *     test and matrix results through a literal cast;
 *   captured, through a shim: the hand-written ARM copy loops whose
 *     destination is the FIFO.  They are `asm` in aletheia and cannot be
 *     compiled for a host anyway: GX_SendFifo48B (0x02080C84),
 *     GX_SendFifo64B (0x02080CA8), GXi_NopClearFifo128_ (0x0207DECC) in
 *     sdk/g3_fifo.c; MIi_CpuSend32 (0x02082AA8), MI_Copy36B (0x02082B60)
 *     and MI_Copy64B (0x02082BA0, which G3X_GetClipMtx reads the result
 *     window with) in sdk/mi_gx.c, each FIFO-aware and a plain copy
 *     otherwise;
 *   captured, through a shim: the DMA-to-FIFO senders, MI_SendGXCommandAsync
 *     (0x02082760), MI_SendGXCommandAsyncFast (0x02082960) and the two TWL
 *     NDMA forms the game calls by address (0x027300F0, 0x0272FEF4), in
 *     sdk/mi_gx.c: the list is pushed through g3d_hw_fifo_send and the
 *     completion callback runs before the call returns.  A DMA engine the
 *     port grows later routes a transfer whose destination is 0x04000400 to
 *     g3d_hw_fifo_send; MIi_FIFOCallback (0x0208284C) and the DMA register
 *     path it drives are not used;
 *   NOT captured: a store through a pointer the code computed once and then
 *     stored through repeatedly (`vu32 *p = &FIFO; *p = a; *p = b;`): only
 *     the last store of the run is seen.  No matched source in the corpus
 *     does this; the SDK's macros re-evaluate the cast on every use;
 *   NOT captured: a store whose right-hand side itself touches the 3D
 *     window (`FIFO = f()` where f writes the FIFO), if the compiler
 *     evaluates the pointer before calling f: the inner access resolves the
 *     outer one before its store lands.  None in the corpus;
 *   NOT captured: port code storing through bw_io_write32 or bw_io(): it
 *     has to call g3d_hw_port_write / g3d_hw_fifo_write;
 *   NOT captured: sdk/mi.c's MI_CpuCopy/MI_CpuFill family with the FIFO as
 *     the destination, which writes consecutive register-file bytes.  No
 *     matched source sends to the FIFO through them;
 *   NOT captured: 8-bit and 16-bit stores to a command port; the hardware
 *     takes 32-bit stores and so does this.
 *
 * TIMING.  The geometry engine here is infinitely fast: a command executes
 * when its last parameter is resolved.  So GXSTAT always reads FIFO empty
 * (bit 26) and less than half full (bit 25), 0 entries, engine and test and
 * matrix-stack busy all clear, which is what makes every SDK busy-wait
 * (`while (GXSTAT & 0x08000000)`) fall straight through.  The GXFIFO
 * interrupt (IF bit 21) is never raised: with a FIFO that is always empty
 * the level-triggered condition would hold forever, and the one SDK user of
 * it, MI_SendGXCommandAsync, is shimmed.  SWAP_BUFFERS renders immediately
 * and the frame is visible from then on; the hardware's one-frame latency
 * (the swap waits for vblank, the frame is displayed during the next) is not
 * modelled.
 *
 * GXSTAT IN THE REGISTER FILE reads bit 15 (matrix stack error) as 0.  The
 * bit is write-one-to-acknowledge, and the SDK acknowledges with
 * `GXSTAT |= 0x8000`: through plain memory that store is only visible when
 * it changes the value, which it would not if the bit read 1.  Reading 0
 * makes every acknowledgement visible and every write of 0 (G3X_Init's
 * `GXSTAT = 0`, the FIFO-interrupt field updates) correctly not one.  No
 * matched source reads bit 15; g3d_hw_gxstat() returns the true value.
 *
 * WHAT THE FRAME HONOURS.  DISP3DCNT bit 0 (texturing; off draws every
 * polygon untextured), bit 1 (toon or highlight for polygon mode 2, with
 * the TOON_TABLE registers), bit 2 with ALPHA_TEST_REF (the alpha test),
 * bit 3 (alpha blending; off draws a translucent polygon opaque, [AP] for
 * the texture's own alpha, which still blends), bit 4 (anti-aliasing,
 * raster.c's approximation), bit 5 with EDGE_COLOR (edge marking), bit 7
 * with bit 6, bits 8-11, FOG_COLOR, FOG_OFFSET and FOG_TABLE (fog), bit 14
 * with CLRIMAGE_OFFSET (the rear-plane bitmap: colour from texture slot 2,
 * depth and fog flag from slot 3, scrolled), CLEAR_COLOR (colour, alpha,
 * the fog bit and the polygon ID the edge marking compares against),
 * CLEAR_DEPTH (scaled onto the rasteriser's 23-bit depth), SWAP_BUFFERS
 * bit 1 (the w-buffer), VIEWPORT (folded into the clip-space x/y of each
 * vertex emitted under it, so the rasteriser's fixed full-screen transform
 * lands it in the viewport; [AP] a polygon is clipped to the screen, not
 * to the viewport rectangle), and the lighting: DIF_AMB, SPE_EMI,
 * LIGHT_VECTOR, LIGHT_COLOR and SHININESS feed engine/g3d/light.c, and a
 * NORMAL under a POLYGON_ATTR that enables a light sets the vertex colour
 * to the lighting equation's result (DIF_AMB bit 15 sets it to the
 * diffuse colour).  The frame's alpha is the hardware's: alpha 0 where
 * nothing was drawn over a clear alpha of 0, a translucent pixel's own
 * alpha there, so the 2D engine sees through and blends with the 3D layer
 * as it does on hardware.  Not honoured, and reported per frame through
 * g3d_hw_unsupported(): shadow polygons (no matched source draws one: the
 * POLYGON_ATTR constants in the corpus all have mode 0).
 * DISP_1DOT_DEPTH is stored and not applied.  The hardware's
 * 2048-polygon / 6144-vertex limits are not imposed (modding: docs/boot.md);
 * RAM_COUNT reports the true counts saturated to the register's fields.
 *
 * TEXTURES come out of the texture-image and texture-palette slots through
 * bw_vram_slot (sdk/dsmem_ext.h), so they follow VRAMCNT exactly as the
 * hardware's texture unit does: TEXIMAGE_PARAM's address (8-byte units),
 * size and format, PLTT_BASE (16-byte units, 8 for the 4-colour format),
 * and for the 4x4-compressed format the index data in slot 1 at
 * 0x20000 + (offset & 0x1FFFF) / 2 (+ 0x10000 for an image in slot 2).  An
 * unmapped piece reads as zero.  Each distinct (image, palette) pair is
 * decoded once per frame with texture.c's decoder.
 *
 * The register file is little-endian, as sdk/dsmem.c keeps it; every access
 * here goes through bw_io_read32 / bw_io_write32.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmem.h"
#include "dsmem_ext.h"
#include "hw3d.h"
#include "prof.h"

#define G3D_HW_SENTINEL 0xFAED5E00u

#define FULL_VIEWPORT 0xBFFF0000u    /* x1 0, y1 0, x2 255, y2 191 */

#define STAT_BOX_RESULT   0x00000002u
#define STAT_PJ_LEVEL     0x00002000u
#define STAT_LESS_HALF    0x02000000u
#define STAT_EMPTY        0x04000000u

/* The --g3d-trace diagnostics (g3d_hw_trace). */
typedef struct Hw3dTrace {
    int on;
    u32 op_count[256];
    u32 w_fifo, w_port, w_send, w_hook, w_ext;   /* words by write path */
    u32 tex_polys[8];                            /* polygons by texture format */
    u32 tex_missing;                             /* textured, no decode */
} Hw3dTrace;
static Hw3dTrace g_tr;

typedef struct TexCacheEntry {
    u32 key_tex, key_pal;
    int ok;
    int borrowed;             /* tex.rgba belongs to the persistent store */
    u64 hash;                 /* the decode's source bytes, hashed        */
    G3dTexture tex;
} TexCacheEntry;

typedef struct Hw3d {
    int ready;

    G3dGx   gx;
    G3dGeom geom;

    /* the packed FIFO's framing */
    int in_packet;
    u8  ops[4];
    u32 slot, need, have;
    u32 par[32];

    /* the direct ports' framing */
    u32 d_op, d_have;
    u32 d_par[32];

    /* the access protocol */
    u32 pending;              /* DS address, 0 = none */
    u32 stat_published;

    /* status the interpreter does not keep */
    u32 pj_level;
    int pj_error;
    u32 box_result;
    u32 fifo_irq;             /* GXSTAT bits 30-31 */

    u32 viewport;
    u32 vtx_adjusted;
    u32 swap_param;

    /* the frame */
    G3dTarget front;
    int  have_frame;
    u32  frames;
    u32  unsup, unsup_frame;
    u32  disp3dcnt;           /* latched for the frame being rendered */
    u8   toon[32 * 3];

    G3dLight light;

    TexCacheEntry *cache;
    u32 n_cache, cap_cache;
} Hw3d;

static Hw3d g_hw;
/* EIDOS_G3D_BADMTX, read once */
static int badmtx_on(void)
{
    static int on = -1;

    if (on < 0) {
        on = getenv("EIDOS_G3D_BADMTX") != NULL;
    }
    return on;
}

static unsigned long long g_ring[64];   /* the last FIFO words (EIDOS_G3D_BADMTX) */
static u32 g_ring_n, g_ring_src;
static u32 g_mtx_log = 0xFFFFFFFFu;   /* matrix commands logged since the armed swap */
static u32 g_ops_frame[256];     /* commands since the last swap (EIDOS_G3D_OPS) */

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static s32 sx16(u32 v)
{
    v &= 0xFFFFu;
    return (v & 0x8000u) ? (s32)v - 0x10000 : (s32)v;
}

static s32 sx10(u32 v)
{
    v &= 0x3FFu;
    return (v & 0x200u) ? (s32)v - 0x400 : (s32)v;
}

static u8 c5to8(u32 v)
{
    return (u8)((v & 0x1Fu) * 255u / 31u);
}

/* ------------------------------------------------------------------ */
/* Status registers                                                    */
/* ------------------------------------------------------------------ */

static u32 gxstat_value(int with_error)
{
    const G3dMtxStack *st = &g_hw.gx.mtx;
    u32 v = 0;

    v |= g_hw.box_result ? STAT_BOX_RESULT : 0u;
    v |= (st->sp & 0x1Fu) << 8;
    v |= g_hw.pj_level ? STAT_PJ_LEVEL : 0u;
    if (with_error && (st->error || g_hw.pj_error)) {
        v |= 0x8000u;
    }
    v |= STAT_LESS_HALF | STAT_EMPTY;
    v |= (g_hw.fifo_irq & 3u) << 30;
    return v;
}

static void publish_stat(void)
{
    g_hw.stat_published = gxstat_value(0);
    bw_io_write32(G3D_HW_GXSTAT, g_hw.stat_published);
}

static void publish_ram_count(void)
{
    u32 polys = g_hw.geom.n_polys > 0xFFFu ? 0xFFFu : g_hw.geom.n_polys;
    u32 verts = g_hw.geom.n_verts > 0x1FFFu ? 0x1FFFu : g_hw.geom.n_verts;

    bw_io_write32(G3D_HW_RAM_COUNT, polys | (verts << 16));
}

static void publish_clip(void)
{
    const G3dMtx *c = g3d_mtx_stack_clip(&g_hw.gx.mtx);
    u32 i;

    for (i = 0; i < 16u; i++) {
        bw_io_write32(G3D_HW_CLIPMTX_RESULT + i * 4u, (u32)c->m[i]);
    }
}

static void publish_vecmtx(void)
{
    const G3dMtx *v = &g_hw.gx.mtx.vec;
    u32 r, c;

    for (r = 0; r < 3u; r++) {
        for (c = 0; c < 3u; c++) {
            bw_io_write32(G3D_HW_VECMTX_RESULT + (r * 3u + c) * 4u,
                          (u32)v->m[r * 4u + c]);
        }
    }
}

/* GXSTAT written by the game: bits 30-31 are the FIFO interrupt condition,
 * bit 15 acknowledges the matrix stack error (and resets the projection
 * stack pointer); everything else is read-only. */
static void gxstat_written(u32 v)
{
    g_hw.fifo_irq = (v >> 30) & 3u;
    if (v & 0x8000u) {
        g_hw.gx.mtx.error = 0;
        g_hw.pj_error = 0;
        g_hw.pj_level = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Running one command                                                 */
/* ------------------------------------------------------------------ */

static void run_cmd(u32 op, const u32 *par, u32 n)
{
    u8 buf[4u + 32u * 4u];
    u32 len = 4u + n * 4u, i;

    bw_wr32(buf, op & 0xFFu);
    for (i = 0; i < n; i++) {
        bw_wr32(buf + 4u + i * 4u, par[i]);
    }
    (void)g3d_gx_run(&g_hw.gx, buf, len, 0, len, len);
}

/* The viewport, folded into clip space so the rasteriser's fixed
 * full-screen mapping lands each vertex where the viewport puts it:
 *     x' = (x * vw + w * (vw + 2 * x1 - 256)) / 256
 *     y' = (y * vh + w * (vh + 2 * y1 - 192)) / 192
 * with y1 counted from the bottom line, as VIEWPORT's is.  Exact identity
 * for the full-screen viewport, which is skipped. */
static void adjust_viewport(void)
{
    G3dGeom *g = &g_hw.geom;
    u32 vp = g_hw.viewport;
    s64 x1 = (s64)(vp & 0xFFu), y1 = (s64)((vp >> 8) & 0xFFu);
    s64 x2 = (s64)((vp >> 16) & 0xFFu), y2 = (s64)((vp >> 24) & 0xFFu);
    s64 vw = x2 - x1 + 1, vh = y2 - y1 + 1;
    u32 i;

    if (vp != FULL_VIEWPORT && vw > 0 && vh > 0) {
        for (i = g_hw.vtx_adjusted; i < g->n_verts; i++) {
            G3dVertex *v = &g->verts[i];
            s64 w = v->w;
            v->x = (fx32)(((s64)v->x * vw + w * (vw + 2 * x1 - 256)) / 256);
            v->y = (fx32)(((s64)v->y * vh + w * (vh + 2 * y1 - 192)) / 192);
        }
    }
    g_hw.vtx_adjusted = g->n_verts;
}

/* ------------------------------------------------------------------ */
/* The tests                                                           */
/* ------------------------------------------------------------------ */

static void pos_test(const u32 *par)
{
    fx32 o[4];
    u32 i;

    /* POS_TEST loads the current vertex too, like VTX_16. */
    g_hw.gx.cx = (fx32)sx16(par[0]);
    g_hw.gx.cy = (fx32)sx16(par[0] >> 16);
    g_hw.gx.cz = (fx32)sx16(par[1]);
    g3d_mtx_xform(g3d_mtx_stack_clip(&g_hw.gx.mtx),
                  g_hw.gx.cx, g_hw.gx.cy, g_hw.gx.cz,
                  &o[0], &o[1], &o[2], &o[3]);
    for (i = 0; i < 4u; i++) {
        bw_io_write32(G3D_HW_POS_RESULT + i * 4u, (u32)o[i]);
    }
}

static void vec_test(const u32 *par)
{
    const G3dMtx *m = &g_hw.gx.mtx.vec;
    s64 v[3];
    u32 i;

    v[0] = (s64)sx10(par[0]) << 3;
    v[1] = (s64)sx10(par[0] >> 10) << 3;
    v[2] = (s64)sx10(par[0] >> 20) << 3;
    for (i = 0; i < 3u; i++) {
        s64 r = (v[0] * m->m[0 * 4 + i] + v[1] * m->m[1 * 4 + i] +
                 v[2] * m->m[2 * 4 + i]) >> 12;
        /* 1 sign + 12 fraction bits, the sign repeated in bits 13-15. */
        u32 r13 = (u32)r & 0x1FFFu;
        u32 r16 = (r13 & 0x1000u) ? (r13 | 0xE000u) : r13;
        bw_io_write16(G3D_HW_VEC_RESULT + i * 2u, (u16)r16);
    }
}

/* Clip a convex polygon of homogeneous points against one plane of the
 * view volume, d(p) = w + s * p[axis] >= 0.  The interpolation goes
 * through a 29-bit fraction so no product leaves 64 bits. */
#define BOX_MAXV 16

static u32 box_clip(s64 (*out)[4], s64 (*in)[4], u32 n, int axis, int sgn)
{
    u32 i, m = 0;

    for (i = 0; i < n; i++) {
        const s64 *a = in[i], *b = in[(i + 1u) % n];
        s64 da = a[3] + sgn * a[axis], db = b[3] + sgn * b[axis];

        if (da >= 0 && m < BOX_MAXV) {
            memcpy(out[m++], a, sizeof(out[0]));
        }
        if ((da >= 0) != (db >= 0) && m < BOX_MAXV) {
            s64 den = da - db;
            s64 q = (s64)(((da < 0 ? -da : da) << 29) / (den < 0 ? -den : den));
            u32 k;
            for (k = 0; k < 4u; k++) {
                out[m][k] = a[k] + (((b[k] - a[k]) * q) >> 29);
            }
            m++;
        }
    }
    return m;
}

/*
 * BOX_TEST: the box (x, y, z) .. (x + w, y + h, z + d) in the current clip
 * matrix, inside if any of its six faces keeps a piece after clipping to
 * -w <= x, y, z <= w.  [AP] a view volume entirely inside the box crosses
 * no face and reads as outside; the public description of the hardware
 * test is face clipping, and that case is not pinned down by it.
 */
static void box_test(const u32 *par)
{
    static const u8 faces[6][4] = {
        { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 },
        { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 },
    };
    const G3dMtx *clip = g3d_mtx_stack_clip(&g_hw.gx.mtx);
    fx32 bx = sx16(par[0]), by = sx16(par[0] >> 16), bz = sx16(par[1]);
    fx32 bw = sx16(par[1] >> 16), bh = sx16(par[2]), bd = sx16(par[2] >> 16);
    s64 c[8][4];
    u32 i, f;

    for (i = 0; i < 8u; i++) {
        fx32 o[4];
        g3d_mtx_xform(clip, bx + ((i & 1u) ? bw : 0), by + ((i & 2u) ? bh : 0),
                      bz + ((i & 4u) ? bd : 0), &o[0], &o[1], &o[2], &o[3]);
        c[i][0] = o[0]; c[i][1] = o[1]; c[i][2] = o[2]; c[i][3] = o[3];
    }
    g_hw.box_result = 0;
    for (f = 0; f < 6u && !g_hw.box_result; f++) {
        s64 a[BOX_MAXV][4], b[BOX_MAXV][4];
        u32 n = 4, axis;

        for (i = 0; i < 4u; i++) {
            memcpy(a[i], c[faces[f][i]], sizeof(a[0]));
        }
        for (axis = 0; axis < 3u && n > 0; axis++) {
            n = box_clip(b, a, n, (int)axis, 1);
            n = box_clip(a, b, n, (int)axis, -1);
        }
        if (n > 0) {
            g_hw.box_result = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Textures out of VRAM                                                */
/* ------------------------------------------------------------------ */

static void copy_slot(int kind, u32 space, u32 off, u8 *dst, u32 n)
{
    while (n > 0) {
        u32 contig = 0, take;
        const u8 *p;

        off %= space;
        p = bw_vram_slot(kind, off, &contig);
        take = 0x2000u - (off & 0x1FFFu);
        if (p != NULL && contig > take) {
            take = contig;
        }
        if (take > n) {
            take = n;
        }
        if (take > space - off) {
            take = space - off;
        }
        if (p != NULL) {
            memcpy(dst, p, take);
        } else {
            memset(dst, 0, take);
        }
        dst += take;
        off += take;
        n -= take;
    }
}

static u32 pal_bytes_of(u32 fmt)
{
    switch (fmt) {
    case G3D_TEXFMT_A3I5:   return 32u * 2u;
    case G3D_TEXFMT_PAL4:   return 4u * 2u;
    case G3D_TEXFMT_PAL16:  return 16u * 2u;
    case G3D_TEXFMT_PAL256: return 256u * 2u;
    case G3D_TEXFMT_A5I3:   return 8u * 2u;
    case G3D_TEXFMT_COMP:   return 0x10004u;
    default:                return 0;
    }
}

#define TEX_SPACE 0x80000u
#define PAL_SPACE 0x18000u

/*
 * Decoded textures persist across frames.  Decoding is the expensive part
 * (about 3.7 ms a frame on the title screen), but a texture's bytes rarely
 * change, so each frame the texel and palette bytes are fetched and hashed
 * (cheap) and the stored decode is reused when the hash matches.
 */
typedef struct TexStore {
    u32 key_tex, key_pal;
    u64 hash;
    int ok;
    G3dTexture tex;
} TexStore;

static TexStore *g_store;
static u32 g_n_store, g_cap_store;

#define TEX_STORE_MAX 512u

static u64 hash_bytes(const u8 *p, size_t n, u64 h)
{
    while (n >= 8u) {
        u64 v;

        memcpy(&v, p, 8);
        h = (h ^ v) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
        p += 8;
        n -= 8;
    }
    while (n > 0) {
        h = (h ^ *p++) * 0x100000001B3ull;
        n--;
    }
    return h;
}

static void store_clear(void)
{
    u32 i;

    for (i = 0; i < g_n_store; i++) {
        g3d_texture_free(&g_store[i].tex);
    }
    g_n_store = 0;
}

static int decode_vram_texture(G3dTexture *out, u32 tp, u32 pltt, u32 key_tex,
                               u32 key_pal, u64 *hash_out)
{
    u32 fmt = G3D_TEX_FORMAT(tp);
    u32 w = 8u << ((tp >> 20) & 7u), h = 8u << ((tp >> 23) & 7u);
    u32 addr = (tp & 0xFFFFu) << 3;
    u32 bytes = g3d_texture_byte_size(fmt, w, h);
    u32 pbytes = pal_bytes_of(fmt);
    u8 *texel, *pal = NULL;
    u64 hash;
    u32 i;
    int rc;

    texel = (u8 *)malloc(bytes ? bytes : 1u);
    if (texel == NULL) {
        return -1;
    }
    if (fmt == G3D_TEXFMT_COMP) {
        u32 blocks = (w / 4u) * (h / 4u);
        u32 iaddr = 0x20000u + ((addr & 0x1FFFFu) >> 1) +
                    (addr >= 0x40000u ? 0x10000u : 0u);
        copy_slot(BW_VSLOT_TEX, TEX_SPACE, addr, texel, blocks * 4u);
        copy_slot(BW_VSLOT_TEX, TEX_SPACE, iaddr, texel + blocks * 4u,
                  blocks * 2u);
    } else {
        copy_slot(BW_VSLOT_TEX, TEX_SPACE, addr, texel, bytes);
    }
    if (pbytes > 0) {
        u32 poff = (pltt & 0x1FFFu) << (fmt == G3D_TEXFMT_PAL4 ? 3 : 4);
        if (poff >= PAL_SPACE) {
            poff %= PAL_SPACE;
        }
        if (pbytes > PAL_SPACE - poff) {
            pbytes = PAL_SPACE - poff;
        }
        pal = (u8 *)malloc(pbytes);
        if (pal == NULL) {
            free(texel);
            return -1;
        }
        copy_slot(BW_VSLOT_TEXPAL, PAL_SPACE, poff, pal, pbytes);
    }
    /* what the decode depends on: the bytes and the format bits */
    hash = hash_bytes(texel, bytes, 0xCBF29CE484222325ull ^ tp);
    if (pal != NULL) {
        hash = hash_bytes(pal, pbytes, hash);
    }
    *hash_out = hash;
    for (i = 0; i < g_n_store; i++) {
        TexStore *s = &g_store[i];

        if (s->key_tex == key_tex && s->key_pal == key_pal) {
            if (s->hash == hash) {
                *out = s->tex;           /* borrowed */
                free(texel);
                free(pal);
                return s->ok ? 0 : -1;
            }
            g3d_texture_free(&s->tex);   /* changed: decode again */
            memset(&s->tex, 0, sizeof(s->tex));
            rc = g3d_texture_decode(&s->tex, fmt, w, h, texel, bytes, pal,
                                    pbytes, (int)((tp >> 29) & 1u));
            s->hash = hash;
            s->ok = rc == 0;
            *out = s->tex;
            free(texel);
            free(pal);
            return rc;
        }
    }
    if (g_n_store == g_cap_store) {
        u32 cap = g_cap_store ? g_cap_store * 2u : 32u;
        TexStore *n = (TexStore *)realloc(g_store, (size_t)cap * sizeof(*n));

        if (n == NULL) {
            free(texel);
            free(pal);
            return -1;
        }
        g_store = n;
        g_cap_store = cap;
    }
    {
        TexStore *s = &g_store[g_n_store++];

        memset(s, 0, sizeof(*s));
        s->key_tex = key_tex;
        s->key_pal = key_pal;
        s->hash = hash;
        rc = g3d_texture_decode(&s->tex, fmt, w, h, texel, bytes, pal, pbytes,
                                (int)((tp >> 29) & 1u));
        s->ok = rc == 0;
        *out = s->tex;
    }
    free(texel);
    free(pal);
    return rc;
}

/* The hash of the texture lookup() last returned (the snapshot keeps it). */
static u64 g_lookup_hash;

static const G3dTexture *lookup(void *user, u32 poly_index)
{
    const G3dPoly *poly = &g_hw.geom.polys[poly_index];
    u32 tp = poly->teximage, fmt = G3D_TEX_FORMAT(tp);
    u32 key_tex = tp & 0x3FF0FFFFu;   /* repeat/flip do not change texels */
    u32 key_pal = fmt == G3D_TEXFMT_DIRECT ? 0u : (poly->pltt_base & 0x1FFFu);
    TexCacheEntry *e;
    u32 i;

    (void)user;
    if (!(g_hw.disp3dcnt & 1u) || fmt == G3D_TEXFMT_NONE) {
        return NULL;
    }
    for (i = 0; i < g_hw.n_cache; i++) {
        e = &g_hw.cache[i];
        if (e->key_tex == key_tex && e->key_pal == key_pal) {
            g_lookup_hash = e->hash;
            return e->ok ? &e->tex : NULL;
        }
    }
    if (g_hw.n_cache == g_hw.cap_cache) {
        u32 cap = g_hw.cap_cache ? g_hw.cap_cache * 2u : 16u;
        TexCacheEntry *n = (TexCacheEntry *)realloc(
            g_hw.cache, (size_t)cap * sizeof(*n));
        if (n == NULL) {
            return NULL;
        }
        g_hw.cache = n;
        g_hw.cap_cache = cap;
    }
    e = &g_hw.cache[g_hw.n_cache++];
    memset(e, 0, sizeof(*e));
    e->key_tex = key_tex;
    e->key_pal = key_pal;
    e->borrowed = 1;
    e->ok = decode_vram_texture(&e->tex, tp, poly->pltt_base, key_tex, key_pal,
                                &e->hash) == 0;
    g_lookup_hash = e->hash;
    return e->ok ? &e->tex : NULL;
}

static void cache_clear(void)
{
    u32 i;

    for (i = 0; i < g_hw.n_cache; i++) {
        if (!g_hw.cache[i].borrowed) {
            g3d_texture_free(&g_hw.cache[i].tex);
        }
    }
    g_hw.n_cache = 0;
    if (g_n_store > TEX_STORE_MAX) {
        store_clear();     /* between frames: nothing borrows from it now */
    }
}

/* ------------------------------------------------------------------ */
/* SWAP_BUFFERS                                                        */
/* ------------------------------------------------------------------ */

/* The clear depth in the rasteriser's units: 15 bits onto its 23-bit z,
 * or [AP] in a w-buffered frame the 24-bit value the hardware widens it to
 * (d * 0x200, plus 0x1FF at 0x7FFF), compared with w directly. */
static s32 clear_depth_of(const G3dTarget *t, u32 cd)
{
    if (t->wbuffer) {
        return (s32)(cd * 0x200u + (cd == 0x7FFFu ? 0x1FFu : 0u));
    }
    return cd == 0x7FFFu ? G3D_DEPTH_FAR : (s32)((cd << 8) | (cd >> 7));
}

/*
 * The clear: CLEAR_COLOR and CLEAR_DEPTH, or with DISP3DCNT bit 14 the
 * rear-plane bitmap, a 256x256 image whose colour (BGR555, bit 15 the
 * alpha) is texture slot 2 and whose depth (bits 0-14) and fog flag (bit
 * 15) are slot 3, scrolled by CLRIMAGE_OFFSET (x bits 0-7, y bits 8-15)
 * with wrapping.  Either way every pixel takes the clear polygon ID
 * (CLEAR_COLOR bits 24-29); the fog flag is CLEAR_COLOR bit 15 for the
 * plain clear.
 */
static u32 g_scale = 1;   /* internal resolution, survives g3d_hw_reset */

static void clear_planes(G3dTarget *t, u32 d3, u32 cc, u32 cd)
{
    u32 n = t->width * t->height, i;
    u8 fog = (u8)((cc & 0x8000u) ? G3D_PIX_FOG : 0u);
    s32 depth = clear_depth_of(t, cd);

    g3d_target_clear(t, c5to8(cc), c5to8(cc >> 5), c5to8(cc >> 10),
                     (u8)(((cc >> 16) & 0x1Fu) * 255u / 31u));
    for (i = 0; i < n; i++) {
        t->depth[i] = depth;
        t->pid[i] = (u8)((cc >> 24) & 0x3Fu);
        t->pflag[i] = fog;
    }
    if (d3 & 0x4000u) {
        u32 ofs = bw_io_read16(G3D_HW_CLRIMAGE_OFS);
        u32 x, y;

        for (y = 0; y < t->height; y++) {
            for (x = 0; x < t->width; x++) {
                /* the bitmap is DS resolution: nearest at scale N */
                u32 at = ((((y / g_scale + (ofs >> 8)) & 0xFFu) * 256u)
                          + ((x / g_scale + ofs) & 0xFFu)) * 2u;
                const u8 *c = bw_vram_slot(BW_VSLOT_TEX, 0x40000u + at, NULL);
                const u8 *d = bw_vram_slot(BW_VSLOT_TEX, 0x60000u + at, NULL);
                u16 cv = c != NULL ? bw_rd16(c) : 0;
                u16 dv = d != NULL ? bw_rd16(d) : 0;
                size_t o = (size_t)y * t->width + x;

                g3d_bgr555((u16)(cv & 0x7FFFu), &t->rgba[o * 4u],
                           &t->rgba[o * 4u + 1u], &t->rgba[o * 4u + 2u]);
                t->rgba[o * 4u + 3u] = (u8)((cv & 0x8000u) ? 255u : 0u);
                t->depth[o] = clear_depth_of(t, dv & 0x7FFFu);
                t->pflag[o] = (u8)((dv & 0x8000u) ? G3D_PIX_FOG : 0u);
            }
        }
    }
}

/* Edge marking, fog and anti-aliasing, in the hardware's order. */
static void rear_passes(G3dTarget *t, u32 d3, u32 cc, u32 cd)
{
    u32 i;

    if (d3 & 0x0020u) {
        u16 edge[8];

        for (i = 0; i < 8u; i++) {
            edge[i] = bw_io_read16(G3D_HW_EDGE_COLOR + i * 2u);
        }
        g3d_target_edge_mark(t, edge, (cc >> 24) & 0x3Fu,
                             clear_depth_of(t, cd));
    }
    if (d3 & 0x0080u) {
        u8 table[32];

        for (i = 0; i < 32u; i++) {
            table[i] = (u8)(bw_io_read16(G3D_HW_FOG_TABLE + (i & ~1u))
                            >> ((i & 1u) * 8u));
        }
        g3d_target_fog(t, table, bw_io_read32(G3D_HW_FOG_COLOR),
                       bw_io_read16(G3D_HW_FOG_OFFSET), (d3 >> 8) & 0xFu,
                       (d3 & 0x0040u) != 0);
    }
    if (d3 & 0x0010u) {
        g3d_target_antialias(t);
    }
}

static void trace_report(const G3dTarget *t, u32 d3);

/* The snapshot for a GPU renderer (hw3d.h).  Textures are copied once each,
 * identified by their decoded pixel buffer. */
static G3dSnapshot g_snap;
static int g_snap_on;
static int g_snap_valid;
static u32 g_snap_cap_ptex;

static int g_skip_raster;
static u32 g_gpu_features;
static G3dBackdropHook g_backdrop_hook;

void g3d_hw_set_backdrop_hook(G3dBackdropHook hook)
{
    g_backdrop_hook = hook;
}

static G3dSnapshotHook g_snapshot_hook;

void g3d_hw_set_snapshot_hook(G3dSnapshotHook hook)
{
    g_snapshot_hook = hook;
}

void g3d_hw_gpu_features(u32 snap_mask)
{
    g_gpu_features = snap_mask;
}

void g3d_hw_skip_raster(int on)
{
    g_skip_raster = on != 0;
}

void g3d_hw_snapshot_enable(int on)
{
    g_snap_on = on != 0;
    if (!g_snap_on) {
        g_snap_valid = 0;
    }
}

const G3dSnapshot *g3d_hw_snapshot(void)
{
    return g_snap_on && g_snap_valid ? &g_snap : NULL;
}

static int snap_grow(void **p, u32 *cap, u32 need, size_t elem)
{
    void *n;
    u32 c;

    if (need <= *cap) {
        return 0;
    }
    c = *cap ? *cap : 64u;
    while (c < need) {
        c *= 2u;
    }
    n = realloc(*p, (size_t)c * elem);
    if (n == NULL) {
        return -1;
    }
    *p = n;
    *cap = c;
    return 0;
}

/* build_snapshot's copies from the frame before, for reuse. */
static G3dSnapTex *g_snap_pool;
static u32 g_snap_n_pool, g_snap_cap_pool;

static void build_snapshot(u32 d3, u32 cc, u32 cd, const G3dTarget *t,
                           u32 unsup)
{
    G3dSnapshot *s = &g_snap;
    const G3dGeom *g = &g_hw.geom;
    const u8 **seen = NULL;
    u32 n_seen = 0, cap_seen = 0, i;

    g_snap_valid = 0;
    s->n_verts = s->n_idx = s->n_polys = 0;
    /* Last frame's copies are kept for this one: a texture whose source is
     * unchanged keeps its copy, so a frame copies only what is new. */
    if (snap_grow((void **)&g_snap_pool, &g_snap_cap_pool, s->n_tex,
                  sizeof(*g_snap_pool)) != 0) {
        for (i = 0; i < s->n_tex; i++) {
            free(s->tex[i].rgba);
        }
        g_snap_n_pool = 0;
    } else {
        for (i = 0; i < s->n_tex; i++) {
            g_snap_pool[i] = s->tex[i];
        }
        g_snap_n_pool = s->n_tex;
    }
    for (i = 0; i < s->n_tex; i++) {
        s->tex[i].rgba = NULL;
    }
    s->n_tex = 0;
    if (snap_grow((void **)&s->verts, &s->cap_verts, g->n_verts, sizeof(*s->verts)) != 0
        || snap_grow((void **)&s->idx, &s->cap_idx, g->n_idx, sizeof(*s->idx)) != 0
        || snap_grow((void **)&s->polys, &s->cap_polys, g->n_polys, sizeof(*s->polys)) != 0
        || snap_grow((void **)&s->poly_tex, &g_snap_cap_ptex, g->n_polys, sizeof(*s->poly_tex)) != 0) {
        return;
    }
    if (g->n_verts > 0) {
        memcpy(s->verts, g->verts, (size_t)g->n_verts * sizeof(*s->verts));
    }
    if (g->n_idx > 0) {
        memcpy(s->idx, g->idx, (size_t)g->n_idx * sizeof(*s->idx));
    }
    if (g->n_polys > 0) {
        memcpy(s->polys, g->polys, (size_t)g->n_polys * sizeof(*s->polys));
    }
    s->n_verts = g->n_verts;
    s->n_idx = g->n_idx;
    s->n_polys = g->n_polys;
    for (i = 0; i < g->n_polys; i++) {
        unsigned long long tt = eidos_prof_begin();
        const G3dTexture *tx = lookup(NULL, i);

        eidos_prof_end(EIDOS_PROF_TEXDEC, tt);
        s32 slot = -1;

        if (tx != NULL && tx->rgba != NULL && tx->width > 0 && tx->height > 0) {
            u32 k;

            for (k = 0; k < n_seen; k++) {
                if (seen[k] == tx->rgba) {
                    slot = (s32)k;
                    break;
                }
            }
            if (slot < 0) {
                size_t bytes = (size_t)tx->width * tx->height * 4u;
                u64 content = g_lookup_hash;
                u32 q;

                if (snap_grow((void **)&seen, &cap_seen, n_seen + 1u, sizeof(*seen)) != 0
                    || snap_grow((void **)&s->tex, &s->cap_tex, s->n_tex + 1u, sizeof(*s->tex)) != 0) {
                    free(seen);
                    return;
                }
                for (q = 0; content != 0 && q < g_snap_n_pool; q++) {
                    G3dSnapTex *old = &g_snap_pool[q];

                    if (old->rgba != NULL && old->content == content
                        && old->w == tx->width && old->h == tx->height) {
                        break;
                    }
                }
                if (content != 0 && q < g_snap_n_pool) {
                    s->tex[s->n_tex] = g_snap_pool[q];
                    g_snap_pool[q].rgba = NULL;
                    seen[n_seen++] = tx->rgba;
                    slot = (s32)s->n_tex++;
                    s->poly_tex[i] = slot;
                    continue;
                }
                s->tex[s->n_tex].content = content;
                s->tex[s->n_tex].rgba = (u8 *)malloc(bytes);
                if (s->tex[s->n_tex].rgba == NULL) {
                    free(seen);
                    return;
                }
                memcpy(s->tex[s->n_tex].rgba, tx->rgba, bytes);
                s->tex[s->n_tex].partial = 0;
                {
                    u32 fmt = G3D_TEX_FORMAT(g->polys[i].teximage);

                    /* only the two alpha formats carry a 5-bit alpha that can
                     * land between transparent and opaque */
                    if (fmt == G3D_TEXFMT_A3I5 || fmt == G3D_TEXFMT_A5I3) {
                        size_t k;

                        for (k = 0; k < (size_t)tx->width * tx->height; k++) {
                            u32 a5 = (u32)tx->rgba[k * 4u + 3u] >> 3;

                            if (a5 > 0u && a5 < 31u) {
                                s->tex[s->n_tex].partial = 1;
                                break;
                            }
                        }
                    }
                }
                s->tex[s->n_tex].w = tx->width;
                s->tex[s->n_tex].h = tx->height;
                seen[n_seen++] = tx->rgba;
                slot = (s32)s->n_tex++;
            }
        }
        s->poly_tex[i] = slot;
    }
    free(seen);
    for (i = 0; i < g_snap_n_pool; i++) {
        free(g_snap_pool[i].rgba);
        g_snap_pool[i].rgba = NULL;
    }
    g_snap_n_pool = 0;
    s->serial = g_hw.frames;
    s->disp3dcnt = d3;
    s->wbuffer = t->wbuffer;
    s->alpha_ref = t->alpha_ref;
    s->highlight = t->highlight;
    s->clear_color = cc;
    s->clear_depth = cd;
    memcpy(s->toon, g_hw.toon, sizeof(s->toon));
    for (i = 0; i < 8u; i++) {
        s->edge[i] = bw_io_read16(G3D_HW_EDGE_COLOR + i * 2u);
    }
    s->fog_color = bw_io_read32(G3D_HW_FOG_COLOR);
    s->fog_offset = bw_io_read16(G3D_HW_FOG_OFFSET) & 0x7FFFu;
    for (i = 0; i < 32u; i++) {
        s->fog_table[i] = (u8)((bw_io_read16(G3D_HW_FOG_TABLE + (i & ~1u))
                                >> ((i & 1u) * 8u)) & 0x7Fu);
    }
    s->unsupported = 0;
    if (d3 & 0x0020u) {
        s->unsupported |= G3D_SNAP_EDGE;
    }
    if (d3 & 0x0080u) {
        s->unsupported |= G3D_SNAP_FOG;
    }
    if (d3 & 0x4000u) {
        s->unsupported |= G3D_SNAP_REARBMP;
    }
    if (unsup & G3D_HW_UNSUP_SHADOW) {
        s->unsupported |= G3D_SNAP_SHADOW;
    }
    s->unsupported &= ~g_gpu_features;
    memset(&s->backdrop, 0, sizeof(s->backdrop));
    if (g_backdrop_hook != NULL) {
        g_backdrop_hook(&s->backdrop);
    }
    if (g_snapshot_hook != NULL) {
        g_snapshot_hook(s);
    }
    g_snap_valid = 1;
}

static void render_frame(void)
{
    G3dTarget *t = &g_hw.front;
    u32 d3 = bw_io_read16(G3D_HW_DISP3DCNT);
    u32 cc = bw_io_read32(G3D_HW_CLEAR_COLOR);
    u32 cd = bw_io_read16(G3D_HW_CLEAR_DEPTH) & 0x7FFFu;
    u32 unsup = 0, i;

    if (t->rgba != NULL && t->width != G3D_SCREEN_W * g_scale) {
        g3d_target_free(t);   /* the scale changed */
    }
    if (t->rgba == NULL
        && g3d_target_init(t, G3D_SCREEN_W * g_scale,
                           G3D_SCREEN_H * g_scale) != 0) {
        return;
    }
    g_hw.disp3dcnt = d3;
    if (g_tr.on) {
        memset(&g3d_raster_stats, 0, sizeof(g3d_raster_stats));
    }

    t->wbuffer = (g_hw.swap_param >> 1) & 1u;
    t->aa = (d3 >> 4) & 1u;
    clear_planes(t, d3, cc, cd);

    t->alpha_ref = (d3 & 4u) ? (bw_io_read16(G3D_HW_ALPHA_TEST_REF) & 0x1Fu) : 0u;
    for (i = 0; i < 32u; i++) {
        u16 c = bw_io_read16(G3D_HW_TOON_TABLE + i * 2u);
        g3d_bgr555(c, &g_hw.toon[i * 3u], &g_hw.toon[i * 3u + 1u],
                   &g_hw.toon[i * 3u + 2u]);
    }
    t->toon = g_hw.toon;
    t->highlight = (d3 >> 1) & 1u;

    for (i = 0; i < g_hw.geom.n_polys; i++) {
        G3dPoly *p = &g_hw.geom.polys[i];
        if (G3D_ATTR_MODE(p->attr) == G3D_POLY_MODE_SHADOW) {
            unsup |= G3D_HW_UNSUP_SHADOW;
        }
        if (!(d3 & 8u) && G3D_ATTR_ALPHA(p->attr) != 0) {
            p->attr |= (u32)0x1F << 16;
        }
    }

    g3d_geom_sort(&g_hw.geom, (int)(g_hw.swap_param & 1u));
    if (getenv("EIDOS_DEBUG_G3D_HASH") != NULL) {
        /* Checking two machines against each other: every 60 frames, FNV-1a
         * hashes of the frame's transformed vertices and polygons, and the
         * registers the picture depends on.  Equal hashes on both: the 3D
         * input is the same, and a difference comes later. */
        /* "all": every frame, not every 60th */
        static u32 frames;
        int every = getenv("EIDOS_DEBUG_G3D_HASH")[0] == 'a';

        if ((++frames % 60u) == 0 || every) {
            u32 hv = 2166136261u, hp = 2166136261u, k;
            const u8 *b;

            for (k = 0; k < g_hw.geom.n_verts; k++) {
                const G3dVertex *v = &g_hw.geom.verts[k];
                s32 f[9] = { v->x, v->y, v->z, v->w, v->s, v->t, v->r, v->g, v->b };
                u32 j;

                for (b = (const u8 *)f, j = 0; j < sizeof(f); j++) {
                    hv = (hv ^ b[j]) * 16777619u;
                }
            }
            for (k = 0; k < g_hw.geom.n_polys; k++) {
                const G3dPoly *p = &g_hw.geom.polys[k];
                u32 f[4] = { p->first, p->count, p->attr, p->teximage };
                u32 j;

                for (b = (const u8 *)f, j = 0; j < sizeof(f); j++) {
                    hp = (hp ^ b[j]) * 16777619u;
                }
            }
            fprintf(stderr, "g3d hash frame %u: verts %u %08X polys %u %08X d3 %04X clear %08X/%04X fog %08X/%04X\n",
                    frames, g_hw.geom.n_verts, hv, g_hw.geom.n_polys, hp, d3, cc, cd,
                    bw_io_read32(G3D_HW_FOG_COLOR), bw_io_read16(G3D_HW_FOG_OFFSET));
            if (g_hw.geom.n_verts > 0) {
                const G3dVertex *v = &g_hw.geom.verts[0];

                fprintf(stderr, "g3d hash frame %u: vertex 0 x %d y %d z %d w %d\n",
                        frames, (int)v->x, (int)v->y, (int)v->z, (int)v->w);
            }
        }
    }
    {
        static u32 swaps;

        if ((++swaps % 60u) == 0 || getenv("EIDOS_G3D_MTX_ALL") != NULL) {
            if (getenv("EIDOS_G3D_MTX") != NULL) {
                fprintf(stderr, "mtx swap %u\n", swaps);
            }
            g_mtx_log = 0;     /* arm the matrix log for the next frame */
        }
    }
    if (getenv("EIDOS_G3D_OPS") != NULL) {
        static u32 n;

        if ((++n % 60u) == 0) {
            fprintf(stderr, "g3d ops (swap %u): POLYGON_ATTR %u NORMAL %u COLOR %u DIF_AMB %u SPE_EMI %u LIGHT_VECTOR %u LIGHT_COLOR %u"
                    " VTX16 %u BEGIN %u TEXIMAGE %u; poly_attr now %08X lights %X\n",
                    n, g_ops_frame[0x29], g_ops_frame[0x21], g_ops_frame[0x20], g_ops_frame[0x30],
                    g_ops_frame[0x31], g_ops_frame[0x32], g_ops_frame[0x33], g_ops_frame[0x23],
                    g_ops_frame[0x40], g_ops_frame[0x2A], g_hw.gx.poly_attr,
                    G3D_ATTR_LIGHTS(g_hw.gx.poly_attr));
        }
        memset(g_ops_frame, 0, sizeof(g_ops_frame));
    }
    {
        /* A debugging aid: EIDOS_G3D_POLYS=lo-hi draws only those polygons of
         * the frame (after the hardware order), to isolate one model part. */
        static int init, lo, hi;

        if (!init) {
            const char *e = getenv("EIDOS_G3D_POLYS");

            init = 1;
            lo = 0;
            hi = -1;
            if (e != NULL && sscanf(e, "%d-%d", &lo, &hi) != 2) {
                lo = 0;
                hi = -1;
            }
        }
        if (hi >= lo) {
            for (i = 0; i < g_hw.geom.n_polys; i++) {
                if ((int)i < lo || (int)i > hi) {
                    g_hw.geom.polys[i].attr &= ~((u32)0x1F << 16);   /* alpha 0: not drawn */
                }
            }
        }
    }
    {
        /* EIDOS_G3D_SKIP=lo-hi leaves those polygons out (the opposite). */
        static int init, lo, hi;

        if (!init) {
            const char *e = getenv("EIDOS_G3D_SKIP");

            init = 1;
            lo = 0;
            hi = -1;
            if (e != NULL && sscanf(e, "%d-%d", &lo, &hi) != 2) {
                lo = 0;
                hi = -1;
            }
        }
        for (i = 0; hi >= lo && i < g_hw.geom.n_polys; i++) {
            if ((int)i >= lo && (int)i <= hi) {
                g_hw.geom.polys[i].attr &= ~((u32)0x1F << 16);
            }
        }
    }
    if (g_snap_on) {
        unsigned long long ts = eidos_prof_begin();

        build_snapshot(d3, cc, cd, t, unsup);
        eidos_prof_end(EIDOS_PROF_SNAP, ts);
    }
    if (g_skip_raster && g_snap_on && g_snap_valid
        && g_snap.unsupported == 0u) {
        /* A GPU renderer draws this frame from the snapshot. */
        cache_clear();
        g_hw.unsup_frame = unsup;
        g_hw.have_frame = 1;
        return;
    }
    {
        unsigned long long tr = eidos_prof_begin();

        g3d_raster_geom(t, &g_hw.geom, lookup, NULL);
        eidos_prof_end(EIDOS_PROF_RASTER, tr);
    }
    if (g_tr.on) {
        trace_report(t, d3);
    }
    cache_clear();
    rear_passes(t, d3, cc, cd);
    if (getenv("EIDOS_DEBUG_G3D_HASH") != NULL) {
        /* the rasteriser's output: its hash and how much of it is drawn */
        static u32 frames;
        int every = getenv("EIDOS_DEBUG_G3D_HASH")[0] == 'a';

        if ((++frames % 60u) == 0 || every) {
            u32 h = 2166136261u, k, drawn = 0, n = t->width * t->height;

            for (k = 0; k < n * 4u; k++) {
                h = (h ^ t->rgba[k]) * 16777619u;
            }
            for (k = 0; k < n; k++) {
                drawn += t->rgba[k * 4u + 3u] != 0;
            }
            fprintf(stderr, "g3d out frame %u: %ux%u %08X drawn %u of %u\n",
                    frames, t->width, t->height, h, drawn, n);
        }
    }

    g_hw.unsup_frame = unsup;
    g_hw.have_frame = 1;
}

static void trace_report(const G3dTarget *t, u32 d3)
{
    static const char *const fmt_name[8] = { "none", "A3I5", "PAL4", "PAL16",
                                             "PAL256", "COMP", "A5I3", "DIRECT" };
    const G3dMtxStack *st = &g_hw.gx.mtx;
    const G3dRasterStats *r = &g3d_raster_stats;
    u32 i, lit = 0, covered = 0, a0 = 0;
    u32 fmts[8] = { 0 };

    for (i = 0; i < g_hw.geom.n_polys; i++) {
        const G3dPoly *p = &g_hw.geom.polys[i];

        fmts[G3D_TEX_FORMAT(p->teximage) & 7u]++;
        if (G3D_ATTR_LIGHTS(p->attr)) {
            lit++;
        }
        if (G3D_ATTR_ALPHA(p->attr) == 0) {
            a0++;
        }
    }
    for (i = 0; i < t->width * t->height; i++) {
        if (t->rgba[i * 4u + 3u] != 0) {
            covered++;
        }
    }
    fprintf(stderr, "g3d-trace swap %u: DISP3DCNT %04X viewport %08X swap_param %u\n",
            g_hw.frames, d3, g_hw.viewport, g_hw.swap_param);
    fprintf(stderr, "  words: fifo-stores %u direct-ports %u send/DMA %u extern-write %u\n",
            g_tr.w_fifo, g_tr.w_port, g_tr.w_send, g_tr.w_ext);
    fprintf(stderr, "  commands:");
    for (i = 0; i < 256u; i++) {
        if (g_tr.op_count[i]) {
            fprintf(stderr, " %02X:%u", i, g_tr.op_count[i]);
        }
    }
    fprintf(stderr, "\n  geometry: verts %u polys %u (lit %u, alpha0 %u)  textures:",
            g_hw.geom.n_verts, g_hw.geom.n_polys, lit, a0);
    for (i = 1; i < 8u; i++) {
        if (fmts[i]) {
            fprintf(stderr, " %s:%u", fmt_name[i], fmts[i]);
        }
    }
    fprintf(stderr, "\n  raster: submitted %u drawn %u rejected: count %u alpha0 %u shadow %u clip %u degenerate %u cull %u\n",
            r->submitted, r->drawn, r->rej_count, r->rej_alpha0, r->rej_shadow,
            r->rej_clip, r->rej_degenerate, r->rej_cull);
    fprintf(stderr, "  mtx: mode %u sp %u err %u pj_level %u; pixels with alpha %u of %u\n",
            st->mode, st->sp, st->error, g_hw.pj_level, covered, t->width * t->height);
    if (g_hw.geom.n_verts) {
        const G3dVertex *v = &g_hw.geom.verts[0];

        fprintf(stderr, "  first vertex: clip %d %d %d w %d  obj %d %d %d\n",
                v->x, v->y, v->z, v->w, v->ox, v->oy, v->oz);
    }
    {
        /* attribute groups: attr word, polygon count, clip-space extent */
        u32 seen[16] = { 0 }, cnt[16] = { 0 }, ns = 0, k;
        s32 mnx[16], mxx[16], mny[16], mxy[16];

        for (i = 0; i < g_hw.geom.n_polys; i++) {
            const G3dPoly *p = &g_hw.geom.polys[i];
            u32 j;

            for (k = 0; k < ns && seen[k] != p->attr; k++) {
            }
            if (k == ns) {
                if (ns == 16u) {
                    continue;
                }
                seen[ns] = p->attr;
                mnx[ns] = mny[ns] = 0x7FFFFFF;
                mxx[ns] = mxy[ns] = -0x7FFFFFF;
                ns++;
            }
            cnt[k]++;
            for (j = 0; j < p->count; j++) {
                const G3dVertex *v = &g_hw.geom.verts[g_hw.geom.idx[p->first + j]];
                s32 sx = v->w ? (s32)(((s64)v->x * 128) / v->w) + 128 : 0;
                s32 sy = v->w ? 96 - (s32)(((s64)v->y * 96) / v->w) : 0;

                if (sx < mnx[k]) mnx[k] = sx;
                if (sx > mxx[k]) mxx[k] = sx;
                if (sy < mny[k]) mny[k] = sy;
                if (sy > mxy[k]) mxy[k] = sy;
            }
        }
        for (k = 0; k < ns; k++) {
            fprintf(stderr, "  attr %08X: %u polys, screen x %d..%d y %d..%d\n",
                    seen[k], cnt[k], mnx[k], mxx[k], mny[k], mxy[k]);
        }
    }
    {
        char name[64];
        FILE *fp;

        /* The 3D engine's own frame, raw RGBA, for off-line viewing. */
        snprintf(name, sizeof(name), "g3d-trace-%u.rgba", g_hw.frames);
        fp = fopen(name, "wb");
        if (fp != NULL) {
            fwrite(t->rgba, 4u, (size_t)t->width * t->height, fp);
            fclose(fp);
        }
    }
    memset(&g_tr.op_count, 0, sizeof(g_tr.op_count));
    g_tr.w_fifo = g_tr.w_port = g_tr.w_send = g_tr.w_ext = 0;
}

void g3d_hw_trace(int on)
{
    g_tr.on = on != 0;
    memset(&g_tr.op_count, 0, sizeof(g_tr.op_count));
    g_tr.w_fifo = g_tr.w_port = g_tr.w_send = g_tr.w_ext = 0;
}

static void swap_buffers(u32 param)
{
    if (g_hw.gx.batch_open) {
        run_cmd(0x41u, NULL, 0);
    }
    g_hw.swap_param = param;
    render_frame();
    g_hw.frames++;
    /* Polygon and vertex RAM start over; matrices, the current vertex,
     * colour and attributes persist, as on the hardware. */
    g_hw.geom.n_verts = 0;
    g_hw.geom.n_idx = 0;
    g_hw.geom.n_polys = 0;
    g_hw.vtx_adjusted = 0;
}

/* ------------------------------------------------------------------ */
/* Executing a command                                                 */
/* ------------------------------------------------------------------ */

/* The current vertex colour, as COLOR would set it. */
static void set_color(u16 c)
{
    g_hw.gx.cr = c5to8(c);
    g_hw.gx.cg = c5to8((u32)c >> 5);
    g_hw.gx.cb = c5to8((u32)c >> 10);
}

static void exec_cmd(u32 op, const u32 *par);

static void exec(u32 op, const u32 *par)
{
    unsigned long long t0 = eidos_prof_begin();
    g_ops_frame[op & 0xFFu]++;
    if (op >= 0x16u && op <= 0x1Cu && badmtx_on()) {
        /* a matrix command with an element far outside any real scene */
        u32 k, n = g3d_gx_param_count(op), bad = 0;
        static u32 swaps_seen;

        for (k = 0; k < n && k < 16u; k++) {
            s32 v = (s32)par[k];

            if ((v > 300000 || v < -300000) && !((op == 0x17u || op == 0x19u) && k >= 9u) && op != 0x1Cu && op != 0x16u && op != 0x18u) {
                bad = 1;
            }
        }
        if (bad) {
            fprintf(stderr, "badmtx op %02X after %u matrix ops:", op, g_ops_frame[0x13]);
            for (k = 0; k < n && k < 16u; k++) {
                fprintf(stderr, " %d", (int)par[k]);
            }
            fprintf(stderr, "\n");
        }
        if (bad && swaps_seen < 3u) {
            /* who sent it: the return addresses up the frame chain */
            void **fp = (void **)__builtin_frame_address(0);
            int d;

            swaps_seen++;
            {
                u32 r;

                for (r = 0; r < 64u; r++) {
                    unsigned long long e = g_ring[(g_ring_n + r) & 63u];

                    fprintf(stderr, " %s%08X", (e >> 32) ? "*" : "", (unsigned)e);
                }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "  ref %p\n", (void *)g3d_hw_flush);
            for (d = 0; d < 14 && fp != NULL; d++) {
                void **up = (void **)fp[0];

                fprintf(stderr, "  from %p\n", fp[1]);
                if (up <= fp) {
                    break;
                }
                fp = up;
            }
        }
    }
    {
        /* EIDOS_G3D_MTX=N: the first N matrix commands after each 60th swap */
        static int init, lim;

        if (!init) {
            const char *e = getenv("EIDOS_G3D_MTX");

            init = 1;
            lim = e != NULL ? atoi(e) : 0;
        }
        if (lim > 0 && g_mtx_log < (u32)lim && ((op >= 0x10u && op <= 0x1Cu) || op == 0x2Au || op == 0x29u)) {
            u32 k, n = g3d_gx_param_count(op);

            fprintf(stderr, "mtx %02X:", op);
            for (k = 0; k < n && k < 16u; k++) {
                fprintf(stderr, " %d", (int)par[k]);
            }
            fprintf(stderr, "\n");
            g_mtx_log++;
        }
    }

    exec_cmd(op, par);
    eidos_prof_end(EIDOS_PROF_GEOM, t0);
}

static void exec_cmd(u32 op, const u32 *par)
{
    G3dMtxStack *st = &g_hw.gx.mtx;
    u32 n = g3d_gx_param_count(op);

    if (g_tr.on) {
        g_tr.op_count[op & 0xFFu]++;
    }
    switch (op) {
    case 0x11:      /* MTX_PUSH */
        if (st->mode == G3D_MTX_MODE_PROJECTION) {
            if (g_hw.pj_level) {
                g_hw.pj_error = 1;
            }
            g_hw.pj_level = 1;
        }
        run_cmd(op, par, n);
        break;
    case 0x12: {    /* MTX_POP */
        s32 c = (s32)(par[0] & 0x3Fu);
        if (c & 0x20) {
            c -= 0x40;
        }
        if (st->mode == G3D_MTX_MODE_PROJECTION) {
            if (!g_hw.pj_level) {
                g_hw.pj_error = 1;
            }
            g_hw.pj_level = 0;
            run_cmd(op, par, n);
        } else if (st->mode == G3D_MTX_MODE_TEXTURE ||
                   (c > 0 && (u32)c <= st->sp)) {
            run_cmd(op, par, n);
        } else {
            s32 target = (s32)st->sp - c;
            if (target < 0 || target >= (s32)G3D_POS_STACK_DEPTH) {
                st->error = 1;
            } else {
                st->sp = (u32)target;
                st->pos = st->pos_stack[st->sp];
                st->vec = st->vec_stack[st->sp];
                st->clip_dirty = 1;
            }
        }
        break;
    }
    case 0x21:      /* NORMAL: the lighting equation under a lit polygon */
        run_cmd(op, par, n);
        if (G3D_ATTR_LIGHTS(g_hw.gx.poly_attr) != 0) {
            set_color(g3d_light_normal(&g_hw.light, &st->vec, par[0],
                                       G3D_ATTR_LIGHTS(g_hw.gx.poly_attr)));
        }
        break;
    case 0x30:      /* DIF_AMB, bit 15: the diffuse colour is the vertex's */
        if (g3d_light_dif_amb(&g_hw.light, par[0])) {
            set_color((u16)(par[0] & 0x7FFFu));
        }
        break;
    case 0x31:      /* SPE_EMI */
        g3d_light_spe_emi(&g_hw.light, par[0]);
        break;
    case 0x32:      /* LIGHT_VECTOR, through the vector matrix now */
        g3d_light_vector(&g_hw.light, &st->vec, par[0]);
        break;
    case 0x33:      /* LIGHT_COLOR */
        g3d_light_color(&g_hw.light, par[0]);
        break;
    case 0x34:      /* SHININESS */
        g3d_light_shininess(&g_hw.light, par);
        break;
    case 0x40:      /* BEGIN_VTXS: closes a list left open */
        if (g_hw.gx.batch_open) {
            run_cmd(0x41u, NULL, 0);
        }
        run_cmd(op, par, n);
        break;
    case 0x50:      /* SWAP_BUFFERS */
        swap_buffers(par[0]);
        break;
    case 0x60:      /* VIEWPORT */
        g_hw.viewport = par[0];
        break;
    case 0x70:      /* BOX_TEST */
        box_test(par);
        break;
    case 0x71:      /* POS_TEST */
        pos_test(par);
        break;
    case 0x72:      /* VEC_TEST */
        vec_test(par);
        break;
    default:
        run_cmd(op, par, n);
        break;
    }
    adjust_viewport();
    publish_stat();
    publish_ram_count();
}

/* The packed FIFO: a command word of four opcodes, then each non-NOP
 * opcode's parameters in order.  A zero-parameter opcode executes as soon
 * as it is reached; an undefined opcode byte is skipped. */
static void fifo_advance(void)
{
    while (g_hw.slot < 4u) {
        u32 op = g_hw.ops[g_hw.slot];
        u32 n;

        if (op == 0) {
            g_hw.slot++;
            continue;
        }
        n = g3d_gx_param_count(op);
        if (n == 0xFFu) {
            g_hw.slot++;
            continue;
        }
        if (n == 0) {
            exec(op, g_hw.par);
            g_hw.slot++;
            continue;
        }
        g_hw.need = n;
        g_hw.have = 0;
        return;
    }
    g_hw.in_packet = 0;
}

static void fifo_word(u32 w)
{
    g_ring[g_ring_n++ & 63u] = ((unsigned long long)g_ring_src << 32) | w;
    if (!g_hw.in_packet) {
        g_hw.ops[0] = (u8)(w & 0xFFu);
        g_hw.ops[1] = (u8)((w >> 8) & 0xFFu);
        g_hw.ops[2] = (u8)((w >> 16) & 0xFFu);
        g_hw.ops[3] = (u8)((w >> 24) & 0xFFu);
        g_hw.slot = 0;
        g_hw.in_packet = 1;
        g_hw.par[0] = 0;
        fifo_advance();
        return;
    }
    g_hw.par[g_hw.have++] = w;
    if (g_hw.have == g_hw.need) {
        exec(g_hw.ops[g_hw.slot], g_hw.par);
        g_hw.slot++;
        fifo_advance();
    }
}

/* A direct port: the address names the opcode, each store is one parameter
 * (a zero-parameter command takes one store of any value). */
static void port_word(u32 op, u32 w)
{
    u32 n = g3d_gx_param_count(op);

    if (n == 0xFFu) {
        return;
    }
    if (n == 0) {
        g_hw.d_par[0] = w;
        exec(op, g_hw.d_par);
        return;
    }
    if (op != g_hw.d_op) {
        g_hw.d_op = op;
        g_hw.d_have = 0;
    }
    g_hw.d_par[g_hw.d_have++] = w;
    if (g_hw.d_have == n) {
        g_hw.d_have = 0;
        exec(op, g_hw.d_par);
    }
}

static void store(u32 addr, u32 w)
{
    if (g_tr.on) {
        if (addr < G3D_HW_PORTS) {
            g_tr.w_fifo++;
        } else {
            g_tr.w_port++;
        }
    }
    if (addr < G3D_HW_PORTS) {
        fifo_word(w);
    } else {
        port_word((addr - G3D_HW_GXFIFO) >> 2, w);
    }
}

/* ------------------------------------------------------------------ */
/* The access protocol                                                 */
/* ------------------------------------------------------------------ */

static void resolve_pending(void)
{
    u32 p = g_hw.pending, v;

    if (p == 0) {
        return;
    }
    g_hw.pending = 0;
    v = bw_io_read32(p);
    if (p == G3D_HW_GXSTAT) {
        if (v != g_hw.stat_published) {
            gxstat_written(v);
            publish_stat();
        }
        return;
    }
    if (v != G3D_HW_SENTINEL) {
        g_ring_src = 1;
        store(p, v);
        g_ring_src = 0;
    }
}

static void refresh_for(u32 a)
{
    if (a < G3D_HW_RAM_COUNT) {
        publish_stat();
    } else if (a < G3D_HW_DISP_1DOT) {
        publish_ram_count();
    } else if (a >= G3D_HW_CLIPMTX_RESULT && a < G3D_HW_VECMTX_RESULT) {
        publish_clip();
    } else if (a >= G3D_HW_VECMTX_RESULT) {
        publish_vecmtx();
    }
    /* DISP_1DOT_DEPTH is a plain write-only latch, and POS/VEC_RESULT are
     * written when their test runs. */
}

static void access_hook(u32 addr, int sync)
{
    u32 a = addr & ~3u;

    resolve_pending();
    if (sync) {
        return;
    }
    if (a < G3D_HW_GXSTAT) {
        g_hw.pending = a;
        bw_io_write32(a, G3D_HW_SENTINEL);
        return;
    }
    refresh_for(a);
    if (a == G3D_HW_GXSTAT) {
        g_hw.pending = a;
    }
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

void g3d_hw_reset(void)
{
    G3dTarget front = g_hw.front;
    G3dGeom geom = g_hw.geom;
    TexCacheEntry *cache = g_hw.cache;
    u32 cap = g_hw.cap_cache;

    cache_clear();
    memset(&g_hw, 0, sizeof(g_hw));
    g_hw.front = front;
    g_hw.geom = geom;
    g_hw.geom.n_verts = g_hw.geom.n_idx = g_hw.geom.n_polys = 0;
    g_hw.cache = cache;
    g_hw.cap_cache = cap;
    g3d_gx_init(&g_hw.gx, &g_hw.geom);
    g3d_light_init(&g_hw.light);
    g_hw.viewport = FULL_VIEWPORT;
    if (g_hw.front.rgba != NULL) {
        g3d_target_clear(&g_hw.front, 0, 0, 0, 0);
    }
    g_hw.ready = 1;

    bw_io_access_hook(G3D_HW_GXFIFO, G3D_HW_WINDOW_END, access_hook);
    publish_stat();
    publish_ram_count();
}

void g3d_hw_flush(void)
{
    if (g_hw.ready) {
        resolve_pending();
    }
}

void g3d_hw_fifo_write(u32 word)
{
    if (!g_hw.ready) {
        return;
    }
    resolve_pending();
    if (g_tr.on) {
        g_tr.w_ext++;
    }
    fifo_word(word);
}

void g3d_hw_fifo_send(const void *src, u32 bytes)
{
    const u8 *p = (const u8 *)src;
    u32 i;

    if (!g_hw.ready || src == NULL) {
        return;
    }
    resolve_pending();
    if (g_tr.on) {
        g_tr.w_send += bytes / 4u;
    }
    for (i = 0; i + 4u <= bytes; i += 4u) {
        fifo_word(bw_rd32(p + i));
    }
}

void g3d_hw_port_write(u32 addr, u32 value)
{
    if (!g_hw.ready || addr < G3D_HW_GXFIFO || addr >= G3D_HW_GXSTAT) {
        return;
    }
    resolve_pending();
    store(addr & ~3u, value);
}

static u32 ds_addr_of_ptr(const volatile void *p)
{
    u32 a = bw_ds_addr_of((const void *)p);

    if (a == 0) {
        /* The raw DS value a macro or an integer argument carries. */
        uintptr_t raw = (uintptr_t)p;
        if (raw >= G3D_HW_GXFIFO && raw < G3D_HW_WINDOW_END) {
            a = (u32)raw;
        }
    }
    return a;
}

int g3d_hw_is_fifo(const volatile void *dst)
{
    u32 a;

    if (!g_hw.ready || dst == NULL) {
        return 0;
    }
    a = ds_addr_of_ptr(dst);
    return a >= G3D_HW_GXFIFO && a < G3D_HW_PORTS;
}

void g3d_hw_claim(const volatile void *dst)
{
    u32 a;

    if (!g_hw.ready || g_hw.pending == 0) {
        return;
    }
    a = ds_addr_of_ptr(dst) & ~3u;
    if (a == g_hw.pending && bw_io_read32(a) == G3D_HW_SENTINEL) {
        /* The access made the pointer; nothing was stored through it. */
        g_hw.pending = 0;
        return;
    }
    resolve_pending();
}

u32 g3d_hw_result_addr(const volatile void *src)
{
    u32 a;

    if (!g_hw.ready || src == NULL) {
        return 0;
    }
    a = ds_addr_of_ptr(src);
    return (a >= G3D_HW_GXSTAT && a < G3D_HW_WINDOW_END) ? a : 0u;
}

void g3d_hw_refresh(void)
{
    if (!g_hw.ready) {
        return;
    }
    resolve_pending();
    publish_stat();
    publish_ram_count();
    publish_clip();
    publish_vecmtx();
}

void g3d_hw_set_scale(u32 scale)
{
    g_scale = scale == 0 ? 1u : scale;
}

void g3d_hw_set_view_scale(s32 kx, s32 ky)
{
    g3d_mtx_set_view_scale((fx32)kx, (fx32)ky);
}

void g3d_hw_view_scale(s32 *kx, s32 *ky)
{
    fx32 x, y;

    g3d_mtx_view_scale(&x, &y);
    if (kx != NULL) {
        *kx = (s32)x;
    }
    if (ky != NULL) {
        *ky = (s32)y;
    }
}

u32 g3d_hw_scale(void)
{
    return g_scale;
}

u32 g3d_hw_frame_scale(void)
{
    return g_hw.have_frame ? g_hw.front.width / G3D_SCREEN_W : g_scale;
}

const u8 *g3d_hw_frame(void)
{
    return g_hw.have_frame ? g_hw.front.rgba : NULL;
}

u32 g3d_hw_frame_count(void)
{
    return g_hw.frames;
}

u32 g3d_hw_unsupported(void)
{
    return g_hw.unsup_frame;
}

u32 g3d_hw_gxstat(void)
{
    if (g_hw.ready) {
        resolve_pending();
    }
    return gxstat_value(1);
}

const G3dGx *g3d_hw_gx(void)
{
    return &g_hw.gx;
}
