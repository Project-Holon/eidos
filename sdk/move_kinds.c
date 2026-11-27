/*
 * sdk/move_kinds.c -- bite, pulse, ball, powder and one_hit moves
 * (move_kinds.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "move_kinds.h"
#include "vfs.h"

/* The game's moves of each kind, from the later games' lists: the moves
 * Strong Jaw (bite), Mega Launcher (pulse), Bulletproof (ball and bomb)
 * and Overcoat and Grass types (powder) act on, and the moves Parental
 * Bond hits once with (one_hit: the user faints, the target's HP is set,
 * the held item is thrown; a second hit would repeat that).  Move ids are
 * Black and White's own. */
typedef struct BuiltIn {
    u16 move;
    u8  kinds;
} BuiltIn;

static const BuiltIn k_builtin[] = {
    { 44, BW_MOVE_KIND_BITE },      /* Bite */
    { 77, BW_MOVE_KIND_POWDER },    /* PoisonPowder */
    { 78, BW_MOVE_KIND_POWDER },    /* Stun Spore */
    { 79, BW_MOVE_KIND_POWDER },    /* Sleep Powder */
    { 120, BW_MOVE_KIND_ONE_HIT },  /* Self-Destruct */
    { 121, BW_MOVE_KIND_BALL },     /* Egg Bomb */
    { 140, BW_MOVE_KIND_BALL },     /* Barrage */
    { 147, BW_MOVE_KIND_POWDER },   /* Spore */
    { 153, BW_MOVE_KIND_ONE_HIT },  /* Explosion */
    { 158, BW_MOVE_KIND_BITE },     /* Hyper Fang */
    { 178, BW_MOVE_KIND_POWDER },   /* Cotton Spore */
    { 188, BW_MOVE_KIND_BALL },     /* Sludge Bomb */
    { 190, BW_MOVE_KIND_BALL },     /* Octazooka */
    { 192, BW_MOVE_KIND_BALL },     /* Zap Cannon */
    { 242, BW_MOVE_KIND_BITE },     /* Crunch */
    { 247, BW_MOVE_KIND_BALL },     /* Shadow Ball */
    { 283, BW_MOVE_KIND_ONE_HIT },  /* Endeavor */
    { 296, BW_MOVE_KIND_BALL },     /* Mist Ball */
    { 301, BW_MOVE_KIND_BALL },     /* Ice Ball */
    { 305, BW_MOVE_KIND_BITE },     /* Poison Fang */
    { 311, BW_MOVE_KIND_BALL },     /* Weather Ball */
    { 331, BW_MOVE_KIND_BALL },     /* Bullet Seed */
    { 352, BW_MOVE_KIND_PULSE },    /* Water Pulse */
    { 360, BW_MOVE_KIND_BALL },     /* Gyro Ball */
    { 374, BW_MOVE_KIND_ONE_HIT },  /* Fling */
    { 396, BW_MOVE_KIND_PULSE | BW_MOVE_KIND_BALL },  /* Aura Sphere */
    { 399, BW_MOVE_KIND_PULSE },    /* Dark Pulse */
    { 402, BW_MOVE_KIND_BALL },     /* Seed Bomb */
    { 406, BW_MOVE_KIND_PULSE },    /* Dragon Pulse */
    { 411, BW_MOVE_KIND_BALL },     /* Focus Blast */
    { 412, BW_MOVE_KIND_BALL },     /* Energy Ball */
    { 422, BW_MOVE_KIND_BITE },     /* Thunder Fang */
    { 423, BW_MOVE_KIND_BITE },     /* Ice Fang */
    { 424, BW_MOVE_KIND_BITE },     /* Fire Fang */
    { 426, BW_MOVE_KIND_BALL },     /* Mud Bomb */
    { 439, BW_MOVE_KIND_BALL },     /* Rock Wrecker */
    { 443, BW_MOVE_KIND_BALL },     /* Magnet Bomb */
    { 476, BW_MOVE_KIND_POWDER },   /* Rage Powder */
    { 486, BW_MOVE_KIND_BALL },     /* Electro Ball */
    { 491, BW_MOVE_KIND_BALL },     /* Acid Spray */
    { 505, BW_MOVE_KIND_PULSE },    /* Heal Pulse */
    { 515, BW_MOVE_KIND_ONE_HIT },  /* Final Gambit */
    { 545, BW_MOVE_KIND_BALL },     /* Searing Shot */
};

#define BUILTIN_COUNT ((int)(sizeof(k_builtin) / sizeof(k_builtin[0])))

typedef struct Line {
    u16 move;
    u8  kinds;
} Line;

static Line *g_lines;
static int   g_count, g_cap;

typedef struct Word {
    const char *name;
    u32 bit;
} Word;

static const Word k_words[] = {
    { "bite", BW_MOVE_KIND_BITE }, { "pulse", BW_MOVE_KIND_PULSE }, { "ball", BW_MOVE_KIND_BALL },
    { "powder", BW_MOVE_KIND_POWDER }, { "one_hit", BW_MOVE_KIND_ONE_HIT }, { NULL, 0 }
};

u32 bw_move_kind_by_name(const char *s, size_t n)
{
    const Word *w;

    for (w = k_words; w->name != NULL; w++) {
        if (strlen(w->name) == n && strncmp(w->name, s, n) == 0) {
            return w->bit;
        }
    }
    return 0;
}

u32 bw_move_kinds(u32 move)
{
    int i;

    for (i = g_count - 1; i >= 0; i--) {
        if (g_lines[i].move == move) {
            return g_lines[i].kinds;
        }
    }
    for (i = 0; i < BUILTIN_COUNT; i++) {
        if (k_builtin[i].move == move) {
            return k_builtin[i].kinds;
        }
    }
    return 0;
}

static void say(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int bw_move_kinds_parse_line(const char *line, char *why, int why_size)
{
    const char *p = line, *s;
    char *end;
    long v;
    u32 kinds = 0, bit;
    int words = 0;
    char msg[96];

    while (is_space(*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#') {
        return 1;
    }
    v = strtol(p, &end, 10);
    if (end == p || v < 1 || v > 0xFFFF) {
        say(why, why_size, "expected `<move id> = <kind> [<kind> ...]`");
        return -1;
    }
    p = end;
    while (is_space(*p)) {
        p++;
    }
    if (*p != '=') {
        say(why, why_size, "expected `=` after the move id");
        return -1;
    }
    p++;
    for (;;) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        s = p;
        while (*p != '\0' && !is_space(*p) && *p != '#') {
            p++;
        }
        words++;
        if ((size_t)(p - s) == 4 && strncmp(s, "none", 4) == 0) {
            continue;
        }
        if ((bit = bw_move_kind_by_name(s, (size_t)(p - s))) == 0) {
            snprintf(msg, sizeof msg, "`%.*s` is not a move kind (bite, pulse, ball, powder, one_hit, none)", (int)(p - s), s);
            say(why, why_size, msg);
            return -1;
        }
        kinds |= bit;
    }
    if (words == 0) {
        say(why, why_size, "expected a kind after `=` (bite, pulse, ball, powder, one_hit, none)");
        return -1;
    }
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 32;
        Line *grown = (Line *)realloc(g_lines, (size_t)cap * sizeof *grown);

        if (grown == NULL) {
            say(why, why_size, "out of memory");
            return -1;
        }
        g_lines = grown;
        g_cap = cap;
    }
    g_lines[g_count].move = (u16)v;
    g_lines[g_count].kinds = (u8)kinds;
    g_count++;
    return 0;
}

void bw_move_kinds_clear(void)
{
    g_count = 0;
}

void bw_move_kinds_load(void)
{
    u32 m;

    bw_move_kinds_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256], why[160];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        snprintf(path, sizeof path, "%s/eidos/tables/move_kinds.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (bw_move_kinds_parse_line(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}
