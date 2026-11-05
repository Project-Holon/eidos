# Research: Nintendo Switch and PS Vita (one-screen)

Written 2026-11-06. Status: research only, nothing built. Both would be
homebrew builds (a `.nro` for the Switch, a `.vpk` for the Vita) for
consoles that run unsigned code. Both use the one-screen layout only.

## Summary

| | Switch | PS Vita |
|---|---|---|
| CPU | 4x Cortex-A57, 1.02 GHz, 64-bit | 4x Cortex-A9, 444 MHz (3 usable), 32-bit |
| RAM | 4 GB (about 3.2 GB to a game in full mode) | 512 MB + 128 MB video |
| Screen | 1280x720 touch (handheld), 1920x1080 docked (no touch) | 960x544 touch, rear touch pad |
| SDK | devkitPro: devkitA64 + libnx | VitaSDK (GCC, arm-vita-eabi) |
| SDL3 | not in devkitPro's packages; community ports exist | upstream SDL3 has a Vita port |
| Graphics | Mesa (nouveau): OpenGL 4.x and OpenGL ES 3.2 | PVR driver: OpenGL ES 2.0 only |
| Route for the game code | the 64-bit host route (wasm2c), as on Android | the 32-bit native build, or wasm2c with bounds checks |
| Effort (my estimate) | medium: about the size of the iOS port | large: the renderer and speed are open problems |
| Verdict | do it first | do it after the Switch, and expect a slow 3D scene |

## What eidos already has that both reuse

- `host/`: the program that runs the wasm32 game module through wasm2c on
  64-bit systems, with SDL3 and OpenGL ES 3.0 (Android, iOS). The GL calls
  the module makes are translated to GLES (`tools/wasm_gl_imports.py`).
- Fibers for the game's threads: `host/fiber.c` (ucontext, or its own code
  where there is no ucontext, as on Android and iOS).
- The one-screen layout, which both consoles need, and touch input (the
  mobile builds play by touch alone).
- Picking the ROM on first start, then writing the extract (`host/pick_mobile.c`).

## Switch

**Toolchain.** devkitA64 and libnx are mature. Players start homebrew
from the Homebrew Menu. The game must run in full ("title takeover") mode:
launched from the Album applet, homebrew gets only about 440 MB of memory,
and an applet cannot use all CPU cores. Emulator ports tell players the
same thing.

**SDL3.** devkitPro packages only SDL2 for the Switch. devkitPro has an
SDL3 branch of its Switch backend, and some forks build it:
[nx-mod/sdl3-nx](https://github.com/nx-mod/sdl3-nx) (SDL 3.4.18, video,
audio, input, files, time; SDL_GPU on deko3d) and
[etonedemid/SDL](https://github.com/etonedemid/SDL) (Vulkan through Mesa).
Other ports, such as a Tenkaichi 3 decomp's Switch PR, built SDL3
themselves. Plan: pin one fork as a source tree, as the Android and iOS
builds pin SDL3's source release.

**Graphics.** devkitPro's Mesa (`switch-mesa`) gives OpenGL ES 3.2 through
EGL. That is more than the GLES 3.0 the host already targets, so the
presenter and the GPU 3D path should work as written. deko3d (native) is
faster but would be a new backend; it can wait.

**Game code.** The Switch is 64-bit ARM, like Android, so it takes the
same wasm2c route. Two changes:
- wasm2c's guard pages reserve 8 GB of address space and catch faults with
  a signal handler. libnx has no such mmap or signals, so the Switch build
  uses wasm2c's bounds checks (`WASM_RT_MEMCHECK_BOUNDS_CHECK`). That costs
  roughly 10 to 20 percent of speed. The A57s are about twice as fast as
  the Vita's cores, and the Android build already runs well on similar
  chips.
- Fibers: no ucontext in libnx. The AArch64 switch that Android and iOS
  use carries over.

**Input.** Docked, there is no touch, so every one-screen screen must work
with buttons alone. Desktop players with a gamepad already need that.
Handheld, touch works as on phones. Joy-Con and Pro Controller map like
any SDL gamepad.

**Files.** No file picker: the game looks for `.nds` files in
`sdmc:/switch/eidos/` and lists them. Saves and settings stay in the same
folder.

**Network.** libnx has BSD sockets, so the metaxy relay (multiplayer, link
play, Mystery Gift) should work as written.

**Code mods.** WAMR in interpreter mode, as now. The Switch does not allow
JIT code from homebrew without extra work.

**Work list.**
1. `cmake/switch-toolchain.cmake` from devkitPro's, `host/` built against
   an SDL3 fork, `.nro` packaging (`elf2nro` with an icon and title).
2. Memory runtime with bounds checks, fiber code for libnx.
3. EGL/GLES 3.2 context through SDL; test the presenter and the GPU 3D.
4. ROM list from the SD card, first-start extract.
5. Gamepad-only pass over the one-screen screens (also helps desktop).
6. CI job that builds the `.nro` (devkitPro has a Docker image).

## PS Vita

**Toolchain.** VitaSDK (GCC for ARMv7, 32-bit). SDL3 supports the Vita
upstream ([SDL3 README-vita](https://wiki.libsdl.org/SDL3/README-vita)).
Output is a `.vpk` installed with VitaShell.

**Graphics: the hard part.** The Vita GPU (PowerVR SGX543MP4+) has OpenGL
ES 2.0 only, through SDL's PVR option (`-DVIDEO_VITA_PVR=ON`), or desktop
GL 1.x/2.x through gl4es4vita. eidos's GPU paths need more:
- the GPU 3D draws polygon IDs into an integer (R8UI) target for edge
  marking, which GLES 2.0 lacks;
- the shaders are GLSL 3.30, which the GLES 3.0 translation turns into
  GLSL ES 3.00, not 1.00.

Two options:
- **A. Software 3D, GPU only to present.** The CPU rasteriser draws the 3D
  at DS size (`engine/g3d/raster.c`), and a small GLES 2.0 shader scales
  the screen to 960x544. Simple, and identical to the reference. But the
  rasteriser is C written for correctness, and at 444 MHz with three cores
  a busy 3D scene (battles, the 3D city maps) may fall under 60 fps.
  DraStic on the Vita runs DS games at full speed with a heavily
  optimised NEON renderer, so it can be done, but not with our current
  code as is.
- **B. A GLES 2.0 GPU 3D.** Edge marking done another way (polygon ID in
  the alpha channel or a second pass), shaders in GLSL ES 1.00. More work,
  but the GPU does the heavy part.

Start with A and measure. Do B only if A is too slow in battles.

**Game code.** The Vita is 32-bit, so two routes:
- **Native 32-bit build.** The main CMake build in its ILP32 mode
  (`cmake/ilp32-toolchain.cmake`, the matched heap and scheduler), compiled
  for ARMv7 directly. No translation layer, so it is the fastest. Risk:
  the code has run on x86 only. ARMv7 traps on unaligned 64-bit and
  multiple loads, so a packed or cast struct that x86 accepts can crash
  here. That needs a pass with GCC's alignment warnings, then testing.
- **wasm2c with bounds checks.** The same route as the Switch, so less
  to maintain. But a 32-bit process cannot reserve the 4 GB a wasm memory
  needs, so every access is checked, which costs speed the Vita does not
  have.

Recommendation: native 32-bit, since speed decides whether the Vita is
playable at all.

**Memory.** 512 MB plus 128 MB of video memory. The game itself fits easily
(the DS has 4 MB). The extract and our caches are what to watch: decoded
texture caches and the worker pool's buffers need a lower cap there.

**Input.** Front touch (960x544) for the one-screen UI, buttons for
everything else, the rear pad unused. L2/R2 exist only on PS TV or with a
DS4 pad, so no action may need them.

**Files.** ROM list from `ux0:data/eidos/`.

**Network.** SceNet sockets through VitaSDK; the relay should work.

**Code mods.** WAMR's interpreter supports 32-bit ARM. Slow on 444 MHz,
but the official mods are rules, not code, except Soul Link.

**Work list.**
1. VitaSDK toolchain file, native ILP32 build of the game for ARMv7, SDL3
   from source with the PVR option.
2. Alignment pass (GCC `-Wcast-align=strict` on src/game and sdk), fix
   what it finds, run the game's own boot.
3. Software 3D at DS size plus a GLES 2.0 presenter; measure fps in a
   battle and in Castelia City.
4. 960x544 one-screen layout check (it is 16:9 like the phones, at a
   lower size: text sizes need a look).
5. ROM list, extract, `.vpk` packaging (`vita-mksfoex`, `vita-pack-vpk`).
6. If too slow: profile, then NEON in the rasteriser's inner loops, then
   option B.

## Prior art

- DraStic DS runs on both consoles (Switch port:
  [NaGaa95/DrasticDS_nx](https://github.com/NaGaa95/DrasticDS_nx)), which
  shows DS-class games are playable on both.
- Many decomp ports reached the Switch through SDL with libnx (for example
  the Tenkaichi 3 decomp's Switch PR, [excinus/Tenkaichi3Decomp-NX#1](https://github.com/excinus/Tenkaichi3Decomp-NX/pull/1)).
- The Vita has a port of the Super Mario 64 decomp (a 32-bit game, as
  here) that draws through vitaGL.

## Open questions for the user

- Distribution: both are homebrew only. A release would carry a `.nro` and
  a `.vpk` next to the APK and IPA.
- Order: Switch first (mostly reuse), Vita second (renderer and speed work).
