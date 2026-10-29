/*
 * engine/ui/padglyph.h -- the gamepad's buttons as the hints draw them: one
 * look per controller family, each drawn by the kit at the key cap's height
 * (13 canvas pixels).  The family follows the controller in use or the
 * player's choice in Settings.
 */

#ifndef EIDOS_UI_PADGLYPH_H
#define EIDOS_UI_PADGLYPH_H

#include "kit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The controller families. */
enum {
    UI_PAD_XBOX360,
    UI_PAD_XBOXONE,
    UI_PAD_PS3,
    UI_PAD_PS4,
    UI_PAD_PS5,
    UI_PAD_SWITCH,
    UI_PAD_GAMECUBE,
    UI_PAD_STEAMDECK,
    UI_PAD_FAMILIES
};

/* The controls, by position: the order of SDL's gamepad buttons (the face
 * buttons by where they sit: south, east, west, north), then the triggers
 * and the left stick's four directions. */
enum {
    UI_PAD_SOUTH, UI_PAD_EAST, UI_PAD_WEST, UI_PAD_NORTH,
    UI_PAD_BACK, UI_PAD_GUIDE, UI_PAD_START,
    UI_PAD_LSTICK, UI_PAD_RSTICK,
    UI_PAD_LSHOULDER, UI_PAD_RSHOULDER,
    UI_PAD_DUP, UI_PAD_DDOWN, UI_PAD_DLEFT, UI_PAD_DRIGHT,
    UI_PAD_LTRIGGER, UI_PAD_RTRIGGER,
    UI_PAD_LS_UP, UI_PAD_LS_DOWN, UI_PAD_LS_LEFT, UI_PAD_LS_RIGHT,
    UI_PAD_CONTROLS
};

/* The family's name, for Settings ("Xbox 360", ...). */
const char *ui_pad_family_name(int family);
/* The width of a control's glyph and the glyph itself, drawn with its top
 * left at (x, y); both 0 for a control the family has no glyph for. */
int ui_pad_glyph_w(int family, int control);
int ui_pad_glyph(EidosUiCtx *c, int x, int y, int family, int control);

#ifdef __cplusplus
}
#endif

#endif
