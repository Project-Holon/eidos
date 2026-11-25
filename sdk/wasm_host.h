/*
 * sdk/wasm_host.h -- what the wasm32 build of eidos asks of the program
 * that runs it (docs/lp64.md, "The wasm32 configuration's architecture").
 *
 * In the wasm32 configuration the game, the port and the engine are one
 * WebAssembly module, translated to C by wasm2c and run inside a native
 * 64-bit program (host/).  Everything the module cannot do by itself is a
 * function the host provides, declared here: the module's imports from
 * the "eidos" module.  The C library's own (files, the clock, the
 * environment) are WASI's, which the host provides too.
 *
 * Fibers.  The module runs on one host thread.  Each of its threads
 * (sdk/plat_thread.c, the wasm32 part) is a fiber of the host: its own
 * native stack for the translated code, and its own part of linear memory
 * for the module's stack.  A switch is a call that returns when the fiber
 * that made it is switched to again; the host keeps each fiber's module
 * stack pointer (__stack_pointer) and puts the next one's in place.  A new
 * fiber starts in the module's export bw_fiber_main(id).
 *
 * Only for the wasm32 build: nothing includes this file elsewhere.
 */

#ifndef BW_PORT_WASM_HOST_H
#define BW_PORT_WASM_HOST_H

#if defined(__wasm__)

#define BW_WASM_IMPORT(name) __attribute__((import_module("eidos"), import_name(#name)))

/* A new fiber `id`, its module stack starting at `stack_top` (the address
 * just past its stack; it grows down).  It runs nothing until switched to. */
BW_WASM_IMPORT(fiber_create) void bw_host_fiber_create(int id, unsigned stack_top);
/* Run fiber `id`; returns when this fiber is switched to again. */
BW_WASM_IMPORT(fiber_switch) void bw_host_fiber_switch(int id);
/* Run fiber `id` and never come back: this fiber is over, the host frees
 * it once it has left it. */
BW_WASM_IMPORT(fiber_exit) void bw_host_fiber_exit(int id);
/* Sleep the whole program (every fiber is blocked, or one asked to wait). */
BW_WASM_IMPORT(sleep_ns) void bw_host_sleep_ns(unsigned long long ns);
/* The host's processor count, for what parallel work it can do itself. */
BW_WASM_IMPORT(cpu_count) unsigned bw_host_cpu_count(void);
/* The platform's "open file" dialog for the ROM (install/picker.h): the
 * same answer as bw_pick_rom, `out` holding the path or the reason. */
BW_WASM_IMPORT(pick_rom) int bw_host_pick_rom(const char *title, char *out, unsigned n);
/* The platform's folders, by the host's own rules (install/location.c,
 * sdk/plat_save.c compiled for the platform): 0 when `out` holds one. */
#define BW_HOST_PATH_APP_HOME 0         /* bw_os_app_home              */
#define BW_HOST_PATH_EXE_DIR  1         /* bw_os_exe_dir               */
#define BW_HOST_PATH_SAVE_DIR 2         /* bw_plat_default_save_dir    */
BW_WASM_IMPORT(os_path) int bw_host_os_path(int kind, char *out, unsigned n);
/* The system clipboard's text, cut to fit `out`: 0, or -1 when it holds
 * no text (SDL hands the text back in memory of its own, which the module
 * cannot read). */
BW_WASM_IMPORT(clipboard_get) int bw_host_clipboard_get(char *out, unsigned n);
/* The player's time zone: local time minus UTC at time `t`, in seconds
 * (daylight saving time included). */
BW_WASM_IMPORT(tz_offset) long long bw_host_tz_offset(long long t);
/* The host's worker threads for the compositors (engine/g2d/par.c): how
 * many there are besides the caller, and a run of `fn(ctx, i)` for i in
 * [0, n) across them, returning when every task is done.  `fn` is a
 * function pointer of the module; each task comes back in through the
 * export bw_par_task(fn, ctx, i), on a thread of its own with its own
 * stack. */
BW_WASM_IMPORT(par_workers) unsigned bw_host_par_workers(void);
BW_WASM_IMPORT(par_run) void bw_host_par_run(unsigned fn, void *ctx, unsigned n);
/* The host's job thread (sdk/plat_thread.h, bw_thread_start_beside): one
 * long task at a time beside the game, at a low priority.  job_ready: 1
 * when the host has the thread.  job_start: `fn(ctx, 0)` given to it, in
 * through bw_par_task as a compositor's task; 0 when it is busy or absent.
 * job_join: wait until the job given is done. */
BW_WASM_IMPORT(job_ready) int bw_host_job_ready(void);
BW_WASM_IMPORT(job_start) int bw_host_job_start(unsigned fn, void *ctx);
BW_WASM_IMPORT(job_join) void bw_host_job_join(void);
/* The heap's lock (sdk/wasm_heap.c): the C library's malloc has none, and
 * the host's threads run module code that allocates.  Recursive. */
BW_WASM_IMPORT(heap_lock) void bw_host_heap_lock(void);
BW_WASM_IMPORT(heap_unlock) void bw_host_heap_unlock(void);
/* A console window for the program's messages (sdk/plat_crash.h,
 * bw_plat_console_open), where the platform has such a thing. */
BW_WASM_IMPORT(console_open) void bw_host_console_open(void);

/* TCP sockets for the relay (sdk/mod_net.c): the host's own, all
 * non-blocking, named by small numbers.  sock_connect: 0 connected, 1 in
 * progress, -1 failed; sock_poll (no wait): 1 ready, 0 not yet, -1 failed;
 * sock_send and sock_recv: the bytes moved, -2 when it would block, -1 on
 * an error (sock_recv's 0: the other end closed); sock_error is SO_ERROR.
 * Addresses are IPv4 in network order, ports in host order. */
BW_WASM_IMPORT(sock_open) int bw_host_sock_open(void);
BW_WASM_IMPORT(sock_connect) int bw_host_sock_connect(int s, unsigned addr, unsigned port);
BW_WASM_IMPORT(sock_poll) int bw_host_sock_poll(int s, int for_write);
BW_WASM_IMPORT(sock_error) int bw_host_sock_error(int s);
BW_WASM_IMPORT(sock_send) int bw_host_sock_send(int s, const void *buf, unsigned n);
BW_WASM_IMPORT(sock_recv) int bw_host_sock_recv(int s, void *buf, unsigned n);
BW_WASM_IMPORT(sock_close) void bw_host_sock_close(int s);
BW_WASM_IMPORT(sock_listen_loopback) int bw_host_sock_listen_loopback(int s, unsigned port);
BW_WASM_IMPORT(sock_accept) int bw_host_sock_accept(int s);
BW_WASM_IMPORT(sock_resolve) int bw_host_sock_resolve(const char *name, unsigned *addr);

#endif /* __wasm__ */

#endif /* BW_PORT_WASM_HOST_H */
