/*
 * tests/test_settings.c -- the settings backend, under SDL's dummy drivers:
 * screen geometry and the touch mapping for every layout x scale (and the
 * fullscreen fits), binding conflicts and capture, the settings.ini round
 * trip, command-line overrides and the volume gain.
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

#include "install.h"
#include "platform.h"
#include "port_test.h"
#include "settings.h"

#define INI "test_settings_work.ini"

static void check_fill(const EidosScreenPlacement *pl, int ow, int oh,
                       const EidosViewport *vp)
{
    /* Fills one dimension exactly, the other is centred; aspect kept to a
     * pixel. */
    long d = (long)vp->content.w * pl->height -
             (long)vp->content.h * pl->width;
    int ok = (vp->content.w == ow || vp->content.h == oh) &&
             vp->content.w <= ow && vp->content.h <= oh &&
             vp->content.x == (ow - vp->content.w) / 2 &&
             vp->content.y == (oh - vp->content.h) / 2 &&
             d < pl->height + pl->width && -d < pl->height + pl->width;

    BW_CHECK_EQ("viewport fills", ok, 1);
}

static void test_geometry(void)
{
    static const int sizes[][2] = { { 768, 1152 }, { 1920, 1080 },
                                    { 1000, 1000 }, { 301, 777 },
                                    { 2560, 1440 }, { 640, 360 },
                                    { 123, 456 } };
    int layout, swap, gap, si, i;
    EidosScreenPlacement pl;
    EidosViewport vp;
    int x, y;

    for (layout = 0; layout < 2; layout++) {
        for (swap = 0; swap < 2; swap++) {
            for (gap = 0; gap <= 8; gap += 8) {
                for (si = 0; si < (int)(sizeof sizes / sizeof sizes[0]);
                     si++) {
                    int ow = sizes[si][0], oh = sizes[si][1], ok;

                    eidos_layout_place(layout, swap, gap, &pl);
                    eidos_display_viewport(&pl, ow, oh, &vp);
                    check_fill(&pl, ow, oh, &vp);
                    /* The bottom screen's corners map to its corners. */
                    ok = eidos_viewport_to_touch(&pl, &vp, vp.screen[1].x,
                                                 vp.screen[1].y, &x, &y) &&
                         x <= 2 && y <= 2;
                    ok &= eidos_viewport_to_touch(
                              &pl, &vp,
                              vp.screen[1].x + vp.screen[1].w - 1,
                              vp.screen[1].y + vp.screen[1].h - 1, &x, &y) &&
                          x >= 250 && x <= 255 && y >= 185 && y <= 191;
                    ok &= !eidos_viewport_to_touch(
                              &pl, &vp, vp.screen[0].x + vp.screen[0].w / 2,
                              vp.screen[0].y + vp.screen[0].h / 2, &x, &y);
                    BW_CHECK_EQ("touch corners", ok, 1);
                }
            }
        }
    }

    /* 3x the layout: nothing is letterboxed. */
    eidos_layout_place(EIDOS_LAYOUT_VERTICAL, 0, 0, &pl);
    eidos_display_viewport(&pl, 768, 1152, &vp);
    BW_CHECK_EQ("3x fills", vp.content.x == 0 && vp.content.y == 0 &&
                vp.screen[1].y == 576 && vp.screen[0].w == 768, 1);
    /* 1920x1080 vertical: height-limited 720x1080, no integer rule. */
    eidos_display_viewport(&pl, 1920, 1080, &vp);
    BW_CHECK_EQ("fs w", vp.content.w, 720);
    BW_CHECK_EQ("fs h", vp.content.h, 1080);
    BW_CHECK_EQ("fs x", vp.content.x, 600);
    BW_CHECK_EQ("fs bottom y", vp.screen[1].y, 540);
    BW_CHECK_EQ("fs touch start",
                eidos_viewport_to_touch(&pl, &vp, 600, 540, &x, &y) &&
                x == 0 && y == 0, 1);
    BW_CHECK_EQ("fs touch end",
                eidos_viewport_to_touch(&pl, &vp, 600 + 719, 1079, &x, &y) &&
                x == 255 && y == 191, 1);
    BW_CHECK_EQ("fs touch letterbox",
                eidos_viewport_to_touch(&pl, &vp, 599, 800, &x, &y), 0);
    /* Horizontal, swapped, 1920x1080: width-limited 1920x720, bottom left. */
    eidos_layout_place(EIDOS_LAYOUT_HORIZONTAL, 1, 0, &pl);
    eidos_display_viewport(&pl, 1920, 1080, &vp);
    BW_CHECK_EQ("h content", vp.content.w == 1920 && vp.content.h == 720 &&
                vp.content.y == 180, 1);
    BW_CHECK_EQ("h swapped bottom x", vp.screen[1].x, 0);
    BW_CHECK_EQ("h swapped top x", vp.screen[0].x, 960);
    BW_CHECK_EQ("h touch",
                eidos_viewport_to_touch(&pl, &vp, 0, 180, &x, &y) &&
                x == 0 && y == 0, 1);
    /* Smaller than the layout still fills. */
    eidos_display_viewport(&pl, 256, 96, &vp);
    BW_CHECK_EQ("tiny w", vp.content.w, 192 + 64);
    BW_CHECK_EQ("tiny h", vp.content.h, 96);
    eidos_display_viewport(&pl, 0, 0, &vp);
    BW_CHECK_EQ("empty output", vp.content.w, 0);

    /* The Fit multiple: smallest N covering the destination rectangles. */
    eidos_layout_place(EIDOS_LAYOUT_VERTICAL, 0, 0, &pl);
    {
        static const int fw[][3] = { { 256, 384, 1 }, { 258, 387, 2 },
                                     { 512, 768, 2 }, { 768, 1152, 2 },
                                     { 760, 1152, 2 }, { 1920, 1080, 2 },
                                     { 3840, 2160, 2 }, { 6000, 6000, 2 },
                                     { 100, 100, 1 } };

        for (i = 0; i < (int)(sizeof fw / sizeof fw[0]); i++) {
            eidos_display_viewport(&pl, fw[i][0], fw[i][1], &vp);
            BW_CHECK_EQ("fit N", eidos_resolution_fit(&vp, 6), fw[i][2]);
        }
        eidos_display_viewport(&pl, 3840, 2160, &vp);
        BW_CHECK_EQ("fit N clamps to max", eidos_resolution_fit(&vp, 2), 2);
        BW_CHECK_EQ("fit N capped at 2", eidos_resolution_fit(&vp, 6), 2);
        BW_CHECK_EQ("fit N: max 1", eidos_resolution_fit(&vp, 0), 1);
        BW_CHECK_EQ("fit N with a larger cap", eidos_resolution_fit_cap(&vp, 6, 6), 6);
    }

    /* A whole-number pixel scale: exact multiples, centred. */
    eidos_display_viewport_n(&pl, 1920, 1080, 2, &vp);
    BW_CHECK_EQ("2x w", vp.content.w, 512);
    BW_CHECK_EQ("2x h", vp.content.h, 768);
    BW_CHECK_EQ("2x centred x", vp.content.x, (1920 - 512) / 2);
    BW_CHECK_EQ("2x bottom screen y", vp.screen[1].y, (1080 - 768) / 2 + 384);
    BW_CHECK_EQ("2x screen h", vp.screen[1].h, 384);
    eidos_display_viewport_n(&pl, 1920, 1080, 9, &vp);
    BW_CHECK_EQ("too large: the largest that fits", vp.content.h, 384 * 2);
    eidos_display_viewport_n(&pl, 1920, 1080, 0, &vp);
    BW_CHECK_EQ("auto fills", vp.content.h, 1080);
    eidos_display_viewport_n(&pl, 200, 300, 3, &vp);
    BW_CHECK_EQ("below 1x fills", vp.content.w, 200);

    /* The first window: the largest multiple whose height fits 85%. */
    eidos_first_window_size(&pl, 1920, 1040, 85, &x, &y);
    BW_CHECK_EQ("1080p: 2x stacked", y, 768);
    eidos_first_window_size(&pl, 2560, 1400, 85, &x, &y);
    BW_CHECK_EQ("1440p: 3x stacked", y, 1152);
    eidos_layout_place(EIDOS_LAYOUT_SIDE_BY_SIDE, 0, 0, &pl);
    eidos_first_window_size(&pl, 1920, 1040, 85, &x, &y);
    BW_CHECK_EQ("1080p side by side: 3x", x, 1536);
    eidos_first_window_size(&pl, 0, 0, 85, &x, &y);
    BW_CHECK_EQ("unknown desktop: 1x", x, 512);
}

static void test_reshape(void)
{
    EidosScreenPlacement pl;
    int w, h;

    eidos_layout_place(EIDOS_LAYOUT_HORIZONTAL, 0, 0, &pl);
    eidos_window_reshape(256, 384, &pl, 0, 0, &w, &h);
    BW_CHECK_EQ("tall -> wide, same area", w == 512 && h == 192, 1);
    eidos_window_reshape(768, 1152, &pl, 0, 0, &w, &h);
    BW_CHECK_EQ("3x tall -> 3x wide", w == 1536 && h == 576, 1);
    eidos_layout_place(EIDOS_LAYOUT_VERTICAL, 0, 0, &pl);
    eidos_window_reshape(1536, 576, &pl, 0, 0, &w, &h);
    BW_CHECK_EQ("wide -> tall", w == 768 && h == 1152, 1);
    /* Clamped to the work area, aspect kept. */
    eidos_window_reshape(1536, 576, &pl, 800, 600, &w, &h);
    BW_CHECK_EQ("clamp height", h == 600 && w == 400, 1);
    eidos_layout_place(EIDOS_LAYOUT_HORIZONTAL, 0, 0, &pl);
    eidos_window_reshape(768, 1152, &pl, 1000, 2000, &w, &h);
    BW_CHECK_EQ("clamp width", w == 1000 && h == 375, 1);
    eidos_window_reshape(0, 0, &pl, 0, 0, &w, &h);
    BW_CHECK_EQ("degenerate stays positive", w >= 1 && h >= 1, 1);
}

/* The window takes the new arrangement's shape; fullscreen never leaves a
 * real (mode-switching) fullscreen flag.  Under the dummy driver the window
 * calls still run, so the size is observable. */
static void test_window_follows_layout(EidosPlatform *p)
{
    int w = 0, h = 0;
    SDL_Window *win = SDL_GetWindowFromID(1);

    eidos_platform_set_layout(p, EIDOS_LAYOUT_STACKED, 0, 0);
    eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 400, 600);
    eidos_platform_set_layout(p, EIDOS_LAYOUT_SIDE_BY_SIDE, 0, 0);
    if (win != NULL) {
        SDL_GetWindowSize(win, &w, &h);
        BW_CHECK_EQ("window turns wide", w > h, 1);
        BW_CHECK_EQ("settings width follows",
                    eidos_settings_get(EIDOS_OPT_DISPLAY_WIDTH), w);
        BW_CHECK_EQ("settings height follows",
                    eidos_settings_get(EIDOS_OPT_DISPLAY_HEIGHT), h);
        eidos_platform_set_layout(p, EIDOS_LAYOUT_STACKED, 0, 0);
        SDL_GetWindowSize(win, &w, &h);
        BW_CHECK_EQ("window turns tall", h > w, 1);
        eidos_platform_set_display(p, EIDOS_DISPLAY_FULLSCREEN, 0, 0);
        BW_CHECK_EQ("no real fullscreen flag",
                    SDL_GetWindowFullscreenMode(win) != NULL, 0);
        eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 0, 0);
    }
}

static void test_platform_modes(EidosPlatform *p)
{
    int layout, swap, n, x, y;
    EidosViewport vp;
    EidosScreenPlacement pl;
    static const int wins[][2] = { { 768, 1152 }, { 900, 700 }, { 333, 555 } };
    static u8 buf[2][256 * 6 * 192 * 6 * 4];

    for (layout = 0; layout < 2; layout++) {
        for (swap = 0; swap < 2; swap++) {
            int wi;

            BW_CHECK_EQ("set_layout",
                        eidos_platform_set_layout(p, layout, swap, 4), 0);
            eidos_layout_place(layout, swap, 4, &pl);
            for (wi = 0; wi < 3; wi++) {
                int ww = wins[wi][0], wh = wins[wi][1], ok;

                BW_CHECK_EQ("set_display",
                            eidos_platform_set_display(
                                p, EIDOS_DISPLAY_WINDOWED, ww, wh), 0);
                eidos_display_viewport(&pl, ww, wh, &vp);
                /* The platform mapping agrees with the pure one. */
                ok = eidos_platform_window_to_touch(
                         p, ww, wh, vp.screen[1].x + vp.screen[1].w / 2,
                         vp.screen[1].y + vp.screen[1].h / 2, &x, &y) &&
                     x >= 120 && x <= 135 && y >= 90 && y <= 101;
                BW_CHECK_EQ("platform touch", ok, 1);
                ok = !eidos_platform_window_to_touch(
                         p, ww, wh, vp.screen[0].x + 1, vp.screen[0].y + 1,
                         &x, &y);
                BW_CHECK_EQ("platform touch top", ok, 1);
            }
        }
    }
    {
        u32 w = 0, h = 0;

        eidos_platform_set_layout(p, EIDOS_LAYOUT_HORIZONTAL, 0, 0);
        eidos_platform_shot_size(p, &w, &h);
        BW_CHECK_EQ("shot size follows layout", w == 512 && h == 192, 1);
    }
    /* Frames of every multiple are accepted; the screenshot is at that
     * multiple, and the textures follow it. */
    eidos_platform_set_layout(p, EIDOS_LAYOUT_STACKED, 0, 0);
    for (n = 1; n <= 6; n++) {
        u32 w = 0, h = 0;
        size_t i, px = (size_t)256 * n * 192 * n;

        for (i = 0; i < px * 4; i++) {
            buf[0][i] = (u8)(n * 10 + (i & 3));
            buf[1][i] = (u8)(n * 20 + (i & 3));
        }
        eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 700 + n * 31,
                                   900);
        eidos_platform_present_n(p, buf[0], buf[1], n);
        eidos_platform_shot_size(p, &w, &h);
        BW_CHECK_EQ("frame scale", eidos_platform_frame_scale(p), n);
        BW_CHECK_EQ("shot at N", w == 256u * n && h == 384u * n, 1);
        BW_CHECK_EQ("shot written",
                    eidos_platform_screenshot(p, "test_settings_shot.bmp"), 0);
    }
    remove("test_settings_shot.bmp");
    /* The same N through a stretched, a shrunk and a non-integer window. */
    eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 257, 385);
    eidos_platform_present_n(p, NULL, NULL, 2);
    eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 1500, 2300);
    eidos_platform_present_n(p, NULL, NULL, 2);
    eidos_platform_present(p, buf[0], buf[1]);
    BW_CHECK_EQ("back to 1x", eidos_platform_frame_scale(p), 1);

    /* Borderless and fullscreen: whether the dummy driver grants them or
     * not, the platform's mode and the answer agree, and the picture is
     * still placed on the output. */
    {
        int rc = eidos_platform_set_display(p, EIDOS_DISPLAY_BORDERLESS, 0, 0);
        BW_CHECK_EQ("borderless agrees",
                    rc == 0 ? eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_BORDERLESS
                            : eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_WINDOWED, 1);
        rc = eidos_platform_set_display(p, EIDOS_DISPLAY_FULLSCREEN, 0, 0);
        BW_CHECK_EQ("fullscreen agrees",
                    rc == 0 ? eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_FULLSCREEN
                            : eidos_platform_display_mode(p) !=
                                  EIDOS_DISPLAY_FULLSCREEN, 1);
        eidos_platform_viewport(p, &vp);
        BW_CHECK_EQ("fs viewport placed", vp.content.w > 0, 1);
        eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 0, 0);
        BW_CHECK_EQ("back to windowed",
                    eidos_platform_display_mode(p), EIDOS_DISPLAY_WINDOWED);
    }
}

static void push_key(int sc, int down)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.down = down != 0;
    ev.key.scancode = (SDL_Scancode)sc;
    SDL_PushEvent(&ev);
}

static void test_bindings(EidosPlatform *p)
{
    char buf[64];
    int kind = 0, index = 0, b;
    EidosCaptured cap;
    SDL_Event ev;

    eidos_settings_reset_bindings();
    /* Defaults: every button has a key and a pad control. */
    for (b = 0; b < eidos_settings_button_count(); b++) {
        BW_CHECK_EQ("default key", eidos_settings_get_key(b, 0) != 0, 1);
        eidos_settings_get_pad(b, 0, &kind, &index);
        BW_CHECK_EQ("default pad", kind >= 0, 1);
    }
    /* Select has two keys by default. */
    BW_CHECK_EQ("select second key", eidos_settings_get_key(7, 1) != 0, 1);

    /* Conflict: binding Z (B) to A moves it. */
    BW_CHECK_EQ("B has Z", eidos_settings_get_key(1, 0), SDL_SCANCODE_Z);
    BW_CHECK_EQ("bind Z to A", eidos_settings_set_key(0, 1, SDL_SCANCODE_Z), 0);
    BW_CHECK_EQ("A slot 1 is Z", eidos_settings_get_key(0, 1), SDL_SCANCODE_Z);
    BW_CHECK_EQ("B lost Z", eidos_settings_get_key(1, 0), 0);
    /* Slot -1 takes the first empty slot (B's slot 0 is empty now). */
    BW_CHECK_EQ("bind K to B", eidos_settings_set_key(1, -1, SDL_SCANCODE_K), 0);
    BW_CHECK_EQ("K in B slot 0", eidos_settings_get_key(1, 0), SDL_SCANCODE_K);
    /* Taking another button's key hands it the key the slot had: a swap. */
    {
        int a0 = eidos_settings_get_key(0, 0);

        BW_CHECK_EQ("A takes B's K", eidos_settings_set_key(0, 0, SDL_SCANCODE_K), 0);
        BW_CHECK_EQ("A slot 0 is K", eidos_settings_get_key(0, 0), SDL_SCANCODE_K);
        BW_CHECK_EQ("B gets A's old key", eidos_settings_get_key(1, 0), a0);
    }
    BW_CHECK_EQ("clear", eidos_settings_set_key(1, 0, 0), 0);
    BW_CHECK_EQ("cleared", eidos_settings_get_key(1, 0), 0);
    BW_CHECK_EQ("escape reserved",
                eidos_settings_set_key(1, 0, SDL_SCANCODE_ESCAPE), -2);
    BW_CHECK_EQ("f12 reserved",
                eidos_settings_set_key(1, 0, SDL_SCANCODE_F12), -2);
    BW_CHECK_EQ("bad button", eidos_settings_set_key(99, 0, SDL_SCANCODE_K), -1);

    /* Pad conflict: south button moves from B to A. */
    BW_CHECK_EQ("pad bind",
                eidos_settings_set_pad(0, -1, EIDOS_PADBIND_BUTTON,
                                       SDL_GAMEPAD_BUTTON_SOUTH), 0);
    eidos_settings_get_pad(1, 0, &kind, &index);
    BW_CHECK_EQ("B lost pad A", kind == -1 ||
                !(kind == EIDOS_PADBIND_BUTTON &&
                  index == SDL_GAMEPAD_BUTTON_SOUTH), 1);

    /* Names are ASCII and non-empty. */
    BW_CHECK_EQ("name Z", strcmp(eidos_scancode_name(SDL_SCANCODE_Z, buf,
                                                     sizeof buf), "Z"), 0);
    BW_CHECK_EQ("name pad", strlen(eidos_pad_control_name(
        EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, buf,
        sizeof buf)) > 0, 1);
    BW_CHECK_EQ("name axis", strcmp(eidos_pad_control_name(
        EIDOS_PADBIND_AXIS_NEG, SDL_GAMEPAD_AXIS_LEFTY, buf, sizeof buf),
        "Left Stick Up"), 0);

    /* Capture a key, then cancel with Escape (which must not quit). */
    eidos_settings_capture_begin();
    BW_CHECK_EQ("pending", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PENDING);
    push_key(SDL_SCANCODE_J, 1);
    BW_CHECK_EQ("pump alive", eidos_platform_pump(p), 1);
    BW_CHECK_EQ("captured key", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_KEY);
    BW_CHECK_EQ("captured J", cap.scancode, SDL_SCANCODE_J);
    BW_CHECK_EQ("consumed", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_NONE);
    eidos_settings_capture_begin();
    push_key(SDL_SCANCODE_ESCAPE, 1);
    BW_CHECK_EQ("escape cancels, no quit", eidos_platform_pump(p), 1);
    BW_CHECK_EQ("cancelled", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_CANCELLED);
    /* A pad button and an axis. */
    eidos_settings_capture_begin();
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    ev.gbutton.down = true;
    ev.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    SDL_PushEvent(&ev);
    eidos_platform_pump(p);
    BW_CHECK_EQ("captured pad", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PAD);
    BW_CHECK_EQ("pad index", cap.pad_index, SDL_GAMEPAD_BUTTON_WEST);
    eidos_settings_capture_begin();
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    ev.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHTX;
    ev.gaxis.value = -30000;
    SDL_PushEvent(&ev);
    eidos_platform_pump(p);
    BW_CHECK_EQ("captured axis", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PAD);
    BW_CHECK_EQ("axis neg", cap.pad_kind, EIDOS_PADBIND_AXIS_NEG);
    /* Not armed: Escape does not quit either (a stray press closed the game). */
    push_key(SDL_SCANCODE_ESCAPE, 1);
    BW_CHECK_EQ("escape does not quit", eidos_platform_pump(p), 1);
}

static void snapshot(int *o, int *k, int *pk, int *pi)
{
    int i, b, s, n = 0;

    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        o[i] = eidos_settings_get(i);
    }
    for (b = 0; b < eidos_settings_button_count(); b++) {
        for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
            k[n] = eidos_settings_get_key(b, s);
            eidos_settings_get_pad(b, s, &pk[n], &pi[n]);
            n++;
        }
    }
}

static void test_file(void)
{
    enum { N = 12 * EIDOS_BIND_SLOTS };
    int o1[EIDOS_OPT_COUNT], o2[EIDOS_OPT_COUNT];
    int k1[N], k2[N], pk1[N], pk2[N], pi1[N], pi2[N];
    FILE *fp;
    char *argv_a[] = { "x", "--fullscreen", "--resolution", "3", "--volume", "40",
                       "--layout", "horizontal", "--swap-screens" };
    char *argv_bad[] = { "x", "--volume", "loud" };
    int i;

    eidos_settings_set_path(INI);
    remove(INI);
    BW_CHECK_EQ("no file -> defaults", eidos_settings_load(), 1);
    BW_CHECK_EQ("default 3D resolution is auto",
                eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("default pixel scale is auto",
                eidos_settings_get(EIDOS_OPT_DISPLAY_PIXEL_SCALE), 0);
    BW_CHECK_EQ("default volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 50);
    BW_CHECK_EQ("default gap", eidos_settings_get(EIDOS_OPT_LAYOUT_GAP), 0);
    BW_CHECK_EQ("clean at start", eidos_settings_dirty(), 0);

    eidos_settings_set(EIDOS_OPT_DISPLAY_MODE, EIDOS_DISPLAY_BORDERLESS);
    eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 4);
    eidos_settings_set(EIDOS_OPT_DISPLAY_WIDTH, 900);
    eidos_settings_set(EIDOS_OPT_DISPLAY_HEIGHT, 700);
    eidos_settings_set(EIDOS_OPT_LAYOUT_ARRANGEMENT, EIDOS_LAYOUT_HORIZONTAL);
    eidos_settings_set(EIDOS_OPT_LAYOUT_SWAP, 1);
    eidos_settings_set(EIDOS_OPT_LAYOUT_GAP, 12);
    eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 35);
    eidos_settings_set_key(0, -1, SDL_SCANCODE_J);
    eidos_settings_set_pad(2, 0, EIDOS_PADBIND_AXIS_POS, SDL_GAMEPAD_AXIS_RIGHTY);
    BW_CHECK_EQ("dirty", eidos_settings_dirty(), 1);
    BW_CHECK_EQ("commit", eidos_settings_commit(), 0);
    BW_CHECK_EQ("clean after commit", eidos_settings_dirty(), 0);
    BW_CHECK_EQ("no tmp left", bw_path_kind_utf8(INI ".tmp"), BW_PATH_NONE);
    snapshot(o1, k1, pk1, pi1);

    eidos_settings_defaults();
    BW_CHECK_EQ("defaults reset", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 50);
    BW_CHECK_EQ("defaults dirty", eidos_settings_dirty(), 1);
    eidos_settings_revert();
    snapshot(o2, k2, pk2, pi2);
    BW_CHECK_EQ("revert restores options", memcmp(o1, o2, sizeof o1), 0);
    BW_CHECK_EQ("revert restores keys", memcmp(k1, k2, sizeof k1), 0);

    BW_CHECK_EQ("load", eidos_settings_load(), 0);
    snapshot(o2, k2, pk2, pi2);
    BW_CHECK_EQ("round trip options", memcmp(o1, o2, sizeof o1), 0);
    BW_CHECK_EQ("round trip keys", memcmp(k1, k2, sizeof k1), 0);
    BW_CHECK_EQ("round trip pad kind", memcmp(pk1, pk2, sizeof pk1), 0);
    BW_CHECK_EQ("round trip pad index", memcmp(pi1, pi2, sizeof pi1), 0);
    /* a pad bound by the player stays theirs (#28): the flag is saved, and
     * only a reset of the bindings clears it */
    BW_CHECK_EQ("pad custom after a pad binding, reloaded", eidos_settings_pad_custom(), 1);
    eidos_settings_reset_bindings();
    BW_CHECK_EQ("reset bindings clears pad custom", eidos_settings_pad_custom(), 0);
    BW_CHECK_EQ("reload", eidos_settings_load(), 0);
    BW_CHECK_EQ("pad custom back from the file", eidos_settings_pad_custom(), 1);

    /* Hand edits: unknown keys and bad values are ignored with a warning. */
    fp = fopen(INI, "wb");
    if (fp == NULL) {
        BW_CHECK_EQ("write ini", 0, 1);
        return;
    }
    fputs("# hi\n[Display]\nmode = Fullscreen\nresolution_3d = 99\n"
          "resolution = 4\nbogus = 1\n"
          "[sound]\nvolume=70\n[keys]\na = Space\nb = Nonsense Key\n"
          "[mystery]\nx=1\n[pad]\nup = dpup | lefty-\n", fp);
    fclose(fp);
    BW_CHECK_EQ("load edited", eidos_settings_load(), 0);
    BW_CHECK_EQ("mode", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);
    BW_CHECK_EQ("bad resolution keeps default (and the old key is dropped)",
                eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 70);
    BW_CHECK_EQ("a is Space", eidos_settings_get_key(0, 0), SDL_SCANCODE_SPACE);
    BW_CHECK_EQ("b emptied", eidos_settings_get_key(1, 0), 0);
    BW_CHECK_EQ("untouched button keeps default",
                eidos_settings_get_key(2, 0), SDL_SCANCODE_S);

    /* Command-line overrides apply but are not saved. */
    eidos_settings_load();
    for (i = 1; i < 9; i++) {
        int rc = eidos_settings_parse_arg(9, argv_a, &i);
        BW_CHECK_EQ("arg consumed", rc, 1);
    }
    BW_CHECK_EQ("override mode", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);
    BW_CHECK_EQ("override resolution",
                eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 3);
    BW_CHECK_EQ("override volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 40);
    BW_CHECK_EQ("override layout",
                eidos_settings_get(EIDOS_OPT_LAYOUT_ARRANGEMENT),
                EIDOS_LAYOUT_HORIZONTAL);
    BW_CHECK_EQ("override swap", eidos_settings_get(EIDOS_OPT_LAYOUT_SWAP), 1);
    BW_CHECK_EQ("override is not dirty", eidos_settings_dirty(), 0);
    eidos_settings_set(EIDOS_OPT_LAYOUT_GAP, 6);
    BW_CHECK_EQ("commit with overrides", eidos_settings_commit(), 0);
    eidos_settings_load();
    BW_CHECK_EQ("override not saved: mode",
                eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);      /* the file's, not the flag's */
    BW_CHECK_EQ("override not saved: volume",
                eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 70);
    BW_CHECK_EQ("override not saved: resolution",
                eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("real change saved", eidos_settings_get(EIDOS_OPT_LAYOUT_GAP), 6);
    i = 1;
    BW_CHECK_EQ("bad value", eidos_settings_parse_arg(3, argv_bad, &i), -1);
    i = 1;
    BW_CHECK_EQ("not ours", eidos_settings_parse_arg(2, (char *[]){ "x", "--frames" }, &i), 0);

    {
        char buf[32];
        eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 55);
        BW_CHECK_EQ("text", strcmp(eidos_settings_value_text(
            EIDOS_OPT_SOUND_VOLUME, buf, sizeof buf), "55%"), 0);
        BW_CHECK_EQ("clamped", (eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 500),
                                eidos_settings_get(EIDOS_OPT_SOUND_VOLUME)), 100);
    }
    remove(INI);
}

static void test_gain(void)
{
    BW_CHECK_EQ("gain 100", eidos_audio_gain(12345, 100), 12345);
    BW_CHECK_EQ("gain 50", eidos_audio_gain(1000, 50), 500);
    BW_CHECK_EQ("gain 50 neg", eidos_audio_gain(-1000, 50), -500);
    BW_CHECK_EQ("gain 0", eidos_audio_gain(-32768, 0), 0);
    BW_CHECK_EQ("saturate hi", eidos_audio_gain(30000, 200), 32767);
    BW_CHECK_EQ("saturate lo", eidos_audio_gain(-30000, 200), -32768);
}

int main(void)
{
    EidosPlatformConfig cfg;
    EidosPlatform *p = NULL;

    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 0);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 0);
    test_geometry();
    test_reshape();
    test_gain();

    eidos_settings_set_path(INI);
    remove(INI);
    eidos_settings_load();
    memset(&cfg, 0, sizeof cfg);
    eidos_settings_fill_config(&cfg);
    cfg.audio = 1;
    cfg.audio_push = 1;
    if (eidos_platform_init(&p, &cfg) != 0) {
        printf("no SDL video (dummy driver unavailable): skipped\n");
        BW_REPORT("test_settings");
    }
    eidos_settings_attach(p);
    test_platform_modes(p);
    test_window_follows_layout(p);
    {
        char buf[16];
        int v = 0;

        eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 0);
        BW_CHECK_EQ("res text auto", strcmp(eidos_settings_value_text(
            EIDOS_OPT_DISPLAY_RESOLUTION, buf, sizeof buf), "auto"), 0);
        eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 5);
        BW_CHECK_EQ("res text 5x", strcmp(eidos_settings_value_text(
            EIDOS_OPT_DISPLAY_RESOLUTION, buf, sizeof buf), "5x"), 0);
        BW_CHECK_EQ("fixed N", eidos_settings_internal_scale(), 5);
        eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 99);
        BW_CHECK_EQ("res clamps to max",
                    eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION),
                    EIDOS_RESOLUTION_MAX);
        /* Fit follows the window. */
        eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 0);
        eidos_settings_set(EIDOS_OPT_LAYOUT_ARRANGEMENT, EIDOS_LAYOUT_VERTICAL);
        eidos_settings_set(EIDOS_OPT_LAYOUT_SWAP, 0);
        eidos_settings_set(EIDOS_OPT_LAYOUT_GAP, 0);
        eidos_settings_set(EIDOS_OPT_DISPLAY_WIDTH, 512);
        eidos_settings_set(EIDOS_OPT_DISPLAY_HEIGHT, 768);
        v = eidos_settings_internal_scale();
        BW_CHECK_EQ("fit at 512x768", v, 2);
        eidos_settings_set(EIDOS_OPT_DISPLAY_WIDTH, 1024);
        eidos_settings_set(EIDOS_OPT_DISPLAY_HEIGHT, 1536);
        BW_CHECK_EQ("fit at 1024x1536 capped", eidos_settings_internal_scale(), 2);
        eidos_settings_revert();
    }
    eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 50);
    {
        s16 frames[64];
        memset(frames, 0x11, sizeof frames);
        eidos_platform_queue_audio(p, frames, 32);   /* runs with a gain */
    }
    test_bindings(p);
    eidos_settings_detach();
    eidos_platform_quit(p);
    test_file();
    /* The relay's port: 4141 by default, and typed as digits. */
    eidos_settings_net_defaults();
    BW_CHECK_EQ("the relay port is the relay's own",
                eidos_settings_net_relay_port(), 4141);
    eidos_settings_net_port_text_begin(0);
    BW_CHECK_EQ("the typing starts empty",
                strcmp(eidos_settings_net_port_text(0), ""), 0);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_5);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_X);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_0);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_0);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_0);
    BW_CHECK_EQ("digits only", strcmp(eidos_settings_net_port_text(0), "5000"), 0);
    BW_CHECK_EQ("Enter keeps it",
                eidos_settings_net_text_key(SDL_SCANCODE_RETURN), 1);
    BW_CHECK_EQ("the port is the typed one", eidos_settings_net_relay_port(), 5000);
    eidos_settings_net_port_text_begin(0);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_RETURN);
    BW_CHECK_EQ("an empty text keeps the port", eidos_settings_net_relay_port(), 5000);
    /* The address takes a name and a port after a colon (';' is the key). */
    eidos_settings_net_relay_text_begin();
    (void)eidos_settings_net_text_key(SDL_SCANCODE_A);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_PERIOD);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_B);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_SEMICOLON);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_9);
    (void)eidos_settings_net_text_key(SDL_SCANCODE_RETURN);
    BW_CHECK_EQ("a domain with a port", strcmp(eidos_settings_net_relay(), "a.b:9"), 0);
    eidos_settings_net_defaults();

    BW_REPORT("test_settings");
}
