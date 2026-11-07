# Consoles: the Switch and the Vita

Status (2026-11-07): the Switch program builds (`eidos.nro`) and runs in
Ryubing through the opening; the Vita program builds (`eidos.vpk`) and
stops at its first frame in Vita3K (below, "The Vita build"). This page holds
the plan, what is ready, and the measurements that decide the route. The
research behind it (sources, prior art, open questions) is in the project's
research notes; the facts that matter are repeated here.

## The route

Both consoles take the 64-bit route the desktops use for 64-bit
(docs/lp64.md): the game compiled to wasm32, translated to C by wasm2c, and
linked with a native host (host/). The game's records then keep the DS's
layout on any CPU, and the host is the only part that knows the platform.

What a console changes:

- **Memory checks.** A desktop reserves 8 GiB of address space and lets
  guard pages catch a stray access. Horizon (Switch) and the Vita's kernel
  give neither the reservation nor fault signals. The host option
  `EIDOS_HOST_BOUNDS_CHECKS` builds the same program with an explicit check
  on every access, and the module's memory is one heap block of a fixed
  budget (`EIDOS_HOST_MEMORY_MB`), grown once at start so it never moves
  (host/main.c; the SDL surfaces made from module pixels rely on that).
  It builds and runs on a desktop, so its cost is measured here first.
- **Graphics.** The Switch has OpenGL 4.3 through devkitPro's Mesa
  (nouveau): the OpenGL backend as it is. The Vita has a GLES2-class GPU
  (vitaGL): the 2D presenter fits, the 3D path needs a cut-down backend.
- **The host layer.** Files, threads (fibers: host/fiber.c's AArch64 stack
  switch), sockets, audio and input go through libnx or VitaSDK.  SDL2 is
  the packaged SDL on both (devkitPro `switch-sdl2`, VitaSDK); SDL3's own
  Switch port exists but only for licensed developers.  So the host keeps
  its SDL3 calls and a layer answers them with SDL2 (host/switch/, below).
- **Left out at first:** the launcher, mods (WAMR), multiplayer (mbedtls),
  the crash handler. The ROM is extracted on a PC (or on the console) and
  the data folder copied to the SD card.
- **Layout:** the one-screen layout only (docs/building.md's phone plan:
  touch is the bottom screen, buttons map straight).

## Toolchains

They install inside the Ubuntu WSL distribution, so Windows stays clean.

- Switch: devkitPro's Docker image `devkitpro/devkita64` (devkitA64 15.2,
  libnx 4.12, SDL2 2.28.5, Mesa 20.1 with EGL and GLES, glad).  devkitPro's
  package servers refuse command-line downloads, so the image is the way
  in; it runs under Docker inside the Ubuntu WSL distribution.
- Vita: VitaSDK (`vdpm`), the same way, once the Switch build runs.
- Testing: an emulator first (Ryujinx's maintained fork for the Switch;
  Vita3K for the Vita), then the owner's modded Switch.

## Steps

1. Measure the bounds-checked build on a desktop. Done for Windows x64;
   numbers below.
2. Toolchain. Done (the Docker image above).
3. Compile the wasm2c output and the runtime with devkitA64. Done: the 96
   files of the game compile with no error or warning (39 MB of code).
4. Host layer for libnx with SDL2, packaged as an `.nro`. Done, not run
   yet: `host/switch/build.sh` (below).
5. Run in the emulator, then on the console; measure; tune (workers: 3,
   internal resolution 1x).
6. The Vita after that, with the GLES2 presenter first.

## The Switch build

`host/switch/build.sh <host build dir> <wabt source> <SDL3 include dir>
<out dir>`, in devkitPro's image, makes `eidos.nro` from a 64-bit host
build made with `EIDOS_HOST_BOUNDS_CHECKS=ON` (its `game_*.c`, `game.h`
and `gl_imports.c`).  The pieces:

- **The SDL3-on-SDL2 layer** (`host/switch/`).  `sdl3_on_sdl2.c` is built
  with SDL3's headers and gives the host every SDL3 function it calls, in
  SDL3's own types: an event is written in SDL3's layout, because
  host/sdl.c copies its bytes to the game.  `sdl2_side.c` does the work
  with SDL2.  Both libraries use the same names, so the build renames
  SDL2's in a copy of its library (`S2_SDL_*`, about 1,170 names) and the
  two files talk through `sw2.h` in plain C types.
  `sdl3_gpu_stubs.c` (written by `gen_gpu_stubs.py` from SDL_gpu.h) makes
  every GPU function fail, so the game finds no native renderer and draws
  with OpenGL ES, as on any system where it cannot start.
- **The host on the Switch.** It takes the phones' way (`HOST_MOBILE`):
  the folder `sdmc:/switch/eidos/` holds `extract/` (the game data),
  `profile/`, `args.txt`, `env.txt` and `stderr.txt`; OpenGL ES 3.0; the
  one-screen layout.  The ROM for the first run is the first `.nds` file
  in that folder (`host/pick_switch.c`).  Fibers use the AArch64 stack
  switch with 4 MiB stacks, since Horizon commits heap memory when it is
  allocated.  `host/switch/include/` covers what newlib lacks
  (`sys/mman.h`, `sigsetjmp`); `timegm` is written out in host/main.c.
- **Memory.** The game's memory is 512 MiB, taken at start.  That is more
  than an applet (the Album) gets, so the program is started over a game
  (holding R while starting one) for the full memory.
- **First runs (Ryubing 1.3.3, 2026-11-07).** The opening plays; 2400
  frames, no crash.  `--profile` per game frame: the game's own step 0.7 ms,
  the CPU 2D compositor 57 ms, sound 29 ms (it renders as much sound as the
  frame lasted), 3D 6 ms.  The same scene on the desktop: 0.06, 3.2, 0.2
  and 0.7 ms.  The emulator costs 10 to 18 times; the real cost on the
  console is the 2D compositor, about 16 ms at a Switch core's speed, so
  the GPU 2D path is the Switch's work for 60 fps.  In Ryubing the picture
  is cut to a column one sixth of its width; the desktop with the same
  settings (`EIDOS_GL_ES=1`, 1920x1080, one screen) draws it whole, so the
  console decides whether it is the port or the emulator.

## The Vita build

The Vita takes the native 32-bit route, not wasm2c: its Cortex-A9 at 444 MHz
has no room for the bounds checks, and the native build is ILP32 already.
VitaSDK's Docker image (`vitasdk/vitasdk`) has SDL3, vitaGL and kubridge;
the build is this project's CMake with `vita.toolchain.cmake`, packaged by
`vita_create_vpk` (title id `EIDS00001`).

- **The DS's layout.** ARM EABI aligns a 64-bit member to 8 and sizes an
  enum to its values; the DS does neither.  The game's sources get
  `sdk/wasm_layout.h`'s `#pragma pack(4)` (as wasm32) and
  `-fno-short-enums`; the files that talk to SDL3 and the C library keep
  the system's own layout and enum size.
- **The program's folder** is `ux0:data/eidos/` (extract/, profile/,
  args.txt, env.txt, stderr.txt), the ROM the first `.nds` file there; the
  C heap is 256 MiB.
- **Calls to DS code addresses** (a table in one overlay names a function
  of another, not yet loaded, overlay; 13 call sites seen on Win32) are
  caught by kubridge's prefetch-abort handler and sent to the function
  (sdk/plat_crash.c).  Vita3K stores that handler but does not call it yet.
- **The open question: the DS address space.** A Vita program cannot be
  given the DS's own addresses as Win32 and the 64-bit route are
  (identity mapping), as far as anyone documents: its memory starts at
  0x81000000.  So the DS memory is movable host blocks, and that mode is
  not complete for the whole game: a DS data address the metadata does not
  list stays a DS address in the data (the save table's pointer at
  `_MainSaveDesc`, read by SAVEDATA_BuildLayout, is the first one met), and
  Win32 with `BW_DS_IDENTITY=0` stops even earlier on a host address handed
  to `bw_ds_ptr`.  Done so far (2026-11-07), testable on Win32 with
  `BW_DS_IDENTITY=0`: the data scan (sdk/armdata.c) also turns every main
  RAM address in the data ranges into its host byte (374 words in the
  ARM9 image), `bw_ds_ptr` hands back a value no DS memory backs as the
  host pointer it already is, and the archive opener (sdk/fs.c) takes a
  converted table entry as the string.  The boot, the save system and the
  title's overlays now run, to frame 348 of the opening.  The next stop is
  of another kind: SDK code that builds an I/O register's address at run
  time (G2x_SetBGyAffine_ writing 0x04000020 from ov179's opening) rather
  than as a literal `tools/dsaddr.py` rewrites.  Identity mapping makes
  those real; here each is a fault, so the rest of this mode is that class.
- **Identity mapping is impossible on the hardware.**  VitaSDK's kernel
  headers (`psp2kern/kernel/sysmem/data_transfers.h`): 0x40000000 and up
  is all of userland, everything below the kernel's, and every DS address
  lies below.  So the Vita runs movable blocks, in two layers:
  1. **Proven at build time** (fast, no system dependency): the data
     scan above; `tools/dsaddr.py`'s CALLEES now matched by their
     `src/game` names too (G2x_SetBGyAffine_ and the blend functions had
     been skipped); `bw_ds_game_ptr` for the interface's "is this a game
     pointer" checks (they compared against 0x02000000..0x02400000).
     With these, Win32 with `BW_DS_IDENTITY=0` plays the opening, the
     title, the field and a trainer battle in the one-screen layout.
  2. **Caught at run time**: kubridge's data-abort handler performs a
     faulting load or store on `bw_ds_ptr`'s byte and goes on, logging
     each site once (to move a frequent one into layer 1).  This is what
     covers a pointer from one overlay into another that the listing does
     not annotate: ov021's weather kinds point into the weather overlays
     (0x021EE8C2), and scanning overlay data for such words is not safe,
     since their tables hold packed numbers in the same range (ov088's
     0x02010502).  Vita3K dispatches data aborts, so this is testable
     there; calls through code addresses (prefetch aborts, which Vita3K
     does not dispatch) get fixed at their call sites.

## Measurements

### Bounds checks against guard pages (2026-11-07)

The same game module, two hosts: the desktop's guard pages, and
`EIDOS_HOST_BOUNDS_CHECKS=ON` with a 512 MiB budget. Windows x64 (MinGW GCC
-O1, wasm-opt -O2), OpenGL, hidden window, 1280x720, tools/bench.py with
hyle's `saves/bench-field.sav`; 3000 frames each.

| Scene | Mode | Guard pages | Bounds checks |
|---|---|---|---|
| Field | max speed | 295 fps, work 1.95 ms (median) | 225 fps, work 2.75 ms |
| Field | paced | 60.2 fps | 59.5 fps |
| Battle | max speed | 454 fps, work 1.49 ms | 317 fps, work 2.02 ms |
| Battle | paced | 60.2 fps | 60.2 fps |

The checks cost about 25 to 30% of the speed (the work per frame goes up by
about 40%), less than the micro-benchmark's 2x in docs/lp64.md, and the game
fits the 512 MiB budget. A Switch core is roughly four to six times slower
than this desktop's: about 11 to 17 ms of work per frame in the field on one
core before the workers share the 2D and the 3D, against a 16.7 ms frame.
Tight, but in reach, and the workers and a 1x internal resolution are the
margin.
