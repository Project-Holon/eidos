/*
 * engine/platform/texpack.h -- HD textures for the GPU 3D (texpack.c).
 */

#ifndef EIDOS_TEXPACK_H
#define EIDOS_TEXPACK_H

#include <stdint.h>

/* A texture's name: a hash of its decoded RGBA and size. */
uint64_t eidos_texpack_hash(const unsigned char *rgba, int w, int h);

/*
 * The HD copy of this w x h RGBA texture (out_w x out_h, a whole multiple),
 * or NULL to draw the original.  *later is set when an automatic copy is
 * being made: the caller asks again on a later frame.  Also writes the
 * original to textures/dump/ while dumping is on.
 */
const unsigned char *eidos_texpack_find(const unsigned char *rgba, int w, int h,
                                        int *out_w, int *out_h, int *later);

#endif
