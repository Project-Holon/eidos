/*
 * sdk/item_heal.c -- how much HP an item heals, from a mod's table
 * (item_heal.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "item_heal.h"
#include "vfs.h"

enum { HP_NONE, HP_FIXED, HP_FRACTION, HP_FULL };

typedef struct Heal {
    u16 item;
    u8  kind;               /* HP_* of `hp`           */
    u16 value;              /* the amount, or the divisor */
    u16 held;               /* `held_hp`, 0 for none  */
} Heal;

static Heal *g_heals;
static u32   g_count, g_cap;

static Heal *find(u32 item)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_heals[i].item == item) {
            return &g_heals[i];
        }
    }
    return NULL;
}

static void say(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
}

static int number(const char *s, size_t n, long lo, long hi, long *out)
{
    char buf[16], *end;
    long v;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtol(buf, &end, 10);
    if (*end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

int bw_item_heal_parse_line(const char *line, char *why, int why_size)
{
    const char *p = line, *s, *eq;
    char *end;
    long item, v;
    Heal h, *have;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0' || *p == '#' || *p == '\r' || *p == '\n') {
        return 1;
    }
    item = strtol(p, &end, 10);
    if (end == p || item < 1 || item > 0xFFFF) {
        say(why, why_size, "expected `<item> = [hp=<amount>] [held_hp=<amount>]`");
        return -1;
    }
    p = end;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '=') {
        say(why, why_size, "expected `=` after the item");
        return -1;
    }
    p++;
    memset(&h, 0, sizeof h);
    h.item = (u16)item;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        s = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '#') {
            p++;
        }
        eq = memchr(s, '=', (size_t)(p - s));
        if (eq != NULL && eq - s == 2 && strncmp(s, "hp", 2) == 0) {
            const char *vs = eq + 1;
            size_t vn = (size_t)(p - vs);

            if (vn == 4 && strncmp(vs, "full", 4) == 0) {
                h.kind = HP_FULL;
            } else if (vn > 2 && vs[0] == '1' && vs[1] == '/' && number(vs + 2, vn - 2, 2, 16, &v) == 0) {
                h.kind = HP_FRACTION;
                h.value = (u16)v;
            } else if (number(vs, vn, 1, 65535, &v) == 0) {
                h.kind = HP_FIXED;
                h.value = (u16)v;
            } else {
                say(why, why_size, "`hp` is 1 to 65535, 1/2 to 1/16 or full");
                return -1;
            }
        } else if (eq != NULL && eq - s == 7 && strncmp(s, "held_hp", 7) == 0) {
            if (number(eq + 1, (size_t)(p - eq - 1), 1, 65535, &v) != 0) {
                say(why, why_size, "`held_hp` is 1 to 65535");
                return -1;
            }
            h.held = (u16)v;
        } else {
            say(why, why_size, "the keys are hp and held_hp");
            return -1;
        }
    }
    if (h.kind == HP_NONE && h.held == 0) {
        say(why, why_size, "expected hp= or held_hp=");
        return -1;
    }
    if ((have = find((u32)item)) != NULL) {
        *have = h;                      /* a later mod's line wins */
        return 0;
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2 : 16;
        Heal *grown = (Heal *)realloc(g_heals, cap * sizeof *grown);

        if (grown == NULL) {
            say(why, why_size, "out of memory");
            return -1;
        }
        g_heals = grown;
        g_cap = cap;
    }
    g_heals[g_count++] = h;
    return 0;
}

void bw_item_heal_clear(void)
{
    g_count = 0;
}

void bw_item_heal_load(void)
{
    u32 m;

    bw_item_heal_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256], why[160];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        snprintf(path, sizeof path, "%s/eidos/tables/item_heal.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_item_heal_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

int bw_item_heal_hp(u32 item, u32 max_hp, u32 missing, u32 *amount)
{
    const Heal *h = find(item);
    u32 a;

    if (h == NULL || h->kind == HP_NONE) {
        return 0;
    }
    switch (h->kind) {
    case HP_FULL:
        a = max_hp;
        break;
    case HP_FRACTION:
        a = max_hp / h->value;
        if (a == 0) {
            a = 1;                      /* (at least 1, as the game's fractions) */
        }
        break;
    default:
        a = h->value;
        break;
    }
    *amount = a < missing ? a : missing;
    return 1;
}

int bw_item_heal_held_hp(u32 item, u32 missing, u32 *amount)
{
    const Heal *h = find(item);

    if (h == NULL || h->held == 0) {
        return 0;
    }
    *amount = h->held < missing ? h->held : missing;
    return 1;
}
