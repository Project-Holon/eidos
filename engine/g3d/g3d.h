/*
 * engine/g3d/g3d.h -- the DS 3D (geometry engine + rasteriser) shim.
 *
 * eidos's third host-side reimplementation of hardware the DS had and a
 * PC does not, after engine/g2d/ (the 2D engine) and engine/audio/ (the
 * ARM7 sound core).  Pure C99, no SDL, no third-party code, standard
 * library only (and not even math.h: every number below is integer
 * arithmetic on the hardware's own 20.12 fixed point).
 *
 * Where the behaviour comes from, in order of authority, and the file
 * headers under engine/g3d/ repeat this per rule:
 *
 *   1. aletheia's asset lane.  tools/assets/nsbmd.py cracked the NSBMD /
 *      NSBTX container, the Nitro resource dictionary (the patricia node
 *      array, the {entrySize, namesOffset} sub-header, the trailing
 *      16-byte name blob), the TEX0 texture record (a u16 offset in
 *      8-byte units plus the upper half of TEXIMAGE_PARAM), the palette
 *      record, and the MDL0 shape dictionary's two-base descriptor
 *      layout; tools/assets/map_shapes.py cracked the render-command
 *      bytecode (BINDMATERIAL 4 / DRAWSHAPE 5) and the material-name to
 *      texture-name link.  aletheia's docs/ASSETS.md records each of
 *      those and the gaps beside them.  This engine parses the same
 *      structures in C and is tested against that lane's own OBJ export.
 *      The MDL0 node dictionary, its rotation/scale components, the
 *      node hierarchy and the shape -> node binding are this engine's own
 *      decode, from the ROM's bytes: neither aletheia's tools nor
 *      aletheia's docs/ASSETS.md decode nodes, so that decode is recorded in
 *      docs/g3d-nodes.md with its evidence and is validated by
 *      self-consistency (orthonormal rotations, acyclic in-range parents,
 *      in-range draws), not by an oracle comparison.
 *   2. aletheia's matched sources.  src/matched/unit_02048AD0.c and
 *      unit_02048BC4.c are g3d_light.c's accessor run and light-set
 *      builder and fix the shape of the game's light object (three
 *      VecFx32 at +0x20, +0x2C, +0x38 over a 0x1C-byte head); they are
 *      what says the game's 3D vectors are VecFx32, i.e. three fx32 in
 *      the 1.19.12 format sdk/types.h defines.  No matched source in
 *      aletheia today contains the geometry engine's own arithmetic.
 *   3. public NDS 3D hardware documentation: the geometry command set
 *      and its parameter counts, the matrix modes and stack depths, the
 *      vertex command encodings, TEXIMAGE_PARAM's bit layout, the
 *      texture pixel formats, and POLYGON_ATTR's fields.
 *
 * Nothing here is derived from any other decompilation, port or emulator.
 *
 * WHAT IS ATTESTED AND WHAT IS NOT.  The parsing and the vertex pipeline
 * are checked against an oracle: tests/test_g3d.c transforms every vertex
 * of two real map models through this engine with an identity matrix
 * stack and compares against the OBJ aletheia's own tools write.  The
 * *rasteriser* has no oracle: there is no DS frame in this repository to
 * compare against, and nothing here claims pixel-exactness with the
 * hardware.  Every rule it follows is either documented hardware
 * behaviour or is marked an approximation in raster.c's header and in
 * docs/engine-3d.md's engine/g3d section.
 */

#ifndef EIDOS_ENGINE_G3D_H
#define EIDOS_ENGINE_G3D_H

#include <stddef.h>

#include "port.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Fixed point and matrices                                            */
/* ------------------------------------------------------------------ */

/*
 * The geometry engine's number is fx32: 1.19.12, sdk/types.h's own
 * typedef, and the same one src/matched/unit_02048AD0.c's VecFx32 is
 * three of.  FX_MUL keeps a 64-bit intermediate and shifts right by 12
 * *arithmetically*, so a negative product truncates towards minus
 * infinity, which is what an ARM `smull` + `mov lsr/asr` pair does and
 * what portability rule 3 forbids routing through a float to "simplify".
 *
 * A matrix is row-major and vectors are rows: a point is transformed as
 * v' = v * M, so M's fourth row is the translation.  That is the DS's
 * own convention, and it is why MTX_MULT_* multiplies the parameter on
 * the *left* of the current matrix (current = param * current): a
 * command issued later ends up applied to the vertex earlier.
 */
typedef struct G3dVec {
    fx32 x, y, z;
} G3dVec;

/* Row-major, m[row * 4 + col].  The 4x3 forms keep the implied fourth
 * column (0, 0, 0, 1) explicit so one type covers both. */
typedef struct G3dMtx {
    fx32 m[16];
} G3dMtx;

void g3d_mtx_identity(G3dMtx *out);
/* out = a * b, in fx32 with 64-bit intermediates. */
void g3d_mtx_mul(G3dMtx *out, const G3dMtx *a, const G3dMtx *b);
/* v' = v * m, with v's implied w == 1.  out_w receives the fourth term. */
void g3d_mtx_xform(const G3dMtx *m, fx32 x, fx32 y, fx32 z,
                   fx32 *ox, fx32 *oy, fx32 *oz, fx32 *ow);

/* The DS's MTX_TRANS / MTX_SCALE as matrices, and the two camera helpers
 * a caller needs that the hardware has SDK functions for rather than
 * commands: a frustum (MTX_FRUSTUM) and a look-at (MTX_LOOKAT). */
void g3d_mtx_translate(G3dMtx *out, fx32 x, fx32 y, fx32 z);
void g3d_mtx_scale(G3dMtx *out, fx32 x, fx32 y, fx32 z);
void g3d_mtx_frustum(G3dMtx *out, fx32 t, fx32 b, fx32 l, fx32 r,
                     fx32 n, fx32 f);
/* fovy is a 16-bit binary angle (65536 == one turn); the sine and cosine
 * come from engine/g2d/affine.c's table, so the 3D engine adds no second
 * trigonometry implementation to the port. */
void g3d_mtx_perspective(G3dMtx *out, u16 fovy, fx32 aspect, fx32 n, fx32 f);
void g3d_mtx_lookat(G3dMtx *out, const G3dVec *eye, const G3dVec *up,
                    const G3dVec *at);

/* ------------------------------------------------------------------ */
/* The matrix stack                                                    */
/* ------------------------------------------------------------------ */

#define G3D_MTX_MODE_PROJECTION  0u
#define G3D_MTX_MODE_POSITION    1u
#define G3D_MTX_MODE_POSVEC      2u
#define G3D_MTX_MODE_TEXTURE     3u

/*
 * The hardware's stacks: one entry for projection, one for texture, 31
 * usable entries for position/vector (the 32nd is the overflow slot,
 * which is why a push at 31 sets the error flag but still writes).  The
 * error flag is sticky and readable, which is how a test can assert that
 * a display list balanced its pushes and pops.
 */
#define G3D_POS_STACK_DEPTH 32u

typedef struct G3dMtxStack {
    u32   mode;
    G3dMtx proj, pos, vec, tex;
    G3dMtx proj_save, tex_save;
    G3dMtx pos_stack[G3D_POS_STACK_DEPTH];
    G3dMtx vec_stack[G3D_POS_STACK_DEPTH];
    u32   sp;        /* position/vector stack pointer */
    u32   error;     /* sticky over/underflow */
    int   clip_dirty;
    u32   view_gen;  /* g3d_mtx_view_scale_generation() when clip was made */
    G3dMtx clip;     /* pos * proj, recomputed lazily */
} G3dMtxStack;

/*
 * The view scale: a runtime widening of the clip matrix.  The clip matrix
 * (position times projection) is what both the vertices and the game's own
 * BOX_TEST / POS_TEST / VEC_TEST culling go through, so scaling its x and y
 * output columns changes what is drawn and what the game decides to draw
 * together.  kx and ky are fx32 factors (FX32_ONE is 1.0, the default and
 * exactly the hardware's behaviour); a factor below one shows more of the
 * world in that direction.  The one-screen layout sets them from the
 * window's shape (engine/platform/geometry.c, eidos_world_geometry).
 * Global to every stack, as the window is; the stacks notice a change
 * through a generation count and rebuild their clip matrix.
 */
void  g3d_mtx_set_view_scale(fx32 kx, fx32 ky);
void  g3d_mtx_view_scale(fx32 *kx, fx32 *ky);
u32   g3d_mtx_view_scale_generation(void);

void g3d_mtx_stack_init(G3dMtxStack *st);
void g3d_mtx_stack_set_mode(G3dMtxStack *st, u32 mode);
void g3d_mtx_stack_push(G3dMtxStack *st);
void g3d_mtx_stack_pop(G3dMtxStack *st, s32 count);
void g3d_mtx_stack_store(G3dMtxStack *st, u32 slot);
void g3d_mtx_stack_restore(G3dMtxStack *st, u32 slot);
void g3d_mtx_stack_load_identity(G3dMtxStack *st);
void g3d_mtx_stack_load(G3dMtxStack *st, const G3dMtx *m);
void g3d_mtx_stack_mult(G3dMtxStack *st, const G3dMtx *m);
const G3dMtx *g3d_mtx_stack_clip(G3dMtxStack *st);

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */

/*
 * GX texture formats, the TEXIMAGE_PARAM format field.  The survey
 * tools/g3d_oracle.py runs over a/0/1/4 (the shared terrain and building
 * texture archive every map model draws from) reports which of them
 * actually occur; see docs/engine-3d.md.  All seven decode here anyway, because
 * a decoder that only handles what one archive happens to use is a trap
 * for the next archive.
 */
#define G3D_TEXFMT_NONE   0u
#define G3D_TEXFMT_A3I5   1u
#define G3D_TEXFMT_PAL4   2u
#define G3D_TEXFMT_PAL16  3u
#define G3D_TEXFMT_PAL256 4u
#define G3D_TEXFMT_COMP   5u   /* 4x4 texel compressed */
#define G3D_TEXFMT_A5I3   6u
#define G3D_TEXFMT_DIRECT 7u

typedef struct G3dTexture {
    u32 width, height;
    u32 format;
    u8 *rgba;    /* owned, width*height*4, R,G,B,A */
} G3dTexture;

void g3d_texture_free(G3dTexture *t);

/* Bytes of texel data one (format, width, height) occupies.  Format 5
 * carries 4 bytes of texel data and 2 bytes of index data per 4x4 block
 * in one region, so it is (w/4)*(h/4)*6 rather than a bits-per-pixel
 * product. */
u32 g3d_texture_byte_size(u32 format, u32 width, u32 height);

/*
 * Decode one texture into RGBA.  `palette` is the raw BGR555 halfwords
 * the texture's own palette record points at and `pal_bytes` its length;
 * a direct-colour texture needs neither.  `color0_transparent` is
 * TEXIMAGE_PARAM bit 29: colour 0 of a paletted format is transparent
 * rather than its palette entry.  Returns 0 on success.
 */
int g3d_texture_decode(G3dTexture *out, u32 format, u32 width, u32 height,
                       const u8 *texel, u32 texel_bytes,
                       const u8 *palette, u32 pal_bytes,
                       int color0_transparent);

/* BGR555 to 8 bits per channel, the same c * 255 / 31 expansion
 * engine/g2d uses for the 2D palettes. */
void g3d_bgr555(u16 v, u8 *r, u8 *g, u8 *b);

/* ------------------------------------------------------------------ */
/* NSBMD / NSBTX containers                                            */
/* ------------------------------------------------------------------ */

/*
 * A Nitro resource dictionary.  `entries` points into the caller's
 * buffer; nothing here copies.  The per-entry record layout is the one
 * aletheia's nsbmd.py cracked: `count + 1` four-byte patricia nodes,
 * then a {u16 entry_size, u16 names_offset} sub-header at offset
 * (count + 1) * 4 (which is exactly the dictionary header's third
 * halfword), then count * entry_size bytes of records, then count
 * sixteen-byte NUL-padded names.
 */
typedef struct G3dResDict {
    const u8 *base;      /* the dictionary's first byte, borrowed */
    u32 size;            /* total bytes, including the 8-byte header */
    u32 count;
    u32 entry_size;
    const u8 *records;   /* count * entry_size bytes, borrowed */
    const u8 *names;     /* count * 16 bytes, borrowed */
} G3dResDict;

int  g3d_dict_parse(G3dResDict *out, const u8 *buf, u32 size, u32 off);
/* Copies at most 16 bytes plus a terminator into `out`. */
void g3d_dict_name(const G3dResDict *d, u32 i, char out[17]);
const u8 *g3d_dict_record(const G3dResDict *d, u32 i);

typedef struct G3dTexRecord {
    char name[17];
    u32  offset;     /* byte offset into the TEX0 texel region */
    u32  width, height;
    u32  format;
    u32  color0;     /* colour 0 is transparent */
    u32  params;     /* the raw upper half of TEXIMAGE_PARAM */
} G3dTexRecord;

typedef struct G3dPalRecord {
    char name[17];
    u32  offset;     /* byte offset into the TEX0 palette region */
} G3dPalRecord;

/* A parsed TEX0 block.  Every pointer borrows the caller's buffer. */
typedef struct G3dTex0 {
    const u8 *base;
    u32 size;
    G3dResDict tex_dict, pal_dict;
    const u8 *texel;  u32 texel_bytes;
    const u8 *pal;    u32 pal_bytes;
} G3dTex0;

int g3d_tex0_parse(G3dTex0 *out, const u8 *block, u32 size);
int g3d_tex0_texture(const G3dTex0 *t, u32 i, G3dTexRecord *out);
int g3d_tex0_palette(const G3dTex0 *t, u32 i, G3dPalRecord *out);
/*
 * Decode texture `i` of a TEX0 block, resolving its palette the way
 * aletheia's asset lane does: the `<name>_pl` naming convention when the
 * palette dictionary has that exact name (a real link, confirmed 1:1
 * across a/0/1/4's shared sets), otherwise the palette entry whose
 * decoded indices all fall in range, tie-broken by how closely the
 * format's index range matches the palette's colour count.  The material
 * record that would name the palette outright is a structure nothing in
 * this project has cracked; that gap is aletheia's, and it is repeated
 * here rather than papered over.
 */
int g3d_tex0_decode(const G3dTex0 *t, u32 i, G3dTexture *out);

/* The outer BMD0 / BTX0 container: find one block by magic. */
const u8 *g3d_container_block(const u8 *data, u32 size, const char magic[4],
                              u32 *block_size);

/* ------------------------------------------------------------------ */
/* Geometry: the display-list interpreter's output                     */
/* ------------------------------------------------------------------ */

typedef struct G3dVertex {
    /* the vertex command's own output, before the position matrix */
    fx32 ox, oy, oz;
    /* clip space: (ox, oy, oz, 1) * position * projection */
    fx32 x, y, z, w;
    /* texture coordinates in s16.4 texel units, after the texture matrix */
    s32  s, t;
    u8   r, g, b;
} G3dVertex;

typedef struct G3dPoly {
    u32 first;      /* first index in G3dGeom.idx */
    u32 count;      /* 3 or 4 */
    u32 attr;       /* POLYGON_ATTR in force at BEGIN_VTXS */
    u32 teximage;   /* TEXIMAGE_PARAM in force */
    u32 pltt_base;
} G3dPoly;

typedef struct G3dGeom {
    G3dVertex *verts; u32 n_verts, cap_verts;
    u32       *idx;   u32 n_idx,   cap_idx;
    G3dPoly   *polys; u32 n_polys, cap_polys;
} G3dGeom;

void g3d_geom_init(G3dGeom *g);
void g3d_geom_free(G3dGeom *g);

/* ------------------------------------------------------------------ */
/* Geometry: the command interpreter                                   */
/* ------------------------------------------------------------------ */

/* Primitive kinds, the BEGIN_VTXS parameter's low two bits. */
#define G3D_PRIM_TRIANGLES  0u
#define G3D_PRIM_QUADS      1u
#define G3D_PRIM_TRISTRIP   2u
#define G3D_PRIM_QUADSTRIP  3u

typedef struct G3dGx {
    G3dMtxStack mtx;
    G3dGeom    *out;

    fx32 cx, cy, cz;          /* the current vertex register */
    s32  cs, ct;              /* the current texcoord, s16.4 texel units */
    u8   cr, cg, cb;          /* the current colour, 8 bits per channel */
    G3dVec normal;

    u32  poly_attr;           /* the value in force */
    u32  poly_attr_pending;   /* POLYGON_ATTR takes effect at BEGIN_VTXS */
    u32  teximage, pltt_base;

    u32  begin_mode;
    int  batch_open;
    u32  batch_first, batch_count;
    u32  strip_v[4];          /* the last vertices, for strip assembly */

    u32  commands;            /* commands executed, for the tests */
    u32  vtx_cmd_count[8];    /* per vertex-command-opcode counters */
} G3dGx;

void g3d_gx_init(G3dGx *gx, G3dGeom *out);

/* ------------------------------------------------------------------ */
/* Lighting (engine/g3d/light.c)                                       */
/* ------------------------------------------------------------------ */

/*
 * The lighting state the hardware keeps: four lights (direction after the
 * vector matrix, its half vector, colour) and the material (diffuse,
 * ambient, specular, emission as BGR555, the shininess table and whether
 * SPE_EMI enables it).  Directions are in 12 fraction bits.
 */
typedef struct G3dLight {
    s32 dir[4][3];
    s32 half[4][3];
    u16 color[4];
    u16 diffuse, ambient, specular, emission;
    int use_table;
    u8  shine[128];
} G3dLight;

void g3d_light_init(G3dLight *l);
/* LIGHT_VECTOR (0x32), the vector matrix in force when it runs. */
void g3d_light_vector(G3dLight *l, const G3dMtx *vec, u32 param);
/* LIGHT_COLOR (0x33). */
void g3d_light_color(G3dLight *l, u32 param);
/* DIF_AMB (0x30); returns bit 15: the diffuse colour is also the vertex
 * colour from now on. */
int  g3d_light_dif_amb(G3dLight *l, u32 param);
/* SPE_EMI (0x31). */
void g3d_light_spe_emi(G3dLight *l, u32 param);
/* SHININESS (0x34): 32 words, four 8-bit entries each, low byte first. */
void g3d_light_shininess(G3dLight *l, const u32 par[32]);
/*
 * NORMAL (0x21) under the POLYGON_ATTR light bits `light_mask`: the
 * vertex colour the lighting equation gives, BGR555.  light.c's header has
 * the equation and its fixed point.
 */
u16  g3d_light_normal(const G3dLight *l, const G3dMtx *vec, u32 param,
                      u32 light_mask);

/* Parameter words opcode `op` takes, or 0xFF for a byte the hardware does
 * not define as a command.  The one table g3d_gx_run decodes with, so the
 * hardware FIFO (engine/g3d/hw3d.c) frames commands exactly as it does. */
u32 g3d_gx_param_count(u32 op);

/*
 * Run the command stream in `data[start .. )`.
 *
 * `soft_limit` stops decoding at the first command boundary at or past
 * it *while no primitive batch is open*, and `hard_limit` stops
 * unconditionally.  Both exist for the same reason aletheia's
 * nsbmd.decode_shape_dl has them: a real MDL0 shape's declared dlSize
 * sometimes ends a handful of bytes before its own last END_VTXS, so a
 * hard cut at dlSize silently drops that shape's last batch while an
 * unbounded run would walk into the next shape.  Pass `size` for both to
 * decode a stream that really does end where it says it does.
 *
 * Returns the offset decoding stopped at.  Stops on the first
 * unrecognised opcode byte, which is also how the stream's own end is
 * detected (real display lists are followed by non-command bytes).
 */
u32 g3d_gx_run(G3dGx *gx, const u8 *data, u32 size, u32 start,
               u32 soft_limit, u32 hard_limit);

/* One command of a stream: opcode `op` (a defined one) with its
 * g3d_gx_param_count(op) parameter words.  What g3d_gx_run does for each
 * command it decodes; the hardware path calls it for each command of the
 * FIFO and the ports. */
void g3d_gx_exec(G3dGx *gx, u32 op, const u32 *par);

/*
 * The locator aletheia's find_gx_stream is: the first four-byte-aligned
 * offset in [start, size) from which at least `min_commands` commands
 * decode without hitting an unrecognised opcode.  A real shape's byte
 * range opens with a short non-command preamble this project has not
 * identified; that gap is documented in aletheia's docs/ASSETS.md and is
 * inherited here rather than guessed at.  Returns the offset, or `size`
 * when no such run exists.
 */
u32 g3d_gx_find_stream(const u8 *data, u32 size, u32 start, u32 min_commands);

/* ------------------------------------------------------------------ */
/* Map models                                                          */
/* ------------------------------------------------------------------ */

typedef struct G3dShape {
    u32  index;
    u32  dl_offset, dl_size;    /* the descriptor's own two fields */
    u32  material;              /* index into the material dictionary, or ~0 */
    char texture[17];           /* the bare texture name, or "" */
    u32  first_poly, n_polys;
    u32  first_vert, n_verts;
    /*
     * The first node that draws this shape, or 0xFFFFFFFF when the render
     * command list never draws it.  A shape may be drawn under several
     * nodes (instanced), so the authoritative list is G3dModel.draws; this
     * field is a convenience for a shape drawn exactly once.
     */
    u32  node;
} G3dShape;

/*
 * One node of a model's node (bone) dictionary.  The dictionary itself is
 * the same Nitro ResDict layout as the shape dictionary; its 4-byte
 * records are offsets from the dictionary's own start to a variable-sized
 * node record, and the records run back-to-back to the byte the model
 * header names as the end of the node section.
 *
 * The record opens with a 4-byte header: a flags byte, a second flags
 * byte, and a u16 that is part of the rotation.  The flags byte's low
 * four bits are ACTIVE-LOW presence bits, and the components follow in
 * this order:
 *
 *     bit 0 clear   translation, 12 bytes (3 x fx32)
 *     bit 1 clear   rotation; its size is 16 bytes if bit 3 is clear, or
 *                   4 bytes if bit 3 is set
 *     bit 2 clear   scale, 24 bytes (3 x fx32 scale + 3 x fx32 inverse)
 *     bit 3            selects the rotation form (see below)
 *
 * so a record is 4, 8, 16, 20, 28, 32, 40, 44 or 56 bytes.  That rule is
 * validated self-consistently: across every model scanned the decoded
 * component offsets land exactly on the next node record, and every
 * reconstructed rotation matrix is orthonormal (1163 / 1163 nodes).
 *
 * THE ROTATION.  The full form (bit 3 clear) stores a rotation matrix
 * explicitly: the third row's x is the header u16, its y and z are the
 * first two halves of the 4-byte field, and the first two rows are the
 * 12 following bytes as three s16 each, all in 1.0 = 4096.  The
 * cross-check is direct: with 21 nodes that carry both the 4-byte field
 * and the 12-byte rows, the 4-byte halves equal the third row of the
 * matrix those rows imply, 21 of 21.  The packed form (bit 3 set) stores
 * only the third row's y and z, always a unit pair, so the rotation is
 * the one about x that takes z to that direction; identity is (0, 4096).
 *
 * THE SCALE.  The 24-byte component is three fx32 scale factors followed
 * by their reciprocals: the second triple is round(2^24 / first), exact
 * in 534 of 537 samples and within 9 units in the remaining one.
 */
typedef struct G3dModelNode {
    char   name[17];
    u8     flags;           /* the record's first byte, raw            */
    u8     has_translation;
    u8     has_rotation;
    u8     has_scale;
    u8     rotation_full;   /* 1 = 12-byte row pair stored, 0 = packed */
    s32    parent;          /* parent index, or -1 for the root        */
    u16    pivot;           /* header u16: rotation row2.x when full   */
    G3dVec translation;     /* fx32, (0,0,0) if absent                 */
    G3dVec scale;           /* fx32, (1,1,1) if absent                 */
    G3dMtx rotation;        /* unit rotation matrix, fx32              */
    G3dMtx local;           /* scale * rotation * translation          */
    G3dMtx world;           /* local composed with the ancestors'       */
} G3dModelNode;

/* One draw of a shape under a node, in render-command order. */
typedef struct G3dModelDraw {
    u32 shape;
    u32 node;
} G3dModelDraw;

/*
 * One map model out of a/0/0/8, decoded shape by shape.  The sub-file is
 * `header || BMD0 || per-tile grid || tail` and the BMD0 is found by its
 * magic, which is the same strip aletheia's map_shapes.py does.
 */
typedef struct G3dModel {
    char      name[17];
    u8       *raw;         /* owned: the whole sub-file            */
    u32       raw_size;
    const u8 *mdl;         /* the model's data tail, inside raw    */
    u32       mdl_size;
    G3dShape *shapes; u32 n_shapes;
    G3dModelNode  *nodes; u32 n_nodes;   /* the node/bone dictionary */
    G3dModelDraw  *draws; u32 n_draws;   /* the shape -> node binding */
    G3dGeom   geom;
} G3dModel;

/* The a/0/1/4 texture archive, indexed by texture name.  Sub-files are
 * decoded lazily: the index is a name scan with no pixel work. */
typedef struct G3dTexArchive G3dTexArchive;

G3dTexArchive *g3d_texarchive_open(const char *rel_path);
void           g3d_texarchive_close(G3dTexArchive *ar);
u32            g3d_texarchive_names(const G3dTexArchive *ar);
/* Returns a borrowed texture owned by the archive, or NULL. */
const G3dTexture *g3d_texarchive_get(G3dTexArchive *ar, const char *name);

/*
 * Load map model `index` out of `rel_path` (a/0/0/8), decode every
 * shape's display list into one G3dGeom, and resolve each shape's
 * material to a texture name.  `ar` may be NULL, in which case the
 * material dictionary is identified without the cross-check against real
 * texture names and the texture names are still recorded.
 */
int  g3d_model_load(G3dModel *out, const char *rel_path, u32 index,
                    const G3dTexArchive *ar);
void g3d_model_free(G3dModel *m);

/*
 * The node dictionary.  `g3d_model_node` returns a borrowed node, or NULL.
 * `local` is scale * rotation * translation in row-vector order and
 * `world` is `local` composed with the ancestors' matrices; that is what
 * `g3d_render_model` applies to a shape's vertices, and the parent index
 * comes from the render-command stream, not from the node record.
 *
 * `g3d_model_set_node_local` overwrites a node's local matrix (the pose a
 * caller would drive from an animation), and `g3d_model_set_node_parent`
 * sets the parent link.  `g3d_model_update_nodes` recomputes every
 * `world` from the locals and the parent links: world = local *
 * parent->world, so a point is transformed by the node's own matrix first
 * and the ancestors' after.
 *
 * The draw list is the shape -> node binding the render-command stream
 * carries: `g3d_model_draw_count` / `g3d_model_draw` read it, and a shape
 * drawn under several nodes appears once per draw.
 */
u32                 g3d_model_node_count(const G3dModel *m);
const G3dModelNode *g3d_model_node(const G3dModel *m, u32 i);
void g3d_model_set_node_local(G3dModel *m, u32 i, const G3dMtx *local);
void g3d_model_set_node_parent(G3dModel *m, u32 i, s32 parent);
void g3d_model_update_nodes(G3dModel *m);
u32                 g3d_model_draw_count(const G3dModel *m);
const G3dModelDraw *g3d_model_draw(const G3dModel *m, u32 i);

/* ------------------------------------------------------------------ */
/* The rasteriser                                                      */
/* ------------------------------------------------------------------ */

#define G3D_SCREEN_W 256u
#define G3D_SCREEN_H 192u

/*
 * The render target: an RGBA buffer in exactly the byte order and at
 * exactly the geometry g2d_frame_compose writes (R, G, B, A, row-major,
 * top row first), so it can stand as BG0's pixels the way the 3D layer
 * does on hardware, and a depth buffer beside it.
 */
typedef struct G3dTarget {
    u32  width, height;
    u8  *rgba;    /* owned */
    s32 *depth;   /* owned; larger is further away */
    /*
     * Per-frame state engine/g3d/hw3d.c sets from the 3D registers.  All
     * zero after g3d_target_init, which is exactly the behaviour the
     * rasteriser had before they existed.
     *   alpha_ref  the alpha test (DISP3DCNT bit 2, ALPHA_TEST_REF): a
     *              pixel whose 5-bit alpha is <= this is not drawn.
     *   toon       32 R,G,B triples, the toon table; NULL makes polygon
     *              mode 2 plain modulate.  Borrowed.
     *   highlight  1: polygon mode 2 is highlight shading (DISP3DCNT
     *              bit 1), 0: toon.
     */
    u32       alpha_ref;
    const u8 *toon;
    u32       highlight;
    /*
     *   wbuffer    1: the depth buffer holds w (SWAP_BUFFERS bit 1), 0: z.
     *   aa         1: record the coverage of span-end pixels of opaque
     *              polygons and the colour beneath them, for
     *              g3d_target_antialias (DISP3DCNT bit 4).
     */
    u32       wbuffer;
    u32       aa;

    /*
     * Per-pixel attributes beside the colour and the depth, owned and
     * allocated by g3d_target_init; what the hardware's rear passes (edge
     * marking, fog, anti-aliasing) read.
     *   pid    the polygon ID of the opaque polygon last drawn there, or
     *          the clear plane's.
     *   pflag  G3D_PIX_* bits.
     *   xid    0x40 | the polygon ID of the translucent pixel last drawn
     *          there, 0 when none since the clear or an opaque pixel.  The
     *          hardware does not draw a translucent pixel over one of the
     *          same ID, so a see-through shape never blends with itself.
     *   cov    an edge pixel's 5-bit coverage (31: full).
     *   under  R,G,B,A of what was there before an edge pixel was drawn.
     */
    u8 *pid;
    u8 *pflag;
    u8 *xid;
    u8 *cov;
    u8 *under;
} G3dTarget;

#define G3D_PIX_OPAQUE 0x01u   /* an opaque polygon drew this pixel      */
#define G3D_PIX_FOG    0x02u   /* the fog applies (POLYGON_ATTR bit 15)  */
#define G3D_PIX_EDGE   0x04u   /* a span end with partial coverage       */

int  g3d_target_init(G3dTarget *t, u32 width, u32 height);
void g3d_target_free(G3dTarget *t);
/* Clear to one colour and to the far depth; polygon IDs 0, no flags. */
void g3d_target_clear(G3dTarget *t, u8 r, u8 g, u8 b, u8 a);

/*
 * The three passes the DS 3D engine runs over the finished frame, in the
 * hardware's order (edge marking, fog, anti-aliasing).  engine/g3d/raster.c
 * says what each does and which parts are approximations.
 *
 * Edge marking: `edge` is the 8 BGR555 EDGE_COLOR entries; the screen
 * border counts as the clear plane, `clear_id` and `clear_depth`.
 */
void g3d_target_edge_mark(G3dTarget *t, const u16 edge[8], u32 clear_id,
                          s32 clear_depth);

/*
 * Fog: `table` the 32 FOG_TABLE densities (7 bits), `color` FOG_COLOR's
 * BGR555 in bits 0-14 and alpha in 16-20, `offset` FOG_OFFSET (15 bits),
 * `shift` DISP3DCNT bits 8-11, `alpha_only` DISP3DCNT bit 6.
 */
void g3d_target_fog(G3dTarget *t, const u8 table[32], u32 color, u32 offset,
                    u32 shift, int alpha_only);

/* Anti-aliasing: blend the silhouette edge pixels the raster recorded. */
void g3d_target_antialias(G3dTarget *t);

#define G3D_DEPTH_FAR 0x7FFFFFFF

/* POLYGON_ATTR fields. */
#define G3D_ATTR_MODE(a)        (((a) >> 4) & 3u)
#define G3D_ATTR_BACK(a)        (((a) >> 6) & 1u)
#define G3D_ATTR_FRONT(a)       (((a) >> 7) & 1u)
#define G3D_ATTR_ALPHA(a)       (((a) >> 16) & 0x1Fu)
#define G3D_ATTR_ID(a)          (((a) >> 24) & 0x3Fu)
#define G3D_ATTR_LIGHTS(a)      ((a) & 0xFu)
#define G3D_ATTR_XLU_DEPTH(a)   (((a) >> 11) & 1u)
#define G3D_ATTR_DEPTH_EQ(a)    (((a) >> 14) & 1u)
#define G3D_ATTR_FOG(a)         (((a) >> 15) & 1u)
#define G3D_POLY_MODE_MODULATE  0u
#define G3D_POLY_MODE_DECAL     1u
#define G3D_POLY_MODE_TOON      2u
#define G3D_POLY_MODE_SHADOW    3u

/* TEXIMAGE_PARAM fields this engine reads. */
#define G3D_TEX_REPEAT_S(p)  (((p) >> 16) & 1u)
#define G3D_TEX_REPEAT_T(p)  (((p) >> 17) & 1u)
#define G3D_TEX_FLIP_S(p)    (((p) >> 18) & 1u)
#define G3D_TEX_FLIP_T(p)    (((p) >> 19) & 1u)
#define G3D_TEX_FORMAT(p)    (((p) >> 26) & 7u)

/*
 * Draw one convex polygon of `n` clip-space vertices (3 or 4).  The
 * near-plane clip, the perspective divide, the viewport transform, the
 * culling test, the edge walk, the depth test and the texture blend are
 * all inside; `tex` may be NULL for an untextured polygon.
 */
/* Diagnostic counters of g3d_raster_polygon (never reset by the engine; the
 * hw3d trace zeroes them per frame). */
typedef struct G3dRasterStats {
    u32 submitted, drawn, rej_count, rej_alpha0, rej_shadow, rej_clip,
        rej_degenerate, rej_cull;
} G3dRasterStats;
extern G3dRasterStats g3d_raster_stats;

void g3d_raster_polygon(G3dTarget *t, const G3dVertex *v, u32 n,
                        u32 attr, u32 teximage, const G3dTexture *tex);

/* Draw every polygon of a geometry buffer, looking a texture up per
 * polygon through the callback (which may return NULL). */
typedef const G3dTexture *(*G3dTexLookup)(void *user, u32 poly_index);
/* Put the polygons in the order the hardware draws them (opaque first, then
 * translucent; the translucent ones sorted by Y unless `manual`). */
void g3d_geom_sort(G3dGeom *g, int manual);
void g3d_raster_geom(G3dTarget *t, const G3dGeom *g,
                     G3dTexLookup lookup, void *user);

/* Render a whole model with a camera: sets the matrix stack up, re-runs
 * nothing (the geometry is already decoded in model space), transforms
 * and rasterises.  `view` is the camera matrix, `proj` the projection. */
void g3d_render_model(G3dTarget *t, const G3dModel *m,
                      const G3dMtx *view, const G3dMtx *proj,
                      G3dTexArchive *ar);

/* ------------------------------------------------------------------ */
/* Meeting the 2D compositor                                           */
/* ------------------------------------------------------------------ */

/*
 * Compose a 2D frame over a 3D one, with the 3D layer at the back: every
 * pixel where the 2D frame would show its backdrop shows the 3D frame
 * instead.  That is the arrangement a DS game uses when BG0 carries the
 * 3D output at the lowest priority, which is the overworld's.
 *
 * What it is not: a general BG0-is-3D hook.  A 2D BG layer *behind* the
 * 3D layer, and the colour special effects blending with a 3D pixel, need
 * the 3D layer inside g2d's own per-pixel stack rather than under it, and
 * that is the piece of Milestone 4 this does not close.  `desc` is the
 * ordinary G2dFrameDesc engine/g2d composes.
 */
void g3d_compose_over(const void *frame_desc, const G3dTarget *t,
                      u32 width, u32 height, u8 *rgba);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_G3D_H */
