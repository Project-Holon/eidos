/*
 * engine/ui/textbox.h -- the game's text windows as the interface's text
 * boxes in the one-screen layout (engine/ui/textbox.c).
 */
#ifndef EIDOS_UI_TEXTBOX_H
#define EIDOS_UI_TEXTBOX_H

#include "ui.h"
#include "kit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TEXTBOX_NARRATION 0  /* white: the plain window, signs, the intro */
#define UI_TEXTBOX_BALLOON   1  /* white with a tail to the speaker          */
#define UI_TEXTBOX_SYSTEM    2  /* black with the strip: the PC, the save   */

/* One box at (x, y), w wide, holding the letters of the game's window
 * `bmpwin` (a BmpWin); a balloon's tail aims at (tail_x, tail_y) when
 * tail_x >= 0.  Canvas pixels; the letters are drawn one canvas pixel per
 * DS pixel (the canvas at the DS scale, eidos_ui_want_ds_scale). */
void ui_textbox_draw(EidosUiCtx *c, int kind, int x, int y, int w, const void *bmpwin,
                     int tail_x, int tail_y);

/* The field's open text windows (talk balloons, the plain window, the
 * system and information windows) as boxes, the DS screen at (ox, oy),
 * s canvas pixels per DS pixel; `skip` is a window not to draw (an open
 * choice list's).  Returns the main-engine BGs to leave out of the
 * screen (g2d_hw_hide_main_bgs), 0 when nothing is open. */
u32  ui_textbox_field(EidosUiCtx *c, void *fieldMap, float ox, float oy, float s, const void *skip);
/* A field text window is open (for a recipe's match). */
int  ui_textbox_field_active(void *fieldMap);
/* The kind of box a field window belongs with (a choice list takes the
 * style of the window it answers). */
int  ui_textbox_field_kind(void *fieldMap, const void *bmpwin);

#ifdef __cplusplus
}
#endif

#endif
