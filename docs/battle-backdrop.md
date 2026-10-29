# The battle backdrop

In the one-screen layout, with the backdrop setting on Real map, a battle
happens in the real 3D map where it started. The game's own battle scenery
(the two platforms, the field disc and the sky dome) is not drawn. The map
around the player is drawn behind the battle instead, with the battle's own
camera.

## The parts

| File | Job |
|---|---|
| `sdk/battle_view.c` | Wraps the game's scenery draws. At the first battle frame it reads where the battle started (zone, position, facing, season, time) and starts a worker thread. It hands the camera and the scene to the renderer each frame. |
| `engine/world/bmap_data.c` | Builds the scene: the ground chunks, buildings and placed objects around the player, with lit vertex colours and textures from the ROM archives. It also keeps the game's walk grid (which tiles are floor, wall, water or outside the map, and which have wild encounters) and a ground index for heights. |
| `engine/world/bmap_gimmick.c` | Adds the gym puzzle models (doors, curtains and the like) that the field places through its gimmick code. |
| `engine/world/bmap_place.c` | Moves the scene into battle space: the spot under the own Pokemon, the angle, one fixed scale. It can search near a start point for a clear spot. |
| `engine/world/bmap_spots.c` | The spot table: one row for each place a battle can start, and how its battle is placed. |
| `engine/platform/plat_gl3d.c` | Draws the scene behind the game's polygons. |
| `tools/spot-editor/eidos_spots.c` | The spot editor, a program of its own (not built by default, not packaged). |
| `tools/battle_spots.py` | Builds the spot table from the game's data and merges the editor's file back into the source. |

## Placement

A map point M goes to battle space by a rotation about the vertical axis
and one scale (`BMAP_PLACE_K`, 0.18 battle units for each map unit). The
spot goes to the own Pokemon's place, and the angle points the player's
facing at the foe. The ground under the two Pokemon is levelled to the
height the sprites stand on.

Rules that every placement keeps:

- The foe is where the player looks. For a trainer, the angle points from
  the player at the trainer, with no turn. For a wild battle, the search may
  turn at most 45 degrees either way.
- Nothing is removed from the view. The search tests lines from the
  battle's two longest-held cameras (the command menu's and the intro's) to
  the feet, middle and head of each Pokemon; a surface across a line, or
  anything just in front of a camera's lens, gives a spot a high cost. The
  fix for a bad spot is a better spot, not a hidden object.
- In a room or a corridor, the battle line runs down the middle.
- The ground is levelled between the two Pokemon (each a half step off on a
  slope or a step), unless a row puts each on its own ground. A spot where
  the two grounds differ by more than a little over a tile gets a high cost.
- No Pokemon stands on a wall tile or outside the map.

## Moving the foe

The game reads every battle position from slot tables in ov094
(`BTLV_MCSS_CalcSlotPosition`: single, double, triple, rotation and trainer
slots). The sprites, the move animations' targets, the camera's moves to a
Pokemon and the position resets all read them. `slot_shift` in
`sdk/battle_view.c` moves the foe's side (the odd positions) in those tables
by the placement's height and distance, and moves the sprites already
standing by the same change. A row can also level the ground between the two
instead (the foe keeps the game's height) and push the foe further or closer.

## The intro camera

A row can change the far battle cameras apart from the command camera. The
intro's and the send-out's camera starts far behind the player's side and
shows a wide view that a narrow room cannot fill, or a side of the place
that looks wrong. Six settings do this (`BmapIntro`, applied in this
order):

- zoom (column `iz`, above 0): the camera comes closer to what it looks at;
  below 1 it goes further out;
- turn (column `iy`, degrees, -180 to 180): the camera and its target turn
  about the upright line through the battle's centre (positive: to the
  right);
- height (column `ih`, battle units): the camera and its target rise;
- slide (column `is`, battle units): the camera and its target move to the
  camera's right;
- aim (column `ia`, degrees): the camera looks further right from where it
  stands;
- tilt (column `it`, degrees, -85 to 85): the camera looks up.

The battle camera commits its eye and target in `G3D_CAMERA_Reset`, which
`sdk/battle_view.c` wraps (`bmap_camera_intro`); only the battle's own
camera, not the others the game commits there. The game's intro pulls the
camera back in a straight line from the command camera's eye to the
intro's. A camera gets the part of the change that matches how far it is
along that line (its eye projected on the line): none at the command
camera, all of it at the intro's eye. The zoom moves every camera by the
same vector (the intro eye's move toward its target), not toward each
camera's own target: the target travels out with the eye during the pull,
and a move toward it took the eye in front of the command camera and back
out again (a dip at the start of the intro). So the command camera and the
close-ups do not change, and the intro slides into the command camera
along one line.
The camera object keeps the game's own eye; only the view the game draws
with and the backdrop's camera change.

## The command camera

A row can also change the battle camera itself: the command menu's camera
and the close-ups, which move with it. The intro's and the send-out's
camera keep their own settings. Six settings, the same as the intro's (column
`cz` zoom, `cy` turn, `ch` height, `cs` slide, `ct` tilt, `ca` aim; a missing
column or zoom 1 and zeros is no change):

- zoom (above 0): the eye comes closer to what the camera itself looks at;
  below 1 it goes further out;
- turn, height, slide, aim and tilt: as the intro's (`bmap_camera_view`).

A camera gets the part of them that its eye is from the intro's end of the
line the intro pulls back along (all at the command camera, none at the
intro's eye), the opposite of the intro's own part. So the two sets of
settings do not meet, and the intro keeps sliding into the command camera
along one line.
The flat sprites follow the new camera (`sdk/battle_view.c` sizes them by
their distance to the game's own command camera against the camera drawn
with), so a closer camera shows bigger Pokemon.

`BmapScene` has no room for these settings (`engine/world/bmap.h`), so
`bmap_place_spec` keeps what it was given for the last eight scenes it
placed (`bmap_place_view`).

## The trainers' sprites

The game puts its trainers' sprites in the same slot tables as the Pokemon
(positions 8 to 13: even the player's side, odd the foe's). A row can move
the foe trainer and the player's own trainer (columns `tfx tfy tfz` for the
foe's, `tux tuy tuz` for yours): right, up and back, in battle units.
`slot_shift` in `sdk/battle_view.c` adds them to the trainers' slots with
the foe's height and distance, and to the trainer sprites already standing.
The scripts that walk a trainer in or out of the picture move it from the
changed slot.

## Views

A row can have several views, each with its own spot, angle, cameras and
trainer offsets. A view is a row of the table with the same id and a view
number (column `vw`: missing or 0 for the first, 1 and on for the others;
`tools/battle_spots.py` keys them `id#N`, and the editor writes a row's
views after it). At a battle's start `bmap_spots_pick_view` takes the view
whose spot is nearest to where the player stands (a fixed view's spot, a safe
area's middle, else the row's usual player place); two spots less than a
tile apart are a tie, and the view whose angle is nearest the player's
facing wins it. The first view's column `vm` set to 1 puts the angle first.
The `ok` mark of the first view counts for the row; the others' is not used.

## The spot table

`engine/world/battle_spots.tsv` has one row for each trainer and each patch
of wild encounter tiles (640 rows for Pokemon Black). `tools/battle_spots.py
gen` builds it:

- Trainers: every NPC with the trainer event type, with its sight range, and
  the gym leaders. The battle starts on a tile of the sight line or beside
  the trainer.
- Wild areas: the tiles with the encounter flag (bit 18 of the attribute
  word: grass, cave floor, sand, water), in zones with an encounter table,
  grouped into patches. Land and water are separate patches.
- For each row, a walk from the zone's entrances (its warps and its edges)
  gives the tile the player most likely reaches first, and the way they
  face there. The editor shows the battle from that place first.

Each row has a mode:

| Mode | Placement |
|---|---|
| auto | The search, from where the player stands. |
| fixed | One spot and one angle, whatever tile the player stands on. |
| zone | The search, kept inside a safe rectangle. The angle is the player's facing or a fixed one. |

At a battle's start, `bmap_spots_find` picks the row: for a trainer battle,
the trainer whose sight line or neighbouring tiles hold the player; for a
wild battle, the area whose box holds the player. A battle with no row uses
auto.

The table is built into the program (`battle_spots_tsv.inc`). A file
`battle_spots.tsv` beside the settings file is read over it at start; the
spot editor writes that file.

## The spot editor

`tools/spot-editor/eidos_spots.c` is a program of its own: no game runs. It
is built only on purpose (`ninja eidos_spots.exe`) and is never packaged.
It reads the player's extracted data, opens any row of the table and shows
its battle placed the way the game would place it.

The left column:

- the place: its name and number, Prev and Next, Mark checked, a count of
  the rows checked;
- the place list: All, Trainers, Wild, To check and Changed; type to search
  (Esc clears); a green dot is a row checked, a blue one a row changed;
- the map from above: the walk grid (floor, walls, water, encounter tiles),
  the trainer and its sight line, the wild area, the safe area, the player
  and their facing, the two Pokemon, the two cameras and the camera space;
- the controls: Auto, Fixed spot or Safe area; the angle; the foe levelled
  or on its own ground and its distance; Back to auto, Default (the
  built-in row), Undo, Reverse (the same battle line seen from its other
  end); a warning when a Pokemon stands on a wall or outside the map or the
  ground step is too big.

The right column:

- the battle through the command camera and the intro camera side by side,
  with a Pokemon sprite on each side (Single or Double, any species); the
  the intro settings in the left column (Ctrl fine steps, Shift coarse
  ones, a click on a number types it in); a drag on the intro view turns and
  raises it, a right drag slides and tilts it, the wheel zooms; Play runs a recorded battle camera in the
  left view (the intro, idle, moves), with the Pokemon where the recording
  has them;
- under the two views, the settings of the place: Views (a button for each
  view, Add view copies the one on show and makes it a fixed spot, Delete,
  By spot or By angle, and which view the game takes for the white player
  dot), the command camera (zoom, turn, height, slide, tilt, aim; a drag on
  the left view turns and raises it, a right drag slides and tilts it, the
  wheel zooms, as on the intro view) and the two trainers' places (right, up,
  back). Show trainers puts the trainers' pictures where the game stands them, each in
  place of its side's Pokemon; the trainer intro (Play) shows the foe
  trainer where the game's script puts it. On the map, a click on another
  view's ring picks that view, and Ctrl+drag moves the player dot in a fixed
  view;
- the overworld: the zone's map with its NPCs (`engine/world/bmap_npc.c`)
  and the player, from the zone's field camera; drag to move, right drag to
  turn, the wheel to zoom, Field camera to go back; the season and the time
  of day.

On the map: drag moves the fixed spot or the player, right drag turns
toward the pointer (Shift: 45-degree steps), Shift+drag draws the safe
area, the wheel zooms, a middle drag pans. Keys: PgDn and PgUp (or Tab)
step through the list, Enter marks a row checked, the arrows move the spot
or the player (Shift: a tile), Ctrl+Z undoes.

Every change is written at once to `battle_spots.tsv` beside the game's
settings file, which the game reads over its built-in table.

### Camera recordings

Play needs recordings of the game's battle camera, made from the player's
own game (they are derived from it, so they are not in the repository):
run eidos with `EIDOS_CAMERA_RECORD=<file>` and fight; every battle frame
adds a line with the game's eye, target and Pokemon places
(`sdk/battle_view.c`, `record_camera`). Cut the parts to play into
`<settings folder>/camera/<name>.cam` and list the names, one a line, in
`<settings folder>/camera/clips.txt`.

To take the edited rows into the source:

    python tools/battle_spots.py merge <path of the edited battle_spots.tsv>

Then rebuild.

## Test switches

These are for tests only and come out before release:
`EIDOS_DEBUG_BMAP_AT`, `EIDOS_DEBUG_SPOT`, `EIDOS_DEBUG_BMAP_K`,
`BMAP_TOPVIEW`. The spot editor's `--shot` and `--script` options are for
its own tests.

## Sprite sizes

The battle draws its Pokemon and trainers as flat sprites most of the time
(MCSS ortho billboards): a sprite's size is its scale, and the camera never
changes it. The scripts fake a camera that goes back or comes in by
tweening the sprites' size (McssWork shadowScale) along with the camera,
over the same frames: the wild intro takes the foe to 0.5 and sends the
player's Pokemon out at 0.5; the camera views 14 and 19 take the two sides
to 0.65 and 0.725. Those are chosen sizes, not distances (the foe's would
be 0.62 for the intro, the player's 0.42), so against the map, which is
real 3D, the Pokemon grow and shrink as the camera moves. Some move
animations switch to true 3D sprites, where the size does follow the
distance.

`sdk/battle_view.c` wraps `MCSS_Draw` and draws each flat sprite at the
size its distance gives: its size over its size at rest (the fake), times
its depth before the game's command camera (where the sizes are right) over
its depth before the camera drawn with (the foe's position taken back to
the game's own for the first). The size at rest follows the size while the
game's camera moves and for a few frames after (a tween's last step comes
late); with the camera still, a change is the script's own (a withdraw, a
faint, the send-out's pop) and stays. A sprite that comes from nothing
grows toward the size the scripts give at that camera: 1 at the command
camera, 0.5 all the way back (learnt from the scripts while they run). The
scales are put back after the draw.

## In front of the Pokemon

The game draws its sprites without the map's depth, so a second pass draws
again the parts of the map that stand between the camera and a Pokemon,
over that Pokemon's sprite box (`plat_gl3d.c`, the front pass). A part
that lies behind a nearer Pokemon is not drawn again over it, and the
see-through parts (water) are drawn again with the rest, so the ground under
them does not show without them.
