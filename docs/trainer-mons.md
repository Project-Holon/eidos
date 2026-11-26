# Trainers' Pokemon: per-member numbers

The game's trainer data stores each Pokemon's species, level, form, item,
moves, and a single strength byte that sets all six IVs at once. It does
not store EVs, nature, shininess, or friendship: nature is derived from
the personality value, and trainer Pokemon are never shiny. Mods can
override these fields per party member in a single table.

Code: `sdk/trainer_mons.h`, `sdk/trainer_mons.c`, applied from
`sdk/mod_event_battle.c`.

## The table

`eidos/tables/trainer_mons.txt` in a mod uses one line per party member:

    <trainer id>.<slot> = [key=value ...]      # a comment

    164.0 = ivs=31,31,31,31,31,31 evs=252,0,4,0,0,252 nature=jolly
    164.1 = nature=modest shiny=1
    12.5 = friendship=0 ability=hidden

The trainer ID is the game's trainer number (its index in the trainer
data), and the slot is the party position (0 to 5). Every key is optional,
and omitted slots keep the game's defaults. A later mod's entry for the
same slot overrides an earlier one. Invalid lines are logged
(`eidos: <file>:<line>: ...`) and skipped.

| Key | Values |
|---|---|
| `ivs` | six numbers 0 to 31: HP, Atk, Def, Sp. Atk, Sp. Def, Speed |
| `evs` | six numbers 0 to 255 in the same order, 510 at most in all |
| `nature` | a nature's name (`adamant`), or its number 0 (Hardy) to 24 (Quirky) |
| `shiny` | `1` shiny, `0` not |
| `friendship` | 0 to 255 |
| `ability` | `1` or `2` for the species' first or second ability, `hidden` for its hidden one |

A species with no second or hidden ability falls back to its first.
Shininess is controlled through the Pokemon's trainer ID, so its
personality value (gender and ability slot) remains unchanged.

## When it applies

Overrides run after trainer battle party setup and before the
`battle.trainer_mon` event, so code mods see the updated values and can
modify them further. This applies to opponents and multi-battle partners
in both field and scripted trainer battles. The Battle Subway and other
battle facilities generate teams from separate data and are not affected.
Stats are recalculated from the new IVs, EVs, and nature. Each overridden
Pokemon is logged:
`eidos: trainer 164 slot 0: ivs evs nature (eidos/tables)`.
