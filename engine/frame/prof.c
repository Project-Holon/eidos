/* engine/frame/prof.c -- see prof.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prof.h"
#include "plat_thread.h"

int eidos_prof_on;

static unsigned g_every;
static unsigned g_n;
static unsigned long long g_sum[EIDOS_PROF_COUNT];
static unsigned long long g_max[EIDOS_PROF_COUNT];
static unsigned long long g_win_start;

static const char *const k_names[EIDOS_PROF_COUNT] = {
    "step", "g3d", "main2d", "sub2d", "present", "audio", "wait",
    "geom", "raster", "snap", "texdec", "gldraw", "glup", "render", "boot3d", "glread", "unprem", "ui", "game"
};

void eidos_prof_enable(unsigned every)
{
    g_every = every != 0 ? every : 300;
    eidos_prof_on = 1;
}

unsigned long long eidos_prof_begin(void)
{
    return eidos_prof_on ? bw_clock_ns() : 0;
}

/* ------------------------------------------------------------------ */
/* The benchmark                                                       */
/* ------------------------------------------------------------------ */

#define BENCH_MAX_FRAMES (1u << 17)     /* about 36 minutes at 60 fps */
#define BENCH_MAX_NOTES 12

typedef struct BenchFrame {
    float interval;         /* ms since the frame before          */
    float work;             /* interval less the pacing wait      */
    float present;          /* upload and present                 */
    float phase[EIDOS_PROF_COUNT];  /* every phase, ms            */
} BenchFrame;

static char g_bench_path[512];
static unsigned g_bench_skip, g_bench_seen, g_bench_n;
static BenchFrame *g_bench;
static unsigned long long g_bench_last;
static unsigned long long g_frame_phase[EIDOS_PROF_COUNT];  /* this frame's */
static char g_note_key[BENCH_MAX_NOTES][32], g_note_val[BENCH_MAX_NOTES][160];
static unsigned g_notes;

void eidos_bench_start(const char *path, unsigned skip)
{
    snprintf(g_bench_path, sizeof g_bench_path, "%s", path);
    g_bench_skip = skip;
    g_bench = (BenchFrame *)calloc(BENCH_MAX_FRAMES, sizeof *g_bench);
    if (!eidos_prof_on) {
        eidos_prof_enable(0x7fffffffu);   /* the probes on, no printed lines */
    }
}

void eidos_bench_note(const char *key, const char *value)
{
    unsigned i;

    if (key == NULL || value == NULL) {
        return;
    }
    for (i = 0; i < g_notes && strcmp(g_note_key[i], key) != 0; i++) {
    }
    if (i == g_notes) {
        if (g_notes == BENCH_MAX_NOTES) {
            return;
        }
        g_notes++;
    }
    snprintf(g_note_key[i], sizeof g_note_key[i], "%s", key);
    snprintf(g_note_val[i], sizeof g_note_val[i], "%s", value);
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;

    return x < y ? -1 : x > y ? 1 : 0;
}

/* The p-th percentile of n sorted values. */
static float pct(const float *v, unsigned n, double p)
{
    unsigned i = (unsigned)(p / 100.0 * (double)(n - 1) + 0.5);

    return n == 0 ? 0.f : v[i < n ? i : n - 1];
}

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') {
            fputc('\\', f);
        }
        fputc((unsigned char)*s >= 0x20 ? *s : ' ', f);
    }
    fputc('"', f);
}

/* `over` names the first count: the frames over `limit` ms (the budget, for
 * the work; late, for the time between frames, whose target is the DS's
 * 16.715 ms); the second is the frames over 33.4 ms (a frame dropped). */
static void json_stats(FILE *f, const char *name, float *v, unsigned n, const char *over,
                       float limit, int last)
{
    double sum = 0;
    unsigned i, over1 = 0, over33 = 0;

    for (i = 0; i < n; i++) {
        sum += v[i];
        over1 += v[i] > limit;
        over33 += v[i] > 33.4f;
    }
    qsort(v, n, sizeof *v, cmp_float);
    fprintf(f, "  \"%s\": {\"mean\": %.3f, \"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f, "
               "\"max\": %.3f, \"%s\": %u, \"over_33_3\": %u}%s\n",
            name, n != 0 ? sum / n : 0.0, pct(v, n, 50), pct(v, n, 95), pct(v, n, 99),
            n != 0 ? v[n - 1] : 0.f, over, over1, over33, last ? "" : ",");
}

void eidos_bench_finish(void)
{
    FILE *f;
    float *v;
    unsigned i;
    char csv[600];

    if (g_bench == NULL || g_bench_path[0] == '\0') {
        return;
    }
    snprintf(csv, sizeof csv, "%s.csv", g_bench_path);
    if ((f = fopen(csv, "w")) != NULL) {
        int k;

        fprintf(f, "frame,interval_ms,work_ms,present_ms");
        for (k = 0; k < EIDOS_PROF_COUNT; k++) {
            fprintf(f, ",%s", k_names[k]);
        }
        fprintf(f, "\n");
        for (i = 0; i < g_bench_n; i++) {
            fprintf(f, "%u,%.3f,%.3f,%.3f", i, g_bench[i].interval, g_bench[i].work, g_bench[i].present);
            for (k = 0; k < EIDOS_PROF_COUNT; k++) {
                fprintf(f, ",%.3f", g_bench[i].phase[k]);
            }
            fprintf(f, "\n");
        }
        fclose(f);
    }
    v = (float *)malloc((g_bench_n != 0 ? g_bench_n : 1) * sizeof *v);
    if (v != NULL && (f = fopen(g_bench_path, "w")) != NULL) {
        fprintf(f, "{\n  \"frames\": %u,\n  \"skipped\": %u,\n", g_bench_n, g_bench_skip);
        for (i = 0; i < g_notes; i++) {
            fprintf(f, "  ");
            json_string(f, g_note_key[i]);
            fprintf(f, ": ");
            json_string(f, g_note_val[i]);
            fprintf(f, ",\n");
        }
        for (i = 0; i < g_bench_n; i++) v[i] = g_bench[i].interval;
        json_stats(f, "interval_ms", v, g_bench_n, "over_20", 20.0f, 0);
        for (i = 0; i < g_bench_n; i++) v[i] = g_bench[i].work;
        json_stats(f, "work_ms", v, g_bench_n, "over_16_7", 16.7f, 0);
        for (i = 0; i < g_bench_n; i++) v[i] = g_bench[i].present;
        json_stats(f, "present_ms", v, g_bench_n, "over_16_7", 16.7f, 1);
        fprintf(f, "}\n");
        fclose(f);
        printf("eidos: benchmark: %u frames written to %s\n", g_bench_n, g_bench_path);
        fflush(stdout);
    }
    free(v);
    free(g_bench);
    g_bench = NULL;
}

/* The frame's own record, at its end. */
static void bench_frame(void)
{
    unsigned long long now = bw_clock_ns();

    if (g_bench != NULL && g_bench_last != 0 && g_bench_seen++ >= g_bench_skip &&
        g_bench_n < BENCH_MAX_FRAMES) {
        BenchFrame *b = &g_bench[g_bench_n++];
        double interval = (double)(now - g_bench_last) / 1e6;
        double wait = (double)g_frame_phase[EIDOS_PROF_WAIT] / 1e6;

        b->interval = (float)interval;
        b->work = (float)(interval > wait ? interval - wait : 0.0);
        b->present = (float)((double)g_frame_phase[EIDOS_PROF_PRESENT] / 1e6);
        {
            int k;

            for (k = 0; k < EIDOS_PROF_COUNT; k++) {
                b->phase[k] = (float)((double)g_frame_phase[k] / 1e6);
            }
        }
    }
    g_bench_last = now;
    memset(g_frame_phase, 0, sizeof g_frame_phase);
}

void eidos_prof_end(int phase, unsigned long long t0)
{
    unsigned long long d;

    if (!eidos_prof_on) {
        return;
    }
    d = bw_clock_ns() - t0;
    g_sum[phase] += d;
    g_frame_phase[phase] += d;
    if (d > g_max[phase]) {
        g_max[phase] = d;
    }
}

void eidos_prof_frame(unsigned long long frame_no)
{
    int i;
    unsigned long long now;

    if (!eidos_prof_on) {
        return;
    }
    bench_frame();
    if (g_n == 0) {
        g_win_start = bw_clock_ns();
    }
    if (++g_n < g_every) {
        return;
    }
    now = bw_clock_ns();
    printf("prof frame %llu (%u frames, %.2f ms/frame wall):", frame_no, g_n,
           (double)(now - g_win_start) / 1e6 / (double)g_n);
    for (i = 0; i < EIDOS_PROF_COUNT; i++) {
        printf(" %s %.2f/%.2f", k_names[i],
               (double)g_sum[i] / 1e6 / (double)g_n, (double)g_max[i] / 1e6);
        g_sum[i] = 0;
        g_max[i] = 0;
    }
    printf(" (avg/max ms)\n");
    fflush(stdout);
    g_n = 0;
}

/* ------------------------------------------------------------------ */
/* The slow-frame sampler                                              */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) && defined(__i386__)
#include <windows.h>
#include <tlhelp32.h>

#define HITCH_MAX 65536
#define HITCH_CALLERS 6

typedef struct HitchSample { DWORD tid, eip, caller[HITCH_CALLERS]; } HitchSample;

/* Not a static array: the 32-bit program must end, with its first stack
 * after it, below the DS main memory at 0x02000000 (sdk/host_vm.c); two more
 * megabytes of .bss pushed the stack over it and the game came up without
 * its identity mapping. */
static HitchSample *g_hitch;
static volatile LONG g_hitch_n;
static volatile LONG g_hitch_printing;  /* a list is being printed: no thread is stopped meanwhile */
static double g_hitch_ms = -1.0;
static DWORD g_exe_lo, g_exe_hi;

static DWORD WINAPI hitch_thread(void *arg)
{
    HANDLE th[64];
    DWORD tid[64], me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    int n = 0, round = 0, i;

    (void)arg;
    for (;;) {
        /* a timer tick between two looks (about 15 ms: one look a frame).
         * A look every millisecond hung the game within seconds (a thread
         * stopped at a bad moment); a steady cost is read over many frames
         * instead (EIDOS_SAMPLE_EVERY). */
        Sleep(1);
        if ((round++ & 15) == 0) {
            /* the process's threads, looked up again every quarter second */
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;

            for (i = 0; i < n; i++) {
                CloseHandle(th[i]);
            }
            n = 0;
            te.dwSize = sizeof te;
            if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid && te.th32ThreadID != me && n < 64) {
                        th[n] = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                           te.th32ThreadID);
                        if (th[n] != NULL) {
                            tid[n++] = te.th32ThreadID;
                        }
                    }
                } while (Thread32Next(snap, &te));
            }
            if (snap != INVALID_HANDLE_VALUE) {
                CloseHandle(snap);
            }
        }
        for (i = 0; i < n; i++) {
            CONTEXT ctx;
            HitchSample s;
            DWORD stack[96];
            SIZE_T got = 0;
            int k, c = 0;

            if (g_hitch_printing || SuspendThread(th[i]) == (DWORD)-1) {
                continue;
            }
            memset(&s, 0, sizeof s);
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(th[i], &ctx)) {
                MEMORY_BASIC_INFORMATION mbi;

                s.tid = tid[i];
                s.eip = ctx.Eip;
                /* The stack is read in place.  Nothing between the stop and
                 * the restart may take a lock the stopped thread can hold
                 * (the heap's, the C library's): ReadProcessMemory did, and
                 * the game hung when a list was being printed. */
                if (VirtualQuery((void *)(ULONG_PTR)ctx.Esp, &mbi, sizeof mbi) != 0 && mbi.State == MEM_COMMIT
                    && (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0) {
                    ULONG_PTR end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

                    got = end - (ULONG_PTR)ctx.Esp;
                    if (got > sizeof stack) {
                        got = sizeof stack;
                    }
                    got &= ~(SIZE_T)3;
                    memcpy(stack, (const void *)(ULONG_PTR)ctx.Esp, got);
                    for (k = 0; k < (int)(got / 4) && c < HITCH_CALLERS; k++) {
                        if (stack[k] >= g_exe_lo && stack[k] < g_exe_hi) {
                            s.caller[c++] = stack[k];
                        }
                    }
                }
            }
            ResumeThread(th[i]);
            /* a thread that waits in the system with nothing of the program
             * near the top of its stack says nothing */
            if (s.tid != 0 && ((s.eip >= g_exe_lo && s.eip < g_exe_hi) || c > 0)) {
                LONG at = InterlockedIncrement(&g_hitch_n) - 1;

                if (at < HITCH_MAX) {
                    g_hitch[at] = s;
                }
            }
        }
    }
    return 0;
}

void eidos_hitch_frame(double game_ms, unsigned long long frame_no)
{
    LONG n, i;

    if (g_hitch_ms < 0.0) {
        const char *e = getenv("EIDOS_HITCH_MS");

        g_hitch_ms = e != NULL && atof(e) > 0.0 ? atof(e) : 0.0;
        if (g_hitch_ms > 0.0) {
            HMODULE exe = GetModuleHandle(NULL);
            const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)exe;
            const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)exe + dos->e_lfanew);

            g_exe_lo = (DWORD)(ULONG_PTR)exe;
            g_exe_hi = g_exe_lo + nt->OptionalHeader.SizeOfImage;
            if (!eidos_prof_on) {
                eidos_prof_enable(0x7fffffffu);
            }
            g_hitch = (HitchSample *)VirtualAlloc(NULL, sizeof(HitchSample) * HITCH_MAX, MEM_RESERVE | MEM_COMMIT,
                                                  PAGE_READWRITE);
            if (g_hitch == NULL) {
                g_hitch_ms = 0.0;
                return;
            }
            CloseHandle(CreateThread(NULL, 0, hitch_thread, NULL, 0, NULL));
        }
    }
    if (g_hitch_ms <= 0.0) {
        return;
    }
    {
        /* EIDOS_SAMPLE_EVERY=N: every N frames' samples, whatever they cost,
         * from frame EIDOS_SAMPLE_FROM on (the lists before it are dropped:
         * printing slows a frame, and the start-up is not what is measured) */
        static int every = -1;
        static unsigned long long last, from;

        if (every < 0) {
            const char *e = getenv("EIDOS_SAMPLE_EVERY"), *f = getenv("EIDOS_SAMPLE_FROM");

            every = e != NULL ? atoi(e) : 0;
            from = f != NULL ? strtoull(f, NULL, 10) : 0;
        }
        if (every > 0) {
            if (frame_no - last < (unsigned long long)every) {
                return;                 /* the list goes on */
            }
            last = frame_no;
            if (frame_no < from) {
                (void)InterlockedExchange(&g_hitch_n, 0);
                return;
            }
            game_ms = g_hitch_ms;       /* printed below */
        }
    }
    n = InterlockedExchange(&g_hitch_n, 0);
    n = n > HITCH_MAX ? HITCH_MAX : n;
    if (game_ms < g_hitch_ms) {
        return;
    }
    g_hitch_printing = 1;
    fprintf(stderr, "eidos: hitch frame %llu game %.0f ms samples %ld\n", frame_no, game_ms, (long)n);
    for (i = 0; i < n; i++) {
        const HitchSample *s = &g_hitch[i];
        int k;

        fprintf(stderr, "eidos: hs %lu %08lX", (unsigned long)s->tid, (unsigned long)s->eip);
        for (k = 0; k < HITCH_CALLERS && s->caller[k] != 0; k++) {
            fprintf(stderr, " %08lX", (unsigned long)s->caller[k]);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
    g_hitch_printing = 0;
}
#else
void eidos_hitch_frame(double game_ms, unsigned long long frame_no)
{
    (void)game_ms;
    (void)frame_no;
}
#endif
