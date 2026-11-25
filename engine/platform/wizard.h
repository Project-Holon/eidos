/*
 * engine/platform/wizard.h -- the first-run setup window, and the one call
 * a front end makes before it starts the game.
 *
 * When the data root is missing or not playable (install/setup.h decides),
 * a windowed build opens this: one SDL window, drawn by eidos itself with
 * its own bitmap font (font.c), the same on every platform.  It says what
 * is needed, takes the ROM from the platform's file dialog
 * (install/picker.h) or from a file dropped on the window, checks it and
 * writes the game data on a worker thread with a progress bar and a
 * Cancel button, says in plain words why a file was refused (an archive,
 * another game, a damaged or modified dump), and hands back the ROM it
 * installed from.  docs/installer.md has the design.
 *
 * Every step is reachable from the keyboard (Enter, Escape, Tab, O) as well
 * as the mouse, and the window is testable without a display: under
 * SDL_VIDEODRIVER=dummy the config's `script` hook sees every frame (the
 * screen, its text, the button rectangles, the pixels) and pushes the
 * events a player would cause, and `pick` replaces the native dialog
 * (tests/test_wizard.c).
 */

#ifndef EIDOS_ENGINE_WIZARD_H
#define EIDOS_ENGINE_WIZARD_H

#include <stddef.h>

#include "types.h"
#include "setup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window's own resolution; it is shown at an integer scale. */
#define EIDOS_WIZARD_WIDTH  512
#define EIDOS_WIZARD_HEIGHT 320

enum {
    EIDOS_WIZARD_WELCOME = 0,   /* what is needed; choose or drop the ROM */
    EIDOS_WIZARD_CHECKING,      /* hashing the ROM */
    EIDOS_WIZARD_WRITING,       /* extracting */
    EIDOS_WIZARD_REFUSED,       /* why it did not work; choose another */
    EIDOS_WIZARD_DONE           /* ready; Start */
};

typedef struct EidosWizardButton {
    int x, y, w, h;             /* window coordinates */
    const char *label;
} EidosWizardButton;

/* What the window shows this frame, for the test hook. */
typedef struct EidosWizardView {
    int screen;                 /* EIDOS_WIZARD_* */
    u32 frame;
    const char *heading;
    const char *body;
    const char *note;           /* a one-line notice, "" when none */
    int progress;               /* 0..1000 while checking or writing */
    int nbuttons;
    EidosWizardButton button[2];
    int focus;                  /* the button Enter presses */
    const u32 *pixels;          /* EIDOS_WIZARD_WIDTH x _HEIGHT, 0xAARRGGBB */
    u32 background;             /* the background colour, for counting ink */
    int scale;                  /* window pixels per wizard pixel */
} EidosWizardView;

typedef struct EidosWizardConfig {
    const char *extract_dir;    /* where the game data goes (required) */
    const char *reason;         /* a sentence shown first (why setup is
                                   open again), or NULL */
    const char *rom;            /* start checking this ROM at once, or NULL */
    int scale;                  /* 0: the largest that fits, at most 3 */
    /* The install's own data (no folder was named for this run): the data
     * is made in the folder of the ROM's own cartridge
     * (bw_setup_install_game), which `extract_dir` only stands for until
     * the ROM is known; `dir_out` (may be NULL) gets the folder. */
    int by_cartridge;
    int game;                   /* BW_GAME_*: only this game's ROM is taken
                                   (with by_cartridge); 0: either */
    char *dir_out;
    size_t dir_len;

    /* Tests only.  The player has no override: only the known good dump
     * is installed (docs/installer.md). */
    int allow_unverified;
    /* Replaces the native dialog; returns BW_PICK_*. */
    int (*pick)(void *user, char *out, size_t n);
    /* Called once a frame after drawing. */
    void (*script)(void *user, const EidosWizardView *view);
    void *user;
    /* Write <prefix>-<screen>.bmp the first time each screen is shown. */
    const char *shot_prefix;
} EidosWizardConfig;

enum {
    EIDOS_WIZARD_INSTALLED = 0, /* `rom_out` holds the ROM installed from */
    EIDOS_WIZARD_QUIT = 1,      /* the player closed the window */
    EIDOS_WIZARD_NO_WINDOW = -1 /* no window could be opened */
};

int eidos_wizard_run(const EidosWizardConfig *cfg, char *rom_out, size_t n);

/*
 * First run, the whole of it: resolve the data root (install/setup.h), run
 * --reset-data, --move-data or --rom if asked, and open the wizard when the
 * data is not playable and a window is allowed; then remember what was
 * installed.  Returns 1 with `data_dir` (what bw_set_data_root takes)
 * filled when the game can start; 0 when the program should exit with
 * *exit_status (0 after a maintenance command or when the player closed
 * setup, 1 on failure, a message having been printed).
 */
int eidos_firstrun(const BwSetupRequest *req, char *data_dir, size_t n,
                   int *exit_status);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_WIZARD_H */
