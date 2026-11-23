/*
 * engine/platform/plat_dual.h -- a second physical display for the second
 * DS screen (dual-screen handhelds: the AYN Thor, the Ayaneo Flip DS, ...).
 *
 * SDL gives the game one window.  On Android the host program opens the
 * second display itself (android/.../DualScreen.java shows a Presentation
 * there, host/dual.c makes an EGL surface over it that shares the game's
 * OpenGL context), and the presenter draws into it between these calls:
 *
 *   eidos_dual_size    1 when a second display is there, with its size in
 *                      pixels; 0 otherwise (every other platform: 0)
 *   eidos_dual_begin   the second display's surface becomes the one GL
 *                      draws to (framebuffer 0); 0 when it could not
 *   eidos_dual_end     shows what was drawn there and gives GL back the
 *                      window's surface
 *   eidos_dual_touch   1 while a finger is on the second display, with
 *                      where, as 0..65535 across and down
 */
#ifndef EIDOS_PLAT_DUAL_H
#define EIDOS_PLAT_DUAL_H

#if defined(__wasm__)
#include "wasm_host.h"

BW_WASM_IMPORT(dual_size)  int  bw_host_dual_size(int *w, int *h);
BW_WASM_IMPORT(dual_begin) int  bw_host_dual_begin(void);
BW_WASM_IMPORT(dual_end)   void bw_host_dual_end(void);
BW_WASM_IMPORT(dual_touch) int  bw_host_dual_touch(int *x, int *y);

static inline int  eidos_dual_size(int *w, int *h)  { return bw_host_dual_size(w, h); }
static inline int  eidos_dual_begin(void)           { return bw_host_dual_begin(); }
static inline void eidos_dual_end(void)             { bw_host_dual_end(); }
static inline int  eidos_dual_touch(int *x, int *y) { return bw_host_dual_touch(x, y); }
#else
static inline int  eidos_dual_size(int *w, int *h)  { (void)w; (void)h; return 0; }
static inline int  eidos_dual_begin(void)           { return 0; }
static inline void eidos_dual_end(void)             { }
static inline int  eidos_dual_touch(int *x, int *y) { (void)x; (void)y; return 0; }
#endif

#endif /* EIDOS_PLAT_DUAL_H */
