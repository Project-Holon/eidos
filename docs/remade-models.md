# Remade models: the tracking list

The one-screen battle backdrop shows the real map. The map draws many small things as flat pictures or
leaves out the sides the field's steep camera never sees. A battle camera low over the ground shows that.
A remade model is such a thing built as a body, by C code inside the game, from the model's own triangles
and pixels, when the battle's map is built (`engine/world/remade/`, table in `engine/world/bmap_remade.c`).

This list has every building model of the game (435), by the shape group of the survey, with its status.
Keep it up to date by hand: when a model is looked at, remade or reviewed, change its line.

A model's name is `b<flag>_<id>`: the building pack flag of its area and its id in that pack.

## Statuses

| Status | Meaning |
|---|---|
| Remade | Built as a body by the source file named. Reviewed and accepted. |
| Stood on the floor | Left as it is; only moved down onto the floor under it. |
| Checked: fine as it is | Looked at in the game from several sides. No change needed. |
| No need: whole | The survey finds it complete from every side. Not looked at one by one. |
| Made earlier, not reviewed | An early try exists outside the game. Not in the game until reviewed. |
| Set aside | Looked at in the overworld; nothing that needs work now. May be looked at again. |
| Not looked at yet | Placed by the game. Nobody has checked whether it needs work. |
| Not placed by the game | Found in no map (see the limits below). Nothing to do. |

Limits of "not placed": the sweep built the battle map of one zone for each map matrix, in spring only,
and asked the loader which models it placed. A model that only another season places, or that the game's
code places (not a map file), would be listed as not placed. The Chargestone crystals were missed this way
once; treat the status as "not found", not as proof.

## Summary

| Group | Models | Remade | Stood on the floor | Checked: fine as it is | No need: whole | Set aside | Made earlier, not reviewed | Not looked at yet | Not placed by the game |
|---|---|---|---|---|---|---|---|---|---|
| Flat pictures | 41 | 6 |  |  |  | 2 |  |  | 33 |
| Flat sheets | 52 | 7 | 3 | 12 |  | 1 | 1 |  | 28 |
| Low things | 5 |  |  |  |  | 2 |  |  | 3 |
| Missing back, even | 53 |  |  |  |  |  | 2 | 34 | 17 |
| Missing back, uneven | 68 |  |  |  |  |  | 2 | 47 | 19 |
| Some sides missing | 53 |  |  |  |  |  |  | 34 | 19 |
| Open shells | 25 |  |  |  |  |  |  | 9 | 16 |
| Solid with a picture part | 7 |  |  |  |  |  |  | 7 |  |
| Whole | 131 |  |  |  | 105 |  |  |  | 26 |
| **All** | **435** | **13** | **3** | **12** | **105** | **5** | **5** | **131** | **161** |

## Where battles can happen

A model matters most when it stands in a zone where a battle can start (a trainer or a wild area: a row of
`engine/world/battle_spots.tsv`, the table the spot editor works on). The column Battles there says in how
many of the model's zones that is so. It is by zone, not by distance: a model in a battle zone can still be
far from every battle place. The open-air world is one map: an open-air model is judged by the one copy
the sweep recorded, so its other copies may stand where battles are or are not.

| Group | Placed | In a zone with battles | In no zone with battles | Open, with battles | Open, without |
|---|---|---|---|---|---|
| Flat pictures | 8 | 8 | 0 | 0 | 0 |
| Flat sheets | 23 | 14 | 9 | 0 | 0 |
| Low things | 2 | 1 | 1 | 0 | 0 |
| Missing back, even | 36 | 24 | 12 | 24 | 12 |
| Missing back, uneven | 49 | 38 | 11 | 38 | 11 |
| Some sides missing | 34 | 29 | 5 | 29 | 5 |
| Open shells | 9 | 6 | 3 | 6 | 3 |
| Solid with a picture part | 7 | 6 | 1 | 6 | 1 |
| Whole | 105 | 83 | 22 | 0 | 0 |
| **All** | **273** | **209** | **64** | **103** | **32** |

Open: not looked at yet, or an early try not reviewed.

## Open points

- Vase (`b0_00043`): no shadow yet.
- Inside the Pokemon League: the plain gold gate (`b0_00103`) and the arched gold fence are not identified in the game yet.
- Pokemon League plaza: the teal ring and light rays round the statue (a field object) do not show in battle.
- Doors: what shows behind a door is the outside of the room; how that should look is not decided.
- Doors and gates that the game opens and shuts by an animation (the League's gates, the doors): a battle draws
  them in their resting position. Whether the field's state reaches the battle for them is not checked. An Elite
  Four battle happens with the room's gate shut.
- Five early tries (plant, fridge, glass door, banner, route gate) wait for a review; then they are written in C.

## Flat pictures (41)

One picture that the map turns to face the camera. From a battle camera it is a card.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00039` | 2.0 x 1.5 x 1.5 | 22 | Twist Mountain, Victory Road, Giant Chasm and 2 more | yes, 9 of 9 zones | Remade | `engine/world/remade/rock.c` | cave rock; faces: rock_faces.c (cave) |
| `b0_00040` | 1.0 x 0.5 x 1.0 | 214 | Twist Mountain, Victory Road, Giant Chasm and 4 more | yes, 24 of 24 zones | Remade | `engine/world/remade/rock.c` | small cave rock; faces: rock_faces.c (cave) |
| `b0_00120` | 17.3 x 7.2 x 11.7 | 1 | Pokémon League | yes, 1 of 1 zones | Set aside |  | inside the Pokemon League (Elite Four room); seen in the overworld 2026-11-12: nothing that stands out |
| `b0_00121` | 5.8 x 5.3 x 0.2 | 1 | Pokémon League | yes, 1 of 1 zones | Set aside |  | inside the Pokemon League (Elite Four room); the same |
| `b1_00021` | 2.0 x 1.5 x 1.5 | 103 | the open-air world, Moor of Icirrus, zone 289 | yes, 2 of 3 zones | Remade | `engine/world/remade/rock.c` | open-air rock; faces: rock_faces.c (open) |
| `b1_00054` | 2.0 x 1.5 x 1.5 | 412 | the open-air world, Desert Resort, Lostlorn Forest and 1 more | yes, 3 of 4 zones | Remade | `engine/world/remade/rock.c` | rock in water, with its ripple; faces: rock_faces.c (water) |
| `b1_00055` | 1.0 x 0.5 x 1.0 | 315 | the open-air world, Desert Resort, Lostlorn Forest and 1 more | yes, 3 of 4 zones | Remade | `engine/world/remade/rock.c` | small rock in water, with its ripple; faces: rock_faces.c (water) |
| `b1_00258` | 1.0 x 0.5 x 1.0 | 95 | the open-air world, Giant Chasm, zone 136 and 2 more | yes, 2 of 5 zones | Remade | `engine/world/remade/rock.c` | small open-air rock; faces: rock_faces.c (open) |

Not placed by the game (33): `b0_00048`, `b0_00049`, `b0_00051`, `b0_00052`, `b0_00055`, `b0_00056`, `b0_00069`, `b0_00070`, `b0_00088`, `b0_00096`, `b1_00103`, `b1_00159`, `b1_00167`, `b1_00265`, `b1_00266`, `b1_00267`, `b1_00268`, `b1_00269`, `b1_00270`, `b1_00271`, `b1_00272`, `b1_00273`, `b1_00274`, `b1_00275`, `b1_00276`, `b1_00277`, `b1_00278`, `b1_00279`, `b1_00280`, `b1_00281`, `b1_00282`, `b1_00283`, `b1_00284`.

## Flat sheets (52)

A flat sheet lying or standing in the map, not turned to the camera.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00022` | 1.6 x 1.9 x 0.0 | 1 | zone 111 | no | Checked: fine as it is |  | notice board on a wall (Mistralton) |
| `b0_00027` | 1.0 x 1.2 x 0.1 | 4 | zone 10, zone 332 | no | Checked: fine as it is |  | window pane in a wall |
| `b0_00029` | 1.0 x 0.9 x 0.5 | 3 | Dreamyard | yes, 1 of 1 zones | Remade | `engine/world/remade/bottles.c` | bottles, one standing and one lying (Dreamyard) |
| `b0_00043` | 1.0 x 1.0 x 0.9 | 8 | Relic Castle | yes, 2 of 2 zones | Remade | `engine/world/remade/rock.c` | vase on a pedestal (Relic Castle); no shadow yet |
| `b0_00044` | 1.2 x 0.8 x 1.2 | 8 | Relic Castle | yes, 2 of 2 zones | Checked: fine as it is |  | the vase's stone pedestal: a real box |
| `b0_00061` | 1.0 x 0.7 x 1.1 | 10 | Cold Storage | yes, 1 of 1 zones | Stood on the floor | `engine/world/remade/floor.c` | boxes (Cold Storage); the map holds them 2 units up |
| `b0_00077` | 1.0 x 0.8 x 0.1 | 16 | Castelia City, zone 14, zone 17 and 8 more | yes, 1 of 11 zones | Checked: fine as it is |  | painting on a wall, 2 units thick |
| `b0_00078` | 2.0 x 1.8 x 0.1 | 8 | zone 10, zone 21, zone 22 and 1 more | no | Checked: fine as it is |  | painting in a gold frame, on a wall |
| `b0_00094` | 1.0 x 0.0 x 1.0 | 28 | Castelia City, zone 9, zone 10 and 5 more | yes, 1 of 8 zones | Remade | `engine/world/remade/cushion.c` | pouffe, orange (living rooms) |
| `b0_00095` | 1.0 x 0.0 x 1.0 | 37 | Castelia City, Nuvema Town, zone 12 and 7 more | yes, 2 of 10 zones | Remade | `engine/world/remade/cushion.c` | pouffe, blue (living rooms) |
| `b0_00103` | 3.0 x 2.0 x 1.0 | 1 | Pokémon League | yes, 1 of 1 zones | Set aside |  | a plain gold barred gate by its model (Pokemon League, inside); never seen in the game yet. A try with gate.c exists, not in the table. The arched gold fence of that room is another object, also to identify |
| `b0_00108` | 2.0 x 0.0 x 2.0 | 1 | Opelucid Gym | yes, 1 of 1 zones | Remade | `engine/world/remade/disc.c` | warp pad (Opelucid Gym) |
| `b0_00111` | 1.5 x 0.0 x 1.5 | 50 | Chargestone Cave | yes, 3 of 3 zones | Stood on the floor | `engine/world/remade/floor.c` | floor glow (Chargestone Cave); the map holds it 8 units up; no visible change seen yet |
| `b0_00112` | 1.5 x 0.0 x 1.5 | 45 | Chargestone Cave | yes, 3 of 3 zones | Stood on the floor | `engine/world/remade/floor.c` | floor glow (Chargestone Cave); the same |
| `b0_00131` | 2.3 x 3.0 x 0.4 | 2 | Bridge Gate, zone 384 | yes, 1 of 2 zones | Checked: fine as it is |  | double door (Bridge Gate), 6 units thick; what shows behind it is the outside of the room |
| `b0_00142` | 1.2 x 1.7 x 0.1 | 6 | zone 52 | no | Checked: fine as it is |  | cabin door (Royal Unova) |
| `b0_00143` | 1.2 x 1.7 x 0.1 | 9 | zone 52 | no | Checked: fine as it is |  | cabin door (Royal Unova) |
| `b1_00101` | 1.2 x 1.7 x 0.1 | 0 | - | no | Made earlier, not reviewed |  | glass door: shown from both sides (Python sketch, not in the game) |
| `b1_00192` | 2.0 x 0.0 x 2.0 | 1 | zone 137 | no | Remade | `engine/world/remade/disc.c` | warp pad (Pokemon League) |
| `b1_00234` | 3.0 x 2.5 x 0.0 | 2 | zone 28 | no | Checked: fine as it is |  | door panel on a building front (Castelia) |
| `b1_00238` | 1.9 x 1.6 x 0.0 | 10 | zone 263 | no | Checked: fine as it is |  | light flare (Marvelous Bridge) |
| `b1_00262` | 1.4 x 2.4 x 0.2 | 1 | Castelia City | yes, 1 of 1 zones | Checked: fine as it is |  | door (Castelia) |
| `b1_00292` | 47.0 x 0.0 x 45.6 | 5 | Castelia City, zone 36, zone 38 and 2 more | yes, 1 of 5 zones | Checked: fine as it is |  | a plane 164 units over the Castelia pier (cloud shadows); not seen in battle |
| `b1_00294` | 3.0 x 2.2 x 1.0 | 1 | zone 137 | no | Remade | `engine/world/remade/gate.c` | barred gate, grey (Pokemon League plaza) |

Not placed by the game (28): `b0_00001`, `b0_00028`, `b0_00032`, `b0_00050`, `b0_00089`, `b0_00090`, `b0_00091`, `b0_00098`, `b0_00102`, `b0_00104`, `b0_00105`, `b1_00150`, `b1_00165`, `b1_00166`, `b1_00188`, `b1_00211`, `b1_00217`, `b1_00218`, `b1_00219`, `b1_00230`, `b1_00236`, `b1_00244`, `b1_00259`, `b1_00260`, `b1_00261`, `b1_00263`, `b1_00264`, `b1_00287`.

## Low things (5)

Low things near the ground.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b1_00036` | 0.9 x 0.4 x 1.0 | 32 | open air; one copy: Route 4 | yes (its one known copy) | Set aside |  | Route 4; seen in the overworld 2026-11-12: nothing that stands out |
| `b1_00193` | 20.8 x 0.2 x 2.4 | 1 | open air; one copy: zone 389 | no (its one known copy) | Set aside |  | seen in the overworld 2026-11-12: nothing that stands out |

Not placed by the game (3): `b0_00113`, `b1_00228`, `b1_00229`.

## Missing back, even (53)

The side away from the field's camera is missing; the two halves are alike.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00000` | 1.4 x 2.2 x 0.9 | 10 | zone 1, zone 10, zone 67 and 7 more | no | Not looked at yet |  |  |
| `b0_00005` | 2.8 x 0.8 x 2.8 | 8 | P2 Laboratory, zone 12, zone 21 and 2 more | yes, 1 of 5 zones | Not looked at yet |  |  |
| `b0_00019` | 1.0 x 1.9 x 1.2 | 69 | Black City, Striaton City, Castelia City and 35 more | yes, 8 of 40 zones | Made earlier, not reviewed |  | potted plant: its back as its front turned (Python sketch, not in the game) |
| `b0_00025` | 1.8 x 1.3 x 0.1 | 23 | Nuvema Town, zone 9, zone 12 and 10 more | yes, 1 of 13 zones | Not looked at yet |  |  |
| `b0_00026` | 1.8 x 1.1 x 0.1 | 2 | zone 14 | no | Not looked at yet |  |  |
| `b0_00060` | 2.5 x 2.8 x 6.3 | 1 | Cold Storage | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00084` | 3.0 x 3.0 x 3.7 | 16 | Victory Road, Wellspring Cave, Mistralton Cave and 1 more | yes, 16 of 16 zones | Not looked at yet |  |  |
| `b0_00092` | 0.9 x 0.5 x 0.9 | 69 | Castelia City, P2 Laboratory, zone 10 and 6 more | yes, 2 of 9 zones | Not looked at yet |  |  |
| `b0_00125` | 9.0 x 10.1 x 10.3 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00016` | 10.0 x 5.4 x 7.2 | 1 | open air; one copy: Nacrene City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00018` | 4.5 x 3.7 x 3.9 | 1 | open air; one copy: Route 3 | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00030` | 2.9 x 3.2 x 3.3 | 2 | Dreamyard | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00039` | 7.0 x 4.8 x 7.5 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00045` | 6.6 x 5.4 x 5.6 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00046` | 3.2 x 4.2 x 3.2 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00049` | 6.4 x 3.9 x 4.6 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00090` | 6.5 x 3.8 x 5.4 | 1 | open air; one copy: Route 3 | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00093` | 1.7 x 1.7 x 0.6 | 1 | zone 28 | no | Made earlier, not reviewed |  | banner: shown from both sides (Python sketch, not in the game) |
| `b1_00100` | 8.0 x 7.9 x 8.4 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00117` | 4.4 x 4.5 x 10.2 | 1 | zone 418 | no | Not looked at yet |  |  |
| `b1_00122` | 7.3 x 4.0 x 4.6 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00146` | 14.0 x 12.9 x 16.2 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00148` | 8.0 x 4.6 x 9.3 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00161` | 14.5 x 8.0 x 8.8 | 1 | open air; one copy: Route 10 | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00162` | 16.0 x 7.1 x 2.2 | 1 | zone 356 | no | Not looked at yet |  |  |
| `b1_00168` | 16.0 x 7.1 x 2.2 | 1 | zone 357 | no | Not looked at yet |  |  |
| `b1_00169` | 16.0 x 7.1 x 2.2 | 1 | Route 10 | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00170` | 16.0 x 7.1 x 2.2 | 1 | zone 359 | no | Not looked at yet |  |  |
| `b1_00171` | 16.0 x 7.1 x 2.2 | 1 | zone 360 | no | Not looked at yet |  |  |
| `b1_00172` | 16.0 x 7.1 x 2.2 | 1 | zone 361 | no | Not looked at yet |  |  |
| `b1_00173` | 16.0 x 7.1 x 2.2 | 1 | zone 362 | no | Not looked at yet |  |  |
| `b1_00174` | 16.0 x 7.1 x 2.2 | 1 | zone 363 | no | Not looked at yet |  |  |
| `b1_00175` | 14.5 x 7.8 x 12.0 | 1 | zone 364 | no | Not looked at yet |  |  |
| `b1_00177` | 2.9 x 3.6 x 2.3 | 1 | Abundant Shrine | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00243` | 5.0 x 3.9 x 18.0 | 6 | Village Bridge | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00290` | 4.0 x 3.5 x 1.3 | 1 | Celestial Tower | yes, 1 of 1 zones | Not looked at yet |  |  |

Not placed by the game (17): `b0_00046`, `b0_00093`, `b1_00070`, `b1_00071`, `b1_00074`, `b1_00075`, `b1_00133`, `b1_00164`, `b1_00176`, `b1_00235`, `b1_00237`, `b1_00251`, `b1_00252`, `b1_00253`, `b1_00254`, `b1_00257`, `b1_00288`.

## Missing back, uneven (68)

The same, but the two halves differ.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00002` | 2.8 x 1.5 x 1.2 | 17 | Castelia City, Nuvema Town, Undella Town and 14 more | yes, 3 of 17 zones | Not looked at yet |  |  |
| `b0_00011` | 2.1 x 1.9 x 0.8 | 17 | Undella Town, zone 12, zone 21 and 9 more | yes, 1 of 12 zones | Not looked at yet |  |  |
| `b0_00012` | 2.1 x 1.9 x 0.9 | 11 | Nuvema Town, Undella Town, zone 21 and 8 more | yes, 2 of 11 zones | Not looked at yet |  |  |
| `b0_00013` | 1.9 x 0.9 x 1.0 | 4 | zone 1, zone 21 | no | Not looked at yet |  |  |
| `b0_00014` | 0.9 x 1.9 x 0.8 | 15 | zone 10, zone 12, zone 21 and 10 more | no | Not looked at yet |  |  |
| `b0_00015` | 1.2 x 1.8 x 0.8 | 23 | Nuvema Town, zone 10, zone 21 and 13 more | yes, 1 of 16 zones | Not looked at yet |  |  |
| `b0_00017` | 1.9 x 0.8 x 1.6 | 12 | Nuvema Town, zone 12, zone 14 and 9 more | yes, 1 of 12 zones | Not looked at yet |  |  |
| `b0_00018` | 1.1 x 1.8 x 0.7 | 17 | Nuvema Town, Undella Town, zone 2 and 13 more | yes, 2 of 16 zones | Made earlier, not reviewed |  | fridge: a flat back added (Python sketch, not in the game) |
| `b0_00034` | 2.7 x 2.5 x 2.3 | 2 | zone 1, zone 146 | no | Not looked at yet |  |  |
| `b0_00035` | 0.9 x 1.0 x 0.5 | 12 | Shopping Mall, Undella Town, zone 2 and 8 more | yes, 2 of 11 zones | Not looked at yet |  |  |
| `b0_00054` | 2.2 x 2.1 x 1.5 | 1 | zone 1 | no | Not looked at yet |  |  |
| `b0_00079` | 1.2 x 1.7 x 1.2 | 16 | Shopping Mall, Undella Town, zone 21 and 5 more | yes, 2 of 8 zones | Not looked at yet |  |  |
| `b0_00080` | 1.2 x 1.7 x 1.2 | 3 | zone 21, zone 43 | no | Not looked at yet |  |  |
| `b0_00081` | 1.0 x 1.8 x 1.1 | 6 | Castelia City, zone 14, zone 270 | yes, 2 of 4 zones | Not looked at yet |  |  |
| `b0_00082` | 1.0 x 1.8 x 1.1 | 6 | Castelia City, zone 14, zone 270 | yes, 2 of 4 zones | Not looked at yet |  |  |
| `b0_00086` | 1.2 x 2.1 x 0.8 | 8 | Shopping Mall, Bridge Gate, zone 76 and 2 more | yes, 2 of 5 zones | Not looked at yet |  |  |
| `b0_00119` | 22.6 x 16.5 x 15.9 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00126` | 2.1 x 1.8 x 0.7 | 8 | Castelia City, P2 Laboratory, zone 14 and 1 more | yes, 3 of 5 zones | Not looked at yet |  |  |
| `b0_00129` | 2.5 x 2.7 x 1.8 | 6 | Castelia City, zone 44, zone 47 and 2 more | yes, 2 of 6 zones | Not looked at yet |  |  |
| `b0_00134` | 6.9 x 2.4 x 6.6 | 1 | zone 381 | no | Not looked at yet |  |  |
| `b0_00138` | 2.7 x 2.5 x 2.3 | 1 | zone 145 | no | Not looked at yet |  |  |
| `b1_00000` | 7.3 x 5.0 x 5.3 | 22 | Black City, the open-air world, zone 289 and 1 more | yes, 2 of 4 zones | Not looked at yet |  |  |
| `b1_00005` | 5.0 x 4.4 x 4.4 | 3 | open air; one copy: zone 389 | no (its one known copy) | Not looked at yet |  |  |
| `b1_00011` | 6.3 x 6.7 x 5.0 | 1 | open air; one copy: Striaton City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00013` | 6.0 x 4.8 x 5.2 | 3 | open air; one copy: Nacrene City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00014` | 4.8 x 4.6 x 5.2 | 2 | open air; one copy: Nacrene City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00015` | 4.5 x 4.8 x 5.2 | 5 | open air; one copy: Nacrene City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00040` | 7.0 x 4.8 x 7.5 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00051` | 10.2 x 11.8 x 5.6 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00057` | 7.4 x 8.8 x 6.0 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00058` | 4.6 x 4.1 x 5.6 | 8 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00105` | 5.0 x 3.0 x 6.8 | 1 | open air; one copy: Striaton City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00112` | 5.1 x 2.6 x 3.2 | 8 | the open-air world, Village Bridge, Lostlorn Forest and 1 more | yes, 3 of 4 zones | Made earlier, not reviewed |  | route gate: missing half added by turning (Python sketch, not in the game) |
| `b1_00113` | 5.0 x 4.5 x 4.9 | 10 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00114` | 7.4 x 7.2 x 6.2 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00116` | 4.6 x 4.4 x 5.5 | 3 | zone 418 | no | Not looked at yet |  |  |
| `b1_00119` | 5.2 x 4.0 x 4.9 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00120` | 17.3 x 7.0 x 11.1 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00126` | 3.8 x 3.3 x 3.5 | 8 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00129` | 15.4 x 16.3 x 7.3 | 1 | Nimbasa City | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00144` | 11.0 x 5.8 x 7.0 | 32 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00155` | 1.9 x 2.4 x 0.8 | 22 | the open-air world, Nimbasa City, Twist Mountain and 2 more | yes, 4 of 5 zones | Not looked at yet |  |  |
| `b1_00157` | 11.4 x 5.1 x 8.3 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00160` | 8.4 x 6.4 x 4.5 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00246` | 7.5 x 3.8 x 4.4 | 1 | open air; one copy: P2 Laboratory | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00289` | 151.6 x 85.6 x 27.9 | 1 | zone 136 | no | Not looked at yet |  |  |
| `b1_00293` | 0.8 x 7.1 x 8.3 | 1 | zone 34 | no | Not looked at yet |  |  |
| `b1_00295` | 41.8 x 6.4 x 37.7 | 1 | open air; one copy: Route 1 | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00296` | 36.8 x 5.4 x 34.5 | 1 | open air; one copy: Route 1 | yes (its one known copy) | Not looked at yet |  |  |

Not placed by the game (19): `b0_00076`, `b1_00004`, `b1_00072`, `b1_00073`, `b1_00076`, `b1_00077`, `b1_00182`, `b1_00183`, `b1_00185`, `b1_00186`, `b1_00187`, `b1_00215`, `b1_00216`, `b1_00297`, `b1_00298`, `b1_00299`, `b1_00300`, `b1_00301`, `b1_00302`.

## Some sides missing (53)

Several sides are missing.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00066` | 2.9 x 0.6 x 0.9 | 6 | Dreamyard | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00087` | 11.6 x 11.6 x 2.2 | 1 | Nimbasa Gym | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00099` | 2.6 x 6.1 x 2.5 | 3 | zone 1 | no | Not looked at yet |  |  |
| `b0_00100` | 11.4 x 7.2 x 5.7 | 1 | Driftveil Gym | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00106` | 2.0 x 1.6 x 2.0 | 2 | Opelucid Gym, zone 422 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b0_00107` | 2.0 x 1.6 x 2.0 | 9 | Nimbasa City, Pokémon League, zone 79 and 2 more | yes, 6 of 9 zones | Not looked at yet |  |  |
| `b0_00114` | 1.3 x 1.0 x 1.3 | 4 | Pokémon League | yes, 4 of 4 zones | Not looked at yet |  |  |
| `b0_00116` | 27.1 x 11.5 x 29.7 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00118` | 6.9 x 15.3 x 10.9 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00122` | 12.1 x 10.0 x 6.2 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00132` | 6.9 x 0.8 x 5.2 | 1 | zone 274 | no | Not looked at yet |  |  |
| `b0_00137` | 8.9 x 5.7 x 29.3 | 1 | Cold Storage | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00006` | 8.6 x 5.6 x 5.8 | 1 | open air; one copy: zone 389 | no (its one known copy) | Not looked at yet |  |  |
| `b1_00017` | 5.5 x 3.0 x 2.8 | 1 | open air; one copy: Nacrene City | yes (its one known copy) | Not looked at yet |  |  |
| `b1_00022` | 5.8 x 1.9 x 2.0 | 4 | Pinwheel Forest | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00026` | 1.9 x 1.7 x 1.7 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00027` | 0.9 x 0.7 x 0.8 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00032` | 8.5 x 4.2 x 4.4 | 1 | Dreamyard | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00044` | 5.2 x 3.4 x 3.5 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00047` | 3.8 x 6.1 x 9.0 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00062` | 4.9 x 4.4 x 5.1 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00063` | 4.9 x 4.4 x 5.0 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00064` | 5.5 x 6.9 x 5.1 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00108` | 13.0 x 9.4 x 10.0 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00111` | 17.2 x 13.0 x 16.6 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00130` | 8.9 x 5.1 x 10.2 | 1 | Nimbasa City | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00132` | 8.1 x 3.7 x 10.8 | 1 | zone 418 | no | Not looked at yet |  |  |
| `b1_00137` | 6.8 x 7.6 x 6.8 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00143` | 9.5 x 10.9 x 8.0 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | Not looked at yet |  |  |
| `b1_00156` | 1.0 x 0.8 x 1.0 | 32 | the open-air world, Castelia City, Nimbasa City and 4 more | yes, 5 of 7 zones | Not looked at yet |  |  |
| `b1_00178` | 5.2 x 3.9 x 4.8 | 1 | Abundant Shrine | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00194` | 59.8 x 4.4 x 43.9 | 1 | zone 28 | no | Not looked at yet |  |  |
| `b1_00204` | 11.3 x 24.4 x 19.3 | 1 | Castelia City | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00291` | 1.9 x 2.1 x 2.0 | 20 | the open-air world, Village Bridge, zone 289 | yes, 2 of 3 zones | Not looked at yet |  |  |

Not placed by the game (19): `b1_00029`, `b1_00031`, `b1_00082`, `b1_00083`, `b1_00084`, `b1_00085`, `b1_00086`, `b1_00087`, `b1_00118`, `b1_00123`, `b1_00138`, `b1_00158`, `b1_00190`, `b1_00191`, `b1_00227`, `b1_00232`, `b1_00248`, `b1_00249`, `b1_00250`.

## Open shells (25)

Open shells: rooms, building fronts.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00071` | 3.4 x 6.2 x 3.4 | 10 | Relic Castle | yes, 5 of 5 zones | Not looked at yet |  |  |
| `b0_00101` | 3.4 x 50.1 x 50.5 | 2 | Driftveil Gym | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00117` | 15.1 x 8.9 x 11.1 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00123` | 39.0 x 46.2 x 31.8 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00124` | 16.4 x 4.8 x 18.1 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00128` | 13.4 x 13.3 x 26.4 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00133` | 9.4 x 14.1 x 5.0 | 1 | zone 274 | no | Not looked at yet |  |  |
| `b0_00140` | 32.0 x 13.6 x 32.0 | 1 | zone 17 | no | Not looked at yet |  |  |
| `b1_00239` | 22.5 x 26.2 x 18.5 | 1 | zone 362 | no | Not looked at yet |  |  |

Not placed by the game (16): `b0_00072`, `b0_00141`, `b1_00080`, `b1_00081`, `b1_00181`, `b1_00184`, `b1_00189`, `b1_00213`, `b1_00214`, `b1_00220`, `b1_00221`, `b1_00222`, `b1_00223`, `b1_00224`, `b1_00225`, `b1_00226`.

## Solid with a picture part (7)

A solid model that has a flat picture as one of its parts.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00097` | 0.7 x 0.8 x 0.4 | 2 | Nacrene Gym | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b0_00115` | 27.5 x 27.9 x 21.7 | 1 | Pokémon League | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00195` | 1.1 x 4.0 x 1.2 | 26 | Castelia City, zone 32, zone 34 | yes, 4 of 6 zones | Not looked at yet |  |  |
| `b1_00196` | 2.3 x 4.2 x 1.3 | 24 | Castelia City, zone 36, zone 38 and 2 more | yes, 1 of 5 zones | Not looked at yet |  |  |
| `b1_00241` | 45.4 x 24.3 x 8.1 | 1 | open air; one copy: zone 147 | no (its one known copy) | Not looked at yet |  |  |
| `b1_00286` | 16.9 x 5.8 x 8.5 | 1 | Village Bridge | yes, 1 of 1 zones | Not looked at yet |  |  |
| `b1_00303` | 1.9 x 5.8 x 1.5 | 2 | Village Bridge | yes, 1 of 1 zones | Not looked at yet |  |  |

## Whole (131)

Complete from every side. Nothing to do.

| Model | Size in tiles (w x h x d) | Copies | Where | Battles there | Status | Source | Note |
|---|---|---|---|---|---|---|---|
| `b0_00003` | 2.2 x 1.8 x 2.2 | 4 | zone 9, zone 390, zone 392 and 1 more | no | No need: whole |  |  |
| `b0_00004` | 2.8 x 1.4 x 2.1 | 6 | Nuvema Town, zone 10, zone 12 and 3 more | yes, 1 of 6 zones | No need: whole |  |  |
| `b0_00006` | 2.7 x 1.1 x 1.0 | 4 | Castelia City, zone 14 | yes, 2 of 3 zones | No need: whole |  |  |
| `b0_00007` | 2.8 x 0.5 x 0.9 | 3 | Liberty Garden, zone 22, zone 25 | yes, 1 of 3 zones | No need: whole |  |  |
| `b0_00008` | 0.9 x 0.8 x 0.9 | 50 | Castelia Gate, Shopping Mall, Nuvema Town and 14 more | yes, 3 of 17 zones | No need: whole |  |  |
| `b0_00009` | 0.7 x 0.8 x 0.7 | 21 | Castelia City, zone 14 | yes, 2 of 3 zones | No need: whole |  |  |
| `b0_00010` | 0.7 x 0.8 x 0.7 | 15 | zone 22, zone 25, zone 26 | no | No need: whole |  |  |
| `b0_00016` | 1.6 x 0.8 x 2.1 | 9 | zone 22, zone 25, zone 52 and 2 more | no | No need: whole |  |  |
| `b0_00020` | 0.8 x 0.5 x 0.9 | 51 | Striaton City, Castelia City, Castelia Gate and 40 more | yes, 6 of 43 zones | No need: whole |  |  |
| `b0_00023` | 2.0 x 1.9 x 1.9 | 3 | Nuvema Town, zone 270, zone 332 | yes, 1 of 3 zones | No need: whole |  |  |
| `b0_00024` | 2.3 x 2.7 x 2.1 | 2 | Nuvema Town, zone 270 | yes, 1 of 2 zones | No need: whole |  |  |
| `b0_00030` | 1.1 x 1.4 x 2.9 | 14 | P2 Laboratory, Nuvema Town, zone 10 and 5 more | yes, 2 of 8 zones | No need: whole |  |  |
| `b0_00031` | 1.1 x 1.4 x 3.0 | 12 | Castelia City, zone 14 | yes, 2 of 3 zones | No need: whole |  |  |
| `b0_00033` | 1.0 x 0.6 x 3.0 | 7 | zone 9, zone 12, zone 44 and 4 more | no | No need: whole |  |  |
| `b0_00036` | 0.8 x 0.8 x 0.8 | 4 | zone 1 | no | No need: whole |  |  |
| `b0_00037` | 1.0 x 0.7 x 1.8 | 1 | zone 1 | no | No need: whole |  |  |
| `b0_00038` | 0.8 x 0.8 x 0.8 | 7 | Striaton Gym, Shopping Mall, zone 26 and 3 more | yes, 2 of 6 zones | No need: whole |  |  |
| `b0_00041` | 2.1 x 2.9 x 3.0 | 13 | Relic Castle | yes, 12 of 12 zones | No need: whole |  |  |
| `b0_00042` | 3.0 x 3.4 x 3.0 | 12 | Relic Castle | yes, 11 of 11 zones | No need: whole |  |  |
| `b0_00045` | 5.0 x 1.4 x 5.0 | 10 | Relic Castle | yes, 5 of 5 zones | No need: whole |  |  |
| `b0_00063` | 1.0 x 0.7 x 1.1 | 34 | Cold Storage | yes, 2 of 2 zones | No need: whole |  |  |
| `b0_00064` | 5.0 x 2.9 x 5.2 | 1 | Cold Storage | yes, 1 of 1 zones | No need: whole |  |  |
| `b0_00065` | 2.3 x 0.7 x 2.7 | 7 | Dreamyard | yes, 1 of 1 zones | No need: whole |  |  |
| `b0_00074` | 1.0 x 0.6 x 1.0 | 13 | Castelia City, Shopping Mall, zone 9 and 7 more | yes, 3 of 11 zones | No need: whole |  |  |
| `b0_00075` | 2.5 x 2.6 x 2.5 | 1 | zone 405 | no | No need: whole |  |  |
| `b0_00083` | 2.8 x 1.6 x 2.2 | 1 | zone 405 | no | No need: whole |  |  |
| `b0_00085` | 3.0 x 3.0 x 3.0 | 16 | Victory Road, Wellspring Cave, Mistralton Cave and 2 more | yes, 15 of 15 zones | No need: whole |  |  |
| `b0_00110` | 3.3 x 2.2 x 3.0 | 21 | Chargestone Cave | yes, 3 of 3 zones | No need: whole |  |  |
| `b0_00127` | 1.9 x 3.0 x 1.9 | 1 | zone 1 | no | No need: whole |  |  |
| `b0_00130` | 3.9 x 4.2 x 3.2 | 1 | zone 148 | no | No need: whole |  |  |
| `b0_00135` | 4.8 x 2.9 x 20.9 | 1 | Cold Storage | yes, 1 of 1 zones | No need: whole |  |  |
| `b0_00136` | 17.6 x 2.9 x 24.5 | 1 | Cold Storage | yes, 1 of 1 zones | No need: whole |  |  |
| `b0_00139` | 3.6 x 3.2 x 3.4 | 1 | zone 17 | no | No need: whole |  |  |
| `b0_00144` | 1.2 x 1.8 x 1.2 | 1 | zone 93 | no | No need: whole |  |  |
| `b0_00145` | 50.4 x 3.9 x 45.2 | 1 | zone 66 | no | No need: whole |  |  |
| `b1_00001` | 5.5 x 2.8 x 11.0 | 33 | the open-air world, Desert Resort, Village Bridge and 1 more | yes, 3 of 4 zones | No need: whole |  |  |
| `b1_00002` | 5.3 x 4.7 x 3.6 | 1 | open air; one copy: Accumula Town | yes (its one known copy) | No need: whole |  |  |
| `b1_00003` | 6.3 x 6.8 x 6.1 | 3 | open air; one copy: Accumula Town | yes (its one known copy) | No need: whole |  |  |
| `b1_00007` | 5.2 x 6.6 x 5.2 | 1 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00008` | 5.4 x 5.9 x 5.4 | 3 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00010` | 5.2 x 5.9 x 4.8 | 1 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00012` | 8.8 x 5.6 x 1.8 | 1 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00019` | 3.0 x 1.0 x 3.0 | 6 | Pinwheel Forest, Lostlorn Forest | yes, 2 of 2 zones | No need: whole |  |  |
| `b1_00020` | 3.1 x 1.1 x 3.0 | 5 | Pinwheel Forest, Lostlorn Forest | yes, 2 of 2 zones | No need: whole |  |  |
| `b1_00023` | 5.9 x 5.1 x 2.0 | 2 | Pinwheel Forest | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00024` | 3.5 x 1.7 x 3.0 | 16 | the open-air world, Nimbasa City, zone 289 | yes, 2 of 3 zones | No need: whole |  |  |
| `b1_00025` | 0.6 x 0.8 x 0.6 | 18 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00028` | 1.0 x 1.4 x 1.1 | 9 | Dreamyard | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00033` | 6.0 x 4.9 x 6.4 | 3 | open air; one copy: Route 4 | yes (its one known copy) | No need: whole |  |  |
| `b1_00034` | 5.8 x 3.9 x 6.1 | 3 | open air; one copy: Route 4 | yes (its one known copy) | No need: whole |  |  |
| `b1_00035` | 1.8 x 1.2 x 4.9 | 20 | open air; one copy: Route 4 | yes (its one known copy) | No need: whole |  |  |
| `b1_00037` | 5.4 x 1.2 x 5.5 | 1 | Desert Resort | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00038` | 6.0 x 6.4 x 5.2 | 1 | Desert Resort | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00041` | 1.8 x 1.9 x 3.8 | 30 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00042` | 1.8 x 1.9 x 3.8 | 14 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00043` | 12.7 x 4.2 x 6.0 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00048` | 3.5 x 4.5 x 3.5 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00050` | 3.5 x 1.7 x 7.5 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00052` | 8.5 x 4.6 x 8.7 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00053` | 15.6 x 10.4 x 6.0 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00056` | 0.8 x 1.0 x 0.8 | 29 | open air; one copy: Route 4 | yes (its one known copy) | No need: whole |  |  |
| `b1_00061` | 8.7 x 3.9 x 9.0 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00065` | 6.0 x 13.9 x 5.8 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00067` | 4.5 x 2.9 x 4.5 | 4 | the open-air world, Twist Mountain | yes, 2 of 2 zones | No need: whole |  |  |
| `b1_00068` | 5.2 x 3.1 x 4.0 | 2 | Twist Mountain | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00069` | 1.2 x 1.3 x 2.2 | 34 | the open-air world, Village Bridge, zone 289 | yes, 2 of 3 zones | No need: whole |  |  |
| `b1_00091` | 5.8 x 3.2 x 2.1 | 1 | open air; one copy: Route 3 | yes (its one known copy) | No need: whole |  |  |
| `b1_00092` | 5.0 x 0.7 x 5.0 | 1 | open air; one copy: Route 3 | yes (its one known copy) | No need: whole |  |  |
| `b1_00094` | 7.0 x 5.4 x 8.5 | 6 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00095` | 7.0 x 5.4 x 7.5 | 24 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00098` | 5.2 x 2.4 x 2.3 | 1 | open air; one copy: Route 3 | yes (its one known copy) | No need: whole |  |  |
| `b1_00099` | 5.1 x 2.4 x 2.2 | 1 | open air; one copy: Route 3 | yes (its one known copy) | No need: whole |  |  |
| `b1_00102` | 13.3 x 9.5 x 10.2 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00104` | 5.2 x 4.7 x 5.2 | 1 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00106` | 1.8 x 1.7 x 0.9 | 4 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00107` | 1.4 x 2.0 x 0.8 | 4 | open air; one copy: Striaton City | yes (its one known copy) | No need: whole |  |  |
| `b1_00109` | 20.5 x 7.2 x 15.2 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00110` | 15.6 x 6.4 x 13.8 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00121` | 6.5 x 8.4 x 6.5 | 2 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00131` | 4.8 x 2.0 x 4.8 | 1 | Nimbasa City | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00134` | 3.2 x 2.1 x 3.1 | 24 | the open-air world, Village Bridge, Lostlorn Forest and 1 more | yes, 3 of 4 zones | No need: whole |  |  |
| `b1_00135` | 7.0 x 6.6 x 7.5 | 12 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00139` | 7.0 x 6.2 x 7.0 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00141` | 4.5 x 6.4 x 4.1 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00147` | 7.9 x 7.0 x 1.5 | 6 | the open-air world, Nimbasa City, zone 289 | yes, 2 of 3 zones | No need: whole |  |  |
| `b1_00149` | 6.8 x 5.8 x 6.8 | 1 | Nimbasa City | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00151` | 1.9 x 1.9 x 0.6 | 33 | the open-air world, Pinwheel Forest, zone 289 | yes, 2 of 3 zones | No need: whole |  |  |
| `b1_00152` | 1.9 x 1.9 x 0.6 | 28 | Black City, the open-air world, Village Bridge and 3 more | yes, 3 of 6 zones | No need: whole |  |  |
| `b1_00153` | 1.4 x 1.0 x 1.0 | 72 | the open-air world, Desert Resort, Village Bridge and 1 more | yes, 3 of 4 zones | No need: whole |  |  |
| `b1_00154` | 1.9 x 1.9 x 0.6 | 53 | the open-air world, Village Bridge, zone 289 | yes, 2 of 3 zones | No need: whole |  |  |
| `b1_00180` | 8.1 x 3.3 x 3.6 | 26 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00197` | 7.7 x 3.6 x 7.3 | 1 | Castelia City | yes, 1 of 1 zones | No need: whole |  |  |
| `b1_00205` | 14.5 x 10.9 x 46.1 | 1 | zone 39 | no | No need: whole |  |  |
| `b1_00206` | 14.7 x 5.1 x 7.1 | 1 | zone 40 | no | No need: whole |  |  |
| `b1_00207` | 7.1 x 5.1 x 14.7 | 1 | zone 38 | no | No need: whole |  |  |
| `b1_00208` | 7.1 x 5.1 x 14.7 | 2 | the open-air world, Castelia City | yes, 2 of 2 zones | No need: whole |  |  |
| `b1_00209` | 10.3 x 5.1 x 13.6 | 2 | the open-air world, zone 36 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00210` | 5.4 x 4.8 x 5.1 | 2 | zone 263 | no | No need: whole |  |  |
| `b1_00212` | 0.8 x 2.9 x 0.8 | 2 | zone 263 | no | No need: whole |  |  |
| `b1_00240` | 9.5 x 9.4 x 7.6 | 1 | open air; one copy: Liberty Garden | yes (its one known copy) | No need: whole |  |  |
| `b1_00242` | 10.3 x 7.7 x 7.0 | 1 | open air; one copy: zone 147 | no (its one known copy) | No need: whole |  |  |
| `b1_00245` | 5.5 x 2.8 x 8.5 | 4 | the open-air world, zone 289 | yes, 1 of 2 zones | No need: whole |  |  |
| `b1_00255` | 6.6 x 4.4 x 7.0 | 1 | zone 295 | no | No need: whole |  |  |
| `b1_00256` | 3.8 x 4.5 x 18.0 | 1 | zone 418 | no | No need: whole |  |  |
| `b1_00285` | 2.0 x 1.8 x 2.0 | 1 | zone 279 | no | No need: whole |  |  |

Not placed by the game (26): `b0_00021`, `b0_00047`, `b0_00053`, `b0_00057`, `b0_00058`, `b0_00059`, `b0_00062`, `b0_00067`, `b0_00068`, `b1_00009`, `b1_00059`, `b1_00060`, `b1_00066`, `b1_00096`, `b1_00097`, `b1_00115`, `b1_00127`, `b1_00128`, `b1_00136`, `b1_00140`, `b1_00142`, `b1_00145`, `b1_00163`, `b1_00231`, `b1_00233`, `b1_00247`.
