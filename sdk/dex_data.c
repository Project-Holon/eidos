/*
 * sdk/dex_data.c -- the Pokedex's data for species a mod adds.
 *
 * The species limit (sdk/bw_limits.c, limit.species, up to 2047) lets a mod
 * add species past 649.  The game's Pokedex reads three things that do not
 * grow with it, and this file gives each its larger form:
 *
 *   The regional table (a/0/1/6 member 668): one u16 a species, its Unova
 *   number (Victini 0), 999 for one not in the Unova Pokedex.  The game has
 *   650 rows and every reader indexes it by species with no check.  Here it
 *   has a row for each species up to the limit (999 past the game's own),
 *   with the numbers mods give in eidos/tables/regional_dex.txt:
 *
 *       <species> = <Unova number> | unova | none
 *
 *   `unova` puts the species in the Unova Pokedex after the highest number
 *   (several of them in species order), so a mod need not pick numbers.
 *
 *   POKE_PERSONAL_LoadFormTable (arm9 poke_personal.c) hands the game this
 *   table; the one-screen interface reads it through bw_dex_regional().
 *
 *   Which ids are species: 1..649, and from 715 (the first id past the
 *   game's forms and eggs in every archive a species indexes) up to the
 *   limit, each one a mod ships a stats record for (a/0/1/6 member n).
 *
 *   The search's two values a species or form has that no record of the
 *   game holds (a/0/9/8 members 1 and 2): its body shape (0..13) and its
 *   name's letter group (the first letter, A 0 .. Z 25 in the Latin-script
 *   languages).  A mod gives them in eidos/tables/pokedex.txt:
 *
 *       <species>[.<form>] = shape <0..13> [letter <n>]
 *
 *   The letter is the species' own (a form line's is not read).  Without
 *   one the search takes it from the name (stubs/ov/patch_ov212_*.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "port.h"
#include "types.h"
#include "vfs.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

extern void *GFL_HEAP_Alloc(u32 heapId, u32 size, int clear, const char *site, u16 tag) GAME_FN;

#define REGIONAL_MEMBER 0x29Cu          /* a/0/1/6 member 668 */
#define NOT_REGIONAL    999u
#define UNOVA_NEXT      0xFFFEu          /* a `unova` line, before its number is known */
#define DEX_IDS         2048u           /* the species limit's ceiling + 1 */
#define FIRST_MOD_ID    715u
#define DEX_FORMS       32u             /* forms a table line can name (0..31) */

static u16 *g_regional;                 /* rows: max(the game's, limit + 1) */
static u32 g_regional_rows;
static s8 g_exists[DEX_IDS];            /* 0 not worked out, 1 a species, -1 not */
static u8 *g_shape;                     /* [species * DEX_FORMS + form]: shape + 1, 0 none */
static u8 g_letter[DEX_IDS];            /* letter + 1, 0 none */
static int g_tables_done;

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > (1L << 22) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if ((buf = (char *)malloc((size_t)n + 1)) == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Each line of a mod's eidos/tables/<name>.txt, comments and blanks gone,
 * in mod order (a later mod's line wins). */
static void each_line(const char *name, void (*fn)(const char *path, u32 line, char *text))
{
    u32 m;

    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200], *text, *p;
        const char *root = bw_mod_root_at(m);
        u32 line = 0;

        if (root == NULL || strlen(root) + strlen(name) + 24 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/%s.txt", root, name);
        if ((text = read_text(path)) == NULL) {
            continue;
        }
        for (p = text; *p != '\0';) {
            char *s = p, *e, *h;

            while (*p != '\0' && *p != '\n') {
                p++;
            }
            e = p;
            if (*p == '\n') {
                p++;
            }
            line++;
            for (h = s; h < e && *h != '#'; h++) {
            }
            e = h;
            while (s < e && (*s == ' ' || *s == '\t' || *s == '\r')) {
                s++;
            }
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
                e--;
            }
            if (s < e) {
                char buf[160];
                size_t n = (size_t)(e - s) < sizeof buf - 1 ? (size_t)(e - s) : sizeof buf - 1;

                memcpy(buf, s, n);
                buf[n] = '\0';
                fn(path, line, buf);
            }
        }
        free(text);
    }
}

/* ------------------------------------------------------------------ */
/* The regional table                                                  */
/* ------------------------------------------------------------------ */

static void regional_line(const char *path, u32 line, char *text)
{
    unsigned species, number;
    char rest[16];

    if (sscanf(text, "%u = %15s", &species, rest) == 2 && species < g_regional_rows) {
        if (strcmp(rest, "none") == 0) {
            g_regional[species] = NOT_REGIONAL;
            return;
        }
        if (strcmp(rest, "unova") == 0) {
            g_regional[species] = UNOVA_NEXT;      /* (numbered once every line is read) */
            return;
        }
        if (sscanf(rest, "%u", &number) == 1 && number < NOT_REGIONAL) {
            g_regional[species] = (u16)number;
            return;
        }
    }
    fprintf(stderr, "eidos: %s:%u: expected `<species 1..%u> = <Unova number 0..998> | unova | none`\n", path,
            (unsigned)line,
            (unsigned)(g_regional_rows - 1u));
}

static void regional_build(void)
{
    BwArcFile arc;
    u32 i, game = 0, need = bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST) + 1u;
    u8 *raw = NULL;

    if (g_regional != NULL) {
        return;
    }
    if (bw_arc_open(&arc, "a/0/1/6") == 0) {
        long n = REGIONAL_MEMBER < arc.count ? bw_arc_member_size(&arc, REGIONAL_MEMBER) : -1;

        if (n > 0 && (raw = (u8 *)malloc((size_t)n)) != NULL
            && bw_arc_read_member(&arc, REGIONAL_MEMBER, raw, (u32)n) == n) {
            game = (u32)n / 2u;
        }
        bw_arc_close(&arc);
    }
    g_regional_rows = game > need ? game : need;
    if ((g_regional = (u16 *)malloc(g_regional_rows * sizeof(u16))) == NULL) {
        free(raw);
        g_regional_rows = 0;
        return;
    }
    for (i = 0; i < g_regional_rows; i++) {
        g_regional[i] = i < game ? (u16)(raw[i * 2u] | raw[i * 2u + 1u] << 8) : (u16)NOT_REGIONAL;
    }
    free(raw);
    each_line("regional_dex", regional_line);
    /* `unova`: the numbers after the highest one, in species order */
    {
        u32 high = 0;
        int any = 0;

        for (i = 0; i < g_regional_rows; i++) {
            if (g_regional[i] < NOT_REGIONAL && (!any || g_regional[i] > high)) {
                high = g_regional[i];
                any = 1;
            }
        }
        for (i = 0; i < g_regional_rows; i++) {
            if (g_regional[i] == UNOVA_NEXT) {
                high = any ? high + 1u : 0u;
                any = 1;
                g_regional[i] = high < NOT_REGIONAL ? (u16)high : (u16)NOT_REGIONAL;
            }
        }
    }
}

int bw_dex_regional(u32 species)
{
    regional_build();
    return species < g_regional_rows && g_regional[species] != NOT_REGIONAL ? (int)g_regional[species] : -1;
}

/* The game's loader of the regional table (arm9 poke_personal.c): the same
 * block on the caller's heap (it frees it), the rows above, and the count
 * of rows in the regional Pokedex. */
u16 *POKE_PERSONAL_LoadFormTable(u32 heap, u16 *countOut)
{
    u16 *table;
    u16 count = 0;
    u32 i;

    regional_build();
    table = (u16 *)GFL_HEAP_Alloc(heap, (g_regional_rows > 0 ? g_regional_rows : 1u) * 2u, 0, "dex_data.c", 0);
    for (i = 0; i < g_regional_rows; i++) {
        table[i] = g_regional[i];
        if (table[i] != NOT_REGIONAL) {
            count++;
        }
    }
    if (countOut != NULL) {
        *countOut = count;
    }
    return table;
}

/* ------------------------------------------------------------------ */
/* Which ids are species                                               */
/* ------------------------------------------------------------------ */

int bw_dex_species_exists(u32 species)
{
    if (species == 0 || species > bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST) || species >= DEX_IDS) {
        return 0;
    }
    if (species <= 649u) {
        return 1;
    }
    if (species < FIRST_MOD_ID) {
        return 0;
    }
    if (g_exists[species] == 0) {
        BwArcFile arc;

        g_exists[species] = -1;
        if (bw_arc_open(&arc, "a/0/1/6") == 0) {
            if (species < arc.count && bw_arc_member_size(&arc, species) > 0) {
                g_exists[species] = 1;
            }
            bw_arc_close(&arc);
        }
    }
    return g_exists[species] == 1;
}

/* ------------------------------------------------------------------ */
/* Dex records and the form chain                                      */
/* ------------------------------------------------------------------ */

/*
 * The dex's records (a/0/9/8 members 0 and 2, the text banks of heights and
 * weights): one a species 0..649, then one a form of the game's species
 * (651..726, in the form chain of member 0).  A species a mod adds, and each
 * of its forms (a stats slot from 715), has record 727 + (id - 715).
 */
#define GAME_RECORDS 727u

u32 bw_dex_record(u32 id)
{
    return id < FIRST_MOD_ID ? id : GAME_RECORDS + (id - FIRST_MOD_ID);
}

/* A species' forms past its first that are a mod's (formCount - 1, at the
 * stats slots formStatsIndex + f - 1 from 715 on), read from its stats
 * record (a/0/1/6 member n: formStatsIndex at 0x1C, formCount at 0x20). */
u32 bw_dex_mod_forms(u32 species, u32 *firstSlot)
{
    BwArcFile arc;
    u8 rec[0x3C];
    u32 count = 0, first = 0;

    if (bw_arc_open(&arc, "a/0/1/6") == 0) {
        if (species < arc.count && bw_arc_read_member(&arc, species, rec, sizeof rec) == (long)sizeof rec) {
            first = (u32)rec[0x1C] | (u32)rec[0x1D] << 8;
            count = rec[0x20];
        }
        bw_arc_close(&arc);
    }
    *firstSlot = first;
    return count > 1u && first >= FIRST_MOD_ID ? count - 1u : 0u;
}

/* The form chain (a/0/9/8 member 0, one u16 a record: the record of the
 * next form, 0 after the last) with the added species' chains, on the
 * game's heap (GFL_HEAP_Free).  *rows its length. */
u16 *bw_dex_links(u32 heap, u32 *rows)
{
    BwArcFile arc;
    u8 *raw = NULL;
    u32 game = 0, n, s, i, last = bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST);
    u16 *links;

    if (bw_arc_open(&arc, "a/0/9/8") == 0) {
        long size = arc.count > 0 ? bw_arc_member_size(&arc, 0) : -1;

        if (size > 0 && (raw = (u8 *)malloc((size_t)size)) != NULL
            && bw_arc_read_member(&arc, 0, raw, (u32)size) == size) {
            game = (u32)size / 2u;
        }
        bw_arc_close(&arc);
    }
    n = game > GAME_RECORDS ? game : GAME_RECORDS;
    for (s = FIRST_MOD_ID; s <= last && s < DEX_IDS; s++) {
        u32 first, count;

        if (!bw_dex_species_exists(s)) {
            continue;
        }
        count = bw_dex_mod_forms(s, &first);
        if (bw_dex_record(s) + 1u > n) {
            n = bw_dex_record(s) + 1u;
        }
        if (count > 0 && bw_dex_record(first + count - 1u) + 1u > n) {
            n = bw_dex_record(first + count - 1u) + 1u;
        }
    }
    links = (u16 *)GFL_HEAP_Alloc(heap, n * 2u, 0, "dex_data.c", 1);
    for (i = 0; i < n; i++) {
        links[i] = i < game ? (u16)(raw[i * 2u] | raw[i * 2u + 1u] << 8) : 0;
    }
    free(raw);
    for (s = FIRST_MOD_ID; s <= last && s < DEX_IDS; s++) {
        u32 first, count, k, r;

        if (!bw_dex_species_exists(s)) {
            continue;
        }
        count = bw_dex_mod_forms(s, &first);
        r = bw_dex_record(s);
        for (k = 1; k <= count; k++) {
            links[r] = (u16)bw_dex_record(first + k - 1u);
            r = links[r];
            links[r] = 0;
        }
    }
    *rows = n;
    return links;
}

/* ------------------------------------------------------------------ */
/* Shapes and letters                                                  */
/* ------------------------------------------------------------------ */

static void pokedex_line(const char *path, u32 line, char *text)
{
    unsigned species, form = 0, v;
    char key[16], *p = text;
    int n, any = 0;

    if (sscanf(p, "%u.%u = %n", &species, &form, &n) == 2 || (form = 0, sscanf(p, "%u = %n", &species, &n) == 1)) {
        p += n;
        while (species < DEX_IDS && form < DEX_FORMS && sscanf(p, "%15s %u%n", key, &v, &n) == 2) {
            if (strcmp(key, "shape") == 0 && v <= 13u) {
                g_shape[species * DEX_FORMS + form] = (u8)(v + 1u);
            } else if (strcmp(key, "letter") == 0 && v < 255u && form == 0) {
                g_letter[species] = (u8)(v + 1u);
            } else {
                break;
            }
            any = 1;
            p += n;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }
        if (any && *p == '\0') {
            return;
        }
    }
    fprintf(stderr, "eidos: %s:%u: expected `<species>[.<form>] = shape <0..13> [letter <n>]`\n", path, (unsigned)line);
}

static void tables_build(void)
{
    if (g_tables_done) {
        return;
    }
    g_tables_done = 1;
    memset(g_letter, 0, sizeof g_letter);
    if (g_shape == NULL && (g_shape = (u8 *)malloc(DEX_IDS * DEX_FORMS)) == NULL) {
        return;
    }
    memset(g_shape, 0, DEX_IDS * DEX_FORMS);
    each_line("pokedex", pokedex_line);
}

int bw_dex_shape(u32 species, u32 form)
{
    tables_build();
    if (g_shape == NULL || species >= DEX_IDS || form >= DEX_FORMS) {
        return -1;
    }
    return (int)g_shape[species * DEX_FORMS + form] - 1;
}

int bw_dex_letter(u32 species)
{
    tables_build();
    return species < DEX_IDS ? (int)g_letter[species] - 1 : -1;
}

/* The mod list or the limit changed (sdk/bw_limits.c): worked out again
 * when next asked. */
void bw_dex_data_forget(void)
{
    free(g_regional);
    g_regional = NULL;
    g_regional_rows = 0;
    memset(g_exists, 0, sizeof g_exists);
    g_tables_done = 0;
}
