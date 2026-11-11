/*
 * engine/ui/theme.h -- the one-screen interface's themes: code that draws
 * the kit's pieces and lays out the screens (docs/ui-themes.md).
 *
 * A theme is a table with one function per kit piece.  A slot left NULL
 * uses the default (Kosmos, ui_def_* in kit.c); a theme that
 * fills a slot draws that piece its own way everywhere it appears, and
 * may call ui_def_* for the parts it keeps.  The pieces call each other
 * through ui_*, so a theme's panel is also the panel inside its dialogs.
 * Generated once from kit.h's prototypes (the slots keep their order).
 */
#ifndef EIDOS_UI_THEME_H
#define EIDOS_UI_THEME_H

#include "kit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Battle (engine/ui/recipe_battle.c): where the screen's parts go.  The
 * battle fills it with Kosmos's places, then lets the theme move them
 * (battle_layout); it draws and tests clicks at these places, so a theme's
 * layout takes the mouse and touch with it.  Canvas pixels.
 */
typedef struct UiBattleLayout {
    int w, h;                   /* the canvas                               */
    int n_own;                  /* the player's plates shown (1 to 3)       */
    EidosUiRect own[3];         /* the player's plates, top to bottom       */
    int foe_x, foe_y, foe_w, foe_step;   /* the foes' plates, a column      */
    EidosUiRect msg;            /* the message and the prompt               */
    EidosUiRect cmd[4];         /* FIGHT, BAG, POKEMON, RUN (the game's order) */
    EidosUiRect move[4];        /* the four moves                           */
    int info_x, info_y;         /* the chosen move's power and accuracy     */
    int own_balls_x, own_balls_y, own_balls_right;   /* the party ball bars */
    int foe_balls_x, foe_balls_y;
} UiBattleLayout;

/* What the battle shows (UiBattleLayout's users): 0 a message, 1 the
 * commands, 2 the moves. */
#define UI_BATTLE_MESSAGE  0
#define UI_BATTLE_COMMANDS 1
#define UI_BATTLE_MOVES    2

/*
 * The field's X menu (engine/ui/recipe_field.c): where its buttons and the
 * party stand.  The menu fills it with Kosmos's places (the party top left,
 * the buttons a column at the right), then the theme moves them; the keys
 * and the mouse follow the places.
 */
typedef struct UiFieldMenuLayout {
    int w, h;                   /* the canvas, the hint bar's top           */
    int n;                      /* the buttons shown                        */
    int slot[6];                /* each button's slot in the game's own 2 x 3 */
    EidosUiRect btn[6];
    int party_x, party_y, party_w;   /* the party: its header, then a row every 32 */
    int np;
} UiFieldMenuLayout;

/*
 * The title's menu (engine/ui/recipe_title.c): CONTINUE's card, the other
 * entries, the logo.  The theme is asked twice: first (phase 0) how the
 * menu is built, then (phase 1) with Kosmos's places filled in, to move
 * them.
 */
typedef struct UiTitleMenuLayout {
    int w, h;                   /* the canvas, the hint bar's top           */
    int n_rows, has_card;
    int stacked;                /* phase 0: one column, as the DS's         */
    int across;                 /* phase 0: the rows side by side (left and
                                   right move among them, up and down go
                                   between the card and the rows)           */
    int logo_w, logo_h;         /* the logo's size when shown               */
    EidosUiRect logo;           /* w 0: not shown                           */
    EidosUiRect card;
    EidosUiRect row[8];
    const char *label[8];       /* the rows' names (English; ui_tr gives the
                                   shown one), for a theme that sizes its
                                   rows by them; NULL: not given            */
} UiTitleMenuLayout;

/*
 * The party screen (engine/ui/recipe_party.c).  The screen fills the layout
 * with Kosmos's places (a window in the middle: six rows in two columns, the
 * prompt under them, the menu at its lower right), then the theme moves
 * them; the mouse follows.
 */
typedef struct UiPartyLayout {
    int w, h;                   /* the canvas, the hint bar's top            */
    int menu_n;                 /* the open menu's entries, 0: none          */
    int sel;                    /* the chosen slot, -1: none                 */
    int no_exit;                /* the theme shows no EXIT button            */
    EidosUiRect frame;          /* the window (a click outside it is B)      */
    int header;                 /* 1: Kosmos's window and header are drawn   */
    EidosUiRect slot[6];
    int column;                 /* 1: the six stand in one column (up and
                                   down go through them in order)            */
    EidosUiRect msg;            /* the prompt, a message                     */
    EidosUiRect menu;           /* the menu's entries, stacked from its foot */
    int menu_eh;                /* a stacked entry's height (Kosmos: 17)     */
    int menu_across;            /* 1: side by side in `menu` instead         */
    int menu_cols;              /* > 0: a grid in `menu`, this many across
                                   (the chosen plate turned over)            */
    int menu_apart;             /* 1: the menu does not shorten the prompt   */
    EidosUiRect detail;         /* w 0: none.  The chosen Pokemon's pane     */
} UiPartyLayout;

/* One Pokemon of the party for a theme's plate (name NULL: an empty slot). */
typedef struct UiPartyMon {
    int slot;
    const char *name;
    int gender, level;          /* level 0: an Egg                           */
    int hp, maxhp;
    int status;                 /* kit's status badge, -1: none              */
    int item;                   /* what it holds: 0 nothing, 1 an item, 2 a letter */
    int sel;                    /* chosen                                    */
    int moving;                 /* the one being moved (SWITCH)              */
    int turned;                 /* frames since it turned over for the menu
                                   (0: its face shows)                       */
    const char *note;           /* the game's label in place of the HP (ABLE,
                                   NOT ABLE, an entry's order), or NULL      */
    u32 note_rgb;
    const UiImage *icon;
    const UiImage *front;       /* its picture, or NULL                      */
    int species, form;          /* for ui_mon_front_draw (its idle loop)     */
    int type1, type2;
} UiPartyMon;

/*
 * The Bag (engine/ui/recipe_bag.c): the pockets, the list's six rows, the
 * chosen item's pane.  A theme that sets bag_layout places them all (the
 * screen draws no window or header of its own then).
 */
typedef struct UiBagLayout {
    int w, h;                   /* the canvas, the hint bar's top            */
    EidosUiRect frame;          /* a click outside it is B                   */
    EidosUiRect pocket[5];
    EidosUiRect list;           /* the rows from its top, 2 apart            */
    int rowh;
    EidosUiRect detail;         /* the item's picture, name and description  */
} UiBagLayout;

/* The chosen move, for battle_move_info. */
typedef struct UiMoveInfo {
    const char *name;
    const char *desc;           /* the game's description, on one line     */
    int type;
    int category;               /* 0 physical, 1 special, 2 status         */
    int power, accuracy;        /* 0: none                                 */
    int pp, maxpp;
} UiMoveInfo;

typedef struct UiThemeOps {
    const char *id;         /* settings.ini's value, lower case, no spaces */
    const char *name;       /* shown in Settings                            */
    /* The colour map: every colour the kit and the screens draw with
     * (fills, lines, text and its shadow; not images) goes through it, so a
     * theme recolours every screen by mapping Kosmos's colours (kit.h's
     * UI_*) to its own.  NULL: the colours as given. */
    u32 (*color)(u32 rgb);
    /* Called when a frame's drawing begins (a theme's own per-frame state). */
    void (*frame_begin)(void);
    /* Text's colour and shadow (shadow < 0: none), given the colour already
     * on the canvas under the text (`behind`, final colours): a light theme
     * keeps white letters on a dark or saturated fill and turns them dark on
     * a light one.  NULL: both through `color`. */
    void (*text)(u32 *rgb, s32 *shadow, u32 behind);

    /* A text box's frame (kind: textbox.h's UI_TEXTBOX_*, plus 16 when it
     * holds a message and is not a list's or a screen's box) and the colours of
     * what stands on it: the fill and the edge (a balloon's tail takes
     * them), the letters and their shadow.  NULL: Kosmos's. */
    void (*textbox)(EidosUiCtx *c, int kind, int x, int y, int w, int h, u32 *fill, u32 *edge, u32 *ink,
                    u32 *shadow);
    /* One answer of a choice list (style: choice_list.h's UI_CHOICE_*), its
     * box being the text box's of that style.  NULL: Kosmos's. */
    void (*choice_row)(EidosUiCtx *c, int style, int x, int y, int w, int h, const char *label, int lit);
    /* A bar's ground across the screen: the field's top bar (where 0, h
     * rows from y, `hot` under the pointer) or a bottom bar of key hints
     * (where 1).  NULL: Kosmos's. */
    void (*strip)(EidosUiCtx *c, int y, int w, int h, int where, int hot);
    /* The clock's tab in the middle of the top bar (cx its middle). */
    void (*clock_tab)(EidosUiCtx *c, int cx, int w, int h, int hot);
    /* The field's X menu: its places, and one button (state 0 plain, 2
     * chosen; icon may be NULL).  NULL: Kosmos's. */
    void (*field_menu_layout)(UiFieldMenuLayout *l);
    /* Drawn under the menu's parts (a band, a strip). */
    void (*field_menu_backdrop)(EidosUiCtx *c, const UiFieldMenuLayout *l);
    void (*field_menu_button)(EidosUiCtx *c, int x, int y, int w, int h, const char *label, const UiImage *icon,
                              int state);
    /* The title's menu (UiTitleMenuLayout above), what is drawn under it,
     * and one of its rows.  NULL: Kosmos's. */
    void (*title_menu_layout)(UiTitleMenuLayout *l, int phase);
    void (*title_menu_backdrop)(EidosUiCtx *c, const UiTitleMenuLayout *l);
    void (*title_menu_row)(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int selected, int hover);
    /* The party screen (UiPartyLayout above): its places; what stands under
     * them (the title and the count at its right); one plate; the chosen
     * Pokemon's pane; the prompt; one entry of the menu.  NULL: Kosmos's. */
    void (*party_layout)(UiPartyLayout *l);
    void (*party_backdrop)(EidosUiCtx *c, const UiPartyLayout *l, const char *title, const char *sub,
                           const char *right);
    void (*party_plate)(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m);
    void (*party_detail)(EidosUiCtx *c, const EidosUiRect *r, const UiPartyMon *m, u32 frame);
    void (*party_prompt)(EidosUiCtx *c, const EidosUiRect *r, const char *text);
    void (*party_menu_entry)(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int sel, int across);
    /* The Bag (UiBagLayout above): its places; its ground (the pocket's
     * name); one pocket (count 0: none shown); one row (name NULL: an
     * empty place); the frame of the item's pane.  NULL: Kosmos's. */
    void (*bag_layout)(UiBagLayout *l);
    void (*bag_backdrop)(EidosUiCtx *c, const UiBagLayout *l, const char *pocket);
    void (*bag_pocket)(EidosUiCtx *c, const EidosUiRect *r, int which, const char *name, int count, int sel,
                       int hover);
    void (*bag_row)(EidosUiCtx *c, int x, int y, int w, int h, const UiImage *icon, const char *name, int count,
                    int sel, int hover);
    void (*bag_detail_frame)(EidosUiCtx *c, const EidosUiRect *r, int shown);
    /* A screen's window (app: "summary"): a theme that returns 1 drew its
     * own ground and set the frame the screen lays itself out in; 0 (or
     * NULL): Kosmos's window. */
    int (*app_frame)(EidosUiCtx *c, const char *app, EidosUiRect *frame);
    /* Battle screens (UiBattleLayout above).  NULL: Kosmos's. */
    void (*battle_layout)(UiBattleLayout *l, int what);
    /* Drawn under the battle's parts (a dock, a band). */
    void (*battle_backdrop)(EidosUiCtx *c, const UiBattleLayout *l, int what);
    /* The box behind the message or the prompt, at alpha a (0 to 255). */
    void (*battle_box)(EidosUiCtx *c, int x, int y, int w, int h, int a);
    /* One command (0 FIGHT, 1 BAG, 2 POKEMON, 3 RUN) in its colour. */
    void (*battle_command)(EidosUiCtx *c, int x, int y, int w, int h, int which,
                           const char *label, u32 color, int sel, int enabled);
    /* One move: its type, name and PP. */
    void (*battle_move)(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name,
                        int pp, int maxpp, int sel, int enabled);
    /* The chosen move's facts where the theme puts them. */
    void (*battle_move_info)(EidosUiCtx *c, const UiBattleLayout *l, const UiMoveInfo *m);
    /* A side's six party balls (side 0 yours, 1 the foe's; kinds as the
     * game's: 0 no Pokemon, 1 ready, 2 fainted, 3 a status problem) at the
     * layout's place for them (x, y; `right`: they end at x), t frames
     * after they first showed. */
    void (*battle_balls)(EidosUiCtx *c, int side, int x, int y, int right, const int *kinds, u32 t);
    /* The plate behind an Ability's words (tw x th pixels) for a side: t
     * frames after it came, or (leaving) after it went.  Sets where the
     * words go. */
    void (*battle_ability)(EidosUiCtx *c, const UiBattleLayout *l, int side, int tw, int th, u32 t,
                           int leaving, int *tx, int *ty);
    void (*panel)(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, u32 fill, u32 edge, int a);
    void (*header)(EidosUiCtx *c, int x, int y, int w, const char *title, const char *sub, const char *right, u32 accent);
    int (*hints)(EidosUiCtx *c, int y, int w, const char *const *pairs, int n, const u32 *keys);
    void (*bar)(EidosUiCtx *c, int x, int y, int w, int h, int num, int den);
    void (*type_tag)(EidosUiCtx *c, int x, int y, int w, int type);
    void (*status_badge)(EidosUiCtx *c, int x, int y, int status);
    void (*scrollbar)(EidosUiCtx *c, int x, int y, int h, int first, int shown, int total, u32 accent);
    void (*row)(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover, const UiTheme *t);
    void (*message)(EidosUiCtx *c, int x, int y, int w, const char *utf8);
    void (*toast)(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon);
    void (*cursor_corners)(EidosUiCtx *c, int x, int y, int w, int h, u32 color);
    int (*keycap)(EidosUiCtx *c, int x, int y, const char *label);
    int (*tab)(EidosUiCtx *c, int x, int y, const char *label, int selected, u32 accent, u32 accent2);
    void (*pocket_tab)(EidosUiCtx *c, int x, int y, int w, const char *label, int selected);
    void (*toggle)(EidosUiCtx *c, int x, int y, int on, u32 accent);
    void (*slider)(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
    void (*stepper)(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent);
    void (*field)(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent);
    void (*key)(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent);
    int (*dialog)(EidosUiCtx *c, int x, int y, int w, const char *msg, const char *const *choices, int n, int cur, u32 accent);
    void (*progress)(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
    void (*spinner)(EidosUiCtx *c, int x, int y, int frame, u32 accent);
    void (*page_dots)(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent);
    void (*target_chevron)(EidosUiCtx *c, int x, int y, u32 color);
    void (*party_balls)(EidosUiCtx *c, int x, int y, const u8 *states, int n);
    int (*name_tag)(EidosUiCtx *c, int x, int y, const char *name, u32 accent);
    void (*category_icon)(EidosUiCtx *c, int x, int y, int kind);
    void (*stat_row)(EidosUiCtx *c, int x, int y, int w, const char *name, int value, int num, int den, u32 accent);
    void (*icon_well)(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected);
    void (*gender)(EidosUiCtx *c, int x, int y, int gender, int font);
    void (*menu_row)(EidosUiCtx *c, int x, int y, int w, const char *label, const UiImage *icon, int selected, int hover);
    void (*party_row)(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level, int hp, int maxhp, const UiImage *icon, int selected);
    void (*plate)(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp, int maxhp, int exp_num, int exp_den);
    void (*plate_status)(EidosUiCtx *c, int x, int y, int w, int level, int badge);
    void (*cmd_button)(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected);
    void (*move_row)(EidosUiCtx *c, int x, int y, int w, int type, const char *name, int pp, int maxpp, int selected);
    int (*item_row)(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name, int count, int selected);
    void (*dex_row)(EidosUiCtx *c, int x, int y, int w, int number, const char *name, int state, int selected);
    void (*info_row)(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value);
} UiThemeOps;

/* The default pieces (eidos's look); a theme may call them. */
void ui_def_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, u32 fill, u32 edge, int a);
void ui_def_header(EidosUiCtx *c, int x, int y, int w, const char *title, const char *sub, const char *right, u32 accent);
int ui_def_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n, const u32 *keys);
void ui_def_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den);
void ui_def_type_tag(EidosUiCtx *c, int x, int y, int w, int type);
void ui_def_status_badge(EidosUiCtx *c, int x, int y, int status);
void ui_def_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown, int total, u32 accent);
void ui_def_row(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover, const UiTheme *t);
void ui_def_message(EidosUiCtx *c, int x, int y, int w, const char *utf8);
void ui_def_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon);
void ui_def_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color);
int ui_def_keycap(EidosUiCtx *c, int x, int y, const char *label);
int ui_def_tab(EidosUiCtx *c, int x, int y, const char *label, int selected, u32 accent, u32 accent2);
void ui_def_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected);
void ui_def_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent);
void ui_def_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void ui_def_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent);
void ui_def_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent);
void ui_def_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent);
int ui_def_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg, const char *const *choices, int n, int cur, u32 accent);
void ui_def_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent);
void ui_def_spinner(EidosUiCtx *c, int x, int y, int frame, u32 accent);
void ui_def_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent);
void ui_def_target_chevron(EidosUiCtx *c, int x, int y, u32 color);
void ui_def_party_balls(EidosUiCtx *c, int x, int y, const u8 *states, int n);
int ui_def_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent);
void ui_def_category_icon(EidosUiCtx *c, int x, int y, int kind);
void ui_def_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value, int num, int den, u32 accent);
void ui_def_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected);
void ui_def_gender(EidosUiCtx *c, int x, int y, int gender, int font);
void ui_def_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label, const UiImage *icon, int selected, int hover);
void ui_def_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level, int hp, int maxhp, const UiImage *icon, int selected);
void ui_def_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp, int maxhp, int exp_num, int exp_den);
void ui_def_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge);
void ui_def_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected);
void ui_def_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name, int pp, int maxpp, int selected);
int ui_def_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name, int count, int selected);
void ui_def_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name, int state, int selected);
void ui_def_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value);

/* The themes: the built-in ones, then those mods add (ui_theme_add).
 * Index 0 is the default. */
int ui_theme_count(void);
/* A colour through the active theme's map (UI_NONE stays UI_NONE).  A
 * colour marked UI_RAW is the theme's own already and passes unmapped. */
u32 ui_theme_color(u32 rgb);
#define UI_RAW(rgb) ((u32)(rgb) | 0x01000000u)
/* Text's colour and shadow through the theme (theme.h's text slot). */
void ui_theme_text(u32 *rgb, s32 *shadow, u32 behind);
const UiThemeOps *ui_theme_at(int i);
/* Adds a theme (a mod's, or a built-in one); 0 when added. */
int ui_theme_add(const UiThemeOps *t);
/* The theme in use; selecting an unknown id selects the default. */
const UiThemeOps *ui_theme_active(void);
void ui_theme_select(const char *id);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_UI_THEME_H */
