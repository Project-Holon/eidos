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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "kit.h"
#include "assets.h"
#include "input.h"
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
    int cw = c->canvas->w, ch = c->canvas->h;
    int avail = ch - 17;

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
    m.x = (cw - m.w) / 2;
    m.y = (avail - m.h) / 2;
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
    int y = c->canvas->h - 17, i, x = 6, hit = -1;

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
        r.h = 17;
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
static Bank g_bank[8];
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
    if (g_nbank >= 8) {
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
    int cw = c->canvas->w, ch = c->canvas->h - 17;
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

static void options_reset(void)
{
    g_opt.hover_row = -1;
    g_opt.cool = 0;
    g_opt.target = -1;
    g_opt.last_page = 0;
    pend_clear();
}

/* Which page the port's overlay patch shows: the choice sprites of the
 * pages it does not use sit off the screen (x 300), so the count of sprites
 * on the screen tells the page: 11 the game's, 10 DISPLAY, 1 SOUND, 2
 * EXTRAS. */
static int options_page(const void *work)
{
    int i, on = 0, known = 0;

    if (CLACT_GetActorSurfacePosAxis == NULL) {
        return g_opt.last_page;
    }
    for (i = 0; i < 11; i++) {
        const void *a = rdp(work, OW_LIST + OL_ACTORS + 4u * (u32)i);

        if (a == NULL) {
            continue;
        }
        known++;
        if (CLACT_GetActorSurfacePosAxis(a, 0, 0) < 256) {
            on++;
        }
    }
    if (known < 11) {
        return g_opt.last_page;
    }
    switch (on) {
    case 11: return PAGE_GAME;
    case 10: return PAGE_DISPLAY;
    case 1:  return PAGE_SOUND;
    case 2:  return PAGE_EXTRAS;
    }
    return g_opt.last_page;
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
    compact = c->canvas->h - 17 - 6 < 193;
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
        box_text(c, m.x + 8, ry, rw, OPT_ROW_H, label, UI_WHITE, sel ? 0x003C50 : (s32)UI_GREY,
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
                    UI_WHITE, UI_GREY, 4);
        }
        hr.x = 0; hr.y = 0; hr.w = c->canvas->w; hr.h = c->canvas->h - 17;
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

    /* a tab click steps the page bar's arrows until the page is reached */
    if (g_opt.target >= 0) {
        if (g_opt.target == page || prompt) {
            g_opt.target = -1;
        } else if (in->frame >= g_opt.cool) {
            int fwd = (g_opt.target - page + PAGE_COUNT) % PAGE_COUNT;

            tap(fwd <= 2 ? 232 : 24, scroll_y + 24 * 5 + 12);
            g_opt.cool = in->frame + 10;
        }
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

    compact = c->canvas->h - 17 - 6 < 196;
    m = modal_rect(c, 60, 260, 320, 80, compact ? 162 : 196, 220, compact ? 162 : 196);
    ui_panel(c, m.x, m.y, m.w, m.h, CUT_DEF, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_header(c, m.x + 4, m.y + 4, m.w - 8, "SAVE", NULL, NULL, UI_SKY);

    x = m.x + 10;
    w = m.w - 20;
    y = m.y + 29;
    /* the place and the date */
    ui_text(c, x, y + text_cy(15, UI_FONT_LARGE), place_name(gs, place, (int)sizeof place),
            UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
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
        int lines = wrapped(c, x, y + 1, w, 15, UI_FONT_LARGE, msg, UI_WHITE, UI_GREY, 3);

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
        int lit = ((uintptr_t)rw >= 0x02000000u && (uintptr_t)rw < 0x02400000u) ? (int)rd16(rw, 0x124) : 0;
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
                snprintf(buf, sizeof buf, "Last saved %u/%u/20%02u  %02u:%02u",
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
    uintptr_t a = (uintptr_t)p;

    return a >= 0x02000000u && a < 0x02400000u && (a & 3u) == 0u;
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

    compact = c->canvas->h - 17 - 6 < 190;
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

const EidosUiRecipe *const ui_recipes_menus[] = { &recipe_options, &recipe_save, &recipe_card };
const u32 ui_recipes_menus_count = 3;
