/*
 * engine/g2d/g2d.h -- the DS 2D (OBJ/OAM) software compositor.
 *
 * This is the first piece of eidos's 2D engine shim: a pure C99 library,
 * no SDL and no third-party code, that turns the Nitro 2D resources the
 * game ships (NCGR character data, NCLR palettes, NCER cell banks, NANR
 * cell animations, NMCR multi-cell node tables) into a 32-bit RGBA
 * buffer, using the DS OBJ rules the decompilation attests.
 *
 * Where the behaviour comes from, in order of authority:
 *
 *   1. aletheia's matched sources.  src/matched/unit_02067B40.c is the
 *      composer's flip lane and states the per-OAM flip arithmetic
 *      (x' = -(x + objWidth), y' = -(y + objHeight)) together with the
 *      two OBJ dimension tables at 0x020A17F0 / 0x020A17D8.
 *      src/matched/unit_020640B0.c is the per-node SRT control block and
 *      states that the NMCR node's own signed (x, y) is written into the
 *      node's translation verbatim, with no second placement source.
 *   2. aletheia's docs/ASSETS.md, which records the attested painter's
 *      order (func_020643CC walks nodes forward, func_02067998 walks a
 *      cell's OAMs forward, the earliest-emitted OAM lands in the lowest
 *      hardware OAM slot, and a lower slot draws in front; attr2's
 *      priority outranks the slot index), and the affine double-size
 *      correction from func_02066E30 (attr0 bits 8-9 == 3 shifts the
 *      entry by half its own width and height).
 *   3. public hardware documentation of the NDS OBJ attribute layout for
 *      the plain bitfield positions (shape/size/flip/priority/palette).
 *
 * Nothing here is derived from any other decompilation, port or emulator.
 *
 * Ownership: every parser below borrows the caller's buffer and does not
 * copy it, except where a field is documented as owned.  A G2dRig owns
 * everything it loaded and is released with g2d_rig_free().
 */

#ifndef EIDOS_ENGINE_G2D_H
#define EIDOS_ENGINE_G2D_H

#include <stddef.h>

#include "port.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* BIOS-style container decompression (LZ77 0x10 / LZ11 0x11)          */
/* ------------------------------------------------------------------ */

/*
 * A NARC sub-file may be wrapped in a BIOS compression container.  The
 * tag byte is 0x10 (LZ77) or 0x11 (LZ11) with a 24-bit decompressed size
 * in the next three bytes; LZ11 spells a zero there as "a 32-bit size
 * follows".  Any other tag is not a container this engine decodes.
 *
 * Returns a malloc'd buffer of *out_size bytes, or NULL when `data` does
 * not open with a container this decoder handles, or when the stream is
 * malformed.  A stream that reaches its declared size with more than
 * three bytes of its container left over is rejected: the tag byte was
 * not a real header, just a coincidental first byte (the same guard
 * aletheia's tools/assets/narc.py applies, and the reason several rig
 * sub-files that "sniff" as LZ77 are skipped rather than mis-decoded).
 */
u8 *g2d_decompress(const u8 *data, u32 size, u32 *out_size);

/* ------------------------------------------------------------------ */
/* NCLR -- palette                                                     */
/* ------------------------------------------------------------------ */

typedef struct G2dColor {
    u8 r, g, b;
} G2dColor;

typedef struct G2dPalette {
    G2dColor *colors;   /* owned */
    u32       count;
} G2dPalette;

/*
 * Append one RLCN (NCLR) resource's colours to `pal`, growing it.  BGR555
 * is expanded to 8 bits per channel as `c * 255 / 31`.  A block's OAM
 * palette-bank nibble indexes into the concatenation of every NCLR of
 * that block, in local order, which is what g2d_rig_load() builds.
 */
int  g2d_palette_append_nclr(G2dPalette *pal, const u8 *data, u32 size);
void g2d_palette_free(G2dPalette *pal);

/* ------------------------------------------------------------------ */
/* NCGR -- character (tile) data                                       */
/* ------------------------------------------------------------------ */

#define G2D_NCGR_FMT_4BPP 3
#define G2D_NCGR_FMT_8BPP 4

/* "unspecified, derive from data" sentinel in the CHAR block's dims */
#define G2D_NCGR_DIM_SENTINEL 0xFFFFu

typedef struct G2dNcgr {
    u16 tile_num_x;      /* sheet width in 8x8 blocks, or the sentinel  */
    u16 tile_num_y;      /* sheet height in 8x8 blocks, or the sentinel */
    u32 pixel_fmt;       /* 3 = 4bpp, 4 = 8bpp                          */
    u32 mapping_type;
    u32 char_format;     /* 0 = 8x8 tiled, 1 = linear bitmap            */
    int sentinel_dims;   /* either dimension is 0xFFFF                  */
    u32 width, height;   /* pixels; 0 when sentinel_dims                */
    u8 *pixels;          /* owned, width*height palette indices, or NULL */
    const u8 *tile_data; /* borrowed from the caller's buffer            */
    u32 tile_data_size;
    u32 bytes_per_tile;  /* 32 (4bpp) or 64 (8bpp)                       */
} G2dNcgr;

int  g2d_ncgr_parse(G2dNcgr *out, const u8 *data, u32 size);
void g2d_ncgr_free(G2dNcgr *ncgr);

/*
 * One 8x8 block's 64 indices, row-major.  The block index is
 * two-dimensional over the sheet: block i is at column i % tile_num_x,
 * row i / tile_num_x.  Out-of-range blocks read as index 0.
 */
void g2d_ncgr_block(const G2dNcgr *ncgr, u32 block_index, u8 out[64]);

/* ------------------------------------------------------------------ */
/* NCER -- cell bank (OAM records)                                     */
/* ------------------------------------------------------------------ */

typedef struct G2dOam {
    s32 x, y;          /* cell-local, with the attested corrections applied */
    u16 shape, size;
    u16 tile_idx;
    u16 priority;      /* attr2 bits 10-11, lower draws in front            */
    u16 pal;           /* attr2 bits 12-15, 16-colour bank selector         */
    int hflip, vflip;
    int affine_double; /* attr0 bits 8-9 == 3                               */
    u32 w, h;          /* object dimensions from shape/size                 */
} G2dOam;

typedef struct G2dCell {
    u16     num_oam;
    u16     cell_attr;
    G2dOam *oams;      /* owned */
} G2dCell;

typedef struct G2dNcer {
    u16      num_cells;
    u16      cell_info_type;  /* 0 = 8-byte cell record, 1 = 16-byte + bbox */
    u32      mapping_type;
    G2dCell *cells;           /* owned */
} G2dNcer;

int  g2d_ncer_parse(G2dNcer *out, const u8 *data, u32 size);
void g2d_ncer_free(G2dNcer *ncer);

/* The two OBJ dimension tables, indexed [shape][size]. */
u32 g2d_obj_width(u32 shape, u32 size);
u32 g2d_obj_height(u32 shape, u32 size);

/* ------------------------------------------------------------------ */
/* NANR -- cell animation                                             */
/* ------------------------------------------------------------------ */

#define G2D_ANIM_ELEMENT_INDEX 0
#define G2D_ANIM_ELEMENT_SRT   1
#define G2D_ANIM_ELEMENT_T     2

typedef struct G2dNanrSequence {
    u16 num_frames;
    u16 loop_start;
    u32 anim_type;   /* low byte selects the frame-data record shape */
    u32 play_mode;
    u32 frames_ofs;  /* byte offset into the shared frame-entry array */
} G2dNanrSequence;

typedef struct G2dNanr {
    u16 num_sequences;
    u16 num_elements;
    u32 seq_ofs, frame_ofs, frame_data_ofs;
    const u8 *body;   /* borrowed */
    u32 body_size;
    G2dNanrSequence *sequences; /* owned */
} G2dNanr;

typedef struct G2dFrame {
    u16 cell_idx;
    s16 px, py;
    s32 sx, sy;   /* fx32, 4096 == 1.0 */
    u16 rot_z;
} G2dFrame;

int  g2d_nanr_parse(G2dNanr *out, const u8 *data, u32 size);
void g2d_nanr_free(G2dNanr *nanr);

/* 0 on success, -1 when the sequence has no such frame. */
int  g2d_nanr_frame_data(const G2dNanr *nanr, u32 seq, u32 frame, G2dFrame *out);

/*
 * The frame index active at elapsed time `t` (NDS frames), summing the
 * per-entry durations and looping from loopStartFrameIdx once the
 * sequence's own total is exceeded -- the same walk func_020643CC runs.
 */
u32 g2d_nanr_frame_at_time(const G2dNanr *nanr, u32 seq, u32 t);

/* ------------------------------------------------------------------ */
/* NMCR -- multi-cell node placement                                   */
/* ------------------------------------------------------------------ */

typedef struct G2dMcNode {
    u16 anim_seq_idx;
    s16 x, y;
    u8  node_attr;
    u8  cell_anim_slot;
} G2dMcNode;

typedef struct G2dMcGroup {
    u16        num_nodes;
    u16        num_cell_anims;
    G2dMcNode *nodes;   /* owned */
} G2dMcGroup;

typedef struct G2dNmcr {
    u16         num_multi_cell;
    u16         signature;
    G2dMcGroup *groups; /* owned */
} G2dNmcr;

int  g2d_nmcr_parse(G2dNmcr *out, const u8 *data, u32 size);
void g2d_nmcr_free(G2dNmcr *nmcr);

/* ------------------------------------------------------------------ */
/* Affine (rotation and scaling)                                       */
/* ------------------------------------------------------------------ */

/*
 * The DS transforms in fixed point, and so does this engine.  The full
 * derivation, the reference-point rule and the deliberate divergence from
 * aletheia's rig.py are in engine/g2d/affine.c's header; the short form:
 *
 *   - a 4096-entry sine table over one turn, fx32 valued, indexed by a
 *     16-bit binary angle shifted right by four (which is what a NANR
 *     frame's rotZ, 1/65536 of a turn, feeds directly);
 *   - PA/PB/PC/PD in 8.8, holding the *inverse* of the intended
 *     transform, because the hardware walks output pixels and asks each
 *     one which texel it shows;
 *   - the product shifted right eight with the fraction discarded, so the
 *     sample is a whole texel: the hardware's nearest neighbour.
 *
 * rig.py inverse-maps the same geometry in double precision and rounds to
 * nearest.  Where the two disagree, on a pixel near a texel boundary, the
 * engine is the reference: it is what the hardware computes.
 */

#define G2D_FX_ONE 4096   /* fx32 1.0, the unit of a NANR frame's sx/sy */

/* fx32 sine/cosine of a 16-bit binary angle (65536 == one turn). */
s32 g2d_sin_fx(u16 angle);
s32 g2d_cos_fx(u16 angle);

/* The four 8.8 OBJ affine parameters (BGxPA..BGxPD hold the same). */
typedef struct G2dAffine {
    s16 pa, pb, pc, pd;
} G2dAffine;

int  g2d_affine_is_identity(s32 sx, s32 sy, u16 rot_z);
void g2d_affine_from_srt(s32 sx, s32 sy, u16 rot_z, G2dAffine *out);

/* One inverse map: an output offset from the reference point to a source
 * offset from the source reference point, fraction discarded. */
void g2d_affine_map(const G2dAffine *m, s32 dx, s32 dy, s32 *sx, s32 *sy);

/*
 * The drawn canvas of an affine OBJ: W x H normally, 2W x 2H with attr0
 * bits 8-9 == 3 (the double-size mode), centred on the same point either
 * way.
 */
void g2d_obj_affine_size(u32 w, u32 h, int double_size, u32 *out_w, u32 *out_h);

/*
 * One OBJ's w*h texel block through the matrix onto its drawn canvas
 * (caller-allocated, the size g2d_obj_affine_size reports).  The output
 * reference point is the canvas centre and the source reference point is
 * the block's centre; a pixel whose source lands outside the block is
 * left as index 0.  Returns 0, or -1 on a bad argument.
 */
int g2d_obj_affine_render(const u8 *src, u32 w, u32 h, const G2dAffine *m,
                          int double_size, u8 *out);

/*
 * A loose raster with its origin recorded: `(ox, oy)` is where the
 * transform's own origin sits inside the buffer, so a local coordinate
 * (x, y) is buffer index (x + ox, y + oy).
 */
typedef struct G2dRaster {
    u32 w, h;
    s32 ox, oy;
    u8 *px;      /* owned, w*h indices */
} G2dRaster;

void g2d_raster_free(G2dRaster *r);

/*
 * Resample one already-composed raster through a frame's (sx, sy, rotZ)
 * about the raster's own origin, sizing the output canvas to the forward
 * transform of the source bounds.  This is the cell-level counterpart of
 * g2d_obj_affine_render: the NNS cell animation carries one SRT per node,
 * applied about the cell origin (aletheia's src/matched/unit_020640B0.c
 * and its docs/notes/g2d_srt_setters.c.txt), not one matrix per OAM about
 * each OAM's own centre.  The per-pixel arithmetic is identical.
 */
int g2d_affine_resample(const u8 *src, u32 sw, u32 sh, s32 src_ox, s32 src_oy,
                        s32 sx, s32 sy, u16 rot_z, G2dRaster *out);

/* ------------------------------------------------------------------ */
/* Compositing                                                         */
/* ------------------------------------------------------------------ */

typedef struct G2dPose {
    u32 width, height;
    u8 *indices;   /* owned, width*height palette indices; 0 = transparent */
    s32 ox, oy;    /* where the rig's origin falls on the canvas: the
                      frames of one animation line up on it */
} G2dPose;

/*
 * Compose one multi-cell group at elapsed time `t` onto its own bounding
 * canvas (two pixels of margin on every side, the convention the oracle
 * uses so the two rasters are directly comparable).
 *
 * A node whose active frame carries a non-identity SRT has its whole cell
 * composed at identity and then resampled through the frame's matrix by
 * g2d_affine_resample, which is what the hardware does with the node's
 * OAM entries and what the asset lane's rig.py does in doubles.
 *
 * Returns 0 on success and -1 on a parse/bounds failure.
 * G2D_ERR_NONIDENTITY is kept for callers that still test for it; it is
 * no longer returned, because the affine path is implemented.
 */
#define G2D_ERR_NONIDENTITY (-2)

int  g2d_compose_pose(const G2dNcgr *ncgr, const G2dNcer *ncer,
                      const G2dNanr *nanr, const G2dMcGroup *group,
                      u32 t, u32 n_pal_colors, G2dPose *out);
void g2d_pose_free(G2dPose *pose);

/*
 * Expand a composed pose into a 32-bit RGBA buffer (width*height*4 bytes,
 * caller-allocated).  Index 0 is the transparent/background index and is
 * written as the opaque dark grey (16, 16, 16) the oracle uses so a
 * pixel-for-pixel comparison has no alpha ambiguity; an index past the
 * end of the palette is written as magenta (255, 0, 255).  Every pixel
 * is fully opaque.
 */
void g2d_pose_to_rgba(const G2dPose *pose, const G2dPalette *pal, u8 *rgba);

/* ------------------------------------------------------------------ */
/* NSCR -- BG screen (tile map)                                        */
/* ------------------------------------------------------------------ */

/*
 * The second piece of the 2D engine: text-mode BG layers.
 *
 * Where the behaviour comes from, in the same order of authority as the
 * OBJ half above:
 *
 *   1. aletheia's matched sources.  src/matched/func_02040EE0.c is the
 *      (x, y) -> screen-entry index helper the whole BG layer uses and
 *      spells out the VRAM screen layout of every size the game's size
 *      code enumerates (see g2d_bg_screen_index).  docs/notes for
 *      func_0203FCA0 (bg_sys.c's per-layer initialiser) pins the BGxCNT
 *      bit assignment this engine's layer fields mirror: bits 0-1
 *      priority, bits 2-4 character base, bit 7 the 16/256 colour mode,
 *      bits 8-12 screen base, bit 13 (extended palette for a text layer,
 *      wraparound for an affine one), bits 14-15 screen size.
 *      src/matched/func_02042ED8.c is the text-screen fill and shows the
 *      screen entry is one u16 per tile; src/matched/func_02042E34.c is
 *      the 8bpp-screen fill, one byte per entry, which is the affine
 *      case this engine does not render yet.
 *   2. aletheia's tools/assets/background.py and docs/ASSETS.md's NSCR
 *      sections for the on-disk NSCR chunk and for the NCGR/NCLR pairing
 *      rule (a property of the archives, not of the hardware, so the
 *      engine does not decide it: the caller names the pair).
 *   3. public hardware documentation of the NDS BG screen entry for the
 *      plain bitfield positions.
 */

/* Screen entry (u16): tile index, H/V flip, 16-colour palette row. */
#define G2D_SCR_TILE(e)  ((u16)((e) & 0x3FF))
#define G2D_SCR_HFLIP(e) ((int)(((e) >> 10) & 1))
#define G2D_SCR_VFLIP(e) ((int)(((e) >> 11) & 1))
#define G2D_SCR_PAL(e)   ((u16)(((e) >> 12) & 0xF))

typedef struct G2dNscr {
    u16  width, height;        /* pixels, as the SCRN sub-header declares */
    u16  tiles_wide, tiles_high;
    u32  unk;                  /* preserved verbatim; 0 or 1 in this ROM  */
    u16 *entries;              /* owned */
    u32  count;
} G2dNscr;

int  g2d_nscr_parse(G2dNscr *out, const u8 *data, u32 size);
void g2d_nscr_free(G2dNscr *nscr);

/*
 * Read one archive member whole and decompress it when it opens with a
 * BIOS container this engine decodes.  Returns a malloc'd buffer of
 * *out_size bytes that the caller frees, or NULL.  (The rig loader has
 * its own copy of this that hands ownership to the G2dRig; a BG screen
 * has no bundle object to own it, so the caller does.)
 */
u8 *g2d_arc_member(BwArcFile *arc, u32 index, u32 *out_size);

/*
 * One 8x8 tile's 64 indices out of an NCGR's character data, addressed
 * flat by `tile_index * bytes_per_tile` -- which is how a BG screen entry
 * addresses character memory, independent of the CHAR sub-header's
 * tile_num_x/y (those describe a sheet's shape, and a BG tileset's are
 * routinely the 0xFFFF sentinel).  A tile index past the end of the
 * character data reads as 64 zeroes, the same tolerance
 * background.py's NcgrTiles.tile_pixels applies.
 */
void g2d_bg_tile(const G2dNcgr *ncgr, u32 tile_index, int hflip, int vflip,
                 u8 out[64]);

/*
 * The game's BG screen-size codes, as src/matched/func_02040EE0.c
 * enumerates them, and that function's own (x, y) -> entry index mapping.
 * Code 0 is the 16x16-tile (128x128 pixel) affine size; codes 1 to 4 are
 * the four text-mode sizes.  Code 3 (512x256) puts the second 32x32 map
 * after the first, and code 4 (512x512) lays four out in a 2x2 grid --
 * the reason a BG layer cannot simply index its map row-major.
 */
#define G2D_BG_SIZE_128x128 0u
#define G2D_BG_SIZE_256x256 1u
#define G2D_BG_SIZE_256x512 2u
#define G2D_BG_SIZE_512x256 3u
#define G2D_BG_SIZE_512x512 4u
#define G2D_BG_SIZE_NONE    5u

/*
 * G2D_BG_SIZE_FLAT is not a hardware code: it selects the row-major
 * reading of an NSCR's entry array at the resource's own declared
 * dimensions.  It is what the asset lane's renderer does, and it is the
 * only reading available for the many NSCR resources whose declared size
 * is not a hardware screen size at all (a/1/4/3#22 is 512x192).  See the
 * docs/engine-2d.md's discrepancy note.
 */
#define G2D_BG_SIZE_FLAT    0x100u

u32 g2d_bg_screen_index(int x, int y, u32 size_code);

/* Map dimensions in tiles for a size code; 0 for an unknown code. */
void g2d_bg_map_tiles(u32 size_code, u32 *tiles_wide, u32 *tiles_high);

/* ------------------------------------------------------------------ */
/* BG layers and the frame compositor                                  */
/* ------------------------------------------------------------------ */

/*
 * The two readings of a 512-pixel-wide or 512-pixel-tall screen, behind a
 * flag rather than behind a guess.  G2D_BG_ORDER_VRAM is what
 * src/matched/func_02040EE0.c attests and is the default (the value zero,
 * so a zeroed layer gets it): the second 32x32 map follows the first for
 * 512x256, and four are laid out in a 2x2 grid for 512x512.
 * G2D_BG_ORDER_ROW_MAJOR reads the same entry array row-major at the map's
 * own tile width, which is what aletheia's background.py does for every
 * NSCR on disk.  The flag is ignored for G2D_BG_SIZE_FLAT, which is
 * row-major by construction, and the two readings coincide for every size
 * except 512x256 and 512x512.  Which one an NSCR file on disk is actually
 * in is not settled by the evidence this repository has; see docs/engine-2d.md.
 */
#define G2D_BG_ORDER_VRAM      0u
#define G2D_BG_ORDER_ROW_MAJOR 1u

typedef struct G2dBgLayer {
    const G2dNscr    *nscr;
    const G2dNcgr    *ncgr;
    const G2dPalette *pal;
    u32 screen_size;   /* a G2D_BG_SIZE_* code, or G2D_BG_SIZE_FLAT */
    u32 screen_order;  /* G2D_BG_ORDER_*; zero is the attested VRAM order */
    u32 priority;      /* BGxCNT bits 0-1; lower draws in front      */
    s32 hofs, vofs;    /* BGxHOFS / BGxVOFS, in pixels               */
    int wrap;          /* a hardware text layer always wraps its map */
    int enabled;
} G2dBgLayer;

/* ------------------------------------------------------------------ */
/* Affine (rotation/scaling) BG layers                                 */
/* ------------------------------------------------------------------ */

/*
 * A rotation/scaling BG, the shape BG2 and BG3 take when
 * src/matched/func_0203FCA0.c's layer type is 1 or 2 rather than 0.  The
 * differences from a text layer are all consequences of one thing: an
 * affine layer's map entry is a single byte.
 *
 *   - one byte per tile, so 256 tiles addressable, no flip bits and no
 *     palette row.  src/matched/func_02042E34.c is the game's own 8bpp
 *     screen fill and writes exactly one byte per entry, against
 *     func_02042ED8.c's u16 per entry for a text screen.
 *   - the character data is 8bpp, 64 bytes per tile, indexing the whole
 *     256-colour palette.
 *   - the map is a plain tiles_wide * tiles_high array in map order.  The
 *     512-wide text screen's block layout has no affine counterpart,
 *     which is the other half of why func_02040EE0's size code 0 (the
 *     16x16-tile, 128x128-pixel map) is a simple x + y * 16.
 *   - four square sizes only: 128, 256, 512 and 1024 pixels.
 *   - BGxCNT bit 13 is the wraparound flag rather than the extended
 *     palette flag (func_0203FCA0 takes that bit from a different
 *     template field for an affine layer than for a text one, which is
 *     how the two shapes are told apart there).
 *
 * The transform is the same 8.8 matrix the OBJ engine uses, plus a 20.8
 * reference point: the hardware's BGxPA..BGxPD and BGxX/BGxY.  For screen
 * pixel (x, y) the sampled texture coordinate is
 *
 *     tx = (ref_x + PA * x + PB * y) >> 8
 *     ty = (ref_y + PC * x + PD * y) >> 8
 *
 * with the fraction discarded, exactly as for an OBJ.  BGxX and BGxY are
 * 28-bit signed on hardware and are sign-extended from 28 bits here.
 */

#define G2D_BG_AFF_SIZE_128  0u
#define G2D_BG_AFF_SIZE_256  1u
#define G2D_BG_AFF_SIZE_512  2u
#define G2D_BG_AFF_SIZE_1024 3u

/* Map side in tiles for an affine size code: 16, 32, 64, 128; 0 if bad. */
u32 g2d_bg_affine_tiles(u32 size_code);

typedef struct G2dBgAffineLayer {
    const u8         *map;       /* borrowed, one byte per tile           */
    u32               map_size;  /* bytes available at `map`              */
    const G2dNcgr    *ncgr;      /* 8bpp character data                   */
    const G2dPalette *pal;
    u32 screen_size;   /* a G2D_BG_AFF_SIZE_* code                        */
    u32 bg_index;      /* 2 or 3: which hardware layer this is            */
    u32 priority;      /* BGxCNT bits 0-1                                 */
    int wrap;          /* BGxCNT bit 13 for an affine layer               */
    s32 ref_x, ref_y;  /* BGxX / BGxY, 20.8                               */
    G2dAffine mtx;     /* BGxPA .. BGxPD, 8.8                             */
    int enabled;
} G2dBgAffineLayer;

/*
 * Sample one affine layer at screen pixel (sx, sy).  Returns 1 and writes
 * the palette index (which is also the colour-table index: an affine map
 * entry carries no palette row) when the layer covers that pixel, 0 when
 * the transformed coordinate falls off a non-wrapping map or past the end
 * of the map array.
 */
int g2d_bg_affine_sample(const G2dBgAffineLayer *layer, s32 sx, s32 sy,
                         u8 *out_index, u32 *out_color_index);

/* One affine layer on its own into a width*height RGBA buffer, the same
 * convention as g2d_bg_render: uncovered pixels black, index 0 painted. */
void g2d_bg_affine_render(const G2dBgAffineLayer *layer, u32 width, u32 height,
                          u8 *rgba);

/*
 * Sample one layer at screen pixel (sx, sy), after scrolling.  Returns 1
 * and writes the palette index and the colour-table index it resolves to
 * when the layer covers that pixel, 0 when it does not (off a non-wrapping
 * map, or past the end of the entry array).  Whether index 0 counts as
 * transparent is the caller's decision: on hardware it is, and
 * g2d_frame_compose treats it so; the asset lane's single-layer renderer
 * paints it as its palette colour instead, and g2d_bg_render matches that.
 */
int g2d_bg_sample(const G2dBgLayer *layer, s32 sx, s32 sy,
                  u8 *out_index, u32 *out_color_index);

/*
 * Render one layer on its own into a width*height RGBA buffer
 * (caller-allocated, width*height*4 bytes), every pixel opaque.  A pixel
 * the layer does not cover is black; a colour index past the end of the
 * palette is magenta; palette index 0 is painted, not skipped.  With
 * screen_size G2D_BG_SIZE_FLAT, no scroll and the NSCR's own dimensions
 * this reproduces aletheia's background.py compose() exactly, which is
 * what tests/test_bg.c checks against the oracle.
 */
void g2d_bg_render(const G2dBgLayer *layer, u32 width, u32 height, u8 *rgba);

/*
 * One OBJ entry for the frame compositor: a composed pose (as
 * g2d_compose_pose produces), its top-left position on screen, the
 * palette its indices resolve through, and its OBJ priority.
 */
typedef struct G2dObjEntry {
    const G2dPose    *pose;
    const G2dPalette *pal;
    s32 x, y;
    u32 priority;      /* attr2 bits 10-11 */
    int semi;          /* attr0 bits 10-11 == 1: a semi-transparent OBJ   */
} G2dObjEntry;

#define G2D_SCREEN_WIDTH  256u
#define G2D_SCREEN_HEIGHT 192u

/*
 * Layer up to four BGs and an OBJ list into one RGBA frame.
 *
 * Painter's order, back to front: the backdrop (colour 0 of
 * `backdrop_pal`, the BG palette's first entry), then priority 3 down to
 * priority 0.  Within one priority the BG layers are drawn in descending
 * array order, so bgs[0] (BG0) ends in front of bgs[3] (BG3); the OBJ
 * entries of that priority are drawn after the BGs, so an OBJ wins a tie
 * against a BG.  The OBJ list itself is drawn in reverse order, keeping
 * the OBJ half's convention that the earliest-emitted entry occupies the
 * lowest hardware OAM slot and a lower slot draws in front.
 *
 * Palette index 0 is transparent for both a BG tile and an OBJ pixel.
 * `rgba` is caller-allocated, width*height*4 bytes; every pixel is
 * opaque.
 */
void g2d_frame_compose(const G2dBgLayer *bgs, u32 n_bgs,
                       const G2dObjEntry *objs, u32 n_objs,
                       const G2dPalette *backdrop_pal,
                       u32 width, u32 height, u8 *rgba);

/*
 * The same frame, described rather than passed as loose arrays, so the
 * affine BGs (and, later, the windows and the colour special effects)
 * have somewhere to go without changing every call site.
 *
 * Front-to-back order within one priority: the OBJ entries in forward
 * list order (the earliest-emitted entry holds the lowest hardware OAM
 * slot and draws in front), then the BG layers by ascending layer number,
 * a text layer's number being its index in `bgs` and an affine layer's
 * being its own `bg_index`.  An OBJ therefore wins a priority tie against
 * any BG, which is what g2d_frame_compose already did.
 */
/* ------------------------------------------------------------------ */
/* Windows and the colour special effects                              */
/* ------------------------------------------------------------------ */

/*
 * The layer bits, in the hardware's order, shared by three registers:
 * WININ/WINOUT use bits 0-5 to say which layers a region shows and bit 5
 * to say whether the colour effect runs there; BLDCNT uses the same bits
 * 0-5 for the first target and bits 8-13 for the second, with bit 5
 * meaning the backdrop.  Two names for bit 5 because the two registers
 * spend it differently.
 */
#define G2D_LAYER_BG0    0x01u
#define G2D_LAYER_BG1    0x02u
#define G2D_LAYER_BG2    0x04u
#define G2D_LAYER_BG3    0x08u
#define G2D_LAYER_OBJ    0x10u
#define G2D_LAYER_BD     0x20u   /* BLDCNT: the backdrop                */
#define G2D_LAYER_EFFECT 0x20u   /* WININ/WINOUT: run the colour effect */
#define G2D_LAYER_ALL    0x3Fu

/*
 * One rectangular window: WIN0H/WIN0V or WIN1H/WIN1V, plus its half of
 * WININ.  The region is [x1, x2) by [y1, y2).  The hardware reads a right
 * or bottom edge that is past the screen, or before its own left or top
 * edge, as the screen edge, and so does this: an x2 greater than
 * G2D_SCREEN_WIDTH or less than x1 means "to the right edge", and the
 * same for y2 against G2D_SCREEN_HEIGHT.
 */
typedef struct G2dWindow {
    int enabled;
    u32 x1, x2, y1, y2;
    u32 layers;        /* the WININ half: layer bits, plus the effect bit */
} G2dWindow;

/*
 * The window system.  Priority, highest first: WIN0, WIN1, the OBJ
 * window, then outside every window.  With no window enabled at all,
 * every layer shows everywhere and the colour effect runs everywhere,
 * which is what the hardware does when DISPCNT's window bits are clear.
 *
 * The OBJ window is a region rather than a rectangle: on hardware it is
 * the union of the pixels of the OBJs whose OAM mode selects it, which is
 * the OBJ engine's business rather than the compositor's.  The caller
 * supplies it as a width*height mask, non-zero inside.
 */
typedef struct G2dWindows {
    G2dWindow win0, win1;
    int       obj_enabled;
    u32       obj_layers;      /* WINOUT bits 8-13 */
    u32       outside_layers;  /* WINOUT bits 0-5  */
    const u8 *obj_mask;        /* width*height, or NULL */
    u32       mask_width, mask_height;
} G2dWindows;

/*
 * The layer bits in force at (x, y): which layers may contribute and
 * whether the colour effect runs.  `windows` may be NULL, which is
 * G2D_LAYER_ALL everywhere.
 */
u32 g2d_window_mask(const G2dWindows *windows, s32 x, s32 y);

/* BLDCNT bits 6-7. */
#define G2D_BLEND_NONE        0u
#define G2D_BLEND_ALPHA       1u
#define G2D_BLEND_BRIGHT_UP   2u
#define G2D_BLEND_BRIGHT_DOWN 3u

/*
 * BLDCNT, BLDALPHA and BLDY together.  The coefficients are the
 * hardware's sixteenths: EVA and EVB are 0..16 (a register value above 16
 * reads as 16) and so is EVY.
 *
 * The arithmetic is done in the 5 bits per channel the DS actually has,
 * not in the 8-bit expansion the palette is stored as: a colour is folded
 * back to 5 bits, blended, clamped to 31, and expanded again.  Anything
 * else would round differently from the hardware on every blended pixel.
 */
typedef struct G2dBlend {
    u32 effect;      /* a G2D_BLEND_* code   */
    u32 first;       /* BLDCNT bits 0-5      */
    u32 second;      /* BLDCNT bits 8-13     */
    u32 eva, evb;    /* BLDALPHA, sixteenths */
    u32 evy;         /* BLDY, sixteenths     */
} G2dBlend;

/* The three effects on 8-bit channels, through the 5-bit fold. */
void g2d_blend_alpha(const u8 first[3], const u8 second[3],
                     u32 eva, u32 evb, u8 out[3]);
void g2d_blend_brightness(const u8 first[3], u32 evy, int up, u8 out[3]);

/*
 * The display's own precision.  The 2D engines produce 15-bit colour, the
 * 3D engine 18-bit, and everything after the layer mix (the 3D layer's
 * alpha blend, the master brightness, the LCD) runs on 6 bits per channel.
 *
 *   g2d_c5to6    a 2D (palette) channel into the 6-bit stage: 0 stays 0,
 *                anything else is 2c + 1, so 31 is 63 and full white stays
 *                full white.  [AP] that the low bit is set for a non-zero
 *                channel is the convention this file picks: public
 *                documentation says the 15-bit colour is widened to 18
 *                bits, not how the new bit is filled.
 *   g2d_c8to6    an 8-bit host channel folded to 6 bits, round to nearest
 *                (exact for every c6 * 255 / 63 expansion, which is what
 *                the 3D rasteriser's blends produce).
 *   g2d_c6to8    a 6-bit channel expanded for the host, c * 255 / 63.
 *
 * g2d_blend_3d is the 3D layer's own alpha blend with the 2D pixel
 * directly beneath it: (3D * (A + 1) + 2D * (31 - A)) / 32 per channel, A
 * the 3D pixel's 5-bit alpha, EVA and EVB unused.  `top` is the 3D
 * pixel's 8-bit colour, `below` a palette colour's 8-bit expansion; the
 * result is 6-bit.
 *
 * g2d_master_bright6 is MASTER_BRIGHT on one 6-bit pixel: up is
 * c + (63 - c) * f / 16, down c - c * f / 16, f the factor with 17..31
 * reading as 16, in place.
 */
u32  g2d_c5to6(u32 c5);
u32  g2d_c8to6(u8 c8);
u8   g2d_c6to8(u32 c6);
void g2d_blend_3d(const u8 top[3], const u8 below[3], u32 alpha5, u8 out6[3]);
void g2d_master_bright6(u8 c6[3], u32 factor, int up);

/*
 * A 3D layer as one stackable layer.  This is the piece g3d_compose_over
 * does not close: that helper only puts a 3D frame behind a whole 2D frame
 * at the backdrop, whereas here the 3D output is an ordinary layer at its
 * own priority and hardware layer number, so a 2D BG can sit in front of or
 * behind it and the colour effects can blend with a 3D pixel.  g2d does not
 * depend on g3d for this: the layer is a raw RGBA buffer in the geometry
 * g3d writes, and whatever produced it is the caller's business.
 *
 * The hardware 3D layer is a full framebuffer, not a sprite, and `bg_index`
 * is the layer it occupies: the caller must not also supply a text or
 * affine BG at that index (the 3D layer owns that slot in g2d_frame_stack).
 * A pixel outside the buffer, a pixel whose alpha is 0, or a layer the
 * window hides, contributes nothing, and the layers beneath show through,
 * as they do on hardware where the rendered frame's alpha 0 (nothing drawn
 * over a clear colour of alpha 0) is transparent to the 2D engine.
 *
 * Any other alpha is the 3D pixel's 5-bit alpha (the 8-bit value folded,
 * so 255 is 31), and it is what the layer blends with: wherever the pixel
 * directly beneath a 3D pixel is a BLDCNT second target, the two are mixed
 * by g2d_blend_3d with the 3D alpha, whatever BLDCNT's effect and first
 * target bits say (EVA and EVB are not used; this is the same exception
 * the semi-transparent OBJ makes).  [AP] that BLDCNT's first-target bit
 * for BG0 is not required: public documentation describes the 3D blend as
 * using the per-pixel alpha in place of the coefficients and does not
 * settle that bit; an opaque 3D pixel (alpha 31) comes out unchanged by
 * the blend either way.  Where the pixel beneath is not a second target,
 * the ordinary rules apply to the 3D pixel as a BG0 pixel (brightness).
 *
 * `hofs` is BG0HOFS, the 3D layer's one scroll: screen x shows buffer
 * column (x + hofs) mod 512, and a column past the buffer is transparent.
 * [AP] the 512-pixel period with its second half empty is how this file
 * reads the register's 9 bits; there is no vertical scroll.
 */
typedef struct G2dLayer3d {
    const u8 *rgba;    /* width*height*4, R,G,B,A; A == 0 is transparent */
    u32 width, height;
    u32 priority;      /* 0..3, like a BG; lower draws in front */
    u32 bg_index;      /* 0..3: the hardware layer number it occupies */
    int enabled;
    s32 hofs;          /* BG0HOFS, 0..511 */
    /*
     * 0 or 1: rgba is width*height pixels.  N > 1: rgba holds
     * (width*N) x (height*N) pixels, N x N per DS pixel, and is only
     * accepted by g2d_frame_compose_desc_scaled with the same scale.
     * width, height, hofs and the window stay in DS pixels.
     */
    u32 scale;
} G2dLayer3d;

/*
 * A pre-resolved layer: one RGBA buffer standing for one hardware BG at its
 * own priority and layer number, where alpha 0 is transparent and anything
 * else is an opaque pixel of that colour.  This is how a layer shape the
 * text and affine samplers above do not decode (an extended rotation/scaling
 * BG with 16-bit map entries, a 256-colour or direct-colour bitmap BG, a
 * text BG through an extended palette) still takes part in the per-pixel
 * stack: engine/g2d/hwcompose.c resolves it from the hardware's own bytes
 * and hands it over here, and from then on it is an ordinary BG for the
 * priority walk, the windows and the colour effects.  Like the 3D layer it
 * takes its layer number's slot: no text or affine layer at that index is
 * consulted while it is enabled.
 */
typedef struct G2dLayerRaster {
    const u8 *rgba;    /* width*height*4, R,G,B,A; A == 0 is transparent */
    u32 width, height;
    u32 priority;      /* 0..3 */
    u32 bg_index;      /* 0..3 */
    int enabled;
} G2dLayerRaster;

typedef struct G2dFrameDesc {
    const G2dBgLayer       *bgs;
    u32                     n_bgs;
    const G2dBgAffineLayer *abgs;
    u32                     n_abgs;
    const G2dLayer3d       *g3d;       /* NULL: no 3D layer       */
    const G2dObjEntry      *objs;
    u32                     n_objs;
    const G2dPalette       *backdrop_pal;
    const G2dWindows       *windows;   /* NULL: no windowing      */
    const G2dBlend         *blend;     /* NULL: no colour effect  */
    const G2dLayerRaster   *rasters;   /* pre-resolved BG layers  */
    u32                     n_rasters;
    /*
     * Optional, width*height*3: every output pixel's colour in the 6-bit
     * stage that follows the layer mix (g2d_c5to6 of a 2D result, the 3D
     * blend's own 6-bit result, the 3D pixel folded by g2d_c8to6).  What
     * the master brightness and the display capture work on; NULL when
     * the caller does not need it.
     */
    u8                     *out6;
    /*
     * Optional: set non-zero when a 2D pixel is alpha blended over a 3D
     * pixel (the 3D layer is the second target beneath it).  A caller that
     * would compose the screen again to find such pixels skips that when
     * none was (demo/eidos_boot.c, boot_gpu3d).
     */
    int                    *blend_over_3d;
} G2dFrameDesc;

void g2d_frame_compose_desc(const G2dFrameDesc *desc, u32 width, u32 height,
                            u8 *rgba);

/*
 * The same composition into a (width*scale) x (height*scale) buffer
 * (desc->out6, when set, is that size too).  Every 2D layer, the windows
 * and the blend registers work in DS pixels, so each DS pixel's 2D result
 * is one scale x scale block; the 3D layer (G2dLayer3d.scale == scale)
 * contributes its own pixel per output pixel and the priority walk, the 3D
 * blend and the colour effects run on those.  scale 0 or 1 is
 * g2d_frame_compose_desc.
 */
void g2d_frame_compose_desc_scaled(const G2dFrameDesc *desc, u32 width,
                                   u32 height, u32 scale, u8 *rgba);

/*
 * One opaque contributor at one pixel.  `layer` is the OBJ entry's index
 * for an OBJ and the BG layer number for a BG, which is what the colour
 * special effects' target masks are keyed on.
 */
typedef struct G2dFrameSample {
    const G2dPalette *pal;
    u32 color_index;
    int is_obj;
    int is_3d;          /* rgb[] holds the pixel; pal/color_index unused */
    int is_raster;      /* likewise, out of a G2dLayerRaster            */
    u8  rgb[3];
    u8  alpha5;         /* a 3D sample's alpha, 1..31                   */
    u32 layer;
} G2dFrameSample;

/*
 * Every opaque contributor at pixel (x, y), front to back, at most `max`
 * of them; returns how many were written.  Entry 0 is the topmost pixel
 * and entry 1 the one directly beneath it.  The backdrop is not in the
 * list: an empty list means the backdrop shows.
 *
 * A layer the window system hides at this pixel contributes nothing, so
 * what comes back is what the screen shows, which is also what the colour
 * special effects have to be decided on: the DS blends only where the
 * first-target pixel is the topmost one and the second-target pixel is
 * the one directly beneath it.
 */
u32 g2d_frame_stack(const G2dFrameDesc *desc, s32 x, s32 y,
                    G2dFrameSample *out, u32 max);

/* The G2D_LAYER_* bit one sample belongs to. */
u32 g2d_frame_sample_bit(const G2dFrameSample *sample);

/* ------------------------------------------------------------------ */
/* Both screens from the hardware state (engine/g2d/hwcompose.c)       */
/* ------------------------------------------------------------------ */

/*
 * Everything above draws from parsed asset files.  Once the game's own code
 * runs, the picture is not described by files any more but by what that
 * code wrote into the DS's 2D hardware: the register file, the palette RAM,
 * OAM and VRAM through the bank mapping.  hwcompose.c reads that state and
 * builds the same layer descriptions the asset path builds (G2dBgLayer,
 * G2dBgAffineLayer, G2dObjEntry, G2dWindows, G2dBlend, G2dLayer3d), then
 * runs the same per-pixel compositor, so the two paths cannot disagree on
 * any rule they share.  What the asset layers cannot express is resolved
 * into a G2dLayerRaster from the hardware's own bytes.
 *
 * The state is passed as plain host pointers rather than read through
 * sdk/dsmem.h directly, so the renderer is a pure function of bytes and can
 * be driven by a test with no address space behind it.
 * g2d_hw_state_from_dsmem() fills one from the live address space.
 *
 * VRAM is passed as 16 KB pages of each engine's BG and OBJ windows, which
 * is the granularity of every VRAM bank mapping (bank I is 16 KB, F and G
 * are 16 KB and map at 16 KB offsets).  A NULL page reads as zero, which is
 * what unmapped VRAM reads as on hardware.
 */
#define G2D_HW_MAIN       0
#define G2D_HW_SUB        1
#define G2D_HW_PAGE_SIZE  0x4000u
#define G2D_HW_BG_PAGES   32u     /* main BG window, 512 KB; sub uses 8  */
#define G2D_HW_OBJ_PAGES  16u     /* main OBJ window, 256 KB; sub uses 8 */
#define G2D_HW_EXTPAL_SLOT_SIZE 0x2000u   /* 16 x 256 colours, BGR555    */

typedef struct G2dHwState {
    const u8 *io;    /* the register file from 0x04000000, >= 0x1070 bytes */
    const u8 *pal;   /* palette RAM from 0x05000000, 2 KB                 */
    const u8 *oam;   /* OAM from 0x07000000, 2 KB                         */
    const u8 *bg_page[2][G2D_HW_BG_PAGES];    /* 0x06000000 / 0x06200000 */
    const u8 *obj_page[2][G2D_HW_OBJ_PAGES];  /* 0x06400000 / 0x06600000 */
    const u8 *bg_extpal[2][4];   /* the four BG extended palette slots   */
    const u8 *obj_extpal[2];     /* the OBJ extended palette             */
    const u8 *lcdc_bank[4];      /* banks A..D, for display mode 2       */
    /*
     * Banks A..D again, writable, for the display capture: a bank's entry
     * is non-NULL only while VRAMCNT has it enabled in LCDC mode (MST 0),
     * which is the one mapping the capture unit writes to (and reads its
     * VRAM source B from).  All NULL: the capture does nothing.
     */
    u8       *capture_bank[4];
} G2dHwState;

/*
 * What a frame used that this renderer does not draw, per engine, so a
 * caller can say so rather than show a silently wrong picture.  The
 * affected layer or OBJ is left out; everything else is drawn.
 */
#define G2D_HW_UNSUP_BG_MODE      0x01u  /* BG mode 6 (large screen), 7   */
#define G2D_HW_UNSUP_DISPLAY_MODE 0x02u  /* main memory display, or a     */
                                         /* VRAM display on the sub engine */
#define G2D_HW_UNSUP_BITMAP_OBJ   0x04u  /* OBJ mode 3                    */
#define G2D_HW_UNSUP_MOSAIC       0x08u  /* the mosaic bit on the 3D BG0  */
#define G2D_HW_UNSUP_NO_3D        0x10u  /* BG0 is 3D but no 3D frame     */
#define G2D_HW_UNSUP_CAPTURE_FIFO 0x20u  /* capture source B = main       */
                                         /* memory display FIFO (read as  */
                                         /* transparent black)            */

/*
 * DISPCAPCNT (0x04000064), the display capture: while bit 31 is set the
 * frame g2d_hw_render draws is also captured, as on hardware where the
 * capture runs through the frame after the bit is set and the bit clears
 * when it completes.  g2d_hw_render does the capture (into
 * G2dHwState.capture_bank); clearing bit 31 is the register file's owner's
 * business: g2d_hw_render_dsmem clears it in the live register file.
 *
 *   source A    bit 24: 0 the main engine's graphics screen (BG, OBJ and 3D
 *               after the colour effects, before the master brightness;
 *               composed even when the display mode shows something else),
 *               alpha 1 everywhere; 1 the 3D frame alone, alpha = its
 *               alpha != 0.
 *   source B    bit 25: 0 VRAM, the bank DISPCNT bits 18-19 select, from
 *               bits 26-27 * 32 KiB, alpha = bit 15; 1 the main memory
 *               display FIFO, which no DMA feeds here (flagged).
 *   mode        bits 29-30: 0 A, 1 B, 2 and 3 A and B blended:
 *               (A * Aa * EVA + B * Ba * EVB) / 16 per 5-bit channel,
 *               clamped to 31, alpha (Aa and EVA > 0) or (Ba and EVB > 0);
 *               EVA bits 0-4 and EVB bits 8-12, above 16 read as 16.
 *   destination bits 16-17 the bank (A..D), bits 18-19 * 32 KiB the offset,
 *               bits 20-21 the size (128x128, 256x64, 256x128, 256x192);
 *               written as BGR555 with the alpha in bit 15, the address
 *               wrapping inside the bank's 128 KiB.
 *
 * [AP] source B is read as the same linear pixel stream the destination
 * is written as (pixel i of the capture at read offset + 2i); for the
 * 256-wide sizes every reading agrees, for 128x128 public documentation
 * does not settle the stride.  A graphics-screen pixel enters the capture
 * as its 6-bit stage value halved (g2d_c5to6 inverted exactly for 2D
 * colours), a 3D pixel as its 8-bit value folded to 6 bits and halved.
 */
#define G2D_HW_DISPCAPCNT 0x04000064u

/*
 * One engine's screen into a 256x192 RGBA buffer (R, G, B, A, every pixel
 * opaque: what eidos_platform_present uploads).  `rgba3d` is the 3D
 * engine's 256x192 output for the main engine's BG0 in 3D mode, or NULL;
 * it is ignored for the sub engine.  `unsupported` may be NULL.  Returns 0,
 * or -1 when a working buffer could not be allocated.
 */
int g2d_hw_render_engine(const G2dHwState *hw, int engine, const u8 *rgba3d,
                         u8 *rgba, u32 *unsupported);

/*
 * Both screens.  POWCNT1 bit 15 decides which engine is on which screen,
 * as on hardware: set puts the main engine on the top screen.
 * `unsupported`, when not NULL, receives the main engine's flags in [0]
 * and the sub engine's in [1].
 */
int g2d_hw_render(const G2dHwState *hw, const u8 *rgba3d,
                  u8 *top, u8 *bottom, u32 unsupported[2]);

/* The live DS address space (sdk/dsmem.h) as a G2dHwState. */
void g2d_hw_state_from_dsmem(G2dHwState *hw);

/* g2d_hw_render over g2d_hw_state_from_dsmem. */
int g2d_hw_render_dsmem(const u8 *rgba3d, u8 *top, u8 *bottom,
                        u32 unsupported[2]);

/*
 * Internal resolution: see the contract at the top of hwcompose.c.  `scale`
 * N >= 1 (0 means 1); top and bottom hold 256N x 192N RGBA pixels and
 * rgba3d, when not NULL, is the 3D frame rendered at the same N.
 */
void g2d_hw_box_downsample(const u8 *src, u32 scale, u8 *dst);
int g2d_hw_render_scaled(const G2dHwState *hw, u32 scale, const u8 *rgba3d,
                         u8 *top, u8 *bottom, u32 unsupported[2]);
int g2d_hw_render_dsmem_scaled(u32 scale, const u8 *rgba3d, u8 *top,
                               u8 *bottom, u32 unsupported[2]);
/* Each screen at the multiple it needs: only the main engine with the 3D
 * layer is rendered at `scale`; the other screen is 256x192 (the presenter
 * scales it).  screen_scale[0..1] (top, bottom) receive the multiple each
 * buffer was written at; both buffers are sized for `scale`. */
int g2d_hw_render_dsmem_adaptive(u32 scale, const u8 *rgba3d, u8 *top,
                                 u8 *bottom, u32 unsupported[2],
                                 u32 screen_scale[2]);
/* As above; with `behind` (256x192 RGBA) the main engine is also composed with
 * its 3D layer off when a GPU 3D layer needs it, on a worker thread beside the
 * other engines.  *behind_done says whether it was. */
/* Fill the compositors' lookup tables.  Call before the first compose and
 * before any compose runs on another thread (g2d_hw_render does). */
void g2d_tables_init(void);

/* The compositors' worker pool (engine/g2d/par.c): runs fn(ctx, 0..n-1), the
 * caller taking task 0 and helping with the rest; serial when the machine has
 * one hardware thread.  Safe to call from several threads at once. */
void g2d_par_run(unsigned n, void (*fn)(void *ctx, unsigned index), void *ctx);
unsigned g2d_par_workers(void);

int g2d_hw_render_dsmem_adaptive_ex(u32 scale, const u8 *rgba3d, u8 *top,
                                    u8 *bottom, u32 unsupported[2],
                                    u32 screen_scale[2], u8 *behind,
                                    int *behind_done);

/* The main engine's graphics screen (the capture's source A) at DS size,
 * RGBA8, whatever the display mode; rgba3d NULL turns the 3D layer off. */
int g2d_hw_main_graphics_dsmem(const u8 *rgba3d, u8 *out);

/* Whether the main engine's last compose alpha blended a 2D pixel over
 * the 3D layer (G2dFrameDesc.blend_over_3d): when it did not, a compose of
 * the same screen over another 3D layer has the same 2D everywhere. */
int g2d_hw_main_blended_over_3d(void);
/* The two at once (with rgba3d, and with the 3D off), on the worker pool. */
int g2d_hw_main_graphics_pair_dsmem(const u8 *rgba3d, u8 *with3d, u8 *without);

/* The OBJs of one screen (0 top, 1 bottom) alone at DS size, RGBA8: alpha
 * 255 where an OBJ pixel shows, 0 elsewhere.  The game's sprites without
 * the BGs, the 3D, the colour effects or the master brightness. */
int g2d_hw_objs_dsmem(int screen, u8 *out);

/* ------------------------------------------------------------------ */
/* Rig blocks (a/0/0/4 and its siblings)                               */
/* ------------------------------------------------------------------ */

typedef struct G2dRig {
    G2dNcgr    ncgr;
    G2dNcer    ncer;
    G2dNanr    nanr;
    G2dNmcr    nmcr;
    G2dPalette palette;
    int        has_nanr;
    u8       **owned;     /* the decompressed member buffers */
    u32        owned_count;
} G2dRig;

/*
 * Load one rig bundle out of an opened archive: every sub-file of the
 * block is classified by its own post-decompression magic (the largest
 * RGCN member is the main sheet, the first RECN/RCMN/RNAN member is the
 * cell bank / node table / animation, every RLCN is concatenated into
 * one palette-bank table).  A member whose compression stream is corrupt
 * is skipped, not fatal: the rig archive carries unused duplicates whose
 * streams do not decode and which no pose needs.
 */
int  g2d_rig_load(G2dRig *rig, BwArcFile *arc, u32 block_start, u32 block_size);
/* Append the palette member `index` (an RLCN) to a loaded rig: a block
 * loaded by parts (one of a species' two sprite sets) and its palette
 * apart.  Returns 0 on success. */
int  g2d_rig_load_palette(G2dRig *rig, BwArcFile *arc, u32 index);
void g2d_rig_free(G2dRig *rig);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_G2D_H */
