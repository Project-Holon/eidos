/*
 * engine/platform/ra_banner_draw.h -- the achievement banner of the
 * two-screen layouts (stacked and side by side), drawn on the CPU like the
 * run-status panel and shown on top of everything (docs/retroachievements.md,
 * "The banner").  The one-screen layout draws its own banner in engine/ui.
 */

#ifndef EIDOS_ENGINE_RA_BANNER_DRAW_H
#define EIDOS_ENGINE_RA_BANNER_DRAW_H

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The picture's own size, in pixels (the port's 6 x 10 font). */
#define EIDOS_RA_BANNER_W 240
#define EIDOS_RA_BANNER_H 52

typedef struct EidosRaBannerView {
    const u8 *rgba;     /* the picture, RGBA (R, G, B, A) */
    int w, h;           /* its size */
    EidosRect dst;      /* where it goes in the output, in output pixels */
    u32 generation;     /* grows when the picture changes */
} EidosRaBannerView;

/*
 * The banner on show and where it is.  `out_w`, `out_h` are the output's
 * size; (`ax`, `ay`, `aw`) the main screen's left, top and width, in
 * output pixels (the top or left one of the two).  The banner sits in that
 * screen's top left corner, about half its width, and slides in from the
 * left.  Returns 0 when there is a banner to draw, -1 when there
 * is none (no banner, or the build has no RetroAchievements).
 */
int eidos_ra_banner_view(EidosRaBannerView *view, int out_w, int out_h,
                         int ax, int ay, int aw);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_RA_BANNER_DRAW_H */
