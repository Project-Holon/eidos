/*
 * engine/ui/theme_harmonia.c -- Harmonia: the one-screen interface in the DS menu's folder
 * tiles (EIDOS_UI_THEME=harmonia).
 *
 * The commands and the moves are a 2 x 2 block of folder tiles in the bottom
 * right corner: a dark tile with a light bevel, a gloss across its upper
 * right, a soft shadow, and a folder's tab at its top left in the entry's
 * colour (a command's, a move's type).  The chosen tile turns white and
 * lifts.  The message is a wide tile beside the block, at the same foot; the
 * chosen move's PP, power and description stand in it.
 *
 * The plates, the party's balls and an Ability's plate are the ones Plasma
 * uses (engine/ui/theme_bw.h): your plate above the message at the left, the
 * foe's top right.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_bw.h"
#include "textbox.h"
#include "ui_lang.h"

/* What the battle shows and for how many frames (the backdrop counts):
 * the tiles rise into place when the commands or the moves appear. */
static int g_what, g_age;

#define TAB_H    6
#define TILE_H   36
#define TILE_GAP 4
#define MSG_H    56
#define LIFT     2

/* The dot mesh of the DS's dark surfaces. */
static void mesh(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    int yy, xx;

    for (yy = 4; yy < h - 4; yy += 5) {
        for (xx = 4; xx < w - 5; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(0x383838u), a);
        }
    }
}

/*
 * A folder tile: a soft shadow, the outline, a light bevel, the dark body
 * (a gloss across its upper right, its edge smooth), and (tab) the folder's
 * tab at its top left in `tip`, wider at its foot.  Chosen: the white
 * surface.  `dots`: the mesh over the body (the message's tile).
 */
static void tile(EidosUiCtx *c, int x, int y, int w, int h, u32 tip, int tab, int sel, int dots, int a)
{
    int i;

    for (i = 0; i < 4; i++) {
        ui_fill(c, x + 1, y + h + i, w, 1, R(0x000000u), a * (sel ? 110 : 80) / (1 + i * i) / 255);
    }
    if (tab) {
        int tw = w * 2 / 5;

        rx_slab(c, y - TAB_H, TAB_H + 1, x * FP, x * FP, (x + tw - 2) * FP, (x + tw + 3) * FP, 0, TAB_H + 1, 0, 0,
                RX_OUTLINE, RX_OUTLINE, a);
        rx_slab(c, y - TAB_H, TAB_H + 1, x * FP, x * FP, (x + tw - 2) * FP, (x + tw + 3) * FP, 1, TAB_H + 1, FP,
                INSET, rx_mix(tip, 0xFFFFFFu, 45, 100), tip, a);
    }
    ui_fill(c, x, y, w, h, R(RX_OUTLINE), a);
    if (sel) {
        rx_slab(c, y, h, (x + 1) * FP, (x + 1) * FP, (x + w - 1) * FP, (x + w - 1) * FP, 1, h - 1, 0, 0, RX_WHITE,
                RX_WHITE_LO, a);
        return;
    }
    /* the bevel: light along the top and the left, dark at the foot */
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(0x4E4E4Eu), a);
    ui_fill(c, x + 2, y + 2, w - 3, h - 3, R(0x161616u), a);
    rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + w - 2) * FP, (x + w - 2) * FP, 2, h - 2, 0, 0, 0x303030u,
            0x1E1E1Eu, a);
    /* the gloss: the upper right, cut by a leaning edge */
    rx_slab(c, y, h, (x + w * 45 / 100 + LEAN(h)) * FP, (x + w * 45 / 100 + 4 * LEAN(h)) * FP, (x + w - 2) * FP,
            (x + w - 2) * FP, 2, h - 2, 0, 0, 0xFFFFFFu, 0xFFFFFFu, a * 18 / 255);
    if (dots) {
        mesh(c, x, y, w, h, a * 150 / 255);
    }
}

/* ---------------------------------------------------------------- */
/* Places                                                            */
/* ---------------------------------------------------------------- */

static void harmonia_battle_layout(UiBattleLayout *l, int what)
{
    int m = AT_LEAST(SX(12), 8), hb = l->h - 17, base = hb - 7, i;
    int ownw = AT_LEAST(SX(220), 190), foew = AT_LEAST(SX(210), 180);
    int tw = AT_LEAST(SX(150), 132), bx = l->w - m - (2 * tw + TILE_GAP);

    /* the block in the corner: the commands and the moves in the same four
     * places */
    for (i = 0; i < 4; i++) {
        EidosUiRect r;

        r.x = bx + (i % 2) * (tw + TILE_GAP);
        r.y = base - TILE_H - (1 - i / 2) * (TILE_H + TILE_GAP + TAB_H);
        r.w = tw;
        r.h = TILE_H;
        l->cmd[i] = l->move[i] = r;
    }
    /* the message's tile beside it, at the same foot */
    l->msg.x = m;
    l->msg.h = MSG_H;
    l->msg.y = base - MSG_H;
    l->msg.w = (what == UI_BATTLE_MESSAGE ? l->w - m : bx - 2 * TILE_GAP) - m;
    l->info_x = l->msg.x + 10;
    l->info_y = l->msg.y + 8;
    /* your plates above the message, the foe's top right */
    for (i = 0; i < l->n_own && i < 3; i++) {
        l->own[i].x = m;
        l->own[i].w = ownw;
        l->own[i].h = OWN_H;
        l->own[i].y = l->msg.y - 7 - OWN_H - (l->n_own - 1 - i) * (OWN_H + 2);
    }
    l->foe_x = l->w - m - foew;
    l->foe_y = m;
    l->foe_w = foew;
    l->foe_step = FOE_H + 2;
    rx_place_balls(l, m, l->msg.y - 7 - OWN_H, l->foe_x);
}

/* ---------------------------------------------------------------- */
/* The hooks                                                         */
/* ---------------------------------------------------------------- */

static void harmonia_battle_box(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    tile(c, x, y, w, h, 0, 0, 0, 1, a * 240 / 255);
}

/* Under the moves: the message's tile (the chosen move's facts stand in
 * it; the battle draws the prompt's itself). */
static void harmonia_battle_backdrop(EidosUiCtx *c, const UiBattleLayout *l, int what)
{
    if (what != g_what) {
        g_what = what;
        g_age = 0;
    } else if (g_age < 100000) {
        g_age++;
    }
    if (what == UI_BATTLE_MOVES) {
        harmonia_battle_box(c, l->msg.x, l->msg.y, l->msg.w, l->msg.h, 255);
    }
}

#define RISE 6

static int rise_y(void)
{
    return g_age < RISE ? (RISE - g_age) * (RISE - g_age) / 4 : 0;
}

static int rise_a(int a)
{
    return g_age < RISE ? a * (g_age + 2) / (RISE + 2) : a;
}

/* A command: its tile, the game's own icon at the left as the DS menu's
 * tiles have theirs, its name after. */
static void harmonia_battle_command(EidosUiCtx *c, int x, int y, int w, int h, int which, const char *name,
                                 u32 color, int sel, int enabled)
{
    int a = rise_a(enabled ? 255 : 120), font = UI_FONT_LARGE;

    (void)color;
    which &= 3;
    y += rise_y() - (sel ? LIFT : 0);
    tile(c, x, y, w, h, rx_cmd_color[which], 1, sel, 0, a);
    rx_game_icon(c, x + 24, y + h / 2, rx_cmd_icon[which]);
    rx_label(c, x + 46, y + ui_text_y(h, font), name, sel, enabled ? RX_TEXT : RX_MUTED, font);
}

/* A move: its tile with the type's tab, its name, its type under. */
static void harmonia_battle_move(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name, int pp,
                              int maxpp, int sel, int enabled)
{
    int a = rise_a(enabled ? 255 : 130), font;

    (void)maxpp;
    y += rise_y();
    if (name == NULL) {
        /* an empty place: the tile, faint, nothing on it */
        tile(c, x, y, w, h, 0x4A4A4Au, 1, 0, 0, a * 110 / 255);
        return;
    }
    y -= sel ? LIFT : 0;
    tile(c, x, y, w, h, ui_type_color(type), 1, sel, 0, a);
    font = ui_text_w(name, UI_FONT_LARGE) <= w - 14 ? UI_FONT_LARGE : UI_FONT_MEDIUM;
    rx_label(c, x + (w - ui_text_w(name, font)) / 2, y + 4, name, sel, enabled && pp > 0 ? RX_TEXT : RX_MUTED, font);
    rx_type_word(c, x + (w - ui_text_w(ui_type_name(type), UI_FONT_SMALL)) / 2, y + h - 14, type, sel);
}

static void harmonia_battle_move_info(EidosUiCtx *c, const UiBattleLayout *l, const UiMoveInfo *m)
{
    rx_move_info(c, l->info_x, l->info_y, l->msg.w - 22, m, g_age);
}

/* ---------------------------------------------------------------- */
/* The field's X menu                                                */
/* ---------------------------------------------------------------- */

/* The menu's folder tiles in one column at the right; the party at the left. */
static void harmonia_field_menu_layout(UiFieldMenuLayout *l)
{
    int m = AT_LEAST(SX(12), 8), tw = AT_LEAST(SX(160), 128), n = l->n > 0 ? l->n : 1, i;
    int gap = 4 + TAB_H, avail = l->h - 2 * m - TAB_H, th = (avail - (n - 1) * gap) / n, y0;

    th = th > 40 ? 40 : th < 24 ? 24 : th;
    y0 = m + TAB_H + (avail - (n * th + (n - 1) * gap)) / 2;
    for (i = 0; i < l->n; i++) {
        l->btn[i].x = l->w - m - tw;
        l->btn[i].y = y0 + i * (th + gap);
        l->btn[i].w = tw;
        l->btn[i].h = th;
    }
    l->party_x = m;
    l->party_y = m;
}

static void harmonia_field_menu_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                                       const UiImage *icon, int state)
{
    int sel = state != 0, font = ui_text_w(label, UI_FONT_LARGE) <= w - 44 ? UI_FONT_LARGE : UI_FONT_MEDIUM;

    y -= sel ? LIFT : 0;
    tile(c, x, y, w, h, 0x626262u, 1, sel, 0, 255);
    if (icon != NULL) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + 3, y + 2, 34, h - 4);
        ui_image(c, x + 8 + (22 - icon->w) / 2, y + (h - icon->h) / 2 + 1, icon, 1);
        ui_pop(c, 0, 0, &saved);
    }
    rx_label(c, x + 38, y + ui_text_y(h, font), label, sel, RX_TEXT, font);
}

/* The title's menu: the DS's one column, CONTINUE's card on top. */
static void harmonia_title_menu_layout(UiTitleMenuLayout *l, int phase)
{
    if (phase == 0) {
        l->stacked = 1;
    }
}

/* [party] */
/* ---------------------------------------------------------------- */
/* The party screen                                                  */
/* ---------------------------------------------------------------- */

/*
 * The DS's party screen, made one screen.  Its own pieces, drawn from the
 * game's: the dark grid; the plates, long octagons in two blues split on a
 * slant, a light line inside a dark outline (the chosen one bright, its line
 * white), the icon on the left end, the green HP tag and thin bar, the level
 * at the lower left and the HP numbers at the lower right; the menu's
 * buttons, slate with pointed ends, the chosen one deep blue; the prompt's
 * white window in its black frame.  In the DS's arrangement: two staggered
 * columns.  The chosen Pokemon stands at the right, which the DS had no room
 * for.
 */

/* Row yy of an octagon h tall with corners cut `cut` deep: how far in. */
static int oct_in(int yy, int h, int cut)
{
    int e = yy < h - 1 - yy ? yy : h - 1 - yy;

    return e < cut ? cut - e : 0;
}

/* An octagon, `in` pixels inside the edge of the one at (x, y, w, h) with
 * corners cut `cut` deep: a blend from top to bot, its slanted edges smooth. */
static void oct(EidosUiCtx *c, int x, int y, int w, int h, int cut, int in, u32 top, u32 bot, int a)
{
    u32 c1, c2;

    if (w >= 120 && h >= 60) {
        rx_frame_note(c, x, y, cut, in);       /* a window's frame: its title's tab keeps inside its corner */
    }
    x += in;
    y += in;
    w -= 2 * in;
    h -= 2 * in;
    cut -= in * 2 / 5;
    cut = cut < 1 ? 1 : cut * 2 > h ? h / 2 : cut;
    c1 = rx_mix(top, bot, cut, h - 1);
    c2 = rx_mix(top, bot, h - cut, h - 1);
    rx_slab(c, y, cut, (x + cut) * FP, x * FP, (x + w - cut) * FP, (x + w) * FP, 0, cut, 0, 0, top, c1, a);
    rx_slab(c, y + cut, h - 2 * cut, x * FP, x * FP, (x + w) * FP, (x + w) * FP, 0, h - 2 * cut, 0, 0, c1, c2, a);
    rx_slab(c, y + h - cut, cut, x * FP, (x + cut) * FP, (x + w) * FP, (x + w - cut) * FP, 0, cut, 0, 0, c2, bot, a);
}

/* The plate's body: two blues split on a slant (the lighter at the left),
 * every edge smooth. */
static void plate_body(EidosUiCtx *c, int x, int y, int w, int h, int cut, u32 lite, u32 dark, int a)
{
    int in = 3, sx, s0, s1, s2, s3;

    oct(c, x, y, w, h, cut, in, dark, dark, a);
    x += in;
    y += in;
    w -= 2 * in;
    h -= 2 * in;
    cut -= in * 2 / 5;
    /* the split's x at the shape's top, at each corner's end, at its foot */
    sx = x + w * 36 / 100;
    s0 = (sx + h * 3 / 4) * FP;
    s1 = (sx + (h - cut) * 3 / 4) * FP;
    s2 = (sx + cut * 3 / 4) * FP;
    s3 = sx * FP;
    rx_slab(c, y, cut, (x + cut) * FP, x * FP, s0, s1, 0, cut, 0, 0, lite, lite, a);
    rx_slab(c, y + cut, h - 2 * cut, x * FP, x * FP, s1, s2, 0, h - 2 * cut, 0, 0, lite, lite, a);
    rx_slab(c, y + h - cut, cut, x * FP, (x + cut) * FP, s2, s3, 0, cut, 0, 0, lite, lite, a);
}

/* A button with pointed ends (`p` deep), `in` inside its edge, smooth. */
static void point_box(EidosUiCtx *c, int x, int y, int w, int h, int p, int in, u32 top, u32 bot, int a)
{
    int half;
    u32 mid;

    x += in;
    y += in;
    w -= 2 * in;
    h -= 2 * in;
    half = h / 2;
    mid = rx_mix(top, bot, half, h - 1);
    rx_slab(c, y, half, (x + p) * FP, x * FP, (x + w - p) * FP, (x + w) * FP, 0, half, 0, 0, top, mid, a);
    rx_slab(c, y + half, h - half, x * FP, (x + p) * FP, (x + w) * FP, (x + w - p) * FP, 0, h - half, 0, 0, mid, bot, a);
}

static int g_party_menu_n;      /* the party menu's entries, 0: closed */

static void harmonia_party_layout(UiPartyLayout *l)
{
    int m = AT_LEAST(SX(12), 8), top = 32, lw = l->w * 54 / 100, pw = (lw - m - 6) / 2, i;
    int msg_h = 32, room = l->h - 8 - msg_h - 8 - top, ph = (room - 10 - 2 * 6) / 3, y0;

    /* the DS's plate is 126 x 46: about three to one */
    ph = ph < 36 ? 36 : ph > 62 ? 62 : ph;
    ph = ph > pw * 37 / 100 ? pw * 37 / 100 : ph;
    y0 = top + (room - (3 * ph + 2 * 6 + 10)) / 2;
    l->frame.x = 0;
    l->frame.y = 0;
    l->frame.w = l->w;
    l->frame.h = l->h;
    l->header = 0;
    for (i = 0; i < 6; i++) {
        l->slot[i].x = m + (i & 1) * (pw + 6);
        l->slot[i].y = y0 + (i >> 1) * (ph + 6) + ((i & 1) ? 10 : 0);
        l->slot[i].w = pw;
        l->slot[i].h = ph;
    }
    l->msg.x = m;
    l->msg.y = l->h - 8 - msg_h;
    l->msg.w = lw - m;
    l->msg.h = msg_h;
    l->detail.x = lw + 10;
    l->detail.y = top;
    l->detail.w = l->w - m - lw - 10;
    l->detail.h = l->h - 8 - top;
    /* the menu's entries stand inside the chosen plate, which turns over
     * for them: two across (three for more than four) */
    l->menu_eh = 22;
    l->menu_apart = 1;
    {
        int at = l->sel >= 0 ? l->sel : 0;

        l->menu_cols = l->menu_n > 4 ? 3 : 2;       /* up to four: always two by two */
        l->menu.x = l->slot[at].x + 5;
        l->menu.y = l->slot[at].y + 4;
        l->menu.w = l->slot[at].w - 10;
        l->menu.h = l->slot[at].h - 8;
        if (l->menu_n > 6) {
            /* too many for a plate: a column beside it */
            l->menu_cols = 0;
            l->menu.w = 132;
            l->menu.x = l->slot[at].x + l->slot[at].w - 28;
            l->menu.h = l->menu_n * (l->menu_eh + 2) + 8;
            l->menu.y = l->slot[at].y + 10 + l->menu.h > l->h - 6 ? l->h - 6 - l->menu.h : l->slot[at].y + 10;
        }
    }
    g_party_menu_n = l->menu_n;
}

static void harmonia_party_backdrop(EidosUiCtx *c, const UiPartyLayout *l, const char *title, const char *sub,
                                    const char *right)
{
    int x, y;

    /* the DS's ground: near black, a faint blue grid with a mark where its
     * lines cross */
    ui_fill(c, 0, 0, l->w, l->h, R(0x10141Cu), 246);
    for (y = 14; y < l->h; y += 24) {
        ui_fill(c, 0, y, l->w, 1, R(0x1C2C40u), 255);
    }
    for (x = 12; x < l->w; x += 24) {
        ui_fill(c, x, 0, 1, l->h, R(0x1C2C40u), 255);
        for (y = 14; y < l->h; y += 24) {
            ui_fill(c, x - 1, y, 3, 1, R(0x2C4660u), 255);
            ui_fill(c, x, y - 1, 1, 3, R(0x2C4660u), 255);
        }
    }
    ui_header(c, 0, 3, l->w, title, sub, right, UI_SKY);
    rx_tone(rx_tone_blue);
}

static void harmonia_party_plate(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m)
{
    int x = r->x, y = r->y, w = r->w, h = r->h, sel = m->sel, fainted, tw, bx, bw;
    const int cut = 6;
    char s[48];

    if (m->name == NULL) {
        /* an empty place: the DS's faint shape */
        oct(c, x, y, w, h, cut, 1, 0x16304Au, 0x16304Au, 150);
        oct(c, x, y, w, h, cut, 2, 0x10141Cu, 0x10141Cu, 200);
        return;
    }
    if (m->turned > 0) {
        /* turned over for the menu: the plate closes to its middle line and
         * opens on its back (a dark plate in the white line) */
        int t = m->turned - 1, hh = t < 3 ? h - t * h / 3 : t < 6 ? (t - 2) * h / 3 : h;

        hh = hh < 4 ? 4 : hh > h ? h : hh;
        y += (h - hh) / 2;
        oct(c, x, y, w, hh, cut < hh / 2 ? cut : hh / 2, 0, 0x0C4869u, 0x0C4869u, 255);
        oct(c, x, y, w, hh, cut < hh / 2 ? cut : hh / 2, 1, 0xFFFFFFu, 0xDCF4FFu, 255);
        if (hh > 6) {
            oct(c, x, y, w, hh, cut < hh / 2 ? cut : hh / 2, 2, 0x0A2238u, 0x0E2C48u, 255);
        }
        return;
    }
    fainted = m->level > 0 && m->hp == 0;
    oct(c, x, y, w, h, cut, 0, sel ? 0x0C4869u : 0x082438u, sel ? 0x0C4869u : 0x082438u, 255);
    oct(c, x, y, w, h, cut, 1, sel ? 0xFFFFFFu : fainted ? 0xE89090u : 0x58C0E8u,
        sel ? 0xDCF4FFu : fainted ? 0xC06060u : 0x3C9CCCu, 255);
    oct(c, x, y, w, h, cut, 2, sel ? 0x0C5C8Cu : 0x0A2C48u, sel ? 0x0C5C8Cu : 0x0A2C48u, 255);
    if (fainted) {
        plate_body(c, x, y, w, h, cut, sel ? 0xF07070u : 0xA03C3Cu, sel ? 0xC84848u : 0x7A2828u, 255);
    } else {
        plate_body(c, x, y, w, h, cut, sel ? 0x28B8FFu : 0x1C6CA8u, sel ? 0x1480C8u : 0x124C80u, 255);
    }
    if (m->icon != NULL) {
        ui_image(c, x + 6 + (32 - m->icon->w) / 2, y + (h - 12 - m->icon->h) / 2 + 1, m->icon, 1);
    }
    /* the game's mark for what it holds, where the DS's plate has it: on the
     * feet of the icon (it was a hand-drawn disc over "Lv.") */
    ui_held_mark_game(c, x + 6, y + (h - 12 - 32) / 2 + 1, m->item);
    ui_text(c, x + 44, y + 2 + (h - 36) / 4, m->name, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_LARGE, 1);
    ui_gender(c, x + w - 20, y + 2 + (h - 36) / 4, m->gender, UI_FONT_LARGE);
    if (m->level <= 0) {
        return;
    }
    snprintf(s, sizeof(s), "%s%d", ui_tr("Lv."), m->level);
    ui_text(c, x + 9, y + h - 13 - (h - 36) / 8, s, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_SMALL, 1);
    if (m->status >= 0) {
        ui_status_badge(c, x + 44, y + h - 13 - (h - 36) / 8, m->status);
    }
    if (m->moving) {
        ui_text(c, x + w - 20 - 30, y + 6, "MOVE", R(0xFFFFFFu), (s32)R(0x0A3050u), UI_FONT_SMALL, 1);
    }
    if (m->note != NULL) {
        ui_text(c, x + 76, y + h - 13 - (h - 36) / 8, m->note, R(rx_mix(m->note_rgb, 0xFFFFFFu, 50, 100)), (s32)R(0x0A3050u),
                UI_FONT_SMALL, 1);
        return;
    }
    /* the green HP tag and the thin bar */
    bx = x + 44;
    bw = w - 44 - 18;
    ui_text(c, bx, y + h / 2 - 3, "HP", R(0x60F070u), (s32)R(0x105020u), UI_FONT_SMALL, 1);
    ui_fill(c, bx + 15, y + h / 2, bw - 15, 5, R(0x202020u), 255);
    ui_fill(c, bx + 16, y + h / 2 + 1, bw - 17, 3, R(0x484848u), 255);
    if (m->maxhp > 0 && m->hp > 0) {
        int fw = (bw - 17) * (m->hp > m->maxhp ? m->maxhp : m->hp) / m->maxhp;
        u32 hi = m->hp * 2 > m->maxhp ? 0x60F070u : m->hp * 5 > m->maxhp ? 0xF8D030u : 0xF85838u;
        u32 lo = m->hp * 2 > m->maxhp ? 0x18B828u : m->hp * 5 > m->maxhp ? 0xD09000u : 0xB02810u;

        fw = fw < 1 ? 1 : fw;
        ui_fill(c, bx + 16, y + h / 2 + 1, fw, 2, R(hi), 255);
        ui_fill(c, bx + 16, y + h / 2 + 3, fw, 1, R(lo), 255);
    }
    snprintf(s, sizeof(s), "%d/%d", m->hp, m->maxhp);
    tw = ui_text_w(s, UI_FONT_SMALL);
    ui_text(c, x + w - 16 - tw, y + h - 13 - (h - 36) / 8, s, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_SMALL, 1);
}

/* The chosen Pokemon in a pane of the plates' make: its picture, and under
 * it its name, level, types and HP (the menu opens over these). */
static void harmonia_party_detail(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m, u32 frame)
{
    int x = r->x, y = r->y, w = r->w, h = r->h, k = 1, pw = 96, phh = 96, py, room, fy, nx, tw;
    char s[48];

    oct(c, x, y, w, h, 8, 0, 0x082438u, 0x082438u, 255);
    oct(c, x, y, w, h, 8, 1, 0x58C0E8u, 0x3C9CCCu, 255);
    oct(c, x, y, w, h, 8, 2, 0x0A2C48u, 0x0A2C48u, 255);
    oct(c, x, y, w, h, 8, 3, 0x0E2238u, 0x143454u, 255);
    if (m == NULL || m->name == NULL) {
        return;
    }
    room = h - 62;
    if (m->front != NULL) {
        pw = m->front->w;
        phh = m->front->h;
        k = (w - 12) / pw < room / phh ? (w - 12) / pw : room / phh;
        k = k < 1 ? 1 : k > 3 ? 3 : k;
    }
    py = y + 6 + (room - phh * k) / 2;
    py = py < y + 4 ? y + 4 : py;
    if (room < phh) {
        py = y + 4 + room - phh;         /* its feet on the room's foot */
    }
    ui_ellipse(c, x + w / 2 - 24 * k - 8, py + phh * k - 7 * k - 2, 48 * k + 16, 9 * k + 4, R(0x081828u), R(0x2C6C9Cu),
               255);
    if (m->front != NULL) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + 3, y + 3, w - 6, h - 6);
        ui_mon_front_draw(c, (u16)m->species, 0, frame, x + (w - pw * k) / 2, py, pw * k, phh * k);
        ui_pop(c, 0, 0, &saved);
    } else if (m->icon != NULL) {
        ui_image(c, x + (w - m->icon->w * 2) / 2, py + 16, m->icon, 2);
    }
    fy = y + h - 56;
    ui_fill(c, x + 8, fy, w - 16, 1, R(0x3C9CCCu), 255);
    nx = ui_text(c, x + 12, fy + 5, m->name, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_LARGE, 1);
    ui_gender(c, nx + 3, fy + 5, m->gender, UI_FONT_LARGE);
    if (m->level <= 0) {
        return;
    }
    snprintf(s, sizeof(s), "%s%d", ui_tr("Lv."), m->level);
    tw = ui_text_w(s, UI_FONT_LARGE);
    ui_text(c, x + w - 12 - tw, fy + 5, s, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_LARGE, 1);
    ui_type_tag(c, x + 12, fy + 22, 46, m->type1);
    if (m->type2 != m->type1) {
        ui_type_tag(c, x + 12 + 50, fy + 22, 46, m->type2);
    }
    snprintf(s, sizeof(s), "%d/%d", m->hp, m->maxhp);
    ui_text(c, x + w - 12 - ui_text_w(s, UI_FONT_SMALL), fy + 24, s, R(RX_WHITE), (s32)R(0x0A3050u), UI_FONT_SMALL, 1);
    ui_text(c, x + 12, fy + 39, "HP", R(0x60F070u), (s32)R(0x105020u), UI_FONT_SMALL, 1);
    ui_bar(c, x + 28, fy + 41, w - 40, 6, m->hp, m->maxhp);
}

/* The DS's white window in its black frame. */
static void harmonia_party_prompt(EidosUiCtx *c, const EidosUiRect *r, const char *text)
{
    ui_fill(c, r->x, r->y, r->w, r->h, R(0x000000u), 255);
    ui_fill(c, r->x + 2, r->y + 2, r->w - 4, r->h - 4, R(0xFFFFFFu), 255);
    ui_fill(c, r->x + 2, r->y + r->h - 5, r->w - 4, 3, R(0xD0D0D8u), 255);
    ui_text(c, r->x + 10, r->y + ui_text_y(r->h, UI_FONT_LARGE), text, R(0x404048u), (s32)R(0xB8B8C0u), UI_FONT_LARGE,
            1);
}

/* The DS's menu button: slate with pointed ends in a double outline; the
 * chosen one deep blue in a white line. */
static void harmonia_party_menu_entry(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int sel,
                                      int across)
{
    (void)across;
    if (h < 20) {
        /* a cell of the turned plate */
        point_box(c, x, y, w, h, 3, 0, sel ? 0xFFFFFFu : 0x74748Cu, sel ? 0xB8E8FFu : 0x74748Cu, 255);
        point_box(c, x, y, w, h, 3, 1, sel ? 0x0A68A4u : 0x40405Au, sel ? 0x085080u : 0x30304Au, 255);
        ui_text(c, x + (w - ui_text_w(label, UI_FONT_SMALL)) / 2, y + ui_text_y(h, UI_FONT_SMALL), label, R(RX_WHITE),
                (s32)R(sel ? 0x043048u : 0x181820u), UI_FONT_SMALL, 1);
        return;
    }
    point_box(c, x, y, w, h, 5, 0, 0x181820u, 0x181820u, 255);
    point_box(c, x, y, w, h, 5, 1, sel ? 0xFFFFFFu : 0xA0A0B8u, sel ? 0xB8E8FFu : 0x74748Cu, 255);
    point_box(c, x, y, w, h, 5, 2, sel ? 0x0C4869u : 0x22222Eu, sel ? 0x0C4869u : 0x22222Eu, 255);
    point_box(c, x, y, w, h, 5, 3, sel ? 0x0A68A4u : 0x40405Au, sel ? 0x085080u : 0x30304Au, 255);
    ui_text(c, x + (w - ui_text_w(label, UI_FONT_LARGE)) / 2, y + ui_text_y(h, UI_FONT_LARGE), label, R(RX_WHITE),
            (s32)R(sel ? 0x043048u : 0x181820u),
            UI_FONT_LARGE, 1);
}


/* A Pokemon's row in a list (the X menu's party, the battle's): the party
 * screen's plate, small. */
static void harmonia_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level,
                               int hp, int maxhp, const UiImage *icon, int selected)
{
    const int h = 30, cut = 4;
    int fainted = level > 0 && hp == 0, nx, tw, bw;
    char s[48];

    (void)slot;
    if (name == NULL) {
        oct(c, x, y, w, h, cut, 1, 0x16304Au, 0x16304Au, 150);
        oct(c, x, y, w, h, cut, 2, 0x10141Cu, 0x10141Cu, 200);
        return;
    }
    if (rx_tone_now() == rx_tone_green && !fainted) {
        /* in battle the DS's plates are green in a lime line */
        oct(c, x, y, w, h, cut, 0, 0x0C2810u, 0x0C2810u, 255);
        oct(c, x, y, w, h, cut, 1, selected ? 0xFFFFFFu : 0x9CF040u, selected ? 0xE0FFC0u : 0x6CC028u, 255);
        oct(c, x, y, w, h, cut, 2, 0x103818u, 0x103818u, 255);
        plate_body(c, x, y, w, h, cut, selected ? 0x48C850u : 0x2C7C38u, selected ? 0x2C9838u : 0x1C5828u, 255);
    } else {
    oct(c, x, y, w, h, cut, 0, selected ? 0x0C4869u : 0x082438u, selected ? 0x0C4869u : 0x082438u, 255);
    oct(c, x, y, w, h, cut, 1, selected ? 0xFFFFFFu : fainted ? 0xE89090u : 0x58C0E8u,
        selected ? 0xDCF4FFu : fainted ? 0xC06060u : 0x3C9CCCu, 255);
    oct(c, x, y, w, h, cut, 2, selected ? 0x0C5C8Cu : 0x0A2C48u, selected ? 0x0C5C8Cu : 0x0A2C48u, 255);
    if (fainted) {
        plate_body(c, x, y, w, h, cut, selected ? 0xF07070u : 0xA03C3Cu, selected ? 0xC84848u : 0x7A2828u, 255);
    } else {
        plate_body(c, x, y, w, h, cut, selected ? 0x28B8FFu : 0x1C6CA8u, selected ? 0x1480C8u : 0x124C80u, 255);
    }
    }
    if (icon != NULL) {
        ui_image(c, x + 5 + (30 - icon->w) / 2, y + (h - icon->h) / 2 - 1, icon, 1);
        /* what it holds, on the icon's feet (ui_party_row_held): the game's own mark */
        ui_held_mark_game(c, x + 5 + (30 - icon->w) / 2, y + (h - icon->h) / 2 - 1, ui_party_row_take_held());
    }
    {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + 38, y, w - 38 - 40, h);
        nx = ui_text(c, x + 38, y + 2, name, R(RX_WHITE), (s32)R(0x0A2018u), UI_FONT_LARGE, 1);
        ui_gender(c, nx + 3, y + 2, gender, UI_FONT_LARGE);
        ui_pop(c, 0, 0, &saved);
    }
    if (level <= 0) {
        return;
    }
    snprintf(s, sizeof(s), "%s%d", ui_tr("Lv."), level);
    tw = ui_text_w(s, UI_FONT_SMALL);
    ui_text(c, x + w - 8 - tw, y + 4, s, R(RX_WHITE), (s32)R(0x0A2018u), UI_FONT_SMALL, 1);
    snprintf(s, sizeof(s), "%d/%d", hp, maxhp);
    tw = ui_text_w(s, UI_FONT_SMALL);
    bw = w - 38 - tw - 14;
    ui_fill(c, x + 38, y + 20, bw, 5, R(0x202020u), 255);
    ui_fill(c, x + 39, y + 21, bw - 2, 3, R(0x484848u), 255);
    if (maxhp > 0 && hp > 0) {
        int fw = (bw - 2) * (hp > maxhp ? maxhp : hp) / maxhp;
        u32 hi = hp * 2 > maxhp ? 0x60F070u : hp * 5 > maxhp ? 0xF8D030u : 0xF85838u;
        u32 lo = hp * 2 > maxhp ? 0x18B828u : hp * 5 > maxhp ? 0xD09000u : 0xB02810u;

        fw = fw < 1 ? 1 : fw;
        ui_fill(c, x + 39, y + 21, fw, 2, R(hi), 255);
        ui_fill(c, x + 39, y + 23, fw, 1, R(lo), 255);
    }
    ui_text(c, x + w - 8 - tw, y + 17, s, R(RX_WHITE), (s32)R(0x0A2018u), UI_FONT_SMALL, 1);
}
/* [/party] */
/* [bag] */
/* ---------------------------------------------------------------- */
/* The Bag                                                           */
/* ---------------------------------------------------------------- */

/*
 * The DS's Bag made one screen: its crimson ground with the rows of small
 * diamonds and the darker bands at its head and foot; the pink card that
 * holds the item's picture and name, the description under it on the dark
 * red; the list's grey labels with pointed ends in a white line; the pocket
 * bar along the bottom, black as the DS's, the pocket shown a pink label.
 */
static void harmonia_bag_layout(UiBagLayout *l)
{
    int m = AT_LEAST(SX(12), 8), top = 32, bar = 24, lw = l->w * 40 / 100, i;
    int foot = l->h - bar - 8, pw = (l->w - 2 * m) / 5;

    l->frame.x = 0;
    l->frame.y = 0;
    l->frame.w = l->w;
    l->frame.h = l->h;
    l->detail.x = m;
    l->detail.y = top;
    l->detail.w = lw - m;
    l->detail.h = foot - top;
    l->list.x = lw + 14;
    l->list.y = top + 2;
    l->list.w = l->w - m - l->list.x;
    l->list.h = foot - l->list.y;
    l->rowh = (l->list.h - 5 * 2) / 6;
    l->rowh = l->rowh < 22 ? 22 : l->rowh > 30 ? 30 : l->rowh;
    for (i = 0; i < 5; i++) {
        l->pocket[i].x = m + i * pw;
        l->pocket[i].y = l->h - bar + 3;
        l->pocket[i].w = pw - 4;
        l->pocket[i].h = bar - 6;
    }
}

static void harmonia_bag_backdrop(EidosUiCtx *c, const UiBagLayout *l, const char *pocket)
{
    int x, y, bar = 24;

    ui_fill(c, 0, 0, l->w, l->h, R(0x8B1029u), 248);
    /* the diamonds: a small mark every 8, each row half a step along */
    for (y = 30; y < l->h - bar; y += 8) {
        for (x = ((y / 8) & 1) ? 4 : 0; x < l->w; x += 8) {
            ui_fill(c, x + 1, y, 1, 3, R(0x7A0C22u), 255);
            ui_fill(c, x, y + 1, 3, 1, R(0x7A0C22u), 255);
        }
    }
    /* the darker band at the head, its edge a row of points */
    ui_fill(c, 0, 0, l->w, 27, R(0x5A0820u), 255);
    for (x = 0; x < l->w; x += 8) {
        ui_fill(c, x + 2, 27, 4, 1, R(0x5A0820u), 255);
        ui_fill(c, x + 3, 28, 2, 1, R(0x5A0820u), 255);
    }
    ui_header(c, 0, 3, l->w, "BAG", pocket, "", UI_PINK);
    /* the pocket bar: the DS's black bar */
    ui_fill(c, 0, l->h - bar, l->w, bar, R(0x000000u), 255);
    ui_fill(c, 0, l->h - bar, l->w, 1, R(0x5A0820u), 255);
    ui_fill(c, 0, l->h - bar + 1, l->w, 1, R(0x292929u), 255);
    rx_tone(rx_tone_crimson);
}

static void harmonia_bag_pocket(EidosUiCtx *c, const EidosUiRect *r, int which, const char *name, int count,
                                int sel, int hover)
{
    int font = ui_text_w(name, UI_FONT_LARGE) <= r->w - 14 ? UI_FONT_LARGE : UI_FONT_SMALL;
    int tw = ui_text_w(name, font);

    (void)which;
    (void)count;
    if (sel) {
        point_box(c, r->x, r->y, r->w, r->h, 4, 0, 0xFFFFFFu, 0xFFFFFFu, 255);
        point_box(c, r->x, r->y, r->w, r->h, 4, 1, 0xF87098u, 0xD83C68u, 255);
    } else if (hover) {
        point_box(c, r->x, r->y, r->w, r->h, 4, 0, 0x5A5A5Au, 0x5A5A5Au, 255);
        point_box(c, r->x, r->y, r->w, r->h, 4, 1, 0x303030u, 0x202020u, 255);
    }
    ui_text(c, r->x + (r->w - tw) / 2, r->y + ui_text_y(r->h, font), name, R(sel || hover ? 0xFFFFFFu : 0xB4B4B4u),
            (s32)R(sel ? 0x8B1029u : 0x292929u), font, 1);
}

/* A row: the DS's grey label, pointed at both ends, in a white line; the
 * chosen one lighter with the cursor's red line. */
static void harmonia_bag_row(EidosUiCtx *c, int x, int y, int w, int h, const UiImage *icon, const char *name,
                             int count, int sel, int hover)
{
    int ty = ui_text_y(h, UI_FONT_LARGE), tx = 12;
    EidosUiRect saved;
    char s[16];

    if (name == NULL) {
        point_box(c, x, y, w, h, 5, 1, 0x6E0C20u, 0x6E0C20u, 170);
        return;
    }
    if (sel) {
        /* the chosen row: light, in a white line inside a black one */
        point_box(c, x - 1, y - 1, w + 2, h + 2, 6, 0, 0x181818u, 0x181818u, 255);
        point_box(c, x, y, w, h, 5, 0, 0xFFFFFFu, 0xFFFFFFu, 255);
        point_box(c, x, y, w, h, 5, 2, 0xF4F4F8u, 0xC4C4D0u, 255);
    } else {
        point_box(c, x, y, w, h, 5, 0, 0x5A0010u, 0x5A0010u, 255);
        point_box(c, x, y, w, h, 5, 1, 0xF0F0F0u, 0xC8C8C8u, 255);
        point_box(c, x, y, w, h, 5, 2, 0x484848u, 0x484848u, 255);
        point_box(c, x, y, w, h, 5, 3, hover ? 0xA4A4ACu : 0x84848Cu, hover ? 0x84848Cu : 0x6C6C74u, 255);
    }
    if (icon != NULL) {
        ui_image(c, x + 9, y + (h - icon->h) / 2, icon, 1);
        tx = 9 + icon->w + 4;
    }
    s[0] = '\0';
    if (count > 0) {
        snprintf(s, sizeof(s), "\xC3\x97%d", count);
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x + tx, y, w - tx - 14 - ui_text_w(s, UI_FONT_LARGE), h);
    ui_text(c, x + tx, y + ty, name, R(sel ? 0x202020u : 0xFFFFFFu), (s32)R(sel ? 0xB4B4BCu : 0x404048u), UI_FONT_LARGE, 1);
    ui_pop(c, 0, 0, &saved);
    if (count > 0) {
        ui_text(c, x + w - 12 - ui_text_w(s, UI_FONT_LARGE), y + ty, s, R(sel ? 0x202020u : 0xFFFFFFu),
                (s32)R(sel ? 0xB4B4BCu : 0x404048u), UI_FONT_LARGE, 1);
    }
}

/* The item's pane: the DS's pink card behind the picture and the name, the
 * description on the dark red under it. */
static void harmonia_bag_detail_frame(EidosUiCtx *c, const EidosUiRect *r, int shown)
{
    int ch = 76, cx = r->x + r->w / 2;

    oct(c, r->x, r->y, r->w, ch, 4, 0, 0x5A0820u, 0x5A0820u, 255);
    oct(c, r->x, r->y, r->w, ch, 4, 1, 0xF87098u, 0xE04870u, 255);
    oct(c, r->x, r->y, r->w, ch, 4, 2, 0xEE527Bu, 0xEE527Bu, 255);
    if (shown) {
        /* the lighter square the DS sets the picture on */
        ui_fill(c, cx - 32, r->y + 5, 64, 50, R(0xF8A8BCu), 255);
        ui_fill(c, cx - 30, r->y + 7, 60, 46, R(0xF890A8u), 255);
    }
    oct(c, r->x, r->y + ch + 6, r->w, r->h - ch - 6, 4, 0, 0x5A0820u, 0x5A0820u, 225);
}
/* [/bag] */
/* [app] */
/* ---------------------------------------------------------------- */
/* A screen's ground and window                                      */
/* ---------------------------------------------------------------- */

/* The DS's grid ground: near black, a faint grid with a mark where its
 * lines cross, in the screen's own tint. */
static void grid_ground(EidosUiCtx *c, int w, int h, u32 base, u32 line, u32 mark)
{
    int x, y;

    ui_fill(c, 0, 0, w, h, R(base), 246);
    for (y = 14; y < h; y += 24) {
        ui_fill(c, 0, y, w, 1, R(line), 255);
    }
    for (x = 12; x < w; x += 24) {
        ui_fill(c, x, 0, 1, h, R(line), 255);
        for (y = 14; y < h; y += 24) {
            ui_fill(c, x - 1, y, 3, 1, R(mark), 255);
            ui_fill(c, x, y - 1, 1, 3, R(mark), 255);
        }
    }
}

/* The Bag's kind of ground: a colour with rows of small diamonds. */
static void diamond_ground(EidosUiCtx *c, int w, int h, u32 base, u32 dot, u32 head)
{
    int x, y;

    ui_fill(c, 0, 0, w, h, R(base), 248);
    for (y = 30; y < h; y += 8) {
        for (x = ((y / 8) & 1) ? 4 : 0; x < w; x += 8) {
            ui_fill(c, x + 1, y, 1, 3, R(dot), 255);
            ui_fill(c, x, y + 1, 3, 1, R(dot), 255);
        }
    }
    ui_fill(c, 0, 0, w, 27, R(head), 255);
}

/* A pane of the party's make: a dark body in a light line and a dark
 * outline, its corners cut. */
static void pane(EidosUiCtx *c, const EidosUiRect *r, u32 line, u32 top, u32 bot)
{
    oct(c, r->x, r->y, r->w, r->h, 8, 0, 0x06101Cu, 0x06101Cu, 255);
    oct(c, r->x, r->y, r->w, r->h, 8, 1, rx_mix(line, 0xFFFFFFu, 25, 100), line, 255);
    oct(c, r->x, r->y, r->w, r->h, 8, 2, 0x06101Cu, 0x06101Cu, 255);
    oct(c, r->x, r->y, r->w, r->h, 8, 3, top, bot, 255);
}

/* A screen of the device: its black bezel, the grey glass of the DS's
 * closed Pokedex, the graph paper coming on over it (lit 0..256). */
static void dex_glass(EidosUiCtx *c, const EidosUiRect *r, int lit, u32 paper, u32 grid, u32 line)
{
    int x, y, a = lit >= 256 ? 255 : lit * 255 / 256;

    ui_fill(c, r->x - 4, r->y - 4, r->w + 8, r->h + 8, R(0x141416u), 255);
    if (a < 255) {
        for (y = 0; y < r->h; y++) {
            ui_fill(c, r->x, r->y + y, r->w, 1, R(rx_mix(0xA6A6AAu, 0x58585Cu, y, r->h - 1)), 255);
        }
    }
    if (a > 0) {
        ui_fill(c, r->x, r->y, r->w, r->h, R(paper), a);
        for (y = r->y + 6; y < r->y + r->h; y += 8) {
            ui_fill(c, r->x, y, r->w, 1, R(grid), a);
        }
        for (x = r->x + 6; x < r->x + r->w; x += 8) {
            ui_fill(c, x, r->y, 1, r->h, R(grid), a);
        }
        ui_fill(c, r->x - 1, r->y - 1, r->w + 2, 1, R(line), a);
        ui_fill(c, r->x - 1, r->y + r->h, r->w + 2, 1, R(line), a);
        ui_fill(c, r->x - 1, r->y, 1, r->h, R(line), a);
        ui_fill(c, r->x + r->w, r->y, 1, r->h, R(line), a);
    }
}

/* The Pokedex as the DS's own device (its opening screen, lying in
 * landscape): the charcoal body, the red ball lid, the lower half sliding
 * out from under the upper with the green tab on its foot. */
static void harmonia_dex(EidosUiCtx *c, int open)
{
    const u32 ink = 0x1A1A1Cu, top = 0x6A6A70u, bot = 0x404046u;
    RxDex g;

    rx_dex_geom(c, open, &g);
    if (g.slide > 0) {
        int x = g.s2.x - 14, w = g.l_end - x, y = g.top + 3, h = g.hh - 6, tx = g.s2.x + g.sw * 62 / 100;

        ui_fill(c, tx, y + h - 2, 26, 5, R(ink), 255);
        ui_fill(c, tx + 1, y + h - 1, 24, 3, R(0x30B868u), 255);
        ui_fill(c, tx + 1, y + h - 1, 24, 1, R(0x7CE0A4u), 255);
        oct(c, x, y, w, h, 8, 0, ink, ink, 255);
        oct(c, x, y, w, h, 8, 2, rx_mix(top, 0x000000u, 12, 100), rx_mix(bot, 0x000000u, 12, 100), 255);
        dex_glass(c, &g.s2, g.lit, 0x08B47Bu, 0x008362u, 0x94EE18u);
    }
    oct(c, g.x0, g.top, g.u_end - g.x0, g.hh, 9, 0, ink, ink, 255);
    oct(c, g.x0, g.top, g.u_end - g.x0, g.hh, 9, 2, top, bot, 255);
    ui_fill(c, g.x0 + 12, g.top + 2, g.u_end - g.x0 - 24, 1, R(rx_mix(top, 0xFFFFFFu, 38, 100)), 255);
    if (g.slide > 0) {
        ui_fill(c, g.u_end, g.top + 9, 3, g.hh - 18, R(0x000000u), 80);     /* the upper half's shadow */
    }
    rx_dex_lid(c, &g, 0xEE4A22u, top, ink, 0x08B47Bu, 0x008362u, 0x94EE18u, 1, 9, 9);
    dex_glass(c, &g.s1, g.lit, 0x08B47Bu, 0x008362u, 0x94EE18u);
}

/* The Trainer Card's stage colours: green, purple, red, silver, gold, black. */
static const u32 k_card_stage[6] = { 0x58C058u, 0xA878E0u, 0xE85858u, 0xC8CCD8u, 0xF0C028u, 0x40404Cu };

static int card_stage(const char *app)
{
    return strncmp(app, "card", 4) == 0 && app[4] >= '0' && app[4] <= '5' && app[5] == '\0' ? app[4] - '0' : -1;
}

/*
 * Each screen on the ground of its own DS screen, not in Kosmos's window
 * over the world: the summary, the options and the settings on the party's
 * dark grid; the Pokedex on its red; the shop on blue diamonds, as the Bag's
 * crimson; the save and the Trainer Card as a pane on the grid.
 */
static int harmonia_app_frame(EidosUiCtx *c, const char *app, EidosUiRect *frame)
{
    int w = c->canvas->w, h = c->canvas->h - ui_bar_h(c);

    if (strcmp(app, "press-start") == 0) {
        /* the DS's: the words alone over the scene, a soft shade behind */
        int i;

        for (i = 0; i < frame->h; i++) {
            int d = i < frame->h - 1 - i ? i : frame->h - 1 - i;

            ui_fill(c, 0, frame->y + i, w, 1, R(0x000000u), d * 150 / (frame->h / 2));
        }
        return 1;
    }
    if (strcmp(app, "style-flag") == 0) {
        return 0;       /* the title menu's flag: the menu's own box (the screen draws it through the panel) */
    }
    if (strcmp(app, "sheet") == 0) {
        /* a sheet hanging from the top bar: the bar's black, its bevelled
         * foot, a grey line down its sides */
        int x = frame->x, y = frame->y, fw = frame->w, fh = frame->h;

        ui_fill(c, x, y, fw, fh, R(0x181818u), 245);
        ui_fill(c, x, y, 1, fh, R(0x4A5262u), 255);
        ui_fill(c, x + fw - 1, y, 1, fh, R(0x4A5262u), 255);
        ui_fill(c, x, y + fh - 2, fw, 1, R(0x313139u), 255);
        ui_fill(c, x, y + fh - 1, fw, 1, R(0x4A5262u), 255);
        ui_fill(c, x, y + fh, fw, 1, R(0x7B8B9Cu), 255);
        ui_fill(c, x, y + fh + 1, fw, 2, R(0x000000u), 60);
        return 1;
    }
    if (strcmp(app, "bag-pockets") == 0) {
        /* the battle Bag's pockets: a column, and the last items used as a list beside it */
        frame->x = 1;
        return 1;
    }
    if (strncmp(app, "cgear-", 6) == 0) {
        /* The C-Gear's sheet: a list under the C-GEAR button, the three
         * channels one under the other and the C-Gear itself last, in the
         * bar's own look (its sheet and its buttons). */
        static int sx, sy;
        const int rw = 74, rh = 25, pitch = 28;
        EidosUiRect r;

        if (strcmp(app, "cgear-sheet") == 0) {
            r.w = rw + 12;
            r.h = frame->w * pitch + 6;
            sx = frame->x + r.w > w - 4 ? w - 4 - r.w : frame->x;
            sy = frame->y;
            r.x = sx;
            r.y = sy;
            return harmonia_app_frame(c, "sheet", &r);
        }
        r.x = sx + 6;
        r.y = sy + 4 + frame->x * pitch;
        r.w = rw;
        r.h = rh;
        if (strcmp(app, "cgear-row-rect") == 0) {
            *frame = r;
            return 1;
        }
        if (strcmp(app, "cgear-row") == 0) {
            EidosUiRect t = r;

            t.h = frame->y ? -rh : rh;
            (void)harmonia_app_frame(c, "sheet-tile", &t);
            frame->x = r.x + 11;
            frame->y = r.y + 2;
            frame->w = 0;               /* upright */
            frame->h = 0;               /* white letters */
            return 1;
        }
        return 0;
    }
    if (strcmp(app, "sheet-tile") == 0) {
        /* a tile of a sheet: the bar's own button (a dark box in a grey
         * line), lighter under the pointer */
        int hot = frame->h < 0, h2 = hot ? -frame->h : frame->h;

        ui_fill(c, frame->x, frame->y, frame->w, h2, R(0x101010u), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, h2 - 2, R(hot ? 0x7B8B9Cu : 0x4A5262u), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, h2 - 4, R(hot ? 0x3A3A44u : 0x24242Au), 255);
        return 1;
    }
    if (strcmp(app, "bar-button") == 0) {
        return 0;
    }
    if (strcmp(app, "card-case") == 0) {
        /* the DS's badge case: dark olive in a pale line */
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x181810u), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0x8C8C6Cu), 255);
        rx_slab(c, frame->y, frame->h, (frame->x + 2) * FP, (frame->x + 2) * FP, (frame->x + frame->w - 2) * FP,
                (frame->x + frame->w - 2) * FP, 2, frame->h - 2, 0, 0, 0x4A4A30u, 0x343420u, 255);
        ui_fill(c, frame->x + frame->w / 2, frame->y + 2, 1, frame->h - 4, R(0x8C8C6Cu), 255);
        return 1;
    }
    if (strcmp(app, "card-arr") == 0) {
        return 0;
    }
    if (strcmp(app, "options-arr") == 0) {
        frame->w = 1;
        return 1;
    }
    if (strcmp(app, "dex-flip") == 0 || strcmp(app, "dex-size") == 0) {
        return 0;                       /* the device has its own two screens */
    }
    if (strcmp(app, "item-card") == 0) {
        /* an item obtained: the DS's white window in its dark frame, a teal
         * rule along its foot; ink letters (the answer 1) */
        oct(c, frame->x - 1, frame->y - 1, frame->w + 2, frame->h + 2, 5, 0, 0x202020u, 0x202020u, 255);
        oct(c, frame->x - 1, frame->y - 1, frame->w + 2, frame->h + 2, 5, 1, 0x6C6C74u, 0x6C6C74u, 255);
        oct(c, frame->x - 1, frame->y - 1, frame->w + 2, frame->h + 2, 5, 2, 0xFFFFFFu, 0xE4E4ECu, 255);
        ui_fill(c, frame->x + 36, frame->y + frame->h - 5, frame->w - 44, 1, R(0x18CDACu), 255);
        frame->h = 1;
        return 1;
    }
    if (strcmp(app, "item-well") == 0) {
        /* the picture's cell: the DS's grey square */
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x505058u), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0xC4C4CCu), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, 1, R(0xFFFFFFu), 255);
        return 1;
    }
    if (strncmp(app, "starter-", 8) == 0 && strcmp(app, "starter-pic") != 0) {
        /* The DS's two screens for the chosen Pokemon on one.  The upper
         * screen (the stage) is a column down the left edge, as wide as the
         * room beside the ball; the lower screen's pieces keep their places
         * round the ball: the question under it, the answers at its right. */
        static int foot;
        int W = w / 2 - 132 < 104 ? 104 : w / 2 - 132 > 190 ? 190 : w / 2 - 132;  /* to the ball's glow */
        int cx = W / 2, i;

        if (strcmp(app, "starter-stage") == 0) {
            /* the ground in the Pokemon's colour (the DS's is its type's:
             * orange, green, blue), paler at the top; the DS's beam of light
             * from above, its pool at the feet; the screen's dark edge */
            u32 rgb = (u32)frame->x & 0xFFFFFFu, lit = rx_mix(rgb, 0xFFF4A0u, 62, 100);
            int pw = frame->w > 0 ? frame->w : 64, ph = frame->h > 0 ? frame->h : 64, k = 4;

            foot = h - 84;
            /* the DS's share of the screen: the largest whole size that
             * stays in the column and under half the height */
            while (k > 1 && (pw * k > W - 36 || ph * k > h / 2)) {
                k--;
            }
            rx_slab(c, 0, h, 0, 0, W * FP, W * FP, 0, h, 0, 0, rx_mix(rgb, 0xFFFFFFu, 14, 100),
                    rx_mix(rgb, 0x000000u, 16, 100), 255);
            rx_slab(c, 0, foot, (cx - W / 8) * FP, (cx - W / 3) * FP, (cx + W / 8) * FP, (cx + W / 3) * FP, 0, foot, 0, 0,
                    lit, lit, 150);
            ui_ellipse(c, cx - W * 3 / 8, foot - 15, W * 3 / 4, 30, R(lit), UI_NONE, 140);
            ui_fill(c, W, 0, 2, h, R(0x18181Cu), 255);
            ui_fill(c, W + 2, 0, 1, h, R(0xFFFFFFu), 70);
            frame->x = cx;
            frame->y = foot;
            frame->w = k;
            return 1;
        }
        if (strcmp(app, "starter-caption") == 0) {
            /* the DS's caption: a dark band across the stage under the feet,
             * its two lines centred in white, a pale line under it */
            const char *a = ui_app_text_get(0), *b = ui_app_text_get(1);
            int y0 = foot + 6, bh = 52;

            ui_fill(c, 0, y0, W, bh, R(0x000000u), 125);
            ui_fill(c, 0, y0 + bh, W, 2, R(0xFFFFFFu), 70);
            if (a[0] != '\0' && ui_text_w(a, UI_FONT_LARGE) <= W - 8) {
                ui_text(c, cx - ui_text_w(a, UI_FONT_LARGE) / 2, y0 + 8, a, R(0xF8F8F8u), (s32)R(0x282828u),
                        UI_FONT_LARGE, 1);
                ui_text(c, cx - ui_text_w(b, UI_FONT_LARGE) / 2, y0 + 28, b, R(0xF8F8F8u), (s32)R(0x282828u),
                        UI_FONT_LARGE, 1);
            } else {
                ui_text(c, cx - ui_text_w(b, UI_FONT_LARGE) / 2, y0 + ui_text_y(bh, UI_FONT_LARGE), b, R(0xF8F8F8u),
                        (s32)R(0x282828u), UI_FONT_LARGE, 1);
            }
            return 1;
        }
        if (strcmp(app, "starter-ask") == 0) {
            /* the DS's question: bare white letters under the ball, on the
             * dark foot the DS lays over the scene (no window) */
            const char *q = ui_app_text_get(2);
            int tw = ui_text_w(q, UI_FONT_LARGE), tx = w / 2 - tw / 2;

            if (frame->x == 1) {
                /* over the case: the DS's line on the case's dark front */
                if (tw > w - 40) {
                    return 0;
                }
                ui_text(c, tx, h - 31, q, R(0xF8F8F8u), (s32)R(0x101010u), UI_FONT_LARGE, 1);
                return 1;
            }
            for (i = 0; i < 64; i++) {
                ui_fill(c, W + 3, h - 64 + i, w - W - 3, 1, R(0x000000u), i * 3);
            }
            if (tx < W + 12) {
                tx = W + 12;
            }
            ui_text(c, tx, h - 31, q, R(0xF8F8F8u), (s32)R(0x101010u), UI_FONT_LARGE, 1);
            return 1;
        }
        if (strcmp(app, "starter-answer-rect") == 0) {
            /* the DS's two buttons: stacked at the right of the ball, their
             * foot on the question's ground */
            int n = frame->x, bw = 112, bh = 26;

            frame->x = w - 12 - bw;
            frame->y = h - 56 - 4 - 2 * bh - 4 + n * (bh + 4);
            frame->w = bw;
            frame->h = bh;
            return 1;
        }
        if (strcmp(app, "starter-answer") == 0) {
            /* the DS's task button: navy with pointed ends in a steel line,
             * the chosen one bright blue; the word at its left in white */
            int hot = frame->h < 0, h2 = hot ? -frame->h : frame->h;

            oct(c, frame->x, frame->y, frame->w, h2, 9, 0, 0x0C1428u, 0x0C1428u, 255);
            oct(c, frame->x, frame->y, frame->w, h2, 9, 1, hot ? 0xB0DCF8u : 0x7088B8u, hot ? 0x68B0E8u : 0x485C88u, 255);
            oct(c, frame->x, frame->y, frame->w, h2, 9, 2, hot ? 0x2C80C8u : 0x2A3654u, hot ? 0x185C9Cu : 0x1A2238u, 255);
            ui_text(c, frame->x + 18, frame->y + ui_text_y(h2, UI_FONT_LARGE), ui_app_text_get(3), R(0xF8F8F8u),
                    (s32)R(0x0C1428u), UI_FONT_LARGE, 1);
            return 1;
        }
        return 0;
    }
    if (strcmp(app, "starter-pic") == 0) {
        /* the chosen Pokemon's box: the DS's white window in its dark
         * rounded frame, a grey rule over the name; ink letters (answer 1) */
        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 0, 0x41414Au, 0x41414Au, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 1, 0xC5C5CDu, 0xC5C5CDu, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 2, 0xFFFFFFu, 0xF0F0F6u, 255);
        ui_fill(c, frame->x + 6, frame->y + frame->h - 28, frame->w - 12, 1, R(0xC5C5CDu), 255);
        frame->h = 1;
        return 1;
    }
    if (strcmp(app, "catch-ground") == 0) {
        /* the DS's screen for a caught Pokemon: a dark teal ground with a
         * fine grid (its name entry's), the Pokemon in a teal light */
        int cx = frame->x + frame->w / 2, cy = frame->y + frame->h / 2, r = frame->w * 62 / 100, x, y, i;

        ui_fill(c, 0, 0, w, h, R(0x0C3440u), 255);
        for (y = 6; y < h; y += 8) {
            ui_fill(c, 0, y, w, 1, R(0x124450u), 255);
        }
        for (x = 6; x < w; x += 8) {
            ui_fill(c, x, 0, 1, h, R(0x124450u), 255);
        }
        for (i = 0; i < 6; i++) {
            int rr = r - i * r / 7;

            ui_ellipse(c, cx - rr, cy - rr, 2 * rr, 2 * rr, R(rx_mix(0x10A088u, 0x58F0D0u, i, 5)), UI_NONE, 255);
        }
        return 1;
    }
    if (strcmp(app, "target-mark") == 0) {
        /* a target: the DS battle cursor's red brackets when chosen (the
         * ones round FIGHT on the DS's own command screen), thin white ones
         * on the others that can be chosen */
        int sel = frame->h < 0, h2 = sel ? -frame->h : frame->h, x = frame->x, y = frame->y, fw = frame->w;
        int n = sel ? 12 : 7, t = sel ? 3 : 1, i;
        u32 col = sel ? 0xF83858u : 0xFFFFFFu;

        for (i = 0; i < 4; i++) {
            int cx = (i & 1) ? x + fw : x, cy = (i & 2) ? y + h2 : y;
            int dx = (i & 1) ? -1 : 1, dy = (i & 2) ? -1 : 1;

            if (sel) {
                ui_fill(c, dx > 0 ? cx - 1 : cx - n - 1, dy > 0 ? cy - 1 : cy - t - 1, n + 2, t + 2, R(0x181818u), 255);
                ui_fill(c, dx > 0 ? cx - 1 : cx - t - 1, dy > 0 ? cy - 1 : cy - n - 1, t + 2, n + 2, R(0x181818u), 255);
            }
            ui_fill(c, dx > 0 ? cx : cx - n, dy > 0 ? cy : cy - t, n, t, R(col), sel ? 255 : 150);
            ui_fill(c, dx > 0 ? cx : cx - t, dy > 0 ? cy : cy - n, t, n, R(col), sel ? 255 : 150);
            if (sel) {
                ui_fill(c, dx > 0 ? cx : cx - n, dy > 0 ? cy : cy - 1, n, 1, R(0xFFB8C8u), 255);
            }
        }
        return 1;
    }
    if (strcmp(app, "levelup") == 0) {
        /* the DS's level-up window: white in a dark frame, over the plate;
         * ink letters (the answer 1) */
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x202020u), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0x6C6C74u), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0xF8F8F8u), 255);
        ui_fill(c, frame->x + 2, frame->y + frame->h - 5, frame->w - 4, 3, R(0xD8D8E0u), 255);
        frame->h = 1;
        return 1;
    }
    if (strcmp(app, "trade-head") == 0) {
        /* the trade's two Pokemon: one of the party's blue plates, split
         * down its middle */
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 0, 0x082438u, 0x082438u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 1, 0xFFFFFFu, 0xDCF4FFu, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 2, 0x18A0E8u, 0x0C68B0u, 255);
        ui_fill(c, frame->x + frame->w / 2 - 14, frame->y + 3, 28, frame->h - 6, R(0x0A4C84u), 255);
        return 1;
    }
    if (strcmp(app, "union-trade") == 0 || strcmp(app, "union-lobby") == 0 || strcmp(app, "union-draw") == 0) {
        /* the DS's Union Room screens: grey paper with a fine dot in a dark
         * frame, white slots */
        int x, y;

        ui_fill(c, frame->x + 3, frame->y + frame->h, frame->w - 3, 3, R(0x000000u), 90);
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x30303Au), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0x8C8C94u), 255);
        for (y = frame->y + 4; y < frame->y + frame->h - 3; y += 4) {
            for (x = frame->x + 4 + ((y / 4) & 1) * 2; x < frame->x + frame->w - 3; x += 4) {
                ui_fill(c, x, y, 1, 1, R(0x9C9CA4u), 255);
            }
        }
        rx_paper(1);
        return 1;
    }
    if (strcmp(app, "relearn") == 0 || strcmp(app, "relearn-list") == 0) {
        /* the DS's move screens: the summary's ruled paper in a dark frame */
        int y;

        ui_fill(c, frame->x + 3, frame->y + frame->h, frame->w - 3, 3, R(0x000000u), 90);
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x30303Au), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0xD4D4D8u), 255);
        for (y = frame->y + 8; y < frame->y + frame->h - 3; y += 8) {
            ui_fill(c, frame->x + 2, y, frame->w - 4, 1, R(0xC4C4CCu), 255);
        }
        rx_paper(1);
        return 1;
    }
    if (strcmp(app, "save-pic") == 0) {
        return 0;                       /* the DS's report has no picture */
    }
    if (strcmp(app, "bag-menu-row") == 0) {
        /* the picked row turned over for its choices: a dark plate in the
         * row's shape, in a white line */
        point_box(c, frame->x, frame->y, frame->w, frame->h, 5, 0, 0xF81038u, 0xF81038u, 255);
        point_box(c, frame->x, frame->y, frame->w, frame->h, 5, 1, 0xFFFFFFu, 0xE0E0E0u, 255);
        point_box(c, frame->x, frame->y, frame->w, frame->h, 5, 2, 0x26262Eu, 0x1A1A22u, 255);
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
        /* the DS's black head over the paper, a lime line under it */
        ui_fill(c, frame->x, frame->y, frame->w, 24, R(0x181818u), 255);
        ui_fill(c, frame->x, frame->y + 24, frame->w, 1, R(0x94EE18u), 255);
        return 1;
    }
    if (strcmp(app, "dex-panel") == 0) {
        /* the first screen: a black head for the name, the DS's grey band
         * across the paper where the Pokemon stands */
        int ph = frame->h - 100;

        ui_fill(c, frame->x, frame->y, frame->w, 54, R(0x181818u), 255);
        ui_fill(c, frame->x, frame->y + 54, frame->w, 1, R(0x94EE18u), 255);
        ui_fill(c, frame->x, frame->y + 56 + ph * 38 / 100, frame->w, ph * 50 / 100, R(0x52525Au), 235);
        ui_fill(c, frame->x, frame->y + 56 + ph * 38 / 100, frame->w, 1, R(0x303034u), 255);
        ui_fill(c, frame->x, frame->y + 56 + ph * 88 / 100, frame->w, 1, R(0x303034u), 255);
        return 1;
    }
    if (strncmp(app, "dex-sect-", 9) == 0) {
        /* a page of the DS's entry as a box: a black head with the page's
         * hatch, its body the page's own paper (INFO's is the dark band the
         * DS writes the text on) */
        u32 paper = 0x303034u, grid = 0x303034u, mark = 0x94EE18u;
        int x = frame->x, y = frame->y, w = frame->w, h = frame->h, i;

        if (app[9] == 'a') {
            /* the map stands on black in the page's orange line */
            ui_fill(c, x - 1, y - 1, w + 2, h + 2, R(0x101010u), 255);
            ui_fill(c, x, y + 13, w, h - 13, R(0xF8A018u), 255);
            ui_fill(c, x + 1, y + 14, w - 2, h - 15, R(0x181818u), 255);
            for (i = 0; i < 3; i++) {
                rx_slab(c, y + 2, 9, (x + 6 + i * 4) * FP, (x + 2 + i * 4) * FP, (x + 8 + i * 4) * FP, (x + 4 + i * 4) * FP,
                        0, 9, 0, 0, 0xF8A018u, 0xF8A018u, 255);
            }
            ui_fill(c, x, y, w, 13, R(0x181818u), 255);
            for (i = 0; i < 3; i++) {
                rx_slab(c, y + 2, 9, (x + 6 + i * 4) * FP, (x + 2 + i * 4) * FP, (x + 8 + i * 4) * FP, (x + 4 + i * 4) * FP,
                        0, 9, 0, 0, 0xF8A018u, 0xF8A018u, 255);
            }
            return 1;
        } else if (app[9] == 'c') {
            paper = 0xA868F0u; grid = 0x8C4CD8u; mark = 0xB888FFu;
        } else if (app[9] == 'f') {
            paper = 0x28A0F0u; grid = 0x1484D0u; mark = 0x58C0FFu;
        }
        ui_fill(c, x - 1, y - 1, w + 2, h + 2, R(0x101010u), 255);
        ui_fill(c, x, y, w, 13, R(0x181818u), 255);
        ui_fill(c, x, y + 13, w, h - 13, R(paper), 255);
        if (grid != paper) {
            for (i = y + 13 + 6; i < y + h; i += 8) {
                ui_fill(c, x, i, w, 1, R(grid), 255);
            }
            for (i = x + 6; i < x + w; i += 8) {
                ui_fill(c, i, y + 13, 1, h - 13, R(grid), 255);
            }
        }
        for (i = 0; i < 3; i++) {
            rx_slab(c, y + 2, 9, (x + 6 + i * 4) * FP, (x + 2 + i * 4) * FP, (x + 8 + i * 4) * FP, (x + 4 + i * 4) * FP, 0,
                    9, 0, 0, mark, mark, 255);
        }
        ui_fill(c, x, y + 13, w, 1, R(mark), 255);
        return 1;
    }
    if (strncmp(app, "dex-row", 7) == 0) {
        /* the DS's row: a black bar, its left end pointed, in a lime line;
         * the chosen one the lid's orange in a white line */
        int sel = strcmp(app, "dex-row-sel") == 0, hot = strcmp(app, "dex-row-hot") == 0;
        u32 line = sel ? 0xFFFFFFu : hot ? 0xD5FF7Bu : 0x94EE18u;
        u32 top = sel ? 0xF07038u : hot ? 0x303030u : 0x202020u, bot = sel ? 0xC84818u : 0x181818u;
        int x = frame->x, y = frame->y, w = frame->w, h = frame->h, p = h / 4;

        point_box(c, x, y, w, h, p, 0, 0x101010u, 0x101010u, 255);
        ui_fill(c, x + w - p - 2, y, p + 2, h, R(0x101010u), 255);
        point_box(c, x, y, w, h, p, 1, line, line, 255);
        ui_fill(c, x + w - p - 2, y + 1, p + 1, h - 2, R(line), 255);
        point_box(c, x, y, w, h, p, 2, top, bot, 255);
        rx_slab(c, y + 2, h - 4, (x + w - p - 4) * FP, (x + w - p - 4) * FP, (x + w - 2) * FP, (x + w - 2) * FP, 0,
                h - 4, 0, 0, top, bot, 255);
        return 1;
    }
    if (strcmp(app, "summary-side") == 0 || strcmp(app, "summary-card") == 0) {
        return 1;                       /* the Pokemon at the right, on the band, in no box */
    }
    if (strcmp(app, "summary") == 0) {
        /* the DS's summary: light ruled paper, a dark band leaning across
         * its right, where the Pokemon stands */
        int y;

        ui_fill(c, 0, 0, w, h, R(0xD4D4D8u), 250);
        for (y = 6; y < h; y += 8) {
            ui_fill(c, 0, y, w, 1, R(0xC4C4CCu), 255);
        }
        /* the DS's grey band, slanted, a teal line along its edge */
        rx_slab(c, 0, h, (w * 62 / 100 + h / 4) * FP, (w * 62 / 100 - h / 4) * FP, (w + h) * FP, (w + h) * FP, 0, h,
                0, 0, 0x52525Au, 0x3A3A44u, 255);
        rx_slab(c, 0, h, (w * 62 / 100 + h / 4) * FP, (w * 62 / 100 - h / 4) * FP, (w * 62 / 100 + h / 4 + 2) * FP,
                (w * 62 / 100 - h / 4 + 2) * FP, 0, h, 0, 0, 0x18CDACu, 0x10AC9Cu, 255);
        rx_slab(c, 0, h, (w * 62 / 100 + h / 4 - 6) * FP, (w * 62 / 100 - h / 4 - 6) * FP,
                (w * 62 / 100 + h / 4 - 3) * FP, (w * 62 / 100 - h / 4 - 3) * FP, 0, h, 0, 0, 0x292929u, 0x292929u, 255);
        {
            /* the DS's checker on the band */
            int cx, cy2;

            for (cy2 = 0; cy2 < h; cy2 += 8) {
                int edge = w * 62 / 100 + h / 4 - cy2 / 2 + 3;

                for (cx = (edge + 7) / 8 * 8; cx < w; cx += 8) {
                    if (((cx / 8) + (cy2 / 8)) & 1) {
                        ui_fill(c, cx, cy2, 8, 8, R(0xFFFFFFu), 13);
                    } else {
                        ui_fill(c, cx, cy2, 8, 8, R(0x000000u), 16);
                    }
                }
            }
        }
        rx_paper(1);
        frame->x = -4;
        frame->y = -1;
        frame->w = w + 8;
        frame->h = h + 1;
        return 1;
    }
    if (strcmp(app, "pc-detail") == 0) {
        /* the DS's upper screen: a white plate in a dark line on the paper,
         * a darker band where the Pokemon stands */
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x30303Au), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0xF8F8F8u), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0xE4E4E8u), 255);
        ui_fill(c, frame->x + 2, frame->y + 26, frame->w - 4, frame->h * 40 / 100, R(0xC8C8D0u), 255);
        ui_fill(c, frame->x + 2, frame->y + 25, frame->w - 4, 1, R(0x9C9CA4u), 255);
        return 1;
    }
    if (strcmp(app, "pc-cell") == 0) {
        /* the DS's cell: a white shape with cut corners on the wallpaper */
        int hot = frame->h < 0, ph = hot ? -frame->h : frame->h;

        oct(c, frame->x, frame->y, frame->w, ph, frame->w / 4, 0, hot ? 0xF85838u : 0x4C8C3Cu, hot ? 0xF85838u : 0x4C8C3Cu,
            255);
        oct(c, frame->x, frame->y, frame->w, ph, frame->w / 4, 1, 0xFFFFFFu, 0xE8F0E4u, 255);
        return 1;
    }
    if (strcmp(app, "pc-box") == 0) {
        /* the box: the DS's green wallpaper in a dark frame */
        int x, y;

        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x30303Au), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0xF0F0F0u), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0x8CC870u), 255);
        for (y = frame->y + 2; y < frame->y + frame->h - 2; y += 4) {
            for (x = frame->x + 2 + ((y / 4) & 1) * 4; x < frame->x + frame->w - 4; x += 8) {
                ui_fill(c, x, y, 2, 2, R(0x7CB860u), 255);
            }
        }
        return 1;
    }
    if (strcmp(app, "pc") == 0) {
        /* the DS's PC: grey paper with a fine dot */
        int x, y;

        ui_fill(c, 0, 0, w, h, R(0x8C8C94u), 250);
        for (y = 2; y < h; y += 4) {
            for (x = 2 + ((y / 4) & 1) * 2; x < w; x += 4) {
                ui_fill(c, x, y, 1, 1, R(0x9C9CA4u), 255);
            }
        }
        rx_paper(1);
        return 1;
    }
    if (strcmp(app, "dex") == 0) {
        /* the DS's Pokedex, the device itself (frame->x: how far open) */
        static const u32 dex[6] = { 0x838383u, 0x202020u, 0x181818u, 0xFFFFFFu, 0xF07038u, 0xC84818u };

        harmonia_dex(c, frame->x);
        rx_tone(dex);
        rx_accent(0xF07038u);
        return 1;
    }
    if (strcmp(app, "shop") == 0) {
        pane(c, frame, 0x58B0F0u, 0x0A2C50u, 0x0E3C68u);
        rx_tone(rx_tone_blue);
        return 1;
    }
    if (strcmp(app, "namein") == 0 || strcmp(app, "xtrans") == 0) {
        /* the DS's name entry: a teal grid, the keys dark with a teal line */
        static const u32 teal[6] = { 0x2CA0A0u, 0x0E3040u, 0x08222Eu, 0xFFFFFFu, 0x24C0C0u, 0x108C8Cu };

        pane(c, frame, 0x2CA0A0u, 0x08202Cu, 0x0C2C3Cu);
        rx_tone(teal);
        return 1;
    }
    if (strcmp(app, "save") == 0) {
        /* the DS's report: white ruled paper in a dark frame, the world
         * still in view behind it */
        int y;

        ui_fill(c, frame->x + 3, frame->y + frame->h, frame->w - 3, 3, R(0x000000u), 90);
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x30303Au), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0xF8F8F8u), 255);
        ui_fill(c, frame->x + 3, frame->y + 3, frame->w - 6, frame->h - 6, R(0xECECF0u), 255);
        for (y = frame->y + 30; y < frame->y + frame->h - 6; y += 12) {
            ui_fill(c, frame->x + 8, y, frame->w - 16, 1, R(0xD0D0D8u), 255);
        }
        rx_paper(1);
        return 1;
    }
    if (strcmp(app, "mystery") == 0) {
        /* the DS's Mystery Gift: bright cyan bands of light, a grey glass
         * panel, the entries grey pills in a white line (chosen: pink) */
        static const u32 gift[6] = { 0xE8E8F0u, 0x68787Cu, 0x566468u, 0xFF48B0u, 0x7C8C90u, 0x66767Au };
        int x;

        (void)x;
        ui_fill(c, frame->x, frame->y, frame->w, frame->h, R(0x202428u), 255);
        ui_fill(c, frame->x + 1, frame->y + 1, frame->w - 2, frame->h - 2, R(0x8C9CA0u), 255);
        ui_fill(c, frame->x + 2, frame->y + 2, frame->w - 4, frame->h - 4, R(0x48565Au), 255);
        rx_tone(gift);
        rx_accent(0xFF48B0u);
        return 1;
    }
    if (strcmp(app, "battle-bag") == 0) {
        /* the DS's battle Bag: dark red over a silver foot, the pockets
         * dark dotted tiles in a gold line */
        static const u32 gold[6] = { 0xF0C020u, 0x3C3C3Cu, 0x202020u, 0xFFFFFFu, 0x6C5C20u, 0x4C4010u };
        int y;

        (void)y;
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 0, 0x181008u, 0x181008u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 1, 0xF0C020u, 0xB08810u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 2, 0x181008u, 0x181008u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 3, 0x2C2C2Cu, 0x181818u, 255);
        rx_tone(gold);
        rx_accent(0xF0C020u);
        return 1;
    }
    if (strcmp(app, "card-row") == 0) {
        /* the DS's rows: the words on the plate over a white rule */
        ui_fill(c, frame->x, frame->y + frame->h - 2, frame->w, 1, R(0xFFFFFFu), 230);
        ui_fill(c, frame->x, frame->y + frame->h - 1, frame->w, 1, R(0x000000u), 50);
        return 1;
    }
    if (card_stage(app) >= 0) {
        /* the DS's card: a bright plate in its stage's colour, a darker
         * frame, the Poke Ball's rings pale across it */
        u32 col = k_card_stage[card_stage(app)];
        int cx = frame->x + frame->w * 42 / 100, cy = frame->y + frame->h * 55 / 100, r, i;
        EidosUiRect saved;

        ui_fill(c, frame->x + 3, frame->y + frame->h, frame->w - 3, 3, R(0x000000u), 90);
        oct(c, frame->x, frame->y, frame->w, frame->h, 6, 0, 0x202020u, 0x202020u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 6, 1, rx_mix(col, 0x000000u, 45, 100),
            rx_mix(col, 0x000000u, 55, 100), 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 6, 3, rx_mix(col, 0xFFFFFFu, 55, 100),
            rx_mix(col, 0xFFFFFFu, 55, 100), 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 6, 4, rx_mix(col, 0xFFFFFFu, 12, 100), col, 255);
        ui_push(c, 0, 0, &saved);
        ui_clip(c, frame->x + 5, frame->y + 5, frame->w - 10, frame->h - 10);
        for (i = 0; i < 4; i++) {
            r = frame->h * (95 - i * 22) / 100;
            ui_ellipse(c, cx - r, cy - r, 2 * r, 2 * r, R(i & 1 ? col : rx_mix(col, 0xFFFFFFu, 28, 100)), UI_NONE, 255);
        }
        ui_fill(c, frame->x + 5, cy - 4, cx - frame->x, 8, R(col), 255);
        ui_pop(c, 0, 0, &saved);
        rx_accent(rx_mix(col, 0x000000u, 35, 100));
        rx_paper(card_stage(app) != 5 && card_stage(app) != 1 && card_stage(app) != 2);
        rx_accent(rx_mix(col, 0x000000u, 35, 100));
        return 1;
    }
    if (strcmp(app, "options") == 0 || strcmp(app, "settings") == 0) {
        /* the DS's options: black with a field of green crosses; the boxes
         * purple in a cyan line, the chosen row blue in a white one */
        static const u32 opt[6] = { 0x41DEFFu, 0x393162u, 0x312952u, 0xFFFFFFu, 0x0884C8u, 0x0073B4u };
        int x, y;

        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 0, 0x000000u, 0x000000u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 1, 0x41DEFFu, 0x20948Bu, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 5, 2, 0x000000u, 0x000000u, 255);
        /* the crosses, under the title bar */
        for (y = frame->y + 38; y < frame->y + frame->h - 4; y += 16) {
            for (x = frame->x + 10; x < frame->x + frame->w - 4; x += 16) {
                ui_fill(c, x - 3, y, 7, 1, R(0x183931u), 255);
                ui_fill(c, x, y - 3, 1, 7, R(0x183931u), 255);
                ui_fill(c, x - 1, y, 3, 1, R(0x20948Bu), 255);
                ui_fill(c, x, y - 1, 1, 3, R(0x20948Bu), 255);
            }
        }
        rx_tone(opt);
        return 1;
    }
    if (strcmp(app, "battle-forget") == 0) {
        /* the DS's forget-a-move: its move page is grey-blue ruled paper
         * (0x39414A, rules 0x525A62) under a black header.  The window in
         * that grey-blue, its rows in a silver line (the user's choice of
         * three, 2026-11-11: not the battle party's green).  The title's
         * tab keeps the header's own pink: a grey accent leaves it, and it
         * is the pink of the DS's cursor on this screen. */
        static const u32 silver[6] = { 0xB4BCC8u, 0x525A62u, 0x39414Au, 0xFFFFFFu, 0x7C8894u, 0x5C6874u };

        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 0, 0x101418u, 0x101418u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 1, 0xDEE2EAu, 0x9CA4ACu, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 2, 0x101418u, 0x101418u, 255);
        oct(c, frame->x, frame->y, frame->w, frame->h, 8, 3, 0x39414Au, 0x24282Eu, 255);
        rx_tone(silver);
        rx_accent(0x9CA4ACu);
        return 1;
    }
    if (strcmp(app, "battle-party") == 0) {
        /* the DS's battle party: black with a faint grid, green plates */
        pane(c, frame, 0x9CF040u, 0x101810u, 0x182818u);
        rx_tone(rx_tone_green);
        rx_accent(0x9CF040u);
        return 1;
    }
    /* save, wireless, the Entralink's and the survey's */
    pane(c, frame, 0x58C0E8u, 0x0E2238u, 0x143454u);
    rx_tone(rx_tone_blue);
    return 1;
}

/* A command button anywhere (the battle's party and Bag, a link's): the
 * DS's menu button. */
static void harmonia_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected)
{
    (void)color;
    harmonia_party_menu_entry(c, x, y, w, h, label, selected, 0);
}
/* [/app] */
/* [title] */
/* The title's menu on the DS's make: black boxes in a white line, the
 * chosen one in the cyan line. */
static const u32 k_tone_title[6] = { 0xF0F0F0u, 0x14141Au, 0x000000u, 0x40E8E8u, 0x0C3C44u, 0x062428u };

static void harmonia_title_menu_backdrop(EidosUiCtx *c, const UiTitleMenuLayout *l)
{
    (void)c;
    (void)l;
    rx_tone(k_tone_title);
}
/* [/title] */
const UiThemeOps ui_theme_harmonia = {
    .id = "harmonia",
    .name = "Harmonia",
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
    .cmd_button = harmonia_button,
    .bar = rx_bar,
    .hints = rx_hints,
    .party_row = harmonia_party_row,
    .title_menu_backdrop = harmonia_title_menu_backdrop,
    .app_frame = harmonia_app_frame,
    .bag_detail_frame = harmonia_bag_detail_frame,
    .bag_row = harmonia_bag_row,
    .bag_pocket = harmonia_bag_pocket,
    .bag_backdrop = harmonia_bag_backdrop,
    .bag_layout = harmonia_bag_layout,
    .party_menu_entry = harmonia_party_menu_entry,
    .party_prompt = harmonia_party_prompt,
    .party_detail = harmonia_party_detail,
    .party_plate = harmonia_party_plate,
    .party_backdrop = harmonia_party_backdrop,
    .party_layout = harmonia_party_layout,
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
    .field_menu_layout = harmonia_field_menu_layout,
    .field_menu_button = harmonia_field_menu_button,
    .title_menu_layout = harmonia_title_menu_layout,
    .battle_layout = harmonia_battle_layout,
    .battle_backdrop = harmonia_battle_backdrop,
    .battle_box = harmonia_battle_box,
    .battle_command = harmonia_battle_command,
    .battle_move = harmonia_battle_move,
    .battle_move_info = harmonia_battle_move_info,
    .battle_balls = rx_battle_balls,
    .battle_ability = rx_battle_ability,
};
