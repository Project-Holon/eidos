/*
 * engine/g3d/gx.c -- the geometry engine's display-list interpreter.
 *
 * A display list is a stream of packed command words: one u32 holding
 * four opcode bytes (0x00 is NOP and is the padding when a group does
 * not need all four slots), immediately followed by every non-NOP
 * opcode's parameter words in order.  That framing, the opcode table and
 * its parameter counts, and the vertex command encodings are public NDS
 * hardware documentation; aletheia's tools/assets/nsbmd.py decodes the
 * same streams for the asset lane and aletheia's docs/ASSETS.md records
 * that real map models overwhelmingly use the compact VTX_10 / VTX_XY /
 * VTX_XZ / VTX_YZ / VTX_DIFF encodings rather than VTX_16, so a decoder
 * that understood only VTX_16 would emit almost no geometry for them.
 *
 * The "current vertex" register persists across commands and is the
 * whole reason the compact forms work: VTX_XY, VTX_XZ and VTX_YZ each
 * set two components and carry the third over, and VTX_DIFF adds a small
 * signed delta to all three.  Every one of the six emits a vertex.
 *
 * Scaling, in fx32 (1.19.12) throughout, no float anywhere:
 *
 *   VTX_16    three s16 values that *are* fx32 already (s16.12)
 *   VTX_10    three s10 values with 6 fractional bits: << 6
 *   VTX_XY/XZ/YZ  two s16.12 values, the third carried over
 *   VTX_DIFF  three s10 deltas in 4.12 units (a range of +-1/8): no shift
 *   NORMAL    three s10 values with 9 fractional bits: << 3
 *   TEXCOORD  two s16.4 values, kept raw, so the unit here is a
 *             sixteenth of a texel -- the same unit the OBJ the asset
 *             lane exports carries before it divides by the texture size
 *   COLOR     three 5-bit channels, expanded c * 255 / 31, the same
 *             expansion engine/g2d uses for a 2D palette
 *
 * The texture matrix is NOT applied to TEXCOORD here.  TEXIMAGE_PARAM's
 * bits 30-31 select a coordinate transform source, and no map model in
 * the corpus was found to need one; aletheia's map_shapes.py records the
 * same finding and the same gap.  Applying an identity-by-assumption
 * matrix would be a guess, so the coordinate arrives at the rasteriser
 * exactly as the command carried it, and the divergence, if any model
 * ever has one, shows up as a texcoord mismatch against the oracle
 * rather than as a silently wrong picture.
 */

#include <stdlib.h>
#include <string.h>

#include "g3d.h"

/* ------------------------------------------------------------------ */
/* The geometry buffer                                                 */
/* ------------------------------------------------------------------ */

void g3d_geom_init(G3dGeom *g)
{
    memset(g, 0, sizeof(*g));
}

void g3d_geom_free(G3dGeom *g)
{
    if (g == NULL) {
        return;
    }
    free(g->verts);
    free(g->idx);
    free(g->polys);
    memset(g, 0, sizeof(*g));
}

static int grow(void **p, u32 *cap, u32 need, u32 elem)
{
    u32 c = *cap ? *cap : 256u;
    void *n;

    if (need <= *cap) {
        return 0;
    }
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

/* ------------------------------------------------------------------ */
/* The opcode table                                                    */
/* ------------------------------------------------------------------ */

/*
 * Parameter word counts, indexed by opcode.  0xFF marks an opcode the
 * hardware does not define: hitting one is how the end of a real display
 * list is detected, because a stream is followed by bytes that are not
 * commands.
 */
#define G3D_OPCODE_LIMIT 0x78u

static const u8 g3d_param_count[G3D_OPCODE_LIMIT] = {
    /* 0x00 */ 0,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x08 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x10 */ 1,    0,    1,    1,    1,    0,    16,   12,
    /* 0x18 */ 16,   12,   9,    3,    3,    0xFF, 0xFF, 0xFF,
    /* 0x20 */ 1,    1,    1,    2,    1,    1,    1,    1,
    /* 0x28 */ 1,    1,    1,    1,    0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x30 */ 1,    1,    1,    1,    32,   0xFF, 0xFF, 0xFF,
    /* 0x38 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x40 */ 1,    0,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x48 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x50 */ 1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x58 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x60 */ 1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x68 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x70 */ 3,    2,    1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x73 upwards, and every byte from 0x78 to 0xFF, are undefined:
     * see param_count() below, which is what keeps a byte the table does
     * not reach from reading as a valid zero-parameter command. */
};

static u32 param_count(u8 op)
{
    return op < G3D_OPCODE_LIMIT ? g3d_param_count[op] : 0xFFu;
}

u32 g3d_gx_param_count(u32 op)
{
    return op <= 0xFFu ? param_count((u8)op) : 0xFFu;
}

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
/* The interpreter                                                     */
/* ------------------------------------------------------------------ */

void g3d_gx_init(G3dGx *gx, G3dGeom *out)
{
    memset(gx, 0, sizeof(*gx));
    g3d_mtx_stack_init(&gx->mtx);
    gx->out = out;
    gx->cr = gx->cg = gx->cb = 255;
    gx->poly_attr = 0;
    gx->poly_attr_pending = 0;
    gx->begin_mode = G3D_PRIM_TRIANGLES;
}

static void emit_vertex(G3dGx *gx)
{
    G3dGeom *g = gx->out;
    G3dVertex *v;

    gx->batch_count++;
    if (g == NULL) {
        return;
    }
    if (grow((void **)&g->verts, &g->cap_verts, g->n_verts + 1u,
             (u32)sizeof(*g->verts)) != 0) {
        return;
    }
    v = &g->verts[g->n_verts++];
    v->ox = gx->cx;
    v->oy = gx->cy;
    v->oz = gx->cz;
    g3d_mtx_xform(g3d_mtx_stack_clip(&gx->mtx), gx->cx, gx->cy, gx->cz,
                  &v->x, &v->y, &v->z, &v->w);
    v->s = gx->cs;
    v->t = gx->ct;
    if ((gx->teximage >> 30) == 1u) {
        /* [HW] TEXIMAGE_PARAM bits 30-31, mode 1: the texture matrix
         * transforms the texcoord.  The vector is (S, T, 1/16, 1/16) in
         * the 1.11.4 format and the matrix is 1.19.12, so a raw sixteenth
         * of a texel is S' = (S*M0 + T*M4 + M8 + M12) >> 12 (GBATEK). */
        const fx32 *m = gx->mtx.tex.m;
        s64 s = gx->cs, t = gx->ct;

        v->s = (s32)((s * m[0] + t * m[4] + m[8] + m[12]) >> 12);
        v->t = (s32)((s * m[1] + t * m[5] + m[9] + m[13]) >> 12);
    }
    v->r = gx->cr;
    v->g = gx->cg;
    v->b = gx->cb;
}

static void add_poly(G3dGx *gx, const u32 *vi, u32 n)
{
    G3dGeom *g = gx->out;
    G3dPoly *p;
    u32 i;

    if (g == NULL) {
        return;
    }
    if (grow((void **)&g->polys, &g->cap_polys, g->n_polys + 1u,
             (u32)sizeof(*g->polys)) != 0 ||
        grow((void **)&g->idx, &g->cap_idx, g->n_idx + n,
             (u32)sizeof(*g->idx)) != 0) {
        return;
    }
    p = &g->polys[g->n_polys++];
    p->first = g->n_idx;
    p->count = n;
    p->attr = gx->poly_attr;
    p->teximage = gx->teximage;
    p->pltt_base = gx->pltt_base;
    for (i = 0; i < n; i++) {
        g->idx[g->n_idx++] = vi[i];
    }
}

/*
 * END_VTXS: fan the batch out into triangles and quads.  The four
 * primitive kinds and the strip winding (an odd triangle of a strip has
 * its last two vertices swapped so every face keeps one orientation, and
 * a quad strip's four consecutive vertices go out as a, b, d, c) are the
 * documented hardware ones, and are what aletheia's decoder assembles
 * too, which is what makes the face counts comparable against its OBJ.
 */
static void close_batch(G3dGx *gx)
{
    u32 base = gx->batch_first;
    u32 n = gx->batch_count;
    u32 i, f[4];

    gx->batch_open = 0;
    switch (gx->begin_mode) {
    case G3D_PRIM_TRIANGLES:
        for (i = 0; i + 3u <= n; i += 3u) {
            f[0] = base + i; f[1] = base + i + 1u; f[2] = base + i + 2u;
            add_poly(gx, f, 3u);
        }
        break;
    case G3D_PRIM_QUADS:
        for (i = 0; i + 4u <= n; i += 4u) {
            f[0] = base + i; f[1] = base + i + 1u;
            f[2] = base + i + 2u; f[3] = base + i + 3u;
            add_poly(gx, f, 4u);
        }
        break;
    case G3D_PRIM_TRISTRIP:
        for (i = 0; i + 3u <= n; i++) {
            if (i & 1u) {
                f[0] = base + i; f[1] = base + i + 2u; f[2] = base + i + 1u;
            } else {
                f[0] = base + i; f[1] = base + i + 1u; f[2] = base + i + 2u;
            }
            add_poly(gx, f, 3u);
        }
        break;
    default:
        for (i = 0; i + 4u <= n; i += 2u) {
            f[0] = base + i; f[1] = base + i + 1u;
            f[2] = base + i + 3u; f[3] = base + i + 2u;
            add_poly(gx, f, 4u);
        }
        break;
    }
    gx->batch_count = 0;
}

static void load_mtx(G3dGx *gx, const u32 *p, u32 n, int mult)
{
    G3dMtx m;
    u32 i;

    if (n == 16u) {
        for (i = 0; i < 16u; i++) {
            m.m[i] = (fx32)p[i];
        }
    } else if (n == 12u) {
        for (i = 0; i < 4u; i++) {
            m.m[i * 4 + 0] = (fx32)p[i * 3 + 0];
            m.m[i * 4 + 1] = (fx32)p[i * 3 + 1];
            m.m[i * 4 + 2] = (fx32)p[i * 3 + 2];
            m.m[i * 4 + 3] = (i == 3u) ? FX32_ONE : 0;
        }
    } else {                    /* 3x3 */
        g3d_mtx_identity(&m);
        for (i = 0; i < 3u; i++) {
            m.m[i * 4 + 0] = (fx32)p[i * 3 + 0];
            m.m[i * 4 + 1] = (fx32)p[i * 3 + 1];
            m.m[i * 4 + 2] = (fx32)p[i * 3 + 2];
        }
    }
    if (mult) {
        g3d_mtx_stack_mult(&gx->mtx, &m);
    } else {
        g3d_mtx_stack_load(&gx->mtx, &m);
    }
}

/* One command: `op` with its parameter words (g3d_gx_param_count(op) of
 * them).  g3d_gx_run calls it for each command of a display list; the
 * hardware path (hw3d.c) for each command of the FIFO and the ports. */
void g3d_gx_exec(G3dGx *gx, u32 op, const u32 *par)
{
    gx->commands++;

    switch (op) {
    case 0x10:  /* MTX_MODE */
        g3d_mtx_stack_set_mode(&gx->mtx, par[0] & 3u);
        break;
    case 0x11:  /* MTX_PUSH */
        g3d_mtx_stack_push(&gx->mtx);
        break;
    case 0x12: { /* MTX_POP, a signed 6-bit count */
        s32 c = (s32)(par[0] & 0x3Fu);
        if (c & 0x20) {
            c -= 0x40;
        }
        g3d_mtx_stack_pop(&gx->mtx, c);
        break;
    }
    case 0x13:  /* MTX_STORE */
        g3d_mtx_stack_store(&gx->mtx, par[0] & 0x1Fu);
        break;
    case 0x14:  /* MTX_RESTORE */
        g3d_mtx_stack_restore(&gx->mtx, par[0] & 0x1Fu);
        break;
    case 0x15:  /* MTX_IDENTITY */
        g3d_mtx_stack_load_identity(&gx->mtx);
        break;
    case 0x16:  /* MTX_LOAD_4x4 */
        load_mtx(gx, par, 16u, 0);
        break;
    case 0x17:  /* MTX_LOAD_4x3 */
        load_mtx(gx, par, 12u, 0);
        break;
    case 0x18:  /* MTX_MULT_4x4 */
        load_mtx(gx, par, 16u, 1);
        break;
    case 0x19:  /* MTX_MULT_4x3 */
        load_mtx(gx, par, 12u, 1);
        break;
    case 0x1A:  /* MTX_MULT_3x3 */
        load_mtx(gx, par, 9u, 1);
        break;
    case 0x1B: { /* MTX_SCALE */
        /* In position+vector mode the hardware scales the position
         * matrix only: a scale would denormalise the directional
         * matrix the lights use. */
        G3dMtx m;
        u32 mode = gx->mtx.mode;
        g3d_mtx_scale(&m, (fx32)par[0], (fx32)par[1], (fx32)par[2]);
        if (mode == G3D_MTX_MODE_POSVEC) {
            gx->mtx.mode = G3D_MTX_MODE_POSITION;
        }
        g3d_mtx_stack_mult(&gx->mtx, &m);
        gx->mtx.mode = mode;
        break;
    }
    case 0x1C: { /* MTX_TRANS */
        G3dMtx m;
        g3d_mtx_translate(&m, (fx32)par[0], (fx32)par[1],
                          (fx32)par[2]);
        g3d_mtx_stack_mult(&gx->mtx, &m);
        break;
    }

    case 0x20:  /* COLOR */
        gx->cr = c5to8(par[0]);
        gx->cg = c5to8(par[0] >> 5);
        gx->cb = c5to8(par[0] >> 10);
        break;
    case 0x21:  /* NORMAL */
        gx->normal.x = (fx32)(sx10(par[0]) << 3);
        gx->normal.y = (fx32)(sx10(par[0] >> 10) << 3);
        gx->normal.z = (fx32)(sx10(par[0] >> 20) << 3);
        break;
    case 0x22:  /* TEXCOORD, s16.4 texel units */
        gx->cs = sx16(par[0]);
        gx->ct = sx16(par[0] >> 16);
        break;
    case 0x23:  /* VTX_16 */
        gx->cx = (fx32)sx16(par[0]);
        gx->cy = (fx32)sx16(par[0] >> 16);
        gx->cz = (fx32)sx16(par[1]);
        gx->vtx_cmd_count[0]++;
        emit_vertex(gx);
        break;
    case 0x24:  /* VTX_10 */
        gx->cx = (fx32)(sx10(par[0]) << 6);
        gx->cy = (fx32)(sx10(par[0] >> 10) << 6);
        gx->cz = (fx32)(sx10(par[0] >> 20) << 6);
        gx->vtx_cmd_count[1]++;
        emit_vertex(gx);
        break;
    case 0x25:  /* VTX_XY */
        gx->cx = (fx32)sx16(par[0]);
        gx->cy = (fx32)sx16(par[0] >> 16);
        gx->vtx_cmd_count[2]++;
        emit_vertex(gx);
        break;
    case 0x26:  /* VTX_XZ */
        gx->cx = (fx32)sx16(par[0]);
        gx->cz = (fx32)sx16(par[0] >> 16);
        gx->vtx_cmd_count[3]++;
        emit_vertex(gx);
        break;
    case 0x27:  /* VTX_YZ */
        gx->cy = (fx32)sx16(par[0]);
        gx->cz = (fx32)sx16(par[0] >> 16);
        gx->vtx_cmd_count[4]++;
        emit_vertex(gx);
        break;
    case 0x28:  /* VTX_DIFF */
        /* [HW] the deltas are s10 in the vertex's own 4.12 units:
         * at most an eighth of a unit either way, not scaled up */
        gx->cx += (fx32)sx10(par[0]);
        gx->cy += (fx32)sx10(par[0] >> 10);
        gx->cz += (fx32)sx10(par[0] >> 20);
        gx->vtx_cmd_count[5]++;
        emit_vertex(gx);
        break;
    case 0x29:  /* POLYGON_ATTR: takes effect at the next BEGIN */
        gx->poly_attr_pending = par[0];
        break;
    case 0x2A:  /* TEXIMAGE_PARAM */
        gx->teximage = par[0];
        break;
    case 0x2B:  /* PLTT_BASE */
        gx->pltt_base = par[0];
        break;

    case 0x40:  /* BEGIN_VTXS */
        /* A batch that never met its END_VTXS emits nothing: the
         * hardware needs the END to draw, and the asset lane's
         * decoder drops such a batch too, which is what keeps the
         * face counts comparable against its OBJ. */
        gx->batch_open = 0;
        gx->poly_attr = gx->poly_attr_pending;
        gx->begin_mode = par[0] & 3u;
        gx->batch_first = (gx->out != NULL) ? gx->out->n_verts : 0u;
        gx->batch_count = 0;
        gx->batch_open = 1;
        break;
    case 0x41:  /* END_VTXS */
        if (gx->batch_open) {
            close_batch(gx);
        }
        break;

    default:
        /* Lighting, material, viewport, swap-buffers and the box
         * tests: consumed with the right parameter count so the
         * stream stays aligned, and otherwise not interpreted.
         * A static map model's picture does not depend on them,
         * and inventing lighting the game has not been shown to
         * enable would be a guess. */
        break;
    }
}

u32 g3d_gx_run(G3dGx *gx, const u8 *data, u32 size, u32 start,
               u32 soft_limit, u32 hard_limit)
{
    u32 p = start;

    if (hard_limit > size) {
        hard_limit = size;
    }
    if (soft_limit > size) {
        soft_limit = size;
    }

    while (p + 4u <= size) {
        u32 word, np, k;
        u8 ops[4];
        int bad = 0;

        if (p >= hard_limit) {
            break;
        }
        word = bw_rd32(data + p);
        ops[0] = (u8)(word & 0xFFu);
        ops[1] = (u8)((word >> 8) & 0xFFu);
        ops[2] = (u8)((word >> 16) & 0xFFu);
        ops[3] = (u8)((word >> 24) & 0xFFu);
        for (k = 0; k < 4u; k++) {
            if (param_count(ops[k]) == 0xFFu) {
                bad = 1;
            }
        }
        if (bad) {
            break;
        }

        np = p + 4u;
        for (k = 0; k < 4u && !bad; k++) {
            u8 op = ops[k];
            u32 npar = param_count(op);
            u32 par[32];
            u32 j;

            if (op == 0) {
                continue;
            }
            if (np + 4u * npar > size) {
                bad = 1;
                break;
            }
            for (j = 0; j < npar; j++) {
                par[j] = bw_rd32(data + np + j * 4u);
            }
            np += 4u * npar;
            g3d_gx_exec(gx, op, par);
        }
        if (bad) {
            break;
        }
        p = np;
        if (p >= soft_limit && !gx->batch_open) {
            break;
        }
    }
    /* An open batch at the stop point is deliberately not closed: see
     * BEGIN_VTXS above. */
    return p;
}

u32 g3d_gx_find_stream(const u8 *data, u32 size, u32 start, u32 min_commands)
{
    u32 off;

    for (off = start; off + 4u <= size; off += 4u) {
        G3dGx probe;

        g3d_gx_init(&probe, NULL);
        (void)g3d_gx_run(&probe, data, size, off, size, size);
        if (probe.commands >= min_commands) {
            return off;
        }
    }
    return size;
}
