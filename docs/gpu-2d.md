# The 2D compositor on the GPU

Status: phase 1 done (2026-11-01). With `EIDOS_G2D_GPU_CHECK=1` the shader
composed 4,949 screens per scene over the title, the field, the X menu, the
Pokedex, the bag, the save screen, the options, the trainer card, the starter
choice and a wild battle, with no pixel different from the CPU compositor.

All four phases are in and on by default. The "GPU drawing" setting
(Display) turns them off; `EIDOS_G2D_GPU=0` or `1` overrides the setting.
Without OpenGL 3.3, or when the shader fails to build, the CPU composes.

Phase 2: a screen the shader can compose is
drawn from its output, composed when the screen is presented (with the
pixels it stands in for). The window is the same with it on and off in both
layouts (the title, the field, the Pokedex, the starter choice, a battle).

Phase 4 is in, with phase 2: the CPU no longer composes a screen the shader
composes. The readers of the pixels get the GPU's screen a frame late (an
asynchronous read back). The CPU still composes a frame with the display
capture on, a screenshot, and any frame the shader cannot take.

Phase 3 is in, with phase 2: the main screen's 3D layer is the GPU's own
3D frame, sampled by the 2D shader at the 3D's resolution. The screen with
the 3D off and the screen over black 3D are no longer composed on the CPU.
The one-screen world view keeps its own composite (its 3D is the window's
shape); the shader makes the three screens it takes (the 3D as the sentinel
colour, the 3D off, a black 3D). Frames with the display capture keep the
CPU path.

Measured on 2026-11-01 (the two-screen layout, a fast PC, per frame):

| Scene | Render, CPU | Render, GPU | Present, CPU | Present, GPU |
|---|---|---|---|---|
| Field (3D) | 2.9 ms | 1.5 ms | 0.39 ms | 0.66 ms |
| Pokedex | 2.3 ms | 1.0 ms | 0.38 ms | 0.63 ms |
| Battle field, one-screen world | 3.9 ms | 2.3 ms | 0.45 ms | 0.65 ms |

## Why

The DS 2D compositor (`engine/g2d/hwcompose.c`, `engine/g2d/bg.c`) runs on the
CPU. Measured on 2026-11-01 (one engine, one frame, a fast PC):

| Scene | Build the layers | Per-pixel compose |
|---|---|---|
| Field | 0.14 ms | 1.86 ms |
| Pokedex | 0.58 ms | 1.66 ms |
| Battle | 0.27 ms | 2.48 ms |
| Starter choice | 0.26 ms | 3.18 ms |

Building the layer descriptions is cheap. The per-pixel compose (the stack
walk, the windows, the colour effects) is the cost, two to three times per
frame. On a slow CPU it is most of the frame (issue #5). That part moves to a
fragment shader. The CPU keeps everything else.

## What moves and what stays

Stays on the CPU (it is cheap and it is the reference):

- Reading the registers, palettes, OAM and VRAM into the layer descriptions
  (`build_bgs`, `build_objs`, `build_windows`, `build_blend`).
- The layers the compositor gets as rasters: extended and bitmap BGs, a
  text BG through an extended palette, mosaic (`build_raster`,
  `apply_mosaic`).
- The OBJ line buffer: one OBJ pixel per screen position, with its priority,
  palette source, semi-transparency and the OBJ window mask.
- The whole CPU compositor, as the fallback and as the reference the GPU
  output is checked against.

Moves to the GPU (one fragment shader, one pass per screen):

- The text BG fetch (screen entry, tile, palette) from the BG VRAM window.
- The affine BG fetch (8.8 matrix, wrap or transparent overflow).
- The rasters, the OBJ line buffer and the 3D layer, as textures.
- Priority, the windows (WIN0, WIN1, the OBJ window, outside), the colour
  effects (alpha, brighten, darken, the 3D layer's own blend, the
  semi-transparent OBJ) and the master brightness.

## Exactness

The shader does integer math only, with the same rules as `bg.c`: colours are
expanded `c * 255 / 31`, the effects work in the 6-bit stage, and the master
brightness works on that stage. A frame is correct when every pixel equals
the CPU compositor's pixel. `EIDOS_G2D_GPU_CHECK=1` composes every frame both
ways, reads the GPU frame back and logs the number of different pixels and
the first one with its layer inputs.

## Inputs per screen and frame

| Input | Form |
|---|---|
| BG VRAM window | R8UI texture, 1024 bytes wide |
| BG palette, OBJ palette, OBJ extended palettes | RGBA8 texture, one row each |
| Text BGs (4) | uniforms: enabled, priority, scroll, size, map and tile offsets, colour depth |
| Affine BGs (2) | uniforms: map and tile offsets, size, matrix, reference point, wrap |
| Rasters (4) | RGBA8 textures, priority and BG number as uniforms |
| OBJ line buffer | RGBA8UI texture 256 x 192: index, priority and source, semi, window |
| 3D layer | RGBA8 texture, its scroll |
| Windows, colour effects, backdrop, master brightness | uniforms |

The BG VRAM window is the large upload (up to 512 KB on the main engine). It
is sent only when its bytes changed since the last frame.

## Fallback to the CPU

A frame is composed on the CPU when any of these is true:

- there is no OpenGL 3.3 context, or the shader failed to build;
- the display mode is not 1 (graphics), or the screen is in forced blank;
- the display capture runs this frame (the game reads its result from VRAM);
- the engine reports an unsupported feature (`G2D_HW_UNSUP_*`).

## Who reads the screens on the CPU

| Reader | Phase 1 | Later |
|---|---|---|
| The presenter | CPU frame | GPU texture, no copy |
| The one-screen interface (`screen_rgba`) | CPU frame | read back a frame late |
| The GPU 3D hand-off (sentinel, behind, zero) | CPU frames | gone: the shader composes the 3D itself |
| The display capture | CPU frame | CPU frame (fallback) |
| Screenshots, mods | CPU frame | read back on request |

## Phases

1. The shader and the check mode. The GPU composes beside the CPU and is
   compared to it every frame. Nothing it draws is shown. Done when the
   check reports no different pixel over the title, the field, the menus,
   the starter choice and a battle.
2. The GPU frame is shown. The presenter draws the GPU texture; the CPU
   per-pixel compose runs only for the fallback frames and for the readers
   that need pixels.
3. The 3D layer composed in the shader at the 3D's own resolution. The
   sentinel, behind and zero screens go away.
4. The interface reads the screens back a frame late; the CPU compose stops
   running on GPU frames.

## The spot editor

The spot editor (`tools/spot-editor`) draws its window with OpenGL through
`engine/platform/plat_gl_view.c`. The 3D views stay GPU textures (no read
back), the sprites are cached textures, and the interface canvas is one
texture laid on top. Each spot drag reuses the work that does not depend on
where the battle stands (`bmap_place_prepare`) and the map's textures
(`BmapScene.tex_gen`). At 2559x1340, a frame takes about 11 ms (it took
about 28 ms), and a drag step takes about 20 ms (it took about 65 ms).
`EIDOS_SPOTS_PROF=1` prints each frame's time.
