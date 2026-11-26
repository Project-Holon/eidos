/*
 * install/profiles.c -- the two game profiles of a game started without
 * the launcher (profiles.h has the rule).
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profiles.h"

static void say(char *msg, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (msg != NULL && n > 0) {
        va_start(ap, fmt);
        vsnprintf(msg, n, fmt, ap);
        va_end(ap);
    }
}

static int join(char *out, size_t n, const char *dir, const char *name)
{
    int w = snprintf(out, n, "%s/%s", dir, name);

    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int bw_profile_dir(const char *user_dir, BwGame game, char *out, size_t n)
{
    int w;

    if (user_dir == NULL || user_dir[0] == '\0'
        || (game != BW_GAME_BLACK && game != BW_GAME_WHITE)) {
        return -1;
    }
    w = snprintf(out, n, "%s/profiles/%s", user_dir, game == BW_GAME_WHITE ? "White" : "Black");
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int bw_profile_has_saves(const char *saves_dir)
{
    char path[BW_PROFILE_PATH_MAX];

    if (saves_dir == NULL || saves_dir[0] == '\0') {
        return 0;
    }
    if (join(path, sizeof(path), saves_dir, "pokeblack.sav") == 0
        && bw_path_kind_utf8(path) == BW_PATH_FILE) {
        return 1;
    }
    if (join(path, sizeof(path), saves_dir, "slots") == 0
        && bw_path_kind_utf8(path) == BW_PATH_DIR && bw_dir_is_empty_utf8(path) == 0) {
        return 1;
    }
    /* the Multiplayer screen's games (sdk/save_games.h) */
    if (join(path, sizeof(path), saves_dir, "games") == 0
        && bw_path_kind_utf8(path) == BW_PATH_DIR && bw_dir_is_empty_utf8(path) == 0) {
        return 1;
    }
    if (join(path, sizeof(path), saves_dir, "multiplayer") == 0
        && bw_path_kind_utf8(path) == BW_PATH_DIR && bw_dir_is_empty_utf8(path) == 0) {
        return 1;
    }
    return 0;
}

/* One file to a place that has none. */
static int copy_one(const char *from, const char *to)
{
    char buf[4096];
    FILE *in, *out;
    size_t got;
    int bad = 0;

    if (bw_path_kind_utf8(from) != BW_PATH_FILE || bw_path_kind_utf8(to) != BW_PATH_NONE) {
        return 0;
    }
    in = bw_fopen_utf8(from, "rb");
    if (in == NULL) {
        return -1;
    }
    out = bw_fopen_utf8(to, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            bad = 1;
            break;
        }
    }
    bad |= ferror(in);
    fclose(in);
    if (fclose(out) != 0 || bad) {
        (void)bw_remove_tree_utf8(to);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The mod list of a copied folder                                     */
/* ------------------------------------------------------------------ */

/* Where the part of `path` below the folder `dir` starts, or NULL when the
 * path is not below it.  The two separators are one, and on Windows the
 * case of a letter does not count: the rule the game compares the folders
 * of its mods by (sdk/mod_menu.c, same_root). */
static const char *below(const char *path, const char *dir)
{
    size_t n = strlen(dir), i;

    while (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) {
        n--;
    }
    for (i = 0; i < n; i++) {
        int a = (unsigned char)path[i], b = (unsigned char)dir[i];

        a = a == '\\' ? '/' : a;
        b = b == '\\' ? '/' : b;
#ifdef _WIN32
        a = tolower(a);
        b = tolower(b);
#endif
        if (a != b) {
            return NULL;        /* (a path that ends first, too) */
        }
    }
    if (path[n] != '/' && path[n] != '\\') {
        return NULL;
    }
    while (path[n] == '/' || path[n] == '\\') {
        n++;
    }
    return path[n] != '\0' ? path + n : NULL;
}

/*
 * The game's record of the player's mods (<saves>/mods.ini) names each mod
 * by its folder.  The record was copied with the saves and the mods with
 * their folder, so a mod of the old mods folder is named by its copy from
 * here on.  Left as it was, the record would switch the old folder's mod
 * on and the game would add the copy as a second one.  A mod kept anywhere
 * else keeps its line.  0, or -1 when the record could not be written (the
 * copy is then taken away, and the game makes a new one).  A line of the
 * record is 1024 bytes at most (sdk/mod_menu.c, write_record).
 */
static int record_moved(const char *saves, const char *old_mods, const char *new_mods)
{
    char path[BW_PROFILE_PATH_MAX], fresh[BW_PROFILE_PATH_MAX], line[2048];
    FILE *in, *out;
    int changed = 0, bad;

    if (old_mods == NULL || join(path, sizeof(path), saves, "mods.ini") != 0
        || join(fresh, sizeof(fresh), saves, "mods.ini.new") != 0
        || bw_path_kind_utf8(path) != BW_PATH_FILE) {
        return 0;
    }
    in = bw_fopen_utf8(path, "rb");
    if (in == NULL) {
        return 0;
    }
    out = bw_fopen_utf8(fresh, "wb");
    if (out == NULL) {
        fclose(in);
        (void)bw_remove_tree_utf8(path);
        return -1;
    }
    while (fgets(line, sizeof(line), in) != NULL) {
        const char *key = line + strspn(line, " \t");
        const char *eq = strchr(key, '=');
        const char *rest = NULL;

        if (strncmp(key, "root", 4) == 0 && eq != NULL && key[4 + strspn(key + 4, " \t")] == '=') {
            rest = below(eq + 1 + strspn(eq + 1, " \t"), old_mods);
        }
        if (rest != NULL) {
            fprintf(out, "root = %s/%s", new_mods, rest);      /* (`rest` ends the line) */
            changed = 1;
        } else {
            fputs(line, out);
        }
    }
    bad = ferror(in);
    fclose(in);
    bad |= fclose(out) != 0;
    if (bad || !changed) {
        (void)bw_remove_tree_utf8(fresh);
        if (bad) {
            (void)bw_remove_tree_utf8(path);
        }
        return bad ? -1 : 0;
    }
    if (bw_remove_tree_utf8(path) != 0 || bw_rename_utf8(fresh, path) != 0) {
        (void)bw_remove_tree_utf8(fresh);
        (void)bw_remove_tree_utf8(path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The old saves beside a profile's own                                */
/* ------------------------------------------------------------------ */

/* The file of save slot `slot` (1 and up: there is no limit) in a saves
 * folder. */
static int slot_path(const char *saves, int slot, char *out, size_t n)
{
    int w = slot == 1 ? snprintf(out, n, "%s/pokeblack.sav", saves)
                      : snprintf(out, n, "%s/slots/save%d.sav", saves, slot);

    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

/* A slot is taken when a file is there, a save or not: a free slot has no
 * file, so nothing is ever written over (sdk/save_tidy.h sets aside the
 * files that hold no save). */
static int slot_taken(const char *saves, int slot)
{
    char path[BW_PROFILE_PATH_MAX];

    return slot_path(saves, slot, path, sizeof(path)) != 0 || bw_path_kind_utf8(path) != BW_PATH_NONE;
}

/* The slots of a folder, in order, by their files' names. */
typedef struct SlotList {
    int *v;
    int n, cap;
} SlotList;

static int slot_list_add(SlotList *l, int x)
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

static int slot_entry(void *ctx, const char *name, int kind)
{
    SlotList *l = (SlotList *)ctx;
    size_t len = strlen(name);
    long v = 0;
    size_t i;

    if (kind != BW_PATH_FILE || len < 9 || strncmp(name, "save", 4) != 0 || strcmp(name + len - 4, ".sav") != 0
        || name[4] == '0') {
        return 0;
    }
    for (i = 4; i < len - 4; i++) {
        if (name[i] < '0' || name[i] > '9' || v > 100000000L) {
            return 0;
        }
        v = v * 10 + (name[i] - '0');
    }
    if (v >= 2) {
        (void)slot_list_add(l, (int)v);
    }
    return 0;
}

static int cmp_slot(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;

    return (x > y) - (x < y);
}

/* A slot holds a save when its file is there and not empty. */
static int slot_used(const char *saves, int slot)
{
    char path[BW_PROFILE_PATH_MAX];
    FILE *fp;
    u64 size = 0;

    if (slot_path(saves, slot, path, sizeof(path)) != 0 || bw_path_kind_utf8(path) != BW_PATH_FILE) {
        return 0;
    }
    fp = bw_fopen_utf8(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (bw_file_size(fp, &size) != 0) {
        size = 0;
    }
    fclose(fp);
    return size > 0;
}

typedef struct MpWalk {
    const char *from;
    const char *to;
    int left;               /* games of a name the profile has already */
    int failed;
} MpWalk;

static int mp_entry(void *ctx, const char *name, int kind)
{
    MpWalk *w = (MpWalk *)ctx;
    char src[BW_PROFILE_PATH_MAX], dst[BW_PROFILE_PATH_MAX];

    if (join(src, sizeof(src), w->from, name) != 0 || join(dst, sizeof(dst), w->to, name) != 0) {
        w->failed = 1;
        return 0;
    }
    if (bw_path_kind_utf8(dst) != BW_PATH_NONE) {
        w->left++;          /* the profile's own stays; two games are never mixed */
    } else if (kind == BW_PATH_DIR ? bw_copy_tree_utf8(src, dst) != 0 : copy_one(src, dst) != 0) {
        w->failed = 1;
    }
    return 0;
}

/*
 * The old saves into the free slots of `saves`: the slots with no file, in
 * order, past the last one when there are not enough below it (there is no
 * limit).  A file already there, a save or not, is never written over.
 * The files are copied to a staging folder first and moved into place
 * after, so a copy that does not finish leaves the profile as it was.  0
 * done (`left` the multiplayer games that stayed behind), -1 failed.
 */
static int import_slots(const char *old, const char *saves, int *left)
{
    char stage[BW_PROFILE_PATH_MAX], src[BW_PROFILE_PATH_MAX], dst[BW_PROFILE_PATH_MAX];
    char tmp[BW_PROFILE_PATH_MAX], line[256];
    SlotList fromlist = {0}, tolist = {0};
    int *from, *to, nfrom, slot, i, rc = -1;
    FILE *in, *out;
    MpWalk mp;

    *left = 0;
    /* the old folder's saves: slot 1 and the slots/ files, by number */
    if (slot_used(old, 1) && slot_list_add(&fromlist, 1) != 0) {
        goto done;
    }
    if (join(src, sizeof(src), old, "slots") == 0 && bw_path_kind_utf8(src) == BW_PATH_DIR) {
        SlotList found = {0};

        (void)bw_list_dir_utf8(src, slot_entry, &found);
        for (i = 0; i < found.n; i++) {
            if (slot_used(old, found.v[i])) {
                (void)slot_list_add(&fromlist, found.v[i]);
            }
        }
        free(found.v);
    }
    qsort(fromlist.v, (size_t)fromlist.n, sizeof *fromlist.v, cmp_slot);
    /* as many free slots of the profile, in order */
    for (slot = 1; tolist.n < fromlist.n; slot++) {
        if (!slot_taken(saves, slot) && slot_list_add(&tolist, slot) != 0) {
            goto done;
        }
    }
    from = fromlist.v;
    to = tolist.v;
    nfrom = fromlist.n;
    if (join(dst, sizeof(dst), saves, "slots") != 0 || bw_mkdirs_utf8(dst) != 0
        || join(stage, sizeof(stage), saves, "import-old") != 0
        || bw_remove_tree_utf8(stage) != 0 || bw_mkdirs_utf8(stage) != 0) {
        goto done;
    }
    for (i = 0; i < nfrom; i++) {
        snprintf(line, sizeof(line), "%d.sav", from[i]);
        if (slot_path(old, from[i], src, sizeof(src)) != 0 || join(tmp, sizeof(tmp), stage, line) != 0
            || copy_one(src, tmp) != 0 || bw_path_kind_utf8(tmp) != BW_PATH_FILE) {
            (void)bw_remove_tree_utf8(stage);
            goto done;
        }
    }
    for (i = 0; i < nfrom; i++) {
        snprintf(line, sizeof(line), "%d.sav", from[i]);
        /* (bw_rename_utf8 fails when the slot has a file: never written over) */
        if (join(tmp, sizeof(tmp), stage, line) != 0 || slot_path(saves, to[i], dst, sizeof(dst)) != 0
            || bw_rename_utf8(tmp, dst) != 0) {
            (void)bw_remove_tree_utf8(stage);
            goto done;
        }
    }
    (void)bw_remove_tree_utf8(stage);
    /* the Your games record: each old line under its save's new slot */
    /* (the files as the game writes them: one line feed a line) */
    if (join(src, sizeof(src), old, "games.ini") == 0 && join(dst, sizeof(dst), saves, "games.ini") == 0
        && (in = bw_fopen_utf8(src, "rb")) != NULL) {
        out = bw_fopen_utf8(dst, "ab");
        while (out != NULL && fgets(line, sizeof(line), in) != NULL) {
            char *rest = line;
            long had = strtol(line, &rest, 10);

            if (rest == line || line[0] == '#') {
                continue;
            }
            rest[strcspn(rest, "\r\n")] = '\0';
            for (i = 0; i < nfrom; i++) {
                if (from[i] == (int)had) {
                    fprintf(out, "%d%s\n", to[i], rest);
                }
            }
        }
        if (out != NULL) {
            fclose(out);
        }
        fclose(in);
    }
    /* the multiplayer games, each in its own folder or file */
    if (join(src, sizeof(src), old, "multiplayer") == 0 && bw_path_kind_utf8(src) == BW_PATH_DIR
        && join(dst, sizeof(dst), saves, "multiplayer") == 0 && bw_mkdirs_utf8(dst) == 0) {
        mp.from = src;
        mp.to = dst;
        mp.left = 0;
        mp.failed = 0;
        if (bw_list_dir_utf8(src, mp_entry, &mp) != 0 || mp.failed) {
            goto done;
        }
        *left = mp.left;
    }
    rc = 0;
done:
    free(fromlist.v);
    free(tolist.v);
    return rc;
}

/* The note that says the old folder is no longer read. */
static void mark_copied(const char *mark, const char *where)
{
    FILE *fp = bw_fopen_utf8(mark, "w");

    if (fp != NULL) {
        fprintf(fp, "eidos copied the saves of this folder to\n%s\n"
                    "and plays from there now. This folder is no longer read.\n", where);
        fclose(fp);
    }
}

int bw_profile_prepare(const char *user_dir, BwGame game, const BwProfileOld *old,
                       char *out, size_t n, char *note, size_t note_len)
{
    char saves[BW_PROFILE_PATH_MAX], mods[BW_PROFILE_PATH_MAX], settings[BW_PROFILE_PATH_MAX];
    char mark[BW_PROFILE_PATH_MAX], other[BW_PROFILE_PATH_MAX];
    int copied = 0;

    say(note, note_len, "%s", "");
    if (bw_profile_dir(user_dir, game, out, n) != 0
        || join(saves, sizeof(saves), out, "saves") != 0
        || join(mods, sizeof(mods), out, "mods") != 0
        || join(settings, sizeof(settings), out, "settings.ini") != 0
        || bw_mkdirs_utf8(saves) != 0 || bw_mkdirs_utf8(mods) != 0) {
        say(note, note_len, "The profile folder of Pokemon %s could not be made.",
            game == BW_GAME_WHITE ? "White" : "Black");
        return BW_PROFILE_FAILED;
    }
    if (old != NULL && old->saves != NULL && bw_profile_has_saves(old->saves)
        && join(mark, sizeof(mark), old->saves, BW_PROFILE_COPIED_NAME) == 0
        && bw_path_kind_utf8(mark) == BW_PATH_NONE
        && (old->game == game || old->game == BW_GAME_UNKNOWN)) {
        if (bw_profile_has_saves(saves)) {
            /* the profile is in use (the launcher's): its saves stay the
             * ones played, and the old ones come beside them */
            int left = 0;
            int rc = import_slots(old->saves, saves, &left);

            if (rc == 0) {
                mark_copied(mark, saves);
                copied = 1;
                if (left > 0) {
                    say(note, note_len, "The saves in %s were copied to free save slots of %s; "
                        "%d multiplayer game%s of a name it has already stayed behind.",
                        old->saves, saves, left, left == 1 ? "" : "s");
                } else {
                    say(note, note_len, "The saves in %s were copied to free save slots of %s.",
                        old->saves, saves);
                }
            } else {
                say(note, note_len, "The saves in %s could not be copied to %s; they stay where "
                    "they are.", old->saves, saves);
            }
        } else {
            FILE *fp;

            if (bw_copy_tree_utf8(old->saves, saves) != 0
                || (old->mods != NULL && bw_path_kind_utf8(old->mods) == BW_PATH_DIR
                    && bw_copy_tree_utf8(old->mods, mods) != 0)
                || (old->settings != NULL && copy_one(old->settings, settings) != 0)) {
                say(note, note_len, "The saves in %s could not all be copied to %s; "
                    "they are played from where they are.", old->saves, saves);
                return BW_PROFILE_FAILED;
            }
            (void)record_moved(saves, old->mods, mods);
            fp = bw_fopen_utf8(mark, "w");
            if (fp != NULL) {
                fprintf(fp, "eidos copied the saves of this folder to\n%s\n"
                            "and plays from there now. This folder is no longer read.\n", saves);
                fclose(fp);
            }
            copied = 1;
            say(note, note_len, "The saves in %s were copied to %s.", old->saves, saves);
        }
    }
    /* a profile with no settings yet starts with the other game's */
    if (bw_profile_dir(user_dir, game == BW_GAME_WHITE ? BW_GAME_BLACK : BW_GAME_WHITE,
                       other, sizeof(other)) == 0) {
        char from[BW_PROFILE_PATH_MAX];

        if (join(from, sizeof(from), other, "settings.ini") == 0) {
            (void)copy_one(from, settings);
        }
    }
    return copied ? BW_PROFILE_COPIED : BW_PROFILE_READY;
}
