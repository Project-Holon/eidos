/*
 * engine/ui/padglyph.c -- the gamepad's buttons as the hints draw them
 * (padglyph.h).
 *
 * Every glyph is 13 canvas pixels high, the key cap's height, and drawn
 * here pixel by pixel: a round face button, a pill for a shoulder, a taller
 * pill for a trigger, a small oval for the menu buttons, a cross for the
 * D-pad and a ring for a stick.  The families differ by their colours and
 * marks, each after its controller:
 *
 *   Xbox 360    gem buttons in their colours with white letters, silver
 *               bumpers and menu buttons with arrows
 *   Xbox One    dark buttons with coloured letters, the view and menu marks
 *   PS3         glossy black buttons with the four shapes in colour,
 *               SELECT a bar and START an arrow
 *   PS4         matte grey buttons, the shapes in lighter colours, the
 *               share and options marks
 *   PS5         white buttons with dark shapes, the create and options marks
 *   Switch      dark buttons with white letters, plus and minus
 *   GameCube    the big teal A, the small red B, the curved X and Y, Z on
 *               the right shoulder, the yellow C stick
 *   Steam Deck  slate buttons with white letters, the view and menu marks
 *
 * The face buttons go by position: on a PlayStation pad the south button is
 * the cross, on a Switch pad it is B, on an Xbox pad it is A.  No maker's
 * logo is drawn.
 */

#include <string.h>

#include "padglyph.h"

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

static void px(EidosUiCtx *c, int x, int y, u32 rgb)
{
    ui_fill(c, x, y, 1, 1, rgb, 255);
}

/* A mask of rows ('X' set) in one colour. */
static void mask(EidosUiCtx *c, int x, int y, const char *const *rows, int n, u32 rgb)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; rows[i][j] != '\0'; j++) {
            if (rows[i][j] == 'X') {
                px(c, x + j, y + i, rgb);
            }
        }
    }
}

/* A mask's width (its first row). */
static int mask_w(const char *const *rows)
{
    return (int)strlen(rows[0]);
}

/* The 13-pixel round button: the face buttons, the guide, the sticks. */
static const char *const k_round[13] = {
    "....XXXXX....",
    "..XXXXXXXXX..",
    ".XXXXXXXXXXX.",
    ".XXXXXXXXXXX.",
    "XXXXXXXXXXXXX",
    "XXXXXXXXXXXXX",
    "XXXXXXXXXXXXX",
    "XXXXXXXXXXXXX",
    "XXXXXXXXXXXXX",
    ".XXXXXXXXXXX.",
    ".XXXXXXXXXXX.",
    "..XXXXXXXXX..",
    "....XXXXX....",
};

/* Whether (j, i) is set in a mask of n rows. */
static int in_mask(const char *const *rows, int n, int j, int i)
{
    return i >= 0 && i < n && j >= 0 && j < (int)strlen(rows[i]) && rows[i][j] == 'X';
}

/* A mask filled, its border pixels in the rim colour. */
static void shape(EidosUiCtx *c, int x, int y, const char *const *rows, int n, u32 fill, u32 rim)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; rows[i][j] != '\0'; j++) {
            if (rows[i][j] != 'X') {
                continue;
            }
            px(c, x + j, y + i,
               in_mask(rows, n, j - 1, i) && in_mask(rows, n, j + 1, i) &&
               in_mask(rows, n, j, i - 1) && in_mask(rows, n, j, i + 1) ? fill : rim);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Marks                                                               */
/* ------------------------------------------------------------------ */

static const char *const k_cross[7] = {
    "X.....X",
    "XX...XX",
    ".XX.XX.",
    "..XXX..",
    ".XX.XX.",
    "XX...XX",
    "X.....X",
};
static const char *const k_circle[7] = {
    "..XXX..",
    ".X...X.",
    "X.....X",
    "X.....X",
    "X.....X",
    ".X...X.",
    "..XXX..",
};
static const char *const k_square[7] = {
    "XXXXXXX",
    "X.....X",
    "X.....X",
    "X.....X",
    "X.....X",
    "X.....X",
    "XXXXXXX",
};
static const char *const k_triangle[6] = {
    "...X...",
    "..X.X..",
    "..X.X..",
    ".X...X.",
    ".X...X.",
    "XXXXXXX",
};
static const char *const k_arrow_r[7] = {
    "X...", "XX..", "XXX.", "XXXX", "XXX.", "XX..", "X...",
};
static const char *const k_arrow_l[7] = {
    "...X", "..XX", ".XXX", "XXXX", ".XXX", "..XX", "...X",
};
static const char *const k_lines[5] = {
    "XXXXX", ".....", "XXXXX", ".....", "XXXXX",
};
static const char *const k_view[6] = {
    "XXXX..",
    "X..X..",
    "X.XXXX",
    "XXX..X",
    "..X..X",
    "..XXXX",
};
static const char *const k_bar[3] = {
    "XXXXX", "XXXXX", "XXXXX",
};
static const char *const k_share[6] = {
    "..X..",
    ".XXX.",
    "X.X.X",
    "..X..",
    "..X..",
    "XXXXX",
};
static const char *const k_create[5] = {
    "X.X.X",
    ".X.X.",
    "XXXXX",
    ".X.X.",
    "X.X.X",
};
static const char *const k_plus[5] = {
    "..X..", "..X..", "XXXXX", "..X..", "..X..",
};
static const char *const k_minus[1] = {
    "XXXXX",
};
static const char *const k_home[6] = {
    "...X...",
    "..XXX..",
    ".XXXXX.",
    "XXXXXXX",
    ".XX.XX.",
    ".XX.XX.",
};
static const char *const k_ring[7] = {
    "..XXX..",
    ".X...X.",
    "X..X..X",
    "X.XXX.X",
    "X..X..X",
    ".X...X.",
    "..XXX..",
};

/* Draw a mark centred in a box (bx, by, bw, bh). */
static void mark(EidosUiCtx *c, int bx, int by, int bw, int bh, const char *const *rows, int n,
                 u32 rgb)
{
    mask(c, bx + (bw - mask_w(rows)) / 2, by + (bh - n) / 2, rows, n, rgb);
}

/* A text label centred in a box, in the hints' small font. */
static void label(EidosUiCtx *c, int bx, int by, int bw, int bh, const char *s, u32 rgb, s32 shadow)
{
    int tw = ui_text_w(s, UI_FONT_SMALL);

    /* the small font's letters sit a pixel left of and above its cell's
     * middle: one down puts them in the middle of the box */
    ui_text(c, bx + (bw - tw) / 2, by + ui_text_y(bh, UI_FONT_SMALL) + 1, s, rgb, shadow,
            UI_FONT_SMALL, 1);
}

/* ------------------------------------------------------------------ */
/* The families                                                        */
/* ------------------------------------------------------------------ */

typedef struct Fam {
    const char *name;
    u32 body, rim, hi;          /* the buttons' body, rim and gloss (0: none) */
    u32 ink;                    /* letters and marks on the body              */
    u32 sbody, srim, sink;      /* shoulders and menu buttons                 */
    u32 face[4];                /* per face button (south, east, west, north):
                                   the mark's colour, or the body's for gems  */
    int gem;                    /* face buttons filled with their colour      */
    const char *letter[4];      /* the face letters; NULL: the shapes          */
    const char *sh[4];          /* LB RB LT RT                                 */
} Fam;

static const Fam k_fam[UI_PAD_FAMILIES] = {
    [UI_PAD_XBOX360] = { "Xbox 360", 0x2A2A2A, 0x5A5A5A, 0, 0xFFFFFF,
                         0xC6CAD1, 0x7A7F88, 0x23262B,
                         { 0x45A83F, 0xD8392E, 0x2F6FCB, 0xEDBB12 }, 1,
                         { "A", "B", "X", "Y" }, { "LB", "RB", "LT", "RT" } },
    [UI_PAD_XBOXONE] = { "Xbox One / Series", 0x151515, 0x484848, 0, 0xFFFFFF,
                         0x151515, 0x484848, 0xE8E8E8,
                         { 0x6CC95C, 0xE8584B, 0x4D8CE8, 0xF4C93A }, 0,
                         { "A", "B", "X", "Y" }, { "LB", "RB", "LT", "RT" } },
    [UI_PAD_PS3]     = { "PS3", 0x0C0C0C, 0x7A7A7A, 0x8A8A8A, 0xFFFFFF,
                         0x121212, 0x6C6C6C, 0xD8D8D8,
                         { 0x5BA8EC, 0xE5484D, 0xE77FC4, 0x2FC07A }, 0,
                         { NULL, NULL, NULL, NULL }, { "L1", "R1", "L2", "R2" } },
    [UI_PAD_PS4]     = { "PS4", 0x2A2D35, 0x4E5361, 0, 0xFFFFFF,
                         0x2A2D35, 0x4E5361, 0xEDEDF2,
                         { 0x93BBFF, 0xFF7C88, 0xF3A6DA, 0x63D6A9 }, 0,
                         { NULL, NULL, NULL, NULL }, { "L1", "R1", "L2", "R2" } },
    [UI_PAD_PS5]     = { "PS5", 0xE2E2E8, 0x8D8D97, 0, 0x34343E,
                         0xE2E2E8, 0x8D8D97, 0x34343E,
                         { 0x3A3A46, 0x3A3A46, 0x3A3A46, 0x3A3A46 }, 0,
                         { NULL, NULL, NULL, NULL }, { "L1", "R1", "L2", "R2" } },
    [UI_PAD_SWITCH]  = { "Switch", 0x262626, 0x5C5C5C, 0, 0xFFFFFF,
                         0x262626, 0x5C5C5C, 0xFFFFFF,
                         { 0xCFCFD2, 0xCFCFD2, 0xCFCFD2, 0xCFCFD2 }, 0,
                         { "B", "A", "Y", "X" }, { "L", "R", "ZL", "ZR" } },
    [UI_PAD_GAMECUBE] = { "GameCube", 0xC2C3CA, 0x6E6F78, 0, 0x2E2E36,
                         0xC2C3CA, 0x6E6F78, 0x2E2E36,
                         { 0x36B89E, 0x9A9BA6, 0xD9343A, 0x9A9BA6 }, 0,
                         { "A", "X", "B", "Y" }, { "", "Z", "L", "R" } },
    [UI_PAD_STEAMDECK] = { "Steam Deck", 0x30353D, 0x667080, 0, 0xFFFFFF,
                         0x30353D, 0x667080, 0xEEF0F4,
                         { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF }, 0,
                         { "A", "B", "X", "Y" }, { "L1", "R1", "L2", "R2" } },
};

const char *ui_pad_family_name(int family)
{
    return family >= 0 && family < UI_PAD_FAMILIES ? k_fam[family].name : "";
}

static u32 darken(u32 rgb, int num, int den)
{
    u32 r = ((rgb >> 16) & 255u) * (u32)num / (u32)den;
    u32 g = ((rgb >> 8) & 255u) * (u32)num / (u32)den;
    u32 b = (rgb & 255u) * (u32)num / (u32)den;

    return (r << 16) | (g << 8) | b;
}

static u32 lighten(u32 rgb, int pct)
{
    u32 r = (rgb >> 16) & 255u, g = (rgb >> 8) & 255u, b = rgb & 255u;

    r += (255u - r) * (u32)pct / 100u;
    g += (255u - g) * (u32)pct / 100u;
    b += (255u - b) * (u32)pct / 100u;
    return (r << 16) | (g << 8) | b;
}

/* The round button: body, rim and a touch of gloss at the top left. */
static void round_button(EidosUiCtx *c, int x, int y, u32 body, u32 rim, u32 hi)
{
    shape(c, x, y, k_round, 13, body, rim);
    if (hi != 0) {
        px(c, x + 3, y + 2, hi);
        px(c, x + 4, y + 2, hi);
        px(c, x + 5, y + 1, hi);
        px(c, x + 6, y + 1, hi);
        px(c, x + 2, y + 3, hi);
        px(c, x + 2, y + 4, hi);
    }
}

/* The GameCube's face: the big green A (south), the small red B (west),
 * the curved X (east) and Y (north) hugging the A, light grey with the
 * letter pressed in. */
static void face_gc(EidosUiCtx *c, int x, int y, const Fam *f, int i)
{
    static const char *const small[9] = {
        "..XXXXX..", ".XXXXXXX.", "XXXXXXXXX", "XXXXXXXXX", "XXXXXXXXX",
        "XXXXXXXXX", "XXXXXXXXX", ".XXXXXXX.", "..XXXXX..",
    };
    static const char *const kid_x[13] = {
        "...XXXX..", "..XXXXXX.", ".XXXXXXXX", ".XXXXXXXX", "..XXXXXXX",
        "...XXXXXX", "...XXXXXX", "...XXXXXX", "..XXXXXXX", ".XXXXXXXX",
        ".XXXXXXXX", "..XXXXXX.", "...XXXX..",
    };
    static const char *const kid_y[10] = {
        "....XXXXX....", "..XXXXXXXXX..", ".XXXXXXXXXXX.", "XXXXXXXXXXXXX",
        "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", "XXXXX...XXXXX",
        "XXX.......XXX", ".X.........X.",
    };

    switch (i) {
    case 0:
        round_button(c, x, y, f->face[0], darken(f->face[0], 3, 5), lighten(f->face[0], 45));
        label(c, x, y, 13, 13, "A", 0xFFFFFF, (s32)darken(f->face[0], 2, 5));
        break;
    case 2:
        shape(c, x + 2, y + 2, small, 9, f->face[2], darken(f->face[2], 3, 5));
        label(c, x, y, 13, 13, "B", 0xFFFFFF, -1);
        break;
    case 1:
        shape(c, x + 2, y, kid_x, 13, 0xD9DAE0, f->rim);
        label(c, x + 5, y, 6, 13, "X", 0x3A3B44, -1);
        break;
    default:
        shape(c, x, y + 1, kid_y, 10, 0xD9DAE0, f->rim);
        label(c, x, y + 1, 13, 8, "Y", 0x3A3B44, -1);
        break;
    }
}

static void face(EidosUiCtx *c, int x, int y, const Fam *f, int i)
{
    if (f == &k_fam[UI_PAD_GAMECUBE]) {
        face_gc(c, x, y, f, i);
        return;
    }
    static const char *const *shapes[4] = { k_cross, k_circle, k_square, k_triangle };
    static const int shape_n[4] = { 7, 7, 7, 6 };

    if (f->gem) {
        u32 col = f->face[i];

        round_button(c, x, y, col, darken(col, 3, 5), lighten(col, 45));
        label(c, x, y, 13, 13, f->letter[i], 0xFFFFFF, (s32)darken(col, 2, 5));
        return;
    }
    round_button(c, x, y, f->body, f->rim, f->hi);
    if (f->letter[i] != NULL) {
        label(c, x, y, 13, 13, f->letter[i], f->face[i], -1);
    } else {
        mark(c, x, y, 13, 13, shapes[i], shape_n[i], f->face[i]);
    }
}

/* A shoulder: a flat pill, 11 high, at the bottom of the 13. */
static void pill(EidosUiCtx *c, int x, int y, int w, const Fam *f, const char *s)
{
    int i, j;

    for (i = 0; i < 11; i++) {
        for (j = 0; j < w; j++) {
            int corner = (i == 0 || i == 10) && (j == 0 || j == w - 1);
            int edge = i == 0 || i == 10 || j == 0 || j == w - 1;

            if (!corner) {
                px(c, x + j, y + 2 + i, edge ? f->srim : f->sbody);
            }
        }
    }
    label(c, x, y + 2, w, 11, s, f->sink, -1);
}

/* A trigger: the full 13 high, its top curved like the trigger's face
 * (round corners, a lighter band along the top), its bottom straight. */
static void trigger(EidosUiCtx *c, int x, int y, int w, const Fam *f, const char *s)
{
    int i, j;

    for (i = 0; i < 13; i++) {
        int in = i == 0 ? 3 : i == 1 ? 1 : 0;   /* the round top's inset */

        for (j = in; j < w - in; j++) {
            int edge = i == 0 || i == 12 || j == in || j == w - 1 - in ||
                       (i == 1 && (j < 3 || j >= w - 3));
            u32 col = edge ? f->srim : (i <= 2 ? lighten(f->sbody, 30) : f->sbody);

            if (i == 12 && (j == 0 || j == w - 1)) {
                continue;
            }
            px(c, x + j, y + i, col);
        }
    }
    label(c, x, y + 2, w, 11, s, f->sink, -1);
}

/* The small oval of a menu button, 15 x 9, with its mark. */
static void menu_button(EidosUiCtx *c, int x, int y, const Fam *f, const char *const *rows, int n)
{
    static const char *const oval[9] = {
        "..XXXXXXXXXXX..",
        ".XXXXXXXXXXXXX.",
        "XXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXX",
        ".XXXXXXXXXXXXX.",
        "..XXXXXXXXXXX..",
    };

    shape(c, x, y + 2, oval, 9, f->sbody, f->srim);
    mark(c, x, y + 2, 15, 9, rows, n, f->sink);
}

/* The D-pad: a cross (on a PlayStation pad four separate arms around an
 * empty middle), the arm of the direction lit.  Both shapes are the same
 * every way round, so the four directions mirror each other. */
static void dpad(EidosUiCtx *c, int x, int y, const Fam *f, int dir, int ps)
{
    static const char *const cross[13] = {
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "XXXXXXXXXXXXX",
        "XXXXXXXXXXXXX",
        "XXXXXXXXXXXXX",
        "XXXXXXXXXXXXX",
        "XXXXXXXXXXXXX",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
    };
    static const char *const sep[13] = {
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "XXXX.....XXXX",
        "XXXX.....XXXX",
        "XXXX.....XXXX",
        "XXXX.....XXXX",
        "XXXX.....XXXX",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
        "....XXXXX....",
    };
    const char *const *m = ps ? sep : cross;
    u32 lit = f->ink == 0xFFFFFF ? 0xFFFFFF : f->ink;
    int i, j;

    shape(c, x, y, m, 13, f->body, f->rim);
    /* the lit arm: its rows or columns of the cross, outside the middle */
    for (i = 0; i < 13; i++) {
        for (j = 0; j < 13; j++) {
            int on = 0;

            if (!in_mask(m, 13, j, i)) {
                continue;
            }
            switch (dir) {
            case 0: on = i <= 3; break;
            case 1: on = i >= 9; break;
            case 2: on = j <= 3; break;
            default: on = j >= 9; break;
            }
            if (on) {
                px(c, x + j, y + i, lit);
            }
        }
    }
}

/* A stick: the round button, its click named; a direction shows the
 * stick's cap pushed that way. */
static void stick(EidosUiCtx *c, int x, int y, const Fam *f, const char *s, int dir)
{
    static const char *const cap[5] = { ".XXX.", "XXXXX", "XXXXX", "XXXXX", ".XXX." };
    static const int dx[4] = { 0, 0, -2, 2 }, dy[4] = { -2, 2, 0, 0 };

    round_button(c, x, y, f->body, f->rim, f->hi);
    if (dir < 0) {
        label(c, x, y, 13, 13, s, f->ink, -1);
        return;
    }
    mask(c, x + 4, y + 4, cap, 5, f->rim);
    mask(c, x + 4 + dx[dir], y + 4 + dy[dir], cap, 5, f->ink);
}

/* ------------------------------------------------------------------ */
/* The glyphs                                                          */
/* ------------------------------------------------------------------ */

static int pill_w(const Fam *f, int i)
{
    int w;

    if (f->sh[i][0] == '\0') {
        return 0;               /* the family has no such button */
    }
    w = ui_text_w(f->sh[i], UI_FONT_SMALL) + 8;

    return w < 15 ? 15 : w;
}

int ui_pad_glyph_w(int family, int control)
{
    const Fam *f;

    if (family < 0 || family >= UI_PAD_FAMILIES || control < 0 || control >= UI_PAD_CONTROLS) {
        return 0;
    }
    f = &k_fam[family];
    if (family == UI_PAD_GAMECUBE && (control == UI_PAD_BACK || control == UI_PAD_GUIDE)) {
        return 0;               /* START is its only menu button */
    }
    switch (control) {
    case UI_PAD_BACK:
    case UI_PAD_START:
        return 15;
    case UI_PAD_LSHOULDER:
        return pill_w(f, 0);
    case UI_PAD_RSHOULDER:
        return pill_w(f, 1);
    case UI_PAD_LTRIGGER:
        return pill_w(f, 2);
    case UI_PAD_RTRIGGER:
        return pill_w(f, 3);
    default:
        return 13;
    }
}

int ui_pad_glyph(EidosUiCtx *c, int x, int y, int family, int control)
{
    const Fam *f;
    int ps = family == UI_PAD_PS3 || family == UI_PAD_PS4 || family == UI_PAD_PS5;

    if (ui_pad_glyph_w(family, control) == 0) {
        return 0;
    }
    f = &k_fam[family];
    switch (control) {
    case UI_PAD_SOUTH:
    case UI_PAD_EAST:
    case UI_PAD_WEST:
    case UI_PAD_NORTH:
        face(c, x, y, f, control - UI_PAD_SOUTH);
        break;
    case UI_PAD_BACK:
        switch (family) {
        case UI_PAD_XBOX360: menu_button(c, x, y, f, k_arrow_l, 7); break;
        case UI_PAD_XBOXONE:
        case UI_PAD_STEAMDECK: menu_button(c, x, y, f, k_view, 6); break;
        case UI_PAD_PS3:     menu_button(c, x, y, f, k_bar, 3); break;
        case UI_PAD_PS4:     menu_button(c, x, y, f, k_share, 6); break;
        case UI_PAD_PS5:     menu_button(c, x, y, f, k_create, 5); break;
        default:             menu_button(c, x, y, f, k_minus, 1); break;
        }
        break;
    case UI_PAD_START:
        switch (family) {
        case UI_PAD_XBOX360: menu_button(c, x, y, f, k_arrow_r, 7); break;
        case UI_PAD_PS3:
        case UI_PAD_GAMECUBE: menu_button(c, x, y, f, k_arrow_r, 7); break;
        case UI_PAD_SWITCH:  menu_button(c, x, y, f, k_plus, 5); break;
        default:             menu_button(c, x, y, f, k_lines, 5); break;
        }
        break;
    case UI_PAD_GUIDE:
        round_button(c, x, y, f->body, f->rim, f->hi);
        if (family == UI_PAD_SWITCH) {
            mark(c, x, y, 13, 13, k_home, 6, f->ink);
        } else {
            mark(c, x, y, 13, 13, k_ring, 7,
                 family == UI_PAD_XBOX360 || family == UI_PAD_XBOXONE ? 0x6CC95C : f->ink);
        }
        break;
    case UI_PAD_LSTICK:
        stick(c, x, y, f, ps || family == UI_PAD_STEAMDECK ? "L3" : "LS", -1);
        break;
    case UI_PAD_RSTICK:
        if (family == UI_PAD_GAMECUBE) {
            round_button(c, x, y, 0xF2C230, 0x9C7A10, 0);   /* the C stick */
            label(c, x, y, 13, 13, "C", 0x5A4608, -1);
        } else {
            stick(c, x, y, f, ps || family == UI_PAD_STEAMDECK ? "R3" : "RS", -1);
        }
        break;
    case UI_PAD_LSHOULDER:
        pill(c, x, y, pill_w(f, 0), f, f->sh[0]);
        break;
    case UI_PAD_RSHOULDER:
        pill(c, x, y, pill_w(f, 1), f, f->sh[1]);
        break;
    case UI_PAD_LTRIGGER:
        trigger(c, x, y, pill_w(f, 2), f, f->sh[2]);
        break;
    case UI_PAD_RTRIGGER:
        trigger(c, x, y, pill_w(f, 3), f, f->sh[3]);
        break;
    case UI_PAD_DUP:
    case UI_PAD_DDOWN:
    case UI_PAD_DLEFT:
    case UI_PAD_DRIGHT:
        dpad(c, x, y, f, control - UI_PAD_DUP, ps);
        break;
    default:
        stick(c, x, y, f, "", control - UI_PAD_LS_UP);
        break;
    }
    return ui_pad_glyph_w(family, control);
}
