/*
 * tests/test_settings_ui.c -- the settings screen's model (tabs, rows,
 * navigation, value changes, defaults, commit, the Multiplayer tab's views)
 * with no platform.
 */

#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "settings.h"
#include "settings_ui.h"

void bw_set_save_dir(const char *path);   /* sdk/save.h */

void bw_mod_title_open_multiplayer(void);

#define INI "test_settings_ui_work.ini"

#define A 0x1u
#define B 0x2u
#define RIGHT 0x10u
#define LEFT 0x20u
#define UP 0x40u
#define DOWN 0x80u
#define R 0x100u
#define L 0x200u

static char g_label[64];
static char g_value[64];

static void row(int i)
{
    eidos_sui_row_text(i, g_label, sizeof g_label, g_value, sizeof g_value);
}

static int press(unsigned b)
{
    eidos_sui_input(0, 0);
    return eidos_sui_input(b, b);
}

int main(void)
{
    int r, i, first, shown, total;
    char help[96];
    /* the multiplayer games listed are the test's own (none) */
    bw_set_save_dir("test_settings_ui_tmp");
    remove(INI);
    eidos_settings_set_path(INI);
    eidos_settings_load();
    eidos_sui_enter();

    /* The screen opens on a tab: no list of sections, no grey bar. */
    BW_CHECK_EQ("seven tabs", eidos_sui_tab_count(), 7);
    BW_CHECK_EQ("the first tab", eidos_sui_tab(), 0);
    BW_CHECK_EQ("its name", strcmp(eidos_sui_tab_name(0), "Display"), 0);
    BW_CHECK_EQ("the last tab", strcmp(eidos_sui_tab_name(6), "Extras"), 0);
    BW_CHECK_EQ("no grey bar", eidos_sui_has_back(), 0);
    row(0);
    BW_CHECK_EQ("the header is the tab", strcmp(g_label, "Display"), 0);
    BW_CHECK_EQ("display rows", eidos_sui_row_count(), 1 + 4);
    row(1);
    BW_CHECK_EQ("mode label", strcmp(g_label, "Window mode"), 0);
    BW_CHECK_EQ("mode value", strcmp(g_value, "Windowed"), 0);
    BW_CHECK_EQ("cursor on first row", eidos_sui_cursor_row(), 1);

    /* Right changes the choice and asks for a redraw. */
    eidos_sui_input(0, 0);
    r = eidos_sui_input(RIGHT, RIGHT);
    BW_CHECK_EQ("right redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("mode changed", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 1);
    BW_CHECK_EQ("dirty", eidos_settings_dirty(), 1);
    row(1);
    BW_CHECK_EQ("mode value text", strcmp(g_value, "Fullscreen"), 0);
    press(LEFT);
    BW_CHECK_EQ("mode back", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 0);

    /* Down moves; the pixel scale and the 3D resolution step from auto. */
    press(DOWN);
    BW_CHECK_EQ("cursor moved", eidos_sui_cursor_row(), 2);
    row(2);
    BW_CHECK_EQ("pixel scale label", strcmp(g_label, "Pixel scale"), 0);
    BW_CHECK_EQ("pixel scale auto default", strcmp(g_value, "Auto"), 0);
    press(RIGHT);
    row(2);
    BW_CHECK_EQ("pixel scale 1x", strcmp(g_value, "1x"), 0);
    press(DOWN);
    row(3);
    BW_CHECK_EQ("resolution label", strcmp(g_label, "3D resolution"), 0);
    BW_CHECK_EQ("resolution auto default", strcmp(g_value, "Auto"), 0);
    press(RIGHT);
    row(3);
    BW_CHECK_EQ("resolution 1x", strcmp(g_value, "1x"), 0);

    /* The action row puts the defaults back. */
    press(DOWN);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("defaults row", strcmp(g_label, "Restore defaults"), 0);
    press(A);
    BW_CHECK_EQ("defaults restored", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("defaults restored: pixel scale",
                eidos_settings_get(EIDOS_OPT_DISPLAY_PIXEL_SCALE), 0);
    press(DOWN);
    BW_CHECK_EQ("the cursor wraps to the top", eidos_sui_cursor_row(), 1);

    /* R and L change the tab, and wrap. */
    r = press(R);
    BW_CHECK_EQ("R redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("R: Layout", eidos_sui_tab(), 1);
    BW_CHECK_EQ("layout rows", eidos_sui_row_count(), 1 + 4);
    press(L);
    BW_CHECK_EQ("L: Display", eidos_sui_tab(), 0);
    press(L);
    BW_CHECK_EQ("L wraps to Extras", eidos_sui_tab(), 6);
    press(R);
    BW_CHECK_EQ("R wraps to Display", eidos_sui_tab(), 0);

    /* Sound: volume steps of 5. */
    press(R);
    press(R);
    press(R);
    BW_CHECK_EQ("sound tab", eidos_sui_tab(), 3);
    row(1);
    BW_CHECK_EQ("volume label", strcmp(g_label, "Master volume"), 0);
    press(LEFT);
    BW_CHECK_EQ("volume -5", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 45);

    /* Controls: the button icons, 12 buttons and the defaults, six shown,
     * a scrollbar. */
    press(L);
    BW_CHECK_EQ("controls tab", eidos_sui_tab(), 2);
    BW_CHECK_EQ("controls rows shown", eidos_sui_row_count(), 1 + 6);
    eidos_sui_scroll(&first, &shown, &total);
    BW_CHECK_EQ("the scroll starts at the top", first, 0);
    BW_CHECK_EQ("six shown", shown, 6);
    BW_CHECK_EQ("fourteen in all", total, 14);
    press(UP);                                /* wraps to the last row */
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("last row", strcmp(g_label, "Restore defaults"), 0);
    eidos_sui_scroll(&first, &shown, &total);
    BW_CHECK_EQ("scrolled to the bottom", first, 14 - 6);
    press(DOWN);                              /* wraps to the first row */
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("the button icons first", strcmp(g_label, "Button icons"), 0);
    press(DOWN);
    for (i = 0; i < 12; i++) {
        row(eidos_sui_cursor_row());
        BW_CHECK_EQ("bind label short", (int)strlen(g_label) <= 6, 1);
        BW_CHECK_EQ("bind value short", (int)strlen(g_value) <= 20, 1);
        press(DOWN);
    }
    press(DOWN);                              /* back to the first row */
    press(DOWN);                              /* past the button icons */
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("first button", strcmp(g_label, "A"), 0);

    /* A on a binding row starts a capture; B cancels it without a platform. */
    press(A);
    BW_CHECK_EQ("capturing", eidos_sui_capturing(), 1);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("press a key", strcmp(g_value, "Press a key"), 0);
    eidos_sui_help_line(0, help, sizeof help);
    BW_CHECK_EQ("capture help", help[0] != 0, 1);
    eidos_sui_input(B, B);
    BW_CHECK_EQ("cancelled", eidos_sui_capturing(), 0);
    BW_CHECK_EQ("still on the tab", eidos_sui_tab(), 2);
    eidos_sui_help_line(0, help, sizeof help);
    BW_CHECK_EQ("bind help", help[0] != 0, 1);
    /* The press that ends a capture acts on nothing: the game sees the host's
     * buttons a frame late, so two quiet frames must pass first. */
    eidos_sui_input(0, 0);
    r = eidos_sui_input(B, B);
    BW_CHECK_EQ("B right after a capture does nothing", r & EIDOS_SUI_EXIT, 0);
    eidos_sui_input(0, 0);

    /* B leaves; the title menu's MULTIPLAYER opens its own screen: your
     * name, Host and Join.  The name and the code are typed on the game's
     * keyboard; Host lists the kept games and a new one; B goes back. */
    r = press(B);
    BW_CHECK_EQ("B leaves the settings", r & EIDOS_SUI_EXIT, EIDOS_SUI_EXIT);
    eidos_sui_leave();
    bw_mod_title_open_multiplayer();
    eidos_sui_enter();
    BW_CHECK_EQ("the multiplayer screen", eidos_sui_tab(), 7);
    BW_CHECK_EQ("no tabs on it", eidos_sui_tab_count(), 0);
    press(R);
    BW_CHECK_EQ("R does not leave it", eidos_sui_tab(), 7);
    BW_CHECK_EQ("name and two buttons", eidos_sui_row_count(), 1 + 3);
    row(1);
    BW_CHECK_EQ("Your name", strcmp(g_label, "Your name"), 0);
    row(2);
    BW_CHECK_EQ("Host", strcmp(g_label, "Host"), 0);
    row(3);
    BW_CHECK_EQ("Join", strcmp(g_label, "Join"), 0);
    {
        int kind = -1, max = 0;
        char text[32];

        /* the name: the game's keyboard, the player's kind */
        r = press(A);
        BW_CHECK_EQ("A opens the keyboard", r & EIDOS_SUI_KEYBOARD, EIDOS_SUI_KEYBOARD);
        BW_CHECK_EQ("for the name", eidos_sui_keyboard(&kind, &max, text, sizeof text) != 0, 1);
        BW_CHECK_EQ("a player's name", kind, 0);
        BW_CHECK_EQ("seven letters", max, 7);
        BW_CHECK_EQ("the game's own question", eidos_sui_keyboard_prompt() == NULL, 1);
        eidos_sui_keyboard_done("Cheren", 1);
        BW_CHECK_EQ("nothing waits", eidos_sui_keyboard(&kind, &max, text, sizeof text), 0);
        row(1);
        BW_CHECK_EQ("the new name", strcmp(g_value, "Cheren"), 0);

        /* Host: the kept games (none here) and a new one, named first */
        press(DOWN);
        press(A);
        BW_CHECK_EQ("New game alone", eidos_sui_row_count(), 1 + 1);
        row(1);
        BW_CHECK_EQ("New game", strcmp(g_label, "New game"), 0);
        r = press(A);
        BW_CHECK_EQ("named first", r & EIDOS_SUI_KEYBOARD, EIDOS_SUI_KEYBOARD);
        BW_CHECK_EQ("its question", eidos_sui_keyboard_prompt() != NULL, 1);
        eidos_sui_keyboard_done(NULL, 0);
        r = press(B);
        BW_CHECK_EQ("B goes back, not out", r & EIDOS_SUI_EXIT, 0);
        BW_CHECK_EQ("the cursor on Host", eidos_sui_cursor_row(), 2);

        /* Join: the code on the keyboard at once */
        press(DOWN);
        r = press(A);
        BW_CHECK_EQ("the code row and the Join button", eidos_sui_row_count(), 1 + 2);
        row(1);
        BW_CHECK_EQ("the code", strcmp(g_label, "Code"), 0);
        BW_CHECK_EQ("the code's keyboard", r & EIDOS_SUI_KEYBOARD, EIDOS_SUI_KEYBOARD);
        (void)eidos_sui_keyboard(&kind, &max, text, sizeof text);
        BW_CHECK_EQ("six letters", max, 6);
        eidos_sui_keyboard_done(NULL, 0);    /* cancelled: nothing is joined */
    }
    r = press(B);
    BW_CHECK_EQ("B goes back, not out", r & EIDOS_SUI_EXIT, 0);
    BW_CHECK_EQ("name, Host and Join again", eidos_sui_row_count(), 1 + 3);
    BW_CHECK_EQ("the cursor on Join", eidos_sui_cursor_row(), 3);

    /* B leaves; leaving commits. */
    r = press(B);
    BW_CHECK_EQ("B exits", r & EIDOS_SUI_EXIT, EIDOS_SUI_EXIT);
    eidos_sui_leave();
    BW_CHECK_EQ("committed", eidos_settings_dirty(), 0);
    eidos_settings_load();
    BW_CHECK_EQ("saved volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 45);

    /* The screen opens again on the tab it was left on; the stylus sets a
     * choice where it is touched, and the arrows act only on the selected
     * row. */
    eidos_sui_enter();
    BW_CHECK_EQ("the tab it was left on", eidos_sui_tab(), 2);
    press(L);
    press(L);
    BW_CHECK_EQ("display", eidos_sui_tab(), 0);
    eidos_settings_set(EIDOS_OPT_DISPLAY_MODE, 0);
    eidos_sui_touch(1, 2);
    BW_CHECK_EQ("touch sets the third choice", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 2);
    BW_CHECK_EQ("cursor on the touched row", eidos_sui_cursor_row(), 1);
    eidos_sui_touch(1, 0);
    BW_CHECK_EQ("touch sets the first choice", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 0);
    BW_CHECK_EQ("row 2 is adjustable", eidos_sui_row_adjustable(2), 1);
    BW_CHECK_EQ("row 3 is adjustable", eidos_sui_row_adjustable(3), 1);
    BW_CHECK_EQ("row 4 is not", eidos_sui_row_adjustable(4), 0);
    eidos_settings_set(EIDOS_OPT_DISPLAY_RESOLUTION, 2);
    eidos_sui_touch(3, EIDOS_SUI_ZONE_INC);
    BW_CHECK_EQ("first touch only selects", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 2);
    BW_CHECK_EQ("selected", eidos_sui_cursor_row(), 3);
    eidos_sui_touch(3, EIDOS_SUI_ZONE_INC);
    BW_CHECK_EQ("arrow raises", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 3);
    eidos_sui_touch(3, EIDOS_SUI_ZONE_DEC);
    BW_CHECK_EQ("arrow lowers", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 2);
    r = press(B);
    BW_CHECK_EQ("B leaves", r & EIDOS_SUI_EXIT, EIDOS_SUI_EXIT);
    eidos_sui_leave();

    remove(INI);
    BW_REPORT("test_settings_ui");
}
