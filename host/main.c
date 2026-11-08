/*
 * host/main.c -- eidos's 64-bit program for the wasm32 build: set up the
 * module's world (host/wasi.c, host/fiber.c), instantiate it and run its
 * main on this thread, which is the module's fiber 0.
 *
 * A trap (an access outside the module's memory, a call through a pointer
 * of the wrong type, an `unreachable`) ends the program with its kind and
 * exit code 3.  The module's own exit is WASI's proc_exit.
 *
 * On Android and iOS SDL owns the process's entry (SDL_main.h turns main
 * into SDL_main) and there is no command line: the arguments are made from
 * the app's own folder (mobile_args).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <execinfo.h>
#endif

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <pthread.h>
#include <sys/mman.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
/* The Switch takes the phones' way too: no command line, a folder of its
 * own (sdmc:/switch/eidos/), OpenGL ES and one screen (docs/consoles.md). */
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__SWITCH__)
#define HOST_MOBILE 1
#include <SDL3/SDL_main.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "host.h"
#include "picker.h"
#include "plat_save.h"
#include "setup.h"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"

w2c_game g_game;
struct w2c_wasi__snapshot__preview1 g_wasi;
struct w2c_eidos g_eidos;
struct w2c_env g_env;

/* clock(): the process's processor time, which WASI does not have; the
 * module only times its own work with it. */
u64 w2c_env_clock(struct w2c_env *e)
{
    (void)e;
    return (u64)clock() * (1000000u / CLOCKS_PER_SEC);
}

/* The ROM dialog (install/picker.h), the platform's own. */
u32 w2c_eidos_pick_rom(struct w2c_eidos *e, u32 title, u32 out, u32 n)
{
    char buf[1024];
    int rc;

    (void)e;
    buf[0] = 0;
#if defined(HOST_MOBILE)
    rc = host_pick_rom((const char *)host_ptr(title, 0), buf, sizeof buf);
#else
    rc = bw_pick_rom(NULL, (const char *)host_ptr(title, 0), buf, sizeof buf);
#endif
    if (n > 0) {
        size_t len = strlen(buf);

        if (len >= n) {
            len = n - 1;
        }
        memcpy(host_ptr(out, n), buf, len);
        ((char *)host_ptr(out, n))[len] = 0;
    }
    return (u32)rc;
}

/* A console for the program's messages (sdk/plat_crash.h,
 * bw_plat_console_open): a release build on Windows is a windowed program
 * and has none until `eidos --console` asks. */
void w2c_eidos_console_open(struct w2c_eidos *e)
{
    (void)e;
#if defined(_WIN32)
    if (GetConsoleWindow() != NULL) {
        return;                         /* started from a terminal */
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
        return;
    }
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#endif
}

/* The heap's lock (sdk/wasm_heap.c): one recursive mutex for the module's
 * malloc, from the module's threads and the host's alike.  malloc is the
 * only code that grows the memory, so the lock also keeps a worker's copy
 * of the memory's size (host/par.c) in step: the copy takes the shared
 * size on the way in and hands a grown one back on the way out. */
static SDL_Mutex *g_heap;

void w2c_eidos_heap_lock(struct w2c_eidos *e)
{
    w2c_game *inst = host_instance();

    (void)e;
    SDL_LockMutex(g_heap);
    if (inst != &g_game) {
        inst->w2c_memory = g_game.w2c_memory;
    }
}

void w2c_eidos_heap_unlock(struct w2c_eidos *e)
{
    w2c_game *inst = host_instance();

    (void)e;
    if (inst != &g_game && inst->w2c_memory.pages > g_game.w2c_memory.pages) {
        g_game.w2c_memory = inst->w2c_memory;
    }
    SDL_UnlockMutex(g_heap);
}

/* The module's own malloc and free, which take the lock themselves
 * (sdk/wasm_heap.c, exported as __wrap_malloc and __wrap_free). */
u32 host_malloc(u32 n)
{
    return w2c_game_0x5F_wrap_malloc(&g_game, n);
}

void host_free(u32 a)
{
    w2c_game_0x5F_wrap_free(&g_game, a);
}

/* The time zone (sdk/wasm_host.h's tz_offset): local time minus UTC at
 * `t`, by the platform's own rules, daylight saving included. */
u64 w2c_eidos_tz_offset(struct w2c_eidos *e, u64 t)
{
    time_t tt = (time_t)(long long)t;
    struct tm lt;

    (void)e;
#if defined(_WIN32)
    if (localtime_s(&lt, &tt) != 0) {
        return 0;
    }
    return (u64)(long long)(_mkgmtime(&lt) - tt);
#else
    if (localtime_r(&tt, &lt) == NULL) {
        return 0;
    }
#if defined(__SWITCH__)
    {
        /* newlib has no timegm: the local fields counted as UTC by hand
         * (days from 1970 by the civil calendar) */
        long long y = lt.tm_year + 1900LL, m = lt.tm_mon + 1, era, yoe, doy, doe, days;

        y -= m <= 2;
        era = (y >= 0 ? y : y - 399) / 400;
        yoe = y - era * 400;
        doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + lt.tm_mday - 1;
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;
        return (u64)(days * 86400 + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec
                     - (long long)tt);
    }
#else
    return (u64)(long long)(timegm(&lt) - tt);
#endif
#endif
}

#if defined(HOST_INDIRECT_LOG)
/* The diagnostic build's report of a call through a pointer of another
 * type (tools/wasm2c_patch.py): the caller's name and the target's address
 * in this executable, for addr2line; each pair once. */
void host_indirect_mismatch(const char *caller, void *target)
{
    static struct { const char *caller; void *target; } seen[4096];
    static int n;
    int i;

    for (i = 0; i < n; i++) {
        if (seen[i].caller == caller && seen[i].target == target) {
            return;
        }
    }
    if (n < 4096) {
        seen[n].caller = caller;
        seen[n].target = target;
        n++;
    }
#if defined(_WIN32)
    fprintf(stderr, "indirect: %s -> exe+0x%llx\n", caller,
            (unsigned long long)((char *)target - (char *)GetModuleHandleW(NULL)));
#else
    fprintf(stderr, "indirect: %s -> %p\n", caller, target);
#endif
}
#endif

/* A call through a DS code address (tools/wasm2c_patch.py): the module
 * knows which function the build compiled for it.  The lookup runs on the
 * calling thread's own instance and stack, and under the heap's lock, as
 * it keeps a table of the addresses it has reported. */
u32 host_code_index(u32 x)
{
    u32 r;

    SDL_LockMutex(g_heap);
    r = w2c_game_bw_code_index(host_instance(), x);
    SDL_UnlockMutex(g_heap);
    return r;
}

/* The platform's folders (sdk/wasm_host.h's os_path): the same functions
 * a native build calls, compiled here for the platform. */
u32 w2c_eidos_os_path(struct w2c_eidos *e, u32 kind, u32 out, u32 n)
{
    char buf[4096];
    size_t len;
    int rc;

    (void)e;
    switch (kind) {
    case 0:  rc = bw_os_app_home(buf, sizeof buf); break;
    case 1:  rc = bw_os_exe_dir(buf, sizeof buf); break;
    case 2:  rc = bw_plat_default_save_dir(buf, sizeof buf); break;
    default: rc = -1; break;
    }
    len = strlen(buf);
    if (rc != 0 || n == 0 || len >= n) {
        return (u32)-1;
    }
    memcpy(host_ptr(out, n), buf, len + 1);
    return 0;
}

/* A trap: the native frames, as the executable's own addresses, for
 * addr2line (the translated functions carry the module's names). */
void host_trap(wasm_rt_trap_t code)
{
    fprintf(stderr, "eidos: the game stopped (wasm trap: %s)\n", wasm_rt_strerror(code));
#if defined(_WIN32)
    {
        void *frames[48];
        unsigned short n = CaptureStackBackTrace(0, 48, frames, NULL);
        HMODULE self = GetModuleHandleW(NULL);
        unsigned short i;

        for (i = 0; i < n; i++) {
            fprintf(stderr, "  frame %2u  exe+0x%llx\n", i,
                    (unsigned long long)((char *)frames[i] - (char *)self));
        }
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    {
        void *frames[64];
        int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, 2);
    }
#endif
    fflush(stderr);
    exit(3);
}

#if defined(_WIN32)
/* A fault outside a wasm trap (the host's own code, a driver): where it
 * happened and the native frames, the way host_trap reports. */
static LONG WINAPI host_fault(EXCEPTION_POINTERS *ep)
{
    HMODULE self = GetModuleHandleW(NULL);

    fprintf(stderr, "eidos: the program stopped (exception 0x%08lX at exe+0x%llx)\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (unsigned long long)((char *)ep->ExceptionRecord->ExceptionAddress - (char *)self));
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && ep->ExceptionRecord->NumberParameters >= 2) {
        uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base = (uintptr_t)g_game.w2c_memory.data;

        fprintf(stderr, "eidos: %s 0x%llx (module address 0x%llx)\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read of",
                (unsigned long long)a, (unsigned long long)(a - base));
    }
    host_trap(WASM_RT_TRAP_NONE);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#if defined(_WIN32)
#include <dbghelp.h>

/* EIDOS_HOST_STALL=<seconds>: after that long, the frames the main thread
 * stands in, the way host_trap prints them, and the program ends.  For a
 * run that hangs (a wait no fiber ever ends). */
static HANDLE g_main_thread;

static DWORD WINAPI stall_watch(LPVOID arg)
{
    HMODULE self = GetModuleHandleW(NULL);
    CONTEXT ctx;
    STACKFRAME64 f;
    int i;

    Sleep((DWORD)(uintptr_t)arg * 1000u);
    SuspendThread(g_main_thread);
    memset(&ctx, 0, sizeof ctx);
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(g_main_thread, &ctx);
    memset(&f, 0, sizeof f);
    f.AddrPC.Offset = ctx.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    fprintf(stderr, "eidos: the main thread after %u s:\n", (unsigned)(uintptr_t)arg);
    for (i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                                       g_main_thread, &f, &ctx, NULL,
                                       SymFunctionTableAccess64, SymGetModuleBase64, NULL); i++) {
        fprintf(stderr, "  frame %2d  exe+0x%llx\n", i,
                (unsigned long long)(f.AddrPC.Offset - (DWORD64)(uintptr_t)self));
    }
    fflush(stderr);
    ExitProcess(4);
    return 0;
}
#endif

#if defined(HOST_MOBILE)
/* The game's arguments on a phone: its data and its profile in the app's
 * own folder (SDL_GetPrefPath: Android's internal files, iOS's sandbox),
 *
 *   <folder>/extract/    the extracted game data (docs/installer.md)
 *   <folder>/profile/    settings and saves
 *
 * and the one-screen layout in the game's window.  Without game data the
 * first-run window asks for the ROM through the system's file list
 * (host/pick_mobile.c) and extracts it there.  Lines of <folder>/args.txt are added after these, and <folder>/env.txt's
 * NAME=value lines set the environment (the EIDOS_* switches), since
 * neither system gives an app a command line.  The program's messages go
 * to <folder>/stderr.txt (to the console with EIDOS_MOBILE_CONSOLE set,
 * as `xcrun simctl launch --console` can). */
static char **mobile_args(int *argc)
{
    static char *argv[64];
    static char data[1024], prof[1024], file[1024], line[1024];
    const char *pref = SDL_GetPrefPath("eidos", "eidos");
    int n = 0;
    FILE *f;

    if (pref == NULL) {
        pref = "./";
    }
    snprintf(file, sizeof file, "%sstderr.txt", pref);
    if (getenv("EIDOS_MOBILE_CONSOLE") == NULL) {
        /* The output descriptors themselves, which the game's writes go to:
         * an app started by a tool (xcrun devicectl) has them on a pipe no
         * one reads, and a full pipe stopped the game in its next message.
         * The file, or nowhere. */
        int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd < 0) {
            fd = open("/dev/null", O_WRONLY);
        }
        if (fd >= 0) {
            fflush(stdout);
            fflush(stderr);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2) {
                close(fd);
            }
        }
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    snprintf(data, sizeof data, "%sextract", pref);
    snprintf(prof, sizeof prof, "%sprofile", pref);
    SDL_CreateDirectory(prof);
    argv[n++] = "eidos";
    argv[n++] = "--no-window";
    argv[n++] = "--data-root";
    argv[n++] = data;
    argv[n++] = "--profile-dir";
    argv[n++] = prof;
    argv[n++] = "--layout";
    argv[n++] = "one";
    argv[n++] = "--window";
    /* A phone draws with OpenGL ES and is played by touch: the hints show the DS's own button
     * letters (a connected pad still shows its own).  Android's curved
     * screens also get a margin of 2% a side inside the safe area. */
    setenv("EIDOS_TOUCH_DEVICE", "1", 1);
    /* the 3D renderer's choices for OpenGL ES (engine/platform/plat_gl3d.c) */
#if !defined(__SWITCH__)    /* the Switch tries desktop OpenGL first (host/sdl.c) */
    setenv("EIDOS_GL_ES", "1", 1);
#endif
#if defined(__ANDROID__)
    setenv("EIDOS_EDGE_MARGIN", "2", 1);
#endif
    snprintf(file, sizeof file, "%senv.txt", pref);
    if ((f = fopen(file, "r")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            char *eq = strchr(line, '=');

            line[strcspn(line, "\r\n")] = 0;
            if (eq != NULL && line[0] != '#') {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        fclose(f);
    }
    snprintf(file, sizeof file, "%sargs.txt", pref);
    if ((f = fopen(file, "r")) != NULL) {
        while (n < 63 && fgets(line, sizeof line, f) != NULL) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] == '@' && line[1] == '/') {
                /* "@/x": x in the app's folder, whose path a tool
                 * outside the phone cannot know */
                char *p = SDL_malloc(strlen(pref) + strlen(line));

                if (p != NULL) {
                    strcpy(p, pref);
                    strcat(p, line + 2);
                    argv[n++] = p;
                }
            } else if (line[0] != 0 && line[0] != '#') {
                argv[n++] = SDL_strdup(line);
            }
        }
        fclose(f);
    }
    argv[n] = NULL;
    *argc = n;
    {
        /* a profile folder named in args.txt (tests, tools/bench.py's
         * runs) is made by the app, as its own one is */
        int i;

        for (i = 1; i + 1 < n; i++) {
            if (strcmp(argv[i], "--profile-dir") == 0 && argv[i + 1] != prof) {
                /* one level at a time: iOS refuses the walk up its
                 * sandbox's parents that a whole-path create makes */
                snprintf(line, sizeof line, "%s/saves", argv[i + 1]);
                if (!SDL_CreateDirectory(argv[i + 1]) || !SDL_CreateDirectory(line)) {
                    fprintf(stderr, "eidos: cannot make %s: %s\n", line, SDL_GetError());
                }
            }
        }
    }
    /* a phone plays landscape only; an iPad turns both ways (the
     * one-screen layout fits a tall window, ios/Info.plist.in) */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#if defined(__APPLE__)
    host_ios_view_setup();
    if (host_ios_is_pad()) {
        SDL_SetHint(SDL_HINT_ORIENTATIONS,
                    "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    }
#endif
    return argv;
}
#endif

static int host_run(int argc, char **argv);

#if defined(__ANDROID__)
/* The game runs on a thread of its own whose stack holds every fiber's
 * (host/fiber.c says why). */
static int g_run_argc, g_run_rc;
static char **g_run_argv;

static void *game_thread(void *p)
{
    (void)p;
    /* Attached to the Java VM before any fiber has a stack: attaching
     * releases the pages of the thread's stack below where it runs, which
     * would empty the fibers' first frames. */
    SDL_GetAndroidJNIEnv();
    g_run_rc = host_run(g_run_argc, g_run_argv);
    return NULL;
}

static int run_on_fiber_stack(int argc, char **argv)
{
    size_t size = host_fiber_region_size();
    void *region = mmap(NULL, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    pthread_attr_t attr;
    pthread_t t;

    if (region == MAP_FAILED) {
        fprintf(stderr, "eidos: no room for the fibers' stacks\n");
        return 1;
    }
    host_fiber_region(region);
    g_run_argc = argc;
    g_run_argv = argv;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, region, size);
    if (pthread_create(&t, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "eidos: no game thread\n");
        return 1;
    }
    pthread_join(t, NULL);
    return g_run_rc;
}
#endif

int main(int argc, char **argv)
{
#if defined(HOST_MOBILE)
    (void)argc;
    (void)argv;
    argv = mobile_args(&argc);
#endif
#if defined(__ANDROID__)
    return run_on_fiber_stack(argc, argv);
#else
    return host_run(argc, argv);
#endif
}

static int host_run(int argc, char **argv)
{
#if defined(_WIN32)
    if (getenv("EIDOS_HOST_STALL") != NULL) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &g_main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, stall_watch,
                     (LPVOID)(uintptr_t)atoi(getenv("EIDOS_HOST_STALL")), 0, NULL);
    }
#endif
#if defined(_WIN32)
    SetUnhandledExceptionFilter(host_fault);
#endif
    g_heap = SDL_CreateMutex();
    wasm_rt_init();
    host_wasi_init();
    host_fibers_init();
    g_wasi.argc = argc;
    g_wasi.argv = argv;
    wasm2c_game_instantiate(&g_game, &g_eidos, &g_env, &g_wasi);
#if defined(HOST_MEMORY_BUDGET_MB) && HOST_MEMORY_BUDGET_MB > 0
    {
        /* Explicit bounds checks (EIDOS_HOST_BOUNDS_CHECKS: a console has
         * no room for wasm2c's 8 GiB of guard pages): the memory is a heap
         * block that a grow may move, and the SDL surfaces made from module
         * pixels (host/sdl.c) must not move.  It is grown to its whole
         * budget here, before anything points into it, so it never grows
         * again (the module's malloc fails past the budget). */
        uint64_t want = (uint64_t)HOST_MEMORY_BUDGET_MB * 1024u * 1024u / 65536u;

        if (want > g_game.w2c_memory.pages &&
            wasm_rt_grow_memory(&g_game.w2c_memory, want - g_game.w2c_memory.pages) == (uint64_t)-1) {
            fprintf(stderr, "eidos: the module's memory cannot have %d MiB\n", HOST_MEMORY_BUDGET_MB);
            return 1;
        }
        g_game.w2c_memory.max_pages = g_game.w2c_memory.pages;
    }
#endif
    /* a trap ends the program in host_trap (host/CMakeLists.txt) */
    w2c_game_0x5Fstart(&g_game);
    wasm2c_game_free(&g_game);
    wasm_rt_free();
    return 0;
}
