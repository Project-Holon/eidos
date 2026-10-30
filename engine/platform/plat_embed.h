/*
 * engine/platform/plat_embed.h -- the game shown inside another program
 * (plat_embed.c; the mapping: embed_shm.h).
 */
#ifndef EIDOS_PLAT_EMBED_H
#define EIDOS_PLAT_EMBED_H

#include <stdint.h>

#include "embed_shm.h"

int eidos_embed_on(void);                       /* EIDOS_EMBED names a mapping that opened */
int eidos_embed_key(int scancode);              /* the host holds the key */
uint32_t eidos_embed_mouse(float *x, float *y); /* picture pixels; SDL_BUTTON_MASK bits */
void eidos_embed_edges(int *pressed, int *released, int *rpressed, int *wheel);
int eidos_embed_want_size(int *w, int *h);      /* the host's picture size, 0 none */
unsigned char *eidos_embed_pixels(int w, int h); /* where a w x h RGB picture goes */
void eidos_embed_publish(int w, int h);         /* the picture there is whole */
int eidos_embed_quit(void);                     /* the host asked, or is gone */
void eidos_embed_end(void);                     /* the game has ended */

#endif
