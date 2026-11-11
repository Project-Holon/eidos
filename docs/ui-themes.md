# Interface themes

The one-screen interface has themes. A theme is code: it draws the kit's
pieces and it places the parts of each screen. A theme has no image files.

Three themes are built in. The player chooses one in Settings (Layout,
"Interface style") or in the game's options (DISPLAY, "STYLE"). The setting is
`ui_theme` in `settings.ini`. `EIDOS_UI_THEME=<id>` overrides it for a test.

| Id | Name | Idea |
|---|---|---|
| `kosmos` | Kosmos | The default. Dark windows over the world, cyan accents. |
| `plasma` | Plasma | A modern take on Black and White. A band along the bottom holds the prompt and the actions as leaning chips. Each screen has a ground in its own hue. |
| `harmonia` | Harmonia | The closest to the DS. Each screen takes the forms and colours of its own DS screen, laid out for one wide screen. |

## How a theme works

`engine/ui/theme.h` has one table of functions, `UiThemeOps`. A slot that is
`NULL` uses Kosmos's code. A theme fills the slots it wants.

There are four kinds of slot.

1. **Colours and text.** `color` maps every colour the screens draw with.
   `text` sets a letter's colour from the colour that is under it.
2. **Kit pieces.** One slot for each piece in `engine/ui/kit.h` (panel, header,
   row, tab, key cap, scroll bar, dialog, party row and so on). A screen calls
   `ui_panel(...)`; the theme's `panel` draws it.
3. **Screen layouts.** A screen fills a layout structure with Kosmos's places.
   Then the theme moves them. The screen draws and tests clicks at the places
   in the structure, so the mouse and touch follow the theme.
4. **Screen grounds.** `app_frame` draws the ground and the window of a screen
   that keeps Kosmos's arrangement.

### Screen layouts

| Screen | Structure | Slots |
|---|---|---|
| Battle | `UiBattleLayout` | `battle_layout`, `battle_backdrop`, `battle_box`, `battle_command`, `battle_move`, `battle_move_info`, `battle_balls`, `battle_ability` |
| X menu | `UiFieldMenuLayout` | `field_menu_layout`, `field_menu_backdrop`, `field_menu_button` |
| Title menu | `UiTitleMenuLayout` | `title_menu_layout`, `title_menu_backdrop`, `title_menu_row` |
| Party | `UiPartyLayout`, `UiPartyMon` | `party_layout`, `party_backdrop`, `party_plate`, `party_detail`, `party_prompt`, `party_menu_entry` |
| Bag | `UiBagLayout` | `bag_layout`, `bag_backdrop`, `bag_pocket`, `bag_row`, `bag_detail_frame` |
| Field bars | none | `strip`, `clock_tab` |
| Text boxes | none | `textbox`, `choice_row` |

A layout can change how the keys move. The title menu has `stacked` (one
column) and `across` (rows side by side). The party has `column` (up and down
go through the six in order) and `menu_across` (left and right move in the
menu). The X menu moves to the nearest button in the direction of the arrow.

### Screen grounds

A screen calls `ui_app_window(c, "<id>", &x, &y, &w, &h)` where Kosmos draws
its window. When the theme's `app_frame` returns 1, the theme drew the ground
and the window, and it can change the frame. When it returns 0, the screen
draws Kosmos's panel.

The ids are: `summary`, `summary-side`, `summary-card`, `pc`, `pc-box`,
`pc-cell`, `dex`, `options`, `save`, `card`, `shop`, `settings`, `namein`,
`xtrans`, `mystery`, `relearn`, `wireless`, `entree`, `survey`, `battle-bag`,
`battle-party`.

Some ids are for one part of a screen: `pc-box`, `pc-cell` and `pc-detail`
(the PC's box, one cell, the pane for the Pokemon under the cursor),
`dex-row`, `dex-row-sel`, `dex-row-hot` and `dex-panel` (a Pokedex row and the
species pane), `summary-card` (the Pokemon's card).

Some ids draw nothing. They ask the theme how the screen is arranged:

| Id | Answer |
|---|---|
| `dex-size` | The theme sets the Pokedex's frame (the whole screen). |
| `dex-flip` | 1: the list is at the right, as on the DS. |
| `summary-side` | 1: the Pokemon's card is at the right. |
| `options-arr` | The frame's width is the arrangement: 1 the DS's order (help, rows, pages at the foot), 2 the help as a pane at the right. |
| `card-arr` | 1: the Trainer Card's picture is at the left. |

A theme must answer every id it does not draw with 0. The PC changes its
own layout when a theme has `app_frame`: the party is a tray of icon cells and
the pane is wide.

## The two Black and White themes

Plasma and Harmonia share code:

| File | Contents |
|---|---|
| `engine/ui/theme_bw.h`, `theme_bw.c` | Shapes with smooth slanted edges (`rx_slab`), text helpers, the battle's HP plates, the party balls, the per-frame modes. |
| `engine/ui/theme_bw_kit.c` | The kit pieces for both themes. `style()` is 0 for Plasma and 1 for Harmonia. |
| `engine/ui/theme_plasma.c` | Plasma's layouts and grounds. |
| `engine/ui/theme_harmonia.c` | Harmonia's layouts and grounds. |

Four modes hold for one frame. A screen's ground turns a mode on. The mode
ends at the bottom bar and at the start of the next frame.

| Mode | Function | Effect |
|---|---|---|
| Paper | `rx_paper(1)` | Harmonia's summary, PC and save. The surfaces are the DS's light grey boxes, the chosen one is cyan, the letters are ink. |
| Tone | `rx_tone(pal)` | Harmonia's other screens. The parts take the six colours of the screen's DS palette. The chosen part is bright with a white line. |
| Cool | `rx_cool(1)` | Plasma's title menu. The accent is blue, not gold. |
| Accent | `rx_accent(rgb)` | The screen's own accent. In Plasma, each screen's accents take the hue of its ground (gold stays in the battle and the party). In Harmonia, the Pokedex and the Trainer Card take their DS accent (lime, grey). |

The gamepad's button pictures follow the theme too (`engine/ui/padglyph.c`):
Kosmos shows each controller's own colours, Plasma shows white buttons with
ink marks, Harmonia shows the DS's blue-grey buttons with white marks.

## Adding a theme

1. Make a `UiThemeOps` table with an `id` and a `name`.
2. Fill `color` and `text` first. Most pieces go through `panel`, so `panel`
   comes next.
3. Add layouts for the screens that need their own arrangement.
4. Register the table with `ui_theme_add`.

A mod can add a theme the same way.

## Tests

`EIDOS_UI_THEME=<id>` with the switches in `docs/test-switches.md` opens each
screen in a scripted run. Check a theme at 1280x720, 1920x1080 and 2560x1440:
the canvas is 640x360, 480x270 and 512x288 at these sizes, so a layout must
fit 480x270.
