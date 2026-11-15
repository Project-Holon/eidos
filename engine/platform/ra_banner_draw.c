/*
 * engine/platform/ra_banner_draw.c -- the achievement banner's picture and
 * place for the two-screen layouts (ra_banner_draw.h).
 *
 * The timing is the game's (sdk/ra.c, bw_ra_banner): this file draws the
 * words into a small RGBA picture with the port's own bitmap font, only
 * when the banner or the interface style changes, and says where the
 * picture is for this frame: centred at the top of the window, sliding down
 * by `progress`.  The style follows the interface style the player chose
 * (Kosmos, Plasma, Harmonia) in its colours.
 */

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "ra_banner_draw.h"
#include "settings.h"

#ifdef EIDOS_HAVE_RA

#include "ra.h"

#define MARGIN 6
#define BORDER 2
#define CHARS  ((EIDOS_RA_BANNER_W - 2 * MARGIN) / EIDOS_FONT_ADVANCE)

static u8 g_picture[EIDOS_RA_BANNER_W * EIDOS_RA_BANNER_H * 4];
static u32 g_gen;               /* what the picture shows is banner number */
static int g_drawn_theme = -1;
static u32 g_drawn_gen;
static int g_drawn_ok;
static u32 g_version;           /* the picture's own count, for the platform */

typedef struct Style {
    u32 body, edge, ink, accent;
} Style;

/* 0xAARRGGBB */
static Style style_for(int theme)
{
    Style s;

    if (theme == 1) {           /* Plasma: the white chip */
        s.body = 0xFFF4F5FAu; s.edge = 0xFF141414u; s.ink = 0xFF202028u; s.accent = 0xFF2A5BD7u;
    } else if (theme == 2) {    /* Harmonia: the grey tab */
        s.body = 0xFF454545u; s.edge = 0xFF141414u; s.ink = 0xFFF0F0F0u; s.accent = 0xFF7FB0E8u;
    } else {                    /* Kosmos */
        s.body = 0xFF1E2230u; s.edge = 0xFF35C8E6u; s.ink = 0xFFE8EAF0u; s.accent = 0xFF35C8E6u;
    }
    return s;
}

static void put(int x, int y, u32 c)
{
    u8 *p;

    if (x < 0 || y < 0 || x >= EIDOS_RA_BANNER_W || y >= EIDOS_RA_BANNER_H) {
        return;
    }
    p = g_picture + ((size_t)y * EIDOS_RA_BANNER_W + (size_t)x) * 4u;
    p[0] = (u8)((c >> 16) & 0xFFu);
    p[1] = (u8)((c >> 8) & 0xFFu);
    p[2] = (u8)(c & 0xFFu);
    p[3] = (u8)((c >> 24) & 0xFFu);
}

static void fill(int x, int y, int w, int h, u32 c)
{
    int i, j;

    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            put(i, j, c);
        }
    }
}

static void text(int x, int y, const char *s, u32 colour, int max)
{
    int n = 0;
    u32 ch;

    while (n < max && (ch = eidos_utf8_next(&s)) != 0) {
        const u8 *rows = eidos_font_glyph(ch);
        int r, col;

        for (r = 0; r < EIDOS_FONT_ROWS; r++) {
            for (col = 0; col < EIDOS_FONT_COLS; col++) {
                if (rows[r] & (0x10u >> col)) {
                    put(x + n * EIDOS_FONT_ADVANCE + col, y + r, colour);
                }
            }
        }
        n++;
    }
}

/* `s` cut into at most two lines of CHARS on spaces; a longer text ends in "...". */
static void wrap2(const char *s, char out[2][CHARS + 4])
{
    size_t n = strlen(s), cut;
    int line;

    out[0][0] = out[1][0] = '\0';
    for (line = 0; line < 2 && n > 0; line++) {
        while (n > 0 && *s == ' ') {
            s++;
            n--;
        }
        if (n <= (size_t)CHARS) {
            cut = n;
        } else {
            cut = CHARS;
            while (cut > 0 && s[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = CHARS;
            }
        }
        memcpy(out[line], s, cut);
        out[line][cut] = '\0';
        s += cut;
        n -= cut;
    }
    if (n > 0) {                /* more than two lines: mark the cut */
        size_t l = strlen(out[1]);

        if (l > (size_t)CHARS - 3) {
            l = (size_t)CHARS - 3;
        }
        memcpy(out[1] + l, "...", 4);
    }
}

static void draw(const BwRaBannerView *v, int theme)
{
    Style st = style_for(theme);
    char lines[2][CHARS + 4];
    char pts[32];

    fill(0, 0, EIDOS_RA_BANNER_W, EIDOS_RA_BANNER_H, 0x00000000u);
    /* the body, its two lower corners cut, and the edge round it */
    fill(0, 0, EIDOS_RA_BANNER_W, EIDOS_RA_BANNER_H, st.edge);
    fill(BORDER, 0, EIDOS_RA_BANNER_W - 2 * BORDER, EIDOS_RA_BANNER_H - BORDER, st.body);
    fill(0, EIDOS_RA_BANNER_H - 4, 4, 4, 0x00000000u);
    fill(EIDOS_RA_BANNER_W - 4, EIDOS_RA_BANNER_H - 4, 4, 4, 0x00000000u);
    text(MARGIN, 5, v->title, st.ink, CHARS);
    wrap2(v->text, lines);
    text(MARGIN, 16, lines[0], st.ink, CHARS);
    text(MARGIN, 27, lines[1], st.ink, CHARS);
    snprintf(pts, sizeof(pts), "%u point%s", v->points, v->points == 1 ? "" : "s");
    text(MARGIN, 39, pts, st.accent, CHARS);
}

int eidos_ra_banner_view(EidosRaBannerView *view, int out_w, int out_h,
                         int ax, int ay, int aw)
{
    BwRaBannerView v;
    int theme, w, h;

    (void)out_h;
    if (view == NULL || bw_ra_banner(&v) == 0) {
        return -1;
    }
    theme = eidos_settings_get(EIDOS_OPT_UI_THEME);
    if (!g_drawn_ok || g_drawn_gen != v.generation || g_drawn_theme != theme) {
        draw(&v, theme);
        g_drawn_gen = v.generation;
        g_drawn_theme = theme;
        g_drawn_ok = 1;
        g_version++;
    }
    (void)g_gen;
    /* about half the main screen's width, never wider than the window; the
     * presenters smooth the picture, so the size need not be a whole
     * multiple of the picture's */
    w = aw * 49 / 100;
    if (w > out_w) {
        w = out_w;
    }
    if (w < EIDOS_RA_BANNER_W) {
        w = EIDOS_RA_BANNER_W;
    }
    h = w * EIDOS_RA_BANNER_H / EIDOS_RA_BANNER_W;
    view->rgba = g_picture;
    view->w = EIDOS_RA_BANNER_W;
    view->h = EIDOS_RA_BANNER_H;
    view->generation = g_version;
    view->dst.x = ax - w + w * v.progress / 256;   /* in from the left */
    view->dst.y = ay;
    view->dst.w = w;
    view->dst.h = h;
    return 0;
}

#else /* !EIDOS_HAVE_RA */

int eidos_ra_banner_view(EidosRaBannerView *view, int out_w, int out_h,
                         int ax, int ay, int aw)
{
    (void)view; (void)out_w; (void)out_h; (void)ax; (void)ay; (void)aw;
    return -1;
}

#endif
