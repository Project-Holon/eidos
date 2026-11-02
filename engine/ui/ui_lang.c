/*
 * engine/ui/ui_lang.c -- the one-screen interface's own labels in the game's
 * language.
 *
 * The interface is written in English.  The kit's text functions pass every
 * string through ui_tr, which gives the French, German, Italian or Spanish
 * words for a label the table knows (engine/ui/ui_lang.tsv, generated into
 * ui_lang_table.h by tools/ui_lang.py) and the string itself otherwise: the
 * game's own text (names, the game's messages) is already in its language.
 */

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

const char *ui_tr(const char *en)
{
    u32 h = 2166136261u, lo = 0, hi = sizeof(k_ui_lang) / sizeof(k_ui_lang[0]);
    const u8 *p;

    if (g_col < 0 || en == NULL) {
        return en;
    }
    for (p = (const u8 *)en; *p != 0; p++) {
        h = (h ^ *p) * 16777619u;
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
            return k_ui_lang[lo].tr[g_col] != NULL ? k_ui_lang[lo].tr[g_col] : en;
        }
    }
    return en;
}
