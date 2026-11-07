# The interface's words in a mod's language

The one-screen interface draws its own labels: menu entries, tabs, button
hints, the settings screen, the title prompt, move details such as PWR and
ACC. It has them in English, French, German, Italian and Spanish. A mod that
translates the game into another language can bring these labels in that
language too.

## The manifest line

```
ui_text = ui_text.txt
```

The path is relative to the mod's folder, like a table or a rule.

## The file

UTF-8 text, one label a line: the English, `=`, the translation.

```
# Lines that start with # are comments.
BAG = 背包
SAVE = 保存
ACCURACY = 命中
Close = 关闭
```

- The English must match the interface's text exactly, capitals included.
  Spaces around the `=` do not count.
- A line with nothing after the `=` keeps the English, so a file can be
  filled in a little at a time.
- `\n` is a line break, `\=` an equals sign and `\\` a backslash.
- A mod's words come before the interface's own languages, whatever the
  game's language. When two mods have the same label, the one loaded later
  wins.
- The words go when the mod is turned off.

## Labels with values in them

Some labels are built from a number or a name: `PWR %d  ACC %d`,
`%s nature.`, `Met at %s, Lv. %u.`, `Last saved %u/%u/20%02u  %02u:%02u`.
Translate the template, with its `%` codes, and the values go into your
words:

```
PWR %d  ACC %d = 威力 %d  命中 %d
%s nature. = 性格：%s
Met at %s, Lv. %u. = 在%s相遇，当时为 Lv. %u。
```

- Keep the same codes in the same order: `%d` (a number), `%u` (a number),
  `%s` (a name or a word), `%02u` (a number with two digits). A width such
  as the `02` may change; the letter may not.
- A translation whose codes differ from the English's is not used: the
  English shows instead, so a slip shows as English, never as a wrong value
  or a crash.
- `%%` is a percent sign.
- The dump (below) lists these templates too, with their codes.

## Which labels there are

- `sdk/api/v5/ui_text_template.txt` lists the labels the interface already
  translates into its other languages, each with an empty translation.
  Copy it into the mod and fill it in.
- Some text is not in that list. To find it, run eidos with
  `EIDOS_UI_TEXT_DUMP` set to a file path. Each run adds the English of every
  label it draws that no mod translates, once each, in the same format. Play
  through the screens you want covered, then fill in the file. The dump also
  lists text from the game itself that passes through the interface, such as
  the player's name, and text built from numbers such as `No. 001`; leave
  those lines out.

## The font

The interface draws with the game's own font (`a/0/2/3`). A character the
font does not have shows as `?`. For a script the cartridge font lacks, such
as Chinese, the mod also replaces that font with one that has the characters.
eidos reads the font through the same file layer as the rest of the game, so
the mod's font is the one the interface uses.

## Length

Labels in another language can be longer or wider than the English. Most of
the interface sizes its boxes to the text, but some tabs and buttons have a
fixed width, and a long label can be cut off there. Report those with a
screenshot.
