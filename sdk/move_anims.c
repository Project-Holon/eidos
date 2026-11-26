/*
 * sdk/move_anims.c -- the animation a move plays (sdk/move_anims.h).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "bw_limits.h"
#include "move_anims.h"
#include "vfs.h"

typedef struct Line {
    u16 move;
    u16 anim;
} Line;

static Line *g_lines;
static u32 g_count, g_cap;
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
        return fail(why, why_size, "expected `<move id> = <the game's move 1..559>`");
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
    if (e == s || anim < 1 || (u32)anim > BW_ANIM_LAST) {
        return fail(why, why_size, "the animation is one of the game's moves, 1 to 559");
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
    if (move >= 1 && move <= BW_ANIM_LAST) {
        return move;                        /* the game's own */
    }
    base = (u32)bw_bh_base(BW_BH_MOVE, (int)move);
    if (base != move && base >= 1 && base <= BW_ANIM_LAST) {
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
