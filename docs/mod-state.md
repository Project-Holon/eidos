# Mod state

This document owns a mod's own saved data: where it lives, when the port reads
and writes it, how it stays in step with the game's save, and how the manifest
becomes registrations at load. `docs/mod-interface.md`, section 3, gives the
manifest keys and `docs/mod-abi.md` the calls a mod makes.

## 1. The state namespace

A mod's own state is one folder per mod, named by the manifest's `state`
namespace, under the save directory. The folder holds one port-owned state
file per game save, so the mod's data always belongs to the save it was
written with.
The port owns the file's header; the mod owns the bytes after it. The game's
own state is the cartridge's backup chip (`sdk/save.h`, its header names
`sdk/arm7/fs.c` and `pokeblack.sav`), and the port's own files are the firmware
flash and the user settings file (`sdk/arm7/nvram.c`, `sdk/firmware.c`;
`sdk/save.h`, its header). All of them share the one save folder and the one
write discipline (`sdk/save.h`). The `mods/` subfolder and the
per-mod folder keep a mod's state apart from the game's file, apart from the
other port files, and apart from another mod's state.

### One file per mod, not a shared file

The write discipline is a whole-file atomic write: `bw_save_write_atomic`
writes `<path>.tmp`, syncs it, and replaces the path (`sdk/save.c`;
`sdk/save.h`). The unit of that discipline is one file, so one file
per mod matches the mechanism the port already has. `bw_save_write_atomic`
also creates the folder part of the path with `parent_of` and
`bw_plat_mkdirs` (`sdk/save.c`), so a folder per mod needs
no new folder call.

A shared file was the alternative: one file with one region per mod, keyed by
an offset table. It is not chosen. A region whose size changes moves every
later region, so a mod's format change migrates its neighbours. A bad write in
one region can damage another mod's bytes, and the port restores nothing and
offers no transaction (`docs/mod-events.md`, section 4). The file boundary
is the only isolation the port can give, because a mod runs in the port's own
address space with the port's privilege (`docs/mod-interface.md`, section 5).

`docs/mod-interface.md`, section 3, allows "one file or folder". A folder per
mod keeps the file boundary and lets a mod keep more than one file under one
namespace, a pairing table beside a run history for example, without a second
namespace. A mod that wants raw files of its own asks for the folder path with
`state_dir`, which makes the folder if it is not there yet.

### Where it lives

The path is `<save dir>/mods/<namespace>/<save>.bin`, where `<save>` is the
name of the game save file the port is running (`pokeblack` for
`pokeblack.sav`). The save directory is `bw_save_dir()`, first match:
`bw_set_save_dir`, then `BW_SAVE_DIR`, then the platform default (`sdk/save.c`;
`sdk/save.h`). A player who keeps several game saves gets one state file per
save, and switching saves switches the mod's data with it.

### The namespace value

The namespace is the manifest's `state` key, an identifier whose default is
derived from `name` (`docs/mod-interface.md`, section 3). The port validates it
as one path component: it must be non-empty, must not contain a separator or a
`.` or `..` component, must not be a reserved device name, and must contain
only a small character set. `bw_save_path` copies the name verbatim
(`sdk/save.c`), so an unvalidated name can leave the `mods/` folder.
A bad namespace is reported and the port falls back to the default, which is
derived from the mod's name (the directory's name when the manifest states
none), the way a bad `order` is reported and the default stands
(`sdk/vfs.c`).

The port's own rules, which the documents leave open:

- The default namespace is the name in lower case. The name is split at the
  spaces, and every character that is not a letter, a digit, a dash or an
  underscore becomes a dash, a run of them becomes one dash and a trailing one
  is cut. The first word stands alone, a dash follows it, and the words after
  it are joined to it without a dash: "Nuzlocke Soul Link" gives
  "nuzlocke-soullink". A name with nothing usable in it gives "mod".
- The namespace is at most 64 characters, and it may hold letters, digits,
  `-`, `_` and `.` only. A reserved device name (CON, PRN, AUX, NUL, COM1
  to COM9, LPT1 to LPT9, with or without an extension) is refused.

### The envelope and the payload

The file is a port-owned header followed by the mod's opaque payload.

| Offset | Field | Owner |
|---|---|---|
| 0 | magic `BWMS`, 4 bytes | port |
| 4 | `version`, u16, the port's header version | port |
| 6 | `flags`, u16, zero | port |
| 8 | `payload_size`, u32 | port |
| 12 | `payload_crc`, u32 | port |
| 16 | `api_written`, u32, the manifest `api` in force | port |
| 20 | `save_check`, u32, a checksum of the game save written in the same step | port |
| 24 | the payload | mod |

The header is 24 bytes (`BwModStateHeader` in `sdk/mod_state.h`). It is written
one field at a time and little-endian, so the file is the same on every host
whatever the compiler does with the struct. The port writes and reads it. The
mod owns the payload
and its internal format, which is the mod-owns-the-bytes rule of
`docs/mod-interface.md`, section 3. The mod never sees the header; the
port-owned calls of section 2 hand it the payload and its length. A mod that
wants the raw file uses the folder path and owns every byte in it, including
its own version marker.

The payload's checksum is a CRC-32: the reflected IEEE polynomial
0xEDB88320, the one every host library computes. It catches a hand edit or a
disk fault, and `bw_save_write_atomic` already rules out a torn write, because
it writes a temporary and replaces it (`sdk/save.c`, `sdk/save.h`). The
largest payload a mod may store is 4 MiB; a bigger store is refused and
reported.

### Removal

The port does not delete a mod's folder when the mod is removed.
`docs/mod-rules.md`, section 2, constraint 3, fixes that a disabled mod's
effects persist, and the port restores nothing (`docs/mod-interface.md`,
section 4). Deleting the state
on removal would rewind a run the player may re-enable, so removal leaves the
folder and a re-enable reads it again. The orphan report, which tells the
player that a mod's folder is left over, belongs to the mod menu: its model
answers it (`sdk/mod_menu.c`) and the Mods section of the port's settings
screen shows it as a row of its own. A player-facing clear action calls the
port's `state_clear`, which deletes the state file and leaves the folder.

## 2. The lifecycle

### The four points

**Created.** The first store creates the file, and `bw_save_write_atomic`
creates the folder. The port writes no empty file at load, so an absent file
and an empty file are the same to a mod, and the first run starts from the
mod's defaults.

**Loaded.** The file that belongs to the save being loaded is the one the
mod's own `state_load` reads: the file is named after the game save, so a mod
that loads at `save.load` reads the file of the save it is loading. The port
also compares the header's `save_check` with the save in force, and the mod
asks for that answer with `state_in_step`: when the two differ, the game save
was written without the mod (an older backup, or a save made while the mod
was disabled), and the mod decides what to do. A new game (`game.new`) starts
with no state.

**Written.** The port writes the mod's state in the same step as the game's
save. The saved-data layer holds a window for that: the port opens it, a
`state_store` inside the window holds the payload instead of writing it, and
the port closes the window with the game's save's answer, which writes the
held payload with the new game save's checksum or drops it when the save
failed. A game save that fails leaves the old state file in place, so the two
never drift apart. A store made outside that window is written at once, with
the checksum of the save currently on disk.

The window is the game's own save: the `save.begin` row opens it and the
`save.done` row closes it, after the handlers have run. A save that never
finished (the player backed out of it) leaves the window open, and the next
`save.begin` closes it and drops the payload it held, so a save that did not
happen writes nothing. A store the mod makes after such a cancellation, while
the window was still open, is dropped with it.

**One checksum.** The checksum of the save in force is the CRC-32 of the
save's bytes as the chip holds them: the save's first `<chip size>` bytes,
with the rest of the chip erased. The layer and the save handoff both use
that one rule, so the two never disagree about whether a mod's file is in
step with the save in force. A file shorter than the chip brings its trailer
along, and a trailer beyond the chip's size is not part of the save in force
and does not travel.

**Discarded.** A store is the whole file: `bw_save_write_atomic` writes a
temporary and replaces the file, so there is no partial state. A store that
fails is reported once per mod and the old file stays. A file the port cannot
read is moved aside (section 5). A player clear action calls
the port's `state_clear`.

### The in-game management constraints

The four constraints are `docs/mod-rules.md`, section 2.

1. The roots and the override registry are rebuildable at the menu, not
   installed once at startup. `bw_set_mod_root`, `bw_add_mod_root` and
   `bw_clear_mods` set the roots, and every change drops the cached merged
   archives (`sdk/vfs.h`, "The mod list"; `sdk/vfs.c`, `bw_vfs_flush`). The
   state namespace is per mod, so a root rebuild does not move state. Enabling
   a mod adds its root and the load point reads its state. The menu's model
   is `sdk/mod_menu.c` and the screen that drives it is the Mods section of
   the port's own settings screen (`engine/platform/settings_ui.c`;
   `docs/mod-services.md`).
2. Disabling a code mod reverts its slots before its library unloads. The
   registry holds the mod's handler, and a code pointer in data resolves
   through the same lookup (`sdk/overlay_image.c`, `bw_ovl_code_entry`), so
   the revert must precede the unload. The script registry and the frame
   table hold the mod's own function too, so they are emptied of it as well.
   The loader keeps the order
   (`sdk/mod_loader.c`): the slots the mod installed go back to their
   originals in the reverse order they were installed, the mod's script and
   frame handlers go out of their registries, `eidos_mod_stop` runs
   (the namespace is in force, so a state call the mod makes there reaches
   the mod's own file and is where a mod flushes its data), and the library
   is unloaded last, because a code pointer into it is still live until then.
3. A disabled mod's effects persist. The state file stays and the game bytes
   the mod wrote stay until the game writes over them or the port restarts
   (`docs/mod-rules.md`, section 2, constraint 3). Re-enabling reads the state
   again and resumes. The port restores nothing.
4. The port starts with no mods. With no mod root there is no state read, and
   `BW_MOD_ROOT` is read once (`sdk/vfs.c`, `mods_init`). The state folder is
   untouched.

### The calls

A mod uses its state through the service table it receives at start-up
(`docs/mod-abi.md`, "The entry points"; `BwModServices`). The port knows which
mod is calling, so no call names a namespace:

```c
s32  (*state_load)(void *buf, u32 size);    /* bytes read, -1 on damage */
int  (*state_store)(const void *buf, u32 size);   /* 0, or -1 */
int  (*state_clear)(void);                         /* 0, or -1 */
int  (*state_info)(BwModStateHeader *out);         /* the header fields */
int  (*state_dir)(char *out, u32 out_size);        /* the mod's folder */
int  (*state_in_step)(void);                       /* 1 in step, 0 out of step, -1 unknown */
```

`state_in_step` answers for the mod that calls it: whether the mod's own
file was written with the game save in force.  The question is per mod, so
it cannot be the `save.load` payload's byte: that payload is built once for
all the handlers of the event, and one mod's file may be in step while
another's is not.  The port keeps the answer in the file's header
(`save_check`) and compares it with the save in force, which the layer reads
from the port's own save state.  The answer is 1 when
the two match, 0 when they differ, and -1 when the question cannot be
answered: there is no state file, or the save in force cannot be read.

The port's own `save.begin` and `save.done` events are where a mod reacts to
a save, and they are what drive the layer's window: the game's own save
opens it and the game's own save closes it, so a mod's file is written with
the checksum of the save the game wrote. A mod that stores its payload in its
`save.begin` handler hands the
bytes to the layer, which holds them while the save is in progress and writes
them with the new save's checksum when it ends. A mod that keeps its own files
uses `state_dir` and the same whole-file discipline.

### The rule cells' own file

A rule keeps its own cells, and the cells do not live in the mod's own file.
One file per mod is the only isolation the port can give, and a rule that
wrote a cell would take the mod's own payload with it. So the cells live in a
file of their own, under a namespace derived from the mod's: the mod's
namespace with `-rules` at the end, cut so it stays inside the length the
saved-data layer accepts. The saved-data layer turns that namespace into a
folder and a file of its own, and the file is named after the game save, so
the cells still follow the game save.

The cells' payload is the rule layer's own: the magic `BWRC`, a version, and
the cells. A file that does not hold that magic, or holds another version, is
not read: the cells stay in memory for the run and the port is told once. A
port that has given the rule layer no file for the cells at all keeps the
cells in memory for the run too, and says so once.

## 3. The run-time manifest

### The schema

The manifest is `<mod>/eidos_mod.txt` (`sdk/vfs.c`, `BW_MOD_MANIFEST`;
`docs/mod-interface.md`, section 3). `read_manifest` parses `#` comments and
`key = value` lines, ignores a line with no `=`, and ignores an unknown key on
purpose so a later version can add keys (`sdk/vfs.c`). It
reads `name` and `order` today. The run-time schema is the manifest of
`docs/mod-interface.md`, section 3: `name`, `order`, `api`, `port`, `code`,
`state`, `override`, `rule` and `settings`.

The port parses the file into one record per mod, `BwModManifest`, and turns
the record into registrations. The keys and what each becomes:

| Key | Carried as | Registration |
|---|---|---|
| `name` | text | the mod name in every report, and the source of the default namespace |
| `order` | signed integer | the load order, ascending; a later mod wins (`sdk/vfs.h`, "The mod list") |
| `api` | integer | the interface version the mod is built against; a mismatch refuses the mod (`docs/mod-abi.md`, section 3) |
| `port` | text | the port version a mod with raw hooks is built against; a mismatch refuses the mod |
| `code` | relative path | the library the loader opens |
| `state` | identifier | the state namespace of section 1 |
| `override` | a list of pairs | one `bw_mod_slot_install_mode` per pair, after the declared check of section 4 |
| `rule` | a list of event and path | a rule binding for the interpreter |
| `settings` | relative path | the settings declaration |
| `ui_text` | relative path | the interface's words in the mod's language |

An `override` value is `<region>:<address> <mode> <symbol> [sig=...]
[switch=...]` (`docs/mod-interface.md`, section 3). The record keeps the region
constant, the address in both the even-start and the Thumb-tagged form, the
mode (`replace` or `wrap`), the symbol, the optional signature and the optional
switch name. The region token resolves the way `docs/mod-implementation.md`,
section 6, fixes: `arm9` to `BW_MOD_REGION_ARM9` and `ovNNN` to
`BW_MOD_REGION_OVL(NNN)` (`sdk/mod_slot.h`). The signature is compared with
the function's own prototype and a mismatch refuses the override. The build
publishes the resolution of every type name the game defines, so a stated
prototype that names a game type is compared whatever function it names; a
name two files resolve differently is not published, and a prototype that
names it is reported and not compared, so there is no false refusal. The
switch name is the key the hook answers to, and it defaults to the row's own
function name (`docs/mod-overrides.md`).

### Where it comes from in a packed mod

In a packed mod the manifest is `eidos_mod.txt` at the root of the tree inside
the `.bw` container. The port unpacks a `.bw` into its own cache at start-up
and reads the directory, so the reader opens `<root>/eidos_mod.txt` the same
way for a packed and an unpacked mod.

### How it becomes registrations

The port parses first and applies second. The parse is `bw_mod_manifest_parse`.
The apply is `bw_mod_manifest_apply`, one call per mod, in load order. The
apply runs after the declared table is registered (section 4) and after the
event layer is reset (`docs/mod-implementation.md`, section 6), so a later mod
wins over an earlier one for one pair (`docs/mod-overrides.md`, "Two mods on
one address").

The apply does four things per mod:

1. Check `api` against the port's version, and check the `port` pin when the
   mod has raw hooks and states one. A mismatch refuses the mod.
2. Derive the namespace from `state`, or from `name` when `state` is absent.
   A stated namespace that is not one path component is reported and the
   default stands.
3. For each `override`: check the pair against the declared table, and the
   stated prototype against the one the build published for the row. The
   ones that are declared and agree stay in the record; the others are
   reported and dropped. The switch the row named goes to the registry,
   which asks it when the chain is walked.
4. Store each `rule` and the `settings` path for the interpreter and the
   settings surface.

The loader does the rest: it resolves each surviving override's symbol in the
loaded library and calls `bw_mod_slot_install_mode(region, addr, symbol,
owner, mode)` (`sdk/mod_slot.c`), in the order the manifest gave them. A
`wrap` goes in front of the chain the pair holds, so the last-loaded wrap
runs first and each wrap's original is the hook that was loaded before it;
a `replace` takes the slot for itself and the links under it do not run
(`docs/mod-overrides.md`).

## 4. The declared-address check

The set is `mod/declared_set.txt`. `tools/bootlink.py` reads it at build time
with `read_declared_set` (`docs/mod-implementation.md`, section 6) and
`tools/ovlink.py` reads it too, and nothing carries it at run time. The port
must know the set at load, when a mod's manifest names its addresses, so the
build emits a table of the set beside the slot publications.

### The emitted table

The build emits one table of the whole set, `bw_declared_rows[]`, from every
row of `mod/declared_set.txt`, the `arm9` rows and the overlay rows together.
The boot link emits it beside the slot publications, into `boot_link.c`
(`tools/bootlink.py`, `write_link_c`; `docs/mod-interface.md`, section 2), and
`bw_boot_link_init` registers it (`tools/bootlink.py`, `write_link_c`). Each row is
a pair:

```c
/* sdk/mod_slot.h */
typedef struct BwModDeclared {
    unsigned int region;   /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;     /* the even function start */
} BwModDeclared;

void bw_mod_declare_table(const BwModDeclared *rows, unsigned int count);
int  bw_mod_slot_declared(unsigned int region, unsigned int addr);
```

`bw_mod_declare_table` stores the rows in a declared set in `sdk/mod_slot.c`.
`bw_mod_slot_declared` answers from it. The set holds a few hundred rows, so the
table is sorted by region and address and searched by bisection.

The table carries the whole set, not only the rows the link defines. A declared
row whose definition the link does not carry has no slot
(`docs/mod-interface.md`, section 2), and an overlay row's slot does not exist
until that overlay loads. The check runs at mod load, which is before any
overlay loads, so a table that only carried published rows would answer
"not declared" for a declared overlay address. One table at start-up makes the
answer independent of what has loaded, and leaves `tools/ovlink.py` with no
change.

### The check

`sdk/mod_manifest.c` calls `bw_mod_slot_declared(region, addr)` for each
`override` pair. The address is normalised before the lookup: the low bit is
masked, so the even start (`arm9:0x02007150`) and the Thumb-tagged value a code
pointer holds (`arm9:0x02007151`) both answer declared
(`docs/mod-interface.md`, section 1).

A pair that answers declared is installed once the loader resolves the symbol.
A pair that answers not declared is reported and skipped, the shape
the port uses for every manifest problem:

```
eidos: mod <name>: override <region>:<addr> is not a declared address
```

### An address that is not declared

The mod still loads and its other keys apply. The report fires once per pair,
the way `read_manifest` reports a bad `order` once and lets the default stand
(`sdk/vfs.c`). The port does not fail the mod, because the trust
model validates nothing and the check is a diagnostic, not a sandbox
(`docs/mod-interface.md`, section 5). No slot consults the address, because no
slot exists for it, and the override is never installed.

## 5. A version story for the state

A mod's state file outlives a port version. Two versions meet in it.

- The port's header version, `BwModStateHeader.version` of section 1. The port
  owns it and bumps it when the header layout changes.
- The mod's payload version. The mod owns it and puts its own version in the
  payload. The port cannot interpret it.

The header also carries `api_written`, the manifest `api` in force when the
state was written (`docs/mod-abi.md`, section 3). The port reports a
mismatch to the mod at load and the mod decides.

### An older file

The port reads a header older than the current version, locates the payload
with the old header's `payload_size`, hands the mod the payload, and writes the
current version on the next store. The payload is opaque, so a header change
that keeps the payload locatable loses nothing. If the old header cannot locate
the payload, the port moves the file to `<save>.bin.v<old>` and starts fresh,
reporting it. The moved file is kept, not deleted, so a player can recover it.

### A newer file

A version newer than the port's is not read. The port reports
`eidos: mod <ns>: state version <n> is newer than <m>`, does not hand the
payload to the mod, moves the file to `<save>.bin.v<n>`, and starts fresh.
Reading a newer layout would misinterpret the payload, and the moved file lets
a newer port read it again.

### A damaged file

The magic is wrong, the file is shorter than the header, `payload_size` exceeds
the file, or `payload_crc` does not match: the port reports
`eidos: mod <ns>: state file is damaged`, does not hand the payload to the mod,
moves the file to `<save>.bin.bad`, and the mod starts from defaults. The crc
catches a hand edit or a disk fault. `bw_save_write_atomic` already rules out a
torn write, because it writes a temporary and replaces it (`sdk/save.c`, `sdk/save.h`).

## 6. What is built

The state layer is `sdk/mod_savedata.c`, the manifest layer is
`sdk/mod_manifest.c`, and the loader is `sdk/mod_loader.c`. Between them they
cover the whole of this document:

- **The envelope.** `sdk/mod_state.h` carries the frozen 24-byte header and the
  six state calls; `sdk/mod_savedata.c` writes and reads it, one field at a
  time and little-endian, and computes the payload's CRC-32.
- **The file.** The namespace validation, the default namespace, the path
  (`<save dir>/mods/<namespace>/<save>.bin`), the whole-file atomic write and
  the move-aside of a file the port cannot read.
- **The lifecycle.** The staged payload between `save.begin` and `save.done`,
  the save's checksum, and the per-mod "in step" answer.
- **The manifest.** The record, the parse, the namespace rules, the version
  check and the declared-address check, in `sdk/mod_manifest.c` and
  `sdk/mod_manifest.h`.
- **The loader.** The library, the host table, the version check, the handler
  bindings, the override installation and the way back down, in
  `sdk/mod_loader.c`.
- **The cells' file.** The rule layer's own namespace and the two calls the
  loader installs for it, in `sdk/mod_rule.c`.
- **The menu.** The model's own list of mods, their switches, their order and
  the orphan report, in `sdk/mod_menu.c`, and the screen that draws it, the
  Mods section of the port's settings screen
  (`engine/platform/settings_ui.c`).

The tests are `tests/test_mod_savedata.c` (the envelope, the versions and the
write discipline), `tests/test_mod_manifest.c` (the parse, the namespace, the
declared check and the override grammar), `tests/test_mod_declare.c` (the
declared set at run time), `tests/test_mod_menu.c` (the menu's model) and
`tests/test_mod_loader.c` (which loads a real library).

## The port's own choices

The documents leave these to the port:

- The header's version is 1 (`BW_MOD_STATE_VERSION` in `sdk/mod_state.h`).
- The payload's checksum is a CRC-32 (the reflected IEEE polynomial), and the
  port publishes it so the game save's own checksum is computed the same way
  (`bw_mod_savedata_crc`).
- The largest payload a mod may store is 4 MiB; a bigger store is refused
  and reported (`BW_MOD_SAVEDATA_MAX_PAYLOAD`).
- A file that is moved aside because it cannot be read keeps the name the
  documents give it, with `.1`, `.2` added when that name is taken, so an
  earlier moved file is never overwritten.

## Related documents

- `docs/mod-interface.md`: the manifest schema, the state namespace, the event
  and state API, and the trust model.
- `docs/mod-events.md`: the event catalogue, the state API and what a mod may
  write.
- `docs/mod-implementation.md`: the event and state implementation plan, the
  declared-set run-time check, and the build order.
- `docs/mod-rules.md`: the rule substrate and the four constraints of in-game
  management.
- `docs/modding.md`: the overview of the mod surface.
- `docs/mod-abi.md`: the entry points and the service table.
- `sdk/save.h`, `sdk/save.c`: the save folder and the atomic whole-file write.
- `sdk/vfs.c`, `sdk/vfs.h`: the mod list, the load order and the manifest
  reader.
- `sdk/mod_slot.h`, `sdk/mod_slot.c`: the `(region, address)` registry the
  declared table and the install join.
