/*
 * engine/ui/recipe_menus.c -- the one-screen recipes for Options, Save and
 * the trainer card (engine/ui/ui.h, docs/unified-view-recipes.md sections 8,
 * 9 and 10).
 *
 * Three modals over the blurred world, in the colour the game gives each:
 * Options is cyan (four tabs, the rows with their choices, the game's help
 * text), Save is blue (place, date and time, the party, badges, Pokedex,
 * play time, the question with YES and NO, the last save) and the trainer
 * card is green (the card's rows, the trainer's picture, the badge case).
 *
 * The game keeps all the logic.  Everything shown is read from the game's
 * own structs (32-bit layout, offsets in the comments) and its own message
 * banks; every control becomes the bottom-screen tap the game tests for or a
 * button press:
 *
 *   Options   the choices are the rectangles of gOptionMenuChoices (the
 *             port's pages use the same columns), the page bar is the strip
 *             at y 144 to 167 (the left quarter goes back a page, the right
 *             quarter forward), CONFIRM and QUIT are gOptionMenuConfirmRect
 *             and gOptionMenuCancelRect, the discard question is the
 *             run-time rectangle of its yes/no menu.
 *   Save      YES and NO are the run-time rectangles of the report event's
 *             yes/no menu (x 192 to 255, y 48 to 71 and 72 to 95).
 *   Card      the card's own touch table (ov111 d_021C8594).
 *
 * A key press that the game would only "wake" (after a touch the game's
 * cursor is hidden, and the first button press only brings it back) is
 * preceded by an X press, which the game takes as a harmless wake-up on every
 * one of these screens.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "kit.h"
#include "ui_lang.h"
#include "dsmem.h"
#include "assets.h"
#include "input.h"
#include "textbox.h"
#include "choice_list.h"
#include "overlay.h"
#include "plat_thread.h"
#include "port.h"
#include "settings.h"

#if defined(__GNUC__) || defined(__clang__)
#define MN_WEAK __attribute__((weak))
#else
#define MN_WEAK
#endif

/* The recipes read the game's structs at the DS layout: 32-bit pointers. */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define MN_GAME 1
#else
#define MN_GAME 0
#endif

#define CUT_DEF (UI_CUT_TL | UI_CUT_BR)

/* ------------------------------------------------------------------ */
/* The game                                                            */
/* ------------------------------------------------------------------ */

/* The game system (src/game/arm9/gamesystem.c), as sdk/mp.c reads it. */
extern void *bw_dsref__GameSystemGlobal MN_WEAK;

extern void  *GAMESYSTEM_GetGameData(const void *gs) MN_WEAK;
extern void  *GAMESYSTEM_GetMyPlayerWork(const void *gs) MN_WEAK;
extern u8     UI_GetInputMode(void) MN_WEAK;

extern void  *GAME_DATA_GetMyStatus(void *gd) MN_WEAK;
extern const u16 *MYSTATUS_GetName(const void *status) MN_WEAK;
extern u16    MYSTATUS_GetIdLow16(const void *status) MN_WEAK;
extern u32    MYSTATUS_GetId(const void *status) MN_WEAK;
extern void  *GAME_DATA_GetMiscSave(void *gd) MN_WEAK;
extern u32    MISC_CountBadges(void *misc) MN_WEAK;
extern u32    MISC_GetBadge(void *misc, u32 bit) MN_WEAK;
extern u32    MISC_GetMoney(void *misc) MN_WEAK;
extern void  *ZUKAN_SAVE_GetFromGameData(void *gd) MN_WEAK;
extern u32    ZUKAN_SAVE_CountSeen(void *zukan, u32 heap) MN_WEAK;
extern u16    ZUKAN_SAVE_CountCaught(void *zukan, u32 heap) MN_WEAK;
extern u8    *GAME_DATA_GetEventWork(void *gd) MN_WEAK;
extern u32    ZONEDATA_CheckEventFlag(void *ev, s32 flag) MN_WEAK;
extern void  *GAME_DATA_GetPlayTime(void *gd) MN_WEAK;
extern u16    PLAYTIME_GetHours(const void *t) MN_WEAK;
extern u8     PLAYTIME_GetMinutes(const void *t) MN_WEAK;
extern u32    PLAYTIME_GetStampYear(const void *t) MN_WEAK;
extern u32    PLAYTIME_GetStampMonth(const void *t) MN_WEAK;
extern u32    PLAYTIME_GetStampDay(const void *t) MN_WEAK;
extern u32    PLAYTIME_GetStampHour(const void *t) MN_WEAK;
extern u32    PLAYTIME_GetStampMinute(const void *t) MN_WEAK;
extern void  *GAME_DATA_GetSaveControl(void *gd) MN_WEAK;
extern int    SAVE_CONTROL_WouldOverwriteSave(void *sc) MN_WEAK;
extern u8     SAVE_CONTROL_HasFlashSave(const void *sc) MN_WEAK;
extern void  *SYSTEM_DATA_GetPlayTime(void *sc) MN_WEAK;
extern u16    GAME_DATA_GetPlayerZoneId(const void *pw) MN_WEAK;
extern u32    ZONEDATA_GetPlaceNameId(u32 zone) MN_WEAK;
extern void  *GAME_DATA_GetMyParty(const void *gd) MN_WEAK;
extern u32    POKEPARTY_GetCount(const void *party) MN_WEAK;
extern void  *POKEPARTY_GetMember(void *party, u32 pos) MN_WEAK;
extern u32    POKE_TOOL_GetPartyValue(void *pp, u32 param, u32 arg) MN_WEAK;
extern u8     APP_TASKMENU_GetCursorPos(const void *menu) MN_WEAK;
extern s16    CLACT_GetActorSurfacePosAxis(const void *actor, u32 space, u32 axis) MN_WEAK;
extern void   STRBUF_ToChars(void *strbuf, u16 *out, u32 max) MN_WEAK;
extern void  *GAMESYSTEM_GetGameComm(const void *gs) MN_WEAK;
extern int    INTRUDE_COMM_GetRequestState(void *comm) MN_WEAK;
extern int    INTRUDE_COMM_CountEventPeers(void *comm) MN_WEAK;

/* The overlay functions the Options screen's state is compared with
 * (the overlay link's names: ovNNN_<function>). */
extern void ov068_OPTION_MENU_StateMain(void *, u32 *, void *) MN_WEAK;
extern void ov068_OPTION_MENU_StateDiscardPrompt(void *, u32 *, void *) MN_WEAK;

#define PARAM_SPECIES 0x05u
#define PARAM_IS_EGG  0x4Cu
#define PARAM_FORM    0x6Fu
#define PARAM_LEVEL   0x9Eu

/* Reads at a DS byte offset of a game struct. */
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

static int debug_on(void)
{
    static int on = -1;

    if (on < 0) {
        on = getenv("EIDOS_UI_DEBUG") != NULL;
    }
    return on;
}

static void *game_system(void)
{
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    return *(void **)bw_dsref__GameSystemGlobal;
}

/* GameSystem (arm9/ica_anime_shared_types.h): procSys 0x08, event 0x18,
 * gameData 0x1C, fieldMap 0x20.  ProcSys: current 0x08.  Proc: overlayId
 * 0x04, seq 0x08, work 0x14. */
static void *current_proc(void *gs)
{
    void *procsys = rdp(gs, 0x08);

    return procsys != NULL ? rdp(procsys, 0x08) : NULL;
}

/* The work of the proc on top when it is overlay `id` and past its init. */
static void *app_work(int id)
{
    void *gs, *cur, *work;

    if (!MN_GAME || !bw_overlay_is_loaded((u32)id)) {
        return NULL;
    }
    gs = game_system();
    if (gs == NULL) {
        return NULL;
    }
    cur = current_proc(gs);
    if (cur == NULL || (int)rd32(cur, 0x04) != id || rd32(cur, 0x08) < 2u) {
        return NULL;
    }
    work = rdp(cur, 0x14);
    return work;
}

static void *game_data(void)
{
    void *gs = game_system();

    return (gs != NULL && GAMESYSTEM_GetGameData != NULL) ? GAMESYSTEM_GetGameData(gs) : NULL;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

typedef struct Metric { int top, h, ok; } Metric;

/* The first row and the height of the capitals' letter pixels of each font
 * (glyph value 1 only: value 2 is the shadow), measured from the glyph of
 * 'H', so a label is centred on the letters themselves. */
static Metric g_metric[3];

static const Metric *font_metric(int font)
{
    Metric *m = &g_metric[font];
    UiGlyph g;
    int x, y, i = 0, top = -1, bot = -1;

    if (m->ok) {
        return m;
    }
    if (!ui_font_glyph(font, 'H', &g)) {
        static const Metric fb[3] = { { 2, 11, 0 }, { 0, 8, 0 }, { 3, 8, 0 } };

        return &fb[font];
    }
    for (y = 0; y < g.cell_h; y++) {
        for (x = 0; x < g.cell_w; x++, i++) {
            int v = (g.bitmap[(i * 2) >> 3] >> (6 - ((i & 3) * 2))) & 3;

            if (v == 1) {
                if (top < 0) {
                    top = y;
                }
                bot = y;
            }
        }
    }
    if (top < 0) {
        top = 0;
        bot = g.cell_h - 1;
    }
    m->top = top;
    m->h = bot - top + 1;
    m->ok = 1;
    return m;
}

/* The y that centres the capitals in a box of height h. */
static int text_cy(int h, int font)
{
    const Metric *m = font_metric(font);

    return (h - m->h + 1) / 2 - m->top;
}

/* Text in a box of w x h at (x, y): align 0 left with pad, 1 centre, 2 right
 * with pad.  Returns the text's width. */
static int box_text(EidosUiCtx *c, int x, int y, int w, int h, const char *s,
                    u32 color, s32 shadow, int font, int align, int pad)
{
    int tw = ui_text_w(s, font);
    int tx = align == 1 ? x + (w - tw) / 2 : (align == 2 ? x + w - pad - tw : x + pad);

    ui_text(c, tx, y + text_cy(h, font), s, color, shadow, font, 1);
    return tw;
}

/* The modal's rectangle: a fraction of the canvas above the footer,
 * clamped, centred. */
typedef struct Modal { int x, y, w, h; } Modal;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static Modal modal_rect(const EidosUiCtx *c, int fw_pct, int wmin, int wmax,
                        int fh_pct, int hmin, int hmax, int hneed)
{
    Modal m;
    /* inside the safe area, above the hint bar (lifted by safe_b) */
    int cw = c->canvas->w - c->safe_l - c->safe_r, ch = c->canvas->h;
    int avail = ch - ui_bar_h(c) - c->safe_t;

    m.w = clampi(cw * fw_pct / 100, wmin, wmax);
    m.h = clampi(ch * fh_pct / 100 - 17, hmin, hmax);
    if (m.h < hneed) {
        m.h = hneed;
    }
    if (m.w > cw - 8) {
        m.w = cw - 8;
    }
    if (m.h > avail - 4) {
        m.h = avail - 4;
    }
    m.x = c->safe_l + (cw - m.w) / 2;
    m.y = c->safe_t + (avail - m.h) / 2;
    return m;
}

/* A pill: a small cut-corner box with a centred label.  state 0 plain, 1
 * chosen, 2 hover. */
static void pill(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                 int state, u32 accent, u32 accent2, int font)
{
    u32 fill = UI_BODY, edge = UI_EDGE, col = UI_EDGE2;
    s32 sh = -1;

    if (state == 1) {
        fill = accent2;
        edge = accent;
        col = UI_WHITE;
        sh = 0x003C50;
    } else if (state == 2) {
        fill = accent2;
        edge = UI_EDGE;
        col = UI_WHITE;
        sh = 0x003C50;
    }
    ui_panel(c, x, y, w, h, CUT_DEF, 3, fill, edge, 255);
    box_text(c, x, y, w, h, label, col, sh, font, 1, 0);
}

/* The key hints bar: ui_hints draws it, the clicks are read here (a press
 * of the key with the wake-up of the game's hidden cursor).  Returns the
 * index of the hint clicked this frame, or -1. */
static int hint_bar(EidosUiCtx *c, const EidosUiFrame *in, const char *const *pairs, int n)
{
    int bh, y = ui_bottom_bar(c, c->canvas->h - ui_bar_h(c), 17, &bh), i, x = 6 + ui_safe_left(c);
    int hit = -1;

    (void)in;
    ui_hints(c, y, c->canvas->w, pairs, n, NULL);
    for (i = 0; i < n; i++) {
        int kw = ui_text_w(pairs[2 * i], UI_FONT_SMALL) + 8;
        int aw = ui_text_w(pairs[2 * i + 1], UI_FONT_SMALL);
        EidosUiRect r;

        if (kw < 13) {
            kw = 13;
        }
        r.x = x - 2;
        r.y = y;
        r.w = kw + 4 + aw + 4;
        r.h = bh;
        if (eidos_ui_hit(r) & EIDOS_UI_CLICK) {
            hit = i;
        }
        x += kw + 4 + aw + 10;
    }
    return hit;
}

/* ------------------------------------------------------------------ */
/* Key presses                                                         */
/* ------------------------------------------------------------------ */

typedef struct Pending { u32 keys; u32 at; } Pending;
static Pending g_pend[4];
static u32 g_frame;

static void pend_clear(void)
{
    memset(g_pend, 0, sizeof g_pend);
}

/* Press `keys` after `delay` frames. */
static void pend_press(u32 keys, u32 delay)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_pend[i].keys == 0) {
            g_pend[i].keys = keys;
            g_pend[i].at = g_frame + delay;
            return;
        }
    }
}

static void pend_pump(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_pend[i].keys != 0 && g_frame >= g_pend[i].at) {
            eidos_ui_press(g_pend[i].keys, 2);
            g_pend[i].keys = 0;
        }
    }
}

/* A game button from a click: after a touch the game hides its cursor and
 * eats the first button press, so X (the harmless wake-up) goes first. */
static void press_key(u32 keys)
{
    if (UI_GetInputMode != NULL && UI_GetInputMode() == 1 && keys != BW_KEY_B &&
        keys != BW_KEY_X && keys != BW_KEY_Y) {
        eidos_ui_press(BW_KEY_X, 2);
        pend_press(keys, 6);
    } else {
        eidos_ui_press(keys, 2);
    }
}

/* One touch on the game's bottom screen, held three frames. */
static void tap(int x, int y)
{
    eidos_ui_tap(x, y, 3);
}

/* ------------------------------------------------------------------ */
/* The game's text                                                     */
/* ------------------------------------------------------------------ */

/* A message bank of a/0/0/<arc> read straight from the archive (through
 * the file layer, so a mod's text shows) and enciphered the way the game's
 * banks are (docs/ASSETS.md, tools/assets/text.py). */
typedef struct Bank { int arc, member; u8 *data; u32 size; } Bank;
static Bank g_bank[32];
static int g_nbank;

static const Bank *bank_get(int arc, int member)
{
    int i;
    char path[16];
    BwArcFile f;
    long n;
    Bank *b;

    for (i = 0; i < g_nbank; i++) {
        if (g_bank[i].arc == arc && g_bank[i].member == member) {
            return g_bank[i].data != NULL ? &g_bank[i] : NULL;
        }
    }
    if (g_nbank >= (int)(sizeof g_bank / sizeof g_bank[0])) {
        return NULL;
    }
    b = &g_bank[g_nbank++];
    b->arc = arc;
    b->member = member;
    b->data = NULL;
    snprintf(path, sizeof path, "a/0/0/%d", arc);
    if (bw_arc_open(&f, path) != 0) {
        return NULL;
    }
    n = bw_arc_member_size(&f, (u32)member);
    if (n > 16) {
        u8 *buf = (u8 *)malloc((size_t)n);

        if (buf != NULL && bw_arc_read_member(&f, (u32)member, buf, (u32)n) == n) {
            b->data = buf;
            b->size = (u32)n;
        } else {
            free(buf);
        }
    }
    bw_arc_close(&f);
    return b->data != NULL ? b : NULL;
}

static u32 rotl16(u32 v, int s)
{
    return ((v << s) | (v >> (16 - s))) & 0xFFFFu;
}

static int put_utf8(char *out, int n, int max, u32 c)
{
    if (c < 0x80u) {
        if (n + 1 < max) out[n++] = (char)c;
    } else if (c < 0x800u) {
        if (n + 2 < max) {
            out[n++] = (char)(0xC0u | (c >> 6));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        }
    } else {
        if (n + 3 < max) {
            out[n++] = (char)(0xE0u | (c >> 12));
            out[n++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    return n;
}

/* One string of a bank as UTF-8.  Line breaks stay '\n'; the control codes
 * are dropped, except the player's name tag (kind 0x0100), which `name`
 * replaces.  Returns out. */
static const char *bank_text(int arc, int member, u32 idx, const char *name,
                             char *out, int max)
{
    const Bank *b = bank_get(arc, member);
    u32 count, off, key, pos, n = 0;

    out[0] = '\0';
    if (b == NULL || b->size < 16u) {
        return out;
    }
    count = b->data[2] | ((u32)b->data[3] << 8);
    if (idx >= count || 20u + 8u * (idx + 1u) > b->size) {
        return out;
    }
    off = b->data[20 + 8 * idx] | ((u32)b->data[21 + 8 * idx] << 8) |
          ((u32)b->data[22 + 8 * idx] << 16) | ((u32)b->data[23 + 8 * idx] << 24);
    pos = 16u + off;
    key = ((idx + 3u) * 0x2983u) & 0xFFFFu;
    while (pos + 2u <= b->size && (int)n < max - 4) {
        u32 u = (b->data[pos] | ((u32)b->data[pos + 1] << 8)) ^ key;

        key = rotl16(key, 3);
        pos += 2;
        if (u == 0xFFFFu) {
            break;
        }
        if (u == 0xFFFEu) {
            out[n++] = '\n';
            continue;
        }
        if (u == 0xF000u) {
            u32 kind, argc, k, a0 = 0;

            if (pos + 4u > b->size) break;
            kind = (b->data[pos] | ((u32)b->data[pos + 1] << 8)) ^ key;
            key = rotl16(key, 3);
            pos += 2;
            argc = (b->data[pos] | ((u32)b->data[pos + 1] << 8)) ^ key;
            key = rotl16(key, 3);
            pos += 2;
            for (k = 0; k < argc && pos + 2u <= b->size; k++) {
                u32 a = (b->data[pos] | ((u32)b->data[pos + 1] << 8)) ^ key;

                if (k == 0) a0 = a;
                key = rotl16(key, 3);
                pos += 2;
            }
            (void)a0;
            if (kind == 0x0100u && name != NULL) {
                const char *s;

                for (s = name; *s != '\0' && (int)n < max - 4; s++) {
                    out[n++] = *s;
                }
            }
            continue;
        }
        if (u == 0x246Du) {
            u = 0x2642u;
        } else if (u == 0x246Eu) {
            u = 0x2640u;
        } else if (u >= 0xD800u && u <= 0xF8FFu) {
            continue;
        }
        n = (u32)put_utf8(out, (int)n, max, u);
    }
    out[n] = '\0';
    return out;
}

/* The game's 16-bit text (a name, a Strbuf's chars) as UTF-8. */
static void units_utf8(const u16 *u, int count, char *out, int max)
{
    int n = 0, i;

    for (i = 0; i < count && u[i] != 0xFFFFu; i++) {
        u32 c = u[i];

        if (c == 0xFFFEu || c == 0xE000u) {
            c = ' ';
        } else if (c == 0x246Du) {
            c = 0x2642u;
        } else if (c == 0x246Eu) {
            c = 0x2640u;
        } else if (c >= 0xD800u && c <= 0xF8FFu) {
            continue;
        }
        n = put_utf8(out, n, max, c);
    }
    out[n] = '\0';
}

/* A cache of bank texts so a frame does not decode again.  The slot is
 * (arc, member, index, name) hashed into a small ring. */
typedef struct TextSlot { int arc, member; u32 idx; char name[24]; char text[400]; int ok; } TextSlot;
static TextSlot g_text[24];

static const char *game_text(int arc, int member, u32 idx, const char *name)
{
    int i;
    TextSlot *s;
    static int next;

    for (i = 0; i < 24; i++) {
        s = &g_text[i];
        if (s->ok && s->arc == arc && s->member == member && s->idx == idx &&
            strcmp(s->name, name != NULL ? name : "") == 0) {
            return s->text;
        }
    }
    s = &g_text[next];
    next = (next + 1) % 24;
    s->arc = arc;
    s->member = member;
    s->idx = idx;
    snprintf(s->name, sizeof s->name, "%s", name != NULL ? name : "");
    bank_text(arc, member, idx, name, s->text, (int)sizeof s->text);
    s->ok = s->text[0] != '\0';
    if (!s->ok) {
        /* not readable: do not cache, the caller shows its fallback */
        return s->text;
    }
    return s->text;
}

/* The game's text for the other recipes (kit.h). */
const char *ui_game_text(int arc, int member, u32 idx)
{
    return game_text(arc, member, idx, NULL);
}

/* Replace the line breaks of a text by spaces (the kit wraps by itself). */
static void flatten(const char *s, char *out, int max)
{
    int n = 0;

    for (; *s != '\0' && n < max - 1; s++) {
        out[n++] = *s == '\n' ? ' ' : *s;
    }
    out[n] = '\0';
}

typedef struct WrapState { EidosUiCtx *c; int x, y, step, font; u32 color; s32 shadow; int n, maxn; } WrapState;

static void wrap_line(void *user, const char *s, int len)
{
    WrapState *w = (WrapState *)user;
    char tmp[256];

    if (w->n >= w->maxn) {
        return;
    }
    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    ui_text(w->c, w->x, w->y + w->n * w->step, tmp, w->color, w->shadow, w->font, 1);
    w->n++;
}

/* Wrapped text from (x, y), line step `step`, at most maxn lines; returns
 * the number of lines. */
static int wrapped(EidosUiCtx *c, int x, int y, int width, int step, int font,
                   const char *text, u32 color, s32 shadow, int maxn)
{
    WrapState w;
    char flat[512];

    flatten(text, flat, (int)sizeof flat);
    w.c = c;
    w.x = x;
    w.y = y;
    w.step = step;
    w.font = font;
    w.color = color;
    w.shadow = shadow;
    w.n = 0;
    w.maxn = maxn;
    ui_wrap(flat, width, font, wrap_line, &w);
    return w.n;
}

/* ------------------------------------------------------------------ */
/* The outside click                                                   */
/* ------------------------------------------------------------------ */

/* A click outside the modal (above the footer) is B: four strips around it
 * are hit regions, so the press must have begun and ended outside. */
static int outside_click(const EidosUiCtx *c, Modal m)
{
    int cw = c->canvas->w, ch = c->canvas->h - ui_bar_h(c);
    EidosUiRect r[4];
    int i, hit = 0;

    r[0].x = 0;           r[0].y = 0;           r[0].w = cw;           r[0].h = m.y;
    r[1].x = 0;           r[1].y = m.y + m.h;   r[1].w = cw;           r[1].h = ch - (m.y + m.h);
    r[2].x = 0;           r[2].y = m.y;         r[2].w = m.x;          r[2].h = m.h;
    r[3].x = m.x + m.w;   r[3].y = m.y;         r[3].w = cw - (m.x + m.w); r[3].h = m.h;
    for (i = 0; i < 4; i++) {
        if (r[i].w > 0 && r[i].h > 0 && (eidos_ui_hit(r[i]) & EIDOS_UI_CLICK)) {
            hit = 1;
        }
    }
    return hit;
}

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

/* OptionMenuWork (ov068/option_menu_types.h). */
#define OW_RUNNER_STATE   0x0DCu   /* u32: runner.state                        */
#define OW_RUNNER_CB      0x0D4u   /* the state function                       */
#define OW_LIST           0x148u
#define OL_CURSOR         0x004u   /* int                                      */
#define OL_ACTORS         0x008u   /* ClactActor *[11]                         */
#define OL_SCROLLY        0x034u   /* int                                      */
#define OL_VALUES         0x044u   /* u8[5]                                    */
#define OW_MSGBOX_PHASE   0x22Eu   /* messageBox.phase (u16): 2 = yes/no open  */
#define OW_MSGBOX_ANSWER  0x22Cu

enum { PAGE_GAME = 0, PAGE_DISPLAY, PAGE_SOUND, PAGE_EXTRAS, PAGE_COUNT };

enum { OPT_CHOICE = 0, OPT_STEP };

typedef struct OptRow {
    const char *label;
    const char *help;           /* NULL: the game's (GAME page)            */
    int option;                 /* settings.h option id (port pages)       */
    int kind;
    int count;                  /* choices                                  */
    const char *names[3];
    int values[3];
    int min, max, step;
    const char *zero;
    char unit;
} OptRow;

/* The port's pages (stubs/ov/patch_ov068_option_menu.c, in the same order
 * and with the same help). */
static const OptRow k_display[] = {
    { "WINDOW", "How the game window is shown. Full covers the whole screen.",
      EIDOS_OPT_DISPLAY_MODE, OPT_CHOICE, 3, { "WINDOW", "FULL", "BORDER" }, { 0, 1, 2 }, 0, 0, 0, 0, 0 },
    { "PIXEL SCALE", "The size of each DS pixel. Auto fills the window.",
      EIDOS_OPT_DISPLAY_PIXEL_SCALE, OPT_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 }, 0, 10, 1, "AUTO", 'X' },
    { "3D DETAIL", "The resolution of the 3D. Auto matches the window.",
      EIDOS_OPT_DISPLAY_RESOLUTION, OPT_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 }, 0, 6, 1, "AUTO", 'X' },
    { "SCREENS", "Stack the screens, put them side by side, or show one screen.",
      EIDOS_OPT_LAYOUT_ARRANGEMENT, OPT_CHOICE, 3, { "STACKED", "WIDE", "ONE" }, { 0, 1, 2 }, 0, 0, 0, 0, 0 },
    { "SWAP SCREENS", "Show the bottom screen first.",
      EIDOS_OPT_LAYOUT_SWAP, OPT_CHOICE, 2, { "ON", "OFF", 0 }, { 1, 0, 0 }, 0, 0, 0, 0, 0 },
};

static const OptRow k_sound[] = {
    { "VOLUME", "The volume of the music and the sound effects.",
      EIDOS_OPT_SOUND_VOLUME, OPT_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 }, 0, 100, 5, 0, '%' },
};

static const OptRow k_extras[] = {
    { "INSTANT TEXT", "Show each page of a message at once, whatever the text speed.",
      EIDOS_OPT_EXTRAS_INSTANT_TEXT, OPT_CHOICE, 2, { "ON", "OFF", 0 }, { 1, 0, 0 }, 0, 0, 0, 0, 0 },
    { "TRANSITIONS", "Doors and map changes. Seamless never goes black.",
      EIDOS_OPT_TRANSITIONS, OPT_CHOICE, 3, { "ORIGINAL", "FAST", "SEAMLESS" }, { 0, 1, 2 }, 0, 0, 0, 0, 0 },
};

static const char *const k_page_name[PAGE_COUNT] = { "GAME", "DISPLAY", "SOUND", "EXTRAS" };

/* The game's page: row -> (label message, first choice index, choice count,
 * help message), choices -> (message, value), all of bank 29 of a/0/0/2
 * (gOptionMenuLabels, gOptionMenuChoices, gOptionMenuSettingRows). */
typedef struct GameRow { int label, help, first, count; } GameRow;
static const GameRow k_game_row[5] = {
    { 1, 25, 0, 3 }, { 2, 26, 3, 2 }, { 3, 27, 5, 2 }, { 4, 28, 7, 2 }, { 7, 31, 9, 2 },
};
static const int k_game_choice_msg[11] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 23, 24 };
static const int k_game_choice_val[11] = { 0, 1, 2, 0, 1, 0, 1, 0, 1, 1, 0 };

/* The tap centres of the choices (gOptionMenuChoices rectangles): three
 * choices at x 112, 160, 208 (48 wide), two at 112 and 184 (72 wide). */
static int choice_tap_x(int count, int c)
{
    return count == 3 ? 136 + 48 * c : 148 + 72 * c;
}

typedef struct OptState {
    int hover_row;
    u32 cool;               /* no new page tap before this frame           */
    int target;             /* the page a tab click is stepping to, or -1  */
    int last_page;
    int pending_hold;
} OptState;

static OptState g_opt = { -1, 0, -1, 0, 0 };

/* The page the game's Options shows, and the page a tab click asks for:
 * written and taken by the overlay patch (stubs/ov/patch_ov068_option_menu.c). */
int eidos_ui_opt_page;
int eidos_ui_opt_page_request = -1;

static void options_reset(void)
{
    g_opt.hover_row = -1;
    g_opt.cool = 0;
    g_opt.target = -1;
    g_opt.last_page = 0;
    pend_clear();
}

/* Which page the port's overlay patch shows: it says so itself (counting
 * the choice sprites on the screen no longer told EXTRAS once it had two
 * rows, and a tab click then stepped the pages without end). */
static int options_page(const void *work)
{
    (void)work;
    return eidos_ui_opt_page >= 0 && eidos_ui_opt_page < PAGE_COUNT ? eidos_ui_opt_page : 0;
}

static int options_match(void)
{
    return app_work(68) != NULL;
}

static const OptRow *page_rows(int page, int *n)
{
    switch (page) {
    case PAGE_DISPLAY:
        *n = (int)(sizeof k_display / sizeof k_display[0]);
        return k_display;
    case PAGE_SOUND:
        *n = (int)(sizeof k_sound / sizeof k_sound[0]);
        return k_sound;
    case PAGE_EXTRAS:
        *n = (int)(sizeof k_extras / sizeof k_extras[0]);
        return k_extras;
    }
    *n = 0;
    return NULL;
}

static void step_text(const OptRow *row, int v, char *out, int max)
{
    if (v == 0 && row->zero != NULL) {
        snprintf(out, (size_t)max, "%s", row->zero);
    } else {
        snprintf(out, (size_t)max, "%d%c", v, row->unit);
    }
}

#define OPT_ROW_H     19
#define OPT_ROW_STEP  21
#define OPT_CELL_W    52

static void options_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    void *work = app_work(68);
    int page, cursor, scroll_y, nrows, r, i;
    const OptRow *prow;
    Modal m;
    int rw, rows_y, tabs_y, help_y, help_h;
    const char *name_tab[PAGE_COUNT];
    u8 values[5];
    char title[40];
    int clicked_row = -1;
    int compact;
    int prompt;
    void *cb;
    EidosUiRect hr;
    const char *pairs[8];

    g_frame = in->frame;
    pend_pump();
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    if (work == NULL) {
        return;
    }

    page = options_page(work);
    g_opt.last_page = page;
    cursor = (int)rd32(work, OW_LIST + OL_CURSOR);
    scroll_y = (int)rd32(work, OW_LIST + OL_SCROLLY);
    for (i = 0; i < 5; i++) {
        values[i] = (u8)rd8(work, OW_LIST + OL_VALUES + (u32)i);
    }
    prow = page_rows(page, &nrows);
    if (page == PAGE_GAME) {
        nrows = 5;
    }
    if (cursor < 0 || cursor >= nrows) {
        cursor = cursor >= 5 ? -1 : 0;
    }
    cb = rdp(work, OW_RUNNER_CB);
    prompt = rd16(work, OW_MSGBOX_PHASE) == 2u &&
             (ov068_OPTION_MENU_StateDiscardPrompt == NULL ||
              cb == (void *)ov068_OPTION_MENU_StateDiscardPrompt);
    if (debug_on() && (in->frame % 120u) == 0) {
        fprintf(stderr, "ui: options page %d cursor %d scroll %d prompt %d cb %p state %u phase %u\n",
                page, cursor, scroll_y, prompt, cb, rd32(work, OW_RUNNER_STATE),
                rd16(work, OW_MSGBOX_PHASE));
    }

    /* the modal */
    /* a canvas too short for the help box shows the rows alone */
    compact = c->canvas->h - ui_bar_h(c) - 6 < 193;
    m = modal_rect(c, 80, 320, 440, 86, compact ? 158 : 193, compact ? 158 : 193,
                   compact ? 158 : 193);
    ui_panel(c, m.x, m.y, m.w, m.h, CUT_DEF, 10, 0x060E1Au, 0x103C5Au, 245);
    snprintf(title, sizeof title, "%s", game_text(2, 29, 0, NULL)[0] != '\0' ? game_text(2, 29, 0, NULL) : "OPTIONS");
    ui_header(c, m.x + 4, m.y + 4, m.w - 8, title, k_page_name[page], NULL, UI_CYAN);

    /* tabs on the left, CONFIRM and QUIT on the right */
    tabs_y = m.y + 29;
    for (i = 0; i < PAGE_COUNT; i++) {
        name_tab[i] = k_page_name[i];
    }
    {
        int x = m.x + 8;

        for (i = 0; i < PAGE_COUNT; i++) {
            int tw = ui_text_w(name_tab[i], UI_FONT_SMALL) + 16;
            int st;

            hr.x = x;
            hr.y = tabs_y;
            hr.w = tw;
            hr.h = 14;
            st = prompt ? 0 : eidos_ui_hit(hr);
            pill(c, x, tabs_y, tw, 14, name_tab[i], i == page ? 1 : ((st & EIDOS_UI_HOVER) ? 2 : 0),
                 UI_CYAN, UI_CYAN2, UI_FONT_SMALL);
            if ((st & EIDOS_UI_CLICK) && i != page) {
                g_opt.target = i;
            }
            x += tw + 3;
        }
        {
            const char *l_ok = game_text(2, 29, 8, NULL), *l_no = game_text(2, 29, 9, NULL);
            int w_ok, w_no, xq;
            int st;

            if (l_ok[0] == '\0') l_ok = "CONFIRM";
            if (l_no[0] == '\0') l_no = "QUIT";
            w_ok = ui_text_w(l_ok, UI_FONT_SMALL) + 16;
            w_no = ui_text_w(l_no, UI_FONT_SMALL) + 16;
            xq = m.x + m.w - 8 - w_no;
            hr.x = xq; hr.y = tabs_y; hr.w = w_no; hr.h = 14;
            st = prompt ? 0 : eidos_ui_hit(hr);
            pill(c, xq, tabs_y, w_no, 14, l_no, (st & EIDOS_UI_HOVER) ? 2 : 0, UI_CYAN, UI_CYAN2,
                 UI_FONT_SMALL);
            if (st & EIDOS_UI_CLICK) {
                tap(220, 180);
            }
            xq -= 3 + w_ok;
            hr.x = xq; hr.w = w_ok;
            st = prompt ? 0 : eidos_ui_hit(hr);
            /* the game's cursor goes on from the last row to CONFIRM (the
             * pages' cursor value 5, read as -1 here): lit like a hover so
             * a pad sees where it is */
            pill(c, xq, tabs_y, w_ok, 14, l_ok,
                 ((st & EIDOS_UI_HOVER) || (cursor < 0 && !prompt && eidos_ui_keys_lit())) ? 2 : 0,
                 UI_CYAN, UI_CYAN2, UI_FONT_SMALL);
            if (st & EIDOS_UI_CLICK) {
                tap(148, 180);
            }
        }
    }

    /* the rows */
    rw = m.w - 16;
    rows_y = m.y + 47;
    g_opt.hover_row = -1;
    for (r = 0; r < nrows; r++) {
        int ry = rows_y + r * OPT_ROW_STEP;
        int sel = r == cursor && eidos_ui_keys_lit();
        int row_st;
        const char *label;
        int count, kind = OPT_CHOICE, cell0;
        int tapy = scroll_y + 24 * r + 12;

        hr.x = m.x + 8; hr.y = ry; hr.w = rw; hr.h = OPT_ROW_H;
        row_st = prompt ? 0 : eidos_ui_hit(hr);
        if (row_st & EIDOS_UI_HOVER) {
            g_opt.hover_row = r;
            if (r != cursor) {
                /* the game's cursor follows the mouse, so a key goes on
                 * from the row the mouse was on */
                s32 row = r;

                memcpy((u8 *)work + OW_LIST + OL_CURSOR, &row, sizeof row);
            }
        }
        ui_panel(c, m.x + 8, ry, rw, OPT_ROW_H, CUT_DEF, 5,
                 sel ? 0x0A283Cu : ((row_st & EIDOS_UI_HOVER) ? 0x081C2Cu : UI_BODY),
                 sel ? UI_CYAN : UI_EDGE, 255);
        if (page == PAGE_GAME) {
            label = game_text(2, 29, (u32)k_game_row[r].label, NULL);
            count = k_game_row[r].count;
        } else {
            label = prow[r].label;
            count = prow[r].kind == OPT_STEP ? 3 : prow[r].count;
            kind = prow[r].kind;
        }
        box_text(c, m.x + 8, ry, rw, OPT_ROW_H, label, UI_WHITE, sel ? 0x003C50 : (s32)UI_EDGE,
                 UI_FONT_LARGE, 0, 8);
        cell0 = m.x + 8 + rw - 8 - OPT_CELL_W * 3;
        if (count == 2) {
            cell0 += OPT_CELL_W;
        }
        /* a row click that misses its choices selects the row: tap its label */
        if ((row_st & EIDOS_UI_CLICK)) {
            clicked_row = r;
        }
        for (i = 0; i < count; i++) {
            int cx = cell0 + i * OPT_CELL_W;
            int chosen, st;
            const char *txt;
            char tbuf[64];

            if (page == PAGE_GAME) {
                int ci = k_game_row[r].first + i;

                txt = game_text(2, 29, (u32)k_game_choice_msg[ci], NULL);
                chosen = values[r] == k_game_choice_val[ci];
            } else if (kind == OPT_STEP) {
                int v = eidos_settings_get(prow[r].option);

                if (i == 0) {
                    txt = "<";
                } else if (i == 2) {
                    txt = ">";
                } else {
                    step_text(&prow[r], v, tbuf, (int)sizeof tbuf);
                    txt = tbuf;
                }
                chosen = i == 1;
            } else {
                txt = prow[r].names[i];
                chosen = eidos_settings_get(prow[r].option) == prow[r].values[i];
            }
            hr.x = cx; hr.y = ry; hr.w = OPT_CELL_W; hr.h = OPT_ROW_H;
            st = prompt ? 0 : eidos_ui_hit(hr);
            if (kind == OPT_STEP && i == 1) {
                st = 0;         /* the value is not a control */
            }
            pill(c, cx + 2, ry + 3, OPT_CELL_W - 4, 13, txt,
                 chosen ? 1 : ((st & EIDOS_UI_HOVER) ? 2 : 0), UI_CYAN, UI_CYAN2, UI_FONT_SMALL);
            if (st & EIDOS_UI_CLICK) {
                tap(page == PAGE_GAME || kind == OPT_CHOICE ? choice_tap_x(count, i)
                                                             : (i == 0 ? 136 : 232), tapy);
                clicked_row = -1;
            }
        }
        if (clicked_row == r) {
            tap(60, tapy);
            clicked_row = -1;
        }
    }

    /* the help: the hovered row's, else the cursor's */
    help_h = 30;
    help_y = rows_y + nrows * OPT_ROW_STEP + 3;
    if (help_y + help_h > m.y + m.h - 6) {
        help_y = m.y + m.h - 6 - help_h;
    }
    if (!compact) {
        int hrow = g_opt.hover_row >= 0 ? g_opt.hover_row : cursor;
        const char *help = "";

        ui_panel(c, m.x + 8, help_y, rw, help_h, CUT_DEF, 5, UI_BODY2, UI_EDGE, 255);
        if (hrow >= 0 && hrow < nrows) {
            help = page == PAGE_GAME ? game_text(2, 29, (u32)k_game_row[hrow].help, NULL)
                                     : prow[hrow].help;
        }
        wrapped(c, m.x + 16, help_y + 3, rw - 16, 12, UI_FONT_MEDIUM, help, UI_LIGHT, UI_EDGE, 2);
    }

    /* the discard question: the game's yes/no at its run-time rectangles */
    if (prompt) {
        const char *q = game_text(2, 29, 33, NULL);
        const char *ch[2];
        int dw = clampi(m.w - 40, 200, 300), dh, dx, dy, cur;
        int ans;

        ch[0] = game_text(2, 29, 34, NULL);
        ch[1] = game_text(2, 29, 35, NULL);
        if (q[0] == '\0') q = "Would you like to make these changes?";
        if (ch[0][0] == '\0') ch[0] = "YES";
        if (ch[1][0] == '\0') ch[1] = "NO";
        {
            void *menu = rdp(work, 0x210);

            cur = menu != NULL ? (int)rd8(menu, 0) : 0;
        }
        dh = 14 + 15 * ui_wrap(q, dw - 20, UI_FONT_LARGE, NULL, NULL) + 26;
        dx = m.x + (m.w - dw) / 2;
        dy = m.y + (m.h - dh) / 2;
        ui_fill(c, m.x, m.y, m.w, m.h, UI_INK, 120);
        ui_dialog(c, dx, dy, dw, q, ch, 2, cur, UI_CYAN);
        /* the two buttons ui_dialog draws: 56 wide, right aligned */
        for (ans = 0; ans < 2; ans++) {
            int bx = dx + dw - 10 - 62 * (2 - ans) + 6;
            int st;

            hr.x = bx; hr.y = dy + dh - 24; hr.w = 56; hr.h = 18;
            st = eidos_ui_hit(hr);
            if (st & EIDOS_UI_HOVER) {
                ui_fill(c, bx, dy + dh - 24, 56, 18, UI_CYAN2, 90);
            }
            if (st & EIDOS_UI_CLICK) {
                tap(204, ans == 0 ? 60 : 84);
            }
        }
    }

    /* a message of the main state (a setting that cannot change, a help
     * the game prints): the game waits for it, a click is the A that ends it */
    if (!prompt && ov068_OPTION_MENU_StateMain != NULL &&
        cb == (void *)ov068_OPTION_MENU_StateMain && rd32(work, OW_RUNNER_STATE) != 0u) {
        u32 rs = rd32(work, OW_RUNNER_STATE);
        const char *msg = rs == 4 ? game_text(2, 29, 36, NULL)
                        : (rs == 2 || rs == 3) ? game_text(3, 0x9E, 22, NULL)
                        : game_text(2, 29, 32, NULL);
        char flat[200];

        flatten(msg, flat, (int)sizeof flat);
        ui_fill(c, m.x, m.y, m.w, m.h, UI_INK, 120);
        if (flat[0] != '\0') {
            int lines = ui_wrap(flat, m.w - 40, UI_FONT_LARGE, NULL, NULL);
            int bh = 14 + 15 * (lines > 0 ? lines : 1);

            ui_panel(c, m.x + 16, m.y + (m.h - bh) / 2, m.w - 32, bh, CUT_DEF, 8, UI_BODY, UI_EDGE2, 255);
            wrapped(c, m.x + 26, m.y + (m.h - bh) / 2 + 7, m.w - 52, 15, UI_FONT_LARGE, flat,
                    UI_WHITE, UI_EDGE, 4);
        }
        hr.x = 0; hr.y = 0; hr.w = c->canvas->w; hr.h = c->canvas->h - ui_bar_h(c);
        if ((eidos_ui_hit(hr) & EIDOS_UI_CLICK) != 0 && rs == 5) {
            press_key(BW_KEY_A);
        }
    }

    /* the footer: a click is the key */
    pairs[0] = "A";  pairs[1] = "Confirm";
    pairs[2] = "B";  pairs[3] = prompt ? "Back" : "Quit";   /* B is the game's QUIT */
    pairs[4] = "L R"; pairs[5] = "Tab";
    pairs[6] = "\xE2\x86\x90\xE2\x86\x92"; pairs[7] = "Change";
    i = hint_bar(c, in, pairs, 4);
    if (i == 0) {
        tap(prompt ? 204 : 148, prompt ? 60 : 180);     /* CONFIRM, or YES */
    } else if (i == 1) {
        press_key(BW_KEY_B);
    } else if (i == 2) {
        g_opt.target = (page + 1) % PAGE_COUNT;
    } else if (i == 3 && !prompt && cursor >= 0 && cursor < nrows) {
        /* the cursor row's next choice (a stepper's next step) */
        int count, chosen = 0, k, tx;

        if (page == PAGE_GAME) {
            count = k_game_row[cursor].count;
            for (k = 0; k < count; k++) {
                if (values[cursor] == k_game_choice_val[k_game_row[cursor].first + k]) {
                    chosen = k;
                }
            }
            tx = choice_tap_x(count, (chosen + 1) % count);
        } else if (prow[cursor].kind == OPT_STEP) {
            tx = 232;
        } else {
            count = prow[cursor].count;
            for (k = 0; k < count; k++) {
                if (eidos_settings_get(prow[cursor].option) == prow[cursor].values[k]) {
                    chosen = k;
                }
            }
            tx = choice_tap_x(count, (chosen + 1) % count);
        }
        tap(tx, scroll_y + 24 * cursor + 12);
    }

    /* B: a right click, or a click outside the modal */
    if (eidos_ui_right_click() || (!prompt && outside_click(c, m))) {
        press_key(BW_KEY_B);
    }

    /* a tab click goes straight to its page: the patch takes it at the
     * screen's next step, one change and one sound */
    if (g_opt.target >= 0) {
        if (g_opt.target != page && !prompt) {
            eidos_ui_opt_page_request = g_opt.target;
        }
        g_opt.target = -1;
    }
}

static const EidosUiRecipe recipe_options = {
    "options", options_match, options_run, options_reset, 0
};

/* ------------------------------------------------------------------ */
/* Save                                                                */
/* ------------------------------------------------------------------ */

/* FieldMap: subscreen 0x24.  FieldSubscreen: mode 0x00, state 0x08, cgear
 * 0x20 (the ReportWork in mode 7).  GameEvent: seq 0x08, work 0x0C.
 * ReportEventWork: gameSystem 0x04, fieldMap 0x08, heapId 0x10 (u16), res
 * 0x18.  ReportEventRes: message 0x18, taskMenu 0x40.  ReportWork:
 * saveCountCurrent 0x114, saveCountMax 0x118 (ov034/report_types.h). */
enum {
    RS_ASK_PRINT = 2, RS_ASK_WAIT = 3, RS_YES_NO = 4, RS_SAVING_PRINT = 5,
    RS_SAVE = 9, RS_SAVED = 10, RS_FAILED = 13, RS_WAIT_MESSAGE = 14,
    RS_WAIT_KEY = 15, RS_WAIT_TIMEOUT = 16, RS_CANCEL = 17
};

typedef struct SaveInfo {
    void *rew;              /* the report event's work                      */
    int   state;
    void *sub;
} SaveInfo;

static int s_save_disp;     /* the save bar's eased fill, 0..1000 */
static char s_save_text[256]; /* the last message line shown while saving */

static int save_read(SaveInfo *s)
{
    void *gs, *fm, *sub, *ev, *rew;

    memset(s, 0, sizeof *s);
    if (!MN_GAME) {
        return 0;
    }
    gs = game_system();
    if (gs == NULL) {
        return 0;
    }
    fm = rdp(gs, 0x20);
    ev = rdp(gs, 0x18);
    if (fm == NULL || ev == NULL) {
        return 0;
    }
    sub = rdp(fm, 0x24);
    if (sub == NULL || rd32(sub, 0x00) != 7u || !bw_overlay_is_loaded(34)) {
        return 0;
    }
    rew = rdp(ev, 0x0C);
    if (rew == NULL || rdp(rew, 0x04) != gs || rdp(rew, 0x08) != fm) {
        return 0;
    }
    s->rew = rew;
    s->sub = sub;
    s->state = (int)rd32(ev, 0x08);
    return 1;
}

static int save_match(void)
{
    SaveInfo s;

    return save_read(&s);
}

static const char *place_name(void *gs, char *out, int max)
{
    out[0] = '\0';
    if (GAMESYSTEM_GetMyPlayerWork != NULL && GAME_DATA_GetPlayerZoneId != NULL &&
        ZONEDATA_GetPlaceNameId != NULL) {
        void *pw = GAMESYSTEM_GetMyPlayerWork(gs);

        if (pw != NULL) {
            u32 id = ZONEDATA_GetPlaceNameId(GAME_DATA_GetPlayerZoneId(pw));

            bank_text(2, 0x59, id, NULL, out, max);
        }
    }
    return out;
}

/* One row of the info block: label left, value right. */
static void info_line(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value)
{
    ui_panel(c, x, y, w, 15, CUT_DEF, 3, UI_BODY2, UI_EDGE, 255);
    box_text(c, x, y, w, 15, label, UI_GREY, UI_EDGE, UI_FONT_LARGE, 0, 6);
    box_text(c, x, y, w, 15, value, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 2, 8);
}

/* The text of the report event's current message, or "" when it has none. */
static void event_message(const SaveInfo *s, char *out, int max)
{
    void *res;

    out[0] = '\0';
    if (s->state >= RS_CANCEL || STRBUF_ToChars == NULL) {
        return;
    }
    res = rdp(s->rew, 0x18);
    if (res != NULL && rdp(res, 0x18) != NULL) {
        u16 chars[256];

        memset(chars, 0xFF, sizeof chars);
        STRBUF_ToChars(rdp(res, 0x18), chars, 255);
        chars[255] = 0xFFFFu;
        units_utf8(chars, 255, out, max);
    }
}

static void save_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    SaveInfo s;
    Modal m;
    void *gs, *gd, *misc;
    char place[64], buf[96], msg[256];
    int x, y, w, i, st_yes_no, answer = -1, busy, msgmode, compact;
    time_t now;
    struct tm tmb, *tm;
    EidosUiRect hr;
    const char *pairs[4];

    g_frame = in->frame;
    pend_pump();
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hide_2d = 1;
    if (!save_read(&s)) {
        return;
    }
    gs = game_system();
    gd = game_data();
    misc = (gd != NULL && GAME_DATA_GetMiscSave != NULL) ? GAME_DATA_GetMiscSave(gd) : NULL;

    compact = c->canvas->h - ui_bar_h(c) - 6 < 196;
    m = modal_rect(c, 60, 260, 320, 80, compact ? 162 : 196, 220, compact ? 162 : 196);
    ui_panel(c, m.x, m.y, m.w, m.h, CUT_DEF, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_header(c, m.x + 4, m.y + 4, m.w - 8, "SAVE", NULL, NULL, UI_SKY);

    x = m.x + 10;
    w = m.w - 20;
    y = m.y + 29;
    /* the place and the date */
    ui_text(c, x, y + text_cy(15, UI_FONT_LARGE), place_name(gs, place, (int)sizeof place),
            UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    now = time(NULL);
    tm = bw_localtime((long long)now, &tmb) == 0 ? &tmb : NULL;
    if (tm != NULL) {
        snprintf(buf, sizeof buf, "%02d/%02d/%04d  %02d:%02d", tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
        box_text(c, x, y, w, 15, buf, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 2, 0);
    }
    y += 16;

    /* the party (a short canvas leaves it out) */
    if (!compact) {
        void *party = (gd != NULL && GAME_DATA_GetMyParty != NULL) ? GAME_DATA_GetMyParty(gd) : NULL;
        u32 n = (party != NULL && POKEPARTY_GetCount != NULL) ? POKEPARTY_GetCount(party) : 0;
        int frame = (int)((in->frame / 20u) & 1u);

        if (n > 6) n = 6;
        for (i = 0; i < (int)n; i++) {
            void *pp = POKEPARTY_GetMember(party, (u32)i);
            const UiImage *im;

            if (pp == NULL || POKE_TOOL_GetPartyValue == NULL) {
                continue;
            }
            im = ui_mon_icon((u16)POKE_TOOL_GetPartyValue(pp, PARAM_SPECIES, 0),
                             (u8)POKE_TOOL_GetPartyValue(pp, PARAM_FORM, 0), frame);
            if (im != NULL) {
                ui_image(c, x + 2 + i * 34, y + (32 - im->h) / 2, im, 1);
            }
        }
    }
    y += compact ? 2 : 33;

    /* the facts */
    {
        u32 badges = (misc != NULL && MISC_CountBadges != NULL) ? MISC_CountBadges(misc) : 0;
        void *ev = (gd != NULL && GAME_DATA_GetEventWork != NULL) ? GAME_DATA_GetEventWork(gd) : NULL;
        int dex_shown = ev != NULL && ZONEDATA_CheckEventFlag != NULL && ZONEDATA_CheckEventFlag(ev, 0x962) == 1u;
        void *pt = (gd != NULL && GAME_DATA_GetPlayTime != NULL) ? GAME_DATA_GetPlayTime(gd) : NULL;

        snprintf(buf, sizeof buf, "%u", badges);
        info_line(c, x, y, w, "GYM BADGES", buf);
        y += 17;
        if (dex_shown && ZUKAN_SAVE_CountSeen != NULL && ZUKAN_SAVE_GetFromGameData != NULL) {
            snprintf(buf, sizeof buf, "%u", ZUKAN_SAVE_CountSeen(ZUKAN_SAVE_GetFromGameData(gd),
                                                               rd16(s.rew, 0x10)));
            info_line(c, x, y, w, "POK\xC3\x89" "DEX", buf);
            y += 17;
        }
        if (pt != NULL && PLAYTIME_GetHours != NULL) {
            snprintf(buf, sizeof buf, "%u:%02u", (unsigned)PLAYTIME_GetHours(pt),
                     (unsigned)PLAYTIME_GetMinutes(pt));
            info_line(c, x, y, w, "TIME", buf);
            y += 17;
        }
    }

    /* the question, the answers, the progress */
    event_message(&s, msg, (int)sizeof msg);
    msgmode = s.state >= RS_SAVING_PRINT;
    /* states 10 and 11 still hold the question's text: the write is over
     * and the game has not printed "saved" yet, so the bar stays, full */
    busy = s.state >= RS_SAVING_PRINT && s.state <= RS_SAVED + 1;
    if (!busy) {
        s_save_disp = 0;
    }
    y += 2;
    if (busy) {
        /* the game's own words for the save in progress */
        const char *q = game_text(3, 0x9E, 4, NULL);

        snprintf(msg, sizeof msg, "%s", q[0] != '\0' ? q : "Saving... Don't turn off the power.");
    } else if (msgmode && msg[0] == '\0' && s_save_text[0] != '\0') {
        /* the game clears its message for a frame or two between the
         * saving line and "saved": keep the last line, not the question */
        snprintf(msg, sizeof msg, "%s", s_save_text);
    } else if (!msgmode || msg[0] == '\0') {
        const char *q = game_text(3, 0x9E, 2, NULL);

        snprintf(msg, sizeof msg, "%s", q[0] != '\0' ? q : "Would you like to save the game?");
    }
    if (msgmode) {
        snprintf(s_save_text, sizeof s_save_text, "%s", msg);
    } else {
        s_save_text[0] = '\0';
    }
    {
        int lines = wrapped(c, x, y + 1, w, 15, UI_FONT_LARGE, msg, UI_WHITE, UI_EDGE, 3);

        if (lines < 1) lines = 1;
        y += lines * 15 + 4;
    }
    st_yes_no = s.state == RS_YES_NO;
    if (!msgmode) {
        const char *ch[2];
        u8 cur = 0;
        void *res = rdp(s.rew, 0x18);
        void *menu = res != NULL ? rdp(res, 0x40) : NULL;

        ch[0] = game_text(2, 168, 8, NULL);
        ch[1] = game_text(2, 168, 9, NULL);
        if (ch[0][0] == '\0') ch[0] = "YES";
        if (ch[1][0] == '\0') ch[1] = "NO";
        if (st_yes_no && menu != NULL && APP_TASKMENU_GetCursorPos != NULL) {
            cur = APP_TASKMENU_GetCursorPos(menu);
        }
        int other_hover = 0;

        for (i = 0; i < 2 && st_yes_no; i++) {
            hr.x = m.x + m.w - 10 - 62 * (2 - i) + 2; hr.y = y; hr.w = 60; hr.h = 18;
            if (eidos_ui_hit(hr) & EIDOS_UI_HOVER) {
                other_hover = 1;
            }
        }
        for (i = 0; i < 2; i++) {
            int bx = m.x + m.w - 10 - 62 * (2 - i) + 2;
            int st = 0;

            hr.x = bx; hr.y = y; hr.w = 60; hr.h = 18;
            if (st_yes_no) {
                st = eidos_ui_hit(hr);
            }
            /* the chosen one is the game's cursor, until the mouse is over
             * the other */
            pill(c, bx, y, 60, 18, ch[i],
                 (st_yes_no && (int)cur == i && !other_hover) ? 1 : ((st & EIDOS_UI_HOVER) ? 2 : 0),
                 UI_SKY, UI_CYAN2, UI_FONT_LARGE);
            if (st & EIDOS_UI_CLICK) {
                answer = i;
            }
        }
        if (!st_yes_no) {
            ui_spinner(c, x + 2, y + 3, (int)(in->frame / 4u), UI_SKY);
        }
        y += 22;
    } else if (busy) {
        /* the game's own bar: ten pieces light as the write advances
         * (ReportWork barPiecesLit 0x124); the fill eases toward it */
        void *rw = rdp(s.sub, 0x20);
        int lit = bw_ds_game_ptr(rw) ? (int)rd16(rw, 0x124) : 0;
        int goal;

        /* the game lights ten pieces for twice the data a save writes, so
         * the write is done at the fifth piece */
        if (lit > 5) lit = 5;
        goal = s.state > RS_SAVE ? 1000 : lit * 200;
        if (s_save_disp < goal) {
            s_save_disp += (goal - s_save_disp + 7) / 8;
        } else if (s_save_disp > goal) {
            s_save_disp = goal;
        }
        if (s.state <= RS_SAVE && lit < 5 && s_save_disp >= goal && s_save_disp < goal + 180 && (in->frame % 3u) == 0) {
            s_save_disp++;      /* creeps while the game writes toward its next piece */
        }
        ui_spinner(c, x + 2, y + 2, (int)(in->frame / 4u), UI_SKY);
        ui_progress(c, x + 22, y + 5, w - 22, s_save_disp, 1000, UI_SKY);
        if (debug_on()) {
            fprintf(stderr, "ui: save bar lit %d disp %d state %d\n", lit, s_save_disp, s.state);
        }
        y += 22;
    } else {
        /* a message waiting for A or B, or for its own time out */
        hr.x = m.x; hr.y = m.y; hr.w = m.w; hr.h = m.h;
        if (eidos_ui_hit(hr) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        y += 4;
    }

    /* the last save */
    if (SAVE_CONTROL_WouldOverwriteSave != NULL && SAVE_CONTROL_HasFlashSave != NULL &&
        GAME_DATA_GetSaveControl != NULL && SYSTEM_DATA_GetPlayTime != NULL && gd != NULL &&
        PLAYTIME_GetStampYear != NULL) {
        void *sc = GAME_DATA_GetSaveControl(gd);

        if (sc != NULL && SAVE_CONTROL_WouldOverwriteSave(sc) == 0 && SAVE_CONTROL_HasFlashSave(sc) == 1) {
            void *pt = SYSTEM_DATA_GetPlayTime(sc);

            if (pt != NULL) {
                snprintf(buf, sizeof buf, ui_trf("Last saved %u/%u/20%02u  %02u:%02u"),
                         PLAYTIME_GetStampMonth(pt), PLAYTIME_GetStampDay(pt),
                         PLAYTIME_GetStampYear(pt), PLAYTIME_GetStampHour(pt),
                         PLAYTIME_GetStampMinute(pt));
                ui_text(c, x, m.y + m.h - 6 - 10, buf, UI_EDGE2, -1, UI_FONT_SMALL, 1);
            }
        }
    }

    if (debug_on() && (in->frame % 60u) == 0) {
        fprintf(stderr, "ui: save state %d msg \"%s\"\n", s.state, msg);
    }

    /* the answers: the game's own yes/no rectangles */
    if (answer == 0) {
        tap(224, 60);
    } else if (answer == 1) {
        tap(224, 84);
    }

    /* the footer */
    pairs[0] = "A"; pairs[1] = msgmode ? "OK" : "Choose";
    pairs[2] = "B"; pairs[3] = "Back";
    i = hint_bar(c, in, pairs, msgmode ? 1 : 2);
    if (i == 0) {
        /* A decides the game's cursor; after a touch the cursor is hidden
         * and the first A only brings it back, so the answer is tapped */
        if (st_yes_no) {
            void *res = rdp(s.rew, 0x18);
            void *menu = res != NULL ? rdp(res, 0x40) : NULL;
            u8 cur = (menu != NULL && APP_TASKMENU_GetCursorPos != NULL) ? APP_TASKMENU_GetCursorPos(menu) : 0;

            tap(224, cur == 0 ? 60 : 84);
        } else {
            eidos_ui_press(BW_KEY_A, 2);
        }
    } else if (i == 1) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    if (st_yes_no && (eidos_ui_right_click() || outside_click(c, m))) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void save_reset(void)
{
    s_save_disp = 0;
    pend_clear();
}

static const EidosUiRecipe recipe_save = {
    "save", save_match, save_run, save_reset, 0
};

/* ------------------------------------------------------------------ */
/* Trainer card                                                        */
/* ------------------------------------------------------------------ */

/* The card runs on a proc of its own (TRCARD_SYS, ov010 code, overlay 111
 * resident) whose work holds a private ProcSys; the screens (ov111, ov110)
 * run on that (docs/unified-view-recipes.md section 9).  Which screen is up
 * is read from the work of the child's top proc, which holds the card's
 * profile in a place of its own: the card (VIEWER_MAIN: front, or back
 * when decoVisible) at 0x28, the badge viewer (VIEWER_ACTOR) in the
 * arguments at 0x108.  A screen that holds it in neither place is the name
 * editor, and the game's own screens show.
 *
 * TrainerCardWork (ov010/trcard_sys.c): childProc 0x08, param 0x18.
 * TrainerCardParam: profile 0x00.  TrainerCardProfile (ov010/
 * field_event_types.h, the card's own data, built from the save when the
 * card opens): badgeMask 0x08 (u16), name 0x0A (u16 x 8), playTime 0x1C
 * (PlayTime *), money 0x20, pokedexCount 0x24, trainerId 0x28 (u16),
 * startYear/Month/Day 0x2F..0x31, clearYear/Month/Day 0x32..0x34, record
 * sums 0x38.. (the back's rows).  ViewerWork (ov111/viewer_types.h):
 * decoVisible 0x3034 (1 on the back), state 0x32C6. */
enum { CK_NONE = 0, CK_CARD, CK_BADGE, CK_BETWEEN };

typedef struct CardInfo {
    int   kind;
    void *work;             /* TrainerCardWork                              */
    void *prof;             /* TrainerCardProfile                           */
    void *vw;               /* ViewerWork (kind CK_CARD)                    */
    int   back;             /* the card shows its back                      */
    int   stable;           /* the card is not turning                      */
} CardInfo;

/* A pointer into the DS main memory (the game's heaps), the only kind the
 * card's structs hold. */
static int plausible(const void *p)
{
    return bw_ds_game_ptr(p) && ((uintptr_t)p & 3u) == 0u;
}

static int card_read(CardInfo *ci)
{
    void *gs, *cur, *work, *child, *ccur, *param, *vw;

    memset(ci, 0, sizeof *ci);
    if (!MN_GAME || !bw_overlay_is_loaded(111)) {
        return 0;
    }
    gs = game_system();
    cur = gs != NULL ? current_proc(gs) : NULL;
    if (cur == NULL || (int)rd32(cur, 0x04) != 111 || rd32(cur, 0x08) < 2u) {
        return 0;
    }
    work = rdp(cur, 0x14);
    if (!plausible(work)) {
        return 0;
    }
    param = rdp(work, 0x18);
    if (!plausible(param) || !plausible(rdp(param, 0x00))) {
        return 0;
    }
    ci->work = work;
    ci->prof = rdp(param, 0x00);
    child = rdp(work, 0x08);
    ccur = plausible(child) ? rdp(child, 0x08) : NULL;
    if (ccur == NULL) {
        ci->kind = CK_BETWEEN;
        return 1;
    }
    /* The screen on top is the one whose work holds the card's profile: the
     * card keeps it at 0x28 (ViewerWork.info), the badge viewer in the
     * arguments it was given (ViewerTextWork.parent, 0x108; ViewerArgs.info
     * first).  Anything else is the name editor or a screen between. */
    vw = rdp(ccur, 0x14);
    if (!plausible(vw)) {
        ci->kind = CK_BETWEEN;
        return 1;
    }
    if (rdp(vw, 0x28) == ci->prof) {
        ci->kind = CK_CARD;
        ci->vw = vw;
        ci->back = rd32(vw, 0x3034) != 0u;
        ci->stable = rd8(vw, 0x32C6) == 0u || rd8(vw, 0x32C6) == 5u;
        return 1;
    }
    if (plausible(rdp(vw, 0x108)) && rdp(rdp(vw, 0x108), 0x00) == ci->prof) {
        ci->kind = CK_BADGE;
        return 1;
    }
    return 0;                   /* the name editor: the game's own screens */
}

static int card_match(void)
{
    CardInfo ci;

    return card_read(&ci);
}

/* ---- the badge case: the eight silhouettes of the game's own art ---- */

/* The case is a 256 x 80 picture in the badge viewer's graphics: archive
 * a/0/4/0, character member 25 (4 bits per pixel, the picture's tiles
 * stored one after the other from tile 32), palette member 1.  Palette
 * index 2 is the plate, 1 the silhouettes' fill (and the frame's lines),
 * 4 5 6 7 14 15 their outlines.  The silhouettes are the pieces that do
 * not touch the picture's edge. */
#define CASE_W 256
#define CASE_H 80

typedef struct Silhouette {
    int x, y, w, h;         /* the piece's box in the case picture          */
    u8 *mask;               /* w * h: 0 none, 1 fill, 2 outline             */
} Silhouette;

static Silhouette g_sil[8];
static int g_sil_n = -1;    /* -1 not tried, else the count found          */

static const u8 *arc_member_alloc(const char *path, u32 member, u32 *size)
{
    BwArcFile f;
    long n;
    u8 *buf = NULL;

    if (bw_arc_open(&f, path) != 0) {
        return NULL;
    }
    n = bw_arc_member_size(&f, member);
    if (n > 0) {
        buf = (u8 *)malloc((size_t)n);
        if (buf != NULL && bw_arc_read_member(&f, member, buf, (u32)n) != n) {
            free(buf);
            buf = NULL;
        }
        if (buf != NULL && size != NULL) {
            *size = (u32)n;
        }
    }
    bw_arc_close(&f);
    return buf;
}

static int sil_cmp(const void *a, const void *b)
{
    return ((const Silhouette *)a)->x - ((const Silhouette *)b)->x;
}

/* A pixel of a piece: not the plate, and not the picture's frame (its
 * border bands and the line between the two plates). */
static int sil_ink(const u8 *px, int x, int y)
{
    u8 v = px[y * CASE_W + x];

    if (v == 2 || v == 0 || x <= 1 || x >= CASE_W - 1 || y <= 4 || y >= CASE_H - 5 ||
        x == 127 || x == 128) {
        return 0;
    }
    return 1;
}

static void sil_load(void)
{
    u32 size = 0;
    const u8 *chr, *i, *end;
    static u8 px[CASE_W * CASE_H];
    static int label[CASE_W * CASE_H];
    static int stack[CASE_W * CASE_H];
    int x, y, found = 0, ncomp = 0;

    g_sil_n = 0;
    chr = arc_member_alloc("a/0/4/0", 25, &size);
    if (debug_on()) {
        fprintf(stderr, "ui: case member %p size %u\n", (const void *)chr, size);
    }
    if (chr == NULL) {
        return;
    }
    end = chr + size;
    for (i = chr; i + 8 < end; i++) {
        if (i[0] == 'R' && i[1] == 'A' && i[2] == 'H' && i[3] == 'C') {
            break;
        }
    }
    if (i + 8 >= end || i + 0x20 + 352u * 32u > end) {
        free((void *)chr);
        return;
    }
    {
        const u8 *tiles = i + 0x20;

        for (y = 0; y < CASE_H; y++) {
            for (x = 0; x < CASE_W; x++) {
                u32 tn = 32u + (u32)(y >> 3) * 32u + (u32)(x >> 3);
                u8 b = tiles[tn * 32u + (u32)(((y & 7) * 4) + ((x & 7) >> 1))];

                px[y * CASE_W + x] = (x & 1) ? (u8)(b >> 4) : (u8)(b & 15);
            }
        }
    }
    free((void *)chr);
    memset(label, 0, sizeof label);
    for (y = 0; y < CASE_H; y++) {
        for (x = 0; x < CASE_W; x++) {
            int sp = 0, minx = x, maxx = x, miny = y, maxy = y, area = 0, edge = 0;

            if (label[y * CASE_W + x] != 0 || !sil_ink(px, x, y)) {
                continue;
            }
            ncomp++;
            stack[sp++] = y * CASE_W + x;
            label[y * CASE_W + x] = ncomp;
            while (sp > 0) {
                int p = stack[--sp], cx = p % CASE_W, cy = p / CASE_W, d;
                static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };

                area++;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
                if (cx == 0 || cy == 0 || cx == CASE_W - 1 || cy == CASE_H - 1) {
                    edge = 1;
                }
                for (d = 0; d < 4; d++) {
                    int nx = cx + dx[d], ny = cy + dy[d], q;

                    if (nx < 0 || ny < 0 || nx >= CASE_W || ny >= CASE_H) {
                        continue;
                    }
                    q = ny * CASE_W + nx;
                    if (label[q] == 0 && sil_ink(px, nx, ny)) {
                        label[q] = ncomp;
                        stack[sp++] = q;
                    }
                }
            }
            (void)edge;
            if (area > 120 && found < 8) {
                Silhouette *s = &g_sil[found];
                int sx, sy, lab = ncomp;

                s->x = minx;
                s->y = miny;
                s->w = maxx - minx + 1;
                s->h = maxy - miny + 1;
                s->mask = (u8 *)calloc((size_t)s->w * (size_t)s->h, 1);
                if (s->mask == NULL) {
                    continue;
                }
                for (sy = 0; sy < s->h; sy++) {
                    for (sx = 0; sx < s->w; sx++) {
                        int q = (miny + sy) * CASE_W + minx + sx;

                        if (label[q] == lab) {
                            s->mask[sy * s->w + sx] = px[q] == 1 ? 1 : 2;
                        }
                    }
                }
                found++;
            }
        }
    }
    if (debug_on()) {
        fprintf(stderr, "ui: silhouettes found %d components %d\n", found, ncomp);
    }
    if (found == 8) {
        qsort(g_sil, 8, sizeof g_sil[0], sil_cmp);
        g_sil_n = 8;
    } else {
        for (x = 0; x < found; x++) {
            free(g_sil[x].mask);
            g_sil[x].mask = NULL;
        }
        g_sil_n = 0;
    }
}

/* Draw silhouette n scaled to fit a box (w x h), fill and outline colours;
 * each target pixel takes the share of the pieces' pixels it covers. */
static void sil_draw(EidosUiCtx *c, int n, int x, int y, int w, int h, u32 fill, u32 line)
{
    const Silhouette *s;
    int tw, th, dx, dy;

    if (g_sil_n != 8 || n < 0 || n > 7) {
        return;
    }
    s = &g_sil[n];
    /* fit: keep the aspect */
    th = h;
    tw = s->w * h / s->h;
    if (tw > w) {
        tw = w;
        th = s->h * w / s->w;
    }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    x += (w - tw) / 2;
    y += (h - th) / 2;
    for (dy = 0; dy < th; dy++) {
        int sy0 = dy * s->h / th, sy1 = (dy + 1) * s->h / th;

        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (dx = 0; dx < tw; dx++) {
            int sx0 = dx * s->w / tw, sx1 = (dx + 1) * s->w / tw;
            int cnt = 0, fl = 0, ln = 0, sx, sy;

            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1 && sy < s->h; sy++) {
                for (sx = sx0; sx < sx1 && sx < s->w; sx++) {
                    u8 m = s->mask[sy * s->w + sx];

                    cnt++;
                    if (m == 1) fl++;
                    else if (m == 2) ln++;
                }
            }
            if (cnt > 0 && fl + ln > 0) {
                int a = (fl + ln) * 255 / cnt;

                ui_fill(c, x + dx, y + dy, 1, 1, ln * 2 > fl + ln ? line : fill, a);
            }
        }
    }
}

/* ---- the trainer's picture, from the card's own data ---- */

/* The card's front draws the player on a 256 colour background layer
 * (ov111 VIEWER_MAIN_LoadBgResources): the characters are a/0/4/0 member
 * 0x1F, the screen member 0x2B for the hero and 0x2C for the heroine, and
 * the figures' colours are entries 64 to 127 of the card's palettes (the
 * same in every one of members 5 to 0x14).  The figure stands inside the
 * picture window, x 160 to 239 and y 16 to 95 of the screen; colour 0 is
 * see-through. */
#define PIC_X 160
#define PIC_Y 16
#define PIC_W 80
#define PIC_H 80
#define PIC_CHARS   0x1F
#define PIC_SCREEN  0x2B
#define PIC_PALETTE 6

static u8 g_pic[PIC_W * PIC_H * 4];
static int g_pic_ok = -1;       /* -1 not loaded, 0 no data, 1 loaded */
static int g_pic_sex = -1;
static UiImage g_pic_im = { PIC_W, PIC_H, g_pic };

/* The body of a section of a Nitro file, or NULL. */
static const u8 *nitro_block(const u8 *d, u32 size, const char *magic, u32 *bsize)
{
    u32 off = 16;

    while (off + 8 <= size) {
        u32 len = rd32(d, off + 4);

        if (memcmp(d + off, magic, 4) == 0 && len >= 8 && off + len <= size) {
            *bsize = len - 8;
            return d + off + 8;
        }
        if (len < 8) {
            break;
        }
        off += len;
    }
    return NULL;
}

static void pic_load(int sex)
{
    u32 cs = 0, ss = 0, ps = 0, cb, sb, pb;
    u8 *cf = ui_arc_member(UI_ARC_CARD, PIC_CHARS, &cs);
    u8 *sf = ui_arc_member(UI_ARC_CARD, PIC_SCREEN + (sex == 1), &ss);
    u8 *pf = ui_arc_member(UI_ARC_CARD, PIC_PALETTE, &ps);
    const u8 *chr = cf != NULL ? nitro_block(cf, cs, "RAHC", &cb) : NULL;
    const u8 *scr = sf != NULL ? nitro_block(sf, ss, "NRCS", &sb) : NULL;
    const u8 *pal = pf != NULL ? nitro_block(pf, ps, "TTLP", &pb) : NULL;

    g_pic_ok = 0;
    g_pic_sex = sex;
    if (chr != NULL && scr != NULL && pal != NULL && cb > 0x18 && sb > 0x0C && pb > 0x10) {
        u32 csize = rd32(chr, 0x10), ntiles, sw, x, y;
        const u8 *tiles = chr + 0x18;
        const u8 *map = scr + 0x0C;
        const u8 *cols = pal + 0x10;
        u32 ncols = rd32(pal, 0x08) / 2;

        if (csize > cb - 0x18) {
            csize = cb - 0x18;
        }
        ntiles = csize / 64;
        sw = rd16(scr, 0) / 8;
        for (y = 0; y < PIC_H; y++) {
            for (x = 0; x < PIC_W; x++) {
                u32 sx = PIC_X + x, sy = PIC_Y + y, at = (sy / 8) * sw + sx / 8;
                u32 e = at * 2 + 1 < sb - 0x0C ? rd16(map, at * 2) : 0;
                u32 t = e & 0x3FF, tx = sx & 7, ty = sy & 7, v = 0;
                u8 *d = g_pic + ((size_t)y * PIC_W + x) * 4;

                if (e & 0x400) tx = 7 - tx;
                if (e & 0x800) ty = 7 - ty;
                if (t < ntiles) {
                    v = tiles[t * 64 + ty * 8 + tx];
                }
                if (v != 0 && v < ncols) {
                    u32 c = rd16(cols, v * 2);

                    d[0] = (u8)((c & 31) * 255 / 31);
                    d[1] = (u8)(((c >> 5) & 31) * 255 / 31);
                    d[2] = (u8)(((c >> 10) & 31) * 255 / 31);
                    d[3] = 255;
                } else {
                    d[0] = d[1] = d[2] = d[3] = 0;
                }
            }
        }
        g_pic_ok = 1;
    }
    free(cf);
    free(sf);
    free(pf);
}

/* ---- the card ---- */

typedef struct CardState {
    int   scroll;           /* the back's list, in rows                      */
    u32   cool;             /* no new flip before this frame                 */
} CardState;

static CardState g_card;
static void *g_card_work;

static void card_reset(void)
{
    memset(&g_card, 0, sizeof g_card);
    g_card_work = NULL;
    pend_clear();
}

typedef struct TapQ { u32 at; int x, y; } TapQ;
static TapQ g_tapq[4];

static void tapq_put(u32 delay, int x, int y)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_tapq[i].at == 0) {
            g_tapq[i].at = g_frame + delay + 1;
            g_tapq[i].x = x;
            g_tapq[i].y = y;
            return;
        }
    }
}

static void tapq_pump(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_tapq[i].at != 0 && g_frame + 1 >= g_tapq[i].at) {
            tap(g_tapq[i].x, g_tapq[i].y);
            if (debug_on()) {
                fprintf(stderr, "ui: card tap %d,%d at %u\n", g_tapq[i].x, g_tapq[i].y, g_frame);
            }
            g_tapq[i].at = 0;
        }
    }
}

static int tapq_busy(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_tapq[i].at != 0) {
            return 1;
        }
    }
    return 0;
}

/* The bottom-screen buttons of the card (ov111 d_021C8594): the badge
 * viewer's button, the turn button, the return button. */
#define CARD_TAP_BADGES_X 16
#define CARD_TAP_TURN_X   132
#define CARD_TAP_RETURN_X 240
#define CARD_TAP_Y        180

/* Turn the card: on the card one tap; on the badge viewer first to the
 * card, then the turn. */
static void card_flip(const CardInfo *ci, const EidosUiFrame *in)
{
    if (debug_on()) {
        fprintf(stderr, "ui: card flip kind %d cool %u busy %d\n", ci->kind, g_card.cool, tapq_busy());
    }
    if (in->frame < g_card.cool || tapq_busy()) {
        return;
    }
    g_card.cool = in->frame + 24;
    if (ci->kind == CK_CARD) {
        tapq_put(0, CARD_TAP_TURN_X, CARD_TAP_Y);
    } else if (ci->kind == CK_BADGE) {
        tapq_put(0, CARD_TAP_BADGES_X, CARD_TAP_Y);
        tapq_put(70, CARD_TAP_TURN_X, CARD_TAP_Y);
    }
}

static void card_back(const CardInfo *ci)
{
    (void)ci;
    if (!tapq_busy()) {
        tap(CARD_TAP_RETURN_X, CARD_TAP_Y);
    }
}

static const char *bank_label(u32 idx, const char *fallback)
{
    const char *t = game_text(2, 0xBB, idx, NULL);

    return t[0] != '\0' ? t : fallback;
}

/* A row of the card: label left, value right (with the pokedollar glyph
 * before a money value). */
static void card_row(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                     const char *value, int money)
{
    int vw = ui_text_w(value, UI_FONT_LARGE);

    ui_panel(c, x, y, w, h, CUT_DEF, 4, 0x04201Au, 0x0A4636u, 235);
    box_text(c, x, y, w, h, label, UI_CARD_L, UI_CARD_D, UI_FONT_LARGE, 0, 6);
    box_text(c, x, y, w, h, value, UI_WHITE, UI_CARD_D, UI_FONT_LARGE, 2, 8);
    if (money) {
        ui_text(c, x + w - 8 - vw - 13, y + text_cy(h, UI_FONT_LARGE), "\xEF\xBF\xA5", UI_WHITE,
                UI_CARD_D, UI_FONT_LARGE, 1);
    }
}

typedef struct BackRow { const char *label; char value[32]; } BackRow;

#define CARD_ROW_MAX 24

static int back_rows(const CardInfo *ci, BackRow *rows)
{
    void *p = ci->prof;
    int n = 0;
    u32 flags = rd32(p, 0x68);
    void *pt = rdp(p, 0x1C);
    static const u32 rec[7] = { 0x38, 0x3C, 0x40, 0x44, 0x4C, 0x50, 0x54 };
    int i;

    rows[n].label = bank_label(5, "ID No.");
    snprintf(rows[n++].value, sizeof rows[0].value, "%05u", rd16(p, 0x28));
    rows[n].label = bank_label(6, "TIME");
    if (pt != NULL && PLAYTIME_GetHours != NULL) {
        snprintf(rows[n].value, sizeof rows[0].value, "%u:%02u", (unsigned)PLAYTIME_GetHours(pt),
                 (unsigned)PLAYTIME_GetMinutes(pt));
    } else {
        rows[n].value[0] = '\0';
    }
    n++;
    rows[n].label = bank_label(7, "ADVENTURE STARTED");
    snprintf(rows[n++].value, sizeof rows[0].value, "%u/%u/%u", rd8(p, 0x30), rd8(p, 0x31),
             2000u + rd8(p, 0x2F));
    rows[n].label = bank_label(8, "HALL OF FAME DEBUT");
    if (rd8(p, 0x33) != 0u) {
        snprintf(rows[n++].value, sizeof rows[0].value, "%u/%u/%u", rd8(p, 0x33), rd8(p, 0x34),
                 2000u + rd8(p, 0x32));
    } else {
        snprintf(rows[n++].value, sizeof rows[0].value, "---");
    }
    for (i = 0; i < 7; i++) {
        rows[n].label = bank_label(9u + (u32)i, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd32(p, rec[i]));
    }
    rows[n].label = bank_label(16, "");
    snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd32(p, 0x58));
    if (flags & 1u) {
        rows[n].label = bank_label(17, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd16(p, 0x5C));
    }
    if (flags & 2u) {
        rows[n].label = bank_label(18, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd16(p, 0x5E));
    }
    if (flags & 4u) {
        rows[n].label = bank_label(19, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd16(p, 0x60));
    }
    if (flags & 8u) {
        rows[n].label = bank_label(20, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd16(p, 0x62));
    }
    if (flags & 16u) {
        rows[n].label = bank_label(21, "");
        snprintf(rows[n++].value, sizeof rows[0].value, "%u", rd16(p, 0x64));
    }
    return n;
}

/* The case along the card's bottom: the eight silhouettes, the earned
 * ones bright. */
static void card_case(EidosUiCtx *c, int x, int cy, int w, u16 mask)
{
    int cw = (w - 8) / 8, cx0 = x + 4 + ((w - 8) - cw * 8) / 2, slot;

    ui_panel(c, x, cy, w, 36, CUT_DEF, 5, 0x03140Fu, 0x0A4636u, 255);
    for (slot = 0; slot < 8; slot++) {
        int earned = (mask >> slot) & 1;

        if (g_sil_n == 8) {
            sil_draw(c, slot, cx0 + slot * cw + 2, cy + 3, cw - 4, 30,
                     earned ? UI_CARD : 0x104032u, earned ? UI_CARD_L : 0x185646u);
        } else {
            ui_ellipse(c, cx0 + slot * cw + cw / 2 - 5, cy + 13, 10, 10,
                       earned ? UI_CARD : 0x104032u, UI_NONE, 255);
        }
    }
}

extern u32 MYSTATUS_GetSex(const void *mystatus) __attribute__((weak));
extern void *GAME_DATA_GetMyStatus(void *gameData) __attribute__((weak));

static void card_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    CardInfo ci;
    Modal m;
    EidosUiRect hr;
    char buf[64], name[40];
    const char *pairs[6];
    int x, w, i, rows_w, hint, compact;
    const u16 *nm;
    int back_face;

    g_frame = in->frame;
    pend_pump();
    tapq_pump();
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    if (!card_read(&ci)) {
        return;
    }
    if (ci.work != g_card_work) {
        card_reset();
        g_card_work = ci.work;
    }
    if (g_sil_n < 0) {
        sil_load();
    }

    back_face = ci.kind == CK_CARD && ci.back;

    compact = c->canvas->h - ui_bar_h(c) - 6 < 190;
    m = modal_rect(c, 72, 300, 380, 80, compact ? 146 : 190, 220, compact ? 146 : 190);
    ui_panel(c, m.x, m.y, m.w, m.h, CUT_DEF, 12, 0x06281Fu, UI_CARD, 248);
    ui_header(c, m.x + 4, m.y + 4, m.w - 8, "TRAINER CARD", back_face ? "RECORDS" : NULL, NULL, UI_CARD);

    x = m.x + 8;
    w = m.w - 16;
    /* the flip button in the title bar */
    {
        const char *fl = "FLIP";
        int fw = ui_text_w(fl, UI_FONT_SMALL) + 16, st;

        hr.x = m.x + m.w - 12 - fw; hr.y = m.y + 6; hr.w = fw; hr.h = 14;
        st = eidos_ui_hit(hr);
        pill(c, hr.x, hr.y, fw, 14, fl, (st & EIDOS_UI_HOVER) ? 2 : 0, UI_CARD, UI_CARD_D,
             UI_FONT_SMALL);
        if (st & EIDOS_UI_CLICK) {
            card_flip(&ci, in);
        }
    }

    nm = (const u16 *)((const u8 *)ci.prof + 0x0A);
    units_utf8(nm, 8, name, (int)sizeof name);

    if (!back_face) {
        void *pt = rdp(ci.prof, 0x1C);
        int cy = m.y + 32, rw = m.w - 112, picx = m.x + m.w - 96, picy = m.y + 43;
        u16 mask = (u16)rd16(ci.prof, 0x08);

        card_row(c, x, cy, rw, 19, bank_label(0, "NAME"), name, 0);
        cy += 21;
        snprintf(buf, sizeof buf, "%05u", rd16(ci.prof, 0x28));
        card_row(c, x, cy, rw, 19, bank_label(5, "ID No."), buf, 0);
        cy += 21;
        snprintf(buf, sizeof buf, "%u", rd32(ci.prof, 0x24));
        card_row(c, x, cy, rw, 19, bank_label(3, "POK\xC3\x89" "DEX"), buf, 0);
        cy += 21;
        if (pt != NULL && PLAYTIME_GetHours != NULL) {
            snprintf(buf, sizeof buf, "%u:%02u", (unsigned)PLAYTIME_GetHours(pt),
                     (unsigned)PLAYTIME_GetMinutes(pt));
        } else {
            buf[0] = '\0';
        }
        card_row(c, x, cy, rw, 19, bank_label(6, "TIME"), buf, 0);
        cy += 21;
        snprintf(buf, sizeof buf, "%u", rd32(ci.prof, 0x20));
        card_row(c, x, cy, rw, 19, bank_label(4, "MONEY"), buf, 1);

        /* the picture, over the card's ring */
        ui_ellipse(c, picx - 4, picy - 4, 88, 88, 0x0A3A2Eu, 0x0A4636u, 200);
        ui_ellipse(c, picx + 16, picy + 16, 48, 48, 0x06281Fu, 0x0A4636u, 200);
        ui_ellipse(c, picx + 6, picy + 66, 68, 12, 0x0A4636u, UI_NONE, 220);
        {
            void *gd = game_data();
            void *me = gd != NULL && GAME_DATA_GetMyStatus != NULL ? GAME_DATA_GetMyStatus(gd)
                                                                  : NULL;
            int sex = me != NULL && MYSTATUS_GetSex != NULL && MYSTATUS_GetSex(me) == 1;

            if (g_pic_ok < 0 || g_pic_sex != sex) {
                pic_load(sex);
            }
            if (g_pic_ok == 1) {
                ui_image(c, picx, picy, &g_pic_im, 1);
            }
        }

        /* the badge case (a short canvas leaves it out) */
        if (!compact) {
            card_case(c, x, m.y + m.h - 44, w, mask);
        }
    } else {
        BackRow rows[CARD_ROW_MAX];
        int n = back_rows(&ci, rows), shown, top, ry, rh = 15, step = 17;
        int area_y = m.y + 32, area_h = m.h - 32 - 10;

        shown = area_h / step;
        if (shown > n) shown = n;
        rows_w = m.w - 16 - 8;
        i = eidos_ui_wheel((EidosUiRect){ x, area_y, w, area_h });
        g_card.scroll -= i;
        if (g_card.scroll > n - shown) g_card.scroll = n - shown;
        if (g_card.scroll < 0) g_card.scroll = 0;
        top = g_card.scroll;
        ry = area_y;
        for (i = top; i < top + shown && i < n; i++) {
            ui_panel(c, x, ry, rows_w, rh, CUT_DEF, 3, 0x04201Au, 0x0A4636u, 235);
            box_text(c, x, ry, rows_w, rh, rows[i].label, UI_CARD_L, UI_CARD_D, UI_FONT_LARGE, 0, 6);
            box_text(c, x, ry, rows_w, rh, rows[i].value, UI_WHITE, UI_CARD_D, UI_FONT_LARGE, 2, 8);
            ry += step;
        }
        if (n > shown) {
            ui_scrollbar(c, x + rows_w + 2, area_y, shown * step - 2, top, shown, n, UI_CARD);
        }
    }

    /* the footer */
    pairs[0] = "\xE2\x86\x90\xE2\x86\x92"; pairs[1] = "Flip";
    pairs[2] = "B"; pairs[3] = "Back";
    hint = hint_bar(c, in, pairs, 2);
    if (hint == 0) {
        card_flip(&ci, in);
    } else if (hint == 1) {
        card_back(&ci);
    }
    if (eidos_ui_right_click() || outside_click(c, m)) {
        card_back(&ci);
    }
    /* the arrow keys turn the card on the card itself; on the badge viewer
     * they turn a badge, so the turn is made here */
    {
        u32 edge = eidos_ui_keys_pressed();

        if (ci.kind == CK_BADGE) {
            eidos_ui_take_keys(BW_KEY_LEFT | BW_KEY_RIGHT);
            if ((edge & (BW_KEY_LEFT | BW_KEY_RIGHT)) != 0u) {
                card_flip(&ci, in);
            }
        }
    }
    if (debug_on() && (in->frame % 120u) == 0) {
        fprintf(stderr, "ui: card kind %d back %d stable %d pic %d sil %d\n", ci.kind, ci.back,
                ci.stable, g_pic_ok, g_sil_n);
    }
}

static const EidosUiRecipe recipe_card = { "card", card_match, card_run, card_reset, 0 };

/* ------------------------------------------------------------------ */
/* The C-Gear's Wireless app                                           */
/* ------------------------------------------------------------------ */

/*
 * wireless: the C-Gear's Wireless app (overlay 122, the game's
 * INTRUDE_COMM_VIEW, src/game/ov122/intrude_comm_view.c).  The game draws
 * it on the lower screen alone; here it is a panel over the blurred field:
 * the Entralink and the Xtransceiver as two tiles, and the app's three
 * facts (the Union Room's players, a wireless distribution, a C-Gear
 * nearby) with the game's own words.  A tile taps the game's button, so
 * the game does what it does for a stylus; Back, B and a click outside tap
 * the game's return button.
 *
 * The app's work (IntrudeCommView, 0x3A8 bytes): state 0x08, args 0x344
 * (IntrudeCommViewArgs: active 0x10, 2 while a friend calls), icaAnime
 * (the game system) 0x354, selectedRow 0x35C, peerTimer 0x364 (bit 5: a
 * distribution), peerSum 0x36C (the Union Room's players), newLogSeen
 * 0x374 (a C-Gear seen), exitStarted 0x38C.  Its text is a/0/0/2 member
 * 23 (24 in Japanese): 2 Union Room, 3 the player count, 4 Wireless Distribution, 5 YES,
 * 6 NO, 12 C-Gear Detected.
 */
#define WL_OVERLAY 122

static int g_wl_sel;

static int wireless_match(void)
{
    void *w = app_work(WL_OVERLAY);

    return w != NULL && rdp(w, 0x344) != NULL && rd32(w, 0x38C) == 0;
}

static const char *wl_text(u32 idx, const char *fallback)
{
    const char *t = game_text(2, 23, idx, NULL);

    return t[0] != '\0' ? t : fallback;
}

static void wireless_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const struct {
        const char *name, *what;
        u32 accent, accent2;
        int tx, ty;
    } tiles[2] = {
        { "ENTRALINK", "Go to the Entralink.", 0x63DE31u, 0x1F5A12u, 67, 63 },
        { "XTRANSCEIVER", "Call a registered friend.", 0xF08A24u, 0x6A3A0Cu, 187, 63 },
    };
    void *w = app_work(WL_OVERLAY);
    void *args, *gs, *comm;
    Modal m;
    int i, x, y, tw, th, hit, mode = 0;
    char buf[64];
    static const char *const hints[] = { "A", "Choose", "B", "Back" };

    g_frame = in->frame;
    pend_pump();
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;      /* an app: the field is not running */
    view->lower_only = 1;
    if (w == NULL) {
        return;
    }
    args = rdp(w, 0x344);
    (void)args;
    gs = rdp(w, 0x354);
    comm = (gs != NULL && GAMESYSTEM_GetGameComm != NULL) ? GAMESYSTEM_GetGameComm(gs) : NULL;
    if (comm != NULL && INTRUDE_COMM_GetRequestState != NULL) {
        mode = INTRUDE_COMM_GetRequestState(comm);
    }

    m = modal_rect(c, 62, 280, 380, 0, 157, 157, 157);
    ui_panel(c, m.x, m.y, m.w, m.h, CUT_DEF, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_header(c, m.x + 4, m.y + 4, m.w - 8, "WIRELESS", NULL, NULL, UI_CYAN);

    /* the two tiles */
    x = m.x + 10;
    y = m.y + 30;
    tw = (m.w - 20 - 8) / 2;
    th = 54;
    for (i = 0; i < 2; i++) {
        EidosUiRect rr = { x + i * (tw + 8), y, tw, th };
        /* the Xtransceiver needs a camera and a microphone the port does
         * not have: its tile is greyed out and does nothing */
        int off = i == 1;
        int st = off ? 0 : eidos_ui_hit(rr), on = !off && ((g_wl_sel == i) || (st & EIDOS_UI_HOVER));
        const char *sub = off ? "Not available." : tiles[i].what;

        if (i == 0 && mode == 2 && INTRUDE_COMM_CountEventPeers != NULL) {
            snprintf(buf, sizeof buf, ui_trf("%d in the Entralink."), INTRUDE_COMM_CountEventPeers(comm));
            sub = buf;
        } else if (i == 0 && mode == 3) {
            sub = "Someone invites you.";
        }
        ui_panel(c, rr.x, rr.y, rr.w, rr.h, CUT_DEF, 8, on ? tiles[i].accent2 : UI_BODY2,
                 on ? tiles[i].accent : UI_EDGE, 250);
        ui_fill(c, rr.x + 6, rr.y + 8, 4, rr.h - 16, off ? UI_EDGE2 : tiles[i].accent, 255);
        box_text(c, rr.x + 12, rr.y + 8, rr.w - 16, 17, ui_tr(tiles[i].name),
                 off ? UI_EDGE2 : UI_WHITE, UI_EDGE, UI_FONT_LARGE, 0, 2);
        box_text(c, rr.x + 12, rr.y + 28, rr.w - 16, 15, ui_tr(sub),
                 off ? UI_EDGE2 : ((i == 0 && mode >= 2) ? tiles[i].accent : UI_LIGHT), -1,
                 UI_FONT_SMALL, 0, 2);
        if (st & EIDOS_UI_HOVER) {
            g_wl_sel = i;
        }
        if (st & EIDOS_UI_CLICK) {
            tap(tiles[i].tx, tiles[i].ty);
        }
    }
    y += th + 8;

    /* the app's facts, in the game's words */
    {
        char n[16];

        snprintf(n, sizeof n, "%u", rd32(w, 0x36C));
        info_line(c, m.x + 10, y, m.w - 20, wl_text(2, "Union Room"), n);
        y += 17;
        info_line(c, m.x + 10, y, m.w - 20, wl_text(4, "Wireless Distribution"),
                  (rd32(w, 0x364) & 32u) ? wl_text(5, "YES") : wl_text(6, "NO"));
        y += 17;
        info_line(c, m.x + 10, y, m.w - 20, wl_text(12, "C-Gear Detected"),
                  rd32(w, 0x374) ? wl_text(5, "YES") : wl_text(6, "NO"));
    }

    /* keys: A takes the Entralink, B goes back */
    {
        u32 edge = eidos_ui_keys_pressed();

        g_wl_sel = 0;           /* the Entralink is the one tile that works */
        if (edge & BW_KEY_A) {
            tap(tiles[g_wl_sel].tx, tiles[g_wl_sel].ty);
        }
        hit = hint_bar(c, in, hints, 2);
        if (hit == 0) {
            tap(tiles[g_wl_sel].tx, tiles[g_wl_sel].ty);
        }
        if (hit == 1 || (edge & BW_KEY_B) || eidos_ui_right_click() || outside_click(c, m)) {
            tap(224, 177);
        }
    }
}

static void wireless_reset(void)
{
    g_wl_sel = 0;
}

static const EidosUiRecipe recipe_wireless = {
    "wireless", wireless_match, wireless_run, wireless_reset, 0
};

/* ------------------------------------------------------------------ */
/* Geonet                                                              */
/* ------------------------------------------------------------------ */

/*
 * Geonet (overlay 219, ov219/beacon_detail.c, the globe in the Pokemon
 * Centers' upper floor) as one screen (mock-up "geonet"): the globe fills
 * the window, the game's windows are the interface's system boxes (the
 * upper screen's on the top left, the lower screen's along the bottom),
 * and View and Quit are tiles with their keys (X, B).  The pointer held
 * on the globe is the stylus there, so the globe turns as the game turns
 * it for a drag.  The game's own windows and its grid are left out of the
 * screen (g2d_hw_hide_main_bgs: BG2 and BG3 of the engine with the 3D).
 *
 * BeaconDetailWork: bmp18 0xC018, bmp1C 0xC01C, bmp40 0xC040 (BmpWins).
 */
#define GEO_OVERLAY 219

extern void g2d_hw_hide_main_bgs(u32 mask) MN_WEAK;

static int geonet_match(void)
{
    if (app_work(GEO_OVERLAY) == NULL) {
        return 0;
    }
    eidos_ui_want_ds_scale();
    return 1;
}

static int geo_win_live(const u8 *w)
{
    return w != NULL && (rd16(w, 0) & 0x7FFFu) == 0x7FB2u;
}

/* A battle-sheet tile with a key: the strip, the label, the keycap. */
static int geo_tile(EidosUiCtx *c, int x, int y, int w, const char *label, const char *key, int lit)
{
    EidosUiRect r;
    int st, rr, h = 21;
    u32 col = lit ? 0x008BACu : UI_EDGE2;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    if (st & EIDOS_UI_HOVER) {
        lit = 1;
        col = 0x008BACu;
    }
    ui_panel(c, x, y - (lit ? 1 : 0), w, h, UI_CUT_TL | UI_CUT_BR, 5, lit ? 0x05343Eu : UI_BODY,
             lit ? 0x008BACu : UI_EDGE, 242);
    for (rr = 3; rr < h - 3; rr++) {
        int left = 3 + (rr < 7 ? 7 - rr : 0);

        if (left < 6) {
            ui_fill(c, x + left, y - (lit ? 1 : 0) + rr, 6 - left, 1, col, 255);
        }
    }
    ui_text(c, x + 12, y - (lit ? 1 : 0) + ui_text_y(h, UI_FONT_LARGE), label, UI_WHITE, 0x313131u,
            UI_FONT_LARGE, 1);
    ui_keycap(c, x + w - 6 - ui_keycap_w(key), y - (lit ? 1 : 0) + 4, key);
    return st;
}

static void geonet_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    const u8 *w = (const u8 *)app_work(GEO_OVERLAY);
    const u8 *wins[3];
    int cw = c->canvas->w, ch = c->canvas->h, i, bw, bottom_top = ch - 4;
    float s = (float)ch / 192.0f;
    float ox = ((float)cw - 256.0f * s) * 0.5f;
    EidosUiRect all;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    if (w == NULL) {
        return;
    }
    if (g2d_hw_hide_main_bgs != NULL) {
        g2d_hw_hide_main_bgs(0x4u | 0x8u);  /* the windows and the grid */
    }
    wins[0] = rdp(w, 0xC018);
    wins[1] = rdp(w, 0xC01C);
    wins[2] = rdp(w, 0xC040);
    bw = (int)(256.0f * s) - 8;
    if (bw > cw - 8) {
        bw = cw - 8;
    }
    /* the lower screen's windows first, along the bottom; the upper
     * screen's on the top left */
    for (i = 0; i < 3; i++) {
        const u8 *b = wins[i];

        if (!geo_win_live(b) || b[2] >= 4) {
            continue;
        }
        bottom_top -= b[6] * 8 + 10;
        ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, (cw - bw) / 2, bottom_top, bw, b, -1, -1);
        bottom_top -= 4;
    }
    {
        int y = 4;

        for (i = 0; i < 3; i++) {
            const u8 *b = wins[i];

            if (!geo_win_live(b) || b[2] < 4) {
                continue;
            }
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, 4, y, b[5] * 8 + 20, b, -1, -1);
            y += b[6] * 8 + 14;
        }
    }
    {
        /* View (X) and Quit (B) */
        int tw = ui_text_w("Quit", UI_FONT_LARGE) + 44, vw = ui_text_w("View", UI_FONT_LARGE) + 44;
        int x = (cw + bw) / 2 - tw, y = bottom_top - 24;

        if (geo_tile(c, x, y, tw, "Quit", "B", 0) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_B, 2);
        }
        x -= vw + 4;
        if (geo_tile(c, x, y, vw, "View", "X", 1) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_X, 2);
        }
    }
    /* the pointer held on the globe is the stylus there */
    all.x = 0; all.y = 0; all.w = cw; all.h = bottom_top - 26;
    if (eidos_ui_held() && (eidos_ui_hit(all) & EIDOS_UI_HOVER)) {
        int mx, my, tx, ty;

        if (eidos_ui_mouse_pos(&mx, &my)) {
            tx = (int)(((float)mx - ox) / s);
            ty = (int)((float)my / s);
            if (tx >= 0 && tx < 256 && ty >= 0 && ty < 192) {
                eidos_ui_tap(tx, ty, 2);
            }
        }
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void geonet_reset(void)
{
}

static const EidosUiRecipe recipe_geonet = {
    "geonet", geonet_match, geonet_run, geonet_reset, 0
};

/* ------------------------------------------------------------------ */
/* The Entree (the Entralink's Monolith)                               */
/* ------------------------------------------------------------------ */

/*
 * The Entree's first screen (overlay 71, ov071/monolith_title.c) as a
 * window over the blurred Entralink (mock-up "entree"): the title, Receive
 * Pass Power, Check Status and Back as battle-style tiles.  A tile taps the
 * game's own button, so the game opens what it opens for the stylus; its
 * keys work as they did.  The Pass Power list and the status follow
 * (pass_power_run, entree_status_run).
 *
 * MonolithToolWork (ov071/monolith_tool_types.h): currentScreen 0x134.
 * Its words: a/0/0/2 file 69 (0 the title, 2 and 3 the two buttons).
 */
#define MONO_OVERLAY 71
#define MONO_TITLE   0          /* currentScreen: the first screen        */
#define MONO_POWER   2          /* the Pass Power list                    */
#define MONO_STATUS  3          /* the Entree's status                    */

static int entree_match(void)
{
    const u8 *w = (const u8 *)app_work(MONO_OVERLAY);

    if (w == NULL) {
        return 0;
    }
    if (getenv("EIDOS_UI_ENTREE_LOG") != NULL) {
        static int last = -1;

        if (w[0x134] != last) {
            fprintf(stderr, "eidos: ui entree screen %d\n", w[0x134]);
            last = w[0x134];
        }
    }
    return w[0x134] == MONO_TITLE || w[0x134] == MONO_POWER || w[0x134] == MONO_STATUS;
}

/* A battle sheet tile; returns its hit state. */
static int sheet_tile_hit(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int lit)
{
    EidosUiRect r;
    int st, rr;
    u32 col;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    lit = lit || (st & EIDOS_UI_HOVER);
    col = lit ? 0x008BACu : UI_EDGE2;
    ui_panel(c, x, y - (lit ? 1 : 0), w, h, UI_CUT_TL | UI_CUT_BR, 5, lit ? 0x05343Eu : UI_BODY,
             lit ? 0x008BACu : UI_EDGE, 242);
    for (rr = 3; rr < h - 3; rr++) {
        int left = 3 + (rr < 7 ? 7 - rr : 0);

        if (left < 6) {
            ui_fill(c, x + left, y - (lit ? 1 : 0) + rr, 6 - left, 1, col, 255);
        }
    }
    ui_text(c, x + 12, y - (lit ? 1 : 0) + ui_text_y(h, UI_FONT_LARGE), label, UI_WHITE, 0x313131u,
            UI_FONT_LARGE, 1);
    return st;
}

static void entree_title_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    int cw = c->canvas->w, ch = c->canvas->h, mw, mh = 150, mx, my;
    char name[24] = "";
    const char *title;
    void *gd = game_data();

    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    if (gd != NULL && GAME_DATA_GetMyStatus != NULL && MYSTATUS_GetName != NULL) {
        units_utf8(MYSTATUS_GetName(GAME_DATA_GetMyStatus(gd)), 8, name, (int)sizeof name);
    }
    title = game_text(2, 69, 0, name);
    mw = cw - 40 < 300 ? cw - 40 : 300;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_text(c, mx + 12, my + 6, title[0] != '\0' ? title : "ENTREE", 0x62BEFFu, 0x313131u, UI_FONT_SMALL, 1);
    ui_fill(c, mx + 10, my + 19, mw - 20, 1, 0x103C5Au, 255);
    {
        /* the keys move the window's own cursor (the game's is on its
         * lower screen); A presses the tile under it */
        static int focus;
        u32 k = eidos_ui_keys_pressed();
        int i, pick = -1;
        const char *labels[3];

        labels[0] = game_text(2, 69, 2, NULL);
        labels[1] = game_text(2, 69, 3, NULL);
        labels[2] = "Back";
        eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_A);
        if (k & BW_KEY_UP) focus = (focus + 2) % 3;
        if (k & BW_KEY_DOWN) focus = (focus + 1) % 3;
        if (k & BW_KEY_A) pick = focus;
        for (i = 0; i < 3; i++) {
            int st = sheet_tile_hit(c, mx + 12, my + 30 + 36 * i, mw - 24, 30, labels[i],
                                    eidos_ui_keys_lit() && i == focus);

            if (st & EIDOS_UI_CLICK) pick = i;
            if (st & EIDOS_UI_HOVER) focus = i;
        }
        if (pick == 0) eidos_ui_tap(128, 58, 3);
        if (pick == 1) eidos_ui_tap(128, 115, 3);
        if (pick == 2) eidos_ui_press(BW_KEY_B, 2);
    }
    if (ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys) < 0) {
        /* the hints press their own keys */
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

/* ------------------------------------------------------------------ */
/* The inner screens' parts (the Entree, the Survey Radar)             */
/* ------------------------------------------------------------------ */

/*
 * The inner screens of the Entree and the Survey Radar (mock-ups
 * "pass_power", "entree_status", "survey_list", "survey_report"): a dark
 * window over the blurred world, the game's own words copied out of its
 * windows (ui_letters: its font, its language, what it printed), battle
 * tiles for what can be chosen.  The keys stay the game's; a click taps
 * the game's own button or moves its cursor with its keys, so the game
 * does what it does for the stylus or the pad.
 */
#define IN_TITLE   0x62BEFFu    /* the window's title                     */
#define IN_MBODY   0x060E1Au
#define IN_MEDGE   0x103C5Au
#define IN_HL      0x008BACu
#define IN_SHADOW  0x313131u
#define IN_HEAD    28           /* the title and its rule                  */

/* The work of the proc on top of a ProcSys (current 0x08; Proc work
 * 0x14), NULL before it has one. */
static void *proc_work_of(void *procsys)
{
    void *cur = procsys != NULL ? rdp(procsys, 0x08) : NULL;

    return cur != NULL ? rdp(cur, 0x14) : NULL;
}

/* The Bmp of a BmpWin, NULL when the window is not live. */
static const void *win_bmp_of(const void *win)
{
    return win != NULL ? ui_bmpwin_bmp(win) : NULL;
}

/* The window over the world, its title the game's words when they are
 * printed. */
static void in_window(EidosUiCtx *c, int x, int y, int w, int h, const void *title_bmp)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 10, IN_MBODY, IN_MEDGE, 245);
    if (title_bmp != NULL) {
        ui_letters(c, title_bmp, x + 12, y + 5, IN_TITLE, IN_SHADOW, NULL, NULL);
    }
    ui_fill(c, x + 10, y + IN_HEAD - 4, w - 20, 1, IN_MEDGE, 255);
}

/* A panel for words that are only read. */
static void in_panel(EidosUiCtx *c, int x, int y, int w, int h)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 255);
}

/* A battle tile (recipe_battle.c sheet_tile): the strip down the left,
 * the chosen one tinted, edged and lifted a pixel.  Returns the hit state;
 * *dy gets the lift for what is drawn on it. */
static int in_tile(EidosUiCtx *c, int x, int y, int w, int h, int lit, int *dy)
{
    EidosUiRect r;
    int st, rr, lift;
    u32 col;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    lit = lit || (st & EIDOS_UI_HOVER);
    lift = lit ? -1 : 0;
    col = lit ? IN_HL : UI_EDGE2;
    ui_panel(c, x, y + lift, w, h, UI_CUT_TL | UI_CUT_BR, 5, lit ? 0x05343Eu : UI_BODY,
             lit ? IN_HL : UI_EDGE, 242);
    for (rr = 3; rr < h - 3; rr++) {
        int left = 3 + (rr < 7 ? 7 - rr : 0);

        if (left < 6) {
            ui_fill(c, x + left, y + lift + rr, 6 - left, 1, col, 255);
        }
    }
    if (dy != NULL) {
        *dy = lift;
    }
    return st;
}

/* A tile labelled with the game's printed words (a Bmp), or with `text`
 * when it has none. */
static int in_tile_words(EidosUiCtx *c, int x, int y, int w, int h, const void *bmp, const char *text,
                         int lit, int dim)
{
    int dy, bw, bh, st = in_tile(c, x, y, w, h, lit, &dy);
    u32 ink = dim ? UI_GREY : UI_WHITE;

    if (bmp != NULL && ui_letters(NULL, bmp, 0, 0, 0, 0, &bw, &bh)) {
        ui_letters(c, bmp, x + 12, y + dy + (h - bh) / 2, ink, IN_SHADOW, NULL, NULL);
    } else if (text != NULL) {
        ui_text(c, x + 12, y + dy + ui_text_y(h, UI_FONT_LARGE), text, ink, IN_SHADOW, UI_FONT_LARGE, 1);
    }
    return st;
}

/* A system box holding a text of the game's banks (lines split at '\n'),
 * h high. */
static void in_sys_text(EidosUiCtx *c, int x, int y, int w, int h, const char *s)
{
    char line[160];
    int n = 0, ly = y + 6;

    ui_textbox_frame(c, UI_TEXTBOX_SYSTEM, x, y, w, h);
    for (;; s++) {
        if (*s == '\n' || *s == '\0') {
            line[n] = '\0';
            ui_text(c, x + 12, ly, line, UI_WHITE, 0x464850u, UI_FONT_LARGE, 1);
            ly += 16;
            n = 0;
            if (*s == '\0') {
                break;
            }
        } else if (n < (int)sizeof line - 1) {
            line[n++] = *s;
        }
    }
}

/* The centre of rectangle i of a game touch table ({top, bottom, left,
 * right} bytes); 0 when it is not a rectangle. */
static int touch_rect_mid(const u8 *table, int i, int *x, int *y)
{
    const u8 *r = table + 4 * i;

    if (r[0] == 0xFF || r[0] == 0xFE || r[1] <= r[0] || r[3] <= r[2]) {
        return 0;
    }
    *x = (r[2] + r[3]) / 2;
    *y = (r[0] + r[1]) / 2;
    return 1;
}

static void tap_rect(const u8 *table, int i)
{
    int x, y;

    if (touch_rect_mid(table, i, &x, &y)) {
        eidos_ui_tap(x, y, 3);
    }
}

/* A left or right arrow in a tile, a hit region of its own. */
static int in_arrow(EidosUiCtx *c, int x, int y, int h, int right, int lit)
{
    EidosUiRect r;
    int st, i, cy = y + h / 2;
    u32 col;

    r.x = x; r.y = y; r.w = 16; r.h = h;
    st = eidos_ui_hit(r);
    col = (lit || (st & EIDOS_UI_HOVER)) ? UI_WHITE : IN_HL;
    for (i = 0; i < 5; i++) {
        int xx = right ? x + 6 + i : x + 10 - i;

        ui_fill(c, xx, cy - (4 - i), 1, 2 * (4 - i) + 1, col, 255);
    }
    return st;
}

static void in_view(EidosUiView *view)
{
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
}

/*
 * The Pass Power list (ov071/monolith_power.c, the list on the lower
 * screen, the information on the upper): the powers as tiles on the left,
 * the current power and the chosen one's text on the right, the game's
 * message as a system box under the window and its YES / NO beside it.
 *
 * MonolithListWork (sub proc): entryBitmaps 0x024, entryDisabled 0x1B4,
 * entryCount 0x27C (rows 1 .. count - 2 are powers), messageWindow 0x280,
 * titleWindow 0x284, messagePrinter 0x294 (non-zero while a message is
 * up), cursor 0x2F4 (0xFFFF none), yesNoMenu 0x310 (a TaskMenuWork).
 * MonolithInfoWork (top proc): windows[3] 0x00 (the header, the current
 * power, the chosen power's text).  YES / NO: a/0/0/2 file 69, 30 and 31.
 */
#define MONO_ROWS     5         /* tiles shown at once                    */
#define MONO_NONE     0xFFFFu

static int g_pp_target = -1;    /* the row the keys are moving to          */
static int g_pp_pick;           /* then A                                   */
static int g_pp_wait;
static int g_pp_first = 1;      /* the first row shown                      */
static int g_pp_seen = -1;      /* the game's cursor last frame             */

/* Moves the game's cursor to g_pp_target with its keys (one press every
 * few frames), then presses A when a pick was asked. */
static void pp_drive(const u8 *l)
{
    int cur = (int)rd32(l, 0x2F4);

    if (g_pp_target < 0) {
        return;
    }
    if (rdp(l, 0x294) != NULL || rdp(l, 0x310) != NULL) {
        g_pp_target = -1;
        return;
    }
    if (g_pp_wait > 0) {
        g_pp_wait--;
        return;
    }
    g_pp_wait = 4;
    if (cur == (int)MONO_NONE || cur < g_pp_target) {
        eidos_ui_press(BW_KEY_DOWN, 2);
    } else if (cur > g_pp_target) {
        eidos_ui_press(BW_KEY_UP, 2);
    } else {
        if (g_pp_pick) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        g_pp_target = -1;
        g_pp_pick = 0;
    }
}

static void pass_power_run(EidosUiCtx *c, const u8 *w)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    const u8 *l = (const u8 *)proc_work_of(rdp(w, 0x130));
    const u8 *top = (const u8 *)proc_work_of(rdp(w, 0x12C));
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 24 < 460 ? cw - 24 : 460;
    int tw = 168, body = MONO_ROWS * 32 - 4, mh = IN_HEAD + body + 10;
    int mx = (cw - mw) / 2, my = (ch - 17 - mh - 48) / 2;
    int count, cur, i, msg, menu, rx, rw, y;
    EidosUiRect wheel;

    if (my < 4) my = 4;
    if (l == NULL || (count = l[0x27C]) < 3 || count > 50) {
        in_window(c, mx, my, mw, mh, NULL);
        return;
    }
    cur = (int)rd32(l, 0x2F4);
    msg = rdp(l, 0x294) != NULL;
    menu = rdp(l, 0x310) != NULL;
    in_window(c, mx, my, mw, mh, win_bmp_of(rdp(l, 0x284)));

    /* the list: keep the game's cursor in view when it moves; the wheel
     * scrolls the tiles */
    if (cur != g_pp_seen && cur != (int)MONO_NONE) {
        if (cur < g_pp_first) g_pp_first = cur;
        if (cur >= g_pp_first + MONO_ROWS) g_pp_first = cur - MONO_ROWS + 1;
    }
    g_pp_seen = cur;
    wheel.x = mx; wheel.y = my; wheel.w = tw + 12; wheel.h = mh;
    g_pp_first -= eidos_ui_wheel(wheel);
    if (g_pp_first > count - 1 - MONO_ROWS) g_pp_first = count - 1 - MONO_ROWS;
    if (g_pp_first < 1) g_pp_first = 1;
    for (i = 0; i < MONO_ROWS; i++) {
        int row = g_pp_first + i, st;

        if (row > count - 2) {
            break;
        }
        st = in_tile_words(c, mx + 12, my + IN_HEAD + 32 * i, tw, 28, rdp(l, 0x24 + 4 * (u32)row), NULL,
                           row == cur, rd32(l, 0x1B4 + 4 * (u32)row) != 0);
        if (!msg && !menu && (st & EIDOS_UI_CLICK)) {
            g_pp_target = row;
            g_pp_pick = 1;
            g_pp_wait = 0;
        }
    }
    if (count - 2 > MONO_ROWS) {
        /* where the shown rows sit in the list */
        int bh = body * MONO_ROWS / (count - 2), by = body * (g_pp_first - 1) / (count - 2);

        ui_fill(c, mx + 12 + tw + 3, my + IN_HEAD, 2, body, UI_EDGE, 255);
        ui_fill(c, mx + 12 + tw + 3, my + IN_HEAD + by, 2, bh, IN_HL, 255);
    }

    /* the current power and the chosen one's words, from the upper screen */
    rx = mx + 12 + tw + 10;
    rw = mx + mw - 12 - rx;
    y = my + IN_HEAD;
    if (top != NULL) {
        const void *h0 = win_bmp_of(rdp(top, 0x00)), *h1 = win_bmp_of(rdp(top, 0x04));
        const void *h2 = win_bmp_of(rdp(top, 0x08));
        int a0 = 0, a1 = 0, a2 = 0, bw;

        ui_letters(NULL, h0, 0, 0, 0, 0, &bw, &a0);
        ui_letters(NULL, h1, 0, 0, 0, 0, &bw, &a1);
        ui_letters(NULL, h2, 0, 0, 0, 0, &bw, &a2);
        in_panel(c, rx, y, rw, 10 + a0 + 6 + a1 + 8);
        ui_letters(c, h0, rx + 10, y + 6, UI_GREY, IN_SHADOW, NULL, NULL);
        ui_letters(c, h1, rx + 10, y + 6 + a0 + 6, UI_WHITE, IN_SHADOW, NULL, NULL);
        y += 10 + a0 + 6 + a1 + 8 + 4;
        if (a2 > 0) {
            in_panel(c, rx, y, rw, a2 + 14);
            ui_letters(c, h2, rx + 10, y + 7, UI_WHITE, IN_SHADOW, NULL, NULL);
        }
    }
    if (in_tile_words(c, rx, my + IN_HEAD + body - 26, rw, 26, NULL, "Back", 0, 0)
        & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_B, 2);
    }

    /* the game's message under the window; YES / NO beside it */
    if (msg) {
        const void *mwin = rdp(l, 0x280);
        int bh = ui_textbox_h(mwin), bw = menu ? mw - 96 : mw, by = my + mh + 4;

        if (bh > 0) {
            EidosUiRect r;

            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, by, bw, mwin, -1, -1);
            r.x = mx; r.y = by; r.w = bw; r.h = bh;
            if (!menu && (eidos_ui_hit(r) & EIDOS_UI_CLICK)) {
                eidos_ui_press(BW_KEY_A, 2);
            }
        }
        if (menu) {
            const u8 *tm = (const u8 *)rdp(l, 0x310);
            int tcur = tm[0x00], j;

            for (j = 0; j < 2; j++) {
                const char *lab = game_text(2, 69, 30u + (u32)j, NULL);
                int st = in_tile_words(c, mx + mw - 92, by + 22 * j, 92, 20, NULL,
                                       lab[0] != '\0' ? lab : (j == 0 ? "YES" : "NO"),
                                       eidos_ui_keys_lit() && tcur == j, 0);

                if (st & EIDOS_UI_CLICK) {
                    /* TaskMenuWork init 0x20: charPosX 0x30, charPosY 0x31,
                     * width 0x32, height 0x33 (posType 0: the top left) */
                    eidos_ui_tap(tm[0x30] * 8 + tm[0x32] * 4, tm[0x31] * 8 + tm[0x33] * 8 * j + tm[0x33] * 4, 3);
                }
            }
        }
    }
    pp_drive(l);
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

/*
 * The Entree's status (ov071/monolith_title.c, MonolithToolStats on the
 * lower screen): its six windows (the title, the total level, the two
 * versions' levels, the time, the missions, the Pass Powers) in two
 * panels, and Back.
 */
static void entree_status_run(EidosUiCtx *c, const u8 *w)
{
    static const char *const hints[] = { "B", "Back" };
    static const u32 keys[] = { BW_KEY_B };
    const u8 *s = (const u8 *)proc_work_of(rdp(w, 0x130));
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 24 < 360 ? cw - 24 : 360;
    int hh[6] = { 0 }, i, bw, pa, pb, mh, mx, my, y;
    const void *b[6];

    for (i = 0; i < 6; i++) {
        b[i] = s != NULL ? win_bmp_of(rdp(s, 4 * (u32)i)) : NULL;
        if (b[i] != NULL) {
            ui_letters(NULL, b[i], 0, 0, 0, 0, &bw, &hh[i]);
        }
    }
    pa = 8 + hh[1] + 6 + hh[2] + 8;
    pb = 8 + hh[3] + 5 + hh[4] + 5 + hh[5] + 8;
    mh = IN_HEAD + pa + 6 + pb + 6 + 26 + 10;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    in_window(c, mx, my, mw, mh, b[0]);
    y = my + IN_HEAD;
    in_panel(c, mx + 12, y, mw - 24, pa);
    ui_letters(c, b[1], mx + 22, y + 8, UI_WHITE, IN_SHADOW, NULL, NULL);
    ui_letters(c, b[2], mx + 22, y + 8 + hh[1] + 6, UI_WHITE, IN_SHADOW, NULL, NULL);
    y += pa + 6;
    in_panel(c, mx + 12, y, mw - 24, pb);
    ui_letters(c, b[3], mx + 22, y + 8, UI_WHITE, IN_SHADOW, NULL, NULL);
    ui_letters(c, b[4], mx + 22, y + 8 + hh[3] + 5, UI_WHITE, IN_SHADOW, NULL, NULL);
    ui_letters(c, b[5], mx + 22, y + 8 + hh[3] + 5 + hh[4] + 5, UI_WHITE, IN_SHADOW, NULL, NULL);
    y += pb + 6;
    if (in_tile_words(c, mx + 12, y, mw - 24, 26, NULL, "Back", eidos_ui_keys_lit(), 0)
        & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void entree_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    const u8 *w = (const u8 *)app_work(MONO_OVERLAY);

    if (w == NULL) {
        return;
    }
    if (w[0x134] == MONO_TITLE) {
        entree_title_run(c, view, in);
        return;
    }
    in_view(view);
    if (w[0x134] == MONO_POWER) {
        pass_power_run(c, w);
    } else {
        g_pp_target = -1;
        entree_status_run(c, w);
    }
}

static void entree_reset(void)
{
    g_pp_target = -1;
    g_pp_seen = -1;
}

static const EidosUiRecipe recipe_entree = {
    "entree", entree_match, entree_run, entree_reset, 0
};

/* ------------------------------------------------------------------ */
/* The Survey Radar                                                    */
/* ------------------------------------------------------------------ */

/*
 * The Survey Radar's first screen (overlay 217, ov217/research_top.c) as
 * a window over the blurred field (mock-up "survey"): the title, Select a
 * Survey and Check Survey Results as battle-style tiles that tap the
 * game's own buttons; the keys move the window's cursor and A presses it;
 * B or a right click leaves.  The survey list and the report follow
 * (survey_list_run, survey_report_run).
 *
 * The first screen is the proc's step RS_TOP (its menu, waiting).
 */
#define RS_OVERLAY 217
#define RS_LIST    202          /* the proc's step: the survey list       */
#define RS_REPORT  203          /* the survey report                      */

static int g_survey_top = 201;      /* the step of the first screen: main, its menu */

static int survey_step(void)
{
    void *gs = game_system(), *cur;

    if (gs == NULL || (cur = current_proc(gs)) == NULL || (int)rd32(cur, 0x04) != RS_OVERLAY) {
        return -1;
    }
    return (int)rd32(cur, 0x08) * 100 + (int)rd32(cur, 0x0C);
}

static int survey_match(void)
{
    int st;

    if (app_work(RS_OVERLAY) == NULL) {
        return 0;
    }
    st = survey_step();
    if (getenv("EIDOS_UI_SURVEY_LOG") != NULL) {
        static int last = -99;

        if (st != last) {
            fprintf(stderr, "eidos: ui survey step %d\n", st);
            last = st;
        }
    }
    return st == g_survey_top || st == RS_LIST || st == RS_REPORT;
}

static void survey_top_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static int focus;
    int cw = c->canvas->w, ch = c->canvas->h, mw, mh = 114, mx, my, i, pick = -1;
    u32 k = eidos_ui_keys_pressed();
    const char *labels[2];

    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    labels[0] = "Select a Survey";
    labels[1] = "Check Survey Results";
    mw = cw - 40 < 300 ? cw - 40 : 300;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_text(c, mx + 12, my + 6, "SURVEY RADAR", 0x62BEFFu, 0x313131u, UI_FONT_SMALL, 1);
    ui_fill(c, mx + 10, my + 19, mw - 20, 1, 0x103C5Au, 255);
    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_A);
    if (k & BW_KEY_UP) focus = (focus + 1) % 2;
    if (k & BW_KEY_DOWN) focus = (focus + 1) % 2;
    if (k & BW_KEY_A) pick = focus;
    for (i = 0; i < 2; i++) {
        int st = sheet_tile_hit(c, mx + 12, my + 30 + 38 * i, mw - 24, 32, labels[i],
                                eidos_ui_keys_lit() && i == focus);

        if (st & EIDOS_UI_CLICK) pick = i;
        if (st & EIDOS_UI_HOVER) focus = i;
    }
    if (pick == 0) eidos_ui_tap(128, 63, 3);
    if (pick == 1) eidos_ui_tap(128, 119, 3);
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

/*
 * The survey list (ov217/research_list.c, the proc's step 202): the
 * surveys as tiles, the game's words under the window as a system box;
 * once one is chosen, its title, its text and its three questions, the
 * game's question and CONFIRM / CANCEL.
 *
 * ResearchList (graphObject, ResearchProc 0x14): state 0x1C, selection
 * 0x2C (0 confirm, 1 cancel), entryCount 0x30, cursorSlot 0x32,
 * touchRects 0x58 (rectangle 0 CONFIRM, 1 CANCEL, 3 + i survey i),
 * entryLabels[10] 0xB4, labels[7] 0xDC (0 title, 1 hint, 2 survey
 * title, 3 its text, 4 .. 6 its questions).  BgTextLabel: window 0x18,
 * visible 0x1C.  States: 1 and 2 choosing (touch, keys), 7 .. 10 the
 * chosen survey (8 touch, 10 keys), 12 starting.  Words: a/0/0/2 file
 * 169 (6 the question, 7 CONFIRM, 8 CANCEL, 9 the survey is starting).
 */
#define SL_ROWS    5

static const void *label_bmp(const u8 *lab, int need_visible)
{
    if (lab == NULL || (need_visible && rd32(lab, 0x1C) == 0)) {
        return NULL;
    }
    return win_bmp_of(rdp(lab, 0x18));
}

static const void *label_win(const u8 *lab)
{
    return lab != NULL ? rdp(lab, 0x18) : NULL;
}

static void survey_list_run(EidosUiCtx *c, const u8 *l)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 24 < 460 ? cw - 24 : 460;
    int state = (int)rd32(l, 0x1C), count = l[0x30], chosen = state >= 7 && state <= 10;
    int mh, mx, my, i, y, rows;
    const u8 *labs = l + 0xDC;

    if (count > 10) count = 10;
    rows = count < SL_ROWS ? count : SL_ROWS;
    if (chosen) {
        /* the chosen survey: its title, its text, its questions */
        const void *t = label_bmp(rdp(labs, 4 * 2), 0), *d = label_bmp(rdp(labs, 4 * 3), 0);
        int th = 0, dh = 0, qh[3] = { 0 }, bw, bh = 44;

        ui_letters(NULL, t, 0, 0, 0, 0, &bw, &th);
        ui_letters(NULL, d, 0, 0, 0, 0, &bw, &dh);
        for (i = 0; i < 3; i++) {
            ui_letters(NULL, label_bmp(rdp(labs, 4 * (u32)(4 + i)), 0), 0, 0, 0, 0, &bw, &qh[i]);
        }
        mh = IN_HEAD + 8 + th + 8 + dh + 10 + (qh[0] + qh[1] + qh[2] + 12) + 14;
        mx = (cw - mw) / 2;
        my = (ch - 17 - mh - 4 - bh) / 2;
        if (my < 4) my = 4;
        in_window(c, mx, my, mw, mh, label_bmp(rdp(labs, 0), 0));
        y = my + IN_HEAD + 6;
        ui_letters(c, t, mx + 14, y, IN_TITLE, IN_SHADOW, NULL, NULL);
        y += th + 8;
        ui_letters(c, d, mx + 14, y, UI_WHITE, IN_SHADOW, NULL, NULL);
        y += dh + 10;
        in_panel(c, mx + 12, y, mw - 24, qh[0] + qh[1] + qh[2] + 12 + 4);
        y += 6;
        for (i = 0; i < 3; i++) {
            ui_fill(c, mx + 22, y + qh[i] / 2 - 1, 3, 3, IN_HL, 255);
            ui_letters(c, label_bmp(rdp(labs, 4 * (u32)(4 + i)), 0), mx + 30, y, UI_LIGHT, IN_SHADOW, NULL, NULL);
            y += qh[i] + 4;
        }
        y = my + mh + 4;
        in_sys_text(c, mx, y, mw - 112, bh, game_text(2, 169, 6, NULL));
        for (i = 0; i < 2; i++) {
            const char *lab = game_text(2, 169, 7u + (u32)i, NULL);
            int st = in_tile_words(c, mx + mw - 108, y + 23 * i, 108, 21, NULL,
                                   lab[0] != '\0' ? lab : (i == 0 ? "CONFIRM" : "CANCEL"),
                                   state == 10 && (int)rd32(l, 0x2C) == i, 0);

            if ((st & EIDOS_UI_CLICK) && (state == 8 || state == 10)) {
                tap_rect(l + 0x58, i);
            }
        }
    } else {
        int bh = ui_textbox_h(label_win(rdp(labs, 4 * 1)));
        int sel = state == 2 ? l[0x32] : -1;
        static int first;

        mh = IN_HEAD + rows * 32 + 26 + 4 + 10;
        mx = (cw - mw) / 2;
        my = (ch - 17 - mh - 4 - (bh > 0 ? bh : 44)) / 2;
        if (my < 4) my = 4;
        in_window(c, mx, my, mw, mh, label_bmp(rdp(labs, 0), 0));
        if (sel >= 0) {
            if (sel < first) first = sel;
            if (sel >= first + SL_ROWS) first = sel - SL_ROWS + 1;
        }
        if (first > count - rows) first = count - rows;
        if (first < 0) first = 0;
        for (i = 0; i < rows; i++) {
            int e = first + i;
            int st = in_tile_words(c, mx + 12, my + IN_HEAD + 32 * i, mw - 24, 28,
                                   label_bmp(rdp(l, 0xB4 + 4 * (u32)e), 0), NULL, e == sel, 0);

            if ((st & EIDOS_UI_CLICK) && (state == 1 || state == 2)) {
                int tx, ty;

                /* the game's own rectangle for the survey, when it is on
                 * its screen */
                if (touch_rect_mid(l + 0x58, 3 + e, &tx, &ty) && ty >= 24 && ty <= 168) {
                    eidos_ui_tap(tx, ty, 3);
                }
            }
        }
        y = my + IN_HEAD + rows * 32 + 4;
        if (in_tile_words(c, mx + 12, y, mw - 24, 26, NULL, "Back", 0, 0) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_B, 2);
        }
        if (state == 12 || state == 13) {
            in_sys_text(c, mx, my + mh + 4, mw, 26, game_text(2, 169, 9, NULL));
        } else if (bh > 0) {
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, my + mh + 4, mw, label_win(rdp(labs, 4 * 1)), -1, -1);
        }
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

/*
 * The survey report (ov217/research_graph.c, the proc's step 203): the
 * survey and its question on the left with the rows the game moves
 * through (the question, the display mode, check the results), the
 * game's circle graph on the right as it draws it, with its words.
 *
 * ResearchGraph (detailObject, ResearchProc 0x18): state 0x20 (2 input
 * by touch, 3 by keys), cursorIndex 0x34 (0 the question, 1 and 2 an
 * answer, 3 the display mode, 4 check the results), touchRects 0x2FE
 * (rectangle 7 check the results), label[9] 0x344 (0 the title, 1 and
 * 2 the survey, 3 the question, 4 an answer, 6 the display mode, 7 not
 * done yet, 8 updating).  Words: a/0/0/2 file 169, 20 check the results.
 */
static int g_rp_target = -1;    /* the row the keys are moving to          */
static u32 g_rp_key;            /* then this key                            */
static int g_rp_wait, g_rp_time;

static void rp_drive(const u8 *g)
{
    int state = (int)rd32(g, 0x20), cur = (int)rd32(g, 0x34);

    if (g_rp_target < 0) {
        return;
    }
    if (++g_rp_time > 120) {
        g_rp_target = -1;             /* a row the game does not offer    */
        return;
    }
    if (g_rp_wait > 0) {
        g_rp_wait--;
        return;
    }
    g_rp_wait = 4;
    if (state == 2) {
        eidos_ui_press(BW_KEY_A, 2);    /* the pad takes over from touch   */
        g_rp_wait = 8;
    } else if (state == 3) {
        if (cur != g_rp_target) {
            eidos_ui_press(BW_KEY_DOWN, 2);
        } else {
            if (g_rp_key != 0) {
                eidos_ui_press(g_rp_key, 2);
            }
            g_rp_target = -1;
        }
    }
}

static void rp_go(int row, u32 key)
{
    g_rp_target = row;
    g_rp_key = key;
    g_rp_wait = 0;
    g_rp_time = 0;
}

/* The report's circle graph as a radar, R pixels round at (cx, cy): a
 * dark disc with rings, a cross and a turning sweep in the interface's
 * blue, and over it what the game draws in its circle ((6, 46) .. (116,
 * 156) of the lower screen: the slices, the outline, the centre), its
 * backdrop (the colour at the square's corner) left out. */
static u32 mix_rgb(u32 a, u32 b, int t)
{
    int r = (int)((a >> 16) & 255) + ((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t / 256;
    int g = (int)((a >> 8) & 255) + ((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t / 256;
    int bl = (int)(a & 255) + ((int)(b & 255) - (int)(a & 255)) * t / 256;

    return (u32)r << 16 | (u32)g << 8 | (u32)bl;
}

static void radar(EidosUiCtx *c, int cx, int cy, int R, const u8 *screen, u32 frame)
{
    const int sr = 55, sx = 61, sy = 101;
    const u8 *k = screen != NULL ? screen + ((size_t)(sy - sr) * 256 + (size_t)(sx - sr)) * 4 : NULL;
    float sweep = (float)(frame % 240) / 240.0f * 6.2831853f;
    int dx, dy;

    for (dy = -R - 2; dy <= R + 2; dy++) {
        for (dx = -R - 2; dx <= R + 2; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            u32 col;
            int t;

            if (d > (float)R + 2.0f) {
                continue;
            }
            if (d > (float)R) {
                ui_fill(c, cx + dx, cy + dy, 1, 1, IN_HL, 255);     /* the rim */
                continue;
            }
            col = 0x061826u;
            if (fabsf(d - (float)R / 3.0f) < 0.6f || fabsf(d - (float)R * 2.0f / 3.0f) < 0.6f
                || dx == 0 || dy == 0) {
                col = 0x0E3A52u;
            }
            {
                /* the sweep and its fading trail */
                float a = atan2f((float)dy, (float)dx) - sweep;

                while (a < 0.0f) a += 6.2831853f;
                while (a >= 6.2831853f) a -= 6.2831853f;
                if (a > 5.5f) {
                    t = (int)((a - 5.5f) / 0.7831853f * 150.0f);
                    col = mix_rgb(col, IN_HL, t);
                }
            }
            if (screen != NULL) {
                int gx = sx + dx * sr / R, gy2 = sy + dy * sr / R;
                const u8 *p = screen + ((size_t)gy2 * 256 + (size_t)gx) * 4;

                if (p[0] != k[0] || p[1] != k[1] || p[2] != k[2]) {
                    col = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
                }
            }
            ui_fill(c, cx + dx, cy + dy, 1, 1, col, 255);
        }
    }
}

static void survey_report_run(EidosUiCtx *c, const u8 *g, const EidosUiFrame *in)
{
    static const char *const hints[] = { "LEFT", "Change", "B", "Back" };
    static const u32 keys[] = { BW_KEY_LEFT, BW_KEY_B };
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 24 < 460 ? cw - 24 : 460;
    int mh = ch - 17 - 8 < 240 ? ch - 17 - 8 : 240;
    int mx = (cw - mw) / 2, my = (ch - 17 - mh) / 2;
    int lw = mw / 2 + 10, state = (int)rd32(g, 0x20), cur = state == 3 ? (int)rd32(g, 0x34) : -1;
    int y, h1 = 0, h2 = 0, bw, dy, st, i;
    const u8 *labs = g + 0x344;
    const void *b1 = label_bmp(rdp(labs, 4), 0), *b2 = label_bmp(rdp(labs, 8), 0);

    in_window(c, mx, my, mw, mh, label_bmp(rdp(labs, 0), 0));
    y = my + IN_HEAD;
    ui_letters(NULL, b1, 0, 0, 0, 0, &bw, &h1);
    ui_letters(NULL, b2, 0, 0, 0, 0, &bw, &h2);
    in_panel(c, mx + 12, y, lw - 12, 8 + h1 + 4 + h2 + 8);
    ui_letters(c, b1, mx + 22, y + 8, UI_WHITE, IN_SHADOW, NULL, NULL);
    ui_letters(c, b2, mx + 22, y + 8 + h1 + 4, UI_LIGHT, IN_SHADOW, NULL, NULL);
    y += 8 + h1 + 4 + h2 + 8 + 4;

    /* the question, its arrows change it */
    st = in_tile_words(c, mx + 12, y, lw - 12, 28, NULL, NULL, cur == 0, 0);
    {
        const void *q = label_bmp(rdp(labs, 12), 0);
        int qw = 0, qh = 0;

        ui_letters(NULL, q, 0, 0, 0, 0, &qw, &qh);
        ui_letters(c, q, mx + 12 + (lw - 12 - qw) / 2, y + (cur == 0 ? -1 : 0) + (28 - qh) / 2, UI_WHITE,
                   IN_SHADOW, NULL, NULL);
    }
    if (in_arrow(c, mx + 16, y, 28, 0, 0) & EIDOS_UI_CLICK) {
        rp_go(0, BW_KEY_LEFT);
    } else if (in_arrow(c, mx + lw - 20, y, 28, 1, 0) & EIDOS_UI_CLICK) {
        rp_go(0, BW_KEY_RIGHT);
    } else if (st & EIDOS_UI_CLICK) {
        rp_go(0, 0);
    }
    y += 32;

    /* the display mode: a click changes it */
    {
        const void *m = label_bmp(rdp(labs, 24), 0);
        int mh2 = 0;

        ui_letters(NULL, m, 0, 0, 0, 0, &bw, &mh2);
        if (mh2 < 20) mh2 = 20;
        st = in_tile(c, mx + 12, y, lw - 12, mh2 + 10, cur == 3, &dy);
        ui_letters(c, m, mx + 24, y + dy + 5, UI_WHITE, IN_SHADOW, NULL, NULL);
        if (st & EIDOS_UI_CLICK) {
            rp_go(3, BW_KEY_RIGHT);
        }
        y += mh2 + 14;
    }
    {
        const char *lab = game_text(2, 169, 20, NULL);

        if (in_tile_words(c, mx + 12, y, lw - 12, 26, NULL, lab[0] != '\0' ? lab : "Check the survey results",
                          cur == 4, 0) & EIDOS_UI_CLICK) {
            tap_rect(g + 0x2FE, 7);
        }
        y += 30;
    }
    if (in_tile_words(c, mx + 12, y, lw - 12, 24, NULL, "Back", 0, 0) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_B, 2);
    }

    /* the game's graph, as it draws it, as a radar, and its words */
    {
        int px = mx + lw + 6, pw = mx + mw - 12 - px, ph = my + mh - 10 - (my + IN_HEAD);
        int gy = my + IN_HEAD + 10, texts = 0, R;

        in_panel(c, px, my + IN_HEAD, pw, ph);
        for (i = 4; i <= 8; i++) {
            int tw2 = 0, th2 = 0;

            if (i != 6 && ui_letters(NULL, label_bmp(rdp(labs, 4 * (u32)i), 1), 0, 0, 0, 0, &tw2, &th2)) {
                texts += th2 + 4;
            }
        }
        R = (pw - 24 < ph - 20 - texts ? pw - 24 : ph - 20 - texts) / 2;
        if (R > 30) {
            radar(c, px + pw / 2, gy + R, R, in->screen_rgba[1], in->frame);
            gy += 2 * R + 8;
        }
        for (i = 4; i <= 8; i++) {
            const void *t = label_bmp(rdp(labs, 4 * (u32)i), 1);
            int tw2 = 0, th2 = 0;

            if (i == 6 || t == NULL || !ui_letters(NULL, t, 0, 0, 0, 0, &tw2, &th2)) {
                continue;
            }
            ui_letters(c, t, px + (pw - tw2) / 2, gy, UI_WHITE, IN_SHADOW, NULL, NULL);
            gy += th2 + 4;
        }
    }
    rp_drive(g);
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void survey_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    const u8 *w = (const u8 *)app_work(RS_OVERLAY);
    int st = survey_step();

    if (w == NULL) {
        return;
    }
    if (st == g_survey_top) {
        survey_top_run(c, view, in);
        return;
    }
    in_view(view);
    if (st == RS_LIST && rdp(w, 0x14) != NULL && rd32(rdp(w, 0x14), 0x1C) != 0) {
        survey_list_run(c, (const u8 *)rdp(w, 0x14));
    } else if (st == RS_REPORT && rdp(w, 0x18) != NULL && rd32(rdp(w, 0x18), 0x20) != 0) {
        survey_report_run(c, (const u8 *)rdp(w, 0x18), in);
    } else {
        g_rp_target = -1;
    }
}

static void survey_reset(void)
{
    g_rp_target = -1;
}

static const EidosUiRecipe recipe_survey = {
    "survey", survey_match, survey_run, survey_reset, 0
};


/* ------------------------------------------------------------------ */
/* The town map and Fly                                                */
/* ------------------------------------------------------------------ */

/*
 * The town map (overlay 72, ov072/townmap.c: Fly's "Fly to where?" and the
 * Town Map item; mock-up "town_map") as one window over the blurred world:
 * the game's own map from its lower screen, three quarters of its size,
 * with its cursor, routes and towns as it draws them; on the right the
 * town under the cursor in the game's words (its upper screen: the name,
 * the description, the places in it); the prompt as the title.  The map
 * is the stylus: a press on it taps the game's map there (a town is
 * chosen, a drag scrolls).  Fly (A), Zoom and Back tap or press what the
 * game offers; the keys are the game's.
 *
 * TownMapWork (ov072/townmap_types.h): selection.box.win 0xB0 (the upper
 * screen: name, description, places), panel.win 0xC4 (the prompt),
 * hoverTown 0x134.  The lower screen's bar: zoom (177, 179), back
 * (243, 179).
 */
#define TM_OVERLAY 72
#define TM_MAP_H   168          /* the map above the game's bar           */

static int townmap_match(void)
{
    return app_work(TM_OVERLAY) != NULL;
}

static void townmap_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Fly", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    const u8 *w = (const u8 *)app_work(TM_OVERLAY);
    int cw = c->canvas->w, ch = c->canvas->h;
    int mapw = 256 * 3 / 4, maph = TM_MAP_H * 3 / 4;
    int mw = cw - 16 < 480 ? cw - 16 : 480;
    int iw = 0, ih = 0, mh, mx, my, rx, rw, y, left_h, right_h;
    const void *info;
    EidosUiRect r;

    in_view(view);
    if (w == NULL) {
        return;
    }
    info = win_bmp_of(rdp(w, 0xB0));
    ui_letters(NULL, info, 0, 0, 0, 0, &iw, &ih);
    left_h = maph + 4 + 4 + 24;
    right_h = ih + 16;
    mh = IN_HEAD + (left_h > right_h ? left_h : right_h) + 10;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    if (my < 2) my = 2;
    in_window(c, mx, my, mw, mh, win_bmp_of(rdp(w, 0xC4)));
    /* the game's map */
    ui_panel(c, mx + 10, my + IN_HEAD, mapw + 4, maph + 4, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, UI_EDGE, 255);
    if (in->screen_rgba[1] != NULL) {
        int dx, dy;

        for (dy = 0; dy < maph; dy++) {
            for (dx = 0; dx < mapw; dx++) {
                const u8 *p = in->screen_rgba[1] + ((size_t)(dy * 4 / 3) * 256 + (size_t)(dx * 4 / 3)) * 4;

                ui_fill(c, mx + 12 + dx, my + IN_HEAD + 2 + dy, 1, 1, (u32)p[0] << 16 | (u32)p[1] << 8 | p[2], 255);
            }
        }
    }
    r.x = mx + 12; r.y = my + IN_HEAD + 2; r.w = mapw; r.h = maph;
    if (eidos_ui_held() && (eidos_ui_hit(r) & EIDOS_UI_HOVER)) {
        int px, py;

        if (eidos_ui_mouse_pos(&px, &py)) {
            eidos_ui_tap((px - r.x) * 4 / 3, (py - r.y) * 4 / 3, 2);
        }
    }
    /* Fly, Zoom and Back under the map */
    y = my + IN_HEAD + maph + 8;
    {
        int tw = (mapw + 4 - 8) / 3;

        if (in_tile_words(c, mx + 10, y, tw, 24, NULL, "Fly", rdp(w, 0x134) != NULL && eidos_ui_keys_lit(), 0)
            & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 2);
        }
        if (in_tile_words(c, mx + 10 + tw + 4, y, tw, 24, NULL, "Zoom", 0, 0) & EIDOS_UI_CLICK) {
            eidos_ui_tap(177, 179, 3);
        }
        if (in_tile_words(c, mx + 10 + 2 * (tw + 4), y, tw, 24, NULL, "Back", 0, 0) & EIDOS_UI_CLICK) {
            eidos_ui_tap(243, 179, 3);
        }
    }
    /* the town under the cursor, in the game's words (its upper screen) */
    rx = mx + 10 + mapw + 4 + 8;
    rw = mx + mw - 10 - rx;
    in_panel(c, rx, my + IN_HEAD, rw, mh - IN_HEAD - 10);
    if (ih > 0) {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, rx + 2, my + IN_HEAD + 2, rw - 4, mh - IN_HEAD - 14);
        ui_letters(c, info, rx + (iw < rw - 12 ? (rw - iw) / 2 : 6), my + IN_HEAD + 8, UI_WHITE, IN_SHADOW, NULL, NULL);
        ui_pop(c, 0, 0, &saved);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void townmap_reset(void)
{
}

static const EidosUiRecipe recipe_townmap = {
    "townmap", townmap_match, townmap_run, townmap_reset, 0
};

/* ------------------------------------------------------------------ */
/* After a catch: the Pokedex registration and the nickname question   */
/* ------------------------------------------------------------------ */

/*
 * After a catch (overlay 92's battle return pushes overlay 207,
 * ov207/zukan_toroku_graphic.c and zukan_nickname.c; mock-up
 * "catch_nickname"): over the held picture of the battle, the caught
 * Pokemon's card (its picture, number, name and types, REGISTERED when it
 * is new to the Pokedex) and the game's question in the battle's message
 * box, its YES and NO as the battle's tiles.  A tile taps the game's own
 * button (the battle input's yes/no, 127,56 and 127,104); the keys are the
 * game's.
 *
 * EventBattleReturnWork (ov092): caughtMon 0x08, catchArgs 0x0C
 * (wasAlreadyCaught first), procSys 0x40.  ZukanTorokuWork (ov207):
 * nickname 0x24.  ZukanNicknameWork: bmpWin 0x24 (the question), input
 * 0x54 (the yes/no).
 */
extern u32 POKE_TOOL_GetPartyValue(void *pp, u32 paramId, u32 arg) MN_WEAK;

static const u8 *catch_work(const u8 **ret)
{
    void *gs = game_system(), *cur, *inner;
    const u8 *rw;

    *ret = NULL;
    if (gs == NULL || (cur = current_proc(gs)) == NULL || rd32(cur, 0x04) != 92 || (rw = rdp(cur, 0x14)) == NULL) {
        return NULL;
    }
    inner = rdp(rw, 0x40) != NULL ? rdp(rdp(rw, 0x40), 0x08) : NULL;
    if (inner == NULL || rd32(inner, 0x04) != 207 || rd32(inner, 0x08) < 2u) {
        return NULL;
    }
    *ret = rw;
    return (const u8 *)rdp(inner, 0x14);
}

static int catch_match(void)
{
    const u8 *rw, *tw = catch_work(&rw);

    return tw != NULL && rdp(tw, 0x24) != NULL;
}

static void catch_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "No" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    const u8 *rw, *tw = catch_work(&rw), *nk;
    int cw = c->canvas->w, ch = c->canvas->h, species = 0, t1 = -1, t2 = -1;
    int px, py, pw = 280, ph = 136, bx, by, bw, bh, i;
    char name[32] = "", num[16];
    void *mon;

    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    if (tw == NULL || (nk = (const u8 *)rdp(tw, 0x24)) == NULL) {
        return;
    }
    mon = rdp(rw, 0x08);
    if (mon != NULL && POKE_TOOL_GetPartyValue != NULL) {
        species = (int)POKE_TOOL_GetPartyValue(mon, 5, 0);
    }
    /* the card */
    px = (cw - pw) / 2;
    py = (ch - 17 - 70 - ph) / 2;
    if (py < 4) py = 4;
    ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 9, IN_MBODY, IN_MEDGE, 240);
    if (rd32(rw, 0x0C) == 0) {
        ui_text(c, px + 10, py + 4, "REGISTERED", IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
    }
    ui_fill(c, px + 8, py + 17, pw - 16, 1, IN_MEDGE, 255);
    if (species > 0 && species < 700) {
        ui_mon_front_draw(c, (u16)species, 0, in->frame, px + 8, py + 20, 110, 110);
        ui_species_name(species, name, (int)sizeof name);
        ui_species_types(species, &t1, &t2);
        snprintf(num, sizeof num, ui_trf("No. %03d"), species);
        ui_text(c, px + 128, py + 36, num, UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
        ui_text(c, px + 128, py + 50, name, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 2);
        if (t1 >= 0) ui_type_tag(c, px + 128, py + 90, 56, t1);
        if (t2 >= 0 && t2 != t1) ui_type_tag(c, px + 190, py + 90, 56, t2);
    }
    /* the question in the battle's box, YES and NO */
    bw = cw - 16 < 300 ? cw - 16 : 300;
    bh = ui_textbox_h(rdp(nk, 0x24));
    bx = (cw - bw) / 2;
    by = ch - 17 - 6 - (bh > 0 ? bh : 42);
    if (bh > 0) {
        ui_panel(c, bx, by, bw, bh, UI_CUT_TL | UI_CUT_BR, 7, UI_BODY, UI_EDGE, 235);
        ui_letters(c, ui_bmpwin_bmp(rdp(nk, 0x24)), bx + 12, by + 7, UI_WHITE, UI_EDGE, NULL, NULL);
    }
    if (rdp(nk, 0x54) != NULL) {
        static const char *const yn[2] = { "YES", "NO" };
        static const int tap_y[2] = { 56, 104 };

        for (i = 0; i < 2; i++) {
            if (sheet_tile_hit(c, bx + bw - 90, by - 4 - 2 * 26 + i * 26, 90, 23, yn[i], 0) & EIDOS_UI_CLICK) {
                eidos_ui_tap(127, tap_y[i], 3);
            }
        }
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
}

static void catch_reset(void)
{
}

static const EidosUiRecipe recipe_catch = {
    "catch", catch_match, catch_run, catch_reset, 0
};

/* ------------------------------------------------------------------ */
/* The move relearner and the move tutors                              */
/* ------------------------------------------------------------------ */

/*
 * The move relearner (overlay 173, ov173/poke_summary_main.c, opened by
 * SCRCMD_PROC_CallMoveRelearn; the tutors open the same screen; mock-up
 * "move_relearn"): a window with the moves the Pokemon can learn as tiles
 * (type, name, PP), the chosen one's power, accuracy and text on the
 * right, the game's question as a system box and its YES / NO as a list.
 * A tile taps the game's own row when it is on the game's screen, else
 * the keys move the game's cursor there; the keys stay the game's.
 *
 * PokeSummaryWork (ov173/poke_summary_types.h): mon 0x000 (PokeSummaryMon:
 * moveList 0x10, selectedIndex 0x14 (the highlighted one of the four rows
 * on screen), scrollIndex 0x16 (the top row): the move is the list's
 * scrollIndex + selectedIndex), moveCount 0x1B8,
 * msgWindow 0x1C0, taskMenu 0x1E0.  The lower screen: rows at
 * (128, 80 + 24 * row), back (232, 180).  Move data: WAZA_TOOL_GetParam
 * (0 type, 3 power, 4 accuracy, 5 PP); names a/0/0/2 file 203, texts 202.
 */
#define RL_OVERLAY 173
#define RL_ROWS    6

extern u32 WAZA_TOOL_GetParam(u32 move, u32 param) MN_WEAK;

static int g_rl_target = -1, g_rl_wait, g_rl_last, g_rl_steps;

static int relearn_match(void)
{
    const u8 *w = (const u8 *)app_work(RL_OVERLAY);

    return w != NULL && rdp(w, 0x000) != NULL && rdp(rdp(w, 0x000), 0x10) != NULL;
}

static void relearn_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Learn", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static int first;
    const u8 *w = (const u8 *)app_work(RL_OVERLAY), *mon;
    const u16 *moves;
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - 24 < 460 ? cw - 24 : 460, mh = IN_HEAD + RL_ROWS * 30 + 8;
    int mx = (cw - mw) / 2, my, lw, rx, rw, count, sel, scroll, i, y;
    char name[40], line[48];

    (void)in;
    in_view(view);
    if (w == NULL || (mon = (const u8 *)rdp(w, 0x000)) == NULL || (moves = (const u16 *)rdp(mon, 0x10)) == NULL) {
        return;
    }
    count = w[0x1B8];
    if (count <= 0 || count > 64) {
        for (count = 0; count < 64 && moves[count] != 0 && moves[count] != 0xFFFFu; count++) {
        }
    }
    scroll = (int)rd16(mon, 0x16);
    sel = scroll + (int)rd16(mon, 0x14);     /* the move in the whole list */
    my = (ch - 17 - mh - 48) / 2;
    if (my < 4) my = 4;
    in_window(c, mx, my, mw, mh, NULL);
    ui_text(c, mx + 12, my + 6, "REMEMBER A MOVE", IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
    lw = mw * 11 / 20;
    if (sel < first) first = sel;
    if (sel >= first + RL_ROWS) first = sel - RL_ROWS + 1;
    if (first > count - RL_ROWS) first = count - RL_ROWS;
    if (first < 0) first = 0;
    for (i = 0; i < RL_ROWS && first + i < count; i++) {
        int row = first + i, mv = moves[row], st, dy, type;
        const char *game = ui_game_text(2, 203, (u32)mv);
        const char *nm = ui_move_name_modern(mv, game);

        st = in_tile(c, mx + 12, my + IN_HEAD + 30 * i, lw - 12, 26, row == sel, &dy);
        snprintf(name, sizeof name, "%s", nm != NULL ? nm : game);
        ui_text(c, mx + 24, my + IN_HEAD + 30 * i + dy + ui_text_y(26, UI_FONT_LARGE), name, UI_WHITE, IN_SHADOW,
                UI_FONT_LARGE, 1);
        type = WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam((u32)mv, 0) : 0;
        ui_type_tag(c, mx + lw - 12 - 50, my + IN_HEAD + 30 * i + dy + 7, 46, type);
        if (st & EIDOS_UI_CLICK) {
            if (row >= scroll && row < scroll + 4) {
                eidos_ui_tap(128, 80 + 24 * (row - scroll), 3);
            } else {
                g_rl_target = row;
                g_rl_wait = 0;
                g_rl_last = -1;
                g_rl_steps = 0;
            }
        }
    }
    /* the chosen move: its numbers and its text */
    rx = mx + lw + 6;
    rw = mx + mw - 12 - rx;
    y = my + IN_HEAD;
    if (sel >= 0 && sel < count) {
        int mv = moves[sel], pw, ac, pp;
        int px[3] = { 10, 90, 150 }, vx[3] = { 50, 116, 166 }, j;
        char desc[400];

        pw = WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam((u32)mv, 3) : 0;
        ac = WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam((u32)mv, 4) : 0;
        pp = WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam((u32)mv, 5) : 0;
        /* the three numbers at their places in a panel of 186 or more;
         * in a narrower one (a tablet's 4:3 canvas) in three equal columns,
         * each value just after its label, so PP stays inside */
        if (rw < 186) {
            static const char *const labs[3] = { "POWER", "ACC", "PP" };

            for (j = 0; j < 3; j++) {
                px[j] = 8 + j * (rw - 12) / 3;
                vx[j] = px[j] + ui_text_w(labs[j], UI_FONT_SMALL) + 3;
            }
        }
        in_panel(c, rx, y, rw, RL_ROWS * 30 - 34);
        ui_text(c, rx + px[0], y + 6, "POWER", UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
        snprintf(line, sizeof line, "%s", pw > 1 ? "" : "---");
        if (pw > 1) snprintf(line, sizeof line, "%d", pw);
        ui_text(c, rx + vx[0], y + 3, line, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
        ui_text(c, rx + px[1], y + 6, "ACC", UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
        snprintf(line, sizeof line, "%s", "---");
        if (ac > 0 && ac <= 100) snprintf(line, sizeof line, "%d", ac);
        ui_text(c, rx + vx[1], y + 3, line, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
        ui_text(c, rx + px[2], y + 6, "PP", UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
        snprintf(line, sizeof line, "%d", pp);
        ui_text(c, rx + vx[2], y + 3, line, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
        ui_fill(c, rx + 8, y + 20, rw - 16, 1, UI_EDGE, 255);
        flatten(ui_game_text(2, 202, (u32)mv), desc, (int)sizeof desc);
        wrapped(c, rx + 10, y + 26, rw - 20, 13, UI_FONT_SMALL, desc, UI_LIGHT, IN_SHADOW, 6);
    }
    if (in_tile_words(c, rx, my + IN_HEAD + RL_ROWS * 30 - 30, rw, 26, NULL, "Back", 0, 0) & EIDOS_UI_CLICK) {
        eidos_ui_tap(232, 180, 3);
    }
    /* the game's question and its YES / NO: the game prints its messages
     * into BMP window 7 (bmpWindows at 0x004, POKE_SUMMARY_MAIN_
     * StartTextStream); 0x1C0 holds no message, so before, the "Give up?"
     * question after B never showed and its YES / NO could not be
     * answered (#24) */
    {
        const void *mwin = rdp(w, 0x004 + 7 * 4);
        int bh = mwin != NULL ? ui_textbox_h(mwin) : 0;

        if (bh > 0) {
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, my + mh + 4, mw, mwin, -1, -1);
        }
        if (rdp(w, 0x1E0) != NULL) {
            ui_taskmenu(c, rdp(w, 0x1E0), mx + mw, my + mh, UI_CHOICE_SYSTEM);
        }
    }
    /* the keys walk the game's cursor to a row off its screen; the walk
     * stops at the row, when the cursor no longer moves, after as many
     * steps as the list has rows, or when the player presses a key, so it
     * can never hold the screen (#24: it pressed DOWN for ever) */
    if (g_rl_target >= 0 && in != NULL && in->buttons != 0) {
        g_rl_target = -1;
    }
    if (g_rl_target >= 0) {
        if (g_rl_wait > 0) {
            g_rl_wait--;
        } else if (sel == g_rl_target || g_rl_target >= count || sel == g_rl_last
                   || g_rl_steps > count) {
            g_rl_target = -1;
        } else {
            eidos_ui_press(sel < g_rl_target ? BW_KEY_DOWN : BW_KEY_UP, 2);
            g_rl_last = sel;
            g_rl_steps++;
            g_rl_wait = 6;
        }
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void relearn_reset(void)
{
    g_rl_target = -1;
}

static const EidosUiRecipe recipe_relearn = {
    "relearn", relearn_match, relearn_run, relearn_reset, 0
};

/* ------------------------------------------------------------------ */
/* Xtransceiver story calls                                            */
/* ------------------------------------------------------------------ */

/*
 * The Xtransceiver's story calls (overlay 174, ov174/local_tvt_sys.c,
 * opened by SCRCMD_PROC_CallValueScreen; mock-ups "xtransceiver_2" and
 * "xtransceiver_4"): over the world, each caller's video feed is a card at
 * the top right, two or four of them in a grid, cut from the game's own
 * upper screen (each caller fills a half or a quarter of it); the one who
 * speaks has the blue edge and the signal marks, and the game's words are
 * a white speech box beside the grid with its tail at the speaker.  The
 * game's text window is left out of the upper screen so the feeds are
 * clean.  A click is A, a right click B; the keys stay the game's.
 *
 * LocalTvtSys (ov174/local_tvt_sys_shared_types.h): charaCount 0x08 (2 or
 * 4), activeChara 0x2C (0 when no one speaks), window 0x5C (a BmpWin),
 * charaIndex 0x88 (the one speaking).
 */
#define TVT_OVERLAY 174

static int tvt_match(void)
{
    const u8 *w = (const u8 *)app_work(TVT_OVERLAY);
    int n;

    if (w == NULL) {
        return 0;
    }
    n = (int)rd32(w, 0x08);
    return n == 2 || n == 4;
}

static void tvt_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Next" };
    static const u32 keys[] = { BW_KEY_A };
    const u8 *w = (const u8 *)app_work(TVT_OVERLAY), *win;
    int cw = c->canvas->w, ch = c->canvas->h, n, cols, rows, fw, fh, gx, gy, i, spk = -1;
    int sx = 0, sy = 0;
    EidosUiRect all;

    in_view(view);
    if (w == NULL) {
        return;
    }
    n = (int)rd32(w, 0x08);
    win = (const u8 *)rdp(w, 0x5C);
    if (win != NULL && g2d_hw_hide_main_bgs != NULL && win[2] < 4) {
        g2d_hw_hide_main_bgs(1u << win[2]);     /* the game's text window */
    }
    if (w[0x88] < n) {
        spk = w[0x88];                  /* the one speaking (charaIndex) */
    }
    cols = 2;
    rows = n == 4 ? 2 : 1;
    fw = cw / 5 < 120 ? cw / 5 : 120;
    fh = n == 4 ? fw * 96 / 128 : fw * 192 / 128;
    gx = cw - 6 - cols * fw - 4;
    gy = 6;
    for (i = 0; i < n; i++) {
        int x = gx + (i % cols) * (fw + 4), y = gy + (i / cols) * (fh + 4);
        int srcx = (i % cols) * 128, srcy = n == 4 ? (i / cols) * 96 : 0, srch = n == 4 ? 96 : 192;
        int lit = i == spk, dx, dy;

        ui_panel(c, x - 3, y - 3, fw + 6, fh + 6, UI_CUT_TL | UI_CUT_BR, 6, IN_MBODY, lit ? IN_HL : IN_MEDGE, 245);
        if (in->screen_rgba[0] != NULL) {
            for (dy = 0; dy < fh; dy++) {
                for (dx = 0; dx < fw; dx++) {
                    const u8 *p = in->screen_rgba[0]
                                  + ((size_t)(srcy + dy * srch / fh) * 256 + (size_t)(srcx + dx * 128 / fw)) * 4;
                    u32 col = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];

                    if ((dy & 1) != 0) {
                        col = (col >> 1 & 0x7F7F7Fu) + (col >> 2 & 0x3F3F3Fu) + (col >> 3 & 0x1F1F1Fu);
                    }
                    ui_fill(c, x + dx, y + dy, 1, 1, col, 255);
                }
            }
        }
        if (lit) {
            int k;

            for (k = 0; k < 3; k++) {
                ui_fill(c, x + fw - 14 + k * 4, y + 8 - 2 * k, 3, 3 + 2 * k, IN_HL, 255);
            }
            sx = x;
            sy = y + fh / 2;
        }
    }
    /* the game's words beside the grid, the tail at the speaker */
    if (win != NULL) {
        int bh = ui_textbox_h(win), bw = gx - 14 < 260 ? gx - 14 : 260, bx = gx - 10 - bw, by;

        if (bh > 0 && bw > 80) {
            by = spk >= 0 ? sy - bh / 2 : gy;
            if (by < 6) by = 6;
            if (spk >= 0) {
                int t, ty = sy < by + 8 ? by + 8 : sy > by + bh - 8 ? by + bh - 8 : sy;

                for (t = 0; t < 10; t++) {
                    ui_fill(c, bx + bw - 2 + t, ty - (6 - t * 6 / 10), 1, 2 * (6 - t * 6 / 10) + 1, 0xFFFFFFu, 255);
                }
                (void)sx;
            }
            ui_textbox_draw(c, UI_TEXTBOX_NARRATION, bx, by, bw, win, -1, -1);
        }
    }
    all.x = 0; all.y = 0; all.w = cw; all.h = ch - ui_bar_h(c);
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void tvt_reset(void)
{
}

static const EidosUiRecipe recipe_tvt = {
    "xtransceiver_story", tvt_match, tvt_run, tvt_reset, 0
};

/* ------------------------------------------------------------------ */
/* The ending: the Hall of Fame, the staff roll, The End              */
/* ------------------------------------------------------------------ */

/*
 * The game clear (ov010 event_gameclear.c) runs the Hall of Fame (overlay
 * 180), the staff roll (181), the save screen (205) and The End (182);
 * mock-ups "hall_of_fame" and "credits".
 *
 *   Hall of Fame  the game's own presentation (its two screens, each
 *                 Pokemon as the game shows it) as one picture in the
 *                 middle, and a card at the right lists the party that
 *                 enters: icon, name, level.
 *   staff roll,   the game's two screens are text and pictures on black:
 *   The End       they are drawn as one column in the middle of the
 *                 window, the black left out, so the roll runs down the
 *                 whole height.
 *
 * The keys and the touch are the game's (A and B speed up or skip as the
 * game allows); a click is A.
 */
extern void *GAME_DATA_GetMyParty(const void *gd) MN_WEAK;
extern u32   POKEPARTY_GetCount(const void *party) MN_WEAK;
extern void *POKEPARTY_GetMember(void *party, u32 pos) MN_WEAK;

static int ending_overlay(void)
{
    void *gs = game_system(), *cur;
    int ov;

    if (gs == NULL || (cur = current_proc(gs)) == NULL || rd32(cur, 0x08) < 2u) {
        return -1;
    }
    ov = (int)rd32(cur, 0x04);
    return ov == 180 || ov == 181 || ov == 182 ? ov : -1;
}

static int ending_match(void)
{
    return ending_overlay() >= 0;
}

/* screen s of the game (0 upper, 1 lower) at (x, y), scaled by num/den,
 * the black left out */
static void ending_screen(EidosUiCtx *c, const u8 *px, int x, int y, int num, int den, int key_black)
{
    int w = 256 * num / den, h = 192 * num / den, dx, dy;

    if (px == NULL) {
        return;
    }
    for (dy = 0; dy < h; dy++) {
        const u8 *row = px + (size_t)(dy * den / num) * 256 * 4;

        for (dx = 0; dx < w; dx++) {
            const u8 *p = row + (size_t)(dx * den / num) * 4;

            if (key_black && p[0] < 12 && p[1] < 12 && p[2] < 12) {
                continue;
            }
            ui_fill(c, x + dx, y + dy, 1, 1, (u32)p[0] << 16 | (u32)p[1] << 8 | p[2], 255);
        }
    }
}

static void ending_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    int cw = c->canvas->w, ch = c->canvas->h, ov = ending_overlay();
    EidosUiRect all;

    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 1;
    view->hold_world = 1;
    ui_fill(c, 0, 0, cw, ch, 0x000000u, 255);
    if (ov == 180) {
        /* the game's presentation on its two screens, one column in the
         * middle of the window */
        int num = ch * 256 / 384, den = 256, sh = 192 * num / den;
        void *gd = game_data(), *party = gd != NULL && GAME_DATA_GetMyParty != NULL ? GAME_DATA_GetMyParty(gd) : NULL;
        int n = party != NULL && POKEPARTY_GetCount != NULL ? (int)POKEPARTY_GetCount(party) : 0, i;
        int pw = 150, rowh = 34, px2, py2;

        ending_screen(c, in->screen_rgba[0], (cw - 256 * num / den) / 2, (ch - 2 * sh) / 2, num, den, 0);
        ending_screen(c, in->screen_rgba[1], (cw - 256 * num / den) / 2, (ch - 2 * sh) / 2 + sh, num, den, 0);
        if (n > 6) n = 6;
        px2 = cw - 8 - pw;
        py2 = (ch - (IN_HEAD + n * (rowh + 3) + 8)) / 2;
        ui_panel(c, px2, py2, pw, IN_HEAD + n * (rowh + 3) + 8, UI_CUT_TL | UI_CUT_BR, 9, IN_MBODY, IN_MEDGE, 225);
        {
            const char *t = ui_game_text(2, 154, 16);

            ui_text(c, px2 + 12, py2 + 6, t[0] != '\0' ? t : "HALL OF FAME", IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
        }
        ui_fill(c, px2 + 10, py2 + IN_HEAD - 4, pw - 20, 1, IN_MEDGE, 255);
        for (i = 0; i < n && POKEPARTY_GetMember != NULL; i++) {
            void *pp = POKEPARTY_GetMember(party, (u32)i);
            int sp = POKE_TOOL_GetPartyValue != NULL ? (int)POKE_TOOL_GetPartyValue(pp, 5, 0) : 0;
            int lv = POKE_TOOL_GetPartyValue != NULL ? (int)POKE_TOOL_GetPartyValue(pp, 0x9E, 0) : 0;
            int fm = POKE_TOOL_GetPartyValue != NULL ? (int)POKE_TOOL_GetPartyValue(pp, 0x6F, 0) : 0;
            int y = py2 + IN_HEAD + i * (rowh + 3);
            const UiImage *ic = ui_mon_icon((u16)sp, (u8)fm, (in->frame / 16) & 1);
            char name[32], lvt[12];

            ui_panel(c, px2 + 8, y, pw - 16, rowh, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 240);
            if (ic != NULL) {
                ui_image(c, px2 + 10, y + 1, ic, 1);
            }
            name[0] = '\0';
            ui_species_name(sp, name, (int)sizeof name);
            ui_text(c, px2 + 44, y + 3, name, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
            snprintf(lvt, sizeof lvt, ui_trf("Lv. %d"), lv);
            ui_text(c, px2 + 44, y + 19, lvt, UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
        }
    } else {
        /* the roll: the two screens as one column, the black left out */
        int num = ch * 1000 / 384 * 256 / 1000, den = 256, sh;

        if (num < den / 2) num = den / 2;
        sh = 192 * num / den;
        ending_screen(c, in->screen_rgba[0], (cw - 256 * num / den) / 2, (ch - 2 * sh) / 2, num, den, 1);
        ending_screen(c, in->screen_rgba[1], (cw - 256 * num / den) / 2, (ch - 2 * sh) / 2 + sh, num, den, 1);
    }
    all.x = 0; all.y = 0; all.w = cw; all.h = ch;
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    }
}

static void ending_reset(void)
{
}

static const EidosUiRecipe recipe_ending = {
    "ending", ending_match, ending_run, ending_reset, 0
};

/* ------------------------------------------------------------------ */
/* The 3D demos                                                        */
/* ------------------------------------------------------------------ */

/*
 * The 3D demo player (overlay 203, ov203/demo3d.c: the Nimbasa City Ferris
 * wheel, the Royal Unova and the others the scripts start through
 * EVENT_FIELD_PROCLINK_CreateCommExitEvent).  The scene is the game's 3D on
 * its upper screen; the lower one holds nothing to read.  It fills the
 * window as the field does; before, no recipe knew it and the two DS
 * screens showed (#24).
 */
static int demo3d_match(void)
{
    void *gs = game_system(), *cur;

    if (gs == NULL || (cur = current_proc(gs)) == NULL || rd32(cur, 0x08) < 2u) {
        return 0;
    }
    return (int)rd32(cur, 0x04) == 203;
}

static void demo3d_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    (void)c;
    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
}

static const EidosUiRecipe recipe_demo3d = {
    "demo3d", demo3d_match, demo3d_run, NULL, 0
};

/* ------------------------------------------------------------------ */
/* Egg hatching                                                        */
/* ------------------------------------------------------------------ */

/*
 * An Egg hatching (overlay 215, ov215/egg_demo_*.c; mock-up "egg_hatch"):
 * the game's own scene (the Egg, its cracks, the flash, the Pokemon) is
 * the world the window shows, its words are the white box at the bottom, and its yes/no is
 * a white list at the box's right end.  The game's windows are left out of
 * its screen.  The keys are the game's; a click on YES or NO moves the
 * game's cursor there with its keys and presses A, a click elsewhere is A.
 *
 * EggDemoWork (ov215/egg_demo_types.h): window 0x58 (the words, a BmpWin
 * on BG1), bmpMenu 0x78 (the yes/no, BmpMenu: cursor 0x2D).
 */
#define EGG_OVERLAY 215

static int egg_match(void)
{
    return app_work(EGG_OVERLAY) != NULL;
}

static void egg_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Next" };
    static const u32 keys[] = { BW_KEY_A };
    const u8 *w = (const u8 *)app_work(EGG_OVERLAY), *win, *menu;
    int cw = c->canvas->w, ch = c->canvas->h, bh = 0, bw, bx, by, i, clicked = 0;
    EidosUiRect all;

    g_frame = in->frame;
    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 0;
    if (w == NULL) {
        return;
    }
    if (g2d_hw_hide_main_bgs != NULL) {
        g2d_hw_hide_main_bgs(0x2u);         /* the game's windows: BG1 */
    }
    /* the scene: the game's upper screen is the world (its 3D and its
     * backdrop), the windows left out */
    /* the words and the yes/no */
    win = (const u8 *)rdp(w, 0x58);
    menu = (const u8 *)rdp(w, 0x78);
    bw = cw - 16 < 300 ? cw - 16 : 300;
    bx = (cw - bw) / 2;
    if (win != NULL && (bh = ui_textbox_h(win)) > 0) {
        by = ch - 17 - 6 - bh;
        ui_textbox_draw(c, UI_TEXTBOX_NARRATION, bx, by, bw, win, -1, -1);
        if (menu != NULL && rd32(w, 0x20) == 1) {
            int cur = menu[0x2D] & 1, lw = 70, lh = 4 + 2 * 18 + 4, lx = bx + bw - lw, ly = by - 3 - lh;

            ui_panel(c, lx, ly, lw, lh, UI_CUT_TL | UI_CUT_BR, 6, 0xFFFFFFu, 0x9698A0u, 255);
            for (i = 0; i < 2; i++) {
                EidosUiRect r;
                const char *lab = ui_game_text(2, 69, 30u + (u32)i);
                int st, lit;

                r.x = lx + 3; r.y = ly + 4 + i * 18; r.w = lw - 6; r.h = 18;
                st = eidos_ui_hit(r);
                lit = (st & EIDOS_UI_HOVER) || (eidos_ui_keys_lit() && i == cur);
                if (lit) {
                    ui_panel(c, r.x, r.y - 1, r.w, 18, UI_CUT_TL | UI_CUT_BR, 5, 0xBFE2EAu, IN_HL, 255);
                    ui_fill(c, r.x + 3, r.y + 2, 3, 12, IN_HL, 255);
                }
                ui_text(c, r.x + 10, r.y - (lit ? 1 : 0) + ui_text_y(18, UI_FONT_LARGE),
                        lab[0] != '\0' ? lab : (i == 0 ? "YES" : "NO"), 0x42424Au, 0xADADB5u, UI_FONT_LARGE, 1);
                if (st & EIDOS_UI_CLICK) {
                    clicked = 1;
                    if (i == cur) {
                        eidos_ui_press(BW_KEY_A, 2);
                    } else {
                        eidos_ui_press(i == 0 ? BW_KEY_UP : BW_KEY_DOWN, 2);
                        pend_press(BW_KEY_A, 6);
                    }
                }
            }
        }
    }
    pend_pump();
    all.x = 0; all.y = 0; all.w = cw; all.h = ch - ui_bar_h(c);
    if (!clicked && (eidos_ui_hit(all) & EIDOS_UI_CLICK)) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void egg_reset(void)
{
}

static const EidosUiRecipe recipe_egg = {
    "egg_hatch", egg_match, egg_run, egg_reset, 0
};

/* ------------------------------------------------------------------ */
/* In-game trades                                                      */
/* ------------------------------------------------------------------ */

/*
 * An in-game trade's demo (overlay 119, ov119/pokemontrade_*.c, opened by
 * ov021 fld_trade.c; mock-up "npc_trade"): the game's own scene is the
 * world the window shows, a card at the top shows the two Pokemon (the one
 * that leaves and the one that arrives: icon, name, level) with the
 * arrow between them, and the game's words are the white box at the
 * bottom (its window left out of the screen).  A click is A.
 *
 * The trade's event (FLD_TRADE_EventMain, a parent of the proc's event):
 * sentMon 0x48, receivedMon 0x4C.  TradeWork (ov119/pokemontrade_types.h):
 * messageWin 0x71C.
 */
#define TRADE_OVERLAY 119

extern int ov021_FLD_TRADE_EventMain(void *event, u32 *seq, void *work) MN_WEAK;
extern void *GAMESYSTEM_GetEvent(void *gameSystem) MN_WEAK;

static const u8 *trade_event_work(void)
{
    void *gs = game_system();
    const u8 *ev = gs != NULL && GAMESYSTEM_GetEvent != NULL ? (const u8 *)GAMESYSTEM_GetEvent(gs) : NULL;
    int depth;

    for (depth = 0; ev != NULL && depth < 6; depth++) {
        if (ov021_FLD_TRADE_EventMain != NULL
            && (uintptr_t)rd32(ev, 4) == (uintptr_t)ov021_FLD_TRADE_EventMain) {
            return (const u8 *)rdp(ev, 0x0C);
        }
        ev = (const u8 *)rdp(ev, 0);
    }
    return NULL;
}

static int trade_match(void)
{
    return app_work(TRADE_OVERLAY) != NULL && trade_event_work() != NULL;
}

static void trade_mon(EidosUiCtx *c, void *mon, int x, int y, u32 frame)
{
    int sp, lv, fm;
    const UiImage *ic;
    char name[32], lvt[12];

    if (mon == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return;
    }
    sp = (int)POKE_TOOL_GetPartyValue(mon, 5, 0);
    lv = (int)POKE_TOOL_GetPartyValue(mon, 0x9E, 0);
    fm = (int)POKE_TOOL_GetPartyValue(mon, 0x6F, 0);
    ic = ui_mon_icon((u16)sp, (u8)fm, (frame / 16) & 1);
    if (ic != NULL) {
        ui_image(c, x, y, ic, 1);
    }
    name[0] = '\0';
    ui_species_name(sp, name, (int)sizeof name);
    ui_text(c, x + 34, y + 2, name, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
    snprintf(lvt, sizeof lvt, ui_trf("Lv. %d"), lv);
    ui_text(c, x + 34, y + 18, lvt, UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
}

static void trade_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Next" };
    static const u32 keys[] = { BW_KEY_A };
    const u8 *tw = (const u8 *)app_work(TRADE_OVERLAY), *ew = trade_event_work(), *win;
    int cw = c->canvas->w, ch = c->canvas->h, pw = 240, px = (cw - pw) / 2, py = 6, i;
    EidosUiRect all;

    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 0;
    if (tw == NULL || ew == NULL) {
        return;
    }
    win = (const u8 *)rdp(tw, 0x71C);
    if (win != NULL && ui_bmpwin_bmp(win) != NULL && g2d_hw_hide_main_bgs != NULL && win[2] < 4) {
        g2d_hw_hide_main_bgs(1u << win[2]);
    }
    ui_panel(c, px, py, pw, 58, UI_CUT_TL | UI_CUT_BR, 9, IN_MBODY, IN_MEDGE, 235);
    trade_mon(c, rdp(ew, 0x4C), px + 10, py + 14, in->frame);
    trade_mon(c, rdp(ew, 0x48), px + pw / 2 + 12, py + 14, in->frame);
    for (i = 0; i < 5; i++) {
        ui_fill(c, px + pw / 2 - 6 + i, py + 29 - (4 - i), 1, 2 * (4 - i) + 1, IN_HL, 255);
    }
    if (win != NULL && ui_bmpwin_bmp(win) != NULL) {
        int bh = ui_textbox_h(win), bw = cw - 16 < 300 ? cw - 16 : 300;

        if (bh > 0) {
            ui_textbox_draw(c, UI_TEXTBOX_NARRATION, (cw - bw) / 2, ch - 17 - 6 - bh, bw, win, -1, -1);
        }
    }
    all.x = 0; all.y = 0; all.w = cw; all.h = ch - ui_bar_h(c);
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void trade_reset(void)
{
}

static const EidosUiRecipe recipe_trade = {
    "npc_trade", trade_match, trade_run, trade_reset, 0
};

/* ------------------------------------------------------------------ */
/* The Union Room's drawing                                            */
/* ------------------------------------------------------------------ */

/*
 * The drawing everyone in the Union Room can join (overlay 23,
 * ov023/oekaki.c; mock-up "union_drawing"): one window over the blurred
 * room with the game's own canvas (its lower screen, everyone's strokes as
 * the game draws them) twice its size, the members on the right (their
 * names, the one drawing marked), the eight pens and the three sizes as
 * tiles, Done, and the game's message as a system box with its YES / NO.
 * The pointer held on the canvas is the stylus there, so the stroke is the
 * game's and the others see it; the tiles tap the game's own buttons.
 *
 * OekakiWork (ov023/oekaki_types.h): playerName[5] 0x018 (Strbufs),
 * nameTagActor[5] 0x098 (a member is there), messageWindow 0x11C, menu
 * 0x178, penColor 0x420A, penSize 0x420B.  The lower screen:
 * canvas (8..248, 16..136), pens (12 + 24 * i, 170), Done (224, 170),
 * sizes (20 + 16 * i, 7).
 */
#define OEKAKI_OVERLAY 23

extern u16 *STRBUF_GetChars(void *strbuf) MN_WEAK;
extern u16  STRBUF_GetLength(void *strbuf) MN_WEAK;

static int oekaki_match(void)
{
    return app_work(OEKAKI_OVERLAY) != NULL;
}

static void oekaki_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const u32 pen_rgb[8] = { 0x000000u, 0xFFFFFFu, 0xFF0000u, 0xFF00FFu,
                                    0x0000FFu, 0x00FFFFu, 0x00FF00u, 0xFFFF00u };
    static const char *const hints[] = { "B", "Done" };
    static const u32 keys[] = { BW_KEY_B };
    const u8 *w = (const u8 *)app_work(OEKAKI_OVERLAY);
    int cw = c->canvas->w, ch = c->canvas->h, k = 2, cvw = 240, cvh = 120;
    int mw, mh, mx, my, x, y, i, members = 0;
    EidosUiRect r;
    char count[16];

    in_view(view);
    if (w == NULL) {
        return;
    }
    if (cvw * k + 24 + 120 > cw - 16 || cvh * k + 100 > ch - 17) {
        k = 1;
    }
    mw = cvw * k + 24 + 120;
    if (mw < 8 * 28 + 8 + 3 * 28 + 120 + 24) mw = 8 * 28 + 8 + 3 * 28 + 120 + 24;
    mh = IN_HEAD + cvh * k + 8 + 30 + 10;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    if (my < 2) my = 2;
    for (i = 0; i < 5; i++) {
        const u8 *sb = (const u8 *)rdp(w, 0x018 + 4u * (u32)i);

        members += sb != NULL && STRBUF_GetLength != NULL && STRBUF_GetLength((void *)sb) > 0;
    }
    snprintf(count, sizeof count, "%d / 5", members);
    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, IN_MBODY, IN_MEDGE, 245);
    ui_text(c, mx + 12, my + 6, "DRAWING", IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
    ui_text(c, mx + mw - 12 - ui_text_w(count, UI_FONT_SMALL), my + 6, count, UI_GREY, IN_SHADOW, UI_FONT_SMALL, 1);
    ui_fill(c, mx + 10, my + IN_HEAD - 4, mw - 20, 1, IN_MEDGE, 255);

    /* the game's canvas */
    x = mx + 12;
    y = my + IN_HEAD;
    ui_fill(c, x - 2, y - 2, cvw * k + 4, cvh * k + 4, UI_EDGE, 255);
    if (in->screen_rgba[1] != NULL) {
        ui_blit_screen(c, x, y, in->screen_rgba[1], 8, 16, cvw, cvh, k, 0);
    }
    r.x = x; r.y = y; r.w = cvw * k; r.h = cvh * k;
    {
        int px = -1, py = -1, pressed = 0, released = 0;

        /* the pointer held on the canvas (a mouse button, a finger) */
        if (eidos_ui_pointer(&px, &py, &pressed, &released) && px >= r.x && px < r.x + r.w && py >= r.y
            && py < r.y + r.h) {
            eidos_ui_tap(8 + (px - r.x) / k, 16 + (py - r.y) / k, 2);
            if (getenv("EIDOS_UI_DEBUG") != NULL) {
                fprintf(stderr, "eidos: oekaki stroke %d,%d\n", 8 + (px - r.x) / k, 16 + (py - r.y) / k);
            }
        }
    }

    /* the pens, the sizes and Done under the canvas */
    y = my + IN_HEAD + cvh * k + 8;
    for (i = 0; i < 8; i++) {
        int sx = x + i * 28, lit = w[0x420A] == i, st;

        r.x = sx; r.y = y; r.w = 24; r.h = 24;
        st = eidos_ui_hit(r);
        ui_panel(c, sx, y - (lit ? 1 : 0), 24, 24, UI_CUT_TL | UI_CUT_BR, 4, pen_rgb[i],
                 lit || (st & EIDOS_UI_HOVER) ? IN_HL : UI_EDGE, 255);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_tap(12 + 24 * i, 170, 3);
        }
    }
    for (i = 0; i < 3; i++) {
        int sx = x + 8 * 28 + 8 + i * 28, lit = w[0x420B] == i, st, d = 4 + 4 * i;

        r.x = sx; r.y = y; r.w = 24; r.h = 24;
        st = eidos_ui_hit(r);
        ui_panel(c, sx, y - (lit ? 1 : 0), 24, 24, UI_CUT_TL | UI_CUT_BR, 4, lit ? 0x05343Eu : UI_BODY,
                 lit || (st & EIDOS_UI_HOVER) ? IN_HL : UI_EDGE, 255);
        ui_ellipse(c, sx + 12 - d / 2, y - (lit ? 1 : 0) + 12 - d / 2, d, d, UI_WHITE, UI_WHITE, 255);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_tap(20 + 16 * i, 7, 3);
        }
    }
    if (in_tile_words(c, mx + mw - 12 - 108, y, 108, 24, NULL, "Done", 0, 0) & EIDOS_UI_CLICK) {
        eidos_ui_tap(224, 170, 3);
    }

    /* the members */
    x = mx + 12 + cvw * k + 12;
    y = my + IN_HEAD;
    for (i = 0; i < 5; i++) {
        const u8 *sb = (const u8 *)rdp(w, 0x018 + 4u * (u32)i);
        int here = sb != NULL && STRBUF_GetLength != NULL && STRBUF_GetLength((void *)sb) > 0;
        char name[32] = "";

        if (here && sb != NULL && STRBUF_GetChars != NULL && STRBUF_GetLength != NULL) {
            units_utf8(STRBUF_GetChars((void *)sb), STRBUF_GetLength((void *)sb), name, (int)sizeof name);
        }
        ui_panel(c, x, y + i * 26, mx + mw - 12 - x, 22, UI_CUT_TL | UI_CUT_BR, 4, here ? UI_BODY2 : IN_MBODY,
                 here ? UI_EDGE : IN_MEDGE, 240);
        if (here) {
            ui_fill(c, x + 5, y + i * 26 + 5, 3, 12, IN_HL, 255);
            ui_text(c, x + 12, y + i * 26 + ui_text_y(22, UI_FONT_LARGE), name, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);
        }
    }

    /* the game's message and its YES / NO */
    {
        const void *mwin = rdp(w, 0x11C);
        int bh = mwin != NULL ? ui_textbox_h(mwin) : 0;

        if (bh > 0) {
            int by = my + mh - 10 - bh;

            if (g2d_hw_hide_main_bgs != NULL) {
                g2d_hw_hide_main_bgs(0x1u);
            }
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx + 12, by, mw - 24 - 100, mwin, -1, -1);
            if (rdp(w, 0x178) != NULL) {
                ui_taskmenu(c, rdp(w, 0x178), mx + mw - 12, by + bh, UI_CHOICE_SYSTEM);
            }
        }
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void oekaki_reset(void)
{
}

static const EidosUiRecipe recipe_oekaki = {
    "union_drawing", oekaki_match, oekaki_run, oekaki_reset, 0
};

/* ------------------------------------------------------------------ */
/* The Union Room's Spin Trade                                         */
/* ------------------------------------------------------------------ */

/*
 * Spin Trade (overlay 137, ov137/guru2*.c; mock-up "union_spin"): the
 * game's own spinner scene (the dish, the Eggs, the arrows, in 3D) is the
 * world the window shows, with a title card and the game's words in the
 * system box at the bottom.  The game's
 * text window is left out of its screen.  A, the touch and the keys stay
 * the game's (A stops the spin as the game asks); a click on the scene is A.
 * The recruiting lobby is its own window (recipe_guru2_lobby below); the
 * Egg list before the spin is the party list's recipe (mode 21).
 *
 * Guru2Entry (ov137/guru2_types.h): session 0x0C.  Guru2Session:
 * playerCount 0x08, gameWork 0x1C (the spinner's Guru2Game, 0 outside it).
 * Guru2Game: windows 0xCE0 (Guru2Windows: mainWindow +0x10), nameBuf[5]
 * (the members' names, Strbufs) after it.
 */
#define GURU2_OVERLAY 137

/* The work of the proc the Spin Trade runs now on its own proc system
 * (Guru2Entry.procHandle, +0x20): the lobby, the party list, the spinner. */
static const u8 *guru2_current(const u8 **sess)
{
    const u8 *e = (const u8 *)app_work(GURU2_OVERLAY);

    *sess = e != NULL ? (const u8 *)rdp(e, 0x0C) : NULL;
    return e != NULL && *sess != NULL ? (const u8 *)proc_work_of(rdp(e, 0x20)) : NULL;
}

static const u8 *guru2_game(void)
{
    const u8 *sess, *cur = guru2_current(&sess);

    /* the session keeps its pointers after the proc ends: only the one
     * running counts */
    return cur != NULL && cur == (const u8 *)rdp(sess, 0x1C) ? cur : NULL;
}

static int guru2_match(void)
{
    return guru2_game() != NULL;
}

static void guru2_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Stop" };
    static const u32 keys[] = { BW_KEY_A };
    const u8 *g = guru2_game(), *win;
    int cw = c->canvas->w, ch = c->canvas->h;
    EidosUiRect all;

    (void)in;
    /* the spinner is 3D: the game's own scene is the world the window
     * shows, the game's text window left out */
    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 0;
    if (g == NULL) {
        return;
    }
    win = (const u8 *)rdp(g, 0xCE0 + 0x10);
    if (win != NULL && ui_bmpwin_bmp(win) != NULL && g2d_hw_hide_main_bgs != NULL && win[2] < 4) {
        g2d_hw_hide_main_bgs(1u << win[2]);
    }
    {
        const char *t = "SPIN TRADE";
        int tw = ui_text_w(t, UI_FONT_SMALL) + 28;

        ui_panel(c, 6, 6, tw, 18, UI_CUT_TL | UI_CUT_BR, 5, IN_MBODY, IN_MEDGE, 235);
        ui_text(c, 18, 9, t, IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
    }
    if (win != NULL && ui_bmpwin_bmp(win) != NULL) {
        int bh = ui_textbox_h(win), bw = cw - 16 < 300 ? cw - 16 : 300;

        if (bh > 0) {
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, (cw - bw) / 2, ch - 17 - 6 - bh, bw, win, -1, -1);
        }
    }
    all.x = 0; all.y = 0; all.w = cw; all.h = ch - ui_bar_h(c);
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void guru2_reset(void)
{
}

static const EidosUiRecipe recipe_guru2 = {
    "union_spin", guru2_match, guru2_run, guru2_reset, 0
};

/*
 * The Spin Trade's recruiting lobby (ov137/guru2_receipt.c): one window
 * over the blurred room with the game's title, the members it lists (its
 * own printed rows: name and ID), the five places of the spinner with
 * the members that have joined, the game's words in the system box under
 * it and its YES / NO.  The keys are the game's (A proceeds, B cancels).
 *
 * Guru2Session.lobbyWork 0x20 (Guru2Lobby, ov137/ov137_types.h):
 * memberWindow 0xD0, messageWindow 0xD4, titleWindow 0xDC, yesNoMenu 0xE8
 * (a BmpMenu: cursor 0x2D), memberActor[5] 0x9C, actorState[5] 0x160
 * (2 shown).
 */
static const u8 *guru2_lobby(void)
{
    const u8 *sess, *cur = guru2_current(&sess);

    return cur != NULL && cur == (const u8 *)rdp(sess, 0x20) ? cur : NULL;
}

static int lobby_match(void)
{
    const u8 *l = guru2_lobby();

    return l != NULL && win_bmp_of(rdp(l, 0xDC)) != NULL;
}

static void lobby_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Proceed", "B", "Cancel" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    const u8 *l = guru2_lobby(), *menu;
    const void *title, *members, *msg;
    int cw = c->canvas->w, ch = c->canvas->h, mw = 320, mh = IN_HEAD + 104, mx, my, i, bh = 0;

    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    if (l == NULL) {
        return;
    }
    title = win_bmp_of(rdp(l, 0xDC));
    members = win_bmp_of(rdp(l, 0xD0));
    msg = rdp(l, 0xD4);
    if (mw > cw - 16) {
        mw = cw - 16;
    }
    if (msg != NULL && win_bmp_of(msg) != NULL) {
        bh = ui_textbox_h(msg);
    }
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh - (bh > 0 ? bh + 6 : 0)) / 2;
    if (my < 4) {
        my = 4;
    }
    in_window(c, mx, my, mw, mh, title);
    /* the members, as the game prints them */
    in_panel(c, mx + 10, my + IN_HEAD, 170, mh - IN_HEAD - 10);
    if (members != NULL) {
        ui_letters(c, members, mx + 16, my + IN_HEAD + 4, UI_WHITE, IN_SHADOW, NULL, NULL);
    }
    /* the spinner's five places, the joined ones lit */
    {
        int cx = mx + 180 + (mw - 180) / 2, cy = my + IN_HEAD + (mh - IN_HEAD - 10) / 2;
        static const int ox[5] = { 0, 29, 18, -18, -29 }, oy[5] = { -30, -9, 24, 24, -9 };

        for (i = 0; i < 5; i++) {
            int on = rdp(l, 0x9C + 4u * (u32)i) != NULL && rd32(l, 0x160 + 4u * (u32)i) == 2;

            ui_ellipse(c, cx + ox[i] - 11, cy + oy[i] - 11, 22, 22, on ? IN_HL : UI_BODY, on ? UI_WHITE : UI_EDGE, 255);
        }
    }
    if (bh > 0) {
        int by = my + mh + 6;

        ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, by, mw, msg, -1, -1);
        menu = (const u8 *)rdp(l, 0xE8);
        if (menu != NULL) {
            int cur = menu[0x2D] & 1, lw = 70, lh = 4 + 2 * 18 + 4, lx = mx + mw - lw, ly = by - 3 - lh;

            ui_panel(c, lx, ly, lw, lh, UI_CUT_TL | UI_CUT_BR, 6, 0xFFFFFFu, 0x9698A0u, 255);
            for (i = 0; i < 2; i++) {
                EidosUiRect r;
                const char *lab = ui_game_text(2, 69, 30u + (u32)i);
                int st, lit;

                r.x = lx + 3; r.y = ly + 4 + i * 18; r.w = lw - 6; r.h = 18;
                st = eidos_ui_hit(r);
                lit = (st & EIDOS_UI_HOVER) || (eidos_ui_keys_lit() && i == cur);
                if (lit) {
                    ui_panel(c, r.x, r.y - 1, r.w, 18, UI_CUT_TL | UI_CUT_BR, 5, 0xBFE2EAu, IN_HL, 255);
                    ui_fill(c, r.x + 3, r.y + 2, 3, 12, IN_HL, 255);
                }
                ui_text(c, r.x + 10, r.y - (lit ? 1 : 0) + ui_text_y(18, UI_FONT_LARGE),
                        lab[0] != '\0' ? lab : (i == 0 ? "YES" : "NO"), 0x42424Au, 0xADADB5u, UI_FONT_LARGE, 1);
                if (st & EIDOS_UI_CLICK) {
                    if (i == cur) {
                        eidos_ui_press(BW_KEY_A, 2);
                    } else {
                        eidos_ui_press(i == 0 ? BW_KEY_UP : BW_KEY_DOWN, 2);
                        pend_press(BW_KEY_A, 6);
                    }
                }
            }
        }
    }
    pend_pump();
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 2, keys);
}

static void lobby_reset(void)
{
}

static const EidosUiRecipe recipe_guru2_lobby = {
    "union_spin_lobby", lobby_match, lobby_run, lobby_reset, 0
};

/* ------------------------------------------------------------------ */
/* Link trades (the Union Room)                                        */
/* ------------------------------------------------------------------ */

/*
 * The Union Room's link trade (overlay 119, ov119/pokemontrade_proc.c and
 * pokemontrade_nego.c; mock-up "union_trade"): one window over the
 * blurred room.  The left column holds the two offers (up to three
 * Pokemon each) and the four feelings the game sends; the right column is
 * the box being browsed, the party first and then the boxes, in the order
 * the game's strip of icons runs.  The game's words are the system box
 * under the window and its lists (SHOW / CANCEL, YES / NO, TRADE /
 * SUMMARY / CANCEL) are the game's own, drawn as lists.
 *
 * The game does the work: a click on a Pokemon taps its icon on the
 * game's strip (the stylus picks it and opens its summary), a feeling
 * taps the game's feeling, the arrows are L and R (the strip snaps to the
 * next box), START is the game's button that ends the choice.  When the
 * game shows a Pokemon's summary the right column shows it; when both
 * offers are made the right column shows the Pokemon under the game's
 * cursor and a click on an offer taps that offer.  The trade itself (the
 * game's 3D scene) is the world, with the two Pokemon on a card.
 *
 * TradeWork (ov119/pokemontrade_types.h): stepFunc 0x5EC, monBuf[2]
 * 0x5DC, partyBoxId 0x630 (the number of boxes: the party's index),
 * gridOriginX 0x63C (the strip's length), taskMenu 0x64C, messageWin
 * 0x71C, boxManager 0x828 (the box save), playerStatus 0x834 (the
 * partner), partyData 0x838, cursorSlot 0xF74 (a column of the strip),
 * cursorColumn 0xF78 (its row), peerSelectedSlot 0xF94, peerSelectedBox
 * 0xF98, offerSlot[2][3] 0xFA4, offerBox[2][3] 0xFBC, offerMon[2][3]
 * 0xFD8 (side 0 the player's, side 1 the partner's), scrollX 0x107C
 * (s16), cursorIndex 0x10A0, records[] 0x11FC (per slot: u16 species,
 * u8 form, u8 sex; the party first, then 30 per box).
 */
#define UT_STEP        0x5ECu
#define UT_MONBUF      0x5DCu
#define UT_PARTY_BOX   0x630u
#define UT_ORIGIN      0x63Cu
#define UT_TASKMENU    0x64Cu
#define UT_MSGWIN      0x71Cu
#define UT_BOXES       0x828u
#define UT_PEER        0x834u
#define UT_PARTY       0x838u
#define UT_CUR_SLOT    0xF74u
#define UT_CUR_ROW     0xF78u
#define UT_SEL_SLOT    0xF94u
#define UT_SEL_BOX     0xF98u
#define UT_OFFER_SLOT  0xFA4u
#define UT_OFFER_BOX   0xFBCu
#define UT_OFFER_MON   0xFD8u
#define UT_SCROLL      0x107Cu
#define UT_CUR_INDEX   0x10A0u
#define UT_RECORDS     0x11FCu

enum { UT_SCENE, UT_BROWSE, UT_SUMMARY, UT_CHOOSE };

/* The game's steps and the screen each one shows; a step not listed is
 * the trade's own scene (the 3D demo, the questions after it). */
#define UT_STEPS(X) \
    X(PROC_StepRestartNetSession, UT_BROWSE) X(PROC_StepAfterWipeLoadBox, UT_BROWSE) \
    X(PROC_StepWaitNetSync22, UT_BROWSE) X(PROC_StepLoadBoxIconGfx, UT_BROWSE) \
    X(PROC_StepSendBoxInfo, UT_BROWSE) X(PROC_StepBuildMarkingTable, UT_BROWSE) \
    X(PROC_StepBuildTradeRecord, UT_BROWSE) X(PROC_StepSendTradeData, UT_BROWSE) \
    X(PROC_StepWaitSyncThenWipe, UT_BROWSE) X(PROC_StepBeginWipeToRestore, UT_BROWSE) \
    X(PROC_StepWaitThenRestoreScene, UT_BROWSE) X(PROC_StepRestoreBrowseScene, UT_BROWSE) \
    X(PROC_StepRebuildTradeScene, UT_BROWSE) X(PROC_StepWaitKeyThenBrowse, UT_BROWSE) \
    X(PROC_StepBrowseGrid, UT_BROWSE) X(PROC_StepTouchbarAction, UT_BROWSE) \
    X(PROC_StepWaitIconFlight, UT_BROWSE) X(PROC_StepWaitBrowseRelease, UT_BROWSE) \
    X(PROC_StepClearPeerSelection, UT_BROWSE) X(PROC_StepRebuildBrowseScene, UT_BROWSE) \
    X(PROC_StepAddSelectedOffer, UT_BROWSE) X(PROC_StepSendAddOffer, UT_BROWSE) \
    X(PROC_StepApplyAddOffer, UT_BROWSE) X(PROC_StepSendApplyAddOffer, UT_BROWSE) \
    X(PROC_StepWaitKeyAfterMessage, UT_BROWSE) X(PROC_ShowCancelMessage, UT_BROWSE) \
    X(PROC_ShowSelectMessage, UT_BROWSE) X(PROC_StepConfirmSendOffer, UT_BROWSE) \
    X(PROC_StepSendOfferConfirm, UT_BROWSE) X(PROC_HideTouchbarButtons, UT_BROWSE) \
    X(PROC_StepWaitTouchReleasePeer, UT_BROWSE) X(PROC_StepResetForNewRound, UT_BROWSE) \
    X(PROC_StepResetGrid, UT_BROWSE) X(PROC_StepEnterBrowsing, UT_BROWSE) \
    X(PROC_StepBeginBrowse, UT_BROWSE) \
    X(NEGO_StepSendOfferRemoval, UT_BROWSE) X(NEGO_StepWaitTouchRelease, UT_BROWSE) \
    X(NEGO_StepShowCancelMessage, UT_BROWSE) X(NEGO_StepOpenCancelMenu, UT_BROWSE) \
    X(NEGO_StepCancelMenuChoice, UT_BROWSE) X(NEGO_StepBeginFinalWait, UT_BROWSE) \
    X(NEGO_StepWaitFinalMessage, UT_BROWSE) X(NEGO_StepOpenFinalAgreeMenu, UT_BROWSE) \
    X(NEGO_StepFinalAgreeMenuChoice, UT_BROWSE) X(NEGO_StepBeginFinalReady, UT_BROWSE) \
    X(NEGO_StepSendFinalReady, UT_BROWSE) X(NEGO_StepSendFinalAgree, UT_BROWSE) \
    X(NEGO_StepResolveFinalAgree, UT_BROWSE) X(NEGO_StepSendReady, UT_BROWSE) \
    X(NEGO_StepWaitReadySync, UT_BROWSE) \
    X(PROC_StepOpenPeerSummary, UT_SUMMARY) X(PROC_StepPeerSummaryMenu, UT_SUMMARY) \
    X(PROC_ShowPeerSummary, UT_SUMMARY) \
    X(NEGO_StepWipeToBrowse, UT_CHOOSE) X(NEGO_StepWaitWipeToBrowse, UT_CHOOSE) \
    X(NEGO_StepBuildBrowseScene, UT_CHOOSE) X(NEGO_StepWaitWipeBrowse, UT_CHOOSE) \
    X(NEGO_StepShowNoOfferMessage, UT_CHOOSE) X(NEGO_StepWaitInputAfterMessage, UT_CHOOSE) \
    X(NEGO_StepBrowseOffersMain, UT_CHOOSE) X(NEGO_StepWaitSelectionDelay, UT_CHOOSE) \
    X(NEGO_StepBrowseOffers, UT_CHOOSE) X(NEGO_StepOpenSummaryFromBrowse, UT_CHOOSE) \
    X(NEGO_StepReturnFromSummary, UT_CHOOSE) \
    X(NEGO_StepSendOfferIndex, UT_CHOOSE) X(NEGO_StepSendOfferMon, UT_CHOOSE) \
    X(NEGO_StepOpenOfferActionMenu, UT_CHOOSE) X(NEGO_StepOfferActionChoice, UT_CHOOSE) \
    X(NEGO_StepBeginConfirm, UT_CHOOSE) X(NEGO_StepSendConfirm, UT_CHOOSE) \
    X(NEGO_StepResolveConfirm, UT_CHOOSE) X(NEGO_StepOpenConfirmMenu, UT_CHOOSE) \
    X(NEGO_StepConfirmMenuChoice, UT_CHOOSE) X(NEGO_StepShowWaitMessageA, UT_CHOOSE) \
    X(NEGO_StepShowWaitMessageB, UT_CHOOSE) X(NEGO_StepWaitMessageThenWipe, UT_CHOOSE) \
    X(NEGO_StepRebuildSceneAfterWipe, UT_CHOOSE) X(NEGO_StepOpenQuitMenu, UT_CHOOSE) \
    X(NEGO_StepQuitMenuChoice, UT_CHOOSE) X(NEGO_StepWaitReplyKey, UT_CHOOSE) \
    X(NEGO_StepResolveReplies, UT_CHOOSE) X(NEGO_StepSendReply, UT_CHOOSE) \
    X(NEGO_StepWaitReplySync, UT_CHOOSE) X(NEGO_StepSendCancelReply, UT_CHOOSE) \
    X(NEGO_StepWaitCancelSync, UT_CHOOSE) X(NEGO_StepSendDeclineConfirm, UT_CHOOSE) \
    X(NEGO_StepBeginDeclineConfirm, UT_CHOOSE) X(NEGO_StepDeclineMenuChoice, UT_CHOOSE) \
    X(NEGO_StepOpenDeclineMenu, UT_CHOOSE)

#define UT_DECL(n, ph) extern void ov119_POKETRADE_##n(void *p) MN_WEAK;
UT_STEPS(UT_DECL)
#undef UT_DECL
#define UT_FN(n, ph) ov119_POKETRADE_##n,
static void (*const ut_fns[])(void *) = { UT_STEPS(UT_FN) };
#undef UT_FN
#define UT_PH(n, ph) ph,
static const u8 ut_phases[] = { UT_STEPS(UT_PH) };
#undef UT_PH

extern u32   POKE_TOOL_GetBoxValue(void *bp, u32 param, u32 arg) MN_WEAK;
extern void *BOX_SAVEDATA_GetPokemon(void *boxes, u32 box, u32 slot) MN_WEAK;
extern void *BOX_SAVEDATA_GetHeader(void *boxes) MN_WEAK;

static const u8 *ut_work(void)
{
    const u8 *w = (const u8 *)app_work(TRADE_OVERLAY);

    return w != NULL && trade_event_work() == NULL ? w : NULL;
}

static int ut_phase(const u8 *w)
{
    uintptr_t fn = (uintptr_t)rd32(w, UT_STEP);
    u32 i;

    for (i = 0; i < sizeof ut_phases; i++) {
        if (ut_fns[i] != NULL && (uintptr_t)ut_fns[i] == fn) {
            return ut_phases[i];
        }
    }
    return UT_SCENE;
}

static int ut_step_is(const u8 *w, void (*fn)(void *))
{
    return fn != NULL && (uintptr_t)rd32(w, UT_STEP) == (uintptr_t)fn;
}

/* A Pokemon as the window shows it. */
typedef struct UtMon {
    int exists, egg, species, form, gender, level, item, ability;
    int moves[4];
    char nick[40];
} UtMon;

/* Read from a party Pokemon (party != 0: the offers) or a box one. */
static int ut_mon(void *mon, int party, UtMon *m)
{
    u32 (*get)(void *, u32, u32) = party ? POKE_TOOL_GetPartyValue : POKE_TOOL_GetBoxValue;
    u16 name[16];
    int i;

    memset(m, 0, sizeof *m);
    if (mon == NULL || get == NULL || get(mon, 0xA9, 0) == 0) {     /* a species */
        return 0;
    }
    m->exists = 1;
    m->species = (int)get(mon, PARAM_SPECIES, 0);
    m->egg = get(mon, PARAM_IS_EGG, 0) != 0;
    m->form = (int)get(mon, PARAM_FORM, 0);
    m->gender = (int)get(mon, 0x6E, 0);
    m->level = (int)get(mon, PARAM_LEVEL, 0);
    m->item = (int)get(mon, 0x06, 0);
    m->ability = (int)get(mon, 0x0A, 0);
    for (i = 0; i < 4; i++) {
        m->moves[i] = (int)get(mon, 0x36u + (u32)i, 0);
    }
    memset(name, 0xFF, sizeof name);
    (void)get(mon, 0x74, (u32)(uintptr_t)name);
    units_utf8(name, 12, m->nick, (int)sizeof m->nick);
    return 1;
}

/* The Pokemon in a slot of a box (box == partyBoxId: the party). */
static int ut_slot_mon(const u8 *w, int box, int slot, UtMon *m)
{
    void *mon = NULL;

    if (box < 0 || slot < 0) {
        return ut_mon(NULL, 0, m);
    }
    if (box == (int)rd32(w, UT_PARTY_BOX)) {
        void *party = rdp(w, UT_PARTY);

        if (party != NULL && POKEPARTY_GetCount != NULL && POKEPARTY_GetMember != NULL
            && slot < (int)POKEPARTY_GetCount(party)) {
            return ut_mon(POKEPARTY_GetMember(party, (u32)slot), 1, m);
        }
        return ut_mon(NULL, 0, m);
    }
    if (BOX_SAVEDATA_GetPokemon != NULL && slot < 30) {
        mon = BOX_SAVEDATA_GetPokemon(rdp(w, UT_BOXES), (u32)box, (u32)slot);
    }
    return ut_mon(mon, 0, m);
}

static const UiImage *ut_icon(const UtMon *m, u32 frame)
{
    if (m->egg) {
        return ui_mon_icon(m->species == 490 ? 651 : 650, 0, (frame / 16) & 1);
    }
    return ui_mon_icon((u16)m->species, (u8)m->form, (frame / 16) & 1);
}

/* The box the strip shows most of (its middle): -1 the party, else the
 * box (ov119 POKETRADE_2D_GetScrollColumn: the party's two columns, then
 * 160 pixels a box). */
static int ut_page(const u8 *w)
{
    int origin = (int)rd32(w, UT_ORIGIN), x = (s16)rd16(w, UT_SCROLL) + 128;

    if (origin > 0) {
        x %= origin;
        if (x < 0) {
            x += origin;
        }
    }
    return x < 96 ? -1 : (x - 96) / 160;
}

/* Where the game draws a slot's icon on its strip (bottom screen pixels,
 * the icon's middle; ov119 POKETRADE_2D_UpdateBoxIconPositions). */
static int ut_slot_x(const u8 *w, int box, int slot)
{
    int origin = (int)rd32(w, UT_ORIGIN), x;

    if (box < 0) {
        x = (slot % 2) == 0 ? 0x1C : 0x3C;
    } else {
        x = box * 160 + (slot % 6) * 24 + 0x14 + 0x60;
    }
    x -= (s16)rd16(w, UT_SCROLL);
    if (origin > 0) {
        while (x < -16) {
            x += origin;
        }
        while (x >= origin - 16) {
            x -= origin;
        }
    }
    return x;
}

/* The four feelings, as the game draws them on its screen (kept once
 * seen: the game hides them under its lists). */
static u8 g_ut_feel_px[4][24 * 24 * 4];
static UiImage g_ut_feel[4];

static void ut_grab_feelings(const EidosUiFrame *in)
{
    const u8 *o = in->objs_rgba[1];
    int i, x, y, n;

    eidos_ui_want_objs(1);
    if (o == NULL) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (g_ut_feel[i].rgba != NULL) {
            continue;
        }
        for (n = 0, y = 0; y < 24; y++) {
            for (x = 0; x < 24; x++) {
                const u8 *s = o + ((size_t)(144 + y) * 256 + (size_t)(8 + i * 24 + x)) * 4;

                memcpy(&g_ut_feel_px[i][(y * 24 + x) * 4], s, 4);
                n += s[3] != 0;
            }
        }
        if (n > 40) {
            g_ut_feel[i].w = 24;
            g_ut_feel[i].h = 24;
            g_ut_feel[i].rgba = g_ut_feel_px[i];
        }
    }
}

/* An offer's tile: the Pokemon's icon and level, or its number. */
static int ut_tile(EidosUiCtx *c, int x, int y, void *mon, int n, int lit, u32 frame)
{
    UtMon m;
    EidosUiRect r;
    int st, full = ut_mon(mon, 1, &m);

    r.x = x; r.y = y; r.w = 48; r.h = 38;
    st = eidos_ui_hit(r);
    lit = lit || (full && (st & EIDOS_UI_HOVER));
    ui_panel(c, x, y, 48, 38, UI_CUT_TL | UI_CUT_BR, 5, full ? 0x05343Eu : UI_BODY,
             lit ? UI_WHITE : full ? IN_HL : UI_EDGE, 255);
    if (full) {
        const UiImage *ic = ut_icon(&m, frame);
        char lv[16];

        if (ic != NULL) {
            ui_image(c, x + 8, y - 4, ic, 1);
        }
        if (!m.egg) {
            snprintf(lv, sizeof lv, "%s%d", ui_tr("Lv."), m.level);
            ui_text(c, x + (48 - ui_text_w(lv, UI_FONT_SMALL)) / 2, y + 26, lv, UI_WHITE, IN_SHADOW,
                    UI_FONT_SMALL, 1);
        }
    } else {
        char d[4];

        snprintf(d, sizeof d, "%d", n + 1);
        ui_text(c, x + (48 - ui_text_w(d, UI_FONT_LARGE)) / 2, y + 11, d, UI_EDGE2, IN_SHADOW, UI_FONT_LARGE, 1);
    }
    return st;
}

/* A Pokemon's card: its picture, name, level, types, ability, item and
 * moves; menu_room keeps that much of the bottom right for the game's
 * list. */
static void ut_card(EidosUiCtx *c, int x, int y, int w, int h, const UtMon *m, int menu_room, u32 frame)
{
    int ix = x + 6, iy = y + 5, i, t1, t2;
    char lv[16];

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 255);
    if (!m->exists) {
        return;
    }
    {
        int nx = ui_text(c, ix, iy, m->egg ? "Egg" : m->nick, UI_WHITE, IN_SHADOW, UI_FONT_LARGE, 1);

        if (!m->egg) {
            ui_gender(c, nx + 3, iy, m->gender, UI_FONT_LARGE);
            snprintf(lv, sizeof lv, "%s %d", ui_tr("Lv."), m->level);
            ui_text(c, x + w - 6 - ui_text_w(lv, UI_FONT_SMALL), iy + 2, lv, UI_LIGHT, IN_SHADOW, UI_FONT_SMALL, 1);
        }
    }
    iy += 17;
    ui_panel(c, ix, iy, 64, 64, UI_CUT_TL | UI_CUT_BR, 5, 0x0B1E34u, UI_EDGE, 255);
    if (m->egg) {
        const UiImage *ic = ut_icon(m, frame);

        if (ic != NULL) {
            ui_image(c, ix + 16, iy + 16, ic, 1);
        }
        return;
    }
    {
        EidosUiRect saved;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, ix + 1, iy + 1, 62, 62);
        ui_mon_front_draw(c, (u16)m->species, 0, frame, ix + 1, iy + 1, 62, 62);
        ui_pop(c, 0, 0, &saved);
    }
    {
        int tx = ix + 70, tw = x + w - 6 - tx;

        ui_species_types(m->species, &t1, &t2);
        ui_type_tag(c, tx, iy, 46, t1);
        if (t2 != t1) {
            ui_type_tag(c, tx + 50, iy, 46, t2);
        }
        ui_info_row(c, tx, iy + 18, tw, ui_tr("Ability"), ui_game_text(2, 182, (u32)m->ability));
        ui_info_row(c, tx, iy + 36, tw, ui_tr("Item"),
                    m->item > 0 ? ui_game_text(2, 54, (u32)m->item) : ui_tr("None"));
    }
    iy += menu_room > 0 ? 66 : 69;
    /* the moves: two by two, or one column beside the game's list */
    for (i = 0; i < 4 && iy + 12 <= y + h - 3; i++) {
        int one = menu_room > 0, cwid = one ? w - 12 - menu_room - 4 : (w - 12) / 2 - 1;
        int cx = one ? ix : ix + (i % 2) * (cwid + 2);
        const char *mv = "-----";

        if (m->moves[i] > 0) {
            const char *game = ui_game_text(2, 203, (u32)m->moves[i]);
            const char *modern = ui_move_name_modern(m->moves[i], game);

            mv = modern != NULL ? modern : game;
        }
        ui_panel(c, cx, iy, cwid, 12, UI_CUT_TL | UI_CUT_BR, 2, UI_BODY, UI_EDGE, 255);
        ui_text(c, cx + 4, iy + ui_text_y(12, UI_FONT_SMALL), mv,
                m->moves[i] > 0 ? UI_WHITE : UI_EDGE2, IN_SHADOW, UI_FONT_SMALL, 1);
        if (one || i % 2 == 1) {
            iy += one ? 13 : 14;
        }
    }
}

/* The box pane: the page's name with its arrows, and its Pokemon. */
static void ut_box(EidosUiCtx *c, const u8 *w, int gx, int gy, u32 frame)
{
    int page = ut_page(w), i, nslot = page < 0 ? 6 : 30, cols = page < 0 ? 2 : 6;
    int box = page < 0 ? (int)rd32(w, UT_PARTY_BOX) : page;
    int cur_slot = (int)rd32(w, UT_CUR_SLOT), cur_row = (int)rd32(w, UT_CUR_ROW);
    char name[48];
    EidosUiRect r;

    name[0] = '\0';
    if (page < 0) {
        snprintf(name, sizeof name, "%s", ui_tr("PARTY"));
    } else if (BOX_SAVEDATA_GetHeader != NULL) {
        const u8 *h = (const u8 *)BOX_SAVEDATA_GetHeader(rdp(w, UT_BOXES));
        u16 u[21];
        int k;

        if (h != NULL) {
            for (k = 0; k < 20; k++) {
                u[k] = (u16)rd16(h, 4u + (u32)page * 40u + 2u * (u32)k);
            }
            u[20] = 0xFFFF;
            units_utf8(u, 20, name, (int)sizeof name);
        }
    }
    ui_panel(c, gx, gy, 180, 14, UI_CUT_TL | UI_CUT_BR, 3, UI_BODY2, UI_EDGE, 255);
    ui_text(c, gx + (180 - ui_text_w(name, UI_FONT_SMALL)) / 2, gy + 2, name, UI_WHITE, IN_SHADOW, UI_FONT_SMALL, 1);
    for (i = 0; i < 2; i++) {
        int st, ax = i == 0 ? gx : gx + 180 - 18, k;

        r.x = ax; r.y = gy; r.w = 18; r.h = 14;
        st = eidos_ui_hit(r);
        for (k = 0; k < 4; k++) {
            int tx = i == 0 ? ax + 7 + k : ax + 10 - k;

            ui_fill(c, tx, gy + 7 - k, 1, 2 * k + 1, (st & EIDOS_UI_HOVER) ? UI_WHITE : UI_CYAN, 255);
        }
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_press(i == 0 ? BW_KEY_L : BW_KEY_R, 2);
        }
    }
    /* the party in two columns in the middle, as the game lays it out */
    for (i = 0; i < nslot; i++) {
        int col = i % cols, row = i / cols, cx = gx + (page < 0 ? 60 : 0) + col * 30, cy = gy + 16 + row * 26, st, lit, off = 0, k;
        const u8 *rec = w + UT_RECORDS + 4u * (u32)(page < 0 ? i : page * 30 + 6 + i);
        const UiImage *ic;
        int sp = (int)rd16(rec, 0);

        if (sp == 0) {
            continue;
        }
        r.x = cx; r.y = cy; r.w = 30; r.h = 26;
        st = eidos_ui_hit(r);
        for (k = 0; k < 3; k++) {
            off |= (int)rd32(w, UT_OFFER_SLOT + 4u * (u32)k) == i && (int)rd32(w, UT_OFFER_BOX + 4u * (u32)k) == box;
        }
        /* the game's own cursor (keys): a column of the strip and its row */
        lit = eidos_ui_keys_lit() && UI_GetInputMode != NULL && UI_GetInputMode() == 0
              && ((page < 0 && cur_slot < 2 && cur_slot + cur_row * 2 == i)
                  || (page >= 0 && cur_slot >= 2 && (cur_slot - 2) / 6 == page
                      && (cur_slot - 2) % 6 == col && cur_row == row));
        if (off || lit || (st & EIDOS_UI_HOVER)) {
            ui_panel(c, cx + 1, cy + 1, 28, 24, UI_CUT_TL | UI_CUT_BR, 4, off ? 0x05343Eu : UI_BODY2,
                     (lit || (st & EIDOS_UI_HOVER)) ? UI_WHITE : IN_HL, 255);
        }
        ic = sp == 650 ? ui_mon_icon(650, 0, 0)
                       : ui_mon_icon((u16)sp, rec[2], (lit || (st & EIDOS_UI_HOVER)) ? (int)((frame / 8) & 1) : 0);
        if (ic != NULL) {
            ui_image(c, cx - 1, cy - 6, ic, 1);
        }
        if (st & EIDOS_UI_CLICK) {
            int dx = ut_slot_x(w, page, i), dy = 0x20 + (page < 0 ? i / 2 : i / 6) * 24;

            if (dx >= 6 && dx < 250) {
                eidos_ui_tap(dx, dy, 3);
            }
        }
    }
}

static int ut_match(void)
{
    const u8 *w = ut_work();
    static u32 last;

    if (debug_on() && (w != NULL ? rd32(w, UT_STEP) : 1u) != last) {
        last = w != NULL ? rd32(w, UT_STEP) : 1u;
        fprintf(stderr, "eidos: ut step %08X at %u\n", (unsigned)last, (unsigned)bw_input_frame_number());
    }
    return w != NULL;
}

extern void ov119_POKETRADE_PROC_StepMenuChoice(void *p) MN_WEAK;

/* The game's list: the trade frees its words once the list has drawn
 * them (ov119 POKETRADE_SAVE_OpenTaskMenuAt), so its labels are read from
 * its bank (a/0/0/2 member 159; 157 on a Japanese cartridge, not read
 * here) by the entries the step opened. */
static void ut_menu(EidosUiCtx *c, const u8 *w, void *tm, int right, int bottom)
{
    int ids[3] = { 24, 25, -1 }, i, n = ui_taskmenu_count(tm);
    const char *labels[3];

    if (n == 3) {
        ids[0] = 4; ids[1] = 3; ids[2] = 5;             /* TRADE, SUMMARY, CANCEL */
    } else if (ut_step_is(w, ov119_POKETRADE_PROC_StepPeerSummaryMenu)) {
        ids[0] = 2; ids[1] = 5;                         /* SHOW, CANCEL */
    } else if (ut_step_is(w, ov119_POKETRADE_PROC_StepMenuChoice)) {
        ids[0] = 0; ids[1] = 5;
    } else if (ut_step_is(w, ov119_POKETRADE_NEGO_StepQuitMenuChoice)) {
        ids[0] = 3; ids[1] = 5;
    } else if (ut_step_is(w, ov119_POKETRADE_NEGO_StepConfirmMenuChoice)) {
        ids[0] = 4; ids[1] = 5;
    }
    for (i = 0; i < 3; i++) {
        labels[i] = ids[i] >= 0 ? ui_game_text(2, 159, (u32)ids[i]) : "";
    }
    ui_taskmenu_labels(c, tm, right, bottom, UI_CHOICE_SYSTEM, labels);
}

/* The trade's own scene (the 3D demo and what follows it): the world, a
 * card with the two Pokemon, the game's words and lists. */
static void ut_scene(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, const u8 *w)
{
    static const char *const hints[] = { "A", "Next" };
    static const u32 keys[] = { BW_KEY_A };
    int cw = c->canvas->w, ch = c->canvas->h, pw = 240, px = (cw - pw) / 2, py = 6, i, by = ch - 17 - 6;
    const u8 *win = (const u8 *)rdp(w, UT_MSGWIN);
    void *tm = rdp(w, UT_TASKMENU);
    EidosUiRect all;

    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 0;
    if (win != NULL && ui_bmpwin_bmp(win) != NULL && g2d_hw_hide_main_bgs != NULL && win[2] < 4) {
        g2d_hw_hide_main_bgs(1u << win[2]);
    }
    if (rdp(w, UT_MONBUF) != NULL && rdp(w, UT_MONBUF + 4u) != NULL && POKE_TOOL_GetPartyValue != NULL
        && POKE_TOOL_GetPartyValue(rdp(w, UT_MONBUF), 0xA9, 0) != 0) {
        ui_panel(c, px, py, pw, 58, UI_CUT_TL | UI_CUT_BR, 9, IN_MBODY, IN_MEDGE, 235);
        trade_mon(c, rdp(w, UT_MONBUF + 4u), px + 10, py + 14, in->frame);
        trade_mon(c, rdp(w, UT_MONBUF), px + pw / 2 + 12, py + 14, in->frame);
        for (i = 0; i < 5; i++) {
            ui_fill(c, px + pw / 2 - 8 + i, py + 25 - (4 - i), 1, 2 * (4 - i) + 1, IN_HL, 255);
            ui_fill(c, px + pw / 2 + 4 - i, py + 35 - (4 - i), 1, 2 * (4 - i) + 1, IN_HL, 255);
        }
    }
    if (win != NULL && ui_bmpwin_bmp(win) != NULL && ui_textbox_h(win) > 0) {
        int bw = cw - 16 < 300 ? cw - 16 : 300;

        by -= ui_textbox_h(win);
        ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, (cw - bw) / 2, by, bw, win, -1, -1);
        if (tm != NULL) {
            ut_menu(c, w, tm, (cw + bw) / 2, by - 3);
        }
    } else if (tm != NULL) {
        ut_menu(c, w, tm, cw - 8, by);
    }
    all.x = 0; all.y = 0; all.w = cw; all.h = ch - ui_bar_h(c);
    if (tm == NULL && (eidos_ui_hit(all) & EIDOS_UI_CLICK)) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    ui_hints(c, ch - ui_bar_h(c), cw, hints, 1, keys);
}

static void ut_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const browse_hints[] = { "A", "Choose", "B", "Back", "L R", "Box", "START", "Done" };
    static const u32 browse_keys[] = { BW_KEY_A, BW_KEY_B, BW_KEY_R, BW_KEY_START };
    static const char *const pick_hints[] = { "A", "Choose", "B", "Back" };
    static const u32 pick_keys[] = { BW_KEY_A, BW_KEY_B };
    const u8 *w = ut_work(), *msg;
    void *tm;
    int cw = c->canvas->w, ch = c->canvas->h, mw = 370, mh = IN_HEAD + 154, mx, my, bh = 0, phase, i, side;
    int x0, y0, gx, offered = 0, cur_index;
    char line[64], who[32];

    if (w == NULL) {
        return;
    }
    phase = ut_phase(w);
    if (phase == UT_SCENE) {
        ut_scene(c, view, in, w);
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    msg = (const u8 *)rdp(w, UT_MSGWIN);
    tm = rdp(w, UT_TASKMENU);
    if (msg != NULL && win_bmp_of(msg) != NULL) {
        bh = ui_textbox_h(msg);
    }
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh - (bh > 0 ? bh + 6 : 0)) / 2;
    if (my < 4) {
        my = 4;
    }
    who[0] = '\0';
    if (rdp(w, UT_PEER) != NULL && MYSTATUS_GetName != NULL) {
        units_utf8(MYSTATUS_GetName(rdp(w, UT_PEER)), 8, who, (int)sizeof who);
    }
    in_window(c, mx, my, mw, mh, NULL);
    ui_text(c, mx + 12, my + 7, ui_tr("TRADE"), IN_TITLE, IN_SHADOW, UI_FONT_SMALL, 1);
    ui_text(c, mx + mw - 12 - ui_text_w(who, UI_FONT_SMALL), my + 7, who, UI_LIGHT, IN_SHADOW, UI_FONT_SMALL, 1);
    x0 = mx + 12;
    y0 = my + IN_HEAD;
    gx = x0 + 156 + 10;
    cur_index = phase == UT_CHOOSE ? (int)rd32(w, UT_CUR_INDEX) : -1;
    /* the offers: the player's, then the partner's */
    for (side = 0; side < 2; side++) {
        int ly = y0 + side * 52;

        snprintf(line, sizeof line, "%s", ui_tr(side == 0 ? "YOUR OFFER" : "PARTNER'S OFFER"));
        ui_text(c, x0, ly, line, side == 0 ? IN_TITLE : UI_LIGHT, IN_SHADOW, UI_FONT_SMALL, 1);
        for (i = 0; i < 3; i++) {
            u32 o = (u32)(side * 3 + i) * 4u;
            void *mon = (int)rd32(w, UT_OFFER_SLOT + o) != -1 ? rdp(w, UT_OFFER_MON + o) : NULL;
            /* the game's list of the offers: the partner's 0..2, the
             * player's 3..5 (ov119 gBrowseOffersHitArea) */
            int idx = side == 0 ? 3 + i : i;
            int st = ut_tile(c, x0 + i * 52, ly + 10, mon, i, cur_index == idx, in->frame);

            offered |= side == 0 && mon != NULL;
            if (phase == UT_CHOOSE && mon != NULL && (st & EIDOS_UI_CLICK)) {
                eidos_ui_tap(idx < 3 ? 64 : 192, 46 + 48 * (idx % 3), 3);
            }
        }
    }
    /* the feelings */
    ui_text(c, x0, y0 + 104, ui_tr("SEND A FEELING"), UI_LIGHT, IN_SHADOW, UI_FONT_SMALL, 1);
    if (ut_step_is(w, ov119_POKETRADE_PROC_StepBrowseGrid)) {
        ut_grab_feelings(in);
    }
    for (i = 0; i < 4; i++) {
        EidosUiRect r;
        int st, fx = x0 + i * 40, fy = y0 + 114;

        r.x = fx; r.y = fy; r.w = 36; r.h = 22;
        st = ut_step_is(w, ov119_POKETRADE_PROC_StepBrowseGrid) ? eidos_ui_hit(r) : 0;
        ui_panel(c, fx, fy, 36, 22, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY, (st & EIDOS_UI_HOVER) ? UI_WHITE : UI_EDGE, 255);
        if (g_ut_feel[i].rgba != NULL) {
            ui_image(c, fx + 6, fy - 1, &g_ut_feel[i], 1);
        }
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_tap(8 + i * 24 + 12, 156, 3);
        }
    }
    /* the right column: the box, or the Pokemon the game shows */
    if (phase == UT_BROWSE) {
        ut_box(c, w, gx, y0, in->frame);
    } else {
        UtMon m;

        if (phase == UT_SUMMARY) {
            ut_slot_mon(w, (int)rd32(w, UT_SEL_BOX), (int)rd32(w, UT_SEL_SLOT), &m);
        } else {
            int idx = cur_index >= 0 && cur_index < 6 ? cur_index : 0, row = idx / 3, col = idx % 3;
            u32 o = (u32)((1 - row) * 3 + col) * 4u;

            ut_mon((int)rd32(w, UT_OFFER_SLOT + o) != -1 ? rdp(w, UT_OFFER_MON + o) : NULL, 1, &m);
        }
        ut_card(c, gx, y0, 180, mh - IN_HEAD - 10, &m, tm != NULL ? 76 : 0, in->frame);
    }
    /* the game's words and lists */
    if (bh > 0) {
        int by = my + mh + 6;

        ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, mx, by, mw, msg, -1, -1);
        if (tm != NULL) {
            ut_menu(c, w, tm, mx + mw, by - 3);
        }
    } else if (tm != NULL) {
        ut_menu(c, w, tm, gx + 180 - 2, my + mh - 12);
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    if (phase == UT_BROWSE) {
        ui_hints(c, ch - ui_bar_h(c), cw, browse_hints, offered ? 4 : 3, browse_keys);
    } else {
        ui_hints(c, ch - ui_bar_h(c), cw, pick_hints, 2, pick_keys);
    }
}

static void ut_reset(void)
{
}

static const EidosUiRecipe recipe_union_trade = {
    "union_trade", ut_match, ut_run, ut_reset, 0
};

/* ------------------------------------------------------------------ */
/* A link battle's VS screen                                           */
/* ------------------------------------------------------------------ */

/*
 * The VS screen before a link battle (overlay 213, ov213/comm_btl_demo_*.c:
 * the two trainers' names and Poke Balls, the VS emblem) is drawn by the
 * game on its upper screen.  It is the world the window shows, as high as
 * the window, and each of its rows is carried out to the window's sides in
 * the colour it has near the screen's edge (its dark backdrop, the line
 * across the middle), so it fills the window.
 */
#define VS_OVERLAY 213

/* In a link battle the VS screen runs on the battle record event's own
 * proc system (ov008 EVENT_BATTLE_REC_SAVE_ProcMain: its proc work's
 * procsys 0x00), under a proc of no overlay; the Wi-Fi match pushes it as
 * the proc on top. */
static int ds_main_ptr(const void *p)
{
    return bw_ds_game_ptr(p);
}

static int vs_match(void)
{
    void *gs, *cur, *w, *ps, *inner;

    if (app_work(VS_OVERLAY) != NULL) {
        return 1;
    }
    if (!MN_GAME || !bw_overlay_is_loaded(VS_OVERLAY) || (gs = game_system()) == NULL
        || (cur = current_proc(gs)) == NULL || rd32(cur, 0x04) != 0xFFFFFFFFu) {
        return 0;
    }
    w = rdp(cur, 0x14);
    ps = ds_main_ptr(w) ? rdp(w, 0x00) : NULL;
    inner = ds_main_ptr(ps) ? rdp(ps, 0x08) : NULL;
    return ds_main_ptr(inner) && (int)rd32(inner, 0x04) == VS_OVERLAY && rd32(inner, 0x08) >= 2u;
}

/* A pixel of the screen picture, the 3D's stand-in colour (the GPU draws
 * the 3D after: a bright green) read as the VS screen's own backdrop. */
static u32 vs_px(const u8 *p)
{
    if (p[1] > 140 && p[0] < 140 && p[2] < 140) {
        return 0x000010u;
    }
    return (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
}

static void vs_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    const u8 *scr = in->screen_rgba[0];
    int cw = c->canvas->w, ch = c->canvas->h, dw = ch * 256 / 192, x0 = (cw - dw) / 2, y;

    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    if (scr == NULL || x0 <= 0) {
        return;
    }
    /* each row out to the window's sides in the colour it has a few
     * pixels in from the screen's edge (the outermost columns are not its
     * picture): the dark backdrop, the line across the middle */
    for (y = 0; y < ch; y++) {
        const u8 *row = scr + (size_t)(y * 192 / ch) * 256 * 4;

        ui_fill(c, 0, y, x0 + 4, 1, vs_px(row + 8 * 4), 255);
        ui_fill(c, x0 + dw - 4, y, cw - (x0 + dw - 4), 1, vs_px(row + 247 * 4), 255);
    }
}

static void vs_reset(void)
{
}

static const EidosUiRecipe recipe_vs = {
    "link_vs", vs_match, vs_run, vs_reset, 0
};

const EidosUiRecipe *const ui_recipes_menus[] = { &recipe_union_trade, &recipe_vs, &recipe_options, &recipe_save, &recipe_card,
                                                  &recipe_wireless, &recipe_geonet, &recipe_entree,
                                                  &recipe_survey, &recipe_townmap, &recipe_catch, &recipe_relearn, &recipe_tvt, &recipe_ending, &recipe_demo3d, &recipe_egg, &recipe_trade, &recipe_oekaki, &recipe_guru2,
                                                  &recipe_guru2_lobby };
const u32 ui_recipes_menus_count = 19;
