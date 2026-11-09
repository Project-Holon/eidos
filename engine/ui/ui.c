/*
 * engine/ui/ui.c -- the one-screen layout's frame: the scene, its recipe,
 * the hit regions and the mouse (engine/ui/ui.h).
 *
 * Immediate mode
 * --------------
 *
 * A recipe draws its interface every frame and asks for each control's hit
 * region while it draws it (eidos_ui_hit).  The answer comes from this
 * frame's mouse: over the region is a hover; a click is a press that started
 * over the region and ended over it.  Nothing is remembered between frames
 * but the point where the button went down, so a recipe whose controls move
 * (a list that scrolls) never sees a click land on the wrong row.
 *
 * Input to the game
 * -----------------
 *
 * A tap is the input layer's script stylus (sdk/input.h): the game sees the
 * stylus at the position from the next frame, for as many frames as asked.
 * A button is the input layer's injection: ORed into the player's own keys,
 * so the player's keyboard and pad are never disturbed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "theme.h"
#include "dsmem.h"
#include "input.h"
#include "textbox.h"
#include "settings.h"
#include "overlay.h"

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static int g_enabled;
static EidosUiCanvas g_canvas;
static EidosUiCtx g_ctx;
static const char *g_scene = "fallback";
static const EidosUiRecipe *g_last;

/* This frame's mouse, in canvas pixels. */
static int g_mx = -1, g_my = -1, g_down, g_pressed, g_released, g_wheel;
/* The mouse highlights only while it is the device in use: it moved,
 * clicked or scrolled since the last key.  A key hands the highlight back
 * to the keys' cursor, so the two never show different things. */
static int g_mouse_live, g_last_mx = -1, g_last_my = -1;
static int g_down_x = -1, g_down_y = -1;     /* where the button went down */
static int g_right;                          /* right button this frame    */
static int g_want_text, g_text_wanted;       /* typing asked this frame    */
static int g_want_objs, g_objs_wanted;       /* OBJ layers asked (bits)    */
static u32 g_keys_prev, g_keys_now, g_keys_edge, g_take;   /* the player's keys    */
static int g_k = 2;

int eidos_ui_enabled(void)
{
    return g_enabled;
}

extern void g2d_hw_hide_main_bgs(u32 mask) __attribute__((weak));
extern void bw_ui_text_set_own(int on) __attribute__((weak));

void eidos_ui_set_enabled(int on)
{
    if (on != g_enabled) {
        g_enabled = on;
        /* the game draws its own text windows again (engine/ui/textbox.c) */
        if (g2d_hw_hide_main_bgs != NULL) {
            g2d_hw_hide_main_bgs(0);
        }
        if (bw_ui_text_set_own != NULL) {
            bw_ui_text_set_own(0);
        }
        eidos_ui_reset();
        bw_input_set_host_mask(0);      /* every button the game's again */
    }
}

int eidos_ui_scale(int win_w, int win_h)
{
    /* the layouts need about 270 canvas rows (the menu's six buttons, the
     * battle's plates and sheets) and about 360 columns (a 4:3 window's
     * width at that height): the largest whole scale that keeps both, so a
     * window narrower than 4:3 (a tablet held upright) gets a smaller scale
     * instead of panels cut off at its sides */
    int k = win_h / 270;

    if (win_w / 360 < k) {
        k = win_w / 360;
    }
    return k < 1 ? 1 : k;
}

const char *eidos_ui_scene_name(void)
{
    return g_scene;
}

void eidos_ui_reset(void)
{
    u32 i;

    for (i = 0; i < eidos_ui_group_count; i++) {
        const EidosUiRecipeGroup *g = &eidos_ui_groups[i];
        u32 j;

        for (j = 0; j < *g->count; j++) {
            if (g->recipes[j]->reset != NULL) {
                g->recipes[j]->reset();
            }
        }
    }
    g_last = NULL;
    g_scene = "fallback";
    g_down_x = g_down_y = -1;
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

/*
 * The game's own touch / keys mode (GFL UI): after a touch the game hides
 * its key cursor and spends the next key press on showing it again.  The
 * one-screen layout mixes the mouse and the keys freely, so once a tap is
 * over the game goes back to keys mode: the next key moves the cursor from
 * where the tap left it.
 */
extern u8   UI_GetInputMode(void) __attribute__((weak));
extern void UI_SetInputMode(u32 value) __attribute__((weak));
static u32 g_tap_end;

void eidos_ui_tap(int x, int y, u32 frames)
{
    if (x < 0 || y < 0 || x >= BW_TOUCH_WIDTH || y >= BW_TOUCH_HEIGHT) {
        return;
    }
    g_tap_end = bw_input_frame_number() + (frames == 0 ? 1 : frames) + 2;
    (void)bw_input_script_touch(bw_input_frame_number(), x, y,
                                frames == 0 ? 1 : frames);
}

/* A touch dragged from (x0, y0) to (x1, y1) on the lower screen: down at
 * the start, moved there in steps of a frame or two, held and let go, for
 * the game's own drags (the summary's move swap, P_STA_SKILL_HandleTouch,
 * picks a move up once the touch moves more than 8 pixels). */
extern int bw_input_script_untouch(u32 frame);

void eidos_ui_drag(int x0, int y0, int x1, int y1)
{
    u32 f = bw_input_frame_number(), k;
    const u32 steps = 8;

    if (x0 < 0 || y0 < 0 || x0 >= BW_TOUCH_WIDTH || y0 >= BW_TOUCH_HEIGHT ||
        x1 < 0 || y1 < 0 || x1 >= BW_TOUCH_WIDTH || y1 >= BW_TOUCH_HEIGHT) {
        return;
    }
    (void)bw_input_script_touch(f, x0, y0, 0);
    for (k = 1; k <= steps; k++) {
        (void)bw_input_script_touch(f + 2 + k * 2, x0 + (x1 - x0) * (int)k / (int)steps,
                                    y0 + (y1 - y0) * (int)k / (int)steps, 0);
    }
    (void)bw_input_script_untouch(f + 2 + steps * 2 + 4);
    g_tap_end = f + 2 + steps * 2 + 6;
}

void eidos_ui_press(u32 keys, u32 frames)
{
    (void)bw_input_inject(keys & BW_KEY_GAME, frames == 0 ? 1 : frames);
}

/* ------------------------------------------------------------------ */
/* Hit regions                                                         */
/* ------------------------------------------------------------------ */

static int inside(EidosUiRect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

int eidos_ui_hit(EidosUiRect r)
{
    int s = 0;

    /* the region in canvas pixels, moved by the drawing origin */
    r.x += g_ctx.ox;
    r.y += g_ctx.oy;
    if (inside(r, g_mx, g_my)) {
        if (g_mouse_live) {
            s |= EIDOS_UI_HOVER;
        }
        if (g_released && inside(r, g_down_x, g_down_y)) {
            s |= EIDOS_UI_CLICK;
        }
    }
    return s;
}

int eidos_ui_mouse_pos(int *x, int *y)
{
    if (x != NULL) *x = g_mx < 0 ? -1 : g_mx - g_ctx.ox;
    if (y != NULL) *y = g_my < 0 ? -1 : g_my - g_ctx.oy;
    return g_mx >= 0;
}

void eidos_ui_want_text(int on)
{
    if (on) {
        g_want_text = 1;
    }
}

int eidos_ui_text_wanted(void)
{
    return g_text_wanted;
}

void eidos_ui_want_objs(int screen)
{
    if (screen == 0 || screen == 1) {
        g_want_objs |= 1 << screen;
    }
}

int eidos_ui_objs_wanted(void)
{
    return g_objs_wanted;
}

int eidos_ui_keys_lit(void)
{
    return !g_mouse_live;
}

int eidos_ui_right_click(void)
{
    return g_right;
}

int eidos_ui_pointer(int *x, int *y, int *pressed, int *released)
{
    *x = g_mx;
    *y = g_my;
    *pressed = g_pressed;
    *released = g_released;
    return g_down;
}

int eidos_ui_held(void)
{
    return g_down;
}

int eidos_ui_wheel(EidosUiRect r)
{
    r.x += g_ctx.ox;
    r.y += g_ctx.oy;
    return inside(r, g_mx, g_my) ? g_wheel : 0;
}

/* ------------------------------------------------------------------ */
/* Motion: scene changes and clicks                                    */
/* ------------------------------------------------------------------ */

/*
 * Every change of scene crossfades: the last picture of the old scene
 * fades out over the first frames of the new one while the new one fades
 * in and settles from a few pixels below.  A click leaves a short ring
 * where it landed.  Both are done here, on the finished canvas, so every
 * recipe gets them without drawing anything.
 */
#define FADE_FRAMES   10
#define SLIDE_PIXELS  0
#define RING_FRAMES   10

static u8 *g_prev;               /* the last frame's canvas               */
static int g_prev_w, g_prev_h;
static u8 *g_fade_from;          /* the old scene's last picture          */
static u8 *g_before;             /* the scene before this one, its last
                                    picture: what an app goes back to       */
static int g_before_w, g_before_h;
static const void *g_shown;      /* the last scene a recipe drew          */
static int g_fade_w, g_fade_h, g_fade_left;
static int g_ring_x = -1, g_ring_y, g_ring_age;
static u32 g_scene_frames;       /* frames since the scene began          */

static int g_intent_kind, g_intent_arg;
static u32 g_intent_left;

void eidos_ui_set_intent(int kind, int arg, u32 frames)
{
    g_intent_kind = kind;
    g_intent_arg = arg;
    g_intent_left = frames;
}

int eidos_ui_take_intent(int kind, int *arg)
{
    if (g_intent_left == 0 || g_intent_kind != kind) {
        return 0;
    }
    if (arg != NULL) {
        *arg = g_intent_arg;
    }
    g_intent_left = 0;
    return 1;
}

void eidos_ui_take_keys(u32 mask)
{
    g_take |= mask & BW_KEY_GAME;
}

u32 eidos_ui_keys_pressed(void)
{
    return g_keys_edge;
}

#define GAP_FRAMES 75
/* The game takes its own menu down a frame or two after its state moves
 * on (a YES / NO, a battle's command menu): a view that stops hiding the
 * game's 2D keeps it hidden this many frames more, so that menu does not
 * flash between the recipe's interface and the next picture. */
#define HIDE_TAIL_FRAMES 3
#define KEY_REPEAT_WAIT 16
#define KEY_REPEAT_STEP 4

/*
 * How dark the game has faded the screens (0..16): the master brightness
 * going down, or the darken effect (BLDCNT, BLDY), on either engine.
 */
static int g_lower_only;         /* this frame's view: lower screen alone */

static u32 game_darkness(void)
{
    u32 e, best = 0;
    /* POWCNT1 bit 15: the main engine is on the upper screen */
    u32 lower = (bw_io_read16(0x04000304u) & 0x8000u) ? 1u : 0u;

    for (e = 0; e < 2; e++) {
        u32 base = e == 0 ? 0x04000000u : 0x04001000u;

        if (g_lower_only && e != lower) {
            continue;               /* the upper screen is off on purpose */
        }
        u32 mb = bw_io_read16(base + 0x6Cu), f = 0;

        if (((mb >> 14) & 3u) == 2u) {
            f = mb & 0x1Fu;
        } else if (((bw_io_read16(base + 0x50u) >> 6) & 3u) == 3u) {
            f = bw_io_read16(base + 0x54u) & 0x1Fu;
        }
        if (f > 16u) {
            f = 16u;
        }
        if (f > best) {
            best = f;
        }
    }
    return best;
}

/*
 * An app over the world closes with the game's fade to black, then the
 * field loads.  Once a modal scene starts to fade out, its last picture
 * stands over the blurred world until the next scene is up, which then
 * crossfades from it: the window never goes empty in between.
 */
static int g_leaving;
/* An app that closes may open the next at once (the party list, the
 * summary to forget a move, the list again): for the first frames of a
 * close its own last picture stays, and the scene it goes back to (the
 * menu) shows only once that long has passed with no new app. */
static u32 g_leave_frame;
static int g_before_shown;          /* the scene before is on show */
#define LEAVE_OWN_FRAMES 30u

static void fade_start(void);
void eidos_ui_hold_canvas(void);

/* While an app closes: its own last picture for the first frames, then the
 * scene it goes back to (crossfading to it once).  Returns whether the
 * scene before is the one on show. */
static int leave_picture(EidosUiView *view)
{
    if (bw_input_frame_number() - g_leave_frame >= LEAVE_OWN_FRAMES && g_before != NULL
        && g_before_w == g_canvas.w && g_before_h == g_canvas.h) {
        if (!g_before_shown) {
            g_before_shown = 1;
            fade_start();
        }
        memcpy(g_canvas.rgba, g_before, (size_t)g_canvas.w * (size_t)g_canvas.h * 4);
        view->blur = 0;
        return 1;
    }
    eidos_ui_hold_canvas();
    view->blur = 1;
    return 0;
}
static u32 g_dark_prev;
static int g_holding;            /* an app's picture held through its fade */

static void fade_start(void);

void eidos_ui_crossfade(void)
{
    fade_start();
}

static int g_own_reveal;         /* the recipe draws its own reveal */

void eidos_ui_own_reveal(void)
{
    g_own_reveal = 1;
}

int eidos_ui_holding(void)
{
    return g_holding;
}

void eidos_ui_hold_canvas(void)
{
    if (g_prev != NULL && g_prev_w == g_canvas.w && g_prev_h == g_canvas.h) {
        memcpy(g_canvas.rgba, g_prev, (size_t)g_prev_w * (size_t)g_prev_h * 4);
    }
}

/* The app on top of the game system's proc stack is ending (Proc.seq 3,
 * PROCSYS_StepProc's end step): it is closing, not changing screens inside
 * itself (the Pokedex's splash to its list fades the same way). */
extern u8 *bw_dsref__GameSystemGlobal __attribute__((weak));

static int app_ending(void)
{
    const u8 *gs, *ps, *cur;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return 0;
    }
    gs = (const u8 *)(uintptr_t)*(const u32 *)bw_dsref__GameSystemGlobal;
    ps = gs != NULL ? (const u8 *)(uintptr_t)*(const u32 *)(gs + 0x08) : NULL;
    cur = ps != NULL ? (const u8 *)(uintptr_t)*(const u32 *)(ps + 0x08) : NULL;
    return cur != NULL && *(const u32 *)(cur + 0x08) == 3u;
}

/* For the log: the top proc's overlay and step, and its child's. */
static void app_state(u32 *ov, u32 *seq, u32 *child_ov, u32 *child_seq)
{
    const u8 *gs, *ps, *cur, *nx;

    *ov = *seq = *child_ov = *child_seq = 0xFFFFFFFFu;
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return;
    }
    gs = (const u8 *)(uintptr_t)*(const u32 *)bw_dsref__GameSystemGlobal;
    ps = gs != NULL ? (const u8 *)(uintptr_t)*(const u32 *)(gs + 0x08) : NULL;
    cur = ps != NULL ? (const u8 *)(uintptr_t)*(const u32 *)(ps + 0x08) : NULL;
    if (cur == NULL) {
        return;
    }
    *ov = *(const u32 *)(cur + 0x04);
    *seq = *(const u32 *)(cur + 0x08);
    nx = (const u8 *)(uintptr_t)*(const u32 *)(cur + 0x1C);
    if (nx != NULL) {
        *child_ov = *(const u32 *)(nx + 0x04);
        *child_seq = *(const u32 *)(nx + 0x08);
    }
}

static void leave_check(EidosUiView *view)
{
    u32 dark;

    g_lower_only = view->lower_only;
    dark = game_darkness();

    {
        /* the world's own screen (the main engine) only: a scene change
         * fades the bottom screen alone */
        u32 mb = bw_io_read16(0x0400006Cu), f = 0;

        if (((mb >> 14) & 3u) == 2u) {
            f = mb & 0x1Fu;
        } else if (((bw_io_read16(0x04000050u) >> 6) & 3u) == 3u) {
            f = bw_io_read16(0x04000054u) & 0x1Fu;
        }
        view->dark = (int)(f > 16u ? 16u : f);
    }
    if (getenv("EIDOS_UI_DARK_LOG") != NULL) {
        u32 ov, sq, cov, csq;

        app_state(&ov, &sq, &cov, &csq);
        fprintf(stderr, "eidos: ui %u %s dark %u blur %d leaving %d proc %d/%d child %d/%d\n",
                (unsigned)bw_input_frame_number(), g_scene, (unsigned)dark, view->blur,
                g_leaving, (int)ov, (int)sq, (int)cov, (int)csq);
    }

    int fading_out = dark > g_dark_prev || dark >= 16u;

    if (view->blur && !g_leaving && app_ending()) {
        g_leaving = 1;
        g_leave_frame = bw_input_frame_number();
        g_before_shown = 0;
    } else if (g_leaving && dark < g_dark_prev && !app_ending()) {
        g_leaving = 0;                  /* it fades back in: the same app */
        fade_start();
    }
    g_dark_prev = dark;
    if (g_leaving) {
        /* the modal's last picture stays, over the blurred world, until the
         * next scene is up and crossfades from it: no empty frames while
         * the game fades and loads */
        /* the scene the app goes back to (the menu), over the world as it
         * was, once no next app has opened (leave_picture) */
        (void)leave_picture(view);
        g_take = 0;
        view->hide_2d = 1;
        view->no_fade = 1;
        view->hold_world = 1;
    } else if (view->blur) {
        /* an app between two of its own screens: the game fades to black
         * and back; its last picture stays instead, and the next screen
         * crossfades from it, so the window never goes dark */
        if (fading_out && g_scene_frames > 0) {
            eidos_ui_hold_canvas();
            g_holding = 1;
        } else if (g_holding) {
            g_holding = 0;
            if (!g_own_reveal) {
                fade_start();
            }
        }
    } else if (!view->no_fade) {
        /* The interface fades with the game: a screen that leaves with a
         * fade to black (the title, a menu, a battle) takes the interface
         * down with it instead of dropping it at the end.  Over the world
         * the world's own screen counts. */
        u32 d = view->mode == EIDOS_VIEW_WORLD ? (u32)view->dark : (dark > 16u ? 16u : dark);

        if (d > 0u) {
            size_t i, n = (size_t)g_canvas.w * (size_t)g_canvas.h * 4;
            u32 k = 16u - d;

            for (i = 0; i < n; i += 4) {
                g_canvas.rgba[i + 0] = (u8)(g_canvas.rgba[i + 0] * k / 16u);
                g_canvas.rgba[i + 1] = (u8)(g_canvas.rgba[i + 1] * k / 16u);
                g_canvas.rgba[i + 2] = (u8)(g_canvas.rgba[i + 2] * k / 16u);
            }
        }
    }
}

extern void GFL_USE_PlaySeDefaultVolume(u32 seqNo) __attribute__((weak));

static u32 g_sound[4];
static volatile int g_sound_n;

void eidos_ui_sound(u32 seq)
{
    if (g_sound_n < 4) {
        g_sound[g_sound_n] = seq;
        g_sound_n++;
    }
}

static volatile u32 g_cry;              /* a species whose cry is queued */
extern u32 PM_VOICE_Play(u32 species, u32 form, u32 pan, u32 dual, u32 volumeBiasB,
                         u32 frequencyOffsetB, u32 reverse, void **recordedVoice)
    __attribute__((weak));

void eidos_ui_cry(u32 species)
{
    g_cry = species;
}

/* The spot editor's Preview in game (and tests): EIDOS_DEBUG_PARTY="species,species,..." and
 * EIDOS_DEBUG_LEVEL replace the party's members, once the field runs, with
 * fresh Pokemon of those species (the player's own trainer ID), on the
 * game's thread through the game's own functions.  A member may name its
 * own level and its experience: "species:level" or "species:level:-N",
 * N experience points short of the next level (-1: the next point levels
 * it up).  Members past the party's count are added.
 * EIDOS_DEBUG_ITEMS="item:count,..." puts items in the bag. */
extern void *GAME_DATA_GetMyParty(const void *gd) __attribute__((weak));
extern void *GAME_DATA_GetMyStatus(void *gd) __attribute__((weak));
extern u32 MYSTATUS_GetId(const void *ms) __attribute__((weak));
extern u32 POKEPARTY_GetCount(const void *party) __attribute__((weak));
extern void *POKEPARTY_GetMember(void *party, u32 pos) __attribute__((weak));
extern void POKE_TOOL_SetupPartyParam(void *pp, u16 species, u16 level, u64 otId) __attribute__((weak));
extern u8 *bw_dsref__GameSystemGlobal __attribute__((weak));
extern void POKE_TOOL_SetMoveSlot(void *pp, u16 move, u32 slot) __attribute__((weak));
extern void MISC_SetBadge(void *misc, u32 bit) __attribute__((weak));
extern void *GAME_DATA_GetMiscSave(void *gd) __attribute__((weak));
extern void *GAME_DATA_GetEventWork(const void *gd) __attribute__((weak));
extern void ZONEDATA_SetEventFlag(void *ev, int flagId) __attribute__((weak));
extern u32 POKE_TOOL_GetExpForLevel(u16 species, u16 form, u16 level) __attribute__((weak));
extern void POKE_TOOL_SetPartyValue(void *pp, u32 paramId, uintptr_t value) __attribute__((weak));
extern void POKE_TOOL_CalcParam(void *pp) __attribute__((weak));
extern void *POKE_TOOL_Create(u16 species, u16 level, u64 otId, u16 heapId) __attribute__((weak));
extern u32 POKEPARTY_Add(void *party, const void *member) __attribute__((weak));
extern u32 POKEPARTY_RemoveMember(void *party, s32 pos) __attribute__((weak));
extern void *GAME_DATA_GetMyItem(const void *gd) __attribute__((weak));
extern void GAME_DATA_SetSubscreenType(void *gd, u32 type) __attribute__((weak));
extern void *BLOCK20_SAVEDATA_GetBlockFromSys(void *gd) __attribute__((weak));
extern void CGEAR_SAVE_SetObtained(void *cgearSave, u8 value) __attribute__((weak));
extern u32 MYITEM_SAVEDATA_AddItem(void *bag, u16 itemId, u16 quantity, u16 heapId) __attribute__((weak));
#define DEBUG_PARAM_EXP 0x08u           /* POKE_TOOL_PARAM_EXP                  */
#define DEBUG_FIELD_HEAP 0x15u          /* the field's heap                     */

/* How many whiteouts the run has seen (sdk/mod_event_battle.c). */
extern u32 bw_debug_whiteouts __attribute__((weak));

/* The party's size while the field runs; -1 before. */
static int debug_party_count(void)
{
    u8 *gs, *gd;
    void *party;

    if (&bw_dsref__GameSystemGlobal == NULL || GAME_DATA_GetMyParty == NULL || POKEPARTY_GetCount == NULL) {
        return -1;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL || (gd = *(u8 **)(gs + 0x1C)) == NULL ||
        (party = GAME_DATA_GetMyParty(gd)) == NULL) {
        return -1;
    }
    return (int)POKEPARTY_GetCount(party);
}

/* EIDOS_DEBUG_WHEN (the party, the battle) and EIDOS_DEBUG_MOVE_WHEN (the
 * moves) hold a test switch until the game is there, instead of a frame
 * count: "party:N" the party has N members or more (the starter came:
 * party:1), "whiteout" a whiteout has happened, in battle or in the field,
 * "whiteout:N" N frames have passed since the first one, "whiteout-done"
 * the whiteout's own script (the blackout, the nurse's or the mother's
 * heal) has ended and the field runs with no event (A pressed after that
 * would talk to the nurse again), "field:N" N frames have passed since the
 * field first ran (past its fade-in, when the player can move: field:200).
 * Without the variable the switch is open. */
extern void *GAMESYSTEM_GetEvent(const void *gsys) __attribute__((weak));
static int debug_when(const char *env)
{
    static u32 whiteout_frame, field_frame, busy_frame;
    const char *w = getenv(env);
    unsigned n = 0;
    u32 now = bw_input_frame_number();
    int field = debug_party_count() >= 0;

    if (&bw_debug_whiteouts != NULL && bw_debug_whiteouts > 0u && whiteout_frame == 0u) {
        whiteout_frame = now == 0u ? 1u : now;
    }
    if (field_frame == 0u && field) {
        field_frame = now == 0u ? 1u : now;
    }
    if (!field || (GAMESYSTEM_GetEvent != NULL && &bw_dsref__GameSystemGlobal != NULL &&
                   GAMESYSTEM_GetEvent(*(void **)bw_dsref__GameSystemGlobal) != NULL)) {
        busy_frame = now;               /* no field yet, or an event runs */
    }
    if (w == NULL || *w == '\0') {
        return 1;
    }
    if (strcmp(w, "whiteout-done") == 0) {
        return whiteout_frame != 0u && busy_frame > whiteout_frame && now > busy_frame;
    }
    if (strncmp(w, "field", 5) == 0) {
        (void)sscanf(w, "field:%u", &n);
        return field_frame != 0u && now >= field_frame + n;
    }
    if (sscanf(w, "party:%u", &n) == 1) {
        return debug_party_count() >= (int)n;
    }
    if (strncmp(w, "whiteout", 8) == 0) {
        (void)sscanf(w, "whiteout:%u", &n);
        return whiteout_frame != 0u && now >= whiteout_frame + n;
    }
    return 0;
}

/* EIDOS_DEBUG_A_UNTIL (a condition as EIDOS_DEBUG_WHEN's): A pressed every
 * 20 frames from EIDOS_DEBUG_A_FROM (a frame, default 0) until the
 * condition opens, through the battles and the texts of a scripted run. */
static void debug_a_until(u32 frame)
{
    const char *from = getenv("EIDOS_DEBUG_A_FROM");

    if (getenv("EIDOS_DEBUG_A_UNTIL") == NULL || (from != NULL && frame < (u32)strtoul(from, NULL, 10))) {
        return;
    }
    if (!debug_when("EIDOS_DEBUG_A_UNTIL") && frame % 20u == 0u) {
        eidos_ui_press(BW_KEY_A, 2);
    }
}

static void debug_party(void)
{
    static int done;
    const char *list = getenv("EIDOS_DEBUG_PARTY");
    u8 *gs, *gd;
    void *party;
    u32 n, i, id;
    u16 lv;

    if (list == NULL && (getenv("EIDOS_DEBUG_ITEMS") != NULL || getenv("EIDOS_DEBUG_PARTY_COUNT") != NULL)) {
        list = "";                      /* items or the count alone */
    }
    if (done || list == NULL || &bw_dsref__GameSystemGlobal == NULL || GAME_DATA_GetMyParty == NULL
        || POKE_TOOL_SetupPartyParam == NULL || POKEPARTY_GetMember == NULL || !debug_when("EIDOS_DEBUG_WHEN")) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL) {
        return;                         /* the field is not running yet    */
    }
    gd = *(u8 **)(gs + 0x1C);
    party = gd != NULL ? GAME_DATA_GetMyParty(gd) : NULL;
    if (party == NULL) {
        return;
    }
    done = 1;
    id = GAME_DATA_GetMyStatus != NULL && MYSTATUS_GetId != NULL ? MYSTATUS_GetId(GAME_DATA_GetMyStatus(gd)) : 0;
    lv = (u16)(getenv("EIDOS_DEBUG_LEVEL") ? atoi(getenv("EIDOS_DEBUG_LEVEL")) : 50);
    n = POKEPARTY_GetCount(party);
    for (i = 0; i < 6 && *list != '\0'; i++) {
        u16 sp = (u16)strtoul(list, (char **)&list, 10), mlv = lv;
        long short_by = 0;
        void *pp;

        if (*list == ':') {
            mlv = (u16)strtoul(list + 1, (char **)&list, 10);
            if (*list == ':') {
                short_by = -strtol(list + 1, (char **)&list, 10);   /* "-N" */
            }
        }
        if (sp != 0) {
            if (i >= n) {
                if (POKE_TOOL_Create == NULL || POKEPARTY_Add == NULL) {
                    break;
                }
                (void)POKEPARTY_Add(party, POKE_TOOL_Create(sp, mlv, id, DEBUG_FIELD_HEAP));
                n = POKEPARTY_GetCount(party);
            }
            pp = POKEPARTY_GetMember(party, i);
            POKE_TOOL_SetupPartyParam(pp, sp, mlv, id);
            if (short_by > 0 && POKE_TOOL_GetExpForLevel != NULL && POKE_TOOL_SetPartyValue != NULL
                && POKE_TOOL_CalcParam != NULL && mlv < 100) {
                u32 next = POKE_TOOL_GetExpForLevel(sp, 0, (u16)(mlv + 1));

                POKE_TOOL_SetPartyValue(pp, DEBUG_PARAM_EXP, (uintptr_t)(next - (u32)short_by));
                POKE_TOOL_CalcParam(pp);
            }
            fprintf(stderr, "eidos: debug party slot %u = species %u lv %u%s\n", (unsigned)i, (unsigned)sp,
                    (unsigned)mlv, short_by > 0 ? " (short of the next level)" : "");
        }
        while (*list == ',' || *list == ' ') {
            list++;
        }
    }
    /* EIDOS_DEBUG_PARTY_COUNT=N: the party keeps its first N members (a
     * one-Pokemon party for the whiteout tests: A alone cannot pick the
     * next Pokemon after a faint) */
    if (getenv("EIDOS_DEBUG_PARTY_COUNT") != NULL && POKEPARTY_RemoveMember != NULL && POKEPARTY_GetCount != NULL) {
        u32 keep = (u32)strtoul(getenv("EIDOS_DEBUG_PARTY_COUNT"), NULL, 10);

        while (keep >= 1u && POKEPARTY_GetCount(party) > keep) {
            (void)POKEPARTY_RemoveMember(party, (s32)POKEPARTY_GetCount(party) - 1);
        }
        fprintf(stderr, "eidos: debug party: %u member(s)\n", (unsigned)POKEPARTY_GetCount(party));
    }
    /* EIDOS_DEBUG_ITEMS="item:count,...": into the bag */
    if (getenv("EIDOS_DEBUG_ITEMS") != NULL && GAME_DATA_GetMyItem != NULL && MYITEM_SAVEDATA_AddItem != NULL) {
        const char *m = getenv("EIDOS_DEBUG_ITEMS");
        unsigned item, count;
        int used;

        while (sscanf(m, "%u:%u%n", &item, &count, &used) == 2) {
            u32 ok = MYITEM_SAVEDATA_AddItem(GAME_DATA_GetMyItem(gd), (u16)item, (u16)count, DEBUG_FIELD_HEAP);

            fprintf(stderr, "eidos: debug item %u x%u %s\n", item, count, ok ? "added" : "refused");
            m += used;
            if (*m != ',') break;
            m++;
        }
    }
    /* EIDOS_DEBUG_FLAGS="hex,hex": event flags set (0x961 and 0x962 give
     * the field menu its POKEMON and POKEDEX buttons) */
    if (getenv("EIDOS_DEBUG_FLAGS") != NULL && ZONEDATA_SetEventFlag != NULL && GAME_DATA_GetEventWork != NULL) {
        const char *m = getenv("EIDOS_DEBUG_FLAGS");
        unsigned flag;
        int used;

        while (sscanf(m, "%x%n", &flag, &used) == 1) {
            ZONEDATA_SetEventFlag(GAME_DATA_GetEventWork(gd), (int)flag);
            fprintf(stderr, "eidos: debug flag 0x%x set\n", flag);
            m += used;
            if (*m != ',') break;
            m++;
        }
    }
    /* EIDOS_DEBUG_CGEAR: the C-Gear obtained and on the bottom screen of
     * the next field (a warp after it shows it) */
    if (getenv("EIDOS_DEBUG_CGEAR") != NULL && GAME_DATA_SetSubscreenType != NULL
        && CGEAR_SAVE_SetObtained != NULL && BLOCK20_SAVEDATA_GetBlockFromSys != NULL) {
        CGEAR_SAVE_SetObtained(BLOCK20_SAVEDATA_GetBlockFromSys(gd), 1);
        GAME_DATA_SetSubscreenType(gd, 0);
        fprintf(stderr, "eidos: debug C-Gear\n");
    }
    /* EIDOS_DEBUG_MOVES="member:move:slot,...": teach moves */
    if (getenv("EIDOS_DEBUG_MOVES") != NULL && POKE_TOOL_SetMoveSlot != NULL) {
        const char *m = getenv("EIDOS_DEBUG_MOVES");
        unsigned mem, mv, sl;
        int used;

        while (sscanf(m, "%u:%u:%u%n", &mem, &mv, &sl, &used) == 3) {
            if (mem < n) {
                POKE_TOOL_SetMoveSlot(POKEPARTY_GetMember(party, mem), (u16)mv, sl);
                fprintf(stderr, "eidos: debug member %u move %u in slot %u\n", mem, mv, sl);
            }
            m += used;
            if (*m != ',') break;
            m++;
        }
    }
    /* EIDOS_DEBUG_UNLOCK: the eight badges and every fly destination (the
     * arrival flags of ov010's warp table d_02170FEC, 17 entries of 0x14
     * bytes: u16 flag, u16 bits (bit 0: a fly destination)) */
    if (getenv("EIDOS_DEBUG_UNLOCK") != NULL && MISC_SetBadge != NULL && GAME_DATA_GetMiscSave != NULL
        && ZONEDATA_SetEventFlag != NULL && GAME_DATA_GetEventWork != NULL) {
        const u8 *tab = (const u8 *)(uintptr_t)0x02170FECu;

        for (i = 0; i < 8; i++) {
            MISC_SetBadge(GAME_DATA_GetMiscSave(gd), i);
        }
        for (i = 0; i < 17; i++) {
            const u8 *e = tab + i * 0x14;
            u16 flag = (u16)(e[0] | (e[1] << 8));

            if ((e[2] & 1) != 0) {
                ZONEDATA_SetEventFlag(GAME_DATA_GetEventWork(gd), flag);
                fprintf(stderr, "eidos: debug fly destination flag %u (zone %u)\n", flag, (unsigned)(e[4] | (e[5] << 8)));
            }
        }
    }
}

/* The save file cannot be written (a folder the system refuses, a full
 * disk): the game believes it saved, but the progress is only in memory.
 * A warning shows at the top of the window for a few seconds, so the
 * player knows not to quit (sdk/arm7/fs.c retries on its own); it shows
 * again if writes fail again after one succeeds. */
extern u32 bw_arm7_backup_failures(void) __attribute__((weak));

#define SAVE_NOTICE_FRAMES 480u

static void save_failing_notice(EidosUiCtx *c)
{
    static u32 shown;               /* frames it has been up this time      */
    const char *msg = "The save could not be written to storage. Do not close the game.";
    int w, x, a;

    if (bw_arm7_backup_failures == NULL || bw_arm7_backup_failures() < 2u) {
        shown = 0;                  /* written: the next failure shows it again */
        return;
    }
    /* up for eight seconds, the last one fading; it does not stay over
     * the game while the writer keeps retrying */
    if (shown >= SAVE_NOTICE_FRAMES) {
        return;
    }
    shown++;
    a = shown + 60u > SAVE_NOTICE_FRAMES ? (int)(SAVE_NOTICE_FRAMES - shown) * 245 / 60 : 245;
    w = ui_text_w(msg, UI_FONT_LARGE) + 24;
    x = (c->canvas->w - w) / 2;
    ui_panel(c, x, 22, w, 22, UI_CUT_TL | UI_CUT_BR, 5, 0x3A0A10u, 0xE04050u, a);
    if (a > 120) {
        ui_text(c, x + 12, 22 + ui_text_y(22, UI_FONT_LARGE), msg, UI_WHITE, 0x200508u, UI_FONT_LARGE, 1);
    }
}

/* Tests: EIDOS_DEBUG_WARP="zone x y z dir" (map units, dir 0 up
 * 1 down 2 left 3 right) warps the player there through the game's own
 * map change, once the field has run 60 frames with no event. */
extern void *EVENT_MAPCHANGE_CreateWarpEventPlain(void *gsys, void *fieldMap, u16 zoneId, const s32 *pos, short dir) __attribute__((weak));
extern void GAMESYSTEM_SetEvent(void *gsys, void *event) __attribute__((weak));
extern void *GAMESYSTEM_GetEvent(const void *gsys) __attribute__((weak));

static void debug_warp(void)
{
    static int done, wait, after;
    const char *at = getenv("EIDOS_DEBUG_WARP");
    unsigned z, d;
    float x, y, zz;
    s32 pos[3];
    u8 *gs;
    void *ev;

    if (done && at != NULL && ++after == 240) {
        /* where the player stands once the new map runs: the height the
         * battle tests use (GameData's player work, fx32 x y z at +4) */
        u8 *gd = *(u8 **)(*(u8 **)bw_dsref__GameSystemGlobal + 0x1C);
        const s32 *p = (const s32 *)(gd + 4 + 4 * 0x44 + 4);

        fprintf(stderr, "eidos: debug warp arrived at (%.1f, %.1f, %.1f)\n", p[0] / 4096.0, p[1] / 4096.0, p[2] / 4096.0);
    }
    if (done || at == NULL || &bw_dsref__GameSystemGlobal == NULL || EVENT_MAPCHANGE_CreateWarpEventPlain == NULL
        || GAMESYSTEM_SetEvent == NULL || GAMESYSTEM_GetEvent == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        wait = 0;
        return;
    }
    if (++wait < 60 || sscanf(at, "%u %f %f %f %u", &z, &x, &y, &zz, &d) != 5) {
        return;
    }
    done = 1;
    pos[0] = (s32)(x * 4096.0f);
    pos[1] = (s32)(y * 4096.0f);
    pos[2] = (s32)(zz * 4096.0f);
    ev = EVENT_MAPCHANGE_CreateWarpEventPlain(gs, *(void **)(gs + 0x20), (u16)z, pos, (short)d);
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
        fprintf(stderr, "eidos: debug warp to zone %u (%.0f, %.0f, %.0f)\n", z, (double)x, (double)y, (double)zz);
    }
}

/*
 * Tests: EIDOS_DEBUG_CUTIN starts one of the game's field cut-ins (ov021
 * FLD3D_CI) the way its script commands do, once the field has run 90
 * frames with no event (after EIDOS_DEBUG_WARP's map change), or the frames
 * a third number gives ("item 621 300"):
 *   "item N"  a key item's (SCRCMD_StartFld3dCiByTable: the item's row of
 *             the 17-row table; the port is told the item as the patched
 *             command tells it)
 *   "seq N"   a sequence by its id (SCRCMD_StartFld3dCi)
 *   "mon P"   party member P's (SCRCMD_StartFld3dCiByVar: the field moves'
 *             cut-in, Cut and the others)
 */
extern void *ov021_FIELDMAP_GetFld3dCi(void *fieldMap) __attribute__((weak));
extern void *ov021_FLD3D_CI_StartSequence(void *gameSystem, void *ci, int sequenceId) __attribute__((weak));
extern void *ov021_FLD3D_CI_StartPartyMemberSequence(void *gameSystem, void *ci, int partyPos) __attribute__((weak));
extern void bw_ui_cutin_item(u32 item) __attribute__((weak));
/* ov021's 17-row key table, through the pointers the build gives overlay
 * data (rows of two u16: the item, then at +2 the sequence id) */
extern void *bw_dsref_ov021_gFld3dCiKeyTable __attribute__((weak));

static void debug_cutin(void)
{
    static int done, wait;
    const char *at = getenv("EIDOS_DEBUG_CUTIN");
    char kind[8];
    unsigned n, quiet = 90;
    u8 *gs;
    void *ci, *ev = NULL;

    if (done || at == NULL || &bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_SetEvent == NULL
        || GAMESYSTEM_GetEvent == NULL || ov021_FIELDMAP_GetFld3dCi == NULL
        || ov021_FLD3D_CI_StartSequence == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        wait = 0;
        return;
    }
    if (sscanf(at, "%7s %u %u", kind, &n, &quiet) < 2 || ++wait < (int)quiet) {
        return;
    }
    if (getenv("EIDOS_DEBUG_WARP") != NULL && wait < 200) {
        return;                         /* the warp first, then a settled field */
    }
    done = 1;
    ci = ov021_FIELDMAP_GetFld3dCi(*(void **)(gs + 0x20));
    if (ci == NULL) {
        return;
    }
    if (strcmp(kind, "item") == 0 && &bw_dsref_ov021_gFld3dCiKeyTable != NULL
        && bw_dsref_ov021_gFld3dCiKeyTable != NULL) {
        const unsigned short *tab = (const unsigned short *)bw_dsref_ov021_gFld3dCiKeyTable;
        int i;

        for (i = 0; i < 17; i++) {
            if (tab[i * 2] == n) {
                ev = ov021_FLD3D_CI_StartSequence(gs, ci, (unsigned char)tab[i * 2 + 1]);
                if (ev != NULL && bw_ui_cutin_item != NULL) {
                    bw_ui_cutin_item(n);
                }
                break;
            }
        }
    } else if (strcmp(kind, "seq") == 0) {
        ev = ov021_FLD3D_CI_StartSequence(gs, ci, (int)(n & 0xFFu));
    } else if (strcmp(kind, "mon") == 0 && ov021_FLD3D_CI_StartPartyMemberSequence != NULL) {
        ev = ov021_FLD3D_CI_StartPartyMemberSequence(gs, ci, (int)(n & 0xFFu));
    }
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
    }
    fprintf(stderr, "eidos: debug cut-in %s %u at input frame %u%s\n", kind, n,
            (unsigned)bw_input_frame_number(), ev != NULL ? "" : ": not started");
}

/* Tests: EIDOS_DEBUG_WHERE=1 logs the zone and the place the player stands
 * once the field has run 120 frames with no event, in the form
 * EIDOS_DEBUG_WARP takes, so a test can start from the same spot with any
 * save (GameData's player work: zone at +0, fx32 x y z at +4). */
static void debug_where(void)
{
    static int done, wait;
    u8 *gs, *gd;
    const u8 *pw;
    const s32 *p;

    if (done || getenv("EIDOS_DEBUG_WHERE") == NULL || &bw_dsref__GameSystemGlobal == NULL
        || GAMESYSTEM_GetEvent == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        wait = 0;
        return;
    }
    if (++wait < 120) {
        return;
    }
    done = 1;
    gd = *(u8 **)(gs + 0x1C);
    pw = gd + 4 + 4 * 0x44;
    p = (const s32 *)(pw + 4);
    fprintf(stderr, "eidos: debug where: EIDOS_DEBUG_WARP=\"%u %.0f %.0f %.0f 1\"\n",
            (unsigned)(pw[0] | (pw[1] << 8)), p[0] / 4096.0, p[1] / 4096.0, p[2] / 4096.0);
}

/* Tests: EIDOS_EVENT_LOG=1 logs the running game event's chain (each
 * event's function, innermost first) whenever it changes, as offsets from
 * GAMESYSTEM_GetEvent: `nm` on the program gives that function's address,
 * and the sum names the event's.  To find which code runs a scene. */
static void event_log(void)
{
    static uintptr_t last[4];
    uintptr_t now[4] = { 0, 0, 0, 0 };
    const u8 *gs, *ev;
    int i;

    if (getenv("EIDOS_EVENT_LOG") == NULL || &bw_dsref__GameSystemGlobal == NULL
        || GAMESYSTEM_GetEvent == NULL) {
        return;
    }
    gs = *(const u8 **)bw_dsref__GameSystemGlobal;
    ev = gs != NULL ? (const u8 *)GAMESYSTEM_GetEvent(gs) : NULL;
    for (i = 0; i < 4 && ev != NULL; i++) {
        now[i] = *(const uintptr_t *)(ev + sizeof(void *));      /* GameEvent func */
        ev = *(const u8 * const *)ev;                            /* parent */
    }
    if (memcmp(now, last, sizeof now) != 0) {
        memcpy(last, now, sizeof now);
        fprintf(stderr, "eidos: event at frame %u:", (unsigned)bw_input_frame_number());
        for (i = 0; i < 4 && now[i] != 0; i++) {
            fprintf(stderr, " %+ld", (long)(now[i] - (uintptr_t)GAMESYSTEM_GetEvent));
        }
        fprintf(stderr, "\n");
    }
}

/* Tests: EIDOS_DEBUG_SCRIPT="ID" runs the game's script ID (a global
 * script: the vending machine's 10400, the shared ones from 2000) as the
 * game starts a script, its work values first set from the "VAR=VALUE"
 * pairs after the ID (hex: "2801 8000=11 8001=1" gives a Potion through the
 * shared item script), once the field has run 90 frames with no event
 * (after EIDOS_DEBUG_WARP's arrival when both are set), so a test reaches
 * a screen the story puts far away. */
extern void *ov010_SCRIPT_SYS_CreateScriptEventSimple(void *gsys, u16 scriptId, void *target, int reserved)
    __attribute__((weak));
extern void *ov010_SCRIPT_SYS_GetEventScriptWork(void *event) __attribute__((weak));
extern int ov010_SCRIPT_WORK_SetWorkValue(void *work, u32 id, int value) __attribute__((weak));

static void debug_script(void)
{
    static int done, wait;
    const char *what = getenv("EIDOS_DEBUG_SCRIPT");
    u8 *gs;
    void *ev;

    if (done || what == NULL || &bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_SetEvent == NULL
        || GAMESYSTEM_GetEvent == NULL || ov010_SCRIPT_SYS_CreateScriptEventSimple == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || *(void **)(gs + 0x20) == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        wait = 0;
        return;
    }
    if (++wait < (getenv("EIDOS_DEBUG_WARP") != NULL ? 330 : 90)) {
        return;
    }
    done = 1;
    ev = ov010_SCRIPT_SYS_CreateScriptEventSimple(gs, (u16)atoi(what), NULL, 0);
    if (ev != NULL && ov010_SCRIPT_SYS_GetEventScriptWork != NULL && ov010_SCRIPT_WORK_SetWorkValue != NULL) {
        const char *p = strchr(what, ' ');
        void *sw = ov010_SCRIPT_SYS_GetEventScriptWork(ev);
        unsigned var, val;
        int n;

        while (p != NULL && sw != NULL && sscanf(p, " %x=%x%n", &var, &val, &n) == 2) {
            ov010_SCRIPT_WORK_SetWorkValue(sw, var, (int)val);
            p += n;
        }
    }
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
        fprintf(stderr, "eidos: debug script %s\n", what);
    }
}

/* Tests: EIDOS_DEBUG_OPEN opens a screen the story puts far away, through
 * the game's own events, once the field has run 90 frames with no event:
 *   "clear N"     the game clear (EVENT_GAMECLEAR_Create): the Hall of
 *                 Fame, the staff roll and the save after them
 *   "trade N S"   in-game trade N for party slot S (FLD_TRADE_CreateEvent)
 *   "relearn S"   the move relearner for party slot S (as
 *                 SCRCMD_PROC_CallMoveRelearn opens it)
 *   "tvt N"       Xtransceiver story call N (as SCRCMD_PROC_CallValueScreen)
 *   "makeegg S"   party slot S becomes an Egg (for a Spin Trade)
 *   "egg S"       party slot S becomes an Egg and hatches (the hatching the
 *                 field's step check starts, SYMBOL_MAP_CreatePokeEventTask)
 *   "box M"       the PC's boxes in mode M (as the PC's script command opens
 *                 them: 0 organize, 1 withdraw, 2 deposit, ...)
 *   "union"       into the Union Room, as its receptionist's script does
 *                 (EVENT_MAPCHANGE_CreateFixedZoneEvent: the room and its
 *                 link; the C-Gear's wireless must be on)
 *   "ltrade"      the Union Room's link trade with no partner (its own
 *                 offline path: the game takes the first offer as the
 *                 partner's), for the trade screen's tests
 * The save these run on is a test copy: the game clear saves. */
extern void *ov010_EVENT_GAMECLEAR_Create(void *gsys, u32 mode) __attribute__((weak));
extern void *ov021_FLD_TRADE_CreateEvent(void *gsys, u32 tradeId, u32 partyIndex) __attribute__((weak));
extern void *ov021_EVENT_NAMEIN_Create(void *gsys, void *fieldMap, void *parent, u32 mode, u16 *result)
    __attribute__((weak));
extern void *ov010_WAZA_OSHIE_CreateMoveList(void *mon, u32 heapId) __attribute__((weak));
extern void *ov010_WAZA_OSHIE_Create(u32 heapId) __attribute__((weak));
extern void *FIELD_STATUS_CreateSubProcCallbackEvent(void *gsys, void *fieldMap, u32 overlayId, void *procData,
                                                     void *procWork, void (*callback)(void *), void *callbackWork)
    __attribute__((weak));
extern void *ov010_SYMBOL_MAP_CreatePokeEventTask(void *gsys, void *fieldMap, void *poke) __attribute__((weak));
extern void *bw_dsref_ov010_gMoveRelearnProcData __attribute__((weak));
extern void *bw_dsref_ov010_gDemo3dProcData __attribute__((weak));
extern void *bw_dsref_ov021_d_021EF78C __attribute__((weak));        /* ov170's ProcData */
extern void *FIELD_STATUS_CreateSubProcNoFadeCallbackEvent(void *gsys, void *fieldMap, u32 overlayId, void *procData,
                                                           void *procWork, void (*callback)(void *), void *callbackWork)
    __attribute__((weak));
extern void *GAME_DATA_GetBoxManager(void *gd) __attribute__((weak));
extern void *GFL_HEAP_Alloc(u32 heapId, u32 size, int clear, const char *site, u16 tag) __attribute__((weak));
extern void *GAME_DATA_GetSaveControl(void *gd) __attribute__((weak));
extern void *SYSTEM_DATA_GetConfig(void *save) __attribute__((weak));
extern void *ZUKAN_SAVE_GetFromGameData(void *gd) __attribute__((weak));
extern u32 ZUKAN_SAVE_IsNationalDexEnabled(void *zukan) __attribute__((weak));
extern void *bw_dsref_ov010_gLocalTvtSysProcData __attribute__((weak));
extern void *POKEPARTY_Create(u32 heap) __attribute__((weak));
extern void *ov020_EVENT_MAPCHANGE_CreateFixedZoneEvent(void *gsys, void *fieldMap) __attribute__((weak));

static void debug_open_done(void *arg)
{
    (void)arg;
}

static void debug_open(void)
{
    static int done, wait;
    static struct { u16 value; u16 pad; void *gameData; } tvt;
    struct BoxParamDbg {
        void *gameData, *boxSave, *party, *myItem, *myStatus;
        int linkMode;
        void *config;
        u32 natDex;
        void *picker;
        int mode;
        u16 result;
        u8 box, slot;
    } *boxp;                            /* BoxParam (ov170/box2_param_types.h) */
    const char *what = getenv("EIDOS_DEBUG_OPEN");
    unsigned a = 0, b2 = 0;
    u8 *gs, *fm, *gd;
    void *ev = NULL;

    if (done || what == NULL || &bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_SetEvent == NULL
        || GAMESYSTEM_GetEvent == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    fm = gs != NULL ? *(u8 **)(gs + 0x20) : NULL;
    if (fm == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
        wait = 0;
        return;
    }
    if (++wait < (getenv("EIDOS_DEBUG_WARP") != NULL ? 330 : 90)) {
        return;
    }
    done = 1;
    gd = *(u8 **)(gs + 0x1C);
    if (sscanf(what, "demo %u", &a) == 1 && FIELD_STATUS_CreateSubProcCallbackEvent != NULL
        && &bw_dsref_ov010_gDemo3dProcData != NULL) {
        /* the field closed first (its overlays share the demo's memory: a
         * script closes it before EVENT_FIELD_PROCLINK_CreateCommExitEvent),
         * then the demo with CommProcParam (ov010/event_field_proclink.c):
         * gameSystem 0x00, kind 0x04 (the demo, 1..18), noon */
        static u8 demo_param[0x18];

        memset(demo_param, 0, sizeof demo_param);
        *(void **)(demo_param + 0x00) = gs;
        *(u32 *)(demo_param + 0x04) = a;
        demo_param[0x0D] = 12;
        ev = FIELD_STATUS_CreateSubProcCallbackEvent(gs, fm, 203, bw_dsref_ov010_gDemo3dProcData, demo_param,
                                                     debug_open_done, NULL);
    } else if (sscanf(what, "clear %u", &a) == 1 && ov010_EVENT_GAMECLEAR_Create != NULL) {
        ev = ov010_EVENT_GAMECLEAR_Create(gs, a);
    } else if (sscanf(what, "trade %u %u", &a, &b2) == 2 && ov021_FLD_TRADE_CreateEvent != NULL) {
        ev = ov021_FLD_TRADE_CreateEvent(gs, a, b2);
    } else if (sscanf(what, "relearn %u", &a) == 1 && ov010_WAZA_OSHIE_CreateMoveList != NULL
               && ov010_WAZA_OSHIE_Create != NULL && FIELD_STATUS_CreateSubProcCallbackEvent != NULL
               && GAME_DATA_GetMyParty != NULL && POKEPARTY_GetMember != NULL
               && &bw_dsref_ov010_gMoveRelearnProcData != NULL) {
        void *mon = POKEPARTY_GetMember(GAME_DATA_GetMyParty(gd), a);
        u8 *param = (u8 *)ov010_WAZA_OSHIE_Create(4);

        /* MoveRelearnParam: mon 0x00, myStatus 0x04, flags 0x08,
         * gameSystem 0x0C, relearnMoves 0x10, enabled 0x19 */
        *(void **)(param + 0x00) = mon;
        *(void **)(param + 0x04) = GAME_DATA_GetMyStatus != NULL ? GAME_DATA_GetMyStatus(gd) : NULL;
        *(u32 *)(param + 0x08) = 0;
        *(void **)(param + 0x0C) = gs;
        *(void **)(param + 0x10) = ov010_WAZA_OSHIE_CreateMoveList(mon, 4);
        param[0x19] = 1;
        ev = FIELD_STATUS_CreateSubProcCallbackEvent(gs, fm, 173, bw_dsref_ov010_gMoveRelearnProcData, param,
                                                     debug_open_done, NULL);
    } else if (sscanf(what, "tvt %u", &a) == 1 && FIELD_STATUS_CreateSubProcCallbackEvent != NULL
               && &bw_dsref_ov010_gLocalTvtSysProcData != NULL) {
        tvt.value = (u16)a;
        tvt.gameData = gd;
        ev = FIELD_STATUS_CreateSubProcCallbackEvent(gs, fm, 174, bw_dsref_ov010_gLocalTvtSysProcData, &tvt,
                                                     debug_open_done, NULL);
    } else if (sscanf(what, "box %u", &a) == 1 && FIELD_STATUS_CreateSubProcNoFadeCallbackEvent != NULL
               && &bw_dsref_ov021_d_021EF78C != NULL && GAME_DATA_GetBoxManager != NULL && GAME_DATA_GetMyItem != NULL
               && GAME_DATA_GetSaveControl != NULL && SYSTEM_DATA_GetConfig != NULL && GFL_HEAP_Alloc != NULL) {
        /* on the game's heap, as the PC's script command puts it (the
         * recipes read it as a game pointer) */
        boxp = (struct BoxParamDbg *)GFL_HEAP_Alloc(4, sizeof(*boxp), 1, "debug_open", 0);
        boxp->gameData = gd;
        boxp->boxSave = GAME_DATA_GetBoxManager(gd);
        boxp->party = GAME_DATA_GetMyParty(gd);
        boxp->myItem = GAME_DATA_GetMyItem(gd);
        boxp->myStatus = GAME_DATA_GetMyStatus(gd);
        boxp->config = SYSTEM_DATA_GetConfig(GAME_DATA_GetSaveControl(gd));
        boxp->natDex = ZUKAN_SAVE_GetFromGameData != NULL && ZUKAN_SAVE_IsNationalDexEnabled != NULL
                       ? ZUKAN_SAVE_IsNationalDexEnabled(ZUKAN_SAVE_GetFromGameData(gd)) : 0;
        boxp->mode = (int)a;
        ev = FIELD_STATUS_CreateSubProcNoFadeCallbackEvent(gs, fm, 170, bw_dsref_ov021_d_021EF78C, boxp,
                                                           debug_open_done, NULL);
    } else if (strcmp(what, "namein") == 0 && ov021_EVENT_NAMEIN_Create != NULL) {
        /* the game's free-text name entry (the beacon message, mode 7) */
        static u16 result;

        ev = ov021_EVENT_NAMEIN_Create(gs, fm, NULL, 7, &result);
    } else if (strcmp(what, "union") == 0 && ov020_EVENT_MAPCHANGE_CreateFixedZoneEvent != NULL) {
        ev = ov020_EVENT_MAPCHANGE_CreateFixedZoneEvent(gs, fm);
    } else if (strcmp(what, "ltrade") == 0 && FIELD_STATUS_CreateSubProcCallbackEvent != NULL
               && GFL_HEAP_Alloc != NULL
               && POKEPARTY_Create != NULL) {
        /* ClubTradeParam (ov006/event_wificlub_types.h): gameData 0x14,
         * party 0x1C; the trade reads up to 0x40 of it */
        u8 *param = (u8 *)GFL_HEAP_Alloc(4, 0x40, 1, "debug_open", 0);

        *(void **)(param + 0x14) = gd;
        *(void **)(param + 0x1C) = POKEPARTY_Create(4);
        /* gPokemonTradeProcData (ov119 0x021E7608, the Union Room's ProcData:
         * ov012 binds its own name for it only while ov012 is loaded) */
        ev = FIELD_STATUS_CreateSubProcCallbackEvent(gs, fm, 119, (void *)(uintptr_t)0x021E7608u, param,
                                                     debug_open_done, NULL);
    } else if (sscanf(what, "makeegg %u", &a) == 1 && GAME_DATA_GetMyParty != NULL && POKEPARTY_GetMember != NULL
               && POKE_TOOL_SetPartyValue != NULL) {
        /* party slot S becomes an Egg, nothing more (a Spin Trade needs one) */
        POKE_TOOL_SetPartyValue(POKEPARTY_GetMember(GAME_DATA_GetMyParty(gd), a), 0x4C, 1);
        fprintf(stderr, "eidos: debug open %s\n", what);
        return;
    } else if (sscanf(what, "egg %u", &a) == 1 && ov010_SYMBOL_MAP_CreatePokeEventTask != NULL
               && GAME_DATA_GetMyParty != NULL && POKEPARTY_GetMember != NULL && POKE_TOOL_SetPartyValue != NULL) {
        void *mon = POKEPARTY_GetMember(GAME_DATA_GetMyParty(gd), a);
        POKE_TOOL_SetPartyValue(mon, 0x4C, 1);          /* an Egg */
        ev = ov010_SYMBOL_MAP_CreatePokeEventTask(gs, fm, mon);
    }
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
        fprintf(stderr, "eidos: debug open %s\n", what);
    } else {
        fprintf(stderr, "eidos: debug open %s: not available\n", what);
    }
}

/* Tests: "ltrade" runs the trade's offline path, which the retail game
 * never takes for the Union Room (state 1): it skips the step that loads
 * the box icons' graphics (StepLoadBoxIconGfx, on the linked path only),
 * so the test loads them as that step would before the boxes are drawn.
 * TradeWork: stepFunc 0x5EC, vramBuf 0x1184. */
extern void ov119_POKETRADE_PROC_StepSendBoxInfo(void *p) __attribute__((weak));
extern void ov119_POKETRADE_2D_LoadBoxIconGfx(void *p, int box) __attribute__((weak));
extern void ov119_POKETRADE_PROC_StepSendAddOffer(void *p) __attribute__((weak));
extern void ov119_POKETRADE_PROC_StepAddSelectedOffer(void *p) __attribute__((weak));
extern void ov119_POKETRADE_PROC_StepSendApplyAddOffer(void *p) __attribute__((weak));
extern void ov119_POKETRADE_PROC_StepApplyAddOffer(void *p) __attribute__((weak));
extern void ov119_POKETRADE_NEGO_StepWipeToBrowse(void *p) __attribute__((weak));
extern void ov119_POKETRADE_NEGO_StepResolveConfirm(void *p) __attribute__((weak));
extern void ov119_POKETRADE_NEGO_StepOpenConfirmMenu(void *p) __attribute__((weak));
extern void ov119_POKETRADE_NEGO_StepResolveReplies(void *p) __attribute__((weak));
extern void ov119_POKETRADE_PROC_StepWaitCursorSync(void *p) __attribute__((weak));
extern void ov119_POKETRADE_NEGO_StoreRemoteOffer(void *p, int i, void *src) __attribute__((weak));

static void debug_ltrade(void)
{
    const char *what = getenv("EIDOS_DEBUG_OPEN");
    u8 *gs, *ps, *cur, *w;
    int b;

    if (what == NULL || strcmp(what, "ltrade") != 0 || &bw_dsref__GameSystemGlobal == NULL
        || ov119_POKETRADE_PROC_StepSendBoxInfo == NULL || ov119_POKETRADE_2D_LoadBoxIconGfx == NULL
        || !bw_overlay_is_loaded(119)) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    ps = gs != NULL ? *(u8 **)(gs + 0x08) : NULL;
    cur = ps != NULL ? *(u8 **)(ps + 0x08) : NULL;
    if (cur == NULL || *(u32 *)(cur + 0x04) != 119u || (w = *(u8 **)(cur + 0x14)) == NULL) {
        return;
    }
    /* no partner: its name is the player's, its offers the player's own */
    *(void **)(w + 0x834) = *(void **)(w + 0x830);
    if (ov119_POKETRADE_NEGO_StoreRemoteOffer != NULL
        && *(uintptr_t *)(w + 0x5EC) == (uintptr_t)ov119_POKETRADE_NEGO_StepWipeToBrowse) {
        for (b = 0; b < 3; b++) {
            void *own = *(void **)(w + 0xFD8 + 4u * (u32)b), *peer = *(void **)(w + 0xFD8 + 12u + 4u * (u32)b);

            if (own != NULL && peer == NULL && *(int *)(w + 0xFA4 + 4u * (u32)b) != -1) {
                ov119_POKETRADE_NEGO_StoreRemoteOffer(w, b, own);
            }
        }
    }
    /* the steps that send to the partner wait for the link; offline they
     * go on as a sent message would */
    {
        static void (*const pairs[][2])(void *) = {
            { ov119_POKETRADE_PROC_StepSendAddOffer, ov119_POKETRADE_PROC_StepAddSelectedOffer },
            { ov119_POKETRADE_PROC_StepSendApplyAddOffer, ov119_POKETRADE_PROC_StepApplyAddOffer },
            /* both chose (offline the link's id is not 0 or 1) */
            { ov119_POKETRADE_NEGO_StepResolveConfirm, ov119_POKETRADE_NEGO_StepOpenConfirmMenu },
        };
        u32 i;

        for (i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
            if (pairs[i][0] != NULL && *(uintptr_t *)(w + 0x5EC) == (uintptr_t)pairs[i][0]) {
                *(uintptr_t *)(w + 0x5EC) = (uintptr_t)pairs[i][1];
            }
        }
    }
    /* both agreed to trade (choice[] 0x11E0: 13 each) */
    if (ov119_POKETRADE_NEGO_StepResolveReplies != NULL && ov119_POKETRADE_PROC_StepWaitCursorSync != NULL
        && *(uintptr_t *)(w + 0x5EC) == (uintptr_t)ov119_POKETRADE_NEGO_StepResolveReplies && w[0x11E0] == 13) {
        *(uintptr_t *)(w + 0x5EC) = (uintptr_t)ov119_POKETRADE_PROC_StepWaitCursorSync;
    }
    if (*(uintptr_t *)(w + 0x5EC) == (uintptr_t)ov119_POKETRADE_PROC_StepSendBoxInfo && *(void **)(w + 0x1184) == NULL) {
        for (b = 0; b <= 24; b++) {
            ov119_POKETRADE_2D_LoadBoxIconGfx(w, b);
        }
    }
}

/* Tests: EIDOS_DEBUG_SEEK=FRAME walks the player, from that frame on, to
 * the nearest other person on the map and talks to them (A), as a player
 * would: one step every few frames toward them, then a turn and A when next
 * to them.  It stops once an event runs (the talk started).  For link
 * tests in the Union Room, where the other players wander. */
extern void *ov021_FIELDMAP_GetMmdlSys(void *fieldMap) __attribute__((weak));
extern int ov010_FLDMMDL_SysSearchUseMmdl(const void *sys, void **out, u32 *iter) __attribute__((weak));
extern void *ov010_FLDMMDL_SysSearchPlayer(void *sys) __attribute__((weak));
extern void ov010_FLDMMDL_GetGridNowPos(const void *mmdl, s16 *out) __attribute__((weak));

static void debug_seek(u32 frame)
{
    static u32 next;
    static int done;
    const char *at = getenv("EIDOS_DEBUG_SEEK");
    u8 *gs, *fm;
    void *sys, *me, *o, *best = NULL;
    u32 iter = 0;
    s16 p[3], q[3], b[3] = { 0, 0, 0 };
    int bd = 1 << 30, dx, dz;

    if (done || at == NULL || frame < (u32)atoi(at) || frame < next || ov021_FIELDMAP_GetMmdlSys == NULL
        || ov010_FLDMMDL_SysSearchUseMmdl == NULL || ov010_FLDMMDL_SysSearchPlayer == NULL
        || ov010_FLDMMDL_GetGridNowPos == NULL || &bw_dsref__GameSystemGlobal == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    fm = gs != NULL ? *(u8 **)(gs + 0x20) : NULL;
    if (fm == NULL || (sys = ov021_FIELDMAP_GetMmdlSys(fm)) == NULL || (me = ov010_FLDMMDL_SysSearchPlayer(sys)) == NULL) {
        return;
    }
    if (GAMESYSTEM_GetEvent != NULL && GAMESYSTEM_GetEvent(gs) != NULL) {
        if (next != 0) {
            done = 1;               /* the talk started */
            fprintf(stderr, "eidos: debug seek: talking\n");
        }
        return;
    }
    ov010_FLDMMDL_GetGridNowPos(me, p);
    while (ov010_FLDMMDL_SysSearchUseMmdl(sys, &o, &iter)) {
        int d;

        if (o == me) {
            continue;
        }
        ov010_FLDMMDL_GetGridNowPos(o, q);
        d = abs(q[0] - p[0]) + abs(q[2] - p[2]);
        if (d < bd) {
            bd = d;
            best = o;
            b[0] = q[0];
            b[2] = q[2];
        }
    }
    if (best == NULL) {
        return;
    }
    dx = b[0] - p[0];
    dz = b[2] - p[2];
    if (bd == 1) {
        u32 dir = dx > 0 ? BW_KEY_RIGHT : dx < 0 ? BW_KEY_LEFT : dz > 0 ? BW_KEY_DOWN : BW_KEY_UP;

        eidos_ui_press(dir, 2);     /* face them */
        next = frame + 8;
        done = 0;
        {
            static int turned;

            if (ui_textbox_field_active(fm)) {
                done = 1;           /* the talk is up: its menus follow */
                fprintf(stderr, "eidos: debug seek: talking\n");
                return;
            }
            if (turned++ > 0) {
                eidos_ui_press(BW_KEY_A, 3);
                next = frame + 30;
            }
        }
        return;
    }
    eidos_ui_press(abs(dx) >= abs(dz) ? (dx > 0 ? BW_KEY_RIGHT : BW_KEY_LEFT) : (dz > 0 ? BW_KEY_DOWN : BW_KEY_UP), 8);
    next = frame + 14;
}

/* Tests: EIDOS_DEBUG_MOVE moves the player by the game's own buttons, a
 * comma-separated list of steps run in order, each from its frame on and
 * not before the one before it is done:
 *   "F:face:D"      turn to D (up, down, left, right) without a step
 *   "F:walk:D:N"    walk N tiles toward D
 *   "F:tile:X:Z"    walk to tile (X, Z), the longer axis first
 *   "F:a"           press A (talk to who is in front, read a sign)
 * F 0 means as soon as the step before is done.  Each step logs where the
 * player stands when it ends; a walk the player cannot finish (a wall, a
 * person, an event) stops after 60 frames without a step and says so. */
extern u16 ov010_FLDMMDL_GetDirDisp(const void *mmdl) __attribute__((weak));

typedef struct DebugMove { u32 frame; int kind, dir, n, x, z; } DebugMove;
enum { DM_FACE, DM_WALK, DM_TILE, DM_A };

/* A direction: the word (up, down, left, right) or its letter (U D L R). */
static int dm_dir(const char *s)
{
    if (strncmp(s, "up", 2) == 0 || strncmp(s, "down", 4) == 0 || strncmp(s, "left", 4) == 0 ||
        strncmp(s, "right", 5) == 0) {
        return s[0] == 'u' ? 0 : s[0] == 'd' ? 1 : s[0] == 'l' ? 2 : 3;
    }
    if ((s[0] == 'U' || s[0] == 'D' || s[0] == 'L' || s[0] == 'R') && (s[1] == '\0' || s[1] == ':')) {
        return s[0] == 'U' ? 0 : s[0] == 'D' ? 1 : s[0] == 'L' ? 2 : 3;
    }
    return -1;
}

static void debug_move(u32 frame)
{
    static DebugMove steps[64];
    static int nsteps = -1, cur, started, tx, tz, stuck_since;
    static s16 last[3];
    static const u32 keys[4] = { BW_KEY_UP, BW_KEY_DOWN, BW_KEY_LEFT, BW_KEY_RIGHT };
    static const int ddx[4] = { 0, 0, -1, 1 }, ddz[4] = { -1, 1, 0, 0 };
    const char *s = getenv("EIDOS_DEBUG_MOVE");
    u8 *gs, *fm;
    void *sys, *me;
    s16 p[3];
    DebugMove *m;

    if (s == NULL || ov021_FIELDMAP_GetMmdlSys == NULL || ov010_FLDMMDL_SysSearchPlayer == NULL
        || ov010_FLDMMDL_GetGridNowPos == NULL || ov010_FLDMMDL_GetDirDisp == NULL
        || &bw_dsref__GameSystemGlobal == NULL) {
        return;
    }
    if (nsteps < 0) {
        char buf[32];

        nsteps = 0;
        while (*s != '\0' && nsteps < 64) {
            const char *e = strchr(s, ',');
            size_t len = e != NULL ? (size_t)(e - s) : strlen(s);
            unsigned f = 0;
            int a = 0, b = 0;

            if (len >= sizeof buf) {
                len = sizeof buf - 1;
            }
            memcpy(buf, s, len);
            buf[len] = '\0';
            m = &steps[nsteps];
            memset(m, 0, sizeof *m);
            if (sscanf(buf, "%u:face:%*s", &f) == 1 && strstr(buf, ":face:") != NULL) {
                m->kind = DM_FACE;
                m->dir = dm_dir(strstr(buf, ":face:") + 6);
            } else if (sscanf(buf, "%u:tile:%d:%d", &f, &a, &b) == 3) {
                m->kind = DM_TILE;
                m->x = a;
                m->z = b;
            } else if (strstr(buf, ":walk:") != NULL && sscanf(buf, "%u", &f) == 1) {
                const char *d = strstr(buf, ":walk:") + 6, *c = strchr(d, ':');

                m->kind = DM_WALK;
                m->dir = dm_dir(d);
                m->n = c != NULL ? atoi(c + 1) : 1;
            } else if (sscanf(buf, "%u:a", &f) == 1 && strstr(buf, ":a") != NULL) {
                m->kind = DM_A;
            } else {
                fprintf(stderr, "eidos: debug move: cannot read '%s'\n", buf);
                f = 0xFFFFFFFFu;
            }
            m->frame = f;
            if (f != 0xFFFFFFFFu && (m->kind == DM_A || m->kind == DM_TILE || m->dir >= 0)) {
                nsteps++;
            } else if (f != 0xFFFFFFFFu) {
                fprintf(stderr, "eidos: debug move: no direction in '%s' (up, down, left, right or U D L R)\n", buf);
            }
            s += len;
            if (*s == ',') {
                s++;
            }
        }
    }
    if (getenv("EIDOS_DEBUG_MOVE_TRACE") != NULL && frame % 500u == 0u) {
        fprintf(stderr, "eidos: debug move trace: input frame %u, step %d of %d (at %u)\n", (unsigned)frame, cur,
                nsteps, cur < nsteps ? (unsigned)steps[cur].frame : 0u);
    }
    /* EIDOS_DEBUG_MOVE_WHEN: the steps' frames count from when it opens */
    if (getenv("EIDOS_DEBUG_MOVE_WHEN") != NULL) {
        static int open;
        static u32 base;

        if (!open) {
            if (!debug_when("EIDOS_DEBUG_MOVE_WHEN")) {
                return;
            }
            open = 1;
            base = frame;
            fprintf(stderr, "eidos: debug move: %s at frame %u\n", getenv("EIDOS_DEBUG_MOVE_WHEN"), (unsigned)frame);
        }
        frame -= base;
    }
    if (cur >= nsteps || frame < steps[cur].frame) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    fm = gs != NULL ? *(u8 **)(gs + 0x20) : NULL;
    if (fm == NULL || (sys = ov021_FIELDMAP_GetMmdlSys(fm)) == NULL || (me = ov010_FLDMMDL_SysSearchPlayer(sys)) == NULL) {
        return;
    }
    m = &steps[cur];
    ov010_FLDMMDL_GetGridNowPos(me, p);
    if (!started) {
        started = 1;
        stuck_since = (int)frame;
        last[0] = p[0];
        last[2] = p[2];
        if (m->kind == DM_WALK) {
            tx = p[0] + ddx[m->dir] * m->n;
            tz = p[2] + ddz[m->dir] * m->n;
        } else {
            tx = m->x;
            tz = m->z;
        }
    }
    if (p[0] != last[0] || p[2] != last[2]) {
        last[0] = p[0];
        last[2] = p[2];
        stuck_since = (int)frame;
    }
    switch (m->kind) {
    case DM_A:
        eidos_ui_press(BW_KEY_A, 3);
        break;
    case DM_FACE:
        if ((int)ov010_FLDMMDL_GetDirDisp(me) != m->dir) {
            if ((int)frame - stuck_since > 60) {
                break;
            }
            if (((int)frame - stuck_since) % 8 == 0) {
                eidos_ui_press(keys[m->dir], 2);    /* a tap turns in place */
            }
            return;
        }
        break;
    default: {
        int dx = tx - p[0], dz = tz - p[2], d;

        if ((dx != 0 || dz != 0) && (int)frame - stuck_since <= 60) {
            d = abs(dx) >= abs(dz) ? (dx > 0 ? 3 : 2) : (dz > 0 ? 1 : 0);
            eidos_ui_press(keys[d], 2);
            return;
        }
        break;
    }
    }
    fprintf(stderr, "eidos: debug move %d at %u: tile (%d, %d) facing %d%s\n", cur, (unsigned)frame, p[0], p[2],
            (int)ov010_FLDMMDL_GetDirDisp(me),
            (m->kind == DM_WALK || m->kind == DM_TILE) && (p[0] != tx || p[2] != tz) ? ", blocked"
            : m->kind == DM_FACE && (int)ov010_FLDMMDL_GetDirDisp(me) != m->dir ? ", could not turn" : "");
    cur++;
    started = 0;
}

/* EIDOS_DEBUG_BATTLE="wild SPECIES LEVEL" or "trainer ID" (the spot
 * editor's Preview in game, tools/spot-editor; EIDOS_DEBUG_FOES replaces
 * the trainer's team, as EIDOS_DEBUG_PARTY the player's): starts that battle through
 * the game's own functions once the field has run 90 frames with no event
 * (after EIDOS_DEBUG_WARP's arrival when both are set). */
extern void *ov021_FIELDMAP_GetEncount(void *fieldMap) __attribute__((weak));
extern void *ov021_FIELD_ENCOUNT_StartScriptedWild(void *encount, u16 species, u8 level, u32 flags) __attribute__((weak));
extern void *ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn(void *gsys, void *fieldMap, int effect, int skipWait, int restoreBg,
                                                      u8 seasonShown, u8 seasonTarget) __attribute__((weak));
extern void ov021_FIELD_ENCOUNT_SetupTrainerBattle(void *encount, void *btlSetup, u32 rule, u32 partnerId, u32 trainerId1,
                                                   u32 trainerId2, u16 heapId) __attribute__((weak));
extern void *BTL_SETUP_Create(u32 heapId) __attribute__((weak));
extern void *ov010_EVENT_BATTLE_CreateScriptedBattle(void *gsys, void *fieldMap, void *btlSetup, int returnImmediately,
                                                     int effectId) __attribute__((weak));
extern int ov010_EVENT_TRAINER_IsDoubleBattle(int trainerId) __attribute__((weak));
extern int ov010_EVENT_TRAINER_GetBattleType(int trainerId) __attribute__((weak));

/* A trainer's battle rule, as the scripts pass it (EVENT_TRAINER_
 * GetBattleType: 0 single, 1 double, 2 triple, 3 rotation). */
static int debug_trainer_rule(unsigned id)
{
    if (ov010_EVENT_TRAINER_GetBattleType != NULL) {
        int t = ov010_EVENT_TRAINER_GetBattleType((int)id);

        return t >= 0 && t <= 3 ? t : 0;
    }
    return ov010_EVENT_TRAINER_IsDoubleBattle != NULL && ov010_EVENT_TRAINER_IsDoubleBattle((int)id);
}
extern void *GAME_EVENT_GetWork(void *event) __attribute__((weak));
extern void *ov010_EVENT_BATTLE_CreateEncounterTask(void *gsys, void *fieldMap, int rule, int partnerId,
                                                    int trainerId1, int trainerId2, int options)
    __attribute__((weak));

static int g_debug_battle_started;

int eidos_ui_debug_battle_started(void)
{
    return g_debug_battle_started;
}

static void debug_battle(void)
{
    static int done, wait, fade_back;
    const char *what = getenv("EIDOS_DEBUG_BATTLE");
    unsigned a1 = 0, a2 = 5;
    u8 *gs;
    void *fm, *enc, *ev = NULL;

    /* A scripted wild battle leaves the screen black: the script that
     * starts one fades the field back in itself.  Without a script, once the
     * battle's events are over, the field fades in the way the game's own
     * battle end does (EVENT_BATTLE_ProcScriptedBattle). */
    if (done && fade_back && ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn != NULL) {
        gs = *(u8 **)bw_dsref__GameSystemGlobal;
        fm = gs != NULL ? *(void **)(gs + 0x20) : NULL;
        if (fm != NULL && GAMESYSTEM_GetEvent(gs) == NULL) {
            fade_back = 0;
            ev = ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn(gs, fm, 0, 0, 1, 0, 0);
            if (ev != NULL) {
                GAMESYSTEM_SetEvent(gs, ev);
            }
        }
        return;
    }

    if (done || what == NULL || &bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_SetEvent == NULL
        || GAMESYSTEM_GetEvent == NULL || ov021_FIELDMAP_GetEncount == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    fm = gs != NULL ? *(void **)(gs + 0x20) : NULL;
    if (fm == NULL || GAMESYSTEM_GetEvent(gs) != NULL || !debug_when("EIDOS_DEBUG_WHEN")) {
        wait = 0;
        return;
    }
    if (++wait < (getenv("EIDOS_DEBUG_WARP") != NULL ? 330 : 90)) {
        return;
    }
    done = 1;
    enc = ov021_FIELDMAP_GetEncount(fm);
    if (enc == NULL) {
        return;
    }
    if (sscanf(what, "wild %u %u", &a1, &a2) >= 1 && ov021_FIELD_ENCOUNT_StartScriptedWild != NULL) {
        ev = ov021_FIELD_ENCOUNT_StartScriptedWild(enc, (u16)a1, (u8)a2, 0);
        fade_back = ev != NULL;
    } else if (sscanf(what, "trainer %u", &a1) == 1 && getenv("EIDOS_DEBUG_FOES") == NULL &&
               ov010_EVENT_BATTLE_CreateEncounterTask != NULL) {
        /* as a trainer's own battle: the trainer's encounter effect and
         * music (EVENT_BATTLE_CreateEncounterTask), with its own rule:
         * single, double, triple or rotation (its record's type) */
        ev = ov010_EVENT_BATTLE_CreateEncounterTask(gs, fm, debug_trainer_rule(a1), 0, (int)a1, 0, 0);
        /* The encounter task is a script's (BattleTask.isScripted, +0x14,
         * src/game/ov010/event_battle.c): on a loss it hands back to the
         * trainer's script, which blacks the player out.  Started here with
         * no script, a loss would leave no event and no field and the game
         * ends (GAMESYSTEM_ProcEnd resets to the title).  As a wild
         * encounter's task, it runs the whiteout itself. */
        if (ev != NULL && GAME_EVENT_GetWork != NULL) {
            *(u32 *)((u8 *)GAME_EVENT_GetWork(ev) + 0x14) = 0u;
        }
    } else if (sscanf(what, "trainer %u", &a1) == 1 && BTL_SETUP_Create != NULL
               && ov021_FIELD_ENCOUNT_SetupTrainerBattle != NULL && ov010_EVENT_BATTLE_CreateScriptedBattle != NULL) {
        void *bs = BTL_SETUP_Create(4);

        ov021_FIELD_ENCOUNT_SetupTrainerBattle(enc, bs, (u32)debug_trainer_rule(a1), 0, a1, 0, 4);
        /* EIDOS_DEBUG_FOES="species,species,...": the trainer's team becomes
         * fresh Pokemon of those species (BattleSetupParam.party[1], +0x28) */
        if (getenv("EIDOS_DEBUG_FOES") != NULL && POKE_TOOL_SetupPartyParam != NULL &&
            POKEPARTY_GetMember != NULL && POKEPARTY_GetCount != NULL) {
            const char *list = getenv("EIDOS_DEBUG_FOES");
            void *party = *(void **)((u8 *)bs + 0x28);
            u16 lv = (u16)(getenv("EIDOS_DEBUG_LEVEL") ? atoi(getenv("EIDOS_DEBUG_LEVEL")) : 50);
            u32 i, n = party != NULL ? POKEPARTY_GetCount(party) : 0;

            for (i = 0; i < n && *list != '\0'; i++) {
                u16 sp = (u16)strtoul(list, (char **)&list, 10);

                if (sp != 0) {
                    POKE_TOOL_SetupPartyParam(POKEPARTY_GetMember(party, i), sp, lv, 0);
                }
                while (*list == ',' || *list == ' ') {
                    list++;
                }
            }
        }
        ev = ov010_EVENT_BATTLE_CreateScriptedBattle(gs, fm, bs, 0, 0);
    }
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
        g_debug_battle_started = 1;
        fprintf(stderr, "eidos: debug battle: %s (input frame %u)\n", what, (unsigned)bw_input_frame_number());
    }
}

/*
 * EIDOS_BATTLE_ONLY (the spot editor's Preview): no logos, no title, no
 * save, no field: on the first frame a battle is built in memory and the
 * battle module runs on top of everything else, the way an encounter starts
 * it (GAME_EVENT_CallProc of ov093 with a battle setup).  The player is
 * fresh game data with the party EIDOS_DEBUG_PARTY names (at
 * EIDOS_DEBUG_LEVEL); EIDOS_DEBUG_BATTLE is "trainer N" (single or double
 * as the trainer is) or "wild SPECIES LEVEL"; EIDOS_DEBUG_BMAP_AT gives the
 * place (its zone, season and hour go to the battle's situation).
 */
struct BoSituation {                    /* src/game/arm9/btl_setup_types.h's FieldSituation */
    int bgType, bgAttr;
    u8 weather, season;
    u16 zoneId;
    u8 hour, minute;
};
extern void GFL_HEAP_CreateChildHeap(u32 parent, u32 heapId, u32 size) __attribute__((weak));
extern void *GAME_DATA_Create(u16 heapId) __attribute__((weak));
extern void ZONEDATA_Init(u16 heapId) __attribute__((weak));
extern void *POKE_TOOL_Create(u16 species, u16 level, u64 otId, u16 heapId) __attribute__((weak));
extern void *POKEPARTY_Create(u32 heapId) __attribute__((weak));
extern void POKEPARTY_Init(void *party) __attribute__((weak));
extern u32 POKEPARTY_Add(void *party, const void *member) __attribute__((weak));
extern void BTL_SETUP_InitFieldSituation(struct BoSituation *sit) __attribute__((weak));
extern void BTL_SETUP_SetupTrainerSingle(void *bs, void *gd, const struct BoSituation *sit, u32 trainerId,
                                         u16 heapId) __attribute__((weak));
extern void BTL_SETUP_SetupTrainerDouble(void *bs, void *gd, const struct BoSituation *sit, u32 trainerId,
                                         u16 heapId) __attribute__((weak));
extern void BTL_SETUP_SetupWild(void *bs, void *gd, void *wildParty, const struct BoSituation *sit, u32 rule,
                                u16 heapId) __attribute__((weak));
extern void PROCSYS_PushGlobal(u32 overlayId, const void *procData, void *param) __attribute__((weak));
extern void *bw_dsref_ov010_d_021EFDAC __attribute__((weak));   /* the battle's ProcData (ov093) */
extern void *bw_dsref_ov010_ov093_id __attribute__((weak));     /* its overlay */

static int g_battle_only_done;

/* The proc on show (the global ProcSys's current one, +0x08): its overlay. */
extern void *g_ProcSys __attribute__((weak));
extern void *bw_dsref_g_ProcSys __attribute__((weak));

static int current_overlay(void)
{
    void *sys = &g_ProcSys != NULL ? g_ProcSys
              : &bw_dsref_g_ProcSys != NULL && bw_dsref_g_ProcSys != NULL ? *(void **)bw_dsref_g_ProcSys : NULL;
    const u8 *proc;

    if (sys == NULL || (proc = *(const u8 *const *)((const u8 *)sys + 0x08)) == NULL) {
        return -1;
    }
    return (int)*(const u32 *)(proc + sizeof(void *));
}

/* Battle only: the battle is over when its module gives the screen back;
 * the game ends there (the logos under it are not wanted). */
static void battle_only_watch(void)
{
    static int seen;
    int ov;

    if (!g_battle_only_done || !g_debug_battle_started) {
        return;
    }
    ov = current_overlay();
    if (ov == 93) {
        seen = 1;
    } else if (seen && ov >= 0) {
        fprintf(stderr, "eidos: battle only: the battle is over\n");
        fflush(stderr);
        exit(0);
    }
}

int eidos_ui_battle_only(void)
{
    const char *v = getenv("EIDOS_BATTLE_ONLY");

    return v != NULL && *v != '\0' && strcmp(v, "0") != 0;
}

static void battle_only(void)
{
    const char *what = getenv("EIDOS_DEBUG_BATTLE"), *list = getenv("EIDOS_DEBUG_PARTY");
    const char *at = getenv("EIDOS_DEBUG_BMAP_AT");
    u16 lv = (u16)(getenv("EIDOS_DEBUG_LEVEL") ? atoi(getenv("EIDOS_DEBUG_LEVEL")) : 50);
    struct BoSituation sit;
    unsigned a1 = 0, a2 = 5, zone, season, hour;
    float x, z;
    void *gd, *party, *bs;

    if (g_battle_only_done || !eidos_ui_battle_only() || what == NULL) {
        return;
    }
    g_battle_only_done = 1;
    if (GFL_HEAP_CreateChildHeap == NULL || GAME_DATA_Create == NULL || GAME_DATA_GetMyParty == NULL
        || POKE_TOOL_Create == NULL || POKEPARTY_Add == NULL || BTL_SETUP_Create == NULL
        || BTL_SETUP_InitFieldSituation == NULL || PROCSYS_PushGlobal == NULL
        || &bw_dsref_ov010_d_021EFDAC == NULL || bw_dsref_ov010_d_021EFDAC == NULL) {
        fprintf(stderr, "eidos: battle only: the battle calls are not in this build\n");
        return;
    }
    /* the game system's own heaps (GAMESYSTEM_ProcInit makes them the same way) */
    GFL_HEAP_CreateChildHeap(1, 3, 0xa << 12);
    GFL_HEAP_CreateChildHeap(1, 4, 4 << 14);
    gd = GAME_DATA_Create(3);
    if (ZONEDATA_Init != NULL) {
        ZONEDATA_Init(3);               /* the zone table (the battle's lights read it) */
    }
    party = gd != NULL ? GAME_DATA_GetMyParty(gd) : NULL;
    if (party == NULL) {
        return;
    }
    /* the player's party: the species named, at the level asked (the game
     * data starts from the save's: emptied first) */
    if (POKEPARTY_Init != NULL) {
        POKEPARTY_Init(party);
    }
    while (list != NULL && *list != '\0' && (POKEPARTY_GetCount == NULL || POKEPARTY_GetCount(party) < 6)) {
        u16 sp = (u16)strtoul(list, (char **)&list, 10);

        if (sp != 0) {
            (void)POKEPARTY_Add(party, POKE_TOOL_Create(sp, lv, 0, 4));
        }
        while (*list == ',' || *list == ' ') {
            list++;
        }
    }
    if (POKEPARTY_GetCount != NULL && POKEPARTY_GetCount(party) == 0) {
        (void)POKEPARTY_Add(party, POKE_TOOL_Create(500, lv, 0, 4));
    }
    memset(&sit, 0, sizeof sit);
    BTL_SETUP_InitFieldSituation(&sit);
    if (at != NULL && sscanf(at, "%u %f %f %*u %u %u", &zone, &x, &z, &season, &hour) == 5) {
        sit.zoneId = (u16)zone;
        sit.season = (u8)season;
        sit.hour = (u8)hour;
    }
    bs = BTL_SETUP_Create(4);
    if (bs == NULL) {
        return;
    }
    if (sscanf(what, "trainer %u", &a1) == 1) {
        int dbl = ov010_EVENT_TRAINER_IsDoubleBattle != NULL && ov010_EVENT_TRAINER_IsDoubleBattle((int)a1);

        if (dbl && BTL_SETUP_SetupTrainerDouble != NULL) {
            BTL_SETUP_SetupTrainerDouble(bs, gd, &sit, a1, 4);
        } else if (BTL_SETUP_SetupTrainerSingle != NULL) {
            BTL_SETUP_SetupTrainerSingle(bs, gd, &sit, a1, 4);
        }
    } else if (sscanf(what, "wild %u %u", &a1, &a2) >= 1 && BTL_SETUP_SetupWild != NULL && POKEPARTY_Create != NULL) {
        /* "wild SPECIES LEVEL 2": a double, two of the species (dark grass,
         * FIELD_ENCOUNT_InitParam's rule) */
        void *wild = POKEPARTY_Create(4);
        unsigned count = 1;

        (void)sscanf(what, "wild %*u %*u %u", &count);
        (void)POKEPARTY_Add(wild, POKE_TOOL_Create((u16)a1, (u16)a2, 0, 4));
        if (count == 2u) {
            (void)POKEPARTY_Add(wild, POKE_TOOL_Create((u16)a1, (u16)a2, 0, 4));
        }
        BTL_SETUP_SetupWild(bs, gd, wild, &sit, count == 2u ? 1u : 0u, 4);
    } else {
        return;
    }
    PROCSYS_PushGlobal(&bw_dsref_ov010_ov093_id != NULL && bw_dsref_ov010_ov093_id != NULL
                           ? (u32)(uintptr_t)bw_dsref_ov010_ov093_id : 93u,
                       bw_dsref_ov010_d_021EFDAC, bs);
    g_debug_battle_started = 1;
    fprintf(stderr, "eidos: battle only: %s\n", what);
}

/* The game-thread calls: one producer (the frame thread), one consumer (the
 * game's thread), so two indexes are enough. */
#define GAME_CALLS 16
static struct { void (*fn)(void *); void *arg; } g_calls[GAME_CALLS];
static volatile u32 g_call_head, g_call_tail;

int eidos_ui_game_call(void (*fn)(void *arg), void *arg)
{
    u32 t = g_call_tail;

    if (fn == NULL || t - g_call_head >= GAME_CALLS) {
        return 0;
    }
    g_calls[t % GAME_CALLS].fn = fn;
    g_calls[t % GAME_CALLS].arg = arg;
    g_call_tail = t + 1;
    return 1;
}

void eidos_ui_game_frame(void)
{
    int i, n = g_sound_n;

    while (g_call_head != g_call_tail) {
        u32 h = g_call_head;

        g_calls[h % GAME_CALLS].fn(g_calls[h % GAME_CALLS].arg);
        g_call_head = h + 1;
    }
    battle_only();
    battle_only_watch();
    if (!eidos_ui_battle_only()) {
        debug_party();
        debug_warp();
        debug_cutin();
        debug_where();
        event_log();
        debug_battle();
        debug_script();
        debug_open();
        debug_ltrade();
        debug_seek(bw_input_frame_number());
        debug_move(bw_input_frame_number());
        debug_a_until(bw_input_frame_number());
    }

    for (i = 0; i < n; i++) {
        if (GFL_USE_PlaySeDefaultVolume != NULL) {
            GFL_USE_PlaySeDefaultVolume(g_sound[i]);
        }
        if (getenv("EIDOS_UI_SOUND_LOG") != NULL) {
            fprintf(stderr, "eidos: ui sound 0x%X (%s)\n", (unsigned)g_sound[i],
                    GFL_USE_PlaySeDefaultVolume != NULL ? "played" : "no sound system");
        }
    }
    g_sound_n = 0;
    if (g_cry != 0) {
        if (PM_VOICE_Play != NULL) {
            PM_VOICE_Play(g_cry, 0, 64, 0, 0, 0, 0, NULL);
        }
        g_cry = 0;
    }
}

#if defined(EIDOS_WRAP_PMVOICE)
/* The game's frame loop calls this once a frame on its main thread
 * (CMakeLists.txt, --wrap=PM_VOICE_StopFinished). */
extern void __real_PM_VOICE_StopFinished(void);
void __wrap_PM_VOICE_StopFinished(void);

void __wrap_PM_VOICE_StopFinished(void)
{

    eidos_ui_game_frame();
    __real_PM_VOICE_StopFinished();
}
#endif
#define MODAL_FADE_FRAMES 90
static u32 g_modal_age = MODAL_FADE_FRAMES;
static EidosUiView g_last_view;
static int g_last_view_ok;
static int g_gap;
static int g_hide_tail;          /* frames the game's 2D stays hidden after */

u32 eidos_ui_scene_age(void)
{
    return g_scene_frames;
}

/* A kept picture resampled to w x h (nearest): the same window area at
 * another canvas scale. */
static void refit(u8 **px, int *pw, int *ph, int w, int h)
{
    u8 *n;
    int x, y;

    if (*px == NULL || *pw <= 0 || *ph <= 0 || (*pw == w && *ph == h)) {
        return;
    }
    n = (u8 *)malloc((size_t)w * (size_t)h * 4);
    if (n == NULL) {
        return;
    }
    for (y = 0; y < h; y++) {
        int sy = y * *ph / h;

        for (x = 0; x < w; x++) {
            memcpy(n + ((size_t)y * w + x) * 4, *px + ((size_t)sy * *pw + x * *pw / w) * 4, 4);
        }
    }
    free(*px);
    *px = n;
    *pw = w;
    *ph = h;
}

static void fade_start(void)
{
    size_t n;

    if (g_prev == NULL || g_prev_w <= 0) {
        return;
    }
    n = (size_t)g_prev_w * (size_t)g_prev_h * 4;
    if (g_fade_from == NULL || g_fade_w * g_fade_h != g_prev_w * g_prev_h) {
        free(g_fade_from);
        g_fade_from = (u8 *)malloc(n);
        if (g_fade_from == NULL) {
            return;
        }
    }
    memcpy(g_fade_from, g_prev, n);
    g_fade_w = g_prev_w;
    g_fade_h = g_prev_h;
    g_fade_left = FADE_FRAMES;
}

/* t in 0..256: the share of the new picture, eased out. */
static int ease(int left, int total)
{
    int t = (total - left) * 256 / total;

    return 256 - (256 - t) * (256 - t) / 256;
}

static void fade_apply(EidosUiCanvas *cv)
{
    int t, slide, x, y;
    u8 *d = cv->rgba;

    if (g_fade_left <= 0) {
        return;
    }
    t = ease(g_fade_left, FADE_FRAMES);
    slide = SLIDE_PIXELS * (256 - t) / 256;
    if (slide > 0) {
        /* the new picture settles from below: move it down slide rows */
        memmove(d + (size_t)slide * cv->w * 4, d, (size_t)(cv->h - slide) * cv->w * 4);
        memset(d, 0, (size_t)slide * cv->w * 4);
    }
    for (y = 0; y < cv->h; y++) {
        for (x = 0; x < cv->w; x++) {
            u8 *n = d + ((size_t)y * cv->w + x) * 4;
            int na = n[3] * t / 256, oa = 0, a, k;
            const u8 *o = NULL;

            if (g_fade_from != NULL && x < g_fade_w && y < g_fade_h) {
                o = g_fade_from + ((size_t)y * g_fade_w + x) * 4;
                oa = o[3] * (256 - t) / 256;
            }
            a = na + oa;
            if (a <= 0) {
                n[3] = 0;
                continue;
            }
            for (k = 0; k < 3; k++) {
                n[k] = (u8)((n[k] * na + (o != NULL ? o[k] * oa : 0)) / a);
            }
            n[3] = (u8)(a > 255 ? 255 : a);
        }
    }
    g_fade_left--;
    cv->generation++;
}

static void ring_apply(EidosUiCtx *c)
{
    int r, a;

    if (g_pressed && g_mx >= 0) {
        g_ring_x = g_mx;
        g_ring_y = g_my;
        g_ring_age = 0;
    }
    if (g_ring_x < 0 || g_ring_age >= RING_FRAMES) {
        return;
    }
    r = 2 + g_ring_age;
    a = 200 * (RING_FRAMES - g_ring_age) / RING_FRAMES;
    {
        int ox = c->ox, oy = c->oy;

        c->ox = c->oy = 0;
        ui_ellipse(c, g_ring_x - r, g_ring_y - r, 2 * r + 1, 2 * r + 1, UI_NONE, UI_WHITE, a);
        c->ox = ox;
        c->oy = oy;
    }
    g_ring_age++;
}

static void keep_prev(const EidosUiCanvas *cv)
{
    size_t n = (size_t)cv->w * (size_t)cv->h * 4;

    if (g_prev == NULL || g_prev_w != cv->w || g_prev_h != cv->h) {
        free(g_prev);
        g_prev = (u8 *)malloc(n);
        g_prev_w = g_prev != NULL ? cv->w : 0;
        g_prev_h = g_prev != NULL ? cv->h : 0;
    }
    if (g_prev != NULL) {
        memcpy(g_prev, cv->rgba, n);
    }
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

/* Which DS screen shows the main engine, the one that carries the 3D:
 * POWCNT1 bit 15 set puts it on the top (engine/g2d/hwcompose.c). */
/* A DS screen of one colour (the game's fade to white, say): that colour
 * as 0xRRGGBB, or -1.  Nine points are enough to tell. */
static long flat_screen(const u8 *px)
{
    static const int xs[3] = { 8, 128, 247 }, ys[3] = { 8, 96, 183 };
    long first = -1;
    int i, j;

    if (px == NULL) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            const u8 *p = px + ((size_t)ys[j] * 256 + (size_t)xs[i]) * 4;
            long c = ((long)p[0] << 16) | ((long)p[1] << 8) | p[2];

            if (first < 0) {
                first = c;
            } else if (c != first) {
                return -1;
            }
        }
    }
    return first;
}

static int screen_with_3d(void)
{
    return (bw_io_read16(0x04000304u) & 0x8000u) != 0 ? 0 : 1;
}

/*
 * For checks only: EIDOS_UI_MOUSE scripts the mouse in window pixels, a
 * comma-separated list of events "frame:x:y" (move there and click: down
 * that frame, up two frames later), "frame:x:y:m" (move only, a hover, or
 * a drag while held), "frame:x:y:d" / "frame:x:y:u" (down and held, up) and
 * "frame:x:y:w+N" / "w-N" (wheel steps).  The real mouse is ignored while
 * the script has events left.
 */
typedef struct ScriptedMouse { u32 frame; int x, y, kind, wheel; } ScriptedMouse;
static ScriptedMouse g_script[256];
static int g_script_n = -1;
static int g_sx = -1, g_sy = -1;
static u32 g_release_at;
static int g_held;              /* down by a "d" event, until its "u" */

static void script_load(void)
{
    const char *s = getenv("EIDOS_UI_MOUSE");

    g_script_n = 0;
    while (s != NULL && *s != '\0' && g_script_n < 256) {
        ScriptedMouse e;
        char *end;

        memset(&e, 0, sizeof(e));
        e.frame = (u32)strtoul(s, &end, 10);
        if (*end != ':') break;
        e.x = (int)strtol(end + 1, &end, 10);
        if (*end != ':') break;
        e.y = (int)strtol(end + 1, &end, 10);
        if (*end == ':') {
            if (end[1] == 'm') {
                e.kind = 1;
                end += 2;
            } else if (end[1] == 'r') {
                e.kind = 3;
                end += 2;
            } else if (end[1] == 'd') {
                e.kind = 4;
                end += 2;
            } else if (end[1] == 'u') {
                e.kind = 5;
                end += 2;
            } else if (end[1] == 'w') {
                e.kind = 2;
                e.wheel = (int)strtol(end + 2, &end, 10);
            }
        }
        g_script[g_script_n++] = e;
        s = (*end == ',') ? end + 1 : end;
        if (*s != '\0' && *s != ',' && (*s < '0' || *s > '9')) break;
    }
}

static int script_mouse(u32 frame, EidosUiMouse *m)
{
    int i, any = 0;

    if (g_script_n < 0) {
        script_load();
    }
    if (g_script_n == 0) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    m->x = g_sx;
    m->y = g_sy;
    if (g_release_at != 0 && frame >= g_release_at) {
        m->released = 1;
        g_release_at = 0;
    } else if (g_release_at != 0 || g_held) {
        m->down = 1;
    }
    for (i = 0; i < g_script_n; i++) {
        if (g_script[i].frame == frame) {
            m->x = g_sx = g_script[i].x;
            m->y = g_sy = g_script[i].y;
            if (g_script[i].kind == 0) {
                m->pressed = m->down = 1;
                g_release_at = frame + 2;
            } else if (g_script[i].kind == 2) {
                m->wheel += g_script[i].wheel;
            } else if (g_script[i].kind == 3) {
                m->right_pressed = 1;
            } else if (g_script[i].kind == 4) {
                m->pressed = m->down = 1;
                g_held = 1;
            } else if (g_script[i].kind == 5) {
                m->down = 0;
                m->released = 1;
                g_held = 0;
            }
        }
        if (g_script[i].frame >= frame) {
            any = 1;
        }
    }
    return any || g_release_at != 0 || g_held || g_sx >= 0;
}

static void read_mouse(const EidosUiFrame *in)
{
    EidosUiMouse scripted;
    const EidosUiMouse *m = &in->mouse;

    if (script_mouse(in->frame, &scripted)) {
        m = &scripted;
    }
    if (m->x < 0 || m->y < 0) {
        g_mx = g_my = -1;
    } else {
        g_mx = m->x / g_k;
        g_my = m->y / g_k;
    }
    g_down = m->down;
    g_pressed = m->pressed;
    g_released = m->released;
    g_wheel = m->wheel;
    g_right = m->right_pressed;
    if (g_pressed) {
        g_down_x = g_mx;
        g_down_y = g_my;
    }
}

static const EidosUiFrame *g_in;

/* Set by a recipe's match for this frame: the canvas takes the DS scale. */
static int g_ds_scale_req;

void eidos_ui_want_ds_scale(void)
{
    g_ds_scale_req = 1;
}

const EidosUiFrame *eidos_ui_frame_in(void)
{
    return g_in;
}

/* What the system covers on each side, in canvas pixels (rounded up, so
 * nothing to read or touch reaches under it). */
static void set_safe(const EidosUiFrame *in)
{
    g_ctx.safe_l = in->safe_l > 0 ? (in->safe_l + g_k - 1) / g_k : 0;
    g_ctx.safe_t = in->safe_t > 0 ? (in->safe_t + g_k - 1) / g_k : 0;
    g_ctx.safe_r = in->safe_r > 0 ? (in->safe_r + g_k - 1) / g_k : 0;
    g_ctx.safe_b = in->safe_b > 0 ? (in->safe_b + g_k - 1) / g_k : 0;
    g_ctx.bar_top = -1;
}

void eidos_ui_frame(const EidosUiFrame *in, EidosUiView *view,
                    const EidosUiCanvas **canvas)
{
    const EidosUiRecipe *r = NULL;
    int w, h;
    u32 i;

    g_in = in;

    {
        /* the built-in themes, and EIDOS_UI_THEME's choice (engine/ui/theme.h) */
        static int themes_ready;

        if (!themes_ready) {
            extern const UiThemeOps ui_theme_debug_outline, ui_theme_remix;

            themes_ready = 1;
            (void)ui_theme_add(&ui_theme_remix);
            (void)ui_theme_add(&ui_theme_debug_outline);
            if (getenv("EIDOS_UI_THEME") != NULL) {
                ui_theme_select(getenv("EIDOS_UI_THEME"));
            }
        }
    }
    eidos_ui_mark_active();          /* settings.h: for game-side patches */
    g_k = eidos_ui_scale(in->win_w, in->win_h);
    /* rounded up: the canvas reaches the window's right and bottom edges
     * (a bar along the bottom sits on the edge, not k - 1 pixels above it) */
    w = (in->win_w + g_k - 1) / g_k;
    h = (in->win_h + g_k - 1) / g_k;
    ui_begin(&g_ctx, &g_canvas, w, h, g_k);
    set_safe(in);
    read_mouse(in);
    g_want_text = 0;
    g_want_objs = 0;
    g_keys_now = bw_input_player_buttons();
    g_keys_edge = g_keys_now & ~g_keys_prev;
    if (g_mx != g_last_mx || g_my != g_last_my) {
        /* a move (not the first sighting of the pointer) */
        if (g_last_mx >= 0 && g_mx >= 0) {
            g_mouse_live = 1;
        }
        g_last_mx = g_mx;
        g_last_my = g_my;
    }
    if (g_pressed || g_down || g_wheel != 0 || g_right) {
        g_mouse_live = 1;
    }
    if (g_keys_edge != 0) {
        g_mouse_live = 0;
    }
    {
        /* the arrows repeat while held, as the game's own lists do: after
         * KEY_REPEAT_WAIT frames, one step every KEY_REPEAT_STEP */
        static u32 held_frames;
        u32 arrows = g_keys_now & (BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);

        if (arrows != 0 && arrows == (g_keys_prev & arrows) && (g_keys_edge & arrows) == 0) {
            held_frames++;
            if (held_frames >= KEY_REPEAT_WAIT
                && (held_frames - KEY_REPEAT_WAIT) % KEY_REPEAT_STEP == 0) {
                g_keys_edge |= arrows;
            }
        } else {
            held_frames = 0;
        }
    }
    g_keys_prev = g_keys_now;
    g_take = 0;

    memset(view, 0, sizeof(*view));
    view->mode = EIDOS_VIEW_FALLBACK;
    view->world_screen = screen_with_3d();
    view->zoom = 1.0f;

    for (i = 0; i < eidos_ui_group_count && r == NULL; i++) {
        const EidosUiRecipeGroup *g = &eidos_ui_groups[i];
        u32 j;

        for (j = 0; j < *g->count; j++) {
            /* For checks only: EIDOS_UI_SKIP="name,name" leaves recipes out. */
            const char *skip = getenv("EIDOS_UI_SKIP");

            if (skip != NULL && strstr(skip, g->recipes[j]->name) != NULL) {
                continue;
            }
            if (g->recipes[j]->match != NULL && g->recipes[j]->match()) {
                r = g->recipes[j];
                break;
            }
        }
    }
    if (r != NULL && (r->round_scale || g_ds_scale_req)) {
        /* round_scale: the window's height over 270, rounded; a scene that
         * shows the game's text (eidos_ui_want_ds_scale): the DS screen's
         * own scale, so the letters are as big as the game's; neither
         * wider than the window (the DS screen's 256 columns, the 360 of
         * eidos_ui_scale) */
        int k = g_ds_scale_req ? in->win_h / 192 : (in->win_h + 135) / 270;
        int kw = g_ds_scale_req ? in->win_w / 256 : (in->win_w + 180) / 360;

        if (kw < k) {
            k = kw;
        }

        if (k > g_k) {
            g_k = k;
            w = (in->win_w + g_k - 1) / g_k;
            h = (in->win_h + g_k - 1) / g_k;
            ui_begin(&g_ctx, &g_canvas, w, h, g_k);
            set_safe(in);
            read_mouse(in);
        }
    }
    g_ds_scale_req = 0;
    /* a recipe that draws the game's text windows itself leaves them out
     * of the screen again this frame (engine/ui/textbox.c) */
    if (g2d_hw_hide_main_bgs != NULL) {
        g2d_hw_hide_main_bgs(0);
    }
    /* the pictures kept from earlier frames, at this canvas's size (the
     * scale changed with the scene): the crossfades still find them */
    refit(&g_prev, &g_prev_w, &g_prev_h, w, h);
    refit(&g_before, &g_before_w, &g_before_h, w, h);
    refit(&g_fade_from, &g_fade_w, &g_fade_h, w, h);
    if (r != NULL && r != g_shown) {
        /* keep the scene before this one: an app closing goes back to it,
         * and shows it at once while the game fades and loads */
        if (g_prev != NULL && g_shown != NULL) {
            size_t n = (size_t)g_prev_w * (size_t)g_prev_h * 4;

            if (g_before == NULL || g_before_w != g_prev_w || g_before_h != g_prev_h) {
                free(g_before);
                g_before = (u8 *)malloc(n);
            }
            if (g_before != NULL) {
                memcpy(g_before, g_prev, n);
                g_before_w = g_prev_w;
                g_before_h = g_prev_h;
            }
        }
        g_shown = r;
    }
    if (r != g_last) {
        if (r != NULL) {
            g_leaving = 0;      /* a gap keeps it: the app is still closing */
        }
        /* a new scene: a press that began in the old one is not a click */
        g_down_x = g_down_y = -1;
        g_last = r;
        g_scene_frames = 0;
        if (r != NULL || !g_leaving) {
            fade_start();       /* (a closing app's crossfade runs on) */
        }
    } else {
        g_scene_frames++;
    }
    if (g_intent_left > 0) {
        g_intent_left--;
    }
    if (g_tap_end != 0 && in->frame >= g_tap_end) {
        g_tap_end = 0;
        if (UI_GetInputMode != NULL && UI_SetInputMode != NULL && UI_GetInputMode() != 0) {
            UI_SetInputMode(0);
        }
    }
    if (r != NULL) {
        g_scene = r->name;
        EidosUiFrame f = *in;
        const char *typed = getenv("EIDOS_UI_TEXT");

        /* For checks only: EIDOS_UI_TEXT="frame:text" types text at that
         * frame, as if from the PC keyboard ("\" before n or b gives Return
         * or Backspace). */
        if (typed != NULL && (u32)strtoul(typed, NULL, 10) == in->frame) {
            static char buf[128];
            const char *s = strchr(typed, ':');
            int o = 0;

            for (s = s != NULL ? s + 1 : ""; *s != '\0' && o < 126; s++) {
                if (s[0] == '\\' && (s[1] == 'n' || s[1] == 'b')) {
                    buf[o++] = s[1] == 'n' ? '\n' : '\b';
                    s++;
                } else {
                    buf[o++] = *s;
                }
            }
            buf[o] = '\0';
            f.text = buf;
        }
        if (getenv("EIDOS_UI_TRACE") != NULL) {
            fprintf(stderr, "eidos: ui trace %u run %s\n", (unsigned)in->frame, r->name);
            fflush(stderr);
        }
        r->run(&g_ctx, view, &f);
        save_failing_notice(&g_ctx);
        if (getenv("EIDOS_UI_TRACE") != NULL) {
            fprintf(stderr, "eidos: ui trace %u done\n", (unsigned)in->frame);
            fflush(stderr);
        }
        leave_check(view);
        g_own_reveal = 0;
        if (getenv("EIDOS_UI_BARE") != NULL) {
            /* For mock-ups only: the world alone, no interface and none of
             * the game's 2D, as a background to draw designs over. */
            memset(g_canvas.rgba, 0, (size_t)g_canvas.w * (size_t)g_canvas.h * 4);
            view->hide_2d = 1;
        }
        if (view->hide_2d) {
            g_hide_tail = HIDE_TAIL_FRAMES;
        } else if (g_hide_tail > 0) {
            g_hide_tail--;
            if (view->mode == EIDOS_VIEW_WORLD) {
                view->hide_2d = 1;
            }
        }
        g_last_view = *view;
        g_last_view_ok = 1;
        g_gap = 0;
    } else {
        g_scene = "fallback";
        /* Between two scenes the game fades and loads (a menu closing, an
         * app opening): no recipe matches for a moment.  The last view
         * holds through that gap, so the window does not jump to the two
         * DS screens and back; a scene with no recipe gets the fallback
         * once the gap is over. */
        if (g_last_view_ok && g_gap < GAP_FRAMES) {
            *view = g_last_view;
            g_gap++;
            /* the last picture of the interface too: an empty canvas would
             * show the game's own DS screen under it for those frames */
            if (g_prev != NULL && g_prev_w == g_canvas.w && g_prev_h == g_canvas.h) {
                memcpy(g_canvas.rgba, g_prev, (size_t)g_prev_w * (size_t)g_prev_h * 4);
            }
            /* the game faded its screen to white (the flash before a
             * battle): the whole window takes it, not a DS-sized box of it
             * (a fade to black keeps the held picture: an app opening
             * over the world does not take the game's fades) */
            if (flat_screen(in->screen_rgba[view->world_screen & 1]) == 0xFFFFFFL) {
                ui_fill(&g_ctx, 0, 0, g_canvas.w, g_canvas.h, 0xFFFFFFu, 255);
                g_fade_left = 0;        /* nothing to blend from: it is all white */
            }
            /* an app closed (its proc is ending or gone, its recipe let
             * go): the scene it goes back to (the menu) shows at once, and
             * stays while the game rebuilds the field, over a sharp world */
            if (((g_last_view.blur && app_ending()) || g_leaving) && g_before != NULL &&
                g_before_w == g_canvas.w && g_before_h == g_canvas.h) {
                if (!g_leaving) {
                    g_leaving = 1;
                    g_leave_frame = bw_input_frame_number();
                    g_before_shown = 0;
                }
                (void)leave_picture(view);
                view->hold_world = 1;
                view->hide_2d = 1;
                view->no_fade = 1;
            }
        } else {
            g_last_view_ok = 0;
        }
        if (getenv("EIDOS_UI_FORCE_WORLD") != NULL) {
            view->mode = EIDOS_VIEW_WORLD;
        }
    }
    /* An app opens from a modal (the start menu, a blurred modal) with the
     * game's fade to black, and closes the same way back to the field.  The
     * one-screen world does not take those fades: the modal moves over the
     * world, which stays as it was, for a while after a modal too (the
     * field fading back in). */
    if (view->mode == EIDOS_VIEW_WORLD) {
        if (view->blur || view->no_fade) {
            g_modal_age = 0;
        } else if (g_modal_age < MODAL_FADE_FRAMES) {
            g_modal_age++;
            view->no_fade = 1;
        }
    }
    {
        /* the blur behind a modal eases in and out (eight frames) instead
         * of snapping on and off: view->blur becomes its strength, 1..16 */
        static int amount;
        int target = (view->blur && view->mode == EIDOS_VIEW_WORLD) ? 16 : 0;

        if (amount < target) {
            amount = amount + 2 < target ? amount + 2 : target;
        } else if (amount > target) {
            amount = amount - 2 > target ? amount - 2 : target;
        }
        if (view->mode == EIDOS_VIEW_WORLD) {
            view->blur = amount;
        } else {
            amount = 0;
        }
    }
    if (g_released) {
        g_down_x = g_down_y = -1;
    }
    g_text_wanted = g_want_text;
    g_objs_wanted = g_want_objs;
    bw_input_set_host_mask(g_take);
    /* a phone's taller hint bar lies over the world, as on a computer: the
     * world keeps the whole window, so it does not move as bars come and
     * go (view->bottom stays 0) */
    view->bottom = 0;
    ring_apply(&g_ctx);
    fade_apply(&g_canvas);
    keep_prev(&g_canvas);
    *canvas = &g_canvas;
}
