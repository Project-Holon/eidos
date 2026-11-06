/*
 * sdk/mod_manifest.c -- the run-time manifest: the record, the parse and the
 * apply (docs/mod-state.md, section 3; docs/mod-interface.md, section 3).
 *
 * A mod ships a manifest, `<mod>/eidos_mod.txt`, and the port reads it twice
 * over: sdk/vfs.c reads the `name` and `order` keys to build the mod list and
 * the load order, and this file reads the whole run-time schema into one
 * record per mod, BwModManifest.  The parse fills the record; the apply turns
 * it into registrations: it checks the version the mod's code was built
 * against, derives the state namespace, checks every override against the
 * declared table and leaves the rule list and the settings path for the
 * interpreter and the settings surface.
 *
 * A manifest problem is one report and the default stands, the way sdk/vfs.c
 * reports a bad order:
 *
 *     eidos: mod <name>: <what is wrong>
 *
 * and the reports the port fixes are the declared check and the prototype
 * check:
 *
 *     eidos: mod <name>: override <region>:<addr> is not a declared address
 *     eidos: mod <name>: override <region>:<addr> states the prototype
 *         '<stated>', but the function's is '<expected>'
 *
 * A check is a diagnostic, not a sandbox: the port validates nothing about
 * a mod's code (docs/mod-interface.md, section 5), so a mod with an
 * undeclared override still loads and its other keys still apply.  An
 * override the prototype check refuses is skipped, and the mod keeps the
 * rest of its manifest.
 *
 * The apply hands the switch each override named to the registry
 * (bw_mod_slot_set_switch): the name is a run-time thing (it comes from the
 * mod's manifest) and the registry asks it, per hook, when the chain is
 * walked (docs/mod-overrides.md, "The switch").
 *
 * The loader (sdk/mod_loader.c) calls, in this order:
 *
 *     void       *bw_mod_manifest_new(const char *dir_name);
 *     int         bw_mod_manifest_parse_dir(void *manifest, const char *root,
 *                                           const char *dir_name);
 *     int         bw_mod_manifest_apply(void *manifest);
 *     u32         bw_mod_manifest_override_count(const void *manifest);
 *     const void *bw_mod_manifest_override_at(const void *manifest, u32 i);
 *
 * `bw_mod_manifest_parse` takes the manifest's text instead of a root, for a
 * caller that read the file itself.  The apply returns 0, or -1 when the mod
 * is refused (a version mismatch), and the loader then skips the mod.  The
 * apply leaves in the record the overrides whose pair is declared; the loader
 * resolves each symbol in the loaded library and calls bw_mod_slot_install
 * (sdk/mod_slot.h) for it, in load order, so a later mod wins over an
 * earlier one for one pair.
 *
 * The apply runs after the declared table is registered and after the event
 * layer is reset (docs/mod-state.md, section 3), so a later mod wins over an
 * earlier one for one pair.
 *
 * The record and its two rows live in sdk/mod_manifest.h: the loader reads
 * an override's region, address and symbol out of the record to install it,
 * so the two files have to agree on the layout.  The calls that take a
 * record take it as a void *, the way sdk/mod_loader.c declares them, so the
 * record's shape stays this file's own.
 *
 * The calls, which a header may carry too:
 *
 *     void bw_mod_manifest_init(void *manifest, const char *dir_name);
 *     void bw_mod_manifest_free(void *manifest);
 *     void *bw_mod_manifest_new(const char *dir_name);
 *     void bw_mod_manifest_delete(void *manifest);
 *     int  bw_mod_manifest_parse(void *manifest, const char *text,
 *                                const char *dir_name);
 *     int  bw_mod_manifest_parse_dir(void *manifest, const char *root,
 *                                    const char *dir_name);
 *     int  bw_mod_manifest_apply(void *manifest);
 *     u32  bw_mod_manifest_override_count(const void *manifest);
 *     const void *bw_mod_manifest_override_at(const void *manifest, u32 i);
 *     u32  bw_mod_manifest_rule_count(const void *manifest);
 *     const void *bw_mod_manifest_rule_at(const void *manifest, u32 i);
 *     u32  bw_mod_manifest_table_count(const void *manifest);
 *     const void *bw_mod_manifest_table_at(const void *manifest, u32 i);
 *     const char *bw_mod_manifest_name(const void *manifest);
 *     const char *bw_mod_manifest_code(const void *manifest);
 *     const char *bw_mod_manifest_state(const void *manifest);
 *     const char *bw_mod_manifest_settings(const void *manifest);
 *     const char *bw_mod_manifest_ui_text(const void *manifest);
 *     int  bw_mod_manifest_has_api(const void *manifest);
 *     int  bw_mod_manifest_namespace_valid(const char *ns);
 *     char *bw_mod_manifest_default_namespace(const char *name);
 *     void bw_mod_manifest_set_port_version(const char *version);
 *     const char *bw_mod_manifest_port_version(void);
 *     void bw_mod_manifest_publish_types(const BwModType *rows,
 *                                         unsigned int count);
 *     u32  bw_mod_manifest_applied_count(void);
 *     const void *bw_mod_manifest_applied_at(u32 i);
 *     void bw_mod_manifest_applied_clear(void);
 *
 * The row the loader reads is sdk/mod_manifest.h's BwModOverride, and the
 * layout is the one the loader repeats:
 *
 *     typedef struct BwModOverride {
 *         unsigned int region;       the region, BW_MOD_REGION_ARM9 or _OVL(id)
 *         unsigned int addr;         the even function start
 *         unsigned int addr_tagged;  the Thumb-tagged form, addr | 1
 *         int          mode;         0 replaces, 1 wraps
 *         char        *symbol;       the symbol the library exports
 *         char        *sig;          the stated prototype, or NULL
 *         char        *switch_name;  the switch name, or NULL
 *     } BwModOverride;
 *
 * The record is the caller's: the parse fills it, the apply reads it, and
 * bw_mod_manifest_free releases what it holds.  The applied list holds the
 * records themselves, so the caller keeps them alive until it clears the
 * list.
 */

#include "types.h"
#include "mod_api.h"
#include "mod_manifest.h"
#include "mod_slot.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The declared set at run time (docs/mod-state.md, section 4).  The build
 * emits the table beside the slot publications and sdk/mod_slot.c keeps it.
 * sdk/mod_slot.h declares the call; the declaration is repeated here, with
 * the signature the document fixes, so this file compiles whether or not the
 * header carries it.
 */
extern int bw_mod_slot_declared(unsigned int region, unsigned int addr);

/* The manifest file inside a mod's tree, packed or not. */
#define BW_MOD_MANIFEST_NAME "eidos_mod.txt"

/* The longest a state namespace may be: the same cap sdk/mod_savedata.c
 * keeps, so a namespace the manifest layer accepts is one the state layer
 * accepts too. */
#define BW_MOD_NAMESPACE_MAX 64

/* How an override takes the game's place: the modes are in sdk/mod_manifest.h
 * (BW_MOD_OVERRIDE_REPLACE and _WRAP), which the loader reads too. */

/*
 * The mark a record carries when it holds something.  The record is the
 * caller's, and the caller may hand over one it never initialised, so the
 * release path checks the mark before it frees anything: a record that does
 * not carry it is empty as far as the port is concerned.
 */
#define BW_MOD_MANIFEST_MAGIC 0x4D414E46u   /* "MANF" */

/* ------------------------------------------------------------------ */
/* The record                                                         */
/* ------------------------------------------------------------------ */

/*
 * The record and its two rows live in sdk/mod_manifest.h: the loader reads
 * an override's region, address and symbol out of the record to install it,
 * so the two files have to agree on the layout.
 */

/* ------------------------------------------------------------------ */
/* The small helpers                                                  */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s)
{
    size_t n;
    char *d;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    d = (char *)malloc(n + 1);
    if (d != NULL) {
        memcpy(d, s, n + 1);
    }
    return d;
}

/* A copy of [s, s + n) as a string. */
static char *dup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);

    if (d == NULL) {
        return NULL;
    }
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/* Trim [*s, *e) of surrounding blanks. */
static void trim(const char **s, const char **e)
{
    while (*s < *e && is_space(**s)) {
        (*s)++;
    }
    while (*e > *s && is_space((*e)[-1])) {
        (*e)--;
    }
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int to_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* A C identifier: the symbol a mod's library exports. */
static int is_ident(const char *s, size_t n)
{
    size_t i;

    if (n == 0) {
        return 0;
    }
    if (!is_alpha(s[0]) && s[0] != '_') {
        return 0;
    }
    for (i = 1; i < n; i++) {
        if (!is_alpha(s[i]) && !is_digit(s[i]) && s[i] != '_') {
            return 0;
        }
    }
    return 1;
}

/* A stable event name, as `battle.faint` spells it. */
static int is_event_name(const char *s, size_t n)
{
    size_t i;

    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];

        if (!is_alpha(c) && !is_digit(c) && c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The prototype of a stated `sig=`                                     */
/* ------------------------------------------------------------------ */

/*
 * The manifest's `sig=` states the prototype the mod expects, and the build
 * publishes the function's own prototype, normalised (tools/ovlink.py's
 * signature).  The two are compared in the same shape, so the port can tell
 * a hook whose prototype does not match the function's
 * (docs/mod-overrides.md, "The prototype is checked").
 *
 * The normalisation is the one tools/ovlink.py's signature applies:
 *
 *   - the qualifiers (const, volatile, register, static, extern, inline)
 *     are dropped;
 *   - a pointer, an array and a function pointer are all `void *`;
 *   - the game's integer typedefs are their C types (u32 is unsigned int,
 *     s16 is short, BOOL is int, and so on);
 *   - the game's own type names are the C type they resolve to (an enum is
 *     an int, a struct or a union by value is the one word the port's
 *     calling convention carries it in, so it is unsigned int; a row whose
 *     aggregate is not one word has no slot, and so no prototype);
 *   - the shape is "<return type> (<parameter types>)", the parameters
 *     separated by ", " and "void" when there are none.
 *
 * A mod that states no prototype is not compared: the port compares what is
 * stated, and nothing stated is nothing to compare.  A stated prototype the
 * port cannot read is reported and not compared either.
 */

/* ------------------------------------------------------------------ */
/* The game's own type names                                           */
/* ------------------------------------------------------------------ */

/*
 * A type the game defines itself (a typedef in one of the game's headers)
 * has a C type underneath.  The build knows the game's headers, so it
 * resolves the name; the port does not, and it is a separate program: the
 * tree is not there at run time.  The way the two agree is the table: the
 * build publishes the resolution of every name it meets, and the port reads
 * the published table (docs/mod-overrides.md, "The prototype is checked").
 *
 * The table is published by the generated link (tools/ovlink.py's
 * dispatcher) at start-up, before any mod loads.  A build that publishes
 * none has no game type names: a stated prototype that names one is then
 * reported and not compared, the way it was before the table existed.
 *
 * Every type name the game defines is published, not only the ones the
 * declared rows' prototypes use, so a stated prototype that names a game
 * type is compared whatever function it names.  A name the build resolved
 * two ways (two files do not agree) is not published: the port reads one
 * answer, so it reads none, and a stated prototype that names it is
 * reported and not compared.
 *
 * One row: the game's own type name and the C type it resolves to.  The
 * row is sdk/mod_manifest.h's BwModType, and the generated code repeats the
 * shape (tools/ovlink.py), so the two must agree.
 */

/* How many type names the table holds.  It is tools/ovlink.py's
 * BW_MOD_TYPES_MAX, and the two must agree: the generator publishes at most
 * this many names and the port keeps at most this many.  A name that does
 * not fit is not read: a stated prototype that names it is reported and
 * not compared, so there is no false refusal. */
#define BW_MOD_TYPES_MAX 4096

static BwModType   g_types[BW_MOD_TYPES_MAX];
static unsigned int g_type_count;

/* Publish the game's own type names.  The build calls this at start-up,
 * beside the declared rows' prototypes.  The strings are the generated
 * table's own and live as long as the program; the port's other publishers
 * keep the pointers the same way. */
void bw_mod_manifest_publish_types(const BwModType *rows, unsigned int count)
{
    unsigned int i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        if (rows[i].name == NULL || rows[i].type == NULL) {
            continue;
        }
        if (g_type_count >= BW_MOD_TYPES_MAX) {
            fprintf(stderr,
                    "eidos: the game's type names hold %u entries, %d fit: the "
                    "rest are not read, and a stated prototype that names one "
                    "is reported and not compared\n", count, BW_MOD_TYPES_MAX);
            return;
        }
        g_types[g_type_count].name = rows[i].name;
        g_types[g_type_count].type = rows[i].type;
        g_type_count++;
    }
}

/* The C type the game's own type name stands for, as the build published
 * it, or NULL when the build published none: a name the port cannot
 * resolve is reported and not compared, so there is no false refusal. */
static const char *game_type_of(const char *w, size_t n)
{
    unsigned int i;

    for (i = 0; i < g_type_count; i++) {
        if (strlen(g_types[i].name) == n &&
            memcmp(g_types[i].name, w, n) == 0) {
            return g_types[i].type;
        }
    }
    return NULL;
}

/* How long a stated prototype may be. */
#define BW_MOD_SIG_MAX 512

/* The C type words a prototype may hold. */
static const char *const g_base_words[] = {
    "unsigned", "signed", "char", "short", "int", "long", "void"
};

/* The game's integer typedefs, as tools/ovlink.py's INT_TYPES maps them. */
static const struct {
    const char *name;
    const char *type;
} g_int_types[] = {
    { "u8", "unsigned char" }, { "s8", "signed char" },
    { "vu8", "unsigned char" }, { "vs8", "signed char" },
    { "u16", "unsigned short" }, { "s16", "short" },
    { "vu16", "unsigned short" }, { "vs16", "short" },
    { "u32", "unsigned int" }, { "s32", "int" },
    { "vu32", "unsigned int" }, { "vs32", "int" },
    { "u64", "unsigned long long" }, { "s64", "long long" },
    { "fx64", "long long" },
    { "fx32", "int" }, { "fx16", "short" },
    { "BOOL", "int" }, { "bool", "int" }, { "_Bool", "int" },
    { "size_t", "unsigned int" }, { "OSTick", "unsigned long long" },
    { "uptr", "unsigned int" }, { "sptr", "int" }      /* address-sized: a word on this host */
};

/* One identifier, with its bounds in the text it was read from. */
typedef struct BwModWord {
    const char *start;
    size_t      len;
} BwModWord;

/* How many identifiers one prototype may hold. */
#define BW_MOD_WORDS_MAX 16

/* Is the word one of the C type words? */
static int is_base_word(const char *w, size_t n)
{
    size_t i;

    for (i = 0; i < sizeof(g_base_words) / sizeof(g_base_words[0]); i++) {
        if (strlen(g_base_words[i]) == n && memcmp(g_base_words[i], w, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The C type the game's typedef stands for, or NULL. */
static const char *int_type_of(const char *w, size_t n)
{
    size_t i;

    for (i = 0; i < sizeof(g_int_types) / sizeof(g_int_types[0]); i++) {
        if (strlen(g_int_types[i].name) == n &&
            memcmp(g_int_types[i].name, w, n) == 0) {
            return g_int_types[i].type;
        }
    }
    return NULL;
}

/* The identifiers in [s, e), in order, without the qualifiers: they say
 * nothing about the type (tools/ovlink.py's _norm_type drops them too). */
static int words_of(const char *s, const char *e, BwModWord *out, int max)
{
    int n = 0;

    while (s < e) {
        const char *start;
        size_t len;

        if (!is_alpha(*s) && *s != '_') {
            s++;
            continue;
        }
        start = s;
        while (s < e && (is_alpha(*s) || is_digit(*s) || *s == '_')) {
            s++;
        }
        len = (size_t)(s - start);
        if ((len == 5 && memcmp(start, "const", 5) == 0) ||
            (len == 8 && memcmp(start, "volatile", 8) == 0) ||
            (len == 8 && memcmp(start, "register", 8) == 0) ||
            (len == 6 && memcmp(start, "static", 6) == 0) ||
            (len == 6 && memcmp(start, "extern", 6) == 0) ||
            (len == 6 && memcmp(start, "inline", 6) == 0) ||
            (len == 7 && memcmp(start, "__inline", 7) == 0)) {
            continue;
        }
        if (n < max) {
            out[n].start = start;
            out[n].len = len;
            n++;
        }
    }
    return n;
}

/* Copy a text into a buffer.  0, or -1 when it does not fit. */
static int set_text(char *out, size_t size, const char *text)
{
    size_t n = strlen(text);

    if (n >= size) {
        return -1;
    }
    memcpy(out, text, n + 1);
    return 0;
}

/* The normalised type of [s, e) (a type without a declarator name), or -1
 * when it does not normalise. */
static int norm_type(const char *s, const char *e, char *out, size_t size)
{
    BwModWord words[BW_MOD_WORDS_MAX];
    const char *p;
    int nwords;
    int i;
    int all_base = 1;
    int has_char = 0;
    int has_short = 0;
    int has_signed = 0;
    int has_unsigned = 0;
    int n_long = 0;

    /* A pointer, an array and a function pointer all travel as void *. */
    for (p = s; p < e; p++) {
        if (*p == '*' || *p == '[' || *p == '(') {
            return set_text(out, size, "void *");
        }
    }
    nwords = words_of(s, e, words, BW_MOD_WORDS_MAX);
    if (nwords == 0) {
        return -1;
    }
    if (words[0].len == 4 && memcmp(words[0].start, "enum", 4) == 0) {
        return set_text(out, size, "int");
    }
    if ((words[0].len == 6 && memcmp(words[0].start, "struct", 6) == 0) ||
        (words[0].len == 5 && memcmp(words[0].start, "union", 5) == 0)) {
        /* A struct or a union by value is the one word the port's calling
         * convention carries it in (tools/ovlink.py's _word_aggregate). */
        return set_text(out, size, "unsigned int");
    }
    for (i = 0; i < nwords; i++) {
        if (!is_base_word(words[i].start, words[i].len)) {
            all_base = 0;
            break;
        }
        if (words[i].len == 4 && memcmp(words[i].start, "char", 4) == 0) {
            has_char = 1;
        } else if (words[i].len == 5 && memcmp(words[i].start, "short", 5) == 0) {
            has_short = 1;
        } else if (words[i].len == 4 && memcmp(words[i].start, "long", 4) == 0) {
            n_long++;
        } else if (words[i].len == 8 &&
                   memcmp(words[i].start, "unsigned", 8) == 0) {
            has_unsigned = 1;
        } else if (words[i].len == 6 && memcmp(words[i].start, "signed", 6) == 0) {
            has_signed = 1;
        }
    }
    if (all_base) {
        if (nwords == 1 && words[0].len == 4 &&
            memcmp(words[0].start, "void", 4) == 0) {
            return set_text(out, size, "void");
        }
        if (has_char) {
            if (has_unsigned) {
                return set_text(out, size, "unsigned char");
            }
            return set_text(out, size, has_signed ? "signed char" : "char");
        }
        if (has_short) {
            return set_text(out, size, has_unsigned ? "unsigned short" : "short");
        }
        if (n_long >= 2) {
            return set_text(out, size,
                            has_unsigned ? "unsigned long long" : "long long");
        }
        if (n_long == 1) {
            return set_text(out, size, has_unsigned ? "unsigned long" : "long");
        }
        return set_text(out, size, has_unsigned ? "unsigned int" : "int");
    }
    if (nwords == 1) {
        const char *type = int_type_of(words[0].start, words[0].len);

        if (type != NULL) {
            return set_text(out, size, type);
        }
        /* The game's own type name: the build published the type it
         * resolves to (bw_mod_manifest_publish_types). */
        type = game_type_of(words[0].start, words[0].len);
        if (type != NULL) {
            return set_text(out, size, type);
        }
    }
    return -1;
}

/* Copy [s, e) into `out` without the word `w`: the text of a type whose
 * declarator name is dropped (tools/ovlink.py's _strip_declarator_name). */
static void copy_without(const char *s, const char *e, const BwModWord *w,
                         char *out, size_t size)
{
    size_t head = (size_t)(w->start - s);
    size_t tail = (size_t)(e - (w->start + w->len));
    size_t n = head + tail;

    if (n >= size) {
        n = size - 1;
    }
    memcpy(out, s, head < n ? head : n);
    if (tail > 0 && head < n) {
        size_t room = n - head;

        memcpy(out + head, w->start + w->len, room < tail ? room : tail);
    }
    out[n] = '\0';
}

/* The type of a parameter: its own text without the declarator name. */
static int norm_param(const char *s, const char *e, char *out, size_t size)
{
    BwModWord words[BW_MOD_WORDS_MAX];
    char buf[BW_MOD_SIG_MAX];
    int nwords = words_of(s, e, words, BW_MOD_WORDS_MAX);

    if (nwords >= 2) {
        const BwModWord *last = &words[nwords - 1];

        if (!is_base_word(last->start, last->len) &&
            int_type_of(last->start, last->len) == NULL) {
            copy_without(s, e, last, buf, sizeof(buf));
            s = buf;
            e = buf + strlen(buf);
        }
    }
    return norm_type(s, e, out, size);
}

/* The normalised return type of a prototype: the text before the function's
 * name, less the name. */
static int norm_ret(const char *s, const char *e, char *out, size_t size)
{
    BwModWord words[BW_MOD_WORDS_MAX];
    char buf[BW_MOD_SIG_MAX];
    int nwords = words_of(s, e, words, BW_MOD_WORDS_MAX);
    const BwModWord *last;

    if (nwords == 0) {
        /* No return type at all: C89's implicit int. */
        return set_text(out, size, "int");
    }
    last = &words[nwords - 1];
    if (is_base_word(last->start, last->len) ||
        int_type_of(last->start, last->len) != NULL ||
        game_type_of(last->start, last->len) != NULL) {
        /* The last word is a type word: the prototype names no function, so
         * the whole text is the return type. */
        return norm_type(s, e, out, size);
    }
    if (nwords == 1) {
        /* One word that is not a type: it is the function's name, and the
         * return type is the implicit int. */
        return set_text(out, size, "int");
    }
    copy_without(s, e, last, buf, sizeof(buf));
    return norm_type(buf, buf + strlen(buf), out, size);
}

/* The normalised parameter list of a prototype: the types, separated by
 * ", ", or "void" when there are none. */
static int norm_params(const char *s, const char *e, char *out, size_t size)
{
    char list[BW_MOD_SIG_MAX];
    const char *p = s;
    size_t n = 0;
    int count = 0;

    list[0] = '\0';
    while (p < e && is_space(*p)) {
        p++;
    }
    while (e > p && is_space(e[-1])) {
        e--;
    }
    if (e == p) {
        /* "()": no parameters. */
        return set_text(out, size, "void");
    }
    if (e - p == 4 && memcmp(p, "void", 4) == 0) {
        /* "(void)": no parameters. */
        return set_text(out, size, "void");
    }
    while (p <= e) {
        const char *q = p;
        int depth = 0;
        char type[64];

        while (q < e) {
            if (*q == '(' || *q == '[') {
                depth++;
            } else if (*q == ')' || *q == ']') {
                depth--;
            } else if (*q == ',' && depth == 0) {
                break;
            }
            q++;
        }
        if (norm_param(p, q, type, sizeof(type)) != 0) {
            return -1;
        }
        if (strcmp(type, "void") == 0) {
            /* A parameter of type void is not a prototype. */
            return -1;
        }
        n += (size_t)snprintf(list + n, sizeof(list) - n, "%s%s",
                              count > 0 ? ", " : "", type);
        if (n >= sizeof(list)) {
            return -1;
        }
        count++;
        if (q >= e) {
            break;
        }
        p = q + 1;
    }
    if (count == 0) {
        return set_text(out, size, "void");
    }
    return set_text(out, size, list);
}

/* The normalised prototype of a stated `sig=`, in the shape the build
 * publishes: "<return type> (<parameter types>)".  0, or -1 when the text
 * cannot be read. */
static int norm_sig(const char *sig, char *out, size_t size)
{
    char work[BW_MOD_SIG_MAX];
    char ret[64];
    char params[BW_MOD_SIG_MAX];
    const char *open;
    const char *close;
    const char *p;
    int depth;
    size_t n;

    if (sig == NULL) {
        return -1;
    }
    n = strlen(sig);
    if (n == 0 || n >= sizeof(work)) {
        return -1;
    }
    memcpy(work, sig, n + 1);
    open = strchr(work, '(');
    if (open == NULL) {
        return -1;
    }
    depth = 0;
    close = NULL;
    for (p = open; *p != '\0'; p++) {
        if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            depth--;
            if (depth == 0) {
                close = p;
                break;
            }
        }
    }
    if (close == NULL) {
        return -1;
    }
    if (norm_ret(work, open, ret, sizeof(ret)) != 0) {
        return -1;
    }
    if (norm_params(open + 1, close, params, sizeof(params)) != 0) {
        return -1;
    }
    n = (size_t)snprintf(out, size, "%s (%s)", ret, params);
    if (n >= size) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The reports                                                        */
/* ------------------------------------------------------------------ */

/*
 * Every manifest problem is reported in the port's one shape:
 *
 *     eidos: mod <name>: <what is wrong>
 *
 * and the default stands: the line is ignored and the mod keeps loading.
 */
static void mod_report(const BwModManifest *m, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "eidos: mod %s: ",
            (m != NULL && m->name != NULL) ? m->name : "(unnamed)");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* The region's own spelling, as a manifest writes it. */
static const char *region_token(unsigned int region, char *buf, size_t size)
{
    if (region == BW_MOD_REGION_ARM9) {
        return "arm9";
    }
    snprintf(buf, size, "ov%03u", region);
    return buf;
}

/* ------------------------------------------------------------------ */
/* The record's life                                                  */
/* ------------------------------------------------------------------ */

/*
 * Set the defaults: the name is the directory name, the order is 0, and
 * every other key is absent.  A name that cannot be copied leaves the record
 * without one, and the reports fall back to "(unnamed)".
 *
 * The record is the caller's, and the caller may hand over one it never
 * filled, so the release path checks the mark before it frees anything.
 */
void bw_mod_manifest_free(void *manifest);   /* below */

void bw_mod_manifest_init(void *manifest, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;

    if (m == NULL) {
        return;
    }
    bw_mod_manifest_free(m);
    m->magic = BW_MOD_MANIFEST_MAGIC;
    m->name = dup_str(dir_name);
}

/* Release everything the record holds and leave it empty. */
void bw_mod_manifest_free(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;
    u32 i;

    if (m == NULL) {
        return;
    }
    if (m->magic == BW_MOD_MANIFEST_MAGIC) {
        /* The switch names the record handed to the registry are the
         * record's own, and they go with it (sdk/mod_slot.c). */
        bw_mod_slot_forget_switches(m->name);
        for (i = 0; i < m->override_count; i++) {
            free(m->overrides[i].symbol);
            free(m->overrides[i].sig);
            free(m->overrides[i].switch_name);
        }
        for (i = 0; i < m->rule_count; i++) {
            free(m->rules[i].event);
            free(m->rules[i].path);
        }
        for (i = 0; i < m->table_count; i++) {
            free(m->tables[i].event);
            free(m->tables[i].path);
        }
        free(m->tables);
        free(m->name);
        free(m->port);
        free(m->code);
        free(m->state);
        free(m->settings);
        free(m->ui_text);
        free(m->overrides);
        free(m->rules);
    }
    memset(m, 0, sizeof(*m));
}

/* A record of the caller's own, on the heap.  NULL when there is no memory. */
void *bw_mod_manifest_new(const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)malloc(sizeof(*m));

    if (m == NULL) {
        return NULL;
    }
    memset(m, 0, sizeof(*m));
    bw_mod_manifest_init(m, dir_name);
    return m;
}

/* Release a record from bw_mod_manifest_new. */
void bw_mod_manifest_delete(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;

    if (m == NULL) {
        return;
    }
    bw_mod_manifest_free(m);
    free(m);
}

/* Replace a string the record holds.  0, or -1 (no memory). */
static int set_str_n(char **slot, const char *value, size_t len)
{
    char *d = dup_n(value, len);

    if (d == NULL) {
        return -1;
    }
    free(*slot);
    *slot = d;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The namespace                                                      */
/* ------------------------------------------------------------------ */

/*
 * The reserved device names of the host file system: a folder of that name
 * is a device, not a folder, so a namespace may not be one.
 */
static const char *const g_reserved_names[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

/* The characters a namespace may hold. */
static int is_namespace_char(char c)
{
    return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '.';
}

/*
 * Is `ns` one path component?  The state path is
 * `<save dir>/mods/<namespace>/<save>.bin` and bw_save_path copies the name
 * verbatim (sdk/save.c), so a namespace that carries a separator or a ".."
 * would leave the `mods/` folder.  A namespace must be:
 *
 *   - not empty, and at most BW_MOD_NAMESPACE_MAX characters;
 *   - no separator ('/', '\') and no drive separator (':');
 *   - not the "." or the ".." component;
 *   - not a reserved device name, with or without an extension;
 *   - only letters, digits, '-', '_' and '.'.
 */
int bw_mod_manifest_namespace_valid(const char *ns)
{
    char stem[8];
    size_t n;
    size_t i;
    size_t stem_len;
    u32 r;

    if (ns == NULL) {
        return 0;
    }
    n = strlen(ns);
    if (n == 0 || n > BW_MOD_NAMESPACE_MAX) {
        return 0;
    }
    if (strcmp(ns, ".") == 0 || strcmp(ns, "..") == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = ns[i];

        if (c == '/' || c == '\\' || c == ':') {
            return 0;
        }
        if (!is_namespace_char(c)) {
            return 0;
        }
    }
    /* The device name is the part before the first dot: "CON.txt" is CON. */
    stem_len = 0;
    while (stem_len < n && ns[stem_len] != '.' && stem_len < sizeof(stem) - 1) {
        stem[stem_len] = ns[stem_len];
        stem_len++;
    }
    stem[stem_len] = '\0';
    for (r = 0; r < sizeof(g_reserved_names) / sizeof(g_reserved_names[0]); r++) {
        const char *name = g_reserved_names[r];
        size_t k;

        if (strlen(name) != stem_len) {
            continue;
        }
        for (k = 0; k < stem_len; k++) {
            if (to_lower((unsigned char)stem[k]) != to_lower((unsigned char)name[k])) {
                break;
            }
        }
        if (k == stem_len) {
            return 0;
        }
    }
    return 1;
}

/*
 * The default namespace, derived from the mod's name: lower case, the first
 * word, a dash, and the rest of the name joined.  The example of
 * docs/mod-interface.md, section 3, is the rule: the name "Nuzlocke Soul
 * Link" gives the namespace "nuzlocke-soullink".
 *
 * Every character that is not a letter or a digit becomes a dash, and a run
 * of them becomes one dash, so the result is one path component by
 * construction and needs no check.
 */
char *bw_mod_manifest_default_namespace(const char *name)
{
    char word[128];
    char *out;
    size_t n;
    size_t i;
    size_t o;
    u32 words;

    if (name == NULL) {
        name = "";
    }
    n = strlen(name);
    out = (char *)malloc(BW_MOD_NAMESPACE_MAX + 1);
    if (out == NULL) {
        return NULL;
    }
    o = 0;
    words = 0;
    for (i = 0; i < n; ) {
        size_t j;
        size_t w = 0;

        while (i < n && is_space(name[i])) {
            i++;
        }
        if (i >= n) {
            break;
        }
        j = i;
        while (j < n && !is_space(name[j])) {
            j++;
        }
        while (i < j) {
            int c = to_lower((unsigned char)name[i]);

            if (!is_alpha((char)c) && !is_digit((char)c) && c != '-' && c != '_') {
                c = '-';
            }
            if (c == '-' && w > 0 && word[w - 1] == '-') {
                i++;
                continue;
            }
            if (w < sizeof(word) - 1) {
                word[w++] = (char)c;
            }
            i++;
        }
        while (w > 0 && word[w - 1] == '-') {
            w--;
        }
        if (w == 0) {
            continue;
        }
        /* The first word, a dash, and the rest of the name joined. */
        if (words == 1 && o < BW_MOD_NAMESPACE_MAX) {
            out[o++] = '-';
        }
        if (w > BW_MOD_NAMESPACE_MAX - o) {
            w = BW_MOD_NAMESPACE_MAX - o;
        }
        memcpy(out + o, word, w);
        o += w;
        words++;
    }
    if (o == 0) {
        out[o++] = 'm';
        out[o++] = 'o';
        out[o++] = 'd';
    }
    out[o] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* The port version                                                   */
/* ------------------------------------------------------------------ */

/*
 * The port version a mod with raw hooks is pinned to (docs/mod-overrides.md,
 * "Version pinning").  The port sets it at start-up; until it does, the pin
 * cannot be checked and the manifest says so once.
 */
static const char *g_port_version;

void bw_mod_manifest_set_port_version(const char *version)
{
    g_port_version = version;
}

const char *bw_mod_manifest_port_version(void)
{
    return g_port_version;
}

/* ------------------------------------------------------------------ */
/* The parse                                                          */
/* ------------------------------------------------------------------ */

/* The region of a manifest's `<region>:<address>`: "arm9" or "ovNNN". */
static int parse_region(const char *s, size_t n, unsigned int *out)
{
    if (n == 4 && memcmp(s, "arm9", 4) == 0) {
        *out = BW_MOD_REGION_ARM9;
        return 0;
    }
    if (n > 2 && s[0] == 'o' && s[1] == 'v') {
        u32 v = 0;
        size_t i;

        for (i = 2; i < n; i++) {
            if (!is_digit(s[i])) {
                return -1;
            }
            v = v * 10u + (u32)(s[i] - '0');
            if (v > 0xFFFFu) {
                return -1;
            }
        }
        *out = BW_MOD_REGION_OVL(v);
        return 0;
    }
    return -1;
}

/* The address of a manifest's `<region>:<address>`, in hex. */
static int parse_addr(const char *s, size_t n, unsigned int *out)
{
    u64 v = 0;
    size_t i = 0;

    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
    }
    if (i >= n) {
        return -1;
    }
    for (; i < n; i++) {
        char c = s[i];
        u32 d;

        if (c >= '0' && c <= '9') {
            d = (u32)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (u32)(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
            d = (u32)(c - 'A') + 10u;
        } else {
            return -1;
        }
        v = v * 16u + (u64)d;
        if (v > 0xFFFFFFFFull) {
            return -1;
        }
    }
    *out = (unsigned int)v;
    return 0;
}

/*
 * Split a value into its tokens.  A token is a run of blanks, except that a
 * quoted part may hold blanks: `sig="int f(int)"` is one token.  At most
 * `max` tokens are taken; the count is returned.
 */
static int tokenize(const char *s, const char *e,
                    const char **tok, size_t *len, int max)
{
    int n = 0;

    while (s < e && n < max) {
        const char *start;

        while (s < e && is_space(*s)) {
            s++;
        }
        if (s >= e) {
            break;
        }
        start = s;
        while (s < e) {
            if (*s == '"') {
                s++;
                while (s < e && *s != '"') {
                    s++;
                }
                if (s < e) {
                    s++;
                }
                continue;
            }
            if (is_space(*s)) {
                break;
            }
            s++;
        }
        tok[n] = start;
        len[n] = (size_t)(s - start);
        n++;
    }
    return n;
}

/* Is the token the word `word`?  The length comes from the word, so a
 * spelling cannot drift. */
static int token_is(const char *tok, size_t len, const char *word)
{
    size_t n = strlen(word);

    return len == n && memcmp(tok, word, n) == 0;
}

/* The value of a `key=value` token, without the quotes around it. */
static void option_value(const char *tok, size_t len, const char **value, size_t *value_len)
{
    const char *eq = memchr(tok, '=', len);
    const char *v;
    size_t n;

    *value = "";
    *value_len = 0;
    if (eq == NULL) {
        return;
    }
    v = eq + 1;
    n = len - (size_t)(v - tok);
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        v++;
        n -= 2;
    }
    *value = v;
    *value_len = n;
}

/* Append one override to the record.  0, or -1 (no memory). */
static int push_override(BwModManifest *m, const BwModOverride *o)
{
    if (m->override_count == m->override_cap) {
        u32 cap = m->override_cap == 0 ? 4u : m->override_cap * 2u;
        BwModOverride *grown =
            (BwModOverride *)realloc(m->overrides, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        m->overrides = grown;
        m->override_cap = cap;
    }
    m->overrides[m->override_count++] = *o;
    return 0;
}

/* Append one rule to the record.  0, or -1 (no memory). */
static int push_rule(BwModManifest *m, const BwModRule *r)
{
    if (m->rule_count == m->rule_cap) {
        u32 cap = m->rule_cap == 0 ? 4u : m->rule_cap * 2u;
        BwModRule *grown = (BwModRule *)realloc(m->rules, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        m->rules = grown;
        m->rule_cap = cap;
    }
    m->rules[m->rule_count++] = *r;
    return 0;
}

/*
 * Parse one `override` value:
 *
 *     <region>:<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
 *
 * A bare address is refused: the region is part of the key, because two
 * overlays share one RAM window (docs/mod-interface.md, section 1).
 */
static int parse_override(BwModManifest *m, const char *value, size_t value_len)
{
    const char *tok[6];
    size_t len[6];
    const char *colon;
    size_t region_len;
    const char *addr_text;
    size_t addr_len;
    unsigned int region;
    unsigned int addr;
    BwModOverride o;
    const char *opt_value;
    size_t opt_len;
    int ntok;
    int i;

    memset(&o, 0, sizeof(o));
    ntok = tokenize(value, value + value_len, tok, len, 6);
    if (ntok < 3) {
        return -1;
    }
    colon = memchr(tok[0], ':', len[0]);
    if (colon == NULL) {
        return -1;
    }
    region_len = (size_t)(colon - tok[0]);
    addr_text = colon + 1;
    addr_len = len[0] - region_len - 1;
    if (parse_region(tok[0], region_len, &region) != 0) {
        return -1;
    }
    if (parse_addr(addr_text, addr_len, &addr) != 0) {
        return -1;
    }
    if (token_is(tok[1], len[1], "replace")) {
        o.mode = BW_MOD_OVERRIDE_REPLACE;
    } else if (token_is(tok[1], len[1], "wrap")) {
        o.mode = BW_MOD_OVERRIDE_WRAP;
    } else {
        return -1;
    }
    if (!is_ident(tok[2], len[2])) {
        return -1;
    }
    o.region = region;
    /* The low bit is masked: the even start and the Thumb-tagged value a
     * code pointer holds are the same function. */
    o.addr = addr & ~1u;
    o.addr_tagged = o.addr | 1u;
    o.symbol = dup_n(tok[2], len[2]);
    if (o.symbol == NULL) {
        return -1;
    }
    for (i = 3; i < ntok; i++) {
        const char *key = tok[i];
        size_t key_len = len[i];

        option_value(key, key_len, &opt_value, &opt_len);
        if (key_len > 4 && memcmp(key, "sig=", 4) == 0) {
            if (o.sig != NULL) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
            o.sig = dup_n(opt_value, opt_len);
            if (o.sig == NULL) {
                free(o.symbol);
                return -1;
            }
        } else if (key_len > 7 && memcmp(key, "switch=", 7) == 0) {
            if (o.switch_name != NULL || opt_len == 0) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
            o.switch_name = dup_n(opt_value, opt_len);
            if (o.switch_name == NULL) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
        } else {
            free(o.symbol);
            free(o.sig);
            free(o.switch_name);
            return -1;
        }
    }
    if (push_override(m, &o) != 0) {
        free(o.symbol);
        free(o.sig);
        free(o.switch_name);
        return -1;
    }
    return 0;
}

/* Parse one `rule` value: `<event> <path>`. */
static int parse_rule(BwModManifest *m, const char *value, size_t value_len)
{
    const char *tok[4];
    size_t len[4];
    BwModRule r;
    int ntok;

    memset(&r, 0, sizeof(r));
    ntok = tokenize(value, value + value_len, tok, len, 4);
    if (ntok != 2) {
        return -1;
    }
    if (!is_event_name(tok[0], len[0]) || len[1] == 0) {
        return -1;
    }
    r.event = dup_n(tok[0], len[0]);
    r.path = dup_n(tok[1], len[1]);
    if (r.event == NULL || r.path == NULL) {
        free(r.event);
        free(r.path);
        return -1;
    }
    if (push_rule(m, &r) != 0) {
        free(r.event);
        free(r.path);
        return -1;
    }
    return 0;
}

/*
 * Parse one `table` value: `<name> <path>`.  The name is a word of letters,
 * digits and `_`, the way the rules spell it (`table.<name>[key]`).  The
 * record reuses the rule's two strings: `event` holds the name.
 */
static int parse_table(BwModManifest *m, const char *value, size_t value_len)
{
    const char *tok[4];
    size_t len[4];
    BwModRule t;
    size_t i;
    int ntok;

    ntok = tokenize(value, value + value_len, tok, len, 4);
    if (ntok != 2 || len[0] == 0 || len[0] >= 32 || len[1] == 0) {
        return -1;
    }
    for (i = 0; i < len[0]; i++) {
        char c = tok[0][i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
              (i > 0 && c >= '0' && c <= '9'))) {
            return -1;
        }
    }
    t.event = dup_n(tok[0], len[0]);
    t.path = dup_n(tok[1], len[1]);
    if (t.event == NULL || t.path == NULL) {
        free(t.event);
        free(t.path);
        return -1;
    }
    if (m->table_count == m->table_cap) {
        u32 cap = m->table_cap == 0 ? 4u : m->table_cap * 2u;
        BwModRule *grown = (BwModRule *)realloc(m->tables, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            free(t.event);
            free(t.path);
            return -1;
        }
        m->tables = grown;
        m->table_cap = cap;
    }
    m->tables[m->table_count++] = t;
    return 0;
}

/* A signed integer, whole text only. */
static int parse_int(const char *s, size_t n, long *out)
{
    long v = 0;
    size_t i = 0;
    int neg = 0;

    if (n == 0) {
        return -1;
    }
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= n) {
        return -1;
    }
    for (; i < n; i++) {
        if (!is_digit(s[i])) {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
        if (v > 2000000000L) {
            return -1;
        }
    }
    *out = neg ? -v : v;
    return 0;
}

/*
 * Parse the manifest's text into the record.
 *
 * `#` starts a comment, each line is `key = value`, a line with no `=` is
 * skipped, and an unknown key is ignored on purpose, so a later version can
 * add keys (sdk/vfs.c).  A malformed line is reported once and the default
 * stands.
 */
int bw_mod_manifest_parse(void *manifest, const char *text, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;
    const char *p;

    if (m == NULL) {
        return -1;
    }
    bw_mod_manifest_init(m, dir_name);
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        char line[1024];
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char *hash;
        char *eq;
        size_t n;
        size_t key_len;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            p = (*e == '\0') ? e : e + 1;
            continue;
        }
        ks = line;
        ke = eq;
        vs = eq + 1;
        ve = line + strlen(line);
        trim(&ks, &ke);
        trim(&vs, &ve);
        key_len = (size_t)(ke - ks);

        if (key_len == 4 && memcmp(ks, "name", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty name, ignored");
            } else if (set_str_n(&m->name, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 5 && memcmp(ks, "order", 5) == 0) {
            long v;

            if (parse_int(vs, (size_t)(ve - vs), &v) != 0 ||
                v < -1000000L || v > 1000000L) {
                mod_report(m, "bad order value '%.*s', ignored", (int)(ve - vs), vs);
            } else {
                m->order = (int)v;
            }
        } else if (key_len == 11 && memcmp(ks, "multiplayer", 11) == 0) {
            long v;

            if (parse_int(vs, (size_t)(ve - vs), &v) != 0 ||
                v < 1L || v > 1000000L) {
                mod_report(m, "bad multiplayer value '%.*s' (a number from 1), "
                           "ignored", (int)(ve - vs), vs);
            } else {
                m->multiplayer = (int)v;
            }
        } else if (key_len == 3 && memcmp(ks, "api", 3) == 0) {
            long v;

            if (parse_int(vs, (size_t)(ve - vs), &v) != 0) {
                mod_report(m, "bad api value '%.*s', ignored", (int)(ve - vs), vs);
            } else {
                m->api = (int)v;
                m->has_api = 1;
            }
        } else if (key_len == 4 && memcmp(ks, "port", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty port version, ignored");
            } else if (set_str_n(&m->port, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 4 && memcmp(ks, "code", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty code path, ignored");
            } else if (set_str_n(&m->code, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 5 && memcmp(ks, "state", 5) == 0) {
            if (ve == vs) {
                mod_report(m, "empty state namespace, ignored");
            } else if (set_str_n(&m->state, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 8 && memcmp(ks, "override", 8) == 0) {
            if (parse_override(m, vs, (size_t)(ve - vs)) != 0) {
                mod_report(m, "bad override '%.*s', ignored", (int)(ve - vs), vs);
            }
        } else if (key_len == 4 && memcmp(ks, "rule", 4) == 0) {
            if (parse_rule(m, vs, (size_t)(ve - vs)) != 0) {
                mod_report(m, "bad rule '%.*s', ignored", (int)(ve - vs), vs);
            }
        } else if (key_len == 5 && memcmp(ks, "table", 5) == 0) {
            if (parse_table(m, vs, (size_t)(ve - vs)) != 0) {
                mod_report(m, "bad table '%.*s', ignored", (int)(ve - vs), vs);
            }
        } else if (key_len == 8 && memcmp(ks, "settings", 8) == 0) {
            if (ve == vs) {
                mod_report(m, "empty settings path, ignored");
            } else if (set_str_n(&m->settings, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 7 && memcmp(ks, "ui_text", 7) == 0) {
            if (ve == vs) {
                mod_report(m, "empty ui_text path, ignored");
            } else if (set_str_n(&m->ui_text, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        }
        /* An unknown key is ignored on purpose: a later version may add
         * keys, and an old port must not refuse a newer manifest. */
        p = (*e == '\0') ? e : e + 1;
    }
    return 0;
}

/* The name of the directory a root names: its last path component. */
static int root_dir_name(const char *root, char *buf, size_t size)
{
    const char *end;
    const char *start;
    size_t n;

    if (root == NULL || size == 0) {
        return -1;
    }
    end = root + strlen(root);
    while (end > root && (end[-1] == '/' || end[-1] == '\\')) {
        end--;
    }
    start = end;
    while (start > root && start[-1] != '/' && start[-1] != '\\') {
        start--;
    }
    if (start == end) {
        return -1;
    }
    n = (size_t)(end - start);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(buf, start, n);
    buf[n] = '\0';
    return 0;
}

/* Read a file into memory.  A NULL result means the file is not there. */
static char *read_file(const char *path, size_t *size_out)
{
    FILE *fp;
    char *buf;
    size_t cap = 4096;
    size_t len = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    for (;;) {
        size_t got;

        if (len + 1 >= cap) {
            char *grown = (char *)realloc(buf, cap * 2);

            if (grown == NULL) {
                free(buf);
                fclose(fp);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        got = fread(buf + len, 1, cap - len - 1, fp);
        len += got;
        if (got == 0) {
            break;
        }
    }
    buf[len] = '\0';
    fclose(fp);
    if (size_out != NULL) {
        *size_out = len;
    }
    return buf;
}

/*
 * Read `<root>/eidos_mod.txt` and parse it.  A mod with no manifest is not an
 * error: the defaults stand.  `dir_name` is the name the namespace is
 * derived from when the manifest states none; NULL takes it from the root.
 */
int bw_mod_manifest_parse_dir(void *manifest, const char *root, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;
    char path[1200];
    char fallback[256];
    char *text;
    const char *name = dir_name;
    int result;

    if (m == NULL || root == NULL) {
        return -1;
    }
    if (name == NULL) {
        if (root_dir_name(root, fallback, sizeof(fallback)) != 0) {
            fallback[0] = '\0';
        }
        name = fallback;
    }
    if (strlen(root) + 1 + sizeof(BW_MOD_MANIFEST_NAME) > sizeof(path)) {
        return -1;
    }
    sprintf(path, "%s/%s", root, BW_MOD_MANIFEST_NAME);
    text = read_file(path, NULL);
    if (text == NULL) {
        bw_mod_manifest_init(m, name);
        return 0;
    }
    result = bw_mod_manifest_parse(m, text, name);
    free(text);
    return result;
}

/* ------------------------------------------------------------------ */
/* The applied manifests                                              */
/* ------------------------------------------------------------------ */

/*
 * The manifests the apply has taken, in load order.  The interpreter and
 * the settings surface read the rule list and the settings path from here.
 * The port forgets the list when it resets the mod layer.
 */
static const BwModManifest **g_applied;
static u32 g_applied_count;
static u32 g_applied_cap;

static void applied_add(const BwModManifest *m)
{
    u32 i;

    for (i = 0; i < g_applied_count; i++) {
        if (g_applied[i] == m) {
            return;
        }
    }
    if (g_applied_count == g_applied_cap) {
        u32 cap = g_applied_cap == 0 ? 8u : g_applied_cap * 2u;
        const BwModManifest **grown =
            (const BwModManifest **)realloc(g_applied, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return;
        }
        g_applied = grown;
        g_applied_cap = cap;
    }
    g_applied[g_applied_count++] = m;
}

u32 bw_mod_manifest_applied_count(void)
{
    return g_applied_count;
}

const void *bw_mod_manifest_applied_at(u32 i)
{
    if (i >= g_applied_count) {
        return NULL;
    }
    return g_applied[i];
}

/* Forget the applied manifests: the port's reset. */
void bw_mod_manifest_applied_clear(void)
{
    free(g_applied);
    g_applied = NULL;
    g_applied_count = 0;
    g_applied_cap = 0;
}

/* ------------------------------------------------------------------ */
/* The apply                                                          */
/* ------------------------------------------------------------------ */

/*
 * The mod's stated prototype against the one the build published for the
 * row.  1 when the override may be installed, 0 when it is refused.
 *
 * The build publishes a prototype for every row whose definition it
 * carries; a row it does not carry has none, and there is nothing to
 * compare with.  A stated prototype the port cannot read is reported and not
 * compared: the port does not know what it says, so it does not refuse the
 * hook over it.  A stated prototype that reads and does not match is
 * refused, and the report names both.
 */
static int sig_agrees(const BwModManifest *m, const BwModOverride *o,
                       const char *region)
{
    const char *expected = bw_mod_slot_prototype(o->region, o->addr);
    char stated[BW_MOD_SIG_MAX];

    if (o->sig == NULL || expected == NULL) {
        return 1;
    }
    if (norm_sig(o->sig, stated, sizeof(stated)) != 0) {
        mod_report(m, "override %s:0x%08X states a prototype the port cannot "
                   "read: '%s' (the function's is '%s')",
                   region, o->addr, o->sig, expected);
        return 1;
    }
    if (strcmp(stated, expected) != 0) {
        mod_report(m, "override %s:0x%08X states the prototype '%s', but the "
                   "function's is '%s'", region, o->addr, stated, expected);
        return 0;
    }
    return 1;
}

/*
 * Turn one manifest into registrations.  One call per mod, in load order.
 *
 *   1. Check `api`, and `port` when the mod has raw hooks, against the
 *      port's version; a mismatch refuses the mod.
 *   2. Derive the namespace from `state`, or from the name when `state` is
 *      absent, and check it is one path component.
 *   3. Check every override against the declared table; a pair that is not
 *      declared is reported and skipped.  Check the stated prototype against
 *      the one the build published for the row; a mismatch is reported and
 *      the override is skipped.  Hand the switch the row named to the
 *      registry, which asks it when the chain is walked.
 *   4. Leave the rule list and the settings path in the record, for the
 *      interpreter and the settings surface.
 *
 * The overrides themselves are installed by the loader, which resolves the
 * symbol in the loaded library (docs/mod-implementation.md, section 6).  The
 * apply leaves in the record the overrides whose pair is declared, in the
 * order the manifest gave them, and the loader installs them in that order.
 *
 * 0, or -1 when the mod is refused and the loader skips it.
 */
int bw_mod_manifest_apply(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;
    u32 i;
    u32 keep;

    if (m == NULL) {
        return -1;
    }

    /* 1. The versions. */
    if (m->has_api && (m->api < BW_MOD_API_MIN || m->api > BW_MOD_API_VERSION)) {
        mod_report(m, "api %d does not match the port's %d",
                   m->api, BW_MOD_API_VERSION);
        return -1;
    }
    if (m->override_count > 0 && m->port != NULL) {
        if (g_port_version == NULL || *g_port_version == '\0') {
            mod_report(m, "the port version is unknown, the pin on '%s' is not checked",
                       m->port);
        } else if (strcmp(m->port, g_port_version) != 0) {
            mod_report(m, "port '%s' does not match the port's '%s'",
                       m->port, g_port_version);
            return -1;
        }
    }

    /* 2. The namespace.  A stated namespace that is not one path component
     * is reported and the default stands. */
    if (m->state != NULL && !bw_mod_manifest_namespace_valid(m->state)) {
        mod_report(m, "bad state namespace '%s', the default stands", m->state);
        free(m->state);
        m->state = NULL;
    }
    if (m->state == NULL) {
        m->state = bw_mod_manifest_default_namespace(m->name);
        if (m->state == NULL) {
            return -1;
        }
    }

    /* 3. The declared check, the prototype check and the switch.  The
     * address is normalised first: the low bit is masked, so the even start
     * and the Thumb-tagged value a code pointer holds are the same
     * function. */
    keep = 0;
    for (i = 0; i < m->override_count; i++) {
        BwModOverride *o = &m->overrides[i];
        char region_buf[16];
        const char *region;

        region = region_token(o->region, region_buf, sizeof(region_buf));
        if (!bw_mod_slot_declared(o->region, o->addr)) {
            mod_report(m, "override %s:0x%08X is not a declared address",
                       region, o->addr);
            free(o->symbol);
            free(o->sig);
            free(o->switch_name);
            continue;
        }
        if (!sig_agrees(m, o, region)) {
            /* The stated prototype is not the function's: the override is
             * refused, and the mod keeps the rest of its manifest. */
            free(o->symbol);
            free(o->sig);
            free(o->switch_name);
            continue;
        }
        /* The switch: the name the mod stated, or the row's own name when
         * it stated none.  The apply hands it to the registry, which asks
         * it when the chain is walked: a switch that is off skips that
         * mod's hook and the walk goes on inward (docs/mod-overrides.md,
         * "The switch"). */
        bw_mod_slot_set_switch(m->name, o->region, o->addr, o->switch_name);
        if (keep != i) {
            m->overrides[keep] = m->overrides[i];
        }
        keep++;
    }
    m->override_count = keep;

    /* 4. The rules and the settings path are in the record; the applied
     * list is what the interpreter and the settings surface read. */
    applied_add(m);
    return 0;
}

/* ------------------------------------------------------------------ */
/* What the loader reads                                              */
/* ------------------------------------------------------------------ */

u32 bw_mod_manifest_override_count(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->override_count : 0u;
}

const void *bw_mod_manifest_override_at(const void *manifest, u32 i)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    if (m == NULL || i >= m->override_count) {
        return NULL;
    }
    return &m->overrides[i];
}

u32 bw_mod_manifest_rule_count(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->rule_count : 0u;
}

const void *bw_mod_manifest_rule_at(const void *manifest, u32 i)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    if (m == NULL || i >= m->rule_count) {
        return NULL;
    }
    return &m->rules[i];
}

u32 bw_mod_manifest_table_count(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->table_count : 0u;
}

const void *bw_mod_manifest_table_at(const void *manifest, u32 i)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    if (m == NULL || i >= m->table_count) {
        return NULL;
    }
    return &m->tables[i];
}

/*
 * Whether the manifest stated an api version at all.  A mod with code and
 * no api is refused by the loader: the version cannot be checked, and the
 * rule is that it cannot be forgotten (docs/mod-abi.md, section 3).
 */
int bw_mod_manifest_has_api(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->has_api : 0;
}

const char *bw_mod_manifest_name(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->name : NULL;
}

const char *bw_mod_manifest_code(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->code : NULL;
}

const char *bw_mod_manifest_state(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->state : NULL;
}

const char *bw_mod_manifest_settings(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->settings : NULL;
}

const char *bw_mod_manifest_ui_text(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->ui_text : NULL;
}
