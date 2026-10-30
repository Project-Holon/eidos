/*
 * engine/platform/platform.c -- SDL3 window, keyboard, gamepads, the mouse
 * as the stylus, frame pacer, audio device.
 *
 * See platform.h for the contract and for where the numbers come from.
 * Three things in here are decisions rather than transcription, and each is
 * commented where it is made: the pixel format the textures are created
 * with, the way the pacer accumulates the frame period's remainder, and the
 * screenshot being taken at 1x from the port's own buffers rather than read
 * back out of the renderer.
 */

#define SDL_MAIN_HANDLED   /* eidos owns main(); SDL_main.h defines no main */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "icon_rgba.h"
#include "input.h"
#include "mod_panel_draw.h"
#include "platform.h"
#include "plat_embed.h"
#include "plat_gl.h"
#include "plat_thread.h"
#include "settings.h"
#include "ui.h"

#define MAX_PADS 8

#define SCREEN_W EIDOS_DS_SCREEN_WIDTH
#define SCREEN_H EIDOS_DS_SCREEN_HEIGHT
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)


/*
 * The presenter's third image: the run-status panel (engine/platform/
 * mod_panel_draw.h).  The two calls are declared here rather than in
 * plat_gl.h: the panel is the port's own picture, drawn by the platform
 * layer beside the game's screens, and plat_gl.h is the presenter's own
 * header (shared with the 3D path).  The declaration is the port's
 * convention for a call whose header is not this file's, the way
 * engine/platform/mod_panel_draw.c declares the model's.
 */
extern void eidos_gl_upload_panel(EidosGl *gl, const unsigned char *rgba,
                                   int w, int h);
extern void eidos_gl_present_panel(EidosGl *gl, int out_w, int out_h,
                                   const SDL_Rect dst[2],
                                   const SDL_Rect *panel);

struct EidosPlatform {
    SDL_Window   *window;
    SDL_Renderer *renderer;        /* NULL when the GPU presenter runs      */
    EidosGl      *gl;              /* the OpenGL presenter (plat_gl.c)      */
    SDL_Texture  *tex[2];          /* 0 = top, 1 = bottom */
    u8           *shadow[2];       /* the last buffer uploaded to each      */
    SDL_Texture  *pre[2];          /* nearest pre-scale targets             */
    int pre_w[2], pre_h[2];
    int tex_n[2];                  /* internal multiple of each tex/shadow  */

    /* the run-status panel: the port's own picture, beside the screens */
    SDL_Texture  *panel_tex;       /* the software path's texture for it    */
    int panel_tex_w, panel_tex_h;
    u32 panel_gen;                 /* the model's generation last uploaded  */
    int panel_gen_ok;              /* ... and whether there was one         */
    int panel_w, panel_h;          /* the picture's size last uploaded      */

    /* the one-screen layout (EIDOS_LAYOUT_ONE): what the interface layer
     * asked the presenter to show this frame, and the mouse */
    EidosUiView ui_view;           /* mode FALLBACK until a view is set      */
    u32  ui_gen;                   /* the canvas generation last uploaded    */
    int  ui_gen_ok;
    int  ui_cw, ui_ch, ui_ck;      /* the canvas shown: size and scale       */
    int  m_pressed, m_released, m_rpressed, m_wheel;   /* this pump's edges  */

    int layout;
    int gap;
    int swap;
    int pixel_scale;            /* 0 auto, else exactly N (settings)    */
    int display_mode;
    int win_x, win_y;               /* windowed position, kept across fullscreen */
    int presented;                  /* a frame has been drawn at least once  */
    int arr_w, arr_h;               /* the arrangement's size last seen      */
    SDL_ThreadID owner;             /* the thread that made the window and GL context */
    int pend_display, pend_layout;  /* a change asked for from another thread  */
    int pd_mode, pd_w, pd_h, pl_layout, pl_swap, pl_gap;
    EidosKeyBinding *keymap_own;    /* set by eidos_platform_set_bindings    */
    EidosPadBinding *padmap_own;
    int swallow_sc;                 /* a key held after Alt+Enter, not a press */
    /* typing for the one-screen layout's name entry (eidos_platform_text_mode):
     * the keyboard's letters are text, not buttons; the arrows still move */
    int text_mode;
    int pad_last;                   /* the last input came from a pad      */
    int pad_type;                   /* that pad's SDL_GamepadType          */
    int face_default;               /* A, B, X, Y are on the shipped buttons */
    char text[128];
    int text_len;
    int cap_state;                  /* EIDOS_CAPTURE_*                       */
    EidosCaptured cap;

    const EidosKeyBinding *keymap;
    u32   keymap_count;
    const EidosPadBinding *padmap;
    u32   padmap_count;
    SDL_Gamepad *pads[MAX_PADS];
    int   pad_subsystem;            /* SDL_INIT_GAMEPAD is up                */

    int touch_down;                 /* the stylus, bottom-screen pixels      */
    int touch_x, touch_y;

    u32 buttons;
    u32 buttons_prev;
    u32 buttons_pressed;
    int quit;
    int shot_request;

    /* the pacer */
    u64 perf_freq;
    u64 perf_start;
    u64 next_ns;
    u64 period_rem;     /* accumulated 1/EIDOS_DS_SYSTEM_CLOCK-ths of a ns */
    int paced;          /* has the first boundary been set                 */
    u32 late_frames;
    u32 lag_frames;      /* whole frames the schedule lagged at the last wait */

    /* audio */
    SDL_AudioStream  *audio_dev;    /* the stream bound to the device        */
    EidosAudioFn      audio_fn;
    void             *audio_user;
    u64               audio_frames;
    volatile int      volume;       /* percent                               */
    int               audio_push;
    /* push mode: the device's own rate, and the resampler's state (the
     * last four input frames and the fractional input position). */
    int               dev_rate;
    double            rs_pos;
    s16               rs_hist[4][2];
};

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */
/* ------------------------------------------------------------------ */

/*
 * Physical key positions (SDL_Scancode), so the defaults land on the same
 * keys on a keyboard that is not QWERTY.  The d-pad is the arrow cluster
 * under the right hand and the four face buttons are the Z X A S block under
 * the left, which is the arrangement every DS-era PC control scheme settled
 * on.  No scancode is bound twice: a table that names one key for two
 * buttons would press both, since the word is rebuilt from the whole table.
 */
static const EidosKeyBinding k_default_keymap[] = {
    { SDL_SCANCODE_UP,        EIDOS_PAD_UP },
    { SDL_SCANCODE_DOWN,      EIDOS_PAD_DOWN },
    { SDL_SCANCODE_LEFT,      EIDOS_PAD_LEFT },
    { SDL_SCANCODE_RIGHT,     EIDOS_PAD_RIGHT },
    { SDL_SCANCODE_X,         EIDOS_PAD_A },
    { SDL_SCANCODE_Z,         EIDOS_PAD_B },
    { SDL_SCANCODE_S,         EIDOS_PAD_X },
    { SDL_SCANCODE_A,         EIDOS_PAD_Y },
    { SDL_SCANCODE_Q,         EIDOS_PAD_L },
    { SDL_SCANCODE_E,         EIDOS_PAD_R },
    { SDL_SCANCODE_RETURN,    EIDOS_PAD_START },
    { SDL_SCANCODE_RSHIFT,    EIDOS_PAD_SELECT },
    { SDL_SCANCODE_BACKSPACE, EIDOS_PAD_SELECT },
};

const EidosKeyBinding *eidos_default_keymap(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(k_default_keymap) / sizeof(k_default_keymap[0]));
    }
    return k_default_keymap;
}

/*
 * The word is rebuilt from the whole table on every pump rather than toggled
 * per key event, so a binding table may name several keys for one button
 * (Backspace and Right Shift are both SELECT) and a key held across a focus
 * change cannot leave a bit stuck down.
 */
static u32 read_keyboard(const EidosPlatform *p)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    u32 word = 0;
    u32 i;

    if (keys == NULL) {
        return 0;
    }
    for (i = 0; i < p->keymap_count; i++) {
        int sc = p->keymap[i].scancode;
        if (sc == p->swallow_sc) {
            continue;
        }
        if (p->text_mode && sc != SDL_SCANCODE_UP && sc != SDL_SCANCODE_DOWN &&
            sc != SDL_SCANCODE_LEFT && sc != SDL_SCANCODE_RIGHT) {
            continue;               /* typing: the letters are text */
        }
        if (sc > 0 && sc < SDL_SCANCODE_COUNT && (keys[sc] || eidos_embed_key(sc))) {
            word |= p->keymap[i].button;
        }
    }
    return word;
}

/* ------------------------------------------------------------------ */
/* Gamepads                                                            */
/* ------------------------------------------------------------------ */

/*
 * Positional: SDL's face buttons are named by position (south, east, west,
 * north) on every pad, and the DS's A/B/X/Y are east/south/north/west, so
 * each DS button lands where it sits on a DS.  The left stick doubles the d-pad and the triggers double
 * the shoulders, past half their travel.
 */
static const EidosPadBinding k_default_padmap[] = {
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_EAST,            EIDOS_PAD_A },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_SOUTH,           EIDOS_PAD_B },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_NORTH,           EIDOS_PAD_X },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_WEST,            EIDOS_PAD_Y },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  EIDOS_PAD_L },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, EIDOS_PAD_R },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_START,         EIDOS_PAD_START },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_BACK,          EIDOS_PAD_SELECT },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_UP,       EIDOS_PAD_UP },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_DOWN,     EIDOS_PAD_DOWN },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT,     EIDOS_PAD_LEFT },
    { EIDOS_PADBIND_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    EIDOS_PAD_RIGHT },
    { EIDOS_PADBIND_AXIS_NEG, SDL_GAMEPAD_AXIS_LEFTY,         EIDOS_PAD_UP },
    { EIDOS_PADBIND_AXIS_POS, SDL_GAMEPAD_AXIS_LEFTY,         EIDOS_PAD_DOWN },
    { EIDOS_PADBIND_AXIS_NEG, SDL_GAMEPAD_AXIS_LEFTX,         EIDOS_PAD_LEFT },
    { EIDOS_PADBIND_AXIS_POS, SDL_GAMEPAD_AXIS_LEFTX,         EIDOS_PAD_RIGHT },
    { EIDOS_PADBIND_AXIS_POS, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,   EIDOS_PAD_L },
    { EIDOS_PADBIND_AXIS_POS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,  EIDOS_PAD_R },
};

/* Half an axis's travel (SDL axes run -32768..32767, triggers 0..32767). */
#define PAD_AXIS_THRESHOLD 16384

const EidosPadBinding *eidos_default_padmap(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(k_default_padmap) / sizeof(k_default_padmap[0]));
    }
    return k_default_padmap;
}

static SDL_JoystickID pad_id(SDL_Gamepad *gc)
{
    return SDL_GetGamepadID(gc);
}

static void pad_open(EidosPlatform *p, SDL_JoystickID id)
{
    SDL_Gamepad *gc;
    int i, slot = -1;

    if (!SDL_IsGamepad(id)) {
        return;
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] == NULL) {
            if (slot < 0) {
                slot = i;
            }
        } else if (pad_id(p->pads[i]) == id) {
            return;                     /* already open                     */
        }
    }
    if (slot < 0) {
        return;
    }
    gc = SDL_OpenGamepad(id);
    if (gc != NULL) {
        p->pads[slot] = gc;
    }
}

static void pad_close(EidosPlatform *p, SDL_JoystickID id)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] != NULL && pad_id(p->pads[i]) == id) {
            SDL_CloseGamepad(p->pads[i]);
            p->pads[i] = NULL;
        }
    }
}

int eidos_platform_pads(const EidosPlatform *p)
{
    int i, n = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < MAX_PADS; i++) {
        n += p->pads[i] != NULL;
    }
    return n;
}

static int pad_binding_held(SDL_Gamepad *gc, const EidosPadBinding *b)
{
    int v;

    if (b->kind == EIDOS_PADBIND_BUTTON) {
        return b->index >= 0 && b->index < SDL_GAMEPAD_BUTTON_COUNT &&
               SDL_GetGamepadButton(gc, (SDL_GamepadButton)b->index);
    }
    if (b->index < 0 || b->index >= SDL_GAMEPAD_AXIS_COUNT) {
        return 0;
    }
    v = SDL_GetGamepadAxis(gc, (SDL_GamepadAxis)b->index);
    if (b->kind == EIDOS_PADBIND_AXIS_POS) {
        return v >= PAD_AXIS_THRESHOLD;
    }
    if (b->kind == EIDOS_PADBIND_AXIS_NEG) {
        return v <= -PAD_AXIS_THRESHOLD;
    }
    return 0;
}

/*
 * The shipped face buttons sit where the DS has them (A east, B south), as
 * on Nintendo's pads.  On a pad whose bottom button confirms (Xbox's A,
 * PlayStation's cross) the shipped binding turns to that pad's habit: A
 * south, B east, X west, Y north.  A binding the player made stands as set.
 */
static int pad_bottom_confirms(int type)
{
    switch (type) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return 0;
    default:
        return 1;
    }
}

/* Whether A, B, X and Y are bound exactly as shipped (k_default_padmap). */
static void note_face_default(EidosPlatform *p)
{
    static const u32 face[4] = { EIDOS_PAD_A, EIDOS_PAD_B, EIDOS_PAD_X, EIDOS_PAD_Y };
    static const int want[4] = { SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH,
                                 SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST };
    int f, seen[4] = { 0, 0, 0, 0 };
    u32 i;

    p->face_default = 0;
    for (i = 0; i < p->padmap_count; i++) {
        for (f = 0; f < 4; f++) {
            if (p->padmap[i].button != face[f]) {
                continue;
            }
            if (p->padmap[i].kind != EIDOS_PADBIND_BUTTON || p->padmap[i].index != want[f] || seen[f]) {
                return;
            }
            seen[f] = 1;
        }
    }
    p->face_default = seen[0] && seen[1] && seen[2] && seen[3];
}

/* A binding as it reads on a pad of `type`. */
static EidosPadBinding pad_binding_for(const EidosPlatform *p, const EidosPadBinding *b, int type)
{
    EidosPadBinding r = *b;

    if (p->face_default && pad_bottom_confirms(type) && r.kind == EIDOS_PADBIND_BUTTON &&
        r.index >= SDL_GAMEPAD_BUTTON_SOUTH && r.index <= SDL_GAMEPAD_BUTTON_NORTH) {
        r.index ^= 1;           /* south <-> east, west <-> north */
    }
    return r;
}

/* engine/ui/ui.c: EIDOS_DEBUG_BATTLE has started its battle (absent from
 * builds without the interface) */
extern int eidos_ui_debug_battle_started(void) __attribute__((weak));

int eidos_platform_face_swap(const EidosPlatform *p)
{
    return p != NULL && p->face_default && pad_bottom_confirms(p->pad_type);
}

/* Rebuilt from the whole table on every pump, as the keyboard is.         */
static u32 read_pads(const EidosPlatform *p)
{
    u32 word = 0;
    u32 i;
    int k;

    for (k = 0; k < MAX_PADS; k++) {
        int type;

        if (p->pads[k] == NULL) {
            continue;
        }
        type = (int)SDL_GetGamepadTypeForID(pad_id(p->pads[k]));
        for (i = 0; i < p->padmap_count; i++) {
            EidosPadBinding b = pad_binding_for(p, &p->padmap[i], type);

            if (pad_binding_held(p->pads[k], &b)) {
                word |= p->padmap[i].button;
            }
        }
    }
    return word;
}

/* ------------------------------------------------------------------ */
/* The stylus                                                          */
/* ------------------------------------------------------------------ */

static void place(const EidosPlatform *p, EidosScreenPlacement *pl)
{
    eidos_layout_place(p->layout, p->swap, p->gap, pl);
}

/* Where the screens land in an output of w x h, at the pixel scale set.  In
 * the one-screen layout this is the fallback's (the two screens side by
 * side): the world view has no screen rectangles of its own. */
static void viewport(const EidosPlatform *p, const EidosScreenPlacement *pl,
                     int w, int h, EidosViewport *vp)
{
    if (p->layout == EIDOS_LAYOUT_ONE) {
        eidos_one_fallback_viewport(p->swap, p->pixel_scale, w, h, vp);
        return;
    }
    eidos_display_viewport_n(pl, w, h, p->pixel_scale, vp);
}

/* The window size for the arrangement when there is no saved one. */
static void default_window_size(const EidosPlatform *p, int *w, int *h)
{
    EidosScreenPlacement pl;
    SDL_Rect area;
    SDL_DisplayID idx = p->window != NULL ? SDL_GetDisplayForWindow(p->window)
                                          : SDL_GetPrimaryDisplay();

    place(p, &pl);
    if (idx == 0 || !SDL_GetDisplayUsableBounds(idx, &area)) {
        area.w = area.h = 0;
    }
    eidos_first_window_size(&pl, area.w, area.h, EIDOS_FIRST_WINDOW_PERCENT, w, h);
}

static void out_size(const EidosPlatform *p, int *w, int *h)
{
    *w = *h = 0;
    if (p->gl != NULL) {
        SDL_GetWindowSizeInPixels(p->window, w, h);
    } else if (p->renderer == NULL ||
               !SDL_GetCurrentRenderOutputSize(p->renderer, w, h)) {
        if (p->window != NULL) {
            SDL_GetWindowSize(p->window, w, h);
        }
    }
}

void eidos_platform_viewport(const EidosPlatform *p, EidosViewport *vp)
{
    EidosScreenPlacement pl;
    int w, h;

    place(p, &pl);
    out_size(p, &w, &h);
    viewport(p, &pl, w, h, vp);
}

int eidos_platform_window_to_touch(const EidosPlatform *p, int win_w,
                                   int win_h, int wx, int wy, int *x, int *y)
{
    EidosScreenPlacement pl;
    EidosViewport vp;

    if (p == NULL || win_w <= 0 || win_h <= 0) {
        return 0;
    }
    place(p, &pl);
    viewport(p, &pl, win_w, win_h, &vp);
    return eidos_viewport_to_touch(&pl, &vp, wx, wy, x, y);
}

/*
 * The world view is on: the one-screen layout, a view that asks for the
 * world, and a presenter that can draw it (the GPU one, with the GPU 3D).
 * Otherwise the one-screen layout shows the fallback.
 */
extern int bw_battle_view_active(void) __attribute__((weak));    /* sdk/battle_view.c, eidos_boot only */

static int world_active(const EidosPlatform *p)
{
    return p != NULL && p->layout == EIDOS_LAYOUT_ONE && p->gl != NULL &&
           eidos_gl_has3d(p->gl) && p->ui_view.mode == EIDOS_VIEW_WORLD;
}

static void read_stylus(EidosPlatform *p)
{
    float fx = 0.0f, fy = 0.0f;
    SDL_MouseButtonFlags buttons = eidos_embed_on() ? (SDL_MouseButtonFlags)eidos_embed_mouse(&fx, &fy)
                                                    : SDL_GetMouseState(&fx, &fy);
    int mx = (int)fx, my = (int)fy, ww = 0, wh = 0, x = 0, y = 0, inside;

    if (!(buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) || p->window == NULL ||
        world_active(p)) {
        /* In the world view the mouse belongs to the interface layer
         * (eidos_platform_mouse); the stylus is only the fallback's. */
        p->touch_down = 0;
        return;
    }
    SDL_GetWindowSize(p->window, &ww, &wh);
    {
        /* The mouse is in window units, the picture in output pixels. */
        int ow = 0, oh = 0;
        out_size(p, &ow, &oh);
        if (ww > 0 && wh > 0 && ow > 0 && oh > 0) {
            mx = (int)((s64)mx * ow / ww);
            my = (int)((s64)my * oh / wh);
            ww = ow;
            wh = oh;
        }
    }
    inside = eidos_platform_window_to_touch(p, ww, wh, mx, my, &x, &y);
    if (!p->touch_down && !inside) {
        return;                         /* a press off the panel            */
    }
    p->touch_down = 1;
    p->touch_x = x;
    p->touch_y = y;
}

int eidos_platform_touch(const EidosPlatform *p, int *x, int *y)
{
    if (p == NULL || !p->touch_down) {
        return 0;
    }
    if (x != NULL) {
        *x = p->touch_x;
    }
    if (y != NULL) {
        *y = p->touch_y;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

u64 eidos_frame_period_ns(void)
{
    return (u64)EIDOS_DS_FRAME_CLOCKS * 1000000000ull
           / (u64)EIDOS_DS_SYSTEM_CLOCK;
}

/*
 * Nanoseconds since perf_start, without the overflow a plain
 * `elapsed * 1000000000 / freq` has: the product overflows 64 bits after
 * about half an hour on a 10 MHz performance counter, which is exactly the
 * kind of bug that only shows up in a long session.
 */
static u64 now_ns(const EidosPlatform *p)
{
    u64 elapsed = SDL_GetPerformanceCounter() - p->perf_start;
    u64 whole = elapsed / p->perf_freq;
    u64 frac = elapsed % p->perf_freq;
    return whole * 1000000000ull + frac * 1000000000ull / p->perf_freq;
}

u64 eidos_platform_now_ns(const EidosPlatform *p)
{
    return p == NULL ? 0 : now_ns(p);
}

u32 eidos_platform_lag_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->lag_frames;
}

u32 eidos_platform_late_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->late_frames;
}

static int g_fast_forward_locked;

void eidos_platform_fast_forward_lock(int locked)
{
    g_fast_forward_locked = locked != 0;
}

int eidos_platform_fast_forward(const EidosPlatform *p)
{
    const bool *keys;

    if (p == NULL || p->window == NULL) {
        return 0;
    }
    if (g_fast_forward_locked) {
        return 1;
    }
    /* SDL keeps the state of the window with keyboard focus only, so the
     * key does nothing while another program has the focus. */
    keys = SDL_GetKeyboardState(NULL);
    if (keys != NULL && keys[SDL_SCANCODE_TAB]) {
        return keys[SDL_SCANCODE_LSHIFT] ? EIDOS_FAST_FORWARD_MAX
                                         : EIDOS_FAST_FORWARD;
    }
    /* EIDOS_FAST_UNTIL_BATTLE (the spot editor's Preview in game): as fast
     * as possible until the first battle shows, then the normal speed; =start
     * (its Play battle) only until EIDOS_DEBUG_BATTLE starts the battle, so
     * the encounter's own transition plays at its speed. */
    {
        static int until = -1, seen;

        if (until < 0) {
            const char *v = getenv("EIDOS_FAST_UNTIL_BATTLE");

            until = v == NULL || *v == '\0' ? 0 : strcmp(v, "start") == 0 ? 2 : 1;
        }
        if (until && !seen) {
            if (until == 2 ? (eidos_ui_debug_battle_started != NULL && eidos_ui_debug_battle_started())
                           : (bw_battle_view_active != NULL && bw_battle_view_active())) {
                seen = 1;
            } else {
                return EIDOS_FAST_FORWARD_MAX;
            }
        }
    }
    /* EIDOS_FAST_FORWARD=1 holds Tab down for a whole run, =max left Shift
     * and Tab (tests, timing). */
    {
        static int env = -1;

        if (env < 0) {
            const char *v = getenv("EIDOS_FAST_FORWARD");
            env = (v == NULL || *v == '\0' || strcmp(v, "0") == 0) ? 1
                : strcmp(v, "max") == 0 ? EIDOS_FAST_FORWARD_MAX
                : EIDOS_FAST_FORWARD;
        }
        return env;
    }
}

/* A sleep of the frame thread.  In the wasm32 build the program's threads
 * take turns on one host thread (sdk/plat_thread.h): the others run while
 * this one waits. */
static void eidos_delay_ms(Uint32 ms)
{
#if defined(__wasm__)
    bw_thread_sleep_ns(ms > 0 ? (unsigned long long)ms * 1000000ull : 100000ull);
#else
    SDL_Delay(ms);
#endif
}

void eidos_platform_wait_frame(EidosPlatform *p)
{
    u64 now;
    u64 period;
    u64 step;
    u64 rem_step;

    if (p == NULL) {
        return;
    }
    {
        /* EIDOS_NOWAIT=1: no pacing at all, every frame drawn and shown,
         * so a run's wall time is what its frames cost (a benchmark). */
        static int nowait = -1;

        if (nowait < 0) {
            const char *v = getenv("EIDOS_NOWAIT");

            nowait = v != NULL && *v != '\0' && strcmp(v, "0") != 0;
        }
        if (nowait) {
            return;
        }
    }
    /* Fast forward: the period is the factor times shorter (with
     * EIDOS_FAST_FORWARD_MAX the host is always behind, so it never waits). */
    period = (u64)EIDOS_DS_FRAME_CLOCKS * 1000000000ull
             / (u64)eidos_platform_fast_forward(p);
    step = period / (u64)EIDOS_DS_SYSTEM_CLOCK;
    /* 560190 * 10^9 mod 33513982 = 3790034, the part of the period the
     * integer nanosecond count drops.  Accumulating it adds the missing
     * nanosecond back every ninth frame instead of losing 0.113 ns a frame. */
    rem_step = period % (u64)EIDOS_DS_SYSTEM_CLOCK;
    now = now_ns(p);
    if (!p->paced) {
        p->next_ns = now;
        p->period_rem = 0;
        p->paced = 1;
    }

    p->next_ns += step;
    p->period_rem += rem_step;
    if (p->period_rem >= (u64)EIDOS_DS_SYSTEM_CLOCK) {
        p->period_rem -= (u64)EIDOS_DS_SYSTEM_CLOCK;
        p->next_ns += 1;
    }

    p->lag_frames = now > p->next_ns ? (u32)((now - p->next_ns) / step) : 0;
    if (now > p->next_ns + 4 * step) {
        /* More than four frames behind: a breakpoint, a suspended machine or
         * a host that cannot keep up.  Running the backlog would show a
         * burst of fast frames, so the schedule is restarted from now. */
        p->late_frames++;
        p->next_ns = now;
        p->period_rem = 0;
        return;
    }

    for (;;) {
        u64 left;
        now = now_ns(p);
        if (now >= p->next_ns) {
            return;
        }
        left = p->next_ns - now;
        if (left > 2500000ull) {
            /* Sleep the bulk, but leave about 2 ms: SDL_Delay may overshoot
             * by the host scheduler's resolution (1 ms at best). */
            eidos_delay_ms((Uint32)((left - 2000000ull) / 1000000ull));
        } else {
            /* The last stretch: yield the core instead of burning it. */
            eidos_delay_ms(0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/* The master volume: a gain on the final mixed sample, saturating (a gain
 * over 100 percent is allowed for a later boost option). */
s16 eidos_audio_gain(s16 v, int percent)
{
    int r = ((int)v * percent + (v < 0 ? -50 : 50)) / 100;

    return (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

static void render(EidosPlatform *p);

void eidos_platform_set_pixel_scale(EidosPlatform *p, int scale)
{
    if (p == NULL) {
        return;
    }
    p->pixel_scale = scale < 0 ? 0 : scale;
    if (p->presented && (p->owner == 0 || SDL_GetCurrentThreadID() == p->owner)) {
        render(p);
    }
}

void eidos_platform_set_outlines(EidosPlatform *p, int mode)
{
    if (p != NULL && p->gl != NULL) {
        eidos_gl_set_edges(p->gl, mode);
    }
}

void eidos_platform_set_volume(EidosPlatform *p, int percent)
{
    if (p != NULL) {
        p->volume = percent < 0 ? 0 : percent > 400 ? 400 : percent;
    }
}

/* SDL asks for `additional` more bytes; they are made in 1024-frame pieces. */
static void audio_callback(void *user, SDL_AudioStream *stream, int additional,
                           int total)
{
    EidosPlatform *p = (EidosPlatform *)user;
    s16 buf[1024 * EIDOS_AUDIO_CHANNELS];

    (void)total;
    while (additional > 0) {
        int len = additional < (int)sizeof(buf) ? additional : (int)sizeof(buf);
        u32 frames = (u32)len / (sizeof(s16) * EIDOS_AUDIO_CHANNELS);

        if (frames == 0) {
            break;
        }
        if (p->audio_fn != NULL) {
            p->audio_fn(p->audio_user, buf, frames);
        } else {
            memset(buf, 0, sizeof(buf));
        }
        p->audio_frames += frames;
        if (p->volume != 100) {
            u32 n;
            for (n = 0; n < frames * 2u; n++) {
                buf[n] = eidos_audio_gain(buf[n], p->volume);
            }
        }
        SDL_PutAudioStreamData(stream, buf,
                               (int)(frames * sizeof(s16) * EIDOS_AUDIO_CHANNELS));
        additional -= (int)(frames * sizeof(s16) * EIDOS_AUDIO_CHANNELS);
    }
}

void eidos_platform_set_audio(EidosPlatform *p, EidosAudioFn fn, void *user)
{
    if (p == NULL) {
        return;
    }
    if (p->audio_dev != NULL) {
        SDL_LockAudioStream(p->audio_dev);
    }
    p->audio_fn = fn;
    p->audio_user = user;
    if (p->audio_dev != NULL) {
        SDL_UnlockAudioStream(p->audio_dev);
    }
}

/* Catmull-Rom through four input frames, t in [0,1) between y1 and y2. */
static s16 cubic16(s16 y0, s16 y1, s16 y2, s16 y3, double t)
{
    double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
    double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    double c = -0.5 * y0 + 0.5 * y2;
    double v = ((a * t + b) * t + c) * t + y1;

    return (s16)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : (int)v);
}

/*
 * The game's 32768 Hz stream to the device's rate, converted once, here,
 * with state kept across calls (the position and the last input frames), so
 * chunk boundaries are seamless.  A device that took 32768 Hz needs none.
 */
void eidos_platform_queue_audio(EidosPlatform *p, const s16 *in, u32 frames)
{
    static s16 out[2u * 4096u];

    if (p == NULL || p->audio_dev == NULL || !p->audio_push || frames == 0) {
        return;
    }
    p->audio_frames += frames;
    if (p->dev_rate == EIDOS_AUDIO_RATE) {
        if (p->volume == 100) {
            SDL_PutAudioStreamData(p->audio_dev, in,
                                   (int)(frames * (u32)(sizeof(s16) * EIDOS_AUDIO_CHANNELS)));
        } else {
            u32 done = 0;
            while (done < frames) {
                u32 n = frames - done < 4096u ? frames - done : 4096u, k;
                for (k = 0; k < n * 2u; k++) {
                    out[k] = eidos_audio_gain(in[done * 2u + k], p->volume);
                }
                SDL_PutAudioStreamData(p->audio_dev, out, (int)(n * 4u));
                done += n;
            }
        }
        return;
    }
    {
        const double step = (double)EIDOS_AUDIO_RATE / (double)p->dev_rate;
        u32 n = 0;
        u32 i = 0;      /* input frames consumed into the history */
        double pos = p->rs_pos;    /* between hist[1] and hist[2] */

        /* hist[0..3] are input frames k-1, k, k+1, k+2; pos is the phase
         * after hist[1].  Advance the window while pos >= 1. */
        for (;;) {
            while (pos >= 1.0) {
                if (i >= frames) {
                    goto flush;
                }
                p->rs_hist[0][0] = p->rs_hist[1][0];
                p->rs_hist[0][1] = p->rs_hist[1][1];
                p->rs_hist[1][0] = p->rs_hist[2][0];
                p->rs_hist[1][1] = p->rs_hist[2][1];
                p->rs_hist[2][0] = p->rs_hist[3][0];
                p->rs_hist[2][1] = p->rs_hist[3][1];
                p->rs_hist[3][0] = eidos_audio_gain(in[i * 2u], p->volume);
                p->rs_hist[3][1] = eidos_audio_gain(in[i * 2u + 1u], p->volume);
                i++;
                pos -= 1.0;
            }
            out[n * 2u] = cubic16(p->rs_hist[0][0], p->rs_hist[1][0],
                                  p->rs_hist[2][0], p->rs_hist[3][0], pos);
            out[n * 2u + 1u] = cubic16(p->rs_hist[0][1], p->rs_hist[1][1],
                                       p->rs_hist[2][1], p->rs_hist[3][1], pos);
            n++;
            pos += step;
            if (n == 4096u) {
                SDL_PutAudioStreamData(p->audio_dev, out, (int)(n * 4u));
                n = 0;
            }
        }
flush:
        p->rs_pos = pos;
        if (n != 0) {
            SDL_PutAudioStreamData(p->audio_dev, out, (int)(n * 4u));
        }
    }
}

/* Queued frames, in the game's 32768 Hz units. */
u32 eidos_platform_audio_queued(const EidosPlatform *p)
{
    u64 dev;

    if (p == NULL || p->audio_dev == NULL || !p->audio_push) {
        return 0;
    }
    dev = (u64)SDL_GetAudioStreamQueued(p->audio_dev) / (u32)(sizeof(s16) * EIDOS_AUDIO_CHANNELS);
    return (u32)(dev * EIDOS_AUDIO_RATE / (u64)p->dev_rate);
}

int eidos_platform_audio_open(const EidosPlatform *p)
{
    return p != NULL && p->audio_dev != NULL;
}

u64 eidos_platform_audio_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->audio_frames;
}

static void open_audio(EidosPlatform *p)
{
    SDL_AudioSpec want, dev;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "eidos: no audio subsystem (%s); running silent\n",
                SDL_GetError());
        return;
    }
    /* 1024 frames is 31 ms at 32768 Hz, under two DS frames: short enough
     * that a sound effect started this frame is not audibly late, long
     * enough that the callback is not called twice per frame. */
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
    memset(&want, 0, sizeof(want));
    want.freq = EIDOS_AUDIO_RATE;
    want.format = SDL_AUDIO_S16;
    want.channels = EIDOS_AUDIO_CHANNELS;
    /* Push mode takes the device's own rate and resamples once itself
     * (queue_audio); callback mode lets SDL convert. */
    p->dev_rate = EIDOS_AUDIO_RATE;
    if (p->audio_push &&
        SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dev, NULL) &&
        dev.freq > 0) {
        p->dev_rate = dev.freq;
        want.freq = dev.freq;
    }
    p->audio_dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &want,
                                             p->audio_push ? NULL : audio_callback,
                                             p->audio_push ? NULL : p);
    if (p->audio_dev == NULL) {
        fprintf(stderr, "eidos: no audio device (%s); running silent\n",
                SDL_GetError());
        return;
    }
    p->rs_pos = 1.0;
    SDL_ResumeAudioStreamDevice(p->audio_dev);
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

static void layout_size(const EidosPlatform *p, u32 *w, u32 *h)
{
    EidosScreenPlacement pl;

    place(p, &pl);
    *w = (u32)pl.width;
    *h = (u32)pl.height;
}

static void set_filter(SDL_Texture *t, int linear)
{
    (void)SDL_SetTextureScaleMode(t, linear ? SDL_SCALEMODE_LINEAR
                                            : SDL_SCALEMODE_NEAREST);
}

/* The renderer takes float rectangles. */
static SDL_FRect frect(const SDL_Rect *r)
{
    SDL_FRect f;

    f.x = (float)r->x;
    f.y = (float)r->y;
    f.w = (float)r->w;
    f.h = (float)r->h;
    return f;
}

#define PRE_MAX_SIDE 8192
/* Above this many pixels per screen (a 6x6 frame is 1.8M) the nearest
 * prescale pass costs more than it gains: plain linear instead.  The same
 * for a software renderer, where the pass is a CPU copy. */
#define PRE_MAX_PIXELS (2 * 1024 * 1024)

static int renderer_is_software(const EidosPlatform *p)
{
    const char *name = SDL_GetRendererName(p->renderer);

    return name != NULL && strcmp(name, SDL_SOFTWARE_RENDERER) == 0;
}

/* Draw screen i's texture into dst, choosing the filter (see platform.h). */
static void draw_screen(EidosPlatform *p, int i, const SDL_Rect *dst)
{
    const int tw = (int)SCREEN_W * p->tex_n[i], th = (int)SCREEN_H * p->tex_n[i];
    SDL_Texture *src = p->tex[i];
    SDL_FRect fdst = frect(dst);

    if (dst->w < tw || dst->h < th) {
        set_filter(src, 1);                       /* downscale */
    } else if (dst->w % tw == 0 && dst->h % th == 0 &&
               dst->w / tw == dst->h / th) {
        set_filter(src, 0);                       /* whole multiple */
    } else {
        int k = (dst->w + tw - 1) / tw, kh = (dst->h + th - 1) / th;

        if (kh > k) {
            k = kh;
        }
        set_filter(src, 0);
        if (k * tw <= PRE_MAX_SIDE && k * th <= PRE_MAX_SIDE &&
            (s64)k * tw * k * th <= PRE_MAX_PIXELS && !renderer_is_software(p)) {
            if (p->pre[i] == NULL || p->pre_w[i] != k * tw ||
                p->pre_h[i] != k * th) {
                if (p->pre[i] != NULL) {
                    SDL_DestroyTexture(p->pre[i]);
                }
                p->pre[i] = SDL_CreateTexture(p->renderer,
                                              SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_TARGET,
                                              k * tw, k * th);
                p->pre_w[i] = k * tw;
                p->pre_h[i] = k * th;
                if (p->pre[i] != NULL) {
                    set_filter(p->pre[i], 1);
                }
            }
            if (p->pre[i] != NULL &&
                SDL_SetRenderTarget(p->renderer, p->pre[i])) {
                SDL_RenderTexture(p->renderer, src, NULL, NULL);
                SDL_SetRenderTarget(p->renderer, NULL);
                SDL_RenderTexture(p->renderer, p->pre[i], NULL, &fdst);
                return;
            }
            SDL_SetRenderTarget(p->renderer, NULL);
        }
        set_filter(src, 1);                       /* plain linear */
    }
    SDL_RenderTexture(p->renderer, src, NULL, &fdst);
}

/* ------------------------------------------------------------------ */
/* The run-status panel                                                */
/* ------------------------------------------------------------------ */

/*
 * The two screens' own area: their bounding box, in the units the two
 * rectangles are given in.  The arrangement also holds the run-status
 * panel's strip, and the screenshot is the DS's screens: the panel's strip
 * is not part of it.
 */
static void screens_rect(const EidosRect *a, const EidosRect *b,
                         EidosRect *out)
{
    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = a->x + a->w;
    int y1 = a->y + a->h;

    if (b->x + b->w > x1) {
        x1 = b->x + b->w;
    }
    if (b->y + b->h > y1) {
        y1 = b->y + b->h;
    }
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
}

/*
 * The panel's picture and the rectangle it goes to in the output, or -1
 * when there is no panel to show: the player hid it, or no mod has filled
 * a line.
 *
 * The room is the arrangement's (eidos_mod_panel_space), the same one the
 * geometry grew the arrangement by, so the picture lands in the room and
 * never on the game.
 */
static int panel_view(const EidosViewport *vp, const EidosScreenPlacement *pl,
                      EidosModPanelView *view)
{
    EidosModPanelSpace space;

    eidos_mod_panel_space(pl, &space);
    return eidos_mod_panel_view(view, vp, &space, pl->width, pl->height);
}

/*
 * The panel's picture, uploaded only when it changed.  The view carries
 * the model's generation, and the picture is redrawn only when the model's
 * lines change (engine/platform/mod_panel_draw.h), so the picture is
 * uploaded once per change and not once per frame.
 */
static void upload_panel(EidosPlatform *p, const EidosModPanelView *view)
{
    if (p->panel_gen_ok && p->panel_gen == view->generation &&
        p->panel_w == view->w && p->panel_h == view->h) {
        return;                         /* the same picture as last frame */
    }
    p->panel_gen = view->generation;
    p->panel_gen_ok = 1;
    p->panel_w = view->w;
    p->panel_h = view->h;
    if (p->gl != NULL) {
        eidos_gl_upload_panel(p->gl, view->rgba, view->w, view->h);
        return;
    }
    /*
     * The software path: a texture of its own.  ABGR8888 is a packed
     * 32-bit pixel 0xAABBGGRR, which on a little-endian host is the byte
     * order R, G, B, A -- what the panel's own drawing writes, the same
     * reasoning as the screens' textures above.
     */
    if (p->panel_tex == NULL || p->panel_tex_w != view->w ||
        p->panel_tex_h != view->h) {
        if (p->panel_tex != NULL) {
            SDL_DestroyTexture(p->panel_tex);
            p->panel_tex = NULL;
        }
        p->panel_tex = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        view->w, view->h);
        p->panel_tex_w = view->w;
        p->panel_tex_h = view->h;
        if (p->panel_tex == NULL) {
            fprintf(stderr, "eidos: SDL_CreateTexture failed: %s\n",
                    SDL_GetError());
            return;
        }
        set_filter(p->panel_tex, 0);         /* nearest: the text is sharp */
    }
    SDL_UpdateTexture(p->panel_tex, NULL, view->rgba, view->w * 4);
}

/* Draw both screen textures into the current output.  Used by present and
 * by every runtime change, so a new mode shows at once. */
static void render(EidosPlatform *p)
{
    EidosViewport vp;
    EidosScreenPlacement pl;
    EidosModPanelView view;
    int i, ow, oh, have_panel;

    place(p, &pl);
    out_size(p, &ow, &oh);
    viewport(p, &pl, ow, oh, &vp);
    have_panel = p->layout != EIDOS_LAYOUT_ONE && panel_view(&vp, &pl, &view) == 0;
    if (have_panel) {
        upload_panel(p, &view);
    }
    if (p->gl != NULL) {
        SDL_Rect rects[2];
        SDL_Rect prect;

        /* The one-screen layout's state for this frame: the world's
         * rectangle for the DS screen's 2D layers, or off. */
        if (p->layout == EIDOS_LAYOUT_ONE) {
            EidosWorldGeometry wg;
            SDL_Rect ds;

            eidos_world_geometry(ow, oh, p->ui_view.zoom, &wg);
            ds.x = wg.ds.x;
            ds.y = wg.ds.y;
            ds.w = wg.ds.w;
            ds.h = wg.ds.h;
            eidos_gl_one_set(p->gl, 1, world_active(p), p->ui_view.world_screen,
                             &ds, p->ui_view.hide_2d, p->ui_view.blur,
                             (p->ui_view.no_fade ? 1 : 0) | (p->ui_view.hold_world ? 2 : 0) |
                             (p->ui_view.keep_world ? 4 : 0) |
                             ((p->ui_view.dark & 31) << 8) |
                             ((p->ui_view.cap_mix & 31) << 13) |
                             (p->ui_view.ds_only ? 1 << 18 : 0));
        } else {
            eidos_gl_one_set(p->gl, 0, 0, 0, NULL, 0, 0, 0);
        }

        for (i = 0; i < 2; i++) {
            rects[i].x = vp.screen[i].x;
            rects[i].y = vp.screen[i].y;
            rects[i].w = vp.screen[i].w;
            rects[i].h = vp.screen[i].h;
        }
        if (eidos_embed_on()) {
            /* shown inside another program: the picture goes to it */
            unsigned char *px = eidos_embed_pixels(ow, oh);

            if (px != NULL && eidos_gl_grab(p->gl, ow, oh, rects, px) == 0) {
                eidos_embed_publish(ow, oh);
            }
            return;
        }
        if (have_panel && view.dst.w > 0 && view.dst.h > 0) {
            prect.x = view.dst.x;
            prect.y = view.dst.y;
            prect.w = view.dst.w;
            prect.h = view.dst.h;
            eidos_gl_present_panel(p->gl, ow, oh, rects, &prect);
        } else {
            eidos_gl_present(p->gl, ow, oh, rects);
        }
        return;
    }
    SDL_RenderClear(p->renderer);
    for (i = 0; i < 2; i++) {
        SDL_Rect dst;

        dst.x = vp.screen[i].x;
        dst.y = vp.screen[i].y;
        dst.w = vp.screen[i].w;
        dst.h = vp.screen[i].h;
        if (dst.w > 0 && dst.h > 0 && p->tex[i] != NULL) {
            draw_screen(p, i, &dst);
        }
    }
    /* The panel's picture, in the room the arrangement reserved for it. */
    if (have_panel && p->panel_tex != NULL && view.dst.w > 0 &&
        view.dst.h > 0) {
        SDL_FRect dst;

        dst.x = (float)view.dst.x;
        dst.y = (float)view.dst.y;
        dst.w = (float)view.dst.w;
        dst.h = (float)view.dst.h;
        SDL_RenderTexture(p->renderer, p->panel_tex, NULL, &dst);
    }
    SDL_RenderPresent(p->renderer);
}

/* (Re)create screen i's texture and shadow for multiple n. */
static int frame_alloc(EidosPlatform *p, int i, int n)
{
    const int w = (int)SCREEN_W * n, h = (int)SCREEN_H * n;

    if (p->tex[i] != NULL) {
        SDL_DestroyTexture(p->tex[i]);
        p->tex[i] = NULL;
    }
    free(p->shadow[i]);
    p->shadow[i] = NULL;
    /*
     * ABGR8888 is a packed 32-bit pixel 0xAABBGGRR, which on a
     * little-endian host is the byte order R, G, B, A -- exactly what
     * g2d_frame_compose writes.  Naming the packed format rather than
     * SDL_PIXELFORMAT_RGBA32 keeps that reasoning visible.
     */
    if (p->gl == NULL) {
        p->tex[i] = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, w, h);
        if (p->tex[i] == NULL) {
            fprintf(stderr, "eidos: SDL_CreateTexture failed: %s\n",
                    SDL_GetError());
            return -1;
        }
    }
    p->shadow[i] = (u8 *)calloc((size_t)w * (size_t)h, 4);
    if (p->shadow[i] == NULL) {
        return -1;
    }
    p->tex_n[i] = n;
    return 0;
}

/* The arrangement changed shape: a windowed (not maximized) window takes the
 * new shape at about the same area, within the desktop work area.  The
 * settings layer learns the new size at once (the SIZE_CHANGED event would
 * tell it later). */
static void reshape_window(EidosPlatform *p)
{
    EidosScreenPlacement pl;
    SDL_Rect area;
    int cw = 0, ch = 0, nw, nh, mw = 0, mh = 0;
    SDL_DisplayID idx;

    if (p->display_mode != EIDOS_DISPLAY_WINDOWED ||
        (SDL_GetWindowFlags(p->window) &
         (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN))) {
        return;
    }
    idx = SDL_GetDisplayForWindow(p->window);
    if (idx != 0 && SDL_GetDisplayUsableBounds(idx, &area)) {
        mw = area.w;
        mh = area.h;
    }
    SDL_GetWindowSize(p->window, &cw, &ch);
    place(p, &pl);
    eidos_window_reshape(cw, ch, &pl, mw, mh, &nw, &nh);
    if (nw != cw || nh != ch) {
        SDL_SetWindowSize(p->window, nw, nh);
        eidos_settings_on_resize(p, nw, nh);
    }
}

int eidos_platform_display_mode(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->display_mode;
}

static int set_display_now(EidosPlatform *p, int mode, int win_w,
                               int win_h)
{
    u32 w, h;
    int was_full, rc = 0;

    if (p == NULL || p->window == NULL) {
        return -1;
    }
    was_full = p->display_mode != EIDOS_DISPLAY_WINDOWED;
    layout_size(p, &w, &h);

    if (mode == EIDOS_DISPLAY_FULLSCREEN || mode == EIDOS_DISPLAY_BORDERLESS) {
        /* Desktop-resolution fullscreen for both: with no fullscreen mode set
         * (SDL_SetWindowFullscreenMode's default) SDL never switches the OS
         * video mode, and the presenter scales the picture. */
        if (!was_full) {
            SDL_GetWindowPosition(p->window, &p->win_x, &p->win_y);
        }
        (void)SDL_SetWindowFullscreenMode(p->window, NULL);
        if (!SDL_SetWindowFullscreen(p->window, true)) {
            fprintf(stderr, "eidos: fullscreen refused (%s)\n",
                    SDL_GetError());
            rc = -1;
        } else {
            p->display_mode = mode;
        }
    } else {
        int cw = 0, ch = 0;

        SDL_SetWindowFullscreen(p->window, false);
        SDL_SetWindowBordered(p->window, true);
        SDL_GetWindowSize(p->window, &cw, &ch);
        if (win_w > 0 && win_h > 0) {
            if (was_full || win_w != cw || win_h != ch) {
                SDL_SetWindowSize(p->window, win_w, win_h);
            }
        } else if (was_full) {
            int dw, dh;

            default_window_size(p, &dw, &dh);
            SDL_SetWindowSize(p->window, dw, dh);
        }
        if (was_full) {
            SDL_SetWindowPosition(p->window, p->win_x, p->win_y);
        }
        p->display_mode = EIDOS_DISPLAY_WINDOWED;
    }
    if (getenv("EIDOS_WINDOW_HIDDEN") != NULL || eidos_embed_on()) {
        SDL_HideWindow(p->window);      /* a mode change shows the window on Windows */
    }
    if (p->presented) {
        render(p);
    }
    return rc;
}

static int set_layout_now(EidosPlatform *p, int layout, int swap, int gap)
{
    u32 nw, nh;

    if (p == NULL) {
        return -1;
    }
    p->layout = layout == EIDOS_LAYOUT_SIDE_BY_SIDE
                ? EIDOS_LAYOUT_SIDE_BY_SIDE
                : layout == EIDOS_LAYOUT_ONE ? EIDOS_LAYOUT_ONE
                                             : EIDOS_LAYOUT_STACKED;
    p->swap = swap != 0;
    p->gap = gap > 0 ? gap : 0;
    layout_size(p, &nw, &nh);
    /*
     * The window takes the arrangement's shape, and the arrangement is the
     * two screens and the run-status panel together.  The size it was last
     * shaped for is the one to compare with, so a panel that appeared or
     * went away -- which changes the arrangement without changing the
     * layout -- reshapes the window too.
     */
    if (p->window != NULL &&
        ((int)nw != p->arr_w || (int)nh != p->arr_h)) {
        reshape_window(p);
    }
    p->arr_w = (int)nw;
    p->arr_h = (int)nh;
    if (p->presented) {
        render(p);
    }
    return 0;
}

/* The window and the GL context belong to the thread that made them (the
 * frame thread).  The settings screen runs on a game thread, so a change it
 * asks for is stored and applied by the owner in eidos_platform_pump. */
int eidos_platform_set_display(EidosPlatform *p, int mode, int win_w,
                               int win_h)
{
    if (p == NULL || p->window == NULL) {
        return -1;
    }
    if (p->owner != 0 && SDL_GetCurrentThreadID() != p->owner) {
        p->pd_mode = mode;
        p->pd_w = win_w;
        p->pd_h = win_h;
        p->pend_display = 1;
        return 0;
    }
    return set_display_now(p, mode, win_w, win_h);
}

int eidos_platform_set_layout(EidosPlatform *p, int layout, int swap, int gap)
{
    if (p == NULL) {
        return -1;
    }
    if (p->owner != 0 && SDL_GetCurrentThreadID() != p->owner) {
        p->pl_layout = layout;
        p->pl_swap = swap;
        p->pl_gap = gap;
        p->pend_layout = 1;
        return 0;
    }
    return set_layout_now(p, layout, swap, gap);
}

int eidos_platform_set_bindings(EidosPlatform *p, const EidosKeyBinding *keys,
                                u32 nkeys, const EidosPadBinding *pads,
                                u32 npads)
{
    EidosKeyBinding *k = NULL;
    EidosPadBinding *g = NULL;

    if (p == NULL) {
        return -1;
    }
    /* One extra element so an empty table is still a valid, non-NULL one. */
    k = (EidosKeyBinding *)calloc(nkeys + 1u, sizeof(*k));
    g = (EidosPadBinding *)calloc(npads + 1u, sizeof(*g));
    if (k == NULL || g == NULL) {
        free(k);
        free(g);
        return -1;
    }
    if (nkeys > 0) {
        memcpy(k, keys, nkeys * sizeof(*k));
    }
    if (npads > 0) {
        memcpy(g, pads, npads * sizeof(*g));
    }
    free(p->keymap_own);
    free(p->padmap_own);
    p->keymap_own = k;
    p->padmap_own = g;
    p->keymap = k;
    p->keymap_count = nkeys;
    p->padmap = g;
    p->padmap_count = npads;
    note_face_default(p);
    return 0;
}

int eidos_platform_init(EidosPlatform **out, const EidosPlatformConfig *cfg)
{
    EidosPlatformConfig defaults;
    EidosPlatform *p;
    u32 w, h;
    int init_w, init_h;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (cfg == NULL) {
        memset(&defaults, 0, sizeof(defaults));
        cfg = &defaults;
    }

    p = (EidosPlatform *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return -1;
    }
    p->owner = SDL_GetCurrentThreadID();
    p->tex_n[0] = p->tex_n[1] = 1;
    p->layout = cfg->layout == EIDOS_LAYOUT_SIDE_BY_SIDE
                ? EIDOS_LAYOUT_SIDE_BY_SIDE
                : cfg->layout == EIDOS_LAYOUT_ONE ? EIDOS_LAYOUT_ONE
                                                  : EIDOS_LAYOUT_STACKED;
    p->gap = cfg->gap > 0 ? cfg->gap : 0;
    p->swap = cfg->swap_screens != 0;
    p->volume = 100;
    p->swallow_sc = -1;
    if (cfg->keymap != NULL && cfg->keymap_count > 0) {
        p->keymap = cfg->keymap;
        p->keymap_count = cfg->keymap_count;
    } else {
        p->keymap = eidos_default_keymap(&p->keymap_count);
    }
    if (cfg->padmap != NULL && cfg->padmap_count > 0) {
        p->padmap = cfg->padmap;
        p->padmap_count = cfg->padmap_count;
    } else {
        p->padmap = eidos_default_padmap(&p->padmap_count);
    }
    note_face_default(p);

    SDL_SetMainReady();
    /*
     * SDL3 declares per-monitor DPI awareness itself, so the system never
     * stretches the finished window bitmap (which would blur the sharp
     * picture the presenter draws); the presenter draws at the window's
     * real pixels (SDL_GetWindowSizeInPixels).
     */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "eidos: SDL_Init failed: %s\n", SDL_GetError());
        free(p);
        return -1;
    }

    layout_size(p, &w, &h);
    p->arr_w = (int)w;
    p->arr_h = (int)h;
    /* The window opens at the size the player left it at, when that size
     * is one for this layout (at least the DS's own size for it) and fits
     * the desktop; otherwise at the default for the layout.  A maximized
     * size is never stored (the maximized state is, cfg->maximized), and a
     * size from another arrangement or a larger screen is not reused. */
    default_window_size(p, &init_w, &init_h);
    if (cfg->win_w >= (int)w && cfg->win_h >= (int)h) {
        SDL_Rect area;

        if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area)
            || (cfg->win_w <= area.w && cfg->win_h <= area.h)) {
            init_w = cfg->win_w;
            init_h = cfg->win_h;
        }
    }
    {
        /* For checks only: EIDOS_WINDOW_SIZE=WxH opens the window at that size. */
        const char *ws = getenv("EIDOS_WINDOW_SIZE");
        int ew = 0, eh = 0;

        if (ws != NULL && sscanf(ws, "%dx%d", &ew, &eh) == 2 && ew > 0 && eh > 0) {
            init_w = ew;
            init_h = eh;
        }
    }

    /*
     * The GPU presenter is the default: the CPU hands over small images and
     * the GPU scales them.  SDL's renderer is the fallback (no OpenGL 3.3, the
     * dummy video driver the headless tests use, or EIDOS_RENDERER=sdl).
     */
    {
        const char *drv = SDL_GetCurrentVideoDriver();
        const char *want = getenv("EIDOS_RENDERER");
        int try_gl = !(drv != NULL && strcmp(drv, "dummy") == 0) &&
                     !(want != NULL && strcmp(want, "sdl") == 0);
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY;
        const char *title = cfg->title != NULL ? cfg->title : "eidos";

        if (getenv("EIDOS_WINDOW_HIDDEN") != NULL || eidos_embed_on()) {
            /* For unattended checks, or shown inside another program
             * (plat_embed.c): a real window system and GPU, no window on
             * the desktop. */
            flags |= SDL_WINDOW_HIDDEN;
        } else if (cfg->maximized
                   && cfg->display_mode == EIDOS_DISPLAY_WINDOWED) {
            flags |= SDL_WINDOW_MAXIMIZED;   /* as the player left it */
        }

        if (try_gl) {
            p->window = SDL_CreateWindow(title, init_w, init_h,
                                         flags | SDL_WINDOW_OPENGL);
            if (p->window != NULL) {
                p->gl = eidos_gl_create(p->window);
                if (p->gl == NULL) {
                    SDL_DestroyWindow(p->window);   /* an OpenGL window cannot take SDL's renderer back */
                    p->window = NULL;
                }
            }
        }
        if (p->window == NULL) {
            p->window = SDL_CreateWindow(title, init_w, init_h, flags);
        }
        if (p->window != NULL) {
            SDL_SetWindowPosition(p->window, SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED);
        }
    }
    if (getenv("EIDOS_RENDERER_REPORT") != NULL) {
        fprintf(stderr, "eidos: presenter %s (video driver %s)\n",
                p->gl != NULL ? "OpenGL 3.3" : "SDL renderer",
                SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver()
                                                    : "none");
    }
    if (p->window == NULL) {
        fprintf(stderr, "eidos: SDL_CreateWindow failed: %s\n", SDL_GetError());
        eidos_platform_quit(p);
        return -1;
    }
    {
        /* The window's icon (the executable's own on Windows comes from
         * assets/eidos.rc; this one is for every platform's title bar and
         * task switcher). */
        SDL_Surface *icon = SDL_CreateSurfaceFrom(
            EIDOS_ICON_SIDE, EIDOS_ICON_SIDE, SDL_PIXELFORMAT_RGBA32,
            (void *)k_eidos_icon_rgba, EIDOS_ICON_SIDE * 4);

        if (icon != NULL) {
            SDL_SetWindowIcon(p->window, icon);
            SDL_DestroySurface(icon);
        }
    }

    /*
     * No vsync on the renderer: the pacer above is the timebase, and a
     * 60.000 Hz or 144 Hz display would otherwise pull the game off the DS's
     * 59.8261 Hz.  Letting SDL pick the driver keeps the dummy video driver
     * (SDL_VIDEODRIVER=dummy, which the headless test uses) working, where
     * it falls back to the software renderer.
     */
    if (p->gl == NULL) {
        p->renderer = SDL_CreateRenderer(p->window, NULL);
        if (p->renderer == NULL) {
            fprintf(stderr, "eidos: SDL_CreateRenderer failed: %s\n",
                    SDL_GetError());
            eidos_platform_quit(p);
            return -1;
        }
        SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
    }

    if (frame_alloc(p, 0, 1) != 0 || frame_alloc(p, 1, 1) != 0) {
        eidos_platform_quit(p);
        return -1;
    }

    p->perf_freq = SDL_GetPerformanceFrequency();
    if (p->perf_freq == 0) {
        p->perf_freq = 1000000000ull;
    }
    p->perf_start = SDL_GetPerformanceCounter();

    /* Gamepads.  SDL3 names the face buttons by position on every pad,
     * Nintendo pads too (the default table is positional).  A machine where
     * the subsystem cannot start still runs, on the keyboard.  Pads already
     * plugged in arrive as SDL_EVENT_GAMEPAD_ADDED events at the first
     * pump. */
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        p->pad_subsystem = 1;
    } else {
        fprintf(stderr, "eidos: no gamepad support (%s); keyboard only\n",
                SDL_GetError());
    }

    /* A hidden window is a test run: it stays silent (EIDOS_NO_AUDIO does
     * the same for a visible one). */
    if (cfg->audio && getenv("EIDOS_WINDOW_HIDDEN") == NULL
        && getenv("EIDOS_NO_AUDIO") == NULL) {
        p->audio_push = cfg->audio_push;
        open_audio(p);
    }

    /* The requested mode, through the same path as a runtime change. */
    if (cfg->display_mode != EIDOS_DISPLAY_WINDOWED) {
        (void)eidos_platform_set_display(p, cfg->display_mode, cfg->win_w,
                                         cfg->win_h);
    }

    *out = p;
    return 0;
}

void eidos_platform_quit(EidosPlatform *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    if (p->audio_dev != NULL) {
        SDL_DestroyAudioStream(p->audio_dev);
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] != NULL) {
            SDL_CloseGamepad(p->pads[i]);
        }
    }
    for (i = 0; i < 2; i++) {
        if (p->tex[i] != NULL) {
            SDL_DestroyTexture(p->tex[i]);
        }
        if (p->pre[i] != NULL) {
            SDL_DestroyTexture(p->pre[i]);
        }
        free(p->shadow[i]);
    }
    if (p->panel_tex != NULL) {
        SDL_DestroyTexture(p->panel_tex);
    }
    if (p->renderer != NULL) {
        SDL_DestroyRenderer(p->renderer);
    }
    eidos_gl_destroy(p->gl);
    if (p->window != NULL) {
        SDL_DestroyWindow(p->window);
    }
    eidos_settings_on_quit(p);
    free(p->keymap_own);
    free(p->padmap_own);
    free(p);
    SDL_Quit();
}

int eidos_platform_pump(EidosPlatform *p)
{
    SDL_Event ev;

    if (p == NULL) {
        return 0;
    }
    if (p->pend_display) {
        p->pend_display = 0;
        (void)set_display_now(p, p->pd_mode, p->pd_w, p->pd_h);
    }
    if (p->pend_layout) {
        p->pend_layout = 0;
        (void)set_layout_now(p, p->pl_layout, p->pl_swap, p->pl_gap);
    }
    p->m_pressed = p->m_released = p->m_rpressed = p->m_wheel = 0;
    if (eidos_embed_on()) {
        /* shown inside another program: its mouse, its size, its end */
        int ew, eh, cw = 0, ch = 0;

        eidos_embed_edges(&p->m_pressed, &p->m_released, &p->m_rpressed, &p->m_wheel);
        if (eidos_embed_want_size(&ew, &eh)) {
            SDL_GetWindowSize(p->window, &cw, &ch);
            if (cw != ew || ch != eh) {
                SDL_SetWindowSize(p->window, ew, eh);
            }
        }
        if (eidos_embed_quit()) {
            p->quit = 1;
        }
    }
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            p->quit = 1;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            p->pad_last = 0;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                p->m_pressed = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                p->m_rpressed = 1;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                p->m_released = 1;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            p->m_wheel += ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                          ? -ev.wheel.integer_y : ev.wheel.integer_y;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (p->text_mode) {
                size_t n = strlen(ev.text.text);

                if (p->text_len + (int)n < (int)sizeof(p->text)) {
                    memcpy(p->text + p->text_len, ev.text.text, n);
                    p->text_len += (int)n;
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            p->pad_last = 0;
            if (p->text_mode && !(ev.key.mod & SDL_KMOD_ALT) &&
                (ev.key.scancode == SDL_SCANCODE_BACKSPACE ||
                 ev.key.scancode == SDL_SCANCODE_RETURN ||
                 ev.key.scancode == SDL_SCANCODE_KP_ENTER)) {
                if (p->text_len + 1 < (int)sizeof(p->text)) {
                    p->text[p->text_len++] =
                        ev.key.scancode == SDL_SCANCODE_BACKSPACE ? '\b' : '\n';
                }
                break;
            }
            if (ev.key.repeat) {
                break;
            }
            if (ev.key.scancode == SDL_SCANCODE_RETURN &&
                (ev.key.mod & SDL_KMOD_ALT)) {
                /* Alt+Enter: windowed <-> borderless.  The Return that
                 * did it is not a Start press. */
                p->swallow_sc = SDL_SCANCODE_RETURN;
                if (!eidos_settings_on_toggle(p)) {
                    (void)eidos_platform_set_display(
                        p, p->display_mode != EIDOS_DISPLAY_WINDOWED
                               ? EIDOS_DISPLAY_WINDOWED
                               : EIDOS_DISPLAY_BORDERLESS,
                        0, 0);
                }
            } else if (p->cap_state == EIDOS_CAPTURE_PENDING &&
                       ev.key.scancode != SDL_SCANCODE_F12) {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                    p->cap_state = EIDOS_CAPTURE_CANCELLED;
                } else {
                    p->cap.scancode = (int)ev.key.scancode;
                    p->cap_state = EIDOS_CAPTURE_KEY;
                }
            } else if (ev.key.scancode == SDL_SCANCODE_F12) {
                /* Escape does nothing here: a stray press closed the game.
                 * The window's close button quits. */
                p->shot_request = 1;
            }
            break;
        case SDL_EVENT_KEY_UP:
            if ((int)ev.key.scancode == p->swallow_sc) {
                p->swallow_sc = -1;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            p->pad_last = 1;
            p->pad_type = (int)SDL_GetGamepadTypeForID(ev.gbutton.which);
            if (p->cap_state == EIDOS_CAPTURE_PENDING) {
                p->cap.pad_kind = EIDOS_PADBIND_BUTTON;
                p->cap.pad_index = ev.gbutton.button;
                p->cap_state = EIDOS_CAPTURE_PAD;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (p->cap_state == EIDOS_CAPTURE_PENDING &&
                (ev.gaxis.value >= PAD_AXIS_THRESHOLD ||
                 ev.gaxis.value <= -PAD_AXIS_THRESHOLD)) {
                p->cap.pad_kind = ev.gaxis.value > 0 ? EIDOS_PADBIND_AXIS_POS
                                                     : EIDOS_PADBIND_AXIS_NEG;
                p->cap.pad_index = ev.gaxis.axis;
                p->cap_state = EIDOS_CAPTURE_PAD;
            }
            break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            if ((ev.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                 ev.type == SDL_EVENT_WINDOW_RESTORED) &&
                p->display_mode == EIDOS_DISPLAY_WINDOWED &&
                !(SDL_GetWindowFlags(p->window) & SDL_WINDOW_MINIMIZED)) {
                /* Remembered for the next run (a restore from minimized
                 * leaves the window as it was). */
                eidos_settings_on_maximize(
                    p, (SDL_GetWindowFlags(p->window) & SDL_WINDOW_MAXIMIZED) != 0);
            }
            if (ev.type == SDL_EVENT_WINDOW_RESIZED &&
                p->display_mode == EIDOS_DISPLAY_WINDOWED &&
                !(SDL_GetWindowFlags(p->window) &
                  (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED |
                   SDL_WINDOW_MAXIMIZED))) {
                /* A maximized size is the desktop's, not the player's: it is
                 * never remembered (it would reopen in the wrong shape). */
                int cw = 0, ch = 0;

                SDL_GetWindowSize(p->window, &cw, &ch);
                eidos_settings_on_resize(p, cw, ch);
            }
            if (p->presented &&
                (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                 ev.type == SDL_EVENT_WINDOW_EXPOSED)) {
                render(p);
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (p->pad_subsystem) {
                pad_open(p, ev.gdevice.which);
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            pad_close(p, ev.gdevice.which);
            break;
        default:
            break;
        }
    }

    p->buttons_prev = p->buttons;
    p->buttons = p->cap_state == EIDOS_CAPTURE_NONE
                 ? read_keyboard(p) | read_pads(p) : 0u;
    p->buttons_pressed = p->buttons & ~p->buttons_prev;
    read_stylus(p);

    /* The port's input layer takes it from here (sdk/input.h): it reaches
     * KEYINPUT, the ARM7's XY halfword and the touch panel at the next
     * frame boundary. */
    {
        BwInputState in;

        memset(&in, 0, sizeof(in));
        in.buttons = p->buttons;
        in.touch = p->touch_down;
        in.touch_x = p->touch_x;
        in.touch_y = p->touch_y;
        bw_input_set_host(&in);
    }
    eidos_settings_idle(p, (u32)SDL_GetTicks());
    return p->quit ? 0 : 1;
}

u32 eidos_platform_buttons(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->buttons;
}

u32 eidos_platform_buttons_pressed(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->buttons_pressed;
}

int eidos_platform_take_screenshot_request(EidosPlatform *p)
{
    int req;

    if (p == NULL) {
        return 0;
    }
    req = p->shot_request;
    p->shot_request = 0;
    return req;
}

void eidos_platform_present(EidosPlatform *p, const u8 *top, const u8 *bottom)
{
    eidos_platform_present_n(p, top, bottom, 1);
}

int eidos_platform_frame_scale(const EidosPlatform *p)
{
    return p == NULL ? 1 : (p->tex_n[0] > p->tex_n[1] ? p->tex_n[0] : p->tex_n[1]);
}

void eidos_platform_present_n(EidosPlatform *p, const u8 *top,
                              const u8 *bottom, int n)
{
    eidos_platform_present_nn(p, top, n, bottom, n);
}

void eidos_platform_present_nn(EidosPlatform *p, const u8 *top, int n_top,
                               const u8 *bottom, int n_bottom)
{
    const u8 *src[2];
    int nn[2];
    int i;

    if (p == NULL) {
        return;
    }
    src[0] = top;
    src[1] = bottom;
    nn[0] = n_top;
    nn[1] = n_bottom;
    for (i = 0; i < 2; i++) {
        size_t bytes;

        if (nn[i] < 1) {
            nn[i] = 1;
        }
        if (nn[i] > 32) {
            nn[i] = 32;
        }
        if (nn[i] != p->tex_n[i] && frame_alloc(p, i, nn[i]) != 0) {
            (void)frame_alloc(p, i, 1);
            return;
        }
        bytes = (size_t)SCREEN_W * (size_t)nn[i] * SCREEN_H * (size_t)nn[i] * 4u;
        if (src[i] != NULL) {
            memcpy(p->shadow[i], src[i], bytes);
        }
        if (p->gl != NULL) {
            eidos_gl_upload(p->gl, i, p->shadow[i], (int)SCREEN_W * nn[i],
                            (int)SCREEN_H * nn[i]);
        } else {
            SDL_UpdateTexture(p->tex[i], NULL, p->shadow[i],
                              (int)SCREEN_W * nn[i] * 4);
        }
    }

    /*
     * The arrangement is the two screens and the run-status panel, so the
     * panel's space is part of the window's shape: a panel that appears
     * (or goes away) changes the arrangement, and the window takes the
     * new shape at the same area, the way a layout change does.
     */
    {
        EidosScreenPlacement pl;

        place(p, &pl);
        if (pl.width != p->arr_w || pl.height != p->arr_h) {
            p->arr_w = pl.width;
            p->arr_h = pl.height;
            reshape_window(p);
        }
    }

    p->presented = 1;
    render(p);
}

int eidos_platform_gpu3d(const EidosPlatform *p)
{
    return p != NULL && p->gl != NULL && eidos_gl_has3d(p->gl);
}

int eidos_platform_read3d_1x(EidosPlatform *p, const struct G3dSnapshot *snap,
                             u8 *out, int async)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    return eidos_gl_read3d_1x(p->gl, snap, out, async);
}

void eidos_platform_set_3d_zero(EidosPlatform *p, const u8 *zero)
{
    if (eidos_platform_gpu3d(p)) {
        eidos_gl_set_3d_zero(p->gl, zero, (int)SCREEN_W, (int)SCREEN_H);
    }
}

int eidos_platform_set_3d(EidosPlatform *p, int screen, const u8 *behind,
                          const struct G3dSnapshot *snap, int n, int bmode,
                          float bfac, int dilate)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    if (screen < 0 || behind == NULL || snap == NULL) {
        eidos_gl_set_3d(p->gl, -1, NULL, 0, 0, 0, 0.f, 0);
        return 0;
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 20) {
        n = 20;
    }
    {
        int rw = (int)SCREEN_W * n, rh = (int)SCREEN_H * n;

        /* The world view: this screen's 3D is rendered at the window's
         * shape and size, one to one with what is shown. */
        if (world_active(p) && screen == p->ui_view.world_screen) {
            int ow, oh;

            out_size(p, &ow, &oh);
            if (p->ui_view.ds_only) {
                /* the DS's own frame: the 3D is the DS rectangle's size */
                EidosWorldGeometry wg;

                eidos_world_geometry(ow, oh, p->ui_view.zoom, &wg);
                ow = wg.ds.w;
                oh = wg.ds.h;
            }
            if (ow > 0 && oh > 0 && ow <= 8192 && oh <= 8192) {
                rw = ow;
                rh = oh;
            }
        }
        if (eidos_gl_render3d(p->gl, snap, rw, rh) != 0) {
            eidos_gl_set_3d(p->gl, -1, NULL, 0, 0, 0, 0.f, 0);
            return -1;
        }
    }
    eidos_gl_set_3d(p->gl, screen, behind, (int)SCREEN_W, (int)SCREEN_H, bmode,
                    bfac, dilate);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The one-screen layout                                               */
/* ------------------------------------------------------------------ */

int eidos_platform_layout(const EidosPlatform *p)
{
    return p == NULL ? EIDOS_LAYOUT_STACKED : p->layout;
}

void eidos_platform_set_view(EidosPlatform *p, const struct EidosUiView *v)
{
    if (p == NULL) {
        return;
    }
    if (v == NULL) {
        memset(&p->ui_view, 0, sizeof(p->ui_view));
        p->ui_view.zoom = 1.0f;
    } else {
        p->ui_view = *v;
        if (p->ui_view.zoom <= 0.0f) {
            p->ui_view.zoom = 1.0f;
        }
    }
}

void eidos_platform_set_ui_canvas(EidosPlatform *p, const struct EidosUiCanvas *c)
{
    if (p == NULL || p->gl == NULL) {
        return;
    }
    if (p->layout != EIDOS_LAYOUT_ONE || c == NULL || c->rgba == NULL ||
        c->w <= 0 || c->h <= 0 || c->k <= 0) {
        eidos_gl_one_canvas(p->gl, NULL, 0, 0, NULL);
        p->ui_cw = p->ui_ch = p->ui_ck = 0;
        return;
    }
    {
        SDL_Rect r;
        const u8 *up = NULL;

        r.x = 0;
        r.y = 0;
        r.w = c->w * c->k;
        r.h = c->h * c->k;
        if (!p->ui_gen_ok || p->ui_gen != c->generation || p->ui_cw != c->w ||
            p->ui_ch != c->h) {
            up = c->rgba;                       /* only when it changed */
            p->ui_gen = c->generation;
            p->ui_gen_ok = 1;
        }
        p->ui_cw = c->w;
        p->ui_ch = c->h;
        p->ui_ck = c->k;
        eidos_gl_one_canvas(p->gl, up, c->w, c->h, &r);
    }
}

int eidos_platform_world_active(const EidosPlatform *p)
{
    return world_active(p);
}

void eidos_platform_view_scale(const EidosPlatform *p, s32 *kx, s32 *ky)
{
    EidosWorldGeometry wg;
    int ow = 0, oh = 0;

    wg.kx = wg.ky = 4096;
    if (world_active(p) && !p->ui_view.ds_only && !p->ui_view.stretch) {
        out_size(p, &ow, &oh);
        eidos_world_geometry(ow, oh, p->ui_view.zoom, &wg);
    }
    if (kx != NULL) {
        *kx = wg.kx;
    }
    if (ky != NULL) {
        *ky = wg.ky;
    }
}

int eidos_platform_mouse(const EidosPlatform *p, struct EidosUiMouse *m)
{
    int mx = 0, my = 0, ww = 0, wh = 0, ow = 0, oh = 0;
    float fx = 0.0f, fy = 0.0f;
    SDL_MouseButtonFlags buttons;

    if (m == NULL) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    m->x = m->y = -1;
    if (p == NULL || p->window == NULL) {
        return 0;
    }
    m->pressed = p->m_pressed;
    m->released = p->m_released;
    m->right_pressed = p->m_rpressed;
    m->wheel = p->m_wheel;
    buttons = eidos_embed_on() ? (SDL_MouseButtonFlags)eidos_embed_mouse(&fx, &fy) : SDL_GetMouseState(&fx, &fy);
    mx = (int)fx;
    my = (int)fy;
    m->down = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    if (!eidos_embed_on() && SDL_GetMouseFocus() != p->window) {
        return 0;
    }
    SDL_GetWindowSize(p->window, &ww, &wh);
    out_size(p, &ow, &oh);
    if (ww > 0 && wh > 0 && ow > 0 && oh > 0) {
        mx = (int)((s64)mx * ow / ww);
        my = (int)((s64)my * oh / wh);
    }
    if (mx >= 0 && my >= 0 && mx < (ow > 0 ? ow : ww) &&
        my < (oh > 0 ? oh : wh)) {
        m->x = mx;
        m->y = my;
    }
    return 1;
}

int eidos_platform_pad_last(const EidosPlatform *p)
{
    return p != NULL && p->pad_last;
}

/* A short, readable name for a key (the arrows as arrows). */
static const char *key_short_name(int sc)
{
    switch (sc) {
    case SDL_SCANCODE_UP:        return "\xE2\x86\x91";
    case SDL_SCANCODE_DOWN:      return "\xE2\x86\x93";
    case SDL_SCANCODE_LEFT:      return "\xE2\x86\x90";
    case SDL_SCANCODE_RIGHT:     return "\xE2\x86\x92";
    case SDL_SCANCODE_RETURN:    return "Enter";
    case SDL_SCANCODE_BACKSPACE: return "Bksp";
    case SDL_SCANCODE_RSHIFT:    return "RShift";
    case SDL_SCANCODE_LSHIFT:    return "Shift";
    case SDL_SCANCODE_SPACE:     return "Space";
    case SDL_SCANCODE_ESCAPE:    return "Esc";
    case SDL_SCANCODE_TAB:       return "Tab";
    default:                     return SDL_GetScancodeName((SDL_Scancode)sc);
    }
}

/* A pad control (EIDOS_PADBIND_*) as its glyph's number (engine/ui/
 * padglyph.h: SDL's button order, then the triggers and the left stick's
 * directions); -1 for a control with no glyph. */
static int pad_control(int kind, int index)
{
    if (kind == EIDOS_PADBIND_BUTTON) {
        return index >= 0 && index <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? index : -1;
    }
    switch (index) {
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return kind == EIDOS_PADBIND_AXIS_POS ? 15 : -1;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return kind == EIDOS_PADBIND_AXIS_POS ? 16 : -1;
    case SDL_GAMEPAD_AXIS_LEFTY:         return kind == EIDOS_PADBIND_AXIS_NEG ? 17 : 18;
    case SDL_GAMEPAD_AXIS_LEFTX:         return kind == EIDOS_PADBIND_AXIS_NEG ? 19 : 20;
    default:                             return -1;
    }
}

void eidos_platform_key_names(const EidosPlatform *p, const char *names[12])
{
    /* the pad: each DS button's first bound control, as a glyph token
     * (kit.h UI_PAD_TOKEN) */
    static char tok[12][3];
    u32 b, i;

    for (b = 0; b < 12; b++) {
        names[b] = NULL;
        if (p == NULL) {
            continue;
        }
        if (p->pad_last) {
            for (i = 0; i < p->padmap_count; i++) {
                EidosPadBinding pb = pad_binding_for(p, &p->padmap[i], p->pad_type);
                int ctl = pad_control(pb.kind, pb.index);

                if (p->padmap[i].button == (1u << b) && ctl >= 0) {
                    tok[b][0] = '\x1F';
                    tok[b][1] = (char)('A' + ctl);
                    tok[b][2] = '\0';
                    names[b] = tok[b];
                    break;
                }
            }
            continue;
        }
        for (i = 0; i < p->keymap_count; i++) {
            if (p->keymap[i].button == (1u << b) && p->keymap[i].scancode > 0) {
                names[b] = key_short_name(p->keymap[i].scancode);
                break;
            }
        }
    }
}

/* The family of icons for the pad in use (engine/ui/padglyph.h UI_PAD_*),
 * from the type SDL reports; Xbox One for a pad SDL cannot place. */
int eidos_platform_pad_family(const EidosPlatform *p)
{
    switch (p != NULL ? p->pad_type : SDL_GAMEPAD_TYPE_UNKNOWN) {
    case SDL_GAMEPAD_TYPE_XBOX360:                    return 0;
    case SDL_GAMEPAD_TYPE_PS3:                        return 2;
    case SDL_GAMEPAD_TYPE_PS4:                        return 3;
    case SDL_GAMEPAD_TYPE_PS5:                        return 4;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return 5;
    case SDL_GAMEPAD_TYPE_GAMECUBE:                   return 6;
    case SDL_GAMEPAD_TYPE_STEAM:                      return 7;
    default:                                          return 1;
    }
}

void eidos_platform_text_mode(EidosPlatform *p, int on)
{
    if (p == NULL || (on != 0) == p->text_mode) {
        return;
    }
    p->text_mode = on != 0;
    p->text_len = 0;
    if (on) {
        SDL_StartTextInput(p->window);
    } else {
        SDL_StopTextInput(p->window);
    }
}

int eidos_platform_take_text(EidosPlatform *p, char *out, int cap)
{
    int n;

    if (p == NULL || out == NULL || cap <= 0) {
        return 0;
    }
    n = p->text_len < cap - 1 ? p->text_len : cap - 1;
    memcpy(out, p->text, (size_t)n);
    out[n] = '\0';
    p->text_len = 0;
    return n;
}

void eidos_platform_window_size(const EidosPlatform *p, int *w, int *h)
{
    int ow = 0, oh = 0;

    if (p != NULL) {
        out_size(p, &ow, &oh);
    }
    if (w != NULL) {
        *w = ow;
    }
    if (h != NULL) {
        *h = oh;
    }
}

int eidos_platform_capture_hires(EidosPlatform *p, const u8 *a2d,
                                 const u8 *behind,
                                 const struct G3dSnapshot *snap, int n,
                                 int blend, int eva, int evb, const u8 *b1x)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 20) {
        n = 20;
    }
    return eidos_gl_capture(p->gl, a2d, behind, snap, n, blend, eva, evb, b1x);
}

void eidos_platform_show_capture(EidosPlatform *p, int screen, int bmode,
                                 float bfac)
{
    if (p != NULL && p->gl != NULL) {
        eidos_gl_show_capture(p->gl, screen, bmode, bfac);
    }
}

void eidos_platform_capture_begin(EidosPlatform *p)
{
    if (p != NULL) {
        memset(&p->cap, 0, sizeof(p->cap));
        p->cap_state = EIDOS_CAPTURE_PENDING;
    }
}

void eidos_platform_capture_cancel(EidosPlatform *p)
{
    if (p != NULL) {
        p->cap_state = EIDOS_CAPTURE_NONE;
    }
}

int eidos_platform_capture_poll(EidosPlatform *p, EidosCaptured *out)
{
    int st;

    if (p == NULL) {
        return EIDOS_CAPTURE_NONE;
    }
    st = p->cap_state;
    if ((st == EIDOS_CAPTURE_KEY || st == EIDOS_CAPTURE_PAD) && out != NULL) {
        *out = p->cap;
    }
    if (st != EIDOS_CAPTURE_PENDING) {
        p->cap_state = EIDOS_CAPTURE_NONE;
    }
    return st;
}

void eidos_platform_shot_size(const EidosPlatform *p, u32 *w, u32 *h)
{
    EidosScreenPlacement pl;
    EidosRect area;

    if (p == NULL) {
        return;
    }
    place(p, &pl);
    /* The screens, not the arrangement: the screenshot is the DS's screens
     * and the run-status panel's strip is not part of them. */
    screens_rect(&pl.screen[0], &pl.screen[1], &area);
    if (w != NULL) {
        *w = (u32)area.w * (u32)eidos_platform_frame_scale(p);
    }
    if (h != NULL) {
        *h = (u32)area.h * (u32)eidos_platform_frame_scale(p);
    }
}

/*
 * The screenshot is composed from the port's own last-uploaded buffers, not
 * read back out of the renderer: a read-back is driver-dependent (and under
 * the dummy video driver there is nothing to read back at all), while the
 * shadow copies are exactly what the engine produced.  RGB24 is chosen so
 * SDL_SaveBMP writes a plain 24-bit bottom-up BMP that a test can parse with
 * no BMP library.
 */
/* The screenshot as the window shows it, the picture's rectangle only: the
 * GPU path composes the picture on the GPU, so the CPU-side copies are not it
 * (a GPU 3D layer is a placeholder colour there). */
static int screenshot_gl(EidosPlatform *p, const char *path)
{
    EidosViewport vp;
    SDL_Rect rects[2];
    SDL_Surface *surf;
    u8 *rgb, *crop;
    EidosRect area;
    int ow, oh, i, cw, ch, cx, cy, rc;

    out_size(p, &ow, &oh);
    eidos_platform_viewport(p, &vp);
    for (i = 0; i < 2; i++) {
        rects[i].x = vp.screen[i].x;
        rects[i].y = vp.screen[i].y;
        rects[i].w = vp.screen[i].w;
        rects[i].h = vp.screen[i].h;
    }
    rgb = (u8 *)malloc((size_t)ow * (size_t)oh * 3u);
    if (rgb == NULL || eidos_gl_grab(p->gl, ow, oh, rects, rgb) != 0) {
        free(rgb);
        return -1;
    }
    /*
     * The crop is the DS's screens, not the whole arrangement: the
     * run-status panel's strip is beside them (or below them) and is not
     * part of a screenshot of the screens.
     */
    screens_rect(&vp.screen[0], &vp.screen[1], &area);
    if (p->layout == EIDOS_LAYOUT_ONE) {
        /* The one-screen layout's picture is the whole window. */
        area.x = area.y = 0;
        area.w = ow;
        area.h = oh;
    }
    cx = area.x < 0 ? 0 : area.x;
    cy = area.y < 0 ? 0 : area.y;
    cw = area.w;
    ch = area.h;
    if (cx + cw > ow) {
        cw = ow - cx;
    }
    if (cy + ch > oh) {
        ch = oh - cy;
    }
    if (cw <= 0 || ch <= 0) {
        free(rgb);
        return -1;
    }
    crop = (u8 *)malloc((size_t)cw * (size_t)ch * 3u);
    if (crop == NULL) {
        free(rgb);
        return -1;
    }
    for (i = 0; i < ch; i++) {
        memcpy(crop + (size_t)i * (size_t)cw * 3u,
               rgb + ((size_t)(cy + i) * (size_t)ow + (size_t)cx) * 3u,
               (size_t)cw * 3u);
    }
    free(rgb);
    surf = SDL_CreateSurfaceFrom(cw, ch, SDL_PIXELFORMAT_RGB24, crop, cw * 3);
    if (surf == NULL) {
        free(crop);
        return -1;
    }
    rc = SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    free(crop);
    return rc ? 0 : -1;
}

int eidos_platform_screenshot(EidosPlatform *p, const char *path)
{
    SDL_Surface *surf;
    u8 *pixels;
    u32 w, h, x, y, i;
    size_t pitch;
    int rc;
    EidosScreenPlacement pl;

    if (p == NULL || path == NULL) {
        return -1;
    }
    if (p->gl != NULL && p->presented) {
        return screenshot_gl(p, path);
    }
    eidos_platform_shot_size(p, &w, &h);
    pitch = (size_t)w * 3u;
    pixels = (u8 *)calloc((size_t)h, pitch);
    if (pixels == NULL) {
        return -1;
    }

    place(p, &pl);
    {
        EidosRect area;

        /* The screens' own area: the panel's strip is not in the picture. */
        screens_rect(&pl.screen[0], &pl.screen[1], &area);
        for (i = 0; i < 2; i++) {
            /* The picture is as large as the larger screen; a screen kept at a
             * smaller multiple is repeated up to it. */
            u32 nn = (u32)eidos_platform_frame_scale(p);
            u32 own = (u32)p->tex_n[i], sw = SCREEN_W * nn, sh = SCREEN_H * nn;
            u32 ox = (u32)(pl.screen[i].x - area.x) * nn;
            u32 oy = (u32)(pl.screen[i].y - area.y) * nn;

            for (y = 0; y < sh; y++) {
                const u8 *s = p->shadow[i] +
                              (size_t)(y * own / nn) * SCREEN_W * own * 4u;
                u8 *d = pixels + (size_t)(oy + y) * pitch + (size_t)ox * 3u;
                for (x = 0; x < sw; x++) {
                    const u8 *q = s + (size_t)(x * own / nn) * 4u;

                    d[x * 3 + 0] = q[0];
                    d[x * 3 + 1] = q[1];
                    d[x * 3 + 2] = q[2];
                }
            }
        }
    }

    surf = SDL_CreateSurfaceFrom((int)w, (int)h, SDL_PIXELFORMAT_RGB24, pixels,
                                 (int)pitch);
    if (surf == NULL) {
        fprintf(stderr, "eidos: surface for the screenshot failed: %s\n",
                SDL_GetError());
        free(pixels);
        return -1;
    }
    rc = SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    free(pixels);
    if (!rc) {
        fprintf(stderr, "eidos: SDL_SaveBMP(%s) failed: %s\n", path,
                SDL_GetError());
        return -1;
    }
    return 0;
}
