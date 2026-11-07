# Consoles: the Switch and the Vita

Status (2026-11-07): the Switch program builds (`eidos.nro`) and has not
run yet; the Vita has not started. This page holds
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
