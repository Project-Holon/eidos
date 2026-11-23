/*
 * host/dual.c -- the second display of a dual-screen handheld
 * (engine/platform/plat_dual.h, sdk/wasm_host.h's dual_*).
 *
 * Android: the activity shows a Presentation on the second display
 * (android/.../DualScreen.java) holding one SurfaceView, and hands its
 * Surface here (nativeDualSurface).  The game's thread makes an EGL window
 * surface over it with the game's own EGLDisplay, config and context, so
 * the presenter draws the second DS screen there with the textures and
 * programs it already has: eglMakeCurrent to the second surface, draw,
 * eglSwapBuffers, eglMakeCurrent back to SDL's surface.  SDL never sees the
 * switch: the context is the one it made, current again on its own surface
 * before the call returns.
 *
 * The second surface swaps with interval 0, so its swap never waits for a
 * vblank: the window's swap keeps the pacing.
 *
 * Fingers on the second display come in from the UI thread
 * (nativeDualTouch) and are read by the game's thread (dual_touch).
 *
 * Every other platform: no second display.
 */

#include <stdint.h>
#include <string.h>

#include "host.h"

#if defined(__ANDROID__)

#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "eidos", __VA_ARGS__)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_released = PTHREAD_COND_INITIALIZER;

/* What the UI thread hands over, under g_lock. */
static ANativeWindow *g_pending;    /* the Surface's window, a reference held */
static int g_pending_gen;           /* bumped on every new Surface or loss   */
static int g_w, g_h;                /* the Surface's size in pixels          */
static int g_touch_down, g_touch_x, g_touch_y;   /* 0..65535                 */

/* The game thread's own. */
static ANativeWindow *g_window;     /* the window g_surface is over          */
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static int g_gen = -1;
static int g_failed_gen = -2;       /* a Surface EGL refused: not retried    */

/* The saved current state while the second surface is drawn to. */
static EGLSurface g_prev_draw, g_prev_read;
static EGLContext g_prev_ctx;

static void drop_surface(void)
{
    if (g_surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_dpy, g_surface);
        g_surface = EGL_NO_SURFACE;
    }
    if (g_window != NULL) {
        ANativeWindow_release(g_window);
        g_window = NULL;
    }
}

/* The game thread: the EGL surface follows the Surface the UI thread gave. */
static void follow(void)
{
    ANativeWindow *want = NULL;
    int gen;

    pthread_mutex_lock(&g_lock);
    gen = g_pending_gen;
    if (gen != g_gen) {
        want = g_pending;
        if (want != NULL) {
            ANativeWindow_acquire(want);
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (gen == g_gen) {
        return;
    }
    drop_surface();
    pthread_mutex_lock(&g_lock);
    g_gen = gen;                        /* the old surface is let go of     */
    pthread_cond_broadcast(&g_released);
    pthread_mutex_unlock(&g_lock);
    if (want == NULL) {
        return;
    }

    {
        EGLDisplay dpy = eglGetCurrentDisplay();
        EGLContext ctx = eglGetCurrentContext();
        EGLint id = 0, n = 0;
        EGLConfig cfg;
        EGLint attrs[] = { EGL_CONFIG_ID, 0, EGL_NONE };

        if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT ||
            !eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &id)) {
            ANativeWindow_release(want);
            pthread_mutex_lock(&g_lock);
            g_gen = -1;                 /* no context yet: try next frame   */
            pthread_mutex_unlock(&g_lock);
            return;
        }
        attrs[1] = id;
        if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1) {
            LOGI("dual screen: no EGL config %d (0x%x)", id, eglGetError());
            ANativeWindow_release(want);
            g_failed_gen = gen;
            return;
        }
        g_dpy = dpy;
        g_window = want;
        g_surface = eglCreateWindowSurface(dpy, cfg, want, NULL);
        if (g_surface == EGL_NO_SURFACE) {
            LOGI("dual screen: eglCreateWindowSurface failed (0x%x)", eglGetError());
            ANativeWindow_release(want);
            g_window = NULL;
            g_failed_gen = gen;
            return;
        }
        LOGI("dual screen: drawing to the second display, %dx%d", g_w, g_h);
    }
}

u32 w2c_eidos_dual_size(struct w2c_eidos *e, u32 pw, u32 ph)
{
    int w, h, on;

    (void)e;
    pthread_mutex_lock(&g_lock);
    on = g_pending != NULL && g_pending_gen != g_failed_gen;
    w = g_w;
    h = g_h;
    pthread_mutex_unlock(&g_lock);
    if (!on) {
        w = h = 0;
    }
    if (pw != 0) {
        int32_t v = w;
        void *d = host_ptr(pw, 4);

        if (d != NULL) {
            memcpy(d, &v, 4);
        }
    }
    if (ph != 0) {
        int32_t v = h;
        void *d = host_ptr(ph, 4);

        if (d != NULL) {
            memcpy(d, &v, 4);
        }
    }
    return on && w > 0 && h > 0;
}

u32 w2c_eidos_dual_begin(struct w2c_eidos *e)
{
    (void)e;
    follow();
    if (g_surface == EGL_NO_SURFACE) {
        return 0;
    }
    g_prev_draw = eglGetCurrentSurface(EGL_DRAW);
    g_prev_read = eglGetCurrentSurface(EGL_READ);
    g_prev_ctx = eglGetCurrentContext();
    if (!eglMakeCurrent(g_dpy, g_surface, g_surface, g_prev_ctx)) {
        LOGI("dual screen: eglMakeCurrent failed (0x%x)", eglGetError());
        eglMakeCurrent(g_dpy, g_prev_draw, g_prev_read, g_prev_ctx);
        drop_surface();
        g_failed_gen = g_gen;
        return 0;
    }
    eglSwapInterval(g_dpy, 0);          /* this surface's: never wait here  */
    return 1;
}

void w2c_eidos_dual_end(struct w2c_eidos *e)
{
    (void)e;
    if (g_surface == EGL_NO_SURFACE) {
        return;
    }
    eglSwapBuffers(g_dpy, g_surface);
    eglMakeCurrent(g_dpy, g_prev_draw, g_prev_read, g_prev_ctx);
}

u32 w2c_eidos_dual_touch(struct w2c_eidos *e, u32 px, u32 py)
{
    int32_t x, y;
    int down;
    void *d;

    (void)e;
    pthread_mutex_lock(&g_lock);
    down = g_touch_down;
    x = g_touch_x;
    y = g_touch_y;
    pthread_mutex_unlock(&g_lock);
    if (px != 0 && (d = host_ptr(px, 4)) != NULL) {
        memcpy(d, &x, 4);
    }
    if (py != 0 && (d = host_ptr(py, 4)) != NULL) {
        memcpy(d, &y, 4);
    }
    return down;
}

/* ---- from DualScreen.java (the UI thread) ---- */

JNIEXPORT void JNICALL
Java_org_projectholon_eidos_DualScreen_nativeDualSurface(JNIEnv *env, jclass cls,
                                                         jobject surface, jint w, jint h)
{
    ANativeWindow *win = surface != NULL ? ANativeWindow_fromSurface(env, surface) : NULL;

    (void)cls;
    pthread_mutex_lock(&g_lock);
    if (g_pending != NULL) {
        ANativeWindow_release(g_pending);
    }
    g_pending = win;
    g_w = w;
    g_h = h;
    g_pending_gen++;
    g_touch_down = 0;
    pthread_mutex_unlock(&g_lock);
}

/*
 * The Surface is going away: forget it, and wait a little for the game's
 * thread to let go of its EGL surface over it (it does at its next frame).
 * A paused game does not draw, so the wait is bounded; the window reference
 * the game thread holds keeps it safe either way.
 */
JNIEXPORT void JNICALL
Java_org_projectholon_eidos_DualScreen_nativeDualSurfaceGone(JNIEnv *env, jclass cls)
{
    struct timespec until;
    int gen;

    (void)env;
    (void)cls;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 250 * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&g_lock);
    if (g_pending != NULL) {
        ANativeWindow_release(g_pending);
        g_pending = NULL;
    }
    gen = ++g_pending_gen;
    g_touch_down = 0;
    while (g_gen != gen) {
        if (pthread_cond_timedwait(&g_released, &g_lock, &until) != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* down: 1 while a finger is on the display; x, y in 0..65535. */
JNIEXPORT void JNICALL
Java_org_projectholon_eidos_DualScreen_nativeDualTouch(JNIEnv *env, jclass cls,
                                                       jint down, jint x, jint y)
{
    (void)env;
    (void)cls;
    pthread_mutex_lock(&g_lock);
    g_touch_down = down != 0;
    g_touch_x = x < 0 ? 0 : x > 65535 ? 65535 : x;
    g_touch_y = y < 0 ? 0 : y > 65535 ? 65535 : y;
    pthread_mutex_unlock(&g_lock);
}

#else /* not Android: no second display */

u32 w2c_eidos_dual_size(struct w2c_eidos *e, u32 pw, u32 ph)
{
    (void)e;
    (void)pw;
    (void)ph;
    return 0;
}

u32 w2c_eidos_dual_begin(struct w2c_eidos *e)
{
    (void)e;
    return 0;
}

void w2c_eidos_dual_end(struct w2c_eidos *e)
{
    (void)e;
}

u32 w2c_eidos_dual_touch(struct w2c_eidos *e, u32 px, u32 py)
{
    (void)e;
    (void)px;
    (void)py;
    return 0;
}

#endif
