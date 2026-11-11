/*
 * engine/ui/theme_plasma.c -- Plasma: the one-screen interface in Black and
 * White's own language, laid out for a wide screen (docs/ui-themes.md).
 *
 * The battle is one band across the bottom, as the DS's message band: its
 * text at the left, the four commands (or the four moves) as chips set into
 * it at the right.  The rules, kept everywhere:
 *   - two surfaces, the DS's HP plates' own: the white strip and the dark
 *     band (see-through, with the DS's dot mesh);
 *   - one lean: every slanted edge leans the same way at the same angle
 *     (one pixel across for two down), drawn smooth (anti-aliased);
 *   - the chosen piece turns to the white surface and lifts;
 *   - a piece's colour (a command's, a move's type) sits on its end cap.
 *
 * The plates run in from the screen's edges (the foe's top left, yours above
 * the band at the right) and end in the lean; your party's balls sit on your
 * plate's band, the foe's under its plate.  The chosen move's PP, power and
 * description (scrolling when long) take the band's text place.  An
 * Ability's plate slides in from its Pokemon's edge.
 *
 * The canvas is 640 x 360 at 720p and smaller at other sizes (a whole-number
 * interface scale), so places scale from 640 x 360.
 *
 * The plates, the balls, the Ability's plate and the move's facts are shared
 * with the other Black-and-White-styled themes (engine/ui/theme_bw.h).  A
 * second design of the same trial is built beside this one
 * (engine/ui/theme_harmonia.c); two more are kept as drafts in
 * engine/ui/theme_drafts/ (not built).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_bw.h"
#include "textbox.h"
#include "ui_lang.h"

/* The commands as Scarlet and Violet order them: FIGHT (the game's 0),
 * POKEMON (2), BAG (1), RUN (3). */
static const int k_place_of[4] = { 0, 2, 1, 3 };

/* What the battle shows and for how many frames (the backdrop counts):
 * the chips rise into place when the commands or the moves appear. */
static int g_what, g_age;

/* ---------------------------------------------------------------- */
/* The band and its chips                                            */
/* ---------------------------------------------------------------- */

/* The band: the dark see-through surface (darker toward its foot, the DS's
 * dot mesh), the white strip along its top. */
static void band(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    int yy, xx;

    ui_fill(c, x, y, w, 1, R(RX_PLATE_OL), a);
    ui_fill(c, x, y + 1, w, 2, R(RX_WHITE), a);
    ui_fill(c, x, y + 3, w, 1, R(RX_DARK_RIM), a);
    for (yy = 4; yy < h; yy++) {
        ui_fill(c, x, y + yy, w, 1, R(rx_mix(0x343434u, 0x1A1A1Au, yy - 4, h - 5)), a * 225 / 255);
    }
    for (yy = 7; yy < h - 2; yy += 5) {
        for (xx = 3; xx < w - 3; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(0x3A3A3Au), a * 110 / 255);
        }
    }
}

/*
 * A chip of the band: a leaning box (x its bottom left corner), its left end
 * a cap in `tip`.  Not chosen it is glass over the band (the mesh shows
 * through), a light line along its top; chosen it is the white surface, a
 * step higher, with a shadow.
 */
static void chip(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int sel, int a)
{
    int lean = LEAN(h), cap = sel ? 9 : 7;
    int xl0 = (x + lean) * FP, xl1 = x * FP, xr0 = (x + w + lean) * FP, xr1 = (x + w) * FP;

    if (sel) {
        rx_slab(c, y + 3, h, xl0, xl1, xr0, xr1, 0, h, 0, 0, 0x000000u, 0x000000u, a * 90 / 255);
        rx_slab(c, y, h, xl0, xl1, xr0, xr1, 0, h, 0, 0, RX_OUTLINE, RX_OUTLINE, a);
        rx_slab(c, y, h, xl0, xl1, xr0, xr1, 1, h - 1, INSET, INSET, RX_WHITE, RX_WHITE_LO, a);
    } else {
        rx_slab(c, y, h, xl0, xl1, xr0, xr1, 0, h, 0, 0, 0x000000u, 0x000000u, a * 120 / 255);
        rx_slab(c, y, h, xl0, xl1, xr0, xr1, 1, h - 1, INSET, INSET, 0xFFFFFFu, 0xFFFFFFu, a * 30 / 255);
        rx_slab(c, y, h, xl0, xl1, xr0, xr1, 1, 2, INSET, INSET, 0xFFFFFFu, 0xFFFFFFu, a * 80 / 255);
    }
    /* the cap, and a dark line where it meets the body */
    rx_slab(c, y, h, xl0, xl1, xl0 + cap * FP, xl1 + cap * FP, 1, h - 1, INSET, 0, rx_mix(tip, 0xFFFFFFu, 40, 100), tip,
         a);
    rx_slab(c, y, h, xl0 + cap * FP, xl1 + cap * FP, xl0 + (cap + 1) * FP, xl1 + (cap + 1) * FP, 1, h - 1, 0, 0,
         RX_OUTLINE, RX_OUTLINE, a * (sel ? 255 : 170) / 255);
}

/* ---------------------------------------------------------------- */
/* Battle: places                                                    */
/* ---------------------------------------------------------------- */

#define BAND_H  56
#define CHIP_H  44
#define CHIP_GAP 5
#define CHIP_LIFT 3

static void plasma_battle_layout(UiBattleLayout *l, int what)
{
    int m = AT_LEAST(SX(12), 8), hb = l->h - 17, by = hb - BAND_H, i;
    int ownw = AT_LEAST(SX(220), 190), foew = AT_LEAST(SX(210), 180);
    int text_w = AT_LEAST(SX(190), 142), lean = LEAN(CHIP_H);
    int cw = (l->w - m - lean - text_w - 3 * CHIP_GAP) / 4;

    /* the band on the hint bar: the text's place at its left, the four
     * chips to the right edge.  A chip's place is its box moved half its
     * lean (the click). */
    for (i = 0; i < 4; i++) {
        EidosUiRect r;

        r.x = text_w + k_place_of[i] * (cw + CHIP_GAP) + lean / 2;
        r.y = by + 7;
        r.w = cw;
        r.h = CHIP_H;
        l->cmd[i] = r;
        r.x = text_w + i * (cw + CHIP_GAP) + lean / 2;
        l->move[i] = r;
    }
    l->msg.x = 0;
    l->msg.y = by;
    l->msg.h = BAND_H;
    l->msg.w = what == UI_BATTLE_MESSAGE ? l->w : text_w;
    l->info_x = 10;
    l->info_y = by + 8;
    /* your plates stand on the band at the right, the foe's top left */
    for (i = 0; i < l->n_own && i < 3; i++) {
        l->own[i].x = l->w - m - ownw;
        l->own[i].w = ownw;
        l->own[i].h = OWN_H;
        l->own[i].y = by - 6 - OWN_H - (l->n_own - 1 - i) * (OWN_H + 2);
    }
    /* your balls on your last plate's band, the foe's under its plates */
    l->own_balls_x = l->w - m - ownw + LEAN(BAR_OWN) + 16;
    l->own_balls_y = by - 6 - OWN_H + NAME_H + STRIP_H + 7;
    l->own_balls_right = 0;
    l->foe_x = m;
    l->foe_y = m;
    l->foe_w = foew;
    l->foe_step = FOE_H + 2;
    l->foe_balls_x = m + 6;
    l->foe_balls_y = -1;
}

/* ---------------------------------------------------------------- */
/* Battle: the band                                                  */
/* ---------------------------------------------------------------- */

/* The message's band.  With the commands or the moves up it is under them
 * already (the backdrop), across the screen. */
static void plasma_battle_box(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    if (g_what == UI_BATTLE_MESSAGE) {
        band(c, x, y, w, h, a);
    }
}

static void plasma_battle_backdrop(EidosUiCtx *c, const UiBattleLayout *l, int what)
{
    if (what != g_what) {
        g_what = what;
        g_age = 0;
    } else if (g_age < 100000) {
        g_age++;
    }
    if (what != UI_BATTLE_MESSAGE) {
        band(c, 0, l->msg.y, l->w, l->msg.h, 255);
    }
}

/* The chips' rise into place: their y offset and alpha for the first
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

/* The middle of a chip's face (past its cap). */
static int chip_mid(int x, int w, int h, int sel)
{
    return x + LEAN(h) / 2 + w / 2 + (sel ? 5 : 4);
}

/* The commands' icons stand on all four chips or on none: whether every
 * word of the row before had room for its icon, and the row being drawn. */
static int g_cmd_icons = 1, g_cmd_fit = 1, g_cmd_seen;

static void plasma_battle_command(EidosUiCtx *c, int x, int y, int w, int h, int which, const char *name,
                                 u32 color, int sel, int enabled)
{
    /* the game's own icons: the physical move's badge, the field menu's
     * Bag and Pokemon, the touch bar's return arrow; and the room each
     * takes */
    static const int icon_of[4] = { UI_APP_ICON_PHYSICAL, UI_APP_ICON_BAG, UI_APP_ICON_POKEMON, UI_APP_ICON_BACK };
    static const int icon_w[4] = { 34, 22, 22, 16 };
    int a = rise_a(enabled ? 255 : 120), font = UI_FONT_LARGE, tw = ui_text_w(name, font), cx, gw;

    (void)color;
    which &= 3;
    x -= LEAN(h) / 2;
    y += rise_y() - (sel ? CHIP_LIFT : 0);
    chip(c, x, y, w, h, rx_cmd_color[which], sel, a);
    cx = chip_mid(x, w, h, sel);
    gw = icon_w[which] + 5 + tw;
    if (g_cmd_seen & (1 << which)) {
        /* this chip again: a new row begins */
        g_cmd_icons = g_cmd_fit;
        g_cmd_fit = 1;
        g_cmd_seen = 0;
    }
    g_cmd_seen |= 1 << which;
    if (gw > w - 18) {
        g_cmd_fit = 0;
    }
    if (gw <= w - 18 && g_cmd_icons) {
        rx_game_icon(c, cx - gw / 2 + icon_w[which] / 2, y + h / 2, icon_of[which]);
        rx_label(c, cx - gw / 2 + icon_w[which] + 5, y + ui_text_y(h, font), name, sel, enabled ? RX_TEXT : RX_MUTED,
              font);
    } else {
        rx_label(c, cx - tw / 2, y + ui_text_y(h, font), name, sel, enabled ? RX_TEXT : RX_MUTED, font);
    }
}

static void plasma_battle_move(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name, int pp,
                              int maxpp, int sel, int enabled)
{
    int a = rise_a(enabled ? 255 : 130), cx, font;

    (void)maxpp;
    x -= LEAN(h) / 2;
    y += rise_y();
    if (name == NULL) {
        /* an empty place: the chip, faint, nothing on it */
        chip(c, x, y, w, h, 0x4A4A4Au, 0, a * 110 / 255);
        return;
    }
    y -= sel ? CHIP_LIFT : 0;
    chip(c, x, y, w, h, ui_type_color(type), sel, a);
    cx = chip_mid(x, w, h, sel);
    font = ui_text_w(name, UI_FONT_LARGE) <= w - 20 ? UI_FONT_LARGE : UI_FONT_MEDIUM;
    rx_label(c, cx + 3 - ui_text_w(name, font) / 2, y + 8, name, sel, enabled && pp > 0 ? RX_TEXT : RX_MUTED, font);
    rx_type_word(c, cx - 3 - ui_text_w(ui_type_name(type), UI_FONT_SMALL) / 2, y + h - 18, type, sel);
}

/* The chosen move in the band's text place. */
static void plasma_battle_move_info(EidosUiCtx *c, const UiBattleLayout *l, const UiMoveInfo *m)
{
    rx_move_info(c, l->info_x, l->info_y, l->msg.w - l->info_x - 14, m, g_age);
}

/* ---------------------------------------------------------------- */
/* The field's X menu                                                */
/* ---------------------------------------------------------------- */

#define MENU_BAND_H 50
#define MENU_CHIP_H 38

/* The band along the bottom, as the battle's: the menu's entries are chips
 * set into it, from the left edge to the right. */
static void plasma_field_menu_layout(UiFieldMenuLayout *l)
{
    int m = AT_LEAST(SX(12), 8), lean = LEAN(MENU_CHIP_H), by = l->h - MENU_BAND_H, i;
    int n = l->n > 0 ? l->n : 1, cw = (l->w - 2 * m - lean - (n - 1) * CHIP_GAP) / n;

    for (i = 0; i < l->n; i++) {
        l->btn[i].x = m + i * (cw + CHIP_GAP) + lean / 2;
        l->btn[i].y = by + 7;
        l->btn[i].w = cw;
        l->btn[i].h = MENU_CHIP_H;
    }
    l->party_x = 0;                     /* it runs in from the screen's edge */
    l->party_y = m;
}

static void plasma_field_menu_backdrop(EidosUiCtx *c, const UiFieldMenuLayout *l)
{
    band(c, 0, l->h - MENU_BAND_H, l->w, MENU_BAND_H, 255);
}

static void plasma_field_menu_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                                     const UiImage *icon, int state)
{
    int sel = state != 0, font = UI_FONT_LARGE, tw = ui_text_w(label, font), iw = icon != NULL ? 26 : 0, cx, gw;

    x -= LEAN(h) / 2;
    y -= sel ? CHIP_LIFT : 0;
    chip(c, x, y, w, h, sel ? RX_ACCENT : 0x8A8A8Au, sel, 255);
    cx = chip_mid(x, w, h, sel);
    gw = iw + tw;
    if (gw > w - 14) {
        font = UI_FONT_MEDIUM;
        tw = ui_text_w(label, font);
        gw = iw + tw;
    }
    if (icon != NULL) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + 8, y + 2, w, h - 4);
        ui_image(c, cx - gw / 2 + (22 - icon->w) / 2, y + (h - icon->h) / 2 + 1, icon, 1);
        ui_pop(c, 0, 0, &saved);
    }
    rx_label(c, cx - gw / 2 + iw, y + ui_text_y(h, font), label, sel, RX_TEXT, font);
}

/* ---------------------------------------------------------------- */
/* The title's menu                                                  */
/* ---------------------------------------------------------------- */

/* CONTINUE's card under the logo in the middle; the other entries chips of
 * the band along the bottom, as the X menu's: one row, however many. */
static int g_title_band_h = MENU_BAND_H;
/* The row's one type (set by the layout from the names). */
static int g_title_font = UI_FONT_LARGE;

/* A name's two lines for a plate: broken at the space, or after the hyphen,
 * nearest its middle (b empty: one word). */
static void title_two_lines(const char *label, char *a, char *b, int cap)
{
    int n = (int)strlen(label), i, best = -1;

    for (i = 1; i < n - 1; i++) {
        if ((label[i] == ' ' || label[i] == '-') && (best < 0 || abs(2 * i - n) < abs(2 * best - n))) {
            best = i;
        }
    }
    b[0] = '\0';
    if (best < 0) {
        snprintf(a, (size_t)cap, "%s", label);
        return;
    }
    snprintf(a, (size_t)cap, "%.*s", best + (label[best] == '-' ? 1 : 0), label);
    snprintf(b, (size_t)cap, "%s", label + best + 1);
}

/* The width a name needs in a type: its one line, or the wider of its two. */
static int title_need(const char *label, int font, int two)
{
    char a[48], b[48];
    int wa, wb;

    if (!two) {
        return ui_text_w(label, font);
    }
    title_two_lines(label, a, b, (int)sizeof a);
    if (b[0] == '\0') {
        return ui_text_w(label, font);
    }
    wa = ui_text_w(a, font);
    wb = ui_text_w(b, font);
    return wa > wb ? wa : wb;
}

static void plasma_title_menu_layout(UiTitleMenuLayout *l, int phase)
{
    static const int k_fonts[3] = { UI_FONT_LARGE, UI_FONT_MEDIUM, UI_FONT_SMALL };
    int m = AT_LEAST(SX(12), 8), lean = LEAN(MENU_CHIP_H), i;
    int n = l->n_rows, cw, total, x0, avail, cardw, cardh, block, top, lh, by;
    int wide[8], room, f, pad = 20, x;

    if (n < 1 || n > 8) {
        return;                         /* too many for the band: Kosmos's */
    }
    if (phase == 0) {
        l->across = n;
        return;
    }
    g_title_band_h = 12 + MENU_CHIP_H;
    by = l->h - g_title_band_h;
    room = l->w - 2 * m - lean - (n - 1) * CHIP_GAP;      /* for the n plates */
    cw = room / n;
    cw = cw > 168 ? 168 : cw;
    for (i = 0; i < n; i++) {
        wide[i] = cw;
    }
    g_title_font = UI_FONT_LARGE;
    if (l->label[0] != NULL) {
        /* from the large type down: equal plates if every name fits one,
         * else plates as wide as their names need if the row fits the
         * screen; the next type only when neither does */
        int done = 0;

        pad = 20;       /* 10 px clear each side of a name: the plate's leaning rule takes 5 of its left */
        for (f = 0; f < 3 && !done; f++) {
            int need[8], sum = 0, equal = 1;

            for (i = 0; i < n; i++) {
                need[i] = l->label[i] != NULL ? title_need(ui_tr(l->label[i]), k_fonts[f], 1) : 0;
                if (need[i] > cw - pad) {
                    equal = 0;
                }
                sum += need[i] + pad < 40 ? 40 : need[i] + pad;
            }
            if (!equal && f == 2 && sum > room && pad > 10) {
                pad -= pad > 14 ? 6 : 4;        /* the small type: less clear space before an overflow */
                f--;
                continue;
            }
            if (equal) {
                done = 1;
            } else if (sum <= room || f == 2) {
                int spare = sum < room ? (room - sum) / n : 0;

                for (i = 0; i < n; i++) {
                    wide[i] = (need[i] + pad < 40 ? 40 : need[i] + pad) + spare;
                }
                done = 1;
            }
            if (done) {
                g_title_font = k_fonts[f];
            }
        }
    }
    total = lean + (n - 1) * CHIP_GAP;
    for (i = 0; i < n; i++) {
        total += wide[i];
    }
    x0 = (l->w - total) / 2;
    x = x0 + lean / 2;
    for (i = 0; i < n; i++) {
        l->row[i].x = x;
        l->row[i].y = by + 7;
        l->row[i].w = wide[i];
        l->row[i].h = MENU_CHIP_H;
        x += wide[i] + CHIP_GAP;
    }
    avail = by - 12;
    cardw = l->w * 50 / 100;
    cardw = cardw < 260 ? 260 : cardw > 340 ? 340 : cardw;
    lh = l->logo_h;
    if (!l->has_card) {
        l->logo.w = lh > 0 && lh <= avail ? l->logo_w : 0;
        l->logo.h = lh;
        l->logo.x = (l->w - l->logo_w) / 2;
        l->logo.y = 6 + (avail - lh) / 2;
        return;
    }
    cardh = avail - lh - 8;
    if (lh <= 0 || cardh < 100) {
        lh = 0;
        cardh = avail - 8;
    }
    cardh = cardh < 100 ? 100 : cardh > 150 ? 150 : cardh;
    block = (lh > 0 ? lh + 8 : 0) + cardh;
    top = 6 + (avail - block) / 2;
    l->logo.w = lh > 0 ? l->logo_w : 0;
    l->logo.h = lh;
    l->logo.x = (l->w - l->logo_w) / 2;
    l->logo.y = top;
    l->card.x = (l->w - cardw) / 2;
    l->card.y = top + (lh > 0 ? lh + 8 : 0);
    l->card.w = cardw;
    l->card.h = cardh;
}

static void plasma_title_menu_backdrop(EidosUiCtx *c, const UiTitleMenuLayout *l)
{
    rx_cool(1);
    if (l->across) {
        band(c, 0, l->h - g_title_band_h, l->w, g_title_band_h, 255);
    }
}

static void plasma_title_menu_row(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int selected,
                                  int hover)
{
    int sel = selected || hover, font = g_title_font, cx;
    const char *name = ui_tr(label);

    if (h != MENU_CHIP_H) {
        /* Kosmos's places (too many entries for the band): a list's row */
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, sel ? UI_CYAN2 : UI_BODY, UI_EDGE, 240);
        rx_label(c, x + 17, y + ui_text_y(h, UI_FONT_LARGE), label, sel, RX_TEXT, UI_FONT_LARGE);
        return;
    }
    x -= LEAN(h) / 2;
    y -= selected ? CHIP_LIFT : 0;
    chip(c, x, y, w, h, selected ? RX_PLASMA_BLUE : 0x8A8A8Au, sel, 255);
    cx = chip_mid(x, w, h, sel);
    if (ui_text_w(name, font) <= w - 12) {
        rx_label(c, cx - ui_text_w(name, font) / 2, y + ui_text_y(h, font), name, sel, RX_TEXT, font);
    } else {
        /* two lines, each at the leaning plate's middle at its own height
         * (the plate leans one pixel across for two down) */
        char a[48], b[48];
        int pitch = font == UI_FONT_LARGE ? 13 : 11, ty = y + ui_text_y(h, font);

        title_two_lines(name, a, b, (int)sizeof a);
        rx_label(c, cx + pitch / 4 - ui_text_w(a, font) / 2, ty - pitch / 2, a, sel, RX_TEXT, font);
        rx_label(c, cx - pitch / 4 - ui_text_w(b, font) / 2, ty - pitch / 2 + pitch, b, sel, RX_TEXT, font);
    }
}

/* [party] */
/* ---------------------------------------------------------------- */
/* The party screen                                                  */
/* ---------------------------------------------------------------- */

#define PARTY_TOP 28

/* A screen's ground: dark glass in the screen's own hue, the lean drawn
 * large across it (a lighter field from `at` percent of the width, two thin
 * stripes of the hue before it), the dot mesh over all. */
static void plasma_ground(EidosUiCtx *c, int w, int h, int at, u32 hue)
{
    int x, y, sx = w * at / 100, l = LEAN(h);
    u32 top = rx_mix(0x0C0C0Cu, hue, 20, 100), bot = rx_mix(0x1C1C1Cu, hue, 42, 100);

    for (y = 0; y < h; y++) {
        ui_fill(c, 0, y, w, 1, R(rx_mix(top, bot, y, h - 1)), 236);
    }
    rx_slab(c, 0, h, (sx + l) * FP, sx * FP, w * FP, w * FP, 0, h, 0, 0, rx_mix(hue, 0xFFFFFFu, 30, 100),
            rx_mix(hue, 0xFFFFFFu, 30, 100), 26);
    rx_slab(c, 0, h, (sx - 14 + l) * FP, (sx - 14) * FP, (sx - 8 + l) * FP, (sx - 8) * FP, 0, h, 0, 0, hue, hue, 150);
    rx_slab(c, 0, h, (sx - 24 + l) * FP, (sx - 24) * FP, (sx - 22 + l) * FP, (sx - 22) * FP, 0, h, 0, 0, hue, hue, 110);
    for (y = 6; y < h - 2; y += 5) {
        for (x = 3; x < w - 3; x += 5) {
            ui_fill(c, x, y, 2, 2, R(rx_mix(0x3A3A3Au, hue, 25, 100)), 70);
        }
    }
}


/* The party as a leaning column of plates at the left, the chosen Pokemon
 * standing at the right over the battle's own HP plate, and the band along
 * the bottom: the prompt at its left, the menu's entries its chips. */
static void plasma_party_layout(UiPartyLayout *l)
{
    int m = AT_LEAST(SX(12), 8), by = l->h - MENU_BAND_H, i;
    int avail = by - PARTY_TOP - 8, ph = (avail - 5 * 3) / 6, pw = l->w * 42 / 100;
    int text_w = AT_LEAST(SX(200), 150);

    ph = ph < 24 ? 24 : ph > 36 ? 36 : ph;
    l->frame.x = 0;
    l->frame.y = 0;
    l->frame.w = l->w;
    l->frame.h = l->h;
    l->header = 0;
    l->column = 1;
    l->no_exit = 1;
    for (i = 0; i < 6; i++) {
        l->slot[i].x = m + (5 - i) * (ph + 3) / 2;
        l->slot[i].y = PARTY_TOP + 4 + i * (ph + 3);
        l->slot[i].w = pw;
        l->slot[i].h = ph;
    }
    l->detail.x = m + pw + 5 * (ph + 3) / 2 + LEAN(ph) + 10;
    l->detail.y = PARTY_TOP + 2;
    l->detail.w = l->w - m - l->detail.x;
    l->detail.h = by - 6 - l->detail.y;
    l->msg.x = 10;
    l->msg.y = by + 8;
    l->msg.w = text_w;
    l->msg.h = MENU_BAND_H - 12;
    l->menu_apart = 1;
    if (l->menu_n > 0 && l->menu_n <= 5) {
        int cw = (l->w - m - text_w - 16 - LEAN(MENU_CHIP_H)) / l->menu_n;

        cw = cw > 120 ? 120 : cw;
        l->menu_across = 1;
        l->menu.w = cw * l->menu_n;
        l->menu.x = l->w - m - LEAN(MENU_CHIP_H) / 2 - l->menu.w;
        l->menu.y = by + 7;
        l->menu.h = MENU_CHIP_H;
        l->msg.w = l->menu.x - 16;
    } else {
        l->menu.w = 136;
        l->menu.x = l->w - m - l->menu.w;
        l->menu.y = PARTY_TOP + 20;
        l->menu.h = by - 8 - l->menu.y;
    }
}

static void plasma_party_backdrop(EidosUiCtx *c, const UiPartyLayout *l, const char *title, const char *sub,
                                  const char *right)
{
    int by = l->h - MENU_BAND_H;

    plasma_ground(c, l->w, by, 46, 0x1478D0u);
    ui_header(c, 0, 3, l->w, title, sub, right, UI_SKY);
    band(c, 0, by, l->w, MENU_BAND_H, 255);
}

/* The held-item mark in Plasma: the game's jar with its body in Plasma's
 * gold (the levels'), its own black edge (kit.h, ui_held_icon). */
static const u32 k_plasma_held[4] = { RX_OUTLINE, 0xDCDEE8u, 0xF8BC28u, 0xC88C10u };

static void plasma_party_plate(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m)
{
    int x = r->x, y = r->y, w = r->w, h = r->h, sel = m->sel, lean = LEAN(h), nx, tw, fainted;
    char s[48];

    if (m->name == NULL) {
        rx_slab(c, y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 0, h, 0, 0, 0x000000u, 0x000000u,
                70);
        return;
    }
    fainted = m->level > 0 && m->hp == 0;
    x += sel ? 8 : 0;
    chip(c, x, y, w, h, fainted ? 0xC02010u : sel ? UI_SKY : 0x8A8A8Au, sel, 255);
    if (m->icon != NULL) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x, y - 6, w, h + 6);
        ui_image(c, x + lean / 2 + 12 + (30 - m->icon->w) / 2, y + (h - m->icon->h) / 2 - 3, m->icon, 1);
        ui_pop(c, 0, 0, &saved);
    }
    /* the game's mark for what it holds, on the feet of the icon where the
     * game has it (it was a plain dot at the start of the HP bar, read as
     * the bar's end): the jar's body in Plasma's gold (its levels'), its own
     * black edge */
    ui_held_mark_swap(c, x + lean / 2 + 11, y + (h - 32) / 2 - 3, m->item, k_plasma_held);
    nx = rx_label(c, x + lean / 2 + 48, y + 2, m->name, sel, RX_TEXT, UI_FONT_LARGE);
    ui_gender(c, nx + 3, y + 2, m->gender, UI_FONT_LARGE);
    if (m->level <= 0) {
        return;
    }
    snprintf(s, sizeof(s), "%s%d", ui_tr("Lv."), m->level);
    tw = ui_text_w(s, UI_FONT_SMALL);
    rx_label(c, x + w + lean / 2 - 8 - tw, y + 4, s, sel, sel ? RX_INK : RX_ACCENT, UI_FONT_SMALL);
    if (m->status >= 0) {
        ui_status_badge(c, x + w + lean / 2 - 8 - tw - 30, y + 3, m->status);
    }
    if (m->moving) {
        rx_label(c, x + w + lean / 2 - 8 - tw - 34, y + 4, "MOVE", sel, sel ? 0x0878C0u : UI_SKY, UI_FONT_SMALL);
    }
    if (m->note != NULL) {
        ui_text(c, x + 48, y + h - 12, m->note, sel ? R(rx_mix(m->note_rgb, 0x000000u, 40, 100)) : R(m->note_rgb),
                sel ? -1 : (s32)R(0x0C0C0Cu), UI_FONT_SMALL, 1);
        return;
    }
    snprintf(s, sizeof(s), "%d/%d", m->hp, m->maxhp);
    tw = ui_text_w(s, UI_FONT_SMALL);
    ui_bar(c, x + 48, y + h - 9, w - 48 - tw - 14, 5, m->hp, m->maxhp);
    rx_label(c, x + w - 8 - tw, y + h - 12, s, sel, RX_TEXT, UI_FONT_SMALL);
}

/* The chosen Pokemon: its picture large, the battle's own HP plate under it
 * and its types. */
static void plasma_party_detail(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m, u32 frame)
{
    int x = r->x, y = r->y, w = r->w, h = r->h, k = 1, pw = 96, phh = 96, py, plw;

    if (m == NULL || m->name == NULL) {
        return;
    }
    if (m->front != NULL) {
        pw = m->front->w;
        phh = m->front->h;
        k = (w - 8) / pw < (h - OWN_H - 24) / phh ? (w - 8) / pw : (h - OWN_H - 24) / phh;
        k = k < 1 ? 1 : k > 3 ? 3 : k;
    }
    py = y + ((h - OWN_H - 22) - phh * k) / 2;
    py = py < y ? y : py;
    /* a stand: the white strip's lean under its feet */
    rx_slab(c, py + phh * k - 10, 8, (x + w / 2 - 30 * k) * FP, (x + w / 2 - 30 * k - 4) * FP,
            (x + w / 2 + 30 * k + 4) * FP, (x + w / 2 + 30 * k) * FP, 0, 8, 0, 0, 0xFFFFFFu, 0xFFFFFFu, 40);
    if (m->front != NULL) {
        ui_mon_front_draw(c, (u16)m->species, 0, frame, x + (w - pw * k) / 2, py, pw * k, phh * k);
    } else if (m->icon != NULL) {
        ui_image(c, x + (w - m->icon->w * 2) / 2, py + 16, m->icon, 2);
    }
    plw = w > 230 ? 230 : w;
    if (m->level > 0) {
        rx_plate(c, x + w - plw, y + h - OWN_H - 18, plw, m->name, m->gender, m->level, m->hp, m->maxhp, 0, 1);
        ui_type_tag(c, x + w - plw + 14, y + h - 14, 46, m->type1);
        if (m->type2 != m->type1) {
            ui_type_tag(c, x + w - plw + 14 + 50, y + h - 14, 46, m->type2);
        }
    } else {
        rx_otext(c, x + (w - ui_text_w(m->name, UI_FONT_LARGE)) / 2, y + h - 30, m->name, RX_TEXT, RX_PLATE_OL,
                 UI_FONT_LARGE);
    }
}

static void plasma_party_prompt(EidosUiCtx *c, const EidosUiRect *r, const char *text)
{
    rx_label(c, r->x, r->y + ui_text_y(r->h, UI_FONT_LARGE), text, 0, RX_TEXT, UI_FONT_LARGE);
}

static void plasma_party_menu_entry(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int sel,
                                    int across)
{
    int font = UI_FONT_LARGE, cx;

    if (!across) {
        chip(c, x - LEAN(h) / 2, y, w, h, sel ? RX_ACCENT : 0x8A8A8Au, sel, 255);
        rx_label(c, x + 14, y + ui_text_y(h, font), label, sel, RX_TEXT, font);
        return;
    }
    x -= LEAN(h) / 2;
    y -= sel ? CHIP_LIFT : 0;
    chip(c, x, y, w, h, sel ? RX_ACCENT : 0x8A8A8Au, sel, 255);
    cx = chip_mid(x, w, h, sel);
    if (ui_text_w(label, font) > w - 20) {
        font = UI_FONT_MEDIUM;
    }
    if (ui_text_w(label, font) > w - 20) {
        font = UI_FONT_SMALL;
    }
    rx_label(c, cx - ui_text_w(label, font) / 2, y + ui_text_y(h, font), label, sel, RX_TEXT, font);
}


/* A Pokemon's row in a list (the X menu's party, the battle's): the party
 * screen's leaning plate, small. */
static void plasma_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level,
                             int hp, int maxhp, const UiImage *icon, int selected)
{
    const int h = 30;
    int lean = LEAN(h), nx, tw, fainted = level > 0 && hp == 0;
    char s[48];

    (void)slot;
    w -= lean;
    if (name == NULL) {
        rx_slab(c, y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 0, h, 0, 0, 0x000000u, 0x000000u,
                70);
        return;
    }
    chip(c, x, y, w, h, fainted ? 0xC02010u : selected ? UI_SKY : 0x8A8A8Au, selected, 255);
    if (icon != NULL) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x, y - 6, w, h + 6);
        ui_image(c, x + lean / 2 + 10 + (28 - icon->w) / 2, y + (h - icon->h) / 2 - 2, icon, 1);
        ui_pop(c, 0, 0, &saved);
        /* what it holds, on the icon's feet (ui_party_row_held) */
        ui_held_mark_swap(c, x + lean / 2 + 10 + (28 - icon->w) / 2, y + (h - icon->h) / 2 - 2, ui_party_row_take_held(),
                          k_plasma_held);
    }
    {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + lean / 2 + 42, y, w - 42 - 34, h);
        nx = rx_label(c, x + lean / 2 + 42, y + 2, name, selected, RX_TEXT, UI_FONT_LARGE);
        ui_gender(c, nx + 3, y + 2, gender, UI_FONT_LARGE);
        ui_pop(c, 0, 0, &saved);
    }
    if (level <= 0) {
        return;
    }
    snprintf(s, sizeof(s), "%s%d", ui_tr("Lv."), level);
    tw = ui_text_w(s, UI_FONT_SMALL);
    rx_label(c, x + w + lean / 2 - 8 - tw, y + 4, s, selected, selected ? RX_INK : RX_ACCENT, UI_FONT_SMALL);
    snprintf(s, sizeof(s), "%d/%d", hp, maxhp);
    tw = ui_text_w(s, UI_FONT_SMALL);
    ui_bar(c, x + 42, y + h - 9, w - 42 - tw - 12, 5, hp, maxhp);
    rx_label(c, x + w - 6 - tw, y + h - 12, s, selected, RX_TEXT, UI_FONT_SMALL);
}
/* [/party] */
/* [bag] */
/* ---------------------------------------------------------------- */
/* The Bag                                                           */
/* ---------------------------------------------------------------- */

/* The pockets are the band's chips; the list a column of leaning rows at
 * the left; the chosen item stands large at the right, its words under it. */
static void plasma_bag_layout(UiBagLayout *l)
{
    int m = AT_LEAST(SX(12), 8), by = l->h - MENU_BAND_H, lean = LEAN(MENU_CHIP_H), i;
    int cw = (l->w - 2 * m - lean - 4 * CHIP_GAP) / 5, lw = l->w * 48 / 100;

    l->frame.x = 0;
    l->frame.y = 0;
    l->frame.w = l->w;
    l->frame.h = l->h;
    for (i = 0; i < 5; i++) {
        l->pocket[i].x = m + i * (cw + CHIP_GAP) + lean / 2;
        l->pocket[i].y = by + 7;
        l->pocket[i].w = cw;
        l->pocket[i].h = MENU_CHIP_H;
    }
    l->list.x = m;
    l->list.y = PARTY_TOP + 6;
    l->list.w = lw;
    l->list.h = by - 8 - l->list.y;
    l->rowh = (l->list.h - 5 * 2) / 6;
    l->rowh = l->rowh < 22 ? 22 : l->rowh > 32 ? 32 : l->rowh;
    l->detail.x = m + lw + 26;
    l->detail.y = PARTY_TOP + 6;
    l->detail.w = l->w - m - l->detail.x;
    l->detail.h = by - 8 - l->detail.y;
}

static void plasma_bag_backdrop(EidosUiCtx *c, const UiBagLayout *l, const char *pocket)
{
    int by = l->h - MENU_BAND_H;

    plasma_ground(c, l->w, by, 52, 0xC82850u);
    ui_header(c, 0, 3, l->w, "BAG", pocket, "", UI_PINK);
    band(c, 0, by, l->w, MENU_BAND_H, 255);
}

static void plasma_bag_pocket(EidosUiCtx *c, const EidosUiRect *r, int which, const char *name, int count, int sel,
                              int hover)
{
    int x = r->x - LEAN(r->h) / 2, y = r->y - (sel ? CHIP_LIFT : 0), font = UI_FONT_LARGE, cx;
    char s[12];

    (void)which;
    chip(c, x, y, r->w, r->h, sel ? UI_PINK : hover ? 0xC8C8C8u : 0x8A8A8Au, sel, 255);
    cx = chip_mid(x, r->w, r->h, sel);
    if (ui_text_w(name, font) > r->w - 24) {
        font = UI_FONT_SMALL;
    }
    rx_label(c, cx - ui_text_w(name, font) / 2, y + ui_text_y(r->h, font) - (count > 0 && !sel ? 4 : 0), name, sel,
             RX_TEXT, font);
    if (count > 0 && !sel) {
        snprintf(s, sizeof(s), "%d", count);
        rx_label(c, cx - ui_text_w(s, UI_FONT_SMALL) / 2, y + r->h - 13, s, 0, RX_MUTED, UI_FONT_SMALL);
    }
}

static void plasma_bag_row(EidosUiCtx *c, int x, int y, int w, int h, const UiImage *icon, const char *name,
                           int count, int sel, int hover)
{
    int lean = LEAN(h), ty = ui_text_y(h, UI_FONT_LARGE), tx = lean / 2 + 16, on = sel || hover;
    EidosUiRect saved;
    char s[16];

    w -= lean;
    if (name == NULL) {
        rx_slab(c, y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 0, h, 0, 0, 0x000000u, 0x000000u,
                60);
        return;
    }
    x += sel ? 6 : 0;
    chip(c, x, y, w, h, sel ? UI_PINK : 0x8A8A8Au, on, 255);
    if (icon != NULL) {
        ui_image(c, x + tx, y + (h - icon->h) / 2, icon, 1);
        tx += icon->w + 4;
    }
    s[0] = '\0';
    if (count > 0) {
        snprintf(s, sizeof(s), "\xC3\x97%d", count);
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x + tx, y, w - tx - 10 - ui_text_w(s, UI_FONT_LARGE), h);
    rx_label(c, x + tx, y + ty, name, on, RX_TEXT, UI_FONT_LARGE);
    ui_pop(c, 0, 0, &saved);
    if (count > 0) {
        rx_label(c, x + w + lean / 2 - 8 - ui_text_w(s, UI_FONT_LARGE), y + ty, s, on, RX_TEXT, UI_FONT_LARGE);
    }
}

/* The item's pane: no box; a leaning stand under the picture, a white rule
 * between the name and the words. */
static void plasma_bag_detail_frame(EidosUiCtx *c, const EidosUiRect *r, int shown)
{
    int cx = r->x + r->w / 2, fh = 64, t = LEAN(fh) / 2, x0 = r->x + 6, x1 = r->x + r->w - 6;

    /* the field: across the pane's head, leaning, the pink fading down */
    rx_slab(c, r->y + 2, fh, (x0 + 2 * t) * FP, x0 * FP, x1 * FP, (x1 - 2 * t) * FP, 0, fh, 0, 0, 0xF04878u, 0x802040u,
            shown ? 150 : 70);
    rx_slab(c, r->y + 2, fh, (x0 + 2 * t) * FP, x0 * FP, (x0 + 2 * t + 2) * FP, (x0 + 2) * FP, 0, fh, 0, 0, RX_WHITE,
            RX_WHITE, 255);
    rx_slab(c, r->y + 2, fh, (x1 - 2) * FP, (x1 - 2 * t - 2) * FP, x1 * FP, (x1 - 2 * t) * FP, 0, fh, 0, 0, RX_WHITE,
            RX_WHITE, 255);
    if (!shown) {
        return;
    }
    ui_ellipse(c, cx - 26, r->y + 46, 52, 10, R(0x000000u), UI_NONE, 80);
    ui_fill(c, r->x + 8, r->y + 74, r->w - 16, 1, R(RX_OUTLINE), 255);
    ui_fill(c, r->x + 8, r->y + 75, r->w - 16, 2, R(RX_WHITE), 255);
}
/* [/bag] */
/* [app] */
/* ---------------------------------------------------------------- */
/* A screen's ground and window                                      */
/* ---------------------------------------------------------------- */

/* A box with its top left and bottom right corners cut (the theme's cut),
 * smooth, in an ink line. */
static void dex_cutbox(EidosUiCtx *c, int x, int y, int w, int h, int cut, u32 top, u32 bot, u32 ink)
{
    int i;

    for (i = 0; i < 2; i++) {
        int in = i * 2, X = x + in, Y = y + in, W = w - 2 * in, Hh = h - 2 * in, C = cut - in;
        u32 a = i ? top : ink, b = i ? bot : ink, m1, m2;

        C = C < 1 ? 1 : C;
        m1 = rx_mix(a, b, C, Hh - 1);
        m2 = rx_mix(a, b, Hh - C, Hh - 1);
        rx_slab(c, Y, C, (X + C) * FP, X * FP, (X + W) * FP, (X + W) * FP, 0, C, 0, 0, a, m1, 255);
        rx_slab(c, Y + C, Hh - 2 * C, X * FP, X * FP, (X + W) * FP, (X + W) * FP, 0, Hh - 2 * C, 0, 0, m1, m2, 255);
        rx_slab(c, Y + Hh - C, C, X * FP, X * FP, (X + W) * FP, (X + W - C) * FP, 0, C, 0, 0, m2, b, 255);
    }
}

/* A screen of the device: dark glass with a leaning glint when it is off,
 * the screen's wine ground coming on (lit 0..256), a line in the hue. */
static void dex_glass(EidosUiCtx *c, const EidosUiRect *r, int lit, u32 line)
{
    int y, a = lit >= 256 ? 255 : lit * 255 / 256, gx = r->x + r->w * 16 / 100, gw = r->w * 14 / 100;

    ui_fill(c, r->x - 4, r->y - 4, r->w + 8, r->h + 8, R(0x101014u), 255);
    if (a < 255) {
        for (y = 0; y < r->h; y++) {
            ui_fill(c, r->x, r->y + y, r->w, 1, R(rx_mix(0x2A2A32u, 0x121216u, y, r->h - 1)), 255);
        }
        rx_slab(c, r->y, r->h, (gx + r->h / 4) * FP, gx * FP, (gx + gw + r->h / 4) * FP, (gx + gw) * FP, 0, r->h, 0, 0,
                0xFFFFFFu, 0xFFFFFFu, 22);
    }
    if (a > 0) {
        for (y = 0; y < r->h; y++) {
            ui_fill(c, r->x, r->y + y, r->w, 1, R(rx_mix(0x1C1014u, 0x3A1820u, y, r->h - 1)), a);
        }
        for (y = r->y + 3; y < r->y + r->h; y += 4) {
            ui_fill(c, r->x, y, r->w, 1, R(0x000000u), a * 36 / 255);
        }
        ui_fill(c, r->x - 1, r->y - 1, r->w + 2, 1, R(line), a);
        ui_fill(c, r->x - 1, r->y + r->h, r->w + 2, 1, R(line), a);
        ui_fill(c, r->x - 1, r->y, 1, r->h, R(line), a);
        ui_fill(c, r->x + r->w, r->y, 1, r->h, R(line), a);
    }
}

/* The Pokedex as the device, the theme's take: the silver body with the
 * pink ball, its corners cut, the white strip along the upper half. */
static void plasma_dex(EidosUiCtx *c, int open)
{
    const u32 ink = 0x16161Au, top = 0xDCDCE2u, bot = 0x9C9CA8u, hue = 0xF04860u;
    RxDex g;

    rx_dex_geom(c, open, &g);
    if (g.slide > 0) {
        int x = g.s2.x - 14, w = g.l_end - x, y = g.top + 3, h = g.hh - 6, tx = g.s2.x + g.sw * 62 / 100;

        ui_fill(c, tx, y + h - 2, 26, 5, R(ink), 255);
        ui_fill(c, tx + 1, y + h - 1, 24, 3, R(0x30B868u), 255);
        dex_cutbox(c, x, y, w, h, 12, rx_mix(top, 0x000000u, 14, 100), rx_mix(bot, 0x000000u, 14, 100), ink);
        dex_glass(c, &g.s2, g.lit, hue);
    }
    dex_cutbox(c, g.x0, g.top, g.u_end - g.x0, g.hh, 14, top, bot, ink);
    ui_fill(c, g.x0 + 16, g.top + 3, g.u_end - g.x0 - 22, 2, R(0xFFFFFFu), 255);
    if (g.slide > 0) {
        ui_fill(c, g.u_end, g.top + 9, 3, g.hh - 18, R(0x000000u), 80);
    }
    rx_dex_lid(c, &g, 0xE8507Au, top, ink, 0x1C1014u, 0x3A1820u, 0xF04860u, 0, 14, 0);
    dex_glass(c, &g.s1, g.lit, hue);
    rx_head_pad(2);     /* the screen's line runs along a title's left end: the cap keeps 2 px from it */
}

/* The Trainer Card's stage colours: green, purple, red, silver, gold, black. */
static const u32 k_card_stage[6] = { 0x58C058u, 0xA878E0u, 0xE85858u, 0xC8CCD8u, 0xF0C028u, 0x40404Cu };

static int card_stage(const char *app)
{
    return strncmp(app, "card", 4) == 0 && app[4] >= '0' && app[4] <= '5' && app[5] == '\0' ? app[4] - '0' : -1;
}

/* A screen's own hue. */
static u32 app_hue(const char *app)
{
    static const struct {
        const char *app;
        u32 hue;
    } k[] = {
        { "summary", 0x1478D0u }, { "relearn", 0x1478D0u }, { "pc", 0x1890C8u },  { "dex", 0xD02840u },    { "card", 0x18B078u },
        { "shop", 0x2060D0u },    { "save", 0x20A0E0u }, { "options", 0x8A6AD8u }, { "settings", 0x8A6AD8u },
        { "namein", 0x18A8A0u },  { "xtrans", 0x18A8A0u }, { "mystery", 0xD86090u }, { "battle-bag", 0xC82850u },
        { "battle-party", 0x1478D0u }, { "battle-forget", 0x1478D0u }, { "wireless", 0xE08020u }, { "entree", 0x30A040u }, { "survey", 0x30A040u },
    };
    unsigned i;

    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (strcmp(app, k[i].app) == 0) {
            return k[i].hue;
        }
    }
    return 0x707070u;
}

/*
 * No screen stands in a window over the world: each is on the glass.  The
 * summary takes the whole screen; the others keep their place on it, a
 * darker leaning field behind them between two white rules.
 */
/* The feet of the top bar's buttons' rules (left, right; canvas pixels at
 * the bar's foot), as "bar-button" last drew them: a sheet that is a button
 * carried on starts exactly there. */
static int g_bb_l[8], g_bb_r[8], g_bb_at;

static int plasma_app_frame(EidosUiCtx *c, const char *app, EidosUiRect *frame)
{
    int w = c->canvas->w, h = c->canvas->h - ui_bar_h(c), l = LEAN(h) / 2;
    int x0 = frame->x - 12, x1 = frame->x + frame->w + 12;

    if (strcmp(app, "press-start") == 0) {
        /* a thin band across the screen between two white rules */
        ui_fill(c, 0, frame->y, w, frame->h, R(0x101010u), 200);
        ui_fill(c, 0, frame->y, w, 1, R(RX_WHITE), 255);
        ui_fill(c, 0, frame->y + frame->h - 1, w, 1, R(RX_WHITE), 255);
        return 1;
    }
    if (strcmp(app, "card-row") == 0) {
        return 0;
    }
    if (card_stage(app) >= 0) {
        /* the glass card in its stage's colour: the colour along its top and
         * in its accents, a pale ring across it */
        u32 col = k_card_stage[card_stage(app)];
        int cx = frame->x + frame->w * 70 / 100, cy = frame->y + frame->h / 2, r = frame->h * 60 / 100;
        EidosUiRect saved;

        rx_accent(col);
        ui_panel(c, frame->x, frame->y, frame->w, frame->h, UI_CUT_TL | UI_CUT_BR, 10, UI_BODY, UI_EDGE, 244);
        ui_push(c, 0, 0, &saved);
        ui_clip(c, frame->x + 3, frame->y + 3, frame->w - 6, frame->h - 6);
        ui_ellipse(c, cx - r, cy - r, 2 * r, 2 * r, UI_NONE, R(col), 70);
        ui_ellipse(c, cx - r + 6, cy - r + 6, 2 * r - 12, 2 * r - 12, UI_NONE, R(col), 40);
        ui_pop(c, 0, 0, &saved);
        ui_fill(c, frame->x + 12, frame->y + frame->h - 3, frame->w - 24, 2, R(col), 255);
        return 1;
    }
    if (strcmp(app, "card-case") == 0) {
        return 0;
    }
    if (strcmp(app, "card-arr") == 0) {
        return 1;
    }
    if (strcmp(app, "options-arr") == 0) {
        frame->w = 2;
        return 1;
    }
    if (strcmp(app, "dex-flip") == 0 || strcmp(app, "dex-size") == 0) {
        return 0;                       /* the device has its own two screens */
    }
    if (strcmp(app, "item-card") == 0) {
        /* an item obtained: a leaning dark plate, the white strip along its
         * top, a gold cap on its left end; white letters (the answer 0) */
        int h = frame->h, lean = LEAN(h), x = frame->x - lean / 2, w = frame->w + 4;

        rx_slab(c, frame->y + 2, h, (x + lean + 2) * FP, (x + 2) * FP, (x + w + lean + 2) * FP, (x + w + 2) * FP, 0, h, 0, 0,
                0x000000u, 0x000000u, 90);
        rx_slab(c, frame->y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 0, h, 0, 0, RX_OUTLINE,
                RX_OUTLINE, 255);
        rx_slab(c, frame->y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 1, h - 1, INSET, INSET,
                0x383838u, 0x1A1A1Au, 255);
        rx_slab(c, frame->y, h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 1, 3, INSET, INSET, RX_WHITE,
                RX_WHITE, 255);
        rx_slab(c, frame->y, h, (x + lean) * FP, x * FP, (x + lean + 6) * FP, (x + 6) * FP, 1, h - 1, INSET, 0,
                0xFFD060u, RX_ACCENT, 255);
        frame->h = 0;
        return 1;
    }
    if (strcmp(app, "item-well") == 0) {
        return 1;                       /* the picture stands on the plate */
    }
    if (strncmp(app, "starter-", 8) == 0 && strcmp(app, "starter-pic") != 0) {
        /* The DS's stage for the chosen Pokemon, remade with the theme's
         * means: the ground is a leaning wall of the Pokemon's colour down
         * the left edge behind a white rule, the DS's beam a leaning shaft
         * of light in it; the caption a white strip that passes the wall's
         * edge; the question in the message band, its answers in the band. */
        static int foot, xf, type;
        int W = w / 2 - 132 < 104 ? 104 : w / 2 - 132 > 190 ? 190 : w / 2 - 132;  /* to the ball's glow */
        int bandh = 30, bandy = h - bandh;     /* the band stands on the key bar */
#define WALL_X(yy) (xf + (foot - (yy)) / 4)     /* the wall's edge at a row: half the theme's lean */

        if (strcmp(app, "starter-stage") == 0) {
            u32 rgb = (u32)frame->x & 0xFFFFFFu;
            int cx, pw = frame->w > 0 ? frame->w : 64, ph = frame->h > 0 ? frame->h : 64, k = 4;

            type = frame->y;
            foot = h - 94;
            xf = W - 28;                        /* the leaning edge clear of the ball at its widest */
            cx = xf / 2;
            /* the wall: the colour at the feet, dark toward the top */
            while (k > 1 && (pw * k > xf - 16 || ph * k > h / 2)) {
                k--;
            }
            /* the wall stops at the band; a deep shade of the colour at the
             * top (not black: orange went brown), the colour itself at the feet */
            rx_slab(c, 0, h, 0, 0, WALL_X(0) * FP, WALL_X(h) * FP, 0, bandy, 0, 0, rx_mix(rgb, 0x000000u, 30, 100),
                    rx_mix(rgb, 0xFFFFFFu, 8, 100), 255);
            /* the shaft of light, parallel to the edge, on the Pokemon */
            rx_slab(c, 0, h, (WALL_X(0) - xf + cx - xf / 5) * FP, (WALL_X(h) - xf + cx - xf / 5) * FP,
                    (WALL_X(0) - xf + cx + xf / 5) * FP, (WALL_X(h) - xf + cx + xf / 5) * FP, 0, bandy, 0, 0, 0xFFFFFFu,
                    0xFFFFFFu, 46);
            /* the white rule down the edge, a dark line outside it */
            rx_slab(c, 0, h, WALL_X(0) * FP, WALL_X(h) * FP, (WALL_X(0) + 4) * FP, (WALL_X(h) + 4) * FP, 0, bandy, 0, 0,
                    RX_WHITE, RX_WHITE, 255);
            rx_slab(c, 0, h, (WALL_X(0) + 4) * FP, (WALL_X(h) + 4) * FP, (WALL_X(0) + 6) * FP, (WALL_X(h) + 6) * FP, 0, bandy,
                    0, 0, RX_OUTLINE, RX_OUTLINE, 150);
            frame->x = cx;
            frame->y = foot;
            frame->w = k;
            return 1;
        }
        if (strcmp(app, "starter-caption") == 0) {
            /* the name in ink on a white strip under the feet; the strip
             * passes the wall's edge (the plates' strip); the type as its
             * word in its colour, where the DS writes a sentence */
            const char *b = ui_app_text_get(1);
            int y0 = foot + 8, bh = 24, xa = WALL_X(y0) + 22, xb = xa - LEAN(bh);
            int ty = y0 + ui_text_y(bh, UI_FONT_LARGE), nw = ui_text_w(b, UI_FONT_LARGE);

            rx_slab(c, y0 + 2, bh, 0, 0, (xa + 2) * FP, (xb + 2) * FP, 0, bh, 0, 0, RX_OUTLINE, RX_OUTLINE, 140);
            rx_slab(c, y0, bh, 0, 0, xa * FP, xb * FP, 0, bh, 0, 0, RX_WHITE, RX_WHITE, 255);
            ui_text(c, 12, ty, b, R(0x181818u), -1, UI_FONT_LARGE, 1);
            if (type != UI_TYPE_UNKNOWN) {
                /* at the strip's right end: the name and the type hold its two ends */
                int tw = ui_text_w(ui_type_name(type), UI_FONT_SMALL);

                if (12 + nw + 10 + tw + 10 < xb) {
                    rx_type_word(c, xb - 10 - tw, y0 + ui_text_y(bh, UI_FONT_SMALL), type, 1);
                }
            }
            return 1;
        }
        if (strcmp(app, "starter-ask") == 0 && frame->x == 1) {
            return 0;                   /* over the case: the band as it is */
        }
        if (strcmp(app, "starter-ask") == 0) {
            /* the question in the message band, clear of the wall */
            {
                u32 tf, te, ti, ts;

                rx_textbox(c, UI_TEXTBOX_SYSTEM | 16, 0, bandy, w, bandh, &tf, &te, &ti, &ts);
            }
            ui_text(c, WALL_X(bandy) + 28, bandy + ui_text_y(bandh, UI_FONT_LARGE), ui_app_text_get(2), R(0xF8F8F8u),
                    (s32)R(0x101010u), UI_FONT_LARGE, 1);
            return 1;
        }
        if (strcmp(app, "starter-answer-rect") == 0) {
            /* the answers inside the band at its right end, side by side */
            int n = frame->x, bw = 66, bh = 18;

            frame->x = w - 18 - 2 * bw - 8 + n * (bw + 8);
            frame->y = bandy + (bandh - bh) / 2 + 1;
            frame->w = bw;
            frame->h = bh;
            return 1;
        }
        if (strcmp(app, "starter-answer") == 0) {
            /* a leaning chip: white with ink letters when chosen, else dark
             * glass under a white rule */
            int hot = frame->h < 0, h2 = hot ? -frame->h : frame->h, lean = LEAN(h2) / 2;
            int xl0 = (frame->x + lean) * FP, xl1 = (frame->x - lean) * FP;
            int xr0 = (frame->x + frame->w + lean) * FP, xr1 = (frame->x + frame->w - lean) * FP;
            const char *t = ui_app_text_get(3);
            int tx = frame->x + (frame->w - ui_text_w(t, UI_FONT_LARGE)) / 2;

            if (hot) {
                rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 0, h2, 0, 0, RX_WHITE, RX_WHITE, 255);
                ui_text(c, tx, frame->y + ui_text_y(h2, UI_FONT_LARGE), t, R(0x181818u), -1, UI_FONT_LARGE, 1);
            } else {
                rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 0, h2, 0, 0, 0x000000u, 0x000000u, 150);
                rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 0, 2, 0, 0, RX_WHITE, RX_WHITE, 200);
                ui_text(c, tx, frame->y + ui_text_y(h2, UI_FONT_LARGE), t, R(0xF8F8F8u), (s32)R(0x101010u), UI_FONT_LARGE,
                        1);
            }
            return 1;
        }
#undef WALL_X
        return 0;
    }
    if (strcmp(app, "starter-pic") == 0) {
        /* the chosen Pokemon's box: a dark plate under the white strip with
         * its right edge leaning (the battle plates' shape), a white rule
         * over the name; white letters (answer 0) */
        int x = frame->x, y = frame->y - 40, fw = frame->w, fh = frame->h, lean = 10;

        frame->y = y;                   /* on the message band, not under it */
        rx_slab(c, y, fh, x * FP, x * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 0, fh, 0, 0, RX_OUTLINE, RX_OUTLINE,
                255);
        rx_slab(c, y, fh, x * FP, x * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 1, fh - 1, FP, INSET, 0x383838u,
                0x1A1A1Au, 255);
        rx_slab(c, y, fh, x * FP, x * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 1, 3, FP, INSET, RX_WHITE, RX_WHITE,
                255);
        ui_fill(c, x + 6, y + fh - 28, fw - 16, 1, R(RX_WHITE), 120);
        frame->h = 0;
        return 1;
    }
    if (strcmp(app, "catch-ground") == 0) {
        /* a caught Pokemon: the theme's leaning field in the DS screen's
         * teal across a dark ground, between two white rules, the Pokemon
         * on it (its light is the field, not a disc) */
        int cx = frame->x + frame->w / 2, fw = frame->w * 2, lean2 = LEAN(h) / 2, x0 = cx - fw / 2, x1 = cx + fw / 2;

        ui_fill(c, 0, 0, w, h, R(0x101418u), 255);
        rx_slab(c, 0, h, (x0 + lean2) * FP, (x0 - lean2) * FP, (x1 + lean2) * FP, (x1 - lean2) * FP, 0, h, 0, 0, 0x18B8A0u,
                0x0C5860u, 255);
        rx_slab(c, 0, h, (x0 + lean2) * FP, (x0 - lean2) * FP, (x0 + lean2 + 3) * FP, (x0 - lean2 + 3) * FP, 0, h, 0, 0,
                RX_WHITE, RX_WHITE, 255);
        rx_slab(c, 0, h, (x1 + lean2 - 3) * FP, (x1 - lean2 - 3) * FP, (x1 + lean2) * FP, (x1 - lean2) * FP, 0, h, 0, 0,
                RX_WHITE, RX_WHITE, 255);
        rx_slab(c, 0, h, (x1 + lean2 + 4) * FP, (x1 - lean2 + 4) * FP, (x1 + lean2 + 5) * FP, (x1 - lean2 + 5) * FP, 0, h, 0,
                0, RX_WHITE, RX_WHITE, 110);
        ui_ellipse(c, cx - frame->w * 34 / 100, frame->y + frame->h - 12, frame->w * 68 / 100, 12, R(0x000000u), UI_NONE, 70);
        return 1;
    }
    if (strcmp(app, "target-mark") == 0) {
        /* a target: leaning ticks at its four corners (the theme's lean, one
         * across for two down), white with a gold foot pair when chosen,
         * faint and short on the others */
        int sel = frame->h < 0, h2 = sel ? -frame->h : frame->h, x = frame->x, y = frame->y, fw = frame->w;
        int n = sel ? 24 : 10, t = sel ? 4 : 1, i;

        for (i = 0; i < 4; i++) {
            int bx = (i & 1) ? x + fw + 3 - t : x - 3, by = (i & 2) ? y + h2 - n : y;

            if (sel) {
                rx_slab(c, by - 1, n + 2, (bx + n / 2) * FP, (bx - 1) * FP, (bx + n / 2 + t + 2) * FP, (bx + t + 1) * FP, 0,
                        n + 2, 0, 0, RX_OUTLINE, RX_OUTLINE, 255);
            }
            rx_slab(c, by, n, (bx + n / 2) * FP, bx * FP, (bx + n / 2 + t) * FP, (bx + t) * FP, 0, n, 0, 0,
                    sel && (i & 2) ? RX_ACCENT : RX_WHITE, sel && (i & 2) ? RX_ACCENT : RX_WHITE, sel ? 255 : 130);
        }
        return 1;
    }
    if (strcmp(app, "levelup") == 0) {
        /* the level-up box: the band's dark glass under a white rule, as
         * the plate it stands on; white letters (the answer 0) */
        int yy;

        for (yy = 0; yy < frame->h; yy++) {
            ui_fill(c, frame->x, frame->y + yy, frame->w, 1, R(rx_mix(0x343434u, 0x1A1A1Au, yy, frame->h - 1)), 235);
        }
        ui_fill(c, frame->x, frame->y, frame->w, 1, R(RX_PLATE_OL), 255);
        ui_fill(c, frame->x, frame->y + 1, frame->w, 2, R(RX_WHITE), 255);
        frame->h = 0;
        return 1;
    }
    if (strcmp(app, "trade-head") == 0) {
        /* the trade's two Pokemon: a leaning dark plate under the white
         * strip, a gold slash between the two */
        int h2 = frame->h, lean = LEAN(h2) / 2, x = frame->x, fw = frame->w;

        rx_slab(c, frame->y, h2, (x + lean) * FP, (x - lean) * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 0, h2, 0, 0,
                RX_OUTLINE, RX_OUTLINE, 255);
        rx_slab(c, frame->y, h2, (x + lean) * FP, (x - lean) * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 1, h2 - 1,
                INSET, INSET, 0x383838u, 0x1A1A1Au, 255);
        rx_slab(c, frame->y, h2, (x + lean) * FP, (x - lean) * FP, (x + fw + lean) * FP, (x + fw - lean) * FP, 1, 3, INSET,
                INSET, RX_WHITE, RX_WHITE, 255);
        return 1;
    }
    if (strcmp(app, "save-pic") == 0) {
        /* the trainer at the window's right: a leaning field in the theme's
         * blue between two white rules, a shadow under the feet */
        int t = LEAN(frame->h) / 2, x = frame->x, y = frame->y, fw = frame->w, fh = frame->h;

        rx_slab(c, y, fh, (x + 2 * t) * FP, x * FP, (x + fw) * FP, (x + fw) * FP, 0, fh, 0, 0, RX_PLASMA_BLUE, 0x1060A0u,
                120);
        rx_slab(c, y, fh, (x + 2 * t) * FP, x * FP, (x + 2 * t + 2) * FP, (x + 2) * FP, 0, fh, 0, 0, RX_WHITE, RX_WHITE,
                255);
        rx_slab(c, y, fh, (x + 2 * t + 5) * FP, (x + 5) * FP, (x + 2 * t + 6) * FP, (x + 6) * FP, 0, fh, 0, 0, RX_WHITE,
                RX_WHITE, 110);
        ui_ellipse(c, x + fw / 2 - 26, y + fh - 14, 56, 10, R(0x000000u), UI_NONE, 90);
        return 1;
    }
    if (strcmp(app, "bag-menu-row") == 0) {
        /* the picked row holds its choices: a dark leaning plate, the
         * screen's pink on its end */
        int lean = LEAN(frame->h), x = frame->x + 6, w = frame->w - lean;

        rx_slab(c, frame->y, frame->h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 0, frame->h, 0, 0,
                RX_OUTLINE, RX_OUTLINE, 255);
        rx_slab(c, frame->y, frame->h, (x + lean) * FP, x * FP, (x + w + lean) * FP, (x + w) * FP, 1, frame->h - 1, INSET,
                INSET, 0x2A2A30u, 0x16161Au, 255);
        rx_slab(c, frame->y, frame->h, (x + lean) * FP, x * FP, (x + lean + 8) * FP, (x + 8) * FP, 1, frame->h - 1, INSET, 0,
                0xFF8AB0u, 0xF04878u, 255);
        frame->x += lean / 2 + 4;
        frame->w -= lean + 8;
        return 1;
    }
    if (strcmp(app, "dex-stats") == 0) {
        /* the lid shows the counts (frame: seen, own, the share owned) */
        rx_dex_stats(frame->x, frame->y, frame->w, frame->h);
        return 1;
    }
    if (strcmp(app, "dex-screen1") == 0 || strcmp(app, "dex-screen2") == 0) {
        RxDex g;

        rx_dex_geom(c, 256, &g);
        *frame = app[10] == '1' ? g.s1 : g.s2;
        return 1;
    }
    if (strcmp(app, "dex-head") == 0) {
        return 1;                       /* the header stands on the glass */
    }
    if (strcmp(app, "dex-panel") == 0) {
        /* no box: the Pokemon on the glass, a white rule under its head */
        ui_fill(c, frame->x + 6, frame->y + 53, frame->w - 12, 1, R(RX_OUTLINE), 255);
        ui_fill(c, frame->x + 6, frame->y + 54, frame->w - 12, 2, R(RX_WHITE), 255);
        return 1;
    }
    if (strncmp(app, "dex-sect-", 9) == 0) {
        /* a part of the entry: darker glass, a leaning cap in the hue on
         * its head, a light line along its top */
        int x = frame->x, y = frame->y, w = frame->w, h = frame->h;

        ui_fill(c, x, y, w, h, R(0x08040Cu), 150);
        ui_fill(c, x, y, w, 1, R(0xFFFFFFu), 46);
        ui_fill(c, x, y + 13, w, 1, R(0xF04860u), 110);
        rx_slab(c, y + 2, 10, (x + 9) * FP, (x + 4) * FP, (x + 14) * FP, (x + 9) * FP, 0, 10, 0, 0, 0xFF7890u, 0xF04860u,
                255);
        return 1;
    }
    if (strncmp(app, "dex-row", 7) == 0) {
        /* a row: a leaning chip, the chosen one white with the screen's red
         * on its end; its contents stand past the cap and move with it */
        int sel = strcmp(app, "dex-row-sel") == 0, hot = strcmp(app, "dex-row-hot") == 0;
        int lean = LEAN(frame->h);

        chip(c, frame->x + (sel ? 4 : 0), frame->y, frame->w - lean - 6, frame->h, sel ? 0xF04860u : 0x8A8A8Au, sel || hot,
             255);
        frame->x += lean / 2 + 8 + (sel ? 4 : 0);
        return 1;
    }
    if (strcmp(app, "summary-side") == 0) {
        return 1;                       /* the Pokemon at the right, as in the party */
    }
    if (strcmp(app, "pc-detail") == 0) {
        /* no box: the Pokemon stands on the ground's light field, a white
         * rule under its name */
        ui_fill(c, frame->x + 6, frame->y + 23, frame->w - 12, 1, R(RX_OUTLINE), 255);
        ui_fill(c, frame->x + 6, frame->y + 24, frame->w - 12, 2, R(RX_WHITE), 255);
        return 1;
    }
    if (strcmp(app, "pc-cell") == 0) {
        /* a cell: a flat pane of light, no frame; under the pointer the
         * white surface */
        int hot = frame->h < 0, ph = hot ? -frame->h : frame->h;

        if (hot) {
            ui_fill(c, frame->x, frame->y, frame->w, ph, R(RX_OUTLINE), 255);
            rx_slab(c, frame->y, ph, (frame->x + 1) * FP, (frame->x + 1) * FP, (frame->x + frame->w - 1) * FP,
                    (frame->x + frame->w - 1) * FP, 1, ph - 1, 0, 0, RX_WHITE, RX_WHITE_LO, 255);
        } else {
            ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, ph - 2, R(0xFFFFFFu), 20);
        }
        return 1;
    }
    if (strcmp(app, "pc-box") == 0) {
        /* the box: a darker field with the white rule at its head */
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x000000u), 110);
        ui_fill(c, frame->x, frame->y, frame->w, 1, R(RX_OUTLINE), 255);
        ui_fill(c, frame->x, frame->y + 1, frame->w, 2, R(RX_WHITE), 255);
        return 1;
    }
    if (strcmp(app, "summary-card") == 0) {
        /* the Pokemon stands on the ground's light field, in no box */
        (void)c;
        return 1;
    }
    if (strcmp(app, "dex") == 0) {
        /* the device (frame->x: how far open) */
        plasma_dex(c, frame->x);
        rx_accent(app_hue(app));
        return 1;
    }
    if (strcmp(app, "pc") == 0) {
        plasma_ground(c, w, h, 58, app_hue(app));
        rx_accent(app_hue(app));
        return 1;
    }
    if (strcmp(app, "style-flag") == 0) {
        /* the title menu's flag: one of the menu's own plates, hanging from
         * the screen's top (its top is off the screen); blue when under
         * the pointer, as the chosen plate's rule */
        int hot = frame->h < 0, fh = hot ? -frame->h : frame->h, lean = LEAN(fh + 4);

        chip(c, frame->x - lean, frame->y - 4, frame->w, fh + 4, hot ? RX_PLASMA_BLUE : 0x8A8A8Au, 0, 255);
        frame->x = frame->x - lean + 13;
        frame->w = lean * fh / (fh + 4);
        return 1;
    }
    if (strcmp(app, "sheet") == 0) {
        /* a sheet hanging from the top bar: the bar's own glass, from the
         * bar's foot rule down to a foot rule of its own; thin white rules
         * down its sides */
        int x = frame->x, y = frame->y, fw = frame->w, fh = frame->h, yy;

        for (yy = 0; yy < fh - 2; yy++) {
            ui_fill(c, x, y + yy, fw, 1, R(rx_mix(0x303030u, 0x1A1A1Au, yy, fh - 3)), 235);
        }
        ui_fill(c, x, y, 1, fh - 2, R(0xFFFFFFu), 130);
        ui_fill(c, x + fw - 1, y, 1, fh - 2, R(0xFFFFFFu), 130);
        ui_fill(c, x, y + fh - 2, fw, 2, R(RX_WHITE), 255);
        ui_fill(c, x, y + fh, fw, 1, R(RX_PLATE_OL), 255);
        return 1;
    }
    if (strcmp(app, "bag-pockets") == 0) {
        /* the battle Bag's pockets: one row of leaning plates */
        frame->x = 2;
        return 1;
    }
    if (strcmp(app, "bag-plate") == 0) {
        /* A plate of that row ( / / : its top to the right, a fifth of its
         * height), drawn so that its right side is the next one's left:
         * white when chosen (answer 1: dark letters), else dark under a
         * white hairline.  Answers its lean in w. */
        int sel = frame->h < 0, ph = sel ? -frame->h : frame->h, lean = ph / 5;
        int xl0 = (frame->x + lean) * FP, xl1 = frame->x * FP, xr0 = xl0 + frame->w * FP, xr1 = xl1 + frame->w * FP;

        rx_slab(c, frame->y, ph, xl0, xl1, xr0, xr1, 0, ph, 0, 0, RX_OUTLINE, RX_OUTLINE, 255);
        if (sel) {
            rx_slab(c, frame->y, ph, xl0, xl1, xr0, xr1, 0, ph, INSET, INSET, 0xFCFCFEu, 0xDCDCE4u, 255);
        } else {
            rx_slab(c, frame->y, ph, xl0, xl1, xr0, xr1, 0, ph, INSET, INSET, 0x323236u, 0x1A1A1Cu, 255);
            rx_slab(c, frame->y, ph, xl0, xl1, xr0, xr1, 0, 2, INSET, INSET, RX_WHITE, RX_WHITE, 220);
        }
        frame->w = lean;
        frame->h = sel;
        return 1;
    }
    if (strncmp(app, "cgear-", 6) == 0) {
        /* The C-Gear's sheet: the top bar itself, carried on downward from
         * the C-GEAR button.  The bar's own paint at the bar's own alpha,
         * from the shade the bar ends on back to the one it starts with; no
         * line under the C-GEAR button (the bar leaves its rule out between
         * the button's two leaning rules, cut along them; the rule stays
         * under the Item button); the bar's foot rule at the sheet's foot.
         * Its left side is the button's left rule carried on, its right side
         * the clock notch's left edge carried on (both down to the right,
         * one across for two down).  Plain rows, a hairline between them, a
         * small square in the channel's colour; the row under the pointer
         * is the white chip. */
        static int lf, rt, sy;         /* the two sides at the sheet's top, in 1/256 of a pixel */
        const int rh = 22, neck = 3;
        int d = neck + frame->x * rh;
#define CG_L(dd) (lf + (dd) * FP / 2)
#define CG_R(dd) (rt + (dd) * FP / 2)

        if (strcmp(app, "cgear-sheet") == 0) {
            int hh = neck + frame->w * rh + 2, yy, a = rx_bar_alpha > 0 ? rx_bar_alpha : 205;
            /* the C-GEAR button's two rules at the bar's foot, as "bar-button" drew them: the noted
             * button whose left rule is nearest the place the screen gives */
            int sep_l = frame->x, sep_r = frame->x + frame->h, best = 1 << 20, i;
            int k = LEAN(frame->y + 3) / 2 + 1;     /* the notch's lean (rx_clock_tab) */

            for (i = 0; i < 8; i++) {
                int far = g_bb_l[i] > frame->x ? g_bb_l[i] - frame->x : frame->x - g_bb_l[i];

                if (g_bb_r[i] > g_bb_l[i] && far < best) {
                    best = far;
                    sep_l = g_bb_l[i];
                    sep_r = g_bb_r[i];
                }
            }

            sy = frame->y;
            lf = sep_l * FP;
            /* the notch's left edge reaches rx_clock_x0 + k three rows under the bar's foot */
            rt = rx_clock_x1 > rx_clock_x0 ? (rx_clock_x0 + k) * FP - 3 * FP / 2 : lf + 95 * FP;
            if (rt < lf + 84 * FP) {
                rt = lf + 84 * FP;
            }
            rx_bar_gap_x0 = lf + FP;
            rx_bar_gap_x1 = sep_r * FP;
            rx_bar_gap_ttl = 2;
            for (yy = 0; yy < hh; yy++) {
                rx_span(c, sy + yy, CG_L(yy), CG_R(yy), rx_mix(0x303030u, 0x1A1A1Au, yy, hh - 1), a);
                ui_fill(c, CG_L(yy) / FP, sy + yy, 1, 1, R(0xFFFFFFu), 110);
                ui_fill(c, (CG_R(yy) - 1) / FP, sy + yy, 1, 1, R(0xFFFFFFu), 110);
            }
            rx_span(c, sy + hh, CG_L(hh), CG_R(hh), RX_WHITE, 255);
            rx_span(c, sy + hh + 1, CG_L(hh + 1), CG_R(hh + 1), RX_WHITE, 255);
            rx_span(c, sy + hh + 2, CG_L(hh + 2), CG_R(hh + 2), RX_PLATE_OL, 255);
            frame->h = 1;               /* the button stays the bar's glass: the sheet is the same piece */
            return 1;
        }
        if (strcmp(app, "cgear-row-rect") == 0) {
            frame->x = CG_L(d) / FP + 2;
            frame->y = sy + d;
            frame->w = (CG_R(d + rh) - CG_L(d)) / FP - 4;
            frame->h = rh;
            return 1;
        }
        if (strcmp(app, "cgear-row") == 0) {
            int hot = frame->y, row = frame->x, state = frame->h, tx = CG_L(d + rh / 2) / FP + 16;
            u32 col = (u32)frame->w & 0xFFFFFFu;

            if (hot) {
                rx_slab(c, sy + d + 1, rh - 2, CG_L(d + 1) + 3 * FP, CG_L(d + rh - 1) + 3 * FP, CG_R(d + 1) - 3 * FP,
                        CG_R(d + rh - 1) - 3 * FP, 0, rh - 2, 0, 0, RX_WHITE, RX_WHITE_LO, 255);
            } else if (row > 0) {
                ui_fill(c, CG_L(d) / FP + 6, sy + d, (CG_R(d) - CG_L(d)) / FP - 12, 1, R(0xFFFFFFu), 46);
            }
            if (row < 3) {
                ui_fill(c, tx - 9, sy + d + rh / 2 - 2, 4, 4, R(col), state == 0 ? 120 : state == 2 ? 255 : 210);
            }
            frame->x = tx;
            frame->y = sy + d + 2;
            frame->w = -1;              /* the mark is drawn here; upright words */
            frame->h = hot;             /* dark letters on the white chip */
            return 1;
        }
#undef CG_L
#undef CG_R
        return 0;
    }
    if (strcmp(app, "sheet-tile") == 0) {
        /* a tile of a sheet: a leaning chip of glass (the X menu's), the
         * white surface under the pointer */
        int hot = frame->h < 0, h2 = hot ? -frame->h : frame->h, lean = LEAN(h2) / 2;
        int xl0 = (frame->x + lean) * FP, xl1 = (frame->x - lean) * FP;
        int xr0 = (frame->x + frame->w + lean) * FP, xr1 = (frame->x + frame->w - lean) * FP;

        rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 0, h2, 0, 0, 0x000000u, 0x000000u, 140);
        rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 1, h2 - 1, INSET, INSET, 0xFFFFFFu, 0xFFFFFFu, hot ? 70 : 30);
        rx_slab(c, frame->y, h2, xl0, xl1, xr0, xr1, 1, 2, INSET, INSET, 0xFFFFFFu, 0xFFFFFFu, hot ? 255 : 90);
        return 1;
    }
    if (strcmp(app, "bar-button") == 0) {
        /* a button of the top bar: a segment of the bar between two leaning
         * rules parallel to the clock's edge on its side ( \ \ at the
         * clock's left, / / at its right); neighbours share a rule.  Lit
         * (under the pointer or held open: a negative height): lighter glass,
         * the bar's foot rule thicker under it. */
        int hot = frame->h < 0, bh = hot ? -frame->h : frame->h, t = LEAN(bh) / 2;
        int left = frame->x + frame->w / 2 < w / 2, d = left ? t : -t;
        int xa = frame->x - 2, xb = frame->x + frame->w + 2, ra = 1, rb = 1;

        /* beside the clock, the clock's own edge is the segment's end */
        if (left && rx_clock_x1 > rx_clock_x0 && xb >= rx_clock_x0 - 10) {
            xb = rx_clock_x0;
            rb = 0;
        }
        if (!left && rx_clock_x1 > rx_clock_x0 && xa <= rx_clock_x1 + 10) {
            xa = rx_clock_x1;
            ra = 0;
        }
        g_bb_l[g_bb_at & 7] = xa + d;
        g_bb_r[g_bb_at & 7] = xb + d;
        g_bb_at++;
        if (hot) {
            rx_slab(c, frame->y, bh, (xa - d) * FP, (xa + d) * FP, (xb - d) * FP, (xb + d) * FP, 0, bh - 2, 0, 0, 0xFFFFFFu,
                    0xFFFFFFu, 56);
            rx_slab(c, frame->y, bh, (xa - d) * FP, (xa + d) * FP, (xb - d) * FP, (xb + d) * FP, bh - 4, bh - 2, 0, 0,
                    RX_WHITE, RX_WHITE, 255);
        }
        if (ra) {
            rx_slab(c, frame->y, bh, (xa - d) * FP, (xa + d) * FP, (xa - d + 1) * FP, (xa + d + 1) * FP, 2, bh - 2, 0, 0,
                    0xFFFFFFu, 0xFFFFFFu, hot ? 255 : 130);
        }
        if (rb) {
            rx_slab(c, frame->y, bh, (xb - d) * FP, (xb + d) * FP, (xb - d + 1) * FP, (xb + d + 1) * FP, 2, bh - 2, 0, 0,
                    0xFFFFFFu, 0xFFFFFFu, hot ? 255 : 130);
        }
        return 1;
    }
    if (strcmp(app, "summary") == 0) {
        plasma_ground(c, w, h, 46, app_hue(app));
        rx_accent(app_hue(app));
        frame->x = -4;
        frame->y = -1;
        frame->w = w + 8;
        frame->h = h + 1;
        return 1;
    }
    /* a windowed screen: a glass card over the world, nothing behind it */
    (void)l;
    (void)x0;
    (void)x1;
    rx_accent(app_hue(app));
    ui_panel(c, frame->x, frame->y, frame->w, frame->h, UI_CUT_TL | UI_CUT_BR, 10, UI_BODY, UI_EDGE, 244);
    return 1;
}
/* [/app] */
/* [title] */
/* (Plasma's title menu is with its X menu, above.) */
/* [/title] */
const UiThemeOps ui_theme_plasma = {
    .id = "plasma",
    .name = "Plasma",
    .color = rx_color,
    .frame_begin = rx_frame_begin,
    .text = rx_text,
    .panel = rx_panel,
    .header = rx_header,
    .row = rx_row,
    .menu_row = rx_menu_row,
    .message = rx_message,
    .tab = rx_tab,
    .move_row = rx_move_row,
    .cmd_button = rx_cmd_button,
    .bar = rx_bar,
    .hints = rx_hints,
    .party_row = plasma_party_row,
    .app_frame = plasma_app_frame,
    .bag_detail_frame = plasma_bag_detail_frame,
    .bag_row = plasma_bag_row,
    .bag_pocket = plasma_bag_pocket,
    .bag_backdrop = plasma_bag_backdrop,
    .bag_layout = plasma_bag_layout,
    .party_menu_entry = plasma_party_menu_entry,
    .party_prompt = plasma_party_prompt,
    .party_detail = plasma_party_detail,
    .party_plate = plasma_party_plate,
    .party_backdrop = plasma_party_backdrop,
    .party_layout = plasma_party_layout,
    .textbox = rx_textbox,
    .choice_row = rx_choice_row,
    .scrollbar = rx_scrollbar,
    .toast = rx_toast,
    .cursor_corners = rx_cursor_corners,
    .keycap = rx_keycap,
    .pocket_tab = rx_pocket_tab,
    .toggle = rx_toggle,
    .slider = rx_slider,
    .stepper = rx_stepper,
    .field = rx_field,
    .key = rx_key,
    .dialog = rx_dialog,
    .progress = rx_progress,
    .page_dots = rx_page_dots,
    .name_tag = rx_name_tag,
    .stat_row = rx_stat_row,
    .icon_well = rx_icon_well,
    .item_row = rx_item_row,
    .dex_row = rx_dex_row,
    .info_row = rx_info_row,
    .strip = rx_strip,
    .clock_tab = rx_clock_tab,
    .plate = rx_plate,
    .plate_status = rx_plate_status,
    .field_menu_layout = plasma_field_menu_layout,
    .field_menu_backdrop = plasma_field_menu_backdrop,
    .field_menu_button = plasma_field_menu_button,
    .title_menu_layout = plasma_title_menu_layout,
    .title_menu_backdrop = plasma_title_menu_backdrop,
    .title_menu_row = plasma_title_menu_row,
    .battle_layout = plasma_battle_layout,
    .battle_backdrop = plasma_battle_backdrop,
    .battle_box = plasma_battle_box,
    .battle_command = plasma_battle_command,
    .battle_move = plasma_battle_move,
    .battle_move_info = plasma_battle_move_info,
    .battle_balls = rx_battle_balls,
    .battle_ability = rx_battle_ability,
};
