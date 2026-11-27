/*
 * sdk/balls.c -- Poke Balls a mod adds or tunes (balls.h,
 * eidos/tables/balls.txt).
 *
 * One line a ball item:
 *
 *     <item> = <base ball item> [ball=<26..255>] [key=value ...]   # comment
 *
 * The base is one of the game's 25 ball items, the ball the item works
 * like.  An added item needs `ball=`, the id its caught Pokemon keep; a
 * line for a game ball tunes it and keeps its own id.  The keys and what
 * each base takes are in docs/game-tables.md (balls.txt).
 *
 * This file also defines five of the game's ARM9 functions, so the game's
 * own callers reach the table (the build cuts the game's definitions, as
 * for sdk/machines.c):
 *
 *   ITEM_LookupBallIdPair, ITEM_GetBallIdForItem (arm9 palanm.c): the
 *     game's 25 {item, ball id} pairs, then the table's;
 *   APP_ICON_GetBallThrowCellFileId, APP_ICON_GetBallThrowAnimFileId (arm9
 *     app_icon.c): the summary's and the trade screen's ball icon, its
 *     palette and character files in a/0/8/3;
 *   NATSUKI_Apply (arm9 natsuki.c): a friendship gain, one more for a
 *     Pokemon in the Luxury Ball or a ball that works like it.
 *
 * With no line, each is the game's own, with one guard: a ball id past 25
 * that the table does not know is the Poke Ball's icon.  The game would
 * read files 0x56 + id and 0x6F + id, which past 25 are other kinds of
 * file (a character file as a palette) or past the archive's end.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "balls.h"
#include "bw_limits.h"
#include "dsmem.h"
#include "port.h"
#include "vfs.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

/* ------------------------------------------------------------------ */
/* The game's balls                                                    */
/* ------------------------------------------------------------------ */

/* arm9 palanm.c: sItemBallIdTable, 25 {item, ball id} pairs (items 1..16
 * are ball ids 1..16, items 492..499 are 17..24, item 576 is 25). */
#define ITEM_BALL_ID_TABLE_ADDR 0x0209E904u
#define ITEM_BALL_ID_COUNT      0x19u

/* The ball items whose rules the game's code names (ov093
 * BTL_SERVER_FLOW_GetBallCatchRate, CalcCaptureShakes; ov092
 * EVENT_BATTLE_RETURN_ApplyHealBall; arm9 NATSUKI_Apply). */
#define ITEM_MASTER_BALL  1
#define ITEM_ULTRA_BALL   2
#define ITEM_GREAT_BALL   3
#define ITEM_NET_BALL     6
#define ITEM_DIVE_BALL    7
#define ITEM_NEST_BALL    8
#define ITEM_REPEAT_BALL  9
#define ITEM_TIMER_BALL   10
#define ITEM_LUXURY_BALL  11
#define ITEM_DUSK_BALL    13
#define ITEM_HEAL_BALL    14
#define ITEM_QUICK_BALL   15

#define BALL_POKE         4     /* the ball id the game shows for none */

#define POKETYPE_BUG      6
#define POKETYPE_WATER    10

#define FX_ONE            0x1000

/* The game's table, in its data (NULL before the data is installed). */
static const u16 *game_pairs(void)
{
    return (const u16 *)bw_ds_try_ptr(ITEM_BALL_ID_TABLE_ADDR);
}

/* The game's lookup: column 0 finds by item, column 1 by ball id. */
static u32 game_lookup(u32 value, u32 column)
{
    const u16 *t = game_pairs();
    u32 i;

    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < ITEM_BALL_ID_COUNT; i++) {
        if (t[i * 2 + column] == value) {
            return t[i * 2 + (column ^ 1)];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The lines                                                           */
/* ------------------------------------------------------------------ */

enum {
    KEY_RATIO = 1u << 0,
    KEY_PER_TURN = 1u << 1,
    KEY_TYPE = 1u << 2,
    KEY_TYPE_2 = 1u << 3,
    KEY_LEVEL = 1u << 4,
    KEY_TURNS = 1u << 5,
    KEY_HEAL = 1u << 6,
    KEY_FRIENDSHIP = 1u << 7,
    KEY_BONUS = 1u << 8,
    KEY_PARTICLES = 1u << 9,
    KEY_SPRITE = 1u << 10,
    KEY_ICON = 1u << 11
};

typedef struct Ball {
    u16 item;
    u16 base;               /* the game ball item it works like */
    u16 ballId;             /* 26..255 for an added ball, 0 for a game ball's line */
    u16 has;                /* KEY_* given */
    int ratio;
    int perTurn;
    int type[2];
    int level;
    int turns;
    int heal;
    int friendship;
    int bonus;
    int particles[6];       /* keys 3, 0x1D, 0x2E, 0x2F, 0x30, 0x31 */
    int sprite[2];          /* a/0/9/5: character, palette */
    int icon[2];            /* a/0/8/3: palette, character */
    char where[96];         /* file:line, for a line refused once the game's balls are known */
} Ball;

static Ball *g_balls;
static u32 g_count, g_cap;
static int g_checked;       /* the bases were checked against the game's table */

static void say(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
}

static int int_value(const char *s, size_t n, long lo, long hi, int *out)
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
    *out = (int)v;
    return 0;
}

/* "1.5": fx32, `lo` to `hi`. */
static int ratio_value(const char *s, size_t n, double lo, double hi, int *out)
{
    char buf[16], *end;
    double v;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtod(buf, &end);
    if (*end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = (int)(v * 4096.0 + 0.5);
    return 0;
}

static const struct {
    const char *name;
    int value;
} k_types[] = {
    { "normal", 0 }, { "fighting", 1 }, { "flying", 2 }, { "poison", 3 }, { "ground", 4 },
    { "rock", 5 }, { "bug", 6 }, { "ghost", 7 }, { "steel", 8 }, { "fire", 9 },
    { "water", 10 }, { "grass", 11 }, { "electric", 12 }, { "psychic", 13 }, { "ice", 14 },
    { "dragon", 15 }, { "dark", 16 }, { NULL, 0 }
};

extern int bw_type_by_name(const char *s, size_t n) __attribute__((weak));     /* sdk/type_icons.c */
extern u32 bw_type_count(void) __attribute__((weak));                           /* sdk/type_chart.c */

/* A type: the game's name, an added type's types.txt `name`, or its number
 * (as the battle handlers' `type`). */
static int type_value(const char *s, size_t n, int *out)
{
    u32 count = bw_type_count != NULL ? bw_type_count() : 17u;
    int i;

    for (i = 0; k_types[i].name != NULL; i++) {
        if (strlen(k_types[i].name) == n && strncmp(k_types[i].name, s, n) == 0) {
            *out = k_types[i].value;
            return 0;
        }
    }
    if (bw_type_by_name != NULL && (*out = bw_type_by_name(s, n)) >= 0) {
        return 0;
    }
    return int_value(s, n, 0, (long)count - 1, out);
}

/* "a,b,c": exactly `count` member numbers. */
static int members_value(const char *s, size_t n, int count, int *out)
{
    const char *end = s + n;
    int i;

    for (i = 0; i < count; i++) {
        const char *c = s;

        while (c < end && *c != ',') {
            c++;
        }
        if (int_value(s, (size_t)(c - s), 0, 0xFFFF, &out[i]) != 0) {
            return -1;
        }
        if (i + 1 < count) {
            if (c == end) {
                return -1;
            }
            s = c + 1;
        } else if (c != end) {
            return -1;
        }
    }
    return 0;
}

static Ball *find_item(u32 item)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_balls[i].item == item) {
            return &g_balls[i];
        }
    }
    return NULL;
}

static Ball *find_id(u32 ballId)
{
    u32 i;

    if (ballId <= BW_BALL_GAME_LAST) {
        return NULL;
    }
    for (i = 0; i < g_count; i++) {
        if (g_balls[i].ballId == ballId) {
            return &g_balls[i];
        }
    }
    return NULL;
}

static void drop(u32 i)
{
    memmove(&g_balls[i], &g_balls[i + 1], (g_count - i - 1) * sizeof *g_balls);
    g_count--;
}

/* Which keys a base takes besides the ones every ball takes. */
static u32 base_keys(u32 base)
{
    switch (base) {
    case ITEM_MASTER_BALL: return 0;
    case ITEM_NET_BALL:    return KEY_RATIO | KEY_TYPE | KEY_TYPE_2;
    case ITEM_NEST_BALL:   return KEY_RATIO | KEY_LEVEL;
    case ITEM_TIMER_BALL:  return KEY_RATIO | KEY_PER_TURN;
    case ITEM_QUICK_BALL:  return KEY_RATIO | KEY_TURNS;
    default:               return KEY_RATIO;
    }
}

int bw_balls_parse_line(const char *line, char *why, int why_size)
{
    const char *p = line, *s, *eq;
    char *end;
    long item, base;
    Ball b, *have;
    u32 i;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0' || *p == '#' || *p == '\r' || *p == '\n') {
        return 1;
    }
    item = strtol(p, &end, 10);
    if (end == p || item < 1 || item > 0xFFFE) {
        say(why, why_size, "expected `<item> = <base ball item> [key=value ...]`");
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
    base = strtol(p, &end, 10);
    if (end == p || base < 1 || base > 0xFFFE) {
        say(why, why_size, "expected the item of the game's ball it works like after `=`");
        return -1;
    }
    p = end;
    memset(&b, 0, sizeof b);
    b.item = (u16)item;
    b.base = (u16)base;
    for (;;) {
        size_t kn, vn;
        const char *vs;

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
        if (eq == NULL) {
            say(why, why_size, "expected key=value");
            return -1;
        }
        kn = (size_t)(eq - s);
        vs = eq + 1;
        vn = (size_t)(p - vs);
#define KEY_IS(name) (kn == sizeof(name) - 1 && strncmp(s, name, kn) == 0)
        if (KEY_IS("ball")) {
            int v;

            if (int_value(vs, vn, BW_BALL_GAME_LAST + 1, BW_BALL_LAST, &v) != 0) {
                say(why, why_size, "`ball` is 26 to 255");
                return -1;
            }
            b.ballId = (u16)v;
        } else if (KEY_IS("ratio")) {
            if (ratio_value(vs, vn, 0.1, 255.0, &b.ratio) != 0) {
                say(why, why_size, "`ratio` is 0.1 to 255");
                return -1;
            }
            b.has |= KEY_RATIO;
        } else if (KEY_IS("per_turn")) {
            if (ratio_value(vs, vn, 0.0, 255.0, &b.perTurn) != 0) {
                say(why, why_size, "`per_turn` is 0 to 255");
                return -1;
            }
            b.has |= KEY_PER_TURN;
        } else if (KEY_IS("type") || KEY_IS("type.2")) {
            int k = kn == 4 ? 0 : 1;

            if (type_value(vs, vn, &b.type[k]) != 0) {
                say(why, why_size, "`type` is a type's name or number");
                return -1;
            }
            b.has |= k == 0 ? KEY_TYPE : KEY_TYPE_2;
        } else if (KEY_IS("level")) {
            if (int_value(vs, vn, 1, 101, &b.level) != 0) {
                say(why, why_size, "`level` is 1 to 101");
                return -1;
            }
            b.has |= KEY_LEVEL;
        } else if (KEY_IS("turns")) {
            if (int_value(vs, vn, 1, 255, &b.turns) != 0) {
                say(why, why_size, "`turns` is 1 to 255");
                return -1;
            }
            b.has |= KEY_TURNS;
        } else if (KEY_IS("heal")) {
            if (int_value(vs, vn, 0, 1, &b.heal) != 0) {
                say(why, why_size, "`heal` is 0 or 1");
                return -1;
            }
            b.has |= KEY_HEAL;
        } else if (KEY_IS("friendship")) {
            if (int_value(vs, vn, 0, 255, &b.friendship) != 0) {
                say(why, why_size, "`friendship` is 0 to 255");
                return -1;
            }
            b.has |= KEY_FRIENDSHIP;
        } else if (KEY_IS("friendship_bonus")) {
            if (int_value(vs, vn, 0, 10, &b.bonus) != 0) {
                say(why, why_size, "`friendship_bonus` is 0 to 10");
                return -1;
            }
            b.has |= KEY_BONUS;
        } else if (KEY_IS("particles")) {
            if (members_value(vs, vn, 6, b.particles) != 0) {
                say(why, why_size, "`particles` is six a/0/0/6 members: a,b,c,d,e,f");
                return -1;
            }
            b.has |= KEY_PARTICLES;
        } else if (KEY_IS("sprite")) {
            if (members_value(vs, vn, 2, b.sprite) != 0) {
                say(why, why_size, "`sprite` is two a/0/9/5 members: character,palette");
                return -1;
            }
            b.has |= KEY_SPRITE;
        } else if (KEY_IS("icon")) {
            if (members_value(vs, vn, 2, b.icon) != 0) {
                say(why, why_size, "`icon` is two a/0/8/3 members: palette,character");
                return -1;
            }
            b.has |= KEY_ICON;
        } else {
            say(why, why_size, "the keys are ball, ratio, per_turn, type, type.2, level, turns, heal, "
                               "friendship, friendship_bonus, particles, sprite and icon");
            return -1;
        }
#undef KEY_IS
    }
    if ((b.has & (KEY_RATIO | KEY_PER_TURN | KEY_TYPE | KEY_TYPE_2 | KEY_LEVEL | KEY_TURNS)) & ~base_keys(b.base)) {
        say(why, why_size, "a key the base's catch rule has no place for (docs/game-tables.md, balls.txt)");
        return -1;
    }
    if ((have = find_item((u32)item)) != NULL) {
        *have = b;                      /* a later mod's line wins */
    } else {
        if (g_count == g_cap) {
            u32 cap = g_cap != 0 ? g_cap * 2 : 16;
            Ball *grown = (Ball *)realloc(g_balls, cap * sizeof *grown);

            if (grown == NULL) {
                say(why, why_size, "out of memory");
                return -1;
            }
            g_balls = grown;
            g_cap = cap;
        }
        g_balls[g_count++] = b;
        have = &g_balls[g_count - 1];
    }
    /* A ball id is one ball's: a later line that takes it wins. */
    for (i = 0; b.ballId != 0 && i < g_count; i++) {
        if (&g_balls[i] != have && g_balls[i].ballId == b.ballId) {
            fprintf(stderr, "eidos: balls: item %u and item %u both take ball %u; item %u keeps it\n",
                    (unsigned)g_balls[i].item, (unsigned)b.item, (unsigned)b.ballId, (unsigned)b.item);
            if (&g_balls[i] < have) {
                have--;
            }
            drop(i);
            break;
        }
    }
    g_checked = 0;
    return 0;
}

/* The lines whose base is not a game ball, or that add an item with no
 * ball id, or take a game ball's item for a new id, are dropped once the
 * game's table can be read. */
static void check(void)
{
    const u16 *t = game_pairs();
    u32 i = 0;

    /* (the table's first item is the Master Ball; 0 before the game's
     * data is in memory) */
    if (g_checked || t == NULL || t[0] == 0) {
        return;
    }
    g_checked = 1;
    while (i < g_count) {
        Ball *b = &g_balls[i];
        const char *bad = NULL;
        u32 own = game_lookup(b->item, 0);

        if (game_lookup(b->base, 0) == 0) {
            bad = "the base is not one of the game's ball items";
        } else if (own == 0 && b->ballId == 0) {
            bad = "an added ball needs ball=26..255";
        } else if (own != 0 && b->ballId != 0) {
            bad = "a game ball keeps its own ball id (no ball=)";
        }
        if (bad != NULL) {
            fprintf(stderr, "eidos: balls: %s: item %u: %s; the line is left out\n",
                    b->where[0] != '\0' ? b->where : "balls.txt", (unsigned)b->item, bad);
            drop(i);
            continue;
        }
        i++;
    }
}

void bw_balls_clear(void)
{
    g_count = 0;
    g_checked = 0;
}

/* ------------------------------------------------------------------ */
/* The battle Bag's pockets of the items mods add                      */
/* ------------------------------------------------------------------ */

/*
 * The battle Bag sorts the bag's items into its four pockets with one byte
 * an item from a/2/3/2 member 0 (ov197 B_BAG_MAIN_BuildPocketLists: bit 0
 * Poke Balls, 1 Battle Items, 2 HP/PP Restore, 3 Status Restore), read at
 * the item's id.  The game's table has a byte for each of its items and no
 * more, so an item past them read the bytes after the table: an added ball
 * showed among the battle items.  Each item's record holds the same bits
 * (a/0/2/4, struct ItemRecord.battlePockets, bits 11..15 of the u16 at
 * +8), so the table is made as long as the item limit, the added items'
 * bytes taken from their records.  A mod's own table member is kept: only
 * the items past its end are added.
 */
#define BATTLE_POCKET_ARC     "a/2/3/2"
#define ITEM_DATA_ARC         "a/0/2/4"
#define ITEM_RECORD_POCKETS   8u

static void grow_battle_pockets(void)
{
    BwArcFile arc;
    u32 items = bw_limit_get(BW_PORT_LIMIT_ITEM_LAST) + 1u;
    u8 *table = NULL;
    long n = -1;
    u32 item;

    if (bw_arc_open(&arc, BATTLE_POCKET_ARC) != 0) {
        return;
    }
    if (arc.count > 0) {
        n = bw_arc_member_size(&arc, 0);
    }
    if (n > 0 && (u32)n < items && (table = (u8 *)calloc(items, 1)) != NULL &&
        bw_arc_read_member(&arc, 0, table, (u32)n) != n) {
        free(table);
        table = NULL;
    }
    bw_arc_close(&arc);
    if (table == NULL) {
        return;
    }
    if (bw_arc_open(&arc, ITEM_DATA_ARC) == 0) {
        for (item = (u32)n; item < items && item < arc.count; item++) {
            long size = bw_arc_member_size(&arc, item);
            u8 *rec = size >= (long)ITEM_RECORD_POCKETS + 2 ? (u8 *)malloc((size_t)size) : NULL;

            if (rec != NULL && bw_arc_read_member(&arc, item, rec, (u32)size) == size) {
                table[item] = (u8)((rec[ITEM_RECORD_POCKETS] | rec[ITEM_RECORD_POCKETS + 1] << 8) >> 11);
            }
            free(rec);
        }
        bw_arc_close(&arc);
    }
    (void)bw_vfs_mem_set("", BATTLE_POCKET_ARC, 0, table, items);
    bw_vfs_flush_path(BATTLE_POCKET_ARC);
    free(table);
}

void bw_balls_load(void)
{
    u32 m;

    grow_battle_pockets();
    bw_balls_clear();
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[512], why[200];
        FILE *f;
        int n = 0, rc;

        if (root == NULL || strlen(root) + 40 > sizeof path) {
            continue;
        }
        snprintf(path, sizeof path, "%s/eidos/tables/balls.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            rc = bw_balls_parse_line(line, why, sizeof why);
            if (rc < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            } else if (rc == 0) {
                Ball *b = find_item((u32)strtol(line, NULL, 10));

                if (b != NULL) {
                    snprintf(b->where, sizeof b->where, "mod %u, balls.txt:%d", (unsigned)m, n);
                }
            }
        }
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

static const Ball *line_of_item(u32 item)
{
    check();
    return find_item(item);
}

static const Ball *line_of_id(u32 ballId)
{
    u32 item;

    check();
    if (ballId <= BW_BALL_GAME_LAST) {
        item = game_lookup(ballId, 1);
        return item != 0 ? find_item(item) : NULL;
    }
    return find_id(ballId);
}

u32 bw_ball_id_of_item(u32 item)
{
    const Ball *b = line_of_item(item);

    if (b != NULL && b->ballId != 0) {
        return b->ballId;
    }
    return game_lookup(item, 0);
}

u32 bw_ball_item_of_id(u32 ballId)
{
    const Ball *b;

    if (ballId <= BW_BALL_GAME_LAST) {
        return game_lookup(ballId, 1);
    }
    b = line_of_id(ballId);
    return b != NULL ? b->item : 0;
}

int bw_ball_shown(int ballId)
{
    const Ball *b;

    if (ballId >= 0 && (u32)ballId <= BW_BALL_GAME_LAST) {
        return ballId;
    }
    b = line_of_id((u32)ballId);
    if (b != NULL) {
        return (int)game_lookup(b->base, 0);
    }
    return BALL_POKE;
}

int bw_ball_particle(int ballId, int key)
{
    const Ball *b = ballId > 0 ? line_of_id((u32)ballId) : NULL;
    int k;

    if (b == NULL || !(b->has & KEY_PARTICLES)) {
        return -1;
    }
    switch (key) {
    case 0x03: k = 0; break;
    case 0x1D: k = 1; break;
    case 0x2E: case 0x2F: case 0x30: case 0x31: k = 2 + (key - 0x2E); break;
    default: return -1;
    }
    return b->particles[k];
}

int bw_ball_sprite(int ballId, int which)
{
    const Ball *b = ballId > 0 ? line_of_id((u32)ballId) : NULL;

    if (b == NULL || !(b->has & KEY_SPRITE) || which < 0 || which > 1) {
        return -1;
    }
    return b->sprite[which];
}

int bw_ball_icon(int ballId, int which)
{
    const Ball *b = ballId > 0 ? line_of_id((u32)ballId) : NULL;

    if (b == NULL || !(b->has & KEY_ICON) || which < 0 || which > 1) {
        return -1;
    }
    return b->icon[which];
}

int bw_ball_catch_base(int item)
{
    const Ball *b = item > 0 ? line_of_item((u32)item) : NULL;

    return b != NULL ? (int)b->base : item;
}

/* The game's numbers for a base (ov093 BTL_SERVER_FLOW_GetBallCatchRate). */
static int game_value(int base, int which)
{
    switch (which) {
    case BW_BALL_RATIO:
        switch (base) {
        case ITEM_ULTRA_BALL:  return 0x2000;
        case ITEM_GREAT_BALL:  return 0x1800;
        case ITEM_NET_BALL:    return 0x3000;
        case ITEM_DIVE_BALL:   return 0x3800;
        case ITEM_NEST_BALL:   return 0x4000;   /* the most: (41 - 1) / 10 */
        case ITEM_REPEAT_BALL: return 0x3000;
        case ITEM_TIMER_BALL:  return 0x4000;   /* the most */
        case ITEM_DUSK_BALL:   return 0x3800;
        case ITEM_QUICK_BALL:  return 0x5000;
        default:               return FX_ONE;
        }
    case BW_BALL_PER_TURN: return 0x4CD;        /* Timer Ball: 0.3 a turn */
    case BW_BALL_TYPE:     return POKETYPE_WATER;
    case BW_BALL_TYPE_2:   return POKETYPE_BUG;
    case BW_BALL_LEVEL:    return 30;           /* Nest Ball: below level 30 */
    case BW_BALL_TURNS:    return 1;            /* Quick Ball: the first turn */
    }
    return 0;
}

int bw_ball_catch_value(int item, int which)
{
    const Ball *b = item > 0 ? line_of_item((u32)item) : NULL;
    int base = b != NULL ? (int)b->base : item;

    if (b != NULL) {
        switch (which) {
        case BW_BALL_RATIO:    if (b->has & KEY_RATIO) return b->ratio; break;
        case BW_BALL_PER_TURN: if (b->has & KEY_PER_TURN) return b->perTurn; break;
        case BW_BALL_TYPE:     if (b->has & KEY_TYPE) return b->type[0]; break;
        case BW_BALL_TYPE_2:   if (b->has & KEY_TYPE_2) return b->type[1]; break;
        case BW_BALL_LEVEL:    if (b->has & KEY_LEVEL) return b->level; break;
        case BW_BALL_TURNS:    if (b->has & KEY_TURNS) return b->turns; break;
        }
    }
    return game_value(base, which);
}

/* The base of a ball id: its line's, the game ball's own item, or 0. */
static int base_of_id(int ballId, const Ball **line)
{
    const Ball *b = ballId > 0 ? line_of_id((u32)ballId) : NULL;

    *line = b;
    if (b != NULL) {
        return b->base;
    }
    if (ballId > 0 && (u32)ballId <= BW_BALL_GAME_LAST) {
        return (int)game_lookup((u32)ballId, 1);
    }
    return 0;
}

int bw_ball_heals(int ballId)
{
    const Ball *b;
    int base = base_of_id(ballId, &b);

    if (b != NULL && (b->has & KEY_HEAL)) {
        return b->heal;
    }
    return base == ITEM_HEAL_BALL;
}

int bw_ball_catch_friendship(int ballId)
{
    const Ball *b;

    (void)base_of_id(ballId, &b);
    if (b != NULL && (b->has & KEY_FRIENDSHIP)) {
        return b->friendship;
    }
    return -1;
}

int bw_ball_friendship_bonus(int ballId)
{
    const Ball *b;
    int base = base_of_id(ballId, &b);

    if (b != NULL && (b->has & KEY_BONUS)) {
        return b->bonus;
    }
    return base == ITEM_LUXURY_BALL ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The game's functions                                                */
/* ------------------------------------------------------------------ */

/* arm9 palanm.c: column 0 looks up by item, column 1 by ball id; 0 when
 * not found.  The game's pairs first, then the table's. */
u16 ITEM_LookupBallIdPair(int value, int column)
{
    if (column == 0) {
        return (u16)bw_ball_id_of_item((u32)value);
    }
    return (u16)bw_ball_item_of_id((u32)value);
}

/* Ball id (Pokemon value 0x98) for a ball item. */
unsigned short ITEM_GetBallIdForItem(int value)
{
    return ITEM_LookupBallIdPair(value, 0);
}

/* arm9 app_icon.c: the ball icon's palette (a/0/8/3 0x56 + id) and
 * character file (0x6F + id).  Ball id 0 is shown as ball 4. */
int APP_ICON_GetBallThrowCellFileId(int ballId)
{
    int own = bw_ball_icon(ballId, 0);

    if (own >= 0) {
        return own;
    }
    ballId = bw_ball_shown(ballId);
    if (ballId == 0) {
        ballId = 4;
    }
    return ballId + 0x56;
}

int APP_ICON_GetBallThrowAnimFileId(int ballId)
{
    int own = bw_ball_icon(ballId, 1);

    if (own >= 0) {
        return own;
    }
    ballId = bw_ball_shown(ballId);
    if (ballId == 0) {
        ballId = 4;
    }
    return ballId + 0x6f;
}

/* arm9 natsuki.c */
struct PokemonParam;

/* (the accessors' last argument, PokeToolArg, is pointer wide on a host) */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, uintptr_t arg) GAME_FN;
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId, uintptr_t value) GAME_FN;
extern u32 FILE_UTIL_GetItemParam(u32 item, u32 sel, u32 heap) GAME_FN;
extern u32 ZONEDATA_GetPlaceNameId(u32 id) GAME_FN;
extern int GPOWER_ApplyFriendshipBonus(int friendship) GAME_FN;

/*
 * A friendship gain: the delta of the Pokemon's band (below 100, below 200,
 * from 200), one more in the Luxury Ball (the table's friendship_bonus), one
 * more where it was met, half again with the Soothe Bell (hold effect
 * 0x35), then Pass Power.  The game's own, with the ball's bonus from the
 * table.
 */
void NATSUKI_Apply(struct PokemonParam *pp, const s8 *deltas, int zoneId, u16 heap)
{
    int placeId;
    u32 species;
    u32 holdEffect;
    int friendship;
    int delta;
    u8 band;

    placeId = (int)ZONEDATA_GetPlaceNameId((u32)zoneId);
    band = 0;
    species = (u16)POKE_TOOL_GetPartyValue(pp, 0xAB, 0);
    if (species == 0 || species == 650) {
        return;
    }
    holdEffect = (u8)FILE_UTIL_GetItemParam((u16)POKE_TOOL_GetPartyValue(pp, 6, 0), 1, heap);
    friendship = (int)POKE_TOOL_GetPartyValue(pp, 9, 0);

    if (friendship >= 100) {
        band++;
    }
    if (friendship >= 200) {
        band++;
    }
    delta = deltas[band];
    if (delta > 0) {
        delta = delta + bw_ball_friendship_bonus((int)POKE_TOOL_GetPartyValue(pp, 0x98, 0));
    }
    if (placeId != 0 && delta > 0) {
        if (placeId == (int)POKE_TOOL_GetPartyValue(pp, 0x96, 0)) {
            delta = delta + 1;
        }
    }
    if (delta > 0 && holdEffect == 0x35) {
        delta = delta * 150 / 100;
    }
    friendship = friendship + GPOWER_ApplyFriendshipBonus(delta);
    if (friendship < 0) {
        friendship = 0;
    }
    if (friendship > 255) {
        friendship = 255;
    }
    POKE_TOOL_SetPartyValue(pp, 9, (u32)friendship);
}
