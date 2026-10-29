/*
 * engine/platform/settings.h -- the port's settings: a model, a file, and
 * the functions that apply a value to the running platform at once.
 *
 * One model, owned here, for the whole process.  A settings screen reads it
 * through the option table and the binding getters, changes it with the
 * setters (each applies immediately to the platform attached with
 * eidos_settings_attach), and ends with commit (save the file, remember this
 * state) or revert (go back to the last committed state).
 *
 * Start-up order:
 *
 *     eidos_settings_load();                       settings.ini, or defaults
 *     eidos_settings_parse_arg(...) per argument   command-line overrides
 *     eidos_settings_fill_config(&cfg);            window size, mode, layout
 *     eidos_platform_init(&plat, &cfg);
 *     eidos_settings_attach(plat);                 applies everything
 *     ...
 *     eidos_settings_detach(); eidos_platform_quit(plat);
 *
 * Options.  Every option is one row of a table (eidos_settings_option) with a
 * group, a name, a kind, a range and a default, so a screen can be built by
 * walking the table and a new option is one more row.  Values are ints: a
 * CHOICE is an index into its names, an INT is a number in [min, max].
 *
 *     Display   mode (windowed / fullscreen / borderless), pixel scale
 *               (auto: the picture fills the window with sharp pixels; or
 *               exactly 1x..10x), 3D resolution (auto: as many 3D pixels as
 *               the window shows; or 1x..6x, the 3D frame being 256N x 192N
 *               per screen).  Hidden rows width/height remember the window
 *               size.
 *
 * The picture always fills the window (or the screen), aspect preserved; the
 * window is freely resizable and its size is remembered (settings.ini is
 * written at exit when only the size changed).  The renderer asks
 * eidos_settings_internal_scale() each frame for N, produces 256N x 192N
 * frames and gives N to eidos_platform_present_n; "auto" is the smallest N
 * whose frame covers the destination rectangle, re-evaluated on resize.
 *     Layout    arrangement (vertical / horizontal / one), swap_screens, gap
 *     Sound     volume (0..100 percent, 50 by default: the game is loud)
 *     Extras    instant_text (off / on): every page of a message shows at
 *               once, whatever the game's text speed; the wait for a
 *               button at the end of a page is the game's, unchanged
 *
 * Key bindings.  Every DS button has EIDOS_BIND_SLOTS keyboard slots and as
 * many gamepad slots.  A slot is empty (0 / kind -1) or one control.
 * Conflict rule: binding a control that another slot already holds MOVES it:
 * the old slot is cleared.  A control is never bound twice, so one key never
 * presses two buttons.  Escape and F12 belong to the platform layer and
 * cannot be bound; Alt+Enter toggles borderless fullscreen.
 *
 * The file is <home>/settings.ini, beside config.txt (the EIDOS_SETTINGS
 * environment variable names another file).  It is plain text, [section] and
 * key = value, # or ; at the start of a line for comments.  Unknown keys and
 * bad values are reported on stderr and ignored.  Keys are written by name
 * ("Z", "Right Shift"); a list is separated with '|'.  Command-line
 * overrides apply to the run but are never written back unless the player
 * changes that option in the screen.
 */

#ifndef EIDOS_ENGINE_SETTINGS_H
#define EIDOS_ENGINE_SETTINGS_H

#include <stddef.h>

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

enum {
    EIDOS_OPT_DISPLAY_MODE = 0,
    EIDOS_OPT_DISPLAY_PIXEL_SCALE,  /* 0 = auto, else 1..EIDOS_PIXEL_SCALE_MAX */
    EIDOS_OPT_DISPLAY_RESOLUTION,   /* the 3D: 0 = auto, else
                                       1..EIDOS_RESOLUTION_MAX              */
    EIDOS_OPT_DISPLAY_WIDTH,        /* hidden: remembered window size     */
    EIDOS_OPT_DISPLAY_HEIGHT,       /* hidden; 0 = 1x the layout          */
    EIDOS_OPT_DISPLAY_MAXIMIZED,    /* hidden: the window was maximized   */
    EIDOS_OPT_LAYOUT_ARRANGEMENT,
    EIDOS_OPT_LAYOUT_SWAP,
    EIDOS_OPT_LAYOUT_GAP,
    EIDOS_OPT_SOUND_VOLUME,
    EIDOS_OPT_EXTRAS_INSTANT_TEXT,  /* 0 off, 1 on: a message page shows
                                       at once (engine/platform/text_instant.c) */
    EIDOS_OPT_BATTLE_BACKDROP,      /* 0 real map, 1 original: the one-screen
                                       battle scenery (sdk/battle_view.c)  */
    EIDOS_OPT_BATTLE_PLATFORMS,     /* 0 off, 1 on: the game's two stages  */
    EIDOS_OPT_MOVE_COLOURS,         /* 0 off, 1 on: move names in battle
                                       messages take their type colour     */
    EIDOS_OPT_PAD_ICONS,            /* the gamepad's button icons: 0 the pad
                                       in use, else ui_pad family + 1    */
    EIDOS_OPT_OUTLINES,             /* the 3D's outlines: 0 off (the
                                       default), 1 thin (one window pixel) */
    EIDOS_OPT_COUNT
};

#define EIDOS_OPTKIND_CHOICE 0   /* value indexes `choices`; all of them shown */
#define EIDOS_OPTKIND_INT    1   /* value in [min, max], adjusted by `step`    */

typedef struct EidosOption {
    const char *group;           /* "Display", "Layout", "Sound" (ASCII)     */
    const char *key;             /* ini key and stable id: "scale"           */
    const char *label;           /* for the screen: "Window scale"           */
    int kind;
    int min, max, step, def;
    const char *const *choices;  /* CHOICE: max + 1 lowercase names          */
    const char *unit;            /* INT: "x", "%", "px"; "" for none         */
    int hidden;                  /* non-zero: kept in the file, not shown    */
    const char *zero_name;       /* INT: text for the value 0 ("fit"), or NULL */
} EidosOption;

int eidos_settings_option_count(void);
/* NULL when out of range. */
const EidosOption *eidos_settings_option(int id);
int eidos_settings_get(int id);
/* Clamp to the range, apply to the platform now.  0 on success, -1 when the
 * host refused the change (the value is left as it was). */
int eidos_settings_set(int id, int value);
/* The value as text for the screen: a choice's name or "3x", "80%". */
const char *eidos_settings_value_text(int id, char *buf, size_t n);

/* Undo every uncommitted change (options and bindings), applying it. */
void eidos_settings_revert(void);
/* All options and bindings to their defaults, applied, not yet committed. */
void eidos_settings_defaults(void);
/* Non-zero when the model differs from the last committed state. */
int  eidos_settings_dirty(void);
/* Save the file atomically and make this state the one revert returns to.
 * 0 on success; -1 when it could not be written (the model is kept). */
int  eidos_settings_commit(void);

/* ------------------------------------------------------------------ */
/* The network settings                                                */
/* ------------------------------------------------------------------ */

/*
 * The relay the peer sessions reach, the port it listens on, the port the
 * external control listens on (0 is off), and the name the player is
 * known by.  They are the port's own: the network service reads them from
 * the port's calls (sdk/mod_net.h), and the port hands them over at its
 * start-up.
 */
const char *eidos_settings_net_relay(void);
void        eidos_settings_net_set_relay(const char *host);
int         eidos_settings_net_relay_port(void);
void        eidos_settings_net_set_relay_port(int port);
int         eidos_settings_net_control_port(void);
void        eidos_settings_net_set_control_port(int port);
const char *eidos_settings_net_name(void);
void        eidos_settings_net_set_name(const char *name);
/* The relay sessions use: the player's own, else the build's public relay
 * (EIDOS_DEFAULT_RELAY); "" when there is neither. */
const char *eidos_settings_net_relay_in_use(void);
/* Text onto the system clipboard (the lobby's code).  0, or -1. */
int         eidos_settings_clipboard_set(const char *text);

/* The two text settings are edited a character at a time: the screen calls
 * the begin, then a key for every stroke, and either keeps the value
 * (Enter) or drops it (Escape). */
void        eidos_settings_net_relay_text_begin(void);
void        eidos_settings_net_name_text_begin(void);
int         eidos_settings_net_text_editing(void);
/* A port typed as digits (control 0: the relay's, 1: the control's); the
 * digits typed so far, or NULL when that port is not being typed. */
void        eidos_settings_net_port_text_begin(int control);
const char *eidos_settings_net_port_text(int control);
int         eidos_settings_net_text_key(int scancode);
void        eidos_settings_net_text_cancel(void);

/* The network settings on their own: the defaults, the undo of an
 * uncommitted change, and whether they differ from the committed state. */
void        eidos_settings_net_defaults(void);
void        eidos_settings_net_revert(void);
int         eidos_settings_net_dirty(void);

/* The character a host key stands for, or 0 when it stands for none. */
int         eidos_settings_scancode_char(int scancode);

/* ------------------------------------------------------------------ */
/* The input injection's limit                                         */
/* ------------------------------------------------------------------ */

/*
 * The longest press the port's input injection allows, in frames.  The
 * port's own ceiling is a minute at the DS's 60 fps; the player may lower
 * it, and 0 turns the injection off.  The setting lives in the file's
 * [input] section (max_press).
 */
int         eidos_settings_input_inject_max(void);
void        eidos_settings_input_set_inject_max(int frames);
void        eidos_settings_input_defaults(void);

/* ------------------------------------------------------------------ */
/* The run-status panel                                                */
/* ------------------------------------------------------------------ */

/*
 * Whether the port shows the run-status panel.  A hidden panel takes no
 * space at all, so the window's arrangement follows the player's choice.
 * The setting lives in the file's [panel] section (visible = on|off).
 */
int         eidos_settings_panel_visible(void);
void        eidos_settings_panel_set_visible(int visible);
void        eidos_settings_panel_revert(void);

/* ------------------------------------------------------------------ */
/* The peer session                                                    */
/* ------------------------------------------------------------------ */

/*
 * The session's code: the code the relay handed out when the player
 * hosted a session, and the code the player types to join one.  It is a
 * setting of the port's own, so it is read, changed, reverted and
 * committed with the rest, and the file keeps it ([session], code).
 */
const char *eidos_settings_session_code(void);
void        eidos_settings_session_set_code(const char *code);
void        eidos_settings_session_code_text_begin(void);
/* The code the player is typing, which is not the code in force: hosting a
 * session must not overwrite what the player typed to join one.  The file
 * keeps it ([session], join_code). */
const char *eidos_settings_session_join_code(void);
void        eidos_settings_session_set_join_code(const char *code);
void        eidos_settings_session_join_code_text_begin(void);
void        eidos_settings_session_defaults(void);
void        eidos_settings_session_revert(void);
int         eidos_settings_session_dirty(void);

/* ------------------------------------------------------------------ */
/* Key bindings                                                        */
/* ------------------------------------------------------------------ */

#define EIDOS_BIND_SLOTS 4

typedef struct EidosButtonInfo {
    u32 mask;                    /* EIDOS_PAD_*                              */
    const char *key;             /* ini key: "a", "start"                    */
    const char *label;           /* "A", "Start", "D-Pad Up"                 */
} EidosButtonInfo;

int eidos_settings_button_count(void);              /* the twelve DS buttons */
const EidosButtonInfo *eidos_settings_button(int index);

/* Keyboard.  scancode 0 is an empty slot. */
int  eidos_settings_get_key(int button, int slot);
/* slot -1: the first empty slot (the last one when none is).  scancode 0
 * clears the slot.  Moves the key from any slot that holds it.  0 on
 * success, -1 bad button/slot, -2 reserved key. */
int  eidos_settings_set_key(int button, int slot, int scancode);

/* Gamepad.  kind is EIDOS_PADBIND_*, or -1 for an empty slot. */
int  eidos_settings_get_pad(int button, int slot, int *kind, int *index);
int  eidos_settings_set_pad(int button, int slot, int kind, int index);

/* Bindings (keyboard and gamepad) back to the defaults, applied. */
void eidos_settings_reset_bindings(void);

/* ASCII names for display.  Always NUL-terminated in buf. */
const char *eidos_scancode_name(int scancode, char *buf, size_t n);
const char *eidos_pad_control_name(int kind, int index, char *buf, size_t n);

/* Capture the next control pressed (forwards to the attached platform;
 * see eidos_platform_capture_*).  Typical use: begin on "press a key for A",
 * poll once a frame; on KEY call eidos_settings_set_key(button, slot, sc).
 * While armed the game receives no input and Escape cancels. */
void eidos_settings_capture_begin(void);
void eidos_settings_capture_cancel(void);
int  eidos_settings_capture_poll(EidosCaptured *out);

/* The buttons the platform's keyboard and pads hold now (0 without a
 * platform).  The game sees them a frame later. */
u32  eidos_settings_host_buttons(void);

/* ------------------------------------------------------------------ */
/* Start-up and the platform                                           */
/* ------------------------------------------------------------------ */

/* Where the file is (the path last used).  eidos_settings_set_path names a
 * different file, for tests and for a launcher; NULL restores the default. */
const char *eidos_settings_path(void);
void eidos_settings_set_path(const char *path);

/* Defaults, then the file.  0 loaded, 1 no file (defaults), -1 unreadable
 * (defaults; the file is left alone).  Resets the committed state too. */
int  eidos_settings_load(void);

/* One command-line argument.  Returns 0 when it is not a settings option,
 * 1 when consumed (*i advanced past a value), -1 for a bad value (reported):
 *   --windowed --fullscreen --borderless  --resolution N|fit   (the old
 *   --scale and --scaling are gone)
 *   --layout vertical|horizontal|one  --swap-screens  --gap N  --volume N     */
int  eidos_settings_parse_arg(int argc, char **argv, int *i);

/* The multiple N the renderer should produce this frame: the Resolution
 * option, or for "fit" the smallest N covering the destination rectangle of
 * each screen in the attached platform's window, clamped to the maximum
 * (1 with no platform).  Present the frame with eidos_platform_present_n. */
int  eidos_settings_internal_scale(void);
int  eidos_settings_touch(int *x, int *y);   /* the stylus, 0 without a window */

/* Window size, mode, layout and tables for eidos_platform_init. */
void eidos_settings_fill_config(EidosPlatformConfig *cfg);

/* Bind the model to a platform and apply all of it.  The pointer must stay
 * valid until eidos_settings_detach (eidos_platform_quit also detaches). */
void eidos_settings_attach(EidosPlatform *p);
void eidos_settings_detach(void);

/* Used by the platform layer for Alt+Enter: toggles windowed/borderless in
 * the model, commits, and returns 1; 0 when `p` is not the attached one. */
int  eidos_settings_on_toggle(EidosPlatform *p);
void eidos_settings_on_quit(EidosPlatform *p);
/* Platform to settings: the windowed size changed (remembered, not dirty). */
void eidos_settings_on_resize(EidosPlatform *p, int w, int h);
/* Platform to settings: the window was maximized (1) or restored (0). */
void eidos_settings_on_maximize(EidosPlatform *p, int maximized);
/* Platform to settings, once per pump: writes a remembered size or
 * maximized state once the window has been still for a moment, so that it
 * survives a run that does not end through the window's close. */
void eidos_settings_idle(EidosPlatform *p, u32 now_ms);

/* Atomically replace `to` with `from` (plat_replace.c). */
int  eidos_replace_file(const char *from, const char *to);

/*
 * Whether the one-screen interface runs: the interface marks every frame it
 * runs (eidos_ui_mark_active), and game-side patches that exist for that
 * layout only ask eidos_ui_active, so the two-screen layout stays the game
 * as it is.  It lives here, in a file the overlay link scans, so a patch
 * can call it.
 */
void eidos_ui_mark_active(void);
int  eidos_ui_active(void);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_SETTINGS_H */
