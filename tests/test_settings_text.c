/*
 * tests/test_settings_text.c -- every text the settings screen shows is in
 * the ui_text template (sdk/api/v5/ui_text_template.txt), so a mod can
 * translate all of it (docs/mod-ui-text.md).
 *
 * The template is loaded as a mod with each line's translation "[English]";
 * the test then walks every tab and row and reads each label, value, choice
 * and help line.  A text with letters in it must come back changed (or hold
 * a "[" where a translated part was put in); what is the player's own data
 * (a mod's name, a key, an address) is allowed by name below.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "settings.h"
#include "settings_ui.h"
#include "ui_lang.h"

void bw_set_save_dir(const char *path);   /* sdk/save.h */
void bw_mod_title_open_multiplayer(void);

#define INI "test_settings_text_work.ini"

#define A 0x1u
#define B 0x2u
#define DOWN 0x80u
#define R 0x100u

static int g_miss;

static void press(unsigned b)
{
    eidos_sui_input(0, 0);
    eidos_sui_input(b, b);
}

/* The data the screen shows as it is: key and button names, numbers. */
static int is_data(const char *s)
{
    static const char *const k_data[] = {
        "Enter", "Space", "Backspace", "Escape", "Tab", "Return",
        "Left", "Right", "Up", "Down", "None", "OpenGL",
        "Xbox 360", "Xbox One", "PS3", "PS4", "PS5", "GameCube", "Steam Deck",
    };
    size_t i;
    int letters = 0;

    for (i = 0; s[i] != '\0'; i++) {
        if ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')) {
            if (++letters >= 2) {
                break;
            }
        } else {
            letters = 0;
        }
    }
    if (letters < 2) {
        return 1;                       /* numbers, "-", one key letter */
    }
    for (i = 0; i < sizeof k_data / sizeof k_data[0]; i++) {
        if (strstr(s, k_data[i]) == s) {
            return 1;
        }
    }
    /* a binding ("X / Pad E": a key and a pad button), a size ("0px") */
    if (strstr(s, " / ") != NULL || (s[0] >= '0' && s[0] <= '9')) {
        return 1;
    }
    return strncmp(s, "Key ", 4) == 0 || strncmp(s, "Pad ", 4) == 0 ||
           (strchr(s, '/') != NULL && strchr(s, ' ') == NULL);
}

static void check(const char *where, const char *s)
{
    const char *t;

    if (s == NULL || s[0] == '\0' || strchr(s, '[') != NULL || is_data(s)) {
        return;
    }
    t = ui_tr(s);
    if (t == s || strcmp(t, s) == 0) {
        printf("  not in the template (%s): \"%s\"\n", where, s);
        g_miss++;
    }
}

static char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

/* "English = " lines into "English = [English]". */
static char *pseudo(const char *tmpl, size_t len, size_t *out_len)
{
    char *out = (char *)malloc(len * 3 + 16);
    size_t o = 0, i = 0;

    while (out != NULL && i < len) {
        size_t e = i;

        while (e < len && tmpl[e] != '\n') {
            e++;
        }
        if (e - i > 3 && tmpl[i] != '#' && memcmp(tmpl + e - 3, " = ", 3) == 0) {
            size_t en = e - i - 3;

            memcpy(out + o, tmpl + i, en + 3);
            o += en + 3;
            out[o++] = '[';
            memcpy(out + o, tmpl + i, en);
            o += en;
            out[o++] = ']';
            out[o++] = '\n';
        }
        i = e + 1;
    }
    *out_len = o;
    return out;
}

/* Every row of the tab on screen: label, value, choices, and the help lines
 * as the cursor walks down. */
static void walk_tab(const char *name)
{
    int rows = eidos_sui_row_count(), r, k;
    char label[96], value[96], names[3][20], where[96];

    for (r = 0; r < rows; r++) {
        int active = 0, n;

        eidos_sui_row_text(r, label, sizeof label, value, sizeof value);
        snprintf(where, sizeof where, "%s, row %d label", name, r);
        check(where, label);
        snprintf(where, sizeof where, "%s, row %d value", name, r);
        check(where, value);
        n = eidos_sui_row_choices(r, &active, names);
        for (k = 0; k < n && k < 3; k++) {
            snprintf(where, sizeof where, "%s, row %d choice", name, r);
            check(where, names[k]);
        }
    }
    for (r = 0; r < rows + 1; r++) {
        int h, hn = eidos_sui_help_count();

        for (h = 0; h < hn; h++) {
            eidos_sui_help_line(h, label, sizeof label);
            snprintf(where, sizeof where, "%s, help", name);
            check(where, label);
        }
        press(DOWN);
    }
}

int main(int argc, char **argv)
{
    size_t len = 0, plen = 0;
    char *tmpl, *text;
    int tab, tabs, pairs;
    char where[64];

    if (argc < 2 || (tmpl = read_all(argv[1], &len)) == NULL) {
        printf("usage: test_settings_text sdk/api/v5/ui_text_template.txt\n");
        return 1;
    }
    text = pseudo(tmpl, len, &plen);
    pairs = ui_lang_mod_add(1, text, plen);
    BW_CHECK_EQ("the template loads", pairs > 400, 1);

    bw_set_save_dir("test_settings_text_tmp");
    remove(INI);
    eidos_settings_set_path(INI);
    eidos_settings_load();
    eidos_sui_enter();
    tabs = eidos_sui_tab_count();
    for (tab = 0; tab < tabs; tab++) {
        snprintf(where, sizeof where, "tab %d", tab);
        check(where, eidos_sui_tab_name(tab));
    }
    for (tab = 0; tab < tabs; tab++) {
        walk_tab(eidos_sui_tab_name(tab));
        press(R);
    }
    eidos_sui_leave();
    /* the title's Multiplayer screen: the name, Host and Join */
    bw_mod_title_open_multiplayer();
    eidos_sui_enter();
    walk_tab("Multiplayer");
    eidos_sui_leave();
    remove(INI);
    printf("  %d texts not in the template\n", g_miss);
    BW_CHECK_EQ("every settings text is in the template", g_miss, 0);
    free(text);
    free(tmpl);
    BW_REPORT("test_settings_text");
}
