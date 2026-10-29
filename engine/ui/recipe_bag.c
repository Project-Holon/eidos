/*
 * engine/ui/recipe_bag.c -- the one-screen recipe for the field Bag
 * (ov070, "ITEMMENU"; docs/unified-view-recipes.md, section 3).
 *
 * The Bag is a modal over the blurred world: five pocket tabs, the item
 * list (the game's item icons, counts), the selected item's details (the
 * icon at 2x, the name, the game's own description text) and, when an item
 * is chosen, the action list (USE, GIVE, TRASH, REGISTER, ...).
 *
 * The game keeps all the logic.  Everything shown is read from the game's
 * own work area (ItemMenuWork, 32-bit layout, offsets in the comments) and
 * from its own message banks; every control becomes the bottom-screen tap
 * the game tests for (the BUTTON_MAN table sButtonHitTable, the pop-up
 * list's run-time rectangles) or a button press.
 *
 * Sub-screens the recipe does not redraw (the quantity chooser, yes/no
 * questions, messages, the move-an-item mode) show the game's own bottom
 * screen in the middle, and a click on it is a tap at that point, so no
 * state of the Bag can leave the player stuck.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "input.h"
#include "overlay.h"

/* ------------------------------------------------------------------ */
/* The game                                                            */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
#define BAG_WEAK __attribute__((weak))
#else
#define BAG_WEAK
#endif

/* The game system (src/game/arm9/gamesystem.c), the way sdk/mp.c reads it. */
extern void *bw_dsref__GameSystemGlobal BAG_WEAK;

/* src/game/arm9: the bag, the messages, the input mode. */
extern u32   MYITEM_SAVEDATA_CountUsedSlotsInPocket(void *bag, u32 pocket) BAG_WEAK;
extern void *MYITEM_SAVEDATA_GetSlot(void *bag, u32 pocket, u32 index) BAG_WEAK;
extern void  MSGDATA_ReadString(void *bank, u32 index, void *dest) BAG_WEAK;
extern void *MSGDATA_CreateFromArchive(u32 a, u32 b, u32 bank, u32 heap) BAG_WEAK;
extern void  MSGDATA_Delete(void *bank) BAG_WEAK;
extern void  FILE_UTIL_ReadString(void *dst, u32 entry, u32 heap) BAG_WEAK;
extern void  FILE_UTIL_ReadStringAlt(void *dst, u32 entry, u32 heap) BAG_WEAK;
extern u32   ITEM_GetRegisterFlagId(u32 id) BAG_WEAK;
extern u32   GAME_DATA_ContainsRecentId(void *gameData, u32 id) BAG_WEAK;
extern u8    UI_GetInputMode(void) BAG_WEAK;
extern void  UI_SetInputMode(u32 value) BAG_WEAK;

/* src/game/ov070/itemmenu.c: the task functions, by the overlay link's names. */
extern void ov070_ITEMMENU_TaskBagMain(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskListInput(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskSelectItemFlash(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskSelectItem(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskOpenActionMenu(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskActionMenuInput(void *) BAG_WEAK;
extern void ov070_ITEMMENU_HandleActionMenuChoice(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskStartFadeIn(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitFadeIn(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitPocketTabPress(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitSideButtonPress(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskBeginReturnToList(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskReturnToListFromQuantity(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskReopenListFromQuantity(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskReturnFromQuantity(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitMessage(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitMessageDismiss(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskWaitMessageThenList(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskReturnToListAfterMessage(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskUseNoticeMessage(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskRegisterResultMessage(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskStartRegisterResult(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskInputGuardMessage(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskOpenTossConfirm(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskTossAskQuantity(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskTossQuantityInput(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskTossConfirmInput(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskSellQuantityInput(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskSellAskConfirm(void *) BAG_WEAK;
extern void ov070_ITEMMENU_TaskSellConfirmInput(void *) BAG_WEAK;

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

static void *rdp(const void *p, u32 off)
{
    return (void *)(uintptr_t)rd32(p, off);
}

/* ItemMenuWork (src/game/ov070/itemmenu_types.h). */
#define W_BAGMODE     0x010u
#define W_BAG         0x018u
#define W_BAGFILTER   0x01Cu
#define W_ITEMS       0x020u    /* BagSlot[310]: the move mode's copy      */
#define W_TASKFN      0x4F8u
#define W_MSGOPEN     0x514u
#define W_MSGBANK     0x51Cu
#define W_HEAP        0x544u    /* u16                                     */
#define W_MENULIST    0x77Cu    /* TaskMenuWork*, non-zero with a pop-up   */
#define W_SAVEDATA    0x000u
#define W_POCKET      0x80Cu
#define W_CURSORROW   0x810u
#define W_SCROLL      0x814u
#define W_WORKCOPY    0x81Cu
#define W_ACTIONS     0x828u    /* u32[4]: the pop-up's action ids         */

/* TaskMenuWork (src/game/arm9/app_taskmenu.c). */
#define TM_CURSOR     0x00u     /* u8                                      */
#define TM_ITEMNUM    0x24u     /* u8                                      */
#define TM_POSTYPE    0x2Cu
#define TM_CHARX      0x30u     /* u8                                      */
#define TM_CHARY      0x31u     /* u8                                      */
#define TM_WIDTH      0x32u     /* u8, in 8 px characters                  */
#define TM_HEIGHT     0x33u     /* u8                                      */

#define POCKETS 5
#define ROWS    6
#define ITEM_ID_MAX 0x280

/* Bank 3 of a/0/0/2 is the bag's own: the labels and the empty state. */
#define MSG_USE      0
#define MSG_EMPTY    135
/* The game's action id to message id table (sActionMsgIds, ov070
 * 0x021BB998): USE, WALK, CHECK, GIVE, TRASH, REGISTER, DESELECT, ... */
static const u8 k_action_msg[11] = { 0, 6, 16, 1, 2, 18, 3, 5, 8, 87, 0 };

/* The pocket names, bank 0x37 of a/0/0/2 (the game's own text, used when
 * the bank cannot be read). */
static const char *const k_pocket_fallback[POCKETS] = {
    "Items", "Medicine", "TMs & HMs", "Berries", "Key Items"
};

/* The bottom-screen tap centres of the pocket tabs, by pocket id
 * (sButtonHitTable entries 4, 0, 1, 2, 3 through sTabPockets). */
static const int k_tab_tap[POCKETS][2] = {
    { 31, 72 }, { 18, 123 }, { 55, 124 }, { 92, 111 }, { 89, 74 }
};
/* The row taps: the list row r (sButtonHitTable entries 11 to 16) and the
 * registration toggle of that row (entries 17 to 22). */
static int row_tap_y(int r) { return 23 + 24 * r; }
#define ROW_TAP_X      183
#define REG_TAP_X      131
/* The bottom row of buttons: previous and next pocket, sort. */
#define PREV_TAP_X     12
#define NEXT_TAP_X     131
#define SORT_TAP_X     163
#define BUTTON_TAP_Y   180

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static void *g_seen_work;       /* the work area the caches belong to      */
static int   g_wheel_pending;
static int   g_was_quantity;    /* the chooser's prompt is kept in g_prompt */
static char  g_prompt[256];
static u32   g_wheel_next;

/* ------------------------------------------------------------------ */
/* Text from the game's banks                                          */
/* ------------------------------------------------------------------ */

typedef struct BagStr {         /* a Strbuf (arm9/strbuf_types.h)          */
    u16 capacity;
    u16 length;
    u32 magic;
    u16 chars[512];
} BagStr;

#define BAG_STRBUF_MAGIC 0xB6F8D2ECu

static void bagstr_init(BagStr *s)
{
    s->capacity = 512;
    s->length = 0;
    s->magic = BAG_STRBUF_MAGIC;
    s->chars[0] = 0xFFFFu;
}

/* The game's 16-bit text as UTF-8; 0xFFFE and 0xE000 are line breaks the
 * kit wraps itself. */
static void units_to_utf8(const u16 *u, char *out, int max)
{
    int n = 0, i;

    for (i = 0; i < 511 && u[i] != 0xFFFFu; i++) {
        u32 c = u[i];

        if (c == 0xFFFEu || c == 0xE000u || c == 0x000Au) {
            c = ' ';
        } else if (c >= 0xD800u && c <= 0xF8FFu) {
            continue;       /* control and private codes */
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

/* Per work area caches: the names, descriptions and the bank texts. */
static char g_name[ITEM_ID_MAX][40];
static u8   g_name_ok[ITEM_ID_MAX];
static char g_desc[ITEM_ID_MAX][320];
static u8   g_desc_ok[ITEM_ID_MAX];
static char g_msg[160][64];
static u8   g_msg_ok[160];
static char g_pocket[POCKETS][40];
static u8   g_pocket_ok[POCKETS];

static void caches_clear(void)
{
    memset(g_name_ok, 0, sizeof g_name_ok);
    memset(g_desc_ok, 0, sizeof g_desc_ok);
    memset(g_msg_ok, 0, sizeof g_msg_ok);
    memset(g_pocket_ok, 0, sizeof g_pocket_ok);
}

static const char *item_name(const void *work, u32 id)
{
    BagStr s;

    if (id == 0 || id >= ITEM_ID_MAX) {
        return "";
    }
    if (!g_name_ok[id]) {
        g_name[id][0] = '\0';
        if (FILE_UTIL_ReadString != NULL) {
            bagstr_init(&s);
            FILE_UTIL_ReadString(&s, id, rd16(work, W_HEAP));
            units_to_utf8(s.chars, g_name[id], (int)sizeof g_name[id]);
        }
        g_name_ok[id] = 1;
    }
    return g_name[id];
}

static const char *item_desc(const void *work, u32 id)
{
    BagStr s;

    if (id == 0 || id >= ITEM_ID_MAX) {
        return "";
    }
    if (!g_desc_ok[id]) {
        g_desc[id][0] = '\0';
        if (FILE_UTIL_ReadStringAlt != NULL) {
            bagstr_init(&s);
            FILE_UTIL_ReadStringAlt(&s, id, rd16(work, W_HEAP));
            units_to_utf8(s.chars, g_desc[id], (int)sizeof g_desc[id]);
        }
        g_desc_ok[id] = 1;
    }
    return g_desc[id];
}

/* A message of the bag's own bank (bank 3), through the game's open bank. */
static const char *bag_msg(const void *work, u32 index)
{
    BagStr s;

    if (index >= 160) {
        return "";
    }
    if (!g_msg_ok[index]) {
        void *bank = rdp(work, W_MSGBANK);

        g_msg[index][0] = '\0';
        if (bank != NULL && MSGDATA_ReadString != NULL) {
            bagstr_init(&s);
            MSGDATA_ReadString(bank, index, &s);
            units_to_utf8(s.chars, g_msg[index], (int)sizeof g_msg[index]);
        }
        g_msg_ok[index] = 1;
    }
    return g_msg[index];
}

/* ------------------------------------------------------------------ */
/* Reading the game's Bag                                              */
/* ------------------------------------------------------------------ */

typedef struct BagView {
    void *work;
    int   pocket;
    int   cursor_row;
    int   scroll;           /* first visible entry is scroll + 1           */
    int   count;            /* entries in the shown list                   */
    int   mode;             /* bagMode                                     */
    int   popup;            /* the action list is up                       */
    int   other;            /* a sub-screen the recipe does not redraw     */
    int   dialog;           /* a message, a question or the quantity chooser */
    int   quantity;         /* the quantity chooser is up                  */
    int   yesno;            /* a yes/no list is up                         */
    int   waiting;          /* a message waits for a key (A advances it)   */
} BagView;

static int task_is(const void *work, void (*fn)(void *))
{
    return fn != NULL && (void *)(uintptr_t)rd32(work, W_TASKFN) == (void *)fn;
}

/* The Bag's work area while the Bag is the app on top, else NULL. */
static void *bag_work(void)
{
    void *gs, *procsys, *cur, *work;

    if (!bw_overlay_is_loaded(70)) {
        return NULL;
    }
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL ||
        MYITEM_SAVEDATA_GetSlot == NULL || MYITEM_SAVEDATA_CountUsedSlotsInPocket == NULL) {
        return NULL;
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    if (gs == NULL) {
        return NULL;
    }
    procsys = rdp(gs, 0x08);
    if (procsys == NULL) {
        return NULL;
    }
    cur = rdp(procsys, 0x08);
    if (cur == NULL || (int)rd32(cur, 0x04) != 70 || (int)rd32(cur, 0x08) != 2) {
        return NULL;
    }
    work = rdp(cur, 0x14);
    if (work == NULL || rdp(work, W_BAG) == NULL) {
        return NULL;
    }
    return work;
}

static const u16 *bag_slot(const BagView *v, int index)
{
    if (index < 0 || index >= v->count) {
        return NULL;
    }
    if (rd32(v->work, W_WORKCOPY) != 0) {
        return (const u16 *)((const u8 *)v->work + W_ITEMS) + 2 * index;
    }
    return (const u16 *)MYITEM_SAVEDATA_GetSlot(rdp(v->work, W_BAG), (u32)v->pocket, (u32)index);
}

static int bag_read(BagView *v, void *work)
{
    int i;

    memset(v, 0, sizeof *v);
    v->work = work;
    v->pocket = (int)rd32(work, W_POCKET);
    v->cursor_row = (int)rd32(work, W_CURSORROW);
    v->scroll = (int)rd32(work, W_SCROLL);
    v->mode = (int)rd32(work, W_BAGMODE);
    if (v->pocket < 0 || v->pocket >= POCKETS) {
        return 0;
    }
    if (rd32(work, W_WORKCOPY) != 0) {
        const u16 *items = (const u16 *)((const u8 *)work + W_ITEMS);

        for (i = 0; i < 310 && items[2 * i] != 0; i++) {
        }
        v->count = i;
    } else {
        v->count = (int)MYITEM_SAVEDATA_CountUsedSlotsInPocket(rdp(work, W_BAG), (u32)v->pocket);
    }
    v->popup = rdp(work, W_MENULIST) != NULL &&
               (task_is(work, ov070_ITEMMENU_HandleActionMenuChoice) ||
                task_is(work, ov070_ITEMMENU_TaskActionMenuInput));
    v->quantity = task_is(work, ov070_ITEMMENU_TaskTossQuantityInput) ||
                  task_is(work, ov070_ITEMMENU_TaskTossAskQuantity) ||
                  task_is(work, ov070_ITEMMENU_TaskSellQuantityInput);
    v->yesno = rdp(work, W_MENULIST) != NULL && !v->popup &&
               (task_is(work, ov070_ITEMMENU_TaskTossConfirmInput) ||
                task_is(work, ov070_ITEMMENU_TaskSellConfirmInput));
    v->waiting = task_is(work, ov070_ITEMMENU_TaskWaitMessage) ||
                 task_is(work, ov070_ITEMMENU_TaskWaitMessageDismiss) ||
                 task_is(work, ov070_ITEMMENU_TaskWaitMessageThenList) ||
                 task_is(work, ov070_ITEMMENU_TaskReturnToListAfterMessage) ||
                 task_is(work, ov070_ITEMMENU_TaskUseNoticeMessage) ||
                 task_is(work, ov070_ITEMMENU_TaskRegisterResultMessage) ||
                 task_is(work, ov070_ITEMMENU_TaskInputGuardMessage);
    v->dialog = v->quantity || v->yesno || v->waiting ||
                task_is(work, ov070_ITEMMENU_TaskOpenTossConfirm) ||
                task_is(work, ov070_ITEMMENU_TaskSellAskConfirm) ||
                task_is(work, ov070_ITEMMENU_TaskStartRegisterResult);
    /* States the recipe draws: the list (also while a pick flashes and the
     * pop-up opens, during the opening and while the game goes back to the
     * list), the action pop-up and the dialogs.  Anything else (the move
     * mode) shows the game's bottom screen. */
    v->other = !(v->popup || v->dialog || task_is(work, ov070_ITEMMENU_TaskBagMain) ||
                 task_is(work, ov070_ITEMMENU_TaskSelectItemFlash) ||
                 task_is(work, ov070_ITEMMENU_TaskSelectItem) ||
                 task_is(work, ov070_ITEMMENU_TaskOpenActionMenu) ||
                 task_is(work, ov070_ITEMMENU_TaskStartFadeIn) ||
                 task_is(work, ov070_ITEMMENU_TaskWaitFadeIn) ||
                 task_is(work, ov070_ITEMMENU_TaskWaitPocketTabPress) ||
                 task_is(work, ov070_ITEMMENU_TaskWaitSideButtonPress) ||
                 task_is(work, ov070_ITEMMENU_TaskBeginReturnToList) ||
                 task_is(work, ov070_ITEMMENU_TaskReturnToListFromQuantity) ||
                 task_is(work, ov070_ITEMMENU_TaskReopenListFromQuantity) ||
                 task_is(work, ov070_ITEMMENU_TaskReturnFromQuantity));
    if (rd32(work, W_TASKFN) == 0) {
        v->other = 0;       /* closing: keep the picture */
        v->dialog = 0;
    }
    {
        /* A state the list above does not name, for a frame or two (a
         * pocket change passes through one), is not the move mode: the
         * game's own screen shows only once such a state lasts. */
        static int unknown_frames;

        if (v->other && !task_is(work, ov070_ITEMMENU_TaskListInput)) {
            unknown_frames++;
            if (unknown_frames < 4) {
                v->other = 0;
            }
        } else {
            unknown_frames = 0;
        }
    }
    return 1;
}

static const char *pocket_name(const BagView *v, int p)
{
    /* bank 0x37 of a/0/0/2 holds the pocket names; the bag does not keep it
     * open, so open it once for all five */
    if (!g_pocket_ok[p]) {
        int i;
        void *bank = NULL;

        if (MSGDATA_CreateFromArchive != NULL && MSGDATA_Delete != NULL &&
            MSGDATA_ReadString != NULL) {
            bank = MSGDATA_CreateFromArchive(0, 2, 0x37, rd16(v->work, W_HEAP));
        }
        for (i = 0; i < POCKETS; i++) {
            BagStr s;

            g_pocket[i][0] = '\0';
            if (bank != NULL) {
                bagstr_init(&s);
                MSGDATA_ReadString(bank, (u32)i, &s);
                units_to_utf8(s.chars, g_pocket[i], (int)sizeof g_pocket[i]);
            }
            if (g_pocket[i][0] == '\0') {
                snprintf(g_pocket[i], sizeof g_pocket[i], "%s", k_pocket_fallback[i]);
            }
            g_pocket_ok[i] = 1;
        }
        if (bank != NULL) {
            MSGDATA_Delete(bank);
        }
    }
    return g_pocket[p];
}

/* ------------------------------------------------------------------ */
/* Input to the game                                                   */
/* ------------------------------------------------------------------ */

/* A key press the game acts on: after a touch the game only brings its key
 * cursor back on the first key (ITEMMENU_TaskBagMain, UI_GetInputMode), so
 * the input mode is set to "keys" first, as that key would. */
static void press_key(u32 key)
{
    if (UI_GetInputMode != NULL && UI_SetInputMode != NULL && UI_GetInputMode() != 0) {
        UI_SetInputMode(0);
    }
    eidos_ui_press(key, 2);
}

static void tap_ds(int x, int y)
{
    eidos_ui_tap(x, y, 3);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define MODAL_FILL   0x2A0814u
#define MODAL_FILL2  0x1E060Eu
#define ROW_FILL     0x3C0A1Au
#define ROW_EMPTY    0x240A12u

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

/* The key hints bar: each hint is a button.  action: 0 press key, 1 tap. */
typedef struct Hint {
    const char *key, *what;
    u32 keymask;
    int tap_x, tap_y;       /* tap_x < 0: press keymask                    */
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
            if (h[i].tap_x >= 0) {
                tap_ds(h[i].tap_x, h[i].tap_y);
            } else {
                press_key(h[i].keymask);
            }
        }
        ui_keycap(c, x, y + 2, h[i].key);
        x += kw + 4;
        x = ui_text(c, x, y + 2 + ui_text_y(13, UI_FONT_SMALL), h[i].what, UI_LIGHT,
                    UI_EDGE, UI_FONT_SMALL, 1) + 10;
    }
}

/* The description, wrapped to the box. */
typedef struct DescPaint {
    EidosUiCtx *c;
    int x, y, line_h, max_y, font, n;
} DescPaint;

static void desc_line(void *user, const char *s, int len)
{
    DescPaint *d = (DescPaint *)user;
    char tmp[128];

    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    if (d->y + d->line_h <= d->max_y) {
        ui_text(d->c, d->x, d->y, tmp, UI_LIGHT, UI_RED2, d->font, 1);
    }
    d->y += d->line_h;
    d->n++;
}

static void draw_description(EidosUiCtx *c, int x, int y, int w, int h, const char *text)
{
    DescPaint d;
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
    d.n = 0;
    ui_wrap(text, w, d.font, desc_line, &d);
}

static void item_row(EidosUiCtx *c, int x, int y, int w, int h, const UiImage *icon,
                     const char *name, int count, int selected, int hover)
{
    int ty = ui_text_y(h, UI_FONT_LARGE), tx = 6;
    u32 fill = selected ? UI_PINK2 : (hover ? 0x5A1530u : ROW_FILL);

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, fill, selected ? UI_WHITE : UI_RED, 255);
    if (icon != NULL) {
        ui_image(c, x + 4, y + (h - icon->h) / 2, icon, 1);
        tx = 4 + icon->w + 4;
    }
    {
        EidosUiRect saved;
        char s[16];
        int room = w - tx - 6;

        s[0] = '\0';
        if (count > 0) {
            snprintf(s, sizeof s, "\xC3\x97%d", count);
            room -= ui_text_w(s, UI_FONT_LARGE) + 6;
        }
        ui_push(c, 0, 0, &saved);
        ui_clip(c, x + tx, y, imax(room, 8), h);
        ui_text(c, x + tx, y + ty, name, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
        ui_pop(c, 0, 0, &saved);
        if (count > 0) {
            ui_text(c, x + w - 8 - ui_text_w(s, UI_FONT_LARGE), y + ty, s, UI_WHITE, UI_RED2,
                    UI_FONT_LARGE, 1);
        }
    }
}

/* A small round Y badge: the item is registered to the Y button. */
static void reg_badge(EidosUiCtx *c, int x, int y, int on, int hover)
{
    ui_ellipse(c, x, y, 12, 12, on ? UI_BAGY : UI_BODY, on ? UI_INK : UI_EDGE2, 255);
    if (hover && !on) {
        ui_ellipse(c, x + 1, y + 1, 10, 10, UI_NONE, UI_BAGY, 255);
    }
    ui_text(c, x + (12 - ui_text_w("Y", UI_FONT_SMALL)) / 2, y + ui_text_y(12, UI_FONT_SMALL), "Y",
            on ? UI_INK : UI_GREY, -1, UI_FONT_SMALL, 1);
}

static int is_registrable(u32 id)
{
    return ITEM_GetRegisterFlagId != NULL && ITEM_GetRegisterFlagId(id) != 0x19;
}

static int is_registered(const BagView *v, u32 id)
{
    void *save = rdp(v->work, W_SAVEDATA);

    if (ITEM_GetRegisterFlagId == NULL || GAME_DATA_ContainsRecentId == NULL || save == NULL) {
        return 0;
    }
    return GAME_DATA_ContainsRecentId(save, ITEM_GetRegisterFlagId(id)) == 1;
}

/* The game's own bottom screen, in the middle, for the states the recipe
 * does not redraw; a click is a tap at that point. */
static void draw_game_pane(EidosUiCtx *c, const EidosUiFrame *in, int cw, int hints_y)
{
    const int cell = 4;
    int k = 1, x, y, cx, cy;
    int avail_h = hints_y - 6;

    if (in->screen_rgba[1] == NULL) {
        return;
    }
    if (avail_h >= 192 * 2 && cw >= 256 * 2) {
        k = 2;
    }
    x = (cw - 256 * k) / 2;
    y = imax(2, (avail_h - 192 * k) / 2 + 2);
    ui_panel(c, x - 4, y - 4, 256 * k + 8, 192 * k + 8, UI_CUT_TL | UI_CUT_BR, 6, MODAL_FILL,
             UI_RED, 255);
    ui_blit_screen(c, x, y, in->screen_rgba[1], 0, 0, 256, 192, k, 0);
    for (cy = 0; cy < 192; cy += cell) {
        for (cx = 0; cx < 256; cx += cell) {
            int st = eidos_ui_hit(rect(x + cx * k, y + cy * k, cell * k, cell * k));

            if (st & EIDOS_UI_CLICK) {
                tap_ds(cx + cell / 2, cy + cell / 2);
            }
        }
    }
}

/* The tap that picks entry i of the game's pop-up list (arm9/app_taskmenu.c:
 * each entry's rectangle is built at run time from its template, the
 * template's position being the bottom right corner when posType is not 0).
 * Every pop-up list of the Bag (actions, yes/no) is one. */
static void menu_tap(const void *menu, int i, int n)
{
    u32 posType = rd32(menu, TM_POSTYPE);
    u32 pk = rd32(menu, TM_CHARX);
    int cx = (int)(pk & 0xFFu), cy = (int)((pk >> 8) & 0xFFu);
    int width = (int)((pk >> 16) & 0xFFu), height = (int)((pk >> 24) & 0xFFu);
    int posx = posType != 0 ? cx - width : cx;
    int posy = posType != 0 ? cy - height * n : cy;

    tap_ds(posx * 8 + width * 4, posy * 8 + 8 * height * i + height * 4);
}

/* A stack of choice buttons for a pop-up list, from (x, y), going down. */
static void menu_buttons(EidosUiCtx *c, const BagView *v, const char *const *labels, int n,
                         int x, int y, int bw, int bh, int gap)
{
    void *menu = rdp(v->work, W_MENULIST);
    int cursor = menu != NULL ? (int)(rd32(menu, TM_CURSOR) & 0xFFu) : -1;
    int hover = -1, i;
    int keys = UI_GetInputMode != NULL && UI_GetInputMode() == 0;

    for (i = 0; i < n; i++) {
        if (eidos_ui_hit(rect(x, y + i * (bh + gap), bw, bh)) & EIDOS_UI_HOVER) {
            hover = i;
        }
    }
    if (hover >= 0 && menu != NULL && hover != cursor && hover < n) {
        /* the game's cursor follows the mouse, so a key goes on from the
         * entry the mouse was on */
        *((u8 *)menu + TM_CURSOR) = (u8)hover;
    }
    for (i = 0; i < n; i++) {
        int ry = y + i * (bh + gap);
        int st = eidos_ui_hit(rect(x, ry, bw, bh));
        int lit = hover >= 0 ? hover == i : (keys && cursor == i);

        ui_panel(c, x, ry, bw, bh, UI_CUT_TL | UI_CUT_BR, 4, lit ? UI_PINK2 : ROW_FILL,
                 lit ? UI_WHITE : UI_RED, 255);
        ui_text(c, x + (bw - ui_text_w(labels[i], UI_FONT_LARGE)) / 2,
                ry + ui_text_y(bh, UI_FONT_LARGE), labels[i], UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
        if ((st & EIDOS_UI_CLICK) && menu != NULL) {
            menu_tap(menu, i, n);
        }
    }
}

/* The action list, over the list, next to the picked row. */
static void draw_popup(EidosUiCtx *c, const BagView *v, int lx, int ly, int lw, int lh, int rowh)
{
    void *menu = rdp(v->work, W_MENULIST);
    const char *labels[4];
    int n, i, bw = 84, bh = 20, gap = 3, x, y, total;

    if (menu == NULL) {
        return;
    }
    n = (int)(rd32(menu, TM_ITEMNUM) & 0xFFu);
    if (n < 1 || n > 4) {
        return;
    }
    for (i = 0; i < n; i++) {
        int id = (int)rd32(v->work, W_ACTIONS + 4u * (u32)i);

        labels[i] = bag_msg(v->work, k_action_msg[id < 0 || id > 10 ? 10 : id]);
        bw = imax(bw, ui_text_w(labels[i], UI_FONT_LARGE) + 24);
    }
    total = n * bh + (n - 1) * gap + 12;
    x = lx + lw - bw - 6;
    y = ly + iclamp(v->cursor_row, 0, ROWS - 1) * (rowh + 2);
    y = iclamp(y + 6, ly + 6, ly + lh - total + 6);
    ui_panel(c, x - 6, y - 6, bw + 12, total, UI_CUT_TL | UI_CUT_BR, 6, MODAL_FILL2, UI_PINK, 250);
    menu_buttons(c, v, labels, n, x, y, bw, bh, gap);
}

/* The text of the game's current message (Strbuf at +0x528), as UTF-8. */
static void message_text(const BagView *v, char *out, int max)
{
    const u8 *sb = (const u8 *)rdp(v->work, 0x528u);
    u16 buf[512];
    int len, i;

    out[0] = '\0';
    if (sb == NULL || *(const u32 *)(sb + 4) != BAG_STRBUF_MAGIC) {
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

typedef struct WrapPaint {
    EidosUiCtx *c;
    int x, y;
} WrapPaint;

static void wrap_line(void *user, const char *s, int len)
{
    WrapPaint *w = (WrapPaint *)user;
    char tmp[160];

    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    ui_text(w->c, w->x, w->y, tmp, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
    w->y += 16;
}

/* A button with a word or a symbol; returns non-zero on a click. */
static int small_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label)
{
    int st = eidos_ui_hit(rect(x, y, w, h));

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, (st & EIDOS_UI_HOVER) ? UI_PINK2 : ROW_FILL,
             (st & EIDOS_UI_HOVER) ? UI_WHITE : UI_RED, 255);
    ui_text(c, x + (w - ui_text_w(label, UI_FONT_LARGE)) / 2, y + ui_text_y(h, UI_FONT_LARGE),
            label, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
    return (st & EIDOS_UI_CLICK) != 0;
}

/* The dialog over the Bag: the game's own message, and what the game waits
 * for (a key, a yes/no choice, or the quantity). */
static void draw_dialog(EidosUiCtx *c, const BagView *v, int mx, int my, int mw, int mh)
{
    char msg[256];
    int dw = imin(mw - 24, 300), lines, dh, dx, dy, i;
    int row_h = 20;
    WrapPaint wp;

    message_text(v, msg, (int)sizeof msg);
    if (v->quantity) {
        /* the game overwrites the prompt with the count ("x3") while the
         * chooser runs: keep the prompt it printed first */
        if (!g_was_quantity || !(msg[0] == 'x' && msg[1] >= '0' && msg[1] <= '9')) {
            snprintf(g_prompt, sizeof g_prompt, "%s", msg);
        }
        g_was_quantity = 1;
        snprintf(msg, sizeof msg, "%s", g_prompt);
    } else {
        g_was_quantity = 0;
    }
    lines = ui_wrap(msg, dw - 24, UI_FONT_LARGE, NULL, NULL);
    if (lines < 1) {
        lines = 1;
    }
    dh = 12 + lines * 16 + ((v->quantity || v->yesno) ? row_h + 10 : 0) + 6;
    dx = mx + (mw - dw) / 2;
    dy = my + (mh - dh) / 2;
    ui_fill(c, mx + 6, my + 6, mw - 12, mh - 12, UI_INK, 120);
    if (v->waiting) {
        /* a click anywhere moves a message on, as A does */
        if (eidos_ui_hit(rect(0, 0, c->canvas->w, c->canvas->h)) & EIDOS_UI_CLICK) {
            press_key(BW_KEY_A);
        }
    }
    ui_panel(c, dx, dy, dw, dh, UI_CUT_TL | UI_CUT_BR, 8, MODAL_FILL2, UI_PINK, 255);
    wp.c = c;
    wp.x = dx + 12;
    wp.y = dy + 10;
    ui_wrap(msg, dw - 24, UI_FONT_LARGE, wrap_line, &wp);
    if (v->yesno) {
        void *menu = rdp(v->work, W_MENULIST);
        const char *labels[2];
        int bw = 56, by = dy + dh - row_h - 8;

        labels[0] = bag_msg(v->work, 132);
        labels[1] = bag_msg(v->work, 133);
        if (labels[0][0] == '\0') labels[0] = "YES";
        if (labels[1][0] == '\0') labels[1] = "NO";
        /* the game's rectangles are stacked; each button taps its own */
        for (i = 0; i < 2; i++) {
            int bx = dx + dw - 12 - (2 - i) * bw - (1 - i) * 6;
            int st = eidos_ui_hit(rect(bx, by, bw, row_h));
            int lit = (st & EIDOS_UI_HOVER) != 0;

            ui_panel(c, bx, by, bw, row_h, UI_CUT_TL | UI_CUT_BR, 4, lit ? UI_PINK2 : ROW_FILL,
                     lit ? UI_WHITE : UI_RED, 255);
            ui_text(c, bx + (bw - ui_text_w(labels[i], UI_FONT_LARGE)) / 2,
                    by + ui_text_y(row_h, UI_FONT_LARGE), labels[i], UI_WHITE, UI_RED2,
                    UI_FONT_LARGE, 1);
            if ((st & EIDOS_UI_CLICK) && menu != NULL) {
                menu_tap(menu, i, 2);
            }
        }
    } else if (v->quantity) {
        int cnt = (int)rd32(v->work, 0x7F4u);
        char num[24];
        int bx = dx + 12, by = dy + dh - row_h - 8, nw = 64, wh;

        if (small_button(c, bx, by, 24, row_h, "-")) {
            press_key(BW_KEY_DOWN);
        }
        snprintf(num, sizeof num, "\xC3\x97%d", cnt);
        ui_panel(c, bx + 28, by, nw, row_h, UI_CUT_TL | UI_CUT_BR, 3, ROW_EMPTY, UI_RED, 255);
        ui_text(c, bx + 28 + (nw - ui_text_w(num, UI_FONT_LARGE)) / 2,
                by + ui_text_y(row_h, UI_FONT_LARGE), num, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
        if (small_button(c, bx + 28 + nw + 4, by, 24, row_h, "+")) {
            press_key(BW_KEY_UP);
        }
        if (small_button(c, dx + dw - 12 - 56 * 2 - 6, by, 56, row_h, "OK")) {
            press_key(BW_KEY_A);
        }
        if (small_button(c, dx + dw - 12 - 56, by, 56, row_h, bag_msg(v->work, 8))) {
            press_key(BW_KEY_B);
        }
        wh = eidos_ui_wheel(rect(mx, my, mw, mh));
        if (wh != 0) {
            press_key(wh > 0 ? BW_KEY_UP : BW_KEY_DOWN);
        }
    }
}

static int match_bag(void)
{
    return bag_work() != NULL;
}

static void run_bag(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    BagView v;
    void *work = bag_work();
    int cw = c->canvas->w, ch = c->canvas->h;
    int hints_y, mx, my, mw, mh, pad = 8, hdr = 22;
    int tabs_x, tabs_w, list_x, list_w, det_x, det_w, body_y, body_h;
    int rowh, i, hover_row = -1, hover_item = 0, sel_item = 0;
    int browse_input;
    char title[48];
    u32 now = in->frame;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    if (work == NULL || !bag_read(&v, work)) {
        return;
    }
    if (work != g_seen_work) {
        g_seen_work = work;
        caches_clear();
        g_wheel_pending = 0;
    }
    hints_y = ch - 17;

    /* ---- the modal ------------------------------------------------- */
    mw = iclamp(cw * 90 / 100, 330, 520);
    mw = imin(mw, cw - 8);
    mh = iclamp(ch * 88 / 100, 200, 300);
    mh = imin(mh, hints_y - 4);
    mx = (cw - mw) / 2;
    my = (hints_y - mh) / 2;

    /* outside the modal: a click is B (not while a sub-screen is up, where
     * the pane holds the game's own screen) */
    browse_input = !v.other && !v.dialog;

    /* The pockets are a column here, so they change with L and R (the
     * game's left and right, pressed for it); left and right themselves do
     * nothing while the list is browsed. */
    if (browse_input && !v.popup && !v.quantity) {
        u32 k = eidos_ui_keys_pressed();

        eidos_ui_take_keys(BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_L | BW_KEY_R);
        if (k & BW_KEY_L) {
            press_key(BW_KEY_LEFT);
        } else if (k & BW_KEY_R) {
            press_key(BW_KEY_RIGHT);
        }
    }
    if (browse_input) {
        int st = eidos_ui_hit(rect(0, 0, cw, my)) | eidos_ui_hit(rect(0, my + mh, cw, hints_y - my - mh)) |
                 eidos_ui_hit(rect(0, my, mx, mh)) | eidos_ui_hit(rect(mx + mw, my, cw - mx - mw, mh));

        if (st & EIDOS_UI_CLICK) {
            press_key(BW_KEY_B);
        }
    }
    if (eidos_ui_right_click()) {
        press_key(BW_KEY_B);
    }

    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, MODAL_FILL, UI_RED, 245);
    snprintf(title, sizeof title, "%s", pocket_name(&v, v.pocket));
    {
        int tx;

        ui_fill(c, mx + 6, my + 6, mw - 12, hdr - 3, UI_BODY2, 255);
        ui_fill(c, mx + 6, my + 6 + hdr - 3, mw - 12, 1, UI_PINK, 255);
        for (i = 0; i < 5; i++) {
            ui_fill(c, mx + 10 + i, my + 10 + i, 1, 11 - 2 * i, UI_PINK, 255);
        }
        tx = ui_text(c, mx + 20, my + 6 + ui_text_y(hdr - 3, UI_FONT_LARGE), "BAG", UI_WHITE,
                     UI_EDGE, UI_FONT_LARGE, 1);
        ui_line(c, tx + 6, my + 6 + 15, tx + 11, my + 6 + 4, UI_EDGE2, 255);
        ui_text(c, tx + 15, my + 6 + ui_text_y(hdr - 3, UI_FONT_LARGE), title, UI_GREY, UI_EDGE,
                UI_FONT_LARGE, 1);
        /* the pocket arrows, right of the header: previous and next */
        {
            int ax = mx + mw - 6 - 40, ay = my + 6, st;

            st = eidos_ui_hit(rect(ax, ay, 18, hdr - 3));
            ui_text(c, ax + 3, ay + ui_text_y(hdr - 3, UI_FONT_LARGE), "\xE2\x86\x90",
                    (st & EIDOS_UI_HOVER) ? UI_PINK : UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if ((st & EIDOS_UI_CLICK) && browse_input) {
                tap_ds(PREV_TAP_X, BUTTON_TAP_Y);
            }
            st = eidos_ui_hit(rect(ax + 20, ay, 18, hdr - 3));
            ui_text(c, ax + 23, ay + ui_text_y(hdr - 3, UI_FONT_LARGE), "\xE2\x86\x92",
                    (st & EIDOS_UI_HOVER) ? UI_PINK : UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if ((st & EIDOS_UI_CLICK) && browse_input) {
                tap_ds(NEXT_TAP_X, BUTTON_TAP_Y);
            }
        }
    }

    body_y = my + 6 + hdr + 4;
    body_h = my + mh - 6 - body_y;
    tabs_x = mx + pad;
    tabs_w = mw < 360 ? 64 : 76;
    det_w = imax(mw < 360 ? 96 : 120, (mw - 2 * pad - tabs_w) * 40 / 100);
    det_x = mx + mw - pad - det_w;
    list_x = tabs_x + tabs_w + 6;
    list_w = det_x - 6 - list_x;
    rowh = iclamp((body_h - (ROWS - 1) * 2) / ROWS, 24, 30);

    /* ---- the pockets ------------------------------------------------ */
    {
        int th = iclamp((body_h - 4 * 4) / POCKETS, 18, 26);

        for (i = 0; i < POCKETS; i++) {
            int ty = body_y + i * (th + 4);
            int st = eidos_ui_hit(rect(tabs_x, ty, tabs_w, th));
            int sel = i == v.pocket;
            int n = (int)MYITEM_SAVEDATA_CountUsedSlotsInPocket(rdp(work, W_BAG), (u32)i);
            const char *nm = pocket_name(&v, i);

            ui_panel(c, tabs_x, ty, tabs_w, th, UI_CUT_TL | UI_CUT_BR, 4,
                     sel ? UI_PINK : ((st & EIDOS_UI_HOVER) ? 0x5A1530u : ROW_FILL),
                     sel ? UI_WHITE : UI_RED, 255);
            ui_text(c, tabs_x + 6, ty + ui_text_y(th, UI_FONT_LARGE), nm, UI_WHITE,
                    sel ? (s32)UI_PINK2 : (s32)UI_RED2, UI_FONT_LARGE, 1);
            if (n > 0 && !sel && tabs_w >= 70) {
                char cnt[12];

                snprintf(cnt, sizeof cnt, "%d", n);
                ui_text(c, tabs_x + tabs_w - 8 - ui_text_w(cnt, UI_FONT_SMALL),
                        ty + ui_text_y(th, UI_FONT_SMALL), cnt, UI_GREY, UI_RED2, UI_FONT_SMALL, 1);
            }
            if ((st & EIDOS_UI_CLICK) && browse_input && !sel) {
                tap_ds(k_tab_tap[i][0], k_tab_tap[i][1]);
            }
        }
    }

    /* ---- the list --------------------------------------------------- */
    {
        int sb = v.count > ROWS ? 8 : 0;
        int rw = list_w - sb;

        for (i = 0; i < ROWS; i++) {
            int ry = body_y + i * (rowh + 2);
            int idx = v.scroll + 1 + i;
            const u16 *slot = bag_slot(&v, idx);

            if (slot == NULL || slot[0] == 0) {
                if (v.count == 0) {
                    continue;       /* the empty state is drawn below */
                }
                ui_panel(c, list_x, ry, rw, rowh, UI_CUT_TL | UI_CUT_BR, 4, ROW_EMPTY, 0x4A0F22u, 255);
                continue;
            } else {
                u32 id = slot[0];
                int st = eidos_ui_hit(rect(list_x, ry, rw, rowh));
                int sel = i == v.cursor_row;
                int reg = v.pocket == 4 && is_registrable(id);

                if (st & EIDOS_UI_HOVER) {
                    hover_row = i;
                    hover_item = (int)id;
                    if (browse_input && !v.popup && i != v.cursor_row &&
                        task_is(work, ov070_ITEMMENU_TaskBagMain)) {
                        /* the game's cursor follows the mouse, so a key
                         * goes on from the row the mouse was on */
                        u32 row = (u32)i;

                        memcpy((u8 *)work + W_CURSORROW, &row, sizeof row);
                    }
                }
                if (sel) {
                    sel_item = (int)id;
                }
                item_row(c, list_x, ry, rw, rowh, ui_item_icon((u16)id), item_name(work, id),
                         v.pocket == 4 ? 0 : slot[1], sel && eidos_ui_keys_lit(),
                         (st & EIDOS_UI_HOVER) && !v.popup);
                if (reg) {
                    int bx = list_x + rw - (v.pocket == 4 ? 18 : 0);
                    int bst = eidos_ui_hit(rect(bx, ry + (rowh - 12) / 2, 12, 12));

                    reg_badge(c, bx, ry + (rowh - 12) / 2, is_registered(&v, id), bst & EIDOS_UI_HOVER);
                    if ((bst & EIDOS_UI_CLICK) && browse_input && !v.popup) {
                        tap_ds(REG_TAP_X, row_tap_y(i));
                    } else if ((st & EIDOS_UI_CLICK) && browse_input && !v.popup &&
                               !(bst & EIDOS_UI_HOVER)) {
                        tap_ds(ROW_TAP_X, row_tap_y(i));
                    }
                } else if ((st & EIDOS_UI_CLICK) && browse_input && !v.popup) {
                    tap_ds(ROW_TAP_X, row_tap_y(i));
                }
            }
        }
        if (v.count == 0) {
            const char *e = bag_msg(work, MSG_EMPTY);
            int eh = 30, ey = body_y + (ROWS * (rowh + 2) - eh) / 2;

            ui_panel(c, list_x, ey, rw, eh, UI_CUT_TL | UI_CUT_BR, 4, ROW_EMPTY, 0x4A0F22u, 255);
            ui_text(c, list_x + (rw - ui_text_w(e, UI_FONT_LARGE)) / 2,
                    ey + ui_text_y(eh, UI_FONT_LARGE), e, UI_LIGHT, UI_RED2, UI_FONT_LARGE, 1);
        }
        if (sb != 0) {
            int sh = ROWS * (rowh + 2) - 2;
            int sx = list_x + rw + 3;
            int st = eidos_ui_hit(rect(sx - 2, body_y, 10, sh));

            ui_scrollbar(c, sx, body_y, sh, imax(0, v.scroll + 1), ROWS, v.count, UI_PINK);
            if ((st & EIDOS_UI_CLICK) && browse_input) {
                /* the game's scroll bar: a tap at x 240, y 26 to 142 */
                tap_ds(240, 26);
            }
        }
        {
            int wh = eidos_ui_wheel(rect(list_x, body_y, list_w, ROWS * (rowh + 2)));

            if (wh != 0 && browse_input && !v.popup) {
                g_wheel_pending += wh;
            }
        }
    }

    /* the wheel: one cursor step every few frames */
    if (g_wheel_pending != 0 && now >= g_wheel_next && browse_input && !v.popup) {
        press_key(g_wheel_pending > 0 ? BW_KEY_UP : BW_KEY_DOWN);
        g_wheel_pending += g_wheel_pending > 0 ? -1 : 1;
        g_wheel_next = now + 4;
    } else if (v.popup || v.other) {
        g_wheel_pending = 0;
    }

    /* ---- the details ------------------------------------------------ */
    {
        int shown = hover_item != 0 && !v.popup ? hover_item : sel_item;
        int dh = ROWS * (rowh + 2) - 2;
        int ix, iy;
        const UiImage *big;

        dh = imax(dh, body_h);
        ui_panel(c, det_x, body_y, det_w, body_h, UI_CUT_TR | UI_CUT_BL, 8, MODAL_FILL2, UI_RED, 255);
        (void)hover_row;
        if (shown != 0) {
            const char *nm = item_name(work, (u32)shown);
            int ty0;

            big = ui_item_icon((u16)shown);
            ix = det_x + det_w / 2;
            iy = body_y + 8;
            ui_ellipse(c, ix - 28, iy + 40, 56, 14, 0x4A0F22u, UI_NONE, 255);
            if (big != NULL) {
                ui_image(c, ix - big->w, iy, big, 2);
            }
            ty0 = iy + 50;
            ui_text(c, det_x + (det_w - ui_text_w(nm, UI_FONT_LARGE)) / 2, ty0, nm, UI_WHITE,
                    UI_RED2, UI_FONT_LARGE, 1);
            draw_description(c, det_x + 8, ty0 + 20, det_w - 16, body_y + body_h - 6 - (ty0 + 20),
                             item_desc(work, (u32)shown));
        }
    }

    /* ---- the action list or the game's own screen ------------------- */
    if (v.popup) {
        draw_popup(c, &v, list_x, body_y, list_w, ROWS * (rowh + 2), rowh);
    }
    if (v.dialog) {
        draw_dialog(c, &v, mx, my, mw, mh);
    }

    /* ---- the key hints ---------------------------------------------- */
    {
        Hint h[5];
        int n = 0;

        h[n].key = "A";
        h[n].what = v.popup ? "Select" : (v.mode == 0 || v.mode == 1 || v.mode == 3 ? "Use" : "Select");
        h[n].keymask = BW_KEY_A;
        h[n].tap_x = -1;
        h[n].tap_y = 0;
        n++;
        h[n].key = "B";
        h[n].what = v.popup ? "Cancel" : "Back";
        h[n].keymask = BW_KEY_B;
        h[n].tap_x = -1;
        h[n].tap_y = 0;
        n++;
        h[n].key = "L R";
        h[n].what = "Pocket";
        h[n].keymask = BW_KEY_R;
        h[n].tap_x = NEXT_TAP_X;
        h[n].tap_y = BUTTON_TAP_Y;
        n++;
        if (v.pocket == 4) {
            h[n].key = "Y";
            h[n].what = "Register";
            h[n].keymask = BW_KEY_Y;
            h[n].tap_x = -1;
            h[n].tap_y = 0;
            n++;
        } else if (v.pocket != 2 && v.pocket != 3 && v.count > 1) {
            h[n].key = "START";
            h[n].what = "Sort";
            h[n].keymask = BW_KEY_START;
            h[n].tap_x = SORT_TAP_X;
            h[n].tap_y = BUTTON_TAP_Y;
            n++;
        }
        if (v.dialog) {
            n = 0;
            if (v.quantity) {
                h[n].key = "A"; h[n].what = "OK"; h[n].keymask = BW_KEY_A;
                h[n].tap_x = -1; h[n].tap_y = 0; n++;
                h[n].key = "B"; h[n].what = "Cancel"; h[n].keymask = BW_KEY_B;
                h[n].tap_x = -1; h[n].tap_y = 0; n++;
                h[n].key = "UP DOWN"; h[n].what = "Amount";
                h[n].keymask = BW_KEY_UP; h[n].tap_x = -1; h[n].tap_y = 0; n++;
            } else if (v.yesno) {
                h[n].key = "A"; h[n].what = "Yes"; h[n].keymask = BW_KEY_A;
                h[n].tap_x = -1; h[n].tap_y = 0; n++;
                h[n].key = "B"; h[n].what = "No"; h[n].keymask = BW_KEY_B;
                h[n].tap_x = -1; h[n].tap_y = 0; n++;
            } else {
                h[n].key = "A"; h[n].what = "Continue"; h[n].keymask = BW_KEY_A;
                h[n].tap_x = -1; h[n].tap_y = 0; n++;
            }
        } else if (v.other) {
            n = 0;
            h[n].key = "B"; h[n].what = "Back"; h[n].keymask = BW_KEY_B;
            h[n].tap_x = -1; h[n].tap_y = 0; n++;
        }
        if (1) {
            hints_bar(c, hints_y, cw, h, n);
        }
    }

    if (v.other) {
        draw_game_pane(c, in, cw, hints_y);
        if (eidos_ui_right_click()) {
            press_key(BW_KEY_B);
        }
    }
}

static void reset_bag(void)
{
    g_seen_work = NULL;
    g_wheel_pending = 0;
    g_wheel_next = 0;
    caches_clear();
}

static const EidosUiRecipe recipe_bag = { "bag", match_bag, run_bag, reset_bag, 0 };

const EidosUiRecipe *const ui_recipes_bag[] = { &recipe_bag };
const u32 ui_recipes_bag_count = 1;
