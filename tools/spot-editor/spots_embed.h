/*
 * tools/spot-editor/spots_embed.h -- the game shown inside the spot editor
 * (spots_embed.c; the mapping: engine/platform/embed_shm.h).
 */
#ifndef SPOTS_EMBED_H
#define SPOTS_EMBED_H

#include <stddef.h>
#include <stdint.h>

#include "embed_shm.h"

/* A new mapping, cleared, its name in `name` (for EIDOS_EMBED); NULL when
 * there is none (not Windows). */
EidosEmbedShm *spots_embed_open(char *name, size_t n);
void spots_embed_close(EidosEmbedShm *s);
int spots_embed_alive(intptr_t proc);   /* the game (a _spawnl handle) still runs */
void spots_embed_kill(intptr_t proc);   /* a moment to end, then ended; the handle closed */

#endif
