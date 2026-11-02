/*
 * engine/g2d/resources.c -- the Nitro 2D resource parsers: NCLR palettes,
 * NCGR character data, NCER cell banks, NANR cell animations and NMCR
 * multi-cell node tables.
 *
 * Every layout here is the one aletheia recovered and attests in
 * aletheia's docs/ASSETS.md and in the matched sources named in g2d.h; the field
 * offsets below are those readings, not a second, independent guess.
 * Each resource is a 16-byte common file header (magic, BOM, version,
 * file size, header size, block count) followed by size-chained blocks
 * whose magic is the on-disk reversal of the block's name.
 *
 * Every read goes through bw_rd16/bw_rd32 (portability rule 4): the bytes
 * are little-endian whatever the host is, and nothing is cast to a wider
 * pointer type.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

/* ------------------------------------------------------------------ */
/* Common header                                                       */
/* ------------------------------------------------------------------ */

/*
 * Locate block `want` of a Nitro resource.  Returns 0 and fills in the
 * block body's offset and size (the body excludes the 8-byte magic+size),
 * or -1.  Blocks are walked by their own declared size, so an optional
 * trailing block (SOPC, LBAL, TXEU) neither has to be known nor gets in
 * the way.
 */
static int find_block(const u8 *data, u32 size, u32 magic, u32 want,
                      u32 *body_ofs, u32 *body_size)
{
    u32 pos;
    u32 file_magic;

    if (size < 16) {
        return -1;
    }
    file_magic = bw_rd32(data);
    if (file_magic != magic) {
        return -1;
    }
    pos = bw_rd16(data + 12);      /* headerSize */
    while (pos + 8 <= size) {
        u32 bmagic = bw_rd32(data + pos);
        u32 bsize = bw_rd32(data + pos + 4);

        if (bsize < 8 || bsize > size - pos) {
            return -1;
        }
        if (bmagic == want) {
            *body_ofs = pos + 8;
            *body_size = bsize - 8;
            return 0;
        }
        pos += bsize;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* NCLR (RLCN) -- palette                                              */
/* ------------------------------------------------------------------ */

int g2d_palette_append_nclr(G2dPalette *pal, const u8 *data, u32 size)
{
    u32 body, body_size;
    u32 data_size, color_ofs;
    u32 n_colors;
    u32 i;
    G2dColor *grown;

    if (find_block(data, size, FOURCC('R', 'L', 'C', 'N'),
                   FOURCC('T', 'T', 'L', 'P'), &body, &body_size) != 0) {
        return -1;
    }
    if (body_size < 16) {
        return -1;
    }
    data_size = bw_rd32(data + body + 8);
    color_ofs = bw_rd32(data + body + 12);
    if (color_ofs > body_size || data_size > body_size - color_ofs) {
        return -1;
    }
    n_colors = data_size / 2;

    grown = (G2dColor *)realloc(pal->colors,
                                (pal->count + n_colors) * sizeof(G2dColor));
    if (grown == NULL && (pal->count + n_colors) != 0) {
        return -1;
    }
    pal->colors = grown;
    for (i = 0; i < n_colors; i++) {
        u16 c = bw_rd16(data + body + color_ofs + i * 2);
        G2dColor *dst = &pal->colors[pal->count + i];

        /* BGR555 to 8 bits per channel, the same integer expansion the
         * asset tools use: v * 255 / 31, truncating. */
        dst->r = (u8)(((u32)(c & 0x1F) * 255u) / 31u);
        dst->g = (u8)(((u32)((c >> 5) & 0x1F) * 255u) / 31u);
        dst->b = (u8)(((u32)((c >> 10) & 0x1F) * 255u) / 31u);
    }
    pal->count += n_colors;
    return 0;
}

void g2d_palette_free(G2dPalette *pal)
{
    free(pal->colors);
    pal->colors = NULL;
    pal->count = 0;
}

/* ------------------------------------------------------------------ */
/* NCGR (RGCN) -- character data                                       */
/* ------------------------------------------------------------------ */

/*
 * The RAHC body is 24 bytes then the pixel array:
 *   0x00 u16 tile_num_y, 0x02 u16 tile_num_x, 0x04 u32 pixel_fmt,
 *   0x08 u32 mapping_type, 0x0C u32 char_format, 0x10 u32 data_size,
 *   0x14 u32 data_ofs.
 *
 * char_format is load bearing: 0 is an 8x8-tiled sheet (blocks laid end
 * to end), 1 is a plain linear bitmap tile_num_x*8 pixels wide.  The rig
 * archive's main sheet declares 1, and reading it as tiled turns every
 * body part into 8-pixel-tall streaks.
 *
 * 0xFFFF in either dimension is the documented "unspecified, derive from
 * data" sentinel.  No real width has been recovered for that case, so the
 * sheet is kept with no pixel buffer at all (allocating from the literal
 * 0xFFFF x 0xFFFF would exhaust memory); tile addressing for such a sheet
 * goes through the OAM's own width instead (see compose.c).
 */
int g2d_ncgr_parse(G2dNcgr *out, const u8 *data, u32 size)
{
    u32 body, body_size;
    u32 data_size, data_ofs;
    u32 t;

    memset(out, 0, sizeof(*out));
    if (find_block(data, size, FOURCC('R', 'G', 'C', 'N'),
                   FOURCC('R', 'A', 'H', 'C'), &body, &body_size) != 0) {
        return -1;
    }
    if (body_size < 24) {
        return -1;
    }
    out->tile_num_y = bw_rd16(data + body + 0);
    out->tile_num_x = bw_rd16(data + body + 2);
    out->pixel_fmt = bw_rd32(data + body + 4);
    out->mapping_type = bw_rd32(data + body + 8);
    out->char_format = bw_rd32(data + body + 12);
    data_size = bw_rd32(data + body + 16);
    data_ofs = bw_rd32(data + body + 20);
    if (data_ofs > body_size) {
        return -1;
    }
    if (data_size > body_size - data_ofs) {
        data_size = body_size - data_ofs;
    }
    out->tile_data = data + body + data_ofs;
    out->tile_data_size = data_size;
    out->bytes_per_tile = (out->pixel_fmt == G2D_NCGR_FMT_8BPP) ? 64u : 32u;

    out->sentinel_dims = (out->tile_num_x == G2D_NCGR_DIM_SENTINEL
                          || out->tile_num_y == G2D_NCGR_DIM_SENTINEL);
    if (out->sentinel_dims) {
        return 0;
    }
    out->width = (u32)out->tile_num_x * 8u;
    out->height = (u32)out->tile_num_y * 8u;
    if (out->width == 0 || out->height == 0) {
        return 0;
    }
    out->pixels = (u8 *)calloc((size_t)out->width * out->height, 1);
    if (out->pixels == NULL) {
        return -1;
    }

    if (out->pixel_fmt == G2D_NCGR_FMT_8BPP) {
        if (out->char_format == 1) {
            u32 n = data_size;
            u32 total = out->width * out->height;

            if (n > total) {
                n = total;
            }
            memcpy(out->pixels, out->tile_data, n);
        } else {
            for (t = 0; t < data_size / 64u; t++) {
                u32 bx = (t % out->tile_num_x) * 8u;
                u32 by = (t / out->tile_num_x) * 8u;
                u32 i;

                if (by + 8u > out->height || bx + 8u > out->width) {
                    break;
                }
                for (i = 0; i < 64u; i++) {
                    out->pixels[(by + i / 8u) * out->width + bx + (i % 8u)] =
                        out->tile_data[t * 64u + i];
                }
            }
        }
        return 0;
    }

    /* 4bpp, low nibble first (the left pixel of the pair). */
    if (out->char_format == 1) {
        u32 total = out->width * out->height;
        u32 i;

        for (i = 0; i < data_size; i++) {
            u32 p = i * 2u;

            if (p + 1u >= total) {
                break;
            }
            out->pixels[p] = (u8)(out->tile_data[i] & 0x0F);
            out->pixels[p + 1u] = (u8)((out->tile_data[i] >> 4) & 0x0F);
        }
        return 0;
    }
    for (t = 0; t < data_size / 32u; t++) {
        u32 bx = (t % out->tile_num_x) * 8u;
        u32 by = (t / out->tile_num_x) * 8u;
        u32 i;

        if (by + 8u > out->height || bx + 8u > out->width) {
            break;
        }
        for (i = 0; i < 32u; i++) {
            u8 b = out->tile_data[t * 32u + i];
            u32 p = (by + i / 4u) * out->width + bx + (i % 4u) * 2u;

            out->pixels[p] = (u8)(b & 0x0F);
            out->pixels[p + 1u] = (u8)((b >> 4) & 0x0F);
        }
    }
    return 0;
}

void g2d_ncgr_free(G2dNcgr *ncgr)
{
    free(ncgr->pixels);
    ncgr->pixels = NULL;
}

void g2d_ncgr_block(const G2dNcgr *ncgr, u32 block_index, u8 out[64])
{
    u32 bx, by, ly;

    memset(out, 0, 64);
    if (ncgr->pixels == NULL || ncgr->tile_num_x == 0) {
        return;
    }
    bx = (block_index % ncgr->tile_num_x) * 8u;
    by = (block_index / ncgr->tile_num_x) * 8u;
    if (by + 8u > ncgr->height || bx + 8u > ncgr->width) {
        return;
    }
    for (ly = 0; ly < 8u; ly++) {
        memcpy(out + ly * 8u, ncgr->pixels + (by + ly) * ncgr->width + bx, 8);
    }
}

/* ------------------------------------------------------------------ */
/* NCER (RECN) -- cell bank                                            */
/* ------------------------------------------------------------------ */

/*
 * The two standard OBJ dimension tables, the host-side copies of the ARM9
 * tables at 0x020A17F0 (width) and 0x020A17D8 (height) that
 * src/matched/unit_02067B40.c's flip arithmetic indexes: three rows of
 * four, row = shape (attr0 bits 14-15), column = size (attr1 bits 14-15).
 */
static const u16 k_obj_width[3][4] = {
    {  8, 16, 32, 64 },   /* square */
    { 16, 32, 32, 64 },   /* wide   */
    {  8,  8, 16, 32 },   /* tall   */
};

static const u16 k_obj_height[3][4] = {
    {  8, 16, 32, 64 },
    {  8,  8, 16, 32 },
    { 16, 32, 32, 64 },
};

u32 g2d_obj_width(u32 shape, u32 size)
{
    if (shape > 2 || size > 3) {
        return 8;
    }
    return k_obj_width[shape][size];
}

u32 g2d_obj_height(u32 shape, u32 size)
{
    if (shape > 2 || size > 3) {
        return 8;
    }
    return k_obj_height[shape][size];
}

/*
 * Decode one attr0/attr1/attr2 triple, applying the two per-OAM-entry
 * placement corrections the draw path performs and a naive composer
 * misses (see g2d.h's provenance note):
 *
 *   flip           x = -(x + w), y = -(y + h)   per set flip bit
 *   affine double  x = x + w/2,  y = y + h/2    when attr0 bits 8-9 == 3
 *
 * The two are mutually exclusive on hardware, because rot/scale mode
 * repurposes attr1's flip bits as a parameter index, so the flip bits are
 * forced off whenever rot/scale is on and the order of the two never
 * matters.
 */
static void oam_decode(G2dOam *o, u16 attr0, u16 attr1, u16 attr2)
{
    u32 y = attr0 & 0xFFu;
    u32 x = attr1 & 0x1FFu;
    int rot_scale = (attr0 >> 8) & 0x1;

    o->y = (s32)(y >= 128u ? (s32)y - 256 : (s32)y);
    o->x = (s32)(x >= 256u ? (s32)x - 512 : (s32)x);
    o->shape = (u16)((attr0 >> 14) & 0x3);
    o->size = (u16)((attr1 >> 14) & 0x3);
    o->hflip = rot_scale ? 0 : (int)((attr1 >> 12) & 0x1);
    o->vflip = rot_scale ? 0 : (int)((attr1 >> 13) & 0x1);
    o->tile_idx = (u16)(attr2 & 0x3FF);
    o->priority = (u16)((attr2 >> 10) & 0x3);
    o->pal = (u16)((attr2 >> 12) & 0xF);
    o->w = g2d_obj_width(o->shape, o->size);
    o->h = g2d_obj_height(o->shape, o->size);
    o->affine_double = (((attr0 >> 8) & 0x3) == 0x3);

    if (o->affine_double) {
        o->x += (s32)(o->w / 2u);
        o->y += (s32)(o->h / 2u);
    }
    if (o->hflip) {
        o->x = -(o->x + (s32)o->w);
    }
    if (o->vflip) {
        o->y = -(o->y + (s32)o->h);
    }
}

int g2d_ncer_parse(G2dNcer *out, const u8 *data, u32 size)
{
    u32 body, body_size;
    u32 cell_data_ofs;
    u32 stride;
    u32 oam_arr_start;
    u32 i;

    memset(out, 0, sizeof(*out));
    if (find_block(data, size, FOURCC('R', 'E', 'C', 'N'),
                   FOURCC('K', 'B', 'E', 'C'), &body, &body_size) != 0) {
        return -1;
    }
    if (body_size < 16) {
        return -1;
    }
    out->num_cells = bw_rd16(data + body + 0);
    out->cell_info_type = bw_rd16(data + body + 2);
    cell_data_ofs = bw_rd32(data + body + 4);
    out->mapping_type = bw_rd32(data + body + 8);

    /* cell_info_type 1 is the extended 16-byte record (the same three
     * fields plus an s16 bounding box); 0 is the plain 8-byte one. */
    stride = (out->cell_info_type == 1) ? 16u : 8u;
    oam_arr_start = cell_data_ofs + (u32)out->num_cells * stride;
    if (cell_data_ofs > body_size || oam_arr_start > body_size) {
        return -1;
    }

    out->cells = (G2dCell *)calloc(out->num_cells ? out->num_cells : 1,
                                   sizeof(G2dCell));
    if (out->cells == NULL) {
        return -1;
    }
    for (i = 0; i < out->num_cells; i++) {
        u32 off = cell_data_ofs + i * stride;
        u32 oam_ofs;
        u32 j;
        G2dCell *cell = &out->cells[i];

        cell->num_oam = bw_rd16(data + body + off);
        cell->cell_attr = bw_rd16(data + body + off + 2);
        oam_ofs = bw_rd32(data + body + off + 4);
        if (cell->num_oam == 0) {
            continue;
        }
        if (oam_arr_start + oam_ofs + (u32)cell->num_oam * 6u > body_size) {
            g2d_ncer_free(out);
            return -1;
        }
        cell->oams = (G2dOam *)calloc(cell->num_oam, sizeof(G2dOam));
        if (cell->oams == NULL) {
            g2d_ncer_free(out);
            return -1;
        }
        for (j = 0; j < cell->num_oam; j++) {
            const u8 *p = data + body + oam_arr_start + oam_ofs + j * 6u;

            oam_decode(&cell->oams[j], bw_rd16(p), bw_rd16(p + 2), bw_rd16(p + 4));
        }
    }
    return 0;
}

void g2d_ncer_free(G2dNcer *ncer)
{
    u32 i;

    if (ncer->cells != NULL) {
        for (i = 0; i < ncer->num_cells; i++) {
            free(ncer->cells[i].oams);
        }
        free(ncer->cells);
        ncer->cells = NULL;
    }
    ncer->num_cells = 0;
}

/* ------------------------------------------------------------------ */
/* NANR (RNAN) -- cell animation                                       */
/* ------------------------------------------------------------------ */

/*
 * ABNK body: numSequences, numTotalFrames, then three body-relative
 * offsets (sequence array, frame-entry array, frame-data pool).  A
 * sequence record is 16 bytes and its +0x0C offset is relative to the
 * frame-entry array, not to the body; a frame entry is 8 bytes (offset
 * into the frame-data pool, duration, 0xBEEF sentinel).  That is exactly
 * what the bank's own fixup routine at 0x02062688 adds base addresses
 * into, which is how the layout is pinned.
 */
static int abnk_parse(G2dNanr *out, const u8 *data, u32 size, u32 magic)
{
    u32 body, body_size;
    u32 i;

    memset(out, 0, sizeof(*out));
    if (find_block(data, size, magic, FOURCC('K', 'N', 'B', 'A'), &body, &body_size) != 0) {
        return -1;
    }
    if (body_size < 16) {
        return -1;
    }
    out->body = data + body;
    out->body_size = body_size;
    out->num_sequences = bw_rd16(out->body + 0);
    out->num_elements = bw_rd16(out->body + 2);
    out->seq_ofs = bw_rd32(out->body + 4);
    out->frame_ofs = bw_rd32(out->body + 8);
    out->frame_data_ofs = bw_rd32(out->body + 12);

    if (out->num_sequences != 0
        && (out->seq_ofs > body_size
            || (u32)out->num_sequences * 16u > body_size - out->seq_ofs)) {
        return -1;
    }
    out->sequences = (G2dNanrSequence *)calloc(
        out->num_sequences ? out->num_sequences : 1, sizeof(G2dNanrSequence));
    if (out->sequences == NULL) {
        return -1;
    }
    for (i = 0; i < out->num_sequences; i++) {
        const u8 *p = out->body + out->seq_ofs + i * 16u;

        out->sequences[i].num_frames = bw_rd16(p);
        out->sequences[i].loop_start = bw_rd16(p + 2);
        out->sequences[i].anim_type = bw_rd32(p + 4);
        out->sequences[i].play_mode = bw_rd32(p + 8);
        out->sequences[i].frames_ofs = bw_rd32(p + 12);
    }
    return 0;
}

int g2d_nanr_parse(G2dNanr *out, const u8 *data, u32 size)
{
    return abnk_parse(out, data, size, FOURCC('R', 'N', 'A', 'N'));
}

/* NMAR (RAMN): the same animation bank, its frames naming multi-cells. */
int g2d_nmar_parse(G2dNanr *out, const u8 *data, u32 size)
{
    return abnk_parse(out, data, size, FOURCC('R', 'A', 'M', 'N'));
}

void g2d_nanr_free(G2dNanr *nanr)
{
    free(nanr->sequences);
    nanr->sequences = NULL;
    nanr->num_sequences = 0;
}

/* (offset into the frame-data pool, duration) for one frame entry. */
static int nanr_frame_entry(const G2dNanr *nanr, u32 seq, u32 frame,
                            u32 *fofs, u32 *duration)
{
    const G2dNanrSequence *s;
    u32 off;

    if (seq >= nanr->num_sequences) {
        return -1;
    }
    s = &nanr->sequences[seq];
    if (frame >= s->num_frames) {
        return -1;
    }
    off = nanr->frame_ofs + s->frames_ofs + frame * 8u;
    if (off + 8u > nanr->body_size) {
        return -1;
    }
    *fofs = bw_rd32(nanr->body + off);
    *duration = bw_rd16(nanr->body + off + 4);
    return 0;
}

int g2d_nanr_frame_data(const G2dNanr *nanr, u32 seq, u32 frame, G2dFrame *out)
{
    u32 fofs, duration, base, etype, need;

    if (nanr_frame_entry(nanr, seq, frame, &fofs, &duration) != 0) {
        return -1;
    }
    base = nanr->frame_data_ofs + fofs;
    etype = nanr->sequences[seq].anim_type & 0xFFu;
    switch (etype) {
    case G2D_ANIM_ELEMENT_INDEX: need = 2; break;
    case G2D_ANIM_ELEMENT_SRT:   need = 16; break;
    case G2D_ANIM_ELEMENT_T:     need = 8; break;
    default: return -1;
    }
    if (base > nanr->body_size || need > nanr->body_size - base) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->cell_idx = bw_rd16(nanr->body + base);
    out->sx = 4096;
    out->sy = 4096;
    if (etype == G2D_ANIM_ELEMENT_T) {
        out->px = (s16)bw_rd16(nanr->body + base + 4);
        out->py = (s16)bw_rd16(nanr->body + base + 6);
    } else if (etype == G2D_ANIM_ELEMENT_SRT) {
        out->rot_z = bw_rd16(nanr->body + base + 2);
        out->sx = (s32)bw_rd32(nanr->body + base + 4);
        out->sy = (s32)bw_rd32(nanr->body + base + 8);
        out->px = (s16)bw_rd16(nanr->body + base + 12);
        out->py = (s16)bw_rd16(nanr->body + base + 14);
    }
    return 0;
}

u32 g2d_nanr_frame_at_time(const G2dNanr *nanr, u32 seq, u32 t)
{
    const G2dNanrSequence *s;
    u32 i;
    u32 total = 0;
    u32 loop_start;
    u32 loop_time = 0;
    u32 et;
    u32 acc;

    if (seq >= nanr->num_sequences) {
        return 0;
    }
    s = &nanr->sequences[seq];
    if (s->num_frames <= 1) {
        return 0;
    }
    loop_start = s->loop_start;
    if (loop_start > (u32)(s->num_frames - 1)) {
        loop_start = (u32)(s->num_frames - 1);
    }
    for (i = 0; i < s->num_frames; i++) {
        u32 fofs, duration;

        if (i == loop_start) {
            loop_time = total;
        }
        if (nanr_frame_entry(nanr, seq, i, &fofs, &duration) != 0) {
            duration = 1;
        }
        /* A zero-duration frame must not stall the timeline. */
        total += duration ? duration : 1u;
    }
    if (total == 0) {
        return 0;
    }
    et = t;
    if (et >= total) {
        u32 loop_len = total - loop_time;

        et = (loop_len == 0) ? loop_time : loop_time + (et - total) % loop_len;
    }
    acc = 0;
    for (i = 0; i < s->num_frames; i++) {
        u32 fofs, duration;

        if (nanr_frame_entry(nanr, seq, i, &fofs, &duration) != 0) {
            duration = 1;
        }
        acc += duration ? duration : 1u;
        if (et < acc) {
            return i;
        }
    }
    return (u32)(s->num_frames - 1);
}

/* ------------------------------------------------------------------ */
/* NMCR (RCMN) -- multi-cell node placement                            */
/* ------------------------------------------------------------------ */

/*
 * MCBK body: numMultiCell, a 0xBEEF signature, then two plain
 * body-relative offsets (the 8-byte descriptor array, and the base the
 * descriptors' own node offsets are relative to).  A node record is 8
 * bytes: animSeqIdx, posX, posY, nodeAttr, cellAnimSlot.  The node names
 * a NANR *sequence*, never a cell: the sequence's current frame names the
 * cell.  The x and y are applied verbatim (unit_020640B0.c).
 */
int g2d_nmcr_parse(G2dNmcr *out, const u8 *data, u32 size)
{
    u32 body, body_size;
    u32 mc_abs, node_abs;
    u32 i;

    memset(out, 0, sizeof(*out));
    if (find_block(data, size, FOURCC('R', 'C', 'M', 'N'),
                   FOURCC('K', 'B', 'C', 'M'), &body, &body_size) != 0) {
        return -1;
    }
    if (body_size < 16) {
        return -1;
    }
    out->num_multi_cell = bw_rd16(data + body + 0);
    out->signature = bw_rd16(data + body + 2);
    mc_abs = bw_rd32(data + body + 4);
    node_abs = bw_rd32(data + body + 8);
    if (out->num_multi_cell != 0
        && (mc_abs > body_size
            || (u32)out->num_multi_cell * 8u > body_size - mc_abs)) {
        return -1;
    }

    out->groups = (G2dMcGroup *)calloc(
        out->num_multi_cell ? out->num_multi_cell : 1, sizeof(G2dMcGroup));
    if (out->groups == NULL) {
        return -1;
    }
    for (i = 0; i < out->num_multi_cell; i++) {
        const u8 *d = data + body + mc_abs + i * 8u;
        u32 node_ofs = bw_rd32(d + 4);
        u32 base = node_abs + node_ofs;
        G2dMcGroup *g = &out->groups[i];
        u32 j;
        u32 kept = 0;

        g->num_nodes = bw_rd16(d);
        g->num_cell_anims = bw_rd16(d + 2);
        if (g->num_nodes == 0) {
            continue;
        }
        g->nodes = (G2dMcNode *)calloc(g->num_nodes, sizeof(G2dMcNode));
        if (g->nodes == NULL) {
            g2d_nmcr_free(out);
            return -1;
        }
        for (j = 0; j < g->num_nodes; j++) {
            u32 noff = base + j * 8u;
            const u8 *p;

            /* A descriptor whose node run overruns the body is truncated
             * rather than rejected: the same tolerance the asset tool
             * applies, so both composers see the same node list. */
            if (noff + 8u > body_size) {
                break;
            }
            p = data + body + noff;
            g->nodes[j].anim_seq_idx = bw_rd16(p);
            g->nodes[j].x = (s16)bw_rd16(p + 2);
            g->nodes[j].y = (s16)bw_rd16(p + 4);
            g->nodes[j].node_attr = p[6];
            g->nodes[j].cell_anim_slot = p[7];
            kept++;
        }
        g->num_nodes = (u16)kept;
    }
    return 0;
}

void g2d_nmcr_free(G2dNmcr *nmcr)
{
    u32 i;

    if (nmcr->groups != NULL) {
        for (i = 0; i < nmcr->num_multi_cell; i++) {
            free(nmcr->groups[i].nodes);
        }
        free(nmcr->groups);
        nmcr->groups = NULL;
    }
    nmcr->num_multi_cell = 0;
}
