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

The value is a base-game move ID (1 to 559). Omitted base-game moves
keep their own animations. Omitted custom moves use the animation of the
move assigned to them in `move_handlers.txt`, falling back to Pound.

To preview an animation, run eidos with `--preview-move <move id>` (and
optionally `--preview-species <attacker> <defender>`, defaulting to
Pikachu attacking Patrat). It loads a copy of the profile's save without
modifying the original, opens a battle, and loops the animation until
the window closes. No turns execute and no damage is dealt. An unknown
move ID exits with code 2 and prints an error to stderr.
