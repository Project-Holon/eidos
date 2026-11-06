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
    "geom", "raster", "snap", "texdec", "gldraw", "glup", "render", "boot3d", "glread", "unprem"
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
        fprintf(f, "frame,interval_ms,work_ms,present_ms\n");
        for (i = 0; i < g_bench_n; i++) {
            fprintf(f, "%u,%.3f,%.3f,%.3f\n", i, g_bench[i].interval, g_bench[i].work,
                    g_bench[i].present);
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
