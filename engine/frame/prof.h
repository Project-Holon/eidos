/*
 * engine/frame/prof.h -- a cheap per-phase frame profiler.
 *
 * Off by default (one predictable branch per probe).  With it on, every
 * `every` frames it prints the average and maximum milliseconds each phase
 * took in that window.  Single-threaded: the frame thread owns it.
 */
#ifndef EIDOS_FRAME_PROF_H
#define EIDOS_FRAME_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EIDOS_PROF_STEP,      /* the game: input, step, vblank              */
    EIDOS_PROF_G3D,       /* the 3D frame                               */
    EIDOS_PROF_MAIN,      /* the main engine's 2D compose               */
    EIDOS_PROF_SUB,       /* the sub engine's 2D compose                */
    EIDOS_PROF_PRESENT,   /* upload and present                         */
    EIDOS_PROF_AUDIO,     /* sound render and queue                     */
    EIDOS_PROF_WAIT,      /* the pacing sleep                           */
    EIDOS_PROF_GEOM,      /* 3D geometry commands (inside step)         */
    EIDOS_PROF_RASTER,    /* software 3D rasteriser (inside step)       */
    EIDOS_PROF_SNAP,      /* GPU snapshot copy (inside step)            */
    EIDOS_PROF_TEXDEC,    /* texture decode (inside snap or raster)     */
    EIDOS_PROF_GLDRAW,    /* GL 3D draw calls (CPU side)                */
    EIDOS_PROF_GLUP,      /* GL texture uploads                         */
    EIDOS_PROF_RENDER,    /* the whole render hook                      */
    EIDOS_PROF_BOOT3D,    /* the GPU 3D layer set-up                    */
    EIDOS_PROF_GLREAD,    /* glReadPixels (waits for the GPU)           */
    EIDOS_PROF_UNPREM,    /* un-premultiply and flip after the read     */
    EIDOS_PROF_COUNT
};

extern int eidos_prof_on;

void eidos_prof_enable(unsigned every);
unsigned long long eidos_prof_begin(void);
void eidos_prof_end(int phase, unsigned long long t0);
/* End of a frame: counts it and prints a line when the window is full.   */
void eidos_prof_frame(unsigned long long frame_no);

/*
 * The benchmark (--bench FILE): every frame after the first `skip` is
 * recorded (the time since the frame before, the work in it: that time less
 * the pacing wait, and the present), and at the end of the run FILE gets a
 * JSON summary (percentiles, the worst frame, the frames over 16.7 and
 * 33.3 ms, the notes) and FILE.csv the frames.  It turns the profiler on.
 * A note is a line of the summary: the platform layer names the system, the
 * backend and the GPU, so runs on different devices compare side by side.
 */
void eidos_bench_start(const char *path, unsigned skip);
void eidos_bench_note(const char *key, const char *value);
/* Writes the files; the run's end calls it (more calls do nothing). */
void eidos_bench_finish(void);

#ifdef __cplusplus
}
#endif

#endif
