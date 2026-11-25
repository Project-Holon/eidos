/*
 * sdk/plat_dir.c -- path kind and directory listing, for Win32 and for
 * POSIX (sdk/plat_dir.h says why it exists).
 *
 * Win32 uses GetFileAttributesA and FindFirstFileA/FindNextFileA, the ANSI
 * forms, because the rest of the file layer opens files with narrow fopen
 * and both must agree on what a path names.  Everything else uses stat and
 * opendir/readdir.
 */

/* stat and opendir are POSIX, not C99: ask for them under -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_dir.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int bw_plat_path_kind(const char *path)
{
    DWORD a;

    if (path == NULL || *path == '\0') {
        return BW_PATH_NONE;
    }
    a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) {
        return BW_PATH_NONE;
    }
    if ((a & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return BW_PATH_DIR;
    }
    if ((a & FILE_ATTRIBUTE_DEVICE) != 0) {
        return BW_PATH_OTHER;
    }
    return BW_PATH_FILE;
}

int bw_plat_list_dir(const char *path, int (*fn)(void *ctx, const char *name),
                     void *ctx)
{
    char pattern[1200];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int rc = 0;

    if (path == NULL || strlen(path) + 3 > sizeof(pattern)) {
        return -1;
    }
    sprintf(pattern, "%s/*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        rc = fn(ctx, fd.cFileName);
    } while (rc == 0 && FindNextFileA(h, &fd));
    FindClose(h);
    return rc;
}

/* A link (a junction, a symbolic link): removed as an entry, never
 * entered. */
static int plat_is_link(const char *path)
{
    DWORD a = GetFileAttributesA(path);

    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static int plat_remove_entry(const char *path)
{
    DWORD a = GetFileAttributesA(path);

    if (a == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if ((a & FILE_ATTRIBUTE_READONLY) != 0) {
        (void)SetFileAttributesA(path, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    if ((a & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return RemoveDirectoryA(path) ? 0 : -1;
    }
    return DeleteFileA(path) ? 0 : -1;
}

#else /* POSIX */

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

int bw_plat_path_kind(const char *path)
{
    struct stat st;

    if (path == NULL || *path == '\0' || stat(path, &st) != 0) {
        return BW_PATH_NONE;
    }
    if (S_ISDIR(st.st_mode)) {
        return BW_PATH_DIR;
    }
    if (S_ISREG(st.st_mode)) {
        return BW_PATH_FILE;
    }
    return BW_PATH_OTHER;
}

int bw_plat_list_dir(const char *path, int (*fn)(void *ctx, const char *name),
                     void *ctx)
{
    DIR *d;
    struct dirent *e;
    int rc = 0;

    if (path == NULL || (d = opendir(path)) == NULL) {
        return -1;
    }
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        rc = fn(ctx, e->d_name);
    }
    closedir(d);
    return rc;
}

#include <unistd.h>

static int plat_is_link(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

static int plat_remove_entry(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        return 0;
    }
    return (S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path)) == 0 ? 0 : -1;
}

#endif

/* The names of a folder's entries, one after the other, each with its NUL. */
typedef struct Names {
    char  *text;
    size_t used;
    size_t room;
    int    failed;
} Names;

static int names_add(void *ctx, const char *name)
{
    Names *n = (Names *)ctx;
    size_t len = strlen(name) + 1;

    if (n->used + len > n->room) {
        size_t room = n->room * 2 + len + 256;
        char *text = (char *)realloc(n->text, room);

        if (text == NULL) {
            n->failed = 1;
            return 1;
        }
        n->text = text;
        n->room = room;
    }
    memcpy(n->text + n->used, name, len);
    n->used += len;
    return 0;
}

static int remove_tree(const char *path, int depth)
{
    Names names;
    size_t at;
    int rc = 0;

    if (plat_is_link(path) || bw_plat_path_kind(path) != BW_PATH_DIR) {
        return plat_remove_entry(path);
    }
    if (depth >= 64) {
        return -1;
    }
    /* the names first: a folder is not changed while it is listed */
    memset(&names, 0, sizeof(names));
    if (bw_plat_list_dir(path, names_add, &names) != 0 || names.failed) {
        free(names.text);
        return -1;
    }
    for (at = 0; at < names.used; at += strlen(names.text + at) + 1) {
        size_t len = strlen(path) + strlen(names.text + at) + 2;
        char *child = (char *)malloc(len);

        if (child == NULL) {
            rc = -1;
            break;
        }
        snprintf(child, len, "%s/%s", path, names.text + at);
        if (remove_tree(child, depth + 1) != 0) {
            rc = -1;
        }
        free(child);
    }
    free(names.text);
    return plat_remove_entry(path) != 0 ? -1 : rc;
}

int bw_plat_remove_tree(const char *path)
{
    if (path == NULL || *path == '\0') {
        return -1;
    }
    return remove_tree(path, 0);
}
