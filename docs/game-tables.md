# Game tables a mod extends

Some game data lives in hardcoded tables in the binary, sized for the
original game with one row per ID. Mods add or override rows using text
files in their `eidos/tables/` directory. eidos merges base and mod rows
into a copy read by the game (`sdk/bw_limits.c`). Later mods override
earlier rows for the same ID. Invalid lines are logged and skipped.

Other files in the same directory: `trainer_mons.txt` ([trainer-mons.md](trainer-mons.md))
and `ability_handlers.txt`, `move_handlers.txt`, `item_handlers.txt`,
`move_kinds.txt` ([battle-handlers.md](battle-handlers.md)).

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
the Douse Drive for a drive. A move or item a mod adds with no such line at
all, in any language, gets one made the same way. `eidos/tables/text_from.txt`
names a source:

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

## balls.txt

Makes an item a Poke Ball of its own, or tunes one of the game's balls.

    <item> = <base ball item> [ball=<26..255>] [key=value ...]

    630 = 3 ball=26                          # works like the Great Ball
    631 = 6 ball=27 ratio=4 type=fire type.2=grass
    632 = 10 ball=28 per_turn=0.5 ratio=5    # a faster Timer Ball
    633 = 3 ball=29 particles=733,734,735,736,737,738 sprite=165,166 icon=172,173
    634 = 14 ball=30 friendship=200          # heals, and starts at 200 friendship
    3 = 3 ratio=1.75                         # the game's Great Ball, tuned

The base is one of the game's 25 ball items (1 to 16, 492 to 499, 576): the
ball the item works like. An added item needs `ball`, the ball id its caught
Pokemon keep (the game's balls are 1 to 25; the box record holds one byte).
The Pokemon keeps the id in a trade, the box and the day care. A line for a
game ball has no `ball`. The item also needs its record (a/0/2/4) with the
Poke Balls pocket, as a copy of the base's has, and `limit.items` when it is
past the game's items.

The battle Bag puts each item in its pockets by a table with one byte per
item (a/2/3/2 member 0), the same bits as the record's battle pockets. The
game's table ends at its last item, so eidos makes it as long as
`limit.items` and takes the bytes of every item past its end from that
item's record. This applies to any item a mod adds, not only balls. A mod
that ships its own table keeps it, and only the items past its end are
added.

The catch keys, by base:

| Base | Rule | Keys (the game's value) |
|---|---|---|
| 1 Master Ball | always catches, three shakes | none |
| 2 Ultra Ball, 3 Great Ball | always | `ratio` (2.0, 1.5) |
| 6 Net Ball | on a Pokemon of `type` or `type.2` | `ratio` (3.0), `type` (water), `type.2` (bug) |
| 7 Dive Ball | a fishing encounter, or the battle's ground attribute 6 | `ratio` (3.5) |
| 8 Nest Ball | below `level`: (41 - level) / 10 | `ratio`, the most (4.0), `level` (30) |
| 9 Repeat Ball | a species the Pokedex has caught | `ratio` (3.0) |
| 10 Timer Ball | 1 + `per_turn` each turn | `ratio`, the most (4.0), `per_turn` (0.3) |
| 13 Dusk Ball | on battle backgrounds 4 and 5 at any time, or at night (time zones 3 and 4) on the backgrounds the game lists for it | `ratio` (3.5) |
| 15 Quick Ball | while the turn count is below `turns` | `ratio` (5.0), `turns` (1) |
| any other | always | `ratio` (1.0) |

`ratio` and `per_turn` are 0.1 to 255 (`per_turn` from 0). `type` takes a
type name, an added type's `name` from `types.txt`, or a type number.
`level` is 1 to 101 and `turns` 1 to 255. A key the base has no place for is
refused.

The keys every ball takes:

| Key | Values | What it does (the default) |
|---|---|---|
| `heal` | 0 or 1 | full HP and no status on the catch (1 for the Heal Ball, 0 for others) |
| `friendship` | 0 to 255 | the caught Pokemon's friendship (no change). The game has no such ball: eidos's own |
| `friendship_bonus` | 0 to 10 | added to each friendship gain (1 for the Luxury Ball, 0 for others) |
| `particles` | six a/0/0/6 members | the throw, opening and send-out particles, in key order: 3, 0x1D, 0x2E, 0x2F, 0x30, 0x31 |
| `sprite` | two a/0/9/5 members | the 2D ball that shakes after the throw: character file, palette |
| `icon` | two a/0/8/3 members | the ball in the summary and the trade screen: palette, character file |

A file key a line does not give is the base's file. The base's files, by
its ball id b (1 to 25):

- a/0/0/6: 3 + (b - 1); 0x1D + (b - 1), except balls 16 to 24, which use
  the Poke Ball's 0x20, and ball 25, which uses 0x2D; 0x2E + 4 × (b - 1) and
  the next three.
- a/0/9/5: character 0x81 + 2 × s and palette 0x82 + 2 × s, where s is b,
  or 17 for ball 25 (17 slots). The cell 0x81 and animation 0x82 are shared.
- a/0/8/3: palette 0x56 + b and character file 0x6F + b.

A new ball's own files are copies of these, as new members past each
archive's end. The game picks no sound by ball, and Black and White have no
ball seals.

A later mod's line for the same item replaces an earlier one. Two items
that take the same `ball` id: the later line keeps it. A line whose base is
not a game ball, or an added item with no `ball`, is said in the log and
left out. A Pokemon in a ball id that no line gives (its mod is off) shows
the Poke Ball's files and has no ball effects.

## move_anims.txt

Maps a move to a base-game move's battle animation.

    53 = 85          # Flamethrower plays Thunderbolt's animation
    600 = 236        # a new move (Moonblast) plays Moonlight's

The value is a move ID with an animation script: a base-game move (1 to 559) or a move whose script a mod provides. Mods can supply a battle animation script for any move, including custom moves, at `a/0/6/6/<move id>` (in the game's raw battle effect script format), for every move up to `limit.moves`. A move's animation resolves in order: its `move_anims.txt` entry, its own script (base-game or mod-supplied), the script of the base move in `move_handlers.txt`, and finally Pound. Scripts reference shared battle effects (a/0/6/7) as `0x231 + n`, preserving the base game's numbering. Custom sound effects added to the sound archive play on the player specified by their sequence.

A few game moves get special handling while their animation plays: Transform, Mirror Move and Nature Power keep the HP gauges shown, and they and Future Sight and Doom Desire (on the hit turn) skip the Substitute doll swap. Baton Pass leaves the doll away, Ally Switch resets the user's sprite, and Tailwind places its particles per side. A new move gets the handling of the game move its `move_handlers.txt` line names, whatever script it plays. A new move with no base line gets the handling of the game move whose script it plays, or none for a script of its own. A game move keeps its own handling.

A move's script plays the variant its turn picks: 0, or 1 on a two-turn move's strike turn, or the value the base's handler sets (Weather Ball 0 to 4 by weather, Techno Blast 0 to 4 by the game's drives and the type number for an added type's drive: 17 on, Judgment the type number of its plate: 0 to 16, and 17 on for added types). A script with fewer variants plays variant 0, so the game's Judgment script (449, one variant) plays the same look for every type, and a mod's 449 with a variant per type gives each type its own (variant 17 is type 17).

Mods add members past the game's counts to every archive an animation reads: move scripts (`a/0/6/6`, 601), shared effects (`a/0/6/7`, 98), particle files (`a/0/0/6`, 733; `.spa`), effect backgrounds and cell actors (`a/0/9/5`, 165), and sounds (`wb_sound_data.sdat/sdat.txt`). A particle file gets a work area of its own size (the game's files use 0x4800 bytes) and may hold any number of textures (the game's hold up to 64). A script may hold up to 64 particle files at once (the game's 16) and cell actors in slots 0 to 5 (the game's 0 to 3; the battle's OBJ palette has room for six). Texture memory stays the DS's (128 KB of textures, 16 KB of palettes for the battle), shared by every loaded file. A sound effect may be larger than its player's heap (10200 bytes for players 1, 2, 3 and 5, 2000 for player 4): the player's heap grows to fit the largest mod sound on it, and the sound heap grows with it, so the game's own sounds and music keep their room.

### Effect script commands of eidos

Scripts can use these commands past the game's 78 (0 to 77):

| Id | Name | Arguments | What it does |
|---|---|---|---|
| 78 | PlaySoundByName | name, slot, pan, delay, pitch, volume, modDepth, modSpeed | Plays the sound sequence named `<mod>:<seq>` (a `seq` of a mod's `sdat.txt`). `<mod>` is the mod's `name` (else its folder's name) with every character other than a letter, a digit, `_`, `-` and `.` changed to `_`: a mod named `Fairy Type` has `Fairy_Type:zap`, and `Fairy Type:zap` finds it too. When two mods give the same key, the later one in load order uses `<key>_2`. `name` is the byte offset of the NUL-terminated name from the end of the command, like a jump offset. The other arguments are PlaySoundEffect's (52), without its last unused word. An unknown name plays nothing. |
| 79 | ParticleColor | file, source, value | Colors every emitter of the loaded particle file `file` (a key, as LoadParticles takes it), including emitters it starts later, until the file is unloaded or the script ends. The color multiplies each particle's own colors (header color, color animation, children), as the emitter color does in the game, where it is always white: white and grey particles take the color, and colored ones darken toward it. `source` 0: `value` is the color (BGR555, `7FFF` white removes the tint). 1: the color of the type numbered by the script's variant (Judgment's variant is its type, and so is Techno Blast's from 17 on; its variants 1 to 4 are the game's drives, so give those source 2 with the drive's type). 2: the color of type `value`. 3: the color of the type of the move whose animation plays (its move data type). |

A type's color is its `types.txt` `color`. Without one, a base-game type's color is the base shade of its battle move button (color 8 of palette row 0 of its `a/0/1/1` member, read at run time, so a mod's button palette changes it too): Normal `6B5A`, Fighting `0978`, Flying `7E2F`, Poison `6113`, Ground `0E37`, Rock `29F2`, Bug `0E93`, Ghost `494B`, Steel `6AFA`, Fire `14DF`, Water `7DE4`, Grass `12C5`, Electric `171F`, Psychic `351F`, Ice `7F09`, Dragon `7D2B`, Dark `252E` (BGR555). An added type without `color` uses its first `colors` entry.

A mod's sound numbers depend on the mods loaded, so scripts name a mod's sounds. Game sounds keep their numbers: play them with PlaySoundEffect (52). A sound effect loads its bank, wave archives and sequence into its player's memory: 10200 bytes for players 1, 2, 3 and 5, 2000 for player 4. A larger one does not play.

To preview an animation, run eidos with `--preview-move <move id>` (and
optionally `--preview-species <attacker> <defender>`, defaulting to
Pikachu attacking Patrat). It loads a copy of the profile's save without
modifying the original, opens a battle, and loops the animation until
the window closes. No turns execute and no damage is dealt. An unknown
move ID exits with code 2 and prints an error to stderr.

## particles.txt

Sets how many emitters and particles a battle particle file (a member of `a/0/0/6`) can have live at once.

    733 = emitters 48 particles 4000
    734 = particles 600          # emitters stay 24

A file's particle system holds 24 live emitters and 200 live particles (the base game's counts for every file). All emitters of the file share the particle pool with their children. When a pool is full, an emitter start does nothing and a new particle is not made. `emitters` is 1 to 1000 and `particles` is 1 to 10000. A value below the base game's count uses the base game's count. A file with no line keeps 24 and 200. A later mod's line for the same file replaces an earlier one.

## Testing a mod on its own saves

`eidos --test-saves <folder>` runs the game with saves, per-save mod files, and settings in `<folder>` instead of the profile. The first run copies the profile's save (the one `--preview-move` uses), its per-save mod files, and settings into `<folder>`, or starts a new game if the profile has no save. Later runs continue the save in `<folder>`, carrying one playthrough across mod edits. Profile saves are never modified.

## New species

Mods add species past 649 by raising `limit.species` (up to 2047) in the
manifest. Species IDs 650 to 714 are unused because the base game's
archives store eggs and alternate forms at those positions. An ID from
715 onward counts as a species once a mod provides its stats record
(a/0/1/6 member), unless it is a form's stats slot. Each alternate form of an added species uses the stats
slot `formStatsIndex + form - 1`, also starting at 715. A form's sprites, icon, and icon palette row follow its slot (sprites at `slot × 20` in a/0/0/4, icons at `slot × 2 + 7` in a/0/0/7). A palette-only form (`formSpriteShared`) keeps its species' sprites and uses its slot's palettes (`slot × 20 + 18` and `+ 19`). A form's stats record keeps its species' form count (`formCount`), as the base game's form records do, and sets `formStatsIndex` and `formSpriteIndex` to 0. The Pokedex page shows the first form in place of a form its own record does not count.

Pokedex form names, heights, and weights (banks 244, 245, and 268 of
a/0/0/2) are indexed by Pokedex record. Base species use their species ID,
and base forms use records 651 to 726. An added species or form slot uses
record `727 + (ID - 715)`. This includes the forms of a base species that a
mod moves to its own slots. Species names, categories, and entries use the
species ID.

The Pokedex lists every form of a species whose forms are at a mod's slots
(`formStatsIndex` from 715), up to 64 forms. The game's form page holds
64 entries, one for each form in each color seen, so a species with more
than 32 forms seen in both colors lists its first 64 entries. Seen flags
of forms past 64 are still kept. Each form shows its own
pictures, types (its stats record), height, weight, and name (the bank 244
line of its record). Other base species keep the base game's form list:
Arceus and Genesect show one form. A form is marked as seen when the player
sees or catches a Pokemon in that form, or gives Arceus a plate or Genesect
a drive in the party or the PC Box.

Added archive members must use the same compression as the base game's
member in the same slot.

The save's Pokedex holds 672 species and the forms of the base game's form
list. eidos stores seen and caught flags for later species, and the seen
and shown flags of forms the save has no room for, in one file per mod
beside each save (`saves/mods/<mod folder>.dex/`), keeping the save valid
for the base game and other tools. A form's flags go to the file of the
mod that ships its species' stats record. A new game resets these files to
empty.

A Pokemon's record keeps forms 0 to 31. A Pokemon in form 32 or higher (up
to 255) keeps form 0 in its record, and eidos keeps the real form in the
same file, keyed by the Pokemon's personality value and trainer ID. Every
screen, the battle, and the Pokedex see the real form. With the mod off,
the Pokemon is in form 0. A Pokemon traded to another game and a battle
video keep form 0.

Hall of Fame entries and Musical photos do not hold forms 32 and higher
yet: such a member can show the wrong form there.

## wild_forms.txt

Sets the form of a wild encounter slot.

    77.grass.3 = 40                  # table 77, grass slot 3: form 40
    77.surf.* = 33                   # every surf slot of table 77
    12.fishing.0 = 2 season=winter   # in winter only

A slot of an encounter table (a/1/2/6, one member per table) keeps the form
in 5 bits, and 31 there means a random form, so the table itself names forms
0 to 30 only. A line gives a slot any form from 0 to 255. The key is the
table (the a/1/2/6 member a zone names), the slot group (`grass`,
`doubleGrass`, `shakingGrass`, `surf`, `surfRipple`, `fishing`,
`fishingRipple`), and the slot (0 to 11 in the grass groups, 0 to 4 in the
others, or `*` for every slot of the group). `season` (`spring`, `summer`,
`autumn`, `winter`, or 0 to 3) limits the line to one season. A line for
the season wins over a line without one, whatever their order or slot form
(in winter, `77.grass.* = 5 season=winter` wins over `77.grass.3 = 40`).
`*` stands for one line per slot of the group, not a weaker default: between
two lines for the same slot and season, the later line in the file wins
(`77.grass.3 = 40` then `77.grass.* = 33` gives slot 3 form 33). The slot's species and levels stay
the table's. A form the species does not have gives form 0, as in the base
game. Magnet Pull and Static read the types of the line's form. A later
mod's line for the same slot and season wins.

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
- `color`: RRGGBB color for the one-screen interface, and the type's color for effect scripts (ParticleColor). Defaults to the first `colors` entry. A base-game type (0 to 16) may have a line with only this key (`10 = color 3890F0`): effect scripts use it in place of the type's move button shade. The one-screen interface keeps the base-game types' own colors.
- `name`: A lowercase word (`a` to `z`, `0` to `9`, `_`) that battle handler tables use for the type (`type=fairy`, see `docs/battle-handlers.md`). The type's displayed name comes from text bank 199.
- `plate`: The item that is this type's plate. Arceus with Multitype holding it takes the type, Judgment becomes the type, and giving it to Arceus in the party or the PC Box sets Arceus's form to the type number (form 17 for type 17). Make the item boost the type's moves like a plate with an `item_handlers.txt` line (`628 = 298 type=fairy`).
- `drive`: The item that is this type's drive, and the Genesect form it gives (5 or more; 1 to 4 are the base drives): `drive 629 5`. Techno Blast becomes the type and plays the variant of the type number (17 for type 17). The game's Techno Blast script (546) has variants 0 to 4, so it plays the plain look; a mod's 546 with variants up to the type number gives the drive its own look.

Arceus and Genesect show an added form only when the mod adds it. Without one they keep their base form's look and stats, and their type still follows the plate. The base game stores every species' alternate forms in one block, so a mod adds a form to Arceus or Genesect by moving the species' forms to its own stats slots: set `formStatsIndex` and `formSpriteIndex` in the species' stats record to a slot from 715, raise its form count, and provide every form's stats record and pictures at those slots (see New species below).

A type without an entry or a valid icon falls back to Normal's icon. A later mod's entry for a type overrides an earlier one. Each screen allocates colors for the added types it displays, so up to six added types can appear on one screen at a time.
