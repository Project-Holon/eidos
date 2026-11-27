/*
 * sdk/mod_strings.c -- a mod's own text in the game's language: its strings
 * files (docs/mod-strings.md).
 *
 * The files
 * ---------
 *
 * `<mod>/strings.txt` holds the mod's words in the language it is written
 * in, and `<mod>/strings.<lang>.txt` (`en`, `fr`, `de`, `it`, `es`) holds
 * them for the game in that language.  One line is one text:
 *
 *     # a comment line
 *     nuz.lives       = Lives
 *     nuz.lives.help  = How many Pokemon may faint before the run ends.
 *     nuz.dead        = {1} fainted. {2} lives left.
 *
 * The key is the word before the first '='; the text is the rest of the
 * line, without the blanks around it.  `\n` is a line break and `\\` a
 * backslash.  `{1}` to `{9}` are the places a rule or a code mod puts its
 * values in, so a translation can put them in its own order.  A line with
 * nothing after the '=' is as no line: a translation can be filled in a
 * little at a time and the plain text shows meanwhile.
 *
 * The lookup
 * ----------
 *
 * The text of a key is the line of the game's language file, else the
 * plain file's line.  The files are read the first time a mod's key is
 * asked, by the mod's folder, so the settings of a mod that is switched off
 * are read the same way as a running one's.  A rule or a code mod names
 * only its namespace: the loader binds each mod's namespace to its folder
 * (bw_mod_strings_bind).
 *
 * The texts given out stay valid for the program's life: Reload mods reads
 * the files again into new tables and keeps the old ones, because a screen
 * may still hold a label it was given before.
 *
 * The settings screen asks from the interface's thread and a rule from the
 * game's, so the tables are read and searched under the shared lock
 * (sdk/plat_thread.h); a file is small and is read once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_strings.h"
#include "plat_thread.h"
#include "types.h"

extern int bw_lang_language(void) __attribute__((weak));   /* sdk/langmap.c */
extern const char *bw_mod_savedata_namespace(void);         /* sdk/mod_savedata.c */

/* The game's language ids (src/game/version.h). */
#define LANG_FRENCH  3
#define LANG_ITALIAN 4
#define LANG_GERMAN  5
#define LANG_SPANISH 7

/* The most mod folders and namespaces the port keeps: the mod menu's 32
 * mods, and as many again for the folders Reload mods reads anew. */
#define STRINGS_TABLES_MAX 64
#define STRINGS_ROOT_MAX   512
#define STRINGS_NS_MAX     96

/* A strings file larger than this is not read (a mistake, not a mod's
 * words). */
#define STRINGS_FILE_MAX   (1L << 22)

const char *bw_mod_lang_code(void)
{
    switch (bw_lang_language != NULL ? bw_lang_language() : 2) {
    case LANG_FRENCH:  return "fr";
    case LANG_ITALIAN: return "it";
    case LANG_GERMAN:  return "de";
    case LANG_SPANISH: return "es";
    }
    return "en";
}

/* ------------------------------------------------------------------ */
/* One file                                                            */
/* ------------------------------------------------------------------ */

typedef struct StrLine {
    const char *key;
    const char *text;
} StrLine;

typedef struct StrFile {
    StrLine *lines;            /* sorted by key */
    u32      count;
} StrFile;

static int key_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

int bw_mod_strings_key_valid(const char *key)
{
    size_t n = 0;

    if (key == NULL || key[0] == '\0') {
        return 0;
    }
    for (; key[n] != '\0'; n++) {
        if (!key_char(key[n]) || n + 1 >= BW_MOD_STRINGS_KEY_MAX) {
            return 0;
        }
    }
    return 1;
}

static int line_cmp(const void *a, const void *b)
{
    return strcmp(((const StrLine *)a)->key, ((const StrLine *)b)->key);
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Read `path` into `out`.  0, or -1 when there is no such file.  A line
 * the reader cannot use is reported with its number, and the rest stands. */
static int file_read(const char *path, StrFile *out)
{
    FILE *f = fopen(path, "rb");
    char *buf, *p;
    long len;
    u32 cap = 0, line_no = 0;

    out->lines = NULL;
    out->count = 0;
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || len > STRINGS_FILE_MAX ||
        fseek(f, 0, SEEK_SET) != 0 || (buf = (char *)malloc((size_t)len + 1u)) == NULL) {
        fclose(f);
        fprintf(stderr, "eidos: %s: could not be read\n", path);
        return -1;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        fprintf(stderr, "eidos: %s: could not be read\n", path);
        return -1;
    }
    fclose(f);
    buf[len] = '\0';
    p = buf;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) {
        p += 3;                     /* a UTF-8 byte order mark */
    }
    /* The buffer is the file's own and stays: the keys and the texts are
     * cut out of it in place. */
    while (*p != '\0') {
        char *end = strchr(p, '\n');
        char *next = end != NULL ? end + 1 : p + strlen(p);
        char *key, *key_end, *eq, *text, *r, *w;

        if (end != NULL) {
            *end = '\0';
        }
        line_no++;
        while (is_blank(*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            p = next;
            continue;
        }
        eq = strchr(p, '=');
        key = p;
        key_end = eq != NULL ? eq : p + strlen(p);
        while (key_end > key && is_blank(key_end[-1])) {
            key_end--;
        }
        if (eq == NULL) {
            fprintf(stderr, "eidos: %s:%u: expected `key = text`; left out\n", path, line_no);
            p = next;
            continue;
        }
        *key_end = '\0';
        if (!bw_mod_strings_key_valid(key)) {
            fprintf(stderr, "eidos: %s:%u: the key '%s' holds a character other than letters, "
                    "digits, '.', '_' and '-', or is longer than %d; left out\n",
                    path, line_no, key, BW_MOD_STRINGS_KEY_MAX - 1);
            p = next;
            continue;
        }
        text = eq + 1;
        while (is_blank(*text)) {
            text++;
        }
        /* the escapes, in place: the text only gets shorter */
        for (r = w = text; *r != '\0'; r++) {
            if (r[0] == '\\' && r[1] == 'n') {
                *w++ = '\n';
                r++;
            } else if (r[0] == '\\' && r[1] == '\\') {
                *w++ = '\\';
                r++;
            } else {
                *w++ = *r;
            }
        }
        while (w > text && is_blank(w[-1])) {
            w--;
        }
        *w = '\0';
        if (text[0] != '\0') {
            if (out->count == cap) {
                u32 c = cap != 0 ? cap * 2u : 32u;
                StrLine *g = (StrLine *)realloc(out->lines, c * sizeof *g);

                if (g == NULL) {
                    break;
                }
                out->lines = g;
                cap = c;
            }
            out->lines[out->count].key = key;
            out->lines[out->count].text = text;
            out->count++;
        }
        p = next;
    }
    if (out->count > 1) {
        u32 i, k = 0;

        qsort(out->lines, out->count, sizeof *out->lines, line_cmp);
        /* a key given twice: the first line stands (qsort keeps no order,
         * so the one nearer the start of the file is found by address) */
        for (i = 0; i < out->count; i++) {
            if (k > 0 && strcmp(out->lines[k - 1].key, out->lines[i].key) == 0) {
                fprintf(stderr, "eidos: %s: the key '%s' is given twice; the first line stands\n",
                        path, out->lines[i].key);
                if (out->lines[i].text < out->lines[k - 1].text) {
                    out->lines[k - 1] = out->lines[i];
                }
                continue;
            }
            out->lines[k++] = out->lines[i];
        }
        out->count = k;
    }
    if (out->count == 0) {
        free(out->lines);
        out->lines = NULL;
        free(buf);
    }
    return 0;
}

static const char *file_find(const StrFile *f, const char *key)
{
    u32 lo = 0, hi = f->count;

    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2u;
        int c = strcmp(key, f->lines[mid].key);

        if (c == 0) {
            return f->lines[mid].text;
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* One mod's two files                                                 */
/* ------------------------------------------------------------------ */

typedef struct StrTable {
    char    root[STRINGS_ROOT_MAX];
    char    lang[3];           /* the language the local file is in */
    u32     epoch;             /* the reload it was read in */
    StrFile local;             /* strings.<lang>.txt */
    StrFile plain;             /* strings.txt */
} StrTable;

/* On the heap, made at the first ask: the 32-bit program's image has
 * little room (sdk/mod_menu.c keeps its modes and tabs on the heap for the
 * same reason). */
static StrTable *g_tables;
static u32       g_table_count;
static u32       g_epoch;

/* The table of `root`, read now when it is not there yet or was read
 * before the last reload or in another language.  NULL when the tables are
 * full.  Under the shared lock. */
static StrTable *table_of(const char *root)
{
    const char *lang = bw_mod_lang_code();
    StrTable *t = NULL;
    char path[STRINGS_ROOT_MAX + 32];
    u32 i;

    if (g_tables == NULL &&
        (g_tables = (StrTable *)calloc(STRINGS_TABLES_MAX, sizeof *g_tables)) == NULL) {
        return NULL;
    }
    for (i = 0; i < g_table_count; i++) {
        if (strcmp(g_tables[i].root, root) == 0) {
            t = &g_tables[i];
            break;
        }
    }
    if (t != NULL && t->epoch == g_epoch && strcmp(t->lang, lang) == 0) {
        return t;
    }
    if (t == NULL) {
        if (g_table_count >= STRINGS_TABLES_MAX || strlen(root) >= sizeof t->root) {
            return NULL;
        }
        t = &g_tables[g_table_count++];
        snprintf(t->root, sizeof t->root, "%s", root);
    }
    /* The old files stay in memory: a text given out before may still be
     * on a screen. */
    snprintf(t->lang, sizeof t->lang, "%s", lang);
    t->epoch = g_epoch;
    snprintf(path, sizeof path, "%s/strings.%s.txt", root, lang);
    (void)file_read(path, &t->local);
    snprintf(path, sizeof path, "%s/strings.txt", root);
    (void)file_read(path, &t->plain);
    return t;
}

const char *bw_mod_strings_get(const char *root, const char *key)
{
    const char *text = NULL;
    StrTable *t;

    if (root == NULL || root[0] == '\0' || key == NULL) {
        return NULL;
    }
    bw_shared_lock();
    t = table_of(root);
    if (t != NULL) {
        text = file_find(&t->local, key);
        if (text == NULL) {
            text = file_find(&t->plain, key);
        }
    }
    bw_shared_unlock();
    return text;
}

const char *bw_mod_strings_text(const char *root, const char *text)
{
    const char *line;

    if (text == NULL || text[0] != '@' || !bw_mod_strings_key_valid(text + 1)) {
        return text;
    }
    line = bw_mod_strings_get(root, text + 1);
    return line != NULL ? line : text + 1;
}

void bw_mod_strings_reload(void)
{
    bw_shared_lock();
    g_epoch++;
    bw_shared_unlock();
}

/* ------------------------------------------------------------------ */
/* The values in a text                                                */
/* ------------------------------------------------------------------ */

/* Put [s, s + n) at out[*o], cut before a character that does not fit. */
static void put_bytes(char *out, size_t size, size_t *o, const char *s, size_t n)
{
    size_t room = size - 1u - *o;

    if (n > room) {
        n = room;
        /* not in the middle of a character: drop the bytes of a cut one */
        while (n > 0 && ((u8)s[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(out + *o, s, n);
    *o += n;
}

size_t bw_mod_strings_fill(const char *line, const char *const *values, u32 count,
                           char *out, size_t size)
{
    size_t o = 0;
    const char *p;

    if (out == NULL || size == 0) {
        return 0;
    }
    if (line == NULL) {
        out[0] = '\0';
        return 0;
    }
    for (p = line; *p != '\0' && o + 1u < size; ) {
        if (p[0] == '{' && p[1] == '{') {
            put_bytes(out, size, &o, "{", 1);
            p += 2;
        } else if (p[0] == '{' && p[1] >= '1' && p[1] <= '9' && p[2] == '}') {
            u32 k = (u32)(p[1] - '1');

            if (k < count && values != NULL && values[k] != NULL) {
                put_bytes(out, size, &o, values[k], strlen(values[k]));
            }
            p += 3;
        } else {
            const char *q = p + 1;

            while (*q != '\0' && *q != '{') {
                q++;
            }
            put_bytes(out, size, &o, p, (size_t)(q - p));
            if (o + 1u >= size) {
                break;
            }
            p = q;
        }
    }
    out[o] = '\0';
    return o;
}

/* ------------------------------------------------------------------ */
/* The namespaces                                                      */
/* ------------------------------------------------------------------ */

typedef struct StrBind {
    char ns[STRINGS_NS_MAX];
    char root[STRINGS_ROOT_MAX];
} StrBind;

static StrBind *g_binds;           /* on the heap, as the tables */
static u32      g_bind_count;

void bw_mod_strings_bind(const char *ns, const char *root)
{
    u32 i;

    if (ns == NULL || ns[0] == '\0' || root == NULL || strlen(ns) >= STRINGS_NS_MAX ||
        strlen(root) >= STRINGS_ROOT_MAX) {
        return;
    }
    bw_shared_lock();
    if (g_binds == NULL &&
        (g_binds = (StrBind *)calloc(STRINGS_TABLES_MAX, sizeof *g_binds)) == NULL) {
        bw_shared_unlock();
        return;
    }
    for (i = 0; i < g_bind_count; i++) {
        if (strcmp(g_binds[i].ns, ns) == 0) {
            break;
        }
    }
    if (i == g_bind_count && g_bind_count < STRINGS_TABLES_MAX) {
        g_bind_count++;
    }
    if (i < g_bind_count) {
        snprintf(g_binds[i].ns, sizeof g_binds[i].ns, "%s", ns);
        snprintf(g_binds[i].root, sizeof g_binds[i].root, "%s", root);
    }
    bw_shared_unlock();
}

const char *bw_mod_strings_root(const char *ns)
{
    const char *root = NULL;
    u32 i;

    if (ns == NULL || ns[0] == '\0') {
        return NULL;
    }
    bw_shared_lock();
    for (i = 0; i < g_bind_count; i++) {
        if (strcmp(g_binds[i].ns, ns) == 0) {
            root = g_binds[i].root;
            break;
        }
    }
    bw_shared_unlock();
    return root;
}

/* ------------------------------------------------------------------ */
/* The services table                                                  */
/* ------------------------------------------------------------------ */

int bw_mod_strings_service_get(const char *key, char *out, u32 size)
{
    const char *text = bw_mod_strings_get(bw_mod_strings_root(bw_mod_savedata_namespace()), key);
    size_t n;

    if (text == NULL) {
        if (out != NULL && size > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    n = strlen(text);
    if (out != NULL && size > 0) {
        size_t o = 0;

        put_bytes(out, size, &o, text, n);
        out[o] = '\0';
    }
    return (int)n;
}

int bw_mod_strings_service_language(char *out, u32 size)
{
    if (out == NULL || size < 3) {
        return -1;
    }
    snprintf(out, size, "%s", bw_mod_lang_code());
    return 2;
}
