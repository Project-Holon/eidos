/*
 * sdk/plat_thread.h -- the host threading primitives the OS shims run on.
 *
 * sdk/os_thread.c runs every game OSThread on a host thread and lets exactly
 * one of them execute game code at a time (docs/boot.md, "Threads,
 * interrupts and the frame").  That needs three primitives and nothing else:
 * a mutex, a condition variable and a way to start and join a thread.  They
 * are the only platform-specific code in the OS layer, and sdk/plat_thread.c
 * holds the two implementations (Win32 and POSIX) behind this interface.
 *
 * Every object is opaque and heap-allocated, so no platform header leaks
 * into the files that include this one.  Creation failures panic: the OS
 * layer cannot run a game thread without them and has nothing to fall back
 * to.
 */

#ifndef BW_PORT_PLAT_THREAD_H
#define BW_PORT_PLAT_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BwMutex  BwMutex;
typedef struct BwCond   BwCond;
typedef struct BwThread BwThread;

BwMutex *bw_mutex_create(void);
void     bw_mutex_destroy(BwMutex *m);
void     bw_mutex_lock(BwMutex *m);
void     bw_mutex_unlock(BwMutex *m);

BwCond  *bw_cond_create(void);
void     bw_cond_destroy(BwCond *c);
/* Atomically release `m`, wait, and re-acquire it.  May wake spuriously.  */
void     bw_cond_wait(BwCond *c, BwMutex *m);
void     bw_cond_broadcast(BwCond *c);

/* Start `fn(arg)` on a new host thread.                                   */
BwThread *bw_thread_start(void (*fn)(void *arg), void *arg);
/* Wait for the thread to return, then release the handle.                 */
void      bw_thread_join(BwThread *t);
/* The calling host thread's identity (never 0): equal for the same thread. */
unsigned long long bw_thread_self_id(void);

#if defined(__wasm__)
/* The wasm32 build's threads take turns on one host thread: a thread that
 * waits for time sleeps here, and the others run meanwhile.              */
void      bw_thread_sleep_ns(unsigned long long ns);
/* End the calling thread here (its joiner wakes); never returns.          */
void      bw_thread_exit(void) __attribute__((noreturn));
#endif

/* The number of hardware threads (at least 1). */
unsigned bw_cpu_count(void);

/* A monotonic clock in nanoseconds (the frame profiler's). */
unsigned long long bw_clock_ns(void);

/* localtime(&t) in the player's time zone, into *out; 0 on success.  The
 * wasm32 build's C library knows no time zone (UTC), so its offset comes
 * from the host program (sdk/wasm_host.h, tz_offset). */
struct tm;
int bw_localtime(long long t, struct tm *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PLAT_THREAD_H */
