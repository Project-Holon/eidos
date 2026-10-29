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
 *                 recipe fills the window with the 3D, copies the logo from
 *                 the other screen (its background keyed out), and draws
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
#include "input.h"

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
extern int  bw_slots_used(int n) TT_WEAK;
extern int  bw_slots_replacing(void) TT_WEAK;

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

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The logo, cut out of the DS screen that holds it and kept for the menu. */
#define LOGO_SX 8
#define LOGO_SY 16
#define LOGO_W  240
#define LOGO_H  142

static u8 g_logo_px[LOGO_W * LOGO_H * 4];
static UiImage g_logo = { 0, 0, g_logo_px };
static int g_logo_have;
static int g_logo_yellow;     /* the brightest capture so far, in yellow pixels */

/* Take the logo from the screen: a pixel is the backdrop when it is dark and
 * connected to the rectangle's edge through dark pixels (the logo's own dark
 * parts are inside its bright outlines). */
/* A pixel of the logo's box that belongs to the dark backdrop. */
static int logo_dark(const u8 *screen, int i)
{
    const u8 *p = screen + ((size_t)(LOGO_SY + i / LOGO_W) * 256 + (size_t)(LOGO_SX + i % LOGO_W)) * 4;
    int mx = p[0] > p[1] ? p[0] : p[1];

    return (p[2] > mx ? p[2] : mx) <= 70;
}

/* Queue a backdrop pixel of the fill once: marked when it is queued. */
static void logo_push(const u8 *screen, u8 *mask, int *stack, int *sp, int i)
{
    if (!mask[i] && logo_dark(screen, i)) {
        mask[i] = 1;
        stack[(*sp)++] = i;
    }
}

static void logo_capture(const u8 *screen)
{
    static u8 mask[LOGO_W * LOGO_H];
    static int stack[LOGO_W * LOGO_H];
    int x, y, sp = 0, n = 0, yellow = 0;

    if (screen == NULL) {
        return;
    }
    /* a fill from the border over the dark backdrop; a pixel is marked when
     * it is queued, so each is queued once and the stack never overflows */
    memset(mask, 0, sizeof mask);
    for (y = 0; y < LOGO_H; y++) {
        for (x = 0; x < LOGO_W; x++) {
            if (x == 0 || y == 0 || x == LOGO_W - 1 || y == LOGO_H - 1) {
                logo_push(screen, mask, stack, &sp, y * LOGO_W + x);
            }
        }
    }
    while (sp > 0) {
        int i = stack[--sp], px = i % LOGO_W, py = i / LOGO_W;

        if (px > 0) logo_push(screen, mask, stack, &sp, i - 1);
        if (px < LOGO_W - 1) logo_push(screen, mask, stack, &sp, i + 1);
        if (py > 0) logo_push(screen, mask, stack, &sp, i - LOGO_W);
        if (py < LOGO_H - 1) logo_push(screen, mask, stack, &sp, i + LOGO_W);
    }
    for (y = 0; y < LOGO_H * LOGO_W; y++) {
        const u8 *p = screen + ((size_t)(LOGO_SY + y / LOGO_W) * 256 +
                                (size_t)(LOGO_SX + y % LOGO_W)) * 4;

        n += mask[y] ? 0 : 1;
        yellow += (!mask[y] && p[0] > 200 && p[1] > 150 && p[2] < 90) ? 1 : 0;
    }
    /* the logo is a few thousand pixels and its yellow is bright: a fade or a
     * blank screen is neither; keep the last good copy */
    if (n <= 4000 || yellow < 700 || yellow * 100 < g_logo_yellow * 95) {
        return;
    }
    g_logo_yellow = yellow > g_logo_yellow ? yellow : g_logo_yellow;
    for (y = 0; y < LOGO_H; y++) {
        for (x = 0; x < LOGO_W; x++) {
            const u8 *p = screen + ((size_t)(LOGO_SY + y) * 256 + (size_t)(LOGO_SX + x)) * 4;
            u8 *q = g_logo_px + ((size_t)y * LOGO_W + (size_t)x) * 4;

            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = mask[y * LOGO_W + x] ? 0 : 255;
        }
    }
    g_logo.w = LOGO_W;
    g_logo.h = LOGO_H;
    g_logo_have = 1;
}

/* How bright the screen's logo is now against the stored copy, 0 to 255:
 * the game fades the screens in and out. */
static int logo_fade(const u8 *screen)
{
    long cur = 0, ref = 0;
    int x, y;

    if (screen == NULL || !g_logo_have) {
        return 255;
    }
    for (y = 0; y < LOGO_H; y += 2) {
        for (x = 0; x < LOGO_W; x += 2) {
            const u8 *q = g_logo_px + ((size_t)y * LOGO_W + (size_t)x) * 4;
            const u8 *p = screen + ((size_t)(LOGO_SY + y) * 256 + (size_t)(LOGO_SX + x)) * 4;

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

    if (!g_logo_have) {
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
static int g_ps_last;           /* frame + 1 when the game's PRESS START last showed */
static int g_ps_ref = -1;       /* the frame it last came on */
static int g_ps_prev;
static int g_started;           /* START was pressed: the prompt holds still */
static u32 g_started_at;

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
    view->zoom = 1.0f;
    view->blur = 0;
    view->hide_2d = 1;
    g_title_seen++;

    /* the logo, from the screen that is not the 3D one */
    logo_capture(other);
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
            int px = (W - pw) / 2;

            hh = 17;
            y = H - hh - ph - clampi(H / 10, 8, 30);
            ui_panel(c, px, y, pw, ph, CUT_DEF, 5, UI_BODY, UI_EDGE2, 215);
            {
                /* the bar's top follows the cut corner, a pixel inside it
                 * (the panel's inside starts where column + row >= 8) */
                int bx;

                for (bx = 3; bx < 5; bx++) {
                    int top = 9 - bx > 3 ? 9 - bx : 3;

                    ui_fill(c, px + bx, y + top, 1, ph - 3 - top, UI_CYAN, 255);
                }
            }
            ui_text(c, px + (pw - tw) / 2, y + ui_text_y(ph, UI_FONT_LARGE), s,
                    UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
        }
    }
    /* the credit, bottom left */
    ui_text(c, 6, H - 17 - 11, "Developed by GAME FREAK inc.", UI_LIGHT, 0x202020u,
            UI_FONT_SMALL, 1);

    /* click anywhere is START */
    all.x = 0;
    all.y = 0;
    all.w = W;
    all.h = H - 17;
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_START, 2);
        g_started = 1;
        g_started_at = in->frame;
    }
    ui_fill(c, 0, H - 17, W, 17, UI_BODY2, 255);       /* the 3D carries its own credit */
    ui_hints(c, H - 17, W, hints, 1, keys);
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
    int n, used = 0;

    if (bw_slots_used == NULL) {
        return 1;
    }
    for (n = 1; n <= 8; n++) {
        used += bw_slots_used(n) ? 1 : 0;
    }
    return used;
}

/* What a click asked of the menu, carried out one press at a time. */
#define ACT_A     0
#define ACT_LEFT  1
#define ACT_RIGHT 2
static int g_target = -1, g_act, g_wait, g_age;

static void menu_reset(void)
{
    g_target = -1;
    g_wait = 0;
    g_age = 0;
    g_info.ok = 0;
    g_info_work = NULL;
}

static int menu_match(void)
{
    if (scene(NULL) != TT_SCENE_MENU) {
        return 0;
    }
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

/* A dark floor of two greys that drifts toward the viewer, a cool light
 * behind the middle and a dark rim (the title's own colours). */
static void backdrop(EidosUiCtx *c, int W, int H, int frame)
{
    int x, y, hy = H * 30 / 100;
    float scroll = (float)frame * 0.010f;

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
            sel ? 0x00506Au : UI_GREY, font, 1);
    if (selected) {
        for (i = 0; i < 5; i++) {
            ui_fill(c, x + w - 15 + i, y + h / 2 - 5 + i, 1, 11 - 2 * i, UI_WHITE, 255);
        }
    }
}

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
        tri_up(c, sx + (sw - 11) / 2, ur.y + (ur.h - 6) / 2, 11,
               eidos_ui_hit(ur) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT);
        tri_down(c, sx + (sw - 11) / 2, dr.y + (dr.h - 6) / 2, 11,
                 eidos_ui_hit(dr) & EIDOS_UI_HOVER ? UI_WHITE : CARD_LIGHT);
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
    fig = ui_overworld(info->sex == 1 ? SPRITE_HEROINE : SPRITE_HERO, 0,
                       hov ? ((int)(in->frame / 7) & 3) : -1);
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
            int cw = gw / 2;

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
    static const char *const hints_ask[] = { "A", "Confirm", "B", "Cancel" };
    static const u32 keys_one[] = { BW_KEY_A, BW_KEY_B };
    static const u32 keys_slots[] = { BW_KEY_A, BW_KEY_RIGHT, BW_KEY_B };
    EidosUiRect all;
    char *dbg;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
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
    all.h = H - 17;

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
        int dsw = H * 4 / 3, x0 = (W - dsw) / 2;

        view->hide_2d = 0;
        if (x0 > 0) {
            /* beside the game's window, the menu's own floor */
            EidosUiRect keep;

            ui_push(c, 0, 0, &keep);
            ui_clip(c, 0, 0, x0, H);
            backdrop(c, W, H, (int)in->frame);
            ui_pop(c, 0, 0, &keep);
            ui_push(c, 0, 0, &keep);
            ui_clip(c, W - x0, 0, x0, H);
            backdrop(c, W, H, (int)in->frame);
            ui_pop(c, 0, 0, &keep);
        }
        ui_hints(c, H - 17, W, hints_ask, 2, keys_one);
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

    /* Two columns, moved like two columns: left and right go between the
     * card and the rows, up and down move inside a column; on the card up
     * and down choose the save (the game's left and right). */
    if (card >= 0 && W * 10 >= H * 15) {
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
    avail = H - 17 - 12;
    wide = W * 10 >= H * 15 && n > 0;
    if (g_logo_have) {
        int x0, y0, x1, y1;

        if (logo_bounds(&x0, &y0, &x1, &y1)) {
            logo_w = clampi(W * 26 / 100, 90, 170);
            logo_h = (y1 - y0) * logo_w / (x1 - x0);
        }
    }
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
        if (logo_h > 0) {
            logo_draw(c, left_x + (left_w - logo_w) / 2, logo_y, logo_w, 255);
        }
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
            logo_draw(c, (W - logo_w) / 2, top - logo_h - 4, logo_w, 255);
        }
        y = top;
        rows_y = y + (card >= 0 ? card_h + gap : 0);
        rows_h = 0;
    }
    (void)rows_h;

    /* the card */
    if (card >= 0) {
        EidosUiRect r;
        int st, act;

        r.x = left_x;
        r.y = y;
        r.w = left_w;
        r.h = card_h;
        st = eidos_ui_hit(r);
        hover_card = (st & EIDOS_UI_HOVER) != 0;
        if (st & EIDOS_UI_CLICK) {
            request(card, ACT_A);
        }
        act = card_draw(c, left_x, y, left_w, card_h, info, card == cursor, hover_card,
                        label, slots, in);
        if (act >= 0) {
            request(card, act);
        }
    }

    /* the other rows */
    {
        int ry = rows_y;

        for (i = 0; i < n; i++) {
            EidosUiRect r;
            int st;

            if (kinds[i] == 0) {
                continue;
            }
            r.x = right_x;
            r.y = ry;
            r.w = right_w;
            r.h = row_h;
            st = eidos_ui_hit(r);
            if (st & EIDOS_UI_CLICK) {
                request(i, ACT_A);
            }
            row_draw(c, right_x, ry, right_w, row_h, kind_label(kinds[i], hook, settings_row),
                     i == cursor && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) != 0);
            ry += row_h + (wide ? 5 : 3);
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
            if (cursor < g_target) {
                eidos_ui_press(BW_KEY_DOWN, 1);
                g_wait = 4;
            } else if (cursor > g_target) {
                eidos_ui_press(BW_KEY_UP, 1);
                g_wait = 4;
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

    ui_hints(c, H - 17, W, slots > 1 && card >= 0 ? hints_slots : hints_one,
             slots > 1 && card >= 0 ? 3 : 2, slots > 1 && card >= 0 ? keys_slots : keys_one);
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
static int boot_match(void)
{
    TtProc *p = root_proc();

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

static void boot_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static int shown = -1;
    u32 top = screen_ink(in->screen_rgba[0]), bottom = screen_ink(in->screen_rgba[1]);

    (void)c;
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
}

static const EidosUiRecipe recipe_boot = { "boot", boot_match, boot_run, NULL, 0 };

const EidosUiRecipe *const ui_recipes_title[] = { &recipe_title, &recipe_menu };
const u32 ui_recipes_title_count = 2;

/* the boot stack's other screens: a group of its own, the last one, so the
 * new-game intro's name entry (the input group) comes first */
const EidosUiRecipe *const ui_recipes_boot[] = { &recipe_boot };
const u32 ui_recipes_boot_count = 1;
