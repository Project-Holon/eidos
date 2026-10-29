/*
 * engine/ui/recipe_shop.c -- the one-screen recipe for the Poke Mart's
 * buy list (the shop script command, ov021 scrcmd_shop.c; the work is the
 * ShopTask the command allocates, docs/unified-view-recipes.md).
 *
 * The shop is a modal over the blurred world: the item list (the game's
 * item icons, names, prices), the money, the selected item's details (the
 * icon at 2x, the game's description, how many are in the bag) and the
 * prompts: the quantity chooser, the buy confirmation and the game's own
 * messages.  Blue is the Mart's colour.
 *
 * The game keeps all the logic.  Everything shown is read from the game's
 * ShopTask (32-bit layout, offsets in the comments); every control becomes
 * the key the game tests: the list and the yes/no list move with UP and
 * DOWN, the quantity with UP, DOWN, LEFT and RIGHT, A decides and B backs
 * out.  A click on a row therefore walks the game's cursor to that row, one
 * step every few frames, and presses A when it is there.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "input.h"
#include "overlay.h"

/* ------------------------------------------------------------------ */
/* The game                                                            */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
#define SH_WEAK __attribute__((weak))
#else
#define SH_WEAK
#endif

extern void *bw_dsref__GameSystemGlobal SH_WEAK;

extern void *GAMESYSTEM_GetGameData(const void *gs) SH_WEAK;
extern void *GAME_DATA_GetMiscSave(void *gd) SH_WEAK;
extern void *GAME_DATA_GetMyItem(void *gd) SH_WEAK;
extern u32   MISC_GetMoney(void *misc) SH_WEAK;
extern u32   BSUBWAY_SCOREDATA_GetBattlePoints(void *data) SH_WEAK;
extern u32   MYITEM_SAVEDATA_GetItemQuantity(void *bag, u32 item, u32 heap) SH_WEAK;
extern void  MSGDATA_ReadString(void *bank, u32 index, void *dest) SH_WEAK;
extern void  FILE_UTIL_ReadString(void *dst, u32 entry, u32 heap) SH_WEAK;
extern void  FILE_UTIL_ReadStringAlt(void *dst, u32 entry, u32 heap) SH_WEAK;
extern void  BMP_MENULIST_GetPos(void *menu, u16 *scroll, u16 *row) SH_WEAK;
extern u8    UI_GetInputMode(void) SH_WEAK;
extern void  UI_SetInputMode(u32 value) SH_WEAK;

/* Reads at a DS byte offset of a game struct (the layout is the decomp's;
 * pointers are 32 bits in this build). */
static u32 rd32(const void *p, u32 off)
{
    return *(const u32 *)((const u8 *)p + off);
}

static u32 rd16(const void *p, u32 off)
{
    return *(const u16 *)((const u8 *)p + off);
}

static u32 rd8(const void *p, u32 off)
{
    return *((const u8 *)p + off);
}

static void *rdp(const void *p, u32 off)
{
    return (void *)(uintptr_t)rd32(p, off);
}

/* ShopTask (ov021/scrcmd_proc_fld_types.h): state 0x00, shopKind 0x02,
 * ShopWork at 0x08.  The ShopWork offsets below are from the task start. */
#define T_STATE       0x00u     /* u8: 4 while the shop screen runs        */
#define T_KIND        0x02u     /* u8                                      */
#define T_MONEY       0x14u     /* MiscSave*                               */
#define T_BAG         0x18u     /* MyItem*                                 */
#define T_HEAP        0x0Cu     /* u16                                     */
#define T_SHOPSTATE   0x08u     /* u8: the shop's own state                */
#define T_ITEMS       0x28u     /* ShopItem[40]: u16 id, pad, s32 price    */
#define T_COUNT       0x168u    /* u16                                     */
#define T_ISTM        0x16Au    /* u8                                      */
#define T_USESBP      0x16Bu    /* u8: battle points instead of money      */
#define T_YESNO       0x18Cu    /* BmpMenu*                                */
#define T_MSGBANK     0x190u    /* the shop's message bank                 */
#define T_TEXT        0x1A0u    /* Strbuf*: the message the game prints    */
#define T_LIST        0x1ACu    /* BmpMenuList*                            */
#define T_ITEMID      0x204u    /* u16: the picked item                    */
#define T_PRICE       0x208u    /* u32                                     */
#define T_MAXQTY      0x20Cu    /* u32                                     */
#define T_QTY         0x210u    /* u16                                     */

/* ShopWork.state values (SHOP_STATE_*). */
enum {
    S_INIT = 0, S_LIST = 1, S_QTY_ENTER = 2, S_QTY = 3, S_LIST_RESUME = 4,
    S_YESNO_OPEN = 5, S_YESNO_OPEN2 = 6, S_YESNO = 7, S_PURCHASE = 8,
    S_BONUS = 9, S_MESSAGE = 10, S_EXIT = 11
};

#define MSG_QUIT      11        /* the shop's bank: the list's last row     */
#define ITEM_ID_MAX   0x280
#define STRBUF_MAGIC  0xB6F8D2ECu

typedef struct ShopStr {        /* a Strbuf (arm9/strbuf_types.h)          */
    u16 capacity;
    u16 length;
    u32 magic;
    u16 chars[512];
} ShopStr;

static void shopstr_init(ShopStr *s)
{
    s->capacity = 512;
    s->length = 0;
    s->magic = STRBUF_MAGIC;
    s->chars[0] = 0xFFFFu;
}

/* The game's 16-bit text as UTF-8; 0xFFFE, 0xE000 and 0x000A are line
 * breaks the kit wraps itself. */
static void units_to_utf8(const u16 *u, char *out, int max)
{
    int n = 0, i;

    for (i = 0; i < 511 && u[i] != 0xFFFFu; i++) {
        u32 c = u[i];

        if (c == 0xF000u && u[i + 1] != 0xFFFFu && u[i + 2] != 0xFFFFu) {
            i += 2 + (int)u[i + 2];     /* a control code: F000, code, argument count, arguments */
            continue;
        }
        if (c == 0xFFFEu || c == 0xE000u || c == 0x000Au) {
            c = ' ';
        } else if (c >= 0xD800u && c <= 0xF8FFu) {
            continue;
        }
        if (c < 0x80u) {
            if (n + 1 >= max) break;
            out[n++] = (char)c;
        } else if (c < 0x800u) {
            if (n + 2 >= max) break;
            out[n++] = (char)(0xC0u | (c >> 6));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            if (n + 3 >= max) break;
            out[n++] = (char)(0xE0u | (c >> 12));
            out[n++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Finding the shop                                                    */
/* ------------------------------------------------------------------ */

#define SCRIPT_WORK_MAGIC 0x3643Fu

/* A DS main memory address (the DS address space is identity-mapped): the
 * words read on the way are not always pointers. */
static int ds_ptr(const void *p)
{
    uintptr_t a = (uintptr_t)p;

    return a >= 0x02000000u && a < 0x02400000u;
}

static int g_log;

typedef struct Shop {
    const u8 *t;            /* the ShopTask                                */
    int       state;        /* ShopWork.state                              */
    int       count;        /* items for sale                              */
    int       cursor;       /* the game's cursor: 0..count (count = Quit)  */
    int       bp;           /* pays in battle points                       */
    int       tm;           /* a TM shop                                   */
} Shop;


/* The ShopTask while the shop screen runs, else NULL: the current event is
 * a script event whose script work keeps the task as its user work. */
static const u8 *shop_task(void)
{
    void *gs, *ev, *ew, *gd, *sw = NULL;
    const u8 *task;
    int i;

    if (!bw_overlay_is_loaded(21) || &bw_dsref__GameSystemGlobal == NULL ||
        bw_dsref__GameSystemGlobal == NULL || GAMESYSTEM_GetGameData == NULL ||
        GAME_DATA_GetMiscSave == NULL || GAME_DATA_GetMyItem == NULL) {
        return NULL;
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    if (!ds_ptr(gs)) {
        return NULL;
    }
    ev = rdp(gs, 0x18);
    if (!ds_ptr(ev)) {
        return NULL;
    }
    ew = rdp(ev, 0x0C);
    if (!ds_ptr(ew)) {
        return NULL;
    }
    /* the event work: the script system and the script work, in one order
     * or the other */
    for (i = 0; i < 2 && sw == NULL; i++) {
        void *cand = rdp(ew, 4u * (u32)i);

        if (ds_ptr(cand) && rd32(cand, 0x00) == SCRIPT_WORK_MAGIC) {
            sw = cand;
        }
    }
    if (sw == NULL) {
        return NULL;
    }
    task = (const u8 *)rdp(sw, 0x3C);
    if (!ds_ptr(task)) {
        return NULL;
    }
    gd = GAMESYSTEM_GetGameData(gs);
    /* the task has the shop's save pointers the start command filled in */
    if (gd == NULL || rdp(task, T_MONEY) != GAME_DATA_GetMiscSave(gd) ||
        rdp(task, T_BAG) != GAME_DATA_GetMyItem(gd) || rd16(task, T_HEAP) != 0x15u) {
        return NULL;
    }
    return task;
}

static int shop_read(Shop *s, const u8 *t)
{
    u16 scroll = 0, row = 0;
    void *list;

    memset(s, 0, sizeof *s);
    s->t = t;
    s->state = (int)rd8(t, T_SHOPSTATE);
    s->count = (int)rd16(t, T_COUNT);
    s->bp = rd8(t, T_USESBP) != 0;
    s->tm = rd8(t, T_ISTM) == 1;
    if (s->count < 0 || s->count > 40 || s->state == S_INIT || s->state >= S_EXIT) {
        return 0;
    }
    list = rdp(t, T_LIST);
    if (list != NULL && BMP_MENULIST_GetPos != NULL) {
        BMP_MENULIST_GetPos(list, &scroll, &row);
        s->cursor = (int)scroll + (int)row;
    }
    if (s->cursor > s->count) {
        s->cursor = s->count;
    }
    return 1;
}

/* The recipe holds the scene while the camera moves in and out too (task
 * states 0 to 3 and 5 to 6), so the world does not change look between the
 * speech and the shop. */
static int match_shop(void)
{
    const u8 *t = shop_task();

    return t != NULL && rd8(t, T_STATE) <= 6u;
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static char g_name[ITEM_ID_MAX][40];
static u8   g_name_ok[ITEM_ID_MAX];
static char g_desc[ITEM_ID_MAX][320];
static u8   g_desc_ok[ITEM_ID_MAX];
static char g_quit[32];
static const void *g_seen;

static void caches_clear(void)
{
    memset(g_name_ok, 0, sizeof g_name_ok);
    memset(g_desc_ok, 0, sizeof g_desc_ok);
    g_quit[0] = '\0';
}

static const char *item_name(const Shop *s, u32 id)
{
    ShopStr b;

    if (id == 0 || id >= ITEM_ID_MAX) {
        return "";
    }
    if (!g_name_ok[id]) {
        g_name[id][0] = '\0';
        if (FILE_UTIL_ReadString != NULL) {
            shopstr_init(&b);
            FILE_UTIL_ReadString(&b, id, rd16(s->t, T_HEAP));
            units_to_utf8(b.chars, g_name[id], (int)sizeof g_name[id]);
        }
        g_name_ok[id] = 1;
    }
    return g_name[id];
}

static const char *item_desc(const Shop *s, u32 id)
{
    ShopStr b;

    if (id == 0 || id >= ITEM_ID_MAX) {
        return "";
    }
    if (!g_desc_ok[id]) {
        g_desc[id][0] = '\0';
        if (FILE_UTIL_ReadStringAlt != NULL) {
            shopstr_init(&b);
            FILE_UTIL_ReadStringAlt(&b, id, rd16(s->t, T_HEAP));
            units_to_utf8(b.chars, g_desc[id], (int)sizeof g_desc[id]);
        }
        g_desc_ok[id] = 1;
    }
    return g_desc[id];
}

static const char *quit_label(const Shop *s)
{
    void *bank = rdp(s->t, T_MSGBANK);

    if (g_quit[0] == '\0') {
        if (bank != NULL && MSGDATA_ReadString != NULL) {
            ShopStr b;

            shopstr_init(&b);
            MSGDATA_ReadString(bank, MSG_QUIT, &b);
            units_to_utf8(b.chars, g_quit, (int)sizeof g_quit);
        }
        if (g_quit[0] == '\0') {
            snprintf(g_quit, sizeof g_quit, "Quit");
        }
    }
    return g_quit;
}

/* The text of the message the game prints (its text buffer), as UTF-8. */
static void message_text(const Shop *s, char *out, int max)
{
    const u8 *sb = (const u8 *)rdp(s->t, T_TEXT);
    u16 buf[512];
    int len, i;

    out[0] = '\0';
    if (sb == NULL || *(const u32 *)(sb + 4) != STRBUF_MAGIC) {
        return;
    }
    len = *(const u16 *)(sb + 2);
    if (len > 510) {
        len = 510;
    }
    for (i = 0; i < len; i++) {
        buf[i] = *(const u16 *)(sb + 8 + 2 * i);
    }
    buf[len] = 0xFFFFu;
    units_to_utf8(buf, out, max);
}

static u32 currency(const Shop *s)
{
    void *gd, *gs;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return 0;
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    gd = gs != NULL && GAMESYSTEM_GetGameData != NULL ? GAMESYSTEM_GetGameData(gs) : NULL;
    (void)gd;
    if (s->bp) {
        /* the battle points record sits behind the task's save pointer
         * (ShopWork.battlePointData) */
        void *bpd = rdp(s->t, 0x20u);

        return (bpd != NULL && BSUBWAY_SCOREDATA_GetBattlePoints != NULL) ?
               BSUBWAY_SCOREDATA_GetBattlePoints(bpd) : 0;
    }
    return MISC_GetMoney != NULL ? MISC_GetMoney(rdp(s->t, T_MONEY)) : 0;
}

/* ------------------------------------------------------------------ */
/* Input to the game                                                   */
/* ------------------------------------------------------------------ */

static void press_key(u32 key)
{
    if (UI_GetInputMode != NULL && UI_SetInputMode != NULL && UI_GetInputMode() != 0) {
        UI_SetInputMode(0);
    }
    eidos_ui_press(key, 2);
}

/* The game's cursor walks to a goal row, one step every few frames; at the
 * goal A is pressed when `decide` is set.  One goal at a time: a new click
 * replaces the old.  The yes/no list has its own goal. */
static int g_goal = -1, g_goal_a;
static int g_yn_goal = -1;
static u32 g_next;
static int g_wheel;
static u32 g_wheel_next;
static int g_top;               /* the first list row shown                */

static void set_goal(int row, int decide)
{
    g_goal = row;
    g_goal_a = decide;
}

static void walk_list(const Shop *s, u32 now)
{
    if (g_goal < 0 || s->state != S_LIST) {
        if (s->state != S_LIST) {
            g_goal = -1;
        }
        return;
    }
    if (now < g_next) {
        return;
    }
    if (s->cursor == g_goal) {
        if (g_goal_a) {
            press_key(BW_KEY_A);
        }
        g_goal = -1;
        g_next = now + 6;
    } else {
        press_key(s->cursor > g_goal ? BW_KEY_UP : BW_KEY_DOWN);
        if (g_log == 1) fprintf(stderr, "ui: shop walk %d to %d" "\n", s->cursor, g_goal);
        g_next = now + 4;
    }
}

static void walk_yesno(const Shop *s, u32 now)
{
    const void *menu = rdp(s->t, T_YESNO);
    int cur;

    if (g_yn_goal < 0 || s->state != S_YESNO || menu == NULL) {
        if (s->state != S_YESNO) {
            g_yn_goal = -1;
        }
        return;
    }
    if (now < g_next) {
        return;
    }
    cur = (int)rd8(menu, 0x2D);
    if (cur == g_yn_goal) {
        press_key(BW_KEY_A);
        g_yn_goal = -1;
        g_next = now + 6;
    } else {
        press_key(cur > g_yn_goal ? BW_KEY_UP : BW_KEY_DOWN);
        g_next = now + 4;
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define MODAL_FILL   0x081A33u
#define MODAL_FILL2  0x06142Au
#define ROW_FILL     0x0B2646u
#define ROW_EMPTY    0x07182Cu
#define ROW_HOVER    0x124A7Au
#define EDGE_BLUE    0x0A4F82u
#define ACCENT       UI_SKY
#define ACCENT2      UI_CYAN2
#define SHADOW       0x041428u
#define YEN          "\xEF\xBF\xA5"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static EidosUiRect rect(int x, int y, int w, int h)
{
    EidosUiRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

typedef struct Hint {
    const char *key, *what;
    u32 keymask;
} Hint;

static void hints_bar(EidosUiCtx *c, int y, int w, const Hint *h, int n)
{
    int i, x = 6;

    ui_fill(c, 0, y, w, 17, UI_BODY2, 235);
    ui_fill(c, 0, y, w, 1, UI_EDGE, 255);
    for (i = 0; i < n; i++) {
        int kw = ui_keycap_w(h[i].key);
        int aw = ui_text_w(h[i].what, UI_FONT_SMALL);
        int rw = kw + 4 + aw + 4;
        int st = eidos_ui_hit(rect(x - 2, y, rw, 17));

        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, y + 1, rw, 15, UI_EDGE, 255);
        }
        if (st & EIDOS_UI_CLICK) {
            press_key(h[i].keymask);
        }
        ui_keycap(c, x, y + 2, h[i].key);
        x += kw + 4;
        x = ui_text(c, x, y + 2 + ui_text_y(13, UI_FONT_SMALL), h[i].what, UI_LIGHT,
                    UI_EDGE, UI_FONT_SMALL, 1) + 10;
    }
}

typedef struct LinePaint {
    EidosUiCtx *c;
    int x, y, line_h, max_y, font;
} LinePaint;

static void paint_line(void *user, const char *s, int len)
{
    LinePaint *d = (LinePaint *)user;
    char tmp[160];

    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    if (d->y + d->line_h <= d->max_y) {
        ui_text(d->c, d->x, d->y, tmp, UI_LIGHT, SHADOW, d->font, 1);
    }
    d->y += d->line_h;
}

static void draw_description(EidosUiCtx *c, int x, int y, int w, int h, const char *text)
{
    LinePaint d;
    int lines;

    d.c = c;
    d.x = x;
    d.font = UI_FONT_MEDIUM;
    d.line_h = 14;
    lines = ui_wrap(text, w, UI_FONT_MEDIUM, NULL, NULL);
    if (lines * 14 > h) {
        d.font = UI_FONT_SMALL;
        d.line_h = 12;
    }
    d.y = y;
    d.max_y = y + h;
    ui_wrap(text, w, d.font, paint_line, &d);
}

static void wrap_message(EidosUiCtx *c, int x, int y, int w, const char *text)
{
    LinePaint d;

    d.c = c;
    d.x = x;
    d.y = y;
    d.font = UI_FONT_LARGE;
    d.line_h = 16;
    d.max_y = y + 16 * 6;
    ui_wrap(text, w, UI_FONT_LARGE, paint_line, &d);
}

/* A button with a word or a symbol; returns non-zero on a click. */
static int small_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label)
{
    int st = eidos_ui_hit(rect(x, y, w, h));

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, (st & EIDOS_UI_HOVER) ? ROW_HOVER : ROW_FILL,
             (st & EIDOS_UI_HOVER) ? UI_WHITE : EDGE_BLUE, 255);
    ui_text(c, x + (w - ui_text_w(label, UI_FONT_LARGE)) / 2, y + ui_text_y(h, UI_FONT_LARGE),
            label, UI_WHITE, SHADOW, UI_FONT_LARGE, 1);
    return (st & EIDOS_UI_CLICK) != 0;
}

/* One row of the list: icon, name, price. */
static void list_row(EidosUiCtx *c, int x, int y, int w, int h, const UiImage *icon,
                     const char *name, const char *price, int lit, int hover)
{
    int ty = ui_text_y(h, UI_FONT_LARGE), tx = 6;
    EidosUiRect saved;
    int room = w - 12;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4,
             lit ? ACCENT2 : (hover ? ROW_HOVER : ROW_FILL), lit ? UI_WHITE : EDGE_BLUE, 255);
    if (icon != NULL) {
        ui_image(c, x + 4, y + (h - icon->h) / 2, icon, 1);
        tx = 4 + icon->w + 4;
        room = w - tx - 6;
    }
    if (price != NULL && price[0] != '\0') {
        room -= ui_text_w(price, UI_FONT_LARGE) + 8;
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x + tx, y, imax(room, 8), h);
    ui_text(c, x + tx, y + ty, name, UI_WHITE, SHADOW, UI_FONT_LARGE, 1);
    ui_pop(c, 0, 0, &saved);
    if (price != NULL && price[0] != '\0') {
        ui_text(c, x + w - 8 - ui_text_w(price, UI_FONT_LARGE), y + ty, price, UI_WHITE, SHADOW,
                UI_FONT_LARGE, 1);
    }
}

static void price_text(const Shop *s, u32 price, char *out, int max)
{
    if (s->bp) {
        snprintf(out, (size_t)max, "%u BP", price);
    } else {
        snprintf(out, (size_t)max, YEN "%u", price);
    }
}

/* The dialog over the list: the game's message and what it waits for. */
static void draw_dialog(EidosUiCtx *c, const Shop *s, int mx, int my, int mw, int mh)
{
    char msg[256];
    int dw = imin(mw - 24, 320), lines, dh, dx, dy, i;
    int row_h = 20;
    int qty = s->state == S_QTY, yn = s->state == S_YESNO;
    int wait = !qty && !yn;

    message_text(s, msg, (int)sizeof msg);
    lines = ui_wrap(msg, dw - 24, UI_FONT_LARGE, NULL, NULL);
    if (lines < 1) {
        lines = 1;
    }
    dh = 12 + lines * 16 + ((qty || yn) ? row_h + 10 : 0) + 6;
    if (qty) {
        dh += row_h + 6;
    }
    dx = mx + (mw - dw) / 2;
    dy = my + (mh - dh) / 2;
    ui_fill(c, mx + 6, my + 6, mw - 12, mh - 12, UI_INK, 120);
    if (wait && (eidos_ui_hit(rect(0, 0, c->canvas->w, c->canvas->h)) & EIDOS_UI_CLICK)) {
        press_key(BW_KEY_A);        /* a click anywhere moves a message on */
    }
    ui_panel(c, dx, dy, dw, dh, UI_CUT_TL | UI_CUT_BR, 8, MODAL_FILL2, ACCENT, 255);
    wrap_message(c, dx + 12, dy + 10, dw - 24, msg);
    if (yn) {
        const void *menu = rdp(s->t, T_YESNO);
        int cur = menu != NULL ? (int)rd8(menu, 0x2D) : 0;
        const char *labels[2];
        int bw = 56, by = dy + dh - row_h - 8, hover = -1;

        labels[0] = "YES";
        labels[1] = "NO";
        for (i = 0; i < 2; i++) {
            int bx = dx + dw - 12 - (2 - i) * bw - (1 - i) * 6;

            if (eidos_ui_hit(rect(bx, by, bw, row_h)) & EIDOS_UI_HOVER) {
                hover = i;
            }
        }
        for (i = 0; i < 2; i++) {
            int bx = dx + dw - 12 - (2 - i) * bw - (1 - i) * 6;
            int st = eidos_ui_hit(rect(bx, by, bw, row_h));
            int lit = hover >= 0 ? hover == i : (cur == i && eidos_ui_keys_lit());

            ui_panel(c, bx, by, bw, row_h, UI_CUT_TL | UI_CUT_BR, 4, lit ? ACCENT2 : ROW_FILL,
                     lit ? UI_WHITE : EDGE_BLUE, 255);
            ui_text(c, bx + (bw - ui_text_w(labels[i], UI_FONT_LARGE)) / 2,
                    by + ui_text_y(row_h, UI_FONT_LARGE), labels[i], UI_WHITE, SHADOW,
                    UI_FONT_LARGE, 1);
            if (st & EIDOS_UI_CLICK) {
                g_yn_goal = i;
            }
        }
    } else if (qty) {
        u32 n = rd16(s->t, T_QTY), price = rd32(s->t, T_PRICE);
        char num[24], total[32], tot_price[24];
        int by = dy + dh - row_h * 2 - 14, bx = dx + 12, nw = 52, wh;

        if (small_button(c, bx, by, 28, row_h, "-10")) {
            press_key(BW_KEY_LEFT);
        }
        if (small_button(c, bx + 32, by, 24, row_h, "-")) {
            press_key(BW_KEY_DOWN);
        }
        snprintf(num, sizeof num, "\xC3\x97%u", n);
        ui_panel(c, bx + 60, by, nw, row_h, UI_CUT_TL | UI_CUT_BR, 3, ROW_EMPTY, EDGE_BLUE, 255);
        ui_text(c, bx + 60 + (nw - ui_text_w(num, UI_FONT_LARGE)) / 2,
                by + ui_text_y(row_h, UI_FONT_LARGE), num, UI_WHITE, SHADOW, UI_FONT_LARGE, 1);
        if (small_button(c, bx + 60 + nw + 4, by, 24, row_h, "+")) {
            press_key(BW_KEY_UP);
        }
        if (small_button(c, bx + 60 + nw + 32, by, 28, row_h, "+10")) {
            press_key(BW_KEY_RIGHT);
        }
        price_text(s, n * price, tot_price, (int)sizeof tot_price);
        snprintf(total, sizeof total, "%s", tot_price);
        ui_text(c, dx + dw - 12 - ui_text_w(total, UI_FONT_LARGE), by + ui_text_y(row_h, UI_FONT_LARGE),
                total, UI_WHITE, SHADOW, UI_FONT_LARGE, 1);
        by += row_h + 6;
        if (small_button(c, dx + dw - 12 - 56 * 2 - 6, by, 56, row_h, "OK")) {
            press_key(BW_KEY_A);
        }
        if (small_button(c, dx + dw - 12 - 56, by, 56, row_h, "BACK")) {
            press_key(BW_KEY_B);
        }
        wh = eidos_ui_wheel(rect(mx, my, mw, mh));
        if (wh != 0) {
            press_key(wh > 0 ? BW_KEY_UP : BW_KEY_DOWN);
        }
    }
}

static void run_shop(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    Shop s;
    const u8 *t = shop_task();
    int cw = c->canvas->w, ch = c->canvas->h;
    int hints_y = ch - 17, mx, my, mw, mh, pad = 8, hdr = 22;
    int list_x, list_w, det_x, det_w, body_y, body_h, rowh, rows, total, i, sb, rw;
    int hover_item = 0, sel_item = 0, hover_row = -1;
    int browse;
    u32 now = in->frame;
    char buf[48];

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hide_2d = 1;
    if (t == NULL || rd8(t, T_STATE) != 4u || !shop_read(&s, t)) {
        view->blur = 0;
        view->hide_2d = 0;
        return;
    }
    if ((const void *)t != g_seen) {
        g_seen = t;
        caches_clear();
        g_goal = g_yn_goal = -1;
        g_top = 0;
        g_wheel = 0;
    }
    if (g_log == 0) {
        g_log = getenv("EIDOS_UI_DEBUG") != NULL ? 1 : 2;
    }
    if (g_log == 1 && (now % 30u) == 0) {
        fprintf(stderr, "ui: shop state %d cursor %d count %d qty %u max %u tm %d bp %d\n", s.state,
                s.cursor, s.count, rd16(t, T_QTY), rd32(t, T_MAXQTY), s.tm, s.bp);
    }
    browse = s.state == S_LIST || s.state == S_QTY_ENTER || s.state == S_LIST_RESUME;

    mw = iclamp(cw * 90 / 100, 330, 520);
    mw = imin(mw, cw - 8);
    mh = iclamp(ch * 88 / 100, 200, 300);
    mh = imin(mh, hints_y - 4);
    mx = (cw - mw) / 2;
    my = (hints_y - mh) / 2;

    /* outside the modal, or a right click: B */
    if (s.state == S_LIST) {
        int st = eidos_ui_hit(rect(0, 0, cw, my)) | eidos_ui_hit(rect(0, my + mh, cw, hints_y - my - mh)) |
                 eidos_ui_hit(rect(0, my, mx, mh)) | eidos_ui_hit(rect(mx + mw, my, cw - mx - mw, mh));

        if (st & EIDOS_UI_CLICK) {
            press_key(BW_KEY_B);
        }
    }
    if (eidos_ui_right_click() && (s.state == S_LIST || s.state == S_QTY || s.state == S_YESNO)) {
        press_key(BW_KEY_B);
    }

    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, MODAL_FILL, EDGE_BLUE, 245);
    {
        int tx;
        char money[40];

        ui_fill(c, mx + 6, my + 6, mw - 12, hdr - 3, UI_BODY2, 255);
        ui_fill(c, mx + 6, my + 6 + hdr - 3, mw - 12, 1, ACCENT, 255);
        for (i = 0; i < 5; i++) {
            ui_fill(c, mx + 10 + i, my + 10 + i, 1, 11 - 2 * i, ACCENT, 255);
        }
        tx = ui_text(c, mx + 20, my + 6 + ui_text_y(hdr - 3, UI_FONT_LARGE), "MART", UI_WHITE,
                     UI_EDGE, UI_FONT_LARGE, 1);
        ui_line(c, tx + 6, my + 6 + 15, tx + 11, my + 6 + 4, UI_EDGE2, 255);
        ui_text(c, tx + 15, my + 6 + ui_text_y(hdr - 3, UI_FONT_LARGE), s.bp ? "BATTLE POINTS" : "BUY",
                UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        if (s.bp) {
            snprintf(money, sizeof money, "%u BP", currency(&s));
        } else {
            snprintf(money, sizeof money, YEN "%u", currency(&s));
        }
        ui_text(c, mx + mw - 12 - ui_text_w(money, UI_FONT_LARGE), my + 6 + ui_text_y(hdr - 3, UI_FONT_LARGE),
                money, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    }

    body_y = my + 6 + hdr + 4;
    body_h = my + mh - 6 - body_y;
    det_w = imax(mw < 360 ? 110 : 140, (mw - 2 * pad) * 40 / 100);
    det_x = mx + mw - pad - det_w;
    list_x = mx + pad;
    list_w = det_x - 6 - list_x;
    total = s.count + 1;
    rowh = 24;
    rows = imax(1, imin(total, (body_h + 2) / (rowh + 2)));
    rowh = iclamp((body_h - (rows - 1) * 2) / rows, 22, 30);
    if (s.cursor < g_top) {
        g_top = s.cursor;
    } else if (s.cursor >= g_top + rows) {
        g_top = s.cursor - rows + 1;
    }
    g_top = iclamp(g_top, 0, imax(0, total - rows));
    sb = total > rows ? 8 : 0;
    rw = list_w - sb;

    for (i = 0; i < rows; i++) {
        int idx = g_top + i, ry = body_y + i * (rowh + 2);
        int st = eidos_ui_hit(rect(list_x, ry, rw, rowh));

        if (idx >= total) {
            break;
        }
        if (st & EIDOS_UI_HOVER) {
            hover_row = idx;
        }
        if (idx < s.count) {
            u32 id = rd16(t, T_ITEMS + 8u * (u32)idx);
            u32 price = rd32(t, T_ITEMS + 8u * (u32)idx + 4u);

            if (st & EIDOS_UI_HOVER) {
                hover_item = (int)id;
            }
            if (idx == s.cursor) {
                sel_item = (int)id;
            }
            price_text(&s, price, buf, (int)sizeof buf);
            list_row(c, list_x, ry, rw, rowh, ui_item_icon((u16)id), item_name(&s, id), buf,
                     idx == s.cursor && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) && browse);
        } else {
            list_row(c, list_x, ry, rw, rowh, NULL, quit_label(&s), NULL,
                     idx == s.cursor && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) && browse);
        }
        if ((st & EIDOS_UI_CLICK) && s.state == S_LIST) {
            set_goal(idx, 1);
            if (g_log == 1) fprintf(stderr, "ui: shop click row %d cursor %d" "\n", idx, s.cursor);
        }
    }
    if (sb != 0) {
        int sh = rows * (rowh + 2) - 2;
        int sx = list_x + rw + 3;

        ui_scrollbar(c, sx, body_y, sh, g_top, rows, total, ACCENT);
    }
    {
        int wh = eidos_ui_wheel(rect(list_x, body_y, list_w, rows * (rowh + 2)));

        if (wh != 0 && s.state == S_LIST) {
            g_wheel += wh;
        }
    }
    if (g_wheel != 0 && s.state == S_LIST && now >= g_wheel_next && g_goal < 0) {
        press_key(g_wheel > 0 ? BW_KEY_UP : BW_KEY_DOWN);
        g_wheel += g_wheel > 0 ? -1 : 1;
        g_wheel_next = now + 4;
    } else if (s.state != S_LIST) {
        g_wheel = 0;
    }
    walk_list(&s, now);
    walk_yesno(&s, now);

    /* ---- the details ------------------------------------------------ */
    {
        int shown = hover_item != 0 && browse ? hover_item : sel_item;

        ui_panel(c, det_x, body_y, det_w, body_h, UI_CUT_TR | UI_CUT_BL, 8, MODAL_FILL2, EDGE_BLUE, 255);
        if (shown != 0) {
            const char *nm = item_name(&s, (u32)shown);
            const UiImage *big = ui_item_icon((u16)shown);
            int ix = det_x + det_w / 2, iy = body_y + 8, ty0 = iy + 50, hav = 0, k;
            u32 price = 0;

            for (k = 0; k < s.count; k++) {
                if (rd16(t, T_ITEMS + 8u * (u32)k) == (u32)shown) {
                    price = rd32(t, T_ITEMS + 8u * (u32)k + 4u);
                    break;
                }
            }
            ui_ellipse(c, ix - 28, iy + 40, 56, 14, 0x0A2A4Cu, UI_NONE, 255);
            if (big != NULL) {
                ui_image(c, ix - big->w, iy, big, 2);
            }
            ui_text(c, det_x + (det_w - ui_text_w(nm, UI_FONT_LARGE)) / 2, ty0, nm, UI_WHITE, SHADOW,
                    UI_FONT_LARGE, 1);
            ty0 += 18;
            if (!s.tm && MYITEM_SAVEDATA_GetItemQuantity != NULL && rdp(t, T_BAG) != NULL) {
                hav = (int)MYITEM_SAVEDATA_GetItemQuantity(rdp(t, T_BAG), (u32)shown, rd16(t, T_HEAP));
                snprintf(buf, sizeof buf, "In bag \xC3\x97%d", hav);
                ui_text(c, det_x + (det_w - ui_text_w(buf, UI_FONT_SMALL)) / 2, ty0, buf, ACCENT,
                        SHADOW, UI_FONT_SMALL, 1);
                ty0 += 14;
            }
            (void)price;
            draw_description(c, det_x + 8, ty0 + 2, det_w - 16, body_y + body_h - 6 - (ty0 + 2),
                             item_desc(&s, (u32)shown));
        }
    }
    (void)hover_row;

    if (!browse && s.state != S_INIT) {
        draw_dialog(c, &s, mx, my, mw, mh);
    }

    /* ---- the key hints ---------------------------------------------- */
    {
        Hint h[4];
        int n = 0;

        if (s.state == S_QTY) {
            h[n].key = "A"; h[n].what = "Buy"; h[n].keymask = BW_KEY_A; n++;
            h[n].key = "B"; h[n].what = "Back"; h[n].keymask = BW_KEY_B; n++;
            h[n].key = "UP DOWN"; h[n].what = "Amount"; h[n].keymask = BW_KEY_UP; n++;
        } else if (s.state == S_YESNO) {
            h[n].key = "A"; h[n].what = "Choose"; h[n].keymask = BW_KEY_A; n++;
            h[n].key = "B"; h[n].what = "No"; h[n].keymask = BW_KEY_B; n++;
        } else if (!browse) {
            h[n].key = "A"; h[n].what = "Continue"; h[n].keymask = BW_KEY_A; n++;
        } else {
            h[n].key = "A"; h[n].what = "Select"; h[n].keymask = BW_KEY_A; n++;
            h[n].key = "B"; h[n].what = "Leave"; h[n].keymask = BW_KEY_B; n++;
        }
        hints_bar(c, hints_y, cw, h, n);
    }
}

static void reset_shop(void)
{
    g_seen = NULL;
    g_goal = g_yn_goal = -1;
    g_top = 0;
    g_wheel = 0;
    g_next = 0;
    caches_clear();
}

static const EidosUiRecipe recipe_shop = { "shop", match_shop, run_shop, reset_shop, 0 };

const EidosUiRecipe *const ui_recipes_shop[] = { &recipe_shop };
const u32 ui_recipes_shop_count = 1;
