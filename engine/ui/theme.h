/*
 * engine/ui/theme.h -- the one-screen interface's themes: code that draws
 * the kit's pieces and lays out the screens (docs/ui-themes.md).
 *
 * A theme is a table with one function per kit piece.  A slot left NULL
 * uses the default (Remaster, ui_def_* in kit.c); a theme that
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
 * battle fills it with Remaster's places, then lets the theme move them
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

typedef struct UiThemeOps {
    const char *id;         /* settings.ini's value, lower case, no spaces */
    const char *name;       /* shown in Settings                            */
    /* The colour map: every colour the kit and the screens draw with
     * (fills, lines, text and its shadow; not images) goes through it, so a
     * theme recolours every screen by mapping Remaster's colours (kit.h's
     * UI_*) to its own.  NULL: the colours as given. */
    u32 (*color)(u32 rgb);
    /* Text's colour and shadow (shadow < 0: none), given the colour already
     * on the canvas under the text (`behind`, final colours): a light theme
     * keeps white letters on a dark or saturated fill and turns them dark on
     * a light one.  NULL: both through `color`. */
    void (*text)(u32 *rgb, s32 *shadow, u32 behind);

    /* Battle screens (UiBattleLayout above).  NULL: Remaster's. */
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
    /* The chosen move's facts where the theme puts them (category 0
     * physical, 1 special, 2 status; power 0: none). */
    void (*battle_move_info)(EidosUiCtx *c, const UiBattleLayout *l, const char *name, int category,
                             int power, int accuracy);
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
