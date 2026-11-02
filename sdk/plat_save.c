/*
 * sdk/plat_save.c -- the save folder, mkdir, sync and replace, for Win32 and
 * for POSIX (sdk/plat_save.h says why it exists).
 *
 * Win32 uses the ANSI forms (CreateDirectoryA, MoveFileExA,
 * GetModuleFileNameA) for the reason sdk/plat_dir.c gives: the rest of the
 * file layer opens files with narrow fopen and both must agree on what a
 * path names.
 */

/* mkdir and fsync are POSIX, not C99: ask for them under -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_dir.h"
#include "plat_save.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>

static int join(char *out, size_t out_size, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);

    if (la + lb + 1 > out_size)
        return -1;
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1);
    return 0;
}

int bw_plat_default_save_dir(char *out, size_t out_size)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);

    /* A portable.txt beside the executable keeps everything beside it, as
     * it does for the data root (docs/installer.md).                       */
    if (n > 0 && n < sizeof exe) {
        char *slash = strrchr(exe, '\\');
        char probe[MAX_PATH + 16];

        if (slash != NULL) {
            slash[1] = '\0';
            if (join(probe, sizeof probe, exe, "portable.txt") == 0
                && bw_plat_path_kind(probe) == BW_PATH_FILE)
                return join(out, out_size, exe, "saves");
        }
    }
    {
        const char *appdata = getenv("APPDATA");

        if (appdata == NULL || appdata[0] == '\0')
            return -1;
        if (strlen(appdata) + 14 > out_size)
            return -1;
        sprintf(out, "%s\\eidos\\saves", appdata);
    }
    return 0;
}

static int make_one(const char *path)
{
    if (CreateDirectoryA(path, NULL))
        return 0;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}

int bw_plat_sync_file(FILE *f)
{
    if (fflush(f) != 0)
        return -1;
    return _commit(_fileno(f)) == 0 ? 0 : -1;
}

int bw_plat_replace_file(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING |
                                 MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}

#else /* POSIX */

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int bw_plat_default_save_dir(char *out, size_t out_size)
{
#if defined(__wasm__)
    /* The platform's rule, which only the host knows (sdk/wasm_host.h). */
    return bw_host_os_path(BW_HOST_PATH_SAVE_DIR, out, (unsigned)out_size);
#elif defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    /* App-private storage is known only to the Java/Objective-C side: the
     * front end calls bw_set_save_dir (docs/installer.md).                 */
    (void)out;
    (void)out_size;
    return -1;
#else
    const char *home = getenv("HOME");
#if defined(__APPLE__)
    if (home == NULL || home[0] == '\0')
        return -1;
    if (strlen(home) + 40 > out_size)
        return -1;
    sprintf(out, "%s/Library/Application Support/eidos/saves", home);
#else
    const char *xdg = getenv("XDG_DATA_HOME");

    if (xdg != NULL && xdg[0] == '/') {
        if (strlen(xdg) + 14 > out_size)
            return -1;
        sprintf(out, "%s/eidos/saves", xdg);
        return 0;
    }
    if (home == NULL || home[0] == '\0')
        return -1;
    if (strlen(home) + 28 > out_size)
        return -1;
    sprintf(out, "%s/.local/share/eidos/saves", home);
#endif
    return 0;
#endif
}

static int make_one(const char *path)
{
    if (mkdir(path, 0777) == 0)
        return 0;
    return errno == EEXIST ? 0 : -1;
}

int bw_plat_sync_file(FILE *f)
{
    if (fflush(f) != 0)
        return -1;
    return fsync(fileno(f)) == 0 ? 0 : -1;
}

int bw_plat_replace_file(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}

#endif

int bw_plat_mkdirs(const char *path)
{
    char buf[1024];
    size_t i, n;

    if (path == NULL || path[0] == '\0')
        return -1;
    n = strlen(path);
    if (n >= sizeof buf)
        return -1;
    memcpy(buf, path, n + 1);
    /* Every separator after the first character ends a parent (a leading
     * one is the root; "C:" before the first is a drive, which exists).    */
    for (i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];

            if (buf[i - 1] == ':' || buf[i - 1] == '/' || buf[i - 1] == '\\')
                continue;
            buf[i] = '\0';
            /* an ancestor that cannot be made is let be: on iOS the
             * sandbox's own parents exist but can be neither looked at nor
             * made; a real failure shows at the last step */
            if (bw_plat_path_kind(buf) != BW_PATH_DIR)
                (void)make_one(buf);
            buf[i] = c;
        }
    }
    if (bw_plat_path_kind(buf) == BW_PATH_DIR)
        return 0;
    return make_one(buf);
}
