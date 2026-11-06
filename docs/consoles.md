# Consoles: the Switch and the Vita

Status: groundwork (2026-11-07). Neither console builds yet. This page holds
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
- **The host layer.** Files, threads (fibers: host/fiber.c already has the
  AArch64 stack switch), sockets (left out at first), audio and input go
  through libnx or VitaSDK; SDL2 is the packaged SDL on both (devkitPro
  `switch-sdl2`, VitaSDK), so the host needs an SDL2 shim for the SDL3
  calls it makes, or a community SDL3 build.
- **Left out at first:** the launcher, mods (WAMR), multiplayer (mbedtls),
  the crash handler. The ROM is extracted on a PC (or on the console) and
  the data folder copied to the SD card.
- **Layout:** the one-screen layout only (docs/building.md's phone plan:
  touch is the bottom screen, buttons map straight).

## Toolchains

They install inside the Ubuntu WSL distribution, so Windows stays clean.

- Switch: devkitPro's pacman, then devkitA64, libnx and the portlibs
  (`switch-dev switch-sdl2 switch-mesa switch-zlib`). The script is
  `E:\dev\ports\setup_switch_wsl.sh` (run in WSL as root). It downloads and
  runs devkitPro's own installer, so it waits for the owner's go.
- Vita: VitaSDK (`vdpm`), the same way, once the Switch build runs.
- Testing: an emulator first (Ryujinx's maintained fork for the Switch;
  Vita3K for the Vita), then the owner's modded Switch.

## Steps

1. Measure the bounds-checked build on a desktop. Done for Windows x64;
   numbers below.
2. Toolchain in WSL (waits for the go above).
3. Compile the wasm2c output and the runtime with devkitA64 (no SDL yet):
   proves the game's code builds for the Switch.
4. Host layer for libnx with SDL2: window, OpenGL (Mesa), audio, input,
   files under `sdmc:/switch/eidos/`. Package an `.nro`.
5. Run in the emulator, then on the console; measure; tune (workers: 3,
   internal resolution 1x).
6. The Vita after that, with the GLES2 presenter first.

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
