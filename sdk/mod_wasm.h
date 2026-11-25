/*
 * sdk/mod_wasm.h -- code mods built for WebAssembly (docs/mod-code-wasm.md).
 *
 * A mod's `code` file is a native library or a wasm module; the loader
 * (sdk/mod_loader.c) tells them apart by the module's magic and hands a
 * module to this layer.  The module runs in WAMR's interpreter.  It reaches
 * the port through the imports of module "eidos" (sdk/api/v5/core.json and
 * eidos_wasm.h): the core calls (log, on, script, memory access) and one
 * import per state call and service, the same table a native mod is
 * handed.
 *
 * A build without the runtime (tools/fetch_wamr.py was not run) keeps this
 * interface: bw_mod_wasm_available answers 0 and every module is refused
 * with a message.
 */
#ifndef BW_PORT_MOD_WASM_H
#define BW_PORT_MOD_WASM_H

#include <stddef.h>

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the port was built with the wasm runtime. */
int bw_mod_wasm_available(void);

/* 1 when the file at `path` starts with the wasm magic ("\0asm"). */
int bw_mod_wasm_is_module(const char *path);

/* A second receiver of the modules' log lines (eidos_log), beside the error
 * output: the mod's label, the text and its length.  For tests; NULL: none. */
void bw_mod_wasm_set_log_tap(void (*tap)(const char *label, const char *text, int len));

/*
 * Read, check and instantiate the module at `path`.  The handle, or NULL
 * with a one-line reason in `err`.  `label` names the mod in messages.
 */
void *bw_mod_wasm_open(const char *path, const char *label, char *err,
                       size_t err_size);

/*
 * Run the module's eidos_mod_start.  `host` is the table the loader built
 * for the mod (its `on` and `script` bind for that mod); it stays valid
 * until bw_mod_wasm_close.  *state is set to the handle, which is the
 * state the mod's event handlers are handed.  The start's answer (0 for
 * started), or -1 when the call failed.
 */
int bw_mod_wasm_start(void *wasm, const BwModHost *host, void **state);

/* Run the module's eidos_mod_stop. */
void bw_mod_wasm_stop(void *wasm);

/* Free the instance and the module. */
void bw_mod_wasm_close(void *wasm);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_WASM_H */
