/*
 * sdk/mod_catalog.c -- the mods a player can get: the katalogos index, read
 * and installed by the game itself (sdk/mod_catalog.h).
 *
 * The rules are the launcher's (launcher/src-tauri/src/catalog.rs and
 * mods.rs, and the katalogos crate's verify); each one here names the rule
 * it copies.  The work that waits (the network, the unpacking) runs on a
 * worker; the worker owns its Work record and touches nothing else, and the
 * game's thread takes the record's results in bw_mod_catalog_poll.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_read.h"
#include "mod_api.h"
#include "mod_bw.h"
#include "mod_catalog.h"
#include "mod_loader.h"
#include "mod_menu.h"
#include "mod_wasm.h"
#include "net_https.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "plat_thread.h"
#include "sha256.h"
#include "vfs.h"

#define PATH_MAX_LEN 1024u

/* The folder a mod the launcher switched off waits in, beside "mods"
 * (launcher/src-tauri/src/mods.rs). */
#define OFF_FOLDER "mods-disabled"

/* Where a download is kept until it is checked, and where mod.json files
 * of releases that never change are kept, both beside "mods". */
#define DOWNLOAD_FOLDER ".katalogos-download"
#define KEPT_FOLDER     "katalogos-cache"

static void set_err(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || size == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, size, fmt, ap);
    va_end(ap);
}

static void copy_text(char *out, size_t size, const char *in)
{
    snprintf(out, size, "%s", in != NULL ? in : "");
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ------------------------------------------------------------------ */
/* The rules                                                           */
/* ------------------------------------------------------------------ */

/* An official mod's id: lower-case letters, digits and '-', not at an
 * end, 48 characters at most (katalogos: valid_id). */
static int valid_id(const char *id)
{
    size_t n = strlen(id), i;

    if (n == 0 || n > 48 || id[0] == '-' || id[n - 1] == '-') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) {
            return 0;
        }
    }
    return 1;
}

/* owner/name, each part of letters, digits, '-', '_' and '.', and neither
 * "." nor ".." (katalogos: valid_repo). */
static int valid_repo(const char *repo)
{
    const char *slash = strchr(repo, '/');
    const char *p;
    int part;

    if (slash == NULL || strchr(slash + 1, '/') != NULL) {
        return 0;
    }
    for (part = 0; part < 2; part++) {
        const char *s = part == 0 ? repo : slash + 1;
        const char *e = part == 0 ? slash : repo + strlen(repo);
        size_t n = (size_t)(e - s);

        if (n == 0 || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) {
            return 0;
        }
        for (p = s; p < e; p++) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) {
                return 0;
            }
        }
    }
    return 1;
}

/* x.y.z, digits only, six a part at most (katalogos: version_of; a
 * leading 'v' is the caller's to drop). */
static int valid_version(const char *v)
{
    int parts = 0;

    for (;;) {
        int digits = 0;

        while (*v >= '0' && *v <= '9') {
            v++;
            digits++;
        }
        if (digits == 0 || digits > 6) {
            return 0;
        }
        parts++;
        if (*v == '\0') {
            return parts == 3;
        }
        if (*v != '.' || parts == 3) {
            return 0;
        }
        v++;
    }
}

static const char *skip_v(const char *v)
{
    while (*v == 'v' || *v == 'V') {
        v++;
    }
    return v;
}

/* A string member into `out`; 1 when it is there and fits whole. */
static int member_text(BwJson obj, const char *key, char *out, size_t size)
{
    BwJson v;

    out[0] = '\0';
    return bw_json_member(obj, key, &v) && bw_json_string(v, out, size) == 0;
}

int bw_mod_catalog_parse_index(const char *text, size_t size, BwCatalogEntry *out,
                               u32 max, u32 *count, u32 *bad, char *err, size_t err_size)
{
    BwJson root, v, mods, item;
    unsigned long long format = 0;
    unsigned i, n;

    *count = 0;
    *bad = 0;
    if (bw_json_parse(text, size, &root) != 0 || bw_json_kind(root) != BW_JSON_OBJECT ||
        !bw_json_member(root, "mods", &mods) || bw_json_kind(mods) != BW_JSON_ARRAY ||
        !bw_json_member(root, "format", &v) || bw_json_u64(v, &format) != 0) {
        set_err(err, err_size, "Invalid mod index.");
        return -1;
    }
    if (format != 1) {
        set_err(err, err_size, "Unsupported mod index format %llu (expected 1). Update eidos.",
                format);
        return -1;
    }
    n = bw_json_count(mods);
    for (i = 0; i < n; i++) {
        BwCatalogEntry e;
        BwJson m;
        int has_mod, has_repo, has_version, ok;

        memset(&e, 0, sizeof(e));
        if (!bw_json_item(mods, i, &item) || bw_json_kind(item) != BW_JSON_OBJECT) {
            (*bad)++;
            continue;
        }
        has_mod = bw_json_member(item, "mod", &m) && bw_json_kind(m) != BW_JSON_NULL;
        has_repo = bw_json_member(item, "repo", &m) && bw_json_kind(m) != BW_JSON_NULL;
        has_version = bw_json_member(item, "version", &m) && bw_json_kind(m) != BW_JSON_NULL;
        /* (katalogos: entry_shape) */
        ok = has_mod != has_repo;
        if (ok && has_mod) {
            ok = member_text(item, "mod", e.official, sizeof(e.official)) && valid_id(e.official) && has_version;
        }
        if (ok && has_repo) {
            ok = member_text(item, "repo", e.repo, sizeof(e.repo)) && valid_repo(e.repo);
        }
        if (ok && has_version) {
            char raw[40];

            ok = member_text(item, "version", raw, sizeof(raw)) && valid_version(skip_v(raw));
            if (ok) {
                copy_text(e.version, sizeof(e.version), skip_v(raw));
            }
        }
        if (!ok) {
            (*bad)++;
            continue;
        }
        /* The index's badge marks: a mod of the index repository is
         * official unless "unofficial": true; another repository's mod is
         * not, unless "official": true. */
        if (has_mod && bw_json_member(item, "unofficial", &m)) {
            int b = 0;

            e.unofficial = bw_json_bool(m, &b) == 0 && b;
        }
        if (has_repo && bw_json_member(item, "official", &m)) {
            int b = 0;

            e.marked_official = bw_json_bool(m, &b) == 0 && b;
        }
        if (*count < max) {
            out[(*count)++] = e;
        }
    }
    return 0;
}

void bw_mod_catalog_entry_id(const BwCatalogEntry *e, char *out, size_t size)
{
    size_t i;

    if (e->official[0] != '\0') {
        snprintf(out, size, "katalogos/%s", e->official);
        return;
    }
    copy_text(out, size, e->repo);
    for (i = 0; out[i] != '\0'; i++) {
        out[i] = lower(out[i]);
    }
}

/* (katalogos: release_url) */
int bw_mod_catalog_entry_url(const BwCatalogEntry *e, const char *base, const char *file,
                             char *out, size_t size)
{
    int n;

    if (e->official[0] != '\0') {
        n = snprintf(out, size, "%s/%s/releases/download/%s-v%s/%s", base, BW_MOD_CATALOG_REPO,
                     e->official, e->version, file);
    } else if (e->version[0] != '\0') {
        n = snprintf(out, size, "%s/%s/releases/download/v%s/%s", base, e->repo, e->version, file);
    } else {
        n = snprintf(out, size, "%s/%s/releases/latest/download/%s", base, e->repo, file);
    }
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

static int blank(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    return *s == '\0';
}

int bw_mod_catalog_parse_mod(const char *text, size_t size, const BwCatalogEntry *e,
                             BwCatalogMod *out, char *err, size_t err_size)
{
    BwJson root, v, pack, item;
    unsigned long long n = 0;
    char want[108], repo[104], path[64], file[16], lower_repo[104];
    unsigned i, count;
    int b = 0;

    memset(out, 0, sizeof(*out));
    if (bw_json_parse(text, size, &root) != 0 || bw_json_kind(root) != BW_JSON_OBJECT) {
        set_err(err, err_size, "invalid mod.json");
        return -1;
    }
    if (!bw_json_member(root, "format", &v) || bw_json_u64(v, &n) != 0 || n != 1) {
        set_err(err, err_size, "mod.json is not format 1");
        return -1;
    }
    if (!member_text(root, "id", out->id, sizeof(out->id)) ||
        !member_text(root, "version", out->version, sizeof(out->version)) ||
        !member_text(root, "repo", repo, sizeof(repo))) {
        set_err(err, err_size, "mod.json has no id, version or repo");
        return -1;
    }
    /* a name, an author and a description longer than the list shows are cut */
    if (!bw_json_member(root, "name", &v) || bw_json_string(v, out->name, sizeof(out->name)) < 0 ||
        !bw_json_member(root, "description", &v) || bw_json_string(v, out->about, sizeof(out->about)) < 0 ||
        blank(out->name) || blank(out->about)) {
        set_err(err, err_size, "mod.json missing name or description");
        return -1;
    }
    if (bw_json_member(root, "author", &v)) {
        (void)bw_json_string(v, out->author, sizeof(out->author));
    }
    bw_mod_catalog_entry_id(e, want, sizeof(want));
    {
        char have[108];

        copy_text(have, sizeof(have), out->id);
        for (i = 0; have[i] != '\0'; i++) {
            have[i] = lower(have[i]);
        }
        if (strcmp(have, want) != 0) {
            set_err(err, err_size, "mod.json is for another mod (%.80s)", out->id);
            return -1;
        }
    }
    if (bw_json_member(root, "official", &v) && bw_json_bool(v, &b) == 0) {
        out->official = b;
    }
    path[0] = '\0';
    if (bw_json_member(root, "path", &v) && bw_json_string(v, path, sizeof(path)) != 0) {
        set_err(err, err_size, "invalid path in mod.json");
        return -1;
    }
    /* (katalogos: verify, "repo / id / path do not agree") */
    if (out->official) {
        char want_path[64];

        snprintf(want_path, sizeof(want_path), "mods/%s", out->id + (strlen(out->id) > 10 ? 10 : 0));
        if (strcmp(repo, BW_MOD_CATALOG_REPO) != 0 || strncmp(out->id, "katalogos/", 10) != 0 ||
            !valid_id(out->id + 10) || strcmp(path, want_path) != 0) {
            set_err(err, err_size, "mod.json repo, id, and path mismatch");
            return -1;
        }
    } else {
        copy_text(lower_repo, sizeof(lower_repo), repo);
        for (i = 0; lower_repo[i] != '\0'; i++) {
            lower_repo[i] = lower(lower_repo[i]);
        }
        if (!valid_repo(repo) || strcmp(out->id, lower_repo) != 0 || path[0] != '\0') {
            set_err(err, err_size, "mod.json repo, id, and path mismatch");
            return -1;
        }
    }
    if (!valid_version(out->version)) {
        set_err(err, err_size, "version %.20s is not x.y.z", out->version);
        return -1;
    }
    if (e->version[0] != '\0' && strcmp(e->version, out->version) != 0) {
        set_err(err, err_size, "version mismatch (index %s, mod.json %s)", e->version, out->version);
        return -1;
    }
    if (!bw_json_member(root, "games", &v) || (count = bw_json_count(v)) == 0) {
        set_err(err, err_size, "mod.json has no games");
        return -1;
    }
    for (i = 0; i < count; i++) {
        char game[16];

        if (!bw_json_item(v, i, &item) || bw_json_string(item, game, sizeof(game)) != 0 ||
            (strcmp(game, "black") != 0 && strcmp(game, "white") != 0)) {
            set_err(err, err_size, "mod.json games must be black or white");
            return -1;
        }
        out->games |= strcmp(game, "black") == 0 ? BW_MOD_GAME_BLACK : BW_MOD_GAME_WHITE;
    }
    if (bw_json_member(root, "api", &v) && bw_json_kind(v) != BW_JSON_NULL) {
        if (bw_json_u64(v, &n) != 0 || n > 100000ull) {
            set_err(err, err_size, "invalid api in mod.json");
            return -1;
        }
        out->api = (int)n;
    }
    if (!bw_json_member(root, "has_code", &v) || bw_json_bool(v, &b) != 0) {
        set_err(err, err_size, "invalid has_code in mod.json");
        return -1;
    }
    out->has_code = b;
    if (!bw_json_member(root, "package", &pack) || bw_json_kind(pack) != BW_JSON_OBJECT ||
        !member_text(pack, "file", file, sizeof(file)) || strcmp(file, "mod.bw") != 0 ||
        !member_text(pack, "sha256", out->sha256, sizeof(out->sha256)) ||
        !bw_json_member(pack, "size", &v) || bw_json_u64(v, &out->size) != 0 ||
        !bw_json_member(pack, "files", &v) || bw_json_u64(v, &n) != 0 || n > 0xFFFFFFFFull) {
        set_err(err, err_size, "invalid package in mod.json");
        return -1;
    }
    out->files = (u32)n;
    if (strlen(out->sha256) != 64) {
        set_err(err, err_size, "invalid package in mod.json");
        return -1;
    }
    for (i = 0; i < 64; i++) {
        char c = out->sha256[i] = lower(out->sha256[i]);

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            set_err(err, err_size, "invalid package in mod.json");
            return -1;
        }
    }
    if (out->size == 0 || out->size > BW_MOD_CATALOG_PACKAGE_MAX) {
        set_err(err, err_size, "package size (%llu bytes) exceeds limit", out->size);
        return -1;
    }
    /* The index's marks decide the badge (bw_mod_catalog_parse_index); the
     * checks above go by mod.json's own "official", where it is hosted. */
    if (e->unofficial) {
        out->official = 0;
    } else if (e->marked_official) {
        out->official = 1;
    }
    return 0;
}

int bw_mod_catalog_fits(const BwCatalogMod *m, int game, int api, int code)
{
    if (game != 0 && (m->games & game) == 0) {
        return 0;                       /* made for the other game */
    }
    if (m->api > api) {
        return 0;                       /* a newer mod interface   */
    }
    if (m->has_code && !code) {
        return 0;                       /* this build runs no code mods */
    }
    return 1;
}

/* (launcher: mods.rs, folder_name) */
void bw_mod_catalog_folder_name(const char *name, char *out, size_t size)
{
    const unsigned char *p = (const unsigned char *)name;
    size_t n = 0, chars = 0, i, start = 0;
    char buf[256];

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    while (*p != '\0' && n + 5 < sizeof(buf)) {
        if (*p < 0x20 || *p == 0x7F || strchr("\\/:*?\"<>|", *p) != NULL) {
            buf[n++] = '_';
            p++;
        } else if (*p == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F) {
            buf[n++] = '_';             /* a C1 control character */
            p += 2;
        } else {
            size_t len = *p < 0x80 ? 1 : *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;

            for (i = 0; i < len && p[i] != '\0'; i++) {
                buf[n++] = (char)p[i];
            }
            p += i;
        }
    }
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t' || buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        n--;
    }
    while (n > start && (buf[n - 1] == '.' || buf[n - 1] == ' ')) {
        n--;
    }
    while (start < n && (buf[start] == '.' || buf[start] == ' ')) {
        start++;
    }
    /* 60 characters at most, whole ones */
    for (i = start; i < n && chars < 60; chars++) {
        unsigned char c = (unsigned char)buf[i];

        i += c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    }
    if (i > n) {
        i = n;
    }
    if (i == start) {
        copy_text(out, size, "mod");
        return;
    }
    snprintf(out, size, "%.*s", (int)(i - start), buf + start);
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

/* The folder a path is in ("a/b/c" and "a/b/c/" give "a/b"); 0 or -1. */
static int parent_of(const char *path, char *out, size_t size)
{
    size_t n = strlen(path);

    while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
        n--;
    }
    while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        n--;
    }
    if (n == 0 || n + 1 > size) {
        return -1;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return 0;
}

/* The last part of a path. */
static const char *name_of(const char *path)
{
    const char *p = path + strlen(path);

    while (p > path && p[-1] != '/' && p[-1] != '\\') {
        p--;
    }
    return p;
}

static int join(char *out, size_t size, const char *a, const char *b)
{
    int n = snprintf(out, size, "%s/%s", a, b);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

/* A small file whole, with a NUL after it (the caller frees it), or NULL. */
static char *read_small(const char *path, size_t max, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t n;

    if (f == NULL) {
        return NULL;
    }
    buf = (char *)malloc(max + 2);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, max + 1, f);
    fclose(f);
    if (n > max) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *size = n;
    return buf;
}

int bw_mod_catalog_installed_as(const char *root, char *id, size_t id_size,
                                char *version, size_t version_size)
{
    char path[PATH_MAX_LEN];
    char *text;
    size_t size = 0;
    BwJson json;
    int ok;

    id[0] = '\0';
    version[0] = '\0';
    if (root == NULL || join(path, sizeof(path), root, BW_MOD_CATALOG_INSTALLED) != 0 ||
        (text = read_small(path, 4096, &size)) == NULL) {
        return 0;
    }
    ok = bw_json_parse(text, size, &json) == 0 && member_text(json, "id", id, id_size) &&
         member_text(json, "version", version, version_size);
    free(text);
    if (!ok) {
        id[0] = '\0';
        version[0] = '\0';
    }
    return ok;
}

/* The folder in `dir` that holds the katalogos mod `id` (launcher:
 * mods.rs, catalog_folder). */
typedef struct FindMod {
    const char *dir;
    const char *id;
    char found[PATH_MAX_LEN];
} FindMod;

static int find_mod_entry(void *ctx, const char *name)
{
    FindMod *f = (FindMod *)ctx;
    char path[PATH_MAX_LEN], id[108], version[24];

    if (name[0] == '.' || join(path, sizeof(path), f->dir, name) != 0 ||
        bw_plat_path_kind(path) != BW_PATH_DIR) {
        return 0;
    }
    if (bw_mod_catalog_installed_as(path, id, sizeof(id), version, sizeof(version)) &&
        strcmp(id, f->id) == 0) {
        copy_text(f->found, sizeof(f->found), path);
        return 1;
    }
    return 0;
}

static int find_mod(const char *dir, const char *id, char *out, size_t size)
{
    FindMod f;

    f.dir = dir;
    f.id = id;
    f.found[0] = '\0';
    (void)bw_plat_list_dir(dir, find_mod_entry, &f);
    copy_text(out, size, f.found);
    return f.found[0] != '\0';
}

/* How many files a folder holds, the ones in its folders too. */
typedef struct CountFiles {
    const char *dir;
    u32 files;
    int depth;
} CountFiles;

static int count_entry(void *ctx, const char *name)
{
    CountFiles *c = (CountFiles *)ctx;
    char path[PATH_MAX_LEN];
    int kind;

    if (join(path, sizeof(path), c->dir, name) != 0) {
        return 0;
    }
    kind = bw_plat_path_kind(path);
    if (kind == BW_PATH_DIR && c->depth < 64) {
        CountFiles sub;

        sub.dir = path;
        sub.files = 0;
        sub.depth = c->depth + 1;
        (void)bw_plat_list_dir(path, count_entry, &sub);
        c->files += sub.files;
    } else if (kind == BW_PATH_FILE) {
        c->files++;
    }
    return 0;
}

static u32 count_files(const char *dir)
{
    CountFiles c;

    c.dir = dir;
    c.files = 0;
    c.depth = 0;
    (void)bw_plat_list_dir(dir, count_entry, &c);
    return c.files;
}

/* ------------------------------------------------------------------ */
/* The worker                                                          */
/* ------------------------------------------------------------------ */

enum { WORK_READ = 1, WORK_INSTALL = 2 };

/* One piece of work: what the game's thread gave it, and what it found.
 * The worker alone touches it until `done` is set. */
typedef struct Work {
    int  kind;
    volatile int done;

    /* given */
    char index_url[BW_HTTPS_URL_MAX + 1];
    char base[256];
    char mods_dir[PATH_MAX_LEN];        /* the installed mods folder   */
    int  game, api, code;
    BwCatalogEntry entry;               /* WORK_INSTALL: the mod       */

    /* found */
    int  ok;
    char err[200];
    BwCatalogMod   *mods;               /* WORK_READ                   */
    BwCatalogEntry *entries;
    u32  count, unfit, unread;
    char dest[PATH_MAX_LEN];            /* WORK_INSTALL: its folder    */
    int  fresh;                         /* ... 1: no copy was there    */
} Work;

static BwThread *g_thread;
static Work     *g_work;
static volatile int g_stop;

/* How far the install is, written by the worker and read by the screen
 * (bw_shared_lock around both). */
static int g_step;
static unsigned long long g_got, g_total;

static void step_set(int step)
{
    bw_shared_lock();
    g_step = step;
    bw_shared_unlock();
}

/* A download into memory. */
typedef struct Mem {
    char  *text;
    size_t size;
    size_t room;
} Mem;

static int mem_sink(void *user, const void *data, u32 size)
{
    Mem *m = (Mem *)user;

    if (m->size + size + 1 > m->room) {
        size_t room = m->room * 2 + size + 1024;
        char *text = (char *)realloc(m->text, room);

        if (text == NULL) {
            return -1;
        }
        m->text = text;
        m->room = room;
    }
    memcpy(m->text + m->size, data, size);
    m->size += size;
    m->text[m->size] = '\0';
    return 0;
}

/* `url` into memory (the caller frees *text).  0, or -1 with the reason. */
static int get_text(const char *url, char **text, size_t *size, char *err, size_t err_size)
{
    BwHttpsGet g;
    Mem m;

    memset(&g, 0, sizeof(g));
    memset(&m, 0, sizeof(m));
    g.max = BW_MOD_CATALOG_JSON_MAX;
    g.sink = mem_sink;
    g.user = &m;
    g.cancel = &g_stop;
    if (bw_https_get(url, &g) != 0 || m.text == NULL) {
        free(m.text);
        set_err(err, err_size, "%s", g.err[0] != '\0' ? g.err : "the file is empty");
        return -1;
    }
    *text = m.text;
    *size = m.size;
    return 0;
}

/*
 * A release file kept on disk by its address (launcher: catalog.rs,
 * get_kept).  A release's files do not change once its tag is out, so a
 * kept copy is the file and the server is not asked again.  Only for an
 * address that names its release: "latest" names another one tomorrow.
 */
static int get_kept(const Work *w, const char *url, int keep, char **text, size_t *size,
                    char *err, size_t err_size)
{
    char parent[PATH_MAX_LEN], dir[PATH_MAX_LEN], path[PATH_MAX_LEN], part[PATH_MAX_LEN];
    char hex[65];
    u8 hash[32];
    BwSha256 sha;
    FILE *f;

    if (!keep || parent_of(w->mods_dir, parent, sizeof(parent)) != 0 ||
        join(dir, sizeof(dir), parent, KEPT_FOLDER) != 0) {
        return get_text(url, text, size, err, err_size);
    }
    bw_sha256_start(&sha);
    bw_sha256_add(&sha, url, strlen(url));
    bw_sha256_end(&sha, hash);
    bw_sha256_hex(hash, hex);
    hex[32] = '\0';
    if (join(path, sizeof(path), dir, hex) != 0) {
        return get_text(url, text, size, err, err_size);
    }
    *text = read_small(path, BW_MOD_CATALOG_JSON_MAX, size);
    if (*text != NULL) {
        return 0;
    }
    if (get_text(url, text, size, err, err_size) != 0) {
        return -1;
    }
    if (bw_plat_mkdirs(dir) == 0 && snprintf(part, sizeof(part), "%s.part", path) < (int)sizeof(part) &&
        (f = fopen(part, "wb")) != NULL) {
        int wrote = fwrite(*text, 1, *size, f) == *size;

        if (fclose(f) != 0 || !wrote || bw_plat_replace_file(part, path) != 0) {
            remove(part);
        }
    }
    return 0;
}

static int name_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = lower(*a), y = lower(*b);

        if (x != y || x == '\0') {
            return (int)(unsigned char)x - (int)(unsigned char)y;
        }
    }
}

/* Official mods first, then by name (launcher: catalog.rs, list). */
static int before(const BwCatalogMod *a, const BwCatalogMod *b)
{
    if (a->official != b->official) {
        return a->official;
    }
    return name_cmp(a->name, b->name) < 0;
}

static void work_read(Work *w)
{
    char *text = NULL;
    size_t size = 0;
    char url[BW_HTTPS_URL_MAX + 1];
    char label[108];
    u32 n = 0, bad = 0, i, j;

    w->mods = (BwCatalogMod *)calloc(BW_MOD_CATALOG_MAX, sizeof(*w->mods));
    w->entries = (BwCatalogEntry *)calloc(BW_MOD_CATALOG_MAX, sizeof(*w->entries));
    if (w->mods == NULL || w->entries == NULL) {
        set_err(w->err, sizeof(w->err), "out of memory");
        return;
    }
    if (get_text(w->index_url, &text, &size, w->err, sizeof(w->err)) != 0) {
        return;
    }
    if (bw_mod_catalog_parse_index(text, size, w->entries, BW_MOD_CATALOG_MAX, &n, &bad,
                                   w->err, sizeof(w->err)) != 0) {
        free(text);
        return;
    }
    free(text);
    w->unread = bad;
    for (i = 0; i < n; i++) {
        BwCatalogEntry e = w->entries[i];
        BwCatalogMod m;
        char why[200];

        if (g_stop) {
            set_err(w->err, sizeof(w->err), "stopped");
            return;
        }
        bw_mod_catalog_entry_id(&e, label, sizeof(label));
        why[0] = '\0';
        if (bw_mod_catalog_entry_url(&e, w->base, "mod.json", url, sizeof(url)) != 0 ||
            get_kept(w, url, e.version[0] != '\0', &text, &size, why, sizeof(why)) != 0) {
            fprintf(stderr, "eidos: the mod index: %s: %s\n", label, why[0] != '\0' ? why : "no address");
            w->unread++;
            continue;
        }
        if (bw_mod_catalog_parse_mod(text, size, &e, &m, why, sizeof(why)) != 0) {
            fprintf(stderr, "eidos: the mod index: %s: %s\n", label, why);
            w->unread++;
            free(text);
            continue;
        }
        free(text);
        if (!bw_mod_catalog_fits(&m, w->game, w->api, w->code)) {
            w->unfit++;
            continue;
        }
        /* in its place by the order, its entry with it */
        for (j = w->count; j > 0 && before(&m, &w->mods[j - 1]); j--) {
            w->mods[j] = w->mods[j - 1];
            w->entries[j] = w->entries[j - 1];
        }
        w->mods[j] = m;
        w->entries[j] = e;
        w->count++;
    }
    w->ok = 1;
}

/* A download into a file, hashed as it comes. */
typedef struct FileSink {
    FILE *f;
    BwSha256 sha;
    unsigned long long size;            /* the bytes written */
} FileSink;

static int file_sink(void *user, const void *data, u32 size)
{
    FileSink *s = (FileSink *)user;

    bw_sha256_add(&s->sha, data, size);
    s->size += size;
    return fwrite(data, 1, size, s->f) == size ? 0 : -1;
}

static void file_progress(void *user, unsigned long long got, unsigned long long total)
{
    (void)user;
    bw_shared_lock();
    g_got = got;
    if (total != 0) {
        g_total = total;
    }
    bw_shared_unlock();
}

/* The unpacking, on the thread beside the game. */
typedef struct Unpack {
    const char *bw;
    const char *dir;
    char err[160];
    int  rc;
    volatile int done;
} Unpack;

static void unpack_main(void *arg)
{
    Unpack *u = (Unpack *)arg;

    u->rc = bw_mod_bw_unpack(u->bw, u->dir, u->err, sizeof(u->err));
    u->done = 1;
}

static int exists(const char *path)
{
    return bw_plat_path_kind(path) != BW_PATH_NONE;
}

/* A folder renamed; a few tries, because a scanner may hold a new file of
 * it open for a moment. */
static int move_folder(const char *from, const char *to)
{
    int tries;

    for (tries = 0; tries < 30; tries++) {
        if (rename(from, to) == 0) {
            return 0;
        }
        bw_net_sleep_ms(100);
    }
    return -1;
}

static void work_install(Work *w)
{
    char parent[PATH_MAX_LEN], off[PATH_MAX_LEN], tmp[PATH_MAX_LEN], bw[PATH_MAX_LEN];
    char in_on[PATH_MAX_LEN], in_off[PATH_MAX_LEN], staging[PATH_MAX_LEN], record[PATH_MAX_LEN];
    char url[BW_HTTPS_URL_MAX + 1];
    char folder[256], hex[65];
    char *text = NULL;
    size_t size = 0;
    BwCatalogMod m;
    BwHttpsGet g;
    FileSink sink;
    Unpack unpack;
    BwThread *t;
    u8 hash[32];
    FILE *f;
    u32 files;
    int n;

    char why[160];

    /* the mod's mod.json, as it is now */
    why[0] = '\0';
    if (bw_mod_catalog_entry_url(&w->entry, w->base, "mod.json", url, sizeof(url)) != 0 ||
        get_text(url, &text, &size, why, sizeof(why)) != 0) {
        set_err(w->err, sizeof(w->err), "Download failed: %s.", why[0] != '\0' ? why : "bad URL");
        return;
    }
    n = bw_mod_catalog_parse_mod(text, size, &w->entry, &m, why, sizeof(why));
    free(text);
    if (n != 0) {
        set_err(w->err, sizeof(w->err), "Download failed: %s.", why);
        return;
    }
    if (!bw_mod_catalog_fits(&m, w->game, w->api, w->code)) {
        set_err(w->err, sizeof(w->err), "This mod version is incompatible with this game.");
        return;
    }

    /* where it goes (launcher: mods.rs, install_package) */
    if (parent_of(w->mods_dir, parent, sizeof(parent)) != 0 ||
        join(off, sizeof(off), parent, OFF_FOLDER) != 0 ||
        join(tmp, sizeof(tmp), parent, DOWNLOAD_FOLDER) != 0 ||
        join(bw, sizeof(bw), tmp, "mod.bw") != 0) {
        set_err(w->err, sizeof(w->err), "Mods folder path is too long.");
        return;
    }
    in_off[0] = '\0';
    if (find_mod(w->mods_dir, m.id, in_on, sizeof(in_on))) {
        copy_text(folder, sizeof(folder), name_of(in_on));
        copy_text(w->dest, sizeof(w->dest), in_on);
    } else {
        char other[PATH_MAX_LEN];

        if (find_mod(off, m.id, in_off, sizeof(in_off))) {
            /* a copy the launcher holds switched off: its folder's name */
            copy_text(folder, sizeof(folder), name_of(in_off));
            other[0] = '\0';
        } else {
            bw_mod_catalog_folder_name(m.name, folder, sizeof(folder));
            if (join(other, sizeof(other), off, folder) != 0) {
                other[0] = '\0';
            }
            w->fresh = 1;
        }
        if (join(w->dest, sizeof(w->dest), w->mods_dir, folder) != 0 || exists(w->dest) ||
            (other[0] != '\0' && exists(other))) {
            set_err(w->err, sizeof(w->err), "A mod named %.100s is already installed.", folder);
            return;
        }
        if (in_off[0] != '\0') {
            w->fresh = 1;               /* (to the game it is a new mod) */
        }
    }
    n = snprintf(staging, sizeof(staging), "%s/.%s.new", w->mods_dir, folder);
    if (n <= 0 || (size_t)n >= sizeof(staging) ||
        join(record, sizeof(record), staging, BW_MOD_CATALOG_INSTALLED) != 0) {
        set_err(w->err, sizeof(w->err), "Mods folder path is too long.");
        return;
    }

    /* the package, into a file, checked against its mod.json */
    (void)bw_plat_remove_tree(tmp);
    if (bw_plat_mkdirs(tmp) != 0 || bw_plat_mkdirs(w->mods_dir) != 0 || (f = fopen(bw, "wb")) == NULL) {
        set_err(w->err, sizeof(w->err), "Cannot write to the mods folder.");
        return;
    }
    memset(&g, 0, sizeof(g));
    sink.f = f;
    sink.size = 0;
    bw_sha256_start(&sink.sha);
    g.max = m.size;
    g.sink = file_sink;
    g.user = &sink;
    g.progress = file_progress;
    g.cancel = &g_stop;
    bw_shared_lock();
    g_got = 0;
    g_total = m.size;
    bw_shared_unlock();
    if (bw_mod_catalog_entry_url(&w->entry, w->base, "mod.bw", url, sizeof(url)) != 0) {
        g.err[0] = '\0';
        n = -1;
    } else {
        n = bw_https_get(url, &g);
    }
    if (fclose(f) != 0 && n == 0) {
        n = -1;
        copy_text(g.err, sizeof(g.err), "could not save file");
    }
    if (n != 0) {
        set_err(w->err, sizeof(w->err), "Download failed: %.150s.", g.err[0] != '\0' ? g.err : "bad URL");
        goto fail;
    }
    bw_sha256_end(&sink.sha, hash);
    bw_sha256_hex(hash, hex);
    if (sink.size != m.size) {
        set_err(w->err, sizeof(w->err), "Download size mismatch: mod.bw is %llu bytes, "
                "mod.json expects %llu.", sink.size, m.size);
        goto fail;
    }
    if (strcmp(hex, m.sha256) != 0) {
        set_err(w->err, sizeof(w->err), "Download SHA-256 does not match mod.json.");
        goto fail;
    }

    /* unpacked beside the installed mods first, so a failed update leaves
     * the old copy; the unpack checks the package limits (sdk/mod_bw.h) */
    step_set(BW_MOD_CATALOG_JOB_UNPACKING);
    (void)bw_plat_remove_tree(staging);
    memset(&unpack, 0, sizeof(unpack));
    unpack.bw = bw;
    unpack.dir = staging;
    t = bw_thread_start_beside(unpack_main, &unpack);
    while (!unpack.done) {
        bw_net_sleep_ms(5);
    }
    bw_thread_join(t);
    if (unpack.rc != 0) {
        set_err(w->err, sizeof(w->err), "Could not unpack mod.bw: %.120s", unpack.err);
        goto fail;
    }
    files = count_files(staging);
    if (files != m.files) {
        set_err(w->err, sizeof(w->err), "mod.bw has %u files (mod.json lists %u).", (unsigned)files,
                (unsigned)m.files);
        goto fail_staging;
    }
    if ((f = fopen(record, "wb")) == NULL ||
        fprintf(f, "{\n  \"id\": \"%s\",\n  \"version\": \"%s\"\n}", m.id, m.version) < 0) {
        if (f != NULL) {
            fclose(f);
        }
        set_err(w->err, sizeof(w->err), "Cannot write to the mods folder.");
        goto fail_staging;
    }
    if (fclose(f) != 0) {
        set_err(w->err, sizeof(w->err), "Cannot write to the mods folder.");
        goto fail_staging;
    }
    if (exists(w->dest) && bw_plat_remove_tree(w->dest) != 0) {
        set_err(w->err, sizeof(w->err), "Could not remove old copy: a file is in use.");
        goto fail_staging;
    }
    if (move_folder(staging, w->dest) != 0) {
        set_err(w->err, sizeof(w->err), "Could not move mod into the mods folder.");
        goto fail_staging;
    }
    if (in_off[0] != '\0') {
        (void)bw_plat_remove_tree(in_off);
    }
    (void)bw_plat_remove_tree(tmp);
    w->ok = 1;
    return;

fail_staging:
    (void)bw_plat_remove_tree(staging);
fail:
    (void)bw_plat_remove_tree(tmp);
}

static void work_main(void *arg)
{
    Work *w = (Work *)arg;

    if (w->kind == WORK_READ) {
        work_read(w);
    } else {
        work_install(w);
    }
    if (!w->ok && w->err[0] == '\0') {
        set_err(w->err, sizeof(w->err), "stopped");
    }
    bw_https_idle(NULL);                /* the connections kept between the files */
    w->done = 1;
}

/* ------------------------------------------------------------------ */
/* The model, on the game's thread                                     */
/* ------------------------------------------------------------------ */

typedef struct Listed {
    BwCatalogMod   mod;
    BwCatalogEntry entry;
    int  installed;                     /* 0, 1 this version, 2 another */
    char root[PATH_MAX_LEN];            /* its folder when installed    */
} Listed;

static Listed *g_list;
static u32  g_count, g_unfit, g_unread;
static int  g_state;
static char g_error[200];
static unsigned long long g_read_at;    /* bw_clock_ns when the list came */
static char g_read_dir[PATH_MAX_LEN];   /* the mods folder it was read for */
static int  g_want_read;                /* a read asked for while the worker was busy */

static int  g_job;                      /* BW_MOD_CATALOG_JOB_*         */
static char g_job_id[108];              /* the mod it installs          */
static char g_job_root[PATH_MAX_LEN];   /* the copy it replaces, or ""  */
static char g_job_dest[PATH_MAX_LEN];   /* where it went                */
static int  g_job_was_on;               /* that copy was switched on    */
static int  g_job_fresh;
static char g_job_error[200];

static void work_free(Work *w)
{
    if (w != NULL) {
        free(w->mods);
        free(w->entries);
        free(w);
    }
}

/* The work record with what every kind needs; NULL without memory or
 * without a mods folder. */
static Work *work_new(int kind)
{
    const char *index = getenv("EIDOS_MOD_INDEX");
    const char *base = getenv("EIDOS_MOD_RELEASES");
    Work *w = (Work *)calloc(1, sizeof(*w));

    if (w == NULL) {
        return NULL;
    }
    w->kind = kind;
    copy_text(w->index_url, sizeof(w->index_url),
              index != NULL && index[0] != '\0' ? index : BW_MOD_CATALOG_INDEX);
    copy_text(w->base, sizeof(w->base), base != NULL && base[0] != '\0' ? base : BW_MOD_CATALOG_RELEASES);
    if (bw_mod_menu_installed_folder(w->mods_dir, sizeof(w->mods_dir)) != 0) {
        free(w);
        return NULL;
    }
    w->game = bw_mod_game_loaded();
    w->api = BW_MOD_API_VERSION;
    w->code = bw_mod_wasm_available();
    return w;
}

static void work_start(Work *w)
{
    (void)bw_net_tls_stream();          /* (its lock is made on this thread) */
    g_stop = 0;
    g_work = w;
    g_thread = bw_thread_start(work_main, w);
}

/* Which of the listed mods are installed, and where. */
static void mark_installed(void)
{
    char folder[PATH_MAX_LEN];
    u32 i;

    if (g_list == NULL) {
        return;
    }
    if (bw_mod_menu_installed_folder(folder, sizeof(folder)) != 0) {
        folder[0] = '\0';
    }
    for (i = 0; i < g_count; i++) {
        char id[108], version[24];

        g_list[i].installed = 0;
        g_list[i].root[0] = '\0';
        if (folder[0] != '\0' && find_mod(folder, g_list[i].mod.id, g_list[i].root, sizeof(g_list[i].root)) &&
            bw_mod_catalog_installed_as(g_list[i].root, id, sizeof(id), version, sizeof(version))) {
            g_list[i].installed = strcmp(version, g_list[i].mod.version) == 0 ? 1 : 2;
        }
    }
}

static void start_read(void)
{
    Work *w = work_new(WORK_READ);

    g_want_read = 0;
    if (w == NULL) {
        g_state = BW_MOD_CATALOG_FAILED;
        copy_text(g_error, sizeof(g_error), "Mods folder not found.");
        return;
    }
    g_state = BW_MOD_CATALOG_READING;
    g_error[0] = '\0';
    work_start(w);
}

/* The mods folder in force is not the one the work, or the list kept, was
 * for: the run went to the other version of the game, which has its own
 * profile and so its own mods (the boot's version switch).  What a mod
 * fits and which ones are installed are then to be found again. */
static int other_folder(const char *was)
{
    char folder[PATH_MAX_LEN];

    return bw_mod_menu_installed_folder(folder, sizeof(folder)) == 0 && strcmp(folder, was) != 0;
}

void bw_mod_catalog_read(unsigned fresh_s)
{
    if (g_work != NULL) {
        const int other = other_folder(g_work->mods_dir);

        if (g_work->kind != WORK_READ || g_stop || other) {
            g_want_read = 1;            /* after the work that runs */
            if (g_state != BW_MOD_CATALOG_READY || other) {
                g_state = BW_MOD_CATALOG_READING;
            }
        }
        return;
    }
    if (g_state == BW_MOD_CATALOG_READY && fresh_s != 0 && !other_folder(g_read_dir) &&
        bw_clock_ns() - g_read_at < (unsigned long long)fresh_s * 1000000000ull) {
        return;
    }
    start_read();
}

/* The worker's finished work, taken on the game's thread. */
static void take(void)
{
    Work *w = g_work;
    int stopped = g_stop;
    u32 i;

    bw_thread_join(g_thread);
    g_thread = NULL;
    g_work = NULL;
    g_stop = 0;
    if (w->kind == WORK_READ) {
        if (w->ok) {
            if (g_list == NULL) {
                g_list = (Listed *)calloc(BW_MOD_CATALOG_MAX, sizeof(*g_list));
            }
            if (g_list != NULL) {
                for (i = 0; i < w->count; i++) {
                    g_list[i].mod = w->mods[i];
                    g_list[i].entry = w->entries[i];
                }
                g_count = w->count;
                g_unfit = w->unfit;
                g_unread = w->unread;
                g_read_at = bw_clock_ns();
                copy_text(g_read_dir, sizeof(g_read_dir), w->mods_dir);
                g_state = BW_MOD_CATALOG_READY;
                mark_installed();
            } else {
                g_state = BW_MOD_CATALOG_FAILED;
                copy_text(g_error, sizeof(g_error), "out of memory");
            }
        } else if (stopped) {
            /* the screen closed: the list is as it was before */
            g_state = g_list != NULL && g_read_at != 0 ? BW_MOD_CATALOG_READY : BW_MOD_CATALOG_NONE;
        } else {
            g_state = BW_MOD_CATALOG_FAILED;
            copy_text(g_error, sizeof(g_error), w->err);
        }
    } else {
        copy_text(g_job_dest, sizeof(g_job_dest), w->dest);
        g_job_fresh = w->fresh;
        if (w->ok) {
            g_job = BW_MOD_CATALOG_JOB_DONE;
            g_job_error[0] = '\0';
        } else {
            g_job = BW_MOD_CATALOG_JOB_FAILED;
            copy_text(g_job_error, sizeof(g_job_error), w->err);
        }
    }
    work_free(w);
    if (g_want_read && g_job != BW_MOD_CATALOG_JOB_DONE && g_job != BW_MOD_CATALOG_JOB_FAILED) {
        start_read();
    }
}

int bw_mod_catalog_poll(void)
{
    if (g_work != NULL && g_work->done) {
        take();
    }
    return g_state;
}

int bw_mod_catalog_state(void)
{
    return g_state;
}

const char *bw_mod_catalog_error(void)
{
    return g_error;
}

u32 bw_mod_catalog_count(void)
{
    return g_state == BW_MOD_CATALOG_READY || g_list != NULL ? g_count : 0;
}

static const BwCatalogMod *mod_at(u32 index)
{
    return (g_list != NULL && index < g_count) ? &g_list[index].mod : NULL;
}

const char *bw_mod_catalog_id_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->id : "";
}

const char *bw_mod_catalog_name_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->name : "";
}

const char *bw_mod_catalog_author_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->author : "";
}

const char *bw_mod_catalog_version_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->version : "";
}

const char *bw_mod_catalog_about_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->about : "";
}

unsigned long long bw_mod_catalog_size_at(u32 index)
{
    const BwCatalogMod *m = mod_at(index);

    return m != NULL ? m->size : 0;
}

u32 bw_mod_catalog_unfit(void)
{
    return g_unfit;
}

u32 bw_mod_catalog_unread(void)
{
    return g_unread;
}

int bw_mod_catalog_installed_at(u32 index)
{
    return (g_list != NULL && index < g_count) ? g_list[index].installed : 0;
}

const char *bw_mod_catalog_root_at(u32 index)
{
    return (g_list != NULL && index < g_count && g_list[index].installed) ? g_list[index].root : "";
}

/* The mod menu's row whose folder is `root`, or -1. */
static int menu_row(const char *root)
{
    u32 i, n = bw_mod_menu_count();

    for (i = 0; i < n; i++) {
        if (strcmp(bw_mod_menu_root_at(i), root) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int bw_mod_catalog_install(u32 index)
{
    Work *w;
    int row;

    if (g_list == NULL || index >= g_count || g_work != NULL ||
        g_job != BW_MOD_CATALOG_JOB_NONE || (w = work_new(WORK_INSTALL)) == NULL) {
        return -1;
    }
    w->entry = g_list[index].entry;
    copy_text(g_job_id, sizeof(g_job_id), g_list[index].mod.id);
    copy_text(g_job_root, sizeof(g_job_root), g_list[index].root);
    g_job_dest[0] = '\0';
    g_job_error[0] = '\0';
    g_job_was_on = 0;
    g_job_fresh = 0;
    /* the installed copy is about to be replaced: nothing of it may run */
    row = g_job_root[0] != '\0' ? menu_row(g_job_root) : -1;
    if (row >= 0 && (bw_mod_menu_enabled_at((u32)row) || bw_mod_menu_running_at((u32)row))) {
        g_job_was_on = bw_mod_menu_enabled_at((u32)row);
        (void)bw_mod_menu_set_enabled((u32)row, 0);
        (void)bw_mod_menu_apply();
    }
    bw_shared_lock();
    g_step = BW_MOD_CATALOG_JOB_DOWNLOADING;
    g_got = 0;
    g_total = g_list[index].mod.size;
    bw_shared_unlock();
    g_job = BW_MOD_CATALOG_JOB_DOWNLOADING;
    work_start(w);
    return 0;
}

int bw_mod_catalog_job(int *index, unsigned long long *got, unsigned long long *total)
{
    int state = g_job;
    u32 i;

    bw_shared_lock();
    if (state == BW_MOD_CATALOG_JOB_DOWNLOADING) {
        state = g_step;                 /* downloading, or unpacking by now */
    }
    if (got != NULL) {
        *got = g_got;
    }
    if (total != NULL) {
        *total = g_total;
    }
    bw_shared_unlock();
    if (index != NULL) {
        *index = -1;
        for (i = 0; g_list != NULL && i < g_count; i++) {
            if (strcmp(g_list[i].mod.id, g_job_id) == 0) {
                *index = (int)i;
            }
        }
    }
    return state;
}

const char *bw_mod_catalog_job_error(void)
{
    return g_job_error;
}

int bw_mod_catalog_finish(void)
{
    int rc = 0, row;

    if (g_job == BW_MOD_CATALOG_JOB_DONE) {
        rc = bw_mod_menu_reload();
        /* A new mod runs at once (its row may be an old one of the
         * player's record, switched off when the mod was removed); an
         * updated one gets the switch it had. */
        row = menu_row(g_job_dest);
        if (row >= 0 && (g_job_fresh || g_job_was_on) && !bw_mod_menu_enabled_at((u32)row)) {
            (void)bw_mod_menu_set_enabled((u32)row, 1);
            if (bw_mod_menu_apply() != 0) {
                rc = -1;
            }
        }
        fprintf(stderr, "eidos: mods: %s installed in %s (code mods loaded: %u)\n", g_job_id, g_job_dest,
                (unsigned)bw_mod_loader_count());
    } else if (g_job == BW_MOD_CATALOG_JOB_FAILED) {
        row = g_job_root[0] != '\0' ? menu_row(g_job_root) : -1;
        if (row >= 0 && g_job_was_on && bw_mod_menu_present_at((u32)row) &&
            !bw_mod_menu_enabled_at((u32)row)) {
            (void)bw_mod_menu_set_enabled((u32)row, 1);
            (void)bw_mod_menu_apply();
        }
    } else {
        return 0;
    }
    g_job = BW_MOD_CATALOG_JOB_NONE;
    g_job_id[0] = '\0';
    mark_installed();
    if (g_want_read && g_work == NULL) {
        start_read();
    }
    return rc;
}

void bw_mod_catalog_stop(void)
{
    if (g_work != NULL) {
        g_stop = 1;
    }
    g_want_read = 0;
}

int bw_mod_catalog_busy(void)
{
    return g_work != NULL;
}

int bw_mod_catalog_can_remove(const char *root)
{
    char folder[PATH_MAX_LEN], parent[PATH_MAX_LEN], manifest[PATH_MAX_LEN];
    size_t i;

    if (root == NULL || root[0] == '\0' || bw_mod_menu_installed_folder(folder, sizeof(folder)) != 0 ||
        parent_of(root, parent, sizeof(parent)) != 0 || name_of(root)[0] == '.' || name_of(root)[0] == '\0') {
        return 0;
    }
    /* the folder it is in is the installed mods folder itself */
    for (i = strlen(folder); i > 0 && (folder[i - 1] == '/' || folder[i - 1] == '\\'); i--) {
        folder[i - 1] = '\0';
    }
    for (i = 0; parent[i] != '\0' || folder[i] != '\0'; i++) {
        char a = parent[i] == '\\' ? '/' : parent[i];
        char b = folder[i] == '\\' ? '/' : folder[i];

        if (a != b) {
            return 0;
        }
    }
    return join(manifest, sizeof(manifest), root, "eidos_mod.txt") == 0 &&
           bw_plat_path_kind(manifest) == BW_PATH_FILE;
}

int bw_mod_catalog_remove(const char *root)
{
    char path[PATH_MAX_LEN];
    int row, rc;

    g_job_error[0] = '\0';
    if (g_work != NULL && g_work->kind == WORK_INSTALL) {
        copy_text(g_job_error, sizeof(g_job_error), "Install in progress.");
        return -1;
    }
    if (!bw_mod_catalog_can_remove(root)) {
        copy_text(g_job_error, sizeof(g_job_error), "This mod is not in the mods folder.");
        return -1;
    }
    copy_text(path, sizeof(path), root);    /* (the row's text goes with the reload) */
    row = menu_row(path);
    if (row >= 0 && (bw_mod_menu_enabled_at((u32)row) || bw_mod_menu_running_at((u32)row))) {
        (void)bw_mod_menu_set_enabled((u32)row, 0);
        (void)bw_mod_menu_apply();
    }
    rc = bw_plat_remove_tree(path);
    if (rc != 0) {
        copy_text(g_job_error, sizeof(g_job_error), "Could not fully remove mod: a file is in use.");
    }
    (void)bw_mod_menu_reload();
    mark_installed();
    fprintf(stderr, "eidos: mods: %s %s (code mods loaded: %u)\n", path, rc == 0 ? "removed" : "not removed whole",
            (unsigned)bw_mod_loader_count());
    return rc;
}
