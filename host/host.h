/*
 * host/host.h -- the native program that runs eidos's wasm32 build.
 *
 * The game, the port and the engine are one WebAssembly module (the wasm32
 * configuration, docs/lp64.md), translated to C by wasm2c under the module
 * name "game" (game.h, generated in the build tree).  This program is the
 * 64-bit side: it instantiates the module, and answers its imports:
 *
 *   wasi_snapshot_preview1   files, the clock, the environment, the
 *                            arguments (host/wasi.c)
 *   eidos                    fibers, sleeping (host/fiber.c) and the rest of
 *                            sdk/wasm_host.h
 *   env                      SDL3 and OpenGL (host/sdl.c, gl_imports.c)
 *
 * The module's memory is its own 32-bit address space; an address the
 * module hands over is an offset into it (host_ptr).
 */

#ifndef EIDOS_HOST_H
#define EIDOS_HOST_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "wasm-rt.h"

/* The import modules' instances: wasm2c passes them to every import. */
struct w2c_wasi__snapshot__preview1 {
    int argc;
    char **argv;
    jmp_buf exit_to;            /* proc_exit lands here                 */
    int exit_code;
};

struct w2c_eidos {
    int unused;
};

/* "env": what the module's own code calls by name and nobody in it defines:
 * SDL3 (host/sdl.c), OpenGL (gl_imports.c, tools/wasm_gl_imports.py) and a
 * few C library calls WASI does not cover (host/main.c). */
struct w2c_env {
    int unused;
};

#include "game.h"

extern w2c_game g_game;
extern struct w2c_wasi__snapshot__preview1 g_wasi;
extern struct w2c_eidos g_eidos;
extern struct w2c_env g_env;

/* A module address as a host pointer; NULL when [addr, addr+len) is not
 * inside the module's memory. */
static inline void *host_ptr(uint32_t addr, uint32_t len)
{
    wasm_rt_memory_t *m = w2c_game_memory(&g_game);

    if ((uint64_t)addr + len > m->size) {
        return NULL;
    }
    return m->data + addr;
}

/* host/par.c: the instance the calling thread runs (a worker's copy, or
 * g_game on the module's own thread). */
w2c_game *host_instance(void);

/* host/sdl.c: the OpenGL context is OpenGL ES (Android, iOS), which the
 * OpenGL imports translate for (tools/wasm_gl_imports.py). */
extern int host_gl_es;
/* its version as major * 10 + minor (30 for OpenGL ES 3.0) */
extern int host_gl_es_version;
/* The framebuffer that is the window's: 0, except on iOS, where SDL's view
 * has one of its own (host/sdl.c). */
extern unsigned host_gl_window_fbo;

/* The ROM dialog on phones (host/pick_mobile.c), as install/picker.h's
 * bw_pick_rom. */
int host_pick_rom(const char *title, char *out, size_t n);

#if defined(__APPLE__)
/* host/ios_view.m: the home indicator hidden, its swipe deferred (iOS). */
void host_ios_view_setup(void);
/* host/ios_view.m: whether the device is an iPad (1) or not (0). */
int host_ios_is_pad(void);
#endif

/* host/fiber.c, host/wasi.c */
void host_fibers_init(void);
#if defined(__ANDROID__)
size_t host_fiber_region_size(void);
void host_fiber_region(void *base);
#endif
void host_wasi_init(void);

/* host/main.c: the module's malloc and free under the heap's lock
 * (sdk/wasm_heap.c), for the host's own allocations in module memory. */
uint32_t host_malloc(uint32_t n);
void host_free(uint32_t addr);

/* host/sdl.c: SDL's objects as the small numbers the module holds (one
 * table for SDL and its GPU, host/sdl_gpu.c). */
uint32_t host_handle_of(void *p);
void *host_handle_ptr(uint32_t h);
void host_handle_drop(uint32_t h);

#endif /* EIDOS_HOST_H */
