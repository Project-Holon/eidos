/*
 * tests/test_wizard.c -- the first-run setup window, driven like a player
 * would drive it, with no display (SDL_VIDEODRIVER=dummy).
 *
 *     test_wizard <synth_dir> <work_dir>
 *
 * The wizard's `script` hook sees every frame and pushes the events a
 * player would cause (a file dropped on the window, a click, a key); its
 * `pick` hook stands in for the native file dialog.  Runs:
 *
 *   1. an archive dropped: refused, "Cannot use this file", and the
 *      message says to unpack it; a mouse click on "Choose another
 *      file..." opens the (scripted) dialog, which returns the synthetic
 *      cartridge; with the test-only override it is checked and written;
 *      "Ready to play"; Enter.  The extraction is there and the window
 *      drew text on every screen (and wrote a BMP of each);
 *   2. the synthetic cartridge dropped without the override: refused as
 *      "Modified ROM", nothing written; Escape quits;
 *   3. a ROM given up front, Escape at once: cancelled or, if the worker
 *      was quicker, finished; either way the folder is never half written;
 *      then the window is closed;
 *   4. no file dialog on the system: the notice says to drag the file;
 *   5. with BW_ROM set: the player's real ROM dropped, no override:
 *      checked, written, "Ready to play", and the result is playable
 *      (bw_setup_check, untrusted).  Skipped without BW_ROM.
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "picker.h"
#include "wizard.h"

#define FRAME_LIMIT 20000   /* about ten minutes at the loop's 30 ms */

typedef struct Script {
    int run;
    int step;
    int failed;
    const char *drop1;
    const char *drop2;
    const char *pick_path;
    int pick_rc;
    int picks;
    int seen[5];
    int max_progress;
    int min_ink;
    char note[512];
    char heading[96];
    char body[2048];
} Script;

static char g_work[512];
static char g_synth[600];

static void push_drop(const char *path)
{
    SDL_Event e;
    static char file[1024];     /* SDL3 events point at their text */

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_DROP_FILE;
    SDL_strlcpy(file, path, sizeof(file));
    e.drop.data = file;
    SDL_PushEvent(&e);
}

static void push_key(SDL_Keycode k)
{
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.key = k;
    SDL_PushEvent(&e);
}

static void push_click(const EidosWizardButton *b)
{
    SDL_Event e;
    int x = b->x + b->w / 2;
    int y = b->y + b->h / 2;

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = (float)x;
    e.motion.y = (float)y;
    SDL_PushEvent(&e);
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = true;
    e.button.x = (float)x;
    e.button.y = (float)y;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.down = false;
    SDL_PushEvent(&e);
}

static void push_quit(void)
{
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

static void push_close(void)
{
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    SDL_PushEvent(&e);
}

static int ink(const EidosWizardView *v)
{
    int i, n = 0;

    for (i = 0; i < EIDOS_WIZARD_WIDTH * EIDOS_WIZARD_HEIGHT; i++) {
        n += v->pixels[i] != v->background;
    }
    return n;
}

static void keep(Script *s, const EidosWizardView *v)
{
    int k = ink(v);

    s->seen[v->screen] = 1;
    if (v->progress > s->max_progress) {
        s->max_progress = v->progress;
    }
    if (s->min_ink == 0 || k < s->min_ink) {
        s->min_ink = k;
    }
    snprintf(s->heading, sizeof(s->heading), "%s", v->heading);
    snprintf(s->body, sizeof(s->body), "%s", v->body);
    snprintf(s->note, sizeof(s->note), "%s", v->note);
}

/* One step of the scripted player, called by the wizard every frame. */
static void script(void *user, const EidosWizardView *v)
{
    Script *s = (Script *)user;

    keep(s, v);
    if (v->frame > FRAME_LIMIT) {
        printf("  run %d: stuck on screen %d (%s)\n", s->run, v->screen, v->heading);
        s->failed = 1;
        push_quit();
        return;
    }
    switch (s->run) {
    case 1:
        if (s->step == 0 && v->screen == EIDOS_WIZARD_WELCOME) {
            push_drop(s->drop1);
            s->step = 1;
        } else if (s->step == 1 && v->screen == EIDOS_WIZARD_REFUSED) {
            if (strcmp(v->heading, "Cannot use this file") != 0
                || strstr(v->body, "unpack") == NULL) {
                printf("  run 1: archive refusal reads \"%s\" / \"%s\"\n", v->heading, v->body);
                s->failed = 1;
            }
            push_click(&v->button[0]);
            s->step = 2;
        } else if (s->step == 2 && v->screen == EIDOS_WIZARD_DONE) {
            push_key(SDLK_RETURN);
            s->step = 3;
        } else if (s->step == 2 && v->screen == EIDOS_WIZARD_REFUSED && s->picks > 0) {
            printf("  run 1: refused: %s / %s\n", v->heading, v->body);
            s->failed = 1;
            push_quit();
        }
        break;
    case 2:
        if (s->step == 0 && v->screen == EIDOS_WIZARD_WELCOME) {
            push_drop(s->drop1);
            s->step = 1;
        } else if (s->step == 1 && v->screen == EIDOS_WIZARD_REFUSED) {
            push_key(SDLK_ESCAPE);
            s->step = 2;
        }
        break;
    case 3:
        if (s->step == 0) {
            push_key(SDLK_ESCAPE);
            s->step = 1;
        } else if (s->step == 1 && (v->screen == EIDOS_WIZARD_REFUSED
                                    || v->screen == EIDOS_WIZARD_DONE)) {
            push_close();
            s->step = 2;
        }
        break;
    case 4:
        if (s->step == 0 && v->screen == EIDOS_WIZARD_WELCOME) {
            push_key(SDLK_O);
            s->step = 1;
        } else if (s->step == 1 && v->note[0] != '\0') {
            push_quit();
            s->step = 2;
        }
        break;
    case 5:
        if (s->step == 0 && v->screen == EIDOS_WIZARD_WELCOME) {
            push_drop(s->drop1);
            s->step = 1;
        } else if (s->step == 1 && v->screen == EIDOS_WIZARD_DONE) {
            push_key(SDLK_RETURN);
            s->step = 2;
        } else if (s->step == 1 && v->screen == EIDOS_WIZARD_REFUSED) {
            printf("  run 5: refused: %s / %s\n", v->heading, v->body);
            s->failed = 1;
            push_quit();
        }
        break;
    default:
        push_quit();
        break;
    }
}

static int pick(void *user, char *out, size_t n)
{
    Script *s = (Script *)user;

    s->picks++;
    snprintf(out, n, "%s", s->pick_path != NULL ? s->pick_path : "");
    return s->pick_rc;
}

static int has_file(const char *dir, const char *name)
{
    char p[1200];

    snprintf(p, sizeof(p), "%s/%s", dir, name);
    return bw_path_kind_utf8(p) == BW_PATH_FILE;
}

static void setup_cfg(EidosWizardConfig *cfg, Script *s, const char *dir, int run)
{
    static char prefix[700];

    memset(cfg, 0, sizeof(*cfg));
    memset(s, 0, sizeof(*s));
    s->run = run;
    cfg->extract_dir = dir;
    cfg->scale = 2;
    cfg->script = script;
    cfg->pick = pick;
    cfg->user = s;
    snprintf(prefix, sizeof(prefix), "%s/shot%d", g_work, run);
    cfg->shot_prefix = prefix;
}

int main(int argc, char **argv)
{
    EidosWizardConfig cfg;
    Script s;
    char zip[700], dir[700], rom_out[1024], shot[800];
    char real_dir[700];
    const char *real_rom;
    FILE *fp;
    int rc;

    if (argc < 3) {
        printf("usage: test_wizard <synth_dir> <work_dir>\n");
        return 2;
    }
    snprintf(g_synth, sizeof(g_synth), "%s/synth.nds", argv[1]);
    snprintf(g_work, sizeof(g_work), "%s", argv[2]);
    bw_remove_tree_utf8(g_work);
    BW_REQUIRE("work dir", bw_mkdirs_utf8(g_work) == 0);
    bw_setup_set_home(g_work);          /* never the player's real home */

    snprintf(zip, sizeof(zip), "%s/Pokemon Black.zip", g_work);
    fp = bw_fopen_utf8(zip, "wb");
    BW_REQUIRE("write the archive", fp != NULL);
    fputs("PK\003\004", fp);
    {
        int i;
        for (i = 0; i < 1024; i++) {
            fputc(0, fp);
        }
    }
    fclose(fp);

    /* 1 */
    snprintf(dir, sizeof(dir), "%s/extract \xc3\xa9", g_work);
    setup_cfg(&cfg, &s, dir, 1);
    cfg.allow_unverified = 1;
    s.drop1 = zip;
    s.pick_path = g_synth;
    s.pick_rc = BW_PICK_OK;
    rc = eidos_wizard_run(&cfg, rom_out, sizeof(rom_out));
    BW_CHECK_EQ("run 1: installed", rc, EIDOS_WIZARD_INSTALLED);
    BW_CHECK_EQ("run 1: script clean", s.failed, 0);
    BW_CHECK_EQ("run 1: the click opened the dialog once", s.picks, 1);
    BW_CHECK_EQ("run 1: reached the end", s.step, 3);
    BW_CHECK_EQ("run 1: the ROM handed back", strcmp(rom_out, g_synth), 0);
    BW_CHECK_EQ("run 1: the extraction is there", has_file(dir, "eidos_manifest.json"), 1);
    BW_CHECK_EQ("run 1: welcome, refused and done were shown",
                s.seen[EIDOS_WIZARD_WELCOME] && s.seen[EIDOS_WIZARD_REFUSED]
                && s.seen[EIDOS_WIZARD_DONE], 1);
    BW_CHECK_EQ("run 1: every frame had text on it", s.min_ink > 1500, 1);
    BW_CHECK_EQ("run 1: last heading", strcmp(s.heading, "Ready to play"), 0);
    snprintf(shot, sizeof(shot), "%s/shot1-welcome.bmp", g_work);
    BW_CHECK_EQ("run 1: welcome screenshot", bw_path_kind_utf8(shot), BW_PATH_FILE);
    snprintf(shot, sizeof(shot), "%s/shot1-done.bmp", g_work);
    BW_CHECK_EQ("run 1: done screenshot", bw_path_kind_utf8(shot), BW_PATH_FILE);

    /* 2 */
    snprintf(dir, sizeof(dir), "%s/extract2", g_work);
    setup_cfg(&cfg, &s, dir, 2);
    s.drop1 = g_synth;
    rc = eidos_wizard_run(&cfg, rom_out, sizeof(rom_out));
    BW_CHECK_EQ("run 2: quit", rc, EIDOS_WIZARD_QUIT);
    BW_CHECK_EQ("run 2: refused as modified", strcmp(s.heading, "Modified ROM"), 0);
    BW_CHECK_EQ("run 2: the message names the known dump",
                strstr(s.body, "known good dump") != NULL, 1);
    BW_CHECK_EQ("run 2: nothing written", bw_path_kind_utf8(dir), BW_PATH_NONE);

    /* 3 */
    snprintf(dir, sizeof(dir), "%s/extract3", g_work);
    setup_cfg(&cfg, &s, dir, 3);
    cfg.rom = g_synth;
    cfg.allow_unverified = 1;
    rc = eidos_wizard_run(&cfg, rom_out, sizeof(rom_out));
    BW_CHECK_EQ("run 3: closed", rc, EIDOS_WIZARD_QUIT);
    BW_CHECK_EQ("run 3: started checking at once", s.seen[EIDOS_WIZARD_CHECKING], 1);
    if (strcmp(s.heading, "Cancelled") == 0) {
        printf("  run 3: cancelled before the worker finished\n");
        BW_CHECK_EQ("run 3: cancelled leaves nothing", bw_path_kind_utf8(dir), BW_PATH_NONE);
    } else {
        printf("  run 3: the worker finished before Escape arrived (%s)\n", s.heading);
        BW_CHECK_EQ("run 3: finished means complete", has_file(dir, "eidos_manifest.json"), 1);
    }
    {
        char partial[800];
        snprintf(partial, sizeof(partial), "%s.partial", dir);
        BW_CHECK_EQ("run 3: no staging folder", bw_path_kind_utf8(partial), BW_PATH_NONE);
    }

    /* 4 */
    snprintf(dir, sizeof(dir), "%s/extract4", g_work);
    setup_cfg(&cfg, &s, dir, 4);
    s.pick_path = "No file dialog is available here.";
    s.pick_rc = BW_PICK_UNAVAILABLE;
    rc = eidos_wizard_run(&cfg, rom_out, sizeof(rom_out));
    BW_CHECK_EQ("run 4: quit", rc, EIDOS_WIZARD_QUIT);
    BW_CHECK_EQ("run 4: the notice says to drag the file", strstr(s.note, "Drag") != NULL, 1);

    /* 5 */
    real_rom = getenv("BW_ROM");
    if (real_rom == NULL || real_rom[0] == '\0') {
        printf("run 5 skipped: set BW_ROM to the Pokemon Black ROM to install it "
               "through the window\n");
    } else {
        char why[512];
        snprintf(real_dir, sizeof(real_dir), "%s/real extract", g_work);
        setup_cfg(&cfg, &s, real_dir, 5);
        s.drop1 = real_rom;
        rc = eidos_wizard_run(&cfg, rom_out, sizeof(rom_out));
        BW_CHECK_EQ("run 5: installed", rc, EIDOS_WIZARD_INSTALLED);
        BW_CHECK_EQ("run 5: script clean", s.failed, 0);
        BW_CHECK_EQ("run 5: the checking screen was shown", s.seen[EIDOS_WIZARD_CHECKING], 1);
        BW_CHECK_EQ("run 5: the writing screen was shown", s.seen[EIDOS_WIZARD_WRITING], 1);
        BW_CHECK_EQ("run 5: the bar moved", s.max_progress > 0, 1);
        BW_CHECK_EQ("run 5: playable", bw_setup_check(real_dir, 0, why, sizeof(why)),
                    BW_ROOT_OK);
        printf("  run 5: %s, progress up to %d/1000\n", s.heading, s.max_progress);
    }

    /* EIDOS_TEST_KEEP keeps the screenshots for a look. */
    if (bw_failures == 0 && getenv("EIDOS_TEST_KEEP") == NULL) {
        bw_remove_tree_utf8(g_work);
    }
    BW_REPORT("test_wizard");
}
