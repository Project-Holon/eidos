/*
 * engine/ui/keyboard_parts.h -- the keyboard's key, shared by the game's
 * name entry (recipe_input.c) and the port's own keyboard
 * (recipe_keyboard.c), so both look the same in every style.
 */
#ifndef EIDOS_UI_KEYBOARD_PARTS_H
#define EIDOS_UI_KEYBOARD_PARTS_H

#include "ui.h"

/* A key of any size: its label centred, doubled on a large key; `cursor`
 * the keys' cursor is on it, `hover` the pointer, `down` it is pressed. */
void ui_kb_key(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int cursor, int hover, int down,
               u32 accent);
/* The column count that lays `n` keys in `aw` x `ah` with keys closest to
 * their best shape. */
int ui_kb_cols(int n, int aw, int ah, int gap);

#endif
