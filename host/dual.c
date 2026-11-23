/*
 * host/dual.c -- the second display of a dual-screen handheld
 * (engine/platform/plat_dual.h, sdk/wasm_host.h's dual_*).
 *
 * Android: the activity shows a Presentation on the second display
 * (android/.../DualScreen.java) holding one SurfaceView in the DS screen's
 * shape, and hands its Surface here (nativeDualSurface).  The presenter
 * draws the second display's picture into a framebuffer of its own with
 * the game's context, on the window's surface as always (dual_post); here
 * it is read into one of two pixel buffers, the other one (last frame's,
 * long done) is mapped, and its rows are copied, bottom row first, into
 * the Surface's next buffer (ANativeWindow_lock / unlockAndPost).  No
 * second EGL surface: GL never switches surfaces, and the second display
 * shows its picture a frame late.
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
#include <GLES3/gl3.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "eidos", __VA_ARGS__)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_released = PTHREAD_COND_INITIALIZER;

/* What the UI thread hands over, under g_lock. */
static ANativeWindow *g_pending;    /* the Surface's window, a reference held */
static int g_pending_gen;           /* bumped on every new Surface or loss   */
static int g_w, g_h;                /* the Surface's size in pixels          */
static int g_touch_down, g_touch_x, g_touch_y;   /* 0..65535                 */

/* The game thread's own. */
static ANativeWindow *g_window;     /* the window the picture goes to        */
static int g_gen = -1;
static int g_geom_w, g_geom_h;      /* the window's buffers' size, set       */
static GLuint g_pbo[2];             /* this frame's readback, last frame's   */
static int g_pbo_w, g_pbo_h, g_pbo_i, g_pbo_full;

/* The game thread: the window follows the Surface the UI thread gave. */
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
    if (g_window != NULL) {
        ANativeWindow_release(g_window);
    }
    g_window = want;
    g_geom_w = g_geom_h = 0;
    g_pbo_full = 0;                     /* last frame's picture was for the old one */
    pthread_mutex_lock(&g_lock);
    g_gen = gen;                        /* the old window is let go of      */
    pthread_cond_broadcast(&g_released);
    pthread_mutex_unlock(&g_lock);
    if (want != NULL) {
        LOGI("dual screen: drawing to the second display, %dx%d", g_w, g_h);
    }
}

static void put_i32(u32 addr, int32_t v)
{
    void *d = addr != 0 ? host_ptr(addr, 4) : NULL;

    if (d != NULL) {
        memcpy(d, &v, 4);
    }
}

u32 w2c_eidos_dual_size(struct w2c_eidos *e, u32 pw, u32 ph)
{
    int w, h, on;

    (void)e;
    pthread_mutex_lock(&g_lock);
    on = g_pending != NULL;
    w = g_w;
    h = g_h;
    pthread_mutex_unlock(&g_lock);
    if (!on) {
        w = h = 0;
    }
    put_i32(pw, w);
    put_i32(ph, h);
    return on && w > 0 && h > 0;
}

/* The mapped picture into the window: GL's rows run bottom-up. */
static void copy_out(const unsigned char *px, int w, int h)
{
    ANativeWindow_Buffer buf;
    int y, rows, cols;

    if (ANativeWindow_lock(g_window, &buf, NULL) != 0) {
        return;
    }
    rows = buf.height < h ? buf.height : h;
    cols = buf.width < w ? buf.width : w;
    for (y = 0; y < rows; y++) {
        memcpy((unsigned char *)buf.bits + (size_t)y * (size_t)buf.stride * 4u,
               px + (size_t)(h - 1 - y) * (size_t)w * 4u, (size_t)cols * 4u);
    }
    ANativeWindow_unlockAndPost(g_window);
}

void w2c_eidos_dual_post(struct w2c_eidos *e, u32 fbo, u32 uw, u32 uh)
{
    int w = (int)uw, h = (int)uh, i;
    GLint read_fbo = 0;

    (void)e;
    follow();
    if (g_window == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (g_geom_w != w || g_geom_h != h) {
        ANativeWindow_setBuffersGeometry(g_window, w, h, WINDOW_FORMAT_RGBA_8888);
        g_geom_w = w;
        g_geom_h = h;
    }
    if (g_pbo_w != w || g_pbo_h != h) {
        if (g_pbo[0] == 0) {
            glGenBuffers(2, g_pbo);
        }
        for (i = 0; i < 2; i++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, NULL, GL_STREAM_READ);
        }
        g_pbo_w = w;
        g_pbo_h = h;
        g_pbo_full = 0;
    }
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[g_pbo_i]);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
    if (g_pbo_full) {
        /* last frame's: its readback is done by now */
        const void *px;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[g_pbo_i ^ 1]);
        px = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)w * h * 4, GL_MAP_READ_BIT);
        if (px != NULL) {
            copy_out((const unsigned char *)px, w, h);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    g_pbo_i ^= 1;
    g_pbo_full = 1;
}

u32 w2c_eidos_dual_touch(struct w2c_eidos *e, u32 px, u32 py)
{
    int32_t x, y;
    int down;

    (void)e;
    pthread_mutex_lock(&g_lock);
    down = g_touch_down;
    x = g_touch_x;
    y = g_touch_y;
    pthread_mutex_unlock(&g_lock);
    put_i32(px, x);
    put_i32(py, y);
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
 * thread to let go of its window (it does at its next frame).
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

void w2c_eidos_dual_post(struct w2c_eidos *e, u32 fbo, u32 w, u32 h)
{
    (void)e;
    (void)fbo;
    (void)w;
    (void)h;
}

u32 w2c_eidos_dual_touch(struct w2c_eidos *e, u32 px, u32 py)
{
    (void)e;
    (void)px;
    (void)py;
    return 0;
}

#endif
