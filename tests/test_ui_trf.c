/*
 * tests/test_ui_trf.c -- a mod's ui_text for labels built from values
 * (engine/ui/ui_lang.c ui_trf): the template is translated before the
 * values go in, and a translation whose conversions differ from the
 * English's is refused, so it cannot make the program read the wrong value.
 */
#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "ui_lang.h"

static const char k_text[] =
    "# a mod's ui_text file\n"
    "PWR %d  ACC %d = \xE5\xA8\x81\xE5\x8A\x9B %d  \xE5\x91\xBD\xE4\xB8\xAD %d\n"
    "%s nature. = \xE6\x80\xA7\xE6\xA0\xBC\xEF\xBC\x9A%s\n"
    "Met at %s, Lv. %u. = at %s, level %u\n"
    "No. %03d = N%d\n"
    "Lv. %d = %s level\n"
    "SEEN %d  OWN %d = %d own\n"
    "%d%% done = %d%% fini, %%\n"
    "Last saved %u/%u/20%02u  %02u:%02u = %u/%u/20%02u %02u:%02u saved\n";

int main(void)
{
    char out[128];

    BW_CHECK_EQ("pairs taken", ui_lang_mod_add(1, k_text, sizeof k_text - 1), 8);

    snprintf(out, sizeof out, ui_trf("PWR %d  ACC %d"), 40, 100);
    BW_CHECK_EQ("two numbers, translated",
                strcmp(out, "\xE5\xA8\x81\xE5\x8A\x9B 40  \xE5\x91\xBD\xE4\xB8\xAD 100"), 0);
    snprintf(out, sizeof out, ui_trf("%s nature."), "Bold");
    BW_CHECK_EQ("a name, translated", strcmp(out, "\xE6\x80\xA7\xE6\xA0\xBC\xEF\xBC\x9A" "Bold"), 0);
    snprintf(out, sizeof out, ui_trf("Met at %s, Lv. %u."), "Route 1", 5u);
    BW_CHECK_EQ("a name and a number", strcmp(out, "at Route 1, level 5"), 0);
    snprintf(out, sizeof out, ui_trf("No. %03d"), 7);
    BW_CHECK_EQ("another width is the same value", strcmp(out, "N7"), 0);
    snprintf(out, sizeof out, ui_trf("Lv. %d"), 12);
    BW_CHECK_EQ("%s for %d is refused: the English", strcmp(out, "Lv. 12"), 0);
    snprintf(out, sizeof out, ui_trf("SEEN %d  OWN %d"), 3, 2);
    BW_CHECK_EQ("one value fewer is refused", strcmp(out, "SEEN 3  OWN 2"), 0);
    snprintf(out, sizeof out, ui_trf("%d%% done"), 50);
    BW_CHECK_EQ("%% is not a value", strcmp(out, "50% fini, %"), 0);
    snprintf(out, sizeof out, ui_trf("Last saved %u/%u/20%02u  %02u:%02u"), 11u, 7u, 26u, 9u, 5u);
    BW_CHECK_EQ("five values", strcmp(out, "11/7/2026 09:05 saved"), 0);
    snprintf(out, sizeof out, ui_trf("ITEM %d"), 4);
    BW_CHECK_EQ("no translation: the English", strcmp(out, "ITEM 4"), 0);

    ui_lang_mod_clear(UI_LANG_ALL_MODS);
    snprintf(out, sizeof out, ui_trf("PWR %d  ACC %d"), 40, 100);
    BW_CHECK_EQ("the mod off: English", strcmp(out, "PWR 40  ACC 100"), 0);
    BW_REPORT("test_ui_trf");
}
