/*
 * engine/ui/recipe_input.c -- the one-screen recipes for name entry and the Xtransceiver
 * (engine/ui/ui.h, docs/unified-view-recipes.md, sections 13 and 14).
 *
 * Name entry (ov194): every page the game has, drawn from the game's own
 * page tables (the grid members of archive 0x77 that the screen holds at
 * work+0x22C), with the keys laid out densely.  A key keeps the position
 * of its cell in the game's own picker: a click taps the centre of that
 * cell on the bottom screen, so the game does the typing.
 *
 * The game's structs are read by offset (32-bit layout; the build is
 * ILP32), the way sdk/mp.c reads the game system.
 */

#include "ui.h"
#include "kit.h"
#include "dsmem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define IN_WEAK __attribute__((weak))
#else
#define IN_WEAK
#endif

/* The game system's global (the pointer variable holds the object's host
 * address; its first word is the running GameSystem), and the global proc
 * stack (the new game's name entry runs on the intro's own stack). */
extern void *bw_dsref__GameSystemGlobal IN_WEAK;
extern void *bw_dsref_g_ProcSys IN_WEAK;

/* ------------------------------------------------------------------ */
/* Reading the game                                                    */
/* ------------------------------------------------------------------ */

typedef unsigned char Byte;

static u32 rd32(const void *p, u32 off)
{
    u32 v;

    memcpy(&v, (const Byte *)p + off, 4);
    return v;
}

static u16 rd16(const void *p, u32 off)
{
    u16 v;

    memcpy(&v, (const Byte *)p + off, 2);
    return v;
}

static void *rdp(const void *p, u32 off)
{
    /* These offsets describe DS records, including four-byte pointers. */
    u32 address = rd32(p, off);
    void *host = address != 0 ? bw_ds_try_ptr(address) : NULL;

    if (host != NULL || sizeof(void *) > 4) return host;
    /* On ILP32 a game heap pointer can also lie outside the DS windows. */
    return (void *)(uintptr_t)address;
}

/* Proc: data 0x00, overlayId 0x04, seq 0x08, work 0x14.  ProcSys: current 0x08. */
static void *procsys_top(void *ps)
{
    return ps != NULL ? rdp(ps, 0x08) : NULL;
}

static int proc_overlay(void *proc)
{
    return proc != NULL ? (int)rd32(proc, 0x04) : -1;
}

static void *proc_work(void *proc, int overlay)
{
    if (proc == NULL || proc_overlay(proc) != overlay || (int)rd32(proc, 0x08) < 2) {
        return NULL;
    }
    return rdp(proc, 0x14);
}

#define OV_NAMEIN 194
#define OV_CTVT   172
#define OV_START  88

static void *game_proc_top(void)
{
    void *gs;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    memcpy(&gs, bw_dsref__GameSystemGlobal, sizeof(gs));
    return gs != NULL ? procsys_top(rdp(gs, 0x08)) : NULL;
}

/* The new game's intro keeps its own proc stack in its work (+0x30). */
static void *intro_proc_top(void)
{
    void *root, *work, *data;

    if (&bw_dsref_g_ProcSys == NULL || bw_dsref_g_ProcSys == NULL) {
        return NULL;
    }
    memcpy(&root, bw_dsref_g_ProcSys, sizeof(root));
    root = procsys_top(root);
    work = proc_work(root, OV_START);
    if (work == NULL) {
        return NULL;
    }
    data = rdp(root, 0x00);
    /* the intro's ProcData (ov088 0x021BD818) */
    if (data == NULL || data != bw_ds_try_ptr(0x021BD818u)) {
        return NULL;
    }
    return procsys_top(rdp(work, 0x30));
}

static void *find_scene_work(int overlay)
{
    void *w = proc_work(game_proc_top(), overlay);

    if (w == NULL && overlay == OV_NAMEIN) {
        w = proc_work(intro_proc_top(), overlay);
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* Name entry: the state                                               */
/* ------------------------------------------------------------------ */

#define NW_FIELD      0x068   /* inputField: text u16[11] at +0, length at +0x1E, max at +0x22 */
#define NW_GRID_X     0x1D4
#define NW_GRID_Y     0x1D8
#define NW_GRID_STATE 0x1E0
#define NW_PAGE       0x1FC
#define NW_SHIFT      0x200
#define NW_ANIM       0x20C
#define NW_MEMBERS    0x22C
#define NW_ARGS       0x2C4

typedef struct NameState {
    void *w;
    int kind, species, maxlen, len;
    int page, shift, gx, gy, busy;
    u16 text[12];
    const Byte *grid;       /* the current page's member (width, height, cells) */
} NameState;

static int name_read(NameState *s)
{
    void *w = find_scene_work(OV_NAMEIN);
    void *args, *members;
    int i, idx;

    if (w == NULL) {
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->w = w;
    args = rdp(w, NW_ARGS);
    members = rdp(w, NW_MEMBERS);
    if (args == NULL || members == NULL) {
        return 0;
    }
    s->kind = (int)rd32(args, 0x00);
    s->species = (int)rd32(args, 0x04);
    s->maxlen = (int)rd32(args, 0x18);
    s->len = rd16(w, NW_FIELD + 0x1E);
    for (i = 0; i < 11; i++) {
        s->text[i] = rd16(w, NW_FIELD + (u32)i * 2);
    }
    s->page = (int)rd32(w, NW_PAGE);
    s->shift = (int)rd32(w, NW_SHIFT) & 1;
    s->gx = (int)rd32(w, NW_GRID_X);
    s->gy = (int)rd32(w, NW_GRID_Y);
    s->busy = rd32(w, NW_GRID_STATE) != 0 || rd32(w, NW_ANIM) != 0;
    if (s->page < 0 || s->page > 3) {
        s->page = 0;
    }
    idx = s->page < 3 ? s->page : 3 + s->shift;
    s->grid = (const Byte *)rdp(members, (u32)idx * 4);
    if (s->grid == NULL) {
        return 0;
    }
    return 1;
}

static int name_match(void)
{
    NameState s;

    return name_read(&s);
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */
/* ------------------------------------------------------------------ */

/* A DS character code as UTF-8 (the kit's font reads code points). */
static int put_utf8(char *out, u32 c)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

static void text_of(const u16 *t, int n, char *out, int cap)
{
    int i, o = 0;

    for (i = 0; i < n && t[i] != 0xFFFF && o + 4 < cap; i++) {
        o += put_utf8(out + o, t[i] == 0x3000 ? ' ' : t[i]);
    }
    out[o] = '\0';
}

static int cell_code(const Byte *grid, int x, int y)
{
    int gw = rd16(grid, 0);

    return rd16(grid, 4 + (u32)(y * gw + x) * 2);
}

/* ------------------------------------------------------------------ */
/* Name entry: drawing                                                 */
/* ------------------------------------------------------------------ */

typedef struct NKey {
    int code;
    int tx, ty;             /* the tap, bottom-screen pixels */
    int cx, cy;             /* the cell in the grid (pages 0..2) */
} NKey;

#define MAXKEYS 80

static const int qwerty_tx[3] = { 5, 6, 7 };
static const int qwerty_ty[3] = { 8, 12, 16 };

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

static int hit_box(EidosUiCtx *c, int x, int y, int w, int h, int *hover)
{
    EidosUiRect r;
    int st;

    (void)c;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    st = eidos_ui_hit(r);
    *hover = (st & EIDOS_UI_HOVER) != 0;
    return (st & EIDOS_UI_CLICK) != 0;
}

/* A key of any size: label centred both ways, one pixel-doubling step up
 * when the key is large. */
static void draw_key(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                     int cursor, int hover, int down, u32 accent)
{
    int font = UI_FONT_LARGE;
    int k = (h >= 30 && w >= 26) ? 2 : 1;
    int tw = ui_text_w(label, font) * k;
    int th = 15 * k;
    u32 fill = cursor ? UI_CYAN2 : (hover ? 0x242424u : UI_BODY2);
    u32 edge = cursor ? accent : (hover ? UI_EDGE2 : UI_EDGE);

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, fill, edge, 255);
    if (down) {
        ui_fill(c, x + 2, y + 2, w - 4, h - 4, 0xFFFFFF, 40);
    }
    ui_text(c, x + (w - tw) / 2, y + (h - th) / 2 + k - 1 + (k == 1 ? 0 : 0), label, UI_WHITE,
            cursor ? 0x003C50 : (s32)UI_GREY, font, k);
}

static int g_pressed_key = -1;

/* Pick the column count that makes the keys closest to the wanted shape. */
static int pick_cols(int n, int aw, int ah, int gap)
{
    int cols, best = 1;
    long best_score = -1;

    for (cols = 1; cols <= n; cols++) {
        int rows = (n + cols - 1) / cols;
        int kw = (aw - (cols - 1) * gap) / cols;
        int kh = (ah - (rows - 1) * gap) / rows;
        long s;

        if (kw < 12 || kh < 12) {
            continue;
        }
        /* the key size, penalising a shape far from 1.15 : 1 */
        s = (long)imin(kw, kh * 12 / 10) * 100;
        if (kw > kh * 15 / 10) {
            s -= (long)(kw - kh * 15 / 10) * 60;
        }
        if (kh > kw * 12 / 10) {
            s -= (long)(kh - kw * 12 / 10) * 60;
        }
        if (s > best_score) {
            best_score = s;
            best = cols;
        }
    }
    return best;
}

static const char *kind_sub(int kind)
{
    switch (kind) {
    case 0: return "PLAYER";
    case 1: return "POKEMON";
    case 2: return "BOX";
    case 3: return "RIVAL";
    case 4: return "FRIEND";
    case 5: return "GREETING";
    case 6: return "THANKS";
    default: return "MESSAGE";
    }
}

static const char *kind_prompt(int kind)
{
    switch (kind) {
    case 0: return "Your name?";
    case 1: return "Give it a nickname?";
    case 2: return "Box name?";
    case 3: return "Your rival's name?";
    case 4: return "Your friend's name?";
    case 5: return "How do you greet passersby?";
    case 6: return "How do you thank passersby?";
    default: return "Your message to passersby?";
    }
}

/* The game prints its prompt on the top screen in a white box ("Your name?",
 * "Tepig's nickname?", the validation notices).  Copy its letters, recoloured
 * for the dark interface, so the text is always the game's own words.  Returns
 * 1 when a prompt was found. */
static int draw_game_prompt(EidosUiCtx *c, const EidosUiFrame *in, int x, int y, int maxw)
{
    const u8 *scr = in->screen_rgba[0];
    int sx, sy, minx = 999, miny = 999, maxx = -1, maxy = -1;

    if (scr == NULL) {
        return 0;
    }
    for (sy = 151; sy < 186; sy++) {
        for (sx = 8; sx < 248; sx++) {
            const u8 *p = scr + ((size_t)sy * 256 + (size_t)sx) * 4;

            if ((int)p[0] + p[1] + p[2] < 330) {
                minx = imin(minx, sx);
                maxx = imax(maxx, sx);
                miny = imin(miny, sy);
                maxy = imax(maxy, sy);
            }
        }
    }
    if (maxx < 0 || maxx - minx + 1 > maxw + 40 || maxy - miny > 40) {
        return 0;
    }
    for (sy = miny; sy <= maxy; sy++) {
        for (sx = minx; sx <= maxx; sx++) {
            const u8 *p = scr + ((size_t)sy * 256 + (size_t)sx) * 4;
            int sum = (int)p[0] + p[1] + p[2];

            if (sum < 330) {
                ui_fill(c, x + sx - minx, y + (sy - miny) + (maxy - miny < 18 ? 0 : -2), 1, 1,
                        sum < 200 ? UI_LIGHT : UI_GREY, 255);
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Name entry: typing on the PC keyboard                               */
/* ------------------------------------------------------------------ */

/*
 * The player's typed letters, waiting for the game: one key a few frames
 * apart, each a tap on the cell that holds the character, switching to
 * the page that has it first when the current one does not.  Backspace is
 * DELETE, Return is OK.
 */
static u32 g_typed[64];
static int g_typed_n;
static u32 g_type_next;          /* the frame the next tap may go         */

static void typed_push(const char *s)
{
    while (s != NULL && *s != '\0' && g_typed_n < 64) {
        const unsigned char *u = (const unsigned char *)s;
        u32 cp;
        int len;

        if (u[0] < 0x80) {
            cp = u[0];
            len = 1;
        } else if ((u[0] & 0xE0) == 0xC0 && u[1] != 0) {
            cp = ((u32)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
            len = 2;
        } else if ((u[0] & 0xF0) == 0xE0 && u[1] != 0 && u[2] != 0) {
            cp = ((u32)(u[0] & 0x0F) << 12) | ((u32)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            len = 3;
        } else {
            s++;
            continue;
        }
        g_typed[g_typed_n++] = cp;
        s += len;
    }
}

/* The page (0..2) and the cell that type code cp; 0 when no page has it. */
static int typed_find(const NameState *s, u32 cp, int *page, int *tx, int *ty)
{
    void *members = rdp(s->w, NW_MEMBERS);
    int p;

    if (members == NULL) {
        return 0;
    }
    for (p = 0; p < 3; p++) {
        int order = (p + s->page) % 3;   /* the current page first */
        const Byte *g = (const Byte *)rdp(members, (u32)order * 4);
        int gw, gh, x, y;

        if (s->page == 3) {
            order = p;
            g = (const Byte *)rdp(members, (u32)order * 4);
        }
        if (g == NULL) {
            continue;
        }
        gw = rd16(g, 0);
        gh = rd16(g, 2);
        for (y = 0; y < gh; y++) {
            for (x = 0; x < gw; x++) {
                if ((u32)cell_code(g, x, y) == cp) {
                    *page = order;
                    *tx = 16 * x + 32;
                    *ty = 24 * y + 52;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void typed_step(const NameState *s, u32 frame, int space_tx, int space_ty)
{
    u32 cp;
    int page, tx, ty;

    if (g_typed_n == 0 || s->busy || frame < g_type_next) {
        return;
    }
    cp = g_typed[0];
    if (cp == '\b') {
        eidos_ui_tap(s->page == 3 ? 216 : 148, s->page == 3 ? 76 : 172, 3);
    } else if (cp == '\n') {
        eidos_ui_tap(s->page == 3 ? 212 : 204, s->page == 3 ? 108 : 172, 3);
    } else if (cp == ' ') {
        if (s->page == 3) {
            eidos_ui_tap(168, 172, 3);
        } else {
            eidos_ui_tap(space_tx, space_ty, 3);
        }
    } else if (!typed_find(s, cp, &page, &tx, &ty)) {
        /* nothing types it: drop it */
    } else if (page != s->page) {
        eidos_ui_tap(36 + 24 * page, 172, 3);
        g_type_next = frame + 12;      /* the page changes, then the key */
        return;
    } else {
        eidos_ui_tap(tx, ty, 3);
    }
    memmove(g_typed, g_typed + 1, (size_t)(g_typed_n - 1) * sizeof(g_typed[0]));
    g_typed_n--;
    g_type_next = frame + 6;
}

static void name_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    NameState s;
    int cw = c->canvas->w, ch = c->canvas->h;
    u32 acc = UI_CYAN;
    int pw, ph, px, py, pad = 8, x0, x1, y;
    int i, n = 0, space_tx = 192, space_ty = 52, space_found = 0;
    NKey keys[MAXKEYS];
    char buf[64], cnt[16];
    int allow;
    static const char *const hint_pairs[] = { "A", "Type", "B", "Delete", "START", "OK", "SELECT", "Page" };
    static const u32 hint_keys[] = { 0x1, 0x2, 0x8, 0x4 };
    static const char *const tabs[4] = { "UPPER", "lower", "Others", "qwerty" };
    int tabw[4], tabs_total = 0, tx, tabs_x, tabs_y, narrow;
    int row_a_h, icon, field_x, field_w;
    int bar_h, keys_y, keys_h, kx, kw, kh, cols, rows, gap = 3;
    const UiImage *img;

    if (!name_read(&s)) {
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 1;
    (void)in;

    allow = !s.busy;

    /* the keys of this page */
    if (s.page < 3) {
        int gw = rd16(s.grid, 0), gh = rd16(s.grid, 2), x2, y2;

        for (y2 = 0; y2 < gh; y2++) {
            for (x2 = 0; x2 < gw; x2++) {
                int code = cell_code(s.grid, x2, y2);
                if (code == 0x20 && !space_found) {
                    space_found = 1;
                    space_tx = 16 * x2 + 32;   /* a blank cell types a space */
                    space_ty = 24 * y2 + 52;
                }
                if (code != 0x3000 && code != 0x20 && code != 0 && n < MAXKEYS) {
                    keys[n].code = code;
                    keys[n].cx = x2;
                    keys[n].cy = y2;
                    keys[n].tx = 16 * x2 + 32;
                    keys[n].ty = 24 * y2 + 52;
                    n++;
                }
            }
        }
    }

    /* typing on the PC keyboard: the platform hands the letters over while
     * this screen asks for them */
    eidos_ui_want_text(1);
    typed_push(in->text);
    typed_step(&s, in->frame, space_tx, space_ty);

    /* the panel */
    pw = cw - 12;
    if (pw > 520) {
        pw = 520;
    }
    ph = ch - 17 - 10;
    if (ph > 262) {
        ph = 262;
    }
    px = (cw - pw) / 2;
    py = (ch - 17 - ph) / 2;
    ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE2, 244);
    snprintf(cnt, sizeof(cnt), "%d / %d", s.len, s.maxlen);
    ui_header(c, px + 1, py + 1, pw - 2, "NAME", kind_sub(s.kind), cnt, acc);

    x0 = px + pad;
    x1 = px + pw - pad;
    y = py + 1 + 22 + 6;

    /* row A: the subject, the prompt, the field, the page tabs */
    icon = 40;
    row_a_h = 40;
    img = NULL;
    if (s.kind == 1) {
        img = ui_mon_icon((u16)s.species, 0, (int)((in->frame / 24) & 1));
    } else if (s.kind == 0) {
        img = ui_overworld(s.species == 1 ? 33 : 0, 0, (int)((in->frame / 10) & 3));
    }
    if (img != NULL) {
        int ik = imin(3, imax(1, (icon - 4) / imax(imax(img->w, img->h), 1)));

        ui_panel(c, x0, y, icon, icon, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, UI_EDGE, 255);
        ui_image(c, x0 + (icon - img->w * ik) / 2, y + (icon - img->h * ik) / 2, img, ik);
        field_x = x0 + icon + 8;
    } else {
        field_x = x0;
    }
    for (i = 0; i < 4; i++) {
        tabw[i] = ui_text_w(tabs[i], UI_FONT_SMALL) + 18;
        tabs_total += tabw[i] + 3;
    }
    tabs_total -= 3;
    /* the tabs sit top right of the row; the prompt and the field take the rest */
    field_w = x1 - field_x;
    narrow = (x1 - tabs_total) - field_x < 140;
    tx = narrow ? x0 : x1 - tabs_total;
    tabs_x = tx;
    tabs_y = narrow ? y + row_a_h + 3 : y;
    for (i = 0; i < 4; i++) {
        if (narrow) {
            tabw[i] = (x1 - x0 - 9) / 4;
        }
        int sel = s.page == i;
        int h, clicked;

        clicked = hit_box(c, tx, tabs_y, tabw[i], 15, &h);
        ui_panel(c, tx, tabs_y, tabw[i], 15, UI_CUT_TL | UI_CUT_BR, 4,
                 sel ? UI_CYAN2 : (h ? 0x242424u : UI_BODY2), sel ? acc : (h ? UI_EDGE2 : UI_EDGE), 255);
        ui_text(c, tx + (tabw[i] - ui_text_w(tabs[i], UI_FONT_SMALL)) / 2,
                tabs_y + ui_text_y(15, UI_FONT_SMALL), tabs[i], sel ? UI_WHITE : UI_LIGHT,
                sel ? 0x003C50 : (s32)UI_EDGE, UI_FONT_SMALL, 1);
        if (clicked && allow && !sel) {
            eidos_ui_tap(36 + 24 * i, 172, 3);
        }
        tx += tabw[i] + 3;
    }
    if (!draw_game_prompt(c, in, field_x, y, narrow ? field_w : tabs_x - 6 - field_x)) {
        ui_text(c, field_x, y + ui_text_y(15, UI_FONT_LARGE), kind_prompt(s.kind), UI_LIGHT,
                UI_EDGE, UI_FONT_LARGE, 1);
    }
    text_of(s.text, s.len, buf, sizeof(buf));
    ui_field(c, field_x, y + row_a_h - 18, field_w, buf, imax(0, s.maxlen - s.len), acc);
    y += row_a_h + 6 + (narrow ? 18 : 0);

    /* the bottom bar: BACK, SPACE (if the page has one), SHIFT, OK */
    bar_h = 24;
    keys_y = y;
    keys_h = py + ph - pad - bar_h - 6 - keys_y;

    if (s.page < 3) {
        int aw = x1 - x0;

        cols = pick_cols(n, aw, keys_h, gap);
        rows = (n + cols - 1) / cols;
        kw = (aw - (cols - 1) * gap) / cols;
        kh = (keys_h - (rows - 1) * gap) / rows;
        kx = x0 + (aw - (kw * cols + gap * (cols - 1))) / 2;
        for (i = 0; i < n; i++) {
            int col = i % cols, row = i / cols;
            int x = kx + col * (kw + gap), yy = keys_y + row * (kh + gap);
            char lab[8];
            int hh, clicked, cur;

            lab[put_utf8(lab, (u32)keys[i].code)] = '\0';
            clicked = hit_box(c, x, yy, kw, kh, &hh);
            cur = keys[i].cx == s.gx && keys[i].cy == s.gy;
            draw_key(c, x, yy, kw, kh, lab, cur, hh, g_pressed_key == i && hh, acc);
            if (clicked && allow) {
                eidos_ui_tap(keys[i].tx, keys[i].ty, 3);
            }
        }
    } else {
        /* QWERTY: three staggered rows on a ten unit grid (the game's own order) */
        int aw = x1 - x0, unit = aw / 10, r, cidx;
        int rowh = (keys_h - 2 * gap) / 3;
        static const int rowlen[3] = { 10, 9, 8 };
        static const int rowoff2[3] = { 0, 1, 2 };   /* in half units */
        int ux = x0 + (aw - unit * 10) / 2;

        for (r = 0; r < 3; r++) {
            for (cidx = 0; cidx < rowlen[r]; cidx++) {
                int code = cell_code(s.grid, cidx, r);
                char lab[8];
                int x, yy, hh, clicked, tapx, tapy;

                if (code == 0x203B || code == 0x3000 || code == 0) {
                    continue;
                }
                x = ux + cidx * unit + rowoff2[r] * unit / 2;
                yy = keys_y + r * (rowh + gap);
                tapx = 8 * (qwerty_tx[r] + 2 * cidx) + 8;
                tapy = 8 * qwerty_ty[r] + 12;
                lab[put_utf8(lab, (u32)code)] = '\0';
                clicked = hit_box(c, x + 1, yy, unit - 2, rowh, &hh);
                draw_key(c, x + 1, yy, unit - 2, rowh, lab,
                         s.gy == r && s.gx == (r == 2 ? cidx + 1 : cidx), hh, 0, acc);
                if (clicked && allow) {
                    eidos_ui_tap(tapx, tapy, 3);
                }
            }
        }
    }

    /* BACK, SPACE, SHIFT, OK */
    {
        int by = py + ph - pad - bar_h;
        int aw = x1 - x0;
        int nb = s.page == 3 ? 4 : 3;
        int wsum = s.page == 3 ? 2 + 4 + 2 + 2 : 3 + 4 + 3, wgt;
        int bx = x0, b;
        struct { const char *lab; int w; int tx, ty; } bt[4];
        int bn = 0;

        bt[bn].lab = "DELETE";  bt[bn].w = s.page == 3 ? 2 : 3;
        bt[bn].tx = s.page == 3 ? 216 : 148; bt[bn].ty = s.page == 3 ? 76 : 172; bn++;
        if (s.page == 3) {
            bt[bn].lab = "Shift";  bt[bn].w = 2; bt[bn].tx = 40; bt[bn].ty = 140; bn++;
            bt[bn].lab = "SPACE"; bt[bn].w = 4; bt[bn].tx = 168; bt[bn].ty = 172; bn++;
        } else {
            bt[bn].lab = "SPACE"; bt[bn].w = 4; bt[bn].tx = space_tx; bt[bn].ty = space_ty; bn++;
        }
        bt[bn].lab = "OK"; bt[bn].w = s.page == 3 ? 2 : 3;
        bt[bn].tx = s.page == 3 ? 212 : 204; bt[bn].ty = s.page == 3 ? 108 : 172; bn++;
        (void)nb;
        for (b = 0; b < bn; b++) {
            int bw2 = (aw - (bn - 1) * gap) * bt[b].w / wsum, hh, clicked;

            wgt = b == bn - 1 ? x1 - bx : bw2;
            clicked = hit_box(c, bx, by, wgt, bar_h, &hh);
            draw_key(c, bx, by, wgt, bar_h, bt[b].lab, 0, hh, 0, acc);
            if (clicked && allow) {
                eidos_ui_tap(bt[b].tx, bt[b].ty, 3);
            }
            bx += wgt + gap;
        }
    }

    /* hints: each one presses its key */
    ui_hints(c, ch - 17, cw, hint_pairs, 4, hint_keys);

    /* a right click is B (delete); a click outside the panel does nothing here */
    if (eidos_ui_right_click() && allow) {
        eidos_ui_press(0x2, 2);
    }
}

static const EidosUiRecipe recipe_name = { "name entry", name_match, name_run, NULL };

/* ------------------------------------------------------------------ */
/* Xtransceiver: the call list (ov172, screen CTVT_SCREEN_CALL)        */
/* ------------------------------------------------------------------ */

/*
 * CtvtWork (0x1FC): screen +0x08 (2 = the call list), memberCount +0x18,
 * call +0x1F0.  CtvtCall (0x408): scroll +0x04 (u16), state +0x08, rows[15]
 * at +0x40 (stride 0x1C; the entry shown on row r is the byte at +0x18 of
 * the row, 0xFF none), entries[15] at +0x1E4 (stride 0x24: active +0x00,
 * selected +0x08, available +0x0C, peer record +0x14, kind +0x18, level
 * +0x1F).  The game tests a tap at x 8..223, row = (y + scroll) / 32, and
 * needs level < 4 (ctvt_call.c CTVT_CALL_HandleTouch).  The back button is
 * the rectangle y 168..191, x 224..247.
 */
extern const u16 *MYSTATUS_GetName(const void *status) IN_WEAK;

#define CW_SCREEN   0x08
#define CW_CALL     0x1F0

static int xt_call_work(void **cwork, void **call)
{
    void *w = find_scene_work(OV_CTVT);

    if (w == NULL || (int)rd32(w, CW_SCREEN) != 2) {
        return 0;
    }
    *cwork = w;
    *call = rdp(w, CW_CALL);
    return *call != NULL;
}

static int xt_match(void)
{
    void *w, *c;

    return xt_call_work(&w, &c);
}

static void xt_name(void *entry, char *out, int cap)
{
    const u16 *t = NULL;
    void *peer = rdp(entry, 0x14);
    int kind = ((const Byte *)entry)[0x18];
    int i, o = 0;

    if (peer != NULL) {
        if (kind == 0x20) {
            t = MYSTATUS_GetName != NULL ? MYSTATUS_GetName(peer) : NULL;
        } else {
            t = (const u16 *)((const Byte *)peer + 0x1A);
        }
    }
    if (t == NULL) {
        snprintf(out, (size_t)cap, "Trainer");
        return;
    }
    for (i = 0; i < 8 && t[i] != 0xFFFF && o + 4 < cap; i++) {
        o += put_utf8(out + o, t[i]);
    }
    out[o] = '\0';
}

static void xt_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    (void)in;
    void *cw, *call;
    int cwid = c->canvas->w, ch = c->canvas->h;
    int pw, ph, px, py, x0, x1, y, r, shown = 0, pad = 8;
    int scroll, rowh = 30;
    char cnt[24];
    static const char *const pairs[] = { "A", "Call", "B", "Back" };
    static const u32 keys[] = { 0x1, 0x2 };
    int rows_y, avail;

    if (!xt_call_work(&cw, &call)) {
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 1;

    scroll = rd16(call, 0x04);
    for (r = 0; r < 15; r++) {
        int slot = ((const Byte *)call)[0x40 + r * 0x1C + 0x18];

        if (slot != 0xFF && slot < 15 && rd32(call, 0x1E4 + (u32)slot * 0x24) == 1) {
            shown++;
        }
    }

    pw = imin(cwid - 12, 440);
    ph = imin(imin(ch - 17 - 12, 250), 22 + 6 + imax(shown, 3) * rowh + 2 * pad + 4);
    px = (cwid - pw) / 2;
    py = (ch - 17 - ph) / 2;
    ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE2, 244);
    snprintf(cnt, sizeof(cnt), "%d nearby", shown);
    ui_header(c, px + 1, py + 1, pw - 2, "XTRANSCEIVER", NULL, cnt, UI_CYAN);
    x0 = px + pad;
    x1 = px + pw - pad;
    rows_y = py + 1 + 22 + 6;
    avail = py + ph - pad - rows_y;

    if (shown == 0) {
        ui_text(c, x0 + 4, rows_y + 6, "Looking for someone to call.", UI_GREY, UI_EDGE,
                UI_FONT_LARGE, 1);
    }
    y = rows_y;
    for (r = 0; r < 15; r++) {
        int slot = ((const Byte *)call)[0x40 + r * 0x1C + 0x18];
        void *e;
        char name[40], letter[8];
        int level, avail_flag, sel, ok, hh, clicked, ty;
        const char *state;
        u32 dot;
        int cx;

        if (slot == 0xFF || slot >= 15) {
            continue;
        }
        e = (Byte *)call + 0x1E4 + slot * 0x24;
        if (rd32(e, 0) != 1) {
            continue;
        }
        if (y + rowh > rows_y + avail) {
            break;
        }
        sel = rd32(e, 0x08) == 1;
        avail_flag = rd32(e, 0x0C) == 1;
        level = ((const Byte *)e)[0x1F];
        ok = level < 4;
        xt_name(e, name, sizeof(name));
        state = !ok ? "Out of range" : sel ? "Calling" : avail_flag ? "Can call" : "Busy";
        dot = !ok ? UI_EDGE2 : sel ? UI_CYAN : avail_flag ? UI_HP_G : UI_HP_Y;

        clicked = hit_box(c, x0, y, x1 - x0, rowh, &hh);
        ui_row(c, x0, y, x1 - x0, rowh - 2, sel, hh && ok, &UI_THEME_MENU);
        /* the letter, centred in its circle */
        cx = x0 + 6;
        ui_ellipse(c, cx, y + (rowh - 2 - 20) / 2, 20, 20, sel ? UI_CYAN2 : UI_BODY2,
                   ok ? UI_LIGHT : UI_EDGE2, 255);
        letter[0] = '\0';
        if (name[0] != '\0') {
            int n = 1;

            if (((Byte)name[0] & 0xE0) == 0xC0) {
                n = 2;
            } else if (((Byte)name[0] & 0xF0) == 0xE0) {
                n = 3;
            }
            memcpy(letter, name, (size_t)n);
            letter[n] = '\0';
        }
        ui_text(c, cx + (20 - ui_text_w(letter, UI_FONT_LARGE)) / 2,
                y + (rowh - 2 - 20) / 2 + ui_text_y(20, UI_FONT_LARGE), letter,
                ok ? UI_WHITE : UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        ui_text(c, cx + 28, y + ui_text_y(rowh - 2, UI_FONT_LARGE), name,
                ok ? UI_WHITE : UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        ui_text(c, x1 - 6 - ui_text_w(state, UI_FONT_MEDIUM),
                y + ui_text_y(rowh - 2, UI_FONT_MEDIUM), state, ok ? UI_LIGHT : UI_GREY,
                UI_EDGE, UI_FONT_MEDIUM, 1);
        ui_fill(c, x1 - 6 - ui_text_w(state, UI_FONT_MEDIUM) - 10, y + (rowh - 2) / 2 - 2, 4, 4,
                dot, 255);
        ty = r * 32 - scroll + 16;
        if (clicked && ok && ty >= 4 && ty < 164) {
            eidos_ui_tap(128, ty, 3);
        }
        y += rowh;
    }

    ui_hints(c, ch - 17, cwid, pairs, 2, keys);
    if (eidos_ui_right_click()) {
        eidos_ui_tap(236, 180, 3);
    }
}

static const EidosUiRecipe recipe_xtrans = { "xtransceiver", xt_match, xt_run, NULL };

/* ------------------------------------------------------------------ */
/* The recipes                                                         */
/* ------------------------------------------------------------------ */

const EidosUiRecipe *const ui_recipes_input[] = { &recipe_name, &recipe_xtrans };
const u32 ui_recipes_input_count = 2;
