/*
 * engine/platform/plat_gpu.h -- the GPU backend the platform layer draws
 * with (docs/graphics-backends.md).
 *
 * The platform layer (platform.c) calls only the eidos_gpu_* calls below.
 * Each one goes to the backend the window was opened with, through its
 * table of operations.  A backend's own state starts with an EidosGpu, so
 * the platform layer holds one pointer whatever the backend is.
 *
 * Backends:
 *   OpenGL 3.3 core / OpenGL ES 3.0 (plat_gl*.c, the table in plat_gl_ops.c)
 *
 * Every operation means what its eidos_gl_* counterpart in plat_gl.h means;
 * that header documents them.  A backend fills every entry.
 */
#ifndef EIDOS_PLAT_GPU_H
#define EIDOS_PLAT_GPU_H

#include <SDL3/SDL.h>

struct G3dSnapshot;
struct G2dGpuFrame;
typedef struct EidosGpu EidosGpu;

typedef struct EidosGpuOps {
    const char *name;                       /* for the log: "OpenGL", ... */
    void (*destroy)(EidosGpu *g);

    /* the two DS screens and the present */
    void (*upload)(EidosGpu *g, int i, const unsigned char *rgba, int w, int h);
    void (*present)(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2]);
    int  (*grab)(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2], unsigned char *rgb);
    void (*upload_panel)(EidosGpu *g, const unsigned char *rgba, int w, int h);
    void (*present_panel)(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2],
                          const SDL_Rect *panel);

    /* the one-screen layout */
    void (*one_set)(EidosGpu *g, int on, int world, int screen, const SDL_Rect *ds,
                    int hide_2d, int blur, int no_fade);
    void (*one_canvas)(EidosGpu *g, const unsigned char *rgba, int w, int h,
                       const SDL_Rect *rect);
    void (*one_bottom)(EidosGpu *g, int px);
    int  (*matches_3d)(const EidosGpu *g, int screen, int out_w, int out_h);

    /* the 2D compositor */
    int  (*compose_2d)(EidosGpu *g, const struct G2dGpuFrame *f, unsigned char *readback,
                       int to_screen);
    int  (*ready_2d)(EidosGpu *g);
    int  (*pixels_2d)(EidosGpu *g, int screen, unsigned char *out);

    /* a tool's window */
    unsigned (*view_keep3d)(EidosGpu *g, unsigned tex, int w, int h);
    unsigned (*view_upload)(EidosGpu *g, unsigned tex, const unsigned char *rgba, int w, int h,
                            int same_size);
    void (*view_free)(EidosGpu *g, unsigned tex);
    void (*view_begin)(EidosGpu *g, int win_w, int win_h, unsigned rgb);
    void (*view_draw)(EidosGpu *g, unsigned tex, int mode, int x, int y, int w, int h, int flip,
                      const int *clip);
    void (*view_draw_fx)(EidosGpu *g, unsigned tex, int x, int y, int w, int h, const int *clip,
                         const float fade[4], float alpha);
    int  (*view_read)(EidosGpu *g, unsigned char *rgba);
    void (*view_end)(EidosGpu *g);

    /* the 3D */
    int  (*has3d)(const EidosGpu *g);
    int  (*capture)(EidosGpu *g, const unsigned char *a2d, const unsigned char *behind,
                    const struct G3dSnapshot *snap, int scale, int blend, int eva, int evb,
                    const unsigned char *b1x);
    void (*show_capture)(EidosGpu *g, int screen, int bmode, float bfac);
    int  (*render3d)(EidosGpu *g, const struct G3dSnapshot *snap, int w, int h);
    void (*set_edges)(EidosGpu *g, int mode);
    void (*set_3d)(EidosGpu *g, int screen, const unsigned char *behind, int w, int h,
                   int bmode, float bfac, int dilate);
    void (*set_3d2d)(EidosGpu *g, int screen);
    void (*set_3d_from2d)(EidosGpu *g, int screen, int bmode, float bfac);
    void (*set_3d2d_shown)(EidosGpu *g, int w, int h);
    void (*set_world_backdrop)(EidosGpu *g, unsigned rgb);
    void (*set_3d_zero)(EidosGpu *g, const unsigned char *zero, int w, int h);
    int  (*read3d)(EidosGpu *g, unsigned char *out, int w, int h);
    int  (*read3d_1x)(EidosGpu *g, const struct G3dSnapshot *snap, unsigned char *out, int async);

    /* The present waits for the display's vblank (on), at most one frame
     * queued, or shows at once (off: the game's pacer alone times it). */
    void (*set_vsync)(EidosGpu *g, int on);
} EidosGpuOps;

/* The head of every backend's state. */
struct EidosGpu {
    const EidosGpuOps *ops;
};

/* The OpenGL backend over a window made with SDL_WINDOW_OPENGL; NULL when
 * the context or the shaders fail (plat_gl_ops.c). */
EidosGpu *eidos_gpu_create_gl(SDL_Window *window);

/* The SDL3 GPU backend over a window made without SDL_WINDOW_OPENGL:
 * `driver` "direct3d12", "vulkan" or "metal", NULL for SDL's choice; NULL
 * when no device or pipeline comes up (plat_sdlgpu.c).  _driver names the
 * one it runs on. */
EidosGpu *eidos_gpu_create_sdl(SDL_Window *window, const char *driver);
const char *eidos_gpu_sdl_driver(const EidosGpu *g);

/* The calls the platform layer makes. */
static inline const char *eidos_gpu_name(const EidosGpu *g) { return g->ops->name; }
static inline void eidos_gpu_destroy(EidosGpu *g) { if (g != NULL) g->ops->destroy(g); }
static inline void eidos_gpu_upload(EidosGpu *g, int i, const unsigned char *rgba, int w, int h)
{ g->ops->upload(g, i, rgba, w, h); }
static inline void eidos_gpu_present(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2])
{ g->ops->present(g, out_w, out_h, dst); }
static inline int eidos_gpu_grab(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2],
                                 unsigned char *rgb)
{ return g->ops->grab(g, out_w, out_h, dst, rgb); }
static inline void eidos_gpu_upload_panel(EidosGpu *g, const unsigned char *rgba, int w, int h)
{ g->ops->upload_panel(g, rgba, w, h); }
static inline void eidos_gpu_present_panel(EidosGpu *g, int out_w, int out_h, const SDL_Rect dst[2],
                                           const SDL_Rect *panel)
{ g->ops->present_panel(g, out_w, out_h, dst, panel); }
static inline void eidos_gpu_one_set(EidosGpu *g, int on, int world, int screen, const SDL_Rect *ds,
                                     int hide_2d, int blur, int no_fade)
{ g->ops->one_set(g, on, world, screen, ds, hide_2d, blur, no_fade); }
static inline void eidos_gpu_one_canvas(EidosGpu *g, const unsigned char *rgba, int w, int h,
                                        const SDL_Rect *rect)
{ g->ops->one_canvas(g, rgba, w, h, rect); }
static inline void eidos_gpu_one_bottom(EidosGpu *g, int px) { g->ops->one_bottom(g, px); }
static inline int eidos_gpu_3d_matches(const EidosGpu *g, int screen, int out_w, int out_h)
{ return g->ops->matches_3d(g, screen, out_w, out_h); }
static inline int eidos_gpu_2d_compose(EidosGpu *g, const struct G2dGpuFrame *f,
                                       unsigned char *readback, int to_screen)
{ return g->ops->compose_2d(g, f, readback, to_screen); }
static inline int eidos_gpu_2d_ready(EidosGpu *g) { return g->ops->ready_2d(g); }
static inline int eidos_gpu_2d_pixels(EidosGpu *g, int screen, unsigned char *out)
{ return g->ops->pixels_2d(g, screen, out); }
static inline unsigned eidos_gpu_view_keep3d(EidosGpu *g, unsigned tex, int w, int h)
{ return g->ops->view_keep3d(g, tex, w, h); }
static inline unsigned eidos_gpu_view_upload(EidosGpu *g, unsigned tex, const unsigned char *rgba,
                                             int w, int h, int same_size)
{ return g->ops->view_upload(g, tex, rgba, w, h, same_size); }
static inline void eidos_gpu_view_free(EidosGpu *g, unsigned tex) { g->ops->view_free(g, tex); }
static inline void eidos_gpu_view_begin(EidosGpu *g, int win_w, int win_h, unsigned rgb)
{ g->ops->view_begin(g, win_w, win_h, rgb); }
static inline void eidos_gpu_view_draw(EidosGpu *g, unsigned tex, int mode, int x, int y, int w,
                                       int h, int flip, const int *clip)
{ g->ops->view_draw(g, tex, mode, x, y, w, h, flip, clip); }
static inline void eidos_gpu_view_draw_fx(EidosGpu *g, unsigned tex, int x, int y, int w, int h,
                                          const int *clip, const float fade[4], float alpha)
{ g->ops->view_draw_fx(g, tex, x, y, w, h, clip, fade, alpha); }
static inline int eidos_gpu_view_read(EidosGpu *g, unsigned char *rgba)
{ return g->ops->view_read(g, rgba); }
static inline void eidos_gpu_view_end(EidosGpu *g) { g->ops->view_end(g); }
static inline int eidos_gpu_has3d(const EidosGpu *g) { return g->ops->has3d(g); }
static inline int eidos_gpu_capture(EidosGpu *g, const unsigned char *a2d,
                                    const unsigned char *behind, const struct G3dSnapshot *snap,
                                    int scale, int blend, int eva, int evb,
                                    const unsigned char *b1x)
{ return g->ops->capture(g, a2d, behind, snap, scale, blend, eva, evb, b1x); }
static inline void eidos_gpu_show_capture(EidosGpu *g, int screen, int bmode, float bfac)
{ g->ops->show_capture(g, screen, bmode, bfac); }
static inline int eidos_gpu_render3d(EidosGpu *g, const struct G3dSnapshot *snap, int w, int h)
{ return g->ops->render3d(g, snap, w, h); }
static inline void eidos_gpu_set_edges(EidosGpu *g, int mode) { g->ops->set_edges(g, mode); }
static inline void eidos_gpu_set_3d(EidosGpu *g, int screen, const unsigned char *behind, int w,
                                    int h, int bmode, float bfac, int dilate)
{ g->ops->set_3d(g, screen, behind, w, h, bmode, bfac, dilate); }
static inline void eidos_gpu_set_3d2d(EidosGpu *g, int screen) { g->ops->set_3d2d(g, screen); }
static inline void eidos_gpu_set_3d_from2d(EidosGpu *g, int screen, int bmode, float bfac)
{ g->ops->set_3d_from2d(g, screen, bmode, bfac); }
static inline void eidos_gpu_set_3d2d_shown(EidosGpu *g, int w, int h)
{ g->ops->set_3d2d_shown(g, w, h); }
static inline void eidos_gpu_set_world_backdrop(EidosGpu *g, unsigned rgb)
{ g->ops->set_world_backdrop(g, rgb); }
static inline void eidos_gpu_set_3d_zero(EidosGpu *g, const unsigned char *zero, int w, int h)
{ g->ops->set_3d_zero(g, zero, w, h); }
static inline int eidos_gpu_read3d(EidosGpu *g, unsigned char *out, int w, int h)
{ return g->ops->read3d(g, out, w, h); }
static inline int eidos_gpu_read3d_1x(EidosGpu *g, const struct G3dSnapshot *snap,
                                      unsigned char *out, int async)
{ return g->ops->read3d_1x(g, snap, out, async); }
static inline void eidos_gpu_set_vsync(EidosGpu *g, int on) { g->ops->set_vsync(g, on); }

#endif
