/*
 * engine/ui/recipe_field.c -- the one-screen recipes for the field: the world, the C-Gear bar, the start menu
 * (engine/ui/ui.h, docs/unified-view-recipes.md).
 *
 * Two recipes, in this priority order:
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
 *                C-Gear, the two DS screens side by side, until a right
 *                click or B.
 *
 * Anything else on the field's lower screen (the save report, the Union
 * Room, the Entralink, dowsing) is not matched and falls back.
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

#define WEAK __attribute__((weak))

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
    int i, x = 6, clicked = -1;

    ui_fill(c, 0, y, w, 17, UI_BODY2, 235);
    ui_fill(c, 0, y, w, 1, UI_EDGE, 255);
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
        r.y = y;
        r.w = kw + 4 + aw + 4;
        r.h = 17;
        st = eidos_ui_hit(r);
        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, y + 1, r.w, 15, UI_EDGE, 255);
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
            UI_WHITE, state ? (s32)UI_CYAN_EDGE : (s32)UI_GREY, UI_FONT_LARGE, 1);
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
    avail = ch - 17 - 16;
    bh = n > 0 ? (avail - (n - 1) * gap) / n : 30;
    bh = clampi(bh, 24, 44);
    top = 8 + (avail - (n * bh + (n - 1) * gap)) / 2;
    if (top < 8) {
        top = 8;
    }
    col_rect.x = colx - 10;
    col_rect.y = 0;
    col_rect.w = colw + 18;
    col_rect.h = ch - 17;

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
        const UiImage *ic = l->egg ? NULL : ui_mon_icon((u16)l->species, (u8)l->form, 0);

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

    hint = hint_bar(c, ch - 17, cw, hints, 3);

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
            hb.y = ch - 17;
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
    if (f.mode == 0 || f.mode == 5 || f.mode == 8) {
        logf_scene("field");
        return 1;
    }
    return 0;
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

static int small_button(EidosUiCtx *c, int *x, int y, const char *label, int h,
                        const char *key)
{
    int kw = key != NULL ? ui_keycap_w(key) : 0;
    int tw = ui_text_w(label, UI_FONT_SMALL);
    int w = tw + 10 + (kw > 0 ? (kw < 13 ? 13 : kw) + 3 : 0);
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
static void field_stick(EidosUiCtx *c, const EidosUiFrame *in, int top, int blocked)
{
    int x, y, pressed, released, down, dx, dy, adx, ady, kx, ky;
    double dist;
    u32 key;

    down = eidos_ui_pointer(&x, &y, &pressed, &released);
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
    int st_bar, x, open_menu = 0, st;
    int wide = cw >= 400;
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
        return;
    }
    if (g_show_cgear && f.mode != 0 && f.mode != 8) {
        g_show_cgear = 0;                   /* no C-Gear panel here         */
    }

    if (g_show_cgear) {
        /* The game's own C-Gear: the two screens, the mouse as the stylus. */
        static const char *const hints[] = { "B", "Back" };
        static u32 prev_b;
        int b = (in->buttons & BW_KEY_B) != 0;

        view->mode = EIDOS_VIEW_FALLBACK;
        if (hint_bar(c, ch - 17, cw, hints, 1) == 0 || right_click(in) || (b && !prev_b)) {
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

    /* While the game's place-name banner shows (it names the same place)
     * or an event runs, the bar slides up out of the way and comes back
     * after. */
    if (GAMESYSTEM_IsEventActive != NULL && GAMESYSTEM_IsEventActive(f.gs) != 0) {
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
    if (in->frame - g_menu_last_frame <= 90u && !banner_shown(&f)) {
        g_bar_slide = 0;
    } else if (banner_shown(&f) || (GAMESYSTEM_IsEventActive != NULL
                                    && GAMESYSTEM_IsEventActive(f.gs) != 0)) {
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

    /* the right group, from the right edge; its buttons come first so a
     * click on one is not also a click on the bar */
    x = cw - 6;
    battery(c, x - 17, 3);
    x -= 24;
    {
        static const char *const names[3] = { "WIRELESS", "ONLINE", "IR" };
        int lit[3];
        int i;

        lit[0] = comm;
        lit[1] = comm && last_cmd == 5;
        lit[2] = comm && last_cmd == 1;
        for (i = 0; i < 3; i++) {
            int tw = wide ? ui_text_w(names[i], UI_FONT_SMALL) : 0;
            int ty = ui_text_y(bh, UI_FONT_SMALL);

            x -= tw;
            if (wide) {
                ui_text(c, x, ty, names[i], lit[i] ? UI_WHITE : UI_EDGE2, -1, UI_FONT_SMALL, 1);
            }
            x -= 7;
            ui_fill(c, x, bh / 2 - 1, 3, 3, lit[i] ? UI_CYAN : UI_EDGE2, 255);
            x -= 6;
        }
    }
    x -= 4;
    if (f.mode == 0 || f.mode == 8) {           /* this save has a C-Gear */
        st = small_button(c, &x, 0, "C-GEAR", bh, NULL);
        if (st & EIDOS_UI_CLICK) {
            g_show_cgear = 1;
            st_bar = 0;
        } else if (st & EIDOS_UI_HOVER) {
            st_bar = 0;
        }
    }
    st = small_button(c, &x, 0, "Menu", bh, "X");
    if (st & EIDOS_UI_CLICK) {
        open_menu = 1;
    }
    if (st & EIDOS_UI_HOVER) {
        st_bar = 0;
    }

    /* the left: the cursor mark and the place */
    {
        int i;

        for (i = 0; i < 4; i++) {
            ui_fill(c, 5 + i, 4 + i, 1, 9 - 2 * i, UI_CYAN, 255);
        }
        ui_text(c, 14, ui_text_y(bh, UI_FONT_LARGE), place, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    }
    /* the middle: the time on its slanted tab */
    {
        int tw = ui_text_w("00:00", UI_FONT_LARGE) + 24;

        bar_trapezoid(c, cw / 2, tw, bh - 1, st_bar & EIDOS_UI_HOVER);
        ui_text(c, cw / 2 - ui_text_w(clock, UI_FONT_LARGE) / 2,
                ui_text_y(bh - 1, UI_FONT_LARGE), clock, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    }
    ui_pop(c, 0, -g_bar_slide, &saved_clip);
    if (g_bar_slide > 0) {
        return;                             /* no clicks while it moves     */
    }
    if ((st_bar & EIDOS_UI_CLICK) || open_menu) {
        eidos_ui_press(BW_KEY_X, 10);
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

const EidosUiRecipe *const ui_recipes_field[] = { &recipe_menu, &recipe_field };
const u32 ui_recipes_field_count = 2;
