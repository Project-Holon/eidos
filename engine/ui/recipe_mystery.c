/*
 * engine/ui/recipe_mystery.c -- the one-screen recipe for Mystery Gift
 * (overlay 121: ov121/mystery.c, mystery_album.c, mystery_util.c).
 *
 * The game keeps all the logic.  Mystery Gift is one proc whose work is a
 * MysteryWork (0x69C bytes, offsets below): a task with a handler and a
 * state, a pop-up list (a MysteryUtilDetailWindow: the menu, the ways to
 * receive, the gift to choose, the info topics), the message window, a
 * yes/no list and, while it is open, the Card Album's own work.  The
 * recipe reads them and draws the screens of the mock-ups
 * (E:/uv/mystery_mock.py): a window over the game's cyan backdrop, tiles
 * for what can be chosen, the game's message in the system box under it.
 *
 * Every control becomes the key the game tests.  The lists move with UP
 * and DOWN, so a click walks the game's cursor to the row and presses A.
 * Infrared is greyed out: eidos has no infrared link yet, so its row is
 * skipped by UP and DOWN and A does nothing on it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "ui_lang.h"
#include "input.h"
#include "textbox.h"
#include "choice_list.h"

#if defined(__GNUC__) || defined(__clang__)
#define MG_WEAK __attribute__((weak))
#else
#define MG_WEAK
#endif

/* ------------------------------------------------------------------ */
/* The game                                                            */
/* ------------------------------------------------------------------ */

extern void *g_ProcSys MG_WEAK;
extern void *bw_dsref_g_ProcSys MG_WEAK;
extern void *bw_dsref__GameSystemGlobal MG_WEAK;
extern void ov121_MYSTERY_TaskReceive(void *task, int *state, void *arg) MG_WEAK;
extern void ov121_MYSTERY_TaskNetAlbum(void *task, int *state, void *arg) MG_WEAK;
extern void ov121_MYSTERY_TaskAlbum(void *task, int *state, void *arg) MG_WEAK;
extern void  MYSTERY_DATA_DecryptBuffer(u8 *buffer, u32 saveType) MG_WEAK;
/* The gifts the relay sent (sdk/mod_net.h). */
extern int   bw_mod_net_gift_count(void) MG_WEAK;
extern const char *bw_mod_net_gift_title(int i) MG_WEAK;
extern const char *bw_mod_net_gift_gifts(int i) MG_WEAK;
extern void  bw_mod_net_gift_choose(int i) MG_WEAK;
extern int   bw_mod_net_gift_chosen(void) MG_WEAK;

/* MysteryWork (ov121/mystery.c) */
#define W_ALBUM         0x008u  /* MysteryAlbumWork *, while the album is open */
#define W_SAVEMENU      0x260u  /* MysteryUtilMenu *: the yes/no list        */
#define W_DIALOG        0x270u  /* MysteryUtilDetailWindow *: the pop-up list */
#define W_RESULTS       0x274u  /* u32[4]: the lists' answers (one per kind)  */
#define W_TEXTBOX       0x284u  /* MysteryUtilMenuWindow *: the message      */
#define W_TASK          0x290u  /* MysteryUtilTask *                          */
#define W_DATA          0x3BCu  /* MysteryData *: the Card Album's cards      */
#define W_PACKET        0x3CCu  /* MysteryPacket: the gift being received     */

/* MysteryUtilTask: handler 0x00, state 0x08.  MysteryUtilDetailWindow:
 * index 0x08, count 0x0C, outIndex 0x60 (&work->dialogResult[kind]).
 * MysteryUtilMenuWindow: bmpWin 0x10.  MysteryUtilMenu: bmpWin 0x00,
 * menuList 0x10. */
#define D_INDEX         0x08u
#define D_COUNT         0x0Cu
#define D_OUT           0x60u

/* MysteryAlbumWork (ov121/mystery_album.c) */
#define A_MENUWIN       0xABCu  /* MysteryUtilMenuWindow *: its question     */
#define A_MENU          0xAC0u  /* MysteryUtilMenu *: SWAP / TRASH / BACK     */
#define A_PAGE          0xAC8u  /* int: the page of four cards               */
#define A_POS           0xACCu  /* u32: 0..3 the card on the page, 4 none    */

/* A card (0xCC bytes; the head of a Wonder Card): the gift 0x00..0x5F
 * (an item at 0x00, a Pokemon's species at 0x1A, its form at 0x1C), the
 * card's title 0x60 (37 units), the date 0xAC (u32: year << 16 | month
 * << 8 | day), the message 0xB2, the gift's kind 0xB3 (1 Pokemon, 2 item, 3
 * power), flags 0xB4 (bit 1: picked up; its message is then the second
 * set). */
#define C_ITEM          0x00u
#define C_SPECIES       0x1Au
#define C_FORM          0x1Cu
#define C_TITLE         0x60u
#define C_DATE          0xACu
#define C_MSG           0xB2u
#define C_KIND          0xB3u
#define C_FLAGS         0xB4u
#define C_SHINY         0x37u   /* 2: always shiny                          */
#define MG_P_TEXT       0xD0u   /* the gift's text (u16[253]) in the packet */

/* The Mystery Gift texts: a/0/0/2 member 80. */
#define MG_BANK         80

/* The kinds of pop-up list (MYSTERY_CreateCardMenu). */
enum { DLG_NONE = -1, DLG_MENU = 0, DLG_RECEIVE = 1, DLG_GIFT = 2, DLG_INFO = 3 };
#define RECEIVE_IR      2       /* GET VIA INFRARED in the receive list       */

static u32 rd32(const void *p, u32 off) { return *(const u32 *)((const u8 *)p + off); }
static u32 rd16(const void *p, u32 off) { return *(const u16 *)((const u8 *)p + off); }
static u32 rd8(const void *p, u32 off) { return *((const u8 *)p + off); }
static void *rdp(const void *p, u32 off) { return (void *)(uintptr_t)rd32(p, off); }

/* Mystery Gift is a proc of overlay 121: of the global ProcSys when the
 * title menu starts it, of the game system's own (GameSystem: procSys
 * 0x08) when the game does.  ProcSys: current 0x08; Proc: overlayId 0x04,
 * seq 0x08, work 0x14; the work is there once MYSTERY_Create has run (seq
 * 2: main). */
static void *mg_work_of(void *sys)
{
    void *cur = sys != NULL ? rdp(sys, 0x08) : NULL;

    if (cur == NULL || rd32(cur, 0x04) != 121u || rd32(cur, 0x08) != 2u) {
        return NULL;
    }
    return rdp(cur, 0x14);
}

static void *mg_work(void)
{
    void *w = NULL, *gs;

    if (&g_ProcSys != NULL) {
        w = mg_work_of(g_ProcSys);
    } else if (&bw_dsref_g_ProcSys != NULL && bw_dsref_g_ProcSys != NULL) {
        w = mg_work_of(*(void **)bw_dsref_g_ProcSys);
    }
    if (w == NULL && &bw_dsref__GameSystemGlobal != NULL && bw_dsref__GameSystemGlobal != NULL) {
        gs = *(void **)bw_dsref__GameSystemGlobal;
        w = gs != NULL ? mg_work_of(rdp(gs, 0x08)) : NULL;
    }
    return w;
}

/* The game's 16-bit text as UTF-8 (0xFFFF ends it; line breaks become
 * spaces; control codes are left out). */
static void units_utf8(const u8 *u, int max_units, char *out, int cap)
{
    int n = 0, i;

    for (i = 0; i < max_units; i++) {
        u32 c = rd16(u, (u32)i * 2u);

        if (c == 0xFFFFu || c == 0) {
            break;
        }
        if (c == 0xF000u) {
            i += 2 + (int)rd16(u, (u32)(i + 2) * 2u);
            continue;
        }
        if (c == 0xFFFEu || c == 0xE000u || c == 0x000Au) {
            c = ' ';
        }
        if (c < 0x80u) {
            if (n + 1 >= cap) break;
            out[n++] = (char)c;
        } else if (c < 0x800u) {
            if (n + 2 >= cap) break;
            out[n++] = (char)(0xC0u | (c >> 6));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            if (n + 3 >= cap) break;
            out[n++] = (char)(0xE0u | (c >> 12));
            out[n++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define MG_TITLE   0x62BEFFu
#define MG_MBODY   0x060E1Au
#define MG_MEDGE   0x103C5Au
#define MG_HL      0x008BACu
#define MG_SHADOW  0x313131u
#define MG_HEAD    28
#define MG_GOLD    0xFFCD4Au
#define MG_GREEN   0x96E696u
#define MG_CARD_A  0xC43C5Cu    /* the Wonder Card's face                    */
#define MG_CARD_B  0x8C2246u
#define MG_CARD_E  0xE67896u
#define MG_PAPER   0xFAEEF2u
#define MG_PINK    0xFFD2E1u

static void mg_window(EidosUiCtx *c, int x, int y, int w, int h, const char *title, const char *right)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 10, MG_MBODY, MG_MEDGE, 245);
    ui_text(c, x + 12, y + 6, title, MG_TITLE, MG_SHADOW, UI_FONT_SMALL, 1);
    if (right != NULL) {
        ui_text(c, x + w - 12 - ui_text_w(right, UI_FONT_SMALL), y + 6, right, UI_GREY, MG_SHADOW,
                UI_FONT_SMALL, 1);
    }
    ui_fill(c, x + 10, y + MG_HEAD - 4, w - 20, 1, MG_MEDGE, 255);
}

static void mg_panel(EidosUiCtx *c, int x, int y, int w, int h)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 255);
}

/* A tile: the strip down the left, the chosen one tinted and edged and
 * lifted a pixel; a dim one is drawn flat and takes no clicks.  Returns
 * the hit state; *dy gets the lift. */
static int mg_tile(EidosUiCtx *c, int x, int y, int w, int h, int lit, int dim, int *dy)
{
    EidosUiRect r;
    int st = 0, rr, lift;
    u32 col;

    r.x = x; r.y = y; r.w = w; r.h = h;
    if (!dim) {
        st = eidos_ui_hit(r);
        lit = lit || (st & EIDOS_UI_HOVER);
    } else {
        lit = 0;
    }
    lift = lit ? -1 : 0;
    col = lit ? MG_HL : UI_EDGE2;
    ui_panel(c, x, y + lift, w, h, UI_CUT_TL | UI_CUT_BR, 5, lit ? 0x05343Eu : UI_BODY,
             lit ? MG_HL : UI_EDGE, dim ? 150 : 242);
    for (rr = 3; rr < h - 3; rr++) {
        int left = 3 + (rr < 7 ? 7 - rr : 0);

        if (left < 6) {
            ui_fill(c, x + left, y + lift + rr, 6 - left, 1, dim ? UI_EDGE : col, 255);
        }
    }
    if (dy != NULL) {
        *dy = lift;
    }
    return st;
}

typedef struct MgWrap { EidosUiCtx *c; int x, y, step, font, n, maxn; u32 color; } MgWrap;

static void mg_wrap_line(void *user, const char *s, int len)
{
    MgWrap *w = (MgWrap *)user;
    char tmp[200];

    if (w->n >= w->maxn) {
        return;
    }
    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    ui_text(w->c, w->x, w->y + w->n * w->step, tmp, w->color, MG_SHADOW, w->font, 1);
    w->n++;
}

/* Wrapped text (line breaks of the game's text become spaces). */
static int mg_wrapped(EidosUiCtx *c, int x, int y, int width, int step, int font, const char *text,
                      u32 color, int maxn)
{
    char flat[700];
    int n = 0;
    MgWrap w;

    for (; *text != '\0' && n < (int)sizeof flat - 1; text++) {
        flat[n++] = *text == '\n' ? ' ' : *text;
    }
    flat[n] = '\0';
    w.c = c; w.x = x; w.y = y; w.step = step; w.font = font; w.n = 0; w.maxn = maxn; w.color = color;
    ui_wrap(flat, width, font, mg_wrap_line, &w);
    return w.n;
}

/* The game's cyan backdrop, filling the window, darkened under the
 * interface.  It is the upper screen while the menu is up; the album and
 * the fades change that screen, so the last good picture is kept. */
static u8 g_back[256 * 192 * 4];
static int g_back_have;

static void mg_backdrop(EidosUiCtx *c, const EidosUiFrame *in, int cw, int ch, int take)
{
    const u8 *scr;
    int k, s;

    /* the backdrop is the cyan screen (the game swaps which one that is):
     * green and blue well above red, and bright enough not to be in a
     * fade */
    for (s = 0; take && s < 2; s++) {
        long cyan = 0;
        int i, n = 0;

        scr = in->screen_rgba[s];
        if (scr == NULL) {
            continue;
        }
        for (i = 0; i < 256 * 192; i += 97, n++) {
            cyan += (scr[i * 4 + 1] + scr[i * 4 + 2]) / 2 - scr[i * 4];
        }
        if (cyan / n > 60) {
            memcpy(g_back, scr, sizeof g_back);
            g_back_have = 1;
            break;
        }
    }
    ui_fill(c, 0, 0, cw, ch, 0x0A2A33u, 255);
    scr = g_back_have ? g_back : NULL;
    if (scr != NULL) {
        k = (cw + 255) / 256;
        if ((ch + 191) / 192 > k) {
            k = (ch + 191) / 192;
        }
        ui_blit_screen(c, (cw - 256 * k) / 2, (ch - 192 * k) / 2, scr, 0, 0, 256, 192, k, 0);
    }
    ui_fill(c, 0, 0, cw, ch, 0x000000u, 130);
}

/* The game's 16-bit text drawn line by line: 0xFFFE (and 0xE000, 0x000A)
 * break the line, 0xFFFF ends it, control codes are left out.  At most
 * `maxn` lines of the small font. */
static void mg_text_lines(EidosUiCtx *c, int x, int y, const u8 *u, int max_units, u32 color, int maxn)
{
    char line[200];
    int n = 0, lines = 0, i;

    for (i = 0; i <= max_units && lines < maxn; i++) {
        u32 ch = i < max_units ? rd16(u, (u32)i * 2u) : 0xFFFFu;

        if (ch == 0xF000u && i + 2 < max_units) {
            i += 2 + (int)rd16(u, (u32)(i + 2) * 2u);
            continue;
        }
        if (ch == 0xFFFFu || ch == 0 || ch == 0xFFFEu || ch == 0xE000u || ch == 0x000Au) {
            line[n] = '\0';
            if (n > 0 || (ch != 0xFFFFu && ch != 0)) {
                ui_text(c, x, y + lines * 14, line, color, MG_SHADOW, UI_FONT_SMALL, 1);
                lines++;
            }
            n = 0;
            if (ch == 0xFFFFu || ch == 0) {
                break;
            }
            continue;
        }
        if (ch < 0x80u) {
            if (n < (int)sizeof line - 1) line[n++] = (char)ch;
        } else if (ch < 0x800u) {
            if (n < (int)sizeof line - 2) {
                line[n++] = (char)(0xC0u | (ch >> 6));
                line[n++] = (char)(0x80u | (ch & 0x3Fu));
            }
        } else if (n < (int)sizeof line - 3) {
            line[n++] = (char)(0xE0u | (ch >> 12));
            line[n++] = (char)(0x80u | ((ch >> 6) & 0x3Fu));
            line[n++] = (char)(0x80u | (ch & 0x3Fu));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Cards                                                               */
/* ------------------------------------------------------------------ */

typedef struct MgCard {
    int slot;
    const u8 *raw;
    char title[80];
    int kind;               /* 1 Pokemon, 2 item, 3 power                */
    int species, form, item;
    int picked;             /* the gift was picked up                    */
    int year, month, day;
} MgCard;

/* The cards are kept encrypted in memory (arm9/mystery_data.c): the
 * MysteryData's buffer (0x00) of 0xA94 bytes (saveType 0x04 is 1; 0x368
 * when 0) XORed with a key from its footer (u16 checksum, u16 seed at the
 * end; seed 0 while the game has it decrypted).  The recipe decrypts its
 * own copy with the game's MYSTERY_DATA_DecryptBuffer, never the game's
 * buffer.  Cards are at 0x100 + slot * 0xCC. */
static u8 g_cards[0xA94];
static u32 g_cards_frame = 0xFFFFFFFFu;

static const u8 *mg_cards(void *data, int *max)
{
    const EidosUiFrame *in = eidos_ui_frame_in();
    u32 type, size;
    const u8 *buf;

    *max = 0;
    if (data == NULL || MYSTERY_DATA_DecryptBuffer == NULL) {
        return NULL;
    }
    buf = (const u8 *)rdp(data, 0x00);
    type = rd32(data, 0x04);
    if (buf == NULL || type > 1u) {
        return NULL;
    }
    size = type == 1u ? 0xA94u : 0x368u;
    *max = type == 1u ? 12 : 3;
    if (in == NULL || in->frame != g_cards_frame) {
        memcpy(g_cards, buf, size);
        if (rd16(g_cards, size - 2u) != 0) {
            MYSTERY_DATA_DecryptBuffer(g_cards, type);
        }
        g_cards_frame = in != NULL ? in->frame : 0xFFFFFFFFu;
    }
    return g_cards;
}

static int mg_card(void *data, int slot, MgCard *out)
{
    const u8 *raw, *all;
    int max;

    all = mg_cards(data, &max);
    if (all == NULL || slot < 0 || slot >= max) {
        return 0;
    }
    raw = all + 0x100 + (u32)slot * 0xCCu;
    if (rd8(raw, C_KIND) < 1 || rd8(raw, C_KIND) > 3) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->slot = slot;
    out->raw = raw;
    units_utf8(raw + C_TITLE, 37, out->title, (int)sizeof out->title);
    out->kind = (int)rd8(raw, C_KIND);
    out->species = (int)rd16(raw, C_SPECIES);
    out->form = (int)rd8(raw, C_FORM);
    out->item = (int)rd16(raw, C_ITEM);
    out->picked = (rd8(raw, C_FLAGS) & 2u) != 0;   /* picked up from the deliveryman */
    /* the date the game wrote when the card came: (year << 16) |
     * (month << 8) | day */
    out->year = (int)rd16(raw, C_DATE + 2);
    out->month = (int)rd8(raw, C_DATE + 1);
    out->day = (int)rd8(raw, C_DATE);
    return 1;
}

static const UiImage *mg_card_icon(const MgCard *cd)
{
    if (cd->kind == 1 && cd->species > 0 && cd->species < 700) {
        return ui_mon_icon((u16)cd->species, (u8)cd->form, 0);
    }
    if (cd->kind == 2 && cd->item > 0 && cd->item < 0x280) {
        return ui_item_icon((u16)cd->item);
    }
    return NULL;
}

/* The card's message (a/0/0/2 member 80, 157 + index or 413 + index). */
static const char *mg_card_message(const MgCard *cd)
{
    u32 idx = rd8(cd->raw, C_MSG) + ((rd8(cd->raw, C_FLAGS) & 2u) ? 413u : 157u);

    return ui_game_text(2, MG_BANK, idx);
}

static void mg_date(const MgCard *cd, char *out, int cap)
{
    if (cd->year > 0) {
        snprintf(out, (size_t)cap, "%02d/%02d/%04d", cd->month, cd->day, cd->year);
    } else {
        out[0] = '\0';
    }
}

/* Small text cut at `w` pixels (a card's title on a narrow tile). */
static void mg_clipped_text(EidosUiCtx *c, int x, int y, int w, const char *s, u32 color)
{
    EidosUiRect saved;

    ui_push(c, 0, 0, &saved);
    ui_clip(c, x, y - 2, w, 16);
    ui_text(c, x, y, s, color, MG_SHADOW, UI_FONT_SMALL, 1);
    ui_pop(c, 0, 0, &saved);
}

/* A slot of the album: the card's colour, its gift and title; an empty
 * slot is a dotted dark tile. */
static int mg_mini_card(EidosUiCtx *c, int x, int y, int w, int h, const MgCard *cd, int lit)
{
    EidosUiRect r;
    int st;
    const UiImage *ic;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    if (cd == NULL) {
        int dx;

        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, lit ? MG_HL : UI_EDGE, 200);
        for (dx = 8; dx < w - 8; dx += 4) {
            ui_fill(c, x + dx, y + h / 2, 1, 1, UI_EDGE2, 255);
        }
        return st;
    }
    lit = lit || (st & EIDOS_UI_HOVER);
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, MG_CARD_B, lit ? MG_HL : UI_EDGE2, 255);
    ic = mg_card_icon(cd);
    if (h < 36) {
        /* a strip: the gift on the left, the title beside it */
        ui_fill(c, x + 3, y + 3, 24, h - 6, MG_CARD_A, 255);
        if (ic != NULL) {
            ui_image(c, x + 3 + (24 - ic->w / 2) / 2 - ic->w / 4, y + (h - ic->h) / 2, ic, 1);
        }
        mg_clipped_text(c, x + 31, y + ui_text_y(h, UI_FONT_SMALL), w - 31 - 4, cd->title,
                        lit ? UI_WHITE : UI_LIGHT);
        if (!cd->picked) {
            ui_fill(c, x + 20, y + 3, 7, 3, MG_GOLD, 255);
        }
        return st;
    }
    ui_fill(c, x + 3, y + 3, w - 6, h - 17, MG_CARD_A, 255);
    if (ic != NULL) {
        ui_image(c, x + 4, y + (h - 14 - ic->h) / 2 + 2, ic, 1);
    }
    ui_fill(c, x + 2, y + h - 14, w - 4, 12, UI_BODY2, 255);
    mg_clipped_text(c, x + 5, y + h - 14, w - 10, cd->title, lit ? UI_WHITE : UI_LIGHT);
    if (!cd->picked) {
        ui_fill(c, x + w - 12, y + 4, 8, 6, MG_GOLD, 255);
        ui_fill(c, x + w - 9, y + 4, 2, 6, MG_CARD_A, 255);
    }
    return st;
}

/* The card itself, large: its gift, whether it was picked up, the event's
 * message, the date. */
static void mg_wonder_card(EidosUiCtx *c, int x, int y, int w, int h, const MgCard *cd)
{
    const UiImage *ic = mg_card_icon(cd);
    char date[32];
    int wy, ww = 64, wh = 60, tx, msg_h;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 9, MG_CARD_B, MG_CARD_E, 255);
    ui_fill(c, x + 3, y + 3, w - 6, 18, MG_CARD_A, 255);
    ui_text(c, x + 12, y + 5, "WONDER CARD", UI_WHITE, MG_CARD_B, UI_FONT_SMALL, 1);
    mg_date(cd, date, (int)sizeof date);
    ui_text(c, x + w - 12 - ui_text_w(date, UI_FONT_SMALL), y + 5, date, MG_PINK, MG_CARD_B, UI_FONT_SMALL, 1);
    wy = y + 28;
    ui_panel(c, x + 10, wy, ww, wh, UI_CUT_TL | UI_CUT_BR, 5, MG_MBODY, MG_CARD_E, 255);
    if (ic != NULL) {
        ui_image(c, x + 10 + (ww - ic->w * 2) / 2, wy + (wh - ic->h * 2) / 2, ic, 2);
    }
    tx = x + 10 + ww + 10;
    mg_wrapped(c, tx, wy + 1, x + w - 10 - tx, 14, UI_FONT_LARGE, cd->title, UI_WHITE, 2);
    ui_text(c, tx, wy + 30, cd->kind == 1 ? "Pok\xC3\xA9mon" : cd->kind == 2 ? "Item" : "Pass Power",
            MG_PINK, MG_CARD_B, UI_FONT_SMALL, 1);
    {
        const char *st = cd->picked ? "Picked up" : "Not picked up yet";
        int beside = tx + 60 + ui_text_w(st, UI_FONT_SMALL) <= x + w - 10;

        ui_text(c, beside ? tx + 60 : tx, beside ? wy + 30 : wy + 44, st, cd->picked ? MG_GREEN : MG_GOLD,
                MG_CARD_B, UI_FONT_SMALL, 1);
    }
    msg_h = h - (wy + wh + 8 - y) - 10;
    if (msg_h > 30) {
        ui_panel(c, x + 10, wy + wh + 8, w - 20, msg_h, UI_CUT_TL | UI_CUT_BR, 5, MG_PAPER, MG_CARD_E, 255);
        {
            char flat[600];
            const char *s = mg_card_message(cd);
            int n = 0;

            for (; *s != '\0' && n < (int)sizeof flat - 1; s++) {
                flat[n++] = *s == '\n' ? ' ' : *s;
            }
            flat[n] = '\0';
            /* dark words on paper, wrapped by hand (the kit's wrapper
             * draws light words with a dark shadow) */
            {
                int lines = 0;
                const char *p = flat;
                char line[120];

                while (*p != '\0' && lines < (msg_h - 8) / 13) {
                    int len = 0, last_space = -1, wpx;

                    while (p[len] != '\0') {
                        char tmp[120];
                        int l2 = len + 1 < (int)sizeof tmp ? len + 1 : (int)sizeof tmp - 1;

                        memcpy(tmp, p, (size_t)l2);
                        tmp[l2] = '\0';
                        wpx = ui_text_w(tmp, UI_FONT_SMALL);
                        if (wpx > w - 36) {
                            break;
                        }
                        if (p[len] == ' ') {
                            last_space = len;
                        }
                        len++;
                    }
                    if (p[len] != '\0' && last_space > 0) {
                        len = last_space;
                    }
                    if (len <= 0) {
                        break;
                    }
                    if (len > (int)sizeof line - 1) {
                        len = (int)sizeof line - 1;
                    }
                    memcpy(line, p, (size_t)len);
                    line[len] = '\0';
                    ui_text(c, x + 18, wy + wh + 12 + lines * 13, line, 0x3C3C3Cu, 0xC8BEC4u, UI_FONT_SMALL, 1);
                    lines++;
                    p += len;
                    while (*p == ' ') {
                        p++;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The cursor walk                                                     */
/* ------------------------------------------------------------------ */

/* A click names a row (or an album slot); the recipe presses the game's
 * keys one step every few frames until the cursor is there, then A. */
static int g_target = -1;
static int g_target_kind = -2;
static int g_wait;

static void mg_walk_list(int kind, int index, int count)
{
    if (g_target < 0 || g_target_kind != kind) {
        g_target = -1;
        return;
    }
    if (g_wait > 0) {
        g_wait--;
        return;
    }
    if (g_target >= count) {
        g_target = -1;
    } else if (index < g_target) {
        eidos_ui_press(BW_KEY_DOWN, 1);
        g_wait = 5;
    } else if (index > g_target) {
        eidos_ui_press(BW_KEY_UP, 1);
        g_wait = 5;
    } else {
        eidos_ui_press(BW_KEY_A, 2);
        g_target = -1;
    }
}

/* The album: pages of four (0 top left, 1 top right, 2 bottom left,
 * 3 bottom right); LEFT and RIGHT cross to the next page at the edges. */
static void mg_walk_album(int page, int pos)
{
    int tp, tpos, cur, tgt;

    if (g_target < 0 || g_target_kind != 100) {
        return;
    }
    if (g_wait > 0) {
        g_wait--;
        return;
    }
    tp = g_target / 4;
    tpos = g_target % 4;
    if (pos > 3) {
        eidos_ui_press(BW_KEY_UP, 1);       /* off the cards: back onto them */
        g_wait = 6;
        return;
    }
    cur = page * 2 + (pos & 1);             /* the column across all pages */
    tgt = tp * 2 + (tpos & 1);
    if (cur < tgt) {
        eidos_ui_press(BW_KEY_RIGHT, 1);
    } else if (cur > tgt) {
        eidos_ui_press(BW_KEY_LEFT, 1);
    } else if ((pos >> 1) != (tpos >> 1)) {
        eidos_ui_press((pos >> 1) < (tpos >> 1) ? BW_KEY_DOWN : BW_KEY_UP, 1);
    } else {
        if (g_target_kind == 100 && g_wait == 0 && g_target >= 0) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        g_target = -1;
        return;
    }
    g_wait = 8;
}

/* ------------------------------------------------------------------ */
/* The screens                                                         */
/* ------------------------------------------------------------------ */

static const char *mg_text(u32 id)
{
    return ui_game_text(2, MG_BANK, id);
}

/* The message window and the yes/no beside it, under a window ending at
 * `bottom`; returns the message box's top (or bottom + 4). */
static int mg_message(EidosUiCtx *c, const EidosUiFrame *in, void *work, void *textbox_win, void *menu,
                      int mx, int mw, int bottom)
{
    int bh = textbox_win != NULL ? ui_textbox_h(textbox_win) : 0, top = bottom + 4;

    if (bh > 0) {
        ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, top, mw, textbox_win, -1, -1);
    }
    (void)work;
    if (menu != NULL && rdp(menu, 0x10) != NULL && rdp(menu, 0x00) != NULL) {
        const u8 *win = (const u8 *)rdp(menu, 0x00);
        int count = (int)rd16(rdp(menu, 0x10), 0x10), lh = 8 + (count > 0 ? count : 2) * 18;
        float ox = (float)(mx + mw) - (float)((win[3] + win[5] + 1) * 8);
        float oy = (float)(top - lh - 4) - (float)((win[4] - 1) * 8);

        ui_choice_list(c, in, rdp(menu, 0x10), win, ox, oy, 1.0f, UI_CHOICE_SYSTEM);
    } else {
        ui_choice_list_idle();
    }
    return top;
}

/* Draws the newest card's preview in the menu (the last card held). */
static void mg_newest(EidosUiCtx *c, void *data, int x, int y, int w, int h)
{
    MgCard cd, last;
    int i, have = 0;
    char line[48];

    mg_panel(c, x, y, w, h);
    ui_text(c, x + 8, y + 5, "NEWEST CARD", MG_TITLE, MG_SHADOW, UI_FONT_SMALL, 1);
    for (i = 0; i < 12; i++) {
        if (mg_card(data, i, &cd)) {
            last = cd;
            have = 1;
        }
    }
    if (!have) {
        mg_wrapped(c, x + 8, y + 24, w - 16, 13, UI_FONT_SMALL,
                   "No cards yet. Received gifts show up here.", UI_GREY, 4);
        return;
    }
    mg_mini_card(c, x + 8, y + 20, w - 16, 44, &last, 1);
    mg_date(&last, line, (int)sizeof line);
    if (line[0] != '\0') {
        char rec[64];

        snprintf(rec, sizeof rec, ui_trf("Received %s"), line);
        ui_text(c, x + 8, y + 70, rec, UI_LIGHT, MG_SHADOW, UI_FONT_SMALL, 1);
    }
    if (!last.picked) {
        mg_wrapped(c, x + 8, y + 86, w - 16, 13, UI_FONT_SMALL,
                   "Pick it up from the deliveryman in any Pok\xC3\xA9mon Center.", MG_GOLD, 3);
    } else {
        ui_text(c, x + 8, y + 86, "Picked up", MG_GREEN, MG_SHADOW, UI_FONT_SMALL, 1);
    }
}

static int mg_card_count(void *data, int *waiting)
{
    MgCard cd;
    int i, n = 0;

    *waiting = 0;
    for (i = 0; i < 12; i++) {
        if (mg_card(data, i, &cd)) {
            n++;
            if (!cd.picked) {
                (*waiting)++;
            }
        }
    }
    return n;
}

/* The pop-up lists: the menu, the ways to receive, the gift, the info. */
static void mg_lists(EidosUiCtx *c, const EidosUiFrame *in, void *work, int kind, void *dlg)
{
    int cw = c->canvas->w, ch = c->canvas->h;
    int index = (int)rd32(dlg, D_INDEX), count = (int)rd32(dlg, D_COUNT);
    void *data = rdp(work, W_DATA);
    int mw = cw - 24 < 420 ? cw - 24 : 420, mh, mx, my, i, dy, st;
    int waiting = 0, cards = mg_card_count(data, &waiting);
    char right[32], sub[48];
    const char *title = kind == DLG_MENU ? "MYSTERY GIFT" : kind == DLG_INFO ? "INFO" : "RECEIVE GIFT";
    void *tb = rdp(work, W_TEXTBOX);

    mh = kind == DLG_RECEIVE ? 120 : kind == DLG_GIFT ? 150 : 168;
    if (mh > ch - 17 - 60) {
        mh = ch - 17 - 60;
    }
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh - 50) / 2;
    if (my < 4) {
        my = 4;
    }
    snprintf(right, sizeof right, ui_trf("Cards %d/12"), cards);
    mg_window(c, mx, my, mw, mh, title, kind == DLG_MENU ? right : NULL);

    if (kind == DLG_MENU || kind == DLG_INFO) {
        int lw = kind == DLG_MENU ? mw / 2 - 8 : 160, rows = count < 4 ? count : 4;

        for (i = 0; i < rows; i++) {
            static const u32 menu_ids[4] = { 80, 81, 131, 82 };
            static const u32 info_ids[4] = { 127, 128, 129, 130 };
            u32 id = kind == DLG_MENU ? menu_ids[i] : info_ids[i];
            int last = i == rows - 1, th = kind == DLG_MENU && !last ? 30 : 24;
            int ty = my + 26 + (kind == DLG_MENU ? i * 34 : i * 28) + (last ? 2 : 0);
            int tw = last ? lw / 2 : lw;

            st = mg_tile(c, mx + 10, ty, tw, th, i == index && eidos_ui_keys_lit(), 0, &dy);
            if (kind == DLG_MENU && !last) {
                const char *subs[3];
                char c1[32], c2[48];

                /* each count with its own words, singular or plural, so a
                 * translation has the whole phrase (ui_trf) */
                snprintf(c1, sizeof c1, cards == 1 ? ui_trf("%d card") : ui_trf("%d cards"), cards);
                snprintf(c2, sizeof c2, waiting == 1 ? ui_trf("%d gift waiting") : ui_trf("%d gifts waiting"),
                         waiting);
                snprintf(sub, sizeof sub, ui_trf("%s, %s"), c1, c2);
                subs[0] = "Wireless or internet";
                subs[1] = sub;
                subs[2] = "How Mystery Gift works";
                ui_text(c, mx + 22, ty + dy + 2, mg_text(id), UI_WHITE, MG_SHADOW, UI_FONT_LARGE, 1);
                ui_text(c, mx + 22, ty + dy + 16, subs[i], i == index ? UI_LIGHT : UI_GREY, MG_SHADOW,
                        UI_FONT_SMALL, 1);
            } else {
                ui_text(c, mx + 22, ty + dy + ui_text_y(th, UI_FONT_LARGE), mg_text(id), UI_WHITE, MG_SHADOW,
                        UI_FONT_LARGE, 1);
            }
            if (st & EIDOS_UI_CLICK) {
                g_target = i;
                g_target_kind = kind;
                g_wait = 0;
            }
        }
        if (kind == DLG_MENU) {
            mg_newest(c, data, mx + 10 + lw + 8, my + 26, mw - lw - 28, mh - 36);
        } else {
            /* the topic under the cursor, read ahead */
            int rx = mx + 10 + lw + 10, rw2 = mw - lw - 30;

            mg_panel(c, rx, my + 26, rw2, mh - 36);
            if (index >= 0 && index < 3) {
                mg_wrapped(c, rx + 8, my + 32, rw2 - 16, 13, UI_FONT_SMALL, mg_text(132u + (u32)index),
                           UI_LIGHT, (mh - 46) / 13);
            }
        }
    } else if (kind == DLG_RECEIVE) {
        static const char *const subs[3] = { "Sent nearby", "Over the internet", "Not available" };
        int tw = (mw - 20 - 12) / 3;

        for (i = 0; i < 3 && i < count; i++) {
            int tx = mx + 10 + i * (tw + 6), dim = i == RECEIVE_IR;
            const char *lab = mg_text(87u + (u32)i);

            st = mg_tile(c, tx, my + 26, tw, 62, i == index && eidos_ui_keys_lit(), dim, &dy);
            ui_text(c, tx + (tw - ui_text_w(lab, UI_FONT_SMALL)) / 2, my + 26 + dy + 30, lab,
                    dim ? UI_GREY : UI_WHITE, MG_SHADOW, UI_FONT_SMALL, 1);
            ui_text(c, tx + (tw - ui_text_w(subs[i], UI_FONT_SMALL)) / 2, my + 26 + dy + 44, subs[i],
                    dim ? UI_EDGE2 : UI_GREY, MG_SHADOW, UI_FONT_SMALL, 1);
            /* a mark for each way: waves, a globe, the infrared arrows */
            {
                int cx = tx + tw / 2, cy = my + 26 + dy + 14;
                u32 col = dim ? UI_EDGE2 : UI_LIGHT;

                if (i == 0) {
                    ui_ellipse(c, cx - 3, cy + 2, 6, 6, col, col, 255);
                    ui_line(c, cx - 8, cy - 2, cx, cy - 6, col, 255);
                    ui_line(c, cx, cy - 6, cx + 8, cy - 2, col, 255);
                } else if (i == 1) {
                    ui_ellipse(c, cx - 8, cy - 8, 16, 16, 0x000000u, col, 255);
                    ui_line(c, cx - 8, cy, cx + 8, cy, col, 255);
                    ui_line(c, cx, cy - 8, cx, cy + 8, col, 255);
                } else {
                    int j;

                    for (j = 0; j < 3; j++) {
                        ui_line(c, cx - 7 + j * 5, cy - 6, cx - 2 + j * 5, cy, col, 255);
                        ui_line(c, cx - 2 + j * 5, cy, cx - 7 + j * 5, cy + 6, col, 255);
                    }
                }
            }
            if ((st & EIDOS_UI_CLICK) && !dim) {
                g_target = i;
                g_target_kind = kind;
                g_wait = 0;
            }
        }
        if (count > 3) {
            st = mg_tile(c, mx + 10, my + 94, 80, 20, index == 3 && eidos_ui_keys_lit(), 0, &dy);
            ui_text(c, mx + 22, my + 94 + dy + ui_text_y(20, UI_FONT_LARGE), mg_text(90), UI_WHITE, MG_SHADOW,
                    UI_FONT_LARGE, 1);
            if (st & EIDOS_UI_CLICK) {
                g_target = 3;
                g_target_kind = kind;
                g_wait = 0;
            }
        }
        /* UP and DOWN step over infrared; A does nothing on it */
        {
            u32 keys = eidos_ui_keys_pressed();

            eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | (index == RECEIVE_IR ? BW_KEY_A : 0u));
            if (g_target < 0 && (keys & BW_KEY_DOWN) && index < count - 1) {
                g_target = index + 1 == RECEIVE_IR ? index + 2 : index + 1;
                g_target_kind = 1000 + kind;      /* move only, no A */
                g_wait = 0;
            } else if (g_target < 0 && (keys & BW_KEY_UP) && index > 0) {
                g_target = index - 1 == RECEIVE_IR ? index - 2 : index - 1;
                g_target_kind = 1000 + kind;
                g_wait = 0;
            }
        }
    } else if (kind == DLG_GIFT) {
        const u8 *pk = (const u8 *)work + W_PACKET;
        char name[80];
        MgCard g;

        memset(&g, 0, sizeof g);
        g.raw = pk;
        g.kind = (int)rd8(pk, C_KIND);
        g.species = (int)rd16(pk, C_SPECIES);
        g.form = (int)rd8(pk, C_FORM);
        g.item = (int)rd16(pk, C_ITEM);
        units_utf8(pk + C_TITLE, 37, name, (int)sizeof name);
        st = mg_tile(c, mx + 10, my + 26, mw - 20, 34, eidos_ui_keys_lit(), 0, &dy);
        {
            const UiImage *ic = mg_card_icon(&g);

            if (ic != NULL) {
                ui_image(c, mx + 18, my + 26 + dy + (34 - ic->h) / 2, ic, 1);
            }
        }
        ui_text(c, mx + 56, my + 26 + dy + ui_text_y(34, UI_FONT_LARGE), name, UI_WHITE, MG_SHADOW,
                UI_FONT_LARGE, 1);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        /* the card's text (the gift's details), its own line breaks */
        mg_panel(c, mx + 10, my + 66, mw - 20, mh - 76);
        mg_text_lines(c, mx + 18, my + 72, pk + MG_P_TEXT, 253, UI_LIGHT, (mh - 84) / 14);
    }

    /* the walk: a move-only target (the keys over infrared) or a click */
    if (g_target_kind == 1000 + kind && g_target >= 0) {
        if (g_wait > 0) {
            g_wait--;
        } else if (index < g_target) {
            eidos_ui_press(BW_KEY_DOWN, 1);
            g_wait = 4;
        } else if (index > g_target) {
            eidos_ui_press(BW_KEY_UP, 1);
            g_wait = 4;
        } else {
            g_target = -1;
        }
    } else {
        mg_walk_list(kind, index, count);
    }
    mg_message(c, in, work, tb != NULL ? rdp(tb, 0x10) : NULL, rdp(work, W_SAVEMENU), mx, mw, my + mh);
}

/* More than one gift up today (sdk/mod_net.c, bw_mod_net_gift_*): the
 * game's search waits until the player picks one, here.  UP and DOWN move
 * the list's own cursor, A or a click picks; B is the game's (it cancels
 * the search).  Returns 1 when the list was drawn. */
static int g_pick_cursor;

static int mg_pick_list(EidosUiCtx *c, const EidosUiFrame *in, void *work)
{
    int cw = c->canvas->w, ch = c->canvas->h, n, i, rows, mw, mh, mx, my, dy, st, first;
    u32 keys;

    if (bw_mod_net_gift_count == NULL || bw_mod_net_gift_chosen == NULL) {
        return 0;
    }
    n = bw_mod_net_gift_count();
    if (n < 2 || bw_mod_net_gift_chosen() >= 0) {
        g_pick_cursor = 0;
        return 0;
    }
    if (g_pick_cursor >= n) {
        g_pick_cursor = n - 1;
    }
    rows = n < 5 ? n : 5;
    mw = cw - 24 < 380 ? cw - 24 : 380;
    mh = 30 + rows * 34;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh - 50) / 2;
    if (my < 4) {
        my = 4;
    }
    mg_window(c, mx, my, mw, mh, "CHOOSE A GIFT", NULL);
    first = g_pick_cursor >= rows ? g_pick_cursor - rows + 1 : 0;
    for (i = 0; i < rows && first + i < n; i++) {
        int k = first + i, ty = my + 26 + i * 34;

        st = mg_tile(c, mx + 10, ty, mw - 20, 30, k == g_pick_cursor && eidos_ui_keys_lit(), 0, &dy);
        ui_text(c, mx + 22, ty + dy + 2, bw_mod_net_gift_title(k), UI_WHITE, MG_SHADOW, UI_FONT_LARGE, 1);
        ui_text(c, mx + 22, ty + dy + 16, bw_mod_net_gift_gifts(k), k == g_pick_cursor ? UI_LIGHT : UI_GREY,
                MG_SHADOW, UI_FONT_SMALL, 1);
        if (st & EIDOS_UI_HOVER) {
            g_pick_cursor = k;
        }
        if (st & EIDOS_UI_CLICK) {
            bw_mod_net_gift_choose(k);
        }
    }
    keys = eidos_ui_keys_pressed();
    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_A);
    if ((keys & BW_KEY_DOWN) && g_pick_cursor < n - 1) {
        g_pick_cursor++;
    } else if ((keys & BW_KEY_UP) && g_pick_cursor > 0) {
        g_pick_cursor--;
    } else if (keys & BW_KEY_A) {
        bw_mod_net_gift_choose(g_pick_cursor);
    }
    {
        static const char *const lines[1] = { "More than one gift is here today. Choose one." };
        int top = my + mh + 4;

        ui_textbox_frame(c, UI_TEXTBOX_SYSTEM, mx, top, mw, 30);
        ui_text(c, mx + 12, top + 7, lines[0], UI_WHITE, 0x464850u, UI_FONT_LARGE, 1);
    }
    (void)in;
    (void)work;
    return 1;
}

/* No list: the message alone (with the search's pulse while receiving). */
static void mg_only_message(EidosUiCtx *c, const EidosUiFrame *in, void *work, int searching)
{
    int cw = c->canvas->w, ch = c->canvas->h, mw = cw - 24 < 360 ? cw - 24 : 360;
    int mx = (cw - mw) / 2, mh = 96, my = (ch - 17 - mh - 50) / 2;
    void *tb = rdp(work, W_TEXTBOX);

    if (searching && mg_pick_list(c, in, work)) {
        return;
    }
    if (searching) {
        int cx, cy, r, phase = (int)(eidos_ui_scene_age() % 60u);

        mw = 220;
        mx = (cw - mw) / 2;
        mg_window(c, mx, my, mw, mh, "RECEIVE GIFT", NULL);
        cx = mx + mw / 2;
        cy = my + 60;
        for (r = 8; r <= 32; r += 8) {
            int rr = r + phase / 15;

            ui_ellipse(c, cx - rr, cy - rr, rr * 2, rr * 2, 0x000000u, r < 32 ? MG_HL : UI_EDGE, 120);
        }
        ui_fill(c, cx - 6, cy - 4, 12, 9, MG_GOLD, 255);
        ui_fill(c, cx - 1, cy - 4, 2, 9, MG_CARD_A, 255);
        mg_message(c, in, work, tb != NULL ? rdp(tb, 0x10) : NULL, rdp(work, W_SAVEMENU), mx, mw, my + mh);
        return;
    }
    mg_message(c, in, work, tb != NULL ? rdp(tb, 0x10) : NULL, rdp(work, W_SAVEMENU), mx, mw,
               (ch - 17) / 2 - 20);
}

/* A gift arriving (MYSTERY_TaskAlbum): the game shows the gift, saves it,
 * then shows its card in the album until A.  States 0 to 9 are the gift
 * and its messages, 10 to 12 build the card's view, 13 on the card.  The
 * page shows the gift large (its Pokemon's sprite, or the item), and the
 * Wonder Card beside it once it is received. */
static void mg_arrival(EidosUiCtx *c, const EidosUiFrame *in, void *work, int state)
{
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 16 < 460 ? cw - 16 : 460, mh = ch - 17 - 60 < 196 ? ch - 17 - 60 : 196;
    int mx = (cw - mw) / 2, my = (ch - 17 - mh - 50) / 2, gw, t;
    const u8 *pk = (const u8 *)work + W_PACKET;
    void *tb = rdp(work, W_TEXTBOX);
    MgCard g;
    static u32 g_arrive_t;

    if (my < 4) {
        my = 4;
    }
    memset(&g, 0, sizeof g);
    g.raw = pk;
    g.kind = (int)rd8(pk, C_KIND);
    g.species = (int)rd16(pk, C_SPECIES);
    g.form = (int)rd8(pk, C_FORM);
    g.item = (int)rd16(pk, C_ITEM);
    g.picked = 0;
    units_utf8(pk + C_TITLE, 37, g.title, (int)sizeof g.title);
    {
        /* the date the game wrote on receiving it */
        g.year = (int)rd16(pk, C_DATE + 2);
        g.month = (int)rd8(pk, C_DATE + 1);
        g.day = (int)rd8(pk, C_DATE);
    }
    if (state == 0) {
        g_arrive_t = in->frame;
    }
    t = (int)(in->frame - g_arrive_t);
    mg_window(c, mx, my, mw, mh, state < 8 ? "RECEIVING GIFT" : "GIFT RECEIVED", NULL);
    gw = mw * 2 / 5;
    /* the gift on a light, growing in as it arrives */
    {
        int cx = mx + 10 + gw / 2, cy = my + 26 + (mh - 36) / 2, r = t < 40 ? t : 40;
        int k;

        for (k = 0; k < 3; k++) {
            int rr = r + k * 10;

            ui_ellipse(c, cx - rr, cy - rr, rr * 2, rr * 2, 0x000000u, k == 0 ? MG_GOLD : MG_HL, 90 - k * 25);
        }
        if (t > 20) {
            if (g.kind == 1 && g.species > 0 && g.species < 700) {
                ui_mon_front_draw(c, (u16)g.species, rd8(pk, C_SHINY) == 2, in->frame, mx + 10, my + 26, gw,
                                  mh - 36);
            } else {
                const UiImage *ic = mg_card_icon(&g);

                if (ic != NULL) {
                    ui_image(c, cx - ic->w * 3 / 2, cy - ic->h * 3 / 2, ic, 3);
                }
            }
        }
    }
    /* the Wonder Card, once the game has received it */
    if (state >= 8) {
        mg_wonder_card(c, mx + 10 + gw + 10, my + 26, mw - gw - 30, mh - 34, &g);
    } else {
        mg_panel(c, mx + 10 + gw + 10, my + 26, mw - gw - 30, mh - 34);
        ui_text(c, mx + gw + 30, my + 34, g.title, UI_WHITE, MG_SHADOW, UI_FONT_LARGE, 1);
        mg_text_lines(c, mx + gw + 30, my + 54, pk + MG_P_TEXT, 253, UI_LIGHT, (mh - 70) / 14);
    }
    if (tb != NULL) {
        mg_message(c, in, work, rdp(tb, 0x10), NULL, mx, mw, my + mh);
    } else {
        ui_choice_list_idle();
    }
}

/* The Card Album. */
static void mg_album(EidosUiCtx *c, const EidosUiFrame *in, void *work, void *aw)
{
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 16 < 460 ? cw - 16 : 460, mh = ch - 17 - 12 < 214 ? ch - 17 - 12 : 214;
    int mx = (cw - mw) / 2, my = (ch - 17 - mh) / 2;
    void *data = rdp(work, W_DATA);
    int page = (int)rd32(aw, A_PAGE), pos = (int)rd32(aw, A_POS), sel = pos < 4 ? page * 4 + pos : -1;
    int cols, rows, gw, sw, sh, i, waiting = 0, n = mg_card_count(data, &waiting);
    char right[16];
    MgCard cd, cards[12];
    int have[12];
    void *menuwin = rdp(aw, A_MENUWIN);

    for (i = 0; i < 12; i++) {
        have[i] = mg_card(data, i, &cards[i]);
    }
    snprintf(right, sizeof right, "%d/12", n);
    mg_window(c, mx, my, mw, mh, mg_text(147), right);
    if (mw >= 420) {
        cols = 3; rows = 4;
        gw = mw - 175 < 196 ? mw - 175 : 196;
    } else {
        cols = 1; rows = 6;
        gw = mw - 20 - 10 - 196;
    }
    sw = (gw - (cols - 1) * 4) / cols;
    sh = (mh - 34 - (rows - 1) * 4) / rows;
    {
        int first = 0, shown = cols * rows;

        if (sel >= shown) {
            first = sel - shown + 1;
        }
        for (i = 0; i < shown && first + i < 12; i++) {
            int s = first + i, st;

            st = mg_mini_card(c, mx + 10 + (i % cols) * (sw + 4), my + 26 + (i / cols) * (sh + 4), sw, sh,
                              have[s] ? &cards[s] : NULL, s == sel);
            if ((st & EIDOS_UI_CLICK) && have[s] && menuwin == NULL) {
                g_target = s;
                g_target_kind = 100;
                g_wait = 0;
            }
        }
    }
    if (sel >= 0 && sel < 12 && have[sel]) {
        cd = cards[sel];
        mg_wonder_card(c, mx + 10 + gw + 10, my + 26, mw - gw - 30, mh - 34, &cd);
    } else {
        mg_panel(c, mx + 10 + gw + 10, my + 26, mw - gw - 30, mh - 34);
        mg_wrapped(c, mx + gw + 28, my + 34, mw - gw - 46, 13, UI_FONT_SMALL,
                   n > 0 ? "Choose a card to see it." : "No cards yet. Received gifts show up here.",
                   UI_GREY, 4);
    }
    mg_walk_album(page, pos);
    /* the album's question and its SWAP / TRASH / BACK */
    if (menuwin != NULL || rdp(aw, A_MENU) != NULL) {
        int top = my + mh - 50;

        mg_message(c, in, work, menuwin != NULL ? rdp(menuwin, 0x10) : NULL, rdp(aw, A_MENU), mx, mw, top);
    } else {
        ui_choice_list_idle();
    }
}

/* ------------------------------------------------------------------ */
/* Test cards                                                          */
/* ------------------------------------------------------------------ */

/* For checks only: EIDOS_DEBUG_MYSTERY_CARDS names Wonder Card files
 * (.pgf, 0xCC bytes, ';' between them) that the game's own
 * MYSTERY_DATA_AddCard puts in the album once Mystery Gift is open, dated
 * today, as if they had been received. */
extern int MYSTERY_DATA_AddCard(void *data, const void *card) MG_WEAK;

static void mg_add_test_cards(void *data)
{
    const char *list = getenv("EIDOS_DEBUG_MYSTERY_CARDS");
    char path[600];

    while (list != NULL && *list != '\0' && MYSTERY_DATA_AddCard != NULL) {
        size_t n = strcspn(list, ";");
        u8 card[0xCC];
        FILE *f;

        if (n >= sizeof path) {
            n = sizeof path - 1;
        }
        memcpy(path, list, n);
        path[n] = '\0';
        list += n + (list[n] == ';' ? 1 : 0);
        f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        if (fread(card, 1, sizeof card, f) == sizeof card) {
            card[C_DATE] = 5;                       /* 2026-11-05 */
            card[C_DATE + 1] = 11;
            card[C_DATE + 2] = (u8)(2026 & 0xFF);
            card[C_DATE + 3] = (u8)(2026 >> 8);
            MYSTERY_DATA_AddCard(data, card);
        }
        fclose(f);
    }
}

static void mg_test_cards(void *work)
{
    static int done;

    if (!done && getenv("EIDOS_DEBUG_MYSTERY_CARDS") != NULL && rdp(work, W_DATA) != NULL) {
        done = 1;
        eidos_ui_game_call(mg_add_test_cards, rdp(work, W_DATA));
    }
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int mystery_match(void)
{
    return mg_work() != NULL;
}

static void mystery_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    void *work = mg_work(), *dlg, *aw, *task;
    int cw = c->canvas->w, ch = c->canvas->h, kind = DLG_NONE, searching = 0;

    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 0;
    view->hide_2d = 1;
    view->ds_only = 1;
    if (work == NULL) {
        mg_backdrop(c, in, cw, ch, 0);
        return;
    }
    mg_test_cards(work);
    aw = rdp(work, W_ALBUM);
    dlg = rdp(work, W_DIALOG);
    task = rdp(work, W_TASK);
    if (dlg != NULL) {
        uintptr_t out = (uintptr_t)rd32(dlg, D_OUT), base = (uintptr_t)work + W_RESULTS;

        if (out >= base && out < base + 16u) {
            kind = (int)((out - base) / 4u);
        }
    }
    mg_backdrop(c, in, cw, ch, aw == NULL && kind == DLG_MENU);
    if (task != NULL && &ov121_MYSTERY_TaskReceive != NULL &&
        rdp(task, 0x00) == (void *)(uintptr_t)ov121_MYSTERY_TaskReceive) {
        searching = 1;
    }
    if (task != NULL && &ov121_MYSTERY_TaskAlbum != NULL &&
        rdp(task, 0x00) == (void *)(uintptr_t)ov121_MYSTERY_TaskAlbum) {
        g_target = -1;
        mg_arrival(c, in, work, (int)rd32(task, 0x08));
    } else if (aw != NULL) {
        mg_album(c, in, work, aw);
    } else if (kind != DLG_NONE) {
        mg_lists(c, in, work, kind, dlg);
    } else {
        g_target = -1;
        mg_only_message(c, in, work, searching);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    if (getenv("EIDOS_UI_MYSTERY_LOG") != NULL && (in->frame % 20u == 0u || getenv("EIDOS_UI_MYSTERY_LOG")[0] == '2')) {
        fprintf(stderr, "mystery: frame %u kind %d index %d album %d page %d pos %d search %d\n", in->frame,
                kind, dlg != NULL ? (int)rd32(dlg, D_INDEX) : -1, aw != NULL,
                aw != NULL ? (int)rd32(aw, A_PAGE) : -1, aw != NULL ? (int)rd32(aw, A_POS) : -1, searching);
    }
}

static void mystery_reset(void)
{
    g_target = -1;
    g_target_kind = -2;
    g_wait = 0;
}

static const EidosUiRecipe recipe_mystery = {
    "mystery", mystery_match, mystery_run, mystery_reset, 0
};

const EidosUiRecipe *const ui_recipes_mystery[] = { &recipe_mystery };
const u32 ui_recipes_mystery_count = sizeof(ui_recipes_mystery) / sizeof(ui_recipes_mystery[0]);
