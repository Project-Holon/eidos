/*
 * engine/ui/theme_bw.h -- the parts the Black-and-White-styled themes share
 * (Plasma, engine/ui/theme_plasma.c's band; Harmonia, engine/ui/theme_harmonia.c's tiles): the DS's
 * HP plates made smooth, the party's balls, an Ability's plate, the chosen
 * move's facts, and the text and shapes they are drawn with.
 *
 * The rules they keep: two surfaces (the plates' white strip and dark band),
 * one lean for every slanted edge (one pixel across for two down), drawn
 * smooth; a piece's colour on one small part of it.
 */
#ifndef EIDOS_UI_THEME_BW_H
#define EIDOS_UI_THEME_BW_H

#include "theme.h"

#define R(c) UI_RAW(c)

#define RX_OUTLINE  0x141414u
#define RX_PLATE_OL 0x202020u
#define RX_DARK_RIM 0x626262u
#define RX_WHITE    0xFFFFFFu
#define RX_WHITE_LO 0xDCDEE8u
#define RX_TEXT     0xF8F8F8u
#define RX_INK      0x202020u
#define RX_INK_SH   0xC4C4C4u
#define RX_MUTED    0xA8A8A8u
#define RX_ACCENT   0xFFAC00u   /* the plates' "Lv" */

#define FP 256                  /* edges are in 1/256 of a pixel            */
#define INSET 300               /* a layer inside a slanted edge: 1.17 px   */
#define LEAN(h) ((h) / 2)       /* the lean: one pixel across for two down  */

#define SX(v) ((v) * l->w / 640)
#define AT_LEAST(v, m) ((v) > (m) ? (v) : (m))

/* The plates: the name line, then the bar (the strip and the band; the
 * foe's band is a lip, yours holds the HP numbers and your party's balls),
 * yours also the experience line. */
#define NAME_H  16
#define STRIP_H 13
#define BAR_FOE 19
#define BAR_OWN 27
#define FOE_H   (NAME_H + BAR_FOE)
#define OWN_H   (NAME_H + BAR_OWN + 6)

/* The commands' colours, the DS's: FIGHT red, BAG gold, POKEMON green,
 * RUN blue (the game's order: FIGHT, BAG, POKEMON, RUN). */
extern const u32 rx_cmd_color[4];
/* The game's own icon for each command: the physical move's badge, the field
 * menu's Bag and Pokemon, the touch bar's return arrow. */
extern const int rx_cmd_icon[4];

u32  rx_mix(u32 a, u32 b, int t, int n);
/* Paper: a screen whose ground is the DS's light grey paper (the summary,
 * the PC).  Its ground turns it on; the hint bar, drawn last, turns it off.
 * On paper the dark surface is a grey box, the chosen one the DS's cyan
 * label, and labels are ink. */
void rx_paper(int on);
void rx_frame_begin(void);
/* A window's frame with cut corners, as a theme draws it layer by layer
 * (`in` pixels inside its outer box; the last layer is its body): noted so a
 * title's tab can keep inside the cut corner.  Until the next frame. */
void rx_frame_note(const EidosUiCtx *c, int x, int y, int cut, int in);
/* How many rows of its top left corner a title's piece whose corner pixel is
 * (x, y) must give up (a 45 degree cut) to keep as far from the noted
 * frame's diagonal as it is from the frame's straight sides.  0: none. */
int  rx_frame_corner(const EidosUiCtx *c, int x, int y);
/* Plasma's title cap stands this many pixels in from its bar's left end
 * (a screen whose own line runs along that end sets it).  Until the next
 * frame. */
void rx_head_pad(int px);
int  rx_head_pad_now(void);
int  rx_on_paper(void);
/* A tone: the six colours a DS ground gives the screen's parts (a box's
 * line, top and foot; the chosen one's line, top and foot), until the
 * bottom bar.  NULL: none. */
/* Cool: Plasma's accent is its blue instead of the plates' gold (the title
 * menu), until the bottom bar. */
#define RX_PLASMA_BLUE 0x38B0FFu
void rx_cool(int on);
int  rx_is_cool(void);
void rx_tone(const u32 *pal);
/* The screen's DS accent (0: none): Kosmos's accent colours for the
 * Pokedex and the Trainer Card become shades of it, until the bottom bar. */
void rx_accent(u32 rgb);
u32  rx_accent_now(void);
const u32 *rx_tone_now(void);
extern const u32 rx_tone_blue[6], rx_tone_red[6], rx_tone_green[6], rx_tone_crimson[6];
int  rx_otext(EidosUiCtx *c, int x, int y, const char *s, u32 col, u32 ol, int font);
int  rx_label(EidosUiCtx *c, int x, int y, const char *s, int white, u32 col, int font);
int  rx_type_word(EidosUiCtx *c, int x, int y, int type, int white);
void rx_slab(EidosUiCtx *c, int y, int H, int xl0, int xl1, int xr0, int xr1, int r0, int r1, int inl,
             int inr, u32 top, u32 bot, int a);
void rx_disc(EidosUiCtx *c, int cx, int cy, int r4, int y0, int y1, u32 col, int a);
/* One row from xl to xr (1/256 of a pixel), its ends blended. */
void rx_span(EidosUiCtx *c, int y, int xl, int xr, u32 col, int a);

/*
 * The Pokedex as the Gen 5 device, lying in landscape: the upper half (the
 * Poke Ball lid at its left end, the first screen) and the lower half (the
 * second screen), which slides out from under it to the right.
 * open: 0 closed .. 200 slid out .. 256 the screens on.
 */
typedef struct RxDex {
    int x0, top, bot, hh;       /* the upper half: its left, top, foot, height */
    int lid_w;                  /* the lid's zone, from x0                    */
    int ccx, cy, erx, ery;      /* the ball: its centre, its radius (erx = ery) */
    int sw;                     /* a screen's width                           */
    int u_end, l_end;           /* the halves' right ends                     */
    int slide, lit;             /* 0..256 each                                */
    EidosUiRect s1, s2;         /* the screens                                */
} RxDex;
void rx_dex_geom(const EidosUiCtx *c, int open, RxDex *g);
/* The Pokedex's counts for the lid: SEEN and OWN as the list counts them,
 * and the share owned by the game's completion rule (part of whole). */
void rx_dex_stats(int seen, int own, int part, int whole);
/* The lid, the Pokedex's gauge: a plate in `lid` with a curved edge toward
 * the screens, two slots at the device's end, a clasp in `body` on each long
 * edge.  On it the ball, facing the player, in a ring that fills with the
 * share owned; the ball is a Poke Ball, then a Great Ball (a third owned), an
 * Ultra Ball (two thirds), a Master Ball (complete).  SEEN over the ball and
 * OWN under it, each a word on the lid over a small screen with its number
 * (the screen from scr_a to scr_b, a line in scr_line, the DS's grid when
 * `grid`).  Unlit (the ball grey, the small screens dark) until the counts
 * are in and the screens are on.  cut_top, cut_foot: the shell's cut at its
 * left corners, which the plate keeps to. */
void rx_dex_lid(EidosUiCtx *c, const RxDex *g, u32 lid, u32 body, u32 ink, u32 scr_a, u32 scr_b, u32 scr_line,
                int grid, int cut_top, int cut_foot);
void rx_game_icon(EidosUiCtx *c, int cx, int cy, int which);

/* The theme's slots */
void rx_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp, int maxhp,
              int exp_num, int exp_den);
void rx_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge);
void rx_battle_balls(EidosUiCtx *c, int side, int x, int y, int right, const int *kinds, u32 t);
void rx_battle_ability(EidosUiCtx *c, const UiBattleLayout *l, int side, int tw, int th, u32 t, int leaving,
                       int *tx, int *ty);
void rx_move_info(EidosUiCtx *c, int x, int y, int w, const UiMoveInfo *m, int age);

/* The kit's pieces (engine/ui/theme_bw_kit.c): every screen's surfaces. */
u32  rx_color(u32 rgb);
void rx_text(u32 *rgb, s32 *shadow, u32 behind);
void rx_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, u32 fill, u32 edge, int a);
void rx_header(EidosUiCtx *c, int x, int y, int w, const char *title, const char *sub, const char *right,
               u32 accent);
void rx_row(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover, const UiTheme *t);
void rx_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label, const UiImage *icon, int selected,
                 int hover);
void rx_message(EidosUiCtx *c, int x, int y, int w, const char *utf8);
int  rx_tab(EidosUiCtx *c, int x, int y, const char *label, int selected, u32 accent, u32 accent2);
void rx_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name, int pp, int maxpp, int selected);
void rx_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected);
void rx_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den);
void rx_strip(EidosUiCtx *c, int y, int w, int h, int where, int hot);
void rx_clock_tab(EidosUiCtx *c, int cx, int w, int h, int hot);
/* The clock's tab as last drawn: its left and right edges (canvas x). */
extern int rx_clock_x0, rx_clock_x1;
/* Plasma's top bar: the alpha it was last painted at, and a stretch of its
 * foot rule left out for the next frames (a sheet that is the bar carried on
 * downward sets it each frame).  The stretch's two ends are in 1/256 of a
 * pixel at the bar's foot and lean as the buttons' rules do (half a pixel a
 * row, down to the right), so the rule is cut along them. */
extern int rx_bar_alpha, rx_bar_gap_x0, rx_bar_gap_x1, rx_bar_gap_ttl;
void rx_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown, int total, u32 accent);
void rx_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon);
void rx_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color);
int rx_keycap(EidosUiCtx *c, int x, int y, const char *label);
void rx_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected);
void rx_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent);
void rx_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void rx_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent);
void rx_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent);
void rx_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent);
int rx_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg, const char *const *choices, int n, int cur, u32 accent);
void rx_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void rx_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent);
int rx_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent);
void rx_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value, int num, int den, u32 accent);
void rx_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected);
void rx_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level, int hp, int maxhp, const UiImage *icon, int selected);
int rx_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name, int count, int selected);
void rx_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name, int state, int selected);
void rx_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value);
void rx_textbox(EidosUiCtx *c, int kind, int x, int y, int w, int h, u32 *fill, u32 *edge, u32 *ink, u32 *shadow);
void rx_choice_row(EidosUiCtx *c, int cstyle, int x, int y, int w, int h, const char *label, int lit);
int  rx_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n, const u32 *keys);

/* Where a layout puts the balls for these plates: yours on your first
 * plate's band (the plate at own_x, own_y), the foe's under its plates. */
void rx_place_balls(UiBattleLayout *l, int own_x, int own_y, int foe_x);

#endif /* EIDOS_UI_THEME_BW_H */
