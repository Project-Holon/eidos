/*
 * engine/platform/plat_gl2d.c -- the DS 2D compositor as a fragment shader
 * (docs/gpu-2d.md).
 *
 * One pass per screen.  The CPU fills a G2dGpuFrame (engine/g2d/g2d_gpu.h)
 * with what bg.c's compose_px reads; this file uploads it and composes the
 * screen the same way: the stack walk front to back (per priority the OBJ
 * pixel, then the BGs by layer number: the 3D layer, a raster, a text BG,
 * an affine BG), the windows, the colour effects and the master
 * brightness, in the same integer steps (bg.c, window.c).  The result is
 * a 256 x 192 RGBA8 texture, row 0 at the bottom of the target = DS row 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_gl_int.h"
#include "g2d_gpu.h"

static const char *k_vs2d =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
    "    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *k_fs2d =
    "#version 330 core\n"
    "uniform usampler2D vram;\n"      /* the BG window, 1024 bytes a row   */
    "uniform usampler2D pal;\n"       /* 256 x 18: BG, OBJ, OBJ ext 0..15  */
    "uniform usampler2D objt;\n"      /* index, priority, source, flags    */
    "uniform usampler2D l3t;\n"
    "uniform usampler2D rast;\n"      /* 256 x 768: the four rasters       */
    "uniform int vram_size;\n"
    "uniform int pal_count[18];\n"
    "uniform int text[40];\n"         /* 4 x en prio hofs vofs tw th code map char bpp8 */
    "uniform int aff[26];\n"          /* 2 x en prio bgi tiles map char wrap rx ry pa pb pc pd */
    "uniform int ras[12];\n"          /* 4 x en prio bgi                   */
    "uniform int obj_on;\n"
    "uniform int l3[4];\n"            /* en prio bgi hofs                  */
    "uniform int win[15];\n"          /* w0 en x1 x2 y1 y2 layers, w1 the same, obj en, obj layers, outside */
    "uniform int bld[6];\n"           /* effect first second eva evb evy   */
    "uniform int mb;\n"
    /* the GPU's own 3D frame as the 3D layer (phase 3): g3m 0 off (l3t),
     * 1 sampled at each DS pixel's centre, 2 per output pixel (osz the
     * output's size, a multiple of the DS screen); premultiplied, row 0 at
     * the bottom */
    "uniform int g3m;\n"
    "uniform sampler2D g3d;\n"
    "uniform ivec2 g3sz;\n"
    "uniform ivec2 osz;\n"
    "int ox, oy;\n"
    "out vec4 color;\n"
    "\n"
    "int rd8(int a) { return int(texelFetch(vram, ivec2(a & 1023, a >> 10), 0).r); }\n"
    "int rd16(int a) { return rd8(a) | (rd8(a + 1) << 8); }\n"
    "ivec3 palc(int row, int ci) {\n"
    "    if (ci < pal_count[row]) return ivec3(texelFetch(pal, ivec2(ci, row), 0).rgb);\n"
    "    return ivec3(255, 0, 255);\n"
    "}\n"
    "int to5(int c) { return (c * 31 + 127) / 255; }\n"
    "int to8(int c5) { return min(c5, 31) * 255 / 31; }\n"
    "int c5to6(int c5) { c5 = min(c5, 31); return c5 != 0 ? c5 * 2 + 1 : 0; }\n"
    "int c8to6(int c8) { return (c8 * 63 + 127) / 255; }\n"
    "int c6to8(int c6) { return min(c6, 63) * 255 / 63; }\n"
    "\n"
    /* bg.c g2d_bg_screen_index, the four hardware sizes and 128x128 */
    "int screen_index(int x, int y, int code) {\n"
    "    if (code == 0) return (x + y * 16) & 0xFFFF;\n"
    "    if (code == 1 || code == 2) return (x + y * 32) & 0xFFFF;\n"
    "    if (code == 3) return ((x & 31) + (y + (x & ~31)) * 32) & 0xFFFF;\n"
    "    if (code == 4) {\n"
    "        int map = ((((y >> 5) * 2 + (x >> 5)) & 0xFFFF) * 0x400) & 0xFFFF;\n"
    "        int off = ((x & 31) + (y & 31) * 32) & 0xFFFF;\n"
    "        return (off + map) & 0xFFFF;\n"
    "    }\n"
    "    return 0;\n"
    "}\n"
    /* bg.c g2d_bg_sample on a hardware text BG: -1 none, else the palette
     * colour index; *idx the tile pixel */
    "int text_sample(int n, int sx, int sy, out int idx) {\n"
    "    int b = n * 10;\n"
    "    int tw = text[b + 4], th = text[b + 5];\n"
    "    idx = 0;\n"
    "    if (tw == 0 || th == 0) return -1;\n"
    "    int px = (sx + text[b + 2]) & (tw * 8 - 1);\n"
    "    int py = (sy + text[b + 3]) & (th * 8 - 1);\n"
    "    int tx = px >> 3, ty = py >> 3;\n"
    "    int ei = screen_index(tx, ty, text[b + 6]);\n"
    "    if (ei >= tw * th) return -1;\n"
    "    int e = rd16(text[b + 7] + ei * 2);\n"
    "    int bpt = text[b + 9] != 0 ? 64 : 32;\n"
    "    int off = (e & 0x3FF) * bpt;\n"
    "    int size = vram_size - text[b + 8];\n"
    "    int lx = px & 7, ly = py & 7;\n"
    "    if (off <= size && bpt <= size - off) {\n"
    "        if (((e >> 10) & 1) != 0) lx = 7 - lx;\n"
    "        if (((e >> 11) & 1) != 0) ly = 7 - ly;\n"
    "        if (bpt == 32) {\n"
    "            int v = rd8(text[b + 8] + off + ly * 4 + (lx >> 1));\n"
    "            idx = (lx & 1) != 0 ? (v >> 4) : (v & 15);\n"
    "        } else {\n"
    "            idx = rd8(text[b + 8] + off + ly * 8 + lx);\n"
    "        }\n"
    "    }\n"
    "    return (bpt == 32 ? ((e >> 12) & 15) * 16 : 0) + idx;\n"
    "}\n"
    /* bg_affine.c g2d_bg_affine_sample */
    "int aff_sample(int n, int sx, int sy, out int idx) {\n"
    "    int b = n * 13;\n"
    "    int tiles = aff[b + 3];\n"
    "    idx = 0;\n"
    "    if (tiles == 0) return -1;\n"
    "    int side = tiles * 8;\n"
    "    int tx = (aff[b + 7] + aff[b + 9] * sx + aff[b + 10] * sy) >> 8;\n"
    "    int ty = (aff[b + 8] + aff[b + 11] * sx + aff[b + 12] * sy) >> 8;\n"
    "    if (aff[b + 6] != 0) { tx &= side - 1; ty &= side - 1; }\n"
    "    else if (tx < 0 || ty < 0 || tx >= side || ty >= side) return -1;\n"
    "    int ei = (ty >> 3) * tiles + (tx >> 3);\n"
    "    if (ei >= vram_size - aff[b + 4]) return -1;\n"
    "    int tile = rd8(aff[b + 4] + ei);\n"
    "    int off = tile * 64;\n"
    "    int size = vram_size - aff[b + 5];\n"
    "    if (off <= size && 64 <= size - off) idx = rd8(aff[b + 5] + off + (ty & 7) * 8 + (tx & 7));\n"
    "    return idx;\n"
    "}\n"
    "\n"
    /* window.c g2d_window_mask */
    "bool win_in(int b, int x, int y) {\n"
    "    int x2 = win[b + 2], y2 = win[b + 4];\n"
    "    if (x2 > 256 || x2 < win[b + 1]) x2 = 256;\n"
    "    if (y2 > 192 || y2 < win[b + 3]) y2 = 192;\n"
    "    return x >= win[b + 1] && x < x2 && y >= win[b + 3] && y < y2;\n"
    "}\n"
    "int win_mask(int x, int y, bool objwin) {\n"
    "    if (win[0] == 0 && win[6] == 0 && win[12] == 0) return 0x3F;\n"
    "    if (win[0] != 0 && win_in(0, x, y)) return win[5];\n"
    "    if (win[6] != 0 && win_in(6, x, y)) return win[11];\n"
    "    if (win[12] != 0 && objwin) return win[13];\n"
    "    return win[14];\n"
    "}\n"
    "\n"
    /* the GPU's 3D at the layer's pixel bx (scrolled) of DS row y, straight */
    "uvec4 g3fetch(int bx, int y) {\n"
    /* 3: the sentinel colour (where the 3D shows), 4: opaque black, 5:
     * nothing (the 3D off: BG0 stays the 3D layer, never a text BG) */
    "    if (g3m == 3) return uvec4(84u, 168u, 52u, 255u);\n"
    "    if (g3m == 4) return uvec4(0u, 0u, 0u, 255u);\n"
    "    if (g3m == 5) return uvec4(0u);\n"
    "    int gx, gy;\n"
    "    if (g3m == 1) {\n"
    "        gx = (bx * 2 + 1) * g3sz.x / 512;\n"
    "        gy = (y * 2 + 1) * g3sz.y / 384;\n"
    "    }\n"
    "    vec4 g;\n"
    "    if (g3m == 1) {\n"
    "        g = texelFetch(g3d, clamp(ivec2(gx, g3sz.y - 1 - gy), ivec2(0), g3sz - 1), 0);\n"
    "    } else {\n"
    /* the output pixel's centre in the layer (scrolled), filtered as the
     * old composite sampled the 3D */
    "        float fx = (float(bx) + (float(ox) + 0.5) * 256.0 / float(osz.x) - float(ox * 256 / osz.x)) / 256.0;\n"
    "        float fy = (float(oy) + 0.5) / float(osz.y);\n"
    "        g = texture(g3d, vec2(fx, 1.0 - fy));\n"
    "    }\n"
    "    if (g.a <= 0.0) return uvec4(0u);\n"
    "    vec3 c = clamp(g.rgb / g.a, 0.0, 1.0);\n"
    "    return uvec4(uvec3(round(c * 255.0)), uint(round(g.a * 255.0)));\n"
    "}\n"
    /* the stack: up to two samples, front to back */
    "ivec3 s_rgb[2];\n"
    "int s_bit[2];\n"
    "bool s_3d[2];\n"
    "int s_a5[2];\n"
    "bool s_semi[2];\n"
    "int n;\n"
    "void push(ivec3 rgb, int bit, bool is3d, int a5, bool semi) {\n"
    "    if (n < 2) { s_rgb[n] = rgb; s_bit[n] = bit; s_3d[n] = is3d; s_a5[n] = a5; s_semi[n] = semi; n++; }\n"
    "}\n"
    "void walk(int x, int y, int allowed, uvec4 o) {\n"
    "    n = 0;\n"
    "    for (int prio = 0; prio < 4 && n < 2; prio++) {\n"
    "        if ((allowed & 0x10) != 0 && obj_on != 0 && o.r != 0u && int(o.g) == prio) {\n"
    "            int row = o.b == 0u ? 1 : 1 + int(o.b);\n"
    "            push(palc(row, int(o.r)), 0x10, false, 0, (o.a & 1u) != 0u);\n"
    "        }\n"
    "        for (int bgi = 0; bgi < 8 && n < 2; bgi++) {\n"
    "            if (bgi < 4 && (allowed & (1 << bgi)) == 0) continue;\n"
    "            int bit = bgi < 4 ? (1 << bgi) : 0;\n"
    "            if (l3[0] != 0 && l3[1] == prio && l3[2] == bgi) {\n"
    "                int bx = (x + l3[3]) & 0x1FF;\n"
    "                if (bx < 256) {\n"
    "                    uvec4 p = g3m == 0 ? texelFetch(l3t, ivec2(bx, y), 0) : g3fetch(bx, y);\n"
    "                    if (p.a != 0u) {\n"
    "                        int a5 = (int(p.a) * 31 + 127) / 255;\n"
    "                        push(ivec3(p.rgb), bit, true, a5 != 0 ? a5 : 1, false);\n"
    "                    }\n"
    "                }\n"
    "                continue;\n"
    "            }\n"
    "            int r = -1;\n"
    "            for (int i = 0; i < 4; i++) {\n"
    "                if (r < 0 && ras[i * 3] != 0 && ras[i * 3 + 1] == prio && ras[i * 3 + 2] == bgi) r = i;\n"
    "            }\n"
    "            if (r >= 0) {\n"
    "                uvec4 p = texelFetch(rast, ivec2(x, y + r * 192), 0);\n"
    "                if (p.a != 0u) push(ivec3(p.rgb), bit, false, 0, false);\n"
    "                continue;\n"
    "            }\n"
    "            if (bgi < 4 && text[bgi * 10] != 0 && text[bgi * 10 + 1] == prio) {\n"
    "                int idx;\n"
    "                int ci = text_sample(bgi, x, y, idx);\n"
    "                if (ci >= 0 && idx != 0) { push(palc(0, ci), bit, false, 0, false); continue; }\n"
    "            }\n"
    "            for (int i = 0; i < 2; i++) {\n"
    "                if (aff[i * 13] == 0 || aff[i * 13 + 1] != prio || aff[i * 13 + 2] != bgi) continue;\n"
    "                int idx;\n"
    "                int ci = aff_sample(i, x, y, idx);\n"
    "                if (ci >= 0 && idx != 0) { push(palc(0, ci), bit, false, 0, false); break; }\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n"
    "ivec3 stack_rgb(int i, out int bit) {\n"
    "    if (i < n) { bit = s_bit[i]; return s_rgb[i]; }\n"
    "    bit = 0x20;\n"
    "    return palc(0, 0);\n"
    "}\n"
    "ivec3 blend_alpha(ivec3 a, ivec3 b) {\n"
    "    int eva = min(bld[3], 16), evb = min(bld[4], 16);\n"
    "    ivec3 o;\n"
    "    for (int i = 0; i < 3; i++) o[i] = to8(min((to5(a[i]) * eva + to5(b[i]) * evb) >> 4, 31));\n"
    "    return o;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    ox = int(gl_FragCoord.x); oy = int(gl_FragCoord.y);\n"
    "    int x = g3m == 2 ? ox * 256 / osz.x : ox, y = g3m == 2 ? oy * 192 / osz.y : oy;\n"
    "    uvec4 o = texelFetch(objt, ivec2(x, y), 0);\n"
    "    int allowed = win_mask(x, y, (o.a & 2u) != 0u);\n"
    "    walk(x, y, allowed, o);\n"
    "    int tbit, bbit;\n"
    "    ivec3 top = stack_rgb(0, tbit);\n"
    "    ivec3 outc = top;\n"
    "    bool is3d = n > 0 && s_3d[0];\n"
    /* window.c and bg.c blend_pixel, in order */
    "    bool done = false;\n"
    "    if ((win_mask(x, y, (o.a & 2u) != 0u) & 0x20) == 0) done = true;\n"
    "    if (!done && is3d) {\n"
    "        ivec3 below = stack_rgb(1, bbit);\n"
    "        if ((bbit & bld[2]) != 0) {\n"
    "            done = true;\n"
    "            if (s_a5[0] < 31) {\n"
    "                int a5 = s_a5[0];\n"
    "                for (int i = 0; i < 3; i++) {\n"
    "                    int c6 = (c8to6(top[i]) * (a5 + 1) + c5to6(to5(below[i])) * (31 - a5)) >> 5;\n"
    "                    outc[i] = c6to8(c6);\n"
    "                }\n"
    "            }\n"
    "        } else if (g3m == 2 && s_a5[0] < 31) {\n"
    /* the GPU's smoothed 3D edges over any 2D (the DS has no such
     * partial pixels where the 2D is not a blend target) */
    "            done = true;\n"
    "            int a5 = s_a5[0];\n"
    "            for (int i = 0; i < 3; i++) {\n"
    "                int c6 = (c8to6(top[i]) * (a5 + 1) + c5to6(to5(below[i])) * (31 - a5)) >> 5;\n"
    "                outc[i] = c6to8(c6);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (!done && n > 0 && tbit == 0x10 && s_semi[0]) {\n"
    "        ivec3 below = stack_rgb(1, bbit);\n"
    "        if ((bbit & bld[2]) != 0) { outc = blend_alpha(top, below); is3d = false; done = true; }\n"
    "    }\n"
    "    if (!done && bld[0] != 0 && (tbit & bld[1]) != 0) {\n"
    "        if (bld[0] == 1) {\n"
    "            ivec3 below = stack_rgb(1, bbit);\n"
    "            if ((bbit & bld[2]) != 0) { outc = blend_alpha(top, below); is3d = false; }\n"
    "        } else {\n"
    "            int evy = min(bld[5], 16);\n"
    "            for (int i = 0; i < 3; i++) {\n"
    "                int a = to5(top[i]);\n"
    "                int v = bld[0] == 2 ? a + (((31 - a) * evy) >> 4) : a - ((a * evy) >> 4);\n"
    "                outc[i] = to8(min(v, 31));\n"
    "            }\n"
    "            is3d = false;\n"
    "        }\n"
    "    }\n"
    /* the master brightness on the 6-bit stage */
    "    int mode = (mb >> 14) & 3, f = min(mb & 0x1F, 16);\n"
    "    if ((mode == 1 || mode == 2) && (mb & 0x1F) != 0) {\n"
    "        for (int i = 0; i < 3; i++) {\n"
    "            int c = is3d ? c8to6(outc[i]) : c5to6(to5(outc[i]));\n"
    "            if (mode == 1) c += ((63 - c) * f) >> 4; else c -= (c * f) >> 4;\n"
    "            outc[i] = c6to8(c);\n"
    "        }\n"
    "    }\n"
    "    color = vec4(vec3(outc) / 255.0, 1.0);\n"
    "}\n";

typedef struct Gl2d {
    GLuint prog, vao, fbo, out, vram[2], pal, obj, l3, rast;
    const void *vram_from[2];   /* the frame and the bytes each one holds   */
    u32 vram_gen[2];
    int vram_h[2];
    GLuint pbo[2];              /* a screen's pixels on their way back      */
    int pbo_full[2];
    GLint u_vram, u_pal, u_obj, u_l3, u_rast, u_vram_size, u_pal_count, u_text, u_aff,
          u_ras, u_obj_on, u_l3p, u_win, u_bld, u_mb, u_g3m, u_g3d, u_g3sz, u_osz;
    GLuint hfbo, hout;          /* the screen at the 3D's size (phase 3)    */
    int hw, hh;
    int failed;
} Gl2d;

static Gl2d g_2d;

static GLuint utex(GLenum ifmt, int w, int h)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0,
                 ifmt == GL_R8UI ? GL_RED_INTEGER : GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    return t;
}

static int gl2d_init(EidosGl *g)
{
    Gl2d *s = &g_2d;

    if (s->prog != 0 || s->failed) {
        return s->prog != 0 ? 0 : -1;
    }
#if defined(__vita__)
    /* vitaGL has no integer textures: the CPU composes the 2D there */
    s->failed = 1;
    return -1;
#endif
    s->prog = eidos_gl_compile_program(g, k_vs2d, k_fs2d);
    if (s->prog == 0) {
        s->failed = 1;
        return -1;
    }
    s->u_vram = g->glGetUniformLocation(s->prog, "vram");
    s->u_pal = g->glGetUniformLocation(s->prog, "pal");
    s->u_obj = g->glGetUniformLocation(s->prog, "objt");
    s->u_l3 = g->glGetUniformLocation(s->prog, "l3t");
    s->u_rast = g->glGetUniformLocation(s->prog, "rast");
    s->u_vram_size = g->glGetUniformLocation(s->prog, "vram_size");
    s->u_pal_count = g->glGetUniformLocation(s->prog, "pal_count");
    s->u_text = g->glGetUniformLocation(s->prog, "text");
    s->u_aff = g->glGetUniformLocation(s->prog, "aff");
    s->u_ras = g->glGetUniformLocation(s->prog, "ras");
    s->u_obj_on = g->glGetUniformLocation(s->prog, "obj_on");
    s->u_l3p = g->glGetUniformLocation(s->prog, "l3");
    s->u_win = g->glGetUniformLocation(s->prog, "win");
    s->u_bld = g->glGetUniformLocation(s->prog, "bld");
    s->u_mb = g->glGetUniformLocation(s->prog, "mb");
    s->u_g3m = g->glGetUniformLocation(s->prog, "g3m");
    s->u_g3d = g->glGetUniformLocation(s->prog, "g3d");
    s->u_g3sz = g->glGetUniformLocation(s->prog, "g3sz");
    s->u_osz = g->glGetUniformLocation(s->prog, "osz");
    g->glGenVertexArrays(1, &s->vao);
    s->pal = utex(GL_RGBA8UI, 256, 18);
    s->obj = utex(GL_RGBA8UI, 256, 192);
    s->l3 = utex(GL_RGBA8UI, 256, 192);
    s->rast = utex(GL_RGBA8UI, 256, 192 * 4);
    glGenTextures(1, &s->out);
    glBindTexture(GL_TEXTURE_2D, s->out);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g->glGenFramebuffers(1, &s->fbo);
    g->glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->out, 0);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        s->failed = 1;
        return -1;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

static void upload(GLuint t, int w, int h, GLenum fmt, const void *px)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, px);
}

int eidos_gl2d_ready(EidosGl *g)
{
    return g != NULL && gl2d_init(g) == 0;
}

int eidos_gl2d_pixels(EidosGl *g, int screen, unsigned char *out)
{
    Gl2d *s = &g_2d;
    const unsigned char *px;

    if (g == NULL || screen < 0 || screen > 1 || !s->pbo_full[screen]) {
        return -1;
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo[screen]);
    px = (const unsigned char *)g->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (px != NULL) {
        memcpy(out, px, 256 * 192 * 4);
        g->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    s->pbo_full[screen] = 0;
    return px != NULL ? 0 : -1;
}

int eidos_gl2d_compose(EidosGl *g, const G2dGpuFrame *f, unsigned char *readback,
                       int to_screen)
{
    Gl2d *s = &g_2d;
    static u8 pal[18 * 256 * 4];
    GLint ints[40];
    GLint vp[4];
    int i, rows, v, same, g3, from2d;

    if (f == NULL || !f->valid || gl2d_init(g) != 0) {
        return -1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    rows = (int)((f->bgv_size + 1023u) / 1024u);
    v = f->screen & 1;          /* one BG window texture per screen */
    if (rows > s->vram_h[v]) {
        if (s->vram[v] != 0) {
            glDeleteTextures(1, &s->vram[v]);
        }
        s->vram[v] = utex(GL_R8UI, 1024, rows);
        s->vram_h[v] = rows;
        s->vram_from[v] = NULL;
    }
    glBindTexture(GL_TEXTURE_2D, s->vram[v]);
    same = s->vram_from[v] == (const void *)f && s->vram_gen[v] == f->bgv_gen;
    s->vram_from[v] = f;
    s->vram_gen[v] = f->bgv_gen;
    if (same) {
        /* the bytes it holds already */
    } else if (f->bgv_size >= 1024u) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, (GLsizei)(f->bgv_size / 1024u),
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, f->bgv);
    }
    if (!same && f->bgv_size % 1024u != 0u) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)(f->bgv_size / 1024u),
                        (GLsizei)(f->bgv_size % 1024u), 1, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                        f->bgv + (f->bgv_size / 1024u) * 1024u);
    }
    for (i = 0; i < 18 * 256; i++) {
        pal[i * 4 + 0] = f->pal[i / 256][i % 256][0];
        pal[i * 4 + 1] = f->pal[i / 256][i % 256][1];
        pal[i * 4 + 2] = f->pal[i / 256][i % 256][2];
        pal[i * 4 + 3] = 255;
    }
    upload(s->pal, 256, 18, GL_RGBA_INTEGER, pal);
    upload(s->obj, 256, 192, GL_RGBA_INTEGER, f->obj);
    /* phase 3: the screen whose 3D the GPU drew takes it from there */
    g3 = to_screen >= 0 && to_screen == g->s3d2 && f->l3_on && g->fb[0].color != 0 && g->fb[0].w > 0
         && g->fb[0].h > 0;
    if (f->l3_on && !g3) {
        upload(s->l3, 256, 192, GL_RGBA_INTEGER, f->l3);
    }
    glBindTexture(GL_TEXTURE_2D, s->rast);
    for (i = 0; i < 4; i++) {
        /* only the rasters in use (the shader reads no other) */
        if (f->raster[i].enabled) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i * 192, 256, 192, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,
                            f->raster[i].rgba);
        }
    }

    g->glUseProgram(s->prog);
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->vram[v]);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s->pal);
    g->glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s->obj);
    g->glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s->l3);
    g->glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, s->rast);
    g->glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, g3 ? g->fb[0].color : 0);
    g->glActiveTexture(GL_TEXTURE0);
    g->glUniform1i(s->u_g3d, 5);
    from2d = to_screen >= 0 && to_screen == g->s3d && g->s3d_from2d && f->l3_on;
    g->glUniform1i(s->u_g3m, g3 ? 1 : from2d ? 3 : 0);
    g->glUniform2i(s->u_g3sz, g->fb[0].w, g->fb[0].h);
    g->glUniform2i(s->u_osz, 256, 192);
    g->glUniform1i(s->u_vram, 0);
    g->glUniform1i(s->u_pal, 1);
    g->glUniform1i(s->u_obj, 2);
    g->glUniform1i(s->u_l3, 3);
    g->glUniform1i(s->u_rast, 4);
    g->glUniform1i(s->u_vram_size, (GLint)f->bgv_size);
    for (i = 0; i < 18; i++) {
        ints[i] = (GLint)f->pal_count[i];
    }
    g->glUniform1iv(s->u_pal_count, 18, ints);
    for (i = 0; i < 4; i++) {
        const G2dGpuText *t = &f->text[i];
        GLint *v = ints + i * 10;

        v[0] = t->enabled; v[1] = (GLint)t->priority; v[2] = t->hofs; v[3] = t->vofs;
        v[4] = (GLint)t->tiles_wide; v[5] = (GLint)t->tiles_high; v[6] = (GLint)t->size_code;
        v[7] = (GLint)t->map_ofs; v[8] = (GLint)t->char_ofs; v[9] = t->bpp8;
    }
    g->glUniform1iv(s->u_text, 40, ints);
    for (i = 0; i < 2; i++) {
        const G2dGpuAffine *a = &f->affine[i];
        GLint *v = ints + i * 13;

        v[0] = a->enabled; v[1] = (GLint)a->priority; v[2] = (GLint)a->bg_index;
        v[3] = (GLint)a->tiles; v[4] = (GLint)a->map_ofs; v[5] = (GLint)a->char_ofs;
        v[6] = a->wrap; v[7] = a->ref_x; v[8] = a->ref_y;
        v[9] = a->pa; v[10] = a->pb; v[11] = a->pc; v[12] = a->pd;
    }
    g->glUniform1iv(s->u_aff, 26, ints);
    for (i = 0; i < 4; i++) {
        ints[i * 3] = f->raster[i].enabled;
        ints[i * 3 + 1] = (GLint)f->raster[i].priority;
        ints[i * 3 + 2] = (GLint)f->raster[i].bg_index;
    }
    g->glUniform1iv(s->u_ras, 12, ints);
    g->glUniform1i(s->u_obj_on, f->obj_on);
    ints[0] = f->l3_on;
    ints[1] = (GLint)f->l3_priority;
    ints[2] = (GLint)f->l3_bg_index;
    ints[3] = f->l3_hofs;
    g->glUniform1iv(s->u_l3p, 4, ints);
    {
        const G2dWindow *w[2];
        int k;

        w[0] = &f->windows.win0;
        w[1] = &f->windows.win1;
        for (k = 0; k < 2; k++) {
            ints[k * 6] = w[k]->enabled;
            ints[k * 6 + 1] = (GLint)w[k]->x1;
            ints[k * 6 + 2] = (GLint)w[k]->x2;
            ints[k * 6 + 3] = (GLint)w[k]->y1;
            ints[k * 6 + 4] = (GLint)w[k]->y2;
            ints[k * 6 + 5] = (GLint)w[k]->layers;
        }
        ints[12] = f->windows.obj_enabled;
        ints[13] = (GLint)f->windows.obj_layers;
        ints[14] = (GLint)f->windows.outside_layers;
        g->glUniform1iv(s->u_win, 15, ints);
    }
    ints[0] = (GLint)f->blend.effect;
    ints[1] = (GLint)f->blend.first;
    ints[2] = (GLint)f->blend.second;
    ints[3] = (GLint)f->blend.eva;
    ints[4] = (GLint)f->blend.evb;
    ints[5] = (GLint)f->blend.evy;
    g->glUniform1iv(s->u_bld, 6, ints);
    g->glUniform1i(s->u_mb, (GLint)f->master_bright);

    glGetIntegerv(GL_VIEWPORT, vp);
    g->glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glViewport(0, 0, 256, 192);
    glDisable(GL_BLEND);
    g->glBindVertexArray(s->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (readback != NULL) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    }
    if (to_screen == 0 || to_screen == 1) {
        int ow = 256, oh = 192;

        if (g3) {
            /* the screen again at the 3D's size, each pixel with the 3D
             * pixel under it: what the window shows */
            /* at least the size it is shown at: the 3D filtered up to it,
             * the 2D pixels sharp */
            ow = g->fb[0].w > g->s3d2_w ? g->fb[0].w : g->s3d2_w;
            oh = g->fb[0].h > g->s3d2_h ? g->fb[0].h : g->s3d2_h;
            ow = ow > 4096 ? 4096 : ow;
            oh = oh > 4096 ? 4096 : oh;
            if (s->hw != ow || s->hh != oh) {
                if (s->hout == 0) {
                    glGenTextures(1, &s->hout);
                    g->glGenFramebuffers(1, &s->hfbo);
                }
                glBindTexture(GL_TEXTURE_2D, s->hout);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                g->glBindFramebuffer(GL_FRAMEBUFFER, s->hfbo);
                g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->hout, 0);
                s->hw = ow;
                s->hh = oh;
            }
            g->glBindFramebuffer(GL_FRAMEBUFFER, s->hfbo);
            glViewport(0, 0, ow, oh);
            g->glUniform1i(s->u_g3m, 2);
            g->glUniform2i(s->u_osz, ow, oh);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        /* the screen's own texture, as eidos_gl_upload fills it: target
         * row 0 is DS row 0, the upload's first row too */
        glBindTexture(GL_TEXTURE_2D, g->tex[to_screen]);
        if (g->tw[to_screen] != ow || g->th[to_screen] != oh) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            g->tw[to_screen] = ow;
            g->th[to_screen] = oh;
        }
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ow, oh);
        g->glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        /* and back to the CPU without waiting (eidos_gl2d_pixels) */
        if (s->pbo[to_screen] == 0) {
            g->glGenBuffers(1, &s->pbo[to_screen]);
            g->glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo[to_screen]);
            g->glBufferData(GL_PIXEL_PACK_BUFFER, 256 * 192 * 4, NULL, GL_STREAM_READ);
        }
        g->glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo[to_screen]);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
        g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        s->pbo_full[to_screen] = 1;
        if (from2d) {
            /* the world composite's other two screens: the 3D off, and
             * (where a 2D layer can blend over the 3D) a black 3D */
            /* unit 0 is the BG window again (the copy above bound the
             * screen's texture there); the copies use unit 7 */
            glBindTexture(GL_TEXTURE_2D, s->vram[v]);
            g->glUniform1i(s->u_g3m, 5);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            g->glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, g->behind_tex);
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 256, 192, 0);
            g->glActiveTexture(GL_TEXTURE0);
            g->behind_w = 256;
            g->behind_h = 192;
            g->has_zero = 0;
            if ((f->blend.second & 1u) != 0u) {
                g->glUniform1i(s->u_g3m, 4);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                g->glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D, g->zero_tex);
                glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 256, 192, 0);
                g->glActiveTexture(GL_TEXTURE0);
                g->has_zero = 1;
            }
        }
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    /* the integer textures off the units the presenter's float samplers
     * use (a driver checks every sampler of a program) */
    for (i = 7; i >= 0; i--) {
        g->glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return 0;
}
