# Mod ABI

A mod with code is a native library compiled against the port's headers. It
reads the port's structs at a fixed layout and calls the port's functions with
fixed signatures, so the port owns a binary interface. This document lists
that surface, what is outside it, how the interface version works and what the
port promises across versions. `docs/mod-events.md` owns the events the
surface carries and `docs/mod-payloads.md` the payload fields.

## 1. The surface

The surface lives in two headers: `sdk/mod_api.h` for the types and the entry
points, and `sdk/mod_state.h` for the state and service calls. Everything a
mod compiles against is in them, and nothing else is part of the ABI.

### The vocabulary

```c
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;
```

Every payload struct uses these fixed-width types and holds no host
pointer, so a payload's layout is the same on every host the port supports.
The three tables a mod is handed (`BwModHost`, `BwModState`,
`BwModServices`) hold function pointers, so *their* layout follows the
pointer width; a mod is a native library built for the port's platform, so
it shares that width, and the tables are reached through pointers rather
than copied.

### The event header

```c
typedef struct BwEvent {
    u32 size;   /* sizeof the concrete payload, as the port built it */
    u32 kind;   /* the BW_EVENT_* id of the event */
} BwEvent;
```

Every payload starts with `BwEvent`. A handler reads `kind` to know the event
and `size` to know how much of the payload the port filled; a payload only
grows at the end within a version.

### The Pokemon reference

```c
typedef struct BwMonRef {
    u32 handle;   /* opaque; passed back to the state calls, never read */
    u8  place;    /* BW_MON_PARTY, BW_MON_BOX, BW_MON_BATTLE, BW_MON_WILD */
    u8  side;     /* in battle: 0 the player's side, 1 the opponent's */
    u8  box;      /* in a box: the box number */
    u8  slot;     /* the position in the party, the box or the battle side */
} BwMonRef;
```

A payload names a Pokemon with a `BwMonRef`. `handle` is the game's own
reference (a party or box record, or a battle record) and is only ever passed
back to the state calls, so the game's record layout stays out of the ABI.
`place`, `side`, `box` and `slot` say where the Pokemon is, so a mod can
compare two references and store one in its own data.

### The payloads

One struct per stable event, `BwEvent` first (`docs/mod-payloads.md`). Its
fields are frozen within a version, and new fields are only added at the end.

### The handler and its return

```c
typedef int (*BwEventHandler)(void *mod_state, BwEvent *event);

enum {
    BW_EVENT_CONTINUE = 0,
    BW_EVENT_REFUSE   = 1
};
```

The contract is `docs/mod-events.md`, section 2: a handler may write the
fields its event marks as modifiable, and returns `BW_EVENT_REFUSE` to refuse
an event that defines a refused outcome.

### The event ids

One `BW_EVENT_*` constant per stable event, named after the event in upper
case: `battle.faint` is `BW_EVENT_BATTLE_FAINT`. An id never changes and is
never reused.

### The entry points

A mod library exports two functions:

```c
int  eidos_mod_start(const BwModHost *host, void **mod_state);
void eidos_mod_stop(void *mod_state);
```

`eidos_mod_start` runs once when the port loads the mod, after the save is
chosen and before the game runs. It receives the host table and stores its own
state pointer; that pointer is the `mod_state` the event handlers receive.
It returns 0, or a non-zero value to tell the port to unload the mod.
`eidos_mod_stop` runs when the port unloads the mod or exits.

`BwModHost` is a table of function pointers the port fills: the interface
version, the two registration calls, the state calls and the service calls.

```c
typedef struct BwModHost {
    u32  api;                                   /* the port's interface version */
    int  (*on)(u32 kind, BwEventHandler fn);    /* bind a handler to an event */
    int  (*script)(u32 vm, u16 opcode, BwScriptHandler fn);  /* to a command */
    const struct BwModState    *state;          /* sdk/mod_state.h */
    const struct BwModServices *services;       /* sdk/mod_state.h */
} BwModHost;
```

A mod calls `host->on` from `eidos_mod_start` for each event it handles, and
`host->script` for each script command it hooks. The port holds the event
bindings until the start entry has returned its state pointer, and registers
them then, in the order the mod bound them. The table is the only way into the
port, so a mod links against nothing but the host executable's exported table,
and one library works with any port build of the same version.

### The script commands

The field script VM and the battle effect VM each dispatch every command
through one table: the field VM's has 609 entries and the battle effect VM's
78. The port hooks both dispatches, so a mod can observe or replace a command
by its opcode without one declared row per command.

```c
enum {
    BW_SCRIPT_VM_FIELD  = 0,   /* the field script VM */
    BW_SCRIPT_VM_BATTLE = 1    /* the battle effect VM */
};

typedef struct BwScriptCommand {
    u16 opcode;                /* the command's opcode */
    u16 nargs;                 /* how many operand bytes the command carries */
    u8  args[BW_SCRIPT_ARGS_MAX];  /* the operands, as the bytes they are in */
    u32 pc;                    /* the program counter before the command */
    u32 next_pc;               /* the program counter after it */
} BwScriptCommand;

typedef int (*BwScriptHandler)(void *mod_state, BwScriptCommand *cmd);

enum {
    BW_SCRIPT_CONTINUE = 0,   /* the game's own handler runs too */
    BW_SCRIPT_REPLACE  = 1    /* the game's own handler does not run */
};
```

The operands are bytes: the game's script holds them as bytes, and its own
handler reads them off the VM's cursor a halfword or a word at a time.
Eighteen of the field VM's commands carry an odd number of bytes, so those
read a byte or two straight off the cursor. The view holds every byte of
every command of both VMs: the port's own width table gives the width of each
command in bytes, and the widest is `BW_SCRIPT_ARGS_MAX`, 60 (the battle
effect VM's opcode 8; the field VM's widest is 22), so a handler reaches the
last operand of the widest command as well as the first of the narrowest. A
handler writes the bytes to change the command the game runs, and the port
writes them back into the script exactly, with nothing rounded to a word.

The program counter is a number, not a pointer: it is the hook's own count of
where the command sits in the script, so the view carries no host pointer and
reads the same at every pointer width. The view is valid only for the call.

Every handler bound to a command runs, in load order, and each one sees the
command as the ones before it left it, so the writes to the operands chain. A
handler that replaces the command does not stop the later handlers; when any
of them replaced it, the game's own handler does not run.

A script handler is handed the state of the mod that registered it. The
port holds the binding until the start entry has returned the mod's state
pointer, so a handler registered from the start entry is handed that state
too; a mod that returns no state hands it none.

The header carries four conveniences for reading and writing the operands a
word at a time: `bw_script_arg_u16`, `bw_script_arg_u32`,
`bw_script_set_arg_u16` and `bw_script_set_arg_u32`. They are not part of the
frozen surface, and a handler may read and write the bytes of `args` itself.

### The state and service calls

`sdk/mod_state.h` declares `BwModState`, one function per value of
`docs/mod-events.md`, section 4: party and box reads and writes, a Pokemon's
fields by `BwMonRef` and field id, the species data and the dex, money,
badges, flags, the map, the player position and the battle style, and the two
moves between places. `BwModServices` declares the port services of
`docs/mod-services.md`: messages, peer sessions, the field avatars, the
mod's own saved data, the timer, the run restart, the random source, the
external control, the frame tick, the save handoff, the run-status panel, the
input injection and the others. Every call in the table is bound; a service
the port has not built is `NULL` in the table, so a mod built against another
version of the surface checks the pointer first. Each name and signature is
frozen within a version.

## 2. What is not in the surface

| Not in the surface | Why |
|---|---|
| The game's own structs (party and box records, battle records, save blocks) | aletheia restructures them; a mod reaches them only through the state calls |
| Raw hooks and their arguments | they are the game's functions, so they change with aletheia; a mod with raw hooks is pinned to one port version (`docs/mod-events.md`, section 5) |
| The DS addresses behind the stable events | the port's binding, which may move without a mod noticing |
| The rule language | it has its own version (`docs/mod-rules-language.md`) |
| The `.bw` container format | kenon owns it; the port only reads it |

## 3. The version

`api` is one integer, the major version of the surface. It is 5. A mod states
the version it was built against in its manifest, and the port checks that
number against its own before it loads the mod. A mod may also compare the
`api` in the host table with the number it was built against and refuse to
start by returning a non-zero value from its start entry. The port loads the
mod when its number is between `BW_MOD_API_MIN` and `BW_MOD_API_VERSION`
(3 and 5) and refuses it otherwise, with a message naming both. An older
number is accepted only when the newer version appended to the surface and
moved nothing, so the old mod reads every struct at the layout it was built
for. Any other mismatch means the mod reads a struct at a layout the port
does not have, which fails silently, so a warning is not enough.

The version moves when the surface moves.

- **Version 1** was the surface without the script command hooks: the
  events, the payloads, the state calls and the services.
- **Version 2** added the script command hooks: the host table's `script`
  call and the command's view.
- **Version 3** gave the command's view the whole operand area: the operands
  are bytes and the view holds every byte of every command of both script
  VMs, so a handler can reach an operand past the eighth word. It added the
  services table's `state_in_step`, which answers for the calling mod
  whether its own saved data was written with the save in force.
- **Version 4** appends and moves nothing, so version 3 mods still load. The
  state table gains `mon_release`, `item_count`, `item_add`, `area_of`,
  `battle_wild`, `play_seconds`, `item_value`, `move_value`,
  `mon_to_box_n`, `mon_from_box`, `mon_recalc`, `species_base` and
  `battle_trainer`; the services table gains `table_get`; the events gain
  `heal.hp`, `mon.revive`, `mon.cure`, `mon.pp`, `mon.form`,
  `battle.trainer_mon` and `battle.exp`;
  `encounter.wild`, `mon.exp`, `mon.levelup`, `catch.attempt`, `item.add`
  and `money.change` gain fields at their ends; `heal` gains `script_id`.
  Version 4 is frozen: the surface in `sdk/api/v4` does not change any
  more.
- **Version 5** appends and moves nothing, so version 3 and 4 mods still
  load. The services table gains `event_player`, `shared_get`,
  `shared_set`, `shared_add` and `shared_cas` (multiplayer, `docs/mod-services.md`, "Multiplayer"),
  and `battle.exp` gains `share` at its end (experience for the bench).
  For battle AI mods: the events `battle.ai` (the item, switch and move a
  Pokemon the game controls chooses, each writable), `battle.ai_replace`,
  `battle.ability`, `battle.stat`, `battle.held_item`, `battle.status` and
  `battle.turn`; the state table gains `type_effect`, `battle_weather` and
  `species_learns`; `battle_get` reads the stat stages, the major status
  and the ability in force (field ids 0x101 to 0x109). The AI's own
  functions (`TR_AI_Think`, `TR_AI_Start`, `TR_AI_GetChosenTarget`,
  `BTL_CLIENT_StepAutoChooseActions`, `BTL_CLIENT_ChooseReplacementsAuto`)
  are in the hookable set for code mods that replace them whole. A mod
  with `multiplayer = <n>` in its manifest receives the other players'
  events through its own handlers. Added later in version 5 (appended,
  nothing moved): the state table's `whiteout` (the game's own whiteout, to the last
  Pokemon Centre); `battle_get` reads the battle's own moves and their PP
  (`MOVE1` to `MOVE4`, `PP1` to `PP4`); `move_value` reads all three stat
  changes of a move (ids 0x100 to 0x113). Version 5 is open: `sdk/api/v5`
  is the current description.

The number cannot be forgotten. A test recomputes the hash of the surface out
of the headers themselves and compares it with the number the header carries,
so a change to the surface fails the test until the hash is updated in the
same change; and the rule is that the same change raises the version. A
comment or a change outside the headers does not move the hash.

The hash is an FNV-1a fold (the seed 2166136261, the multiplier 16777619): a
name goes in byte by byte, a number goes in as eight little-endian bytes. It
covers

- the event header and the Pokemon reference, field by field;
- the handler types, by their size;
- every event id, by its constant name and its value;
- every small enum, by its name and its value;
- every payload struct, field by field;
- the script command's view and the script enums;
- the three tables, member by member;
- the port's own state file header, field by field.

Each field and each member goes in as its name, its offset and its size. The
fold therefore cannot see a field that changes its type without changing its
size, and it cannot see the signature of a call at all; the version is the
guard for those.

The three tables hold function pointers, so their layout follows the pointer
width and the hash follows it: the number beside the version number is the
32-bit one, the port's reference configuration, and the test carries the
64-bit one as well, so a change fails on every width the port is built at.

| Pointers | Hash |
|---|---|
| 32-bit | 0xF31D7215 (the number in `sdk/mod_api.h`) |
| 64-bit | 0xC25CE7CF |

The payload structs, the ids and the enums hold no pointer and hash the
same everywhere.

## 4. What the port promises

- **Within a version** a mod runs unchanged, with no rebuild: event names,
  ids, payload layouts, the handler type, the script command's view and every
  state and service call stay as they are. Fields may be added at the end of a
  payload.
- **Across versions** a mod rebuilds against the new headers. Its source
  survives where it uses the stable parts: event names, `BwMonRef`, and the
  state calls. Only code that reads a changed payload field needs an edit.
- **aletheia's restructuring** does not reach a mod that uses only the stable
  tier. Renames and re-splits move the port's bindings and the game's structs,
  both of which the surface hides. It reaches raw hooks, which is why those
  are pinned to a port version.

## Still to build

1. **A signature the port cannot see.** The hash folds in each field's
   name, its offset and its size, and each table member's name, offset and
   size. A field that changes its type without changing its size (an
   unsigned field that becomes a signed one) and a call whose parameters
   change are both invisible to it, so the test does not fail for them. The
   interface version is the guard there, and the loader is what enforces it.
   A mod that states a prototype in its manifest is checked against the
   function's own (`docs/mod-overrides.md`, "The prototype is checked"), so a
   hook whose stated prototype differs is refused; a mod that states none is
   not checked.
2. **The script handler's state.** A script handler registered from the start
   entry point is handed the mod's state, the way an event handler is: the
   port holds the binding until the start has returned the state pointer.

## Related documents

- `docs/mod-events.md`: the stable events, the handler contract and the state
  values.
- `docs/mod-payloads.md`: the fields of each payload.
- `docs/mod-services.md`: the port services.
- `docs/mod-interface.md`: the slot, the manifest and the trust model.
- `docs/pointer-width.md`, `docs/lp64.md`: the integer widths the port uses.
