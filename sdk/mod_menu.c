/*
 * sdk/mod_menu.c -- the mod menu's model: the installed mods, the switch,
 * the clear action, the orphan report and a mod's own settings.
 *
 * The model the port's own settings screen draws its Mods section from
 * (docs/mod-services.md, "The mod menu").  It answers questions and holds
 * the state behind the answers; it draws nothing.  The screen asks how
 * many mods there are, what the i-th one is called, whether it is on, what
 * its settings are and which saved-data folders belong to a mod that is no
 * longer installed, and it draws those.
 *
 * The rules the model lives by are the four constraints of docs/mod-rules.md,
 * section 2, as docs/mod-state.md, section 2, states them.  What they mean
 * for this file:
 *
 *   1. The roots are rebuildable at the menu.  The model never installs the
 *      mod list once and leaves it there: when the player leaves the menu
 *      the list is built again from the switches (bw_clear_mods and
 *      bw_add_mod_root, or bw_mod_set_order when the player has an order
 *      of their own), and every change drops the cached merged archives
 *      (bw_vfs_flush, inside the file layer).  The state namespace is per
 *      mod, so a rebuild of the roots moves no state: the namespace is the
 *      manifest's `state` key, and the folder it names is the mod's own.
 *
 *   2. A code mod is disabled in order.  The model drives that order: the
 *      mods' state is flushed first (the saved-data layer's save handoff),
 *      then the mods that are going off are stopped, and the loader reverts
 *      every (region, address) pair the mod installed before it unloads the
 *      library (sdk/mod_loader.c).  The model does not touch the slot
 *      registry itself: the registry holds a pointer into the library, so
 *      only the loader may revert and unload.
 *
 *   3. A disabled mod's effects stay.  The model deletes nothing when a
 *      mod is turned off: the state file stays on the disk and the game
 *      bytes the mod wrote stay until the game writes over them.  The port
 *      restores nothing, and turning the mod on again reads its state
 *      again, because the namespace and the file it names are the same.
 *
 *   4. The port starts with no mods.  The model reads the port's list and
 *      never writes it at start-up, except to restore the list the player
 *      chose last time (bw_mod_menu_start): with no record of the player's
 *      own, the port's list stands as it is, and with no mod root there is
 *      no state read and the state folder is untouched.
 *
 * The port's own record
 * ---------------------
 *
 * The player's list is one file of the port's own, <save dir>/mods.ini:
 * the mods the player knows about, the state of their switches and the
 * order the player chose for them.  It is written the way every other port
 * file is written (a whole file, atomically: sdk/save.h), and it is the
 * only thing of the menu's own that outlives the port, beside the mods'
 * own data.
 *
 * The player's order
 * ------------------
 *
 * The order the mods load in is the order their overrides and their rules
 * take effect, so it is the player's to choose (docs/mod-services.md, "The
 * mod menu": "The order can be changed").  The manifest's `order` key is
 * the default: it is the mod author's own order, and it decides the list
 * while the player has not said otherwise.  The model holds the player's
 * list in the order the player chose (bw_mod_menu_move_up and its
 * siblings), the apply hands that order to the file layer
 * (bw_mod_set_order, sdk/vfs.h) and the loader loads the mods in it,
 * because the loader walks the file layer's list in order
 * (sdk/mod_loader.c).
 *
 * The order is the player's own and it is in the port's record
 * (<save dir>/mods.ini), so it outlives the port: the start-up restore
 * reads it back and the list comes up in the order the player left it in.
 *
 * The state namespace is per mod, so moving a mod in the list moves no
 * state: the namespace is the manifest's `state` key and it travels with
 * the mod, not with the mod's place in the list.
 *
 * The state the model needs from the other layers
 * ------------------------------------------------
 *
 * The saved data (sdk/mod_savedata.c) owns a mod's own file.  The menu
 * reaches it through two calls: bw_mod_savedata_set_namespace tells the
 * layer which mod it is serving, and bw_mod_savedata_clear deletes that
 * mod's state file and leaves its folder -- exactly the player's "clear
 * saved data".  The layer's own path rule (<save dir>/mods/<namespace>/)
 * is what the menu follows to find the folders that belong to no mod (the
 * orphan report) and to keep the player's settings beside the mod's saved
 * data.
 *
 * The manifest (sdk/mod_manifest.c) owns the record: the name, the state
 * namespace, the settings path and the code path.  The menu parses a
 * manifest per mod, the way the loader does, because the menu shows the
 * mods the loader did not load as well.
 *
 * The loader (sdk/mod_loader.c) owns the libraries: it loads them, it
 * stops them and it unloads them.  The menu calls it and does nothing
 * else with a library.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_event.h"
#include "mod_loader.h"
#include "mod_manifest.h"
#include "mod_menu.h"
#include "mod_rule.h"
#include "mod_table.h"
#include "mod_bw.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "save.h"
#include "vfs.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The manifest layer (sdk/mod_manifest.c).  The record is opaque here: the
 * menu creates one per mod, parses the manifest into it and reads the
 * pieces it needs through these calls, the way sdk/mod_loader.c declares
 * them.  The manifest layer has no header of its own yet, so the
 * prototypes are here.
 */
extern void       *bw_mod_manifest_new(const char *dir_name);
extern void        bw_mod_manifest_delete(void *manifest);
extern int         bw_mod_manifest_parse_dir(void *manifest, const char *root,
                                              const char *dir_name);
extern const char *bw_mod_manifest_name(const void *manifest);
extern const char *bw_mod_manifest_about(const void *manifest, int what);
extern const char *bw_mod_manifest_code(const void *manifest);
extern const char *bw_mod_manifest_state(const void *manifest);
extern const char *bw_mod_manifest_settings(const void *manifest);
extern int         bw_mod_manifest_namespace_valid(const char *ns);
extern char       *bw_mod_manifest_default_namespace(const char *name);

/*
 * The saved data (sdk/mod_savedata.c).  The layer has no header yet, so
 * the prototypes are here, the way sdk/mod_loader.c declares them.
 */
extern void bw_mod_savedata_set_namespace(const char *ns);
extern const char *bw_mod_savedata_namespace(void);
extern int  bw_mod_savedata_clear(void);
extern void bw_mod_savedata_save_begin(void);
extern int  bw_mod_savedata_save_done(int ok, u32 check);
extern u32  bw_mod_savedata_save_check(void);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many mods the menu holds. */
#define BW_MOD_MENU_MAX 32

/* The lengths of the port's own buffers. */
#define BW_MOD_MENU_PATH_MAX 1024
#define BW_MOD_MENU_ROOT_MAX 512
#define BW_MOD_MENU_NAME_MAX 96
#define BW_MOD_MENU_ABOUT_MAX 400
#define BW_MOD_MENU_NS_MAX 96
#define BW_MOD_MENU_SET_MAX 256
#define BW_MOD_MENU_KEY_MAX 64
#define BW_MOD_MENU_LABEL_MAX 64
#define BW_MOD_MENU_CHOICE_LEN 32
#define BW_MOD_MENU_TEXT_MAX 128        /* a text option's longest value + 1 */
#define BW_MOD_MENU_MODE_ID 32
#define BW_MOD_MENU_MODE_LINE 128
#define BW_MOD_MENU_CODE_MAX  160  /* a world code, or "ap <host:port> <slot> <password>" */
#define BW_MOD_MENU_MODES_MAX 8         /* the modes one mod may offer      */
#define BW_MOD_MENU_TABS_MAX 8         /* a mod's own Settings tabs (`tab` lines) */
#define BW_MOD_MENU_TAB_LEN 32          /* a tab's title + 1 */
#define BW_MOD_MENU_SHOW_MAX 63         /* a `show` row's longest text */
#define BW_MOD_MENU_LOBBY_BIT (1u << 31) /* an option's `@lobby` (modes are bits 0-7) */

/* A mod declares as many options, and an option as many choices, as it
 * wants: the lists grow.  The bounds only stop a broken file from taking
 * the whole memory. */
#define BW_MOD_MENU_OPTIONS_MAX 4096
#define BW_MOD_MENU_CHOICE_MAX  1024

/* The port's own record of the player's list. */
#define BW_MOD_MENU_RECORD "mods.ini"

/* The player's choices for one mod, beside the mod's saved data. */
#define BW_MOD_MENU_CHOICES "settings.ini"

/* ------------------------------------------------------------------ */
/* The records                                                         */
/* ------------------------------------------------------------------ */

/*
 * One option of a mod's own settings (docs/mod-services.md, "Mod
 * settings").  The declaration is the file the manifest's `settings` key
 * names:
 *
 *     option nuzlocke.dupes   bool    "Dupes clause"     on
 *     option nuzlocke.cap     choice  "Level caps"       off|gym|ace
 *
 * The key is the name the mod asks setting_get for, the label is the text
 * the screen shows, and the value is the player's choice: 0 or 1 for a
 * bool, the index of the chosen name for a choice.
 */
typedef struct BwModMenuOption {
    char key[BW_MOD_MENU_KEY_MAX];
    char label[BW_MOD_MENU_LABEL_MAX];
    int  kind;                     /* BW_MOD_SETTING_BOOL, _CHOICE, _INT, _TEXT */
    int  def;                      /* the default: 0/1, a choice index, a number */
    int  min;                      /* _INT: the lowest value  */
    int  max;                      /* _INT: the highest value; _TEXT: the longest text */
    int  value;                    /* the player's choice             */
    int  choice_count;
    int  choice_cap;
    char (*choices)[BW_MOD_MENU_CHOICE_LEN];   /* grows; freed with the model */
    char text[BW_MOD_MENU_TEXT_MAX];           /* _TEXT: the player's text   */
    char text_def[BW_MOD_MENU_TEXT_MAX];       /* _TEXT: the default         */
    u32  modes;                    /* the modes it is shown in (bit k: the mod's
                                    * mode k); 0: the Mods section alone */
    char when_key[BW_MOD_MENU_KEY_MAX];        /* `?key=value`: shown only while */
    char when_val[BW_MOD_MENU_CHOICE_LEN];     /* that option has that value     */
    int  when_not;                             /* `?key!=value`: while it has not */
    int  tab;                                  /* the mod's tab it is on (`tab`
                                                * lines), -1: under the mod in Mods */
    int  word;                                 /* _TEXT `word`: one word, no space */
    char heading[BW_MOD_MENU_LABEL_MAX];       /* a `heading` line above it      */
    char *help;                                /* its `help` line (freed with it) */
    char *yaml;                                /* its `yaml` line: "<key> <values>" */
} BwModMenuOption;

/*
 * A way to play a mod offers (a `mode` line): the Multiplayer screen shows
 * it as a card, and a new game started from its screen tells the mod which
 * mode the player chose (mode_get, during game.new).
 *
 *     mode solo "Solo randomizer" "A shuffled game for you alone."
 */
typedef struct BwModMenuMode {
    char id[BW_MOD_MENU_MODE_ID];
    char title[BW_MOD_MENU_LABEL_MAX];
    char line[BW_MOD_MENU_MODE_LINE];
    int  off;                      /* `off`: shown, not chosen yet (not built) */
    int  joins;                    /* `joins`: its games are worlds others join
                                    * by code (the Multiplayer screen's Join box) */
    int  in;                       /* `in=<mode>`: a type on that mode's card
                                    * (its index), -1 for a card of its own */
    char game[BW_MOD_MENU_LABEL_MAX];  /* `export="<AP game>"`: Start saves an
                                    * Archipelago options file instead */
} BwModMenuMode;

/* One mod the menu shows. */
typedef struct BwModMenuMod {
    char root[BW_MOD_MENU_ROOT_MAX];    /* the mod's own folder          */
    char name[BW_MOD_MENU_NAME_MAX];    /* the name the manifest gives   */
    char about[BW_MOD_MENU_ABOUT_MAX];  /* its `description` line, "" for none */
    char author[64];                    /* its `author` line             */
    char version[24];                   /* its `version` line            */
    char ns[BW_MOD_MENU_NS_MAX];        /* the state namespace           */
    char settings[BW_MOD_MENU_SET_MAX]; /* the settings declaration      */
    int  order;                         /* the manifest's order key      */
    int  player_pos;                    /* the player's place, -1 for none */
    u32  base_pos;                      /* the place it was built at     */
    int  enabled;                       /* the player's switch           */
    int  running;                       /* the root is in the port's list */
    int  present;                       /* the mod's folder is there     */
    int  needs_game;                    /* 0, or the game it is made for when
                                         * that is not the one loaded
                                         * (BW_MOD_GAME_*, sdk/vfs.h; -1: its
                                         * `game` line names none): the port
                                         * does not load it */
    char needs_eidos[16];               /* its `eidos` line when that asks
                                         * for a newer eidos than this one,
                                         * else "": the port does not load it */
    int  has_code;                      /* the mod ships code            */
    u32  option_count;
    u32  option_cap;
    BwModMenuOption *options;
    u32  mode_count;
    BwModMenuMode *modes;               /* BW_MOD_MENU_MODES_MAX, on the heap
                                         * (the 32-bit image has little room) */
    char heading_next[BW_MOD_MENU_LABEL_MAX];  /* a `heading` waiting for its option */
    char (*tabs)[BW_MOD_MENU_TAB_LEN];  /* its `tab` titles, on the heap; NULL for none */
    u32  tab_count;
    int  tab_cur;                       /* the tab the next lines go on, -1 none */
} BwModMenuMod;

/* A mod the port does not load whatever its switch says: made for the
 * other game, or for a newer eidos. */
static int unavailable(const BwModMenuMod *m)
{
    return m->needs_game != 0 || m->needs_eidos[0] != '\0';
}

/* One row of the port's own record: a mod's root, its switch and the
 * player's place in the list. */
typedef struct BwModMenuRow {
    char root[BW_MOD_MENU_ROOT_MAX];
    int  enabled;
    int  order;         /* the player's place in the list, -1 for none */
} BwModMenuRow;

static BwModMenuMod g_mods[BW_MOD_MENU_MAX];
static u32          g_count;
static int          g_built;

/*
 * The player's own order is in force: the record named an order, or the
 * player moved a mod.  While it is, the list is the player's and the
 * manifest's `order` key does not decide it; the file layer is told the
 * order as it is (bw_mod_set_order).  g_player_order_base is the value it
 * had when the menu was opened, so the revert can put it back.
 */
static int g_player_order;
static int g_player_order_base;

/* The port's own record of the player's list is there. */
static int g_have_record;

/* How many mods the last scan of the installed mods folder found. */
static u32 g_installed_found;

/* The next apply runs even when no switch changed: a reload. */
static int g_force_apply;

/* The folders in the save directory that no mod claims. */
static char g_orphans[BW_MOD_MENU_MAX][BW_MOD_MENU_NS_MAX];
static u32  g_orphan_count;

/* The mod that is calling: the port sets it around the calls it makes into
 * a mod, the way the loader sets the saved-data layer's. */
static char g_namespace[BW_MOD_MENU_NS_MAX];

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

static void copy_str(char *dst, size_t size, const char *src)
{
    size_t n;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim [*s, *e) of the blanks at its ends. */
static void trim(const char **s, const char **e)
{
    while (*s < *e && is_space(**s)) {
        (*s)++;
    }
    while (*e > *s && is_space((*e)[-1])) {
        (*e)--;
    }
}

/* The port's reports, the shape the documents give. */
static void report(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void report(const char *label, const char *fmt, ...)
{
    va_list ap;

    if (label == NULL || label[0] == '\0') {
        label = "?";
    }
    fprintf(stderr, "eidos: mod %s: ", label);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Read a whole file into memory.  A NULL result means the file is not
 * there. */
static char *read_file(const char *path)
{
    FILE *fp;
    char *buf;
    size_t cap = 4096;
    size_t len = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    for (;;) {
        size_t got;

        if (len + 1 >= cap) {
            char *grown = (char *)realloc(buf, cap * 2);
            if (grown == NULL) {
                free(buf);
                fclose(fp);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        got = fread(buf + len, 1, cap - len - 1, fp);
        len += got;
        if (got == 0) {
            break;
        }
    }
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

/* Grow a text buffer and add a piece to it. */
static int text_add(char **buf, size_t *len, size_t *cap, const char *text)
{
    size_t n = strlen(text);

    if (*len + n + 1 > *cap) {
        size_t grown_cap = (*cap == 0) ? 1024 : *cap;
        char *grown;

        while (grown_cap < *len + n + 1) {
            grown_cap *= 2;
        }
        grown = (char *)realloc(*buf, grown_cap);
        if (grown == NULL) {
            return -1;
        }
        *buf = grown;
        *cap = grown_cap;
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* The paths                                                           */
/* ------------------------------------------------------------------ */

/* The port's own record of the player's list: <save dir>/mods.ini. */
static int record_path(char *out, size_t size)
{
    return bw_save_path(out, size, BW_MOD_MENU_RECORD);
}

/* The folder the mods' own folders live in: <save dir>/mods. */
static int mods_folder_path(char *out, size_t size)
{
    return bw_save_path(out, size, "mods");
}

/* ------------------------------------------------------------------ */
/* The port's own record of the player's list                          */
/* ------------------------------------------------------------------ */

/*
 * Read the record: the mods the player knows about, their switches and
 * the player's place for each of them.  The rows come back in the order the
 * file gives them.
 */
static u32 read_record(BwModMenuRow *rows, u32 max)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    const char *p;
    u32 count = 0;

    if (record_path(path, sizeof path) != 0) {
        return 0;
    }
    text = read_file(path);
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char line[512];
        char *hash;
        char *eq;
        size_t n;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\0') ? e : e + 1;

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        {
            const char *s = line;
            const char *en = line + strlen(line);

            trim(&s, &en);
            if (s == en || *s == '[') {
                continue;               /* an empty line or a section */
            }
            eq = strchr(s, '=');
            if (eq == NULL) {
                continue;
            }
            ks = s;
            ke = eq;
            vs = eq + 1;
            ve = en;
            trim(&ks, &ke);
            trim(&vs, &ve);
            if ((size_t)(ke - ks) == 4 && memcmp(ks, "root", 4) == 0) {
                if (count >= max) {
                    continue;
                }
                copy_str(rows[count].root, sizeof rows[count].root, vs);
                rows[count].enabled = 0;
                rows[count].order = -1;
                count++;
            } else if ((size_t)(ke - ks) == 7 && memcmp(ks, "enabled", 7) == 0) {
                if (count > 0) {
                    rows[count - 1].enabled = (strtol(vs, NULL, 10) != 0);
                }
            } else if ((size_t)(ke - ks) == 5 && memcmp(ks, "order", 5) == 0) {
                if (count > 0) {
                    long v = strtol(vs, NULL, 10);

                    /* The player's place in the list: a number the model
                     * can hold.  A value that is not one is ignored, so a
                     * hand edit cannot scramble the list: the manifest's
                     * own order key stands instead. */
                    if (v < 0 || v >= (long)BW_MOD_MENU_MAX) {
                        v = -1;
                    }
                    rows[count - 1].order = (int)v;
                }
            }
            /* An unknown key is ignored, the way the manifest reader
             * ignores one: a later version may add keys. */
        }
    }
    free(text);
    return count;
}

/*
 * Write the record: the mods the player knows about, their switches and
 * the order the player chose.  One block per mod, in the order the player
 * left them in:
 *
 *     [mod]
 *     root = <the mod's folder>
 *     enabled = 1
 *     order = 0
 *
 * The `order` key is the player's place in the list, 0 first.  It is not
 * the manifest's `order` key: that one is the mod author's, and it decides
 * the list while the player has not said otherwise.
 */
static int write_record(void)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    u32 i;
    int rc;

    if (record_path(path, sizeof path) != 0) {
        return -1;
    }
    if (text_add(&text, &len, &cap,
                 "# eidos mod record (sdk/mod_menu.c).\n"
                 "# The mods the player knows about, the state of their\n"
                 "# switches and the order the player chose for them.\n"
                 "# The port owns this file.\n") != 0) {
        return -1;
    }
    for (i = 0; i < g_count; i++) {
        char block[1024];
        int n = snprintf(block, sizeof block,
                         "\n[mod]\nroot = %s\nenabled = %d\norder = %d\n",
                         g_mods[i].root, g_mods[i].enabled ? 1 : 0, (int)i);

        if (n < 0 || (size_t)n >= sizeof block) {
            free(text);
            return -1;
        }
        if (text_add(&text, &len, &cap, block) != 0) {
            free(text);
            return -1;
        }
    }
    rc = bw_save_write_atomic(path, text, (u32)len);
    free(text);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The manifest of one mod                                             */
/* ------------------------------------------------------------------ */

/*
 * Read one mod's manifest: the name, the state namespace, the settings
 * declaration, the code path and the order key.
 *
 * The namespace follows the manifest layer's rule: the stated `state` when
 * it is one path component, and the default derived from the name when it
 * is not (docs/mod-state.md, section 1).  The manifest layer reports a bad
 * namespace when it applies a manifest; the menu does not report it again,
 * because the loader does, once, when the mod is loaded.
 */
static void read_manifest(BwModMenuMod *m)
{
    void *record;
    const char *ns;

    record = bw_mod_manifest_new(NULL);
    if (record == NULL) {
        return;
    }
    if (bw_mod_manifest_parse_dir(record, m->root, NULL) != 0) {
        /* The manifest could not be read: the defaults stand, the way
         * they stand for a mod with no manifest at all. */
        bw_mod_manifest_delete(record);
        return;
    }
    copy_str(m->name, sizeof m->name, bw_mod_manifest_name(record));
    {
        const char *t;

        t = bw_mod_manifest_about(record, 0);
        copy_str(m->about, sizeof m->about, t != NULL ? t : "");
        t = bw_mod_manifest_about(record, 1);
        copy_str(m->author, sizeof m->author, t != NULL ? t : "");
        t = bw_mod_manifest_about(record, 2);
        copy_str(m->version, sizeof m->version, t != NULL ? t : "");
    }
    ns = bw_mod_manifest_state(record);
    if (ns != NULL && ns[0] != '\0' && bw_mod_manifest_namespace_valid(ns)) {
        copy_str(m->ns, sizeof m->ns, ns);
    } else {
        char *def = bw_mod_manifest_default_namespace(
            bw_mod_manifest_name(record));

        if (def != NULL) {
            copy_str(m->ns, sizeof m->ns, def);
            free(def);
        }
    }
    copy_str(m->settings, sizeof m->settings,
             bw_mod_manifest_settings(record));
    {
        const char *code = bw_mod_manifest_code(record);

        m->has_code = (code != NULL && code[0] != '\0');
    }
    /* The manifest layer has no call for the order key yet; the record is
     * published in sdk/mod_manifest.h, and the loader reads the same
     * field. */
    m->order = ((const BwModManifest *)record)->order;
    bw_mod_manifest_delete(record);
}

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

/* The next token of a line: a quoted string or a word.  NULL at the end
 * of the line. */
static char *next_token(char **p)
{
    char *s = *p;
    char *start;
    char *out;
    size_t n;

    while (*s != '\0' && is_space(*s)) {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    if (*s == '"') {
        s++;
        start = s;
        while (*s != '\0' && *s != '"') {
            s++;
        }
        n = (size_t)(s - start);
        if (*s == '"') {
            s++;
        }
        *p = s;
        out = (char *)malloc(n + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, start, n);
        out[n] = '\0';
        return out;
    }
    start = s;
    while (*s != '\0' && !is_space(*s)) {
        s++;
    }
    n = (size_t)(s - start);
    *p = s;
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* A bool's value: 1 for "on", "true" and "1", 0 for "off", "false" and
 * "0", and -1 for anything else (a typo is reported, not read as off). */
static int bool_value(const char *text)
{
    if (text == NULL) {
        return -1;
    }
    if (strcmp(text, "on") == 0 || strcmp(text, "true") == 0 ||
        strcmp(text, "1") == 0) {
        return 1;
    }
    if (strcmp(text, "off") == 0 || strcmp(text, "false") == 0 ||
        strcmp(text, "0") == 0) {
        return 0;
    }
    return -1;
}

/* Add one name to a choice option.  0, or -1 (no memory, or too many). */
static int option_add_choice(BwModMenuOption *o, const char *name, size_t n)
{
    if (o->choice_count >= o->choice_cap) {
        int cap = o->choice_cap == 0 ? 8 : o->choice_cap * 2;
        char (*grown)[BW_MOD_MENU_CHOICE_LEN];

        if (cap > BW_MOD_MENU_CHOICE_MAX) {
            return -1;
        }
        grown = (char (*)[BW_MOD_MENU_CHOICE_LEN])realloc(
            o->choices, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        o->choices = grown;
        o->choice_cap = cap;
    }
    if (n >= BW_MOD_MENU_CHOICE_LEN) {
        n = BW_MOD_MENU_CHOICE_LEN - 1;
    }
    memcpy(o->choices[o->choice_count], name, n);
    o->choices[o->choice_count][n] = '\0';
    o->choice_count++;
    return 0;
}

/* A key is a word without spaces, '=' or '#': it is written as
 * `key = value` in the player's choices file. */
static int key_valid(const char *key)
{
    const char *s;

    if (key == NULL || *key == '\0' || strlen(key) >= BW_MOD_MENU_KEY_MAX) {
        return 0;
    }
    for (s = key; *s != '\0'; s++) {
        if (*s == '=' || *s == '#' || *s == '"' || is_space(*s)) {
            return 0;
        }
    }
    return 1;
}

/* Cut a line's comment: the first '#' outside a quoted label. */
static void cut_comment(char *line)
{
    int quoted = 0;
    char *s;

    for (s = line; *s != '\0'; s++) {
        if (*s == '"') {
            quoted = !quoted;
        } else if (*s == '#' && !quoted) {
            *s = '\0';
            return;
        }
    }
}

/*
 * One line of the settings declaration:
 *
 *     option <key> <bool|choice> "<label>" <value>
 *
 * The value of a bool is "on" or "off"; the value of a choice is the list
 * of its names, separated with '|', and the first of them is the default.
 * The documents give the example and leave the grammar's details open
 * (docs/mod-services.md, "Mod settings"), so the reading is the port's own.
 */
/*
 * A mode line:
 *
 *     mode <id> "<title>" "<line>" [off] [joins] [in=<mode>] [export "<game>"]
 *
 * The modes come before the options that name them.  `off` shows the card
 * without letting the player choose it (a mode the mod has not built yet).
 * `joins`: the mode's games are worlds others join with a code, typed in the
 * Multiplayer screen's Join box.  `in=<mode>`: no card of its own, a type on
 * that mode's card.  `export "<game>"`: Start saves an Archipelago options
 * file for that game ({version} is Black or White) instead of a new game.
 */
static int parse_mode(BwModMenuMod *m, char *p)
{
    char *id = next_token(&p);
    char *title = next_token(&p);
    char *line = next_token(&p);
    char *flag = next_token(&p);
    BwModMenuMode *md;
    int rc = -1;

    if (id == NULL || title == NULL || line == NULL || !key_valid(id) ||
        strlen(id) >= BW_MOD_MENU_MODE_ID || strchr(id, ',') != NULL) {
        report(m->name, "a mode line needs <id> \"<title>\" \"<line>\"; it is ignored");
    } else if (m->mode_count >= BW_MOD_MENU_MODES_MAX) {
        report(m->name, "more than %d modes; '%s' is ignored", BW_MOD_MENU_MODES_MAX, id);
    } else if (m->modes == NULL &&
               (m->modes = (BwModMenuMode *)calloc(BW_MOD_MENU_MODES_MAX,
                                                   sizeof(BwModMenuMode))) == NULL) {
        report(m->name, "no memory for the modes");
    } else {
        u32 i;

        for (i = 0; i < m->mode_count; i++) {
            if (strcmp(m->modes[i].id, id) == 0) {
                break;
            }
        }
        if (i < m->mode_count) {
            report(m->name, "the mode '%s' is declared twice; the second is ignored", id);
        } else {
            if (strlen(title) >= sizeof md->title || strlen(line) >= sizeof md->line) {
                report(m->name, "the mode '%s' has a title longer than %d or a line "
                       "longer than %d characters; the rest is cut", id,
                       (int)sizeof md->title - 1, (int)sizeof md->line - 1);
            }
            md = &m->modes[m->mode_count++];
            memset(md, 0, sizeof *md);
            copy_str(md->id, sizeof md->id, id);
            copy_str(md->title, sizeof md->title, title);
            copy_str(md->line, sizeof md->line, line);
            md->in = -1;
            /* the words after the line, in any order */
            while (flag != NULL) {
                if (strcmp(flag, "off") == 0) {
                    md->off = 1;
                } else if (strcmp(flag, "joins") == 0) {
                    md->joins = 1;
                } else if (strncmp(flag, "in=", 3) == 0) {
                    u32 k;

                    for (k = 0; k + 1 < m->mode_count; k++) {
                        if (strcmp(m->modes[k].id, flag + 3) == 0 && m->modes[k].in < 0) {
                            md->in = (int)k;
                        }
                    }
                    if (md->in < 0) {
                        report(m->name, "the mode '%s' is in '%s', which no mode line above "
                               "declares as a card", id, flag + 3);
                    }
                } else if (strcmp(flag, "export") == 0) {
                    char *game = next_token(&p);

                    if (game != NULL) {
                        copy_str(md->game, sizeof md->game, game);
                        free(game);
                    }
                } else {
                    report(m->name, "the mode '%s' has the word '%s', which is not off, joins, "
                           "in=<mode> or export \"<game>\"; it is ignored", id, flag);
                }
                free(flag);
                flag = next_token(&p);
            }
            rc = 0;
        }
    }
    free(id);
    free(title);
    free(line);
    free(flag);
    return rc;
}

/*
 * The modes an option is shown in: a last word `@id,id` on its line, cut
 * off the value.  Each id names a mode declared above.  0 (no word): the
 * option is in the Mods section alone.
 */
static u32 cut_mode_tags(BwModMenuMod *m, const char *key, char *value)
{
    char *at = NULL;
    char *s;
    int quoted = 0;
    u32 bits = 0;

    for (s = value; *s != '\0'; s++) {
        if (*s == '"') {
            quoted = !quoted;
        } else if (*s == '@' && !quoted && (s == value || is_space(s[-1]))) {
            at = s;
        }
    }
    if (at == NULL) {
        return 0;
    }
    for (s = at + 1; *s != '\0'; s++) {
        if (is_space(*s)) {
            return 0;               /* not the last word: part of the value */
        }
    }
    {
        char *t = at + 1;

        while (*t != '\0') {
            char *e = strchr(t, ',');
            size_t n = e != NULL ? (size_t)(e - t) : strlen(t);
            u32 i;

            if (n == 5 && memcmp(t, "lobby", 5) == 0) {
                /* the co-op lobby's rules: the host's value is everyone's */
                bits |= BW_MOD_MENU_LOBBY_BIT;
                t += n + (e != NULL ? 1 : 0);
                continue;
            }
            for (i = 0; i < m->mode_count; i++) {
                if (strlen(m->modes[i].id) == n && memcmp(m->modes[i].id, t, n) == 0) {
                    bits |= 1u << i;
                    break;
                }
            }
            if (i == m->mode_count && n > 0) {
                report(m->name, "the option '%s' names the mode '%.*s', which no "
                       "mode line above declares", key, (int)n, t);
            }
            t += n + (e != NULL ? 1 : 0);
        }
    }
    /* the value ends before the word */
    while (at > value && is_space(at[-1])) {
        at--;
    }
    *at = '\0';
    return bits;
}

/*
 * The condition an option is shown under: a last word `?key=value` on its
 * line, cut off the value into `out` ("key=value"; "" for none).  It names
 * another option of the same mod and one of its values (a choice's name,
 * on/off, a number, a text), so a screen asks only what the answers so far
 * leave open: a world's code only to join it, its rules only to make it.
 */
static void cut_when(char *value, char *out, size_t n)
{
    char *s, *q = NULL;
    int quoted = 0;

    out[0] = '\0';
    for (s = value; *s != '\0'; s++) {
        if (*s == '"') {
            quoted = !quoted;
        } else if (*s == '?' && !quoted && (s == value || is_space(s[-1]))) {
            q = s;
        }
    }
    if (q == NULL || strchr(q, '=') == NULL) {
        return;
    }
    for (s = q + 1; *s != '\0'; s++) {
        if (is_space(*s)) {
            return;                 /* not the last word: part of the value */
        }
    }
    copy_str(out, n, q + 1);
    while (q > value && is_space(q[-1])) {
        q--;
    }
    *q = '\0';
}

/*
 * The lines that dress the options:
 *
 *     heading "<text>"                  a title above the next option
 *     help <key> "<text>"               what the option does (the screen's
 *                                       side panel, while its row is chosen)
 *     yaml <key> <name> [<v>|<v>|...]   the option in an Archipelago options
 *                                       file: its name there, and the value
 *                                       of each choice (bool: off|on; an int
 *                                       is written as the number)
 *
 * help and yaml name an option declared above them.
 */
static int parse_extra(BwModMenuMod *m, const char *what, char *p)
{
    char *a = next_token(&p);
    BwModMenuOption *o = NULL;
    u32 i;
    int rc = 0;

    if (a == NULL) {
        report(m->name, "a %s line with nothing after it; it is ignored", what);
        return -1;
    }
    if (strcmp(what, "heading") == 0) {
        copy_str(m->heading_next, sizeof m->heading_next, a);
        free(a);
        return 0;
    }
    for (i = 0; i < m->option_count; i++) {
        if (strcmp(m->options[i].key, a) == 0) {
            o = &m->options[i];
        }
    }
    if (o == NULL) {
        report(m->name, "the %s line names '%s', which no option line above declares", what, a);
        free(a);
        return -1;
    }
    if (strcmp(what, "help") == 0) {
        char *text = next_token(&p);

        if (text != NULL) {
            free(o->help);
            o->help = text;
        } else {
            rc = -1;
        }
    } else {
        const char *s = p;
        size_t n;

        while (*s != '\0' && is_space(*s)) {
            s++;
        }
        n = strlen(s);
        while (n > 0 && is_space(s[n - 1])) {
            n--;
        }
        free(o->yaml);
        o->yaml = (char *)malloc(n + 1);
        if (o->yaml != NULL) {
            memcpy(o->yaml, s, n);
            o->yaml[n] = '\0';
        }
        rc = n > 0 ? 0 : -1;
    }
    if (rc != 0) {
        report(m->name, "the %s line for '%s' is not complete; it is ignored", what, a);
    }
    free(a);
    return rc;
}

/*
 * `tab "<title>"`: the lines after it, up to the next `tab` line, are a tab
 * of the settings screen of their own (after the port's tabs).  Lines
 * before any tab stay under the mod's row in the Mods section.
 */
static int parse_tab(BwModMenuMod *m, char *p)
{
    char *title = next_token(&p);
    char *more = next_token(&p);

    if (title == NULL || title[0] == '\0' || more != NULL || strlen(title) >= BW_MOD_MENU_TAB_LEN) {
        report(m->name, "a tab line needs one quoted title of at most %d characters; it is ignored",
               BW_MOD_MENU_TAB_LEN - 1);
        free(title);
        free(more);
        return -1;
    }
    if (m->tab_count >= BW_MOD_MENU_TABS_MAX) {
        report(m->name, "more than %d tabs; '%s' and what follows go on the last one",
               BW_MOD_MENU_TABS_MAX, title);
        free(title);
        return -1;
    }
    if (m->tabs == NULL) {
        m->tabs = (char (*)[BW_MOD_MENU_TAB_LEN])calloc(BW_MOD_MENU_TABS_MAX, BW_MOD_MENU_TAB_LEN);
        if (m->tabs == NULL) {
            free(title);
            return -1;
        }
    }
    copy_str(m->tabs[m->tab_count], BW_MOD_MENU_TAB_LEN, title);
    m->tab_cur = (int)m->tab_count++;
    m->heading_next[0] = '\0';
    free(title);
    return 0;
}

static int parse_option(BwModMenuMod *m, char *line)
{
    char when[BW_MOD_MENU_KEY_MAX + BW_MOD_MENU_CHOICE_LEN + 2];
    char *p = line;
    char *key;
    char *kind;
    char *label;
    char *value;
    BwModMenuOption *o;
    u32 modes;

    key = next_token(&p);
    if (key == NULL) {
        return 0;
    }
    if (strcmp(key, "mode") == 0) {
        free(key);
        return parse_mode(m, p);
    }
    if (strcmp(key, "heading") == 0 || strcmp(key, "help") == 0 || strcmp(key, "yaml") == 0) {
        int rc = parse_extra(m, key, p);

        free(key);
        return rc;
    }
    if (strcmp(key, "tab") == 0) {
        free(key);
        return parse_tab(m, p);
    }
    if (strcmp(key, "button") == 0 || strcmp(key, "show") == 0) {
        /* `button <key> "<label>"` and `show <key> "<label>"`: an option
         * line with its kind first and no value */
        char *k2 = next_token(&p);
        char row[4096];

        if (k2 == NULL) {
            report(m->name, "a %s line with no key; it is ignored", key);
            free(key);
            return -1;
        }
        snprintf(row, sizeof row, "option %s %s %s", k2, key, p);
        free(k2);
        free(key);
        return parse_option(m, row);
    }
    if (strcmp(key, "option") != 0) {
        free(key);
        return 0;
    }
    free(key);
    key = next_token(&p);
    kind = next_token(&p);
    label = next_token(&p);
    if (key == NULL || kind == NULL || label == NULL) {
        free(key);
        free(kind);
        free(label);
        return -1;
    }
    /* The value is the rest of the line: the names of a choice are
     * separated with '|' and hold no space. */
    {
        const char *s = p;
        const char *e;

        while (*s != '\0' && is_space(*s)) {
            s++;
        }
        e = s + strlen(s);
        trim(&s, &e);
        value = (char *)malloc((size_t)(e - s) + 1);
        if (value == NULL) {
            free(key);
            free(kind);
            free(label);
            return -1;
        }
        memcpy(value, s, (size_t)(e - s));
        value[e - s] = '\0';
    }
    /* the two last words come in either order */
    cut_when(value, when, sizeof when);
    modes = cut_mode_tags(m, key, value);
    if (when[0] == '\0') {
        cut_when(value, when, sizeof when);
    }
    if (!key_valid(key)) {
        report(m->name, "the option key '%s' holds a space, '=', '#' or '\"', "
               "or is too long; it is ignored", key);
        free(key);
        free(kind);
        free(label);
        free(value);
        return -1;
    }
    {
        u32 i;

        for (i = 0; i < m->option_count; i++) {
            if (strcmp(m->options[i].key, key) == 0) {
                report(m->name, "the option '%s' is declared twice; the "
                       "second is ignored", key);
                free(key);
                free(kind);
                free(label);
                free(value);
                return -1;
            }
        }
    }
    if (m->option_count >= m->option_cap) {
        u32 cap = m->option_cap == 0 ? 16u : m->option_cap * 2u;
        BwModMenuOption *grown;

        grown = cap > BW_MOD_MENU_OPTIONS_MAX ? NULL
                : (BwModMenuOption *)realloc(m->options, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            report(m->name, "too many options; the rest are ignored");
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        m->options = grown;
        m->option_cap = cap;
    }
    o = &m->options[m->option_count];
    memset(o, 0, sizeof *o);
    copy_str(o->key, sizeof o->key, key);
    copy_str(o->label, sizeof o->label, label);
    /* the group it is in: the last heading above it (the screen draws the
     * heading where the group changes, whichever of its options a mode shows) */
    copy_str(o->heading, sizeof o->heading, m->heading_next);
    o->tab = m->tab_cur;
    if (when[0] != '\0') {
        char *eq = strchr(when, '=');

        if (eq > when && eq[-1] == '!') {
            o->when_not = 1;            /* `?key!=value` */
            eq[-1] = '\0';
        }
        *eq = '\0';
        copy_str(o->when_key, sizeof o->when_key, when);
        copy_str(o->when_val, sizeof o->when_val, eq + 1);
    }
    if (strcmp(kind, "bool") == 0) {
        o->kind = BW_MOD_SETTING_BOOL;
        o->def = bool_value(value);
        if (o->def < 0) {
            report(m->name, "the bool option '%s' has the value '%s'; it takes "
                   "on or off; it is ignored", o->key, value);
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        o->value = o->def;
    } else if (strcmp(kind, "choice") == 0) {
        const char *s = value;

        o->kind = BW_MOD_SETTING_CHOICE;
        for (;;) {
            const char *sep = strchr(s, '|');
            const char *name = s;
            const char *name_end = (sep != NULL) ? sep : s + strlen(s);
            size_t n;

            trim(&name, &name_end);
            n = (size_t)(name_end - name);
            if (n > 0 && option_add_choice(o, name, n) != 0) {
                report(m->name, "the option '%s' has too many choices; the "
                       "rest are ignored", o->key);
                break;
            }
            if (sep == NULL) {
                break;
            }
            s = sep + 1;
        }
        if (o->choice_count == 0) {
            report(m->name, "the option '%s' names no choice; it is ignored",
                   o->key);
            free(o->choices);
            o->choices = NULL;
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        o->def = 0;
        o->value = o->def;
    } else if (strcmp(kind, "int") == 0) {
        /* `<min> <max> <default>`, three whole numbers. */
        long lo, hi, def;
        char *end;

        lo = strtol(value, &end, 10);
        hi = strtol(end, &end, 10);
        def = strtol(end, &end, 10);
        while (*end != '\0' && is_space(*end)) {
            end++;
        }
        if (*end != '\0' || lo > hi || def < lo || def > hi ||
            lo < -1000000000L || hi > 1000000000L) {
            report(m->name, "the int option '%s' needs '<min> <max> <default>' "
                   "with min <= default <= max; it is ignored", key);
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        o->kind = BW_MOD_SETTING_INT;
        o->min = (int)lo;
        o->max = (int)hi;
        o->def = (int)def;
        o->value = o->def;
    } else if (strcmp(kind, "text") == 0 || strcmp(kind, "secret") == 0) {
        /* text: `<longest> "<default>" [word]`, the default may be left out
         * (empty); a text holds no '"' (the choices file quotes it); `word`:
         * one word (the keyboard has no SPACE).  secret: `<longest>` alone,
         * a password: never written to a file, cleared when the settings
         * close, and any printable letter */
        int secret = kind[0] == 's';
        char *end;
        long len = strtol(value, &end, 10);
        char *q = end;
        char *def = secret ? NULL : next_token(&q);
        char *flag = next_token(&q);
        int word = flag != NULL && strcmp(flag, "word") == 0;

        while (*q != '\0' && is_space(*q)) {
            q++;
        }
        if (end == value || len < 1 || len >= BW_MOD_MENU_TEXT_MAX || *q != '\0' ||
            (flag != NULL && (!word || secret)) || (def != NULL && (long)strlen(def) > len)) {
            report(m->name, secret ? "the secret option '%s' needs '<longest>' with 1 <= longest <= %d; "
                                     "it is ignored"
                                   : "the text option '%s' needs '<longest> \"<default>\" [word]' with "
                                     "1 <= longest <= %d; it is ignored", key, BW_MOD_MENU_TEXT_MAX - 1);
            free(def);
            free(flag);
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        o->kind = secret ? BW_MOD_SETTING_SECRET : BW_MOD_SETTING_TEXT;
        o->max = (int)len;
        o->word = word;
        copy_str(o->text_def, sizeof o->text_def, def != NULL ? def : "");
        copy_str(o->text, sizeof o->text, o->text_def);
        free(def);
        free(flag);
        if (secret && (modes & BW_MOD_MENU_LOBBY_BIT)) {
            report(m->name, "the secret option '%s' cannot be a lobby rule; @lobby is ignored", key);
            modes &= ~BW_MOD_MENU_LOBBY_BIT;
        }
    } else if ((strcmp(kind, "button") == 0 || strcmp(kind, "show") == 0) && value[0] == '\0') {
        o->kind = kind[0] == 'b' ? BW_MOD_SETTING_BUTTON : BW_MOD_SETTING_SHOW;
        if (modes & BW_MOD_MENU_LOBBY_BIT) {
            report(m->name, "the %s '%s' cannot be a lobby rule; @lobby is ignored", kind, key);
            modes &= ~BW_MOD_MENU_LOBBY_BIT;
        }
    } else {
        report(m->name, "the option '%s' has the unknown kind '%s'; it is "
               "ignored", key, kind);
        free(key);
        free(kind);
        free(label);
        free(value);
        return -1;
    }
    o->modes = modes;
    m->option_count++;
    free(key);
    free(kind);
    free(label);
    free(value);
    return 0;
}

/* The path of the mod's settings declaration: <root>/<settings path>. */
static int declaration_path(char *out, size_t size, const BwModMenuMod *m)
{
    int n;

    if (m->root[0] == '\0' || m->settings[0] == '\0') {
        return -1;
    }
    n = snprintf(out, size, "%s/%s", m->root, m->settings);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }
    return 0;
}

/* The path of the player's choices for the mod, beside the mod's saved
 * data: <save dir>/mods/<namespace>/settings.ini. */
static int choices_path(char *out, size_t size, const BwModMenuMod *m)
{
    char rel[256];
    int n;

    if (m->ns[0] == '\0') {
        return -1;
    }
    n = snprintf(rel, sizeof rel, "mods/%s/%s", m->ns, BW_MOD_MENU_CHOICES);
    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* Read the declaration file and fill the mod's options. */
static void read_declaration(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    char *p;

    if (declaration_path(path, sizeof path, m) != 0) {
        return;                 /* the manifest names no settings */
    }
    text = read_file(path);
    if (text == NULL) {
        return;                 /* no declaration: the mod has no settings */
    }
    m->tab_cur = -1;
    p = text;
    while (*p != '\0') {
        char *e = strchr(p, '\n');
        char line[4096];
        size_t n;

        if (e == NULL) {
            e = p + strlen(p);
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\n') ? e + 1 : e;
        cut_comment(line);
        parse_option(m, line);
    }
    free(text);
}

/* Read the player's choices and put them over the defaults. */
static void read_choices(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    const char *p;

    if (m->option_count == 0) {
        return;
    }
    if (choices_path(path, sizeof path, m) != 0) {
        return;
    }
    text = read_file(path);
    if (text == NULL) {
        return;                 /* no choices yet: the defaults stand */
    }
    p = text;
    while (*p != '\0') {
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char line[512];
        char *hash;
        char *eq;
        size_t n;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\0') ? e : e + 1;

        /* a text's value is quoted and may hold '#' */
        (void)hash;
        cut_comment(line);
        {
            const char *s = line;
            const char *en = line + strlen(line);

            trim(&s, &en);
            if (s == en) {
                continue;
            }
            eq = strchr(s, '=');
            if (eq == NULL) {
                continue;
            }
            ks = s;
            ke = eq;
            vs = eq + 1;
            ve = en;
            trim(&ks, &ke);
            trim(&vs, &ve);
            {
                u32 i;

                for (i = 0; i < m->option_count; i++) {
                    BwModMenuOption *o = &m->options[i];
                    long value;

                    if ((size_t)(ke - ks) != strlen(o->key) ||
                        memcmp(o->key, ks, (size_t)(ke - ks)) != 0) {
                        continue;
                    }
                    if (o->kind == BW_MOD_SETTING_SECRET || o->kind == BW_MOD_SETTING_BUTTON ||
                        o->kind == BW_MOD_SETTING_SHOW) {
                        break;                  /* never kept in the file */
                    }
                    if (o->kind == BW_MOD_SETTING_TEXT) {
                        /* key = "text" */
                        if (ve - vs >= 2 && vs[0] == '"' && ve[-1] == '"' &&
                            (size_t)(ve - vs - 2) <= (size_t)o->max) {
                            memcpy(o->text, vs + 1, (size_t)(ve - vs - 2));
                            o->text[ve - vs - 2] = '\0';
                        }
                        break;
                    }
                    value = strtol(vs, NULL, 10);
                    if (o->kind == BW_MOD_SETTING_BOOL) {
                        if (value != 0 && value != 1) {
                            break;
                        }
                    } else if (o->kind == BW_MOD_SETTING_INT) {
                        if (value < o->min || value > o->max) {
                            break;
                        }
                    } else {
                        if (value < 0 || value >= o->choice_count) {
                            break;
                        }
                    }
                    o->value = (int)value;
                    break;
                }
            }
        }
    }
    free(text);
}

/* The mod's own settings: the declaration and the player's choices. */
static void read_settings(BwModMenuMod *m)
{
    read_declaration(m);
    read_choices(m);
}

/* Write the player's choices for one mod: beside the mod's saved data. */
static int write_choices(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    u32 i;
    int rc;

    if (m->option_count == 0) {
        return 0;
    }
    if (choices_path(path, sizeof path, m) != 0) {
        return -1;
    }
    if (text_add(&text, &len, &cap,
                 "# eidos mod settings (sdk/mod_menu.c): the player's\n"
                 "# choices for one mod.  The port owns this file.\n") != 0) {
        return -1;
    }
    for (i = 0; i < m->option_count; i++) {
        BwModMenuOption *o = &m->options[i];
        char line[256];
        int n;

        if (o->kind == BW_MOD_SETTING_SECRET || o->kind == BW_MOD_SETTING_BUTTON ||
            o->kind == BW_MOD_SETTING_SHOW) {
            continue;                   /* a password is never written; a button
                                         * and a mod's row hold no choice */
        }
        n = o->kind == BW_MOD_SETTING_TEXT
                ? snprintf(line, sizeof line, "%s = \"%s\"\n", o->key, o->text)
                : snprintf(line, sizeof line, "%s = %d\n", o->key, o->value);

        if (n < 0 || (size_t)n >= sizeof line) {
            free(text);
            return -1;
        }
        if (text_add(&text, &len, &cap, line) != 0) {
            free(text);
            return -1;
        }
    }
    rc = bw_save_write_atomic(path, text, (u32)len);
    free(text);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/* Release the model. */
static void model_free(void)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        u32 o;

        for (o = 0; o < g_mods[i].option_count; o++) {
            free(g_mods[i].options[o].choices);
            free(g_mods[i].options[o].help);
            free(g_mods[i].options[o].yaml);
        }
        free(g_mods[i].options);
        g_mods[i].options = NULL;
        g_mods[i].option_count = 0;
        g_mods[i].option_cap = 0;
        free(g_mods[i].modes);
        g_mods[i].modes = NULL;
        g_mods[i].mode_count = 0;
        free(g_mods[i].tabs);
        g_mods[i].tabs = NULL;
        g_mods[i].tab_count = 0;
    }
    g_count = 0;
    g_built = 0;
    g_orphan_count = 0;
    g_player_order = 0;
    g_player_order_base = 0;
    g_have_record = 0;
}

/* A new row of the model, or NULL when the model is full. */
static BwModMenuMod *model_add(void)
{
    BwModMenuMod *m;

    if (g_count >= BW_MOD_MENU_MAX) {
        return NULL;
    }
    m = &g_mods[g_count++];
    memset(m, 0, sizeof *m);
    return m;
}

/* Two spellings of one folder: '\' and '/' alike, a trailing one left out,
 * and on Windows the case too (a mod given with --mod is also found in the
 * mods folder, spelled the other way). */
static int same_root(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b), i;

    while (la > 1 && (a[la - 1] == '/' || a[la - 1] == '\\')) la--;
    while (lb > 1 && (b[lb - 1] == '/' || b[lb - 1] == '\\')) lb--;
    if (la != lb) {
        return 0;
    }
    for (i = 0; i < la; i++) {
        int ca = (unsigned char)a[i], cb = (unsigned char)b[i];

        if (ca == '\\') ca = '/';
        if (cb == '\\') cb = '/';
#ifdef _WIN32
        ca = tolower(ca);
        cb = tolower(cb);
#endif
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

/* The row of the mod whose root is `root`, or NULL. */
static BwModMenuMod *model_find_root(const char *root)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (same_root(g_mods[i].root, root)) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/* The row of the mod whose namespace is `ns`, or NULL. */
static BwModMenuMod *model_find_ns(const char *ns)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_mods[i].ns[0] != '\0' && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/*
 * The order two rows stand in.
 *
 * The player's own place decides when both the rows have one: the list is
 * the order the player chose.  The manifest's `order` key decides the rest:
 * a mod the player has not ordered keeps the place its author gave it, and
 * it comes before the mods the player has ordered, so a mod the player has
 * never seen does not take a win away from one they have.
 */
static int model_before(const BwModMenuMod *a, const BwModMenuMod *b)
{
    if ((a->player_pos >= 0) != (b->player_pos >= 0)) {
        return a->player_pos < 0;
    }
    if (a->player_pos >= 0) {
        return a->player_pos < b->player_pos;
    }
    return a->order < b->order;
}

/* The rows in load order: the player's own order when the record names
 * one, the manifest's order key otherwise.  The sort is stable, so the
 * mods the port's list holds keep the order the port gave them. */
static void model_sort(void)
{
    u32 i;
    u32 j;

    for (i = 1; i < g_count; i++) {
        BwModMenuMod tmp = g_mods[i];

        for (j = i; j > 0 && model_before(&tmp, &g_mods[j - 1]); j--) {
            g_mods[j] = g_mods[j - 1];
        }
        g_mods[j] = tmp;
    }
}

/*
 * Build the model: the mods the port's list holds, in load order, and the
 * mods the player's record holds that the list does not.
 *
 * The record is the player's own: it names the mods the player knows
 * about, their switches and the place the player gave each of them in the
 * list.  A mod the record does not name is one the player has not ordered:
 * the manifest's `order` key decides where it goes.
 */
/*
 * The installed mods folder: where the player puts mods.  EIDOS_MODS_DIR
 * names it; otherwise it is "mods" beside the save folder
 * (<app data>/eidos/mods next to <app data>/eidos/saves).  It is not
 * <save dir>/mods, which holds the mods' own saved data.
 */
int bw_mod_menu_installed_folder(char *out, size_t size)
{
    const char *env = getenv("EIDOS_MODS_DIR");
    const char *save;
    size_t n;
    int k;

    if (out == NULL || size == 0) {
        return -1;
    }
    if (env != NULL && env[0] != '\0') {
        k = snprintf(out, size, "%s", env);
        return (k < 0 || (size_t)k >= size) ? -1 : 0;
    }
    save = bw_save_dir();
    if (save == NULL || save[0] == '\0') {
        return -1;
    }
    n = strlen(save);
    while (n > 0 && (save[n - 1] == '/' || save[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && save[n - 1] != '/' && save[n - 1] != '\\') {
        n--;
    }
    if (n == 0) {
        return -1;
    }
    k = snprintf(out, size, "%.*smods", (int)n, save);
    return (k < 0 || (size_t)k >= size) ? -1 : 0;
}

typedef struct BwModMenuInstalled {
    const char *folder;
    u32         found;
} BwModMenuInstalled;

/* Does `path` end with `.bw` (any case)? */
static int is_bw_file(const char *path)
{
    size_t n = strlen(path);

    return n > 3 && path[n - 3] == '.' &&
           (path[n - 2] == 'b' || path[n - 2] == 'B') &&
           (path[n - 1] == 'w' || path[n - 1] == 'W');
}

/*
 * One entry of the installed folder: a folder with a manifest, or a `.bw`
 * package (unpacked to the cache, whose folder is the root the file layer
 * holds, sdk/vfs.c).  A mod the model does not hold yet joins it, on: a
 * mod put in the folder is meant to run.  The player's record keeps the
 * switch from then on.
 */
static int installed_entry(void *ctx, const char *name)
{
    BwModMenuInstalled *scan = (BwModMenuInstalled *)ctx;
    char path[BW_MOD_MENU_PATH_MAX];
    char root[BW_MOD_MENU_ROOT_MAX];
    BwModMenuMod *m;
    int n;

    if (name[0] == '.') {
        return 0;
    }
    n = snprintf(path, sizeof path, "%s/%s", scan->folder, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        return 0;
    }
    if (bw_plat_path_kind(path) == BW_PATH_DIR) {
        char manifest[BW_MOD_MENU_PATH_MAX];

        n = snprintf(manifest, sizeof manifest, "%s/eidos_mod.txt", path);
        if (n < 0 || (size_t)n >= sizeof manifest ||
            bw_plat_path_kind(manifest) != BW_PATH_FILE) {
            return 0;       /* a folder without a manifest is not a mod */
        }
        copy_str(root, sizeof root, path);
    } else if (is_bw_file(path)) {
        char err[160];

        err[0] = '\0';
        if (bw_mod_bw_cache_path(path, root, sizeof root) != 0 ||
            bw_mod_bw_unpack(path, root, err, sizeof err) != 0) {
            report(name, "the package could not be read: %s",
                   err[0] != '\0' ? err : "no cache folder");
            return 0;
        }
    } else {
        return 0;
    }
    scan->found++;
    if (model_find_root(root) != NULL) {
        return 0;
    }
    m = model_add();
    if (m == NULL) {
        return 1;           /* the menu is full: stop */
    }
    copy_str(m->root, sizeof m->root, root);
    copy_str(m->name, sizeof m->name, name);
    m->running = 0;
    m->enabled = 1;
    m->present = 1;
    m->player_pos = -1;
    return 0;
}

static void scan_installed(void)
{
    char folder[BW_MOD_MENU_PATH_MAX];
    BwModMenuInstalled scan;

    g_installed_found = 0;
    if (bw_mod_menu_installed_folder(folder, sizeof folder) != 0 ||
        bw_plat_path_kind(folder) != BW_PATH_DIR) {
        return;
    }
    scan.folder = folder;
    scan.found = 0;
    (void)bw_plat_list_dir(folder, installed_entry, &scan);
    g_installed_found = scan.found;
}

static void model_build(void)
{
    BwModMenuRow rows[BW_MOD_MENU_MAX];
    u32 row_count;
    u32 list_count;
    u32 i;

    model_free();
    list_count = bw_mod_count();
    for (i = 0; i < list_count; i++) {
        const char *root = bw_mod_root_at(i);
        BwModMenuMod *m;

        if (root == NULL || root[0] == '\0' || model_find_root(root) != NULL) {
            continue;
        }
        m = model_add();
        if (m == NULL) {
            break;
        }
        copy_str(m->root, sizeof m->root, root);
        copy_str(m->name, sizeof m->name, bw_mod_name_at(i));
        m->order = bw_mod_order_at(i);
        m->player_pos = -1;
        m->running = 1;
        m->enabled = 1;
        m->present = (bw_plat_path_kind(root) == BW_PATH_DIR);
    }
    /* The player's record: the mods the player knows about, their switches
     * and the place the player gave them in the list.  A mod the port's
     * list holds and the record does not name keeps the switch the port
     * gave it. */
    row_count = read_record(rows, BW_MOD_MENU_MAX);
    g_have_record = (row_count > 0);
    for (i = 0; i < row_count; i++) {
        BwModMenuMod *m;

        if (rows[i].root[0] == '\0') {
            continue;
        }
        m = model_find_root(rows[i].root);
        if (m == NULL) {
            m = model_add();
            if (m == NULL) {
                break;
            }
            copy_str(m->root, sizeof m->root, rows[i].root);
            m->running = 0;
            m->present = (bw_plat_path_kind(rows[i].root) == BW_PATH_DIR);
            m->player_pos = -1;
        }
        m->enabled = rows[i].enabled;
        m->player_pos = rows[i].order;
        if (m->player_pos >= 0) {
            g_player_order = 1;
        }
    }
    /* The installed mods folder: the mods the player put there. */
    scan_installed();
    /* Every mod's own manifest: the name, the namespace, the settings and
     * the order key come from it. */
    for (i = 0; i < g_count; i++) {
        read_manifest(&g_mods[i]);
        if (g_mods[i].present == 0) {
            g_mods[i].present =
                (bw_plat_path_kind(g_mods[i].root) == BW_PATH_DIR);
        }
        {
            /* (made for the other game: it stays in the list, named so,
             * and is not handed to the port, which would refuse it) */
            const int games = g_mods[i].present ? bw_mod_games_at(g_mods[i].root) : BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE;
            const int have = bw_mod_game_loaded();

            g_mods[i].needs_game = games == 0 ? -1 : have != 0 && (games & have) == 0 ? games : 0;
        }
        /* (made for a newer eidos: the same, named so) */
        g_mods[i].needs_eidos[0] = '\0';
        if (g_mods[i].present) {
            (void)bw_mod_needs_eidos_at(g_mods[i].root, g_mods[i].needs_eidos, sizeof g_mods[i].needs_eidos);
        }
    }
    model_sort();
    for (i = 0; i < g_count; i++) {
        g_mods[i].base_pos = i;
        if (!unavailable(&g_mods[i])) {
            read_settings(&g_mods[i]);      /* (a mod for the other game offers no options and no modes) */
        }
    }
    g_player_order_base = g_player_order;
    g_built = 1;
}

/* The model, built when it is not. */
static void model_ready(void)
{
    if (!g_built) {
        model_build();
    }
}

/*
 * Move the row at `from` to `to`, the rows between them shifting one
 * place.  The rows move as they are, so a row's own settings (the mod's
 * options) travel with it and no mod's state namespace moves: the
 * namespace is the mod's own, not its place in the list.
 */
static void model_move(u32 from, u32 to)
{
    BwModMenuMod tmp;

    if (from == to || from >= g_count || to >= g_count) {
        return;
    }
    tmp = g_mods[from];
    if (from < to) {
        memmove(&g_mods[from], &g_mods[from + 1],
                (to - from) * sizeof(g_mods[0]));
    } else {
        memmove(&g_mods[to + 1], &g_mods[to],
                (from - to) * sizeof(g_mods[0]));
    }
    g_mods[to] = tmp;
}

/* 1 when the port's list holds the mods that are on, in the order the
 * model holds them. */
static int model_list_matches(void)
{
    u32 count = bw_mod_count();
    u32 n = 0;
    u32 i;

    for (i = 0; i < g_count; i++) {
        const char *root;

        if (!g_mods[i].enabled || !g_mods[i].present || unavailable(&g_mods[i])) {
            continue;
        }
        if (n >= count) {
            return 0;
        }
        root = bw_mod_root_at(n);
        if (root == NULL || strcmp(root, g_mods[i].root) != 0) {
            return 0;
        }
        n++;
    }
    return n == count;
}

/*
 * The roots in force: the mods that are on, in the order the list holds
 * them.
 *
 * The player's own order is handed to the file layer as it is
 * (bw_mod_set_order), so the loader loads the mods in the order the
 * player chose.  With no order of the player's the manifest's `order` key
 * decides it again, the way it always did: the roots are added one at a
 * time and the file layer sorts them in.
 */
static int model_rebuild_roots(void)
{
    const char *roots[BW_MOD_MENU_MAX];
    u32 n = 0;
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (!g_mods[i].enabled || !g_mods[i].present || unavailable(&g_mods[i])) {
            continue;   /* off, its folder is gone, or it is made for the other game */
        }
        roots[n++] = g_mods[i].root;
    }
    if (!g_player_order) {
        bw_clear_mods();
        for (i = 0; i < g_count; i++) {
            if (!g_mods[i].enabled || !g_mods[i].present || unavailable(&g_mods[i])) {
                continue;
            }
            if (bw_add_mod_root(g_mods[i].root) != 0) {
                report(g_mods[i].name, "the mod's folder could not be added "
                       "to the list");
                return -1;
            }
        }
        return 0;
    }
    if (bw_mod_set_order(roots, n) != 0) {
        report("the mod list", "the list could not be built");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The model's own calls                                                */
/* ------------------------------------------------------------------ */

void bw_mod_menu_reset(void)
{
    model_free();
    g_namespace[0] = '\0';
}

void bw_mod_menu_open(void)
{
    model_build();
}

/*
 * Reload: read the installed mods folder and every mod again, and run the
 * whole apply even when no switch changed, so a mod whose files changed
 * is stopped and loaded again and the cached merged archives are dropped.
 * Switches the player changed and did not apply yet are dropped: the
 * player's record is what stands.
 */
int bw_mod_menu_reload(void)
{
    model_build();
    g_force_apply = 1;
    return bw_mod_menu_apply();
}

/* Test switch EIDOS_MOD_MODE=<id>: the first new game of the run is in
 * that mode, as if the Multiplayer screen's Start had chosen it
 * (docs/test-switches.md). */
void bw_mod_menu_mode_from_env(void)
{
    const char *want = getenv("EIDOS_MOD_MODE");
    u32 i, k;

    if (want != NULL && want[0] != '\0') {
        /* EIDOS_MOD_CODE: what the Join box would hand a mode that joins (a
         * world's code, or "ap <host:port> <slot> <password>") */
        const char *code = getenv("EIDOS_MOD_CODE");

        if (bw_mod_menu_find_mode(want, &i, &k) && bw_mod_menu_mode_begin_join(i, k, code) == 0) {
            fprintf(stderr, "eidos: EIDOS_MOD_MODE: the first new game is in mode '%s' (%s)\n", want,
                    g_mods[i].name);
        } else {
            fprintf(stderr, "eidos: EIDOS_MOD_MODE: no mod offers the mode '%s'\n", want);
        }
    }
}

void bw_mod_menu_start(void)
{
    u32 i;

    model_build();
    if (!g_have_record && g_installed_found == 0) {
        /* No record of the player's own: the port's list stands as it is
         * (the environment's BW_MOD_ROOT, or what an explicit call set),
         * and with no mod root there is no state read and the state
         * folder is untouched (docs/mod-state.md, section 2, constraint
         * 4). */
        return;
    }
    /* The record holds the player's own switches: the mods the player
     * turned off are off, the ones the player turned on are on.  A mod
     * the record does not mention keeps the state the port's list gave
     * it.  The model holds them already: it reads the record when it is
     * built.
     *
     * The list in force is the player's own: the mods that are on, in the
     * order the player chose.  The rebuild drops the cached merged
     * archives with it, and the loader loads the mods in that order,
     * because it walks the file layer's list in order. */
    if (!model_list_matches()) {
        (void)model_rebuild_roots();
    }
    for (i = 0; i < g_count; i++) {
        g_mods[i].running = g_mods[i].enabled && !unavailable(&g_mods[i]);
    }
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_count(void)
{
    model_ready();
    return g_count;
}

const char *bw_mod_menu_name_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].name : "";
}

const char *bw_mod_menu_namespace_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].ns : "";
}

const char *bw_mod_menu_settings_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].settings : "";
}

const char *bw_mod_menu_root_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].root : "";
}

int bw_mod_menu_order_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].order : 0;
}

int bw_mod_menu_enabled_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].enabled : 0;
}

int bw_mod_menu_running_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].running : 0;
}

int bw_mod_menu_needs_game_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].needs_game : 0;
}

const char *bw_mod_menu_needs_eidos_at(u32 index)
{
    model_ready();
    return index < g_count && g_mods[index].needs_eidos[0] != '\0' ? g_mods[index].needs_eidos : NULL;
}

int bw_mod_menu_unavailable_at(u32 index)
{
    model_ready();
    return index < g_count && unavailable(&g_mods[index]);
}

const char *bw_mod_menu_about_at(u32 index, int what)
{
    model_ready();
    if (index >= g_count) {
        return "";
    }
    return what == 0 ? g_mods[index].about : what == 1 ? g_mods[index].author : what == 2 ? g_mods[index].version : "";
}

static int g_manager;

void bw_mod_menu_set_manager(int on)
{
    g_manager = on != 0;
}

int bw_mod_menu_manager(void)
{
    return g_manager;
}

int bw_mod_menu_present_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].present : 0;
}

int bw_mod_menu_has_code_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].has_code : 0;
}

/* ------------------------------------------------------------------ */
/* The switch                                                          */
/* ------------------------------------------------------------------ */

int bw_mod_menu_set_enabled(u32 index, int on)
{
    model_ready();
    if (index >= g_count) {
        return -1;
    }
    g_mods[index].enabled = on ? 1 : 0;
    return 0;
}

int bw_mod_menu_dirty(void)
{
    u32 i;

    model_ready();
    for (i = 0; i < g_count; i++) {
        if ((g_mods[i].enabled && !unavailable(&g_mods[i])) != g_mods[i].running) {
            return 1;
        }
        if (g_mods[i].base_pos != i) {
            return 1;          /* the player moved a mod */
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The order                                                           */
/* ------------------------------------------------------------------ */

/*
 * Move the mod at `index` one place towards the front of the list.  0 on
 * success, -1 when there is no such mod or it is already at the front.
 */
int bw_mod_menu_move_up(u32 index)
{
    model_ready();
    if (index >= g_count || index == 0) {
        return -1;
    }
    model_move(index, index - 1);
    g_player_order = 1;
    return 0;
}

/*
 * Move the mod at `index` one place towards the back of the list.  0 on
 * success, -1 when there is no such mod or it is already at the back.
 */
int bw_mod_menu_move_down(u32 index)
{
    model_ready();
    if (index >= g_count || index + 1 >= g_count) {
        return -1;
    }
    model_move(index, index + 1);
    g_player_order = 1;
    return 0;
}

/*
 * Move the mod at `index` to `to`, the place it takes in the list: 0 is
 * the front.  A `to` past the end of the list is the last place.  0 on
 * success, -1 when there is no such mod.
 */
int bw_mod_menu_move_to(u32 index, u32 to)
{
    model_ready();
    if (index >= g_count) {
        return -1;
    }
    if (to >= g_count) {
        to = g_count - 1;
    }
    if (to == index) {
        return 0;
    }
    model_move(index, to);
    g_player_order = 1;
    return 0;
}

void bw_mod_menu_revert(void)
{
    u32 i;
    u32 j;

    model_ready();
    for (i = 0; i < g_count; i++) {
        if (!unavailable(&g_mods[i])) {    /* (a mod for the other game keeps the switch the player gave it) */
            g_mods[i].enabled = g_mods[i].running;
        }
    }
    /* The order goes back to the one the list had when the menu was
     * opened: every row to the place it was built at. */
    for (i = 1; i < g_count; i++) {
        BwModMenuMod tmp = g_mods[i];

        for (j = i; j > 0 && g_mods[j - 1].base_pos > tmp.base_pos; j--) {
            g_mods[j] = g_mods[j - 1];
        }
        g_mods[j] = tmp;
    }
    g_player_order = g_player_order_base;
}

int bw_mod_menu_apply(void)
{
    u32 i;

    model_ready();
    if (!g_force_apply && !bw_mod_menu_dirty()) {
        return 0;               /* nothing changed: the mods keep running */
    }
    g_force_apply = 0;

    /*
     * 1. The mods' state is flushed before anything is stopped
     *    (docs/mod-state.md, section 2, constraint 2).  The saved-data
     *    layer holds a payload while a save is in progress and writes it
     *    when the save ends; the flush is what ends it, with the checksum
     *    of the save in force.
     */
    bw_mod_savedata_save_begin();
    (void)bw_mod_savedata_save_done(1, bw_mod_savedata_save_check());

    /*
     * 2. The mods that are running are stopped, the last loaded first.
     *    The loader reverts every (region, address) pair the mod
     *    installed before it unloads the library, because the registry
     *    holds a pointer into the library (sdk/mod_loader.c).
     */
    bw_mod_loader_stop_all();

    /*
     * 3. The event layer is built again: a handler of a stopped mod
     *    still points into the library that was unloaded, so the port's
     *    own rows are bound again and the rules are dropped (the loader
     *    binds them again when it loads the mods that are wanted).
     */
    bw_mod_event_reset();
    bw_mod_event_bind_all();
    bw_mod_rule_reset();
    bw_mod_table_reset();

    /*
     * 4. The roots are built again from the switches, in the order the
     *    player chose.  Every change drops the cached merged archives, so
     *    the next open of an archive sees the new list (sdk/vfs.c).  The
     *    order is the player's own, so it is handed to the file layer as
     *    it is (bw_mod_set_order) and the loader loads the mods in it.
     */
    (void)model_rebuild_roots();

    /*
     * 5. The mods that are wanted are loaded again, so the load point
     *    reads their state: a state file that stayed on the disk is read
     *    again, and a mod that was off keeps its file.
     */
    (void)bw_mod_loader_load_all();

    /*
     * 6. The player's list is written: the switches and the order are the
     *    player's own and outlive the port.
     */
    (void)write_record();

    for (i = 0; i < g_count; i++) {
        g_mods[i].running = g_mods[i].enabled && !unavailable(&g_mods[i]);
        /* The list is the port's list now, so the order the player chose
         * is the order the list holds: there is nothing left to apply. */
        g_mods[i].base_pos = i;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The clear action                                                    */
/* ------------------------------------------------------------------ */

int bw_mod_menu_clear(u32 index)
{
    char saved[BW_MOD_MENU_NS_MAX];
    int rc;

    model_ready();
    if (index >= g_count) {
        return -1;
    }
    if (g_mods[index].ns[0] == '\0') {
        return -1;
    }
    /*
     * The saved-data layer owns the mod's own file.  The layer deletes the
     * state file and leaves the folder, which is the rule of
     * docs/mod-state.md, section 1: the port does not delete a mod's
     * folder when the mod is removed, so a re-enable reads the file again.
     */
    copy_str(saved, sizeof saved, g_namespace);
    bw_mod_savedata_set_namespace(g_mods[index].ns);
    rc = bw_mod_savedata_clear();
    bw_mod_savedata_set_namespace(saved[0] != '\0' ? saved : NULL);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The orphan report                                                   */
/* ------------------------------------------------------------------ */

/* One entry of the save directory's mods folder. */
typedef struct BwModMenuScan {
    const char *path;
    u32         count;
} BwModMenuScan;

static int scan_entry(void *ctx, const char *name)
{
    BwModMenuScan *scan = (BwModMenuScan *)ctx;
    char path[BW_MOD_MENU_PATH_MAX];
    int n;

    if (scan->count >= BW_MOD_MENU_MAX) {
        return 0;
    }
    n = snprintf(path, sizeof path, "%s/%s", scan->path, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        return 0;
    }
    if (bw_plat_path_kind(path) != BW_PATH_DIR) {
        return 0;               /* only the folders are state */
    }
    if (model_find_ns(name) != NULL) {
        return 0;               /* a mod claims this folder */
    }
    copy_str(g_orphans[scan->count], sizeof g_orphans[0], name);
    scan->count++;
    return 0;
}

/*
 * The folders in <save dir>/mods/ that no mod claims: a mod that was
 * removed and whose folder was left behind.  The port reports them at the
 * menu (docs/mod-state.md, section 1, "Removal").
 *
 * The folder is only read, never made: with no mods the state folder is
 * untouched.
 */
static void scan_orphans(void)
{
    char path[BW_MOD_MENU_PATH_MAX];
    BwModMenuScan scan;

    g_orphan_count = 0;
    if (mods_folder_path(path, sizeof path) != 0) {
        return;
    }
    scan.path = path;
    scan.count = 0;
    (void)bw_plat_list_dir(path, scan_entry, &scan);
    g_orphan_count = scan.count;
}

u32 bw_mod_menu_orphan_count(void)
{
    model_ready();
    scan_orphans();
    return g_orphan_count;
}

const char *bw_mod_menu_orphan_at(u32 index)
{
    model_ready();
    scan_orphans();
    return index < g_orphan_count ? g_orphans[index] : NULL;
}

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_option_count(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].option_count : 0u;
}

const char *bw_mod_menu_option_key(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return "";
    }
    return g_mods[index].options[option].key;
}

const char *bw_mod_menu_option_label(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return "";
    }
    return g_mods[index].options[option].label;
}

int bw_mod_menu_option_kind(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return BW_MOD_SETTING_BOOL;
    }
    return g_mods[index].options[option].kind;
}

int bw_mod_menu_option_value(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].value;
}

int bw_mod_menu_option_default(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].def;
}

int bw_mod_menu_option_set(u32 index, u32 option, int value)
{
    BwModMenuMod *m;
    BwModMenuOption *o;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return -1;
    }
    m = &g_mods[index];
    o = &m->options[option];
    if (o->kind == BW_MOD_SETTING_BOOL) {
        if (value != 0 && value != 1) {
            return -1;
        }
    } else if (o->kind == BW_MOD_SETTING_INT) {
        if (value < o->min || value > o->max) {
            return -1;
        }
    } else {
        if (value < 0 || value >= o->choice_count) {
            return -1;
        }
    }
    o->value = value;
    /* The choice is kept beside the mod's saved data. */
    (void)write_choices(m);
    return 0;
}

int bw_mod_menu_option_min(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].min;
}

int bw_mod_menu_option_max(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].max;
}

int bw_mod_menu_option_choice_count(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].choice_count;
}

const char *bw_mod_menu_option_choice(u32 index, u32 option, int choice)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return NULL;
    }
    if (choice < 0 || choice >= g_mods[index].options[option].choice_count) {
        return NULL;
    }
    return g_mods[index].options[option].choices[choice];
}

const char *bw_mod_menu_option_text(u32 index, u32 option, char *buf,
                                     size_t size)
{
    BwModMenuOption *o;

    model_ready();
    if (buf == NULL || size == 0) {
        return "";
    }
    buf[0] = '\0';
    if (index >= g_count || option >= g_mods[index].option_count) {
        return buf;
    }
    o = &g_mods[index].options[option];
    if (o->kind == BW_MOD_SETTING_TEXT || o->kind == BW_MOD_SETTING_SECRET || o->kind == BW_MOD_SETTING_SHOW) {
        snprintf(buf, size, "%s", o->text);
    } else if (o->kind == BW_MOD_SETTING_BUTTON) {
        buf[0] = '\0';
    } else if (o->kind == BW_MOD_SETTING_BOOL) {
        snprintf(buf, size, "%s", bw_mod_menu_rule_value(index, option) ? "on" : "off");
    } else if (o->kind == BW_MOD_SETTING_INT) {
        snprintf(buf, size, "%d", bw_mod_menu_rule_value(index, option));
    } else if (o->choice_count > 0) {
        int choice = bw_mod_menu_rule_value(index, option);

        if (choice < 0 || choice >= o->choice_count) {
            choice = 0;
        }
        snprintf(buf, size, "%s", o->choices[choice]);
    }
    return buf;
}

int bw_mod_menu_option_set_text(u32 index, u32 option, const char *text)
{
    BwModMenuOption *o;
    const char *s;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count || text == NULL) {
        return -1;
    }
    o = &g_mods[index].options[option];
    if ((o->kind != BW_MOD_SETTING_TEXT && o->kind != BW_MOD_SETTING_SECRET) || (int)strlen(text) > o->max) {
        return -1;
    }
    for (s = text; *s != '\0'; s++) {
        if ((*s == '"' && o->kind == BW_MOD_SETTING_TEXT) || *s == '\n' || *s == '\r') {
            return -1;
        }
    }
    copy_str(o->text, sizeof o->text, text);
    if (o->kind == BW_MOD_SETTING_TEXT) {
        (void)write_choices(&g_mods[index]);
    }
    return 0;
}

static int same_word(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

/* 0 when the option's `?key=value` does not hold now on mode `mode`'s
 * screen: the screen leaves the row out.  The condition holds when the
 * option it names is not asked on that screen (the rules a solo game and a
 * world's maker share, with no World question in solo), and when the mod
 * declares no such option (the mod's own mistake hides nothing). */
static int option_shown(const BwModMenuMod *m, const BwModMenuOption *o, u32 mode)
{
    const BwModMenuOption *c = NULL;
    char now[BW_MOD_MENU_TEXT_MAX];
    u32 i;

    if (o->when_key[0] == '\0') {
        return 1;
    }
    for (i = 0; i < m->option_count; i++) {
        if (strcmp(m->options[i].key, o->when_key) == 0) {
            c = &m->options[i];
            break;
        }
    }
    if (c == NULL || (mode < 32u && ((c->modes >> mode) & 1u) == 0)) {
        return 1;
    }
    switch (c->kind) {
    case BW_MOD_SETTING_BOOL:
        copy_str(now, sizeof now, c->value ? "on" : "off");
        break;
    case BW_MOD_SETTING_CHOICE:
        copy_str(now, sizeof now, c->value >= 0 && c->value < c->choice_count
                                  ? c->choices[c->value] : "");
        break;
    case BW_MOD_SETTING_TEXT:
    case BW_MOD_SETTING_SECRET:
    case BW_MOD_SETTING_SHOW:
        copy_str(now, sizeof now, c->text);
        break;
    case BW_MOD_SETTING_BUTTON:
        now[0] = '\0';
        break;
    default:
        snprintf(now, sizeof now, "%d", c->value);
        break;
    }
    return same_word(now, o->when_val) != o->when_not;
}

int bw_mod_menu_option_in_mode(u32 index, u32 option, u32 mode)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count || mode >= 32u) {
        return 0;
    }
    return ((g_mods[index].options[option].modes >> mode) & 1u) &&
           option_shown(&g_mods[index], &g_mods[index].options[option], mode);
}

/* ------------------------------------------------------------------ */
/* The modes a mod offers                                              */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_mode_count(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].mode_count : 0u;
}

static const BwModMenuMode *mode_at(u32 index, u32 mode)
{
    model_ready();
    if (index >= g_count || mode >= g_mods[index].mode_count) {
        return NULL;
    }
    return &g_mods[index].modes[mode];
}

const char *bw_mod_menu_mode_id(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL ? md->id : "";
}

const char *bw_mod_menu_mode_title(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL ? md->title : "";
}

const char *bw_mod_menu_mode_line(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL ? md->line : "";
}

int bw_mod_menu_mode_off(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md == NULL || md->off;
}

int bw_mod_menu_find_mode(const char *id, u32 *index, u32 *mode)
{
    u32 i, k;

    model_ready();
    for (i = 0; id != NULL && i < g_count; i++) {
        for (k = 0; k < g_mods[i].mode_count; k++) {
            if (strcmp(g_mods[i].modes[k].id, id) == 0) {
                if (index != NULL) *index = i;
                if (mode != NULL) *mode = k;
                return 1;
            }
        }
    }
    return 0;
}

/* The mode the next new game is started in: the mod's namespace and the
 * mode's id.  Set by the screen's Start, read by the mod during game.new
 * (mode_get), and dropped when game.new is done. */
static char g_mode_ns[BW_MOD_MENU_NS_MAX];
static char g_mode_id[BW_MOD_MENU_MODE_ID];
static char g_mode_code[BW_MOD_MENU_CODE_MAX];  /* the world the game joins, "" for none */
static int  g_mode_used;           /* a game.new has had it */

/* sdk/save_games.h (weak: the model's tests link without it) */
extern void bw_games_pending(int kind, int role, const char *ns, const char *mode,
                             const char *code) __attribute__((weak));
extern void bw_games_pending_clear(void) __attribute__((weak));

int bw_mod_menu_mode_begin(u32 index, u32 mode)
{
    return bw_mod_menu_mode_begin_join(index, mode, NULL);
}

int bw_mod_menu_mode_begin_join(u32 index, u32 mode, const char *code)
{
    const BwModMenuMode *md = mode_at(index, mode);

    if (md == NULL || md->off || g_mods[index].ns[0] == '\0' ||
        (code != NULL && code[0] != '\0' && !md->joins)) {
        return -1;
    }
    copy_str(g_mode_ns, sizeof g_mode_ns, g_mods[index].ns);
    copy_str(g_mode_id, sizeof g_mode_id, md->id);
    copy_str(g_mode_code, sizeof g_mode_code, code != NULL ? code : "");
    g_mode_used = 0;
    if (bw_games_pending != NULL) {
        /* the save goes under Your games: a world made or joined, or a game of
         * one's own */
        char rec[24];

        /* an Archipelago room's line ("ap <host:port> <slot> <password>")
         * is kept as AP and the port */
        if (strncmp(g_mode_code, "ap ", 3) == 0) {
            const char *colon = strchr(g_mode_code + 3, ':'), *sp;

            sp = colon != NULL ? strchr(colon, ' ') : NULL;
            snprintf(rec, sizeof rec, "AP%.*s", colon != NULL && sp != NULL ? (int)(sp - colon - 1) : 0,
                     colon != NULL ? colon + 1 : "");
        } else {
            copy_str(rec, sizeof rec, g_mode_code);
        }
        bw_games_pending(md->game[0] != '\0' ? 2 : md->joins ? 1 : 0, g_mode_code[0] != '\0' ? 1 : 0,
                         g_mods[index].ns, md->id, rec);
    }
    return 0;
}

int bw_mod_menu_mode_joins(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL && md->joins && !md->off;
}

void bw_mod_menu_mode_end(void)
{
    if (getenv("EIDOS_MP_UI_LOG") != NULL && g_mode_id[0] != '\0') {
        fprintf(stderr, "eidos: mode: '%s' of '%s' ends\n", g_mode_id, g_mode_ns);
    }
    g_mode_ns[0] = '\0';
    g_mode_id[0] = '\0';
    g_mode_code[0] = '\0';
    g_mode_used = 0;
    if (bw_games_pending_clear != NULL) {
        bw_games_pending_clear();   /* its record was written at its first save, or never */
    }
}

void bw_mod_menu_mode_new_game(void)
{
    if (getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: mode: a new game; the mode is '%s' of '%s', %s\n", g_mode_id, g_mode_ns,
                g_mode_used ? "used by the game before: ended" : "this game's");
    }
    if (g_mode_used) {
        bw_mod_menu_mode_end();     /* the mode was the game before this one's */
    } else if (g_mode_id[0] != '\0') {
        g_mode_used = 1;
    }
}

/* ------------------------------------------------------------------ */
/* The co-op lobby's rules (`@lobby` options)                          */
/* ------------------------------------------------------------------ */

/* sdk/mp.h: the session's shared table (the host's copy is the real one).
 * Weak: the model's own tests link without the multiplayer layer. */
extern int bw_mp_shared_get(const char *ns, const char *key, s32 *out) __attribute__((weak));
extern int bw_mp_shared_set(const char *ns, const char *key, s32 value) __attribute__((weak));
extern int bw_mp_is_host(void) __attribute__((weak));

static int rule_key(char *out, size_t size, const char *key)
{
    int n = snprintf(out, size, "rule.%s", key);

    return n > 0 && (size_t)n < size ? 0 : -1;
}

static int value_ok(const BwModMenuOption *o, int v)
{
    if (o->kind == BW_MOD_SETTING_BOOL) return v == 0 || v == 1;
    if (o->kind == BW_MOD_SETTING_INT) return v >= o->min && v <= o->max;
    if (o->kind == BW_MOD_SETTING_CHOICE) return v >= 0 && v < o->choice_count;
    return 0;
}

/* The host's value of a lobby rule while a session is open: 1 and *out,
 * or 0 (no session, not a rule, or the host has not sent it). */
static int lobby_value(const BwModMenuMod *m, const BwModMenuOption *o, int *out)
{
    char k[BW_MOD_MENU_KEY_MAX + 8];
    s32 v;

    if (!(o->modes & BW_MOD_MENU_LOBBY_BIT) || o->kind >= BW_MOD_SETTING_TEXT ||
        bw_mp_shared_get == NULL || m->ns[0] == '\0' || rule_key(k, sizeof k, o->key) != 0) {
        return 0;
    }
    if (bw_mp_shared_get(m->ns, k, &v) != 0 || !value_ok(o, (int)v)) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

int bw_mod_menu_option_in_lobby(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return (g_mods[index].options[option].modes & BW_MOD_MENU_LOBBY_BIT) != 0 &&
           g_mods[index].options[option].kind < BW_MOD_SETTING_TEXT;
}

/* ------------------------------------------------------------------ */
/* The mods' own Settings tabs and rows (version 7)                     */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_tab_count(u32 index)
{
    model_ready();
    return index < g_count && g_mods[index].tabs != NULL ? g_mods[index].tab_count : 0u;
}

const char *bw_mod_menu_tab_title(u32 index, u32 tab)
{
    model_ready();
    if (index >= g_count || g_mods[index].tabs == NULL || tab >= g_mods[index].tab_count) {
        return "";
    }
    return g_mods[index].tabs[tab];
}

int bw_mod_menu_option_tab(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return -1;
    }
    return g_mods[index].options[option].tab;
}

int bw_mod_menu_option_word(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].word;
}

int bw_mod_menu_option_shown(u32 index, u32 option)
{
    const BwModMenuOption *o;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    o = &g_mods[index].options[option];
    if (o->kind == BW_MOD_SETTING_SHOW && o->text[0] == '\0') {
        return 0;                       /* a mod's row with nothing to say */
    }
    return option_shown(&g_mods[index], o, 32u);
}

/* (weak: a build without the event layer, the menu's own test, still
 * links; there the press reaches no mod) */
#pragma weak bw_mod_event_fire

int bw_mod_menu_press(u32 index, u32 option)
{
    BwSettingButton p;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count ||
        g_mods[index].options[option].kind != BW_MOD_SETTING_BUTTON) {
        return -1;
    }
    memset(&p, 0, sizeof p);
    copy_str((char *)p.key, sizeof p.key, g_mods[index].options[option].key);
    if (bw_mod_event_fire != NULL) {
        (void)bw_mod_event_fire(BW_EVENT_SETTING_BUTTON, (BwEventPayload *)&p);
    }
    return 0;
}

void bw_mod_menu_clear_secrets(void)
{
    u32 i, o;

    for (i = 0; i < g_count; i++) {
        for (o = 0; o < g_mods[i].option_count; o++) {
            BwModMenuOption *op = &g_mods[i].options[o];

            if (op->kind == BW_MOD_SETTING_SECRET) {
                memset(op->text, 0, sizeof op->text);
            }
        }
    }
}

int bw_mod_menu_rule_value(u32 index, u32 option)
{
    int v;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    if (lobby_value(&g_mods[index], &g_mods[index].options[option], &v)) {
        return v;
    }
    return g_mods[index].options[option].value;
}

int bw_mod_menu_rules_locked(void)
{
    s32 v;

    /* in a session, a joiner plays by the host's rules: its own stay as
     * they are and cannot be changed from the lobby */
    return bw_mp_shared_get != NULL && bw_mp_is_host != NULL &&
           bw_mp_shared_get("", "", &v) >= 0 && !bw_mp_is_host();
}

void bw_mod_menu_rules_push(void)
{
    static u32 sent_sum = 0xFFFFFFFFu;
    u32 i, o, sum = 2166136261u;
    char k[BW_MOD_MENU_KEY_MAX + 8];

    if (bw_mp_shared_set == NULL || bw_mp_is_host == NULL || !bw_mp_is_host()) {
        return;
    }
    model_ready();
    /* only when a value changed (a frame's call is cheap) */
    for (i = 0; i < g_count; i++) {
        for (o = 0; o < g_mods[i].option_count; o++) {
            if (g_mods[i].enabled && bw_mod_menu_option_in_lobby(i, o)) {
                sum = (sum ^ (u32)g_mods[i].options[o].value ^ (i << 8) ^ (o << 16)) * 16777619u;
            }
        }
    }
    if (sum == sent_sum) {
        return;
    }
    for (i = 0; i < g_count; i++) {
        for (o = 0; o < g_mods[i].option_count; o++) {
            if (g_mods[i].enabled && bw_mod_menu_option_in_lobby(i, o) &&
                rule_key(k, sizeof k, g_mods[i].options[o].key) == 0) {
                if (bw_mp_shared_set(g_mods[i].ns, k, (s32)g_mods[i].options[o].value) != 0) {
                    return;         /* no session yet: sent at the next call */
                }
            }
        }
    }
    sent_sum = sum;
}

/* The calling mod's namespace (see bw_mod_menu_setting_get). */
static const char *calling_ns(void)
{
    const char *ns = g_namespace;

    if (ns[0] == '\0') {
        ns = bw_mod_savedata_namespace();
    }
    return ns != NULL ? ns : "";
}

/* The services table's mode_get: the mode the player chose for this new
 * game, if it is the calling mod's; "" otherwise. */
int bw_mod_menu_mode_get(char *out, u32 size)
{
    const char *ns = calling_ns();
    const char *id = (ns[0] != '\0' && strcmp(ns, g_mode_ns) == 0) ? g_mode_id : "";

    if (getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: mode: mode_get by '%s': the mode is '%s' of '%s', answer '%s' (out %p, size %u)\n", ns, g_mode_id,
                g_mode_ns, id, (void *)out, (unsigned)size);
    }
    if (out == NULL || size == 0) {
        return -1;
    }
    copy_str(out, size, id);
    return (int)strlen(out);
}

int bw_mod_menu_mode_running(void)
{
    return g_mode_id[0] != '\0';
}

/* The services table's mode_code: the world code the player typed in the
 * Multiplayer screen's Join box for this new game, if the mode is the
 * calling mod's; "" for a new world. */
int bw_mod_menu_mode_code(char *out, u32 size)
{
    const char *ns = calling_ns();
    const char *code = (ns[0] != '\0' && strcmp(ns, g_mode_ns) == 0) ? g_mode_code : "";

    if (out == NULL || size == 0) {
        return -1;
    }
    copy_str(out, size, code);
    return (int)strlen(out);
}

int bw_mod_menu_mode_in(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL ? md->in : -1;
}

const char *bw_mod_menu_mode_export(u32 index, u32 mode)
{
    const BwModMenuMode *md = mode_at(index, mode);

    return md != NULL && md->game[0] != '\0' ? md->game : NULL;
}

/* The first mode of an enabled mod that joins: a relay world's (no
 * `export`) or an Archipelago room's (`export`). */
static int find_joining_kind(int room, u32 *index, u32 *mode)
{
    u32 i, k;

    model_ready();
    for (i = 0; i < g_count; i++) {
        if (!g_mods[i].enabled) {
            continue;
        }
        for (k = 0; k < g_mods[i].mode_count; k++) {
            const BwModMenuMode *md = &g_mods[i].modes[k];

            if (md->joins && !md->off && (md->game[0] != '\0') == (room != 0)) {
                if (index != NULL) *index = i;
                if (mode != NULL) *mode = k;
                return 1;
            }
        }
    }
    return 0;
}

int bw_mod_menu_find_joining(u32 *index, u32 *mode)
{
    return find_joining_kind(0, index, mode);
}

int bw_mod_menu_find_room(u32 *index, u32 *mode)
{
    return find_joining_kind(1, index, mode);
}

const char *bw_mod_menu_option_heading(u32 index, u32 option)
{
    model_ready();
    return index < g_count && option < g_mods[index].option_count ? g_mods[index].options[option].heading : "";
}

const char *bw_mod_menu_option_help(u32 index, u32 option)
{
    model_ready();
    return index < g_count && option < g_mods[index].option_count && g_mods[index].options[option].help != NULL
           ? g_mods[index].options[option].help : "";
}

const char *bw_mod_menu_option_yaml(u32 index, u32 option)
{
    model_ready();
    return index < g_count && option < g_mods[index].option_count ? g_mods[index].options[option].yaml : NULL;
}

/* The n-th '|' word of `s` into out ("" past the last). */
static void nth_word(const char *s, int n, char *out, size_t size)
{
    while (n > 0 && s != NULL && *s != '\0') {
        if (*s++ == '|') {
            n--;
        }
    }
    {
        size_t k = 0;

        while (s != NULL && s[k] != '\0' && s[k] != '|' && k + 1 < size) {
            out[k] = s[k];
            k++;
        }
        out[k] = '\0';
    }
}

/* A YAML scalar for a name: quoted when it holds anything but letters,
 * digits, spaces, '_' and '-'. */
static void yaml_quote(char *out, size_t size, const char *s)
{
    size_t k = 0, i;
    int plain = s[0] != '\0';

    for (i = 0; s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];

        if (!(isalnum(ch) || ch == ' ' || ch == '_' || ch == '-' || ch >= 0x80)) {
            plain = 0;
        }
    }
    if (plain) {
        copy_str(out, size, s);
        return;
    }
    if (k + 1 < size) out[k++] = '"';
    for (i = 0; s[i] != '\0' && k + 3 < size; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            out[k++] = '\\';
        }
        out[k++] = s[i];
    }
    if (k + 1 < size) out[k++] = '"';
    out[k] = '\0';
}

int bw_mod_menu_mode_write_yaml(u32 index, u32 mode, const char *name, char version, char *path, u32 size)
{
    const BwModMenuMode *md = mode_at(index, mode);
    const BwModMenuMod *m;
    const char *dir = bw_save_dir();
    char game[BW_MOD_MENU_LABEL_MAX], qname[96], qgame[160], fname[64], dpath[512];
    char *text;
    size_t cap = 16384, n = 0;
    u32 o;
    int rc;

    if (md == NULL || md->game[0] == '\0' || dir == NULL || path == NULL || size == 0) {
        return -1;
    }
    m = &g_mods[index];
    /* {version}: the cartridge's own */
    {
        const char *v = strstr(md->game, "{version}");

        if (v != NULL) {
            snprintf(game, sizeof game, "%.*s%s%s", (int)(v - md->game), md->game,
                     version == 'W' ? "White" : "Black", v + 9);
        } else {
            copy_str(game, sizeof game, md->game);
        }
    }
    yaml_quote(qname, sizeof qname, name != NULL && name[0] != '\0' ? name : "Player");
    yaml_quote(qgame, sizeof qgame, game);
    text = (char *)malloc(cap);
    if (text == NULL) {
        return -1;
    }
    n += (size_t)snprintf(text + n, cap - n,
                          "# Archipelago options for %s, from the eidos Multiplayer screen (%s).\n"
                          "# Send this file to the room's host.\n"
                          "name: %s\ngame: %s\n%s:\n", game, m->name, qname, qgame, qgame);
    for (o = 0; o < m->option_count && n < cap; o++) {
        const BwModMenuOption *op = &m->options[o];
        char key[64], val[128];
        const char *vals;

        if (op->yaml == NULL || !bw_mod_menu_option_in_mode(index, o, mode)) {
            continue;
        }
        vals = op->yaml;
        nth_word(vals, 0, key, sizeof key);
        {
            char *sp = strchr(key, ' ');

            if (sp != NULL) *sp = '\0';
        }
        vals = strchr(op->yaml, ' ');
        while (vals != NULL && *vals == ' ') vals++;
        if (op->kind == BW_MOD_SETTING_INT) {
            snprintf(val, sizeof val, "%d", op->value);
        } else if (op->kind == BW_MOD_SETTING_TEXT) {
            yaml_quote(val, sizeof val, op->text);
        } else {
            nth_word(vals, op->value, val, sizeof val);
            if (val[0] == '\0') {
                copy_str(val, sizeof val, op->kind == BW_MOD_SETTING_BOOL ? (op->value ? "true" : "false")
                                                                           : op->choices[op->value]);
            }
        }
        n += (size_t)snprintf(text + n, cap - n, "  %s: %s\n", key, val);
    }
    if (n >= cap) {
        free(text);
        return -1;
    }
    snprintf(dpath, sizeof dpath, "%s/archipelago", dir);
    (void)bw_plat_mkdirs(dpath);
    /* the file is named after the player: letters and digits only */
    {
        size_t k = 0, i;
        const char *src = name != NULL && name[0] != '\0' ? name : "Player";

        for (i = 0; src[i] != '\0' && k + 1 < sizeof fname - 5; i++) {
            unsigned char ch = (unsigned char)src[i];

            fname[k++] = (char)(isalnum(ch) ? ch : '_');
        }
        fname[k] = '\0';
    }
    rc = snprintf(path, size, "%s/%s.yaml", dpath, fname);
    if (rc < 0 || (u32)rc >= size) {
        free(text);
        return -1;
    }
    rc = bw_save_write_atomic(path, text, (u32)n);
    free(text);
    return rc == 0 ? 0 : -1;
}

/* The services table's setting_text: a text option's value. */
int bw_mod_menu_setting_text(const char *key, char *out, u32 size)
{
    BwModMenuMod *m;
    const char *ns = calling_ns();
    u32 i;

    if (key == NULL || out == NULL || size == 0 || ns[0] == '\0') {
        return -1;
    }
    model_ready();
    m = model_find_ns(ns);
    if (m == NULL) {
        return -1;
    }
    for (i = 0; i < m->option_count; i++) {
        if ((m->options[i].kind == BW_MOD_SETTING_TEXT || m->options[i].kind == BW_MOD_SETTING_SECRET) &&
            strcmp(m->options[i].key, key) == 0) {
            copy_str(out, size, m->options[i].text);
            return (int)strlen(m->options[i].text);
        }
    }
    return -1;
}

/* The calling mod's option `key` of kind `kind`, or NULL. */
static BwModMenuOption *own_option(const char *key, int kind, u32 *index)
{
    const char *ns = calling_ns();
    BwModMenuMod *m;
    u32 i;

    if (key == NULL || ns[0] == '\0') {
        return NULL;
    }
    model_ready();
    m = model_find_ns(ns);
    for (i = 0; m != NULL && i < m->option_count; i++) {
        if (strcmp(m->options[i].key, key) == 0 &&
            (m->options[i].kind == kind || (kind == BW_MOD_SETTING_TEXT && m->options[i].kind == BW_MOD_SETTING_SECRET))) {
            if (index != NULL) {
                *index = (u32)(m - g_mods);
            }
            return &m->options[i];
        }
    }
    return NULL;
}

int bw_mod_menu_setting_show(const char *key, const char *text)
{
    BwModMenuOption *o = own_option(key, BW_MOD_SETTING_SHOW, NULL);
    const char *s;

    if (o == NULL || text == NULL || strlen(text) > BW_MOD_MENU_SHOW_MAX) {
        return -1;
    }
    for (s = text; *s != '\0'; s++) {
        if (*s == '\n' || *s == '\r') {
            return -1;
        }
    }
    copy_str(o->text, sizeof o->text, text);
    return 0;
}

int bw_mod_menu_setting_set_text(const char *key, const char *text)
{
    u32 index = 0;
    BwModMenuOption *o = own_option(key, BW_MOD_SETTING_TEXT, &index);

    if (o == NULL) {
        return -1;
    }
    return bw_mod_menu_option_set_text(index, (u32)(o - g_mods[index].options), text);
}

/* ------------------------------------------------------------------ */
/* The mod that is calling                                             */
/* ------------------------------------------------------------------ */

void bw_mod_menu_set_namespace(const char *ns)
{
    if (ns == NULL || ns[0] == '\0') {
        g_namespace[0] = '\0';
        return;
    }
    copy_str(g_namespace, sizeof g_namespace, ns);
}

const char *bw_mod_menu_namespace(void)
{
    return g_namespace;
}

/*
 * The services table's setting_get (docs/mod-services.md, "Mod settings").
 * The loader binds this one, and the mod that calls it is the one whose
 * namespace is in force.
 */
int bw_mod_menu_setting_get(const char *key)
{
    BwModMenuMod *m;
    const char *ns = g_namespace;
    u32 i;

    /* The loader sets the saved-data layer's namespace around every call
     * into a mod (an event handler, the start, a frame), and not this
     * one; the mod that is calling is the same, so its name answers. */
    if (ns[0] == '\0') {
        ns = bw_mod_savedata_namespace();
    }
    if (key == NULL || ns == NULL || ns[0] == '\0') {
        return -1;
    }
    model_ready();
    m = model_find_ns(ns);
    if (m == NULL) {
        return -1;
    }
    for (i = 0; i < m->option_count; i++) {
        if (strcmp(m->options[i].key, key) == 0) {
            int v;

            /* a lobby rule in a session: the host's value, for everyone */
            if (lobby_value(m, &m->options[i], &v)) {
                return v;
            }
            return m->options[i].value;
        }
    }
    return -1;
}
