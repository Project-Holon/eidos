/*
 * engine/ui/ui_lang.h -- the one-screen interface's labels in the game's
 * language (engine/ui/ui_lang.c).
 */
#ifndef EIDOS_UI_LANG_H
#define EIDOS_UI_LANG_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's language (src/game/version.h ids: 2 English, 3 French, 4
 * Italian, 5 German, 7 Spanish); anything else is English. */
void ui_lang_set(int game_language);

/* `en` in a mod's words when a mod has it, else in the game's language when
 * the interface's table has it, else `en`. */
const char *ui_tr(const char *en);

/* A printf template ("PWR %d  ACC %d") in a mod's words or the game's
 * language, to be filled in after: a label built from numbers or names is
 * translated before its values go in.  The translation is taken only when
 * its conversions are the English's, the same kinds in the same order
 * (flags and widths may differ); else the English, so a translator's slip
 * cannot make the program read the wrong value (docs/mod-ui-text.md). */
#if defined(__GNUC__)
const char *ui_trf(const char *fmt) __attribute__((format_arg(1)));
#else
const char *ui_trf(const char *fmt);
#endif

/* A mod's `ui_text` file (docs/mod-ui-text.md): "English = translation"
 * lines; '#' starts a comment.  `owner` names the mod for
 * ui_lang_mod_clear.  Returns the number of pairs taken. */
int ui_lang_mod_add(u32 owner, const char *text, size_t len);

/* Forget one mod's words, or every mod's with UI_LANG_ALL_MODS. */
#define UI_LANG_ALL_MODS 0xFFFFFFFFu
void ui_lang_mod_clear(u32 owner);

#ifdef __cplusplus
}
#endif

#endif
