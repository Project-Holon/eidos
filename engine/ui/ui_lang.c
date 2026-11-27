/*
 * engine/ui/ui_lang.c -- the one-screen interface's own labels in the game's
 * language.
 *
 * The interface is written in English.  Each place that draws one of its
 * labels asks ui_tr for it, which gives the French, German, Italian or
 * Spanish words for a label the table knows (engine/ui/ui_lang.tsv,
 * generated into ui_lang_table.h by tools/ui_lang.py) and the string itself
 * otherwise.  The kit draws what it is handed: a name (the player's, a
 * Pokemon's, a mod's) or the game's own text is already in its language and
 * is never looked up, so a player named like a label keeps the name.
 *
 * A mod can bring the interface's words in its own language (a translation
 * of the game into one the cartridges do not have): its `ui_text` file has
 * one "English = translation" line a label (docs/mod-ui-text.md).  A mod's
 * words come before the table's, whatever the game's language, and a mod
 * loaded later wins over one loaded before it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_lang.h"

typedef struct UiLangRow {
    u32 hash;                   /* FNV-1a of the English                    */
    const char *en;
    const char *tr[4];          /* French, German, Italian, Spanish; NULL:
                                   the English                              */
} UiLangRow;

#include "ui_lang_table.h"

/* The game's language ids (src/game/version.h). */
#define LANG_FRENCH  3
#define LANG_ITALIAN 4
#define LANG_GERMAN  5
#define LANG_SPANISH 7

static int g_col = -1;          /* the table's column, -1 English */

void ui_lang_set(int game_language)
{
    switch (game_language) {
    case LANG_FRENCH:  g_col = 0; break;
    case LANG_GERMAN:  g_col = 1; break;
    case LANG_ITALIAN: g_col = 2; break;
    case LANG_SPANISH: g_col = 3; break;
    default:           g_col = -1; break;
    }
}

static u32 fnv1a(const char *s)
{
    u32 h = 2166136261u;
    const u8 *p;

    for (p = (const u8 *)s; *p != 0; p++) {
        h = (h ^ *p) * 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* The mods' words                                                     */
/* ------------------------------------------------------------------ */

typedef struct ModRow {
    u32 hash;
    u32 seq;                    /* the order it came in: a later one wins   */
    u32 owner;                  /* the mod it came from                     */
    char *en, *tr;
} ModRow;

static ModRow *g_mod;
static u32 g_mod_n, g_mod_cap, g_seq;

/* By hash, then the latest first, so the first match is the one in force. */
static int mod_cmp(const void *a, const void *b)
{
    const ModRow *x = (const ModRow *)a, *y = (const ModRow *)b;

    if (x->hash != y->hash) {
        return x->hash < y->hash ? -1 : 1;
    }
    return x->seq > y->seq ? -1 : x->seq < y->seq ? 1 : 0;
}

/* A side of a line, its spaces trimmed: "\n" is a line break, "\=" an
 * equals sign and "\\" a backslash. */
static char *cell_dup(const char *s, size_t n)
{
    char *out;
    size_t i, k = 0;

    while (n > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        n--;
    }
    while (n > 0 && (s[n - 1u] == ' ' || s[n - 1u] == '\t')) {
        n--;
    }
    out = (char *)malloc(n + 1u);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1u < n) {
            char c = s[i + 1u];

            if (c == 'n' || c == '=' || c == '\\') {
                out[k++] = c == 'n' ? '\n' : c;
                i++;
                continue;
            }
        }
        out[k++] = s[i];
    }
    out[k] = '\0';
    return out;
}

int ui_lang_mod_add(u32 owner, const char *text, size_t len)
{
    const char *p = text, *end = text + len;
    int added = 0;

    /* a UTF-8 byte order mark */
    if (len >= 3u && (u8)p[0] == 0xEFu && (u8)p[1] == 0xBBu && (u8)p[2] == 0xBFu) {
        p += 3;
    }
    while (p < end) {
        const char *ls = p, *le = p, *eq;
        ModRow r;

        while (le < end && *le != '\n') {
            le++;
        }
        p = le < end ? le + 1 : le;
        if (le > ls && le[-1] == '\r') {
            le--;
        }
        if (le == ls || *ls == '#') {
            continue;               /* a blank line or a comment */
        }
        /* "English = translation": the first '=' that is not "\=" */
        for (eq = ls; eq < le && !(*eq == '=' && (eq == ls || eq[-1] != '\\')); eq++) {
        }
        if (eq == le) {
            continue;               /* no '=' on the line */
        }
        if (g_mod_n == g_mod_cap) {
            u32 cap = g_mod_cap != 0u ? g_mod_cap * 2u : 256u;
            ModRow *grown = (ModRow *)realloc(g_mod, cap * sizeof(ModRow));

            if (grown == NULL) {
                break;
            }
            g_mod = grown;
            g_mod_cap = cap;
        }
        r.en = cell_dup(ls, (size_t)(eq - ls));
        r.tr = cell_dup(eq + 1, (size_t)(le - (eq + 1)));
        if (r.en == NULL || r.tr == NULL) {
            free(r.en);
            free(r.tr);
            break;
        }
        if (r.en[0] == '\0' || r.tr[0] == '\0') {
            free(r.en);             /* nothing to translate, or no
                                     * translation yet: the English stays */
            free(r.tr);
            continue;
        }
        r.hash = fnv1a(r.en);
        r.seq = ++g_seq;
        r.owner = owner;
        g_mod[g_mod_n++] = r;
        added++;
    }
    if (g_mod_n > 1u) {
        qsort(g_mod, g_mod_n, sizeof(ModRow), mod_cmp);
    }
    return added;
}

void ui_lang_mod_clear(u32 owner)
{
    u32 i, k = 0;

    for (i = 0; i < g_mod_n; i++) {
        if (owner == UI_LANG_ALL_MODS || g_mod[i].owner == owner) {
            free(g_mod[i].en);
            free(g_mod[i].tr);
        } else {
            g_mod[k++] = g_mod[i];
        }
    }
    g_mod_n = k;
}

static const char *mod_lookup(u32 h, const char *en)
{
    u32 lo = 0, hi = g_mod_n;

    while (lo < hi) {
        u32 mid = (lo + hi) / 2;

        if (g_mod[mid].hash < h) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < g_mod_n && g_mod[lo].hash == h; lo++) {
        if (strcmp(g_mod[lo].en, en) == 0) {
            return g_mod[lo].tr;
        }
    }
    return NULL;
}

/* A file a test switch names, written once a string: the interface draws a
 * string every frame, so the strings already written are remembered by
 * hash. */
typedef struct OnceLog {
    const char *env;
    int state;                      /* 0 not looked, 1 on, -1 off */
    FILE *f;
    u32 seen[8192];
} OnceLog;

static void once_log(OnceLog *lg, u32 h, const char *en, const char *tail)
{
    u32 i, k;
    const char *s;

    if (lg->state == 0) {
        const char *path = getenv(lg->env);

        lg->f = path != NULL && path[0] != '\0' ? fopen(path, "ab") : NULL;
        lg->state = lg->f != NULL ? 1 : -1;
    }
    if (lg->state < 0 || h == 0u) {
        return;
    }
    for (i = h % 8192u, k = 0; k < 8192u; i = (i + 1u) % 8192u, k++) {
        if (lg->seen[i] == h) {
            return;
        }
        if (lg->seen[i] == 0u) {
            lg->seen[i] = h;
            break;
        }
    }
    for (s = en; *s != '\0'; s++) {
        if (*s == '\n') {
            fputs("\\n", lg->f);
        } else if (*s == '=') {
            fputs("\\=", lg->f);
        } else if (*s == '\\') {
            fputs("\\\\", lg->f);
        } else {
            fputc(*s, lg->f);
        }
    }
    fputs(tail, lg->f);
    fflush(lg->f);
}

/*
 * For translators: EIDOS_UI_TEXT_DUMP=<file> writes every label the
 * interface asks for and no mod translates, once each, as the start of a
 * ui_text file.
 */
static OnceLog g_dump = { "EIDOS_UI_TEXT_DUMP", 0, NULL, { 0 } };

static void dump(u32 h, const char *en)
{
    once_log(&g_dump, h, en, " = \n");
}

/* The next conversion of a printf template from *p on: its kind (the
 * length modifier and the conversion letter, as "lu" or "s") into kind,
 * 0 at the end.  "%%" is not a conversion. */
static int next_conv(const char **p, char kind[4])
{
    const char *s = *p;

    for (;;) {
        int n = 0;

        while (*s != '\0' && *s != '%') {
            s++;
        }
        if (*s == '\0') {
            *p = s;
            return 0;
        }
        s++;
        if (*s == '%') {
            s++;
            continue;
        }
        while (*s != '\0' && strchr("-+ #0123456789.", *s) != NULL) {
            s++;                        /* flags, width, precision */
        }
        while (*s != '\0' && strchr("hlzjt", *s) != NULL && n < 2) {
            kind[n++] = *s++;
        }
        if (*s == '\0') {
            *p = s;
            return 0;
        }
        kind[n++] = *s++;
        kind[n] = '\0';
        *p = s;
        return 1;
    }
}

const char *ui_trf(const char *fmt)
{
    const char *tr = ui_tr(fmt), *a = fmt, *b = tr;
    char ka[4], kb[4];

    if (tr == fmt) {
        return fmt;
    }
    for (;;) {
        int ha = next_conv(&a, ka), hb = next_conv(&b, kb);

        if (ha != hb || (ha && strcmp(ka, kb) != 0)) {
            return fmt;                 /* not the same values: the English */
        }
        if (!ha) {
            return tr;
        }
    }
}

/* The table's words for en in the game's language, NULL when it has none. */
static const char *table_lookup(u32 h, const char *en)
{
    u32 lo = 0, hi = sizeof(k_ui_lang) / sizeof(k_ui_lang[0]);

    if (g_col < 0) {
        return NULL;
    }
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;

        if (k_ui_lang[mid].hash < h) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < sizeof(k_ui_lang) / sizeof(k_ui_lang[0]) && k_ui_lang[lo].hash == h; lo++) {
        if (strcmp(k_ui_lang[lo].en, en) == 0) {
            return k_ui_lang[lo].tr[g_col];
        }
    }
    return NULL;
}

const char *ui_tr(const char *en)
{
    u32 h;
    const char *word;

    if (en == NULL || en[0] == '\0') {
        return en;
    }
    h = fnv1a(en);
    if (g_mod_n != 0u && (word = mod_lookup(h, en)) != NULL) {
        return word;
    }
    dump(h, en);
    word = table_lookup(h, en);
    return word != NULL ? word : en;
}

const char *ui_tr_mod(const char *text)
{
    const char *word;

    if (text == NULL || text[0] == '\0' || g_mod_n == 0u) {
        return text;
    }
    word = mod_lookup(fnv1a(text), text);
    return word != NULL ? word : text;
}

/*
 * For whoever adds a label: EIDOS_UI_TEXT_UNTRANSLATED=<file> writes each
 * string the kit draws that is a label of the table with other words in
 * the game's language, once each: a label whose call site does not ask
 * ui_tr for it, or a name that happens to be a label (a player named
 * Hilbert in a French game: drawn as it is, as it should be).
 */
static OnceLog g_untranslated = { "EIDOS_UI_TEXT_UNTRANSLATED", 0, NULL, { 0 } };

void ui_lang_note_drawn(const char *text)
{
    u32 h;
    const char *word;

    if (g_untranslated.state < 0 || text == NULL || text[0] == '\0') {
        return;
    }
    h = fnv1a(text);
    word = table_lookup(h, text);
    if (word != NULL && strcmp(word, text) != 0) {
        once_log(&g_untranslated, h, text, "\n");
    } else if (g_untranslated.state == 0) {
        once_log(&g_untranslated, 0u, text, "");     /* opens it, writes nothing */
    }
}
