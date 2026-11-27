/*
 * sdk/text_from.c -- a mod's move and item lines in the game's language
 * (docs/game-tables.md, Text lines).
 *
 * Some of the game's lines about a move or an item are whole sentences with
 * the name inside: "{poke} used\nPound!" (bank 13, three a move: the
 * player's, a wild and a foe's Pokemon), the name in capitals (bank 286),
 * "a Potion" (bank 279, English only: the other languages leave it empty)
 * and "Potions" (bank 280).  A mod written in one language (its manifest's
 * `lang`, English when it has none) ships these in that language.  When the
 * game is in another one and the mod has no `<bank>.<lang>.msg` line for an
 * entry it adds, the line is made here from the game's own line of a source
 * entry, with the source's name swapped for the mod's name in the game's
 * language (banks 203 and 54, the mod's `.<lang>.msg` first):
 *
 *   - a move's source: eidos/tables/text_from.txt `move.<id> = <move>`, else
 *     the move it works like (move_handlers.txt), else Pound;
 *   - an item's: `item.<id> = <item>`, else TM95 for a TM, the Flame Plate
 *     for a plate, the Douse Drive for a drive (types.txt), else none.
 *
 * Only the mod's own entries past the game's (moves from 560, items from
 * 627) are made, and only lines no later mod changed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "bw_limits.h"
#include "type_icons.h"
#include "types.h"
#include "vfs.h"
#include "vfs_msg.h"

extern int bw_mod_text_get(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
extern int bw_lang_language(void) __attribute__((weak));

#define TEXT_ARCHIVE "a/0/0/2"

#define GAME_MOVES  560u            /* moves 0..559 are the game's */
#define GAME_ITEMS  627u            /* items 0..626 */
#define BANK_USED   13u             /* "{poke} used\n<move>!", three a move */
#define BANK_MNAME  203u            /* move names */
#define BANK_MCAPS  286u            /* move names in capitals */
#define BANK_INAME  54u             /* item names */
#define BANK_ARTICLE 279u           /* "a <item>" (English) */
#define BANK_PLURAL 280u            /* "<items>" */
#define SRC_POUND   1u
#define SRC_TM95    620u
#define SRC_PLATE   298u            /* Flame Plate */
#define SRC_DRIVE   116u            /* Douse Drive */
#define MAX_UNITS   400u

typedef struct From {
    u32 id, src;
} From;

static From *g_moves, *g_items;
static u32 g_nmoves, g_nitems, g_cmoves, g_citems;

static const char *game_code(void)
{
    switch (bw_lang_language != NULL ? bw_lang_language() : 2) {
    case 3: return "fr";
    case 4: return "it";
    case 5: return "de";
    case 7: return "es";
    }
    return "en";
}

static void put_from(From **list, u32 *n, u32 *cap, u32 id, u32 src)
{
    u32 i;

    for (i = 0; i < *n; i++) {
        if ((*list)[i].id == id) {
            (*list)[i].src = src;       /* a later mod's line wins */
            return;
        }
    }
    if (*n == *cap) {
        u32 c = *cap != 0 ? *cap * 2 : 16;
        From *g = (From *)realloc(*list, c * sizeof *g);

        if (g == NULL) {
            return;
        }
        *list = g;
        *cap = c;
    }
    (*list)[*n].id = id;
    (*list)[*n].src = src;
    (*n)++;
}

static u32 find_from(const From *list, u32 n, u32 id)
{
    u32 i;

    for (i = 0; i < n; i++) {
        if (list[i].id == id) {
            return list[i].src;
        }
    }
    return 0;
}

/* eidos/tables/text_from.txt of every mod: `move.<id> = <move>`,
 * `item.<id> = <item>`. */
static void read_text_from(void)
{
    u32 m;

    g_nmoves = g_nitems = 0;
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[160];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        snprintf(path, sizeof path, "%s/eidos/tables/text_from.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            unsigned id, src;
            char kind[8];

            n++;
            if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == '\0') {
                continue;
            }
            if (sscanf(line, " %7[a-z].%u = %u", kind, &id, &src) == 3 && src != 0) {
                if (strcmp(kind, "move") == 0) {
                    put_from(&g_moves, &g_nmoves, &g_cmoves, id, src);
                    continue;
                }
                if (strcmp(kind, "item") == 0) {
                    put_from(&g_items, &g_nitems, &g_citems, id, src);
                    continue;
                }
            }
            fprintf(stderr, "eidos: %s:%d: expected `move.<id> = <move>` or `item.<id> = <item>`; left out\n",
                    path, n);
        }
        fclose(f);
    }
}

/* The language the mod's plain text is written in: its manifest's `lang`,
 * else English. */
static void mod_lang(const char *root, char out[3])
{
    char path[1200], line[256];
    FILE *f;

    strcpy(out, "en");
    if (strlen(root) + 20 > sizeof path) {
        return;
    }
    snprintf(path, sizeof path, "%s/eidos_mod.txt", root);
    if ((f = fopen(path, "rb")) == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char v[8];

        if (sscanf(line, " lang = %7s", v) == 1 && strlen(v) == 2) {
            out[0] = v[0];
            out[1] = v[1];
            out[2] = '\0';
        }
    }
    fclose(f);
}

/* A mod's line file (`<bank>.msg` or `<bank>.<lang>.msg`), 0 when read. */
static int read_lines(const char *root, u32 bank, const char *lang, BwMsgLines *out)
{
    char path[1200];
    FILE *f;
    long size;
    char *text;
    int rc;

    memset(out, 0, sizeof *out);
    if (lang != NULL) {
        snprintf(path, sizeof path, "%s/a/0/0/2/%u.%s.msg", root, (unsigned)bank, lang);
    } else {
        snprintf(path, sizeof path, "%s/a/0/0/2/%u.msg", root, (unsigned)bank);
    }
    if ((f = fopen(path, "rb")) == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || (text = (char *)malloc((size_t)size + 1)) == NULL) {
        fclose(f);
        return -1;
    }
    size = (long)fread(text, 1, (size_t)size, f);
    fclose(f);
    rc = bw_msg_parse(text, (size_t)size, out, NULL, 0);
    free(text);
    return rc;
}

static const BwMsgLine *line_at(const BwMsgLines *l, u32 index)
{
    u32 i;
    const BwMsgLine *found = NULL;

    for (i = 0; i < l->count; i++) {
        if (l->lines[i].index == index) {
            found = &l->lines[i];       /* (a later line in the file wins) */
        }
    }
    return found;
}

/* The line of a bank as the game reads it now: its units, or -1. */
static int text_now(u32 bank, u32 entry, u16 *out)
{
    return bw_mod_text_get(2, bank, entry, 0, out, MAX_UNITS);
}

/* `tmpl` with the first `from` in it swapped for `to`: the count, or -1
 * when `from` is not in it. */
static int swap_name(const u16 *tmpl, int nt, const u16 *from, int nf, const u16 *to, int nto, u16 *out)
{
    int i;

    if (nt == 0) {
        return 0;                       /* (an empty line stays empty: the article in a non-English game) */
    }
    for (i = 0; nf > 0 && i + nf <= nt; i++) {
        if (memcmp(tmpl + i, from, (size_t)nf * sizeof(u16)) == 0) {
            if (nt - nf + nto > (int)MAX_UNITS) {
                return -1;
            }
            memcpy(out, tmpl, (size_t)i * sizeof(u16));
            memcpy(out + i, to, (size_t)nto * sizeof(u16));
            memcpy(out + i + nto, tmpl + i + nf, (size_t)(nt - i - nf) * sizeof(u16));
            return nt - nf + nto;
        }
    }
    return -1;
}

static u16 upper(u16 c)
{
    if (c >= 'a' && c <= 'z') {
        return (u16)(c - 0x20);
    }
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
        return (u16)(c - 0x20);         /* (é É, ü Ü, ñ Ñ: Latin-1's capitals) */
    }
    return c;
}

static u32 move_source(u32 move)
{
    u32 src = find_from(g_moves, g_nmoves, move);
    int base;

    if (src != 0) {
        return src;
    }
    base = bw_bh_base(BW_BH_MOVE, (int)move);
    return base >= 1 && (u32)base < GAME_MOVES ? (u32)base : SRC_POUND;
}

static u32 item_source(u32 item)
{
    u32 src = find_from(g_items, g_nitems, item), t, f;

    if (src != 0) {
        return src;
    }
    if (bw_machine_bit_of_item(item) >= 0) {
        return SRC_TM95;
    }
    if (bw_type_of_plate(item) != 0) {
        return SRC_PLATE;
    }
    if (bw_type_of_drive(item, &t, &f)) {
        return SRC_DRIVE;
    }
    return 0;
}

/* One bank of one mod: every line of its plain file for an entry it adds,
 * with no line in the game's language and unchanged since, made again. */
static void make_bank(const char *root, u32 bank, const char *code)
{
    BwMsgLines plain, local;
    BwMsgLine *made;
    u32 n = 0, i;
    int have_local;

    if (read_lines(root, bank, NULL, &plain) != 0 || plain.count == 0) {
        bw_msg_lines_free(&plain);
        return;
    }
    have_local = read_lines(root, bank, code, &local) == 0;
    made = (BwMsgLine *)calloc(plain.count, sizeof *made);
    for (i = 0; made != NULL && i < plain.count; i++) {
        const BwMsgLine *l = &plain.lines[i];
        u16 now[MAX_UNITS], tmpl[MAX_UNITS], from[MAX_UNITS], to[MAX_UNITS], out[MAX_UNITS];
        int nnow, nt, nf, nto, no = -1;
        u32 id = bank == BANK_USED ? l->index / 3u : l->index, src;

        if (line_at(&plain, l->index) != l || (have_local && line_at(&local, l->index) != NULL)) {
            continue;                   /* (a later line of the file, or translated) */
        }
        if ((bank == BANK_USED || bank == BANK_MCAPS) ? id < GAME_MOVES : id < GAME_ITEMS) {
            continue;                   /* (the game's own entries keep their lines) */
        }
        nnow = text_now(bank, l->index, now);
        if (nnow != (int)l->len || (l->len != 0 && memcmp(now, l->units, l->len * sizeof(u16)) != 0)) {
            continue;                   /* (a later mod changed it) */
        }
        if (bank == BANK_MCAPS) {
            int k;

            nto = text_now(BANK_MNAME, id, to);
            if (nto <= 0) {
                continue;
            }
            for (k = 0; k < nto; k++) {
                out[k] = upper(to[k]);
            }
            no = nto;
        } else {
            u32 name_bank = bank == BANK_USED ? BANK_MNAME : BANK_INAME;

            src = bank == BANK_USED ? move_source(id) : item_source(id);
            if (src == 0) {
                continue;               /* (an item with no source: its own line stays) */
            }
            nt = text_now(bank, bank == BANK_USED ? src * 3u + l->index % 3u : src, tmpl);
            nf = text_now(name_bank, src, from);
            nto = text_now(name_bank, id, to);
            if (nt < 0 || nf <= 0 || nto <= 0) {
                continue;
            }
            no = swap_name(tmpl, nt, from, nf, to, nto, out);
            if (no < 0) {
                fprintf(stderr, "eidos: text: bank %u line %u: the source line does not name its entry; "
                        "the mod's own line stays\n", (unsigned)bank, (unsigned)l->index);
                continue;
            }
        }
        made[n].units = (u16 *)malloc(((size_t)no + 1) * sizeof(u16));
        if (made[n].units == NULL) {
            continue;
        }
        memcpy(made[n].units, out, (size_t)no * sizeof(u16));
        made[n].index = l->index;
        made[n].len = (u32)no;
        n++;
    }
    if (n > 0 && bw_vfs_lines_set(root, TEXT_ARCHIVE, bank, made, n) != 0) {
        fprintf(stderr, "eidos: text: bank %u: the game-language lines could not be made\n", (unsigned)bank);
    }
    for (i = 0; made != NULL && i < n; i++) {
        free(made[i].units);
    }
    free(made);
    bw_msg_lines_free(&plain);
    if (have_local) {
        bw_msg_lines_free(&local);
    }
}

void bw_text_from_apply(void)
{
    static const u32 banks[] = { BANK_USED, BANK_MCAPS, BANK_ARTICLE, BANK_PLURAL };
    const char *code = game_code();
    u32 m, b;

    bw_vfs_lines_clear();
    read_text_from();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char lang[3];

        if (root == NULL) {
            continue;
        }
        mod_lang(root, lang);
        if (strcmp(lang, code) == 0) {
            continue;                   /* (written in the game's language) */
        }
        for (b = 0; b < sizeof banks / sizeof banks[0]; b++) {
            make_bank(root, banks[b], code);
        }
    }
}
