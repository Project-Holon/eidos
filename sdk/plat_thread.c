/*
 * sdk/plat_thread.c -- mutex, condition variable and thread start, for
 * Win32 and for POSIX.
 *
 * The one platform file of the OS layer (sdk/plat_thread.h says why it
 * exists).  Win32 uses CRITICAL_SECTION and CONDITION_VARIABLE (Vista and
 * later) and CreateThread; everything else uses pthreads.  C11 <threads.h>
 * would cover both, but MSVC and older macOS SDKs do not ship it, so the two
 * native APIs are the portable choice.
 */

#include <stdint.h>
#include <stdlib.h>

#include "plat_thread.h"
#include "port.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct BwMutex  { CRITICAL_SECTION cs; };
struct BwCond   { CONDITION_VARIABLE cv; };
struct BwThread { HANDLE h; void (*fn)(void *); void *arg; };

BwMutex *bw_mutex_create(void)
{
    BwMutex *m = (BwMutex *)malloc(sizeof(*m));
    if (m == NULL) {
        bw_panic("bw_mutex_create", 0, "out of memory");
    }
    InitializeCriticalSection(&m->cs);
    return m;
}

void bw_mutex_destroy(BwMutex *m)
{
    if (m != NULL) {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

void bw_mutex_lock(BwMutex *m)   { EnterCriticalSection(&m->cs); }
void bw_mutex_unlock(BwMutex *m) { LeaveCriticalSection(&m->cs); }

BwCond *bw_cond_create(void)
{
    BwCond *c = (BwCond *)malloc(sizeof(*c));
    if (c == NULL) {
        bw_panic("bw_cond_create", 0, "out of memory");
    }
    InitializeConditionVariable(&c->cv);
    return c;
}

void bw_cond_destroy(BwCond *c)
{
    free(c);   /* a Win32 condition variable owns no resources */
}

void bw_cond_wait(BwCond *c, BwMutex *m)
{
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
}

void bw_cond_broadcast(BwCond *c)
{
    WakeAllConditionVariable(&c->cv);
}

static DWORD WINAPI bw_thread_entry(LPVOID p)
{
    BwThread *t = (BwThread *)p;
    ULONG guarantee = 64 * 1024;    /* the crash report's room on an overflow */

    SetThreadStackGuarantee(&guarantee);
    t->fn(t->arg);
    return 0;
}

BwThread *bw_thread_start(void (*fn)(void *arg), void *arg)
{
    BwThread *t = (BwThread *)malloc(sizeof(*t));
    if (t == NULL) {
        bw_panic("bw_thread_start", 0, "out of memory");
    }
    t->fn = fn;
    t->arg = arg;
    t->h = CreateThread(NULL, 0, bw_thread_entry, t, 0, NULL);
    if (t->h == NULL) {
        bw_panic("bw_thread_start", 0, "CreateThread failed");
    }
    return t;
}

void bw_thread_join(BwThread *t)
{
    if (t != NULL) {
        WaitForSingleObject(t->h, INFINITE);
        CloseHandle(t->h);
        free(t);
    }
}

unsigned long long bw_thread_self_id(void)
{
    return (unsigned long long)GetCurrentThreadId() + 1ull;
}

unsigned bw_cpu_count(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (unsigned)si.dwNumberOfProcessors : 1u;
}

unsigned long long bw_clock_ns(void)
{
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart / f.QuadPart) * 1000000000ull
           + (unsigned long long)(c.QuadPart % f.QuadPart) * 1000000000ull
                 / (unsigned long long)f.QuadPart;
}

#elif defined(__wasm__)

/*
 * The wasm32 build (sdk/wasm_host.h): threads are fibers of the host and
 * take turns.  A thread runs until it blocks (a lock held by another, a
 * wait, a join, a sleep), then the next one that can run runs.  The lock
 * and the condition need no atomics: nothing runs in between.  This is the
 * run token's own model (sdk/os_thread.c), so the game's threads behave as
 * they do on the native builds; the port's helper threads (the battle map,
 * the 2D workers) run when the others wait.
 */
#include <string.h>
#include <time.h>

#include "wasm_host.h"

#define FIBERS_MAX    64
#define FIBER_STACK   (512u * 1024u)    /* the module stack of a thread */

enum { F_FREE, F_READY, F_BLOCKED, F_DONE };

struct BwMutex  { int locked; };
struct BwCond   { unsigned gen; };
struct BwThread { int id; };

typedef struct Fiber {
    int state;
    const void *wait;               /* what a blocked fiber waits for       */
    unsigned gen;                   /* the condition's generation it saw    */
    unsigned long long until;       /* a sleeping fiber: when it wakes (ns) */
    void (*fn)(void *);
    void *arg;
    unsigned char *stack;
    int joined;                     /* the handle was joined: free it all   */
} Fiber;

static Fiber g_f[FIBERS_MAX];
static int g_cur;                   /* fiber 0 is the program's own thread  */
static int g_inited;

static void fibers_init(void)
{
    if (!g_inited) {
        g_inited = 1;
        g_f[0].state = F_READY;
    }
}

/* Whether fiber i can run now. */
static int fiber_ready(int i)
{
    const Fiber *f = &g_f[i];

    if (f->state == F_READY) {
        return 1;
    }
    if (f->state != F_BLOCKED) {
        return 0;
    }
    if (f->until != 0) {
        return bw_clock_ns() >= f->until;
    }
    return 0;
}

/* Wake every fiber blocked on `what`. */
static void fiber_wake(const void *what)
{
    int i;

    for (i = 0; i < FIBERS_MAX; i++) {
        if (g_f[i].state == F_BLOCKED && g_f[i].wait == what && g_f[i].until == 0) {
            g_f[i].state = F_READY;
            g_f[i].wait = NULL;
        }
    }
}

/* The current fiber has blocked (or yields): run the next one that can.
 * Returns when this fiber runs again. */
static void fiber_schedule(void)
{
    int k, me = g_cur;

    for (;;) {
        unsigned long long now = 0, soonest = 0;

        for (k = 1; k <= FIBERS_MAX; k++) {
            int i = (me + k) % FIBERS_MAX;

            if (fiber_ready(i)) {
                g_f[i].state = F_READY;
                g_f[i].until = 0;
                if (i == me) {
                    return;
                }
                g_cur = i;
                if (g_f[me].state == F_DONE) {
                    bw_host_fiber_exit(i);
                } else {
                    bw_host_fiber_switch(i);
                }
                return;
            }
        }
        /* nothing can run: sleep until the first sleeper wakes */
        for (k = 0; k < FIBERS_MAX; k++) {
            if (g_f[k].state == F_BLOCKED && g_f[k].until != 0 &&
                (soonest == 0 || g_f[k].until < soonest)) {
                soonest = g_f[k].until;
            }
        }
        if (soonest == 0) {
            bw_panic("bw_thread", 0, "every thread waits for another: deadlock");
        }
        now = bw_clock_ns();
        if (soonest > now) {
            bw_host_sleep_ns(soonest - now);
        }
    }
}

/* The module's entry for a new fiber (sdk/wasm_host.h). */
__attribute__((export_name("bw_fiber_main"))) void bw_fiber_main(int id);
void bw_fiber_main(int id)
{
    Fiber *f = &g_f[id];

    f->fn(f->arg);
    f->state = F_DONE;
    fiber_wake(f);
    fiber_schedule();               /* never returns */
}

BwMutex *bw_mutex_create(void)
{
    BwMutex *m = (BwMutex *)calloc(1, sizeof(*m));
    if (m == NULL) {
        bw_panic("bw_mutex_create", 0, "out of memory");
    }
    return m;
}

void bw_mutex_destroy(BwMutex *m)
{
    free(m);
}

void bw_mutex_lock(BwMutex *m)
{
    fibers_init();
    while (m->locked) {
        g_f[g_cur].state = F_BLOCKED;
        g_f[g_cur].wait = m;
        fiber_schedule();
    }
    m->locked = 1;
}

void bw_mutex_unlock(BwMutex *m)
{
    m->locked = 0;
    fiber_wake(m);
}

BwCond *bw_cond_create(void)
{
    BwCond *c = (BwCond *)calloc(1, sizeof(*c));
    if (c == NULL) {
        bw_panic("bw_cond_create", 0, "out of memory");
    }
    return c;
}

void bw_cond_destroy(BwCond *c)
{
    free(c);
}

void bw_cond_wait(BwCond *c, BwMutex *m)
{
    fibers_init();
    bw_mutex_unlock(m);
    g_f[g_cur].state = F_BLOCKED;
    g_f[g_cur].wait = c;
    fiber_schedule();
    bw_mutex_lock(m);
}

void bw_cond_broadcast(BwCond *c)
{
    c->gen++;
    fiber_wake(c);
}

BwThread *bw_thread_start(void (*fn)(void *arg), void *arg)
{
    BwThread *t = (BwThread *)malloc(sizeof(*t));
    int i;

    fibers_init();
    for (i = 1; i < FIBERS_MAX && g_f[i].state != F_FREE; i++) {
    }
    if (t == NULL || i >= FIBERS_MAX) {
        bw_panic("bw_thread_start", 0, "no fiber left");
    }
    memset(&g_f[i], 0, sizeof(g_f[i]));
    g_f[i].stack = (unsigned char *)malloc(FIBER_STACK);
    if (g_f[i].stack == NULL) {
        bw_panic("bw_thread_start", 0, "out of memory");
    }
    g_f[i].fn = fn;
    g_f[i].arg = arg;
    g_f[i].state = F_READY;
    bw_host_fiber_create(i, (unsigned)(unsigned long)(g_f[i].stack + FIBER_STACK) & ~15u);
    t->id = i;
    return t;
}

void bw_thread_join(BwThread *t)
{
    Fiber *f;

    if (t == NULL) {
        return;
    }
    f = &g_f[t->id];
    while (f->state != F_DONE) {
        g_f[g_cur].state = F_BLOCKED;
        g_f[g_cur].wait = f;
        fiber_schedule();
    }
    /* its host fiber was freed when it was left; the module stack now */
    free(f->stack);
    memset(f, 0, sizeof(*f));
    free(t);
}

unsigned long long bw_thread_self_id(void)
{
    fibers_init();
    return (unsigned long long)g_cur + 1ull;    /* the fiber */
}

void bw_thread_exit(void)
{
    fibers_init();
    g_f[g_cur].state = F_DONE;
    fiber_wake(&g_f[g_cur]);
    fiber_schedule();
    __builtin_unreachable();
}

/* Sleep this thread: the others run meanwhile (sdk/plat_thread.h). */
void bw_thread_sleep_ns(unsigned long long ns)
{
    fibers_init();
    g_f[g_cur].state = F_BLOCKED;
    g_f[g_cur].wait = NULL;
    g_f[g_cur].until = bw_clock_ns() + (ns > 0 ? ns : 1);
    fiber_schedule();
}

unsigned bw_cpu_count(void)
{
    return 1u;                      /* the fibers take turns on one thread */
}

unsigned long long bw_clock_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull
           + (unsigned long long)ts.tv_nsec;
}

#else /* POSIX */

#include <pthread.h>
#include <unistd.h>
#include <time.h>

struct BwMutex  { pthread_mutex_t mx; };
struct BwCond   { pthread_cond_t cv; };
struct BwThread { pthread_t th; void (*fn)(void *); void *arg; };

BwMutex *bw_mutex_create(void)
{
    BwMutex *m = (BwMutex *)malloc(sizeof(*m));
    if (m == NULL || pthread_mutex_init(&m->mx, NULL) != 0) {
        bw_panic("bw_mutex_create", 0, "pthread_mutex_init failed");
    }
    return m;
}

void bw_mutex_destroy(BwMutex *m)
{
    if (m != NULL) {
        pthread_mutex_destroy(&m->mx);
        free(m);
    }
}

void bw_mutex_lock(BwMutex *m)   { pthread_mutex_lock(&m->mx); }
void bw_mutex_unlock(BwMutex *m) { pthread_mutex_unlock(&m->mx); }

BwCond *bw_cond_create(void)
{
    BwCond *c = (BwCond *)malloc(sizeof(*c));
    if (c == NULL || pthread_cond_init(&c->cv, NULL) != 0) {
        bw_panic("bw_cond_create", 0, "pthread_cond_init failed");
    }
    return c;
}

void bw_cond_destroy(BwCond *c)
{
    if (c != NULL) {
        pthread_cond_destroy(&c->cv);
        free(c);
    }
}

void bw_cond_wait(BwCond *c, BwMutex *m)
{
    pthread_cond_wait(&c->cv, &m->mx);
}

void bw_cond_broadcast(BwCond *c)
{
    pthread_cond_broadcast(&c->cv);
}

static void *bw_thread_entry(void *p)
{
    BwThread *t = (BwThread *)p;
    t->fn(t->arg);
    return NULL;
}

BwThread *bw_thread_start(void (*fn)(void *arg), void *arg)
{
    BwThread *t = (BwThread *)malloc(sizeof(*t));
    if (t == NULL) {
        bw_panic("bw_thread_start", 0, "out of memory");
    }
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->th, NULL, bw_thread_entry, t) != 0) {
        bw_panic("bw_thread_start", 0, "pthread_create failed");
    }
    return t;
}

void bw_thread_join(BwThread *t)
{
    if (t != NULL) {
        pthread_join(t->th, NULL);
        free(t);
    }
}

unsigned long long bw_thread_self_id(void)
{
    /* pthread_t is an integer or a pointer on every POSIX host built */
    return (unsigned long long)(uintptr_t)pthread_self() | 1ull << 63;
}

unsigned bw_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return n > 0 ? (unsigned)n : 1u;
}

unsigned long long bw_clock_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull
           + (unsigned long long)ts.tv_nsec;
}

#endif

/* ------------------------------------------------------------------ */
/* Local time, every platform                                          */
/* ------------------------------------------------------------------ */

#include <time.h>
#if defined(__wasm__)
#include "wasm_host.h"
#endif

int bw_localtime(long long t, struct tm *out)
{
    time_t tt;
    struct tm *r;

#if defined(__wasm__)
    tt = (time_t)(t + bw_host_tz_offset(t));
    r = gmtime(&tt);
#else
    tt = (time_t)t;
    r = localtime(&tt);
#endif
    if (r == NULL) {
        return -1;
    }
    *out = *r;
    return 0;
}
