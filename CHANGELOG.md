# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

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

### Battle Remastering & Tools
- **Battlefields & Camera**: Arenas now support multiple camera angles, trainers have dedicated field positions, and trainer sprite scaling persists on switch-ins.
- **Spot Editor**: Redesigned layout, added in-editor battle preview, and added opponent trainer scaling controls.

### Bug Fixes
- Fixed crash when switching Pokémon after an opposing Pokémon faints (#6).
- Fixed crashes during evolution sequences.
- Fixed crash when sorting items in the bag.

## 0.1.7

- macOS: 3D scenes draw again. The title screen's Reshiram, the new
  game's house scene and the starter choice showed as a flat white or
  green screen. 3D edge smoothing is off on macOS for now.
- macOS: choosing your ROM no longer crashes the game.
- Remaster battles: about 150 battle places now have a camera placed and
  checked by hand, so the battle shows the right part of the map. Story
  battles start where the player stands.
- Remaster battle menus: your Pokemon's HP plate stays at the bottom
  right, and the actions and moves show as a small grid above it, in each
  move's type colour. Moves use their modern names (Double Slap, Dragon
  Breath).
- Remaster starter choice: the gift box in widescreen, the game's own text,
  and a small box with the Pokemon's animated sprite in the close-up. Its
  cry plays when the close-up opens.
- Remaster intro: the trainer choice keeps the DS frame, and the flight
  over the house fills the window.
- Remaster menus: menus and apps open and close with smooth fades, with no
  flashes or black frames, and going back to the menu from an app is
  quick. The menu icons are the game's own sprites. The Pokedex shows each
  Pokemon's animated sprite and opens with a short animation. The
  interface has more room in small windows such as 1280x720.
- Launcher: Play shows when a new version is out, and Settings has a ROMs
  section for Black and White.
- Settings: values no longer overlap their arrows, and long text such as
  the mods folder is shortened to fit small windows. The CONTINUE card on
  the title menu keeps its cut corner.
- Downloads: the macOS launcher is a disk image (.dmg) with a READ ME
  FIRST file for the first start, and each release has one file per
  download.

## 0.1.6

- 64-bit on Windows, Linux (an AppImage) and macOS (Apple silicon), with
  multiplayer and code mods. The game keeps the DS's own memory layout and
  plays the same as before. There are no 32-bit packages any more.
- One screen: a new layout that shows the game on one screen, wider than
  the DS, with a new interface for the menus, the Bag, the Pokedex, the
  party, battles, Options, Save, the trainer card, the PC, name entry and
  the title screen, in the game's own colours, font and icons. It works
  with the mouse, and names can be typed on the keyboard. Start it with
  Remaster in the launcher, or choose ONE in Options, DISPLAY, SCREENS.
- Multiplayer: a player who left no longer stays on the map, the other
  player stays visible while a menu is open, avatars keep up with running
  and cycling, fast-forward is off in a session, a joiner sends much less
  data, and the name is asked when Multiplayer first opens.
- The launcher updates itself when it starts, on every platform, and keeps
  running on the version it has if an update fails. Install this version
  once by hand; later ones arrive by themselves. It also has a new look,
  two play buttons (Remaster and Classic), and a game data folder per
  profile.
- SDL3 in place of SDL2.
- Fixes: the trainer card no longer crashes the game; NINTENDO WFC
  SETTINGS, which closed years ago and crashed the game, is gone from the
  title menu; the volume starts at 50%.

## 0.1.5

- 3D outlines at high resolution match the DS: no more gap and thin line
  under objects standing on the floor, and no comb or flicker along edges.

## 0.1.4

- Instant text: each page of a message appears at once, whatever the text
  speed. It is on the new Extras tab in Settings and the EXTRAS page in the
  game's Options.
- Launcher: shows its real version, outlines the folder, copy and remove
  buttons, and highlights text boxes and lists on hover.

## 0.1.3

- The port's settings inside the game's Options screen: DISPLAY and SOUND
  pages next to the game's own, changed with the arrows in the bottom bar or
  with L and R.
- No console window next to the game. Start `eidos --console` to get one for
  a bug report.
- Launcher: the version list keeps working when GitHub limits requests.

## 0.1.2

- Two display settings, each Auto or a fixed size: Pixel scale (how big each
  DS pixel is) and 3D resolution (how detailed the 3D is). The 3D is now
  sharp by default.
- The window opens at a size that fits the screen.

## 0.1.1

- Escape no longer closes the game. Use the window's close button.

## 0.1.0

- First release. The game asks for your own ROM on the first start.
- Multiplayer through the public relay: host a game and share the code.
- The launcher: profiles, versions and mods.
