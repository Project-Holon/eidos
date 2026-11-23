/*
 * engine/platform/plat_dual.h -- a second physical display for the second
 * DS screen (dual-screen handhelds: the AYN Thor, the Ayaneo Flip DS, ...).
 *
 * SDL gives the game one window.  On Android the host program opens the
 * second display itself (android/.../DualScreen.java shows a Presentation
 * there, sized to the DS screen's shape), and the presenter draws its
 * picture into a framebuffer of its own, never leaving the window's
 * surface; the host reads it back (a frame late, through two pixel
 * buffers) and copies it to the second display's window:
 *
 *   eidos_dual_size    1 when a second display is there, with its size in
 *                      pixels; 0 otherwise (every other platform: 0)
 *   eidos_dual_post    the picture in framebuffer `fbo`, w x h, for the
 *                      second display
 *   eidos_dual_touch   1 while a finger is on the second display, with
 *                      where, as 0..65535 across and down
 */
#ifndef EIDOS_PLAT_DUAL_H
#define EIDOS_PLAT_DUAL_H

#if defined(__wasm__)
#include "wasm_host.h"

BW_WASM_IMPORT(dual_size)  int  bw_host_dual_size(int *w, int *h);
BW_WASM_IMPORT(dual_post)  void bw_host_dual_post(unsigned fbo, int w, int h);
BW_WASM_IMPORT(dual_touch) int  bw_host_dual_touch(int *x, int *y);

static inline int  eidos_dual_size(int *w, int *h)  { return bw_host_dual_size(w, h); }
static inline void eidos_dual_post(unsigned fbo, int w, int h) { bw_host_dual_post(fbo, w, h); }
static inline int  eidos_dual_touch(int *x, int *y) { return bw_host_dual_touch(x, y); }
#else
static inline int  eidos_dual_size(int *w, int *h)  { (void)w; (void)h; return 0; }
static inline void eidos_dual_post(unsigned fbo, int w, int h) { (void)fbo; (void)w; (void)h; }
static inline int  eidos_dual_touch(int *x, int *y) { (void)x; (void)y; return 0; }
#endif

#endif /* EIDOS_PLAT_DUAL_H */
