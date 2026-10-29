/*
 * tests/test_gpu3d.c -- the GPU 3D rasteriser against the software one.
 *
 * Both draw the same clip-space polygons (engine/platform/plat_gl3d.c and
 * engine/g3d/raster.c).  At 256x192 the two must agree except where the
 * fill rule puts an edge pixel on one side or the other, so the test counts
 * pixels that differ by more than a small tolerance and allows a thin
 * fraction of them (the polygon edges).  The scenes cover vertex colour
 * (perspective correct), a repeating / flipping texture with alpha, decal,
 * toon and highlight shading, translucency over a clear plane of alpha 0,
 * the depth test in either draw order, culling and the alpha test.
 *
 * It needs a real window system (a hidden OpenGL 3.3 window); with none, or
 * under SDL_VIDEODRIVER=dummy, it reports a skip and passes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "g3d.h"
#include "hw3d.h"
#include "plat_gl.h"
#include "port.h"
#include "port_test.h"

#define W 256
#define H 192

static G3dVertex g_verts[64];
static u32 g_idx[64];
static G3dPoly g_polys[16];
static s32 g_ptex[16];
static G3dSnapTex g_tex[3];
static u32 g_nv, g_ni, g_np;

static void scene_reset(void)
{
    g_nv = g_ni = g_np = 0;
}

static G3dVertex mkv(s32 x, s32 y, s32 z, s32 w, s32 s, s32 t, u8 r, u8 g, u8 b)
{
    G3dVertex v;

    memset(&v, 0, sizeof(v));
    v.x = x; v.y = y; v.z = z; v.w = w;
    v.s = s; v.t = t;
    v.r = r; v.g = g; v.b = b;
    return v;
}

static void add_poly(const G3dVertex *v, u32 n, u32 attr, u32 teximage,
                     s32 tex)
{
    u32 i;

    g_polys[g_np].first = g_ni;
    g_polys[g_np].count = n;
    g_polys[g_np].attr = attr;
    g_polys[g_np].teximage = teximage;
    g_polys[g_np].pltt_base = 0;
    g_ptex[g_np] = tex;
    for (i = 0; i < n; i++) {
        g_verts[g_nv] = v[i];
        g_idx[g_ni++] = g_nv++;
    }
    g_np++;
}

#define ATTR(alpha, id, front, back) \
    ((u32)(alpha) << 16 | (u32)(id) << 24 | ((front) ? 0x80u : 0u) | ((back) ? 0x40u : 0u))

static u32 teximage(int rs, int rt, int fs, int ft)
{
    return ((u32)rs << 16) | ((u32)rt << 17) | ((u32)fs << 18) | ((u32)ft << 19)
           | (7u << 26);
}

static G3dSnapshot make_snap(void)
{
    G3dSnapshot s;
    u32 i;

    memset(&s, 0, sizeof(s));
    s.verts = g_verts; s.n_verts = g_nv;
    s.idx = g_idx; s.n_idx = g_ni;
    s.polys = g_polys; s.n_polys = g_np;
    s.poly_tex = g_ptex;
    s.tex = g_tex; s.n_tex = 3;
    s.disp3dcnt = 1u;                      /* texturing on */
    s.clear_depth = 0x7FFFu;
    for (i = 0; i < 32u; i++) {            /* a toon ramp */
        s.toon[i * 3u + 0] = (u8)(i * 8u);
        s.toon[i * 3u + 1] = (u8)(255u - i * 8u);
        s.toon[i * 3u + 2] = (u8)(64u + i * 4u);
    }
    return s;
}

/* The software reference for the same polygons. */
static void soft_render_sz(const G3dSnapshot *s, u8 *rgba, u32 sw, u32 sh)
{
    G3dTarget t;
    G3dTexture tx[3];
    u32 i, k;

    g3d_target_init(&t, sw, sh);
    g3d_target_clear(&t, 0, 0, 0, 0);
    t.alpha_ref = s->alpha_ref;
    t.toon = s->toon;
    t.highlight = s->highlight;
    t.wbuffer = s->wbuffer;
    for (i = 0; i < s->n_tex; i++) {
        tx[i].width = s->tex[i].w;
        tx[i].height = s->tex[i].h;
        tx[i].format = 7;
        tx[i].rgba = s->tex[i].rgba;
    }
    for (i = 0; i < s->n_polys; i++) {
        G3dVertex v[4];
        const G3dPoly *p = &s->polys[i];

        for (k = 0; k < p->count; k++) {
            v[k] = s->verts[s->idx[p->first + k]];
        }
        g3d_raster_polygon(&t, v, p->count, p->attr, p->teximage,
                           s->poly_tex[i] >= 0 ? &tx[s->poly_tex[i]] : NULL);
    }
    memcpy(rgba, t.rgba, (size_t)sw * sh * 4u);
    g3d_target_free(&t);
}

static void soft_render(const G3dSnapshot *s, u8 *rgba)
{
    soft_render_sz(s, rgba, W, H);
}

/* Pixels where the two frames differ by more than `tol` in any channel. */
static u32 diff_count(const u8 *a, const u8 *b, int tol, u32 *covered)
{
    u32 i, bad = 0, cov = 0;

    for (i = 0; i < (u32)W * H; i++) {
        int c, d = 0;

        if (a[i * 4u + 3u] != 0 || b[i * 4u + 3u] != 0) {
            cov++;
        }
        for (c = 0; c < 4; c++) {
            int e = (int)a[i * 4u + (u32)c] - (int)b[i * 4u + (u32)c];

            if (e < 0) {
                e = -e;
            }
            if (e > d) {
                d = e;
            }
        }
        if (d > tol) {
            bad++;
        }
    }
    *covered = cov;
    return bad;
}

static void check_scene(const char *name, const G3dSnapshot *s, EidosGl *gl,
                        int tol, double max_frac)
{
    static u8 soft[W * H * 4], gpu[W * H * 4];
    u32 bad, cov;
    char msg[96];

    soft_render(s, soft);
    if (eidos_gl_render3d(gl, s, W, H) != 0 || eidos_gl_read3d(gl, gpu, W, H) != 0) {
        BW_CHECK_EQ(name, 0, 1);
        return;
    }
    bad = diff_count(soft, gpu, tol, &cov);
    if (getenv("GPU3D_DUMP") != NULL) {
        char path[256];
        FILE *f;
        u32 k, which;

        for (which = 0; which < 2u; which++) {
            snprintf(path, sizeof(path), "%s_%s_%s.ppm", getenv("GPU3D_DUMP"),
                     which ? "gpu" : "soft", name);
            for (k = 0; path[k] != 0; k++) {
                if (path[k] == ' ' || path[k] == ',' || path[k] == ':') {
                    path[k] = '_';
                }
            }
            f = fopen(path, "wb");
            if (f == NULL) {
                printf("  cannot write %s\n", path);
            }
            if (f != NULL) {
                const u8 *src = which ? gpu : soft;

                fprintf(f, "P6 %d %d 255 ", W, H);
                for (k = 0; k < (u32)W * H; k++) {
                    fwrite(src + k * 4u, 1, 3, f);
                }
                fclose(f);
            }
        }
    }
    snprintf(msg, sizeof(msg), "%s: %u of %u covered pixels differ", name, bad, cov);
    printf("  %s\n", msg);
    BW_CHECK_EQ(name, cov > 100u, 1);
    BW_CHECK_EQ(name, (double)bad <= max_frac * (double)cov, 1);
}


/* ------------------------------------------------------------------ */
/* The presenter and the 3D composite, read back from the window        */
/* ------------------------------------------------------------------ */

static u8 g_img[W * H * 4];
static u8 g_behind[W * H * 4];

/* A window of the given size; the drawable size may differ on a scaled
 * display, so the tests ask for it and skip what does not apply. */
static int window_pixels(SDL_Window *win, int w, int h, int *dw, int *dh)
{
    SDL_SetWindowSize(win, w, h);
    SDL_GetWindowSizeInPixels(win, dw, dh);
    return *dw > 0 && *dh > 0;
}

/* Exact-multiple output is pure nearest; a larger fractional scale keeps most
 * pixels their own colour (the blend is one output pixel at each seam). */
static void test_presenter(EidosGl *gl, SDL_Window *win)
{
    static const int sizes[][2] = { { 512, 384 }, { 768, 576 }, { 896, 672 } };
    u32 i;
    int dw, dh, k;

    for (i = 0; i < (u32)(W * H); i++) {
        u32 x = i % W, y = i / W;

        g_img[i * 4u + 0] = (u8)(x * 37u + y * 11u);
        g_img[i * 4u + 1] = (u8)(x * 7u + y * 53u);
        g_img[i * 4u + 2] = (u8)(x ^ y);
        g_img[i * 4u + 3] = 255;
    }
    eidos_gl_upload(gl, 0, g_img, W, H);
    for (k = 0; k < 3; k++) {
        SDL_Rect dst[2];
        u8 *rgb;
        u32 same = 0, x, y;

        if (!window_pixels(win, sizes[k][0], sizes[k][1], &dw, &dh)) {
            continue;
        }
        memset(dst, 0, sizeof(dst));
        dst[0].w = dw;
        dst[0].h = dh;
        rgb = (u8 *)malloc((size_t)dw * (size_t)dh * 3u);
        if (rgb == NULL || eidos_gl_grab(gl, dw, dh, dst, rgb) != 0) {
            BW_CHECK_EQ("presenter grab", 0, 1);
            free(rgb);
            continue;
        }
        for (y = 0; y < (u32)dh; y++) {
            for (x = 0; x < (u32)dw; x++) {
                u32 sx = (u32)((double)(x + 0.5) * W / dw);
                u32 sy = (u32)((double)(y + 0.5) * H / dh);
                const u8 *e = g_img + ((size_t)sy * W + sx) * 4u;
                const u8 *g = rgb + ((size_t)y * (size_t)dw + x) * 3u;

                if (e[0] == g[0] && e[1] == g[1] && e[2] == g[2]) {
                    same++;
                }
            }
        }
        printf("  presenter %dx%d: %.1f%% of pixels pure\n", dw, dh,
               100.0 * same / ((double)dw * dh));
        if (dw % W == 0 && dh % H == 0 && dw / W == dh / H) {
            BW_CHECK_EQ("presenter whole multiple is nearest",
                        same == (u32)(dw * dh), 1);
        } else {
            BW_CHECK_EQ("presenter fractional scale keeps pixels sharp",
                        (double)same >= 0.6 * (double)dw * dh, 1);
        }
        free(rgb);
    }
}

/* The GPU 3D layer under the 2D layers: inside the 3D layer's coverage the
 * picture is the GPU frame over what is behind it, everywhere else it is the
 * screen as composed (a 2D overlay stays on top). */
static void check_composite(const char *name, const G3dSnapshot *s, EidosGl *gl,
                            SDL_Window *win)
{
    static u8 soft[512 * 384 * 4];
    SDL_Rect dst[2];
    u8 *rgb;
    int dw, dh, mismatch = 0, x, y;
    u32 i, covered = 0;

    if (!window_pixels(win, 512, 384, &dw, &dh) || dw != 512 || dh != 384) {
        printf("  %s: skipped (drawable %dx%d)\n", name, dw, dh);
        return;
    }
    /* behind: a gradient with an opaque 2D overlay at x >= 200; the screen
     * with the 3D layer on is the sentinel colour except under the overlay */
    for (i = 0; i < (u32)(W * H); i++) {
        u32 px = i % W, py = i / W;
        u8 *b = g_behind + i * 4u, *im = g_img + i * 4u;

        b[0] = 20; b[1] = (u8)(30u + py / 2u); b[2] = (u8)(120u + px / 4u); b[3] = 255;
        if (px >= 200u) {
            b[0] = 220; b[1] = 20; b[2] = 200;
        }
        im[0] = 84; im[1] = 168; im[2] = 52; im[3] = 255;   /* the sentinel */
        if (px >= 200u) {
            im[0] = 220; im[1] = 20; im[2] = 200;
        }
    }
    eidos_gl_upload(gl, 0, g_img, W, H);
    memset(dst, 0, sizeof(dst));
    dst[0].w = dw;
    dst[0].h = dh;
    BW_CHECK_EQ("composite render", eidos_gl_render3d(gl, s, dw, dh), 0);
    eidos_gl_set_3d(gl, 0, g_behind, W, H, 0, 0.f, 0);
    rgb = (u8 *)malloc((size_t)dw * (size_t)dh * 3u);
    if (rgb == NULL || eidos_gl_grab(gl, dw, dh, dst, rgb) != 0) {
        BW_CHECK_EQ("composite grab", 0, 1);
        free(rgb);
        return;
    }
    soft_render_sz(s, soft, (u32)dw, (u32)dh);
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            const u8 *sf = soft + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            const u8 *b = g_behind + ((size_t)(y / 2) * W + (size_t)(x / 2)) * 4u;
            const u8 *g = rgb + ((size_t)y * (size_t)dw + (size_t)x) * 3u;
            double a = sf[3] / 255.0;
            int c, bad = 0;

            if (x / 2 >= 200) {
                a = 0.0;                        /* the overlay stays on top */
            }
            if (a > 0.0) {
                covered++;
            }
            for (c = 0; c < 3; c++) {
                int ex = (int)(sf[c] * a + b[c] * (1.0 - a) + 0.5);

                if (abs(ex - (int)g[c]) > 10) {
                    bad = 1;
                }
            }
            mismatch += bad;
        }
    }
    printf("  %s: %d of %u covered pixels differ\n", name, mismatch, covered);
    BW_CHECK_EQ(name, covered > 1000u, 1);
    BW_CHECK_EQ(name, (double)mismatch <= 0.03 * (double)covered, 1);
    free(rgb);
}

/* The hardware's polygon order: opaque first, then translucent sorted by the
 * top vertex's Y (automatic mode) or as sent (manual mode). */
static void test_sort(void)
{
    G3dVertex v[16];
    u32 idx[16];
    G3dPoly polys[5];
    G3dGeom g;
    u32 i;
    /* sent: 0 translucent high on screen, 1 opaque, 2 translucent low on
     * screen, 3 opaque, 4 translucent in the middle */
    static const u32 alpha[5] = { 14, 31, 14, 31, 20 };
    static const s32 ytop[5] = { 2000, 0, -2000, 0, 0 };

    for (i = 0; i < 5u; i++) {
        v[i * 3u + 0] = mkv(-100, ytop[i], 0, 4096, 0, 0, 255, 255, 255);
        v[i * 3u + 1] = mkv(100, ytop[i] + 500, 0, 4096, 0, 0, 255, 255, 255);
        v[i * 3u + 2] = mkv(0, ytop[i] + 900, 0, 4096, 0, 0, 255, 255, 255);
        idx[i * 3u + 0] = i * 3u;
        idx[i * 3u + 1] = i * 3u + 1u;
        idx[i * 3u + 2] = i * 3u + 2u;
        polys[i].first = i * 3u;
        polys[i].count = 3;
        polys[i].attr = ATTR(alpha[i], i, 1, 1);
        polys[i].teximage = 0;
        polys[i].pltt_base = 0;
    }
    memset(&g, 0, sizeof(g));
    g.verts = v; g.n_verts = 15;
    g.idx = idx; g.n_idx = 15;
    g.polys = polys; g.n_polys = 5;
    g3d_geom_sort(&g, 0);
    /* opaque 1, 3; then translucent from the top of the screen down (clip y
     * grows upwards): 0 (y 2000), 4 (y 0), 2 (y -2000) */
    BW_CHECK_EQ("sort auto 0", polys[0].first / 3u, 1);
    BW_CHECK_EQ("sort auto 1", polys[1].first / 3u, 3);
    BW_CHECK_EQ("sort auto 2", polys[2].first / 3u, 0);
    BW_CHECK_EQ("sort auto 3", polys[3].first / 3u, 4);
    BW_CHECK_EQ("sort auto 4", polys[4].first / 3u, 2);
    for (i = 0; i < 5u; i++) {      /* back to the order sent, then manual */
        polys[i].first = i * 3u;
        polys[i].attr = ATTR(alpha[i], i, 1, 1);
    }
    g3d_geom_sort(&g, 1);
    BW_CHECK_EQ("sort manual 0", polys[0].first / 3u, 1);
    BW_CHECK_EQ("sort manual 1", polys[1].first / 3u, 3);
    BW_CHECK_EQ("sort manual 2", polys[2].first / 3u, 0);
    BW_CHECK_EQ("sort manual 3", polys[3].first / 3u, 2);
    BW_CHECK_EQ("sort manual 4", polys[4].first / 3u, 4);
}

/* Edge marking at a scale.  The outlines are off unless the setting asks
 * for them; thin, they are one window pixel wide at every scale (melonDS's
 * rule: a pixel touching a farther pixel of another ID), so a sliver seen
 * edge-on is a thin line, not a wedge. */
static int count_green(const u8 *out, int ew, int y, int x0, int x1)
{
    int x, n = 0;

    for (x = x0; x < x1; x++) {
        const u8 *px = out + ((size_t)y * (size_t)ew + (size_t)x) * 4u;

        n += px[0] < 40 && px[1] > 200 && px[2] < 40;
    }
    return n;
}

static void test_edge_marking(EidosGl *gl)
{
    enum { SC = 4, EW = W * SC, EH = H * SC };
    static u8 out[EW * EH * 4];
    G3dVertex q[4];
    G3dSnapshot s;
    int mode;

    scene_reset();
    q[0] = mkv(-2000, -2000, 0, 4096, 0, 0, 0, 0, 255);
    q[1] = mkv(2000, -2000, 0, 4096, 0, 0, 0, 0, 255);
    q[2] = mkv(2000, 2000, 0, 4096, 0, 0, 0, 0, 255);
    q[3] = mkv(-2000, 2000, 0, 4096, 0, 0, 0, 0, 255);
    add_poly(q, 4, ATTR(31, 1, 1, 1), 0, -1);
    q[0] = mkv(0, -1500, -2000, 4096, 0, 0, 255, 0, 0);
    q[1] = mkv(10, -1500, -2000, 4096, 0, 0, 255, 0, 0);
    q[2] = mkv(10, 1500, -2000, 4096, 0, 0, 255, 0, 0);
    q[3] = mkv(0, 1500, -2000, 4096, 0, 0, 255, 0, 0);
    add_poly(q, 4, ATTR(31, 2, 1, 1), 0, -1);
    s = make_snap();
    s.disp3dcnt |= 0x20u;
    s.edge[0] = 0x03E0u;                    /* green */
    for (mode = 0; mode < 2; mode++) {
        int y = EH / 2, left, total;

        eidos_gl_set_edges(gl, mode);
        if (eidos_gl_render3d(gl, &s, EW, EH) != 0
            || eidos_gl_read3d(gl, out, EW, EH) != 0) {
            BW_CHECK_EQ("edge marking: render", 0, 1);
            return;
        }
        left = count_green(out, EW, y, 0, EW / 2 - 12);
        total = count_green(out, EW, y, 0, EW);
        printf("  edge marking %s at %dx: %d outline pixels on the left, %d on the row\n",
               mode ? "thin" : "off", SC, left, total);
        if (mode == 0) {
            BW_CHECK_EQ("edge marking off: no outline", total, 0);
        } else {
            BW_CHECK_EQ("edge marking thin: the quad is outlined", left >= 1, 1);
            BW_CHECK_EQ("edge marking thin: one window pixel wide", left <= 2, 1);
            BW_CHECK_EQ("edge marking thin: the sliver is a line, not a wedge", total <= 8, 1);
        }
    }
    eidos_gl_set_edges(gl, 0);
}

int main(void)
{
    SDL_Window *win;
    EidosGl *gl;
    G3dVertex tri[3], quad[4];
    G3dSnapshot s;
    u8 *checker, *half, *partial;
    u32 i;

    setvbuf(stdout, NULL, _IONBF, 0);   /* a crash must not eat the log */
    test_sort();
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("test_gpu3d: SKIP (no video: %s)\n", SDL_GetError());
        return 0;
    }
    win = SDL_CreateWindow("test_gpu3d", W, H,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    printf("  window %p\n", (void *)win);
    gl = win != NULL ? eidos_gl_create(win) : NULL;
    printf("  gl %p\n", (void *)gl);
    if (gl == NULL || !eidos_gl_has3d(gl)) {
        printf("test_gpu3d: SKIP (no OpenGL 3.3 here)\n");
        if (gl != NULL) {
            eidos_gl_destroy(gl);
        }
        if (win != NULL) {
            SDL_DestroyWindow(win);
        }
        SDL_Quit();
        return 0;
    }

    /* textures: an 8x8 checker (opaque) and an 8x4 with a transparent half */
    checker = (u8 *)malloc(8 * 8 * 4);
    half = (u8 *)malloc(8 * 4 * 4);
    for (i = 0; i < 64u; i++) {
        int on = (int)(((i % 8u) / 2u + (i / 8u) / 2u) & 1u);

        checker[i * 4u + 0] = on ? 255 : 40;
        checker[i * 4u + 1] = on ? 200 : 90;
        checker[i * 4u + 2] = on ? 30 : 220;
        checker[i * 4u + 3] = 255;
    }
    for (i = 0; i < 32u; i++) {
        half[i * 4u + 0] = (u8)(i * 7u);
        half[i * 4u + 1] = 180;
        half[i * 4u + 2] = (u8)(255u - i * 7u);
        half[i * 4u + 3] = (i % 8u) < 4u ? 255 : 0;
    }
    g_tex[0].w = 8; g_tex[0].h = 8; g_tex[0].rgba = checker;
    g_tex[1].w = 8; g_tex[1].h = 4; g_tex[1].rgba = half;
    partial = (u8 *)malloc(8 * 4 * 4);
    for (i = 0; i < 32u; i++) {
        partial[i * 4u + 0] = 250;
        partial[i * 4u + 1] = (u8)(40u + i * 5u);
        partial[i * 4u + 2] = 40;
        partial[i * 4u + 3] = (i % 8u) < 4u ? 128 : 255;   /* 5-bit alpha 16, 31 */
    }
    g_tex[2].w = 8; g_tex[2].h = 4; g_tex[2].rgba = partial; g_tex[2].partial = 1;

    /* 1. gouraud triangle, perspective (w varies) */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 0);
    tri[1] = mkv(3200, -2000, 0, 8192, 0, 0, 0, 255, 0);
    tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 0, 0, 255);
    add_poly(tri, 3, ATTR(31, 1, 1, 1), 0, -1);
    s = make_snap();
    check_scene("gouraud triangle", &s, gl, 6, 0.06);

    /* 2. textured modulate quad, repeat and flip */
    scene_reset();
    quad[0] = mkv(-3500, -3000, 0, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3500, -3000, 0, 4096, 8 * 16 * 3, 0, 255, 255, 255);
    quad[2] = mkv(3500, 3000, 0, 4096, 8 * 16 * 3, 8 * 16 * 3, 255, 255, 255);
    quad[3] = mkv(-3500, 3000, 0, 4096, 0, 8 * 16 * 3, 255, 255, 255);
    {
        u32 m;

        for (m = 0; m < 16u; m++) {
            char nm[64];

            scene_reset();
            add_poly(quad, 4, ATTR(31, 2, 1, 1),
                     teximage((int)(m & 1u), (int)((m >> 1) & 1u),
                              (int)((m >> 2) & 1u), (int)((m >> 3) & 1u)), 0);
            s = make_snap();
            snprintf(nm, sizeof(nm), "wrap rs%u rt%u fs%u ft%u", m & 1u,
                     (m >> 1) & 1u, (m >> 2) & 1u, (m >> 3) & 1u);
            check_scene(nm, &s, gl, 8, 0.03);
        }
    }

    /* 2b. a quad whose vertex colours are not planar: the GPU draws it as two
     * triangles, the software rasteriser interpolates along the edges, so the
     * gradient differs a little (tolerance); the shape and texture agree. */
    scene_reset();
    quad[1].r = 220; quad[1].g = 220; quad[1].b = 220;
    quad[3].r = 200; quad[3].g = 200;
    add_poly(quad, 4, ATTR(31, 2, 1, 1), teximage(1, 1, 1, 0), 0);
    s = make_snap();
    check_scene("textured quad, non-planar colours", &s, gl, 40, 0.01);

    /* 3. decal with a half-transparent texture, clamp */
    scene_reset();
    quad[0] = mkv(-2500, -2500, 0, 4096, 0, 0, 255, 60, 60);
    quad[1] = mkv(2500, -2500, 0, 4096, 8 * 16, 0, 255, 60, 60);
    quad[2] = mkv(2500, 2500, 0, 4096, 8 * 16, 4 * 16, 255, 60, 60);
    quad[3] = mkv(-2500, 2500, 0, 4096, 0, 4 * 16, 255, 60, 60);
    add_poly(quad, 4, ATTR(31, 3, 1, 1) | (1u << 4), teximage(0, 0, 0, 0), 1);
    s = make_snap();
    check_scene("decal, half transparent texture", &s, gl, 8, 0.03);

    /* 4. translucent triangle over an opaque one and over the clear plane */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[1] = mkv(1500, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[2] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 255, 0);
    add_poly(tri, 3, ATTR(31, 4, 1, 1), 0, -1);
    tri[0] = mkv(-1000, -3000, 0, 4096, 0, 0, 0, 200, 255);
    tri[1] = mkv(3500, -1000, 0, 4096, 0, 0, 0, 200, 255);
    tri[2] = mkv(-500, 3000, 0, 4096, 0, 0, 0, 200, 255);
    add_poly(tri, 3, ATTR(14, 5, 1, 1), 0, -1);
    s = make_snap();
    check_scene("translucent over opaque and clear", &s, gl, 8, 0.04);

    /* 4b. two translucent triangles of one polygon ID overlap: the second
     * is not drawn where the first is (a third, of another ID, is).  Over
     * an opaque backdrop, so the result does not depend on the clear alpha. */
    scene_reset();
    quad[0] = mkv(-3800, -3800, 2000, 4096, 0, 0, 40, 40, 40);
    quad[1] = mkv(3800, -3800, 2000, 4096, 0, 0, 40, 40, 40);
    quad[2] = mkv(3800, 3800, 2000, 4096, 0, 0, 40, 40, 40);
    quad[3] = mkv(-3800, 3800, 2000, 4096, 0, 0, 40, 40, 40);
    add_poly(quad, 4, ATTR(31, 1, 1, 1), 0, -1);
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 0, 120, 255);
    tri[1] = mkv(1500, -2500, 0, 4096, 0, 0, 0, 120, 255);
    tri[2] = mkv(-3000, 2000, 0, 4096, 0, 0, 0, 120, 255);
    add_poly(tri, 3, ATTR(16, 7, 1, 1), 0, -1);
    tri[0] = mkv(-1000, -3000, 0, 4096, 0, 0, 255, 120, 0);
    tri[1] = mkv(3500, -1000, 0, 4096, 0, 0, 255, 120, 0);
    tri[2] = mkv(-500, 3000, 0, 4096, 0, 0, 255, 120, 0);
    add_poly(tri, 3, ATTR(16, 7, 1, 1), 0, -1);
    tri[0] = mkv(-2000, 1000, 0, 4096, 0, 0, 0, 255, 0);
    tri[1] = mkv(2000, 1000, 0, 4096, 0, 0, 0, 255, 0);
    tri[2] = mkv(0, -3500, 0, 4096, 0, 0, 0, 255, 0);
    add_poly(tri, 3, ATTR(16, 9, 1, 1), 0, -1);
    s = make_snap();
    check_scene("translucent polygons of one ID do not stack", &s, gl, 8, 0.04);

    /* 5. depth test, both draw orders */
    for (i = 0; i < 2u; i++) {
        scene_reset();
        quad[0] = mkv(-3000, -3000, 0, 4096, 0, 0, 255, 0, 0);
        quad[1] = mkv(2000, -3000, 0, 4096, 0, 0, 255, 0, 0);
        quad[2] = mkv(2000, 2000, 0, 4096, 0, 0, 255, 0, 0);
        quad[3] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 0, 0);
        if (i == 1u) {
            add_poly(quad, 4, ATTR(31, 6, 1, 1), 0, -1);
        }
        {
            G3dVertex far[4];

            far[0] = mkv(-1000, -1000, 2000, 4096, 0, 0, 0, 255, 0);
            far[1] = mkv(3500, -1000, 2000, 4096, 0, 0, 0, 255, 0);
            far[2] = mkv(3500, 3000, 2000, 4096, 0, 0, 0, 255, 0);
            far[3] = mkv(-1000, 3000, 2000, 4096, 0, 0, 0, 255, 0);
            add_poly(far, 4, ATTR(31, 7, 1, 1), 0, -1);
        }
        if (i == 0u) {
            add_poly(quad, 4, ATTR(31, 6, 1, 1), 0, -1);
        }
        s = make_snap();
        check_scene(i == 0u ? "depth: near drawn last" : "depth: near drawn first",
                    &s, gl, 4, 0.02);
    }

    /* 6. toon and highlight shading */
    for (i = 0; i < 2u; i++) {
        scene_reset();
        tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 20, 0, 0);
        tri[1] = mkv(3200, -2000, 0, 4096, 0, 0, 250, 0, 0);
        tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 120, 0, 0);
        add_poly(tri, 3, ATTR(31, 8, 1, 1) | (2u << 4), 0, -1);
        s = make_snap();
        s.highlight = i;
        check_scene(i == 0u ? "toon" : "highlight", &s, gl, 8, 0.04);
    }

    /* 7. culling: a back face with back culled draws nothing; the alpha test */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 255);
    tri[1] = mkv(200, 3000, 0, 4096, 0, 0, 255, 0, 255);
    tri[2] = mkv(3200, -2000, 0, 4096, 0, 0, 255, 0, 255);
    add_poly(tri, 3, ATTR(31, 9, 1, 0), 0, -1);
    s = make_snap();
    {
        static u8 gpu[W * H * 4];
        u32 any = 0;

        BW_CHECK_EQ("cull render", eidos_gl_render3d(gl, &s, W, H), 0);
        BW_CHECK_EQ("cull read", eidos_gl_read3d(gl, gpu, W, H), 0);
        for (i = 0; i < (u32)W * H; i++) {
            any |= gpu[i * 4u + 3u];
        }
        BW_CHECK_EQ("culled face draws nothing", any, 0);
    }
    scene_reset();
    quad[0] = mkv(-3500, -3000, 0, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3500, -3000, 0, 4096, 8 * 16, 0, 255, 255, 255);
    quad[2] = mkv(3500, 3000, 0, 4096, 8 * 16, 4 * 16, 255, 255, 255);
    quad[3] = mkv(-3500, 3000, 0, 4096, 0, 4 * 16, 255, 255, 255);
    add_poly(quad, 4, ATTR(31, 10, 1, 1), teximage(0, 0, 0, 0), 1);
    s = make_snap();
    s.alpha_ref = 8;
    check_scene("alpha test", &s, gl, 8, 0.03);

    /* depth: an opaque polygon with partly transparent texels writes depth only
     * where the final alpha is opaque (two passes on the GPU) */
    scene_reset();
    quad[0] = mkv(-3000, -3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[1] = mkv(3000, -3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[2] = mkv(3000, 3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[3] = mkv(-3000, 3000, 3000, 4096, 0, 0, 40, 255, 40);
    add_poly(quad, 4, ATTR(31, 11, 1, 1), 0, -1);               /* far, green */
    quad[0] = mkv(-3000, -3000, -1000, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3000, -3000, -1000, 4096, 8 * 16, 0, 255, 255, 255);
    quad[2] = mkv(3000, 3000, -1000, 4096, 8 * 16, 4 * 16, 255, 255, 255);
    quad[3] = mkv(-3000, 3000, -1000, 4096, 0, 4 * 16, 255, 255, 255);
    add_poly(quad, 4, ATTR(31, 12, 1, 1), teximage(0, 0, 0, 0), 2);   /* near */
    quad[0] = mkv(-3000, -3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[1] = mkv(3000, -3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[2] = mkv(3000, 3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[3] = mkv(-3000, 3000, 1000, 4096, 0, 0, 250, 20, 20);
    add_poly(quad, 4, ATTR(31, 13, 1, 1), 0, -1);               /* middle, red */
    s = make_snap();
    check_scene("partly transparent texels and depth", &s, gl, 8, 0.03);

    /* anti-aliasing (DISP3DCNT bit 4): edge pixels get partial coverage */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 0);
    tri[1] = mkv(3200, -2000, 0, 4096, 0, 0, 255, 0, 0);
    tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 255, 0, 0);
    add_poly(tri, 3, ATTR(31, 1, 1, 1), 0, -1);
    for (i = 0; i < 2u; i++) {
        static u8 gpu[W * H * 4];
        u32 k, edge = 0;

        s = make_snap();
        s.disp3dcnt |= i ? 0x10u : 0u;
        BW_CHECK_EQ("aa render", eidos_gl_render3d(gl, &s, W, H), 0);
        BW_CHECK_EQ("aa read", eidos_gl_read3d(gl, gpu, W, H), 0);
        for (k = 0; k < (u32)W * H; k++) {
            if (gpu[k * 4u + 3u] != 0 && gpu[k * 4u + 3u] != 255) {
                edge++;
            }
        }
        printf("  anti-aliasing %s: %u edge pixels\n", i ? "on" : "off", edge);
        BW_CHECK_EQ(i ? "aa on has partial edges" : "aa off has hard edges",
                    i ? edge > 100u : edge == 0u, 1);
    }

    test_presenter(gl, win);

    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 0);
    tri[1] = mkv(3200, -2000, 0, 8192, 0, 0, 0, 255, 0);
    tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 0, 0, 255);
    add_poly(tri, 3, ATTR(31, 1, 1, 1), 0, -1);
    s = make_snap();
    check_composite("composite: opaque triangle", &s, gl, win);
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[1] = mkv(1500, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[2] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 255, 0);
    add_poly(tri, 3, ATTR(31, 4, 1, 1), 0, -1);
    tri[0] = mkv(-1000, -3000, 0, 4096, 0, 0, 0, 200, 255);
    tri[1] = mkv(3500, -1000, 0, 4096, 0, 0, 0, 200, 255);
    tri[2] = mkv(-500, 3000, 0, 4096, 0, 0, 0, 200, 255);
    add_poly(tri, 3, ATTR(14, 5, 1, 1), 0, -1);
    s = make_snap();
    check_composite("composite: translucent over opaque", &s, gl, win);

    test_edge_marking(gl);

    free(checker);
    free(half);
    free(partial);
    eidos_gl_destroy(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    BW_REPORT("test_gpu3d");
}
