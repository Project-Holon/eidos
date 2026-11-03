/*
 * engine/ui/choice_list.h -- the game's own choice lists in the interface's
 * style, for the one-screen view.
 *
 * Many of the game's questions are a BmpMenuList (arm9/bmp_menulist.c) in a
 * window the game draws: a script's list menu and its yes/no in the field,
 * the professor's yes/no and name lists in the intro.  ui_choice_list
 * covers that window with a list in the kit's style, at the same place,
 * with the same rows.
 *
 * The game's cursor is the marked row.  It moves only by UP and DOWN
 * presses, so the game plays its own cursor sound, prints the row's help
 * and keeps its state.  A row is picked by A.
 *
 *   mouse   the cursor follows the pointer; a click picks the row
 *   finger  a tap on another row moves the cursor there; a tap on the
 *           cursor's row picks it
 *   wheel   moves the cursor
 *
 * A click or a tap outside the list does nothing (it is not A: that would
 * pick the row the cursor is on).  B is the caller's: a right click, where
 * the game takes B.
 */
#ifndef EIDOS_UI_CHOICE_LIST_H
#define EIDOS_UI_CHOICE_LIST_H

#include "ui.h"
#include "kit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Draw the list `list` (a BmpMenuList) over its window `win` (a BmpWin).
 * The DS screen the window is on stands at (ox, oy) in canvas pixels,
 * `s` canvas pixels per DS pixel.  Returns 1 when the list was drawn, 0
 * when it cannot be read (nothing drawn).
 */
#define UI_CHOICE_DARK   0  /* the interface's dark panel                    */
#define UI_CHOICE_WHITE  1  /* a person's question: the white box's style    */
#define UI_CHOICE_SYSTEM 2  /* a system question: the black box's style      */
int  ui_choice_list(EidosUiCtx *c, const EidosUiFrame *in, void *list, const void *win,
                    float ox, float oy, float s, int style);

/* No list is open this frame: a pending walk of the cursor is dropped. */
void ui_choice_list_idle(void);

/*
 * The game's touch menus (APP_TASKMENU: the yes/no of the C-Gear's power,
 * the PC, the Entree and others), drawn as a list in `style` whose bottom
 * right corner is (right, bottom), with the game's own labels.  A click or
 * a tap taps the game's row where the game put it, so the game does what
 * it does for the stylus.  The keys are the game's when it takes them;
 * when it takes none (isDisableKey), UP and DOWN move the list's own
 * cursor and A taps that row.  Returns 1 when the menu was drawn.
 */
int  ui_taskmenu(EidosUiCtx *c, void *menu, int right, int bottom, int style);
/* The same with the rows' labels given (labels[0..count-1]): for a game
 * that frees its menu's words once the menu has drawn them. */
int  ui_taskmenu_labels(EidosUiCtx *c, void *menu, int right, int bottom, int style,
                        const char *const *labels);
/* Its row count (0 when it cannot be read) and a tap on row i. */
int  ui_taskmenu_count(const void *menu);
void ui_taskmenu_tap(const void *menu, int i);

#ifdef __cplusplus
}
#endif

#endif
