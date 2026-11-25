/*
 * engine/ui/recipe_keyboard.c -- the port's own keyboard in the one-screen
 * interface (engine/platform/port_keyboard.c is its model).
 *
 * It has the game keyboard's look (recipe_input.c): the panel, the header
 * with the count, the question and the page tabs over the field, the keys
 * and the bottom bar.  It has more: up to 64 letters, every printable
 * ASCII character (UPPER, lower, Symbols, qwerty), a caret L and R move, a
 * password shown as dots until SHOW (or Y), and BACK.  The field scrolls
 * so the caret stays in it.
 *
 * It stands over any scene while it is open (the settings, the Multiplayer
 * screens): its recipe group is the first (recipes.c).
 */

#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "input.h"
#include "keyboard_parts.h"
#include "port_keyboard.h"

#define HEAD_IN (ui_theme_active_has_frame() ? 3 : 1)

static int imax(int a, int b) { return a > b ? a : b; }

static int hit(int x, int y, int w, int h, int *hover)
{
    EidosUiRect r;
    int st;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    *hover = (st & EIDOS_UI_HOVER) != 0;
    return (st & EIDOS_UI_CLICK) != 0;
}

/* The length in bytes of the UTF-8 character at s. */
static int ch_len(const char *s)
{
    unsigned char b = (unsigned char)*s;

    return b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
}

static int width_of(const char *s, int n)
{
    char buf[512];

    if (n >= (int)sizeof buf) {
        n = (int)sizeof buf - 1;
    }
    memcpy(buf, s, (size_t)n);
    buf[n] = '\0';
    return ui_text_w(buf, UI_FONT_LARGE);
}

/* `text` cut to `maxw` with "..." at its end. */
static const char *fit(char *out, size_t n, const char *text, int maxw)
{
    size_t len;

    snprintf(out, n, "%s", text);
    if (ui_text_w(out, UI_FONT_LARGE) <= maxw) {
        return out;
    }
    for (len = strlen(out); len > 0; len--) {
        if (((unsigned char)out[len] & 0xC0) == 0x80) {
            continue;
        }
        snprintf(out + len, n - len, "...");
        if (ui_text_w(out, UI_FONT_LARGE) <= maxw) {
            return out;
        }
    }
    return out;
}

/*
 * The field: the text before the caret goes through the style's field (it
 * draws the caret after it), the rest after the caret in the same letters.
 * When the text is wider than the field, it is shown from the place that
 * keeps the caret in view.
 */
static void field(EidosUiCtx *c, int x, int y, int w, u32 acc)
{
    char shown[EIDOS_PKB_MAX * 3 + 1], before[EIDOS_PKB_MAX * 3 + 1];
    int at, s = 0, e, room = w - 14, len;

    eidos_pkb_display(shown, (int)sizeof shown, &at);
    len = (int)strlen(shown);
    /* from the start, or the first letter that lets the caret show */
    while (s < at && width_of(shown + s, at - s) > room - 6) {
        s += ch_len(shown + s);
    }
    e = at;
    while (e < len && width_of(shown + s, e + ch_len(shown + e) - s) <= room) {
        e += ch_len(shown + e);
    }
    memcpy(before, shown + s, (size_t)(at - s));
    before[at - s] = '\0';
    ui_field(c, x, y, w, before, 0, acc);
    if (e > at) {
        char after[EIDOS_PKB_MAX * 3 + 1];

        memcpy(after, shown + at, (size_t)(e - at));
        after[e - at] = '\0';
        ui_text(c, x + 6 + ui_text_w(before, UI_FONT_LARGE), y + ui_text_y(18, UI_FONT_LARGE), after, UI_WHITE,
                UI_EDGE, UI_FONT_LARGE, 1);
    }
}

static int kb_match(void)
{
    return eidos_pkb() != NULL;
}

static void kb_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const tabs[EIDOS_PKB_PAGES] = { "UPPER", "lower", "Symbols", "qwerty" };
    const EidosPkb *k = eidos_pkb();
    int cw = c->canvas->w, ch = c->canvas->h, pw, ph, px, py, pad = 8, gap = 3, bar_h = 24;
    int x0, x1, y, i, tx, tabw[EIDOS_PKB_PAGES], total = 0, narrow, keys_y, keys_h, hv;
    char cnt[16], line[160];
    const char *labels[128];
    int row[128], half[128], n;
    u32 acc = UI_CYAN;

    if (k == NULL) {
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hide_2d = 1;

    /* typing on the PC's keyboard */
    eidos_ui_want_text(1);
    if (in->text != NULL && in->text[0] != '\0') {
        eidos_pkb_type(in->text);
    }

    pw = cw - 12;
    if (pw > 520) pw = 520;
    ph = ch - ui_bar_h(c) - 10;
    if (ph > 262) ph = 262;
    px = (cw - pw) / 2;
    py = (ch - ui_bar_h(c) - ph) / 2;
    if (!ui_app_window(c, "namein", &px, &py, &pw, &ph)) {
        ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE2, 244);
    }
    snprintf(cnt, sizeof cnt, "%d / %d", k->len, k->max);
    ui_header(c, px + HEAD_IN, py + HEAD_IN, pw - 2 * HEAD_IN, k->title, k->sub[0] != '\0' ? k->sub : NULL, cnt,
              acc);
    x0 = px + pad;
    x1 = px + pw - pad;
    y = py + HEAD_IN + 22 + 6;

    /* the question and the page tabs (top right; under the question when
     * the panel is narrow), then the field */
    for (i = 0; i < EIDOS_PKB_PAGES; i++) {
        tabw[i] = ui_text_w(tabs[i], UI_FONT_SMALL) + 18;
        total += tabw[i] + 3;
    }
    total -= 3;
    narrow = (x1 - total) - x0 < 140;
    tx = narrow ? x0 : x1 - total;
    for (i = 0; i < EIDOS_PKB_PAGES; i++) {
        int ty = narrow ? y + 18 : y, sel = k->page == i;

        if (narrow) {
            tabw[i] = (x1 - x0 - 9) / 4;
        }
        if (hit(tx, ty, tabw[i], 15, &hv) && !sel) {
            eidos_pkb_tap_page(i);
        }
        ui_panel(c, tx, ty, tabw[i], 15, UI_CUT_TL | UI_CUT_BR, 4, sel ? UI_CYAN2 : (hv ? 0x242424u : UI_BODY2),
                 sel ? acc : (hv ? UI_EDGE2 : UI_EDGE), 255);
        ui_text(c, tx + (tabw[i] - ui_text_w(tabs[i], UI_FONT_SMALL)) / 2, ty + ui_text_y(15, UI_FONT_SMALL),
                tabs[i], sel ? UI_WHITE : UI_LIGHT, sel ? 0x003C50 : (s32)UI_EDGE, UI_FONT_SMALL, 1);
        tx += tabw[i] + 3;
    }
    ui_text(c, x0, y + ui_text_y(15, UI_FONT_LARGE),
            fit(line, sizeof line, k->prompt, (narrow ? x1 : x1 - total - 6) - x0), UI_LIGHT, UI_EDGE,
            UI_FONT_LARGE, 1);
    if (narrow) {
        y += 18;
    }
    field(c, x0, y + 40 - 18, x1 - x0, acc);
    y += 40 + 6;
    keys_y = y;
    keys_h = py + ph - pad - bar_h - 6 - keys_y;

    /* the keys */
    n = eidos_pkb_keys(k->page, k->shift, NULL, labels, row, half, 128);
    if (k->page == EIDOS_PKB_QWERTY) {
        /* four staggered rows on a ten key grid */
        int unit = (x1 - x0) / 10, ux = x0 + (x1 - x0 - unit * 10) / 2;
        int rowh = (keys_h - 3 * gap) / 4;

        for (i = 0; i < n; i++) {
            int x = ux + half[i] * unit / 2, yy = keys_y + row[i] * (rowh + gap);

            if (hit(x + 1, yy, unit - 2, rowh, &hv)) {
                eidos_pkb_tap_key(i);
            }
            ui_kb_key(c, x + 1, yy, unit - 2, rowh, labels[i], k->bar < 0 && k->key == i, hv, 0, acc);
        }
    } else {
        int aw = x1 - x0, cols = ui_kb_cols(n, aw, keys_h, gap), rows = (n + cols - 1) / cols;
        int kw = (aw - (cols - 1) * gap) / cols, kh = (keys_h - (rows - 1) * gap) / rows;
        int kx = x0 + (aw - (kw * cols + gap * (cols - 1))) / 2;

        eidos_pkb_set_cols(cols);
        for (i = 0; i < n; i++) {
            int x = kx + (i % cols) * (kw + gap), yy = keys_y + (i / cols) * (kh + gap);

            if (hit(x, yy, kw, kh, &hv)) {
                eidos_pkb_tap_key(i);
            }
            ui_kb_key(c, x, yy, kw, kh, labels[i], k->bar < 0 && k->key == i, hv, 0, acc);
        }
    }

    /* the bottom bar: BACK, (SHOW,) (SHIFT,) DELETE, (SPACE,) OK */
    {
        int ids[8], parts[8], nb = eidos_pkb_bar_buttons(ids, parts, labels, 8), b, wsum = 0;
        int bx = x0, by = py + ph - pad - bar_h;

        for (b = 0; b < nb; b++) {
            wsum += parts[b];
        }
        for (b = 0; b < nb; b++) {
            int bw = b == nb - 1 ? x1 - bx : (x1 - x0 - (nb - 1) * gap) * parts[b] / wsum;

            if (hit(bx, by, bw, bar_h, &hv)) {
                eidos_pkb_tap_bar(ids[b]);
            }
            ui_kb_key(c, bx, by, bw, bar_h, labels[b], k->bar == ids[b], hv, 0, acc);
            bx += bw + gap;
        }
    }

    /* the hints: each presses its key */
    {
        static const char *const with_show[] = { "A", "Type", "B", "Delete", "L R", "Move", "Y", "Show",
                                                  "START", "OK", "SELECT", "Page" };
        static const u32 with_show_keys[] = { BW_KEY_A, BW_KEY_B, BW_KEY_R, BW_KEY_Y, BW_KEY_START, BW_KEY_SELECT };
        static const char *const plain[] = { "A", "Type", "B", "Delete", "L R", "Move", "START", "OK",
                                             "SELECT", "Page" };
        static const u32 plain_keys[] = { BW_KEY_A, BW_KEY_B, BW_KEY_R, BW_KEY_START, BW_KEY_SELECT };
        const char *pairs[12];

        memcpy(pairs, k->secret ? with_show : plain, sizeof(pairs[0]) * (k->secret ? 12 : 10));
        if (k->len == 0) {
            pairs[3] = "Back";          /* B on an empty field leaves */
        }
        if (k->secret && k->show) {
            pairs[7] = "Hide";
        }
        ui_hints(c, ch - ui_bar_h(c), cw, pairs, k->secret ? 6 : 5, k->secret ? with_show_keys : plain_keys);
    }
    (void)imax;
}

static const EidosUiRecipe recipe_keyboard = { "port keyboard", kb_match, kb_run, NULL, 0 };

const EidosUiRecipe *const ui_recipes_keyboard[] = { &recipe_keyboard };
const u32 ui_recipes_keyboard_count = 1;
