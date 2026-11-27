/*
 * sdk/type_icons.c -- the pictures of the types mods add (limit.types).
 *
 * The game draws a type's icon from a/0/8/3: tiles member 0x22 + type, a
 * 32x16 4bpp picture, coloured by a row of the type palette (member 0x21,
 * rows 0..2) that _PokeTypePltOffset gives per type (arm9 app_icon.c).
 * Colour 0 is clear, 14 the outline, 15 the white name.  The battle's move
 * buttons take a palette of a/0/1/1 per type (ov094 d_0220A5F0), and the
 * Hall of Fame a particle and a palette member of a/2/1/6 per type (ov180).
 *
 * A mod describes each type it adds in eidos/tables/types.txt:
 *
 *     17 = icon 172 colors F6A5D5 9C4A7B label 40 button E6B4D5 DE94C5 D57BB4 C562A4 B44A94 9C3983 hof 34 3 color FF9CD6
 *
 *   icon    its a/0/8/3 member: colour 0 clear, 1 and 2 the colours below,
 *           14 the outline, 15 the white name
 *   colors  one or two colours (RRGGBB) for its colours 1 and 2
 *   label   its a/1/5/9 member, six Pokedex labels (Japanese, French,
 *           German, Italian, Spanish, Korean)
 *   button  the six shades of its battle move button (RRGGBB)
 *   hof     its Hall of Fame particle and palette members of a/2/1/6
 *   color   its colour on the one-screen interface
 *
 * A later mod's line for a type wins.  A type with no line, or no usable
 * icon, shows Normal's icon; with no button, Normal's button; with no hof,
 * Normal's.
 *
 * The palette.  The type palette's three rows have 8 free colours; the
 * screens that show type icons have one free OBJ row each, and the battle and
 * the summary have no other.  So an added type's colours are not given a
 * place for good: each screen gives the added types it shows a pair of its
 * free row's colours (1-2, 3-4, .. 11-12), the least lately shown pair first
 * when all six are taken, and writes the pair there.  The icon the screen
 * loads is the variant of the type's icon drawn with that pair (made once,
 * served from memory as a/0/8/3 member ICON_VARIANTS + 6 * (type - 17) +
 * pair).  A screen shows at most six type icons at once (the summary: two
 * types and four moves), so the pairs of the icons on screen stay theirs.
 * The screens are pushed by their patches (bw_type_screen_push).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "type_icons.h"
#include "vfs.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

#define GAME_TYPES     17u
#define MAX_TYPE       30u
#define ADDED          (MAX_TYPE + 1u - GAME_TYPES)
#define PAIRS          6u
#define ICON_FIRST     0x22u            /* a/0/8/3: icon of type 0       */
#define ICON_VARIANTS  0x400u           /* a/0/8/3: the added types' icon variants */
#define BUTTON_NORMAL  0xC2u            /* a/0/1/1: Normal's move button */
#define BUTTON_ADDED   0x400u           /* a/0/1/1: the added types' buttons */
#define LABEL_VARIANTS 0x400u           /* a/1/5/9: the added types' Pokedex label variants */
#define HOF_NORMAL_FX  0x22u
#define HOF_NORMAL_PAL 3u

extern int bw_mod_vfs_member_get(const char *archive, u32 index, void *out, u32 size);
extern int bw_mod_vfs_member_set_for(const char *root, const char *archive, u32 index,
                                     const void *data, u32 size);
extern int bw_mod_text_get(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
extern int bw_mod_text_set_for(const char *root, u32 archive, u32 file, u32 entry, u32 block,
                               const u16 *units, u32 count);

/* The Pokedex search's names (a/0/0/2 bank 277): an added type's at line
 * SEARCH_NAME_LINE + type - 17, copied from bank 199 (the game's type names)
 * at apply; ov210 asks for it (patch_ov210_zknsearch_main.c). */
#define SEARCH_NAME_BANK 277u
#define SEARCH_NAME_LINE 256u
#define TYPE_NAME_BANK   199u

/* The game's functions this calls (weak: tools that link the library
 * without the game). */
extern void GX_LoadOBJPltt(const void *src, u32 offset, u32 size) GAME_FN;
extern void GXS_LoadOBJPltt(const void *src, u32 offset, u32 size) GAME_FN;
extern void PALANM_CopyToSlot(void *fade, const void *src, u32 slot, u32 dst, u16 size) GAME_FN;

/* The game's tables, bound by name (sdk/armdata.h). */
extern void *bw_dsref__PokeTypePltOffset __attribute__((weak));
extern void *bw_dsref_ov094_d_0220A5F0 __attribute__((weak));
extern void *bw_dsref_ov180_gHallOfFameEffectArchives __attribute__((weak));
extern void *bw_dsref_ov180_gHallOfFameTrainerPaletteIds __attribute__((weak));

/* _PokeTypePltOffset's 17 rows (arm9 0x0209FCF8), for a build that binds
 * no name for it. */
static const u8 k_game_rows[GAME_TYPES] = { 0, 0, 1, 1, 0, 0, 2, 1, 0, 0, 1, 2, 0, 1, 1, 2, 0 };

typedef struct AddedType {
    int have;               /* a line names it                       */
    int root;               /* the mod whose line it is              */
    u32 icon;               /* a/0/8/3 member, or ~0                 */
    u32 ncolors;
    u16 colors[2];          /* BGR555                                */
    u32 first_rgb;          /* the first colour, 0xRRGGBB            */
    u32 label;              /* a/1/5/9 member, or ~0                 */
    int has_button;
    u16 button[6];
    int has_hof;
    u32 hof_fx, hof_pal;
    int has_color;
    u32 color;              /* 0xRRGGBB                              */
    char name[24];          /* its word in the tables (`type=fairy`) */
    u32 plate;              /* its plate's item (Arceus, Judgment), 0 none */
    u32 drive;              /* its drive's item (Genesect, Techno Blast), 0 none */
    u32 drive_form;         /* the Genesect form its drive gives */
    int variants;           /* the icon variants are in memory       */
    int labels;             /* the label variants are in memory      */
} AddedType;

static AddedType g_types[ADDED];

static AddedType *added(u32 type)
{
    if (type < GAME_TYPES || type > MAX_TYPE || type > bw_limit_get(BW_PORT_LIMIT_TYPE_LAST)) {
        return NULL;
    }
    return &g_types[type - GAME_TYPES];
}

/* ------------------------------------------------------------------ */
/* types.txt                                                           */
/* ------------------------------------------------------------------ */

static int rgb(const char *s, u32 *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 16);

    if (end - s != 6) {
        return -1;
    }
    *out = (u32)v;
    return 0;
}

static u16 bgr555(u32 rgb24)
{
    return (u16)((((rgb24 >> 3) & 31u) << 10) | (((rgb24 >> 11) & 31u) << 5) | ((rgb24 >> 19) & 31u));
}

/* `<type> = key values ...`; the words are split in place. */
static void type_line(const char *path, u32 line, char *text, int root)
{
    char *w[40];
    int n = 0, i;
    char *end;
    unsigned long type = strtoul(text, &end, 10);
    AddedType t;

    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (end == text || *end != '=') {
        fprintf(stderr, "eidos: %s:%u: expected `<type> = <key> <values> ...`\n", path, (unsigned)line);
        return;
    }
    if (type < GAME_TYPES || type > MAX_TYPE) {
        fprintf(stderr, "eidos: %s:%u: type %lu is not an added type (17 to 30)\n", path, (unsigned)line, type);
        return;
    }
    for (end++; n < 40;) {
        while (*end == ' ' || *end == '\t') {
            end++;
        }
        if (*end == '\0') {
            break;
        }
        w[n++] = end;
        while (*end != '\0' && *end != ' ' && *end != '\t') {
            end++;
        }
        if (*end != '\0') {
            *end++ = '\0';
        }
    }
    memset(&t, 0, sizeof t);
    t.have = 1;
    t.root = root;
    t.icon = t.label = 0xFFFFFFFFu;
    for (i = 0; i < n;) {
        const char *k = w[i++];
        u32 c;
        int j;

        if (strcmp(k, "icon") == 0 && i < n) {
            t.icon = (u32)strtoul(w[i++], NULL, 10);
        } else if (strcmp(k, "label") == 0 && i < n) {
            t.label = (u32)strtoul(w[i++], NULL, 10);
        } else if (strcmp(k, "colors") == 0) {
            while (i < n && rgb(w[i], &c) == 0) {
                if (t.ncolors == 2) {
                    fprintf(stderr, "eidos: %s:%u: a type has at most 2 colours; the rest are left out\n",
                            path, (unsigned)line);
                    i++;
                    continue;
                }
                if (t.ncolors == 0) {
                    t.first_rgb = c;
                }
                t.colors[t.ncolors++] = bgr555(c);
                i++;
            }
        } else if (strcmp(k, "button") == 0) {
            for (j = 0; j < 6 && i < n && rgb(w[i], &c) == 0; j++, i++) {
                t.button[j] = bgr555(c);
            }
            t.has_button = j == 6;
            if (j != 6) {
                fprintf(stderr, "eidos: %s:%u: `button` takes six colours; Normal's button is used\n",
                        path, (unsigned)line);
            }
        } else if (strcmp(k, "hof") == 0 && i + 1 < n) {
            t.hof_fx = (u32)strtoul(w[i++], NULL, 10);
            t.hof_pal = (u32)strtoul(w[i++], NULL, 10);
            t.has_hof = 1;
        } else if (strcmp(k, "color") == 0 && i < n && rgb(w[i], &c) == 0) {
            t.color = c;
            t.has_color = 1;
            i++;
        } else if (strcmp(k, "plate") == 0 && i < n) {
            t.plate = (u32)strtoul(w[i++], NULL, 10);
        } else if (strcmp(k, "drive") == 0 && i + 1 < n) {
            t.drive = (u32)strtoul(w[i++], NULL, 10);
            t.drive_form = (u32)strtoul(w[i++], NULL, 10);
            if (t.drive_form < 5u) {
                fprintf(stderr, "eidos: %s:%u: a drive's Genesect form is 5 or more (1 to 4 are the game's "
                        "drives); the drive is left out\n", path, (unsigned)line);
                t.drive = 0;
            }
        } else if (strcmp(k, "name") == 0 && i < n) {
            const char *s = w[i++];
            size_t l = strlen(s), q;
            int ok = l > 0 && l < sizeof t.name;

            for (q = 0; ok && q < l; q++) {
                ok = (s[q] >= 'a' && s[q] <= 'z') || (s[q] >= '0' && s[q] <= '9' && q > 0) || s[q] == '_';
            }
            if (ok) {
                memcpy(t.name, s, l + 1);
            } else {
                fprintf(stderr, "eidos: %s:%u: `name %s` is not a lowercase word (a to z, 0 to 9, _); left out\n",
                        path, (unsigned)line, s);
            }
        } else {
            fprintf(stderr, "eidos: %s:%u: `%s` is not a key of types.txt (icon, colors, label, button, hof, color, "
                    "name, plate, drive)\n", path, (unsigned)line, k);
            return;
        }
    }
    g_types[type - GAME_TYPES] = t;     /* (a later mod's line wins) */
}

static void read_types(void)
{
    u32 m;

    memset(g_types, 0, sizeof g_types);
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[1024];
        FILE *f;
        u32 n = 0;

        if (root == NULL || strlen(root) + 32 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/types.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            char *s = line, *e, *h;

            n++;
            if ((h = strchr(s, '#')) != NULL) {
                *h = '\0';
            }
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
                *--e = '\0';
            }
            if (*s != '\0') {
                type_line(path, n, s, (int)m);
            }
        }
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* The members made from them                                          */
/* ------------------------------------------------------------------ */

/* A chunk of an NNS file (RGCN's RAHC, RLCN's TTLP): its data and size. */
static u8 *chunk(u8 *file, u32 size, const char *magic, u32 *data_size)
{
    u32 p = 0x10;

    while (p + 8 <= size) {
        u32 len = (u32)file[p + 4] | (u32)file[p + 5] << 8 | (u32)file[p + 6] << 16 | (u32)file[p + 7] << 24;

        if (len < 8 || p + len > size) {
            return NULL;
        }
        if (memcmp(file + p, magic, 4) == 0) {
            /* CHAR: the tile data's size at +0x18, the data at +0x20;
             * PLTT: the colours' size at +0x10, the data at +0x18 */
            u32 at = memcmp(magic, "RAHC", 4) == 0 ? 0x18u : 0x10u;
            u32 hdr = at + 8u;
            u32 n = (u32)file[p + at] | (u32)file[p + at + 1] << 8 | (u32)file[p + at + 2] << 16 |
                    (u32)file[p + at + 3] << 24;

            *data_size = n <= len - hdr ? n : len - hdr;
            return file + p + hdr;
        }
        p += len;
    }
    return NULL;
}

/* Six variants of an added type's picture (`member` of `archive`): colours 1
 * and 2 drawn with pair k's colours (1 + 2k, 2 + 2k), as members base +
 * 6 * (type - 17) + k.  1 when they are in memory. */
static int make_six(u32 type, AddedType *t, const char *archive, u32 member, u32 base, const char *what)
{
    static u8 pic[8192], var[8192];
    int size = bw_mod_vfs_member_get(archive, member, pic, sizeof pic);
    u8 *px;
    u32 n, i, k;

    if (member == 0xFFFFFFFFu || size <= 0 || (u32)size > sizeof pic ||
        (px = chunk(pic, (u32)size, "RAHC", &n)) == NULL || n == 0) {
        fprintf(stderr, "eidos: type %u: no %s in %s member %d\n", (unsigned)type, what, archive, (int)member);
        return 0;
    }
    for (i = 0; i < n * 2; i++) {
        u32 c = (px[i / 2] >> ((i & 1) * 4)) & 15u;

        if (!(c == 0 || c == 14 || c == 15 || (c >= 1 && c <= t->ncolors))) {
            fprintf(stderr, "eidos: type %u: its %s uses colour %u (it has %u colours, 0, 14 and 15)\n",
                    (unsigned)type, what, (unsigned)c, (unsigned)t->ncolors);
            return 0;
        }
    }
    for (k = 0; k < PAIRS; k++) {
        u8 *vp;

        memcpy(var, pic, (size_t)size);
        vp = var + (px - pic);
        for (i = 0; i < n; i++) {
            u32 lo = vp[i] & 15u, hi = vp[i] >> 4;

            lo = lo >= 1 && lo <= 2 ? lo + 2 * k : lo;
            hi = hi >= 1 && hi <= 2 ? hi + 2 * k : hi;
            vp[i] = (u8)(lo | hi << 4);
        }
        if (bw_mod_vfs_member_set_for(bw_mod_root_at((u32)t->root), archive,
                                      base + PAIRS * (type - GAME_TYPES) + k, var, (u32)size) != 0) {
            return 0;
        }
    }
    return 1;
}

/* An added type's move button: Normal's frame (row 0, colours 0..7) and the
 * type's six shades (8..13). */
static void make_button(u32 type, AddedType *t)
{
    static u8 pal[2048];
    int size = bw_mod_vfs_member_get("a/0/1/1", BUTTON_NORMAL, pal, sizeof pal);
    u8 *c;
    u32 n, i;

    if (size <= 0 || (u32)size > sizeof pal || (c = chunk(pal, (u32)size, "TTLP", &n)) == NULL || n < 32) {
        t->has_button = 0;
        return;
    }
    for (i = 0; i < 6; i++) {
        c[(8 + i) * 2] = (u8)(t->button[i] & 0xFF);
        c[(8 + i) * 2 + 1] = (u8)(t->button[i] >> 8);
    }
    if (bw_mod_vfs_member_set_for(bw_mod_root_at((u32)t->root), "a/0/1/1", BUTTON_ADDED + (type - GAME_TYPES),
                                  pal, (u32)size) != 0) {
        t->has_button = 0;
    }
}

void bw_type_icons_apply(void)
{
    u32 type;

    read_types();
    for (type = GAME_TYPES; type <= MAX_TYPE; type++) {
        AddedType *t = added(type);

        if (t == NULL || !t->have) {
            continue;
        }
        if (t->ncolors == 0) {
            fprintf(stderr, "eidos: type %u: no `colors`; Normal's icon is shown\n", (unsigned)type);
        } else {
            t->variants = make_six(type, t, "a/0/8/3", t->icon, ICON_VARIANTS, "icon");
            if (!t->variants) {
                fprintf(stderr, "eidos: type %u: Normal's icon is shown\n", (unsigned)type);
            }
            t->labels = t->label != 0xFFFFFFFFu &&
                        make_six(type, t, "a/1/5/9", t->label, LABEL_VARIANTS, "Pokedex labels");
        }
        if (t->has_button) {
            make_button(type, t);
        }
        {
            u16 name[64];
            int n = bw_mod_text_get(2, TYPE_NAME_BANK, type, 0, name, 64);

            if (n > 0) {
                (void)bw_mod_text_set_for(bw_mod_root_at((u32)t->root), 2, SEARCH_NAME_BANK,
                                          SEARCH_NAME_LINE + (type - GAME_TYPES), 0, name, (u32)n);
            }
        }
    }
}

/* The added type a types.txt `name` names (`fairy`), -1 for none.  Read
 * at the first ask after the mod list is applied. */
int bw_type_by_name(const char *s, size_t n)
{
    u32 type;

    for (type = GAME_TYPES; type <= MAX_TYPE; type++) {
        AddedType *t = added(type);

        if (t != NULL && t->have && t->name[0] != '\0' && strlen(t->name) == n && strncmp(t->name, s, n) == 0) {
            return (int)type;
        }
    }
    return -1;
}

/* The added type whose `plate` is `item` (17..30), or 0. */
u32 bw_type_of_plate(u32 item)
{
    u32 type;

    for (type = GAME_TYPES; item != 0u && type <= MAX_TYPE; type++) {
        AddedType *t = added(type);

        if (t != NULL && t->have && t->plate == item) {
            return type;
        }
    }
    return 0;
}

/* The added type whose `drive` is `item` and the Genesect form it gives:
 * 1, or 0 for an item that is no added drive. */
int bw_type_of_drive(u32 item, u32 *type, u32 *form)
{
    u32 k;

    for (k = GAME_TYPES; item != 0u && k <= MAX_TYPE; k++) {
        AddedType *t = added(k);

        if (t != NULL && t->have && t->drive == item) {
            *type = k;
            *form = t->drive_form;
            return 1;
        }
    }
    return 0;
}

int bw_type_ui_color(u32 type, u32 *rgb)
{
    AddedType *t = added(type);

    if (t == NULL || !t->have || (!t->has_color && t->ncolors == 0)) {
        return 0;
    }
    *rgb = t->has_color ? t->color : t->first_rgb;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The game's per-type tables                                          */
/* ------------------------------------------------------------------ */

static int g_buttons[MAX_TYPE + 1];
static u32 g_hof_fx[MAX_TYPE + 1], g_hof_pal[MAX_TYPE + 1];

void bw_type_icons_overlay_loaded(u32 id)
{
    u32 last = bw_limit_get(BW_PORT_LIMIT_TYPE_LAST), type;

    if (last < GAME_TYPES) {
        return;     /* no added type: the game's tables, where they are */
    }
    if (id == 94u && &bw_dsref_ov094_d_0220A5F0 != NULL && bw_dsref_ov094_d_0220A5F0 != NULL) {
        memcpy(g_buttons, bw_dsref_ov094_d_0220A5F0, GAME_TYPES * sizeof(int));
        for (type = GAME_TYPES; type <= last && type <= MAX_TYPE; type++) {
            AddedType *t = added(type);

            g_buttons[type] = t != NULL && t->have && t->has_button ? (int)(BUTTON_ADDED + (type - GAME_TYPES))
                                                                    : (int)BUTTON_NORMAL;
        }
        bw_dsref_ov094_d_0220A5F0 = g_buttons;
    }
    if (id == 180u && &bw_dsref_ov180_gHallOfFameEffectArchives != NULL &&
        bw_dsref_ov180_gHallOfFameEffectArchives != NULL && &bw_dsref_ov180_gHallOfFameTrainerPaletteIds != NULL &&
        bw_dsref_ov180_gHallOfFameTrainerPaletteIds != NULL) {
        memcpy(g_hof_fx, bw_dsref_ov180_gHallOfFameEffectArchives, GAME_TYPES * sizeof(u32));
        memcpy(g_hof_pal, bw_dsref_ov180_gHallOfFameTrainerPaletteIds, GAME_TYPES * sizeof(u32));
        for (type = GAME_TYPES; type <= last && type <= MAX_TYPE; type++) {
            AddedType *t = added(type);
            int own = t != NULL && t->have && t->has_hof;

            g_hof_fx[type] = own ? t->hof_fx : g_hof_fx[0];
            g_hof_pal[type] = own ? t->hof_pal : g_hof_pal[0];
        }
        bw_dsref_ov180_gHallOfFameEffectArchives = g_hof_fx;
        bw_dsref_ov180_gHallOfFameTrainerPaletteIds = g_hof_pal;
    }
}

/* ------------------------------------------------------------------ */
/* The screens                                                         */
/* ------------------------------------------------------------------ */

typedef struct Screen {
    const void *owner;
    int engine, base, row;
    void *palanm;
    int bank;
    u16 type_rows;
    int pair_type[PAIRS];   /* the type each pair is drawn for, 0: none */
    u32 pair_used[PAIRS];   /* when it was last shown                   */
    u32 tick;
} Screen;

#define SCREENS_MAX 4

static Screen g_screens[SCREENS_MAX];
static int g_nscreens;

static void write_colors(Screen *s, u32 first, const u16 *colors, u32 n)
{
    u32 offset = (u32)s->row * 32u + first * 2u;

    if (s->engine == 0 && GX_LoadOBJPltt != NULL) {
        GX_LoadOBJPltt(colors, offset, n * 2u);
    } else if (s->engine != 0 && GXS_LoadOBJPltt != NULL) {
        GXS_LoadOBJPltt(colors, offset, n * 2u);
    }
    if (s->palanm != NULL && PALANM_CopyToSlot != NULL) {
        PALANM_CopyToSlot(s->palanm, colors, (u32)s->bank, (u32)s->row * 16u + first, (u16)(n * 2u));
    }
}

void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank, u16 type_rows)
{
    static const u16 k_ends[2] = { 0x294A, 0x7FFF };    /* the outline, the white name */
    Screen *s = NULL;
    int i;

    if (bw_limit_get(BW_PORT_LIMIT_TYPE_LAST) < GAME_TYPES) {
        return;     /* no added type: the screen's rows are the game's alone */
    }
    for (i = 0; i < g_nscreens; i++) {
        if (g_screens[i].owner == owner) {
            Screen keep = g_screens[i];

            memmove(&g_screens[i], &g_screens[i + 1], (size_t)(g_nscreens - i - 1) * sizeof(Screen));
            g_screens[g_nscreens - 1] = keep;
            s = &g_screens[g_nscreens - 1];
            break;
        }
    }
    if (s == NULL) {
        if (g_nscreens == SCREENS_MAX) {
            memmove(&g_screens[0], &g_screens[1], (SCREENS_MAX - 1) * sizeof(Screen));
            g_nscreens--;
        }
        s = &g_screens[g_nscreens++];
    }
    memset(s, 0, sizeof *s);
    s->owner = owner;
    s->engine = engine;
    s->base = base;
    s->row = row;
    s->palanm = palanm;
    s->bank = bank;
    s->type_rows = type_rows;
    write_colors(s, 14, k_ends, 2);
}

void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                           u16 type_rows)
{
    int i;

    for (i = 0; i < g_nscreens; i++) {
        if (g_screens[i].owner == owner) {
            return;
        }
    }
    bw_type_screen_push(owner, engine, base, row, palanm, bank, type_rows);
}

int bw_types_added(void)
{
    return bw_limit_get(BW_PORT_LIMIT_TYPE_LAST) >= GAME_TYPES;
}

void bw_type_screen_select(const void *owner)
{
    int i;

    for (i = 0; i < g_nscreens - 1; i++) {
        if (g_screens[i].owner == owner) {
            Screen keep = g_screens[i];

            memmove(&g_screens[i], &g_screens[i + 1], (size_t)(g_nscreens - i - 1) * sizeof(Screen));
            g_screens[g_nscreens - 1] = keep;
            return;
        }
    }
}

void bw_type_screen_pop(const void *owner)
{
    int i;

    for (i = 0; i < g_nscreens; i++) {
        if (g_screens[i].owner == owner) {
            memmove(&g_screens[i], &g_screens[i + 1], (size_t)(g_nscreens - i - 1) * sizeof(Screen));
            g_nscreens--;
            return;
        }
    }
}

u16 bw_type_screen_fade_extra(const void *palanm, int bank, u16 rows)
{
    int i;

    for (i = g_nscreens - 1; i >= 0; i--) {
        const Screen *s = &g_screens[i];

        if (s->palanm == palanm && s->bank == bank && s->type_rows != 0 && (rows & s->type_rows) == s->type_rows) {
            return (u16)(1u << s->row);
        }
    }
    return 0;
}

/* The pair the screen on top draws an added type with: -1 when there is no
 * such screen or the type has no icon. */
static int pair_for(u32 type)
{
    AddedType *t = added(type);
    Screen *s;
    int k, pick = -1;

    if (t == NULL || !t->have || !t->variants) {
        return -1;
    }
    if (g_nscreens == 0) {
        static int told;

        if (!told) {
            fprintf(stderr, "eidos: a screen with no place for added types' colours shows type %u: "
                    "Normal's icon\n", (unsigned)type);
            told = 1;
        }
        return -1;
    }
    s = &g_screens[g_nscreens - 1];
    s->tick++;
    for (k = 0; k < (int)PAIRS; k++) {
        if (s->pair_type[k] == (int)type) {
            s->pair_used[k] = s->tick;
            return k;
        }
    }
    for (k = 0; k < (int)PAIRS; k++) {
        if (s->pair_type[k] == 0) {
            pick = k;
            break;
        }
        if (pick < 0 || s->pair_used[k] < s->pair_used[pick]) {
            pick = k;
        }
    }
    s->pair_type[pick] = (int)type;
    s->pair_used[pick] = s->tick;
    write_colors(s, 1u + 2u * (u32)pick, t->colors, t->ncolors);
    return pick;
}

/* ------------------------------------------------------------------ */
/* The game's two icon functions (arm9 app_icon.c)                     */
/* ------------------------------------------------------------------ */

u32 APP_ICON_GetTypeCellFileId(u32 type)
{
    int k;

    if (type < GAME_TYPES) {
        return type + ICON_FIRST;
    }
    k = pair_for(type);
    return k < 0 ? ICON_FIRST : ICON_VARIANTS + PAIRS * (type - GAME_TYPES) + (u32)k;
}

/* ------------------------------------------------------------------ */
/* Characters of added types, for screens that swap character proxies  */
/* ------------------------------------------------------------------ */

extern void *ARC_TOOL_CreateHandle(u32 archive, unsigned int heap) GAME_FN;
extern void ARC_TOOL_DeleteHandle(void *handle) GAME_FN;
extern int CLACT_RegisterCharacter(void *archive, u32 file, u32 compressed, u32 vram, u16 heap) GAME_FN;
extern void CLACT_ReleaseCharacter(u32 idx) GAME_FN;

typedef struct TypeChar {
    const void *owner;
    u32 type;
    u32 member;             /* the icon variant it holds */
    int chr;
} TypeChar;

#define TYPE_CHARS_MAX 16

static TypeChar g_chars[TYPE_CHARS_MAX];

int bw_type_icon_char(const void *owner, u32 type, u16 heap, u32 vram)
{
    u32 member = APP_ICON_GetTypeCellFileId(type);      /* (gives the type its pair) */
    TypeChar *c = NULL;
    void *arc;
    int i;

    if (member == ICON_FIRST || ARC_TOOL_CreateHandle == NULL || CLACT_RegisterCharacter == NULL) {
        return -1;
    }
    for (i = 0; i < TYPE_CHARS_MAX && c == NULL; i++) {
        if (g_chars[i].owner == owner && g_chars[i].type == type) {
            if (g_chars[i].member == member) {
                return g_chars[i].chr;
            }
            c = &g_chars[i];
            CLACT_ReleaseCharacter((u32)c->chr);        /* (its pair moved: a hidden icon) */
        }
    }
    for (i = 0; i < TYPE_CHARS_MAX && c == NULL; i++) {
        if (g_chars[i].owner == NULL) {
            c = &g_chars[i];
        }
    }
    if (c == NULL) {
        return -1;
    }
    arc = ARC_TOOL_CreateHandle(0x53, heap);    /* a/0/8/3 (APP_ICON_GetArchiveId) */
    c->chr = CLACT_RegisterCharacter(arc, member, 0, vram, heap);
    ARC_TOOL_DeleteHandle(arc);
    c->owner = c->chr >= 0 ? owner : NULL;
    c->type = type;
    c->member = member;
    return c->chr;
}

void bw_type_icon_chars_release(const void *owner)
{
    int i;

    for (i = 0; i < TYPE_CHARS_MAX; i++) {
        if (g_chars[i].owner == owner && owner != NULL) {
            CLACT_ReleaseCharacter((u32)g_chars[i].chr);
            g_chars[i].owner = NULL;
        }
    }
}

u32 bw_type_label_member(u32 type)
{
    AddedType *t = added(type);
    Screen *s;
    int k;

    if (t == NULL || !t->labels || g_nscreens == 0) {
        return 0xFFFFFFFFu;
    }
    s = &g_screens[g_nscreens - 1];
    for (k = 0; k < (int)PAIRS; k++) {
        if (s->pair_type[k] == (int)type) {
            return LABEL_VARIANTS + PAIRS * (type - GAME_TYPES) + (u32)k;
        }
    }
    return 0xFFFFFFFFu;
}

u8 APP_ICON_GetTypePaletteOffset(int type)
{
    if (type >= 0 && type < (int)GAME_TYPES) {
        if (&bw_dsref__PokeTypePltOffset != NULL && bw_dsref__PokeTypePltOffset != NULL) {
            return ((const u8 *)bw_dsref__PokeTypePltOffset)[type];
        }
        return k_game_rows[type];
    }
    if (pair_for((u32)type) < 0) {
        return k_game_rows[0];
    }
    return (u8)((g_screens[g_nscreens - 1].row - g_screens[g_nscreens - 1].base) & 15);
}
