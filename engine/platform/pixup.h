/* engine/platform/pixup.h -- edge-directed pixel-art upscaler (pixup.c). */
#ifndef EIDOS_PIXUP_H
#define EIDOS_PIXUP_H

/* src: w x h RGBA.  Returns a malloc'd (w*s) x (h*s) RGBA image, or NULL. */
unsigned char *eidos_pixup(const unsigned char *src, int w, int h, int s);

#endif
