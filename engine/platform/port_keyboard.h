/*
 * engine/platform/port_keyboard.h -- the port's own keyboard: a text of the
 * port's or a mod's (a name, a code, a user name, a password) typed in the
 * one-screen interface.
 *
 * It has the game keyboard's look and pages (engine/ui/recipe_keyboard.c
 * draws it) and none of its limits: up to EIDOS_PKB_MAX letters, every
 * printable ASCII character, a caret that moves, and a password shown as
 * dots until SHOW.  The settings model opens it (engine/platform/
 * settings_ui.c, keyboard_open) when the one-screen interface is on; the
 * classic two-screen layout keeps the game's own name entry.
 *
 * Keys come in two ways: the DS keys the settings screen reads
 * (eidos_pkb_input) and the pointer and the PC keyboard, through the
 * drawing (eidos_pkb_tap_*, eidos_pkb_type).  Either may end it; the
 * settings model asks eidos_pkb_result each frame.
 */
#ifndef EIDOS_PORT_KEYBOARD_H
#define EIDOS_PORT_KEYBOARD_H

#define EIDOS_PKB_MAX 64

/* The pages, in the order the tabs show them. */
#define EIDOS_PKB_UPPER   0
#define EIDOS_PKB_LOWER   1
#define EIDOS_PKB_SYMBOLS 2
#define EIDOS_PKB_QWERTY  3
#define EIDOS_PKB_PAGES   4

/* The buttons of the bottom bar. */
#define EIDOS_PKB_BACK   0
#define EIDOS_PKB_SHOW   1
#define EIDOS_PKB_SHIFT  2
#define EIDOS_PKB_DELETE 3
#define EIDOS_PKB_SPACE  4
#define EIDOS_PKB_OK     5

/* How eidos_pkb_open is asked. */
#define EIDOS_PKB_SECRET   0x01     /* shown as dots; SHOW and Y reveal it  */
#define EIDOS_PKB_NO_SPACE 0x02     /* no SPACE key (a user name)            */

typedef struct EidosPkb {
    char title[24];             /* the header: "SIGN IN", "NAME"            */
    char sub[32];               /* and what is typed: "PASSWORD"            */
    char prompt[96];            /* the question over the field              */
    char text[EIDOS_PKB_MAX + 1];
    int  len, caret, max;
    int  secret, show, no_space;
    int  page, shift;           /* shift: the qwerty page's capitals        */
    int  key;                   /* the key the cursor is on, or -1          */
    int  bar;                   /* the bar button it is on (EIDOS_PKB_*), -1 */
    int  cols;                  /* the grid's columns as drawn               */
} EidosPkb;

/* Open the keyboard on `start` (letters outside printable ASCII are left
 * out).  `max` is cut to EIDOS_PKB_MAX. */
void eidos_pkb_open(const char *title, const char *sub, const char *prompt, const char *start, int max,
                    int flags);
/* The keyboard while it is open, NULL when it is not. */
const EidosPkb *eidos_pkb(void);
/* The DS keys pressed this frame (BW_KEY_*): 1 when something changed. */
int eidos_pkb_input(unsigned pressed);
/* 1: OK with the text, -1: Back, 0: still open.  Taking it closes the
 * keyboard; `out` gets the text on OK. */
int eidos_pkb_result(char *out, int n);
void eidos_pkb_close(void);

/* The drawing's side: the grid's column count as laid out (the arrows move
 * over it), and the pointer's and the PC keyboard's input. */
void eidos_pkb_set_cols(int cols);
void eidos_pkb_tap_key(int i);
void eidos_pkb_tap_bar(int button);
void eidos_pkb_tap_page(int page);
void eidos_pkb_type(const char *utf8);

/* The keys of a page: their count, and each one's character (`codes`) and
 * label (UTF-8, the port's own marks for the font's curly quotes and its
 * Pokedollar sign).  The qwerty page's rows: `row` gets each key's row and
 * `half` its left edge in half keys (NULL for the other pages). */
int eidos_pkb_keys(int page, int shift, char *codes, const char *labels[], int *row, int *half, int max);
/* The bottom bar's buttons for the open keyboard: their count, ids
 * (EIDOS_PKB_*), widths in parts and labels. */
int eidos_pkb_bar_buttons(int *ids, int *parts, const char **labels, int max);
/* The text as the field shows it: dots for a hidden secret, the port's
 * marks for ' " $.  `caret_at` gets the byte where the caret stands. */
void eidos_pkb_display(char *out, int n, int *caret_at);

#endif
