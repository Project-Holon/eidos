/*
 * sdk/shops.c -- the items shops sell, from the mods' eidos/tables/shops.txt
 * (sdk/shops.h).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "shops.h"
#include "vfs.h"

typedef struct List {
    int count;              /* 0 or -1: the game's own */
    u16 items[BW_SHOP_ITEMS];
    s32 prices[BW_SHOP_ITEMS];
} List;

static List g_mart[BW_SHOP_MARTS];
static List g_bp[BW_SHOP_BP];

void bw_shops_clear(void)
{
    int i;

    for (i = 0; i < BW_SHOP_MARTS; i++) {
        g_mart[i].count = -1;
    }
    for (i = 0; i < BW_SHOP_BP; i++) {
        g_bp[i].count = -1;
    }
}

static int fail(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
    return -1;
}

int bw_shops_parse_line(const char *text, char *why, int why_size)
{
    char line[1024], *hash, *e;
    const char *s;
    int bp, n, count = 0;
    long v;
    List l;
    u32 last_item = bw_limit_get(BW_PORT_LIMIT_ITEM_LAST);

    snprintf(line, sizeof line, "%s", text);
    if ((hash = strchr(line, '#')) != NULL) {
        *hash = '\0';
    }
    s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return 1;
    }
    if (strncmp(s, "mart.", 5) == 0) {
        bp = 0;
        s += 5;
    } else if (strncmp(s, "bp.", 3) == 0) {
        bp = 1;
        s += 3;
    } else {
        return fail(why, why_size, "expected `mart.<n> = <item> ...` or `bp.<n> = <item>:<price> ...`");
    }
    v = strtol(s, &e, 10);
    if (e == s || v < 0 || v >= (bp ? BW_SHOP_BP : BW_SHOP_MARTS)) {
        return fail(why, why_size, bp ? "the BP list is 0 to 2" : "the mart list is 0 to 25");
    }
    n = (int)v;
    s = e;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '=') {
        return fail(why, why_size, "expected `=` after the list");
    }
    s++;
    memset(&l, 0, sizeof l);
    for (;;) {
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        if (count == BW_SHOP_ITEMS) {
            return fail(why, why_size, "more than 40 items");
        }
        v = strtol(s, &e, 10);
        if (e == s || v < 1 || (u32)v > last_item) {
            return fail(why, why_size, "an item id is 1 to the item limit");
        }
        l.items[count] = (u16)v;
        s = e;
        if (bp) {
            if (*s != ':') {
                return fail(why, why_size, "a BP list's item is `<item>:<price>`");
            }
            v = strtol(s + 1, &e, 10);
            if (e == s + 1 || v < 0 || v > 65535) {
                return fail(why, why_size, "a price is 0 to 65535");
            }
            l.prices[count] = (s32)v;
            s = e;
        } else if (*s == ':') {
            return fail(why, why_size, "a mart list's items have no price (their own is used)");
        }
        if (*s != '\0' && !isspace((unsigned char)*s)) {
            return fail(why, why_size, "expected a space between items");
        }
        count++;
    }
    if (count == 0) {
        return fail(why, why_size, "a list needs at least one item");
    }
    l.count = count;
    if (bp) {
        g_bp[n] = l;
    } else {
        g_mart[n] = l;
    }
    return 0;
}

void bw_shops_load(void)
{
    u32 m;

    bw_shops_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[1024], why[200];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/shops.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_shops_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

int bw_shop_mart(int list, const u16 **items)
{
    if (list < 0 || list >= BW_SHOP_MARTS || g_mart[list].count <= 0) {
        return -1;
    }
    *items = g_mart[list].items;
    return g_mart[list].count;
}

int bw_shop_bp(int list, const u16 **items, const s32 **prices)
{
    if (list < 0 || list >= BW_SHOP_BP || g_bp[list].count <= 0) {
        return -1;
    }
    *items = g_bp[list].items;
    *prices = g_bp[list].prices;
    return g_bp[list].count;
}
