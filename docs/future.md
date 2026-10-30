# Future work

What is known to be missing or worth adding, written 2026-10-04. Items marked
(prior art) come from what other recomp and emulator projects do; see the
"Prior art" section of docs/graphics-backends.md for the sources.

## Known gaps in what exists

- **Tearing.** The presenter swaps with an interval of 0, so a frame can tear.
  A vsync option needs the pacer and the audio to follow the display (dynamic
  audio rate control, as emulators do) so a 59 Hz or 144 Hz display does not
  change the game speed. (prior art)
- **Fullscreen and Borderless are the same.** Both use desktop-resolution
  fullscreen, which never changes the OS video mode. Either merge them into one
  setting or give Borderless a real meaning (a borderless window that is not
  fullscreen, for window managers that treat them differently).
- **Saved window size.** `display.width` and `display.height` are stored but the
  window always opens at the DS size for the layout. Decide: remember the last
  non-maximized size (never a maximized one), or drop the two options.
- **GPU 3D gaps.** The GPU rasteriser (engine/platform/plat_gl3d.c) does not do
  the rear-plane bitmap or shadow polygons (fog and edge marking it does). A
  frame that uses one is drawn by the CPU rasteriser at a capped scale. Neither
  shows up in play: a run through the title and the field (2026-10-30) had no
  frame with either, and no matched source draws a shadow polygon. The
  rear-plane bitmap would be a textured full-screen quad with depth.
- **CPU cost of a frame.** The game's own code is a small part of a frame
  (about 0.5 ms in the field); the rest is the DS's 2D compositor (three
  composes a frame: the main screen, the bottom screen and the main screen
  with its 3D layer off, about 2 ms of CPU each) and the 3D hand-off. The
  per-pixel layer walk in engine/g2d/bg.c is the next thing to make cheaper.
- **2D over 3D.** A translucent 2D layer or sprite blended over a GPU 3D layer is
  lost: the composite shows the GPU frame wherever the 2D screen differs from the
  3D-off screen. The fix is to keep the layer-priority and blend information per
  pixel (a mask plus the 2D colour and its blend factor) and blend in the shader.
- **3D alpha.** Where the GPU draws a translucent pixel over a translucent one the
  alpha is "over"; the CPU rasteriser keeps the larger alpha.
- **Capture scenes.** A scene that captures the main screen into a VRAM bank
  every frame and shows that bank (the title: a feedback blend with the frame
  before) is kept at the chosen scale on the GPU (demo/eidos_boot.c,
  boot_capture_plan; engine/platform/plat_gl.c, eidos_gl_capture).  Only the
  plain form is covered: the whole screen, the graphics as source A, the
  shown bank as source B.  A capture of the 3D alone, a capture used later
  as a texture or a background, and a partial size stay at DS size.
- **Untested hosts.** Linux and macOS have never run; the OpenGL path needs
  checking there (macOS stops at OpenGL 4.1, which is enough for 3.3 core).
  Everything on Windows was checked with hidden windows and pixel readback, not
  in a visible window by a person.

## Graphics options other projects offer (prior art)

- An **Auto** internal resolution that follows the window (RT64), next to the
  fixed multiples. The GPU path can already do it; the CPU path keeps its cap.
- **Downsampling**: render at 2x or 4x the target and shrink (RT64), and
  **MSAA** with a choice of 2, 4 or 8 samples. eidos uses 4 samples when the
  game turns anti-aliasing on.
- An **integer scaling** option (pixel-perfect, with borders) next to the
  default fill-the-window picture.
- **Sharp bilinear** is in; further filters (a CRT or LCD-grid shader, an
  xBRZ-style 2D upscaler) are optional presenter shaders.
- Further backends: Direct3D 12, Vulkan and Metal (docs/graphics-backends.md);
  RT64 supports exactly those three.
- A **performance overlay** (frame time per phase) built on the existing
  `--profile` counters.
- **Texture replacement and mods** that supply their own textures and meshes
  (aletheia and eidos are built for modding; docs/modding.md).

## Settings screen

- The Options-style screen (engine/platform/mod_options_proc.c) shows the cursor and markers exactly as the game's data places them,
  and moves its text into a message bank that a mod can translate.
- More sections as the port grows: audio (music and effects separately),
  controller options (dead zone), language, a reset to defaults per section.
