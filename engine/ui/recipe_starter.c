/*
 * engine/ui/recipe_starter.c -- the one-screen recipe for the starter
 * choice (overlay 223, psel: the gift box in the player's room).
 *
 * The DS splits the scene: the lower screen is the open case with the
 * three Poke Balls ("Choose a Pokemon."), then the close-up of the ball
 * picked with YES / NO; the upper screen is the stage, the three Pokemon as
 * silhouettes, the one picked lit by a spotlight with its caption ("The
 * Fire-type Pokemon Tepig").  Here the lower screen's 3D fills the window,
 * as the scene the player acts on, and the kit draws the rest: the message
 * in the middle, and in the close-up the Pokedex's
 * picture box at the left (the picked Pokemon's front sprite in its idle
 * loop, its name and its type) and YES / NO at the right.  Before the case
 * opens, the closed box under the game's own prompt; a click answers it.
 *
 * The game keeps all the logic (src/game/ov223/psel_graphic.c,
 * PSEL_GRAPHIC_Main).  What is shown is read from its work (PselWork,
 * src/game/ov223/psel_types.h); a click becomes the touch the game tests
 * for (the balls are circles of the hit table d_021BC75C, the YES / NO
 * menu sits at character column 23 to 31, rows 11 to 16).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "input.h"
#include "dsmem.h"

#if defined(__GNUC__) || defined(__clang__)
#define PS_WEAK __attribute__((weak))
#else
#define PS_WEAK
#endif

/* The recipes read the game's structs at the DS layout: 32-bit pointers. */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define PS_GAME 1
#else
#define PS_GAME 0
#endif

#define CUT_DEF         (UI_CUT_TL | UI_CUT_BR)
#define PSEL_OVERLAY    223

/* PselWork (src/game/ov223/psel_types.h) */
#define PW_SUBSTATE     0x0Cu           /* PSEL_GRAPHIC_Main's state         */
#define PW_SELECTED     0x228u          /* the ball picked, 4 for none       */
#define PW_STRBUF       0x258u          /* the lower screen's message        */
#define PW_TASKMENU     0x264u          /* the YES / NO menu                 */

extern int bw_overlay_is_loaded(u32 id) PS_WEAK;
extern void *bw_dsref__GameSystemGlobal PS_WEAK;
extern void *bw_dsref_ov223_d_021BC710 PS_WEAK;     /* the three species */
extern u8   APP_TASKMENU_GetCursorPos(const void *menu) PS_WEAK;
extern void STRBUF_ToChars(void *strbuf, u16 *out, u32 max) PS_WEAK;

static int g_last_sub = -1;            /* the game's state last frame      */

/* The balls on the lower screen: the circles of the game's hit table. */
static const int g_ball_x[3] = { 66, 128, 189 };
static const int g_ball_y[3] = { 96, 66, 96 };
#define BALL_R          32

static u32 rd32(const void *p, u32 off)
{
    return *(const u32 *)((const u8 *)p + off);
}

static void *rdp(const void *p, u32 off)
{
    return (void *)(uintptr_t)rd32(p, off);
}

static int debug_on(void)
{
    static int on = -1;

    if (on < 0) {
        on = getenv("EIDOS_STARTER_LOG") != NULL;
    }
    return on;
}

static int starter_match(void)
{
    return bw_overlay_is_loaded != NULL && bw_overlay_is_loaded(PSEL_OVERLAY);
}

/* The work of the starter choice: the proc on top of the game system's
 * stack (GameSystem procSys 0x08; ProcSys current 0x08; Proc overlayId
 * 0x04, seq 0x08, subSeq 0x0C, work 0x14), past its init.  *outer is
 * PSEL_Main's state: 9 while PSEL_GRAPHIC_Main runs the choice. */
static void *psel_work(int *outer)
{
    void *gs, *ps, *cur;

    if (!PS_GAME || &bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    ps = gs != NULL ? rdp(gs, 0x08) : NULL;
    cur = ps != NULL ? rdp(ps, 0x08) : NULL;
    if (cur == NULL || rd32(cur, 0x04) != PSEL_OVERLAY || rd32(cur, 0x08) < 2u) {
        return NULL;
    }
    *outer = (int)rd32(cur, 0x0C);
    return rdp(cur, 0x14);
}

static int species_of(int ball)
{
    static const int dflt[3] = { 495, 498, 501 };

    if (&bw_dsref_ov223_d_021BC710 != NULL && bw_dsref_ov223_d_021BC710 != NULL) {
        return ((const u16 *)bw_dsref_ov223_d_021BC710)[ball];
    }
    return dflt[ball];
}

static int put_utf8(char *o, int at, int cap, u32 u)
{
    if (u < 0x80u && at + 1 < cap) {
        o[at++] = (char)u;
    } else if (u < 0x800u && at + 2 < cap) {
        o[at++] = (char)(0xC0u | (u >> 6));
        o[at++] = (char)(0x80u | (u & 0x3Fu));
    } else if (u >= 0x800u && at + 3 < cap) {
        o[at++] = (char)(0xE0u | (u >> 12));
        o[at++] = (char)(0x80u | ((u >> 6) & 0x3Fu));
        o[at++] = (char)(0x80u | (u & 0x3Fu));
    }
    return at;
}

/* The game's message as one line of UTF-8 (its line breaks become spaces,
 * the kit wraps by itself). */
static void strbuf_text(void *sb, char *out, int cap)
{
    u16 u[128];
    int i, at = 0;

    out[0] = '\0';
    if (sb == NULL || STRBUF_ToChars == NULL) {
        return;
    }
    memset(u, 0xFF, sizeof u);
    STRBUF_ToChars(sb, u, 127);
    u[127] = 0xFFFFu;
    for (i = 0; i < 127 && u[i] != 0xFFFFu; i++) {
        u32 ch = u[i];

        if (ch == 0xFFFEu || ch == 0xE000u) {
            ch = ' ';
        } else if (ch >= 0xD800u && ch <= 0xF8FFu) {
            continue;
        }
        at = put_utf8(out, at, cap, ch);
    }
    out[at] = '\0';
}

/* A string of the game's own message bank for this scene (PselWork
 * msgBank 0x234), cached: the bank is the game's, so a mod's text shows. */
#define PW_MSGBANK      0x234u
#define MSG_YES         22              /* YES, then NO                      */

extern void *MSGDATA_AllocString(void *bank, u32 index) PS_WEAK;
extern void  STRBUF_Delete(void *strbuf) PS_WEAK;

static char g_text[2][128];
static int  g_text_ok[2];

static const char *game_text(void *w, u32 index, const char *fallback)
{
    int slot = (int)index - MSG_YES;

    if (slot < 0 || slot > 1) {
        return fallback;
    }
    if (!g_text_ok[slot] && MSGDATA_AllocString != NULL && STRBUF_Delete != NULL
        && rdp(w, PW_MSGBANK) != NULL) {
        void *sb = MSGDATA_AllocString(rdp(w, PW_MSGBANK), index);

        if (sb != NULL) {
            strbuf_text(sb, g_text[slot], (int)sizeof g_text[slot]);
            STRBUF_Delete(sb);
            g_text_ok[slot] = 1;
        }
    }
    return g_text_ok[slot] && g_text[slot][0] != '\0' ? g_text[slot] : fallback;
}

/* The picked Pokemon: its front sprite, playing the idle loop the game
 * plays (ui_mon_front_anim).  The box is sized and the sprite placed from
 * the loop's first frame, so the loop moves about a fixed point instead of
 * being refitted each frame.  The box takes its colour from the sprite. */

static int g_pic_species = -1;          /* the sprite on show                */
static u32 g_pic_start;                 /* the frame its loop started        */
static int g_pic_w, g_pic_h;            /* the first frame's size            */
static int g_pic_ox, g_pic_oy;          /* and its origin                    */
static u32 g_pic_rgb;                   /* the sprite's main colour          */

static u32 scale_rgb(u32 rgb, int num, int den)
{
    return (((rgb >> 16 & 255u) * (u32)num / (u32)den) << 16)
         | (((rgb >> 8 & 255u) * (u32)num / (u32)den) << 8)
         | ((rgb & 255u) * (u32)num / (u32)den);
}

/* The colour the sprite shows most of, weighted to the vivid ones: each
 * palette colour it uses scores its pixel count times its saturation;
 * near black outlines and near white highlights do not count. */
static u32 main_colour(const UiImage *im)
{
    u32 col[64], best = 0x780E20u;
    int cnt[64], n = 0, i, k;
    long score = -1;

    for (i = 0; i < im->w * im->h; i++) {
        const u8 *p = im->rgba + (size_t)i * 4;
        u32 rgb = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];

        if (p[3] == 0) {
            continue;
        }
        for (k = 0; k < n && col[k] != rgb; k++) {
        }
        if (k == n && n < 64) {
            col[n] = rgb;
            cnt[n++] = 0;
        }
        if (k < n) {
            cnt[k]++;
        }
    }
    for (k = 0; k < n; k++) {
        int r = (int)(col[k] >> 16 & 255u), g = (int)(col[k] >> 8 & 255u), b = (int)(col[k] & 255u);
        int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        long sc;

        if (hi < 64 || lo > 200) {
            continue;
        }
        sc = (long)cnt[k] * (hi - lo);
        if (sc > score) {
            score = sc;
            best = col[k];
        }
    }
    return best;
}

static int sprite_first(int species, u32 frame)
{
    const UiImage *im;

    if (species == g_pic_species) {
        return 1;
    }
    im = ui_mon_front_anim((u16)species, 0, 0, &g_pic_ox, &g_pic_oy);
    if (im == NULL) {
        return 0;
    }
    g_pic_species = species;
    g_pic_start = frame;
    g_pic_rgb = main_colour(im);
    {
        /* the drawn pixels, not the canvas (it has a margin, and empty
         * space where a part moves later in the loop) */
        int x, y, x0 = im->w, y0 = im->h, x1 = -1, y1 = -1;

        for (y = 0; y < im->h; y++) {
            for (x = 0; x < im->w; x++) {
                if (im->rgba[((size_t)y * (size_t)im->w + (size_t)x) * 4 + 3] != 0) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
            }
        }
        if (x1 < x0) {
            x0 = y0 = 0;
            x1 = im->w - 1;
            y1 = im->h - 1;
        }
        g_pic_w = x1 - x0 + 1;
        g_pic_h = y1 - y0 + 1;
        g_pic_ox -= x0;
        g_pic_oy -= y0;
    }
    return 1;
}

/* The sprite with its first frame's bottom centre at (cx, by). */
static void sprite_draw(EidosUiCtx *c, int species, u32 frame, int cx, int by)
{
    const UiImage *im;
    int ox, oy;

    im = ui_mon_front_anim((u16)species, 0, frame - g_pic_start, &ox, &oy);
    if (im != NULL) {
        ui_image(c, cx - (g_pic_w / 2 - g_pic_ox + ox), by - (g_pic_h - g_pic_oy + oy), im, 1);
    }
}

/* The Pokedex's picture box, smaller, in the Pokemon's colour: the
 * shadow and the Pokemon, then a strip with its name and type.  Its left
 * edge at x, its bottom at `bottom`. */
#define BOX_W   124             /* at least; the message keeps this clear   */
#define BOX_GAP 8

static void picture_box(EidosUiCtx *c, u32 frame, int species, int x, int bottom)
{
    char name[64];
    int t1, t2, w, h, pw, ph, cx, by, y;
    u32 edge, shade, fill;

    if (!sprite_first(species, frame)) {
        return;
    }
    pw = g_pic_w;
    ph = g_pic_h;
    edge = g_pic_rgb;
    shade = scale_rgb(edge, 1, 2);
    fill = scale_rgb(edge, 1, 8);
    w = (pw + 12 > BOX_W ? pw + 12 : BOX_W);
    h = 6 + ph + 6 + 26;
    y = bottom - h;
    cx = x + w / 2;
    by = y + 6 + ph;
    ui_panel(c, x, y, w, h, CUT_DEF, 6, fill, edge, 255);
    ui_ellipse(c, cx - pw * 3 / 8, by - 11, pw * 3 / 4, 14, shade, UI_NONE, 255);
    ui_ellipse(c, cx - pw * 5 / 16, by - 8, pw * 5 / 8, 9, scale_rgb(edge, 1, 4), UI_NONE, 255);
    sprite_draw(c, species, frame, cx, by);
    ui_fill(c, x + 6, y + h - 28, w - 12, 1, shade, 255);
    if (ui_species_name(species, name, (int)sizeof name) > 0) {
        ui_text(c, x + 8, y + h - 24 + ui_text_y(20, UI_FONT_LARGE), name, UI_WHITE, UI_EDGE,
                UI_FONT_LARGE, 1);
    }
    ui_species_types(species, &t1, &t2);
    if (t1 != UI_TYPE_UNKNOWN) {
        int tx = x + w - 8 - 48;

        if (t2 != t1) {
            ui_type_tag(c, tx, y + h - 22, 48, t2);
            tx -= 52;
        }
        ui_type_tag(c, tx, y + h - 22, 48, t1);
    }
}

/* The message, wrapped to `w`, its bottom at `bottom` (the game's longer
 * lines take two or three rows). */
typedef struct MsgLines { EidosUiCtx *c; int x, y, n; } MsgLines;

static void msg_line(void *user, const char *s, int len)
{
    MsgLines *m = (MsgLines *)user;
    char line[160];

    if (len > (int)sizeof line - 1) {
        len = (int)sizeof line - 1;
    }
    memcpy(line, s, (size_t)len);
    line[len] = 0;
    ui_text(m->c, m->x, m->y + m->n * 15, line, UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
    m->n++;
}

static void message_box(EidosUiCtx *c, int x, int bottom, int w, const char *text)
{
    int lines = ui_wrap(text, w - 20, UI_FONT_LARGE, NULL, NULL);
    int h = 14 + 15 * (lines > 0 ? lines : 1);   /* as ui_dialog, without its buttons */
    MsgLines m;

    ui_panel(c, x, bottom - h, w, h, CUT_DEF, 6, UI_BODY, UI_EDGE, 215);
    m.c = c;
    m.x = x + 10;
    m.y = bottom - h + 7;
    m.n = 0;
    ui_wrap(text, w - 20, UI_FONT_LARGE, msg_line, &m);
}

static void starter_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    int outer = -1;
    void *w = psel_work(&outer);
    int cw = c->canvas->w, ch = c->canvas->h;
    int sub, sel, i, mw, mx, closeup, menu;
    float s, ox;
    char msg[256];
    const char *pairs[4];

    view->mode = EIDOS_VIEW_WORLD;
    /* the screen with the 3D (the case, the close-up; before the case
     * opens, the closed gift box): the main engine's, which POWCNT1 bit 15
     * puts on the top screen when set */
    view->world_screen = (bw_io_read16(0x04000304u) & 0x8000u) != 0 ? 0 : 1;
    view->zoom = 1.0f;
    if (w == NULL) {
        return;
    }
    sub = (int)rd32(w, PW_SUBSTATE);
    sel = (int)rd32(w, PW_SELECTED);
    if (debug_on() && (in->frame % 30u) == 0) {
        fprintf(stderr, "starter: state %d sub %d selected %d\n", outer, sub, sel);
    }
    /* before the case opens the game waits for A or a touch under its own
     * prompt: a click is A */
    if (outer == 4 && sub == 4) {
        EidosUiRect all;

        pairs[0] = "A"; pairs[1] = "OK";
        ui_hints(c, ch - 17, cw, pairs, 1, NULL);
        all.x = 0; all.y = 0; all.w = cw; all.h = ch;
        if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        return;
    }
    /* after YES (14 to 16, then the fade out, state 10) the game closes its
     * YES / NO a frame later than it changes state: its 2D stays hidden to
     * the end, so the old menu never shows */
    if ((outer == 9 && sub >= 14) || outer == 10) {
        view->hide_2d = 1;
        g_last_sub = -1;
        return;
    }
    /* the case (2 to 7, 12, 13) and the close-up (8 to 11); before, the
     * game's own pictures */
    if (outer != 9 || sub < 2 || sub > 13) {
        g_last_sub = -1;
        return;
    }
    /* the close-up opens: the picked Pokemon's cry, as when it was lit */
    if (sub == 8 && g_last_sub != 8 && sel >= 0 && sel <= 2) {
        eidos_ui_cry((u32)species_of(sel));
    }
    g_last_sub = sub;
    view->hide_2d = 1;
    closeup = sub >= 8 && sub <= 11;
    menu = sub == 11;

    /* the DS screen's place in the window: its height, centred */
    s = (float)ch / 192.0f;
    ox = ((float)cw - 256.0f * s) / 2.0f;

    /* the balls as click targets (the game's own glow marks the one picked) */
    if (!closeup) {
        for (i = 0; i < 3; i++) {
            EidosUiRect r;
            int bx = (int)(ox + (float)g_ball_x[i] * s), by = (int)((float)g_ball_y[i] * s);
            int br = (int)((float)BALL_R * s);
            int st;

            r.x = bx - br; r.y = by - br; r.w = 2 * br; r.h = 2 * br;
            st = eidos_ui_hit(r);
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_tap(g_ball_x[i], g_ball_y[i], 3);
            }
        }
    }

    /* the close-up: the picked Pokemon's box at the bottom left, and when
     * the game asks, YES and NO at the bottom right; the message between
     * them keeps both rooms clear, so it sits the same with or without them */
    if (closeup && sel >= 0 && sel <= 2) {
        picture_box(c, in->frame, species_of(sel), 10, ch - 17 - 6);
    }
    strbuf_text(rdp(w, PW_STRBUF), msg, (int)sizeof msg);
    mx = 10 + BOX_W + BOX_GAP;
    mw = cw - 2 * mx;
    if (msg[0] != '\0') {
        message_box(c, mx, ch - 17 - 6, mw, msg);
    }
    if (menu) {
        void *tm = rdp(w, PW_TASKMENU);
        int cur = (tm != NULL && APP_TASKMENU_GetCursorPos != NULL) ? APP_TASKMENU_GetCursorPos(tm) : 0;
        int bx = cw - 10 - BOX_W, hover = -1;
        EidosUiRect r[2];

        for (i = 0; i < 2; i++) {
            r[i].x = bx;
            r[i].y = ch - 17 - 6 - 2 * 26 - 4 + i * 30;
            r[i].w = BOX_W;
            r[i].h = 26;
            if (eidos_ui_hit(r[i]) & EIDOS_UI_HOVER) {
                hover = i;
            }
        }
        for (i = 0; i < 2; i++) {
            ui_menu_row(c, r[i].x, r[i].y, r[i].w, game_text(w, MSG_YES + (u32)i, i == 0 ? "YES" : "NO"),
                        NULL, hover < 0 && cur == i, hover == i);
            if (eidos_ui_hit(r[i]) & EIDOS_UI_CLICK) {
                eidos_ui_tap(220, i == 0 ? 100 : 124, 3);
            }
        }
    }

    /* the keys */
    pairs[0] = "A"; pairs[1] = "Choose";
    pairs[2] = "B"; pairs[3] = "Back";
    ui_hints(c, ch - 17, cw, pairs, menu ? 2 : 1, NULL);
    if (menu && eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void starter_reset(void)
{
    g_last_sub = -1;
    g_text_ok[0] = g_text_ok[1] = 0;
    g_pic_species = -1;
}

static const EidosUiRecipe recipe_starter = { "starter", starter_match, starter_run, starter_reset, 0 };

const EidosUiRecipe *const ui_recipes_starter[] = { &recipe_starter };
const u32 ui_recipes_starter_count = 1;
