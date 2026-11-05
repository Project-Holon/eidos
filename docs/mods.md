# Mods

This is the user-facing guide to the data layer: how to lay a mod over the
game's own extracted files, and what a mod can replace. The design account is
docs/files.md, "The file layer"; `docs/modding.md` is the overview of the whole mod
surface. Every rule below is attributed to the code that enforces it.

A mod is a directory laid out like the data root. The port reads the data
root and every mod root through one resolver, so a mod never edits the
game's files: it adds a layer the resolver prefers.

## The layering model

There is one base layer, the **data root**, and zero or more **mod roots**
over it in a **load order**. A later layer wins.

| Layer | Where it comes from | Set by |
|---|---|---|
| data root | the extraction's `data/` tree (`sdk/vfs.c` default is `extract/data`) | `bw_set_data_root`, else `BW_DATA_ROOT`, else the compiled-in default (`sdk/port.h`, `sdk/vfs.c`) |
| mod roots | zero or more directories laid at the same relative layout | `bw_set_mod_root`, `bw_add_mod_root`, else `BW_MOD_ROOT` |

The layers are checked in reverse load order, so the latest mod holding a
file wins (`sdk/vfs.c`, `bw_vfs_file_layer`). With no mods the resolver
answers from the data root alone.

The mod list is in load order: index 0 loads first and wins least, the last
index wins most. The order is the list order, re-sorted by each mod's
manifest `order` key (`sdk/vfs.h`, "The mod list"). The player may choose
another order: the mod menu's model keeps it and hands the list to the vfs
as it is (`bw_mod_set_order`), and the player's own record carries it
(`<save dir>/mods.ini`, an `order` key per mod). A mod the record does not
name keeps the manifest's order and comes before the mods the player did
order, so a mod the player has never seen does not take a win away from one
they have.

| Function | Declared in | Returns |
|---|---|---|
| `bw_mod_count` | `sdk/vfs.h` | number of mod roots |
| `bw_mod_root_at(i)` | `sdk/vfs.h` | the root of mod `i` in load order, or NULL |
| `bw_mod_name_at(i)` | `sdk/vfs.h` | the manifest name, or the directory name |
| `bw_mod_order_at(i)` | `sdk/vfs.h` | the manifest order value, else 0 |
| `bw_mod_root()` | `sdk/port.h` | the root that wins most (the last in load order), or `""` with no mods |

Every change to the roots drops the cached merged archives, so a tool that
changes the list at run time sees the new view at the next open
(`bw_vfs_flush`, called by `bw_set_mod_root`, `bw_add_mod_root`,
`bw_clear_mods` and `bw_set_data_root`).

## The per-mod manifest

A mod root may hold `eidos_mod.txt`. A missing manifest is not an error: the
defaults apply (`sdk/vfs.c`, `read_manifest`). The format is `key = value`
lines.

| Key | Value | Default |
|---|---|---|
| `name` | a display name | the directory name |
| `order` | a signed integer, the load order key | `0` |

These are the keys the data layer reads. A mod with code or rules adds more
keys (`api`, `port`, `code`, `state`, `override`, `rule`, `settings`), which
the mod loader reads (`docs/mod-interface.md`, section 3).

Rules (`sdk/vfs.c`, `read_manifest`):

- `#` starts a comment: the rest of the line, from the `#` on, is dropped.
- A line with no `=` is ignored.
- A key the data layer does not read is ignored here, so the loader's keys
  and later keys do not disturb it.
- `name` needs a non-empty value; an empty one leaves the default.
- `order` is parsed as a decimal integer in `-1000000` to `1000000`. A value
  that is empty, not a number, or out of that range is reported on stderr and
  ignored, so the default `0` stands rather than a guessed value.
- Mods load in ascending `order`. Mods with equal `order` keep the order
  they were added in.

Example:

```
# First mod
name  = Fairy type
order = 10
```

## Setting the roots

| Call | Declared in | What it does |
|---|---|---|
| `bw_set_mod_root(path)` | `sdk/port.h` | replaces the whole list with the roots in `path`, which may name several separated by `;`; NULL or `""` clears the list |
| `bw_add_mod_root(path)` | `sdk/vfs.h` | appends one root and re-sorts, returns 0 or -1 (empty path or no memory) |
| `bw_mod_set_order(roots, count)` | `sdk/vfs.h` | replaces the whole list with the roots in the order given, without sorting them by the manifest's `order` key: this is the order the player chose, and it is the order the mods load in |
| `bw_clear_mods()` | `sdk/vfs.h` | clears the list |

The separator is `;` on every host, because a Windows drive letter uses `:`
(`sdk/vfs.h`). Each entry is trimmed; an empty entry is skipped
(`sdk/vfs.c`, `set_mod_list`). A trailing separator on a root is stripped,
so resolution never produces a doubled slash (`copy_root`).

`BW_MOD_ROOT` in the environment is parsed the same way, as a `;` separated
list. It is read the first time the list is consulted, unless a call set the
list first: an explicit call wins over the environment, and after the first
consult the environment is not read again (`sdk/vfs.c`, `mods_init`).

```
BW_MOD_ROOT=mods/fairy-type;mods/qol eidos_boot --window
```

In C:

```c
bw_set_mod_root("mods/fairy-type");      /* one mod */
bw_set_mod_root("mods/a;mods/b");        /* a load order, then a clear */
bw_set_mod_root(NULL);                   /* off again */
```

`BW_DATA_ROOT` works the same way for the data root: it wins over the
compiled-in default, and an explicit `bw_set_data_root` call wins over both
(`sdk/port.h`).

## Whole-file overrides

A mod replaces a game file by holding a **regular file** at the same
NitroFS-relative path. A path resolves to the latest layer holding a regular
file there, the data root last.

| Call | What it answers |
|---|---|
| `bw_data_path(out, n, rel)` | the resolved path, joined to the winning root |
| `bw_vfs_file_layer(rel)` | which layer holds a regular file: `-1` for the data root, `0` to `bw_mod_count()-1` for a mod, `BW_LAYER_NONE` (`-2`) when no layer does |
| `bw_data_exists(rel)` | 1 if the resolved path is a readable file, else 0 |

Two points the code makes:

- The check is for a *regular file*, not merely something `fopen` accepts. A
  directory at an archive's path is a member override (below), not a
  replacement file (`sdk/vfs.c`, `bw_vfs_file_layer`).
- `bw_data_path` writes the resolved path even when the file is absent; it
  does not fail on a missing file. `bw_data_exists` is the reader that tells
  the two apart (`sdk/port.h`).

A mod can also add a file the game does not have, for a port feature that
reads it through the same resolver. The following Pokemon (Extras,
`sdk/follow.c`) read `follow/<species>.bin`, the walking sprite of a Unova
Pokemon, which Black and White do not have: 16 colours (BGR555), then six
32x32 4bpp frames (up twice, down twice, left twice), 3104 bytes.
`tools/follow_pack.py` makes such a mod from the user's own copy of
pokeemerald-expansion's overworld sheets.

## Member overrides

A mod changes one member of a NARC by holding a **directory** at the
archive's NitroFS path. Each file in it named `<index>` or `<index>.<ext>`
replaces that member, or adds it past the archive's end (`sdk/vfs_narc.c`).

The naming rule (`sdk/vfs_narc.c`, `member_index`):

- `<index>` or `<index>.<ext>` with no further dot: `12`, `0012`, `12.bin`,
  `12.nclr`. Leading zeros are allowed; the extension is free.
- Anything else is not a member and is ignored: `README.txt`,
  `12.bin.bak`, a dotfile. A subdirectory whose name looks like an index is
  also not a member.
- Two files naming the same index in one mod is an error. It is reported on
  stderr and the archive fails to open rather than pick one
  (`collect`, `collect_one`).
- The format's limit applies: `BW_NARC_MAX_MEMBERS` is `0xFFFF`, so indices
  `0` to `65534`. A larger index is refused with a message
  (`sdk/vfs.h`, `member_index`).

How the pieces combine (`sdk/vfs.c`, `bw_vfs_build_merged` and `collect`):

- The **latest layer holding a regular file** at the archive's path supplies
  the base archive. A whole-file replacement therefore discards every
  earlier mod's members of that archive.
- Every **later** mod holding a directory at that path contributes members
  on top of it. For one index, the latest mod wins.
- A member index past the base archive's count extends it. An index between
  the base end and a mod's index that no mod fills is an empty member.
- A directory at a path where no layer holds a regular file is just a
  directory, not a new archive. A brand-new archive ships as a whole NARC
  (`bw_vfs_build_merged` returns 0; docs/files.md, "The file layer").
- The base file must be a NARC. Members over a file that is not a NARC are
  an error, reported on stderr (`parse_narc`).

Every reader sees the same archive. The port synthesises the merged archive
as a whole NARC in memory, laid out the way the cartridge's are (header,
BTAF, BTNF, GMIF, 4-byte member alignment padded with `0xFF`), and the FS
shim serves it as a memory file. The matched readers that read the NARC's
own header and tables cannot tell a merged archive from a shipped one
(`sdk/vfs_narc.c`, file header; docs/files.md, "The file layer"). Repacking any
real archive from its own tables reproduces it byte for byte
(`bw_narc_pack`; `tests/test_vfs_members.c`).

The interfaces a tool can call directly (`sdk/vfs.h`):

| Call | What it does |
|---|---|
| `bw_narc_pack(...)` | pack members into a NARC image; a real archive's members with its own BTNF reproduce it byte for byte |
| `bw_vfs_member_overrides(rel)` | how many member files the mods contribute, 0 for none, `-1` for a malformed mod directory |
| `bw_vfs_build_merged(rel, &out, &size)` | build the merged archive from scratch: 1 with a malloc'd image, 0 when no mod contributes a member, `-1` on an error |
| `bw_vfs_acquire(rel, &data, &size, &handle)` | the cached form the FS shim uses; release with `bw_vfs_release(handle)` |
| `bw_vfs_flush()` | forget every cached merged archive |

A merged image is cached per path with a reference count held by the open
files reading it. An open file keeps its image across a change to the mod
list; an image nobody holds is rebuilt after a change (`sdk/vfs_narc.c`, the
cache; confirmed in `tests/test_vfs_members.c`).

## A worked example

The eidos home folder holds the extraction and is the place the installer
means the mod overlay root to sit, as `mods/` beside `extract/` and
`config.txt` (`docs/installer.md`, "Where the data goes"). On Windows that
is `%LOCALAPPDATA%\eidos`.

Data root (the extraction's `data/`):

```
extract/data/a/0/0/2          a plain file
extract/data/a/0/1/6          a NARC (the port's personal-data archive)
```

A mod that replaces the whole of `a/0/0/2`, replaces member 25 of
`a/0/1/6`, and adds member 10007 past its end:

```
mods/first-mod/eidos_mod.txt
mods/first-mod/a/0/0/2                the replacement file, whole
mods/first-mod/a/0/1/6/25             member 25, replaced
mods/first-mod/a/0/1/6/10007.bin      member 10007, added
```

`mods/first-mod/eidos_mod.txt`:

```
# First mod
name  = First mod
order = 10
```

Turn it on with the environment:

```
BW_MOD_ROOT=mods/first-mod eidos_boot --window
```

or in C:

```c
bw_set_data_root("extract/data");
bw_set_mod_root("mods/first-mod");
```

The results:

- `bw_data_path("a/0/0/2")` resolves to `mods/first-mod/a/0/0/2`, because
  that is the latest layer holding a regular file there.
- `bw_vfs_file_layer("a/0/0/2")` is `0`.
- `bw_data_path("a/0/1/6")` resolves to `extract/data/a/0/1/6`. The mod holds
  a *directory* there, not a regular file, so the base archive is the file
  the members apply to.
- `bw_vfs_file_layer("a/0/1/6")` is `-1` (the data root).
- `bw_vfs_member_overrides("a/0/1/6")` is `2`.
- Reading `a/0/1/6` through the FS gives the base archive with member 25 and
  member 10007 replaced, and the neighbour members untouched.

Keep the member files in the game's own format (an NCGR, a compressed blob,
a data record). Converting from a readable format is a tool's job, not the
file layer's (docs/files.md, "The file layer").

## What the tests verify

The two tests are the evidence for the rules above.

`tests/test_vfs.c` (whole-file resolution, 13 checks; no game data). It
writes a base root and one mod root that disagree about one file, then
checks:

- with no mod root, a path resolves under the base root;
- a file in both resolves to the overlay and reads back as the overlay's
  bytes;
- a file only in the base falls through to the base;
- a file only in the overlay resolves to the overlay;
- `bw_data_exists` agrees, absent file included;
- clearing the mod root restores base-only resolution;
- a trailing separator on a root is stripped, so no doubled slash appears.

`tests/test_vfs_members.c` (member overrides and the mod list; the
synthetic half needs no game data, the data half runs when a data root is
given). It builds a base tree and seven mod trees and reads every case three
independent ways: `sdk/narc.c`'s reader plus the matched `func_02049578` on
its handle, the matched readers by archive id (`func_02048DDC`,
`func_02048E34`, `func_02048F4C`), and `FS_OpenFile` of the archive, whose
whole image must equal a NARC packed from the expected member list. Cases:

- no mods, and an empty mod whose member directory holds only non-member
  names;
- one mod replacing a member and adding members past the end, with a hole;
- two mods overriding the same member in both load orders;
- a manifest's `order` and `name`, including that a mod listed first but
  with a higher `order` wins, and that `bw_mod_root()` is that winning root;
- a whole-file replacement superseding earlier member directories, and a
  later member directory applied on top of a whole file;
- two files naming one index in one mod: the member count is `-1`, the
  archive refuses to open;
- member files over a base that is not a NARC;
- a mod directory over a path no layer holds is not an archive;
- an open file keeping its merged image across a mod list change, and the
  repeated-acquire cache returning the same image and handle;
- the `;` list form.

With game data (`argv[1]`), it also repacks every real archive byte for byte,
then replaces member 5 of the real `a/0/1/6` and adds one past its end, and
reads both through the real ARM9 archive table and `func_02048E34` /
`func_02048F4C`, by file id and by ROM range.

Not yet covered by either test:

- `bw_add_mod_root` and `bw_clear_mods` are not called; the tests use
  `bw_set_mod_root` only.
- `BW_MOD_ROOT` is not read; the tests set the list from C.
- `bw_vfs_file_layer`'s return value and `BW_LAYER_NONE` are not asserted
  directly; the tests check the resolved path instead.
- `bw_mod_root_at` and the default manifest name (the directory name) are
  not asserted; the manifest `name` is.
- A malformed or out-of-range `order` value, and an unknown manifest key,
  are not asserted.
- A member index at or past `BW_NARC_MAX_MEMBERS` (`0xFFFF`) is not
  asserted.
- `bw_vfs_flush` is not called directly.
- `bw_data_path` returning a path for a file that does not exist is not
  asserted; only `bw_data_exists` is.

## Limits

- BTAF's member count is a `u16`, so an archive holds at most 65535 members
  (`BW_NARC_MAX_MEMBERS`). This is the cartridge format's limit, not one the
  port adds (`sdk/vfs.h`).
- The ROM FAT the boot copies (`FS_LoadArchiveTables`) still holds the
  cartridge sizes, and a strict sub-range of an archive opened by ROM offset
  keeps the cartridge layout's offsets. No matched caller does either with
  an archive (docs/files.md, "The file layer").
- Paths are narrow strings throughout, as `fopen` takes them; `sdk/plat_dir.c`
  is the one platform file that tells a file from a directory and lists one.
