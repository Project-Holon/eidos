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
#include "dsmem.h"
#include "input.h"
#include "settings.h"

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

void eidos_ui_set_enabled(int on)
{
    if (on != g_enabled) {
        g_enabled = on;
        eidos_ui_reset();
        bw_input_set_host_mask(0);      /* every button the game's again */
    }
}

int eidos_ui_scale(int win_w, int win_h)
{
    /* the layouts need about 270 canvas rows (the menu's six buttons, the
     * battle's plates and sheets): the largest whole scale that keeps them */
    int k = win_h / 270;

    (void)win_w;
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
static u32 game_darkness(void)
{
    u32 e, best = 0;

    for (e = 0; e < 2; e++) {
        u32 base = e == 0 ? 0x04000000u : 0x04001000u;
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
    u32 dark = game_darkness();

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
        if (g_before != NULL && g_before_w == g_canvas.w && g_before_h == g_canvas.h) {
            fade_start();               /* from the app to the scene before */
        }
    } else if (g_leaving && dark < g_dark_prev && !app_ending()) {
        g_leaving = 0;                  /* it fades back in: the same app */
        fade_start();
    }
    g_dark_prev = dark;
    if (g_leaving) {
        /* the modal's last picture stays, over the blurred world, until the
         * next scene is up and crossfades from it: no empty frames while
         * the game fades and loads */
        if (g_before != NULL && g_before_w == g_canvas.w && g_before_h == g_canvas.h) {
            /* the scene the app goes back to (the menu), at once, over the
             * world as it was: the blur eases out under it */
            memcpy(g_canvas.rgba, g_before, (size_t)g_canvas.w * (size_t)g_canvas.h * 4);
            view->blur = 0;
        } else {
            eidos_ui_hold_canvas();
            view->blur = 1;
        }
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
 * game's thread through the game's own functions. */
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

static void debug_party(void)
{
    static int done;
    const char *list = getenv("EIDOS_DEBUG_PARTY");
    u8 *gs, *gd;
    void *party;
    u32 n, i, id;
    u16 lv;

    if (done || list == NULL || &bw_dsref__GameSystemGlobal == NULL || GAME_DATA_GetMyParty == NULL
        || POKE_TOOL_SetupPartyParam == NULL || POKEPARTY_GetMember == NULL) {
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
    for (i = 0; i < n && *list != '\0'; i++) {
        u16 sp = (u16)strtoul(list, (char **)&list, 10);

        if (sp != 0) {
            POKE_TOOL_SetupPartyParam(POKEPARTY_GetMember(party, i), sp, lv, id);
            fprintf(stderr, "eidos: debug party slot %u = species %u lv %u\n", (unsigned)i, (unsigned)sp, (unsigned)lv);
        }
        while (*list == ',' || *list == ' ') {
            list++;
        }
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

/* EIDOS_DEBUG_BATTLE="wild SPECIES LEVEL" or "trainer ID" (the spot
 * editor's Preview in game, tools/spot-editor; EIDOS_DEBUG_FOES replaces
 * the trainer's team, as EIDOS_DEBUG_PARTY the player's): starts that battle through
 * the game's own functions once the field has run 90 frames with no event
 * (after EIDOS_DEBUG_WARP's arrival when both are set). */
extern void *ov021_FIELDMAP_GetEncount(void *fieldMap) __attribute__((weak));
extern void *ov021_FIELD_ENCOUNT_StartScriptedWild(void *encount, u16 species, u8 level, u32 flags) __attribute__((weak));
extern void ov021_FIELD_ENCOUNT_SetupTrainerBattle(void *encount, void *btlSetup, u32 rule, u32 partnerId, u32 trainerId1,
                                                   u32 trainerId2, u16 heapId) __attribute__((weak));
extern void *BTL_SETUP_Create(u32 heapId) __attribute__((weak));
extern void *ov010_EVENT_BATTLE_CreateScriptedBattle(void *gsys, void *fieldMap, void *btlSetup, int returnImmediately,
                                                     int effectId) __attribute__((weak));
extern int ov010_EVENT_TRAINER_IsDoubleBattle(int trainerId) __attribute__((weak));

static void debug_battle(void)
{
    static int done, wait;
    const char *what = getenv("EIDOS_DEBUG_BATTLE");
    unsigned a1 = 0, a2 = 5;
    u8 *gs;
    void *fm, *enc, *ev = NULL;

    if (done || what == NULL || &bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_SetEvent == NULL
        || GAMESYSTEM_GetEvent == NULL || ov021_FIELDMAP_GetEncount == NULL) {
        return;
    }
    gs = *(u8 **)bw_dsref__GameSystemGlobal;
    fm = gs != NULL ? *(void **)(gs + 0x20) : NULL;
    if (fm == NULL || GAMESYSTEM_GetEvent(gs) != NULL) {
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
    } else if (sscanf(what, "trainer %u", &a1) == 1 && BTL_SETUP_Create != NULL
               && ov021_FIELD_ENCOUNT_SetupTrainerBattle != NULL && ov010_EVENT_BATTLE_CreateScriptedBattle != NULL) {
        void *bs = BTL_SETUP_Create(4);
        int dbl = ov010_EVENT_TRAINER_IsDoubleBattle != NULL && ov010_EVENT_TRAINER_IsDoubleBattle((int)a1);

        ov021_FIELD_ENCOUNT_SetupTrainerBattle(enc, bs, dbl ? 1u : 0u, 0, a1, 0, 4);
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
        fprintf(stderr, "eidos: debug battle: %s\n", what);
    }
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
    debug_party();
    debug_warp();
    debug_battle();

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
static int screen_with_3d(void)
{
    return (bw_io_read16(0x04000304u) & 0x8000u) != 0 ? 0 : 1;
}

/*
 * For checks only: EIDOS_UI_MOUSE scripts the mouse in window pixels, a
 * comma-separated list of events "frame:x:y" (move there and click: down
 * that frame, up two frames later), "frame:x:y:m" (move only, a hover) and
 * "frame:x:y:w+N" / "w-N" (wheel steps).  The real mouse is ignored while
 * the script has events left.
 */
typedef struct ScriptedMouse { u32 frame; int x, y, kind, wheel; } ScriptedMouse;
static ScriptedMouse g_script[256];
static int g_script_n = -1;
static int g_sx = -1, g_sy = -1;
static u32 g_release_at;

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
    } else if (g_release_at != 0) {
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
            }
        }
        if (g_script[i].frame >= frame) {
            any = 1;
        }
    }
    return any || g_release_at != 0 || g_sx >= 0;
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

void eidos_ui_frame(const EidosUiFrame *in, EidosUiView *view,
                    const EidosUiCanvas **canvas)
{
    const EidosUiRecipe *r = NULL;
    int w, h;
    u32 i;

    eidos_ui_mark_active();          /* settings.h: for game-side patches */
    g_k = eidos_ui_scale(in->win_w, in->win_h);
    /* rounded up: the canvas reaches the window's right and bottom edges
     * (a bar along the bottom sits on the edge, not k - 1 pixels above it) */
    w = (in->win_w + g_k - 1) / g_k;
    h = (in->win_h + g_k - 1) / g_k;
    ui_begin(&g_ctx, &g_canvas, w, h, g_k);
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
    if (r != NULL && r->round_scale) {
        int k = (in->win_h + 135) / 270;

        if (k > g_k) {
            g_k = k;
            w = (in->win_w + g_k - 1) / g_k;
            h = (in->win_h + g_k - 1) / g_k;
            ui_begin(&g_ctx, &g_canvas, w, h, g_k);
            read_mouse(in);
        }
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
            /* an app closed (its proc is ending or gone, its recipe let
             * go): the scene it goes back to (the menu) shows at once, and
             * stays while the game rebuilds the field, over a sharp world */
            if (((g_last_view.blur && app_ending()) || g_leaving) && g_before != NULL &&
                g_before_w == g_canvas.w && g_before_h == g_canvas.h) {
                if (!g_leaving) {
                    g_leaving = 1;
                    fade_start();
                }
                memcpy(g_canvas.rgba, g_before, (size_t)g_canvas.w * (size_t)g_canvas.h * 4);
                view->blur = 0;
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
    ring_apply(&g_ctx);
    fade_apply(&g_canvas);
    keep_prev(&g_canvas);
    *canvas = &g_canvas;
}
