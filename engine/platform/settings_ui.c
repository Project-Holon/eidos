/*
 * engine/platform/settings_ui.c -- see settings_ui.h.
 */
#include "settings_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "mod_menu.h"
#include "vfs.h"                /* BW_MOD_GAME_*: the game a mod is made for */
#include "mod_net.h"
#include "mod_title.h"
#include "mp.h"
#include "save_slots.h"         /* a mode's new game takes a free slot */
#include "save_games.h"         /* the saves the Multiplayer screen made: Your games */
#include "mod_v6.h"             /* the player's multiplayer name (bw_mod_v6_player_name) */
#include "ui_lang.h"            /* labels joined with a value are translated in parts */
#include <ctype.h>
#include "port_keyboard.h"      /* the port's own keyboard (the one-screen interface) */

/* engine/ui/ui.c: the one-screen interface is drawing (then the port's own
 * keyboard takes the texts; the classic layout keeps the game's) */
#if defined(__GNUC__) || defined(__clang__)
extern int eidos_ui_enabled(void) __attribute__((weak));
#else
extern int eidos_ui_enabled(void);
#endif

/*
 * The port's own network settings (engine/platform/settings.c): the
 * relay's address, the relay's port, the control's port and the player's
 * name.  They are declared here because settings.h does not carry them
 * yet: the header is not this wave's file.  The screen shows them and the
 * player changes them there; the boot hands them to the network service
 * at the port's start-up (sdk/mod_net.h).
 */
extern const char *eidos_settings_net_relay(void);
extern void        eidos_settings_net_set_relay(const char *host);
extern int         eidos_settings_net_relay_port(void);
extern void        eidos_settings_net_set_relay_port(int port);
extern int         eidos_settings_net_control_port(void);
extern void        eidos_settings_net_set_control_port(int port);
extern const char *eidos_settings_net_name(void);
extern void        eidos_settings_net_set_name(const char *name);
extern void        eidos_settings_net_relay_text_begin(void);
extern void        eidos_settings_net_name_text_begin(void);
extern int         eidos_settings_net_text_editing(void);
extern int         eidos_settings_net_text_key(int scancode);
extern void        eidos_settings_net_text_cancel(void);
extern void        eidos_settings_net_defaults(void);
extern void        eidos_settings_net_revert(void);

/*
 * The port's own session (engine/platform/settings.c): the session's code
 * is a setting of the port's own, so it is read, changed, reverted and
 * committed with the rest of the settings, and the file keeps it
 * ([session], code).  The calls the screen makes on the peer session
 * service are the port's own too (sdk/mod_net.h, "The port's own
 * session").
 */

/*
 * The second code of a session: the one the player types to join a
 * session, kept apart from the code in force, so that hosting a session
 * does not overwrite what the player has typed.  It is a setting of the
 * port's own as well ([session], join_code).
 */

/*
 * The port's own run-status panel and the input injection's limit: the
 * settings' model holds them (engine/platform/settings.c), and the screen
 * shows them and the player changes them there.
 */

/*
 * The run restart (sdk/mod_restart.c has no header of its own, so the
 * prototype is here, the way the boot's is): the port's own restart.  The
 * request is recorded and the boot's frame loop carries it out, because
 * the reset clears the memory the game is running in.
 */
extern void bw_mod_restart(u32 flags);

/* ================================================================== */
/* UI TEXT: every string the settings screen shows.  ASCII only.       */
/* A translation or a mod replaces this block.                         */
/* ================================================================== */

#define STR_TITLE "Settings"
static const char *const k_tab_names[] = {
    "Display", "Layout", "Controls", "Sound", "Mods", "Network", "Extras",
    "Multiplayer"
};

/* The order of the tabs.  Multiplayer is a screen of its own, opened
 * from the title menu. */
static const int k_tab_order[] = { 0, 1, 2, 3, 4, 5, 6 };
#define TAB_STRIP 7

/* The mods' own tabs (`tab` lines, version 7) come after the port's: a
 * section number from TAB_MOD_BASE up, one per tab of a mod that is
 * running, in the mods' order. */
#define TAB_MOD_BASE 16
#define MODTAB_MAX   16
static struct { int mod, tab; } g_modtab[MODTAB_MAX];
static int g_modtab_n;

static void modtabs_build(void)
{
    u32 i, k;

    g_modtab_n = 0;
    for (i = 0; i < bw_mod_menu_count(); i++) {
        if (!bw_mod_menu_running_at(i)) {
            continue;
        }
        for (k = 0; k < bw_mod_menu_tab_count(i) && g_modtab_n < MODTAB_MAX; k++) {
            g_modtab[g_modtab_n].mod = (int)i;
            g_modtab[g_modtab_n].tab = (int)k;
            g_modtab_n++;
        }
    }
}

static int strip_count(void)
{
    return TAB_STRIP + g_modtab_n;
}

/* The section at place `pos` of the strip, and the place of section `t`. */
static int strip_tab(int pos)
{
    return pos < TAB_STRIP ? k_tab_order[pos] : TAB_MOD_BASE + (pos - TAB_STRIP);
}

static int strip_pos(int t)
{
    int i;

    if (t >= TAB_MOD_BASE) {
        return TAB_STRIP + (t - TAB_MOD_BASE);
    }
    for (i = 0; i < TAB_STRIP; i++) {
        if (k_tab_order[i] == t) {
            return i;
        }
    }
    return t;                   /* a section off the strip (Multiplayer): its own number */
}

/* forward: the section's name (the port's own, or a mod tab's title) */
static const char *tab_title(int t);

/* Short button names for the Controls rows (the backend labels are long). */
static const char *const k_button_short[] = {
    "A", "B", "X", "Y", "L", "R", "Start", "Select", "Up", "Down", "Left", "Right"
};

/* Short gamepad control names: the value column is narrow.  Indexes match
 * the backend's EIDOS_PADBIND_BUTTON order (SDL game controller buttons). */
static const char *const k_pad_short[] = {
    "Pad S", "Pad E", "Pad W", "Pad N", "Back", "Guide", "Start", "LS Click",
    "RS Click", "LB", "RB", "DP Up", "DP Down", "DP Left", "DP Right"
};
static const char *const k_axis_pos_short[] = { "LS Right", "LS Down", "RS Right", "RS Down", "LT", "RT" };
static const char *const k_axis_neg_short[] = { "LS Left", "LS Up", "RS Left", "RS Up", "LT -", "RT -" };

/* Help text (bottom screen).  Two lines per option id, then fixed texts. */
static const char *const k_option_help[EIDOS_OPT_COUNT][2] = {
    [EIDOS_OPT_DISPLAY_MODE]        = { "", "" },
    [EIDOS_OPT_DISPLAY_PIXEL_SCALE] = { "",
                                        "Auto fills the window." },
    [EIDOS_OPT_DISPLAY_RESOLUTION]  = { "",
                                        "Auto matches the window." },
    [EIDOS_OPT_LAYOUT_ARRANGEMENT]  = { "", "" },
    [EIDOS_OPT_LAYOUT_SWAP]         = { "", "" },
    [EIDOS_OPT_LAYOUT_GAP]          = { "", "" },
    [EIDOS_OPT_SOUND_VOLUME]        = { "", "" },
    [EIDOS_OPT_EXTRAS_INSTANT_TEXT] = { "",
                                        "" },
    [EIDOS_OPT_BATTLE_BACKDROP]     = { "Overworld map or original",
                                        "battle backgrounds." },
    [EIDOS_OPT_BATTLE_PLATFORMS]    = { "Original platforms under Pokemon.",
                                        "" },
    [EIDOS_OPT_MOVE_COLOURS]        = { "Colour move names by type",
                                        "in battle messages." },
    [EIDOS_OPT_WEATHER_HAZE]        = { "Fog, sand and snow also dim",
                                        "the Pokemon in battle." },
    [EIDOS_OPT_PAD_ICONS]           = { "Controller icons in hints.",
                                        "Auto matches the controller." },
    [EIDOS_OPT_OUTLINES]            = { "One screen pixel wide.",
                                        "" },
    [EIDOS_OPT_TRANSITIONS]         = { "Seamless skips the fade to black",
                                        "at doors and map changes." },
    [EIDOS_OPT_GPU_2D]              = { "Off renders on the CPU.",
                                        "" },
    [EIDOS_OPT_RENDERER]            = { "Native: Direct3D, Metal or",
                                        "Vulkan. Applies on restart." },
    [EIDOS_OPT_VSYNC]               = { "Syncs frames to display.",
                                        "Off uses native DS frame rate." },
    [EIDOS_OPT_UI_THEME]            = { "Applies to one-screen",
                                        "menus and battles." },
};
#define HELP_BIND1     "Bind a key or gamepad button."
#define HELP_BIND2     ""
#define HELP_DEFAULTS1 "Reset this page."
#define HELP_DEFAULTS2 ""
#define HELP_CAPTURE1  "Press a key or gamepad button."
#define HELP_CAPTURE2  "B: cancel"
#define HELP_SECTION_DISPLAY  "Window, scale and resolution."
#define HELP_SECTION_LAYOUT   "Screen arrangement."
#define HELP_SECTION_CONTROLS "Keyboard and gamepad."
#define HELP_SECTION_SOUND    ""
#define HELP_SECTION_MODS     ""
#define HELP_SECTION_NETWORK  "Relay and player name."
#define HELP_SECTION_SESSION  "Host or join a game."
#define HELP_SECTION_MOD_TAB  "Mod settings."
#define HELP_SECTION_EXTRAS   ""
#define HELP_KEYS_L0   "A: open"
#define HELP_KEYS_L0B  "B: back"
#define HELP_KEYS_L1   "L/R: tabs"
#define HELP_KEYS_L1B  "A: select   B: back"

/* The Mods section: the mods, their switches, their own settings, the
 * clear action and the folders no mod claims. */
#define HELP_MOD_SWITCH1 ""
#define HELP_MOD_FOR_WHITE  "This mod is for Pokemon White."
#define HELP_MOD_FOR_BLACK  "This mod is for Pokemon Black."
#define HELP_MOD_FOR_NONE   "Not for Pokemon Black or White."
#define HELP_MOD_NOT_LOADED "Not loaded in this game."
#define HELP_MOD_NEEDS_EIDOS  "Made for a newer eidos."
#define HELP_MOD_UPDATE_EIDOS "Update eidos to use it."
#define HELP_MOD_SWITCH2 "X/Y: move"
#define HELP_MOD_OPTION1 ""
#define HELP_MOD_OPTION2 ""
#define HELP_MOD_CLEAR1  "Delete the mod's saved data."
#define HELP_MOD_CLEAR2  ""
#define HELP_MOD_RELOAD1 "Reload the mods folder."
#define HELP_MOD_RELOAD2 ""
#define HELP_MOD_FOLDER1 ""
#define HELP_MOD_FOLDER2 ""
#define HELP_MOD_REVERT1 ""
#define HELP_MOD_REVERT2 ""
#define HELP_ORPHAN1     "Saved data from a removed mod."
#define HELP_ORPHAN2     ""

/* The Network section: the port's own settings for the network service. */
#define HELP_NET_RELAY1  "Use tls://host for encryption."
#define HELP_NET_RELAY2  ""
#define HELP_NET_RPORT1  ""
#define HELP_NET_RPORT2  "A: type"
#define HELP_NET_CPORT1  ""
#define HELP_NET_CPORT2  "A: type"
#define HELP_NET_NAME1   ""
#define HELP_NET_NAME2   "A: change"
#define HELP_NET_DEFAULTS1 "Reset this page."
#define HELP_NET_DEFAULTS2 ""
#define HELP_TEXT_EDIT1  "Type on the keyboard."
#define HELP_TEXT_EDIT2  "Enter: done   Esc: cancel"

/* The Session section: the port's own peer session (sdk/mod_net.h, "The
 * port's own session"). */
#define HELP_SESSION_CODE1  ""
#define HELP_SESSION_CODE2  "A: copy"
#define STR_COPIED          "copied"
#define HELP_SESSION_JCODE1 ""
#define HELP_SESSION_JCODE2 "A: type"
#define HELP_SESSION_HOST1  ""
#define HELP_SESSION_HOST2  ""
#define HELP_SESSION_JOIN1  ""
#define HELP_SESSION_JOIN2  ""
#define HELP_SESSION_LEAVE1 ""
#define HELP_SESSION_LEAVE2 ""
#define HELP_SESSION_SELF1  "You."
#define HELP_SESSION_SELF2  ""
#define HELP_SESSION_PEER1  ""
#define HELP_SESSION_PEER2  ""
#define HELP_MP_START1      ""
#define HELP_MP_START2      ""
#define HELP_MP_PLAYER1     ""
#define HELP_MP_PLAYER2     ""
#define HELP_MP_GAME1       "Host this game."
#define HELP_MP_GAME2       ""
#define HELP_MP_NEW1        "Host a new game."
#define HELP_MP_NEW2        "You name it first."
#define HELP_SESSION_REVERT1 ""
#define HELP_SESSION_REVERT2 ""

/* The run restart (the Mods section) and the menu's own question. */
#define HELP_MOD_RESTART1 "Back to the title screen."
#define HELP_MOD_RESTART2 ""
#define HELP_Q_CHOOSE1 "Left/Right: choose"
#define HELP_Q_CHOOSE2 "A: confirm   B: cancel"

/* The run-status panel (the Mods section) and the input injection's limit
 * (the Network section). */
#define HELP_PANEL1   ""
#define HELP_PANEL2   ""
#define HELP_INJECT1  "Max input press (frames)."
#define HELP_INJECT2  ""

/* Row labels by option id (EIDOS_OPT_*); NULL for hidden options. */
static const char *const k_option_labels[EIDOS_OPT_COUNT] = {
    [EIDOS_OPT_DISPLAY_MODE]        = "Window mode",
    [EIDOS_OPT_DISPLAY_PIXEL_SCALE] = "Pixel scale",
    [EIDOS_OPT_DISPLAY_RESOLUTION]  = "3D resolution",
    /* width, height, maximized: hidden, NULL */
    [EIDOS_OPT_LAYOUT_ARRANGEMENT]  = "Screen layout",
    [EIDOS_OPT_LAYOUT_SWAP]         = "Swap screens",
    [EIDOS_OPT_LAYOUT_GAP]          = "Screen gap",
    [EIDOS_OPT_SOUND_VOLUME]        = "Master volume",
    [EIDOS_OPT_EXTRAS_INSTANT_TEXT] = "Instant text",
    [EIDOS_OPT_BATTLE_BACKDROP]     = "Battle backdrop",
    [EIDOS_OPT_BATTLE_PLATFORMS]    = "Battle platforms",
    [EIDOS_OPT_MOVE_COLOURS]        = "Move colours",
    [EIDOS_OPT_WEATHER_HAZE]        = "Weather haze",
    [EIDOS_OPT_PAD_ICONS]           = "Button icons",
    [EIDOS_OPT_OUTLINES]            = "3D outlines",
    [EIDOS_OPT_TRANSITIONS]         = "Map transitions",
    [EIDOS_OPT_GPU_2D]              = "GPU drawing",
    [EIDOS_OPT_RENDERER]            = "Renderer",
    [EIDOS_OPT_VSYNC]               = "VSync",
    [EIDOS_OPT_UI_THEME]            = "Interface style",
};

/* Value names that differ from the backend's lowercase choice names:
 * {option id, choice index, text}. */
typedef struct ValueName { int id; int index; const char *text; } ValueName;
static const ValueName k_value_names[] = {
    { EIDOS_OPT_UI_THEME,            0, "Kosmos" },
    { EIDOS_OPT_UI_THEME,            1, "Plasma" },
    { EIDOS_OPT_UI_THEME,            2, "Harmonia" },
    { EIDOS_OPT_RENDERER,            0, "OpenGL" },
    { EIDOS_OPT_RENDERER,            1, "Native" },
    { EIDOS_OPT_VSYNC,               0, "Off" },
    { EIDOS_OPT_VSYNC,               1, "On" },
    { EIDOS_OPT_GPU_2D,              0, "Off" },
    { EIDOS_OPT_GPU_2D,              1, "On" },
    { EIDOS_OPT_DISPLAY_MODE,        0, "Windowed" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Fullscreen" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Borderless" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Side by side" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  2, "One screen" },
    { EIDOS_OPT_LAYOUT_SWAP,         0, "Off" },
    { EIDOS_OPT_LAYOUT_SWAP,         1, "On" },
    { EIDOS_OPT_EXTRAS_INSTANT_TEXT, 0, "Off" },
    { EIDOS_OPT_EXTRAS_INSTANT_TEXT, 1, "On" },
    { EIDOS_OPT_BATTLE_BACKDROP,     0, "Real map" },
    { EIDOS_OPT_BATTLE_BACKDROP,     1, "Original" },
    { EIDOS_OPT_BATTLE_PLATFORMS,    0, "Off" },
    { EIDOS_OPT_BATTLE_PLATFORMS,    1, "On" },
    { EIDOS_OPT_MOVE_COLOURS,        0, "Off" },
    { EIDOS_OPT_MOVE_COLOURS,        1, "On" },
    { EIDOS_OPT_WEATHER_HAZE,        0, "Off" },
    { EIDOS_OPT_WEATHER_HAZE,        1, "On" },
    { EIDOS_OPT_PAD_ICONS,           0, "Auto" },
    { EIDOS_OPT_PAD_ICONS,           1, "Xbox 360" },
    { EIDOS_OPT_PAD_ICONS,           2, "Xbox One" },
    { EIDOS_OPT_PAD_ICONS,           3, "PS3" },
    { EIDOS_OPT_PAD_ICONS,           4, "PS4" },
    { EIDOS_OPT_PAD_ICONS,           5, "PS5" },
    { EIDOS_OPT_PAD_ICONS,           6, "Switch" },
    { EIDOS_OPT_PAD_ICONS,           7, "GameCube" },
    { EIDOS_OPT_PAD_ICONS,           8, "Steam Deck" },
    { EIDOS_OPT_OUTLINES,            0, "Off" },
    { EIDOS_OPT_OUTLINES,            1, "Thin" },
    { EIDOS_OPT_TRANSITIONS,         0, "Original" },
    { EIDOS_OPT_TRANSITIONS,         1, "Fast" },
    { EIDOS_OPT_TRANSITIONS,         2, "Seamless" },
};

/* Short names for the side-by-side choices (a choice has about 48 pixels). */
static const ValueName k_choice_names[] = {
    { EIDOS_OPT_DISPLAY_MODE,        0, "Window" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Full" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Border" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Wide" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  2, "One" },
};

#define STR_AUTO         "Auto"
#define STR_BACK_L1     "Back"
#define STR_MORE_BELOW  "More below"
#define STR_MORE_ABOVE  "More above"
#define STR_MORE_BOTH   "More above and below"
#define HELP_BACK_L0    "Exit."
#define HELP_BACK_L1    ""
#define STR_DEFAULTS    "Restore defaults"
#define STR_PRESS_KEY   "Press a key"
#define STR_UNBOUND     "None"

/* The Mods and Network sections' own labels. */
#define STR_MOD_CLEAR   "  Clear saved data"
#define STR_MOD_REVERT  "Revert changes"
#define STR_MOD_RELOAD  "Reload mods"
#define STR_MOD_FOLDER  "Mods folder"
#define STR_ORPHAN      "Unused"
#define STR_NET_RELAY   "Relay address"
#define STR_NET_RPORT   "Relay port"
#define STR_NET_CPORT   "Control port"
#define STR_NET_NAME    "Your name"
#define STR_NONE        "(none)"

/* The Session section's own labels. */
#define STR_SESSION_CODE     "Code"
#define STR_MP_START         "Start game"
#define STR_MP_READY         "Ready"
#define STR_MP_RUNNING       "Playing"
#define STR_MP_MISSING       "offline"
#define STR_MP_MODS          ", mods differ"
#define STR_SESSION_JOINCODE "Code"
#define STR_MP_CONNECTING    "Connecting..."
#define STR_MP_WAIT_HOST     "Waiting for host"
#define STR_MP_NOT_FOUND     "Code not found"
#define STR_MP_DEFAULT_NAME  "Player"
#define STR_SESSION_HOST     "Host"
#define STR_SESSION_JOIN     "Join"
#define STR_SESSION_LEAVE    "Leave"
#define STR_SESSION_SELF     "You"
#define STR_SESSION_PEER     "Player"
#define STR_SESSION_REVERT   "Revert changes"
#define STR_MP_NEW_GAME      "New game"
#define STR_MP_MODE_OFF      "Unavailable"
#define STR_MP_MODE_START    "Start new game"
#define STR_MP_MODE_MAKE     "Create world"
#define STR_MP_MODE_EXPORT   "Save options file"
#define STR_MP_MODE_TYPE     "Type"
#define STR_MP_SAVED         "Saved. Path copied."
#define STR_MP_NOT_SAVED     "Could not save file"
#define STR_MP_CHECKING      "Checking code..."
#define STR_MP_NO_CODE       "Unknown game or world code"
#define STR_MP_NO_ANSWER     "Relay did not respond"
#define STR_MP_HOST_BUSY     "Still connecting. Try again in a moment."
#define STR_MP_NO_PASTE      "No game code in clipboard"
#define STR_MP_WORLD_JOIN    "Join world"
#define STR_MP_WORLD_WAITING "Not opened by host yet"
#define STR_MP_WORLD_CLOSED  "Closed by host"
#define STR_MP_WORLD_BROKEN  "Unavailable on relay"
#define STR_MP_WORLD_FULL    "All player slots taken"
#define STR_MP_ROOM_NO_MOD   "Requires an Archipelago mod"
#define STR_MP_ROOM_JOIN     "Join room"
#define STR_MP_WORLD_IS_WHITE "World requires Pokemon White"
#define STR_MP_WORLD_IS_BLACK "World requires Pokemon Black"
#define STR_MP_WORLD_NO_MOD  "Requires a mod to join worlds"
#define STR_MP_WORLD_PLAYERS "Players"
#define STR_MP_WORLD_GAME    "Game"
#define STR_KB_NAME          "Your name?"
#define STR_KB_CODE          "Game code?"
#define STR_KB_NEW_GAME      "New game name?"

/* The run restart (the Mods section) and the menu's own question. */
#define STR_MOD_RESTART "Restart run"
#define STR_Q_TITLE     "Confirm"
#define STR_Q_YES       "Yes"
#define STR_Q_NO        "No"
#define STR_Q_CANCEL    "Cancel"

/* What the session's rows say about the work: the relay takes up to
 * three seconds to answer, so the row says so while it waits. */
#define STR_SESSION_WAIT "Waiting..."
#define STR_SESSION_IN   "In a game"
#define STR_SESS_OK      "OK"
#define STR_SESS_NORELAY "No relay"
#define STR_SESS_BUSY    "Busy"
#define STR_SESS_NOSESS  "No session"
#define STR_SESS_REFUSED "Refused"
#define STR_SESS_BIG     "Too big"
#define STR_SESS_IO      "No reply"
#define STR_SESS_ARG     "Bad code"
#define STR_SESS_FAILED  "Failed"

/* The port's own rows: the run-status panel's switch (the Mods section)
 * and the input injection's limit (the Network section). */
#define STR_PANEL       "Status panel"
#define STR_INJECT      "Max press"
#define STR_OFF         "Off"
#define STR_ON          "On"
#define STR_UNAVAILABLE "Unavailable"   /* a mod made for the other game */

/* The step the injection's limit moves by: half a second at the DS's 60
 * fps.  The model clamps the value to the port's own ceiling (a minute),
 * so the row never writes a value the port will not take. */
#define INJECT_STEP     30

/* ================================================================== */

/* A text into a buffer the screen hands out. */
static void put(char *dst, int n, const char *s)
{
    if (n > 0) {
        snprintf(dst, (size_t)n, "%.*s", n - 1, s);
    }
}

/*
 * The rows of the screen.  The first four kinds are the port's own
 * settings; the rest are the Mods section (the port's own panel switch and
 * the model of sdk/mod_menu.c), the Network section (the port's own
 * network settings and the input injection's limit) and the Session
 * section (the port's own peer session).
 */
enum {
    ROW_OPTION,           /* an option of the port's own                  */
    ROW_BIND,             /* a key binding                               */
    ROW_DEFAULTS,         /* "Restore defaults"                          */
    ROW_SECTION,          /* a section of the list                       */
    ROW_PANEL,            /* the run-status panel's switch               */
    ROW_MOD_SWITCH,       /* a mod's switch: arg is the mod              */
    ROW_MOD_OPTION,       /* a mod's own setting: arg the mod, arg2 it  */
    ROW_MOD_CLEAR,        /* "Clear saved data": arg is the mod          */
    ROW_MOD_REVERT,       /* the switches back to the port's list        */
    ROW_MOD_RELOAD,       /* read the mods folder again, restart the mods */
    ROW_MOD_FOLDER,       /* where the installed mods live (shown only)   */
    ROW_MOD_RESTART,      /* the run restart (the port's own)            */
    ROW_ORPHAN,           /* a saved-data folder no installed mod claims */
    ROW_NET_RELAY,        /* the relay's address (text)                  */
    ROW_NET_RELAY_PORT,   /* the relay's port (a number)                 */
    ROW_NET_CONTROL_PORT, /* the control's port (a number)               */
    ROW_NET_NAME,         /* the player's name (text)                    */
    ROW_INJECT_MAX,       /* the longest press the injection allows     */
    ROW_NET_DEFAULTS,     /* the network settings back to their default  */
    ROW_SESSION_CODE,     /* the code in force (read only)               */
    ROW_SESSION_JOINCODE, /* the code the player types to join with      */
    ROW_SESSION_HOST,     /* start a session                             */
    ROW_SESSION_JOIN,     /* join a session                              */
    ROW_SESSION_LEAVE,    /* leave the session                           */
    ROW_SESSION_SELF,     /* this player's own number in the session     */
    ROW_SESSION_PEER,     /* a peer of the session: arg is its number    */
    ROW_MP_START,         /* the host starts the game (sdk/mp.h)          */
    ROW_MP_PLAYER,        /* a player of the lobby: arg is its peer       */
    ROW_MP_STATUS,        /* a line of the Multiplayer tab: arg STATUS_*  */
    ROW_MP_JOIN_GO,       /* join with the code typed above               */
    ROW_MP_GAME,          /* a game the host kept: arg is its index       */
    ROW_MP_NEW_GAME,      /* a new game, named first                      */
    ROW_SESSION_REVERT,   /* the codes back to the committed state       */
    ROW_SESSION_DEFAULTS, /* the codes back to nothing                    */
    ROW_MP_MODE,          /* a way to play a mod offers: arg the mod, arg2 the mode */
    ROW_MP_MODE_START,    /* the mode's new game                          */
    ROW_MP_MODE_TYPE,     /* the card's types (its `in=` modes): the one in force */
    ROW_MP_WORLD_LINE,    /* a line about the world a code names: arg it */
    ROW_MP_WORLD_JOIN,    /* join that world: a new game in a free slot   */
    ROW_MP_YOUR_GAME,     /* Your games: arg its place (eidos_sui_mp_game) */
    ROW_MP_ROOM_LINE,     /* an Archipelago room's server (read only)      */
    ROW_MP_ROOM_SLOT,     /* the slot name to join as (text)              */
    ROW_MP_ROOM_PASS,     /* the room's password (text, may be empty)     */
    ROW_MP_ROOM_JOIN      /* join the room: a new game in a free slot     */
};

typedef struct Row { int type; int arg; int arg2; } Row;

#define TAB_COUNT 8
#define TAB_MODS 4
#define TAB_NETWORK 5
#define TAB_EXTRAS 6
#define TAB_SESSION 7

static struct {
    int level;        /* 0 section list, 1 inside a section      */
    int tab;          /* the section                             */
    int cursor;       /* index into the tab's rows                */
    int scroll;       /* first visible row                        */
    int capturing;    /* button index + 1, 0 when not capturing   */
    int editing;      /* a text of the Network section is edited  */
    int mods_open;    /* the Mods section was entered             */
    int wait_release; /* a capture just ended: ignore buttons until all are up */
    int hold_dir;     /* held direction bit being repeated        */
    int hold_frames;
    Row *rows;        /* the rows of the section                  */
    int nrows;
    int rowcap;
} g;

/* One more row.  The list grows as it needs: the Mods section holds a
 * row per mod, per setting and per folder. */
static void add_row(int type, int arg, int arg2)
{
    if (g.nrows >= g.rowcap) {
        int cap = g.rowcap != 0 ? g.rowcap * 2 : 32;
        Row *rows = (Row *)realloc(g.rows, (size_t)cap * sizeof(Row));

        if (rows == NULL) {
            return;                 /* the row is dropped */
        }
        g.rows = rows;
        g.rowcap = cap;
    }
    g.rows[g.nrows].type = type;
    g.rows[g.nrows].arg = arg;
    g.rows[g.nrows].arg2 = arg2;
    g.nrows++;
}

/*
 * The Mods section: the port's own panel switch, then the installed mods in
 * load order, each with its switch, its own settings under its name and the
 * clear action; then the folders in the save directory that no installed mod
 * claims.
 */
static void build_mods_rows(void)
{
    u32 count = bw_mod_menu_count();
    u32 i, o;

    /* The run-status panel is the port's own: the mods fill it, and the
     * player hides it from here (docs/mod-services.md, "Run-status panel"). */
    add_row(ROW_PANEL, 0, 0);
    for (i = 0; i < count; i++) {
        add_row(ROW_MOD_SWITCH, (int)i, 0);
        for (o = 0; o < bw_mod_menu_option_count(i); o++) {
            if (bw_mod_menu_option_tab(i, o) < 0) {
                add_row(ROW_MOD_OPTION, (int)i, (int)o);
            }
        }
        if (bw_mod_menu_namespace_at(i)[0] != '\0') {
            add_row(ROW_MOD_CLEAR, (int)i, 0);
        }
    }
    for (i = 0; i < bw_mod_menu_orphan_count(); i++) {
        add_row(ROW_ORPHAN, (int)i, 0);
    }
    /* The installed mods folder and the reload: a mod dropped into the
     * folder, or a mod whose files changed, is picked up without leaving
     * the game. */
    add_row(ROW_MOD_RELOAD, 0, 0);
    add_row(ROW_MOD_FOLDER, 0, 0);
    add_row(ROW_MOD_REVERT, 0, 0);
    /* The run restart: the mod layer's own restart (sdk/mod_restart.c) is
     * the port's own restart, and the menu offers it beside the mod
     * layer's other rows.  A restart ends the run, so the player is asked
     * first (the menu's own question). */
    add_row(ROW_MOD_RESTART, 0, 0);
}

/* The Network section: the port's own settings for the network service, and
 * the input injection's limit, which is the port's own too: a mod may ask
 * for a press, and the player says how long a press may be (0 is off). */
static void build_net_rows(void)
{
    add_row(ROW_NET_RELAY, 0, 0);
    add_row(ROW_NET_RELAY_PORT, 0, 0);
    add_row(ROW_NET_CONTROL_PORT, 0, 0);
    add_row(ROW_INJECT_MAX, 0, 0);
    add_row(ROW_NET_DEFAULTS, 0, 0);
}

/*
 * The Multiplayer tab.  Before a game: your name, Host and Join.  Host
 * lists the games the host kept and a new one; Join asks for the code.  In
 * a game: the code, the players, and Start (the host) or the wait for the
 * host.
 */
#define MP_VIEW_CHOOSE 0
#define MP_VIEW_JOIN   1
#define MP_VIEW_HOST   2
#define MP_VIEW_MODE   3   /* a mod's way to play: its options and Start */
#define MP_VIEW_WORLD  4   /* a world a typed code names: its rules, Join */
#define MP_VIEW_ROOM   5   /* an Archipelago room a typed port names    */

/* The Archipelago room a typed port names: the server (EIDOS_AP_HOST or
 * archipelago.gg), the slot to join as (the player's name to start with)
 * and the room's password. */
static struct {
    char host[64];
    char port[8];
    char slot[24];
    char pass[24];
    char said[64];
} g_room;

/* The mode the Mode view shows (sdk/mod_menu.h, "The ways to play a mod
 * offers"), the card it was opened from (its `in=` modes are its types),
 * and what its Start last said (no free save slot, the options file). */
static int g_mode_mod = -1;
static int g_mode_k = -1;
static int g_mode_card = -1;
static char g_mode_said[96];

/* The code typed in the Join box, asked about before joining (metaxy's
 * code lookup): busy while the relay answers; what it said when the code
 * names nothing; the world it names (WORLD view). */
static struct {
    int  busy;
    char code[17];
    char said[64];
    /* the world: its state, free places, game, players and rules */
    char state[12];
    int  free;
    char game[8];
    int  nplayers;
    char players[16][24];
    u8   claimed[16];
    int  nlines;
    char line_label[24][48];
    char line_value[24][48];
    u32  join_mod, join_mode;
    int  can_join;
} g_look;

/* A Your games entry to go on with, and a new co-op game to name, asked
 * for from the hub (done at the screen's next step). */
static int g_req_continue = -1;
static int g_req_coop_new;
static int g_req_paste;                /* (hub) join with the clipboard's code, at the next step */
/* The hosting under way was asked for on the hub (a game of Your games):
 * if it fails the player is put back on the hub, with the reason. */
static int g_host_from_hub;
/* Your games' list as last read, and the screen's steps (defined with the
 * list, below) */
static int g_gl_n;
static unsigned g_mp_tick;

/* The host's games, read when the Host view opens (as many as there are). */
static char (*g_games)[BW_MP_GAME_NAME_MAX];
static int  g_ngames;

/* The host's games into a list grown to hold them all: how many. */
static int read_mp_games(char (**list)[BW_MP_GAME_NAME_MAX])
{
    int n = bw_mp_game_count();
    char (*grown)[BW_MP_GAME_NAME_MAX];

    if (n <= 0) {
        return 0;
    }
    grown = (char (*)[BW_MP_GAME_NAME_MAX])realloc(*list, (size_t)n * BW_MP_GAME_NAME_MAX);
    if (grown == NULL) {
        return 0;
    }
    *list = grown;
    return bw_mp_game_list(grown, n);
}

/* The game's keyboard: what it is open for (KB_*), and how. */
#define KB_NONE     0
#define KB_NAME     1
#define KB_CODE     2
#define KB_NEW_GAME 3
#define KB_MOD_TEXT 4   /* a mod's text option: g_kb.mod, g_kb.opt */
#define KB_AP_SLOT  5   /* an Archipelago room's slot name */
#define KB_AP_PASS  6   /* and its password */

/* The game's keyboard takes at most this many letters (decision 20 of the
 * mod API's version 6: the name entry's limit). */
#define KB_LIMIT 10

static struct {
    int  target;
    int  kind;      /* the name entry's kind: 0 a player, 4 a plain text */
    int  max;
    int  mod, opt;  /* KB_MOD_TEXT: the option */
    char text[EIDOS_PKB_MAX + 8];
} g_kb;

#define STATUS_CONNECTING 0
#define STATUS_WAIT_HOST  1
#define STATUS_ENDED      2

static int g_mp_view;
static int g_copied;           /* the code was just copied */

/*
 * What the screen shows of the port's own session.  The state itself is
 * the port's own (sdk/mod_net.h, "The port's own session"); the screen
 * keeps the last answer, so it can say what happened.
 */
static struct {
    int busy;        /* the work is running                     */
    int have;        /* there is an answer                      */
    int rc;          /* the answer of the last work             */
    int host;        /* the last work was a host                */
    u32 stamp;       /* the peer list's stamp: it changes when the list does */
    int view;        /* the view the rows were built for */
} g_sess;

static int mp_in_game(void)
{
    BwMpState st = bw_mp_state();

    return st != BW_MP_OFF && st != BW_MP_ENDED;
}

static void build_session_rows(void)
{
    u32 peers;
    u32 i;

    if (mp_in_game()) {
        add_row(ROW_SESSION_CODE, 0, 0);
        peers = bw_mp_player_count();
        for (i = 0; i < peers; i++) {
            BwMpPlayer p;

            if (bw_mp_player_at(i, &p) != 0) {
                break;
            }
            add_row(ROW_MP_PLAYER, (int)p.peer, (int)i);
        }
        /* the session's rules: the multiplayer mods' `@lobby` options */
        {
            u32 m, o;

            for (m = 0; m < bw_mod_menu_count(); m++) {
                if (!bw_mod_menu_enabled_at(m)) {
                    continue;
                }
                for (o = 0; o < bw_mod_menu_option_count(m); o++) {
                    if (bw_mod_menu_option_in_lobby(m, o)) {
                        add_row(ROW_MOD_OPTION, (int)m, (int)o);
                    }
                }
            }
        }
        if (bw_mp_is_host()) {
            add_row(ROW_MP_START, 0, 0);
        } else {
            add_row(ROW_MP_STATUS, STATUS_WAIT_HOST, 0);
        }
        add_row(ROW_SESSION_LEAVE, 0, 0);
        return;
    }
    if (bw_mp_state() == BW_MP_ENDED) {
        add_row(ROW_MP_STATUS, STATUS_ENDED, 0);
    }
    if (g_sess.busy) {
        add_row(ROW_MP_STATUS, STATUS_CONNECTING, 0);
        return;
    }
    if (g_mp_view == MP_VIEW_JOIN) {
        add_row(ROW_SESSION_JOINCODE, 0, 0);
        add_row(ROW_MP_JOIN_GO, 0, 0);
        return;
    }
    if (g_mp_view == MP_VIEW_HOST) {
        int i;

        for (i = 0; i < g_ngames; i++) {
            add_row(ROW_MP_GAME, i, 0);
        }
        add_row(ROW_MP_NEW_GAME, 0, 0);
        return;
    }
    if (g_mp_view == MP_VIEW_ROOM) {
        add_row(ROW_MP_ROOM_LINE, 0, 0);
        add_row(ROW_MP_ROOM_SLOT, 0, 0);
        add_row(ROW_MP_ROOM_PASS, 0, 0);
        add_row(ROW_MP_ROOM_JOIN, 0, 0);
        return;
    }
    if (g_mp_view == MP_VIEW_WORLD) {
        int i;

        for (i = 0; i < g_look.nlines; i++) {
            add_row(ROW_MP_WORLD_LINE, i, 0);
        }
        add_row(ROW_MP_WORLD_JOIN, 0, 0);
        return;
    }
    if (g_mp_view == MP_VIEW_MODE) {
        u32 o, k;
        int types = 0;

        /* the card's types first (Multiworld or Archipelago) */
        for (k = 0; k < bw_mod_menu_mode_count((u32)g_mode_mod); k++) {
            types += bw_mod_menu_mode_in((u32)g_mode_mod, k) == g_mode_card;
        }
        if (types > 0) {
            add_row(ROW_MP_MODE_TYPE, g_mode_mod, g_mode_k);
        }
        for (o = 0; o < bw_mod_menu_option_count((u32)g_mode_mod); o++) {
            if (bw_mod_menu_option_in_mode((u32)g_mode_mod, o, (u32)g_mode_k)) {
                add_row(ROW_MOD_OPTION, g_mode_mod, (int)o);
            }
        }
        add_row(ROW_MP_MODE_START, 0, 0);
        return;
    }
    add_row(ROW_NET_NAME, 0, 0);
    add_row(ROW_SESSION_HOST, 0, 0);
    add_row(ROW_SESSION_JOIN, 0, 0);
    /* the ways to play the mods that are on offer (their `mode` lines) */
    {
        u32 i, k;

        for (i = 0; i < bw_mod_menu_count(); i++) {
            if (!bw_mod_menu_enabled_at(i) || !bw_mod_menu_present_at(i) || bw_mod_menu_unavailable_at(i)) {
                continue;
            }
            for (k = 0; k < bw_mod_menu_mode_count(i); k++) {
                if (bw_mod_menu_mode_in(i, k) < 0) {
                    add_row(ROW_MP_MODE, (int)i, (int)k);   /* a card; its types are inside */
                }
            }
        }
    }
    /* Your games: what was started here goes on from here (the one-screen
     * view draws them in a list of its own) */
    {
        int k, n = eidos_sui_mp_games();

        for (k = 0; k < n; k++) {
            add_row(ROW_MP_YOUR_GAME, k, 0);
        }
    }
}

/* The mod option KB_MOD_TEXT opens for (keyboard_open clears g_kb). */
static int g_kb_mod_next, g_kb_opt_next;

/* The game's keyboard opens for a text: the frame's result asks for it. */
static int keyboard_open(int target)
{
    const char *start = "";

    memset(&g_kb, 0, sizeof g_kb);
    g_kb.target = target;
    g_kb.kind = 4;
    switch (target) {
    case KB_NAME:
        /* a plain text, not the game's player name (kind 0): the game
         * refuses an empty player name, so a player who opened the
         * multiplayer screen could not leave it without typing one; OK on
         * an empty text closes the keyboard and keeps the name as it was */
        g_kb.max = 7;               /* the game's own length for a name */
        start = eidos_settings_net_name();
        break;
    case KB_CODE:
        g_kb.max = 6;               /* the relay's codes */
        start = eidos_settings_session_join_code();
        break;
    case KB_MOD_TEXT: {
        static char now[128];

        g_kb.mod = g_kb_mod_next;
        g_kb.opt = g_kb_opt_next;
        g_kb.max = bw_mod_menu_option_max((u32)g_kb.mod, (u32)g_kb.opt);
        start = bw_mod_menu_option_text((u32)g_kb.mod, (u32)g_kb.opt, now, sizeof now);
        break;
    }
    case KB_AP_SLOT:
        g_kb.max = (int)sizeof g_room.slot - 1;
        start = g_room.slot;
        break;
    case KB_AP_PASS:
        g_kb.max = (int)sizeof g_room.pass - 1;
        start = g_room.pass;
        break;
    default:
        g_kb.max = 10;
        break;
    }
    if (eidos_ui_enabled != NULL && eidos_ui_enabled()) {
        /* the one-screen interface: the port's own keyboard, as long as the
         * text may be (up to EIDOS_PKB_MAX) */
        const char *title = "NAME", *sub = "", *prompt = eidos_sui_keyboard_prompt();
        int flags = 0;

        if (target == KB_MOD_TEXT) {
            const char *head = bw_mod_menu_option_heading((u32)g_kb.mod, (u32)g_kb.opt);
            const char *help = bw_mod_menu_option_help((u32)g_kb.mod, (u32)g_kb.opt);

            title = head != NULL && head[0] != '\0' ? head : tab_title(g.tab);
            sub = bw_mod_menu_option_label((u32)g_kb.mod, (u32)g_kb.opt);
            if (help != NULL && help[0] != '\0') {
                prompt = help;
            }
            if (bw_mod_menu_option_kind((u32)g_kb.mod, (u32)g_kb.opt) == BW_MOD_SETTING_SECRET) {
                flags |= EIDOS_PKB_SECRET;
            }
            if (bw_mod_menu_option_word((u32)g_kb.mod, (u32)g_kb.opt)) {
                flags |= EIDOS_PKB_NO_SPACE;
            }
        } else if (target == KB_AP_PASS) {
            title = "ROOM";
            sub = "PASSWORD";
            flags |= EIDOS_PKB_SECRET;
        } else if (target == KB_AP_SLOT) {
            title = "ROOM";
            sub = "SLOT";
            flags |= EIDOS_PKB_NO_SPACE;
        } else if (target == KB_CODE) {
            title = "JOIN";
            sub = "CODE";
            flags |= EIDOS_PKB_NO_SPACE;
        } else if (target == KB_NEW_GAME) {
            title = "NEW GAME";
            sub = "NAME";
        } else {
            sub = "MULTIPLAYER";
        }
        {
            char t2[32], s2[32];
            int j;

            /* the header's words in capitals, as the game's keyboard has them */
            put(t2, (int)sizeof t2, title);
            put(s2, (int)sizeof s2, sub);
            for (j = 0; t2[j] != '\0'; j++) t2[j] = (char)toupper((unsigned char)t2[j]);
            for (j = 0; s2[j] != '\0'; j++) s2[j] = (char)toupper((unsigned char)s2[j]);
            eidos_pkb_open(t2, s2, prompt, start, g_kb.max, flags);
        }
        return EIDOS_SUI_SFX_SELECT;
    }
    if (g_kb.max > KB_LIMIT) {
        g_kb.max = KB_LIMIT;            /* the game's keyboard: its own limit */
    }
    put(g_kb.text, (int)sizeof g_kb.text, start != NULL ? start : "");
    return EIDOS_SUI_KEYBOARD | EIDOS_SUI_SFX_SELECT;
}

int eidos_sui_keyboard(int *kind, int *max, char *text, int n)
{
    if (g_kb.target == KB_NONE || eidos_pkb() != NULL) {
        /* (none, or the port's own keyboard has it: the game's is not
         * opened under it) */
        return 0;
    }
    *kind = g_kb.kind;
    *max = g_kb.max;
    put(text, n, g_kb.text);
    return g_kb.target;
}

/* The keyboard's question for the port's own texts. */
const char *eidos_sui_keyboard_prompt(void)
{
    switch (g_kb.target) {
    case KB_NAME:     return STR_KB_NAME;
    case KB_CODE:     return STR_KB_CODE;
    case KB_NEW_GAME: return STR_KB_NEW_GAME;
    case KB_MOD_TEXT: return bw_mod_menu_option_label((u32)g_kb.mod, (u32)g_kb.opt);
    case KB_AP_SLOT:  return "Slot name?";
    case KB_AP_PASS:  return "Room password?";
    default:          return NULL;
    }
}

static int session_action(int type);

void eidos_sui_keyboard_done(const char *text, int ok)
{
    int target = g_kb.target;
    char buf[EIDOS_PKB_MAX + 8];
    int i;

    g_kb.target = KB_NONE;
    if (!ok || text == NULL) {
        return;
    }
    put(buf, (int)sizeof buf, text);
    switch (target) {
    case KB_NAME:
        if (buf[0] != '\0') {
            eidos_settings_net_set_name(buf);
            bw_mod_net_set_name(buf);
            bw_mp_rename(buf);
        }
        break;
    case KB_CODE:
        for (i = 0; buf[i] != '\0'; i++) {
            if (buf[i] >= 'a' && buf[i] <= 'z') {
                buf[i] = (char)(buf[i] - 'a' + 'A');
            }
        }
        eidos_settings_session_set_join_code(buf);
        if (buf[0] != '\0') {
            (void)session_action(ROW_MP_JOIN_GO);   /* joins at once */
        }
        break;
    case KB_NEW_GAME:
        if (bw_mp_game_create(buf, NULL, 0) == 0) {
            /* hosted at once (from the hub's card too: not the kept games' list) */
            g_mp_view = MP_VIEW_HOST;
            (void)session_action(ROW_SESSION_HOST);
        }
        break;
    case KB_MOD_TEXT:
        (void)bw_mod_menu_option_set_text((u32)g_kb.mod, (u32)g_kb.opt, buf);
        break;
    case KB_AP_SLOT:
        /* a slot name holds no space (the join line is words) */
        for (i = 0; buf[i] != '\0'; i++) {
            if (buf[i] == ' ') buf[i] = '_';
        }
        if (buf[0] != '\0') {
            put(g_room.slot, (int)sizeof g_room.slot, buf);
        }
        break;
    case KB_AP_PASS:
        for (i = 0; buf[i] != '\0'; i++) {
            if (buf[i] == ' ') buf[i] = '_';
        }
        put(g_room.pass, (int)sizeof g_room.pass, buf);   /* empty: no password */
        break;
    default:
        break;
    }
}

/* A mod's own tab: its options, buttons and rows of that tab that show
 * now (their `?key=value` holds; a `show` row has a text). */
static void build_modtab_rows(int k)
{
    u32 m, o;

    if (k < 0 || k >= g_modtab_n) {
        return;
    }
    m = (u32)g_modtab[k].mod;
    for (o = 0; o < bw_mod_menu_option_count(m); o++) {
        if (bw_mod_menu_option_tab(m, o) == g_modtab[k].tab && bw_mod_menu_option_shown(m, o)) {
            add_row(ROW_MOD_OPTION, (int)m, (int)o);
        }
    }
}

/* What a mod tab shows: changes when the mod sets a row's text or an
 * answer changes a condition; the rows are built again then. */
static u32 g_modtab_sig;

static u32 modtab_sig(void)
{
    u32 m, o, h = 2166136261u;
    int k = g.tab - TAB_MOD_BASE;

    if (g.level != 1 || k < 0 || k >= g_modtab_n) {
        return 0;
    }
    m = (u32)g_modtab[k].mod;
    for (o = 0; o < bw_mod_menu_option_count(m); o++) {
        if (bw_mod_menu_option_tab(m, o) == g_modtab[k].tab && bw_mod_menu_option_shown(m, o)) {
            h = (h ^ (o + 1u)) * 16777619u;
        }
    }
    return h;
}

static void build_rows(void)
{
    int i;

    g.nrows = 0;
    if (g.level == 0) {
        for (i = 0; i < strip_count(); i++) {
            add_row(ROW_SECTION, strip_tab(i), 0);
        }
        return;
    }
    if (g.tab >= TAB_MOD_BASE) {
        build_modtab_rows(g.tab - TAB_MOD_BASE);
        return;
    }
    switch (g.tab) {
    case 0:
        add_row(ROW_OPTION, EIDOS_OPT_DISPLAY_MODE, 0);
        add_row(ROW_OPTION, EIDOS_OPT_DISPLAY_PIXEL_SCALE, 0);
        add_row(ROW_OPTION, EIDOS_OPT_DISPLAY_RESOLUTION, 0);
        add_row(ROW_OPTION, EIDOS_OPT_OUTLINES, 0);
        add_row(ROW_OPTION, EIDOS_OPT_GPU_2D, 0);
        add_row(ROW_OPTION, EIDOS_OPT_RENDERER, 0);
        add_row(ROW_OPTION, EIDOS_OPT_VSYNC, 0);
        break;
    case 1:
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_ARRANGEMENT, 0);
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_SWAP, 0);
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_GAP, 0);
        if (eidos_settings_get(EIDOS_OPT_LAYOUT_ARRANGEMENT) == EIDOS_LAYOUT_ONE) {
            add_row(ROW_OPTION, EIDOS_OPT_UI_THEME, 0);
        }
        break;
    case 2:
        add_row(ROW_OPTION, EIDOS_OPT_PAD_ICONS, 0);
        for (i = 0; i < eidos_settings_button_count(); i++) {
            add_row(ROW_BIND, i, 0);
        }
        break;
    case 3:
        add_row(ROW_OPTION, EIDOS_OPT_SOUND_VOLUME, 0);
        break;
    case TAB_EXTRAS:
        add_row(ROW_OPTION, EIDOS_OPT_EXTRAS_INSTANT_TEXT, 0);
        add_row(ROW_OPTION, EIDOS_OPT_TRANSITIONS, 0);
        if (eidos_settings_get(EIDOS_OPT_LAYOUT_ARRANGEMENT) == EIDOS_LAYOUT_ONE) {
            /* the battle backdrop is the one-screen layout's own */
            add_row(ROW_OPTION, EIDOS_OPT_BATTLE_BACKDROP, 0);
            add_row(ROW_OPTION, EIDOS_OPT_BATTLE_PLATFORMS, 0);
            add_row(ROW_OPTION, EIDOS_OPT_MOVE_COLOURS, 0);
            if (eidos_settings_get(EIDOS_OPT_BATTLE_BACKDROP) == 0) {
                /* the weather's fog is the real-map backdrop's own */
                add_row(ROW_OPTION, EIDOS_OPT_WEATHER_HAZE, 0);
            }
        }
        break;
    case TAB_MODS:
        build_mods_rows();
        return;
    case TAB_NETWORK:
        build_net_rows();
        return;
    case TAB_SESSION:
        build_session_rows();
        return;
    default:
        break;
    }
    add_row(ROW_DEFAULTS, 0, 0);
}

/*
 * The rows are built again, and the cursor follows the row it was on: the
 * kind of the row and its argument are kept, and the cursor goes back to
 * the row that has them.  A row that is gone (a peer that left) leaves the
 * cursor where it is, as near as it can.
 */
static void clamp_scroll(void);

static void build_rows_keep_cursor(void)
{
    int type = -1;
    int arg = 0, arg2 = 0;
    int i;

    if (g.nrows > 0 && g.cursor >= 0 && g.cursor < g.nrows) {
        type = g.rows[g.cursor].type;
        arg = g.rows[g.cursor].arg;
        arg2 = g.rows[g.cursor].arg2;
    }
    build_rows();
    if (type < 0) {
        return;
    }
    /* the same row (a mod's options share its index: the option too), or
     * else the first of its kind */
    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == type && g.rows[i].arg == arg && g.rows[i].arg2 == arg2) {
            g.cursor = i;
            clamp_scroll();
            return;
        }
    }
    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == type && g.rows[i].arg == arg) {
            g.cursor = i;
            return;
        }
    }
    if (g.cursor >= g.nrows) {
        g.cursor = g.nrows > 0 ? g.nrows - 1 : 0;
    }
    clamp_scroll();
}

/* ------------------------------------------------------------------ */
/* The menu's own question                                             */
/* ------------------------------------------------------------------ */

/*
 * The port has no way to ask the player a question from the menu: the
 * game's own yes/no window needs the field's talk window, and the settings
 * screen is a menu at the title.  So the menu asks its own question: a row
 * that asks, and the answer it waits for.
 *
 * While the question is up the screen shows it alone (the header, the
 * question and the grey bar).  Left and right choose the answer, A takes
 * the answer that is chosen and B says no.  The answer starts at "no": the
 * player has to choose the other one.
 */
#define QUESTION_NONE    0
#define QUESTION_RESTART 1

/* What a question says: the row's label and the help's first line. */
typedef struct QuestionText {
    const char *label;
    const char *help;
} QuestionText;

static const QuestionText k_questions[] = {
    { "", "" },                              /* QUESTION_NONE    */
    { "Restart run?",                    /* QUESTION_RESTART */
      "Return to title screen." },
};

static struct {
    int asking;     /* what is asked (QUESTION_*)                  */
    int answer;     /* 1: yes, 0: no: the answer the player chose */
    int restart;    /* the run restart, asked for: carried out when the
                       screen has closed (eidos_sui_leave)        */
} g_q;

static int question_up(void)
{
    return g_q.asking != QUESTION_NONE;
}

/* The question goes up. */
static int question_ask(int what)
{
    if (what <= QUESTION_NONE || what > (int)(sizeof k_questions /
                                              sizeof k_questions[0]) - 1) {
        return 0;
    }
    g_q.asking = what;
    g_q.answer = 0;
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
}

/*
 * The question is answered.  The action it was about runs when the answer
 * is yes; the restart leaves the screen, because the run it ends is the
 * screen's own game.
 */
static int question_close(int yes)
{
    int what = g_q.asking;

    g_q.asking = QUESTION_NONE;
    if (yes && what == QUESTION_RESTART) {
        /* The port's own restart, once the screen has closed: the screen is
         * a game process still fading out, and a reset under it left a dead
         * menu (the reset clears the game's memory, not this screen's own
         * state).  eidos_sui_leave records the request, and the boot's frame
         * loop carries it out. */
        g_q.restart = 1;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT | EIDOS_SUI_EXIT;
    }
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
}

/*
 * Whether the screen has the grey bar.  Inside a section it is "Back", one
 * more place below the visible rows.  The main list has none: B leaves the
 * menu, and the sixth bar shows one more section instead.
 */
static int has_back(void)
{
    return 0;
}

/* The rows shown at once. */
static int visible_rows(void)
{
    return EIDOS_SUI_VISIBLE + 1;
}

/* The cursor runs over the rows and then, with the grey bar, one more
 * place: the bar. */
static int on_back(void)
{
    return has_back() && g.cursor >= g.nrows;
}

static void clamp_scroll(void)
{
    if (on_back()) {
        return;
    }
    if (g.cursor < g.scroll) {
        g.scroll = g.cursor;
    }
    if (g.cursor >= g.scroll + visible_rows()) {
        g.scroll = g.cursor - visible_rows() + 1;
    }
    if (g.scroll > g.nrows - visible_rows()) {
        g.scroll = g.nrows - visible_rows();
    }
    if (g.scroll < 0) {
        g.scroll = 0;
    }
}

static int g_last_tab;
static int g_mp_only;          /* the Multiplayer screen alone (title menu) */
static void enter_section(int t);

void eidos_sui_enter(void)
{
    Row *rows = g.rows;
    int rowcap = g.rowcap;

    memset(&g, 0, sizeof g);
    g.rows = rows;               /* the list's own storage is kept */
    g.rowcap = rowcap;
    g_mp_view = MP_VIEW_CHOOSE;
    if (g_look.busy) {
        bw_mod_net_lookup_stop();
    }
    g_look.busy = 0;
    g_look.said[0] = '\0';
    g_req_continue = -1;
    g_req_coop_new = 0;
    g_gl_n = -1;                    /* Your games read again */
    modtabs_build();                /* the running mods' own tabs */
    g_mp_only = bw_mod_title_take_multiplayer();
    if (g_mp_only) {
        const char *join = bw_mod_title_take_join();

        g.tab = TAB_SESSION;
        g.level = 1;
        build_rows();
        if (join != NULL && !mp_in_game()) {
            /* --join CODE or an invite link: the code is joined at once */
            eidos_settings_session_set_join_code(join);
            g_mp_view = MP_VIEW_JOIN;
            build_rows();
            (void)session_action(ROW_MP_JOIN_GO);
        } else if (eidos_settings_net_name()[0] == '\0') {
            /* No name yet: the other players see it, so the keyboard asks
             * for it before the screen first shows (mod_options_proc.c
             * opens it in place of the screen's fade-in). */
            (void)keyboard_open(KB_NAME);
        }
        return;
    }
    /* the screen opens on the tab it was left on */
    enter_section(g_last_tab);
}

void eidos_sui_leave(void)
{
    eidos_pkb_close();
    bw_mod_menu_clear_secrets();
    if (g_q.restart) {
        g_q.restart = 0;
        bw_mod_restart(0);
    }
    if (g.capturing) {
        eidos_settings_capture_cancel();
        g.capturing = 0;
    }
    if (g.editing) {
        /* The text being edited is given up: the value before the edit
         * stands. */
        eidos_settings_net_text_cancel();
        g.editing = 0;
    }
    if (g_q.asking != QUESTION_NONE) {
        /* A question that was up is given up: the answer is no. */
        g_q.asking = QUESTION_NONE;
    }
    if (g.mods_open) {
        /* The mods the player changed: the switches apply now, when the
         * player leaves the menu (sdk/mod_menu.h). */
        (void)bw_mod_menu_apply();
        g.mods_open = 0;
    }
    eidos_settings_commit();
}

/*
 * Reload: the mods folder is read again and the list applies at once (a
 * mod whose files changed restarts), then the rows are built again, since
 * mods may have come or gone.
 */
static int reload_mods(void)
{
    int rc = bw_mod_menu_reload();

    build_rows_keep_cursor();
    return EIDOS_SUI_REDRAW | (rc == 0 ? EIDOS_SUI_SFX_SELECT : EIDOS_SUI_SFX_BACK);
}

static void enter_section(int t)
{
    g.tab = t;
    if (t < TAB_STRIP) {
        g_last_tab = t;
    }
    g.level = 1;
    g.cursor = 0;
    g.scroll = 0;
    if (t == TAB_MODS && !g.mods_open) {
        /* The Mods section: the model is built again from the port's list
         * and the player's record, the way sdk/mod_menu.h says: the
         * screen calls it once, when the section is entered. */
        bw_mod_menu_open();
        g.mods_open = 1;
    }
    build_rows();
}

static void leave_section(void)
{
    int i;

    g.level = 0;
    /* the cursor goes back to the section's own place on the list */
    g.cursor = strip_pos(g.tab);
    (void)i;
    g.scroll = 0;
    build_rows();
    clamp_scroll();
}

static void move_cursor(int d)
{
    g_copied = 0;
    int places = g.nrows + (has_back() ? 1 : 0);

    if (places <= 0) {
        return;
    }
    g.cursor = (g.cursor + d + places) % places;
    clamp_scroll();
}

/* Left or right on an option row (d = -1 or +1). */
static int change_option(int id, int d, int wrap)
{
    const EidosOption *o = eidos_settings_option(id);
    int v, nv;
    if (o == NULL) {
        return 0;
    }
    v = eidos_settings_get(id);
    nv = v + d * (o->step > 0 ? o->step : 1);
    if (nv > o->max) {
        nv = wrap ? o->min : o->max;
    } else if (nv < o->min) {
        nv = wrap ? o->max : o->min;
    }
    if (nv == v) {
        return 0;
    }
    return eidos_settings_set(id, nv) == 0;
}

/*
 * A mod's own setting: a bool turns on and off, a choice steps through
 * its names and wraps around.  The choice is kept beside the mod's saved
 * data at once (sdk/mod_menu.h).
 */
static int change_mod_option(int mod, int option, int d)
{
    int kind = bw_mod_menu_option_kind((u32)mod, (u32)option);
    int count = bw_mod_menu_option_choice_count((u32)mod, (u32)option);
    int value = bw_mod_menu_option_value((u32)mod, (u32)option);

    if (kind == BW_MOD_SETTING_TEXT) {
        return 0;                   /* a text is typed (mod_text_edit) */
    }
    if (bw_mod_menu_option_in_lobby((u32)mod, (u32)option) && bw_mod_menu_rules_locked()) {
        return 0;                   /* a joiner plays by the host's rules */
    }
    if (kind == BW_MOD_SETTING_BOOL) {
        value = value != 0 ? 0 : 1;
    } else if (kind == BW_MOD_SETTING_INT) {
        /* A number steps by one and stops at its bounds. */
        int lo = bw_mod_menu_option_min((u32)mod, (u32)option);
        int hi = bw_mod_menu_option_max((u32)mod, (u32)option);

        value += d;
        if (value < lo) {
            value = lo;
        } else if (value > hi) {
            value = hi;
        }
    } else {
        if (count <= 0) {
            return 0;
        }
        value += d;
        if (value < 0) {
            value = count - 1;
        } else if (value >= count) {
            value = 0;
        }
    }
    if (bw_mod_menu_option_set((u32)mod, (u32)option, value) != 0) {
        return 0;
    }
    /* a mode's rows can hang on this answer (an option's `?key=value`) */
    if (g_mp_view == MP_VIEW_MODE) {
        build_rows_keep_cursor();
    }
    return 1;
}

/* A mod's text option: typed on the game's keyboard. */
static int mod_text_edit(int mod, int option)
{
    g_kb_mod_next = mod;
    g_kb_opt_next = option;
    return EIDOS_SUI_REDRAW | keyboard_open(KB_MOD_TEXT);
}

/* A mod's button: the mod hears setting.button with its key; what it does
 * (a sign-in) shows in its rows when it sets them. */
static int mod_button(int mod, int option)
{
    return bw_mod_menu_press((u32)mod, (u32)option) == 0 ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT
                                                          : EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
}

/* The port's own port number: the relay's or the control's. */
static void change_net_port(int type, int d)
{
    if (type == ROW_NET_RELAY_PORT) {
        eidos_settings_net_set_relay_port(eidos_settings_net_relay_port() + d);
    } else {
        eidos_settings_net_set_control_port(eidos_settings_net_control_port() + d);
    }
}

/* ------------------------------------------------------------------ */
/* The port's own session                                              */
/* ------------------------------------------------------------------ */

/*
 * What the screen shows of the port's own session.  The state itself is
 * the port's own (sdk/mod_net.h, "The port's own session"); the screen
 * keeps the last answer, so it can say what happened.
 */

/*
 * Ask the port's own session how it is doing.  The relay takes up to
 * three seconds to answer, so the work runs on the port's own worker and
 * the screen asks each frame; the answer is taken once, and the code the
 * relay handed out goes into the model, where the row shows it and the
 * player reads it to the other players.
 *
 * The peers of the session come with the answer: the port's own handler
 * collected them (engine/platform/settings.c), and the stamp says when
 * the list changed, so the rows are built again when it does.
 *
 * Returns non-zero when the screen has to draw again.
 */
static const char *session_answer_text(int rc);

static int poll_session(void)
{
    BwPortSessionState st;
    int redraw = 0;

    memset(&st, 0, sizeof st);
    (void)bw_port_session_poll(&st);
    if (st.busy != g_sess.busy) {
        g_sess.busy = st.busy;
        if (g.tab == TAB_SESSION) {
            build_rows();
            g.cursor = 0;
            g.scroll = 0;
        }
        redraw = 1;
    }
    if (st.done) {
        g_sess.have = 1;
        g_sess.rc = st.rc;
        g_sess.host = st.host;
        if (!st.host && st.rc != BW_MOD_NET_OK && g_mp_view == MP_VIEW_JOIN && g.tab == TAB_SESSION) {
            /* a join the relay refused: back on the hub, saying why */
            put(g_look.said, (int)sizeof g_look.said,
                strcmp(bw_mod_net_last_error(), "unknown-code") == 0 ? STR_MP_NO_CODE : session_answer_text(st.rc));
            g_mp_view = MP_VIEW_CHOOSE;
            build_rows();
            g.cursor = 0;
            g.scroll = 0;
        }
        if (st.host && g_host_from_hub) {
            g_host_from_hub = 0;
            if (st.rc != BW_MOD_NET_OK && g_mp_view == MP_VIEW_HOST && g.tab == TAB_SESSION) {
                /* a game of Your games that could not be hosted: back on the hub, saying why */
                put(g_look.said, (int)sizeof g_look.said,
                    st.rc == BW_MOD_NET_E_IO || st.rc == BW_MOD_NET_E_NO_RELAY ? STR_MP_NO_ANSWER
                                                                             : session_answer_text(st.rc));
                g_mp_view = MP_VIEW_CHOOSE;
                build_rows();
                g.cursor = 0;
                g.scroll = 0;
            }
        }
        if (st.rc == BW_MOD_NET_OK && st.code[0] != '\0') {
            /* The code in force: the one the relay handed out, or the one
             * the player joined with.  The code the player types is a
             * setting of its own and is not touched here. */
            eidos_settings_session_set_code(st.code);
            if (getenv("EIDOS_MP_UI_LOG") != NULL) {
                fprintf(stderr, "eidos: mp ui: session code %s (%s)\n", st.code, st.host ? "hosting" : "joined");
            }
        }
        redraw = 1;
    }
    if (st.stamp + bw_mp_stamp() != g_sess.stamp) {
        g_sess.stamp = st.stamp + bw_mp_stamp();
        if (g.tab == TAB_SESSION) {
            build_rows_keep_cursor();
        }
        redraw = 1;
    }
    return redraw;
}

/* The service's own answers, in the screen's own words. */
static const char *session_answer_text(int rc)
{
    switch (rc) {
    case BW_MOD_NET_OK:           return STR_SESS_OK;
    case BW_MOD_NET_E_NO_RELAY:   return STR_SESS_NORELAY;
    case BW_MOD_NET_E_BUSY:       return STR_SESS_BUSY;
    case BW_MOD_NET_E_NO_SESSION: return STR_SESS_NOSESS;
    case BW_MOD_NET_E_RELAY:      return STR_SESS_REFUSED;
    case BW_MOD_NET_E_TOO_BIG:    return STR_SESS_BIG;
    case BW_MOD_NET_E_IO:         return STR_SESS_IO;
    case BW_MOD_NET_E_ARG:        return STR_SESS_ARG;
    default:                      return STR_SESS_FAILED;
    }
}

/* The value of the Host and Join rows: the state of the work. */
static void session_value(int host, char *out, int n)
{
    if (n > 0) {
        out[0] = '\0';
    }
    if (g_sess.busy) {
        put(out, n, STR_SESSION_WAIT);
        return;
    }
    if (g_sess.have && g_sess.host == host && g_sess.rc != BW_MOD_NET_OK) {
        if (strcmp(bw_mod_net_last_error(), "unknown-code") == 0) {
            put(out, n, STR_MP_NOT_FOUND);
        } else {
            put(out, n, session_answer_text(g_sess.rc));
        }
    }
}

/* The Mode view's Type row: the next of the card's types (the card itself
 * and its `in=` modes), wrapping round.  1 when it changed. */
static int mode_type_step(int d)
{
    int list[16], n = 0, k, at = 0;

    if (g_mode_mod < 0) {
        return 0;
    }
    list[n++] = g_mode_card;
    for (k = 0; k < (int)bw_mod_menu_mode_count((u32)g_mode_mod) && n < 16; k++) {
        if (bw_mod_menu_mode_in((u32)g_mode_mod, (u32)k) == g_mode_card) {
            list[n++] = k;
        }
    }
    if (n < 2) {
        return 0;
    }
    for (k = 0; k < n; k++) {
        if (list[k] == g_mode_k) {
            at = k;
        }
    }
    g_mode_k = list[(at + d + n) % n];
    g_mode_said[0] = '\0';
    build_rows_keep_cursor();
    return 1;
}

/* ------------------------------------------------------------------ */
/* The world a typed code names (metaxy's code lookup)                 */
/* ------------------------------------------------------------------ */

/* Past one JSON value from p (a string, a number, an object, an array). */
static const char *json_end(const char *p)
{
    int depth = 0, in_str = 0;

    for (; *p != '\0'; p++) {
        if (in_str) {
            if (*p == '\\' && p[1] != '\0') {
                p++;
            } else if (*p == '"') {
                in_str = 0;
                if (depth == 0) {
                    return p + 1;
                }
            }
            continue;
        }
        if (*p == '"') {
            in_str = 1;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (--depth <= 0) {
                return p + 1;
            }
        } else if (depth == 0 && (*p == ',' || *p == '\n')) {
            return p;
        }
    }
    return p;
}

/* The value of `key` in the object at obj: its start and end, 1; or 0. */
static int json_get(const char *obj, const char *key, const char **vs, const char **ve)
{
    const char *p = obj;
    size_t kl = strlen(key);

    while (*p == ' ') p++;
    if (*p != '{') {
        return 0;
    }
    p++;
    for (;;) {
        const char *k;

        while (*p == ' ' || *p == ',' || *p == '\n') p++;
        if (*p != '"') {
            return 0;
        }
        k = ++p;
        while (*p != '\0' && *p != '"') {
            p += (*p == '\\' && p[1] != '\0') ? 2 : 1;
        }
        if (*p != '"') {
            return 0;
        }
        p++;
        while (*p == ' ' || *p == ':') p++;
        if ((size_t)(p - k) >= kl + 1 && strncmp(k, key, kl) == 0 && k[kl] == '"') {
            *vs = p;
            *ve = json_end(p);
            return 1;
        }
        p = json_end(p);
        if (*p == '\0' || *p == '}') {
            return 0;
        }
    }
}

/* A value as plain text: a string unquoted; an array or a word with its
 * quotes and spaces taken out ("[randomize,similar_stats]"). */
static void json_text(const char *vs, const char *ve, char *out, size_t size)
{
    size_t k = 0;
    int in_str = 0;

    for (; vs < ve && k + 1 < size; vs++) {
        if (*vs == '"') {
            in_str = !in_str;
        } else if (*vs == '\\' && in_str && vs + 1 < ve) {
            out[k++] = *++vs;
        } else if (in_str || *vs != ' ') {
            out[k++] = *vs;             /* a name keeps its spaces */
        }
    }
    out[k] = '\0';
}

/* A yaml line's values the same way ("[randomize, similar_stats]" too). */
static void plain_word(const char *s, size_t n, char *out, size_t size)
{
    size_t k = 0, i;

    for (i = 0; i < n && s[i] != '\0' && k + 1 < size; i++) {
        if (s[i] != '"' && s[i] != ' ' && s[i] != '\'') {
            out[k++] = s[i];
        }
    }
    out[k] = '\0';
}

static void look_line(const char *label, const char *value)
{
    if (g_look.nlines < 24) {
        put(g_look.line_label[g_look.nlines], 48, label);
        put(g_look.line_value[g_look.nlines], 48, value);
        g_look.nlines++;
    }
}

/* The lookup's JSON as the World view's lines: the game, the players, and
 * the rules the joining mod declares (`yaml` lines name each option as the
 * world's configuration does), each in that mod's own words. */
static void look_read_world(const char *json)
{
    const char *vs, *ve, *p;
    char buf[64];
    int have_mod;
    u32 o;

    g_look.nlines = 0;
    g_look.nplayers = 0;
    g_look.free = 0;
    g_look.state[0] = '\0';
    g_look.game[0] = '\0';
    if (json_get(json, "state", &vs, &ve)) json_text(vs, ve, g_look.state, sizeof g_look.state);
    if (json_get(json, "free", &vs, &ve)) g_look.free = atoi(vs);
    if (json_get(json, "version", &vs, &ve)) json_text(vs, ve, g_look.game, sizeof g_look.game);
    if (json_get(json, "players", &vs, &ve) && *vs == '[') {
        for (p = vs + 1; p < ve && g_look.nplayers < 16;) {
            const char *ns, *ne;

            while (p < ve && *p != '{') p++;
            if (p >= ve) break;
            if (json_get(p, "name", &ns, &ne)) {
                json_text(ns, ne, g_look.players[g_look.nplayers], sizeof g_look.players[0]);
            }
            g_look.claimed[g_look.nplayers] = json_get(p, "claimed", &ns, &ne) && strncmp(ns, "true", 4) == 0;
            g_look.nplayers++;
            p = json_end(p);
        }
    }
    have_mod = bw_mod_menu_find_joining(&g_look.join_mod, &g_look.join_mode);
    look_line(STR_MP_WORLD_GAME, g_look.game[0] == 'w' ? "White" : g_look.game[0] == 'b' ? "Black" : "...");
    snprintf(buf, sizeof buf, "%d of %d free", g_look.free, g_look.nplayers);
    look_line(STR_MP_WORLD_PLAYERS, buf);
    for (o = 0; have_mod && o < bw_mod_menu_option_count(g_look.join_mod); o++) {
        const char *y = bw_mod_menu_option_yaml(g_look.join_mod, o), *sp;
        char key[48], want[64], have[64];
        int kind, n, i, idx = 0;

        if (y == NULL || !bw_mod_menu_option_in_mode(g_look.join_mod, o, g_look.join_mode)) {
            continue;
        }
        sp = strchr(y, ' ');
        put(key, (int)sizeof key, y);
        if (sp != NULL && (size_t)(sp - y) < sizeof key) {
            key[sp - y] = '\0';
        }
        kind = bw_mod_menu_option_kind(g_look.join_mod, o);
        n = kind == BW_MOD_SETTING_CHOICE ? bw_mod_menu_option_choice_count(g_look.join_mod, o)
            : kind == BW_MOD_SETTING_BOOL ? 2 : 0;
        have[0] = '\0';
        if ((json_get(json, "player_options", &vs, &ve) && json_get(vs, key, &vs, &ve)) ||
            (json_get(json, "options", &vs, &ve) && json_get(vs, key, &vs, &ve))) {
            json_text(vs, ve, have, sizeof have);
        }
        if (kind == BW_MOD_SETTING_INT) {
            look_line(bw_mod_menu_option_label(g_look.join_mod, o), have[0] != '\0' ? have : "...");
            continue;
        }
        /* the choice whose yaml value is the world's; a rule the world
         * leaves out is at its default, the option's first choice */
        for (i = 0; i < n && sp != NULL && have[0] != '\0'; i++) {
            const char *s = sp + 1;
            int k = i;

            while (k > 0 && *s != '\0') {
                if (*s++ == '|') k--;
            }
            plain_word(s, strcspn(s, "|"), want, sizeof want);
            if (strcmp(want, have) == 0) {
                idx = i;
                break;
            }
        }
        if (kind == BW_MOD_SETTING_BOOL) {
            look_line(bw_mod_menu_option_label(g_look.join_mod, o), idx ? STR_ON : STR_OFF);
        } else {
            const char *c = bw_mod_menu_option_choice(g_look.join_mod, o, idx);

            look_line(bw_mod_menu_option_label(g_look.join_mod, o), c != NULL ? c : "...");
        }
    }
    for (o = 0; o < (u32)g_look.nplayers; o++) {
        look_line(g_look.players[o], g_look.claimed[o] ? "Joined" : "Free");
    }
    g_look.can_join = 0;
    g_look.said[0] = '\0';
    if (!have_mod) {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_WORLD_NO_MOD);
    } else if (strcmp(g_look.state, "waiting") == 0) {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_WORLD_WAITING);
    } else if (strcmp(g_look.state, "closed") == 0) {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_WORLD_CLOSED);
    } else if (strcmp(g_look.state, "open") != 0) {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_WORLD_BROKEN);
    } else if (g_look.free <= 0) {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_WORLD_FULL);
    } else if ((g_look.game[0] == 'w') != (bw_mp_local_game() == 'W')) {
        /* every place is made for the maker's game: the other one's save
         * would get the wrong data */
        put(g_look.said, (int)sizeof g_look.said,
            g_look.game[0] == 'w' ? STR_MP_WORLD_IS_WHITE : STR_MP_WORLD_IS_BLACK);
    } else {
        g_look.can_join = 1;
    }
}

/* Each step while the relay answers the lookup: a co-op game is joined as
 * before; a world opens the World view; nothing is said on the hub. */
static int lookup_step(void)
{
    int rc;

    if (!g_look.busy) {
        return 0;
    }
    rc = bw_mod_net_lookup_poll();
    if (rc == 0 && ++g_look.busy > 600) {
        /* ten seconds with no answer: said, and nothing is joined */
        bw_mod_net_lookup_stop();
        g_look.busy = 0;
        put(g_look.said, (int)sizeof g_look.said, STR_MP_NO_ANSWER);
        g_mp_view = MP_VIEW_CHOOSE;
        build_rows();
        g.cursor = 0;
        g.scroll = 0;
        return EIDOS_SUI_REDRAW;
    }
    if (rc == 0) {
        return 0;
    }
    g_look.busy = 0;
    if (rc < 0 || bw_mod_net_lookup_kind() == BW_LOOKUP_SESSION) {
        /* a co-op game (or a relay that could not be asked: the join says
         * what went wrong, as before) */
        if (bw_port_session_start(0, g_look.code) == 0) {
            g_sess.busy = 1;
        }
    } else if (bw_mod_net_lookup_kind() == BW_LOOKUP_WORLD) {
        look_read_world(bw_mod_net_lookup_json());
        g_mp_view = MP_VIEW_WORLD;
    } else {
        put(g_look.said, (int)sizeof g_look.said, STR_MP_NO_CODE);
        g_mp_view = MP_VIEW_CHOOSE;
    }
    if (getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: mp ui: code %s is a %s\n", g_look.code,
                rc < 0 ? "session (no answer)" : bw_mod_net_lookup_kind() == BW_LOOKUP_WORLD ? "world"
                : bw_mod_net_lookup_kind() == BW_LOOKUP_SESSION ? "session" : "nothing");
    }
    build_rows();
    g.cursor = 0;
    g.scroll = 0;
    return EIDOS_SUI_REDRAW;
}

/*
 * The Session section's own rows.  Hosting and joining do not block the
 * frame thread: the port's own worker asks the relay and the screen
 * waits for the answer (poll_session).
 */
static int session_action(int type)
{
    switch (type) {
    case ROW_SESSION_HOST:
        if (g_mp_view == MP_VIEW_CHOOSE) {
            /* Host: the games the host kept, newest first, and a new one */
            g_ngames = read_mp_games(&g_games);
            g_mp_view = MP_VIEW_HOST;
            g_sess.have = 0;
            build_rows();
            g.cursor = 0;
            g.scroll = 0;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        if (bw_port_session_start(1, NULL) == 0) {
            g_sess.busy = 1;
            g_sess.have = 0;
            build_rows();
            g.cursor = 0;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    case ROW_SESSION_JOIN:
        /* Join: the code is asked for on the game's keyboard at once */
        g_mp_view = MP_VIEW_JOIN;
        g_sess.have = 0;
        build_rows();
        g.cursor = 0;
        g.scroll = 0;
        return EIDOS_SUI_REDRAW | keyboard_open(KB_CODE);
    case ROW_MP_GAME:
        if (bw_mp_game_select(g_games[g.rows[g.cursor].arg]) != 0) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        return session_action(ROW_SESSION_HOST);
    case ROW_MP_NEW_GAME:
        /* the new game is named first */
        return keyboard_open(KB_NEW_GAME);
    case ROW_MP_JOIN_GO: {
        const char *code = eidos_settings_session_join_code();

        if (code == NULL || code[0] == '\0') {
            /* no code to join with: the player types one first */
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        /* a number of up to five digits is an Archipelago room's port
         * (a relay code has six characters): the Room view */
        {
            size_t n = strlen(code), k;
            int digits = n >= 2 && n <= 5;

            for (k = 0; k < n; k++) {
                digits &= code[k] >= '0' && code[k] <= '9';
            }
            if (digits) {
                const char *host = getenv("EIDOS_AP_HOST");

                memset(&g_room, 0, sizeof g_room);
                put(g_room.host, (int)sizeof g_room.host, host != NULL && host[0] != '\0' ? host : "archipelago.gg");
                put(g_room.port, (int)sizeof g_room.port, code);
                put(g_room.slot, (int)sizeof g_room.slot,
                    eidos_settings_net_name()[0] != '\0' ? eidos_settings_net_name() : STR_MP_DEFAULT_NAME);
                g_mp_view = MP_VIEW_ROOM;
                build_rows();
                g.cursor = 0;
                g.scroll = 0;
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        /* first the relay says what the code names: a co-op game or a
         * world (lookup_step); a relay that cannot be asked: a co-op game,
         * as before */
        g_look.said[0] = '\0';
        if (bw_mod_net_lookup_start(code) == 0) {
            put(g_look.code, (int)sizeof g_look.code, code);
            g_look.busy = 1;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        if (bw_port_session_start(0, code) == 0) {
            g_sess.busy = 1;
            build_rows();
            g.cursor = 0;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    case ROW_SESSION_LEAVE:
        /* The service's leave does not block. */
        bw_port_session_leave();
        g_mp_view = MP_VIEW_CHOOSE;
        g_sess.have = 0;
        build_rows();
        g.cursor = 0;
        g.scroll = 0;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_MP_START:
        /* The host starts the game when everyone is ready. */
        if (bw_mp_start() == 0) {
            build_rows_keep_cursor();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    case ROW_MP_MODE: {
        /* a mod's way to play: its options and Start */
        const Row *r = &g.rows[g.cursor];

        if (bw_mod_menu_mode_off((u32)r->arg, (u32)r->arg2)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        g_mode_mod = r->arg;
        g_mode_k = r->arg2;
        g_mode_card = r->arg2;
        g_mode_said[0] = '\0';
        g_mp_view = MP_VIEW_MODE;
        build_rows();
        g.cursor = 0;
        g.scroll = 0;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    }
    case ROW_MP_MODE_TYPE:
        return mode_type_step(1) ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT : EIDOS_SUI_REDRAW;
    case ROW_MP_WORLD_LINE:
    case ROW_MP_ROOM_LINE:
        return 0;                       /* a line to read */
    case ROW_MP_ROOM_SLOT:
        return EIDOS_SUI_REDRAW | keyboard_open(KB_AP_SLOT);
    case ROW_MP_ROOM_PASS:
        return EIDOS_SUI_REDRAW | keyboard_open(KB_AP_PASS);
    case ROW_MP_ROOM_JOIN: {
        /* the mod that joins rooms starts a new game, told the room
         * (mode_code: "ap <host:port> <slot> <password>") */
        u32 rm, rk;
        char line[160];

        if (!bw_mod_menu_find_room(&rm, &rk)) {
            put(g_room.said, (int)sizeof g_room.said, STR_MP_ROOM_NO_MOD);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        snprintf(line, sizeof line, "ap %s:%s %s %s", g_room.host, g_room.port, g_room.slot,
                 g_room.pass[0] != '\0' ? g_room.pass : "-");
        if (bw_mod_menu_mode_begin_join(rm, rk, line) != 0) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (getenv("EIDOS_MP_UI_LOG") != NULL) {
            fprintf(stderr, "eidos: mp ui: joins the room %s:%s as %s\n", g_room.host, g_room.port, g_room.slot);
        }
        (void)bw_slots_new_game();
        bw_mod_title_request_autostart();
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
    }
    case ROW_MP_WORLD_JOIN:
        /* the world's own new game: the mod that joins worlds, told the code
         * (mode_code during game.new) */
        if (!g_look.can_join) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (bw_mod_menu_mode_begin_join(g_look.join_mod, g_look.join_mode, g_look.code) != 0) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        (void)bw_slots_new_game();
        bw_mod_title_request_autostart();
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
    case ROW_MP_MODE_START:
        if (bw_mod_menu_mode_off((u32)g_mode_mod, (u32)g_mode_k)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;   /* a type not built yet */
        }
        if (bw_mod_menu_mode_export((u32)g_mode_mod, (u32)g_mode_k) != NULL) {
            /* an Archipelago options file for the room's host; its path
             * goes on the clipboard to attach or send */
            char path[600], name[40];

            if (bw_mod_v6_player_name(name, sizeof name) <= 0) {
                put(name, (int)sizeof name, STR_MP_DEFAULT_NAME);
            }
            if (bw_mod_menu_mode_write_yaml((u32)g_mode_mod, (u32)g_mode_k, name, bw_mp_local_game(), path,
                                            sizeof path) == 0) {
                (void)eidos_settings_clipboard_set(path);
                put(g_mode_said, (int)sizeof g_mode_said, STR_MP_SAVED);
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
            put(g_mode_said, (int)sizeof g_mode_said, STR_MP_NOT_SAVED);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        /* A new game in a file of its own (games/, sdk/save_games.h), told
         * the mode (mode_get during game.new). */
        if (bw_mod_menu_mode_begin((u32)g_mode_mod, (u32)g_mode_k) != 0) {
            if (getenv("EIDOS_MP_UI_LOG") != NULL) {
                fprintf(stderr, "eidos: mp ui: mode %d/%d did not begin\n", g_mode_mod, g_mode_k);
            }
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (getenv("EIDOS_MP_UI_LOG") != NULL) {
            fprintf(stderr, "eidos: mp ui: mode %d/%d begins a new game\n", g_mode_mod, g_mode_k);
        }
        (void)bw_slots_new_game();
        bw_mod_title_request_autostart();
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
    case ROW_SESSION_REVERT:
        /* The code back to the state the last commit left. */
        eidos_settings_session_revert();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_SESSION_DEFAULTS:
        /* The code back to nothing. */
        eidos_settings_session_defaults();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    default:
        break;
    }
    return 0;
}

/* The player's switch of a mod. */
static int toggle_mod(int mod)
{
    int on = bw_mod_menu_enabled_at((u32)mod);

    if (bw_mod_menu_unavailable_at((u32)mod)) {
        return 0;               /* (made for the other game: it cannot be switched on) */
    }
    return bw_mod_menu_set_enabled((u32)mod, !on) == 0;
}

/* The row of a mod's switch in the list that is built now. */
static int mod_row(u32 mod)
{
    int i;

    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == ROW_MOD_SWITCH && (u32)g.rows[i].arg == mod) {
            return i;
        }
    }
    return -1;
}

/*
 * The player's own order: the order the mods load in is the order the
 * player chose (sdk/mod_menu.h, "The order").  L and R move the mod the
 * cursor is on; the list is built again and the cursor follows the mod.
 */
static int move_mod(int mod, int up)
{
    u32 index = (u32)mod;
    int row;

    if (up) {
        if (bw_mod_menu_move_up(index) != 0) {
            return 0;
        }
        index--;
    } else {
        if (bw_mod_menu_move_down(index) != 0) {
            return 0;
        }
        index++;
    }
    build_rows();
    row = mod_row(index);
    if (row >= 0) {
        g.cursor = row;
    }
    clamp_scroll();
    return 1;
}

/*
 * The text being edited: the player types on the host keyboard, and the
 * keys come in one at a time through the platform's capture, the same one
 * the key bindings use.
 */
static void begin_text_edit(int type);

/* A text row: the name and the code on the game's keyboard, the others on
 * the host keyboard. */
static int text_row_action(int type)
{
    if (type == ROW_NET_NAME) {
        return EIDOS_SUI_REDRAW | keyboard_open(KB_NAME);
    }
    if (type == ROW_SESSION_JOINCODE) {
        return EIDOS_SUI_REDRAW | keyboard_open(KB_CODE);
    }
    begin_text_edit(type);
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
}

static void begin_text_edit(int type)
{
    if (type == ROW_NET_RELAY) {
        eidos_settings_net_relay_text_begin();
    } else if (type == ROW_NET_NAME) {
        eidos_settings_net_name_text_begin();
    } else if (type == ROW_SESSION_JOINCODE) {
        eidos_settings_session_join_code_text_begin();
    } else if (type == ROW_NET_RELAY_PORT || type == ROW_NET_CONTROL_PORT) {
        eidos_settings_net_port_text_begin(type == ROW_NET_CONTROL_PORT);
    } else {
        eidos_settings_session_code_text_begin();
    }
    g.editing = 1;
    eidos_settings_capture_begin();
}

static int poll_capture(void)
{
    EidosCaptured c;
    int r;
    memset(&c, 0, sizeof c);
    r = eidos_settings_capture_poll(&c);
    if (r == EIDOS_CAPTURE_KEY) {
        eidos_settings_set_key(g.capturing - 1, 0, c.scancode);
    } else if (r == EIDOS_CAPTURE_PAD) {
        eidos_settings_set_pad(g.capturing - 1, 0, c.pad_kind, c.pad_index);
    } else if (r == EIDOS_CAPTURE_CANCELLED) {
        g.capturing = 0;
        g.wait_release = 1;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    } else {
        return 0;
    }
    g.capturing = 0;
    g.wait_release = 1;
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
}

/*
 * One key of the host keyboard goes into the text being edited.  The
 * capture is armed again for the next key, and the edit ends when the
 * player presses Enter (the value is kept) or Escape (it is dropped).
 */
static int poll_text(void)
{
    EidosCaptured c;
    int r, done = 0;

    memset(&c, 0, sizeof c);
    r = eidos_settings_capture_poll(&c);
    if (r == EIDOS_CAPTURE_KEY) {
        done = eidos_settings_net_text_key(c.scancode);
        eidos_settings_capture_begin();     /* the next key */
    } else if (r == EIDOS_CAPTURE_CANCELLED) {
        eidos_settings_net_text_cancel();
        done = 2;
    } else if (r == EIDOS_CAPTURE_NONE) {
        /* No keyboard to type on: the text ends with the value it has. */
        done = 1;
    } else {
        return 0;
    }
    if (done != 0) {
        eidos_settings_capture_cancel();
        g.editing = 0;
        g.wait_release = 1;
        if (done == 1 && g.cursor < g.nrows &&
            g.rows[g.cursor].type == ROW_NET_NAME) {
            const char *name = eidos_settings_net_name();

            bw_mod_net_set_name(name);
            bw_mp_rename(name);
        }
        return EIDOS_SUI_REDRAW |
               (done == 1 ? EIDOS_SUI_SFX_SELECT : EIDOS_SUI_SFX_BACK);
    }
    return EIDOS_SUI_REDRAW;
}

/* Direction repeat: the first press acts at once, then after 20 frames every
 * 3 frames while held. */
static unsigned repeat_dirs(unsigned pressed, unsigned held)
{
    static const unsigned dirs[] = { 0x40u, 0x80u, 0x10u, 0x20u };
    unsigned out = pressed & 0xF0u;
    unsigned h = held & 0xF0u;
    int i;
    for (i = 0; i < 4; i++) {
        if (h & dirs[i]) {
            if (g.hold_dir != (int)dirs[i]) {
                g.hold_dir = (int)dirs[i];
                g.hold_frames = 0;
            } else {
                g.hold_frames++;
                if (g.hold_frames >= 20 && (g.hold_frames - 20) % 3 == 0) {
                    out |= dirs[i];
                }
            }
            return out;
        }
    }
    g.hold_dir = 0;
    g.hold_frames = 0;
    return out;
}

/* ------------------------------------------------------------------ */
/* The menu's own question                                             */
/* ------------------------------------------------------------------ */

/* The question's own input.  (The question's state is above, with the
 * other state of the screen.) */
static int question_input(unsigned pressed, unsigned held)
{
    unsigned act = repeat_dirs(pressed, held);
    int res = 0;

    if (act & 0x20u) {                   /* left: yes (YES is the left choice) */
        if (!g_q.answer) {
            g_q.answer = 1;
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
        }
    } else if (act & 0x10u) {            /* right: no */
        if (g_q.answer) {
            g_q.answer = 0;
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
        }
    }
    if (pressed & 2u) {                  /* B: no */
        return question_close(0);
    }
    if (pressed & 1u) {                  /* A: the answer that is chosen */
        return question_close(g_q.answer);
    }
    return res;
}

/* The screen's own input: the buttons the player pressed. */
static int sui_input(unsigned pressed, unsigned held);

/* A click from the one-screen layout (engine/ui/recipe_settings.c): taken
 * at the screen's next step, as a touch of that row or that section, so
 * the screen answers it once, with one sound, as it answers the stylus. */
static int g_req_row = -1, g_req_zone, g_req_tab = -1;
static int g_req_host_game;            /* (eidos_sui_mp_act, at the end) */
static int mp_take_host_game(void);
static int g_req_abs;                  /* (eidos_sui_mp_row_act, at the end) */
static int mp_take_row(void);

void eidos_sui_request_touch(int row, int zone)
{
    g_req_row = row;
    g_req_zone = zone;
}

void eidos_sui_request_tab(int tab)
{
    g_req_tab = tab;
}

int eidos_sui_input(unsigned pressed, unsigned held)
{
    int res;

    g_mp_tick++;
    if (eidos_pkb() != NULL) {
        /* the port's own keyboard has the keys until it closes (its OK or
         * Back, from a key, the pointer or the PC's keyboard) */
        char text[EIDOS_PKB_MAX + 8];
        int changed = eidos_pkb_input(pressed), r = eidos_pkb_result(text, (int)sizeof text);

        (void)held;
        if (r != 0) {
            eidos_sui_keyboard_done(text, r > 0);
            build_rows_keep_cursor();
            return EIDOS_SUI_REDRAW | (r > 0 ? EIDOS_SUI_SFX_SELECT : EIDOS_SUI_SFX_BACK);
        }
        return changed ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE : 0;
    }
    if (g.tab >= TAB_MOD_BASE && g.level == 1) {
        u32 sig;

        if (g.tab - TAB_MOD_BASE >= g_modtab_n) {
            enter_section(TAB_MODS);            /* its mod went off */
            return EIDOS_SUI_REDRAW;
        }
        sig = modtab_sig();
        if (sig != g_modtab_sig) {
            g_modtab_sig = sig;
            build_rows_keep_cursor();
            res = EIDOS_SUI_REDRAW;
            if (pressed == 0) {
                return res;
            }
        }
    }
    if (g_req_tab >= 0) {
        int t = g_req_tab;

        g_req_tab = -1;
        if (g.level == 1 && !g_mp_only && t >= 0 && t < strip_count() && strip_tab(t) != g.tab
            && !question_up() && !g.capturing && !g.editing) {
            enter_section(strip_tab(t));
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
        }
    }
    if (g_req_host_game >= 0) {
        return mp_take_host_game();
    }
    if (g_req_coop_new) {
        /* a new co-op game: named on the keyboard, then hosted */
        g_req_coop_new = 0;
        return EIDOS_SUI_REDRAW | keyboard_open(KB_NEW_GAME);
    }
    if (g_req_paste) {
        /* the clipboard's code: joined with as a typed one is (eidos_sui_keyboard_done) */
        char text[160], code[16];

        g_req_paste = 0;
        g_look.said[0] = '\0';
        if (eidos_settings_clipboard_get(text, (int)sizeof text) != 0 ||
            !eidos_sui_code_from_text(text, code, (int)sizeof code)) {
            put(g_look.said, (int)sizeof g_look.said, STR_MP_NO_PASTE);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        g_mp_view = MP_VIEW_JOIN;
        g_sess.have = 0;
        build_rows();
        g.cursor = 0;
        g.scroll = 0;
        eidos_settings_session_set_join_code(code);
        return EIDOS_SUI_REDRAW | session_action(ROW_MP_JOIN_GO);
    }
    if (g_req_continue >= 0) {
        int want = g_req_continue;
        EidosSuiGame gm;

        g_req_continue = -1;
        if (eidos_sui_mp_game(want, &gm) != 0 || !gm.ok) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (gm.kind == EIDOS_SUI_GAME_COOP) {
            int res;

            g_req_host_game = gm.kept;      /* hosted again: a new code */
            g_look.said[0] = '\0';
            res = mp_take_host_game();
            if (g_sess.busy) {
                g_host_from_hub = 1;        /* (poll_session takes its answer) */
            } else if (g_mp_view == MP_VIEW_HOST && !mp_in_game()) {
                /* it did not start: the hub again, saying why, not the Host view's list */
                put(g_look.said, (int)sizeof g_look.said, STR_MP_HOST_BUSY);
                g_mp_view = MP_VIEW_CHOOSE;
                build_rows();
                g.cursor = 0;
                g.scroll = 0;
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
            }
            return res;
        }
        /* a mode's save: the game goes on from it; the mod reads its own
         * saved data (no mode is told) */
        if (!bw_slots_continue(gm.slot)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (getenv("EIDOS_MP_UI_LOG") != NULL) {
            fprintf(stderr, "eidos: mp ui: Your games: slot %d goes on\n", gm.slot);
        }
        bw_mod_title_request_autostart();
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
    }
    if (lookup_step()) {
        return EIDOS_SUI_REDRAW;
    }
    if (g_req_abs >= 0) {
        return mp_take_row();
    }
    if (g_req_row >= 0) {
        int row = g_req_row;

        g_req_row = -1;
        (void)eidos_sui_hover(row);     /* the row first, so the touch acts on it */
        return eidos_sui_touch(row, g_req_zone);
    }
    res = sui_input(pressed, held);

    /* the host's lobby rules go to the players when one changed */
    if (mp_in_game()) {
        bw_mod_menu_rules_push();
    }

    /*
     * The port's own session: the work runs on the port's own worker (the
     * relay takes up to three seconds to answer), so the screen asks for
     * the answer here and draws the screen again when it comes.
     */
    if (poll_session()) {
        res |= EIDOS_SUI_REDRAW;
    }
    /* The game has started: the screen closes and the title menu goes
     * straight into the game (sdk/mod_title.h). */
    if (bw_mp_started_pending()) {
        res |= EIDOS_SUI_EXIT;
    }
    return res;
}

static int sui_input(unsigned pressed, unsigned held)
{
    unsigned act;
    const Row *r;
    int res;

    if (question_up()) {
        /* The menu's own question is up: it takes the input until it is
         * answered. */
        return question_input(pressed, held);
    }

    if (g.editing) {
        return poll_text();
    }

    if (g.capturing) {
        res = poll_capture();
        if (g.capturing && (pressed & 2u)) {          /* B cancels when no platform arms the capture */
            eidos_settings_capture_cancel();
            g.capturing = 0;
            g.wait_release = 1;
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        return res;
    }

    if (g.wait_release) {
        /* The press that set a key must not also act as that button.  The
         * buttons given here are the game's, a frame behind the host's (and
         * `held` is the game's repeat, empty between repeats), so the host's
         * own buttons must also be up, for two frames running. */
        if ((pressed | held) == 0u && eidos_settings_host_buttons() == 0u) {
            if (++g.wait_release > 2) {
                g.wait_release = 0;
            }
        } else {
            g.wait_release = 1;
        }
        return 0;
    }

    act = repeat_dirs(pressed, held);
    if (pressed & 2u) {
        if (g.tab == TAB_SESSION && g_mp_view != MP_VIEW_CHOOSE &&
            !mp_in_game() && !g_sess.busy) {
            int was = g_mp_view;
            int from = was == MP_VIEW_JOIN || was == MP_VIEW_WORLD || was == MP_VIEW_ROOM ? ROW_SESSION_JOIN
                     : was == MP_VIEW_MODE ? ROW_MP_MODE : ROW_SESSION_HOST;

            g_look.said[0] = '\0';          /* the World view's word stays there */
            if (g_look.busy) {
                bw_mod_net_lookup_stop();   /* B while the code is asked about */
                g_look.busy = 0;
            }
            g_mp_view = MP_VIEW_CHOOSE;
            build_rows();
            /* back on the row the view came from */
            for (g.cursor = 0; g.cursor < g.nrows - 1; g.cursor++) {
                if (g.rows[g.cursor].type == from &&
                    (from != ROW_MP_MODE || (g.rows[g.cursor].arg == g_mode_mod &&
                                             g.rows[g.cursor].arg2 == g_mode_k))) {
                    break;
                }
            }
            g.scroll = 0;
            clamp_scroll();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_BACK;
    }
    if ((pressed & 0x300u) && !g_mp_only) {
        /* L and R: the tab before or after */
        int d = (pressed & 0x100u) ? 1 : -1, n = strip_count();

        enter_section(strip_tab((strip_pos(g.tab) + d + n) % n));
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x40u) {
        move_cursor(-1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x80u) {
        move_cursor(1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (on_back()) {
        if (pressed & 1u) {
            if (g.level == 0) {
                return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
            }
            leave_section();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    r = &g.rows[g.cursor];
    if (r->type == ROW_SECTION) {
        if (pressed & 1u) {
            enter_section(r->arg);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_OPTION) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        const EidosOption *o = eidos_settings_option(r->arg);
        int choice = o != NULL && o->kind == EIDOS_OPTKIND_CHOICE;
        if (d == 0 && (pressed & 1u) && choice) {
            d = 1;
        }
        if (d != 0 && change_option(r->arg, d, choice)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_PANEL) {
        /* The run-status panel's switch: left and right turn it on and off
         * alike, and the model's own value is what the drawing asks for. */
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0) {
            eidos_settings_panel_set_visible(!eidos_settings_panel_visible());
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_MOD_SWITCH) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0 && toggle_mod(r->arg)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        /* The player's own order: Y moves the mod towards the front of the
         * list, X towards the back. */
        if (pressed & 0x800u) {
            if (move_mod(r->arg, 1)) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
            }
        }
        if (pressed & 0x400u) {
            if (move_mod(r->arg, 0)) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
            }
        }
        return 0;
    }
    if (r->type == ROW_MOD_OPTION) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        int kind = bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2);

        if (kind == BW_MOD_SETTING_TEXT || kind == BW_MOD_SETTING_SECRET) {
            return (pressed & 1u) ? mod_text_edit(r->arg, r->arg2) : 0;
        }
        if (kind == BW_MOD_SETTING_BUTTON) {
            return (pressed & 1u) ? mod_button(r->arg, r->arg2) : 0;
        }
        if (kind == BW_MOD_SETTING_SHOW) {
            return 0;                   /* a line to read */
        }
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0 && change_mod_option(r->arg, r->arg2, d)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_NET_RELAY_PORT || r->type == ROW_NET_CONTROL_PORT) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (pressed & 1u) {
            /* A: type the port on the keyboard, as the address is typed */
            begin_text_edit(r->type);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        if (d != 0) {
            change_net_port(r->type, d);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_INJECT_MAX) {
        /* The longest press the injection allows: the model's own value, in
         * frames.  0 turns the injection off. */
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d != 0) {
            int v = eidos_settings_input_inject_max() + d * INJECT_STEP;

            if (v != eidos_settings_input_inject_max()) {
                eidos_settings_input_set_inject_max(v);
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        return 0;
    }
    if (r->type == ROW_NET_RELAY || r->type == ROW_NET_NAME ||
        r->type == ROW_SESSION_JOINCODE) {
        if (pressed & 1u) {
            return text_row_action(r->type);
        }
        return 0;
    }
    if (r->type == ROW_SESSION_CODE) {
        if (pressed & 1u) {
            /* A: the code onto the clipboard, to paste to the others */
            g_copied = eidos_settings_clipboard_set(eidos_settings_session_code()) == 0;
            return EIDOS_SUI_REDRAW |
                   (g_copied ? EIDOS_SUI_SFX_SELECT : EIDOS_SUI_SFX_BACK);
        }
        return 0;
    }
    if (r->type == ROW_SESSION_HOST || r->type == ROW_SESSION_JOIN ||
        r->type == ROW_SESSION_LEAVE || r->type == ROW_SESSION_REVERT ||
        r->type == ROW_SESSION_DEFAULTS || r->type == ROW_MP_START ||
        r->type == ROW_MP_JOIN_GO || r->type == ROW_MP_GAME ||
        r->type == ROW_MP_NEW_GAME || r->type == ROW_MP_MODE ||
        r->type == ROW_MP_MODE_START) {
        if (pressed & 1u) {
            return session_action(r->type);
        }
        return 0;
    }
    if (pressed & 1u) {
        switch (r->type) {
        case ROW_BIND:
            eidos_settings_capture_begin();
            g.capturing = r->arg + 1;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_DEFAULTS:
            /* the tab's own settings back to their defaults */
            if (g.tab == 2) {
                eidos_settings_reset_bindings();
            } else {
                int i;

                for (i = 0; i < g.nrows; i++) {
                    if (g.rows[i].type == ROW_OPTION) {
                        const EidosOption *o = eidos_settings_option(g.rows[i].arg);

                        if (o != NULL) {
                            (void)eidos_settings_set(g.rows[i].arg, o->def);
                        }
                    }
                }
            }
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_MOD_CLEAR:
            /* The player's "clear saved data" for one mod: the mod's own
             * state file goes and its folder stays (sdk/mod_menu.h). */
            if (bw_mod_menu_clear((u32)r->arg) == 0) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        case ROW_MOD_REVERT:
            /* The switches go back to the port's list, and the panel's own
             * switch goes back to the state the last commit left. */
            bw_mod_menu_revert();
            eidos_settings_panel_revert();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_MOD_RELOAD:
            return reload_mods();
        case ROW_MOD_RESTART:
            /* The run ends: the player is asked first (the menu's own
             * question). */
            return question_ask(QUESTION_RESTART);
        case ROW_NET_DEFAULTS:
            /* The network settings back to their defaults, and the
             * injection's limit with them: it is this section's own row. */
            eidos_settings_net_defaults();
            eidos_settings_input_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

int eidos_sui_row_count(void)
{
    int n;

    if (question_up()) {
        return 3;               /* the header, the question, the grey bar */
    }
    n = g.nrows - g.scroll;
    if (n > visible_rows()) {
        n = visible_rows();
    }
    /* the header, the visible rows, and the grey bar inside a section */
    return 1 + n + (has_back() ? 1 : 0);
}

const char *eidos_sui_tab_name(int i)
{
    if (i < 0 || i >= strip_count()) {
        return "";
    }
    return tab_title(strip_tab(i));
}

int eidos_sui_scroll(int *first, int *shown, int *total)
{
    if (first != NULL) {
        *first = question_up() ? 0 : g.scroll;
    }
    if (shown != NULL) {
        *shown = visible_rows();
    }
    if (total != NULL) {
        *total = question_up() ? 1 : g.nrows;
    }
    return 0;
}

int eidos_sui_has_back(void)
{
    return question_up() || has_back();
}

int eidos_sui_cursor_row(void)
{
    if (question_up()) {
        return 1;               /* the question's own row */
    }
    if (on_back()) {
        return eidos_sui_row_count() - 1;
    }
    return 1 + g.cursor - g.scroll;
}

/* 1 when left and right change the row's value (an option row). */
/* A pad control (EIDOS_PADBIND_*) as its icon's number: SDL's button order,
 * then the triggers and the left stick's four directions (padglyph.h). */
static int pad_icon(int kind, int index)
{
    if (kind == EIDOS_PADBIND_BUTTON) {
        return index >= 0 && index <= 14 ? index : -1;
    }
    switch (index) {
    case 4:  return kind == EIDOS_PADBIND_AXIS_POS ? 15 : -1;     /* LT */
    case 5:  return kind == EIDOS_PADBIND_AXIS_POS ? 16 : -1;     /* RT */
    case 1:  return kind == EIDOS_PADBIND_AXIS_NEG ? 17 : 18;     /* stick up, down */
    case 0:  return kind == EIDOS_PADBIND_AXIS_NEG ? 19 : 20;     /* stick left, right */
    default: return -1;
    }
}

int eidos_sui_row_pad(int row, char *key, int nk)
{
    const Row *r;
    int slot, kind, index;

    if (key != NULL && nk > 0) {
        key[0] = '\0';
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return -1;
    }
    r = &g.rows[g.scroll + row - 1];
    if (r->type != ROW_BIND || g.capturing == r->arg + 1) {
        return -1;
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        int sc = eidos_settings_get_key(r->arg, slot);

        if (sc != 0 && key != NULL && nk > 0) {
            eidos_scancode_name(sc, key, (size_t)nk);
            break;
        }
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (eidos_settings_get_pad(r->arg, slot, &kind, &index) == 0 && kind >= 0) {
            return pad_icon(kind, index);
        }
    }
    return -1;
}

int eidos_sui_row_adjustable(int row)
{
    const Row *r;

    if (row < 1 || (has_back() && row == eidos_sui_row_count() - 1)
        || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_MOD_OPTION:
        /* (a text, a password, a button and a mod's line have no value to
         * step: A opens the keyboard or presses it) */
        return bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2) < BW_MOD_SETTING_TEXT;
    case ROW_OPTION:
    case ROW_PANEL:
    case ROW_INJECT_MAX:
    case ROW_NET_RELAY_PORT:
    case ROW_NET_CONTROL_PORT:
        return 1;
    default:
        return 0;
    }
}

/*
 * The stylus went down on a row (a row of eidos_sui_row_count()).  `zone`
 * says where: 0..2 one of the choices drawn on the row, EIDOS_SUI_ZONE_DEC
 * or _INC the arrows around a value, EIDOS_SUI_ZONE_ROW anywhere else.  The
 * cursor goes to the row.  A choice is set at once; the arrows act when
 * the row was already the selected one (they are only drawn there); a row
 * without a value acts as it does for A.  Returns what eidos_sui_input does.
 */
int eidos_sui_touch(int row, int zone)
{
    const Row *r;
    int was, res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;

    if (question_up()) {
        /* The menu's own question is up: a touch on YES or NO answers it,
         * on the rest of its row with the answer shown (the grey bar says
         * no). */
        if (row == 1) {
            return question_close(zone == 0 ? 1 : zone == 1 ? 0 : g_q.answer);
        }
        if (row == 2) {
            return question_close(0);
        }
        return 0;
    }
    if (g.wait_release) {
        return 0;
    }
    if (g.capturing) {
        eidos_settings_capture_cancel();
        g.capturing = 0;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    if (g.editing) {
        /* The text is being edited: the keyboard is the player's now. */
        return 0;
    }
    if (has_back() && row == eidos_sui_row_count() - 1) {
        leave_section();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    was = g.cursor == g.scroll + row - 1;
    g.cursor = g.scroll + row - 1;
    r = &g.rows[g.cursor];
    switch (r->type) {
    case ROW_SECTION:
        enter_section(r->arg);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_OPTION: {
        const EidosOption *o = eidos_settings_option(r->arg);

        if (o == NULL) {
            break;
        }
        if (zone >= 0 && o->kind == EIDOS_OPTKIND_CHOICE && o->min + zone <= o->max) {
            if (eidos_settings_get(r->arg) != o->min + zone
                && eidos_settings_set(r->arg, o->min + zone) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        } else if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            if (change_option(r->arg, zone == EIDOS_SUI_ZONE_INC ? 1 : -1, 0)) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    }
    case ROW_PANEL:
        /* The switch is drawn as two choices, off and on. */
        if (zone >= 0 && zone <= 1) {
            eidos_settings_panel_set_visible(zone);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        } else if (was) {
            eidos_settings_panel_set_visible(!eidos_settings_panel_visible());
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_MOD_SWITCH:
        /* The switch is drawn as two choices, off and on. */
        if (zone >= 0 && zone <= 1) {
            if (bw_mod_menu_set_enabled((u32)r->arg, zone) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        } else if (was && toggle_mod(r->arg)) {
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_MP_MODE_TYPE:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC || zone == EIDOS_SUI_ZONE_ROW)) {
            res = mode_type_step(zone == EIDOS_SUI_ZONE_DEC ? -1 : 1) ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT
                                                                     : EIDOS_SUI_REDRAW;
        }
        break;
    case ROW_MP_WORLD_JOIN:
    case ROW_MP_ROOM_SLOT:
    case ROW_MP_ROOM_PASS:
    case ROW_MP_ROOM_JOIN:
        if (was) {
            res = session_action(r->type);
        }
        break;
    case ROW_MP_ROOM_LINE:
        break;
    case ROW_MP_WORLD_LINE:
        break;
    case ROW_MP_YOUR_GAME:
        if (was) {
            g_req_continue = r->arg;    /* goes on at the next step */
            res = EIDOS_SUI_REDRAW;
        }
        break;
    case ROW_MOD_OPTION: {
        int kind = bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2);

        if (kind == BW_MOD_SETTING_TEXT || kind == BW_MOD_SETTING_SECRET) {
            if (was) {
                res = mod_text_edit(r->arg, r->arg2);
            }
            break;
        }
        if (kind == BW_MOD_SETTING_BUTTON) {
            if (was) {
                res = mod_button(r->arg, r->arg2);
            }
            break;
        }
        if (kind == BW_MOD_SETTING_SHOW) {
            break;
        }
        if (kind == BW_MOD_SETTING_BOOL && zone >= 0 && zone <= 1) {
            /* drawn as two choices, off and on: the one touched */
            if ((bw_mod_menu_option_value((u32)r->arg, (u32)r->arg2) != 0) != zone
                && change_mod_option(r->arg, r->arg2, 1)) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
            break;
        }
    }
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            if (change_mod_option(r->arg, r->arg2,
                                  zone == EIDOS_SUI_ZONE_INC ? 1 : -1)) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    case ROW_MOD_CLEAR:
        if (was) {
            if (bw_mod_menu_clear((u32)r->arg) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            } else {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
            }
        }
        break;
    case ROW_MOD_REVERT:
        if (was) {
            bw_mod_menu_revert();
            eidos_settings_panel_revert();
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_MOD_RELOAD:
        if (was) {
            res = reload_mods();
        }
        break;
    case ROW_MOD_RESTART:
        if (was) {
            /* The run ends: the player is asked first. */
            res = question_ask(QUESTION_RESTART);
        }
        break;
    case ROW_NET_RELAY:
    case ROW_NET_NAME:
    case ROW_SESSION_JOINCODE:
        if (was) {
            res = text_row_action(r->type);
        }
        break;
    case ROW_SESSION_HOST:
    case ROW_SESSION_JOIN:
    case ROW_SESSION_LEAVE:
    case ROW_SESSION_REVERT:
    case ROW_SESSION_DEFAULTS:
    case ROW_MP_START:
    case ROW_MP_JOIN_GO:
    case ROW_MP_GAME:
    case ROW_MP_NEW_GAME:
    case ROW_MP_MODE:
    case ROW_MP_MODE_START:
        if (was) {
            res = session_action(r->type);
        }
        break;
    case ROW_NET_RELAY_PORT:
    case ROW_NET_CONTROL_PORT:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            change_net_port(r->type, zone == EIDOS_SUI_ZONE_INC ? 1 : -1);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        } else if (was) {
            begin_text_edit(r->type);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_INJECT_MAX:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            int v = eidos_settings_input_inject_max() +
                    (zone == EIDOS_SUI_ZONE_INC ? INJECT_STEP : -INJECT_STEP);

            if (v != eidos_settings_input_inject_max()) {
                eidos_settings_input_set_inject_max(v);
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    case ROW_BIND:
        eidos_settings_capture_begin();
        g.capturing = r->arg + 1;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_DEFAULTS:
        if (was) {
            eidos_settings_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_NET_DEFAULTS:
        if (was) {
            eidos_settings_net_defaults();
            eidos_settings_input_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    default:
        break;
    }
    return res;
}

int eidos_sui_capturing(void)
{
    return g.capturing;
}

int eidos_sui_tab(void)
{
    /* its place on the strip (eidos_sui_tab_name's index) */
    return g.level == 0 ? -1 : strip_pos(g.tab);
}

int eidos_sui_level(void)
{
    return g.level;
}

int eidos_sui_tab_count(void)
{
    /* the Multiplayer screen has no tabs */
    return g_mp_only ? 0 : strip_count();
}

static const char *tab_title(int t)
{
    if (t >= TAB_MOD_BASE) {
        int k = t - TAB_MOD_BASE;

        return k < g_modtab_n ? bw_mod_menu_tab_title((u32)g_modtab[k].mod, (u32)g_modtab[k].tab) : "";
    }
    return t >= 0 && t < TAB_COUNT ? k_tab_names[t] : "";
}

static void option_value_of(int id, int v, char *out, int n)
{
    const EidosOption *o = eidos_settings_option(id);
    size_t i;
    if (o != NULL && o->kind == EIDOS_OPTKIND_CHOICE) {
        for (i = 0; i < sizeof k_value_names / sizeof k_value_names[0]; i++) {
            if (k_value_names[i].id == id && k_value_names[i].index == v) {
                put(out, n, k_value_names[i].text);
                return;
            }
        }
    }
    if (o != NULL && o->zero_name != NULL && v == 0) {
        put(out, n, STR_AUTO);
        return;
    }
    eidos_settings_value_text(id, out, (size_t)n);
}

static void option_value(int id, char *out, int n)
{
    option_value_of(id, eidos_settings_get(id), out, n);
}

static void pad_short(int kind, int index, char *out, int n)
{
    char tmp[48];
    const char *t = NULL;
    if (kind == EIDOS_PADBIND_BUTTON) {
        if (index >= 0 && index < (int)(sizeof k_pad_short / sizeof k_pad_short[0])) {
            t = k_pad_short[index];
        }
    } else if (index >= 0 && index < 6) {
        t = kind == EIDOS_PADBIND_AXIS_POS ? k_axis_pos_short[index] : k_axis_neg_short[index];
    }
    if (t == NULL) {
        eidos_pad_control_name(kind, index, tmp, sizeof tmp);
        t = tmp;
    }
    put(out, n, t);
}

/* "Key / Pad": the first key and the first pad control, in short names. */
static void bind_value(int button, char *out, int n)
{
    char key[48], pad[32];
    int slot, kind, index;
    key[0] = '\0';
    pad[0] = '\0';
    for (slot = 0; slot < EIDOS_BIND_SLOTS && key[0] == '\0'; slot++) {
        int sc = eidos_settings_get_key(button, slot);
        if (sc != 0) {
            eidos_scancode_name(sc, key, sizeof key);
        }
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (eidos_settings_get_pad(button, slot, &kind, &index) == 0 && kind >= 0) {
            pad_short(kind, index, pad, sizeof pad);
            break;
        }
    }
    if (key[0] == '\0' && pad[0] == '\0') {
        put(out, n, STR_UNBOUND);
    } else if (pad[0] == '\0') {
        put(out, n, key);
    } else if (key[0] == '\0') {
        snprintf(out, (size_t)n, "- / %s", pad);
    } else {
        snprintf(out, (size_t)n, "%s / %s", key, pad);
    }
}

/* A text of the port's own settings: what is not set says so. */
static const char *net_text(const char *s)
{
    return (s != NULL && s[0] != '\0') ? s : STR_NONE;
}

void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv)
{
    const Row *r;
    char buf[96];

    if (nl > 0) { label[0] = '\0'; }
    if (nv > 0) { value[0] = '\0'; }
    if (question_up()) {
        /* The menu's own question: the header, the question and the grey
         * bar. */
        if (row == 0) {
            put(label, nl, STR_Q_TITLE);
        } else if (row == 1) {
            put(label, nl, k_questions[g_q.asking].label);
            put(value, nv, g_q.answer ? STR_Q_YES : STR_Q_NO);
        } else if (row == 2) {
            put(label, nl, STR_Q_CANCEL);
        }
        return;
    }
    if (row == 0) {
        int above = g.scroll > 0;
        int below = g.scroll + visible_rows() < g.nrows;

        (void)above;
        (void)below;
        put(label, nl, tab_title(g.tab));
        return;
    }
    if (has_back() && row == eidos_sui_row_count() - 1) {
        put(label, nl, STR_BACK_L1);
        return;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_SECTION:
        put(label, nl, tab_title(r->arg));
        break;
    case ROW_OPTION:
        put(label, nl, k_option_labels[r->arg] != NULL ? k_option_labels[r->arg] : "");
        option_value(r->arg, value, nv);
        break;
    case ROW_BIND: {
        const EidosButtonInfo *b = eidos_settings_button(r->arg);
        put(label, nl, r->arg >= 0 && r->arg < (int)(sizeof k_button_short / sizeof k_button_short[0])
                           ? k_button_short[r->arg] : (b != NULL ? b->label : ""));
        if (g.capturing == r->arg + 1) {
            put(value, nv, STR_PRESS_KEY);
        } else {
            bind_value(r->arg, value, nv);
        }
        break;
    }
    case ROW_PANEL:
        /* The run-status panel and the player's switch. */
        put(label, nl, STR_PANEL);
        put(value, nv, eidos_settings_panel_visible() ? STR_ON : STR_OFF);
        break;
    case ROW_MOD_SWITCH:
        /* The mod's name and its switch; a mod whose folder is gone says
         * so, and stays off until it is back. */
        if (!bw_mod_menu_present_at((u32)r->arg)) {
            snprintf(buf, sizeof buf, ui_trf("%s (missing)"),
                     bw_mod_menu_name_at((u32)r->arg));
            put(label, nl, buf);
        } else if (bw_mod_menu_needs_eidos_at((u32)r->arg) != NULL) {
            /* A mod made for a newer eidos: the port does not load it.  The
             * row says which eidos it needs, and its switch does nothing. */
            snprintf(buf, sizeof buf, ui_trf("%s (eidos %s or later)"), bw_mod_menu_name_at((u32)r->arg),
                     bw_mod_menu_needs_eidos_at((u32)r->arg));
            put(label, nl, buf);
            put(value, nv, STR_UNAVAILABLE);
            break;
        } else if (bw_mod_menu_unavailable_at((u32)r->arg)) {
            /* A mod made for the other game: the port does not load it.
             * The row says which game it needs, and its switch does
             * nothing. */
            const int g = bw_mod_menu_needs_game_at((u32)r->arg);

            snprintf(buf, sizeof buf, ui_trf(g == BW_MOD_GAME_WHITE ? "%s (White only)" : g == BW_MOD_GAME_BLACK ? "%s (Black only)"
                                                                                                             : "%s (unknown game)"),
                     bw_mod_menu_name_at((u32)r->arg));
            put(label, nl, buf);
            put(value, nv, STR_UNAVAILABLE);
            break;
        } else {
            put(label, nl, bw_mod_menu_name_at((u32)r->arg));
        }
        put(value, nv, bw_mod_menu_enabled_at((u32)r->arg) ? STR_ON : STR_OFF);
        break;
    case ROW_MOD_OPTION:
        /* The mod's own setting, under the mod's name (in a mode's view
         * there is no name above it). */
        snprintf(buf, sizeof buf, g.tab == TAB_SESSION || g.tab >= TAB_MOD_BASE ? "%s" : "  %s",
                 bw_mod_menu_option_label((u32)r->arg, (u32)r->arg2));
        put(label, nl, buf);
        bw_mod_menu_option_text((u32)r->arg, (u32)r->arg2, buf, sizeof buf);
        if (bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2) == BW_MOD_SETTING_SECRET) {
            /* a password: a dot a letter (U+2022, engine/ui/assets.c) */
            int j, len = (int)strlen(buf), o2 = 0;

            for (j = 0; j < len && o2 + 4 < (int)sizeof buf; j++) {
                o2 += 3;
            }
            for (j = 0; j < o2; j += 3) {
                buf[j] = '\xE2';
                buf[j + 1] = '\x80';
                buf[j + 2] = '\xA2';
            }
            buf[o2] = '\0';
        }
        put(value, nv, buf);
        break;
    case ROW_MOD_CLEAR:
        put(label, nl, STR_MOD_CLEAR);
        break;
    case ROW_MOD_REVERT:
        put(label, nl, STR_MOD_REVERT);
        break;
    case ROW_MOD_RELOAD:
        put(label, nl, STR_MOD_RELOAD);
        break;
    case ROW_MOD_FOLDER:
        put(label, nl, STR_MOD_FOLDER);
        if (bw_mod_menu_installed_folder(buf, sizeof buf) != 0) {
            buf[0] = '\0';
        }
        put(value, nv, buf);
        break;
    case ROW_MOD_RESTART:
        put(label, nl, STR_MOD_RESTART);
        break;
    case ROW_ORPHAN: {
        const char *name = bw_mod_menu_orphan_at((u32)r->arg);
        put(label, nl, name != NULL ? name : "");
        put(value, nv, STR_ORPHAN);
        break;
    }
    case ROW_NET_RELAY:
        put(label, nl, STR_NET_RELAY);
        put(value, nv, net_text(eidos_settings_net_relay()));
        break;
    case ROW_NET_RELAY_PORT:
        put(label, nl, STR_NET_RPORT);
        if (eidos_settings_net_port_text(0) != NULL) {
            snprintf(buf, sizeof buf, "%s_", eidos_settings_net_port_text(0));
        } else {
            snprintf(buf, sizeof buf, "%d", eidos_settings_net_relay_port());
        }
        put(value, nv, buf);
        break;
    case ROW_NET_CONTROL_PORT:
        put(label, nl, STR_NET_CPORT);
        if (eidos_settings_net_port_text(1) != NULL) {
            snprintf(buf, sizeof buf, "%s_", eidos_settings_net_port_text(1));
        } else {
            snprintf(buf, sizeof buf, "%d", eidos_settings_net_control_port());
        }
        put(value, nv, buf);
        break;
    case ROW_NET_NAME:
        put(label, nl, STR_NET_NAME);
        put(value, nv, eidos_settings_net_name()[0] != '\0'
                       ? eidos_settings_net_name() : STR_MP_DEFAULT_NAME);
        break;
    case ROW_MP_STATUS:
        put(label, nl, r->arg == STATUS_CONNECTING ? STR_MP_CONNECTING :
                       r->arg == STATUS_WAIT_HOST ? STR_MP_WAIT_HOST :
                       bw_mp_end_reason());
        break;
    case ROW_MP_JOIN_GO:
        put(label, nl, STR_SESSION_JOIN);
        session_value(0, value, nv);
        break;
    case ROW_MP_GAME:
        put(label, nl, g_games[r->arg]);
        if (strcmp(g_games[r->arg], bw_mp_game()) == 0) {
            session_value(1, value, nv);
        }
        break;
    case ROW_MP_NEW_GAME:
        put(label, nl, STR_MP_NEW_GAME);
        if (bw_mp_game()[0] == '\0') {
            session_value(1, value, nv);
        }
        break;
    case ROW_MP_MODE:
        put(label, nl, bw_mod_menu_mode_title((u32)r->arg, (u32)r->arg2));
        if (bw_mod_menu_mode_off((u32)r->arg, (u32)r->arg2)) {
            put(value, nv, STR_MP_MODE_OFF);
        }
        break;
    case ROW_MP_MODE_START:
        put(label, nl, bw_mod_menu_mode_export((u32)g_mode_mod, (u32)g_mode_k) != NULL ? STR_MP_MODE_EXPORT
                       : bw_mod_menu_mode_joins((u32)g_mode_mod, (u32)g_mode_k) ? STR_MP_MODE_MAKE
                       : STR_MP_MODE_START);
        if (bw_mod_menu_mode_off((u32)g_mode_mod, (u32)g_mode_k)) {
            put(value, nv, STR_MP_MODE_OFF);
        } else if (g_mode_said[0] != '\0') {
            put(value, nv, g_mode_said);
        }
        break;
    case ROW_MP_MODE_TYPE:
        put(label, nl, STR_MP_MODE_TYPE);
        put(value, nv, bw_mod_menu_mode_title((u32)g_mode_mod, (u32)g_mode_k));
        break;
    case ROW_MP_WORLD_LINE:
        if (r->arg >= 0 && r->arg < g_look.nlines) {
            put(label, nl, g_look.line_label[r->arg]);
            put(value, nv, g_look.line_value[r->arg]);
        }
        break;
    case ROW_MP_WORLD_JOIN:
        put(label, nl, STR_MP_WORLD_JOIN);
        put(value, nv, g_look.said);
        break;
    case ROW_MP_ROOM_LINE:
        put(label, nl, "Server");
        snprintf(buf, sizeof buf, "%s:%s", g_room.host, g_room.port);
        put(value, nv, buf);
        break;
    case ROW_MP_ROOM_SLOT:
        put(label, nl, "Slot");
        put(value, nv, g_room.slot);
        break;
    case ROW_MP_ROOM_PASS:
        put(label, nl, "Password");
        put(value, nv, g_room.pass[0] != '\0' ? "*****" : "None");
        break;
    case ROW_MP_ROOM_JOIN:
        put(label, nl, STR_MP_ROOM_JOIN);
        put(value, nv, g_room.said);
        break;
    case ROW_MP_YOUR_GAME: {
        EidosSuiGame gm;

        if (eidos_sui_mp_game(r->arg, &gm) == 0) {
            put(label, nl, gm.title);
            if (!gm.ok) {
                snprintf(buf, sizeof buf, "Needs %s", gm.owner);
            } else {
                snprintf(buf, sizeof buf, "%s%s%s", gm.host ? "Host" : "Joined", gm.code[0] != '\0' ? " " : "",
                         gm.code);
            }
            put(value, nv, buf);
        }
        break;
    }
    case ROW_INJECT_MAX:
        /* The longest press the injection allows, in frames.  0 is off. */
        put(label, nl, STR_INJECT);
        if (eidos_settings_input_inject_max() == 0) {
            put(value, nv, STR_OFF);
        } else {
            snprintf(buf, sizeof buf, "%d", eidos_settings_input_inject_max());
            put(value, nv, buf);
        }
        break;
    case ROW_SESSION_CODE:
        /* The code in force: the one the relay handed out when the player
         * hosted a session, or the one the player joined a session with.
         * The code the player types is a row of its own. */
        put(label, nl, STR_SESSION_CODE);
        if (g_copied) {
            snprintf(buf, sizeof buf, "%s  %s", net_text(eidos_settings_session_code()),
                     ui_tr(STR_COPIED));
            put(value, nv, buf);
        } else {
            put(value, nv, net_text(eidos_settings_session_code()));
        }
        break;
    case ROW_SESSION_JOINCODE:
        /* The code being typed: the one the player types to join a
         * session.  Hosting a session does not overwrite it. */
        put(label, nl, STR_SESSION_JOINCODE);
        put(value, nv, net_text(eidos_settings_session_join_code()));
        break;
    case ROW_SESSION_HOST:
        put(label, nl, STR_SESSION_HOST);
        session_value(1, value, nv);
        break;
    case ROW_SESSION_JOIN:
        put(label, nl, STR_SESSION_JOIN);
        break;
    case ROW_SESSION_LEAVE:
        put(label, nl, STR_SESSION_LEAVE);
        break;
    case ROW_SESSION_SELF:
        /* This player's own number in the session. */
        put(label, nl, STR_SESSION_SELF);
        snprintf(buf, sizeof buf, "#%u",
                 (unsigned int)bw_port_session_self_peer());
        put(value, nv, buf);
        break;
    case ROW_SESSION_PEER: {
        /* A peer of the session: the name the relay gave it and the number
         * it has. */
        char name[BW_MOD_NET_NAME_MAX + 1];

        name[0] = '\0';
        if (bw_port_session_peer_at((u32)r->arg2, NULL, name,
                                    (u32)sizeof(name)) != 0) {
            name[0] = '\0';
        }
        put(label, nl, name[0] != '\0' ? name : STR_SESSION_PEER);
        snprintf(buf, sizeof buf, "#%u", (unsigned int)(u32)r->arg);
        put(value, nv, buf);
        break;
    }
    case ROW_MP_START: {
        /* The host's Start: ready, or what it waits for. */
        const char *why = bw_mp_start_blocker();
        BwMpState st = bw_mp_state();

        put(label, nl, STR_MP_START);
        if (st == BW_MP_PLAYING || st == BW_MP_GRACE) {
            put(value, nv, STR_MP_RUNNING);
        } else {
            put(value, nv, why[0] != '\0' ? why : STR_MP_READY);
        }
        break;
    }
    case ROW_MP_PLAYER: {
        /* A player of the lobby: its name, and whether it is here, its
         * game, and whether its multiplayer mods match the host's. */
        BwMpPlayer p;

        memset(&p, 0, sizeof p);
        (void)bw_mp_player_at((u32)r->arg2, &p);
        put(label, nl, p.name[0] != '\0' ? p.name : STR_SESSION_PEER);
        snprintf(buf, sizeof buf, "#%u %s%s", (unsigned int)p.peer,
                 !p.connected ? ui_tr(STR_MP_MISSING) :
                 p.game == 'W' ? ui_tr("White") : p.game == 'B' ? ui_tr("Black") : "...",
                 p.connected && p.game != 0 && !p.mods_ok ? ui_tr(STR_MP_MODS) : "");
        put(value, nv, buf);
        break;
    }
    case ROW_SESSION_REVERT:
        put(label, nl, STR_SESSION_REVERT);
        break;
    case ROW_SESSION_DEFAULTS:
        put(label, nl, STR_DEFAULTS);
        break;
    default:
        put(label, nl, STR_DEFAULTS);
        break;
    }
}

int eidos_sui_row_choices(int row, int *active, char names[3][20])
{
    const Row *r;

    if (question_up()) {
        /* the question's row: YES and NO, the one the cursor is on lit (a
         * touch screen answers by tapping one) */
        if (row != 1) {
            return 0;
        }
        put(names[0], 20, STR_Q_YES);
        put(names[1], 20, STR_Q_NO);
        *active = g_q.answer ? 0 : 1;
        return 2;
    }
    const EidosOption *o;
    int i, count;

    if (question_up()) {
        /* The menu's own question carries its answer as the row's value:
         * the drawing paths differ in what they show of a row. */
        return 0;
    }
    if (row < 1 || (has_back() && row == eidos_sui_row_count() - 1)
        || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    r = &g.rows[g.scroll + row - 1];
    if (r->type == ROW_PANEL) {
        /* The panel's switch: two choices, off and on. */
        put(names[0], 20, STR_OFF);
        put(names[1], 20, STR_ON);
        if (active != NULL) {
            *active = eidos_settings_panel_visible() ? 1 : 0;
        }
        return 2;
    }
    if (r->type == ROW_MOD_OPTION && bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2) == BW_MOD_SETTING_BOOL) {
        /* A mod's on/off option: the two choices, as the port's own and the
         * mod's switch have them (not "on" between arrows) */
        put(names[0], 20, STR_OFF);
        put(names[1], 20, STR_ON);
        if (active != NULL) {
            *active = bw_mod_menu_option_value((u32)r->arg, (u32)r->arg2) ? 1 : 0;
        }
        return 2;
    }
    if (r->type == ROW_MOD_SWITCH) {
        /* The mod's switch: two choices, off and on.  A mod made for the
         * other game has none: its row says "Unavailable". */
        if (bw_mod_menu_unavailable_at((u32)r->arg)) {
            return 0;
        }
        put(names[0], 20, STR_OFF);
        put(names[1], 20, STR_ON);
        if (active != NULL) {
            *active = bw_mod_menu_enabled_at((u32)r->arg) && !bw_mod_menu_unavailable_at((u32)r->arg) ? 1 : 0;
        }
        return 2;
    }
    if (r->type != ROW_OPTION) {
        return 0;
    }
    o = eidos_settings_option(r->arg);
    if (o == NULL || o->kind != EIDOS_OPTKIND_CHOICE) {
        return 0;
    }
    count = o->max - o->min + 1;
    if (count < 2 || count > 3) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        size_t k;
        int found = 0;

        for (k = 0; k < sizeof k_choice_names / sizeof k_choice_names[0]; k++) {
            if (k_choice_names[k].id == r->arg && k_choice_names[k].index == o->min + i) {
                put(names[i], 20, k_choice_names[k].text);
                found = 1;
            }
        }
        if (!found) {
            option_value_of(r->arg, o->min + i, names[i], 20);
        }
    }
    if (active != NULL) {
        *active = eidos_settings_get(r->arg) - o->min;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Help text for the bottom screen */

int eidos_sui_help_count(void)
{
    return 4;
}

void eidos_sui_help_line(int i, char *out, int n)
{
    const char *l[4] = { "", "", "", "" };
    const Row *r = (g.nrows > 0 && !on_back()) ? &g.rows[g.cursor] : NULL;
    if (n > 0) {
        out[0] = '\0';
    }
    if (question_up()) {
        /* The menu's own question: what it asks, and how it is answered. */
        l[0] = k_questions[g_q.asking].help;
        l[1] = HELP_Q_CHOOSE2;
        l[2] = HELP_Q_CHOOSE1;
        l[3] = "";
    } else if (g.capturing) {
        l[0] = HELP_CAPTURE1;
        l[1] = HELP_CAPTURE2;
    } else if (g.editing) {
        l[0] = HELP_TEXT_EDIT1;
        l[1] = HELP_TEXT_EDIT2;
    } else if (on_back()) {
        l[0] = g.level == 0 ? HELP_BACK_L0 : HELP_BACK_L1;
        l[2] = g.level == 0 ? HELP_KEYS_L0 : HELP_KEYS_L1;
        l[3] = g.level == 0 ? HELP_KEYS_L0B : HELP_KEYS_L1B;
    } else if (g.level == 0 && r != NULL) {
        static const char *const sec[] = {
            HELP_SECTION_DISPLAY, HELP_SECTION_LAYOUT, HELP_SECTION_CONTROLS,
            HELP_SECTION_SOUND, HELP_SECTION_MODS, HELP_SECTION_NETWORK,
            HELP_SECTION_EXTRAS, HELP_SECTION_SESSION };
        l[0] = r->arg >= TAB_MOD_BASE ? HELP_SECTION_MOD_TAB : sec[r->arg];
        l[2] = HELP_KEYS_L0;
        l[3] = HELP_KEYS_L0B;
    } else if (r != NULL) {
        switch (r->type) {
        case ROW_OPTION:
            l[0] = k_option_help[r->arg][0] != NULL ? k_option_help[r->arg][0] : "";
            l[1] = k_option_help[r->arg][1] != NULL ? k_option_help[r->arg][1] : "";
            break;
        case ROW_BIND:
            l[0] = HELP_BIND1;
            l[1] = HELP_BIND2;
            break;
        case ROW_PANEL:
            l[0] = HELP_PANEL1;
            l[1] = HELP_PANEL2;
            break;
        case ROW_MOD_SWITCH:
            if (bw_mod_menu_needs_eidos_at((u32)r->arg) != NULL) {
                /* (made for a newer eidos: why it has no switch) */
                l[0] = HELP_MOD_NEEDS_EIDOS;
                l[1] = HELP_MOD_UPDATE_EIDOS;
                break;
            }
            if (bw_mod_menu_unavailable_at((u32)r->arg)) {
                /* (made for the other game: why it has no switch) */
                const int g_ = bw_mod_menu_needs_game_at((u32)r->arg);

                l[0] = g_ == BW_MOD_GAME_WHITE ? HELP_MOD_FOR_WHITE : g_ == BW_MOD_GAME_BLACK ? HELP_MOD_FOR_BLACK : HELP_MOD_FOR_NONE;
                l[1] = HELP_MOD_NOT_LOADED;
                break;
            }
            l[0] = HELP_MOD_SWITCH1;
            l[1] = HELP_MOD_SWITCH2;
            break;
        case ROW_MOD_OPTION:
            if (g.tab >= TAB_MOD_BASE) {
                /* a mod tab: the mod's own `help` line, split in two where a
                 * word ends nearest its middle */
                static char half[2][160];
                const char *h = bw_mod_menu_option_help((u32)r->arg, (u32)r->arg2);
                int len = h != NULL ? (int)strlen(h) : 0, cut = -1, j;

                for (j = 0; j < len; j++) {
                    if (h[j] == ' ' && (cut < 0 || abs(j - len / 2) < abs(cut - len / 2))) {
                        cut = j;
                    }
                }
                if (len <= 44 || cut < 0) {
                    put(half[0], (int)sizeof half[0], h != NULL ? h : "");
                    half[1][0] = '\0';
                } else {
                    snprintf(half[0], sizeof half[0], "%.*s", cut, h);
                    put(half[1], (int)sizeof half[1], h + cut + 1);
                }
                l[0] = half[0];
                l[1] = half[1];
                break;
            }
            l[0] = HELP_MOD_OPTION1;
            l[1] = HELP_MOD_OPTION2;
            break;
        case ROW_MOD_CLEAR:
            l[0] = HELP_MOD_CLEAR1;
            l[1] = HELP_MOD_CLEAR2;
            break;
        case ROW_MOD_RELOAD:
            l[0] = HELP_MOD_RELOAD1;
            l[1] = HELP_MOD_RELOAD2;
            break;
        case ROW_MOD_FOLDER:
            l[0] = HELP_MOD_FOLDER1;
            l[1] = HELP_MOD_FOLDER2;
            break;
        case ROW_MOD_REVERT:
            l[0] = HELP_MOD_REVERT1;
            l[1] = HELP_MOD_REVERT2;
            break;
        case ROW_ORPHAN:
            l[0] = HELP_ORPHAN1;
            l[1] = HELP_ORPHAN2;
            break;
        case ROW_NET_RELAY:
            l[0] = HELP_NET_RELAY1;
            l[1] = HELP_NET_RELAY2;
            break;
        case ROW_NET_RELAY_PORT:
            l[0] = HELP_NET_RPORT1;
            l[1] = HELP_NET_RPORT2;
            break;
        case ROW_NET_CONTROL_PORT:
            l[0] = HELP_NET_CPORT1;
            l[1] = HELP_NET_CPORT2;
            break;
        case ROW_NET_NAME:
            l[0] = HELP_NET_NAME1;
            l[1] = HELP_NET_NAME2;
            break;
        case ROW_INJECT_MAX:
            l[0] = HELP_INJECT1;
            l[1] = HELP_INJECT2;
            break;
        case ROW_NET_DEFAULTS:
            l[0] = HELP_NET_DEFAULTS1;
            l[1] = HELP_NET_DEFAULTS2;
            break;
        case ROW_SESSION_CODE:
            l[0] = HELP_SESSION_CODE1;
            l[1] = HELP_SESSION_CODE2;
            break;
        case ROW_SESSION_JOINCODE:
            l[0] = HELP_SESSION_JCODE1;
            l[1] = HELP_SESSION_JCODE2;
            break;
        case ROW_SESSION_HOST:
            l[0] = HELP_SESSION_HOST1;
            l[1] = HELP_SESSION_HOST2;
            break;
        case ROW_SESSION_JOIN:
            l[0] = HELP_SESSION_JOIN1;
            l[1] = HELP_SESSION_JOIN2;
            break;
        case ROW_SESSION_LEAVE:
            l[0] = HELP_SESSION_LEAVE1;
            l[1] = HELP_SESSION_LEAVE2;
            break;
        case ROW_SESSION_SELF:
            l[0] = HELP_SESSION_SELF1;
            l[1] = HELP_SESSION_SELF2;
            break;
        case ROW_SESSION_PEER:
            l[0] = HELP_SESSION_PEER1;
            l[1] = HELP_SESSION_PEER2;
            break;
        case ROW_MP_START:
            l[0] = HELP_MP_START1;
            l[1] = HELP_MP_START2;
            break;
        case ROW_MP_PLAYER:
            l[0] = HELP_MP_PLAYER1;
            l[1] = HELP_MP_PLAYER2;
            break;
        case ROW_MP_GAME:
            l[0] = HELP_MP_GAME1;
            l[1] = HELP_MP_GAME2;
            break;
        case ROW_MP_NEW_GAME:
            l[0] = HELP_MP_NEW1;
            l[1] = HELP_MP_NEW2;
            break;
        case ROW_SESSION_REVERT:
            l[0] = HELP_SESSION_REVERT1;
            l[1] = HELP_SESSION_REVERT2;
            break;
        case ROW_SESSION_DEFAULTS:
            l[0] = HELP_DEFAULTS1;
            l[1] = HELP_DEFAULTS2;
            break;
        case ROW_MOD_RESTART:
            l[0] = HELP_MOD_RESTART1;
            l[1] = HELP_MOD_RESTART2;
            break;
        default:
            l[0] = HELP_DEFAULTS1;
            l[1] = HELP_DEFAULTS2;
            break;
        }
        l[2] = HELP_KEYS_L1;
        l[3] = HELP_KEYS_L1B;
        /* a refused host or join: the relay's own words */
        if ((r->type == ROW_SESSION_HOST || r->type == ROW_MP_JOIN_GO ||
             r->type == ROW_MP_GAME || r->type == ROW_MP_NEW_GAME) &&
            g_sess.have && g_sess.rc != BW_MOD_NET_OK &&
            bw_mod_net_last_error_text()[0] != '\0') {
            l[1] = bw_mod_net_last_error_text();
        }
    }
    if (i >= 0 && i < 4) {
        put(out, n, l[i]);
    }
}

/*
 * The mouse is over a row: the cursor goes there and nothing else happens
 * (no sound, no action), so a key goes on from the row the mouse was on.
 * `row` is as for eidos_sui_touch.  Returns 1 when the cursor moved.
 */
int eidos_sui_hover(int row)
{
    int at;

    if (question_up() || g.wait_release || g.capturing || g.editing) {
        return 0;
    }
    if (has_back() && row == eidos_sui_row_count() - 1) {
        at = g.nrows;
    } else if (row >= 1 && g.scroll + row - 1 < g.nrows) {
        at = g.scroll + row - 1;
    } else {
        return 0;
    }
    if (g.cursor == at) {
        return 0;
    }
    g.cursor = at;
    return 1;
}

/*
 * The mouse wheel over the list: the rows move by `steps` (up for a
 * positive count), as a page scrolls, with no sound.  A cursor the move
 * leaves outside the shown rows goes to the nearest shown one, quietly as
 * under eidos_sui_hover.  Returns 0 when the screen takes no wheel now (a
 * question, a capture, an edit): the caller then moves the
 * cursor with keys as before.
 */
int eidos_sui_wheel(int steps)
{
    int top, vis = visible_rows();

    if (question_up() || g.wait_release || g.capturing || g.editing) {
        return 0;
    }
    if (g.nrows <= vis) {
        return 1;                   /* nothing to scroll; no key either    */
    }
    top = g.scroll - steps;
    top = top > g.nrows - vis ? g.nrows - vis : top;
    top = top < 0 ? 0 : top;
    g.scroll = top;
    if (!on_back()) {
        if (g.cursor < top) {
            g.cursor = top;
        } else if (g.cursor >= top + vis) {
            g.cursor = top + vis - 1;
        }
    }
    return 1;
}

/*
 * The Multiplayer screen seen by a one-screen view of its own
 * (engine/ui/recipe_multiplayer.c).  The view asks for an action by name;
 * the screen finds the row that does it and takes a touch on it at its
 * next step, as it takes a click on the settings recipe's rows, so the
 * sounds, the keyboards and the work are the ones the rows have.
 */
static int g_req_host_game = -1;   /* a kept game to host at the next step (declared above) */

int eidos_sui_mp_up(void)
{
    return g_mp_only && g.level == 1 && g.tab == TAB_SESSION;
}

int eidos_sui_mp_view(void)
{
    if (!eidos_sui_mp_up()) {
        return -1;
    }
    if (g_sess.busy || g_look.busy) {
        return EIDOS_SUI_MP_BUSY;
    }
    return g_mp_view == MP_VIEW_JOIN ? EIDOS_SUI_MP_JOIN
         : g_mp_view == MP_VIEW_HOST ? EIDOS_SUI_MP_HOST
         : g_mp_view == MP_VIEW_MODE ? EIDOS_SUI_MP_MODE
         : g_mp_view == MP_VIEW_WORLD ? EIDOS_SUI_MP_WORLD
         : g_mp_view == MP_VIEW_ROOM ? EIDOS_SUI_MP_ROOM : EIDOS_SUI_MP_CHOOSE;
}

/* The mods' ways to play, in the order the Choose view lists them. */
static int mode_at_flat(int k, u32 *mod, u32 *mode)
{
    u32 i, m;

    for (i = 0; i < bw_mod_menu_count(); i++) {
        if (!bw_mod_menu_enabled_at(i) || !bw_mod_menu_present_at(i) || bw_mod_menu_unavailable_at(i)) {
            continue;
        }
        for (m = 0; m < bw_mod_menu_mode_count(i); m++) {
            if (bw_mod_menu_mode_in(i, m) >= 0) {
                continue;               /* a type on another mode's card */
            }
            if (k-- == 0) {
                *mod = i;
                *mode = m;
                return 1;
            }
        }
    }
    return 0;
}

int eidos_sui_mp_modes(void)
{
    u32 a, b;
    int k = 0;

    while (mode_at_flat(k, &a, &b)) {
        k++;
    }
    return k;
}

int eidos_sui_mp_mode_info(int k, const char **id, const char **title, const char **line)
{
    u32 mod, mode;

    if (k < 0) {
        /* the mode the Mode view shows */
        if (g_mode_mod < 0) {
            return -1;
        }
        mod = (u32)g_mode_mod;
        mode = (u32)g_mode_k;
    } else if (!mode_at_flat(k, &mod, &mode)) {
        return -1;
    }
    if (id != NULL) *id = bw_mod_menu_mode_id(mod, mode);
    if (title != NULL) *title = bw_mod_menu_mode_title(mod, mode);
    if (line != NULL) *line = bw_mod_menu_mode_line(mod, mode);
    return bw_mod_menu_mode_off(mod, mode);
}

const char *eidos_sui_mp_mode_owner(void)
{
    return g_mode_mod >= 0 ? bw_mod_menu_name_at((u32)g_mode_mod) : "";
}

int eidos_sui_mp_rows(void)
{
    return eidos_sui_mp_up() && !question_up() ? g.nrows : 0;
}

int eidos_sui_mp_row(int i, char *label, int nl, char *value, int nv)
{
    int keep = g.scroll, kind = EIDOS_SUI_MP_ROW_ACTION;
    const Row *r;

    if (nl > 0) label[0] = '\0';
    if (nv > 0) value[0] = '\0';
    if (!eidos_sui_mp_up() || question_up() || i < 0 || i >= g.nrows) {
        return -1;
    }
    /* the row's text as the list shows it, wherever the list scrolls */
    g.scroll = i;
    eidos_sui_row_text(1, label, nl, value, nv);
    g.scroll = keep;
    r = &g.rows[i];
    if (r->type == ROW_MOD_OPTION) {
        int k2 = bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2);

        kind = k2 == BW_MOD_SETTING_TEXT || k2 == BW_MOD_SETTING_SECRET ? EIDOS_SUI_MP_ROW_TEXT
               : k2 == BW_MOD_SETTING_BUTTON ? EIDOS_SUI_MP_ROW_START
               : k2 == BW_MOD_SETTING_SHOW ? EIDOS_SUI_MP_ROW_INFO : EIDOS_SUI_MP_ROW_VALUE;
    } else if (r->type == ROW_MP_MODE_TYPE) {
        kind = EIDOS_SUI_MP_ROW_VALUE;
    } else if (r->type == ROW_MP_MODE_START || r->type == ROW_MP_WORLD_JOIN || r->type == ROW_MP_ROOM_JOIN) {
        kind = EIDOS_SUI_MP_ROW_START;
    } else if (r->type == ROW_MP_WORLD_LINE || r->type == ROW_MP_ROOM_LINE) {
        kind = EIDOS_SUI_MP_ROW_INFO;
    } else if (r->type == ROW_MP_ROOM_SLOT || r->type == ROW_MP_ROOM_PASS) {
        kind = EIDOS_SUI_MP_ROW_TEXT;
    }
    return kind;
}

int eidos_sui_mp_row_extra(int i, const char **heading, const char **help, int *ok)
{
    static const char *const start_help[3] = {
        "Uses a free save slot, listed under Your games, and "
        "your other saves stay as they are.",
        "Makes the world on the relay and starts your game in it. Its code shows in your game: send it to "
        "your friends, who type it in the Join box.",
        "Saves your options as a file for the Archipelago room's host. The host also needs the "
        "arkhipelagos.apworld file from the mod's releases.",
    };
    const Row *r;

    if (heading != NULL) *heading = "";
    if (help != NULL) *help = "";
    if (ok != NULL) *ok = 1;
    if (!eidos_sui_mp_up() || i < 0 || i >= g.nrows) {
        return -1;
    }
    r = &g.rows[i];
    if (r->type == ROW_MOD_OPTION) {
        if (heading != NULL) *heading = bw_mod_menu_option_heading((u32)r->arg, (u32)r->arg2);
        if (help != NULL) *help = bw_mod_menu_option_help((u32)r->arg, (u32)r->arg2);
    } else if (r->type == ROW_MP_MODE_TYPE) {
        if (help != NULL) *help = "Left/Right switches the mode. The rows below "
                                  "and the text above change with it.";
    } else if (r->type == ROW_MP_MODE_START) {
        int k = bw_mod_menu_mode_export((u32)g_mode_mod, (u32)g_mode_k) != NULL ? 2
                : bw_mod_menu_mode_joins((u32)g_mode_mod, (u32)g_mode_k) ? 1 : 0;

        if (help != NULL) *help = start_help[k];
        if (ok != NULL) *ok = !bw_mod_menu_mode_off((u32)g_mode_mod, (u32)g_mode_k);
    } else if (r->type == ROW_MP_WORLD_JOIN) {
        if (help != NULL) *help = "Starts your game in this world, in a free save slot. It is listed under Your "
                                  "games. The host sets the rules.";
        if (ok != NULL) *ok = g_look.can_join;
    } else if (r->type == ROW_MP_ROOM_SLOT) {
        if (help != NULL) *help = "The slot name in the room's game, as in the options file you sent its host.";
    } else if (r->type == ROW_MP_ROOM_PASS) {
        if (help != NULL) *help = "The room's password, if its host set one. Leave it empty if not.";
    } else if (r->type == ROW_MP_ROOM_JOIN) {
        if (help != NULL) *help = "Starts your game in the room, in a free save slot. It is listed under Your "
                                  "games and links to the room each time you play it.";
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Your games: the co-op games this player hosts, and the saves the     */
/* Multiplayer screen's cards made (sdk/save_games.h)                   */
/* ------------------------------------------------------------------ */

/* The list, read from the disk at most twice a second (the hub draws it
 * every frame): the cards' saves first, newest first, then the co-op
 * games the host kept. */
static EidosSuiGame *g_gl;     /* grown to hold them all */
static int g_gl_cap;
static int g_gl_n = -1;   /* (declared above) */
static unsigned g_gl_tick;

static void games_read(void)
{
    static char (*kept)[BW_MP_GAME_NAME_MAX];
    int nk, ng, i;

    if (g_gl_n >= 0 && g_mp_tick - g_gl_tick < 30u) {
        return;
    }
    g_gl_tick = g_mp_tick;
    g_gl_n = 0;
    ng = bw_games_count();
    nk = read_mp_games(&kept);
    if (ng + nk > g_gl_cap) {
        EidosSuiGame *grown = (EidosSuiGame *)realloc(g_gl, (size_t)(ng + nk) * sizeof *grown);

        if (grown == NULL) {
            return;
        }
        g_gl = grown;
        g_gl_cap = ng + nk;
    }
    for (i = 0; i < ng; i++) {
        EidosSuiGame *out = &g_gl[g_gl_n];
        BwGame g;
        u32 mod, mode;
        int found = 0;

        if (bw_games_at(i, &g) != 0) {
            break;
        }
        memset(out, 0, sizeof *out);
        out->kind = g.kind == BW_GAME_WORLD ? EIDOS_SUI_GAME_WORLD : g.kind == BW_GAME_ROOM ? EIDOS_SUI_GAME_ROOM
                    : EIDOS_SUI_GAME_SOLO;
        out->host = g.role == BW_GAME_HOST;
        out->slot = g.slot;
        put(out->code, (int)sizeof out->code, g.code);
        if (bw_mod_menu_find_mode(g.mode, &mod, &mode)) {
            put(out->title, (int)sizeof out->title, bw_mod_menu_mode_title(mod, mode));
            put(out->owner, (int)sizeof out->owner, bw_mod_menu_name_at(mod));
            found = bw_mod_menu_enabled_at(mod) && bw_mod_menu_present_at(mod) && !bw_mod_menu_unavailable_at(mod);
        } else {
            put(out->title, (int)sizeof out->title, g.mode);
            put(out->owner, (int)sizeof out->owner, g.ns);
        }
        /* a save of a mod that is off cannot go on: said, not hidden */
        out->ok = found;
        g_gl_n++;
    }
    for (i = 0; i < nk; i++) {
        EidosSuiGame *out = &g_gl[g_gl_n++];

        memset(out, 0, sizeof *out);
        out->kind = EIDOS_SUI_GAME_COOP;
        out->host = 1;
        out->ok = 1;
        out->kept = i;
        put(out->title, (int)sizeof out->title, kept[i]);
    }
}

int eidos_sui_code_from_text(const char *text, char *out, int n)
{
    static const char link[] = "eidos://join/";
    static const char letters[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    char code[8];
    int len = 0, digits = 0, i;

    if (out == NULL || n <= 0) {
        return 0;
    }
    out[0] = '\0';
    if (text == NULL) {
        return 0;
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    for (i = 0; link[i] != '\0'; i++) {
        char ch = text[i];

        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch != link[i]) {
            break;
        }
    }
    if (link[i] == '\0') {
        text += i;
    }
    for (; *text != '\0' && *text != ' ' && *text != '\t' && *text != '\r' && *text != '\n'; text++) {
        char ch = *text;

        if (len >= 6) {
            return 0;                   /* longer than any code */
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        digits += ch >= '0' && ch <= '9';
        code[len++] = ch;
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    if (*text != '\0') {
        return 0;                       /* more words after it */
    }
    code[len] = '\0';
    if (len == 6) {
        for (i = 0; i < 6; i++) {
            if (strchr(letters, code[i]) == NULL) {
                return 0;
            }
        }
    } else if (len < 2 || len > 5 || digits != len) {
        return 0;                       /* not a room's port either */
    }
    if (len + 1 > n) {
        return 0;
    }
    memcpy(out, code, (size_t)len + 1);
    return 1;
}

int eidos_sui_mp_games(void)
{
    games_read();
    return g_gl_n;
}

int eidos_sui_mp_game(int i, EidosSuiGame *out)
{
    games_read();
    if (out == NULL || i < 0 || i >= g_gl_n) {
        return -1;
    }
    *out = g_gl[i];
    return 0;
}

/* A row of the whole list asked for by a view that lays the rows out itself
 * (eidos_sui_mp_row_act), done at the screen's next step on that row: the
 * list's own scrolling and its grey bar never come into it. */
static int g_req_abs = -1, g_req_abs_zone;

void eidos_sui_mp_row_act(int i, int zone)
{
    if (!eidos_sui_mp_up() || question_up() || g.editing || i < 0 || i >= g.nrows) {
        return;
    }
    if (getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: mp ui: row %d (type %d) zone %d\n", i, g.rows[i].type, zone);
    }
    g_req_abs = i;
    g_req_abs_zone = zone;
}

/* Run from eidos_sui_input: the row a view asked for, as A (or left and
 * right on a value) does it there. */
static int mp_take_row(void)
{
    int i = g_req_abs, zone = g_req_abs_zone;
    const Row *r;

    g_req_abs = -1;
    if (i < 0 || i >= g.nrows) {
        return 0;
    }
    g.cursor = i;
    clamp_scroll();
    r = &g.rows[i];
    if (r->type == ROW_MOD_OPTION) {
        int k2 = bw_mod_menu_option_kind((u32)r->arg, (u32)r->arg2);

        if (k2 == BW_MOD_SETTING_TEXT || k2 == BW_MOD_SETTING_SECRET) {
            return mod_text_edit(r->arg, r->arg2);
        }
        if (k2 == BW_MOD_SETTING_BUTTON) {
            return mod_button(r->arg, r->arg2);
        }
        if (k2 == BW_MOD_SETTING_SHOW) {
            return EIDOS_SUI_REDRAW;
        }
        return change_mod_option(r->arg, r->arg2, zone == EIDOS_SUI_ZONE_DEC ? -1 : 1)
               ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT : EIDOS_SUI_REDRAW;
    }
    if (r->type == ROW_MP_MODE_TYPE) {
        return mode_type_step(zone == EIDOS_SUI_ZONE_DEC ? -1 : 1) ? EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT
                                                                  : EIDOS_SUI_REDRAW;
    }
    return session_action(r->type);
}

static int mp_action_row(int action, int arg)
{
    static const int types[] = {
        ROW_NET_NAME, ROW_SESSION_HOST, ROW_SESSION_JOIN, ROW_MP_GAME,
        ROW_MP_NEW_GAME, ROW_MP_START, ROW_SESSION_LEAVE, ROW_SESSION_CODE,
        -1, ROW_MP_MODE, ROW_MP_MODE_START
    };
    int i, k = 0;

    if (action < 0 || action >= (int)(sizeof types / sizeof types[0]) || types[action] < 0) {
        return -1;
    }
    if (types[action] == ROW_MP_MODE) {
        /* the arg-th mode row */
        for (i = 0; i < g.nrows; i++) {
            if (g.rows[i].type == ROW_MP_MODE && k++ == arg) {
                return i;
            }
        }
        return -1;
    }
    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == types[action] && (types[action] != ROW_MP_GAME || g.rows[i].arg == arg)) {
            return i;
        }
    }
    return -1;
}

int eidos_sui_mp_can(int action, int arg)
{
    return eidos_sui_mp_up() && mp_action_row(action, arg) >= 0;
}

int eidos_sui_mp_act(int action, int arg)
{
    int i;

    if (!eidos_sui_mp_up() || question_up() || g.editing) {
        return 0;
    }
    if (action == EIDOS_SUI_MP_ACT_HOST_KEPT) {
        /* a game the host kept: the Host view's own row for it */
        if (g_mp_view != MP_VIEW_CHOOSE || mp_in_game() || g_sess.busy) {
            return 0;
        }
        g_req_host_game = arg;
        return 1;
    }
    if (action == EIDOS_SUI_MP_ACT_CONTINUE || action == EIDOS_SUI_MP_ACT_COOP_NEW ||
        action == EIDOS_SUI_MP_ACT_PASTE) {
        /* from the hub only, done at the screen's next step */
        if (g_mp_view != MP_VIEW_CHOOSE || mp_in_game() || g_sess.busy || g_look.busy) {
            return 0;
        }
        if (action == EIDOS_SUI_MP_ACT_COOP_NEW) {
            g_req_coop_new = 1;
        } else if (action == EIDOS_SUI_MP_ACT_PASTE) {
            g_req_paste = 1;
        } else {
            g_req_continue = arg;
        }
        return 1;
    }
    if (action == EIDOS_SUI_MP_ACT_COPY) {
        /* as A on the code's row: the code onto the clipboard */
        if (mp_action_row(action, arg) < 0) {
            return 0;
        }
        g_copied = eidos_settings_clipboard_set(eidos_settings_session_code()) == 0;
        return g_copied;
    }
    i = mp_action_row(action, arg);
    if (getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: mp ui: act %d arg %d -> row %d of %d (scroll %d, view %d)\n", action, arg, i,
                g.nrows, g.scroll, g_mp_view);
    }
    if (i < 0) {
        return 0;
    }
    if (i < g.scroll) {
        g.scroll = i;
    }
    eidos_sui_request_touch(i - g.scroll + 1, 0);
    return 1;
}

/* Run from eidos_sui_input: a kept game asked for from the hub is hosted
 * as the Host view's row for it hosts it. */
static int mp_take_host_game(void)
{
    int want = g_req_host_game, i;

    g_req_host_game = -1;
    if (want < 0 || session_action(ROW_SESSION_HOST) == 0) {
        return 0;
    }
    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == ROW_MP_GAME && g.rows[i].arg == want) {
            g.cursor = i;
            return session_action(ROW_MP_GAME);
        }
    }
    return EIDOS_SUI_REDRAW;
}

int eidos_sui_mp_status(char *out, int n)
{
    int i;
    char value[8];

    if (n > 0) {
        out[0] = '\0';
    }
    if (!eidos_sui_mp_up()) {
        return 0;
    }
    if (g_mp_view == MP_VIEW_CHOOSE && !g_look.busy && g_look.said[0] != '\0' && !mp_in_game()) {
        put(out, n, ui_tr(g_look.said));    /* a typed code names nothing */
        return 1;
    }
    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == ROW_MP_STATUS) {
            if (i < g.scroll) {
                return 0;
            }
            eidos_sui_row_text(i - g.scroll + 1, out, n, value, (int)sizeof value);
            return out[0] != '\0';
        }
    }
    return 0;
}
