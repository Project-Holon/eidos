/*
 * engine/ui/popup.h -- a mod's pop-up, drawn over every one-screen scene
 * (engine/ui/popup.c).
 */

#ifndef EIDOS_UI_POPUP_H
#define EIDOS_UI_POPUP_H

#include "ui.h"

/* The first pop-up waiting, its top at canvas row `below`, at the right
 * edge or `centred`; `frame` is the interface's frame count. */
void eidos_ui_popup_draw(EidosUiCtx *c, u32 frame, int below, int centred);

#endif /* EIDOS_UI_POPUP_H */
