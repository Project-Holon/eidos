# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

## 0.2.4

### Engine & Performance
- **Native Renderer**: `Settings > Display > Renderer > Native` draws with Direct3D 12 on Windows, Metal on Mac and iPhone and Vulkan on Linux and Android, through SDL3's GPU API; OpenGL stays the default and takes over when the native renderer cannot start.
- **VSync**: `Settings > Display > VSync` (on by default): on a 60 Hz display, or 120 or 240 Hz, the game runs at the display's own rate and every frame waits for the screen, so each frame shows for the same time instead of one now and then showing twice. Fast forward and other rates (50, 75, 144 Hz) pace by the clock as before.
- **Benchmark**: `--bench FILE` writes each frame's timing and a summary (percentiles, late and dropped frames, the system and renderer); `tools/bench.py` runs the standard field and battle scenes.
- **Nintendo Switch and PlayStation Vita (early)**: First homebrew builds for consoles with custom firmware: the Switch as an `.nro` (OpenGL, the one-screen layout), the Vita as a `.vpk` (OpenGL through vitaGL when the console's shader compiler is extracted, else SDL's renderer). Both have run only in emulators so far: expect rough edges, and reports are welcome.
- **Native 2D**: With the native renderer, the DS's 2D layers are drawn on the GPU as well (Settings > Display > GPU drawing), as OpenGL already did: the same picture, pixel for pixel, with less work on the CPU.

### One-Screen Interface
- **Key Item Showcase**: Receiving a key item (the Xtransceiver, the C-Gear and the others) plays its moment in the one-screen layout again, in the interface's style: a flash, the item spinning up out of the light in the middle of the screen with a burst of stars during the jingle, then settling into its card as the message opens. Before, the screen froze through the jingle.
- **Badges**: Receiving a Gym Badge plays the key item's moment with the badge, as the Trainer Card's badge case draws it, then its card over the game's message; before, the badge spun over a zoomed copy of the DS screen (#24).
- **Field Moves**: Cut, Strength, Surf and the other field moves show their cut-in across the whole window: a flash, then a band with the Pokémon crossing it, over the field; before, the band was the DS screen's width with mismatched sides (#24).
- **Teaching a TM**: The list that teaches a TM (and the one for an evolution stone) is one-screen, with each Pokémon's ABLE, UNABLE or LEARNED in place of its HP; before, it switched to the DS screens (#24).
- **Status Conditions**: The battle HP plates show poison, paralysis, sleep, burn and freeze, at the moment the game's own gauge shows them (#24).
- **Double Battles**: Both opposing Pokémon have their HP plates; before, only one showed (#24).
- **VS Screens**: The VS banner before a story battle (the first one with your rival included) shows in the one-screen layout again, across the whole window, over the room around it; before, the screen froze and the banner never appeared. Other cut-ins the game draws over its screen show the same way.

### Bug Fixes
- **Item Card**: The item card no longer stays on screen for a moment after its message box closes.
- **Trainers' School Blackboard**: Closing the blackboard's list of topics with B (Cancel) no longer closes the game (#24).
- **Interface Translations**: A mod's `ui_text` file now also translates the labels built from values (`PWR %d  ACC %d`, `Lv. %d`, `Received %s` and about 40 more), the settings' key names and the multiplayer and timer lines; a translation whose `%` codes do not match the English is ignored. The template lists every label the interface draws, the Settings page and the multiplayer messages included, and the multiplayer pause screen draws its text in the game's font, so a translation's own letters show there too. "Lv." in the party lists and a long translation of it in battle now come through too (#21).
- **GPU Drawing Setting**: `Settings > Display > GPU drawing` shows On and Off like the other switches, not "on" and "off".
- **Move Names**: The modern English move names (Hi Jump Kick to High Jump Kick and the others) now replace only the English cartridge's names; before, they replaced the move names of every language and of mods (#21).

### Mods & Launcher
- **Mods Page**: Opening the Mods tab again no longer downloads the catalog again or empties the list for a moment; the catalog loads once a launcher run, and the refresh button loads it again.
- **GitHub Outages**: When GitHub's download service fails (it answered 504 for every mod for a while), the launcher fetches the file through GitHub's API instead, for versions and mods alike, and only while enough of the hour's API requests remain. Each mod's details are kept once read, so the Mods page shows them without asking GitHub again until a mod has a new release.

## 0.2.3

### Mods & Launcher
- **Mod Icons**: The Mods page shows each mod's icon.
- **Whiteout for Mods**: A code mod can white the player out as after a lost battle (`whiteout`): the blackout screen, the last Pokémon Center and the nurse.
- **Mod Warps**: A mod that sends a door to a cell (`map.warp`) now lands the player on that cell; before, a door's own exit was looked up in the new zone.
- **Battle Moves for Mods**: `battle_get` reads a Pokémon's moves and PP as the battle has them, and `move_value` reads all three stat changes of a move.
- **Rules**: `player_x` and `player_z` give the cell the player stands on.
- **Interface Translations**: A mod can bring the one-screen interface's labels in its own language with a `ui_text` file; `EIDOS_UI_TEXT_DUMP` lists the labels a run draws, for translators (#21).
- **Downloads**: The Android APK and iOS IPA keep one name across releases, so the README's download links always give the latest; the release notes list the launchers and phone apps first.

### Bug Fixes
- **Pokédex Crash**: Opening the Pokédex for the first time no longer crashes the game (#22).
- **Pokédex Tabs**: In the one-screen layout, clicking CRY no longer opens AREA; each tab opens its own page (#22).
- **Field Reads for Mods**: A mod's reads in the field (the party, the zone, the player's position) now work before the first battle; the player's position never worked before.
- **Battle Reads**: A mod's reads and writes of a Pokémon in battle (`battle_ref`, `battle_get`, `battle_set`) now reach the Pokémon asked for; before, every one reached the Pokémon in front, so the bench and the trainer's own side could not be read. A write reaches the battle's own copy too, so a Pokémon a mod puts at 0 HP counts as fainted when the battle checks who can still fight.
- **Mod Start Reads**: A mod that reads a move or an item (`move_value`, `item_value`) when it starts no longer crashes the game; the read returns -1 until the game data is open.
- **Whiteout Event**: The `whiteout` event now fires when you lose a battle, wild or trainer. Before, it never fired in a real game, so a Nuzlocke or Soul Link run did not end after a lost battle.
- **Rules Mods Remember**: A rules mod's saved values (a Nuzlocke's lost Pokémon, its used areas, its end) are now kept with the save. Before, the first rules mod loaded kept them only until the game closed.
- **Launcher Icons**: Item icons are centred in their square.

## 0.2.2

### Mods & Launcher
- **Get Mods**: The Mods page lists the mods of katalogos, our mod index, with Install and Update; a download is checked before it is unpacked. The official mods are Nuzlocke, Soul Link (multiplayer) and Exp. Share.
- **Mod Icons**: A mod can show an item's or a Pokémon's picture from your own ROM, or its own image.
- **Black & White**: The Default profile is now Black, a White profile is added, and the sidebar switch picks between them; the launcher's look follows (Reshiram by day for Black, Zekrom by night for White).
- **Saves**: The Play page shows the saves of the profile's game only.
- **ROMs**: A profile plays the language your ROM is in when you have only one (an Italian Black ROM alone no longer asks for an English one), and Play asks for the ROM a game is missing.
- **Mod Interface 5**: Experience for the whole party (`battle.exp` share), and hooks for battle AI mods: the trainer's item, switch and move choices, the Pokémon it sends in, abilities, stat changes, held items, statuses and turns, with reads for stat stages, the type chart and what a species can learn.

### Battle
- **YES / NO**: The one-screen choice shows the game's own answers (FORGET when a move is replaced) instead of "YES" or garbled text (#17, thanks aqali1).
- **Party List**: The one-screen list says why a Pokémon cannot be chosen ("no energy left to battle") (#18, thanks aqali1).

### Bug Fixes
- **European Cartridges**: Fixed the game crashing on Pokémon and Options in the field menu on Italian, French, German and Spanish cartridges.
- **Code Mods**: Fixed the game crashing at start-up with any mod that runs code.
- **Faint Events**: A mod's faint rule now fires once per fainted Pokémon, not for every Pokémon a move touched.

## 0.2.1

### One-Screen Mode & UI
- **Giving Items**: Giving an item from the Bag uses the one-screen party list ("Give to which Pokémon?", swapping held items) instead of the DS screen.

### Battle Remastering & Tools
- **Battle Spots**: Fixed the camera of N's battle in Accumula Town sitting far out.

### Bug Fixes
- **Launcher**: Fixed the box legendary, badges and trainer pictures showing as broken images.
- **Launcher**: Fixed "This release has no notes" when GitHub's request limit is reached; the notes now come from the changelog.

PS: Honestly I'm only pushing this because the launcher is broken LOL.

## 0.2.0

### One-Screen Mode & UI
- **Text Boxes**: Speech, narration and system messages are redrawn as one-screen boxes in the game's font; lists, YES / NO and the field's menus (Union Room talk, Battle Subway) are our own lists.
- **New Screens**: Pokédex search, catching and nicknaming, town map and Fly, move relearner, Egg hatching, in-game trades, Xtransceiver calls (up to four people), Hall of Fame and credits, trainer card, Entree Forest and Pass Powers, Survey Radar, PC box list and marking.
- **Field Windows**: Money, items received and place names have their own cards; the season card stays the game's.
- **Battle**: Ability pop-ups and the trainer intro are drawn as plates.
- **Union Room & Battle Subway**: Trading (offers, boxes, feelings, summaries), battles (rule card, Pokémon entry, VS screen), drawing, Spin Trade (lobby, Egg list, spinner) and the Battle Subway's Pokémon entry list are one-screen; the Union Room card has the new window style.
- **Mystery Gift**: Its menus, gift list, Wonder Cards and Card Album are one-screen; a received gift shows its Pokémon or item with its card.
- **Title Screen**: The logo is the cartridge's own (Black or White, each language).
- **Place Names**: The place's name changes in the top bar instead of a sliding banner (#15).
- **C-Gear**: The top bar's C-Gear button shrinks to its mark on narrow windows; IR is greyed out.
- **UI fixes**: Top bar, balloon tails, nickname screen, text colours and backdrop culling, thanks @TbangIT (#14).

### Mystery Gift
- **Online gifts**: Receiving over Nintendo WFC gets the event gifts from the relay: each runs from its original day of the month for 7 days, for your region, language and game.
- **Wireless gifts**: Receiving over wireless lists the events that were given out locally; pick one and receive it as on the DS.
- **Infrared**: Greyed out.

### Link Play
- **Union Room over the internet**: Two eidos players meet in the Union Room through the relay, with no code to type, and can trade and battle as on two DS consoles.

### Phones
- **Android and iOS**: First builds of eidos for Android (APK) and iPhone (IPA, unsigned: install it with a sideloading tool), one-screen only.
- **iPad**: Both orientations; the one-screen interface fits narrower windows, and the settings tabs wrap instead of dropping off.

### Saves
- **First Save**: A New Game writes no file until you save; the save goes to its own slot.
- **Save Warning**: A red warning shows when saves keep failing, and fades after a few seconds.

### Languages
- **Every European cartridge plays through battles**: Fixed the move-forget screen crashing (and other screens' windows breaking) on French, German, Italian, Spanish and White cartridges.
- **Interface in the game's language**: The one-screen interface's labels, buttons and hints follow the cartridge's language (French, German, Italian, Spanish), with the games' own words.
- **German Black**: Fixed a crash when loading a save in some places (the Nimbasa City Pokémon Center).
- **Accented letters**: Ö, Î, Â and other accented capitals no longer show as Greek letters in the small font.

### Graphics & Performance
- **GPU drawing**: The DS screens are now composed on the graphics card, including the 3D at full resolution, about 1 to 1.5 ms less CPU per frame (#5). `Settings > Display > GPU drawing` turns it off.
- **Full-window fades**: Fades and flashes (the starter gift box) now cover the whole one-screen window instead of a 4:3 square.

### Touch
- **Item button**: The field's top bar has an Item (Y) button for the registered item, so the field needs no keys on a touch screen.

### Spot editor
- Drawn with the GPU: about 11 ms a frame at 1440p (was 28), dragging a spot about 20 ms a step (was 65).
- Shows which battles are double, and previews the trainer's own battle.
- **Progress sheet**: Every place in story order with its trainers and wild areas checked, and totals per section.
- **Spots**: The hand-placed spots ship in the built-in table; story fights are placed from their scenes; Black City and White Forest have their buildings, trees and residents (#8).

### Bug Fixes
- Fixed crashes when a wild battle opens, in in-game trades, in the Union Room and when the Spin Trade starts.
- **C-Gear**: Fixed the crash when the scientist in Striaton City gives you the C-Gear (#13).
- **Title Screen button**: Fixed the white screen after the second click; the game now restarts at the title (#11).
- **Soft reset**: L + R + START + SELECT, leaving Mystery Gift and the game's other resets now restart at the title instead of stopping.
- **Mystery Gift**: Fixed crashes when a received Pokémon gift's Wonder Card opens.
- **Geonet**: Fixed the globe on the PC.

## 0.1.9

### Bug Fixes
- **Saves**: Fixed title screen failing to display existing saves after quitting an unsaved New Game; the menu now properly selects the last-played save (#10).
- **One-Screen UI**: Fixed naming keyboard rendering behind the Multiplayer screen and softlocking input (#9).
- **Keyboard Navigation**: Fixed one-screen keyboard D-pad/arrow controls using original DS key coordinates instead of matching the visible layout (#7).

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
