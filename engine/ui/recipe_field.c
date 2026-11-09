/*
 * engine/ui/recipe_field.c -- the one-screen recipes for the field: the world, the C-Gear bar, the start menu
 * (engine/ui/ui.h, docs/unified-view-recipes.md).
 *
 * Three recipes, in this priority order:
 *
 *   field-menu   the X menu (field sub-screen mode 1).  The world stays
 *                visible; the party is a column on the left and the six
 *                buttons are a column on the right, drawn with the shape of
 *                the original buttons (a folder tab at the top left, a
 *                diagonal sheen, a black outline, the cyan selected state).
 *                The icons are the game's own: the game still draws the menu
 *                on the bottom screen, and each button's picture is cut out
 *                of it.  A click is a tap at the original button's centre
 *                (docs/unified-view-recipes.md, section 2), so the game does
 *                what it does for a stylus.  The selected button is the
 *                game's own cursor.
 *
 *   field        nothing open (the C-Gear or the no-gear panel below): the
 *                world fills the window, and a thin C-Gear bar runs along
 *                the top: the place name, the time on a slanted tab, the
 *                radio state and the battery.  A click on the bar opens the
 *                menu (the X press); the C-GEAR button shows the game's own
 *                C-Gear as a sheet over the world (lower_sheet), the
 *                pointer as the stylus, until Close, a right click or B.
 *
 *   field-lower  the field's other lower screens (the Union Room, the
 *                Entralink's map, dowsing): the world, and the game's
 *                lower screen as the same sheet.  The save report has its
 *                own recipe (engine/ui/recipe_menus.c).
 *
 * The game's state is read the way sdk/mp.c reads it: the game system's
 * global, then the field map, the field sub-screen and the menu by their
 * 32-bit offsets (docs/unified-view-recipes.md, sections 0.4, 1, 2, 16).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "ui.h"
#include "kit.h"
#include "plat_thread.h"
#include "port.h"
#include "dsmem.h"
#include "input.h"
#include "choice_list.h"
#include "textbox.h"
#include "item_show.h"
#include "ui_lang.h"
#include "overlay.h"

#define WEAK __attribute__((weak))

static int small_button(EidosUiCtx *c, int *x, int y, const char *label, int h, const char *key);
static int small_button_w(const char *label, const char *key);
struct FieldNow;
static int choices_now(const struct FieldNow *f, u8 **list, u8 **win);
static int union_card(EidosUiCtx *c, const EidosUiFrame *in, const struct FieldNow *f);
static int entralink_card(EidosUiCtx *c, const EidosUiFrame *in);
static int power_card(EidosUiCtx *c, const struct FieldNow *f);
static void item_card(EidosUiCtx *c, const struct FieldNow *f, int text_up);
static int item_show_active(void);
static int cutin_other(const struct FieldNow *f);
static void cutin_view(EidosUiView *view);
extern void bw_mod_restart(u32 flags) WEAK;

static void title_restart(void *arg)
{
    (void)arg;
    bw_mod_restart(0);
}

/* The game system's global and the accessors the recipes call. */
extern void *bw_dsref__GameSystemGlobal WEAK;
extern void *GAME_DATA_GetMyParty(const void *gd) WEAK;
extern u32   POKEPARTY_GetCount(const void *party) WEAK;
extern void *POKEPARTY_GetMember(void *party, u32 pos) WEAK;
extern u32   POKE_TOOL_GetPartyValue(void *pp, u32 paramId, u32 arg) WEAK;
extern const u16 *MYSTATUS_GetName(const void *mystatus) WEAK;
extern u32   ZONEDATA_GetPlaceNameId(u32 id) WEAK;
extern void  GF_STANDARD_GetCachedTime(u32 *time) WEAK;

#define P_SPECIES    0x05u
#define P_IS_EGG     0x4Cu
#define P_GENDER     0x6Eu
#define P_FORM       0x6Fu
#define P_NICKNAME   0x74u
#define P_STATUS     0x9Du
#define P_LEVEL      0x9Eu
#define P_HP         0xA0u
#define P_MAX_HP     0xA1u

/* ------------------------------------------------------------------ */
/* Reading the game                                                    */
/* ------------------------------------------------------------------ */

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static void wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }
/* the game's sound effects (arm9/gfl_use.c) */
static u16 rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }
static u8 *rdp(const u8 *p) { u32 v = rd32(p); return (u8 *)(uintptr_t)v; }

typedef struct FieldNow {
    u8 *gs;                 /* the game system                            */
    u8 *fm;                 /* the field map                              */
    u8 *sub;                /* the field sub-screen                       */
    u8 *panel;              /* sub->cgear: the C-Gear or the menu work    */
    u32 mode;               /* the sub-screen's mode                      */
    u32 sstate;             /* the sub-screen's state                     */
    u32 main_state;         /* fieldMap->mainState: 1 running              */
} FieldNow;

static int field_now(FieldNow *f)
{
    memset(f, 0, sizeof(*f));
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return 0;
    }
    f->gs = *(u8 **)bw_dsref__GameSystemGlobal;
    if (f->gs == NULL) {
        return 0;
    }
    f->fm = rdp(f->gs + 0x20);
    if (f->fm == NULL) {
        return 0;
    }
    f->main_state = rd32(f->fm + 0xC0);
    f->sub = rdp(f->fm + 0x24);
    if (f->sub == NULL) {
        return 0;
    }
    f->mode = rd32(f->sub);
    f->sstate = rd32(f->sub + 8);
    f->panel = rdp(f->sub + 0x20);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The game's text                                                     */
/* ------------------------------------------------------------------ */

/* UTF-16 units to UTF-8; stops at 0xFFFF. */
static void units_to_utf8(const u16 *u, int max, char *out, int n)
{
    int i, o = 0;

    for (i = 0; i < max && u[i] != 0xFFFFu && u[i] != 0 && o < n - 4; i++) {
        u32 c = u[i];

        if (c == 0xE000u) {
            c = ' ';
        } else if (c == 0xFFFEu) {                  /* a new line          */
            c = '\n';
        } else if (c == 0xF000u) {                  /* a tag: skip it       */
            int argc = i + 2 < max ? (int)u[i + 2] : 0;

            i += 2 + argc;
            continue;
        } else if (c >= 0xD800u && c < 0xE000u) {
            continue;
        }
        if (c < 0x80u) {
            out[o++] = (char)c;
        } else if (c < 0x800u) {
            out[o++] = (char)(0xC0u | (c >> 6));
            out[o++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            out[o++] = (char)(0xE0u | (c >> 12));
            out[o++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    out[o] = '\0';
}

/* One message of the game's text archive (a/0/0/2), decoded the way
 * tools/assets/text.py does: a per-string key (index + 3) * 0x2983, rotated
 * left by 3 after each unit.  Empty when the data is missing. */
#define MSG_FILES 4
typedef struct MsgCache { u32 member; u8 *raw; u32 size; int tried; } MsgCache;
static MsgCache g_msgs[MSG_FILES];
static BwArcFile g_msg_arc;
static int g_msg_arc_state;

static const MsgCache *msg_file(u32 member)
{
    int i, free_slot = -1;
    long n;
    u8 *buf;

    for (i = 0; i < MSG_FILES; i++) {
        if (g_msgs[i].tried && g_msgs[i].member == member) {
            return g_msgs[i].raw != NULL ? &g_msgs[i] : NULL;
        }
        if (!g_msgs[i].tried && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return NULL;
    }
    g_msgs[free_slot].tried = 1;
    g_msgs[free_slot].member = member;
    if (g_msg_arc_state == 0) {
        g_msg_arc_state = bw_arc_open(&g_msg_arc, "a/0/0/2") == 0 ? 1 : -1;
    }
    if (g_msg_arc_state != 1 || member >= g_msg_arc.count) {
        return NULL;
    }
    n = bw_arc_member_size(&g_msg_arc, member);
    if (n < 16) {
        return NULL;
    }
    buf = (u8 *)malloc((size_t)n);
    if (buf == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(&g_msg_arc, member, buf, (u32)n) != n) {
        free(buf);
        return NULL;
    }
    g_msgs[free_slot].raw = buf;
    g_msgs[free_slot].size = (u32)n;
    return &g_msgs[free_slot];
}

static void msg_text(u32 member, u32 index, char *out, int n)
{
    const MsgCache *m = msg_file(member);
    u16 units[96];
    u32 count, base, off, key, pos;
    int i;

    out[0] = '\0';
    if (m == NULL) {
        return;
    }
    count = rd16(m->raw + 2);
    base = rd32(m->raw + 12);
    if (index >= count || base + 4 + (index + 1) * 8 > m->size) {
        return;
    }
    off = rd32(m->raw + base + 4 + index * 8);
    pos = 16 + off;
    key = ((index + 3) * 0x2983u) & 0xFFFFu;
    for (i = 0; i < 95; i++) {
        u32 w;

        if (pos + 2 > m->size) {
            break;
        }
        w = (u32)rd16(m->raw + pos) ^ key;
        units[i] = (u16)w;
        pos += 2;
        key = ((key << 3) | (key >> 13)) & 0xFFFFu;
        if (w == 0xFFFFu) {
            break;
        }
    }
    units[i < 96 ? i : 95] = 0xFFFFu;
    units_to_utf8(units, 95, out, n);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int g_test_right_frame = -2;

/* B: a right click.  EIDOS_UI_FIELD_RIGHT=<frame> fakes one for scripted
 * checks (the scripted mouse has no right button). */
static int right_click(const EidosUiFrame *in)
{
    if (g_test_right_frame == -2) {
        const char *e = getenv("EIDOS_UI_FIELD_RIGHT");

        g_test_right_frame = e != NULL ? atoi(e) : -1;
    }
    return eidos_ui_right_click() ||
           (g_test_right_frame >= 0 && in->frame == (u32)g_test_right_frame);
}

static int g_log = -1;

static void logf_scene(const char *what)
{
    static const char *last;

    if (g_log < 0) {
        g_log = getenv("EIDOS_UI_FIELD_LOG") != NULL;
    }
    if (g_log && what != last) {
        fprintf(stderr, "eidos: ui field: %s\n", what);
        last = what;
    }
}

/* Hint bar with actions: like ui_hints, but each hint carries an action
 * code the caller handles (a key the game may swallow cannot be pressed
 * from here, so the caller taps instead).  Returns the clicked index, or
 * -1. */
static int hint_bar(EidosUiCtx *c, int y, int w, const char *const *pairs, int n)
{
    int bh, top = ui_bottom_bar(c, y, 17, &bh), lift = bh - 17, i, x = 6 + ui_safe_left(c);
    int clicked = -1;

    /* on the bottom edge, taller on a phone, the keys in its middle; it
     * lies over the world (no bar_top), so opening and closing the field's
     * menu does not move the world */
    y = top + lift / 2;
    ui_fill(c, 0, top, w, bh, UI_BODY2, 235);
    ui_fill(c, 0, top, w, 1, UI_EDGE, 255);
    for (i = 0; i < n; i++) {
        const char *key = pairs[2 * i], *what = pairs[2 * i + 1];
        int kw = ui_keycap_w(key);
        int aw = ui_text_w(what, UI_FONT_SMALL);
        EidosUiRect r;
        int st;

        if (kw < 13) {
            kw = 13;
        }
        r.x = x - 2;
        r.y = top;
        r.w = kw + 4 + aw + 4;
        r.h = bh;
        st = eidos_ui_hit(r);
        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, top + 1, r.w, bh - 2, UI_EDGE, 255);
        }
        if (st & EIDOS_UI_CLICK) {
            clicked = i;
        }
        ui_keycap(c, x, y + 2, key);
        x += kw + 4;
        x = ui_text(c, x, y + 2 + ui_text_y(13, UI_FONT_SMALL), what, UI_LIGHT,
                    UI_EDGE, UI_FONT_SMALL, 1) + 10;
    }
    return clicked;
}

/* ------------------------------------------------------------------ */
/* The menu                                                            */
/* ------------------------------------------------------------------ */

/* The game's slots, as the touch table has them (section 2). */
static const int k_slot_cx[7] = { 64, 192, 64, 192, 64, 192, 236 };
static const int k_slot_cy[7] = { 44, 44, 92, 92, 140, 140, 180 };
/* The buttons' rectangles on the bottom screen (x, y of the top left). */
static const int k_slot_x[6] = { 8, 136, 8, 136, 8, 136 };
static const int k_slot_y[6] = { 24, 24, 72, 72, 120, 120 };

#define ICON_W 30
#define ICON_H 40              /* tall enough for the tallest (the Bag, female) */

typedef struct MenuIcon {
    int ok;
    int w, h;                       /* the cropped picture                  */
    u8 px[ICON_W * ICON_H * 4];     /* RGBA, A 0 around the picture         */
    int lit;                        /* taken while its button was lit       */
} MenuIcon;

static MenuIcon g_icons[8];         /* by button id                         */

/* The button's picture: the game draws it as sprites over the button's
 * plate (a BG), so the bottom screen's OBJ layer alone (objs, from
 * eidos_ui_want_objs) holds just the picture, whatever the plate looks
 * like (a lit button is cyan). */
/* The Pokedex's and the Bag's pictures stand taller than the others and
 * than the button: those are shrunk a little to this height (each output
 * pixel the average of the input pixels it covers, so it stays smooth);
 * the others are left as the game draws them. */
#define ICON_TALL 26

static void icon_shrink(MenuIcon *ic, int th)
{
    static u8 out[ICON_W * ICON_H * 4];
    int tw = (ic->w * th + ic->h / 2) / ic->h, x, y;

    if (tw < 1) {
        tw = 1;
    }
    for (y = 0; y < th; y++) {
        for (x = 0; x < tw; x++) {
            /* the source box of this pixel, in 1/256ths */
            int sx0 = x * ic->w * 256 / tw, sx1 = (x + 1) * ic->w * 256 / tw;
            int sy0 = y * ic->h * 256 / th, sy1 = (y + 1) * ic->h * 256 / th;
            int64_t acc[4] = { 0, 0, 0, 0 }, area = 0;
            int u, v;

            for (v = sy0 / 256; v * 256 < sy1; v++) {
                int wy = (v * 256 + 256 < sy1 ? v * 256 + 256 : sy1) - (v * 256 > sy0 ? v * 256 : sy0);

                for (u = sx0 / 256; u * 256 < sx1; u++) {
                    int wx = (u * 256 + 256 < sx1 ? u * 256 + 256 : sx1) - (u * 256 > sx0 ? u * 256 : sx0);
                    const u8 *p = ic->px + ((size_t)v * (size_t)ic->w + (size_t)u) * 4;
                    int64_t wgt = (int64_t)wx * wy, aw = wgt * p[3];

                    acc[0] += aw * p[0];
                    acc[1] += aw * p[1];
                    acc[2] += aw * p[2];
                    acc[3] += aw;
                    area += wgt;
                }
            }
            {
                u8 *d = out + ((size_t)y * (size_t)tw + (size_t)x) * 4;

                if (acc[3] > 0) {
                    d[0] = (u8)(acc[0] / acc[3]);
                    d[1] = (u8)(acc[1] / acc[3]);
                    d[2] = (u8)(acc[2] / acc[3]);
                    d[3] = (u8)(acc[3] / area);
                } else {
                    d[0] = d[1] = d[2] = d[3] = 0;
                }
            }
        }
    }
    memcpy(ic->px, out, (size_t)tw * (size_t)th * 4);
    ic->w = tw;
    ic->h = th;
}

static void icon_grab(MenuIcon *ic, const u8 *objs, int sx, int sy)
{
    int x, y, x0 = ICON_W, y0 = ICON_H, x1 = -1, y1 = -1;

    if (objs == NULL || sx < 0 || sy < 0 || sx + ICON_W > 256 || sy + ICON_H > 192) {
        return;
    }
    for (y = 0; y < ICON_H; y++) {
        for (x = 0; x < ICON_W; x++) {
            if (objs[((size_t)(sy + y) * 256 + (size_t)(sx + x)) * 4 + 3] != 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < x0) {
        return;
    }
    /* the picture alone, trimmed to what the game draws */
    ic->w = x1 - x0 + 1;
    ic->h = y1 - y0 + 1;
    for (y = 0; y < ic->h; y++) {
        memcpy(ic->px + (size_t)y * (size_t)ic->w * 4,
               objs + ((size_t)(sy + y0 + y) * 256 + (size_t)(sx + x0)) * 4, (size_t)ic->w * 4);
    }
    if (ic->h > ICON_TALL) {
        icon_shrink(ic, ICON_TALL);
    }
    ic->ok = 1;
}

/* The button shape: a folder tab at the top left (its right edge slants),
 * then the body.  Row y of the shape, shrunk by `ins` on every side: the
 * span [*x0, *x1), or 0 when the row is outside. */
static int btn_span(int w, int h, int t, int tw, int ins, int y, int *x0, int *x1)
{
    int e;

    if (y < ins || y >= h - ins) {
        return 0;
    }
    *x0 = ins;
    e = tw + (y < t ? y : t) - ins;
    *x1 = e;
    if (y >= t + ins) {
        *x1 = w - ins;
    }
    if (*x1 > w - ins) {
        *x1 = w - ins;
    }
    return *x1 > *x0;
}

/* state: 0 plain, 1 hover, 2 selected. */
static void draw_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                        const MenuIcon *icon, int state)
{
    const int t = 5;
    int tw = w * 42 / 100, r, x0, x1;
    u32 outline, edge, light, dark, spine;

    if (state == 2) {
        outline = UI_CYAN_EDGE;
        edge = UI_INK;
        light = UI_CYAN;
        dark = UI_CYAN2;
        spine = 0x7CF2E6u;
    } else if (state == 1) {
        outline = UI_CYAN2;
        edge = UI_INK;
        light = UI_CYAN;
        dark = UI_CYAN2;
        spine = 0x7CF2E6u;
    } else {
        outline = UI_INK;
        edge = UI_EDGE;
        light = 0x2E2E2Eu;
        dark = 0x151515u;
        spine = UI_EDGE2;
    }
    for (r = 0; r < h; r++) {                       /* the outline          */
        if (btn_span(w, h, t, tw, 0, r, &x0, &x1)) {
            ui_fill(c, x + x0, y + r, x1 - x0, 1, outline, 255);
        }
    }
    for (r = 0; r < h; r++) {                       /* the edge             */
        if (btn_span(w, h, t, tw, 1, r, &x0, &x1)) {
            ui_fill(c, x + x0, y + r, x1 - x0, 1, edge, 255);
        }
    }
    for (r = 0; r < h; r++) {                       /* the face, two tones  */
        int sx;

        if (!btn_span(w, h, t, tw, 2, r, &x0, &x1)) {
            continue;
        }
        sx = w * 40 / 100 + r * 8 / 10;             /* the diagonal         */
        if (sx > x1) {
            sx = x1;
        }
        if (sx < x0) {
            sx = x0;
        }
        if (sx > x0) {
            ui_fill(c, x + x0, y + r, sx - x0, 1, light, 255);
        }
        if (x1 > sx) {
            ui_fill(c, x + sx, y + r, x1 - sx, 1, dark, 255);
        }
        if (sx > x0 && sx < x1) {
            ui_fill(c, x + sx, y + r, 1, 1, spine, 255);
        }
        if (r == 2 && x1 > x0 + 2) {                /* the top highlight    */
            ui_fill(c, x + x0 + 1, y + r, sx - x0 > 2 ? sx - x0 - 1 : 1, 1,
                    state ? 0x9CFFF2u : UI_EDGE2, 255);
        }
    }
    if (icon != NULL && icon->ok) {
        UiImage im;
        int iy = y + (h - icon->h) / 2 + 1;
        EidosUiRect saved;

        im.w = icon->w;
        im.h = icon->h;
        im.rgba = icon->px;
        ui_push(c, 0, 0, &saved);
        /* inside the button: the tall pictures are shrunk to fit it */
        ui_clip(c, x + 3, y + 2, 40, h - 4);
        ui_image(c, x + 6 + (ICON_W - icon->w) / 2, iy, &im, 1);
        ui_pop(c, 0, 0, &saved);
    }
    ui_text(c, x + 6 + ICON_W + 6, y + t + ui_text_y(h - t, UI_FONT_LARGE), label,
            UI_WHITE, state ? (s32)UI_CYAN_EDGE : (s32)UI_EDGE, UI_FONT_LARGE, 1);
}

typedef struct PartyLine {
    char name[40];
    int gender, level, hp, maxhp, species, form, egg;
} PartyLine;

static int read_party(const FieldNow *f, PartyLine *out, int max)
{
    void *party;
    u32 i, n;
    u8 *gd;
    int k = 0;

    if (GAME_DATA_GetMyParty == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0;
    }
    gd = rdp(f->gs + 0x1C);
    if (gd == NULL) {
        return 0;
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL) {
        return 0;
    }
    n = POKEPARTY_GetCount(party);
    for (i = 0; i < n && k < max; i++) {
        void *pp = POKEPARTY_GetMember(party, i);
        u16 nick[48];
        PartyLine *l = &out[k];

        if (pp == NULL) {
            continue;
        }
        memset(nick, 0xFF, sizeof(nick));
        (void)POKE_TOOL_GetPartyValue(pp, P_NICKNAME, (u32)(uintptr_t)nick);
        units_to_utf8(nick, 48, l->name, (int)sizeof(l->name));
        l->egg = POKE_TOOL_GetPartyValue(pp, P_IS_EGG, 0) != 0;
        l->species = (int)POKE_TOOL_GetPartyValue(pp, P_SPECIES, 0);
        l->form = (int)POKE_TOOL_GetPartyValue(pp, P_FORM, 0);
        l->gender = (int)POKE_TOOL_GetPartyValue(pp, P_GENDER, 0);
        l->level = (int)POKE_TOOL_GetPartyValue(pp, P_LEVEL, 0);
        l->hp = (int)POKE_TOOL_GetPartyValue(pp, P_HP, 0);
        l->maxhp = (int)POKE_TOOL_GetPartyValue(pp, P_MAX_HP, 0);
        if (l->name[0] == '\0') {
            snprintf(l->name, sizeof(l->name), "?");
        }
        k++;
    }
    return k;
}

static int menu_match(void)
{
    FieldNow f;

    if (!field_now(&f)) {
        return 0;
    }
    if (f.mode == 1 && f.panel != NULL) {
        logf_scene("field-menu");
        return 1;
    }
    return 0;
}

static int g_menu_frame_open;
static int g_menu_leaving;          /* the menu is closing or opening an app */
static int g_menu_shown;            /* its panels are on show                */
static u32 g_menu_last_frame;       /* the last frame it ran                 */
static int g_menu_closing;          /* closing to the field (not to an app)  */
static int g_scrim;
static u32 g_menu_back_frame;       /* the frame the menu came back from an app */
#define MENU_BACK_HOLD 20u                 /* the world's dimming behind it, 0..90  */

static void menu_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    FieldNow f;
    int cw = c->canvas->w, ch = c->canvas->h;
    u8 *menu;
    int layout, ready, i, n = 0, sel = -1, hover_any = 0;
    int slot_of[6], btn_of[6];
    char label[6][40];
    PartyLine party[6];
    int np;
    int colw, colx, bh, gap = 4, avail, top;
    int px, py, pw;
    int click_slot = -1, click_party = -1, in_panel = 0, hint = -1;
    EidosUiRect col_rect, party_rect;
    static const char *const hints[] = { "X", "Close", "A", "Open", "Arrows", "Move" };

    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 1;          /* the game's own empty message frame     */
    view->no_fade = 1;          /* a fade from here opens an app over it  */
    view->keep_world = 1;
    g_menu_frame_open++;
    g_menu_last_frame = in->frame;
    if (!field_now(&f) || f.panel == NULL) {
        return;
    }
    /* back from an app that closed the field (the Bag, the trainer card):
     * until the field runs again its 3D is empty (the sky's clear colour),
     * so the last picture of the field stands, and is not kept from */
    {
        /* the game fades its own screen in from black as the field comes
         * back (master brightness, BLDY): its 3D may still be empty then */
        u32 mb = bw_io_read16(0x0400006Cu);
        int faded = ((mb >> 14) & 3u) == 2u && (mb & 0x1Fu) != 0u;

        if (f.main_state != 1 || faded) {
            view->hold_world = 1;
            view->keep_world = 0;
        }
    }
    menu = f.panel;
    layout = menu[0x21];
    (void)layout;
    ready = rd32(menu + 0x0C) == 3 && f.sstate == 0;

    /* The bottom screen's own animations (the back button, the pressed
     * icon) are not shown here: the menu skips the back button's, and once it is
     * leaving it is not drawn, so a close or a choice takes effect at
     * once.  FieldMenu.state (+0x0C): 4 back anim, 5 decide, 6 press anim,
     * 7 / 8 notify, 9 slide out, 10 done. */
    {
        u32 st = rd32(menu + 0x0C);

        if (st == 4) {
            wr32(menu + 0x0C, 5);           /* BACK_ANIM -> DECIDE        */
            g_menu_closing = 1;
        }
        if (st >= 4) {
            g_menu_shown = 0;
            if (g_menu_closing) {
                /* back to the field: the menu fades out at once, the
                 * dimming with it; the next opening eases in again */
                if (!g_menu_leaving) {
                    eidos_ui_crossfade();
                }
                g_scrim = 0;
            } else {
                /* opening an app: the menu's last picture stays until the
                 * app is up and crossfades from it */
                eidos_ui_hold_canvas();
            }
            g_menu_leaving = 1;
            return;
        }
        if (g_menu_leaving && !g_menu_closing) {
            g_menu_back_frame = in->frame;  /* back from an app */
        }
        g_menu_leaving = 0;
        g_menu_closing = 0;
        /* Back from an app: one app may open the next at once (the party
         * list, then the summary to forget a move, then the list again),
         * passing through the menu for a few frames.  The app's last
         * picture stands that long, so the menu does not flash in between;
         * a menu the player came back to shows after. */
        if (in->frame - g_menu_back_frame < MENU_BACK_HOLD) {
            eidos_ui_hold_canvas();
            return;
        }
    }
    {
        /* the world steps back a little while the menu is up, easing in
         * when it opens from the field; back from an app it is already
         * there, so the world does not light up and dim again */
        if (g_scrim < 90) {
            g_scrim = g_scrim + 12 > 90 ? 90 : g_scrim + 12;
        }
        ui_fill(c, 0, 0, cw, ch, 0x000008u, g_scrim);
    }

    /* Which slots hold a button, and which one the game's cursor is on. */
    for (i = 0; i < 6; i++) {
        const u8 *it = menu + 0x34 + (size_t)i * 0x1C;
        u32 id = rd32(it + 8);

        if (rd32(it) != 0 && id >= 1 && id <= 6) {
            slot_of[n] = i;
            btn_of[n] = (int)id;
            n++;
        }
    }
    /* the cursor's slot from cursorCol / cursorRow, which a move (the
     * game's or ours) sets at once; selectedItem follows a frame later */
    if (rd32(menu + 0x2C) != 0 && menu[0x1E] + 2 * menu[0x1F] < 6) {
        sel = menu[0x1E] + 2 * menu[0x1F];
    }
    /* The keys follow the column the buttons are drawn in: up and down go
     * to the previous and next button in it (the game's own cursor, moved
     * the way FIELD_MENU_MoveCursor moves it, with its sound), left and
     * right do nothing. */
    if (ready && n > 0) {
        u32 k = eidos_ui_keys_pressed();

        eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
        if (k & (BW_KEY_UP | BW_KEY_DOWN)) {
            int pos = -1, to;

            /* from the game's cursor slot, shown or not (the first key
             * moves at once instead of only showing the cursor) */
            int at = menu[0x1E] + 2 * menu[0x1F];

            for (i = 0; i < n; i++) {
                if (slot_of[i] == at) {
                    pos = i;
                }
            }
            to = pos < 0 ? 0 : pos + ((k & BW_KEY_UP) ? -1 : 1);
            if (to >= 0 && to < n && to != pos) {
                /* FieldMenu.cursorCol / cursorRow: slot = col + 2 * row
                 * (FIELD_MENU_GetSlotIndex); FIELD_MENU_UpdateSelection
                 * picks selectedItem from them on the game's next frame */
                menu[0x1E] = (u8)(slot_of[to] & 1);
                menu[0x1F] = (u8)(slot_of[to] >> 1);
                wr32(menu + 0x24, 1);                  /* cursorDirty      */
                wr32(menu + 0x2C, 1);                  /* keyCursorActive  */
                eidos_ui_sound(0x548);  /* SE_FIELD_MENU_MOVE */
                sel = slot_of[to];
            }
        }
    }
    /* The labels are the game's own text (bank 34), the player's name for
     * the trainer card. */
    for (i = 0; i < n; i++) {
        static const u8 msg_of[7] = { 7, 2, 1, 3, 4, 5, 6 };

        label[i][0] = '\0';
        if (btn_of[i] == 4) {
            u8 *ms = rdp(menu + 0x18);

            if (ms != NULL && MYSTATUS_GetName != NULL) {
                units_to_utf8(MYSTATUS_GetName(ms), 16, label[i], 40);
            }
        } else {
            msg_text(34, msg_of[btn_of[i]], label[i], 40);
        }
        if (label[i][0] == '\0') {
            static const char *const dflt[7] = { "", "POKeMON", "POKeDEX", "BAG", "",
                                                 "SAVE", "OPTIONS" };

            snprintf(label[i], 40, "%s", dflt[btn_of[i]]);
        }
    }
    /* The icons: taken from the game's sprites once the menu has settled;
     * the lit button's picture may be mid-animation, so it is taken again
     * once that button is no longer lit. */
    {
        /* the sprites are drawn for the interface only while an icon
         * still has to be cut from them (eidos_ui_want_objs: a whole
         * screen's OBJs a frame) */
        int need = 0;

        for (i = 0; i < n; i++) {
            const MenuIcon *ic = &g_icons[btn_of[i]];

            if (!ic->ok || (ic->lit && slot_of[i] != sel)) {
                need = 1;
            }
        }
        if (need) {
            eidos_ui_want_objs(1);
        }
    }
    if (ready && in->objs_rgba[1] != NULL) {
        for (i = 0; i < n; i++) {
            int s = slot_of[i];
            MenuIcon *ic = &g_icons[btn_of[i]];

            if (!ic->ok || (ic->lit && s != sel)) {
                icon_grab(ic, in->objs_rgba[1], k_slot_x[s] + 2, k_slot_y[s] + 2);
                ic->lit = s == sel;
            }
        }
    }

    /* The panels show once the game's menu has settled and every icon is
     * cut from it, and then fade in together (the first opening of a
     * session waits the few frames the game's menu takes to slide in). */
    {
        int all = n > 0;

        for (i = 0; i < n; i++) {
            if (!g_icons[btn_of[i]].ok) {
                all = 0;
            }
        }
        if (!all) {
            g_menu_shown = 0;
            return;
        }
        if (!g_menu_shown) {
            g_menu_shown = 1;
            if (g_menu_frame_open > 1) {
                eidos_ui_crossfade();
            }
        }
    }

    /* Layout. */
    colw = clampi(cw * 30 / 100, 118, 168);
    colx = cw - colw - 8;
    avail = ch - ui_bar_h(c) - 16;
    bh = n > 0 ? (avail - (n - 1) * gap) / n : 30;
    bh = clampi(bh, 24, 44);
    top = 8 + (avail - (n * bh + (n - 1) * gap)) / 2;
    if (top < 8) {
        top = 8;
    }
    col_rect.x = colx - 10;
    col_rect.y = 0;
    col_rect.w = colw + 18;
    col_rect.h = ch - ui_bar_h(c);

    px = 8;
    py = 8;
    pw = clampi(cw * 30 / 100, 118, 168);
    np = read_party(&f, party, 6);
    party_rect.x = px;
    party_rect.y = py;
    party_rect.w = pw;
    party_rect.h = 22 + np * 32;

    /* The party (the game's data; a click on a row opens the party list). */
    {
        char cnt[16];

        snprintf(cnt, sizeof(cnt), "%d/6", np);
        ui_header(c, px, py, pw, "PARTY", NULL, cnt, UI_SKY);
    }
    for (i = 0; i < np; i++) {
        const PartyLine *l = &party[i];
        EidosUiRect r;
        int st;
        const UiImage *ic = l->egg ? ui_mon_icon(l->species == 490 ? 651 : 650, 0, 0)
                                    : ui_mon_icon((u16)l->species, (u8)l->form, 0);

        r.x = px;
        r.y = py + 24 + i * 32;
        r.w = pw;
        r.h = 30;
        st = eidos_ui_hit(r);
        if (st & EIDOS_UI_CLICK) {
            click_party = i;
        }
        ui_party_row(c, px, r.y, pw, i, l->egg ? "Egg" : l->name,
                     l->egg ? 2 : l->gender, l->egg ? 0 : l->level,
                     l->egg ? 0 : l->hp, l->egg ? 0 : l->maxhp, ic,
                     (st & EIDOS_UI_HOVER) != 0);
    }

    /* The buttons.  While the mouse is over one, it alone is lit (the
     * keys' cursor shows again with the next key). */
    for (i = 0; i < n; i++) {
        EidosUiRect r = { colx - 8, top + i * (bh + gap), colw + 8, bh };

        if (eidos_ui_hit(r) & EIDOS_UI_HOVER) {
            hover_any = 1;
        }
    }
    for (i = 0; i < n; i++) {
        EidosUiRect r;
        int st, state, bx = colx, by = top + i * (bh + gap);

        state = slot_of[i] == sel && !hover_any ? 2 : 0;
        r.x = bx - 8;
        r.y = by;
        r.w = colw + 8;
        r.h = bh;
        st = eidos_ui_hit(r);
        if (st & EIDOS_UI_HOVER) {
            state = 2;
            if (ready && slot_of[i] != menu[0x1E] + 2 * menu[0x1F]) {
                /* the game's cursor follows the mouse, so a key goes on
                 * from the button the mouse was on */
                menu[0x1E] = (u8)(slot_of[i] & 1);
                menu[0x1F] = (u8)(slot_of[i] >> 1);
                wr32(menu + 0x24, 1);              /* cursorDirty      */
            }
        }
        if (st & EIDOS_UI_CLICK) {
            click_slot = slot_of[i];
        }
        if (state == 2) {
            bx -= 8;
        } else if (state == 1) {
            bx -= 4;
        }
        draw_button(c, bx, by, colw + (colx - bx), bh, label[i], &g_icons[btn_of[i]], state);
    }

    hint = hint_bar(c, ch - ui_bar_h(c), cw, hints, 3);
    {
        /* Title screen: what the DS's soft reset does (L + R + START +
         * SELECT), which a touch screen has no keys for.  A second click
         * within three seconds confirms: unsaved progress is lost. */
        static u32 armed_at;
        int armed = armed_at != 0u && in->frame - armed_at < 180u;
        int bh, top = ui_bottom_bar(c, ch - ui_bar_h(c), 17, &bh);
        int tx = cw - 4 - c->safe_r;
        /* in the bar's middle, as its keys are (taller on a phone) */
        int st = small_button(c, &tx, top + (bh - 17) / 2,
                              armed ? "Sure? Unsaved progress is lost" : "Title screen", 17, NULL);

        if (st & EIDOS_UI_CLICK) {
            if (armed) {
                armed_at = 0;
                if (bw_mod_restart != NULL) {
                    /* the reset belongs to the game's thread (a phone runs
                     * the game on its own thread, not the interface's) */
                    eidos_ui_game_call(title_restart, NULL);
                }
            } else {
                armed_at = in->frame != 0u ? in->frame : 1u;
            }
            hint = -2;                      /* not a click on the menu      */
        }
    }

    /* What a click does. */
    in_panel = 0;
    {
        EidosUiRect whole;

        whole.x = 0;
        whole.y = 0;
        whole.w = cw;
        whole.h = ch;
        if (eidos_ui_hit(whole) & EIDOS_UI_CLICK) {
            EidosUiRect hb;

            hb.x = 0;
            hb.y = ch - ui_bar_h(c);
            hb.w = cw;
            hb.h = 17;
            in_panel = (eidos_ui_hit(col_rect) & EIDOS_UI_CLICK) != 0 ||
                       (eidos_ui_hit(party_rect) & EIDOS_UI_CLICK) != 0 ||
                       (eidos_ui_hit(hb) & EIDOS_UI_CLICK) != 0;
            if (!in_panel && click_slot < 0 && click_party < 0 && hint < 0) {
                click_slot = 6;                     /* outside: back        */
            }
        }
    }
    if (hint == 0) {
        click_slot = 6;
    } else if (hint == 1 && sel >= 0) {
        click_slot = sel;
    }
    if (click_party >= 0) {
        /* open the party list on that Pokemon: the POKEMON button, then
         * the party recipe picks the row (eidos_ui_take_intent) */
        for (i = 0; i < n; i++) {
            if (btn_of[i] == 1) {
                click_slot = slot_of[i];
            }
        }
        eidos_ui_set_intent(EIDOS_UI_INTENT_PARTY_PICK, click_party, 240);
    }
    if (right_click(in)) {
        click_slot = 6;
    }
    if (click_slot >= 0 && ready) {
        eidos_ui_tap(k_slot_cx[click_slot], k_slot_cy[click_slot], 3);
    }
}

static void menu_reset(void)
{
    memset(g_icons, 0, sizeof(g_icons));
    g_menu_frame_open = 0;
    g_menu_shown = 0;
    g_menu_closing = 0;
    g_scrim = 0;
}

static const EidosUiRecipe recipe_menu = {
    "field-menu", menu_match, menu_run, menu_reset, 0
};

/* ------------------------------------------------------------------ */
/* The field: the C-Gear bar                                           */
/* ------------------------------------------------------------------ */

static int g_show_cgear;            /* the game's own C-Gear is shown      */
/* pixels the bar is slid up; it starts out of sight, so a field that opens
 * with an event (the season card after loading a save) never shows it */
#define BAR_HIDDEN 17
static int g_bar_slide = BAR_HIDDEN;

/* The place-name banner (FIELD_PLACE_NAME): the game draws it across the
 * top of the screen when the player enters a town or a route.  Its state
 * (+0x3C) is 0 while it is idle. */
extern void *FIELDMAP_GetPlaceName(void *fieldMap) __attribute__((weak));
/* An event runs (a script, the season card when a save is loaded): the
 * player has no menu then, and the bar keeps out of the way. */
extern int GAMESYSTEM_IsEventActive(void *gameSystem) __attribute__((weak));

/* The place's name in the bar, and the name before it while the bar's
 * name changes: the old one fades out over NAME_OUT frames, the new one in
 * over NAME_IN (only when the words change: a building's own floor and its
 * town have the same name, and nothing moves). */
#define NAME_OUT 16u
#define NAME_IN  24u
static char g_bar_name[64], g_bar_prev[64];
static u32 g_bar_fade = NAME_OUT + NAME_IN;

static int banner_shown(const FieldNow *f)
{
    const u8 *pn;

    if (FIELDMAP_GetPlaceName == NULL || f->fm == NULL) {
        return 0;
    }
    pn = (const u8 *)FIELDMAP_GetPlaceName(f->fm);
    return pn != NULL && rd32(pn + 0x3C) != 0;
}

static int field_match(void)
{
    FieldNow f;

    if (!field_now(&f)) {
        return 0;
    }
    /* mode 7 is the save report; the start menu's save has its own recipe
     * (recipe_menus.c, before this one), so a mode 7 here is a script's
     * save (a receptionist's "Would you like to save the game?"): the
     * world, its yes/no as a choice list, the report left out */
    if (f.mode == 0 || f.mode == 5 || f.mode == 7 || f.mode == 8) {
        u8 *list, *win;

        logf_scene("field");
        /* the game's text at the game's own size */
        if (ui_textbox_field_active(f.fm) || choices_now(&f, &list, &win) || item_show_active()) {
            eidos_ui_want_ds_scale();
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The field's choice lists                                            */
/* ------------------------------------------------------------------ */

/*
 * A script's list menu (a receptionist's "Enter the Union Room / Info /
 * Cancel") and its yes/no window are overlay 21's FieldMenuFunc: a window
 * on the upper screen, a BmpMenuList the keys move.  In the one-screen view
 * that window is covered by a list in the interface's own style, at the
 * same place and with the same rows.  The game's cursor is the marked row
 * and moves only by UP and DOWN presses, so the game plays its own sound
 * and prints the row's help as it does for the keys.  The mouse: the cursor
 * follows the pointer and a click picks the row (A).  A finger: a tap on
 * another row moves the cursor there, a tap on the cursor's row picks it.
 * A tap outside the list does nothing; a right click is B.
 *
 * The open window is found from the game's current event: the script's
 * list event (SCRCMD_MSGWIN_MenuEventMain: the menu is in the script's
 * common work, ScrcmdCommonWork.menu.fieldMenu) or its yes/no event
 * (SCRCMD_MSGWIN_YesNoEventMain: YesNoEventWork.menu), each at its waiting
 * step (2).
 */
extern void *GAMESYSTEM_GetEvent(void *gameSystem) __attribute__((weak));
extern int ov021_SCRCMD_MSGWIN_MenuEventMain(void *ev, int *seq, void *work) __attribute__((weak));
extern int ov021_SCRCMD_MSGWIN_YesNoEventMain(void *ev, int *seq, void *work) __attribute__((weak));

/* Finds the script's open list and its window (overlay 21's FieldMenuFunc:
 * bmpWin +0x08, menuList +0x10). */
extern int bw_ui_field_menu_now(const void **menuList, const void **bmpWin) WEAK;

static int choices_now(const FieldNow *f, u8 **list, u8 **win)
{
    u8 *ev, *work, *fmf = NULL;
    uintptr_t func;

    *list = *win = NULL;
    /* any list the field runs (stubs/ov/patch_ov021_field_msgbg.c): the
     * Union Room's talk menu, the Battle Subway's, the scripts' */
    if (bw_ui_field_menu_now != NULL) {
        const void *l, *w;

        if (bw_ui_field_menu_now(&l, &w) && w != NULL && (rd16((const u8 *)w) & 0x7FFFu) == 0x7FB2u) {
            *list = (u8 *)l;
            *win = (u8 *)w;
            return 1;
        }
    }
    if (GAMESYSTEM_GetEvent == NULL) {
        return 0;
    }
    ev = (u8 *)GAMESYSTEM_GetEvent(f->gs);
    if (ev == NULL || rd32(ev + 8) != 2u) {
        return 0;
    }
    func = (uintptr_t)rd32(ev + 4);
    work = rdp(ev + 0x0C);
    if (work == NULL) {
        return 0;
    }
    if (ov021_SCRCMD_MSGWIN_MenuEventMain != NULL
        && func == (uintptr_t)ov021_SCRCMD_MSGWIN_MenuEventMain) {
        u8 *cmd = rdp(work), *common = cmd != NULL ? rdp(cmd + 0x20) : NULL;

        fmf = common != NULL ? rdp(common + 0x34 + 0x1C) : NULL;
    } else if (ov021_SCRCMD_MSGWIN_YesNoEventMain != NULL
               && func == (uintptr_t)ov021_SCRCMD_MSGWIN_YesNoEventMain) {
        fmf = rdp(work + 8);
    }
    if (fmf == NULL) {
        return 0;
    }
    *list = rdp(fmf + 0x10);
    *win = rdp(fmf + 0x08);
    return *list != NULL && *win != NULL && (*win)[2] < 4;   /* the upper screen */
}

/*
 * Draws the open choice list, if there is one (engine/ui/choice_list.h),
 * over the world's DS screen: the window's height, centred.  Returns 1
 * while one is open (the caller's own clicks on the world wait).
 */
extern void g2d_hw_hide_main_bgs(u32 mask) WEAK;
extern void bw_ui_text_set_own(int on) WEAK;

/*
 * The field's text: its open windows as the interface's text boxes
 * (engine/ui/textbox.c), the game's own windows and balloon polygons left
 * out of the screen.  The DS screen stands at the window's height,
 * centred.  Returns 1 while a window is open.
 */
/* For before-and-after pictures only: EIDOS_UI_PLAIN_FIELD leaves the
 * game's own windows, lists, banners and lower cards on the field. */
static int plain_field(void)
{
    static int plain = -1;

    if (plain < 0) {
        plain = getenv("EIDOS_UI_PLAIN_FIELD") != NULL;
    }
    return plain;
}

extern int ov012_UNION_MAIN_IsIdle(void *gameSystem) WEAK;

/* A text box of the field is up this frame (field_text drew it). */
static int g_text_up;

/* Where the world's DS screen sits on the canvas (engine/platform/
 * geometry.c): the window's height, centred, on a window 4:3 or wider; the
 * window's width, centred, on a narrower one (a tablet held upright). */
static void world_place(const EidosUiCtx *c, float *s, float *ox, float *oy)
{
    float cw = (float)c->canvas->w, ch = (float)c->canvas->h;

    if (cw * 3.0f >= ch * 4.0f) {
        *s = ch / 192.0f;
        *ox = (cw - 256.0f * *s) * 0.5f;
        *oy = 0.0f;
    } else {
        *s = cw / 256.0f;
        *ox = 0.0f;
        *oy = (ch - 192.0f * *s) * 0.5f;
    }
}

static int field_text(EidosUiCtx *c, const FieldNow *f)
{
    u8 *list = NULL, *win = NULL;
    float s, ox, oy;
    u32 hide;
    int text;

    world_place(c, &s, &ox, &oy);
    if (bw_ui_text_set_own != NULL) {
        bw_ui_text_set_own(1);
    }
    (void)choices_now(f, &list, &win);
    hide = ui_textbox_field(c, f->fm, ox, oy, s, win);
    if (win != NULL && win[2] < 4) {
        hide |= 1u << win[2];           /* an open list's window: ours shows */
    }
    /* the card goes with the box: not while the game holds its window open
     * with the text already cleared (it closes it some frames later) */
    item_card(c, f, hide != 0 && ui_textbox_field_drawn() > 0);
    text = hide != 0;
    /* the season's scoreboard stays the game's own (the player prefers it);
     * the game's place-name banner is left out: the bar's name changes
     * instead */
    if (banner_shown(f)) {
        hide |= 0x8u | 0x10u;           /* the game's banner: BG3, its letters */
    }
    if (g2d_hw_hide_main_bgs != NULL) {
        g2d_hw_hide_main_bgs(hide);
    }
    return text;
}

/*
 * The item a script just gave (mock-up "item_get"): while the game's
 * "obtained" message is up, a card above it with the item's icon, its
 * name and its pocket (the Bag's own words).  The script command that puts
 * the item in the Bag tells the port which one (bw_ui_item_got,
 * stubs/ov/patch_ov021_scrcmd_trainer_audio_item.c); the card shows from
 * that moment until the message closes.
 */
extern u32 bw_ui_item_last(u32 *count, u32 *serial) WEAK;
extern void FILE_UTIL_ReadString(void *dst, u32 entry, u32 heap) WEAK;
extern u32 FILE_UTIL_GetItemParam(u32 item, u32 param, u32 heap) WEAK;
extern u32 ov021_FIELDMAP_GetHeapId(void *fieldMap) WEAK;

typedef struct ItemCard {
    u32 serial;             /* the item the card is for                    */
    u32 item;
    u32 pocket;
    char name[48];
    volatile int ready;
    void *fm;
} ItemCard;

static ItemCard g_item_card;
static int g_item_card_drawn;   /* item_card drew this frame              */
static u32 g_item_seen;     /* the item serial last frame                 */
static u32 g_item_age;      /* frames since it changed                    */
static int g_item_show;     /* this message is the item's                 */
static int g_item_text;     /* a message was up last frame                */

typedef struct ItemStr { u16 capacity, length; u32 magic; u16 chars[64]; } ItemStr;

static void item_card_make(void *arg)
{
    ItemCard *k = (ItemCard *)arg;
    ItemStr s;
    u32 heap = ov021_FIELDMAP_GetHeapId != NULL ? ov021_FIELDMAP_GetHeapId(k->fm) : 0;

    k->name[0] = '\0';
    if (FILE_UTIL_ReadString != NULL) {
        s.capacity = 64;
        s.length = 0;
        s.magic = 0xB6F8D2ECu;
        s.chars[0] = 0xFFFFu;
        FILE_UTIL_ReadString(&s, k->item, heap);
        units_to_utf8(s.chars, 64, k->name, (int)sizeof k->name);
    }
    k->pocket = FILE_UTIL_GetItemParam != NULL ? FILE_UTIL_GetItemParam(k->item, 5, heap) : 99u;
    k->ready = 1;
}

/*
 * The badges: their cut-in is the key items' with a badge (ov021 FLD3D_CI
 * sequence 2 for the Trio Badge, 4 to 10 for the Basic to the Legend Badge;
 * 45 plays the Bolt Badge's again), so the showcase shows it the same way,
 * with the badge as the badge case draws it (ui_badge_image), its name from
 * the Trainer Card's texts (a/0/0/2 bank 187, line 88 + 3 x badge, its
 * first line) and "BADGE" on the card, which stays over the game's
 * "received the ... Badge" message as an item's card does.
 */
static int g_badge_card;            /* 1 + the badge whose card stands      */
static int g_badge_card_text;       /* its message has been up              */
static u32 g_badge_card_age;

static int badge_of_sequence(int seq)
{
    return seq == 2 ? 0 : (seq >= 4 && seq <= 10) ? seq - 3 : seq == 45 ? 3 : -1;
}

static void badge_name(int badge, char *out, int cap)
{
    const char *t = ui_game_text(2, 187, 88u + 3u * (u32)badge);
    int n = 0;

    while (t != NULL && t[n] != '\0' && t[n] != '\n' && t[n] != '\r' && n < cap - 1) {
        out[n] = t[n];
        n++;
    }
    out[n] = '\0';
}

static int badge_card(EidosUiCtx *c, int text_up)
{
    int cw = c->canvas->w, ch = c->canvas->h, w;
    char name[48];
    const char *pocket = ui_tr("BADGE");

    if (g_badge_card == 0) {
        return 0;
    }
    if (!text_up) {
        /* gone with its message, or never up after a while */
        if (g_badge_card_text || ++g_badge_card_age > 300u) {
            g_badge_card = 0;
        }
        return 0;
    }
    g_badge_card_text = 1;
    badge_name(g_badge_card - 1, name, (int)sizeof name);
    w = item_card_width(name, pocket);
    item_card_draw(c, (cw - w) / 2, ITEM_CARD_Y(ch), w, ui_badge_image(g_badge_card - 1), name, pocket);
    return 1;
}

static void item_card(EidosUiCtx *c, const FieldNow *f, int text_up)
{
    u32 serial = 0, item;
    int cw = c->canvas->w, ch = c->canvas->h, w, x, y;
    const UiImage *ic;
    const char *pocket;

    g_item_card_drawn = 0;
    if (badge_card(c, text_up)) {
        g_item_card_drawn = 1;
        return;
    }
    if (bw_ui_item_last == NULL) {
        return;
    }
    item = bw_ui_item_last(NULL, &serial);
    /* the item's message is the one that opens soon after the item went
     * into the Bag (the script prints it next) */
    if (serial != g_item_seen) {
        g_item_seen = serial;
        g_item_age = 0;
        g_item_show = text_up;
    } else if (g_item_age < 100000u) {
        g_item_age++;
    }
    if (text_up && !g_item_text) {
        g_item_show = g_item_age < 240u;
    }
    g_item_text = text_up;
    if (!text_up || !g_item_show || item == 0 || item > 0x400) {
        return;
    }
    if (g_item_card.serial != serial) {
        g_item_card.serial = serial;
        g_item_card.item = item;
        g_item_card.ready = 0;
        g_item_card.fm = f->fm;
        if (!eidos_ui_game_call(item_card_make, &g_item_card)) {
            g_item_card.serial = 0;
            return;
        }
    }
    if (!g_item_card.ready || item_show_active()) {
        return;                 /* (the showcase draws the card it forms) */
    }
    pocket = g_item_card.pocket < 5 ? ui_game_text(2, 0x37, g_item_card.pocket) : "";
    ic = ui_item_icon((u16)item);
    w = item_card_width(g_item_card.name, pocket);
    x = (cw - w) / 2;
    y = ITEM_CARD_Y(ch);
    item_card_draw(c, x, y, w, ic, g_item_card.name, pocket);
    g_item_card_drawn = 1;
}

/*
 * The key-item showcase (engine/ui/item_show.c): the game's cut-in for a
 * key item (ov021 FLD3D_CI, the flash and the item spinning out of a glow
 * during the jingle) is drawn on the DS screen alone, which the one-screen
 * world does not show.  While it runs, the world holds its last wide
 * picture (the game froze its own screen the same way) and the interface
 * plays its own version, which ends as the item card.  The cut-in tells
 * the port its item (bw_ui_cutin_item, stubs/ov/patch_ov021_fld_wipe_3dobj.c);
 * the item card takes over once the "obtained" message opens.
 */
extern u32 bw_ui_cutin_last(u32 *serial) WEAK;
extern int ov021_FLD3D_CI_EventMain(void *event, int *state, void *work) WEAK;

typedef struct ItemShow {
    u32 serial;             /* the cut-in it is for                         */
    int on;
    int t;                  /* frames since the cut-in began                */
    u32 start, end;         /* the game frames it began and ended at        */
    int ended;              /* frames since it ended, 0 while it runs       */
    ItemCard card;          /* the name and pocket, read on the game thread */
    int badge;              /* 1 + the badge (0 Trio .. 7 Legend) it shows,
                               0 for a key item                             */
    int badge_seen;         /* this cut-in's badge was shown                */
} ItemShow;

static ItemShow g_show;

/* The showcase is up (the field asks for the DS text scale then too, so
 * the card it ends as is the size of the one over the message). */
static int item_show_active(void)
{
    return g_show.on;
}

/* The game's cut-in event is running (it, or a task it pushed). */
static int cutin_running(const FieldNow *f)
{
    const u8 *ev;
    int i;

    if (GAMESYSTEM_GetEvent == NULL || ov021_FLD3D_CI_EventMain == NULL || f->gs == NULL) {
        return 0;
    }
    ev = (const u8 *)GAMESYSTEM_GetEvent(f->gs);
    for (i = 0; i < 4 && ev != NULL; i++) {
        if (*(void *const *)(ev + sizeof(void *)) == (void *)ov021_FLD3D_CI_EventMain) {
            return 1;
        }
        ev = *(const u8 *const *)ev;
    }
    return 0;
}

/*
 * The field moves' cut-in (Cut, Strength, Surf and the others: ov021
 * FLD3D_CI sequence 3, FLD3D_CI_StartPokemonSequence) in the interface's
 * style.  The game's: a white flash, then a cyan band across the middle of
 * its screen with speed streaks, the Pokemon crossing it from the right
 * edge to the left, the band closing (about 90 frames).  On one screen the
 * band crosses the whole window, the Pokemon is its animated front picture,
 * and the world holds its last wide picture under it, as for the key
 * items.  The cut-in's state is the game's own (Fld3dCi, FieldMap.fld3dCi:
 * sequenceId +0x66, the Pokemon's species +0x9C, form +0xA0, shininess
 * +0xA8, ov021/fld3d_ci_types.h).
 */
extern void *ov021_FIELDMAP_GetFld3dCi(void *fieldMap) WEAK;

#define MS_SEQ_POKEMON 3

static struct {
    int on;
    u32 start;              /* the game frame the cut-in began at           */
    int species, shiny;
} g_mon_show;

/* The running cut-in's sequence id, -1 when none. */
static int cutin_sequence(const FieldNow *f, const u8 **ci_out)
{
    const u8 *ci;

    if (!cutin_running(f) || ov021_FIELDMAP_GetFld3dCi == NULL || f->fm == NULL) {
        return -1;
    }
    ci = (const u8 *)ov021_FIELDMAP_GetFld3dCi(f->fm);
    if (ci == NULL) {
        return -1;
    }
    if (ci_out != NULL) {
        *ci_out = ci;
    }
    return (int)rd16(ci + 0x66);
}

/* One frame: 1 while the cut-in holds the world. */
static int mon_show(EidosUiCtx *c, const FieldNow *f)
{
    const u8 *ci = NULL;
    int seq = cutin_sequence(f, &ci);
    int cw = c->canvas->w, ch = c->canvas->h;
    u32 now = bw_input_frame_number();
    int t, bh, by, i;
    float open;

    if (seq != MS_SEQ_POKEMON) {
        g_mon_show.on = 0;
        return 0;
    }
    if (!g_mon_show.on) {
        g_mon_show.on = 1;
        g_mon_show.start = now;
        g_mon_show.species = (int)rd32(ci + 0x9C);
        g_mon_show.shiny = rd32(ci + 0xA8) != 0;
    }
    t = (int)(now - g_mon_show.start);

    /* the flash (the game's, frames 6 to 26) */
    if (t < 28) {
        int a = t < 6 ? t * 255 / 6 : t < 20 ? 255 : (28 - t) * 255 / 8;

        ui_fill(c, 0, 0, cw, ch, UI_WHITE, a < 0 ? 0 : a);
        if (t < 20) {
            return 1;
        }
    }
    /* the band: opens from 26, closes from 86 */
    open = t < 26 ? 0.f : t < 32 ? (float)(t - 26) / 6.f : t < 86 ? 1.f : t < 94 ? (float)(94 - t) / 8.f : 0.f;
    if (open <= 0.f) {
        return 1;
    }
    bh = (int)((float)ch * 0.40f * open);
    by = ch / 2 - bh / 2;
    for (i = 0; i < bh; i++) {
        /* cyan, lighter towards the middle */
        float d = bh > 1 ? fabsf((float)i / (float)(bh - 1) * 2.f - 1.f) : 0.f;
        int r = (int)(0x9A - 0x8A * d), g = (int)(0xEE - 0x40 * d), b = (int)(0xFF - 0x10 * d);

        ui_fill(c, 0, by + i, cw, 1, (u32)(r << 16 | g << 8 | b), 235);
    }
    ui_fill(c, 0, by, cw, 2, UI_WHITE, 220);
    ui_fill(c, 0, by + bh - 2, cw, 2, UI_WHITE, 220);
    /* speed streaks, racing left */
    for (i = 0; i < 14 && bh > 12; i++) {
        u32 h = (u32)i * 2654435761u;
        int len = cw / 10 + (int)(h >> 26) * cw / 160;
        int sy = by + 4 + (int)((h >> 8) % (u32)(bh - 8));
        int speed = cw / 24 + (int)((h >> 20) & 15);
        int sx = cw - (int)(((u32)t * (u32)speed + (h >> 4)) % (u32)(cw + len));

        ui_fill(c, sx, sy, len, 2, UI_WHITE, 170);
    }
    /* the Pokemon, crossing at a steady pace (the game's: the right edge at
     * 30, the left by 88) */
    if (t >= 28 && t < 90 && g_mon_show.species > 0) {
        int size = (int)((float)ch * 0.40f * 0.9f);
        float k = (float)(t - 28) / 60.f;
        int mx = (int)((float)cw - k * (float)(cw + size)) ;
        int my = ch / 2 - size / 2;

        ui_mon_front_draw(c, (u16)g_mon_show.species, g_mon_show.shiny, now, mx, my, size, size);
    }
    return 1;
}

/* One frame: 1 while the showcase holds the world (the caller keeps the
 * world's last wide picture and leaves the game's capture out). */
static int item_show(EidosUiCtx *c, const FieldNow *f)
{
    u32 serial = 0, item;
    int cw = c->canvas->w, ch = c->canvas->h, w, cx, cy;
    float s, ox, oy, fr;
    u32 now;
    const char *pocket;

    if (bw_ui_cutin_last == NULL) {
        return 0;
    }
    item = bw_ui_cutin_last(&serial);
    if (serial != g_show.serial) {
        /* a new cut-in: its name and its pocket, read by the game's thread */
        g_show.serial = serial;
        g_show.on = item != 0 && item <= 0x400;
        g_show.t = 0;
        g_show.ended = 0;
        g_show.badge = 0;
        g_show.card.item = item;
        g_show.card.ready = 0;
        g_show.card.fm = f->fm;
        if (g_show.on && !eidos_ui_game_call(item_card_make, &g_show.card)) {
            g_show.on = 0;
        }
    }
    if (!g_show.on) {
        /* a badge's cut-in: the same showcase, once per cut-in */
        int seq = cutin_sequence(f, NULL), b = badge_of_sequence(seq);

        if (seq < 0) {
            g_show.badge_seen = 0;
        } else if (b >= 0 && !g_show.badge_seen && ui_badge_image(b) != NULL) {
            g_show.badge_seen = 1;
            g_show.on = 1;
            g_show.t = 0;
            g_show.ended = 0;
            g_show.badge = b + 1;
            badge_name(b, g_show.card.name, (int)sizeof g_show.card.name);
            g_show.card.ready = 1;
            g_badge_card = 0;
        }
    }
    if (!g_show.on) {
        return 0;
    }
    /* the game's own frames, so the showcase keeps the game's time at any
     * speed: t since the cut-in began, ended since it ended */
    now = bw_input_frame_number();
    if (g_show.t == 0 && g_show.ended == 0) {
        g_show.start = now;
    }
    g_show.t = (int)(now - g_show.start) + 1;
    if (g_show.ended == 0 && !cutin_running(f) && g_show.t > 10) {
        g_show.end = now;
        g_show.ended = 1;
    } else if (g_show.ended > 0) {
        g_show.ended = (int)(now - g_show.end) + 1;
    }
    if (!g_show.card.ready) {
        return 1;
    }
    /* the mock-up's frame: the game's flash comes 7 frames into the
     * cut-in; the entrance plays up to the hold, which lasts as long as
     * the cut-in does; once it ends (the message opens two frames later)
     * the item goes down into the card over 30 frames, so the card forms
     * as the message opens, never before it */
    fr = (float)(g_show.t - 7) * 146.f / 161.f;
    if (fr > ITEM_SHOW_HOLD) {
        fr = ITEM_SHOW_HOLD;
    }
    if (g_show.ended > 0) {
        fr = ITEM_SHOW_HOLD + (float)g_show.ended * (ITEM_SHOW_END - ITEM_SHOW_HOLD) / 30.f;
    }
    if (fr >= ITEM_SHOW_END || g_show.ended > 90) {
        /* the card is whole: drawn this frame once more (item_card ran
         * already, held back), and the "obtained" message's card has it
         * from the next, at the same place */
        fr = ITEM_SHOW_END;
        g_show.on = 0;
        if (g_show.badge > 0) {
            /* the card stays over the badge's message (badge_card) */
            g_badge_card = g_show.badge;
            g_badge_card_text = 0;
            g_badge_card_age = 0;
            g_show.badge = 0;
        }
    }
    if (fr < 0.f) {
        return 1;
    }
    world_place(c, &s, &ox, &oy);
    /* the middle of the DS screen, where the game's own cut-in shows the
     * item */
    cx = (int)(ox + 128.f * s);
    cy = (int)(oy + 96.f * s);
    if (g_show.badge > 0) {
        pocket = ui_tr("BADGE");
    } else {
        pocket = g_show.card.pocket < 5 ? ui_game_text(2, 0x37, g_show.card.pocket) : "";
    }
    w = item_card_width(g_show.card.name, pocket);
    item_show_draw(c, fr, (float)(g_show.t - 7) * 146.f / 161.f, cx, cy, (cw - w) / 2, ITEM_CARD_Y(ch), w,
                   g_show.badge > 0 ? ui_badge_image(g_show.badge - 1) : ui_item_icon((u16)g_show.card.item),
                   g_show.card.name, pocket);
    return 1;
}

/*
 * The game's other cut-ins (ov021 FLD3D_CI: a trainer's VS banner before a
 * story battle, a Pokemon's cut-in for a field move): the game draws them
 * with its 3D over a capture of its screen.  The one-screen world showed
 * the capture's still picture and left them out.  While one runs, the world
 * is live with the game's 2D, so the cut-in's 3D shows (the GPU draws it at
 * the window's size: the VS banner crosses the whole window), and beyond the
 * DS screen, where the game has only its backdrop colour, the field's kept
 * wide picture shows instead (held_backdrop).
 */
static int cutin_other(const FieldNow *f)
{
    return !g_show.on && !g_mon_show.on && cutin_running(f);
}

static void cutin_view(EidosUiView *view)
{
    view->hide_2d = 0;
    view->cap_mix = 0;
    view->stretch = 0;
    view->hold_world = 0;
    view->held_backdrop = 1;
}

static int field_choices(EidosUiCtx *c, const EidosUiFrame *in, const FieldNow *f)
{
    u8 *list, *win;
    float s, ox, oy;
    int style;

    world_place(c, &s, &ox, &oy);
    if (!choices_now(f, &list, &win)) {
        ui_choice_list_idle();
        return 0;
    }
    /* the list takes the style of the box it answers */
    style = ui_textbox_field_kind(f->fm, win) == UI_TEXTBOX_SYSTEM ? UI_CHOICE_SYSTEM : UI_CHOICE_WHITE;
    if (!ui_choice_list(c, in, list, win, ox, oy, s, style)) {
        return 0;
    }
    if (right_click(in)) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    return 1;
}

static void bar_trapezoid(EidosUiCtx *c, int cx, int w, int h, int hot)
{
    int y;

    for (y = 0; y < h; y++) {
        int inset = y * 5 / (h > 1 ? h - 1 : 1);
        int x0 = cx - w / 2 + inset, x1 = cx + w / 2 - inset;

        ui_fill(c, x0, y, x1 - x0, 1, hot ? UI_EDGE : UI_BODY, 245);
        ui_fill(c, x0, y, 1, 1, UI_EDGE2, 255);
        ui_fill(c, x1 - 1, y, 1, 1, UI_EDGE2, 255);
    }
    ui_fill(c, cx - w / 2 + 5, h, w - 10, 1, UI_CYAN, 255);
}

/* The bar's ground as the name fades into it (UI_INK under the bar's alpha
 * over the world, about). */
#define BAR_INK 0x101214u

/* a over b by alpha (0..255) */
static u32 mix_rgb(u32 a, u32 b, int alpha)
{
    u32 r = (((a >> 16) & 255u) * (u32)alpha + ((b >> 16) & 255u) * (u32)(255 - alpha)) / 255u;
    u32 g = (((a >> 8) & 255u) * (u32)alpha + ((b >> 8) & 255u) * (u32)(255 - alpha)) / 255u;
    u32 bl = ((a & 255u) * (u32)alpha + (b & 255u) * (u32)(255 - alpha)) / 255u;

    return r << 16 | g << 8 | bl;
}

/* The width small_button gives a label and its key. */
static int small_button_w(const char *label, const char *key)
{
    int kw = key != NULL ? ui_keycap_w(key) : 0;

    return ui_text_w(label, UI_FONT_SMALL) + 10 + (kw > 0 ? (kw < 13 ? 13 : kw) + 3 : 0);
}

static int small_button(EidosUiCtx *c, int *x, int y, const char *label, int h,
                        const char *key)
{
    int w = small_button_w(label, key);
    EidosUiRect r;
    int st, xx = *x - w;

    r.x = xx;
    r.y = y;
    r.w = w;
    r.h = h;
    st = eidos_ui_hit(r);
    ui_panel(c, xx, y + 1, w, h - 2, UI_CUT_TL | UI_CUT_BR, 3,
             (st & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY, (st & EIDOS_UI_HOVER) ? UI_CYAN : UI_EDGE2,
             240);
    {
        int tx = xx + 5;

        if (key != NULL) {
            tx += ui_keycap(c, tx, y + (h - 13) / 2, key) + 3;
        }
        ui_text(c, tx, y + ui_text_y(h, UI_FONT_SMALL), label, UI_WHITE, UI_EDGE,
                UI_FONT_SMALL, 1);
    }
    *x = xx - 4;
    return st;
}

/* The C-Gear button as its mark alone (a ring with its bar), for a bar
 * too narrow for the word; the same place and clicks as small_button. */
static int cgear_mark_button(EidosUiCtx *c, int *x, int h)
{
    int w = 20, xx = *x - w, st, cy = h / 2;
    EidosUiRect r;

    r.x = xx;
    r.y = 0;
    r.w = w;
    r.h = h;
    st = eidos_ui_hit(r);
    ui_panel(c, xx, 1, w, h - 2, UI_CUT_TL | UI_CUT_BR, 3,
             (st & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY, (st & EIDOS_UI_HOVER) ? UI_CYAN : UI_EDGE2, 240);
    ui_ellipse(c, xx + 5, cy - 5, 10, 10, UI_BODY, UI_WHITE, 255);
    ui_fill(c, xx + 9, cy - 1, 2, 2, UI_CYAN, 255);
    *x = xx - 4;
    return st;
}

/*
 * The game's own lower screen as a sheet over the world, in the one-screen
 * view: the C-Gear (its wireless and IR panels, the survey radar), and the
 * field's other lower screens (the Union Room's, the Entralink's map).  It
 * stands at 1x on the right, below the bar, in a frame; the pointer held
 * on it is the stylus.  With `closable`, a Close tab (or B, or a right
 * click, which the caller handles) puts it away.  Returns 1 when the tab
 * was clicked, -1 when the canvas is too small for it (the caller falls
 * back to the two screens).
 */
static int lower_sheet(EidosUiCtx *c, const EidosUiFrame *in, int cw, int ch,
                       int top, int closable)
{
    const int w = 256, h = 192;
    int px, py, mx, my, closed = 0;
    EidosUiRect r;

    if (in->screen_rgba[1] == NULL || cw < w + 12 || ch < top + h + (closable ? 24 : 8)) {
        return -1;
    }
    px = cw - w - 6;
    py = top + (ch - top - h) / 2 + (closable ? 8 : 0);
    ui_fill(c, px - 3, py - 3, w + 6, h + 6, UI_EDGE, 255);
    ui_fill(c, px - 2, py - 2, w + 4, h + 4, UI_CYAN, 255);
    ui_fill(c, px - 1, py - 1, w + 2, h + 2, UI_INK, 255);
    ui_blit_screen(c, px, py, in->screen_rgba[1], 0, 0, w, h, 1, 0);
    if (closable) {
        int x = px + w + 3;

        if (small_button(c, &x, py - 3 - 16, "Close", 16, "B") & EIDOS_UI_CLICK) {
            closed = 1;
        }
    }
    r.x = px;
    r.y = py;
    r.w = w;
    r.h = h;
    (void)eidos_ui_hit(r);
    if (eidos_ui_held() && eidos_ui_mouse_pos(&mx, &my) && mx >= px && my >= py
        && mx < px + w && my < py + h) {
        eidos_ui_tap(mx - px, my - py, 2);
    }
    return closed;
}

/*
 * The C-Gear's drop-down: a short sheet under the bar while the C-Gear is
 * on, the one-screen stand-in for its lower screen's home.  Three channel
 * buttons in the C-Gear's own colours (IR red, Online blue, Wireless
 * orange), each lit while that channel has someone, and a C-Gear button
 * for the game's whole lower screen (the survey, the settings).  A channel
 * button taps that channel's hexagon on the game's C-Gear, so the game
 * opens the app it opens for a stylus (the Wireless app then takes the
 * window, lower-only).  IR is greyed out and takes no click: eidos has no
 * infrared link yet.  Returns 1 to close, 2 for the whole C-Gear.
 */
static int cgear_drop(EidosUiCtx *c, const EidosUiFrame *in, int cw, int top,
                      int wireless_lit, int online_lit, int on)
{
    static const struct {
        const char *name;
        u32 colour;
        int tx, ty;                 /* the hexagon on the game's C-Gear      */
    } ch[3] = {
        { "IR", 0xE0384Au, 63, 62 },
        { "ONLINE", 0x2F7FE8u, 96, 110 },
        { "WIRELESS", 0xF08A24u, 190, 118 },
    };
    const int bw = 76, bhh = 28, gap = 4, pad = 5;
    int fullw = ui_text_w("C-GEAR", UI_FONT_SMALL) + 14;
    int w = pad * 2 + 3 * bw + 2 * gap + gap + fullw, h = bhh + pad * 2;
    int x0 = cw - w - 4, y0 = top + 2, i, res = 0;
    int lit[3];

    (void)in;
    lit[0] = 0;
    lit[1] = online_lit;
    lit[2] = wireless_lit;
    if (x0 < 2) {
        x0 = 2;
    }
    ui_panel(c, x0, y0, w, h, UI_CUT_BL | UI_CUT_BR, 5, UI_BODY, UI_EDGE, 235);
    ui_fill(c, x0 + 5, y0 + h - 1, w - 10, 1, UI_CYAN, 255);
    for (i = 0; i < 3; i++) {
        int bx = x0 + pad + i * (bw + gap), by = y0 + pad;
        EidosUiRect r = { bx, by, bw, bhh };
        /* IR is greyed out: eidos has no infrared link yet */
        int ir = i == 0, st = ir ? 0 : eidos_ui_hit(r);
        u32 col = ch[i].colour;
        const char *status = ir ? "Not available" : on ? "On" : "Off";

        ui_panel(c, bx, by, bw, bhh, UI_CUT_TL | UI_CUT_BR, 4,
                 (st & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY2, lit[i] ? col : UI_EDGE2, ir ? 150 : 245);
        ui_fill(c, bx + 4, by + 4, 3, bhh - 8, lit[i] ? col : UI_EDGE2, 255);
        ui_text(c, bx + 11, by + 3, ch[i].name, ir ? UI_GREY : lit[i] ? UI_WHITE : UI_LIGHT, UI_EDGE,
                UI_FONT_SMALL, 1);
        ui_text(c, bx + 11, by + 15, status, ir ? UI_EDGE2 : lit[i] ? col : UI_GREY, -1, UI_FONT_SMALL, 1);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_tap(ch[i].tx, ch[i].ty, 3);
            res = 1;
        }
    }
    {
        int bx = x0 + w - pad - fullw, by = y0 + pad;
        EidosUiRect r = { bx, by, fullw, bhh };
        int st = eidos_ui_hit(r);

        ui_panel(c, bx, by, fullw, bhh, UI_CUT_TL | UI_CUT_BR, 4,
                 (st & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY2, UI_CYAN2, 245);
        ui_text(c, bx + 7, by + ui_text_y(bhh, UI_FONT_SMALL), "C-GEAR", UI_WHITE, UI_EDGE,
                UI_FONT_SMALL, 1);
        if (st & EIDOS_UI_CLICK) {
            res = 2;
        }
    }
    return res;
}

static void battery(EidosUiCtx *c, int x, int y)
{
    ui_fill(c, x, y + 1, 15, 8, UI_LIGHT, 255);
    ui_fill(c, x + 1, y + 2, 13, 6, UI_INK, 255);
    ui_fill(c, x + 15, y + 3, 2, 4, UI_LIGHT, 255);
    ui_fill(c, x + 2, y + 3, 3, 4, UI_HP_G, 255);
    ui_fill(c, x + 6, y + 3, 3, 4, UI_HP_G, 255);
    ui_fill(c, x + 10, y + 3, 3, 4, UI_HP_G, 255);
}

/* The game's screen capture on the top screen's 2D: a door (and other
 * scene changes) captures the old picture and shows it as a bitmap BG2
 * (BG mode 5) blended over the new world.  That picture is the DS's
 * 256x192 only, so in the wide view it is not shown: the world (or its
 * kept picture) is. */
static int capture_bg_shown(void)
{
    u32 dispcnt = bw_io_read32(0x04000000u);

    return (dispcnt & 7u) >= 3u && (dispcnt & 0x400u) != 0u;
}

/* How much of the captured picture covers the world, 0..16: all of it
 * while the 3D layer is off (the new scene loads behind it), then the
 * first-target weight of the game's blend while it fades out. */
static int capture_mix(void)
{
    u32 dispcnt = bw_io_read32(0x04000000u), bldcnt, eva;

    if (!capture_bg_shown()) {
        return 0;
    }
    if ((dispcnt & 0x100u) == 0u) {
        return 16;
    }
    bldcnt = bw_io_read16(0x04000050u);
    if (((bldcnt >> 6) & 3u) != 1u || (bldcnt & 0x04u) == 0u) {
        return 16;
    }
    eva = bw_io_read16(0x04000052u) & 0x1Fu;
    return eva > 16u ? 16 : (int)eva;
}

/* The field's draw mode (FIELDMAP_GetDrawMode): 2 while an encounter
 * effect draws its model over a capture of the screen (ov076
 * ENCEFF_ModelSceneEventMain, ov075's screen texture). */
extern int ov021_FIELDMAP_GetDrawMode(void *fieldMap) __attribute__((weak));

static int encounter_effect(void)
{
    void *gs = (&bw_dsref__GameSystemGlobal != NULL && bw_dsref__GameSystemGlobal != NULL)
               ? *(void **)bw_dsref__GameSystemGlobal : NULL;
    void *fm = gs != NULL ? *(void **)((u8 *)gs + 0x20) : NULL;

    return fm != NULL && ov021_FIELDMAP_GetDrawMode != NULL && ov021_FIELDMAP_GetDrawMode(fm) == 2;
}

/* ------------------------------------------------------------------ */
/* The field: the stick                                                */
/* ------------------------------------------------------------------ */

/* A stick that appears where the finger (or the mouse) goes down on the
 * world and follows it: the knob shows the way, the player walks the one
 * of the four directions the drag is closest to, and runs (B, with the
 * Running Shoes) once the knob reaches the ring.  A tap that does not
 * drag is A (talk, read a sign).  The keys are pressed as the player's own
 * would be, so the game moves the player exactly as with the D-pad. */
#define STICK_R      18     /* the ring, canvas pixels                     */
#define STICK_KNOB   7
#define STICK_DEAD   5      /* a drag shorter than this walks nowhere      */
#define STICK_TAP    15u    /* frames: a shorter press with no drag is A   */

typedef struct Stick {
    int on;                 /* held, down on the world                     */
    int ax, ay;             /* where it went down                          */
    int moved;              /* the drag left the dead zone                 */
    u32 since;
} Stick;

static Stick g_stick;

/* `top`: the world starts under it (the bar); `blocked`: an event runs or
 * the bar moves, so no stick starts and a held one lets go. */
/* A card over the world (the Union Room's, the Entralink's): the rectangle
 * it took last frame, where a press is the card's, not the stick's. */
static EidosUiRect g_card;

static void field_stick(EidosUiCtx *c, const EidosUiFrame *in, int top, int blocked)
{
    int x, y, pressed, released, down, dx, dy, adx, ady, kx, ky;
    double dist;
    u32 key;

    down = eidos_ui_pointer(&x, &y, &pressed, &released);
    if (pressed && g_card.w > 0 && x >= g_card.x && x < g_card.x + g_card.w && y >= g_card.y
        && y < g_card.y + g_card.h) {
        pressed = 0;                    /* on a card: the card's            */
    }
    if (pressed && !blocked && x >= 0 && y > top) {
        g_stick.on = 1;
        g_stick.ax = x;
        g_stick.ay = y;
        g_stick.moved = 0;
        g_stick.since = in->frame;
    }
    if (!g_stick.on) {
        return;
    }
    if (!down || blocked) {
        if (released && !blocked && !g_stick.moved && in->frame - g_stick.since < STICK_TAP) {
            eidos_ui_press(BW_KEY_A, 4);
        }
        g_stick.on = 0;
        return;
    }
    dx = x - g_stick.ax;
    dy = y - g_stick.ay;
    dist = sqrt((double)dx * dx + (double)dy * dy);
    if (dist >= STICK_DEAD) {
        g_stick.moved = 1;
    }
    if (g_stick.moved && dist >= STICK_DEAD) {
        adx = dx < 0 ? -dx : dx;
        ady = dy < 0 ? -dy : dy;
        key = adx >= ady ? (dx > 0 ? BW_KEY_RIGHT : BW_KEY_LEFT) : (dy > 0 ? BW_KEY_DOWN : BW_KEY_UP);
        if (dist >= STICK_R) {
            key |= BW_KEY_B;
        }
        eidos_ui_press(key, 2);
    }
    /* the ring where it went down, the knob under the finger, kept on the
     * ring; the ring turns cyan while running */
    kx = dx;
    ky = dy;
    if (dist > STICK_R) {
        kx = (int)(dx * STICK_R / dist);
        ky = (int)(dy * STICK_R / dist);
    }
    ui_ellipse(c, g_stick.ax - STICK_R, g_stick.ay - STICK_R, 2 * STICK_R + 1, 2 * STICK_R + 1,
               UI_INK, UI_NONE, 90);
    ui_ellipse(c, g_stick.ax - STICK_R, g_stick.ay - STICK_R, 2 * STICK_R + 1, 2 * STICK_R + 1,
               UI_NONE, dist >= STICK_R ? UI_CYAN : UI_WHITE, dist >= STICK_R ? 220 : 120);
    ui_ellipse(c, g_stick.ax + kx - STICK_KNOB, g_stick.ay + ky - STICK_KNOB, 2 * STICK_KNOB + 1,
               2 * STICK_KNOB + 1, UI_WHITE, UI_NONE, 150);
    ui_ellipse(c, g_stick.ax + kx - STICK_KNOB, g_stick.ay + ky - STICK_KNOB, 2 * STICK_KNOB + 1,
               2 * STICK_KNOB + 1, UI_NONE, UI_WHITE, 230);
}

static void field_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    FieldNow f;
    int cw = c->canvas->w, ch = c->canvas->h;
    const int bh = 16;
    char place[64], clock[8];
    u32 zone = 0, t3[3] = { 0, 0, 0 };
    int have_time = 0, comm = 0, last_cmd = 0;
    EidosUiRect bar;
    int st_bar, x, open_menu = 0, use_item = 0, st, choosing;
    int wide = cw >= 400, tab_w, tab_l, tab_r;
    EidosUiRect saved_clip;

    if (getenv("EIDOS_UI_FIELD_DEBUG") != NULL) {
        int ok = field_now(&f);
        fprintf(stderr, "eidos: ui field %u ok %d main %u dispcnt %08x bldcnt %04x alpha %04x cap %08x\n", in->frame, ok, ok ? f.main_state : 0u, (unsigned)bw_io_read32(0x04000000u), (unsigned)bw_io_read16(0x04000050u), (unsigned)bw_io_read16(0x04000052u), (unsigned)bw_io_read32(0x04000064u));
    }
    /* right after the menu the field is still settling (its main state
     * is not back to running for some 20 frames): the bar stays all the
     * same, so it does not blink out between the menu and the field */
    if (!field_now(&f) || (f.main_state != 1 && in->frame - g_menu_last_frame > 90u)) {
        /* not running yet (the start sequence, the season card) or closing
         * (a zone change, a door): the world alone, no bar, so nothing
         * flashes; while the game shows no wide world (a door shows a
         * capture of its screen) its last picture stands, faded */
        view->mode = EIDOS_VIEW_WORLD;
        view->keep_world = 1;
        view->hide_2d = capture_bg_shown();
        view->cap_mix = capture_mix();
        view->stretch = encounter_effect();
        if (cutin_other(&f)) {
            /* a cut-in as the battle starts (the VS banner) */
            cutin_view(view);
        }
        return;
    }
    {
        /* EIDOS_UI_FIELD_CGEAR=<frame>: the C-GEAR button is clicked at
         * that frame (scripted runs have no pointer) */
        const char *e = getenv("EIDOS_UI_FIELD_CGEAR");

        if (e != NULL && in->frame == (u32)atoi(e) && (f.mode == 0 || f.mode == 8)) {
            g_show_cgear = 1;
        }
        e = getenv("EIDOS_UI_FIELD_CGEAR_FULL");
        if (e != NULL && in->frame == (u32)atoi(e) && (f.mode == 0 || f.mode == 8)) {
            g_show_cgear = 2;
        }
    }
    if (getenv("EIDOS_UI_EVENT_LOG") != NULL && GAMESYSTEM_IsEventActive != NULL) {
        static int last = -1;
        int ev = GAMESYSTEM_IsEventActive(f.gs) != 0;

        if (ev != last) {
            fprintf(stderr, "eidos: ui field %u event %d mode %u\n", in->frame, ev, f.mode);
            last = ev;
        }
    }
    if (g_show_cgear && f.mode != 0 && f.mode != 8) {
        g_show_cgear = 0;                   /* no C-Gear panel here         */
    }

    if (g_show_cgear == 2) {
        /* The game's own C-Gear as a sheet over the world (lower_sheet);
         * the two screens side by side only when the canvas is too small. */
        static const char *const hints[] = { "B", "Back" };
        static u32 prev_b;
        int b = (in->buttons & BW_KEY_B) != 0;
        int shut;

        view->mode = EIDOS_VIEW_WORLD;
        view->keep_world = 1;
        view->hide_2d = capture_bg_shown();
        view->cap_mix = capture_mix();
        shut = lower_sheet(c, in, cw, ch, bh, 1);
        if (shut < 0) {
            view->mode = EIDOS_VIEW_FALLBACK;
            shut = hint_bar(c, ch - ui_bar_h(c), cw, hints, 1) == 0;
        }
        if (shut || right_click(in) || (b && !prev_b)) {
            g_show_cgear = 0;
        }
        prev_b = (u32)b;
        return;
    }

    view->mode = EIDOS_VIEW_WORLD;
    view->keep_world = 1;
    view->hide_2d = capture_bg_shown();
    view->cap_mix = capture_mix();
    view->stretch = encounter_effect();

    /* While an event runs, the bar slides up out of the way and comes back
     * after.  It stays through the game's place-name banner: its own name
     * changes instead. */
    g_text_up = 0;
    g_item_card_drawn = 0;
    if (!plain_field()) {
        g_text_up = field_text(c, &f);
    }
    if (item_show(c, &f) || mon_show(c, &f)) {
        /* the game's cut-in freezes its screen (a capture behind its 3D)
         * and flashes it white: the world holds its last wide picture from
         * the cut-in's start instead, under ours, so the kept picture is
         * never the white flash */
        view->hide_2d = 1;
        view->cap_mix = 0;
        view->hold_world = 1;
    } else if (cutin_other(&f)) {
        /* another cut-in (a VS banner) */
        cutin_view(view);
    }
    choosing = !plain_field() && field_choices(c, in, &f);
    if (choosing) {
        /* a choice list is open: its own clicks (field_choices) */
    } else if (GAMESYSTEM_IsEventActive != NULL && GAMESYSTEM_IsEventActive(f.gs) != 0) {
        /* An event (a talk, a scene): the game's speech bubbles go on
         * with A or B only (FIELD_MSGBG_KeyWaitCheckAdvance), so a click
         * on the world is A and a right click is B. */
        EidosUiRect all = { 0, 0, cw, ch };

        if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 4);
        } else if (right_click(in)) {
            eidos_ui_press(BW_KEY_B, 4);
        }
    }
    field_stick(c, in, bh - g_bar_slide,
                GAMESYSTEM_IsEventActive != NULL && GAMESYSTEM_IsEventActive(f.gs) != 0);
    /* Opening and closing the menu are events too: right after the menu
     * the bar is simply there, fading in with the menu fading out. */
    if (in->frame - g_menu_last_frame <= 90u) {
        g_bar_slide = 0;
    } else if (choosing || g_text_up || (GAMESYSTEM_IsEventActive != NULL
                                               && GAMESYSTEM_IsEventActive(f.gs) != 0)) {
        /* a list or a text box (the Union Room's talks run with no
         * event; its battle rule card stands at the top of the screen):
         * the bar makes room for them too */
        g_bar_slide = g_bar_slide + 3 < bh + 1 ? g_bar_slide + 3 : bh + 1;
    } else {
        g_bar_slide = g_bar_slide > 2 ? g_bar_slide - 2 : 0;
    }
    if (g_bar_slide >= bh + 1) {
        return;
    }

    /* The place and the time, from the game. */
    zone = rd16(f.fm + 0xD8);
    place[0] = '\0';
    if (ZONEDATA_GetPlaceNameId != NULL) {
        msg_text(0x59, ZONEDATA_GetPlaceNameId(zone), place, (int)sizeof(place));
        if (getenv("EIDOS_UI_FIELD_DEBUG") != NULL && (in->frame % 100) == 0) {
            fprintf(stderr, "eidos: ui field: zone %u place id %u '%s'\n", zone,
                    ZONEDATA_GetPlaceNameId(zone), place);
        }
    }
    if (GF_STANDARD_GetCachedTime != NULL) {
        GF_STANDARD_GetCachedTime(t3);
        have_time = t3[0] < 24 && t3[1] < 60;
    }
    if (!have_time) {
        time_t tt = time(NULL);
        struct tm tmb;
        struct tm *tmv = bw_localtime((long long)tt, &tmb) == 0 ? &tmb : NULL;

        if (tmv != NULL) {
            t3[0] = (u32)tmv->tm_hour;
            t3[1] = (u32)tmv->tm_min;
            t3[2] = (u32)tmv->tm_sec;
        }
    }
    snprintf(clock, sizeof(clock), "%02u%c%02u", t3[0] % 100u, (t3[2] & 1u) ? ' ' : ':',
             t3[1] % 100u);
    if (f.panel != NULL && (f.mode == 0 || f.mode == 8)) {
        comm = f.panel[0x45A] != 0;
        last_cmd = (int)rd32(f.panel + 0x218);
    }

    /* The bar. */
    ui_push(c, 0, -g_bar_slide, &saved_clip);
    bar.x = 0;
    bar.y = 0;
    bar.w = cw;
    bar.h = bh;
    st_bar = eidos_ui_hit(bar);
    ui_fill(c, 0, 0, cw, bh, UI_INK, st_bar & EIDOS_UI_HOVER ? 205 : 170);
    ui_fill(c, 0, bh, cw, 1, UI_EDGE, 200);

    /* The time on its slanted tab in the middle, Menu right of it, Item
     * and the C-Gear left of it: the buttons keep their places whatever
     * the width, and never run under the time.  Buttons come before the
     * bar's own click, so a click on one is not also a click on the bar. */
    tab_w = ui_text_w("00:00", UI_FONT_LARGE) + 24;
    tab_l = cw / 2 - tab_w / 2;
    tab_r = cw / 2 + tab_w / 2;
    x = tab_r + 6 + small_button_w("Menu", "X");
    st = small_button(c, &x, 0, "Menu", bh, "X");
    if (st & EIDOS_UI_CLICK) {
        open_menu = 1;
    }
    if (st & EIDOS_UI_HOVER) {
        st_bar = 0;
    }

    /* the right group, from the right edge: the battery and the three
     * link lights, their names only while they clear Menu */
    x = cw - 6 - c->safe_r;     /* clear of a phone's notch and corners */
    battery(c, x - 17, 3);
    x -= 24;
    {
        static const char *const names[3] = { "WIRELESS", "ONLINE", "IR" };
        int lit[3];
        int i, named = wide, need = 0;

        for (i = 0; i < 3; i++) {
            need += ui_text_w(names[i], UI_FONT_SMALL) + 13;
        }
        if (x - need < tab_r + 6 + small_button_w("Menu", "X") + 8) {
            named = 0;
        }
        lit[0] = comm;
        lit[1] = comm && last_cmd == 5;
        lit[2] = comm && last_cmd == 1;
        for (i = 0; i < 3; i++) {
            int tw = named ? ui_text_w(names[i], UI_FONT_SMALL) : 0;
            int ty = ui_text_y(bh, UI_FONT_SMALL);

            x -= tw;
            if (named) {
                ui_text(c, x, ty, names[i], lit[i] ? UI_WHITE : UI_EDGE2, -1, UI_FONT_SMALL, 1);
            }
            x -= 7;
            ui_fill(c, x, bh / 2 - 1, 3, 3, lit[i] ? UI_CYAN : UI_EDGE2, 255);
            x -= 6;
        }
    }

    /* left of the time: Y, the registered item (the Bicycle, the Dowsing
     * Machine: a touch screen has no key for it), then the C-Gear */
    x = tab_l - 6;
    st = small_button(c, &x, 0, "Item", bh, "Y");
    if (st & EIDOS_UI_CLICK) {
        use_item = 1;
    }
    if (st & EIDOS_UI_HOVER) {
        st_bar = 0;
    }
    if (f.mode == 0 || f.mode == 8) {           /* this save has a C-Gear */
        /* a narrow window (an iPad): when the place name would not fit
         * beside the full button, the button is the C-Gear's mark alone */
        int name_room = x - small_button_w("C-GEAR", NULL) - 4 - 2 - (14 + c->safe_l);

        if (ui_text_w(place, UI_FONT_LARGE) > name_room) {
            st = cgear_mark_button(c, &x, bh);
        } else {
            st = small_button(c, &x, 0, "C-GEAR", bh, NULL);
        }
        if (st & EIDOS_UI_CLICK) {
            g_show_cgear = g_show_cgear ? 0 : 1;
            st_bar = 0;
        } else if (st & EIDOS_UI_HOVER) {
            st_bar = 0;
        }
    }

    /* the left: the cursor mark and the place, cut off before the
     * buttons */
    {
        EidosUiRect saved;
        int i, px = 14 + c->safe_l;

        for (i = 0; i < 4; i++) {
            ui_fill(c, 5 + c->safe_l + i, 4 + i, 1, 9 - 2 * i, UI_CYAN, 255);
        }
        if (strcmp(place, g_bar_name) != 0) {
            if (g_bar_name[0] != '\0') {
                snprintf(g_bar_prev, sizeof g_bar_prev, "%s", g_bar_name);
                g_bar_fade = 0;
            }
            snprintf(g_bar_name, sizeof g_bar_name, "%s", place);
        }
        if (x - 2 > px) {
            const char *shown = place;
            int a = 255;

            if (g_bar_fade < NAME_OUT + NAME_IN) {
                shown = g_bar_fade < NAME_OUT ? g_bar_prev : place;
                a = g_bar_fade < NAME_OUT ? 255 - (int)(g_bar_fade * 255u / NAME_OUT)
                                          : (int)((g_bar_fade - NAME_OUT) * 255u / NAME_IN);
                g_bar_fade++;
            }
            /* a name still too long for the room takes the small font */
            int font = ui_text_w(shown, UI_FONT_LARGE) > x - 2 - px ? UI_FONT_SMALL : UI_FONT_LARGE;

            ui_push(c, 0, 0, &saved);
            ui_clip(c, px, 0, x - 2 - px, bh);
            ui_text(c, px, ui_text_y(bh, font), shown, mix_rgb(UI_WHITE, BAR_INK, a),
                    a < 128 ? (s32)BAR_INK : (s32)UI_EDGE, font, 1);
            ui_pop(c, 0, 0, &saved);
        }
    }
    /* the middle: the time on its slanted tab */
    bar_trapezoid(c, cw / 2, tab_w, bh - 1, st_bar & EIDOS_UI_HOVER);
    ui_text(c, cw / 2 - ui_text_w(clock, UI_FONT_LARGE) / 2,
            ui_text_y(bh - 1, UI_FONT_LARGE), clock, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    ui_pop(c, 0, -g_bar_slide, &saved_clip);
    if (g_bar_slide > 0) {
        return;                             /* no clicks while it moves     */
    }
    if (g_show_cgear == 1) {
        static u32 prev_b;
        int b = (in->buttons & BW_KEY_B) != 0;
        int what = cgear_drop(c, in, cw, bh, comm, comm && last_cmd == 5, comm);

        if (what == 1 || right_click(in) || (b && !prev_b)) {
            g_show_cgear = 0;
        } else if (what == 2) {
            g_show_cgear = 2;
        }
        prev_b = (u32)b;
        return;                             /* the bar's clicks wait        */
    }
    if ((st_bar & EIDOS_UI_CLICK) || open_menu) {
        eidos_ui_press(BW_KEY_X, 10);
    }
    if (use_item) {
        eidos_ui_press(BW_KEY_Y, 10);
    }
}

static void field_reset(void)
{
    g_show_cgear = 0;
    g_bar_slide = BAR_HIDDEN;
    g_stick.on = 0;
}

static const EidosUiRecipe recipe_field = {
    "field", field_match, field_run, field_reset, 0
};

/* ------------------------------------------------------------------ */
/* The field's other lower screens                                     */
/* ------------------------------------------------------------------ */

/*
 * field-lower: the field runs and its lower screen is neither the C-Gear,
 * the no-gear panel nor the menu (the Union Room, the Entralink's map,
 * dowsing and the rest the game puts there).  The world fills the window
 * and the game's lower screen is a sheet over it (lower_sheet), the
 * pointer as the stylus; a right click is B.  The save report (mode 7) has
 * its own recipe.
 */
static int lower_match(void)
{
    FieldNow f;

    if (!field_now(&f) || f.main_state != 1) {
        return 0;
    }
    if (f.mode == 0 || f.mode == 1 || f.mode == 5 || f.mode == 7 || f.mode == 8) {
        return 0;
    }
    logf_scene("field-lower");
    {
        u8 *l, *w;

        if (ui_textbox_field_active(f.fm) || f.mode == 9 || choices_now(&f, &l, &w)) {
            eidos_ui_want_ds_scale();
        }
    }
    return 1;
}

static void lower_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    FieldNow f;
    int own;

    /* the field as everywhere else: the world, the top bar, the stick, the
     * game's text; then the lower screen's own card over it */
    g_card.w = 0;
    field_run(c, view, in);
    if (view->mode != EIDOS_VIEW_WORLD || !field_now(&f) || f.main_state != 1) {
        return;
    }
    /* the Union Room and the Entralink: their own cards; the other lower
     * screens: the game's screen as a sheet */
    {
        u8 *l, *w;

        if (f.mode == 2 && choices_now(&f, &l, &w)) {
            /* a list is open (the talk menu, a YES / NO): the card would
             * cover it (field_run drew the list) */
            return;
        }
        if (f.mode == 2 && (g_text_up || (ov012_UNION_MAIN_IsIdle != NULL && bw_overlay_is_loaded(12)
                                           && ov012_UNION_MAIN_IsIdle(f.gs) == 0))) {
            /* a talk runs (a request, with the battle's rule card the game
             * draws on its screen): the card steps aside until it ends */
            return;
        }
    }
    own = plain_field() ? 0 : (f.mode == 2 && union_card(c, in, &f)) || (f.mode == 3 && entralink_card(c, in))
          || (f.mode == 9 && power_card(c, &f));
    if (!own && lower_sheet(c, in, c->canvas->w, c->canvas->h, 16, 0) < 0) {
        view->mode = EIDOS_VIEW_FALLBACK;
    }
}

/* ------------------------------------------------------------------ */
/* The C-Gear's power                                                  */
/* ------------------------------------------------------------------ */

/*
 * The C-Gear's power on/off question (field sub-screen mode 9, ov035
 * c_gear_power_onoff.c; mock-up "cgear_prompt"): the question as a system
 * box at the bottom, the game's note above it, its YES / NO as a system
 * list.  The game takes no keys for that menu, so the list's own cursor
 * moves with them and A taps the row.  After the answer the game waits for
 * a touch on its message: a click on the box is that touch.
 *
 * CGearPowerOnOffWork (ov035/c_gear_power_onoff_types.h): messageWindow
 * 0x18, menuWindow 0x1C (the note), taskMenu 0x44.
 */
static int power_card(EidosUiCtx *c, const FieldNow *f)
{
    const u8 *w = f->panel;
    const void *msg, *note;
    void *menu;
    int cw = c->canvas->w, ch = c->canvas->h, bh, bw, x, y, nw = 0, nh = 0;

    if (w == NULL) {
        return 0;
    }
    msg = rdp(w + 0x18);
    note = rdp(w + 0x1C);
    menu = rdp(w + 0x44);
    {
        int mw2 = 0, mh2 = 0;

        /* the box as tall as the words (the game's window has room for a
         * longer message) */
        if (!ui_letters(NULL, ui_bmpwin_bmp(msg), 0, 0, 0, 0, &mw2, &mh2)) {
            return 1;                   /* nothing printed yet             */
        }
        bh = mh2 + 14;
        bw = cw - 8 < 256 ? cw - 8 : 256;
        x = (cw - bw) / 2;
        y = ch - 4 - bh;
        ui_textbox_frame(c, UI_TEXTBOX_SYSTEM, x, y, bw, bh);
        ui_letters(c, ui_bmpwin_bmp(msg), x + 12, y + 7, UI_WHITE, 0x464850u, NULL, NULL);
    }
    if (note != NULL && ui_letters(NULL, ui_bmpwin_bmp(note), 0, 0, 0, 0, &nw, &nh)) {
        /* the note: a quieter panel above the box, at its left */
        ui_panel(c, x, y - nh - 18, nw + 20, nh + 14, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, 0x464850u, 220);
        ui_letters(c, ui_bmpwin_bmp(note), x + 10, y - nh - 11, UI_LIGHT, 0x464850u, NULL, NULL);
    }
    if (menu != NULL) {
        ui_taskmenu(c, menu, x + bw, y - 3, UI_CHOICE_SYSTEM);
    } else {
        EidosUiRect r;

        r.x = x; r.y = y; r.w = bw; r.h = bh;
        if (eidos_ui_hit(r) & EIDOS_UI_CLICK) {
            eidos_ui_tap(128, 96, 2);
        }
    }
    g_card.x = x;
    g_card.y = y - nh - 18;
    g_card.w = bw;
    g_card.h = ch - g_card.y;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The Union Room                                                      */
/* ------------------------------------------------------------------ */

/*
 * The Union Room's lower screen (field sub-screen mode 2, ov031
 * union_subdisp.c) as a card on the right of the world (mock-up u1): the
 * chat log, the newest lines last, and the game's five buttons under it
 * (Talk, Battle, Trade, Draw, Spin Trade) as battle-style tiles with the
 * game's own icons.  A tile taps the game's button where the game drew it,
 * so the game does what it does for the stylus.
 *
 * The card can be put away so it does not hide the room: its Hide tab, or
 * SELECT, puts it away; a Chat tab at the right edge, or SELECT, brings it
 * back.  With the keys, SELECT first gives the card the keys: LEFT and
 * RIGHT move along the tiles, A presses the chosen one, B gives the keys
 * back to the room; SELECT again puts the card away.
 *
 * Records: UnionRoomWork (GAME_COMM_GetWork) chatLog 0x30E0, its entries
 * 0x24 bytes (name u16[8] 0x00, PMS 0x10, chatCount 0x18, sex 0x20,
 * chatType 0x21, relation 0x22), newestIndex 0x43C (the absolute index of
 * the newest line, -1 empty); the line of index i is entry[i % 30].  Fixed
 * lines are a/0/0/2 file 201 (chatType 1: 6, 2..5: 0..3; 4 FRIEND, 5
 * PLAYER).  UnionSubdispWork appealNo 0x30 (0: no button held down).
 */
extern void *GAMESYSTEM_GetGameComm(const void *gameSystem) WEAK;
extern void *GAME_COMM_GetWork(const void *comm) WEAK;
extern void *PMSI_PARAM_ToString(const void *sentence, unsigned int heapId) WEAK;
extern void  STRBUF_Delete(void *strbuf) WEAK;
extern u16  *STRBUF_GetChars(void *strbuf) WEAK;
extern u16   STRBUF_GetLength(void *strbuf) WEAK;
extern u32   ov021_FIELDMAP_GetHeapId(void *fieldMap) WEAK;

#define UNION_LINES 4

static int g_union_shown = 1;       /* the card is out                      */
static int g_union_keys;            /* the card has the keys                */
static int g_union_focus = 1;       /* the tile the keys are on             */
static int g_union_pressed = -1;    /* the tile last pressed                */

/* A chat sentence's words, made on the game's thread (its heap). */
typedef struct UnionPms {
    s32 index;
    u16 count;
    char text[96];
} UnionPms;

static UnionPms g_union_pms[UNION_LINES] = { { -1, 0, "" }, { -1, 0, "" }, { -1, 0, "" }, { -1, 0, "" } };
static volatile int g_union_pms_busy;

typedef struct UnionPmsJob {
    void *fm;
    const u8 *entry;
    int slot;
    s32 index;
} UnionPmsJob;

static UnionPmsJob g_union_job;

static void union_pms_make(void *arg)
{
    UnionPmsJob *j = (UnionPmsJob *)arg;
    void *sb;

    g_union_pms[j->slot].text[0] = '\0';
    if (PMSI_PARAM_ToString != NULL && ov021_FIELDMAP_GetHeapId != NULL && STRBUF_GetChars != NULL
        && STRBUF_GetLength != NULL) {
        sb = PMSI_PARAM_ToString(j->entry + 0x10, ov021_FIELDMAP_GetHeapId(j->fm));
        if (sb != NULL) {
            units_to_utf8(STRBUF_GetChars(sb), STRBUF_GetLength(sb), g_union_pms[j->slot].text,
                          (int)sizeof g_union_pms[j->slot].text);
            if (STRBUF_Delete != NULL) {
                STRBUF_Delete(sb);
            }
        }
    }
    g_union_pms[j->slot].index = j->index;
    g_union_pms[j->slot].count = rd16(j->entry + 0x18);
    g_union_pms_busy = 0;
}

/* The words of chat line `index` (entry `e`), into out. */
static void union_line_text(void *fm, const u8 *e, s32 index, int slot, char *out, int n)
{
    static const u8 fixed[6] = { 0, 6, 0, 1, 2, 3 };
    u8 type = e[0x21];

    out[0] = '\0';
    if (type >= 1 && type <= 5) {
        char *p;

        msg_text(201, fixed[type], out, n);
        for (p = out; *p != '\0'; p++) {
            if (*p == '\n') {
                *p = ' ';
            }
        }
        return;
    }
    if (g_union_pms[slot].index == index && g_union_pms[slot].count == rd16(e + 0x18)) {
        snprintf(out, (size_t)n, "%s", g_union_pms[slot].text);
        return;
    }
    if (!g_union_pms_busy) {
        g_union_pms_busy = 1;
        g_union_job.fm = fm;
        g_union_job.entry = e;
        g_union_job.slot = slot;
        g_union_job.index = index;
        if (!eidos_ui_game_call(union_pms_make, &g_union_job)) {
            g_union_pms_busy = 0;
        }
    }
}

/* Wraps s into lines of at most w pixels; returns the count. */
static int union_wrap(const char *s, int w, char lines[][96], int max)
{
    int n = 0;
    char cur[96] = "";
    const char *p = s;

    while (*p != '\0' && n < max) {
        char word[48];
        char t[160];
        int k = 0;

        while (*p == ' ') {
            p++;
        }
        while (*p != '\0' && *p != ' ' && k < 47) {
            word[k++] = *p++;
        }
        word[k] = '\0';
        if (k == 0) {
            break;
        }
        snprintf(t, sizeof t, "%s%s%s", cur, cur[0] ? " " : "", word);
        if (cur[0] != '\0' && ui_text_w(t, UI_FONT_LARGE) > w) {
            snprintf(lines[n++], 96, "%s", cur);
            snprintf(cur, sizeof cur, "%s", word);
        } else {
            snprintf(cur, sizeof cur, "%s", t);
        }
    }
    if (cur[0] != '\0' && n < max) {
        snprintf(lines[n++], 96, "%s", cur);
    }
    return n;
}

/* The cards' window, as the interface's windows (the trade, the Spin
 * Trade's lobby): navy, cut corners, the title in light blue. */
#define CARD_BODY   0x060E1Au
#define CARD_EDGE   0x103C5Au
#define CARD_TITLE  0x62BEFFu
#define CARD_HL     0x008BACu
#define CARD_TILE   0x0B1E34u

static void card_window(EidosUiCtx *c, int x, int y, int w, int h, const char *title)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 10, CARD_BODY, CARD_EDGE, 240);
    ui_text(c, x + 12, y + 6, title, CARD_TITLE, 0x313131u, UI_FONT_SMALL, 1);
    ui_fill(c, x + 10, y + 19, w - 20, 1, CARD_EDGE, 255);
}

static int union_card(EidosUiCtx *c, const EidosUiFrame *in, const FieldNow *f)
{
    static const char *const names[5] = { "Talk", "Battle", "Trade", "Draw", "Spin" };
    static const int bx[5] = { 32, 80, 128, 176, 224 };
    u8 *uw, *log;
    int cw = c->canvas->w;
    int pw = cw / 2 - 8 > 196 ? 196 : cw / 2 - 8;
    int x0 = cw - 6 - pw, top = 22, y, i, tw, chh;
    s32 newest;
    u32 keys = eidos_ui_keys_pressed();
    EidosUiRect r;
    char text[UNION_LINES][160], wrapped[UNION_LINES][3][96];
    int nwrap[UNION_LINES], nl = 0;
    const u8 *ent[UNION_LINES];

    if (GAMESYSTEM_GetGameComm == NULL || GAME_COMM_GetWork == NULL
        || (uw = (u8 *)GAME_COMM_GetWork(GAMESYSTEM_GetGameComm(f->gs))) == NULL) {
        return 0;
    }
    log = uw + 0x30E0;
    newest = (s32)rd32(log + 0x43C);
    eidos_ui_want_objs(1);

    /* SELECT: the card out, then the keys to it, then put away */
    eidos_ui_take_keys(BW_KEY_SELECT | (g_union_keys ? (BW_KEY_A | BW_KEY_B | BW_KEY_LEFT | BW_KEY_RIGHT
                                                         | BW_KEY_UP | BW_KEY_DOWN) : 0u));
    if (keys & BW_KEY_SELECT) {
        if (!g_union_shown) {
            g_union_shown = 1;
            g_union_keys = 1;
        } else if (!g_union_keys) {
            g_union_keys = 1;
        } else {
            g_union_shown = 0;
            g_union_keys = 0;
        }
    }
    if (!g_union_shown) {
        /* the Chat tab at the right edge */
        int tx = cw - 4;
        int st = small_button(c, &tx, top + 4, "Chat", 17, "SELECT");

        if (st & EIDOS_UI_CLICK) {
            g_union_shown = 1;
        }
        return 1;
    }

    /* the lines: up to four, the newest last */
    for (i = newest; i >= 0 && i > newest - UNION_LINES; i--) {
        const u8 *e = log + 0x24 * (u32)(i % 30);
        int k = newest - i;

        ent[k] = e;
        union_line_text(f->fm, e, i, k, text[k], (int)sizeof text[k]);
        nwrap[k] = union_wrap(text[k], pw - 20, wrapped[k], 3);
        nl++;
    }
    chh = 22;
    for (i = 0; i < nl; i++) {
        chh += 11 + 16 * (nwrap[i] > 0 ? nwrap[i] : 1) + 4;
    }
    chh += 2;
    card_window(c, x0, top, pw, chh, "UNION ROOM");
    {
        /* Hide: the card goes away so the room shows */
        int hx = x0 + pw - 6;
        int st = small_button(c, &hx, top + 4, "Hide", 13, NULL);

        if (st & EIDOS_UI_CLICK) {
            g_union_shown = 0;
            g_union_keys = 0;
        }
    }
    y = top + 23;
    for (i = nl - 1; i >= 0; i--) {
        const u8 *e = ent[i];
        char name[24];
        int j;

        units_to_utf8((const u16 *)e, 8, name, (int)sizeof name);
        tw = ui_text(c, x0 + 10, y, name, e[0x20] ? 0xFF8C78u : 0x62BEFFu, 0x313131u, UI_FONT_SMALL, 1);
        if (e[0x22] == 1 || e[0x22] == 2) {
            char tag[24];

            msg_text(201, e[0x22] == 1 ? 4 : 5, tag, (int)sizeof tag);
            ui_text(c, tw + 5, y, tag, UI_GREY, 0x313131u, UI_FONT_SMALL, 1);
        }
        y += 11;
        for (j = 0; j < nwrap[i]; j++) {
            ui_text(c, x0 + 10, y + ui_text_y(16, UI_FONT_LARGE), wrapped[i][j], UI_WHITE, 0x313131u,
                    UI_FONT_LARGE, 1);
            y += 16;
        }
        if (nwrap[i] == 0) {
            y += 16;
        }
        y += 4;
    }

    /* the five buttons */
    {
        int tile = (pw - 4 * 3) / 5, by = top + chh + 4;

        g_card.x = x0;
        g_card.y = top;
        g_card.w = pw;
        g_card.h = chh + 4 + 34;
        int appeal = f->panel != NULL ? f->panel[0x30] : 0;

        if (g_union_keys) {
            if (keys & BW_KEY_LEFT) {
                g_union_focus = (g_union_focus + 4) % 5;
            }
            if (keys & BW_KEY_RIGHT) {
                g_union_focus = (g_union_focus + 1) % 5;
            }
            if (keys & BW_KEY_B) {
                g_union_keys = 0;
            }
        }
        for (i = 0; i < 5; i++) {
            int lit = i == g_union_pressed && appeal != 0;
            int focus = g_union_keys && i == g_union_focus;
            int tx = x0 + i * (tile + 3), ty = by - (lit ? 1 : 0), st, rr;
            u32 col = lit || focus ? CARD_HL : CARD_EDGE;

            r.x = tx;
            r.y = by;
            r.w = tile;
            r.h = 34;
            st = eidos_ui_hit(r);
            if (st & EIDOS_UI_HOVER) {
                col = CARD_HL;
            }
            ui_panel(c, tx, ty, tile, 34, UI_CUT_TL | UI_CUT_BR, 5,
                     lit ? 0x05343Eu : ((st & EIDOS_UI_HOVER) || focus) ? 0x0E2A44u : CARD_TILE,
                     lit ? CARD_HL : ((st & EIDOS_UI_HOVER) || focus) ? UI_WHITE : CARD_EDGE, 245);
            for (rr = 3; rr < 34 - 3; rr++) {
                int left = 3 + (rr < 7 ? 7 - rr : 0);

                if (left < 6) {
                    ui_fill(c, tx + left, ty + rr, 6 - left, 1, col, 255);
                }
            }
            if (in->objs_rgba[1] != NULL) {
                /* the game's icon, without its button frame */
                int sx, sy;

                for (sy = 0; sy < 14; sy++) {
                    for (sx = 0; sx < 16; sx++) {
                        const u8 *p = in->objs_rgba[1]
                                      + ((size_t)(175 + sy) * 256 + (size_t)(bx[i] - 8 + sx)) * 4;

                        if (p[3] != 0 && p[0] + p[1] + p[2] > 300) {
                            ui_fill(c, tx + (tile - 16) / 2 + 2 + sx, ty + 4 + sy, 1, 1,
                                    (u32)p[0] << 16 | (u32)p[1] << 8 | p[2], 255);
                        }
                    }
                }
            }
            ui_text(c, tx + (tile - ui_text_w(names[i], UI_FONT_SMALL)) / 2 + 2, ty + 22, names[i],
                    lit ? UI_WHITE : UI_LIGHT, 0x313131u, UI_FONT_SMALL, 1);
            if ((st & EIDOS_UI_CLICK) || (focus && (keys & BW_KEY_A))) {
                eidos_ui_tap(bx[i], 181, 3);
                g_union_pressed = i;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The Entralink                                                       */
/* ------------------------------------------------------------------ */

/*
 * The Entralink's lower screen (field sub-screen mode 3, ov018
 * intrude_subdisp.c) as a card on the right of the world (mock-up
 * "entralink"): the game's own map, without the DS frame around it, in a
 * card of the Union Room card's family.  A click or a tap on the map taps
 * the game's map at the same point, so the game does what it does for the
 * stylus.  The game's messages there are field windows, drawn as the
 * interface's boxes by field_text.
 *
 * The card goes away and comes back as the Union Room card does: its Hide
 * tab and a Map tab, or SELECT on the keys, a pad or a keyboard.
 */
#define ENTRA_SX 22         /* the map on the game's lower screen          */
#define ENTRA_SY 26
#define ENTRA_W  214
#define ENTRA_H  120

static int g_entra_shown = 1;

#define ENTRA_DW (ENTRA_W * 2 / 3)    /* the map at two thirds          */
#define ENTRA_DH (ENTRA_H * 2 / 3)

static int entralink_card(EidosUiCtx *c, const EidosUiFrame *in)
{
    int cw = c->canvas->w;
    int pw = ENTRA_DW + 14, ph = 22 + ENTRA_DH + 8;
    int x0 = cw - 6 - pw, top = 22, st;
    u32 keys = eidos_ui_keys_pressed();
    EidosUiRect r;

    if (in->screen_rgba[1] == NULL) {
        return 0;
    }
    eidos_ui_take_keys(BW_KEY_SELECT);
    if (keys & BW_KEY_SELECT) {
        g_entra_shown = !g_entra_shown;
    }
    if (!g_entra_shown) {
        int tx = cw - 4;

        st = small_button(c, &tx, top + 4, "Map", 17, "SELECT");
        if (st & EIDOS_UI_CLICK) {
            g_entra_shown = 1;
        }
        return 1;
    }
    card_window(c, x0, top, pw, ph, "OWN WORLD");
    {
        int hx = x0 + pw - 6;

        st = small_button(c, &hx, top + 4, "Hide", 13, NULL);
        if (st & EIDOS_UI_CLICK) {
            g_entra_shown = 0;
        }
    }
    {
        /* the game's map, every third pixel of a row and column left out */
        int dx, dy;

        for (dy = 0; dy < ENTRA_DH; dy++) {
            for (dx = 0; dx < ENTRA_DW; dx++) {
                const u8 *p = in->screen_rgba[1]
                              + ((size_t)(ENTRA_SY + dy * 3 / 2) * 256 + (size_t)(ENTRA_SX + dx * 3 / 2)) * 4;

                ui_fill(c, x0 + 7 + dx, top + 21 + dy, 1, 1, (u32)p[0] << 16 | (u32)p[1] << 8 | p[2], 255);
            }
        }
    }
    g_card.x = x0;
    g_card.y = top;
    g_card.w = pw;
    g_card.h = ph;
    r.x = x0 + 7;
    r.y = top + 21;
    r.w = ENTRA_DW;
    r.h = ENTRA_DH;
    st = eidos_ui_hit(r);
    if (eidos_ui_held() && (st & EIDOS_UI_HOVER)) {
        int mx, my;

        if (eidos_ui_mouse_pos(&mx, &my)) {
            eidos_ui_tap(ENTRA_SX + (mx - r.x) * 3 / 2, ENTRA_SY + (my - r.y) * 3 / 2, 2);
        }
    }
    return 1;
}

static void lower_reset(void)
{
}

static const EidosUiRecipe recipe_lower = {
    "field-lower", lower_match, lower_run, lower_reset, 0
};

const EidosUiRecipe *const ui_recipes_field[] = { &recipe_menu, &recipe_field, &recipe_lower };
const u32 ui_recipes_field_count = 3;

/* ------------------------------------------------------------------ */
/* Apps that use the lower screen alone                                */
/* ------------------------------------------------------------------ */

/*
 * lower-only, the last recipe of all: no other recipe knows the scene, the
 * game has blanked the upper screen and draws on the lower one (the
 * C-Gear's Wireless and IR apps, the Xtransceiver's, their prompts).  The
 * lower screen fills the window instead of standing small beside a black
 * one: the largest whole scale that fits, centred, the pointer as the
 * stylus, a right click B.  It takes the scene after three such frames, so
 * a fade that darkens the upper screen first does not flicker to it.
 */
static int dark_screen(const u8 *s)
{
    int x, y;

    for (y = 4; y < 192; y += 9) {
        for (x = 3; x < 256; x += 7) {
            const u8 *p = s + ((size_t)y * 256 + (size_t)x) * 4;

            if (p[0] > 12 || p[1] > 12 || p[2] > 12) {
                return 0;
            }
        }
    }
    return 1;
}

static int busy_screen(const u8 *s)
{
    int x, y, n = 0;
    u32 first = 0;

    for (y = 4; y < 192; y += 9) {
        for (x = 3; x < 256; x += 7) {
            const u8 *p = s + ((size_t)y * 256 + (size_t)x) * 4;
            u32 v = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];

            if (n++ == 0) {
                first = v;
            } else if (v != first) {
                return 1;
            }
        }
    }
    return 0;
}

static int g_lower_frames;

static int lower_only_match(void)
{
    const EidosUiFrame *in = eidos_ui_frame_in();

    if (in == NULL || in->screen_rgba[0] == NULL || in->screen_rgba[1] == NULL
        || !dark_screen(in->screen_rgba[0]) || !busy_screen(in->screen_rgba[1])) {
        g_lower_frames = 0;
        return 0;
    }
    if (++g_lower_frames < 3) {
        return 0;
    }
    logf_scene("lower-only");
    return 1;
}

static void lower_only_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    int cw = c->canvas->w, ch = c->canvas->h;
    float k = (float)ch / 192.0f;
    int sw = (int)(256.0f * k + 0.5f);
    int px = (cw - sw) / 2, mx, my;

    /* the game's lower screen is the world screen: the window's height,
     * centred, nothing of the 3D beside it */
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 1;
    view->ds_only = 1;
    view->lower_only = 1;
    if (eidos_ui_held() && eidos_ui_mouse_pos(&mx, &my) && mx >= px && mx < px + sw
        && my >= 0 && my < ch) {
        eidos_ui_tap((int)((float)(mx - px) / k), (int)((float)my / k), 2);
    }
    if (right_click(in)) {
        eidos_ui_press(BW_KEY_B, 4);
    }
}

static void lower_only_reset(void)
{
    g_lower_frames = 0;
}

static const EidosUiRecipe recipe_lower_only = {
    "lower-only", lower_only_match, lower_only_run, lower_only_reset, 0
};

/*
 * The whiteout's message ("... scurried to a Pokemon Center ..."): the
 * game's whiteout event (ov010 SCRCMD_NETWORK_WhiteOutEventMain) writes it
 * on its upper screen over black while no field map runs, the lower screen
 * black.  It is that one screen at the window's height, centred, black
 * beside it, as the DS shows it; before, no recipe took it and the two DS
 * screens showed side by side.  The nurse's (or the mother's) heal that
 * follows runs in the field, under the field's own recipe.
 */
extern int ov010_SCRCMD_NETWORK_WhiteOutEventMain(void *event, u32 *seq, void *work) WEAK;
extern void *GAMESYSTEM_GetEvent(void *gameSystem) WEAK;

static int whiteout_match(void)
{
    const u8 *gs, *ev;
    int i;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL
        || GAMESYSTEM_GetEvent == NULL || ov010_SCRCMD_NETWORK_WhiteOutEventMain == NULL) {
        return 0;
    }
    gs = *(const u8 **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || rdp(gs + 0x20) != NULL) {
        return 0;                   /* a field map runs: the field's recipes */
    }
    ev = (const u8 *)GAMESYSTEM_GetEvent((void *)gs);
    for (i = 0; i < 6 && ev != NULL; i++) {
        if (*(void *const *)(ev + sizeof(void *)) == (void *)ov010_SCRCMD_NETWORK_WhiteOutEventMain) {
            logf_scene("whiteout");
            return 1;
        }
        ev = *(const u8 *const *)ev;
    }
    return 0;
}

static void whiteout_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    float s, ox, oy;

    (void)in;
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;         /* the upper screen holds the message */
    view->ds_only = 1;
    /* the game's wireless signal icon (GFL_NET's, top right of its screen
     * while the radio is on): on the field the bar covers it; here the
     * screen is black around the words, so its corner is black too */
    world_place(c, &s, &ox, &oy);
    ui_fill(c, (int)(ox + 236.0f * s), (int)oy, (int)(20.0f * s) + 1, (int)(20.0f * s) + 1, 0x000000u, 255);
}

static const EidosUiRecipe recipe_whiteout = {
    "whiteout", whiteout_match, whiteout_run, NULL, 0
};

const EidosUiRecipe *const ui_recipes_lower[] = { &recipe_whiteout, &recipe_lower_only };
const u32 ui_recipes_lower_count = 2;
