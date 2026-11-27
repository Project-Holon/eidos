# Moves, abilities and items that work like a built-in one

A mod can give a new move, ability or held item the battle effect of an
existing one, and change the numbers that effect uses: an Intimidate that
lowers Sp. Atk by 2, a Blaze for Water moves that doubles their power, a
held item that heals like Leftovers but by half. The same lines can tune a
built-in one.

Code: `sdk/battle_handlers.h`, `sdk/battle_handlers.c`, the patches
`stubs/ov/patch_ov093_*.c`, and the generator `tools/handler_params.py`.

## The tables

Three files in a mod, one line an id:

    eidos/tables/ability_handlers.txt
    eidos/tables/move_handlers.txt
    eidos/tables/item_handlers.txt

    <id> = <base id> [key=value ...]      # a comment

    165 = 22 stat=spatk stages=-2         # Super Intimidate
    22 = 22 stages=-2                     # the built-in Intimidate, tuned
    1 = 234 hp=1/2                        # item 1 held heals like Leftovers, by half

The battle runs the base id's handlers for the id. A chain of bases is
followed (`a = b`, `b = c`); a chain that comes back to itself is refused.
A later mod's line for the same id replaces an earlier one. A line that
does not read, a key the base's handlers have no place for, or a value out
of range is said in the log (`eidos: battle tables: ...`) and left out.

## Several bases on one line

An id can work like up to four built-in ones at once, each with its own
keys, joined with `+`:

    190 = 22 stat=spatk stages=-2 + 26     # Intimidate on Sp. Atk, and Levitate
    22 = 22 stages=-2 + 2                  # the built-in Intimidate, tuned, and Drizzle
    1 = 234 hp=1/2 + 161                   # Leftovers by half, and Mago Berry

A key is that base's: `stages` after `22` changes Intimidate's stat drop,
not another base's. A part that names the line's own id is the built-in
one, as on a line of one base. A part whose base has a line of several
bases itself is refused, and so is one base named twice.

Some handlers keep state in their factor's work slots (Truant remembers
the turn it loafs, Levitate whether it is in the air). `handler_params.json`
lists the slots for each id that uses them (`"work": [0, 1]`). Two bases
whose slots overlap would overwrite each other's state, so such a line is
refused (item slot 6 is the engine's own and does not count). Kenon
refuses the same lines before they reach the game.

## eidos's own move bases

Some later moves have no Black and White move to work like. eidos adds
their battle code, and a move line names it in place of a base id:

    571 = misty_terrain
    570 = flower_shield type=grass stat=def stages=1
    568 = geomancy

| Name | What it does | Move record |
|---|---|---|
| `misty_terrain` | For five turns, grounded Pokemon get no major status, confusion or Yawn, Rest fails for them, and Dragon moves do half damage to them. Fails while it is up. | category 10, target 10 (as Gravity) |
| `flower_shield` | Raises the stat of every Pokemon of the type on the field (`type`, `stat`, `stages`: Grass, Defense, +1). Fails when there is none. | category 10, target 10 (as Haze) |
| `crafty_shield` | For the rest of the turn, status moves other Pokemon use on the user's side fail. Moves that target every Pokemon (Perish Song) still work. Fails while it is up. | category 11, target 12, priority 3 (as Quick Guard) |
| `fairy_lock` | No Pokemon on the field can flee or switch out during the next turn (Mean Look's trap for two turn ends). Fails when every Pokemon is already trapped. | category 10, target 10 (as Haze) |
| `geomancy` | Razor Wind's charge turn with its own line. The stat changes come from the move record. Power Herb skips the charge. | category 2, target 7, the charge flag, its stat changes |
| `fell_stinger` | When the move knocks a target out, the user's stat rises (`stat`, `stages`: Attack, +3; `stages=2` for Generation VI). | a damaging move (as Bug Bite) |
| `needs_status` | The move fails on a target without the status (`status`: poison, Venom Drench). The move record gives what it does to one that has it. | as Venom Drench: category 2, target 5, its stat changes |
| `spiky_shield` | Protect. A contact move it stops costs the attacker HP (`hp`: 1/8), with Rough Skin's line. | a copy of Protect's |
| `kings_shield` | Protect from damaging moves only: status moves pass. A contact move it stops lowers the attacker's stat (`stat`, `stages`: Attack, -2; `stages=-1` for Generation VIII). | a copy of Protect's |
| `baneful_bunker` | Protect. A contact move it stops gives the attacker the status (`status`: poison). | a copy of Protect's |
| `contact_guard` | Protect, and a contact move it stops does to the attacker what its keys say: `hp`, `stat` and `stages`, `status` (any of them, or none). | a copy of Protect's |
| `super_effective` | The move is super effective against the type (`type`, no default), whatever the type chart says. No handlers of its own: give it as a part (`600 = 58 + super_effective type=water`). | the move's own |
| `freeze_dry` | `super_effective` against Water (Freeze-Dry). | as Ice Beam, power 70 |
| `second_type` | The move's effectiveness is that of its type and of the second type (`type`, no default) together. Same-type power stays its own type's. | the move's own |
| `flying_press` | `second_type` Flying (Flying Press: Fighting and Flying). | a Fighting contact move |
| `add_type` | Each target gets the type (`type`, no default) as a third type, until it leaves the field or its type changes (Soak, Conversion). Fails on a target that has the type. | as Soak |
| `trick_or_treat` | `add_type` Ghost. | as Soak, Ghost |
| `forests_curse` | `add_type` Grass. | as Soak, Grass |
| `grassy_terrain` | For five turns (`turns`), grounded Pokemon heal 1/16 of their HP (`hp`) at each turn end, their Grass moves have 1.5 times the power (`ratio`; 1.3 for Generation VIII), and Earthquake, Magnitude and Bulldoze do half damage to them. | as Gravity |
| `electric_terrain` | For five turns, grounded Pokemon cannot fall asleep or get drowsy (Rest fails), and their Electric moves have more power (`ratio`). | as Gravity |
| `psychic_terrain` | For five turns, grounded Pokemon are safe from other sides' moves of raised priority (the move record's, or an ability's: Prankster), and their Psychic moves have more power (`ratio`). | as Gravity |
| `hits_floating` | A Ground move hits Flying and floating Pokemon (Levitate, Magnet Rise, Air Balloon) as neutral. Thousand Arrows: `479 + hits_floating` (Smack Down's handlers ground the target). | the move's own |
| `parting_shot` | The target's two stats fall (`stat`, `stages`, `stat.2`, `stages.2`: Attack and Sp. Atk, -1), then the user switches out as after U-turn. | as Soak |
| `topsy_turvy` | Each target's stat stages turn the other way. Fails when none has a stage changed. | as Soak |
| `needs_berry` | The move fails unless the user has eaten a berry since it came in. Before that it cannot be chosen (choosing it shows why) and the trainer AI leaves it out. | the move's own |
| `belch` | `needs_berry` (Belch). | Poison, special, power 120 |
| `burn_up` | Fails unless the user has the type (`type`: Fire); after the damage the user loses it. A Pokemon of that type alone is left with no type: every move hits it for neutral damage. | Fire, special, power 130 |
| `shore_up` | Heals half the HP (`ratio`), two thirds in a sandstorm (`ratio.2`). | as Recover |
| `strength_sap` | The user heals as much HP as the target's Attack (its stages counted), then the target's stat falls (`stat`, `stages`: Attack, -1). Fails when the target's Attack is at its lowest. | as Soak |
| `rototiller` | Every grounded Pokemon of the type (`type`: Grass) on the field has two stats raised (`stat`, `stages`, `stat.2`, `stages.2`: Attack and Sp. Atk, +1). Fails when there is none. | as Gravity |
| `magnetic_flux` | Every Pokemon of the user's side with Plus or Minus has two stats raised (Defense and Sp. Def, +1). Fails when there is none. | a copy of Quick Guard's |
| `set_type` | Each target becomes the type alone (`type`: Water; Soak with a type of your choice). | as Soak |
| `electrify` | The target's moves for the rest of the turn are of the type (`type`: Electric). | as Soak |
| `ion_deluge` | For the rest of the turn, moves of `type.2` (Normal) are of `type` (Electric). Fails while it is up. | as Gravity, priority 1 |
| `sticky_web` | A web on the foes' side: each grounded Pokemon that comes in there has its stat lowered (`stat`, `stages`: Speed, -1). Fails while it is up. Defog takes it off its target's side and Rapid Spin off its user's. | as Spikes |
| `powder` | For the rest of the turn, a move of `type` (Fire) the target uses fails and explodes, costing it an HP fraction (`hp`: 1/4). Grass Pokemon are not covered. | as Soak, Bug, priority 1 |
| `throat_chop` | Each target hit cannot use sound moves for `turns` turns (2: this one and the next): they cannot be chosen and fail when called. | Dark, physical, power 80 |
| `laser_focus` | The user's moves are critical hits for the rest of the turn and the next, unless the target is safe from them (Battle Armor, Lucky Chant). | as Focus Energy |
| `stomping_tantrum` | `ratio` (2.0) times the power when the user's last move failed (missed, had no effect or did nothing). | Ground, physical, power 75 |
| `spectral_thief` | Before the damage, the user takes the target's raised stat stages for its own (up to +6), and the target's go back to neutral. | Ghost, physical, power 90 |
| `pollen_puff` | Aimed at an ally, the move heals it an HP fraction (`hp`: 1/2) in place of the damage. | Bug, special, power 90 |

Many later moves need no base of eidos's own: the move record and a game
move as the base give all they do.

| Move | Base | Record |
|---|---|---|
| Nuzzle | none | power 20, paralysis at 100 |
| Play Nice, Noble Roar, Baby-Doll Eyes | none | the stat changes (and priority 1 for Baby-Doll Eyes) |
| Draining Kiss | 202 Giga Drain | drain 75 |
| Psychic Fangs | 280 Brick Break | Psychic, power 85 |
| First Impression | 252 Fake Out | no flinch |
| Infestation | 20 Bind | Bug, power 20 (the bound Pokemon's lines are Bind's) |
| Venom Drench | `needs_status` | Attack, Sp. Atk and Speed -1 |
| Fell Stinger | `fell_stinger` | power 50 |
| Thousand Arrows | 479 Smack Down + `hits_floating` | Ground, power 90 |

The four guards are Protect where the game tests a move by its id: they
count in Protect's streak (a second guard in a row can fail), Metronome,
Assist, Sleep Talk and Copycat leave them alone, and the trainer AI sees
Protect. Their contact effects are the game's own records, so Magic Guard,
Clear Body and the status immunities apply.

A third type counts wherever the battle asks whether a Pokemon has a type
(same-type power, weather damage, the type tests of moves, abilities and
items), in type effectiveness and in Stealth Rock's damage. Three types
(or a second type) can make a move eight times or an eighth as effective,
as in the later games: the damage is doubled or halved once more past
four times or a quarter, and the line is "super effective" or "not very
effective". Black Sludge, Anticipation and the trainer AI's type tests
count the third type; Reflect Type copies it, and Camouflage changes a
Pokemon that has one. The type checks of statuses (Fire against a burn,
Grass against Leech Seed) and the AI's switching choices still see the
two types.

These names are for move lines; ability lines have names of their own
(below). A name can be one part of a line of several bases. The code is in `stubs/ov/patch_ov093_hand_waza.c`: Misty Terrain
and Crafty Shield add a field and a side event factor of their own and
end them in the turn check, so the game's field and side effect tables
keep their size.

Their battle lines are added when a mod's line names the base, in that
mod's text (`a/0/0/2`). A mod that ships its own line at the same bank and
index keeps it, so a mod can reword them or translate them:

| Base | Bank 15 (one line) | Bank 14 (set of three: yours, wild, foe's) |
|---|---|---|
| `misty_terrain` | 400 start, 401 end | 2003 status blocked |
| `crafty_shield` |  | 2006 start, 2009 move blocked |
| `fairy_lock` | 402 |  |
| `geomancy` |  | 2000 charge turn |
| `grassy_terrain` | 403 start, 404 end |  |
| `electric_terrain` | 405 start, 406 end | 2012 sleep blocked |
| `psychic_terrain` | 407 start, 408 end | 2015 move blocked |
| `topsy_turvy` |  | 2018 stages reversed |
| `burn_up` |  | 2021 type lost |
| `ion_deluge` | 409 |  |
| `electrify` |  | 2024 moves electrified |
| `sticky_web` | 410 laid on your side, 411 on the foe's, 412 and 413 taken away | 2027 caught |
| `add_type` (and Trick-or-Treat, Forest's Curse) |  | 2030 type added |
| `powder` | 414 exploded | 2033 covered |
| `throat_chop` |  | 2036 move blocked |
| `laser_focus` |  | 2039 start |
| `spectral_thief` |  | 2042 stats stolen |
| `belch`, `needs_berry` |  | 2045 cannot be chosen |

A line that names the move takes the move's own name at run time (bank
203, so the mod's `203.msg` and `203.<lang>.msg` name it). The rest of each
French, German, Italian and Spanish line is the game's own line of the same
shape: the terrains' notices and Crafty Shield's Wide Guard's and Quick
Guard's, Geomancy's charge Charge's, Throat Chop's the game's "cannot use",
the terrains' and Ion Deluge's start Gravity's (German only), Sticky Web's
Spikes' (laid: Italian; taken away: French and Italian). A language whose
line of the shape puts an article or a common noun in place of the move's
bare name gets no line, and the move shows the game's own lines only (a
stat change; Belch that cannot be chosen "But it failed!"): the terrains'
end lines, Fairy Lock,
Topsy-Turvy, Burn Up, Electrify, Sticky Web's catch, Powder, Laser Focus,
Spectral Thief and Belch are in English only, and `add_type` in English and
French (Soak's "prend le type"). English keeps the later games' wording
where the line names no move. A mod gives its own with `15.fr.msg`,
`14.de.msg` and the like. The guards, `set_type`, `strength_sap`, Pollen
Puff and Grassy Terrain's heal show the game's own lines (Protect's, Rough
Skin's, Soak's, "HP was restored").

## eidos's own ability bases

Some later abilities have no Black and White ability to work like, and
some are a game ability with other numbers. eidos adds the first kind, and
building blocks a modder sets with keys. An ability line names one in
place of a base id:

    165 = pixilate                        # Normal moves become Fairy (the added type named fairy)
    166 = strong_jaw
    167 = boost_kind kind=sound ratio=1.3 # Punk Rock's attack side
    168 = hit_type_stat type=fire type.2=water stat=speed stages=6   # Steam Engine
    169 = bulletproof + 26                # and Levitate

| Name | What it does | Keys (default) |
|---|---|---|
| `pixilate`, `aerilate`, `refrigerate`, `galvanize` | The holder's Normal moves take the type and have more power. Pixilate's type is the added type named `fairy` in `types.txt`; with none and no `type`, the moves keep their type. | `type` (fairy, flying, ice, electric), `ratio` (1.2) |
| `boost_kind` | The holder's moves of a kind have more power. | `kind` (contact), `ratio` (1.3) |
| `tough_claws`, `strong_jaw`, `mega_launcher` | The same for contact, bite and pulse moves. | `ratio` (1.3, 1.5, 1.5) |
| `boost_type` | The holder's moves of a type have more power (Steelworker, Transistor, Dragon's Maw). | `type` (steel), `ratio` (1.5) |
| `guard_kind` | Moves of a kind do less damage to the holder (Fluffy's contact half, Ice Scales with `kind=special`). | `kind` (contact), `ratio` (0.5) |
| `fur_coat` | The holder's Defense is higher against physical moves. | `ratio` (2.0) |
| `block_kind`, `bulletproof` | Moves of a kind have no effect on the holder, with its pop-up and "It doesn't affect ...". | `kind` (ball) |
| `priority_type`, `gale_wings` | The holder's moves of a type go first. With `hp`, only while its HP is at least that part of its maximum (Gale Wings: full HP). | `type` (flying), `priority` (1), `hp` (none, 1/1) |
| `entry_stat` | The holder's own stat changes when it enters (Intrepid Sword, Dauntless Shield). | `stat` (atk), `stages` (1) |
| `ko_stat` | The holder's stat changes for each Pokemon its move knocks out (Chilling Neigh, Grim Neigh). | `stat` (atk), `stages` (1) |
| `hit_type_stat` | Hit by a move of the type, the holder's stat changes (Water Compaction, Steam Engine, Thermal Exchange). | `type` (water), `type.2` (none), `stat` (def), `stages` (2) |
| `sweet_veil` | The holder and its side cannot fall asleep, get drowsy or use Rest. | none |
| `aroma_veil` | The holder and its side are safe from infatuation, Taunt, Torment, Disable, Heal Block and Encore. | none |
| `flower_veil` | Pokemon of the type on the holder's side keep their stats and get no major status from the other side. | `type` (grass) |
| `cheek_pouch` | When the holder eats its berry, it also heals part of its maximum HP. Not for a berry it eats with Bug Bite or Pluck. | `hp` (1/3) |
| `protean` | When its move goes ahead (not when it is asleep, frozen, flinching or fully paralysed), the holder takes the move's type (after Normalize and the `-ate` bases) before "X used Y!". A move that Metronome or Sleep Talk calls counts, not the caller. Not Struggle. | none |
| `magician` | Holding nothing, the holder takes the item of the first Pokemon its move hits. Not with a move Sheer Force powers. | none |
| `symbiosis` | When an ally uses up its held item (a berry, a herb, a gem; not one taken from it), the holder passes it its own, with Bestow's line. Not Mail, nor an item bound to a species. | none |
| `parental_bond` | The holder's damaging moves that hit once hit twice, the second hit with part of the power, and the game says "Hit 2 times!". Not moves that hit several targets or several times, moves with a charge turn (Fly, Solar Beam and the others Sleep Talk cannot call), nor moves of `kind` (`one_hit`: Self-Destruct, Explosion, Endeavor, Fling and Final Gambit; `move_kinds.txt` changes the list). | `ratio` (0.25; 0.5 for Generation VI), `kind` (`one_hit`) |
| `stance_change` | Before a damaging move ("X used Y!") the holder takes its attack form, before King's Shield (a move whose line names `kings_shield`) its guard form, with its pop-up and "X transformed!". It takes the guard form again when it leaves the battle. The species needs both forms (a mod's Aegislash-style species). | `form` (1, the attack form), `form.2` (0, the guard form) |
| `grass_pelt` | The holder's Defense is higher while Grassy Terrain (`grassy_terrain`) is up. | `ratio` (1.5) |

Some later abilities are a game ability with keys and need no base of
eidos's own:

| Ability | Line |
|---|---|
| Competitive | `128 stat=spatk` (Defiant on Sp. Atk) |
| Earth Eater | `11 type=ground` (Water Absorb on Ground) |
| Well-Baked Body | `157 type=fire stat=def stages=2` (Sap Sipper on Fire) |
| Steelworker | `boost_type` (its defaults) |
| Water Compaction | `hit_type_stat` (its defaults) |

The bases' lines are the game's own: the ability's pop-up and the line of
the closest game ability (Soundproof's "It doesn't affect ...", Clear
Body's, Color Change's, Pickpocket's, the stat change lines). They need no
text of their own in any language. The code is at the end of
`stubs/ov/patch_ov093_hand_tokusei.c`; each base reads its keys with
`bw_bh_arg`.

Parental Bond's hit count: the game asks the hit count only for moves
with several hits (`BTL_SERVER_FLOW_CalcMultiHitCount`); eidos asks it for
a single-hit move too when the attacker's ability is `parental_bond` (or a
line with it), and the second hit is the game's own multi-hit path.
Symbiosis needs to know an item was used up, not taken: eidos's
`BTL_SERVER_FLOW_SetHeldItem` puts the used-up item in the item-gone
event's frame (event variable 0x100, past the game's own keys).

## Move kinds

Black and White's move records flag contact, punch and sound moves. Later
games also tell bite, pulse, ball and bomb, and powder moves apart, and
moves that hit once by their nature (`one_hit`: Parental Bond does not hit
twice with them). `eidos/tables/move_kinds.txt` gives a move these kinds:

    <move id> = <kind> [<kind> ...]       # or none

    560 = bite                 # an added move that bites
    44 = none                  # Bite is not a bite move
    561 = one_hit              # an added move the user faints with

A line replaces the move's kinds; a later mod's line wins. The game's own
moves have the later games' kinds already: bite (Bite, Crunch, the three
Fangs, Poison Fang, Hyper Fang), pulse (Water Pulse, Dark Pulse, Dragon
Pulse, Aura Sphere, Heal Pulse), ball (Acid Spray, Aura Sphere, Barrage,
Bullet Seed, Egg Bomb, Electro Ball, Energy Ball, Focus Blast, Gyro Ball,
Ice Ball, Magnet Bomb, Mist Ball, Mud Bomb, Octazooka, Rock Wrecker,
Searing Shot, Seed Bomb, Shadow Ball, Sludge Bomb, Weather Ball, Zap
Cannon), powder (PoisonPowder, Stun Spore, Sleep Powder, Spore, Cotton
Spore, Rage Powder), one_hit (Self-Destruct, Explosion, Endeavor, Fling,
Final Gambit). Moves of several hits and moves with a charge turn need no
kind: their move records say so. Code: `sdk/move_kinds.c`.

## The keys

| Key | Values | Where the handler uses it |
|---|---|---|
| `stat` | `atk`, `def`, `spatk`, `spdef`, `speed`, `accuracy`, `evasion` | a stat change it commits |
| `stages` | -6 to 6 | that stat change's amount |
| `status` | `paralysis`, `sleep`, `freeze`, `burn`, `poison`, `confusion`, `infatuation` | a status it adds (with that status's own default duration) |
| `weather` | `sun`, `rain`, `hail`, `sand` | a weather it starts |
| `chance` | 0 to 100 | a percent roll |
| `hp` | `1/1` to `1/16` | an HP fraction (of the Pokemon's maximum HP) |
| `ratio` | 0.1 to 4.0 | a multiplier it applies (power, damage, accuracy, speed) |
| `type` | `normal` to `dark`, an added type's `name` from `types.txt` (`fairy`), or a type number up to `limit.types` | a type it tests (pinch abilities, absorb abilities, resist berries) |
| `turns` | 1 to 63 | how long an effect it starts lasts (Reflect 5, Tailwind 4, Uproar 3, Rest's sleep 3) |
| `damage` | 1 to 999 | a fixed damage (Dragon Rage 40, SonicBoom 20) |
| `power` | 1 to 255 | a power it sets (a Pledge combination's 150) |
| `accuracy` | 1 to 100 | an accuracy it sets (Thunder and Hurricane in sun, 50) |
| `kind` | `contact`, `punch`, `sound`, `physical`, `special`, `status`, `bite`, `pulse`, `ball`, `powder`, `one_hit` | a kind of move eidos's ability bases test (the first three are the move record's flags, the last five `move_kinds.txt`) |
| `priority` | -6 to 6 | priority an eidos ability base adds |
| `form` | 0 to 31 | a form an eidos ability base gives the holder (`stance_change`) |

`ratio` also takes the power doubling of Gust, Pursuit, Revenge, Payback,
Hex, Acrobatics, Round and the like (`ratio=1.5`: half again). `chance`
also takes the chance a handler sets for an added effect (Tri Attack's 20)
and, on Ancient Power, Silver Wind and Ominous Wind, the move record's
chance of their rise in every stat (10): `chance=100` raises them each time.

An id whose handlers use two numbers of one kind in different handlers
numbers them in its handler table's order: `ratio` and `ratio.2`, `stat.2`
and `stages.2`.

## Which ids take which keys

`sdk/handler_params.json`, shipped next to the program as
`handler_params.json` with the release's version in it (the launcher's
copy of a version: `<user folder>/versions/<version>/`), and copied by the
game at each start to `<user folder>/handler_params.json` (the version that
ran last; `%APPDATA%\eidos` on Windows), lists for every
built-in move, ability and item the keys it takes and their built-in
values, and notes for the places that cannot be changed:

    {"ability": {"22": {"keys": {"stat": "atk", "stages": -1}, "notes": []}, ...}}

eidos's own move bases are listed in `"move"` under their names
(`"misty_terrain"`), from a hand-written block in the generator.

`tools/handler_params.py` writes it, and `sdk/battle_handler_sites.inc`
(the same places for the engine), from the game itself: the handler
registries and tables in overlay 93's own bytes, and the handlers' source in
aletheia. A key is offered only where its place is sure:

- one place of that kind in the handler, counting every function of the
  overlay it calls;
- a constant value, not one worked out as the battle runs (Download,
  Moody, Anger Point);
- not in a loop (Bad Dreams, Moxie);
- before any command the handler commits whose engine work could reach
  another place of the kind (an ability pop-up and a message are display
  only and do not count).

Anything else is listed under `notes` as not adjustable, never guessed.

    python -I tools/handler_params.py <aletheia root> <extract dir> <out dir>

It reads the extract's `y9.bin` and `overlays_dec/y9_0093.bin` (the
player's own ROM, which CI does not have), so its two outputs are kept in
the repository and written again when the aletheia pin moves.

## How it works

`BTL_EVENT_DispatchToFactors` tells `sdk/battle_handlers.c` which move,
ability or item a running handler belongs to and its row in the handler
table, around each handler call (calls nest). At each place a number is
used (`BTL_SERVER_FLOW_CommitCmdRecord` for stat changes, statuses and
weather, `BTL_CALC_RollPercent`, `BTL_CALC_DivideMaxHp` and
`DivideMaxHpMin1`, `BTL_EVENT_MultiplyVar`, and the three helpers given a
type), the first place of its kind in the call takes the running id's key,
when the site table lists that place. `HAND_TOKUSEI_AddAbilityHandlers`,
`HAND_ITEM_AddFactor` and `HAND_WAZA_AddMoveHandler` look the base up in the
registry and add the factor under the id itself. With no table lines every
patched function does what the game's does.

A line of several bases is one factor whose handler table is the bases'
tables joined in line order, made once and kept for the run (`bw_bh_join`),
with the first base's priority. The game's dispatch runs only the first row
of a factor that answers an event; for a joined table it runs the first
answering row of each base instead (`bw_bh_row_end`), so Intimidate and
Drizzle both act on the switch-in. A running row is mapped back to its base
and its row in that base's table, so the base's keys and site list apply.

## What a copy does not inherit

A copy runs the base's handlers only. A move's own side effects (its stat
changes, its added status and chance, its drain, heal and recoil, hit
counts, critical stage, flinch chance, priority and flags such as sound,
punch, contact, charge and Protect) and an item's own amounts come from the
move and item records, which a mod edits there.

The game's checks of a move by its id outside the move's own handlers ask
the move's bases (`bw_bh_move_is`), so a copy counts as the move it works
like:

| Check | Where |
|---|---|
| Protect's streak (Protect, Detect, Endure, Wide Guard, Quick Guard) | `HAND_WAZA_ProtectResetStreakAfterOtherMove` |
| Moves Encore cannot repeat, Me First cannot copy, Sleep Talk, Assist, Copycat and Mimic cannot pick, the Metronome item and Pressure leave alone, gems ignore | `BTL_ID_Is*Move` (ov095) |
| Moves Metronome cannot pick | `BTL_CALC_ContainsU16` |
| Skill Swap and Substitute (the flow's own code) | `BTL_SERVER_FLOW_ProcMoveSpecialEffect` |
| Pursuit on a switch | `BTL_SERVER_FLOW_ProcPursuitBeforeSwitch` |
| The Pledge partners and combinations, Round's promotion | `ProcPledgeCombo`, `FindPendingActionByMove`, `HAND_WAZA_FindPledgeCombo` |
| Fusion Flare and Fusion Bolt's partner | `HAND_WAZA_FusionDoublePower` |
| The bind messages and animations | `HAND_WAZA_TrapOnAddSickMessage`, `BTL_SICK_ApplyBindDamage` |
| U-turn's, Circle Throw's and Freeze Shock's lines | their handlers |
| Last Resort's own slot, Sketch's and Follow Me's refusals, Power Herb and Sky Drop | their handlers |
| The trainer AI's move tests | `TR_AI_CompareMove`, `TR_AI_CmdLoadCurrentMove`, `TR_AI_CmdLoadLastUsedMove`, `TR_AI_CompareKnowsMove`, `TR_AI_CmdLoadMoveStreak` |

Struggle stays a test of its own id: the game exempts it as the move a
Pokemon falls back on with no PP, not for its effect. Echoed Voice and
Round count uses of the move itself, so a copy keeps its own count, as the
game keeps Echoed Voice's and Round's apart. Curse's target choice (a
Ghost type aims at a foe) is still Curse's id only.

The game's checks of an ability by its id outside the ability's own
handlers ask the ability's bases (`bw_bh_ability_is`). A copy counts as the
ability it works like, and a line of several bases counts as each:

| Check | Where |
|---|---|
| Abilities Role Play, Entrainment, Trace and Skill Swap cannot take; Multitype kept from Gastro Acid and from an ability change | `BTL_ID_IsAbility*Blocked`, `BTL_ID_IsMultitypeAbility` (ov095), `BTL_SERVER_FLOW_CheckSickImmunity` |
| Arceus's types from its plate with Multitype (the party, box and summary screens, and the battle) | arm9 `POKE_TOOL_GetBoxValue`, `POKE_TOOL_GetPartyValueRaw` (`sdk/poke_value.c`, the two callers of `POKE_TOOL_GetBoxValueRaw`) |
| The Ability Urge that does not run Imposter's entry again | `BTL_SERVER_FLOW_ItemEffectTriggerEntryEvent` |
| Mold Breaker's list of abilities it ignores (a line of several bases: only the listed base's rows are skipped), its entry line for Turboblaze and Teravolt | `HAND_TOKUSEI_MoldBreaker_SkipCheck`, `_ShowAbilityMessage`, `BTL_EVENT_DispatchToFactors` |
| Plus and Minus partners, Shadow Tag against Shadow Tag, Mummy against Mummy | `HAND_TOKUSEI_CheckAllyHasAbility`, `_ShadowTag_PreventEscape`, `_MummySpreadAbility` |
| Run and switch refused by Shadow Tag, Arena Trap, Magnet Pull; Run Away; Levitate against Arena Trap; Klutz | `BTL_CLIENT_FindTrapper`, `_GetEscapeBlockerForSide`, `_CanShadowTagTrap`, `_CanArenaTrapTrap`, `_IsHeldItemEffective`, `BTL_SERVER_FLOW_CanUseHeldItem` |
| No Guard, Guts against a burn, Unnerve leaving, Early Bird, Gluttony, Illusion's disguise | `BTL_SERVER_FLOW_CheckNoGuardHit`, `_CalcMoveDamage`, `_ProcPokeLeaveField`, `BTL_POKEPARAM_TickSleepTurn`, `HAND_ITEM_IsHpAtOrBelowFraction`, `BTL_SIDE_SetupClientParty`, `BTL_SLOT_UpdateIllusion` |
| Worry Seed, Rest, Simple Beam and Entrainment refused by Truant, Insomnia and Vital Spirit | their handlers |
| The trainer AI: the ability its scripts compare (the first base), its switch choices (Wonder Guard, Natural Cure, an absorbing member) | `TR_AI_GetAbility`, `TR_AI_CmdLoadCurrentAbility`, `BTL_CLIENT_ShouldSwitchVsWonderGuard`, `_FindNaturalCureSwitch`, `_FindMemberWithAbility` |
| Outside battle: the lead's encounter effects (Intimidate, Keen Eye, Static, Magnet Pull, Cute Charm, Synchronize, Compoundeyes, Suction Cups, Sticky Hold, Pressure, Hustle, Vital Spirit, Illuminate, Arena Trap, No Guard, Stench, White Smoke, Quick Feet, Sand Veil, Snow Cloak), eggs hatching sooner (Flame Body, Magma Armor), Pickup, Honey Gather, Natural Cure after a battle | `FIELD_ENCOUNT_ApplyLeadAbility`, `FIELD_EVENT_CHECK_GetEggCycleDecrement`, `EVENT_BATTLE_RETURN_ApplyPickupAndHoneyGather`, `_ApplyNaturalCure` |

With `EIDOS_ABILITY_LOG=1` (docs/test-switches.md) the log says once a run
which mod ability a check took as a game one
(`eidos: battle tables: ability 165 is taken as 104 ...`). An added Mummy
spreads itself; Mummy as one of several bases spreads Mummy. Mold Breaker
also ignores eidos's guarding bases (`guard_kind`, `fur_coat`,
`block_kind`, `bulletproof`, `grass_pelt` and the three veils). Forecast,
Flower Gift and Illusion still compare a new ability with their own id, to
see that it changed.
