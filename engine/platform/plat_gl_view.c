/*
 * engine/platform/plat_gl_view.c -- drawing straight into a window with
 * OpenGL, for a tool that lays 3D pictures, sprites and an interface
 * canvas over each other (tools/spot-editor): the 3D picture stays a GPU
 * texture (no read back), the sprites are textures, the canvas is one
 * texture drawn on top.
 */

#include <stdlib.h>
#include <string.h>

#include "plat_gl_int.h"

static const char *k_vs_view =
    "#version 330 core\n"
    "uniform vec4 rect;\n"            /* x0 y0 x1 y1, NDC                  */
    "uniform vec4 uvr;\n"             /* u0 v0 u1 v1                       */
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
    "    uv = mix(uvr.xy, uvr.zw, c);\n"
    "    gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);\n"
    "}\n";

/* mode 0: a premultiplied 3D picture, its clear pixels left out and the rest
 * shown opaque and unpremultiplied (what eidos_gl_read3d gives the CPU);
 * mode 1: a straight sprite, its clear pixels left out, the rest opaque;
 * mode 2: a straight canvas, blended by its alpha;
 * mode 3: a straight sprite as mode 1, its colours faded fx.a of the way to
 * fx.rgb and blended at `alpha` (eidos_gl_view_draw_fx) */
static const char *k_fs_view =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform int mode;\n"
    "uniform vec4 fx;\n"
    "uniform float alpha;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec4 c = texture(tex, uv);\n"
    "    if (mode == 2) { color = c; return; }\n"
    "    if (c.a <= 0.0) discard;\n"
    "    if (mode == 3) { color = vec4(mix(c.rgb, fx.rgb, fx.a), alpha); return; }\n"
    "    color = vec4(mode == 0 ? c.rgb / c.a : c.rgb, 1.0);\n"
    "}\n";

typedef struct GlView {
    GLuint prog, vao;
    GLint u_rect, u_uvr, u_tex, u_mode, u_fx, u_alpha;
    int win_w, win_h;
    int failed;
} GlView;

static GlView g_view;

static int view_init(EidosGl *g)
{
    GlView *v = &g_view;

    if (v->prog != 0 || v->failed) {
        return v->prog != 0 ? 0 : -1;
    }
    v->prog = eidos_gl_compile_program(g, k_vs_view, k_fs_view);
    if (v->prog == 0) {
        v->failed = 1;
        return -1;
    }
    v->u_rect = g->glGetUniformLocation(v->prog, "rect");
    v->u_uvr = g->glGetUniformLocation(v->prog, "uvr");
    v->u_tex = g->glGetUniformLocation(v->prog, "tex");
    v->u_mode = g->glGetUniformLocation(v->prog, "mode");
    v->u_fx = g->glGetUniformLocation(v->prog, "fx");
    v->u_alpha = g->glGetUniformLocation(v->prog, "alpha");
    eidos_gl_quad_vao(g, &v->vao);
    return 0;
}

static GLuint tex_new(void)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

unsigned eidos_gl_view_keep3d(EidosGl *g, unsigned tex, int w, int h)
{
    GLuint t = (GLuint)tex;

    if (g == NULL || g->fb[0].w != w || g->fb[0].h != h) {
        return tex;
    }
    if (t == 0) {
        t = tex_new();
    }
    glBindTexture(GL_TEXTURE_2D, t);
    g->glBindFramebuffer(GL_FRAMEBUFFER, g->fb[0].fbo);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return (unsigned)t;
}

unsigned eidos_gl_view_upload(EidosGl *g, unsigned tex, const unsigned char *rgba, int w, int h, int same_size)
{
    GLuint t = (GLuint)tex;

    (void)g;
    if (rgba == NULL || w <= 0 || h <= 0) {
        return tex;
    }
    if (t == 0) {
        t = tex_new();
        same_size = 0;
    }
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (same_size) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    return (unsigned)t;
}

void eidos_gl_view_free(EidosGl *g, unsigned tex)
{
    GLuint t = (GLuint)tex;

    (void)g;
    if (t != 0) {
        glDeleteTextures(1, &t);
    }
}

void eidos_gl_view_begin(EidosGl *g, int win_w, int win_h, unsigned rgb)
{
    if (g == NULL) {
        return;
    }
    (void)view_init(g);
    g_view.win_w = win_w;
    g_view.win_h = win_h;
    eidos_gl_make_current(g);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor((float)((rgb >> 16) & 255u) / 255.0f, (float)((rgb >> 8) & 255u) / 255.0f,
                 (float)(rgb & 255u) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static float g_fx[4], g_alpha = 1.0f;

void eidos_gl_view_draw_fx(EidosGl *g, unsigned tex, int x, int y, int w, int h, const int *clip, const float fade[4],
                           float alpha)
{
    int i;

    for (i = 0; i < 4; i++) {
        g_fx[i] = fade[i];
    }
    g_alpha = alpha;
    eidos_gl_view_draw(g, tex, 3, x, y, w, h, 0, clip);
}

void eidos_gl_view_draw(EidosGl *g, unsigned tex, int mode, int x, int y, int w, int h, int flip,
                        const int *clip)
{
    GlView *v = &g_view;
    float W = (float)v->win_w, H = (float)v->win_h;

    if (g == NULL || tex == 0 || v->prog == 0 || w <= 0 || h <= 0 || W <= 0.0f || H <= 0.0f) {
        return;
    }
    if (clip != NULL) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(clip[0], v->win_h - (clip[1] + clip[3]), clip[2], clip[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    if (mode == 2 || mode == 3) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    g->glUseProgram(v->prog);
    g->glUniform4f(v->u_rect, (float)x / W * 2.0f - 1.0f, 1.0f - (float)y / H * 2.0f,
                   (float)(x + w) / W * 2.0f - 1.0f, 1.0f - (float)(y + h) / H * 2.0f);
    /* a top-down picture's first row at the top; a GL target's (flip) at the bottom */
    if (flip) {
        g->glUniform4f(v->u_uvr, 0.0f, 1.0f, 1.0f, 0.0f);
    } else {
        g->glUniform4f(v->u_uvr, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    g->glUniform1i(v->u_mode, mode);
    if (mode == 3) {
        g->glUniform4f(v->u_fx, g_fx[0], g_fx[1], g_fx[2], g_fx[3]);
        g->glUniform1f(v->u_alpha, g_alpha);
    }
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    g->glUniform1i(v->u_tex, 0);
    g->glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

int eidos_gl_view_read(EidosGl *g, unsigned char *rgba)
{
    int y, w = g_view.win_w, h = g_view.win_h;
    unsigned char *row;

    if (g == NULL || rgba == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    row = (unsigned char *)malloc((size_t)w * 4u);
    for (y = 0; row != NULL && y < h / 2; y++) {
        unsigned char *a = rgba + (size_t)y * (size_t)w * 4u, *b = rgba + (size_t)(h - 1 - y) * (size_t)w * 4u;

        memcpy(row, a, (size_t)w * 4u);
        memcpy(a, b, (size_t)w * 4u);
        memcpy(b, row, (size_t)w * 4u);
    }
    free(row);
    return 0;
}

void eidos_gl_view_end(EidosGl *g)
{
    if (g != NULL) {
        eidos_gl_swap(g);
    }
}
