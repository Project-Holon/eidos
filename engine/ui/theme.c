/*
 * engine/ui/theme.c -- the theme in use and the kit's dispatch: each ui_*
 * piece calls the active theme's function, or the default (theme.h).
 */
#include <string.h>

#include "theme.h"

#define UI_THEME_MAX 32

static const UiThemeOps k_default = { .id = "remaster", .name = "Remaster" };   /* every piece the default */

static const UiThemeOps *g_list[UI_THEME_MAX] = { &k_default };
static int g_count = 1;
static const UiThemeOps *g_active = &k_default;

int ui_theme_count(void)
{
    return g_count;
}

const UiThemeOps *ui_theme_at(int i)
{
    return i >= 0 && i < g_count ? g_list[i] : NULL;
}

int ui_theme_add(const UiThemeOps *t)
{
    int i;

    if (t == NULL || t->id == NULL || g_count >= UI_THEME_MAX) {
        return -1;
    }
    for (i = 0; i < g_count; i++) {
        if (strcmp(g_list[i]->id, t->id) == 0) {
            return -1;              /* one theme per id: the first stays */
        }
    }
    g_list[g_count++] = t;
    return 0;
}

u32 ui_theme_color(u32 rgb)
{
    if (rgb == UI_NONE) {
        return rgb;
    }
    if ((rgb & 0xFF000000u) != 0u) {
        return rgb & 0x00FFFFFFu;   /* UI_RAW: the theme's own colour */
    }
    return g_active->color != NULL ? g_active->color(rgb) : rgb;
}

void ui_theme_text(u32 *rgb, s32 *shadow, u32 behind)
{
    if ((*rgb & 0xFF000000u) != 0u || g_active->text == NULL) {
        *rgb = ui_theme_color(*rgb);
        if (*shadow >= 0) {
            *shadow = (s32)ui_theme_color((u32)*shadow);
        }
        return;
    }
    g_active->text(rgb, shadow, behind);
}

const UiThemeOps *ui_theme_active(void)
{
    return g_active;
}

void ui_theme_select(const char *id)
{
    int i;

    g_active = &k_default;
    for (i = 0; id != NULL && i < g_count; i++) {
        if (strcmp(g_list[i]->id, id) == 0) {
            g_active = g_list[i];
        }
    }
}

/* The dispatch, one per piece. */
void ui_panel(EidosUiCtx *c, int x, int y, int w, int h, int cut, int corner, u32 fill, u32 edge, int a)
{
    (g_active->panel != NULL ? g_active->panel : ui_def_panel)(c, x, y, w, h, cut, corner, fill, edge, a);
}

void ui_header(EidosUiCtx *c, int x, int y, int w, const char *title, const char *sub, const char *right, u32 accent)
{
    (g_active->header != NULL ? g_active->header : ui_def_header)(c, x, y, w, title, sub, right, accent);
}

int ui_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, int n, const u32 *keys)
{
    return (g_active->hints != NULL ? g_active->hints : ui_def_hints)(c, y, w, pairs, n, keys);
}

void ui_bar(EidosUiCtx *c, int x, int y, int w, int h, int num, int den)
{
    (g_active->bar != NULL ? g_active->bar : ui_def_bar)(c, x, y, w, h, num, den);
}

void ui_type_tag(EidosUiCtx *c, int x, int y, int w, int type)
{
    (g_active->type_tag != NULL ? g_active->type_tag : ui_def_type_tag)(c, x, y, w, type);
}

void ui_status_badge(EidosUiCtx *c, int x, int y, int status)
{
    (g_active->status_badge != NULL ? g_active->status_badge : ui_def_status_badge)(c, x, y, status);
}

void ui_scrollbar(EidosUiCtx *c, int x, int y, int h, int first, int shown, int total, u32 accent)
{
    (g_active->scrollbar != NULL ? g_active->scrollbar : ui_def_scrollbar)(c, x, y, h, first, shown, total, accent);
}

void ui_row(EidosUiCtx *c, int x, int y, int w, int h, int selected, int hover, const UiTheme *t)
{
    (g_active->row != NULL ? g_active->row : ui_def_row)(c, x, y, w, h, selected, hover, t);
}

void ui_message(EidosUiCtx *c, int x, int y, int w, const char *utf8)
{
    (g_active->message != NULL ? g_active->message : ui_def_message)(c, x, y, w, utf8);
}

void ui_toast(EidosUiCtx *c, int x, int y, const char *utf8, const UiImage *icon)
{
    (g_active->toast != NULL ? g_active->toast : ui_def_toast)(c, x, y, utf8, icon);
}

void ui_cursor_corners(EidosUiCtx *c, int x, int y, int w, int h, u32 color)
{
    (g_active->cursor_corners != NULL ? g_active->cursor_corners : ui_def_cursor_corners)(c, x, y, w, h, color);
}

int ui_keycap(EidosUiCtx *c, int x, int y, const char *label)
{
    return (g_active->keycap != NULL ? g_active->keycap : ui_def_keycap)(c, x, y, label);
}

int ui_tab(EidosUiCtx *c, int x, int y, const char *label, int selected, u32 accent, u32 accent2)
{
    return (g_active->tab != NULL ? g_active->tab : ui_def_tab)(c, x, y, label, selected, accent, accent2);
}

void ui_pocket_tab(EidosUiCtx *c, int x, int y, int w, const char *label, int selected)
{
    (g_active->pocket_tab != NULL ? g_active->pocket_tab : ui_def_pocket_tab)(c, x, y, w, label, selected);
}

void ui_toggle(EidosUiCtx *c, int x, int y, int on, u32 accent)
{
    (g_active->toggle != NULL ? g_active->toggle : ui_def_toggle)(c, x, y, on, accent);
}

void ui_slider(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    (g_active->slider != NULL ? g_active->slider : ui_def_slider)(c, x, y, w, num, den, accent);
}

void ui_stepper(EidosUiCtx *c, int x, int y, int w, const char *value, u32 accent)
{
    (g_active->stepper != NULL ? g_active->stepper : ui_def_stepper)(c, x, y, w, value, accent);
}

void ui_field(EidosUiCtx *c, int x, int y, int w, const char *text, int room, u32 accent)
{
    (g_active->field != NULL ? g_active->field : ui_def_field)(c, x, y, w, text, room, accent);
}

void ui_key(EidosUiCtx *c, int x, int y, int w, const char *label, int selected, u32 accent)
{
    (g_active->key != NULL ? g_active->key : ui_def_key)(c, x, y, w, label, selected, accent);
}

int ui_dialog(EidosUiCtx *c, int x, int y, int w, const char *msg, const char *const *choices, int n, int cur, u32 accent)
{
    return (g_active->dialog != NULL ? g_active->dialog : ui_def_dialog)(c, x, y, w, msg, choices, n, cur, accent);
}

void ui_progress(EidosUiCtx *c, int x, int y, int w, int num, int den, u32 accent)
{
    (g_active->progress != NULL ? g_active->progress : ui_def_progress)(c, x, y, w, num, den, accent);
}

void ui_spinner(EidosUiCtx *c, int x, int y, int frame, u32 accent)
{
    (g_active->spinner != NULL ? g_active->spinner : ui_def_spinner)(c, x, y, frame, accent);
}

void ui_page_dots(EidosUiCtx *c, int x, int y, int n, int cur, u32 accent)
{
    (g_active->page_dots != NULL ? g_active->page_dots : ui_def_page_dots)(c, x, y, n, cur, accent);
}

void ui_target_chevron(EidosUiCtx *c, int x, int y, u32 color)
{
    (g_active->target_chevron != NULL ? g_active->target_chevron : ui_def_target_chevron)(c, x, y, color);
}

void ui_party_balls(EidosUiCtx *c, int x, int y, const u8 *states, int n)
{
    (g_active->party_balls != NULL ? g_active->party_balls : ui_def_party_balls)(c, x, y, states, n);
}

int ui_name_tag(EidosUiCtx *c, int x, int y, const char *name, u32 accent)
{
    return (g_active->name_tag != NULL ? g_active->name_tag : ui_def_name_tag)(c, x, y, name, accent);
}

void ui_category_icon(EidosUiCtx *c, int x, int y, int kind)
{
    (g_active->category_icon != NULL ? g_active->category_icon : ui_def_category_icon)(c, x, y, kind);
}

void ui_stat_row(EidosUiCtx *c, int x, int y, int w, const char *name, int value, int num, int den, u32 accent)
{
    (g_active->stat_row != NULL ? g_active->stat_row : ui_def_stat_row)(c, x, y, w, name, value, num, den, accent);
}

void ui_icon_well(EidosUiCtx *c, int x, int y, int size, const UiImage *icon, int selected)
{
    (g_active->icon_well != NULL ? g_active->icon_well : ui_def_icon_well)(c, x, y, size, icon, selected);
}

void ui_gender(EidosUiCtx *c, int x, int y, int gender, int font)
{
    (g_active->gender != NULL ? g_active->gender : ui_def_gender)(c, x, y, gender, font);
}

void ui_menu_row(EidosUiCtx *c, int x, int y, int w, const char *label, const UiImage *icon, int selected, int hover)
{
    (g_active->menu_row != NULL ? g_active->menu_row : ui_def_menu_row)(c, x, y, w, label, icon, selected, hover);
}

void ui_party_row(EidosUiCtx *c, int x, int y, int w, int slot, const char *name, int gender, int level, int hp, int maxhp, const UiImage *icon, int selected)
{
    (g_active->party_row != NULL ? g_active->party_row : ui_def_party_row)(c, x, y, w, slot, name, gender, level, hp, maxhp, icon, selected);
}

void ui_plate(EidosUiCtx *c, int x, int y, int w, const char *name, int gender, int level, int hp, int maxhp, int exp_num, int exp_den)
{
    (g_active->plate != NULL ? g_active->plate : ui_def_plate)(c, x, y, w, name, gender, level, hp, maxhp, exp_num, exp_den);
}

void ui_plate_status(EidosUiCtx *c, int x, int y, int w, int level, int badge)
{
    (g_active->plate_status != NULL ? g_active->plate_status : ui_def_plate_status)(c, x, y, w, level, badge);
}

void ui_cmd_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 color, int selected)
{
    (g_active->cmd_button != NULL ? g_active->cmd_button : ui_def_cmd_button)(c, x, y, w, h, label, color, selected);
}

void ui_move_row(EidosUiCtx *c, int x, int y, int w, int type, const char *name, int pp, int maxpp, int selected)
{
    (g_active->move_row != NULL ? g_active->move_row : ui_def_move_row)(c, x, y, w, type, name, pp, maxpp, selected);
}

int ui_item_row(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name, int count, int selected)
{
    return (g_active->item_row != NULL ? g_active->item_row : ui_def_item_row)(c, x, y, w, icon, name, count, selected);
}

void ui_dex_row(EidosUiCtx *c, int x, int y, int w, int number, const char *name, int state, int selected)
{
    (g_active->dex_row != NULL ? g_active->dex_row : ui_def_dex_row)(c, x, y, w, number, name, state, selected);
}

void ui_info_row(EidosUiCtx *c, int x, int y, int w, const char *label, const char *value)
{
    (g_active->info_row != NULL ? g_active->info_row : ui_def_info_row)(c, x, y, w, label, value);
}
