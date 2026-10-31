/*
 * engine/ui/kit.c -- the interface kit: the pieces, drawn into the canvas.
 *
 * What this file is
 * -----------------
 *
 * The one-screen layout redraws, with new shapes, what the second screen
 * held (docs/ui-kit.md).  Every piece is code: it draws from a few
 * primitives at the DS pixel size into the canvas (engine/ui/ui.h), in the
 * colours Black and White draw, with the game's own font (assets.c) and
 * icons (assets_icons.c).  Nothing here is shipped as an image.  The shapes
 * and sizes follow the design prototype piece by piece.
 *
 * The canvas
 * ----------
 *
 * The canvas is straight (not premultiplied) RGBA.  Drawing blends source
 * over the pixel already there, so a translucent panel shows what is under
 * it, and an opaque one replaces it.  A piece that is built from several
 * layers (a panel is an outline, an edge and a fill) decides each pixel's
 * single colour first and blends once, so a translucent piece is uniformly
 * translucent and its layers do not show through each other.
 *
 * Panels
 * ------
 *
 * A panel is three nested shapes, each one pixel inside the last: a black
 * outline, an edge colour, the fill.  A shape is the box with some corners
 * cut at 45 degrees; the cut of the shape at inset i is `corner - i/2` so
 * the three diagonals stay parallel.  A pixel at (dx, dy) from a cut
 * corner is inside when dx + dy >= cut.
 *
 * Text
 * ----
 *
 * ui_text draws the game's font: glyph value 1 in the letter colour, 2 in
 * the shadow colour.  The caps of each font sit at fixed rows inside its
 * cell (first inked row and cap height below); ui_text_y(h, font) is the y
 * that puts the capitals in the middle of a box of height h, so every row
 * and button centres its label the same way.  Every row that holds an
 * icon is made at least icon height + 6 tall, and its text is centred in
 * that height.
 *
 * The context
 * -----------
 *
 * A piece is drawn at (x, y) in the context's current origin.  ui_push
 * moves the origin; the clip is in canvas pixels.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "kit.h"
#include "padglyph.h"

/* ------------------------------------------------------------------ */
/* Themes                                                              */
/* ------------------------------------------------------------------ */

/* accent, accent2, fill, edge */
const UiTheme UI_THEME_MENU    = { UI_CYAN,   UI_CYAN2, UI_BODY,  UI_EDGE };
const UiTheme UI_THEME_PARTY   = { UI_SKY,    UI_NAVY2, UI_NAVY,  0x084A73u };
const UiTheme UI_THEME_BAG     = { UI_WHITE,  UI_PINK2, 0x3C0A1Au, UI_RED };
const UiTheme UI_THEME_DEX     = { UI_LIME,   0x285014u, UI_BODY2, UI_EDGE };
const UiTheme UI_THEME_CARD    = { UI_CARD,   UI_CARD_D, 0x08201Cu, UI_CARD_D };
const UiTheme UI_THEME_BATTLE  = { UI_CURSOR, UI_RED2,  UI_BODY,  UI_EDGE };
const UiTheme UI_THEME_OPTIONS = { UI_CYAN,   UI_CYAN2, UI_BODY2, UI_EDGE };

#define SHADOW_DARK   0x313131u     /* text shadow on plates               */
#define SHADOW_SEL    0x003C50u     /* text shadow on a selected cyan row  */

/* ------------------------------------------------------------------ */
/* The context and the primitives                                      */
/* ------------------------------------------------------------------ */

void ui_begin(EidosUiCtx *c, EidosUiCanvas *canvas, int w, int h, int k)
{
    size_t need;

    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    need = (size_t)w * (size_t)h * 4;
    if (canvas->rgba == NULL || canvas->w != w || canvas->h != h) {
        u8 *p = (u8 *)realloc(canvas->rgba, need);
        if (p == NULL) {
            /* Out of memory: keep the old picture's size and draw nothing. */
            c->canvas = canvas;
            c->ox = c->oy = 0;
            c->clip.x = c->clip.y = c->clip.w = c->clip.h = 0;
            return;
        }
        canvas->rgba = p;
        canvas->w = w;
        canvas->h = h;
    }
    canvas->k = k;
    memset(canvas->rgba, 0, need);
    canvas->generation++;
    c->canvas = canvas;
    c->ox = 0;
    c->oy = 0;
    c->clip.x = 0;
    c->clip.y = 0;
    c->clip.w = w;
    c->clip.h = h;
}

void ui_push(EidosUiCtx *c, int dx, int dy, EidosUiRect *saved_clip)
{
    if (saved_clip != NULL) {
        *saved_clip = c->clip;
    }
    c->ox += dx;
    c->oy += dy;
}

void ui_pop(EidosUiCtx *c, int dx, int dy, const EidosUiRect *saved_clip)
{
    c->ox -= dx;
    c->oy -= dy;
    if (saved_clip != NULL) {
        c->clip = *saved_clip;
    }
}

void ui_clip(EidosUiCtx *c, int x, int y, int w, int h)
{
    int x0 = c->ox + x, y0 = c->oy + y, x1 = x0 + w, y1 = y0 + h;
    int cx1 = c->clip.x + c->clip.w, cy1 = c->clip.y + c->clip.h;

    if (x0 < c->clip.x) {
        x0 = c->clip.x;
    }
    if (y0 < c->clip.y) {
        y0 = c->clip.y;
    }
    if (x1 > cx1) {
        x1 = cx1;
    }
    if (y1 > cy1) {
        y1 = cy1;
    }
    c->clip.x = x0;
    c->clip.y = y0;
    c->clip.w = x1 > x0 ? x1 - x0 : 0;
    c->clip.h = y1 > y0 ? y1 - y0 : 0;
}

/* Blend one colour over a canvas pixel (straight alpha). */
static void blend_at(EidosUiCanvas *cv, int px, int py, u32 rgb, int a)
{
    u8 *d = cv->rgba + ((size_t)py * (size_t)cv->w + (size_t)px) * 4;
    int sr = (int)((rgb >> 16) & 255), sg = (int)((rgb >> 8) & 255), sb = (int)(rgb & 255);
    int da = d[3];

    if (a <= 0) {
        return;
    }
    if (a >= 255 || da == 0) {
        d[0] = (u8)sr;
        d[1] = (u8)sg;
        d[2] = (u8)sb;
        d[3] = (u8)(a > 255 ? 255 : a);
        return;
    }
    {
        int oa = a + (da * (255 - a) + 127) / 255;
        int wd = da * (255 - a);           /* destination weight (x255)   */
        int ws = a * 255;                  /* source weight (x255)        */
        int den = oa * 255;

        d[0] = (u8)((sr * ws + d[0] * wd + den / 2) / den);
        d[1] = (u8)((sg * ws + d[1] * wd + den / 2) / den);
        d[2] = (u8)((sb * ws + d[2] * wd + den / 2) / den);
        d[3] = (u8)oa;
    }
}

/* One pixel at (x, y) in the current origin, clipped. */
static void put(EidosUiCtx *c, int x, int y, u32 rgb, int a)
{
    int px = c->ox + x, py = c->oy + y;

    if (px < c->clip.x || py < c->clip.y || px >= c->clip.x + c->clip.w ||
        py >= c->clip.y + c->clip.h) {
        return;
    }
    if (px < 0 || py < 0 || px >= c->canvas->w || py >= c->canvas->h) {
        return;
    }
    blend_at(c->canvas, px, py, rgb, a);
}

void ui_fill(EidosUiCtx *c, int x, int y, int w, int h, u32 rgb, int a)
{
    /* the rectangle clipped once, then each pixel blended as put() would */
    int x0 = c->ox + x, y0 = c->oy + y, x1 = x0 + w, y1 = y0 + h, px, py;

    if (x0 < c->clip.x) x0 = c->clip.x;
    if (y0 < c->clip.y) y0 = c->clip.y;
    if (x1 > c->clip.x + c->clip.w) x1 = c->clip.x + c->clip.w;
    if (y1 > c->clip.y + c->clip.h) y1 = c->clip.y + c->clip.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->canvas->w) x1 = c->canvas->w;
    if (y1 > c->canvas->h) y1 = c->canvas->h;
    for (py = y0; py < y1; py++) {
        for (px = x0; px < x1; px++) {
            blend_at(c->canvas, px, py, rgb, a);
        }
    }
}

void ui_line(EidosUiCtx *c, int x0, int y0, int x1, int y1, u32 rgb, int a)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err;

    err = dx + dy;
    for (;;) {
        int e2;

        put(c, x0, y0, rgb, a);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Is pixel (px, py) inside the ellipse that fills a w x h box? */
static int in_ellipse(int px, int py, int w, int h)
{
    long u = 2L * px + 1 - w, v = 2L * py + 1 - h;

    if (px < 0 || py < 0 || px >= w || py >= h) {
        return 0;
    }
    return u * u * (long)h * h + v * v * (long)w * w <= (long)w * w * (long)h * h;
}

void ui_ellipse(EidosUiCtx *c, int x, int y, int w, int h, u32 fill, u32 outline, int a)
{
    int xx, yy;

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            u32 col;

            if (!in_ellipse(xx, yy, w, h)) {
                continue;
            }
            if (!in_ellipse(xx - 1, yy, w, h) || !in_ellipse(xx + 1, yy, w, h) ||
                !in_ellipse(xx, yy - 1, w, h) || !in_ellipse(xx, yy + 1, w, h)) {
                col = outline != UI_NONE ? outline : fill;
            } else {
                col = fill;
            }
            if (col != UI_NONE) {
                put(c, x + xx, y + yy, col, a);
            }
        }
    }
}

/*
 * A rectangle of a DS screen (256 x 192 RGBA, as the game composed it),
 * drawn at (x, y) scaled by k.  With key set, the backdrop is left out:
 * every pixel of the colour at the rectangle's top-left corner that is
 * reachable from the rectangle's edge (a flood from the border), so the
 * game's own picture comes without the panel it was drawn on, while the
 * same colour inside the picture stays.
 */
void ui_blit_screen(EidosUiCtx *c, int x, int y, const u8 *screen,
                    int sx, int sy, int w, int h, int k, int key)
{
    static u8 mask[256 * 192];
    static int stack[256 * 192];
    const u8 *bg;
    int xx, yy, dx, dy, sp = 0;

    if (screen == NULL || w <= 0 || h <= 0 || sx < 0 || sy < 0 ||
        sx + w > 256 || sy + h > 192) {
        return;
    }
    if (k < 1) {
        k = 1;
    }
    memset(mask, 0, (size_t)w * (size_t)h);
    if (key) {
        bg = screen + ((size_t)sy * 256 + (size_t)sx) * 4;
        for (yy = 0; yy < h; yy++) {
            for (xx = 0; xx < w; xx++) {
                if (xx == 0 || yy == 0 || xx == w - 1 || yy == h - 1) {
                    stack[sp++] = yy * w + xx;
                }
            }
        }
        while (sp > 0) {
            int i = stack[--sp], px = i % w, py = i / w;
            const u8 *p = screen + ((size_t)(sy + py) * 256 + (size_t)(sx + px)) * 4;

            if (mask[i] || p[0] != bg[0] || p[1] != bg[1] || p[2] != bg[2]) {
                continue;
            }
            mask[i] = 1;
            if (px > 0 && !mask[i - 1] && sp < 256 * 192) stack[sp++] = i - 1;
            if (px < w - 1 && !mask[i + 1] && sp < 256 * 192) stack[sp++] = i + 1;
            if (py > 0 && !mask[i - w] && sp < 256 * 192) stack[sp++] = i - w;
            if (py < h - 1 && !mask[i + w] && sp < 256 * 192) stack[sp++] = i + w;
        }
    }
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            const u8 *p = screen + ((size_t)(sy + yy) * 256 + (size_t)(sx + xx)) * 4;
            u32 rgb;

            if (mask[yy * w + xx]) {
                continue;
            }
            rgb = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            for (dy = 0; dy < k; dy++) {
                for (dx = 0; dx < k; dx++) {
                    put(c, x + xx * k + dx, y + yy * k + dy, rgb, 255);
                }
            }
        }
    }
}

void ui_image(EidosUiCtx *c, int x, int y, const UiImage *im, int k)
{
    int xx, yy, dx, dy;

    if (im == NULL || im->rgba == NULL) {
        return;
    }
    if (k < 1) {
        k = 1;
    }
    for (yy = 0; yy < im->h; yy++) {
        for (xx = 0; xx < im->w; xx++) {
            const u8 *p = im->rgba + ((size_t)yy * (size_t)im->w + (size_t)xx) * 4;
            u32 rgb;

            if (p[3] == 0) {
                continue;
            }
            rgb = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            for (dy = 0; dy < k; dy++) {
                for (dx = 0; dx < k; dx++) {
                    put(c, x + xx * k + dx, y + yy * k + dy, rgb, p[3]);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

/* The first inked row of the capitals and their height, per font. */
static const int k_cap_top[3] = { 2, 0, 3 };
static const int k_cap_h[3]   = { 11, 8, 8 };

/* Next UTF-8 code point; invalid bytes give '?'. */
static u32 next_code(const char **sp)
{
    const u8 *s = (const u8 *)*sp;
    u32 c = s[0];
    int n, i;

    if (c < 0x80) {
        *sp += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        c &= 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        c &= 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        c &= 0x07;
    } else {
        *sp += 1;
        return '?';
    }
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *sp += i;
            return '?';
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *sp += n + 1;
    return c;
}

int ui_text(EidosUiCtx *c, int x, int y, const char *utf8, u32 color,
            s32 shadow, int font, int k)
{
    const char *s = utf8;

    if (utf8 == NULL) {
        return x;
    }
    if (k < 1) {
        k = 1;
    }
    while (*s != '\0') {
        u32 code = next_code(&s);
        UiGlyph g;

        if (!ui_font_glyph(font, code, &g)) {
            x += ui_font_fallback_adv(font) * k;
            continue;
        }
        {
            int gx, gy, i = 0;

            for (gy = 0; gy < g.cell_h; gy++) {
                for (gx = 0; gx < g.cell_w; gx++, i++) {
                    int v = (g.bitmap[(i * 2) >> 3] >> (6 - ((i & 3) * 2))) & 3;
                    u32 col;
                    int dx, dy;

                    if (v == 0) {
                        continue;
                    }
                    if (v == 1) {
                        col = color;
                    } else if (shadow >= 0) {
                        col = (u32)shadow;
                    } else {
                        continue;
                    }
                    for (dy = 0; dy < k; dy++) {
                        for (dx = 0; dx < k; dx++) {
                            put(c, x + (g.bearing + gx) * k + dx, y + gy * k + dy, col, 255);
                        }
                    }
                }
            }
        }
        x += g.adv * k;
    }
    return x;
}

int ui_text_w(const char *utf8, int font)
{
    const char *s = utf8;
    int w = 0;

    if (utf8 == NULL) {
        return 0;
    }
    while (*s != '\0') {
        u32 code = next_code(&s);
        UiGlyph g;

        w += ui_font_glyph(font, code, &g) ? g.adv : ui_font_fallback_adv(font);
    }
    return w;
}

/* floor(v / 2) */
static int half_floor(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

int ui_text_y(int h, int font)
{
    if (font < 0 || font > 2) {
        font = 0;
    }
    return half_floor(h - k_cap_h[font]) - k_cap_top[font];
}

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

int ui_wrap(const char *utf8, int width, int font,
            void (*line)(void *user, const char *start, int len), void *user)
{
    const char *p = utf8;
    const char *ls = NULL, *le = NULL;      /* the current line            */
    int lines = 0;

    if (utf8 == NULL) {
        return 0;
    }
    while (*p != '\0') {
        const char *ws, *we;
        char tmp[512];
        int tl;

        while (*p != '\0' && is_space(*p)) {
            if (*p == '\n' && ls != NULL) {
                if (line != NULL) {
                    line(user, ls, (int)(le - ls));
                }
                lines++;
                ls = NULL;
            }
            p++;
        }
        if (*p == '\0') {
            break;
        }
        ws = p;
        while (*p != '\0' && !is_space(*p)) {
            p++;
        }
        we = p;
        if (ls == NULL) {
            ls = ws;
            le = we;
            continue;
        }
        /* would ls..we fit? */
        tl = (int)(we - ls);
        if (tl > (int)sizeof tmp - 1) {
            tl = (int)sizeof tmp - 1;
        }
        memcpy(tmp, ls, (size_t)tl);
        tmp[tl] = '\0';
        if (ui_text_w(tmp, font) > width) {
            if (line != NULL) {
                line(user, ls, (int)(le - ls));
            }
            lines++;
            ls = ws;
            le = we;
        } else {
            le = we;
        }
    }
    if (ls != NULL) {
        if (line != NULL) {
            line(user, ls, (int)(le - ls));
        }
        lines++;
    }
    return lines;
}

/* Draw `len` bytes of text (not NUL-terminated). */
static int text_n(EidosUiCtx *c, int x, int y, const char *s, int len, u32 color,
                  s32 shadow, int font)
{
    char tmp[256];

    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    return ui_text(c, x, y, tmp, color, shadow, font, 1);
}

/* ------------------------------------------------------------------ */
/* Panels                                                              */
/* ------------------------------------------------------------------ */

/* Is (px, py) inside the panel shape at the given inset? */
static int in_shape(int px, int py, int w, int h, int cut, int corner, int inset)
{
    int cc = corner - inset / 2;
    int dx = px - inset, dy = py - inset;
    int rx = (w - 1 - inset) - px, by = (h - 1 - inset) - py;

    if (cc < 0) {
        cc = 0;
    }
    if (dx < 0 || dy < 0 || rx < 0 || by < 0) {
        return 0;
    }
    if ((cut & UI_CUT_TL) && dx + dy < cc) {
        return 0;
    }
    if ((cut & UI_CUT_TR) && rx + dy < cc) {
        return 0;
    }
    if ((cut & UI_CUT_BR) && rx + by < cc) {
        return 0;
    }
    if ((cut & UI_CUT_BL) && dx + by < cc) {
        return 0;
    }
    return 1;
}

/* The panel, with an optional lighter fill right of a diagonal (the start
 * menu's buttons): pixels with x >= split_x + (h - y) take split_rgb. */
static void panel_split(EidosUiCtx *c, int x, int y, int w, int h, int cut,
                        int corner, u32 fill, u32 edge, int a, u32 split_rgb,
                        int split_x)
{
    int xx, yy;

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            u32 col;

            if (!in_shape(xx, yy, w, h, cut, corner, 0)) {
                continue;
            }
            if (!in_shape(xx, yy, w, h, cut, corner, 1)) {
                col = UI_INK;
            } else if (!in_shape(xx, yy, w, h, cut, corner, 2)) {
                col = edge;
            } else {
                col = fill;
                if (split_rgb != UI_NONE && xx >= split_x + (h - yy)) {
                    col = split_rgb;
                }
            }
            put(c, x + xx, y + yy, col, a);
        }
    }
}

void ui_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner,
              u32 fill, u32 edge, int a)
{
    panel_split(c, x, y, w, h, cut, corner, fill, edge, a, UI_NONE, 0);
}

/* The default cut: top left and bottom right. */
#define CUT_DEF (UI_CUT_TL | UI_CUT_BR)

/* ------------------------------------------------------------------ */
/* Header, hints, bar, tags                                            */
/* ------------------------------------------------------------------ */

#define HEADER_CUT 6

void ui_header(EidosUiCtx *c, int x, int y, int w, const char *title,
               const char *sub, const char *right, u32 accent)
{
    const int h = 22;
    int i, tx, ty = ui_text_y(h - 3, UI_FONT_LARGE);

    /* the title bar, its top left corner cut like the modal's (a header
     * sits a few pixels inside a modal whose corner is cut 6 to 12, and a
     * square corner would cross the modal's edge) */
    for (i = 0; i < h - 3; i++) {
        int in = i < HEADER_CUT ? HEADER_CUT - i : 0;

        ui_fill(c, x + in, y + i, w - in, 1, UI_BODY2, 255);
    }
    ui_fill(c, x, y + h - 3, w, 1, accent, 255);            /* the accent rule  */
    ui_fill(c, x, y + h - 2, w, 1, UI_INK, 255);
    for (i = 0; i < 5; i++) {                               /* the chevron      */
        ui_fill(c, x + 4 + i, y + 4 + i, 1, 11 - 2 * i, accent, 255);
    }
    tx = ui_text(c, x + 13, y + ty, title, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    if (sub != NULL && sub[0] != '\0') {
        ui_line(c, tx + 6, y + 15, tx + 11, y + 4, UI_EDGE2, 255);
        ui_text(c, tx + 15, y + ty, sub, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
    }
    if (right != NULL && right[0] != '\0') {
        ui_text(c, x + w - ui_text_w(right, UI_FONT_LARGE) - 8, y + ty, right,
                UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
    }
}

/*
 * The key a hint names, as the player's device calls it.  Recipes write
 * the DS's names ("A", "START", "L R", the arrows); the interface layer
 * hands the kit the names of the keys the player last used
 * (ui_set_key_names: the keyboard's bound keys, or the pad's buttons), and
 * every key cap shows those.
 */
static const char *g_key_names[12];

void ui_set_key_names(const char *const names[12])
{
    int i;

    for (i = 0; i < 12; i++) {
        g_key_names[i] = names != NULL ? names[i] : NULL;
    }
}

const char *ui_key_label(const char *ds)
{
    static const char *const ds_names[12] = {
        "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y"
    };
    static char pair[48];
    int i;

    if (ds == NULL) {
        return "";
    }
    if (strcmp(ds, "L R") == 0 && g_key_names[9] != NULL && g_key_names[8] != NULL) {
        snprintf(pair, sizeof(pair), "%s %s", g_key_names[9], g_key_names[8]);
        return pair;
    }
    for (i = 0; i < 12; i++) {
        if (strcmp(ds, ds_names[i]) == 0) {
            return g_key_names[i] != NULL ? g_key_names[i] : ds;
        }
    }
    return ds;                  /* arrows and anything else stay as written */
}

/*
 * A gamepad's control in a key name: UI_PAD_TOKEN and then 'A' plus the
 * control (padglyph.h), drawn as that control's glyph in the family the
 * interface layer set (ui_set_pad_family).  "L R" becomes two of them.
 */
static int g_pad_family;

void ui_set_pad_family(int family)
{
    g_pad_family = family;
}

int ui_pad_family(void)
{
    return g_pad_family;
}

/* The glyphs of a run of tokens: their width, and drawn when c is set. */
static int pad_tokens(EidosUiCtx *c, int x, int y, const char *s)
{
    int w = 0;

    for (; *s != '\0'; s++) {
        if (*s == UI_PAD_TOKEN && s[1] != '\0') {
            int ctl = s[1] - 'A';

            if (w > 0) {
                w += 2;
            }
            w += c != NULL ? ui_pad_glyph(c, x + w, y, g_pad_family, ctl)
                           : ui_pad_glyph_w(g_pad_family, ctl);
            s++;
        }
    }
    return w;
}

int ui_keycap_w(const char *label)
{
    int tw;

    label = ui_key_label(label);
    if (label[0] == UI_PAD_TOKEN) {
        return pad_tokens(NULL, 0, 0, label);
    }
    tw = ui_text_w(label, UI_FONT_SMALL);
    return tw + 8 > 13 ? tw + 8 : 13;
}

int ui_keycap(EidosUiCtx *c, int x, int y, const char *label)
{
    int tw;

    label = ui_key_label(label);
    if (label[0] == UI_PAD_TOKEN) {
        return pad_tokens(c, x, y, label);
    }
    tw = ui_text_w(label, UI_FONT_SMALL);
    int w = tw + 8 > 13 ? tw + 8 : 13;

    ui_panel(c, x, y, w, 13, CUT_DEF, 2, UI_BODY, UI_EDGE2, 255);
    ui_text(c, x + (w - tw) / 2, y + ui_text_y(13, UI_FONT_SMALL) + 1, label, UI_WHITE,
            UI_EDGE, UI_FONT_SMALL, 1);
    return w;
}

int ui_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n,
             const u32 *keys)
{
    int i, x = 6;

    ui_fill(c, 0, y, w, 17, UI_BODY2, 235);
    ui_fill(c, 0, y, w, 1, UI_EDGE, 255);
    for (i = 0; i < n; i++) {
        const char *key = pairs[2 * i], *what = pairs[2 * i + 1];
        int kw = ui_keycap_w(key);
        int aw = ui_text_w(what, UI_FONT_SMALL);
        EidosUiRect r;
        int state;

        if (kw < 13) {
            kw = 13;
        }
        r.x = x - 2;            /* eidos_ui_hit adds the origin */
        r.y = y;
        r.w = kw + 4 + aw + 4;
        r.h = 17;
        state = eidos_ui_hit(r);
        if (state & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, y + 1, r.w, 15, UI_EDGE, 255);
        }
        if ((state & EIDOS_UI_CLICK) && keys != NULL && keys[i] != 0) {
            eidos_ui_press(keys[i], 2);
        }
        ui_keycap(c, x, y + 2, key);
        x += kw + 4;
        /* the small font's letters sit a pixel above its cell's middle */
        x = ui_text(c, x, y + 3 + ui_text_y(13, UI_FONT_SMALL), what, UI_LIGHT,
                    UI_EDGE, UI_FONT_SMALL, 1) + 10;
    }
    return 17;
}

void ui_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den)
{
    u32 col, hi;
    int fw, r, g, b;

    ui_fill(c, x, y, w, h, UI_INK, 255);
    ui_fill(c, x + 1, y + 1, w - 2, h - 2, 0x313131u, 255);
    if (den <= 0 || num <= 0) {
        return;
    }
    if (num > den) {
        num = den;
    }
    fw = (int)(((long)(w - 2) * num) / den);
    col = num * 2 > den ? UI_HP_G : num * 5 > den ? UI_HP_Y : UI_HP_R;
    if (fw <= 0) {
        return;
    }
    r = (int)((col >> 16) & 255) + 60;
    g = (int)((col >> 8) & 255) + 60;
    b = (int)(col & 255) + 60;
    hi = ((u32)(r > 255 ? 255 : r) << 16) | ((u32)(g > 255 ? 255 : g) << 8) |
         (u32)(b > 255 ? 255 : b);
    ui_fill(c, x + 1, y + 1, fw, h - 2, col, 255);
    ui_fill(c, x + 1, y + 1, fw, 1, hi, 255);
}

/* Black and White's own type numbers (17 types, Fire = 9), then the
 * unknown mark at UI_TYPE_UNKNOWN. */
static const char *const k_type_name[18] = {
    "NORMAL", "FIGHTING", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST",
    "STEEL", "FIRE", "WATER", "GRASS", "ELECTRIC", "PSYCHIC", "ICE",
    "DRAGON", "DARK", "???"
};

/* NORMAL and FIRE are sampled from the game's move screen; the rest are
 * the usual type colours until the game's own type palette is read. */
static const u32 k_type_color[18] = {
    0xACA494u, 0xC03028u, 0xA890F0u, 0xA040A0u, 0xE0C068u, 0xB8A038u, 0xA8B820u,
    0x705898u, 0xB8B8D0u, 0xF65231u, 0x6890F0u, 0x78C850u, 0xF8D030u,
    0xF85888u, 0x98D8D8u, 0x7038F8u, 0x705848u, 0x525252u
};

/* Move names the one-screen layout spells as later games do: Black and
 * White fit every name in 12 letters (DragonBreath, Hi Jump Kick); from X
 * and Y on they read in full.  Indexed by the game's move number. */
static const struct { u16 id; const char *name; } k_modern_moves[] = {
    { 3, "Double Slap" },    { 9, "Thunder Punch" },   { 11, "Vice Grip" },
    { 28, "Sand Attack" },   { 49, "Sonic Boom" },     { 61, "Bubble Beam" },
    { 76, "Solar Beam" },    { 77, "Poison Powder" },  { 84, "Thunder Shock" },
    { 108, "Smokescreen" },  { 120, "Self-Destruct" }, { 135, "Soft-Boiled" },
    { 136, "High Jump Kick" }, { 185, "Feint Attack" }, { 223, "Dynamic Punch" },
    { 225, "Dragon Breath" }, { 245, "Extreme Speed" }, { 246, "Ancient Power" },
    { 265, "Smelling Salts" }, { 297, "Feather Dance" }, { 320, "Grass Whistle" },
};

const char *ui_move_name_modern(int id)
{
    size_t i;

    for (i = 0; i < sizeof(k_modern_moves) / sizeof(k_modern_moves[0]); i++) {
        if (k_modern_moves[i].id == id) {
            return k_modern_moves[i].name;
        }
    }
    return NULL;
}

const char *ui_type_name(int type)
{
    return (type >= 0 && type < 18) ? k_type_name[type] : "???";
}

u32 ui_type_color(int type)
{
    return (type >= 0 && type < 18) ? k_type_color[type] : UI_EDGE2;
}

void ui_type_tag(EidosUiCtx *c, int x, int y, int w, int type)
{
    u32 col = ui_type_color(type);
    u32 dark = ((col >> 17) & 0x7F) << 16 | ((col >> 9) & 0x7F) << 8 | ((col >> 1) & 0x7F);
    const char *name = ui_type_name(type);

    ui_panel(c, x, y, w, 13, CUT_DEF, 3, col, col, 255);
    ui_text(c, x + (w - ui_text_w(name, UI_FONT_SMALL)) / 2,
            y + ui_text_y(13, UI_FONT_SMALL), name, UI_WHITE, (s32)dark,
            UI_FONT_SMALL, 1);
}

static const char *const k_status_name[7] = { "PSN", "PAR", "BRN", "SLP", "FRZ", "FNT", "PKRS" };
static const u32 k_status_color[7] = {
    0xA040A0u, 0xE6BE1Eu, 0xE65A28u, 0x8C8C8Cu, 0x5AC8E6u, 0x961428u, 0xC83CC8u
};

void ui_status_badge(EidosUiCtx *c, int x, int y, int status)
{
    u32 col, dark;
    const char *name;
    int tw, w;

    if (status < 0 || status > 6) {
        return;
    }
    col = k_status_color[status];
    dark = ((col >> 17) & 0x7F) << 16 | ((col >> 9) & 0x7F) << 8 | ((col >> 1) & 0x7F);
    name = k_status_name[status];
    tw = ui_text_w(name, UI_FONT_SMALL);
    w = tw + 8 > 24 ? tw + 8 : 24;
    ui_panel(c, x, y, w, 11, CUT_DEF, 2, col, col, 255);
    ui_text(c, x + (w - tw) / 2, y + ui_text_y(11, UI_FONT_SMALL), name, UI_WHITE,
            (s32)dark, UI_FONT_SMALL, 1);
}

void ui_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown,
                  int total, u32 accent)
{
    int th, ty, track = h - 2;

    ui_fill(c, x, y, 5, h, UI_INK, 255);
    ui_fill(c, x + 1, y + 1, 3, track, UI_BODY2, 255);
    if (total <= 0 || shown >= total) {
        th = track;
        ty = 0;
    } else {
        th = (int)((long)track * shown / total);
        if (th < 6) {
            th = 6;
        }
        if (th > track) {
            th = track;
        }
        ty = (int)((long)(track - th) * first / (total - shown));
        if (ty < 0) {
            ty = 0;
        }
        if (ty > track - th) {
            ty = track - th;
        }
    }
    ui_fill(c, x + 1, y + 1 + ty, 3, th, accent, 255);
}

/* ------------------------------------------------------------------ */
/* Rows, message, toast                                                */
/* ------------------------------------------------------------------ */

void ui_row(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover,
            const UiTheme *t)
{
    u32 fill = t->fill, edge = t->edge;

    if (selected) {
        fill = t->accent2;
        edge = t->accent;
    } else if (hover) {
        fill = t->accent2;      /* the selected look without the outline */
    }
    ui_panel(c, x, y, w, h, CUT_DEF, 4, fill, edge, 255);
}

void ui_message(EidosUiCtx *c, int x, int y, int w, const char *utf8)
{
    ui_panel(c, x, y, w, 30, CUT_DEF, 6, UI_BODY, UI_EDGE, 215);
    ui_text(c, x + 10, y + ui_text_y(30, UI_FONT_LARGE), utf8, UI_WHITE, UI_GREY,
            UI_FONT_LARGE, 1);
}

void ui_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon)
{
    int h = 22, tx = 8, w;

    if (icon != NULL && icon->h + 6 > h) {
        h = icon->h + 6;
    }
    w = ui_text_w(utf8, UI_FONT_LARGE) + 20 + (icon != NULL ? icon->w + 4 : 0);
    ui_panel(c, x, y, w, h, CUT_DEF, 5, UI_BODY, UI_CYAN, 235);
    if (icon != NULL) {
        ui_image(c, x + tx, y + (h - icon->h) / 2, icon, 1);
        tx += icon->w + 4;
    }
    ui_text(c, x + tx, y + ui_text_y(h, UI_FONT_LARGE), utf8, UI_WHITE, UI_GREY,
            UI_FONT_LARGE, 1);
}

void ui_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color)
{
    const int arm = 5;
    int i;

    for (i = 0; i < 2; i++) {
        /* two passes: a black shadow one pixel out, then the colour */
        u32 col = i == 0 ? UI_INK : color;
        int o = i == 0 ? 1 : 0;

        ui_fill(c, x - o, y - o, arm + o, 2 + o, col, 255);                 /* TL */
        ui_fill(c, x - o, y - o, 2 + o, arm + o, col, 255);
        ui_fill(c, x + w - arm, y - o, arm + o, 2 + o, col, 255);           /* TR */
        ui_fill(c, x + w - 2, y - o, 2 + o, arm + o, col, 255);
        ui_fill(c, x + w - arm, y + h - 2, arm + o, 2 + o, col, 255);       /* BR */
        ui_fill(c, x + w - 2, y + h - arm, 2 + o, arm + o, col, 255);
        ui_fill(c, x - o, y + h - 2, arm + o, 2 + o, col, 255);             /* BL */
        ui_fill(c, x - o, y + h - arm, 2 + o, arm + o, col, 255);
    }
}

/* ------------------------------------------------------------------ */
/* Tabs and form controls                                              */
/* ------------------------------------------------------------------ */

int ui_tab(EidosUiCtx *c, int x, int y, const char *label, int selected,
           u32 accent, u32 accent2)
{
    int w = ui_text_w(label, UI_FONT_SMALL) + 16;

    ui_panel(c, x, y, w, 14, CUT_DEF, 4, selected ? accent2 : UI_BODY2,
             selected ? accent : UI_EDGE, 255);
    ui_text(c, x + 8, y + ui_text_y(14, UI_FONT_SMALL), label,
            selected ? UI_WHITE : UI_GREY, selected ? -1 : (s32)UI_EDGE,
            UI_FONT_SMALL, 1);
    return w;
}

void ui_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected)
{
    ui_panel(c, x, y, w, 18, CUT_DEF, 4, selected ? UI_PINK : 0x3C0A1Au,
             selected ? UI_WHITE : UI_RED, 255);
    ui_text(c, x + 6, y + ui_text_y(18, UI_FONT_LARGE), label, UI_WHITE,
            selected ? (s32)UI_PINK2 : (s32)UI_RED2, UI_FONT_LARGE, 1);
}

void ui_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent)
{
    ui_panel(c, x, y, 26, 12, CUT_DEF, 3, on ? UI_CYAN2 : UI_BODY2,
             on ? accent : UI_EDGE, 255);
    ui_fill(c, x + (on ? 15 : 3), y + 3, 8, 6, on ? UI_WHITE : UI_EDGE2, 255);
}

void ui_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    int fx, i, yy;

    if (den <= 0) {
        den = 1;
    }
    if (num < 0) {
        num = 0;
    }
    if (num > den) {
        num = den;
    }
    fx = (int)((long)(w - 2) * num / den);
    if (fx < 3) {
        fx = 3;
    }
    if (fx > w - 4) {
        fx = w - 4;
    }
    ui_fill(c, x, y + 3, w, 3, UI_INK, 255);
    ui_fill(c, x + 1, y + 4, w - 2, 1, UI_EDGE, 255);
    ui_fill(c, x + 1, y + 4, fx, 1, accent, 255);
    /* the knob: a box with a pointed bottom, white with a black outline */
    for (yy = 0; yy < 9; yy++) {
        int half = yy <= 6 ? 3 : (yy == 7 ? 1 : 0);

        for (i = -half; i <= half; i++) {
            int edge = yy == 0 || yy >= 7 || i == -half || i == half || yy == 6;
            put(c, x + fx + i, y + yy, edge ? UI_INK : UI_WHITE, 255);
        }
    }
}

void ui_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent)
{
    int ty = ui_text_y(15, UI_FONT_LARGE);

    ui_panel(c, x, y, w, 15, CUT_DEF, 3, UI_BODY2, UI_EDGE, 255);
    ui_text(c, x + 4, y + ty, "\xE2\x86\x90", accent, -1, UI_FONT_LARGE, 1);
    ui_text(c, x + w - 12, y + ty, "\xE2\x86\x92", accent, -1, UI_FONT_LARGE, 1);
    ui_text(c, x + (w - ui_text_w(value, UI_FONT_LARGE)) / 2, y + ty, value, UI_WHITE,
            UI_EDGE, UI_FONT_LARGE, 1);
}

void ui_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent)
{
    int i, ty = ui_text_y(18, UI_FONT_LARGE), tx;

    ui_panel(c, x, y, w, 18, CUT_DEF, 4, UI_BODY, accent, 255);
    tx = ui_text(c, x + 6, y + ty, text, UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
    ui_fill(c, tx + 1, y + 3, 1, 11, accent, 255);
    for (i = 0; i < room; i++) {
        ui_line(c, tx + 5 + i * 7, y + 13, tx + 9 + i * 7, y + 13, UI_EDGE2, 255);
    }
}

void ui_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent)
{
    int tw = ui_text_w(label, UI_FONT_LARGE);

    if (w <= 0) {
        w = tw + 8 > 16 ? tw + 8 : 16;
    }
    ui_panel(c, x, y, w, 16, CUT_DEF, 3, selected ? UI_CYAN2 : UI_BODY2,
             selected ? accent : UI_EDGE, 255);
    ui_text(c, x + (w - tw) / 2, y + ui_text_y(16, UI_FONT_LARGE), label, UI_WHITE,
            selected ? (s32)SHADOW_SEL : (s32)UI_GREY, UI_FONT_LARGE, 1);
}

typedef struct DialogLines {
    EidosUiCtx *c;
    int x, y, n;
} DialogLines;

static void dialog_line(void *user, const char *s, int len)
{
    DialogLines *d = (DialogLines *)user;

    text_n(d->c, d->x, d->y + d->n * 15 + 7, s, len, UI_WHITE, UI_GREY, UI_FONT_LARGE);
    d->n++;
}

int ui_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg,
              const char *const *choices, int n, int cur, u32 accent)
{
    int lines = ui_wrap(msg, w - 20, UI_FONT_LARGE, NULL, NULL);
    int h = 14 + 15 * lines + 26;
    int i, bx;
    DialogLines d;

    ui_panel(c, x, y, w, h, CUT_DEF, 8, UI_BODY, UI_EDGE2, 255);
    d.c = c;
    d.x = x + 10;
    d.y = y;
    d.n = 0;
    ui_wrap(msg, w - 20, UI_FONT_LARGE, dialog_line, &d);
    bx = x + w - 10;
    for (i = n - 1; i >= 0; i--) {
        int sel = i == cur;

        bx -= 56;
        ui_panel(c, bx, y + h - 24, 56, 18, CUT_DEF, 4, sel ? UI_CYAN2 : UI_BODY2,
                 sel ? accent : UI_EDGE, 255);
        ui_text(c, bx + (56 - ui_text_w(choices[i], UI_FONT_LARGE)) / 2,
                y + h - 24 + ui_text_y(18, UI_FONT_LARGE), choices[i], UI_WHITE,
                sel ? (s32)SHADOW_SEL : (s32)UI_GREY, UI_FONT_LARGE, 1);
        bx -= 6;
    }
    return h;
}

void ui_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    int fw, i;

    ui_fill(c, x, y, w, 6, UI_INK, 255);
    ui_fill(c, x + 1, y + 1, w - 2, 4, UI_BODY2, 255);
    if (den <= 0 || num <= 0) {
        return;
    }
    if (num > den) {
        num = den;
    }
    fw = (int)((long)(w - 2) * num / den);
    for (i = 1; i < fw; i++) {
        ui_fill(c, x + i, y + 1, 1, 4, ((i / 3) & 1) == 0 ? accent : UI_CYAN2, 255);
    }
}

void ui_spinner(EidosUiCtx *c, int x, int y, int frame, u32 accent)
{
    static const int dx[8] = { 4, 3, 0, -3, -4, -3, 0, 3 };
    static const int dy[8] = { 0, 3, 4, 3, 0, -3, -4, -3 };
    int i;

    for (i = 0; i < 8; i++) {
        u32 col = ((i - frame) & 7) < 3 ? accent : UI_EDGE;

        ui_fill(c, x + 6 + dx[i] - 1, y + 6 + dy[i] - 1, 2, 2, col, 255);
    }
}

void ui_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent)
{
    int i;

    for (i = 0; i < n; i++) {
        int px = x + i * 8;

        if (i == cur) {
            ui_fill(c, px, y, 6, 6, UI_INK, 255);
            ui_fill(c, px + 1, y + 1, 4, 4, accent, 255);
        } else {
            ui_fill(c, px + 1, y + 1, 4, 4, UI_INK, 255);
            ui_fill(c, px + 2, y + 2, 2, 2, UI_EDGE2, 255);
        }
    }
}

/* A small filled shape given as one [left, right] span per row, with a
 * 1 pixel outline (a pixel with an empty 4-neighbour). */
static void span_shape(EidosUiCtx *c, int x, int y, int rows, const int *l,
                       const int *r, u32 fill, u32 outline)
{
    int yy, xx;

    for (yy = 0; yy < rows; yy++) {
        for (xx = l[yy]; xx <= r[yy]; xx++) {
            int edge = xx == l[yy] || xx == r[yy] || yy == 0 || yy == rows - 1 ||
                       xx < l[yy - 1] || xx > r[yy - 1] || xx < l[yy + 1] ||
                       xx > r[yy + 1];

            put(c, x + xx, y + yy, edge ? outline : fill, 255);
        }
    }
}

void ui_target_chevron(EidosUiCtx *c, int x, int y, u32 color)
{
    static const int l[7] = { 0, 1, 2, 3, 3, 4, 5 };
    int r[7], i;

    for (i = 0; i < 7; i++) {
        r[i] = 10 - l[i];
    }
    span_shape(c, x, y, 7, l, r, color, UI_INK);
}

void ui_party_balls(EidosUiCtx *c, int x, int y, const u8 *states, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        int bx = x + i * 10, xx, yy;
        u32 top;

        if (states[i] == 3) {
            ui_ellipse(c, bx + 1, y + 1, 7, 7, UI_NONE, UI_EDGE2, 255);
            continue;
        }
        top = states[i] == 0 ? 0xE6283Cu : states[i] == 1 ? 0xE6BE1Eu : 0x525252u;
        for (yy = 0; yy < 9; yy++) {
            for (xx = 0; xx < 9; xx++) {
                u32 col;

                if (!in_ellipse(xx, yy, 9, 9)) {
                    continue;
                }
                if (!in_ellipse(xx - 1, yy, 9, 9) || !in_ellipse(xx + 1, yy, 9, 9) ||
                    !in_ellipse(xx, yy - 1, 9, 9) || !in_ellipse(xx, yy + 1, 9, 9) ||
                    yy == 4) {
                    col = UI_INK;
                } else {
                    col = yy < 4 ? top : UI_WHITE;
                }
                put(c, bx + xx, y + yy, col, 255);
            }
        }
        put(c, bx + 4, y + 4, UI_WHITE, 255);
    }
}

int ui_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent)
{
    int w = ui_text_w(name, UI_FONT_SMALL) + 12, i;

    ui_panel(c, x, y, w, 12, CUT_DEF, 3, UI_BODY, accent, 220);
    ui_text(c, x + 6, y + ui_text_y(12, UI_FONT_SMALL), name, UI_WHITE, -1,
            UI_FONT_SMALL, 1);
    for (i = 0; i < 4; i++) {
        ui_fill(c, x + (w - 7) / 2 + i, y + 12 + i, 7 - 2 * i, 1, accent, 255);
    }
    return w;
}

void ui_category_icon(EidosUiCtx *c, int x, int y, int kind)
{
    /* Stand-ins until the game's own three icons are read from its data. */
    u32 base = kind == 0 ? 0xC83C1Eu : kind == 1 ? 0x2846A0u : 0x6E6E6Eu;

    ui_fill(c, x, y, 16, 11, UI_INK, 255);
    ui_fill(c, x + 1, y + 1, 14, 9, base, 255);
    if (kind == 0) {
        static const int l[7] = { 6, 6, 4, 2, 4, 6, 6 };
        static const int r[7] = { 8, 8, 10, 12, 10, 8, 8 };
        int yy, xx;

        for (yy = 0; yy < 7; yy++) {
            for (xx = l[yy]; xx <= r[yy]; xx++) {
                put(c, x + xx + 1, y + yy + 2, 0xFFC83Cu, 255);
            }
        }
    } else if (kind == 1) {
        ui_ellipse(c, x + 3, y + 1, 10, 9, UI_NONE, 0x8CBEFFu, 255);
        ui_fill(c, x + 6, y + 4, 4, 3, 0xC8E6FFu, 255);
    } else {
        ui_ellipse(c, x + 4, y + 2, 8, 7, UI_NONE, UI_WHITE, 255);
        ui_fill(c, x + 4, y + 5, 8, 1, UI_WHITE, 255);
    }
}

void ui_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value,
                 int num, int den, u32 accent)
{
    char v[16];
    int bw = w - 92, fw, n = value, i = 0, j;
    char tmp[16];
    int ty = ui_text_y(15, UI_FONT_MEDIUM);

    ui_panel(c, x, y, w, 15, CUT_DEF, 3, UI_BODY2, UI_EDGE, 255);
    ui_text(c, x + 6, y + ty, name, UI_GREY, UI_EDGE, UI_FONT_MEDIUM, 1);
    if (n < 0) {
        n = 0;
    }
    do {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0 && i < 12);
    for (j = 0; j < i; j++) {
        v[j] = tmp[i - 1 - j];
    }
    v[i] = '\0';
    ui_text(c, x + 74 - ui_text_w(v, UI_FONT_MEDIUM), y + ty, v, UI_WHITE, UI_EDGE,
            UI_FONT_MEDIUM, 1);
    if (bw < 4) {
        return;
    }
    ui_fill(c, x + 84, y + 5, bw, 5, UI_INK, 255);
    if (den > 0 && num > 0) {
        fw = (int)((long)(bw - 2) * (num > den ? den : num) / den);
        if (fw < 1) {
            fw = 1;
        }
        ui_fill(c, x + 85, y + 6, fw, 3, accent, 255);
    }
}

void ui_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected)
{
    ui_ellipse(c, x, y, size, size, UI_INK, UI_NONE, 255);
    ui_ellipse(c, x + 1, y + 1, size - 2, size - 2, selected ? UI_CYAN_EDGE : UI_EDGE2,
               UI_NONE, 255);
    ui_ellipse(c, x + 2, y + 2, size - 4, size - 4, UI_BODY, UI_NONE, 255);
    if (icon != NULL) {
        ui_image(c, x + (size - icon->w) / 2, y + (size - icon->h) / 2, icon, 1);
    }
}

void ui_gender(EidosUiCtx *c, int x, int y, int gender, int font)
{
    if (gender == 0) {
        ui_text(c, x, y, "\xE2\x99\x82", 0x63B4FFu, SHADOW_DARK, font, 1);
    } else if (gender == 1) {
        ui_text(c, x, y, "\xE2\x99\x80", 0xFF6363u, SHADOW_DARK, font, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Rows with content                                                   */
/* ------------------------------------------------------------------ */

void ui_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label,
                 const UiImage *icon, int selected, int hover)
{
    const int h = 26;
    int sel = selected || hover;
    int ty = ui_text_y(h, UI_FONT_LARGE);

    if (sel) {
        panel_split(c, x, y, w, h, UI_CUT_TL | UI_CUT_BL, 6, UI_CYAN,
                    selected ? UI_CYAN_EDGE : UI_CYAN2, 255, UI_CYAN2, w / 2);
    } else {
        panel_split(c, x, y, w, h, UI_CUT_TL | UI_CUT_BL, 6, UI_BODY, UI_EDGE, 235,
                    0x1E1E1Eu, w / 2);
    }
    ui_icon_well(c, x + 6, y + 2, 22, icon, sel);
    ui_text(c, x + 34, y + ty, label, UI_WHITE, sel ? (s32)SHADOW_DARK : (s32)UI_GREY,
            UI_FONT_LARGE, 1);
}

void ui_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name,
                  int gender, int level, int hp, int maxhp, const UiImage *icon,
                  int selected)
{
    const int h = 30;
    char num[16], lv[16], hpn[24];
    int tw, hw, nx;

    num[0] = (char)('1' + slot);
    num[1] = '\0';
    if (name == NULL) {
        ui_panel(c, x, y, w, h, CUT_DEF, 5, 0x06182Cu, UI_NAVY, 200);
        ui_text(c, x + 10, y + ui_text_y(h, UI_FONT_LARGE), num, UI_EDGE2, -1, UI_FONT_LARGE, 1);
        ui_text(c, x + 40, y + ui_text_y(h, UI_FONT_LARGE), "Empty", UI_EDGE2, -1,
                UI_FONT_LARGE, 1);
        return;
    }
    ui_panel(c, x, y, w, h, CUT_DEF, 5, selected ? UI_NAVY2 : UI_NAVY,
             selected ? UI_SKY : 0x084A73u, 235);
    ui_fill(c, x + 2, y + 2, 13, h - 4, 0x04182Cu, 255);                  /* index strip */
    ui_text(c, x + 5, y + ui_text_y(h, UI_FONT_LARGE), num, UI_LIGHT, UI_EDGE,
            UI_FONT_LARGE, 1);
    if (icon != NULL) {
        ui_image(c, x + 16 + (22 - icon->w) / 2, y + (h - icon->h) / 2, icon, 1);
    }
    nx = ui_text(c, x + 40, y + 1, name, UI_WHITE, SHADOW_DARK, UI_FONT_LARGE, 1);
    ui_gender(c, nx + 3, y + 1, gender, UI_FONT_LARGE);
    {
        int n = level, i = 0, j;
        char t[12];

        lv[0] = 'L';
        lv[1] = 'v';
        lv[2] = '.';
        do {
            t[i++] = (char)('0' + n % 10);
            n /= 10;
        } while (n > 0 && i < 8);
        for (j = 0; j < i; j++) {
            lv[3 + j] = t[i - 1 - j];
        }
        lv[3 + i] = '\0';
    }
    tw = ui_text_w(lv, UI_FONT_SMALL);
    ui_text(c, x + w - 9 - tw, y + 3, lv, UI_LIGHT, SHADOW_DARK, UI_FONT_SMALL, 1);
    {
        int a = hp, b = maxhp, i = 0, j;
        char t[12];

        do {
            t[i++] = (char)('0' + a % 10);
            a /= 10;
        } while (a > 0 && i < 8);
        for (j = 0; j < i; j++) {
            hpn[j] = t[i - 1 - j];
        }
        hpn[i++] = '/';
        j = i;
        i = 0;
        do {
            t[i++] = (char)('0' + b % 10);
            b /= 10;
        } while (b > 0 && i < 8);
        for (; i > 0; i--) {
            hpn[j++] = t[i - 1];
        }
        hpn[j] = '\0';
    }
    hw = ui_text_w(hpn, UI_FONT_SMALL);
    ui_bar(c, x + 40, y + 19, w - 52 - hw, 4, hp, maxhp);
    ui_text(c, x + w - 9 - hw, y + 16, hpn, UI_LIGHT, SHADOW_DARK, UI_FONT_SMALL, 1);
}

void ui_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender,
              int level, int hp, int maxhp, int exp_num, int exp_den)
{
    int player = exp_den > 0;
    int h = player ? 32 : 24, nx, tw, i, j;
    char lv[16], t[12];
    int n = level;

    ui_panel(c, x, y, w, h, UI_CUT_TR | UI_CUT_BL, 6, UI_BODY, UI_EDGE, 225);
    nx = ui_text(c, x + 8, y + 1, name, UI_WHITE, SHADOW_DARK, UI_FONT_LARGE, 1);
    ui_gender(c, nx + 3, y + 1, gender, UI_FONT_LARGE);
    lv[0] = 'L';
    lv[1] = 'v';
    lv[2] = '.';
    i = 0;
    do {
        t[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0 && i < 8);
    for (j = 0; j < i; j++) {
        lv[3 + j] = t[i - 1 - j];
    }
    lv[3 + i] = '\0';
    tw = ui_text_w(lv, UI_FONT_SMALL);
    ui_text(c, x + w - 10 - tw, y + 2, lv, UI_LIGHT, SHADOW_DARK, UI_FONT_SMALL, 1);
    ui_text(c, x + 8, y + 14, "HP", UI_LIME, SHADOW_DARK, UI_FONT_SMALL, 1);
    if (!player) {
        ui_bar(c, x + 24, y + 16, w - 36, 4, hp, maxhp);
    } else {
        char hpn[24];
        int a = hp, b = maxhp, k;

        i = 0;
        do {
            t[i++] = (char)('0' + a % 10);
            a /= 10;
        } while (a > 0 && i < 8);
        for (j = 0; j < i; j++) {
            hpn[j] = t[i - 1 - j];
        }
        hpn[i] = '/';
        k = i + 1;
        i = 0;
        do {
            t[i++] = (char)('0' + b % 10);
            b /= 10;
        } while (b > 0 && i < 8);
        for (; i > 0; i--) {
            hpn[k++] = t[i - 1];
        }
        hpn[k] = '\0';
        /* the numbers after the bar, on its row; the EXP line along the
         * bottom, inside the cut corners */
        {
            int nw = ui_text_w(hpn, UI_FONT_SMALL), bw = w - 24 - nw - 8 - 12;
            int ex = x + 10, ew0 = w - 16, ew;

            ui_bar(c, x + 24, y + 16, bw, 4, hp, maxhp);
            ui_text(c, x + 24 + bw + 5, y + 14, hpn, UI_WHITE, SHADOW_DARK, UI_FONT_SMALL, 1);
            ui_fill(c, ex, y + h - 6, ew0, 2, UI_INK, 255);
            if (exp_num > 0) {
                ew = (int)((long)ew0 * (exp_num > exp_den ? exp_den : exp_num) / exp_den);
                ui_fill(c, ex, y + h - 6, ew, 1, UI_SKY, 255);
            }
        }
    }
}

void ui_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                   u32 color, int selected)
{
    u32 dark = ((color >> 16) / 3) << 16 | (((color >> 8) & 255) / 3) << 8 | ((color & 255) / 3);
    int tw = ui_text_w(label, UI_FONT_LARGE);

    ui_panel(c, x, y, w, h, CUT_DEF, 6, UI_BODY, selected ? UI_CURSOR : UI_EDGE, 235);
    {
        /* the colour strip, following the cut top-left corner: a row's
         * strip starts where the panel's inside starts on that row */
        int r;

        for (r = 3; r < h - 3; r++) {
            int left = 3 + (r < 8 ? 8 - r : 0);

            if (left < 8) {
                ui_fill(c, x + left, y + r, 8 - left, 1, color, 255);
            }
        }
    }
    ui_text(c, x + (w - tw) / 2 + 3, y + ui_text_y(h, UI_FONT_LARGE), label, color,
            (s32)dark, UI_FONT_LARGE, 1);
}

void ui_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name,
                 int pp, int maxpp, int selected)
{
    const int h = 22;
    char s[24];
    int i = 0, j, n;
    char t[12];

    if (name == NULL) {
        ui_panel(c, x, y, w, h, CUT_DEF, 6, UI_BODY2, UI_EDGE, 160);
        ui_text(c, x + 52, y + ui_text_y(h, UI_FONT_LARGE), "-", UI_EDGE2, -1, UI_FONT_LARGE, 1);
        return;
    }
    ui_panel(c, x, y, w, h, CUT_DEF, 6, UI_BODY, selected ? UI_CURSOR : UI_EDGE, 235);
    ui_type_tag(c, x + 5, y + (h - 13) / 2, 42, type);
    ui_text(c, x + 52, y + ui_text_y(h, UI_FONT_LARGE), name, UI_WHITE, UI_GREY,
            UI_FONT_LARGE, 1);
    s[0] = 'P';
    s[1] = 'P';
    s[2] = ' ';
    j = 3;
    n = pp;
    do {
        t[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0 && i < 8);
    for (; i > 0; i--) {
        s[j++] = t[i - 1];
    }
    s[j++] = '/';
    n = maxpp;
    i = 0;
    do {
        t[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0 && i < 8);
    for (; i > 0; i--) {
        s[j++] = t[i - 1];
    }
    s[j] = '\0';
    ui_text(c, x + w - 10 - ui_text_w(s, UI_FONT_SMALL), y + ui_text_y(h, UI_FONT_SMALL), s,
            UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
}

int ui_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon,
                const char *name, int count, int selected)
{
    int h = 20, tx = 6;
    int ty;

    if (icon != NULL && icon->h + 6 > h) {
        h = icon->h + 6;
    }
    ty = ui_text_y(h, UI_FONT_LARGE);
    ui_panel(c, x, y, w, h, CUT_DEF, 4, selected ? UI_PINK2 : 0x3C0A1Au,
             selected ? UI_WHITE : UI_RED, 255);
    if (icon != NULL) {
        ui_image(c, x + 4, y + (h - icon->h) / 2, icon, 1);
        tx = 4 + icon->w + 3;
    }
    ui_text(c, x + tx, y + ty, name, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
    if (count > 0) {
        char s[16], t[12];
        int i = 0, j = 0, n = count;

        s[j++] = '\xC3';
        s[j++] = '\x97';
        do {
            t[i++] = (char)('0' + n % 10);
            n /= 10;
        } while (n > 0 && i < 8);
        for (; i > 0; i--) {
            s[j++] = t[i - 1];
        }
        s[j] = '\0';
        ui_text(c, x + w - 8 - ui_text_w(s, UI_FONT_LARGE), y + ty, s, UI_WHITE, UI_RED2,
                UI_FONT_LARGE, 1);
    }
    return h;
}

void ui_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name,
                int state, int selected)
{
    char s[8];
    int ty = ui_text_y(18, UI_FONT_LARGE);

    ui_panel(c, x, y, w, 18, CUT_DEF, 4, selected ? 0x285014u : UI_BODY2,
             selected ? UI_LIME : UI_EDGE, 255);
    if (state == 2) {
        u8 st = 0;

        ui_party_balls(c, x + 7, y + 4, &st, 1);
    } else if (state == 1) {
        ui_ellipse(c, x + 6, y + 4, 10, 10, UI_NONE, UI_LIGHT, 255);
    }
    s[0] = (char)('0' + (number / 100) % 10);
    s[1] = (char)('0' + (number / 10) % 10);
    s[2] = (char)('0' + number % 10);
    s[3] = '\0';
    ui_text(c, x + 24, y + ty, s, selected ? UI_LIME : UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
    if (name != NULL && state != 0) {
        ui_text(c, x + 50, y + ty, name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    } else {
        ui_text(c, x + 50, y + ty, "?????", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
    }
}

void ui_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value)
{
    int ty = ui_text_y(15, UI_FONT_LARGE);

    ui_panel(c, x, y, w, 15, CUT_DEF, 3, UI_BODY2, UI_EDGE, 255);
    ui_text(c, x + 6, y + ty, label, UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
    ui_text(c, x + w - 6 - ui_text_w(value, UI_FONT_LARGE), y + ty, value, UI_WHITE,
            UI_EDGE, UI_FONT_LARGE, 1);
}
