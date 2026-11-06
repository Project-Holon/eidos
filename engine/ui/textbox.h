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
 * `bmpwin` (a BmpWin).  Canvas pixels; the letters are drawn one canvas
 * pixel per DS pixel (the canvas at the DS scale, eidos_ui_want_ds_scale).
 * The two tail arguments are unused (-1): only the field's balloons have a
 * tail (ui_textbox_field). */
void ui_textbox_draw(EidosUiCtx *c, int kind, int x, int y, int w, const void *bmpwin,
                     int tail_x, int tail_y);

/* A box of `kind` without words (the caller prints into it). */
void ui_textbox_frame(EidosUiCtx *c, int kind, int x, int y, int w, int h);
/* The height ui_textbox_draw gives the window's box; 0 when it is blank. */
int  ui_textbox_h(const void *bmpwin);
/* The Bmp of a live BmpWin, or NULL. */
const void *ui_bmpwin_bmp(const void *bmpwin);
/* The letters of a Bmp (the game's 4-bit text bitmap), what is printed
 * in it placed with its top-left at (x, y), one canvas pixel per bitmap
 * pixel, the letters in `ink`, their shadow in `shadow`; *w and *h get
 * the size of what is printed (0 when blank).  With c NULL it only
 * measures.  Returns 0 when nothing is printed. */
int  ui_letters(EidosUiCtx *c, const void *bmp, int x, int y, u32 ink, u32 shadow, int *w, int *h);
/* Debug: a Bmp's colour indices as a PGM file. */
void ui_letters_dump(const void *bmp, const char *path);

/* The field's open text windows (talk balloons, the plain window, the
 * system and information windows) as boxes, the DS screen at (ox, oy),
 * s canvas pixels per DS pixel; `skip` is a window not to draw (an open
 * choice list's).  Returns the main-engine BGs to leave out of the
 * screen (g2d_hw_hide_main_bgs), 0 when nothing is open. */
u32  ui_textbox_field(EidosUiCtx *c, void *fieldMap, float ox, float oy, float s, const void *skip);
/* How many boxes the last ui_textbox_field drew (an open window with
 * nothing printed in it draws none). */
int  ui_textbox_field_drawn(void);
/* A field text window is open (for a recipe's match). */
int  ui_textbox_field_active(void *fieldMap);
/* The kind of box a field window belongs with (a choice list takes the
 * style of the window it answers). */
int  ui_textbox_field_kind(void *fieldMap, const void *bmpwin);

#ifdef __cplusplus
}
#endif

#endif
