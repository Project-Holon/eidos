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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "textbox.h"
#include "kit.h"

#define WEAK __attribute__((weak))

extern void *ov021_FIELDMAP_GetMsgBg(void *fieldMap) WEAK;
extern void *ov021_FIELDMAP_GetInfoWindow(void *fieldMap) WEAK;
extern int  bw_ui_talk_target_get(const void *win, int *x, int *y) WEAK;
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

/* A tail from (x0, y0) on a box's edge to (x1, y1), `half` wide at its
 * base, in the box's colours. */
static void tail(EidosUiCtx *c, int x0, int y0, int x1, int y1, int half, u32 fill, u32 edge)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (ady >= adx) {
        int steps = ady, i;
        int dir = dy > 0 ? 1 : -1;

        if (steps < 2) {
            return;
        }
        /* Erase the box's border at the base (into the box by 2px) so the tail and bubble merge */
        for (i = -2; i <= 0; i++) {
            int y = y0 + dir * i;
            ui_fill(c, x0 - half + 1, y, 2 * half - 1, 1, fill, 255);
        }
        for (i = 0; i <= steps; i++) {
            int y = y0 + dir * i;
            int cx = x0 + dx * i / steps;
            int hw = half - half * i / steps;

            ui_fill(c, cx - hw - 1, y, 2 * hw + 3, 1, UI_INK, 255);
            ui_fill(c, cx - hw, y, 2 * hw + 1, 1, edge, 255);
            if (hw > 0) {
                ui_fill(c, cx - hw + 1, y, 2 * hw - 1, 1, fill, 255);
            }
        }
    } else {
        int steps = adx, i;
        int dir = dx > 0 ? 1 : -1;

        if (steps < 2) {
            return;
        }
        /* Erase the box's border at the base (into the box by 2px) so the tail and bubble merge */
        for (i = -2; i <= 0; i++) {
            int x = x0 + dir * i;
            ui_fill(c, x, y0 - half + 1, 1, 2 * half - 1, fill, 255);
        }
        for (i = 0; i <= steps; i++) {
            int x = x0 + dir * i;
            int cy = y0 + dy * i / steps;
            int hw = half - half * i / steps;

            ui_fill(c, x, cy - hw - 1, 1, 2 * hw + 3, UI_INK, 255);
            ui_fill(c, x, cy - hw, 1, 2 * hw + 1, edge, 255);
            if (hw > 0) {
                ui_fill(c, x, cy - hw + 1, 1, 2 * hw - 1, fill, 255);
            }
        }
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

void ui_textbox_draw(EidosUiCtx *c, int kind, int x, int y, int w, const void *bmpwin,
                     int tail_x, int tail_y)
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
    if (kind == UI_TEXTBOX_BALLOON && tail_x >= 0 && tail_y >= 0) {
        int bx = tail_x;
        if (w >= 48) {
            if (bx < x + 24) bx = x + 24;
            if (bx > x + w - 24) bx = x + w - 24;
        } else {
            bx = x + w / 2;
        }

        if (tail_y >= y + h) {
            int y0 = y + h - 1;
            int dy = tail_y - y0;
            if (dy >= 6) {
                int tip_y = tail_y - 6;
                if (tip_y > y0 + 40) tip_y = y0 + 40;
                if (tip_y < y0 + 12) tip_y = y0 + 12;
                int tip_x = bx + (tail_x - bx) * (tip_y - y0) / dy;
                tail(c, bx, y0, tip_x, tip_y, 8, fill, edge);
            }
        } else if (tail_y <= y) {
            int y0 = y;
            int dy = y0 - tail_y;
            if (dy >= 6) {
                int tip_y = tail_y + 6;
                if (tip_y < y0 - 40) tip_y = y0 - 40;
                if (tip_y > y0 - 12) tip_y = y0 - 12;
                int tip_x = bx + (tail_x - bx) * (y0 - tip_y) / dy;
                tail(c, bx, y0, tip_x, tip_y, 8, fill, edge);
            }
        } else if (tail_x < x) {
            int x0 = x;
            int dx = x0 - tail_x;
            if (dx >= 6) {
                int by = tail_y;
                if (by < y + 16) by = y + 16;
                if (by > y + h - 16) by = y + h - 16;
                int tip_x = tail_x + 6;
                if (tip_x < x0 - 40) tip_x = x0 - 40;
                if (tip_x > x0 - 12) tip_x = x0 - 12;
                int tip_y = by + (tail_y - by) * (x0 - tip_x) / dx;
                tail(c, x0, by, tip_x, tip_y, 8, fill, edge);
            }
        } else if (tail_x >= x + w) {
            int x0 = x + w - 1;
            int dx = tail_x - x0;
            if (dx >= 6) {
                int by = tail_y;
                if (by < y + 16) by = y + 16;
                if (by > y + h - 16) by = y + h - 16;
                int tip_x = tail_x - 6;
                if (tip_x > x0 + 40) tip_x = x0 + 40;
                if (tip_x < x0 + 12) tip_x = x0 + 12;
                int tip_y = by + (tail_y - by) * (tip_x - x0) / dx;
                tail(c, x0, by, tip_x, tip_y, 8, fill, edge);
            }
        }
    }
    if (kind == UI_TEXTBOX_SYSTEM) {
        strip(c, x, y, h, 7, TB_HL);
    }
    tx = x + (kind == UI_TEXTBOX_SYSTEM ? 10 : 8);
    ty = y + 5;
    draw_letters(c, &b, tx, ty, ink, shadow);
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

u32 ui_textbox_field(EidosUiCtx *c, void *fm, float ox, float oy, float s, const void *skip)
{
    FieldWin w[8];
    u32 hide;
    int n = field_wins(fm, w, 8, &hide, skip), i;
    int cw = c->canvas->w, ch = c->canvas->h;
    const u8 *info = NULL;

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
        int x, y, tx = -1, ty = -1;

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

            if (w[i].talkwin != NULL && bw_ui_talk_target_get(w[i].talkwin, &px, &py)) {
                tx = (int)(ox + (float)px * s);
                ty = (int)(oy + (float)py * s);
            }
        }
        ui_textbox_draw(c, w[i].kind, x, y, bwid, bw, tx, ty);
    }
    return hide;
}
