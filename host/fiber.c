/*
 * host/fiber.c -- the module's threads as fibers of this program's one
 * thread (sdk/wasm_host.h, sdk/plat_thread.c's wasm32 part).
 *
 * A fiber has its own native stack, which the translated code runs on, and
 * the module stack pointer it had when it was left: __stack_pointer is a
 * global of the module, one for all its threads, so a switch puts the next
 * fiber's value in place.  The module decides who runs; this only switches.
 * A fiber that is over is freed once another one runs (a fiber cannot free
 * the stack it runs on).
 *
 * Windows has fibers of its own; elsewhere ucontext does the same, except
 * on Android (bionic has no makecontext or swapcontext) and iOS (where they
 * are unavailable), where a few lines of arm64 assembly switch the stacks.
 */

/* Before every header: on macOS, ucontext_t holds the saved registers only
 * when _XOPEN_SOURCE is defined where its type is first seen (stdlib.h
 * already brings it in).  Without them the context is 64 bytes, and
 * getcontext and swapcontext write the registers past its end, over the
 * fiber table.  _DARWIN_C_SOURCE and _DEFAULT_SOURCE keep the rest of the
 * system's names (macOS, glibc). */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "host.h"

#if defined(__aarch64__) && (defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) \
                             || defined(__SWITCH__))
#define HOST_FIBER_ASM 1
#endif

#define FIBERS_MAX   64
#if defined(__SWITCH__)
/* Horizon's heap is committed when allocated (no pages made on first
 * touch), so each fiber's stack costs its whole size at once. */
#define NATIVE_STACK (4u * 1024u * 1024u)
#else
#define NATIVE_STACK (16u * 1024u * 1024u)   /* reserved; committed as used */
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef void *NativeFiber;
#elif defined(HOST_FIBER_ASM)
#include <time.h>
typedef struct { void *sp; void *stack; } *NativeFiber;
#else
#include <time.h>
#include <ucontext.h>
typedef struct { ucontext_t uc; void *stack; } *NativeFiber;
#if defined(__APPLE__)
/* the registers' room is in the context (see the top of this file) */
_Static_assert(sizeof(((ucontext_t *)0)->__mcontext_data) > 0,
               "ucontext_t has no room for the registers");
#endif
#endif

typedef struct {
    NativeFiber fiber;
    uint32_t sp;                /* its __stack_pointer while it is left    */
    int dead;
} HostFiber;

static HostFiber g_fib[FIBERS_MAX];
static int g_cur;

#if (defined(__APPLE__) && defined(__aarch64__)) || defined(HOST_FIBER_ASM)
/* Referenced by the assembly entry below. */
__attribute__((used, noinline))
#endif
static void fiber_run(int id)
{
    w2c_game_bw_fiber_main(&g_game, (u32)id);
    fprintf(stderr, "eidos_host: fiber %d returned\n", id);   /* never */
    abort();
}

#if defined(_WIN32)

static VOID CALLBACK fiber_proc(LPVOID p)
{
    fiber_run((int)(intptr_t)p);
}

void host_fibers_init(void)
{
    g_fib[0].fiber = ConvertThreadToFiber(NULL);
    if (g_fib[0].fiber == NULL) {
        fprintf(stderr, "eidos_host: no fibers (%lu)\n", (unsigned long)GetLastError());
        exit(1);
    }
}

static NativeFiber native_create(int id)
{
    return CreateFiberEx(64u * 1024u, NATIVE_STACK, 0, fiber_proc, (LPVOID)(intptr_t)id);
}

static void native_switch(NativeFiber to) { SwitchToFiber(to); }
static void native_free(NativeFiber f) { DeleteFiber(f); }

#elif defined(HOST_FIBER_ASM)

#if defined(__APPLE__)
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif

/* host_ctx_swap(&from->sp, to->sp): the registers a call must keep (x19 to
 * x30, d8 to d15) go on the stack being left, whose pointer is stored; the
 * other stack's are loaded the same way, and its x30 is where it returns.
 * A new fiber's first frame returns into host_ctx_entry with its id in x19,
 * and a zero frame pointer ends its frame chain (a backtrace stops there). */
void host_ctx_swap(void **save, void *load);
void host_ctx_entry(void);

__asm__(".text\n"
        ".p2align 2\n"
        ".globl " ASM_NAME(host_ctx_swap) "\n"
        ASM_NAME(host_ctx_swap) ":\n"
        "    sub sp, sp, #160\n"
        "    stp x19, x20, [sp, #0]\n"
        "    stp x21, x22, [sp, #16]\n"
        "    stp x23, x24, [sp, #32]\n"
        "    stp x25, x26, [sp, #48]\n"
        "    stp x27, x28, [sp, #64]\n"
        "    stp x29, x30, [sp, #80]\n"
        "    stp d8, d9, [sp, #96]\n"
        "    stp d10, d11, [sp, #112]\n"
        "    stp d12, d13, [sp, #128]\n"
        "    stp d14, d15, [sp, #144]\n"
        "    mov x9, sp\n"
        "    str x9, [x0]\n"
        "    mov sp, x1\n"
        "    ldp x19, x20, [sp, #0]\n"
        "    ldp x21, x22, [sp, #16]\n"
        "    ldp x23, x24, [sp, #32]\n"
        "    ldp x25, x26, [sp, #48]\n"
        "    ldp x27, x28, [sp, #64]\n"
        "    ldp x29, x30, [sp, #80]\n"
        "    ldp d8, d9, [sp, #96]\n"
        "    ldp d10, d11, [sp, #112]\n"
        "    ldp d12, d13, [sp, #128]\n"
        "    ldp d14, d15, [sp, #144]\n"
        "    add sp, sp, #160\n"
        "    ret\n"
        ".globl " ASM_NAME(host_ctx_entry) "\n"
        ASM_NAME(host_ctx_entry) ":\n"
        "    mov x29, xzr\n"
        "    mov x30, xzr\n"
        "    mov x0, x19\n"
        "    bl " ASM_NAME(fiber_run) "\n"
        "    brk #0\n");

static struct { void *sp; void *stack; } g_main_fiber;

#if defined(__ANDROID__)
/* Android: Java code a fiber calls through JNI (SDL's, for one) runs on
 * that fiber's stack, and ART takes a JNI reference outside the thread's
 * own stack for a bad one and aborts.  So the stacks are slots of the
 * game thread's stack (host/main.c): slot 0 is left to ART's guard pages,
 * fiber N has slot N, and the thread itself runs in the last one. */
static char *g_region;

size_t host_fiber_region_size(void)
{
    return (size_t)(FIBERS_MAX + 1) * NATIVE_STACK;
}

void host_fiber_region(void *base)
{
    g_region = (char *)base;
}
#endif

void host_fibers_init(void)
{
    g_fib[0].fiber = (NativeFiber)&g_main_fiber;
}

static void *stack_alloc(int id)
{
#if defined(__ANDROID__)
    if (g_region != NULL) {
        return g_region + (size_t)id * NATIVE_STACK;
    }
#endif
    (void)id;
    return malloc(NATIVE_STACK);
}

static NativeFiber native_create(int id)
{
    NativeFiber f = (NativeFiber)calloc(1, sizeof(*f));
    void **frame;

    if (f == NULL || (f->stack = stack_alloc(id)) == NULL) {
        return NULL;
    }
    frame = (void **)((((uintptr_t)f->stack + NATIVE_STACK) & ~(uintptr_t)15) - 160u);
    memset(frame, 0, 160);
    frame[0] = (void *)(intptr_t)id;            /* x19 */
    frame[11] = (void *)host_ctx_entry;         /* x30 */
    f->sp = frame;
    return f;
}

static void native_free(NativeFiber f)
{
#if defined(__ANDROID__)
    if (g_region != NULL) {
        free(f);
        return;
    }
#endif
    free(f->stack);
    free(f);
}

#else

#if defined(__APPLE__) && defined(__aarch64__)
/* Darwin's makecontext leaves a frame chain into _ctx_start above the
 * fiber stack. Metal takes a backtrace during context creation, and its
 * walker follows that chain outside the allocation. Give this independent
 * stack a terminating frame pointer before entering any compiled C code.
 * Tail-branch so the C prologue saves the zero frame pointer. */
__attribute__((naked)) static void fiber_proc(int id)
{
    __asm__("mov x29, xzr\n"
            "b _fiber_run");
}
#else
static void fiber_proc(int id)
{
    fiber_run(id);
}
#endif

static struct { ucontext_t uc; void *stack; } g_main_fiber;

void host_fibers_init(void)
{
    g_fib[0].fiber = (NativeFiber)&g_main_fiber;
}

static NativeFiber native_create(int id)
{
    NativeFiber f = (NativeFiber)calloc(1, sizeof(*f));

    if (f == NULL || (f->stack = malloc(NATIVE_STACK)) == NULL) {
        return NULL;
    }
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = f->stack;
    f->uc.uc_stack.ss_size = NATIVE_STACK;
    f->uc.uc_link = NULL;
    makecontext(&f->uc, (void (*)(void))fiber_proc, 1, id);
    return f;
}

static void native_free(NativeFiber f)
{
    free(f->stack);
    free(f);
}

#endif

/* Free the fibers that are over, but never the one running. */
static void reap(void)
{
    int i;

    for (i = 1; i < FIBERS_MAX; i++) {
        if (g_fib[i].dead && i != g_cur && g_fib[i].fiber != NULL) {
            native_free(g_fib[i].fiber);
            g_fib[i].fiber = NULL;
            g_fib[i].dead = 0;
        }
    }
}

static void switch_to(int id)
{
    u32 *sp = w2c_game_0x5F_stack_pointer(&g_game);
    int from = g_cur;

    if (id < 0 || id >= FIBERS_MAX || g_fib[id].fiber == NULL) {
        fprintf(stderr, "eidos_host: switch to fiber %d that does not exist\n", id);
        abort();
    }
    g_fib[from].sp = *sp;
    *sp = g_fib[id].sp;
    g_cur = id;
#if defined(_WIN32)
    native_switch(g_fib[id].fiber);
#elif defined(HOST_FIBER_ASM)
    host_ctx_swap(&g_fib[from].fiber->sp, g_fib[id].fiber->sp);
#else
    swapcontext(&g_fib[from].fiber->uc, &g_fib[id].fiber->uc);
#endif
    /* this fiber runs again */
    reap();
}

void w2c_eidos_fiber_create(struct w2c_eidos *e, u32 id, u32 stack_top)
{
    (void)e;
    if (id == 0 || id >= FIBERS_MAX) {
        fprintf(stderr, "eidos_host: bad fiber id %u\n", (unsigned)id);
        abort();
    }
    reap();
    g_fib[id].fiber = native_create((int)id);
    g_fib[id].sp = stack_top;
    g_fib[id].dead = 0;
    if (g_fib[id].fiber == NULL) {
        fprintf(stderr, "eidos_host: no fiber for thread %u\n", (unsigned)id);
        abort();
    }
}

void w2c_eidos_fiber_switch(struct w2c_eidos *e, u32 id)
{
    (void)e;
    switch_to((int)id);
}

void w2c_eidos_fiber_exit(struct w2c_eidos *e, u32 id)
{
    (void)e;
    g_fib[g_cur].dead = 1;
    switch_to((int)id);
    fprintf(stderr, "eidos_host: a finished fiber ran again\n");
    abort();
}

void w2c_eidos_sleep_ns(struct w2c_eidos *e, u64 ns)
{
    (void)e;
#if defined(_WIN32)
    Sleep((DWORD)((ns + 999999u) / 1000000u));
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ns / 1000000000u);
    ts.tv_nsec = (long)(ns % 1000000000u);
    nanosleep(&ts, NULL);
#endif
}

u32 w2c_eidos_cpu_count(struct w2c_eidos *e)
{
    (void)e;
#if defined(_WIN32)
    {
        SYSTEM_INFO si;

        GetSystemInfo(&si);
        return si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1u;
    }
#else
    return 1u;
#endif
}
