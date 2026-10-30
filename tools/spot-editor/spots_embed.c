/*
 * tools/spot-editor/spots_embed.c -- the spot editor's side of the game
 * shown inside it (engine/platform/embed_shm.h): the shared memory and the
 * game's process.  Apart from eidos_spots.c so <windows.h> stays out of it.
 */

#include "spots_embed.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE g_map;

EidosEmbedShm *spots_embed_open(char *name, size_t n)
{
    EidosEmbedShm *s;

    spots_embed_close(NULL);
    snprintf(name, n, "Local\\eidos_spots_embed_%lu", (unsigned long)GetCurrentProcessId());
    g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)EIDOS_EMBED_BYTES, name);
    if (g_map == NULL) {
        return NULL;
    }
    s = (EidosEmbedShm *)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (s == NULL) {
        CloseHandle(g_map);
        g_map = NULL;
        return NULL;
    }
    memset(s, 0, sizeof *s);
    s->magic = EIDOS_EMBED_MAGIC;
    s->size = (uint32_t)EIDOS_EMBED_BYTES;
    s->mx = s->my = -1;
    return s;
}

void spots_embed_close(EidosEmbedShm *s)
{
    if (s != NULL) {
        UnmapViewOfFile(s);
    }
    if (g_map != NULL) {
        CloseHandle(g_map);
        g_map = NULL;
    }
}

int spots_embed_alive(intptr_t proc)
{
    return proc > 0 && WaitForSingleObject((HANDLE)proc, 0) == WAIT_TIMEOUT;
}

void spots_embed_kill(intptr_t proc)
{
    if (proc > 0) {
        if (WaitForSingleObject((HANDLE)proc, 1500) == WAIT_TIMEOUT) {
            TerminateProcess((HANDLE)proc, 0);
        }
        CloseHandle((HANDLE)proc);
    }
}

#else

EidosEmbedShm *spots_embed_open(char *name, size_t n) { (void)name; (void)n; return NULL; }
void spots_embed_close(EidosEmbedShm *s) { (void)s; }
int spots_embed_alive(intptr_t proc) { (void)proc; return 0; }
void spots_embed_kill(intptr_t proc) { (void)proc; }

#endif
