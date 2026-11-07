/*
 * sdk/plat_crash.c -- a host crash reported on stderr (sdk/plat_crash.h).
 *
 * Win32: an unhandled-exception filter.  It prints the exception code, for
 * an access violation the data address and whether it was a read or a write,
 * then the faulting instruction and the frame-pointer chain's return
 * addresses, each as the executable's preferred address (ImageBase +
 * offset, what addr2line and the map file use) whatever ASLR did.
 *
 * POSIX: a SA_SIGINFO handler for SIGSEGV, SIGBUS, SIGILL and SIGFPE that
 * prints the signal and the faulting address.  The interrupted context's
 * registers are machine-specific there, so no frame walk.
 *
 * Both exit with status 3 and never return into the faulting code.
 */

#if !defined(_WIN32) && !defined(__wasm__) && !defined(__vita__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* sigaction, siginfo_t */
#endif

#include "plat_crash.h"

static void (*bw_crash_context_fn)(void);

void bw_plat_crash_context(void (*fn)(void))
{
    bw_crash_context_fn = fn;
}

void bw_plat_crash_context_run(void)
{
    if (bw_crash_context_fn != 0) {
        bw_crash_context_fn();
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <stdint.h>

static int bw_crash_readable(uintptr_t addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (addr == 0 || VirtualQuery((LPCVOID)addr, &mbi, sizeof mbi) == 0) {
        return 0;
    }
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return 0;
    }
    return addr + len <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

/* An address inside the executable as its preferred address, else as is. */
static void bw_crash_print_addr(const char *what, uintptr_t addr)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + (uintptr_t)dos->e_lfanew);
    uintptr_t size = nt->OptionalHeader.SizeOfImage;

    if (addr >= base && addr < base + size) {
        fprintf(stderr, "  %-6s 0x%08llX  (exe+0x%llX)\n", what,
                (unsigned long long)((uintptr_t)nt->OptionalHeader.ImageBase + (addr - base)),
                (unsigned long long)(addr - base));
    } else if (addr >= 0x02000000u && addr < 0x08000000u) {
        /* The DS's own address range (identity-mapped in an ILP32 boot):
         * as a pc, a DS code address the data held that nothing relocated
         * to a host function. */
        fprintf(stderr, "  %-6s 0x%08llX  (a DS address)\n", what,
                (unsigned long long)addr);
    } else {
        fprintf(stderr, "  %-6s 0x%08llX  (outside the executable)\n", what,
                (unsigned long long)addr);
    }
}

/* The frame pointer and the pc of a captured context. */
static void bw_crash_context(const CONTEXT *c, uintptr_t *pc, uintptr_t *fp)
{
#if defined(_M_IX86) || defined(__i386__)
    *pc = (uintptr_t)c->Eip;
    *fp = (uintptr_t)c->Ebp;
#elif defined(_M_X64) || defined(__x86_64__)
    *pc = (uintptr_t)c->Rip;
    *fp = (uintptr_t)c->Rbp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    *pc = (uintptr_t)c->Pc;
    *fp = (uintptr_t)c->Fp;
#else
    (void)c;
    *pc = 0;
    *fp = 0;
#endif
}

/* The return addresses of the frame-pointer chain from `fp`. */
static void bw_crash_walk(uintptr_t fp)
{
    int i;

    for (i = 0; i < 48 && bw_crash_readable(fp, 2 * sizeof(uintptr_t)); i++) {
        const uintptr_t *frame = (const uintptr_t *)fp;
        bw_crash_print_addr("called", frame[1]);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
}

static LONG WINAPI bw_crash_filter(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t pc;
    uintptr_t fp;

    bw_crash_context(ep->ContextRecord, &pc, &fp);
    pc = (uintptr_t)er->ExceptionAddress;

    (void)freopen("eidos_crash.log", "w", stderr);   /* a double-clicked run has no console */
    fprintf(stderr, "CRASH: exception 0x%08lX", (unsigned long)er->ExceptionCode);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        fprintf(stderr, ", %s of host address 0x%08llX",
                er->ExceptionInformation[0] == 1 ? "write" :
                er->ExceptionInformation[0] == 8 ? "execute" : "read",
                (unsigned long long)er->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");
    bw_plat_crash_context_run();
    bw_crash_print_addr("pc", pc);
    bw_crash_walk(fp);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

static int bw_crash_on;
static BwCodeResolver bw_code_resolver;

/* The DS address space eidos maps at the same host addresses (sdk/dsmem.h):
 * an execute fault inside it is a call to a DS code address. */
#define BW_DS_CODE_LO 0x01FF8000u
#define BW_DS_CODE_HI 0x02800000u

static LONG WINAPI bw_ds_call_handler(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t at;
    const char *name = NULL;
    void *fn;
    static unsigned int reported[256];
    static unsigned int nreported;
    unsigned int i;

    if (bw_code_resolver == NULL || er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION
            || er->NumberParameters < 2 || er->ExceptionInformation[0] != 8) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    at = (uintptr_t)er->ExceptionInformation[1];
    if (at < BW_DS_CODE_LO || at >= BW_DS_CODE_HI) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    fn = bw_code_resolver((unsigned int)at, &name);
    if (fn == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    for (i = 0; i < nreported && reported[i] != (unsigned int)at; i++) {
    }
    if (i == nreported && nreported < 256) {
        reported[nreported++] = (unsigned int)at;
        fprintf(stderr, "ds call: 0x%08X -> %s\n", (unsigned int)at, name ? name : "?");
#if defined(_M_IX86) || defined(__i386__)
        /* who called: the call just pushed its return address, so [esp]
         * names the call site (the map file then names its function); a
         * system without this redirect (the Vita) needs that pointer
         * relocated where it is stored (docs/consoles.md) */
        if (bw_crash_readable((uintptr_t)ep->ContextRecord->Esp, sizeof(uintptr_t))) {
            bw_crash_print_addr("from", *(const uintptr_t *)(uintptr_t)ep->ContextRecord->Esp);
        }
#endif
    }
#if defined(_M_X64) || defined(__x86_64__)
    ep->ContextRecord->Rip = (DWORD64)(uintptr_t)fn;
#elif defined(_M_IX86) || defined(__i386__)
    ep->ContextRecord->Eip = (DWORD)(uintptr_t)fn;
#else
    return EXCEPTION_CONTINUE_SEARCH;
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

void bw_plat_crash_code_resolver(BwCodeResolver resolve)
{
    static int added;

    bw_code_resolver = resolve;
    if (resolve != NULL && !added) {
        AddVectoredExceptionHandler(1, bw_ds_call_handler);
        added = 1;
    }
}

void bw_plat_console_open(void)
{
    if (GetConsoleWindow() != NULL) {
        return;                         /* started from a terminal */
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
        return;
    }
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
}

void bw_plat_crash_install(void)
{
    /* Room for the report on a stack overflow, which otherwise kills the
     * process before the filter can run (the stack is gone). */
    ULONG guarantee = 64 * 1024;

    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(bw_crash_filter);
    bw_crash_on = 1;
}

int bw_plat_crash_installed(void)
{
    return bw_crash_on;
}

/* Every other thread of the process: suspended, its pc and frame chain. */
static void bw_crash_dump_threads(void)
{
    DWORD self = GetCurrentThreadId();
    DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;

    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) {
        do {
            HANDLE h;
            CONTEXT c;
            uintptr_t pc;
            uintptr_t fp;

            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                           | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (h == NULL) {
                continue;
            }
            if (SuspendThread(h) != (DWORD)-1) {
                memset(&c, 0, sizeof c);
                c.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(h, &c)) {
                    bw_crash_context(&c, &pc, &fp);
                    fprintf(stderr, "thread %lu:\n", (unsigned long)te.th32ThreadID);
                    bw_crash_print_addr("pc", pc);
                    bw_crash_walk(fp);
                }
            }
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    fflush(stderr);
}

typedef struct BwWatchdog {
    unsigned seconds;
    unsigned long long (*progress)(void);
} BwWatchdog;

static BwWatchdog g_watchdog;

static DWORD WINAPI bw_watchdog_main(LPVOID arg)
{
    unsigned long long last = g_watchdog.progress();
    unsigned still = 0;

    (void)arg;
    while (still < g_watchdog.seconds) {
        unsigned long long now;

        Sleep(1000);
        now = g_watchdog.progress();
        if (now != last) {
            last = now;
            still = 0;
        } else {
            still++;
        }
    }
    fprintf(stderr, "WATCHDOG: no progress for %u s (at %llu)\n",
            g_watchdog.seconds, last);
    bw_plat_crash_context_run();
    bw_crash_dump_threads();
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    HANDLE h;

    g_watchdog.seconds = seconds;
    g_watchdog.progress = progress;
    h = CreateThread(NULL, 0, bw_watchdog_main, NULL, 0, NULL);
    if (h != NULL) {
        CloseHandle(h);
    }
}

/* The sampling profiler (plat_crash.h). */
static FILE *g_sample_out;

static DWORD WINAPI bw_sampler_main(LPVOID arg)
{
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE th[64];
    DWORD tid[64];
    int nth = 0, k, tick = 0;

    (void)arg;
    fprintf(g_sample_out, "base %p\n", (void *)GetModuleHandleA(NULL));
    for (;;) {
        if (tick++ % 500 == 0) {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;

            for (k = 0; k < nth; k++) {
                CloseHandle(th[k]);
            }
            nth = 0;
            te.dwSize = sizeof te;
            if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && nth < 64) {
                        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                                              | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                        if (h != NULL) {
                            tid[nth] = te.th32ThreadID;
                            th[nth++] = h;
                        }
                    }
                } while (Thread32Next(snap, &te));
            }
            if (snap != INVALID_HANDLE_VALUE) {
                CloseHandle(snap);
            }
        }
        for (k = 0; k < nth; k++) {
            CONTEXT c;
            uintptr_t pc, fp;
            int d;

            if (SuspendThread(th[k]) == (DWORD)-1) {
                continue;
            }
            memset(&c, 0, sizeof c);
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th[k], &c)) {
                bw_crash_context(&c, &pc, &fp);
                fprintf(g_sample_out, "%lu %lx", (unsigned long)tid[k], (unsigned long)pc);
                for (d = 0; d < 12 && fp != 0; d++) {
                    uintptr_t fr[2];
                    SIZE_T got = 0;

                    if (!ReadProcessMemory(GetCurrentProcess(), (void *)fp, fr, sizeof fr, &got)
                        || got != sizeof fr || fr[0] <= fp) {
                        break;
                    }
                    fprintf(g_sample_out, " %lx", (unsigned long)fr[1]);
                    fp = fr[0];
                }
                fputc('\n', g_sample_out);
            }
            ResumeThread(th[k]);
        }
        Sleep(1);
    }
    return 0;
}

static void bw_sampler_flush(void)
{
    if (g_sample_out != NULL) {
        fflush(g_sample_out);
    }
}

void bw_plat_sampler_start(void)
{
    const char *path = getenv("EIDOS_SAMPLE");
    HANDLE h;

    if (path == NULL || g_sample_out != NULL) {
        return;
    }
    g_sample_out = fopen(path, "w");
    if (g_sample_out == NULL) {
        return;
    }
    setvbuf(g_sample_out, NULL, _IOFBF, 1 << 20);
    atexit(bw_sampler_flush);
    h = CreateThread(NULL, 0, bw_sampler_main, NULL, 0, NULL);
    if (h != NULL) {
        SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(h);
    }
}

void bw_plat_backtrace(void)
{
    void *frames[48];
    USHORT n = CaptureStackBackTrace(1, 48, frames, NULL);
    USHORT i;

    for (i = 0; i < n; i++) {
        bw_crash_print_addr("called", (uintptr_t)frames[i]);
    }
    fflush(stderr);
}

#elif defined(__wasm__)

#include <stdint.h>

#include "wasm_host.h"

/* The wasm32 build: a fault is a trap, which the host program reports
 * (host/main.c), and there is no second thread for a watchdog. */

int bw_plat_crash_installed(void)
{
    return 0;
}

void bw_plat_backtrace(void)
{
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    (void)seconds;
    (void)progress;
}

void bw_plat_sampler_start(void)
{
}

void bw_plat_console_open(void)
{
    bw_host_console_open();
}

void bw_plat_crash_install(void)
{
}

static BwCodeResolver bw_code_resolver;

void bw_plat_crash_code_resolver(BwCodeResolver resolve)
{
    bw_code_resolver = resolve;
}

/* A call to a DS code address, the wasm32 way: a function pointer is a
 * table index, and the host program sends every indirect call whose index
 * lies in the DS address space here first (host/CMakeLists.txt).  The
 * answer is the index of the function the build compiled for it, or the
 * address back when there is none (the call then traps, as before).  The
 * first call through each address is reported once, as on Win32. */
__attribute__((export_name("bw_code_index")))
unsigned int bw_code_index(unsigned int at)
{
    static unsigned int reported[256];
    static unsigned int nreported;
    const char *name = NULL;
    void *fn;
    unsigned int i;

    if (bw_code_resolver == NULL) {
        return at;
    }
    fn = bw_code_resolver(at, &name);
    if (fn == NULL) {
        return at;
    }
    for (i = 0; i < nreported && reported[i] != at; i++) {
    }
    if (i == nreported && nreported < 256) {
        reported[nreported++] = at;
        fprintf(stderr, "ds call: 0x%08X -> %s\n", at, name ? name : "?");
    }
    return (unsigned int)(uintptr_t)fn;
}

#elif defined(__vita__)

/* The Vita: no signals reach an application, so no crash report (the
 * system's own crash dump stands), no watchdog and no sampler.
 *
 * A call to a DS code address is caught as on Win32, through kubridge (the
 * kernel plugin Vita ports use for this; Vita3K has it too): the jump to
 * the address faults in the prefetch, and the handler sends it on to the
 * function the build compiled for it.  Such an address reaches a call when
 * a table in one overlay names a function of another overlay that was not
 * loaded yet, and the game copied the pointer before it was (a process's
 * init, a field gimmick's callbacks): overlays share their addresses, so
 * only the call itself can say which function it means. */

#include <stdint.h>
#include <kubridge.h>

static BwCodeResolver bw_code_resolver;
static KuKernelExceptionHandler g_old_prefetch;

static void bw_ds_call_handler(KuKernelExceptionContext *ctx)
{
    static unsigned int reported[256];
    static unsigned int nreported;
    const char *name = NULL;
    unsigned int at = ctx->pc;
    void *fn = NULL;
    unsigned int i;

    if (bw_code_resolver != NULL && at >= 0x01FF8000u && at < 0x02800000u) {
        /* a Thumb target ran with T set: the DS stored it with bit 0 */
        if (ctx->SPSR & 0x20u) {
            fn = bw_code_resolver(at | 1u, &name);
        }
        if (fn == NULL) {
            fn = bw_code_resolver(at, &name);
        }
    }
    if (fn == NULL) {
        if (g_old_prefetch != NULL) {
            g_old_prefetch(ctx);
        }
        return;
    }
    for (i = 0; i < nreported && reported[i] != at; i++) {
    }
    if (i == nreported && nreported < 256) {
        reported[nreported++] = at;
        fprintf(stderr, "ds call: 0x%08X -> %s (from 0x%08X)\n", at, name ? name : "?",
                (unsigned int)ctx->lr);
    }
    /* the host function, in its own instruction set (bit 0: Thumb) */
    ctx->pc = (SceUInt32)(uintptr_t)fn & ~1u;
    ctx->SPSR = (ctx->SPSR & ~0x20u) | (((SceUInt32)(uintptr_t)fn & 1u) << 5);
}

int bw_plat_crash_installed(void)
{
    return 0;
}

void bw_plat_backtrace(void)
{
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    (void)seconds;
    (void)progress;
}

void bw_plat_sampler_start(void)
{
}

void bw_plat_console_open(void)
{
}

void bw_plat_crash_install(void)
{
}

void bw_plat_crash_code_resolver(BwCodeResolver resolve)
{
    static int added;

    bw_code_resolver = resolve;
    if (resolve != NULL && !added) {
        added = kuKernelRegisterExceptionHandler(KU_KERNEL_EXCEPTION_TYPE_PREFETCH_ABORT,
                                                 bw_ds_call_handler, &g_old_prefetch,
                                                 NULL) >= 0;
        if (!added) {
            fprintf(stderr, "eidos: no kubridge, so a call to a DS code address will "
                    "crash (install kubridge.skprx)\n");
        }
    }
}

#else /* POSIX */

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void bw_crash_handler(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    /* fprintf is not async-signal-safe; the process is ending anyway and the
     * report is the point. */
    fprintf(stderr, "CRASH: signal %d (%s) at address %p\n", sig,
            sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" :
            sig == SIGILL ? "SIGILL" : "SIGFPE", si ? si->si_addr : NULL);
    bw_plat_crash_context_run();
    fflush(stderr);
    _exit(3);
}

static int bw_crash_on;

int bw_plat_crash_installed(void)
{
    return bw_crash_on;
}

/* No portable way to walk the stack here; the panic message stands alone. */
void bw_plat_backtrace(void)
{
}

typedef struct BwWatchdog {
    unsigned seconds;
    unsigned long long (*progress)(void);
} BwWatchdog;

static BwWatchdog g_watchdog;

static void *bw_watchdog_main(void *arg)
{
    unsigned long long last = g_watchdog.progress();
    unsigned still = 0;

    (void)arg;
    while (still < g_watchdog.seconds) {
        struct timespec ts;
        unsigned long long now;

        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
        now = g_watchdog.progress();
        if (now != last) {
            last = now;
            still = 0;
        } else {
            still++;
        }
    }
    fprintf(stderr, "WATCHDOG: no progress for %u s (at %llu)\n",
            g_watchdog.seconds, last);
    bw_plat_crash_context_run();
    fflush(stderr);
    _exit(3);
    return NULL;
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    pthread_t t;

    g_watchdog.seconds = seconds;
    g_watchdog.progress = progress;
    if (pthread_create(&t, NULL, bw_watchdog_main, NULL) == 0) {
        pthread_detach(t);
    }
}

void bw_plat_sampler_start(void)
{
}

void bw_plat_console_open(void)
{
}

void bw_plat_crash_install(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = bw_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        sigaction(sigs[i], &sa, NULL);
    }
    bw_crash_on = 1;
}

void bw_plat_crash_code_resolver(BwCodeResolver resolve)
{
    (void)resolve;               /* Win32 only (sdk/plat_crash.h) */
}

#endif
