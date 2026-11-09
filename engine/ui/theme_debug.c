/*
 * engine/ui/theme_debug.c -- "debug-outline", a theme for checking the
 * theme table in the game: every panel is drawn as the default draws it,
 * then outlined in magenta, so each piece built on panels shows that it
 * went through the theme.  Selected with EIDOS_UI_THEME=debug-outline; not
 * offered in Settings.  It is also the smallest example of a theme: fill
 * the slots to change, leave the rest NULL, call ui_def_* to keep a part.
 */
#include "theme.h"

static void outline_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner,
                          u32 fill, u32 edge, int a)
{
    ui_def_panel(c, x, y, w, h, cut, corner, fill, edge, a);
    ui_fill(c, x, y, w, 1, 0xFF00FFu, 255);
    ui_fill(c, x, y + h - 1, w, 1, 0xFF00FFu, 255);
    ui_fill(c, x, y, 1, h, 0xFF00FFu, 255);
    ui_fill(c, x + w - 1, y, 1, h, 0xFF00FFu, 255);
}

const UiThemeOps ui_theme_debug_outline = {
    .id = "debug-outline",
    .name = "Debug outline",
    .panel = outline_panel,
};
