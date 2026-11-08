/*
 * engine/ui/choice_list.c -- the game's own choice lists in the interface's
 * style (engine/ui/choice_list.h).
 *
 * BmpMenuList (arm9/bmp_menulist.c): items 0x00 (a BmpMenuListItem array:
 * a Strbuf and a value, 8 bytes each), count 0x10 (u16), rows 0x12 (u16).
 * Its scroll and row come from BMP_MENULIST_GetPos.  BmpWin
 * (arm9/bmp_win_types.h): bgLayer 0x02, posX 0x03, posY 0x04, width 0x05,
 * height 0x06, in tiles.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "choice_list.h"
#include "dsmem.h"
#include "input.h"

#define WEAK __attribute__((weak))

extern void BMP_MENULIST_GetPos(void *menu, u16 *outScroll, u16 *outRow) WEAK;
extern u16 *STRBUF_GetChars(void *strbuf) WEAK;
extern u16  STRBUF_GetLength(void *strbuf) WEAK;

#define CHOICE_MAX 32

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static u16 rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }
static u8 *rdp(const u8 *p) { return (u8 *)(uintptr_t)rd32(p); }

static int g_target = -1;           /* the row asked for: >= 0 pick it,
                                       -2 - row: only move the cursor there */
static const void *g_list;          /* the list it was asked on             */
static u32 g_key_frame;             /* the last frame a key went in         */
static int g_hover = -1;            /* the row under the pointer last frame */
static int g_px, g_py;              /* the pointer last frame               */
static int g_pick;                  /* the press went down on the cursor's
                                       row, or on a row the mouse was already
                                       over: its click picks the row        */

/* The game's 16-bit text as UTF-8; tags are left out. */
static void units_utf8(const u16 *u, int max, char *out, int n)
{
    int i, o = 0;

    for (i = 0; i < max && u[i] != 0xFFFFu && u[i] != 0 && o < n - 4; i++) {
        u32 ch = u[i];

        if (ch == 0xE000u) {
            ch = ' ';
        } else if (ch == 0xF000u) {
            int argc = i + 2 < max ? (int)u[i + 2] : 0;

            i += 2 + argc;
            continue;
        } else if (ch >= 0xD800u && ch < 0xE000u) {
            continue;
        }
        if (ch < 0x80u) {
            out[o++] = (char)ch;
        } else if (ch < 0x800u) {
            out[o++] = (char)(0xC0u | (ch >> 6));
            out[o++] = (char)(0x80u | (ch & 0x3Fu));
        } else {
            out[o++] = (char)(0xE0u | (ch >> 12));
            out[o++] = (char)(0x80u | ((ch >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (ch & 0x3Fu));
        }
    }
    out[o] = '\0';
}

/* A game string still alive: in the game's memory, with the magic the game
 * clears when it deletes one (arm9/strbuf.c, STRBUF_Delete). */
#define STRBUF_LIVE 0xB6F8D2ECu

static int live_strbuf(const u8 *sb)
{
    return sb != NULL && ((uintptr_t)sb & 3u) == 0 && bw_ds_game_ptr(sb) &&
           rd32(sb + 4) == STRBUF_LIVE;
}

/* Row i's text.  0 when the list is gone: B on a script's list frees its
 * items while the menu's event still names it for a frame (the Trainers'
 * School blackboard, #24); its strings are dead then. */
static int item_text(const u8 *list, int i, char *out, int n)
{
    u8 *items = rdp(list), *sb;

    out[0] = '\0';
    if (items == NULL || STRBUF_GetChars == NULL || STRBUF_GetLength == NULL) {
        return 1;
    }
    if (!bw_ds_game_ptr(items)) {
        return 0;
    }
    sb = rdp(items + 8 * i);
    if (sb != NULL) {
        u16 len;
        const u16 *u;

        if (!live_strbuf(sb)) {
            return 0;
        }
        len = STRBUF_GetLength(sb);
        u = STRBUF_GetChars(sb);
        if (u != NULL) {
            units_utf8(u, len, out, n);
        }
    }
    return 1;
}

void ui_choice_list_idle(void)
{
    g_target = -1;
}

#define HL 0x008BACu        /* the interface's highlight (kit UI_CYAN2) */

/* a over b at k/16 */
static u32 mix16(u32 a, u32 b, int k)
{
    u32 o = 0;
    int sh;

    for (sh = 0; sh < 24; sh += 8) {
        u32 x = (a >> sh) & 255u, y = (b >> sh) & 255u;

        o |= ((x * (u32)k + y * (u32)(16 - k)) / 16u) << sh;
    }
    return o;
}

int ui_choice_list(EidosUiCtx *c, const EidosUiFrame *in, void *list_p, const void *win_p,
                   float ox, float oy, float s, int style)
{
    const u8 *list = (const u8 *)list_p, *win = (const u8 *)win_p;
    int cw = c->canvas->w, chh = c->canvas->h;
    int count, rows, scroll, row, cur;
    int gx0, gy0, gx1, gy1, pitch, bh, w, h, x, y, i, hover = -1;
    char text[CHOICE_MAX][48];
    EidosUiRect r;

    if (list == NULL || win == NULL || BMP_MENULIST_GetPos == NULL) {
        g_target = -1;
        return 0;
    }
    count = rd16(list + 0x10);
    rows = rd16(list + 0x12);
    if (count < 1 || count > CHOICE_MAX || rows < 1) {
        g_target = -1;
        return 0;
    }
    if (rows > count) {
        rows = count;
    }
    {
        u16 sc = 0, rw = 0;

        BMP_MENULIST_GetPos((void *)list_p, &sc, &rw);
        scroll = sc;
        row = rw;
    }
    cur = scroll + row;

    /* the game's window, its frame included, in canvas pixels: the list
     * stands where the game's stood, at its right edge */
    gx1 = (int)(ox + (float)((win[3] + win[5] + 1) * 8) * s + 0.99f);
    gy0 = (int)(oy + (float)((win[4] - 1) * 8) * s);
    (void)gx0; (void)gy1;
    pitch = 18;
    bh = pitch;
    w = 0;
    for (i = 0; i < rows; i++) {
        int tw;

        if (!item_text(list, scroll + i, text[i], (int)sizeof text[i])) {
            g_target = -1;
            return 0;
        }
        tw = ui_text_w(text[i], UI_FONT_LARGE) + 30;
        if (tw > w) {
            w = tw;
        }
    }
    h = 4 + rows * pitch + 4;
    x = gx1 - w;
    y = gy0;
    if (x + w > cw - 4) x = cw - 4 - w;
    if (x < 4) x = 4;
    if (y + h > chh - 4) y = chh - 4 - h;
    if (y < 4) y = 4;

    /* the panel in the style of the box it answers (mock-ups n9, n3A) */
    if (style == UI_CHOICE_SYSTEM) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, 0x464850u, 240);
    } else if (style == UI_CHOICE_WHITE) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0xFFFFFFu, 0x9698A0u, 255);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 235);
    }
    for (i = 0; i < rows; i++) {
        r.x = x + 3; r.y = y + 4 + i * pitch; r.w = w - 6; r.h = bh;
        if (eidos_ui_hit(r) & EIDOS_UI_HOVER) {
            hover = i;
        }
    }
    {
        int px = -1, py = -1, pressed = 0, released = 0;
        int held = eidos_ui_pointer(&px, &py, &pressed, &released);
        int moved = px != g_px || py != g_py;

        if (pressed) {
            /* a finger lands where it presses; a mouse was over the row
             * before it pressed */
            g_pick = hover >= 0 && (scroll + hover == cur || (hover == g_hover && !moved));
        }
        /* the cursor follows a moving mouse; a finger moves it by a tap */
        if (hover >= 0 && !held && moved && scroll + hover != cur && g_target == -1) {
            g_target = -2 - (scroll + hover);
            g_list = list;
        }
        g_hover = hover;
        g_px = px;
        g_py = py;
    }
    for (i = 0; i < rows; i++) {
        int ry = y + 4 + i * pitch, st, lit = row == i;
        u32 ink, shadow;

        r.x = x + 3; r.y = ry; r.w = w - 6; r.h = bh;
        st = eidos_ui_hit(r);
        if (lit) {
            /* the battle sheet's chosen tile: tinted with the accent,
             * edged in it, its strip, raised a pixel */
            u32 base = style == UI_CHOICE_WHITE ? 0xFFFFFFu : UI_BODY;
            u32 tintc = style == UI_CHOICE_WHITE ? mix16(HL, base, 4) : mix16(HL, base, 5);
            int rr;

            ui_panel(c, r.x, ry - 1, r.w, bh, UI_CUT_TL | UI_CUT_BR, 5, tintc, HL, 250);
            for (rr = 3; rr < bh - 3; rr++) {
                int left = 3 + (rr < 7 ? 7 - rr : 0);

                if (left < 6) {
                    ui_fill(c, r.x + left, ry - 1 + rr, 6 - left, 1, HL, 255);
                }
            }
        }
        if (style == UI_CHOICE_WHITE) {
            ink = 0x42424Au; shadow = 0xADADB5u;
        } else {
            ink = lit ? UI_WHITE : UI_LIGHT; shadow = 0x464850u;
        }
        ui_text(c, r.x + 10, ry - (lit ? 1 : 0) + ui_text_y(bh, UI_FONT_LARGE), text[i], ink, shadow,
                UI_FONT_LARGE, 1);
        if (st & EIDOS_UI_CLICK) {
            g_target = g_pick ? scroll + i : -2 - (scroll + i);
            g_list = list;
        }
    }
    /* more rows above or below: the game scrolls its window with the keys */
    if (count > rows) {
        if (scroll > 0) {
            for (i = 0; i < 4; i++) {
                ui_fill(c, x + w / 2 - i, y + 1 + i, 2 * i + 1, 1, UI_CYAN, 255);
            }
        }
        if (scroll + rows < count) {
            for (i = 0; i < 4; i++) {
                ui_fill(c, x + w / 2 - i, y + h - 2 - i, 2 * i + 1, 1, UI_CYAN, 255);
            }
        }
        r.x = x; r.y = y; r.w = w; r.h = h;
        i = eidos_ui_wheel(r);
        if (i != 0 && g_target == -1) {
            int t = cur - i;

            t = t < 0 ? 0 : t >= count ? count - 1 : t;
            g_target = -2 - t;
            g_list = list;
        }
    }

    /* Tests: EIDOS_DEBUG_PICK="Battle,Battles for two,Confirm" picks the
     * next named entry when a list holding it opens, as a click would */
    {
        static int picked;
        const char *want = getenv("EIDOS_DEBUG_PICK");

        if (want != NULL && g_target == -1) {
            const char *p = want;
            char label[48];
            int k, n = 0;

            for (k = 0; k < picked && p != NULL; k++) {
                p = strchr(p, ',');
                p = p != NULL ? p + 1 : NULL;
            }
            while (p != NULL && p[n] != '\0' && p[n] != ',' && n < (int)sizeof label - 1) {
                label[n] = p[n];
                n++;
            }
            label[n] = '\0';
            for (k = 0; n > 0 && k < count; k++) {
                char t[48];

                if (item_text(list, k, t, (int)sizeof t) && strcmp(t, label) == 0) {
                    g_target = k;
                    g_list = list;
                    picked++;
                    fprintf(stderr, "eidos: debug pick '%s' (row %d)\n", label, k);
                    break;
                }
            }
        }
    }
    /* walk the game's cursor to the asked row, one key every other frame */
    if (g_target != -1 && g_list == list) {
        int target = g_target >= 0 ? g_target : -2 - g_target;

        if (in->frame - g_key_frame >= 2u) {
            g_key_frame = in->frame;
            if (cur < target) {
                eidos_ui_press(BW_KEY_DOWN, 1);
            } else if (cur > target) {
                eidos_ui_press(BW_KEY_UP, 1);
            } else {
                if (g_target >= 0) {
                    eidos_ui_press(BW_KEY_A, 2);
                }
                g_target = -1;
            }
        }
    } else {
        g_target = -1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The game's touch menus (APP_TASKMENU)                               */
/* ------------------------------------------------------------------ */

/*
 * TaskMenuWork (arm9/app_taskmenu.c): cursorPos 0x00 (u8), isDisableKey
 * 0x10, itemWork 0x1C (TaskMenuItem, 0x0C each: a Strbuf first), and the
 * template at 0x20: itemNum 0x24 (u8), posType 0x2C, charPosX 0x30,
 * charPosY 0x31, width 0x32, height 0x33 (characters; posType not 0: the
 * position is the bottom right).
 */
static int g_tm_focus;              /* our own row when the game takes no keys */
static const void *g_tm_menu;

int ui_taskmenu_count(const void *menu)
{
    int n = menu != NULL ? ((const u8 *)menu)[0x24] : 0;

    return n > 0 && n <= 8 ? n : 0;
}

void ui_taskmenu_tap(const void *menu_p, int i)
{
    const u8 *m = (const u8 *)menu_p;
    int n = ui_taskmenu_count(menu_p), cx, cy, w, h, px, py;

    if (n == 0 || i < 0 || i >= n) {
        return;
    }
    cx = m[0x30];
    cy = m[0x31];
    w = m[0x32] != 0 ? m[0x32] : 13;
    h = m[0x33] != 0 ? m[0x33] : 3;
    px = rd32(m + 0x2C) != 0 ? cx - w : cx;
    py = rd32(m + 0x2C) != 0 ? cy - h * n : cy;
    eidos_ui_tap(px * 8 + w * 4, py * 8 + 8 * h * i + h * 4, 3);
}

int ui_taskmenu(EidosUiCtx *c, void *menu_p, int right, int bottom, int style)
{
    return ui_taskmenu_labels(c, menu_p, right, bottom, style, NULL);
}

int ui_taskmenu_labels(EidosUiCtx *c, void *menu_p, int right, int bottom, int style,
                       const char *const *labels)
{
    const u8 *m = (const u8 *)menu_p;
    int n = ui_taskmenu_count(menu_p), i, w = 0, h, x, y, cur, keys_off, pick = -1;
    char text[8][48];
    u32 k = eidos_ui_keys_pressed();
    EidosUiRect r;

    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        u8 *items = rdp(m + 0x1C), *sb = items != NULL ? rdp(items + 0x0C * i) : NULL;
        int tw;

        text[i][0] = '\0';
        if (labels != NULL) {
            if (labels[i] != NULL) {
                strncpy(text[i], labels[i], sizeof text[i] - 1);
                text[i][sizeof text[i] - 1] = '\0';
            }
        } else if (sb != NULL && STRBUF_GetChars != NULL && STRBUF_GetLength != NULL && STRBUF_GetChars(sb) != NULL) {
            units_utf8(STRBUF_GetChars(sb), STRBUF_GetLength(sb), text[i], (int)sizeof text[i]);
        }
        tw = ui_text_w(text[i], UI_FONT_LARGE) + 30;
        if (tw > w) w = tw;
    }
    if (w < 60) w = 60;
    h = 4 + n * 18 + 4;
    x = right - w;
    y = bottom - h;
    if (menu_p != g_tm_menu) {
        g_tm_menu = menu_p;
        g_tm_focus = m[0x00] < n ? m[0x00] : 0;
    }
    /* the game moves its own cursor with the keys, unless it takes none
     * (then the keys move ours and A taps the row) */
    keys_off = rd32(m + 0x10) != 0;
    if (keys_off) {
        eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_A);
        if (k & BW_KEY_UP) g_tm_focus = (g_tm_focus + n - 1) % n;
        if (k & BW_KEY_DOWN) g_tm_focus = (g_tm_focus + 1) % n;
        if (k & BW_KEY_A) pick = g_tm_focus;
        cur = g_tm_focus;
    } else {
        cur = m[0x00];
    }
    if (style == UI_CHOICE_SYSTEM) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, 0x464850u, 240);
    } else if (style == UI_CHOICE_WHITE) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, 0xFFFFFFu, 0x9698A0u, 255);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 235);
    }
    for (i = 0; i < n; i++) {
        int ry = y + 4 + i * 18, st, lit, rr;
        u32 ink, shadow;

        r.x = x + 3; r.y = ry; r.w = w - 6; r.h = 18;
        st = eidos_ui_hit(r);
        if (st & EIDOS_UI_HOVER) {
            g_tm_focus = i;
        }
        lit = (st & EIDOS_UI_HOVER) || (eidos_ui_keys_lit() && i == cur);
        if (lit) {
            u32 base = style == UI_CHOICE_WHITE ? 0xFFFFFFu : UI_BODY;

            ui_panel(c, r.x, ry - 1, r.w, 18, UI_CUT_TL | UI_CUT_BR, 5,
                     mix16(HL, base, style == UI_CHOICE_WHITE ? 4 : 5), HL, 250);
            for (rr = 3; rr < 18 - 3; rr++) {
                int left = 3 + (rr < 7 ? 7 - rr : 0);

                if (left < 6) {
                    ui_fill(c, r.x + left, ry - 1 + rr, 6 - left, 1, HL, 255);
                }
            }
        }
        if (style == UI_CHOICE_WHITE) {
            ink = 0x42424Au; shadow = 0xADADB5u;
        } else {
            ink = lit ? UI_WHITE : UI_LIGHT; shadow = 0x464850u;
        }
        ui_text(c, r.x + 10, ry - (lit ? 1 : 0) + ui_text_y(18, UI_FONT_LARGE), text[i], ink, shadow,
                UI_FONT_LARGE, 1);
        if (st & EIDOS_UI_CLICK) {
            pick = i;
        }
    }
    if (pick >= 0) {
        ui_taskmenu_tap(menu_p, pick);
    }
    return 1;
}
