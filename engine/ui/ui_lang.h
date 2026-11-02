/*
 * engine/ui/ui_lang.h -- the one-screen interface's labels in the game's
 * language (engine/ui/ui_lang.c).
 */
#ifndef EIDOS_UI_LANG_H
#define EIDOS_UI_LANG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's language (src/game/version.h ids: 2 English, 3 French, 4
 * Italian, 5 German, 7 Spanish); anything else is English. */
void ui_lang_set(int game_language);

/* `en` in that language when the interface's table has it, else `en`. */
const char *ui_tr(const char *en);

#ifdef __cplusplus
}
#endif

#endif
