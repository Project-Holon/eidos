/*
 * tests/test_ui_kit.c -- the interface kit (engine/ui/kit.c, assets.c,
 * assets_icons.c).
 *
 * Two parts.
 *
 * Synthetic checks, no game data: a font is built in memory (an NFTR with
 * one glyph per ASCII character, every glyph a block of letter pixels, a
 * different advance for space and 'i') and the kit is checked against it:
 * text width, UTF-8 handling, the glyph values (1 letter, 2 shadow), the
 * centring rule, word wrap, panel shapes and their cut corners, blending,
 * the HP bar thresholds, the scroll thumb, clip and origin, the canvas
 * (re)sizing, and that every piece draws without leaving the canvas.
 *
 * Data checks, when the data root holds the game (skipped otherwise): the
 * three fonts load and have the accented letters, item icons and Pokemon
 * icons (#495 Snivy to #503 Samurott, both frames) decode to pictures with
 * content, and a contact sheet of every piece, the fonts and the icons is
 * written as a PNG for a person to look at.  The sheet's path is the
 * UI_KIT_SHEET environment variable, else ui_kit_sheet.png in the working
 * directory.
 *
 * The kit calls two hooks of the interface layer (hit regions and button
 * presses); the test supplies its own, which report "mouse over nothing".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "kit.h"
#include "port.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The interface layer's hooks                                         */
/* ------------------------------------------------------------------ */

static int g_hover_all;     /* the fake mouse is over every region        */
static u32 g_pressed;

int eidos_ui_hit(EidosUiRect r)
{
    (void)r;
    return g_hover_all ? EIDOS_UI_HOVER : 0;
}

void eidos_ui_press(u32 keys, u32 frames)
{
    (void)frames;
    g_pressed |= keys;
}

/* ------------------------------------------------------------------ */
/* A font built in memory                                              */
/* ------------------------------------------------------------------ */

#define SF_CW 4                 /* cell width                           */
#define SF_CH 4                 /* cell height                          */
#define SF_REC 7                /* 3 metric bytes + 16 pixels * 2 bits  */
#define SF_GLYPHS 95            /* 0x20 .. 0x7E                         */

static void w16le(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void w32le(u8 *p, u32 v) { w16le(p, v); w16le(p + 2, v >> 16); }

/*
 * One glyph per ASCII character.  A glyph's first row is letter pixels
 * (value 1) and its second row shadow pixels (value 2); the rest is empty,
 * except 'B', whose whole cell is letter.  The advance is 5, 3 for space
 * and 'i'; the bearing of 'j' is -1.
 */
static u32 build_font(u8 *out)
{
    u32 pos = 0, cglp_body, cwdh_body, cmap_body, i;
    u32 glyph_bytes = SF_GLYPHS * SF_REC;

    memset(out, 0, 4096);
    memcpy(out, "RTFN", 4);
    w16le(out + 4, 0xFFFE);
    w16le(out + 6, 0x0101);
    w16le(out + 12, 0x10);      /* header size */
    w16le(out + 14, 4);         /* chunks */
    pos = 0x10;
    /* FINF: chunk header 8 + body 20 */
    memcpy(out + pos, "FNIF", 4);
    w32le(out + pos + 4, 28);
    out[pos + 8 + 1] = SF_CH;
    cglp_body = pos + 28 + 8;
    cwdh_body = cglp_body + 8 + glyph_bytes + 8;
    cmap_body = cwdh_body + 8 + 8;
    w32le(out + pos + 8 + 8, cglp_body);
    w32le(out + pos + 8 + 12, cwdh_body);
    w32le(out + pos + 8 + 16, cmap_body);
    pos += 28;
    /* CGLP */
    memcpy(out + pos, "PLGC", 4);
    w32le(out + pos + 4, 8 + 8 + glyph_bytes);
    out[pos + 8] = SF_CW;
    out[pos + 9] = SF_CH;
    w16le(out + pos + 10, SF_REC);
    w16le(out + pos + 14, 2);   /* bits per pixel */
    for (i = 0; i < SF_GLYPHS; i++) {
        u8 *g = out + pos + 16 + i * SF_REC;
        u32 ch = 0x20 + i;

        g[0] = ch == 'j' ? 0xFF : 0;                    /* bearing */
        g[1] = SF_CW;
        g[2] = (ch == ' ' || ch == 'i') ? 3 : 5;        /* advance */
        if (ch == 'B') {
            memset(g + 3, 0x55, 4);                     /* all value 1 */
        } else if (ch != ' ') {
            g[3] = 0x55;                                /* row 0: value 1 */
            g[4] = 0xAA;                                /* row 1: value 2 */
        }
    }
    pos += 8 + 8 + glyph_bytes;
    /* CWDH (not read) */
    memcpy(out + pos, "HDWC", 4);
    w32le(out + pos + 4, 16);
    pos += 16;
    /* CMAP: method 0, 0x20..0x7E from glyph 0 */
    memcpy(out + pos, "PAMC", 4);
    w32le(out + pos + 4, 8 + 12 + 4);
    w16le(out + pos + 8, 0x20);
    w16le(out + pos + 10, 0x7E);
    w16le(out + pos + 12, 0);
    w32le(out + pos + 16, 0);
    w16le(out + pos + 20, 0);
    pos += 8 + 12 + 4;
    w32le(out + 8, pos);
    return pos;
}

static void install_synthetic_font(void)
{
    static u8 buf[4096];
    u32 n = build_font(buf);
    int f;

    for (f = 0; f < 3; f++) {
        if (ui_font_load_nftr(f, buf, n) != 0) {
            printf("  synthetic font %d did not load\n", f);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const u8 *px(const EidosUiCanvas *cv, int x, int y)
{
    return cv->rgba + ((size_t)y * (size_t)cv->w + (size_t)x) * 4;
}

static u32 rgb_at(const EidosUiCanvas *cv, int x, int y)
{
    const u8 *p = px(cv, x, y);

    return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
}

static int alpha_at(const EidosUiCanvas *cv, int x, int y)
{
    return px(cv, x, y)[3];
}

typedef struct WrapOut {
    char lines[8][64];
    int n;
} WrapOut;

static void wrap_cb(void *user, const char *s, int len)
{
    WrapOut *w = (WrapOut *)user;

    if (w->n < 8) {
        memcpy(w->lines[w->n], s, (size_t)len);
        w->lines[w->n][len] = '\0';
    }
    w->n++;
}

/* ------------------------------------------------------------------ */
/* Synthetic checks                                                    */
/* ------------------------------------------------------------------ */

static int test_canvas(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    EidosUiRect saved;
    u32 gen;

    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 64, 32, 3);
    BW_CHECK_EQ("canvas w", cv.w, 64);
    BW_CHECK_EQ("canvas h", cv.h, 32);
    BW_CHECK_EQ("canvas k", cv.k, 3);
    BW_CHECK_EQ("clip is all", c.clip.w * 1000 + c.clip.h, 64 * 1000 + 32);
    BW_CHECK_EQ("origin zero", c.ox + c.oy, 0);
    gen = cv.generation;
    ui_fill(&c, 2, 2, 4, 4, 0xFF0000, 255);
    BW_CHECK_EQ("fill red", rgb_at(&cv, 3, 3), 0xFF0000);
    BW_CHECK_EQ("fill alpha", alpha_at(&cv, 3, 3), 255);
    BW_CHECK_EQ("outside untouched", alpha_at(&cv, 7, 7), 0);

    /* a new frame clears and bumps the generation */
    ui_begin(&c, &cv, 64, 32, 3);
    BW_CHECK_EQ("cleared", alpha_at(&cv, 3, 3), 0);
    BW_REQUIRE("generation bumped", cv.generation == gen + 1);
    /* a resize reallocates */
    ui_begin(&c, &cv, 100, 50, 2);
    BW_CHECK_EQ("resized w", cv.w, 100);
    ui_fill(&c, 99, 49, 1, 1, 0x00FF00, 255);
    BW_CHECK_EQ("last pixel", rgb_at(&cv, 99, 49), 0x00FF00);

    /* origin and clip */
    ui_begin(&c, &cv, 40, 40, 2);
    ui_push(&c, 10, 10, &saved);
    ui_clip(&c, 0, 0, 5, 5);
    ui_fill(&c, -3, -3, 30, 30, 0x0000FF, 255);
    BW_CHECK_EQ("inside the clip", alpha_at(&cv, 12, 12), 255);
    BW_CHECK_EQ("past the clip", alpha_at(&cv, 16, 16), 0);
    BW_CHECK_EQ("before the clip", alpha_at(&cv, 8, 8), 0);
    ui_pop(&c, 10, 10, &saved);
    ui_fill(&c, 30, 30, 2, 2, 0x0000FF, 255);
    BW_CHECK_EQ("clip restored", alpha_at(&cv, 31, 31), 255);
    /* drawing off the canvas does nothing */
    ui_fill(&c, -50, -50, 200, 3, 0x123456, 255);
    ui_fill(&c, 38, 38, 20, 20, 0x123456, 255);
    BW_CHECK_EQ("edge pixel", rgb_at(&cv, 39, 39), 0x123456);

    free(cv.rgba);
    return 0;
}

static int test_blend(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */

    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 8, 8, 1);
    ui_fill(&c, 0, 0, 8, 8, 0x000000, 255);
    ui_fill(&c, 0, 0, 4, 4, 0xFFFFFF, 128);
    BW_CHECK_EQ("half white on black r", px(&cv, 1, 1)[0], 128);
    BW_CHECK_EQ("alpha stays opaque", alpha_at(&cv, 1, 1), 255);
    /* translucent onto empty keeps its own colour and alpha */
    ui_begin(&c, &cv, 8, 8, 1);
    ui_fill(&c, 4, 4, 2, 2, 0x204060, 100);
    BW_CHECK_EQ("on empty keeps colour", rgb_at(&cv, 4, 4), 0x204060);
    BW_CHECK_EQ("on empty keeps alpha", alpha_at(&cv, 4, 4), 100);
    /* translucent on translucent accumulates alpha */
    ui_fill(&c, 4, 4, 2, 2, 0x204060, 100);
    BW_REQUIRE("alpha grows", alpha_at(&cv, 4, 4) > 100 && alpha_at(&cv, 4, 4) < 255);
    free(cv.rgba);
    return 0;
}

static int test_text(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    WrapOut w;
    int n, x;

    install_synthetic_font();

    /* width: advances sum */
    BW_CHECK_EQ("width AB", ui_text_w("AB", UI_FONT_LARGE), 10);
    BW_CHECK_EQ("width i", ui_text_w("ii", UI_FONT_SMALL), 6);
    BW_CHECK_EQ("width A A", ui_text_w("A A", UI_FONT_MEDIUM), 13);
    BW_CHECK_EQ("width empty", ui_text_w("", UI_FONT_LARGE), 0);
    BW_CHECK_EQ("width NULL", ui_text_w(NULL, UI_FONT_LARGE), 0);
    /* a character the font lacks is '?', one glyph whatever its UTF-8 length */
    BW_CHECK_EQ("e acute is one glyph", ui_text_w("\xC3\xA9", UI_FONT_LARGE), 5);
    BW_CHECK_EQ("male sign is one glyph", ui_text_w("\xE2\x99\x82", UI_FONT_LARGE), 5);
    BW_CHECK_EQ("bad byte is one glyph", ui_text_w("\xFF" "A", UI_FONT_LARGE), 10);
    BW_CHECK_EQ("cut sequence", ui_text_w("\xE2\x99", UI_FONT_LARGE), 5);

    /* centring: the y that centres capitals (first inked row, cap height) */
    BW_CHECK_EQ("ty large 30", ui_text_y(30, UI_FONT_LARGE), 7);     /* (30-11)/2 - 2 */
    BW_CHECK_EQ("ty large 19", ui_text_y(19, UI_FONT_LARGE), 2);
    BW_CHECK_EQ("ty large 20", ui_text_y(20, UI_FONT_LARGE), 2);     /* 4 - 2 */
    BW_CHECK_EQ("ty small 13", ui_text_y(13, UI_FONT_SMALL), 2);     /* (13-8)/2 - 0 */
    BW_CHECK_EQ("ty small 14", ui_text_y(14, UI_FONT_SMALL), 3);
    BW_CHECK_EQ("ty medium 15", ui_text_y(15, UI_FONT_MEDIUM), 0);   /* (15-8)/2 - 3 */
    BW_CHECK_EQ("ty floors negative", ui_text_y(4, UI_FONT_LARGE), -6);
    /* a taller box moves the text by half the difference */
    BW_CHECK_EQ("ty step", ui_text_y(34, UI_FONT_LARGE) - ui_text_y(30, UI_FONT_LARGE), 2);

    /* drawing: value 1 is the letter, value 2 the shadow, a negative shadow
     * draws none, the return is the x after the text */
    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 40, 12, 1);
    x = ui_text(&c, 2, 1, "AA", 0xFF0000, 0x00FF00, UI_FONT_LARGE, 1);
    BW_CHECK_EQ("text returns x after", x, 12);
    BW_CHECK_EQ("letter row", rgb_at(&cv, 2, 1), 0xFF0000);
    BW_CHECK_EQ("letter row end", rgb_at(&cv, 5, 1), 0xFF0000);
    BW_CHECK_EQ("shadow row", rgb_at(&cv, 3, 2), 0x00FF00);
    BW_CHECK_EQ("below is empty", alpha_at(&cv, 3, 3), 0);
    BW_CHECK_EQ("second glyph", rgb_at(&cv, 7, 1), 0xFF0000);
    ui_begin(&c, &cv, 40, 12, 1);
    ui_text(&c, 2, 1, "A", 0xFF0000, -1, UI_FONT_LARGE, 1);
    BW_CHECK_EQ("no shadow letter", rgb_at(&cv, 2, 1), 0xFF0000);
    BW_CHECK_EQ("no shadow row empty", alpha_at(&cv, 3, 2), 0);
    /* a negative bearing draws left of x */
    ui_begin(&c, &cv, 40, 12, 1);
    ui_text(&c, 4, 1, "j", 0x0000FF, -1, UI_FONT_LARGE, 1);
    BW_CHECK_EQ("bearing -1", alpha_at(&cv, 3, 1), 255);
    BW_CHECK_EQ("bearing -1 start", alpha_at(&cv, 2, 1), 0);
    /* the whole cell of 'B' is letter */
    ui_begin(&c, &cv, 40, 12, 1);
    ui_text(&c, 0, 0, "B", 0xFFFFFF, 0x808080, UI_FONT_LARGE, 1);
    BW_CHECK_EQ("B corner", alpha_at(&cv, 3, 3), 255);
    BW_CHECK_EQ("B shadow row is letter", rgb_at(&cv, 1, 1), 0xFFFFFF);
    /* a size multiple k scales positions and advance */
    ui_begin(&c, &cv, 40, 12, 1);
    x = ui_text(&c, 0, 0, "A", 0xFFFFFF, -1, UI_FONT_LARGE, 2);
    BW_CHECK_EQ("scaled advance", x, 10);
    BW_CHECK_EQ("scaled letter", alpha_at(&cv, 7, 1), 255);
    BW_CHECK_EQ("scaled gap", alpha_at(&cv, 8, 0), 0);
    BW_CHECK_EQ("scaled below", alpha_at(&cv, 3, 2), 0);

    /* word wrap (advance 5 per letter, 3 per space) */
    memset(&w, 0, sizeof w);
    n = ui_wrap("AAA BBB CCC", 33, UI_FONT_LARGE, wrap_cb, &w);
    BW_CHECK_EQ("wrap fits two", n, 2);
    BW_CHECK_EQ("wrap line 0", strcmp(w.lines[0], "AAA BBB"), 0);
    BW_CHECK_EQ("wrap line 1", strcmp(w.lines[1], "CCC"), 0);
    memset(&w, 0, sizeof w);
    n = ui_wrap("AAA BBB CCC", 32, UI_FONT_LARGE, wrap_cb, &w);
    BW_CHECK_EQ("wrap one narrower", n, 3);
    BW_CHECK_EQ("wrap count only", ui_wrap("AAA BBB CCC", 32, UI_FONT_LARGE, NULL, NULL), 3);
    memset(&w, 0, sizeof w);
    n = ui_wrap("AAAAAAAAAAAAAAAAAAAA B", 20, UI_FONT_LARGE, wrap_cb, &w);
    BW_CHECK_EQ("long word stays whole", n, 2);
    BW_CHECK_EQ("long word line", (int)strlen(w.lines[0]), 20);
    memset(&w, 0, sizeof w);
    n = ui_wrap("A\nB", 500, UI_FONT_LARGE, wrap_cb, &w);
    BW_CHECK_EQ("newline breaks", n, 2);
    BW_CHECK_EQ("wrap empty", ui_wrap("", 100, UI_FONT_LARGE, NULL, NULL), 0);
    BW_CHECK_EQ("wrap blanks", ui_wrap("   ", 100, UI_FONT_LARGE, NULL, NULL), 0);
    memset(&w, 0, sizeof w);
    n = ui_wrap("  AA   BB  ", 500, UI_FONT_LARGE, wrap_cb, &w);
    BW_CHECK_EQ("wrap keeps inner spacing", strcmp(w.lines[0], "AA   BB"), 0);

    free(cv.rgba);
    return 0;
}

static int test_panels(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    int x, y, n;

    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 40, 40, 1);
    /* 20 x 14 panel, TL and BR cut by 5, fill red, edge green */
    ui_panel(&c, 4, 4, 20, 14, UI_CUT_TL | UI_CUT_BR, 5, 0xFF0000, 0x00FF00, 255);
    BW_CHECK_EQ("cut corner is empty", alpha_at(&cv, 4, 4), 0);
    BW_CHECK_EQ("cut corner row 3", alpha_at(&cv, 4 + 3, 4 + 1), 0);
    BW_CHECK_EQ("cut ends outline", rgb_at(&cv, 4 + 5, 4), UI_INK);
    BW_CHECK_EQ("cut ends outline (left)", rgb_at(&cv, 4, 4 + 5), UI_INK);
    BW_CHECK_EQ("uncut TR corner", rgb_at(&cv, 4 + 19, 4), UI_INK);
    BW_CHECK_EQ("uncut BL corner", rgb_at(&cv, 4, 4 + 13), UI_INK);
    BW_CHECK_EQ("BR cut", alpha_at(&cv, 4 + 19, 4 + 13), 0);
    BW_CHECK_EQ("edge line", rgb_at(&cv, 4 + 1, 4 + 8), 0x00FF00);
    BW_CHECK_EQ("edge line top", rgb_at(&cv, 4 + 10, 4 + 1), 0x00FF00);
    BW_CHECK_EQ("fill", rgb_at(&cv, 4 + 10, 4 + 7), 0xFF0000);
    BW_CHECK_EQ("outline left", rgb_at(&cv, 4, 4 + 8), UI_INK);
    BW_CHECK_EQ("outline bottom", rgb_at(&cv, 4 + 10, 4 + 13), UI_INK);
    BW_CHECK_EQ("outside is empty", alpha_at(&cv, 3, 8), 0);
    BW_CHECK_EQ("outside right is empty", alpha_at(&cv, 24, 8), 0);
    /* the panel is symmetric under a transposed cut: cut TR and BL */
    ui_begin(&c, &cv, 40, 40, 1);
    ui_panel(&c, 0, 0, 20, 14, UI_CUT_TR | UI_CUT_BL, 5, 0xFF0000, 0x00FF00, 255);
    BW_CHECK_EQ("TR cut", alpha_at(&cv, 19, 0), 0);
    BW_CHECK_EQ("BL cut", alpha_at(&cv, 0, 13), 0);
    BW_CHECK_EQ("TL kept", rgb_at(&cv, 0, 0), UI_INK);
    BW_CHECK_EQ("BR kept", rgb_at(&cv, 19, 13), UI_INK);
    /* every cut corner removes the same number of pixels, 5 + 4 + 3 + 2 + 1 = 15 at
     * the outline layer; all four corners cut: area = 20*14 - 4*15 */
    ui_begin(&c, &cv, 40, 40, 1);
    ui_panel(&c, 0, 0, 20, 14, 15, 5, 0xFF0000, 0x00FF00, 255);
    n = 0;
    for (y = 0; y < 14; y++) {
        for (x = 0; x < 20; x++) {
            n += alpha_at(&cv, x, y) != 0;
        }
    }
    BW_CHECK_EQ("area with four cuts", n, 20 * 14 - 4 * 15);
    /* translucent: one colour per pixel, blended once */
    ui_begin(&c, &cv, 40, 40, 1);
    ui_panel(&c, 0, 0, 20, 14, UI_CUT_TL, 3, 0xFFFFFF, 0x808080, 128);
    BW_CHECK_EQ("translucent outline alpha", alpha_at(&cv, 10, 0), 128);
    BW_CHECK_EQ("translucent fill alpha", alpha_at(&cv, 10, 7), 128);
    BW_CHECK_EQ("translucent fill colour", rgb_at(&cv, 10, 7), 0xFFFFFF);
    BW_CHECK_EQ("translucent edge colour", rgb_at(&cv, 10, 1), 0x808080);
    /* a 1 pixel high / tiny panel does not crash */
    ui_panel(&c, 0, 0, 1, 1, 15, 5, 0xFF0000, 0x00FF00, 255);
    ui_panel(&c, 0, 0, 0, 0, 15, 5, 0xFF0000, 0x00FF00, 255);

    free(cv.rgba);
    return 0;
}

static int test_pieces(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    UiImage icon;
    u8 icon_px[24 * 24 * 4];
    const char *pairs[] = { "A", "Select", "B", "Back" };
    u32 keys[2] = { 1, 2 };
    const char *choices[] = { "YES", "NO" };
    u8 balls[4] = { 0, 1, 2, 3 };
    int h, i;

    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 400, 300, 1);

    /* the HP bar: colour by fraction, 50% and 20% are the lower colour */
    ui_bar(&c, 0, 0, 52, 4, 51, 100);
    BW_CHECK_EQ("bar frame", rgb_at(&cv, 0, 0), UI_INK);
    BW_CHECK_EQ("bar empty part", rgb_at(&cv, 50, 2), 0x313131);
    BW_CHECK_EQ("bar green fill", rgb_at(&cv, 5, 2), UI_HP_G);
    ui_bar(&c, 0, 10, 52, 4, 50, 100);
    BW_CHECK_EQ("bar 50 is yellow", rgb_at(&cv, 5, 12), UI_HP_Y);
    ui_bar(&c, 0, 20, 52, 4, 21, 100);
    BW_CHECK_EQ("bar 21 is yellow", rgb_at(&cv, 5, 22), UI_HP_Y);
    ui_bar(&c, 0, 30, 52, 4, 20, 100);
    BW_CHECK_EQ("bar 20 is red", rgb_at(&cv, 5, 32), UI_HP_R);
    ui_bar(&c, 0, 40, 52, 4, 100, 100);
    BW_CHECK_EQ("full bar reaches the end", rgb_at(&cv, 50, 42), UI_HP_G);
    ui_bar(&c, 0, 50, 52, 4, 0, 100);
    BW_CHECK_EQ("empty bar", rgb_at(&cv, 5, 52), 0x313131);
    ui_bar(&c, 0, 60, 52, 4, 1, 0);          /* no divide by zero */
    ui_bar(&c, 0, 70, 52, 4, 500, 100);      /* over full is full */
    BW_CHECK_EQ("over full", rgb_at(&cv, 50, 72), UI_HP_G);

    /* the scroll thumb: size from shown/total, position from first */
    ui_begin(&c, &cv, 400, 300, 1);
    ui_scrollbar(&c, 0, 0, 52, 0, 5, 10, UI_CYAN);
    BW_CHECK_EQ("thumb at the top", rgb_at(&cv, 2, 1), UI_CYAN);
    BW_CHECK_EQ("track below the thumb", rgb_at(&cv, 2, 40), UI_BODY2);
    ui_begin(&c, &cv, 400, 300, 1);
    ui_scrollbar(&c, 0, 0, 52, 5, 5, 10, UI_CYAN);
    BW_CHECK_EQ("thumb at the bottom", rgb_at(&cv, 2, 50), UI_CYAN);
    BW_CHECK_EQ("track above the thumb", rgb_at(&cv, 2, 2), UI_BODY2);
    ui_begin(&c, &cv, 400, 300, 1);
    ui_scrollbar(&c, 0, 0, 52, 0, 10, 10, UI_CYAN);
    BW_CHECK_EQ("all shown fills the track", rgb_at(&cv, 2, 50), UI_CYAN);
    ui_scrollbar(&c, 0, 0, 52, 0, 1, 1000, UI_CYAN);   /* tiny thumb: at least 6 */

    /* rows: selected uses the accent outline and fill, hover only the fill */
    ui_begin(&c, &cv, 400, 300, 1);
    ui_row(&c, 0, 0, 60, 18, 1, 0, &UI_THEME_MENU);
    BW_CHECK_EQ("selected fill", rgb_at(&cv, 30, 9), UI_CYAN2);
    BW_CHECK_EQ("selected edge", rgb_at(&cv, 30, 1), UI_CYAN);
    ui_row(&c, 0, 20, 60, 18, 0, 1, &UI_THEME_MENU);
    BW_CHECK_EQ("hover fill", rgb_at(&cv, 30, 29), UI_CYAN2);
    BW_CHECK_EQ("hover keeps the edge", rgb_at(&cv, 30, 21), UI_EDGE);
    ui_row(&c, 0, 40, 60, 18, 0, 0, &UI_THEME_MENU);
    BW_CHECK_EQ("plain fill", rgb_at(&cv, 30, 49), UI_BODY);

    /* key hints: height 17, a key press when its region is clicked */
    ui_begin(&c, &cv, 400, 300, 1);
    h = ui_hints(&c, 100, 400, pairs, 2, keys);
    BW_CHECK_EQ("hints height", h, 17);
    BW_CHECK_EQ("hints top line", rgb_at(&cv, 200, 100), UI_EDGE);
    BW_CHECK_EQ("hints background alpha", alpha_at(&cv, 200, 108), 235);
    g_hover_all = 1;
    ui_hints(&c, 100, 400, pairs, 2, keys);
    g_hover_all = 0;
    BW_CHECK_EQ("hint hover does not press", (int)g_pressed, 0);
    ui_hints(&c, 100, 400, NULL, 0, NULL);

    /* the rest draw inside their boxes */
    ui_begin(&c, &cv, 400, 300, 1);
    icon.w = 24;
    icon.h = 24;
    icon.rgba = icon_px;
    memset(icon_px, 255, sizeof icon_px);
    BW_CHECK_EQ("keycap grows with its label",
                ui_keycap(&c, 0, 0, "START") > ui_keycap(&c, 0, 0, "A"), 1);
    BW_CHECK_EQ("keycap minimum", ui_keycap(&c, 0, 0, "A"), 13);
    ui_header(&c, 0, 20, 300, "BAG", "ITEMS", "3/6", UI_PINK);
    BW_CHECK_EQ("header accent rule", rgb_at(&cv, 150, 20 + 19), UI_PINK);
    BW_CHECK_EQ("header ink rule", rgb_at(&cv, 150, 20 + 20), UI_INK);
    BW_CHECK_EQ("header last row empty", alpha_at(&cv, 150, 20 + 21), 0);
    BW_CHECK_EQ("header title bar", rgb_at(&cv, 150, 20 + 10), UI_BODY2);
    BW_CHECK_EQ("header chevron", rgb_at(&cv, 4, 20 + 4), UI_PINK);
    ui_type_tag(&c, 0, 50, 40, 10);
    BW_CHECK_EQ("type tag fill", rgb_at(&cv, 3, 52), ui_type_color(10));
    BW_CHECK_EQ("type names", strcmp(ui_type_name(16), "DARK"), 0);
    BW_CHECK_EQ("type 9 is FIRE", strcmp(ui_type_name(9), "FIRE"), 0);
    BW_CHECK_EQ("unknown type", strcmp(ui_type_name(UI_TYPE_UNKNOWN), "???"), 0);
    BW_CHECK_EQ("type out of range", strcmp(ui_type_name(99), "???"), 0);
    for (i = 0; i < 7; i++) {
        ui_status_badge(&c, i * 30, 70, i);
    }
    ui_status_badge(&c, 0, 70, 99);          /* ignored */
    BW_CHECK_EQ("badge fill", rgb_at(&cv, 5, 73), 0xA040A0);
    ui_message(&c, 0, 90, 200, "Hello");
    BW_CHECK_EQ("message height", alpha_at(&cv, 100, 90 + 29), 215);
    BW_CHECK_EQ("message below", alpha_at(&cv, 100, 90 + 30), 0);
    ui_toast(&c, 0, 130, "Got it", NULL);
    BW_CHECK_EQ("toast without icon is 22 high", alpha_at(&cv, 20, 130 + 21), 235);
    ui_toast(&c, 0, 160, "Got it", &icon);
    BW_CHECK_EQ("toast with a 24 icon is 30 high", alpha_at(&cv, 20, 160 + 29), 235);
    BW_CHECK_EQ("toast without icon ends there", alpha_at(&cv, 20, 130 + 22), 0);
    ui_cursor_corners(&c, 200, 100, 30, 30, UI_CURSOR);
    BW_CHECK_EQ("cursor corner", rgb_at(&cv, 200, 100), UI_CURSOR);
    BW_CHECK_EQ("cursor middle of an edge is empty", alpha_at(&cv, 215, 100), 0);
    ui_tab(&c, 0, 200, "General", 1, UI_CYAN, UI_CYAN2);
    ui_pocket_tab(&c, 80, 200, 60, "Items", 0);
    ui_toggle(&c, 150, 200, 1, UI_CYAN);
    ui_toggle(&c, 180, 200, 0, UI_CYAN);
    ui_slider(&c, 210, 200, 80, 3, 10, UI_CYAN);
    ui_slider(&c, 210, 214, 80, 0, 0, UI_CYAN);
    ui_stepper(&c, 0, 220, 60, "10", UI_CYAN);
    ui_field(&c, 70, 220, 100, "Saili", 3, UI_CYAN);
    ui_key(&c, 180, 220, 0, "A", 1, UI_CYAN);
    BW_CHECK_EQ("dialog is as tall as its text",
                ui_dialog(&c, 0, 240, 150, "Save the game?", choices, 2, 0, UI_CYAN),
                14 + 15 * 1 + 26);
    ui_progress(&c, 200, 240, 80, 1, 2, UI_CYAN);
    ui_spinner(&c, 290, 240, 3, UI_CYAN);
    ui_page_dots(&c, 310, 240, 4, 1, UI_CYAN);
    ui_target_chevron(&c, 340, 240, UI_CURSOR);
    ui_party_balls(&c, 300, 260, balls, 4);
    BW_CHECK_EQ("name tag width", ui_name_tag(&c, 200, 260, "Dom", UI_SKY), ui_text_w("Dom", UI_FONT_SMALL) + 12);
    ui_category_icon(&c, 340, 260, 0);
    ui_category_icon(&c, 360, 260, 1);
    ui_category_icon(&c, 380, 260, 2);
    ui_stat_row(&c, 0, 270, 140, "HP", 123, 3, 4, UI_CYAN);
    ui_icon_well(&c, 150, 270, 22, &icon, 1);
    ui_gender(&c, 180, 270, 0, UI_FONT_LARGE);
    ui_menu_row(&c, 200, 0, 140, "BAG", NULL, 1, 0);
    ui_party_row(&c, 200, 30, 150, 0, "Saili", 0, 7, 26, 26, NULL, 1);
    ui_party_row(&c, 200, 62, 150, 3, NULL, 0, 0, 0, 0, NULL, 0);
    ui_plate(&c, 200, 100, 140, "Saili", 0, 7, 8, 26, 2, 5);
    ui_plate(&c, 200, 140, 140, "Purrloin", 1, 7, 9, 10, 0, 0);
    ui_cmd_button(&c, 200, 180, 110, 28, "FIGHT", UI_FIGHT, 1);
    ui_move_row(&c, 200, 210, 160, 0, "Tackle", 34, 35, 1);
    ui_move_row(&c, 200, 234, 160, 0, NULL, 0, 0, 0);
    BW_CHECK_EQ("item row grows for a 24 icon",
                ui_item_row(&c, 0, 0, 120, &icon, "Poke Ball", 3, 1), 30);
    BW_CHECK_EQ("item row without icon", ui_item_row(&c, 0, 40, 120, NULL, "Potion", 0, 0), 20);
    ui_dex_row(&c, 0, 70, 120, 1, "Snivy", 2, 1);
    ui_dex_row(&c, 0, 90, 120, 4, NULL, 0, 0);
    ui_info_row(&c, 0, 110, 120, "HT", "1'08\"");

    free(cv.rgba);
    return 0;
}

/* Text inside rows that hold an icon: centred in the row's own height. */
static int test_icon_rows(void)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    UiImage icon;
    u8 px_buf[16 * 16 * 4];
    int h;

    install_synthetic_font();
    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 200, 100, 1);
    icon.w = 16;
    icon.h = 16;
    icon.rgba = px_buf;
    memset(px_buf, 0, sizeof px_buf);
    h = ui_item_row(&c, 0, 0, 120, &icon, "A", 0, 0);
    BW_CHECK_EQ("16 icon row is at least icon + 6", h >= 22, 1);
    /* the synthetic 'A' draws its first row at the font's y; the row's
     * centring puts that at ui_text_y(h) + the cap offset inside the row */
    BW_CHECK_EQ("label row", alpha_at(&cv, 4 + 16 + 3, ui_text_y(h, UI_FONT_LARGE)) != 0, 1);
    free(cv.rgba);
    return 0;
}

/* ------------------------------------------------------------------ */
/* PNG output (stored deflate: no compression library needed)          */
/* ------------------------------------------------------------------ */

static u32 g_crc_table[256];

static void crc_init(void)
{
    u32 n, k;

    for (n = 0; n < 256; n++) {
        u32 v = n;

        for (k = 0; k < 8; k++) {
            v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
        }
        g_crc_table[n] = v;
    }
}

static u32 crc_update(u32 crc, const u8 *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        crc = g_crc_table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    }
    return crc;
}

static void be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void png_chunk(FILE *f, const char *type, const u8 *data, u32 len)
{
    u8 head[8], tail[4];
    u32 crc;

    be32(head, len);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    if (len > 0) {
        fwrite(data, 1, len, f);
    }
    crc = crc_update(0xFFFFFFFFu, (const u8 *)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    be32(tail, crc);
    fwrite(tail, 1, 4, f);
}

/* Write an RGB image (3 bytes per pixel) as a PNG. */
static int write_png(const char *path, const u8 *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    u8 ihdr[13], *raw, *z;
    size_t rawn = (size_t)h * ((size_t)w * 3 + 1), zn, pos = 0, off = 0;
    u32 a = 1, b = 0, y;
    size_t i;

    if (f == NULL) {
        return -1;
    }
    crc_init();
    raw = (u8 *)malloc(rawn);
    for (y = 0; y < (u32)h; y++) {
        raw[(size_t)y * ((size_t)w * 3 + 1)] = 0;
        memcpy(raw + (size_t)y * ((size_t)w * 3 + 1) + 1, rgb + (size_t)y * (size_t)w * 3,
               (size_t)w * 3);
    }
    for (i = 0; i < rawn; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    zn = 2 + rawn + (rawn / 65535 + 1) * 5 + 4;
    z = (u8 *)malloc(zn);
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (off < rawn) {
        size_t n = rawn - off > 65535 ? 65535 : rawn - off;

        z[pos++] = (off + n == rawn) ? 1 : 0;
        z[pos++] = (u8)n;
        z[pos++] = (u8)(n >> 8);
        z[pos++] = (u8)~n;
        z[pos++] = (u8)(~n >> 8);
        memcpy(z + pos, raw + off, n);
        pos += n;
        off += n;
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    be32(ihdr, (u32)w);
    be32(ihdr + 4, (u32)h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    fwrite(sig, 1, 8, f);
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (u32)pos);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

/* The canvas over a flat background, scaled by `scale`, as a PNG. */
static int write_canvas_png(const char *path, const EidosUiCanvas *cv, int scale, u32 bg)
{
    int w = cv->w * scale, h = cv->h * scale, x, y;
    u8 *rgb = (u8 *)malloc((size_t)w * (size_t)h * 3);
    int rc;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const u8 *p = px(cv, x / scale, y / scale);
            int a = p[3];
            u8 *d = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;

            d[0] = (u8)((p[0] * a + (int)((bg >> 16) & 255) * (255 - a)) / 255);
            d[1] = (u8)((p[1] * a + (int)((bg >> 8) & 255) * (255 - a)) / 255);
            d[2] = (u8)((p[2] * a + (int)(bg & 255) * (255 - a)) / 255);
        }
    }
    rc = write_png(path, rgb, w, h);
    free(rgb);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Data checks and the contact sheet                                   */
/* ------------------------------------------------------------------ */

static int image_pixels(const UiImage *im)
{
    int n = 0, i;

    if (im == NULL) {
        return 0;
    }
    for (i = 0; i < im->w * im->h; i++) {
        n += im->rgba[i * 4 + 3] != 0;
    }
    return n;
}

static int images_differ(const UiImage *a, const UiImage *b)
{
    return a != NULL && b != NULL && a->w == b->w && a->h == b->h &&
           memcmp(a->rgba, b->rgba, (size_t)a->w * (size_t)a->h * 4) != 0;
}

static void label(EidosUiCtx *c, int x, int y, const char *s)
{
    ui_text(c, x, y, s, UI_LIME, UI_INK, UI_FONT_SMALL, 1);
}

static void draw_sheet(EidosUiCtx *c)
{
    static const char *const names[6] = { "POK\xC3\x89MON", "POK\xC3\x89" "DEX", "BAG",
                                          "Sally", "SAVE", "OPTIONS" };
    static const struct { const char *name; int g, lv, hp, mhp, species; } party[3] = {
        { "Saili", 0, 7, 26, 26, 498 }, { "Dom", 0, 4, 18, 18, 504 },
        { "Buri-chu", 1, 2, 14, 14, 506 }
    };
    static const char *const types_shown[18] = { 0 };
    const char *const pairs[] = { "A", "Select", "B", "Back", "X", "Menu", "\xE2\x86\x90\xE2\x86\x92", "Page" };
    const char *const pockets[5] = { "Items", "Medicine", "TMs & HMs", "Berries", "Key Items" };
    u8 balls[4] = { 0, 1, 2, 3 };
    int x, y, i;
    int tx[6] = { 0, 1, 2, 3, 4, 5 };

    (void)types_shown;
    (void)tx;
    x = 8;
    y = 4;
    label(c, x, y, "HEADERS (accent per app)");
    y += 14;
    ui_header(c, x, y, 280, "PARTY", NULL, "3/6", UI_SKY);
    y += 24;
    ui_header(c, x, y, 280, "BAG", "ITEMS", NULL, UI_PINK);
    y += 24;
    ui_header(c, x, y, 280, "POK\xC3\x89" "DEX", "UNOVA", NULL, UI_LIME);
    y += 24;
    ui_header(c, x, y, 280, "OPTIONS", "GAME", NULL, UI_CYAN);
    y += 24;
    label(c, x, y, "START MENU ROWS: plain / selected");
    y += 14;
    for (i = 0; i < 6; i++) {
        ui_menu_row(c, x + (i % 2) * 146, y + (i / 2) * 28, 140, names[i], NULL, i == 2, 0);
    }
    y += 3 * 28 + 4;
    label(c, x, y, "PARTY ROWS: lead / plain / empty");
    y += 14;
    ui_party_row(c, x, y, 150, 0, party[0].name, party[0].g, party[0].lv, party[0].hp,
                 party[0].mhp, ui_mon_icon((u16)(party[0].species), 0, 0), 1);
    ui_party_row(c, x + 156, y, 150, 1, party[1].name, party[1].g, party[1].lv, party[1].hp,
                 party[1].mhp, ui_mon_icon((u16)(party[1].species), 0, 0), 0);
    y += 32;
    ui_party_row(c, x, y, 150, 2, party[2].name, party[2].g, party[2].lv, party[2].hp,
                 party[2].mhp, ui_mon_icon((u16)(party[2].species), 0, 1), 0);
    ui_party_row(c, x + 156, y, 150, 3, NULL, 0, 0, 0, 0, NULL, 0);
    y += 34;
    label(c, x, y, "BATTLE PLATES: foe / player");
    y += 14;
    ui_plate(c, x, y, 140, "Purrloin", 1, 7, 92, 100, 0, 0);
    ui_plate(c, x + 146, y, 140, "Saili", 0, 7, 8, 26, 3, 5);
    y += 40;
    label(c, x, y, "HP BARS: high / mid / low");
    y += 14;
    ui_bar(c, x, y, 90, 4, 90, 100);
    ui_bar(c, x + 96, y, 90, 4, 45, 100);
    ui_bar(c, x + 192, y, 90, 4, 12, 100);
    y += 10;
    label(c, x, y, "BATTLE COMMANDS: selected / plain");
    y += 14;
    ui_cmd_button(c, x, y, 110, 28, "FIGHT", UI_FIGHT, 1);
    ui_cmd_button(c, x + 116, y, 110, 20, "POK\xC3\x89MON", UI_MON, 0);
    ui_cmd_button(c, x + 116, y + 22, 110, 20, "BAG", UI_BAGY, 0);
    ui_cmd_button(c, x, y + 30, 110, 20, "RUN", UI_RUN, 0);
    y += 56;
    label(c, x, y, "MOVE ROWS: selected / plain / empty");
    y += 14;
    ui_move_row(c, x, y, 160, 0, "Tackle", 34, 35, 1);
    ui_move_row(c, x + 166, y, 160, 0, "Tail Whip", 30, 30, 0);
    ui_move_row(c, x, y + 24, 160, 10, "Ember", 25, 25, 0);
    ui_move_row(c, x + 166, y + 24, 160, 0, NULL, 0, 0, 0);
    y += 52;
    label(c, x, y, "MESSAGE");
    y += 14;
    ui_message(c, x, y, 240, "What will Saili do?");
    y += 36;
    label(c, x, y, "MORE: toggles, slider, stepper, field, keys");
    y += 14;
    ui_toggle(c, x, y, 1, UI_CYAN);
    ui_toggle(c, x + 30, y, 0, UI_CYAN);
    ui_slider(c, x + 62, y, 80, 6, 10, UI_CYAN);
    ui_stepper(c, x + 150, y, 60, "10", UI_CYAN);
    ui_progress(c, x + 220, y + 3, 60, 1, 2, UI_CYAN);
    ui_spinner(c, x + 286, y, 2, UI_CYAN);
    y += 20;
    ui_field(c, x, y, 120, "Saili", 3, UI_CYAN);
    ui_key(c, x + 128, y, 0, "A", 1, UI_CYAN);
    ui_key(c, x + 150, y, 0, "B", 0, UI_CYAN);
    ui_key(c, x + 172, y, 0, "C", 0, UI_CYAN);
    ui_page_dots(c, x + 200, y + 6, 4, 1, UI_CYAN);
    ui_target_chevron(c, x + 240, y + 4, UI_CURSOR);
    ui_party_balls(c, x + 260, y + 4, balls, 4);

    /* right column */
    x = 344;
    y = 4;
    label(c, x, y, "TYPE TAGS");
    y += 14;
    for (i = 0; i < 18; i++) {
        ui_type_tag(c, x + (i % 5) * 56, y + (i / 5) * 16, 52, i);
    }
    y += 4 * 16 + 6;
    label(c, x, y, "BAG: pocket tabs / item row");
    y += 14;
    for (i = 0; i < 5; i++) {
        ui_pocket_tab(c, x + (i % 3) * 94, y + (i / 3) * 20, 90, pockets[i], i == 0);
    }
    y += 44;
    y += ui_item_row(c, x, y, 180, ui_item_icon(4), "Pok\xC3\xA9 Ball", 3, 1) + 2;
    y += ui_item_row(c, x, y, 180, ui_item_icon(17), "Potion", 5, 0) + 4;
    label(c, x, y, "POK\xC3\x89" "DEX ROWS: selected / owned / seen / unknown");
    y += 14;
    ui_dex_row(c, x, y, 180, 1, "Snivy", 1, 1);
    y += 20;
    ui_dex_row(c, x, y, 180, 4, "Tepig", 2, 0);
    y += 20;
    ui_dex_row(c, x, y, 180, 2, NULL, 0, 0);
    y += 22;
    label(c, x, y, "INFO ROWS");
    y += 14;
    ui_info_row(c, x, y, 180, "HT", "1'08\"");
    ui_info_row(c, x, y + 17, 180, "WT", "25.6 lbs.");
    y += 36;
    label(c, x, y, "ICON BADGES: plain / selected, status badges");
    y += 14;
    ui_icon_well(c, x, y, 22, ui_item_icon(4), 0);
    ui_icon_well(c, x + 26, y, 22, ui_item_icon(17), 1);
    for (i = 0; i < 7; i++) {
        ui_status_badge(c, x + 54 + i * 18 - (i > 3 ? 0 : 0), y + (i % 2) * 12, i);
    }
    y += 28;
    label(c, x, y, "KEY HINTS");
    y += 14;
    {
        EidosUiRect saved;

        ui_push(c, x, 0, &saved);
        ui_hints(c, y, 250, pairs, 4, NULL);
        ui_pop(c, x, 0, &saved);
    }
    y += 22;
    label(c, x, y, "PANELS: modal frames per app");
    y += 14;
    ui_panel(c, x, y, 80, 50, UI_CUT_TL | UI_CUT_BR, 10, UI_THEME_BAG.fill, UI_THEME_BAG.edge, 255);
    ui_panel(c, x + 86, y, 80, 50, UI_CUT_TL | UI_CUT_BR, 10, UI_THEME_CARD.fill, UI_THEME_CARD.edge, 255);
    ui_panel(c, x + 172, y, 80, 50, UI_CUT_TL | UI_CUT_BR, 10, UI_BODY, UI_EDGE, 255);
    y += 58;
    label(c, x, y, "GAME FONT: large / medium / small");
    y += 14;
    ui_text(c, x, y, "Pok\xC3\xA9mon Black 0123456789 \xE2\x99\x82\xE2\x99\x80 \xC3\x97 \xE2\x80\xA6", UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
    ui_text(c, x, y + 18, "Pok\xC3\xA9mon Black 0123456789 \xE2\x99\x82\xE2\x99\x80 \xC3\x97 \xE2\x80\xA6", UI_WHITE, UI_GREY, UI_FONT_MEDIUM, 1);
    ui_text(c, x, y + 34, "Pok\xC3\xA9mon Black 0123456789 \xE2\x99\x82\xE2\x99\x80 \xC3\x97 \xE2\x80\xA6", UI_WHITE, UI_GREY, UI_FONT_SMALL, 1);
    y += 52;
    label(c, x, y, "ITEM ICONS (24x24)");
    y += 14;
    for (i = 1; i <= 12; i++) {
        ui_image(c, x + (i - 1) * 26, y, ui_item_icon((u16)(i == 12 ? 626 : i * 5)), 1);
    }
    y += 28;
    label(c, x, y, "POK\xC3\x89MON ICONS #495-#503, frames 0 and 1");
    y += 14;
    for (i = 0; i < 9; i++) {
        ui_image(c, x + (i % 5) * 70, y + (i / 5) * 34, ui_mon_icon((u16)(495 + i), 0, 0), 1);
        ui_image(c, x + (i % 5) * 70 + 33, y + (i / 5) * 34, ui_mon_icon((u16)(495 + i), 0, 1), 1);
    }
    y += 72;
    label(c, x, y, "FIGURES: trainer, 4 directions, standing + walk");
    y += 14;
    {
        const UiImage *t0 = ui_overworld(0, 0, 0);
        int f, step = t0 != NULL ? t0->w + 2 : 20, hh = t0 != NULL ? t0->h + 2 : 20;

        for (i = 0; i < 4; i++) {
            for (f = -1; f < 4; f++) {
                ui_image(c, x + (f + 1) * step, y + i * hh, ui_overworld(0, i, f), 1);
            }
        }
        y += 4 * hh + 4;
    }
    label(c, x, y, "FIGURES: the 32 others, standing, down / up / left / right");
    y += 14;
    for (i = 0; i < 32; i++) {
        int d;

        for (d = 0; d < 4; d++) {
            ui_image(c, x + (i % 8) * 80 + d * 19, y + (i / 8) * 34, ui_overworld((u16)(1 + i), d, 0), 1);
        }
    }
}

static int test_data(const char *root)
{
    EidosUiCanvas cv;
    EidosUiCtx c = { 0 };   /* safe_* and bar_top: a desktop frame */
    UiGlyph g, q;
    const char *path = getenv("UI_KIT_SHEET");
    int i, n, ok = 1;

    /* Is there game data? */
    ui_assets_reset();
    if (!ui_font_glyph(UI_FONT_LARGE, 'A', &g)) {
        printf("  no game data under %s: data checks skipped\n", root);
        return 0;
    }
    BW_CHECK_EQ("large font cell", g.cell_w * 100 + g.cell_h, 12 * 100 + 15);
    ui_font_glyph(UI_FONT_SMALL, 'A', &g);
    BW_CHECK_EQ("small font cell", g.cell_w * 100 + g.cell_h, 10 * 100 + 10);
    ui_font_glyph(UI_FONT_MEDIUM, 'A', &g);
    BW_CHECK_EQ("medium font cell", g.cell_w * 100 + g.cell_h, 11 * 100 + 13);
    for (i = 0; i < 3; i++) {
        BW_REQUIRE("font has e acute", ui_font_glyph(i, 0xE9, &g) && ui_font_glyph(i, '?', &q) &&
                   (g.bitmap != q.bitmap));
        BW_REQUIRE("font has male sign", ui_font_glyph(i, 0x2642, &g) && g.bitmap != q.bitmap);
        BW_REQUIRE("font has female sign", ui_font_glyph(i, 0x2640, &g) && g.bitmap != q.bitmap);
        BW_REQUIRE("font has multiplication", ui_font_glyph(i, 0xD7, &g) && g.bitmap != q.bitmap);
        BW_REQUIRE("font has ellipsis", ui_font_glyph(i, 0x2026, &g) && g.bitmap != q.bitmap);
        BW_REQUIRE("font has a left arrow", ui_font_glyph(i, 0x2190, &g) && g.bitmap != q.bitmap);
    }
    BW_REQUIRE("text has width", ui_text_w("Pok\xC3\xA9mon", UI_FONT_LARGE) > 30);
    BW_REQUIRE("digits are narrower than M",
               ui_text_w("1", UI_FONT_LARGE) <= ui_text_w("M", UI_FONT_LARGE));

    BW_REQUIRE("item 4 icon", ui_item_icon(4) != NULL);
    BW_CHECK_EQ("item icon size", ui_item_icon(4)->w * 100 + ui_item_icon(4)->h, 2424);
    BW_REQUIRE("item icon has pixels", image_pixels(ui_item_icon(4)) > 100);
    BW_REQUIRE("item 0 has none", ui_item_icon(0) == NULL);
    BW_REQUIRE("item past the end has none", ui_item_icon(9999) == NULL);
    n = 0;
    for (i = 1; i < 627; i++) {
        n += image_pixels(ui_item_icon((u16)i)) > 20;
    }
    BW_CHECK_EQ("every item has an icon", n, 626);
    BW_REQUIRE("different items differ", images_differ(ui_item_icon(4), ui_item_icon(17)));
    BW_REQUIRE("icons are cached", ui_item_icon(4) == ui_item_icon(4));

    for (i = 495; i <= 503; i++) {
        const UiImage *f0 = ui_mon_icon((u16)i, 0, 0), *f1 = ui_mon_icon((u16)i, 0, 1);

        BW_REQUIRE("mon icon frame 0", f0 != NULL && image_pixels(f0) > 100);
        BW_REQUIRE("mon icon frame 1", f1 != NULL && image_pixels(f1) > 100);
        BW_REQUIRE("mon icon is 32x32", f0->w == 32 && f0->h == 32);
        BW_REQUIRE("frames differ", images_differ(f0, f1));
    }
    BW_REQUIRE("mon 0 has none", ui_mon_icon(0, 0, 0) == NULL);
    BW_REQUIRE("mon frame 2 has none", ui_mon_icon(495, 0, 2) == NULL);
    BW_REQUIRE("snivy and tepig differ", images_differ(ui_mon_icon(495, 0, 0), ui_mon_icon(498, 0, 0)));
    n = 0;
    for (i = 1; i <= 649; i++) {
        n += image_pixels(ui_mon_icon((u16)i, 0, 0)) > 50;
    }
    BW_CHECK_EQ("every species has an icon", n, 649);
    /* an alternate form has its own picture (Deerling, Basculin, Unfezant) */
    BW_REQUIRE("a form icon decodes", ui_mon_icon(550, 1, 0) != NULL);
    BW_REQUIRE("the form differs", images_differ(ui_mon_icon(550, 0, 0), ui_mon_icon(550, 1, 0)));

    /* the walking figures */
    BW_REQUIRE("trainer stands", image_pixels(ui_overworld(0, 0, -1)) > 60);
    for (i = 0; i < 4; i++) {
        int f;

        BW_REQUIRE("trainer walks", ui_overworld(0, i, 0) != NULL && image_pixels(ui_overworld(0, i, 0)) > 60);
        for (f = 1; f < 4; f++) {
            n = images_differ(ui_overworld(0, i, 0), ui_overworld(0, i, f)) ||
                images_differ(ui_overworld(0, i, 1), ui_overworld(0, i, f));
            BW_REQUIRE("walk frames differ", n);
        }
        BW_REQUIRE("directions differ", i == 0 || images_differ(ui_overworld(0, 0, -1), ui_overworld(0, i, -1)));
    }
    printf("  trainer figure %dx%d, other figure %dx%d\n", ui_overworld(0, 0, 0)->w, ui_overworld(0, 0, 0)->h,
           ui_overworld(1, 0, 0)->w, ui_overworld(1, 0, 0)->h);
    BW_REQUIRE("same size every frame", ui_overworld(0, 0, 0)->w == ui_overworld(0, 3, 2)->w &&
               ui_overworld(0, 0, 0)->h == ui_overworld(0, 3, 2)->h);
    for (i = 1; i <= 32; i++) {
        BW_REQUIRE("figure exists", image_pixels(ui_overworld((u16)i, 0, 0)) > 20);
    }
    BW_REQUIRE("heroine figure 33", ui_overworld(33, 0, 0) != NULL);
    BW_REQUIRE("no figure 34", ui_overworld(34, 0, 0) == NULL);
    BW_REQUIRE("no direction 4", ui_overworld(0, 4, 0) == NULL);
    /* a Pokemon's front comes from its animated sprite; trainers' are not decoded */
    BW_REQUIRE("Pokemon front", image_pixels(ui_mon_front(495, 0, 0)) > 100);
    BW_REQUIRE("trainer fronts are not decoded", ui_trainer_front(1) == NULL);

    /* the contact sheet */
    memset(&cv, 0, sizeof cv);
    ui_begin(&c, &cv, 660, 880, 2);
    draw_sheet(&c);
    if (path == NULL) {
        path = "ui_kit_sheet.png";
    }
    if (write_canvas_png(path, &cv, 2, 0x1E1E26) != 0) {
        printf("  could not write %s\n", path);
        ok = 0;
    } else {
        printf("  contact sheet: %s\n", path);
    }
    free(cv.rgba);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    BW_TEST_SETUP(argc, argv);

    if (test_canvas() != 0 || test_blend() != 0 || test_text() != 0 ||
        test_panels() != 0 || test_pieces() != 0 || test_icon_rows() != 0) {
        BW_REPORT("test_ui_kit");
    }
    /* the data checks use the game's own fonts, not the synthetic ones */
    ui_assets_reset();
    if (test_data(bw_data_root()) != 0) {
        BW_REPORT("test_ui_kit");
    }
    BW_REPORT("test_ui_kit");
}
