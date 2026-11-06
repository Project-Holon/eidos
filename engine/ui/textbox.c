/*
 * engine/ui/textbox.c -- the game's text windows as the interface's text
 * boxes, in the one-screen layout (engine/ui/textbox.h).
 *
 * Three kinds, from the approved mock-ups:
 *
 *   narration  white, a grey edge, cut corners (the plain window at the
 *              bottom of the field, signs, the professor's intro)
 *   balloon    the same white box with a tail to the speaker (people)
 *   system     black with the battle tiles' strip down the left (the PC,
 *              the save, notices)
 *
 * The words are the game's own: each window's bitmap (a BmpWin's Bmp of
 * 4-bit tiles) is copied letter by letter into the box, so the text prints
 * at the game's speed, in the game's font and every language, and the
 * box needs no knowledge of what is said.  The game's own windows are left
 * out of the screen (g2d_hw_hide_main_bgs) and its balloon polygons are not
 * drawn (bw_ui_text_own, stubs/ov/patch_ov021_talkmsgwin.c).
 *
 * Records (src/game/ov021/field_msgbg_types.h, talkmsgwin_types.h,
 * arm9/bmp_win_types.h, arm9/bmp_shared_types.h):
 *   FieldMsgBg  bgFrame 0x002, bgFrameSys 0x004, msgPrint[4] 0x010 (0x24
 *               each: printQue 0x0C, bmpWin 0x14), talkSys 0x15C
 *   TalkMsgWinSys param.bgLayer 0x010, windows 0x018 (0x64 each: state
 *               0x00, isPlain 0x02, bmpWin 0x0C)
 *   BmpWin      bgLayer 0x02, posX 0x03, posY 0x04, width 0x05, height
 *               0x06 (tiles), bmp 0x0C
 *   Bmp         pixels 0x00, width 0x04, height 0x06
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "textbox.h"
#include "kit.h"

#define WEAK __attribute__((weak))

extern void *ov021_FIELDMAP_GetMsgBg(void *fieldMap) WEAK;
extern void *ov021_FIELDMAP_GetInfoWindow(void *fieldMap) WEAK;
extern int  bw_ui_talk_target_get(const void *win, int *x, int *y, int *side) WEAK;
extern int  bw_ui_field_wins(const void **out, int max) WEAK;

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static u16 rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }
static u8 *rdp(const u8 *p) { return (u8 *)(uintptr_t)rd32(p); }

/* The approved colours (mock-ups n3A and n4b S1). */
#define TB_PAPER    0xFFFFFFu
#define TB_PEDGE    0x9698A0u
#define TB_INK      0x42424Au
#define TB_INKSH    0xADADB5u
#define TB_SYS      0x000000u
#define TB_SEDGE    0x464850u
#define TB_SYSSH    0x464850u
#define TB_HL       0x008BACu

/* ------------------------------------------------------------------ */
/* The game's window bitmaps                                           */
/* ------------------------------------------------------------------ */

/* One pixel of a bitmap of 4-bit tiles, bw pixels wide. */
static int bmp_px(const u8 *px, int bw, int x, int y)
{
    u8 v = px[((y >> 3) * (bw >> 3) + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];

    return (x & 1) != 0 ? v >> 4 : v & 15;
}

typedef struct WinBmp {
    const u8 *px;
    int w, h;               /* pixels                                      */
    int ground, ink;        /* the commonest colour index, then the next    */
    int top, bottom;        /* the rows that hold anything (bottom
                               exclusive); top > bottom when blank          */
    int right;              /* the last column that holds anything + 1     */
    int left;               /* the first column that holds anything        */
    u16 letters;            /* the colours that are letters (bit per index;
                               the rest are their shadows): a word the game
                               prints in another colour (an item's name in
                               blue) is one of these besides `ink`         */
} WinBmp;

static int bmp_scan(const u8 *bmp, WinBmp *b)
{
    int cnt[16] = { 0 }, x, y, i;

    memset(b, 0, sizeof(*b));
    if (bmp == NULL || (b->px = rdp(bmp)) == NULL) {
        return 0;
    }
    b->w = rd16(bmp + 0x04);
    b->h = rd16(bmp + 0x06);
    if (b->w <= 0 || b->h <= 0 || b->w > 256 || b->h > 192 || (b->w & 7) || (b->h & 7)) {
        return 0;
    }
    for (y = 0; y < b->h; y++) {
        for (x = 0; x < b->w; x++) {
            cnt[bmp_px(b->px, b->w, x, y)]++;
        }
    }
    for (i = 1; i < 16; i++) {
        if (cnt[i] > cnt[b->ground]) {
            b->ground = i;
        }
    }
    b->ink = -1;
    {
        /* the two commonest colours after the ground are the letters and
         * their shadow; the shadow sits one pixel right of, below, or
         * diagonally from the letter, so the colour that more often has the
         * other one there is the letter */
        int c1 = -1, c2 = -1, ab = 0, ba = 0;

        for (i = 0; i < 16; i++) {
            if (i == b->ground || cnt[i] == 0) {
                continue;
            }
            if (c1 < 0 || cnt[i] > cnt[c1]) {
                c2 = c1;
                c1 = i;
            } else if (c2 < 0 || cnt[i] > cnt[c2]) {
                c2 = i;
            }
        }
        b->ink = c1;
        if (c1 >= 0 && c2 >= 0) {
            for (y = 0; y + 1 < b->h; y++) {
                for (x = 0; x + 1 < b->w; x++) {
                    int v = bmp_px(b->px, b->w, x, y);
                    int r = bmp_px(b->px, b->w, x + 1, y), d = bmp_px(b->px, b->w, x, y + 1);
                    int dd = bmp_px(b->px, b->w, x + 1, y + 1);

                    if (v == c1 && (r == c2 || d == c2 || dd == c2)) ab++;
                    if (v == c2 && (r == c1 || d == c1 || dd == c1)) ba++;
                }
            }
            b->ink = ab >= ba ? c1 : c2;
        }
    }
    {
        /* every colour: a letter has its shadow right of, below or
         * diagonally from it; a shadow has its letter left of or above it */
        int as_letter[16] = { 0 }, as_shadow[16] = { 0 };

        for (y = 0; y + 1 < b->h; y++) {
            for (x = 0; x + 1 < b->w; x++) {
                int v = bmp_px(b->px, b->w, x, y);
                int r = bmp_px(b->px, b->w, x + 1, y), d = bmp_px(b->px, b->w, x, y + 1);

                if (v == b->ground) {
                    continue;
                }
                if (r != b->ground && r != v) { as_letter[v]++; as_shadow[r]++; }
                if (d != b->ground && d != v) { as_letter[v]++; as_shadow[d]++; }
            }
        }
        b->letters = 0;
        for (i = 0; i < 16; i++) {
            if (i != b->ground && cnt[i] > 0 && as_letter[i] > as_shadow[i]) {
                b->letters |= (u16)(1u << i);
            }
        }
        if (b->ink >= 0) {
            b->letters |= (u16)(1u << b->ink);
        }
    }
    b->top = b->h;
    b->bottom = 0;
    b->left = b->w;
    for (y = 0; y < b->h; y++) {
        for (x = 0; x < b->w; x++) {
            if (bmp_px(b->px, b->w, x, y) != b->ground) {
                if (y < b->top) b->top = y;
                if (y + 1 > b->bottom) b->bottom = y + 1;
                if (x + 1 > b->right) b->right = x + 1;
                if (x < b->left) b->left = x;
            }
        }
    }
    return 1;
}

static int win_bmp(const u8 *bmpwin, WinBmp *b)
{
    if (bmpwin == NULL) {
        memset(b, 0, sizeof(*b));
        return 0;
    }
    return bmp_scan(rdp(bmpwin + 0x0C), b);
}

/* The window's letters at (x, y): the letters in `ink`, the rest (their
 * shadow, the "more" cursor) in `shadow`. */
static void draw_letters(EidosUiCtx *c, const WinBmp *b, int x, int y, u32 ink, u32 shadow)
{
    int xx, yy;
    /* a word in another colour: the interface's accent, light on a dark
     * box, dark on a white one */
    u32 accent = ((ink >> 16) & 255u) > 160u ? 0x7FD0FFu : 0x007A9Cu;

    for (yy = 0; yy < b->h; yy++) {
        for (xx = 0; xx < b->w; xx++) {
            int v = bmp_px(b->px, b->w, xx, yy);

            if (v != b->ground) {
                u32 col = v == b->ink ? ink : (b->letters >> v) & 1u ? accent : shadow;

                ui_fill(c, x + xx, y + yy, 1, 1, col, 255);
            }
        }
    }
}

const void *ui_bmpwin_bmp(const void *bmpwin)
{
    const u8 *w = (const u8 *)bmpwin;

    if (w == NULL || (rd16(w) & 0x7FFFu) != 0x7FB2u) {
        return NULL;
    }
    return rdp(w + 0x0C);
}

int ui_letters(EidosUiCtx *c, const void *bmp, int x, int y, u32 ink, u32 shadow, int *w, int *h)
{
    WinBmp b;

    if (w != NULL) *w = 0;
    if (h != NULL) *h = 0;
    if (!bmp_scan((const u8 *)bmp, &b) || b.ink < 0 || b.top >= b.bottom) {
        return 0;
    }
    if (w != NULL) *w = b.right - b.left;
    if (h != NULL) *h = b.bottom - b.top;
    if (c != NULL) {
        draw_letters(c, &b, x - b.left, y - b.top, ink, shadow);
    }
    return 1;
}

void ui_letters_dump(const void *bmp, const char *path)
{
    WinBmp b;
    FILE *f;
    int x, y;

    if (!bmp_scan((const u8 *)bmp, &b) || (f = fopen(path, "wb")) == NULL) {
        return;
    }
    fprintf(f, "P5\n%d %d\n15\n", b.w, b.h);
    for (y = 0; y < b.h; y++) {
        for (x = 0; x < b.w; x++) {
            fputc(bmp_px(b.px, b.w, x, y), f);
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* The boxes                                                           */
/* ------------------------------------------------------------------ */

/* How far (x, y) lies inside the line through a and b, on the side of
 * the point o (pixels; negative outside). */
static float side_in(float ax, float ay, float bx, float by, float ox, float oy, float x, float y)
{
    float nx = ay - by, ny = bx - ax, len = sqrtf(nx * nx + ny * ny);
    float d;

    if (len <= 0.0f) {
        return 1e9f;
    }
    d = (nx * (x - ax) + ny * (y - ay)) / len;
    return nx * (ox - ax) + ny * (oy - ay) < 0.0f ? -d : d;
}

/* The game's balloon tail (TALKMSGWIN_DrawBalloonShape): a triangle from
 * two base points (a, b) on the box to the point the game aims it at (t).
 * Its two long sides take the box's edge colour; inside the box (bx, by,
 * bw, bh) it is the box's fill, so box and tail are one shape (the game's
 * frame has a notch there). */
static void tail(EidosUiCtx *c, int bx, int by, int bw, int bh, float ax, float ay,
                 float b2x, float b2y, float tx, float ty, u32 fill, u32 edge)
{
    int x0 = (int)floorf(fminf(fminf(ax, b2x), tx)), x1 = (int)ceilf(fmaxf(fmaxf(ax, b2x), tx));
    int y0 = (int)floorf(fminf(fminf(ay, b2y), ty)), y1 = (int)ceilf(fmaxf(fmaxf(ay, b2y), ty));
    int x, y;

    for (y = y0; y <= y1; y++) {
        int run = -1;
        u32 run_col = 0;

        for (x = x0; x <= x1 + 1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            int has = 0;
            u32 col = 0;

            if (x <= x1) {
                float da = side_in(ax, ay, tx, ty, b2x, b2y, px, py);
                float db = side_in(b2x, b2y, tx, ty, ax, ay, px, py);
                float dc = side_in(ax, ay, b2x, b2y, tx, ty, px, py);
                int in_box = x >= bx && x < bx + bw && y >= by && y < by + bh;

                if (da >= -0.5f && db >= -0.5f && dc >= -0.5f) {
                    has = 1;
                    col = in_box || (da >= 1.0f && db >= 1.0f) ? fill : edge;
                }
            }
            if (run >= 0 && (!has || col != run_col)) {
                ui_fill(c, run, y, x - run, 1, run_col, 255);
                run = -1;
            }
            if (has && run < 0) {
                run = x;
                run_col = col;
            }
        }
    }
}

/* The game's tail base for a side (TALKMSGWIN_CalcTail, on the frame box
 * x, y, w, h): 16 pixels wide at the middle of the side it leaves, or 64
 * pixels in from a corner for a window whose tail keeps to one edge.
 * 0 when the side has no tail. */
static int tail_base(int side, int x, int y, int w, int h, float *ax, float *ay, float *bx, float *by)
{
    int base = w >= 16 && h >= 16 ? 16 : h;

    switch (side) {
    case 1:                             /* TALKMSGWIN_TAIL_SIDE_TOP          */
    case 2:                             /* TALKMSGWIN_TAIL_SIDE_BOTTOM       */
        *ax = (float)(x + w / 2 - base / 2);
        *bx = (float)(x + w / 2 + base / 2);
        *ay = *by = (float)(side == 1 ? y + 3 : y + h - 3);
        return 1;
    case 3:                             /* TALKMSGWIN_TAIL_SIDE_LEFT         */
    case 4:                             /* TALKMSGWIN_TAIL_SIDE_RIGHT        */
        *ax = *bx = (float)(side == 3 ? x + 1 : x + w - 1);
        *ay = (float)(y + h / 2 - base / 2);
        *by = (float)(y + h / 2 + base / 2);
        return 1;
    case 5:                             /* TALKMSGWIN_TAIL_SIDE_TOP_LEFT     */
    case 7:                             /* TALKMSGWIN_TAIL_SIDE_BOTTOM_LEFT  */
        *ax = (float)(x + 0x40);
        *bx = (float)(x + 0x50);
        *ay = *by = (float)(side == 5 ? y + 4 : y + h - 4);
        return 1;
    case 6:                             /* TALKMSGWIN_TAIL_SIDE_TOP_RIGHT    */
    case 8:                             /* TALKMSGWIN_TAIL_SIDE_BOTTOM_RIGHT */
        *ax = (float)(x + w - 0x50);
        *bx = (float)(x + w - 0x40);
        *ay = *by = (float)(side == 6 ? y + 4 : y + h - 4);
        return 1;
    default:
        return 0;
    }
}

/* The battle tiles' strip (recipe_battle.c sheet_tile): columns 3..5 down
 * the left, its top following the cut top-left corner. */
static void strip(EidosUiCtx *c, int x, int y, int h, int cut, u32 col)
{
    int r;

    for (r = 3; r < h - 3; r++) {
        int left = 3 + (r < cut + 2 ? cut + 2 - r : 0);

        if (left < 6) {
            ui_fill(c, x + left, y + r, 6 - left, 1, col, 255);
        }
    }
}

void ui_textbox_frame(EidosUiCtx *c, int kind, int x, int y, int w, int h)
{
    if (kind == UI_TEXTBOX_SYSTEM) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 7, TB_SYS, TB_SEDGE, 255);
        strip(c, x, y, h, 7, TB_HL);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 7, TB_PAPER, TB_PEDGE, 255);
    }
}

int ui_textbox_h(const void *bmpwin)
{
    WinBmp b;

    if (!win_bmp((const u8 *)bmpwin, &b) || b.ink < 0 || b.top >= b.bottom) {
        return 0;
    }
    return b.h + 10;
}

static void textbox_draw(EidosUiCtx *c, int kind, int x, int y, int w, const void *bmpwin,
                         int tail_x, int tail_y, int tail_side)
{
    WinBmp b;
    int h, tx, ty;
    u32 fill, edge, ink, shadow;

    if (!win_bmp((const u8 *)bmpwin, &b) || b.ink < 0 || b.top >= b.bottom) {
        return;                         /* nothing printed in it yet        */
    }
    h = b.h + 10;
    if (kind == UI_TEXTBOX_SYSTEM) {
        fill = TB_SYS; edge = TB_SEDGE; ink = UI_WHITE; shadow = TB_SYSSH;
    } else {
        fill = TB_PAPER; edge = TB_PEDGE; ink = TB_INK; shadow = TB_INKSH;
    }
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 7, fill, edge, 255);
    if (kind == UI_TEXTBOX_BALLOON && tail_x >= 0) {
        /* the tail over the box, so it opens the box's edge where it
         * leaves; none when the point it aims at is inside the box */
        float ax, ay, bx, by;

        if (tail_base(tail_side, x, y, w, h, &ax, &ay, &bx, &by)
            && !(tail_x >= x && tail_x < x + w && tail_y >= y && tail_y < y + h)) {
            tail(c, x, y, w, h, ax, ay, bx, by, (float)tail_x, (float)tail_y, fill, edge);
        }
    }
    if (kind == UI_TEXTBOX_SYSTEM) {
        strip(c, x, y, h, 7, TB_HL);
    }
    tx = x + (kind == UI_TEXTBOX_SYSTEM ? 10 : 8);
    ty = y + 5;
    draw_letters(c, &b, tx, ty, ink, shadow);
}

void ui_textbox_draw(EidosUiCtx *c, int kind, int x, int y, int w, const void *bmpwin,
                     int tail_x, int tail_y)
{
    (void)tail_x;
    (void)tail_y;
    textbox_draw(c, kind, x, y, w, bmpwin, -1, -1, 0);
}

/* ------------------------------------------------------------------ */
/* The field's windows                                                 */
/* ------------------------------------------------------------------ */

typedef struct FieldWin {
    const u8 *bmpwin;
    int kind;
    const u8 *talkwin;      /* its TalkMsgWin (the tail's target), or NULL */
} FieldWin;

static int field_wins(void *fm, FieldWin *out, int max, u32 *hide, const void *skip)
{
    u8 *bg, *talk;
    int n = 0, i, j;
    u32 frame, sysf;

    *hide = 0;
    if (fm == NULL || ov021_FIELDMAP_GetMsgBg == NULL || (bg = (u8 *)ov021_FIELDMAP_GetMsgBg(fm)) == NULL) {
        return 0;
    }
    frame = rd16(bg + 0x002);
    sysf = rd16(bg + 0x004);
    talk = rdp(bg + 0x15C);
    if (talk != NULL) {
        for (i = 0; i < 32 && n < max; i++) {
            const u8 *w = talk + 0x18 + 0x64 * i;
            u16 st = rd16(w);
            const u8 *bw = rdp(w + 0x0C);

            if ((st == 3 || st == 4) && bw != NULL) {
                out[n].bmpwin = bw;
                out[n].kind = w[2] ? UI_TEXTBOX_NARRATION : UI_TEXTBOX_BALLOON;
                out[n].talkwin = w;
                n++;
            }
        }
    }
    if (bw_ui_field_wins != NULL) {
        const void *wins[16];
        int m = bw_ui_field_wins(wins, 16);

        for (i = 0; i < m && n < max; i++) {
            const u8 *bw = (const u8 *)wins[i];
            int dup = 0;

            if (bw == skip || (rd16(bw) & 0x7FFFu) != 0x7FB2u) {
                continue;               /* the list's own, or not live      */
            }
            for (j = 0; j < n; j++) {
                dup |= out[j].bmpwin == bw;
            }
            if (dup) {
                continue;
            }
            out[n].bmpwin = bw;
            out[n].kind = bw[9] == 0xE ? UI_TEXTBOX_SYSTEM : UI_TEXTBOX_NARRATION;
            out[n].talkwin = NULL;
            n++;
        }
    }
    if (n > 0) {
        if (frame < 4) *hide |= 1u << frame;
        if (sysf < 4) *hide |= 1u << sysf;
        for (i = 0; i < n; i++) {
            if (out[i].bmpwin[2] < 4) *hide |= 1u << out[i].bmpwin[2];
        }
        if (talk != NULL && rd32(talk + 0x10) < 4) *hide |= 1u << rd32(talk + 0x10);
    }
    return n;
}

int ui_textbox_field_active(void *fm)
{
    FieldWin w[8];
    u32 hide;

    return field_wins(fm, w, 8, &hide, NULL) > 0;
}

int ui_textbox_field_kind(void *fm, const void *bmpwin)
{
    u8 *bg;

    if (fm == NULL || ov021_FIELDMAP_GetMsgBg == NULL || (bg = (u8 *)ov021_FIELDMAP_GetMsgBg(fm)) == NULL
        || bmpwin == NULL) {
        return UI_TEXTBOX_NARRATION;
    }
    return ((const u8 *)bmpwin)[9] == 0xE ? UI_TEXTBOX_SYSTEM : UI_TEXTBOX_NARRATION;
}

/* Boxes ui_textbox_field drew last time: a window the game keeps open with
 * nothing printed in it (it clears the text some frames before it closes
 * the window) draws no box. */
static int g_field_drawn;

int ui_textbox_field_drawn(void)
{
    return g_field_drawn;
}

u32 ui_textbox_field(EidosUiCtx *c, void *fm, float ox, float oy, float s, const void *skip)
{
    FieldWin w[8];
    u32 hide;
    int n = field_wins(fm, w, 8, &hide, skip), i;
    int cw = c->canvas->w, ch = c->canvas->h;
    const u8 *info = NULL;

    g_field_drawn = 0;

    /* the information window (the money a vending machine or a shop
     * counter shows: FIELDMAP_GetInfoWindow, a FieldMsgWin whose BmpWin is
     * at +0x00) is a card at the top right, the way the bar shows status */
    if (ov021_FIELDMAP_GetInfoWindow != NULL) {
        const u8 *iw = (const u8 *)ov021_FIELDMAP_GetInfoWindow(fm);

        info = iw != NULL ? rdp(iw) : NULL;
    }

    for (i = 0; i < n; i++) {
        const u8 *bw = w[i].bmpwin;
        int gx = (int)(ox + (float)((bw[3] - 1) * 8) * s);
        int gy = (int)(oy + (float)((bw[4] - 1) * 8) * s);
        int gw = (int)((float)((bw[5] + 2) * 8) * s);
        int bwid = bw[5] * 8 + 16;
        int x, y, tx = -1, ty = -1, side = 0;

        /* the game's place, the box as wide as the game's (not less than
         * its letters need), kept on the screen */
        if (bwid < gw) bwid = gw;
        x = gx + (gw - bwid) / 2;
        y = gy;
        if (x + bwid > cw - 4) x = cw - 4 - bwid;
        if (x < 4) x = 4;
        if (y + bw[6] * 8 + 10 > ch - 4) y = ch - 4 - (bw[6] * 8 + 10);
        if (y < 4) y = 4;
        if (bw == info) {
            x = cw - 4 - bwid;
            y = 4;
        }
        if (w[i].kind == UI_TEXTBOX_BALLOON && bw_ui_talk_target_get != NULL) {
            int px, py;

            if (w[i].talkwin != NULL && bw_ui_talk_target_get(w[i].talkwin, &px, &py, &side)) {
                tx = (int)(ox + (float)px * s);
                ty = (int)(oy + (float)py * s);
            }
        }
        textbox_draw(c, w[i].kind, x, y, bwid, bw, tx, ty, side);
        {
            WinBmp b;

            if (win_bmp(bw, &b) && b.ink >= 0 && b.top < b.bottom) {
                g_field_drawn++;            /* textbox_draw's own test */
            }
        }
    }
    return hide;
}
