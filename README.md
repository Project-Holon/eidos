<h1><img src="docs/banner.png" alt="eidos"></h1>

A native port of Pokémon Black and White built on the [aletheia](https://github.com/Project-Holon/aletheia) decompilation. Rather than emulating the Nintendo DS, eidos compiles the decompiled C source directly to native code and loads assets from a user-supplied ROM.

Supports Windows, macOS, Linux, Android, and iOS, with experimental builds for Nintendo Switch and PlayStation Vita.

[![Discord](https://img.shields.io/badge/Discord-Join-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/Fpyemx4qp)

> **Disclaimer:** eidos is an unofficial fan project and is not affiliated with or endorsed by Nintendo, Game Freak, Creatures Inc., or The Pokémon Company. It contains no copyrighted game assets—you must provide your own legally obtained copy of Pokémon Black or White.

## Download

[![Windows](https://img.shields.io/badge/Windows-Download-0078D6?style=for-the-badge&logo=windows11&logoColor=white)](https://github.com/Project-Holon/eidos/releases/latest/download/eidos-launcher-windows-setup.exe)
[![macOS](https://img.shields.io/badge/macOS-Download-000000?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/Project-Holon/eidos/releases/latest/download/eidos-launcher-macos.dmg)
[![Linux](https://img.shields.io/badge/Linux-Download-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://github.com/Project-Holon/eidos/releases/latest/download/eidos-launcher-linux-x86_64.AppImage)
[![Android](https://img.shields.io/badge/Android-Download-3DDC84?style=for-the-badge&logo=android&logoColor=white)](https://github.com/Project-Holon/eidos/releases/latest/download/eidos-android.apk)
[![iOS](https://img.shields.io/badge/iOS-Download-147EFB?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/Project-Holon/eidos/releases/latest/download/eidos-ios.ipa)

### Desktop (Windows, macOS, Linux)

The desktop launcher installs and updates eidos, manages your ROMs, and installs mods. Because builds are unsigned:

- **Windows:** Click **More info** → **Run anyway** if SmartScreen prompts you.
- **macOS (Apple Silicon):** Follow the single Terminal command in `READ ME FIRST.txt` inside the disk image to clear the quarantine attribute before opening.
- **Linux:** Mark the `.AppImage` as executable (`chmod +x`) and run it.
- **Steam Deck:** Add the launcher to Steam as a non-Steam game and start it in Game Mode, with the **Gamepad** controller layout. Desktop Mode also works: Steam then sends the controller as keyboard keys too, and eidos reads one of them at a time.

### Mobile (Android, iOS)

- **Android (8.0+):** Install the APK directly and allow installation from unknown sources if prompted.
- **iOS / iPadOS (15.0+):** Sideload the unsigned IPA using AltStore, Sideloadly, or a similar tool with your Apple ID.

### Console Homebrew (Switch, PS Vita)

Experimental builds are available on the [Releases page](https://github.com/Project-Holon/eidos/releases) for consoles running custom firmware (currently tested in emulators; see `docs/consoles.md`):

- **Nintendo Switch:** Extract `eidos-<version>-switch.zip` to the root of your SD card, place your `.nds` ROM in `sdmc:/switch/eidos/`, and launch eidos from the Homebrew Menu using title takeover (hold **R** while launching a game) for full memory access.
- **PlayStation Vita:** Install `eidos-<version>-vita.vpk` via VitaShell and copy your `.nds` ROM to `ux0:data/eidos/`. Hardware-accelerated rendering requires `libshacccg.suprx` in `ur0:data/`.

On first launch, eidos verifies the ROM in that directory and extracts the required game data.

## Supported ROMs

Pokémon Black and Pokémon White (English, French, German, Italian, and Spanish) are supported and verified against known-good dumps during setup. Japanese and Korean releases are not yet supported.

## Status & Features

The main story is playable, though crashes and visual glitches can still occur. Please report bugs via the issue tracker or Discord so they can be fixed here or upstream in aletheia.

- **Single-Screen & Dual-Screen Modes:** Play in a unified widescreen window with adapted UI layouts for the overworld, battles, menus, and C-Gear/Pokédex apps, or use the classic dual-screen DS layout.
- **Remaster Battles:** Battles take place directly in the surrounding overworld map, using hand-tuned camera placements across ~150 locations.
- **Hardware-Accelerated 3D:** High-resolution internal rendering with fog, edge marking, and anti-aliasing. Uses OpenGL by default, with native backends for Direct3D 12, Metal, and Vulkan.
- **Display & Speed Controls:** Native refresh-rate VSync, instant text, and fast-forward (`Tab` for 2× speed, `Tab + F` for 4×, `Left Shift + Tab` for uncapped).
- **Input & Settings:** Remappable keyboard, gamepad, and touch controls, with port settings integrated directly into the in-game Options menu.
- **Mod Support:** Built-in mod loader supporting asset overrides, rule tweaks, and code mods (`docs/modding.md`), browsable from the launcher's mod index.
- **Multiplayer & WFC (Experimental):** Co-op overworld presence, online Union Room trades and battles, and Mystery Gift distribution via a custom relay server.
- **Spot Editor:** Tooling for authoring and previewing battle camera placements (`tools/spot-editor`).

## Milestones

- [x] **Milestone 1**: Portability
  - [x] 32-bit reference build, warning-free on GCC, Clang, and MSVC
  - [x] Native 64-bit build (paired with aletheia's 64-bit source)
- [x] **Milestone 2**: Platform layer (SDL3)
  - [x] Windowing, keyboard, gamepad, and mouse/touch input
  - [x] Audio output and frame pacing at the DS's native 59.83 Hz
  - [x] Native execution of the game's boot sequence and frame scheduler
- [x] **Milestone 3**: 2D engine
  - [x] Backgrounds, sprites, affine layers, windows, and color blending
  - [x] Dual-screen composition driven by simulated DS hardware registers
- [ ] **Milestone 4**: 3D engine
  - [x] Geometry engine, textures, models, and maps
  - [x] GPU renderer with fog, edge marking, and anti-aliasing
  - [ ] Pixel-accurate verification against DS hardware
- [ ] **Milestone 5**: Audio
  - [x] Sequencer, instruments, PCM, ADPCM, and PSG channels
  - [x] Music, sound effects, and Pokémon cries driven by native sound calls
  - [ ] Verification against DS hardware audio output
- [ ] **Milestone 6**: Data
  - [x] First-run ROM verification and asset extraction
  - [x] Virtual file layer with mod overrides
  - [ ] Human-readable formats for extracted data
- [ ] **Milestone 7**: Gameplay
  - [x] Overworld, battles, menus, and save files
  - [ ] Fully playable from new game to credits
- [ ] **Milestone 8**: Modding and enhancements
  - [x] Higher internal resolution
  - [x] In-game settings menu
  - [x] Unified widescreen layout and Remaster battles
  - [ ] Stable mod API and documented data formats
- [ ] **Milestone 9**: Packaging
  - [x] Windows, macOS, and Linux builds with launcher
  - [x] Android and iOS builds
  - [ ] Nintendo Switch and PlayStation Vita builds verified on hardware
- [ ] **Milestone 10**: Pokémon White and localization
  - [x] Pokémon White support
  - [x] French, German, Italian, and Spanish support
  - [ ] Japanese and Korean support

## Building from Source

### Requirements

- A checkout of [aletheia](https://github.com/Project-Holon/aletheia) alongside this repository, checked out at the revision specified in `docs/aletheia-pin.md`
- A legally obtained ROM of Pokémon Black or White (see [Supported ROMs](#supported-roms))
- CMake 3.16+, Ninja, and Python 3.11+
- For the 32-bit build: MinGW-w64 GCC (`i686-w64-mingw32-gcc`) or MSVC x86 (see `docs/building.md` and `docs/consoles.md` for 64-bit, mobile, and console toolchains)
- SDL3 (on Windows, run `python tools/fetch_sdl.py` to fetch prebuilt binaries)

### Build Steps

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
    -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_OVERLAYS=ALL
cmake --build build --target eidos_boot
ctest --test-dir build
```

See `docs/building.md` for 64-bit builds, alternative compilers, and CMake options.

## Running

Launch `eidos_boot`. On first run, you will be prompted to select your ROM for verification and extraction.

Save files are stored in:
- **Windows:** `%APPDATA%\eidos\saves`
- **macOS:** `~/Library/Application Support/eidos/saves`
- **Linux:** `~/.local/share/eidos/saves`

### Default Controls

| DS Input | Keyboard / Mouse | DS Input | Keyboard / Mouse |
| --- | --- | --- | --- |
| D-Pad | Arrow keys | START | Return |
| A | X | B | Z |
| X | S | Y | A |
| L | Q | R | E |
| SELECT | Right Shift | Touch | Left click |

**Shortcuts:** `Tab` (2× fast-forward), `Tab + F` (4× fast-forward), `Left Shift + Tab` (uncapped fast-forward), `Alt + Return` (fullscreen), `F12` (screenshot).

## Community

Join the [Project Holon Discord](https://discord.gg/Fpyemx4qp) for troubleshooting, bug reports, multiplayer matchmaking, and development updates.

## Documentation

| File | Description |
| --- | --- |
| `docs/architecture.md` | High-level architecture and integration with aletheia |
| `docs/runtime.md` | Frame loop, scheduler, heap, platform layer, and boot sequence |
| `docs/engine-2d.md` | 2D rendering engine |
| `docs/engine-3d.md` | 3D rendering engine |
| `docs/graphics-backends.md` | OpenGL and native graphics backends |
| `docs/audio.md` | Audio engine and ARM7 sound driver |
| `docs/files.md` | Virtual filesystem, overlays, and first-run ROM extractor |
| `docs/building.md` | Build options, compilers, and platform toolchains |
| `docs/consoles.md` | Nintendo Switch and PlayStation Vita builds |
| `docs/launcher.md` | Desktop launcher architecture |
| `docs/milestones-detail.md` | Detailed milestone breakdown and verification notes |
| `docs/status.md` | Current implementation status and remaining work |
| `docs/unified-view.md` | Single-screen composition rules (layers, canvas, input, placeholders) |
| `docs/unified-view-inventory.md` | Per-screen inventory and single-screen layout strategies |
| `docs/modding.md` | Mod loader, rules, and code mods |
| `docs/mod-ui-text.md` | Translating UI text via mods |
| `docs/multiplayer.md` | Relay-based multiplayer architecture |
| `docs/link-play.md` | Online Union Room and link play |
| `docs/test-switches.md` | Debug and test flags |

## Legal

Code in this repository is licensed under the MIT License (`LICENSE`). This repository contains no game assets or decompiled game source code; it compiles source from aletheia and loads assets extracted from your own ROM at runtime. Release builds contain only the compiled engine binary. See `LEGAL.md` for details.

Pokémon and Nintendo DS are trademarks of Nintendo. Pokémon Black and White are copyright Nintendo, Creatures Inc., and GAME FREAK inc.
