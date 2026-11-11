/*
 * engine/g2d/compose.c -- the software OBJ compositor.
 *
 * Two operations, and the second is only the first repeated over a node
 * list:
 *
 *   render_oam()  one OAM entry's pixel block, fetched out of the NCGR
 *                 through the OAM's tile index and mirrored per its flip
 *                 bits, with the palette-bank nibble applied as a
 *                 `bank * 16` row offset into the block's palette.
 *   compose()     every node of a multi-cell group, each resolving its
 *                 own NANR sequence's active frame to a cell, painted
 *                 onto one canvas in the attested order.
 *
 * **Affine frames.**  A node whose active NANR frame carries a
 * non-identity (sx, sy, rotZ) does not move its OAMs as rigid rectangles:
 * the SRT block is per node and applies about the cell origin, so the
 * cell is composed at identity by cell_identity_raster() and then
 * resampled as one picture through the frame's 8.8 matrix
 * (g2d_affine_resample, engine/g2d/affine.c).  That is one picture, so it
 * contributes one paint unit at the frontmost priority of the cell's
 * OAMs.
 *
 * **Tile addressing.**  An OAM's tile index is a block index into the
 * sheet's tile_num_x-wide grid of 8x8 blocks, so an OAM taller than one
 * block steps by tile_num_x per block row, not by its own width.  The
 * exception is a sheet that declares the 0xFFFF "unspecified" dimensions:
 * it has no sheet width, and its cell bank resolves tile indices by plain
 * sequential 1D OBJ mapping instead, so the row stride is the OAM's own
 * width in tiles.
 *
 * **Painter's order.**  The composer emits nodes forward and, within one
 * node's cell, that cell's OAMs forward; the earliest-emitted OAM lands
 * in the lowest hardware OAM slot and a lower slot draws in front.  A
 * painter that blits in list order and lets later writes win therefore
 * has to run the *reverse* of the emission order, and attr2's priority
 * outranks the slot index (lower priority value draws in front whatever
 * the index).  So: collect every unit with (priority, emission order),
 * sort descending, paint in that order; the last thing painted is the
 * lowest-priority-value, earliest-emitted unit, which ends on top.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

/* One paint unit: one OAM's pixels at an absolute canvas position. */
typedef struct PaintUnit {
    u16 priority;
    u32 order;
    s32 ax, ay;
    u32 w, h;
    u8 *px;      /* owned, w*h indices */
} PaintUnit;

static void free_units(PaintUnit *units, u32 count)
{
    u32 i;

    for (i = 0; i < count; i++) {
        free(units[i].px);
    }
    free(units);
}

/*
 * One OAM's pixel block, flips applied.  Returns a malloc'd w*h buffer.
 *
 * The palette bank is applied only when the block's palette actually has
 * (bank + 1) * 16 entries: a single 16-colour table has no second bank to
 * offset into, and reading past its end would be worse than ignoring the
 * nibble.  Index 0 is the transparent index and is never offset.
 */
static u8 *render_oam(const G2dNcgr *ncgr, const G2dOam *oam, u32 n_pal_colors)
{
    u32 tiles_w = oam->w / 8u;
    u32 tiles_h = oam->h / 8u;
    u32 row_stride = (ncgr->tile_num_x == G2D_NCGR_DIM_SENTINEL)
                     ? tiles_w : (u32)ncgr->tile_num_x;
    u32 offset = ((u32)oam->pal * 16u + 16u <= n_pal_colors)
                 ? (u32)oam->pal * 16u : 0u;
    u8 *px = (u8 *)calloc((size_t)oam->w * oam->h, 1);
    u32 ty, tx, ly, lx;

    if (px == NULL) {
        return NULL;
    }
    for (ty = 0; ty < tiles_h; ty++) {
        for (tx = 0; tx < tiles_w; tx++) {
            u8 block[64];

            g2d_ncgr_block(ncgr, (u32)oam->tile_idx + ty * row_stride + tx, block);
            for (ly = 0; ly < 8u; ly++) {
                for (lx = 0; lx < 8u; lx++) {
                    u8 v = block[ly * 8u + lx];

                    px[(ty * 8u + ly) * oam->w + tx * 8u + lx] =
                        v ? (u8)(v + offset) : 0;
                }
            }
        }
    }

    if (oam->hflip) {
        for (ly = 0; ly < oam->h; ly++) {
            u8 *row = px + ly * oam->w;

            for (lx = 0; lx < oam->w / 2u; lx++) {
                u8 tmp = row[lx];

                row[lx] = row[oam->w - 1u - lx];
                row[oam->w - 1u - lx] = tmp;
            }
        }
    }
    if (oam->vflip) {
        for (ly = 0; ly < oam->h / 2u; ly++) {
            u8 *a = px + ly * oam->w;
            u8 *b = px + (oam->h - 1u - ly) * oam->w;
            u32 i;

            for (i = 0; i < oam->w; i++) {
                u8 tmp = a[i];

                a[i] = b[i];
                b[i] = tmp;
            }
        }
    }
    return px;
}

/*
 * One cell composed at identity onto its own tight canvas, with the cell
 * origin's position inside it recorded.  This is the input the affine
 * path resamples: the NNS cell animation carries one SRT per node, about
 * the cell origin, so the whole cell is transformed as one picture rather
 * than each OAM as its own rigid rectangle (aletheia's
 * src/matched/unit_020640B0.c and aletheia's docs/ASSETS.md's per-pixel SRT pass).
 *
 * The paint order inside the cell is the same rule as the pose's: emitted
 * forward, so a plain painter runs the reverse, with attr2's priority
 * outranking the index.
 */
static int cell_identity_raster(const G2dNcgr *ncgr, const G2dCell *cell,
                                u32 n_pal_colors, G2dRaster *out)
{
    s32 minx = 0x7FFFFFFF, miny = 0x7FFFFFFF;
    s32 maxx = -0x7FFFFFFF, maxy = -0x7FFFFFFF;
    u32 i, n = cell->num_oam;
    u32 *order;

    memset(out, 0, sizeof(*out));
    if (n == 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        const G2dOam *o = &cell->oams[i];

        if (o->x < minx) { minx = o->x; }
        if (o->y < miny) { miny = o->y; }
        if (o->x + (s32)o->w > maxx) { maxx = o->x + (s32)o->w; }
        if (o->y + (s32)o->h > maxy) { maxy = o->y + (s32)o->h; }
    }
    out->w = (u32)(maxx - minx);
    out->h = (u32)(maxy - miny);
    out->ox = -minx;
    out->oy = -miny;
    out->px = (u8 *)calloc((size_t)out->w * out->h, 1);
    if (out->px == NULL) {
        return -1;
    }

    /* Indices sorted descending by (priority, index): an insertion sort,
     * because a cell's OAM count is small and the order must be stable. */
    order = (u32 *)malloc((size_t)n * sizeof(u32));
    if (order == NULL) {
        g2d_raster_free(out);
        return -1;
    }
    for (i = 0; i < n; i++) {
        u32 j = i;

        while (j > 0) {
            const G2dOam *a = &cell->oams[order[j - 1]];

            if (a->priority > cell->oams[i].priority
                || (a->priority == cell->oams[i].priority
                    && order[j - 1] > i)) {
                break;
            }
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }

    for (i = 0; i < n; i++) {
        const G2dOam *oam = &cell->oams[order[i]];
        u8 *px = render_oam(ncgr, oam, n_pal_colors);
        u32 ly, lx;

        if (px == NULL) {
            free(order);
            g2d_raster_free(out);
            return -1;
        }
        for (ly = 0; ly < oam->h; ly++) {
            s32 py = oam->y - miny + (s32)ly;

            if (py < 0 || (u32)py >= out->h) {
                continue;
            }
            for (lx = 0; lx < oam->w; lx++) {
                u8 v = px[ly * oam->w + lx];
                s32 cxp = oam->x - minx + (s32)lx;

                if (v == 0 || cxp < 0 || (u32)cxp >= out->w) {
                    continue;
                }
                out->px[(u32)py * out->w + (u32)cxp] = v;
            }
        }
        free(px);
    }
    free(order);
    return 0;
}

/* Descending by (priority, emission order): see the painter's-order note. */
static int unit_cmp(const void *a, const void *b)
{
    const PaintUnit *ua = (const PaintUnit *)a;
    const PaintUnit *ub = (const PaintUnit *)b;

    if (ua->priority != ub->priority) {
        return (ua->priority > ub->priority) ? -1 : 1;
    }
    if (ua->order != ub->order) {
        return (ua->order > ub->order) ? -1 : 1;
    }
    return 0;
}

static int compose_nodes(const G2dNcgr *ncgr, const G2dNcer *ncer,
                         const G2dNanr *nanr, const G2dMcGroup *group,
                         const G2dNodeFrame *given, u32 t, u32 n_pal_colors, G2dPose *out);

int g2d_compose_pose(const G2dNcgr *ncgr, const G2dNcer *ncer,
                     const G2dNanr *nanr, const G2dMcGroup *group,
                     u32 t, u32 n_pal_colors, G2dPose *out)
{
    return compose_nodes(ncgr, ncer, nanr, group, NULL, t, n_pal_colors, out);
}

int g2d_compose_pose_frames(const G2dNcgr *ncgr, const G2dNcer *ncer,
                            const G2dNanr *nanr, const G2dMcGroup *group,
                            const G2dNodeFrame *frames, u32 n_pal_colors, G2dPose *out)
{
    return compose_nodes(ncgr, ncer, nanr, group, frames, 0, n_pal_colors, out);
}

/* `given` NULL: every node on its own sequence's frame at time `t`. */
static int compose_nodes(const G2dNcgr *ncgr, const G2dNcer *ncer,
                         const G2dNanr *nanr, const G2dMcGroup *group,
                         const G2dNodeFrame *given, u32 t, u32 n_pal_colors, G2dPose *out)
{
    PaintUnit *units;
    u32 count = 0;
    u32 capacity;
    u32 i;
    s32 minx = 0x7FFFFFFF, miny = 0x7FFFFFFF;
    s32 maxx = -0x7FFFFFFF, maxy = -0x7FFFFFFF;
    const s32 margin = 2;
    s32 ox, oy;

    memset(out, 0, sizeof(*out));
    if (group == NULL || group->num_nodes == 0) {
        return -1;
    }

    /* An upper bound: every node contributes at most its cell's OAMs. */
    capacity = 0;
    for (i = 0; i < ncer->num_cells; i++) {
        if (ncer->cells[i].num_oam > capacity) {
            capacity = ncer->cells[i].num_oam;
        }
    }
    capacity = (capacity + 1u) * group->num_nodes;
    units = (PaintUnit *)calloc(capacity, sizeof(PaintUnit));
    if (units == NULL) {
        return -1;
    }

    for (i = 0; i < group->num_nodes; i++) {
        const G2dMcNode *node = &group->nodes[i];
        G2dFrame frame;
        const G2dCell *cell;
        u32 frame_idx, seq;
        u32 j;

        if (nanr == NULL) {
            continue;
        }
        seq = given != NULL ? given[i].seq : node->anim_seq_idx;
        frame_idx = given != NULL ? given[i].frame : g2d_nanr_frame_at_time(nanr, seq, t);
        if (g2d_nanr_frame_data(nanr, seq, frame_idx, &frame) != 0) {
            continue;   /* a node whose sequence has no such frame is skipped */
        }
        if (frame.cell_idx >= ncer->num_cells) {
            continue;
        }
        cell = &ncer->cells[frame.cell_idx];
        if (cell->num_oam == 0) {
            continue;
        }
        if (!g2d_affine_is_identity(frame.sx, frame.sy, frame.rot_z)) {
            /*
             * A non-identity frame transforms the whole cell as one
             * picture: compose it at identity, resample it through the
             * frame's 8.8 matrix about the cell origin, and contribute
             * the result as a single paint unit placed at the node's own
             * translation.  Its priority is the frontmost of the cell's
             * OAMs, because the cell is now indivisible.
             */
            G2dRaster src, dst;
            PaintUnit *u = &units[count];
            u16 prio = cell->oams[0].priority;

            for (j = 1; j < cell->num_oam; j++) {
                if (cell->oams[j].priority < prio) {
                    prio = cell->oams[j].priority;
                }
            }
            if (cell_identity_raster(ncgr, cell, n_pal_colors, &src) != 0) {
                free_units(units, count);
                return -1;
            }
            if (g2d_affine_resample(src.px, src.w, src.h, src.ox, src.oy,
                                    frame.sx, frame.sy, frame.rot_z,
                                    &dst) != 0) {
                g2d_raster_free(&src);
                free_units(units, count);
                return -1;
            }
            g2d_raster_free(&src);

            u->px = dst.px;
            u->priority = prio;
            u->order = count;
            u->ax = (s32)node->x + frame.px - dst.ox;
            u->ay = (s32)node->y + frame.py - dst.oy;
            u->w = dst.w;
            u->h = dst.h;
            if (u->ax < minx) { minx = u->ax; }
            if (u->ay < miny) { miny = u->ay; }
            if (u->ax + (s32)u->w > maxx) { maxx = u->ax + (s32)u->w; }
            if (u->ay + (s32)u->h > maxy) { maxy = u->ay + (s32)u->h; }
            count++;
            continue;
        }
        for (j = 0; j < cell->num_oam; j++) {
            const G2dOam *oam = &cell->oams[j];
            PaintUnit *u = &units[count];

            u->px = render_oam(ncgr, oam, n_pal_colors);
            if (u->px == NULL) {
                free_units(units, count);
                return -1;
            }
            u->priority = oam->priority;
            u->order = count;
            u->ax = (s32)node->x + frame.px + oam->x;
            u->ay = (s32)node->y + frame.py + oam->y;
            u->w = oam->w;
            u->h = oam->h;
            if (u->ax < minx) { minx = u->ax; }
            if (u->ay < miny) { miny = u->ay; }
            if (u->ax + (s32)u->w > maxx) { maxx = u->ax + (s32)u->w; }
            if (u->ay + (s32)u->h > maxy) { maxy = u->ay + (s32)u->h; }
            count++;
        }
    }

    if (count == 0 || minx > maxx) {
        free_units(units, count);
        out->width = 1;
        out->height = 1;
        out->indices = (u8 *)calloc(1, 1);
        return out->indices != NULL ? 0 : -1;
    }

    ox = -minx + margin;
    oy = -miny + margin;
    out->ox = ox;
    out->oy = oy;
    out->width = (u32)(maxx - minx) + (u32)margin * 2u;
    out->height = (u32)(maxy - miny) + (u32)margin * 2u;
    out->indices = (u8 *)calloc((size_t)out->width * out->height, 1);
    if (out->indices == NULL) {
        free_units(units, count);
        return -1;
    }

    qsort(units, count, sizeof(PaintUnit), unit_cmp);
    for (i = 0; i < count; i++) {
        const PaintUnit *u = &units[i];
        u32 ly;

        for (ly = 0; ly < u->h; ly++) {
            s32 py = u->ay + oy + (s32)ly;
            u32 lx;

            if (py < 0 || (u32)py >= out->height) {
                continue;
            }
            for (lx = 0; lx < u->w; lx++) {
                u8 v = u->px[ly * u->w + lx];
                s32 px = u->ax + ox + (s32)lx;

                if (v == 0) {
                    continue;
                }
                if (px >= 0 && (u32)px < out->width) {
                    out->indices[(u32)py * out->width + (u32)px] = v;
                }
            }
        }
    }
    free_units(units, count);
    return 0;
}

void g2d_pose_free(G2dPose *pose)
{
    free(pose->indices);
    pose->indices = NULL;
    pose->width = 0;
    pose->height = 0;
}

void g2d_pose_to_rgba(const G2dPose *pose, const G2dPalette *pal, u8 *rgba)
{
    u32 n = pose->width * pose->height;
    u32 i;

    for (i = 0; i < n; i++) {
        u8 idx = pose->indices[i];
        u8 r, g, b;

        if (idx == 0) {
            r = 16; g = 16; b = 16;          /* the background index */
        } else if ((u32)idx >= pal->count) {
            r = 255; g = 0; b = 255;         /* past the palette: loud */
        } else {
            r = pal->colors[idx].r;
            g = pal->colors[idx].g;
            b = pal->colors[idx].b;
        }
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }
}
