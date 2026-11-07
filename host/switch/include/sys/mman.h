/*
 * host/switch/include/sys/mman.h -- wasm2c's runtime includes this header on
 * every system but Windows.  Horizon has no mmap; the Switch build sets
 * WASM_RT_USE_MMAP=0 (the memory is a heap block, host/main.c), so nothing
 * from it is used and the header can be empty.
 */
#ifndef HOST_SWITCH_SYS_MMAN_H
#define HOST_SWITCH_SYS_MMAN_H
#endif
