/*
 * engine/ui/theme_bw.c -- the parts the Black-and-White-styled themes share
 * (engine/ui/theme_bw.h).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_bw.h"
#include "ui_lang.h"

const u32 rx_cmd_color[4] = { 0xF05038u, 0xFFC040u, 0x52CD29u, 0x40A8F0u };
const int rx_cmd_icon[4] = { UI_APP_ICON_PHYSICAL, UI_APP_ICON_BAG, UI_APP_ICON_POKEMON, UI_APP_ICON_BACK };

static int g_paper;

static const u32 *g_tone;
static int g_cool;
static u32 g_accent;

void rx_accent(u32 rgb)
{
    g_accent = rgb;
}

u32 rx_accent_now(void)
{
    return g_accent;
}

const u32 rx_tone_blue[6] = { 0x3C9CCCu, 0x16385Cu, 0x102A48u, 0xFFFFFFu, 0x28B8FFu, 0x1480C8u };
const u32 rx_tone_red[6] = { 0xC04058u, 0x4A0C16u, 0x30080Eu, 0xFFFFFFu, 0xF05070u, 0xC02040u };
const u32 rx_tone_green[6] = { 0x6CC028u, 0x183820u, 0x102818u, 0xFFFFFFu, 0x48C850u, 0x2C9838u };
const u32 rx_tone_crimson[6] = { 0xD04868u, 0x6E0C20u, 0x5A0820u, 0xFFFFFFu, 0xF87098u, 0xD83C68u };

void rx_paper(int on)
{
    g_paper = on;
    if (!on) {
        g_tone = NULL;
        g_cool = 0;
        g_accent = 0;
    }
}

static int g_fr_x, g_fr_y, g_fr_cut, g_fr_in, g_head_pad;

void rx_frame_begin(void)
{
    rx_paper(0);
    g_fr_cut = 0;
    g_head_pad = 0;
}

void rx_head_pad(int px)
{
    g_head_pad = px;
}

int rx_head_pad_now(void)
{
    return g_head_pad;
}

void rx_frame_note(const EidosUiCtx *c, int x, int y, int cut, int in)
{
    x += c->ox;
    y += c->oy;
    if (in == 0) {
        g_fr_x = x;
        g_fr_y = y;
        g_fr_cut = cut;
        g_fr_in = 0;
    } else if (g_fr_cut > 0 && x == g_fr_x && y == g_fr_y && in > g_fr_in) {
        g_fr_in = in;
    }
}

int rx_frame_corner(const EidosUiCtx *c, int x, int y)
{
    /* the frame's innermost line is the layer before its body; its cut is
     * the outer one less 2 px for every 5 px in (as the frames are drawn) */
    int i = g_fr_in > 0 ? g_fr_in - 1 : 0, ci = g_fr_cut - i * 2 / 5, dx = x + c->ox - g_fr_x, dy = y + c->oy - g_fr_y;
    int gap, t;

    if (g_fr_cut <= 0 || dx < 0 || dy < 0 || dx > g_fr_cut + 12 || dy > g_fr_cut + 12) {
        return 0;
    }
    if (ci < 1) {
        ci = 1;
    }
    /* the clear pixels between that line and the piece on the straight sides */
    gap = (dx < dy ? dx : dy) - (i + 1);
    if (gap < 0) {
        gap = 0;
    }
    /* along the diagonal: the line, its blended neighbour, then the same
     * gap (x 1.41, a diagonal's steps), counted in x + y */
    t = 2 * i + ci + 2 + (gap * 3 + 1) / 2 - (dx + dy);
    return t < 0 ? 0 : t > 9 ? 9 : t;
}

void rx_cool(int on)
{
    g_cool = on;
}

int rx_is_cool(void)
{
    return g_cool;
}

void rx_tone(const u32 *pal)
{
    g_tone = pal;
}

const u32 *rx_tone_now(void)
{
    return g_tone;
}

int rx_on_paper(void)
{
    return g_paper;
}

u32 rx_mix(u32 a, u32 b, int t, int n)
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


/* ---------------------------------------------------------------- */
/* Text                                                              */
/* ---------------------------------------------------------------- */

/* Text with the plates' dark outline (letters on the scene or the band). */
int rx_otext(EidosUiCtx *c, int x, int y, const char *s, u32 col, u32 ol, int font)
{
    static const int d[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
                                 { 1, 0 },   { -1, 1 }, { 0, 1 },  { 1, 1 } };
    int i;

    for (i = 0; i < 8; i++) {
        ui_text(c, x + d[i][0], y + d[i][1], s, R(ol), -1, font, 1);
    }
    return ui_text(c, x, y, s, R(col), -1, font, 1);
}

/* A label on the band: the letters with the DS's drop shadow, dark on the
 * dark surface; ink with a light one on the white. */
int rx_label(EidosUiCtx *c, int x, int y, const char *s, int white, u32 col, int font)
{
    if (g_paper) {
        u32 ink = col == RX_TEXT ? 0x30303Au : col == RX_MUTED ? 0x484854u : rx_mix(col, 0x000000u, 50, 100);

        return ui_text(c, x, y, s, R(ink), (s32)R(white ? 0x9CD8F0u : 0xB4B4BCu), font, 1);
    }
    if (g_tone != NULL) {
        /* on a tone the chosen part is bright, not white: light letters */
        return ui_text(c, x, y, s, R(white || col == RX_INK ? RX_WHITE : col), (s32)R(0x06101Cu), font, 1);
    }
    if (white) {
        return ui_text(c, x, y, s, R(col == RX_TEXT ? RX_INK : col), (s32)R(RX_INK_SH), font, 1);
    }
    return ui_text(c, x, y, s, R(col), (s32)R(0x0C0C0Cu), font, 1);
}

/* A type's name in its colour, fit for the surface. */
int rx_type_word(EidosUiCtx *c, int x, int y, int type, int white)
{
    u32 col = ui_type_color(type);

    if (white) {
        return ui_text(c, x, y, ui_type_name(type), R(rx_mix(col, 0x000000u, 40, 100)), -1, UI_FONT_SMALL, 1);
    }
    return ui_text(c, x, y, ui_type_name(type), R(rx_mix(col, 0xFFFFFFu, 35, 100)), (s32)R(0x0C0C0Cu), UI_FONT_SMALL,
                   1);
}

/* ---------------------------------------------------------------- */
/* Shapes: smooth slanted edges                                      */
/* ---------------------------------------------------------------- */

/*
 * Rows r0 to r1 of a shape H rows tall whose left edge runs straight from
 * xl0 (top) to xl1 (bottom) and its right edge from xr0 to xr1, each moved
 * in by inl and inr; the pixels an edge crosses are blended by how much of
 * them it covers.  A blend from top to bot over the rows drawn.
 */
void rx_slab(EidosUiCtx *c, int y, int H, int xl0, int xl1, int xr0, int xr1, int r0, int r1, int inl,
                 int inr, u32 top, u32 bot, int a)
{
    int yy;

    for (yy = r0; yy < r1; yy++) {
        int xl = xl0 + (xl1 - xl0) * (2 * yy + 1) / (2 * H) + inl;
        int xr = xr0 + (xr1 - xr0) * (2 * yy + 1) / (2 * H) - inr;
        int il, ir;
        u32 col = top == bot ? top : rx_mix(top, bot, yy - r0, r1 - r0 - 1);

        if (xl < 0) {
            xl = 0;
        }
        if (xr <= xl) {
            continue;
        }
        il = (xl + FP - 1) / FP;        /* the first whole pixel            */
        ir = xr / FP;                   /* one past the last whole pixel    */
        if (il > ir) {
            ui_fill(c, xl / FP, y + yy, 1, 1, R(col), a * (xr - xl) / FP);
            continue;
        }
        if (ir > il) {
            ui_fill(c, il, y + yy, ir - il, 1, R(col), a);
        }
        if (il * FP > xl) {
            ui_fill(c, il - 1, y + yy, 1, 1, R(col), a * (il * FP - xl) / FP);
        }
        if (xr > ir * FP) {
            ui_fill(c, ir, y + yy, 1, 1, R(col), a * (xr - ir * FP) / FP);
        }
    }
}

void rx_span(EidosUiCtx *c, int y, int xl, int xr, u32 col, int a)
{
    int il, ir;

    if (xl < 0) {
        xl = 0;
    }
    if (xr <= xl) {
        return;
    }
    il = (xl + FP - 1) / FP;
    ir = xr / FP;
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

static int g_dex_seen, g_dex_own, g_dex_part, g_dex_whole;

void rx_dex_stats(int seen, int own, int part, int whole)
{
    g_dex_seen = seen;
    g_dex_own = own;
    g_dex_part = part < 0 ? 0 : part > whole ? whole : part;
    g_dex_whole = whole;
}

void rx_dex_geom(const EidosUiCtx *c, int open, RxDex *g)
{
    int W = c->canvas->w, H = c->canvas->h - ui_bar_h(c);
    int slide = open >= 200 ? 256 : open < 0 ? 0 : open * 256 / 200, cw, x0c;

    slide = 256 - (256 - slide) * (256 - slide) / 256;      /* eased out */
    g->top = 4;
    g->bot = H - 4;
    g->hh = g->bot - g->top;
    g->lid_w = g->hh * 45 / 100;
    g->sw = (W - g->lid_w - 40) / 2;
    cw = g->lid_w + g->sw + 14;
    x0c = (W - cw) / 2;
    g->x0 = x0c + (4 - x0c) * slide / 256;
    g->erx = g->ery = g->lid_w * 29 / 100;
    g->ccx = g->x0 + g->lid_w * 47 / 100;
    g->cy = (g->top + g->bot) / 2;
    g->u_end = g->x0 + cw;
    g->s1.x = g->x0 + g->lid_w + 2;
    g->s1.y = g->top + 12;
    g->s1.w = g->sw;
    g->s1.h = g->hh - 24;
    g->s2 = g->s1;
    g->s2.x = g->s1.x + (g->sw + 20) * slide / 256;
    g->s2.y += 3;                       /* the lower half is a step smaller */
    g->s2.h -= 6;
    g->l_end = g->s2.x + g->sw + 12;
    g->slide = slide;
    g->lit = open <= 200 ? 0 : open >= 256 ? 256 : (open - 200) * 256 / 56;
}

/* Half the width (1/256 px) of the ellipse (erx, ery) on the row dy from its
 * centre; 0 outside it. */
static int ell_half(int erx, int ery, int dy)
{
    double t = ((double)dy + 0.5) / (double)ery;

    if (t <= -1.0 || t >= 1.0) {
        return 0;
    }
    return (int)((double)erx * (double)FP * sqrt(1.0 - t * t));
}

/* A smooth disc of r pixels about (cx, cy), in spans; side < 0: its left
 * half only, > 0: its right half, 0: all of it. */
static void dex_disc(EidosUiCtx *c, int cx, int cy, int r, int side, u32 top, u32 bot)
{
    int dy;

    for (dy = -r; dy < r; dy++) {
        int h = ell_half(r, r, dy);

        if (h > 0) {
            rx_span(c, cy + dy, side > 0 ? cx * FP : cx * FP - h, side < 0 ? cx * FP : cx * FP + h,
                    top == bot ? top : rx_mix(top, bot, dy + r, 2 * r - 1), 255);
        }
    }
}

/* A clasp on a long edge: a tab from the edge inward, its inner end
 * narrowed.  dir 1: from the top edge down; -1: from the foot up. */
static void dex_clasp(EidosUiCtx *c, int x, int y, int w, int d, int dir, u32 col, u32 ink)
{
    int flat = d * 50 / 100, tip = d - flat, cut = w / 4;
    int y0 = dir > 0 ? y : y - d;

    if (dir > 0) {
        rx_slab(c, y0, flat + 1, (x - 2) * FP, (x - 2) * FP, (x + w + 2) * FP, (x + w + 2) * FP, 0, flat + 1, 0, 0, ink,
                ink, 255);
        rx_slab(c, y0 + flat + 1, tip + 1, (x - 2) * FP, (x + cut - 2) * FP, (x + w + 2) * FP, (x + w - cut + 2) * FP, 0,
                tip + 1, 0, 0, ink, ink, 255);
        rx_slab(c, y0, flat, x * FP, x * FP, (x + w) * FP, (x + w) * FP, 0, flat, 0, 0, col, col, 255);
        rx_slab(c, y0 + flat, tip - 1, x * FP, (x + cut) * FP, (x + w) * FP, (x + w - cut) * FP, 0, tip - 1, 0, 0, col,
                rx_mix(col, 0x000000u, 18, 100), 255);
    } else {
        rx_slab(c, y0 + tip - 1, flat + 1, (x - 2) * FP, (x - 2) * FP, (x + w + 2) * FP, (x + w + 2) * FP, 0, flat + 1, 0,
                0, ink, ink, 255);
        rx_slab(c, y0 - 2, tip + 1, (x + cut - 2) * FP, (x - 2) * FP, (x + w - cut + 2) * FP, (x + w + 2) * FP, 0, tip + 1,
                0, 0, ink, ink, 255);
        rx_slab(c, y0 + tip, flat, x * FP, x * FP, (x + w) * FP, (x + w) * FP, 0, flat, 0, 0, col, col, 255);
        rx_slab(c, y0 + 1, tip - 1, (x + cut) * FP, x * FP, (x + w - cut) * FP, (x + w) * FP, 0, tip - 1, 0, 0,
                rx_mix(col, 0xFFFFFFu, 12, 100), col, 255);
    }
}

/* The ball's kind by the share owned: 0 Poke Ball, 1 Great Ball (a third),
 * 2 Ultra Ball (two thirds), 3 Master Ball (complete). */
static int dex_ball_kind(void)
{
    if (g_dex_whole <= 0) {
        return 0;
    }
    if (g_dex_part >= g_dex_whole) {
        return 3;
    }
    return g_dex_part * 3 >= g_dex_whole * 2 ? 2 : g_dex_part * 3 >= g_dex_whole ? 1 : 0;
}

/* The distance from (u, v) to the segment (ax, ay)-(bx, by). */
static double seg_dist(double u, double v, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, t = ((u - ax) * dx + (v - ay) * dy) / (dx * dx + dy * dy);

    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    dx = u - (ax + t * dx);
    dy = v - (ay + t * dy);
    return sqrt(dx * dx + dy * dy);
}

/*
 * The colour of a ball's upper half at (u, v), in radii from its centre (v
 * is negative above it), the ball seen from the front.  Every mark stands in
 * a dark line of its ground's colour.
 */
static u32 dex_ball_top(int kind, double u, double v)
{
    double au = u < 0 ? -u : u, e;

    switch (kind) {
    case 1: {
        /* Great Ball: blue; a red shoulder on each side, a long bump that
         * leans out from the band toward the top */
        double lx = au - 0.60, ly = v + 0.50, rx2 = lx * 0.94 - ly * 0.34, ry2 = lx * 0.34 + ly * 0.94;

        e = (rx2 / 0.17) * (rx2 / 0.17) + (ry2 / 0.43) * (ry2 / 0.43);
        return e < 1.0 ? (rx2 < -0.05 ? 0xF86060u : 0xE02C2Cu) : e < 1.55 ? 0x183C90u : 0x3A7CE8u;
    }
    case 2: {
        /* Ultra Ball: black; a yellow bar on each side, leaning in toward
         * the top */
        e = au - (0.54 + 0.14 * v);
        e = e < 0 ? -e : e;
        if (e < 0.105) {
            return au - (0.54 + 0.14 * v) < -0.04 ? 0xFFE868u : 0xF4C818u;
        }
        return e < 0.16 ? 0x101014u : 0x3A3A46u;
    }
    case 3: {
        /* Master Ball: purple; a pink bump on each shoulder; the white M */
        double m, lx = au - 0.54, ly = v + 0.50;

        e = (lx * lx + ly * ly) / (0.21 * 0.21);
        if (e < 1.0) {
            return lx * lx + (ly + 0.06) * (ly + 0.06) < 0.10 * 0.10 ? 0xFFB8E0u : 0xF070B8u;
        }
        if (e < 1.5) {
            return 0x4A1C78u;
        }
        m = seg_dist(au, v, 0.17, -0.40, 0.17, -0.78);
        e = seg_dist(au, v, 0.17, -0.78, 0.0, -0.54);
        m = e < m ? e : m;
        return m < 0.045 ? 0xFFFFFFu : m < 0.085 ? 0x4A1C78u : 0x8A48C8u;
    }
    default:
        return 0xE83030u;       /* Poke Ball: red */
    }
}

/* A ball's colour shaded as a sphere lit from its upper left: a glint, a
 * soft light, a darker far edge. */
static u32 dex_ball_shade(u32 col, double u, double v)
{
    double d2 = u * u + v * v, lx = u + 0.38, ly = v + 0.46, l2 = lx * lx + ly * ly;

    if (l2 < 0.012) {
        return rx_mix(col, 0xFFFFFFu, 78, 100);
    }
    if (l2 < 0.16) {
        col = rx_mix(col, 0xFFFFFFu, (int)((0.16 - l2) * 190.0), 100);
    }
    if (d2 > 0.52) {
        col = rx_mix(col, 0x000000u, (int)((d2 - 0.52) * 62.0), 100);
    }
    return col;
}

void rx_dex_lid(EidosUiCtx *c, const RxDex *g, u32 lid, u32 body, u32 ink, u32 scr_a, u32 scr_b, u32 scr_line,
                int grid, int cut_top, int cut_foot)
{
    int zl = (g->x0 + 3) * FP, y, y0 = g->top + 3, y1 = g->bot - 3;
    /* the plate's edge toward the screens: a shallow arc */
    int ax = g->x0 + g->lid_w * 60 / 100, arx = g->lid_w * 36 / 100, ary = g->hh * 62 / 100;
    int r = g->erx, rim = AT_LEAST(r / 12, 3), bw = AT_LEAST(r * 12 / 100, 3), br = r * 27 / 100;
    int ri = r + rim + 3, ro = ri + AT_LEAST(r / 9, 4), kind = dex_ball_kind(), xx, yy;
    int cd = AT_LEAST(g->hh * 7 / 100, 9), lit = g->lit >= 256 && g_dex_whole > 0;
    u32 hi = rx_mix(lid, 0xFFFFFFu, 16, 100), lo = rx_mix(lid, 0x000000u, 22, 100);

    for (y = y0; y < y1; y++) {
        int dy = y - g->cy, h = ell_half(arx, ary, dy);
        /* the plate keeps inside the shell's cut corners */
        int kt = cut_top - (y - g->top), kf = cut_foot - (g->bot - 1 - y);
        int zr = zl + (kt > 0 ? kt : kf > 0 ? kf : 0) * FP;

        rx_span(c, y, zr, ax * FP + h + 3 * FP, ink, 255);
        rx_span(c, y, zr, ax * FP + h, rx_mix(hi, lo, y - y0, y1 - y0 - 1), 255);
        /* a light line inside the edge */
        rx_span(c, y, ax * FP + h - 3 * FP, ax * FP + h - 2 * FP, 0xFFFFFFu, 70);
    }
    /* the slots at the device's end */
    {
        int sh = AT_LEAST(g->hh * 9 / 100, 10), sx = g->x0 + 8, i;

        for (i = 0; i < 2; i++) {
            int sy = i == 0 ? y0 + g->hh * 8 / 100 : y1 - g->hh * 8 / 100 - sh;

            ui_fill(c, sx, sy + 1, 5, sh - 2, R(ink), 255);
            ui_fill(c, sx + 1, sy, 3, sh, R(ink), 255);
            ui_fill(c, sx + 5, sy + 2, 1, sh - 4, R(0xFFFFFFu), 60);
        }
    }
    /* the clasps on the two long edges */
    {
        int cw2 = AT_LEAST(g->lid_w * 24 / 100, 16);
        int x = g->x0 + g->lid_w * 50 / 100;

        dex_clasp(c, x, y0, cw2, cd, 1, rx_mix(body, 0xFFFFFFu, 12, 100), ink);
        dex_clasp(c, x, y1, cw2, cd, -1, rx_mix(body, 0x000000u, 10, 100), ink);
    }
    /* the gauge: a ring round the ball, filled clockwise from its top by the
     * share owned */
    for (yy = -ro - 1; yy <= ro + 1; yy++) {
        for (xx = -ro - 1; xx <= ro + 1; xx++) {
            int d2 = xx * xx + yy * yy;
            double d, cov, ang;
            int on;

            if (d2 < (ri - 1) * (ri - 1) || d2 > (ro + 1) * (ro + 1)) {
                continue;
            }
            d = sqrt((double)d2);
            cov = (d < ri ? d - (ri - 1) : 1.0) * (d > ro ? (ro + 1) - d : 1.0);
            ang = atan2((double)xx, (double)-yy) / 6.283185307179586;
            ang = ang < 0 ? ang + 1.0 : ang;
            on = lit && ang * (double)g_dex_whole < (double)g_dex_part;
            ui_pixel(c, g->ccx + xx, g->cy + yy, R(on ? 0xFFF8ECu : rx_mix(lid, 0x000000u, 72, 100)), (int)(cov * 255.0));
        }
    }
    /* the ball, facing the player: its black rim, the upper half of its kind,
     * the white lower half, the band, the button */
    dex_disc(c, g->ccx, g->cy, r + rim, 0, ink, ink);
    for (yy = -r; yy < r; yy++) {
        for (xx = -r; xx < r; xx++) {
            int d2 = (2 * xx + 1) * (2 * xx + 1) + (2 * yy + 1) * (2 * yy + 1);
            double cov;
            u32 col;

            if (d2 > (2 * r + 2) * (2 * r + 2)) {
                continue;
            }
            cov = (double)r - sqrt((double)d2) / 2.0 + 0.5;
            cov = cov > 1.0 ? 1.0 : cov;
            if (cov <= 0.0) {
                continue;
            }
            if (!lit) {
                /* unlit: grey glass, its upper half darker */
                col = yy < 0 ? rx_mix(0x4E4E56u, 0x3A3A42u, yy + r, r) : rx_mix(0x70707Au, 0x55555Eu, yy, r);
            } else {
                double u = ((double)xx + 0.5) / (double)r, v = ((double)yy + 0.5) / (double)r;

                col = dex_ball_shade(yy < 0 ? dex_ball_top(kind, u, v) : 0xF4F4F0u, u, v);
            }
            ui_pixel(c, g->ccx + xx, g->cy + yy, R(col), (int)(cov * 255.0));
        }
    }
    for (yy = -bw; yy < bw; yy++) {
        int h = ell_half(r, r, yy);

        rx_span(c, g->cy + yy, g->ccx * FP - h, g->ccx * FP + h, ink, 255);
    }
    dex_disc(c, g->ccx, g->cy, br + rim + 1, 0, ink, ink);
    if (lit) {
        dex_disc(c, g->ccx, g->cy, br, 0, 0xFCFCF8u, 0xD0D0CAu);
        dex_disc(c, g->ccx, g->cy, br * 55 / 100 + 1, 0, 0xB8B8B0u, 0xD8D8D0u);
        dex_disc(c, g->ccx, g->cy, br * 55 / 100 - 1, 0, 0xF4F4EEu, 0xE2E2DAu);
    } else {
        dex_disc(c, g->ccx, g->cy, br, 0, 0x7A7A84u, 0x5C5C66u);
    }
    /* SEEN over the ball, OWN under it: the word on the lid, a gap, a small
     * screen with the number; each group in the middle of the room between a
     * clasp and the ring */
    {
        int sw2 = AT_LEAST(g->lid_w * 38 / 100, 40), sh2 = g->hh >= 300 ? 19 : 17, group = 10 + 5 + sh2 + 4, i;

        for (i = 0; i < 2; i++) {
            int fa = i == 0 ? y0 + cd + 2 : g->cy + ro + 1, fb = i == 0 ? g->cy - ro - 1 : y1 - cd - 2;
            int ly = fa + (fb - fa - group) / 2, by = ly + 10 + 5 + 2, bx = g->ccx - sw2 / 2, k;
            const char *word = ui_tr(i == 0 ? "SEEN" : "OWN");
            char num[16];

            if (fb - fa < group) {
                continue;               /* no room on a very small canvas */
            }
            ui_text(c, g->ccx - ui_text_w(word, UI_FONT_LARGE) / 2, ly + ui_text_y(10, UI_FONT_LARGE), word, R(0xFFFFFFu),
                    (s32)R(rx_mix(lid, 0x000000u, 55, 100)), UI_FONT_LARGE, 1);
            ui_fill(c, bx - 2, by - 1, sw2 + 4, sh2 + 2, R(ink), 255);
            ui_fill(c, bx - 1, by - 2, sw2 + 2, sh2 + 4, R(ink), 255);
            if (!lit) {
                ui_fill(c, bx, by, sw2, sh2, R(rx_mix(scr_b, 0x202024u, 70, 100)), 255);
                continue;
            }
            for (k = 0; k < sh2; k++) {
                ui_fill(c, bx, by + k, sw2, 1, R(grid ? scr_a : rx_mix(scr_a, scr_b, k, sh2 - 1)), 255);
            }
            if (grid) {
                for (k = by + 5; k < by + sh2; k += 8) {
                    ui_fill(c, bx, k, sw2, 1, R(scr_b), 255);
                }
                for (k = bx + 5; k < bx + sw2; k += 8) {
                    ui_fill(c, k, by, 1, sh2, R(scr_b), 255);
                }
            }
            ui_fill(c, bx, by, sw2, 1, R(scr_line), 255);
            ui_fill(c, bx, by + sh2 - 1, sw2, 1, R(scr_line), 255);
            ui_fill(c, bx, by, 1, sh2, R(scr_line), 255);
            ui_fill(c, bx + sw2 - 1, by, 1, sh2, R(scr_line), 255);
            snprintf(num, sizeof num, "%d", i == 0 ? g_dex_seen : g_dex_own);
            ui_text(c, g->ccx - ui_text_w(num, UI_FONT_LARGE) / 2, by + ui_text_y(sh2, UI_FONT_LARGE), num, R(0xFFFFFFu),
                    (s32)R(0x101010u), UI_FONT_LARGE, 1);
        }
    }
}

/* A disc about (cx, cy), r4 quarter pixels in radius, rows y0 to y1 of it
 * (relative to cy), smooth. */
void rx_disc(EidosUiCtx *c, int cx, int cy, int r4, int y0, int y1, u32 col, int a)
{
    int r = (r4 + 3) / 4, yy, xx, sx, sy;

    for (yy = -r; yy <= r; yy++) {
        if (yy < y0 || yy > y1) {
            continue;
        }
        for (xx = -r; xx <= r; xx++) {
            int n = 0;

            for (sy = 0; sy < 4; sy++) {
                for (sx = 0; sx < 4; sx++) {
                    /* the sample, in eighths of a pixel from the centre */
                    int qx = xx * 8 + sx * 2 - 3, qy = yy * 8 + sy * 2 - 3;

                    if (qx * qx + qy * qy <= r4 * r4 * 4) {
                        n++;
                    }
                }
            }
            if (n > 0) {
                ui_pixel(c, cx + xx, cy + yy, R(col), a * n / 16);
            }
        }
    }
}

/* One of the game's own icons, what it shows centred on (cx, cy). */
void rx_game_icon(EidosUiCtx *c, int cx, int cy, int which)
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
    if (x1 >= 0) {
        ui_image(c, cx - (x0 + x1 + 1) / 2, cy - (y0 + y1 + 1) / 2, im, 1);
    }
}

/* ---------------------------------------------------------------- */
/* The chosen move's facts                                           */
/* ---------------------------------------------------------------- */

/*
 * The chosen move's facts in a place w wide from (x, y): its PP, its power
 * and accuracy, each on a line, the kind's badge beside them, and its
 * description on the line under, scrolling when it is longer than its place
 * (it waits, runs to its end, waits, and starts again; `age` counts the
 * frames the moves have been up).
 */
void rx_move_info(EidosUiCtx *c, int x, int y, int w, const UiMoveInfo *m, int age)
{
    static const int badge[3] = { UI_APP_ICON_PHYSICAL, UI_APP_ICON_SPECIAL, UI_APP_ICON_STATUS };
    static char shown[40];
    static int since;
    int font = UI_FONT_SMALL, nx, dw, off = 0;
    char s[32];
    const UiImage *im = ui_app_icon(badge[m->category % 3]);
    EidosUiRect saved;

    /* the PP */
    nx = rx_label(c, x, y, "PP", 0, RX_MUTED, font);
    snprintf(s, sizeof(s), "%d/%d", m->pp, m->maxpp);
    rx_label(c, nx + 6, y, s, 0, m->pp == 0 ? 0xFF6040u : RX_TEXT, font);
    /* the power, and the accuracy after it */
    nx = rx_label(c, x, y + 12, "Power", 0, RX_MUTED, font);
    snprintf(s, sizeof(s), m->power > 0 ? "%d" : "-", m->power);
    nx = rx_label(c, nx + 6, y + 12, s, 0, RX_TEXT, font);
    nx = rx_label(c, nx + 12, y + 12, "Acc.", 0, RX_MUTED, font);
    snprintf(s, sizeof(s), m->accuracy > 0 ? "%d" : "-", m->accuracy);
    rx_label(c, nx + 6, y + 12, s, 0, RX_TEXT, font);
    if (im != NULL) {
        ui_image(c, x + w - im->w, y - 3, im, 1);        /* on the PP's line: clear of the accuracy */
    }
    /* the description */
    if (strncmp(shown, m->name, sizeof(shown) - 1) != 0 || age < since) {
        snprintf(shown, sizeof(shown), "%s", m->name);
        since = age;
    }
    font = UI_FONT_MEDIUM;
    dw = ui_text_w(m->desc, font);
    if (dw > w) {
        /* 50 frames still, a pixel every 2 frames, 60 frames at the end */
        int over = dw - w, t = (age - since) % (50 + 2 * over + 60);

        off = t < 50 ? 0 : t < 50 + 2 * over ? (t - 50) / 2 : over;
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x - 1, y + 25, w + 2, 18);
    rx_label(c, x - off, y + 27, m->desc, 0, RX_TEXT, font);
    ui_pop(c, 0, 0, &saved);
}

/* ---------------------------------------------------------------- */
/* The plates                                                        */
/* ---------------------------------------------------------------- */

static void rx_hp_colors(int hp, int maxhp, u32 *hi, u32 *lo)
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

/*
 * A Pokemon's plate, the DS's made smooth: the name, gender and level above
 * (letters outlined), then a bar from the screen's edge that ends in the
 * lean: the white strip with the HP bar, the dark band under it (yours: the
 * HP numbers; your party's balls go on it), yours also the experience line.
 */
void rx_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp,
                        int maxhp, int exp_num, int exp_den)
{
    int own = exp_den > 0, cw = c->canvas->w, right = x + w / 2 > cw / 2;
    int bh = own ? BAR_OWN : BAR_FOE, by = y + NAME_H, lean = LEAN(bh), nx, lx;
    /* the bar's edges, and the room inside them */
    int xl0 = right ? (x + lean) * FP : 0, xl1 = right ? x * FP : 0;
    int xr0 = right ? cw * FP : (x + w) * FP, xr1 = right ? cw * FP : (x + w - lean) * FP;
    int inl = right ? INSET : 0, inr = right ? 0 : INSET;
    int c0 = right ? x + lean + 6 : x + 6, c1 = right ? x + w - 6 : x + w - lean - 6;
    char lv[16];

    /* the name line */
    nx = rx_otext(c, c0 + 2, y + 1, name, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE);
    ui_gender(c, nx + 3, y + 1, gender, UI_FONT_LARGE);
    snprintf(lv, sizeof(lv), "%d", level);
    lx = c1 - 2 - ui_text_w(lv, UI_FONT_LARGE);
    rx_otext(c, lx, y + 1, lv, RX_TEXT, RX_PLATE_OL, UI_FONT_LARGE);
    rx_otext(c, lx - 3 - ui_text_w("Lv", UI_FONT_SMALL), y + 5, "Lv", RX_ACCENT, RX_PLATE_OL, UI_FONT_SMALL);
    /* the bar: a shadow, the outline, the strip, a dark line, the band */
    rx_slab(c, by + 2, bh, xl0, xl1, xr0, xr1, 0, bh, 0, 0, 0x000000u, 0x000000u, 70);
    rx_slab(c, by, bh, xl0, xl1, xr0, xr1, 0, bh, 0, 0, RX_PLATE_OL, RX_PLATE_OL, 255);
    rx_slab(c, by, bh, xl0, xl1, xr0, xr1, 1, STRIP_H, inl, inr, RX_WHITE, RX_WHITE_LO, 255);
    rx_slab(c, by, bh, xl0, xl1, xr0, xr1, STRIP_H + 1, bh - 1, inl, inr, 0x4A4A4Au, 0x2C2C2Cu, 255);
    rx_slab(c, by, bh, xl0, xl1, xr0, xr1, STRIP_H + 1, STRIP_H + 2, inl, inr, 0x707070u, 0x707070u, 255);
    /* the HP tag and bar in the strip */
    {
        int tx = c0 + 4, tw = 20, bx = tx + tw + 2, bw = c1 - 6 - bx, fw;
        u32 hi, lo;

        ui_fill(c, tx, by + 3, tw, 8, R(0x303030u), 255);
        ui_text(c, tx + 3, by + 3 + ui_text_y(8, UI_FONT_SMALL), "HP", R(0x00FF4Au), -1, UI_FONT_SMALL, 1);
        ui_fill(c, bx, by + 4, bw, 6, R(0x303030u), 255);
        ui_fill(c, bx + 1, by + 5, bw - 2, 4, R(0x585858u), 255);
        if (maxhp > 0 && hp > 0) {
            fw = (int)((long)(bw - 2) * (hp > maxhp ? maxhp : hp) / maxhp);
            if (fw < 1) {
                fw = 1;
            }
            rx_hp_colors(hp, maxhp, &hi, &lo);
            ui_fill(c, bx + 1, by + 5, fw, 2, R(hi), 255);
            ui_fill(c, bx + 1, by + 7, fw, 2, R(lo), 255);
        }
    }
    if (own) {
        char nums[24];
        int ex0 = c0 + 30, ew = c1 - ex0;

        snprintf(nums, sizeof(nums), "%d/%d", hp, maxhp);
        ui_text(c, c1 - 8 - ui_text_w(nums, UI_FONT_LARGE), by + 12, nums, R(0xFFFFFFu), (s32)R(0x1C1C1Cu),
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

void rx_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge)
{
    int right = x + w / 2 > c->canvas->w / 2;

    (void)level;
    if (badge >= 0) {
        ui_status_badge(c, (right ? x + w - 6 : x + w - LEAN(BAR_FOE) - 6) - 80, y + 3, badge);
    }
}

/* ---------------------------------------------------------------- */
/* The party's balls, an Ability's plate                             */
/* ---------------------------------------------------------------- */

/* One ball, 9 pixels across: red over white; a status problem's yellow;
 * a fainted Pokemon's greys; no Pokemon: a dark dot. */
static void rx_ball(EidosUiCtx *c, int cx, int cy, int kind)
{
    u32 top = kind == 3 ? 0xF0B030u : kind == 2 ? 0x6A6E78u : 0xE8403Au;
    u32 bot = kind == 2 ? 0x9A9EA8u : 0xF8F8F8u;

    if (kind == 0) {
        rx_disc(c, cx, cy, 10, -3, 3, RX_OUTLINE, 150);
        return;
    }
    rx_disc(c, cx, cy, 18, -5, 5, RX_OUTLINE, 255);
    rx_disc(c, cx, cy, 14, -4, -1, top, 255);
    rx_disc(c, cx, cy, 14, 1, 4, bot, 255);
    ui_fill(c, cx - 1, cy - 1, 3, 3, R(RX_OUTLINE), 255);
    ui_pixel(c, cx, cy, R(0xF8F8F8u), 255);
}

/* A side's six balls from (x, y), the middle of the first: the foe's first
 * ball at the left, yours at the right, as the game lays its bars; each
 * comes a moment after the one before when they first show. */
void rx_battle_balls(EidosUiCtx *c, int side, int x, int y, int right, const int *kinds, u32 t)
{
    int i;

    (void)right;
    if (side == 1) {
        y += 5;
    }
    for (i = 0; i < 6; i++) {
        if (t >= (u32)(4 + i * 3)) {
            rx_ball(c, x + i * 11, y, kinds[side == 0 ? 5 - i : i]);
        }
    }
}

/*
 * An Ability's plate: a bar that slides in from its Pokemon's edge of the
 * screen and ends in the lean (the white strip along its top, a gold cap at
 * its end), under the foe's plates or above yours.
 */
void rx_battle_ability(EidosUiCtx *c, const UiBattleLayout *l, int side, int tw, int th, u32 t,
                                 int leaving, int *tx, int *ty)
{
    int own = side == 0, n = l->n_own > 0 ? l->n_own : 1;
    int right = own ? l->own[0].x + l->own[0].w / 2 > l->w / 2 : l->foe_x + l->foe_w / 2 > l->w / 2;
    int ph = th + 12, lean = LEAN(ph), pw = tw + 40 + lean, k = t < 8 ? (int)t : 8, off, y;
    int xl0, xl1, xr0, xr1, cl0, cl1;

    /* in: it slows as it arrives; out: it speeds up */
    off = leaving ? pw * k * k / 64 : pw * (8 - k) * (8 - k) / 64;
    y = own ? l->own[0].y - ph - 8 : l->foe_y + n * l->foe_step + 16;
    if (right) {
        xl0 = (l->w - pw + off + lean) * FP;
        xl1 = (l->w - pw + off) * FP;
        xr0 = xr1 = (l->w + 2) * FP;
        cl0 = xl0;
        cl1 = xl1;
        *tx = l->w - pw + off + lean + 18;
    } else {
        xl0 = xl1 = 0;
        xr0 = (pw - off) * FP;
        xr1 = (pw - off - lean) * FP;
        cl0 = xr0 - 8 * FP;
        cl1 = xr1 - 8 * FP;
        *tx = pw - off - lean - 18 - tw;
    }
    *ty = y + 4 + (ph - 5 - th) / 2;
    rx_slab(c, y + 2, ph, xl0, xl1, xr0, xr1, 0, ph, 0, 0, 0x000000u, 0x000000u, 70);
    rx_slab(c, y, ph, xl0, xl1, xr0, xr1, 0, ph, 0, 0, RX_PLATE_OL, RX_PLATE_OL, 255);
    rx_slab(c, y, ph, xl0, xl1, xr0, xr1, 1, 3, right ? INSET : 0, right ? 0 : INSET, RX_WHITE, RX_WHITE, 255);
    rx_slab(c, y, ph, xl0, xl1, xr0, xr1, 4, ph - 1, right ? INSET : 0, right ? 0 : INSET, 0x4A4A4Au, 0x2C2C2Cu, 255);
    /* the cap at its end */
    rx_slab(c, y, ph, cl0, cl1, cl0 + 8 * FP, cl1 + 8 * FP, 4, ph - 1, right ? INSET : 0, right ? 0 : INSET,
         rx_mix(RX_ACCENT, 0xFFFFFFu, 40, 100), RX_ACCENT, 255);
}

void rx_place_balls(UiBattleLayout *l, int own_x, int own_y, int foe_x)
{
    l->own_balls_x = own_x + LEAN(BAR_OWN) + 16;
    l->own_balls_y = own_y + NAME_H + STRIP_H + 7;
    l->own_balls_right = 0;
    l->foe_balls_x = foe_x + 6;
    l->foe_balls_y = -1;
}
