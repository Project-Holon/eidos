/*
 * sdk/save_tidy.c -- a saves folder put in order (save_tidy.h).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "save_tidy.h"

#define PATH_MAX_LEN 600
#define LINE_MAX_LEN 256

/* ------------------------------------------------------------------ */
/* Whether an image holds a save                                       */
/* ------------------------------------------------------------------ */

/*
 * The backup image as the game writes it (aletheia src/game/arm9/
 * backup_system.c, savedata.c, save_control.c; the launcher reads it the
 * same way, morphe crates/nds/src/save.rs): two copies of the save data,
 * at 0 and 0x24000, each 0x23F9C bytes.  A copy ends with the table of its
 * blocks' CRCs at 0x23F00 and a footer in its last 0x10 bytes: u32 save
 * count, u32 data size (0x23F9C), u32 id, u16 pad, u16 CRC of the table.
 * A block is its payload, then u16 count and u16 CRC.  The system block
 * (id 0x1B at 0x19400, 0x68 bytes) has the active flag at 0x2C: set while
 * a new game runs, cleared by its first save.
 */
#define COPY_SIZE    0x23F9Cu
#define SECOND_COPY  0x24000u
#define CRC_TABLE    0x23F00u
#define FOOTER       (COPY_SIZE - 0x10u)
#define SYSTEM_ID    0x1Bu
#define SYSTEM_AT    0x19400u
#define SYSTEM_SIZE  0x68u
#define SYSTEM_ACTIVE 0x2Cu

/* CRC-16/CCITT from 0xFFFF (the game's MATH_CalcCRC16CCITT). */
static unsigned crc16(const u8 *p, size_t n)
{
    unsigned c = 0xFFFFu;
    size_t i;
    int b;

    for (i = 0; i < n; i++) {
        c ^= (unsigned)p[i] << 8;
        for (b = 0; b < 8; b++) {
            c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) : (c << 1);
        }
        c &= 0xFFFFu;
    }
    return c;
}

static unsigned le16(const u8 *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int copy_holds(const u8 *c)
{
    return le32(c + FOOTER + 4) == COPY_SIZE && le16(c + FOOTER + 0xE) == crc16(c + CRC_TABLE, FOOTER - CRC_TABLE);
}

static int system_holds(const u8 *c)
{
    unsigned want = le16(c + CRC_TABLE + SYSTEM_ID * 2);

    return le16(c + SYSTEM_AT + SYSTEM_SIZE + 2) == want && crc16(c + SYSTEM_AT, SYSTEM_SIZE) == want;
}

int bw_save_bytes_loadable(const u8 *image, size_t size)
{
    const u8 *copies[2];
    int n = 0, i;

    if (image == NULL || size < SECOND_COPY + COPY_SIZE) {
        return 0;
    }
    if (copy_holds(image)) {
        copies[n++] = image;
    }
    if (copy_holds(image + SECOND_COPY)) {
        copies[n++] = image + SECOND_COPY;
    }
    if (n == 0) {
        return 0;                       /* no copy the game takes */
    }
    if (n == 2 && le32(copies[1] + FOOTER) > le32(copies[0] + FOOTER)) {
        const u8 *t = copies[0];        /* the newer copy first */

        copies[0] = copies[1];
        copies[1] = t;
    }
    for (i = 0; i < n; i++) {
        if (system_holds(copies[i])) {
            return copies[i][SYSTEM_AT + SYSTEM_ACTIVE] == 0;
        }
    }
    return 1;                           /* not sure: it stays a save */
}

int bw_save_image_loadable(const char *path)
{
    FILE *f;
    u8 *buf;
    long size;
    int r;

    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if ((unsigned long)size < SECOND_COPY + COPY_SIZE) {
        fclose(f);
        return 0;                       /* empty or cut short: no save */
    }
    buf = (u8 *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    r = fread(buf, 1, (size_t)size, f) == (size_t)size ? bw_save_bytes_loadable(buf, (size_t)size) : -1;
    fclose(f);
    free(buf);
    return r;
}

/* ------------------------------------------------------------------ */
/* The folder                                                          */
/* ------------------------------------------------------------------ */

static void say(char *note, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (note != NULL && n > 0) {
        va_start(ap, fmt);
        vsnprintf(note, n, fmt, ap);
        va_end(ap);
    }
}

static int join(char *out, size_t n, const char *dir, const char *name)
{
    int w = snprintf(out, n, "%s/%s", dir, name);

    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

/* A slot's file, relative to the folder: slot 1 is pokeblack.sav, slot n
 * slots/save<n>.sav (save_slots.c). */
static void slot_rel(int n, char *out, size_t size)
{
    if (n == 1) {
        snprintf(out, size, "pokeblack.sav");
    } else {
        snprintf(out, size, "slots/save%d.sav", n);
    }
}

static int exists(const char *saves, const char *rel)
{
    char p[PATH_MAX_LEN];

    return join(p, sizeof p, saves, rel) == 0 && bw_plat_path_kind(p) != BW_PATH_NONE;
}

static long mtime_of(const char *saves, const char *rel)
{
    char p[PATH_MAX_LEN];
    struct stat st;

    return join(p, sizeof p, saves, rel) == 0 && stat(p, &st) == 0 ? (long)st.st_mtime : 0;
}

/* A growing list of ints. */
typedef struct Ints {
    int *v;
    int n, cap;
} Ints;

static int ints_add(Ints *l, int x)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        int *v = (int *)realloc(l->v, (size_t)cap * sizeof *v);

        if (v == NULL) {
            return -1;
        }
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = x;
    return 0;
}

static int ints_has(const Ints *l, int x)
{
    int i;

    for (i = 0; i < l->n; i++) {
        if (l->v[i] == x) {
            return 1;
        }
    }
    return 0;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;

    return (x > y) - (x < y);
}

/* "<prefix><n><suffix>" with nothing after: n, else 0. */
static int number_in(const char *name, const char *prefix, const char *suffix)
{
    size_t lp = strlen(prefix), ls = strlen(suffix), ln = strlen(name);
    const char *p;
    long v = 0;

    if (ln <= lp + ls || strncmp(name, prefix, lp) != 0 || strcmp(name + ln - ls, suffix) != 0) {
        return 0;
    }
    for (p = name + lp; p < name + ln - ls; p++) {
        if (*p < '0' || *p > '9' || v > 100000000L) {
            return 0;
        }
        v = v * 10 + (*p - '0');
    }
    return (name[lp] == '0') ? 0 : (int)v;
}

typedef struct Scan {
    Ints *list;
    const char *prefix, *suffix;
} Scan;

static int scan_entry(void *ctx, const char *name)
{
    Scan *s = (Scan *)ctx;
    int n = number_in(name, s->prefix, s->suffix);

    if (n > 0) {
        (void)ints_add(s->list, n);
    }
    return 0;
}

/* The numbers of the files "<prefix><n><suffix>" in `dir` (relative). */
static void scan_numbers(const char *saves, const char *dir, const char *prefix, const char *suffix, Ints *out)
{
    char p[PATH_MAX_LEN];
    Scan s;

    s.list = out;
    s.prefix = prefix;
    s.suffix = suffix;
    if (join(p, sizeof p, saves, dir) == 0 && bw_plat_path_kind(p) == BW_PATH_DIR) {
        (void)bw_plat_list_dir(p, scan_entry, &s);
    }
}

/* ------------------------------------------------------------------ */
/* The plan                                                            */
/* ------------------------------------------------------------------ */

typedef struct Move {
    char from[200], to[200];
} Move;

typedef struct Plan {
    Move *moves;
    int nmoves, cap;
    char *games;            /* the lines games/games.ini ends with ("" none new) */
    size_t games_len, games_cap;
    int slot;               /* slot.txt's new number; 0: as it is */
    int retire;             /* games.ini to games.ini.moved */
    char retire_to[96];
    int to_pool, set_aside, renumbered;
} Plan;

static int plan_move(Plan *p, const char *from, const char *to)
{
    if (p->nmoves == p->cap) {
        int cap = p->cap ? p->cap * 2 : 16;
        Move *m = (Move *)realloc(p->moves, (size_t)cap * sizeof *m);

        if (m == NULL) {
            return -1;
        }
        p->moves = m;
        p->cap = cap;
    }
    snprintf(p->moves[p->nmoves].from, sizeof p->moves[0].from, "%s", from);
    snprintf(p->moves[p->nmoves].to, sizeof p->moves[0].to, "%s", to);
    p->nmoves++;
    return 0;
}

static int plan_games_line(Plan *p, const char *line)
{
    size_t n = strlen(line);

    if (p->games_len + n + 2 > p->games_cap) {
        size_t cap = (p->games_cap ? p->games_cap * 2 : 1024) + n;
        char *g = (char *)realloc(p->games, cap);

        if (g == NULL) {
            return -1;
        }
        p->games = g;
        p->games_cap = cap;
    }
    memcpy(p->games + p->games_len, line, n);
    p->games_len += n;
    p->games[p->games_len++] = '\n';
    p->games[p->games_len] = '\0';
    return 0;
}

static void plan_free(Plan *p)
{
    free(p->moves);
    free(p->games);
    memset(p, 0, sizeof *p);
}

/* A name in `dir` that is free on the disk and in the plan: "<base><ext>",
 * else "<base>-2<ext>", "<base>-3<ext>" and on. */
static void free_name(const char *saves, const Plan *p, const char *dir, const char *base, const char *ext,
                      char *out, size_t size)
{
    int k, i, taken;

    for (k = 1;; k++) {
        if (k == 1) {
            snprintf(out, size, "%s/%s%s", dir, base, ext);
        } else {
            snprintf(out, size, "%s/%s-%d%s", dir, base, k, ext);
        }
        taken = exists(saves, out);
        for (i = 0; !taken && i < p->nmoves; i++) {
            taken = strcmp(p->moves[i].to, out) == 0;
        }
        if (!taken) {
            return;
        }
    }
}

static int read_slot_txt(const char *saves)
{
    char path[PATH_MAX_LEN];
    FILE *f;
    int n = 1;

    if (join(path, sizeof path, saves, "slot.txt") == 0 && (f = fopen(path, "rb")) != NULL) {
        if (fscanf(f, "%d", &n) != 1 || n == 0) {
            n = 1;
        }
        fclose(f);
    }
    return n;
}

/* The lines of a games.ini: (slot, the rest of the line after the slot). */
typedef struct Rec {
    int slot;
    char rest[LINE_MAX_LEN];
} Rec;

static int read_records(const char *saves, const char *rel, Rec **out, int *count)
{
    char path[PATH_MAX_LEN], line[LINE_MAX_LEN];
    FILE *f;
    int cap = 0;

    *out = NULL;
    *count = 0;
    if (join(path, sizeof path, saves, rel) != 0 || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *rest = line;
        long n;

        if (line[0] == '#') {
            continue;
        }
        n = strtol(line, &rest, 10);
        if (rest == line || n < 1 || n > 100000000L) {
            continue;
        }
        rest[strcspn(rest, "\r\n")] = '\0';
        if (*count == cap) {
            Rec *r;

            cap = cap ? cap * 2 : 16;
            r = (Rec *)realloc(*out, (size_t)cap * sizeof *r);
            if (r == NULL) {
                fclose(f);
                return -1;
            }
            *out = r;
        }
        (*out)[*count].slot = (int)n;
        snprintf((*out)[*count].rest, sizeof (*out)[0].rest, "%s", rest);
        (*count)++;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The saves' per-save mod files                                       */
/* ------------------------------------------------------------------ */

int bw_save_state_name(const char *rel, char *out, size_t size)
{
    size_t n = strlen(rel), i;

    if (n > 4 && strcmp(rel + n - 4, ".sav") == 0) {
        n -= 4;
    }
    if (n == 0 || n + 1 > size || n >= 64) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        char c = rel[i];

        out[i] = (char)(c == '/' || c == '\\' ? '-'
                        : (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                              || c == '_' ? c : '_');
    }
    out[n] = '\0';
    return 0;
}

/*
 * The per-save layer keeps a mod's data for a save in
 * mods/<namespace>/<the save's name>.bin (and files it moved aside,
 * <name>.bin.<suffix>).  A save that moves takes its files along: each
 * move of a save is followed in the plan by the moves of its files, and a
 * file already at a new name (left by a save that is gone) is moved aside
 * first, to aside-<name>, never written over.
 *
 * Until the saves named their files, every save used "pokeblack": such a
 * file goes to the save it was written with, when that is one save other
 * than pokeblack.sav (its header's save_check is that save's checksum,
 * the layer's rule: the CRC-32 of the chip's 512 KB with the save's bytes
 * first).  A file of no other save stays pokeblack.sav's.
 */
typedef struct StateFile {
    char ns[72], name[128];
} StateFile;

typedef struct StateFiles {
    StateFile *v;
    int n, cap;
    const char *ns;             /* (the listing: the folder being read) */
} StateFiles;

static int state_file_add(StateFiles *l, const char *ns, const char *name)
{
    if (strlen(ns) >= sizeof l->v[0].ns || strlen(name) >= sizeof l->v[0].name) {
        return 0;               /* (no name the layer writes) */
    }
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        StateFile *v = (StateFile *)realloc(l->v, (size_t)cap * sizeof *v);

        if (v == NULL) {
            return -1;
        }
        l->v = v;
        l->cap = cap;
    }
    snprintf(l->v[l->n].ns, sizeof l->v[0].ns, "%s", ns);
    snprintf(l->v[l->n].name, sizeof l->v[0].name, "%s", name);
    l->n++;
    return 0;
}

static int list_state_file(void *ctx, const char *name)
{
    StateFiles *l = (StateFiles *)ctx;

    if (strstr(name, ".bin") != NULL) {
        (void)state_file_add(l, l->ns, name);
    }
    return 0;
}

typedef struct StateDirs {
    const char *saves;
    StateFiles *files;
} StateDirs;

static int list_state_dir(void *ctx, const char *name)
{
    StateDirs *d = (StateDirs *)ctx;
    char p[PATH_MAX_LEN];

    if (name[0] == '.' || snprintf(p, sizeof p, "%s/mods/%s", d->saves, name) >= (int)sizeof p
        || bw_plat_path_kind(p) != BW_PATH_DIR) {
        return 0;
    }
    d->files->ns = name;
    (void)bw_plat_list_dir(p, list_state_file, d->files);
    return 0;
}

static int state_file_at(const StateFiles *l, const char *ns, const char *name)
{
    int i;

    for (i = 0; i < l->n; i++) {
        if (strcmp(l->v[i].ns, ns) == 0 && strcmp(l->v[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* One file's move in the plan, and in the list that stands for the folder
 * as the plan leaves it. */
static int plan_state_move(Plan *p, StateFiles *l, int i, const char *to_name)
{
    char from[200], to[200];

    if (snprintf(from, sizeof from, "mods/%s/%s", l->v[i].ns, l->v[i].name) >= (int)sizeof from
        || snprintf(to, sizeof to, "mods/%s/%s", l->v[i].ns, to_name) >= (int)sizeof to
        || strlen(to_name) >= sizeof l->v[i].name) {
        return 0;               /* (a name too long to be one of the layer's) */
    }
    if (plan_move(p, from, to) != 0) {
        return -1;
    }
    snprintf(l->v[i].name, sizeof l->v[i].name, "%s", to_name);
    return 0;
}

/* The files of the save named `from` (stem) to the save named `to`. */
static int plan_state_follow(Plan *p, StateFiles *l, const char *from, const char *to)
{
    size_t lf = strlen(from);
    int i;

    for (i = 0; i < l->n; i++) {
        const char *rest = l->v[i].name + lf;
        char want[160];
        int j;

        if (strncmp(l->v[i].name, from, lf) != 0 || strncmp(rest, ".bin", 4) != 0 || (rest[4] != '\0' && rest[4] != '.')
            || snprintf(want, sizeof want, "%s%s", to, rest) >= (int)sizeof want) {
            continue;
        }
        if ((j = state_file_at(l, l->v[i].ns, want)) >= 0) {
            char aside[200];
            int k;

            for (k = 1;; k++) {             /* a file a gone save left there: aside, not written over */
                if (k == 1) {
                    snprintf(aside, sizeof aside, "aside-%s", want);
                } else {
                    snprintf(aside, sizeof aside, "aside-%d-%s", k, want);
                }
                if (state_file_at(l, l->v[j].ns, aside) < 0) {
                    break;
                }
            }
            if (plan_state_move(p, l, j, aside) != 0) {
                return -1;
            }
        }
        if (plan_state_move(p, l, i, want) != 0) {
            return -1;
        }
    }
    return 0;
}

extern u32 bw_mod_savedata_crc(const void *data, u32 size)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((weak))
#endif
    ;

#define CHIP_SIZE 0x80000u

/* The layer's checksum of a save file (its save_check), or 0 when it
 * cannot be read. */
static u32 save_file_check(const char *saves, const char *rel)
{
    char path[PATH_MAX_LEN];
    u8 *chip;
    FILE *f;
    u32 crc = 0;

    if (bw_mod_savedata_crc == NULL || join(path, sizeof path, saves, rel) != 0 || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if ((chip = (u8 *)malloc(CHIP_SIZE)) != NULL) {
        size_t got = fread(chip, 1, CHIP_SIZE, f);

        memset(chip + got, 0xFF, CHIP_SIZE - got);
        crc = got > 0 ? bw_mod_savedata_crc(chip, CHIP_SIZE) : 0;
        free(chip);
    }
    fclose(f);
    return crc;
}

/* The header's save_check of a state file (mods/<ns>/<name>), 0 when there
 * is none. */
static u32 state_file_check(const char *saves, const char *ns, const char *name)
{
    char path[PATH_MAX_LEN];
    u8 head[24];
    FILE *f;
    u32 check = 0;

    if (snprintf(path, sizeof path, "%s/mods/%s/%s", saves, ns, name) >= (int)sizeof path
        || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if (fread(head, 1, sizeof head, f) == sizeof head && le32(head) == 0x534D5742u) {  /* "BWMS", BW_MOD_STATE_MAGIC */
        check = le32(head + 20);
    }
    fclose(f);
    return check;
}

static int plan_state_files(const char *saves, Plan *p, const Ints *solo, const Ints *pool)
{
    StateFiles l = {0};
    StateDirs d;
    char path[PATH_MAX_LEN], from[160], to[160];
    int i, nsaves = solo->n + pool->n, m, nmoves = p->nmoves;
    u32 *checks = NULL;

    if (join(path, sizeof path, saves, "mods") != 0 || bw_plat_path_kind(path) != BW_PATH_DIR) {
        return 0;
    }
    d.saves = saves;
    d.files = &l;
    (void)bw_plat_list_dir(path, list_state_dir, &d);
    if (l.n == 0) {
        free(l.v);
        return 0;
    }
    /* the files all saves shared: to the save they were written with */
    for (i = 0; i < l.n; i++) {
        u32 want;
        int k, found = -1, many = 0;

        if (strcmp(l.v[i].name, "pokeblack.bin") != 0 || (want = state_file_check(saves, l.v[i].ns, "pokeblack.bin")) == 0) {
            continue;
        }
        if (checks == NULL) {
            if ((checks = (u32 *)calloc((size_t)(nsaves > 0 ? nsaves : 1), sizeof *checks)) == NULL) {
                free(l.v);
                return -1;
            }
            for (k = 0; k < nsaves; k++) {
                char rel[96];

                if (k < solo->n) {
                    slot_rel(solo->v[k], rel, sizeof rel);
                } else {
                    snprintf(rel, sizeof rel, "games/game%d.sav", pool->v[k - solo->n]);
                }
                checks[k] = save_file_check(saves, rel);
            }
        }
        for (k = 0; k < nsaves; k++) {
            if (checks[k] == want) {
                many += found >= 0;
                found = k;
            }
        }
        if (found < 0 || many > 0 || (found < solo->n && solo->v[found] == 1)) {
            continue;           /* pokeblack.sav's, or no single save's: it stays */
        }
        if (found < solo->n) {
            slot_rel(solo->v[found], from, sizeof from);
        } else {
            snprintf(from, sizeof from, "games/game%d.sav", pool->v[found - solo->n]);
        }
        if (bw_save_state_name(from, to, sizeof to) == 0 && state_file_at(&l, l.v[i].ns, strcat(to, ".bin")) < 0) {
            /* (first in the plan: the saves' own moves then take it along) */
            if (plan_state_move(p, &l, i, to) != 0) {
                free(checks);
                free(l.v);
                return -1;
            }
        }
    }
    free(checks);
    /* the moves just added go first: they name the files by the saves'
     * names before any save moves */
    if (p->nmoves > nmoves) {
        int added = p->nmoves - nmoves;
        Move *tmp = (Move *)malloc((size_t)added * sizeof *tmp);

        if (tmp == NULL) {
            free(l.v);
            return -1;
        }
        memcpy(tmp, p->moves + nmoves, (size_t)added * sizeof *tmp);
        memmove(p->moves + added, p->moves, (size_t)nmoves * sizeof *tmp);
        memcpy(p->moves, tmp, (size_t)added * sizeof *tmp);
        free(tmp);
    }
    /* each save's own move, then its files' (in the plan's order) */
    {
        Plan out = *p;
        Move *all = p->moves;
        int total = p->nmoves;

        out.moves = NULL;
        out.nmoves = out.cap = 0;
        for (m = 0; m < total; m++) {
            if (plan_move(&out, all[m].from, all[m].to) != 0) {
                free(out.moves);
                free(l.v);
                return -1;
            }
            if (strncmp(all[m].from, "mods/", 5) == 0) {
                continue;
            }
            if (bw_save_state_name(all[m].from, from, sizeof from) == 0
                && bw_save_state_name(all[m].to, to, sizeof to) == 0
                && plan_state_follow(&out, &l, from, to) != 0) {
                free(out.moves);
                free(l.v);
                return -1;
            }
        }
        free(p->moves);
        p->moves = out.moves;
        p->nmoves = out.nmoves;
        p->cap = out.cap;
    }
    free(l.v);
    return 0;
}

/* The plan for the folder as it is now: 0 made (maybe empty), -1 failed. */
static int make_plan(const char *saves, Plan *p)
{
    Ints solo = {0}, pool = {0}, keep = {0}, gone = {0};
    Rec *old = NULL, *have = NULL;
    int nold = 0, nhave = 0, i, k_next = 0, cur, cur_to = 0, newest = 0;
    long newest_t = -1;
    char rel[96], to[96], line[LINE_MAX_LEN + 16];

    memset(p, 0, sizeof *p);
    /* the slots that hold a file */
    scan_numbers(saves, "slots", "save", ".sav", &solo);
    for (i = 0; i < solo.n; i++) {
        if (solo.v[i] == 1) {
            solo.v[i] = solo.v[--solo.n];   /* (slots/save1.sav is not slot 1's) */
            i--;
        }
    }
    if (exists(saves, "pokeblack.sav")) {
        (void)ints_add(&solo, 1);
    }
    qsort(solo.v, (size_t)solo.n, sizeof *solo.v, cmp_int);
    /* the games already in games/ and their lines */
    scan_numbers(saves, "games", "game", ".sav", &pool);
    if (read_records(saves, "games/games.ini", &have, &nhave) != 0
        || read_records(saves, "games.ini", &old, &nold) != 0) {
        goto fail;
    }
    for (i = 0; i < pool.n; i++) {
        k_next = pool.v[i] > k_next ? pool.v[i] : k_next;
    }
    for (i = 0; i < nhave; i++) {
        k_next = have[i].slot > k_next ? have[i].slot : k_next;
        snprintf(line, sizeof line, "%d%s", have[i].slot, have[i].rest);
        if (plan_games_line(p, line) != 0) {
            goto fail;
        }
    }
    cur = read_slot_txt(saves);
    /* 1. an older eidos's Multiplayer screen games: to games/ */
    for (i = 0; i < nold; i++) {
        int n = old[i].slot;

        if (!ints_has(&solo, n) || ints_has(&gone, n)) {
            continue;                   /* its save is not there: the line goes */
        }
        k_next++;
        slot_rel(n, rel, sizeof rel);
        snprintf(to, sizeof to, "games/game%d.sav", k_next);
        snprintf(line, sizeof line, "%d%s", k_next, old[i].rest);
        if (plan_move(p, rel, to) != 0 || plan_games_line(p, line) != 0 || ints_add(&gone, n) != 0) {
            goto fail;
        }
        if (n == cur) {
            cur_to = -k_next;
        }
        p->to_pool++;
    }
    if (nold > 0 || exists(saves, "games.ini")) {
        p->retire = 1;
        free_name(saves, p, ".", "games", ".ini.moved", p->retire_to, sizeof p->retire_to);
        memmove(p->retire_to, p->retire_to + 2, strlen(p->retire_to + 2) + 1);     /* (no "./") */
    }
    /* 2. the files that hold no save: to unreadable/ */
    for (i = 0; i < solo.n; i++) {
        int n = solo.v[i];
        char abs_path[PATH_MAX_LEN], base[32];

        if (ints_has(&gone, n)) {
            continue;
        }
        slot_rel(n, rel, sizeof rel);
        if (join(abs_path, sizeof abs_path, saves, rel) != 0) {
            goto fail;
        }
        if (bw_save_image_loadable(abs_path) != 0) {
            (void)ints_add(&keep, n);   /* a save, or a file not read: it stays */
            continue;
        }
        snprintf(base, sizeof base, "slot%d", n);
        free_name(saves, p, "unreadable", base, ".sav", to, sizeof to);
        if (plan_move(p, rel, to) != 0 || ints_add(&gone, n) != 0) {
            goto fail;
        }
        p->set_aside++;
    }
    /* 3. the saves left close up in their order */
    for (i = 0; i < keep.n; i++) {
        int n = keep.v[i], t = i + 1;
        long mt;

        slot_rel(n, rel, sizeof rel);
        mt = mtime_of(saves, rel);
        if (mt > newest_t) {
            newest_t = mt;
            newest = t;
        }
        if (n == cur) {
            cur_to = t;
        }
        if (t == n) {
            continue;
        }
        slot_rel(t, to, sizeof to);
        if (plan_move(p, rel, to) != 0) {
            goto fail;
        }
        p->renumbered++;
    }
    /* slot.txt: the save it named, where it went */
    if (cur_to != 0) {
        p->slot = cur_to;
    } else if (ints_has(&gone, cur)) {
        p->slot = newest != 0 ? newest : 1;     /* its file was set aside */
    } else if (cur > 0 && !ints_has(&solo, cur) && p->nmoves > 0) {
        p->slot = keep.n + 1;           /* a free slot stays a free slot */
    }
    if (p->slot == cur) {
        p->slot = 0;
    }
    if (plan_state_files(saves, p, &solo, &pool) != 0) {
        goto fail;
    }
    free(solo.v);
    free(pool.v);
    free(keep.v);
    free(gone.v);
    free(old);
    free(have);
    return 0;
fail:
    free(solo.v);
    free(pool.v);
    free(keep.v);
    free(gone.v);
    free(old);
    free(have);
    plan_free(p);
    return -1;
}

static int plan_empty(const Plan *p)
{
    return p->nmoves == 0 && p->slot == 0 && !p->retire;
}

/* tidy.txt: the plan, one step a line, "end" last. */
static int plan_write(const char *saves, const Plan *p)
{
    char path[PATH_MAX_LEN];
    size_t cap = 256 + (size_t)p->nmoves * 420 + p->games_len * 2, n = 0;
    char *text = (char *)malloc(cap);
    const char *g;
    int i, r;

    if (text == NULL || join(path, sizeof path, saves, "tidy.txt") != 0) {
        free(text);
        return -1;
    }
    n += (size_t)snprintf(text + n, cap - n, "# the saves folder put in order (sdk/save_tidy.h)\n");
    for (i = 0; i < p->nmoves; i++) {
        n += (size_t)snprintf(text + n, cap - n, "move %s %s\n", p->moves[i].from, p->moves[i].to);
    }
    for (g = p->games; g != NULL && *g != '\0';) {
        size_t len = strcspn(g, "\n");

        n += (size_t)snprintf(text + n, cap - n, "games %.*s\n", (int)len, g);
        g += len + (g[len] == '\n');
    }
    if (p->slot != 0) {
        n += (size_t)snprintf(text + n, cap - n, "slot %d\n", p->slot);
    }
    if (p->retire) {
        n += (size_t)snprintf(text + n, cap - n, "retire %s\n", p->retire_to);
    }
    n += (size_t)snprintf(text + n, cap - n, "counts %d %d %d\nend\n", p->to_pool, p->set_aside, p->renumbered);
    r = bw_save_write_atomic(path, text, (u32)n);
    free(text);
    return r;
}

/* The plan back from tidy.txt: 1 read, 0 none there, -1 not whole. */
static int plan_read(const char *saves, Plan *p)
{
    char path[PATH_MAX_LEN], line[LINE_MAX_LEN + 420];
    FILE *f;
    int ended = 0;

    memset(p, 0, sizeof *p);
    if (join(path, sizeof path, saves, "tidy.txt") != 0 || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char a[200], b[200];

        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "move ", 5) == 0 && sscanf(line + 5, "%199s %199s", a, b) == 2) {
            (void)plan_move(p, a, b);
        } else if (strncmp(line, "games ", 6) == 0) {
            (void)plan_games_line(p, line + 6);
        } else if (strncmp(line, "slot ", 5) == 0) {
            p->slot = atoi(line + 5);
        } else if (strncmp(line, "retire ", 7) == 0) {
            p->retire = 1;
            snprintf(p->retire_to, sizeof p->retire_to, "%s", line + 7);
        } else if (strncmp(line, "counts ", 7) == 0) {
            (void)sscanf(line + 7, "%d %d %d", &p->to_pool, &p->set_aside, &p->renumbered);
        } else if (strcmp(line, "end") == 0) {
            ended = 1;
        }
    }
    fclose(f);
    if (!ended) {
        plan_free(p);
        return -1;
    }
    return 1;
}

/* The folder a relative file is in, made. */
static int make_parent(const char *saves, const char *rel)
{
    char dir[PATH_MAX_LEN];
    const char *slash = strrchr(rel, '/');

    if (slash == NULL) {
        return 0;
    }
    if (snprintf(dir, sizeof dir, "%s/%.*s", saves, (int)(slash - rel), rel) >= (int)sizeof dir) {
        return -1;
    }
    return bw_plat_mkdirs(dir);
}

/* One move: done already when the file is at its new name and gone from
 * the old.  A file at the new name with the old one still there is never
 * written over: the step fails. */
static int do_move(const char *saves, const Move *m, char *note, size_t note_size)
{
    char from[PATH_MAX_LEN], to[PATH_MAX_LEN];
    int have_from, have_to;

    if (join(from, sizeof from, saves, m->from) != 0 || join(to, sizeof to, saves, m->to) != 0) {
        return -1;
    }
    have_from = bw_plat_path_kind(from) == BW_PATH_FILE;
    have_to = bw_plat_path_kind(to) != BW_PATH_NONE;
    if (!have_from) {
        return 0;                       /* moved before, or gone: nothing to do */
    }
    if (have_to) {
        say(note, note_size, "The saves were not put in order: %s is there already, and %s stays.", m->to, m->from);
        return -1;
    }
    if (make_parent(saves, m->to) != 0) {
        say(note, note_size, "The saves were not put in order: the folder of %s could not be made.", m->to);
        return -1;
    }
    /* (the move itself refuses a file that came at the new name since the
     * check above: never written over) */
    switch (bw_plat_move_new(from, to)) {
    case 0:
        return 0;
    case 1:
        say(note, note_size, "The saves were not put in order: %s is there already, and %s stays.", m->to, m->from);
        return -1;
    default:
        say(note, note_size, "The saves were not put in order: %s could not be moved to %s.", m->from, m->to);
        return -1;
    }
}

/*
 * tidy.step: how many of the plan's moves are done, written after each
 * one.  A name can come up twice in a plan (slot 2's file set aside, then
 * slot 3's save moved into slot 2), so a start that goes on must not judge
 * the done moves by the files: it goes on from the step.  A crash between
 * a move and its step leaves that one move to do again, and it is seen as
 * done (its file gone from the old name, at the new one).
 */
static int read_step(const char *saves)
{
    char path[PATH_MAX_LEN];
    FILE *f;
    int n = 0;

    if (join(path, sizeof path, saves, "tidy.step") == 0 && (f = fopen(path, "rb")) != NULL) {
        if (fscanf(f, "%d", &n) != 1 || n < 0) {
            n = 0;
        }
        fclose(f);
    }
    return n;
}

static int write_step(const char *saves, int n)
{
    char path[PATH_MAX_LEN], text[16];
    int w = snprintf(text, sizeof text, "%d\n", n);

    return join(path, sizeof path, saves, "tidy.step") == 0 ? bw_save_write_atomic(path, text, (u32)w) : -1;
}

static int plan_run(const char *saves, const Plan *p, char *note, size_t note_size)
{
    char path[PATH_MAX_LEN], to[PATH_MAX_LEN];
    /* EIDOS_TIDY_STOP_AFTER=n (docs/test-switches.md): a crash after n
     * moves, for the test that the next start goes on */
    const char *stop = getenv("EIDOS_TIDY_STOP_AFTER");
    int i, stop_after = stop != NULL && stop[0] != '\0' ? atoi(stop) : -1;

    for (i = read_step(saves); i < p->nmoves; i++) {
        if (i == stop_after) {
            say(note, note_size, "The saves were not put in order: stopped after %d moves (EIDOS_TIDY_STOP_AFTER).", i);
            return -1;
        }
        if (do_move(saves, &p->moves[i], note, note_size) != 0 || write_step(saves, i + 1) != 0) {
            return -1;
        }
    }
    if (p->games_len > 0) {
        size_t n = strlen("# the saves the Multiplayer screen made (sdk/save_games.h)\n");
        char *text = (char *)malloc(n + p->games_len + 1);
        int r;

        if (text == NULL || join(path, sizeof path, saves, "games") != 0 || bw_plat_mkdirs(path) != 0
            || join(path, sizeof path, saves, "games/games.ini") != 0) {
            free(text);
            return -1;
        }
        memcpy(text, "# the saves the Multiplayer screen made (sdk/save_games.h)\n", n);
        memcpy(text + n, p->games, p->games_len);
        r = bw_save_write_atomic(path, text, (u32)(n + p->games_len));
        free(text);
        if (r != 0) {
            return -1;
        }
    }
    if (p->slot != 0) {
        char text[16];
        int w = snprintf(text, sizeof text, "%d\n", p->slot);

        if (join(path, sizeof path, saves, "slot.txt") != 0 || bw_save_write_atomic(path, text, (u32)w) != 0) {
            return -1;
        }
    }
    if (p->retire && join(path, sizeof path, saves, "games.ini") == 0 && bw_plat_path_kind(path) == BW_PATH_FILE) {
        if (join(to, sizeof to, saves, p->retire_to) != 0 || bw_plat_move_new(path, to) != 0) {
            say(note, note_size, "The saves were not put in order: games.ini could not be kept as %s.", p->retire_to);
            return -1;
        }
    }
    return 0;
}

static int tidy_locked(const char *saves, char *note, size_t note_size)
{
    char path[PATH_MAX_LEN];
    Plan p;
    int r;

    r = plan_read(saves, &p);
    if (r < 0) {
        say(note, note_size, "The saves were not put in order: tidy.txt in %s is not whole.", saves);
        return -1;
    }
    if (r == 0) {
        if (make_plan(saves, &p) != 0) {
            say(note, note_size, "The saves in %s could not be read to put them in order.", saves);
            return -1;
        }
        if (plan_empty(&p)) {
            plan_free(&p);
            return 0;
        }
        /* (step 0 first: a step left by an older plan never counts for this one) */
        if (write_step(saves, 0) != 0 || plan_write(saves, &p) != 0) {
            plan_free(&p);
            say(note, note_size, "The saves were not put in order: tidy.txt could not be written in %s.", saves);
            return -1;
        }
    }
    if (plan_run(saves, &p, note, note_size) != 0) {
        plan_free(&p);
        if (note == NULL || note[0] == '\0') {
            say(note, note_size, "The saves in %s were not put in order; the next start goes on.", saves);
        }
        return -1;
    }
    if (join(path, sizeof path, saves, "tidy.txt") == 0) {
        (void)remove(path);
    }
    if (join(path, sizeof path, saves, "tidy.step") == 0) {
        (void)remove(path);         /* (only once the plan is gone) */
    }
    say(note, note_size,
        "The saves in %s were put in order: %d Multiplayer screen game(s) to games/, %d file(s) with no save "
        "set aside in unreadable/, %d save(s) numbered again.",
        saves, p.to_pool, p.set_aside, p.renumbered);
    plan_free(&p);
    return 1;
}

int bw_saves_tidy(const char *saves, char *note, size_t note_size)
{
    void *lock;
    int r;

    say(note, note_size, "%s", "");
    if (saves == NULL || saves[0] == '\0' || bw_plat_path_kind(saves) != BW_PATH_DIR) {
        return 0;
    }
    /* one process at a time: another eidos (or the launcher's) at work on
     * the folder finishes it, and this one leaves it be */
    lock = bw_plat_lock_folder(saves);
    if (lock == NULL) {
        return 0;
    }
    r = tidy_locked(saves, note, note_size);
    bw_plat_unlock_folder(lock);
    return r;
}
