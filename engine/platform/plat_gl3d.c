/*
 * engine/platform/plat_gl3d.c -- the DS 3D engine on the GPU.
 *
 * Draws a G3dSnapshot (engine/g3d/hw3d.h) into a framebuffer at any size,
 * following engine/g3d/raster.c rule for rule: clip-space vertices (the GPU
 * clips and divides), perspective-correct vertex colour and texture
 * coordinates, nearest texel fetch with the repeat, flip and clamp modes,
 * modulate / decal / toon / highlight shading on 6-bit colour and 5-bit alpha,
 * the alpha test, the z or w depth buffer, front / back culling on the
 * projected winding.  The result is premultiplied RGBA (a translucent pixel
 * over the clear plane keeps its own alpha, as the 2D engine expects).
 *
 * Edge marking is a second pass: the draw writes each opaque pixel's
 * polygon ID beside its colour, and the pass outlines every opaque pixel
 * that is nearer than a neighbour of another ID, in the EDGE_COLOR entry of
 * its ID, as engine/g3d/raster.c g3d_target_edge_mark does, at any
 * resolution:
 *
 *   - where: a pixel is in the outline when the first pixel of another ID
 *     in one of the four directions is at most one DS pixel away (the
 *     scale, in framebuffer pixels), so the outline is one DS pixel wide
 *     and follows the polygon's own edge at the higher resolution;
 *   - whether: that boundary is an edge when the hardware's test passes
 *     for two points one DS pixel apart across it, half a DS pixel inside
 *     and half a DS pixel outside, as the DS compares the centres of two
 *     neighbouring pixels.
 *
 * The two points are placed from the boundary, not from a fixed grid, so
 * the answer moves with the object.  Tested per framebuffer pixel, one DS
 * pixel away, the outline left a gap under an object standing on the floor
 * and drew a thin line below it; tested at the nearest framebuffer
 * boundary, it drew a comb of thin lines where two surfaces meet at nearly
 * the same depth; tested at the centres of a screen-fixed grid of DS
 * pixels, its dashes flickered as the scene moved under that grid.
 *
 * Not done on the GPU (the snapshot says so in `unsupported` and the caller
 * shows the CPU frame instead): fog, the rear-plane bitmap, shadow polygons.
 * Anti-aliasing is the resolution itself.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_gl_int.h"
#ifdef far
#undef far /* the windows headers define it; BmapDraw has a member of that name */
#endif
#include "bmap.h"
#include "bmap_place.h"
#include "g3d.h"
#include "hw3d.h"
#include "prof.h"

#define FLOATS_PER_VERTEX 9u

static const char *k_vs3 =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec2 a_st;\n"
    "layout(location = 2) in vec3 a_rgb;\n"
    "out vec2 v_st;\n"
    "out vec3 v_rgb;\n"
    "void main() {\n"
    "    v_st = a_st;\n"
    "    v_rgb = a_rgb;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *k_fs3 =
    "#version 330 core\n"
    "in vec2 v_st;\n"
    "in vec3 v_rgb;\n"
    "layout(location = 0) out vec4 o;\n"
    "layout(location = 1) out uint o_id;\n"
    "uniform int u_id;\n"
    "uniform int u_mode;\n"          /* 0 modulate, 1 decal, 2 toon/highlight */
    "uniform int u_alpha5;\n"
    "uniform int u_tex;\n"
    "uniform sampler2D u_texture;\n"
    "uniform ivec2 u_texsize;\n"
    "uniform ivec4 u_wrap;\n"        /* repeat s, repeat t, flip s, flip t */
    "uniform int u_alpharef;\n"
    "uniform int u_toon;\n"
    "uniform int u_highlight;\n"
    "uniform vec3 u_toontab[32];\n"
    "uniform int u_front;\n"
    "uniform int u_back;\n"
    "uniform int u_wbuf;\n"
    "uniform int u_pass;\n"
    "int wrapc(int v, int size, int rep, int flip) {\n"
    "    if (size <= 0) { return 0; }\n"
    "    if (rep == 0) { return clamp(v, 0, size - 1); }\n"
    "    if (flip != 0) {\n"
    "        int k = v & (size * 2 - 1);\n"
    "        if (k >= size) { k = size * 2 - 1 - k; }\n"
    "        return k;\n"
    "    }\n"
    "    return v & (size - 1);\n"
    "}\n"
    "int mod6(int a8, int b8) {\n"
    "    int a6 = a8 >> 2;\n"
    "    int b6 = b8 >> 2;\n"
    "    int r = ((a6 + 1) * (b6 + 1) - 1) >> 6;\n"
    "    r = min(r, 63);\n"
    "    return r * 255 / 63;\n"
    "}\n"
    "int mod5(int a, int b) {\n"
    "    int r = ((a + 1) * (b + 1) - 1) >> 5;\n"
    "    return min(r, 31);\n"
    "}\n"
    "void main() {\n"
    "    if (gl_FrontFacing) { if (u_front == 0) { discard; } }\n"
    "    else { if (u_back == 0) { discard; } }\n"
    "    ivec3 c = clamp(ivec3(floor(v_rgb + 0.001)), ivec3(0), ivec3(255));\n"
    "    ivec3 hl = ivec3(0);\n"
    "    if (u_toon == 1) {\n"
    "        ivec3 e = ivec3(u_toontab[c.r >> 3] + 0.5);\n"
    "        if (u_highlight == 1) { hl = e; c.g = c.r; c.b = c.r; }\n"
    "        else { c = e; }\n"
    "    }\n"
    "    int a5 = u_alpha5;\n"
    "    ivec3 col = c;\n"
    "    if (u_tex == 1) {\n"
    "        int x = wrapc(int(floor(v_st.x / 16.0)), u_texsize.x, u_wrap.x, u_wrap.z);\n"
    "        int y = wrapc(int(floor(v_st.y / 16.0)), u_texsize.y, u_wrap.y, u_wrap.w);\n"
    "        ivec4 tc = ivec4(texelFetch(u_texture, ivec2(x, y), 0) * 255.0 + 0.5);\n"
    "        if (u_mode == 1) {\n"
    "            int ta = tc.a >> 3;\n"
    "            col = (tc.rgb * ta + c * (31 - ta)) / 31;\n"
    "        } else {\n"
    "            col = ivec3(mod6(tc.r, c.r), mod6(tc.g, c.g), mod6(tc.b, c.b));\n"
    "            a5 = mod5(tc.a >> 3, u_alpha5);\n"
    "        }\n"
    "    }\n"
    "    col = min(col + hl, ivec3(255));\n"
    "    if (u_pass == 1 && a5 < 31) { discard; }\n"
    "    if (u_pass == 2 && a5 >= 31) { discard; }\n"
    "    if (a5 == 0 || a5 <= u_alpharef) { discard; }\n"
    "    float a = float(a5) / 31.0;\n"
    "    o = vec4(vec3(col) / 255.0 * a, a);\n"
    "    o_id = uint(u_id);\n"
    "    gl_FragDepth = (u_wbuf == 1) ? clamp((1.0 / gl_FragCoord.w) / 16777216.0, 0.0, 1.0)\n"
    "                                 : gl_FragCoord.z;\n"
    "}\n";

/* Edge marking, over the frame just drawn (see the file comment).  A pixel
 * off the frame counts as the clear plane: its ID and depth. */
static const char *k_vse =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *k_fse =
    "#version 330 core\n"
    "uniform sampler2D u_col;\n"
    "uniform usampler2D u_id;\n"
    "uniform sampler2D u_dep;\n"
    "uniform int u_step;\n"
    "uniform uint u_cid;\n"
    "uniform float u_cdep;\n"
    "uniform vec3 u_edge[8];\n"
    "out vec4 o;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 sz = textureSize(u_col, 0);\n"
    "    vec4 c = texelFetch(u_col, p, 0);\n"
    "    uint id = texelFetch(u_id, p, 0).r;\n"
    "    o = c;\n"
    "    if ((id & 0x80u) == 0u) { return; }\n"
    "    id &= 0x3Fu;\n"
    "    int half_ = u_step / 2;\n"
    "    ivec2 k[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        int j = 1;\n"
    "        bool found = false;\n"
    "        for (; j <= u_step; j++) {\n"
    "            ivec2 q = p + k[i] * j;\n"
    "            uint qid = u_cid;\n"
    "            if (q.x >= 0 && q.y >= 0 && q.x < sz.x && q.y < sz.y) {\n"
    "                qid = texelFetch(u_id, q, 0).r & 0x3Fu;\n"
    "            }\n"
    "            if (qid != id) { found = true; break; }\n"
    "        }\n"
    "        if (!found) { continue; }\n"
    "        ivec2 bi = p + k[i] * (j - 1);\n"
    "        ivec2 a = clamp(bi - k[i] * half_, ivec2(0), sz - 1);\n"
    "        if ((texelFetch(u_id, a, 0).r & 0x3Fu) != id) { a = bi; }\n"
    "        float d = texelFetch(u_dep, a, 0).r;\n"
    "        ivec2 b = bi + k[i] * (u_step - half_);\n"
    "        uint nid = u_cid;\n"
    "        float nd = u_cdep;\n"
    "        if (b.x >= 0 && b.y >= 0 && b.x < sz.x && b.y < sz.y) {\n"
    "            nid = texelFetch(u_id, b, 0).r & 0x3Fu;\n"
    "            nd = texelFetch(u_dep, b, 0).r;\n"
    "        }\n"
    "        if (nid != id && d < nd) {\n"
    "            o = vec4(u_edge[id >> 3], 1.0);\n"
    "            return;\n"
    "        }\n"
    "    }\n"
    "}\n";

/* Fog, over the frame after the edge marking (raster.c, g3d_target_fog, is
 * the reference).  The ID buffer's bit 6 is the pixel's fog flag; the depth
 * texture holds the depth in the DS's 15-bit units times 1/32768 (z/w as
 * gl_FragCoord.z, or w / 2^24 for a w-buffered frame).  The colour target is
 * premultiplied, so the straight colour is taken back out, blended, and
 * multiplied again; a clear-plane pixel of alpha 0 blends from the clear
 * colour. */
static const char *k_fsf =
    "#version 330 core\n"
    "uniform sampler2D u_col;\n"
    "uniform usampler2D u_id;\n"
    "uniform sampler2D u_dep;\n"
    "uniform vec4 u_fog;\n"
    "uniform float u_table[32];\n"
    "uniform float u_offset;\n"
    "uniform float u_step;\n"
    "uniform int u_aonly;\n"
    "uniform vec3 u_clear;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec4 c = texelFetch(u_col, p, 0);\n"
    "    uint id = texelFetch(u_id, p, 0).r;\n"
    "    o = c;\n"
    "    if ((id & 0x40u) == 0u) { return; }\n"
    "    float d = min(floor(texelFetch(u_dep, p, 0).r * 32768.0), 32767.0);\n"
    "    float den;\n"
    "    if (u_step <= 0.0) { den = d > u_offset ? u_table[31] : u_table[0]; }\n"
    "    else if (d <= u_offset + u_step) { den = u_table[0]; }\n"
    "    else if (d >= u_offset + u_step * 32.0) { den = u_table[31]; }\n"
    "    else {\n"
    "        float rel = d - u_offset - u_step;\n"
    "        int k = int(floor(rel / u_step));\n"
    "        float f = rel - float(k) * u_step;\n"
    "        den = floor((u_table[k] * (u_step - f) + u_table[k + 1] * f) / u_step);\n"
    "    }\n"
    "    if (den >= 127.0) { den = 128.0; }\n"
    "    vec3 sc = c.a > 0.0 ? c.rgb / c.a : ((id & 0x80u) == 0u ? u_clear : vec3(0.0));\n"
    "    float a5 = floor(c.a * 31.0 + 0.5);\n"
    "    float na = floor((u_fog.a * 31.0 * den + a5 * (128.0 - den)) / 128.0) / 31.0;\n"
    "    vec3 nc = u_aonly == 1 ? sc : (u_fog.rgb * den + sc * (128.0 - den)) / 128.0;\n"
    "    o = vec4(nc * na, na);\n"
    "}\n";

/* ------------------------------------------------------------------ */
/* The battle backdrop (sdk/battle_view.c, docs/battle-backdrop.md)     */
/* ------------------------------------------------------------------ */

/* A placed scene (engine/world/bmap.h) drawn before the game's polygons with
 * the battle's camera: the same projection, the same view scale and the same
 * depth buffer, so the game's sprites, shadows and particles depth test
 * against the map.  The scene's textures and vertices go to the GPU once per
 * scene, when the snapshot names a new one. */
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

static const char *k_vsb =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_st;\n"
    "layout(location = 2) in vec4 a_rgb;\n"
    "uniform vec4 u_r0;\n"
    "uniform vec4 u_r1;\n"
    "uniform vec4 u_r2;\n"
    "uniform vec4 u_r3;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_pivot;\n"
    "uniform int u_bb;\n"
    "uniform vec3 u_uv0;\n"
    "uniform vec3 u_uv1;\n"
    "out vec2 v_st;\n"
    "out vec3 v_rgb;\n"
    "out vec3 v_pos;\n"
    "out float v_bbc;\n"
    "void main() {\n"
    "    vec3 q = a_pos;\n"
    "    v_bbc = 1.0;\n"
    "    if (u_bb > 0) {\n"
    "        /* built facing +z around the pivot: turn it toward the camera */\n"
    "        vec3 z = u_eye - u_pivot;\n"
    "        if (u_bb == 2) { z.y = 0.0; }\n"
    "        z = normalize(z);\n"
    "        vec3 x = normalize(cross(vec3(0.0, 1.0, 0.0), z));\n"
    "        vec3 y = cross(z, x);\n"
    "        vec3 o = a_pos - u_pivot;\n"
    "        q = u_pivot + x * o.x + y * o.y + z * o.z;\n"
    "    }\n"
    "    vec4 p = vec4(q, 1.0);\n"
    "    v_pos = q;\n"
    "    /* the draw's texture animation this frame (identity: none) */\n"
    "    v_st = vec2(dot(u_uv0, vec3(a_st, 1.0)), dot(u_uv1, vec3(a_st, 1.0)));\n"
    "    v_rgb = a_rgb.rgb;\n"
    "    gl_Position = vec4(dot(u_r0, p), dot(u_r1, p), dot(u_r2, p), dot(u_r3, p));\n"
    "}\n";

static const char *k_fsb =
    "#version 330 core\n"
    "in vec2 v_st;\n"
    "in vec3 v_rgb;\n"
    "in vec3 v_pos;\n"
    "in float v_bbc;\n"
    "layout(location = 0) out vec4 o;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_foc;\n"
    "uniform vec4 u_atm;\n"
    "uniform vec3 u_sky;\n"
    "uniform vec4 u_fog;\n"
    "uniform vec3 u_fogc;\n"
    "uniform int u_tex;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_alpha;\n"
    "uniform float u_cut;\n"
    "uniform float u_cut_hi;\n"
    "uniform int u_front;\n"
    "uniform vec4 u_poke[32];\n"
    "uniform int u_npoke;\n"
    "uniform int u_grass;\n"
    "void main() {\n"
    "    if (u_front == 1) {\n"
    "        /* the second pass: what of the map stands between the camera and\n"
    "           a Pokemon, over the sprite's box (seen at the Pokemon's\n"
    "           distance: 2.8 units either side, from just under its feet to\n"
    "           6 above), is drawn again over it */\n"
    "        bool keep = false, behind = false;\n"
    "        vec3 f = v_pos - u_eye;\n"
    "        for (int i = 0; i < u_npoke; i++) {\n"
    "            vec3 d = u_poke[i].xyz - u_eye;\n"
    "            float L = length(d);\n"
    "            if (L < 1e-3) { continue; }\n"
    "            vec3 n = d / L;\n"
    "            float along = dot(f, n);\n"
    "            if (along < 0.05) { continue; }\n"
    "            vec3 side = normalize(cross(n, vec3(0.0, 1.0, 0.0)));\n"
    "            vec3 up = cross(side, n);\n"
    "            vec3 o = (f - n * along) * (L / along);\n"
    "            float sx = dot(o, side), uy = dot(o, up);\n"
    "            float hgt = abs(u_poke[i].w);\n"
    "            if (abs(sx) < 0.47 * hgt && uy > -1.0 && uy < hgt) {\n"
    "                if (u_poke[i].w < 0.0) { behind = true; }   /* a shield: never covered */\n"
    "                /* in front of this Pokemon: drawn again; behind it\n"
    "                   (a nearer Pokemon covers it): never over that one,\n"
    "                   tested against the sprite's own size, not the loose\n"
    "                   box (a near Pokemon's loose box reaches the far one\n"
    "                   on screen, and the map in front of the far one went) */\n"
    "                if (along <= L - 0.3) { keep = true; }\n"
    "                else if (abs(sx) < 0.25 * hgt && uy < 0.6 * hgt) { behind = true; }\n"
    "            }\n"
    "        }\n"
    "        if (!keep || behind) { discard; }\n"
    "    }\n"
    "    vec4 c = vec4(v_rgb, 1.0);\n"
    "    if (u_tex == 1) { c *= texture(u_texture, v_st); }\n"
    "    float a = c.a * u_alpha;\n"
    "    if (a < u_cut || a >= u_cut_hi) { discard; }\n"
    "    if (u_cut > 0.4) { a = 1.0; }\n"
    "    if (u_fog.z > 0.0) {\n"
    "        float f = clamp((length(v_pos.xz - u_fog.xy) - u_fog.z) / u_fog.w, 0.0, 1.0);\n"
    "        c.rgb = mix(c.rgb, u_fogc, f);\n"
    "    }\n"
    "    if (u_atm.w > 0.0) {\n"
    "        /* the far part fades into the sky before the map ends */\n"
    "        float f = smoothstep(u_atm.z, u_atm.w, length(v_pos.xz - u_atm.xy));\n"
    "        c.rgb = mix(c.rgb, u_sky, f);\n"
    "    }\n"
    "    o = vec4(c.rgb * a, a);\n"
    "}\n";

/* A see-through triangle of the map, with its draw and its middle: these
 * are drawn far to near, each blended over what lies behind it (one object,
 * a gym's stage with the glow behind it, can lie both before and behind
 * another part of itself). */
/* The stencil while the game's polygons draw: the low bits hold 0x40 | the
 * polygon ID of the last see-through pixel (the hardware's rule), the top
 * bit marks every pixel the game drew, for the backdrop's front pass. */
#define STENCIL_GAME 0x80u
#define STENCIL_IDS  0x7Fu

typedef struct BdPiece {
    unsigned draw, first, count;
    float mid[3];
} BdPiece;

/* A see-through draw whose material is opaque (alpha 31: the texture alone
 * has soft or cut-out texels): its solid texels draw in the depth pass with
 * the opaque map, so only its soft edges are blended, and they need no
 * triangle order of their own. */
#define BD_CORE_CUT 0.99f
#define BD_SOFT_ONLY(d) ((d)->alpha >= 31u)

typedef struct Gl3dBackdrop {
    GLuint prog, vao, vbo, ibo;
    GLint u_r[4], u_pivot, u_bb, u_atm, u_sky, u_eye, u_foc, u_fog, u_fogc, u_tex, u_texture, u_alpha, u_cut, u_cut_hi;
    GLint u_front, u_poke, u_npoke, u_grass, u_uv0, u_uv1;
    const void *scene;          /* the scene on the GPU                    */
    unsigned gen;
    float reach;                /* half-width of the placed map, battle units */
    GLuint *tex;
    unsigned n_tex;
    BdPiece *piece;             /* the see-through parts, and their order  */
    unsigned n_piece;
    unsigned *order;
    float *dist;
    GLuint *idx;                /* their vertices far to near (the ibo's)  */
    unsigned n_idx;
    unsigned *at;               /* where each piece in the order starts    */
    float idx_eye[3];           /* the camera the ibo was sorted for       */
    int idx_ok;
} Gl3dBackdrop;

static Gl3dBackdrop g_bd;

static void backdrop_free_scene(void)
{
    if (g_bd.n_tex > 0) {
        glDeleteTextures((GLsizei)g_bd.n_tex, g_bd.tex);
    }
    free(g_bd.tex);
    g_bd.tex = NULL;
    g_bd.n_tex = 0;
    free(g_bd.piece);
    free(g_bd.order);
    free(g_bd.dist);
    free(g_bd.idx);
    free(g_bd.at);
    g_bd.at = NULL;
    g_bd.piece = NULL;
    g_bd.order = NULL;
    g_bd.dist = NULL;
    g_bd.idx = NULL;
    g_bd.idx_ok = 0;
    g_bd.n_piece = 0;
    g_bd.scene = NULL;
}

/* The see-through parts of a scene (see BdPiece). */
static void backdrop_pieces(const BmapScene *sc)
{
    unsigned i, n = 0, cap = 0;

    for (i = 0; i < sc->nd; i++) {
        const BmapDraw *d = &sc->d[i];
        unsigned a, e;

        if (!d->translucent || d->count == 0 || d->first + d->count > sc->nv) {
            continue;
        }
        for (a = d->first; a < d->first + d->count; a = e) {
            BdPiece *pc;
            unsigned j;

            /* one triangle (an object can be both near and far), or the
             * whole draw when only its soft edges blend */
            e = BD_SOFT_ONLY(d) ? d->first + d->count / 3u * 3u : a + 3;
            if (e > d->first + d->count || e == a) {
                break;              /* a draw whose count is not whole triangles */
            }
            if (n == cap) {
                unsigned ncap = cap ? cap * 2 : 64;
                BdPiece *np = (BdPiece *)realloc(g_bd.piece, ncap * sizeof *np);

                if (np == NULL) {
                    break;
                }
                g_bd.piece = np;
                cap = ncap;
            }
            pc = &g_bd.piece[n++];
            pc->draw = i;
            pc->first = a;
            pc->count = e - a;
            pc->mid[0] = pc->mid[1] = pc->mid[2] = 0.0f;
            for (j = a; j < e; j++) {
                pc->mid[0] += sc->v[j].x / (float)(e - a);
                pc->mid[1] += sc->v[j].y / (float)(e - a);
                pc->mid[2] += sc->v[j].z / (float)(e - a);
            }
        }
    }
    g_bd.n_piece = n;
    g_bd.order = (unsigned *)malloc((n > 0 ? n : 1) * sizeof *g_bd.order);
    g_bd.dist = (float *)malloc((n > 0 ? n : 1) * sizeof *g_bd.dist);
    {
        unsigned k, verts = 0;

        for (k = 0; k < n; k++) {
            verts += g_bd.piece[k].count;
        }
        g_bd.idx = (GLuint *)malloc((verts > 0 ? verts : 1) * sizeof *g_bd.idx);
    }
    g_bd.at = (unsigned *)malloc((n > 0 ? n : 1) * sizeof *g_bd.at);
    g_bd.idx_ok = 0;
    if (g_bd.order == NULL || g_bd.dist == NULL || g_bd.idx == NULL || g_bd.at == NULL) {
        g_bd.n_piece = 0;
    }
}

static int piece_far_first(const void *a, const void *b)
{
    float da = g_bd.dist[*(const unsigned *)a], db = g_bd.dist[*(const unsigned *)b];

    return da < db ? 1 : da > db ? -1 : 0;
}

static void backdrop_destroy(EidosGl *g)
{
    backdrop_free_scene();
    if (g_bd.vbo != 0) {
        g->glDeleteBuffers(1, &g_bd.vbo);
    }
    if (g_bd.ibo != 0) {
        g->glDeleteBuffers(1, &g_bd.ibo);
    }
    if (g_bd.vao != 0) {
        g->glDeleteVertexArrays(1, &g_bd.vao);
    }
    if (g_bd.prog != 0) {
        g->glDeleteProgram(g_bd.prog);
    }
    memset(&g_bd, 0, sizeof(g_bd));
}

static int backdrop_init(EidosGl *g)
{
    int i;

    g_bd.prog = eidos_gl_compile_program(g, k_vsb, k_fsb);
    if (g_bd.prog == 0) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        char nm[8];

        snprintf(nm, sizeof(nm), "u_r%d", i);
        g_bd.u_r[i] = g->glGetUniformLocation(g_bd.prog, nm);
    }
    g_bd.u_eye = g->glGetUniformLocation(g_bd.prog, "u_eye");
    g_bd.u_foc = g->glGetUniformLocation(g_bd.prog, "u_foc");
    g_bd.u_pivot = g->glGetUniformLocation(g_bd.prog, "u_pivot");
    g_bd.u_bb = g->glGetUniformLocation(g_bd.prog, "u_bb");
    g_bd.u_atm = g->glGetUniformLocation(g_bd.prog, "u_atm");
    g_bd.u_sky = g->glGetUniformLocation(g_bd.prog, "u_sky");
    g_bd.u_fog = g->glGetUniformLocation(g_bd.prog, "u_fog");
    g_bd.u_fogc = g->glGetUniformLocation(g_bd.prog, "u_fogc");
    g_bd.u_tex = g->glGetUniformLocation(g_bd.prog, "u_tex");
    g_bd.u_texture = g->glGetUniformLocation(g_bd.prog, "u_texture");
    g_bd.u_alpha = g->glGetUniformLocation(g_bd.prog, "u_alpha");
    g_bd.u_cut = g->glGetUniformLocation(g_bd.prog, "u_cut");
    g_bd.u_cut_hi = g->glGetUniformLocation(g_bd.prog, "u_cut_hi");
    g_bd.u_front = g->glGetUniformLocation(g_bd.prog, "u_front");
    g_bd.u_poke = g->glGetUniformLocation(g_bd.prog, "u_poke");
    g_bd.u_npoke = g->glGetUniformLocation(g_bd.prog, "u_npoke");
    g_bd.u_grass = g->glGetUniformLocation(g_bd.prog, "u_grass");
    g_bd.u_uv0 = g->glGetUniformLocation(g_bd.prog, "u_uv0");
    g_bd.u_uv1 = g->glGetUniformLocation(g_bd.prog, "u_uv1");
    g->glGenVertexArrays(1, &g_bd.vao);
    g->glGenBuffers(1, &g_bd.vbo);
    g->glGenBuffers(1, &g_bd.ibo);
    return 0;
}

/* Upload a texture with a box-filtered mip chain (alpha-weighted colour, so
 * cut-out edges do not darken): the far map does not shimmer, the near map
 * keeps its crisp texels (nearest magnification). */
static void backdrop_mips(const BmapTexture *t)
{
    int w = t->w, h = t->h, lvl = 0, cut = 0;
    float cov = 0.0f;
    unsigned char *cur = (unsigned char *)malloc((size_t)w * h * 4);

    if (cur == NULL) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, t->rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        return;
    }
    memcpy(cur, t->rgba, (size_t)w * h * 4);
    {
        int q, n = w * h, hard = 0, on = 0;

        for (q = 0; q < n; q++) {
            unsigned a = t->rgba[(size_t)q * 4 + 3];

            hard += a == 0u || a >= 248u;
            on += a >= 128u;
        }
        cut = hard * 10 >= n * 9 && on > 0 && on < n;
        cov = (float)on / (float)(n > 0 ? n : 1);
    }
    for (;;) {
        int nw, nh, x, y;
        unsigned char *nx;

        glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, cur);
        if (w == 1 && h == 1) {
            break;
        }
        nw = w > 1 ? w / 2 : 1;
        nh = h > 1 ? h / 2 : 1;
        nx = (unsigned char *)malloc((size_t)nw * nh * 4);
        if (nx == NULL) {
            break;
        }
        for (y = 0; y < nh; y++) {
            for (x = 0; x < nw; x++) {
                unsigned sum[4] = { 0, 0, 0, 0 }, wa = 0, n = 0;
                int dx, dy, k;

                for (dy = 0; dy < 2; dy++) {
                    for (dx = 0; dx < 2; dx++) {
                        int sx = x * 2 + dx, sy = y * 2 + dy;
                        const unsigned char *p;

                        if (sx >= w) { sx = w - 1; }
                        if (sy >= h) { sy = h - 1; }
                        p = cur + ((size_t)sy * w + sx) * 4;
                        for (k = 0; k < 3; k++) {
                            sum[k] += (unsigned)p[k] * p[3];
                        }
                        sum[3] += p[3];
                        wa += p[3];
                        n++;
                    }
                }
                for (k = 0; k < 3; k++) {
                    nx[((size_t)y * nw + x) * 4 + k] =
                        wa > 0 ? (unsigned char)(sum[k] / wa) : 0;
                }
                nx[((size_t)y * nw + x) * 4 + 3] = (unsigned char)(sum[3] / n);
            }
        }
        if (cut) {
            /* keep the covered share of a cut-out texture: the alpha test
             * would otherwise eat the leaves at a distance */
            float lo = 1.0f, hi = 16.0f;
            int it, k2;

            for (it = 0; it < 12; it++) {
                float mid = 0.5f * (lo + hi);
                unsigned cnt = 0;

                for (k2 = 0; k2 < nw * nh; k2++) {
                    cnt += (float)nx[(size_t)k2 * 4 + 3] * mid >= 128.0f;
                }
                if ((float)cnt / (float)(nw * nh) < cov) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            for (k2 = 0; k2 < nw * nh; k2++) {
                float a2 = (float)nx[(size_t)k2 * 4 + 3] * hi;

                nx[(size_t)k2 * 4 + 3] = (unsigned char)(a2 > 255.0f ? 255.0f : a2);
            }
        }
        free(cur);
        cur = nx;
        w = nw;
        h = nh;
        lvl++;
    }
    free(cur);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lvl);
}

static void backdrop_upload(EidosGl *g, const BmapScene *sc)
{
    unsigned i;

    backdrop_free_scene();
    g_bd.scene = sc;
    if (sc->nt > 0) {
        g_bd.tex = (GLuint *)calloc(sc->nt, sizeof(GLuint));
        if (g_bd.tex == NULL) {
            g_bd.scene = NULL;
            return;
        }
        g_bd.n_tex = sc->nt;
        glGenTextures((GLsizei)sc->nt, g_bd.tex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (i = 0; i < sc->nt; i++) {
        const BmapTexture *t = &sc->t[i];

        glBindTexture(GL_TEXTURE_2D, g_bd.tex[i]);
        backdrop_mips(t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        !t->repeat_s ? GL_CLAMP_TO_EDGE
                        : t->flip_s ? GL_MIRRORED_REPEAT : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        !t->repeat_t ? GL_CLAMP_TO_EDGE
                        : t->flip_t ? GL_MIRRORED_REPEAT : GL_REPEAT);
    }
    {
        /* the half-width of the placed map around the player tile */
        float x0 = 0.0f, x1 = 0.0f, z0 = 7.0f, z1 = 7.0f;
        unsigned n;

        for (n = 0; n < sc->nv; n++) {
            const BmapVertex *v = &sc->v[n];

            x0 = v->x < x0 ? v->x : x0;
            x1 = v->x > x1 ? v->x : x1;
            z0 = v->z < z0 ? v->z : z0;
            z1 = v->z > z1 ? v->z : z1;
        }
        g_bd.reach = fminf(fminf(-x0, x1), fminf(7.0f - z0, z1 - 7.0f));
    }
    backdrop_pieces(sc);
    g->glBindVertexArray(g_bd.vao);
    g->glBindBuffer(GL_ARRAY_BUFFER, g_bd.vbo);
    g->glBufferData(GL_ARRAY_BUFFER,
                    (GLsizeiptr)((size_t)sc->nv * sizeof(BmapVertex)), sc->v,
                    GL_STATIC_DRAW);
    g->glEnableVertexAttribArray(0);
    g->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BmapVertex),
                             (void *)offsetof(BmapVertex, x));
    g->glEnableVertexAttribArray(1);
    g->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BmapVertex),
                             (void *)offsetof(BmapVertex, u));
    g->glEnableVertexAttribArray(2);
    g->glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                             sizeof(BmapVertex),
                             (void *)offsetof(BmapVertex, r));
}

static void vcross(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float vdot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vnorm(float *v)
{
    float l = sqrtf(vdot(v, v));

    if (l > 1e-6f) {
        v[0] /= l; v[1] /= l; v[2] /= l;
    }
}

/* clip = the game's perspective (NNS_G3dGlbPerspectiveW) times its look-at,
 * x and y scaled by the view scale as the geometry engine does.  Row i gives
 * clip component i from (x, y, z, 1). */
static void backdrop_matrix(const G3dBackdrop *b, float r[4][4])
{
    float f[3], rt[3], u[3], t, m0, m5, a, c, ex, ey, ef, kx, ky;
    fx32 kxf, kyf;
    int i;

    g3d_mtx_view_scale(&kxf, &kyf);
    kx = (float)kxf / 4096.0f;
    ky = (float)kyf / 4096.0f;
    for (i = 0; i < 3; i++) {
        f[i] = b->target[i] - b->eye[i];
    }
    vnorm(f);
    vcross(f, b->up, rt);
    vnorm(rt);
    vcross(rt, f, u);
    t = b->fovy_sin / b->fovy_cos;
    m5 = 1.0f / t;
    m0 = m5 / b->aspect;
    a = -(b->zfar + b->znear) / (b->zfar - b->znear);
    c = -2.0f * b->zfar * b->znear / (b->zfar - b->znear);
    ex = vdot(rt, b->eye);
    ey = vdot(u, b->eye);
    ef = vdot(f, b->eye);
    for (i = 0; i < 3; i++) {
        r[0][i] = m0 * kx * rt[i];
        r[1][i] = m5 * ky * u[i];
        r[2][i] = -a * f[i];
        r[3][i] = f[i];
    }
    r[0][3] = -m0 * kx * ex;
    r[1][3] = -m5 * ky * ey;
    r[2][3] = a * ef + c;
    r[3][3] = -ef;
}

/* Draw the scene.  The frame's framebuffer is bound and cleared. */
/* front: the second pass, after the game's polygons.  The game draws its
 * sprites without the map's depth, so the map would never hide a Pokemon;
 * this pass draws the map again where it stands between the camera and a
 * Pokemon (the shader keeps those parts only).  The depth is cleared over
 * the Pokemon's boxes first, so the parts drawn again hide one another in
 * the right order (a hedge before the ground behind it). */
static void front_depth_clear(const G3dBackdrop *b, const float r[4][4])
{
    GLint vp[4];
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    unsigned i, c;

    glGetIntegerv(GL_VIEWPORT, vp);
    for (i = 0; i < b->n_poke && i < G3D_BACKDROP_SPRITES; i++) {
        float d[3], L, n[3], side[3];

        for (c = 0; c < 3; c++) {
            d[c] = b->poke[i][c] - b->eye[c];
        }
        L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L < 1e-3f) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            n[c] = d[c] / L;
        }
        side[0] = -n[2];
        side[1] = 0.0f;
        side[2] = n[0];
        {
            float sl = sqrtf(side[0] * side[0] + side[2] * side[2]);

            if (sl < 1e-3f) {
                continue;
            }
            side[0] /= sl;
            side[2] /= sl;
        }
        for (c = 0; c < 4; c++) {
            float p[3], cl[4];
            int k;
            float hgt = fabsf(b->poke[i][3]);
            float sx = (c & 1) ? 0.47f * hgt : -0.47f * hgt, uy = (c & 2) ? hgt : -1.0f;

            p[0] = b->poke[i][0] + side[0] * sx;
            p[1] = b->poke[i][1] + uy;
            p[2] = b->poke[i][2] + side[2] * sx;
            for (k = 0; k < 4; k++) {
                cl[k] = r[k][0] * p[0] + r[k][1] * p[1] + r[k][2] * p[2] + r[k][3];
            }
            if (cl[3] < 0.05f) {
                x0 = y0 = -1e9f;            /* behind the camera: the whole frame */
                x1 = y1 = 1e9f;
                continue;
            }
            x0 = fminf(x0, cl[0] / cl[3]);
            x1 = fmaxf(x1, cl[0] / cl[3]);
            y0 = fminf(y0, cl[1] / cl[3]);
            y1 = fmaxf(y1, cl[1] / cl[3]);
        }
    }
    if (x1 < x0) {
        return;
    }
    x0 = fmaxf(x0, -1.0f);
    y0 = fmaxf(y0, -1.0f);
    x1 = fminf(x1, 1.0f);
    y1 = fminf(y1, 1.0f);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp[0] + (GLint)floorf((x0 * 0.5f + 0.5f) * (float)vp[2]),
              vp[1] + (GLint)floorf((y0 * 0.5f + 0.5f) * (float)vp[3]),
              (GLsizei)ceilf((x1 - x0) * 0.5f * (float)vp[2]) + 2,
              (GLsizei)ceilf((y1 - y0) * 0.5f * (float)vp[3]) + 2);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

static void backdrop_draw(EidosGl *g, const G3dBackdrop *b, int front)
{
    const BmapScene *sc = (const BmapScene *)b->scene;
    float r[4][4];
    unsigned pass, i;
    /* the field plays its animations a frame a field frame (60 a second) */
    unsigned uv_frame = (unsigned)(SDL_GetTicks() * 60u / 1000u);
    int uv_set = 1;

    if (g_bd.prog == 0 || sc == NULL || sc->nv == 0) {
        return;
    }
    if (g_bd.scene != sc || g_bd.gen != b->scene_gen) {
        backdrop_upload(g, sc);
        g_bd.gen = b->scene_gen;
        if (g_bd.scene == NULL) {
            return;
        }
    }
    backdrop_matrix(b, r);
    g->glUseProgram(g_bd.prog);
    g->glBindVertexArray(g_bd.vao);
    g->glBindBuffer(GL_ARRAY_BUFFER, g_bd.vbo);
    for (i = 0; i < 4; i++) {
        g->glUniform4f(g_bd.u_r[i], r[i][0], r[i][1], r[i][2], r[i][3]);
    }
    {
        float at[4] = { 0.0f, 7.0f, 0.0f, 0.0f };

        if (g_bd.reach > 40.0f) {
            at[2] = 0.45f * g_bd.reach;
            at[3] = 0.97f * g_bd.reach;
        }
        g->glUniform4f(g_bd.u_atm, at[0], at[1], at[2], at[3]);
        float sk[3];

        sk[0] = (float)b->sky[0] / 255.0f;
        sk[1] = (float)b->sky[1] / 255.0f;
        sk[2] = (float)b->sky[2] / 255.0f;
        g->glUniform3fv(g_bd.u_sky, 1, sk);
    }
    g->glUniform3fv(g_bd.u_eye, 1, b->eye);
    g->glUniform1i(g_bd.u_front, front);
    g->glUniform1i(g_bd.u_npoke, (GLint)(b->n_poke > G3D_BACKDROP_SPRITES ? G3D_BACKDROP_SPRITES : b->n_poke));
    g->glUniform4fv(g_bd.u_poke, G3D_BACKDROP_SPRITES, &b->poke[0][0]);
    g->glUniform3fv(g_bd.u_foc, 1, b->target);
    {
        /* the zone's fog: linear in the distance from the player's tile
         * (battle x 0, z 7), scaled from map units; .z = start, .w = span */
        float fg[4] = { 0.0f, 7.0f, 0.0f, 1.0f };
        float fc[3] = { 0.0f, 0.0f, 0.0f };

        if (sc->fog_on && sc->fog_end > sc->fog_start) {
            fg[2] = sc->fog_start * (sc->place_k > 0.0f ? sc->place_k : BMAP_PLACE_K) + 1e-3f;
            fg[3] = (sc->fog_end - sc->fog_start) * (sc->place_k > 0.0f ? sc->place_k : BMAP_PLACE_K);
            fc[0] = (float)sc->fog[0] / 255.0f;
            fc[1] = (float)sc->fog[1] / 255.0f;
            fc[2] = (float)sc->fog[2] / 255.0f;
        }
        g->glUniform4f(g_bd.u_fog, fg[0], fg[1], fg[2], fg[3]);
        g->glUniform3fv(g_bd.u_fogc, 1, fc);
    }
    g->glActiveTexture(GL_TEXTURE0);
    g->glUniform1i(g_bd.u_texture, 0);
    glDisable(GL_STENCIL_TEST);
    if (front) {
        front_depth_clear(b, (const float (*)[4])r);
        if (!b->front_only) {
            /* only where the game drew (its sprites, their shadows and
             * effects): the map in front of a sprite covers the sprite and
             * nothing else, whatever the sprite's size */
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, STENCIL_GAME, STENCIL_GAME);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 1.0f); /* no slope term: it pushed far, grazing triangles over the near map */
    g->glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            glEnable(GL_BLEND);
            g->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                   GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        unsigned n = pass == 1 && g_bd.n_piece > 0 ? g_bd.n_piece : sc->nd;
        int sorted = pass == 1 && g_bd.n_piece > 0;

        if (sorted && !(g_bd.idx_ok && g_bd.idx_eye[0] == b->eye[0] &&
                        g_bd.idx_eye[1] == b->eye[1] && g_bd.idx_eye[2] == b->eye[2])) {
            /* the see-through parts far to near from this camera, as one
             * index buffer (the back and the front pass share it) */
            for (i = 0; i < g_bd.n_piece; i++) {
                const float *m = g_bd.piece[i].mid;
                float dx = m[0] - b->eye[0], dy = m[1] - b->eye[1], dz = m[2] - b->eye[2];

                g_bd.order[i] = i;
                g_bd.dist[i] = dx * dx + dy * dy + dz * dz;
            }
            qsort(g_bd.order, g_bd.n_piece, sizeof *g_bd.order, piece_far_first);
            g_bd.n_idx = 0;
            for (i = 0; i < g_bd.n_piece; i++) {
                const BdPiece *pc = &g_bd.piece[g_bd.order[i]];
                unsigned k;

                g_bd.at[i] = g_bd.n_idx;
                for (k = 0; k < pc->count; k++) {
                    g_bd.idx[g_bd.n_idx++] = pc->first + k;
                }
            }
            g->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_bd.ibo);
            g->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                            (GLsizeiptr)((size_t)g_bd.n_idx * sizeof *g_bd.idx),
                            g_bd.idx, GL_STREAM_DRAW);
            memcpy(g_bd.idx_eye, b->eye, sizeof g_bd.idx_eye);
            g_bd.idx_ok = 1;
        }
        for (i = 0; i < n; i++) {
            const BmapDraw *d;
            unsigned first, count, run = 1;

            if (sorted) {
                const BdPiece *pc = &g_bd.piece[g_bd.order[i]];

                d = &sc->d[pc->draw];
                first = pc->first;
                count = pc->count;
                /* the next triangles in the order that come from the same
                 * draw (the same texture and state): one call for all */
                while (i + run < n && g_bd.piece[g_bd.order[i + run]].draw == pc->draw) {
                    run++;
                }
            } else {
                d = &sc->d[i];
                first = d->first;
                count = d->count;
            }
            /* the depth pass also takes the solid texels of a draw whose
             * soft edges blend later */
            if (((d->translucent != 0) != (pass == 1)
                 && !(pass == 0 && d->translucent && BD_SOFT_ONLY(d)))
                || count == 0 || d->alpha == 0 || first + count > sc->nv) {
                i += run - 1;
                continue;
            }
            if (d->cull == 0) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(d->cull == 1 ? GL_BACK : GL_FRONT);
            }
            if (d->tex >= 0 && (unsigned)d->tex < g_bd.n_tex) {
                glBindTexture(GL_TEXTURE_2D, g_bd.tex[d->tex]);
                g->glUniform1i(g_bd.u_tex, 1);
            } else {
                g->glUniform1i(g_bd.u_tex, 0);
            }
            if (d->uvanim != 0 && d->uvanim <= sc->nuva && sc->uva[d->uvanim - 1].frames > 0) {
                const BmapUvAnim *ua = &sc->uva[d->uvanim - 1];
                const float *m = ua->m + (size_t)(uv_frame % ua->frames) * 6u;

                g->glUniform3fv(g_bd.u_uv0, 1, m);
                g->glUniform3fv(g_bd.u_uv1, 1, m + 3);
                uv_set = 1;
            } else if (uv_set) {
                static const float id[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

                g->glUniform3fv(g_bd.u_uv0, 1, id);
                g->glUniform3fv(g_bd.u_uv1, 1, id + 3);
                uv_set = 0;
            }
            g->glUniform1i(g_bd.u_bb, d->billboard);
            g->glUniform1i(g_bd.u_grass, d->grass);
            g->glUniform3fv(g_bd.u_pivot, 1, d->pivot);
            g->glUniform1f(g_bd.u_alpha, (float)d->alpha / 31.0f);
            if (pass == 0) {
                g->glUniform1f(g_bd.u_cut, d->translucent ? BD_CORE_CUT : 0.5f);
                g->glUniform1f(g_bd.u_cut_hi, 2.0f);
            } else {
                g->glUniform1f(g_bd.u_cut, 0.02f);
                g->glUniform1f(g_bd.u_cut_hi, BD_SOFT_ONLY(d) ? BD_CORE_CUT : 2.0f);
            }
            if (sorted) {
                unsigned end = i + run < g_bd.n_piece ? g_bd.at[i + run] : g_bd.n_idx;

                glDrawElements(GL_TRIANGLES, (GLsizei)(end - g_bd.at[i]), GL_UNSIGNED_INT,
                               (const void *)((size_t)g_bd.at[i] * sizeof(GLuint)));
                i += run - 1;
            } else {
                glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
            }
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    g->glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static u8 c5to8(u32 v)
{
    v &= 31u;
    return (u8)((v << 3) | (v >> 2));
}

int eidos_gl3d_init(EidosGl *g)
{
    g->p3d = eidos_gl_compile_program(g, k_vs3, k_fs3);
    if (g->p3d == 0) {
        return -1;
    }
#define LOC(field, name) g->field = g->glGetUniformLocation(g->p3d, name)
    LOC(u_mode, "u_mode");
    LOC(u_alpha5, "u_alpha5");
    LOC(u_tex3, "u_tex");
    LOC(u_texture, "u_texture");
    LOC(u_texsize3, "u_texsize");
    LOC(u_wrap, "u_wrap");
    LOC(u_alpharef, "u_alpharef");
    LOC(u_toon, "u_toon");
    LOC(u_highlight, "u_highlight");
    LOC(u_toontab, "u_toontab");
    LOC(u_front, "u_front");
    LOC(u_back, "u_back");
    LOC(u_wbuf, "u_wbuf");
    LOC(u_pass, "u_pass");
    LOC(u_id, "u_id");
#undef LOC
    /* Edge marking is optional: without it, frames that use it stay on the
     * CPU rasteriser (g3d_hw_gpu_features). */
    g->pedge = eidos_gl_compile_program(g, k_vse, k_fse);
    if (g->pedge != 0) {
        g->ue_col = g->glGetUniformLocation(g->pedge, "u_col");
        g->ue_id = g->glGetUniformLocation(g->pedge, "u_id");
        g->ue_dep = g->glGetUniformLocation(g->pedge, "u_dep");
        g->ue_step = g->glGetUniformLocation(g->pedge, "u_step");
        g->ue_cid = g->glGetUniformLocation(g->pedge, "u_cid");
        g->ue_cdep = g->glGetUniformLocation(g->pedge, "u_cdep");
        g->ue_edge = g->glGetUniformLocation(g->pedge, "u_edge");
    }
    /* Fog likewise (the fog pass reads the same three targets). */
    g->pfog = eidos_gl_compile_program(g, k_vse, k_fsf);
    if (g->pfog != 0) {
        g->uf_col = g->glGetUniformLocation(g->pfog, "u_col");
        g->uf_id = g->glGetUniformLocation(g->pfog, "u_id");
        g->uf_dep = g->glGetUniformLocation(g->pfog, "u_dep");
        g->uf_fog = g->glGetUniformLocation(g->pfog, "u_fog");
        g->uf_table = g->glGetUniformLocation(g->pfog, "u_table");
        g->uf_offset = g->glGetUniformLocation(g->pfog, "u_offset");
        g->uf_step = g->glGetUniformLocation(g->pfog, "u_step");
        g->uf_aonly = g->glGetUniformLocation(g->pfog, "u_aonly");
        g->uf_clear = g->glGetUniformLocation(g->pfog, "u_clear");
    }
    /* The battle backdrop is optional too: without its program the game's
     * own scenery is not drawn either and the frame has only the sky
     * colour. */
    (void)backdrop_init(g);
    g->glGenFramebuffers(1, &g->fb[0].fbo);
    g->glGenFramebuffers(1, &g->fb[1].fbo);
    g->glGenFramebuffers(1, &g->fb[2].fbo);
    glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &g->integer_samples);
    g->glGenVertexArrays(1, &g->vao3);
    g->glGenBuffers(1, &g->vbo3);
    g->have3d = 1;
    if (glGetError() != GL_NO_ERROR) {
        return -1;
    }
    g3d_hw_gpu_features((g->pedge != 0 ? G3D_SNAP_EDGE : 0u)
                        | (g->pfog != 0 ? G3D_SNAP_FOG : 0u));
    return 0;
}

void eidos_gl3d_destroy(EidosGl *g)
{
    unsigned i;

    for (i = 0; i < 3; i++) {
        GlFb *f = &g->fb[i];

        if (f->fbo != 0) {
            g->glDeleteFramebuffers(1, &f->fbo);
        }
        if (f->depth != 0) {
            glDeleteTextures(1, &f->depth);
        }
        if (f->color != 0) {
            glDeleteTextures(1, &f->color);
            glDeleteTextures(1, &f->pid);
            glDeleteTextures(1, &f->color2);
        }
        if (f->post_fbo != 0) {
            g->glDeleteFramebuffers(1, &f->post_fbo);
        }
        if (f->ms_fbo != 0) {
            g->glDeleteFramebuffers(1, &f->ms_fbo);
            g->glDeleteRenderbuffers(1, &f->ms_color);
            g->glDeleteRenderbuffers(1, &f->ms_depth);
            g->glDeleteRenderbuffers(1, &f->ms_id);
        }
    }
    if (g->vbo3 != 0) {
        g->glDeleteBuffers(1, &g->vbo3);
    }
    if (g->vao3 != 0) {
        g->glDeleteVertexArrays(1, &g->vao3);
    }
    if (g->p3d != 0) {
        g->glDeleteProgram(g->p3d);
    }
    if (g->pedge != 0) {
        g->glDeleteProgram(g->pedge);
    }
    if (g->pfog != 0) {
        g->glDeleteProgram(g->pfog);
    }
    backdrop_destroy(g);
    g3d_hw_gpu_features(0u);
    for (i = 0; i < g->n_gtex; i++) {
        glDeleteTextures(1, &g->gtex[i]);
    }
    free(g->gtex);
    free(g->gtex_key);
    free(g->vtx);
}

static void tex_params(GLint filter)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* Attach `color`, the ID buffer and the depth to the frame's FBO, and
 * `color2` to the edge pass's.  The two colour textures swap after an
 * edge pass, so this runs again then. */
static int fbo_attach(EidosGl *g, GlFb *f)
{
    static const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

    g->glBindFramebuffer(GL_FRAMEBUFFER, f->fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, f->color, 0);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                              GL_TEXTURE_2D, f->pid, 0);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_TEXTURE_2D, f->depth, 0);
    g->glDrawBuffers(2, bufs);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return -1;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->post_fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, f->color2, 0);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return -1;
    }
    return 0;
}

static int fbo_resize(EidosGl *g, GlFb *f, int w, int h)
{
    if (f->w == w && f->h == h && f->color != 0) {
        return 0;
    }
    if (f->color == 0) {
        glGenTextures(1, &f->color);
        glGenTextures(1, &f->color2);
        glGenTextures(1, &f->pid);
        glGenTextures(1, &f->depth);
        g->glGenFramebuffers(1, &f->post_fbo);
    }
    glBindTexture(GL_TEXTURE_2D, f->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    tex_params(GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, f->color2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    tex_params(GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, f->pid);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_BYTE, NULL);
    tex_params(GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, f->depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    tex_params(GL_NEAREST);
    if (fbo_attach(g, f) != 0) {
        return -1;
    }
    f->w = w;
    f->h = h;
    return 0;
}

/* The edge-marking pass: the frame's colour, ID and depth in, the marked
 * frame out into color2, which then becomes the frame's colour. */
static void edge_pass(EidosGl *g, GlFb *f, const G3dSnapshot *s, int w,
                      double clear_depth)
{
    GLfloat edge[24];
    GLuint t;
    int k, step = w / 256;

    for (k = 0; k < 8; k++) {
        u32 c = s->edge[k];

        edge[k * 3 + 0] = (float)c5to8(c) / 255.f;
        edge[k * 3 + 1] = (float)c5to8(c >> 5) / 255.f;
        edge[k * 3 + 2] = (float)c5to8(c >> 10) / 255.f;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->post_fbo);
    glViewport(0, 0, f->w, f->h);
    g->glUseProgram(g->pedge);
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f->color);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, f->pid);
    g->glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, f->depth);
    g->glUniform1i(g->ue_col, 0);
    g->glUniform1i(g->ue_id, 1);
    g->glUniform1i(g->ue_dep, 2);
    g->glUniform1i(g->ue_step, step < 1 ? 1 : step);
    g->glUniform1ui(g->ue_cid, (GLuint)((s->clear_color >> 24) & 0x3Fu));
    g->glUniform1f(g->ue_cdep, (GLfloat)clear_depth);
    g->glUniform3fv(g->ue_edge, 8, edge);
    g->glBindVertexArray(g->vao3);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, 0);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    g->glActiveTexture(GL_TEXTURE0);
    t = f->color;
    f->color = f->color2;
    f->color2 = t;
    (void)fbo_attach(g, f);
}

/* The fog pass: the frame's colour, ID and depth in, the fogged frame out
 * into color2, which then becomes the frame's colour (k_fsf). */
static void fog_pass(EidosGl *g, GlFb *f, const G3dSnapshot *s)
{
    GLfloat table[32];
    GLuint t;
    u32 fc = s->fog_color, cc = s->clear_color;
    u32 shift = (s->disp3dcnt >> 8) & 0xFu;
    int k;

    for (k = 0; k < 32; k++) {
        table[k] = (GLfloat)s->fog_table[k];
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->post_fbo);
    glViewport(0, 0, f->w, f->h);
    g->glUseProgram(g->pfog);
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f->color);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, f->pid);
    g->glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, f->depth);
    g->glUniform1i(g->uf_col, 0);
    g->glUniform1i(g->uf_id, 1);
    g->glUniform1i(g->uf_dep, 2);
    g->glUniform4f(g->uf_fog, (float)c5to8(fc) / 255.f,
                   (float)c5to8(fc >> 5) / 255.f, (float)c5to8(fc >> 10) / 255.f,
                   (float)((fc >> 16) & 0x1Fu) / 31.f);
    g->glUniform1fv(g->uf_table, 32, table);
    g->glUniform1f(g->uf_offset, (GLfloat)(s->fog_offset & 0x7FFFu));
    g->glUniform1f(g->uf_step, shift > 10u ? 0.f : (GLfloat)(0x400u >> shift));
    g->glUniform1i(g->uf_aonly, (s->disp3dcnt & 0x40u) != 0);
    {
        GLfloat clear[3];

        clear[0] = (float)c5to8(cc) / 255.f;
        clear[1] = (float)c5to8(cc >> 5) / 255.f;
        clear[2] = (float)c5to8(cc >> 10) / 255.f;
        g->glUniform3fv(g->uf_clear, 1, clear);
    }
    g->glBindVertexArray(g->vao3);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, 0);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    g->glActiveTexture(GL_TEXTURE0);
    t = f->color;
    f->color = f->color2;
    f->color2 = t;
    (void)fbo_attach(g, f);
}

/*
 * DISP3DCNT bit 4 turns on the hardware's edge anti-aliasing.  The GPU's
 * equivalent is multisampling: render into a 4-sample target and resolve it
 * into the frame's texture.  Not worth its memory at a large size, where the
 * resolution does the same job.
 */
#define MSAA_SAMPLES 4
#define MSAA_MAX_PIXELS (2048 * 1536)

static int msaa_target(EidosGl *g, GlFb *f, int w, int h)
{
    /* the ID buffer is an integer target: a driver whose integer formats
     * take fewer samples (macOS: GL_MAX_INTEGER_SAMPLES is 1) rejects its
     * storage with GL_INVALID_OPERATION, and that error failed the frame */
    static GLint samples = -1;

    if (samples < 0) {
        GLint any = 0, integer = 0;

        glGetIntegerv(GL_MAX_SAMPLES, &any);
        glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &integer);
        samples = any < integer ? any : integer;
    }
    if (samples < MSAA_SAMPLES || (long)w * (long)h > MSAA_MAX_PIXELS) {
        return 0;
    }
    if (f->ms_fbo == 0) {
        g->glGenFramebuffers(1, &f->ms_fbo);
        g->glGenRenderbuffers(1, &f->ms_color);
        g->glGenRenderbuffers(1, &f->ms_depth);
        g->glGenRenderbuffers(1, &f->ms_id);
    }
    if (f->ms_w != w || f->ms_h != h) {
        g->glBindRenderbuffer(GL_RENDERBUFFER, f->ms_color);
        g->glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_RGBA8, w, h);
        g->glBindRenderbuffer(GL_RENDERBUFFER, f->ms_depth);
        g->glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_DEPTH24_STENCIL8, w, h);
        /* the ID buffer too, so that edge marking and fog, which read it,
         * can run on an anti-aliased frame (resolved from sample 0) */
        g->glBindRenderbuffer(GL_RENDERBUFFER, f->ms_id);
        g->glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_R8UI, w, h);
        g->glBindFramebuffer(GL_FRAMEBUFFER, f->ms_fbo);
        g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, f->ms_color);
        g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, f->ms_id);
        g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, f->ms_depth);
        {
            static const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

            g->glDrawBuffers(2, both);
        }
        f->ms_w = w;
        f->ms_h = h;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->ms_fbo);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        f->ms_w = 0;
        return 0;
    }
    return 1;
}

#define ATTR_DEFAULT (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))

/* What a snapshot texture holds, for the upload cache: FNV-1a over its size
 * and texels (0 is kept for "nothing uploaded"). */
static unsigned tex_key(const G3dSnapTex *t)
{
    unsigned h = 2166136261u;
    size_t i, n = (size_t)t->w * t->h * 4u;
    const u32 *w = (const u32 *)t->rgba;

    h = (h ^ t->w) * 16777619u;
    h = (h ^ t->h) * 16777619u;
    if (t->content != 0) {
        /* the snapshot knows what the texels came from: no need to read them */
        h = (h ^ (unsigned)t->content) * 16777619u;
        h = (h ^ (unsigned)(t->content >> 32)) * 16777619u;
        return h != 0 ? h : 1u;
    }
    for (i = 0; t->rgba != NULL && i < n / 4u; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h != 0 ? h : 1u;
}

/* Render the snapshot at w x h into the framebuffer; the colour texture is
 * then eidos_gl_3d_texture(g).  Returns 0, or -1 if the GPU path cannot
 * draw this frame. */
/* One run of polygons that share their state (render_into). */
typedef struct Gl3dRun {
    int deq, mask, xlu_depth, mode, toon, a5, front, back;
    int textured, slot, wrap, id, two_pass;
    int fog;           /* POLYGON_ATTR bit 15: ID buffer bit 6           */
} Gl3dRun;

static void draw_run(EidosGl *g, const G3dSnapshot *s, const Gl3dRun *d,
                     unsigned first, unsigned verts, int *prev_depth_eq,
                     int *prev_mask, s32 *bound_tex)
{
    GLint xid = (GLint)(0x40 | d->id);
    GLint uid = (GLint)(0x80 | (d->fog ? 0x40 : 0) | d->id);

    if (d->deq != *prev_depth_eq) {
        glDepthFunc(d->deq ? GL_LEQUAL : GL_LESS);
        *prev_depth_eq = d->deq;
    }
    if (d->mask != *prev_mask) {
        glDepthMask(d->mask ? GL_TRUE : GL_FALSE);
        *prev_mask = d->mask;
    }
    g->glUniform1i(g->u_mode, (GLint)d->mode);
    g->glUniform1i(g->u_toon, (GLint)d->toon);
    g->glUniform1i(g->u_alpha5, (GLint)d->a5);
    g->glUniform1i(g->u_front, (GLint)d->front);
    g->glUniform1i(g->u_back, (GLint)d->back);
    if (d->textured) {
        if (*bound_tex != d->slot) {
            glBindTexture(GL_TEXTURE_2D, g->gtex[d->slot]);
            *bound_tex = d->slot;
        }
        g->glUniform1i(g->u_tex3, 1);
        g->glUniform2i(g->u_texsize3, (GLint)s->tex[d->slot].w,
                       (GLint)s->tex[d->slot].h);
        g->glUniform4i(g->u_wrap, (GLint)(d->wrap & 1), (GLint)((d->wrap >> 1) & 1),
                       (GLint)((d->wrap >> 2) & 1), (GLint)((d->wrap >> 3) & 1));
    } else {
        g->glUniform1i(g->u_tex3, 0);
    }
    g->glUniform1i(g->u_id, uid);
    if (d->two_pass) {
        /* An opaque polygon whose texture has partly transparent
         * texels: the hardware decides per pixel from the final alpha,
         * so the opaque texels write depth and the translucent ones
         * do not (unless bit 11 says so).  One draw cannot do both:
         * two passes can. */
        glDepthMask(GL_TRUE);
        glStencilFunc(GL_ALWAYS, STENCIL_GAME, 0xFFu);
        g->glUniform1i(g->u_pass, 1);
        g->glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
        glDepthMask(d->xlu_depth ? GL_TRUE : GL_FALSE);
        glStencilFunc(GL_NOTEQUAL, xid | STENCIL_GAME, STENCIL_IDS);
        g->glUniform1i(g->u_pass, 2);
        /* a translucent pixel keeps the ID beneath it */
        g->glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
        g->glUniform1i(g->u_pass, 0);
        *prev_mask = d->xlu_depth;
    } else {
        if (d->a5 >= 31) {
            glStencilFunc(GL_ALWAYS, STENCIL_GAME, 0xFFu);
        } else {
            glStencilFunc(GL_NOTEQUAL, xid | STENCIL_GAME, STENCIL_IDS);
        }
        g->glColorMaski(1, d->a5 >= 31, d->a5 >= 31, d->a5 >= 31, d->a5 >= 31);
        glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
    }
}

static int render_into(EidosGl *g, GlFb *fb, const G3dSnapshot *s, int w, int h)
{
    unsigned i, nv = 0;
    size_t need;
    u32 cc, alpha;
    u8 cr, cg, cb;
    s32 bound_tex = -2;
    int prev_depth_eq = -1, prev_mask = -1;
    GLfloat toon[96];
    unsigned long long t_up, t_dr;
    int use_ms, edges, fog, backdrop;
    double cdepth;

    if (g == NULL || !g->have3d || s == NULL || w < 1 || h < 1) {
        return -1;
    }
    backdrop = s->backdrop.valid && g_bd.prog != 0 && !s->wbuffer;
    if (fbo_resize(g, fb, w, h) != 0) {
        return -1;
    }

    /* textures: one GL texture per snapshot texture */
    while (g->n_gtex < s->n_tex) {
        if (g->n_gtex == g->cap_gtex) {
            unsigned c = g->cap_gtex ? g->cap_gtex * 2u : 16u;
            GLuint *n = (GLuint *)realloc(g->gtex, (size_t)c * sizeof(*n));
            unsigned *k;

            if (n == NULL) {
                return -1;
            }
            g->gtex = n;
            k = (unsigned *)realloc(g->gtex_key, (size_t)c * sizeof(*k));
            if (k == NULL) {
                return -1;
            }
            g->gtex_key = k;
            g->cap_gtex = c;
        }
        g->gtex_key[g->n_gtex] = 0;
        glGenTextures(1, &g->gtex[g->n_gtex]);
        glBindTexture(GL_TEXTURE_2D, g->gtex[g->n_gtex]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g->n_gtex++;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    t_up = eidos_prof_begin();
    /* Upload only what changed: the same snapshot is drawn twice in a frame
     * (the window's size and the DS size), and most textures stay from one
     * frame to the next.  Re-specifying a texture the GPU is still drawing
     * from can make the driver wait for that draw. */
    if (!g->tex_serial_valid || g->tex_serial != s->serial) {
        for (i = 0; i < s->n_tex; i++) {
            unsigned key = tex_key(&s->tex[i]);

            if (g->gtex_key[i] == key && key != 0) {
                continue;
            }
            g->gtex_key[i] = key;
            glBindTexture(GL_TEXTURE_2D, g->gtex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->tex[i].w,
                         (GLsizei)s->tex[i].h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         s->tex[i].rgba);
        }
        g->tex_serial = s->serial;
        g->tex_serial_valid = 1;
    }

    eidos_prof_end(EIDOS_PROF_GLUP, t_up);
    t_dr = eidos_prof_begin();
    /* vertices: each polygon as a fan of triangles */
    need = (size_t)s->n_polys * 6u * FLOATS_PER_VERTEX;
    if (need > g->cap_vtx) {
        float *n = (float *)realloc(g->vtx, need * sizeof(float));

        if (n == NULL) {
            return -1;
        }
        g->vtx = n;
        g->cap_vtx = need;
    }
    for (i = 0; i < s->n_polys; i++) {
        const G3dPoly *p = &s->polys[i];
        static const unsigned fan[6] = { 0, 1, 2, 0, 2, 3 };
        unsigned tri, k, ntri;

        if (p->count < 3u || p->count > 4u) {
            continue;
        }
        ntri = p->count - 2u;
        for (tri = 0; tri < ntri * 3u; tri++) {
            const G3dVertex *v = &s->verts[s->idx[p->first + fan[tri]]];
            float *o = g->vtx + (size_t)nv * FLOATS_PER_VERTEX;

            o[0] = (float)v->x; o[1] = (float)v->y;
            o[2] = (float)v->z; o[3] = (float)v->w;
            o[4] = (float)v->s; o[5] = (float)v->t;
            o[6] = (float)v->r; o[7] = (float)v->g; o[8] = (float)v->b;
            nv++;
            (void)k;
        }
    }

    /* Edge marking needs the ID buffer, which the multisampled target does
     * not carry; at a large size the resolution smooths the edges anyway. */
    edges = (s->disp3dcnt & 0x20u) != 0 && g->pedge != 0;
    fog = (s->disp3dcnt & 0x80u) != 0 && g->pfog != 0;
    use_ms = g->integer_samples >= MSAA_SAMPLES &&
             (s->disp3dcnt & 0x10u) != 0 && msaa_target(g, fb, w, h);
    if (!use_ms) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    }
    glViewport(0, 0, w, h);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* the clear plane: premultiplied colour, alpha as set */
    cc = s->clear_color;
    alpha = ((cc >> 16) & 0x1Fu) * 255u / 31u;
    cr = c5to8(cc);
    cg = c5to8(cc >> 5);
    cb = c5to8(cc >> 10);
    if (backdrop) {
        /* the sky beyond the map; opaque, the 2D layers are behind it */
        alpha = 255u;
        cr = s->backdrop.sky[0];
        cg = s->backdrop.sky[1];
        cb = s->backdrop.sky[2];
    }
    if (backdrop && s->backdrop.front_only) {
        alpha = 0u;                     /* only the front pass shows */
    }
    glClearColor((float)cr / 255.f * (float)alpha / 255.f,
                 (float)cg / 255.f * (float)alpha / 255.f,
                 (float)cb / 255.f * (float)alpha / 255.f,
                 (float)alpha / 255.f);
    cdepth = s->wbuffer ? (double)(s->clear_depth & 0x7FFFu) / 32768.0
                        : ((s->clear_depth & 0x7FFFu) == 0x7FFFu
                           ? 1.0
                           : (double)(s->clear_depth & 0x7FFFu) / 32767.0);
    glClearDepth(cdepth);
    glStencilMask(0xFFu);
    glClearStencil(0);
    {
        /* the ID buffer is an integer target: cleared on its own, to the
         * clear plane's polygon ID (multisampled or not) */
        GLfloat c4[4];
        GLuint id4[4] = { 0, 0, 0, 0 };

        c4[0] = (float)cr / 255.f * (float)alpha / 255.f;
        c4[1] = (float)cg / 255.f * (float)alpha / 255.f;
        c4[2] = (float)cb / 255.f * (float)alpha / 255.f;
        c4[3] = (float)alpha / 255.f;
        id4[0] = ((cc >> 24) & 0x3Fu) | ((cc & 0x8000u) ? 0x40u : 0u);
        g->glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g->glClearBufferfv(GL_COLOR, 0, c4);
        g->glClearBufferuiv(GL_COLOR, 1, id4);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    if (backdrop) {
        if (s->backdrop.front_only) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        }
        backdrop_draw(g, &s->backdrop, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    if (nv > 0) {
        unsigned drawn = 0, first = 0;

        g->glBindVertexArray(g->vao3);
        g->glBindBuffer(GL_ARRAY_BUFFER, g->vbo3);
        g->glBufferData(GL_ARRAY_BUFFER,
                        (GLsizeiptr)((size_t)nv * FLOATS_PER_VERTEX * sizeof(float)),
                        g->vtx, GL_STREAM_DRAW);
        g->glEnableVertexAttribArray(0);
        g->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float), (void *)0);
        g->glEnableVertexAttribArray(1);
        g->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float),
                                 (void *)(4 * sizeof(float)));
        g->glEnableVertexAttribArray(2);
        g->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float),
                                 (void *)(6 * sizeof(float)));

        g->glUseProgram(g->p3d);
        glEnable(GL_DEPTH_TEST);
        /* The stencil holds 0x40 | the polygon ID of the translucent pixel
         * last drawn (0: none, or an opaque pixel since): the hardware does
         * not draw a translucent pixel over one of the same ID. */
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glEnable(GL_BLEND);
        g->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                               GL_ONE_MINUS_SRC_ALPHA);
        for (i = 0; i < 96u; i++) {
            toon[i] = (float)s->toon[i];
        }
        g->glUniform3fv(g->u_toontab, 32, toon);
        g->glUniform1i(g->u_texture, 0);
        g->glActiveTexture(GL_TEXTURE0);
        g->glUniform1i(g->u_alpharef, (GLint)s->alpha_ref);
        g->glUniform1i(g->u_highlight, (GLint)s->highlight);
        g->glUniform1i(g->u_wbuf, (GLint)s->wbuffer);

        {
            /* Consecutive polygons with the same state are one draw: the
             * hardware's order is the order of the vertices inside a draw
             * as much as between draws (depth, blending and the stencil
             * test see the same sequence), so the result is the same with
             * a fraction of the calls.  A field frame has thousands of
             * polygons and a few dozen states. */
            Gl3dRun run, cur;
            unsigned run_first = 0, run_verts = 0;
            int have = 0;

            memset(&run, 0, sizeof(run));
            memset(&cur, 0, sizeof(cur));
            for (i = 0; i < s->n_polys; i++) {
                const G3dPoly *p = &s->polys[i];
                u32 attr = p->attr, mode, a5, ti;
                u32 verts;
                s32 slot = s->poly_tex[i];

                if (p->count < 3u || p->count > 4u) {
                    continue;
                }
                verts = (p->count - 2u) * 3u;
                if (attr == 0) {
                    attr = ATTR_DEFAULT;
                }
                mode = G3D_ATTR_MODE(attr);
                a5 = G3D_ATTR_ALPHA(attr);
                if (a5 == 0 || mode == G3D_POLY_MODE_SHADOW) {
                    first += verts;
                    continue;
                }
                ti = p->teximage;
                cur.deq = G3D_ATTR_DEPTH_EQ(attr) != 0;
                cur.mask = (a5 >= 31u) || G3D_ATTR_XLU_DEPTH(attr);
                cur.xlu_depth = G3D_ATTR_XLU_DEPTH(attr) != 0;
                cur.mode = mode == G3D_POLY_MODE_DECAL ? 1
                         : mode == G3D_POLY_MODE_TOON ? 2 : 0;
                cur.toon = mode == G3D_POLY_MODE_TOON;
                cur.a5 = (int)a5;
                cur.front = G3D_ATTR_FRONT(attr) != 0;
                cur.back = G3D_ATTR_BACK(attr) != 0;
                cur.textured = slot >= 0 && (s->disp3dcnt & 1u);
                cur.slot = cur.textured ? slot : -1;
                cur.wrap = cur.textured ? (int)(G3D_TEX_REPEAT_S(ti) | (G3D_TEX_REPEAT_T(ti) << 1)
                                                | (G3D_TEX_FLIP_S(ti) << 2) | (G3D_TEX_FLIP_T(ti) << 3)) : 0;
                cur.id = (int)G3D_ATTR_ID(attr);
                cur.two_pass = a5 >= 31u && cur.textured && s->tex[slot].partial;
                cur.fog = G3D_ATTR_FOG(attr) != 0;
                if (have && run_first + run_verts == first
                    && memcmp(&run, &cur, sizeof(run)) == 0) {
                    run_verts += verts;
                } else {
                    if (have) {
                        draw_run(g, s, &run, run_first, run_verts, &prev_depth_eq,
                                 &prev_mask, &bound_tex);
                        drawn++;
                    }
                    run = cur;
                    run_first = first;
                    run_verts = verts;
                    have = 1;
                }
                first += verts;
            }
            if (have) {
                draw_run(g, s, &run, run_first, run_verts, &prev_depth_eq,
                         &prev_mask, &bound_tex);
                drawn++;
            }
        }
        (void)drawn;
    }
    if (backdrop && s->backdrop.n_poke > 0) {
        backdrop_draw(g, &s->backdrop, 1);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    g->glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (use_ms) {
        /* resolve the samples into the frame's targets: the colour averaged,
         * the ID buffer and the depth from one sample (integer and depth
         * targets do not average), which the passes below read */
        static const GLenum c0[1] = { GL_COLOR_ATTACHMENT0 };
        static const GLenum c1[1] = { GL_COLOR_ATTACHMENT1 };
        static const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->ms_fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        g->glDrawBuffers(1, c0);
        g->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (edges || fog) {
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            g->glDrawBuffers(1, c1);
            g->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            g->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                                 GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        g->glDrawBuffers(2, both);
    }
    if (edges) {
        edge_pass(g, fb, s, w, cdepth);
    }
    if (fog) {
        fog_pass(g, fb, s);
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eidos_prof_end(EIDOS_PROF_GLDRAW, t_dr);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

static int render_with_aa(EidosGl *g, GlFb *fb, const G3dSnapshot *s, int w, int h)
{
    if (g != NULL && s != NULL && w > 0 && h > 0 &&
        g->integer_samples < MSAA_SAMPLES && (s->disp3dcnt & 0x10u) != 0 &&
        (long)w * h <= MSAA_MAX_PIXELS) {
        /* Some GL implementations (including Apple's) cannot multisample
         * integer attachments. Render the complete scene and postprocesses
         * at 2x instead, then average only the normalized colour target. */
        G3dSnapshot sample = *s;
        static const GLenum color[1] = { GL_COLOR_ATTACHMENT0 };
        static const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

        sample.disp3dcnt &= ~0x10u;
        if (render_into(g, &g->fb[2], &sample, w * 2, h * 2) != 0 ||
            fbo_resize(g, fb, w, h) != 0) {
            return -1;
        }
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->fb[2].fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        g->glDrawBuffers(1, color);
        g->glBlitFramebuffer(0, 0, w * 2, h * 2, 0, 0, w, h,
                             GL_COLOR_BUFFER_BIT, GL_LINEAR);
        g->glDrawBuffers(2, both);
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return glGetError() == GL_NO_ERROR ? 0 : -1;
    }
    return render_into(g, fb, s, w, h);
}

int eidos_gl_render3d(EidosGl *g, const G3dSnapshot *s, int w, int h)
{
    if (g == NULL) return -1;
    return render_with_aa(g, &g->fb[0], s, w, h);
}

static void unpremultiply(const unsigned char *raw, unsigned char *out,
                          int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++) {
        const unsigned char *s = raw + (size_t)(h - 1 - y) * (size_t)w * 4u;
        unsigned char *d = out + (size_t)y * (size_t)w * 4u;

        for (x = 0; x < w; x++) {
            unsigned a = s[x * 4 + 3];

            if (a == 255u) {
                d[x * 4 + 0] = s[x * 4 + 0];
                d[x * 4 + 1] = s[x * 4 + 1];
                d[x * 4 + 2] = s[x * 4 + 2];
                d[x * 4 + 3] = 255;
            } else if (a == 0) {
                d[x * 4 + 0] = d[x * 4 + 1] = d[x * 4 + 2] = d[x * 4 + 3] = 0;
            } else {
                unsigned c;

                for (c = 0; c < 3; c++) {
                    unsigned v = (s[x * 4 + c] * 255u + a / 2u) / a;

                    d[x * 4 + c] = (unsigned char)(v > 255u ? 255u : v);
                }
                d[x * 4 + 3] = (unsigned char)a;
            }
        }
    }
}

static int read_fb(EidosGl *g, const GlFb *fb, unsigned char *out, int w, int h)
{
    unsigned char *raw;
    unsigned long long t_rd;

    raw = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (raw == NULL) {
        return -1;
    }
    t_rd = eidos_prof_begin();
    g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eidos_prof_end(EIDOS_PROF_GLREAD, t_rd);
    t_rd = eidos_prof_begin();
    unpremultiply(raw, out, w, h);
    eidos_prof_end(EIDOS_PROF_UNPREM, t_rd);
    free(raw);
    return 0;
}

/* Start reading `fb` into the pack buffer without waiting for the GPU. */
static int read_fb_start(EidosGl *g, const GlFb *fb, int w, int h)
{
    if (g->pbo == 0) {
        g->glGenBuffers(1, &g->pbo);
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo);
    g->glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)((size_t)w * (size_t)h * 4u),
                    NULL, GL_STREAM_READ);
    g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g->pbo_pending = 1;
    return 0;
}

/* Collect the read started earlier (one frame ago). */
static int read_fb_finish(EidosGl *g, unsigned char *out, int w, int h)
{
    const unsigned char *raw;
    int rc = -1;

    if (!g->pbo_pending) {
        return 1;
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo);
    raw = (const unsigned char *)g->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (raw != NULL) {
        unsigned long long t0 = eidos_prof_begin();

        unpremultiply(raw, out, w, h);
        eidos_prof_end(EIDOS_PROF_UNPREM, t0);
        g->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        rc = 0;
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g->pbo_pending = 0;
    return rc;
}

int eidos_gl_read3d(EidosGl *g, unsigned char *out, int w, int h)
{
    if (g == NULL || out == NULL || g->fb[0].w != w || g->fb[0].h != h) {
        return -1;
    }
    return read_fb(g, &g->fb[0], out, w, h);
}

/* The frame at DS size, top-down non-premultiplied RGBA: what the software
 * rasteriser's frame is, for the 2D compositor and the display capture.
 * 1 = unchanged since the last call (out is not touched).  With `async` the
 * GPU is not waited for: the call returns the frame read one call ago (the
 * first call returns 1), so the picture lags a frame but the CPU never
 * stalls. */
int eidos_gl_read3d_1x(EidosGl *g, const G3dSnapshot *s, unsigned char *out,
                       int async)
{
    int got = 1;

    if (g == NULL || !g->have3d || s == NULL || out == NULL) {
        return -1;
    }
    if (g->read_valid && s->serial == g->read_serial) {
        return 1;
    }
    if (render_with_aa(g, &g->fb[1], s, 256, 192) != 0) {
        g->read_valid = 0;
        return -1;
    }
    if (async) {
        got = read_fb_finish(g, out, 256, 192);
        read_fb_start(g, &g->fb[1], 256, 192);
    } else {
        g->pbo_pending = 0;
        if (read_fb(g, &g->fb[1], out, 256, 192) != 0) {
            g->read_valid = 0;
            return -1;
        }
        got = 0;
    }
    g->read_serial = s->serial;
    g->read_valid = 1;
    return got;
}
