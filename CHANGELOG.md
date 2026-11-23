# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

## 0.2.6

Overhauled 3D battle scenes, expanded multiplayer and randomizer/Archipelago support, Mod API v6, and bug fixes.

### Real-Map Battles & Spot Editor
- **3D Props, Buildings & Interiors**:
  - Replaced flat billboard sprites with full 3D models and native ground shadows for rocks, living-room pouffes, Dreamyard bottles, warp pads, the Pokémon League plaza gate, and Relic Castle vases.
  - Added separate 3D door models to houses, labs, and Pokémon Centers so doorways no longer render as empty black openings.
  - Extruded back walls, roof ends, and corners from native textures for buildings that only had camera-facing geometry in the overworld (houses, Pokémon Centers, shops, office blocks, towers, the lighthouse, the Ferris wheel, and the station hall), both up close and as distant landmarks (such as the Icirrus City Pokémon Center viewed from Route 8).
  - Completed back faces, full 3D bodies, and floor grounding for indoor furniture and props (cupboards, bookshelves, fridges, televisions, vending machines, lockers, kitchen counters, tables, statues, chandeliers, and potted plants), and added missing south walls and doorways to shared house interiors (Driftveil, Mistralton, Undella, White Forest) and bridge gate halls.
- **3D Grass & Foliage**:
  - Replaced flat crossed-tuft sprites for tall grass, dark grass, long grass, and White Forest grass with seasonal 3D foliage models using native palette colors.
  - Pokémon now stand inside their tile's grass cluster with per-blade depth sorting so individual leaves occlude sprites cleanly instead of clipping along a flat plane.
  - Replaced the flat top plane of long grass (which previously submerged Pokémon up to their heads) with full-height blades that flatten around each combatant across single, double, triple, and rotation battles.
  - Limited detailed 3D grass generation to tiles near the battle while keeping lightweight tufts in the distance, and added back-face visibility to seasonal plant rows (spring/summer flowers, autumn stalks, and winter tufts across Routes 1, 2, 3, 17, 18, and neighboring towns).
- **Live Overworld State**: Real-map battles now reflect the active state of dynamic and story-swapped map objects at the moment an encounter begins, including opened gym curtains and switches, pushed strength boulders, Route 4's moving trucks, Chargestone Cave's floating stones, the Pokémon League's light pillars, and post-story map replacements.
- **Water, Sky, Weather & Fog**:
  - Extended ocean geometry past map boundaries and across empty map cells, removed distant sky-color terrain fading, and generated rock/grass cliff skirts beneath distant coastlines so far-off land no longer floats in midair (Route 1, Route 18, P2 Laboratory).
  - Animated water surfaces and shorelines in battle to match the overworld, and anchored animated surface decals (Pinwheel Forest's drifting pond leaves, shoreline foam) directly onto the water plane beneath Pokémon.
  - Added area-specific procedural voxel clouds to outdoor battles that drift in real time and respond to time-of-day lighting, while disabling clouds inside caves and indoor areas (Wellspring Cave, Chargestone Cave, Relic Castle).
  - Ported overworld weather effects into real-map battles using the game's native weather routines, tables, and sprites (rendered cleanly beneath battle UI panels), including particle systems for snow, rain, sandstorm, blizzard, downpour, thunderstorm, storm, and diamond dust, plus scrolling overlay sheets for blizzards, storms, and the Desert Resort sandstorm.
  - Applied native per-weather lighting and fog tables to real-map battles so rain, snow, sandstorms, and fog tint and haze the environment just like in the overworld, with an optional `Extras > Weather haze` setting (off by default) to apply weather haze to Pokémon and trainers as well.
  - Matched battle fog depth calculation to the overworld camera axis so distant cave and room walls retain their geometry instead of flattening into solid fog (Chargestone Cave, Driftveil Gym).
- **Location & Dungeon Geometry**:
  - **Nuvema Town**: Restored missing south walls and doorways and grounded furniture in the player's bedroom.
  - **Striaton Gym**: Completed walls, booths, chairs, dividers, and doorway curtains, and replaced flat props with 3D teacups, potted plants, and candles.
  - **Nacrene Gym**: Completed room geometry for the library and Gym Leader's chamber.
  - **Castelia City**: Restored missing south walls and doorways and grounded furniture across office building interiors.
  - **Castelia Gym**: Added back-face geometry to the honeycomb walls and puzzle gates.
  - **Liberty Garden**: Completed the island's missing terrain and extended the surrounding sea, removed a neighboring area's map that rendered beside the island, restored south walls and doorway geometry in the lighthouse room, and completed north faces on the basement storage blocks.
  - **Relic Castle**: Enclosed room walls, rebuilt the ruined bedroom, stood exit doorways upright, removed spurious generated walls around quicksand pits, and grounded floating vases onto their pedestals.
  - **Nimbasa City**: Added enclosing walls and roofs (textured from each building's exterior models) to the Big Stadium's football, baseball, and soccer fields and the Small Court's tennis and basketball courts so interior battles no longer sit in an empty void.
  - **Nimbasa Gym**: Extruded roller-coaster rails and tracks into full 3D geometry, modeled raised 3D switch pads, and synced track/pad colors to the active switch state.
  - **Route 5**: Closed terrain seams where connected road surfaces drop onto a lower layer by the gate path, and resolved z-fighting stripes on building floors and ground shadows that sit flush with the terrain in the gate tunnel.
  - **Driftveil Gym**: Positioned battles on each trainer's adjacent walkway and floor level; completed geometry for traffic cones, walkway undersides, lift posts (including railed lift back faces), and pit walls; grounded cones and pads onto walkways; closed front/back gaps between lift arrow signs and their posts; and synced lift up/down arrow lighting to the lift's state.
  - **Cold Storage**: Completed walkway crates and ice blocks, grounded stacked ice blocks onto the blocks beneath them, and restored missing south walls and doorways in the Team Plasma container room.
  - **Route 6**: Closed the open east face of the tree-topped riverside ledge with wrap-around terraced steps matching the route's ledges.
  - **Mistralton Gym**: Added 3D relief to cannon arrow plates, completed back-face geometry on entrance statues, and adjusted Pilot 2 and Worker 5 battle placements to clear nearby fences.
  - **Route 7**: Rebuilt the raised wooden walkways with four-sided support posts to the ground, plank thickness, and undersides; completed the back and west faces of the cabin door; and placed walkway trainer battles onto the planks rather than the ground below.
  - **Icirrus Gym**: Replaced flat snowy rock and icicle sprites with 3D meshes, completed back-face geometry on entrance pedestals and platform mechanisms (levers, gears, floor switches), and repositioned two obstructed trainer battles.
  - **Dragonspiral Tower**: Restored missing south walls on the pool and fallen-pillar floors while keeping doorway exits open, completed underside/south faces on upper-floor ledges, ramps, and stairs, extended upper-floor ramp and doorway landing walls down into the pit using matching block textures, and assigned dedicated battle spots for each trainer.
  - **Route 8**: Rebuilt the pond stakes as cylindrical 3D wooden posts with water-surface ripples instead of flat angled sprites, and replaced the flat tilted driftwood sheet in the pond with a 3D branched trunk.
  - **Moor of Icirrus**: Rebuilt the marsh pond stakes as cylindrical 3D wooden posts with water-surface ripples and replaced the flat driftwood sheet with a 3D branched trunk.
  - **Tubeline Bridge**: Recognized the bridge's translucent grating deck as a valid battle floor instead of dropping battles onto the girders underneath.
  - **Shopping Mall Nine**: Added back faces to railings, stair sides, benches, planters, and counters; restored the missing windowed south wall around the open entrance; and grounded cash registers and shelving onto the shop counters.
  - **Opelucid City**: Restored missing south walls and doorway lintels in both trainer houses, and fixed floating tabletop geometry on the upper-floor dining table.
  - **Opelucid Gym**: Completed north faces on the four entrance statues and entrance walkway, replaced flat dragon crest sprites above the front statues with 3D geometry, and moved the story Veteran's battle from the hall floor onto his elevated walkway.
  - **N's Castle**: Closed open sides on the throne platform and the steps below it.
  - **Village Bridge**: Added 3D plank depth to the three riverside benches and grounded the two floating hedge planters behind them onto the street.
  - **Undella Town**: Restored missing south walls, entrance alcove sides, and doorway geometry in the villa, replaced the table's flat vase flowers with a 3D model, and filled hollow gaps inside the fountain hedges.
  - **Abundant Shrine**: Removed a black seam down the shrine hut's side and sealed the gaps under its eaves where the side walls stopped short of the roof.
  - **Black City**: Grounded floating shop-counter props (lamps, glass cases, and boxes) onto the counters.
  - **White Forest**: Grounded floating tall grass (previously hovering 8 units above the floor) and great tree roots (previously hovering 4 units above) onto the forest floor with proper ground shadows.
- **Camera & Placement Handling**:
  - **Sprite Depth & Occlusion**: Pokémon and trainer sprites are now depth-tested at their standing tile coordinates so foreground map geometry accurately occludes only the parts it covers.
  - **Tree Occlusion & Seasons**: Trees intersecting or adjacent to the battle camera (including Pinwheel Forest's dark teal trees) are culled cleanly as whole models across both camera angles and in every season without popping mid-battle. Tree culling and Pokémon ground height now use native material names and collision tile heights rather than texture colors.
  - **Camera Clearance**: Walls and small props that the battle camera sits inside or immediately behind are now culled in their entirety, including their top faces.
  - **Story & Legendary Encounters**: Added dedicated battle spots for Professor Juniper's Route 1 catching tutorial and script-triggered encounters without an overworld NPC (post-game Reshiram/Zekrom atop Dragonspiral Tower, Dreamyard Musharna, and Lostlorn Forest Zoroark), and moved the Landorus encounter in front of the Abundant Shrine rather than past the map border.
  - **Rail NPCs & Trainer Spacing**: Properly resolve coordinates for NPCs placed on walkway rails (Opelucid Gym, Castelia City streets), and increased default combatant spacing for trainer battles without altering hand-tuned spots.
  - **Multi-Layer & Obstructed Ground**: Fixed-spot wild area views now sample floor height from the spot's own tile rather than the floor nearest the player (fixing sunken Pokémon in two Wellspring Cave views). Battles triggered on obstructed or seasonally blocked tiles now search outward for valid ground instead of placing Pokémon inside trees.
  - **Tall Grass & Flowers**: Anchored grass and flower placement to world map tiles so foliage stays consistent regardless of battle spot or camera angle.
  - **Surfing Mounts**: When starting a battle while Surfing, the water mount now floats beneath your active Pokémon rather than remaining at its overworld tile, and is hidden if that same Pokémon is fighting.
- **Seamless Battle Loading & Culling**: Eliminated the loading pause when entering real-map battles: rebuilt map geometry is cached to disk, area models pre-build in the background upon map entry, battle scene assembly runs on a worker thread during the encounter transition, and GPU uploads happen across the transition's blank frames. Off-screen map geometry is culled in both renderers.
- **Spot Editor**:
  - Side-by-side workspace showing the Intro and Command camera views alongside the Overworld view, tile map, and location list, with native HiDPI rendering and adjustable UI scaling (`Ctrl`/`Cmd` + `+`, `-`, `0`).
  - Displays both opponent trainer positions (initial battle placement and post-battle return tile), per-floor tile maps and Overworld isolation for stacked interiors (Driftveil Gym), proper multi-layer ground height (Pinwheel Forest), Professor Juniper's back sprite during the catching tutorial, and all eight Gym Badges in the built-in preview save.
  - Resolved missing opponent sprites in the Overworld view for dynamic or script-spawned encounters (stadium visitors, story battles, Drayden, Landorus, Musharna) by falling back to trainer class or Pokémon species sprites, or a default marker when none is specified.
  - Avoided full battle placement recalculation while dragging cameras or adjusting sliders, making camera manipulation several times faster on large outdoor maps.
  - Added automatic hot-reloading when the spot file changes on disk, update notifications, automatic migration when built-in battle tile coordinates change, and merged hand-tuned battle spots for Liberty Garden, Nimbasa City, Cold Storage, Route 6, Mistralton Cave, Route 7, Dragonspiral Tower, Route 8, Shopping Mall Nine, Route 9, Black City, and White Forest into the built-in table (407 verified spots total; #8).
  - Fixed Black City and White Forest residents rendering below ground level, translucent foreground geometry (bridge and tree shadows) rendering as solid black artifacts over Pokémon sprites, and vertical placement for Driftveil Gym lifts and the Champion's platform.

### Multiplayer, Randomizers & Archipelago
- **Redesigned Multiplayer Hub**: Rebuilt the one-screen Multiplayer menu across all three UI styles, combining your Trainer Card, game creation cards, a unified **Join** box, and **Your Games** on a single page. The lobby now displays connected player avatars in a row, readiness status, active session rules, and the room code above the Start button.
- **Unified Join Box**: Enter any room code (for a friend's co-op session, a shared multiworld, or an Archipelago server as `host:port` with slot name and password) in a single input box. Room rules and connected players are previewed before joining, and invalid codes are rejected without creating a save file.
- **Your Games List**: Co-op sessions you host and multiplayer/modded runs started from the Multiplayer screen are now grouped under **Your Games** (labeled `Host` or `Joined`) and launched from there instead of cluttering the title menu's Continue screen.
- **In-Game Check Tracker**: Added a `Tracker` button to the overworld top bar during tracked runs (randomizers, multiworlds, and Archipelago sessions). Displays an interactive Unova map with color-coded check completion squares (green = all reachable, orange = partially reachable, red = unreachable, grey = completed), a per-location checklist, key progression items, and a live player-location map.
- **Archipelago Integration**: Join Archipelago rooms directly from the Join box, or export the host YAML options template from the Multiworld setup card.
- **Trainer Customization**: Customize your trainer's hair, outfit, trousers, and skin tone on the Multiplayer screen. Custom palettes appear in lobbies, the overworld, and battles across all Multiplayer-launched modes (and are visible to other players), while standard single-player saves keep vanilla palettes.
- **Mod-Defined Game Modes & Lobby Rules**: Mods can register custom game modes (randomizers, multiworlds, Archipelago presets) with structured, documented option pages that always initialize in a fresh save slot. Co-op hosts can configure mod rules (such as Nuzlocke lives and level caps) directly in the lobby for all participants.

### One-Screen Mode & UI
- **Dowsing Machine Overlay**: Compacted the Dowsing Machine into a top-right HUD card so it no longer obscures the player in 4:3 and tall windows, and changed the top bar's `Menu` button to `Close` while active; item-found messages show over the card (thanks @aqali1, #36, #43).
- **4× Fast Forward**: Added `Tab + F` for 4× speed (between `Tab` at 2× and `Left Shift + Tab` uncapped), accompanied by a theme-styled speed badge in one-screen mode (thanks @aqali1, #42).
- **Caught Species Indicator**: Wild battle plates now display a caught Poké Ball icon next to the Pokémon's name, styled to match the active UI theme.
- **Touch List Scrolling**: Lists with scrollbars now support touch and mouse dragging, either by dragging the list content directly or dragging the scrollbar thumb, without triggering an accidental tap on release.
- **Pokémon Musical Dress-Up**: Added a one-screen layout for the Musical dressing room featuring full-scale stage rendering, themed dialogue boxes, and mouse/touch stylus controls (gamepad/keyboard support planned; #44).
- **Plasma Style Refinements**: Anchored Plasma dialogue bands consistently along the bottom edge above the hint bar, and updated battle party `BACK`, post-faint switch, and `RUN` buttons to use Plasma's angled button style.
- **Keyboard & Settings Navigation**: Added a `Back` button (and `B` on an empty field) to the multiplayer name and room-code keyboards to cancel without clearing existing text, and enabled silent page scrolling with the mouse wheel in Settings.

### Modding
- **Mod API v6**:
  - Patch archive members and text strings at runtime from code (including archives already held open by the game), read/write script work variables, grant Gym Badges, trigger field scripts, customize the type chart, prompt the player for text input, manage per-mod persistent files, and participate in online `metaxy` world sessions.
  - Added hooks to override HM move deletion, shiny odds, Gracidea usage, active season, wild encounter slots, type matchups, starter selection, and field move availability (`move.known` for using Cut, Strength, Surf, and Waterfall without teaching them).
  - Extended mod settings definitions with text inputs, custom game modes (`mode` with `joins`, `in=`, and `export`), host-synced lobby rules (`@lobby`), conditional option rows (`?key=value`), and `heading`/`help`/`yaml` metadata, queryable via `setting_text`, `mode_get`, `mode_code`, and `player_name` (`docs/mod-services.md`).

### Fixes
- **Diamond Dust Crash**: Fixed a divide-by-zero crash in the native weather code when entering Icirrus City during the December 31 diamond dust event (tolerated on DS hardware, fatal on native targets).
- **Pause Menu Speed Spike**: Fixed game logic and audio briefly running at 2× speed when opening or closing pause-menu screens in one-screen mode.
- **Move Name Highlighting**: Fixed type-colored battle text tinting prefixes inside Pokémon names that match move names (e.g., "Charm" in "Charmander" or "Dig" in "Diglett") instead of only coloring the move itself (thanks @aqali1, #35).
- **Party Status Icons**: Fixed status condition badges in the party screen displaying the wrong ailment (such as showing paralysis as sleep; thanks @aqali1, #39, #41).
- **In-Game Style Option**: Moved the unreachable `STYLE` setting in the in-game Options menu to the `EXTRAS` tab so it can be selected with keyboard/controller and mouse (#37).
- **Flash Diagnostic Logging**: Disabled automatic `eidos_flash_*.png` screenshot and log generation in the `saves` folder unless explicitly enabled (#38).
- **Yes/No Cursor Highlight**: Fixed missing cursor highlight on the post-catch nickname prompt and fixed battle confirmation prompts resetting the highlight to the first option after selecting the second (#45).
- **Nickname Keyboard Dismissal**: Fixed the on-screen keyboard remaining visible over the "transferred to the PC" message after nicknaming a caught Pokémon (#45).
- **Run Restart Prompt**: Fixed a softlock when restarting a run from the Settings > Mods page, and converted the confirmation prompt into tappable `Yes` / `No` buttons for touch screens.

## 0.2.5

### One-Screen Mode & UI
- **Interface Styles**: Added selectable UI themes for one-screen menus and battles under `Settings > Layout > Interface style` (`STYLE` in the in-game Options menu): **Kosmos** (the previous default), **Plasma** (a modern take on Black and White), and **Harmonia** (closest to the original DS interface), each with dedicated screen layouts.
- **Style Chooser & Live Preview**: Prompt to select a UI style on first launch (reopenable anytime from the title menu with `SELECT`). Includes an interactive preview mode that lets you test the X menu, party, Bag, and battles across styles using a built-in in-memory save without touching your actual save file.
- **Held Item Indicators**: Pokémon holding an item or Mail now display the native held-item or envelope icon in the party screen and battle Pokémon list.
- **Animated Pokémon Sprites**: Fixed multi-part animated Pokémon sprites in the Pokédex and other screens so the full rig animation plays properly instead of looping only the first segment or drifting apart.
- **Cut-in Overlays**: Trainer `VS` banners and the other field cut-ins fill the window as the DS frames them (up to 16:9) over the field's own sharp picture; fixed dark side strips on cut-ins that do not darken the screen, thanks @TbangIT (#34).
- **Top Bar Clock**: Fixed the clock's digits shifting each time the colon blinks (#34).

### Engine & Performance
- **Frame Time Reduction**: Reduced CPU frame times by roughly 50% across all renderers while keeping video and audio output bit-for-bit identical (38–66% faster in the overworld and battles on Windows across OpenGL, Direct3D 12, and Vulkan; ~50% faster in Switch emulation).
- **Memory Operations**: Replaced the game's byte-by-byte memory copy and comparison routines with optimized native implementations.
- **3D Rendering Pipeline**: Execute 3D geometry commands directly, avoid full general-purpose polygon sorting each frame, and skip redundant OpenGL state changes.
- **UI Alpha Blending**: Optimized translucent panel and fill rendering to blend whole scanlines without per-pixel division.
- **File Lookup Caching**: Cache virtual filesystem path resolution (base data vs. mod overrides) with a 1-second TTL so newly added mod files are still picked up at runtime without per-frame stat overhead.
- **Audio Mixer**: Skip silent audio channels during mixing.
- **Title Screen**: Reduced `Press Start` frame work by about a quarter in one-screen mode by skipping 2D composition that nothing shows and caching the scaled logo (#34).

### Fixes
- **Fast Forward**: Fixed `Tab` triggering fast forward during `Alt+Tab` and other window-switching shortcuts (#34).
- **Sound Sequences**: A sequence prepared ahead of its start now reports as playing, as on the DS, so the game cannot drop it before it starts (#34).
- **Text Colours**: A message with coloured words keeps the game's default ink for the rest of its text (#34).
- **Elevators**: Fixed elevator floor menus only listing "Cancel" (#33).
- **Bag Crash**: Fixed a crash when using a second Potion from the overworld Bag (#33).
- **Town Map**: Removed the Fly prompt when opening the Town Map as a key item (#33).
- **Dowsing Machine**: Fixed the Dowsing Machine falling back to dual-screen mode in tall aspect ratios (#33).
- **Whiteout Softlock**: Allowed mouse clicks and touch taps (in addition to the `A` button) to advance the post-whiteout message.
- **Nickname Prompt**: Delayed the `YES` / `NO` nickname buttons until the nickname prompt appears, rather than showing them during Pokédex registration.
- **Continue Screen**: Validate files in the `saves` directory before listing them so non-save files no longer appear as save slots.
- **Pokédex Numbering**: Display each Pokémon's actual Pokédex number (starting with Victini at `000`) instead of its 1-based list index.
- **Billboard Scenery**: Fixed camera-facing billboard objects (such as rocks in Pinwheel Forest) sliding toward the center of the screen and popping in/out during camera movement in one-screen mode (#26).
- **Controller Remapping**: Fixed custom face-button bindings being overwritten by automatic `A`/`B` swapping when mapping the classic DS layout (#28).
- **Single-Frame Flash Diagnostics**: When a single-frame visual glitch occurs (#5), automatically log scene composition details and save screenshots of the surrounding frames to `eidos_flash.log` in the `saves` folder.
- **Battle HP Bars**: Keep one-screen HP plates visible throughout battle and animate HP drain smoothly, instead of hiding the plate whenever the underlying DS gauge hides during move animations or switch-ins.
- **Whiteout Screen**: Render the post-battle whiteout sequence centered in one-screen mode instead of showing both DS screens side by side.
- **Launcher Theme**: Preserve the selected theme under `Settings > Appearance > Theme` when switching game profiles, while retaining `Follow the game` as the default (#29).
- **Steam Deck Double Input**: Suppress duplicate keyboard events while a controller is active so Steam Desktop Mode does not advance menus two steps at a time (#30, #32).
- **Name Entry Navigation**: Enabled D-Pad navigation to the bottom `DELETE`, `SPACE`, and `OK` buttons on the naming screen, and move the cursor to `OK` when pressing `START` (#30).
- **Multiplayer Name Prompt**: Allow confirming an empty name with `OK` to exit the Multiplayer name prompt (#30).
- **Bag Pocket Navigation**: Restored `Left` / `Right` D-Pad navigation for switching Bag pockets alongside `L` / `R` (#32).
- **Launcher Controller & Keyboard Support**: Added full gamepad and keyboard navigation to the launcher (`D-Pad`/Arrow keys to navigate, `A`/`Enter` to confirm, `B`/`Escape` to go back, `LB`/`RB` to switch tabs) (#31).

## 0.2.4

eidos now runs on the Switch and Vita, draws with each platform's native renderer, and brings improvements to the Remaster option along with many bug fixes.

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
- **Team Size**: Before a trainer battle and when a Pokémon is sent out, each side's six balls show its team (able, with a status, fainted or empty), as the game shows them; before, they never appeared (#24).
- **Rotation Battles**: The move screen with its Rotate left and Rotate right buttons is one-screen, and all three Pokémon of each side have their HP plates; before, choosing a move switched to the DS screens and only the front Pokémon had a plate (#24).
- **Double Battle Plates**: Your Pokémon's plates keep the field's order; before, they traded places every turn, so an attack on one could seem to take the other's HP (#24).
- **TMs**: The Bag names a TM's move ("TM35 Flamethrower") with its type, power, accuracy and PP, and the question "Teach Flamethrower to a Pokémon?" shows with YES and NO; before, it showed USE and CANCEL without the question (#24).
- **Move Order**: In a Pokémon's summary, dragging a move onto another swaps them, with the mouse or a finger, as on the DS's touch screen (#24).
- **3D Scenes**: The Nimbasa City Ferris wheel, the Royal Unova and the game's other 3D scenes show in the one-screen layout; before, they switched to the DS screens (#24).

### Bug Fixes
- **Crash After 15 Minutes**: Fixed the game stopping with "no fiber left" after about 15 minutes in battle or the field; with the radio on and the relay out of reach, each try to join it kept a thread (#23).
- **French Black**: Fixed the crash when using the Bag (a Potion, switching Pokémon) on the French Black cartridge (#25).
- **Item Card**: The item card no longer stays on screen for a moment after its message box closes.
- **Trainers' School Blackboard**: Closing the blackboard's list of topics with B (Cancel) no longer closes the game (#24).
- **Move Relearner**: Fixed the Move Reminder locking the game: picking a move past the first four kept the cursor pressing down, and B's "Give up?" question never showed, so the screen could not be left (#24).
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
