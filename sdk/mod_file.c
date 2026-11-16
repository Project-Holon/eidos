/*
 * sdk/mod_file.c -- a mod's own files, mod API version 6
 * (docs/mod-services.md, "Mod files").
 *
 * A mod keeps files that are shared across the player's saves (a stored
 * world, cached data) in its own folder, <save dir>/modfiles/<namespace>/.
 * The mod's saved data (state_store) stays per save, in
 * <save dir>/mods/<namespace>/, so the two never meet.
 *
 * - A name is plain: 1 to 64 of the letters, the digits and . _ -, not
 *   starting with a dot, with no "..", and not a device name of Windows
 *   (CON, NUL, COM1 and the like, with or without an extension).  So a
 *   name never leaves the folder, on any platform.
 * - The quota is 16 MiB for all of a mod's files.  A write that would
 *   pass it answers -1 and changes nothing: the old file stays whole.
 * - A file is written to a temporary name beside it, pushed to the disk
 *   and then put in place in one step (bw_plat_replace_file), so a crash
 *   never leaves half a file.  A temporary left by a crash does not count
 *   and is not listed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "port.h"
#include "save.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "mod_v6.h"

extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;

#define TMP_SUFFIX ".~tmp"

static char g_base[1024];   /* "" : <save dir>/modfiles */

void bw_mod_file_set_base(const char *dir)
{
    snprintf(g_base, sizeof(g_base), "%s", dir != NULL ? dir : "");
}

static const char *calling_ns(void)
{
    return bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : "";
}

int bw_mod_file_name_ok(const char *name)
{
    static const char *const devices[] = {
        "CON", "PRN", "AUX", "NUL", "CLOCK$",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    size_t n, i, stem;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n == 0 || n > BW_MOD_FILE_NAME_MAX || name[0] == '.' ||
        name[n - 1] == '.' || strstr(name, "..") != NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];

        if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    stem = strcspn(name, ".");
    for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        size_t d = strlen(devices[i]), k;

        if (d != stem) {
            continue;
        }
        for (k = 0; k < d; k++) {
            if (toupper((unsigned char)name[k]) != devices[i][k]) {
                break;
            }
        }
        if (k == d) {
            return 0;
        }
    }
    return 1;
}

/* A namespace as a folder name: the state layer's rule. */
static int ns_ok(const char *ns)
{
    size_t i, n = ns != NULL ? strlen(ns) : 0;

    if (n == 0 || n > 64) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = ns[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

/* <base>/<ns>, made when `make` is set.  0 or -1. */
static int mod_dir(const char *ns, char *out, size_t size, int make)
{
    char rel[128];
    int n;

    if (!ns_ok(ns)) {
        return -1;
    }
    if (g_base[0] != '\0') {
        n = snprintf(out, size, "%s/%s", g_base, ns);
        if (n < 0 || (size_t)n >= size) {
            return -1;
        }
    } else {
        snprintf(rel, sizeof(rel), "modfiles/%s", ns);
        if (bw_save_path(out, size, rel) != 0) {
            return -1;
        }
    }
    if (make && bw_plat_mkdirs(out) != 0) {
        return -1;
    }
    return 0;
}

static long file_size(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long end;

    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    end = ftell(fp);
    fclose(fp);
    return end;
}

typedef struct ListCtx {
    const char *dir;
    u64   used;
    u32   files;
    char *out;
    u32   size;
    u32   len;      /* the length the whole list needs */
    int   listing;  /* write the lines */
} ListCtx;

static void put_text(ListCtx *c, const char *s)
{
    size_t n = strlen(s), i;

    for (i = 0; i < n; i++) {
        if (c->out != NULL && c->len + 1 < c->size) {
            c->out[c->len] = s[i];
            c->out[c->len + 1] = '\0';
        }
        c->len++;
    }
}

static int count_one(void *ctx, const char *name)
{
    ListCtx *c = (ListCtx *)ctx;
    char path[1200];
    long sz;

    if (!bw_mod_file_name_ok(name)) {
        return 0;       /* a temporary, or nothing of the mod's */
    }
    snprintf(path, sizeof(path), "%s/%s", c->dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_FILE || (sz = file_size(path)) < 0) {
        return 0;
    }
    c->used += (u64)sz;
    c->files++;
    if (c->listing) {
        char line[128];

        snprintf(line, sizeof(line), "%s %ld\n", name, sz);
        put_text(c, line);
    }
    return 0;
}

/* The bytes the mod's files use. */
static u64 used_bytes(const char *dir)
{
    ListCtx c;

    memset(&c, 0, sizeof(c));
    c.dir = dir;
    if (bw_plat_path_kind(dir) == BW_PATH_DIR) {
        (void)bw_plat_list_dir(dir, count_one, &c);
    }
    return c.used;
}

static void say(const char *ns, const char *what, const char *name)
{
    fprintf(stderr, "eidos: mod %s: %s (%s)\n", ns != NULL ? ns : "?", what,
            name != NULL ? name : "");
}

int bw_mod_file_write_for(const char *ns, const char *name, const void *data, u32 size)
{
    char dir[1024], path[1200], tmp[1200];
    long old;
    u64 used;
    FILE *fp;

    if (!bw_mod_file_name_ok(name) || (data == NULL && size != 0)) {
        say(ns, "mod_file_write: not a plain file name", name);
        return -1;
    }
    if (mod_dir(ns, dir, sizeof(dir), 1) != 0) {
        say(ns, "mod_file_write: no folder for the mod's files", name);
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(tmp, sizeof(tmp), "%s%s", path, TMP_SUFFIX);
    old = bw_plat_path_kind(path) == BW_PATH_FILE ? file_size(path) : 0;
    used = used_bytes(dir);
    if (old > 0 && (u64)old <= used) {
        used -= (u64)old;
    }
    if (used + size > BW_MOD_FILE_QUOTA) {
        say(ns, "mod_file_write: the mod's files would pass 16 MiB", name);
        return -1;
    }
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        return -1;
    }
    if ((size != 0 && fwrite(data, 1, size, fp) != size) || bw_plat_sync_file(fp) != 0) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fclose(fp) != 0 || bw_plat_replace_file(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int bw_mod_file_read_for(const char *ns, const char *name, void *out, u32 size, u32 *got)
{
    char dir[1024], path[1200];
    FILE *fp;
    long sz;
    size_t n = 0;

    if (got != NULL) {
        *got = 0;
    }
    if (!bw_mod_file_name_ok(name) || (out == NULL && size != 0) ||
        mod_dir(ns, dir, sizeof(dir), 0) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_FILE || (sz = file_size(path)) < 0 ||
        sz > 0x7FFFFFFFL) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (size != 0) {
        n = fread(out, 1, (size_t)size < (size_t)sz ? (size_t)size : (size_t)sz, fp);
    }
    fclose(fp);
    if (got != NULL) {
        *got = (u32)n;
    }
    return (int)sz;
}

int bw_mod_file_list_for(const char *ns, char *out, u32 size)
{
    char dir[1024], head[96];
    ListCtx c;

    if ((out == NULL && size != 0) || mod_dir(ns, dir, sizeof(dir), 0) != 0) {
        return -1;
    }
    if (out != NULL && size != 0) {
        out[0] = '\0';
    }
    /* the head line first: the bytes used and the quota */
    snprintf(head, sizeof(head), "used %llu quota %u\n",
             (unsigned long long)used_bytes(dir), BW_MOD_FILE_QUOTA);
    memset(&c, 0, sizeof(c));
    c.dir = dir;
    c.out = out;
    c.size = size;
    c.listing = 1;
    put_text(&c, head);
    if (bw_plat_path_kind(dir) == BW_PATH_DIR) {
        (void)bw_plat_list_dir(dir, count_one, &c);
    }
    return (int)c.len;
}

int bw_mod_file_delete_for(const char *ns, const char *name)
{
    char dir[1024], path[1200];

    if (!bw_mod_file_name_ok(name) || mod_dir(ns, dir, sizeof(dir), 0) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_FILE) {
        return -1;
    }
    return remove(path) == 0 ? 0 : -1;
}

int bw_mod_file_write(const char *name, const void *data, u32 size)
{
    return bw_mod_file_write_for(calling_ns(), name, data, size);
}

int bw_mod_file_read(const char *name, void *out, u32 size, u32 *got)
{
    return bw_mod_file_read_for(calling_ns(), name, out, size, got);
}

int bw_mod_file_list(char *out, u32 size)
{
    return bw_mod_file_list_for(calling_ns(), out, size);
}

int bw_mod_file_delete(const char *name)
{
    return bw_mod_file_delete_for(calling_ns(), name);
}
