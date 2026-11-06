# Modding

eidos runs the game's own code, compiled from aletheia's matched C. A mod
changes that game without editing the matched code: it replaces data, it
reacts to what the game does, and it reads and writes game state through the
port. This document is the overview. It says what the mod surface is made of,
how each example game mode is built on it, and which document owns each part.

## 1. The surface

A mod is a directory laid out like the data root, with a manifest,
`<mod>/eidos_mod.txt`. It can hold any mix of the parts below.

| Part | What a mod does with it | Status | Owner |
|---|---|---|---|
| Data layer | Replace a whole file, or one member of a NARC archive | built | `docs/mods.md` |
| Slots | One indirection per declared game function; with no mod installed it calls the original | built | `docs/mod-interface.md` |
| Events | Observe a game moment, with a typed payload; refuse it where the event defines a refusal | built | `docs/mod-events.md`, `docs/mod-payloads.md` |
| Raw hooks | Replace or wrap one declared game function by `(region, address)` | built | `docs/mod-overrides.md` |
| State API | Read and write party, boxes, money, flags, map and position through accessors | built | `docs/mod-events.md`, section 4 |
| Mod state | Keep the mod's own data, stored per save slot | built | `docs/mod-state.md` |
| Port services | Messages, peer sessions, field avatars, timer, run-status panel and the others | built | `docs/mod-services.md` |
| Rules | Write simple rules in a small language instead of C | built | `docs/mod-rules.md`, `docs/mod-rules-language.md` |
| Code loading | Load a mod's native library at start-up, check its interface version | built | `docs/mod-abi.md`, `docs/mod-implementation.md` |

The base is never edited. A build with no mods behaves exactly like a build
without the mod machinery, because every slot calls the original when nothing
is installed.

## 2. How a code mod runs

A mod with code is a native library that ships beside its manifest. The port
loads it in the load order, after the save is chosen and before the game runs
(`sdk/mod_loader.c`).

1. **The manifest is read and applied.** The parse fills one record per mod
   (`sdk/mod_manifest.c`); the apply checks the versions, derives the state
   namespace, checks every `override` against the declared table and against
   the prototype the build published for the row, and leaves the rule list
   and the settings path in the record.
2. **The versions are checked.** The manifest's `api` must be the port's
   interface version. A mod with raw hooks may also state the `port` version
   it was built against, and when it does the pin is checked and a mismatch
   refuses the mod. A mismatch is reported with a message naming both
   numbers, and the mod is never loaded. A mod with code and no `api` at all
   is refused too: the number cannot be forgotten.
3. **The library is opened** (`LoadLibraryW` on Windows, `dlopen` elsewhere)
   and `eidos_mod_start` and `eidos_mod_stop` are found in it.
4. **The mod's start entry runs.** It is handed the host table: the port's
   interface version, the registration call, the state table and the services
   table. It stores its own state pointer, which is the `mod_state` every
   handler receives. A non-zero return tells the port not to load the mod.
5. **The handlers are bound.** A mod binds a handler to an event with
   `host->on`, and a handler to a script command with `host->script`. The
   port holds the bindings until the start entry has returned its state
   pointer, and registers them then, in the order the mod bound them. A
   script handler is handed the mod's state, the way an event handler is, so
   a handler that reads the mod's own data finds it. A frame handler is bound
   through the services table's `on_frame` and runs once a game frame with
   the mod's own namespace in force.
6. **The overrides are installed.** Each `override` line that survived the
   declared check has its symbol resolved in the loaded library and is
   installed in the slot registry, in load order, so a later mod wins over an
   earlier one for one pair.

The way back down: the slots the mod installed are reverted to their
originals, the mod's script and frame handlers go out of their registries
(they hold the mod's own function, so a handler left behind after the unload
would be a call into a library that is gone), `eidos_mod_stop` runs (the
mod's namespace is in force, so a state call it makes there reaches its own
file, and that is where it flushes its data), and the library is unloaded
last, because a code pointer into it is still live until then.

**The refusals reach the game.** A refusal never just skips the game
function, because most game functions return a value or do work their caller
depends on. Each refusable event defines its own refused outcome, and the
port produces it at the slot: the refused outcome is what the caller sees,
so the game's own text and the game's own state stay consistent. A refused
catch keeps the ball and the turn, a refused withdrawal leaves the Pokemon in
its box. Where no such path exists and a refusal would leave the game's own
text saying something false, the event is not refusable; a mod reacts after
the fact through the state calls.

A handler that refuses can tell the player why through the message service,
which shows the text in the game's own window. Every handler bound to an event
runs; if any of them refuses, the action is refused.

## 3. The two tiers of hooks

A mod binds to the game at one of two levels.

**Events, the stable tier.** An event is a game moment with a name and a
typed payload: `encounter.wild`, `battle.faint`, `heal`,
`box.deposit`. The name and the payload layout are frozen within an interface
version (`docs/mod-abi.md`). The port maps each event onto the game functions that
carry it, and a mod never sees those functions. When aletheia renames or
re-splits a file, the port changes the mapping and the mod keeps working.
This tier is small on purpose: 51 events, derived from the challenge runs of
`docs/mod-modes.md` and listed in `docs/mod-events.md`, section 1.

**Raw hooks, the unstable tier.** A raw hook names a declared game function by
`(region, address)` and replaces or wraps it. It reaches anything in the
declared set, but its arguments are the game's own and can change when
aletheia restructures the code. A mod that uses raw hooks states the port
version it was built against. The field script VM and the battle effect VM
each get one hook on their command dispatch, keyed by opcode, so a mod can
watch or replace any script command without one declared address per command.

The address is always the key for a raw hook, never a function name. DS
addresses are fixed for the life of the ROM; names change with aletheia's
cleanup. Two overlays can share one RAM window, so the region is part of the
key: `ov093:0x021C5200` and `ov208:0x021C5200` are two different functions.

### The order several mods run in

Mods load in ascending manifest `order`, and mods with equal `order` keep the
order they were added in.

- **Every handler runs.** All handlers bound to an event run, in load order. A
  refusal does not stop the later handlers.
- **Modifications chain.** Each handler sees the payload as the handlers
  before it left it. The last write to a field wins.
- **Any refusal refuses.** If one handler refuses, the action is refused.
- **Raw hooks chain.** The registry holds a chain per `(region, address)`. A
  `wrap` goes in front of the chain, so the last-loaded wrap runs first and
  each wrap's original is the hook that was loaded before it; two wraps on
  one function nest. A `replace` takes the slot for itself: the links under
  it do not run, and the port names them (`docs/mod-overrides.md`).

## 4. Port services

Some things a mod needs are not game functions. The port provides them
(`docs/mod-services.md`):

- **Messages** in the game's own text windows, and yes/no prompts.
- **Peer sessions.** A player starts a session, the project's relay hands back
  a short code, and the other players join with it, the way Archipelago links
  games. Mods on each side exchange their own messages. It is independent of
  the game's own wireless code.
- **Field avatars** for co-op: another player drawn in the local field.
- **Pokemon export and import**, a **run-status panel**, a **timer with
  splits**, **run restart**, a **seeded random source**, **external control**
  for stream tools, **input injection**, a **frame tick** and **save handoff**.
- **Mod settings** on the port's settings screen, and a **mod menu** to enable,
  disable and order mods.
- **The player's own session**, in a Session section of the settings screen:
  host a session, read the code the relay hands out, type a code to join
  one, leave, and see the players of the session.

All of them are built, and every call in the services table is bound: the
run-status panel and the input injection included, and the mod settings with
them. The mod menu's model is `sdk/mod_menu.c` and the screen that draws it is
the port's own Mods section on the settings screen
(`engine/platform/settings_ui.c`): the player enables, disables, reorders and
clears the mods there, and the changes apply when the player leaves the menu.
The section's last row restarts the run, after the player says yes.
The Session section is the port's own, and it drives the peer session service
through the port's own calls (`engine/platform/settings.c`): the wait for the
relay does not stall the game, because the call runs on the port's own worker
thread and the screen polls it.

## 5. The game modes

`docs/mod-modes.md` lists every challenge run studied and what each needs.
The modes below are the four the design started from; each lists what it
needs and which parts of the surface supply it.
The mode's own rules are the mod's business; this section shows that the
surface makes them possible.

### Nuzlocke

The player may catch only the first encounter on each route, and a Pokemon
that faints is dead for the rest of the run.

| Need | Supplied by |
|---|---|
| Know the route, and the first encounter on it | `route.change` (`FIELD_MAP_CHANGE_SetupZone`, 0x021643E8, ov010); `encounter.wild` with species and zone (`FIELD_ENCOUNT_CreateZoneWildMons`, 0x021A954C, ov021); `encounter.static` for statics and scripted battles (`EVENT_BATTLE_CreateScriptedBattle`, 0x0216E7C8, ov010) |
| Refuse a catch that breaks the rule | `battle.item` refusal for a ball (`BTLV_CORE_GetSelectedItem`, 0x021EA01C, ov093), which keeps the ball and the turn; or `catch.attempt` (`BTL_SERVER_FLOW_TryThrowBall`, 0x021CBA40, ov093), which blocks the ball as in a trainer battle |
| Know which of the player's Pokemon fainted | `battle.faint` with the Pokemon's identity (`BTL_SERVER_FLOW_ProcFaintedPoke`, 0x021C4F64, ov093) |
| Keep the dead out of play, for example in a grave box | State API: `mon_to_box`, which puts a party Pokemon in the first free box slot (`BOX_SAVEDATA_AddPokemon` 0x020074D8 and `POKEPARTY_RemoveMember`, arm9); `box.withdraw` refusal; `battle.action` refuses a switch to a dead Pokemon |
| Stop a revive or a heal from bringing one back | `battle.item` and `item.use` refusals; `heal`, whose payload says which Pokemon heal; HP is party field 0xA0 (`POKE_TOOL_SetPartyValue`, 0x02017E40) |
| Know the run ended | `whiteout` (`SCRCMD_NETWORK_CreateWhiteOutEvent`, 0x0215F6E8, ov010: every whiteout) |
| Nickname every catch | `catch.result`, then the game's own nickname screen (`ZUKAN_NICKNAME_Create`, 0x0220B55C, ov207) |
| Remember dead Pokemon and used routes | Mod state, saved and loaded with the game save |

### Soul Link

Two players, each in their own game. The two encounters on the same route are
linked; when one dies, its partner dies too.

| Need | Supplied by |
|---|---|
| Everything Nuzlocke needs | as above |
| Exchange encounters, catches and deaths with the partner | Peer sessions |
| Kill a linked Pokemon outside battle | State API: party field 0xA0 set to 0, or a move to the grave box |
| Kill a linked Pokemon during a battle | State API: the battle's own copy of the Pokemon (`BTL_POKEPARAM_ZeroHp`, 0x021D5FB8, ov093). The battle works on a copy, so a write to the party record has no effect until the battle ends |
| Keep linked pairs together when one is boxed | `box.deposit`, `box.withdraw` and `party.swap` events (`BOX2_MAIN_UpdateBoxSlots` 0x021DD530, ov170; `POKEPARTY_SwapMembers` 0x0201AB8C) |
| Survive a restart on either side | Mod state per save slot; the pair table is exchanged again on reconnect |

Nuzlocke plus Soul Link is the union: the Nuzlocke rules applied to the
linked pairs, with one shared run record.

### Co-op

Each player plays their own game. The mod shares positions, so each player
sees the other's avatar walking in their world. Battles, scripts and story
stay local to each game.

| Need | Supplied by |
|---|---|
| Read the player's position, map and facing | State API (`FIELD_PLAYER_GetPos`, 0x021A3038, ov021; `GAME_DATA_GetPlayerZoneId`, 0x02012AC8) |
| Know when the player warps or changes map | `route.change` and the map-change events |
| Draw the partner as an avatar | The field avatar service, which creates the avatar in the game's own entity system (`FLDMMDL_AddMmdlCore`, 0x0216CDC4, ov010) |
| Send position updates | Peer sessions, every few frames from the frame tick |

`docs/mod-coop.md` owns the entity details: the pool headroom, the avatar's
model and resources, and the per-frame position update.

### The game's link features

Link battles, trades and the other wireless features are not mods. They are a
separate piece of work that replicates the game's own wireless protocol, and
this surface does not depend on it.

### Other runs

Monotype, Wedlocke, Egglocke, level caps, itemless and the other runs combine
the same parts: type and species reads, refusals on catches and item use, and
mod state. `docs/mod-modes.md` lists each one against the events and services
it uses. Randomizers and difficulty hacks are data: encounter tables,
trainer parties, base stats and moves are replaced through the data layer.

## 6. Constraints

- **Matched files are never edited.** Every mod is additive, and the base
  stays the original game.
- **Hooks exist only where declared.** A game call binds by name when the port
  is built, so a mod can reach only the functions the port routes through a
  slot. `mod/declared_set.txt` is that list, and the generators read it.
- **Overlay data does not persist.** An overlay's data is reloaded every time
  the overlay loads. A change that must last is made through a modifiable
  payload field or a game accessor, not by writing overlay data.
- **A mod is trusted.** It is native code with the port's full privilege. The
  port promises a deterministic load order, a visible conflict report and an
  on/off switch per slot, nothing more (`docs/mod-interface.md`, section 5).
- **Two mods on one raw hook.** The later mod in the load order wins, and the
  port reports the earlier one as shadowed. Events are not exclusive: every
  bound handler runs.

## 7. What is still open

1. **The relay server**: the protocol is fixed (`docs/mod-services.md`,
   "Peer sessions"), but the server that answers it is not part of the port.
2. **The nickname's force**: `mon.nickname`'s `force` is marked, but the row
   that carries it has no applier, so a write to it has no effect: the game
   has no call that forces the nickname prompt, so the port has no hook at
   that decision (`docs/mod-payloads.md`, section 0).

## Related documents

- `docs/mods.md`: the data layer, how to lay out a mod directory.
- `docs/mod-interface.md`: the declared set, the slot, the manifest and the
  trust model.
- `docs/mod-modes.md`: every challenge run studied and what each needs.
- `docs/mod-events.md`: the stable events, the handler contract, the state
  values and the raw hooks.
- `docs/mod-services.md`: the port services.
- `docs/mod-payloads.md`: the payload of each event.
- `docs/mod-overrides.md`: replacing or wrapping one game function.
- `docs/mod-abi.md`: the frozen surface and the interface version.
- `docs/mod-state.md`: the mod's own saved data.
- `docs/mod-rules.md`, `docs/mod-rules-language.md`: rules without C.
- `docs/mod-coop.md`: co-op and the partner avatars.
- `docs/mod-implementation.md`: the build plan and its order.
- `docs/hooks.md`: the compiled-in hooks the port uses for its own features.
