/*
 * engine/platform/port_keyboard.c -- the port's own keyboard's model
 * (port_keyboard.h).  engine/ui/recipe_keyboard.c draws it.
 *
 * The cursor stands on a key of the page or on a button of the bottom bar.
 * The arrows move it as the keys are drawn: along a row (wrapping), and up
 * or down to the key of the next row nearest across; down from the last
 * row (or up from the first) it goes to the bar, to the button under it,
 * and back the same way.  On the grid pages the drawing says how many
 * columns it laid the keys in (eidos_pkb_set_cols); the qwerty page has its
 * rows of its own, staggered as a keyboard's.
 */

#include <string.h>

#include "port_keyboard.h"

#define KEY_A      0x0001u
#define KEY_B      0x0002u
#define KEY_SELECT 0x0004u
#define KEY_START  0x0008u
#define KEY_RIGHT  0x0010u
#define KEY_LEFT   0x0020u
#define KEY_UP     0x0040u
#define KEY_DOWN   0x0080u
#define KEY_R      0x0100u
#define KEY_L      0x0200u
#define KEY_Y      0x0800u

static const char k_upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char k_lower[] = "abcdefghijklmnopqrstuvwxyz0123456789";
static const char k_symbols[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
/* the qwerty page: four rows, each from a left edge in half keys */
static const char *const k_qwerty[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const int k_qwerty_half[4] = { 0, 0, 1, 3 };

static EidosPkb g_kb;
static int g_up;
static int g_result;

/* ------------------------------------------------------------------ */
/* The keys                                                            */
/* ------------------------------------------------------------------ */

/* A character's label: itself, or the port's own mark where the game's font
 * draws something else (its ' and " are curly, its $ the Pokedollar sign;
 * engine/ui/assets.c, port_marks). */
static const char *label_of(char ch)
{
    static char one[128][2];

    switch (ch) {
    case '\'': return "\xEF\xBC\x87";       /* U+FF07 */
    case '"':  return "\xEF\xBC\x82";       /* U+FF02 */
    case '$':  return "\xEF\xBC\x84";       /* U+FF04 */
    default:
        if ((unsigned char)ch < 128) {
            one[(unsigned char)ch][0] = ch;
            one[(unsigned char)ch][1] = '\0';
            return one[(unsigned char)ch];
        }
        return "?";
    }
}

int eidos_pkb_keys(int page, int shift, char *codes, const char *labels[], int *row, int *half, int max)
{
    int n = 0, r, i;

    if (page == EIDOS_PKB_QWERTY) {
        for (r = 0; r < 4; r++) {
            for (i = 0; k_qwerty[r][i] != '\0' && n < max; i++) {
                char ch = k_qwerty[r][i];

                if (shift && ch >= 'a' && ch <= 'z') {
                    ch = (char)(ch - 'a' + 'A');
                }
                if (codes != NULL) codes[n] = ch;
                if (labels != NULL) labels[n] = label_of(ch);
                if (row != NULL) row[n] = r;
                if (half != NULL) half[n] = k_qwerty_half[r] + 2 * i;
                n++;
            }
        }
        return n;
    }
    {
        const char *s = page == EIDOS_PKB_UPPER ? k_upper : page == EIDOS_PKB_LOWER ? k_lower : k_symbols;

        for (i = 0; s[i] != '\0' && n < max; i++) {
            if (codes != NULL) codes[n] = s[i];
            if (labels != NULL) labels[n] = label_of(s[i]);
            n++;
        }
    }
    (void)row;
    (void)half;
    return n;
}

int eidos_pkb_bar_buttons(int *ids, int *parts, const char **labels, int max)
{
    int n = 0;

#define BTN(id_, parts_, label_) \
    do { if (n < max) { ids[n] = (id_); parts[n] = (parts_); labels[n] = (label_); n++; } } while (0)
    BTN(EIDOS_PKB_BACK, 2, "BACK");
    if (g_kb.secret) {
        BTN(EIDOS_PKB_SHOW, 2, g_kb.show ? "HIDE" : "SHOW");
    }
    if (g_kb.page == EIDOS_PKB_QWERTY) {
        BTN(EIDOS_PKB_SHIFT, 2, "SHIFT");
    }
    BTN(EIDOS_PKB_DELETE, 3, "DELETE");
    if (!g_kb.no_space) {
        BTN(EIDOS_PKB_SPACE, 3, "SPACE");
    }
    BTN(EIDOS_PKB_OK, 3, "OK");
#undef BTN
    return n;
}

/* ------------------------------------------------------------------ */
/* Opening and closing                                                 */
/* ------------------------------------------------------------------ */

static void put(char *out, size_t n, const char *s)
{
    size_t i;

    for (i = 0; s != NULL && s[i] != '\0' && i + 1 < n; i++) {
        out[i] = s[i];
    }
    out[i] = '\0';
}

void eidos_pkb_open(const char *title, const char *sub, const char *prompt, const char *start, int max,
                    int flags)
{
    int i;

    memset(&g_kb, 0, sizeof g_kb);
    put(g_kb.title, sizeof g_kb.title, title);
    put(g_kb.sub, sizeof g_kb.sub, sub);
    put(g_kb.prompt, sizeof g_kb.prompt, prompt);
    g_kb.max = max < 1 ? 1 : (max > EIDOS_PKB_MAX ? EIDOS_PKB_MAX : max);
    g_kb.secret = (flags & EIDOS_PKB_SECRET) != 0;
    g_kb.no_space = (flags & EIDOS_PKB_NO_SPACE) != 0;
    for (i = 0; start != NULL && start[i] != '\0' && g_kb.len < g_kb.max; i++) {
        unsigned char ch = (unsigned char)start[i];

        if (ch >= 0x20 && ch < 0x7F && !(ch == ' ' && g_kb.no_space)) {
            g_kb.text[g_kb.len++] = (char)ch;
        }
    }
    g_kb.text[g_kb.len] = '\0';
    g_kb.caret = g_kb.len;
    g_kb.page = g_kb.secret ? EIDOS_PKB_LOWER : EIDOS_PKB_UPPER;
    g_kb.key = 0;
    g_kb.bar = -1;
    g_kb.cols = 10;
    g_up = 1;
    g_result = 0;
}

const EidosPkb *eidos_pkb(void)
{
    return g_up ? &g_kb : NULL;
}

void eidos_pkb_close(void)
{
    /* the text goes with it: a password is never kept */
    memset(&g_kb, 0, sizeof g_kb);
    g_up = 0;
    g_result = 0;
}

int eidos_pkb_result(char *out, int n)
{
    int r = g_result;

    if (!g_up || r == 0) {
        return 0;
    }
    if (out != NULL && n > 0) {
        put(out, (size_t)n, r > 0 ? g_kb.text : "");
    }
    eidos_pkb_close();
    return r;
}

/* ------------------------------------------------------------------ */
/* Typing                                                              */
/* ------------------------------------------------------------------ */

static int insert(char ch)
{
    if (g_kb.len >= g_kb.max || (ch == ' ' && g_kb.no_space)) {
        return 0;
    }
    memmove(g_kb.text + g_kb.caret + 1, g_kb.text + g_kb.caret, (size_t)(g_kb.len - g_kb.caret + 1));
    g_kb.text[g_kb.caret] = ch;
    g_kb.len++;
    g_kb.caret++;
    return 1;
}

/* Delete the letter before the caret; on an empty text, leave (as the game's
 * keyboard of the port's own does: B on an empty field is Back). */
static int backspace(void)
{
    if (g_kb.len == 0) {
        g_result = -1;
        return 1;
    }
    if (g_kb.caret == 0) {
        return 0;
    }
    memmove(g_kb.text + g_kb.caret - 1, g_kb.text + g_kb.caret, (size_t)(g_kb.len - g_kb.caret + 1));
    g_kb.len--;
    g_kb.caret--;
    return 1;
}

static int set_page(int page)
{
    int n;

    if (page == g_kb.page) {
        return 0;
    }
    g_kb.page = page;
    n = eidos_pkb_keys(page, g_kb.shift, NULL, NULL, NULL, NULL, 128);
    if (g_kb.key >= n) {
        g_kb.key = n - 1;
    }
    if (g_kb.bar == EIDOS_PKB_SHIFT && page != EIDOS_PKB_QWERTY) {
        g_kb.bar = EIDOS_PKB_DELETE;
    }
    return 1;
}

static int press_bar(int button)
{
    switch (button) {
    case EIDOS_PKB_BACK:   g_result = -1; return 1;
    case EIDOS_PKB_OK:     g_result = 1; return 1;
    case EIDOS_PKB_DELETE: return backspace();
    case EIDOS_PKB_SPACE:  return insert(' ');
    case EIDOS_PKB_SHOW:   g_kb.show = !g_kb.show; return 1;
    case EIDOS_PKB_SHIFT:  g_kb.shift = !g_kb.shift; return 1;
    default:               return 0;
    }
}

void eidos_pkb_tap_key(int i)
{
    char codes[128];
    int n;

    if (!g_up) {
        return;
    }
    n = eidos_pkb_keys(g_kb.page, g_kb.shift, codes, NULL, NULL, NULL, 128);
    if (i >= 0 && i < n) {
        g_kb.key = i;
        g_kb.bar = -1;
        (void)insert(codes[i]);
    }
}

void eidos_pkb_tap_bar(int button)
{
    if (!g_up) {
        return;
    }
    g_kb.bar = button;
    (void)press_bar(button);
}

void eidos_pkb_tap_page(int page)
{
    if (g_up && page >= 0 && page < EIDOS_PKB_PAGES) {
        (void)set_page(page);
    }
}

void eidos_pkb_type(const char *utf8)
{
    int i;

    for (i = 0; g_up && utf8 != NULL && utf8[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)utf8[i];

        if (ch == 0x08) {
            (void)backspace();
        } else if (ch >= 0x20 && ch < 0x7F) {
            (void)insert((char)ch);
        }
    }
}

void eidos_pkb_set_cols(int cols)
{
    if (cols >= 1) {
        g_kb.cols = cols;
    }
}

/* ------------------------------------------------------------------ */
/* The arrows                                                          */
/* ------------------------------------------------------------------ */

/* Each key's row and its middle across, in half keys; the rows' count and
 * the whole width, also in half keys. */
static int layout(int *row, int *mid, int *rows, int *width)
{
    int half[128], n, i;

    if (g_kb.page == EIDOS_PKB_QWERTY) {
        n = eidos_pkb_keys(g_kb.page, g_kb.shift, NULL, NULL, row, half, 128);
        for (i = 0; i < n; i++) {
            mid[i] = half[i] + 1;
        }
        *rows = 4;
        *width = 20;
        return n;
    }
    n = eidos_pkb_keys(g_kb.page, g_kb.shift, NULL, NULL, NULL, NULL, 128);
    for (i = 0; i < n; i++) {
        row[i] = i / g_kb.cols;
        mid[i] = 2 * (i % g_kb.cols) + 1;
    }
    *rows = (n + g_kb.cols - 1) / g_kb.cols;
    *width = 2 * g_kb.cols;
    return n;
}

/* The key of row `r` under `x` (half keys): the nearest one whose middle is
 * at most a key away (the qwerty rows stand a half or a whole key apart),
 * or -1 where the row is short and nothing stands under x. */
static int nearest(const int *row, const int *mid, int n, int r, int x)
{
    int i, best = -1, bd = 3;

    for (i = 0; i < n; i++) {
        int d = mid[i] > x ? mid[i] - x : x - mid[i];

        if (row[i] == r && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* The bar's buttons: the one under the share `x / width` across, and a
 * button's middle as that share. */
static int bar_at(int x, int width)
{
    int ids[8], parts[8], total = 0, at = 0, b, n;
    const char *labels[8];

    n = eidos_pkb_bar_buttons(ids, parts, labels, 8);
    for (b = 0; b < n; b++) {
        total += parts[b];
    }
    for (b = 0; b < n; b++) {
        at += parts[b];
        if (x * total < at * width) {
            return ids[b];
        }
    }
    return ids[n - 1];
}

static int bar_mid(int button, int width)
{
    int ids[8], parts[8], total = 0, at = 0, b, n, mid = width / 2;
    const char *labels[8];

    n = eidos_pkb_bar_buttons(ids, parts, labels, 8);
    for (b = 0; b < n; b++) {
        total += parts[b];
    }
    for (b = 0; b < n; b++) {
        if (ids[b] == button) {
            mid = (2 * at + parts[b]) * width / (2 * total);
        }
        at += parts[b];
    }
    return mid;
}

static int move(unsigned k)
{
    int row[128], mid[128], rows, width, n, i, x, r;

    n = layout(row, mid, &rows, &width);
    if (n == 0) {
        return 0;
    }
    if (g_kb.bar >= 0) {
        int ids[8], parts[8], b, nb;
        const char *labels[8];

        nb = eidos_pkb_bar_buttons(ids, parts, labels, 8);
        for (b = 0; b < nb && ids[b] != g_kb.bar; b++) {
        }
        if (b == nb) {
            b = 0;
        }
        if (k & (KEY_LEFT | KEY_RIGHT)) {
            g_kb.bar = ids[(b + ((k & KEY_LEFT) ? nb - 1 : 1)) % nb];
            return 1;
        }
        x = bar_mid(g_kb.bar, width);
        if (k & KEY_UP) {
            /* the lowest key above the button (a short last row: the row
             * above it) */
            for (r = rows - 1, i = -1; r >= 0 && i < 0; r--) {
                i = nearest(row, mid, n, r, x);
            }
        } else {
            i = nearest(row, mid, n, 0, x);
        }
        g_kb.key = i < 0 ? 0 : i;
        g_kb.bar = -1;
        return 1;
    }
    i = g_kb.key < n ? g_kb.key : n - 1;
    if (k & (KEY_LEFT | KEY_RIGHT)) {
        int first = i, last = i;

        while (first > 0 && row[first - 1] == row[i]) first--;
        while (last < n - 1 && row[last + 1] == row[i]) last++;
        g_kb.key = (k & KEY_LEFT) ? (i == first ? last : i - 1) : (i == last ? first : i + 1);
        return 1;
    }
    r = row[i] + ((k & KEY_UP) ? -1 : 1);
    if (r >= 0 && r < rows) {
        int j = nearest(row, mid, n, r, mid[i]);

        if (j >= 0) {
            g_kb.key = j;
            return 1;
        }
    }
    /* off the keys: to the bar, the button under the key */
    g_kb.bar = bar_at(mid[i], width);
    return 1;
}

int eidos_pkb_input(unsigned pressed)
{
    int changed = 0;

    if (!g_up || g_result != 0) {
        return 0;
    }
    if (pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
        changed |= move(pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT));
    }
    if (pressed & KEY_A) {
        if (g_kb.bar >= 0) {
            changed |= press_bar(g_kb.bar);
        } else {
            char codes[128];
            int n = eidos_pkb_keys(g_kb.page, g_kb.shift, codes, NULL, NULL, NULL, 128);

            if (g_kb.key >= 0 && g_kb.key < n) {
                changed |= insert(codes[g_kb.key]);
            }
        }
    }
    if (pressed & KEY_B) {
        changed |= backspace();
    }
    if (pressed & KEY_START) {
        g_result = 1;
        changed = 1;
    }
    if (pressed & KEY_SELECT) {
        changed |= set_page((g_kb.page + 1) % EIDOS_PKB_PAGES);
    }
    if ((pressed & KEY_L) && g_kb.caret > 0) {
        g_kb.caret--;
        changed = 1;
    }
    if ((pressed & KEY_R) && g_kb.caret < g_kb.len) {
        g_kb.caret++;
        changed = 1;
    }
    if ((pressed & KEY_Y) && g_kb.secret) {
        g_kb.show = !g_kb.show;
        changed = 1;
    }
    return changed;
}

/* ------------------------------------------------------------------ */
/* The field                                                           */
/* ------------------------------------------------------------------ */

void eidos_pkb_display(char *out, int n, int *caret_at)
{
    int i, o = 0;

    if (n <= 0) {
        return;
    }
    if (caret_at != NULL) {
        *caret_at = 0;
    }
    for (i = 0; i < g_kb.len; i++) {
        const char *s = (g_kb.secret && !g_kb.show) ? "\xE2\x80\xA2" : label_of(g_kb.text[i]);   /* U+2022 */
        int l = (int)strlen(s);

        if (i == g_kb.caret && caret_at != NULL) {
            *caret_at = o;
        }
        if (o + l >= n) {
            break;
        }
        memcpy(out + o, s, (size_t)l);
        o += l;
    }
    if (g_kb.caret >= g_kb.len && caret_at != NULL) {
        *caret_at = o;
    }
    out[o] = '\0';
}
