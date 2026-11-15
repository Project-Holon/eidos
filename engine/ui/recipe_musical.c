/*
 * engine/ui/recipe_musical.c -- the one-screen recipe for the Pokemon
 * Musical's dress-up (#44): overlay 133 (src/game/ov133/dup_fitting.c),
 * run by MUSICAL_DRESSUP_SYS (ov010/musical_dressup_sys.c).
 *
 * The dressing room fills the window: the game's lower screen (the stage,
 * the Pokemon, its prop ring, the OK button) is the world screen at a whole
 * scale, and the room round it is the game's own pink pattern, drawn by
 * the interface in the same pixels (the canvas at the DS scale), so there
 * is no frame.  The stage's own backdrop is part of its 3D scene, so the
 * room (the 2D pattern under it) meets the stage at a visible edge.  Under
 * the stage, the game's own message in the style's text box; at the foot
 * the hint bar's strip, empty.  The pointer is the stylus on the stage.
 *
 * The game reads only the touch screen here (DUP_FITTING_HandleTouch and
 * DUP_FITTING_HandlePickTouch: no keys at all), so there are no key hints;
 * pad and keyboard controls are still to come.
 */
#include <stdint.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "ui_lang.h"
#include "dsmem.h"
#include "input.h"
#include "textbox.h"
#include "overlay.h"

#if defined(__GNUC__) || defined(__clang__)
#define MU_WEAK __attribute__((weak))
#else
#define MU_WEAK
#endif

#define OV_DRESSUP 133u

/* DupFittingWork (ov133/dup_fitting_types.h) */
#define DW_WINDOW_SHOWN 0x18D0u   /* the message window is up                */
#define DW_WINDOW       0x18ECu   /* its BmpWin                              */
/* MusicalDressupWork (ov010/musical_dressup_sys.c): the fitting's work */
#define MW_UI           0x04u

/* the room under the stage: the message box and the hint bar */
#define MU_BOX_H  44
#define MU_ROWS   (192 + MU_BOX_H + 17 + 6)

extern void *bw_dsref__GameSystemGlobal MU_WEAK;
/* engine/g2d/g2d.h: the main engine's screen at DS size, without the 3D */
extern int g2d_hw_main_graphics_dsmem(const u8 *rgba3d, u8 *out) MU_WEAK;

static u32 rd32(const void *p, u32 off)
{
    u32 v;

    memcpy(&v, (const u8 *)p + off, 4);
    return v;
}

static void *rdp(const void *p, u32 off)
{
    return (void *)(uintptr_t)rd32(p, off);
}

static int gptr_ok(const void *p)
{
    return p != NULL && bw_ds_game_ptr(p) && ((uintptr_t)p & 3u) == 0;
}

/* The fitting's work, through the proc on top (GameSystem: procSys 0x08;
 * ProcSys: current 0x08; Proc: work 0x14), or NULL. */
static void *dressup_work(void)
{
    void *gs, *ps, *cur, *mw, *dw;

    if (!bw_overlay_is_loaded(OV_DRESSUP) || &bw_dsref__GameSystemGlobal == NULL
        || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    memcpy(&gs, bw_dsref__GameSystemGlobal, sizeof gs);
    if (!gptr_ok(gs) || !gptr_ok(ps = rdp(gs, 0x08)) || !gptr_ok(cur = rdp(ps, 0x08))
        || !gptr_ok(mw = rdp(cur, 0x14)) || !gptr_ok(dw = rdp(mw, MW_UI))) {
        return NULL;
    }
    return dw;
}

/* ------------------------------------------------------------------ */
/* The pattern                                                         */
/* ------------------------------------------------------------------ */

/* The stage's pink pattern repeats every 64 pixels each way.  The game
 * shows the main engine (the one with the 3D) on the lower screen here, and
 * the 3D covers the whole stage, so the screen the interface gets
 * (EidosUiFrame.screen_rgba) holds the compositor's placeholder colour
 * there; the pattern comes from the main engine's screen without the 3D.
 * One clean repeat (each pixel the median of all its repeats on the
 * screen, so the sparkles and the props drop out), laid out over a whole
 * screen-sized buffer for ui_blit_screen.  A pixel with no clean sample
 * keeps last frame's colour. */
static u8 g_pat[256 * 192 * 4];
static u8 g_flat[256 * 192 * 4];
static int g_pat_ok;

/* The fade the world view gets (engine/ui/ui.c): the main engine's master
 * brightness, or its BLDY, down (dark > 0) or up (dark < 0), in 16ths. */
static int main_fade(void)
{
    u32 mb = bw_io_read16(0x0400006Cu), bld = (bw_io_read16(0x04000050u) >> 6) & 3u, f;

    if (((mb >> 14) & 3u) != 0u) {
        f = mb & 0x1Fu;
        f = f > 16u ? 16u : f;
        return ((mb >> 14) & 3u) == 2u ? (int)f : ((mb >> 14) & 3u) == 1u ? -(int)f : 0;
    }
    if (bld == 2u || bld == 3u) {
        f = bw_io_read16(0x04000054u) & 0x1Fu;
        f = f > 16u ? 16u : f;
        return bld == 3u ? (int)f : -(int)f;
    }
    return 0;
}

static void pattern_fade(int fade)
{
    size_t i;

    if (fade == 0) {
        return;
    }
    for (i = 0; i < sizeof g_pat; i += 4) {
        int c;

        for (c = 0; c < 3; c++) {
            int v = g_pat[i + (size_t)c];

            g_pat[i + (size_t)c] = (u8)(fade > 0 ? v - v * fade / 16 : v + (255 - v) * -fade / 16);
        }
    }
}

static void pattern_make(const u8 *scr)
{
    static u8 tile[64 * 64 * 4];
    int u, v, x, y;

    for (v = 0; v < 64; v++) {
        for (u = 0; u < 64; u++) {
            const u8 *s[12];
            int n = 0, i, j;

            for (y = v; y < 192; y += 64) {
                for (x = u; x < 256; x += 64) {
                    if (x >= 14 && x < 242 && n < 12) {
                        const u8 *p = scr + ((size_t)y * 256 + (size_t)x) * 4;

                        if (p[0] + p[1] + p[2] < 640) {        /* not the sparkles' whites */
                            s[n++] = p;
                        }
                    }
                }
            }
            for (i = 1; i < n; i++) {                           /* by brightness */
                for (j = i; j > 0 && s[j][0] + s[j][1] + s[j][2] < s[j - 1][0] + s[j - 1][1] + s[j - 1][2]; j--) {
                    const u8 *t = s[j];

                    s[j] = s[j - 1];
                    s[j - 1] = t;
                }
            }
            if (n > 0) {
                memcpy(tile + ((size_t)v * 64 + (size_t)u) * 4, s[n / 2], 4);
            }
        }
    }
    for (y = 0; y < 192; y++) {
        for (x = 0; x < 256; x++) {
            memcpy(g_pat + ((size_t)y * 256 + (size_t)x) * 4, tile + ((size_t)(y % 64) * 64 + (size_t)(x % 64)) * 4, 4);
        }
    }
    g_pat_ok = 1;
}

/* The pattern over the canvas rectangle (x, y, w, h), in phase with the
 * stage whose top-left is (ox, oy). */
static void pattern_fill(EidosUiCtx *c, int ox, int oy, int x, int y, int w, int h)
{
    int yy, xx;

    for (yy = y; yy < y + h;) {
        int sy = ((yy - oy) % 64 + 64) % 64, bh = 192 - sy < y + h - yy ? 192 - sy : y + h - yy;

        for (xx = x; xx < x + w;) {
            int sx = ((xx - ox) % 64 + 64) % 64, bw = 256 - sx < x + w - xx ? 256 - sx : x + w - xx;

            ui_blit_screen(c, xx, yy, g_pat, sx, sy, bw, bh, 1, 0);
            xx += bw;
        }
        yy += bh;
    }
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int musical_match(void)
{
    if (dressup_work() == NULL) {
        return 0;
    }
    eidos_ui_want_ds_scale_rows(MU_ROWS);
    return 1;
}

static void musical_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    void *dw = dressup_work();
    int cw = c->canvas->w, ch = c->canvas->h, sx = (cw - 256) / 2, sy = 0, mx, my;

    if (dw == NULL) {
        return;
    }
    /* the stage: the game's lower screen as the world, its own height in canvas pixels */
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 1;
    view->ds_only = 1;
    view->lower_only = 1;
    view->bottom = (ch - 192) * c->canvas->k;
    /* made again each frame, so the room fades in and out with the stage */
    if (g2d_hw_main_graphics_dsmem != NULL && g2d_hw_main_graphics_dsmem(NULL, g_flat) == 0) {
        pattern_make(g_flat);
        pattern_fade(main_fade());
    }
    if (g_pat_ok) {
        /* the room round the stage, and over the DS's own border of dots at its sides */
        pattern_fill(c, sx, sy, 0, 0, sx + 14, 192);
        pattern_fill(c, sx, sy, sx + 242, 0, cw - (sx + 242), 192);
        pattern_fill(c, sx, sy, 0, 192, cw, ch - 192);
    }
    /* the game's message, in the style's box */
    /* (the game prints every line of the dress-up, the opening, a prop's
     * name and the help, in this one window on the upper screen, which the
     * one screen does not show) */
    if (rd32(dw, DW_WINDOW_SHOWN) != 0 && gptr_ok(rdp(dw, DW_WINDOW))) {
        const void *win = rdp(dw, DW_WINDOW);
        int bh = ui_textbox_h(win);

        if (bh > 0) {
            /* under the stage (a style's band goes to the bottom edge) */
            ui_textbox_draw(c, UI_TEXTBOX_NARRATION, sx, 192 + 3, 256, win, -1, -1);
        }
    }
    /* the bar without hints: the game takes no keys here (see the top) */
    ui_hints(c, ch - ui_bar_h(c), cw, NULL, 0, NULL);
    /* the pointer is the stylus on the stage */
    if (eidos_ui_held() && eidos_ui_mouse_pos(&mx, &my) && mx >= sx && mx < sx + 256 && my >= sy && my < sy + 192) {
        eidos_ui_tap(mx - sx, my - sy, 2);
    }
}

static void musical_reset(void)
{
    g_pat_ok = 0;
}

static const EidosUiRecipe recipe_musical = {
    "musical_dressup", musical_match, musical_run, musical_reset, 0
};

const EidosUiRecipe *const ui_recipes_musical[] = { &recipe_musical };
const u32 ui_recipes_musical_count = 1;
