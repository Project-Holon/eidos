/*
 * engine/ui/recipe_title.c -- the one-screen recipes for the title screen and
 * the title menu (engine/ui/ui.h, docs/unified-view-recipes.md section 15).
 *
 * Both scenes run on the global proc stack (g_ProcSys), overlay 88:
 *
 *   Title screen  ProcData 0x021BD574.  The attract loop: Reshiram's 3D on
 *                 the screen that carries it (the loop swaps the two DS
 *                 screens, so the recipe always shows the 3D screen as the
 *                 world), the logo and PRESS START on the other screen.  The
 *                 recipe fills the window with the 3D, draws the logo from
 *                 the game's files (faded as the other screen fades it), and draws
 *                 PRESS START on a dark plate that blinks like the game's.
 *                 A click anywhere is START.
 *   Title menu    ProcData 0x021BD66C.  The menu's rows are the proc's own
 *                 list (StartMenuWork.rowIds), the cursor is its cursorIndex.
 *                 The game has no touch here, so a click on a row is the
 *                 presses the game wants: UP or DOWN until the cursor is on
 *                 the row, then A.  The CONTINUE card shows the save (name,
 *                 play time, badges, Pokedex, the party) and the player's
 *                 overworld figure, which walks while the mouse is over the
 *                 card.
 *
 * Everything shown is read from the game's own structs (32-bit layouts, the
 * offsets are in the comments); a missing accessor leaves its part out.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "ui_lang.h"
#include "theme.h"
#include "input.h"
#include "choice_list.h"
#include "textbox.h"
#include "g2d.h"
#include "port.h"
#include "settings.h"
#include "avatar.h"

#if defined(__GNUC__) || defined(__clang__)
#define TT_WEAK __attribute__((weak))
#else
#define TT_WEAK
#endif

#define CUT_DEF (UI_CUT_TL | UI_CUT_BR)

/* The heroine's figure in the kit (assets_sprites.c); the hero is 0. */
#define SPRITE_HERO     0
#define SPRITE_HEROINE  33

/* ------------------------------------------------------------------ */
/* The game's data                                                     */
/* ------------------------------------------------------------------ */

/* the global proc system: a variable of the game's own since aletheia
 * 6c2aae879f defines it, the image's word through its pointer before */
extern void *g_ProcSys TT_WEAK;
extern void *bw_dsref_g_ProcSys TT_WEAK;
extern void *bw_dsref_ov088_d_021BD574 TT_WEAK;     /* title ProcData      */
extern void *bw_dsref_ov088_d_021BD66C TT_WEAK;     /* menu ProcData       */
extern void *bw_dsref_ov088_d_0209DF9C TT_WEAK;     /* intro cutscene ProcData */
extern void *bw_dsref__GameSystemGlobal TT_WEAK;
extern int bw_overlay_is_loaded(u32 id) TT_WEAK;

extern void  *SAVE_CONTROL_Get(void) TT_WEAK;
extern void  *SYSTEM_DATA_GetMyStatus(void *save) TT_WEAK;
extern void  *SYSTEM_DATA_GetPlayTime(void *save) TT_WEAK;
extern const u16 *MYSTATUS_GetName(const void *status) TT_WEAK;
extern u32    MYSTATUS_GetSex(const void *status) TT_WEAK;
extern u16    PLAYTIME_GetHours(const void *t) TT_WEAK;
extern u8     PLAYTIME_GetMinutes(const void *t) TT_WEAK;
extern u32    MISC_CountBadges(void *misc) TT_WEAK;
extern u8     MISC_GetSlotState(void *misc, unsigned slot) TT_WEAK;
extern void  *ZUKAN_SAVE_GetBlock(void *save) TT_WEAK;
extern u32    ZUKAN_SAVE_CountSeen(void *zukan, u32 heap) TT_WEAK;
extern u32    ZONEDATA_CheckEventFlag(void *ev, s32 flag) TT_WEAK;
extern void  *POKEPARTY_GetSaveData(void *save) TT_WEAK;
extern u32    POKEPARTY_GetCount(const void *party) TT_WEAK;
extern void  *POKEPARTY_GetMember(void *party, u32 pos) TT_WEAK;
extern u32    POKE_TOOL_GetPartyValue(void *pp, u32 param, u32 arg) TT_WEAK;

/* The port's title-menu hooks (sdk/mod_title.h, sdk/save_slots.h). */
extern int  bw_mod_title_settings_on(void) TT_WEAK;
extern int  bw_mod_title_in_settings(void) TT_WEAK;
extern void bw_slots_label(char *out, u32 size) TT_WEAK;
extern int  bw_slots_count(void) TT_WEAK;

/* Proc (arm9/procsys.h): data 0x00, overlayId 0x04, seq 0x08, subSeq 0x0C,
 * procWork 0x10, work 0x14.  ProcSys: current 0x08. */
typedef struct TtProc {
    const void *data;
    u32 overlayId, seq, subSeq;
    void *procWork, *work;
} TtProc;

#define TT_SCENE_NONE   0
#define TT_SCENE_TITLE  1
#define TT_SCENE_MENU   2

/* StartMenuWork (ov088/startmenu_types.h) */
#define SM_SAVE         0x00    /* void *saveData                          */
#define SM_PLAYER       0x04    /* void *playerData (MyStatus)             */
#define SM_UNLOCK       0x08    /* void *unlockData (the MISC block)       */
#define SM_EVENT        0x10    /* void *eventWork                         */
#define SM_ROWS         0x120   /* u8 rowIds[8]                            */
#define SM_CURSOR       0x128   /* u8 cursorIndex                          */
#define SM_STATE        0x13C   /* int state                               */

/* The menu's states (docs/unified-view-recipes.md section 15). */
#define SM_STATE_SELECT 5
#define SM_STATE_SCROLL 6


static TtProc *root_proc(void)
{
    void *sys;

    if (&g_ProcSys != NULL) {
        sys = g_ProcSys;
    } else if (&bw_dsref_g_ProcSys != NULL && bw_dsref_g_ProcSys != NULL) {
        sys = *(void **)bw_dsref_g_ProcSys;
    } else {
        return NULL;
    }
    if (sys == NULL) {
        return NULL;
    }
    return *(TtProc **)((char *)sys + 0x08);
}

static int addr_is(const void *p, void *const *ref, uintptr_t ds)
{
    if (p == NULL) {
        return 0;
    }
    if (ref != NULL && *ref != NULL) {
        return p == *ref;
    }
    return (uintptr_t)p == ds;
}

static int scene(TtProc **out)
{
    TtProc *p = root_proc();
    void *const *t = &bw_dsref_ov088_d_021BD574 != NULL ? &bw_dsref_ov088_d_021BD574 : NULL;
    void *const *m = &bw_dsref_ov088_d_021BD66C != NULL ? &bw_dsref_ov088_d_021BD66C : NULL;

    if (p == NULL || p->overlayId != 88) {
        return TT_SCENE_NONE;
    }
    if (out != NULL) {
        *out = p;
    }
    if (addr_is(p->data, t, 0x021BD574u)) {
        return TT_SCENE_TITLE;
    }
    if (addr_is(p->data, m, 0x021BD66Cu)) {
        return TT_SCENE_MENU;
    }
    return TT_SCENE_NONE;
}

static u8 rd8(const void *base, u32 off)
{
    return *((const u8 *)base + off);
}

static void *rdp(const void *base, u32 off)
{
    return *(void *const *)((const char *)base + off);
}

/* ------------------------------------------------------------------ */
/* Shared drawing                                                      */
/* ------------------------------------------------------------------ */

static int tt_white(void);      /* the game is Pokemon White (below, with the backdrop) */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The logo, read from the game's files: the title's logo layer (overlay 88's
 * STARTMENU_LogoInit) is archive a/0/2/6, member 0 the 256-colour tiles,
 * member 1 the screen, member 2 the palette.  The layer sits at (0, 0) on
 * its DS screen, so the picture lines up with that screen's pixels (the
 * fade below compares the two).  Colour 0 is the backdrop. */
#define LOGO_ARC "a/0/2/6"
#define LOGO_W   256
#define LOGO_H   192

static u8 g_logo_px[LOGO_W * LOGO_H * 4];
static UiImage g_logo = { 0, 0, g_logo_px };
static int g_logo_have;         /* 0 not tried, 1 loaded, -1 failed */

static void logo_load(void)
{
    BwArcFile arc;
    u8 *chr = NULL, *scr = NULL, *pal = NULL;
    u32 chr_n = 0, scr_n = 0, pal_n = 0;
    G2dNcgr ncgr;
    G2dNscr nscr;
    G2dPalette colors = { NULL, 0 };
    int have_ncgr = 0, have_nscr = 0;
    u32 tx, ty;
    static u32 gen;

    if (gen != eidos_ui_data_generation()) {
        gen = eidos_ui_data_generation();   /* the other version's logo */
        g_logo_have = 0;
    }
    if (g_logo_have != 0) {
        return;
    }
    g_logo_have = -1;
    if (bw_arc_open(&arc, LOGO_ARC) != 0) {
        return;
    }
    chr = g2d_arc_member(&arc, 0, &chr_n);
    scr = g2d_arc_member(&arc, 1, &scr_n);
    pal = g2d_arc_member(&arc, 2, &pal_n);
    bw_arc_close(&arc);
    have_ncgr = chr != NULL && g2d_ncgr_parse(&ncgr, chr, chr_n) == 0;
    have_nscr = scr != NULL && g2d_nscr_parse(&nscr, scr, scr_n) == 0;
    if (have_ncgr && have_nscr && pal != NULL && g2d_palette_append_nclr(&colors, pal, pal_n) == 0
        && ncgr.bytes_per_tile == 64) {
        memset(g_logo_px, 0, sizeof g_logo_px);
        for (ty = 0; ty < nscr.tiles_high && ty * 8 < LOGO_H; ty++) {
            for (tx = 0; tx < nscr.tiles_wide && tx * 8 < LOGO_W; tx++) {
                u16 e = nscr.entries[ty * nscr.tiles_wide + tx];
                u8 px[64];
                int i;

                g2d_bg_tile(&ncgr, G2D_SCR_TILE(e), G2D_SCR_HFLIP(e), G2D_SCR_VFLIP(e), px);
                for (i = 0; i < 64; i++) {
                    u8 *q = g_logo_px + ((size_t)(ty * 8 + (u32)(i / 8)) * LOGO_W
                                         + tx * 8 + (u32)(i % 8)) * 4;

                    if (px[i] != 0 && px[i] < colors.count) {
                        q[0] = colors.colors[px[i]].r;
                        q[1] = colors.colors[px[i]].g;
                        q[2] = colors.colors[px[i]].b;
                        q[3] = 255;
                    }
                }
            }
        }
        g_logo.w = LOGO_W;
        g_logo.h = LOGO_H;
        g_logo_have = 1;
    }
    if (have_ncgr) {
        g2d_ncgr_free(&ncgr);
    }
    if (have_nscr) {
        g2d_nscr_free(&nscr);
    }
    g2d_palette_free(&colors);
    free(chr);
    free(scr);
    free(pal);
}

/* How bright the screen's logo is now against the stored copy, 0 to 255:
 * the game fades the screens in and out. */
static int logo_fade(const u8 *screen)
{
    long cur = 0, ref = 0;
    int x, y;

    if (screen == NULL || g_logo_have != 1) {
        return 255;
    }
    for (y = 0; y < LOGO_H; y += 2) {
        for (x = 0; x < LOGO_W; x += 2) {
            const u8 *q = g_logo_px + ((size_t)y * LOGO_W + (size_t)x) * 4;
            const u8 *p = screen + ((size_t)y * 256 + (size_t)x) * 4;

            if (q[3] != 0) {
                ref += (long)q[0] + q[1] + q[2];
                cur += (long)p[0] + p[1] + p[2];
            }
        }
    }
    if (ref <= 0) {
        return 255;
    }
    cur = cur * 255 / ref;
    return cur > 255 ? 255 : (int)cur;
}

/* The logo's box in the source: the rows and columns that hold pixels. */
static int logo_bounds(int *x0, int *y0, int *x1, int *y1)
{
    int x, y, a = LOGO_W, b = LOGO_H, c = -1, d = -1;

    logo_load();
    if (g_logo_have != 1) {
        return 0;
    }
    for (y = 0; y < LOGO_H; y++) {
        for (x = 0; x < LOGO_W; x++) {
            if (g_logo_px[((size_t)y * LOGO_W + (size_t)x) * 4 + 3] != 0) {
                a = x < a ? x : a;
                b = y < b ? y : b;
                c = x > c ? x : c;
                d = y > d ? y : d;
            }
        }
    }
    if (c < a || d < b) {
        return 0;
    }
    *x0 = a;
    *y0 = b;
    *x1 = c + 1;
    *y1 = d + 1;
    return 1;
}

/* Draw the logo scaled to width w (a box filter, so it stays crisp), top
 * left at (x, y).  Returns the height drawn. */
static int logo_draw(EidosUiCtx *c, int x, int y, int w, int alpha)
{
    static u8 out[400 * 260 * 4];
    /* what `out` holds: the logo is the same picture frame after frame, so
     * it is scaled again only when its place in the source, its size or its
     * alpha changes */
    static int c_bx0 = -1, c_by0, c_sw, c_sh, c_dw, c_dh, c_alpha;
    static u32 c_gen;       /* (or the game's data: the two versions' logos have the same box) */
    UiImage im;
    int bx0, by0, bx1, by1, sw, sh, dw, dh, ox, oy;

    if (!logo_bounds(&bx0, &by0, &bx1, &by1)) {
        return 0;
    }
    sw = bx1 - bx0;
    sh = by1 - by0;
    dw = clampi(w, 16, 400);
    dh = sh * dw / sw;
    if (dh > 260) {
        dh = 260;
    }
    if (c_bx0 != bx0 || c_by0 != by0 || c_sw != sw || c_sh != sh
        || c_dw != dw || c_dh != dh || c_alpha != alpha || c_gen != eidos_ui_data_generation()) {
        c_gen = eidos_ui_data_generation();
        for (oy = 0; oy < dh; oy++) {
            for (ox = 0; ox < dw; ox++) {
                int sx0 = bx0 + ox * sw / dw, sx1 = bx0 + (ox + 1) * sw / dw;
                int sy0 = by0 + oy * sh / dh, sy1 = by0 + (oy + 1) * sh / dh;
                int xx, yy, n = 0, na = 0, r = 0, g = 0, b = 0;
                u8 *q = out + ((size_t)oy * (size_t)dw + (size_t)ox) * 4;

                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                for (yy = sy0; yy < sy1; yy++) {
                    for (xx = sx0; xx < sx1; xx++) {
                        const u8 *p = g_logo_px + ((size_t)yy * LOGO_W + (size_t)xx) * 4;

                        n++;
                        if (p[3] != 0) {
                            na++;
                            r += p[0];
                            g += p[1];
                            b += p[2];
                        }
                    }
                }
                if (na == 0) {
                    q[0] = q[1] = q[2] = q[3] = 0;
                } else {
                    q[0] = (u8)(r / na);
                    q[1] = (u8)(g / na);
                    q[2] = (u8)(b / na);
                    q[3] = (u8)(255 * na / n * alpha / 255);
                }
            }
        }
        c_bx0 = bx0;
        c_by0 = by0;
        c_sw = sw;
        c_sh = sh;
        c_dw = dw;
        c_dh = dh;
        c_alpha = alpha;
    }
    im.w = dw;
    im.h = dh;
    im.rgba = out;
    ui_image(c, x, y, &im, 1);
    return dh;
}

/* ------------------------------------------------------------------ */
/* The title screen                                                    */
/* ------------------------------------------------------------------ */

static int title_match(void)
{
    return scene(NULL) == TT_SCENE_TITLE;
}

/* Whether the game's own PRESS START shows on the other screen now: bright
 * pixels in the band it uses (y 158 to 178). */
static int game_press_start_visible(const u8 *screen)
{
    int x, y, n = 0;

    if (screen == NULL) {
        return -1;
    }
    for (y = 160; y < 176; y++) {
        for (x = 80; x < 176; x++) {
            const u8 *p = screen + ((size_t)y * 256 + (size_t)x) * 4;

            if (p[0] > 200 && p[1] > 200 && p[2] > 200) {
                n++;
            }
        }
    }
    return n > 30;
}

static int g_title_seen;        /* frames since the title began            */
static int g_title_passed;      /* the title has been up this run           */
static int g_ps_last;           /* frame + 1 when the game's PRESS START last showed */
static int g_ps_ref = -1;       /* the frame it last came on */
static int g_ps_prev;
static int g_started;           /* START was pressed: the prompt holds still */
static u32 g_started_at;

/* The title's scene is built for the DS's 4:3 frame: a window taller than
 * that (an iPad held upright) would show past it, below Reshiram.  There
 * the view zooms in until the frame's height fills the window, trimming
 * the sides instead (2.0 at most, the world's largest zoom). */
static float title_zoom(const EidosUiFrame *in)
{
    float a = in->win_h > 0 ? (float)in->win_w / (float)in->win_h : 0.0f;
    float z = a > 0.0f && a < 4.0f / 3.0f ? (4.0f / 3.0f) / a : 1.0f;

    return z > 2.0f ? 2.0f : z;
}

static void title_reset(void)
{
    g_started = 0;
    g_title_seen = 0;
    g_ps_last = 0;
    g_ps_ref = -1;
    g_ps_prev = 0;
}

static void title_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    const u8 *other = in->screen_rgba[1 - (view->world_screen ? 1 : 0)];
    EidosUiCanvas *cv = c->canvas;
    int W = cv->w, H = cv->h, m, lw, y, vis, hh;
    static const char *const hints[] = { "START", "Start" };
    static const u32 keys[] = { BW_KEY_START };
    EidosUiRect all;
    char *dbg;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = title_zoom(in);
    view->blur = 0;
    view->hide_2d = 1;
    g_title_seen++;
    g_title_passed = 1;

    /* the logo, faded as the screen that is not the 3D one fades it */
    logo_load();
    m = clampi(W / 28, 6, 22);
    lw = clampi(W * 30 / 100, 100, 200);
    logo_draw(c, m, m, lw, logo_fade(other));

    /* PRESS START: the game's blink (visible 64 frames, hidden 32) while its
     * band shows it; after the attract swap the band is empty and the same
     * rhythm goes on from the last time it came on */
    vis = game_press_start_visible(other);
    if (vis > 0 && g_ps_prev <= 0) {
        g_ps_ref = (int)in->frame;
    }
    if (vis >= 0) {
        g_ps_prev = vis;
    }
    if (vis > 0) {
        g_ps_last = (int)in->frame + 1;
    }
    {
        int on;

        if ((eidos_ui_keys_pressed() & (BW_KEY_START | BW_KEY_A)) != 0u) {
            g_started = 1;
            g_started_at = in->frame;
        }
        if (g_started && in->frame - g_started_at > 180u) {
            g_started = 0;      /* the title stayed: the press was too early */
        }
        if (g_started) {
            on = 1;             /* pressed: no more blinking on the way out */
        } else if (g_ps_last > 0 && (int)in->frame + 1 - g_ps_last < 40) {
            on = vis > 0;
        } else if (g_ps_ref >= 0) {
            on = (((int)in->frame - g_ps_ref) % 96 + 96) % 96 < 64;
        } else {
            on = 1;
        }

        if (on) {
            const char *s = "PRESS START";
            int tw = ui_text_w(s, UI_FONT_LARGE), pw = tw + 36, ph = 26;
            int px = (W - pw) / 2, themed;

            hh = 17;
            y = H - hh - ph - clampi(H / 10, 8, 30);
            themed = ui_app_window(c, "press-start", &px, &y, &pw, &ph);
            if (!themed) {
                ui_panel(c, px, y, pw, ph, CUT_DEF, 5, UI_BODY, UI_EDGE2, 215);
            }
            if (!themed) {
                /* the bar's top follows the cut corner, a pixel inside it
                 * (the panel's inside starts where column + row >= 8) */
                int bx;

                for (bx = 3; bx < 5; bx++) {
                    int top = 9 - bx > 3 ? 9 - bx : 3;

                    ui_fill(c, px + bx, y + top, 1, ph - 3 - top, UI_CYAN, 255);
                }
            }
            ui_text(c, px + (pw - tw) / 2, y + ui_text_y(ph, UI_FONT_LARGE), s,
                    UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        }
    }
    /* the credit, bottom left (dark ink on White's light floor) */
    if (tt_white()) {
        ui_text(c, 6, H - ui_bar_h(c) - 11, "Developed by GAME FREAK inc.", UI_RAW(0x303038u),
                (s32)UI_RAW(0xF4F4F4u), UI_FONT_SMALL, 1);
    } else {
        ui_text(c, 6, H - ui_bar_h(c) - 11, "Developed by GAME FREAK inc.", UI_LIGHT, 0x202020u,
                UI_FONT_SMALL, 1);
    }

    /* click anywhere is START */
    all.x = 0;
    all.y = 0;
    all.w = W;
    all.h = H - ui_bar_h(c);
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_START, 2);
        g_started = 1;
        g_started_at = in->frame;
    }
    ui_fill(c, 0, H - ui_bar_h(c), W, ui_bar_h(c), UI_BODY2, 255);   /* the 3D carries its own credit */
    ui_hints(c, H - ui_bar_h(c), W, hints, 1, keys);
    dbg = getenv("EIDOS_TITLE_DEBUG");
    if (dbg != NULL && ((in->frame % 30) == 0 || dbg[0] == '2')) {
        fprintf(stderr, "title: frame %u ws %d vis %d logo %d\n", in->frame,
                view->world_screen, vis, g_logo_have);
    }
}

static const EidosUiRecipe recipe_title = {
    "title", title_match, title_run, title_reset, 0
};

/* ------------------------------------------------------------------ */
/* The title menu                                                      */
/* ------------------------------------------------------------------ */

/* The row kinds (StartMenuWork.rowIds) and their labels; the game's own
 * list, in the order it built them, kind 8 is the blank. */
static const char *kind_label(int kind, int hook, int settings_row)
{
    switch (kind) {
    case 0: return "CONTINUE";
    case 1: return "NEW GAME";
    case 2: return "MYSTERY GIFT";
    case 3: return "BATTLE COMPETITION";
    case 4: return "GAME SYNC SETTINGS";
    case 5: return "NINTENDO WFC SETTINGS";
    case 6: return hook ? "MULTIPLAYER" : "MIC TEST";
    case 7: return hook && settings_row ? "SETTINGS" : "RELOCATOR";
    default: return "";
    }
}

typedef struct SaveInfo {
    int ok;
    char name[40];
    int hours, minutes, badges, seen, seen_on, sex;
    int nmon;
    struct { u16 species; u8 form, egg; } mon[6];
} SaveInfo;

static SaveInfo g_info;
static char g_info_key[32];
static u32 g_info_frame;
static void *g_info_work;

static void name_utf8(const u16 *src, char *out, int max)
{
    int i, n = 0;

    for (i = 0; i < 12 && src[i] != 0xFFFF && src[i] != 0 && n < max - 4; i++) {
        u16 ch = src[i];

        if (ch < 0x80) {
            out[n++] = (char)ch;
        } else if (ch < 0x800) {
            out[n++] = (char)(0xC0 | (ch >> 6));
            out[n++] = (char)(0x80 | (ch & 0x3F));
        } else {
            out[n++] = (char)(0xE0 | (ch >> 12));
            out[n++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    out[n] = '\0';
}

static void info_read(void *work)
{
    void *save = rdp(work, SM_SAVE), *player = rdp(work, SM_PLAYER);
    void *misc = rdp(work, SM_UNLOCK), *ev = rdp(work, SM_EVENT);
    SaveInfo *s = &g_info;
    const u16 *nm;
    void *t, *party;
    u32 i, n;

    memset(s, 0, sizeof *s);
    if (save == NULL || player == NULL || MYSTATUS_GetName == NULL) {
        return;
    }
    nm = MYSTATUS_GetName(player);
    if (nm != NULL) {
        name_utf8(nm, s->name, (int)sizeof s->name);
    }
    s->sex = MYSTATUS_GetSex != NULL ? (int)MYSTATUS_GetSex(player) : 0;
    if (SYSTEM_DATA_GetPlayTime != NULL && PLAYTIME_GetHours != NULL &&
        PLAYTIME_GetMinutes != NULL) {
        t = SYSTEM_DATA_GetPlayTime(save);
        if (t != NULL) {
            s->hours = PLAYTIME_GetHours(t);
            s->minutes = PLAYTIME_GetMinutes(t);
        }
    }
    if (MISC_CountBadges != NULL && misc != NULL) {
        s->badges = (int)MISC_CountBadges(misc);
    }
    if (ZONEDATA_CheckEventFlag != NULL && ev != NULL && ZUKAN_SAVE_GetBlock != NULL &&
        ZUKAN_SAVE_CountSeen != NULL) {
        s->seen_on = ZONEDATA_CheckEventFlag(ev, 0x962) == 1;
        if (s->seen_on) {
            s->seen = (int)ZUKAN_SAVE_CountSeen(ZUKAN_SAVE_GetBlock(save), 11);
        }
    }
    if (POKEPARTY_GetSaveData != NULL && POKEPARTY_GetCount != NULL &&
        POKEPARTY_GetMember != NULL && POKE_TOOL_GetPartyValue != NULL) {
        party = POKEPARTY_GetSaveData(save);
        if (party != NULL) {
            n = POKEPARTY_GetCount(party);
            for (i = 0; i < n && i < 6; i++) {
                void *pp = POKEPARTY_GetMember(party, i);

                if (pp == NULL) {
                    break;
                }
                s->mon[i].species = (u16)POKE_TOOL_GetPartyValue(pp, 0x05, 0);
                s->mon[i].form = (u8)POKE_TOOL_GetPartyValue(pp, 0x6F, 0);
                s->mon[i].egg = (u8)(POKE_TOOL_GetPartyValue(pp, 0x4C, 0) != 0);
                s->nmon++;
            }
        }
    }
    s->ok = 1;
}

/* The card's facts, read again when the save slot or the menu changes (only
 * while the menu waits for input: its heap is alive then). */
static const SaveInfo *info_get(void *work, const EidosUiFrame *in, int state)
{
    char key[32] = "";

    if (bw_slots_label != NULL) {
        bw_slots_label(key, sizeof key);
    }
    if ((work != g_info_work || strcmp(key, g_info_key) != 0 || !g_info.ok) &&
        state >= 4 && state <= 6) {
        g_info_work = work;
        memcpy(g_info_key, key, sizeof g_info_key);
        g_info_frame = in->frame;
        info_read(work);
    }
    return &g_info;
}

static int slot_count(void)
{
    return bw_slots_count != NULL ? bw_slots_count() : 1;
}

/* What a click asked of the menu, carried out one press at a time. */
#define ACT_A     0
#define ACT_LEFT  1
#define ACT_RIGHT 2
static int g_target = -1, g_act, g_wait, g_age;

static int g_mods_open, g_mods_cool;    /* (the mod manager, below) */
static int g_ver_open, g_ver_cool;      /* (the other version's window, below) */

static void menu_reset(void)
{
    g_mods_open = 0;
    g_mods_cool = 0;
    g_ver_open = 0;
    g_ver_cool = 0;
    g_target = -1;
    g_wait = 0;
    g_age = 0;
    g_info.ok = 0;
    g_info_work = NULL;
}

static int menu_match(void)
{
    TtProc *mp = NULL;

    if (scene(&mp) != TT_SCENE_MENU) {
        return 0;
    }
    (void)mp;   /* the notices are long: they keep the interface's size */
    /* the settings screens of the port keep the DS screens */
    if (bw_mod_title_in_settings != NULL && bw_mod_title_settings_on != NULL &&
        bw_mod_title_settings_on() && bw_mod_title_in_settings()) {
        return 0;
    }
    return 1;
}

/* ---- the backdrop: the title's checkered floor, seen in perspective ---- */

static int ifloor(float f)
{
    int i = (int)f;

    return (float)i > f ? i - 1 : i;
}

static u32 rgb3(int r, int g, int b)
{
    r = clampi(r, 0, 255);
    g = clampi(g, 0, 255);
    b = clampi(b, 0, 255);
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

extern const char *bw_lang_gamecode(void) TT_WEAK;

/* Whether the game is Pokemon White (the cartridge's game code, IRA?). */
static int tt_white(void)
{
    const char *code = bw_lang_gamecode != NULL ? bw_lang_gamecode() : NULL;

    return code != NULL && code[0] != '\0' && code[1] != '\0' && code[2] == 'A';
}

/* A dark floor of two greys that drifts toward the viewer, a cool light
 * behind the middle and a dark rim (the title's own colours).  Pokemon
 * White's title has the same floor in its own colours: a white sky and two
 * light greys (C3 and 96 at the near edge on its DS screen) that fade to
 * white at the horizon, a little darker toward the corners. */
static void backdrop(EidosUiCtx *c, int W, int H, int frame)
{
    int x, y, hy = H * 30 / 100;
    float scroll = (float)frame * 0.010f;

    if (tt_white()) {
        for (y = 0; y < H; y += 2) {
            float t = y < hy ? 0.0f : (float)(y - hy + 1) / (float)(H - hy);
            float z = 1.0f / (t + 0.07f);

            for (x = 0; x < W; x += 2) {
                float dx = ((float)x - (float)W * 0.5f) / ((float)W * 0.5f);
                float dy = ((float)y - (float)H * 0.46f) / ((float)H * 0.5f);
                float rr = dx * dx * 0.7f + dy * dy;
                float rim = 1.0f - 0.22f * (rr > 1.0f ? 1.0f : rr);
                int v = 255;

                if (y >= hy) {
                    float u = dx * (float)W * 0.5f * z / 52.0f;
                    float w = z * 1.15f + scroll;
                    int chk = (ifloor(u) + ifloor(w)) & 1;
                    float fade = t < 0.45f ? t / 0.45f : 1.0f;

                    v = 255 - (int)((chk ? 60.0f : 105.0f) * fade);
                }
                v = (int)((float)v * rim);
                ui_fill(c, x, y, 2, 2, rgb3(v, v, v), 255);
            }
        }
        return;
    }
    for (y = 0; y < H; y += 2) {
        float t = y < hy ? 0.0f : (float)(y - hy + 1) / (float)(H - hy);
        float z = 1.0f / (t + 0.07f);

        for (x = 0; x < W; x += 2) {
            float dx = ((float)x - (float)W * 0.5f) / ((float)W * 0.5f);
            float dy = ((float)y - (float)H * 0.46f) / ((float)H * 0.5f);
            float rr = dx * dx * 0.7f + dy * dy;
            float rim = 1.0f - 0.62f * (rr > 1.0f ? 1.0f : rr);
            float glow = 1.0f - rr * 1.6f;
            int r, g, b, v;

            if (y < hy) {
                v = 5 + (int)(8.0f * (float)y / (float)hy);
                r = v;
                g = v + 1;
                b = v + 3;
            } else {
                float u = dx * (float)W * 0.5f * z / 52.0f;
                float w = z * 1.15f + scroll;
                int chk = (ifloor(u) + ifloor(w)) & 1;
                float fade = t < 0.45f ? t / 0.45f : 1.0f;

                v = (int)((chk ? 52.0f : 15.0f) * fade);
                r = v;
                g = v + 1;
                b = v + 3;
            }
            if (glow > 0.0f) {
                int gl = (int)(34.0f * glow * glow);

                r += gl / 4;
                g += gl / 2;
                b += gl;
            }
            ui_fill(c, x, y, 2, 2, rgb3((int)((float)r * rim), (int)((float)g * rim),
                                        (int)((float)b * rim)), 255);
        }
    }
}

/* ---- pieces ---- */

static void tri_up(EidosUiCtx *c, int x, int y, int w, u32 col)
{
    int i;

    for (i = 0; i < w / 2 + 1; i++) {
        ui_fill(c, x + w / 2 - i, y + i, 2 * i + 1, 1, col, 255);
    }
}

static void tri_down(EidosUiCtx *c, int x, int y, int w, u32 col)
{
    int i;

    for (i = 0; i < w / 2 + 1; i++) {
        ui_fill(c, x + i, y + i, w - 2 * i, 1, col, 255);
    }
}

/* A label that fits in w: the large font, else the medium, else the small. */
static int fit_font(const char *s, int w)
{
    if (ui_text_w(s, UI_FONT_LARGE) <= w) {
        return UI_FONT_LARGE;
    }
    if (ui_text_w(s, UI_FONT_MEDIUM) <= w) {
        return UI_FONT_MEDIUM;
    }
    return UI_FONT_SMALL;
}

/* One plain row; the cursor's row is cyan with a chevron, a hovered one is
 * the same cyan without the outline. */
static void row_draw(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                     int selected, int hover)
{
    int sel = selected || hover, i, font = fit_font(label, w - 38);

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, sel ? UI_CYAN2 : UI_BODY,
             selected ? UI_CYAN : sel ? UI_CYAN2 : UI_EDGE, 240);
    ui_fill(c, x + 6, y + 5, 3, h - 10, sel ? UI_WHITE : UI_EDGE2, 255);
    ui_text(c, x + 17, y + ui_text_y(h, font), label, UI_WHITE,
            sel ? 0x00506Au : UI_EDGE, font, 1);
    if (selected) {
        for (i = 0; i < 5; i++) {
            ui_fill(c, x + w - 15 + i, y + h / 2 - 5 + i, 1, 11 - 2 * i, UI_WHITE, 255);
        }
    }
}

static int g_card_lr;           /* the card's saves change with left and right */

#define CARD_LIGHT  0x9CF4FFu
#define CARD_SHADE  0x00506Au

/* The CONTINUE card.  Returns ACT_LEFT or ACT_RIGHT when a slot arrow was
 * clicked, else -1. */
static int card_draw(EidosUiCtx *c, int x, int y, int w, int h, const SaveInfo *info,
                     int sel, int hov, const char *label, int slots,
                     const EidosUiFrame *in)
{
    int on = sel || hov, act = -1, tb = 22, k, strip = 0;
    u32 body = on ? UI_CYAN2 : UI_NAVY, edge = sel ? UI_CYAN : on ? UI_CYAN2 : UI_NAVY2;
    int wide = h >= 112;
    int px, py, slot_w, fw = 0, fh = 0, fx = 0, fy = 0;
    const UiImage *fig = NULL;

    ui_panel(c, x, y, w, h, CUT_DEF, 8, body, edge, 245);
    /* the header strip keeps the card's cut corner (a plain fill squared
     * the top left off again) */
    ui_panel(c, x + 2, y + 2, w - 4, tb - 4, UI_CUT_TL, 7,
             on ? 0x00718Fu : 0x061E38u, on ? 0x00718Fu : 0x061E38u, 255);
    ui_fill(c, x + 2, y + tb - 2, w - 4, 1, on ? UI_CYAN : UI_NAVY2, 255);
    ui_text(c, x + 10, y + 2 + ui_text_y(tb - 4, UI_FONT_LARGE), "CONTINUE", UI_WHITE,
                 CARD_SHADE, UI_FONT_LARGE, 1);
    if (slots > 1 && label != NULL && label[0] != '\0') {
        /* The save slots: a strip on the card's right edge, an up arrow, the
         * slot number over the count, a down arrow.  The arrows are buttons
         * (the game switches slots on left / right; up / down are mapped to
         * them by the menu). */
        char cur[12] = "", cnt[12] = "";
        int i, n = 0, half = 0, sx, sw = 22, top = y + tb + 2, bot = y + h - 12;
        int mid = (top + bot) / 2;
        EidosUiRect ur, dr;

        for (i = 0; label[i] != '\0' && n < 10; i++) {
            char ch = label[i];

            if (ch == '/') {
                cur[n] = '\0';
                half = 1;
                n = 0;
            } else if (ch >= '0' && ch <= '9') {
                (half ? cnt : cur)[n++] = ch;
            }
        }
        (half ? cnt : cur)[n] = '\0';
        strip = sw + 4;
        sx = x + w - 6 - sw;
        ui_fill(c, sx, top, sw, bot - top, on ? 0x00627Cu : 0x0A2848u, 200);
        ur.x = sx;
        ur.y = top;
        ur.w = sw;
        ur.h = mid - 12 - top;
        dr = ur;
        dr.y = mid + 12;
        dr.h = bot - dr.y;
        if (g_card_lr) {
            /* the saves change with left and right here */
            ui_text(c, sx + (sw - ui_text_w("\xE2\x86\x90", UI_FONT_LARGE)) / 2, ur.y + (ur.h - 12) / 2,
                    "\xE2\x86\x90", eidos_ui_hit(ur) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT, CARD_SHADE,
                    UI_FONT_LARGE, 1);
            ui_text(c, sx + (sw - ui_text_w("\xE2\x86\x92", UI_FONT_LARGE)) / 2, dr.y + (dr.h - 12) / 2,
                    "\xE2\x86\x92", eidos_ui_hit(dr) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT, CARD_SHADE,
                    UI_FONT_LARGE, 1);
        } else {
            tri_up(c, sx + (sw - 11) / 2, ur.y + (ur.h - 6) / 2, 11,
                   eidos_ui_hit(ur) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT);
            tri_down(c, sx + (sw - 11) / 2, dr.y + (dr.h - 6) / 2, 11,
                     eidos_ui_hit(dr) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT);
        }
        ui_text(c, sx + (sw - ui_text_w(cur, UI_FONT_SMALL)) / 2, mid - 11, cur, UI_WHITE,
                CARD_SHADE, UI_FONT_SMALL, 1);
        ui_fill(c, sx + 6, mid, sw - 12, 1, CARD_LIGHT, 255);
        ui_text(c, sx + (sw - ui_text_w(cnt, UI_FONT_SMALL)) / 2, mid + 2, cnt, CARD_LIGHT,
                CARD_SHADE, UI_FONT_SMALL, 1);
        if (eidos_ui_hit(ur) & EIDOS_UI_CLICK) {
            act = ACT_LEFT;
        } else if (eidos_ui_hit(dr) & EIDOS_UI_CLICK) {
            act = ACT_RIGHT;
        }
    }
    if (info == NULL || !info->ok) {
        return act;
    }
    slot_w = (w - 20 - strip) / 6;
    py = y + h - 33;
    px = x + 10;

    /* the player's figure stands on the party row, right of the facts, and may
     * rise above the card's top edge */
    {
        /* a plain save's trainer: the game's own colours (the Multiplayer
         * screen's look is its games' only) */
        AvatarLook plain;

        memset(&plain, 0, sizeof plain);
        plain.body = (u8)(info->sex == 1);
        fig = ui_overworld_look(&plain, 0, hov ? ((int)(in->frame / 7) & 3) : -1);
    }
    if (fig != NULL) {
        int room = py - 3 - (y - 10);
        int fk = wide && fig->h * 3 <= room ? 3 : fig->h * 2 <= room ? 2 : 1;

        fw = fig->w * fk;
        fh = fig->h * fk;
        fx = x + w - 16 - fw - strip;
        fy = py - 3 - fh;
        ui_ellipse(c, fx - 8, py - 9, fw + 16, 10, on ? 0x006C86u : 0x07203Eu, UI_NONE, 255);
    }

    /* the facts */
    {
        static const char *const cap[4] = { "PLAYER", "TIME", "BADGES", "POKEDEX" };
        char v[4][40];
        int n = info->seen_on ? 4 : 3;
        int gx = x + 10, gy = y + tb + 5;
        int gw = w - 20 - strip - (fw > 0 ? fw + 10 : 0);

        snprintf(v[0], sizeof v[0], "%s", info->name);
        snprintf(v[1], sizeof v[1], "%d:%02d", info->hours, info->minutes);
        snprintf(v[2], sizeof v[2], "%d", info->badges);
        snprintf(v[3], sizeof v[3], "%d", info->seen);
        if (wide) {
            int cw = gw / 2, nw = ui_text_w(v[0], UI_FONT_LARGE) + 8;

            /* a long name (seven letters) pushes the second column right */
            if (nw > cw) {
                cw = nw;
            }
            for (k = 0; k < n; k++) {
                int cx = gx + (k & 1) * cw, cy = gy + (k >> 1) * 29;

                ui_text(c, cx, cy, cap[k], CARD_LIGHT, CARD_SHADE, UI_FONT_SMALL, 1);
                ui_text(c, cx, cy + 10, v[k], UI_WHITE, CARD_SHADE, UI_FONT_LARGE, 1);
            }
        } else {
            int cw = gw / n;

            for (k = 0; k < n; k++) {
                ui_text(c, gx + k * cw, gy, cap[k], CARD_LIGHT, CARD_SHADE, UI_FONT_SMALL, 1);
                ui_text(c, gx + k * cw, gy + 10, v[k], UI_WHITE, CARD_SHADE, UI_FONT_LARGE, 1);
            }
        }
    }

    /* the party: the icons, and a mark for each empty slot */
    for (k = 0; k < 6; k++) {
        int sx = px + k * slot_w;

        if (k < info->nmon && !info->mon[k].egg) {
            const UiImage *ic = ui_mon_icon(info->mon[k].species, info->mon[k].form,
                                            (int)(in->frame / 20) & 1);

            if (ic != NULL) {
                ui_image(c, sx + (slot_w - ic->w) / 2, py + (32 - ic->h) / 2, ic, 1);
            }
        } else if (k < info->nmon) {
            ui_ellipse(c, sx + slot_w / 2 - 5, py + 9, 10, 14, UI_WHITE, UI_GREY, 255);
        } else {
            ui_ellipse(c, sx + slot_w / 2 - 7, py + 9, 14, 14, UI_NONE,
                       on ? 0x0AC4E6u : 0x1A5E96u, 255);
        }
    }
    if (fig != NULL) {
        ui_image(c, fx, fy, fig, fw / fig->w);
    }
    return act;
}

/* ---- the menu ---- */

static void request(int target, int act)
{
    g_target = target;
    g_act = act;
    g_age = 0;
}

/*
 * The title menu's yes/no (a BmpMenu, arm9/bmp_menu.c: header.items 0x00,
 * selected 0x2D) as a list in the system box's style, its right edge at
 * `right`, its foot at `foot`.  A click on the other row presses UP or
 * DOWN, on the chosen row A (the game's cursor moves by its own keys).
 */
extern u16 *STRBUF_GetChars(void *strbuf) TT_WEAK;
extern u16  STRBUF_GetLength(void *strbuf) TT_WEAK;

static void title_yes_no(EidosUiCtx *c, const EidosUiFrame *in, const u8 *menu, int right, int foot)
{
    const u8 *items = *(const u8 *const *)menu;
    int sel = menu[0x2D], i, w = 0, rh = 18, h, x, y;
    char label[2][24];

    (void)in;
    for (i = 0; i < 2; i++) {
        const u8 *sb = items != NULL ? *(const u8 *const *)(items + 8 * i) : NULL;

        label[i][0] = '\0';
        if (sb != NULL && STRBUF_GetChars != NULL && STRBUF_GetLength != NULL) {
            name_utf8(STRBUF_GetChars((void *)sb), label[i], 24);
        }
        if (label[i][0] == '\0') {
            snprintf(label[i], sizeof label[i], "%s", i == 0 ? "YES" : "NO");
        }
        if (ui_text_w(label[i], UI_FONT_LARGE) + 30 > w) {
            w = ui_text_w(label[i], UI_FONT_LARGE) + 30;
        }
    }
    h = 4 + 2 * rh + 4;
    x = right - w;
    y = foot - h;
    if (ui_theme_active()->textbox != NULL) {
        ui_textbox_frame(c, UI_TEXTBOX_SYSTEM, x, y, w, h);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, 0x464850u, 240);
    }
    for (i = 0; i < 2; i++) {
        int ry = y + 4 + i * rh, lit = i == sel, st, rr;
        EidosUiRect r;

        r.x = x + 3; r.y = ry; r.w = w - 6; r.h = rh;
        st = eidos_ui_hit(r);
        if (ui_theme_active()->choice_row != NULL) {
            ui_theme_active()->choice_row(c, UI_CHOICE_SYSTEM, r.x, ry, r.w, rh, label[i], lit);
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_press(lit ? BW_KEY_A : (i < sel ? BW_KEY_UP : BW_KEY_DOWN), 2);
            }
            continue;
        }
        if (lit) {
            ui_panel(c, r.x, ry - 1, r.w, rh, UI_CUT_TL | UI_CUT_BR, 5, 0x05343Eu, 0x008BACu, 250);
            for (rr = 3; rr < rh - 3; rr++) {
                int left = 3 + (rr < 7 ? 7 - rr : 0);

                if (left < 6) {
                    ui_fill(c, r.x + left, ry - 1 + rr, 6 - left, 1, 0x008BACu, 255);
                }
            }
        }
        ui_text(c, r.x + 10, ry - (lit ? 1 : 0) + ui_text_y(rh, UI_FONT_LARGE), label[i],
                lit ? UI_WHITE : UI_LIGHT, 0x464850u, UI_FONT_LARGE, 1);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_press(lit ? BW_KEY_A : (i < sel ? BW_KEY_UP : BW_KEY_DOWN), 2);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The interface style: its flag on the menu, and its chooser          */
/* ------------------------------------------------------------------ */

/*
 * The three styles' signs (13 x 13, the interface's own pixels): a spark
 * for Kosmos, a bolt for Plasma, two notes for Harmonia.  W white, L light,
 * B the sign's colour, D its shade.  The style in use glows (a ring of its
 * colour, one pixel) and stands on a white line; the others are grey.
 */
static const char *const k_style_id[3] = { "kosmos", "plasma", "harmonia" };
static const char *const k_style_name[3] = { "KOSMOS", "PLASMA", "HARMONIA" };
static const char *const k_style_line[3] = { "Clean and dark", "Bold, angled", "Original DS" };
static const u32 k_sign_tone[3][3] = {       /* light, colour, shade */
    { 0xA0EEFAu, 0x00C8E8u, 0x0084A4u },
    { 0xFFEA96u, 0xF8BC28u, 0xC4840Cu },
    { 0xA8D0FAu, 0x489CECu, 0x2464B8u },
};
static const char *const k_sign[3][13] = {
    {
        "......D......",
        "......D...L..",
        "......B..LWL.",
        ".....BLB..L..",
        "....BLLLB....",
        "...BLLWLLB...",
        "DDBLLWWWLLBDD",
        "...BLLWLLB...",
        "....BLLLB....",
        ".....BLB.....",
        "......B......",
        "......D......",
        "......D......"
    },
    {
        ".......LLLLL.",
        "......LBBBD..",
        ".....LBBBD...",
        "....LBBBD....",
        "...LBBBBBBBD.",
        "..LBDDBBBBD..",
        "......LBBD...",
        ".....LBBD....",
        "....LBDD.....",
        "...LBD.......",
        "..LBD........",
        ".LD..........",
        "L............"
    },
    {
        ".............",
        "...LLLLLLLLLL",
        "...BBBBBBBBBB",
        "...BD......BD",
        "...BD......BD",
        "...BD......BD",
        "...BD......BD",
        "...BD......BD",
        "...BD......BD",
        ".LLBB....LLBB",
        "LBBBB...LBBBB",
        "DBBBD...DBBBD",
        ".DDD.....DDD."
    }
};

static int sign_at(int which, int i, int j)
{
    return i >= 0 && j >= 0 && i < 13 && j < 13 && k_sign[which][j][i] != '.';
}

static void style_sign(EidosUiCtx *c, int x, int y, int which, int lit)
{
    int i, j;

    for (j = -1; j <= 13; j++) {
        for (i = -1; i <= 13; i++) {
            u32 col;
            char t;

            if (!sign_at(which, i, j)) {
                if (lit && (sign_at(which, i - 1, j) || sign_at(which, i + 1, j) || sign_at(which, i, j - 1)
                            || sign_at(which, i, j + 1))) {
                    ui_fill(c, x + i, y + j, 1, 1, UI_RAW(k_sign_tone[which][1]), 78);
                }
                continue;
            }
            t = k_sign[which][j][i];
            col = t == 'W' ? 0xFFFFFFu : k_sign_tone[which][t == 'L' ? 0 : t == 'B' ? 1 : 2];
            if (!lit) {
                u32 v = (((col >> 16) & 255u) * 30u + ((col >> 8) & 255u) * 59u + (col & 255u) * 11u) / 100u;

                v = 70u + v * 90u / 255u;
                col = v << 16 | v << 8 | (v + 8u);
            }
            ui_fill(c, x + i, y + j, 1, 1, UI_RAW(col), 255);
        }
    }
}

/* The sign alone, each of its points `k` pixels across (a card that has no
 * picture yet shows it in the picture's place). */
static void style_sign_big(EidosUiCtx *c, int x, int y, int which, int lit, int k)
{
    int i, j;

    for (j = 0; j < 13; j++) {
        for (i = 0; i < 13; i++) {
            u32 col;
            char t;

            if (!sign_at(which, i, j)) {
                continue;
            }
            t = k_sign[which][j][i];
            col = t == 'W' ? 0xFFFFFFu : k_sign_tone[which][t == 'L' ? 0 : t == 'B' ? 1 : 2];
            if (!lit) {
                u32 v = (((col >> 16) & 255u) * 30u + ((col >> 8) & 255u) * 59u + (col & 255u) * 11u) / 100u;

                v = 70u + v * 90u / 255u;
                col = v << 16 | v << 8 | (v + 8u);
            }
            ui_fill(c, x + i * k, y + j * k, k, k, UI_RAW(col), 255);
        }
    }
}

/* The style in use, as the settings number them (0 Kosmos, 1 Plasma, 2 Harmonia). */
static int style_now(void)
{
    const char *id = ui_theme_active()->id;
    int i;

    for (i = 0; i < 3; i++) {
        if (id != NULL && strcmp(id, k_style_id[i]) == 0) {
            return i;
        }
    }
    return 0;
}

#define STYLE_FLAG_W 70
#define STYLE_FLAG_H 30

/* The flag at the menu's top right: the style's name over the three signs.
 * Drawn as the menu's own entries are:
 * through the same panel call (Kosmos's cut panel, Harmonia's black box in a
 * white line), or by the style itself (hook "style-flag": in x, y, w, h the
 * flag's box, h negative under the pointer; out x the contents' left at the
 * box's foot, w how far the box leans to the right at its top).  Returns
 * its box for the pointer. */
static EidosUiRect style_flag(EidosUiCtx *c, int W, int hot)
{
    EidosUiRect r;
    int cur = style_now(), fx, fy, fw, fh, lean = 0, left, i, cwid;
    const char *name = k_style_name[cur];

    r.w = STYLE_FLAG_W;
    r.h = STYLE_FLAG_H;
    r.x = W - r.w - 10 - c->safe_r;
    r.y = 0;
    fx = r.x;
    fy = r.y;
    fw = r.w;
    fh = hot ? -r.h : r.h;
    if (ui_app_window(c, "style-flag", &fx, &fy, &fw, &fh)) {
        left = fx;
        lean = fw;
        cwid = r.w - 22;                /* (the plate's rule at its left, its slant at its right) */
    } else {
        ui_panel(c, r.x, r.y - 8, r.w, r.h + 8, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, hot ? UI_CYAN : UI_EDGE, 240);
        left = r.x + 8;
        cwid = r.w - 16;
    }
#define FLAG_X(row) (left + lean * (r.h - (row)) / r.h)
    ui_text(c, FLAG_X(6) + (cwid - ui_text_w(name, UI_FONT_SMALL)) / 2, r.y + 3, name, UI_RAW(0xF8F8F8u),
            (s32)UI_RAW(0x101014u), UI_FONT_SMALL, 1);
    for (i = 0; i < 3; i++) {
        int sx = FLAG_X(19) + i * (cwid - 13) / 2;

        style_sign(c, sx, r.y + 12, i, i == cur);
        if (i == cur) {
            ui_fill(c, sx, r.y + 12 + 14, 13, 1, UI_RAW(0xFFFFFFu), 255);
        }
    }
#undef FLAG_X
    return r;
}

/* The chooser's sounds are the title menu's own: 0x548 a move, 0x54C a
 * choice, 0x551 a step back. */
/* The chooser is up (its cards). */
static int g_style_open, g_style_first, g_style_before, g_style_cool, g_style_asked, g_style_demo, g_style_go;

extern int  bw_slots_try_begin(const char *path) TT_WEAK;
extern int  bw_slots_try_embedded(void) TT_WEAK;
extern void eidos_boot_restart_to(int what) TT_WEAK;

/* A test's own files for the chooser, in the folder EIDOS_DEMO_DIR names:
 * demo.sav (a save for the preview in the built-in one's place) and
 * kosmos.bmp, plasma.bmp, harmonia.bmp (card pictures).  1 when the file is
 * there.  Without the variable nothing is read: the preview's save is in
 * the program and the cards' pictures are the player's own. */
static int demo_file(const char *name, char *out, size_t n)
{
    const char *dir = getenv("EIDOS_DEMO_DIR");
    FILE *f;

    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    snprintf(out, n, "%s/%s", dir, name);
    if ((f = fopen(out, "rb")) == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* A card's picture: the file as read (src), and at the card's size (px). */
static struct {
    u8 *src, *px;
    int sw, sh, w, h, tried;
} g_pic[3];

static u32 le32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

extern const char *bw_save_dir(void) TT_WEAK;
extern int bw_plat_mkdirs(const char *path) TT_WEAK;

/* An uncompressed 24 or 32 bit BMP of at most `limit` bytes, as RGBA. */
static u8 *bmp_read(const char *path, long limit, int *w_out, int *h_out)
{
    FILE *f = fopen(path, "rb");
    long size;
    u8 *file, *px = NULL;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = size > 54 && size < limit ? (u8 *)malloc((size_t)size) : NULL;
    if (file != NULL && fread(file, 1, (size_t)size, f) == (size_t)size && file[0] == 'B' && file[1] == 'M') {
        u32 off = le32(file + 10);
        int w = (int)le32(file + 18), h = (int)le32(file + 22), bpp = file[28] | file[29] << 8, up = h > 0, x, y;
        int bytes = bpp / 8, stride = (w * bytes + 3) & ~3;

        h = h < 0 ? -h : h;
        if ((bpp == 24 || bpp == 32) && w > 0 && h > 0 && w <= 8192 && h <= 8192
            && (u32)size >= off + (u32)stride * (u32)h) {
            px = (u8 *)malloc((size_t)w * (size_t)h * 4u);
            for (y = 0; px != NULL && y < h; y++) {
                const u8 *row = file + off + (size_t)stride * (size_t)(up ? h - 1 - y : y);

                for (x = 0; x < w; x++) {
                    px[((size_t)y * (size_t)w + (size_t)x) * 4u + 0] = row[x * bytes + 2];
                    px[((size_t)y * (size_t)w + (size_t)x) * 4u + 1] = row[x * bytes + 1];
                    px[((size_t)y * (size_t)w + (size_t)x) * 4u + 2] = row[x * bytes + 0];
                    px[((size_t)y * (size_t)w + (size_t)x) * 4u + 3] = 255;
                }
            }
            *w_out = w;
            *h_out = h;
        }
    }
    free(file);
    fclose(f);
    return px;
}

/* RGBA as a 24 bit BMP.  0 when written. */
static int bmp_write(const char *path, const u8 *rgba, int w, int h)
{
    int stride = (w * 3 + 3) & ~3, x, y;
    u32 size = 54u + (u32)stride * (u32)h;
    u8 head[54], *row;
    FILE *f;

    row = (u8 *)calloc(1, (size_t)stride);
    f = row != NULL ? fopen(path, "wb") : NULL;
    if (f == NULL) {
        free(row);
        return -1;
    }
    memset(head, 0, sizeof head);
    head[0] = 'B';
    head[1] = 'M';
    head[2] = (u8)size;
    head[3] = (u8)(size >> 8);
    head[4] = (u8)(size >> 16);
    head[5] = (u8)(size >> 24);
    head[10] = 54;
    head[14] = 40;
    head[18] = (u8)w;
    head[19] = (u8)(w >> 8);
    head[22] = (u8)h;
    head[23] = (u8)(h >> 8);
    head[26] = 1;
    head[28] = 24;
    fwrite(head, 1, sizeof head, f);
    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            const u8 *sp = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;

            row[x * 3 + 0] = sp[2];
            row[x * 3 + 1] = sp[1];
            row[x * 3 + 2] = sp[0];
        }
        fwrite(row, 1, (size_t)stride, f);
    }
    free(row);
    return fclose(f) == 0 ? 0 : -1;
}

/* The player's own cards: <the save folder>/style-cards/<style>.bmp, the
 * pictures the game takes of its own battle screen while the chooser's
 * preview runs (eidos_ui_style_card_from).  A release carries no picture
 * of the game. */
static int cards_dir(char *out, size_t n)
{
    const char *sd = bw_save_dir != NULL ? bw_save_dir() : NULL;

    if (sd == NULL || sd[0] == '\0') {
        return 0;
    }
    snprintf(out, n, "%s/style-cards", sd);
    return 1;
}

/* A card's picture: the player's own, else the test builds' (the folder
 * "demo" beside the game). */
static void pic_load(int t)
{
    char path[700], dir[600], name[32];
    FILE *f = NULL;

    g_pic[t].tried = 1;
    if (cards_dir(dir, sizeof dir)) {
        snprintf(path, sizeof path, "%s/%s.bmp", dir, k_style_id[t]);
        f = fopen(path, "rb");
    }
    if (f != NULL) {
        fclose(f);
    } else {
        snprintf(name, sizeof name, "%s.bmp", k_style_id[t]);
        if (!demo_file(name, path, sizeof path)) {
            return;
        }
    }
    g_pic[t].src = bmp_read(path, 16L * 1024 * 1024, &g_pic[t].sw, &g_pic[t].sh);
}

/* Where the game's picture of its window goes before it is made a card. */
int eidos_ui_style_card_tmp(char *out, int cap)
{
    char dir[600];

    if (!cards_dir(dir, sizeof dir)) {
        return 0;
    }
    if (bw_plat_mkdirs != NULL) {
        (void)bw_plat_mkdirs(dir);
    }
    snprintf(out, (size_t)cap, "%s/shot.bmp", dir);
    return 1;
}

/* The game's picture of the style's battle screen (ui.c, the preview) as
 * that style's card: brought down to 640 across (each pixel the mean of
 * those it stands for), the big file removed, the chooser's copy dropped. */
void eidos_ui_style_card_from(const char *bmp, int t)
{
    char path[700], dir[600];
    int W = 0, H = 0, cw, chh, x, y;
    u8 *src, *px;

    if (t < 0 || t > 2 || bmp == NULL || !cards_dir(dir, sizeof dir)) {
        return;
    }
    src = bmp_read(bmp, 96L * 1024 * 1024, &W, &H);
    (void)remove(bmp);
    if (src == NULL) {
        return;
    }
    cw = W > 640 ? 640 : W;
    chh = (int)((long)H * cw / W);
    px = chh > 0 ? (u8 *)malloc((size_t)cw * (size_t)chh * 4u) : NULL;
    for (y = 0; px != NULL && y < chh; y++) {
        int y0 = y * H / chh, y1 = (y + 1) * H / chh;

        for (x = 0; x < cw; x++) {
            int x0 = x * W / cw, x1 = (x + 1) * W / cw, xx, yy;
            u32 r = 0, g = 0, b = 0, n = 0;

            for (yy = y0; yy < y1 || yy == y0; yy++) {
                const u8 *sp = src + ((size_t)yy * (size_t)W + (size_t)x0) * 4u;

                for (xx = x0; xx < x1 || xx == x0; xx++, sp += 4) {
                    r += sp[0];
                    g += sp[1];
                    b += sp[2];
                    n++;
                }
            }
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 0] = (u8)(r / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 1] = (u8)(g / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 2] = (u8)(b / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 3] = 255;
        }
    }
    snprintf(path, sizeof path, "%s/%s.bmp", dir, k_style_id[t]);
    if (px != NULL && bmp_write(path, px, cw, chh) == 0) {
        /* the chooser reads it again when it next opens */
        free(g_pic[t].src);
        free(g_pic[t].px);
        memset(&g_pic[t], 0, sizeof(g_pic[t]));
    }
    free(px);
    free(src);
}

/* The picture at the card's size (each pixel the mean of those it stands for). */
static void pic_fit(int t, int cw, int chh)
{
    int x, y, W, H;
    u8 *px;

    if (!g_pic[t].tried) {
        pic_load(t);
    }
    if (g_pic[t].src == NULL || cw < 8 || chh < 8 || (g_pic[t].px != NULL && g_pic[t].w == cw && g_pic[t].h == chh)) {
        return;
    }
    px = (u8 *)realloc(g_pic[t].px, (size_t)cw * (size_t)chh * 4u);
    if (px == NULL) {
        return;
    }
    g_pic[t].px = px;
    g_pic[t].w = cw;
    g_pic[t].h = chh;
    W = g_pic[t].sw;
    H = g_pic[t].sh;
    for (y = 0; y < chh; y++) {
        int y0 = y * H / chh, y1 = (y + 1) * H / chh;

        for (x = 0; x < cw; x++) {
            int x0 = x * W / cw, x1 = (x + 1) * W / cw, xx, yy;
            u32 r = 0, g = 0, b = 0, n = 0;

            for (yy = y0; yy < y1 || yy == y0; yy++) {
                const u8 *sp = g_pic[t].src + ((size_t)yy * (size_t)W + (size_t)x0) * 4u;

                for (xx = x0; xx < x1 || xx == x0; xx++, sp += 4) {
                    r += sp[0];
                    g += sp[1];
                    b += sp[2];
                    n++;
                }
            }
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 0] = (u8)(r / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 1] = (u8)(g / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 2] = (u8)(b / n);
            px[((size_t)y * (size_t)cw + (size_t)x) * 4u + 3] = 255;
        }
    }
}

static void style_set(int which)
{
    which = which < 0 ? 2 : which > 2 ? 0 : which;
    if (which != style_now()) {
        (void)eidos_settings_set(EIDOS_OPT_UI_THEME, which);
        ui_theme_select(k_style_id[which]);         /* now: the rest of this frame is in it */
        if (ui_theme_active()->frame_begin != NULL) {
            ui_theme_active()->frame_begin();
        }
        eidos_ui_sound(0x548);
    }
}

static void style_close(int keep)
{
    if (keep) {
        (void)eidos_settings_set(EIDOS_OPT_UI_THEME, style_now());
        (void)eidos_settings_commit();
    } else {
        style_set(g_style_before);
    }
    g_style_open = 0;
    g_style_first = 0;
    g_style_cool = 15;
    eidos_ui_crossfade();
}

static void style_show(void)
{
    char path[600];

    g_style_open = 1;
    /* the preview: the save built into the program (or a test's own file) */
    g_style_demo = bw_slots_try_begin != NULL && eidos_boot_restart_to != NULL
                   && (demo_file("demo.sav", path, sizeof path)
                       || (bw_slots_try_embedded != NULL && bw_slots_try_embedded()));
}

/* The preview: the real game on the chooser's save, kept in memory.  On the
 * game's thread: it changes the save the game holds.  The game starts
 * again on that save at once: no Continue row, so none of its questions. */
static void style_try_start(void *arg)
{
    char path[600];

    (void)arg;
    if (bw_slots_try_begin(demo_file("demo.sav", path, sizeof path) ? path : NULL)) {
        eidos_boot_restart_to(1);
    } else {
        eidos_ui_try_clear();                   /* no save in the file: the chooser again */
        g_style_open = 1;
    }
}

/* The chooser in the title menu's place.  1 while it is up: the menu is
 * not drawn and takes no key. */
static int style_screen(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, int W, int H, int ready)
{
    u32 k = eidos_ui_keys_pressed();
    int bar = ui_bar_h(c), cur, i;

    if (eidos_ui_try_state() >= 2 && ready) {
        /* back from the preview's game */
        eidos_ui_try_clear();
        g_style_asked = 1;
        style_show();
        k = 0;
    }
    if (eidos_ui_try_state() != 0) {
        /* the preview is going in (or coming back): the menu, under the
         * preview's cover, takes no key; the game starts on the preview's
         * save once the key's sound has played */
        eidos_ui_take_keys(BW_KEY_GAME);
        if (g_style_go && --g_style_cool <= 0) {
            g_style_go = 0;
            g_style_cool = 0;
            (void)eidos_ui_game_call(style_try_start, NULL);
        }
        return 0;
    }
    if (!g_style_asked && ready) {
        /* the first time the menu is up: ask, when no style was ever chosen
         * (not while a test names the style) */
        g_style_asked = 1;
        if (!eidos_settings_style_chosen() && getenv("EIDOS_UI_THEME") == NULL) {
            style_show();
            g_style_first = 1;
            g_style_before = style_now();
        }
    }
    eidos_ui_take_keys(BW_KEY_SELECT);
    if (g_style_cool > 0) {
        g_style_cool--;                 /* the key that closed it is not the menu's */
        eidos_ui_take_keys(BW_KEY_GAME);
    }
    if (!g_style_open) {
        if (ready && g_style_cool == 0 && (k & BW_KEY_SELECT)) {
            style_show();
            g_style_before = style_now();
            eidos_ui_sound(0x54C);
            eidos_ui_crossfade();
        } else {
            return 0;
        }
        k = 0;
    }
    eidos_ui_take_keys(BW_KEY_GAME);
    if (k & BW_KEY_LEFT) {
        style_set(style_now() - 1);
    } else if (k & BW_KEY_RIGHT) {
        style_set(style_now() + 1);
    }
    cur = style_now();
    {
        const char *hp[8];
        u32 hk[4];
        int hn = 0;
        const char *head = "Choose a style";
        const char *sub = g_style_first ? "You can change it later from this menu."
                                        : "Applies instantly.";
        int m = W >= 560 ? 14 : 8, gap = 8, cw = (W - 2 * m - 2 * gap) / 3, chh = cw * 9 / 16;
        int block = 52 + chh + 22, top = (H - bar - block) / 2 + 52, act = -1, x;

        if (top < 52) {
            top = 52;
        }
        /* on the title's own floor, dimmed (as the menu's questions are):
         * the world behind the menu is no picture of its own (after a
         * preview it is still the preview's last one) */
        (void)view;
        backdrop(c, W, H, (int)in->frame);
        ui_fill(c, 0, 0, W, H, UI_RAW(0x000000u), 150);
        ui_text(c, (W - ui_text_w(head, UI_FONT_LARGE) * 2) / 2, top - 52, head, UI_RAW(0xFFFFFFu),
                (s32)UI_RAW(0x101014u), UI_FONT_LARGE, 2);
        ui_text(c, (W - ui_text_w(sub, UI_FONT_SMALL)) / 2, top - 16, sub, UI_RAW(0xBEC4D0u), (s32)UI_RAW(0x101014u),
                UI_FONT_SMALL, 1);
        for (i = 0; i < 3; i++) {
            EidosUiRect r;
            UiImage im;
            int lit = i == cur, st;

            x = m + i * (cw + gap);
            r.x = x;
            r.y = top;
            r.w = cw;
            r.h = chh + 20;
            st = eidos_ui_hit(r);
            if (st & EIDOS_UI_CLICK) {
                act = i;
            }
            if (lit) {
                ui_fill(c, x - 2, top - 2, cw + 4, chh + 24, UI_RAW(0xFFFFFFu), 255);
            } else if (st & EIDOS_UI_HOVER) {
                ui_fill(c, x - 1, top - 1, cw + 2, chh + 22, UI_RAW(0x8A90A0u), 255);
            }
            ui_fill(c, x, top, cw, chh + 20, UI_RAW(0x0A0C10u), 255);
            pic_fit(i, cw, chh);
            im.w = g_pic[i].w;
            im.h = g_pic[i].h;
            im.rgba = g_pic[i].px;
            if (im.rgba != NULL && im.w == cw) {
                ui_image(c, x, top, &im, 1);
                if (!lit) {
                    ui_fill(c, x, top, cw, chh, UI_RAW(0x000000u), 110);
                }
            } else {
                /* no picture of this style yet (the game takes its own while
                 * the preview runs): the style's sign in its place */
                int k = chh >= 100 ? 4 : 3;

                ui_fill(c, x, top, cw, chh, UI_RAW(lit ? 0x161B24u : 0x10141Au), 255);
                style_sign_big(c, x + (cw - 13 * k) / 2, top + (chh - 13 * k) / 2, i, lit, k);
            }
            style_sign(c, x + 5, top + chh + 4, i, lit);
            ui_text(c, x + 23, top + chh + ui_text_y(20, UI_FONT_LARGE), k_style_name[i],
                    UI_RAW(lit ? 0xFFFFFFu : 0x9AA0AEu), (s32)UI_RAW(0x000000u), UI_FONT_LARGE, 1);
            if (ui_text_w(k_style_name[i], UI_FONT_LARGE) + ui_text_w(k_style_line[i], UI_FONT_SMALL) + 36 <= cw) {
                ui_text(c, x + cw - 5 - ui_text_w(k_style_line[i], UI_FONT_SMALL),
                        top + chh + ui_text_y(20, UI_FONT_SMALL), k_style_line[i], UI_RAW(lit ? 0xAAB0BEu : 0x6E7482u),
                        -1, UI_FONT_SMALL, 1);
            }
        }
        hp[2 * hn] = "A";
        hp[2 * hn + 1] = "Choose";
        hk[hn++] = BW_KEY_A;
        if (g_style_demo) {
            hp[2 * hn] = "Y";
            hp[2 * hn + 1] = "Preview";
            hk[hn++] = BW_KEY_Y;
        }
        hp[2 * hn] = "\xE2\x86\x90\xE2\x86\x92";
        hp[2 * hn + 1] = "Style";
        hk[hn++] = BW_KEY_RIGHT;
        if (!g_style_first) {
            hp[2 * hn] = "B";
            hp[2 * hn + 1] = "Back";
            hk[hn++] = BW_KEY_B;
        }
        ui_hints(c, H - bar, W, hp, hn, hk);
        if (act >= 0 && act != cur) {
            style_set(act);
        } else if (act == cur || (k & BW_KEY_A)) {
            eidos_ui_sound(0x54C);
            style_close(1);
        } else if ((k & BW_KEY_Y) && g_style_demo) {
            /* the real game, on the chooser's save: its X menu, party, Bag
             * and a battle (ui.c); back here from its START */
            eidos_ui_sound(0x54C);
            g_style_open = 0;
            g_style_go = 1;
            g_style_cool = 10;
            eidos_ui_try_begin();
        } else if ((k & BW_KEY_B) || eidos_ui_right_click()) {
            eidos_ui_sound(g_style_first ? 0x54C : 0x551);
            style_close(g_style_first);     /* the first time there is no style to go back to */
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The mod manager                                                     */
/* ------------------------------------------------------------------ */

/*
 * The game's own mod manager, for a game no launcher starts (a phone, a
 * console, a download run by itself): offered when the game was started with
 * --mods (bw_mod_menu_manager).  A flag at the menu's top left, the style
 * flag's twin, opens a window in the menu's place: at the left the mods
 * installed that work with this game (a mod made for the other version is
 * not listed), each with its switch; at the right the chosen mod: its name,
 * who made it, its version and what it does.  The switches apply when the
 * window closes (bw_mod_menu_apply, on the game's thread), before any game
 * starts.
 *
 * The pad: up and down choose a mod, A turns it on or off, B closes.  The
 * pointer: a click chooses a mod, a click on the chosen mod or on its switch
 * turns it, the wheel or a drag moves the list, the button at the window's
 * foot turns the chosen mod, a right click closes.  Y opens the window from
 * the menu.
 */
extern int         bw_mod_menu_manager(void) TT_WEAK;
extern void        bw_mod_menu_open(void) TT_WEAK;
extern u32         bw_mod_menu_count(void) TT_WEAK;
extern const char *bw_mod_menu_name_at(u32 index) TT_WEAK;
extern const char *bw_mod_menu_about_at(u32 index, int what) TT_WEAK;
extern int         bw_mod_menu_enabled_at(u32 index) TT_WEAK;
extern int         bw_mod_menu_present_at(u32 index) TT_WEAK;
extern int         bw_mod_menu_needs_game_at(u32 index) TT_WEAK;
extern int         bw_mod_menu_set_enabled(u32 index, int on) TT_WEAK;
extern int         bw_mod_menu_dirty(void) TT_WEAK;
extern int         bw_mod_menu_apply(void) TT_WEAK;

/* The mods index (sdk/mod_catalog.h): the mods the player can get.  The
 * numbers are that header's states. */
extern const char *bw_mod_menu_root_at(u32 index) TT_WEAK;
extern void        bw_mod_catalog_read(unsigned fresh_s) TT_WEAK;
extern int         bw_mod_catalog_poll(void) TT_WEAK;
extern const char *bw_mod_catalog_error(void) TT_WEAK;
extern u32         bw_mod_catalog_count(void) TT_WEAK;
extern const char *bw_mod_catalog_id_at(u32 index) TT_WEAK;
extern const char *bw_mod_catalog_name_at(u32 index) TT_WEAK;
extern const char *bw_mod_catalog_author_at(u32 index) TT_WEAK;
extern const char *bw_mod_catalog_version_at(u32 index) TT_WEAK;
extern const char *bw_mod_catalog_about_at(u32 index) TT_WEAK;
extern unsigned long long bw_mod_catalog_size_at(u32 index) TT_WEAK;
extern int         bw_mod_catalog_installed_at(u32 index) TT_WEAK;
extern const char *bw_mod_catalog_root_at(u32 index) TT_WEAK;
extern int         bw_mod_catalog_install(u32 index) TT_WEAK;
extern int         bw_mod_catalog_job(int *index, unsigned long long *got, unsigned long long *total) TT_WEAK;
extern const char *bw_mod_catalog_job_error(void) TT_WEAK;
extern int         bw_mod_catalog_finish(void) TT_WEAK;
extern void        bw_mod_catalog_stop(void) TT_WEAK;
extern int         bw_mod_catalog_can_remove(const char *root) TT_WEAK;
extern int         bw_mod_catalog_remove(const char *root) TT_WEAK;

enum { CAT_NONE, CAT_READING, CAT_READY, CAT_FAILED };
enum { JOB_NONE, JOB_DOWNLOADING, JOB_UNPACKING, JOB_DONE, JOB_FAILED };

#define MODS_MAX 64
#define MODS_ROW 36
#define MODS_GAP 4
#define MODS_ROWS_MAX (2 * MODS_MAX + 8)

/* What a line of the list is. */
enum {
    MROW_MOD,                   /* an installed mod: ref its place in the mod list   */
    MROW_GET,                   /* a mod of the index: ref its place in the index    */
    MROW_NOTE                   /* a line of text: ref 0 reading, 1 failed, 2 none there, 3 none installed */
};

typedef struct ModsLine {
    int kind, ref;
    int y, h;                   /* its place in the list, from the list's top */
} ModsLine;

static int g_mods_open, g_mods_cur, g_mods_first, g_mods_cool;
static int g_mods_tab;                  /* the tab shown: 0 installed, 1 available       */
static int g_mods_ask;                  /* the remove question is up; its choice in g_mods_ask_cur */
static int g_mods_ask_cur;
static char g_mods_ask_root[1024];      /* the folder the question is about              */
static char g_mods_ask_name[64];
static int g_mods_job_sent;             /* the job's end was handed to the game's thread */
static int g_mods_wait;                 /* 1: an install was asked of the game's thread; 2: it runs */
static int g_mods_stopped;              /* the player stopped the download: its end is no error */
static char g_mods_follow[112];         /* the index id of the mod just installed: the list's choice goes to it */
static char g_mods_note[200];           /* what went wrong last, and the index id it is about */
static char g_mods_note_id[112];

static int mods_on(void)
{
    return bw_mod_menu_manager != NULL && bw_mod_menu_count != NULL && bw_mod_menu_manager();
}

/* The index is in this build (the tests link the screen without it). */
static int mods_cat(void)
{
    return bw_mod_catalog_read != NULL && bw_mod_catalog_poll != NULL;
}

/* The mods the window lists, as indices of the mod list: those whose files
 * are there and that work with the game loaded.  How many. */
static int mods_listed(u32 *idx, int *on)
{
    u32 n = bw_mod_menu_count(), i;
    int k = 0;

    *on = 0;
    for (i = 0; i < n && k < MODS_MAX; i++) {
        if (bw_mod_menu_present_at(i) && bw_mod_menu_needs_game_at(i) == 0) {
            *on += bw_mod_menu_enabled_at(i) != 0;
            idx[k++] = i;
        }
    }
    return k;
}

/* The flag at the menu's top left: MODS over how many are on.  The style
 * flag's own box (the same panel, or the style's own "style-flag" plate).
 * Returns its box for the pointer. */
static EidosUiRect mods_flag(EidosUiCtx *c, int hot, int on)
{
    EidosUiRect r;
    int fx, fy, fw, fh, lean = 0, left, cwid;
    char line[24];

    r.w = STYLE_FLAG_W;
    r.h = STYLE_FLAG_H;
    r.x = 24 + c->safe_l;
    r.y = 0;
    fx = r.x;
    fy = r.y;
    fw = r.w;
    fh = hot ? -r.h : r.h;
    if (ui_app_window(c, "style-flag", &fx, &fy, &fw, &fh)) {
        left = fx;
        lean = fw;
        cwid = r.w - 22;
    } else {
        ui_panel(c, r.x, r.y - 8, r.w, r.h + 8, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, hot ? UI_CYAN : UI_EDGE, 240);
        left = r.x + 8;
        cwid = r.w - 16;
    }
#define FLAG_X(row) (left + lean * (r.h - (row)) / r.h)
    ui_text(c, FLAG_X(6) + (cwid - ui_text_w("MODS", UI_FONT_SMALL)) / 2, r.y + 3, "MODS", UI_RAW(0xF8F8F8u),
            (s32)UI_RAW(0x101014u), UI_FONT_SMALL, 1);
    snprintf(line, sizeof line, ui_trf("%d ON"), on);
    ui_text(c, FLAG_X(19) + (cwid - ui_text_w(line, UI_FONT_SMALL)) / 2, r.y + 15, line, UI_RAW(0xBEC4D0u),
            (s32)UI_RAW(0x101014u), UI_FONT_SMALL, 1);
#undef FLAG_X
    return r;
}

static void mods_show(void)
{
    bw_mod_menu_open();         /* the list as it stands on disk and in the player's record */
    g_mods_open = 1;
    g_mods_cur = 0;
    g_mods_first = 0;
    g_mods_tab = mods_cat() ? 1 : 0;    /* it opens on the mods to get */
    g_mods_ask = 0;
    g_mods_note[0] = '\0';
    g_mods_follow[0] = '\0';
    g_mods_wait = 0;
    if (mods_cat()) {
        bw_mod_catalog_read(600);       /* the index, unless read in the last ten minutes */
    }
    eidos_ui_sound(0x54C);
    eidos_ui_crossfade();
}

/* On the game's thread: the switches take effect (mods stop and load). */
static void mods_apply(void *arg)
{
    (void)arg;
    (void)bw_mod_menu_apply();
}

/* On the game's thread: the index's work that changes the mod list. */
static void mods_install_call(void *arg)
{
    if (bw_mod_menu_dirty()) {
        (void)bw_mod_menu_apply();      /* (the install's end reads the list again: the switches first) */
    }
    if (bw_mod_catalog_install((u32)(uintptr_t)arg) != 0) {
        snprintf(g_mods_note, sizeof g_mods_note, "%s", ui_tr("Could not start download."));
        g_mods_wait = 0;
    }
}

static void mods_finish_call(void *arg)
{
    (void)arg;
    (void)bw_mod_catalog_finish();
    g_mods_job_sent = 0;
}

static void mods_remove_call(void *arg)
{
    (void)arg;
    if (bw_mod_menu_dirty()) {
        (void)bw_mod_menu_apply();
    }
    if (bw_mod_catalog_remove(g_mods_ask_root) != 0) {
        snprintf(g_mods_note, sizeof g_mods_note, "%s", bw_mod_catalog_job_error());
        g_mods_note_id[0] = '\0';
    }
}

static void mods_close(void)
{
    if (bw_mod_menu_dirty()) {
        (void)eidos_ui_game_call(mods_apply, NULL);
    }
    if (mods_cat()) {
        bw_mod_catalog_stop();          /* (a list still being read) */
    }
    g_mods_open = 0;
    g_mods_cool = 15;
    eidos_ui_sound(0x551);
    eidos_ui_crossfade();
}

static void mods_turn(u32 index)
{
    (void)bw_mod_menu_set_enabled(index, !bw_mod_menu_enabled_at(index));
    eidos_ui_sound(0x54C);
}

typedef struct ModsWrap { EidosUiCtx *c; int x, y, w, max_y; u32 ink; } ModsWrap;

static void mods_wrap_line(void *user, const char *start, int len)
{
    ModsWrap *o = (ModsWrap *)user;
    char buf[160];

    if (len >= (int)sizeof(buf) - 3) {
        len = (int)sizeof(buf) - 4;
    }
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    if (o->y + 32 > o->max_y && start[len] != '\0') {
        /* the last line there is room for, and the text goes on: it says so */
        size_t n = (size_t)len;

        for (;;) {
            while (n > 0 && buf[n - 1] == ' ') {
                n--;
            }
            memcpy(buf + n, "...", 4);
            if (n == 0 || ui_text_w(buf, UI_FONT_LARGE) <= o->w) {
                break;
            }
            do {
                n--;
            } while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80);
        }
    }
    if (o->y + 16 <= o->max_y) {
        ui_text(o->c, o->x, o->y, buf, o->ink, UI_EDGE, UI_FONT_LARGE, 1);
    }
    o->y += 16;
}

/* The text, or its start with "..." when it is wider than `width` (cut at
 * a whole character, into `buf`). */
static const char *mods_fit(const char *text, int width, int font, char *buf, size_t size)
{
    size_t n, most = (size_t)(width / 2 + 8);   /* (no letter is narrower than two rows) */

    if (ui_text_w(text, font) <= width) {
        return text;
    }
    if (most > size - 4) {
        most = size - 4;
    }
    snprintf(buf, most + 1, "%s", text);
    n = strlen(buf);
    while (n > 0) {
        do {
            n--;
        } while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80);
        while (n > 0 && buf[n - 1] == ' ') {
            n--;
        }
        memcpy(buf + n, "...", 4);
        if (ui_text_w(buf, font) <= width) {
            break;
        }
    }
    return buf;
}

/* A size for a person: "827 KB", "24.0 MB". */
static void mods_size_text(unsigned long long bytes, char *out, size_t size)
{
    if (bytes >= 1024ull * 1024ull) {
        unsigned long long tenths = bytes * 10ull / (1024ull * 1024ull);

        snprintf(out, size, "%llu.%llu MB", tenths / 10ull, tenths % 10ull);
    } else {
        snprintf(out, size, "%llu KB", (bytes + 1023ull) / 1024ull);
    }
}

/* The index's mod that is installed in `root`, or -1. */
static int mods_cat_of(const char *root)
{
    u32 i, n;

    if (!mods_cat() || root[0] == '\0') {
        return -1;
    }
    n = bw_mod_catalog_count();
    for (i = 0; i < n; i++) {
        if (bw_mod_catalog_installed_at(i) != 0 && strcmp(bw_mod_catalog_root_at(i), root) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * The list's lines for this frame, and how many.  `tab`: 0 the installed
 * mods, 1 the index's.  `counts`: how many installed mods and how many of
 * the index are listed.
 */
static int mods_lines(ModsLine *line, int tab, const u32 *idx, int n_inst, int cat_state, int counts[2])
{
    int n = 0, y = 0, i;
    u32 total = mods_cat() ? bw_mod_catalog_count() : 0, k;

#define MODS_ADD(k_, ref_, h_, gap_) do { line[n].kind = (k_); line[n].ref = (int)(ref_); line[n].y = y; \
                                          line[n].h = (h_); n++; y += (h_) + (gap_); } while (0)
    counts[0] = n_inst;
    counts[1] = 0;
    for (k = 0; k < total; k++) {
        counts[1] += bw_mod_catalog_installed_at(k) == 0;
    }
    if (tab != 1) {
        for (i = 0; i < n_inst; i++) {
            MODS_ADD(MROW_MOD, idx[i], MODS_ROW, MODS_GAP);
        }
        if (n_inst == 0) {
            MODS_ADD(MROW_NOTE, 3, 20, MODS_GAP);
        }
    }
    if (tab != 0 && mods_cat()) {
        for (k = 0; k < total && n < MODS_ROWS_MAX - 2; k++) {
            if (bw_mod_catalog_installed_at(k) == 0) {
                MODS_ADD(MROW_GET, k, MODS_ROW, MODS_GAP);
            }
        }
        if (cat_state == CAT_READING || cat_state == CAT_NONE) {
            MODS_ADD(MROW_NOTE, 0, 20, MODS_GAP);
        } else if (cat_state == CAT_FAILED) {
            MODS_ADD(MROW_NOTE, 1, MODS_ROW, MODS_GAP);
        } else if (counts[1] == 0) {
            MODS_ADD(MROW_NOTE, 2, 20, MODS_GAP);
        }
    }
#undef MODS_ADD
    return n;
}

/* Can the pad's choice stand on this line? */
static int mods_can_choose(const ModsLine *l)
{
    return l->kind == MROW_MOD || l->kind == MROW_GET || (l->kind == MROW_NOTE && l->ref == 1);
}

/* The window in the title menu's place.  1 while it is up: the menu is not
 * drawn and takes no key. */
static int mods_screen(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, int W, int H, int ready)
{
    static ModsLine line[MODS_ROWS_MAX];
    const char *hints[12];
    u32 hint_keys[6];
    u32 k = eidos_ui_keys_pressed(), idx[MODS_MAX];
    int bar = ui_bar_h(c), n_inst, on, i, x0, y0, ww, wh, top, foot, lx, lw, px, pw, nl, counts[2];
    int cat_state = CAT_NONE, job = JOB_NONE, job_cat = -1, busy, tabs = mods_cat();
    int list_h, all_h, cur_y = 0, cur_h = 0, nh = 0, act = 0, act_line = -1, follow_now = 0;
    /* a row is the style's own card where it has one, its text past the card's cap */
    const int chip = ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL;
    const int tpad = chip ? 5 + (ui_theme_active()->mp_pad > 0 ? ui_theme_active()->mp_pad : 8) + 4 : 10;
    unsigned long long job_got = 0, job_total = 0;
    EidosUiRect list;
    char right[48];

    (void)view;
    if (!mods_on()) {
        return 0;
    }
    eidos_ui_take_keys(BW_KEY_Y);
    if (g_mods_cool > 0) {
        g_mods_cool--;                  /* the key that closed it is not the menu's */
        eidos_ui_take_keys(BW_KEY_GAME);
    }
    if (!g_mods_open) {
        if (ready && g_mods_cool == 0 && (k & BW_KEY_Y)) {
            mods_show();
        } else {
            return 0;
        }
        k = 0;
    }
    eidos_ui_take_keys(BW_KEY_GAME);

    /* the index's state, and an install's end handed to the game's thread */
    if (mods_cat()) {
        cat_state = bw_mod_catalog_poll();
        job = bw_mod_catalog_job(&job_cat, &job_got, &job_total);
        if ((job == JOB_DONE || job == JOB_FAILED) && !g_mods_job_sent) {
            if (job == JOB_FAILED) {
                snprintf(g_mods_note, sizeof g_mods_note, "%s", g_mods_stopped ? "" : bw_mod_catalog_job_error());
            } else {
                g_mods_note[0] = '\0';
                g_mods_tab = 0;         /* the new mod is among the installed ones */
            }
            g_mods_job_sent = eidos_ui_game_call(mods_finish_call, NULL);
        }
    }
    if (g_mods_wait == 1 && job != JOB_NONE) {
        g_mods_wait = 2;
    } else if (g_mods_wait == 2 && job == JOB_NONE && !g_mods_job_sent) {
        g_mods_wait = 0;                /* it ended, and the mod list was read again */
        follow_now = 1;
    }
    busy = job != JOB_NONE || g_mods_wait != 0;
    if (tabs && !busy && !g_mods_ask && (k & (BW_KEY_L | BW_KEY_R | BW_KEY_LEFT | BW_KEY_RIGHT))) {
        g_mods_tab = (k & (BW_KEY_R | BW_KEY_RIGHT)) ? 1 : 0;
        g_mods_cur = 0;
        g_mods_first = 0;
        eidos_ui_sound(0x548);
    }

    n_inst = mods_listed(idx, &on);
    nl = mods_lines(line, tabs ? g_mods_tab : 0, idx, n_inst, cat_state, counts);

    /* the list's choice goes to the mod that was just installed */
    if (follow_now && g_mods_follow[0] != '\0') {
        for (i = 0; i < nl; i++) {
            if (line[i].kind == MROW_MOD) {
                int ci = mods_cat_of(bw_mod_menu_root_at((u32)line[i].ref));

                if (ci >= 0 && strcmp(bw_mod_catalog_id_at((u32)ci), g_mods_follow) == 0) {
                    g_mods_cur = i;
                    g_mods_follow[0] = '\0';
                    break;
                }
            }
        }
        g_mods_follow[0] = '\0';        /* (it failed: the choice stays) */
    }
    /* the choice on a line it can stand on */
    if (g_mods_cur >= nl) {
        g_mods_cur = nl - 1;
    }
    if (g_mods_cur < 0) {
        g_mods_cur = 0;
    }
    for (i = g_mods_cur; i < nl && !mods_can_choose(&line[i]); i++) {
    }
    if (i >= nl) {
        for (i = g_mods_cur; i >= 0 && !mods_can_choose(&line[i]); i--) {
        }
    }
    g_mods_cur = i;                     /* (-1: nothing to choose) */

    if (!g_mods_ask && !busy) {
        int step = (k & BW_KEY_DOWN) ? 1 : (k & BW_KEY_UP) ? -1 : 0;

        for (i = g_mods_cur + step; step != 0 && i >= 0 && i < nl; i += step) {
            if (mods_can_choose(&line[i])) {
                g_mods_cur = i;
                eidos_ui_sound(0x548);
                break;
            }
        }
    }

    /* on the title's own floor, dimmed, as the style chooser is */
    backdrop(c, W, H, (int)in->frame);
    ui_fill(c, 0, 0, W, H, UI_RAW(0x000000u), 150);
    x0 = 10 + c->safe_l;
    y0 = 8;
    ww = W - 20 - c->safe_l - c->safe_r;
    wh = H - bar - 16;
    ui_panel(c, x0, y0, ww, wh, UI_CUT_TL | UI_CUT_BR, 9, UI_BODY, UI_EDGE, 246);
    snprintf(right, sizeof right, ui_trf("%d ON"), on);
    ui_header(c, x0 + 4, y0 + 4, ww - 8, "MODS", "", right, UI_CYAN);
    top = y0 + 4 + 22 + 6;
    foot = y0 + wh - 8;
    lx = x0 + 8;
    lw = (ww - 16) * 44 / 100;

    /* the two tabs over the list */
    if (tabs) {
        int tx = lx, t;

        for (t = 0; t < 2; t++) {
            EidosUiRect tr;
            char label[40];
            int st;

            if (t == 1 && cat_state != CAT_READY) {
                /* (no count yet: the label says what the index does) */
                snprintf(label, sizeof label, "%s %s", ui_tr("AVAILABLE"), cat_state == CAT_FAILED ? "!" : "...");
            } else {
                snprintf(label, sizeof label, t == 0 ? ui_trf("INSTALLED %d") : ui_trf("AVAILABLE %d"), counts[t]);
            }
            tr.x = tx;
            tr.y = top;
            tr.w = ui_text_w(label, UI_FONT_SMALL) + 16;
            tr.h = 14;
            st = eidos_ui_hit(tr);
            tx += ui_tab(c, tx, top, label, t == g_mods_tab || (st & EIDOS_UI_HOVER) != 0,
                         t == g_mods_tab ? UI_CYAN : UI_EDGE2, t == g_mods_tab ? UI_CYAN2 : UI_BODY2) + 3;
            if ((st & EIDOS_UI_CLICK) && t != g_mods_tab && !busy && !g_mods_ask) {
                g_mods_tab = t;
                g_mods_cur = 0;
                g_mods_first = 0;
                eidos_ui_sound(0x548);
            }
        }
        top += 14 + 5;
    }

    /* the list, scrolled by canvas rows */
    list_h = foot - top;
    all_h = nl > 0 ? line[nl - 1].y + line[nl - 1].h : 0;
    list.x = lx;
    list.y = top;
    list.w = lw + 8;
    list.h = list_h;
    g_mods_first += eidos_ui_wheel(list) * (MODS_ROW + MODS_GAP);
    if (g_mods_cur >= 0) {
        cur_y = line[g_mods_cur].y;
        cur_h = line[g_mods_cur].h;
        if ((k & (BW_KEY_UP | BW_KEY_DOWN)) || busy) {
            /* the pad's choice stays in view */
            if (cur_y < g_mods_first) {
                g_mods_first = cur_y;
            } else if (cur_y + cur_h > g_mods_first + list_h) {
                g_mods_first = cur_y + cur_h - list_h;
            }
        }
    }
    if (g_mods_first > all_h - list_h) {
        g_mods_first = all_h - list_h;
    }
    if (g_mods_first < 0) {
        g_mods_first = 0;
    }
    {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, lx, top, lw, list_h);
        for (i = 0; i < nl; i++) {
            const ModsLine *l = &line[i];
            EidosUiRect r, sw;
            EidosUiRect saved2;
            int st, sel = i == g_mods_cur;

            r.x = lx;
            r.y = top + l->y - g_mods_first;
            r.w = lw;
            r.h = l->h;
            if (r.y + r.h <= top || r.y >= foot) {
                continue;
            }
            if (l->kind == MROW_NOTE && l->ref != 1) {
                const char *text = l->ref == 0 ? ui_tr("Loading mod list...")
                                 : l->ref == 2 ? ui_tr("All available mods are installed.")
                                               : ui_tr("No mods installed.");

                if (l->ref == 0) {
                    ui_spinner(c, r.x + 4, r.y + 4, (int)(in->frame / 6u), UI_CYAN);
                }
                ui_text(c, r.x + (l->ref == 0 ? 22 : 4), r.y + 3, text, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
                continue;
            }
            {
                EidosUiRect shown = r;

                if (shown.y < top) {
                    shown.h -= top - shown.y;
                    shown.y = top;
                }
                if (shown.y + shown.h > foot) {
                    shown.h = foot - shown.y;
                }
                st = (g_mods_ask || busy) ? 0 : eidos_ui_hit(shown);
            }
            if (chip) {
                /* the style's own card (Plasma's leaning chip, its cap leaning with it), drawn a
                 * little inside the row so its leaning ends stay in the list */
                ui_theme_active()->mp_part(c, 0, r.x + 5, r.y, r.w - 10, r.h, sel, (st & EIDOS_UI_HOVER) != 0, 1);
            } else {
                ui_row(c, r.x, r.y, r.w, r.h, sel, (st & EIDOS_UI_HOVER) != 0, &UI_THEME_MENU);
            }
            ui_push(c, 0, 0, &saved2);
            {
                /* (the row's text, inside the part of the row the list shows) */
                int cy0 = r.y < top ? top : r.y, cy1 = r.y + r.h > foot ? foot : r.y + r.h;

                ui_clip(c, r.x + 6, cy0, r.w - 50, cy1 - cy0);
            }
            if (l->kind == MROW_NOTE) {
                ui_text(c, r.x + tpad, r.y + 3, ui_tr("Could not load mod list."), UI_WHITE, UI_EDGE,
                        UI_FONT_LARGE, 1);
                {
                    char why[200], fit[200];

                    /* (the reason is written to follow a colon: here it stands alone) */
                    snprintf(why, sizeof why, "%s", bw_mod_catalog_error());
                    if (why[0] >= 'a' && why[0] <= 'z') {
                        why[0] = (char)(why[0] - 'a' + 'A');
                    }
                    ui_text(c, r.x + tpad, r.y + 19, mods_fit(why, r.w - 10 - tpad, UI_FONT_LARGE, fit, sizeof fit),
                            UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
                }
                ui_pop(c, 0, 0, &saved2);
                if (st & EIDOS_UI_CLICK) {
                    act = sel ? 'A' : 0;
                    act_line = i;
                    g_mods_cur = i;
                }
                continue;
            }
            {
                const char *name = l->kind == MROW_MOD ? bw_mod_menu_name_at((u32)l->ref)
                                                       : bw_mod_catalog_name_at((u32)l->ref);
                const char *about = l->kind == MROW_MOD
                                    ? (bw_mod_menu_about_at != NULL ? bw_mod_menu_about_at((u32)l->ref, 0) : "")
                                    : bw_mod_catalog_about_at((u32)l->ref);
                char fit[200];
                int room = r.w - 48 - tpad;     /* up to the switch, or the word at the right */

                /* (a mod with no description: its name alone, in the row's middle) */
                ui_text(c, r.x + tpad, about[0] != '\0' ? r.y + 3 : r.y + ui_text_y(MODS_ROW, UI_FONT_LARGE),
                        mods_fit(name, room - (l->kind == MROW_MOD ? 44 : 0), UI_FONT_LARGE, fit, sizeof fit),
                        UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
                ui_text(c, r.x + tpad, r.y + 19, mods_fit(about, room, UI_FONT_LARGE, fit, sizeof fit), UI_GREY,
                        UI_EDGE, UI_FONT_LARGE, 1);
            }
            ui_pop(c, 0, 0, &saved2);
            sw.x = lx + lw - 40;
            sw.y = r.y;
            sw.w = 40;
            sw.h = MODS_ROW;
            if (l->kind == MROW_MOD) {
                int ci = mods_cat_of(bw_mod_menu_root_at((u32)l->ref));

                ui_toggle(c, sw.x + 10, r.y + (MODS_ROW - 10) / 2, bw_mod_menu_enabled_at((u32)l->ref) != 0, UI_CYAN);
                if (ci >= 0 && bw_mod_catalog_installed_at((u32)ci) == 2) {
                    /* a newer version is in the index */
                    ui_text(c, sw.x - 4 - ui_text_w(ui_tr("UPDATE"), UI_FONT_SMALL), r.y + 4, ui_tr("UPDATE"),
                            sel ? UI_WHITE : UI_CYAN, UI_EDGE, UI_FONT_SMALL, 1);
                }
            } else if (job_cat == l->ref && busy) {
                char pct[12];

                snprintf(pct, sizeof pct, "%d%%", job_total != 0 ? (int)(job_got * 100ull / job_total) : 0);
                ui_text(c, lx + lw - 8 - ui_text_w(pct, UI_FONT_SMALL), r.y + (MODS_ROW - 8) / 2, pct,
                        sel ? UI_WHITE : UI_CYAN, UI_EDGE, UI_FONT_SMALL, 1);
            } else {
                ui_text(c, lx + lw - 8 - ui_text_w(ui_tr("GET"), UI_FONT_SMALL), r.y + (MODS_ROW - 8) / 2,
                        ui_tr("GET"), sel ? UI_WHITE : UI_CYAN, UI_EDGE, UI_FONT_SMALL, 1);
            }
            if (st & EIDOS_UI_CLICK) {
                if (sel || (l->kind == MROW_MOD && (eidos_ui_hit(sw) & EIDOS_UI_CLICK))) {
                    act = 'A';
                    act_line = i;
                } else {
                    eidos_ui_sound(0x548);
                }
                g_mods_cur = i;
            }
        }
        ui_pop(c, 0, 0, &saved);
    }
    if (all_h > list_h) {
        ui_scrollbar(c, lx + lw + 3, top, list_h, g_mods_first, list_h, all_h, UI_CYAN);
    }

    /* the chosen mod */
    px = lx + lw + 12;
    pw = x0 + ww - 8 - px;
    ui_panel(c, px, y0 + 4 + 22 + 6, pw, foot - (y0 + 4 + 22 + 6), UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 255);
    if (g_mods_cur >= 0 && line[g_mods_cur].kind != MROW_NOTE) {
        const ModsLine *l = &line[g_mods_cur];
        int inst = l->kind == MROW_MOD;
        u32 m = (u32)l->ref;
        int ci = inst ? mods_cat_of(bw_mod_menu_root_at(m)) : l->ref;
        const char *name = inst ? bw_mod_menu_name_at(m) : bw_mod_catalog_name_at(m);
        const char *by = inst ? (bw_mod_menu_about_at != NULL ? bw_mod_menu_about_at(m, 1) : "") : bw_mod_catalog_author_at(m);
        const char *ver = inst ? (bw_mod_menu_about_at != NULL ? bw_mod_menu_about_at(m, 2) : "") : bw_mod_catalog_version_at(m);
        const char *text = inst ? (bw_mod_menu_about_at != NULL ? bw_mod_menu_about_at(m, 0) : "") : bw_mod_catalog_about_at(m);
        int update = inst && ci >= 0 && bw_mod_catalog_installed_at((u32)ci) == 2;
        int removable = inst && bw_mod_catalog_can_remove != NULL && bw_mod_catalog_can_remove(bw_mod_menu_root_at(m));
        int mine = busy && ci >= 0 && ci == job_cat;    /* the install is this mod's */
        int ptop = y0 + 4 + 22 + 6, tx = px + 12, ty = ptop + 10, tw = pw - 24, by_y = foot - 34;
        char buf[160];
        EidosUiRect saved;
        ModsWrap o;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, tx, ptop + 2, tw, foot - ptop - 4);
        ui_text(c, tx, ty, name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 2);
        ty += 34;
        if (by[0] != '\0') {
            snprintf(buf, sizeof buf, ui_trf("by %s"), by);
            ui_text(c, tx, ty, buf, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        }
        if (ver[0] != '\0') {
            snprintf(buf, sizeof buf, ui_trf("Version %s"), ver);
            ui_text(c, tx + tw - ui_text_w(buf, UI_FONT_LARGE), ty, buf, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        }
        ty += 20;
        ui_fill(c, tx, ty, tw, 1, UI_EDGE, 255);
        o.c = c;
        o.x = tx;
        o.y = ty + 8;
        o.w = tw;
        o.max_y = by_y - 40;
        o.ink = UI_WHITE;
        if (text[0] != '\0') {
            (void)ui_wrap(text, tw, UI_FONT_LARGE, mods_wrap_line, &o);
        }
        ui_pop(c, 0, 0, &saved);

        /* the line over the actions: what an install does now, what went
         * wrong, a newer version, or the download's size */
        if (mine) {
            char a[24], b[24];

            mods_size_text(job_got, a, sizeof a);
            mods_size_text(job_total, b, sizeof b);
            if (job == JOB_DOWNLOADING) {
                snprintf(buf, sizeof buf, ui_trf("Downloading: %s of %s"), a, b);
            } else {
                snprintf(buf, sizeof buf, "%s", ui_tr("Installing..."));
            }
            ui_text(c, tx, by_y - 22, buf, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            ui_progress(c, tx, by_y + 4, tw, job == JOB_DOWNLOADING ? (int)(job_got >> 10) : 1,
                        job == JOB_DOWNLOADING ? (int)(job_total >> 10) : 1, UI_CYAN);
        } else {
            if (g_mods_note[0] != '\0' && (g_mods_note_id[0] == '\0' ||
                                           (ci >= 0 && strcmp(g_mods_note_id, bw_mod_catalog_id_at((u32)ci)) == 0))) {
                o.y = by_y - 38;
                o.max_y = by_y - 2;
                o.ink = UI_PINK;
                (void)ui_wrap(g_mods_note, tw, UI_FONT_LARGE, mods_wrap_line, &o);
            } else if (update) {
                snprintf(buf, sizeof buf, ui_trf("Version %s is available."), bw_mod_catalog_version_at((u32)ci));
                ui_text(c, tx, by_y - 22, buf, UI_CYAN, UI_EDGE, UI_FONT_LARGE, 1);
            } else if (!inst) {
                char a[24];

                mods_size_text(bw_mod_catalog_size_at(m), a, sizeof a);
                snprintf(buf, sizeof buf, ui_trf("Download: %s"), a);
                ui_text(c, tx, by_y - 22, buf, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
            }
            /* the actions, each with the key the hint bar names for it */
            if (!busy && !g_mods_ask) {
                const char *label[3];
                int key[3], nb = 0, b, bw2, gap = 8;

                if (inst) {
                    label[nb] = bw_mod_menu_enabled_at(m) ? ui_tr("TURN OFF") : ui_tr("TURN ON");
                    key[nb++] = 'A';
                    if (update) {
                        label[nb] = ui_tr("UPDATE");
                        key[nb++] = 'X';
                    }
                    if (removable) {
                        label[nb] = ui_tr("REMOVE");
                        key[nb++] = 'Y';
                    }
                } else {
                    label[nb] = ui_tr("GET");
                    key[nb++] = 'A';
                }
                bw2 = (tw - gap * (nb - 1)) / nb;
                if (bw2 > 150) {
                    bw2 = 150;
                }
                for (b = 0; b < nb; b++) {
                    EidosUiRect br;
                    int st;

                    br.x = tx + b * (bw2 + gap);
                    br.y = by_y;
                    br.w = bw2;
                    br.h = 24;
                    st = eidos_ui_hit(br);
                    ui_key(c, br.x, br.y, br.w, label[b], (st & EIDOS_UI_HOVER) != 0, UI_CYAN);
                    if (st & EIDOS_UI_CLICK) {
                        act = key[b];
                        act_line = g_mods_cur;
                    }
                }
                /* the pad's keys for the same */
                if (act == 0) {
                    if (k & BW_KEY_A) {
                        act = 'A';
                    } else if ((k & BW_KEY_X) && update) {
                        act = 'X';
                    } else if ((k & BW_KEY_Y) && removable) {
                        act = 'Y';
                    }
                    act_line = act != 0 ? g_mods_cur : act_line;
                }
                hints[nh] = "A";
                hints[nh + 1] = inst ? "On / Off" : "Get";
                hint_keys[nh / 2] = BW_KEY_A;
                nh += 2;
                if (update) {
                    hints[nh] = "X";
                    hints[nh + 1] = "Update";
                    hint_keys[nh / 2] = BW_KEY_X;
                    nh += 2;
                }
                if (removable) {
                    hints[nh] = "Y";
                    hints[nh + 1] = "Remove";
                    hint_keys[nh / 2] = BW_KEY_Y;
                    nh += 2;
                }
            }
        }
    } else if (g_mods_cur >= 0 && !busy && !g_mods_ask) {
        /* the line that says the list could not be read: A asks again */
        if (k & BW_KEY_A) {
            act = 'A';
            act_line = g_mods_cur;
        }
        hints[nh] = "A";
        hints[nh + 1] = "Try again";
        hint_keys[nh / 2] = BW_KEY_A;
        nh += 2;
    }

    /* what was asked for */
    if (act != 0 && act_line >= 0 && !busy && !g_mods_ask) {
        const ModsLine *l = &line[act_line];

        if (l->kind == MROW_NOTE) {
            bw_mod_catalog_read(0);
            eidos_ui_sound(0x54C);
        } else if (l->kind == MROW_GET || act == 'X') {
            int ci = l->kind == MROW_GET ? l->ref : mods_cat_of(bw_mod_menu_root_at((u32)l->ref));

            if (ci >= 0 && cat_state == CAT_READING) {
                /* (the list on screen is the one before: its mods are asked for when the new one is in) */
                snprintf(g_mods_note, sizeof g_mods_note, "%s", ui_tr("Loading mod list. Try again in a moment."));
                snprintf(g_mods_note_id, sizeof g_mods_note_id, "%s", bw_mod_catalog_id_at((u32)ci));
            } else if (ci >= 0) {
                snprintf(g_mods_follow, sizeof g_mods_follow, "%s", bw_mod_catalog_id_at((u32)ci));
                snprintf(g_mods_note_id, sizeof g_mods_note_id, "%s", g_mods_follow);
                g_mods_note[0] = '\0';
                g_mods_stopped = 0;
                g_mods_wait = eidos_ui_game_call(mods_install_call, (void *)(uintptr_t)(u32)ci) ? 1 : 0;
                eidos_ui_sound(0x54C);
            }
        } else if (act == 'Y') {
            snprintf(g_mods_ask_root, sizeof g_mods_ask_root, "%s", bw_mod_menu_root_at((u32)l->ref));
            snprintf(g_mods_ask_name, sizeof g_mods_ask_name, "%s", bw_mod_menu_name_at((u32)l->ref));
            g_mods_ask = 1;
            g_mods_ask_cur = 1;         /* on "keep" */
            eidos_ui_sound(0x54C);
            k = 0;
        } else {
            mods_turn((u32)l->ref);
        }
    }

    /* the remove question, over the window */
    if (g_mods_ask) {
        const char *ch[2];
        char q[160];
        int dw = ww - 80 < 300 ? ww - 80 : 300, dh, dx, dy, ans, chosen = -1;

        ch[0] = ui_tr("REMOVE");
        ch[1] = ui_tr("KEEP");
        snprintf(q, sizeof q, ui_trf("Remove %s? Its save data is kept."), g_mods_ask_name);
        dh = 14 + 15 * ui_wrap(q, dw - 20, UI_FONT_LARGE, NULL, NULL) + 26;
        dx = x0 + (ww - dw) / 2;
        dy = y0 + (wh - dh) / 2;
        ui_fill(c, x0, y0, ww, wh, UI_INK, 120);
        if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
            g_mods_ask_cur = (k & BW_KEY_LEFT) ? 0 : 1;
            eidos_ui_sound(0x548);
        }
        ui_dialog(c, dx, dy, dw, q, ch, 2, g_mods_ask_cur, UI_CYAN);
        /* the two buttons ui_dialog draws: 56 wide, right aligned */
        for (ans = 0; ans < 2; ans++) {
            EidosUiRect br;
            int st;

            br.x = dx + dw - 10 - 62 * (2 - ans) + 6;
            br.y = dy + dh - 24;
            br.w = 56;
            br.h = 18;
            st = eidos_ui_hit(br);
            if (st & EIDOS_UI_HOVER) {
                ui_fill(c, br.x, br.y, br.w, br.h, UI_CYAN2, 90);
            }
            if (st & EIDOS_UI_CLICK) {
                chosen = ans;
            }
        }
        if (k & BW_KEY_A) {
            chosen = g_mods_ask_cur;
        }
        if ((k & BW_KEY_B) || eidos_ui_right_click()) {
            chosen = 1;
        }
        if (chosen == 0) {
            g_mods_note[0] = '\0';
            (void)eidos_ui_game_call(mods_remove_call, NULL);
            eidos_ui_sound(0x54C);
        } else if (chosen == 1) {
            eidos_ui_sound(0x551);
        }
        if (chosen >= 0) {
            g_mods_ask = 0;
        }
        hints[0] = "A";
        hints[1] = "Confirm";
        hints[2] = "\xE2\x86\x90\xE2\x86\x92";
        hints[3] = "Choose";
        hints[4] = "B";
        hints[5] = "Keep";
        hint_keys[0] = BW_KEY_A;
        hint_keys[1] = BW_KEY_RIGHT;
        hint_keys[2] = BW_KEY_B;
        ui_hints(c, H - bar, W, hints, 3, hint_keys);
        return 1;
    }

    if (busy) {
        /* an install: the download can be stopped, the unpacking not */
        nh = 0;
        if (job == JOB_DOWNLOADING) {
            hints[0] = "B";
            hints[1] = "Stop";
            hint_keys[0] = BW_KEY_B;
            nh = 2;
            if ((k & BW_KEY_B) || eidos_ui_right_click()) {
                bw_mod_catalog_stop();
                g_mods_stopped = 1;
                eidos_ui_sound(0x551);
            }
        }
        ui_hints(c, H - bar, W, hints, nh / 2, hint_keys);
        return 1;
    }
    hints[nh] = "\xE2\x86\x91\xE2\x86\x93";
    hints[nh + 1] = "Mod";
    hint_keys[nh / 2] = BW_KEY_DOWN;
    nh += 2;
    if (tabs) {
        hints[nh] = "\xE2\x86\x90\xE2\x86\x92";
        hints[nh + 1] = "Tab";
        hint_keys[nh / 2] = BW_KEY_RIGHT;
        nh += 2;
    }
    hints[nh] = "B";
    hints[nh + 1] = "Back";
    hint_keys[nh / 2] = BW_KEY_B;
    nh += 2;
    ui_hints(c, H - bar, W, hints, nh / 2, hint_keys);
    if ((k & BW_KEY_B) || eidos_ui_right_click()) {
        mods_close();
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The other version of the game                                       */
/* ------------------------------------------------------------------ */

/*
 * A standalone game that plays from the two game profiles can go to its
 * other version without leaving the program (demo/eidos_boot.c,
 * eidos_boot_game_switch).  A third flag, between the mod manager's and
 * the style's, names the two versions, the one played marked as the style
 * in use is on the style's flag.  A click, or X, goes to the other version
 * when its data is in the install.  When it is not, a window asks for that
 * version's ROM, shows the check and the writing of its game data, and the
 * game goes to it when the data is made.
 */
extern int  eidos_boot_game(void) TT_WEAK;
extern int  eidos_boot_other_game(void) TT_WEAK;
extern int  eidos_boot_game_switch(void) TT_WEAK;
extern int  eidos_boot_game_add(void) TT_WEAK;
extern int  eidos_boot_game_add_state(int *permille, const char **text) TT_WEAK;
extern void eidos_boot_game_add_cancel(void) TT_WEAK;

#define VER_WHITE 2             /* install.h's BW_GAME_WHITE (1 is Black) */
/* eidos_boot_game_add_state's answers */
enum { VADD_NONE, VADD_CHOOSING, VADD_CHECKING, VADD_WRITING, VADD_FAILED, VADD_DONE };

#define VER_FLAG_W 86

static int g_ver_cur;           /* the window's choice: 0 the first, 1 BACK */

static int ver_on(void)
{
    return eidos_boot_other_game != NULL && eidos_boot_game != NULL && eidos_boot_game_switch != NULL
           && eidos_boot_game_add != NULL && eidos_boot_game_add_state != NULL
           && eidos_boot_game_add_cancel != NULL && eidos_boot_other_game() != 0;
}

/* The flag's box: at the menu's top, in the middle, or at the window's
 * foot on the hint bar in a style that asks for it (theme.h,
 * title_version_flag_low). */
static EidosUiRect ver_flag_box(EidosUiCtx *c, int W, int H)
{
    EidosUiRect r;

    r.w = VER_FLAG_W;
    r.h = STYLE_FLAG_H;
    r.x = (W - r.w) / 2;
    r.y = ui_theme_active()->title_version_flag_low ? H - ui_bar_h(c) - r.h : 0;
    return r;
}

/* The flag: VERSION over the two games' names, the one played white over a
 * line, the other grey (dark when its data is not in the install yet).
 * The style flag's own box, hung from the window's top or standing on the
 * hint bar.  Returns its box for the pointer. */
static EidosUiRect ver_flag(EidosUiCtx *c, int W, int H, int hot)
{
    EidosUiRect r = ver_flag_box(c, W, H);
    int low = r.y != 0;
    int white = eidos_boot_game() == VER_WHITE, there = eidos_boot_other_game() == 1;
    int fx, fy, fw, fh, lean = 0, left, cwid, i, w0, w1, x;
    const char *name[2];

    name[0] = ui_tr("BLACK");
    name[1] = ui_tr("WHITE");
    fx = r.x;
    fy = r.y;
    fw = r.w;
    fh = hot ? -r.h : r.h;
    if (!low && ui_app_window(c, "style-flag", &fx, &fy, &fw, &fh)) {
        left = fx;
        lean = fw;
        cwid = r.w - 22;
    } else {
        /* (hung from the top: its head is past the window's edge; on the
         * bar: its foot is under the bar, which is drawn after it) */
        ui_panel(c, r.x, low ? r.y : r.y - 8, r.w, r.h + 8, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY,
                 hot ? UI_CYAN : UI_EDGE, 240);
        left = r.x + 8;
        cwid = r.w - 16;
    }
#define FLAG_X(row) (left + lean * (r.h - (row)) / r.h)
    ui_text(c, FLAG_X(6) + (cwid - ui_text_w(ui_tr("VERSION"), UI_FONT_SMALL)) / 2, r.y + 3, ui_tr("VERSION"),
            UI_RAW(0xF8F8F8u), (s32)UI_RAW(0x101014u), UI_FONT_SMALL, 1);
    w0 = ui_text_w(name[0], UI_FONT_SMALL);
    w1 = ui_text_w(name[1], UI_FONT_SMALL);
    x = FLAG_X(19) + (cwid - (w0 + 8 + w1)) / 2;
    for (i = 0; i < 2; i++) {
        int now = i == white;
        int wd = i == 0 ? w0 : w1;

        ui_text(c, x, r.y + 15, name[i], UI_RAW(now ? 0xF8F8F8u : there ? 0xBEC4D0u : 0x70768Au),
                (s32)UI_RAW(0x101014u), UI_FONT_SMALL, 1);
        if (now) {
            ui_fill(c, x, r.y + 12 + 14, wd, 1, UI_RAW(0xFFFFFFu), 255);
        }
        x += wd + 8;
    }
#undef FLAG_X
    return r;
}

/* The flag was chosen: the other version at once when its data is there,
 * else the window that asks for its ROM. */
static void ver_press(void)
{
    if (eidos_boot_other_game() == 1) {
        if (eidos_boot_game_switch() == 0) {
            eidos_ui_sound(0x54C);
            eidos_ui_crossfade();
            g_ver_cool = 30;            /* the menu is the other game's from the next frames on */
        }
        return;
    }
    g_ver_open = 1;
    g_ver_cur = 0;
    eidos_ui_sound(0x54C);
    eidos_ui_crossfade();
}

static void ver_close(void)
{
    g_ver_open = 0;
    g_ver_cool = 15;
    eidos_ui_sound(0x551);
    eidos_ui_crossfade();
}

/* The window in the menu's place.  Returns whether it is up. */
static int ver_screen(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, int W, int H, int ready)
{
    const char *hints[6];
    u32 hint_keys[3];
    u32 k = eidos_ui_keys_pressed();
    const char *other, *why = "", *ch[2];
    char msg[1200];
    int bar = ui_bar_h(c), state, permille = 0, dw, dh, dx, dy, lines, nch = 0, chosen = -1, ans, busy;

    (void)view;
    if (!ver_on()) {
        g_ver_open = 0;
        return 0;
    }
    eidos_ui_take_keys(BW_KEY_X);
    if (g_ver_cool > 0) {
        g_ver_cool--;                   /* the key that closed it is not the menu's */
        eidos_ui_take_keys(BW_KEY_GAME);
    }
    if (!g_ver_open) {
        if (ready && g_ver_cool == 0 && (k & BW_KEY_X)) {
            ver_press();
        }
        if (!g_ver_open) {
            return 0;
        }
        k = 0;
    }
    eidos_ui_take_keys(BW_KEY_GAME);
    state = eidos_boot_game_add_state(&permille, &why);
    if (state == VADD_NONE && eidos_boot_other_game() == 1) {
        /* the data was made and the game went to that version: the window
         * has nothing left to ask */
        g_ver_open = 0;
        g_ver_cool = 15;
        return 0;
    }
    busy = state == VADD_CHOOSING || state == VADD_CHECKING || state == VADD_WRITING || state == VADD_DONE;
    other = eidos_boot_game() == VER_WHITE ? "Black" : "White";

    backdrop(c, W, H, (int)in->frame);
    switch (state) {
    case VADD_CHOOSING:
        snprintf(msg, sizeof msg, "%s", ui_tr("Select the ROM in the file dialog."));
        break;
    case VADD_CHECKING:
        snprintf(msg, sizeof msg, "%s", ui_tr("Checking ROM..."));
        break;
    case VADD_WRITING:
        snprintf(msg, sizeof msg, "%s", ui_tr("Writing game data..."));
        break;
    case VADD_DONE:
        snprintf(msg, sizeof msg, ui_trf("Starting Pok\xC3\xA9mon %s."), other);
        break;
    case VADD_FAILED: {
        /* the installer's own sentence, which is plain ASCII for a
         * terminal: the game's name as this window spells it */
        static const char plain[] = "Pokemon", name[] = "Pok\xC3\xA9mon";
        size_t n = 0;

        while (*why != '\0' && n + sizeof name < sizeof msg) {
            if (strncmp(why, plain, sizeof plain - 1) == 0) {
                memcpy(msg + n, name, sizeof name - 1);
                n += sizeof name - 1;
                why += sizeof plain - 1;
            } else {
                msg[n++] = *why++;
            }
        }
        msg[n] = '\0';
        ch[nch++] = ui_tr("CHOOSE");
        ch[nch++] = ui_tr("BACK");
        break;
    }
    default:
        snprintf(msg, sizeof msg,
                 ui_trf("Pok\xC3\xA9mon %s is not installed. Select your Pok\xC3\xA9mon %s ROM (.nds)."),
                 other, other);
        ch[nch++] = ui_tr("CHOOSE");
        ch[nch++] = ui_tr("BACK");
        break;
    }
    dw = W - 80 < 320 ? W - 80 : 320;
    lines = ui_wrap(msg, dw - 20, UI_FONT_LARGE, NULL, NULL);
    dh = 14 + 15 * lines + 26;
    dx = (W - dw) / 2;
    dy = (H - bar - dh) / 2;
    if (nch == 2 && (k & (BW_KEY_LEFT | BW_KEY_RIGHT))) {
        g_ver_cur = (k & BW_KEY_LEFT) ? 0 : 1;
        eidos_ui_sound(0x548);
    }
    ui_dialog(c, dx, dy, dw, msg, ch, nch, g_ver_cur, UI_CYAN);
    if (state == VADD_CHECKING || state == VADD_WRITING) {
        /* the bar in the place of the choices, the two steps one after the other */
        ui_progress(c, dx + 10, dy + dh - 16, dw - 20, (state == VADD_WRITING ? 1000 : 0) + permille, 2000, UI_CYAN);
    }
    /* the two buttons ui_dialog draws: 56 wide, right aligned */
    for (ans = 0; ans < nch; ans++) {
        EidosUiRect br;
        int st;

        br.x = dx + dw - 10 - 62 * (nch - ans) + 6;
        br.y = dy + dh - 24;
        br.w = 56;
        br.h = 18;
        st = eidos_ui_hit(br);
        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, br.x, br.y, br.w, br.h, UI_CYAN2, 90);
        }
        if (st & EIDOS_UI_CLICK) {
            chosen = ans;
        }
    }
    if (nch == 2 && (k & BW_KEY_A)) {
        chosen = g_ver_cur;
    }
    if ((k & BW_KEY_B) || eidos_ui_right_click()) {
        chosen = busy ? 2 : 1;
    }
    if (chosen == 0) {
        /* (a failure's message is put away, then the dialog is asked for) */
        eidos_boot_game_add_cancel();
        (void)eidos_boot_game_add();
        eidos_ui_sound(0x54C);
    } else if (chosen == 1) {
        eidos_boot_game_add_cancel();
        ver_close();
    } else if (chosen == 2 && (state == VADD_CHECKING || state == VADD_WRITING)) {
        eidos_boot_game_add_cancel();   /* nothing stays behind; the question comes back */
        eidos_ui_sound(0x551);
    }
    if (nch == 2) {
        hints[0] = "A";
        hints[1] = "Confirm";
        hints[2] = "\xE2\x86\x90\xE2\x86\x92";
        hints[3] = "Choose";
        hints[4] = "B";
        hints[5] = "Back";
        hint_keys[0] = BW_KEY_A;
        hint_keys[1] = BW_KEY_RIGHT;
        hint_keys[2] = BW_KEY_B;
        ui_hints(c, H - bar, W, hints, 3, hint_keys);
    } else if (state == VADD_CHECKING || state == VADD_WRITING) {
        hints[0] = "B";
        hints[1] = "Stop";
        hint_keys[0] = BW_KEY_B;
        ui_hints(c, H - bar, W, hints, 1, hint_keys);
    } else {
        ui_hints(c, H - bar, W, hints, 0, hint_keys);
    }
    return 1;
}

static void menu_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    TtProc *p = NULL;
    EidosUiCanvas *cv = c->canvas;
    int W = cv->w, H = cv->h;
    const void *work;
    int kinds[8], n = 0, i, cursor, state, card = -1, nrows, hook, settings_row;
    int top, avail, slots, wide, row_h, card_h = 0, left_x, left_w, right_x, right_w, y;
    int logo_h = 0, logo_w = 0, logo_y = 0, rows_y, rows_h;
    int hover_card = 0;
    const SaveInfo *info = NULL;
    char label[24] = "";
    static const char *const hints_one[] = { "A", "Choose", "B", "Back" };
    static const char *const hints_slots[] = { "A", "Choose", "\xE2\x86\x91\xE2\x86\x93", "Save",
                                               "B", "Back" };
    static const char *const hints_slots_lr[] = { "A", "Choose", "\xE2\x86\x90\xE2\x86\x92", "Save",
                                                  "B", "Back" };
    static const char *const hints_ask[] = { "A", "Confirm", "B", "Cancel" };
    static const u32 keys_one[] = { BW_KEY_A, BW_KEY_B };
    static const u32 keys_slots[] = { BW_KEY_A, BW_KEY_RIGHT, BW_KEY_B };
    EidosUiRect all;
    char *dbg;
    UiTitleMenuLayout tl;
    const UiThemeOps *theme = ui_theme_active();
    int stacked = 0, across = 0, logo_x = 0;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = title_zoom(in);
    view->blur = 0;
    view->hide_2d = 1;
    scene(&p);
    work = p != NULL ? p->work : NULL;
    if (work == NULL || p->seq < 2) {
        backdrop(c, W, H, (int)in->frame);
        return;
    }
    state = *(const int *)((const char *)work + SM_STATE);
    all.x = 0;
    all.y = 0;
    all.w = W;
    all.h = H - ui_bar_h(c);
    if (!g_mods_open && style_screen(c, view, in, W, H, state == SM_STATE_SELECT)) {
        return;
    }
    if (!g_style_open && eidos_ui_try_state() == 0 && mods_screen(c, view, in, W, H, state == SM_STATE_SELECT)) {
        return;
    }
    if (!g_style_open && !g_mods_open && eidos_ui_try_state() == 0
        && ver_screen(c, view, in, W, H, state == SM_STATE_SELECT)) {
        return;
    }

    /* the game's own questions and notices (states 7 to 10): its windows stay
     * visible, only the keys are offered.  CONTINUE passes through them for
     * a frame on its way out: a state counts once it has held, or the
     * game's DS screen would flash in for that frame. */
    {
        static int ask_for;

        ask_for = state >= 7 && state <= 10 ? ask_for + 1 : 0;
        if (ask_for > 0 && ask_for < 4) {
            state = 0;
        }
    }
    if (state >= 7 && state <= 10) {
        /* the game's questions and notices as the interface's system box
         * over the menu's floor, dimmed (mock-up n9): the message window
         * (StartMenuWork windows[13] 0xE8), the information window
         * (descWindow 0xF0) when it holds text, and the yes/no
         * (yesNoMenu 0x7C, a BmpMenu: items 0x00, selected 0x2D) as a list
         * in the same style */
        const u8 *msgw = *(const u8 *const *)((const char *)work + 0xE8);
        const u8 *desc = *(const u8 *const *)((const char *)work + 0xF0);
        const u8 *yn = *(const u8 *const *)((const char *)work + 0x7C);
        int bw = H * 4 / 3 - 8, bx = (W - bw) / 2, by;
        int lift = 0;

        backdrop(c, W, H, (int)in->frame);
        ui_fill(c, 0, 0, W, H, 0x000000u, 150);
        by = H - ui_bar_h(c) - 4 - 42;
        if (msgw != NULL && (*(const u16 *)msgw & 0x7FFFu) == 0x7FB2u) {
            by = H - ui_bar_h(c) - 4 - (msgw[6] * 8 + 10);
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, bx, by, bw, msgw, -1, -1);
            if (ui_textbox_band() && ui_textbox_h(msgw) > 0) {
                /* a band is docked on the bottom edge wherever it was asked for: the yes/no
                 * goes by where it is, and the notice's band stands on it, not under it */
                by = ui_textbox_place(c, UI_TEXTBOX_SYSTEM, by, ui_textbox_h(msgw));
                lift = ui_textbox_h(msgw) + 6;
            }
        }
        if (desc != NULL && (*(const u16 *)desc & 0x7FFFu) == 0x7FB2u) {
            ui_textbox_band_lift(lift);
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, bx, 8, bw, desc, -1, -1);
            ui_textbox_band_lift(0);
        }
        /* the connect prompt asks at its step 3 (STARTMENU_StateConnectPrompt) */
        if (yn != NULL && state == 7 && *((const u8 *)work + 0x12A) == 3) {
            title_yes_no(c, in, yn, bx + bw, by - 2);
        }
        ui_hints(c, H - ui_bar_h(c), W, hints_ask, 2, keys_one);
        return;
    }
    backdrop(c, W, H, (int)in->frame);

    for (i = 0; i < 8; i++) {
        int k = rd8(work, SM_ROWS + (u32)i);

        if (k == 8) {
            break;
        }
        kinds[n++] = k;
    }
    cursor = rd8(work, SM_CURSOR);
    hook = bw_mod_title_settings_on != NULL && bw_mod_title_settings_on();
    /* kind 7 is the port's SETTINGS row while the game's own use of it is off */
    settings_row = hook && MISC_GetSlotState != NULL && rdp(work, SM_UNLOCK) != NULL &&
                   !MISC_GetSlotState(rdp(work, SM_UNLOCK), 3);
    for (i = 0; i < n; i++) {
        if (kinds[i] == 0) {
            card = i;
        }
    }
    nrows = n - (card >= 0 ? 1 : 0);
    if (card >= 0) {
        info = info_get((void *)work, in, state);
    }
    slots = slot_count();
    memset(&tl, 0, sizeof tl);
    tl.w = W;
    tl.h = H - ui_bar_h(c);
    tl.n_rows = nrows;
    tl.has_card = card >= 0;
    if (theme->title_menu_layout != NULL && W * 10 >= H * 15) {
        theme->title_menu_layout(&tl, 0);
        stacked = tl.stacked;
        across = stacked ? 0 : tl.across;   /* the rows side by side: this many a row */
    }

    if (across) {
        /* the rows side by side under the card: left and right move among
         * them (on the card they choose the save, the game's own keys), up
         * and down go between the card and the rows */
        static int last_row = -1;
        u32 k = state == SM_STATE_SELECT ? eidos_ui_keys_pressed() : 0;
        int to = cursor;

        eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
        if (cursor != card) {
            last_row = cursor;
        }
        if (k & (BW_KEY_UP | BW_KEY_DOWN)) {
            /* a row's place among the rows, the card apart */
            int j = cursor - (card >= 0 && cursor > card ? 1 : 0);

            if (card >= 0 && cursor == card && (k & BW_KEY_DOWN)) {
                to = last_row >= 0 && last_row < n && last_row != card ? last_row : (card == 0 ? 1 : 0);
            } else if (cursor != card && (k & BW_KEY_UP)) {
                if (j >= across) {
                    j -= across;                /* the row of chips above */
                    to = j + (card >= 0 && j >= card ? 1 : 0);
                } else if (card >= 0) {
                    to = card;
                }
            } else if (cursor != card && (k & BW_KEY_DOWN) && j + across < nrows) {
                j += across;
                to = j + (card >= 0 && j >= card ? 1 : 0);
            }
        } else if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
            int d = (k & BW_KEY_LEFT) ? -1 : 1;

            if (cursor == card) {
                if (slots > 1) {
                    eidos_ui_press(d < 0 ? BW_KEY_LEFT : BW_KEY_RIGHT, 2);
                }
            } else {
                to = cursor + d;
                if (to == card) {
                    to += d;
                }
            }
        }
        if (to >= 0 && to < n && to != cursor) {
            *((u8 *)work + SM_CURSOR) = (u8)to;
            cursor = to;
            eidos_ui_sound(0x548);  /* the menu's cursor sound */
        }
    } else
    /* Two columns, moved like two columns: left and right go between the
     * card and the rows, up and down move inside a column; on the card up
     * and down choose the save (the game's left and right). */
    if (card >= 0 && W * 10 >= H * 15 && !stacked) {
        static int last_row = -1;
        /* the arrows are the interface's in every state of the menu (a key
         * that reached the game while it scrolls would cross columns); they
         * act only while it waits for a choice */
        u32 k = state == SM_STATE_SELECT ? eidos_ui_keys_pressed() : 0;

        eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
        if (cursor != card) {
            last_row = cursor;
        }
        if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
            int to = card;

            if (cursor == card) {
                to = last_row >= 0 && last_row < n && last_row != card ? last_row
                                                                         : (card == 0 ? 1 : 0);
            }
            if (to >= 0 && to < n && to != cursor) {
                *((u8 *)work + SM_CURSOR) = (u8)to;
                cursor = to;
                eidos_ui_sound(0x548);  /* the menu's cursor sound */
            }
        } else if (k & (BW_KEY_UP | BW_KEY_DOWN)) {
            int up = (k & BW_KEY_UP) != 0;

            if (cursor == card) {
                if (slots > 1) {
                    eidos_ui_press(up ? BW_KEY_LEFT : BW_KEY_RIGHT, 2);
                }
            } else {
                /* the cursor is written here, as for left and right: a press
                 * handed to the game would be read a frame later, and a
                 * second key in that frame would step from a stale row
                 * (and wrap onto the card) */
                int to = cursor + (up ? -1 : 1);

                if (to != card && to >= 0 && to < n) {
                    *((u8 *)work + SM_CURSOR) = (u8)to;
                    cursor = to;
                    eidos_ui_sound(0x548);
                }
            }
        }
    }
    if (bw_slots_label != NULL) {
        bw_slots_label(label, sizeof label);
    }

    /* the layout: two columns on a wide canvas (the logo and the card, the
     * rows), one column otherwise */
    avail = H - ui_bar_h(c) - 12;
    wide = W * 10 >= H * 15 && n > 0 && !stacked;
    logo_load();
    if (g_logo_have == 1) {
        int x0, y0, x1, y1;

        if (logo_bounds(&x0, &y0, &x1, &y1)) {
            logo_w = clampi(W * 26 / 100, 90, 170);
            logo_h = (y1 - y0) * logo_w / (x1 - x0);
        }
    }
    tl.logo_w = logo_w;
    tl.logo_h = logo_h;
    if (wide) {
        int total_w = clampi(W * 84 / 100, 380, 600);
        int block_h, gap = 8;

        left_w = total_w * 50 / 100;
        right_w = total_w * 46 / 100;
        left_x = (W - total_w) / 2;
        right_x = left_x + total_w - right_w;
        if (card >= 0 && logo_h > 0 && avail - logo_h - gap >= 128) {
            card_h = clampi(avail - logo_h - gap, 128, 150);
        } else {
            logo_h = 0;
            card_h = card >= 0 ? clampi(avail - 8, 112, 150) : 0;
        }
        block_h = logo_h > 0 ? logo_h + gap + card_h : card_h;
        if (card < 0) {
            /* no save: the rows alone, in the middle, under the logo */
            int tot = nrows * 26 + (nrows - 1) * 5;

            right_w = clampi(W * 42 / 100, 190, 280);
            right_x = (W - right_w) / 2;
            left_x = right_x;
            left_w = right_w;
            logo_h = logo_h > 0 && avail - logo_h - gap >= tot ? logo_h : 0;
            card_h = tot;
            block_h = logo_h > 0 ? logo_h + gap + card_h : card_h;
        }
        top = 6 + (avail - block_h) / 2;
        logo_y = top;
        y = top + (logo_h > 0 ? logo_h + gap : 0);        /* the card's top */
        rows_h = card_h;
        row_h = nrows > 0 ? clampi((rows_h - (nrows - 1) * 5) / nrows, 20, 34) : 26;
        logo_x = left_x + (left_w - logo_w) / 2;
        rows_y = y + (rows_h - (nrows * row_h + (nrows - 1) * 5)) / 2;
    } else {
        int tot, gap = 4;

        left_w = right_w = clampi(W * 60 / 100, 250, 340);
        left_x = right_x = (W - left_w) / 2;
        row_h = 26;
        card_h = card >= 0 ? 92 : 0;
        for (;;) {
            tot = (card >= 0 ? card_h + gap : 0) + nrows * (row_h + 3);
            if (tot <= avail || (row_h <= 20 && card_h <= 74)) {
                break;
            }
            if (row_h > 20) {
                row_h -= 2;
            } else {
                card_h -= 4;
            }
        }
        top = 6 + (avail - tot) / 2;
        if (logo_h > 0 && top - 4 >= logo_h) {
            logo_x = (W - logo_w) / 2;
            logo_y = top - logo_h - 4;
        } else {
            logo_h = 0;
        }
        y = top;
        rows_y = y + (card >= 0 ? card_h + gap : 0);
        rows_h = 0;
    }
    (void)rows_h;

    /* the places, then the theme's own */
    {
        int ry = rows_y, j = 0;

        if (logo_h > 0) {
            tl.logo.x = logo_x;
            tl.logo.y = logo_y;
            tl.logo.w = logo_w;
            tl.logo.h = logo_h;
        }
        tl.card.x = left_x;
        tl.card.y = y;
        tl.card.w = left_w;
        tl.card.h = card_h;
        for (i = 0; i < n; i++) {
            if (kinds[i] == 0) {
                continue;
            }
            tl.row[j].x = right_x;
            tl.row[j].y = ry;
            tl.row[j].w = right_w;
            tl.row[j].h = row_h;
            j++;
            ry += row_h + (wide ? 5 : 3);
        }
        {
            int jj = 0;

            for (i = 0; i < n && jj < 8; i++) {
                if (kinds[i] != 0) {
                    tl.label[jj++] = kind_label(kinds[i], hook, settings_row);
                }
            }
        }
        if (theme->title_menu_layout != NULL && W * 10 >= H * 15) {
            theme->title_menu_layout(&tl, 1);
        }
        if (theme->title_menu_backdrop != NULL) {
            theme->title_menu_backdrop(c, &tl);
        }
        if (tl.logo.w > 0) {
            logo_draw(c, tl.logo.x, tl.logo.y, tl.logo.w, 255);
        }
    }

    /* the card */
    if (card >= 0) {
        EidosUiRect r;
        int st, act;

        r = tl.card;
        st = eidos_ui_hit(r);
        hover_card = (st & EIDOS_UI_HOVER) != 0;
        if (st & EIDOS_UI_CLICK) {
            request(card, ACT_A);
        }
        g_card_lr = stacked || across || !wide;
        act = card_draw(c, tl.card.x, tl.card.y, tl.card.w, tl.card.h, info, card == cursor, hover_card,
                        label, slots, in);
        if (act >= 0) {
            request(card, act);
        }
    }

    /* the other rows */
    {
        int j = 0;

        for (i = 0; i < n; i++) {
            EidosUiRect r;
            int st;

            if (kinds[i] == 0) {
                continue;
            }
            r = tl.row[j++];
            st = eidos_ui_hit(r);
            if (st & EIDOS_UI_CLICK) {
                request(i, ACT_A);
            }
            if (theme->title_menu_row != NULL) {
                theme->title_menu_row(c, r.x, r.y, r.w, r.h, kind_label(kinds[i], hook, settings_row),
                                      i == cursor && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) != 0);
            } else {
                row_draw(c, r.x, r.y, r.w, r.h, kind_label(kinds[i], hook, settings_row),
                         i == cursor && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) != 0);
            }
        }
    }

    /* the wheel moves the game's cursor; a right click is B */
    if (state == SM_STATE_SELECT && g_target < 0) {
        int wh = eidos_ui_wheel(all);

        if (wh != 0) {
            int t = clampi(cursor - (wh > 0 ? 1 : -1), 0, n - 1);

            if (t != cursor) {
                request(t, -1);
            }
        }
        if (eidos_ui_right_click()) {
            eidos_ui_press(BW_KEY_B, 2);
        }
    }

    /* carry a click out: one press per game step, only while the menu waits
     * for input */
    if (g_wait > 0) {
        g_wait--;
    }
    if (g_target >= 0) {
        g_age++;
        if (g_age > 240 || g_target >= n) {
            g_target = -1;
        } else if (state == SM_STATE_SELECT && g_wait == 0) {
            if (cursor != g_target) {
                /* the cursor goes straight to the row, as the keys above do:
                 * a press per row would play the cursor sound for each row
                 * passed */
                *((u8 *)work + SM_CURSOR) = (u8)g_target;
                if (g_act < 0) {
                    eidos_ui_sound(0x548);  /* the wheel's one step */
                }
                g_wait = 2;
            } else {
                if (g_act == ACT_A) {
                    eidos_ui_press(BW_KEY_A, 2);
                } else if (g_act == ACT_LEFT) {
                    eidos_ui_press(BW_KEY_LEFT, 1);
                } else if (g_act == ACT_RIGHT) {
                    eidos_ui_press(BW_KEY_RIGHT, 1);
                }
                g_target = -1;
                g_wait = 10;
            }
        }
    }

    {
        /* the flag of the interface's style, and its key in the bar */
        const char *const *base = slots > 1 && card >= 0 ? (stacked || across || !wide ? hints_slots_lr : hints_slots)
                                                          : hints_one;
        const u32 *bk = slots > 1 && card >= 0 ? keys_slots : keys_one;
        int bn = slots > 1 && card >= 0 ? 3 : 2, j;
        const char *hp[12];
        u32 hk[6];
        EidosUiRect fr;
        int hn;

        fr.w = STYLE_FLAG_W;
        fr.h = STYLE_FLAG_H;
        fr.x = W - fr.w - 10 - c->safe_r;
        fr.y = 0;
        j = eidos_ui_hit(fr);
        (void)style_flag(c, W, (j & EIDOS_UI_HOVER) != 0);
        if ((j & EIDOS_UI_CLICK) && state == SM_STATE_SELECT && g_style_cool == 0 && eidos_ui_try_state() == 0) {
            style_show();
            g_style_before = style_now();
            eidos_ui_sound(0x54C);
            eidos_ui_crossfade();
        }
        for (j = 0; j < bn; j++) {
            hp[2 * j] = base[2 * j];
            hp[2 * j + 1] = base[2 * j + 1];
            hk[j] = bk[j];
        }
        hp[2 * bn] = "SELECT";
        hp[2 * bn + 1] = "Style";
        hk[bn] = BW_KEY_SELECT;
        hn = bn + 1;
        if (ver_on()) {
            /* the other version's flag, between the two, and its key */
            EidosUiRect vr = ver_flag_box(c, W, H);

            j = eidos_ui_hit(vr);
            (void)ver_flag(c, W, H, (j & EIDOS_UI_HOVER) != 0);
            if ((j & EIDOS_UI_CLICK) && state == SM_STATE_SELECT && g_ver_cool == 0 && eidos_ui_try_state() == 0) {
                ver_press();
            }
            hp[2 * hn] = "X";
            hp[2 * hn + 1] = "Version";
            hk[hn] = BW_KEY_X;
            hn++;
        }
        if (mods_on()) {
            /* the mod manager's flag, at the other corner, and its key */
            u32 mi[MODS_MAX];
            int on = 0;
            EidosUiRect mr;

            (void)mods_listed(mi, &on);
            mr.w = STYLE_FLAG_W;
            mr.h = STYLE_FLAG_H;
            mr.x = 24 + c->safe_l;
            mr.y = 0;
            j = eidos_ui_hit(mr);
            (void)mods_flag(c, (j & EIDOS_UI_HOVER) != 0, on);
            if ((j & EIDOS_UI_CLICK) && state == SM_STATE_SELECT && g_mods_cool == 0 && eidos_ui_try_state() == 0) {
                mods_show();
            }
            hp[2 * hn] = "Y";
            hp[2 * hn + 1] = "Mods";
            hk[hn] = BW_KEY_Y;
            hn++;
        }
        ui_hints(c, H - ui_bar_h(c), W, hp, hn, hk);
    }
    dbg = getenv("EIDOS_TITLE_DEBUG");
    if (dbg != NULL && (in->frame % 30) == 0) {
        fprintf(stderr, "menu: frame %u state %d cursor %d n %d card %d target %d\n", in->frame,
                state, cursor, n, card, g_target);
    }
}

static const EidosUiRecipe recipe_menu = {
    "title_menu", menu_match, menu_run, menu_reset, 0
};

/* ------------------------------------------------------------------ */
/* Boot: the company logos, the copyright, the opening, the intro      */
/* ------------------------------------------------------------------ */

/*
 * Everything else the boot stack runs before the game (the logos and the
 * copyright in ov088, the opening cinematic in ov179, the new-game intro and
 * the cutscene after it):
 * one screen shown big in the middle of the window, the one that holds
 * the picture, rather than the two DS screens side by side.  No interface.
 */
static u8 *intro_cmd_work(void);

static int boot_match(void)
{
    TtProc *p = root_proc();

    /* the professor's words at the game's own size */
    if (intro_cmd_work() != NULL) {
        eidos_ui_want_ds_scale();
    }

    if (p == NULL || scene(NULL) != TT_SCENE_NONE) {
        return 0;
    }
    if (p->overlayId == 0xFFFFFFFFu) {
        /* The cutscene after the professor's intro (the Unova flyover, the
         * season card, the walk to the house) runs as a main-program proc
         * before any field map exists, so no field recipe takes it.  That
         * proc is the game's main one for the whole game, so the cutscene
         * is told by what is not there yet: no field map (GameSystem
         * fieldMap +0x20) and no battle (overlay 94).  Otherwise every
         * gap between two of the game's screens would come here. */
        void *const *c = &bw_dsref_ov088_d_0209DF9C != NULL ? &bw_dsref_ov088_d_0209DF9C : NULL;
        void *gs = (&bw_dsref__GameSystemGlobal != NULL && bw_dsref__GameSystemGlobal != NULL)
                   ? *(void **)bw_dsref__GameSystemGlobal : NULL;

        static int field_seen;

        if (gs != NULL && *(u32 *)((u8 *)gs + 0x20) != 0u) {
            field_seen = 1;
        }
        /* the cutscene comes once, before the first field of a run: once a
         * field has been up, an app that closed it (the Bag, the trainer
         * card) is not the cutscene */
        if (field_seen) {
            return 0;
        }
        if (bw_overlay_is_loaded != NULL && bw_overlay_is_loaded(94)) {
            return 0;
        }
        return addr_is(p->data, c, 0x0209DF9Cu);
    }
    return p->overlayId == 88 || p->overlayId == 179;
}

/* How much picture a DS screen holds: the sum of its brightness on a
 * coarse grid. */
static u32 screen_ink(const u8 *s)
{
    u32 sum = 0;
    int x, y;

    if (s == NULL) {
        return 0;
    }
    for (y = 4; y < 192; y += 8) {
        for (x = 4; x < 256; x += 8) {
            const u8 *p = s + ((size_t)y * 256 + (size_t)x) * 4;

            sum += (u32)p[0] + p[1] + p[2];
        }
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/* The professor's intro: its choices                                  */
/* ------------------------------------------------------------------ */

/*
 * The intro (overlay 204) runs on the new-game proc's own ProcSys
 * (NewGameIntroWork procSys 0x30, ov088/startmenu_types.h); its proc's
 * work is the IntroGraphicWork (cmdWork 0x14).  IntroCmdWork
 * (ov204/intro_types.h): msg 0x18, commands 0x20 (8 IntroCommand
 * pointers, tag at 0x00), slots 0x40 (12 bytes each).  IntroMsgWork
 * (ov204/intro_msg.c): the menu's BmpMenuList 0x24, its window 0x38.
 */
extern int ov204_INTRO_CMD_CmdAnimMode(char *p, u32 *q) TT_WEAK;
extern void *bw_dsref_ov204_gIntroCmdCommandTable TT_WEAK;
typedef void (*TtDataFn)(void);
extern TtDataFn bw_ovl_code_entry(u32 value) TT_WEAK;
extern void ov088_STARTMENU_NewGameIntroMain(void) TT_WEAK;
extern void ov204_INTRO_GRAPHIC_Main(void) TT_WEAK;

/* A code word of the game's data is `fn`: the host function itself (a
 * pointer the loader relocated) or a DS address that resolves to it. */
static int code_is(u32 word, const void *fn)
{
    if (fn == NULL || word == 0u) {
        return 0;
    }
    if ((uintptr_t)word == (uintptr_t)fn) {
        return 1;
    }
    return bw_ovl_code_entry != NULL && (const void *)bw_ovl_code_entry(word) == fn;
}

/* The proc runs `main` (its ProcData: init, main, end). */
static int proc_runs(const TtProc *p, void (*main)(void))
{
    return p != NULL && p->data != NULL && code_is(((const u32 *)p->data)[1], (const void *)main);
}

static u8 *intro_cmd_work(void)
{
    TtProc *p = root_proc(), *child;
    u8 *ngw, *sys, *gw;

    if (p == NULL || p->overlayId != 88 || p->work == NULL
        || !proc_runs(p, ov088_STARTMENU_NewGameIntroMain)) {
        return NULL;
    }
    ngw = (u8 *)p->work;
    sys = *(u8 **)(ngw + 0x30);
    child = sys != NULL ? *(TtProc **)(sys + 0x08) : NULL;
    if (child == NULL || child->work == NULL || !proc_runs(child, ov204_INTRO_GRAPHIC_Main)) {
        return NULL;
    }
    gw = (u8 *)child->work;
    return *(u8 **)(gw + 0x14);
}

/* The gender choice's state words (INTRO_CMD_CmdAnimMode: step, choice 0
 * boy 1 girl), or NULL when it is not running. */
static u32 *intro_gender(u8 *cmd)
{
    u32 *table;
    int i;

    if (cmd == NULL || ov204_INTRO_CMD_CmdAnimMode == NULL
        || &bw_dsref_ov204_gIntroCmdCommandTable == NULL
        || bw_dsref_ov204_gIntroCmdCommandTable == NULL) {
        return NULL;
    }
    table = (u32 *)bw_dsref_ov204_gIntroCmdCommandTable;
    for (i = 0; i < 8; i++) {
        const u32 *q = *(const u32 **)(cmd + 0x20 + 4 * i);

        if (q != NULL && q[0] < 0x40u
            && code_is(table[q[0]], (const void *)ov204_INTRO_CMD_CmdAnimMode)) {
            return (u32 *)(cmd + 0x40 + 12 * i);
        }
    }
    return NULL;
}

/*
 * The intro's choices by touch.  Returns 1 when a choice is up (the tap is
 * not A then).  The professor's yes/no and name lists are the game's own
 * BmpMenuList: the shared choice list (engine/ui/choice_list.h).  The boy
 * and the girl stand on the left and the right of the screen: a tap on the
 * one not chosen moves the game's choice there (LEFT or RIGHT, so the game
 * turns them as for the keys), a tap on the chosen one is A.  A tap
 * anywhere else does nothing.
 */
extern void g2d_hw_hide_main_bgs(u32 mask) TT_WEAK;

/*
 * The professor's words as the interface's narration box (engine/ui/
 * textbox.c): the intro's message window (IntroMsgWork textWin 0x1C) at
 * its place, the game's own window and its menu window left out of the
 * screen.  Returns 1 while the intro runs.
 */
static int intro_text(EidosUiCtx *c)
{
    u8 *cmd = intro_cmd_work(), *msg;
    const u8 *tw, *mw;
    float sc = (float)c->canvas->h / 192.0f;
    float ox = ((float)c->canvas->w - 256.0f * sc) * 0.5f;
    u32 hide = 0;

    if (cmd == NULL || (msg = *(u8 **)(cmd + 0x18)) == NULL) {
        return 0;
    }
    tw = *(const u8 **)(msg + 0x1C);
    mw = *(const u8 **)(msg + 0x38);
    if (tw != NULL && (*(const u16 *)tw & 0x7FFFu) == 0x7FB2u) {
        int gw = (int)((float)((tw[5] + 2) * 8) * sc);
        int x = (int)(ox + (float)((tw[3] - 1) * 8) * sc);
        int y = (int)((float)((tw[4] - 1) * 8) * sc);

        if (y + tw[6] * 8 + 10 > c->canvas->h - 4) {
            y = c->canvas->h - 4 - (tw[6] * 8 + 10);
        }
        ui_textbox_draw(c, UI_TEXTBOX_NARRATION, x, y, gw, tw, -1, -1);
        if (tw[2] < 4) hide |= 1u << tw[2];
    }
    if (mw != NULL && (*(const u16 *)mw & 0x7FFFu) == 0x7FB2u && mw[2] < 4) {
        hide |= 1u << mw[2];
    }
    if (g2d_hw_hide_main_bgs != NULL) {
        g2d_hw_hide_main_bgs(hide);
    }
    return 1;
}

static int intro_choices(EidosUiCtx *c, const EidosUiFrame *in)
{
    u8 *cmd = intro_cmd_work(), *msg;
    u32 *g;
    float sc = (float)c->canvas->h / 192.0f;
    float ox = ((float)c->canvas->w - 256.0f * sc) * 0.5f;

    if (cmd == NULL) {
        ui_choice_list_idle();
        return 0;
    }
    msg = *(u8 **)(cmd + 0x18);
    if (msg != NULL && *(void **)(msg + 0x24) != NULL && *(void **)(msg + 0x38) != NULL) {
        if (ui_choice_list(c, in, *(void **)(msg + 0x24), *(void **)(msg + 0x38), ox, 0.0f, sc, UI_CHOICE_WHITE)) {
            if (eidos_ui_right_click()) {
                eidos_ui_press(BW_KEY_B, 2);
            }
            return 1;
        }
    }
    ui_choice_list_idle();
    g = intro_gender(cmd);
    if (g != NULL && (g[0] == 1u || g[0] == 4u)) {
        int side;
        EidosUiRect r;

        for (side = 0; side < 2; side++) {
            r.x = (int)(ox + (float)(side * 128) * sc);
            r.y = 0;
            r.w = (int)(128.0f * sc);
            r.h = (int)(144.0f * sc);       /* above the message box */
            if ((eidos_ui_hit(r) & EIDOS_UI_CLICK) && g[0] == 1u) {
                if ((u32)side == g[1]) {
                    eidos_ui_press(BW_KEY_A, 2);
                } else {
                    eidos_ui_press(side == 0 ? BW_KEY_LEFT : BW_KEY_RIGHT, 2);
                }
            }
        }
        return 1;
    }
    return 0;
}

static void boot_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static int shown = -1;
    u32 top = screen_ink(in->screen_rgba[0]), bottom = screen_ink(in->screen_rgba[1]);

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    /* The title program's scenes, the new-game intro among them, are drawn
     * for the DS's frame (banners that run off its edges, sprites placed
     * for its shape): no widened 3D, nothing outside the DS rectangle.  The
     * opening cinematic and the flyover after the intro are 3D scenes and
     * fill the window. */
    {
        TtProc *p = root_proc();

        view->ds_only = p != NULL && p->overlayId == 88;
    }
    /* the screen with the picture; a clear lead is needed to switch, so a
     * fade does not make the view jump between the two */
    if (shown < 0 || (shown == 0 && bottom > top * 2 + 2000) ||
        (shown == 1 && top > bottom * 2 + 2000)) {
        shown = bottom > top ? 1 : 0;
    }
    if (view->world_screen != shown && in->screen_rgba[shown] != NULL) {
        view->world_screen = shown;
    }
    /* Touch and A.  Before the title (the logos, the copyright) and in the
     * opening cinematic, A and a tap skip as START does.  After it (the
     * professor's intro, the cutscene after it), a tap is A: the game's
     * text waits for A, and a touch screen has no A to press. */
    {
        TtProc *p = root_proc();
        int skip = !g_title_passed || (p != NULL && p->overlayId == 179);
        EidosUiRect all = { 0, 0, c->canvas->w, c->canvas->h };

        if (!skip) {
            (void)intro_text(c);
        }
        if (!skip && intro_choices(c, in)) {
            /* a choice is up: the taps go to it (intro_choices) */
        } else if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
            eidos_ui_press(skip ? BW_KEY_START : BW_KEY_A, 2);
        } else if (skip && (eidos_ui_keys_pressed() & BW_KEY_A) != 0u) {
            eidos_ui_press(BW_KEY_START, 2);
        }
    }
}

static const EidosUiRecipe recipe_boot = { "boot", boot_match, boot_run, NULL, 0 };

const EidosUiRecipe *const ui_recipes_title[] = { &recipe_title, &recipe_menu };
const u32 ui_recipes_title_count = 2;

/* the boot stack's other screens: a group of its own, the last one, so the
 * new-game intro's name entry (the input group) comes first */
const EidosUiRecipe *const ui_recipes_boot[] = { &recipe_boot };
const u32 ui_recipes_boot_count = 1;
