/*
 * host/switch/include/sys/mman.h -- the three calls wasm2c's runtime makes
 * for its guard-page memory (wasm-rt-mem-impl.c, WASM_RT_USE_MMAP=1):
 * reserve the module's 8 GiB with no access, make the part in use
 * readable and writable, release it.  Horizon has no mmap; host/switch/
 * vmem.c answers them with libnx (docs/consoles.md, "Memory checks").
 */
#ifndef HOST_SWITCH_SYS_MMAN_H
#define HOST_SWITCH_SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE     0
#define PROT_READ     1
#define PROT_WRITE    2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)-1)

void *mmap(void *addr, size_t size, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t size);
int mprotect(void *addr, size_t size, int prot);

#endif
