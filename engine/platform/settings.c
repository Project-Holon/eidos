/*
 * engine/platform/settings.c -- the settings model, settings.ini and the
 * apply functions.  See settings.h for the contract.
 *
 * The file holds the option table, the key and gamepad bindings, and two
 * sections of the port's own that are not rows of that table: [network] (the
 * relay, the two ports and the player's name) and [input] (the longest
 * press the port's input injection allows).
 *
 * It holds the port's own session too: the two codes of a session (the one
 * in force and the one the player types to join one) are settings of the
 * port's own ([session], code and join_code), the peers of the session are
 * collected here, and the calls the port's own menu makes on the peer
 * session service (sdk/mod_net.h, "The port's own session") are here,
 * beside the network settings they read.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "input.h"
#include "mod_net.h"
#include "mp.h"
#include "mod_panel_draw.h"
#include "plat_thread.h"
#include "setup.h"
#include "settings.h"

#define NBTN 12
#define PATH_MAX_LEN BW_SETUP_PATH_MAX

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */

static const char *const k_modes[] = { "windowed", "fullscreen", "borderless" };
static const char *const k_arrangements[] = { "vertical", "horizontal", "one" };
static const char *const k_offon[] = { "off", "on" };
static const char *const k_backdrops[] = { "real map", "original" };
static const char *const k_pad_icons[] = {
    "auto", "xbox360", "xboxone", "ps3", "ps4", "ps5", "switch", "gamecube", "steamdeck"
};

static const EidosOption k_options[EIDOS_OPT_COUNT] = {
    { "Display", "mode", "Display mode", EIDOS_OPTKIND_CHOICE, 0, 2, 1,
      EIDOS_DISPLAY_WINDOWED, k_modes, "", 0, NULL },
    { "Display", "pixel_scale", "Pixel scale", EIDOS_OPTKIND_INT, 0,
      EIDOS_PIXEL_SCALE_MAX, 1, 0, NULL, "x", 0, "auto" },
    /* "resolution_3d", not the old "resolution" (1x by default then): a file
     * from 0.1.x gets the new default, auto */
    { "Display", "resolution_3d", "3D resolution", EIDOS_OPTKIND_INT, 0,
      EIDOS_RESOLUTION_MAX, 1, 0, NULL, "x", 0, "auto" },
    { "Display", "width", "Window width", EIDOS_OPTKIND_INT, 0, 16384, 1, 0,
      NULL, "px", 1, NULL },
    { "Display", "height", "Window height", EIDOS_OPTKIND_INT, 0, 16384, 1, 0,
      NULL, "px", 1, NULL },
    { "Display", "maximized", "Window maximized", EIDOS_OPTKIND_CHOICE, 0, 1,
      1, 0, k_offon, "", 1, NULL },
    { "Layout", "arrangement", "Screen layout", EIDOS_OPTKIND_CHOICE, 0, 2, 1,
      EIDOS_LAYOUT_VERTICAL, k_arrangements, "", 0, NULL },
    { "Layout", "swap_screens", "Swap screens", EIDOS_OPTKIND_CHOICE, 0, 1, 1,
      0, k_offon, "", 0, NULL },
    { "Layout", "gap", "Gap between screens", EIDOS_OPTKIND_INT, 0, 128, 1, 0,
      NULL, "px", 0, NULL },
    { "Sound", "volume", "Master volume", EIDOS_OPTKIND_INT, 0, 100, 5, 50,
      NULL, "%", 0, NULL },
    { "Extras", "instant_text", "Instant text", EIDOS_OPTKIND_CHOICE, 0, 1, 1,
      0, k_offon, "", 0, NULL },
    { "Extras", "battle_backdrop", "Battle backdrop", EIDOS_OPTKIND_CHOICE, 0,
      1, 1, 0, k_backdrops, "", 0, NULL },
    { "Extras", "battle_platforms", "Battle platforms", EIDOS_OPTKIND_CHOICE,
      0, 1, 1, 0, k_offon, "", 0, NULL },
    { "Extras", "move_colours", "Move colours", EIDOS_OPTKIND_CHOICE, 0, 1, 1,
      1, k_offon, "", 0, NULL },
    { "Controls", "button_icons", "Button icons", EIDOS_OPTKIND_CHOICE, 0, 8, 1,
      0, k_pad_icons, "", 0, NULL },
};

static const EidosButtonInfo k_buttons[NBTN] = {
    { EIDOS_PAD_A,      "a",      "A" },
    { EIDOS_PAD_B,      "b",      "B" },
    { EIDOS_PAD_X,      "x",      "X" },
    { EIDOS_PAD_Y,      "y",      "Y" },
    { EIDOS_PAD_L,      "l",      "L" },
    { EIDOS_PAD_R,      "r",      "R" },
    { EIDOS_PAD_START,  "start",  "Start" },
    { EIDOS_PAD_SELECT, "select", "Select" },
    { EIDOS_PAD_UP,     "up",     "D-Pad Up" },
    { EIDOS_PAD_DOWN,   "down",   "D-Pad Down" },
    { EIDOS_PAD_LEFT,   "left",   "D-Pad Left" },
    { EIDOS_PAD_RIGHT,  "right",  "D-Pad Right" },
};

typedef struct PadCtl {
    int kind;                    /* -1 empty                                  */
    int index;
} PadCtl;

/* ------------------------------------------------------------------ */
/* The network settings                                                */
/* ------------------------------------------------------------------ */

/*
 * The settings the network service needs (sdk/mod_net.h): the relay's
 * address, the relay's port, the control's port and the player's name.
 *
 * They are the port's own settings: they live in settings.ini under
 * [network], the settings screen shows them and the player changes them
 * there, and the boot hands them to the service at the port's start-up
 * (bw_mod_net_set_relay, bw_mod_net_set_name and
 * bw_mod_net_control_set_port).
 *
 * They are not rows of the option table above because two of them are
 * text and that table holds numbers.  They are part of the same model,
 * so the same commit, revert and defaults reach them.
 */

/* The lengths of the port's own buffers. */
#define EIDOS_NET_RELAY_MAX 64
#define EIDOS_NET_NAME_MAX 32
#define EIDOS_NET_PORT_MAX 65535
/* The relay's own port (metaxy's default, plain TCP). */
#define EIDOS_NET_RELAY_PORT_DEFAULT 4141

/* The longest session code the port keeps: the relay's short code, and the
 * code the player types to join one (the service's own limit is the same:
 * BW_MOD_NET_CODE_MAX). */
#define EIDOS_SESSION_CODE_MAX 32

typedef struct NetModel {
    char relay[EIDOS_NET_RELAY_MAX];    /* the relay's address           */
    int  relay_port;                    /* the relay's port              */
    int  control_port;                  /* the control's port: 0 is off  */
    char name[EIDOS_NET_NAME_MAX];      /* the player's name             */
} NetModel;

/*
 * The two codes of a session.
 *
 * The code in force is the code of the session the player is in: the relay
 * handed it out when the player hosted a session, and the player reads it
 * to the other players.  The code being typed is the one the player types
 * to join a session: it is what the join asks the relay for, and it is
 * kept apart from the code in force, so that hosting a session does not
 * overwrite what the player has typed.
 *
 * They are settings of the port's own, so they are read, changed,
 * reverted and committed with the rest, and the file keeps them
 * ([session], code and join_code).
 */
typedef struct SessionModel {
    char code[EIDOS_SESSION_CODE_MAX];  /* the code in force            */
    char join[EIDOS_SESSION_CODE_MAX];   /* the code the player types   */
} SessionModel;

typedef struct Model {
    int v[EIDOS_OPT_COUNT];
    int key[NBTN][EIDOS_BIND_SLOTS];
    PadCtl pad[NBTN][EIDOS_BIND_SLOTS];
    NetModel net;
    int inject_max;                 /* the longest press the port allows */
    int panel_visible;               /* the run-status panel: 1 when shown */
    SessionModel session;            /* the session's code                */
} Model;

static Model g_cur, g_saved;
static int g_ovr[EIDOS_OPT_COUNT];       /* set from the command line         */
static int g_ovr_val[EIDOS_OPT_COUNT];
static EidosPlatform *g_plat;
static char g_path[PATH_MAX_LEN + 16];
static int g_have_path;
static int g_inited;
static int g_size_pending;   /* a window resize not yet in the file */
static u32 g_size_ms;        /* when it was first seen pending (idle)  */

/*
 * The text setting being edited, and the value it had before the edit.
 * The screen edits one text at a time: it begins the edit, the keys the
 * player types go in, and the edit ends with the value kept (Enter) or
 * dropped (Escape).  The value itself is the model's, so the screen shows
 * what the player has typed.
 */

/* The texts the screen edits. */
#define EIDOS_TEXT_RELAY 0        /* the relay's address                */
#define EIDOS_TEXT_NAME  1        /* the player's name                  */
#define EIDOS_TEXT_CODE  2        /* the code in force                  */
#define EIDOS_TEXT_JOIN  3        /* the code the player types         */
#define EIDOS_TEXT_RPORT 4        /* the relay's port, typed as digits */
#define EIDOS_TEXT_CPORT 5        /* the control's port, the same      */

static struct {
    int  which;                   /* EIDOS_TEXT_*, -1: none            */
    char saved[EIDOS_NET_RELAY_MAX];
    char port[8];                 /* the digits of a port being typed  */
} g_text;

static void model_defaults(Model *m);
static BwMutex *session_lock(void);

static void ensure_init(void)
{
    if (!g_inited) {
        g_inited = 1;
        model_defaults(&g_cur);
        g_saved = g_cur;
        g_text.which = -1;          /* no text is being edited */
        /* The port's own session state is kept under a lock, and the lock
         * is made here: the settings are loaded before any frame runs, so
         * it is there for the whole run. */
        session_lock();
    }
}

static int button_index(u32 mask)
{
    int i;

    for (i = 0; i < NBTN; i++) {
        if (k_buttons[i].mask == mask) {
            return i;
        }
    }
    return -1;
}

static void model_defaults(Model *m)
{
    const EidosKeyBinding *kb;
    const EidosPadBinding *pb;
    u32 nk, np, i;
    int o, b, s;

    for (o = 0; o < EIDOS_OPT_COUNT; o++) {
        m->v[o] = k_options[o].def;
    }
    for (b = 0; b < NBTN; b++) {
        for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
            m->key[b][s] = 0;
            m->pad[b][s].kind = -1;
            m->pad[b][s].index = 0;
        }
    }
    kb = eidos_default_keymap(&nk);
    for (i = 0; i < nk; i++) {
        b = button_index(kb[i].button);
        for (s = 0; b >= 0 && s < EIDOS_BIND_SLOTS; s++) {
            if (m->key[b][s] == 0) {
                m->key[b][s] = kb[i].scancode;
                break;
            }
        }
    }
    pb = eidos_default_padmap(&np);
    for (i = 0; i < np; i++) {
        b = button_index(pb[i].button);
        for (s = 0; b >= 0 && s < EIDOS_BIND_SLOTS; s++) {
            if (m->pad[b][s].kind < 0) {
                m->pad[b][s].kind = pb[i].kind;
                m->pad[b][s].index = pb[i].index;
                break;
            }
        }
    }
    /* The network settings start empty: the port has no relay of its own
     * (the session calls say "no relay"), the control is off and the
     * player has no name yet (the service's own default stands).  The
     * relay's port is metaxy's own default. */
    m->net.relay[0] = '\0';
    m->net.relay_port = EIDOS_NET_RELAY_PORT_DEFAULT;
    m->net.control_port = 0;
    m->net.name[0] = '\0';
    /* The input injection: the port allows a press of up to a minute, and
     * the player may lower it (or turn the injection off with 0). */
    m->inject_max = (int)BW_INPUT_INJECT_MAX_FRAMES;
    /* The run-status panel is shown until the player hides it. */
    m->panel_visible = 1;
    /* The session's codes: none yet.  The relay hands one out when the
     * player hosts a session, and the player types one to join one. */
    m->session.code[0] = '\0';
    m->session.join[0] = '\0';
}

static void model_reset_bindings(Model *m)
{
    Model d;

    model_defaults(&d);
    memcpy(m->key, d.key, sizeof(m->key));
    memcpy(m->pad, d.pad, sizeof(m->pad));
}

/*
 * The input injection's own setting, handed to the port's input layer: the
 * longest press the port allows, in frames.  The player may lower it (a
 * chat-controlled mod turns votes into presses, and the player decides how
 * long a key may be held for it) or turn the injection off with 0.
 *
 * The port's own limit (a minute, sdk/input.h) is the ceiling: the setting
 * lowers it, never raises it.
 */
static void apply_input(void)
{
    bw_input_set_max_press((u32)g_cur.inject_max);
}

/* ------------------------------------------------------------------ */
/* Applying to the platform                                            */
/* ------------------------------------------------------------------ */

static int g_attaching;     /* the first apply: the window was just created */

static int apply_display(void)
{
    /* The window opens at the DS size for the layout: applying the stored
     * size on top of that made it open small and jump larger. */
    return eidos_platform_set_display(
        g_plat, g_cur.v[EIDOS_OPT_DISPLAY_MODE],
        g_attaching ? 0 : g_cur.v[EIDOS_OPT_DISPLAY_WIDTH],
        g_attaching ? 0 : g_cur.v[EIDOS_OPT_DISPLAY_HEIGHT]);
}

static int apply_layout(void)
{
    return eidos_platform_set_layout(g_plat,
                                     g_cur.v[EIDOS_OPT_LAYOUT_ARRANGEMENT],
                                     g_cur.v[EIDOS_OPT_LAYOUT_SWAP],
                                     g_cur.v[EIDOS_OPT_LAYOUT_GAP]);
}

static void apply_volume(void)
{
    eidos_platform_set_volume(g_plat, g_cur.v[EIDOS_OPT_SOUND_VOLUME]);
}

static void apply_pixel_scale(void)
{
    eidos_platform_set_pixel_scale(g_plat, g_cur.v[EIDOS_OPT_DISPLAY_PIXEL_SCALE]);
}

/*
 * The run-status panel: the port's own text panel beside the two screens,
 * which the mods fill (docs/mod-services.md, "Run-status panel").  The
 * player's choice is the model's, and the apply hands it to the drawing
 * (eidos_mod_panel_set_visible) and lays the window out again: a panel the
 * player has hidden takes no space, so the arrangement changes with it.
 */
static void apply_panel(void)
{
    eidos_mod_panel_set_visible(g_cur.panel_visible);
    if (g_plat != NULL) {
        (void)apply_layout();
    }
}

static int apply_bindings(void)
{
    EidosKeyBinding *keys;
    EidosPadBinding *pads;
    u32 nk = 0, np = 0;
    int b, s, rc;

    keys = (EidosKeyBinding *)calloc(NBTN * EIDOS_BIND_SLOTS + 1,
                                     sizeof(*keys));
    pads = (EidosPadBinding *)calloc(NBTN * EIDOS_BIND_SLOTS + 1,
                                     sizeof(*pads));
    if (keys == NULL || pads == NULL) {
        free(keys);
        free(pads);
        return -1;
    }
    for (b = 0; b < NBTN; b++) {
        for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
            if (g_cur.key[b][s] > 0) {
                keys[nk].scancode = g_cur.key[b][s];
                keys[nk++].button = k_buttons[b].mask;
            }
            if (g_cur.pad[b][s].kind >= 0) {
                pads[np].kind = g_cur.pad[b][s].kind;
                pads[np].index = g_cur.pad[b][s].index;
                pads[np++].button = k_buttons[b].mask;
            }
        }
    }
    rc = eidos_platform_set_bindings(g_plat, keys, nk, pads, np);
    free(keys);
    free(pads);
    return rc;
}

static int apply_option_group(int id)
{
    if (g_plat == NULL) {
        return 0;
    }
    switch (id) {
    case EIDOS_OPT_DISPLAY_MODE:
    case EIDOS_OPT_DISPLAY_WIDTH:
    case EIDOS_OPT_DISPLAY_HEIGHT:
        return apply_display();
    case EIDOS_OPT_DISPLAY_MAXIMIZED:
        return 0;               /* read when the window opens */
    case EIDOS_OPT_DISPLAY_PIXEL_SCALE:
        apply_pixel_scale();
        return 0;
    case EIDOS_OPT_DISPLAY_RESOLUTION:
        return 0;               /* the renderer polls internal_scale() */
    case EIDOS_OPT_LAYOUT_ARRANGEMENT:
    case EIDOS_OPT_LAYOUT_SWAP:
    case EIDOS_OPT_LAYOUT_GAP:
        return apply_layout();
    case EIDOS_OPT_SOUND_VOLUME:
        apply_volume();
        return 0;
    default:
        return 0;
    }
}

static void apply_all(void)
{
    apply_panel();                /* the panel's place in the arrangement */
    if (g_plat == NULL) {
        return;
    }
    (void)apply_layout();
    if (apply_display() != 0) {
        g_cur.v[EIDOS_OPT_DISPLAY_MODE] = eidos_platform_display_mode(g_plat);
    }
    apply_volume();
    apply_pixel_scale();
    (void)apply_bindings();
}

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

int eidos_settings_option_count(void)
{
    return EIDOS_OPT_COUNT;
}

const EidosOption *eidos_settings_option(int id)
{
    return id >= 0 && id < EIDOS_OPT_COUNT ? &k_options[id] : NULL;
}

int eidos_settings_get(int id)
{
    ensure_init();
    return id >= 0 && id < EIDOS_OPT_COUNT ? g_cur.v[id] : 0;
}

static int clamp_opt(int id, int v)
{
    return v < k_options[id].min ? k_options[id].min
           : v > k_options[id].max ? k_options[id].max : v;
}

int eidos_settings_set(int id, int value)
{
    int old, rc;

    ensure_init();
    if (id < 0 || id >= EIDOS_OPT_COUNT) {
        return -1;
    }
    old = g_cur.v[id];
    g_cur.v[id] = clamp_opt(id, value);
    g_ovr[id] = 0;                       /* the player's choice now          */
    rc = apply_option_group(id);
    if (rc != 0) {
        g_cur.v[id] = old;
        return -1;
    }
    return 0;
}

const char *eidos_settings_value_text(int id, char *buf, size_t n)
{
    int v;

    if (buf == NULL || n == 0) {
        return "";
    }
    buf[0] = '\0';
    if (id < 0 || id >= EIDOS_OPT_COUNT) {
        return buf;
    }
    v = eidos_settings_get(id);
    if (k_options[id].kind == EIDOS_OPTKIND_CHOICE) {
        snprintf(buf, n, "%s", k_options[id].choices[v]);
    } else {
        if (v == 0 && k_options[id].zero_name != NULL) {
            snprintf(buf, n, "%s", k_options[id].zero_name);
        } else {
            snprintf(buf, n, "%d%s", v, k_options[id].unit);
        }
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* The network settings                                                */
/* ------------------------------------------------------------------ */

/* The buffer a text setting keeps, and its size. */
static char *net_text_buf(int which, size_t *size)
{
    if (which == EIDOS_TEXT_RELAY) {
        *size = sizeof g_cur.net.relay;
        return g_cur.net.relay;
    }
    if (which == EIDOS_TEXT_NAME) {
        *size = sizeof g_cur.net.name;
        return g_cur.net.name;
    }
    if (which == EIDOS_TEXT_CODE) {
        *size = sizeof g_cur.session.code;
        return g_cur.session.code;
    }
    if (which == EIDOS_TEXT_JOIN) {
        *size = sizeof g_cur.session.join;
        return g_cur.session.join;
    }
    if (which == EIDOS_TEXT_RPORT || which == EIDOS_TEXT_CPORT) {
        *size = 6;                  /* five digits and the end */
        return g_text.port;
    }
    *size = 0;
    return NULL;
}

static void net_text_copy(char *dst, size_t size, const char *src)
{
    size_t n;

    if (size == 0) {
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

const char *eidos_settings_net_relay(void)
{
    ensure_init();
    return g_cur.net.relay;
}

void eidos_settings_net_set_relay(const char *host)
{
    ensure_init();
    net_text_copy(g_cur.net.relay, sizeof g_cur.net.relay,
                  host != NULL ? host : "");
}

int eidos_settings_net_relay_port(void)
{
    ensure_init();
    /* 0 is "not set" (a settings.ini written before the default): the
     * relay's own port stands */
    return g_cur.net.relay_port != 0 ? g_cur.net.relay_port
                                     : EIDOS_NET_RELAY_PORT_DEFAULT;
}

void eidos_settings_net_set_relay_port(int port)
{
    ensure_init();
    if (port < 0) {
        port = 0;
    }
    if (port > EIDOS_NET_PORT_MAX) {
        port = EIDOS_NET_PORT_MAX;
    }
    g_cur.net.relay_port = port;
}

int eidos_settings_net_control_port(void)
{
    ensure_init();
    return g_cur.net.control_port;
}

void eidos_settings_net_set_control_port(int port)
{
    ensure_init();
    if (port < 0) {
        port = 0;
    }
    if (port > EIDOS_NET_PORT_MAX) {
        port = EIDOS_NET_PORT_MAX;
    }
    g_cur.net.control_port = port;
}

/*
 * The input injection's limit: the longest press the port allows, in frames.
 * The player may lower the port's own ceiling, and 0 turns the injection
 * off.  The value is applied to the input layer with the rest of the
 * settings (apply_input).
 */
int eidos_settings_input_inject_max(void)
{
    ensure_init();
    return g_cur.inject_max;
}

void eidos_settings_input_set_inject_max(int frames)
{
    ensure_init();
    if (frames < 0) {
        frames = 0;
    }
    if (frames > (int)BW_INPUT_INJECT_MAX_FRAMES) {
        frames = (int)BW_INPUT_INJECT_MAX_FRAMES;
    }
    if (frames == g_cur.inject_max) {
        return;
    }
    g_cur.inject_max = frames;
    apply_input();               /* the injection follows at once */
}

/*
 * The limit back to the port's own ceiling (a minute), the way the other
 * settings go back to their defaults.  The value is applied at once, so the
 * injection is on again as soon as the row says so.
 */
void eidos_settings_input_defaults(void)
{
    ensure_init();
    eidos_settings_input_set_inject_max((int)BW_INPUT_INJECT_MAX_FRAMES);
}

/*
 * The run-status panel: the port's own text panel beside the two screens,
 * which the mods fill (docs/mod-services.md, "Run-status panel").  The
 * player hides it from the port's settings, and a hidden panel takes no
 * space: the window's arrangement changes with it.
 *
 * The setting is the model's, so it is read, changed, reverted and committed
 * with the rest of the settings, and the file keeps it ([panel], visible).
 * The drawing asks eidos_mod_panel_visible() before it draws anything.
 */
int eidos_settings_panel_visible(void)
{
    ensure_init();
    return g_cur.panel_visible;
}

void eidos_settings_panel_set_visible(int visible)
{
    ensure_init();
    visible = (visible != 0) ? 1 : 0;
    if (visible == g_cur.panel_visible) {
        return;
    }
    g_cur.panel_visible = visible;
    apply_panel();
}

/* The panel back to the state the last commit left (the screen's "Revert
 * changes" row). */
void eidos_settings_panel_revert(void)
{
    ensure_init();
    if (g_cur.panel_visible == g_saved.panel_visible) {
        return;
    }
    g_cur.panel_visible = g_saved.panel_visible;
    apply_panel();
}

const char *eidos_settings_net_name(void)
{
    ensure_init();
    return g_cur.net.name;
}

void eidos_settings_net_set_name(const char *name)
{
    ensure_init();
    net_text_copy(g_cur.net.name, sizeof g_cur.net.name,
                  name != NULL ? name : "");
}

/*
 * The character a key of the host keyboard stands for: the ASCII the
 * player types.  0 when the key is not a character (a function key, a
 * modifier).  The port has no keyboard of its own on the DS screens, so
 * the player types on the host.
 */
int eidos_settings_scancode_char(int scancode)
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return 'a' + (scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return '1' + (scancode - SDL_SCANCODE_1);
    }
    if (scancode >= SDL_SCANCODE_KP_0 && scancode <= SDL_SCANCODE_KP_9) {
        return '0' + (scancode - SDL_SCANCODE_KP_0);
    }
    switch (scancode) {
    case SDL_SCANCODE_0:           return '0';
    case SDL_SCANCODE_SPACE:       return ' ';
    case SDL_SCANCODE_MINUS:       return '-';
    case SDL_SCANCODE_EQUALS:      return '=';
    case SDL_SCANCODE_LEFTBRACKET: return '[';
    case SDL_SCANCODE_RIGHTBRACKET:return ']';
    case SDL_SCANCODE_BACKSLASH:   return '\\';
    case SDL_SCANCODE_SEMICOLON:   return ';';
    case SDL_SCANCODE_APOSTROPHE:  return '\'';
    case SDL_SCANCODE_GRAVE:       return '`';
    case SDL_SCANCODE_COMMA:       return ',';
    case SDL_SCANCODE_PERIOD:      return '.';
    case SDL_SCANCODE_SLASH:       return '/';
    case SDL_SCANCODE_KP_PERIOD:   return '.';
    case SDL_SCANCODE_KP_DIVIDE:   return '/';
    case SDL_SCANCODE_KP_MINUS:    return '-';
    case SDL_SCANCODE_KP_PLUS:     return '+';
    default:                       return 0;
    }
}

/* The text settings: the screen edits them. */
void eidos_settings_net_relay_text_begin(void)
{
    ensure_init();
    g_text.which = EIDOS_TEXT_RELAY;
    memcpy(g_text.saved, g_cur.net.relay, sizeof g_cur.net.relay);
}

void eidos_settings_net_name_text_begin(void)
{
    ensure_init();
    g_text.which = EIDOS_TEXT_NAME;
    memcpy(g_text.saved, g_cur.net.name, sizeof g_cur.net.name);
}

/* A port is typed as digits: the edit starts empty, and Enter sets the
 * number (an empty text keeps the port as it was). */
void eidos_settings_net_port_text_begin(int control)
{
    ensure_init();
    g_text.which = control ? EIDOS_TEXT_CPORT : EIDOS_TEXT_RPORT;
    g_text.port[0] = '\0';
    g_text.saved[0] = '\0';
}

const char *eidos_settings_net_port_text(int control)
{
    int want = control ? EIDOS_TEXT_CPORT : EIDOS_TEXT_RPORT;

    return g_text.which == want ? g_text.port : NULL;
}

int eidos_settings_net_text_editing(void)
{
    return g_text.which >= 0;
}

/* The edit is given up: the value the text had before it stands. */
void eidos_settings_net_text_cancel(void)
{
    char *buf;
    size_t size;

    if (g_text.which < 0) {
        return;
    }
    buf = net_text_buf(g_text.which, &size);
    if (buf != NULL) {
        memcpy(buf, g_text.saved, size);
    }
    g_text.which = -1;
}

/*
 * One key of the host keyboard goes into the text being edited.  Returns 0
 * while the text is still being edited, 1 when the edit ended and the
 * value was kept, and 2 when it ended and the value was dropped.
 */
int eidos_settings_net_text_key(int scancode)
{
    char *buf;
    size_t size, len;
    int c;

    if (g_text.which < 0) {
        return 0;
    }
    buf = net_text_buf(g_text.which, &size);
    if (buf == NULL) {
        g_text.which = -1;
        return 2;
    }
    len = strlen(buf);
    if (scancode == SDL_SCANCODE_ESCAPE) {
        eidos_settings_net_text_cancel();
        return 2;
    }
    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER) {
        if ((g_text.which == EIDOS_TEXT_RPORT ||
             g_text.which == EIDOS_TEXT_CPORT) && g_text.port[0] != '\0') {
            int v = atoi(g_text.port);

            if (g_text.which == EIDOS_TEXT_RPORT) {
                eidos_settings_net_set_relay_port(v);
            } else {
                eidos_settings_net_set_control_port(v);
            }
        }
        g_text.which = -1;
        return 1;                   /* the value is kept */
    }
    if (scancode == SDL_SCANCODE_BACKSPACE || scancode == SDL_SCANCODE_DELETE) {
        if (len > 0) {
            buf[len - 1] = '\0';
        }
        return 0;
    }
    c = eidos_settings_scancode_char(scancode);
    if ((g_text.which == EIDOS_TEXT_RPORT || g_text.which == EIDOS_TEXT_CPORT) &&
        (c < '0' || c > '9')) {
        return 0;                   /* a port is digits only */
    }
    if (g_text.which == EIDOS_TEXT_RELAY && c == ';') {
        c = ':';                    /* the key of ':' without Shift: name:port */
    }
    if (c != 0 && len + 1 < size) {
        buf[len] = (char)c;
        buf[len + 1] = '\0';
    }
    return 0;
}

/* The defaults, and the last committed state. */
void eidos_settings_net_defaults(void)
{
    ensure_init();
    g_cur.net.relay[0] = '\0';
    g_cur.net.relay_port = EIDOS_NET_RELAY_PORT_DEFAULT;
    g_cur.net.control_port = 0;
    g_cur.net.name[0] = '\0';
    g_text.which = -1;
}

void eidos_settings_net_revert(void)
{
    ensure_init();
    g_cur.net = g_saved.net;
    g_text.which = -1;
}

int eidos_settings_net_dirty(void)
{
    ensure_init();
    return memcmp(&g_cur.net, &g_saved.net, sizeof g_cur.net) != 0;
}

/* ------------------------------------------------------------------ */
/* The session's code                                                  */
/* ------------------------------------------------------------------ */

/*
 * The two codes of a session are settings of the port's own: the code in
 * force (the one the relay handed out when the player hosted a session, or
 * the one the player joined a session with) and the code the player types
 * to join one.  They are in the model, so the same commit, revert and
 * defaults reach them, and the file keeps them ([session], code and
 * join_code).
 */

/* The code in force: the code of the session the player is in. */
const char *eidos_settings_session_code(void)
{
    ensure_init();
    return g_cur.session.code;
}

void eidos_settings_session_set_code(const char *code)
{
    ensure_init();
    net_text_copy(g_cur.session.code, sizeof g_cur.session.code,
                  code != NULL ? code : "");
}

/* The code being typed: the one the player types to join a session. */
const char *eidos_settings_session_join_code(void)
{
    ensure_init();
    return g_cur.session.join;
}

void eidos_settings_session_set_join_code(const char *code)
{
    ensure_init();
    net_text_copy(g_cur.session.join, sizeof g_cur.session.join,
                  code != NULL ? code : "");
}

/* The screen begins the edit of the code in force. */
void eidos_settings_session_code_text_begin(void)
{
    ensure_init();
    g_text.which = EIDOS_TEXT_CODE;
    memcpy(g_text.saved, g_cur.session.code, sizeof g_cur.session.code);
}

/* The screen begins the edit of the code the player types to join a
 * session. */
void eidos_settings_session_join_code_text_begin(void)
{
    ensure_init();
    g_text.which = EIDOS_TEXT_JOIN;
    memcpy(g_text.saved, g_cur.session.join, sizeof g_cur.session.join);
}

/* The codes back to nothing (the section's "Restore defaults"). */
void eidos_settings_session_defaults(void)
{
    ensure_init();
    g_cur.session.code[0] = '\0';
    g_cur.session.join[0] = '\0';
    g_text.which = -1;
}

/* The codes back to the state the last commit left (the section's
 * "Revert changes"). */
void eidos_settings_session_revert(void)
{
    ensure_init();
    memcpy(g_cur.session.code, g_saved.session.code,
           sizeof g_cur.session.code);
    memcpy(g_cur.session.join, g_saved.session.join,
           sizeof g_cur.session.join);
    g_text.which = -1;
}

int eidos_settings_session_dirty(void)
{
    ensure_init();
    return memcmp(g_cur.session.code, g_saved.session.code,
                  sizeof g_cur.session.code) != 0 ||
           memcmp(g_cur.session.join, g_saved.session.join,
                  sizeof g_cur.session.join) != 0;
}

/* ------------------------------------------------------------------ */
/* The port's own session                                              */
/* ------------------------------------------------------------------ */

/*
 * The port's own calls on the peer session service (sdk/mod_net.h, "The
 * port's own session").  They live here, beside the network settings
 * they read, and not in the mod layer's surface: a mod reaches the
 * service through the services table, which is frozen (docs/mod-abi.md),
 * and the port does not go through that table.
 *
 * Two things the mod-facing calls do not do are done here.
 *
 *   - The service is given the relay and the name the port's own settings
 *     hold, before the call.  The boot hands them over once, at the
 *     port's start-up; the menu changes them while the game runs, and the
 *     session the player starts from the menu is the one that uses them.
 *
 *   - The call does not block the frame thread.  A host or a join waits
 *     for the relay, for up to three seconds (sdk/mod_net.c), and the
 *     settings screen is drawn on the frame thread, so the call is made
 *     on the port's own worker and the screen polls it.  The worker
 *     publishes its answer under the lock; the frame thread takes it and
 *     puts the code into the model, so the model is only ever written by
 *     the thread that draws it.
 */

/* The work in flight, and the answer of the last one. */
static struct {
    BwMutex  *lock;
    BwThread *thread;
    int       busy;        /* the worker is running                    */
    int       done;        /* the worker finished, not taken yet        */
    int       taken;       /* the frame thread has taken the answer    */
    int       rc;          /* the answer of the service                */
    int       host;        /* the work was a host                      */
    char      code[BW_MOD_NET_CODE_MAX + 1];     /* the code to join with */
    char      result[BW_MOD_NET_CODE_MAX + 1];   /* the code that came back */
} g_sess;

/*
 * The lock the port's own session state is kept under.  It is made when the
 * settings are loaded, which is before any frame runs, so it is there for
 * the whole run.
 */
static BwMutex *session_lock(void)
{
    if (g_sess.lock == NULL) {
        g_sess.lock = bw_mutex_create();
    }
    return g_sess.lock;
}

/* ------------------------------------------------------------------ */
/* The peers of the session                                            */
/* ------------------------------------------------------------------ */

/*
 * The peers of the session, as the port's own handler collects them.
 *
 * The service keeps the names of the peers, but its own table cannot be
 * walked (it publishes a count and a name, not the numbers), so the port's
 * own handler keeps the numbers it was told about, and the screen's rows
 * are built from them.
 *
 * The handler is called on the game's frame (the service drains its queue
 * there, sdk/mod_net.c) and the screen reads the list on the thread that
 * draws it, which is not the same thread, so the list is kept under the
 * port's own lock.
 */

/* How many peers one session may hold (the service's own limit is the
 * same: BW_MOD_NET_PEERS_MAX). */
#define EIDOS_SESSION_PEERS_MAX 16

static struct {
    u32  number[EIDOS_SESSION_PEERS_MAX];
    char name[EIDOS_SESSION_PEERS_MAX][BW_MOD_NET_NAME_MAX + 1];
    int  count;
    u32  stamp;         /* changes when the list changes               */
    int  reconciled;    /* the list was filled in from the service     */
} g_peers;

/*
 * The saved-data layer's namespace calls (sdk/mod_savedata.c has no header
 * of its own, so the prototypes are here, the way sdk/mod_net.c has them).
 * The service keys its handlers on the namespace of the mod that is
 * calling, so the binding puts the port's own key in force.
 */
extern const char *bw_mod_savedata_namespace(void);
extern void        bw_mod_savedata_set_namespace(const char *ns);

/* A peer joined, or is here again after a rejoin: the number is the one it
 * had, so the record is reused. */
static void peer_add(u32 number, const char *name)
{
    BwMutex *lock = session_lock();
    int i;

    bw_mutex_lock(lock);
    for (i = 0; i < g_peers.count; i++) {
        if (g_peers.number[i] == number) {
            break;
        }
    }
    if (i == g_peers.count) {
        if (g_peers.count >= EIDOS_SESSION_PEERS_MAX) {
            bw_mutex_unlock(lock);
            return;                     /* the list is full */
        }
        g_peers.count++;
    } else if (strcmp(g_peers.name[i], name != NULL ? name : "") == 0) {
        /* The peer is already there, with the same name: nothing changed. */
        bw_mutex_unlock(lock);
        return;
    }
    g_peers.number[i] = number;
    snprintf(g_peers.name[i], sizeof(g_peers.name[i]), "%s",
             name != NULL ? name : "");
    g_peers.stamp++;
    bw_mutex_unlock(lock);
}

/* A peer left. */
static void peer_drop(u32 number)
{
    BwMutex *lock = session_lock();
    int i;

    bw_mutex_lock(lock);
    for (i = 0; i < g_peers.count; i++) {
        if (g_peers.number[i] == number) {
            break;
        }
    }
    if (i < g_peers.count) {
        for (; i + 1 < g_peers.count; i++) {
            g_peers.number[i] = g_peers.number[i + 1];
            memcpy(g_peers.name[i], g_peers.name[i + 1],
                   sizeof(g_peers.name[i]));
        }
        g_peers.count--;
        g_peers.stamp++;
    }
    bw_mutex_unlock(lock);
}

/* The session is over: the peers are gone with it. */
static void peers_clear(void)
{
    BwMutex *lock = session_lock();

    bw_mutex_lock(lock);
    if (g_peers.count != 0) {
        g_peers.count = 0;
        g_peers.stamp++;
    }
    g_peers.reconciled = 0;
    bw_mutex_unlock(lock);
}

/*
 * The list is filled in again from the service's own table.
 *
 * The port's own handler hears the joins and the drops from the frame it
 * is bound on, but a reset empties the service's table of handlers
 * (bw_mod_net_reset) and the handler is bound again only when the menu
 * asks about the session: a session that a mod started in between has
 * peers the port never saw.  The service's own table cannot be walked,
 * but a peer of the session is asked for by its number, and the numbers
 * a relay hands out are the ones it keeps for the session's lifetime.
 */
static void peers_reconcile(void)
{
    u32 self = bw_mod_net_self_peer();
    u32 number;

    for (number = 1; number <= BW_MOD_NET_PEERS_MAX; number++) {
        char name[BW_MOD_NET_NAME_MAX + 1];

        if (number == self) {
            continue;                   /* this player is a row of its own */
        }
        if (bw_mod_net_peer_name(number, name, (u32)sizeof(name)) != 0) {
            continue;                   /* no peer of that number */
        }
        peer_add(number, name);
    }
}

/*
 * The port's own session handler: the peers of the session.
 *
 * The service tells every handler that asked for the session's events
 * about a join and about a drop (sdk/mod_net.c), and the name of a peer
 * that joined is in the service's own table by then, so it is asked for
 * there.  A peer that left is gone from that table, so the name the port
 * keeps is the one it had.
 */
static void session_peer_event(u32 peer, int kind, const void *buf, u32 size)
{
    char name[BW_MOD_NET_NAME_MAX + 1];

    (void)buf;
    (void)size;
    if (kind == (int)BW_SESSION_JOIN) {
        if (bw_mod_net_peer_name(peer, name, (u32)sizeof(name)) != 0) {
            name[0] = '\0';
        }
        peer_add(peer, name);
    } else if (kind == (int)BW_SESSION_LEAVE) {
        peer_drop(peer);
    }
}

/*
 * The binding: the port's own session handler, under the port's own key
 * (sdk/mod_net.h, "The port's own session handler").  The service keys its
 * handlers on the namespace of the mod that is calling, so the port's own
 * key is in force while the handler is bound, and the one that was in
 * force is put back afterwards.
 */
void bw_port_session_bind(void)
{
    const char *saved = bw_mod_savedata_namespace();

    bw_mod_savedata_set_namespace(BW_PORT_SESSION_KEY);
    bw_mod_net_on_session(session_peer_event);
    bw_mod_savedata_set_namespace(saved);
}

/* The peers of the session, for the screen's rows. */
u32 bw_port_session_peer_count(void)
{
    BwMutex *lock = session_lock();
    u32 n;

    bw_mutex_lock(lock);
    n = (u32)g_peers.count;
    bw_mutex_unlock(lock);
    return n;
}

int bw_port_session_peer_at(u32 index, u32 *number, char *name, u32 name_size)
{
    BwMutex *lock = session_lock();
    int rc = -1;

    bw_mutex_lock(lock);
    if (index < (u32)g_peers.count) {
        if (number != NULL) {
            *number = g_peers.number[index];
        }
        if (name != NULL && name_size > 0) {
            snprintf(name, (size_t)name_size, "%s", g_peers.name[index]);
        }
        rc = 0;
    }
    bw_mutex_unlock(lock);
    return rc;
}

/* This player's own number in the session, and the name the relay gave it. */
u32 bw_port_session_self_peer(void)
{
    return bw_mod_net_self_peer();
}

int bw_port_session_self_name(char *out, u32 out_size)
{
    u32 self = bw_mod_net_self_peer();

    if (out == NULL || out_size == 0) {
        return -1;
    }
    if (self == 0) {
        out[0] = '\0';
        return -1;
    }
    return bw_mod_net_peer_name(self, out, out_size);
}

/* The list's stamp: it changes when the list does, so the screen knows when
 * to build its rows again. */
static u32 peers_stamp(void)
{
    BwMutex *lock = session_lock();
    u32 s;

    bw_mutex_lock(lock);
    s = g_peers.stamp;
    bw_mutex_unlock(lock);
    return s;
}

/*
 * The relay a session uses: the player's own when one is set, else the
 * public relay this build names (EIDOS_DEFAULT_RELAY, docs/distribution.md),
 * written as `tls://name:port` or `name:port`.  The port is the setting's
 * when the address names none.
 */
#ifndef EIDOS_DEFAULT_RELAY
#define EIDOS_DEFAULT_RELAY ""
#endif

const char *eidos_settings_net_relay_in_use(void)
{
    const char *own = eidos_settings_net_relay();

    return (own != NULL && own[0] != '\0') ? own : EIDOS_DEFAULT_RELAY;
}

int eidos_settings_clipboard_set(const char *text)
{
    return SDL_SetClipboardText(text != NULL ? text : "") ? 0 : -1;
}

/* The relay and the name the port's own settings hold. */
static void session_give_relay(void)
{
    const char *host = eidos_settings_net_relay_in_use();
    const char *name = eidos_settings_net_name();
    int port = eidos_settings_net_relay_port();

    if (port < 0) {
        port = 0;
    }
    bw_mod_net_set_relay(host != NULL ? host : "", (u16)port);
    bw_mod_net_set_name(name != NULL ? name : "");
}

/*
 * The worker: the call that blocks.  It takes the lock only to copy what
 * it needs and to publish what it got, so the frame thread's poll never
 * waits for the relay.
 */
static void session_worker(void *arg)
{
    char code[BW_MOD_NET_CODE_MAX + 1];
    char join[BW_MOD_NET_CODE_MAX + 1];
    int host;
    int rc;

    (void)arg;
    bw_mutex_lock(g_sess.lock);
    host = g_sess.host;
    memcpy(join, g_sess.code, sizeof join);
    bw_mutex_unlock(g_sess.lock);

    code[0] = '\0';
    if (host) {
        rc = bw_mod_net_session_host(code, (u32)sizeof code);
    } else {
        rc = bw_mod_net_session_join(join);
    }

    bw_mutex_lock(g_sess.lock);
    g_sess.rc = rc;
    if (host) {
        memcpy(g_sess.result, code, sizeof g_sess.result);
    } else {
        memcpy(g_sess.result, join, sizeof g_sess.result);
    }
    g_sess.done = 1;
    g_sess.busy = 0;
    bw_mutex_unlock(g_sess.lock);
}

/*
 * Start a session (host != 0) or join one (host == 0, with the code).
 * The call does not block.
 */
int bw_port_session_start(int host, const char *code)
{
    BwThread *old;

    session_lock();
    /* The port's own handler is bound before the session starts, so the
     * peers that join it are collected. */
    bw_port_session_bind();
    /* The service is given the port's own relay and name first: they are
     * the settings the player may have changed since the boot. */
    session_give_relay();

    bw_mutex_lock(g_sess.lock);
    if (g_sess.busy) {
        bw_mutex_unlock(g_sess.lock);
        return -1;
    }
    /* The last work's thread, if its answer was never taken (the player
     * left the menu before the relay answered): it is joined here, and it
     * has published its answer by now, so this returns at once. */
    old = g_sess.thread;
    g_sess.thread = NULL;
    bw_mutex_unlock(g_sess.lock);
    if (old != NULL) {
        bw_thread_join(old);
    }

    bw_mutex_lock(g_sess.lock);
    g_sess.host = host != 0 ? 1 : 0;
    memset(g_sess.code, 0, sizeof g_sess.code);
    if (code != NULL) {
        size_t n = strlen(code);

        if (n >= sizeof g_sess.code) {
            n = sizeof g_sess.code - 1;
        }
        memcpy(g_sess.code, code, n);
    }
    memset(g_sess.result, 0, sizeof g_sess.result);
    g_sess.rc = 0;
    g_sess.done = 0;
    g_sess.taken = 0;
    g_sess.busy = 1;
    g_sess.thread = bw_thread_start(session_worker, NULL);
    bw_mutex_unlock(g_sess.lock);
    return 0;
}

/*
 * Ask how the session is doing.  The answer of a finished work is given
 * once: the frame thread takes it, puts the code into the model and
 * shows it.
 *
 * The poll is also where the port's own handler is bound again: a reset
 * empties the service's table of handlers (bw_mod_net_reset), and the
 * screen asks every frame, so the handler is in place for the session
 * that follows.
 */
int bw_port_session_poll(BwPortSessionState *out)
{
    BwThread *t = NULL;
    int done = 0;
    int busy;

    if (g_sess.lock == NULL) {
        if (out != NULL) {
            memset(out, 0, sizeof(*out));
        }
        return 0;
    }
    bw_port_session_bind();
    bw_mutex_lock(g_sess.lock);
    busy = g_sess.busy;
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->busy = busy;
        out->rc = g_sess.rc;
        out->host = g_sess.host;
        memcpy(out->code, g_sess.result, sizeof(out->code));
        out->code[sizeof(out->code) - 1] = '\0';
    }
    if (g_sess.done && !g_sess.taken) {
        g_sess.taken = 1;
        done = 1;
        if (out != NULL) {
            out->done = 1;
        }
        t = g_sess.thread;
        g_sess.thread = NULL;
    }
    bw_mutex_unlock(g_sess.lock);
    if (t != NULL) {
        /* The work is over: the worker has published its answer and is
         * about to return, so this does not wait. */
        bw_thread_join(t);
        /* A session the port opened is a multiplayer session: the lobby
         * takes it over (sdk/mp.h). */
        if (out != NULL && out->rc == 0) {
            bw_mp_attach(out->host, out->code);
        }
    }
    if (out != NULL) {
        out->peers = bw_port_session_peer_count();
        out->stamp = peers_stamp();
    }
    if (bw_mod_net_session_active()) {
        /* A session runs: the list is filled in from the service's own
         * table once, in case the port's own handler was bound after the
         * peers had joined. */
        BwMutex *lock = session_lock();
        int reconciled;

        bw_mutex_lock(lock);
        reconciled = g_peers.reconciled;
        g_peers.reconciled = 1;
        bw_mutex_unlock(lock);
        if (!reconciled) {
            peers_reconcile();
        }
    } else if (!busy) {
        /* No session runs: the peers of the one that ended are gone. */
        peers_clear();
    }
    return done;
}

/* Leave the session.  The service's leave does not block. */
void bw_port_session_leave(void)
{
    bw_mp_leave();
    peers_clear();
}

/* Whether a session is running. */
int bw_port_session_active(void)
{
    return bw_mod_net_session_active();
}

/* ------------------------------------------------------------------ */
/* Bindings                                                            */
/* ------------------------------------------------------------------ */

int eidos_settings_button_count(void)
{
    return NBTN;
}

const EidosButtonInfo *eidos_settings_button(int index)
{
    return index >= 0 && index < NBTN ? &k_buttons[index] : NULL;
}

int eidos_settings_get_key(int button, int slot)
{
    ensure_init();
    if (button < 0 || button >= NBTN || slot < 0 || slot >= EIDOS_BIND_SLOTS) {
        return 0;
    }
    return g_cur.key[button][slot];
}

static int pick_slot(int button, int slot, int is_pad)
{
    int s;

    if (slot >= 0) {
        return slot < EIDOS_BIND_SLOTS ? slot : -1;
    }
    for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
        if (is_pad ? g_cur.pad[button][s].kind < 0
                   : g_cur.key[button][s] == 0) {
            return s;
        }
    }
    return EIDOS_BIND_SLOTS - 1;
}

int eidos_settings_set_key(int button, int slot, int scancode)
{
    int b, s;

    ensure_init();
    if (button < 0 || button >= NBTN) {
        return -1;
    }
    slot = pick_slot(button, slot, 0);
    if (slot < 0) {
        return -1;
    }
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) {
        return -1;
    }
    if (scancode == SDL_SCANCODE_ESCAPE || scancode == SDL_SCANCODE_F12) {
        return -2;
    }
    if (scancode != 0) {
        for (b = 0; b < NBTN; b++) {
            for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
                if (g_cur.key[b][s] == scancode) {
                    /* a key belongs to one button: the other one takes the
                     * key this slot had, so it is never left without */
                    g_cur.key[b][s] = (b == button && s == slot) ? scancode
                                    : (b == button) ? 0 : g_cur.key[button][slot];
                }
            }
        }
    }
    g_cur.key[button][slot] = scancode;
    if (g_plat != NULL) {
        (void)apply_bindings();
    }
    return 0;
}

int eidos_settings_get_pad(int button, int slot, int *kind, int *index)
{
    ensure_init();
    if (button < 0 || button >= NBTN || slot < 0 || slot >= EIDOS_BIND_SLOTS) {
        return -1;
    }
    if (kind != NULL) {
        *kind = g_cur.pad[button][slot].kind;
    }
    if (index != NULL) {
        *index = g_cur.pad[button][slot].index;
    }
    return 0;
}

int eidos_settings_set_pad(int button, int slot, int kind, int index)
{
    int b, s;

    ensure_init();
    if (button < 0 || button >= NBTN) {
        return -1;
    }
    slot = pick_slot(button, slot, 1);
    if (slot < 0) {
        return -1;
    }
    if (kind >= 0) {
        if (kind > EIDOS_PADBIND_AXIS_NEG || index < 0) {
            return -1;
        }
        for (b = 0; b < NBTN; b++) {
            for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
                if (g_cur.pad[b][s].kind == kind &&
                    g_cur.pad[b][s].index == index) {
                    if (b == button) {
                        g_cur.pad[b][s].kind = -1;
                        g_cur.pad[b][s].index = 0;
                    } else {
                        g_cur.pad[b][s] = g_cur.pad[button][slot];
                    }
                }
            }
        }
    } else {
        index = 0;
    }
    g_cur.pad[button][slot].kind = kind < 0 ? -1 : kind;
    g_cur.pad[button][slot].index = index;
    if (g_plat != NULL) {
        (void)apply_bindings();
    }
    return 0;
}

void eidos_settings_reset_bindings(void)
{
    ensure_init();
    model_reset_bindings(&g_cur);
    if (g_plat != NULL) {
        (void)apply_bindings();
    }
}

static int is_ascii_name(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if ((unsigned char)*s < 32u || (unsigned char)*s > 126u) {
            return 0;
        }
    }
    return 1;
}

const char *eidos_scancode_name(int scancode, char *buf, size_t n)
{
    const char *nm;

    if (buf == NULL || n == 0) {
        return "";
    }
    nm = scancode > 0 && scancode < SDL_SCANCODE_COUNT
         ? SDL_GetScancodeName((SDL_Scancode)scancode) : "";
    if (scancode == 0) {
        snprintf(buf, n, "None");
    } else if (is_ascii_name(nm)) {
        snprintf(buf, n, "%s", nm);
    } else {
        snprintf(buf, n, "Key %d", scancode);
    }
    return buf;
}

static const char *const k_pad_button_names[] = {
    "Pad South", "Pad East", "Pad West", "Pad North", "Back", "Guide",
    "Start", "Left Stick Click", "Right Stick Click", "Left Bumper",
    "Right Bumper", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"
};

static const char *const k_pad_axis_pos[] = {
    "Left Stick Right", "Left Stick Down", "Right Stick Right",
    "Right Stick Down", "Left Trigger", "Right Trigger"
};
static const char *const k_pad_axis_neg[] = {
    "Left Stick Left", "Left Stick Up", "Right Stick Left",
    "Right Stick Up", "Left Trigger -", "Right Trigger -"
};

const char *eidos_pad_control_name(int kind, int index, char *buf, size_t n)
{
    const size_t nb = sizeof(k_pad_button_names) / sizeof(k_pad_button_names[0]);

    if (buf == NULL || n == 0) {
        return "";
    }
    if (kind < 0) {
        snprintf(buf, n, "None");
    } else if (kind == EIDOS_PADBIND_BUTTON) {
        if (index >= 0 && (size_t)index < nb) {
            snprintf(buf, n, "%s", k_pad_button_names[index]);
        } else {
            snprintf(buf, n, "Pad Button %d", index);
        }
    } else if (index >= 0 && index < 6) {
        snprintf(buf, n, "%s", kind == EIDOS_PADBIND_AXIS_POS
                                   ? k_pad_axis_pos[index]
                                   : k_pad_axis_neg[index]);
    } else {
        snprintf(buf, n, "Pad Axis %d%s", index,
                 kind == EIDOS_PADBIND_AXIS_POS ? "+" : "-");
    }
    return buf;
}

void eidos_settings_capture_begin(void)
{
    eidos_platform_capture_begin(g_plat);
}

void eidos_settings_capture_cancel(void)
{
    eidos_platform_capture_cancel(g_plat);
}

int eidos_settings_capture_poll(EidosCaptured *out)
{
    return eidos_platform_capture_poll(g_plat, out);
}

u32 eidos_settings_host_buttons(void)
{
    return eidos_platform_buttons(g_plat);
}

/* ------------------------------------------------------------------ */
/* The file                                                            */
/* ------------------------------------------------------------------ */

const char *eidos_settings_path(void)
{
    if (!g_have_path) {
        char home[PATH_MAX_LEN];
        const char *env = getenv("EIDOS_SETTINGS");
        const char *saves = getenv("BW_SAVE_DIR");

        g_path[0] = '\0';
        if (env != NULL && env[0] != '\0') {
            snprintf(g_path, sizeof(g_path), "%s", env);
        } else if (saves != NULL && saves[0] != '\0') {
            /* a game run on a save folder of its own (a second player on
             * the same computer) keeps its settings there too */
            snprintf(g_path, sizeof(g_path), "%s/settings.ini", saves);
        } else if (bw_setup_home(home, sizeof(home)) == 0) {
            snprintf(g_path, sizeof(g_path), "%s/settings.ini", home);
        }
        g_have_path = 1;
    }
    return g_path;
}

void eidos_settings_set_path(const char *path)
{
    g_have_path = 0;
    g_path[0] = '\0';
    if (path != NULL) {
        snprintf(g_path, sizeof(g_path), "%s", path);
        g_have_path = 1;
    }
}

/* A key's file token: SDL's name when it is plain ASCII and reads back to the
 * same scancode, else "sc:N". */
static void key_token(int sc, char *buf, size_t n)
{
    const char *nm = sc > 0 && sc < SDL_SCANCODE_COUNT
                     ? SDL_GetScancodeName((SDL_Scancode)sc) : "";

    if (is_ascii_name(nm) && strchr(nm, '|') == NULL &&
        SDL_GetScancodeFromName(nm) == (SDL_Scancode)sc) {
        snprintf(buf, n, "%s", nm);
    } else {
        snprintf(buf, n, "sc:%d", sc);
    }
}

static void pad_token(const PadCtl *c, char *buf, size_t n)
{
    if (c->kind == EIDOS_PADBIND_BUTTON) {
        const char *nm = SDL_GetGamepadStringForButton(
            (SDL_GamepadButton)c->index);

        if (nm != NULL) {
            snprintf(buf, n, "%s", nm);
        } else {
            snprintf(buf, n, "button:%d", c->index);
        }
    } else {
        const char *nm = SDL_GetGamepadStringForAxis(
            (SDL_GamepadAxis)c->index);
        char sign = c->kind == EIDOS_PADBIND_AXIS_POS ? '+' : '-';

        if (nm != NULL) {
            snprintf(buf, n, "%s%c", nm, sign);
        } else {
            snprintf(buf, n, "axis:%d%c", c->index, sign);
        }
    }
}

static int parse_pad_token(const char *t, PadCtl *out)
{
    size_t len = strlen(t);
    int v;

    if (len == 0) {
        return -1;
    }
    if (strncmp(t, "button:", 7) == 0) {
        out->kind = EIDOS_PADBIND_BUTTON;
        out->index = atoi(t + 7);
        return out->index >= 0 ? 0 : -1;
    }
    if (t[len - 1] == '+' || t[len - 1] == '-') {
        char nm[64];
        int kind = t[len - 1] == '+' ? EIDOS_PADBIND_AXIS_POS
                                     : EIDOS_PADBIND_AXIS_NEG;

        if (strncmp(t, "axis:", 5) == 0) {
            out->kind = kind;
            out->index = atoi(t + 5);
            return out->index >= 0 ? 0 : -1;
        }
        if (len - 1 >= sizeof(nm)) {
            return -1;
        }
        memcpy(nm, t, len - 1);
        nm[len - 1] = '\0';
        v = (int)SDL_GetGamepadAxisFromString(nm);
        if (v == (int)SDL_GAMEPAD_AXIS_INVALID) {
            return -1;
        }
        out->kind = kind;
        out->index = v;
        return 0;
    }
    v = (int)SDL_GetGamepadButtonFromString(t);
    if (v == (int)SDL_GAMEPAD_BUTTON_INVALID) {
        return -1;
    }
    out->kind = EIDOS_PADBIND_BUTTON;
    out->index = v;
    return 0;
}

static char *trim(char *s)
{
    char *e;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        *--e = '\0';
    }
    return s;
}

static void lower_into(char *dst, size_t n, const char *src)
{
    size_t i;

    for (i = 0; i + 1 < n && src[i] != '\0'; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/*
 * A switch in the file: "on" and "off", and the other words a hand-edited
 * file may use for them.  1 or 0; -1 when the word is none of them.
 */
static int parse_switch(const char *text)
{
    char low[16];

    lower_into(low, sizeof(low), text);
    if (strcmp(low, "on") == 0 || strcmp(low, "true") == 0 ||
        strcmp(low, "yes") == 0 || strcmp(low, "1") == 0) {
        return 1;
    }
    if (strcmp(low, "off") == 0 || strcmp(low, "false") == 0 ||
        strcmp(low, "no") == 0 || strcmp(low, "0") == 0) {
        return 0;
    }
    return -1;
}

/* Parse an option's text into a value.  0 on success. */
static int parse_value(int id, const char *text, int *out)
{
    const EidosOption *o = &k_options[id];
    char low[64];
    char *end = NULL;
    long n;
    int i;

    lower_into(low, sizeof(low), text);
    if (o->kind == EIDOS_OPTKIND_CHOICE) {
        for (i = o->min; i <= o->max; i++) {
            if (strcmp(low, o->choices[i]) == 0) {
                *out = i;
                return 0;
            }
        }
        if (o->max == 1 && o->min == 0) {
            if (!strcmp(low, "true") || !strcmp(low, "yes") ||
                !strcmp(low, "1")) {
                *out = 1;
                return 0;
            }
            if (!strcmp(low, "false") || !strcmp(low, "no") ||
                !strcmp(low, "0")) {
                *out = 0;
                return 0;
            }
        }
        return -1;
    }
    if (o->zero_name != NULL && (strcmp(low, o->zero_name) == 0 ||
                                 strcmp(low, "fit") == 0)) {
        *out = 0;               /* "fit": the 3D's auto before 0.1.2 */
        return 0;
    }
    n = strtol(text, &end, 10);
    if (end != text && o->unit[0] == 'x' && o->unit[1] == '\0' &&
        (*end == 'x' || *end == 'X') && end[1] == '\0') {
        end++;                           /* "3x" for the multiples */
    }
    if (end == text || *end != '\0') {
        return -1;
    }
    if (n < o->min || n > o->max) {
        return -1;
    }
    *out = (int)n;
    return 0;
}

static int parse_key_token(const char *t)
{
    int sc;

    if (strncmp(t, "sc:", 3) == 0) {
        sc = atoi(t + 3);
    } else {
        sc = (int)SDL_GetScancodeFromName(t);
    }
    if (sc <= 0 || sc >= SDL_SCANCODE_COUNT || sc == SDL_SCANCODE_ESCAPE ||
        sc == SDL_SCANCODE_F12) {
        return 0;
    }
    return sc;
}

/* Fill the bindings of one button from a '|' list. */
static void parse_bind_list(Model *m, int b, int is_pad, char *list,
                            int line)
{
    int slot = 0;
    char *tok = list;

    /* Clear first: a key listed in the file replaces the default list. */
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (is_pad) {
            m->pad[b][slot].kind = -1;
            m->pad[b][slot].index = 0;
        } else {
            m->key[b][slot] = 0;
        }
    }
    slot = 0;
    while (tok != NULL && *trim(tok) != '\0') {
        char *bar = strchr(tok, '|');
        char *item;

        if (bar != NULL) {
            *bar = '\0';
        }
        item = trim(tok);
        if (*item != '\0') {
            if (slot >= EIDOS_BIND_SLOTS) {
                fprintf(stderr, "eidos: settings.ini:%d: more than %d "
                        "bindings for '%s'; extra ignored\n", line,
                        EIDOS_BIND_SLOTS, k_buttons[b].key);
                return;
            }
            if (is_pad) {
                PadCtl c;
                if (parse_pad_token(item, &c) == 0) {
                    m->pad[b][slot++] = c;
                } else {
                    fprintf(stderr, "eidos: settings.ini:%d: unknown pad "
                            "control '%s' ignored\n", line, item);
                }
            } else {
                int sc = parse_key_token(item);
                if (sc != 0) {
                    m->key[b][slot++] = sc;
                } else {
                    fprintf(stderr, "eidos: settings.ini:%d: unknown or "
                            "reserved key '%s' ignored\n", line, item);
                }
            }
        }
        tok = bar != NULL ? bar + 1 : NULL;
    }
}

/* Remove duplicated controls, first listing wins (a hand-edited file). */
static void model_dedupe(Model *m)
{
    int b, s, b2, s2;

    for (b = 0; b < NBTN; b++) {
        for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
            for (b2 = b; b2 < NBTN; b2++) {
                for (s2 = (b2 == b ? s + 1 : 0); s2 < EIDOS_BIND_SLOTS; s2++) {
                    if (m->key[b][s] != 0 && m->key[b][s] == m->key[b2][s2]) {
                        m->key[b2][s2] = 0;
                    }
                    if (m->pad[b][s].kind >= 0 &&
                        m->pad[b][s].kind == m->pad[b2][s2].kind &&
                        m->pad[b][s].index == m->pad[b2][s2].index) {
                        m->pad[b2][s2].kind = -1;
                        m->pad[b2][s2].index = 0;
                    }
                }
            }
        }
    }
}

static void parse_file(FILE *fp, Model *m)
{
    char line[2048];
    char section[32] = "";
    int lineno = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *s = line, *eq, *key, *val;
        int id, found;

        lineno++;
        if (lineno == 1 && (unsigned char)s[0] == 0xEF &&
            (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
            s += 3;
        }
        s = trim(s);
        if (*s == '\0' || *s == '#' || *s == ';') {
            continue;
        }
        if (*s == '[') {
            char *close = strchr(s, ']');

            if (close == NULL) {
                fprintf(stderr, "eidos: settings.ini:%d: bad section line "
                        "ignored\n", lineno);
                section[0] = '?';
                section[1] = '\0';
                continue;
            }
            *close = '\0';
            lower_into(section, sizeof(section), trim(s + 1));
            continue;
        }
        eq = strchr(s, '=');
        if (eq == NULL) {
            fprintf(stderr, "eidos: settings.ini:%d: no '=' in line, "
                    "ignored\n", lineno);
            continue;
        }
        *eq = '\0';
        key = trim(s);
        val = trim(eq + 1);
        lower_into(key, strlen(key) + 1, key);

        if (strcmp(section, "network") == 0) {
            /* The port's own network settings: the relay's address and the
             * player's name are text, the two ports are numbers. */
            if (strcmp(key, "relay") == 0) {
                net_text_copy(m->net.relay, sizeof m->net.relay, val);
            } else if (strcmp(key, "name") == 0) {
                net_text_copy(m->net.name, sizeof m->net.name, val);
            } else if (strcmp(key, "relay_port") == 0 ||
                       strcmp(key, "control_port") == 0) {
                long n = strtol(val, NULL, 10);

                if (n < 0 || n > EIDOS_NET_PORT_MAX) {
                    fprintf(stderr, "eidos: settings.ini:%d: bad value '%s' "
                            "for network.%s, kept the default\n", lineno, val,
                            key);
                } else if (strcmp(key, "relay_port") == 0) {
                    m->net.relay_port = (int)n;
                } else {
                    m->net.control_port = (int)n;
                }
            } else {
                fprintf(stderr, "eidos: settings.ini:%d: unknown key '%s' in "
                        "[network], ignored\n", lineno, key);
            }
            continue;
        }
        if (strcmp(section, "session") == 0) {
            /* The port's own session: the code in force, and the code the
             * player types to join a session. */
            if (strcmp(key, "code") == 0) {
                net_text_copy(m->session.code, sizeof m->session.code, val);
            } else if (strcmp(key, "join_code") == 0) {
                net_text_copy(m->session.join, sizeof m->session.join, val);
            } else {
                fprintf(stderr, "eidos: settings.ini:%d: unknown key '%s' in "
                        "[session], ignored\n", lineno, key);
            }
            continue;
        }
        if (strcmp(section, "input") == 0) {
            /* The port's own input setting: the longest press the port
             * allows, in frames.  0 is off. */
            if (strcmp(key, "max_press") == 0) {
                long n = strtol(val, NULL, 10);

                if (n < 0 || n > (long)BW_INPUT_INJECT_MAX_FRAMES) {
                    fprintf(stderr, "eidos: settings.ini:%d: bad value '%s' "
                            "for input.max_press, kept the default\n", lineno,
                            val);
                } else {
                    m->inject_max = (int)n;
                }
            } else {
                fprintf(stderr, "eidos: settings.ini:%d: unknown key '%s' in "
                        "[input], ignored\n", lineno, key);
            }
            continue;
        }
        if (strcmp(section, "panel") == 0) {
            /* The port's own panel setting: whether the run-status panel is
             * shown. */
            if (strcmp(key, "visible") == 0) {
                int v = parse_switch(val);

                if (v < 0) {
                    fprintf(stderr, "eidos: settings.ini:%d: bad value '%s' "
                            "for panel.visible, kept the default\n", lineno, val);
                } else {
                    m->panel_visible = v;
                }
            } else {
                fprintf(stderr, "eidos: settings.ini:%d: unknown key '%s' in "
                        "[panel], ignored\n", lineno, key);
            }
            continue;
        }
        if (strcmp(section, "keys") == 0 || strcmp(section, "pad") == 0) {
            int b;

            found = 0;
            for (b = 0; b < NBTN; b++) {
                if (strcmp(key, k_buttons[b].key) == 0) {
                    parse_bind_list(m, b, section[0] == 'p', val, lineno);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "eidos: settings.ini:%d: unknown button "
                        "'%s' in [%s], ignored\n", lineno, key, section);
            }
            continue;
        }
        found = 0;
        for (id = 0; id < EIDOS_OPT_COUNT; id++) {
            char grp[32];

            lower_into(grp, sizeof(grp), k_options[id].group);
            if (strcmp(grp, section) == 0 && strcmp(k_options[id].key, key) == 0) {
                int v;

                found = 1;
                if (parse_value(id, val, &v) == 0) {
                    m->v[id] = v;
                } else {
                    fprintf(stderr, "eidos: settings.ini:%d: bad value "
                            "'%s' for %s.%s, kept the default\n", lineno, val,
                            grp, key);
                }
                break;
            }
        }
        /* the 3D resolution's key before 0.1.2: dropped without a word */
        if (!found && !(strcmp(section, "display") == 0 &&
                        strcmp(key, "resolution") == 0)) {
            fprintf(stderr, "eidos: settings.ini:%d: unknown key '%s' in "
                    "[%s], ignored\n", lineno, key, section);
        }
    }
    model_dedupe(m);
}

static int write_file(FILE *fp, const Model *m)
{
    char tok[128];
    int id, b, s, kind, first;
    char prev[32] = "";

    fprintf(fp, "# eidos settings.  Edit by hand or from the settings screen.\n"
                "# Lists are separated with '|'.  Unknown keys are ignored.\n");
    for (id = 0; id < EIDOS_OPT_COUNT; id++) {
        const EidosOption *o = &k_options[id];
        char grp[32];

        lower_into(grp, sizeof(grp), o->group);
        if (strcmp(grp, prev) != 0) {
            fprintf(fp, "\n[%s]\n", grp);
            snprintf(prev, sizeof(prev), "%s", grp);
        }
        if (o->kind == EIDOS_OPTKIND_CHOICE) {
            fprintf(fp, "%s = %s\n", o->key, o->choices[m->v[id]]);
        } else {
            fprintf(fp, "%s = %d\n", o->key, m->v[id]);
        }
    }
    for (kind = 0; kind < 2; kind++) {
        fprintf(fp, "\n[%s]\n", kind == 0 ? "keys" : "pad");
        for (b = 0; b < NBTN; b++) {
            fprintf(fp, "%s = ", k_buttons[b].key);
            first = 1;
            for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
                if (kind == 0 ? m->key[b][s] > 0 : m->pad[b][s].kind >= 0) {
                    if (kind == 0) {
                        key_token(m->key[b][s], tok, sizeof(tok));
                    } else {
                        pad_token(&m->pad[b][s], tok, sizeof(tok));
                    }
                    fprintf(fp, "%s%s", first ? "" : " | ", tok);
                    first = 0;
                }
            }
            fprintf(fp, "\n");
        }
    }
    /* The port's own network settings: the relay, the two ports and the
     * player's name. */
    fprintf(fp, "\n[network]\n");
    fprintf(fp, "relay = %s\n", m->net.relay);
    fprintf(fp, "relay_port = %d\n", m->net.relay_port);
    fprintf(fp, "control_port = %d\n", m->net.control_port);
    fprintf(fp, "name = %s\n", m->net.name);
    /* The port's own session: the code in force, and the code the player
     * types to join a session. */
    fprintf(fp, "\n[session]\n");
    fprintf(fp, "code = %s\n", m->session.code);
    fprintf(fp, "join_code = %s\n", m->session.join);
    /* The input injection: the longest press the port allows, in frames.
     * 0 is off. */
    fprintf(fp, "\n[input]\n");
    fprintf(fp, "max_press = %d\n", m->inject_max);
    /* The run-status panel: the player hides it from the port's settings. */
    fprintf(fp, "\n[panel]\n");
    fprintf(fp, "visible = %s\n", m->panel_visible ? "on" : "off");
    return ferror(fp) ? -1 : 0;
}

int eidos_settings_load(void)
{
    const char *path = eidos_settings_path();
    Model m;
    FILE *fp;
    int i;

    ensure_init();
    g_inited = 1;
    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        g_ovr[i] = 0;
    }
    model_defaults(&m);
    g_cur = g_saved = m;
    apply_input();
    apply_panel();
    /* The port's own session handler is bound here, at the port's
     * start-up: a session that a mod starts is collected even when the
     * menu is closed.  The menu binds it again whenever it asks about the
     * session, because a reset empties the service's table of handlers. */
    bw_port_session_bind();
    if (path[0] == '\0') {
        return 1;
    }
    fp = bw_fopen_utf8(path, "rb");
    if (fp == NULL) {
        return bw_path_kind_utf8(path) == BW_PATH_NONE ? 1 : -1;
    }
    parse_file(fp, &m);
    fclose(fp);
    g_cur = g_saved = m;
    apply_input();
    apply_panel();
    if (g_plat != NULL) {
        apply_all();
    }
    return 0;
}

static int save_model(const Model *out)
{
    const char *path = eidos_settings_path();
    char tmp[PATH_MAX_LEN + 8];
    char dir[PATH_MAX_LEN];
    FILE *fp;
    char *sep;

    if (path[0] == '\0') {
        fprintf(stderr, "eidos: no settings location; changes last this run\n");
        return -1;
    }
    if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir)) {
        fprintf(stderr, "eidos: settings path too long; changes last this run\n");
        return -1;
    }
    sep = strrchr(dir, '/');
    if (sep == NULL) {
        sep = strrchr(dir, '\\');
    }
    if (sep != NULL) {
        *sep = '\0';
        (void)bw_mkdirs_utf8(dir);
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        fprintf(stderr, "eidos: settings path too long; changes last this run\n");
        return -1;
    }
    fp = bw_fopen_utf8(tmp, "wb");
    if (fp == NULL) {
        fprintf(stderr, "eidos: cannot write %s\n", tmp);
        return -1;
    }
    if (write_file(fp, out) != 0 || fflush(fp) != 0) {
        fclose(fp);
        remove(tmp);
        fprintf(stderr, "eidos: write to %s failed\n", tmp);
        return -1;
    }
    fclose(fp);
    if (eidos_replace_file(tmp, path) != 0) {
        remove(tmp);
        fprintf(stderr, "eidos: cannot replace %s\n", path);
        return -1;
    }
    return 0;
}

int eidos_settings_commit(void)
{
    Model out;
    int i;

    ensure_init();
    out = g_cur;
    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        if (g_ovr[i]) {
            out.v[i] = g_saved.v[i];     /* a command-line value is not saved */
        }
    }
    if (save_model(&out) != 0) {
        return -1;
    }
    g_size_pending = 0;
    g_saved = out;
    return 0;
}

int eidos_settings_dirty(void)
{
    Model c;
    int i;

    ensure_init();
    c = g_cur;
    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        if (g_ovr[i]) {
            c.v[i] = g_saved.v[i];
        }
    }
    /* The whole model, the network settings among it: the struct has no
     * padding, so the comparison is exact. */
    return memcmp(&c, &g_saved, sizeof(c)) != 0;
}

void eidos_settings_revert(void)
{
    int i;

    ensure_init();
    g_cur = g_saved;
    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        if (g_ovr[i]) {
            g_cur.v[i] = g_ovr_val[i];
        }
    }
    apply_input();
    apply_all();
}

void eidos_settings_defaults(void)
{
    int i;

    ensure_init();
    model_defaults(&g_cur);
    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        g_ovr[i] = 0;
    }
    apply_input();
    apply_all();
}

/* ------------------------------------------------------------------ */
/* Command line, platform                                              */
/* ------------------------------------------------------------------ */

static void override_opt(int id, int v)
{
    ensure_init();
    g_cur.v[id] = clamp_opt(id, v);
    g_ovr[id] = 1;
    g_ovr_val[id] = g_cur.v[id];
    (void)apply_option_group(id);
}

static int bad_arg(const char *name, const char *val)
{
    fprintf(stderr, "eidos: bad value '%s' for %s\n", val, name);
    return -1;
}

int eidos_settings_parse_arg(int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    const char *val = NULL;
    int v, id = -1, takes = 0;

    if (strcmp(a, "--fullscreen") == 0) {
        override_opt(EIDOS_OPT_DISPLAY_MODE, EIDOS_DISPLAY_FULLSCREEN);
        return 1;
    }
    if (strcmp(a, "--borderless") == 0) {
        override_opt(EIDOS_OPT_DISPLAY_MODE, EIDOS_DISPLAY_BORDERLESS);
        return 1;
    }
    if (strcmp(a, "--windowed") == 0) {
        override_opt(EIDOS_OPT_DISPLAY_MODE, EIDOS_DISPLAY_WINDOWED);
        return 1;
    }
    if (strcmp(a, "--swap-screens") == 0) {
        override_opt(EIDOS_OPT_LAYOUT_SWAP, 1);
        return 1;
    }
    if (strcmp(a, "--two-screens") == 0) {
        /* the saved two-screen arrangement, or stacked when one is saved */
        ensure_init();
        if (g_cur.v[EIDOS_OPT_LAYOUT_ARRANGEMENT] == EIDOS_LAYOUT_ONE) {
            override_opt(EIDOS_OPT_LAYOUT_ARRANGEMENT, EIDOS_LAYOUT_STACKED);
        }
        return 1;
    }
    if (strcmp(a, "--resolution") == 0) {
        id = EIDOS_OPT_DISPLAY_RESOLUTION;
    } else if (strcmp(a, "--pixel-scale") == 0) {
        id = EIDOS_OPT_DISPLAY_PIXEL_SCALE;
    } else if (strcmp(a, "--layout") == 0) {
        id = EIDOS_OPT_LAYOUT_ARRANGEMENT;
    } else if (strcmp(a, "--gap") == 0) {
        id = EIDOS_OPT_LAYOUT_GAP;
    } else if (strcmp(a, "--volume") == 0) {
        id = EIDOS_OPT_SOUND_VOLUME;
    }
    if (id < 0) {
        return 0;
    }
    takes = 1;
    if (*i + takes >= argc) {
        fprintf(stderr, "eidos: %s needs a value\n", a);
        return -1;
    }
    val = argv[*i + 1];
    if (parse_value(id, val, &v) != 0) {
        return bad_arg(a, val);
    }
    *i += takes;
    override_opt(id, v);
    return 1;
}

void eidos_settings_fill_config(EidosPlatformConfig *cfg)
{
    ensure_init();
    cfg->win_w = g_cur.v[EIDOS_OPT_DISPLAY_WIDTH];
    cfg->win_h = g_cur.v[EIDOS_OPT_DISPLAY_HEIGHT];
    cfg->layout = g_cur.v[EIDOS_OPT_LAYOUT_ARRANGEMENT];
    cfg->gap = g_cur.v[EIDOS_OPT_LAYOUT_GAP];
    cfg->swap_screens = g_cur.v[EIDOS_OPT_LAYOUT_SWAP];
    cfg->display_mode = g_cur.v[EIDOS_OPT_DISPLAY_MODE];
    cfg->maximized = g_cur.v[EIDOS_OPT_DISPLAY_MAXIMIZED];
}

void eidos_settings_attach(EidosPlatform *p)
{
    ensure_init();
    g_plat = p;
    g_attaching = 1;
    apply_all();
    g_attaching = 0;
}

void eidos_settings_detach(void)
{
    g_plat = NULL;
}

int eidos_settings_on_toggle(EidosPlatform *p)
{
    if (p == NULL || p != g_plat) {
        return 0;
    }
    if (eidos_settings_set(EIDOS_OPT_DISPLAY_MODE,
                           g_cur.v[EIDOS_OPT_DISPLAY_MODE] !=
                                   EIDOS_DISPLAY_WINDOWED
                               ? EIDOS_DISPLAY_WINDOWED
                               : EIDOS_DISPLAY_BORDERLESS) == 0) {
        (void)eidos_settings_commit();
    }
    return 1;
}

/* The stylus on the touch screen, for the settings screen: non-zero while
 * down, with the pixel of the bottom screen. */
int eidos_settings_touch(int *x, int *y)
{
    return g_plat != NULL ? eidos_platform_touch(g_plat, x, y) : 0;
}

int eidos_settings_internal_scale(void)
{
    EidosViewport vp;
    int r;

    ensure_init();
    r = g_cur.v[EIDOS_OPT_DISPLAY_RESOLUTION];
    if (r > 0) {
        return r;
    }
    if (g_plat == NULL) {
        return 1;
    }
    eidos_platform_viewport(g_plat, &vp);
    return eidos_resolution_fit_cap(
        &vp, k_options[EIDOS_OPT_DISPLAY_RESOLUTION].max,
        eidos_platform_gpu3d(g_plat) ? EIDOS_RESOLUTION_MAX
                                     : EIDOS_RESOLUTION_FIT_CAP);
}

void eidos_settings_on_resize(EidosPlatform *p, int w, int h)
{
    if (p == NULL || p != g_plat || w <= 0 || h <= 0) {
        return;
    }
    w = clamp_opt(EIDOS_OPT_DISPLAY_WIDTH, w);
    h = clamp_opt(EIDOS_OPT_DISPLAY_HEIGHT, h);
    if (g_saved.v[EIDOS_OPT_DISPLAY_WIDTH] != w ||
        g_saved.v[EIDOS_OPT_DISPLAY_HEIGHT] != h) {
        g_size_pending = 1;
        g_size_ms = 0;              /* eidos_settings_idle stamps it */
    }
    /* Remembered in the committed state too: a resize is never "dirty". */
    g_cur.v[EIDOS_OPT_DISPLAY_WIDTH] = g_saved.v[EIDOS_OPT_DISPLAY_WIDTH] = w;
    g_cur.v[EIDOS_OPT_DISPLAY_HEIGHT] = g_saved.v[EIDOS_OPT_DISPLAY_HEIGHT] = h;
}

void eidos_settings_on_maximize(EidosPlatform *p, int maximized)
{
    if (p == NULL || p != g_plat) {
        return;
    }
    maximized = maximized != 0;
    if (g_saved.v[EIDOS_OPT_DISPLAY_MAXIMIZED] != maximized) {
        g_size_pending = 1;
        g_size_ms = 0;
    }
    g_cur.v[EIDOS_OPT_DISPLAY_MAXIMIZED] = maximized;
    g_saved.v[EIDOS_OPT_DISPLAY_MAXIMIZED] = maximized;
}

/*
 * The window's size and maximized state are written half a second after
 * the last change (a drag sends many), not only when the window closes: a
 * run that ends any other way (a crash, the console closed) kept the size
 * of the run before.
 */
void eidos_settings_idle(EidosPlatform *p, u32 now_ms)
{
    if (p == NULL || p != g_plat || !g_size_pending) {
        return;
    }
    if (g_size_ms == 0) {
        g_size_ms = now_ms != 0 ? now_ms : 1u;
        return;
    }
    if (now_ms - g_size_ms >= 500u) {
        g_size_pending = 0;
        (void)save_model(&g_saved);
    }
}

void eidos_settings_on_quit(EidosPlatform *p)
{
    if (p == g_plat) {
        if (g_size_pending) {
            g_size_pending = 0;
            (void)save_model(&g_saved);
        }
        g_plat = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The one-screen interface, running or not                            */
/* ------------------------------------------------------------------ */

static u32 g_ui_ran_at;
static int g_ui_ran;

void eidos_ui_mark_active(void)
{
    g_ui_ran = 1;
    g_ui_ran_at = bw_input_frame_number();
}

int eidos_ui_active(void)
{
    return g_ui_ran && bw_input_frame_number() - g_ui_ran_at <= 4u;
}
