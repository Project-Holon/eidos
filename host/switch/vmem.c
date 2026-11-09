/*
 * host/switch/vmem.c -- guard-page memory for the game module on Horizon
 * (include/sys/mman.h says which calls).
 *
 * A desktop reserves the module's 8 GiB of address space and maps pages
 * into it as the memory grows; a stray access lands on an unmapped page
 * and faults, so the game's code carries no check of its own.  Horizon
 * has no mmap, but a program can do the same with libnx: it reserves a
 * range of the code region (virtmemFindCodeMemory) and maps heap blocks
 * into it with svcMapProcessCodeMemory, then makes them read-write with
 * svcSetProcessMemoryPermission (as the module loader does for a module's
 * data).  Pages of the range nothing was mapped to stay unmapped.
 *
 * Without the program's own process handle (a loader that does not give
 * it), or when the kernel refuses, the memory is one heap block of the
 * budget with a no-access block after it: no fault for an access far past
 * the end, but no check on every access either (the 32-bit builds have
 * none at all).  The log says which one runs.
 */
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <switch.h>

#ifndef HOST_MEMORY_BUDGET_MB
#define HOST_MEMORY_BUDGET_MB 512
#endif

#define VMEM_PAGE        0x1000u
#define VMEM_MAX_MAPS    16
#define VMEM_TAIL_GUARD  (64u * 1024u * 1024u)

typedef struct VmemMap {
    u64 dst;
    void *src;
    u64 size;
} VmemMap;

static struct {
    int guard_pages;            /* 1: the reserved range; 0: the heap block */
    u8 *base;
    u64 size;                   /* the range, or the block with its tail    */
    u64 used;                   /* bytes made readable from base            */
    VirtmemReservation *rv;
    VmemMap maps[VMEM_MAX_MAPS];
    int n_maps;
} g_vm;

/* One page mapped, made read-write and taken back: the kernel lets this
 * program map code memory (a loader may not give it the right handle). */
static int probe_map(u64 dst)
{
    Handle h = envGetOwnProcessHandle();
    void *src = memalign(VMEM_PAGE, VMEM_PAGE);
    Result rc;

    if (src == NULL) {
        return 0;
    }
    rc = svcMapProcessCodeMemory(h, dst, (u64)(uintptr_t)src, VMEM_PAGE);
    if (R_SUCCEEDED(rc)) {
        Result rp = svcSetProcessMemoryPermission(h, dst, VMEM_PAGE, Perm_Rw);

        if (R_SUCCEEDED(rp)) {
            *(volatile u32 *)(uintptr_t)dst = 1u;
            (void)svcSetProcessMemoryPermission(h, dst, VMEM_PAGE, Perm_None);
        }
        (void)svcUnmapProcessCodeMemory(h, dst, (u64)(uintptr_t)src, VMEM_PAGE);
        rc = rp;
    }
    if (R_FAILED(rc)) {
        fprintf(stderr, "eidos: module memory: code memory refused (0x%x)\n", (unsigned)rc);
    }
    free(src);
    return R_SUCCEEDED(rc);
}

static void *reserve_range(size_t size)
{
    void *at;

    if (!envGetOwnProcessHandle()) {
        return NULL;
    }
    virtmemLock();
    at = virtmemFindCodeMemory(size, VMEM_PAGE);
    g_vm.rv = at != NULL ? virtmemAddReservation(at, size) : NULL;
    virtmemUnlock();
    if (g_vm.rv != NULL && !probe_map((u64)(uintptr_t)at)) {
        virtmemLock();
        virtmemRemoveReservation(g_vm.rv);
        virtmemUnlock();
        g_vm.rv = NULL;
    }
    return g_vm.rv != NULL ? at : NULL;
}

static void *heap_block(void)
{
    u64 budget = (u64)HOST_MEMORY_BUDGET_MB * 1024u * 1024u;
    u8 *b = (u8 *)memalign(VMEM_PAGE, budget + VMEM_TAIL_GUARD);

    if (b == NULL) {
        return NULL;
    }
    memset(b, 0, budget);
    /* the tail: an access just past the budget faults */
    (void)svcSetMemoryPermission(b + budget, VMEM_TAIL_GUARD, Perm_None);
    g_vm.size = budget + VMEM_TAIL_GUARD;
    g_vm.used = budget;
    return b;
}

void *mmap(void *addr, size_t size, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    if (g_vm.base != NULL) {
        errno = ENOMEM;             /* one module, one memory */
        return MAP_FAILED;
    }
    g_vm.base = (u8 *)reserve_range(size);
    if (g_vm.base != NULL) {
        g_vm.guard_pages = 1;
        g_vm.size = size;
        fprintf(stderr, "eidos: module memory: %llu MiB reserved, guard pages\n",
                (unsigned long long)(size >> 20));
        return g_vm.base;
    }
    g_vm.base = (u8 *)heap_block();
    if (g_vm.base == NULL) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    fprintf(stderr, "eidos: module memory: one %d MiB block (no own process handle "
            "or no code memory), a no-access block after it\n", HOST_MEMORY_BUDGET_MB);
    return g_vm.base;
}

/* Makes [addr, addr + size) readable and writable: the module's memory
 * grows from base, so this is always the range right after the last. */
int mprotect(void *addr, size_t size, int prot)
{
    u64 at = (u64)((u8 *)addr - g_vm.base);
    VmemMap *m;
    Result rc;

    (void)prot;
    if (g_vm.base == NULL || (u8 *)addr < g_vm.base || at + size > g_vm.size) {
        errno = EINVAL;
        return -1;
    }
    if (size == 0 || at + size <= g_vm.used) {
        return 0;                   /* readable already */
    }
    if (!g_vm.guard_pages || at != g_vm.used || g_vm.n_maps == VMEM_MAX_MAPS
        || (size % VMEM_PAGE) != 0) {
        errno = ENOMEM;
        return -1;
    }
    m = &g_vm.maps[g_vm.n_maps];
    m->src = memalign(VMEM_PAGE, size);
    if (m->src == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memset(m->src, 0, size);
    m->dst = (u64)(uintptr_t)addr;
    m->size = size;
    rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(), m->dst, (u64)(uintptr_t)m->src, size);
    if (R_SUCCEEDED(rc)) {
        rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), m->dst, size, Perm_Rw);
        if (R_FAILED(rc)) {
            (void)svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), m->dst,
                                            (u64)(uintptr_t)m->src, size);
        }
    }
    if (R_FAILED(rc)) {
        fprintf(stderr, "eidos: module memory: mapping %llu MiB failed (0x%x)\n",
                (unsigned long long)(size >> 20), (unsigned)rc);
        free(m->src);
        errno = ENOMEM;
        return -1;
    }
    g_vm.n_maps++;
    g_vm.used = at + size;
    return 0;
}

int munmap(void *addr, size_t size)
{
    int i;

    (void)size;
    if (g_vm.base == NULL || (u8 *)addr != g_vm.base) {
        errno = EINVAL;
        return -1;
    }
    if (g_vm.guard_pages) {
        for (i = g_vm.n_maps - 1; i >= 0; i--) {
            VmemMap *m = &g_vm.maps[i];

            (void)svcSetProcessMemoryPermission(envGetOwnProcessHandle(), m->dst, m->size, Perm_None);
            (void)svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), m->dst,
                                            (u64)(uintptr_t)m->src, m->size);
            free(m->src);
        }
        virtmemLock();
        virtmemRemoveReservation(g_vm.rv);
        virtmemUnlock();
    } else {
        (void)svcSetMemoryPermission(g_vm.base + g_vm.used, g_vm.size - g_vm.used, Perm_Rw);
        free(g_vm.base);
    }
    memset(&g_vm, 0, sizeof(g_vm));
    return 0;
}
