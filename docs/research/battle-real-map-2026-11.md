# Battles on the real map: research log, November 2026

This file records one block of research on the one-screen battle that is
drawn on the real 3D map (`docs/battle-backdrop.md`). It says what the goal
was, what was tried, what was found, what was decided and what is still
open. The work was first done on a local research branch with Python scripts
outside the repository. On 2026-11-12 it was brought into this tree, and the
remade models are now built by C code in the game (see "Later decisions" at
the end). The list of every model and its status is `docs/remade-models.md`.

## Goal

The game's maps were made for one steep camera. A battle uses a low camera
that turns. From that camera the maps show their limits: flat pictures that
stand for objects, sides that were never drawn, the end of the map, a flat
colour where a sky should be.

Every battle must happen on the real map, with no exceptions. Almost every
battle place gets its camera set by hand in the spot editor. So the goal is
not a better automatic camera. The goal is to repair the places and the
assets themselves, so a hand-set camera has something good to look at.

Rules for this work:

- Real solutions only. A fix must look right everywhere. No rule made for
  one scene or one kind of object.
- Keep the original game's style.
- Some work is done by hand. Program rules alone are not enough.
- Doubt every mirroring. A chair can be mirrored. The walls of a room
  cannot.
- New textures are kept as files for now. Later the game derives them from
  its own textures when it starts, so the project ships none.
- A change for real-map battles must not change battles with the Original
  backdrop. Check this with a pixel comparison after each change.
- Show results as in-game videos with the camera moving. Sheets of stills
  do not help the review.

## What was measured first

- The battle cameras always stand to the right of the battle line and look
  4 to 87 degrees to the left. They never look across or back.
- The sides that maps do not draw are the north faces.
- Inside places, a camera that looks south or south-west shows undrawn
  sides in 42 to 45 percent of the places tested.
- The building models were sorted into groups: whole, low, flat sheet,
  picture (mostly rocks), safe to mirror, uneven to mirror, partial, open.

## Rocks as bodies

A rock in the field is one flat picture, 32 by 32 pixels, that turns to the
camera. In a battle it looks like a card.

Tried and rejected: stacked rings (stripes, flat), a smooth body, cubes (too
tall), blocks as high as the picture stands (too flat), a foot cut at the
widest row (cut in lines).

Accepted as the basis: a body of blocks, one block a picture pixel. Each
picture row becomes a round slice. Four quarters each show the middle of
the picture. A block is as high as a pixel looks from the command camera
(the picture's lean plus 12.5 degrees). The foot keeps the picture's own
outline and sinks a little into the ground. The body stands on the point
the picture turns about, not on the picture's lower edge.

Faults found on the way and their fixes:

- The battle started black for up to a second. The spot search walked every
  triangle of every body. Bodies are now skipped by the search, and block
  faces are joined into rectangles with one texture a body (about 2,200
  triangles a rock). The map is ready in 114 to 277 ms on a cave map.
- Cut-out quads for thin shapes showed lines at a distance. Do not use
  cut-outs for thin shapes.
- Rocks floated because the body stood on the picture's lower edge.

Open: variety between rocks, rocks in water, a body that hides part of a
Pokemon sprite, and a late map on a large open-air place with many rocks
(Pinwheel Forest, 14 rocks: still black at the second intro still).

## Rock shadows

How the game draws a Pokemon's shadow: it draws the sprite again in black
at half the sprite's alpha, turned 11.25 degrees about Z (it leans left),
laid back on the ground with a turn of 0xC400 about X, and moved by
(-0.25, 0, -0.375) battle units. So the light comes from behind the camera,
a little from its right, 45 degrees up. 48 species have their own numbers.

A rock's shadow made by that same rule falls behind the rock, and the battle
camera cannot see it. This was checked with the camera turned 45 degrees:
the rock's shadow and the Pokemon's shadow lie the same way. A Pokemon's
shadow shows only because the sprite is flat and wider at the head than at
the feet. Earlier tries (a rim moved back, then left) were guesses and did
not agree with the sprites.

Built in the research branch:

- A: the shadow by the Pokemon's rule only (every block thrown on the
  ground by that light, as one see-through black sheet).
- B: the same, plus a dark outline two blocks wide round the foot, as the
  game's own rock picture has.

The choice between A and B is open. B was recommended.

The rule is exact for round bodies. A body that is not round needs its
shadow worked out in the game when the battle is placed. That is not built.

## The far fade ("fog")

The renderer faded the far map into the sky colour. The rule was: no fade
when the nearest map edge is 40 battle units away or less; otherwise a
circle from 45 to 97 percent of that distance, on all sides. A spot moved by
a few metres switched the fade on or off, and a narrow map (a Castelia
street) turned blue from just behind the foe.

Decision: no far fade at all. It is removed from both renderers in the
research branch (the shaders were recompiled). The zone's own fog from the
original game stays. Not on `main` yet. The spot editor must be rebuilt
when it lands.

## Where a map ends

The loader already loads every cell of a zone's map within 160 tiles of the
player. Where a battle shows the end of a map, the map itself ends there.

- Routes and towns sit in one shared open-air map and get their neighbours.
- Many places stand alone: city streets, rooms, caves, gates. The data has
  no neighbour for them. Of 171 zones with battles, 35 are in the shared
  map.
- The Castelia street is its own map, 32 by 64 tiles. The pier is 32 by 32.

### The sea

Built in the research branch: where water lies on the edge of everything
loaded, it continues outward (8192 map units) at the same height, with the
same texture and animation, and with its bed. On the Castelia pier the sea
now reaches the horizon and the seam does not show. The pier's sea tiles are
not water in the walk grid, so the rule reads the surface itself (a level
see-through sheet).

Not handled: water that ends at a missing cell inside the shared map.

### Joining maps through doors

Idea: load the map a door leads to and lay it beside the current one.

Findings in Castelia:

- The main avenue is a ring. The streets leave it like spokes toward the
  central plaza. The avenue draws each street as a narrow gap between two
  towers.
- The plaza draws each street's mouth turned (8, 17 and 9 degrees for three
  streets) and at 55 to 67 percent of the street's real width.
- Each street draws the plaza as a stub of road and a flat skyline picture.
- A street's map is 32 tiles wide. The mouths on the plaza are 9 to 12
  tiles apart. The street maps cannot all sit round the plaza at real size.
- A door into such a map gives rail coordinates, not a place in the world.
  The real place and turn were measured by walking the player through each
  door at both of its ends.

Decision on scope: do not join the avenue ring to the streets. Only inner
parts of one place would be joined, and every join would be set by hand.

Three ways to treat the ground both maps draw were built and compared:

| Way | From a street | From the plaza |
|---|---|---|
| Cut both maps at the door line | The plaza shows beyond the street | A street shows, with gaps in the ground |
| Hide whole objects, cut only the ground | About the same | About the same |
| Remove nothing | A wall stands across the street | The camera is inside a building |

All three have faults, because the maps were never made to fit together.
From inside a street with only the plaza joined the result is reasonable in
still pictures. It was not checked with the camera moving.

Decision: do not change this now. The wanted end state is scenery made by
hand in the game's style for the places where a map ends. That is later
work.

## The sky

The space beyond a map is one flat colour. The original battle has its own
far scenery: 20 sets, 26 models, each a ground disc, a soft horizon band and
a sky with a slow animation. They are soft gradients made for a small
screen. Decision: they are not reused. A proper sky is wanted, reused from
other assets if any fit, or made new. Nothing is built.

## Other results from the same period

- Full-battle recordings work: from the overworld, through the intro, a wait
  at the menu for the idle camera, a move, and back to the overworld.
- A wild test battle at a trainer's row ignores the row's hand-set spot and
  searches the area. Use the trainer's own battle to see a hand-set row.
- Among caves only Wellspring Cave has hand-set spots.
- Noted for the model work: the flower planter and the railing on the
  Castelia pier.

## Later decisions (2026-11-12, evening and night)

### Decisions on the open questions above

- Rock shadows: A, the Pokemon shadow rule alone, with no outline.
- Rocks: the three sides the picture does not have are drawn by hand, a face
  of its own for each side. Rearranging the picture's pixels by a rule
  (patches, mirrored halves) was rejected, and so was lighting the body
  again. The rocks in water are done, with their ripple flat on the water.
- The late map: the preparation passes now skip remade bodies. Route 1's
  battle map is ready in 356 ms (was 771 ms).

### The battle shows the field as it was

A battle on the real map built the map from its files, so anything the game
had changed was back in its first state: Striaton Gym's curtains were closed
again, pushed stones were back. Now every 3D object the field draws is noted
each field frame (its model, textures, animation frames, place and size) and
handed to the battle's map when the battle starts. One mechanism, no case
for a kind of object. Checked: all eight gyms, Chargestone Cave's floating
stones, a Strength boulder, the Pokemon League's light pillars. A model made
for the field's camera (the Chargestone stones) is stood where that camera
shows it.

Not done: the teal ring and the light rays round the League's statue do not
show in battle although the object is carried; texture and material
animations of carried objects are not played.

### All models surveyed

A sweep built the battle map of every map matrix and asked the loader which
building models it places: 273 of 435 are placed somewhere. Each model has a
shape group from a survey of its own triangles (whole, flat picture, flat
sheet, missing back, some sides missing, open shell). The groups, the counts
and the status of every model are in `docs/remade-models.md`.

### Flat sheets

The 23 placed flat sheets were found in the game one by one. Twelve need
nothing (paintings, a notice, a window pane, doors, a light flare, the
vase's pedestal, a cloud-shadow plane). The others:

- Pouffes: a rounded cushion. The first build, a block with the picture's
  dark outline on its edges, was rejected.
- Bottles: a standing and a lying turned body, with a shadow.
- Warp pads: a turned disc with raised rings. The first build raised every
  pixel by itself and its round edge came out as steps: rejected.
- The Pokemon League plaza's barred gate: the cut-out bars given depth. (A gold gate inside the League has the
  same build but was never found in the game; it is not in the table. Both gates, and the doors, are opened
  and shut by the game with an animation: a battle draws them in their resting position, which is not checked
  against the field's state yet.)
- A vase: a turned body with four bosses. Accepted; no shadow yet.
- Boxes and Chargestone's floor glows: moved down onto the floor.

The map holds some of these at a height over the floor that differs by room
(a pouffe's picture is 3.0 to 3.7 units up). A remade body can be marked to
rest on the floor; the map builder then moves it onto the highest level
surface under its middle.

Rules that came out of the reviews: a remade object must look good by itself
as a 3D object (its real form, no dark picture outline kept as a band, no
one-sided shade of the picture as a patch on one side); a before and after
picture must show the changed part large.

### How the remade models are made and shipped

The first builders were Python scripts in a research folder. They wrote one
record file a model, which the research build of the game loaded. That was a
sketching tool. It is replaced:

- Each kind of object has a builder in C in `engine/world/remade/` (cushion,
  bottles, disc, gate, floor, rock), and `engine/world/bmap_remade.c` says
  which model takes which builder with which settings. The rocks' hand-drawn
  faces are a C source file too.
- The game runs a model's builder the first time a battle map holds the
  model, on the model as the player's own game data has it, and keeps the
  result in memory. Nothing built is in the repository or in a release, and
  nothing is written to disk.
- Each C builder was checked against the Python builder's output triangle by
  triangle. New objects are written in C from the start.
- The work is done in this tree on `main`, not on a side branch.

`EIDOS_NO_REMADE=1` leaves every model as the map has it, for comparisons.
Battles with the Original backdrop were compared pixel by pixel after each
change (16 of 16 pictures identical).

## Open list

1. The vase: a shadow.
2. The League's teal ring and light rays; animations of carried objects.
3. What shows behind a door (the outside of a room).
4. The five early tries (fridge, glass door, banner, route gate, plant): a
   review, then a builder in C.
5. The groups not looked at yet: 133 placed models (`docs/remade-models.md`).
6. Gates and doors that the game opens and shuts: their state in a battle.
7. Sprites hidden by bodies; whether people get bodies.
8. Battles in a dark cave should look dark.
9. A proper sky.
10. Hand-made scenery where a stand-alone map ends; joining the parts of a
    place (shelved).
11. The Pokemon Center, a PC, the Castelia pier's planter and railing.
