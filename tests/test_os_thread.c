/*
 * tests/test_os_thread.c -- OS threads on host threads, synthetically.
 *
 * No game data.  Each case boots the OS layer the way NitroMain's system
 * initialiser does (tests/os_test.h) on the test's main thread, which
 * becomes OSi_LauncherThread, and checks the NitroSDK scheduling rules:
 *
 *   - the highest-priority READY thread runs, and a lower one waits until
 *     everything above it sleeps or exits,
 *   - sleep on a queue and wakeup of the whole queue, wakeup direct,
 *   - only one game thread runs at a time (the run token), checked by two
 *     yielding threads that would overlap on a preemptive host,
 *   - OS_YieldThread round-robins equal priorities,
 *   - destroy and kill (the destructor runs on the killed thread),
 *   - OS_Sleep wakes after the right number of frames of the tick clock,
 *   - the switch callback sees every switch.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "os_test.h"
#include "port_test.h"

#define STACK_WORDS 256

static char g_log[128];
static int  g_nlog;

static void logc(char c)
{
    if (g_nlog + 1 < (int)sizeof(g_log)) {
        g_log[g_nlog++] = c;
        g_log[g_nlog] = '\0';
    }
}

static void log_reset(void)
{
    g_nlog = 0;
    g_log[0] = '\0';
}

#define CHECK_LOG(label, want)                                             \
    do {                                                                   \
        bw_checks++;                                                       \
        if (strcmp(g_log, (want)) != 0) {                                  \
            bw_failures++;                                                 \
            printf("  FAIL %-40s got \"%s\", want \"%s\"\n", (label), g_log, (want)); \
        }                                                                  \
    } while (0)

/* ---- priority order ------------------------------------------------ */

static void letter_proc(void *arg)
{
    logc(*(const char *)arg);
}

static int test_priority(void)
{
    static OSThread a, b, c;
    static u32 sa[STACK_WORDS], sb[STACK_WORDS], sc[STACK_WORDS];
    static const char la = 'A', lb = 'B', lc = 'C';

    os_test_boot();
    log_reset();

    OS_CreateThread(&a, letter_proc, (void *)&la, sa + STACK_WORDS, sizeof(sa), 10);
    OS_CreateThread(&b, letter_proc, (void *)&lb, sb + STACK_WORDS, sizeof(sb), 5);
    OS_CreateThread(&c, letter_proc, (void *)&lc, sc + STACK_WORDS, sizeof(sc), 20);
    BW_CHECK_EQ("created threads wait", g_nlog, 0);

    (void)OS_DisableScheduler();
    OS_WakeupThreadDirect(&a);
    OS_WakeupThreadDirect(&b);
    OS_WakeupThreadDirect(&c);
    BW_CHECK_EQ("scheduler disabled: nothing ran", g_nlog, 0);
    (void)OS_EnableScheduler();
    OS_RescheduleThread();
    /* B (5) and A (10) outrank the launcher (16); C (20) does not. */
    CHECK_LOG("above the launcher, in priority order", "BA");
    BW_CHECK_EQ("A terminated", OS_IsThreadTerminated(&a), 1);
    BW_CHECK_EQ("C still waiting to run", OS_IsThreadTerminated(&c), 0);

    OS_JoinThread(&c);
    CHECK_LOG("below the launcher, once it sleeps", "BAC");
    BW_CHECK_EQ("current is the launcher again",
                OSi_ThreadInfo.current == &OSi_LauncherThread, 1);

    /* A raised priority takes the CPU at once. */
    log_reset();
    OS_CreateThread(&c, letter_proc, (void *)&lc, sc + STACK_WORDS, sizeof(sc), 20);
    OS_WakeupThreadDirect(&c);
    CHECK_LOG("lower thread waits", "");
    (void)OS_SetThreadPriority(&c, 3);
    CHECK_LOG("raised above the launcher, runs", "C");
    return 0;
}

/* ---- sleep and wakeup ---------------------------------------------- */

static OSThreadQueue g_q;
static int g_wakes;
static int g_stop;

static void sleeper_proc(void *arg)
{
    (void)arg;
    while (!g_stop) {
        OS_SleepThread(&g_q);
        g_wakes++;
    }
}

static int test_sleep_wakeup(void)
{
    static OSThread t1, t2;
    static u32 s1[STACK_WORDS], s2[STACK_WORDS];

    os_test_boot();
    memset(&g_q, 0, sizeof(g_q));
    g_wakes = 0;
    g_stop = 0;

    OS_CreateThread(&t1, sleeper_proc, NULL, s1 + STACK_WORDS, sizeof(s1), 8);
    OS_CreateThread(&t2, sleeper_proc, NULL, s2 + STACK_WORDS, sizeof(s2), 9);
    OS_WakeupThreadDirect(&t1);   /* runs to its first sleep */
    OS_WakeupThreadDirect(&t2);
    BW_CHECK_EQ("both asleep on the queue", g_q.head == &t1 && g_q.tail == &t2, 1);
    BW_CHECK_EQ("no wakes yet", g_wakes, 0);

    OS_WakeupThread(&g_q);
    BW_CHECK_EQ("one wakeup wakes the queue", g_wakes, 2);
    BW_CHECK_EQ("and both sleep again", g_q.head == &t1 && g_q.tail == &t2, 1);

    OS_WakeupThread(&g_q);
    OS_WakeupThread(&g_q);
    BW_CHECK_EQ("three wakeups", g_wakes, 6);

    g_stop = 1;
    OS_WakeupThread(&g_q);
    BW_CHECK_EQ("stopped threads terminated",
                OS_IsThreadTerminated(&t1) && OS_IsThreadTerminated(&t2), 1);

    /* Destroying a sleeping thread takes it off its queue. */
    g_stop = 0;
    OS_CreateThread(&t1, sleeper_proc, NULL, s1 + STACK_WORDS, sizeof(s1), 8);
    OS_WakeupThreadDirect(&t1);
    BW_CHECK_EQ("recreated thread sleeps", g_q.head == &t1, 1);
    OS_DestroyThread(&t1);
    BW_CHECK_EQ("destroyed: off the queue", g_q.head == NULL, 1);
    BW_CHECK_EQ("destroyed: terminated", OS_IsThreadTerminated(&t1), 1);
    OS_JoinThread(&t1);           /* returns at once */
    return 0;
}

/* ---- the run token ------------------------------------------------- */

static volatile int g_inside;
static int g_overlaps;

static void yielder_proc(void *arg)
{
    int i;
    for (i = 0; i < 50; i++) {
        volatile int spin;
        if (g_inside) {
            g_overlaps++;
        }
        g_inside = 1;
        for (spin = 0; spin < 2000; spin++) {
        }
        logc(*(const char *)arg);
        g_inside = 0;
        OS_YieldThread();
    }
}

static int test_token(void)
{
    static OSThread x, y;
    static u32 sx[STACK_WORDS], sy[STACK_WORDS];
    static const char lx = 'x', ly = 'y';
    int i;
    int alternates = 1;

    os_test_boot();
    log_reset();
    g_overlaps = 0;
    g_inside = 0;

    (void)OS_DisableScheduler();
    OS_CreateThread(&x, yielder_proc, (void *)&lx, sx + STACK_WORDS, sizeof(sx), 12);
    OS_CreateThread(&y, yielder_proc, (void *)&ly, sy + STACK_WORDS, sizeof(sy), 12);
    OS_WakeupThreadDirect(&x);
    OS_WakeupThreadDirect(&y);
    (void)OS_EnableScheduler();
    OS_JoinThread(&x);
    OS_JoinThread(&y);

    BW_CHECK_EQ("no two game threads ran at once", g_overlaps, 0);
    BW_CHECK_EQ("100 turns", g_nlog, 100);
    for (i = 1; i < g_nlog; i++) {
        if (g_log[i] == g_log[i - 1]) {
            alternates = 0;
        }
    }
    BW_CHECK_EQ("yield round-robins equal priority", alternates, 1);
    /* OSi_InsertThreadToList puts a thread before others of its priority,
     * so the one created last runs first. */
    BW_CHECK_EQ("newest of equal priority first", g_log[0], 'y');
    return 0;
}

/* Stack arrays must survive another worker using the shared wasm instance.
 * Scalar loop counters alone can stay in wasm locals and miss a bad swap. */
static int g_stack_failures;
static void stack_proc(void *arg)
{
    volatile u32 words[1024];
    u32 seed = *(u32 *)arg;
    unsigned i, turn;
    for (i = 0; i < 1024; ++i) words[i] = seed ^ i;
    for (turn = 0; turn < 32; ++turn) {
        OS_YieldThread();
        for (i = 0; i < 1024; ++i) if (words[i] != (seed ^ i)) g_stack_failures++;
    }
}

static int test_stack_handoff(void)
{
    static OSThread a, b;
    static u32 sa[STACK_WORDS], sb[STACK_WORDS];
    static u32 seeds[] = { 0x13579BDFu, 0x2468ACE0u };
    unsigned round;
    os_test_boot();
    g_stack_failures = 0;
    /* Repeated exit/recreate also exercises shadow-stack reclamation and
     * restoration of wasm2c's call-depth counter after native unwinding. */
    for (round = 0; round < 16; ++round) {
        (void)OS_DisableScheduler();
        OS_CreateThread(&a, stack_proc, &seeds[0], sa + STACK_WORDS, sizeof(sa), 12);
        OS_CreateThread(&b, stack_proc, &seeds[1], sb + STACK_WORDS, sizeof(sb), 12);
        OS_WakeupThreadDirect(&a);
        OS_WakeupThreadDirect(&b);
        (void)OS_EnableScheduler();
        OS_JoinThread(&a);
        OS_JoinThread(&b);
    }
    BW_CHECK_EQ("stack arrays survive 1024 handoffs", g_stack_failures, 0);
    return 0;
}

/* ---- kill: the destructor runs on the killed thread ---------------- */

static OSThread *g_dtor_ran_on;
static void *g_dtor_arg;

static void dtor(void *arg)
{
    g_dtor_ran_on = OSi_ThreadInfo.current;
    g_dtor_arg = arg;
}

static void forever_proc(void *arg)
{
    (void)arg;
    for (;;) {
        OS_SleepThread(NULL);
    }
}

static int test_kill(void)
{
    static OSThread k;
    static u32 sk[STACK_WORDS];
    static int token;

    os_test_boot();
    g_dtor_ran_on = NULL;
    g_dtor_arg = NULL;

    OS_CreateThread(&k, forever_proc, NULL, sk + STACK_WORDS, sizeof(sk), 6);
    k.destructor = dtor;
    OS_WakeupThreadDirect(&k);
    BW_CHECK_EQ("victim asleep", k.state, OS_THREAD_STATE_WAITING);
    OS_KillThread(&k, &token);
    BW_CHECK_EQ("destructor ran", g_dtor_ran_on == &k, 1);
    BW_CHECK_EQ("with the kill argument", g_dtor_arg == (void *)&token, 1);
    BW_CHECK_EQ("victim terminated", OS_IsThreadTerminated(&k), 1);
    return 0;
}

/* ---- OS_Sleep on the tick clock ------------------------------------ */

static int g_frames;
static u64 g_woke_frame;

static void frame_hook(void *user)
{
    (void)user;
    eidos_frame_run_one(&g_os_test_loop);
    g_frames++;
}

static void napper_proc(void *arg)
{
    (void)arg;
    OS_Sleep(100);
    g_woke_frame = eidos_frame_count(&g_os_test_loop);
}

static int g_switches;
static void count_switch(OSThread *from, OSThread *to)
{
    (void)from;
    (void)to;
    g_switches++;
}

static int test_os_sleep(void)
{
    static OSThread n;
    static u32 sn[STACK_WORDS];

    os_test_boot();
    g_frames = 0;
    g_woke_frame = 0;
    g_switches = 0;
    bw_os_set_halt_hook(frame_hook, NULL);
    (void)OS_SetSwitchThreadCallback(count_switch);

    OS_CreateThread(&n, napper_proc, NULL, sn + STACK_WORDS, sizeof(sn), 4);
    OS_WakeupThreadDirect(&n);    /* sleeps 100 ms: 52365 ticks */
    OS_JoinThread(&n);            /* launcher sleeps, idle halts, frames pass */

    /* 52365 ticks of 8752.97 a frame: the alarm is due during frame 6. */
    BW_CHECK_EQ("OS_Sleep(100) woke at frame 6", (long)g_woke_frame, 6);
    BW_CHECK_EQ("six frames of halt", g_frames, 6);
    BW_REQUIRE("switch callback saw the switches", g_switches >= 4);
    (void)OS_SetSwitchThreadCallback(NULL);
    bw_os_set_halt_hook(NULL, NULL);
    return 0;
}

#ifndef _WIN32
/* Optional synthetic save I/O in a caller-supplied scratch directory. The
 * same guest executable exercises the host's WASI files, not a mock VFS. */
static int test_host_files(const char *directory)
{
    char path[1024], renamed[1024], child[1024];
    const char bytes[] = "eidos synthetic save\n";
    char restored[sizeof(bytes)];
    FILE *f;
    struct stat st;
    snprintf(child, sizeof(child), "%s/files", directory);
    snprintf(path, sizeof(path), "%s/files/save.tmp", directory);
    snprintf(renamed, sizeof(renamed), "%s/files/save.dat", directory);
    BW_CHECK_EQ("host mkdir", mkdir(child, 0700), 0);
    f = fopen(path, "wb+");
    BW_REQUIRE("host opens a save", f != NULL);
    BW_CHECK_EQ("host writes save bytes", fwrite(bytes, 1, sizeof(bytes), f), sizeof(bytes));
    BW_CHECK_EQ("host flush", fflush(f), 0);
    BW_CHECK_EQ("host sync", fsync(fileno(f)), 0);
    BW_CHECK_EQ("host tell", ftell(f), sizeof(bytes));
    BW_CHECK_EQ("host seek", fseek(f, 0, SEEK_SET), 0);
    BW_CHECK_EQ("host reads save bytes", fread(restored, 1, sizeof(restored), f), sizeof(restored));
    BW_CHECK_EQ("host bytes match", memcmp(bytes, restored, sizeof(bytes)), 0);
    BW_CHECK_EQ("host close", fclose(f), 0);
    BW_CHECK_EQ("host rename", rename(path, renamed), 0);
    BW_CHECK_EQ("host stat", stat(renamed, &st), 0);
    BW_CHECK_EQ("host file size", st.st_size, sizeof(bytes));
    f = fopen(renamed, "rb");
    BW_REQUIRE("host reopens renamed save", f != NULL);
    memset(restored, 0, sizeof(restored));
    BW_CHECK_EQ("host reload", fread(restored, 1, sizeof(restored), f), sizeof(restored));
    BW_CHECK_EQ("host reload matches", memcmp(bytes, restored, sizeof(bytes)), 0);
    BW_CHECK_EQ("host closes reload", fclose(f), 0);
    BW_CHECK_EQ("host removes save", remove(renamed), 0);
    BW_CHECK_EQ("host removes directory", rmdir(child), 0);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    int fail = 0;

    fail |= test_priority();
    fail |= test_sleep_wakeup();
    fail |= test_token();
    fail |= test_stack_handoff();
    fail |= test_kill();
    fail |= test_os_sleep();

    /* A reset stops every game thread's host thread. */
    bw_os_reset();
    BW_CHECK_EQ("reset leaves no host threads", bw_os_host_threads(), 0);
#ifndef _WIN32
    if (argc > 1) fail |= test_host_files(argv[1]);
#else
    (void)argc;
    (void)argv;
#endif

    (void)fail;
    BW_REPORT("test_os_thread");
}
