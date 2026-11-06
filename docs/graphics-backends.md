# Graphics backends: plan

Status of the renderer and where it goes. Written 2026-10-04; the backends
section rewritten 2026-11-06.

## What runs today

- The 2D engine (backgrounds, sprites, priorities, blending) and the 3D engine
  (polygons, textures, lighting) are software models of the DS chips, written in
  C (`engine/g2d`, `engine/g3d`). They are the reference: every other renderer is
  tested against them (`tests/test_gpu3d.c` draws the same polygons both ways).
- The presenter (`engine/platform/plat_gl.c`) is the GPU part. It uploads each
  screen as a texture and scales it to the window with one OpenGL 3.3 core
  shader. SDL's own renderer is the fallback when OpenGL 3.3 is not available
  (also used by the dummy video driver in the headless tests, or with
  `EIDOS_RENDERER=sdl`).
- The 3D engine runs on the GPU too (`engine/platform/plat_gl3d.c`). Every
  SWAP_BUFFERS also copies what the CPU rasteriser was given (polygons, decoded
  textures, register state) into a snapshot (`g3d_hw_snapshot`); the thread that
  owns the GL context draws it into a framebuffer of any size, following
  `engine/g3d/raster.c` rule for rule (perspective correct colour and texture,
  repeat/flip/clamp, modulate/decal/toon/highlight on 6-bit colour and 5-bit
  alpha, alpha test, z or w depth, culling). The CPU rasteriser is skipped for
  frames the GPU draws.
- How the GPU 3D meets the 2D layers: the compositor stays on the CPU at DS
  resolution. For a screen whose BG0 is the 3D layer it makes two small images,
  the screen with the 3D layer set to one opaque unlikely colour and the screen
  with the 3D layer off; the shader shows the GPU frame where the two differ and
  folds the master brightness in exactly. When the display capture reads the 3D
  layer (the title screen and the logo scenes do, every frame) the GPU frame is
  also rendered at DS size and read back (asynchronously, one frame late when it
  only feeds the capture) because the capture works at DS resolution on the
  hardware too; those scenes cannot exceed DS resolution.
- Edge marking is a GPU pass: the draw writes each opaque pixel's polygon ID
  to a second (R8UI) target, and a full-screen pass outlines each opaque pixel
  nearer than a neighbour of another ID, the neighbour one DS pixel away at any
  scale. Such a frame is drawn without multisampling.
- Frames that use fog, the rear-plane bitmap or shadow polygons are not drawn
  by the GPU yet (`G3dSnapshot.unsupported`, less the features the GPU reports
  with `g3d_hw_gpu_features`); the CPU rasteriser draws them (at most
  `BOOT_CPU_3D_MAX`), in row bands on the worker pool, the same to the bit as
  one pass.
- A snapshot texture is uploaded only when its texels change (a hash per GL
  texture): re-specifying every texture each frame made the driver wait on the
  previous frame's draw, which cost a 6x frame about 25 ms.
- Also done for speed: decoded textures persist across frames (hash of the
  source bytes), the 2D compositors run on a worker pool (`engine/g2d/par.c`:
  the engines in parallel, each screen in four row bands), the decoded-texture
  and snapshot copies are cheap, and the screens without a 3D layer stay 256x192
  with the GPU scaling them.
- Measured on the title screen (frame 4750 on, a 6 core machine, hidden window):
  60 fps at internal resolution 1x, 2x, 4x, 6x, 10x and 20x (the title is a
  capture scene, so the setting changes nothing there); the CPU path (SDL
  renderer) gets 31 fps at 1x and 12 fps at 6x. Debug aids: `--profile`,
  `EIDOS_WINDOW_HIDDEN=1`, `EIDOS_WINDOW_SIZE=WxH`, `EIDOS_GL_DUMP=file.bmp:N`.

## The backends (decided 2026-11-06)

The platform layer draws through one table of operations (`plat_gpu.h`); a
backend fills it.  Two backends, plus the software path as fallback and
reference:

| Backend | Files | Platforms | Role |
|---|---|---|---|
| SDL3 GPU | `plat_sdlgpu.c`, `plat_sdlgpu3d.c`, `shaders/*.hlsl` | Direct3D 12 on Windows, Metal on macOS and iOS, Vulkan on Linux and Android | the native path: the setting Renderer, Native (`EIDOS_RENDERER=gpu[:driver]`) |
| OpenGL 3.3 core / ES 3.0 | `plat_gl*.c` | everywhere, the web and the Switch included | the default for now, and the fallback when SDL's GPU does not come up |
| Software (C) | `engine/g2d`, `engine/g3d` | everywhere | the reference; draws what a backend cannot |

Why one SDL3 backend and not three written by hand (the research,
2026-11-06): SDL's GPU API is a thin layer over each platform's own API, so
the game loses no measurable speed at a DS frame's few hundred draws, and one
backend is one place to fix and one picture to keep equal to the software
reference.  Its known weak spot is the presentation (frame latency, open
issues on Metal); a backend of its own for one platform comes only when the
frame-time log (`--bench`, `tools/bench.py`) shows SDL's presentation is the
cause of stutter there.  The table keeps that a contained job.

The SDL3 backend runs inside the wasm module like the rest of the platform
layer: the 64-bit builds answer its SDL calls in `host/sdl_gpu.c` (handles,
struct layouts, transfer-buffer mapping through a block of module memory).

The shaders are HLSL, compiled by `tools/shaders.py` (DXC to DXIL and SPIR-V,
SPIRV-Cross to MSL; the compilers from `tools/fetch_shadertools.py`) into the
committed `shaders_gen.h`.  Differences from the OpenGL renderer are listed in
`plat_sdlgpu3d.c`'s header: the polygon IDs in an R8 UNORM target, a depth
copy the shaders write, clip z moved from GL's -w..w to 0..w, top-down
frames, and the 2x anti-aliasing everywhere.

Not in the SDL3 backend yet: the GPU 2D compositor (`docs/gpu-2d.md`; the CPU
composes the 2D), and a tool's window (the spot editor draws with OpenGL).

## Why this set

What comparable projects use (checked 2026-10-04):

- RT64 (the renderer of Zelda64Recomp and N64Recomp): Direct3D 12, Vulkan and
  Metal. It needs D3D12 Shader Model 6, Vulkan 1.2 or Metal Argument Buffers
  Tier 2, so it excludes older GPUs.
- Ship of Harkinian (libultraship, Fast3D): Direct3D 11 (Windows default), OpenGL
  (all platforms) and Metal; a Vulkan backend is in review.
- melonDS: a compute-shader renderer that needs OpenGL 4.3 and reproduces the
  DS rasteriser more closely, but cannot run on macOS, which stops at 4.1 (see
  "Prior art").
- macOS: OpenGL has been deprecated since 10.14 and is frozen at the 4.1 core
  profile. It still works, including on Apple Silicon, and Apple has announced no
  removal date. Metal is the long term route there.

SDL3's 2D renderer cannot run custom shaders; its GPU layer wraps Vulkan,
Direct3D 12 and Metal, and is what eidos's native backend is written on (see
"The backends").

## Design rules for the interface

- The backend takes a snapshot of the frame (geometry, decoded textures, the
  register state) and never reads emulated memory itself; the game threads
  produce the snapshot, the thread that owns the graphics context consumes it.
- The 2D compositor stays on the CPU at DS resolution (it is cheap) and hands
  the backend two small images per 3D screen: the screen with the software 3D
  and the screen with the 3D layer off. The backend draws the 3D at any
  resolution and composites it in a shader.
- The software 3D rasteriser stays as the reference and the fallback: it draws
  any frame the GPU backend cannot (see below) and every frame when there is no
  GPU backend. The display capture (DISPCAPCNT) needs the 3D frame at DS size; a
  GPU backend gives it by reading its own 1x render back.
- Every backend is tested against the software output at 1x on fixed scenes with
  a stated tolerance.
- No backend may change the OS display mode (fullscreen is desktop fullscreen).

## Not done yet

Fog, the rear-plane bitmap, shadow polygons and
`DISP_1DOT_DEPTH` on the GPU path (those frames fall back to the CPU
rasteriser); quads with non-planar vertex colours differ slightly from the
software edge interpolation; the alpha-blend of the 3D layer with BLDALPHA is
approximated by the 3D layer's own alpha; scenes with both a capture and a
visible 3D layer read the frame back synchronously.

## Prior art (checked 2026-10-04) and what it means for eidos

- **RT64** (Zelda64Recomp, N64Recomp): the resolution choices are the original
  resolution, twice the original, and "Auto" (the display's resolution), with
  optional 2x or 4x downsampling (render larger, shrink) and MSAA up to 8x; the
  maintainers note that downsampling together with Auto is demanding. Its
  README says it upscales framebuffer effects by raising "the resolution of all
  framebuffers and any associated region copies by a scaling factor". For
  eidos: an "Auto" internal resolution that follows the window, plus a separate
  quality option (downsampling, MSAA), is the established shape; and the DS
  display capture (a framebuffer copy the game reads back) is exactly the case
  RT64 handles by scaling the copied region too. Doing the same needs the
  capture destination kept as a high-resolution shadow while the game does not
  touch it from the CPU; scenes that capture every frame (the title and logo
  scenes) are limited to DS resolution until that exists.
- **melonDS**: a classic OpenGL renderer that needs OpenGL 3.1 and upscales the
  3D, and a compute-shader renderer (OpenGL 4.3) that reproduces the DS
  rasteriser more closely. The two-tier split (simple and compatible, exact and
  modern) is the same as this document's software-reference plus GPU backends.
- **Pixel-art filtering**: "sharp bilinear" (a nearest-neighbour scale to a whole
  multiple, then linear for the rest; or the equivalent one-pixel blend at the
  seam) keeps pixels crisp at any window size and is soft only below about 2x.
  eidos uses the one-pixel-seam form in the presenter shader
  (`engine/platform/plat_gl.c`) and has a test that reads the window back.
- **Windows scaling**: SDL2 apps were not DPI aware by default, so Windows
  stretches the finished window bitmap (blur) on a display set to 125 or 150
  percent. SDL's `SDL_WINDOWS_DPI_SCALING` hint makes the process DPI aware and
  measures windows in DPI-scaled points while the GL drawable is in real pixels
  (`SDL_GL_GetDrawableSize`, which the presenter already uses). Set in
  `eidos_platform_init`.
- **Frame pacing**: emulators that run a fixed-rate system on a display with a
  different refresh rate either lock the swap to the display and correct audio
  with small resampling changes (dynamic rate control), or pace in software as
  eidos does (the DS rate is 59.8261 Hz). eidos presents with a swap interval of
  0, which can tear; a vsync option with audio rate control is a later item.
