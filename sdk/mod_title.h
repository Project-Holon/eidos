/*
 * sdk/mod_title.h -- state shared by the title menu hooks
 * (stubs/ov/reimpl_ov088_*.c, hook name "title_settings", docs/hooks.md).
 *
 * The menu proc of overlay 88 keeps its rows in an 8 byte list and has
 * exactly eight row kinds, all with a window and a row buffer of their own
 * (docs/settings-design.md).  The hook gives the last kind, 7, to the
 * SETTINGS row while the game's own use of it (the conditional row whose
 * test is func_0200C308(save, 3)) is off.
 */
#ifndef BW_PORT_MOD_TITLE_H
#define BW_PORT_MOD_TITLE_H

#define BW_HOOK_TITLE_SETTINGS "title_settings"

/* Row-list value of the SETTINGS row, and the proc selection (+0x129) the
 * exit function maps to the settings proc. */
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the hook is on. */
int  bw_mod_title_settings_on(void);

/* 1 while the settings screen (placeholder) is up: the menu proc is then
 * restarted showing only the SETTINGS row. */
int  bw_mod_title_in_settings(void);
int  bw_mod_title_set_in_settings(int on);

/* The settings screen rebuilt its rows: the next time the cursor state is
 * reached after the text is printed it comes back at once (no fade).
 * take returns the flag and clears it. */
int  bw_mod_title_set_rebuild(void);
int  bw_mod_title_take_rebuild(void);

/* The title menu goes straight into the game the next time it waits for
 * input: Continue when there is a save, else New Game.  Multiplayer's start
 * asks for it (sdk/mp.h); take returns the request and clears it. */
void bw_mod_title_request_autostart(void);
/* The request is waiting (the title menu goes straight on with the save in
 * force: the slot module leaves that slot as it is). */
int  bw_mod_title_autostart_pending(void);

/* The title menu's MULTIPLAYER row opens the settings process on the
 * Multiplayer screen alone; take returns the request and clears it. */
void bw_mod_title_open_multiplayer(void);
int  bw_mod_title_take_multiplayer(void);

/* A game to join from the start (`--join CODE` or an eidos://join/CODE
 * link, docs/distribution.md): the title menu opens the Multiplayer screen
 * at once and the screen joins that code.  pending says whether one waits;
 * take hands the code over (or NULL) and clears it. */
void bw_mod_title_request_join(const char *code);
int  bw_mod_title_join_pending(void);
const char *bw_mod_title_take_join(void);
int  bw_mod_title_take_autostart(void);

/* Where the settings screen sends the player back to: the title menu process
 * (overlay id and process table), noted when the menu leaves for it. */
void bw_mod_title_set_return(int overlay, const void *table);
int  bw_mod_title_return_ov(void);
const void *bw_mod_title_return_table(void);

#ifdef __cplusplus
}
#endif
#endif
