/*
 * engine/platform/settings_ui.h -- the settings screen's model: tabs, rows,
 * navigation and text.  No drawing and no game calls: the screen itself
 * (stubs/ov/reimpl_ov088_*.c, docs/settings-design.md) asks this module what
 * to show and tells it which buttons were pressed.
 *
 * Layout of the screen: two levels, like the game's own Options screen.
 * Level 0 lists the sections (Display, Layout, Controls, Sound); A enters one.
 * Level 1 shows the section name as the header and up to EIDOS_SUI_VISIBLE
 * option rows (more rows scroll); B returns to level 0 with the cursor on
 * that section.  Every section ends with two action rows, "Revert changes"
 * and "Restore defaults".  B at level 0 leaves the screen.  Leaving
 * the screen commits (eidos_sui_leave); Revert undoes everything since the
 * screen was opened or last committed.
 *
 * Text is plain ASCII.  All of it is in one table in settings_ui.c
 * ("UI TEXT"), so a translation or a mod replaces that table.
 */
#ifndef EIDOS_ENGINE_SETTINGS_UI_H
#define EIDOS_ENGINE_SETTINGS_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#define EIDOS_SUI_VISIBLE 5   /* option rows shown; the grey bar is a sixth */

/* Result flags of eidos_sui_input. */
#define EIDOS_SUI_REDRAW      0x01   /* rebuild the rows                      */
#define EIDOS_SUI_EXIT        0x02   /* the player left the screen            */
#define EIDOS_SUI_KEYBOARD    0x04   /* open the game's keyboard (below)      */
#define EIDOS_SUI_SFX_MOVE    0x10
#define EIDOS_SUI_SFX_SELECT  0x20
#define EIDOS_SUI_SFX_BACK    0x40

/* Button bits as the game reads them (A 1, B 2, right 0x10, left 0x20, up
 * 0x40, down 0x80, R 0x100, L 0x200). */

void eidos_sui_enter(void);     /* level 0, first row, nothing capturing    */
void eidos_sui_leave(void);     /* commit; cancels a capture                  */

/* One frame.  `pressed` are the buttons that went down this frame, `held`
 * the buttons down now (used for repeat).  Returns EIDOS_SUI_* flags. */
int eidos_sui_input(unsigned pressed, unsigned held);

/* What to draw.  Row 0 is the header; rows 1..eidos_sui_row_count()-2 are the
 * visible option rows and the last row, eidos_sui_row_count()-1, is the grey
 * bar ("Done" on the list of sections, "Back" inside one).  `label` and `value` are ASCII; value may be "". */
int  eidos_sui_row_count(void);
void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
/* A row that is an option with two or three named choices (window mode, screen
 * layout, swap): how many, which is active (0..count-1) and each name.  The
 * game's Options screen shows all the choices side by side and marks the
 * active one; 0 means draw label and value instead. */
int  eidos_sui_row_choices(int row, int *active, char names[3][20]);
/* The highlighted row, 1..eidos_sui_row_count()-1. */
int  eidos_sui_cursor_row(void);
/* 1 when the screen's last row is the grey bar (Back, or a question's
 * cancel); the main list has none, and its sixth bar is a row. */
int  eidos_sui_has_back(void);
/* The tabs: the name of tab i (in screen order); eidos_sui_tab() is the
 * one shown. */
const char *eidos_sui_tab_name(int i);
/* The list's scroll: the first row shown, how many are shown, how many
 * there are (for the scrollbar). */
int  eidos_sui_scroll(int *first, int *shown, int *total);
int  eidos_sui_row_adjustable(int row);     /* left and right change its value */
/* A binding row's gamepad control as its icon's number (engine/ui/
 * padglyph.h), -1 when the row binds none; *key gets the keyboard part of
 * its value ("" when no key). */
int  eidos_sui_row_pad(int row, char *key, int nk);

/* The stylus: see eidos_sui_touch in settings_ui.c. */
#define EIDOS_SUI_ZONE_ROW (-1)
#define EIDOS_SUI_ZONE_DEC (-2)
#define EIDOS_SUI_ZONE_INC (-3)
int  eidos_sui_touch(int row, int zone);
/* A click of the one-screen layout, answered at the screen's next step as
 * a touch of that row (zone as eidos_sui_touch) or of that section's tab. */
void eidos_sui_request_touch(int row, int zone);
void eidos_sui_request_tab(int tab);
/* The mouse is over a row (numbered as for eidos_sui_touch): the cursor goes
 * there, with no sound and no action.  Returns 1 when it moved. */
int  eidos_sui_hover(int row);
/* The mouse wheel over the rows: scroll them `steps` (up when positive)
 * with no sound, the cursor kept on a shown row.  0 when the screen takes
 * no wheel now (a question, a capture, an edit). */
int  eidos_sui_wheel(int steps);
/*
 * The game's own keyboard (the name entry screen) for a text of the screen.
 * After EIDOS_SUI_KEYBOARD, eidos_sui_keyboard says what it is for: the
 * name entry's kind and length and the text it starts with; it answers 0
 * when nothing waits.  The screen that opened it hands the text back with
 * eidos_sui_keyboard_done (ok 0: the player cancelled).
 */
int  eidos_sui_keyboard(int *kind, int *max, char *text, int n);
void eidos_sui_keyboard_done(const char *text, int ok);
/* The question the keyboard asks for that text, or NULL: the game's own. */
const char *eidos_sui_keyboard_prompt(void);

/* Non-zero while waiting for a key or pad control. */
int  eidos_sui_capturing(void);

/* Help text for the bottom screen: eidos_sui_help_count() lines for the
 * highlighted row (a line may be ""). */
int  eidos_sui_help_count(void);
void eidos_sui_help_line(int i, char *out, int n);

/*
 * The Multiplayer screen opened alone from the title menu, for a one-screen
 * view of its own (engine/ui/recipe_multiplayer.c).  eidos_sui_mp_up: that
 * screen is showing.  eidos_sui_mp_view: what it shows before a game
 * (EIDOS_SUI_MP_*), -1 when it is not up.  eidos_sui_mp_act: do what the
 * row of that action does (EIDOS_SUI_MP_ACT_*; arg: the kept game's index
 * for GAME and HOST_KEPT), at the screen's next step, as a touch on that
 * row; 0 when the screen has no such row now.  eidos_sui_mp_status: the
 * screen's status line (connecting, code not found, the session ended).
 */
/*
 * The code in a text the player pasted: spaces around it go, an
 * `eidos://join/` in front of it goes, letters become capitals.  1 and the
 * code in `out` when what is left is a relay's code (6 characters of
 * 23456789ABCDEFGHJKMNPQRSTUVWXYZ) or an Archipelago room's port (2 to 5
 * digits); 0 for anything else.
 */
int eidos_sui_code_from_text(const char *text, char *out, int n);

#define EIDOS_SUI_MP_CHOOSE 0
#define EIDOS_SUI_MP_JOIN   1
#define EIDOS_SUI_MP_HOST   2
#define EIDOS_SUI_MP_BUSY   3
#define EIDOS_SUI_MP_MODE   4   /* a mod's way to play: its options, Start */
#define EIDOS_SUI_MP_WORLD  5   /* a world a typed code names: its rules, Join */
#define EIDOS_SUI_MP_ROOM   6   /* an Archipelago room (a typed port): slot, password, Join */

#define EIDOS_SUI_MP_ACT_NAME      0    /* the player's name: the keyboard   */
#define EIDOS_SUI_MP_ACT_HOST      1    /* Host: the kept games' view        */
#define EIDOS_SUI_MP_ACT_JOIN      2    /* Join: the code's keyboard, then join */
#define EIDOS_SUI_MP_ACT_GAME      3    /* (Host view) host kept game arg    */
#define EIDOS_SUI_MP_ACT_NEW_GAME  4    /* (Host view) name a new game, host */
#define EIDOS_SUI_MP_ACT_START     5    /* (lobby, host) start the game      */
#define EIDOS_SUI_MP_ACT_LEAVE     6    /* (lobby) leave the session         */
#define EIDOS_SUI_MP_ACT_COPY      7    /* (lobby) copy the code             */
#define EIDOS_SUI_MP_ACT_HOST_KEPT 8    /* host kept game arg from the start */
#define EIDOS_SUI_MP_ACT_MODE      9    /* open mod mode arg (eidos_sui_mp_modes order) */
#define EIDOS_SUI_MP_ACT_MODE_START 10  /* (Mode view) the mode's new game    */
#define EIDOS_SUI_MP_ACT_CONTINUE  11   /* (hub) go on with Your games arg    */
#define EIDOS_SUI_MP_ACT_COOP_NEW  12   /* (hub) a new co-op game: name, host */
#define EIDOS_SUI_MP_ACT_PASTE     13   /* (hub) join with the code on the clipboard */

/* Your games: the co-op games this player hosts and the saves the cards
 * made (sdk/save_games.h), the latter first, newest first.  ok: it can go
 * on (a save of a mod that is off cannot). */
#define EIDOS_SUI_GAME_COOP  0
#define EIDOS_SUI_GAME_SOLO  1
#define EIDOS_SUI_GAME_WORLD 2
#define EIDOS_SUI_GAME_ROOM  3   /* an Archipelago room */
typedef struct EidosSuiGame {
    int  kind;                  /* EIDOS_SUI_GAME_*                          */
    int  host;                  /* made or hosted here (else joined)         */
    int  ok;
    int  slot;                  /* a card's save: its slot                   */
    int  kept;                  /* a co-op game: its index in the kept list  */
    char title[48];             /* the game's name, or the mode's title      */
    char owner[40];             /* the mod                                   */
    char code[17];              /* a world's code, "" when none              */
} EidosSuiGame;
int  eidos_sui_mp_games(void);
int  eidos_sui_mp_game(int i, EidosSuiGame *out);

/* The mods' ways to play (`mode` lines of the mods that are on): how many,
 * and the k-th one's id, title and line (k -1: the one the Mode view
 * shows); the answer is 1 when it is `off` (shown, not chosen), 0 when it
 * can be chosen, -1 when there is no such mode. */
int  eidos_sui_mp_modes(void);
int  eidos_sui_mp_mode_info(int k, const char **id, const char **title, const char **line);
/* The name of the mod whose mode the Mode view shows. */
const char *eidos_sui_mp_mode_owner(void);

/* Every row of the Multiplayer screen's list, for a view that lays them
 * out itself (the Mode view's options): how many; the i-th row's label,
 * value and kind (EIDOS_SUI_MP_ROW_*, -1 none); a touch on it at the next
 * step (zone as for eidos_sui_touch), scrolling the list to it. */
#define EIDOS_SUI_MP_ROW_ACTION 0      /* A does it                       */
#define EIDOS_SUI_MP_ROW_VALUE  1      /* left and right change it        */
#define EIDOS_SUI_MP_ROW_TEXT   2      /* A types it                      */
#define EIDOS_SUI_MP_ROW_START  3      /* the Mode view's Start, Join the world */
#define EIDOS_SUI_MP_ROW_INFO   4      /* a line to read (the World view)   */
int  eidos_sui_mp_rows(void);
int  eidos_sui_mp_row(int i, char *label, int nl, char *value, int nv);
void eidos_sui_mp_row_act(int i, int zone);
/* A row's heading above it (an option's `heading`), what it does (its
 * `help`, the Type row's mode line, Start's own words), and whether it can
 * be chosen now (a type not built yet, a world that cannot be joined). */
int  eidos_sui_mp_row_extra(int i, const char **heading, const char **help, int *ok);

int  eidos_sui_mp_up(void);
int  eidos_sui_mp_view(void);
int  eidos_sui_mp_can(int action, int arg);
int  eidos_sui_mp_act(int action, int arg);
int  eidos_sui_mp_status(char *out, int n);

/* Test hooks.  eidos_sui_tab is the section index, -1 at level 0. */
int  eidos_sui_level(void);
int  eidos_sui_tab(void);
int  eidos_sui_tab_count(void);

#ifdef __cplusplus
}
#endif
#endif
