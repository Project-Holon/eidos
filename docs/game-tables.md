# Game tables a mod extends

Some game data lives in hardcoded tables in the binary, sized for the
original game with one row per ID. Mods add or override rows using text
files in their `eidos/tables/` directory. eidos merges base and mod rows
into a copy read by the game (`sdk/bw_limits.c`). Later mods override
earlier rows for the same ID. Invalid lines are logged and skipped.

Other files in the same directory: `trainer_mons.txt` ([trainer-mons.md](trainer-mons.md))
and `ability_handlers.txt`, `move_handlers.txt`, `item_handlers.txt`
([battle-handlers.md](battle-handlers.md)).

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

The machine is `TM01` to `TM95` or `HM01` to `HM06`, and the value is a
move ID up to `limit.moves`. The Bag and move-teaching screens show the
new move. The item's description text is separate: update the TM's
description in the item text when changing its move. TM and HM
compatibility is set in each species' data.

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

The ID is the attacking type (0 Normal to 16 Dark, in game order: Normal, Fighting, Flying, Poison, Ground, Rock, Bug, Ghost, Steel, Fire, Water, Grass, Electric, Psychic, Ice, Dragon, Dark). Values are effectiveness codes against each defending type in the same order: 0 (no effect), 2 (not very effective), 4 (normal), 8 (super effective). A shorter line changes only the defending types it reaches. Omitted types keep their base-game values. Later mods override earlier ones per value.
