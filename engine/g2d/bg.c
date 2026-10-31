/*
 * engine/g2d/bg.c -- text-mode BG layers and the frame compositor.
 *
 * The NSCR (screen / tile map) parser, the per-layer rasteriser with
 * scroll and screen-size handling, and the compositor that stacks up to
 * four BG layers and an OBJ list by priority into one RGBA frame.
 *
 * Provenance is stated in g2d.h's NSCR section.  The two rules worth
 * repeating at the point where they are implemented:
 *
 *   - The screen-entry index for a given (tile x, tile y) is NOT row
 *     major for a 512-pixel-wide or 512-pixel-tall hardware map.
 *     src/matched/func_02040EE0.c is the game's own helper and lays out
 *     every size: 512x256 puts the second 32x32 map after the first, and
 *     512x512 lays four out in a 2x2 grid, 0x400 entries apart.  That is
 *     g2d_bg_screen_index below, transcribed from it.
 *
 *   - An NSCR resource on disk is NOT in that order.  Its entry array is
 *     row major at its own declared width, which is the only reading
 *     available for the many screens whose declared size is not a
 *     hardware size at all (a/1/4/3#22 is 512x192 and a/0/3/3#29 is
 *     448x192).  The two readings coincide for every size except
 *     512x256 and 512x512; G2D_BG_SIZE_FLAT selects the row-major one
 *     and is what the oracle comparison uses.  See docs/engine-2d.md.
 *
 * Every read goes through bw_rd16/bw_rd32 (portability rule 4).
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

/* ------------------------------------------------------------------ */
/* NSCR (RCSN) -- screen                                               */
/* ------------------------------------------------------------------ */

/*
 * The SCRN (on-disk "NRCS") body is 12 bytes then the entry array:
 *   0x00 u16 width_px, 0x02 u16 height_px, 0x04 u32 unknown,
 *   0x08 u32 data_size, 0x0C .. data_size bytes of u16 entries.
 * data_size is exactly width/8 * height/8 * 2 for every NSCR sub-file
 * aletheia's asset lane scanned, and no NSCR sub-file in any covered
 * archive is compressed.
 */
int g2d_nscr_parse(G2dNscr *out, const u8 *data, u32 size)
{
    u32 pos, hsize, bmagic, bsize, body, body_size;
    u32 data_size;
    u32 i;

    memset(out, 0, sizeof(*out));
    if (size < 16 || bw_rd32(data) != FOURCC('R', 'C', 'S', 'N')) {
        return -1;
    }
    hsize = bw_rd16(data + 12);
    pos = hsize;
    if (pos + 8 > size) {
        return -1;
    }
    bmagic = bw_rd32(data + pos);
    bsize = bw_rd32(data + pos + 4);
    if (bmagic != FOURCC('N', 'R', 'C', 'S') || bsize < 8 || bsize > size - pos) {
        return -1;
    }
    body = pos + 8;
    body_size = bsize - 8;
    if (body_size < 12) {
        return -1;
    }
    out->width = bw_rd16(data + body + 0);
    out->height = bw_rd16(data + body + 2);
    out->unk = bw_rd32(data + body + 4);
    data_size = bw_rd32(data + body + 8);
    if (data_size > body_size - 12) {
        data_size = body_size - 12;
    }
    out->tiles_wide = (u16)(out->width / 8u);
    out->tiles_high = (u16)(out->height / 8u);
    out->count = data_size / 2u;
    out->entries = (u16 *)calloc(out->count ? out->count : 1, sizeof(u16));
    if (out->entries == NULL) {
        return -1;
    }
    for (i = 0; i < out->count; i++) {
        out->entries[i] = bw_rd16(data + body + 12 + i * 2u);
    }
    return 0;
}

void g2d_nscr_free(G2dNscr *nscr)
{
    free(nscr->entries);
    nscr->entries = NULL;
    nscr->count = 0;
}

/* ------------------------------------------------------------------ */
/* Archive members                                                     */
/* ------------------------------------------------------------------ */

u8 *g2d_arc_member(BwArcFile *arc, u32 index, u32 *out_size)
{
    long raw_size = bw_arc_member_size(arc, index);
    u8 *raw;
    u8 *dec;
    u32 dec_size = 0;

    *out_size = 0;
    if (raw_size <= 0) {
        return NULL;
    }
    raw = (u8 *)malloc((size_t)raw_size);
    if (raw == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(arc, index, raw, (u32)raw_size) != raw_size) {
        free(raw);
        return NULL;
    }
    dec = g2d_decompress(raw, (u32)raw_size, &dec_size);
    if (dec != NULL) {
        free(raw);
        *out_size = dec_size;
        return dec;
    }
    *out_size = (u32)raw_size;
    return raw;
}

/* ------------------------------------------------------------------ */
/* Character data, addressed the way a screen entry addresses it       */
/* ------------------------------------------------------------------ */

void g2d_bg_tile(const G2dNcgr *ncgr, u32 tile_index, int hflip, int vflip,
                 u8 out[64])
{
    u8 tmp[64];
    u32 off, i;

    memset(out, 0, 64);
    if (ncgr == NULL || ncgr->tile_data == NULL || ncgr->bytes_per_tile == 0) {
        return;
    }
    off = tile_index * ncgr->bytes_per_tile;
    if (off > ncgr->tile_data_size
        || ncgr->bytes_per_tile > ncgr->tile_data_size - off) {
        return;   /* past the end of this NCGR's data: blank */
    }
    if (ncgr->bytes_per_tile == 32u) {
        /* 4bpp, low nibble first (the left pixel of the pair). */
        for (i = 0; i < 32u; i++) {
            u8 b = ncgr->tile_data[off + i];

            tmp[i * 2u] = (u8)(b & 0x0F);
            tmp[i * 2u + 1u] = (u8)((b >> 4) & 0x0F);
        }
    } else {
        memcpy(tmp, ncgr->tile_data + off, 64);
    }

    for (i = 0; i < 64u; i++) {
        u32 lx = i % 8u;
        u32 ly = i / 8u;

        if (hflip) {
            lx = 7u - lx;
        }
        if (vflip) {
            ly = 7u - ly;
        }
        out[i] = tmp[ly * 8u + lx];
    }
}

/* ------------------------------------------------------------------ */
/* Screen layout                                                       */
/* ------------------------------------------------------------------ */

/*
 * src/matched/func_02040EE0.c, transcribed.  The arithmetic there is u16
 * throughout and case 4 truncates the scaled map index to sixteen bits
 * before adding the in-map offset; that truncation is kept, because a map
 * index large enough to wrap is exactly what an out-of-range (x, y) would
 * produce and the game's own helper would wrap it.
 */
u32 g2d_bg_screen_index(int x, int y, u32 size_code)
{
    u16 map, off;

    switch (size_code) {
    case G2D_BG_SIZE_128x128:
        return (u16)(x + y * 16);
    case G2D_BG_SIZE_256x256:
    case G2D_BG_SIZE_256x512:
        return (u16)(x + y * 32);
    case G2D_BG_SIZE_512x256:
        return (u16)((x & 0x1f) + (y + (x & ~0x1f)) * 32);
    case G2D_BG_SIZE_512x512:
        map = (u16)((y >> 5) * 2 + (x >> 5));
        off = (u16)((x & 0x1f) + (y & 0x1f) * 32);
        map = (u16)(map * 0x400);
        return (u16)(off + map);
    default:
        return 0;
    }
}

void g2d_bg_map_tiles(u32 size_code, u32 *tiles_wide, u32 *tiles_high)
{
    u32 w = 0, h = 0;

    switch (size_code) {
    case G2D_BG_SIZE_128x128: w = 16; h = 16; break;
    case G2D_BG_SIZE_256x256: w = 32; h = 32; break;
    case G2D_BG_SIZE_256x512: w = 32; h = 64; break;
    case G2D_BG_SIZE_512x256: w = 64; h = 32; break;
    case G2D_BG_SIZE_512x512: w = 64; h = 64; break;
    default: break;
    }
    *tiles_wide = w;
    *tiles_high = h;
}

/* ------------------------------------------------------------------ */
/* Layer sampling                                                      */
/* ------------------------------------------------------------------ */

static void layer_map_tiles(const G2dBgLayer *layer, u32 *tw, u32 *th)
{
    if (layer->screen_size == G2D_BG_SIZE_FLAT) {
        *tw = layer->nscr->tiles_wide;
        *th = layer->nscr->tiles_high;
        return;
    }
    g2d_bg_map_tiles(layer->screen_size, tw, th);
}

int g2d_bg_sample(const G2dBgLayer *layer, s32 sx, s32 sy,
                  u8 *out_index, u32 *out_color_index)
{
    u32 tw = 0, th = 0;
    s32 px, py;
    u32 tx, ty, entry_idx;
    u16 entry;
    u8 index;
    u32 base;

    if (layer == NULL || layer->nscr == NULL || layer->ncgr == NULL) {
        return 0;
    }
    layer_map_tiles(layer, &tw, &th);
    if (tw == 0 || th == 0) {
        return 0;
    }

    px = sx + layer->hofs;
    py = sy + layer->vofs;
    if (layer->wrap) {
        s32 mw = (s32)(tw * 8u);
        s32 mh = (s32)(th * 8u);

        /* A power-of-two map (every hardware size) wraps with a mask: the
         * same non-negative remainder, without the division. */
        if ((mw & (mw - 1)) == 0) {
            px &= mw - 1;
        } else {
            px %= mw;
            if (px < 0) {
                px += mw;
            }
        }
        if ((mh & (mh - 1)) == 0) {
            py &= mh - 1;
        } else {
            py %= mh;
            if (py < 0) {
                py += mh;
            }
        }
    } else if (px < 0 || py < 0
               || px >= (s32)(tw * 8u) || py >= (s32)(th * 8u)) {
        return 0;
    }

    tx = (u32)px / 8u;
    ty = (u32)py / 8u;
    /*
     * The two readings of a 512-pixel-wide or 512-pixel-tall map, behind
     * the layer's own flag.  G2D_BG_SIZE_FLAT is row-major by
     * construction and ignores it; for a hardware size code the default
     * (zero, G2D_BG_ORDER_VRAM) is the order func_02040EE0 attests, and
     * G2D_BG_ORDER_ROW_MAJOR is background.py's reading of the same entry
     * array.  The two coincide for every size but 512x256 and 512x512.
     */
    if (layer->screen_size == G2D_BG_SIZE_FLAT
        || layer->screen_order == G2D_BG_ORDER_ROW_MAJOR) {
        entry_idx = ty * tw + tx;
    } else {
        entry_idx = g2d_bg_screen_index((int)tx, (int)ty, layer->screen_size);
    }
    if (entry_idx >= layer->nscr->count) {
        return 0;
    }
    entry = layer->nscr->entries[entry_idx];

    /* One pixel of the tile, flips applied: the same value
     * g2d_bg_tile would put at that spot, without decoding the other 63. */
    {
        const G2dNcgr *nc = layer->ncgr;
        u32 bpt = nc->bytes_per_tile;
        u32 lx = (u32)px % 8u, ly = (u32)py % 8u;
        u32 off = G2D_SCR_TILE(entry) * bpt;

        index = 0;
        if (nc->tile_data != NULL && bpt != 0 && off <= nc->tile_data_size
            && bpt <= nc->tile_data_size - off) {
            if (G2D_SCR_HFLIP(entry)) {
                lx = 7u - lx;
            }
            if (G2D_SCR_VFLIP(entry)) {
                ly = 7u - ly;
            }
            if (bpt == 32u) {
                u8 b = nc->tile_data[off + ly * 4u + (lx >> 1)];

                index = (lx & 1u) ? (u8)(b >> 4) : (u8)(b & 0x0F);
            } else {
                index = nc->tile_data[off + ly * 8u + lx];
            }
        }
    }

    /*
     * 4bpp selects one 16-colour bank with the entry's palette row; 8bpp
     * ignores that field and indexes the whole 256-colour palette.  (An
     * extended-palette 8bpp layer, BGxCNT bit 13, would select a 256-entry
     * slot with the same field; no BG resource in the archives the asset
     * lane covers uses one, and this engine does not implement it.)
     */
    base = (layer->ncgr->bytes_per_tile == 32u)
         ? (u32)G2D_SCR_PAL(entry) * 16u : 0u;

    if (out_index != NULL) {
        *out_index = index;
    }
    if (out_color_index != NULL) {
        *out_color_index = base + index;
    }
    return 1;
}

void g2d_bg_render(const G2dBgLayer *layer, u32 width, u32 height, u8 *rgba)
{
    u32 x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            u8 *out = rgba + ((size_t)y * width + x) * 4u;
            u8 index;
            u32 ci;

            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
            out[3] = 255;
            if (!g2d_bg_sample(layer, (s32)x, (s32)y, &index, &ci)) {
                continue;
            }
            if (layer->pal != NULL && ci < layer->pal->count) {
                out[0] = layer->pal->colors[ci].r;
                out[1] = layer->pal->colors[ci].g;
                out[2] = layer->pal->colors[ci].b;
            } else {
                out[0] = 255;
                out[1] = 0;
                out[2] = 255;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The frame compositor                                                */
/* ------------------------------------------------------------------ */

static void put_color(u8 *px, const G2dPalette *pal, u32 ci)
{
    if (pal != NULL && ci < pal->count) {
        px[0] = pal->colors[ci].r;
        px[1] = pal->colors[ci].g;
        px[2] = pal->colors[ci].b;
    } else {
        px[0] = 255;
        px[1] = 0;
        px[2] = 255;
    }
    px[3] = 255;
}

/*
 * The compositor resolves one pixel at a time rather than painting layer
 * by layer.  Painting was enough while the answer was only "which colour
 * ends on top"; the DS colour special effects need to know which layer
 * that topmost pixel belongs to and which layer is directly beneath it,
 * and that is a per-pixel question.  The order it walks is the painting
 * order read front to back, so the picture is unchanged.
 *
 * Front-to-back within one priority: the OBJ entries in forward list
 * order, then the BG layers by ascending layer number (a text layer's
 * number is its index in `bgs`, an affine layer carries its own).
 */

#define G2D_MAX_BG_INDEX 8u

/* g2d_window_mask with its "no window enabled" answer taken inline: the
 * per-pixel callers mostly have none, and a call per pixel adds up. */
static u32 win_mask(const G2dWindows *w, s32 x, s32 y)
{
    if (w == NULL
        || (!w->win0.enabled && !w->win1.enabled && !w->obj_enabled)) {
        return G2D_LAYER_ALL;
    }
    return g2d_window_mask(w, x, y);
}


static int obj_sample(const G2dObjEntry *obj, s32 x, s32 y, u8 *out_index)
{
    s32 px, py;
    u8 index;

    if (obj->pose == NULL || obj->pose->indices == NULL) {
        return 0;
    }
    px = x - obj->x;
    py = y - obj->y;
    if (px < 0 || py < 0
        || (u32)px >= obj->pose->width || (u32)py >= obj->pose->height) {
        return 0;
    }
    index = obj->pose->indices[(size_t)py * obj->pose->width + px];
    if (index == 0) {
        return 0;   /* palette index 0 is transparent for an OBJ too */
    }
    *out_index = index;
    return 1;
}

/*
 * Every opaque contributor at one pixel, front to back, at most `max` of
 * them.  Returns how many were written.  Entry 0 is the topmost pixel and
 * entry 1 is the one directly beneath it, which is all the colour effects
 * need; the backdrop is not in the list and is what shows when the list
 * runs out.
 */
u32 g2d_frame_sample_bit(const G2dFrameSample *sample)
{
    if (sample->is_obj) {
        return G2D_LAYER_OBJ;
    }
    return (sample->layer < 4u) ? (1u << sample->layer) : 0u;
}

/* The enabled raster layer at (prio, bgi), or NULL. */
static const G2dLayerRaster *raster_at(const G2dFrameDesc *desc, u32 prio,
                                       u32 bgi)
{
    u32 i;

    for (i = 0; i < desc->n_rasters; i++) {
        const G2dLayerRaster *r = &desc->rasters[i];

        if (r->enabled && r->priority == prio && r->bg_index == bgi) {
            return r;
        }
    }
    return NULL;
}

static int raster_owns(const G2dFrameDesc *desc, u32 prio, u32 bgi)
{
    return raster_at(desc, prio, bgi) != NULL;
}

static int raster_sample(const G2dFrameDesc *desc, u32 prio, u32 bgi,
                         s32 x, s32 y, G2dFrameSample *out)
{
    const G2dLayerRaster *r = raster_at(desc, prio, bgi);
    const u8 *src;

    if (r == NULL || r->rgba == NULL || x < 0 || y < 0
        || (u32)x >= r->width || (u32)y >= r->height) {
        return 0;
    }
    src = r->rgba + ((size_t)(u32)y * r->width + (u32)x) * 4u;
    if (src[3] == 0) {
        return 0;
    }
    out->pal = NULL;
    out->color_index = 0;
    out->is_obj = 0;
    out->is_3d = 0;
    out->is_raster = 1;
    out->rgb[0] = src[0];
    out->rgb[1] = src[1];
    out->rgb[2] = src[2];
    out->layer = bgi;
    return 1;
}

u32 g2d_frame_stack(const G2dFrameDesc *desc, s32 x, s32 y,
                    G2dFrameSample *out, u32 max)
{
    u32 count = 0;
    u32 prio, i, bgi;
    u32 allowed = win_mask(desc->windows, x, y);

    for (prio = 0; prio < 4u && count < max; prio++) {
        for (i = 0; i < desc->n_objs && count < max; i++) {
            const G2dObjEntry *obj = &desc->objs[i];
            u8 index;

            if ((allowed & G2D_LAYER_OBJ) == 0) {
                break;   /* the window hides every OBJ at this pixel */
            }
            if (obj->priority != prio || !obj_sample(obj, x, y, &index)) {
                continue;
            }
            out[count].pal = obj->pal;
            out[count].color_index = index;
            out[count].is_obj = 1;
            out[count].is_3d = 0;
            out[count].is_raster = 0;
            out[count].layer = i;
            count++;
        }
        for (bgi = 0; bgi < G2D_MAX_BG_INDEX && count < max; bgi++) {
            u8 index;
            u32 ci;

            if (bgi < 4u && (allowed & (1u << bgi)) == 0) {
                continue;   /* the window hides this BG at this pixel */
            }
            /*
             * The 3D layer occupies one hardware layer number and takes
             * the slot: no text or affine layer at this index is
             * consulted.  Its alpha-0 pixels are transparent, so the walk
             * carries on to the next layer number there; BG0HOFS moves
             * the buffer sideways with a 512-pixel period.
             */
            if (desc->g3d != NULL && desc->g3d->enabled
                && desc->g3d->priority == prio
                && desc->g3d->bg_index == bgi) {
                const G2dLayer3d *l3 = desc->g3d;
                u32 bx = (u32)(x + l3->hofs) & 0x1FFu;

                if (l3->rgba != NULL && x >= 0 && y >= 0
                    && bx < l3->width && (u32)y < l3->height) {
                    const u8 *src = l3->rgba
                        + ((size_t)(u32)y * l3->width + bx) * 4u;

                    if (src[3] != 0) {
                        u32 a5 = ((u32)src[3] * 31u + 127u) / 255u;

                        out[count].pal = NULL;
                        out[count].color_index = 0;
                        out[count].is_obj = 0;
                        out[count].is_3d = 1;
                        out[count].is_raster = 0;
                        out[count].rgb[0] = src[0];
                        out[count].rgb[1] = src[1];
                        out[count].rgb[2] = src[2];
                        out[count].alpha5 = (u8)(a5 != 0 ? a5 : 1u);
                        out[count].layer = bgi;
                        count++;
                    }
                }
                continue;
            }
            /*
             * A pre-resolved layer takes its slot the same way, but has
             * transparent pixels: alpha 0 lets the walk carry on to the
             * next layer number, exactly as a text BG's index 0 does.
             */
            if (raster_sample(desc, prio, bgi, x, y, &out[count])) {
                count++;
                continue;
            }
            if (raster_owns(desc, prio, bgi)) {
                continue;
            }
            if (bgi < desc->n_bgs) {
                const G2dBgLayer *layer = &desc->bgs[bgi];

                if (layer->enabled && layer->priority == prio
                    && g2d_bg_sample(layer, x, y, &index, &ci) && index != 0) {
                    out[count].pal = layer->pal;
                    out[count].color_index = ci;
                    out[count].is_obj = 0;
                    out[count].is_3d = 0;
                    out[count].is_raster = 0;
                    out[count].layer = bgi;
                    count++;
                    continue;
                }
            }
            for (i = 0; i < desc->n_abgs && count < max; i++) {
                const G2dBgAffineLayer *layer = &desc->abgs[i];

                if (!layer->enabled || layer->priority != prio
                    || layer->bg_index != bgi) {
                    continue;
                }
                if (g2d_bg_affine_sample(layer, x, y, &index, &ci)
                    && index != 0) {
                    out[count].pal = layer->pal;
                    out[count].color_index = ci;
                    out[count].is_obj = 0;
                    out[count].is_3d = 0;
                    out[count].is_raster = 0;
                    out[count].layer = bgi;
                    count++;
                    break;
                }
            }
        }
    }
    return count;
}

/*
 * One pixel's colour before any effect, and which layer bit it belongs
 * to.  `n` is what g2d_frame_stack returned and `i` is how deep to look:
 * past the end of the stack is the backdrop, colour 0 of the BG palette.
 */
static void stack_color(const G2dFrameDesc *desc, const G2dFrameSample *stack,
                        u32 n, u32 i, u8 rgb[3], u32 *bit)
{
    u8 px[4];

    if (i < n) {
        if (stack[i].is_3d || stack[i].is_raster) {
            rgb[0] = stack[i].rgb[0];
            rgb[1] = stack[i].rgb[1];
            rgb[2] = stack[i].rgb[2];
            *bit = g2d_frame_sample_bit(&stack[i]);
            return;
        }
        put_color(px, stack[i].pal, stack[i].color_index);
        *bit = g2d_frame_sample_bit(&stack[i]);
    } else {
        put_color(px, desc->backdrop_pal, 0);
        *bit = G2D_LAYER_BD;
    }
    rgb[0] = px[0];
    rgb[1] = px[1];
    rgb[2] = px[2];
}

/*
 * The 6-bit stage's value of one output pixel: a 2D result is a palette
 * colour's expansion (or a 2D effect's 5-bit result's), widened by
 * g2d_c5to6; a 3D pixel passed through, or the 3D blend's 6-bit result
 * expanded by g2d_c6to8, folds back exactly through g2d_c8to6.
 */
static u8 g_six_lut[2][256];
static int g_six_ready;

/* Both folds are pure functions of one byte.  Filled once, on the thread that
 * starts a frame (g2d_tables_init), before the compositors run on the worker
 * pool: a lazy fill from several threads would be a data race. */
void g2d_tables_init(void)
{
    u32 i;

    if (!g_six_ready) {
        for (i = 0; i < 256u; i++) {
            g_six_lut[0][i] = (u8)g2d_c5to6(((u32)i * 31u + 127u) / 255u);
            g_six_lut[1][i] = (u8)g2d_c8to6((u8)i);
        }
        g_six_ready = 1;
    }
    (void)g2d_sin_fx(0);
}

static void six_of(const u8 out[3], int is3d, u8 o6[3])
{
    const u8 (*lut)[256] = g_six_lut;

    o6[0] = lut[is3d != 0][out[0]];
    o6[1] = lut[is3d != 0][out[1]];
    o6[2] = lut[is3d != 0][out[2]];
}

/*
 * The colour effects at one pixel, `out` holding the topmost colour on
 * entry.  Returns non-zero when what is left in `out` is a 3D colour (the
 * pixel passed through, or the 3D layer's own blend), zero when it is a 2D
 * colour or a 2D effect's 5-bit result: which of the two six_of needs.
 */
static int blend_pixel(const G2dFrameDesc *desc, const G2dFrameSample *stack,
                       u32 n, const u8 top[3], u32 top_bit, u32 x, u32 y,
                       u8 *out)
{
    int top3d = (n > 0 && stack[0].is_3d) ? 1 : 0;
    u8 below[3];
    u32 below_bit;

    /*
     * The DS rule, in the order the hardware applies it: the window at
     * this pixel must allow the effect, the topmost pixel must belong to
     * the first target set, and for alpha the pixel directly beneath it
     * must belong to the second target set.  Nothing else blends.
     */
    if ((win_mask(desc->windows, (s32)x, (s32)y)
         & G2D_LAYER_EFFECT) == 0) {
        return top3d;
    }
    /*
     * The 3D layer's own blend comes first: a 3D pixel on top of a second
     * target is mixed with it by its per-pixel alpha, EVA and EVB unused,
     * whatever the effect field says (g2d.h, G2dLayer3d).  Otherwise the
     * 3D pixel takes the ordinary rules below as a BG0 pixel.
     */
    if (top3d) {
        stack_color(desc, stack, n, 1, below, &below_bit);
        if ((below_bit & desc->blend->second) != 0) {
            u8 c6[3];

            if (stack[0].alpha5 >= 31u) {
                return 1;   /* (3D * 32 + 2D * 0) / 32: the 3D pixel */
            }
            g2d_blend_3d(top, below, stack[0].alpha5, c6);
            out[0] = g2d_c6to8(c6[0]);
            out[1] = g2d_c6to8(c6[1]);
            out[2] = g2d_c6to8(c6[2]);
            return 1;
        }
    }
    /*
     * A semi-transparent OBJ is the other exception: such a pixel counts
     * as a first target whatever BLDCNT's bit 4 says and is alpha blended
     * whatever its effect field says, provided the pixel beneath it is a
     * second target.  When it is not, the OBJ falls through to the
     * ordinary rule below (so a brightness effect still applies to it if
     * BLDCNT names the OBJ layer).
     */
    if (n > 0 && stack[0].is_obj && stack[0].layer < desc->n_objs
        && desc->objs[stack[0].layer].semi) {
        stack_color(desc, stack, n, 1, below, &below_bit);
        if ((below_bit & desc->blend->second) != 0) {
            if (desc->blend_over_3d != NULL && n > 1 && stack[1].is_3d) {
                *desc->blend_over_3d = 1;
            }
            g2d_blend_alpha(top, below, desc->blend->eva,
                            desc->blend->evb, out);
            return 0;
        }
    }
    if (desc->blend->effect == G2D_BLEND_NONE) {
        return top3d;
    }
    if ((top_bit & desc->blend->first) == 0) {
        return top3d;
    }
    if (desc->blend->effect == G2D_BLEND_ALPHA) {
        stack_color(desc, stack, n, 1, below, &below_bit);
        if ((below_bit & desc->blend->second) == 0) {
            return top3d;
        }
        if (desc->blend_over_3d != NULL && n > 1 && stack[1].is_3d) {
            *desc->blend_over_3d = 1;
        }
        g2d_blend_alpha(top, below, desc->blend->eva,
                        desc->blend->evb, out);
    } else {
        g2d_blend_brightness(top, desc->blend->evy,
                             desc->blend->effect == G2D_BLEND_BRIGHT_UP, out);
    }
    return 0;
}

/*
 * The per-frame plan: what g2d_frame_stack decides pixel by pixel from the
 * descriptor alone (which layers exist, their priority and layer number),
 * resolved once into the list of steps it walks, front to back.  Only the
 * per-pixel parts (window, sample) are left for the pixel loop.  A step is
 * one candidate layer; the candidates of one (priority, layer number) are
 * adjacent, and the first that hits ends that group, as in g2d_frame_stack.
 */
enum { PLAN_OBJ, PLAN_3D, PLAN_RASTER, PLAN_BG, PLAN_ABG };

typedef struct {
    u8 kind;
    u8 key;        /* priority * 8 + layer number                    */
    u8 bgi;
    u8 pad;
    u32 idx;       /* the object, raster, BG or affine layer index   */
} PlanStep;

static void plan_add(PlanStep *plan, u32 *n, u8 kind, u32 key, u32 bgi,
                     u32 idx)
{
    plan[*n].kind = kind;
    plan[*n].key = (u8)key;
    plan[*n].bgi = (u8)bgi;
    plan[*n].pad = 0;
    plan[*n].idx = idx;
    (*n)++;
}

static u32 plan_build(const G2dFrameDesc *desc, PlanStep *plan)
{
    u32 n = 0, prio, i, bgi;

    for (prio = 0; prio < 4u; prio++) {
        for (i = 0; i < desc->n_objs; i++) {
            if (desc->objs[i].priority == prio) {
                plan_add(plan, &n, PLAN_OBJ, prio * 8u, 0, i);
            }
        }
        for (bgi = 0; bgi < G2D_MAX_BG_INDEX; bgi++) {
            u32 key = prio * 8u + bgi;
            const G2dLayerRaster *r;

            if (desc->g3d != NULL && desc->g3d->enabled
                && desc->g3d->priority == prio && desc->g3d->bg_index == bgi) {
                plan_add(plan, &n, PLAN_3D, key, bgi, 0);
                continue;
            }
            r = raster_at(desc, prio, bgi);
            if (r != NULL) {
                plan_add(plan, &n, PLAN_RASTER, key, bgi,
                         (u32)(r - desc->rasters));
                continue;
            }
            if (bgi < desc->n_bgs && desc->bgs[bgi].enabled
                && desc->bgs[bgi].priority == prio) {
                plan_add(plan, &n, PLAN_BG, key, bgi, bgi);
            }
            for (i = 0; i < desc->n_abgs; i++) {
                const G2dBgAffineLayer *a = &desc->abgs[i];

                if (a->enabled && a->priority == prio && a->bg_index == bgi) {
                    plan_add(plan, &n, PLAN_ABG, key, bgi, i);
                }
            }
        }
    }
    return n;
}

static void put_sample(G2dFrameSample *o, const G2dPalette *pal, u32 ci,
                       u32 layer, int is_obj)
{
    o->pal = pal;
    o->color_index = ci;
    o->is_obj = is_obj;
    o->is_3d = 0;
    o->is_raster = 0;
    o->layer = layer;
}

/* g2d_frame_stack over a plan: the same samples, in the same order. */
static u32 plan_stack(const G2dFrameDesc *desc, const PlanStep *plan, u32 np,
                      s32 x, s32 y, const u8 *src3, G2dFrameSample *out,
                      u32 max)
{
    u32 count = 0, k;
    u32 allowed = win_mask(desc->windows, x, y);
    int done_key = -1;

    for (k = 0; k < np && count < max; k++) {
        const PlanStep *st = &plan[k];
        u8 index;
        u32 ci;

        if (st->kind == PLAN_OBJ) {
            const G2dObjEntry *obj;

            if ((allowed & G2D_LAYER_OBJ) == 0) {
                continue;
            }
            obj = &desc->objs[st->idx];
            if (obj_sample(obj, x, y, &index)) {
                put_sample(&out[count], obj->pal, index, st->idx, 1);
                count++;
            }
            continue;
        }
        if ((st->bgi < 4u && (allowed & (1u << st->bgi)) == 0)
            || (int)st->key == done_key) {
            continue;
        }
        switch (st->kind) {
        case PLAN_3D:
            done_key = st->key;
            if (src3 != NULL) {
                u32 a5 = ((u32)src3[3] * 31u + 127u) / 255u;

                put_sample(&out[count], NULL, 0, st->bgi, 0);
                out[count].is_3d = 1;
                out[count].rgb[0] = src3[0];
                out[count].rgb[1] = src3[1];
                out[count].rgb[2] = src3[2];
                out[count].alpha5 = (u8)(a5 != 0 ? a5 : 1u);
                count++;
            }
            break;
        case PLAN_RASTER: {
            const G2dLayerRaster *r = &desc->rasters[st->idx];
            const u8 *src;

            done_key = st->key;
            if (r->rgba == NULL || x < 0 || y < 0
                || (u32)x >= r->width || (u32)y >= r->height) {
                break;
            }
            src = r->rgba + ((size_t)(u32)y * r->width + (u32)x) * 4u;
            if (src[3] == 0) {
                break;
            }
            put_sample(&out[count], NULL, 0, st->bgi, 0);
            out[count].is_raster = 1;
            out[count].rgb[0] = src[0];
            out[count].rgb[1] = src[1];
            out[count].rgb[2] = src[2];
            count++;
            break;
        }
        case PLAN_BG: {
            const G2dBgLayer *layer = &desc->bgs[st->idx];

            if (g2d_bg_sample(layer, x, y, &index, &ci) && index != 0) {
                put_sample(&out[count], layer->pal, ci, st->bgi, 0);
                count++;
                done_key = st->key;
            }
            break;
        }
        default: {
            const G2dBgAffineLayer *layer = &desc->abgs[st->idx];

            if (g2d_bg_affine_sample(layer, x, y, &index, &ci) && index != 0) {
                put_sample(&out[count], layer->pal, ci, st->bgi, 0);
                count++;
                done_key = st->key;
            }
            break;
        }
        }
    }
    return count;
}

/*
 * The 3D layer's pixel for DS pixel (x, y), sub-pixel (sx, sy) of a layer
 * rendered at `scale` times the DS size, or NULL where it is transparent
 * (no layer, outside the buffer, alpha 0).  BG0HOFS moves whole DS pixels.
 */
static const u8 *l3_src(const G2dLayer3d *l3, u32 x, u32 y, u32 sx, u32 sy,
                        u32 scale)
{
    u32 bx;
    const u8 *src;

    if (l3 == NULL || !l3->enabled || l3->rgba == NULL) {
        return NULL;
    }
    bx = (u32)((s32)x + l3->hofs) & 0x1FFu;
    if (bx >= l3->width || y >= l3->height) {
        return NULL;
    }
    src = l3->rgba + (((size_t)y * scale + sy) * ((size_t)l3->width * scale)
                      + (size_t)bx * scale + sx) * 4u;
    return src[3] != 0 ? src : NULL;
}

/* One DS pixel's colour (and 6-bit stage) given its 3D sample. */
static void compose_px(const G2dFrameDesc *desc, const PlanStep *plan,
                       u32 np, u32 x, u32 y, const u8 *src3, u8 *out,
                       u8 *out6, u8 *zero6)
{
    G2dFrameSample stack[2];
    u8 top[3];
    u32 top_bit, n;
    int is3d;

    /*
     * The pixel beneath the top one is read only by the 3D blend, a
     * semi-transparent OBJ and alpha blending (blend_pixel), so the walk
     * stops at the first layer unless one of those can apply; the answer
     * is the same either way.
     */
    n = plan_stack(desc, plan, np, (s32)x, (s32)y, src3, stack, 1);
    if (n == 1 && desc->blend != NULL
        && (stack[0].is_3d
            || (stack[0].is_obj && stack[0].layer < desc->n_objs
                && desc->objs[stack[0].layer].semi)
            || (desc->blend->effect == G2D_BLEND_ALPHA
                && (g2d_frame_sample_bit(&stack[0])
                    & desc->blend->first) != 0))) {
        n = plan_stack(desc, plan, np, (s32)x, (s32)y, src3, stack, 2);
    }
    stack_color(desc, stack, n, 0, top, &top_bit);

    out[0] = top[0];
    out[1] = top[1];
    out[2] = top[2];
    out[3] = 255;

    is3d = (n > 0 && stack[0].is_3d) ? 1 : 0;
    if (desc->blend != NULL) {
        is3d = blend_pixel(desc, stack, n, top, top_bit, x, y, out);
    }
    if (out6 != NULL) {
        six_of(out, is3d, out6);
    }
    if (zero6 != NULL) {
        /* the same pixel with the 3D layer's colours black: different only
         * where a 3D sample takes part */
        u32 k;
        int any3d = 0;

        for (k = 0; k < n; k++) {
            if (stack[k].is_3d) {
                stack[k].rgb[0] = stack[k].rgb[1] = stack[k].rgb[2] = 0;
                any3d = 1;
            }
        }
        if (!any3d && out6 != NULL) {
            memcpy(zero6, out6, 3u);
        } else {
            u8 z[4];

            stack_color(desc, stack, n, 0, top, &top_bit);
            z[0] = top[0];
            z[1] = top[1];
            z[2] = top[2];
            z[3] = 255;
            is3d = (n > 0 && stack[0].is_3d) ? 1 : 0;
            if (desc->blend != NULL) {
                is3d = blend_pixel(desc, stack, n, top, top_bit, x, y, z);
            }
            six_of(z, is3d, zero6);
        }
    }
}

/* The rows [y0, y1) of a frame: independent of every other row, so bands of
 * them run on the worker pool. */
typedef struct ComposeCtx {
    const G2dFrameDesc *desc;
    const PlanStep *plan;
    u32 np, width, height, n3, w_out;
    const G2dLayer3d *l3;
    u8 *rgba;
} ComposeCtx;

/* The plan for one row: the OBJ steps of OBJs that do not cross row y are
 * dropped (they cannot hit any pixel of it), the rest is kept in order, so
 * the walk is the same with only the OBJs that can answer.  A frame has up
 * to 128 OBJs and a row crosses a handful. */
static u32 row_plan(const G2dFrameDesc *desc, const PlanStep *plan, u32 np,
                    s32 y, PlanStep *out)
{
    u32 k, n = 0;

    for (k = 0; k < np; k++) {
        if (plan[k].kind == PLAN_OBJ) {
            const G2dObjEntry *obj = &desc->objs[plan[k].idx];

            if (obj->pose == NULL || obj->pose->indices == NULL
                || y < obj->y || y - obj->y >= (s32)obj->pose->height) {
                continue;
            }
        }
        out[n++] = plan[k];
    }
    return n;
}

static void compose_rows(const ComposeCtx *c, u32 y0, u32 y1)
{
    const G2dFrameDesc *desc = c->desc;
    u32 x, y, sx, sy;
    u32 n3 = c->n3, w_out = c->w_out;
    const PlanStep *plan = c->plan;
    u32 np = c->np;
    PlanStep *rp = (PlanStep *)malloc((np ? np : 1u) * sizeof(*rp));
    ComposeCtx row = *c;

    if (rp == NULL) {
        return;
    }
    c = &row;
    row.plan = rp;
    for (y = y0; y < y1; y++) {
        row.np = row_plan(desc, plan, np, (s32)y, rp);
        for (x = 0; x < c->width; x++) {
            u8 base[4], base6[3];
            int have_base = 0;

            if (n3 == 1u) {
                compose_px(desc, c->plan, c->np, x, y,
                           l3_src(c->l3, x, y, 0, 0, 1u),
                           c->rgba + ((size_t)y * w_out + x) * 4u,
                           desc->out6 != NULL
                               ? desc->out6 + ((size_t)y * w_out + x) * 3u
                               : NULL,
                           desc->zero6 != NULL
                               ? desc->zero6 + ((size_t)y * w_out + x) * 3u
                               : NULL);
                continue;
            }
            /* The 2D result of this DS pixel is shared by every sub-pixel
             * the 3D layer leaves transparent. */
            for (sy = 0; sy < n3; sy++) {
                for (sx = 0; sx < n3; sx++) {
                    size_t at = (size_t)(y * n3 + sy) * w_out + (x * n3 + sx);
                    const u8 *p3 = l3_src(c->l3, x, y, sx, sy, n3);
                    u8 *out = c->rgba + at * 4u;
                    u8 *o6 = desc->out6 != NULL ? desc->out6 + at * 3u : NULL;

                    if (p3 != NULL) {
                        compose_px(desc, c->plan, c->np, x, y, p3, out, o6, NULL);
                        continue;
                    }
                    if (!have_base) {
                        compose_px(desc, c->plan, c->np, x, y, NULL, base,
                                   base6, NULL);
                        have_base = 1;
                    }
                    memcpy(out, base, 4u);
                    if (o6 != NULL) {
                        memcpy(o6, base6, 3u);
                    }
                }
            }
        }
    }
    free(rp);
}

#define COMPOSE_BANDS 4u

static void compose_band(void *arg, unsigned band)
{
    const ComposeCtx *c = (const ComposeCtx *)arg;
    u32 y0 = c->height * band / COMPOSE_BANDS;
    u32 y1 = c->height * (band + 1u) / COMPOSE_BANDS;

    compose_rows(c, y0, y1);
}

void g2d_frame_compose_desc_scaled(const G2dFrameDesc *desc, u32 width,
                                   u32 height, u32 scale, u8 *rgba)
{
    u32 np = 0;
    u32 n3 = (scale == 0) ? 1u : scale;
    PlanStep *plan;
    const G2dLayer3d *l3 = desc->g3d;
    ComposeCtx ctx;

    if (l3 != NULL && l3->enabled && (l3->scale == 0 ? 1u : l3->scale) != n3) {
        return;   /* a layer rendered at another scale cannot be mixed */
    }
    g2d_tables_init();   /* a no-op after the first call */

    /* Each layer appears in at most one step; a raster or the 3D layer
     * replaces the BG and affine steps of its slot. */
    plan = (PlanStep *)malloc(((size_t)desc->n_objs + desc->n_rasters + 1u
                               + desc->n_bgs + desc->n_abgs + 8u)
                              * sizeof(PlanStep));
    if (plan == NULL) {
        return;
    }
    np = plan_build(desc, plan);

    ctx.desc = desc;
    ctx.plan = plan;
    ctx.np = np;
    ctx.width = width;
    ctx.height = height;
    ctx.n3 = n3;
    ctx.w_out = width * n3;
    ctx.l3 = l3;
    ctx.rgba = rgba;
    if (height >= COMPOSE_BANDS * 8u) {
        g2d_par_run(COMPOSE_BANDS, compose_band, &ctx);
    } else {
        compose_rows(&ctx, 0, height);
    }
    free(plan);
}

void g2d_frame_compose_desc(const G2dFrameDesc *desc, u32 width, u32 height,
                            u8 *rgba)
{
    g2d_frame_compose_desc_scaled(desc, width, height, 1u, rgba);
}

void g2d_frame_compose(const G2dBgLayer *bgs, u32 n_bgs,
                       const G2dObjEntry *objs, u32 n_objs,
                       const G2dPalette *backdrop_pal,
                       u32 width, u32 height, u8 *rgba)
{
    G2dFrameDesc desc;

    memset(&desc, 0, sizeof(desc));
    desc.bgs = bgs;
    desc.n_bgs = n_bgs;
    desc.objs = objs;
    desc.n_objs = n_objs;
    desc.backdrop_pal = backdrop_pal;
    g2d_frame_compose_desc(&desc, width, height, rgba);
}
