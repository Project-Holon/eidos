/*
 * engine/platform/platform.h -- the host platform layer: the window, the
 * keyboard, gamepads, the mouse as the stylus, the audio device and the
 * frame pacer.
 *
 * This is the only part of eidos that includes SDL3, and it is the only part
 * that is optional: the engine library, the shim, the stubs and every test
 * build and run without it (CMakeLists.txt, EIDOS_SDL).  Nothing here knows
 * anything about the game; it hands the frame loop in engine/frame/ two
 * 256x192 RGBA buffers and a button word, and that is the whole contract.
 *
 * Where the numbers come from:
 *
 *   - The DS is two 256x192 screens.  The window is an integer multiple of
 *     that layout, stacked vertically (the hardware's own arrangement) or
 *     side by side, because a non-integer scale resamples a picture whose
 *     every pixel the 2D engine placed exactly.
 *   - The DS frame rate is the system clock over the frame's clock count,
 *     33513982 / 560190 = 59.8261 Hz, not 60.  The pacer keeps the exact
 *     rational period and accumulates the sub-nanosecond remainder, so a
 *     long session does not drift the way a 16 ms or 16.667 ms constant
 *     would (16 ms is 4.5 seconds fast per hour).
 *   - The audio device is 32768 Hz stereo signed 16-bit, which is the DS
 *     mixer's own output rate; the callback is silent until the sequencer
 *     exists (Milestone 5).
 *   - The button word is the DS keypad bit assignment (KEYINPUT / the SDK's
 *     PAD_* bits), active high here: a set bit means the button is down.
 *     Keeping the hardware's bit order means the matched input code can be
 *     handed this word unchanged when it arrives.
 *   - Every pump hands the buttons and the stylus to the port's input layer
 *     (sdk/input.h, bw_input_set_host), which puts them where the game
 *     reads them at the next frame boundary.  The bindings are tables a
 *     caller may replace; none is hard-coded outside the default tables.
 */

#ifndef EIDOS_ENGINE_PLATFORM_H
#define EIDOS_ENGINE_PLATFORM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

/*
 * The DS keypad, in the hardware's own bit order.  On hardware KEYINPUT is
 * active low; this word is active high (a set bit is a pressed button),
 * because every caller in the port asks "is A down", and the inversion
 * belongs at the one place that reads the register, not at fifty call sites.
 */
#define EIDOS_PAD_A      0x0001u
#define EIDOS_PAD_B      0x0002u
#define EIDOS_PAD_SELECT 0x0004u
#define EIDOS_PAD_START  0x0008u
#define EIDOS_PAD_RIGHT  0x0010u
#define EIDOS_PAD_LEFT   0x0020u
#define EIDOS_PAD_UP     0x0040u
#define EIDOS_PAD_DOWN   0x0080u
#define EIDOS_PAD_R      0x0100u
#define EIDOS_PAD_L      0x0200u
#define EIDOS_PAD_X      0x0400u
#define EIDOS_PAD_Y      0x0800u
#define EIDOS_PAD_DEBUG  0x2000u    /* the debug button (DS development units) */

/*
 * One keyboard binding.  `scancode` is an SDL_Scancode value, a physical key
 * position rather than a character, so the defaults land on the same keys on
 * a QWERTZ or AZERTY keyboard as on a QWERTY one.  Several bindings may name
 * the same button; the button is down when any of them is.
 */
typedef struct EidosKeyBinding {
    int scancode;
    u32 button;
} EidosKeyBinding;

/*
 * The default table, documented in docs/runtime.md:
 *
 *     d-pad        the arrow keys
 *     A            X          B            Z
 *     X            S          Y            A
 *     L            Q          R            E
 *     START        Return     SELECT       Right Shift, Backspace
 *
 * No key is bound to two buttons: the word is rebuilt from the whole table
 * on every pump, so a key that appeared twice would press both buttons.
 *
 * Escape quits, F12 writes a screenshot.  Those two are the platform
 * layer's own and are not in the table.
 */
const EidosKeyBinding *eidos_default_keymap(u32 *count);

/*
 * One gamepad binding, through SDL's gamepad API (every pad SDL's
 * controller database knows, under one positional layout).  `kind` says
 * what `index` is: an SDL_GamepadButton, or an SDL_GamepadAxis
 * read past half its travel in the positive or the negative direction.
 * Every open pad is read through the same table; any binding held presses
 * its button.
 */
#define EIDOS_PADBIND_BUTTON   0
#define EIDOS_PADBIND_AXIS_POS 1
#define EIDOS_PADBIND_AXIS_NEG 2

typedef struct EidosPadBinding {
    int kind;
    int index;
    u32 button;
} EidosPadBinding;

/*
 * The default table.  SDL names a pad's face buttons by position (south,
 * east, west, north, on Nintendo pads too), and the defaults keep the DS's
 * own positions: DS A is east, B south, X north, Y west.
 *
 *     A            east               B            south
 *     X            north              Y            west
 *     L, R         the shoulders and the triggers
 *     START        Start              SELECT       Back
 *     d-pad        the d-pad and the left stick
 */
const EidosPadBinding *eidos_default_padmap(u32 *count);

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

#define EIDOS_DS_SCREEN_WIDTH  256u
#define EIDOS_DS_SCREEN_HEIGHT 192u

#define EIDOS_LAYOUT_STACKED       0   /* top screen above the bottom one */
#define EIDOS_LAYOUT_SIDE_BY_SIDE  1
#define EIDOS_LAYOUT_ONE           2   /* one screen: the world fills the
                                          window, the interface over it
                                          (engine/ui/ui.h)               */
#define EIDOS_LAYOUT_VERTICAL      EIDOS_LAYOUT_STACKED
#define EIDOS_LAYOUT_HORIZONTAL    EIDOS_LAYOUT_SIDE_BY_SIDE

#define EIDOS_DISPLAY_WINDOWED     0
/* Neither fullscreen mode ever changes the OS video mode: both are
 * SDL3's desktop fullscreen (desktop-sized, picture scaled by the
 * presenter).  They are kept as two values for the settings file only. */
#define EIDOS_DISPLAY_FULLSCREEN   1
#define EIDOS_DISPLAY_BORDERLESS   2

/* The largest internal resolution multiple the port offers (a data value:
 * the 3D resolution option's range follows it).  A frame is 256N x 192N per
 * screen. */
#define EIDOS_RESOLUTION_MAX       6
/* The largest whole-number pixel scale the Pixel scale option offers. */
#define EIDOS_PIXEL_SCALE_MAX      10
/* The window on a first start: the largest whole multiple of the layout
 * whose height fits this percentage of the desktop's work area. */
#define EIDOS_FIRST_WINDOW_PERCENT 85
/* Auto never picks more than this: the per-frame cost grows with N^2, and
 * a maximized window would otherwise render at 6x.  A user who wants more
 * sets the explicit N. */
#define EIDOS_RESOLUTION_FIT_CAP   2

/*
 * Screen geometry, as data and without SDL.  A placement says where each of
 * the two screens (0 = top, 1 = bottom, the hardware's names) sits in DS
 * pixels; a viewport says where they land in an output of a given size.
 * `swap` puts the bottom screen first (above, or on the left).
 */
typedef struct EidosRect { int x, y, w, h; } EidosRect;

typedef struct EidosScreenPlacement {
    EidosRect screen[2];
    int width, height;          /* the whole arrangement, DS pixels */
} EidosScreenPlacement;

typedef struct EidosViewport {
    EidosRect content;          /* the picture in the output: fills it in
                                   one dimension, centred in the other     */
    EidosRect screen[2];        /* each screen's destination rectangle     */
} EidosViewport;

void eidos_layout_place(int layout, int swap, int gap,
                        EidosScreenPlacement *out);
/* The picture always fills the output, aspect preserved: letterboxed only
 * by what the aspect ratio forces.  No integer-multiple rule. */
void eidos_display_viewport(const EidosScreenPlacement *pl, int out_w,
                            int out_h, EidosViewport *vp);
/* As eidos_display_viewport at a whole-number pixel scale: every DS pixel
 * is exactly `scale` output pixels, the picture centred.  When the output
 * cannot hold that scale, the largest one it holds; 0 (auto) or an output
 * smaller than 1x fills it as eidos_display_viewport does. */
void eidos_display_viewport_n(const EidosScreenPlacement *pl, int out_w,
                              int out_h, int scale, EidosViewport *vp);
/*
 * The one-screen layout (EIDOS_LAYOUT_ONE).  Three pieces of arithmetic,
 * with no SDL, shared by the presenter, the mouse and the 3D setup so they
 * cannot disagree.
 *
 * Arrangement.  eidos_layout_place(EIDOS_LAYOUT_ONE, ...) gives the shape
 * the window takes when the layout is chosen: 16:9 (512 x 288), the shape of
 * the world view.  The run-status panel is not shown in this layout.
 *
 * Fallback.  eidos_one_fallback_viewport puts the two DS screens side by
 * side, filling the window letterboxed, exactly as the side-by-side layout
 * does (pixel_scale 0 is auto, N a whole multiple as in
 * eidos_display_viewport_n).  The mouse is the stylus on the bottom screen
 * through this viewport.
 *
 * World.  One DS screen fills the window and its 3D is rendered at the
 * window's shape.  Let a = win_w / win_h.  The DS view is 4:3.  Horizontal
 * plus: for a >= 4/3 the vertical view stays the DS's own and the
 * horizontal one grows, which is the clip matrix scaled in x by
 * kx = (4/3) / a and in y by ky = 1 (a factor below one shows more).  For a
 * window narrower than 4:3 the roles swap (kx = 1, ky = a / (4/3)), so the
 * DS's whole width is always visible and the window shows more above and
 * below instead of cropping the sides.  `zoom` multiplies both: zoom < 1
 * shows more in both directions, zoom > 1 less (clamped to 0.25 .. 2).  The
 * factors are fx32 (4096 = 1.0) because that is what the geometry engine
 * takes (g3d_hw_set_view_scale), and the rectangle below is computed from
 * the rounded factors so the 2D layers land exactly where the 3D put the
 * DS screen's pixels.
 *
 * `ds` is the DS screen's rectangle in the window: where its 2D layers
 * (text boxes, banners) are drawn, unstretched at the same pixel ratio as
 * the 3D, centred.  Always 4:3 (up to a pixel).  It extends past the window
 * only when zoom > 1.
 */
typedef struct EidosWorldGeometry {
    s32 kx, ky;                 /* clip matrix x and y factors, fx32       */
    EidosRect ds;               /* the DS screen's rectangle in the window */
} EidosWorldGeometry;

#define EIDOS_WORLD_ZOOM_MIN 0.25f
#define EIDOS_WORLD_ZOOM_MAX 2.0f

void eidos_world_geometry(int win_w, int win_h, float zoom,
                          EidosWorldGeometry *out);
void eidos_one_fallback_place(int swap, EidosScreenPlacement *out);
void eidos_one_fallback_viewport(int swap, int pixel_scale, int out_w,
                                 int out_h, EidosViewport *vp);
/* The window pixel (wx, wy) as a point in the world's DS rectangle, in DS
 * pixels (0..255 x 0..191, clamped); non-zero when it is inside. */
int  eidos_world_to_ds(const EidosWorldGeometry *g, int wx, int wy, int *x,
                       int *y);

/* The window size for a first start: the largest whole multiple of the
 * arrangement whose height fits `percent` of work_h and whose width fits
 * work_w, at least 1x.  work_w/work_h 0 (unknown) gives 1x. */
void eidos_first_window_size(const EidosScreenPlacement *pl, int work_w,
                             int work_h, int percent, int *out_w, int *out_h);
/* The auto internal resolution: the smallest N whose 256N x 192N frame is
 * at least as large as each screen's destination rectangle, in [1, max]. */
int  eidos_resolution_fit(const EidosViewport *vp, int max);
/* As eidos_resolution_fit with the cap given: the CPU renderer's cost grows
 * with the square of N, so Fit is capped low there; the GPU renderer has no
 * such limit. */
int  eidos_resolution_fit_cap(const EidosViewport *vp, int max, int cap);
/* The window size after the arrangement changes to `to`: the same area as
 * cur_w x cur_h, the aspect of `to`, scaled down to fit max_w x max_h (the
 * desktop work area; 0 = unbounded). */
void eidos_window_reshape(int cur_w, int cur_h, const EidosScreenPlacement *to,
                          int max_w, int max_h, int *out_w, int *out_h);
/* The bottom-screen pixel under output point (wx, wy); non-zero when the
 * point is on the bottom screen; both coordinates are clamped to it either
 * way. */
int  eidos_viewport_to_touch(const EidosScreenPlacement *pl,
                             const EidosViewport *vp, int wx, int wy,
                             int *x, int *y);

typedef struct EidosPlatformConfig {
    const char *title;       /* NULL: "eidos"                            */
    int   win_w, win_h;      /* windowed size in pixels; 0: 1x the
                                layout (the window is freely resizable)   */
    int   layout;            /* EIDOS_LAYOUT_*                           */
    int   gap;               /* DS pixels between the two screens        */
    int   swap_screens;      /* bottom screen first                      */
    int   display_mode;      /* EIDOS_DISPLAY_*; 0 windowed              */
    int   maximized;         /* windowed: open maximized                 */
    int   audio;             /* non-zero: open the audio device          */
    int   audio_push;        /* non-zero: the program pushes its samples
                                from its own thread (eidos_platform_queue_
                                audio) instead of a callback on SDL's     */
    const EidosKeyBinding *keymap;  /* NULL: eidos_default_keymap()      */
    u32   keymap_count;
    const EidosPadBinding *padmap;  /* NULL: eidos_default_padmap()      */
    u32   padmap_count;
} EidosPlatformConfig;

typedef struct EidosPlatform EidosPlatform;

/*
 * Create the window, the renderer, the two screen textures and (when asked)
 * the audio device.  Returns 0 on success; on failure nothing is left open
 * and the SDL error is printed on stderr.  A failure to open *audio* is not
 * a failure to start: the message is printed and the platform runs silent,
 * because a machine with no sound device should still show the game.
 */
int  eidos_platform_init(EidosPlatform **out, const EidosPlatformConfig *cfg);
void eidos_platform_quit(EidosPlatform *p);

/*
 * Drain the event queue, update the button word and the stylus, and hand
 * both to the input layer (bw_input_set_host, sdk/input.h).  Returns 0
 * when the application has been asked to quit (the window was closed or
 * Escape was pressed), non-zero to keep running.
 */
int  eidos_platform_pump(EidosPlatform *p);
/* The buttons currently held, as EIDOS_PAD_* bits. */
u32  eidos_platform_buttons(const EidosPlatform *p);

/* The buttons that went down during the last pump (edge-triggered). */
u32  eidos_platform_buttons_pressed(const EidosPlatform *p);

/*
 * The stylus: the left mouse button, or a finger (SDL turns touches into
 * the mouse), on the bottom screen.  A press starts only inside the bottom
 * screen; a drag that leaves it is held to the nearest edge, as a stylus
 * cannot leave the panel without being lifted.  Window coordinates are
 * mapped through the window's current size, so they stay right if the
 * window is resized.  Returns non-zero while it is down, with the
 * bottom-screen pixel in *x (0..255) and *y (0..191).
 */
int  eidos_platform_touch(const EidosPlatform *p, int *x, int *y);

/*
 * The mapping eidos_platform_touch uses, exposed for tests: the
 * bottom-screen pixel under window point (wx, wy) for a window `win_w` x
 * `win_h` showing this platform's layout.  Returns non-zero when the point
 * is on the bottom screen; *x and *y are clamped to it either way.
 */
int  eidos_platform_window_to_touch(const EidosPlatform *p, int win_w,
                                    int win_h, int wx, int wy,
                                    int *x, int *y);

/*
 * Runtime changes (engine/platform/settings.h drives these).  None recreates
 * the renderer or the textures.  Each returns 0 on success; a display mode
 * the host refuses (a headless driver, say) returns -1 and leaves the window
 * as it was.
 */
/* Windowed size win_w x win_h is applied when entering windowed mode from
 * another mode, or when it differs from the current size and is > 0; 0 keeps
 * the window as it is (1x the layout when coming back from fullscreen). */
int  eidos_platform_set_display(EidosPlatform *p, int mode, int win_w,
                                int win_h);
int  eidos_platform_set_layout(EidosPlatform *p, int layout, int swap, int gap);
/* Master volume, percent, applied to the final mixed samples with
 * saturation, in both the callback and the push path.  Default 100. */
void eidos_platform_set_volume(EidosPlatform *p, int percent);
/* The 2D pixel scale: 0 (auto) fills the window with sharp pixels, N shows
 * every DS pixel as exactly N x N output pixels (eidos_display_viewport_n). */
void eidos_platform_set_pixel_scale(EidosPlatform *p, int scale);
/* The gain itself, exposed for tests: v * percent / 100, rounded, saturated. */
s16  eidos_audio_gain(s16 v, int percent);
/* Replace both binding tables; the tables are copied. */
int  eidos_platform_set_bindings(EidosPlatform *p, const EidosKeyBinding *keys,
                                 u32 nkeys, const EidosPadBinding *pads,
                                 u32 npads);
/* Each screen's destination rectangle in the current output (0 top, 1
 * bottom), and the output size. */
void eidos_platform_viewport(const EidosPlatform *p, EidosViewport *vp);
int  eidos_platform_display_mode(const EidosPlatform *p);

/*
 * Capture the next control pressed, for a key-binding screen.  begin arms
 * it: while armed the game sees no buttons and Escape cancels instead of
 * quitting.  poll returns EIDOS_CAPTURE_PENDING until something happens,
 * then one of KEY / PAD / CANCELLED once (the state returns to NONE);
 * *out holds the control.
 */
#define EIDOS_CAPTURE_NONE      0
#define EIDOS_CAPTURE_PENDING   1
#define EIDOS_CAPTURE_KEY       2
#define EIDOS_CAPTURE_PAD       3
#define EIDOS_CAPTURE_CANCELLED 4

typedef struct EidosCaptured {
    int scancode;               /* KEY: SDL_Scancode                       */
    int pad_kind;               /* PAD: EIDOS_PADBIND_*                    */
    int pad_index;              /* PAD: SDL_GamepadButton / Axis          */
} EidosCaptured;

void eidos_platform_capture_begin(EidosPlatform *p);
void eidos_platform_capture_cancel(EidosPlatform *p);
int  eidos_platform_capture_poll(EidosPlatform *p, EidosCaptured *out);

/* Gamepads currently open.                                                */
int  eidos_platform_pads(const EidosPlatform *p);

/* Non-zero once F12 has been pressed since the last call; clears the flag. */
int  eidos_platform_take_screenshot_request(EidosPlatform *p);

/*
 * Upload two RGBA buffers (byte order R, G, B, A -- what g2d_frame_compose
 * writes) and present them.  Each is 256n x 192n pixels, n = 1 for
 * eidos_platform_present; eidos_platform_present_n takes any n in
 * [1, EIDOS_RESOLUTION_MAX or more].  The renderer should produce the n that
 * eidos_settings_internal_scale() returned for this frame.  When n differs
 * from the last frame the textures are recreated; a NULL buffer keeps that
 * screen's last image (black after a size change).
 *
 * Filtering, per screen, from the frame size to its destination rectangle:
 * smaller destination: linear (a downscale); an exact whole multiple:
 * nearest; any other upscale: nearest to the next whole multiple, then
 * linear down to the destination (sharp pixels with soft edges only where
 * the ratio is fractional); plain linear when that intermediate would be
 * over 8192 pixels or cannot be created.
 */
void eidos_platform_present(EidosPlatform *p, const u8 *top, const u8 *bottom);
void eidos_platform_present_n(EidosPlatform *p, const u8 *top,
                              const u8 *bottom, int n);
/* As present_n with a multiple for each screen (a screen without the 3D layer
 * can stay at 1 while the other is larger). */
void eidos_platform_present_nn(EidosPlatform *p, const u8 *top, int n_top,
                               const u8 *bottom, int n_bottom);

/*
 * The GPU 3D path.  eidos_platform_gpu3d is true when the OpenGL presenter
 * can rasterise the 3D engine itself.  eidos_platform_set_3d (before the
 * next present) draws `snap` at 256n x 192n on the GPU and shows it as the
 * 3D layer of screen `screen` (0 top, 1 bottom) over `behind`, that screen's
 * 256x192 RGBA with the 3D layer off; `bmode` 0/1/2 and `bfac` 0..1 are the
 * engine's master brightness.  screen < 0 turns the GPU 3D off.  Returns 0,
 * or -1 if it cannot (draw the CPU frame instead).
 */
struct G3dSnapshot;
int  eidos_platform_gpu3d(const EidosPlatform *p);

/*
 * The one-screen layout (EIDOS_LAYOUT_ONE; engine/ui/ui.h).  Each frame,
 * before eidos_platform_set_3d and the present, the caller gives the view
 * the interface layer chose and its canvas:
 *
 *   - eidos_platform_set_view: FALLBACK shows the two DS screens side by
 *     side (letterboxed, the mouse is the stylus); WORLD shows
 *     view->world_screen filling the window, its 3D rendered by the GPU at
 *     the window's size with the projection widened (eidos_world_geometry),
 *     its 2D layers at the DS rectangle in the middle, blurred and dimmed
 *     when view->blur.  NULL means FALLBACK.  WORLD needs the GPU
 *     presenter; without it the fallback is shown.
 *   - eidos_platform_set_ui_canvas: the interface picture, drawn over
 *     either with nearest filtering at canvas->k window pixels per canvas
 *     pixel, at the window's top-left, alpha blended.  Uploaded only when
 *     canvas->generation changes.  NULL hides it.
 *
 * The widening reaches the game through the geometry engine's view scale:
 * eidos_platform_view_scale gives the fx32 factors to pass to
 * g3d_hw_set_view_scale (4096, 4096 when the world view is not active), and
 * the caller applies them before the game's next frame so the game's own
 * culling sees the wider view.
 *
 * eidos_platform_mouse fills the window mouse for the interface layer: the
 * position in window pixels (-1 outside), the button state, and the edges
 * and wheel steps seen by the last eidos_platform_pump.  Returns non-zero
 * when the pointer is over the window.
 */
struct EidosUiView;
struct EidosUiCanvas;
struct EidosUiMouse;
int  eidos_platform_layout(const EidosPlatform *p);
void eidos_platform_set_view(EidosPlatform *p, const struct EidosUiView *v);
void eidos_platform_set_ui_canvas(EidosPlatform *p,
                                  const struct EidosUiCanvas *c);
int  eidos_platform_world_active(const EidosPlatform *p);
void eidos_platform_view_scale(const EidosPlatform *p, s32 *kx, s32 *ky);
int  eidos_platform_mouse(const EidosPlatform *p, struct EidosUiMouse *m);
void eidos_platform_window_size(const EidosPlatform *p, int *w, int *h);
/* Typing (the one-screen name entry): on, the keyboard's letters, digits,
 * Backspace and Return are text, not game buttons (the arrows still are);
 * take_text hands over what was typed since the last call, UTF-8 with "\b"
 * for Backspace and "\n" for Return. */
void eidos_platform_text_mode(EidosPlatform *p, int on);
/* The device the player used last (1 a pad, 0 the keyboard or mouse), and
 * the names of the game's twelve buttons on it (names[i] for bit i: the
 * bound keyboard keys, or the pad's labels; NULL when none). */
int  eidos_platform_pad_last(const EidosPlatform *p);
/* The icon family for the pad in use (engine/ui/padglyph.h UI_PAD_*), from
 * the type SDL reports for the pad pressed last. */
int  eidos_platform_pad_family(const EidosPlatform *p);
void eidos_platform_key_names(const EidosPlatform *p, const char *names[12]);
int  eidos_platform_take_text(EidosPlatform *p, char *out, int cap);

/*
 * The display capture at the 3D's resolution (GPU path only).  A scene that
 * captures the main screen into VRAM every frame and shows that bank would
 * otherwise stay at DS size.  _capture_hires makes this frame's capture from
 * the graphics screen with a sentinel 3D layer (a2d), the same with the 3D
 * off (behind) and the 3D frame, mixed (blend != 0, eva and evb in
 * sixteenths) with the capture before it, or with b1x when that is given;
 * _show_capture names the screen that shows it this frame (-1: none).
 */
int  eidos_platform_capture_hires(EidosPlatform *p, const u8 *a2d,
                                  const u8 *behind,
                                  const struct G3dSnapshot *snap, int n,
                                  int blend, int eva, int evb, const u8 *b1x);
void eidos_platform_show_capture(EidosPlatform *p, int screen, int bmode,
                                 float bfac);
/* The GPU's 3D frame at DS size into `out` (256x192 RGBA); 0 new, 1 unchanged,
 * -1 failure. */
int  eidos_platform_read3d_1x(EidosPlatform *p, const struct G3dSnapshot *snap,
                              u8 *out, int async);
/* The 3D screen composed with an opaque black 3D layer (256x192 RGBA), for
 * the 2D layers the game blends over the 3D (plat_gl.c); NULL for none.
 * Call before eidos_platform_set_3d. */
void eidos_platform_set_3d_zero(EidosPlatform *p, const u8 *zero);
int  eidos_platform_set_3d(EidosPlatform *p, int screen, const u8 *behind,
                           const struct G3dSnapshot *snap, int n, int bmode,
                           float bfac, int dilate);
/* The multiple of the last presented frame (1 before the first). */
int  eidos_platform_frame_scale(const EidosPlatform *p);

/*
 * Write the last presented pair as a BMP through SDL_SaveBMP at the internal
 * resolution of that frame (256n x 192n per screen, in the configured layout
 * with the gap scaled by n).  The window size is not applied.  Returns 0 on
 * success.
 *
 * This works under SDL_VIDEODRIVER=dummy, which is what lets a headless test
 * check that the engine actually drew something.
 */
int  eidos_platform_screenshot(EidosPlatform *p, const char *path);

/* The dimensions eidos_platform_screenshot writes, for a caller that wants
 * to check them without opening the file. */
void eidos_platform_shot_size(const EidosPlatform *p, u32 *w, u32 *h);

/* ------------------------------------------------------------------ */
/* The frame pacer                                                     */
/* ------------------------------------------------------------------ */

/*
 * The DS video timing: 355 dots x 263 lines x 6 clocks per dot = 560190
 * system clocks per frame at 33513982 Hz, i.e. 59.8261 frames per second.
 */
#define EIDOS_DS_SYSTEM_CLOCK   33513982u
#define EIDOS_DS_FRAME_CLOCKS     560190u

/* The exact frame period in nanoseconds, floor and remainder:
 * 560190 * 10^9 / 33513982 = 16715113 ns + 3790034 / 33513982 ns, i.e.
 * 16715113.113 ns.  A 16 ms period would run 4.3 percent fast and a
 * 16666667 ns one 0.29 percent fast, which is a frame every six minutes. */
u64 eidos_frame_period_ns(void);

/*
 * Sleep until the next frame boundary.  The boundary advances by the exact
 * rational period every call and the remainder is accumulated, so the n-th
 * frame is due at round(n * 560190 * 10^9 / 33513982) ns after the first.
 * A caller that falls more than a few frames behind (a breakpoint, a
 * suspended laptop) is resynchronised rather than made to run the backlog.
 */
void eidos_platform_wait_frame(EidosPlatform *p);

/* Fast forward: while Tab is held in the game window, the game runs
 * EIDOS_FAST_FORWARD frames in the time of one; while left Shift and Tab
 * are held, it runs as fast as the host can (EIDOS_FAST_FORWARD_MAX, a
 * bound no host reaches).  Right Shift is not used: it is SELECT.  Returns
 * the speed factor: one of the two while the keys are held, 1 otherwise
 * (and with no window).  eidos_platform_wait_frame paces by it. */
#define EIDOS_FAST_FORWARD     2
#define EIDOS_FAST_FORWARD_MAX 1000
int  eidos_platform_fast_forward(const EidosPlatform *p);

/* A lock on fast forward: while it is set (a multiplayer session: the
 * other players cannot speed up with this one), eidos_platform_fast_forward
 * answers 1 whatever keys are held.  The caller sets it every frame. */
void eidos_platform_fast_forward_lock(int locked);

/* Monotonic nanoseconds since the platform was created. */
u64  eidos_platform_now_ns(const EidosPlatform *p);

/* How many times the pacer had to resynchronise because the frame took
 * longer than the period; a non-zero count means the host is too slow or
 * something blocked. */
u32  eidos_platform_late_frames(const EidosPlatform *p);
/* Whole frames the schedule lagged when the last frame wait began (0 when on
 * time): a caller that is badly behind may skip presenting, never a step. */
u32  eidos_platform_lag_frames(const EidosPlatform *p);

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/*
 * The DS mixer's output rate.  The ARM7 sound core mixes its sixteen
 * channels at 32768 Hz; opening the host device at the same rate keeps the
 * eventual sequencer (Milestone 5) from resampling twice.
 */
#define EIDOS_AUDIO_RATE     32768
#define EIDOS_AUDIO_CHANNELS 2

/*
 * The callback shape: fill `frames` stereo signed-16 frames (2 * frames
 * samples) and return.  With no callback registered the device is fed
 * silence, which is what happens today.
 */
typedef void (*EidosAudioFn)(void *user, s16 *out, u32 frames);

void eidos_platform_set_audio(EidosPlatform *p, EidosAudioFn fn, void *user);

/* Non-zero when a device is open. */
int  eidos_platform_audio_open(const EidosPlatform *p);

/* How many frames of audio the silent callback has produced, which is the
 * only observable the device gives us until there is a mixer. */
u64  eidos_platform_audio_frames(const EidosPlatform *p);

/*
 * The push mode (EidosPlatformConfig.audio_push): no callback runs on SDL's
 * audio thread, so a producer that is not thread-safe (the game's own sound
 * driver, which the game thread drives through the ARM7 stand-in) renders
 * on its own thread and hands the frames over here.  `queue` copies
 * `frames` stereo frames to the device's queue; `queued` is how many are
 * waiting, which the producer keeps a few frames' worth ahead.  Both do
 * nothing (0) without a push-mode device.
 */
void eidos_platform_queue_audio(EidosPlatform *p, const s16 *in, u32 frames);
u32  eidos_platform_audio_queued(const EidosPlatform *p);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_PLATFORM_H */
