/*
 * sdk/type_chart.c -- the type chart with the types mods add (limit.types).
 *
 * The game's chart is ov093's gBtlCalcTypeAffinityTable, one code per
 * attacking and defending type: 0 no effect, 2 half, 4 normal, 8 double.
 * The PC build of the game reads it as [attacker * count + defender], count
 * being limit.types + 1 (aletheia arm9/poke_type.h, POKETYPE_COUNT), and
 * takes 31 as "no type" instead of the DS's 17.  When ov093 loads, the port
 * builds a count x count chart: the game's cells, 4 for every pair with an
 * added type, then every enabled mod's eidos/tables/type_chart.txt in load
 * order:
 *
 *     <attacking type> = <code against type 0> <code against type 1> ...
 *
 * A row may stop short: the cells it leaves out keep their value.  A later
 * mod's cell wins.  Rows for the game's own types are allowed.  The port
 * points the chart's name at the built chart when there is one; with no
 * added type and no row, the game reads its own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "type_chart.h"
#include "vfs.h"

#define GAME_TYPES 17u

extern void *bw_dsref_ov093_gBtlCalcTypeAffinityTable __attribute__((weak));

static u8 *g_built;      /* count x count, the game's cells and the mods' rows */
static u8 *g_live;       /* the copy the game reads (a mod's chart_set writes it) */
static u32 g_built_count;
static int g_have;       /* g_built is up to date with the mod list */
static int g_rows;       /* some mod gave a row */

u32 bw_type_count(void)
{
    return bw_limit_get(BW_PORT_LIMIT_TYPE_LAST) + 1u;
}

/* One line of type_chart.txt. */
static void chart_line(const char *path, u32 line, const char *text, u32 count)
{
    char *end;
    unsigned long atk = strtoul(text, &end, 10);
    const char *s = end;
    u32 def = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (end == text || *s != '=') {
        fprintf(stderr, "eidos: %s:%u: expected `<type> = <code> <code> ...`\n", path, (unsigned)line);
        return;
    }
    if (atk >= count) {
        fprintf(stderr, "eidos: %s:%u: type %lu is past limit.types\n", path, (unsigned)line, atk);
        return;
    }
    s++;
    for (;;) {
        unsigned long v;

        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        v = strtoul(s, &end, 10);
        if (end == s || (v != 0 && v != 2 && v != 4 && v != 8)) {
            fprintf(stderr, "eidos: %s:%u: cell %u: codes are 0, 2, 4 or 8\n", path, (unsigned)line, (unsigned)def);
            return;
        }
        if (def >= count) {
            fprintf(stderr, "eidos: %s:%u: more cells than types\n", path, (unsigned)line);
            return;
        }
        g_built[atk * count + def] = (u8)v;
        def++;
        s = end;
    }
    g_rows = 1;
}

static void read_rows(u32 count)
{
    u32 m;

    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[512];
        FILE *f;
        u32 n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/type_chart.txt", root);
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
                chart_line(path, n, s, count);
            }
        }
        fclose(f);
    }
}

/* Build the chart from the game's 17 x 17 cells. */
static int build(const u8 *game)
{
    u32 n = bw_type_count(), a;

    if (g_have && g_built_count == n) {
        return 0;
    }
    free(g_built);
    free(g_live);
    g_built = (u8 *)malloc(n * n);
    g_live = (u8 *)malloc(n * n);
    if (g_built == NULL || g_live == NULL) {
        free(g_built);
        free(g_live);
        g_built = g_live = NULL;
        g_have = 0;
        return -1;
    }
    memset(g_built, 4, n * n);
    for (a = 0; a < GAME_TYPES && a < n; a++) {
        memcpy(g_built + a * n, game + a * GAME_TYPES, GAME_TYPES < n ? GAME_TYPES : n);
    }
    g_rows = 0;
    read_rows(n);
    g_built_count = n;
    g_have = 1;
    return 0;
}

int bw_type_chart_loaded(const u8 *game)
{
    if (&bw_dsref_ov093_gBtlCalcTypeAffinityTable == NULL || game == NULL || build(game) != 0) {
        return 0;
    }
    if (g_built_count == GAME_TYPES && !g_rows) {
        return 0;       /* the game's own chart, where it is */
    }
    memcpy(g_live, g_built, g_built_count * g_built_count);
    bw_dsref_ov093_gBtlCalcTypeAffinityTable = g_live;
    return 1;
}

const u8 *bw_type_chart_built(void)
{
    return g_have && (g_built_count != GAME_TYPES || g_rows) ? g_built : NULL;
}

void bw_type_chart_forget(void)
{
    g_have = 0;
}
