/*
 * engine/platform/plat_gl.h -- the GPU presenter.
 *
 * The two DS screens are uploaded as RGBA textures (any size, so the 2D can
 * stay 256x192 while a 3D layer is larger) and drawn by one fragment shader
 * to the window at its own resolution.  The shader keeps pixels sharp at any
 * scale, with soft edges only where the ratio is fractional.  The CPU only
 * uploads; all scaling happens on the GPU.  OpenGL 3.3 core (the context SDL
 * creates on Windows, Linux and macOS).  If it cannot be created the
 * platform layer falls back to SDL's renderer.
 */
#ifndef EIDOS_PLAT_GL_H
#define EIDOS_PLAT_GL_H

#include <SDL3/SDL.h>

typedef struct EidosGl EidosGl;

/* The window must have been created with SDL_WINDOW_OPENGL.  NULL on any
 * failure (the caller then recreates the window without the flag). */
EidosGl *eidos_gl_create(SDL_Window *window);
void eidos_gl_destroy(EidosGl *gl);

/* The presenter's third image: the run-status panel (engine/platform/
 * mod_panel_draw.h), drawn beside the game's screens. */
void eidos_gl_upload_panel(EidosGl *gl, const unsigned char *rgba, int w, int h);
void eidos_gl_present_panel(EidosGl *gl, int out_w, int out_h, const SDL_Rect dst[2],
                            const SDL_Rect *panel);
/* SDL_GL_SwapWindow, its vblank wait counted as the pacing's (prof.h). */
void eidos_gl_swap(EidosGl *gl);
/* VSync on or off: the swap waits for the vblank or not. */
void eidos_gl_set_vsync(EidosGl *gl, int on);

/* Replace screen `i`'s image: w x h RGBA bytes. */
void eidos_gl_upload(EidosGl *gl, int i, const unsigned char *rgba, int w,
                     int h);

/* Clear and draw both screens into destination rectangles (window pixels,
 * y down) of an output out_w x out_h, then swap. */
void eidos_gl_present(EidosGl *gl, int out_w, int out_h, const SDL_Rect dst[2]);

/* Draw both screens as present does, but read the window's pixels instead of
 * swapping: rgb is out_w x out_h x 3 bytes, top-down.  0 on success. */
int eidos_gl_grab(EidosGl *gl, int out_w, int out_h, const SDL_Rect dst[2],
                  unsigned char *rgb);

/*
 * The one-screen layout (engine/ui/ui.h; the presenter's own state, set each
 * frame before the present).  _set turns it on or off; with `world` the
 * window shows DS screen `screen` whose 3D was rendered at the window's
 * size (eidos_gl_render3d with the window's w and h), the screen's 2D
 * layers in `ds` (window pixels), `hide_2d` leaving them out and `blur`
 * blurring and darkening the world.  Without `world` (the fallback) the two
 * screens are drawn into the rectangles given to present.  _canvas shows the
 * interface picture, premultiplied RGBA w x h, in `rect` (window pixels,
 * NULL: none) over either; pass rgba only when the picture changed.
 * _3d_matches says the last 3D frame is `screen`'s and was rendered at
 * out_w x out_h.
 */
void eidos_gl_one_set(EidosGl *gl, int on, int world, int screen,
                      const SDL_Rect *ds, int hide_2d, int blur, int no_fade);
/* no_fade: bit 0 the game's fade is not applied to the world, bit 1 the last
 * picture of the world is shown as it was (EidosUiView.hold_world), bit 2
 * this world is the one to keep that picture of (EidosUiView.keep_world). */
void eidos_gl_one_canvas(EidosGl *gl, const unsigned char *rgba, int w, int h,
                         const SDL_Rect *rect);
/* Window pixels at the bottom the world leaves free (EidosUiView.bottom):
 * it is drawn above them; 0 for the whole window. */
void eidos_gl_one_bottom(EidosGl *gl, int px);
int  eidos_gl_3d_matches(const EidosGl *gl, int screen, int out_w, int out_h);

/* The GPU 3D rasteriser (plat_gl3d.c).  has3d is false when the optional part
 * failed to build; then the platform keeps drawing screens only. */
struct G3dSnapshot;
int eidos_gl_has3d(const EidosGl *gl);

/* The 2D compositor on the GPU (plat_gl2d.c, docs/gpu-2d.md): composes one
 * engine's frame into its 256 x 192 target; with `readback` copies it out
 * (256 x 192 RGBA, DS row 0 first), with `to_screen` 0 or 1 into that
 * screen's texture in place of an upload.  -1 when the GPU path is not
 * available. */
struct G2dGpuFrame;
int eidos_gl2d_compose(EidosGl *gl, const struct G2dGpuFrame *f, unsigned char *readback,
                       int to_screen);
/* 1 when the 2D shader is built (or builds now). */
int eidos_gl2d_ready(EidosGl *gl);
/* The last screen eidos_gl2d_compose drew to `screen`, read back without a
 * wait (256 x 192 RGBA into `out`); -1 when none came since the last call. */
int eidos_gl2d_pixels(EidosGl *gl, int screen, unsigned char *out);

/* Drawing a tool's window with the GPU (plat_gl_view.c): keep3d copies the
 * last eidos_gl_render3d picture into a texture (made when `tex` is 0);
 * upload makes or refills a texture from straight top-down RGBA; begin
 * clears the window, draw lays a texture over the window rectangle (mode 0
 * a kept 3D picture: clear pixels out, the rest opaque; 1 a sprite: the
 * same, straight; 2 a canvas blended by its alpha; flip for a kept 3D
 * picture; clip x y w h or NULL), read copies the window out top-down,
 * end shows it. */
unsigned eidos_gl_view_keep3d(EidosGl *gl, unsigned tex, int w, int h);
unsigned eidos_gl_view_upload(EidosGl *gl, unsigned tex, const unsigned char *rgba, int w, int h,
                              int same_size);
void eidos_gl_view_free(EidosGl *gl, unsigned tex);
void eidos_gl_view_begin(EidosGl *gl, int win_w, int win_h, unsigned rgb);
void eidos_gl_view_draw(EidosGl *gl, unsigned tex, int mode, int x, int y, int w, int h, int flip,
                        const int *clip);
/* A straight sprite (as mode 1), its colours faded fade[3] (0..1) of the
 * way to fade[0..2] and blended at `alpha` (0..1). */
void eidos_gl_view_draw_fx(EidosGl *gl, unsigned tex, int x, int y, int w, int h, const int *clip,
                           const float fade[4], float alpha);
int eidos_gl_view_read(EidosGl *gl, unsigned char *rgba);
void eidos_gl_view_end(EidosGl *gl);

/* The display capture at the 3D's resolution: see plat_gl.c. */
int eidos_gl_capture(EidosGl *gl, const unsigned char *a2d,
                     const unsigned char *behind,
                     const struct G3dSnapshot *snap, int scale, int blend,
                     int eva, int evb, const unsigned char *b1x);
void eidos_gl_show_capture(EidosGl *gl, int screen, int bmode, float bfac);
/* Draw the snapshot at w x h; 0 on success. */
int eidos_gl_render3d(EidosGl *gl, const struct G3dSnapshot *snap, int w, int h);
/* The outlines the edge-marking pass draws: 0 none (the default), 1 one
 * window pixel wide, 2 one DS pixel wide. */
void eidos_gl_set_edges(EidosGl *gl, int mode);
/* Make screen `screen` (0 top, 1 bottom) show the last rendered 3D frame over
 * `behind`, the screen at DS resolution with its 3D layer off; bmode 0/1/2 and
 * bfac 0..1 are the engine's master brightness.  screen < 0 turns it off. */
void eidos_gl_set_3d(EidosGl *gl, int screen, const unsigned char *behind,
                     int w, int h, int bmode, float bfac, int dilate);
/* Phase 3 (docs/gpu-2d.md): the last rendered 3D frame is screen `screen`'s
 * 3D layer, composed by the 2D shader with the rest of the screen at the
 * frame's size (eidos_gl2d_compose); -1 none.  Turns eidos_gl_set_3d's
 * composite off, and that one turns this off. */
void eidos_gl_set_3d2d(EidosGl *gl, int screen);
/* eidos_gl_set_3d's composite, with its screens (the screen with the
 * sentinel 3D, the 3D off, a black 3D) made by eidos_gl2d_compose. */
void eidos_gl_set_3d_from2d(EidosGl *gl, int screen, int bmode, float bfac);
/* The size that screen is shown at in the window (the composite is made at
 * least that big). */
void eidos_gl_set_3d2d_shown(EidosGl *gl, int w, int h);
/* The one-screen world's colour beyond the DS screen where no 3D is: the
 * screen's backdrop with its master brightness, 0xRRGGBB. */
void eidos_gl_set_world_backdrop(EidosGl *gl, unsigned rgb);
/* The same screen composed with an opaque black 3D layer (w x h RGBA), so
 * the presenter can tell what a 2D layer adds over the 3D and how much of
 * the 3D shows through a blend; NULL for none.  Call before eidos_gl_set_3d. */
void eidos_gl_set_3d_zero(EidosGl *gl, const unsigned char *zero, int w, int h);

/* Test aid: the last 3D frame as top-down, non-premultiplied RGBA, w x h as
 * rendered (the size given to eidos_gl_render3d).  0 on success. */
int eidos_gl_read3d(EidosGl *gl, unsigned char *out, int w, int h);

/* The 3D frame at DS size (256x192 top-down RGBA, what the CPU rasteriser
 * would have drawn), for the 2D compositor and the display capture.  0 new
 * data, 1 unchanged since the last call, -1 failure. */
int eidos_gl_read3d_1x(EidosGl *gl, const struct G3dSnapshot *snap,
                       unsigned char *out, int async);

#endif
