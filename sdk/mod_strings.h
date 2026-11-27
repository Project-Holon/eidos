/*
 * sdk/mod_strings.h -- a mod's own text in the game's language: its strings
 * files (docs/mod-strings.md).
 *
 * A mod keeps its words in `strings.txt` at its root and in one file a
 * language beside it, `strings.<lang>.txt` (`strings.fr.txt`).  A line is
 * `key = text`.  The text of a key is the line of the game's language file,
 * else the plain file's.  The settings declaration (`@key` for a label),
 * the rules (`message @key`) and the code mods (string_get) all read the
 * same files.
 */
#ifndef BW_PORT_MOD_STRINGS_H
#define BW_PORT_MOD_STRINGS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The longest key, and the most values a text takes ({1} to {9}). */
#define BW_MOD_STRINGS_KEY_MAX    64
#define BW_MOD_STRINGS_VALUES_MAX 9

/* The game's language as a file names it: "en", "fr", "de", "it" or "es"
 * (src/game/version.h: 2, 3, 5, 4, 7; anything else is English). */
const char *bw_mod_lang_code(void);

/* Is `key` a key a strings file may hold: letters, digits, '.', '_' and
 * '-', 1 to 63 characters. */
int bw_mod_strings_key_valid(const char *key);

/* The text of `key` for the mod at `root`: its strings.<lang>.txt line,
 * else its strings.txt line; NULL when neither has it.  The text lives
 * until the program ends. */
const char *bw_mod_strings_get(const char *root, const char *key);

/* A text a declaration wrote: "@key" is the key's text (the key itself
 * when no file has it, so the mistake shows), anything else is itself. */
const char *bw_mod_strings_text(const char *root, const char *text);

/* `line` with {1} to {9} replaced by values[0] to values[8] (a place with
 * no value is left out); "{{" is one '{'.  Cut at `size`, never in the
 * middle of a character.  The length written. */
size_t bw_mod_strings_fill(const char *line, const char *const *values, u32 count,
                           char *out, size_t size);

/* The loader: the mod with namespace `ns` is at `root`, so a rule or a
 * code mod that names only its namespace reads that root's files. */
void bw_mod_strings_bind(const char *ns, const char *root);
const char *bw_mod_strings_root(const char *ns);

/* Read every strings file again the next time a key is asked (the mod
 * menu's Reload mods).  The texts given out before stay valid. */
void bw_mod_strings_reload(void);

/* The services table (version 7): the calling mod's text of `key`, cut
 * to `size`; its whole length, or -1 when no file has the key.  The game's
 * language code ("fr"); 2, or -1 when `size` is under 3. */
int bw_mod_strings_service_get(const char *key, char *out, u32 size);
int bw_mod_strings_service_language(char *out, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_STRINGS_H */
