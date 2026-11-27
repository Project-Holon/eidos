# Test switches

Environment variables for scripted test runs. They act only on copies of
saves (run with `--profile-dir` on a scratch folder) and never in a normal
game. The code is in `engine/ui/ui.c` unless noted.

## Setting up the game

| Variable | What it does |
|---|---|
| `EIDOS_DEBUG_PARTY="species:level,..."` | Replaces the party's members with fresh Pokemon. `0` keeps a slot. Members past the party's count are added. `species.form:level` gives a form. |
| `EIDOS_DEBUG_LEVEL=N` | The level for members that name none (default 50). |
| `EIDOS_DEBUG_MOVES="move,move,move,move"` | The first party member's moves, slot by slot, each with its full PP. `0` keeps a slot's move. Set with `EIDOS_DEBUG_PARTY`'s party. The form `"member:move:slot,..."` (any `:`) teaches moves to any member instead. |
| `EIDOS_DEBUG_PARTY_COUNT=N` | The party keeps its first N members. Use a one-Pokemon party for whiteout tests: A alone cannot pick the next Pokemon after a faint. |
| `EIDOS_DEBUG_ITEMS="item:count,..."` | Puts items in the bag. |
| `EIDOS_DEBUG_PROPS=N` | The first N Musical props (of 100) are owned. |
| `EIDOS_DEBUG_DRESSUP_LOG=1` | The one-screen Musical dress-up logs the saved outfit when it opens (8 records of 6 bytes: place, unused, prop id, turn) and what is worn and where (`prop@place`) whenever it changes. It also logs where the screen's parts stand, in canvas pixels, each time that changes (`layout:` lines: the stage, the tiles, the body part's label, the line's box, DONE, the ring's places, and the body's places while one is chosen), so a scripted pointer can click them in any style (`engine/ui/recipe_musical.c`). |
| `EIDOS_DEBUG_DRESSUP_STYLUS=N` | The first N times the dress-up opens it is left to the game (the two screens, the stylus), to compare what the stylus and the one-screen list save. |
| `EIDOS_DEBUG_DRESSUP_DEMO=1` | The dress-up starts the game's first-visit demo once, on a save that has seen it (`sdk/ui_dressup.c`). |
| `EIDOS_DEBUG_HELD="item,item,..."` | What the party's members hold, in the party's order (0 leaves a member as it is). Set once the field runs, after `EIDOS_DEBUG_PARTY`'s party. Item 137 to 148 are the Mail items. || `EIDOS_DEBUG_BATTLE="trainer ID"` | Starts that trainer's battle once the field is quiet, with the trainer's own rule (single, double, triple or rotation: trainer 18 is a double, 615 a rotation). A loss whites the player out, as after a wild battle. |
| `EIDOS_DEBUG_OPEN="KIND ..."` | Opens a screen once the field is quiet: `relearn P` (the move relearner for party member P), `namein` (the game's free-text name entry, the beacon message's), `demo K` (3D demo K, 1 to 18: 1 the Royal Unova, 3 the Nimbasa Ferris wheel), `trade N M`, `clear N`, `box N`, `tvt N`, `union`, `ltrade`, `makeegg P`, `egg P`, `dowsing` (the Dowsing Machine on the lower screen, as using the item does). |
| `EIDOS_DEBUG_BATTLE="wild SPECIES LEVEL"` | Starts a scripted wild battle. A loss has no script to end it and the game goes back to the title: use a trainer battle to test a whiteout. |
| `EIDOS_DEBUG_BATTLE_AGAIN=N` | With `EIDOS_DEBUG_BATTLE`: the battle starts N more times, each once the field is quiet again after the one before (a second battle in one session, in the same place). |
| `EIDOS_DEBUG_WARP="zone x y z dir"` | Warps the player when the field runs. |
| `EIDOS_DEBUG_WARP_SEASON=1` | With `EIDOS_DEBUG_WARP`: the warp first sets the save's season from the clock, as the game's own map changes do. With `EIDOS_RTC` a test then sees the place in that date's season and calendar weather (the calendar applies only while the two seasons agree). |
| `EIDOS_DEBUG_WEATHER=<id>` | A real-map battle shows this field weather (the game's id: 1 snow, 2 rain, 3 sandstorm, 4 blizzard, 5 downpour, 6 thunderstorm, 7 storm, 8 diamond dust, 9 fog, 12 the desert's sandstorm) in place of the game's stored one. |
| `BMAP_WEATHER_FOG=0` | A real-map battle's map keeps its clear-weather light and fog under a weather (the weather's own light and fog are on otherwise: the fog is the thicker of the field camera's and the battle camera's measure). `BMAP_WEATHER_SPRITE_FOG=1` lays the fog over the Pokemon's and trainers' sprites as the setting Extras > Weather haze does. |
| `EIDOS_DEBUG_AVATAR="<look> <dx> <dz>[;...]"` | Other players' figures beside the player once the field has run 90 quiet frames, each in a look (10 hex digits, as a session sends them: body, hair, accent, trousers, skin; `0006090408` is Hilbert, blonde, blue, white, dark brown) and `dx`, `dz` cells away. Code: `sdk/mp.c`. |
| `EIDOS_MOD_MODE=<id>` | The first new game of the run is in a mod's mode (a `mode` line of its settings file, docs/mod-services.md), as if the Multiplayer screen's Start had chosen it: `mode_get` answers the id in game.new. The mod's options are the player's saved choices. Code: `sdk/mod_menu.c`. |
| `EIDOS_MOD_CODE=<text>` | With `EIDOS_MOD_MODE`: what the Join box hands a mode marked `joins`, answered by `mode_code` in game.new: a world's code, or `ap <host:port> <slot> <password>` (`-` for none) for an Archipelago room. Code: `sdk/mod_menu.c`. |
| `EIDOS_DEBUG_POPUP="title\|text\|icon;..."` | Queues those mod pop-ups at the first frame, as if a mod had called `popup` (icon 0 none, 1 trophy, 2 info, 3 warning): their look without a mod. Code: `engine/ui/popup.c`. |
| `EIDOS_DEBUG_TRACKER=<file>` | The field's Tracker shows that file's text, as if a mod had set it with `tracker_set` (docs/mod-services.md, "The Tracker"): its look without a mod. Code: `engine/ui/tracker.c`. |
| `EIDOS_DEBUG_MOVE_KNOWN=<slot>` | That party slot knows every field move the game asks about (`move.known`, docs/mod-events.md): Cut, Strength, Surf and Waterfall work with no member that learned them, without a mod. An egg in that slot still does not. Code: `sdk/mod_event_v6.c`. |
| `EIDOS_UI_FIELD_TRACKER=<frame>` | The top bar's Tracker button is clicked at that frame, when a mod (or `EIDOS_DEBUG_TRACKER`) gives the field a tracker. Code: `engine/ui/recipe_field.c`. |
| `EIDOS_DEBUG_CUTIN="item N"`, `"seq N"` or `"mon P"` | Starts a field cut-in as the scripts do, after 90 quiet field frames (a third number sets the frames): a key item's (its item id), a sequence by its id (2 and 4 to 10 are the badges), or party member P's field-move cut-in. |

## Driving the game

| Variable | What it does |
|---|---|
| `EIDOS_DEBUG_MOVE="F:walk:down:N,F:face:up,F:tile:X:Z,F:a"` | Steps for the player. A direction is a word (up, down, left, right) or its letter (U, D, L, R). F is the input frame the step waits for; 0 starts it at once. The log gives each step's tile and facing, and "blocked" when the player did not get there. |
| `EIDOS_DEBUG_BATTLE_SCENE=N` | Sets the game's own Battle Scene option before a debug battle (0: the move animations play; 1: off), whatever the save has. The value before is logged. |
| `EIDOS_DEBUG_DEX_STATE=0\|1\|2` | The Pokedex screens draw every species as not seen (0), seen (1: its facts as question marks) or caught (2), whatever the save has. Display only (`engine/ui/recipe_dex.c`). |
| `EIDOS_DEBUG_DEX_NATIONAL=1` | Once the field runs, the save has the National Pokedex and shows it (its view mode set to National). Changes the save. |
| `EIDOS_DEBUG_DEX_OWN=N` | The Pokedex device of the Plasma and Harmonia styles shows N owned (the number, the gauge's ring and the ball's kind), whatever the save has. Display only. |
| `EIDOS_DEBUG_AUTO_PLAYER=1` | The trainer AI chooses for the player's side too (moves, switches, replacements), so a battle plays itself; the screen shows it as always, and in the one-screen layout a move it chose is picked in the menus (`2`: sent without the menus, for the two-screen layout). With an AI mod loaded the mod's choices apply to the side it handles. |
| `EIDOS_SHOT_EVERY=N` | A screenshot every N frames from frame `EIDOS_SHOT_FROM` (default 0) on, into `--screenshot-dir`: the frames of a recording. |
| `EIDOS_UI_SCRIPT=FILE` | A test's steps, one a line, run once a game frame; they wait for screens, not for frame numbers, so a run does not depend on the machine's load. `wait SCENE [N]` (the one-screen interface's scene, a recipe's name such as `field`, `field-menu`, `pokedex`, is up for N frames in a row; `*` is any; `battle_map`: the battle's real map built, placed and shown, so a capture never shows the battle before its map; a name's spaces are written as underscores; default 20), `frames N`, `key NAMES [HOLD]` (`A`, `UP`, `L+R`, ...), `mash SCENE NAMES [EVERY]` (the keys every EVERY frames, default 40, until the scene is SCENE), `type TEXT` (one word, typed as if from the PC's keyboard) or `type $NAME` (the text of the environment variable NAME, so a test password is never in the script file), `tap X Y [HOLD]`, `drag X0 Y0 X1 Y1` (the stylus down on the lower screen, moved there and lifted), `mouse X Y` and `click X Y` (the pointer, in window pixels), `cmouse DX Y` and `cclick DX Y` (the same in the interface's own pixels, DX from the canvas's middle, so a place holds at every window size), `cdown DX Y`, `cmove DX Y` and `cup DX Y` (a held press, a move while held and its release: a drag), `shot NAME` (the window to NAME.bmp in `--screenshot-dir`), `rec NAME N` (a picture of each of the next N frames, NAME_0000.bmp on, while the steps after it run; each is logged with its input frame), `theme ID` (the interface style), `log TEXT`, `quit`. A wait of 6000 frames ends the run with status 3 and names the line. |
| `EIDOS_DEBUG_A_UNTIL=CONDITION` | Presses A every 20 frames until the condition opens (from frame `EIDOS_DEBUG_A_FROM`, default 0). It drives battles and texts without a fixed number of presses. |
| `EIDOS_DEBUG_MOVE_TRACE=1` | Logs the move steps' state every 500 frames. |

## Conditions

`EIDOS_DEBUG_WHEN` holds the party and battle switches, `EIDOS_DEBUG_MOVE_WHEN`
holds the move steps (their frames then count from when it opens), and
`EIDOS_DEBUG_A_UNTIL` stops the A presses. A condition is one of:

| Condition | Opens when |
|---|---|
| `field:N` | N frames after the field first runs. `field:200` is past the fade-in, when the player can move. |
| `party:N` | The party has N members or more. `party:1`: the starter came. |
| `whiteout` or `whiteout:N` | A whiteout has happened, or N frames after it. |
| `whiteout-done` | The whiteout's own script (the blackout, the heal) has ended and the field runs with no event. Pressing A after this would talk to the nurse again. |

## Logs and saves

| Variable | What it does |
|---|---|
| `EIDOS_MOD_MESSAGE_LOG=1` | Each mod message goes to the log too (`sdk/mod_message.c`). |
| `EIDOS_VFS_MEM_DUMP=DIR` | Each archive member a mod sets in memory (`vfs_member_set`) is also written to `DIR/<archive>/<index>`, a mod folder's member layout, so the same bytes can be run as plain member files without the mod's code (`sdk/mod_vfs.c`). |
| `EIDOS_SAVE_TEST_AT=FRAME` | The game saves at its first quiet moment after that frame (`demo/eidos_boot.c`). |
| `EIDOS_TIDY_STOP_AFTER=N` | The saves folder's tidy step (`sdk/save_tidy.h`) stops after N moves as a crash would, leaving its plan (`tidy.txt`) for the next start. For the test that the next start goes on and loses nothing (`tests/test_save_tidy.c`). |
| `EIDOS_GL_DUMP=FILE.bmp:N` (or `:N-M`) | Writes the OpenGL presenter's window at the Nth present (or every present from N to M as FILE.N.bmp) (`engine/platform/plat_gl.c`). |
| `EIDOS_GL_STATE=N` | At the Nth present, logs the presenter's GL state (viewport, scissor, framebuffer, the screens' rectangles), how far a full-window clear reaches and which columns the frame lights (`engine/platform/plat_gl.c`). |
| `EIDOS_EVENT_LOG=1` | Logs the running game event's chain whenever it changes, as offsets from `GAMESYSTEM_GetEvent`: add the offset to that function's address in `nm` of the program to name each event. Finds which code runs a scene. |
| `EIDOS_OVERLAY_LOG=1` | Logs each overlay load and unload with its frame (`sdk/overlay.c`). |
| `EIDOS_LIVE_LOG=1` | Logs the field's moving objects (id, object code, place) once in each zone, and each 3D object the field draws that the battle map takes over (`engine/ui/ui.c`, `sdk/battle_view.c`). |
| `EIDOS_NO_REMADE=1` | The battle map keeps every model as the map has it: no remade models (`docs/battle-backdrop.md`). For comparing. |
| `EIDOS_NO_CLOUDS=1` | Outdoor battles have no cloud layer (`docs/battle-backdrop.md`). For comparing. |
| `BMAP_MODEL_LOG=1` (or `all`) | Logs each building model the battle map places near the player (`all`: the far ones too), with its place and whether it is remade. |
| `BMAP_PLACE_LOG=1` | Logs the battle map's placement: timings of its stages, the live objects, the bodies stood on the floor. |
| `BMAP_REMADE_DUMP=DIR` | Writes what each builder of a remade model made as `DIR/<model>.bmc`, for comparing two builds triangle by triangle. |
| `BMAP_DUMP=FILE`, `BMAP_MODELS_DUMP=DIR` | Research: write the whole battle scene, or every building model standing alone, for tools outside the game. |
| `BMAP_GRASS_FILE=FILE` | Real-map battles build their grass from a hand-made layers file, `engine/world/grass/tall_grass.txt`, built into the program (`tools/grass_inc.py` writes `tall_grass_txt.inc` from it): every leaf is drawn pixel by pixel in the grass picture's colours and placed by hand (format at the top of `engine/world/bmap_grass.c`). It holds four kinds: the normal tall grass (a bush a tile), the dark grass (the same bush with one more layer), White Forest's grass (the normal bush) and the long grass in green and teal (tussocks of tall blades in place of the field's lid, which buries the Pokemon; its `part` statement presses the blades down where a Pokemon stands). `BMAP_GRASS_FILE=FILE` reads another file in its place, to try a change without a build. `BMAP_GRASS_FILE=0`: no hand-made grass (the old tufts and the long grass's lid). `BMAP_GRASS_LOG=1` logs what it builds. |
| `BMAP_GRASS_LEAFRULE=0` | Turns off the leaf rule (`engine/world/bmap_leaf.c`), which is on: the hand-made grass covers a Pokemon leaf by leaf and ring by ring, not by the flat cut at the sprite's depth. A leaf is always whole. While the Pokemon stands outside a ring's reach, the whole ring goes by its bush's heart (nearer the eye than the Pokemon: over it). Inside the reach, a leaf is over the Pokemon when its tip is nearer the eye than the Pokemon's feet. Both renderers keep it in depth: a leaf over a sprite is laid just before the sprite's depth, a leaf under it just behind, and back at its own depth after the sprites so their shadows stay under the leaves. While a sprite fades in or out the game draws it after the shadows, and the flat cut holds for those frames. The spot editor's views have their own pass for it. |
| `BMAP_GRASS_FEET=0` | Turns off the bush at the feet, which is on: the hand-made bush of the tile a Pokemon stands on grows at the Pokemon's place, not at the tile's middle (by the battle's rule: a single's two places, a double's four, a triple's or a rotation's six; with two Pokemon on one tile its bush stays at the tile's middle). Not for a grass that is pressed down (`part`). |
| `BMAP_FRONT_TINT=1` | Spot editor (OpenGL): every pixel of the map laid again over a sprite is painted red, to see what covers a Pokemon. |
| `BMAP_FRONT_SPRITE_ONLY=0` | Spot editor: the map laid over a sprite shows over the sprite's whole box again. By default it shows only on the sprite's own pixels, as the game lays it. |
| `EIDOS_HIDE_SPRITES=1` | Native renderer: the battle's sprites are not drawn (the map and the shadows alone). For comparing what covers a sprite. |
| `EIDOS_ANIM_HOLD=1` | Native renderer: the battle map's texture animations and bobbing objects stand still, so two captures of one scene are equal pixel for pixel. |
| `BMAP_DEBUG_BACK=1` | Research: paints the never-drawn side of every one-sided draw magenta. |
| `BMAP_NO_SHELL=1` | Real-map battles leave Nimbasa City's sports floors without their walls and roof (`stadium_shell`, `engine/world/bmap_data.c`). `BMAP_SHELL_LOG=1` lists the building's pictures by name and size. |
| `EIDOS_BMAP_NO_AHEAD=1` | A real-map battle's map is not started when the encounter starts (`ahead_field`, `sdk/battle_view.c`): it is built at the battle's first frame, and the game stands still for it. With `BMAP_PLACE_LOG=1` each battle says which way its map went, and which input changed when a map started ahead was not taken. |
| `EIDOS_BMAP_NO_WARM=1` | An area's remade models are not built ahead when the player comes into it (`warm_field`, `sdk/battle_view.c`). |
| `EIDOS_NO_BESIDE=1` | 64-bit build: no job thread on the host (`host/par.c`). The battle map's work runs on the game's own thread, and nothing is built ahead. |
| `EIDOS_SLOW_LOG=1` | Native renderer: logs each step of a frame's render that took over 2 ms (`slow: ...`): the battle map's upload and its parts, the 3D pass, the submit, the 2D screens. |
| `EIDOS_BD_LOG=1` | Native renderer: every 120th frame, what the battle map's first pass sent (`backdrop log: ...`): the draws sent and left out, the camera, the fog; or why the map is not drawn. A map that cannot be sent to the GPU (no memory) always says so (`eidos: backdrop: ...`). |
| `BMAP_PLANTS_ONE_SIDE=1` | Real-map battles draw Route 1's seasonal plant (`r01_sp_01`, `r01_su_01`, `r01_au_01`, `r01_wi_01`) from its south side only, as the field does (`plants_both_sides`, `engine/world/bmap_place.c`). |
| `BMAP_NO_COAST=1` | Real-map battles leave the far coast as the map has it: no sea in the grid cells that hold nothing, no cliffs under far ground whose edge hangs open (`close_coast`, `engine/world/bmap_data.c`). |
| `BMAP_CAM_YAW=DEG`, `BMAP_FORCE_FACING`, `BMAP_FREE_ANGLE` | Research: turn the battle cameras or fix the placement's facing. |
| `BMAP_FIX_BACKS=1`, `BMAP_JOIN=...`, `BMAP_JOIN_MODE=...` | Research, not for use: tries that were shelved (`docs/research/battle-real-map-2026-11.md`). |
| `EIDOS_DEBUG_WHERE=1` | Logs the player's zone and position once the field runs, as an `EIDOS_DEBUG_WARP` value: a test can then start from that spot with any save. |
| `EIDOS_FLASH_WATCH=1` | Turns on the flash watch (off by default since #38; made for #5): when one frame of the upper screen's picture differs strongly from the frames on both sides while those agree, the three frames' 2D state goes to `eidos_flash.log` in the saves folder and the first six events' pictures to `eidos_flash_<n>_<a,b,c>.png` beside it. Only three frames in a row count, so fast forward logs nothing. |
| `EIDOS_FLASH_TEST=N` | Draws garbage tiles over the upper half of the first picture from frame N, to check the flash watch. |
| `EIDOS_UI_FADE_LOG=1` | One-screen interface: while the game's screen is dark, or a picture is held or leaving, logs each frame's scene, the game's darkness (0 to 16), and whether the interface holds its picture or cross-fades (`eidos: ui fade: ...`, `engine/ui/ui.c`). A hold that comes and goes every few frames is a stepped fade read as many fades. |
| `EIDOS_UI_KEY_LOG=1` | Logs the input frame at which each press of a UI script's `key` and `mash` steps goes down (`eidos: ui key: ...`), for a recording's overlay. |
| `EIDOS_UI_SKIP=name,name` | One-screen interface: leaves those recipes out, so their screens show as the game's own two screens. For checking what a recipe changes. |
| `EIDOS_CATCH_LOG=1` | One-screen interface, the Pokedex registration after a catch: logs the game's state, step and fade level as they change and each A or B (`eidos: catch: ...`, `engine/ui/recipe_menus.c`). A species the save owns starts at state 1: no registration. |
| `EIDOS_MP_UI_LOG=1` | Logs the Play Together screens' steps (`eidos: mp ui: ...`, `engine/platform/settings_ui.c`) and when a game mode starts with a new game, is asked for by a mod, and ends (`eidos: mode: ...`, `sdk/mod_menu.c`). |
| `BMAP_CLEAR_LOG=1` | Real-map battles: logs each frame what the camera's clearance goes by: the sprites reported, the nearest one's distance, the camera's place and the places kept (`clear: ...`, `sdk/battle_view.c`). |
| `EIDOS_BATTLE_SPRITE_LOG=1` | Real-map battles: logs each battle sprite's place and size as the map's pass sees it (`sdk/battle_view.c`). |
| `BMAP_LEAF_LOG=1` | Real-map battles: logs each frame how many hand-made grass leaves the leaf rule puts under a sprite (`engine/world/bmap_leaf.c`). A count that changes while nothing moves but the camera shows leaves changing sides. |
| `RM_SETTLE_LOG=1` | Remade rooms: logs what `settle` moved down, and an `apart` line for each piece it moved away from a piece it stood on (`engine/world/remade/mesh.c`). |
| `EIDOS_AUDIO_WAV=FILE` | With `EIDOS_NO_AUDIO=1`, writes the game's sound to a WAV (16-bit stereo, 32768 Hz). Each frame writes its own share, so the sound lines up with `--screenshot` frames at any speed: frame N starts at N x 560190 / 33513982 s. |

## Pacing

| Variable | What it does |
|---|---|
| `EIDOS_REFRESH_HZ=RATE` | The pacer acts as if the display ran at RATE Hz. `60` turns VSync on for a test on a display at another rate (the frames then wait for that display's own vblank). |
| `EIDOS_HITCH_MS=N` | A sampler for slow frames (Windows, the 32-bit build). A thread looks at every other thread about once a millisecond; when the game's own threads took N ms or more between two frames, that frame's samples are printed (`eidos: hitch frame ...`, then `eidos: hs THREAD EIP CALLER...`). A link map names the addresses. |
| `EIDOS_NOWAIT=1` | No pacing at all: every frame runs as soon as the last one ends. |

## Two games in one session

`EIDOS_MP_TEST=host:FILE` makes a game host at frame `EIDOS_MP_TEST_AT`
(default 300), write its code to FILE and start when the lobby is ready.
`EIDOS_MP_TEST=join:FILE` makes another game join with that code. See
`docs/multiplayer.md`, section Tests.

A joiner's save comes from the host. To give each player a prepared save,
put these files in the host's `saves/multiplayer` folder before the run:

- `host.sav`: the host's game.
- `host_id.txt`: a fixed 32-digit hexadecimal id.
- `FINGERPRINT.sav`: the joiner's game.

Then put `HOSTID SECRET` in the joiner's `saves/multiplayer/secrets.txt`.
FINGERPRINT is the host's name for that secret (`fingerprint()` in
`sdk/mp.c`). The host's `shared.txt` holds the mods' shared table (a Soul
Link's starter pair) and can be copied with the saves.

A multiplayer game does not fast forward, so a two-game test plays at the
game's own speed. Make a long scene (the opening, the starter) once, save
both games, and start each test from those saves.

## A standalone game and its two versions

| Switch | What it does |
|---|---|
| `EIDOS_WINDOW_HIDDEN=1` | The game's window never shows (a test run). It also keeps the setup window from opening: a run that finds no game data says what is missing and stops, as `--no-window` does. |
| `EIDOS_USER_DIR=<folder>` | The user folder a standalone game uses: `profiles/Black`, `profiles/White` and a plain start's old `saves/` and `mods/` are looked for there, not in the player's own folder. With `EIDOS_HOME` (the game data and `config.txt`) a test touches nothing of the player's. |
| `EIDOS_NO_BUILT_DATA=1` | A development build leaves out the data root compiled into it, as a release build has none: the install's own folders are used. |
| `EIDOS_GAME_SWITCH_AT=<frame>[,<frame>...]` | The other version of the game at those frames (the frame count starts again at each switch). |
| `EIDOS_GAME_ADD_ROM=<file>` | The file the title menu's add-a-version window takes, with no file dialog. |

`--import-old-saves` copies a plain start's old saves into their game's
profile and ends (the launcher's call). The test programs are
`tests/test_profiles.c` (the rules, on made-up folders) and, outside the
repository's tests, whole starts of the game on a test home.

## The mods index

The game's own mod manager (a standalone game's, `docs/distribution.md`)
reads the mods index and installs from it (`sdk/mod_catalog.h`).

| Switch | What it does |
|---|---|
| `EIDOS_MODCAT_TEST=list` | Reads the index at frame `EIDOS_MODCAT_TEST_AT` (default 300), with no screen, and prints the mods that fit this game: one line each, then how long it took and the longest frame meanwhile (`eidos_boot: mod catalog test: ...`). |
| `EIDOS_MODCAT_TEST=get:ID` | The same, then installs the mod ID (`katalogos/nuzlocke`, `owner/name` in lower case), or updates it, and prints the mod menu's rows and how many code mods are loaded. |
| `EIDOS_MODCAT_TEST=remove:ID` | Removes the installed mod ID and prints how many code mods were loaded before and after. |
| `EIDOS_MODCAT_TEST=idle:MS` | Asks for nothing and prints the longest frame of MS milliseconds: the number to compare the others' longest frames with. |
| `EIDOS_MOD_INDEX=URL` | The index to read in place of katalogos's (https only). |
| `EIDOS_MOD_RELEASES=URL` | The server the release files are asked of, in place of `https://github.com`: `URL/OWNER/REPO/releases/download/TAG/FILE`. |
| `EIDOS_TLS_CA=FILE` | One more certificate root (a PEM file), for a test server with a certificate of its own. It also counts for a TLS relay. |

A test of this part needs a server that answers as GitHub does (the index, a
redirect to each release file) and can answer wrongly on purpose: a package
with another hash, a longer one, a connection that breaks off or goes
silent, a 404, a 503. The tests of `sdk/net_https.c` and of the index's
rules need no network (`test_https`, `test_mod_catalog`).

## The style chooser

The title menu asks for the interface style the first time it is up with no
style chosen (`ui_theme` is not in `settings.ini`).  `EIDOS_UI_THEME` skips
the question.  SELECT on the title menu opens the chooser at any time.

The chooser's preview plays on a save built into the program
(`sdk/preview_save.c`, written by `tools/preview_save.py`): it is unpacked
into memory, given to the backup chip from there, and nothing of the
preview is written to any file.  It is no save slot.  The cards' pictures
are the player's own: the game takes one of its battle screen in each style
while the preview runs and keeps them in
`<save folder>/style-cards/<style>.bmp`; until then a card shows its
style's sign.  A test may name a folder with `EIDOS_DEMO_DIR`: its
`demo.sav` is played in the built-in save's place (held in memory), and its
`kosmos.bmp`, `plasma.bmp`, `harmonia.bmp` (uncompressed 24 or 32 bit) are
card pictures.
The preview (Y) is the real game on that save, started with no title menu
and none of its questions, on four screens in turn (Y): the X menu, the
party, the Bag, a battle's command menu.  SELECT is the next style, START
goes back to the chooser (the game starts again at the title menu, not at
the logos).
