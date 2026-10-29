/*
 * engine/ui/kit.h -- the interface kit: the pieces the second screen used
 * to hold, drawn at the DS pixel size into the canvas (docs/ui-kit.md).
 *
 * The rules (docs/ui-kit.md, "Rules"): the game's colours, the game's font
 * and icons, new shapes; drawn by code, nothing shipped; one canvas pixel
 * is one DS pixel; layouts relative to the canvas.
 *
 * Everything draws into an EidosUiCtx: the canvas plus a clip rectangle and
 * an origin, so a piece is drawn at (0, 0) of its own box and the caller
 * places the box.  Colours are 0xRRGGBB; alpha is a separate 0..255.
 *
 * Game data
 * ---------
 *
 * The font (archive a/0/2/3, NFTR), the item icons, the Pokemon icons, the
 * overworld sprites and the trainer sprites are read from the player's
 * extracted data through the port's file system (sdk/vfs.h) the first time
 * a piece needs them, and kept.  A piece whose data is missing draws
 * nothing for that part (the layout does not move).
 */

#ifndef EIDOS_UI_KIT_H
#define EIDOS_UI_KIT_H

#include "types.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Colours (docs/ui-kit.md, "Colours"; every one sampled from the game) */
/* ------------------------------------------------------------------ */

#define UI_INK        0x000000u
#define UI_BODY       0x101010u
#define UI_BODY2      0x181818u
#define UI_EDGE       0x393939u
#define UI_EDGE2      0x525252u
#define UI_GREY       0x9C9C9Cu
#define UI_LIGHT      0xD5D5D5u
#define UI_WHITE      0xFFFFFFu
#define UI_CYAN       0x00D5C5u
#define UI_CYAN2      0x008BACu
#define UI_CYAN_EDGE  0x4A94FFu
#define UI_NAVY       0x08294Au
#define UI_NAVY2      0x084A73u
#define UI_SKY        0x08B4FFu
#define UI_LIME       0x94EE18u
#define UI_HP_G       0x62FF62u
#define UI_HP_Y       0xFFDE31u
#define UI_HP_R       0xFF5231u
#define UI_RED        0x8B1029u
#define UI_RED2       0x5A0820u
#define UI_PINK       0xEE527Bu
#define UI_PINK2      0xCD395Au
#define UI_DEX        0xE61E3Cu
#define UI_DEX2       0x780E20u
#define UI_DEX3       0x3C0812u
#define UI_CURSOR     0xFF107Bu
#define UI_FIGHT      0xDE184Au
#define UI_MON        0x63DE31u
#define UI_BAGY       0xFFBD29u
#define UI_RUN        0x31B4FFu
#define UI_CARD       0x39FFBDu
#define UI_CARD_L     0x9CFFD5u
#define UI_CARD_D     0x00624Au

/* "No colour" for an argument that takes a colour (a fill or an outline). */
#define UI_NONE       0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

struct EidosUiCtx {
    EidosUiCanvas *canvas;
    int ox, oy;             /* origin: (0, 0) of the piece being drawn    */
    EidosUiRect clip;       /* canvas pixels                              */
};

/* Start a frame: size the canvas to w x h at scale k and clear it. */
void ui_begin(EidosUiCtx *c, EidosUiCanvas *canvas, int w, int h, int k);
/* Move the origin by (dx, dy), saving the clip into *saved_clip (may be
 * NULL).  ui_pop moves the origin back and restores the saved clip.  Between
 * them ui_clip narrows the clip to a box in the current origin's pixels. */
void ui_push(EidosUiCtx *c, int dx, int dy, EidosUiRect *saved_clip);
void ui_pop(EidosUiCtx *c, int dx, int dy, const EidosUiRect *saved_clip);
void ui_clip(EidosUiCtx *c, int x, int y, int w, int h);

/* Plain drawing, in the current origin's pixels. */
void ui_fill(EidosUiCtx *c, int x, int y, int w, int h, u32 rgb, int a);
void ui_line(EidosUiCtx *c, int x0, int y0, int x1, int y1, u32 rgb, int a);
/* fill and outline may be UI_NONE; the ellipse fills the box (x, y, w, h). */
void ui_ellipse(EidosUiCtx *c, int x, int y, int w, int h, u32 fill, u32 outline, int a);

/* ------------------------------------------------------------------ */
/* Text: the game's font                                               */
/* ------------------------------------------------------------------ */

#define UI_FONT_LARGE  0    /* NFTR 0, 12x15: labels and names            */
#define UI_FONT_SMALL  1    /* NFTR 1, 10x10: small labels, hints         */
#define UI_FONT_MEDIUM 2    /* NFTR 2, 11x13: descriptions                */

/* Draw UTF-8 text; value 1 of a glyph is the letter, 2 its shadow
 * (shadow < 0: none).  k is a whole-number size multiple (titles).
 * Returns the x after the text. */
int  ui_text(EidosUiCtx *c, int x, int y, const char *utf8, u32 color,
             s32 shadow, int font, int k);
int  ui_text_w(const char *utf8, int font);
/* The y that centres capital letters in a box of height h. */
int  ui_text_y(int h, int font);
/* Word-wrap utf8 to width; calls back each line.  Returns the line count. */
int  ui_wrap(const char *utf8, int width, int font,
             void (*line)(void *user, const char *start, int len), void *user);

/* ------------------------------------------------------------------ */
/* Images: the game's icons and sprites                                */
/* ------------------------------------------------------------------ */

typedef struct UiImage {
    int w, h;
    const u8 *rgba;         /* owned by the kit                           */
} UiImage;

void ui_image(EidosUiCtx *c, int x, int y, const UiImage *im, int k);

/* Key names for hints: the DS's names ("A", "START", "L R") as the
 * player's device calls them (set every frame by the interface layer:
 * names[i] for BW_KEY bit i, NULL keeps the DS name). ui_keycap shows the
 * translated name; ui_keycap_w is its width. */
void ui_set_key_names(const char *const names[12]);
/* A key name may name a gamepad control instead: UI_PAD_TOKEN, then 'A'
 * plus the control (engine/ui/padglyph.h); the key cap is then that
 * control's glyph in the family set here (UI_PAD_*). */
#define UI_PAD_TOKEN '\x1F'
void ui_set_pad_family(int family);
int  ui_pad_family(void);
const char *ui_key_label(const char *ds);
int  ui_keycap_w(const char *label);

/* A rectangle (sx, sy, w, h) of a DS screen as the game composed it
 * (EidosUiFrame.screen_rgba), drawn at (x, y) scaled by k.  key non-zero
 * leaves out the backdrop: the colour at the rectangle's top-left corner
 * wherever it reaches the rectangle's edge.  For a whole pane the game
 * draws (a map, a form list); icons and sprites come from the game's data
 * (ui_mon_front_anim, ui_item_icon) or its OBJ layer (eidos_ui_want_objs). */
void ui_blit_screen(EidosUiCtx *c, int x, int y, const u8 *screen,
                    int sx, int sy, int w, int h, int k, int key);

/* The game's own images, decoded on first use; NULL when unavailable. */
const UiImage *ui_item_icon(u16 item);
const UiImage *ui_mon_icon(u16 species, u8 form, int frame);
const UiImage *ui_overworld(u16 sprite, int dir, int frame);  /* walk frames */
const UiImage *ui_trainer_front(u16 trainer_class);
const UiImage *ui_mon_front(u16 species, u8 form, int shiny);
/* The front as the game animates it: the idle loop at `t` frames, and
 * where the rig's origin (the point its frames line up on) falls in the
 * image.  The image lasts until the next call. */
const UiImage *ui_mon_front_anim(u16 species, int shiny, u32 t, int *ox, int *oy);
/* That loop drawn in a box: the largest whole scale that fits, standing on
 * the box's bottom, centred. */
void ui_mon_front_draw(EidosUiCtx *c, u16 species, int shiny, u32 t, int x, int y, int w, int h);

/* ------------------------------------------------------------------ */
/* Pieces (docs/ui-kit.md, "Pieces")                                   */
/* ------------------------------------------------------------------ */

#define UI_CUT_TL 1
#define UI_CUT_TR 2
#define UI_CUT_BR 4
#define UI_CUT_BL 8

typedef struct UiTheme {    /* one app's colours                          */
    u32 accent;             /* rules, selected outline                    */
    u32 accent2;            /* selected fill                              */
    u32 fill;               /* panel fill                                 */
    u32 edge;               /* panel edge                                 */
} UiTheme;

extern const UiTheme UI_THEME_MENU, UI_THEME_PARTY, UI_THEME_BAG, UI_THEME_DEX,
                     UI_THEME_CARD, UI_THEME_BATTLE, UI_THEME_OPTIONS;

void ui_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner,
              u32 fill, u32 edge, int a);
void ui_header(EidosUiCtx *c, int x, int y, int w, const char *title,
               const char *sub, const char *right, u32 accent);
/* The key hints bar along the bottom; each pair is (key, action); returns
 * the bar's height. Each hint is also a hit region that presses its key. */
int  ui_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n,
              const u32 *keys);
void ui_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den);
void ui_type_tag(EidosUiCtx *c, int x, int y, int w, int type);
void ui_status_badge(EidosUiCtx *c, int x, int y, int status);
void ui_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown,
                  int total, u32 accent);
void ui_row(EidosUiCtx *c, int x, int y, int w, int h, int selected,
            int hover, const UiTheme *t);
void ui_message(EidosUiCtx *c, int x, int y, int w, const char *utf8);
void ui_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon);
void ui_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color);

/* ------------------------------------------------------------------ */
/* More pieces (docs/ui-kit.md, "Pieces")                              */
/* ------------------------------------------------------------------ */

/* Type numbers are the game's: 0 NORMAL, 1 FIGHTING, 2 FLYING, 3 POISON,
 * 4 GROUND, 5 ROCK, 6 BUG, 7 GHOST, 8 STEEL, 9 ???, 10 FIRE, 11 WATER,
 * 12 GRASS, 13 ELECTRIC, 14 PSYCHIC, 15 ICE, 16 DRAGON, 17 DARK.  Status
 * numbers: 0 PSN, 1 PAR, 2 BRN, 3 SLP, 4 FRZ, 5 FNT, 6 PKRS.  Gender: 0
 * male, 1 female, 2 none. */
/* Types are the game's own numbers: NORMAL 0 .. STEEL 8, FIRE 9, WATER 10,
 * GRASS 11, ELECTRIC 12, PSYCHIC 13, ICE 14, DRAGON 15, DARK 16; the
 * unknown mark (an unseen Pokemon) is UI_TYPE_UNKNOWN. */
#define UI_TYPE_UNKNOWN 17
const char *ui_type_name(int type);
/* The spelling later games give a move whose Black and White name was
 * squeezed into 12 letters ("Dragon Breath"), or NULL. */
const char *ui_move_name_modern(int id);
u32  ui_type_color(int type);
/* recipe_dex.c: a species' name from the game's text (> 0 when found) and
 * its two types (the same number twice for one type). */
int  ui_species_name(int species, char *out, int cap);
void ui_species_types(int species, int *t1, int *t2);

/* Each returns the width it drew, where that depends on the text. */
int  ui_keycap(EidosUiCtx *c, int x, int y, const char *label);
int  ui_tab(EidosUiCtx *c, int x, int y, const char *label, int selected,
            u32 accent, u32 accent2);
void ui_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected);
void ui_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent);
void ui_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void ui_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent);
void ui_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent);
void ui_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent);
/* Question box: text, then the choices at the bottom right; returns the height. */
int  ui_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg,
               const char *const *choices, int n, int cur, u32 accent);
void ui_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void ui_spinner(EidosUiCtx *c, int x, int y, int frame, u32 accent);
void ui_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent);
void ui_target_chevron(EidosUiCtx *c, int x, int y, u32 color);
/* states: one byte per slot, 0 ok, 1 status, 2 fainted, 3 empty. */
void ui_party_balls(EidosUiCtx *c, int x, int y, const u8 *states, int n);
int  ui_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent);
void ui_category_icon(EidosUiCtx *c, int x, int y, int kind);   /* 0 physical, 1 special, 2 status */
void ui_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value,
                 int num, int den, u32 accent);
void ui_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected);
void ui_gender(EidosUiCtx *c, int x, int y, int gender, int font);

/* Rows with content.  Heights: menu row 26, party row 30, foe plate 24,
 * player plate 36, move row 22, info row 15, pocket tab 18, dex row 18; an
 * item row is max(20, icon height + 6). */
void ui_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label,
                 const UiImage *icon, int selected, int hover);
/* name NULL: an empty slot. */
void ui_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name,
                  int gender, int level, int hp, int maxhp, const UiImage *icon,
                  int selected);
/* A battle plate; exp_den > 0 makes it the player's (HP numbers and the
 * experience line). */
void ui_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender,
              int level, int hp, int maxhp, int exp_num, int exp_den);
void ui_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label,
                   u32 color, int selected);
/* name NULL: an empty slot. */
void ui_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name,
                 int pp, int maxpp, int selected);
/* Returns the row's height. */
int  ui_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon,
                 const char *name, int count, int selected);
/* state: 0 unknown, 1 seen, 2 caught; name may be NULL. */
void ui_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name,
                int state, int selected);
void ui_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_UI_KIT_H */
