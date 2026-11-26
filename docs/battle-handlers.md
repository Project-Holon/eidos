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
| `type` | `normal` to `dark` | a type it tests (pinch abilities, absorb abilities, resist berries) |

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

A copy runs the base's handlers only. Code elsewhere that tests an id by
number does not know the new one: another handler asking whether a Pokemon
has Intimidate, the trainer AI's scoring, server-flow checks of a few move
ids. A move's own side effects (its stat changes, its added status and
chance) and an item's own amounts come from the move and item records,
which a mod edits there.
