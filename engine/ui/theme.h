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

typedef struct UiThemeOps {
    const char *id;         /* settings.ini's value, lower case, no spaces */
    const char *name;       /* shown in Settings                            */
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
