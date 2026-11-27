/*
 * sdk/bw_limits.c -- the game's content limits, raised by mods (bw_limits.h).
 *
 * Two halves.
 *
 * The numbers.  Each limit aletheia names in src/game/port_limits.h has a
 * row in k_limits: its vanilla value (the literal the DS build keeps), the
 * manifest key a mod raises it with, and the ceiling the game's storage
 * allows (an item id is a u16 everywhere it is stored, and 0xFFFF is the
 * "none" value some lists end with).  bw_limits_apply reads every enabled
 * mod's manifest; bw_limit_get answers the highest value, never below the
 * vanilla one.
 *
 * The tables.  A table with one row per id lives in the game's own data,
 * sized for the vanilla count, and the game indexes it with the id.
 * Raising the limit alone would make the game read past its end.  The
 * port builds a copy with a row for every id up to the limit and points
 * the game's name at it: the game's code reads `item_gfx_member[item]`
 * through the name's reference (sdk/armdata.h, bw_dsref_<name>), so a
 * longer copy is all it takes.  Rows past the vanilla ones start as a copy
 * of a default row (row 0, the game's own "nothing" entry) and take the
 * values mods give in eidos/tables/<table>.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "armdata.h"
#include "bw_limits.h"
#include "port.h"
#include "vfs.h"

typedef struct LimitRow {
    const char *key;        /* the manifest key                           */
    u32         vanilla;    /* the value the DS build has                 */
    u32         ceiling;    /* the most the game's storage holds          */
} LimitRow;

static const LimitRow k_limits[BW_PORT_LIMIT_COUNT] = {
    /* ITEM_LAST: item ids are u16; 0xFFFF ends some lists. */
    { "limit.items", 626u, 0xFFFEu },
    /* MOVE_LAST: move ids are u16; 0xFFFF ends the learnsets.  The battle
     * view's effect scripts number the moves below 0x231 and its other
     * effects from 0x231 (ov093 btlv_effvm.c), so a move past 0x230 has
     * no animation until that routing is remapped. */
    { "limit.moves", 559u, 0xFFFEu },
    /* BTLV_EFFECT_MOVE_END: the first battle effect id that is not a move
     * (ov094 btlv_effvm_types.h).  No mod sets it: it is to follow the
     * move limit once every effect id the battle server sends is written
     * relative to it, so it stays at the DS value until then. */
    { NULL, 0x231u, 0x231u },
    /* FLDMMDL_OBJCODE_TABLE_COUNT: the field object code table (ov010
     * fldmmdl_types.h).  Not raisable until the code-to-index remap is in
     * the port. */
    { NULL, 799u, 799u },
    /* SPECIES_LAST: the last species id (arm9 species.h).  Species are u16
     * in the records; the dex's bits past the save's room live in the dex
     * extension (sdk/dex_save.c).  2047 is the wild and trainer records'
     * 11-bit field. */
    { "limit.species", 649u, 2047u },
    /* TYPE_LAST: the last type id (arm9 poke_type.h).  31 is the PC
     * build's "no type", and the item record's Natural Gift type is 5
     * bits with 31 for none, so 30 is the most.  A game tree older than
     * that (no BW_LIMIT_TYPE_LAST, CMakeLists.txt) keeps 17 as "no type"
     * and a chart of 17 columns: the limit stays at 16 there.  The screens
     * that show a type take an added one through sdk/type_icons.c. */
#ifdef BW_GAME_TYPE_LIMIT
    { "limit.types", 16u, 30u },
#else
    { "limit.types", 16u, 16u },
#endif
};

static u32 g_value[BW_PORT_LIMIT_COUNT];
static int g_have;

static void defaults(void)
{
    int i;

    for (i = 0; i < BW_PORT_LIMIT_COUNT; i++) {
        g_value[i] = k_limits[i].vanilla;
    }
    g_have = 1;
}

u32 bw_limit_get(int id)
{
    if (!g_have) {
        defaults();
    }
    if (id < 0 || id >= BW_PORT_LIMIT_COUNT) {
        return 0;
    }
    return g_value[id];
}

const char *bw_limit_key(int id)
{
    return id >= 0 && id < BW_PORT_LIMIT_COUNT ? k_limits[id].key : NULL;
}

/* ------------------------------------------------------------------ */
/* Reading the mods                                                    */
/* ------------------------------------------------------------------ */

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > (1L << 24) ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
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

/* The next line of `*p` as [*s, *e), comment and blanks trimmed; 0 at the
 * end of the text. */
static int next_line(const char **p, const char **s, const char **e)
{
    const char *a = *p, *b;

    if (*a == '\0') {
        return 0;
    }
    b = a;
    while (*b != '\0' && *b != '\n') {
        b++;
    }
    *p = *b == '\0' ? b : b + 1;
    {
        const char *h = a;

        while (h < b && *h != '#') {
            h++;
        }
        b = h;
    }
    while (a < b && (*a == ' ' || *a == '\t' || *a == '\r')) {
        a++;
    }
    while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r')) {
        b--;
    }
    *s = a;
    *e = b;
    return 1;
}

static int parse_u32(const char *s, const char *e, u32 *out)
{
    u32 v = 0;
    int base = 10;

    if (s == e) {
        return -1;
    }
    if (e - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (; s < e; s++) {
        int d;

        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = *s - 'a' + 10;
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = *s - 'A' + 10;
        } else {
            return -1;
        }
        if (v > (0xFFFFFFFFu - (u32)d) / (u32)base) {
            return -1;
        }
        v = v * (u32)base + (u32)d;
    }
    *out = v;
    return 0;
}

/* The limits one mod's manifest declares. */
static void read_manifest_limits(const char *root)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;

    if (strlen(root) + 16 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos_mod.txt", root);
    text = read_text(path);
    if (text == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *ks, *ke, *vs;
        int id;
        u32 v;

        while (eq < e && *eq != '=') {
            eq++;
        }
        if (eq == e) {
            continue;
        }
        ks = s;
        ke = eq;
        while (ke > ks && (ke[-1] == ' ' || ke[-1] == '\t')) {
            ke--;
        }
        vs = eq + 1;
        while (vs < e && (*vs == ' ' || *vs == '\t')) {
            vs++;
        }
        for (id = 0; id < BW_PORT_LIMIT_COUNT; id++) {
            size_t kl;

            if (k_limits[id].key == NULL) {
                continue;   /* the port sets this one, not a mod */
            }
            kl = strlen(k_limits[id].key);

            if ((size_t)(ke - ks) != kl || memcmp(ks, k_limits[id].key, kl) != 0) {
                continue;
            }
            if (parse_u32(vs, e, &v) != 0) {
                fprintf(stderr, "eidos: mod %s: bad value for %s, ignored\n",
                        root, k_limits[id].key);
            } else if (v > k_limits[id].ceiling) {
                fprintf(stderr, "eidos: mod %s: %s = %u is past the ceiling %u, "
                        "the ceiling is used\n", root, k_limits[id].key,
                        (unsigned)v, (unsigned)k_limits[id].ceiling);
                v = k_limits[id].ceiling;
            }
            if (v > g_value[id]) {
                g_value[id] = v;
            }
        }
    }
    free(text);
}

/* ------------------------------------------------------------------ */
/* The tables                                                          */
/* ------------------------------------------------------------------ */

typedef struct TableSpec {
    const char *name;       /* the game's name (sdk/armdata.h)            */
    const char *file;       /* eidos/tables/<file>.txt                    */
    int         limit;      /* the limit the last row id is               */
    u32         vanilla;    /* rows the game's table has                  */
    u32         row_halves; /* u16 values per row                         */
} TableSpec;

static const TableSpec k_tables[] = {
    /* Per item: the icon's tile member and palette member in a/0/2/5
     * (arm9/file_util.c, FILE_UTIL_Get and FILE_UTIL_LoadItemRecord). */
    { "item_gfx_member", "item_icons", BW_PORT_LIMIT_ITEM_LAST, 627u, 2u },
};

#define TABLE_COUNT (sizeof(k_tables) / sizeof(k_tables[0]))

static u16 *g_table[TABLE_COUNT];

/* Apply one mod's rows for table `t` to `rows` (count of them). */
static void read_table_rows(const char *root, const TableSpec *t, u16 *rows,
                            u32 count)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;
    u32 line = 0;

    if (strlen(root) + strlen(t->file) + 24 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos/tables/%s.txt", root, t->file);
    text = read_text(path);
    if (text == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *q;
        u32 id, k, values[8];

        line++;
        if (s == e) {
            continue;
        }
        while (eq < e && *eq != '=') {
            eq++;
        }
        q = eq;
        while (q > s && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        if (eq == e || parse_u32(s, q, &id) != 0) {
            fprintf(stderr, "eidos: %s:%u: expected `<id> = <values>`\n",
                    path, (unsigned)line);
            continue;
        }
        q = eq + 1;
        for (k = 0; k < t->row_halves; k++) {
            const char *vs, *ve;

            while (q < e && (*q == ' ' || *q == '\t')) {
                q++;
            }
            vs = q;
            while (q < e && *q != ' ' && *q != '\t') {
                q++;
            }
            ve = q;
            if (parse_u32(vs, ve, &values[k]) != 0 || values[k] > 0xFFFFu) {
                break;
            }
        }
        if (k != t->row_halves) {
            fprintf(stderr, "eidos: %s:%u: expected %u values of 0..65535\n",
                    path, (unsigned)line, (unsigned)t->row_halves);
            continue;
        }
        if (id >= count) {
            fprintf(stderr, "eidos: %s:%u: id %u is past the limit (raise %s)\n",
                    path, (unsigned)line, (unsigned)id,
                    k_limits[t->limit].key);
            continue;
        }
        for (k = 0; k < t->row_halves; k++) {
            rows[id * t->row_halves + k] = (u16)values[k];
        }
    }
    free(text);
}

static int build_table(u32 ti)
{
    const TableSpec *t = &k_tables[ti];
    const BwDataImage *img = bw_data_installed();
    const BwDataName *n;
    const u16 *vanilla;
    u32 count = bw_limit_get(t->limit) + 1u;
    u32 i, m;
    u16 *rows;
    int modded = 0;

    if (img == NULL || (n = bw_data_find(img, t->name)) == NULL ||
        n->ref == NULL || *n->ref == NULL) {
        return 0;               /* the game's table is not in this build */
    }
    vanilla = (const u16 *)*n->ref;
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200];
        const char *root = bw_mod_root_at(m);

        if (strlen(root) + strlen(t->file) + 24 < sizeof(path)) {
            FILE *f;

            sprintf(path, "%s/eidos/tables/%s.txt", root, t->file);
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                modded = 1;
            }
        }
    }
    if (count <= t->vanilla && !modded) {
        return 0;               /* the game's own table serves */
    }
    if (count < t->vanilla) {
        count = t->vanilla;
    }
    rows = (u16 *)malloc((size_t)count * t->row_halves * sizeof(u16));
    if (rows == NULL) {
        return -1;
    }
    memcpy(rows, vanilla, (size_t)t->vanilla * t->row_halves * sizeof(u16));
    for (i = t->vanilla; i < count; i++) {
        memcpy(rows + i * t->row_halves, vanilla, t->row_halves * sizeof(u16));
    }
    for (m = 0; m < bw_mod_count(); m++) {
        read_table_rows(bw_mod_root_at(m), t, rows, count);
    }
    free(g_table[ti]);
    g_table[ti] = rows;
    *n->ref = rows;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The Pokemon icons' palettes                                         */
/* ------------------------------------------------------------------ */

/*
 * _IconPaletteTable (arm9 poke_tool_ext.c, POKE_TOOL_CalcIconPalette): one
 * byte per icon index, the palette (0..2, a/0/0/7's three) in the low four
 * bits and the female icon's in the high four.  The icon index is the n of
 * the icon members (7 + 2n): the species, 650 and 651 the eggs, the
 * alternate forms from 652.  712 bytes on the DS (up to the next object,
 * gFontFormatInitFuncs at 0x0209E73C).
 *
 * eidos/tables/icon_palette.txt:  <icon index> = <palette> [<female palette>]
 * The female palette is the palette when not given.  Rows past the game's
 * are kept, for the icons a later species limit adds (they read as 0 until
 * a mod gives them).
 */
#define ICON_PAL_VANILLA 712u
#define ICON_PAL_MAX     4096u

static u8 *g_icon_pal;              /* the copy in the game's place      */
static const u8 *g_icon_pal_game;   /* the game's own table              */

static void read_icon_palette_rows(const char *root, u8 *rows, u32 *count)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;
    u32 line = 0;

    if (strlen(root) + 40 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos/tables/icon_palette.txt", root);
    if ((text = read_text(path)) == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *q, *vs;
        u32 id, v[2], k;

        line++;
        if (s == e) {
            continue;
        }
        while (eq < e && *eq != '=') {
            eq++;
        }
        q = eq;
        while (q > s && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        if (eq == e || parse_u32(s, q, &id) != 0 || id >= ICON_PAL_MAX) {
            fprintf(stderr, "eidos: %s:%u: expected `<icon index 0..%u> = <palette> [<female palette>]`\n", path,
                    (unsigned)line, (unsigned)(ICON_PAL_MAX - 1u));
            continue;
        }
        q = eq + 1;
        for (k = 0; k < 2; k++) {
            while (q < e && (*q == ' ' || *q == '\t')) {
                q++;
            }
            if (q == e) {
                break;
            }
            vs = q;
            while (q < e && *q != ' ' && *q != '\t') {
                q++;
            }
            if (parse_u32(vs, q, &v[k]) != 0 || v[k] > 15u) {
                k = 3;
                break;
            }
        }
        while (q < e && (*q == ' ' || *q == '\t')) {
            q++;
        }
        if (k == 0 || k == 3 || q != e) {
            fprintf(stderr, "eidos: %s:%u: expected one or two palettes of 0..15\n", path, (unsigned)line);
            continue;
        }
        if (k == 1) {
            v[1] = v[0];
        }
        rows[id] = (u8)(v[0] | (v[1] << 4));
        if (id + 1u > *count) {
            *count = id + 1u;
        }
    }
    free(text);
}

static int build_icon_palette(void)
{
    const BwDataImage *img = bw_data_installed();
    const BwDataName *n;
    u32 m, count = ICON_PAL_VANILLA;
    int modded = 0;
    u8 *rows;

    if (img == NULL || (n = bw_data_find(img, "_IconPaletteTable")) == NULL || n->ref == NULL || *n->ref == NULL) {
        return 0;
    }
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200];
        const char *root = bw_mod_root_at(m);
        FILE *f;

        if (strlen(root) + 40 < sizeof(path)) {
            sprintf(path, "%s/eidos/tables/icon_palette.txt", root);
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                modded = 1;
            }
        }
    }
    if (!modded) {
        if (g_icon_pal != NULL) {       /* (a mod with rows was turned off) */
            *n->ref = (void *)g_icon_pal_game;
            free(g_icon_pal);
            g_icon_pal = NULL;
        }
        return 0;               /* the game's own table serves */
    }
    if ((rows = (u8 *)calloc(ICON_PAL_MAX, 1)) == NULL) {
        return -1;
    }
    /* the game's own rows first: the reference still points at them, or at
     * an earlier apply's copy, whose first rows are the game's own then */
    if (g_icon_pal_game == NULL) {
        g_icon_pal_game = (const u8 *)*n->ref;
    }
    memcpy(rows, g_icon_pal_game, ICON_PAL_VANILLA);
    for (m = 0; m < bw_mod_count(); m++) {
        read_icon_palette_rows(bw_mod_root_at(m), rows, &count);
    }
    free(g_icon_pal);
    g_icon_pal = rows;
    *n->ref = rows;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The moves TMs and HMs teach                                         */
/* ------------------------------------------------------------------ */

/*
 * g_0209EAA0 (arm9 file_util.c, FILE_UTIL_GetMachineMove and
 * FILE_UTIL_IsHmMove): the move id each machine teaches, one u16 per
 * machine in the item order: TM01..TM92 rows 0..91, HM01..HM06 rows
 * 92..97, TM93..TM95 rows 98..100: 101 rows, a machine being an item in
 * FILE_UTIL_IsMachineItem's two id windows.
 *
 * Mods add TMs from TM96: a species record's machine bits are four u32
 * (bits 0..94 TM01..TM95, 95..100 HM01..HM06), so bits 101..127 are free,
 * and TM96..TM122 take bit and table row TM number + 5.  Each is an item a
 * mod adds (eidos/tables/machine_items.txt), which the port's
 * FILE_UTIL_IsMachineItem, FILE_UTIL_GetMachineMove and FILE_UTIL_GetId
 * know (sdk/machines.c).  The copy has 128 rows: the game's 101 are
 * followed by other data, so they alone are copied.
 *
 * eidos/tables/machine_moves.txt:  TM35 = 53    HM03 = 57    TM96 = 605
 * eidos/tables/machine_items.txt:  TM96 = 627
 */
#define MACHINES 101u
#define MACHINE_ROWS 128u
#define NEW_TM_FIRST 96u
#define NEW_TM_LAST 122u

static u16 g_machine_items[MACHINE_ROWS];      /* the item of each added TM's row, 0: none */

static u16 *g_machines;
static const u16 *g_machines_game;

/* The row of `TM35` or `HM03` (case does not count), or -1. */
static int machine_row(const char *s, const char *e)
{
    u32 n;

    if (e - s < 3 || (s[0] != 'T' && s[0] != 't' && s[0] != 'H' && s[0] != 'h') || (s[1] != 'M' && s[1] != 'm') ||
        parse_u32(s + 2, e, &n) != 0) {
        return -1;
    }
    if (s[0] == 'H' || s[0] == 'h') {
        return n >= 1 && n <= 6 ? (int)(91 + n) : -1;
    }
    if (n >= 1 && n <= 92) {
        return (int)n - 1;
    }
    if (n >= NEW_TM_FIRST && n <= NEW_TM_LAST) {
        return (int)(n + 5);
    }
    return n >= 93 && n <= 95 ? (int)(n + 5) : -1;
}

static void read_machine_rows(const char *root, u16 *rows)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;
    u32 line = 0;

    if (strlen(root) + 40 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos/tables/machine_moves.txt", root);
    if ((text = read_text(path)) == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *q, *vs;
        u32 move;
        int row;

        line++;
        if (s == e) {
            continue;
        }
        while (eq < e && *eq != '=') {
            eq++;
        }
        q = eq;
        while (q > s && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        row = eq == e ? -1 : machine_row(s, q);
        q = eq + 1;
        while (q < e && (*q == ' ' || *q == '\t')) {
            q++;
        }
        vs = q;
        while (q < e && *q != ' ' && *q != '\t') {
            q++;
        }
        if (row < 0 || parse_u32(vs, q, &move) != 0 || move == 0 || move > bw_limit_get(BW_PORT_LIMIT_MOVE_LAST)) {
            fprintf(stderr, "eidos: %s:%u: expected `TM01..TM122 or HM01..HM06 = <move id 1..%u>`\n", path,
                    (unsigned)line, (unsigned)bw_limit_get(BW_PORT_LIMIT_MOVE_LAST));
            continue;
        }
        rows[row] = (u16)move;
    }
    free(text);
}

/* One mod's machine_items.txt: the item of each added TM. */
static void read_machine_items(const char *root)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;
    u32 line = 0;

    if (strlen(root) + 40 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos/tables/machine_items.txt", root);
    if ((text = read_text(path)) == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *q, *vs;
        u32 item, r;
        int row;

        line++;
        if (s == e) {
            continue;
        }
        while (eq < e && *eq != '=') {
            eq++;
        }
        q = eq;
        while (q > s && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        row = eq == e ? -1 : machine_row(s, q);
        q = eq + 1;
        while (q < e && (*q == ' ' || *q == '\t')) {
            q++;
        }
        vs = q;
        while (q < e && *q != ' ' && *q != '\t') {
            q++;
        }
        if (row < (int)(NEW_TM_FIRST + 5) || parse_u32(vs, q, &item) != 0 || item <= 626u ||
            item > bw_limit_get(BW_PORT_LIMIT_ITEM_LAST)) {
            fprintf(stderr, "eidos: %s:%u: expected `TM96..TM122 = <item id 627..%u>`\n", path,
                    (unsigned)line, (unsigned)bw_limit_get(BW_PORT_LIMIT_ITEM_LAST));
            continue;
        }
        for (r = 0; r < MACHINE_ROWS; r++) {
            if (g_machine_items[r] == item && r != (u32)row) {
                g_machine_items[r] = 0;     /* (one TM an item: a later line wins) */
            }
        }
        g_machine_items[row] = (u16)item;
    }
    free(text);
}

int bw_machine_bit_of_item(u32 item)
{
    u32 r;

    if (item <= 626u) {
        return -1;
    }
    for (r = NEW_TM_FIRST + 5; r < MACHINE_ROWS; r++) {
        if (g_machine_items[r] == item) {
            return (int)r;
        }
    }
    return -1;
}

u32 bw_machine_item_of_bit(u32 bit)
{
    return bit < MACHINE_ROWS ? g_machine_items[bit] : 0;
}

static int build_machine_moves(void)
{
    const BwDataImage *img = bw_data_installed();
    const BwDataName *n;
    u32 m;
    int modded = 0;
    u16 *rows;

    if (img == NULL || (n = bw_data_find(img, "g_0209EAA0")) == NULL || n->ref == NULL || *n->ref == NULL) {
        return 0;
    }
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200];
        const char *root = bw_mod_root_at(m);
        FILE *f;

        if (strlen(root) + 40 < sizeof(path)) {
            sprintf(path, "%s/eidos/tables/machine_moves.txt", root);
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                modded = 1;
            }
            sprintf(path, "%s/eidos/tables/machine_items.txt", root);
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                modded = 1;
            }
        }
    }
    if (g_machines_game == NULL) {
        g_machines_game = (const u16 *)*n->ref;
    }
    if (!modded) {
        memset(g_machine_items, 0, sizeof g_machine_items);
        if (g_machines != NULL) {       /* (a mod with rows was turned off) */
            *n->ref = (void *)g_machines_game;
            free(g_machines);
            g_machines = NULL;
        }
        return 0;
    }
    if ((rows = (u16 *)calloc(MACHINE_ROWS, sizeof *rows)) == NULL) {
        return -1;
    }
    memcpy(rows, g_machines_game, MACHINES * sizeof *rows);
    memset(g_machine_items, 0, sizeof g_machine_items);
    for (m = 0; m < bw_mod_count(); m++) {
        read_machine_rows(bw_mod_root_at(m), rows);
        read_machine_items(bw_mod_root_at(m));
    }
    for (m = NEW_TM_FIRST + 5; m < MACHINE_ROWS; m++) {
        if (g_machine_items[m] != 0 && rows[m] == 0) {
            fprintf(stderr, "eidos: TM%u has an item (machine_items.txt) and no move (machine_moves.txt)\n",
                    (unsigned)(m - 5));
        }
    }
    free(g_machines);
    g_machines = rows;
    *n->ref = rows;
    return 0;
}

static const void *table_now(const char *name)
{
    const BwDataImage *img = bw_data_installed();
    const BwDataName *n;

    if (img == NULL || (n = bw_data_find(img, name)) == NULL || n->ref == NULL) {
        return NULL;
    }
    return *n->ref;
}

const u16 *bw_machine_moves(void)
{
    return (const u16 *)table_now("g_0209EAA0");
}

const u16 *bw_item_icon_table(void)
{
    return (const u16 *)table_now("item_gfx_member");
}

const u8 *bw_icon_palette_table(u32 *rows)
{
    const u8 *t = (const u8 *)table_now("_IconPaletteTable");

    if (rows != NULL) {
        *rows = t != NULL && t == g_icon_pal ? ICON_PAL_MAX : ICON_PAL_VANILLA;
    }
    return t;
}

extern void bw_dex_ext_forget(void) __attribute__((weak));     /* sdk/dex_save.c */
extern void bw_dex_data_forget(void) __attribute__((weak));    /* sdk/dex_data.c */
extern void bw_cries_forget(void) __attribute__((weak));       /* sdk/cries.c */
extern void bw_type_chart_forget(void) __attribute__((weak));  /* sdk/type_chart.c */
extern void bw_type_icons_apply(void) __attribute__((weak));   /* sdk/type_icons.c */
extern void bw_tm_pocket_forget(void) __attribute__((weak));   /* sdk/tm_pocket.c */

int bw_limits_apply(void)
{
    u32 i;
    int rc = 0;

    if (bw_dex_ext_forget != NULL) {
        bw_dex_ext_forget();        /* (the mod list may be another: which mod adds which species) */
    }
    if (bw_tm_pocket_forget != NULL) {
        bw_tm_pocket_forget();      /* (and which mod adds which TM) */
    }
    if (bw_dex_data_forget != NULL) {
        bw_dex_data_forget();       /* (and another regional table, other species) */
    }
    if (bw_cries_forget != NULL) {
        bw_cries_forget();          /* (and other cries) */
    }
    if (bw_type_chart_forget != NULL) {
        bw_type_chart_forget();     /* (and other types) */
    }

    defaults();
    for (i = 0; i < bw_mod_count(); i++) {
        read_manifest_limits(bw_mod_root_at(i));
    }
    for (i = 0; i < BW_PORT_LIMIT_COUNT; i++) {
        if (g_value[i] != k_limits[i].vanilla && k_limits[i].key != NULL) {
            fprintf(stderr, "eidos: %s raised to %u\n", k_limits[i].key,
                    (unsigned)g_value[i]);
        }
    }
    for (i = 0; i < TABLE_COUNT; i++) {
        if (build_table(i) != 0) {
            fprintf(stderr, "eidos: the extended table %s could not be built\n",
                    k_tables[i].name);
            rc = -1;
        }
    }
    if (build_icon_palette() != 0) {
        fprintf(stderr, "eidos: the icon palette table could not be built\n");
        rc = -1;
    }
    if (build_machine_moves() != 0) {
        fprintf(stderr, "eidos: the TM and HM move table could not be built\n");
        rc = -1;
    }
    if (bw_type_icons_apply != NULL) {
        bw_type_icons_apply();      /* the added types' icons and buttons */
    }
    return rc;
}
