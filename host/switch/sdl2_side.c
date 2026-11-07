/*
 * host/switch/sdl2_side.c -- the SDL2 half of the Switch build's SDL
 * (host/switch/sw2.h says why there are two).  This file alone sees SDL2's
 * headers; host/switch/build.sh compiles it with SDL2's names renamed to
 * S2_SDL_*, as its library is.
 */

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <switch.h>

#include "sw2.h"

/* ------------------------------------------------------------------ */
/* Init, errors, time                                                  */
/* ------------------------------------------------------------------ */

int sw2_init(void)
{
    static int sockets;

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        return 1;
    }
    if (!sockets) {
        /* BSD sockets (host/net.c: multiplayer, Mystery Gift) need the
         * system's socket service started first */
        sockets = 1;
        socketInitializeDefault();
    }
    return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
                    SDL_INIT_JOYSTICK | SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0;
}

void sw2_quit(void) { SDL_Quit(); }
const char *sw2_error(void) { return SDL_GetError(); }
uint64_t sw2_counter(void) { return SDL_GetPerformanceCounter(); }
uint64_t sw2_frequency(void) { return SDL_GetPerformanceFrequency(); }
void sw2_delay(uint32_t ms) { SDL_Delay(ms); }

/* ------------------------------------------------------------------ */
/* The window and OpenGL (ES)                                          */
/* ------------------------------------------------------------------ */

int sw2_gl_attribute(int which, int value)
{
    static const SDL_GLattr k_attr[] = {
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_MAJOR_VERSION,
        SDL_GL_CONTEXT_MINOR_VERSION, SDL_GL_CONTEXT_FLAGS, SDL_GL_DEPTH_SIZE,
        SDL_GL_STENCIL_SIZE, SDL_GL_DOUBLEBUFFER,
    };

    if (which < 0 || which >= SW2_GL_OTHER) {
        return 1;                       /* not one the Switch needs */
    }
    return SDL_GL_SetAttribute(k_attr[which], value) == 0;
}

void sw2_display_size(int *w, int *h, int *refresh)
{
    SDL_DisplayMode m;

    if (SDL_GetDesktopDisplayMode(0, &m) != 0) {
        m.w = 1280;
        m.h = 720;
        m.refresh_rate = 60;
    }
    *w = m.w;
    *h = m.h;
    *refresh = m.refresh_rate > 0 ? m.refresh_rate : 60;
}

void *sw2_window_create(const char *title, int w, int h)
{
    int rate;

    /* the Switch's window is its whole screen (1280x720 handheld,
     * 1920x1080 docked), whatever size is asked for */
    (void)w;
    (void)h;
    sw2_display_size(&w, &h, &rate);
    return SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
}

void sw2_window_destroy(void *win) { SDL_DestroyWindow((SDL_Window *)win); }

void sw2_window_size(void *win, int *w, int *h)
{
    SDL_GetWindowSize((SDL_Window *)win, w, h);
}

void *sw2_gl_create(void *win) { return SDL_GL_CreateContext((SDL_Window *)win); }

int sw2_gl_make_current(void *win, void *ctx)
{
    return SDL_GL_MakeCurrent((SDL_Window *)win, (SDL_GLContext)ctx) == 0;
}

void sw2_gl_destroy(void *ctx) { SDL_GL_DeleteContext((SDL_GLContext)ctx); }
void sw2_gl_swap(void *win) { SDL_GL_SwapWindow((SDL_Window *)win); }
int sw2_gl_set_interval(int interval) { return SDL_GL_SetSwapInterval(interval) == 0; }
int sw2_gl_get_interval(void) { return SDL_GL_GetSwapInterval(); }
void *sw2_gl_proc(const char *name) { return SDL_GL_GetProcAddress(name); }

/* ------------------------------------------------------------------ */
/* The 2D renderer                                                     */
/* ------------------------------------------------------------------ */

void *sw2_renderer_create(void *win)
{
    return SDL_CreateRenderer((SDL_Window *)win, -1, SDL_RENDERER_ACCELERATED);
}

void sw2_renderer_destroy(void *r) { SDL_DestroyRenderer((SDL_Renderer *)r); }

void sw2_renderer_output(void *r, int *w, int *h)
{
    SDL_GetRendererOutputSize((SDL_Renderer *)r, w, h);
}

/* SDL2 and SDL3 number the pixel formats and texture accesses alike */
void *sw2_texture_create(void *r, uint32_t format, int access, int w, int h)
{
    return SDL_CreateTexture((SDL_Renderer *)r, format, access, w, h);
}

void sw2_texture_destroy(void *t) { SDL_DestroyTexture((SDL_Texture *)t); }

int sw2_texture_nearest(void *t, int nearest)
{
    return SDL_SetTextureScaleMode((SDL_Texture *)t,
                                   nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear) == 0;
}

int sw2_texture_update(void *t, const int *rect, const void *pixels, int pitch)
{
    SDL_Rect r;

    if (rect != NULL) {
        r.x = rect[0];
        r.y = rect[1];
        r.w = rect[2];
        r.h = rect[3];
    }
    return SDL_UpdateTexture((SDL_Texture *)t, rect != NULL ? &r : NULL, pixels, pitch) == 0;
}

int sw2_render_target(void *r, void *t)
{
    return SDL_SetRenderTarget((SDL_Renderer *)r, (SDL_Texture *)t) == 0;
}

int sw2_render_color(void *r, int cr, int cg, int cb, int ca)
{
    return SDL_SetRenderDrawColor((SDL_Renderer *)r, (Uint8)cr, (Uint8)cg, (Uint8)cb,
                                  (Uint8)ca) == 0;
}

int sw2_render_clear(void *r) { return SDL_RenderClear((SDL_Renderer *)r) == 0; }

int sw2_render_copy(void *r, void *t, const float *src, const float *dst)
{
    SDL_Rect s;
    SDL_FRect d;

    if (src != NULL) {
        s.x = (int)src[0];
        s.y = (int)src[1];
        s.w = (int)src[2];
        s.h = (int)src[3];
    }
    if (dst != NULL) {
        d.x = dst[0];
        d.y = dst[1];
        d.w = dst[2];
        d.h = dst[3];
    }
    return SDL_RenderCopyF((SDL_Renderer *)r, (SDL_Texture *)t, src != NULL ? &s : NULL,
                           dst != NULL ? &d : NULL) == 0;
}

void sw2_render_present(void *r) { SDL_RenderPresent((SDL_Renderer *)r); }

int sw2_save_bmp(int w, int h, uint32_t format, void *pixels, int pitch, const char *file)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, pitch, format);
    int ok;

    if (s == NULL) {
        return 0;
    }
    ok = SDL_SaveBMP(s, file) == 0;
    SDL_FreeSurface(s);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

int sw2_poll(Sw2Event *out)
{
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        memset(out, 0, sizeof *out);
        switch (e.type) {
        case SDL_QUIT:
            out->type = SW2_EV_QUIT;
            return 1;
        case SDL_CONTROLLERDEVICEADDED:
            /* SDL2 names a new pad by its device index, SDL3 by its id */
            out->type = SW2_EV_PAD_ADDED;
            out->a = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
            return 1;
        case SDL_CONTROLLERDEVICEREMOVED:
            out->type = SW2_EV_PAD_REMOVED;
            out->a = e.cdevice.which;
            return 1;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            out->type = SW2_EV_PAD_BUTTON;
            out->a = e.cbutton.which;
            out->b = e.cbutton.button;
            out->c = e.cbutton.state == SDL_PRESSED;
            return 1;
        case SDL_CONTROLLERAXISMOTION:
            out->type = SW2_EV_PAD_AXIS;
            out->a = e.caxis.which;
            out->b = e.caxis.axis;
            out->c = e.caxis.value;
            return 1;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            out->type = SW2_EV_KEY;
            out->a = e.key.keysym.scancode;
            out->b = e.key.keysym.sym;
            out->c = e.key.state == SDL_PRESSED;
            out->d = ((int64_t)e.key.repeat << 16) | e.key.keysym.mod;
            return 1;
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            out->type = e.type == SDL_FINGERDOWN ? SW2_EV_FINGER_DOWN :
                        e.type == SDL_FINGERUP ? SW2_EV_FINGER_UP : SW2_EV_FINGER_MOTION;
            out->a = e.tfinger.touchId;
            out->b = e.tfinger.fingerId;
            out->x = e.tfinger.x;
            out->y = e.tfinger.y;
            out->dx = e.tfinger.dx;
            out->dy = e.tfinger.dy;
            out->pressure = e.tfinger.pressure;
            return 1;
        case SDL_MOUSEMOTION:
            out->type = SW2_EV_MOUSE_MOTION;
            out->a = e.motion.which;
            out->b = e.motion.state;
            out->x = (float)e.motion.x;
            out->y = (float)e.motion.y;
            out->dx = (float)e.motion.xrel;
            out->dy = (float)e.motion.yrel;
            return 1;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            out->type = SW2_EV_MOUSE_BUTTON;
            out->a = e.button.which;
            out->b = e.button.button;
            out->c = e.button.state == SDL_PRESSED;
            out->d = e.button.clicks;
            out->x = (float)e.button.x;
            out->y = (float)e.button.y;
            return 1;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                out->type = SW2_EV_RESIZED;
                out->a = e.window.data1;
                out->b = e.window.data2;
                return 1;
            }
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            out->type = SW2_EV_BACKGROUND;
            return 1;
        case SDL_APP_DIDENTERFOREGROUND:
            out->type = SW2_EV_FOREGROUND;
            return 1;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gamepads                                                            */
/* ------------------------------------------------------------------ */

static int index_of(int32_t id)
{
    int i, n = SDL_NumJoysticks();

    for (i = 0; i < n; i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == id) {
            return i;
        }
    }
    return -1;
}

int sw2_pad_is_gamepad(int32_t id)
{
    int i = index_of(id);

    return i >= 0 && SDL_IsGameController(i);
}

void *sw2_pad_open(int32_t id)
{
    int i = index_of(id);

    return i >= 0 ? SDL_GameControllerOpen(i) : NULL;
}

void sw2_pad_close(void *pad) { SDL_GameControllerClose((SDL_GameController *)pad); }

int32_t sw2_pad_id(void *pad)
{
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick((SDL_GameController *)pad));
}

int sw2_pad_button(void *pad, int button)
{
    return SDL_GameControllerGetButton((SDL_GameController *)pad,
                                       (SDL_GameControllerButton)button);
}

int sw2_pad_axis(void *pad, int axis)
{
    return SDL_GameControllerGetAxis((SDL_GameController *)pad, (SDL_GameControllerAxis)axis);
}

/* ------------------------------------------------------------------ */
/* Keyboard and mouse                                                  */
/* ------------------------------------------------------------------ */

const uint8_t *sw2_keys(int *n) { return SDL_GetKeyboardState(n); }
const char *sw2_scancode_name(int sc) { return SDL_GetScancodeName((SDL_Scancode)sc); }
int sw2_scancode_from_name(const char *name) { return SDL_GetScancodeFromName(name); }

uint32_t sw2_mouse(float *x, float *y)
{
    int ix = 0, iy = 0;
    uint32_t b = SDL_GetMouseState(&ix, &iy);

    if (x != NULL) {
        *x = (float)ix;
    }
    if (y != NULL) {
        *y = (float)iy;
    }
    return b;
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/* SDL2 and SDL3 number the sample formats alike (0x8010: 16-bit LE) */
uint32_t sw2_audio_open(int freq, int format, int channels)
{
    SDL_AudioSpec want, have;

    memset(&want, 0, sizeof want);
    want.freq = freq;
    want.format = (SDL_AudioFormat)format;
    want.channels = (Uint8)channels;
    want.samples = 1024;
    want.callback = NULL;               /* pushed to: SDL_QueueAudio */
    return SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
}

void sw2_audio_close(uint32_t dev) { SDL_CloseAudioDevice(dev); }

int sw2_audio_put(uint32_t dev, const void *buf, int len)
{
    return SDL_QueueAudio(dev, buf, (Uint32)len) == 0;
}

int sw2_audio_queued(uint32_t dev) { return (int)SDL_GetQueuedAudioSize(dev); }
void sw2_audio_resume(uint32_t dev) { SDL_PauseAudioDevice(dev, 0); }
void sw2_audio_lock(uint32_t dev) { SDL_LockAudioDevice(dev); }
void sw2_audio_unlock(uint32_t dev) { SDL_UnlockAudioDevice(dev); }

/* ------------------------------------------------------------------ */
/* Threads and locks                                                   */
/* ------------------------------------------------------------------ */

void *sw2_thread(int (*fn)(void *), const char *name, size_t stack, void *data)
{
    return SDL_CreateThreadWithStackSize(fn, name, stack, data);
}

void *sw2_mutex(void) { return SDL_CreateMutex(); }
void sw2_lock(void *m) { SDL_LockMutex((SDL_mutex *)m); }
void sw2_unlock(void *m) { SDL_UnlockMutex((SDL_mutex *)m); }
void *sw2_cond(void) { return SDL_CreateCond(); }
void sw2_cond_wait(void *c, void *m) { SDL_CondWait((SDL_cond *)c, (SDL_mutex *)m); }
void sw2_cond_broadcast(void *c) { SDL_CondBroadcast((SDL_cond *)c); }
