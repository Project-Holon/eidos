/*
 * host/wasi.c -- WASI preview 1 for the module: what its C library asks of
 * the system (files, directories, the clock, the arguments, the
 * environment), answered with this machine's own.
 *
 * One directory is opened for the module before it starts: fd 3, "/", the
 * root of every path.  The module's C library makes a path absolute against
 * its working directory ("/") and hands over the part after the root, so
 * "C:/Users/x/eidos" arrives as "C:/Users/x/eidos" on Windows and
 * "home/x/eidos" arrives for "/home/x/eidos" elsewhere: the host puts the
 * root back (nothing on Windows, "/" elsewhere).  Paths are UTF-8 both
 * ways.
 *
 * Only what eidos uses is here; anything else answers ENOSYS.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <bcrypt.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#if defined(__SWITCH__)
#include <switch.h>
#endif

/* errno values of WASI */
#define W_OK_        0
#define W_EACCES     2
#define W_EBADF      8
#define W_EEXIST     20
#define W_EINVAL     28
#define W_EIO        29
#define W_EISDIR     31
#define W_ENOENT     44
#define W_ENOSYS     52
#define W_ENOTDIR    54
#define W_ENOTEMPTY  55
#define W_ESPIPE     70
#define W_EFAULT     21

/* file types */
#define FT_CHAR 2
#define FT_DIR  3
#define FT_REG  4

#define FDS_MAX 256

typedef struct {
    int used;
    int is_dir;
    int native;                 /* a file's native descriptor               */
    char *path;                 /* a directory's full path ("" the root)    */
    int append;
} Fd;

static Fd g_fd[FDS_MAX];
static char **g_environ;

static wasm_rt_memory_t *mem(void) { return w2c_game_memory(&g_game); }

static int span_ok(u32 addr, u64 len)
{
    return (u64)addr + len <= mem()->size;
}

static u8 *P(u32 addr) { return mem()->data + addr; }

static void st32(u32 a, u32 v) { memcpy(P(a), &v, 4); }
static void st64(u32 a, u64 v) { memcpy(P(a), &v, 8); }
static u32 ld32(u32 a) { u32 v; memcpy(&v, P(a), 4); return v; }

static int errno_wasi(int e)
{
    switch (e) {
    case ENOENT: return W_ENOENT;
    case EEXIST: return W_EEXIST;
    case EACCES: return W_EACCES;
    case EBADF: return W_EBADF;
    case EISDIR: return W_EISDIR;
    case ENOTDIR: return W_ENOTDIR;
    case ENOTEMPTY: return W_ENOTEMPTY;
    case EINVAL: return W_EINVAL;
    default: return W_EIO;
    }
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

/* The native path of `rel` under directory fd `dir`, malloc'd, or NULL. */
static char *full_path(u32 dir, u32 path, u32 len)
{
    const char *base;
    char *out;
    size_t bl;

    if (dir >= FDS_MAX || !g_fd[dir].used || !g_fd[dir].is_dir || !span_ok(path, len)) {
        return NULL;
    }
    base = g_fd[dir].path;
    bl = strlen(base);
    out = (char *)malloc(bl + len + 2);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, base, bl);
    if (bl > 0 && base[bl - 1] != '/' && len > 0) {
        out[bl++] = '/';
    }
    memcpy(out + bl, P(path), len);
    out[bl + len] = 0;
#if defined(__SWITCH__)
    {
        /* A path that names its device ("sdmc:/switch/eidos/profile", the
         * host's own folder) is whole: the root's "/" before it would ask
         * newlib for a device called "/sdmc". */
        const char *s = out;
        const char *colon, *slash;

        while (*s == '/') {
            s++;
        }
        colon = strchr(s, ':');
        slash = strchr(s, '/');
        if (colon != NULL && colon > s && (slash == NULL || colon < slash)) {
            memmove(out, s, strlen(s) + 1);
            return out;
        }
    }
#endif
#if !defined(_WIN32)
    if (out[0] != '/') {
        char *abs = (char *)malloc(strlen(out) + 2);

        if (abs != NULL) {
            abs[0] = '/';
            strcpy(abs + 1, out);
        }
        free(out);
        out = abs;
    }
#endif
    return out;
}

#if defined(_WIN32)
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(n > 0 ? n : 1) * sizeof(wchar_t));

    if (w != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n > 0 ? n : 1);
    }
    return w;
}
#endif

#if defined(__ANDROID__)
/* The ROM a player picks on Android is a descriptor the host already holds
 * (host/pick_mobile.c), named /proc/self/fd/<n>.  Reopening that path is
 * refused (the file belongs to another app's provider), so the host uses
 * the descriptor itself.  -1 for any other path. */
static int held_fd(const char *p)
{
    char *end;
    long n;

    if (strncmp(p, "/proc/self/fd/", 14) != 0) {
        return -1;
    }
    n = strtol(p + 14, &end, 10);
    return end != p + 14 && *end == 0 && n >= 0 ? (int)n : -1;
}
#endif

static int native_stat(const char *p, struct stat *st)
{
#if defined(__ANDROID__)
    if (held_fd(p) >= 0) {
        return fstat(held_fd(p), st);
    }
#endif
#if defined(_WIN32)
    wchar_t *w = wide(p);
    struct _stat64 s64;
    int rc = w != NULL ? _wstat64(w, &s64) : -1;

    free(w);
    if (rc == 0) {
        memset(st, 0, sizeof(*st));
        st->st_mode = s64.st_mode;
        st->st_size = (off_t)s64.st_size;
        st->st_mtime = (time_t)s64.st_mtime;
        st->st_atime = (time_t)s64.st_atime;
        st->st_ctime = (time_t)s64.st_ctime;
    }
    return rc;
#else
    return stat(p, st);
#endif
}

static void filestat_out(u32 buf, int type, u64 size, u64 mt)
{
    memset(P(buf), 0, 64);
    P(buf)[16] = (u8)type;
    st64(buf + 24, 1);
    st64(buf + 32, size);
    st64(buf + 40, mt);
    st64(buf + 48, mt);
    st64(buf + 56, mt);
}

static int alloc_fd(void)
{
    int i;

    for (i = 4; i < FDS_MAX; i++) {
        if (!g_fd[i].used) {
            memset(&g_fd[i], 0, sizeof(g_fd[i]));
            g_fd[i].used = 1;
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Arguments, environment, clock, random                              */
/* ------------------------------------------------------------------ */

u32 w2c_wasi__snapshot__preview1_args_sizes_get(struct w2c_wasi__snapshot__preview1 *w, u32 argc, u32 size)
{
    u32 n = 0;
    int i;

    for (i = 0; i < w->argc; i++) {
        n += (u32)strlen(w->argv[i]) + 1;
    }
    st32(argc, (u32)w->argc);
    st32(size, n);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_args_get(struct w2c_wasi__snapshot__preview1 *w, u32 argv, u32 buf)
{
    int i;

    for (i = 0; i < w->argc; i++) {
        size_t n = strlen(w->argv[i]) + 1;

        st32(argv + 4u * (u32)i, buf);
        memcpy(P(buf), w->argv[i], n);
        buf += (u32)n;
    }
    return 0;
}

static void env_load(void)
{
    if (g_environ != NULL) {
        return;
    }
#if defined(_WIN32)
    {
        wchar_t *block = GetEnvironmentStringsW(), *e;
        size_t count = 0, k = 0;

        for (e = block; *e; e += wcslen(e) + 1) {
            count++;
        }
        g_environ = (char **)calloc(count + 1, sizeof(char *));
        for (e = block; g_environ != NULL && *e; e += wcslen(e) + 1) {
            int n;

            if (e[0] == L'=') {
                continue;           /* the drive-letter cwd pseudo-variables */
            }
            n = WideCharToMultiByte(CP_UTF8, 0, e, -1, NULL, 0, NULL, NULL);
            g_environ[k] = (char *)malloc((size_t)n);
            if (g_environ[k] != NULL) {
                WideCharToMultiByte(CP_UTF8, 0, e, -1, g_environ[k], n, NULL, NULL);
                k++;
            }
        }
        FreeEnvironmentStringsW(block);
    }
#else
    {
        extern char **environ;
        size_t count = 0, k;

        while (environ[count] != NULL) {
            count++;
        }
        g_environ = (char **)calloc(count + 1, sizeof(char *));
        for (k = 0; g_environ != NULL && k < count; k++) {
            g_environ[k] = strdup(environ[k]);
        }
    }
#endif
}

u32 w2c_wasi__snapshot__preview1_environ_sizes_get(struct w2c_wasi__snapshot__preview1 *w, u32 count, u32 size)
{
    u32 n = 0, c = 0;

    (void)w;
    env_load();
    for (; g_environ != NULL && g_environ[c] != NULL; c++) {
        n += (u32)strlen(g_environ[c]) + 1;
    }
    st32(count, c);
    st32(size, n);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_environ_get(struct w2c_wasi__snapshot__preview1 *w, u32 env, u32 buf)
{
    u32 c;

    (void)w;
    env_load();
    for (c = 0; g_environ != NULL && g_environ[c] != NULL; c++) {
        size_t n = strlen(g_environ[c]) + 1;

        st32(env + 4u * c, buf);
        memcpy(P(buf), g_environ[c], n);
        buf += (u32)n;
    }
    return 0;
}

static u64 now_ns(u32 id)
{
#if defined(_WIN32)
    if (id == 0) {                  /* realtime */
        FILETIME ft;
        u64 t;

        GetSystemTimePreciseAsFileTime(&ft);
        t = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return (t - 116444736000000000ull) * 100u;
    } else {
        LARGE_INTEGER c, f;

        QueryPerformanceCounter(&c);
        QueryPerformanceFrequency(&f);
        return (u64)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
    }
#else
    struct timespec ts;

    clock_gettime(id == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000u + (u64)ts.tv_nsec;
#endif
}

u32 w2c_wasi__snapshot__preview1_clock_time_get(struct w2c_wasi__snapshot__preview1 *w, u32 id, u64 prec, u32 out)
{
    (void)w;
    (void)prec;
    st64(out, now_ns(id));
    return 0;
}

u32 w2c_wasi__snapshot__preview1_clock_res_get(struct w2c_wasi__snapshot__preview1 *w, u32 id, u32 out)
{
    (void)w;
    (void)id;
    st64(out, 1000u);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_random_get(struct w2c_wasi__snapshot__preview1 *w, u32 buf, u32 len)
{
    (void)w;
    if (!span_ok(buf, len)) {
        return W_EFAULT;
    }
#if defined(_WIN32)
    BCryptGenRandom(NULL, P(buf), len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    {
        FILE *f = fopen("/dev/urandom", "rb");

        if (f == NULL || fread(P(buf), 1, len, f) != len) {
            if (f != NULL) {
                fclose(f);
            }
            return W_EIO;
        }
        fclose(f);
    }
#endif
    return 0;
}

void w2c_wasi__snapshot__preview1_proc_exit(struct w2c_wasi__snapshot__preview1 *w, u32 code)
{
    fflush(stdout);
    fflush(stderr);
    /* The module may exit from any fiber: leave the program here. */
    exit((int)code);
    (void)w;
}

u32 w2c_wasi__snapshot__preview1_sched_yield(struct w2c_wasi__snapshot__preview1 *w)
{
    (void)w;
    return 0;
}

/* poll_oneoff: only clock subscriptions (sleep), which is what the module's
 * C library uses it for. */
u32 w2c_wasi__snapshot__preview1_poll_oneoff(struct w2c_wasi__snapshot__preview1 *w, u32 in, u32 out, u32 n, u32 nev)
{
    u32 i, done = 0;
    u64 wait = 0;

    (void)w;
    for (i = 0; i < n; i++) {
        u32 s = in + i * 48u;
        u8 tag = P(s)[8];

        if (tag == 0) {             /* clock */
            u64 t;
            u16 flags;

            memcpy(&t, P(s + 24), 8);
            memcpy(&flags, P(s + 40), 2);
            if (flags & 1u) {       /* absolute */
                u64 now = now_ns(1);

                t = t > now ? t - now : 0;
            }
            if (wait == 0 || t < wait) {
                wait = t;
            }
        }
    }
    if (wait > 0) {
        w2c_eidos_sleep_ns(&g_eidos, wait);
    }
    for (i = 0; i < n; i++) {
        u32 s = in + i * 48u, e = out + done * 32u;

        memset(P(e), 0, 32);
        memcpy(P(e), P(s), 8);      /* userdata */
        P(e)[10] = P(s)[8];         /* type */
        done++;
    }
    st32(nev, done);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

u32 w2c_wasi__snapshot__preview1_fd_prestat_get(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf)
{
    (void)w;
    if (fd != 3) {
        return W_EBADF;
    }
    memset(P(buf), 0, 8);
    st32(buf + 4, 1);               /* "/" */
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_prestat_dir_name(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 path, u32 len)
{
    (void)w;
    if (fd != 3 || len < 1) {
        return W_EBADF;
    }
    P(path)[0] = '/';
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_get(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf)
{
    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used) {
        return W_EBADF;
    }
    memset(P(buf), 0, 24);
    P(buf)[0] = (u8)(fd < 3 ? FT_CHAR : g_fd[fd].is_dir ? FT_DIR : FT_REG);
    if (g_fd[fd].append) {
        P(buf)[2] = 1;
    }
    st64(buf + 8, ~0ull);
    st64(buf + 16, ~0ull);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 flags)
{
    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used) {
        return W_EBADF;
    }
    g_fd[fd].append = (flags & 1u) != 0;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_renumber(struct w2c_wasi__snapshot__preview1 *w, u32 from, u32 to)
{
    (void)w;
    if (from >= FDS_MAX || to >= FDS_MAX || !g_fd[from].used || to < 4) {
        return W_EBADF;
    }
    if (g_fd[to].used) {
        w2c_wasi__snapshot__preview1_fd_close(w, to);
    }
    g_fd[to] = g_fd[from];
    memset(&g_fd[from], 0, sizeof(g_fd[from]));
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1 *w, u32 fd)
{
    (void)w;
    if (fd < 4 || fd >= FDS_MAX || !g_fd[fd].used) {
        return fd < 4 ? 0 : W_EBADF;
    }
    if (!g_fd[fd].is_dir) {
#if defined(_WIN32)
        _close(g_fd[fd].native);
#else
        close(g_fd[fd].native);
#endif
    }
    free(g_fd[fd].path);
    memset(&g_fd[fd], 0, sizeof(g_fd[fd]));
    return 0;
}

static long long io_rw_here(u32 fd, u32 iovs, u32 n, int wr, long long at)
{
    long long total = 0;
    u32 i;

    if (fd >= FDS_MAX || !g_fd[fd].used || g_fd[fd].is_dir) {
        return -W_EBADF;
    }
    if (!span_ok(iovs, (u64)n * 8u)) {
        return -W_EFAULT;
    }
#if defined(_WIN32)
    if (at >= 0 && _lseeki64(g_fd[fd].native, at, SEEK_SET) < 0) {
        return -W_EIO;
    }
#else
    if (at >= 0 && lseek(g_fd[fd].native, (off_t)at, SEEK_SET) < 0) {
        return -W_EIO;
    }
#endif
    for (i = 0; i < n; i++) {
        u32 b = ld32(iovs + i * 8u), l = ld32(iovs + i * 8u + 4u);
        long long r;

        if (!span_ok(b, l)) {
            return -W_EFAULT;
        }
        if (l == 0) {
            continue;
        }
        if (fd == 1 || fd == 2) {
            r = (long long)fwrite(P(b), 1, l, fd == 1 ? stdout : stderr);
            if (fd == 2) {
                fflush(stderr);
            }
        } else if (fd == 0) {
            r = (long long)fread(P(b), 1, l, stdin);
        } else {
#if defined(_WIN32)
            r = wr ? _write(g_fd[fd].native, P(b), l) : _read(g_fd[fd].native, P(b), l);
#else
            r = wr ? write(g_fd[fd].native, P(b), l) : read(g_fd[fd].native, P(b), l);
#endif
        }
        if (r < 0) {
            return -errno_wasi(errno);
        }
        total += r;
        if ((u32)r < l) {
            break;
        }
    }
    return total;
}

/* fd_pread and fd_pwrite leave the file position where it was (POSIX
 * pread); the C library mixes them with plain reads on one file. */
static long long io_rw(u32 fd, u32 iovs, u32 n, int wr, long long at)
{
    long long r, pos = -1, from = -1;
    int trace = !wr && fd > 2 && fd < FDS_MAX && g_fd[fd].used && !g_fd[fd].is_dir
                && getenv("EIDOS_DEBUG_IO") != NULL;

    if ((at >= 0 || trace) && fd > 2 && fd < FDS_MAX && g_fd[fd].used && !g_fd[fd].is_dir) {
#if defined(_WIN32)
        pos = _lseeki64(g_fd[fd].native, 0, SEEK_CUR);
#else
        pos = (long long)lseek(g_fd[fd].native, 0, SEEK_CUR);
#endif
        from = at >= 0 ? at : pos;
        if (at < 0) {
            pos = -1;               /* a plain read moves the position on */
        }
    }
    r = io_rw_here(fd, iovs, n, wr, at);
    if (trace && r >= 0) {
        /* EIDOS_DEBUG_IO: each read, its place, its length and an FNV-1a
         * hash of the bytes, to compare two machines */
        u32 h = 2166136261u, i, left = (u32)r;

        for (i = 0; i < n && left > 0; i++) {
            u32 b = ld32(iovs + i * 8u), l = ld32(iovs + i * 8u + 4u), k;
            const u8 *d = (const u8 *)P(b);

            if (l > left) {
                l = left;
            }
            for (k = 0; k < l; k++) {
                h = (h ^ d[k]) * 16777619u;
            }
            left -= l;
        }
        fprintf(stderr, "io read fd %u at %lld %s len %lld hash %08X\n", fd, from,
                at >= 0 ? "pread" : "read", r, h);
    }
    if (pos >= 0) {
#if defined(_WIN32)
        _lseeki64(g_fd[fd].native, pos, SEEK_SET);
#else
        lseek(g_fd[fd].native, (off_t)pos, SEEK_SET);
#endif
    }
    return r;
}

u32 w2c_wasi__snapshot__preview1_fd_read(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u32 out)
{
    long long r = io_rw(fd, iovs, n, 0, -1);

    (void)w;
    if (r < 0) {
        return (u32)-r;
    }
    st32(out, (u32)r);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_write(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u32 out)
{
    long long r = io_rw(fd, iovs, n, 1, -1);

    (void)w;
    if (r < 0) {
        return (u32)-r;
    }
    st32(out, (u32)r);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_pread(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u64 at, u32 out)
{
    long long r = io_rw(fd, iovs, n, 0, (long long)at);

    (void)w;
    if (r < 0) {
        return (u32)-r;
    }
    st32(out, (u32)r);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_pwrite(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 iovs, u32 n, u64 at, u32 out)
{
    long long r = io_rw(fd, iovs, n, 1, (long long)at);

    (void)w;
    if (r < 0) {
        return (u32)-r;
    }
    st32(out, (u32)r);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_seek(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u64 off, u32 whence, u32 out)
{
    long long r;

    (void)w;
    if (fd < 3) {
        return W_ESPIPE;
    }
    if (fd >= FDS_MAX || !g_fd[fd].used || g_fd[fd].is_dir) {
        return W_EBADF;
    }
#if defined(_WIN32)
    r = _lseeki64(g_fd[fd].native, (long long)off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
#else
    r = (long long)lseek(g_fd[fd].native, (off_t)(long long)off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
#endif
    if (r < 0) {
        return errno_wasi(errno);
    }
    st64(out, (u64)r);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_tell(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 out)
{
    return w2c_wasi__snapshot__preview1_fd_seek(w, fd, 0, 1, out);
}

u32 w2c_wasi__snapshot__preview1_fd_sync(struct w2c_wasi__snapshot__preview1 *w, u32 fd)
{
    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used) {
        return W_EBADF;
    }
    if (!g_fd[fd].is_dir && fd > 2) {
#if defined(_WIN32)
        _commit(g_fd[fd].native);
#else
        fsync(g_fd[fd].native);
#endif
    }
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_datasync(struct w2c_wasi__snapshot__preview1 *w, u32 fd)
{
    return w2c_wasi__snapshot__preview1_fd_sync(w, fd);
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_get(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf)
{
    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used) {
        return W_EBADF;
    }
    if (fd < 3) {
        filestat_out(buf, FT_CHAR, 0, 0);
        return 0;
    }
    if (g_fd[fd].is_dir) {
        filestat_out(buf, FT_DIR, 0, 0);
        return 0;
    }
    {
#if defined(_WIN32)
        struct _stat64 st;

        if (_fstat64(g_fd[fd].native, &st) != 0) {
            return errno_wasi(errno);
        }
#else
        struct stat st;

        if (fstat(g_fd[fd].native, &st) != 0) {
            return errno_wasi(errno);
        }
#endif
        filestat_out(buf, FT_REG, (u64)st.st_size, (u64)st.st_mtime * 1000000000ull);
    }
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_set_size(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u64 size)
{
    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used || g_fd[fd].is_dir) {
        return W_EBADF;
    }
#if defined(_WIN32)
    return _chsize_s(g_fd[fd].native, (long long)size) == 0 ? 0 : W_EIO;
#else
    return ftruncate(g_fd[fd].native, (off_t)size) == 0 ? 0 : W_EIO;
#endif
}

u32 w2c_wasi__snapshot__preview1_fd_readdir(struct w2c_wasi__snapshot__preview1 *w, u32 fd, u32 buf, u32 len, u64 cookie, u32 used)
{
    u32 at = 0;
    u64 idx = 0;

    (void)w;
    if (fd >= FDS_MAX || !g_fd[fd].used || !g_fd[fd].is_dir || !span_ok(buf, len)) {
        return W_EBADF;
    }
#if defined(_WIN32)
    {
        char *pat = (char *)malloc(strlen(g_fd[fd].path) + 3);
        wchar_t *wp;
        WIN32_FIND_DATAW fdw;
        HANDLE h;

        if (pat == NULL) {
            return W_EIO;
        }
        sprintf(pat, "%s/*", g_fd[fd].path);
        wp = wide(pat);
        free(pat);
        h = wp != NULL ? FindFirstFileW(wp, &fdw) : INVALID_HANDLE_VALUE;
        free(wp);
        if (h == INVALID_HANDLE_VALUE) {
            st32(used, 0);
            return 0;
        }
        do {
            char name[1024];
            int nl = WideCharToMultiByte(CP_UTF8, 0, fdw.cFileName, -1, name, sizeof name, NULL, NULL) - 1;
            u8 head[24];
            u32 k;

            if (nl < 0) {
                continue;
            }
            if (idx++ < cookie) {
                continue;
            }
            memset(head, 0, sizeof head);
            {
                u64 next = idx;
                u32 namlen = (u32)nl;

                memcpy(head, &next, 8);
                memcpy(head + 16, &namlen, 4);
                head[20] = (u8)((fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FT_DIR : FT_REG);
            }
            for (k = 0; k < 24 && at < len; k++) {
                P(buf)[at++] = head[k];
            }
            for (k = 0; k < (u32)nl && at < len; k++) {
                P(buf)[at++] = (u8)name[k];
            }
            if (at >= len) {
                break;
            }
        } while (FindNextFileW(h, &fdw));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(g_fd[fd].path[0] ? g_fd[fd].path : "/");
        struct dirent *e;

        if (d == NULL) {
            st32(used, 0);
            return 0;
        }
        while ((e = readdir(d)) != NULL) {
            u8 head[24];
            u32 k, nl = (u32)strlen(e->d_name);

            if (idx++ < cookie) {
                continue;
            }
            memset(head, 0, sizeof head);
            memcpy(head, &idx, 8);
            memcpy(head + 16, &nl, 4);
            head[20] = (u8)(e->d_type == DT_DIR ? FT_DIR : FT_REG);
            for (k = 0; k < 24 && at < len; k++) {
                P(buf)[at++] = head[k];
            }
            for (k = 0; k < nl && at < len; k++) {
                P(buf)[at++] = (u8)e->d_name[k];
            }
            if (at >= len) {
                break;
            }
        }
        closedir(d);
    }
#endif
    st32(used, at);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

u32 w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 dirflags,
                                             u32 path, u32 len, u32 oflags, u64 rights, u64 rights_inh,
                                             u32 fdflags, u32 out)
{
    char *p = full_path(dir, path, len);
    struct stat st;
    int fd, isdir, flags, nfd;

    (void)w;
    (void)dirflags;
    (void)rights_inh;
    if (p == NULL) {
        return W_EBADF;
    }
    isdir = native_stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
    if ((oflags & 2u) || (isdir && !(rights & 0x40u))) {     /* O_DIRECTORY, or a directory to read */
        if (!isdir) {
            free(p);
            return (oflags & 2u) ? W_ENOTDIR : W_ENOENT;
        }
        fd = alloc_fd();
        if (fd < 0) {
            free(p);
            return W_EIO;
        }
        g_fd[fd].is_dir = 1;
        g_fd[fd].path = p;
        st32(out, (u32)fd);
        return 0;
    }
    if (isdir) {
        free(p);
        return W_EISDIR;
    }
    {
        int rd = (rights & 0x2u) != 0, wr = (rights & 0x40u) != 0;

        flags = rd && wr ? O_RDWR : wr ? O_WRONLY : O_RDONLY;
    }
    if (oflags & 1u) flags |= O_CREAT;
    if (oflags & 4u) flags |= O_EXCL;
    if (oflags & 8u) flags |= O_TRUNC;
    if (fdflags & 1u) flags |= O_APPEND;
#if defined(_WIN32)
    {
        wchar_t *wp = wide(p);

        nfd = wp != NULL ? _wopen(wp, flags | O_BINARY, _S_IREAD | _S_IWRITE) : -1;
        free(wp);
    }
#else
#if defined(__ANDROID__)
    if (held_fd(p) >= 0 && (flags & (O_WRONLY | O_RDWR)) == 0) {
        nfd = dup(held_fd(p));
        if (nfd >= 0) {
            lseek(nfd, 0, SEEK_SET);
        }
    } else
#endif
    nfd = open(p, flags, 0644);
#endif
    if (nfd < 0) {
        int e = errno;

        if (getenv("EIDOS_WASI_TRACE")) {
            fprintf(stderr, "wasi: path_open %s failed (errno %d)\n", p, e);
        }
        free(p);
        return errno_wasi(e);
    }
    fd = alloc_fd();
    if (fd < 0) {
        free(p);
        return W_EIO;
    }
    if (getenv("EIDOS_DEBUG_IO") != NULL) {
        /* the path from the data folder down (no user folders in the log) */
        const char *rel = strstr(p, "/data/");

        if (rel == NULL) {
            rel = strstr(p, "\\data\\");
        }
        fprintf(stderr, "io open fd %d %s\n", fd, rel != NULL ? rel + 6 : p);
    }
    g_fd[fd].path = p;          /* kept for the Switch's rename report */
    g_fd[fd].native = nfd;
    g_fd[fd].append = (fdflags & 1u) != 0;
    st32(out, (u32)fd);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_path_filestat_get(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 flags,
                                                     u32 path, u32 len, u32 buf)
{
    char *p = full_path(dir, path, len);
    struct stat st;
    int rc;

    (void)w;
    (void)flags;
    if (p == NULL) {
        return W_EBADF;
    }
    rc = native_stat(p, &st);
    free(p);
    if (rc != 0) {
        return errno_wasi(errno);
    }
    filestat_out(buf, (st.st_mode & S_IFMT) == S_IFDIR ? FT_DIR : FT_REG, (u64)st.st_size,
                 (u64)st.st_mtime * 1000000000ull);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_path_create_directory(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 path, u32 len)
{
    char *p = full_path(dir, path, len);
    int rc;

    (void)w;
    if (p == NULL) {
        return W_EBADF;
    }
#if defined(_WIN32)
    {
        wchar_t *wp = wide(p);

        rc = wp != NULL ? _wmkdir(wp) : -1;
        free(wp);
    }
#else
    rc = mkdir(p, 0755);
#endif
    free(p);
    return rc == 0 ? 0 : errno_wasi(errno);
}

u32 w2c_wasi__snapshot__preview1_path_remove_directory(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 path, u32 len)
{
    char *p = full_path(dir, path, len);
    int rc;

    (void)w;
    if (p == NULL) {
        return W_EBADF;
    }
#if defined(_WIN32)
    {
        wchar_t *wp = wide(p);

        rc = wp != NULL ? _wrmdir(wp) : -1;
        free(wp);
    }
#else
    rc = rmdir(p);
#endif
    free(p);
    return rc == 0 ? 0 : errno_wasi(errno);
}

u32 w2c_wasi__snapshot__preview1_path_unlink_file(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 path, u32 len)
{
    char *p = full_path(dir, path, len);
    int rc;

    (void)w;
    if (p == NULL) {
        return W_EBADF;
    }
#if defined(_WIN32)
    {
        wchar_t *wp = wide(p);

        rc = wp != NULL ? _wremove(wp) : -1;
        free(wp);
    }
#else
    rc = unlink(p);
#endif
    free(p);
    return rc == 0 ? 0 : errno_wasi(errno);
}

u32 w2c_wasi__snapshot__preview1_path_rename(struct w2c_wasi__snapshot__preview1 *w, u32 dir, u32 from, u32 flen,
                                               u32 dir2, u32 to, u32 tlen)
{
    char *a = full_path(dir, from, flen), *b = full_path(dir2, to, tlen);
    int rc = -1;

    (void)w;
    if (a != NULL && b != NULL) {
#if defined(_WIN32)
        wchar_t *wa = wide(a), *wb = wide(b);

        rc = wa != NULL && wb != NULL && MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
        if (rc != 0) {
            errno = EACCES;
        }
        free(wa);
        free(wb);
#else
        rc = rename(a, b);
#endif
#if defined(__SWITCH__)
        if (rc != 0) {
            /* A folder (the first run's extract.partial to extract): newlib's
             * rename failed in Ryubing, so the folder is renamed by the
             * system's own call; both answers are logged for a console. */
            int e = errno;
            FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
            const char *pa = strncmp(a, "sdmc:", 5) == 0 ? a + 5 : a;
            const char *pb = strncmp(b, "sdmc:", 5) == 0 ? b + 5 : b;
            char fa[FS_MAX_PATH], fb[FS_MAX_PATH];
            Result r = MAKERESULT(Module_Libnx, LibnxError_NotFound);

            if (fs != NULL && strlen(pa) < sizeof fa && strlen(pb) < sizeof fb) {
                snprintf(fa, sizeof fa, "%s", pa);
                snprintf(fb, sizeof fb, "%s", pb);
                r = fsFsRenameDirectory(fs, fa, fb);
            }
            fprintf(stderr, "eidos: rename %s to %s: errno %d, the folder rename 0x%x\n",
                    a, b, e, (unsigned)r);
            {
                /* a folder with an open file in it is locked (0xE02) */
                size_t al = strlen(a);
                int i;

                for (i = 0; i < FDS_MAX; i++) {
                    if (g_fd[i].used && g_fd[i].path != NULL
                        && strncmp(g_fd[i].path, a, al) == 0) {
                        fprintf(stderr, "eidos:   still open: fd %d %s%s\n", i,
                                g_fd[i].path, g_fd[i].is_dir ? " (folder)" : "");
                    }
                }
            }
            rc = R_SUCCEEDED(r) ? 0 : -1;
            errno = e;
        }
#endif
    }
    free(a);
    free(b);
    return rc == 0 ? 0 : errno_wasi(errno);
}

/* The rest of WASI: the C library links a wrapper for each, the game
 * calls none of them. */
#define W1 struct w2c_wasi__snapshot__preview1 *w
u32 w2c_wasi__snapshot__preview1_fd_advise(W1, u32 a, u64 b, u64 c, u32 d) { (void)w; (void)a; (void)b; (void)c; (void)d; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_fd_allocate(W1, u32 a, u64 b, u64 c) { (void)w; (void)a; (void)b; (void)c; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_fd_fdstat_set_rights(W1, u32 a, u64 b, u64 c) { (void)w; (void)a; (void)b; (void)c; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_fd_filestat_set_times(W1, u32 a, u64 b, u64 c, u32 d) { (void)w; (void)a; (void)b; (void)c; (void)d; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_path_filestat_set_times(W1, u32 a, u32 b, u32 c, u32 d, u64 e, u64 f, u32 g) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_path_link(W1, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_path_readlink(W1, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_path_symlink(W1, u32 a, u32 b, u32 c, u32 d, u32 e) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_sock_accept(W1, u32 a, u32 b, u32 c) { (void)w; (void)a; (void)b; (void)c; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_sock_recv(W1, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_sock_send(W1, u32 a, u32 b, u32 c, u32 d, u32 e) { (void)w; (void)a; (void)b; (void)c; (void)d; (void)e; return W_ENOSYS; }
u32 w2c_wasi__snapshot__preview1_sock_shutdown(W1, u32 a, u32 b) { (void)w; (void)a; (void)b; return W_ENOSYS; }
#undef W1

/* The standard streams and the root, before the module starts. */
void host_wasi_init(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        g_fd[i].used = 1;
    }
    g_fd[3].used = 1;
    g_fd[3].is_dir = 1;
#if defined(_WIN32)
    g_fd[3].path = strdup("");
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#else
    g_fd[3].path = strdup("/");
#endif
}
