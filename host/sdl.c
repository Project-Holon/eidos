/*
 * host/sdl.c -- the module's SDL3 imports (host/), answered with this
 * program's SDL3.
 *
 * The module calls SDL as it does on every other build (engine/platform/);
 * what it gets here is the same call, with three translations:
 *
 *   addresses   a pointer the module passes is an offset into its memory
 *               (host_ptr); a string or an array SDL hands back is copied
 *               into a block of module memory the module's own malloc made
 *   handles     SDL's objects (a window, a texture, a gamepad, a surface,
 *               the audio stream, the GL context) are this program's
 *               pointers, which do not fit the module's 32 bits: the module
 *               holds a small number for each (handle_of / handle_ptr)
 *   events      SDL_Event is the same bytes on both sides except where an
 *               event carries a string (text input, a dropped file): that
 *               string is copied into module memory and the pointer, which
 *               sits at a different offset with 4-byte pointers, written
 *               where the module's SDL_Event has it
 *
 * The module's SDL structs are laid out as wasm32 lays them out
 * (engine/platform/ keeps the natural layout, sdk/wasm_layout.h): every one
 * used here is plain numbers but SDL_Event's string members.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED   /* main() is host/main.c's */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "host.h"

/* ------------------------------------------------------------------ */
/* Handles                                                             */
/* ------------------------------------------------------------------ */

#define HANDLES_MAX 4096

static void *g_handle[HANDLES_MAX];

static u32 handle_of(void *p)
{
    u32 i, free_at = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 1; i < HANDLES_MAX; i++) {
        if (g_handle[i] == p) {
            return i;
        }
        if (g_handle[i] == NULL && free_at == 0) {
            free_at = i;
        }
    }
    if (free_at == 0) {
        fprintf(stderr, "eidos: out of SDL handles\n");
        abort();
    }
    g_handle[free_at] = p;
    return free_at;
}

static void *handle_ptr(u32 h)
{
    return h > 0 && h < HANDLES_MAX ? g_handle[h] : NULL;
}

static void handle_drop(u32 h)
{
    if (h > 0 && h < HANDLES_MAX) {
        g_handle[h] = NULL;
    }
}

/* The same table for the GPU imports (host/sdl_gpu.c): one handle space,
 * so a window made here is the window SDL's GPU claims there. */
u32 host_handle_of(void *p) { return handle_of(p); }
void *host_handle_ptr(u32 h) { return handle_ptr(h); }
void host_handle_drop(u32 h) { handle_drop(h); }

/* ------------------------------------------------------------------ */
/* Module memory                                                       */
/* ------------------------------------------------------------------ */

static void *M(u32 addr) { return addr == 0 ? NULL : host_ptr(addr, 0); }

/* A string for the module: copied into a block of its memory kept for
 * `slot` (one per kind of answer, so an answer stays valid until the same
 * kind is asked again, as SDL's own do). */
#define SLOTS 32
static u32 g_slot_addr[SLOTS];
static u32 g_slot_size[SLOTS];

static u32 to_module(int slot, const void *data, size_t n)
{
    if (data == NULL) {
        return 0;
    }
    if (g_slot_size[slot] < n) {
        if (g_slot_addr[slot] != 0) {
            host_free(g_slot_addr[slot]);
        }
        g_slot_size[slot] = (u32)(n < 256 ? 256 : n);
        g_slot_addr[slot] = host_malloc(g_slot_size[slot]);
        if (g_slot_addr[slot] == 0) {
            g_slot_size[slot] = 0;
            return 0;
        }
    }
    memcpy(host_ptr(g_slot_addr[slot], (u32)n), data, n);
    return g_slot_addr[slot];
}

static u32 str_to_module(int slot, const char *s)
{
    return s == NULL ? 0 : to_module(slot, s, strlen(s) + 1);
}

/* Interned strings that SDL keeps for good (a key's name, a pad button's
 * name): copied once each, so any number of them stay valid together. */
static u32 str_interned(const char *s)
{
    static const char *key[512];
    static u32 val[512];
    int i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < 512 && key[i] != NULL; i++) {
        if (key[i] == s) {
            return val[i];
        }
    }
    if (i == 512) {
        return str_to_module(31, s);
    }
    val[i] = host_malloc((u32)strlen(s) + 1);
    if (val[i] != 0) {
        memcpy(host_ptr(val[i], 0), s, strlen(s) + 1);
        key[i] = s;
    }
    return val[i];
}

enum { S_ERROR, S_DRIVER, S_RENDERER, S_KEYS, S_TEXT, S_DROP, S_DROP2, S_MODE };

/* ------------------------------------------------------------------ */
/* Init, hints, time                                                   */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_Init(struct w2c_env *e, u32 flags) { (void)e; return SDL_Init(flags); }
u32 w2c_env_SDL_InitSubSystem(struct w2c_env *e, u32 flags) { (void)e; return SDL_InitSubSystem(flags); }
void w2c_env_SDL_QuitSubSystem(struct w2c_env *e, u32 flags) { (void)e; SDL_QuitSubSystem(flags); }
u32 w2c_env_SDL_WasInit(struct w2c_env *e, u32 flags) { (void)e; return SDL_WasInit(flags); }
void w2c_env_SDL_Quit(struct w2c_env *e) { (void)e; SDL_Quit(); }
void w2c_env_SDL_SetMainReady(struct w2c_env *e) { (void)e; SDL_SetMainReady(); }

u32 w2c_env_SDL_SetHint(struct w2c_env *e, u32 name, u32 value)
{
    (void)e;
    return SDL_SetHint((const char *)M(name), (const char *)M(value));
}

u32 w2c_env_SDL_GetError(struct w2c_env *e)
{
    (void)e;
    return str_to_module(S_ERROR, SDL_GetError());
}

u64 w2c_env_SDL_GetTicks(struct w2c_env *e) { (void)e; return SDL_GetTicks(); }
u64 w2c_env_SDL_GetPerformanceCounter(struct w2c_env *e) { (void)e; return SDL_GetPerformanceCounter(); }
u64 w2c_env_SDL_GetPerformanceFrequency(struct w2c_env *e) { (void)e; return SDL_GetPerformanceFrequency(); }
void w2c_env_SDL_Delay(struct w2c_env *e, u32 ms) { (void)e; SDL_Delay(ms); }
u64 w2c_env_SDL_GetCurrentThreadID(struct w2c_env *e) { (void)e; return 1; }   /* one thread */

u32 w2c_env_SDL_SetAtomicInt(struct w2c_env *e, u32 a, u32 v)
{
    u32 old;

    (void)e;
    memcpy(&old, host_ptr(a, 4), 4);
    memcpy(host_ptr(a, 4), &v, 4);      /* the fibers take turns: no race */
    return old;
}

u32 w2c_env_SDL_GetAtomicInt(struct w2c_env *e, u32 a)
{
    u32 v;

    (void)e;
    memcpy(&v, host_ptr(a, 4), 4);
    return v;
}

u32 w2c_env_SDL_SetClipboardText(struct w2c_env *e, u32 text)
{
    (void)e;
    return SDL_SetClipboardText((const char *)M(text));
}

u32 w2c_env_SDL_GetCurrentVideoDriver(struct w2c_env *e)
{
    (void)e;
    return str_to_module(S_DRIVER, SDL_GetCurrentVideoDriver());
}

/* The benchmark's notes (engine/frame/prof.h): the system and its cores. */
u32 w2c_env_SDL_GetPlatform(struct w2c_env *e)
{
    (void)e;
    return str_interned(SDL_GetPlatform());
}

u32 w2c_env_SDL_GetNumLogicalCPUCores(struct w2c_env *e)
{
    (void)e;
    return (u32)SDL_GetNumLogicalCPUCores();
}

/* ------------------------------------------------------------------ */
/* Displays and windows                                                */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_GetPrimaryDisplay(struct w2c_env *e) { (void)e; return SDL_GetPrimaryDisplay(); }

u32 w2c_env_SDL_GetDisplayUsableBounds(struct w2c_env *e, u32 id, u32 rect)
{
    (void)e;
    return SDL_GetDisplayUsableBounds(id, (SDL_Rect *)M(rect));
}

u32 w2c_env_SDL_GetDisplayForWindow(struct w2c_env *e, u32 w)
{
    (void)e;
    return SDL_GetDisplayForWindow((SDL_Window *)handle_ptr(w));
}

/* SDL_DisplayMode as the wasm32 module lays it out: eight 4-byte fields
 * and the internal pointer, 4 bytes there. */
typedef struct {
    u32 displayID, format;
    int32_t w, h;
    float pixel_density, refresh_rate;
    int32_t refresh_rate_numerator, refresh_rate_denominator;
    u32 internal;
} WDisplayMode;
_Static_assert(sizeof(WDisplayMode) == 36, "wasm32 SDL_DisplayMode");

u32 w2c_env_SDL_GetCurrentDisplayMode(struct w2c_env *e, u32 id)
{
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(id);
    WDisplayMode w;

    (void)e;
    if (m == NULL) {
        return 0;
    }
    w.displayID = m->displayID;
    w.format = (u32)m->format;
    w.w = m->w;
    w.h = m->h;
    w.pixel_density = m->pixel_density;
    w.refresh_rate = m->refresh_rate;
    w.refresh_rate_numerator = m->refresh_rate_numerator;
    w.refresh_rate_denominator = m->refresh_rate_denominator;
    w.internal = 0;
    return to_module(S_MODE, &w, sizeof w);
}

u32 w2c_env_SDL_CreateWindow(struct w2c_env *e, u32 title, u32 w, u32 h, u64 flags)
{
    (void)e;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    /* A phone has one screen-sized window (the setup window's too), with
     * the system's bars hidden rather than drawn over it. */
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    return handle_of(SDL_CreateWindow((const char *)M(title), (int)w, (int)h, (SDL_WindowFlags)flags));
}

void w2c_env_SDL_DestroyWindow(struct w2c_env *e, u32 w)
{
    (void)e;
    SDL_DestroyWindow((SDL_Window *)handle_ptr(w));
    handle_drop(w);
}

u64 w2c_env_SDL_GetWindowFlags(struct w2c_env *e, u32 w)
{
    (void)e;
    return SDL_GetWindowFlags((SDL_Window *)handle_ptr(w));
}

u32 w2c_env_SDL_GetWindowSize(struct w2c_env *e, u32 w, u32 pw, u32 ph)
{
    (void)e;
    return SDL_GetWindowSize((SDL_Window *)handle_ptr(w), (int *)M(pw), (int *)M(ph));
}

u32 w2c_env_SDL_GetWindowSizeInPixels(struct w2c_env *e, u32 w, u32 pw, u32 ph)
{
    (void)e;
    return SDL_GetWindowSizeInPixels((SDL_Window *)handle_ptr(w), (int *)M(pw), (int *)M(ph));
}

u32 w2c_env_SDL_GetWindowSafeArea(struct w2c_env *e, u32 w, u32 rect)
{
    static SDL_Rect last;
    SDL_Rect *r = (SDL_Rect *)M(rect);
    u32 rc;

    (void)e;
    rc = SDL_GetWindowSafeArea((SDL_Window *)handle_ptr(w), r);     /* four ints */
    if (rc && (r->x != last.x || r->y != last.y || r->w != last.w || r->h != last.h)) {
        last = *r;
        fprintf(stderr, "eidos: safe area now %d,%d %dx%d\n", r->x, r->y, r->w, r->h);
    }
    return rc;
}

u32 w2c_env_SDL_SetWindowSize(struct w2c_env *e, u32 w, u32 x, u32 y)
{
    (void)e;
    return SDL_SetWindowSize((SDL_Window *)handle_ptr(w), (int)x, (int)y);
}

u32 w2c_env_SDL_GetWindowPosition(struct w2c_env *e, u32 w, u32 px, u32 py)
{
    (void)e;
    return SDL_GetWindowPosition((SDL_Window *)handle_ptr(w), (int *)M(px), (int *)M(py));
}

u32 w2c_env_SDL_SetWindowPosition(struct w2c_env *e, u32 w, u32 x, u32 y)
{
    (void)e;
    return SDL_SetWindowPosition((SDL_Window *)handle_ptr(w), (int)x, (int)y);
}

u32 w2c_env_SDL_SetWindowFullscreen(struct w2c_env *e, u32 w, u32 on)
{
    (void)e;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    /* a phone's window stays fullscreen whatever the game's window mode
     * setting (a desktop's) asks: windowed, Android shows its status and
     * navigation bars over the game */
    on = 1;
#endif
    return SDL_SetWindowFullscreen((SDL_Window *)handle_ptr(w), on != 0);
}

u32 w2c_env_SDL_SetWindowFullscreenMode(struct w2c_env *e, u32 w, u32 mode)
{
    (void)e;
    /* only ever NULL (desktop fullscreen, engine/platform/platform.c) */
    (void)mode;
    return SDL_SetWindowFullscreenMode((SDL_Window *)handle_ptr(w), NULL);
}

u32 w2c_env_SDL_SetWindowBordered(struct w2c_env *e, u32 w, u32 on)
{
    (void)e;
    return SDL_SetWindowBordered((SDL_Window *)handle_ptr(w), on != 0);
}

u32 w2c_env_SDL_SetWindowIcon(struct w2c_env *e, u32 w, u32 surf)
{
    (void)e;
    return SDL_SetWindowIcon((SDL_Window *)handle_ptr(w), (SDL_Surface *)handle_ptr(surf));
}

u32 w2c_env_SDL_HideWindow(struct w2c_env *e, u32 w)
{
    (void)e;
    return SDL_HideWindow((SDL_Window *)handle_ptr(w));
}

u32 w2c_env_SDL_GetWindowProperties(struct w2c_env *e, u32 w)
{
    (void)e;
    return SDL_GetWindowProperties((SDL_Window *)handle_ptr(w));
}

u32 w2c_env_SDL_GetPointerProperty(struct w2c_env *e, u32 props, u32 name, u32 dflt)
{
    (void)e;
    (void)props;
    (void)name;
    return dflt;        /* a native window handle means nothing to the module */
}

u32 w2c_env_SDL_StartTextInput(struct w2c_env *e, u32 w)
{
    (void)e;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    /* The name entry's keyboard is the game's own, played by touch.  Text
     * input on a phone raises the system's keyboard over it (iOS for any
     * text input, whatever SDL's hints say), so it is only started with a
     * hardware keyboard to type on. */
    if (!SDL_HasKeyboard()) {
        return 1;
    }
#endif
    return SDL_StartTextInput((SDL_Window *)handle_ptr(w));
}

u32 w2c_env_SDL_StopTextInput(struct w2c_env *e, u32 w)
{
    (void)e;
    return SDL_StopTextInput((SDL_Window *)handle_ptr(w));
}

/* ------------------------------------------------------------------ */
/* Surfaces, the renderer, textures                                    */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_CreateSurfaceFrom(struct w2c_env *e, u32 w, u32 h, u32 fmt, u32 pixels, u32 pitch)
{
    (void)e;
    /* the pixels stay in module memory, which never moves (host/CMakeLists.txt) */
    return handle_of(SDL_CreateSurfaceFrom((int)w, (int)h, (SDL_PixelFormat)fmt, M(pixels), (int)pitch));
}

void w2c_env_SDL_DestroySurface(struct w2c_env *e, u32 s)
{
    (void)e;
    SDL_DestroySurface((SDL_Surface *)handle_ptr(s));
    handle_drop(s);
}

u32 w2c_env_SDL_SaveBMP(struct w2c_env *e, u32 s, u32 path)
{
    (void)e;
    return SDL_SaveBMP((SDL_Surface *)handle_ptr(s), (const char *)M(path));
}

u32 w2c_env_SDL_CreateRenderer(struct w2c_env *e, u32 w, u32 name)
{
    (void)e;
    return handle_of(SDL_CreateRenderer((SDL_Window *)handle_ptr(w), (const char *)M(name)));
}

void w2c_env_SDL_DestroyRenderer(struct w2c_env *e, u32 r)
{
    (void)e;
    SDL_DestroyRenderer((SDL_Renderer *)handle_ptr(r));
    handle_drop(r);
}

u32 w2c_env_SDL_GetRendererName(struct w2c_env *e, u32 r)
{
    (void)e;
    return str_to_module(S_RENDERER, SDL_GetRendererName((SDL_Renderer *)handle_ptr(r)));
}

u32 w2c_env_SDL_GetCurrentRenderOutputSize(struct w2c_env *e, u32 r, u32 pw, u32 ph)
{
    (void)e;
    return SDL_GetCurrentRenderOutputSize((SDL_Renderer *)handle_ptr(r), (int *)M(pw), (int *)M(ph));
}

u32 w2c_env_SDL_SetRenderDrawColor(struct w2c_env *e, u32 r, u32 cr, u32 cg, u32 cb, u32 ca)
{
    (void)e;
    return SDL_SetRenderDrawColor((SDL_Renderer *)handle_ptr(r), (Uint8)cr, (Uint8)cg, (Uint8)cb, (Uint8)ca);
}

u32 w2c_env_SDL_RenderClear(struct w2c_env *e, u32 r) { (void)e; return SDL_RenderClear((SDL_Renderer *)handle_ptr(r)); }
u32 w2c_env_SDL_RenderPresent(struct w2c_env *e, u32 r) { (void)e; return SDL_RenderPresent((SDL_Renderer *)handle_ptr(r)); }

u32 w2c_env_SDL_SetRenderTarget(struct w2c_env *e, u32 r, u32 t)
{
    (void)e;
    return SDL_SetRenderTarget((SDL_Renderer *)handle_ptr(r), (SDL_Texture *)handle_ptr(t));
}

u32 w2c_env_SDL_RenderTexture(struct w2c_env *e, u32 r, u32 t, u32 src, u32 dst)
{
    (void)e;
    return SDL_RenderTexture((SDL_Renderer *)handle_ptr(r), (SDL_Texture *)handle_ptr(t),
                             (const SDL_FRect *)M(src), (const SDL_FRect *)M(dst));
}

u32 w2c_env_SDL_CreateTexture(struct w2c_env *e, u32 r, u32 fmt, u32 access, u32 w, u32 h)
{
    (void)e;
    return handle_of(SDL_CreateTexture((SDL_Renderer *)handle_ptr(r), (SDL_PixelFormat)fmt,
                                       (SDL_TextureAccess)access, (int)w, (int)h));
}

void w2c_env_SDL_DestroyTexture(struct w2c_env *e, u32 t)
{
    (void)e;
    SDL_DestroyTexture((SDL_Texture *)handle_ptr(t));
    handle_drop(t);
}

u32 w2c_env_SDL_UpdateTexture(struct w2c_env *e, u32 t, u32 rect, u32 pixels, u32 pitch)
{
    (void)e;
    return SDL_UpdateTexture((SDL_Texture *)handle_ptr(t), (const SDL_Rect *)M(rect), M(pixels), (int)pitch);
}

u32 w2c_env_SDL_SetTextureScaleMode(struct w2c_env *e, u32 t, u32 mode)
{
    (void)e;
    return SDL_SetTextureScaleMode((SDL_Texture *)handle_ptr(t), (SDL_ScaleMode)mode);
}

/* ------------------------------------------------------------------ */
/* OpenGL context                                                      */
/* ------------------------------------------------------------------ */

int host_gl_es;
int host_gl_es_version;
unsigned host_gl_window_fbo;

/* The module asks for OpenGL 3.3 core.  Android and iOS have OpenGL ES 3.0
 * instead, whose shading language and calls the OpenGL imports translate
 * to (tools/wasm_gl_imports.py). */
u32 w2c_env_SDL_GL_SetAttribute(struct w2c_env *e, u32 attr, u32 v)
{
    (void)e;
#if defined(HOST_GLES)
    switch ((SDL_GLAttr)attr) {
    case SDL_GL_CONTEXT_PROFILE_MASK: v = SDL_GL_CONTEXT_PROFILE_ES; break;
    case SDL_GL_CONTEXT_MAJOR_VERSION: v = 3; break;
    case SDL_GL_CONTEXT_MINOR_VERSION: v = 0; break;
    case SDL_GL_CONTEXT_FLAGS: v = 0; break;
    default: break;
    }
#endif
    return SDL_GL_SetAttribute((SDL_GLAttr)attr, (int)v);
}

/* The OpenGL driver on stderr, for reports of a slow game: Mesa's llvmpipe
 * and softpipe draw on the CPU. */
static void report_gl(void)
{
    typedef const unsigned char *(SDLCALL *GetString)(unsigned name);
    GetString get = (GetString)SDL_GL_GetProcAddress("glGetString");
    const char *r = get != NULL ? (const char *)get(0x1F01u) : NULL;   /* GL_RENDERER */
    const char *v = get != NULL ? (const char *)get(0x1F02u) : NULL;   /* GL_VERSION */

    fprintf(stderr, "eidos: OpenGL %s (%s)\n", r != NULL ? r : "?", v != NULL ? v : "?");
    host_gl_es = v != NULL && strncmp(v, "OpenGL ES", 9) == 0;
    if (host_gl_es) {
        int major = 0, minor = 0;

        /* "OpenGL ES 3.0 <vendor's own>" */
        if (sscanf(v + 9, " %d.%d", &major, &minor) == 2) {
            host_gl_es_version = major * 10 + minor;
        }
    }
    if (r != NULL && (strstr(r, "llvmpipe") != NULL || strstr(r, "softpipe") != NULL ||
                      strstr(r, "Software") != NULL)) {
        fprintf(stderr, "eidos: this OpenGL draws on the CPU, so the game runs slowly; "
                "install the graphics card's driver\n");
    }
}

u32 w2c_env_SDL_GL_CreateContext(struct w2c_env *e, u32 w)
{
    SDL_GLContext c;

    (void)e;
    c = SDL_GL_CreateContext((SDL_Window *)handle_ptr(w));
#if defined(HOST_GL_ES_FALLBACK)
    /* The Switch: Mesa's desktop OpenGL first (the shaders as they are, the
     * GPU 3D with its anti-aliasing), OpenGL ES 3.0 when that fails. */
    if (c == NULL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        c = SDL_GL_CreateContext((SDL_Window *)handle_ptr(w));
    }
#endif
    if (c != NULL) {
        SDL_Rect safe;
        int ww = 0, wh = 0;

        report_gl();
        if (SDL_GetWindowSafeArea((SDL_Window *)handle_ptr(w), &safe)
            && SDL_GetWindowSize((SDL_Window *)handle_ptr(w), &ww, &wh)
            && (safe.x != 0 || safe.y != 0 || safe.w != ww || safe.h != wh)) {
            fprintf(stderr, "eidos: window %dx%d, safe area %d,%d %dx%d\n",
                    ww, wh, safe.x, safe.y, safe.w, safe.h);
        }
#if defined(SDL_PLATFORM_IOS)
        host_gl_window_fbo = (unsigned)SDL_GetNumberProperty(
            SDL_GetWindowProperties((SDL_Window *)handle_ptr(w)),
            SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
#endif
    }
    return handle_of(c);
}

u32 w2c_env_SDL_GL_DestroyContext(struct w2c_env *e, u32 c)
{
    u32 rc;

    (void)e;
    rc = SDL_GL_DestroyContext((SDL_GLContext)handle_ptr(c));
    handle_drop(c);
    return rc;
}

u32 w2c_env_SDL_GL_MakeCurrent(struct w2c_env *e, u32 w, u32 c)
{
    (void)e;
    return SDL_GL_MakeCurrent((SDL_Window *)handle_ptr(w), (SDL_GLContext)handle_ptr(c));
}

u32 w2c_env_SDL_GL_SetSwapInterval(struct w2c_env *e, u32 n) { (void)e; return SDL_GL_SetSwapInterval((int)n); }
u32 w2c_env_SDL_GL_GetSwapInterval(struct w2c_env *e, u32 out)
{
    int n = 0;
    bool ok = SDL_GL_GetSwapInterval(&n);

    (void)e;
    if (out != 0) {
        memcpy(host_ptr(out, 4), &n, 4);
    }
    return ok;
}
/* EIDOS_FPS: every 120 frames, the frames per second and how much of a
 * frame went to the game and its drawing calls (between swaps) and to the
 * swap itself (waiting for the GPU and the display). */
u32 w2c_env_SDL_GL_SwapWindow(struct w2c_env *e, u32 w)
{
    static int on = -1;
    static Uint64 last, work, swap, start;
    static int n;
    Uint64 t0, t1;
    u32 rc;

    (void)e;
    if (on < 0) {
        on = getenv("EIDOS_FPS") != NULL;
    }
    if (!on) {
        return SDL_GL_SwapWindow((SDL_Window *)handle_ptr(w));
    }
    t0 = SDL_GetTicksNS();
    if (last != 0) {
        work += t0 - last;
    } else {
        start = t0;
    }
    rc = SDL_GL_SwapWindow((SDL_Window *)handle_ptr(w));
    t1 = SDL_GetTicksNS();
    swap += t1 - t0;
    last = t1;
    if (++n == 120) {
        fprintf(stderr, "eidos: fps %.1f, per frame %.2f ms between swaps, %.2f ms in the swap\n",
                120.0 * 1e9 / (double)(t1 - start), (double)work / 120e6, (double)swap / 120e6);
        n = 0;
        work = swap = 0;
        start = t1;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Keyboard, mouse, gamepads                                           */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_GetKeyboardState(struct w2c_env *e, u32 numkeys)
{
    int n = 0;
    const bool *k = SDL_GetKeyboardState(&n);

    (void)e;
    if (numkeys != 0) {
        memcpy(host_ptr(numkeys, 4), &n, 4);
    }
    return to_module(S_KEYS, k, (size_t)n * sizeof(bool));
}

u32 w2c_env_SDL_GetMouseState(struct w2c_env *e, u32 px, u32 py)
{
    (void)e;
    return SDL_GetMouseState((float *)M(px), (float *)M(py));
}

u32 w2c_env_SDL_GetMouseFocus(struct w2c_env *e)
{
    (void)e;
    return handle_of(SDL_GetMouseFocus());
}

u32 w2c_env_SDL_GetScancodeName(struct w2c_env *e, u32 sc)
{
    (void)e;
    return str_interned(SDL_GetScancodeName((SDL_Scancode)sc));
}

u32 w2c_env_SDL_GetScancodeFromName(struct w2c_env *e, u32 name)
{
    (void)e;
    return SDL_GetScancodeFromName((const char *)M(name));
}

u32 w2c_env_SDL_IsGamepad(struct w2c_env *e, u32 id) { (void)e; return SDL_IsGamepad(id); }

u32 w2c_env_SDL_OpenGamepad(struct w2c_env *e, u32 id)
{
    (void)e;
    return handle_of(SDL_OpenGamepad(id));
}

void w2c_env_SDL_CloseGamepad(struct w2c_env *e, u32 g)
{
    (void)e;
    SDL_CloseGamepad((SDL_Gamepad *)handle_ptr(g));
    handle_drop(g);
}

u32 w2c_env_SDL_GetGamepadID(struct w2c_env *e, u32 g) { (void)e; return SDL_GetGamepadID((SDL_Gamepad *)handle_ptr(g)); }
u32 w2c_env_SDL_GetGamepadTypeForID(struct w2c_env *e, u32 id) { (void)e; return (u32)SDL_GetGamepadTypeForID(id); }

u32 w2c_env_SDL_GetGamepadButton(struct w2c_env *e, u32 g, u32 b)
{
    (void)e;
    return SDL_GetGamepadButton((SDL_Gamepad *)handle_ptr(g), (SDL_GamepadButton)b);
}

u32 w2c_env_SDL_GetGamepadAxis(struct w2c_env *e, u32 g, u32 a)
{
    (void)e;
    return (u32)(s32)SDL_GetGamepadAxis((SDL_Gamepad *)handle_ptr(g), (SDL_GamepadAxis)a);
}

u32 w2c_env_SDL_GetGamepadStringForButton(struct w2c_env *e, u32 b)
{
    (void)e;
    return str_interned(SDL_GetGamepadStringForButton((SDL_GamepadButton)b));
}

u32 w2c_env_SDL_GetGamepadStringForAxis(struct w2c_env *e, u32 a)
{
    (void)e;
    return str_interned(SDL_GetGamepadStringForAxis((SDL_GamepadAxis)a));
}

u32 w2c_env_SDL_GetGamepadButtonFromString(struct w2c_env *e, u32 s)
{
    (void)e;
    return (u32)SDL_GetGamepadButtonFromString((const char *)M(s));
}

u32 w2c_env_SDL_GetGamepadAxisFromString(struct w2c_env *e, u32 s)
{
    (void)e;
    return (u32)SDL_GetGamepadAxisFromString((const char *)M(s));
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/* The module's SDL_Event: the same bytes, but a string member sits where
 * a 4-byte pointer puts it (SDL_TextInputEvent: windowID at 16, text at
 * 20; SDL_DropEvent: windowID 16, x 20, y 24, source 28, data 32). */
static void event_to_module(const SDL_Event *ev, u32 out)
{
    u8 *m = (u8 *)host_ptr(out, sizeof(SDL_Event));

    memcpy(m, ev, sizeof(SDL_Event));
    switch (ev->type) {
    case SDL_EVENT_TEXT_INPUT: {
        u32 t = str_to_module(S_TEXT, ev->text.text);

        memcpy(m + 16, &ev->text.windowID, 4);
        memcpy(m + 20, &t, 4);
        break;
    }
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
    case SDL_EVENT_DROP_POSITION: {
        u32 src = str_to_module(S_DROP2, ev->drop.source);
        u32 data = str_to_module(S_DROP, ev->drop.data);

        memcpy(m + 16, &ev->drop.windowID, 4);
        memcpy(m + 20, &ev->drop.x, 4);
        memcpy(m + 24, &ev->drop.y, 4);
        memcpy(m + 28, &src, 4);
        memcpy(m + 32, &data, 4);
        break;
    }
    default:
        break;
    }
}

/* The game's audio stream (push mode: one at a time).  A phone stops the
 * sound device when the app goes to the background and does not start it
 * again on the way back; the game's sound driver keeps time by what the
 * device plays, so a stopped device stops the game (the title froze after
 * Press Start, waiting on its sound).  The host starts it again. */
static SDL_AudioStream *g_audio;

static void app_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        fprintf(stderr, "eidos: app to the background\n");
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        fprintf(stderr, "eidos: app back in front; audio queued %d\n",
                g_audio != NULL ? SDL_GetAudioStreamQueued(g_audio) : -1);
        if (g_audio != NULL && !SDL_ResumeAudioStreamDevice(g_audio)) {
            fprintf(stderr, "eidos: audio resume failed: %s\n", SDL_GetError());
        }
        break;
    default:
        break;
    }
}

u32 w2c_env_SDL_PollEvent(struct w2c_env *e, u32 out)
{
    SDL_Event ev;

    (void)e;
    if (!SDL_PollEvent(&ev)) {
        return 0;
    }
    app_event(&ev);
    if (out != 0) {
        event_to_module(&ev, out);
    }
    return 1;
}

u32 w2c_env_SDL_WaitEventTimeout(struct w2c_env *e, u32 out, u32 ms)
{
    SDL_Event ev;

    (void)e;
    if (!SDL_WaitEventTimeout(&ev, (Sint32)ms)) {
        return 0;
    }
    app_event(&ev);
    if (out != 0) {
        event_to_module(&ev, out);
    }
    return 1;
}

u32 w2c_env_SDL_PushEvent(struct w2c_env *e, u32 in)
{
    SDL_Event ev;

    (void)e;
    memcpy(&ev, host_ptr(in, sizeof(SDL_Event)), sizeof(SDL_Event));
    return SDL_PushEvent(&ev);      /* the port pushes no string events */
}

void w2c_env_SDL_SetEventEnabled(struct w2c_env *e, u32 type, u32 on)
{
    (void)e;
    SDL_SetEventEnabled(type, on != 0);
}

/* ------------------------------------------------------------------ */
/* Audio (push mode only: no callback runs on SDL's thread)            */
/* ------------------------------------------------------------------ */

u32 w2c_env_SDL_GetAudioDeviceFormat(struct w2c_env *e, u32 dev, u32 spec, u32 frames)
{
    (void)e;
    return SDL_GetAudioDeviceFormat(dev, (SDL_AudioSpec *)M(spec), (int *)M(frames));
}

u32 w2c_env_SDL_OpenAudioDeviceStream(struct w2c_env *e, u32 dev, u32 spec, u32 cb, u32 user)
{
    (void)e;
    (void)user;
    if (cb != 0) {
        fprintf(stderr, "eidos: an audio callback cannot run in this build (push mode only)\n");
        return 0;
    }
    g_audio = SDL_OpenAudioDeviceStream(dev, (const SDL_AudioSpec *)M(spec), NULL, NULL);
    return handle_of(g_audio);
}

void w2c_env_SDL_DestroyAudioStream(struct w2c_env *e, u32 s)
{
    (void)e;
    if ((SDL_AudioStream *)handle_ptr(s) == g_audio) {
        g_audio = NULL;
    }
    SDL_DestroyAudioStream((SDL_AudioStream *)handle_ptr(s));
    handle_drop(s);
}

u32 w2c_env_SDL_ResumeAudioStreamDevice(struct w2c_env *e, u32 s)
{
    (void)e;
    return SDL_ResumeAudioStreamDevice((SDL_AudioStream *)handle_ptr(s));
}

u32 w2c_env_SDL_PutAudioStreamData(struct w2c_env *e, u32 s, u32 buf, u32 len)
{
    (void)e;
    return SDL_PutAudioStreamData((SDL_AudioStream *)handle_ptr(s), M(buf), (int)len);
}

u32 w2c_env_SDL_GetAudioStreamQueued(struct w2c_env *e, u32 s)
{
    (void)e;
    return (u32)SDL_GetAudioStreamQueued((SDL_AudioStream *)handle_ptr(s));
}

u32 w2c_env_SDL_LockAudioStream(struct w2c_env *e, u32 s)
{
    (void)e;
    return SDL_LockAudioStream((SDL_AudioStream *)handle_ptr(s));
}

u32 w2c_env_SDL_UnlockAudioStream(struct w2c_env *e, u32 s)
{
    (void)e;
    return SDL_UnlockAudioStream((SDL_AudioStream *)handle_ptr(s));
}
