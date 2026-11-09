/*
 * engine/ui/theme_remix.c -- Remix: the one-screen interface laid out like
 * the recent games (Sword and Shield, Scarlet and Violet), every piece
 * drawn as Black and White draw theirs (docs/ui-themes.md).
 *
 * Battle: the DS's top screen with the recent games' command column.  The
 * message is the DS's dark see-through band across the bottom; your plate
 * stands on it at the left, the foe's at the top right, each the DS's
 * white bar running in from the screen's edge to a point, the name and
 * level above it.  The commands are a column at the right over the band:
 * FIGHT! as the battle ball's red with its dark lens, POKEMON, BAG and RUN
 * as the DS's hex buttons; the moves the DS's rounded buttons in their
 * type's colour, the chosen move's power, accuracy and category in the
 * band where the DS prints them.
 *
 * The canvas is 640 x 360 at 720p and smaller at other sizes (the interface
 * scale is a whole number), so places scale from 640 x 360.
 */
#include <stdio.h>
#include <string.h>

#include "theme.h"

/* Black and White's colours (the DS's screens) */
#define RX_WHITE    0xFAFAFAu
#define RX_BAR      0xF8F8F8u
#define RX_OUTLINE  0x181820u
#define RX_SHADE    0x1E1E22u
#define RX_LV       0x6EDC8Cu
#define RX_RIM      0xC4C4CEu
#define RX_RIM_DARK 0x60606Au
#define RX_HEX      0x2C2C30u
#define RX_HEX_SEL  0x40404Au
#define RX_LENS     0x3C2C2Eu
#define RX_RED_TOP  0xE82C46u
#define RX_RED_BOT  0xAA1026u
#define RX_FIGHT    0xF01E3Eu
#define RX_GOLD     0xFFF478u
#define RX_TEAL     0x30C8E0u
#define RX_INK      0x28282Cu

#define R(c) UI_RAW(c)

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

/* The screens Remix has not laid out yet keep Remaster's drawing, with
 * Black and White's teal for its cyan accents. */
static u32 remix_color(u32 rgb)
{
    switch (rgb) {
    case UI_CYAN:
    case UI_CYAN_EDGE:
    case UI_CURSOR:
        return RX_TEAL;
    default:
        return rgb;
    }
}

/* Outlined text, as the DS writes names on its plates: the letters with a
 * one-pixel dark ring. */
static int otext(EidosUiCtx *c, int x, int y, const char *s, u32 col, u32 ring, int font)
{
    static const int d[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
                                 { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    int i;

    for (i = 0; i < 8; i++) {
        ui_text(c, x + d[i][0], y + d[i][1], s, R(ring), (s32)R(ring), font, 1);
    }
    return ui_text(c, x, y, s, R(col), -1, font, 1);
}

/* ---------------------------------------------------------------- */
/* Shapes                                                            */
/* ---------------------------------------------------------------- */

/* An octagon (a box with its four corners cut by k), shrunk by `in`, in a
 * vertical blend: the DS's battle buttons. */
static void octagon(EidosUiCtx *c, int x, int y, int w, int h, int k, int in, u32 top, u32 bot, int a)
{
    int yy;

    for (yy = in; yy < h - in; yy++) {
        int e = yy < k ? k - yy : yy >= h - k ? yy - (h - 1 - k) : 0;
        int l = x + in + (e > in ? e - in : 0), r = x + w - in - (e > in ? e - in : 0);

        if (r > l) {
            ui_fill(c, l, y + yy, r - l, 1, R(mix(top, bot, yy, h)), a);
        }
    }
}

/* A rounded box of radius r, shrunk by `in`, in a vertical blend. */
static void rounded(EidosUiCtx *c, int x, int y, int w, int h, int r, int in, u32 top, u32 bot, int a)
{
    int yy;

    for (yy = in; yy < h - in; yy++) {
        int d = yy < r ? r - yy : yy >= h - r ? yy - (h - 1 - r) : 0, e = 0;

        while (d > 0 && (r - e) * (r - e) + d * d > r * r + r / 2) {
            e++;
        }
        if (w - 2 * in - 2 * e > 0) {
            ui_fill(c, x + in + e, y + yy, w - 2 * in - 2 * e, 1, R(mix(top, bot, yy, h)), a);
        }
    }
}

/* The DS's plate bar: white, outlined, running from x0 to x1 and ending in
 * a point on the `tip` side (+1 right, -1 left), h tall. */
static void plate_bar(EidosUiCtx *c, int x0, int x1, int y, int h, int tip, int a)
{
    int yy, half = h / 2;

    for (yy = 0; yy < h; yy++) {
        int p = yy <= half ? yy : h - 1 - yy;   /* the point's reach this row */
        int l = x0, r = x1;

        if (tip > 0) {
            r = x1 + p;
        } else {
            l = x0 - p;
        }
        ui_fill(c, l, y + yy, r - l, 1, R(RX_OUTLINE), a);
        if (yy > 0 && yy < h - 1) {
            int il = tip > 0 ? l : l + 1, ir = tip > 0 ? r - 1 : r;

            if (tip > 0 && x0 > 0) {
                il++;
            }
            if (tip < 0 && x1 < 9999) {
                ir--;
            }
            ui_fill(c, il, y + yy, ir - il, 1, R(RX_BAR), a);
        }
    }
}

/* The DS's HP bar: the HP tag, then the dark slot. */
static void hp_bar(EidosUiCtx *c, int x, int y, int w, int hp, int maxhp)
{
    ui_fill(c, x, y, 13, 6, R(0x282830u), 255);
    ui_text(c, x + 1, y - 2, "HP", R(0xC8EC48u), -1, UI_FONT_SMALL, 1);
    ui_bar(c, x + 14, y, w - 14, 6, hp, maxhp);
}

/* ---------------------------------------------------------------- */
/* Battle                                                            */
/* ---------------------------------------------------------------- */

#define SX(v) ((v) * l->w / 640)
#define SY(v) ((v) * l->h / 360)
#define AT_LEAST(v, m) ((v) > (m) ? (v) : (m))

static void remix_battle_layout(UiBattleLayout *l, int what)
{
    int hint = 17, band = 42, gap = 5;
    int bandy = l->h - hint - band;
    int colw = AT_LEAST(SX(176), 150), colx = l->w - SX(12) - colw;
    int fight = AT_LEAST(SY(50), 36), row = AT_LEAST(SY(30), 24), mrow = AT_LEAST(SY(34), 28);
    int ownw = AT_LEAST(SX(232), 190), ownh = 44, i;

    (void)what;
    /* the message: the DS's band across the whole bottom */
    l->msg.x = 0;
    l->msg.y = bandy;
    l->msg.w = l->w;
    l->msg.h = band;
    /* your plate on the band at the left, from the screen's edge */
    for (i = 0; i < l->n_own && i < 3; i++) {
        l->own[i].x = 0;
        l->own[i].w = ownw;
        l->own[i].h = ownh;
        l->own[i].y = bandy - gap - ownh - (l->n_own - 1 - i) * (ownh + 2);
    }
    l->own_balls_x = 14;
    l->own_balls_y = (l->n_own > 0 ? l->own[0].y : bandy - ownh) - 22;
    l->own_balls_right = 0;
    /* the foe's plate at the top right, to the screen's edge */
    l->foe_w = AT_LEAST(SX(232), 190);
    l->foe_x = l->w - l->foe_w;
    l->foe_y = SY(12);
    l->foe_step = 30;
    l->foe_balls_x = l->w - 6 - 6 * 16 - 8;
    l->foe_balls_y = -1;
    /* the commands, a column at the right over the band: FIGHT! (the
     * game's 0) first and tallest, then POKEMON (2), BAG (1), RUN (3) */
    {
        static const int row_of[4] = { 0, 2, 1, 3 };
        int y0 = bandy - gap - (fight + 3 * row + 3 * gap);

        for (i = 0; i < 4; i++) {
            int r = row_of[i];

            l->cmd[i].x = colx;
            l->cmd[i].w = colw;
            l->cmd[i].y = r == 0 ? y0 : y0 + fight + gap + (r - 1) * (row + gap);
            l->cmd[i].h = r == 0 ? fight : row;
        }
    }
    /* the moves, the same column */
    {
        int y0 = bandy - gap - (4 * mrow + 3 * gap);

        for (i = 0; i < 4; i++) {
            l->move[i].x = colx;
            l->move[i].w = colw;
            l->move[i].y = y0 + i * (mrow + gap);
            l->move[i].h = mrow;
        }
        l->info_x = 16;
        l->info_y = bandy;
    }
}

/* The DS's message band: dark, see-through, a thin light rim on top. */
static void remix_battle_box(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    int yy;

    for (yy = 0; yy < h; yy++) {
        ui_fill(c, x, y + yy, w, 1, R(mix(0x080C0Cu, 0x000000u, yy, h)), (150 + 60 * yy / (h > 1 ? h - 1 : 1)) * a / 255);
    }
    ui_fill(c, x, y, w, 1, R(0x788080u), 200 * a / 255);
}

/* FIGHT!: the ball's red with its dark lens, the label in the ball's red. */
static void fight_button(EidosUiCtx *c, int x, int y, int w, int h, int sel)
{
    int k = h * 30 / 100;
    int lw = w * 84 / 100, lh = h * 56 / 100;
    const char *t = "FIGHT!";
    int tw = ui_text_w(t, UI_FONT_LARGE), big = tw * 2 <= lw - 10 && 30 <= lh + 6;

    octagon(c, x, y, w, h, k, 0, sel ? RX_GOLD : RX_RIM_DARK, sel ? RX_GOLD : RX_RIM_DARK, 255);
    octagon(c, x, y, w, h, k, 1, RX_RIM, RX_RIM, 255);
    octagon(c, x, y, w, h, k, 3, RX_RED_TOP, RX_RED_BOT, 255);
    ui_ellipse(c, x + (w - lw) / 2, y + (h - lh) / 2, lw, lh, R(RX_LENS), R(0x140A0Cu), 255);
    if (big) {
        int ty = y + (h - 30) / 2;

        ui_text(c, x + (w - tw * 2) / 2 + 2, ty + 2, t, R(0x380610u), -1, UI_FONT_LARGE, 2);
        ui_text(c, x + (w - tw * 2) / 2, ty, t, R(RX_FIGHT), (s32)R(0x380610u), UI_FONT_LARGE, 2);
    } else {
        otext(c, x + (w - tw) / 2, y + ui_text_y(h, UI_FONT_LARGE), t, RX_FIGHT, 0x380610u, UI_FONT_LARGE);
    }
}

/* POKEMON, BAG, RUN: the DS's dark hex buttons, the label in its colour. */
static void hex_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 col, int sel,
                       int enabled)
{
    int k = h * 30 / 100, a = enabled ? 255 : 150;
    int tw = ui_text_w(label, UI_FONT_LARGE);

    octagon(c, x, y, w, h, k, 0, sel ? RX_GOLD : RX_RIM_DARK, sel ? RX_GOLD : RX_RIM_DARK, a);
    octagon(c, x, y, w, h, k, 1, RX_RIM, RX_RIM, a);
    octagon(c, x, y, w, h, k, 2, sel ? RX_HEX_SEL : RX_HEX, sel ? RX_HEX_SEL : 0x1A1A1Eu, a);
    otext(c, x + (w - tw) / 2, y + ui_text_y(h, UI_FONT_LARGE), label, enabled ? col : 0x707078u,
          0x101012u, UI_FONT_LARGE);
}

static void remix_battle_command(EidosUiCtx *c, int x, int y, int w, int h, int which, const char *label,
                                 u32 color, int sel, int enabled)
{
    if (which == 0) {
        fight_button(c, x, y, w, h, sel);
    } else {
        hex_button(c, x, y, w, h, label, color, sel, enabled);
    }
}

/* A move as the DS draws it: a rounded button in its type's colour, light
 * at the top, the name, the type's tag and the PP. */
static void remix_battle_move(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name, int pp,
                              int maxpp, int sel, int enabled)
{
    u32 col, light;
    int a = enabled ? 255 : 140;
    char pps[24];
    const char *tn;
    int tw;

    if (name == NULL) {
        rounded(c, x, y, w, h, 6, 0, 0x202024u, 0x202024u, 120);
        return;
    }
    col = ui_type_color(type);
    light = mix(col, 0xFFFFFFu, 55, 100);
    rounded(c, x, y, w, h, 7, 0, sel ? RX_GOLD : 0x18181Cu, sel ? RX_GOLD : 0x18181Cu, a);
    rounded(c, x, y, w, h, 6, sel ? 2 : 1, light, col, a);
    rounded(c, x, y + 2, w, h * 42 / 100, 4, 2, 0xFFFFFFu, 0xFFFFFFu, 60 * a / 255);
    ui_text(c, x + 10, y + 3, name, R(RX_INK), (s32)R(0xFAFAFAu), UI_FONT_LARGE, 1);
    /* the type's tag at the left of the second line, the PP at its right */
    tn = ui_type_name(type);
    tw = ui_text_w(tn, UI_FONT_SMALL) + 8;
    ui_fill(c, x + 10, y + h - 13, tw, 10, R(RX_OUTLINE), a);
    ui_fill(c, x + 11, y + h - 12, tw - 2, 8, R(mix(col, 0x000000u, 30, 100)), a);
    ui_text(c, x + 14, y + h - 12 + ui_text_y(8, UI_FONT_SMALL), tn, R(RX_WHITE), -1, UI_FONT_SMALL, 1);
    snprintf(pps, sizeof(pps), "PP %d/%d", pp, maxpp);
    ui_text(c, x + w - 9 - ui_text_w(pps, UI_FONT_SMALL), y + h - 12 + ui_text_y(8, UI_FONT_SMALL), pps,
            R(pp == 0 ? 0xC02010u : RX_INK), (s32)R(0xFAFAFAu), UI_FONT_SMALL, 1);
}

/* The chosen move's facts in the band, where the DS prints them. */
static void remix_battle_move_info(EidosUiCtx *c, const UiBattleLayout *l, const char *name, int category,
                                   int power, int accuracy)
{
    static const char *const cat[3] = { "Physical", "Special", "Status" };
    char facts[64];
    int y = l->msg.y + 4;

    remix_battle_box(c, l->msg.x, l->msg.y, l->msg.w, l->msg.h, 225);
    ui_text(c, l->info_x, y, name, R(RX_WHITE), (s32)R(0x38383Cu), UI_FONT_LARGE, 1);
    if (power > 0) {
        snprintf(facts, sizeof(facts), "Power %d   Accuracy %d   %s", power, accuracy, cat[category % 3]);
    } else if (accuracy > 0) {
        snprintf(facts, sizeof(facts), "Accuracy %d   %s", accuracy, cat[category % 3]);
    } else {
        snprintf(facts, sizeof(facts), "%s", cat[category % 3]);
    }
    ui_text(c, l->info_x, y + 18, facts, R(0xC4C8CEu), -1, UI_FONT_SMALL, 1);
}

/* ---------------------------------------------------------------- */
/* Plates                                                            */
/* ---------------------------------------------------------------- */

/* A battle plate as the DS's top screen draws it: the name, gender and
 * level in outlined white over a white bar that runs in from the screen's
 * edge and ends in a point; yours (exp_den > 0) also carries the HP
 * numbers on a dark strip under the bar, and the experience line. */
static void remix_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp,
                        int maxhp, int exp_num, int exp_den)
{
    int own = exp_den > 0;
    int nx = own ? x + 16 : x + 26, by = y + 15, bh = 11;
    char lv[16];

    nx = otext(c, nx, y, name, RX_WHITE, RX_OUTLINE, UI_FONT_LARGE);
    ui_gender(c, nx + 2, y, gender, UI_FONT_LARGE);
    snprintf(lv, sizeof(lv), "%d", level);
    {
        int lx = x + w - (own ? 52 : 40);

        otext(c, lx, y + 3, "Lv", RX_LV, RX_OUTLINE, UI_FONT_SMALL);
        otext(c, lx + 12, y, lv, RX_WHITE, RX_OUTLINE, UI_FONT_LARGE);
    }
    if (own) {
        plate_bar(c, x, x + w - 12, by, bh, 1, 245);
        hp_bar(c, x + 40, by + 2, w - 12 - 40 - 8, hp, maxhp);
        {
            char nums[24];
            int sx = x + 34, sw = w - 12 - 34;

            snprintf(nums, sizeof(nums), "%d/%d", hp, maxhp);
            ui_fill(c, sx, by + bh + 2, sw, 13, R(0x222226u), 210);
            otext(c, x + w - 16 - ui_text_w(nums, UI_FONT_MEDIUM), by + bh + 2, nums, RX_WHITE, RX_OUTLINE,
                  UI_FONT_MEDIUM);
            ui_fill(c, sx + 6, by + bh + 12, 110, 2, R(0x18283Cu), 255);
            if (exp_num > 0) {
                ui_fill(c, sx + 6, by + bh + 12, 110 * (exp_num > exp_den ? exp_den : exp_num) / exp_den, 2,
                        R(0x50C8F8u), 255);
            }
        }
    } else {
        plate_bar(c, x + 12, x + w, by, bh, -1, 245);
        hp_bar(c, x + 22, by + 2, w - 22 - 14, hp, maxhp);
    }
}

/* The condition badge beside the level. */
static void remix_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge)
{
    (void)level;
    if (badge >= 0) {
        ui_status_badge(c, x + w - 92, y + 2, badge);
    }
}

const UiThemeOps ui_theme_remix = {
    .id = "remix",
    .name = "Remix",
    .color = remix_color,
    .plate = remix_plate,
    .plate_status = remix_plate_status,
    .battle_layout = remix_battle_layout,
    .battle_box = remix_battle_box,
    .battle_command = remix_battle_command,
    .battle_move = remix_battle_move,
    .battle_move_info = remix_battle_move_info,
};
