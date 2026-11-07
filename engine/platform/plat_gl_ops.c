/*
 * engine/platform/plat_gl_ops.c -- the OpenGL backend's table of operations
 * (plat_gpu.h).
 *
 * Each entry is the eidos_gl_* call of the same meaning (plat_gl.h), reached
 * through a small function that turns the backend's head back into the GL
 * state it starts (struct EidosGl, plat_gl_int.h).
 */
#include "plat_gl_int.h"

static EidosGl *gl(EidosGpu *g) { return (EidosGl *)g; }
static const EidosGl *glc(const EidosGpu *g) { return (const EidosGl *)g; }

static void op_destroy(EidosGpu *g) { eidos_gl_destroy(gl(g)); }
static void op_upload(EidosGpu *g, int i, const unsigned char *rgba, int w, int h)
{ eidos_gl_upload(gl(g), i, rgba, w, h); }
static void op_present(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2])
{ eidos_gl_present(gl(g), out_w, out_h, dst); }
static int op_grab(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2], unsigned char *rgb)
{ return eidos_gl_grab(gl(g), out_w, out_h, dst, rgb); }
static void op_upload_panel(EidosGpu *g, const unsigned char *rgba, int w, int h)
{ eidos_gl_upload_panel(gl(g), rgba, w, h); }
static void op_present_panel(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2],
                             const SDL_Rect *panel)
{ eidos_gl_present_panel(gl(g), out_w, out_h, dst, panel); }
static void op_one_set(EidosGpu *g, int on, int world, int screen, const SDL_Rect *ds, int hide_2d,
                       int blur, int no_fade)
{ eidos_gl_one_set(gl(g), on, world, screen, ds, hide_2d, blur, no_fade); }
static void op_one_canvas(EidosGpu *g, const unsigned char *rgba, int w, int h, const SDL_Rect *rect)
{ eidos_gl_one_canvas(gl(g), rgba, w, h, rect); }
static void op_one_bottom(EidosGpu *g, int px) { eidos_gl_one_bottom(gl(g), px); }
static int op_matches_3d(const EidosGpu *g, int screen, int out_w, int out_h)
{ return eidos_gl_3d_matches(glc(g), screen, out_w, out_h); }
static int op_compose_2d(EidosGpu *g, const struct G2dGpuFrame *f, unsigned char *readback,
                         int to_screen)
{ return eidos_gl2d_compose(gl(g), f, readback, to_screen); }
static int op_ready_2d(EidosGpu *g) { return eidos_gl2d_ready(gl(g)); }
static int op_pixels_2d(EidosGpu *g, int screen, unsigned char *out)
{ return eidos_gl2d_pixels(gl(g), screen, out); }
static unsigned op_view_keep3d(EidosGpu *g, unsigned tex, int w, int h)
{ return eidos_gl_view_keep3d(gl(g), tex, w, h); }
static unsigned op_view_upload(EidosGpu *g, unsigned tex, const unsigned char *rgba, int w, int h,
                               int same_size)
{ return eidos_gl_view_upload(gl(g), tex, rgba, w, h, same_size); }
static void op_view_free(EidosGpu *g, unsigned tex) { eidos_gl_view_free(gl(g), tex); }
static void op_view_begin(EidosGpu *g, int win_w, int win_h, unsigned rgb)
{ eidos_gl_view_begin(gl(g), win_w, win_h, rgb); }
static void op_view_draw(EidosGpu *g, unsigned tex, int mode, int x, int y, int w, int h, int flip,
                         const int *clip)
{ eidos_gl_view_draw(gl(g), tex, mode, x, y, w, h, flip, clip); }
static void op_view_draw_fx(EidosGpu *g, unsigned tex, int x, int y, int w, int h, const int *clip,
                            const float fade[4], float alpha)
{ eidos_gl_view_draw_fx(gl(g), tex, x, y, w, h, clip, fade, alpha); }
static int op_view_read(EidosGpu *g, unsigned char *rgba) { return eidos_gl_view_read(gl(g), rgba); }
static void op_view_end(EidosGpu *g) { eidos_gl_view_end(gl(g)); }
static int op_has3d(const EidosGpu *g) { return eidos_gl_has3d(glc(g)); }
static int op_capture(EidosGpu *g, const unsigned char *a2d, const unsigned char *behind,
                      const struct G3dSnapshot *snap, int scale, int blend, int eva, int evb,
                      const unsigned char *b1x)
{ return eidos_gl_capture(gl(g), a2d, behind, snap, scale, blend, eva, evb, b1x); }
static void op_show_capture(EidosGpu *g, int screen, int bmode, float bfac)
{ eidos_gl_show_capture(gl(g), screen, bmode, bfac); }
static int op_render3d(EidosGpu *g, const struct G3dSnapshot *snap, int w, int h)
{ return eidos_gl_render3d(gl(g), snap, w, h); }
static void op_set_edges(EidosGpu *g, int mode) { eidos_gl_set_edges(gl(g), mode); }
static void op_set_3d(EidosGpu *g, int screen, const unsigned char *behind, int w, int h, int bmode,
                      float bfac, int dilate)
{ eidos_gl_set_3d(gl(g), screen, behind, w, h, bmode, bfac, dilate); }
static void op_set_3d2d(EidosGpu *g, int screen) { eidos_gl_set_3d2d(gl(g), screen); }
static void op_set_3d_from2d(EidosGpu *g, int screen, int bmode, float bfac)
{ eidos_gl_set_3d_from2d(gl(g), screen, bmode, bfac); }
static void op_set_3d2d_shown(EidosGpu *g, int w, int h) { eidos_gl_set_3d2d_shown(gl(g), w, h); }
static void op_set_world_backdrop(EidosGpu *g, unsigned rgb)
{ eidos_gl_set_world_backdrop(gl(g), rgb); }
static void op_set_3d_zero(EidosGpu *g, const unsigned char *zero, int w, int h)
{ eidos_gl_set_3d_zero(gl(g), zero, w, h); }
static int op_read3d(EidosGpu *g, unsigned char *out, int w, int h)
{ return eidos_gl_read3d(gl(g), out, w, h); }
static int op_read3d_1x(EidosGpu *g, const struct G3dSnapshot *snap, unsigned char *out, int async)
{ return eidos_gl_read3d_1x(gl(g), snap, out, async); }
/* swap interval 1: the swap waits for the vblank (the driver queues no
 * more than it must for that) */
static void op_set_vsync(EidosGpu *g, int on)
{
    eidos_gl_set_vsync(gl(g), on);
}

static const EidosGpuOps g_gl_ops = {
    "OpenGL",
    op_destroy,
    op_upload, op_present, op_grab, op_upload_panel, op_present_panel,
    op_one_set, op_one_canvas, op_one_bottom, op_matches_3d,
    op_compose_2d, op_ready_2d, op_pixels_2d,
    op_view_keep3d, op_view_upload, op_view_free, op_view_begin, op_view_draw, op_view_draw_fx,
    op_view_read, op_view_end,
    op_has3d, op_capture, op_show_capture, op_render3d, op_set_edges, op_set_3d, op_set_3d2d,
    op_set_3d_from2d, op_set_3d2d_shown, op_set_world_backdrop, op_set_3d_zero, op_read3d,
    op_read3d_1x,
    op_set_vsync,
};

EidosGpu *eidos_gpu_create_gl(SDL_Window *window)
{
    EidosGl *g = eidos_gl_create(window);

    if (g == NULL) {
        return NULL;
    }
    g->base.ops = &g_gl_ops;
    return &g->base;
}
