# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

## 0.1.9

### Bug Fixes
- **Saves**: Fixed saves seeming gone from the title screen after starting a New Game and quitting before saving. The title now opens on the save you played last, and nothing was ever deleted (#10).
- Fixed the name keyboard opening hidden behind the Multiplayer screen in one-screen mode, which left no way to type or leave (#9).
- Fixed the arrows and D-pad on the one-screen name keyboard following the DS's key grid: they now move over the keys as shown, every move lands on a key, and the opposite direction always comes back (#7).

## 0.1.8

### One-Screen Mode & UI
- **Touch & Mouse Movement**: Added an on-screen virtual stick: drag to walk, push past the outer ring to run, and tap NPCs to interact.
- **PC Storage Rework**: New 3-pane layout (Party, Box, Details), drag-and-drop support, and a dedicated Move Items screen.
- **Level-Up & Evolution**: Unified move learning, move forgetting, and evolution menus with the modern one-screen UI. Evolution sequences now fill the window.
- **Battle UI**:
  - Poké Balls now remain visible while shaking during capture attempts.
  - Move info can now be inspected directly from the party list.
  - Field targeting: In double and triple battles, select targets directly on the battlefield instead of from a text list.
- Restored one-screen boot and title screens, and encounter transitions now scale to fill the entire window.

### Controls & Input
- **Controller Glyphs**: Added Xbox and PlayStation button prompts (`Settings > Controls > Button icons`).
- Standardized controller menu confirmation to the bottom face button (A / Cross).
- Fixed keys immediately firing their bound action while being rebound, and the D-Pad can now be bound on the keyboard (#3).

### Visuals & Extras
- **Type-Colored Moves**: Move names in battle messages can now be tinted by type (`Extras > Move colours`).
- **Map Transitions**: Added options for Original, Fast, and Seamless transitions (`Extras`).
- **3D Outlines**: Now disabled by default; toggle between Off and Thin in `Settings > Display`.

### Engine & Performance
- **Performance**: ~25% reduction in CPU frame times in the overworld (#5).
- Added a warning when the graphics driver falls back to software (CPU) rendering.
- macOS: Fixed core crashes on Apple Silicon (thanks @DarthMDev, #2).
- Windows: Source builds now properly locate `SDL3.dll` (#4).

### Launcher
- **ROM Library**: Add any number of Pokémon Black and White ROMs (files, a folder, or drag and drop). Each is matched by its hash to the known good dumps, with its language, and shows as Verified, Modified or Missing. Files stay where they are, and a moved file can be located again.

### Battle Remastering & Tools
- **Battlefields & Camera**: Arenas now support multiple camera angles, trainers have dedicated field positions, and trainer sprite scaling persists on switch-ins.
- **Spot Editor**: Redesigned layout, added in-editor battle preview, and added opponent trainer scaling controls.

### Bug Fixes
- Fixed crash when switching Pokémon after an opposing Pokémon faints (#6).
- Fixed crashes during evolution sequences.
- Fixed crash when sorting items in the bag.

## 0.1.7

### One-Screen Mode & UI
- **Menus & Apps**: Smooth fades when menus and apps open and close, with no flashes or black frames, and a quick return to the menu. Menu icons use the game's own sprites, and the interface has more room in small windows such as 1280x720.
- **Pokédex**: Animated Pokémon sprites and a short opening animation.
- **Battle Menus**: Your HP plate stays at the bottom right, with actions and moves in a small grid above it, tinted by type. Moves use their modern names (Double Slap, Dragon Breath).
- **Starter Choice**: Widescreen gift box, the game's own text, and an animated sprite in the close-up with its cry.
- **Intro**: The trainer choice keeps the DS frame, and the flight over the house fills the window.

### Battle Remastering & Tools
- **Battle Cameras**: About 150 battle places now have a hand-placed camera. Story battles start where the player stands.

### Launcher & Settings
- **Launcher**: Shows when a new version is out, and Settings has a ROMs section for Black and White.
- **Settings**: Values no longer overlap their arrows, long text is shortened to fit small windows, and the CONTINUE card keeps its cut corner.
- **Downloads**: The macOS launcher is a disk image (.dmg) with a READ ME FIRST file, and each release has one file per download.

### Bug Fixes
- macOS: Fixed 3D scenes showing as a flat white or green screen (title, intro, starter choice). 3D edge smoothing is off on macOS for now.
- macOS: Fixed a crash when choosing your ROM.

## 0.1.6

### Platforms
- **64-bit Builds**: Windows, Linux (AppImage) and macOS (Apple silicon), with multiplayer and code mods. The game keeps the DS's own memory layout and plays the same. No more 32-bit packages.
- **SDL3**: Replaces SDL2.

### One-Screen Mode & UI
- **One-Screen Layout**: The game on one screen, wider than the DS, with a new interface for the menus, Bag, Pokédex, party, battles, Options, Save, trainer card, PC, name entry and title screen. Works with the mouse, and names can be typed on the keyboard. Start it with Remaster in the launcher, or `Options > DISPLAY > SCREENS > ONE`.

### Multiplayer
- Players who leave no longer stay on the map, the other player stays visible while a menu is open, and avatars keep up with running and cycling.
- Fast-forward is off in a session, joiners send much less data, and the name is asked when Multiplayer first opens.

### Launcher
- **Self-Updating**: The launcher updates itself on every platform and keeps running on its version if an update fails. Install this version once by hand; later ones arrive by themselves.
- New look, two play buttons (Remaster and Classic), and a game data folder per profile.

### Bug Fixes
- Fixed a crash when opening the trainer card.
- Removed NINTENDO WFC SETTINGS from the title menu (the service closed, and it crashed the game).
- The volume now starts at 50%.

## 0.1.5

### Visuals & Extras
- **3D Outlines**: At high resolution they now match the DS: no gap or thin line under objects on the floor, and no comb or flicker along edges.

## 0.1.4

### Visuals & Extras
- **Instant Text**: Each page of a message appears at once, whatever the text speed (`Settings > Extras`, or `Options > EXTRAS` in the game).

### Launcher
- Shows its real version, outlines the folder, copy and remove buttons, and highlights text boxes and lists on hover.

## 0.1.3

### Settings
- **In-Game Settings**: DISPLAY and SOUND pages in the game's Options screen, changed with the arrows in the bottom bar or with L and R.

### Engine & Performance
- No console window next to the game. Start `eidos --console` to get one for a bug report.

### Launcher
- The version list keeps working when GitHub limits requests.

## 0.1.2

### Visuals & Extras
- **Display Settings**: Pixel scale (how big each DS pixel is) and 3D resolution (how detailed the 3D is), each Auto or a fixed size. The 3D is now sharp by default.
- The window opens at a size that fits the screen.

## 0.1.1

### Bug Fixes
- Escape no longer closes the game. Use the window's close button.

## 0.1.0

- First release. The game asks for your own ROM on the first start.
- **Multiplayer**: Host a game through the public relay and share the code.
- **Launcher**: Profiles, versions and mods.
