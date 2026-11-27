/*
 * sdk/mod_menu.h -- the mod menu's model: the installed mods, the switch,
 * the clear action, the orphan report and a mod's own settings.
 *
 * What this file is
 * -----------------
 *
 * The model behind the port's own Mods section (docs/mod-services.md, "The
 * mod menu").  It answers the screen's questions -- how many mods are
 * installed, what the i-th one is called, whether it is on, what its
 * settings are, and which of the saved-data folders on the disk belong to a
 * mod that is no longer installed -- and it holds the state behind those
 * answers.  It draws nothing: the screen (engine/platform/mod_options_proc.c
 * and the screen's own model, engine/platform/settings_ui.c) draws what this
 * file answers.
 *
 * The rules the model lives by
 * ----------------------------
 *
 * The four constraints of docs/mod-rules.md, section 2, as docs/mod-state.md,
 * section 2 ("The in-game management constraints") states them:
 *
 *   1. The roots are rebuildable at the menu, not installed once at
 *      start-up.  bw_set_mod_root, bw_add_mod_root, bw_mod_set_order and
 *      bw_clear_mods set them, and every change drops the cached merged
 *      archives (bw_vfs_flush).  The state namespace is per mod, so a root
 *      rebuild moves no state.  Enabling a mod adds its root, and the load
 *      point reads its state.
 *   2. A code mod is disabled in order: the mod's state is flushed, then
 *      every (region, address) pair it installed is reverted to its
 *      original, and only then is its library unloaded.  The model drives
 *      that order; the loader (sdk/mod_loader.c) does the reverting and the
 *      unloading.
 *   3. A disabled mod's effects stay: its state file stays on the disk and
 *      the game bytes it wrote stay until the game writes over them.  The
 *      port restores nothing, and turning the mod on again reads its state
 *      again.
 *   4. The port starts with no mods: with no mod root there is no state
 *      read, the state folder is untouched, and BW_MOD_ROOT is read once.
 *      The model reads the port's list; it writes the list only when the
 *      player applies the menu, and at start-up only to restore the list
 *      the player chose last time (bw_mod_menu_start).
 *
 * What the model holds
 * --------------------
 *
 * The port's own record of the player's list is <save dir>/mods.ini: the
 * mods the player knows about, the state of their switches and the order
 * the player chose for them.  It is the port's file, written the way every
 * other port file is written (a whole file, atomically: sdk/save.h).  With
 * no record and no mod root the port starts with no mods, which is
 * constraint 4; with a record the player has already chosen, and the list
 * is restored -- the switches and the order.
 *
 * The order the player chose is the player's own, because the order the
 * mods load in is the order their overrides and their rules take effect
 * (docs/mod-services.md, "The mod menu": "The order can be changed").  The
 * manifest's `order` key is the default: it decides the list while the
 * player has not said otherwise.  The model holds the list in the order
 * the player chose, the apply hands that order to the file layer
 * (bw_mod_set_order, sdk/vfs.h) and the loader loads the mods in it.  The
 * state namespace is per mod, so moving a mod in the list moves no state.
 *
 * A mod's own settings (docs/mod-services.md, "Mod settings") are declared
 * in the file the manifest's `settings` key names and the player's choices
 * live beside the mod's saved data, in <save dir>/mods/<namespace>/.  The
 * mod reads them through the services table's setting_get, which this file
 * answers (bw_mod_menu_setting_get).
 *
 * The calls
 * ---------
 *
 * The model is one model for the whole process.  The port opens it when the
 * menu opens (bw_mod_menu_open), the screen asks it questions, the player
 * changes a switch (bw_mod_menu_set_enabled) or moves a mod
 * (bw_mod_menu_move_up and its siblings), and the changes are applied when
 * the player leaves the menu (bw_mod_menu_apply).  The port calls
 * bw_mod_menu_start once at start-up, before the loader runs.
 */

#ifndef BW_PORT_MOD_MENU_H
#define BW_PORT_MOD_MENU_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/*
 * The port's start-up: restore the list the player chose last time, if
 * there is one, and build the model.  The port calls this before the
 * loader runs (bw_mod_loader_load_all), so the mods the player left on are
 * loaded and their state is read at the load point.
 *
 * The restore is the player's own: the mods that are on, in the order the
 * player chose, are the list the port holds from then on.
 *
 * With no record of the player's own (no <save dir>/mods.ini) the port's
 * list stands as it is: the environment's BW_MOD_ROOT, or what an explicit
 * call set.  Nothing is written and no mod's state is read.
 */
void bw_mod_menu_start(void);

/*
 * The menu opens: rebuild the model from the port's list and the player's
 * record.  The list is not changed: the changes apply when the player
 * leaves the menu (bw_mod_menu_apply).
 *
 * The screen calls this once, when the Mods section is entered.  A rebuild
 * drops the switches the player has not applied yet, so it is not a call
 * for every frame.
 */
void bw_mod_menu_open(void);

/*
 * The installed mods folder: EIDOS_MODS_DIR, or "mods" beside the save
 * folder (<app data>/eidos/mods).  A folder in it with an eidos_mod.txt,
 * or a `.bw` package, is a mod; one the player has not seen joins the
 * list switched on.  0, or -1 when there is no such path.
 */
int bw_mod_menu_installed_folder(char *out, size_t size);

/*
 * Read the installed mods folder and every mod again and apply the list
 * at once, even with no switch changed: a mod whose files changed is
 * stopped and loaded again, and the cached merged archives are dropped.
 * Game data already in memory (the map on screen) shows the change the
 * next time the game loads it.  0, or -1 when a step failed.
 */
int bw_mod_menu_reload(void);

/* Forget the model (the port's power-on).  The files on the disk are
 * untouched. */
void bw_mod_menu_reset(void);

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

/* How many mods the menu shows. */
u32 bw_mod_menu_count(void);

/* The name the manifest gives the mod at `index`, or "" when there is
 * none. */
const char *bw_mod_menu_name_at(u32 index);

/* The mod's state namespace (docs/mod-state.md, section 1), or "" when
 * there is none. */
const char *bw_mod_menu_namespace_at(u32 index);

/* The path of the mod's settings declaration, relative to the mod's own
 * folder, or "" when the manifest names none. */
const char *bw_mod_menu_settings_at(u32 index);

/* The mod's own folder, or "" when there is none. */
const char *bw_mod_menu_root_at(u32 index);

/* The manifest's order key: the load order the port sorts by, and the
 * order a mod keeps when the player has not moved it.  It is the mod
 * author's own key (sdk/vfs.h, "The mod list"). */
int bw_mod_menu_order_at(u32 index);

/* The player's switch: 1 when the mod is on. */
int bw_mod_menu_enabled_at(u32 index);

/* 1 when the mod's root is in the port's list right now, so the mod runs
 * (or runs its rules) as it stands. */
int bw_mod_menu_running_at(u32 index);

/* 1 when the mod's own folder is there.  A mod the player knows about
 * whose folder is gone is still in the list, with its switch as it was. */
int bw_mod_menu_present_at(u32 index);

/* 0 when the mod works on the game that is loaded.  Else the game it is made
 * for (BW_MOD_GAME_BLACK or BW_MOD_GAME_WHITE, sdk/vfs.h), or -1 when its
 * `game` line names neither: the mod stays in the list and the port does not
 * load it, whatever its switch says. */
int bw_mod_menu_needs_game_at(u32 index);

/* The eidos version the mod needs (its `eidos` line, "0.2.9") when that is
 * newer than this one, else NULL: the port does not load it either. */
const char *bw_mod_menu_needs_eidos_at(u32 index);

/* 1 when the port does not load the mod whatever its switch says (made for
 * the other game, or for a newer eidos). */
int bw_mod_menu_unavailable_at(u32 index);

/* A line of the mod's manifest for a mod list: what 0 its `description`, 1
 * its `author`, 2 its `version`.  "" when the manifest has no such line. */
const char *bw_mod_menu_about_at(u32 index, int what);

/* The game's own mod manager (the MODS button of the title menu,
 * engine/ui/recipe_title.c) is offered: the start option --mods.  A game
 * the desktop launcher starts does not pass it (the launcher manages the
 * mods there); a phone or a console always does (host/main.c). */
void bw_mod_menu_set_manager(int on);
int  bw_mod_menu_manager(void);

/* 1 when the mod ships code, so it has a library the loader opens and
 * unloads. */
int bw_mod_menu_has_code_at(u32 index);

/* ------------------------------------------------------------------ */
/* The order                                                           */
/* ------------------------------------------------------------------ */

/*
 * The player's own order.
 *
 * The list the screen shows is in the order the player chose, and the
 * order the mods load in is the order their overrides and their rules take
 * effect, so it is the player's to choose (docs/mod-services.md, "The mod
 * menu": "The order can be changed").  The move calls change it, and the
 * change is held until the player leaves the menu, like the switches: the
 * mods that are running do not change until then.
 *
 * The manifest's `order` key is the default: a mod the player has not
 * moved keeps the place its author gave it (docs/modding.md, section 3).
 * The player's own order is in the port's record (<save dir>/mods.ini), so
 * it outlives the port, and the start-up restore reads it back.
 *
 * The state namespace is per mod, so a mod that moves in the list keeps
 * its own state: the namespace is the manifest's `state` key and it
 * travels with the mod.
 */

/* Move the mod at `index` one place towards the front of the list.  0 on
 * success, -1 when there is no such mod or it is already at the front. */
int bw_mod_menu_move_up(u32 index);

/* Move the mod at `index` one place towards the back of the list.  0 on
 * success, -1 when there is no such mod or it is already at the back. */
int bw_mod_menu_move_down(u32 index);

/* Move the mod at `index` to `to`, the place it takes in the list: 0 is
 * the front.  A `to` past the end of the list is the last place.  0 on
 * success, -1 when there is no such mod. */
int bw_mod_menu_move_to(u32 index, u32 to);

/* ------------------------------------------------------------------ */
/* The switch                                                          */
/* ------------------------------------------------------------------ */

/*
 * The player's switch.  The change is held until the player leaves the
 * menu; the mods that are running do not change until then.  0 on success,
 * -1 when there is no such mod.
 */
int bw_mod_menu_set_enabled(u32 index, int on);

/* Non-zero when the switches or the order differ from the port's list, so
 * there is something to apply. */
int bw_mod_menu_dirty(void);

/*
 * Apply the switches and the order: the changes apply when the player
 * leaves the menu.
 *
 * The order is the one docs/mod-state.md, section 2, constraint 2, fixes:
 * the mods' state is flushed, the mods that are running are stopped (the
 * loader reverts the slots they installed before it unloads their
 * libraries, the last loaded first), the roots are rebuilt from the
 * switches (which drops the cached merged archives) and the mods that are
 * wanted are loaded again, so the load point reads their state.  A mod's
 * own state file stays where it is, and so do the game bytes the mod wrote
 * (constraint 3).
 *
 * The roots are rebuilt in the order the player chose: the order is handed
 * to the file layer as it is (bw_mod_set_order, sdk/vfs.h), so the loader
 * loads the mods in it and a later mod wins over an earlier one as the
 * player chose it.
 *
 * Every running mod is stopped and loaded again, not only the ones that
 * are going off: the loader loads every mod in the list, so a mod that
 * stays on must not be in the list twice.
 *
 * 0 on success.  A mod whose folder cannot be added to the list is
 * reported and the rest of the list stands.
 */
int bw_mod_menu_apply(void);

/* Drop the player's changes: the switches go back to the port's list and
 * the order goes back to the one the list had when the menu was opened. */
void bw_mod_menu_revert(void);

/* ------------------------------------------------------------------ */
/* The clear action                                                    */
/* ------------------------------------------------------------------ */

/*
 * The player's "clear saved data" for one mod: the mod's own state file
 * goes and its folder stays (docs/mod-state.md, section 1, "Removal").  The
 * port does not delete a mod's folder when the mod is removed, so a
 * re-enable reads the file again.
 *
 * 0 when the state is clear (there was none, or it is gone), -1 when the
 * file could not be removed.
 */
int bw_mod_menu_clear(u32 index);

/* ------------------------------------------------------------------ */
/* The orphan report                                                   */
/* ------------------------------------------------------------------ */

/*
 * The folders in <save dir>/mods/ that no mod in the list claims: a mod
 * that was removed and whose folder was left behind (docs/mod-state.md,
 * section 1, "Removal").  The port reports them at the menu.
 */
u32 bw_mod_menu_orphan_count(void);

/* The name of the orphan at `index` (its state namespace), or NULL. */
const char *bw_mod_menu_orphan_at(u32 index);

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

/* The kinds an option can have (docs/mod-services.md, "Mod settings"). */
#define BW_MOD_SETTING_BOOL   0   /* on or off            */
#define BW_MOD_SETTING_CHOICE 1   /* one of a list of names */
#define BW_MOD_SETTING_INT    2   /* a number between a low and a high bound */
#define BW_MOD_SETTING_TEXT   3   /* a short text (a world's code, a room) */
#define BW_MOD_SETTING_SECRET 4   /* a password: never written, cleared when the
                                   * settings close (version 7)  */
#define BW_MOD_SETTING_BUTTON 5   /* a row with no value: the mod's event
                                   * setting.button (version 7) */
#define BW_MOD_SETTING_SHOW   6   /* a row whose value the mod sets
                                   * (setting_show), hidden while "" (version 7) */

/* How many options the mod at `index` declares. */
u32 bw_mod_menu_option_count(u32 index);

/* The option's key: the name the mod asks setting_get for. */
const char *bw_mod_menu_option_key(u32 index, u32 option);

/* The option's label: the text the screen shows. */
const char *bw_mod_menu_option_label(u32 index, u32 option);

/* The option's kind: BW_MOD_SETTING_BOOL, _CHOICE or _INT. */
int bw_mod_menu_option_kind(u32 index, u32 option);

/* The option's value: 0 or 1 for a bool, the index of the chosen name for
 * a choice.  The player's choice while it is in force, the default
 * otherwise. */
int bw_mod_menu_option_value(u32 index, u32 option);

/* The option's default, the value it has while the player has not chosen
 * one. */
int bw_mod_menu_option_default(u32 index, u32 option);

/* The player's choice for the option.  0 on success, -1 when there is no
 * such option or the value is not one it can take.  The choice is stored
 * beside the mod's saved data at once. */
int bw_mod_menu_option_set(u32 index, u32 option, int value);

/* An int option's bounds; 0 and 0 for the other kinds. */
int bw_mod_menu_option_min(u32 index, u32 option);
int bw_mod_menu_option_max(u32 index, u32 option);

/* How many names a choice option has. */
int bw_mod_menu_option_choice_count(u32 index, u32 option);

/* The name of the choice at `choice` (0 .. count - 1), or NULL. */
const char *bw_mod_menu_option_choice(u32 index, u32 option, int choice);

/* The option's value as text: "on" or "off" for a bool, the chosen name
 * for a choice.  Always NUL-terminated in `buf`. */
const char *bw_mod_menu_option_text(u32 index, u32 option, char *buf,
                                     size_t size);

/* A text option's new value (its longest is the option's max): 0, or -1
 * when it is not a text option, too long, or holds '"' or a line end. */
int bw_mod_menu_option_set_text(u32 index, u32 option, const char *text);

/* Whether the option is shown in the mod's mode `mode` (its `@id` word). */
int bw_mod_menu_option_in_mode(u32 index, u32 option, u32 mode);

/*
 * Version 7: a mod's own tabs of the settings screen (`tab "<title>"`
 * lines; docs/mod-services.md, "Mod settings").  tab_count/tab_title: its
 * tabs; option_tab: the tab an option is on, -1 for one under the mod's row
 * in the Mods section.  option_shown: its `?key=value` (or `!=`) holds and,
 * for a `show` row, the mod gave it a text.  option_word: a text of one
 * word (its keyboard has no SPACE).  press: a `button` row was chosen (the
 * event setting.button).  clear_secrets: every password is forgotten (the
 * settings closed).
 */
u32  bw_mod_menu_tab_count(u32 index);
const char *bw_mod_menu_tab_title(u32 index, u32 tab);
int  bw_mod_menu_option_tab(u32 index, u32 option);
int  bw_mod_menu_option_shown(u32 index, u32 option);
int  bw_mod_menu_option_word(u32 index, u32 option);
int  bw_mod_menu_press(u32 index, u32 option);
void bw_mod_menu_clear_secrets(void);
/* The calling mod's services (the namespace in force): a `show` row's text
 * (at most 63 bytes, no line end; "" hides it), and a text or secret
 * option's new value.  0, or -1. */
int  bw_mod_menu_setting_show(const char *key, const char *text);
int  bw_mod_menu_setting_set_text(const char *key, const char *text);

/*
 * The co-op lobby's rules: options tagged `@lobby` (bool, choice or int).
 * The host sets them in the lobby; while a session is open every player's
 * mod reads the host's value (setting_get), sent through the session's
 * shared table, and nobody's own choice is changed by it.
 *
 * in_lobby: the option is a rule.  rule_value: the value in force (the
 * host's in a session).  rules_locked: this game is a joiner in a session
 * (the rules are the host's to change).  rules_push: the host sends its
 * rules when one changed (called each frame of the lobby screen).
 */
int  bw_mod_menu_option_in_lobby(u32 index, u32 option);
int  bw_mod_menu_rule_value(u32 index, u32 option);
int  bw_mod_menu_rules_locked(void);
void bw_mod_menu_rules_push(void);

/* ------------------------------------------------------------------ */
/* The ways to play a mod offers (`mode` lines)                        */
/* ------------------------------------------------------------------ */

/*
 * A mod's settings file may declare modes:
 *
 *     mode solo "Solo randomizer" "A shuffled game for you alone."
 *     option arkh.goal choice "Goal" champion|elite4 @solo,multiworld
 *
 * The Multiplayer screen shows each mode as a card; its screen shows the
 * options tagged with it, and its Start begins a new game in a free save
 * slot.  During that new game's game.new the mod asks mode_get which mode
 * it was.  A mode marked `off` is shown and cannot be chosen.
 */
u32 bw_mod_menu_mode_count(u32 index);
const char *bw_mod_menu_mode_id(u32 index, u32 mode);
const char *bw_mod_menu_mode_title(u32 index, u32 mode);
const char *bw_mod_menu_mode_line(u32 index, u32 mode);
int bw_mod_menu_mode_off(u32 index, u32 mode);
/* The mod and mode an id names, among every mod's modes: 1, or 0. */
int bw_mod_menu_find_mode(const char *id, u32 *index, u32 *mode);

/* The next new game is in this mode (the screen's Start): 0, or -1. */
int bw_mod_menu_mode_begin(u32 index, u32 mode);
/* Test switch EIDOS_MOD_MODE=<id>: the first new game is in that mode.
 * The port calls it once the mod list is final (either start-up path). */
void bw_mod_menu_mode_from_env(void);
/* Drop the chosen mode (a Continue). */
void bw_mod_menu_mode_end(void);
/* game.new is about to run its handlers: the chosen mode is this new
 * game's; the next new game has none. */
void bw_mod_menu_mode_new_game(void);
/* The services table's mode_get: the chosen mode's id when it is the
 * calling mod's, else "".  The id's length, or -1. */
int bw_mod_menu_mode_get(char *out, u32 size);
/* 1 while a mode is chosen (from its card until the next new game or a
 * Continue), whichever mod's it is. */
int bw_mod_menu_mode_running(void);
/* The services table's setting_text: the text option's value.  Its length,
 * or -1 when the key names no text option of the calling mod. */
int bw_mod_menu_setting_text(const char *key, char *out, u32 size);

/*
 * A mode line's words after its line (docs/mod-services.md):
 * - `joins`: its games are worlds others join by code.  The Multiplayer
 *   screen's Join box starts such a mode with the code typed
 *   (mode_begin_join); the mod reads it with mode_code during game.new.
 * - `in=<mode>`: a type on that mode's card, not a card of its own
 *   (mode_in: the card mode's index, -1 for a card).
 * - `export "<game>"`: Start writes an Archipelago options file
 *   (mode_write_yaml) instead of a new game.
 */
int bw_mod_menu_mode_begin_join(u32 index, u32 mode, const char *code);
int bw_mod_menu_mode_joins(u32 index, u32 mode);
int bw_mod_menu_mode_in(u32 index, u32 mode);
const char *bw_mod_menu_mode_export(u32 index, u32 mode);
/* The first mode of an enabled mod that joins relay worlds (`joins`, no
 * `export`): 1, or 0 for none.  find_room: the one that joins Archipelago
 * rooms (`joins` and `export`); its mode_code is
 * "ap <host:port> <slot> <password>" ("-" for no password). */
int bw_mod_menu_find_joining(u32 *index, u32 *mode);
int bw_mod_menu_find_room(u32 *index, u32 *mode);
/* The services table's mode_code: the world code of this new game when the
 * mode is the calling mod's ("" for a new world).  Its length, or -1. */
int bw_mod_menu_mode_code(char *out, u32 size);
/* The options file of an `export` mode, from the options shown in it and
 * their `yaml` lines: <save folder>/archipelago/<name>.yaml.  version 'B'
 * or 'W' fills {version}.  0 and the file's path, or -1. */
int bw_mod_menu_mode_write_yaml(u32 index, u32 mode, const char *name, char version, char *path, u32 size);

/* An option's `heading` (a title above its row) and `help` (what it does):
 * "" for none. */
const char *bw_mod_menu_option_heading(u32 index, u32 option);
const char *bw_mod_menu_option_help(u32 index, u32 option);
/* An option's `yaml` line ("<name> <values>"), or NULL. */
const char *bw_mod_menu_option_yaml(u32 index, u32 option);

/* ------------------------------------------------------------------ */
/* The mod that is calling                                             */
/* ------------------------------------------------------------------ */

/*
 * The port tells the model which mod is calling, the way the loader tells
 * the saved-data layer (bw_mod_savedata_set_namespace): the model's
 * setting_get answers for that mod alone.  NULL or "" clears it.
 */
void bw_mod_menu_set_namespace(const char *ns);

/* The namespace of the mod that is calling, or "" when there is none. */
const char *bw_mod_menu_namespace(void);

/*
 * The services table's setting_get (docs/mod-services.md, "Mod settings"):
 * the value of the option the key names, for the mod that is calling.  -1
 * when the key names no option of that mod, or when no mod is calling.
 *
 * The loader binds this one, and it sets the
 * namespace around the calls it makes into the mod, the way it sets the
 * saved-data layer's (bw_mod_menu_set_namespace).
 */
int bw_mod_menu_setting_get(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_MENU_H */
