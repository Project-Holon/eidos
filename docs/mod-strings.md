# A mod's words in the game's language

A mod can give each of its texts in several languages. The game shows the
text for the language of the cartridge in use, and the mod's plain text when
it has none for that language. The languages are the ones eidos plays:

| Code | Language |
|---|---|
| `en` | English |
| `fr` | French |
| `de` | German |
| `it` | Italian |
| `es` | Spanish |

These are the codes the game text files use too (`a/0/0/2/<bank>.<lang>.msg`,
`docs/game-tables.md`). A cartridge in another language plays as English.

## The name and the description

```
name = Nuzlocke
name.fr = Nuzlocke (règles)
description = Lose a Pokemon when it faints.
description.fr = Un Pokémon K.O. est perdu.
```

`name.<lang>` and `description.<lang>` are the lines a game in that language
shows: on the Settings screen's Mods section and in the title screen's mod
manager. The launcher's Mods screen shows the `name.<lang>` of the profile's
language. Without one, the plain `name` and `description` show. The plain `name` stays the mod's own name: the mod's
saved data folder (its namespace) comes from it, so it does not change with
the language.

## The strings files

The mod's other texts go in its strings files, at the mod's root:

- `strings.txt`: the plain words, in the language the mod is written in.
- `strings.<lang>.txt` (`strings.fr.txt`): the words for a game in that
  language.

```
# strings.txt
nuz.lives       = Lives
nuz.lives.help  = How many Pokemon may faint before the run ends.
nuz.dead        = Pokemon lost: {1}. Lives left: {2}.
```

```
# strings.fr.txt
nuz.lives       = Vies
nuz.lives.help  = Combien de Pokémon peuvent être K.O. avant la fin.
nuz.dead        = Il reste {2} vies. Pokémon perdus : {1}.
```

- The files are UTF-8. A line is `key = text`.
- A key holds letters, digits, `.`, `_` and `-`, at most 63 characters.
- A line whose first character (after blanks) is `#` is a comment. A `#`
  inside a text is part of the text.
- The blanks around the text do not count. `\n` is a line break and `\\` a
  backslash.
- `{1}` to `{9}` are the places for values a rule or a code mod gives. A
  translation can put them in its own order. `{{` is one `{`.
- A line with nothing after the `=` counts as no line, so the plain text
  shows. A translation can be filled in a little at a time.
- A key given twice: the first line counts, and the log says so.

The text of a key is the line of the file for the game's language, else the
line of `strings.txt`. The same files serve the settings, the rules and the
code. **Reload mods** reads them again.

## In the settings file

In a mod's settings file (`docs/mod-services.md`, "Mod settings"), `@key` can
stand wherever a quoted text goes, and for each name of a choice:

```
tab     @nuz.tab
heading @nuz.group
option  nuz.lives int    @nuz.lives 0 10 3
help    nuz.lives @nuz.lives.help
option  nuz.cap   choice @nuz.cap @nuz.cap.off|@nuz.cap.gym|@nuz.cap.ace @solo
mode    solo @nuz.solo @nuz.solo.line
button  nuz.reset @nuz.reset
```

- This works for option, button and show labels, `help` lines, headings, tab
  titles, and mode titles and lines.
- A choice named `@key` is still that word for everything else: a condition
  names it (`?nuz.cap=@nuz.cap.gym`), and an Archipelago options file writes
  the key without the `@` when the `yaml` line gives no value for it.
- The `@lobby` and mode words after the choice names keep working.
- A key that no file has shows as the key, and the log names it.
- As written in the settings file, `@` included, a choice's name and a tab's
  title hold at most 31 characters, a label, a heading and a mode's title at
  most 63, and a mode's line at most 127. The text a key gives has no limit.

## In a rule

`@key` is a text in a rule (`docs/mod-rules-language.md`):

```
language 1
rule lost on battle.faint
    when event.side == 0
    do message @nuz.dead(state.lost, 3 - state.lost)
```

- `@key` is the key's text. `@key(<value>, ...)` puts the values in the
  places `{1}`, `{2}` and on. A value is a text or a number, at most 9.
- A rule that names a key neither `strings.txt` nor the file for the game's
  language has is refused at load, as a rule that names an undeclared table
  is.
- `@key` works wherever a text does: `message`, `send`, a comparison.

## In code

Two calls of the services table (`docs/mod-services.md`, version 7):

```c
int (*string_get)(const char *key, char *out, u32 size);
int (*string_language)(char *out, u32 size);
```

- `string_get` copies the calling mod's text for `key`, cut to `size` and
  never in the middle of a character, and answers the text's whole length.
  It answers -1 when no file has the key. The mod puts its own values in the
  `{1}` places.
- `string_language` writes the game's language code (`en`, `fr`, `de`, `it`
  or `es`) and answers 2. It answers -1 when `size` is under 3.

A wasm mod imports them as `eidos.string_get` and `eidos.string_language`.
An eidos older than 0.2.8 does not have them, and refuses a module that
imports them.

## The interface's words

A mod's `ui_text` file (`docs/mod-ui-text.md`) has a file for each language
too: `ui_text.fr.txt` beside `ui_text.txt` (the name of the file the
manifest gives, with the language before its extension). Without a
`ui_text` line, eidos reads `ui_text.<lang>.txt` at the mod's root. The
lines of the language's file come after the plain file's, so they win.

Every text the one-screen interface draws goes through these words: the
game's own labels, and the ones a mod adds (a pop-up's title and text, a
`show` row, the Tracker). A code mod that shows `popup("Run over", ...)` can
have `Run over = Partie finie` in `ui_text.fr.txt`.

Code: `sdk/mod_strings.c` (the files, the lookup, the values),
`sdk/mod_manifest.c` (the manifest lines), `sdk/mod_menu.c` (the settings),
`sdk/mod_rule.c` (`@key`), `sdk/mod_loader.c` (`ui_text.<lang>.txt`, the
services).
