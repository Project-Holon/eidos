/*
 * install/setup.h -- where the data root lives, whether it is usable, and
 * the first-run decisions that need no window.
 *
 * install.h turns a ROM into an extraction; this decides where that
 * extraction goes and whether the one already there can be played, and it
 * is what every front end asks first:
 *
 *   - the locations: the eidos home folder per platform (Local AppData on
 *     Windows, Application Support on macOS and iOS, $XDG_DATA_HOME on
 *     Linux, the app's internal storage on Android), the extraction inside
 *     it, the config file beside it, and portable mode (a portable.txt
 *     beside the executable puts all three there instead);
 *   - the resolution order of docs/installer.md: --data-root, then the
 *     BW_DATA_ROOT environment variable, then the folder saved in the
 *     config file, then the platform default;
 *   - the check: an extraction is playable when its eidos_manifest.json
 *     names this installer version and a known good dump, and the files
 *     the engine opens first are there;
 *   - the command-line path that skips the wizard (--rom), the headless
 *     answer when there is no window (--no-window), and the two
 *     maintenance commands (--reset-data, --move-data).
 *
 * Nothing here draws.  The SDL wizard (engine/platform/wizard.h) is what
 * a windowed build runs when bw_setup_prepare says the data is missing;
 * it calls back into this file to remember what it installed.  Paths are
 * UTF-8 everywhere, as in install.h.  The operating-system calls (the
 * known folders, the executable's own folder, the wide environment and
 * command line on Windows) are in install/location.c.
 */

#ifndef EIDOS_SETUP_H
#define EIDOS_SETUP_H

#include <stddef.h>

#include "install.h"

#define BW_SETUP_PATH_MAX 1024

#ifdef _WIN32
#define BW_SETUP_SEP "\\"
#else
#define BW_SETUP_SEP "/"
#endif

/* ------------------------------------------------------------------ */
/* Locations                                                           */
/* ------------------------------------------------------------------ */

/*
 * The eidos home folder, in this order:
 *   1. the folder a front end set with bw_setup_set_home (Android, where
 *      only the Java side knows the app's internal storage);
 *   2. the EIDOS_HOME environment variable (tests, and anyone who wants
 *      everything in one place);
 *   3. the executable's folder, when a file named portable.txt is in it;
 *   4. the platform default (bw_os_app_home).
 * 0 on success, -1 if none of them gives a folder.
 */
int  bw_setup_home(char *out, size_t n);
void bw_setup_set_home(const char *dir);   /* NULL forgets it */

/* <home>/extract and <home>/config.txt. */
int  bw_setup_default_extract(char *out, size_t n);
int  bw_setup_config_path(char *out, size_t n);

/* ------------------------------------------------------------------ */
/* The operating system (install/location.c)                           */
/* ------------------------------------------------------------------ */

/* The platform default home: %LOCALAPPDATA%\eidos, ~/Library/Application
 * Support/eidos, $XDG_DATA_HOME/eidos (~/.local/share/eidos).  -1 where
 * there is none without help from the front end (Android). */
int   bw_os_app_home(char *out, size_t n);
/* The folder holding the running executable, -1 if the platform will not
 * say. */
int   bw_os_exe_dir(char *out, size_t n);
/* An environment variable as UTF-8 (on Windows through the wide
 * environment, so a non-ASCII value survives).  NULL if unset or empty. */
const char *bw_os_getenv(const char *name, char *buf, size_t n);
/* The command line as UTF-8.  On Windows argv is in the ANSI code page,
 * so this rebuilds it from the wide command line; elsewhere it returns
 * argv unchanged.  The result lives until the process exits. */
char **bw_os_utf8_argv(int *argc, char **argv);
/* `path` made absolute against the current folder (it need not exist).
 * 0 on success. */
int   bw_os_abspath(const char *path, char *out, size_t n);

/* ------------------------------------------------------------------ */
/* The config file                                                     */
/* ------------------------------------------------------------------ */

/*
 * Two lines of "key=value" text: where the extraction is and which ROM it
 * was made from.  The ROM is never copied; its path is kept so a later
 * installer version that changes the layout can re-extract without asking.
 */
#define BW_SETUP_ROMS_MAX 16

typedef struct BwSetupConfig {
    char data_root[BW_SETUP_PATH_MAX];  /* the extraction folder, "" unset */
    char rom[BW_SETUP_PATH_MAX];        /* the ROM it came from, "" unset */
    /* An install that holds both games (below, "The two games"): the
     * folder played last ("played="), the folder each game was last played
     * from ("game.black=", "game.white="), and the ROM of each cartridge
     * ("rom.IRAO=").  A version of eidos from before them keeps its two
     * lines and drops these, which only costs their memory.  `data_root`
     * and `rom` keep their first meaning whatever is played: a start that
     * names no folder and does not ask for the game played last (the
     * launcher's Black profile) finds its data by them. */
    char played[BW_SETUP_PATH_MAX];
    char game_root[2][BW_SETUP_PATH_MAX];       /* [0] Black, [1] White */
    struct {
        char code[5];
        char path[BW_SETUP_PATH_MAX];
    } roms[BW_SETUP_ROMS_MAX];
    int nroms;
} BwSetupConfig;

/* 0 read, 1 no config file yet (`c` cleared), -1 unreadable. */
int bw_setup_config_load(BwSetupConfig *c);
/* 0 written (to a temporary file first, then renamed into place). */
int bw_setup_config_save(const BwSetupConfig *c);

/* ------------------------------------------------------------------ */
/* Is the data root usable                                             */
/* ------------------------------------------------------------------ */

typedef enum BwRootState {
    BW_ROOT_OK = 0,
    BW_ROOT_MISSING,        /* nothing there, or an empty folder */
    BW_ROOT_INCOMPLETE,     /* a folder, but no manifest or no data/ */
    BW_ROOT_OLD,            /* made by another installer version */
    BW_ROOT_OTHER_IMAGE,    /* made from an image that is not a known good
                               dump (a hack, a translation, --any) */
    BW_ROOT_DAMAGED,        /* the manifest is fine, files are missing */
    BW_ROOT_OTHER_GAME      /* a folder named for this run (--data-root,
                               BW_DATA_ROOT) holds the game --game did not
                               ask for */
} BwRootState;

/*
 * Check an extraction folder.  `trusted` is set for a folder the player or
 * a developer named explicitly (--data-root, BW_DATA_ROOT): for those the
 * NitroFS tree being there is enough, so aletheia's own extract/ (which
 * has no eidos manifest) and an --any extraction both run.  `why` gets one
 * sentence for the player.
 */
BwRootState bw_setup_check(const char *extract_dir, int trusted,
                           char *why, size_t why_len);

/*
 * The extraction folder and the NitroFS folder the engine opens for a path
 * the player gave.  Both spellings are taken: the extraction folder (the
 * one with eidos_manifest.json and data/) and the data/ folder itself (the
 * one with a/0/0/2, which is what BW_DATA_ROOT has always named).
 */
void bw_setup_split(const char *path, char *extract_dir, size_t ne,
                    char *data_dir, size_t nd);

/* ------------------------------------------------------------------ */
/* The two games                                                       */
/* ------------------------------------------------------------------ */

/*
 * One install can hold the data of both games.  English Black's extraction
 * is <home>/extract, as it always was; any other cartridge's is
 * <home>/extract-<game code> (extract-IRAO: English White), the names the
 * launcher gives them.  A folder that already holds a game stays where it
 * is, whatever its name: the game a folder holds is read from the
 * header.bin the extractor wrote there, never from the folder's name.
 */

/* "black" or "white" (any case) as a game; BW_GAME_UNKNOWN otherwise. */
BwGame bw_setup_game_named(const char *name);
/* "Black" or "White", for a sentence; "" for BW_GAME_UNKNOWN. */
const char *bw_setup_game_title(BwGame game);
/* The game of a cartridge's game code: IRB? Black, IRA? White. */
BwGame bw_setup_game_of_code(const char *gamecode);
/* The game an extraction folder holds, and its game code (`code` may be
 * NULL).  BW_GAME_UNKNOWN when the folder has no readable header.bin. */
BwGame bw_setup_game_of(const char *extract_dir, char code[5]);
/*
 * The playable extraction of `game` in this install: the folder that game
 * was last played from, else the config file's folder when it holds that
 * game, else <home>/extract, else <home>/extract-<code> for each cartridge
 * the port plays, in the order of the known dumps.  0 with `out` filled,
 * -1 when the install has none.
 */
int bw_setup_find_game(BwGame game, char *out, size_t n);
/*
 * Where the data of a cartridge with this game code is made: the folder
 * that already holds that cartridge, else <home>/extract for English Black
 * and <home>/extract-<code> for the others (and for English Black when
 * <home>/extract holds another cartridge).  Never a folder that holds
 * another cartridge's data.
 */
int bw_setup_place_for(const char *gamecode, char *out, size_t n);
/* The folder (an extraction of this install) is the one played last: the
 * next start that follows the game played last (BwSetupRequest.follow_last)
 * plays it.  0 on success. */
int bw_setup_set_current(const char *extract_dir);
/* What the install made in the folder of a cartridge: the folder is the
 * one played last, its game's folder, and `rom` the ROM its cartridge
 * came from.  `data_root=` is left as it is. */
int bw_setup_remember_game(const char *extract_dir, const char *rom);

/* ------------------------------------------------------------------ */
/* The first-run decision                                              */
/* ------------------------------------------------------------------ */

typedef struct BwSetupRequest {
    const char *data_root;  /* --data-root PATH */
    const char *rom;        /* --rom PATH: install from it, no wizard */
    const char *move_to;    /* --move-data PATH */
    int no_window;          /* --no-window: never open the wizard */
    int reset;              /* --reset-data */
    int game;               /* --game black|white: BW_GAME_*, 0 not given */
    int follow_last;        /* with no folder and no game named: the game
                               played last (a standalone game sets it; not
                               an option of the command line) */
} BwSetupRequest;

enum {
    BW_SOURCE_FLAG = 0,     /* --data-root */
    BW_SOURCE_ENV,          /* BW_DATA_ROOT */
    BW_SOURCE_CONFIG,       /* the config file */
    BW_SOURCE_DEFAULT,      /* the platform default */
    BW_SOURCE_GAME,         /* --game: that game's folder in this install */
    BW_SOURCE_LAST          /* the game played last (follow_last) */
};

typedef struct BwSetupResolved {
    char extract_dir[BW_SETUP_PATH_MAX];
    char data_dir[BW_SETUP_PATH_MAX];       /* what bw_set_data_root takes */
    char rom[BW_SETUP_PATH_MAX];            /* the remembered ROM, "" none */
    int  source;                            /* BW_SOURCE_* */
    BwRootState state;
    char why[512];                          /* for the player when not OK */
} BwSetupResolved;

/*
 * Consume one of the setup options at argv[*i] (advancing *i past its
 * value).  1 consumed, 0 not a setup option, -1 an option missing its
 * value.  A front end calls this first in its own argument loop.
 */
int bw_setup_parse_arg(BwSetupRequest *req, int argc, char **argv, int *i);

/* The lines a front end prints in its usage text. */
const char *bw_setup_usage(void);

/* Where the data root is and what state it is in; never writes. */
int bw_setup_resolve(const BwSetupRequest *req, BwSetupResolved *out);

enum {
    BW_PREPARE_READY = 0,   /* out->data_dir is playable */
    BW_PREPARE_WIZARD,      /* not playable; a windowed build opens the
                               wizard on out->extract_dir */
    BW_PREPARE_EXIT_OK,     /* a maintenance command ran; exit 0 */
    BW_PREPARE_FAILED       /* a message was printed; exit 1 */
};

/*
 * Everything first run does without a window: resolve, run --reset-data or
 * --move-data, install from --rom (or re-extract an older installer's
 * extraction from the remembered ROM) with progress on stderr, and, when
 * the data is still missing and `req->no_window` is set, print where it
 * looked and the command that would fill it.  `progress` (may be NULL) is
 * used for the --rom installation instead of the stderr bar.
 */
int bw_setup_prepare(const BwSetupRequest *req, BwSetupResolved *out);

/* How far bw_setup_install got, for a front end that words its own
 * headline (the wizard): the message alone is the same everywhere. */
enum {
    BW_STAGE_IDENTIFY = 0,  /* not readable as a cartridge image at all */
    BW_STAGE_VERDICT,       /* identified, and not the supported dump */
    BW_STAGE_SPACE,         /* not enough free space */
    BW_STAGE_EXTRACT,       /* the extraction failed */
    BW_STAGE_DONE           /* extracted, or already there */
};

typedef struct BwSetupOutcome {
    int stage;              /* BW_STAGE_*: the last one reached */
    BwRomVerdict verdict;   /* from BW_STAGE_VERDICT on */
    BwRomInfo info;         /* from BW_STAGE_VERDICT on */
    int already;            /* the folder already held this extraction */
    int other_game;         /* bw_setup_install_game: the ROM is the game
                               that was not asked for; nothing was read
                               past its header */
} BwSetupOutcome;

/*
 * Install from `rom` into `extract_dir` without a window: identify, refuse
 * anything but the known good dump (`allow_unverified` is for tests),
 * check the free space, extract (replacing an eidos extraction of another
 * image: the player chose this ROM), then remember the folder and the ROM
 * in the config file unless `remember` is 0.  0 on success, -1 with `msg`
 * filled in words for the player, BW_CANCELLED if `progress` stopped it.
 * `outcome` (may be NULL) says how far it got.
 */
int bw_setup_install(const char *rom, const char *extract_dir,
                     int allow_unverified, int remember,
                     BwProgressFn progress, void *ctx,
                     BwSetupOutcome *outcome, char *msg, size_t msg_len);

/*
 * The same into the install's own folder for the ROM's cartridge
 * (bw_setup_place_for), so the data of one game never replaces the
 * other's.  `want` refuses a ROM of the other game before anything is
 * read past its header (BW_GAME_UNKNOWN: either game).  `dir_out` gets the
 * folder.
 */
int bw_setup_install_game(const char *rom, BwGame want,
                          int allow_unverified, int remember,
                          BwProgressFn progress, void *ctx,
                          BwSetupOutcome *outcome, char *dir_out, size_t dir_len,
                          char *msg, size_t msg_len);

/* The hash an extraction of an identified image records: for a dump
 * trimmed of its padding, the hash of the whole dump it was cut from, so
 * a trimmed dump's extraction is recognised as the known dump's. */
const char *bw_setup_identity(const BwRomInfo *info);

/* The free space an extraction of `rom_size` bytes asks for: the ROM's
 * own size (the extraction is about 80 percent of it) plus a margin. */
u64 bw_setup_space_needed(u32 rom_size);

/* Save `extract_dir` and `rom` (either may be NULL to keep the saved one)
 * as the config file's entries.  0 on success. */
int bw_setup_remember(const char *extract_dir, const char *rom);

/*
 * --reset-data: delete the extraction at `extract_dir` and forget it in
 * the config file.  Only an eidos extraction (one with eidos_manifest.json,
 * or the installer's own staging folders) is deleted; a folder holding
 * anything else is refused, never emptied.
 */
int bw_setup_reset(const char *extract_dir, char *msg, size_t msg_len);

/*
 * --move-data: move the extraction at `from` to `to` (absent, or an empty
 * folder) and save the new place.  A rename when both are on one volume;
 * across volumes the extraction is made again from the remembered ROM and
 * the old one deleted afterwards.  0 on success.
 */
int bw_setup_move(const char *from, const char *to, char *msg, size_t msg_len);

#endif /* EIDOS_SETUP_H */
