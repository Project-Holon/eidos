/*
 * engine/ui/theme_drafts/theme_bubbles.c -- a battle design kept for a later theme: the DS's round buttons: FIGHT in the bottom right corner, POKEMON, BAG and RUN on a
 * quarter ring round it, each with the game's own icon; the moves four round buttons in the corner.
 *
 * A DRAFT: not built, not registered (engine/ui/ui.c adds the themes).  It is the Remix trial
 * source of 2026-11-09 with this design fixed (variant() below); the other designs' code is
 * still in it and goes when this becomes a theme.  It builds against engine/ui/theme.h as of
 * that day (the battle hooks have changed since: battle_move_info takes a UiMoveInfo).
 */

/*
 * engine/ui/theme_remix.c -- Remix: the one-screen interface in Black and
 * White's own language, laid out like the recent games (docs/ui-themes.md).
 *
 * Two surfaces only, the DS's HP plates' own: the white strip and the dark
 * band.  One rule for the choice: the chosen piece turns white.  A piece's
 * colour (a command's, a move's type) sits on one small part of it: its
 * rim, its stripe, its tab.  The commands and the moves share one set of
 * places, so nothing moves when one replaces the other.
 *
 * Four designs on trial (EIDOS_REMIX_LAYOUT), each one idea:
 *   a  Bubbles: the DS's round buttons, all four the same, each with the
 *      game's own icon: FIGHT in the bottom right corner, POKEMON, BAG and
 *      RUN on a quarter ring round it; the chosen one's name at the message
 *      box's end; the moves four equal round buttons in the corner.
 *   b  Band: one band across the bottom is the whole interface, as the DS's
 *      message band: its text at the left, four slanted cells cut into it.
 *   c  Tiles: the DS menu's folder tiles, a 2 x 2 block in the corner with
 *      the message a wide tile beside it and your plate above the message.
 *   d  Hexagons: the DS's move buttons, a column at the right (Scarlet and
 *      Violet's places) standing on a message bar of the same shape.
 *
 * The canvas is 640 x 360 at 720p and smaller at other sizes (a whole-number
 * interface scale), so places scale from 640 x 360.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme.h"

#define R(c) UI_RAW(c)

/* The plates' colours */
#define RX_OUTLINE  0x101010u
#define RX_PLATE_OL 0x202020u
#define RX_BODY_T   0x3C3C3Cu
#define RX_BODY_B   0x242424u
#define RX_DOT      0x484848u
#define RX_DARK_RIM 0x626262u
#define RX_WHITE    0xFFFFFFu
#define RX_WHITE_LO 0xD5D5D5u
#define RX_TEXT     0xF8F8F8u
#define RX_INK      0x202020u
#define RX_INK_SH   0xC4C4C4u
#define RX_MUTED    0x9C9C9Cu
#define RX_ACCENT   0xFFAC00u   /* the plates' "Lv" */

/* The commands' colours, the DS's: FIGHT red, BAG gold, POKEMON green,
 * RUN blue. */
static const u32 k_cmd_color[4] = { 0xF05038u, 0xFFC040u, 0x52CD29u, 0x40A8F0u };
/* The commands as Scarlet and Violet order a list: FIGHT (the game's 0),
 * POKEMON (2), BAG (1), RUN (3). */
static const int k_row_of[4] = { 0, 2, 1, 3 };

/* What the battle shows and for how many frames (the backdrop counts):
 * pieces rise into place when the commands or the moves appear. */
static int g_what, g_age;

/* This file's design (the trial source keeps all four). */
static int variant(void)
{
    return 0;
}

static u32 mix(u32 a, u32 b, int t, int n)
{
    int ch, out = 0;

    if (n <= 0) {
        return a;
    }
    for (ch = 16; ch >= 0; ch -= 8) {
        int va = (int)((a >> ch) & 255u), vb = (int)((b >> ch) & 255u);

        out |= ((va + (vb - va) * t / n) & 255) << ch;
    }
    return (u32)out;
}

static int luma(u32 c)
{
    return (int)(((c >> 16) & 255u) * 3u + ((c >> 8) & 255u) * 6u + (c & 255u)) / 10;
}

/* The game's own text (a message) on a white surface: ink. */
static void remix_text(u32 *rgb, s32 *shadow, u32 behind)
{
    if (luma(behind) > 170 && luma(*rgb) > 150) {
        *rgb = RX_INK;
        if (*shadow >= 0) {
            *shadow = (s32)RX_INK_SH;
        }
    }
}

/* Text with the plates' dark outline (letters on the scene or the band). */
static int otext(EidosUiCtx *c, int x, int y, const char *s, u32 col, u32 ol, int font)
{
    static const int d[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
                                 { 1, 0 },   { -1, 1 }, { 0, 1 },  { 1, 1 } };
    int i;

    for (i = 0; i < 8; i++) {
        ui_text(c, x + d[i][0], y + d[i][1], s, R(ol), -1, font, 1);
    }
    return ui_text(c, x, y, s, R(col), -1, font, 1);
}

/* A label: white and outlined on the dark surface, ink on the white. */
static int label(EidosUiCtx *c, int x, int y, const char *s, int white, u32 col, int font)
{
    if (white) {
        return ui_text(c, x, y, s, R(col == RX_TEXT ? RX_INK : col), (s32)R(RX_INK_SH), font, 1);
    }
    return otext(c, x, y, s, col, RX_PLATE_OL, font);
}

static void label_mid(EidosUiCtx *c, int cx, int y, const char *s, int white, u32 col, int font)
{
    label(c, cx - ui_text_w(s, font) / 2, y, s, white, col, font);
}

/* A type's name in its colour, fit for the surface. */
static int type_word(EidosUiCtx *c, int x, int y, int type, int white)
{
    u32 col = ui_type_color(type);

    if (white) {
        return ui_text(c, x, y, ui_type_name(type), R(mix(col, 0x000000u, 40, 100)), -1, UI_FONT_SMALL, 1);
    }
    return otext(c, x, y, ui_type_name(type), mix(col, 0xFFFFFFu, 35, 100), RX_PLATE_OL, UI_FONT_SMALL);
}

/* The font a move's name takes in `room` pixels. */
static int name_font(const char *name, int room)
{
    return ui_text_w(name, UI_FONT_LARGE) <= room ? UI_FONT_LARGE : UI_FONT_MEDIUM;
}

/* The band: the dark surface across the screen (darker toward its foot,
 * the DS's dot mesh), the white strip along its top. */
static void band(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    int yy, xx;

    ui_fill(c, x, y, w, 1, R(RX_PLATE_OL), a);
    ui_fill(c, x, y + 1, w, 2, R(RX_WHITE), a);
    ui_fill(c, x, y + 3, w, 1, R(RX_DARK_RIM), a);
    for (yy = 4; yy < h; yy++) {
        ui_fill(c, x, y + yy, w, 1, R(mix(0x343434u, 0x1A1A1Au, yy - 4, h - 5)), a * 225 / 255);
    }
    for (yy = 7; yy < h - 2; yy += 5) {
        for (xx = 3; xx < w - 3; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(0x3A3A3Au), a * 110 / 255);
        }
    }
}

/* ---------------------------------------------------------------- */
/* a  Bubbles                                                        */
/* ---------------------------------------------------------------- */

/* An octagon (its corners cut at 45 degrees, `k` deep) in a blend. */
/* The round buttons' sides (a trial: EIDOS_REMIX_SIDES 8, 10 or 12). */
static int sides(void)
{
    static int n = -1;

    if (n < 0) {
        const char *e = getenv("EIDOS_REMIX_SIDES");

        n = e != NULL ? atoi(e) : 10;
        if (n != 8 && n != 12) {
            n = 10;
        }
    }
    return n;
}

/* How far row yy of a w x h polygon stands in from each side.  A square
 * box: the regular polygon of sides() sides, flat at the top, its sides
 * touching the box; any other box: an octagon cut k deep. */
static int row_in(int w, int h, int k, int yy)
{
    if (w == h && w > 2) {
        int n = sides(), i;
        double a = w / 2.0, r = a / cos(3.14159265358979 / n), y = yy + 0.5 - a, best = 0.0;

        for (i = 0; i < n; i++) {
            double t0 = 3.14159265358979 * (2 * i + 1) / n, t1 = 3.14159265358979 * (2 * i + 3) / n;
            double x0 = r * cos(t0), y0 = -r * sin(t0), x1 = r * cos(t1), y1 = -r * sin(t1);

            if ((y >= y0 && y <= y1) || (y >= y1 && y <= y0)) {
                double x = y1 == y0 ? (x0 > x1 ? x0 : x1) : x0 + (x1 - x0) * (y - y0) / (y1 - y0);

                if (x > best) {
                    best = x;
                }
            }
        }
        return (int)(a - best + 0.5);
    }
    {
        int u = k - yy, v = k - (h - 1 - yy), d = u > v ? u : v;

        return d > 0 ? d : 0;
    }
}

/* An octagon (its corners cut at 45 degrees, `k` deep; or the round
 * buttons' polygon in a square box) in a blend. */
static void octagon(EidosUiCtx *c, int x, int y, int w, int h, int k, u32 top, u32 bot, int a)
{
    int yy;

    for (yy = 0; yy < h; yy++) {
        int d = row_in(w, h, k, yy);

        if (w - 2 * d > 0) {
            ui_fill(c, x + d, y + yy, w - 2 * d, 1, R(top == bot ? top : mix(top, bot, yy, h - 1)), a);
        }
    }
}

/* The DS's round button: a deep-cut octagon, a shadow under it, the
 * outline, a rim (`rim`) with a darker inner line, the dark meshed body
 * with a light sheen across its top; chosen, the white surface. */
static void bubble(EidosUiCtx *c, int x, int y, int w, int h, u32 rim, int white, int a)
{
    int k = (h < w ? h : w) * 3 / 10, yy, xx;

    octagon(c, x + 1, y + 3, w, h, k, 0x000000u, 0x000000u, a * 70 / 255);
    octagon(c, x, y, w, h, k, RX_OUTLINE, RX_OUTLINE, a);
    octagon(c, x + 1, y + 1, w - 2, h - 2, k - 1, mix(rim, 0xFFFFFFu, 45, 100), rim, a);
    octagon(c, x + 3, y + 3, w - 6, h - 6, k - 2, mix(rim, 0x000000u, 50, 100), mix(rim, 0x000000u, 50, 100), a);
    if (white) {
        octagon(c, x + 4, y + 4, w - 8, h - 8, k - 2, RX_WHITE, RX_WHITE_LO, a);
        return;
    }
    octagon(c, x + 4, y + 4, w - 8, h - 8, k - 2, RX_BODY_T, RX_BODY_B, a);
    for (yy = 7; yy < h - 6; yy += 5) {
        int d = row_in(w, h, k, yy);

        for (xx = 7 + d; xx < w - 8 - d; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(RX_DOT), a * 150 / 255);
        }
    }
    /* the sheen: a lighter band under the top rim, fading down */
    for (yy = 4; yy < 4 + (h - 8) * 2 / 5; yy++) {
        int d = row_in(w, h, k, yy);

        ui_fill(c, x + d + 5, y + yy, w - 2 * d - 10, 1, R(0xFFFFFFu),
                a * 34 * (4 + (h - 8) * 2 / 5 - yy) / ((h - 8) * 2 / 5) / 255);
    }
}

/* One of the game's own icons, what it shows centred on (cx, cy), k times
 * its size. */
static void game_icon(EidosUiCtx *c, int cx, int cy, int which, int k)
{
    const UiImage *im = ui_app_icon(which);
    int x0, y0, x1 = -1, y1 = -1, xx, yy;

    if (im == NULL) {
        return;
    }
    x0 = im->w;
    y0 = im->h;
    for (yy = 0; yy < im->h; yy++) {
        for (xx = 0; xx < im->w; xx++) {
            if (im->rgba[((size_t)yy * (size_t)im->w + (size_t)xx) * 4 + 3] != 0) {
                x0 = xx < x0 ? xx : x0;
                x1 = xx > x1 ? xx : x1;
                y0 = yy < y0 ? yy : y0;
                y1 = yy > y1 ? yy : y1;
            }
        }
    }
    if (x1 < 0) {
        return;
    }
    ui_image(c, cx - (x0 + x1 + 1) * k / 2, cy - (y0 + y1 + 1) * k / 2, im, k);
}

/* The command the entry draws (the command hook notes it). */
static int g_which;

/* Design a: where the chosen command's name ends (the message box's right
 * end). */
static int g_corner_x, g_corner_y;

/* ---------------------------------------------------------------- */
/* b  Band                                                           */
/* ---------------------------------------------------------------- */

/* A slanted cell of the band (it leans right, one pixel every two rows):
 * at its left edge a dark line, a stripe of its colour and a light line;
 * chosen, the white surface.  The last cell runs to the screen's edge. */
static void cell(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int white, int a)
{
    int yy, cw = c->canvas->w, skew = (h - 1) / 2;

    for (yy = 0; yy < h; yy++) {
        int xs = x + (h - 1 - yy) / 2, xe = xs + w;

        if (x + w + skew >= cw - 2) {
            xe = cw;
        }
        if (white) {
            ui_fill(c, xs + 6, y + yy, xe - xs - 6, 1, R(yy >= h - 2 ? RX_WHITE_LO : RX_WHITE), a);
        }
        ui_fill(c, xs, y + yy, 1, 1, R(RX_OUTLINE), a);
        ui_fill(c, xs + 1, y + yy, 4, 1, R(yy < 2 ? mix(tip, 0xFFFFFFu, 45, 100) : tip), a);
        ui_fill(c, xs + 5, y + yy, 1, 1, R(RX_OUTLINE), a);
        if (!white) {
            ui_fill(c, xs + 6, y + yy, 1, 1, R(RX_DARK_RIM), a * 140 / 255);
        }
    }
}

static int cell_mid(int x, int w, int h)
{
    return x + (h - 1) / 4 + w / 2 + 3;
}

/* ---------------------------------------------------------------- */
/* c  Tiles                                                          */
/* ---------------------------------------------------------------- */

#define TAB_H 5

/* The DS menu's tile: a dark box, its outline and bevel, a lighter gloss
 * toward the top right, a soft shadow under it, and (tab) a folder's tab at
 * the top left in `tip`.  Chosen: the white surface. */
static void tile(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int tab, int white, int a)
{
    int yy;

    ui_fill(c, x + 1, y + h, w, 1, R(0x000000u), a * 90 / 255);
    ui_fill(c, x + 1, y + h + 1, w, 1, R(0x000000u), a * 55 / 255);
    ui_fill(c, x + 1, y + h + 2, w, 1, R(0x000000u), a * 25 / 255);
    if (tab) {
        int tw = w * 2 / 5;

        ui_fill(c, x, y - TAB_H, tw, TAB_H + 1, R(RX_OUTLINE), a);
        ui_fill(c, x + 1, y - TAB_H + 1, tw - 2, TAB_H, R(tip), a);
        ui_fill(c, x + 1, y - TAB_H + 1, tw - 2, 1, R(mix(tip, 0xFFFFFFu, 45, 100)), a);
    }
    ui_fill(c, x, y, w, h, R(RX_OUTLINE), a);
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(white ? RX_WHITE_LO : 0x525252u), a);
    ui_fill(c, x + 2, y + 2, w - 4, h - 4, R(white ? RX_WHITE : 0x202020u), a);
    if (white) {
        return;
    }
    for (yy = 2; yy < h - 2; yy++) {
        int xs = w * 45 / 100 + (yy - 2) * 3;

        if (xs < w - 2) {
            ui_fill(c, x + xs, y + yy, w - 2 - xs, 1, R(0x363636u), a);
        }
    }
}

/* ---------------------------------------------------------------- */
/* d  Hexagons                                                       */
/* ---------------------------------------------------------------- */

/* A long hexagon (its ends points at 45 degrees) in a blend. */
static void hexagon(EidosUiCtx *c, int x, int y, int w, int h, u32 top, u32 bot, int a)
{
    int yy;

    for (yy = 0; yy < h; yy++) {
        int d = abs(2 * yy - (h - 1)) / 2;

        if (w - 2 * d > 0) {
            ui_fill(c, x + d, y + yy, w - 2 * d, 1, R(top == bot ? top : mix(top, bot, yy, h - 1)), a);
        }
    }
}

/* The DS's move button: a shadow, the outline, a thick rim in `tip` with a
 * darker inner line, the dark meshed body; chosen, the white surface. */
static void hex(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int white, int a)
{
    int yy, xx;

    hexagon(c, x + 1, y + 2, w, h, 0x000000u, 0x000000u, a * 70 / 255);
    hexagon(c, x, y, w, h, RX_OUTLINE, RX_OUTLINE, a);
    hexagon(c, x + 2, y + 1, w - 4, h - 2, mix(tip, 0xFFFFFFu, 45, 100), tip, a);
    hexagon(c, x + 4, y + 2, w - 8, h - 4, mix(tip, 0x000000u, 45, 100), mix(tip, 0x000000u, 45, 100), a);
    if (white) {
        hexagon(c, x + 5, y + 3, w - 10, h - 6, RX_WHITE, RX_WHITE_LO, a);
        return;
    }
    hexagon(c, x + 5, y + 3, w - 10, h - 6, RX_BODY_T, RX_BODY_B, a);
    for (yy = 6; yy < h - 6; yy += 5) {
        int d = abs(2 * yy - (h - 1)) / 2;

        for (xx = 10 + d; xx < w - 11 - d; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(RX_DOT), a * 150 / 255);
        }
    }
}

/* ---------------------------------------------------------------- */
/* Battle: places                                                    */
/* ---------------------------------------------------------------- */

#define SX(v) ((v) * l->w / 640)
#define AT_LEAST(v, m) ((v) > (m) ? (v) : (m))

/* The plates: the name line, then the bar (foe 19 rows, yours 27 and the
 * experience line). */
#define NAME_H  16
#define BAR_FOE 19
#define BAR_OWN 27
#define FOE_H   (NAME_H + BAR_FOE)
#define OWN_H   (NAME_H + BAR_OWN + 6)

static void place_own(UiBattleLayout *l, int x, int w, int bottom)
{
    int i;

    for (i = 0; i < l->n_own && i < 3; i++) {
        l->own[i].x = x;
        l->own[i].w = w;
        l->own[i].h = OWN_H;
        l->own[i].y = bottom - OWN_H - (l->n_own - 1 - i) * (OWN_H + 2);
    }
    l->own_balls_y = (l->n_own > 0 ? l->own[0].y : bottom - OWN_H) - 14;
    l->own_balls_right = x + w / 2 > l->w / 2;
    l->own_balls_x = l->own_balls_right ? x + w - 8 : x + 8;
}

static void place_foe(UiBattleLayout *l, int x, int y, int w)
{
    l->foe_x = x;
    l->foe_y = y;
    l->foe_w = w;
    l->foe_step = FOE_H + 2;
    l->foe_balls_x = x + 8;
    l->foe_balls_y = -1;
}

/* The commands and the moves take the same places. */
static void both(UiBattleLayout *l, int cmd, int move, int x, int y, int w, int h)
{
    l->cmd[cmd].x = l->move[move].x = x;
    l->cmd[cmd].y = l->move[move].y = y;
    l->cmd[cmd].w = l->move[move].w = w;
    l->cmd[cmd].h = l->move[move].h = h;
}

static void remix_battle_layout(UiBattleLayout *l, int what)
{
    int m = AT_LEAST(SX(12), 8), hb = l->h - 17, i;
    int ownw = AT_LEAST(SX(220), 190), foew = AT_LEAST(SX(210), 180);

    switch (variant()) {
    default:
    case 0: {
        /* FIGHT in the middle, a button's half inside the corner; the
         * others on the ring round it: POKEMON at 90 degrees, BAG at 135,
         * RUN at 180 (sin and cos in 1/1000) */
        static const int ring[4][2] = { { 0, 0 }, { -707, -707 }, { 0, -1000 }, { -1000, 0 } };
        static const int at_of[4] = { 0, 1, 2, 3 };
        /* FIGHT a third larger than the three */
        int gap = 3, base = hb - 6, right = l->w - m, d = 48, rr = 72;
        int f = d * 4 / 3;
        int w = AT_LEAST(SX(204), 188), h = 88, mw = (w - gap) / 2, mh = (h - gap) / 2;
        int ox = right - f / 2, oy = base - f / 2, left;

        for (i = 0; i < 4; i++) {
            int k = at_of[i], sz = i == 0 ? f : d;

            l->cmd[i].x = ox + ring[k][0] * rr / 1000 - sz / 2;
            l->cmd[i].y = oy + ring[k][1] * rr / 1000 - sz / 2;
            l->cmd[i].w = l->cmd[i].h = sz;
        }
        for (i = 0; i < 4; i++) {
            l->move[i].x = right - w + (i % 2) * (mw + gap);
            l->move[i].y = base - h + (i / 2) * (mh + gap);
            l->move[i].w = mw;
            l->move[i].h = mh;
        }
        left = what == UI_BATTLE_MOVES ? right - w : ox - rr - d / 2;
        l->msg.x = m;
        l->msg.h = 44;
        l->msg.y = base - l->msg.h;
        l->msg.w = (what == UI_BATTLE_MESSAGE ? right : left - 6) - m;
        g_corner_x = l->msg.x + l->msg.w - 12;
        g_corner_y = l->msg.y + ui_text_y(l->msg.h, UI_FONT_LARGE);
        place_own(l, m, ownw, l->msg.y - 6);
        place_foe(l, l->w - m - foew, m, foew);
        break;
    }
    case 1: {
        /* the band: its text at the left, four slanted cells to the right
         * edge */
        int bh = 54, by = hb - bh, ch = bh - 4, skew = (ch - 1) / 2;
        int x0 = AT_LEAST(SX(176), 132), cw = (l->w - x0 - skew) / 4;

        for (i = 0; i < 4; i++) {
            both(l, i, k_row_of[i], x0 + k_row_of[i] * cw, by + 4, cw, ch);
        }
        l->msg.x = 0;
        l->msg.y = by;
        l->msg.h = bh;
        l->msg.w = what == UI_BATTLE_MESSAGE ? l->w : x0;
        place_own(l, l->w - m - ownw, ownw, by - 5);
        place_foe(l, m, m, foew);
        break;
    }
    case 2: {
        /* the tiles 2 x 2 in the corner, the message tile beside them at
         * the same foot, your plate above the message */
        int gap = 4, base = hb - 7, tw = AT_LEAST(SX(150), 132), th = 36;
        int bx = l->w - m - (2 * tw + gap);

        for (i = 0; i < 4; i++) {
            both(l, i, i, bx + (i % 2) * (tw + gap), base - th - (1 - i / 2) * (th + gap + TAB_H), tw, th);
        }
        l->msg.x = m;
        l->msg.h = 46;
        l->msg.y = base - l->msg.h;
        l->msg.w = (what == UI_BATTLE_MESSAGE ? l->w - m : bx - 2 * gap) - m;
        place_own(l, m, ownw, l->msg.y - 7);
        place_foe(l, l->w - m - foew, m, foew);
        break;
    }
    case 3: {
        /* the column at the right, down to the foot; the message bar to
         * its left, your plate on the bar */
        int gap = 2, hh = 24, w = AT_LEAST(SX(156), 144), cx = l->w - m - w, y0;

        l->msg.x = m + 14;
        l->msg.w = (what == UI_BATTLE_MESSAGE ? l->w - m : cx - 5) - 14 - l->msg.x;
        l->msg.h = 38;
        l->msg.y = hb - 4 - l->msg.h;
        y0 = hb - 4 - (4 * hh + 3 * gap);
        for (i = 0; i < 4; i++) {
            both(l, i, k_row_of[i], cx, y0 + k_row_of[i] * (hh + gap), w, hh);
        }
        place_own(l, m, ownw, l->msg.y - 5);
        place_foe(l, l->w - m - foew, m, foew);
        break;
    }
    }
    l->info_x = l->msg.x + 10;
    l->info_y = l->msg.y + 10;
}

/* ---------------------------------------------------------------- */
/* Battle: the hooks                                                 */
/* ---------------------------------------------------------------- */

static void surface(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    switch (variant()) {
    case 0:
        octagon(c, x + 1, y + 3, w, h, 8, 0x000000u, 0x000000u, a * 70 / 255);
        octagon(c, x, y, w, h, 8, RX_OUTLINE, RX_OUTLINE, a);
        octagon(c, x + 1, y + 1, w - 2, h - 2, 7, RX_WHITE, RX_WHITE_LO, a);
        octagon(c, x + 3, y + 3, w - 6, h - 6, 6, RX_DARK_RIM, RX_DARK_RIM, a);
        octagon(c, x + 4, y + 4, w - 8, h - 8, 6, 0x2C2C2Cu, 0x1C1C1Cu, a);
        break;
    case 2:
        tile(c, x, y, w, h, 0, 0, 0, a * 240 / 255);
        break;
    case 3:
        hex(c, x - 14, y, w + 28, h, 0xE8E8E8u, 0, a * 245 / 255);
        break;
    default:
        band(c, x, y, w, h, a);
        break;
    }
}

/* The message's surface.  With the commands or the moves up it is under
 * them already (the backdrop), full width. */
static void remix_battle_box(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    if (g_what == UI_BATTLE_MESSAGE) {
        surface(c, x, y, w, h, a);
    }
}

static void remix_battle_backdrop(EidosUiCtx *c, const UiBattleLayout *l, int what)
{
    if (what != g_what) {
        g_what = what;
        g_age = 0;
    } else if (g_age < 1000) {
        g_age++;
    }
    if (what == UI_BATTLE_MESSAGE) {
        return;
    }
    if (variant() == 1) {
        band(c, 0, l->msg.y, l->w, l->msg.h, 255);
    } else {
        surface(c, l->msg.x, l->msg.y, l->msg.w, l->msg.h, 255);
    }
}

/* A piece's rise into place: its y offset and its alpha for the first
 * frames after the commands or the moves appear. */
#define RISE 6

static int rise_y(void)
{
    return g_age < RISE ? (RISE - g_age) * (RISE - g_age) / 4 : 0;
}

static int rise_a(int a)
{
    return g_age < RISE ? a * (g_age + 2) / (RISE + 2) : a;
}

/* One piece: a command (type < 0, its name alone) or a move (its name,
 * type and PP). */
static void entry(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int type, const char *name, int pp,
                  int maxpp, int sel, int a)
{
    char pps[20];
    u32 col = a >= 255 ? RX_TEXT : RX_MUTED, ppc;
    int cx, big = UI_FONT_LARGE, small = UI_FONT_SMALL;

    a = rise_a(a);
    y += rise_y();
    snprintf(pps, sizeof(pps), "%d/%d", pp, maxpp);
    ppc = pp == 0 && type >= 0 ? 0xFF6040u : RX_TEXT;
    switch (variant()) {
    default:
    case 0:
        cx = x + w / 2;
        if (type < 0) {
            /* a command: a white rim and the game's own icon (FIGHT: the
             * physical move's badge; POKEMON and the Bag: the field
             * menu's; RUN: the touch bar's return arrow); the chosen one's
             * name at the message box's end */
            static const int icon_of[4] = { UI_APP_ICON_PHYSICAL, UI_APP_ICON_BAG, UI_APP_ICON_POKEMON,
                                            UI_APP_ICON_BACK };

            bubble(c, x, y, w, h, 0xF0F0F0u, sel, a);
            if (g_which >= 0) {
                game_icon(c, cx, y + h / 2, icon_of[g_which], 1);
            }
            if (sel && g_which >= 0) {
                label(c, g_corner_x - ui_text_w(name, big), g_corner_y, name, 0, RX_ACCENT, big);
            }
            break;
        }
        bubble(c, x, y, w, h, tip, sel, a);
        big = name_font(name, w - 24);
        label_mid(c, cx, y + 7, name, sel, col, big);
        label_mid(c, cx, y + h - 17, pps, sel, ppc, small);
        break;
    case 1:
        cx = cell_mid(x, w, h);
        cell(c, x, y, w, h, tip, sel, a);
        if (type < 0) {
            label_mid(c, cx, y + ui_text_y(h, big), name, sel, col, big);
            break;
        }
        big = name_font(name, w - 26);
        label_mid(c, cx + 5, y + 8, name, sel, col, big);
        {
            int tw = ui_text_w(ui_type_name(type), small), tx = cx - 5 - (tw + 7 + ui_text_w(pps, small)) / 2;

            type_word(c, tx, y + h - 19, type, sel);
            label(c, tx + tw + 7, y + h - 19, pps, sel, ppc, small);
        }
        break;
    case 2:
        cx = x + w / 2;
        tile(c, x, y, w, h, tip, 1, sel, a);
        if (type < 0) {
            label_mid(c, cx, y + ui_text_y(h, big), name, sel, col, big);
            break;
        }
        big = name_font(name, w - 12);
        label_mid(c, cx, y + 4, name, sel, col, big);
        {
            int tw = ui_text_w(ui_type_name(type), small), tx = cx - (tw + 8 + ui_text_w(pps, small)) / 2;

            type_word(c, tx, y + h - 14, type, sel);
            label(c, tx + tw + 8, y + h - 14, pps, sel, ppc, small);
        }
        break;
    case 3:
        hex(c, x, y, w, h, tip, sel, a);
        if (type < 0) {
            label_mid(c, x + w / 2, y + ui_text_y(h, big), name, sel, col, big);
            break;
        }
        {
            int right = x + w - h / 2 - 4, nx = x + h / 2 + 5;

            label(c, right - ui_text_w(pps, small), y + ui_text_y(h, small), pps, sel, ppc, small);
            big = name_font(name, right - 34 - nx);
            label(c, nx, y + ui_text_y(h, big), name, sel, col, big);
        }
        break;
    }
}

static void remix_battle_command(EidosUiCtx *c, int x, int y, int w, int h, int which, const char *name,
                                 u32 color, int sel, int enabled)
{
    (void)color;
    g_which = which & 3;
    entry(c, x, y, w, h, k_cmd_color[which & 3], -1, name, 0, 0, sel, enabled ? 255 : 120);
}

static void remix_battle_move(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name, int pp,
                              int maxpp, int sel, int enabled)
{
    if (name == NULL) {
        /* an empty place: the piece, faint, nothing on it */
        g_which = -1;
        entry(c, x, y, w, h, 0x4A4A4Au, -1, "", 0, 0, 0, 110);
        return;
    }
    entry(c, x, y, w, h, ui_type_color(type), type, name, pp, maxpp, sel, enabled ? 255 : 130);
}

/* The chosen move's facts where the prompt stands: its kind, then its
 * power and accuracy (one line where the surface is wide). */
static void remix_battle_move_info(EidosUiCtx *c, const UiBattleLayout *l, const char *name, int category,
                                   int power, int accuracy)
{
    static const char *const cat[3] = { "Physical", "Special", "Status" };
    int x = l->info_x, y = l->info_y, font = UI_FONT_LARGE;
    int room = (variant() == 3 ? l->move[0].x : l->msg.x + l->msg.w) - x - 8, x0 = x;
    char pw[24], ac[24];

    (void)name;
    snprintf(pw, sizeof(pw), power > 0 ? "Power %d" : "Power -", power);
    snprintf(ac, sizeof(ac), accuracy > 0 ? "Accuracy %d" : "Accuracy -", accuracy);
    if (variant() == 3) {
        y = l->msg.y + ui_text_y(l->msg.h, font);
    }
    x = label(c, x, y, cat[category % 3], 0, RX_ACCENT, font) + 16;
    if (x - x0 + ui_text_w(pw, font) + 16 + ui_text_w(ac, font) > room) {
        /* two lines: the numbers under, in the small font where narrow */
        font = ui_text_w(pw, font) + 12 + ui_text_w(ac, font) > room ? UI_FONT_SMALL : font;
        x = x0;
        y += 17;
    }
    x = label(c, x, y, pw, 0, RX_TEXT, font) + (font == UI_FONT_SMALL ? 10 : 16);
    label(c, x, y, ac, 0, RX_TEXT, font);
}

/* ---------------------------------------------------------------- */
/* The plates                                                        */
/* ---------------------------------------------------------------- */

/* One row of a plate's bar: from the screen's edge to the point, which
 * stands back 45 degrees above and below the white strip's middle. */
static void bar_row(EidosUiCtx *c, int right, int x, int w, int cw, int yy, int y, int in, u32 col)
{
    int d = abs(yy - 7) + in;

    if (right) {
        ui_fill(c, x + d, y + yy, cw - x - d, 1, R(col), 255);
    } else {
        ui_fill(c, 0, y + yy, x + w - d, 1, R(col), 255);
    }
}

static void hp_colors(int hp, int maxhp, u32 *hi, u32 *lo)
{
    if (hp * 2 > maxhp) {
        *hi = 0x00FF4Au;
        *lo = 0x00BD20u;
    } else if (hp * 5 > maxhp) {
        *hi = 0xFFE040u;
        *lo = 0xD0A000u;
    } else {
        *hi = 0xFF5A3Au;
        *lo = 0xC02010u;
    }
}

/* A Pokemon's plate, as the DS's: the name, gender and level above (letters
 * outlined), then a bar from the screen's edge that ends in a point: a
 * white strip with the HP bar, a dark band under it (yours: the HP numbers),
 * yours also the experience line under the bar. */
static void remix_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp,
                        int maxhp, int exp_num, int exp_den)
{
    int own = exp_den > 0, cw = c->canvas->w, right = x + w / 2 > cw / 2;
    int bh = own ? BAR_OWN : BAR_FOE, by = y + NAME_H, yy, nx, lx;
    int c0 = right ? x + 18 : x + 6, c1 = right ? x + w - 6 : x + w - 18;
    char lv[16];

    /* the name line */
    nx = otext(c, c0 + 2, y + 1, name, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE);
    ui_gender(c, nx + 3, y + 1, gender, UI_FONT_LARGE);
    snprintf(lv, sizeof(lv), "%d", level);
    lx = c1 - 2 - ui_text_w(lv, UI_FONT_LARGE);
    otext(c, lx, y + 1, lv, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE);
    otext(c, lx - 3 - ui_text_w("Lv", UI_FONT_SMALL), y + 5, "Lv", 0xFFAC00u, RX_PLATE_OL, UI_FONT_SMALL);
    /* the bar */
    for (yy = 0; yy < bh; yy++) {
        bar_row(c, right, x, w, cw, yy, by, 0, RX_PLATE_OL);
    }
    for (yy = 1; yy < bh - 1; yy++) {
        u32 col = yy == 1 || yy == 13 ? 0x626262u : yy <= 11 ? 0xFFFFFFu : yy == 12 ? 0xD5D5D5u : 0x313131u;

        bar_row(c, right, x, w, cw, yy, by, 1, col);
    }
    /* the HP tag and bar in the white strip */
    {
        int tx = c0 + 4, tw = 20, bx = tx + tw + 2, bw = c1 - 6 - bx, fw;
        u32 hi, lo;

        ui_fill(c, tx, by + 3, tw, 8, R(0x313131u), 255);
        ui_text(c, tx + 3, by + 3 + ui_text_y(8, UI_FONT_SMALL), "HP", R(0x00FF4Au), -1, UI_FONT_SMALL, 1);
        ui_fill(c, bx, by + 4, bw, 6, R(0x313131u), 255);
        ui_fill(c, bx + 1, by + 5, bw - 2, 4, R(0x5A5A5Au), 255);
        if (maxhp > 0 && hp > 0) {
            fw = (int)((long)(bw - 2) * (hp > maxhp ? maxhp : hp) / maxhp);
            if (fw < 1) {
                fw = 1;
            }
            hp_colors(hp, maxhp, &hi, &lo);
            ui_fill(c, bx + 1, by + 5, fw, 2, R(hi), 255);
            ui_fill(c, bx + 1, by + 7, fw, 2, R(lo), 255);
        }
    }
    if (own) {
        char nums[24];
        int ex0 = c0 + 30, ew = c1 - ex0;

        snprintf(nums, sizeof(nums), "%d/%d", hp, maxhp);
        ui_text(c, c1 - 8 - ui_text_w(nums, UI_FONT_LARGE), by + 12, nums, R(0xFFFFFFu), (s32)R(0x838383u),
                UI_FONT_LARGE, 1);
        /* the experience line */
        ui_fill(c, ex0, by + bh + 1, ew, 4, R(RX_PLATE_OL), 255);
        ui_fill(c, ex0 + 1, by + bh + 2, ew - 2, 2, R(0xFFFFFFu), 255);
        if (exp_num > 0) {
            ui_fill(c, ex0 + 1, by + bh + 2, (ew - 2) * (exp_num > exp_den ? exp_den : exp_num) / exp_den, 2,
                    R(0x40B8F0u), 255);
        }
    }
}

static void remix_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge)
{
    int right = x + w / 2 > c->canvas->w / 2;

    (void)level;
    if (badge >= 0) {
        ui_status_badge(c, (right ? x + w - 6 : x + w - 18) - 80, y + 3, badge);
    }
}

const UiThemeOps ui_theme_draft_bubbles = {
    .id = "bubbles",
    .name = "Bubbles",
    .text = remix_text,
    .plate = remix_plate,
    .plate_status = remix_plate_status,
    .battle_layout = remix_battle_layout,
    .battle_backdrop = remix_battle_backdrop,
    .battle_box = remix_battle_box,
    .battle_command = remix_battle_command,
    .battle_move = remix_battle_move,
    .battle_move_info = remix_battle_move_info,
};
