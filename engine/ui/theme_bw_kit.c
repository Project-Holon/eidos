/*
 * engine/ui/theme_bw_kit.c -- the kit's pieces for the Black-and-White-styled
 * themes (engine/ui/theme_bw.h): what every screen is built from, in
 * Plasma's and Harmonia's surfaces.
 *
 * Both keep the plates' two surfaces: a dark one (see-through, the DS's dot
 * mesh on large pieces) and the white one, which a chosen piece turns to.
 * An app's own colour (the Bag's red, the party's blue) stays on small
 * parts: a header's cap or tab, a rule, a chosen row's cap.
 *   Plasma    leaning cut corners, drawn smooth; glass rows with an end cap.
 *   Harmonia  the DS menu's folder tiles: square, bevelled, a gloss across
 *             the upper right, a soft shadow; headers are folder tabs.
 *
 * The screens' recipes pass Kosmos's colours (kit.h's UI_*): a piece reads
 * its role from them (a dark fill, or a chosen one) and the colour map
 * turns the dark tints left in direct fills to the plates' greys.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_bw.h"
#include "ui_lang.h"

/* 0 Plasma, 1 Harmonia: the theme that is up. */
static int style(void)
{
    const UiThemeOps *t = ui_theme_active();

    return t != NULL && t->id != NULL && t->id[0] == 'h';
}

static int chan(u32 c, int sh)
{
    return (int)((c >> sh) & 255u);
}

static int luma(u32 c)
{
    return (chan(c, 16) * 3 + chan(c, 8) * 6 + chan(c, 0)) / 10;
}

static int spread(u32 c)
{
    int r = chan(c, 16), g = chan(c, 8), b = chan(c, 0);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);

    return mx - mn;
}

static int peak(u32 c)
{
    int r = chan(c, 16), g = chan(c, 8), b = chan(c, 0);

    return r > g ? (r > b ? r : b) : (g > b ? g : b);
}

/* What a panel is, from the fill and the edge Kosmos gives it. */
enum { ROLE_DARK, ROLE_WHITE, ROLE_COLOUR };

static int role_of(u32 fill, u32 edge, int h)
{
    /* the fills Kosmos marks a chosen piece with */
    static const u32 chosen[] = { UI_CYAN, UI_CYAN2, UI_NAVY2, UI_PINK, UI_PINK2, UI_SKY, UI_DEX, UI_DEX2, UI_CARD };
    unsigned i;

    for (i = 0; i < sizeof(chosen) / sizeof(chosen[0]); i++) {
        if (fill == chosen[i]) {
            return ROLE_WHITE;
        }
    }
    if (luma(fill) >= 170) {
        return ROLE_WHITE;              /* a light box (a text box) */
    }
    if (peak(fill) >= 0x60 && spread(fill) >= 0x30) {
        return ROLE_COLOUR;             /* a type's tag, a pen, a badge */
    }
    /* a small piece lit by a tinted fill and a bright edge (a chosen key,
     * a lit button); a large panel with a bright edge stays dark */
    if (h <= 44 && edge != UI_NONE && spread(fill) >= 0x20 && peak(edge) >= 0xA0) {
        return ROLE_WHITE;
    }
    /* lit by its edge alone: the cursor's colour or white on a plain
     * fill, the pointer's grey fill with a bright edge, a highlight colour
     * that is not a frame's (the sky and cyan frames stay frames) */
    if (h <= 44 && edge != UI_NONE && spread(fill) < 0x20) {
        if (edge == UI_CURSOR || edge == UI_WHITE || (fill == UI_EDGE && peak(edge) >= 0xA0)) {
            return ROLE_WHITE;
        }
        if (peak(edge) >= 0xC0 && spread(edge) >= 0x30 && edge != UI_SKY && edge != UI_CYAN && edge != UI_CYAN_EDGE
            && edge != UI_PINK && edge != UI_LIME && edge != UI_CARD) {
            return ROLE_WHITE;
        }
    }
    return ROLE_DARK;
}

static int is_type_colour(u32 rgb)
{
    int i;

    for (i = 0; i < 18; i++) {
        if (ui_type_color(i) == rgb) {
            return 1;
        }
    }
    return 0;
}

/* Kosmos's own accent and its shades (cyan to teal: a hue of 165 to 196
 * degrees), a type's colour (Ice) apart. */
static int kosmos_cyan(u32 rgb)
{
    int r = chan(rgb, 16), g = chan(rgb, 8), b = chan(rgb, 0);
    int mx = g > b ? g : b, mn = r, hue;

    if (r >= g || r >= b || mx - mn < 0x30) {
        return 0;
    }
    if (is_type_colour(rgb)) {
        return 0;
    }
    hue = g >= b ? 120 + 60 * (b - r) / (mx - mn) : 240 - 60 * (g - r) / (mx - mn);
    return hue >= 165 && hue <= 196;
}

/* The same shade in the plates' gold. */
static u32 to_gold(u32 rgb)
{
    int r = chan(rgb, 16), g = chan(rgb, 8), b = chan(rgb, 0);
    int mx = g > b ? g : b, mn = r;

    if (rx_accent_now() != 0) {
        /* the same shade in the screen's own hue */
        u32 h = rx_mix(rx_accent_now(), 0xFFFFFFu, 25, 100);
        int am = peak(h) > 0 ? peak(h) : 1;

        return (u32)((chan(h, 16) * mx / am) << 16 | (chan(h, 8) * mx / am) << 8 | (chan(h, 0) * mx / am));
    }
    if (rx_is_cool()) {
        /* the same shade in Plasma's blue */
        return (u32)((mn + (mx - mn) * 22 / 100) << 16 | (mn + (mx - mn) * 69 / 100) << 8 | mx);
    }
    return (u32)(mx << 16 | (mn + (mx - mn) * 675 / 1000) << 8 | mn);
}

/* Kosmos's accents for the Pokedex and the Trainer Card as shades of the
 * screen's DS accent, when it names one. */
static u32 accent_swap(u32 rgb)
{
    u32 a = rx_accent_now();

    if (a == 0 || rgb == 0xE6283Cu || rgb == 0xFF6363u) {  /* (a Poke Ball's red and the female sign stay) */
        return rgb;
    }
    if (rgb == UI_DEX || rgb == UI_CARD || rgb == UI_CARD_L) {
        return a;
    }
    if (rgb == UI_DEX2 || rgb == UI_CARD_D) {
        return rx_mix(a, 0x000000u, 45, 100);
    }
    if (rgb == UI_DEX3) {
        return rx_mix(a, 0x000000u, 75, 100);
    }
    {
        /* the screen's other shades of Kosmos's accent (the card's dark
         * greens, the Pokedex's pinks and reds): the same value in the DS
         * accent's hue (a grey accent: plain greys) */
        int r = chan(rgb, 16), g = chan(rgb, 8), b = chan(rgb, 0);
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int green = g > r + 0x10 && g >= b, red = r > g + 0x38 && r > b + 0x28;

        if (!is_type_colour(rgb) && ((spread(a) < 0x20 && green) || (spread(a) >= 0x20 && red))) {
            int am = peak(a) > 0 ? peak(a) : 1;

            return (u32)((chan(a, 16) * mx / am) << 16 | (chan(a, 8) * mx / am) << 8 | (chan(a, 0) * mx / am));
        }
    }
    return rgb;
}

/* The colour a piece keeps of an accent: itself when it has one, else the
 * plates' gold. */
static u32 accent_of(u32 c)
{
    int r = chan(c, 16), g = chan(c, 8), b = chan(c, 0);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);

    if (c == UI_NONE || mx - mn < 0x30) {
        return !style() && rx_accent_now() != 0 ? rx_mix(rx_accent_now(), 0xFFFFFFu, 25, 100) : RX_ACCENT;
    }
    if (style()) {
        return accent_swap(c);
    }
    if (!style() && rx_accent_now() != 0) {
        return rx_mix(rx_accent_now(), 0xFFFFFFu, 25, 100);      /* Plasma: the screen's own hue */
    }
    return !style() && kosmos_cyan(c) ? to_gold(c) : c;
}

/* ---------------------------------------------------------------- */
/* The colour map and the text                                       */
/* ---------------------------------------------------------------- */

/* Kosmos's dark tints (the party's navy, the Bag's and the Pokedex's dark
 * reds, the card's dark green) become the plates' greys; every other colour
 * stands (accents, types, HP). */
u32 rx_color(u32 rgb)
{
    int r = chan(rgb, 16), g = chan(rgb, 8), b = chan(rgb, 0);

    if (style()) {
        return accent_swap(rgb);        /* Harmonia: the DS's own colours */
    }
    {
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);

    if (mx < 0x80 && mx - mn >= 0x10 && !is_type_colour(rgb)) {
        int v = (r * 3 + g * 6 + b) / 10 + 0x10;

        return (u32)(v << 16 | v << 8 | v);
    }
    if (kosmos_cyan(rgb)) {
        return to_gold(rgb);
    }
    }
    return rgb;
}

/* Light letters on the white surface turn to ink. */
void rx_text(u32 *rgb, s32 *shadow, u32 behind)
{
    /* on a tone the chosen part is bright blue or red, not white: letters stay light on it */
    if (luma(behind) > (rx_tone_now() != NULL ? 215 : 170) && luma(*rgb) > 120) {
        *rgb = RX_INK;
        if (*shadow >= 0) {
            *shadow = (s32)RX_INK_SH;
        }
    } else {
        *rgb = rx_color(*rgb);
        if (*shadow >= 0) {
            *shadow = (s32)rx_color((u32)*shadow);
        }
        /* Kosmos's dim greys (an unchosen value, a disabled word) were set
         * against its black, and its tinted letters against its own fills:
         * a colour too near the surface under it is set apart */
        {
            int lb = luma(behind), lt = luma(*rgb), d = lt > lb ? lt - lb : lb - lt;
            int near = spread(*rgb) < 0x18 ? 0x60 : 0x30;

            if (d < near && lb <= 0xA8) {
                int v = lb + 0x70 > 0xF0 ? 0xF0 : lb + 0x70;

                *rgb = (u32)(v << 16 | v << 8 | v);
                if (*shadow >= 0) {
                    *shadow = (s32)0x0C0C0Cu;
                }
            } else if (d < near) {
                *rgb = 0x30303Au;
                if (*shadow >= 0) {
                    *shadow = (s32)0xB4B4BCu;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* Surfaces                                                          */
/* ---------------------------------------------------------------- */

/* One row from xl to xr (1/256 pixels), its ends blended. */
static void span(EidosUiCtx *c, int y, int xl, int xr, u32 col, int a)
{
    int il = (xl + FP - 1) / FP, ir = xr / FP;

    if (xr <= xl) {
        return;
    }
    if (il > ir) {
        ui_fill(c, xl / FP, y, 1, 1, R(col), a * (xr - xl) / FP);
        return;
    }
    if (ir > il) {
        ui_fill(c, il, y, ir - il, 1, R(col), a);
    }
    if (il * FP > xl) {
        ui_fill(c, il - 1, y, 1, 1, R(col), a * (il * FP - xl) / FP);
    }
    if (xr > ir * FP) {
        ui_fill(c, ir, y, 1, 1, R(col), a * (xr - ir * FP) / FP);
    }
}

/* How far row yy of a box h tall with leaning cuts (`ext` rows each, half a
 * pixel a row) stands in at its left and right, in 1/256 pixels. */
static void lean_in(int yy, int h, int cut, int ext, int *l, int *r)
{
    *l = *r = 0;
    if ((cut & UI_CUT_TL) && yy < ext) {
        *l = (ext - yy) * FP / 2 - FP / 4;
    }
    if ((cut & UI_CUT_BL) && yy >= h - ext) {
        *l = (yy - (h - ext) + 1) * FP / 2 - FP / 4;
    }
    if ((cut & UI_CUT_TR) && yy < ext) {
        *r = (ext - yy) * FP / 2 - FP / 4;
    }
    if ((cut & UI_CUT_BR) && yy >= h - ext) {
        *r = (yy - (h - ext) + 1) * FP / 2 - FP / 4;
    }
}

static void mesh(EidosUiCtx *c, int x, int y, int w, int h, int a)
{
    int yy, xx;

    for (yy = 5; yy < h - 4; yy += 5) {
        for (xx = 5; xx < w - 6; xx += 5) {
            ui_fill(c, x + xx, y + yy, 2, 2, R(0x3A3A3Au), a);
        }
    }
}

/* Plasma's surface: a box with leaning cuts.  Dark glass (a line along its
 * top, light or the app's colour; the mesh when large), the white, or a
 * colour. */
static void plasma_surface(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, int role, u32 col,
                           u32 line, int a)
{
    int ext = corner * 2 < h ? corner * 2 : h, yy, l, r;

    if (corner <= 0) {
        cut = 0;
    }
    for (yy = 0; yy < h; yy++) {
        lean_in(yy, h, cut, ext, &l, &r);
        span(c, y + yy, x * FP + l, (x + w) * FP - r, RX_OUTLINE, a);
    }
    for (yy = 1; yy < h - 1; yy++) {
        u32 v = role == ROLE_WHITE ? rx_mix(RX_WHITE, RX_WHITE_LO, yy, h - 1)
                : role == ROLE_COLOUR ? rx_mix(rx_mix(col, 0xFFFFFFu, 30, 100), col, yy, h - 1)
                : yy == 1 ? line : rx_mix(0x363636u, 0x1C1C1Cu, yy, h - 1);

        lean_in(yy, h, cut, ext, &l, &r);
        span(c, y + yy, x * FP + l + (l ? INSET : FP), (x + w) * FP - r - (r ? INSET : FP), v, a);
    }
    if (role == ROLE_DARK && h >= 40 && w >= 60) {
        mesh(c, x, y, w, h, a * 110 / 255);
    }
}

/* Harmonia's surface: a square tile with a light bevel, a soft shadow under
 * a solid one.  Dark (a gloss across the upper right of a small one, the
 * mesh on a large one; the app's colour along its top), the white, or a
 * colour. */
static void harmonia_surface(EidosUiCtx *c, int x, int y, int w, int h, int role, u32 col, u32 line, int a)
{
    int i;
    /* the dark tile in the app's own tint, as the DS's (the party's blue,
     * the Bag's red); a plain fill is the menu's charcoal */
    u32 body = spread(col) >= 0x10 && peak(col) < 0x60 ? col : 0x242424u;
    u32 top = rx_mix(body, 0xFFFFFFu, 12, 100), rim = rx_mix(body, 0xFFFFFFu, 30, 100);

    if (rx_on_paper() && role != ROLE_COLOUR) {
        /* on the DS's paper: a grey box in a thin dark line; chosen, its
         * cyan label */
        ui_fill(c, x, y, w, h, R(role == ROLE_WHITE ? 0x18587Cu : 0x6C6C78u), a);
        if (w < 3 || h < 3) {
            return;
        }
        if (role == ROLE_WHITE) {
            rx_slab(c, y, h, (x + 1) * FP, (x + 1) * FP, (x + w - 1) * FP, (x + w - 1) * FP, 1, h - 1, 0, 0, 0x8CE4FCu,
                    0x48B8E8u, a);
        } else if (h <= 24) {
            ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(0xC8C8D0u), a);
            ui_fill(c, x + 1, y + 1, w - 2, 1, R(0xE8E8ECu), a);
        } else {
            ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(0xE0E0E4u), a);
            ui_fill(c, x + 1, y + 1, w - 2, 1, R(0xF8F8F8u), a);
            ui_fill(c, x + 1, y + h - 2, w - 2, 1, R(0xB8B8C0u), a);
        }
        return;
    }
    if (rx_tone_now() != NULL && role != ROLE_COLOUR) {
        const u32 *t = rx_tone_now();
        int s = role == ROLE_WHITE ? 3 : 0;

        ui_fill(c, x + 1, y, w - 2, h, R(0x06101Cu), a);
        ui_fill(c, x, y + 1, w, h - 2, R(0x06101Cu), a);
        if (w < 5 || h < 5) {
            return;
        }
        ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(t[s]), a);
        rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + w - 2) * FP, (x + w - 2) * FP, 2, h - 2, 0, 0, t[s + 1],
                t[s + 2], a);
        return;
    }
    if (a >= 200 && h >= 16) {
        for (i = 0; i < 3; i++) {
            ui_fill(c, x + 1, y + h + i, w, 1, R(0x000000u), 70 / (1 + i * i));
        }
    }
    ui_fill(c, x, y, w, h, R(RX_OUTLINE), a);
    if (w < 4 || h < 4) {
        return;
    }
    if (role == ROLE_WHITE) {
        rx_slab(c, y, h, (x + 1) * FP, (x + 1) * FP, (x + w - 1) * FP, (x + w - 1) * FP, 1, h - 1, 0, 0, RX_WHITE,
                RX_WHITE_LO, a);
        return;
    }
    if (role == ROLE_COLOUR) {
        ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(rx_mix(col, 0xFFFFFFu, 45, 100)), a);
        ui_fill(c, x + 2, y + 2, w - 3, h - 3, R(col), a);
        return;
    }
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(line != 0x4E4E4Eu ? line : rim), a);
    ui_fill(c, x + 2, y + 2, w - 3, h - 3, R(rx_mix(body, 0x000000u, 45, 100)), a);
    rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + w - 2) * FP, (x + w - 2) * FP, 2, h - 2, 0, 0, top,
            rx_mix(body, 0x000000u, 15, 100), a);
    if (h <= 48 && h >= 14 && w >= 40) {
        rx_slab(c, y, h, (x + w * 45 / 100 + LEAN(h)) * FP, (x + w * 45 / 100 + 4 * LEAN(h)) * FP, (x + w - 2) * FP,
                (x + w - 2) * FP, 2, h - 2, 0, 0, 0xFFFFFFu, 0xFFFFFFu, a * 18 / 255);
    } else if (h > 48 && w >= 60) {
        mesh(c, x, y, w, h, a * 110 / 255);
    }
}

static void surface_as(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, int role, u32 col, u32 line,
                       int a)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (style()) {
        harmonia_surface(c, x, y, w, h, role, col, line, a);
    } else {
        plasma_surface(c, x, y, w, h, cut, corner, role, col, line, a);
    }
}

/* The dark surface with a plain line, or the white. */
static void surface(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, int white, int a)
{
    surface_as(c, x, y, w, h, cut, corner, white ? ROLE_WHITE : ROLE_DARK, 0, style() ? 0x4E4E4Eu : 0x626262u, a);
}

/* The same in an app's tint (Harmonia shows it; Plasma keeps its glass). */
static void surface_tint(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, int white, u32 tint,
                         int a)
{
    surface_as(c, x, y, w, h, cut, corner, white ? ROLE_WHITE : ROLE_DARK, tint, style() ? 0x4E4E4Eu : 0x626262u, a);
}

/* An end cap in `col` at a piece's left (Plasma: leaning with the cut). */
static void cap(EidosUiCtx *c, int x, int y, int h, u32 col, int a)
{
    if (style()) {
        ui_fill(c, x + 2, y + 2, 4, h - 4, R(col), a);
        ui_fill(c, x + 2, y + 2, 4, 1, R(rx_mix(col, 0xFFFFFFu, 45, 100)), a);
        ui_fill(c, x + 6, y + 2, 1, h - 4, R(RX_OUTLINE), a);
    } else {
        rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + 7) * FP, (x + 7) * FP, 2, h - 2, 0, 0,
                rx_mix(col, 0xFFFFFFu, 40, 100), col, a);
        ui_fill(c, x + 7, y + 2, 1, h - 4, R(RX_OUTLINE), a);
    }
}

/* ---------------------------------------------------------------- */
/* The pieces                                                        */
/* ---------------------------------------------------------------- */

void rx_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, u32 fill, u32 edge, int a)
{
    u32 line = style() ? 0x4E4E4Eu : 0x626262u;

    if (fill == UI_NONE) {
        return;
    }
    /* the app's colour, when the edge carries one, along the dark
     * surface's top */
    if (edge != UI_NONE && spread(edge) >= 0x30 && peak(edge) >= 0x70) {
        line = edge;
    }
    /* Harmonia's frame takes the edge's colour whatever it is (the DS's
     * coloured frames) */
    if (style() && edge != UI_NONE && spread(edge) >= 0x20) {
        line = edge;
    }
    if (h <= 20 && w <= 120 && a >= 200) {
        /* a small piece on the white surface (a value on a chosen row): the
         * chosen one is the ink, the others pale */
        int bx = c->ox + x + w / 2, by = c->oy + y + h / 2;

        if (bx >= 0 && by >= 0 && bx < c->canvas->w && by < c->canvas->h) {
            const u8 *px = c->canvas->rgba + ((size_t)by * (size_t)c->canvas->w + (size_t)bx) * 4u;

            if (px[3] >= 200 && px[0] >= 0xD0 && px[1] >= 0xD0 && px[2] >= 0xD0) {
                int role = role_of(fill, edge, h);

                if (role != ROLE_COLOUR) {
                    surface_as(c, x, y, w, h, cut, corner, ROLE_COLOUR, role == ROLE_WHITE ? (rx_on_paper() ? 0x0878C0u : 0x2C2C2Cu) : 0xC4C6D0u,
                               line, a);
                    return;
                }
            }
        }
    }
    if (!style()) {
        int role = role_of(fill, edge, h);

        /* Plasma: Kosmos's cyan on a line or a coloured part is the gold
         * (a type's or a status's small tag keeps its colour) */
        if (kosmos_cyan(line)) {
            line = to_gold(line);
        }
        if (role == ROLE_COLOUR && h > 13 && kosmos_cyan(fill)) {
            fill = to_gold(fill);
        }
        surface_as(c, x, y, w, h, cut, corner, role, fill, line, a);
        return;
    }
    surface_as(c, x, y, w, h, cut, corner, role_of(fill, edge, h), fill, line, a);
}

/* A slab whose left side is upright at xl, its top left corner cut `t` rows
 * deep at 45 degrees (so a tab keeps inside a frame's cut corner); `more`
 * moves the cut's edge further in (a fill inside an outline). */
static void cut_slab(EidosUiCtx *c, int y, int H, int xl, int xr0, int xr1, int r0, int r1, int inl, int inr,
                     u32 top, u32 bot, int a, int t, int more)
{
    int r;

    for (r = r0; r < r1 && r < t; r++) {
        int l = xl + (t - r) * FP - FP / 2 + more;

        rx_slab(c, y, H, l, l, xr0, xr1, r, r + 1, inl, inr, r1 - r0 > 1 ? rx_mix(top, bot, r - r0, r1 - r0 - 1) : top,
                r1 - r0 > 1 ? rx_mix(top, bot, r - r0, r1 - r0 - 1) : top, a);
    }
    if (r1 > t) {
        int from = r0 > t ? r0 : t;

        rx_slab(c, y, H, xl, xl, xr0, xr1, from, r1, inl, inr,
                r1 - r0 > 1 ? rx_mix(top, bot, from - r0, r1 - r0 - 1) : top, bot, a);
    }
}

/* A screen's title: the app's colour on a cap (Plasma) or a folder's tab
 * (Harmonia), the plates' white strip under it. */
void rx_header(EidosUiCtx *c, int x, int y, int w, const char *title, const char *sub, const char *right,
               u32 accent)
{
    const int h = 22;
    int ty = ui_text_y(h - 3, UI_FONT_LARGE), tx;
    u32 col = accent_of(accent);

    if (style() && rx_on_paper()) {
        /* on the DS's paper screens the title stands on a black bar; its
         * corner keeps inside a frame's cut corner */
        int t = rx_frame_corner(c, x, y), r;

        for (r = 0; r < t; r++) {
            int l = (x + t - r) * FP - FP / 2;

            rx_span(c, y + r, l, (x + w) * FP, 0x18181Cu, 255);
            rx_span(c, y + r, l, (x + 4) * FP, col, 255);
        }
        ui_fill(c, x, y + t, w, h - 3 - t, R(0x18181Cu), 255);
        ui_fill(c, x, y + t, 4, h - 3 - t, R(col), 255);
        tx = ui_text(c, x + 12, y + ty, title, R(RX_TEXT), -1, UI_FONT_LARGE, 1) + 6;
    } else if (style()) {
        int tw = ui_text_w(title, UI_FONT_LARGE) + 22;
        /* in a frame with cut corners the tab's own corner is cut with it:
         * the tab never stands over the frame's line or outside it */
        int t = rx_frame_corner(c, x + 1, y);

        /* the tab holds the title; the rest of the bar is the tile's */
        cut_slab(c, y, h - 3, (x + 1) * FP, (x + tw) * FP, (x + tw + 6) * FP, 0, h - 3, 0, 0, RX_OUTLINE, RX_OUTLINE, 255,
                 t, 0);
        cut_slab(c, y, h - 3, (x + 1) * FP, (x + tw) * FP, (x + tw + 6) * FP, 1, h - 3, FP, INSET,
                 rx_mix(col, 0x000000u, 12, 100), rx_mix(col, 0x000000u, 50, 100), 255, t, FP / 2);
        cut_slab(c, y, h - 3, (x + 1) * FP, (x + tw) * FP, (x + tw + 6) * FP, 1, 2, FP, INSET,
                 rx_mix(col, 0xFFFFFFu, 50, 100), rx_mix(col, 0xFFFFFFu, 50, 100), 255, t, FP / 2);
        tx = rx_otext(c, x + 9, y + ty, title, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE) + 14;
    } else {
        /* the bar and its cap lean with the card's cut corner: neither
         * stands outside it */
        rx_slab(c, y, h - 3, (x + LEAN(h - 3)) * FP, x * FP, (x + w) * FP, (x + w) * FP, 0, h - 3, 0, 0, 0x2A2A2Au,
                0x222222u, 255);
        {
            /* the cap, clear of a line that runs along the bar's left end */
            int cx = x + rx_head_pad_now();

            rx_slab(c, y, h - 3, (cx + LEAN(h - 3)) * FP, cx * FP, (cx + 8 + LEAN(h - 3)) * FP, (cx + 8) * FP, 0, h - 3, 0,
                    0, rx_mix(col, 0xFFFFFFu, 40, 100), col, 255);
            tx = rx_otext(c, cx + 9 + LEAN(h - 3) + 6, y + ty, title, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE) + 2;
        }
    }
    ui_fill(c, x, y + h - 3, w, 1, R(RX_OUTLINE), 255);
    ui_fill(c, x, y + h - 2, w, 2, R(RX_WHITE), 255);
    if (sub != NULL && sub[0] != '\0') {
        ui_text(c, tx + 8, y + ty, sub, R(RX_MUTED), (s32)R(0x0C0C0Cu), UI_FONT_LARGE, 1);
    }
    if (right != NULL && right[0] != '\0') {
        ui_text(c, x + w - ui_text_w(right, UI_FONT_LARGE) - 8, y + ty, right, R(RX_TEXT), (s32)R(0x0C0C0Cu),
                UI_FONT_LARGE, 1);
    }
}

/* A list's row: the dark surface, or chosen (or under the pointer) the
 * white with the app's colour on its cap. */
void rx_row(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover, const UiTheme *t)
{
    if (selected || hover) {
        surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, 1, selected ? 255 : 215);
        if (selected && h >= 10) {
            cap(c, x, y, h, accent_of(t != NULL ? t->accent : UI_NONE), 255);
        }
    } else {
        surface_tint(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, 0, t != NULL ? t->fill : 0, 235);
    }
}

void rx_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label, const UiImage *icon, int selected,
                 int hover)
{
    const int h = 26;
    int sel = selected || hover;

    surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BL, 6, sel, sel ? 255 : 235);
    if (icon != NULL) {
        ui_image(c, x + 6 + (22 - icon->w) / 2, y + 2 + (22 - icon->h) / 2, icon, 1);
    }
    rx_label(c, x + 34, y + ui_text_y(h, UI_FONT_LARGE), label, sel, RX_TEXT, UI_FONT_LARGE);
}

void rx_message(EidosUiCtx *c, int x, int y, int w, const char *utf8)
{
    surface(c, x, y, w, 30, UI_CUT_TL | UI_CUT_BR, 6, 0, 235);
    rx_label(c, x + 10, y + ui_text_y(30, UI_FONT_LARGE), utf8, 0, RX_TEXT, UI_FONT_LARGE);
}

/* A tab of a tab bar: chosen, the white surface. */
int rx_tab(EidosUiCtx *c, int x, int y, const char *label, int selected, u32 accent, u32 accent2)
{
    int w = ui_text_w(label, UI_FONT_SMALL) + 18, h = 15;

    (void)accent2;
    surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, selected, 255);
    if (selected) {
        ui_fill(c, x + 3, y + h - 3, w - 6, 1, R(accent_of(accent)), 255);
    }
    rx_label(c, x + (w - ui_text_w(label, UI_FONT_SMALL)) / 2, y + ui_text_y(h, UI_FONT_SMALL), label, selected,
             selected ? RX_TEXT : RX_MUTED, UI_FONT_SMALL);
    return w;
}

/* A move in a list: the type's tag, the name, the PP; chosen, the white
 * surface (Kosmos marks it with the cursor's colour on the edge). */
void rx_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name, int pp, int maxpp, int selected)
{
    const int h = 22;
    char s[24];

    if (style() && rx_on_paper()) {
        /* the DS summary's move: a dark pill with pointed ends in a teal
         * line (the chosen one's line white), the letters white */
        int yy;

        for (yy = 0; yy < h; yy++) {
            int d = 2 * yy - (h - 1), in = (d < 0 ? -d : d) * 5 / (h - 1);
            int edge = yy == 0 || yy == h - 1;

            ui_fill(c, x + in, y + yy, w - 2 * in, 1, R(0x41414Au), name == NULL ? 150 : 255);
            if (!edge) {
                ui_fill(c, x + in + 1, y + yy, w - 2 * in - 2, 1, R(selected ? 0xFFFFFFu : 0x18CDACu), name == NULL ? 150 : 255);
            }
            if (yy >= 2 && yy < h - 2) {
                ui_fill(c, x + in + 2, y + yy, w - 2 * in - 4, 1, R(selected ? 0x2C6058u : 0x394141u), name == NULL ? 150 : 255);
            }
        }
        if (name == NULL) {
            return;
        }
        ui_type_tag(c, x + 8, y + (h - 13) / 2, 42, type);
        ui_text(c, x + 55, y + ui_text_y(h, UI_FONT_LARGE), name, R(0xFFFFFFu), (s32)R(0x202428u), UI_FONT_LARGE, 1);
        snprintf(s, sizeof(s), "PP %d/%d", pp, maxpp);
        ui_text(c, x + w - 12 - ui_text_w(s, UI_FONT_SMALL), y + ui_text_y(h, UI_FONT_SMALL), s,
                R(pp == 0 ? 0xFF8060u : 0xFFFFFFu), (s32)R(0x202428u), UI_FONT_SMALL, 1);
        return;
    }
    if (name == NULL) {
        surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0, 160);
        rx_label(c, x + 52, y + ui_text_y(h, UI_FONT_LARGE), "-", 0, RX_MUTED, UI_FONT_LARGE);
        return;
    }
    surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, selected, 235);
    ui_type_tag(c, x + 5, y + (h - 13) / 2, 42, type);
    rx_label(c, x + 52, y + ui_text_y(h, UI_FONT_LARGE), name, selected, RX_TEXT, UI_FONT_LARGE);
    snprintf(s, sizeof(s), "PP %d/%d", pp, maxpp);
    rx_label(c, x + w - 10 - ui_text_w(s, UI_FONT_SMALL), y + ui_text_y(h, UI_FONT_SMALL), s, selected,
             pp == 0 ? 0xFF6040u : RX_TEXT, UI_FONT_SMALL);
}

/* A command button outside the battle's own (the Bag's and the party's in
 * battle, a link's): the dark surface with its colour on a cap; chosen, the
 * white. */
void rx_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected)
{
    /* the word never runs over the cap: a smaller type when the button is
     * too narrow for it (a longer word in another language) */
    int room = w - 12, font = UI_FONT_LARGE;

    if (ui_text_w(label, font) > room) {
        font = UI_FONT_MEDIUM;
    }
    if (ui_text_w(label, font) > room) {
        font = UI_FONT_SMALL;
    }
    surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, selected, 255);
    cap(c, x, y, h, accent_of(color), 255);
    rx_label(c, x + (w - ui_text_w(label, font)) / 2 + 3, y + ui_text_y(h, font), label, selected,
             selected ? RX_TEXT : rx_mix(accent_of(color), 0xFFFFFFu, 30, 100), font);
}

/* The HP bar of the plates: a dark frame, two greens (yellows, reds). */
void rx_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den)
{
    u32 hi, lo;
    int fw;

    ui_fill(c, x, y, w, h, R(0x303030u), 255);
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(0x585858u), 255);
    if (den <= 0 || num <= 0 || w < 3) {
        return;
    }
    if (num > den) {
        num = den;
    }
    fw = (int)(((long)(w - 2) * num) / den);
    if (fw < 1) {
        fw = 1;
    }
    if (num * 2 > den) {
        hi = 0x00FF4Au;
        lo = 0x00BD20u;
    } else if (num * 5 > den) {
        hi = 0xFFE040u;
        lo = 0xD0A000u;
    } else {
        hi = 0xFF5A3Au;
        lo = 0xC02010u;
    }
    ui_fill(c, x + 1, y + 1, fw, (h - 2 + 1) / 2, R(hi), 255);
    ui_fill(c, x + 1, y + 1 + (h - 2 + 1) / 2, fw, (h - 2) / 2, R(lo), 255);
}

/*
 * A bar's ground.  Harmonia: the DS's near-black status bar, its inner edge
 * bevelled in blue-grey (under the top bar, over a bottom one).  Plasma: the
 * band's glass with the white strip along its inner edge.
 */
void rx_strip(EidosUiCtx *c, int y, int w, int h, int where, int hot)
{
    if (where == 1) {
        rx_paper(0);                    /* a bottom bar ends the screen */
    }
    if (style()) {
        ui_fill(c, 0, y, w, h, R(0x181818u), hot ? 250 : 235);
        if (where == 0) {
            ui_fill(c, 0, y + h - 2, w, 1, R(0x313139u), 255);
            ui_fill(c, 0, y + h - 1, w, 1, R(0x4A5262u), 255);
            ui_fill(c, 0, y + h, w, 1, R(0x7B8B9Cu), 255);
            ui_fill(c, 0, y + h + 1, w, 2, R(0x000000u), 60);
        } else {
            ui_fill(c, 0, y, w, 1, R(0x7B8B9Cu), 255);
            ui_fill(c, 0, y + 1, w, 1, R(0x4A5262u), 255);
            ui_fill(c, 0, y + 2, w, 1, R(0x313139u), 255);
        }
        return;
    }
    if (where == 0) {
        int yy;

        int a = hot ? 235 : 205, g0 = 0, g1 = 0;

        rx_bar_alpha = a;
        if (rx_bar_gap_ttl > 0) {
            rx_bar_gap_ttl--;
            g0 = rx_bar_gap_x0 < 0 ? 0 : rx_bar_gap_x0;
            g1 = rx_bar_gap_x1 > w * FP ? w * FP : rx_bar_gap_x1;
        }
        for (yy = 0; yy < h - 2; yy++) {
            ui_fill(c, 0, y + yy, w, 1, R(rx_mix(0x1A1A1Au, 0x303030u, yy, h - 3)), a);
        }
        if (g1 > g0) {
            /* a sheet hangs here as the bar carried on: no rule over it */
            int rr;

            for (rr = h - 2; rr < h; rr++) {
                int off = (h - rr) * FP / 2;    /* the ends lean: half a pixel a row */

                rx_span(c, y + rr, 0, g0 - off, RX_WHITE, 255);
                rx_span(c, y + rr, g1 - off, w * FP, RX_WHITE, 255);
                rx_span(c, y + rr, g0 - off, g1 - off, 0x303030u, a);
            }
            ui_fill(c, 0, y + h, g0 / FP, 1, R(RX_PLATE_OL), 255);
            ui_fill(c, g1 / FP + 1, y + h, w - g1 / FP - 1, 1, R(RX_PLATE_OL), 255);
        } else {
            ui_fill(c, 0, y + h - 2, w, 2, R(RX_WHITE), 255);
            ui_fill(c, 0, y + h, w, 1, R(RX_PLATE_OL), 255);
        }
    } else {
        ui_fill(c, 0, y, w, h, R(0x161616u), 240);
        ui_fill(c, 0, y, w, 1, R(RX_OUTLINE), 255);
    }
}

/* The clock's tab under the top bar's middle.  Harmonia: a folder's tab
 * hanging from the bar; Plasma: a leaning white chip. */
int rx_clock_x0, rx_clock_x1;
int rx_bar_alpha, rx_bar_gap_x0, rx_bar_gap_x1, rx_bar_gap_ttl;

void rx_clock_tab(EidosUiCtx *c, int cx, int w, int h, int hot)
{
    int x0 = cx - w / 2, x1 = cx + w / 2;

    rx_clock_x0 = x0;
    rx_clock_x1 = x1;

    if (style()) {
        rx_slab(c, 0, h + 3, x0 * FP, (x0 + 4) * FP, x1 * FP, (x1 - 4) * FP, 0, h + 3, 0, 0, RX_OUTLINE, RX_OUTLINE, 255);
        rx_slab(c, 0, h + 3, x0 * FP, (x0 + 4) * FP, x1 * FP, (x1 - 4) * FP, 0, h + 2, FP, FP, hot ? 0x6A6A6Au : 0x525252u,
                hot ? 0x4A4A4Au : 0x393939u, 255);
    } else {
        /* Plasma: a trapezoid hanging from the top edge, wide at the top;
         * the buttons at its two sides lean toward it */
        int k = LEAN(h + 3) / 2 + 1;

        rx_slab(c, 0, h + 3, (x0 - k) * FP, (x0 + k) * FP, (x1 + k) * FP, (x1 - k) * FP, 0, h + 3, 0, 0, RX_OUTLINE,
                RX_OUTLINE, 255);
        rx_slab(c, 0, h + 3, (x0 - k) * FP, (x0 + k) * FP, (x1 + k) * FP, (x1 - k) * FP, 0, h + 2, INSET, INSET, RX_WHITE,
                hot ? RX_WHITE : RX_WHITE_LO, 255);
    }
}

/* The key hints: a dark bar along the bottom (Plasma: the band's foot;
 * Harmonia: the DS's near-black bar with its bevelled top edge). */
int rx_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n, const u32 *keys)
{
    int bh, top = ui_bottom_bar(c, y, 17, &bh), lift = bh - 17, i, x = 6 + ui_safe_left(c);

    rx_paper(0);                        /* the screen's parts are drawn */
    y = top + lift / 2;
    if (lift > 0) {
        c->bar_top = c->oy + top;
    }
    rx_strip(c, top, w, bh, 1, 0);
    for (i = 0; i < n; i++) {
        const char *key = pairs[2 * i], *what = pairs[2 * i + 1];
        int kw = ui_keycap_w(key), aw = ui_text_w(what, UI_FONT_SMALL), state;
        EidosUiRect r;

        if (kw < 13) {
            kw = 13;
        }
        r.x = x - 2;
        r.y = top;
        r.w = kw + 4 + aw + 4;
        r.h = bh;
        state = eidos_ui_hit(r);
        if (state & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, top + 3, r.w, bh - 4, R(0x3A3A3Au), 255);
        }
        if ((state & EIDOS_UI_CLICK) && keys != NULL && keys[i] != 0) {
            eidos_ui_press(keys[i], 2);
        }
        ui_keycap(c, x, y + 2, key);
        x += kw + 4;
        x = ui_text(c, x, y + 3 + ui_text_y(13, UI_FONT_SMALL), what, R(0xD8D8D8u), (s32)R(0x0C0C0Cu), UI_FONT_SMALL,
                    1) + 10;
    }
    return 17 + lift;
}

/* ---------------------------------------------------------------- */
/* The smaller pieces                                                */
/* ---------------------------------------------------------------- */

static u32 plain_line(void)
{
    return style() ? 0x4E4E4Eu : 0x626262u;
}

/* Plasma's leaning box: an outline, then a blend inside it. */
static void lean_box(EidosUiCtx *c, int x, int y, int w, int h, u32 top, u32 bot, int a)
{
    int l = LEAN(h);

    rx_slab(c, y, h, (x + l) * FP, x * FP, (x + w) * FP, (x + w - l) * FP, 0, h, 0, 0, RX_OUTLINE, RX_OUTLINE, a);
    rx_slab(c, y, h, (x + l) * FP, x * FP, (x + w) * FP, (x + w - l) * FP, 1, h - 1, INSET, INSET, top, bot, a);
}

/* Harmonia's small bevelled box. */
static void bevel_box(EidosUiCtx *c, int x, int y, int w, int h, u32 col, int a)
{
    ui_fill(c, x, y, w, h, R(RX_OUTLINE), a);
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(rx_mix(col, 0xFFFFFFu, 45, 100)), a);
    ui_fill(c, x + 2, y + 2, w - 3, h - 3, R(col), a);
}

/* A list's place: Plasma a hairline with a white thumb, Harmonia the DS's
 * boxed track with the app's colour on the thumb. */
void rx_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown, int total, u32 accent)
{
    int th, ty, track = h - 2;
    u32 col = accent_of(accent);

    if (style()) {
        ui_fill(c, x, y, 5, h, R(RX_OUTLINE), 255);
        ui_fill(c, x + 1, y + 1, 3, track, R(0x313139u), 255);
    } else {
        ui_fill(c, x + 2, y, 1, h, R(0x626262u), 220);
    }
    if (total <= 0 || shown >= total) {
        th = track;
        ty = 0;
    } else {
        th = (int)((long)track * shown / total);
        th = th < 8 ? 8 : th > track ? track : th;
        ty = (int)((long)(track - th) * first / (total - shown));
        ty = ty < 0 ? 0 : ty > track - th ? track - th : ty;
    }
    if (style()) {
        ui_fill(c, x + 1, y + 1 + ty, 3, th, R(col), 255);
        ui_fill(c, x + 1, y + 1 + ty, 1, th, R(rx_mix(col, 0xFFFFFFu, 50, 100)), 255);
    } else {
        ui_fill(c, x, y + 1 + ty, 5, th, R(RX_OUTLINE), 255);
        ui_fill(c, x + 1, y + 2 + ty, 3, th - 2, R(RX_WHITE), 255);
    }
}

void rx_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon)
{
    int h = 22, tx = 14, w;

    if (icon != NULL && icon->h + 6 > h) {
        h = icon->h + 6;
    }
    w = ui_text_w(utf8, UI_FONT_LARGE) + 26 + (icon != NULL ? icon->w + 4 : 0);
    surface_as(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, ROLE_DARK, 0, plain_line(), 240);
    cap(c, x, y, h, RX_ACCENT, 255);
    if (icon != NULL) {
        ui_image(c, x + tx, y + (h - icon->h) / 2, icon, 1);
        tx += icon->w + 4;
    }
    rx_label(c, x + tx, y + ui_text_y(h, UI_FONT_LARGE), utf8, 0, RX_TEXT, UI_FONT_LARGE);
}

/* The cursor's four corners: Plasma's in the plates' gold, Harmonia's in
 * the DS's own cursor colour, a light line inside each. */
void rx_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color)
{
    const int arm = 6;
    u32 col = style() ? color : RX_ACCENT;
    int i;

    for (i = 0; i < 2; i++) {
        u32 v = i == 0 ? R(RX_OUTLINE) : (style() ? col : R(col));
        int o = i == 0 ? 1 : 0;

        ui_fill(c, x - o, y - o, arm + o, 2 + o, v, 255);
        ui_fill(c, x - o, y - o, 2 + o, arm + o, v, 255);
        ui_fill(c, x + w - arm, y - o, arm + o, 2 + o, v, 255);
        ui_fill(c, x + w - 2, y - o, 2 + o, arm + o, v, 255);
        ui_fill(c, x + w - arm, y + h - 2, arm + o, 2 + o, v, 255);
        ui_fill(c, x + w - 2, y + h - arm, 2 + o, arm + o, v, 255);
        ui_fill(c, x - o, y + h - 2, arm + o, 2 + o, v, 255);
        ui_fill(c, x - o, y + h - arm, 2 + o, arm + o, v, 255);
    }
    if (style()) {
        ui_fill(c, x, y, arm - 1, 1, R(0xFFFFFFu), 150);
        ui_fill(c, x + w - arm, y, arm - 1, 1, R(0xFFFFFFu), 150);
    }
}

/* A key's cap: Plasma a white leaning chip with an ink letter, Harmonia the
 * DS's blue-grey button.  A pad's glyph stays the pad's own picture. */
int rx_keycap(EidosUiCtx *c, int x, int y, const char *label)
{
    const char *l = ui_key_label(label);
    int tw, w;

    if (l[0] == UI_PAD_TOKEN) {
        return ui_def_keycap(c, x, y, label);
    }
    tw = ui_text_w(l, UI_FONT_SMALL);
    w = tw + 8 > 13 ? tw + 8 : 13;
    if (style()) {
        ui_fill(c, x, y, w, 13, R(RX_OUTLINE), 255);
        ui_fill(c, x + 1, y + 1, w - 2, 11, R(0x7B8B9Cu), 255);
        ui_fill(c, x + 1, y + 2, w - 2, 10, R(0x4A5262u), 255);
        ui_fill(c, x + 1, y + 11, w - 2, 1, R(0x313139u), 255);
        ui_text(c, x + (w - tw) / 2, y + ui_text_y(13, UI_FONT_SMALL) + 1, l, R(RX_TEXT), (s32)R(0x202028u),
                UI_FONT_SMALL, 1);
    } else {
        rx_slab(c, y, 13, (x + 2) * FP, x * FP, (x + w) * FP, (x + w - 2) * FP, 0, 13, 0, 0, RX_OUTLINE, RX_OUTLINE,
                255);
        rx_slab(c, y, 13, (x + 2) * FP, x * FP, (x + w) * FP, (x + w - 2) * FP, 1, 12, INSET, INSET, RX_WHITE,
                RX_WHITE_LO, 255);
        ui_text(c, x + (w - tw) / 2, y + ui_text_y(13, UI_FONT_SMALL) + 1, l, R(RX_INK), -1, UI_FONT_SMALL, 1);
    }
    return w;
}

void rx_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected)
{
    surface_tint(c, x, y, w, 18, UI_CUT_TL | UI_CUT_BR, 4, selected, 0x3C0A1Au, 255);
    if (selected) {
        ui_fill(c, x + 3, y + 14, w - 6, 2, R(UI_PINK), 255);
    }
    rx_label(c, x + 6, y + ui_text_y(18, UI_FONT_LARGE), label, selected, RX_TEXT, UI_FONT_LARGE);
}

/* A switch: the knob is the white surface; on, the track takes the colour. */
void rx_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent)
{
    u32 col = accent_of(accent);

    if (style()) {
        bevel_box(c, x, y, 26, 12, on ? col : 0x313139u, 255);
        ui_fill(c, x + (on ? 14 : 2), y + 2, 10, 8, R(RX_OUTLINE), 255);
        ui_fill(c, x + (on ? 15 : 3), y + 3, 8, 6, R(on ? RX_WHITE : 0x7B8B9Cu), 255);
    } else {
        lean_box(c, x, y, 28, 12, on ? rx_mix(col, 0xFFFFFFu, 30, 100) : 0x363636u, on ? col : 0x1C1C1Cu, 255);
        rx_slab(c, y, 12, (x + (on ? 16 : 6)) * FP, (x + (on ? 13 : 3)) * FP, (x + (on ? 25 : 15)) * FP,
                (x + (on ? 22 : 12)) * FP, 2, 10, 0, 0, on ? RX_WHITE : 0x8A8A8Au, on ? RX_WHITE_LO : 0x6A6A6Au, 255);
    }
}

void rx_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    u32 col = accent_of(accent);
    int fx;

    den = den <= 0 ? 1 : den;
    num = num < 0 ? 0 : num > den ? den : num;
    fx = (int)((long)(w - 2) * num / den);
    fx = fx < 4 ? 4 : fx > w - 5 ? w - 5 : fx;
    ui_fill(c, x, y + 3, w, 4, R(RX_OUTLINE), 255);
    ui_fill(c, x + 1, y + 4, w - 2, 2, R(style() ? 0x313139u : 0x4A4A4Au), 255);
    ui_fill(c, x + 1, y + 4, fx, 1, R(rx_mix(col, 0xFFFFFFu, 40, 100)), 255);
    ui_fill(c, x + 1, y + 5, fx, 1, R(col), 255);
    if (style()) {
        ui_fill(c, x + fx - 4, y, 8, 10, R(RX_OUTLINE), 255);
        ui_fill(c, x + fx - 3, y + 1, 6, 8, R(RX_WHITE), 255);
        ui_fill(c, x + fx - 3, y + 6, 6, 3, R(RX_WHITE_LO), 255);
    } else {
        rx_slab(c, y, 10, (x + fx - 2) * FP, (x + fx - 6) * FP, (x + fx + 6) * FP, (x + fx + 2) * FP, 0, 10, 0, 0,
                RX_OUTLINE, RX_OUTLINE, 255);
        rx_slab(c, y, 10, (x + fx - 2) * FP, (x + fx - 6) * FP, (x + fx + 6) * FP, (x + fx + 2) * FP, 1, 9, INSET,
                INSET, RX_WHITE, RX_WHITE_LO, 255);
    }
}

void rx_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent)
{
    int ty = ui_text_y(15, UI_FONT_LARGE);
    u32 col = rx_mix(accent_of(accent), 0xFFFFFFu, 25, 100);

    surface(c, x, y, w, 15, UI_CUT_TL | UI_CUT_BR, 3, 0, 255);
    ui_text(c, x + 5, y + ty, "\xE2\x86\x90", R(col), -1, UI_FONT_LARGE, 1);
    ui_text(c, x + w - 13, y + ty, "\xE2\x86\x92", R(col), -1, UI_FONT_LARGE, 1);
    rx_label(c, x + (w - ui_text_w(value, UI_FONT_LARGE)) / 2, y + ty, value, 0, RX_TEXT, UI_FONT_LARGE);
}

/* A line being typed: Harmonia's is the DS's white paper, Plasma's the
 * glass over a white rule. */
void rx_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent)
{
    int i, ty = ui_text_y(18, UI_FONT_LARGE), tx, white = style();
    u32 col = accent_of(accent);

    surface(c, x, y, w, 18, UI_CUT_TL | UI_CUT_BR, 4, white, 255);
    if (!white) {
        ui_fill(c, x + 3, y + 15, w - 6, 2, R(RX_WHITE), 255);
    }
    tx = rx_label(c, x + 7, y + ty - (white ? 0 : 1), text, white, RX_TEXT, UI_FONT_LARGE);
    ui_fill(c, tx + 1, y + 3, 1, 11, R(white ? rx_mix(col, 0x000000u, 30, 100) : col), 255);
    for (i = 0; i < room; i++) {
        ui_fill(c, tx + 5 + i * 7, y + 13, 5, 1, R(white ? 0xA0A4ACu : 0x6A6A6Au), 255);
    }
}

void rx_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent)
{
    int tw = ui_text_w(label, UI_FONT_LARGE);

    (void)accent;
    if (w <= 0) {
        w = tw + 8 > 16 ? tw + 8 : 16;
    }
    surface(c, x, y, w, 16, UI_CUT_TL | UI_CUT_BR, 3, selected, 255);
    rx_label(c, x + (w - tw) / 2, y + ui_text_y(16, UI_FONT_LARGE), label, selected, RX_TEXT, UI_FONT_LARGE);
}

typedef struct RxLines {
    EidosUiCtx *c;
    int x, y, n, white;
} RxLines;

static void rx_dialog_line(void *user, const char *s, int len)
{
    RxLines *d = (RxLines *)user;
    char buf[256];

    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }
    memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    rx_label(d->c, d->x, d->y + d->n * 15 + 7, buf, d->white, RX_TEXT, UI_FONT_LARGE);
    d->n++;
}

/* A question with its answers: the dark surface under the app's colour, the
 * answers as the theme's buttons (the chosen one white). */
int rx_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg, const char *const *choices, int n, int cur,
              u32 accent)
{
    int lines = ui_wrap(msg, w - 24, UI_FONT_LARGE, NULL, NULL);
    int h = 14 + 15 * lines + 28, i, bx;
    RxLines d;

    surface_as(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 8, ROLE_DARK, 0, accent_of(accent), 250);
    d.c = c;
    d.x = x + 12;
    d.y = y;
    d.n = 0;
    d.white = 0;
    ui_wrap(msg, w - 24, UI_FONT_LARGE, rx_dialog_line, &d);
    bx = x + w - 10;
    for (i = n - 1; i >= 0; i--) {
        int sel = i == cur;

        bx -= 58;
        surface(c, bx, y + h - 26 - (sel && !style() ? 1 : 0), 58, 19, UI_CUT_TL | UI_CUT_BR, 4, sel, 255);
        rx_label(c, bx + (58 - ui_text_w(choices[i], UI_FONT_LARGE)) / 2,
                 y + h - 26 - (sel && !style() ? 1 : 0) + ui_text_y(19, UI_FONT_LARGE), choices[i], sel, RX_TEXT,
                 UI_FONT_LARGE);
        bx -= 6;
    }
    return h;
}

void rx_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    u32 col = accent_of(accent);
    int fw;

    ui_fill(c, x, y, w, 6, R(RX_OUTLINE), 255);
    ui_fill(c, x + 1, y + 1, w - 2, 4, R(style() ? 0x313139u : 0x4A4A4Au), 255);
    if (den <= 0 || num <= 0) {
        return;
    }
    num = num > den ? den : num;
    fw = (int)((long)(w - 2) * num / den);
    ui_fill(c, x + 1, y + 1, fw, 2, R(rx_mix(col, 0xFFFFFFu, 40, 100)), 255);
    ui_fill(c, x + 1, y + 3, fw, 2, R(col), 255);
}

/* A row of pages: Plasma's leaning dashes (the page shown white), Harmonia's
 * bevelled squares (the page shown in the app's colour). */
void rx_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent)
{
    int i;

    for (i = 0; i < n; i++) {
        int px = x + i * 8;

        if (style()) {
            bevel_box(c, px, y, 6, 6, i == cur ? accent_of(accent) : 0x4A5262u, 255);
        } else {
            rx_slab(c, y, 6, (px + 3) * FP, px * FP, (px + 7) * FP, (px + 4) * FP, 0, 6, 0, 0,
                    i == cur ? RX_WHITE : 0x6A6A6Au, i == cur ? RX_WHITE : 0x6A6A6Au, 255);
        }
    }
}

int rx_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent)
{
    int w = ui_text_w(name, UI_FONT_SMALL) + 14, i;
    u32 col = accent_of(accent);

    surface_as(c, x, y, w, 13, UI_CUT_TL | UI_CUT_BR, 3, ROLE_DARK, 0, col, 235);
    rx_label(c, x + 7, y + ui_text_y(13, UI_FONT_SMALL), name, 0, RX_TEXT, UI_FONT_SMALL);
    for (i = 0; i < 4; i++) {
        ui_fill(c, x + (w - 7) / 2 + i, y + 13 + i, 7 - 2 * i, 1, R(col), 255);
    }
    return w;
}

/* A stat: its name, its value, a two-toned bar as the HP's. */
void rx_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value, int num, int den, u32 accent)
{
    char v[16];
    int bw = w - 92, fw, ty = ui_text_y(15, UI_FONT_MEDIUM);
    u32 col = accent_of(accent);

    if (style()) {
        surface(c, x, y, w, 15, 0, 0, 0, 255);
    } else {
        ui_fill(c, x, y + 14, w, 1, R(0x4A4A4Au), 255);
    }
    rx_label(c, x + 6, y + ty, name, 0, RX_MUTED, UI_FONT_MEDIUM);
    snprintf(v, sizeof(v), "%d", value < 0 ? 0 : value);
    rx_label(c, x + 74 - ui_text_w(v, UI_FONT_MEDIUM), y + ty, v, 0, RX_TEXT, UI_FONT_MEDIUM);
    if (bw < 4) {
        return;
    }
    ui_fill(c, x + 84, y + 5, bw, 5, R(RX_OUTLINE), 255);
    ui_fill(c, x + 85, y + 6, bw - 2, 3, R(style() ? 0x313139u : 0x4A4A4Au), 255);
    if (den > 0 && num > 0) {
        fw = (int)((long)(bw - 2) * (num > den ? den : num) / den);
        fw = fw < 1 ? 1 : fw;
        ui_fill(c, x + 85, y + 6, fw, 1, R(rx_mix(col, 0xFFFFFFu, 40, 100)), 255);
        ui_fill(c, x + 85, y + 7, fw, 2, R(col), 255);
    }
}

void rx_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected)
{
    if (style()) {
        ui_fill(c, x, y, size, size, R(RX_OUTLINE), 255);
        ui_fill(c, x + 1, y + 1, size - 2, size - 2, R(selected ? RX_WHITE : 0x4A5262u), 255);
        ui_fill(c, x + 2, y + 2, size - 3, size - 3, R(0x202024u), 255);
    } else {
        ui_ellipse(c, x, y, size, size, R(RX_OUTLINE), UI_NONE, 255);
        ui_ellipse(c, x + 1, y + 1, size - 2, size - 2, R(selected ? RX_WHITE : 0x626262u), UI_NONE, 255);
        ui_ellipse(c, x + 2, y + 2, size - 4, size - 4, R(0x1C1C1Cu), UI_NONE, 255);
    }
    if (icon != NULL) {
        ui_image(c, x + (size - icon->w) / 2, y + (size - icon->h) / 2, icon, 1);
    }
}

/* A Pokemon of the party: its place in the party on the row's end, its
 * icon, name, level and HP.  The party's blue stays on Harmonia's tile and
 * on the chosen row's end. */
void rx_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level, int hp,
                  int maxhp, const UiImage *icon, int selected)
{
    const int h = 30;
    char num[4], lv[48], hpn[24];
    int tw, hw, nx, font = UI_FONT_LARGE, room;
    EidosUiRect saved;

    num[0] = (char)('1' + slot);
    num[1] = '\0';
    if (name == NULL) {
        surface_tint(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, 0, 0x06182Cu, 150);
        rx_label(c, x + 6, y + ui_text_y(h, UI_FONT_LARGE), num, 0, 0x6A6A6Au, UI_FONT_LARGE);
        rx_label(c, x + 40, y + ui_text_y(h, UI_FONT_LARGE), "Empty", 0, 0x6A6A6Au, UI_FONT_LARGE);
        return;
    }
    surface_tint(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, selected, UI_NAVY, 240);
    if (style()) {
        ui_fill(c, x + 2, y + 2, 13, h - 4, R(selected ? 0x0878C0u : 0x04182Cu), 255);
        ui_fill(c, x + 15, y + 2, 1, h - 4, R(RX_OUTLINE), 255);
        ui_text(c, x + 5, y + ui_text_y(h, UI_FONT_LARGE), num, R(RX_TEXT), (s32)R(0x0C0C0Cu), UI_FONT_LARGE, 1);
    } else {
        rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + 17) * FP, (x + 13) * FP, 2, h - 2, 0, 0,
                selected ? 0x30C0FFu : 0x2A2A2Au, selected ? UI_SKY : 0x161616u, 255);
        ui_text(c, x + 5, y + ui_text_y(h, UI_FONT_LARGE), num, R(selected ? RX_WHITE : RX_MUTED),
                (s32)R(0x0C0C0Cu), UI_FONT_LARGE, 1);
    }
    if (icon != NULL) {
        ui_image(c, x + 17 + (22 - icon->w) / 2, y + (h - icon->h) / 2, icon, 1);
    }
    room = w - 41 - 9 - 2 - 8;
    if (level > 0) {
        room -= ui_text_w(ui_tr("Lv."), UI_FONT_SMALL) +
                ui_text_w(level >= 100 ? "100" : level >= 10 ? "50" : "5", UI_FONT_SMALL);
    }
    if (ui_text_w(name, UI_FONT_LARGE) > room) {
        font = UI_FONT_SMALL;
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x + 41, y, room + 8, h);
    nx = rx_label(c, x + 41, y + (font == UI_FONT_LARGE ? 1 : 3), name, selected, RX_TEXT, font);
    ui_gender(c, nx + 3, y + (font == UI_FONT_LARGE ? 1 : 3), gender, font);
    ui_pop(c, 0, 0, &saved);
    if (level <= 0) {
        return;                         /* an Egg */
    }
    snprintf(lv, sizeof(lv), "%s%d", ui_tr("Lv."), level);
    tw = ui_text_w(lv, UI_FONT_SMALL);
    rx_label(c, x + w - 9 - tw, y + 3, lv, selected, selected ? RX_INK : RX_ACCENT, UI_FONT_SMALL);
    snprintf(hpn, sizeof(hpn), "%d/%d", hp, maxhp);
    hw = ui_text_w(hpn, UI_FONT_SMALL);
    ui_bar(c, x + 41, y + 19, w - 53 - hw, 5, hp, maxhp);
    rx_label(c, x + w - 9 - hw, y + 16, hpn, selected, RX_TEXT, UI_FONT_SMALL);
}

/* An item of the Bag: the Bag's red on Harmonia's tile and the chosen row's
 * end. */
int rx_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name, int count, int selected)
{
    int h = 20, tx = 10, ty;

    if (icon != NULL && icon->h + 6 > h) {
        h = icon->h + 6;
    }
    ty = ui_text_y(h, UI_FONT_LARGE);
    surface_tint(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, selected, 0x3C0A1Au, 255);
    if (selected) {
        cap(c, x, y, h, UI_PINK, 255);
    }
    if (icon != NULL) {
        ui_image(c, x + 9, y + (h - icon->h) / 2, icon, 1);
        tx = 9 + icon->w + 3;
    }
    rx_label(c, x + tx, y + ty, name, selected, RX_TEXT, UI_FONT_LARGE);
    if (count > 0) {
        char s[16];

        snprintf(s, sizeof(s), "\xC3\x97%d", count);
        rx_label(c, x + w - 8 - ui_text_w(s, UI_FONT_LARGE), y + ty, s, selected, RX_TEXT, UI_FONT_LARGE);
    }
    return h;
}

void rx_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name, int state, int selected)
{
    char s[8];
    int ty = ui_text_y(18, UI_FONT_LARGE);

    surface_tint(c, x, y, w, 18, UI_CUT_TL | UI_CUT_BR, 4, selected, UI_DEX3, 255);
    if (state == 2) {
        u8 st = 0;

        ui_party_balls(c, x + 7, y + 4, &st, 1);
    } else if (state == 1) {
        ui_ellipse(c, x + 6, y + 4, 10, 10, UI_NONE, R(selected ? RX_INK : 0xD5D5D5u), 255);
    }
    snprintf(s, sizeof(s), "%03d", number % 1000);
    rx_label(c, x + 24, y + ty, s, selected, selected ? 0xB01830u : RX_MUTED, UI_FONT_LARGE);
    if (name != NULL && state != 0) {
        rx_label(c, x + 50, y + ty, name, selected, RX_TEXT, UI_FONT_LARGE);
    } else {
        rx_label(c, x + 50, y + ty, "?????", selected, selected ? 0x707070u : RX_MUTED, UI_FONT_LARGE);
    }
}

/* A fact and its value: Plasma's on a hairline, Harmonia's on a tile. */
void rx_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value)
{
    int ty = ui_text_y(15, UI_FONT_LARGE);

    if (style() && rx_on_paper()) {
        /* the DS's summary rows: the label white on dark grey after a
         * bullet, the value ink on the light cell beside it */
        int lw = w * 42 / 100, vf = UI_FONT_LARGE;

        /* the label's cell holds the label; a value too long for the rest
         * takes the small letters */
        if (lw < ui_text_w(label, UI_FONT_LARGE) + 18) {
            lw = ui_text_w(label, UI_FONT_LARGE) + 18;
        }
        if (lw > w - 24) {
            lw = w - 24;
        }
        if (ui_text_w(value, vf) > w - lw - 8) {
            vf = UI_FONT_SMALL;
        }

        ui_fill(c, x, y, w, 15, R(0x6C6C78u), 255);
        ui_fill(c, x + 1, y + 1, lw - 1, 13, R(0x54545Fu), 255);
        ui_fill(c, x + lw, y + 1, w - lw - 1, 13, R(0xE4E4E8u), 255);
        ui_fill(c, x + 5, y + 6, 3, 3, R(0xFFFFFFu), 255);
        ui_text(c, x + 12, y + ty, label, R(0xFFFFFFu), (s32)R(0x30303Au), UI_FONT_LARGE, 1);
        ui_text(c, x + w - 6 - ui_text_w(value, vf), y + ui_text_y(15, vf), value, R(0x30303Au), (s32)R(0xB4B4BCu), vf,
                1);
        return;
    }
    if (style()) {
        surface(c, x, y, w, 15, 0, 0, 0, 255);
    } else {
        ui_fill(c, x, y, w, 14, R(0x1A1A1Au), 150);
        ui_fill(c, x, y + 14, w, 1, R(0x626262u), 255);
    }
    rx_label(c, x + 6, y + ty, label, 0, RX_MUTED, UI_FONT_LARGE);
    rx_label(c, x + w - 6 - ui_text_w(value, UI_FONT_LARGE), y + ty, value, 0, RX_TEXT, UI_FONT_LARGE);
}

/* ---------------------------------------------------------------- */
/* Text boxes and the lists that answer them                         */
/* ---------------------------------------------------------------- */

/* A box with its corners taken off (two pixels), the DS's window shape. */
static void round_box(EidosUiCtx *c, int x, int y, int w, int h, u32 col, int a)
{
    ui_fill(c, x + 2, y, w - 4, 1, R(col), a);
    ui_fill(c, x + 1, y + 1, w - 2, 1, R(col), a);
    ui_fill(c, x, y + 2, w, h - 4, R(col), a);
    ui_fill(c, x + 1, y + h - 2, w - 2, 1, R(col), a);
    ui_fill(c, x + 2, y + h - 1, w - 4, 1, R(col), a);
}

/*
 * The frame of a text box (kind: textbox.h's; 0 and 1 a person's or a sign's
 * words, 2 the system's) and the colours of what stands on it.
 *   Harmonia  the DS's windows: white paper in a dark rounded frame with a
 *             grey inner line; the system's box in the status bar's
 *             blue-grey bevel.
 *   Plasma    the plates' white strip as a leaning box; the system's the
 *             band's glass under a white rule, the plates' gold on its end.
 */
void rx_textbox(EidosUiCtx *c, int kind, int x, int y, int w, int h, u32 *fill, u32 *edge, u32 *ink, u32 *shadow)
{
    int i, message = (kind & 16) != 0;

    kind &= 15;
    if (!style() && message && kind != 1) {
        /* Plasma: a message is a band across the screen (the white strip
         * along its top, the glass and its mesh), the letters light */
        int cw = c->canvas->w, yy, xx;

        ui_fill(c, 0, y - 3, cw, 1, R(RX_PLATE_OL), 255);
        ui_fill(c, 0, y - 2, cw, 2, R(RX_WHITE), 255);
        ui_fill(c, 0, y, cw, 1, R(RX_DARK_RIM), 255);
        for (yy = 1; yy < h + 3; yy++) {
            ui_fill(c, 0, y + yy, cw, 1, R(rx_mix(0x343434u, 0x1A1A1Au, yy, h + 2)), 232);
        }
        for (yy = 4; yy < h; yy += 5) {
            for (xx = 3; xx < cw - 3; xx += 5) {
                ui_fill(c, xx, y + yy, 2, 2, R(0x3A3A3Au), 110);
            }
        }
        *ink = R(RX_TEXT);
        *shadow = R(0x0C0C0Cu);
        *fill = R(0x262626u);
        *edge = R(RX_OUTLINE);
        return;
    }
    if (kind != 2) {
        *ink = R(0x42424Au);
        *shadow = R(0xADADB5u);
        if (style()) {
            for (i = 0; i < 3; i++) {
                ui_fill(c, x + 2, y + h + i, w - 2, 1, R(0x000000u), 70 / (1 + i * i));
            }
            round_box(c, x, y, w, h, 0x41414Au, 255);
            round_box(c, x + 1, y + 1, w - 2, h - 2, 0xC5C5CDu, 255);
            round_box(c, x + 2, y + 2, w - 4, h - 4, 0xFFFFFFu, 255);
            ui_fill(c, x + 3, y + h - 5, w - 6, 2, R(0xEEEEF4u), 255);
            *fill = R(0xFFFFFFu);
            *edge = R(0x41414Au);
        } else {
            plasma_surface(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 7, ROLE_WHITE, 0, 0, 255);
            *fill = R(RX_WHITE);
            *edge = R(RX_OUTLINE);
        }
        return;
    }
    *ink = R(RX_TEXT);
    *fill = R(0x202024u);
    if (style()) {
        /* the DS's: black in a white frame, a grey line inside it */
        *shadow = R(0x50505Au);
        *edge = R(0xF8F8F8u);
        *fill = R(0x000000u);
        round_box(c, x, y, w, h, 0x000000u, 255);
        round_box(c, x + 1, y + 1, w - 2, h - 2, 0xF8F8F8u, 255);
        ui_fill(c, x + 3, y + 3, w - 6, h - 6, R(0x70707Cu), 255);
        ui_fill(c, x + 4, y + 4, w - 8, h - 8, R(0x000000u), 255);
    } else {
        int ext = 14 < h ? 14 : h, yy, l, r;

        *shadow = R(0x0C0C0Cu);
        *edge = R(RX_OUTLINE);
        for (yy = 0; yy < h; yy++) {
            lean_in(yy, h, UI_CUT_TL | UI_CUT_BR, ext, &l, &r);
            span(c, y + yy, x * FP + l, (x + w) * FP - r, RX_OUTLINE, 250);
        }
        for (yy = 1; yy < h - 1; yy++) {
            lean_in(yy, h, UI_CUT_TL | UI_CUT_BR, ext, &l, &r);
            span(c, y + yy, x * FP + l + (l ? INSET : FP), (x + w) * FP - r - (r ? INSET : FP),
                 yy <= 2 ? RX_WHITE : rx_mix(0x343434u, 0x1C1C1Cu, yy, h - 1), 250);
        }
        /* the gold on its end, under the cut */
        rx_slab(c, y, h, (x + 2) * FP, (x + 2) * FP, (x + 6) * FP, (x + 6) * FP, ext + 1 < h - 3 ? ext + 1 : 3, h - 3,
                0, 0, rx_mix(RX_ACCENT, 0xFFFFFFu, 40, 100), RX_ACCENT, 255);
    }
}

/*
 * One answer of a list (style: choice_list.h's; 1 the white box's).  The
 * chosen one turns to the other surface: on Harmonia the DS's pale band and
 * its arrow, on Plasma a leaning chip (dark on the white box, white on the
 * dark one).
 */
void rx_choice_row(EidosUiCtx *c, int cstyle, int x, int y, int w, int h, const char *label, int lit)
{
    int white = cstyle == 1, ty = ui_text_y(h, UI_FONT_LARGE);

    if (style()) {
        if (lit) {
            int i;

            ui_fill(c, x + 1, y, w - 2, h, R(white ? 0x8CB4DCu : 0x7B8B9Cu), 255);
            ui_fill(c, x + 1, y + 1, w - 2, h - 2, R(white ? 0xD6E8F8u : 0x4A5262u), 255);
            for (i = 0; i < 4; i++) {
                ui_fill(c, x + 4 + i, y + h / 2 - 4 + i, 1, 8 - 2 * i, R(white ? 0x42424Au : RX_WHITE), 255);
            }
        }
        if (white) {
            ui_text(c, x + 12, y + ty, label, R(0x42424Au), (s32)R(lit ? 0xA8BCD0u : 0xADADB5u), UI_FONT_LARGE, 1);
        } else {
            ui_text(c, x + 12, y + ty, label, R(lit ? RX_WHITE : 0xD8D8D8u), (s32)R(0x202028u), UI_FONT_LARGE, 1);
        }
        return;
    }
    if (lit) {
        /* the chip inside its row: 1 px clear above and below, from past
         * the box's gold strip to 2 px before the row's end, a small lean
         * (4 px) so the word keeps its room; the word stands where the
         * other rows' words stand */
        const int cl = 4, cx = x + 6, cy = y + 1, cw2 = w - 5, ch2 = h - 2;

        rx_slab(c, cy, ch2, (cx + cl) * FP, cx * FP, (cx + cw2) * FP, (cx + cw2 - cl) * FP, 0, ch2, 0, 0, RX_OUTLINE,
                RX_OUTLINE, 255);
        if (white) {
            rx_slab(c, cy, ch2, (cx + cl) * FP, cx * FP, (cx + cw2) * FP, (cx + cw2 - cl) * FP, 1, ch2 - 1, INSET, INSET,
                    0x3A3A3Au, 0x1C1C1Cu, 255);
            ui_text(c, x + 14, y + ty, label, R(RX_TEXT), (s32)R(0x0C0C0Cu), UI_FONT_LARGE, 1);
        } else {
            rx_slab(c, cy, ch2, (cx + cl) * FP, cx * FP, (cx + cw2) * FP, (cx + cw2 - cl) * FP, 1, ch2 - 1, INSET, INSET,
                    RX_WHITE, RX_WHITE_LO, 255);
            ui_text(c, x + 14, y + ty, label, R(RX_INK), (s32)R(RX_INK_SH), UI_FONT_LARGE, 1);
        }
        return;
    }
    if (white) {
        ui_text(c, x + 14, y + ty, label, R(0x42424Au), (s32)R(0xADADB5u), UI_FONT_LARGE, 1);
    } else {
        ui_text(c, x + 14, y + ty, label, R(0xD8D8D8u), (s32)R(0x0C0C0Cu), UI_FONT_LARGE, 1);
    }
}
