# The installer

eidos ships no game data. On first run the player gives it their own dump of
Pokemon Black and the installer turns it into the data root the engine reads.
This document has two halves: the extractor core in `install/` and its
command-line front end `eidos_extract`, and first run: where the data root
lives on each platform, how the engine decides it is playable, and the
setup window that asks for the ROM when it is not.

## The extractor core

`install/` is plain C99 and the standard library, plus one small file of
operating-system calls (`install/fsutil.c`, Win32 and POSIX behind one
`#ifdef`). It has no SDL and nothing of the game, so every front end links
the same code and shows the same messages.

| Function | What it does |
|---|---|
| `bw_rom_identify` | header fields, header checksum, declared capacity, a streamed SHA-1 of the whole image (and of the image padded back to capacity when it is trimmed) |
| `bw_rom_verdict` | the decision and the message for the player (below) |
| `bw_rom_extract` | the extraction, staged and renamed into place |
| `bw_*_utf8` | file calls taking UTF-8 paths on every platform |

`eidos_extract [--any] [--replace] <rom.nds> [out_dir]` is the command-line
front end: identify only without `out_dir`, identify then extract with it.

### Verified on the real ROM

Run against the dump aletheia is built against (SHA-1
`26ad0b9967aa279c4a266ee69f52b9b2332399a5`, 268,435,456 bytes) and compared
file by file (size and SHA-1) with aletheia's own `extract/` (written by
`tools/ndsrom.py extract` and `tools/delink.py`):

- **726 files written, 726 identical**: 247 NitroFS files, 237 overlay
  files, 229 decompressed overlays, `arm9_dec.bin` (681,920 bytes), the
  header, header extension, ARM9, ARM7, FNT, FAT, `y9.bin`, the banner and
  the four DSi regions.
- **Nothing missing**: every file of aletheia's tree is written, except the
  four listed under "Intended differences".
- Time on this machine (Windows 11, MinGW-w64 GCC 14, warm file cache): about
  3 s to identify (hash) and 3 s to extract with an optimised build, 8 s in
  total unoptimised. A front end that has already identified the image passes
  its hash in, so the extraction does not hash it again.

`test_extract_rom` repeats this on every `ctest` run where the environment
variable `BW_ROM` names the ROM: it runs `eidos_extract` into a folder named
`extract é€ ü` (spaces and characters outside ASCII and Latin-1), compares
every written file with the tree that holds `BW_DATA_ROOT` (its parent
directory), checks nothing expected is missing, checks a second run is a
no-op and that no staging directory is left, then deletes the extraction.
The expected hashes are computed from that tree on each run, so no hash of
any game file is written into the repository. Without `BW_ROM`, or without
the reference tree, the test reports itself skipped.

`BW_ROM` can also be given once, at configure time (`-DBW_ROM=<path to the
ROM>`): the build then passes it to `test_extract_rom`, `test_firstrun_rom`
and `test_wizard`, so they run on every `ctest` without the environment
variable. `-DEIDOS_TEST_DIALOGS=ON` does the same for `test_picker`.

### Intended differences from aletheia's extract

| aletheia writes | eidos | why |
|---|---|---|
| `manifest.json`, `residue/` | not written | `ndsrom.py`'s rebuild bookkeeping: only needed to turn the tree back into a ROM |
| `arm9i_dec.bin` | not written | the DSi ARM9 extension decrypted; needs a DSi key the player supplies, which the installer does not take yet, and nothing in the port reads it |
| `ov230d.bin` | not written | overlay 230 after its own decryptor, a view for aletheia's disassembler only; `overlays_dec/y9_0230.bin` (the ROM bytes) is written |
| (nothing) | `eidos_manifest.json` | what was extracted from which image, by which installer version |

`overlays_dec/` is written because the port's data tests read it
(`test_overlay` checks overlay 230's static initialisers against it) and
`tools/title_closure.py` does; it is `delink.py`'s step 3 with its checks
(compressed size non-zero and inside the file, the decompressed size equal
to the table's).

### Refusing a wrong game or a bad dump

`bw_rom_identify` refuses outright what is not a cartridge image at all: an
archive (ZIP, 7-Zip, RAR, gzip; "unpack it and choose the .nds file
inside"), a file smaller than a header, a file larger than any cartridge.
Everything else is identified and `bw_rom_verdict` decides:

| Verdict | When | The player is told |
|---|---|---|
| `OK` | the known good dump, or it trimmed of padding only | proceed |
| `NOT_DS` | unknown game code and a wrong header checksum | not a DS cartridge image |
| `OTHER_GAME` | a valid DS header, another game | the title and game code found, and what is needed |
| `UNSUPPORTED` | Pokemon White, or Black of another region | which release it is, and that only IRBO is supported |
| `DAMAGED` | Black with a wrong header checksum, or cut short by more than padding | dump the cartridge again (without trimming) |
| `MODIFIED` | Black, whole, another SHA-1 | damaged, patched (a hack or a translation) or another revision; both hashes shown |

A trimmed dump is accepted when padding it back with 0xFF to the capacity
the header declares (byte 0x14) gives the known hash: trimmers cut only the
0xFF tail, and on this cartridge everything past 0x0C45D400 is 0xFF. Only
`OK` is extracted; `eidos_extract --any` overrides the verdict for testing
(the extraction's own structural checks still apply).

### An existing output folder

The choice is **reuse a finished extraction, never resume a partial one**.
Extraction takes seconds, and a resumed run would have to trust files a
killed run may have half-written; restarting is both simpler and safer.

| `out_dir` is | Result |
|---|---|
| absent, or an empty folder | extracted into |
| an extraction of the same image by this installer version | left alone, success, "already extracted" |
| an extraction of the same image by another installer version | replaced (the layout changed, `BW_INSTALL_VERSION`) |
| an extraction of another image | refused, unless `replace` (`--replace`) |
| a file, or a folder with files eidos did not write | refused; the installer never deletes what it did not create |

"Finished" means `eidos_manifest.json` is present: it is the last file
written, and it records the installer version and the image's SHA-1.

### Atomic output

Everything is written into `<out_dir>.partial`, a sibling (so the final
rename never crosses file systems), starting with a marker file
`.eidos_partial` and ending with the manifest. The marker is then removed and
the folder renamed to `out_dir`; when replacing, the old extraction is first
renamed to `<out_dir>.old` and deleted after the swap (restored if the swap
fails). On any error the staging folder is deleted. A staging or set-aside
folder left by a killed process is recognised by its marker or manifest and
removed on the next run; one that is not recognisably eidos's is refused,
never deleted.

### Paths, progress and cancelling

Paths are UTF-8 everywhere. On Windows they are converted to UTF-16 for the
wide API, and long ones are made absolute with the `\\?\` prefix so the
260-character limit does not apply; `eidos_extract` reads its arguments
from the wide command line. Renames and deletes on Windows retry for up to
two seconds on sharing errors, which antivirus scanners and OneDrive cause
on files that have just been written. Removal never follows a link or
junction.

Progress is a callback: bytes for the hash (`what` = "hashing"), KiB for
the extraction with the file just written. The callback returns 0 to go
on; anything else stops the hash or the extraction at the next block or
file, and the call returns `BW_CANCELLED` (-2) with nothing left behind
(the staging folder is removed as on any error). `bw_free_space_utf8`
gives the free bytes on the volume that holds, or would hold, a path.

### Not done in the core yet

- Reading the image from an open descriptor rather than a path, for
  Android's content URIs (see below).
- Per-file hashes in the manifest, so the engine can verify its data
  without the ROM.
- White, other regions, other revisions: one line each in the known-dump
  table once the port supports them.

## First run

Built on top of the core, in two layers:

| File | What it is |
|---|---|
| `install/setup.c`, `setup.h` | where the data root lives, whether it is playable, the config file, `--rom`, `--reset-data`, `--move-data`, the headless answer. No SDL |
| `install/location.c` | the operating-system half: the known folder, the executable's folder, the environment and command line as UTF-8 |
| `install/picker.c`, `picker.h`, `picker_mac.m` | the platform's own file dialog. No SDL |
| `engine/platform/wizard.c`, `wizard.h` | the setup window (SDL) and `eidos_firstrun`, the one call a front end makes |
| `engine/platform/font.c` | the window's 5 x 7 bitmap font, drawn for eidos (printable ASCII; Latin-1 letters shown as their base letter, anything else as a box) |

A front end passes its arguments through `bw_setup_parse_arg`, then calls
`eidos_firstrun`, which either hands back the data folder to pass to
`bw_set_data_root` or says to exit. `demo/eidos_view` does exactly this,
so it now starts the way the game will.

### The flow

1. **Resolve** (`bw_setup_resolve`): `--data-root`, then `BW_DATA_ROOT`,
   then the folder saved in the config file, then the platform default.
   A path given either way may name the extraction folder (the one with
   `eidos_manifest.json` and `data/`) or its `data/` folder (what
   `BW_DATA_ROOT` has always named).
2. **Check** (`bw_setup_check`): playable when the manifest names this
   installer version and the known good dump, and `data/a` and
   `arm9_dec.bin` are there. A folder named explicitly (`--data-root`,
   `BW_DATA_ROOT`) is trusted: the NitroFS tree being there is enough, so
   aletheia's own `extract/` (no eidos manifest) and an `--any` extraction
   both run. The states and their sentences for the player: missing (or an
   empty folder), incomplete (someone else's files, or no finished
   extraction), made by another installer version, made from another image,
   files missing.
3. **Act** (`bw_setup_prepare`): `--reset-data` and `--move-data` run and
   exit; `--rom <file>` installs without a window (progress on stderr) and
   continues; an older installer's extraction is made again from the
   remembered ROM when there is no window. Otherwise, when the data is not
   playable: with `--no-window` it prints where it looked, why, and the
   `--rom` / `eidos_extract` commands that would fill it, and fails; with a
   window, the wizard opens on that folder.
4. **The wizard** (`eidos_wizard_run`), one SDL window, 512 x 320 shown at
   an integer scale (the largest of 1 to 3 that fits the screen):
   - *Welcome*: what is needed and why (a ROM, nothing downloaded, the ROM
     not copied), where the data will go, "Choose ROM file..." and Quit;
     dropping the file on the window does the same. When setup opened
     because of something other than missing data, the reason comes
     first.
   - *Checking ROM*, then *Extracting game data*: a progress bar
     driven by the worker thread and a Cancel button (Escape).
   - *Refused*, with a headline per case and the core's message under it:
     "Cannot use this file" (an archive, too small, too large),
     "Not a DS ROM", "Wrong DS game", "Wrong game version" (the other
     game), "Unsupported ROM" (another region), "Corrupted ROM dump",
     "Modified ROM" (modified, patched or another revision, both hashes
     shown), "Not enough free space", "Failed to write game data",
     "Cancelled". One button: "Choose another file...".
   - *Ready to play*: where the data is, which ROM it came from; Start.
   Every button has a key: Enter (the focused button), Escape (quit or
   cancel), Tab or the arrows (move focus), O (choose a file). The mouse
   highlights and clicks. There is no override for an unsupported image in
   the window (`eidos_extract --any` stays a developer tool).
5. **Remember**: the extraction folder and the ROM's path go into the
   config file (not when the folder came from `BW_DATA_ROOT`, which is a
   developer's setting), and the data root is checked again before the
   game starts.

The check, the free space test (the ROM's size plus 16 MB, an upper
bound: the extraction is about 80 percent of the ROM) and the extraction
(replacing an eidos extraction of another image, since the player chose
this ROM for this folder) are `bw_setup_install`, shared by `--rom`, the
wizard and `--move-data`. A trimmed dump's extraction records the hash of
the whole dump it was cut from (`bw_setup_identity`), so the check
recognises it as the known dump.

### Where the data goes

The data root is the extraction folder (`data/` inside it is what the engine
opens as `BW_DATA_ROOT`). It is re-derivable from the ROM, so it goes where
an operating system keeps large local application data, never in a roaming,
synced or backed-up location, and never in a cache directory the system may
purge. The eidos home folder holds `extract/` and `config.txt`; the mod
overlay root (see "The file layer" in docs/files.md) is meant to sit beside
them as `mods/`.

| Platform | eidos home (`bw_os_app_home`) | Notes |
|---|---|---|
| Windows | `%LOCALAPPDATA%\eidos` | `SHGetKnownFolderPath(FOLDERID_LocalAppData)`; Local, not Roaming, so 210 MB do not roam |
| macOS | `~/Library/Application Support/eidos` | from `HOME`, which is the app container when sandboxed, so this is what `NSApplicationSupportDirectory` returns there too |
| Linux | `$XDG_DATA_HOME/eidos` (default `~/.local/share/eidos`) | a relative `XDG_DATA_HOME` is ignored, as the spec says; Flatpak and Snap set it to their own per-app folder |
| Android | `Context.getFilesDir()/eidos` | app-private internal storage; `eidos_firstrun` sets it from `SDL_AndroidGetInternalStoragePath` through `bw_setup_set_home` |
| iOS | `Library/Application Support/eidos` in the app container | same code as macOS (`HOME` is the container); marking it `NSURLIsExcludedFromBackupKey` is still to do |

Before the platform default come, in order: a folder a front end set with
`bw_setup_set_home`, the `EIDOS_HOME` environment variable (the tests use
it, so they never touch the player's real folder), and **portable mode**:
a `portable.txt` beside the executable makes the executable's folder the
home, for a USB stick (Windows, Linux through `/proc/self/exe`, macOS
through `_NSGetExecutablePath`).

The config file is `key=value` lines, written to a temporary file and
renamed into place: `data_root=` and `rom=` (the extraction a start plays
when nothing else names one, and the ROM it came from) and, for an install
that holds both games, `played=` (the folder played last), `game.black=`
and `game.white=` (the folder each game was last played from) and one
`rom.<game code>=` a cartridge. Only a standalone game follows `played=`
and only a standalone game writes it: a start with a profile folder of its
own (the launcher's) that names no data folder reads `data_root=`, which
playing the other game never moves. It is kept in the home
folder on every platform, Linux included: it holds only machine-local
paths, which the XDG rules put with data and state, not with configuration.

### Both games in one install

One install can hold Pokemon Black and Pokemon White (`install/setup.h`,
"The two games"). Each cartridge's data has its own folder: English Black's
is `<home>/extract`, as it always was, and any other cartridge's is
`<home>/extract-<game code>` (`extract-IRAO` is English White), the names
the launcher gives them. The game a folder holds is read from the
`header.bin` the extractor wrote there, never from the folder's name, so a
folder from an older eidos stays where it is whatever it holds.

- `--game black` or `--game white` (also `--game=black`) starts that game.
  Without it a standalone game plays the game played last. With its data missing, a
  window asks for that game's ROM; with `--no-window` the start says what
  is missing and ends with status 1.
- `--rom PATH` (also `--rom=PATH`) makes the data of the ROM's cartridge in
  that cartridge's own folder. A second game's ROM never replaces the first
  game's data. `--no-window --rom PATH` makes the data and ends, status 0
  on success, without starting the game.
- A folder named for one run (`--data-root`, `BW_DATA_ROOT`) is used as it
  is; `--game` with a folder of the other game is refused.

While a setup step runs for a launcher that sets `RECOMPLAUNCHER`, each
whole percent is also one line on the standard output,
`progress <0 to 1> <text>`: the check of the ROM is the first half, the
writing of the data the second. The last line is always
`progress 1.00 The game data is ready`: a folder that held that ROM's data
already has nothing written, so the lines before it stop at the half.

### Resetting and moving

- `--reset-data` deletes the extraction at the resolved folder and clears
  the saved setting; the next start asks for the ROM again. Only an eidos
  extraction (it has `eidos_manifest.json`), an empty folder, or the
  installer's own leftovers (`.partial` with its marker, `.old`) are
  deleted; a folder with anything else in it is refused and left alone.
- `--move-data <folder>` moves the extraction to a new or empty folder
  and saves the new place. On one volume it is a rename; across volumes
  the extraction is made again there from the remembered ROM, and the old
  one is deleted afterwards. Without a remembered ROM it says to use
  `--rom <ROM> --data-root <folder>` instead.

Both exit when done. The window has no buttons for them yet.

### Where the saves go

The player's own files are the opposite of the data root: small, not
re-derivable, worth backing up and worth following the player to another
machine. They go where the operating system keeps per-user application
data that is backed up (roaming on Windows), never beside the data root and
never in a cache.

| Platform | Save folder | Notes |
|---|---|---|
| Windows | `%APPDATA%\eidos\saves` | Roaming: a few hundred KiB that should follow the player. `portable.txt` beside the executable switches to `<exe dir>\saves`, as for the data root |
| macOS | `~/Library/Application Support/eidos/saves` | inside the app container when sandboxed |
| Linux | `$XDG_DATA_HOME/eidos/saves` (default `~/.local/share/eidos/saves`) | |
| Android | `Context.getFilesDir()/eidos/saves` | app-private; the front end passes it to `bw_set_save_dir`, because only the Java side knows it |
| iOS | `Library/Application Support/eidos/saves` in the app container | backed up (not excluded, unlike the data root); set by the front end through `bw_set_save_dir` |

Resolution order (`sdk/save.h`): `bw_set_save_dir` (a front end, a
`--save-dir` option, the tests), then the `BW_SAVE_DIR` environment
variable, then the platform default above. With none (a mobile build whose
front end has not set it yet) the game runs on in-memory state and says so.

A game started without the launcher on a computer does not play from the
platform default any more: it plays from the launcher's two game profiles,
`profiles/Black` and `profiles/White` beside the save folder above, each
with its own `saves/`, `mods/` and `settings.ini`
(docs/distribution.md, "A standalone game"). The platform default is where
a plain start of an older eidos kept its saves, and they are copied into
their game's profile once.

The folder holds three files, each written whole to a sibling `.tmp`,
synced and swapped in, so a crash leaves the old file or the new one:

| File | What | Written |
|---|---|---|
| `pokeblack.sav` | the cartridge's 512 KiB backup flash, a raw image (the format emulators use; a DeSmuME `.dsv` trailer is ignored on load) | one second after the game's last write, at power-off, at exit |
| `firmware.bin` | the console's 256 KiB firmware flash (header, user settings, Wi-Fi settings) | at the end of each firmware write |
| `firmware.ini` | the console's user settings, human-editable (`docs/firmware.md`) | only when asked for (`bw_firmware_write_default`); the port only reads it |

### Picking the ROM, per platform

SDL3's file dialog answers on a callback, so `install/picker.c` calls each platform's own,
behind `bw_pick_rom` (filtered to `.nds`, with "All files" offered, since
dumps arrive with other extensions). Every platform also takes the file
dropped on the window (`SDL_DROPFILE`) and `--rom`, so a missing dialog is
a message, never a dead end.

| Platform | Picker | State |
|---|---|---|
| Windows | `IFileOpenDialog` (COM), falling back to `GetOpenFileNameW`; a UTF-16 path converted to UTF-8 | built; `test_picker` opens it and answers it (below) |
| macOS | `NSOpenPanel` in `install/picker_mac.m`, compiled only on Apple (`enable_language(OBJC)`), not restricted to `.nds`; a sandboxed app keeps read access to the chosen file for the life of the process | written, not compiled here (no macOS SDK on this machine) |
| Linux | the XDG desktop portal's `FileChooser.OpenFile` over D-Bus, through `libdbus-1` loaded with `dlopen` (neither the build nor a machine without D-Bus needs it; the request path is predicted from the bus name and a handle token so the Response match is in place before the call), then `zenity`, then `kdialog` | written; syntax-checked against stand-in headers, not run |
| Android | Storage Access Framework, `ACTION_OPEN_DOCUMENT` | deferred: `bw_pick_rom` says unavailable. The plan: the Java side starts the intent, takes the `content://` URI, opens it with `ContentResolver.openFileDescriptor`, and hands the detached fd to a core entry point that reads from an open `FILE *` (`fdopen`), which is the one core change it needs |
| iOS | `UIDocumentPickerViewController` (open in place) | deferred, as Android: the picker returns a security-scoped URL; between `startAccessingSecurityScopedResource` and `stop...` its path opens with `fopen`, so no core change is needed, only the Objective-C picker beside `picker_mac.m` |

SDL 3's `SDL_ShowOpenFileDialog` covers Windows, macOS and Linux with one
call; when the platform layer moves to SDL 3 it can replace the three
desktop branches.

### When the data is missing during play

A file missing during play (deleted by hand, a failing disk) should stop
with a message naming the file and telling the player to run setup again
(`--reset-data`, then start), not crash inside the game's code; that is
the file layer's job and not built yet. The wizard writes only into the
data root (through the staged rename) and the config file; the player's
saves live elsewhere and are never touched by an installation, a reset or
a move.

### Testing it

- `test_dataroot` (no game data, every configuration including MSVC): the
  locations, the config round trip with a non-ASCII path, both spellings
  of a data root, every check state trusted and not, the option parser,
  the resolution order, the headless install of the synthetic cartridge
  (refused as modified with nothing written, an archive refused, cancelled
  half way with nothing left, installed with the test override and
  remembered, a second run "already"), free space, move and reset (and
  their refusals), `bw_setup_prepare` without a window.
- `test_wizard` (SDL, `SDL_VIDEODRIVER=dummy`): the window driven by
  scripted events through its `script` hook, the native dialog replaced by
  its `pick` hook. An archive dropped and refused; a mouse click on
  "Choose another file..."; the synthetic cartridge checked and written
  (test override) to a folder named outside ASCII; Start; the same image
  refused without the override; Escape during a check; closing the window;
  a system with no dialog. It checks the headline and message of each
  screen, that every frame has text on it, and writes a BMP of each screen
  (`EIDOS_TEST_KEEP=1` keeps them). With `BW_ROM` set it also drops the
  real ROM on the window, with no override, and checks the result is
  playable.
- `test_firstrun_rom` (`tests/firstrun_rom.cmake`, `BW_ROM`): the whole
  headless path through `eidos_view` in a scratch eidos home: `--rom`
  installs and the demo draws from the new data, a plain start finds it
  through the saved setting, `--move-data`, `--reset-data`, and a start
  with no data and no window fails with the message.
- `test_picker` (`EIDOS_TEST_DIALOGS=1`, Windows): `bw_pick_rom` opens the
  real dialog; a helper thread types the full path of a file named outside
  ASCII into it and presses Open, then closes a second one: `BW_PICK_OK`
  with the UTF-8 path, then `BW_PICK_CANCELLED`.
