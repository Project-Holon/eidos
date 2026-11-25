/*
 * tests/test_port_keyboard.c -- the port's own keyboard's model
 * (engine/platform/port_keyboard.c): typing from keys, the pointer and the
 * PC's keyboard, the caret, the limit, the pages, the password's dots and
 * SHOW, the arrows over the grid and the bar, OK and Back.
 */

#include <stdio.h>
#include <string.h>

#include "port_keyboard.h"
#include "port_test.h"

#define KA 0x0001u
#define KB 0x0002u
#define KSEL 0x0004u
#define KSTART 0x0008u
#define KRIGHT 0x0010u
#define KLEFT 0x0020u
#define KUP 0x0040u
#define KDOWN 0x0080u
#define KR 0x0100u
#define KL 0x0200u
#define KY 0x0800u

int main(void)
{
    const EidosPkb *k;
    char out[EIDOS_PKB_MAX + 8], shown[256];
    int at, n, i, ids[8], parts[8];
    const char *labels[128];
    char codes[128];

    printf("opening\n");
    eidos_pkb_open("SIGN IN", "USER NAME", "Your user name?", "Pla\tyer", 20, EIDOS_PKB_NO_SPACE);
    k = eidos_pkb();
    BW_REQUIRE("it is open", k != NULL);
    BW_CHECK_EQ("a letter outside printable ASCII is left out", strcmp(k->text, "Player"), 0);
    BW_CHECK_EQ("the caret at the end", (long)k->caret, 6);
    BW_CHECK_EQ("a name opens on the capitals", (long)k->page, EIDOS_PKB_UPPER);

    printf("typing\n");
    eidos_pkb_type("1 2");
    BW_CHECK_EQ("the PC's keys type, a space is not taken", strcmp(k->text, "Player12"), 0);
    (void)eidos_pkb_input(KL);
    (void)eidos_pkb_input(KL);
    eidos_pkb_type("x");
    BW_CHECK_EQ("L moves the caret, a letter goes there", strcmp(k->text, "Playerx12"), 0);
    (void)eidos_pkb_input(KB);
    BW_CHECK_EQ("B deletes before the caret", strcmp(k->text, "Player12"), 0);
    (void)eidos_pkb_input(KR);
    (void)eidos_pkb_input(KR);
    BW_CHECK_EQ("R moves it back to the end", (long)k->caret, 8);
    eidos_pkb_tap_key(0);
    BW_CHECK_EQ("a key's tap types it", strcmp(k->text, "Player12A"), 0);
    n = eidos_pkb_bar_buttons(ids, parts, labels, 8);
    for (i = 0; i < n; i++) {
        BW_CHECK_EQ("no SPACE for one word", ids[i] != EIDOS_PKB_SPACE, 1);
    }
    for (i = 0; i < 30; i++) {
        eidos_pkb_type("z");
    }
    BW_CHECK_EQ("the limit holds", (long)k->len, 20);

    printf("the arrows\n");
    eidos_pkb_set_cols(10);
    eidos_pkb_tap_key(0);
    (void)eidos_pkb_input(KLEFT);
    BW_CHECK_EQ("left from the first key wraps to its row's last", (long)k->key, 9);
    (void)eidos_pkb_input(KDOWN);
    BW_CHECK_EQ("down goes to the next row", (long)k->key, 19);
    for (i = 0; i < 2; i++) {
        (void)eidos_pkb_input(KDOWN);
    }
    BW_CHECK_EQ("off the last row: the bar", k->bar >= 0, 1);
    (void)eidos_pkb_input(KUP);
    BW_CHECK_EQ("and up again: a key", k->bar < 0, 1);
    (void)eidos_pkb_input(KSEL);
    BW_CHECK_EQ("SELECT turns the page", (long)k->page, EIDOS_PKB_LOWER);

    printf("OK\n");
    (void)eidos_pkb_input(KSTART);
    BW_CHECK_EQ("START is OK", (long)eidos_pkb_result(out, (int)sizeof out), 1);
    BW_CHECK_EQ("with the text", strncmp(out, "Player12A", 9), 0);
    BW_CHECK_EQ("and it is closed", eidos_pkb() == NULL, 1);

    printf("a password\n");
    eidos_pkb_open("SIGN IN", "PASSWORD", "Your password?", "", 64, EIDOS_PKB_SECRET);
    k = eidos_pkb();
    BW_CHECK_EQ("a password opens on the small letters", (long)k->page, EIDOS_PKB_LOWER);
    eidos_pkb_type("a\"$'");
    eidos_pkb_display(shown, (int)sizeof shown, &at);
    BW_CHECK_EQ("four dots", strcmp(shown, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"), 0);
    BW_CHECK_EQ("the caret after them", (long)at, 12);
    (void)eidos_pkb_input(KY);
    eidos_pkb_display(shown, (int)sizeof shown, &at);
    BW_CHECK_EQ("Y shows it, with the port's marks for \" $ '",
                strcmp(shown, "a\xEF\xBC\x82\xEF\xBC\x84\xEF\xBC\x87"), 0);
    n = eidos_pkb_keys(EIDOS_PKB_SYMBOLS, 0, codes, labels, NULL, NULL, 128);
    BW_CHECK_EQ("the symbols page has every printable mark", (long)n, 32);
    for (i = 0; i < n && codes[i] != '$'; i++) {
    }
    BW_CHECK_EQ("$ is labelled with the full-width dollar", strcmp(labels[i], "\xEF\xBC\x84"), 0);
    n = eidos_pkb_keys(EIDOS_PKB_QWERTY, 1, codes, NULL, NULL, NULL, 128);
    BW_CHECK_EQ("qwerty has four rows of keys", (long)n, 36);
    BW_CHECK_EQ("SHIFT gives capitals", codes[10], 'Q');
    for (i = 0; i < 4; i++) {
        (void)eidos_pkb_input(KB);
    }
    BW_CHECK_EQ("B deletes to empty", (long)k->len, 0);
    (void)eidos_pkb_input(KB);
    BW_CHECK_EQ("B on an empty field is Back", (long)eidos_pkb_result(out, (int)sizeof out), -1);
    BW_CHECK_EQ("Back gives no text", out[0], 0);

    printf("closing forgets\n");
    eidos_pkb_open("SIGN IN", "PASSWORD", "?", "hunter2", 64, EIDOS_PKB_SECRET);
    eidos_pkb_close();
    BW_CHECK_EQ("closed", eidos_pkb() == NULL, 1);
    eidos_pkb_open("X", "Y", "?", "", 8, 0);
    BW_CHECK_EQ("nothing of the password is left", eidos_pkb()->text[0], 0);
    eidos_pkb_tap_bar(EIDOS_PKB_OK);
    BW_CHECK_EQ("the bar's OK", (long)eidos_pkb_result(out, (int)sizeof out), 1);

    BW_REPORT("test_port_keyboard");
}
