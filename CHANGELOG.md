# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

## Unreleased

- Battle spots: every trainer the game can battle has a place in the table
  (433 of the 436 the scripts start; 26 new rows: Team Plasma grunts,
  gym trainers a leader's script calls, Cheren and Bianca on the routes,
  the Champion, The Riches and more), a fight's versions sharing a row.
- Spot editor: "Play battle" starts the game straight into the battle at
  this place and leaves it to you (keys, pad or mouse), on a copy of your
  save, from the encounter itself: the flash and the transition play at
  their own speed, and a trainer brings their own effect and music.
  "Preview in game" still skips to the battle and plays it by itself.
- One-screen layout: the wild encounter effects fill the window (the swirl
  into the grass showed boxed at the DS size for a few frames), and the
  white flash before a battle fills it too.
- Controllers: on Xbox and PlayStation pads the bottom button now confirms
  and the right one goes back (A and B on Xbox, cross and circle on
  PlayStation); Nintendo pads keep the DS layout. Controls you set
  yourself stay as set.
- One-screen PC: the game's own sounds for moving the cursor, picking up,
  putting down, putting back and a refused move. After a key press, a
  mouse resting over a slot no longer takes the selection back until it
  moves.
- One-screen layout: the boot logos, the intro, the title menu and the new
  game's name entry show in the one-screen layout again (they fell back to
  the two DS screens since the aletheia update earlier in this release).
- Remaster battles, double and triple: the Pokemon choosing has corners
  around it on the field. Picking a target puts the corners on the Pokemon
  themselves instead of a list: the arrows move between them as they
  stand, the mouse picks the one under it, and A or a click takes it. A
  move that hits several (Earthquake, Surf) takes them all from any one.
- One-screen PC: a new layout with the party on the left, the box in the
  middle and the selected Pokemon's details on the right. Moving a Pokemon
  takes effect at once with the keyboard, a pad or the mouse (click or
  drag), and the arrows follow the layout on screen. Summary opens straight
  from its button, each box shows how full it is, the game's message
  window no longer shows through, and sprites keep their size at 720p.
- One-screen PC: Move Items has its own screen. Take an item from a
  Pokemon and drop it on another to give it, or to swap the two items.
- One-screen PC: after leaving and coming back to the PC, the keys move
  the new layout again instead of the game's old cursor.
- Remaster battles: on the target list of double and triple battles, up
  and down walk the list as shown. In the Bag, BACK sits at the bottom
  right as on the DS, and BACK in the Bag and the party list lights up when
  the keys reach it.
- Source builds on Windows: SDL3.dll is copied next to eidos_boot.exe, so it
  starts without it on the PATH (reported by raspberryzombie).
- 3D outlines: off by default, as most players prefer. Settings > Display
  has "3D outlines" (Off or Thin). Thin draws the edge one screen pixel
  wide at any size, so it no longer makes spikes and combs at high
  resolution.
- Controller icons: hints show the controller's own buttons when a pad was
  used last: Xbox 360, Xbox One / Series, PS3, PS4, PS5, Switch, GameCube
  and Steam Deck, drawn for eidos (no maker's logos). Settings > Controls
  has "Button icons" (Auto follows the controller), and the Controls rows
  show the pad's icon next to the key.
- Hint bars: the text sits level with its key or button.
- Battle spots: every scripted story fight has its own row (Nuvema Town's
  second fight among them), and a fight's versions (one per starter) share
  it.
- Remaster battles: a battle place can hold several views. The battle
  takes the one nearest where the player stands, or the one closest to the
  way they face. Each view sets its own command camera (zoom, turn, height,
  slide, tilt, aim) and moves the foe's and your trainer.
- Spot editor: Pokemon at their in-game size, trainer sprites drawn as the
  game draws them (no stray second head), your trainer as the boy or the
  girl (taken from your save), and controls for views, the camera and the
  trainers.
- Remaster battles: move names in battle messages take their type's colour
  ("Tepig used Ember!" with Ember in fire red). Extras has a "Move colours"
  setting to turn it off.
- Controls: setting a key no longer also acts as that button. Setting A
  no longer gets stuck, setting B no longer leaves the screen, and setting
  L, R or a D-Pad direction no longer changes the section or moves the
  cursor.
- Remaster battles: the prompt ("What will Tepig do?") no longer shows
  twice while the action menu fades in. It stands in the message box and
  shows the game's own text.
- Network: when another program holds the external control's port, the
  control now reports it and stays off, instead of seeming on while tools
  reached the other program.

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
