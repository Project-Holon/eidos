/*
 * sdk/move_anims.c -- the animation a move plays (sdk/move_anims.h).
 *
 * A move's animation is the battle effect script of its id (a/0/6/6 member
 * = the move id, read by the battle view; the boundary between move scripts
 * and the other effects moves with limit.moves).  A mod may ship a script
 * of its own for any move, a new one included: a file a/0/6/6/<move id> in
 * the mod.  A move plays, in order: its move_anims.txt line's script, its
 * own (the game's, or a mod's), the script of the move its handler line
 * names (move_handlers.txt), Pound's.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "bw_limits.h"
#include "move_anims.h"
#include "plat_dir.h"
#include "vfs.h"

typedef struct Line {
    u16 move;
    u16 anim;
} Line;

static Line *g_lines;
static u32 g_count, g_cap;
static u8 *g_own;               /* a bit a move id: a mod ships its script */
static u32 g_own_n;             /* move ids the bits cover */

/* A mod's script of `move` (a file a/0/6/6/<move> in a mod). */
static int has_own_script(u32 move)
{
    return move < g_own_n && (g_own[move >> 3] >> (move & 7)) & 1;
}

static int own_entry(void *ctx, const char *name)
{
    long n = 0;
    int k = 0;

    (void)ctx;
    while (name[k] >= '0' && name[k] <= '9' && k < 6) {
        n = n * 10 + (name[k++] - '0');
    }
    if (k > 0 && (name[k] == '\0' || name[k] == '.') && (u32)n < g_own_n) {
        g_own[n >> 3] |= (u8)(1u << (n & 7));
    }
    return 0;
}

/* The scripts the mods ship, up to the move limit. */
static void find_own_scripts(void)
{
    u32 m;

    free(g_own);
    g_own_n = bw_limit_get(BW_PORT_LIMIT_MOVE_LAST) + 1u;
    if (g_own_n > 0x8000u) {
        g_own_n = 0x8000u;          /* (the request marks a mod's script with 0x8000) */
    }
    if ((g_own = (u8 *)calloc((g_own_n + 7u) / 8u, 1)) == NULL) {
        g_own_n = 0;
        return;
    }
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200];

        if (root != NULL && strlen(root) + 16 < sizeof path) {
            sprintf(path, "%s/a/0/6/6", root);
            (void)bw_plat_list_dir(path, own_entry, NULL);
        }
    }
}
static u32 g_preview, g_preview_a, g_preview_b;

void bw_move_anims_clear(void)
{
    free(g_lines);
    g_lines = NULL;
    g_count = g_cap = 0;
}

static int fail(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
    return -1;
}

int bw_move_anims_parse_line(const char *text, char *why, int why_size)
{
    char line[256], *hash, *e;
    const char *s;
    long move, anim;
    u32 i;

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
    move = strtol(s, &e, 10);
    if (e == s || move < 1 || (u32)move > bw_limit_get(BW_PORT_LIMIT_MOVE_LAST)) {
        return fail(why, why_size, "expected `<move id> = <a move with a script>`");
    }
    s = e;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '=') {
        return fail(why, why_size, "expected `=` after the move id");
    }
    s++;
    anim = strtol(s, &e, 10);
    if (e == s || anim < 1 || ((u32)anim > BW_ANIM_LAST && !has_own_script((u32)anim))) {
        return fail(why, why_size, "the animation is a move with a script: the game's 1 to 559, or one a mod ships");
    }
    while (isspace((unsigned char)*e)) {
        e++;
    }
    if (*e != '\0') {
        return fail(why, why_size, "one animation per line");
    }
    for (i = 0; i < g_count; i++) {
        if (g_lines[i].move == move) {
            g_lines[i].anim = (u16)anim;    /* a later line (a later mod's) wins */
            return 0;
        }
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2 : 32;
        Line *grown = (Line *)realloc(g_lines, cap * sizeof *grown);

        if (grown == NULL) {
            return fail(why, why_size, "out of memory");
        }
        g_lines = grown;
        g_cap = cap;
    }
    g_lines[g_count].move = (u16)move;
    g_lines[g_count].anim = (u16)anim;
    g_count++;
    return 0;
}

void bw_move_anims_load(void)
{
    u32 m;

    bw_move_anims_clear();
    find_own_scripts();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256], why[200];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/move_anims.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_move_anims_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

u32 bw_move_anim(u32 move)
{
    u32 i, base;

    for (i = 0; i < g_count; i++) {
        if (g_lines[i].move == move) {
            return g_lines[i].anim;
        }
    }
    if ((move >= 1 && move <= BW_ANIM_LAST) || has_own_script(move)) {
        return move;                        /* its own: the game's, or a mod's */
    }
    base = (u32)bw_bh_base(BW_BH_MOVE, (int)move);
    if (base != move && base >= 1 && (base <= BW_ANIM_LAST || has_own_script(base))) {
        return base;                        /* the move it works like */
    }
    return 1;                               /* Pound's, not another effect */
}

void bw_preview_set(u32 move, u32 attacker, u32 defender)
{
    g_preview = move;
    g_preview_a = attacker;
    g_preview_b = defender;
}

u32 bw_preview_move(void)
{
    return g_preview;
}

u32 bw_preview_attacker(void)
{
    return g_preview_a;
}

u32 bw_preview_defender(void)
{
    return g_preview_b;
}
