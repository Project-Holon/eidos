/*
 * host/switch/sw2.h -- the line between the Switch build's two halves of
 * SDL (docs/consoles.md).
 *
 * The host is written for SDL3 (host/sdl.c and the rest); devkitPro has
 * SDL2 for the Switch.  host/switch/sdl3_on_sdl2.c gives the host the SDL3
 * functions it calls, and host/switch/sdl2_side.c does the work with SDL2.
 * The two cannot share a file: both libraries call their functions and
 * types by the same names (SDL_PollEvent, SDL_Event).  So SDL2's own names
 * are renamed in its library (S2_SDL_*, host/switch/build.sh), and the
 * halves talk through this header, in plain C types only.
 */
#ifndef HOST_SWITCH_SW2_H
#define HOST_SWITCH_SW2_H

#include <stddef.h>
#include <stdint.h>

/* Init, errors, time */
int sw2_init(void);
void sw2_quit(void);
const char *sw2_error(void);
uint64_t sw2_counter(void);
uint64_t sw2_frequency(void);
void sw2_delay(uint32_t ms);

/* The window and OpenGL (ES) */
enum { SW2_GL_PROFILE, SW2_GL_MAJOR, SW2_GL_MINOR, SW2_GL_FLAGS, SW2_GL_DEPTH,
       SW2_GL_STENCIL, SW2_GL_DOUBLE, SW2_GL_OTHER };
int sw2_gl_attribute(int which, int value);
void *sw2_window_create(const char *title, int w, int h);
void sw2_window_destroy(void *win);
void sw2_window_size(void *win, int *w, int *h);
void sw2_display_size(int *w, int *h, int *refresh);
void *sw2_gl_create(void *win);
int sw2_gl_make_current(void *win, void *ctx);
void sw2_gl_destroy(void *ctx);
void sw2_gl_swap(void *win);
int sw2_gl_set_interval(int interval);
int sw2_gl_get_interval(void);
void *sw2_gl_proc(const char *name);

/* The 2D renderer (the first-run window) */
void *sw2_renderer_create(void *win);
void sw2_renderer_destroy(void *r);
void sw2_renderer_output(void *r, int *w, int *h);
void *sw2_texture_create(void *r, uint32_t format, int access, int w, int h);
void sw2_texture_destroy(void *t);
int sw2_texture_nearest(void *t, int nearest);
int sw2_texture_update(void *t, const int *rect, const void *pixels, int pitch);
int sw2_render_target(void *r, void *t);
int sw2_render_color(void *r, int cr, int cg, int cb, int ca);
int sw2_render_clear(void *r);
int sw2_render_copy(void *r, void *t, const float *src, const float *dst);
void sw2_render_present(void *r);

/* Surfaces: only a picture saved as BMP (screenshots) */
int sw2_save_bmp(int w, int h, uint32_t format, void *pixels, int pitch, const char *file);

/* Events, already in the terms SDL3 uses (gamepad ids are joystick
 * instance ids; buttons and axes keep their numbers, which SDL2 and SDL3
 * share; scancodes and keycodes too) */
enum {
    SW2_EV_NONE, SW2_EV_QUIT, SW2_EV_PAD_ADDED, SW2_EV_PAD_REMOVED,
    SW2_EV_PAD_BUTTON, SW2_EV_PAD_AXIS, SW2_EV_KEY, SW2_EV_FINGER_DOWN,
    SW2_EV_FINGER_UP, SW2_EV_FINGER_MOTION, SW2_EV_MOUSE_MOTION,
    SW2_EV_MOUSE_BUTTON, SW2_EV_RESIZED, SW2_EV_BACKGROUND, SW2_EV_FOREGROUND
};
typedef struct Sw2Event {
    int type;
    int64_t a, b, c, d;     /* ids, button, down, value, scancode... */
    float x, y, dx, dy, pressure;
} Sw2Event;
int sw2_poll(Sw2Event *out);

/* Gamepads */
int sw2_pad_is_gamepad(int32_t id);
void *sw2_pad_open(int32_t id);
void sw2_pad_close(void *pad);
int32_t sw2_pad_id(void *pad);
int sw2_pad_button(void *pad, int button);
int sw2_pad_axis(void *pad, int axis);

/* Keyboard and mouse */
const uint8_t *sw2_keys(int *n);
const char *sw2_scancode_name(int sc);
int sw2_scancode_from_name(const char *name);
uint32_t sw2_mouse(float *x, float *y);

/* Audio: one device, pushed to (SDL2's queue) */
uint32_t sw2_audio_open(int freq, int format, int channels);
void sw2_audio_close(uint32_t dev);
int sw2_audio_put(uint32_t dev, const void *buf, int len);
int sw2_audio_queued(uint32_t dev);
void sw2_audio_resume(uint32_t dev);
void sw2_audio_lock(uint32_t dev);
void sw2_audio_unlock(uint32_t dev);

/* Threads and locks */
void *sw2_thread(int (*fn)(void *), const char *name, size_t stack, void *data);
void *sw2_mutex(void);
void sw2_lock(void *m);
void sw2_unlock(void *m);
void *sw2_cond(void);
void sw2_cond_wait(void *c, void *m);
void sw2_cond_broadcast(void *c);

#endif
