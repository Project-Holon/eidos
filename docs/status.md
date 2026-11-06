# Port status: done, and what is left to port

docs/milestones-detail.md's "Planned Milestones" is the detailed checklist, grouped by
milestone. This file is the same work grouped by what it waits on. aletheia's
decompilation is code complete: the whole game's code is matched (2,183,286
of 2,183,302 bytes, 99.9993%), 35,520 functions with none missing and none
doubled, 98.99% of game code and 99.20% of all code fully linked, and 181 of
243 regions link byte-identical. 33,524 matched translation units sit under
`src/matched/` (4,117 in `arm9`, 24,482 in `ov/`, 3,296 in `ov_sdk`), with
matched files in 178 of the 237 overlays. The port no longer waits on
matching; it waits on port integration, on getting that matched C to compile
and run here.

Maintained by hand: move an item when its status changes, in the same commit
that changes it. Where an item is only partly done, it says which part.

## Done

- **The game's own boot, to the title menu**: NitroMain and the title-path
  overlays (ov009, ov010, ov011, ov013, ov017, ov088, ov179, ov228) run
  natively: logos, copyright, the opening, the title screen and the title
  menu, in a window or headless (`eidos_boot`, `test_boot_run`,
  `test_boot_window`; `docs/boot.md`, `docs/boot-handoff.md`).
- **engine/g2d**: OBJ and BG compositors, affine OBJ and BG, windows and the
  colour special effects. Pixel-identical to aletheia's Python renderers on
  the oracle sets (`test_g2d`, `test_bg`, `test_bg_affine`, `test_window`).
- **engine/g3d**: fx32 geometry pipeline, matrix stack, display-list
  interpreter, all seven texture formats, NSBMD/NSBTX containers, the MDL0
  shape dictionary and node dictionary (record layout, translation, full
  rotation, scale, parent/child tree, shape binding; the **packed rotation
  completion is provisional**, see `docs/g3d-nodes.md`), and a software
  rasteriser with synthetic tests. Geometry oracle-exact; the rasteriser is
  not oracle-checked (no DS frame exists in the tree).
- **engine/audio**: SDAT, SBNK, SWAR/SWAV, SSEQ interpreter, 16-channel
  mixer. Bit-identical to aletheia's `sseqsynth.py` on three sequences
  (`test_audio`).
- **engine/frame and engine/platform**: the vblank frame loop with the DS OS
  tick, and the SDL3 window, input, audio device and pacer.
- **sdk/**: the SDK-name shims (`fs`, `mi`, `os_alloc`, `os_irq`, `gx`,
  `os_tick`) plus the port's own `narc`, `vfs`, `game_globals`, `aliases`
  and `panic`.
- **Milestone 6, file layer**: `sdk/vfs.c` owns the data root and a
  manifest-ordered list of mod roots and resolves a NitroFS path through
  them (`test_vfs`); `sdk/vfs_narc.c` applies member-level NARC overrides
  (replace or add members, later mod wins) as one merged archive every
  reader sees, the matched ones included (`test_vfs_members`).
- **Milestone 4, 2D/3D integration**: `G2dLayer3d` puts the 3D output inside
  `g2d_frame_stack` as an ordinary layer, so a 2D BG can sit in front of or
  behind it, a window can hide it, and the colour effects blend with a 3D
  pixel. `g3d`-independent (the layer is a raw RGBA buffer); `test_compose3d`
  is the synthetic check. `g3d_compose_over` remains for the backdrop case.

- **The game's own heap, down to the arena** (ILP32): aletheia matched the
  heap constructor (0x0202FC48) and the expanded heap; they run as written
  under `EIDOS_HEAP_MATCHED` (`test_heap`, 206 checks). Two EXACT files
  (`func_02060888`, `func_02060E30`) depend on mwccarm's frame layout and
  are transcribed in `stubs/`; worth fixing on the aletheia side.
- **Archives by id**: `unit_02048DDC` is in; the game's readers open
  archives through the name table and the file layer, mod overlay first
  (`test_arcid`).
- **First-run extractor core** (`install/`, `eidos_extract`): identical to
  aletheia's ndsrom.py + blz.py on a synthetic cartridge (`test_extract`)
  and, file for file, to aletheia's extract/ on the real ROM (726 of 726,
  `test_extract_rom` with `BW_ROM` set); refuses wrong games and bad dumps
  with a message, atomic output, existing-folder rules
  (`docs/installer.md`).
- **Builds with no game data**, and `tools/check.py` runs GCC and Clang at
  both pointer widths, ASan+UBSan and no-SDL, warning-free.
- **The mod layer**: the whole of it is built. `sdk/mod_api.h` and
  `sdk/mod_state.h` are the frozen surface a mod compiles against: the
  events and their payloads, the state and service tables, the host table and
  the entry points. The interface version is 5 (version 3 and 4 mods still load) and the surface hash is
  0xF31D7215 at 32 bits and 0xC25CE7CF at 64.
  `sdk/mod_event.c` is the dispatch (the binding table, the fan-out, the
  refusals and the scope stack) and the five area files hold the
  rows; `sdk/mod_state.c` the state calls over the game's own accessors;
  `sdk/mod_manifest.c` the manifest record, the parse, the namespace rules,
  the declared-address check and the apply; `sdk/mod_savedata.c` a mod's own
  saved data; `sdk/mod_loader.c` the loader. The services are
  `sdk/mod_message.c` and `sdk/mod_text.c` (the messages), `sdk/mod_timer.c`
  (the timer, the splits and the mod's own random source), `sdk/mod_export.c`
  (a Pokemon's export and import), `sdk/mod_net.c` (the peer sessions and the
  external control), `sdk/mod_frame.c` (the frame tick), `sdk/mod_avatar.c`
  (the field avatars), `sdk/mod_restart.c` (the run restart),
  `sdk/mod_handoff.c` (the save handoff), `sdk/mod_panel.c` (the run-status
  panel), `sdk/mod_input.c` (the input injection) and `sdk/mod_menu.c` (the
  mod menu's model, which the settings screen draws). `sdk/mod_script.c` and
  `stubs/reimpl_vm_control.c` are the script command hooks, and
  `sdk/mod_rule.c` the rule interpreter. The tests are the
  `tests/test_mod_*.c` files. `docs/modding.md` is the overview.

## Open, no aletheia dependency

- **The installer front ends**: the setup window and the core behind it are
  in (`install/setup.c`, `engine/platform/wizard.c`, `docs/installer.md`):
  the ROM picker per platform, the progress callback and Cancel, the
  data-root location per platform, the behaviour without data, and the
  free-space check (`bw_free_space_utf8`). Still open: the Android
  descriptor entry point for content URIs, the Linux and macOS picker
  branches (syntax-checked only; no Linux or macOS machine here), and the
  optional DSi BIOS input.
- **The Pokemon record layer** (`func_02017E1C`/`E40`, `func_02018598`) is
  matched in aletheia; `tools/closure.py` reports its closure as 182 files
  with 2 open functions and 47 ARM9 data names. It needs game data to test
  meaningfully.

- **Milestone 4, 3D gaps**: the texture matrix (`TEXIMAGE_PARAM` bits 30-31)
  is unimplemented, and so are shadow polygons (no matched `POLYGON_ATTR`
  constant selects them). Lighting, fog, edge marking, anti-aliasing, the
  rear-plane bitmap and the w-buffer are done on the hardware path; the
  lighting's fixed point and the anti-aliasing's coverage rule are marked
  approximations (`engine/g3d/light.c`, `engine/g3d/raster.c`).
- **Milestone 4, 2D gaps**: bitmap OBJs, BG mode 6, display mode 3 and
  capture from main memory are flagged, not drawn; the matched code sets
  none of them as a constant. The 3D alpha blend, mosaic, the display
  capture and the 6-bit master brightness are done
  (`engine/g2d/hwcompose.c`).
- **Milestone 4, per-shape display-list preamble**: each shape's declared
  range opens with a short non-command preamble the loader locates by a
  self-consistency scan. Cracking it removes the heuristic.
- **Milestone 4, rasteriser oracle gap**: not decompilation, but there is no
  DS frame in the tree to compare a picture against, so the rasteriser's two
  approximations (affine per-span texture interpolation, a conventional
  top-left fill rule) stay unverified. A frame captured from an emulator
  would close it.
- **Milestone 1, portability sweep**: the under-declared-globals sweep.
  MSVC is done: `msvc-x64`, `msvc-x86` and `clangcl-x64` build warning-free
  at `/W4` and pass every test with game data, MinGW at both widths too;
  CRT dialogs are off in every MSVC executable (`sdk/plat_nodialog.c`).
  Still open: GCC and Clang on a real Linux machine (and macOS), which
  `tools/check.py` covers but nobody has run there yet. SDL3 is wired up
  under MSVC too, through the `vc` devel zip (docs/building.md "Building with MSVC").
- **Milestone 5, oracle breadth**: more sequences in the audio oracle is
  mechanical coverage, not accuracy.
- **Milestone 8+**: high-resolution/widescreen rendering, packaging.

- **The mod layer's remaining gaps**: the relay server is not part of the
  port, and one marked payload field has no applier (`mon.nickname`'s
  `force`: the game has no call that forces the nickname prompt, so the
  port has no hook at that decision). The rest of the layer is in place: the
  Mods section of the settings screen (`engine/platform/settings_ui.c`) draws
  the model behind it (`sdk/mod_menu.c`), the player can reorder their mods
  and restart the run from it, the Session section of the same screen hosts a
  session, shows the code the relay hands out, joins one from a typed code,
  leaves one and lists the players of it, the run-status panel is shown beside
  the screens and the player can hide it, the input injection takes a mod's
  press, the relay's address and the control's port are settings of the port's
  own, and a save that arrives from a peer is put to the player before it is
  installed.

## Open, port integration

- **Milestone 2, the wider boot closure**: the heap constructor and the
  boot's own init (`sub_0200545C`: heaps, child heap 7, the archive table)
  are matched and in, and the title-path overlays compile into the boot,
  which reaches the title menu. What is left is the rest of the closure and
  the field and battle overlays: the code is matched, it is not wired into
  the port.
- **Milestone 3, opening cinematic**: it runs and is drawn from the
  hardware state the game itself writes (ov179, `docs/boot.md`). What is
  open is a pixel comparison against a DS capture, plus the documented
  compositor approximations.
- **Milestone 5, audio accuracy**: the ADSR curve, the volume and pan laws,
  Tie, portamento, modulation and the pitch wheel are documented
  approximations of the hardware's; correcting them needs the DS sound
  driver, which is ARM7 and outside aletheia's ARM9 decompilation.
- **Milestone 5, the game's own sound calls**: the matched ARM9 code calls
  the SND library, but the boot does not run it yet, so `eidos_view` still
  chooses the SEQ id. Wiring those calls into `sdk/arm7/` is port work.
- **Milestone 7, gameplay**: field system (movement, warps, script VM,
  encounters), battle, menus, save. All of it is matched; none of it is
  wired into the port yet, which is the bulk of the remaining work.

## Where to look next

The decompilation no longer gates the port; the work is integration. The
next step past the title menu, and what each one took, is in `docs/boot.md`
("How far it gets") and `docs/boot-handoff.md` (the state, the frames
reached, the open items in order). `docs/overlays.md` covers compiling more
overlays into the executable.

## Past the title menu

- **NEW GAME**: loads ov204 (new-game intro) and ov194 (name entry). Both
  are matched now (123 and 148 functions); neither is compiled into the boot
  yet, so the load has nothing to run.
