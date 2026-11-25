/*
 * install/fsutil.c -- the file system calls the installer needs, with UTF-8
 * paths on every platform.
 *
 * This is the one file in install/ that talks to the operating system
 * beyond stdio; everything OS-specific sits behind the #ifdef below, Win32
 * on one side and POSIX on the other.
 *
 * Why it exists: the C library's fopen/mkdir/rename take the path as bytes
 * in the current code page, which on Windows is not UTF-8 (a player whose
 * user folder is "C:\Users\Andre\Telechargements" with accents could not
 * open their own ROM), and C99 has no way at all to list or remove a
 * directory.  The installer needs both: it writes into a staging directory
 * and renames it into place (so a half-written data root never exists
 * under the real name), and it clears its own leftovers after an
 * interrupted run.
 *
 * Windows paths are widened to UTF-16 and, when long, made absolute with
 * the "\\?\" prefix so the 260-character MAX_PATH limit does not apply
 * (a data root under a OneDrive folder gets close to it).  Removal never
 * follows a link or a junction: it deletes the link, not what it points
 * at.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* lstat, fileno, rmdir under -std=c99 */
#endif

#include <stdlib.h>
#include <string.h>

#include "install.h"

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <wchar.h>
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define WPATH_MAX 4096u

#ifdef _WIN32

/* ------------------------------------------------------------------ */
/* Win32                                                               */
/* ------------------------------------------------------------------ */

/*
 * UTF-8 to UTF-16, then "\\?\" (or "\\?\UNC\") on the absolute form when the
 * path is long enough for MAX_PATH to matter.  0 on success.
 */
static int widen(const char *path, wchar_t *out, size_t n)
{
    wchar_t tmp[WPATH_MAX];
    wchar_t full[WPATH_MAX];
    DWORD len;

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, tmp,
                            (int)WPATH_MAX) == 0) {
        return -1;
    }
    if (wcslen(tmp) < 200 || wcsncmp(tmp, L"\\\\?\\", 4) == 0) {
        if (wcslen(tmp) + 1 > n) {
            return -1;
        }
        wcscpy(out, tmp);
        return 0;
    }
    len = GetFullPathNameW(tmp, WPATH_MAX, full, NULL);
    if (len == 0 || len >= WPATH_MAX) {
        return -1;
    }
    if (full[0] == L'\\' && full[1] == L'\\') {
        if (len + 7 > n) {
            return -1;
        }
        wcscpy(out, L"\\\\?\\UNC\\");
        wcscat(out, full + 2);
    } else {
        if (len + 5 > n) {
            return -1;
        }
        wcscpy(out, L"\\\\?\\");
        wcscat(out, full);
    }
    return 0;
}

/* Antivirus scanners and cloud-sync clients (OneDrive) open files the
 * moment they appear, which makes a rename or delete fail for a moment with
 * a sharing error.  Retry those for up to two seconds. */
static int transient(void)
{
    DWORD e = GetLastError();
    return e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION
        || e == ERROR_LOCK_VIOLATION || e == ERROR_DIR_NOT_EMPTY;
}

FILE *bw_fopen_utf8(const char *path, const char *mode)
{
    wchar_t wpath[WPATH_MAX];
    wchar_t wmode[8];

    if (widen(path, wpath, WPATH_MAX) != 0
        || MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8) == 0) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
}

int bw_path_kind_utf8(const char *path)
{
    wchar_t wpath[WPATH_MAX];
    DWORD attr;

    if (widen(path, wpath, WPATH_MAX) != 0) {
        return BW_PATH_NONE;
    }
    attr = GetFileAttributesW(wpath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return BW_PATH_NONE;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? BW_PATH_DIR : BW_PATH_FILE;
}

static int mkdir_one(const char *path)
{
    wchar_t wpath[WPATH_MAX];

    if (widen(path, wpath, WPATH_MAX) != 0) {
        return -1;
    }
    if (CreateDirectoryW(wpath, NULL)) {
        return 0;
    }
    return bw_path_kind_utf8(path) == BW_PATH_DIR ? 0 : -1;
}

int bw_dir_is_empty_utf8(const char *path)
{
    wchar_t pat[WPATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int empty = 1;
    size_t n;

    if (widen(path, pat, WPATH_MAX - 3) != 0) {
        return -1;
    }
    n = wcslen(pat);
    if (n > 0 && pat[n - 1] != L'\\' && pat[n - 1] != L'/') {
        pat[n++] = L'\\';
    }
    pat[n++] = L'*';
    pat[n] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            empty = 0;
        }
    } while (empty && FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

int bw_rename_utf8(const char *from, const char *to)
{
    wchar_t wf[WPATH_MAX];
    wchar_t wt[WPATH_MAX];
    int tries;

    if (widen(from, wf, WPATH_MAX) != 0 || widen(to, wt, WPATH_MAX) != 0) {
        return -1;
    }
    if (GetFileAttributesW(wt) != INVALID_FILE_ATTRIBUTES) {
        return -1;                      /* never replace */
    }
    for (tries = 0; tries < 40; tries++) {
        if (MoveFileExW(wf, wt, 0)) {
            return 0;
        }
        if (!transient()) {
            return -1;
        }
        Sleep(50);
    }
    return -1;
}

/* rm -r on a wide path held in `buf` (WPATH_MAX long, `len` used). */
static int remove_tree_w(wchar_t *buf, size_t len)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int rc = 0;
    int tries;

    if (len + 3 >= WPATH_MAX) {
        return -1;
    }
    buf[len] = L'\\';
    buf[len + 1] = L'*';
    buf[len + 2] = 0;
    h = FindFirstFileW(buf, &fd);
    buf[len] = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            size_t nl = wcslen(fd.cFileName);
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (len + 1 + nl + 1 >= WPATH_MAX) {
                rc = -1;
                continue;
            }
            buf[len] = L'\\';
            wcscpy(buf + len + 1, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                if (remove_tree_w(buf, len + 1 + nl) != 0) {
                    rc = -1;
                }
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!RemoveDirectoryW(buf)) {   /* a junction: the link only */
                    rc = -1;
                }
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    SetFileAttributesW(buf, fd.dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
                }
                for (tries = 0; !DeleteFileW(buf); tries++) {
                    if (!transient() || tries == 40) {
                        rc = -1;
                        break;
                    }
                    Sleep(50);
                }
            }
            buf[len] = 0;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (tries = 0; !RemoveDirectoryW(buf); tries++) {
        if (!transient() || tries == 40) {
            return -1;
        }
        Sleep(50);
    }
    return rc;
}

int bw_remove_tree_utf8(const char *path)
{
    wchar_t buf[WPATH_MAX];
    DWORD attr;
    size_t n;

    if (widen(path, buf, WPATH_MAX) != 0) {
        return -1;
    }
    attr = GetFileAttributesW(buf);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileW(buf) ? 0 : -1;
    }
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        return RemoveDirectoryW(buf) ? 0 : -1;
    }
    n = wcslen(buf);
    while (n > 0 && (buf[n - 1] == L'\\' || buf[n - 1] == L'/')) {
        buf[--n] = 0;
    }
    return remove_tree_w(buf, n);
}

int bw_list_dir_utf8(const char *path,
                     int (*fn)(void *ctx, const char *name, int kind), void *ctx)
{
    wchar_t buf[WPATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    size_t len;
    int rc = 0;

    if (widen(path, buf, WPATH_MAX) != 0) {
        return -1;
    }
    len = wcslen(buf);
    while (len > 0 && (buf[len - 1] == L'\\' || buf[len - 1] == L'/')) {
        buf[--len] = 0;
    }
    if (len + 3 >= WPATH_MAX) {
        return -1;
    }
    wcscpy(buf + len, L"\\*");
    h = FindFirstFileW(buf, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        char name[1024];

        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0
            || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, (int)sizeof(name),
                                NULL, NULL) == 0) {
            continue;
        }
        rc = fn(ctx, name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? BW_PATH_DIR
                                                                             : BW_PATH_FILE);
    } while (rc == 0 && FindNextFileW(h, &fd));
    FindClose(h);
    return rc;
}

int bw_file_size(FILE *fp, u64 *size)
{
    struct _stati64 st;

    if (_fstati64(_fileno(fp), &st) != 0 || st.st_size < 0) {
        return -1;
    }
    *size = (u64)st.st_size;
    return 0;
}

/* The free bytes on the volume of an existing path. */
static int free_space_one(const char *path, u64 *bytes)
{
    wchar_t wpath[WPATH_MAX];
    ULARGE_INTEGER avail;

    if (widen(path, wpath, WPATH_MAX) != 0
        || !GetDiskFreeSpaceExW(wpath, &avail, NULL, NULL)) {
        return -1;
    }
    *bytes = (u64)avail.QuadPart;
    return 0;
}

#else

/* ------------------------------------------------------------------ */
/* POSIX                                                               */
/* ------------------------------------------------------------------ */

FILE *bw_fopen_utf8(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int bw_path_kind_utf8(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return BW_PATH_NONE;
    }
    return S_ISDIR(st.st_mode) ? BW_PATH_DIR : BW_PATH_FILE;
}

static int mkdir_one(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    return (errno == EEXIST && bw_path_kind_utf8(path) == BW_PATH_DIR) ? 0 : -1;
}

int bw_dir_is_empty_utf8(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int empty = 1;

    if (d == NULL) {
        return -1;
    }
    while (empty && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            empty = 0;
        }
    }
    closedir(d);
    return empty;
}

int bw_rename_utf8(const char *from, const char *to)
{
    struct stat st;

    if (lstat(to, &st) == 0) {
        return -1;                      /* never replace */
    }
    return rename(from, to) == 0 ? 0 : -1;
}

int bw_remove_tree_utf8(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;
    char *names = NULL;
    size_t used = 0, cap = 0, at;
    int rc = 0, failed = 0;

    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0 ? 0 : -1;  /* a file or a link: never followed */
    }
    d = opendir(path);
    if (d == NULL) {
        return -1;
    }
    /* The names first, the removals after: a folder that changes while it
     * is read can skip entries.  The wasm host lists a folder again for
     * each part of a long listing and counts on from where it was, so
     * entries removed in between moved the rest forward and half of a
     * large folder was left (the overlays of a replaced extraction). */
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name) + 1;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (used + n > cap) {
            size_t want = cap != 0 ? cap * 2 : 4096;
            char *grown;

            while (want < used + n) {
                want *= 2;
            }
            grown = (char *)realloc(names, want);
            if (grown == NULL) {
                rc = -1;
                break;
            }
            names = grown;
            cap = want;
        }
        memcpy(names + used, e->d_name, n);
        used += n;
    }
    closedir(d);
    for (at = 0; rc == 0 && at < used; at += strlen(names + at) + 1) {
        size_t n = strlen(path) + 1 + strlen(names + at) + 1;
        char *child = (char *)malloc(n);

        if (child == NULL) {
            rc = -1;
            break;
        }
        snprintf(child, n, "%s/%s", path, names + at);
        if (bw_remove_tree_utf8(child) != 0) {
            failed = -1;
        }
        free(child);
    }
    free(names);
    if (rc == 0) {
        rc = failed;
    }
    if (rmdir(path) != 0) {
        rc = -1;
    }
    return rc;
}

int bw_list_dir_utf8(const char *path,
                     int (*fn)(void *ctx, const char *name, int kind), void *ctx)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int rc = 0;

    if (d == NULL) {
        return -1;
    }
    while (rc == 0 && (e = readdir(d)) != NULL) {
        struct stat st;
        size_t n;
        char *child;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        n = strlen(path) + 1 + strlen(e->d_name) + 1;
        child = (char *)malloc(n);
        if (child == NULL) {
            rc = -1;
            break;
        }
        snprintf(child, n, "%s/%s", path, e->d_name);
        if (lstat(child, &st) == 0 && (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
            rc = fn(ctx, e->d_name, S_ISDIR(st.st_mode) ? BW_PATH_DIR : BW_PATH_FILE);
        }
        free(child);
    }
    closedir(d);
    return rc;
}

int bw_file_size(FILE *fp, u64 *size)
{
    struct stat st;

    if (fstat(fileno(fp), &st) != 0 || st.st_size < 0) {
        return -1;
    }
    *size = (u64)st.st_size;
    return 0;
}

/* The free bytes on the volume of an existing path, for an unprivileged
 * user (f_bavail, not f_bfree, which counts the blocks kept for root). */
static int free_space_one(const char *path, u64 *bytes)
{
    struct statvfs sv;

    if (statvfs(path, &sv) != 0) {
        return -1;
    }
    *bytes = (u64)sv.f_bavail * (u64)sv.f_frsize;
    return 0;
}

#endif

/* ------------------------------------------------------------------ */
/* Both                                                                */
/* ------------------------------------------------------------------ */

int bw_mkdirs_utf8(const char *path)
{
    char *buf;
    size_t n = strlen(path);
    size_t i;
    int rc = 0;

    if (n == 0) {
        return -1;
    }
    buf = (char *)malloc(n + 1);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    /* A prefix that cannot be created ("C:", a UNC "//server", a folder
     * the user may enter but not list) is not an error by itself: the
     * last component decides. */
    for (i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];
            buf[i] = '\0';
            (void)mkdir_one(buf);
            buf[i] = c;
        }
    }
    rc = mkdir_one(buf);
    free(buf);
    return rc;
}

/* One file; the destination must not exist. */
static int copy_file(const char *from, const char *to)
{
    enum { CHUNK = 1 << 16 };
    char *buf = (char *)malloc(CHUNK);
    FILE *in = bw_fopen_utf8(from, "rb");
    FILE *out;
    size_t got;
    int bad = 0;

    if (in == NULL || buf == NULL) {
        if (in != NULL) {
            fclose(in);
        }
        free(buf);
        return -1;
    }
    out = bw_fopen_utf8(to, "wb");
    if (out == NULL) {
        fclose(in);
        free(buf);
        return -1;
    }
    while ((got = fread(buf, 1, CHUNK, in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            bad = 1;
            break;
        }
    }
    bad |= ferror(in);
    fclose(in);
    free(buf);
    if (fclose(out) != 0 || bad) {
        (void)bw_remove_tree_utf8(to);      /* never a part of a file */
        return -1;
    }
    return 0;
}

typedef struct CopyWalk {
    const char *from;
    const char *to;
    int failed;
} CopyWalk;

static int copy_entry(void *ctx, const char *name, int kind)
{
    CopyWalk *w = (CopyWalk *)ctx;
    size_t nf = strlen(w->from) + 1 + strlen(name) + 1;
    size_t nt = strlen(w->to) + 1 + strlen(name) + 1;
    char *src = (char *)malloc(nf);
    char *dst = (char *)malloc(nt);

    if (src == NULL || dst == NULL) {
        w->failed = 1;
    } else {
        snprintf(src, nf, "%s/%s", w->from, name);
        snprintf(dst, nt, "%s/%s", w->to, name);
        if (kind == BW_PATH_DIR) {
            if (bw_copy_tree_utf8(src, dst) != 0) {
                w->failed = 1;
            }
        } else if (bw_path_kind_utf8(dst) == BW_PATH_NONE && copy_file(src, dst) != 0) {
            w->failed = 1;
        }
    }
    free(src);
    free(dst);
    return 0;       /* the other entries are still copied */
}

int bw_copy_tree_utf8(const char *from, const char *to)
{
    CopyWalk w;

    if (bw_path_kind_utf8(from) != BW_PATH_DIR || bw_mkdirs_utf8(to) != 0) {
        return -1;
    }
    w.from = from;
    w.to = to;
    w.failed = 0;
    if (bw_list_dir_utf8(from, copy_entry, &w) != 0 || w.failed) {
        return -1;
    }
    return 0;
}

int bw_free_space_utf8(const char *path, u64 *bytes)
{
    char *buf;
    size_t n = strlen(path);
    int rc = -1;

    buf = (char *)malloc(n + 4);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (;;) {
        if (n > 0 && bw_path_kind_utf8(buf) != BW_PATH_NONE) {
            rc = free_space_one(buf, bytes);
            break;
        }
        /* Up one level: drop the last component, then its separators. */
        while (n > 0 && buf[n - 1] != '/' && buf[n - 1] != '\\') {
            n--;
        }
        if (n == 0) {
            /* A relative name with no folder part: the current folder. */
            rc = free_space_one(".", bytes);
            break;
        }
        while (n > 1 && (buf[n - 1] == '/' || buf[n - 1] == '\\')) {
            n--;
        }
        buf[n] = '\0';
        if (n == 2 && buf[1] == ':') {
            buf[2] = '\\';              /* "C:" is the drive's cwd; ask "C:\" */
            buf[3] = '\0';
            n = 3;
        }
    }
    free(buf);
    return rc;
}
