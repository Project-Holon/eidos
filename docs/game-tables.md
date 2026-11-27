# Game tables a mod extends

Some game data lives in hardcoded tables in the binary, sized for the
original game with one row per ID. Mods add or override rows using text
files in their `eidos/tables/` directory. eidos merges base and mod rows
into a copy read by the game (`sdk/bw_limits.c`). Later mods override
earlier rows for the same ID. Invalid lines are logged and skipped.

Other files in the same directory: `trainer_mons.txt` ([trainer-mons.md](trainer-mons.md))
and `ability_handlers.txt`, `move_handlers.txt`, `item_handlers.txt`
([battle-handlers.md](battle-handlers.md)).

## Text lines

A mod adds or changes game text line by line with `a/0/0/2/<bank>.msg`
files (`<line> = <text>`, format in `sdk/vfs_msg.h`), without shipping the
whole bank. Later mods win per line.

    a/0/0/2/203.msg       572 = Crafty Shield
    a/0/0/2/203.fr.msg    572 = Vigilance

`<bank>.<lang>.msg` (`en`, `fr`, `de`, `it`, `es`) holds lines for that
language's game only. It applies after the mod's `<bank>.msg`, so a line it
lacks keeps the plain file's text. eidos runs the English, French, German,
Italian and Spanish games, which share the same banks and line numbers.

Some lines are whole sentences with a name inside: a move's "used" lines
(bank 13, three per move at `3 × move`: the player's, a wild and a foe's
Pokémon), its name in capitals (bank 286), and an item's article and plural
lines (banks 279 and 280; the article line is empty in every language but
English). The manifest's `lang` (`en`, `fr`, `de`, `it` or `es`; English
when omitted) says which language a mod's plain files are written in. In a
game of another language, eidos makes these lines for the moves and items a
mod adds (moves from 560, items from 627) that have no line in the game's
language: it takes the game's own line of a source entry and swaps in the
mod's name in the game's language (bank 203 or 54, the `.<lang>.msg` line
first). A move's source is the move it works like in `move_handlers.txt`,
else Pound. An item's source is TM95 for a TM, the Flame Plate for a plate,
the Douse Drive for a drive. `eidos/tables/text_from.txt` names a source:

    move.572 = 469        # Crafty Shield's lines from Quick Guard's
    item.630 = 17         # item 630's lines from the Potion's

## item_icons.txt

Maps each item's bag icon to its tile member and palette member in a/0/2/5.

    <item id> = <tile member> <palette member>

Items beyond the base game's 627 require raising `limit.items` in the
manifest.

## icon_palette.txt

Sets the palette for a Pokemon's party and box icon using one of the three
icon palettes in a/0/0/7.

    <icon index> = <palette> [<female palette>]

    130 = 2          # Gyarados's icon in palette 2
    593 = 0 1        # palette 0, and palette 1 for the female icon

The icon index is the same n used for the icon's members in a/0/0/7
(`7 + 2n` and `8 + 2n`): the species number, 650 and 651 for eggs, and
alternate forms starting at 652 in the game's internal order. When
omitted, the female palette defaults to the primary palette. The base game
uses palettes 0 to 2 (values 0 to 15 are accepted). Rows beyond the base
game's 712 are preserved for icons added by a later species limit.

## machine_moves.txt

Sets the move each TM and HM teaches.

    TM35 = 53        # TM35 teaches Flamethrower
    HM03 = 57
    TM96 = 605       # a TM a mod adds

Keys are `TM01` to `TM95`, `HM01` to `HM06`, or custom TMs `TM96` to `TM122`
(see `machine_items.txt`). Values are move IDs up to `limit.moves`. The Bag
and move-teaching screens show the assigned move. Item descriptions are
separate: update the TM's description in the item text when changing its
move. TM and HM compatibility is set in each species' data.

## machine_items.txt

Maps each custom TM to an item ID.

    TM96 = 627

Keys are `TM96` to `TM122`. Values are custom item IDs (627 up to
`limit.items`) assigned to the TM pocket in the item data, like base TMs.
The taught move comes from `machine_moves.txt`. A species can learn the TM
when bit `TM number + 5` of its machine compatibility bitfield is set
(`TM96` is bit 101, `TM122` is bit 127). The Bag lists custom TMs by number
after base TMs and before HMs, and both the Bag and shops display their TM
number. Custom TMs are never HMs. While a mod adds TMs, the TM pocket
holds all 27 alongside the 101 base TMs and HMs. The save's own pocket
keeps only the base machines. eidos stores each custom TM the player has
in a file of the mod that adds it, beside the save
(`saves/mods/<mod folder>.bag/`), written with each game save. With the
mod off, the save's bag shows only the base machines.

## shops.txt

Sets shop inventories and Battle Subway BP prize lists.

    mart.5 = 1 2 3 50          # the Poke Mart with eight badges
    bp.1 = 50:48 213:32        # a BP prize list, each item with its price

A `mart` list is one of the game's 26 shop lists (0 to 25). Each item sells
at its price from the item data, minus any Pass Power discount. The Poke
Mart main clerk selects lists 0 to 5 by badge count: list 0 at 0 badges,
list 1 at 1 or 2, list 2 at 3 or 4, list 3 at 5 or 6, list 4 at 7, and
list 5 at 8. Lists 6 to 25 cover other clerks, such as second Mart counters
and department store floors. A `bp` list is one of the 3 Battle Subway prize
lists and sets each item's price in BP. Each list holds up to 40 items.
Omitted lists keep their base-game inventories.

## item_heal.txt

Sets how much HP an item heals past what its item record can hold.

    600 = hp=500                 # a bag item that heals 500 HP
    601 = hp=1/3                 # a third of the Pokemon's max HP
    602 = hp=full
    155 = held_hp=300            # Oran Berry, held, heals 300 HP

The item record holds the HP an item restores in one byte: 1 to 252 HP, with
253, 254 and 255 for a quarter, a half and all of the Pokemon's HP. A held
berry's fixed heal is its hold effect parameter, 1 to 255. `hp` (1 to 65535,
`1/2` to `1/16` of max HP, or `full`) replaces the record's amount when the
item is used from the Bag in battle or on a party member, and when it
revives. The item still needs its HP-restore or revive flag in the record.
`held_hp` (1 to 65535) replaces the amount of a held item that heals a fixed
amount when eaten (the Oran Berry's handler, or an item that works like it in
`item_handlers.txt`). An item never heals more than the HP the Pokemon is
missing. Omitted items keep their record's amounts.

## move_anims.txt

Maps a move to a base-game move's battle animation.

    53 = 85          # Flamethrower plays Thunderbolt's animation
    600 = 236        # a new move (Moonblast) plays Moonlight's

The value is a move ID with an animation script: a base-game move (1 to 559) or a move whose script a mod provides. Mods can supply a battle animation script for any move, including custom moves, at `a/0/6/6/<move id>` (in the game's raw battle effect script format). A move's animation resolves in order: its `move_anims.txt` entry, its own script (base-game or mod-supplied), the script of the base move in `move_handlers.txt`, and finally Pound. Scripts reference shared battle effects (a/0/6/7) as `0x231 + n`, preserving the base game's numbering. Custom sound effects added to the sound archive play on the player specified by their sequence.

To preview an animation, run eidos with `--preview-move <move id>` (and
optionally `--preview-species <attacker> <defender>`, defaulting to
Pikachu attacking Patrat). It loads a copy of the profile's save without
modifying the original, opens a battle, and loops the animation until
the window closes. No turns execute and no damage is dealt. An unknown
move ID exits with code 2 and prints an error to stderr.

## Testing a mod on its own saves

`eidos --test-saves <folder>` runs the game with saves, per-save mod files, and settings in `<folder>` instead of the profile. The first run copies the profile's save (the one `--preview-move` uses), its per-save mod files, and settings into `<folder>`, or starts a new game if the profile has no save. Later runs continue the save in `<folder>`, carrying one playthrough across mod edits. Profile saves are never modified.

## New species

Mods add species past 649 by raising `limit.species` (up to 2047) in the
manifest. Species IDs 650 to 714 are unused because the base game's
archives store eggs and alternate forms at those positions. An ID from
715 onward counts as a species once a mod provides its stats record
(a/0/1/6 member). Each alternate form of an added species uses the stats
slot `formStatsIndex + form - 1`, also starting at 715. A form's sprites, icon, and icon palette row follow its slot (sprites at `slot × 20` in a/0/0/4, icons at `slot × 2 + 7` in a/0/0/7). A palette-only form (`formSpriteShared`) keeps its species' sprites and uses its slot's palettes (`slot × 20 + 18` and `+ 19`).

Pokedex height and weight lines (banks 244, 245, and 268 of a/0/0/2) are
indexed by Pokedex record. Base species use their species ID, and base
forms use records 651 to 726. An added species or form slot uses record
`727 + (ID - 715)`. Names, categories, and entries use the species ID.

Added archive members must use the same compression as the base game's
member in the same slot.

The save's Pokedex holds 672 species. eidos stores seen and caught flags
for later species in one file per mod beside each save
(`saves/mods/<mod folder>.dex/`), keeping the save valid for the base
game and other tools. A new game resets these files to empty.

## regional_dex.txt

Sets a species' Unova Pokedex membership and number.

    715 = unova      # in the Unova Pokedex, numbered after the last one
    716 = 157        # a number of its own
    717 = none       # not in the Unova Pokedex

`unova` assigns numbers after the highest number in the table, in species
ID order. Numbers start at 0 (Victini) and set the Unova Pokedex order.
Omitted base-game species keep their original numbers. Added species are
excluded from the Unova Pokedex until assigned a number or `unova`.

## pokedex.txt

Sets Pokedex search fields stored outside species data.

    715 = shape 3 letter 21
    716.1 = shape 5      # form 1

`shape` sets the body shape filter (0 to 13) for a species or one of its
forms. `letter` sets the letter group of the species' name for name
search (A is 0, Z is 25). Omitted values keep their base-game defaults.
For an added species, an omitted `letter` defaults to the first letter of
its name, and an omitted `shape` matches no shape. An added form without
a `shape` inherits its species' shape.

## cries.txt

Sets the cry for a species or form.

    715 = 25         # Pikachu's cry
    716.1 = 134      # form 1 plays Vaporeon's cry

The value is a wave archive ID in the sound archive: a base-game species' cry (its species ID) or one added by a mod. Omitted forms inherit their species' cry. Omitted base-game species keep their own cries. Omitted added species play no cry.

## type_chart.txt

Sets type effectiveness for each attacking type against each defending type.

    12 = 8                           # Electric hits Normal for double
    9 = 4 4 4 4 4 2 8 4 8 2 2 8 4 4 8 2 4    # Fire's base-game line in full

The ID is the attacking type (0 Normal to 16 Dark, in game order: Normal, Fighting, Flying, Poison, Ground, Rock, Bug, Ghost, Steel, Fire, Water, Grass, Electric, Psychic, Ice, Dragon, Dark). Values are effectiveness codes against each defending type in the same order: 0 (no effect), 2 (not very effective), 4 (normal), 8 (super effective). A shorter line changes only the defending types it reaches. Omitted types keep their base-game values. Later mods override earlier ones per value. With added types (`limit.types`, below), lines and values run up to the last type, and every pair with an added type defaults to 4.

## New types

Mods add types past the base game's 17 by raising `limit.types` (up to 30) in the manifest (`limit.types = 17` adds type 17). Type 31 is reserved internally for "no type", so 30 is the maximum. Each added type requires a name at line `<type>` of text bank 199 (`a/0/0/2/199.msg`), matchups in `type_chart.txt`, and graphics and colors in `types.txt`. Added types appear in battles, the Bag's TM pocket, the Summary screen, the PC Box, the Pokédex (entry pages, registration, and search), the battle party list, and the Move Reminder.

## types.txt

Defines each added type's graphics and colors.

    17 = name fairy icon 172 colors F6A5D5 9C4A7B label 113 button F6A4D5 D594BD BD7BA4 9C6A8B 835273 624152 hof 47 17 color F6A5D5

- `icon`: Member of a/0/8/3 (32x16 4bpp RGCN, matching member 0x22). Uses palette indices 0 (transparent), 1 and 2 (from `colors`), 14 (outline), and 15 (white text).
- `colors`: One or two RRGGBB colors for icon palette indices 1 and 2.
- `label`: Member of a/1/5/9 containing the six Pokédex labels for other languages (Japanese, French, German, Italian, Spanish, Korean), 32x16 each, using the icon's palette. Defaults to `icon`.
- `button`: Six RRGGBB shades for the battle move button. Defaults to Normal's button.
- `hof`: Hall of Fame particle and palette members in a/2/1/6. Defaults to Normal's.
- `color`: RRGGBB color for the one-screen interface. Defaults to the first `colors` entry.
- `name`: A lowercase word (`a` to `z`, `0` to `9`, `_`) that battle handler tables use for the type (`type=fairy`, see `docs/battle-handlers.md`). The type's displayed name comes from text bank 199.
- `plate`: The item that is this type's plate. Arceus with Multitype holding it takes the type, Judgment becomes the type, and giving it to Arceus in the party or the PC Box sets Arceus's form to the type number (form 17 for type 17). Make the item boost the type's moves like a plate with an `item_handlers.txt` line (`628 = 298 type=fairy`).
- `drive`: The item that is this type's drive, and the Genesect form it gives (5 or more; 1 to 4 are the base drives): `drive 629 5`. Techno Blast becomes the type and plays the plain animation.

Arceus and Genesect show an added form only when the mod adds it. Without one they keep their base form's look and stats, and their type still follows the plate. The base game stores every species' alternate forms in one block, so a mod adds a form to Arceus or Genesect by moving the species' forms to its own stats slots: set `formStatsIndex` and `formSpriteIndex` in the species' stats record to a slot from 715, raise its form count, and provide every form's stats record and pictures at those slots (see New species below).

A type without an entry or a valid icon falls back to Normal's icon. A later mod's entry for a type overrides an earlier one. Each screen allocates colors for the added types it displays, so up to six added types can appear on one screen at a time.
