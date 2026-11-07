/*
 * host/switch/sdl3_on_sdl2.c -- the SDL3 functions the host calls, on the
 * Switch, where devkitPro has SDL2 only (host/switch/sw2.h).
 *
 * Built with SDL3's headers, so the types the host and the game see are
 * SDL3's own: an event is written in SDL3's layout (the host copies its
 * bytes to the game, host/sdl.c), a surface is SDL3's struct.  The work is
 * done by host/switch/sdl2_side.c.  SDL's objects (a window, a gamepad, a
 * mutex) are SDL2's pointers under SDL3's opaque names.
 *
 * What the Switch has no use for answers plainly: the window is the whole
 * screen and does not move, there is no clipboard, and the GPU API is not
 * there (host/switch/sdl3_gpu_stubs.c), so the game draws with OpenGL ES.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>

#include "sw2.h"

#define SWITCH_DIR "sdmc:/switch/eidos/"

static char g_error[256];
static SDL_InitFlags g_inited;
static void *g_window;

static bool fail(const char *what)
{
    snprintf(g_error, sizeof g_error, "%s", what);
    return false;
}

const char *SDL_GetError(void)
{
    if (g_error[0] != '\0') {
        return g_error;
    }
    return sw2_error();
}

/* ------------------------------------------------------------------ */
/* Init, platform, time, memory                                        */
/* ------------------------------------------------------------------ */

bool SDL_Init(SDL_InitFlags flags)
{
    if (!sw2_init()) {
        return false;
    }
    g_inited |= flags;
    return true;
}

bool SDL_InitSubSystem(SDL_InitFlags flags) { return SDL_Init(flags); }
void SDL_QuitSubSystem(SDL_InitFlags flags) { g_inited &= ~flags; }
SDL_InitFlags SDL_WasInit(SDL_InitFlags flags) { return flags != 0 ? (g_inited & flags) : g_inited; }

void SDL_Quit(void)
{
    g_inited = 0;
    sw2_quit();
}

void SDL_SetMainReady(void) {}
bool SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return true; }
const char *SDL_GetPlatform(void) { return "Nintendo Switch"; }
const char *SDL_GetCurrentVideoDriver(void) { return "switch"; }

/* an application has cores 0 to 2; the system keeps the fourth */
int SDL_GetNumLogicalCPUCores(void) { return 3; }

Uint64 SDL_GetPerformanceCounter(void) { return sw2_counter(); }
Uint64 SDL_GetPerformanceFrequency(void) { return sw2_frequency(); }

Uint64 SDL_GetTicksNS(void)
{
    static Uint64 start;
    Uint64 now = sw2_counter(), f = sw2_frequency();

    if (start == 0) {
        start = now;
    }
    now -= start;
    return (now / f) * 1000000000u + (now % f) * 1000000000u / f;
}

Uint64 SDL_GetTicks(void) { return SDL_GetTicksNS() / 1000000u; }
void SDL_Delay(Uint32 ms) { sw2_delay(ms); }

void *SDL_malloc(size_t size) { return malloc(size); }
char *SDL_strdup(const char *s) { return strdup(s); }

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

bool SDL_CreateDirectory(const char *path)
{
    char buf[1024];
    size_t i;

    snprintf(buf, sizeof buf, "%s", path);
    /* each parent first, past the device name ("sdmc:/") */
    for (i = 1; buf[i] != '\0'; i++) {
        if (buf[i] == '/' && buf[i - 1] != ':') {
            buf[i] = '\0';
            mkdir(buf, 0777);
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
        return fail("cannot make the folder");
    }
    return true;
}

char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org;
    (void)app;
    SDL_CreateDirectory(SWITCH_DIR);
    return strdup(SWITCH_DIR);
}

/* ------------------------------------------------------------------ */
/* Properties: small sets of named values                              */
/* ------------------------------------------------------------------ */

#define PROPS_MAX 32
#define PROP_ITEMS 16

typedef struct Prop {
    char name[64];
    int kind;                   /* 0 none, 1 pointer, 2 string, 3 number */
    void *ptr;
    char str[256];
    Sint64 num;
} Prop;

static struct { int used; Prop item[PROP_ITEMS]; } g_props[PROPS_MAX];

SDL_PropertiesID SDL_CreateProperties(void)
{
    int i;

    for (i = 0; i < PROPS_MAX; i++) {
        if (!g_props[i].used) {
            memset(&g_props[i], 0, sizeof g_props[i]);
            g_props[i].used = 1;
            return (SDL_PropertiesID)(i + 1);
        }
    }
    fail("out of property sets");
    return 0;
}

void SDL_DestroyProperties(SDL_PropertiesID props)
{
    if (props >= 1 && props <= PROPS_MAX) {
        g_props[props - 1].used = 0;
    }
}

static Prop *prop_find(SDL_PropertiesID props, const char *name, int make)
{
    int i;

    if (props < 1 || props > PROPS_MAX || !g_props[props - 1].used) {
        return NULL;
    }
    for (i = 0; i < PROP_ITEMS; i++) {
        Prop *p = &g_props[props - 1].item[i];

        if (p->kind != 0 && strcmp(p->name, name) == 0) {
            return p;
        }
    }
    if (!make) {
        return NULL;
    }
    for (i = 0; i < PROP_ITEMS; i++) {
        Prop *p = &g_props[props - 1].item[i];

        if (p->kind == 0) {
            snprintf(p->name, sizeof p->name, "%s", name);
            return p;
        }
    }
    return NULL;
}

bool SDL_SetPointerProperty(SDL_PropertiesID props, const char *name, void *value)
{
    Prop *p = prop_find(props, name, 1);

    if (p == NULL) {
        return fail("no room for the property");
    }
    p->kind = 1;
    p->ptr = value;
    return true;
}

bool SDL_SetStringProperty(SDL_PropertiesID props, const char *name, const char *value)
{
    Prop *p = prop_find(props, name, 1);

    if (p == NULL) {
        return fail("no room for the property");
    }
    p->kind = 2;
    snprintf(p->str, sizeof p->str, "%s", value != NULL ? value : "");
    return true;
}

bool SDL_SetNumberProperty(SDL_PropertiesID props, const char *name, Sint64 value)
{
    Prop *p = prop_find(props, name, 1);

    if (p == NULL) {
        return fail("no room for the property");
    }
    p->kind = 3;
    p->num = value;
    return true;
}

void *SDL_GetPointerProperty(SDL_PropertiesID props, const char *name, void *dflt)
{
    Prop *p = prop_find(props, name, 0);

    return p != NULL && p->kind == 1 ? p->ptr : dflt;
}

static const char *prop_string(SDL_PropertiesID props, const char *name, const char *dflt)
{
    Prop *p = prop_find(props, name, 0);

    return p != NULL && p->kind == 2 ? p->str : dflt;
}

static Sint64 prop_number(SDL_PropertiesID props, const char *name, Sint64 dflt)
{
    Prop *p = prop_find(props, name, 0);

    return p != NULL && p->kind == 3 ? p->num : dflt;
}

/* ------------------------------------------------------------------ */
/* Threads, locks, thread-local values                                 */
/* ------------------------------------------------------------------ */

SDL_Thread *SDL_CreateThreadWithPropertiesRuntime(SDL_PropertiesID props,
                                                  SDL_FunctionPointer begin,
                                                  SDL_FunctionPointer end)
{
    SDL_ThreadFunction fn = (SDL_ThreadFunction)SDL_GetPointerProperty(
        props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, NULL);

    (void)begin;
    (void)end;
    if (fn == NULL) {
        fail("a thread with no function");
        return NULL;
    }
    return (SDL_Thread *)sw2_thread(
        fn, prop_string(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "eidos"),
        (size_t)prop_number(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 0),
        SDL_GetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, NULL));
}

SDL_Mutex *SDL_CreateMutex(void) { return (SDL_Mutex *)sw2_mutex(); }
void SDL_LockMutex(SDL_Mutex *m) { if (m != NULL) sw2_lock(m); }
void SDL_UnlockMutex(SDL_Mutex *m) { if (m != NULL) sw2_unlock(m); }
SDL_Condition *SDL_CreateCondition(void) { return (SDL_Condition *)sw2_cond(); }
void SDL_WaitCondition(SDL_Condition *c, SDL_Mutex *m) { sw2_cond_wait(c, m); }
void SDL_BroadcastCondition(SDL_Condition *c) { sw2_cond_broadcast(c); }

/* An id is a slot number (from 1), given out the first time it is set;
 * each thread has its own row of slots.  The destructors are not run: the
 * host's values live as long as the program. */
#define TLS_SLOTS 64
static __thread const void *t_tls[TLS_SLOTS];
static int g_tls_next;
static void *g_tls_lock;

bool SDL_SetTLS(SDL_TLSID *id, const void *value, SDL_TLSDestructorCallback destructor)
{
    (void)destructor;
    if (id->value == 0) {
        if (g_tls_lock == NULL) {
            g_tls_lock = sw2_mutex();
        }
        sw2_lock(g_tls_lock);
        if (id->value == 0) {
            if (g_tls_next >= TLS_SLOTS) {
                sw2_unlock(g_tls_lock);
                return fail("out of thread-local slots");
            }
            id->value = ++g_tls_next;
        }
        sw2_unlock(g_tls_lock);
    }
    t_tls[id->value - 1] = value;
    return true;
}

void *SDL_GetTLS(SDL_TLSID *id)
{
    int v = id->value;

    return v >= 1 && v <= TLS_SLOTS ? (void *)t_tls[v - 1] : NULL;
}

/* ------------------------------------------------------------------ */
/* The window and the display                                          */
/* ------------------------------------------------------------------ */

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
    (void)flags;
    g_window = sw2_window_create(title, w, h);
    return (SDL_Window *)g_window;
}

void SDL_DestroyWindow(SDL_Window *win)
{
    if (win != NULL) {
        sw2_window_destroy(win);
    }
    if ((void *)win == g_window) {
        g_window = NULL;
    }
}

bool SDL_GetWindowSize(SDL_Window *win, int *w, int *h)
{
    int ww = 0, wh = 0;

    sw2_window_size(win, &ww, &wh);
    if (w != NULL) {
        *w = ww;
    }
    if (h != NULL) {
        *h = wh;
    }
    return true;
}

bool SDL_GetWindowSizeInPixels(SDL_Window *win, int *w, int *h) { return SDL_GetWindowSize(win, w, h); }

bool SDL_GetWindowSafeArea(SDL_Window *win, SDL_Rect *rect)
{
    rect->x = 0;
    rect->y = 0;
    return SDL_GetWindowSize(win, &rect->w, &rect->h);
}

bool SDL_GetWindowPosition(SDL_Window *win, int *x, int *y)
{
    (void)win;
    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    return true;
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *win)
{
    (void)win;
    return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS |
           SDL_WINDOW_MOUSE_FOCUS;
}

SDL_PropertiesID SDL_GetWindowProperties(SDL_Window *win)
{
    static SDL_PropertiesID props;

    (void)win;
    if (props == 0) {
        props = SDL_CreateProperties();
    }
    return props;
}

/* the window is the screen: it does not move, change size or hide */
bool SDL_SetWindowSize(SDL_Window *win, int w, int h) { (void)win; (void)w; (void)h; return true; }
bool SDL_SetWindowPosition(SDL_Window *win, int x, int y) { (void)win; (void)x; (void)y; return true; }
bool SDL_SetWindowBordered(SDL_Window *win, bool on) { (void)win; (void)on; return true; }
bool SDL_SetWindowFullscreen(SDL_Window *win, bool on) { (void)win; (void)on; return true; }
bool SDL_HideWindow(SDL_Window *win) { (void)win; return true; }
bool SDL_SetWindowIcon(SDL_Window *win, SDL_Surface *icon) { (void)win; (void)icon; return true; }

bool SDL_SetWindowFullscreenMode(SDL_Window *win, const SDL_DisplayMode *mode)
{
    (void)win;
    (void)mode;
    return true;
}

SDL_DisplayID SDL_GetPrimaryDisplay(void) { return 1; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *win) { (void)win; return 1; }

const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID id)
{
    static SDL_DisplayMode mode;
    int w, h, rate;

    (void)id;
    sw2_display_size(&w, &h, &rate);
    memset(&mode, 0, sizeof mode);
    mode.displayID = 1;
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = w;
    mode.h = h;
    mode.pixel_density = 1.0f;
    mode.refresh_rate = (float)rate;
    mode.refresh_rate_numerator = rate;
    mode.refresh_rate_denominator = 1;
    return &mode;
}

bool SDL_GetDisplayUsableBounds(SDL_DisplayID id, SDL_Rect *rect)
{
    int rate;

    (void)id;
    rect->x = 0;
    rect->y = 0;
    sw2_display_size(&rect->w, &rect->h, &rate);
    return true;
}

bool SDL_SetClipboardText(const char *text) { (void)text; return fail("no clipboard"); }
bool SDL_StartTextInput(SDL_Window *win) { (void)win; return true; }
bool SDL_StopTextInput(SDL_Window *win) { (void)win; return true; }

/* ------------------------------------------------------------------ */
/* OpenGL (ES)                                                         */
/* ------------------------------------------------------------------ */

bool SDL_GL_SetAttribute(SDL_GLAttr attr, int value)
{
    int which;

    switch (attr) {
    case SDL_GL_CONTEXT_PROFILE_MASK: which = SW2_GL_PROFILE; break;
    case SDL_GL_CONTEXT_MAJOR_VERSION: which = SW2_GL_MAJOR; break;
    case SDL_GL_CONTEXT_MINOR_VERSION: which = SW2_GL_MINOR; break;
    case SDL_GL_CONTEXT_FLAGS: which = SW2_GL_FLAGS; break;
    case SDL_GL_DEPTH_SIZE: which = SW2_GL_DEPTH; break;
    case SDL_GL_STENCIL_SIZE: which = SW2_GL_STENCIL; break;
    case SDL_GL_DOUBLEBUFFER: which = SW2_GL_DOUBLE; break;
    default: which = SW2_GL_OTHER; break;
    }
    /* the profile values are SDL2's too (core 1, compatibility 2, ES 4) */
    return sw2_gl_attribute(which, value) != 0;
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *win) { return (SDL_GLContext)sw2_gl_create(win); }
bool SDL_GL_MakeCurrent(SDL_Window *win, SDL_GLContext ctx) { return sw2_gl_make_current(win, ctx) != 0; }
bool SDL_GL_DestroyContext(SDL_GLContext ctx) { sw2_gl_destroy(ctx); return true; }

bool SDL_GL_SwapWindow(SDL_Window *win)
{
    sw2_gl_swap(win);
    return true;
}

bool SDL_GL_SetSwapInterval(int interval) { return sw2_gl_set_interval(interval) != 0; }

bool SDL_GL_GetSwapInterval(int *interval)
{
    *interval = sw2_gl_get_interval();
    return true;
}

SDL_FunctionPointer SDL_GL_GetProcAddress(const char *proc)
{
    return (SDL_FunctionPointer)sw2_gl_proc(proc);
}

/* ------------------------------------------------------------------ */
/* The 2D renderer (the first-run window) and surfaces                 */
/* ------------------------------------------------------------------ */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, const char *name)
{
    (void)name;
    return (SDL_Renderer *)sw2_renderer_create(win);
}

void SDL_DestroyRenderer(SDL_Renderer *r) { if (r != NULL) sw2_renderer_destroy(r); }
const char *SDL_GetRendererName(SDL_Renderer *r) { (void)r; return "opengles2"; }

bool SDL_GetCurrentRenderOutputSize(SDL_Renderer *r, int *w, int *h)
{
    sw2_renderer_output(r, w, h);
    return true;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, SDL_PixelFormat format, SDL_TextureAccess access,
                               int w, int h)
{
    return (SDL_Texture *)sw2_texture_create(r, (uint32_t)format, (int)access, w, h);
}

void SDL_DestroyTexture(SDL_Texture *t) { if (t != NULL) sw2_texture_destroy(t); }

bool SDL_SetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode mode)
{
    return sw2_texture_nearest(t, mode == SDL_SCALEMODE_NEAREST) != 0;
}

bool SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch)
{
    int r[4];

    if (rect != NULL) {
        r[0] = rect->x;
        r[1] = rect->y;
        r[2] = rect->w;
        r[3] = rect->h;
    }
    return sw2_texture_update(t, rect != NULL ? r : NULL, pixels, pitch) != 0;
}

bool SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) { return sw2_render_target(r, t) != 0; }

bool SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    return sw2_render_color(r, cr, cg, cb, ca) != 0;
}

bool SDL_RenderClear(SDL_Renderer *r) { return sw2_render_clear(r) != 0; }

bool SDL_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst)
{
    float s[4], d[4];

    if (src != NULL) {
        s[0] = src->x;
        s[1] = src->y;
        s[2] = src->w;
        s[3] = src->h;
    }
    if (dst != NULL) {
        d[0] = dst->x;
        d[1] = dst->y;
        d[2] = dst->w;
        d[3] = dst->h;
    }
    return sw2_render_copy(r, t, src != NULL ? s : NULL, dst != NULL ? d : NULL) != 0;
}

bool SDL_RenderPresent(SDL_Renderer *r)
{
    sw2_render_present(r);
    return true;
}

SDL_Surface *SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat format, void *pixels, int pitch)
{
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof *s);

    if (s == NULL) {
        fail("out of memory");
        return NULL;
    }
    s->flags = SDL_SURFACE_PREALLOCATED;
    s->format = format;
    s->w = w;
    s->h = h;
    s->pitch = pitch;
    s->pixels = pixels;
    s->refcount = 1;
    return s;
}

void SDL_DestroySurface(SDL_Surface *s) { free(s); }

bool SDL_SaveBMP(SDL_Surface *s, const char *file)
{
    return sw2_save_bmp(s->w, s->h, (uint32_t)s->format, s->pixels, s->pitch, file) != 0;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/* the events the host pushes itself, given back before the system's */
#define PUSHED_MAX 64
static SDL_Event g_pushed[PUSHED_MAX];
static int g_pushed_head, g_pushed_n;
static void *g_event_lock;

bool SDL_PushEvent(SDL_Event *ev)
{
    if (g_event_lock == NULL) {
        g_event_lock = sw2_mutex();
    }
    sw2_lock(g_event_lock);
    if (g_pushed_n == PUSHED_MAX) {
        sw2_unlock(g_event_lock);
        return fail("the event queue is full");
    }
    g_pushed[(g_pushed_head + g_pushed_n++) % PUSHED_MAX] = *ev;
    sw2_unlock(g_event_lock);
    return true;
}

void SDL_SetEventEnabled(Uint32 type, bool enabled) { (void)type; (void)enabled; }

static Uint32 window_id(void) { return g_window != NULL ? 1u : 0u; }

static int convert(const Sw2Event *s, SDL_Event *ev)
{
    memset(ev, 0, sizeof *ev);
    ev->common.timestamp = SDL_GetTicksNS();
    switch (s->type) {
    case SW2_EV_QUIT:
        ev->type = SDL_EVENT_QUIT;
        return 1;
    case SW2_EV_PAD_ADDED:
    case SW2_EV_PAD_REMOVED:
        ev->type = s->type == SW2_EV_PAD_ADDED ? SDL_EVENT_GAMEPAD_ADDED
                                               : SDL_EVENT_GAMEPAD_REMOVED;
        ev->gdevice.which = (SDL_JoystickID)s->a;
        return 1;
    case SW2_EV_PAD_BUTTON:
        ev->type = s->c ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        ev->gbutton.which = (SDL_JoystickID)s->a;
        ev->gbutton.button = (Uint8)s->b;
        ev->gbutton.down = s->c != 0;
        return 1;
    case SW2_EV_PAD_AXIS:
        ev->type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        ev->gaxis.which = (SDL_JoystickID)s->a;
        ev->gaxis.axis = (Uint8)s->b;
        ev->gaxis.value = (Sint16)s->c;
        return 1;
    case SW2_EV_KEY:
        ev->type = s->c ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        ev->key.windowID = window_id();
        ev->key.scancode = (SDL_Scancode)s->a;
        ev->key.key = (SDL_Keycode)s->b;
        ev->key.mod = (SDL_Keymod)(s->d & 0xFFFF);
        ev->key.down = s->c != 0;
        ev->key.repeat = (s->d >> 16) != 0;
        return 1;
    case SW2_EV_FINGER_DOWN:
    case SW2_EV_FINGER_UP:
    case SW2_EV_FINGER_MOTION:
        ev->type = s->type == SW2_EV_FINGER_DOWN ? SDL_EVENT_FINGER_DOWN :
                   s->type == SW2_EV_FINGER_UP ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
        ev->tfinger.touchID = (SDL_TouchID)s->a;
        ev->tfinger.fingerID = (SDL_FingerID)s->b;
        ev->tfinger.x = s->x;
        ev->tfinger.y = s->y;
        ev->tfinger.dx = s->dx;
        ev->tfinger.dy = s->dy;
        ev->tfinger.pressure = s->pressure;
        ev->tfinger.windowID = window_id();
        return 1;
    case SW2_EV_MOUSE_MOTION:
        ev->type = SDL_EVENT_MOUSE_MOTION;
        ev->motion.windowID = window_id();
        ev->motion.which = (SDL_MouseID)s->a;
        ev->motion.state = (SDL_MouseButtonFlags)s->b;
        ev->motion.x = s->x;
        ev->motion.y = s->y;
        ev->motion.xrel = s->dx;
        ev->motion.yrel = s->dy;
        return 1;
    case SW2_EV_MOUSE_BUTTON:
        ev->type = s->c ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        ev->button.windowID = window_id();
        ev->button.which = (SDL_MouseID)s->a;
        ev->button.button = (Uint8)s->b;
        ev->button.down = s->c != 0;
        ev->button.clicks = (Uint8)s->d;
        ev->button.x = s->x;
        ev->button.y = s->y;
        return 1;
    case SW2_EV_RESIZED:
        ev->type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
        ev->window.windowID = window_id();
        ev->window.data1 = (Sint32)s->a;
        ev->window.data2 = (Sint32)s->b;
        return 1;
    case SW2_EV_BACKGROUND:
        ev->type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        return 1;
    case SW2_EV_FOREGROUND:
        ev->type = SDL_EVENT_DID_ENTER_FOREGROUND;
        return 1;
    default:
        return 0;
    }
}

bool SDL_PollEvent(SDL_Event *ev)
{
    Sw2Event s;
    SDL_Event tmp;

    if (ev == NULL) {
        ev = &tmp;
    }
    if (g_pushed_n > 0 && g_event_lock != NULL) {
        sw2_lock(g_event_lock);
        if (g_pushed_n > 0) {
            *ev = g_pushed[g_pushed_head];
            g_pushed_head = (g_pushed_head + 1) % PUSHED_MAX;
            g_pushed_n--;
            sw2_unlock(g_event_lock);
            return true;
        }
        sw2_unlock(g_event_lock);
    }
    while (sw2_poll(&s)) {
        if (convert(&s, ev)) {
            return true;
        }
    }
    return false;
}

bool SDL_WaitEventTimeout(SDL_Event *ev, Sint32 ms)
{
    Uint64 until = SDL_GetTicks() + (ms < 0 ? 0 : (Uint64)ms);

    for (;;) {
        if (SDL_PollEvent(ev)) {
            return true;
        }
        if (ms >= 0 && SDL_GetTicks() >= until) {
            return false;
        }
        sw2_delay(1);
    }
}

/* ------------------------------------------------------------------ */
/* Gamepads                                                            */
/* ------------------------------------------------------------------ */

bool SDL_IsGamepad(SDL_JoystickID id) { return sw2_pad_is_gamepad((int32_t)id) != 0; }
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { return (SDL_Gamepad *)sw2_pad_open((int32_t)id); }
void SDL_CloseGamepad(SDL_Gamepad *pad) { if (pad != NULL) sw2_pad_close(pad); }
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *pad) { return (SDL_JoystickID)sw2_pad_id(pad); }

/* SDL3 numbers the buttons and axes as SDL2 did (south/A is 0, and on) */
bool SDL_GetGamepadButton(SDL_Gamepad *pad, SDL_GamepadButton b) { return sw2_pad_button(pad, (int)b) != 0; }
Sint16 SDL_GetGamepadAxis(SDL_Gamepad *pad, SDL_GamepadAxis a) { return (Sint16)sw2_pad_axis(pad, (int)a); }

SDL_GamepadType SDL_GetGamepadTypeForID(SDL_JoystickID id)
{
    (void)id;
    return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
}

/* SDL's own names for the buttons and axes (its mapping strings) */
static const char *const k_buttons[] = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
    "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
    "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
};
static const char *const k_axes[] = {
    "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger",
};

const char *SDL_GetGamepadStringForButton(SDL_GamepadButton b)
{
    return (int)b >= 0 && (size_t)b < sizeof k_buttons / sizeof k_buttons[0] ? k_buttons[b] : NULL;
}

const char *SDL_GetGamepadStringForAxis(SDL_GamepadAxis a)
{
    return (int)a >= 0 && (size_t)a < sizeof k_axes / sizeof k_axes[0] ? k_axes[a] : NULL;
}

SDL_GamepadButton SDL_GetGamepadButtonFromString(const char *s)
{
    size_t i;

    for (i = 0; s != NULL && i < sizeof k_buttons / sizeof k_buttons[0]; i++) {
        if (strcmp(s, k_buttons[i]) == 0) {
            return (SDL_GamepadButton)i;
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

SDL_GamepadAxis SDL_GetGamepadAxisFromString(const char *s)
{
    size_t i;

    for (i = 0; s != NULL && i < sizeof k_axes / sizeof k_axes[0]; i++) {
        if (strcmp(s, k_axes[i]) == 0) {
            return (SDL_GamepadAxis)i;
        }
    }
    return SDL_GAMEPAD_AXIS_INVALID;
}

/* ------------------------------------------------------------------ */
/* Keyboard and mouse (a USB keyboard, the touch screen's mouse)       */
/* ------------------------------------------------------------------ */

const bool *SDL_GetKeyboardState(int *n) { return (const bool *)sw2_keys(n); }
const char *SDL_GetScancodeName(SDL_Scancode sc) { return sw2_scancode_name((int)sc); }
SDL_Scancode SDL_GetScancodeFromName(const char *name) { return (SDL_Scancode)sw2_scancode_from_name(name); }
SDL_MouseButtonFlags SDL_GetMouseState(float *x, float *y) { return (SDL_MouseButtonFlags)sw2_mouse(x, y); }
SDL_Window *SDL_GetMouseFocus(void) { return (SDL_Window *)g_window; }

/* ------------------------------------------------------------------ */
/* Audio: one stream on one device, pushed to                          */
/* ------------------------------------------------------------------ */

struct SDL_AudioStream { uint32_t dev; };

bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID dev, SDL_AudioSpec *spec, int *frames)
{
    (void)dev;
    /* the Switch's own output */
    spec->format = SDL_AUDIO_S16;
    spec->channels = 2;
    spec->freq = 48000;
    if (frames != NULL) {
        *frames = 1024;
    }
    return true;
}

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback cb, void *user)
{
    SDL_AudioStream *s;
    uint32_t d;

    (void)dev;
    (void)user;
    if (cb != NULL) {
        fail("no audio callbacks here");
        return NULL;
    }
    /* SDL2 converts from the spec asked for, as an SDL3 stream does; the
     * device opens paused, as SDL3's does */
    d = sw2_audio_open(spec->freq, (int)spec->format, spec->channels);
    if (d == 0) {
        return NULL;
    }
    s = (SDL_AudioStream *)calloc(1, sizeof *s);
    if (s == NULL) {
        sw2_audio_close(d);
        fail("out of memory");
        return NULL;
    }
    s->dev = d;
    return s;
}

void SDL_DestroyAudioStream(SDL_AudioStream *s)
{
    if (s != NULL) {
        sw2_audio_close(s->dev);
        free(s);
    }
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *s) { sw2_audio_resume(s->dev); return true; }
bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len) { return sw2_audio_put(s->dev, buf, len) != 0; }
int SDL_GetAudioStreamQueued(SDL_AudioStream *s) { return sw2_audio_queued(s->dev); }
bool SDL_LockAudioStream(SDL_AudioStream *s) { sw2_audio_lock(s->dev); return true; }
bool SDL_UnlockAudioStream(SDL_AudioStream *s) { sw2_audio_unlock(s->dev); return true; }
