# Test switches

Environment variables for scripted test runs. They act only on copies of
saves (run with `--profile-dir` on a scratch folder) and never in a normal
game. The code is in `engine/ui/ui.c` unless noted.

## Setting up the game

| Variable | What it does |
|---|---|
| `EIDOS_DEBUG_PARTY="species:level,..."` | Replaces the party's members with fresh Pokemon. `0` keeps a slot. Members past the party's count are added. |
| `EIDOS_DEBUG_LEVEL=N` | The level for members that name none (default 50). |
| `EIDOS_DEBUG_PARTY_COUNT=N` | The party keeps its first N members. Use a one-Pokemon party for whiteout tests: A alone cannot pick the next Pokemon after a faint. |
| `EIDOS_DEBUG_ITEMS="item:count,..."` | Puts items in the bag. |
| `EIDOS_DEBUG_HELD="item,item,..."` | What the party's members hold, in the party's order (0 leaves a member as it is). Set once the field runs, after `EIDOS_DEBUG_PARTY`'s party. Item 137 to 148 are the Mail items. |
| `EIDOS_DEBUG_MOVES="member:move:slot,..."` | Teaches moves. |
| `EIDOS_DEBUG_BATTLE="trainer ID"` | Starts that trainer's battle once the field is quiet, with the trainer's own rule (single, double, triple or rotation: trainer 18 is a double, 615 a rotation). A loss whites the player out, as after a wild battle. |
| `EIDOS_DEBUG_OPEN="KIND ..."` | Opens a screen once the field is quiet: `relearn P` (the move relearner for party member P), `namein` (the game's free-text name entry, the beacon message's), `demo K` (3D demo K, 1 to 18: 1 the Royal Unova, 3 the Nimbasa Ferris wheel), `trade N M`, `clear N`, `box N`, `tvt N`, `union`, `ltrade`, `makeegg P`, `egg P`. |
| `EIDOS_DEBUG_BATTLE="wild SPECIES LEVEL"` | Starts a scripted wild battle. A loss has no script to end it and the game goes back to the title: use a trainer battle to test a whiteout. |
| `EIDOS_DEBUG_WARP="zone x y z dir"` | Warps the player when the field runs. |
| `EIDOS_DEBUG_CUTIN="item N"`, `"seq N"` or `"mon P"` | Starts a field cut-in as the scripts do, after 90 quiet field frames (a third number sets the frames): a key item's (its item id), a sequence by its id (2 and 4 to 10 are the badges), or party member P's field-move cut-in. |

## Driving the game

| Variable | What it does |
|---|---|
| `EIDOS_DEBUG_MOVE="F:walk:down:N,F:face:up,F:tile:X:Z,F:a"` | Steps for the player. A direction is a word (up, down, left, right) or its letter (U, D, L, R). F is the input frame the step waits for; 0 starts it at once. The log gives each step's tile and facing, and "blocked" when the player did not get there. |
| `EIDOS_DEBUG_BATTLE_SCENE=N` | Sets the game's own Battle Scene option before a debug battle (0: the move animations play; 1: off), whatever the save has. The value before is logged. |
| `EIDOS_DEBUG_DEX_OWN=N` | The Pokedex device of the Plasma and Harmonia styles shows N owned (the number, the gauge's ring and the ball's kind), whatever the save has. Display only. |
| `EIDOS_DEBUG_AUTO_PLAYER=1` | The trainer AI chooses for the player's side too (moves, switches, replacements), so a battle plays itself; the screen shows it as always, and in the one-screen layout a move it chose is picked in the menus (`2`: sent without the menus, for the two-screen layout). With an AI mod loaded the mod's choices apply to the side it handles. |
| `EIDOS_SHOT_EVERY=N` | A screenshot every N frames from frame `EIDOS_SHOT_FROM` (default 0) on, into `--screenshot-dir`: the frames of a recording. |
| `EIDOS_UI_SCRIPT=FILE` | A test's steps, one a line, run once a game frame; they wait for screens, not for frame numbers, so a run does not depend on the machine's load. `wait SCENE [N]` (the one-screen interface's scene, a recipe's name such as `field`, `field-menu`, `pokedex`, is up for N frames in a row; `*` is any; a name's spaces are written as underscores; default 20), `frames N`, `key NAMES [HOLD]` (`A`, `UP`, `L+R`, ...), `mash SCENE NAMES [EVERY]` (the keys every EVERY frames, default 40, until the scene is SCENE), `tap X Y [HOLD]`, `mouse X Y` and `click X Y` (the pointer, in window pixels), `cmouse DX Y` and `cclick DX Y` (the same in the interface's own pixels, DX from the canvas's middle, so a place holds at every window size), `shot NAME` (the window to NAME.bmp in `--screenshot-dir`), `rec NAME N` (a picture of each of the next N frames, NAME_0000.bmp on, while the steps after it run; each is logged with its input frame), `theme ID` (the interface style), `log TEXT`, `quit`. A wait of 6000 frames ends the run with status 3 and names the line. |
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
| `EIDOS_SAVE_TEST_AT=FRAME` | The game saves at its first quiet moment after that frame (`demo/eidos_boot.c`). |
| `EIDOS_GL_DUMP=FILE.bmp:N` (or `:N-M`) | Writes the OpenGL presenter's window at the Nth present (or every present from N to M as FILE.N.bmp) (`engine/platform/plat_gl.c`). |
| `EIDOS_GL_STATE=N` | At the Nth present, logs the presenter's GL state (viewport, scissor, framebuffer, the screens' rectangles), how far a full-window clear reaches and which columns the frame lights (`engine/platform/plat_gl.c`). |
| `EIDOS_EVENT_LOG=1` | Logs the running game event's chain whenever it changes, as offsets from `GAMESYSTEM_GetEvent`: add the offset to that function's address in `nm` of the program to name each event. Finds which code runs a scene. |
| `EIDOS_OVERLAY_LOG=1` | Logs each overlay load and unload with its frame (`sdk/overlay.c`). |
| `EIDOS_DEBUG_WHERE=1` | Logs the player's zone and position once the field runs, as an `EIDOS_DEBUG_WARP` value: a test can then start from that spot with any save. |
| `EIDOS_FLASH_WATCH=0` | Turns off the flash watch (on by default, #5): when one frame of the upper screen's picture differs strongly from the frames on both sides while those agree, the three frames' 2D state goes to `eidos_flash.log` in the saves folder and the first six events' pictures to `eidos_flash_<n>_<a,b,c>.png` beside it. Only three frames in a row count, so fast forward logs nothing. |
| `EIDOS_FLASH_TEST=N` | Draws garbage tiles over the upper half of the first picture from frame N, to check the flash watch. |
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
