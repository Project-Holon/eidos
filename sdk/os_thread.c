/*
 * sdk/os_thread.c -- OS threads (os_thread.o) on host threads, under one
 * run token.
 *
 * Why the module is a shim and not the matched C
 *
 *   aletheia has nearly all of os_thread.o as EXACT C (src/matched/arm9_sdk/
 *   func_02085264.c OS_InitThread through func_02085B60.c
 *   OS_EnableScheduler, plus OS_SleepThread.c, OS_WakeupThread.c,
 *   OS_SelectThread.c, OSi_RescheduleThread.c), and its bodies are what this
 *   file transcribes.  It cannot be compiled as written for two reasons.
 *   The switch itself is the CPU: OSi_RescheduleThread ends in
 *   OS_SaveContext / OS_LoadContext, register-file copies in asm.  And the
 *   module's .bss at 0x02150FD0 is reached under three spellings that are
 *   one object on the DS and three on a host: `g_02150FD0.OSi_ThreadInfo`
 *   (the module's own files), `OSi_ThreadInfo` (os_mutex.o, the CARD and
 *   NVRAM code) and `g_02150FEC` (the overlays).  So the whole module is
 *   here, transcribed function by function against the struct in sdk/os.h,
 *   and the matched files of os_thread.o must stay out of the build.  The
 *   one object exported for the rest of the matched code is
 *   `OSi_ThreadInfo`, which is what every caller outside the module reads.
 *
 * The run token (docs/boot.md, "Threads, interrupts and the frame")
 *
 *   Each OSThread runs on a host thread of its own, started the first time
 *   the scheduler selects it.  Exactly one of them holds the token and runs
 *   game code; every other one is blocked in host_switch() or in its start
 *   trampoline.  The token moves exactly where the DS would switch context:
 *   in OSi_RescheduleThread (sleep, wakeup, yield, priority change, exit)
 *   and at the end of an interrupt (bw_os_irq_thread_switch).  Because only
 *   the token holder touches game state, the game's own single-core
 *   assumptions stay true and the thread list, queues and OSThread fields
 *   need no host locking at all; the lock guards the token itself, the
 *   interrupt latch and the halt wait.
 *
 *   A thread resumed on the DS returns from OS_SaveContext with 1 and leaves
 *   OSi_RescheduleThread; here host_switch() returns when the token comes
 *   back, and the code after it is the same.  A thread that terminated
 *   never resumes: its host thread longjmps back to its trampoline and
 *   returns (thread_end; in the wasm32 build, which has no longjmp, the
 *   thread's fiber ends there instead, sdk/plat_thread.c).  The launcher thread is the host thread that called
 *   bw_os_reset(); it has no trampoline, so if it ever terminates it parks.
 *
 *   Host-only state (the host thread, its saved CPSR, a pending "restart in
 *   OSi_ExitThread" from OS_KillThread) lives in a side table keyed by the
 *   OSThread pointer, never in the game's struct.
 *
 * The restart (docs/boot.md, "Threads, interrupts and the frame")
 *
 *   bw_os_stop_threads stops every game thread but the caller's, and
 *   bw_os_thread_reset (the OS layer's power-on) does the same and keeps
 *   the caller's record.  The caller is the thread that holds the run
 *   token, which is the thread that runs code now: for the port's restart
 *   that is the frame thread, the one that runs the port's own code (the
 *   frame loop) while it holds the token.  A thread running the game's own
 *   code cannot be the caller, because the reset clears the memory the
 *   game runs in.
 *
 *   The caller's host thread is neither killed nor joined, so the caller
 *   carries on and can start the game again; the next run's idle thread is
 *   that same host thread, so the frame loop stays where it is.  The other
 *   threads are blocked (the token holder is the only one that runs), and
 *   the kill flag is honoured where they block: in wait_token and in the
 *   halt wait, so that a join does not hang.  The launcher has no host
 *   thread of its own and cannot be killed; it is retired instead: it
 *   never resumes, and the next run's launcher is a record of its own.
 *
 *   The token is handed over when a thread dies (the trampoline's end
 *   clears it and wakes the waiters), so that a wait for a token that will
 *   never come does not hang.
 *
 * The context
 *
 *   OSThread.context is the game's, but its registers are the host thread's
 *   and the port never reads them: OS_CreateThread zeroes it rather than
 *   building an ARM frame nobody will load.  stackTop/stackBottom hold the
 *   integer the matched OS_CreateThread would store on this host (exact on
 *   ILP32; the low half of the address on LP64), and the two stack canaries
 *   are written where the SDK writes them.  The launcher's are the DS's own
 *   DTCM numbers; the game's stack checks are dead-stripped from this image
 *   (OS_GetStackStatus has no address in layout/symbols_sdk.txt), so nothing
 *   reads either.
 */

#if !defined(__wasm__)
#include <setjmp.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"
#include "plat_thread.h"
#include "port.h"

/* Declared here rather than in os.h: the SDK names a game file may also
 * declare with its own prototypes. */
void OS_Halt(void);
u32  OS_DisableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);
u32  OS_EnableInterrupts(void);
u32  OS_GetProcMode(void);
void OS_Terminate(void);
void OS_CreateAlarm(OSAlarm *alarm);
void OS_SetAlarm(OSAlarm *alarm, u64 tick, OSAlarmHandler handler, void *arg);
void OS_CancelAlarm(OSAlarm *alarm);

void      OS_CreateThread(OSThread *thread, void (*func)(void *), void *arg,
                          void *stack, u32 stackSize, u32 prio);
void      OS_ExitThread(void);
void      OS_RescheduleThread(void);
void      OS_WakeupThread(OSThreadQueue *queue);
u32       OS_DisableScheduler(void);
u32       OS_EnableScheduler(void);
int       OS_SetThreadPriority(OSThread *thread, u32 prio);
OSThread *OS_SelectThread(void);
void      OS_SleepThread(OSThreadQueue *queue);
void      OS_WakeupThreadDirect(OSThread *thread);
OSMutex  *OSi_RemoveMutexLinkFromQueue(OSMutexQueue *queue);

#define BW_STATIC_ASSERT(name, cond) typedef char bw_static_assert_##name[(cond) ? 1 : -1]

/* On a 32-bit target the game's structs are the DS's, byte for byte. */
BW_STATIC_ASSERT(thread_size,  sizeof(void *) != 4 || sizeof(OSThread) == 0xC0);
BW_STATIC_ASSERT(thread_state, sizeof(void *) != 4 || offsetof(OSThread, state) == 0x64);
BW_STATIC_ASSERT(thread_link,  sizeof(void *) != 4 || offsetof(OSThread, link) == 0x7C);
BW_STATIC_ASSERT(thread_join,  sizeof(void *) != 4 || offsetof(OSThread, joinQueue) == 0x9C);
BW_STATIC_ASSERT(thread_errno, sizeof(void *) != 4 || offsetof(OSThread, systemErrno) == 0xBC);
BW_STATIC_ASSERT(info_size,    sizeof(void *) != 4 || sizeof(OSThreadInfo) == 0x10);
BW_STATIC_ASSERT(fnptr_size,   sizeof(OSSwitchThreadCallback) == sizeof(void *));

/* ------------------------------------------------------------------ */
/* The module's data (os_thread.o .bss)                                */
/* ------------------------------------------------------------------ */

OSThreadInfo OSi_ThreadInfo;
OSThread     OSi_LauncherThread;
OSThread     OSi_IdleThread;

static u32  g_idle_stack[50];          /* OSi_IdleThreadStack            */
static u32  g_reschedule_count;        /* OSi_RescheduleCount            */
static int  g_thread_initialized;      /* OSi_IsThreadInitialized        */
static u32  g_thread_id_count;         /* OSi_ThreadIdCount              */
static OSSwitchThreadCallback g_system_switch_cb;  /* ...InSwitchThread  */

/* The launcher's stack on the DS (OS_InitThread): DTCM + 0x3F80 minus the
 * IRQ stack (0x800) is the bottom, and the system stack (0x1800) below it. */
#define DS_LAUNCHER_STACK_BOTTOM (0x02FE0000u + 0x3F80u - 0x800u)
#define DS_LAUNCHER_STACK_TOP    (DS_LAUNCHER_STACK_BOTTOM - 0x1800u)

/* ------------------------------------------------------------------ */
/* Host side: the lock, the token, one record per OSThread             */
/* ------------------------------------------------------------------ */

typedef struct HostRec {
    OSThread *thread;
    BwThread *host;          /* NULL until first selected (or launcher)   */
    int       started;
    int       launcher;
    int       kill;          /* destroyed while blocked: leave            */
    int       finished;      /* the host thread has returned              */
    int       redirect;      /* OS_KillThread: resume in OSi_ExitThread   */
    void     *redirect_arg;
    void    (*fn)(void *);
    void     *arg;
    u32       cpsr;          /* the thread's CPSR I/F bits while out      */
    int       kept;          /* the caller's: its host thread runs on     */
    int       retired;       /* the thread must not resume                */
#if !defined(__wasm__)
    jmp_buf   exit_to;
#endif
} HostRec;

static BwMutex   *g_mx;
static BwCond    *g_cv;
static OSThread  *g_token;
static HostRec  **g_recs;
static int        g_nrecs;
static int        g_caprecs;
static HostRec   *g_cur;     /* the record running code now (the token's) */

static void thread_over(HostRec *r);

/* Lock not held.  The thread is over: back to its trampoline, which
 * returns (thread_over); in the wasm32 build its fiber ends here. */
#if defined(__wasm__)
static void thread_end(HostRec *r)
{
    thread_over(r);
    bw_thread_exit();
}
#else
static void thread_end(HostRec *r)
{
    longjmp(r->exit_to, 1);
}
#endif

void bw_os_lock(void)     { bw_mutex_lock(g_mx); }
void bw_os_unlock(void)   { bw_mutex_unlock(g_mx); }
void bw_os_wake_all(void) { bw_cond_broadcast(g_cv); }

/*
 * The wait in OS_Halt (sdk/os_interrupt.c): the thread is blocked with the
 * lock held, and the kill flag is honoured here as it is in wait_token, so
 * that a thread destroyed while it halts leaves and its host thread can be
 * joined.  Without it a reset that stopped a halted thread would wait for
 * it for ever.
 */
void bw_os_wait(void)
{
    HostRec *r = g_cur;

    if (r != NULL && r->kill && !r->launcher) {
        bw_mutex_unlock(g_mx);
        thread_end(r);
    }
    bw_cond_wait(g_cv, g_mx);
}

static HostRec *rec_find(const OSThread *t)
{
    int i;

    for (i = 0; i < g_nrecs; i++) {
        if (!g_recs[i]->retired && g_recs[i]->thread == t) {
            return g_recs[i];
        }
    }
    return NULL;
}

/*
 * The record of a thread whatever its state: a retired thread (the
 * launcher of a run that was reset) is still running, and the scheduler
 * must find its record when it switches away from it.  A live record wins:
 * the next run's launcher is a record of its own, and the retired one is
 * the same thread object.
 */
static HostRec *rec_find_any(const OSThread *t)
{
    HostRec *old = NULL;
    int i;

    for (i = 0; i < g_nrecs; i++) {
        if (g_recs[i]->thread == t) {
            if (!g_recs[i]->retired) {
                return g_recs[i];
            }
            if (old == NULL) {
                old = g_recs[i];
            }
        }
    }
    return old;
}

static HostRec *rec_get(OSThread *t)
{
    HostRec *r = rec_find(t);

    if (r != NULL) {
        return r;
    }
    if (g_nrecs == g_caprecs) {
        int cap = g_caprecs ? g_caprecs * 2 : 16;
        HostRec **n = (HostRec **)realloc(g_recs, (size_t)cap * sizeof(*n));
        if (n == NULL) {
            bw_panic("OS_CreateThread", 0x020853C0u, "out of memory for the host thread table");
        }
        g_recs = n;
        g_caprecs = cap;
    }
    r = (HostRec *)calloc(1, sizeof(*r));
    if (r == NULL) {
        bw_panic("OS_CreateThread", 0x020853C0u, "out of memory for the host thread table");
    }
    r->thread = t;
    g_recs[g_nrecs++] = r;
    return r;
}

/* Stop a record's host thread (it must not hold the token) and forget it. */
static void rec_stop(HostRec *r)
{
    BwThread *h;

    bw_mutex_lock(g_mx);
    r->kill = 1;
    bw_cond_broadcast(g_cv);
    h = r->host;
    r->host = NULL;
    bw_mutex_unlock(g_mx);
    bw_thread_join(h);         /* NULL-safe */
}

static void exit_thread(void *arg);

/* The frame handoff (bw_os_frame_delegate): the frame thread's record, and
 * a frame another thread asked it to run. */
static HostRec      *g_frame_rec;
static unsigned long long g_frame_host; /* its host thread (bw_thread_self_id) */
static BwOsHaltHook  g_freq_hook;
static void         *g_freq_user;
static int           g_freq;       /* a frame is asked for              */
static int           g_serving;    /* the frame thread runs one for it  */

/*
 * Lock held, and `r` is the frame thread: run the frame another thread asked
 * for (bw_os_frame_delegate).  The asker holds the token and is halted, so
 * the frame runs as it would on the asker; the lock is released while it
 * runs, as OS_Halt releases it around the hook.
 */
static void serve_frame(void)
{
    BwOsHaltHook hook = g_freq_hook;
    void *user = g_freq_user;

    g_serving = 1;
    bw_mutex_unlock(g_mx);
    hook(user);
    bw_mutex_lock(g_mx);
    g_serving = 0;
    g_freq = 0;
    bw_cond_broadcast(g_cv);
}

/* Lock held.  Returns, lock still held, once `r` holds the token. */
static void wait_token(HostRec *r)
{
    while (g_token != r->thread) {
        if (r->kill && !r->launcher) {
            bw_mutex_unlock(g_mx);
            thread_end(r);
        }
        if (g_freq && !g_serving && r == g_frame_rec) {
            serve_frame();
            continue;
        }
        bw_cond_wait(g_cv, g_mx);
    }
    if (r->retired) {
        /* The thread this record belongs to was reset while it was
         * waiting: it must not run again.  It parks for good (the reset
         * that retired it has no other way to stop it: the launcher has no
         * host thread of its own to unwind). */
        for (;;) {
            bw_cond_wait(g_cv, g_mx);
        }
    }
}

/* Lock not held.  The thread has the token again: put its CPU state back. */
static void resume(HostRec *r)
{
    g_cur = r;               /* the thread that runs code now */
    bw_os_cpsr_set(r->cpsr);
    if (r->redirect) {
        void *arg = r->redirect_arg;

        r->redirect = 0;
        /* OSi_ExitThread_ArgSpecified's context: IRQs off (cpsr |= 0x80). */
        bw_os_cpsr_set(OS_INTRMODE_IRQ_DISABLE);
        exit_thread(arg);
    }
}

static void trampoline(void *p)
{
    HostRec *r = (HostRec *)p;

    bw_mutex_lock(g_mx);
#if !defined(__wasm__)
    if (setjmp(r->exit_to) != 0) {
        thread_over(r);         /* reached by longjmp, lock released */
        return;
    }
#endif
    wait_token(r);
    bw_mutex_unlock(g_mx);
    r->cpsr = 0;                /* a new context starts with IRQs enabled */
    resume(r);
    r->fn(r->arg);
    OS_ExitThread();            /* the DS context's lr */
    thread_over(r);
}

/* Lock not held: the thread is over. */
static void thread_over(HostRec *r)
{
    bw_mutex_lock(g_mx);
    r->finished = 1;
    /* The token of a thread that dies is handed over: the thread that
     * held it is gone, and a wait for a token that will never come must
     * not hang.  The thread that takes it over is the one the reset
     * leaves running (bw_os_stop_threads gives it to the caller). */
    if (g_token == r->thread) {
        g_token = NULL;
        bw_os_wake_all();
    }
    bw_mutex_unlock(g_mx);
}

/*
 * The context switch: OS_SaveContext(from) + OS_LoadContext(to).  Called by
 * the token holder, `from` being OSi_ThreadInfo's previous current thread.
 */
static void host_switch(OSThread *from, OSThread *to)
{
    HostRec *fr = rec_find_any(from);
    HostRec *tr = rec_get(to);

    if (fr == NULL) {
        bw_panic("OSi_RescheduleThread", 0x020851A0u,
                 "the running thread has no host thread (OS_InitThread not called?)");
    }
    fr->cpsr = bw_os_cpsr_get();

    bw_mutex_lock(g_mx);
    g_token = to;
    if (!tr->started) {
        tr->started = 1;
        tr->host = bw_thread_start(trampoline, tr);
    }
    bw_cond_broadcast(g_cv);

    if (from->state == OS_THREAD_STATE_TERMINATED) {
        if (!fr->launcher) {
            bw_mutex_unlock(g_mx);
            thread_end(fr);
        }
        for (;;) {               /* the launcher has nowhere to return to */
            bw_cond_wait(g_cv, g_mx);
        }
    }
    wait_token(fr);
    bw_mutex_unlock(g_mx);
    resume(fr);
}

/* sdk/os.h */
int bw_os_frame_delegate(BwOsHaltHook hook, void *user)
{
    HostRec *me = g_cur;

    if (g_mx == NULL || me == NULL) {
        return 0;
    }
    bw_mutex_lock(g_mx);
    if (g_frame_rec == NULL || g_frame_rec->retired || g_frame_rec->finished) {
        g_frame_rec = me;          /* the first thread to run a frame */
        g_frame_host = bw_thread_self_id();
    }
    /* The frame thread runs its own frames.  It is told by its host thread
     * as well as by the token: after a restart it is back in the old idle
     * loop while the next run's launcher holds the token, and asking
     * itself for the frame would wait for ever. */
    if (g_frame_rec != NULL && g_frame_rec != me && bw_thread_self_id() == g_frame_host) {
        /* The frame thread after a restart, in its idle loop while the
         * next run's launcher holds the token: it waits for the token as
         * the idle thread, and serves the frames until then (wait_token). */
        HostRec *fr = g_frame_rec;

        wait_token(fr);
        bw_mutex_unlock(g_mx);
        resume(fr);
        return 1;
    }
    if (g_frame_rec == me) {
        bw_mutex_unlock(g_mx);
        return 0;
    }
    g_freq_hook = hook;
    g_freq_user = user;
    g_freq = 1;
    bw_cond_broadcast(g_cv);
    while (g_freq) {
        if (me->kill && !me->launcher) {
            bw_mutex_unlock(g_mx);
            thread_end(me);
        }
        if (me->retired) {
            /* A restart in the frame retired this thread (the launcher):
             * it must not run again (wait_token parks it the same way). */
            for (;;) {
                bw_cond_wait(g_cv, g_mx);
            }
        }
        bw_cond_wait(g_cv, g_mx);
    }
    if (me->retired) {
        for (;;) {
            bw_cond_wait(g_cv, g_mx);
        }
    }
    bw_mutex_unlock(g_mx);
    return 1;
}

int bw_os_host_threads(void)
{
    int i, n = 0;

    if (g_mx == NULL) {
        return 0;
    }
    bw_mutex_lock(g_mx);
    for (i = 0; i < g_nrecs; i++) {
        if (g_recs[i]->started && !g_recs[i]->finished && !g_recs[i]->launcher) {
            n++;
        }
    }
    bw_mutex_unlock(g_mx);
    return n;
}

/* ------------------------------------------------------------------ */
/* Lists and queues (OSi_InsertThreadToList and friends)               */
/* ------------------------------------------------------------------ */

static OSSwitchThreadCallback switch_cb(void)
{
    OSSwitchThreadCallback cb;
    memcpy(&cb, &OSi_ThreadInfo.switchCallback, sizeof(cb));
    return cb;
}

/* func_020850FC: sorted by priority, after every thread of equal priority. */
static void insert_to_list(OSThread *thread)
{
    OSThread *t = OSi_ThreadInfo.list;
    OSThread *pre = NULL;

    while (t != NULL && t->priority < thread->priority) {
        pre = t;
        t = t->next;
    }
    if (pre == NULL) {
        thread->next = OSi_ThreadInfo.list;
        OSi_ThreadInfo.list = thread;
    } else {
        thread->next = pre->next;
        pre->next = thread;
    }
}

/* func_02085158 */
static void remove_from_list(OSThread *thread)
{
    OSThread *t = OSi_ThreadInfo.list;
    OSThread *pre = NULL;

    while (t != NULL && t != thread) {
        pre = t;
        t = t->next;
    }
    if (pre == NULL) {
        OSi_ThreadInfo.list = thread->next;
    } else {
        pre->next = thread->next;
    }
}

/* OSi_InsertLinkToQueue.c: priority order, FIFO among equals. */
static void insert_link(OSThreadQueue *queue, OSThread *thread)
{
    OSThread *next = queue->head;

    while (next != NULL && next->priority <= thread->priority) {
        if (next == thread) {
            return;
        }
        next = next->link.next;
    }
    if (next == NULL) {
        OSThread *prev = queue->tail;

        if (prev == NULL) {
            queue->head = thread;
        } else {
            prev->link.next = thread;
        }
        thread->link.prev = prev;
        thread->link.next = NULL;
        queue->tail = thread;
    } else {
        OSThread *prev = next->link.prev;

        if (prev == NULL) {
            queue->head = thread;
        } else {
            prev->link.next = thread;
        }
        thread->link.prev = prev;
        thread->link.next = next;
        next->link.prev = thread;
    }
}

/* OSi_RemoveLinkFromQueue.c */
static OSThread *remove_link(OSThreadQueue *queue)
{
    OSThread *t = queue->head;

    if (t != NULL) {
        OSThread *next = t->link.next;

        queue->head = next;
        if (next != NULL) {
            next->link.prev = NULL;
        } else {
            queue->tail = NULL;
            t->queue = NULL;
        }
    }
    return t;
}

/* func_02085078 */
static OSThread *remove_specified_link(OSThreadQueue *queue, OSThread *thread)
{
    OSThread *t = queue->head;
    OSThread *next;
    OSThread *prev;

    while (t != NULL) {
        next = t->link.next;
        if (t == thread) {
            prev = t->link.prev;
            if (queue->head == t) {
                queue->head = next;
            } else {
                prev->link.next = next;
            }
            if (queue->tail == t) {
                queue->tail = prev;
            } else {
                next->link.prev = prev;
            }
            break;
        }
        t = next;
    }
    return t;
}

/* func_020850CC (global: os_mutex.o calls it) */
OSMutex *OSi_RemoveMutexLinkFromQueue(OSMutexQueue *queue)
{
    OSMutex *t = queue->head;

    if (t != NULL) {
        OSMutex *next = t->link.next;

        queue->head = next;
        if (next != NULL) {
            next->link.prev = NULL;
        } else {
            queue->tail = NULL;
        }
    }
    return t;
}

/* func_02086088, OSi_UnlockAllMutex (os_mutex.o; here so exit links alone) */
static void unlock_all_mutex(OSThread *thread)
{
    OSMutex *mutex;

    while (thread->mutexQueue.head != NULL) {
        mutex = OSi_RemoveMutexLinkFromQueue(&thread->mutexQueue);
        mutex->count = 0;          /* count and type both cleared */
        mutex->thread = NULL;
        OS_WakeupThread(&mutex->queue);
    }
}

/* 0x020856F8, OSi_CancelThreadAlarmForSleep */
static void cancel_sleep_alarm(OSThread *thread)
{
    if (thread->alarmForSleep != NULL) {
        OS_CancelAlarm(thread->alarmForSleep);
    }
}

/* ------------------------------------------------------------------ */
/* The scheduler                                                       */
/* ------------------------------------------------------------------ */

/* OS_SelectThread.c: the first READY thread in priority order. */
OSThread *OS_SelectThread(void)
{
    OSThread *t = OSi_ThreadInfo.list;

    while (t != NULL && t->state != OS_THREAD_STATE_READY) {
        t = t->next;
    }
    return t;
}

/* OSi_RescheduleThread.c */
static void reschedule(void)
{
    OSThread *cur;
    OSThread *next;
    OSSwitchThreadCallback cb;

    if (g_reschedule_count != 0) {
        return;
    }
    if (OSi_ThreadInfo.irqDepth > 0 || OS_GetProcMode() == OS_PROCMODE_IRQ) {
        OSi_ThreadInfo.isNeedRescheduling = 1;
        return;
    }
    cur = OSi_ThreadInfo.current;
    next = OS_SelectThread();
    if (cur == next || next == NULL) {
        return;
    }
    if (g_system_switch_cb != NULL) {
        g_system_switch_cb(cur, next);
    }
    cb = switch_cb();
    if (cb != NULL) {
        cb(cur, next);
    }
    OSi_ThreadInfo.current = next;
    host_switch(cur, next);
}

/*
 * The thread-switch tail of the IRQ handler (src/matched/itcm_sdk/
 * func_01FF8148.c, OS_IrqHandler_ThreadSwitch, asm): every thread waiting
 * in OS_WaitIrq becomes ready, and if anything asked for a reschedule while
 * in IRQ mode, the highest-priority ready thread gets the CPU.  Like the
 * asm, it does not consult OSi_RescheduleCount and calls only the user
 * switch callback.  Called by sdk/os_interrupt.c with the CPU back in the
 * interrupted thread's mode and CPSR.
 */
void bw_os_irq_thread_switch(void)
{
    OSThread *t;
    OSThread *cur;
    OSThread *next;
    OSSwitchThreadCallback cb;

    t = OSi_IrqThreadQueue.head;
    if (t != NULL) {
        while (t != NULL) {
            OSThread *n = t->link.next;

            t->state = OS_THREAD_STATE_READY;
            t->queue = NULL;
            t->link.prev = NULL;
            t->link.next = NULL;
            t = n;
        }
        OSi_IrqThreadQueue.head = NULL;
        OSi_IrqThreadQueue.tail = NULL;
        OSi_ThreadInfo.isNeedRescheduling = 1;
    }
    if (OSi_ThreadInfo.isNeedRescheduling == 0) {
        return;
    }
    OSi_ThreadInfo.isNeedRescheduling = 0;
    if (!g_thread_initialized) {
        return;
    }
    next = OS_SelectThread();
    cur = OSi_ThreadInfo.current;
    if (next == NULL || next == cur) {
        return;
    }
    cb = switch_cb();
    if (cb != NULL) {
        cb(cur, next);
    }
    OSi_ThreadInfo.current = next;
    host_switch(cur, next);
}

/* 0x020858BC */
void OS_RescheduleThread(void)
{
    u32 e = OS_DisableInterrupts();
    reschedule();
    (void)OS_RestoreInterrupts(e);
}

/* func_02085B30 */
u32 OS_DisableScheduler(void)
{
    u32 e = OS_DisableInterrupts();
    u32 count = 0;

    if (g_reschedule_count < 0xFFFFFFFFu) {
        count = g_reschedule_count++;
    }
    (void)OS_RestoreInterrupts(e);
    return count;
}

/* func_02085B60 */
u32 OS_EnableScheduler(void)
{
    u32 e = OS_DisableInterrupts();
    u32 count = 0;

    if (g_reschedule_count != 0) {
        count = g_reschedule_count--;
    }
    (void)OS_RestoreInterrupts(e);
    return count;
}

/* ------------------------------------------------------------------ */
/* Creation and initialisation                                         */
/* ------------------------------------------------------------------ */

static bw_sdk_u32 addr32(const void *p)
{
    return (bw_sdk_u32)(u32)(uintptr_t)p;
}

static void put32(void *p, u32 v)
{
    memcpy(p, &v, sizeof(v));
}

/* OSi_IdleThreadProc (func_02085B20, matched; transcribed so the module
 * links without it). */
static void idle_proc(void *arg)
{
    (void)arg;
    for (;;) {
        (void)OS_EnableInterrupts();
        OS_Halt();
    }
}

/* func_020853C0 */
void OS_CreateThread(OSThread *thread, void (*func)(void *), void *arg,
                     void *stack, u32 stackSize, u32 prio)
{
    u32 e = OS_DisableInterrupts();
    HostRec *r;

    thread->id = ++g_thread_id_count;          /* OSi_GetUnusedThreadId */
    thread->priority = prio;
    thread->state = OS_THREAD_STATE_WAITING;
    thread->profiler = NULL;
    insert_to_list(thread);

    thread->stackBottom = addr32(stack);
    thread->stackTop = addr32((u8 *)stack - stackSize);
    thread->stackWarningOffset = 0;
    if (stack != NULL && stackSize >= 12u) {
        u8 *bottom = (u8 *)stack;
        u8 *top = bottom - stackSize;

        put32(bottom - 8, 0xFDDB597Du);
        put32(top, 0x7BF9DD5Bu);
        memset(top + 4, 0, stackSize - 12u);
    }
    thread->joinQueue.head = thread->joinQueue.tail = NULL;
    memset(&thread->context, 0, sizeof(thread->context));
    thread->mutex = NULL;
    thread->mutexQueue.head = NULL;
    thread->mutexQueue.tail = NULL;
    thread->destructor = NULL;
    thread->queue = NULL;
    thread->link.prev = thread->link.next = NULL;
    memset(thread->specific, 0, sizeof(thread->specific));
    thread->alarmForSleep = NULL;

    /* A thread object can be created again once its thread is over; its
     * previous host thread (finished, or destroyed while blocked) goes. */
    r = rec_find(thread);
    if (r != NULL && r->kept) {
        /* The kept record's host thread is the caller of the reset: it is
         * running, and it must not be stopped.  The thread object is
         * created again (the game starts again), so the record is
         * re-armed: the host thread stays, and the trampoline running on
         * it goes on with the thread's own entry.  That is the idle
         * thread's case (the frame thread stays the frame thread); any
         * other entry would leave the running host thread on the old
         * code, so the record is retired and the thread gets a record of
         * its own. */
        if (r->fn != func) {
            r->retired = 1;
            r = NULL;
        }
    }
    if (r != NULL && r->host != NULL && !r->kept) {
        rec_stop(r);
    }
    r = rec_get(thread);
    if (r->kept) {
        r->fn = func;
        r->arg = arg;
        r->kill = 0;
        r->finished = 0;
        r->redirect = 0;
        r->redirect_arg = NULL;
        r->cpsr = 0;
        r->kept = 0;
    } else {
        r->host = NULL;
        r->started = 0;
        r->kill = 0;
        r->finished = 0;
        r->redirect = 0;
        r->redirect_arg = NULL;
        r->launcher = 0;
        r->fn = func;
        r->arg = arg;
        r->cpsr = 0;
    }

    (void)OS_RestoreInterrupts(e);
}

/* func_02085264 */
void OS_InitThread(void)
{
    HostRec *r;

    if (g_thread_initialized) {
        return;
    }
    g_thread_initialized = 1;

    OSi_LauncherThread.priority = OS_THREAD_LAUNCHER_PRIORITY;
    OSi_LauncherThread.id = 0;
    OSi_LauncherThread.state = OS_THREAD_STATE_READY;
    OSi_LauncherThread.next = NULL;
    OSi_LauncherThread.profiler = NULL;
    OSi_ThreadInfo.list = &OSi_LauncherThread;

    OSi_LauncherThread.stackBottom = DS_LAUNCHER_STACK_BOTTOM;
    OSi_LauncherThread.stackTop = DS_LAUNCHER_STACK_TOP;
    OSi_LauncherThread.stackWarningOffset = 0;
    OSi_LauncherThread.joinQueue.head = OSi_LauncherThread.joinQueue.tail = NULL;

    OSi_ThreadInfo.isNeedRescheduling = 0;
    OSi_ThreadInfo.irqDepth = 0;
    /* The SDK also stores &OSi_ThreadInfo at 0x02FFFFA0 for the debugger;
     * a host pointer does not fit a DS word, and no game code reads it. */

    r = rec_get(&OSi_LauncherThread);
    r->launcher = 1;
    r->started = 1;
    r->kept = 0;              /* the launcher is a record of its own now */
    bw_mutex_lock(g_mx);
    /* The launcher takes the token, as on the DS.  After a restart the
     * caller held it: the caller is the frame thread, back in its idle
     * loop, and it waits for the token there like the idle thread it is
     * (bw_os_frame_delegate), serving the frames meanwhile.  Left with the
     * caller, the token never came back: the launcher ran without it and
     * its first switch waited for ever (the Title screen restart). */
    g_token = &OSi_LauncherThread;
    OSi_ThreadInfo.current = &OSi_LauncherThread;
    g_cur = r;
    bw_cond_broadcast(g_cv);
    bw_mutex_unlock(g_mx);

    memset(&OSi_ThreadInfo.switchCallback, 0, sizeof(OSi_ThreadInfo.switchCallback));

    OS_CreateThread(&OSi_IdleThread, idle_proc, NULL, g_idle_stack + 50,
                    sizeof(g_idle_stack), OS_THREAD_PRIORITY_MAX);
    OSi_IdleThread.priority = OS_THREAD_IDLE_PRIORITY;
    OSi_IdleThread.state = OS_THREAD_STATE_READY;
}

/* func_020853B0 */
int OS_IsThreadAvailable(void)
{
    return g_thread_initialized;
}

/* ------------------------------------------------------------------ */
/* Exit, destroy, kill, join                                           */
/* ------------------------------------------------------------------ */

/* func_02085560, OSi_ExitThread_Destroy */
static void exit_destroy(void)
{
    OSThread *cur = OSi_ThreadInfo.current;

    (void)OS_DisableScheduler();
    unlock_all_mutex(cur);
    if (cur->queue != NULL) {
        (void)remove_specified_link(cur->queue, cur);
    }
    remove_from_list(cur);
    cur->state = OS_THREAD_STATE_TERMINATED;
    OS_WakeupThread(&cur->joinQueue);
    (void)OS_EnableScheduler();
    OS_RescheduleThread();
    OS_Terminate();            /* unreachable, as on the DS */
}

/* func_02085528, OSi_ExitThread */
static void exit_thread(void *arg)
{
    OSThread *cur = OSi_ThreadInfo.current;
    OSThreadDestructor destructor = cur->destructor;

    if (destructor != NULL) {
        cur->destructor = NULL;
        destructor(arg);
        (void)OS_DisableInterrupts();
    }
    exit_destroy();
}

/*
 * 0x020854B0 (asm in aletheia) and func_020854D0, OSi_ExitThread_
 * ArgSpecified.  With a destructor stack set the SDK reloads the context to
 * run OSi_ExitThread on it; the host thread's own stack does that job.
 */
void OS_ExitThread(void)
{
    (void)OS_DisableInterrupts();
    exit_thread(NULL);
}

/* func_020855BC */
void OS_DestroyThread(OSThread *thread)
{
    u32 e = OS_DisableInterrupts();
    HostRec *r;

    if (OSi_ThreadInfo.current == thread) {
        exit_destroy();
    }
    (void)OS_DisableScheduler();
    unlock_all_mutex(thread);
    cancel_sleep_alarm(thread);
    if (thread->queue != NULL) {
        (void)remove_specified_link(thread->queue, thread);
    }
    remove_from_list(thread);
    thread->state = OS_THREAD_STATE_TERMINATED;
    OS_WakeupThread(&thread->joinQueue);
    (void)OS_EnableScheduler();

    /* Its host thread is blocked waiting for the token; let it go. */
    r = rec_find(thread);
    if (r != NULL && r->started && !r->launcher) {
        bw_mutex_lock(g_mx);
        r->kill = 1;
        bw_cond_broadcast(g_cv);
        bw_mutex_unlock(g_mx);
    }
    (void)OS_RestoreInterrupts(e);
    OS_RescheduleThread();
}

/* func_0208565C */
void OS_KillThreadWithPriority(OSThread *thread, void *arg, u32 prio)
{
    u32 e = OS_DisableInterrupts();
    HostRec *r;

    if (thread == OSi_ThreadInfo.current) {
        exit_thread(arg);      /* OSi_ExitThread_ArgSpecified */
    }
    cancel_sleep_alarm(thread);

    /* OS_InitContext(&thread->context, OSi_ExitThread, ...), r0 = arg,
     * IRQs off: the next time it runs, it runs the destructor and exits. */
    r = rec_get(thread);
    r->redirect = 1;
    r->redirect_arg = arg;
    thread->state = OS_THREAD_STATE_READY;

    (void)OS_DisableScheduler();
    (void)OS_SetThreadPriority(thread, prio);
    (void)OS_EnableScheduler();
    reschedule();
    (void)OS_RestoreInterrupts(e);
}

/* 0x02085638 */
void OS_KillThread(OSThread *thread, void *arg)
{
    OS_KillThreadWithPriority(thread, arg, (u32)thread->priority);
}

/* func_02085710 */
void OS_JoinThread(OSThread *thread)
{
    u32 e = OS_DisableInterrupts();

    while (thread->state != OS_THREAD_STATE_TERMINATED) {
        OS_SleepThread(&thread->joinQueue);
    }
    (void)OS_RestoreInterrupts(e);
}

/* 0x0208574C */
int OS_IsThreadTerminated(const OSThread *thread)
{
    return thread->state == OS_THREAD_STATE_TERMINATED;
}

/* ------------------------------------------------------------------ */
/* Sleep, wake, yield, priority                                        */
/* ------------------------------------------------------------------ */

/* OS_SleepThread.c */
void OS_SleepThread(OSThreadQueue *queue)
{
    u32 e = OS_DisableInterrupts();
    OSThread *cur = OSi_ThreadInfo.current;

    if (queue != NULL) {
        cur->queue = queue;
        insert_link(queue, cur);
    }
    cur->state = OS_THREAD_STATE_WAITING;
    reschedule();
    (void)OS_RestoreInterrupts(e);
}

/* func_02085760 */
void OS_SleepThreadDirect(OSThread *thread, OSThreadQueue *queue)
{
    u32 e = OS_DisableInterrupts();

    if (thread->state == OS_THREAD_STATE_READY) {
        if (queue != NULL) {
            thread->queue = queue;
            insert_link(queue, thread);
        }
        thread->state = OS_THREAD_STATE_WAITING;
        reschedule();
    }
    (void)OS_RestoreInterrupts(e);
}

/* OS_WakeupThread.c */
void OS_WakeupThread(OSThreadQueue *queue)
{
    u32 e = OS_DisableInterrupts();

    if (queue->head != NULL) {
        while (queue->head != NULL) {
            OSThread *t = remove_link(queue);

            t->state = OS_THREAD_STATE_READY;
            t->queue = NULL;
            t->link.prev = t->link.next = NULL;
        }
        queue->tail = NULL;
        queue->head = NULL;
        reschedule();
    }
    (void)OS_RestoreInterrupts(e);
}

/* func_02085868 */
void OS_WakeupThreadDirect(OSThread *thread)
{
    u32 e = OS_DisableInterrupts();

    thread->state = OS_THREAD_STATE_READY;
    reschedule();
    (void)OS_RestoreInterrupts(e);
}

/* func_020858D8: move behind the last thread of the same priority. */
void OS_YieldThread(void)
{
    OSThread *cur = OSi_ThreadInfo.current;
    OSThread *pre = NULL;
    OSThread *last = NULL;
    OSThread *t;
    OSThread *tpre;
    int same = 0;
    u32 e = OS_DisableInterrupts();

    t = OSi_ThreadInfo.list;
    tpre = NULL;
    while (t != NULL) {
        if (t == cur) {
            pre = tpre;
        }
        if (cur->priority == t->priority) {
            last = t;
            same++;
        }
        tpre = t;
        t = t->next;
    }
    if (same <= 1 || last == cur) {
        (void)OS_RestoreInterrupts(e);
        return;
    }
    if (pre == NULL) {
        OSi_ThreadInfo.list = cur->next;
    } else {
        pre->next = cur->next;
    }
    cur->next = last->next;
    last->next = cur;
    reschedule();
    (void)OS_RestoreInterrupts(e);
}

/* func_02085988 */
int OS_SetThreadPriority(OSThread *thread, u32 prio)
{
    OSThread *t = OSi_ThreadInfo.list;
    OSThread *pre = NULL;
    u32 e = OS_DisableInterrupts();

    while (t != NULL && t != thread) {
        pre = t;
        t = t->next;
    }
    if (t == NULL || t == &OSi_IdleThread) {
        (void)OS_RestoreInterrupts(e);
        return 0;
    }
    if (t->priority != prio) {
        if (pre == NULL) {
            OSi_ThreadInfo.list = thread->next;
        } else {
            pre->next = thread->next;
        }
        thread->priority = prio;
        insert_to_list(thread);
        reschedule();
    }
    (void)OS_RestoreInterrupts(e);
    return 1;
}

/* 0x02085A30 */
u32 OS_GetThreadPriority(const OSThread *thread)
{
    return (u32)thread->priority;
}

/* func_02085AD8, OSi_SleepAlarmCallback */
static void sleep_alarm_callback(void *arg)
{
    OSThread *volatile *pp = (OSThread *volatile *)arg;
    OSThread *t = *pp;

    *pp = NULL;
    t->alarmForSleep = NULL;
    OS_WakeupThreadDirect(t);
}

/* func_02085A38 */
void OS_Sleep(u32 msec)
{
    OSAlarm alarm;
    OSThread *volatile p_thread;
    u32 e;

    OS_CreateAlarm(&alarm);
    p_thread = OSi_ThreadInfo.current;
    e = OS_DisableInterrupts();
    p_thread->alarmForSleep = &alarm;
    OS_SetAlarm(&alarm, ((u64)33514u * msec) / 64u, sleep_alarm_callback,
                (void *)&p_thread);
    while (p_thread != NULL) {
        OS_SleepThread(NULL);
    }
    (void)OS_RestoreInterrupts(e);
}

/* func_02085AF8 */
OSSwitchThreadCallback OS_SetSwitchThreadCallback(OSSwitchThreadCallback callback)
{
    u32 e = OS_DisableInterrupts();
    OSSwitchThreadCallback prev = switch_cb();

    memcpy(&OSi_ThreadInfo.switchCallback, &callback, sizeof(callback));
    (void)OS_RestoreInterrupts(e);
    return prev;
}

/* ------------------------------------------------------------------ */
/* Reset and restart                                                   */
/* ------------------------------------------------------------------ */

/*
 * The caller's own record: the record of the thread that holds the run
 * token, which is the thread that is running code now (the token holder is
 * the only one that runs).  For the port's restart that is the frame
 * thread: the thread that runs the port's own code (the frame loop) while
 * it holds the token.
 *
 * A thread running the game's own code cannot be the caller: the reset
 * clears the memory the game runs in.
 */
static HostRec *rec_caller(void)
{
    HostRec *r = NULL;

    if (g_mx == NULL) {
        return NULL;
    }
    bw_mutex_lock(g_mx);
    if (g_serving) {
        /* A frame the frame thread runs for a halted thread: the caller
         * is the frame thread, though the halted thread holds the token. */
        r = g_frame_rec;
    } else if (g_token != NULL) {
        r = rec_find(g_token);
    }
    bw_mutex_unlock(g_mx);
    return r;
}

/*
 * A thread that cannot be killed: the launcher has no host thread of its
 * own (it is the host thread that called bw_os_reset), so there is no
 * trampoline to unwind it with.  It is retired instead: it never resumes
 * (wait_token parks it), and the next run's launcher is a record of its
 * own.  The record is not freed: the retired thread is parked inside it.
 */
static void rec_retire(HostRec *r)
{
    bw_mutex_lock(g_mx);
    r->retired = 1;
    bw_cond_broadcast(g_cv);
    bw_mutex_unlock(g_mx);
}

/*
 * Stop every game thread but the caller's.
 *
 * The caller's record is left alone: its host thread is neither killed nor
 * joined, so the caller carries on and can start the game again.  Every
 * other thread is stopped: the ones with a host thread of their own are
 * killed and joined (they are blocked, and the kill flag is honoured where
 * they block: in wait_token and in the halt wait), and the launcher, which
 * has no host thread of its own, is retired.
 *
 * The token belongs to the caller, and the caller keeps it.  A thread that
 * dies with the token hands it over (the trampoline's end clears it and
 * wakes the waiters), so that a wait does not hang.
 */
static void stop_threads(HostRec *me)
{
    int i;

    if (g_mx == NULL) {
        return;
    }
    for (i = 0; i < g_nrecs; i++) {
        HostRec *r = g_recs[i];

        if (r == me || r->retired) {
            continue;
        }
        if (r->host != NULL) {
            rec_stop(r);
        } else if (r->launcher) {
            /* The launcher has no host thread of its own: it is the host
             * thread that called bw_os_reset, and there is no trampoline
             * to unwind it with. */
            rec_retire(r);
        }
    }
    bw_mutex_lock(g_mx);
    if (me == NULL || g_token != me->thread) {
        /* The token of a thread that is gone goes to the caller, or is
         * dropped: a wait for a token that will never come must not
         * hang. */
        g_token = (me != NULL) ? me->thread : NULL;
    }
    bw_os_wake_all();
    bw_mutex_unlock(g_mx);
}

/* The OS layer's restart entry point (sdk/os.h says what it is for). */
void bw_os_stop_threads(void)
{
    stop_threads(rec_caller());
}

/*
 * The OS layer back to power-on: the thread list, the launcher and the
 * idle thread are the ones of a fresh start, and every thread but the
 * caller's is stopped.
 *
 * The caller's record stays: the caller is the frame thread, the idle
 * thread's host thread, and the next run's idle thread is that same host
 * thread (so the frame loop stays where it is).  The retired records stay
 * as well: their threads are parked inside them.
 */
void bw_os_thread_reset(void)
{
    HostRec *me;
    int i;

    if (g_mx == NULL) {
        g_mx = bw_mutex_create();
        g_cv = bw_cond_create();
    }
    me = rec_caller();
    stop_threads(me);

    /* Forget the threads that are gone. */
    for (i = 0; i < g_nrecs; ) {
        HostRec *r = g_recs[i];

        if (r != me && !r->retired) {
            if (r == g_frame_rec) {
                g_frame_rec = NULL;
                g_frame_host = 0;
            }
            free(r);
            g_recs[i] = g_recs[g_nrecs - 1];
            g_nrecs--;
            continue;
        }
        i++;
    }

    /* The caller's record: its host thread is running, and the next run's
     * idle thread is that same host thread. */
    if (me != NULL) {
        me->kept = 1;
        me->kill = 0;
        me->finished = 0;
        me->redirect = 0;
        me->redirect_arg = NULL;
    }

    memset(&OSi_ThreadInfo, 0, sizeof(OSi_ThreadInfo));
    memset(&OSi_LauncherThread, 0, sizeof(OSi_LauncherThread));
    memset(&OSi_IdleThread, 0, sizeof(OSi_IdleThread));
    memset(g_idle_stack, 0, sizeof(g_idle_stack));
    g_reschedule_count = 0;
    g_thread_initialized = 0;
    g_thread_id_count = 0;
    g_system_switch_cb = NULL;
    g_cur = me;               /* the caller is the thread that runs code */

    bw_mutex_lock(g_mx);
    /* The token: the caller holds it and keeps it.  The caller runs the
     * port's own code, and the game cannot run until the caller hands the
     * token over at the next switch. */
    g_token = (me != NULL) ? me->thread : NULL;
    bw_os_wake_all();
    bw_mutex_unlock(g_mx);

    if (me != NULL) {
        OSi_ThreadInfo.current = me->thread;
    }
}

/* ------------------------------------------------------------------ */
/* Address spellings (sdk/aliases.c says why a host link needs them)  */
/* ------------------------------------------------------------------ */

void      func_02085264(void)                       { OS_InitThread(); }
int       func_020853B0(void)                       { return OS_IsThreadAvailable(); }
void      func_020853C0(OSThread *t, void (*f)(void *), void *a, void *s, u32 n, u32 p)
                                                    { OS_CreateThread(t, f, a, s, n, p); }
void      func_020854B0(void)                       { OS_ExitThread(); }
void      func_020855BC(OSThread *t)                { OS_DestroyThread(t); }
void      func_02085638(OSThread *t, void *a)       { OS_KillThread(t, a); }
void      func_0208565C(OSThread *t, void *a, u32 p){ OS_KillThreadWithPriority(t, a, p); }
void      func_02085710(OSThread *t)                { OS_JoinThread(t); }
int       func_0208574C(const OSThread *t)          { return OS_IsThreadTerminated(t); }
void      func_02085760(OSThread *t, OSThreadQueue *q) { OS_SleepThreadDirect(t, q); }
void      func_020857B0(OSThreadQueue *q)           { OS_SleepThread(q); }
void      func_02085800(OSThreadQueue *q)           { OS_WakeupThread(q); }
void      func_02085868(OSThread *t)                { OS_WakeupThreadDirect(t); }
OSThread *func_02085890(void)                       { return OS_SelectThread(); }
void      func_020858BC(void)                       { OS_RescheduleThread(); }
void      func_020858D8(void)                       { OS_YieldThread(); }
int       func_02085988(OSThread *t, u32 p)         { return OS_SetThreadPriority(t, p); }
u32       func_02085A30(const OSThread *t)          { return OS_GetThreadPriority(t); }
void      func_02085A38(u32 ms)                     { OS_Sleep(ms); }
void      sub_02085A38(u32 ms)                      { OS_Sleep(ms); }
OSSwitchThreadCallback func_02085AF8(OSSwitchThreadCallback cb)
                                                    { return OS_SetSwitchThreadCallback(cb); }
u32       func_02085B30(void)                       { return OS_DisableScheduler(); }
u32       func_02085B60(void)                       { return OS_EnableScheduler(); }
OSMutex  *func_020850CC(OSMutexQueue *q)            { return OSi_RemoveMutexLinkFromQueue(q); }
