/*
 * install/setup.c -- the data root's location, its check, and the parts of
 * first run that need no window.  See setup.h for the contract and
 * docs/installer.md for the design it implements.
 *
 * Plain C99 and the standard library; the operating-system calls are in
 * install/location.c and install/fsutil.c.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup.h"

#define MANIFEST_NAME "eidos_manifest.json"
#define PARTIAL_MARK  ".eidos_partial"
#define CONFIG_NAME   "config.txt"
#define PORTABLE_NAME "portable.txt"

static char g_home[BW_SETUP_PATH_MAX];

static void say(char *msg, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (msg != NULL && n > 0) {
        va_start(ap, fmt);
        vsnprintf(msg, n, fmt, ap);
        va_end(ap);
    }
}

static int copy(char *out, size_t n, const char *s)
{
    size_t len = strlen(s);

    if (len + 1 > n) {
        return -1;
    }
    memcpy(out, s, len + 1);
    return 0;
}

static int is_sep(char c)
{
    return c == '/' || c == '\\';
}

/* `dir` + separator + `name`, without doubling a trailing separator. */
static int join(char *out, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    int w;

    if (len > 0 && is_sep(dir[len - 1])) {
        w = snprintf(out, n, "%s%s", dir, name);
    } else {
        w = snprintf(out, n, "%s" BW_SETUP_SEP "%s", dir, name);
    }
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

/* Drop trailing separators (but not the one of a root). */
static void trim_seps(char *path)
{
    size_t len = strlen(path);

    while (len > 1 && is_sep(path[len - 1])
           && !(len == 3 && path[1] == ':')) {
        path[--len] = '\0';
    }
}

/* The folder part of `path`, "." when there is none. */
static void parent_of(const char *path, char *out, size_t n)
{
    size_t len;

    if (copy(out, n, path) != 0) {
        out[0] = '\0';
        return;
    }
    trim_seps(out);
    len = strlen(out);
    while (len > 0 && !is_sep(out[len - 1])) {
        len--;
    }
    if (len == 0) {
        (void)copy(out, n, ".");
        return;
    }
    out[len] = '\0';
    trim_seps(out);
}

/* ------------------------------------------------------------------ */
/* Locations                                                           */
/* ------------------------------------------------------------------ */

void bw_setup_set_home(const char *dir)
{
    if (dir == NULL || copy(g_home, sizeof(g_home), dir) != 0) {
        g_home[0] = '\0';
    }
}

int bw_setup_home(char *out, size_t n)
{
    char buf[BW_SETUP_PATH_MAX];
    char probe[BW_SETUP_PATH_MAX];

    if (g_home[0] != '\0') {
        return copy(out, n, g_home);
    }
    if (bw_os_getenv("EIDOS_HOME", buf, sizeof(buf)) != NULL) {
        return copy(out, n, buf);
    }
    /* Portable mode: a USB stick carries its data with it. */
    if (bw_os_exe_dir(buf, sizeof(buf)) == 0
        && join(probe, sizeof(probe), buf, PORTABLE_NAME) == 0
        && bw_path_kind_utf8(probe) == BW_PATH_FILE) {
        return copy(out, n, buf);
    }
    return bw_os_app_home(out, n);
}

int bw_setup_default_extract(char *out, size_t n)
{
    char home[BW_SETUP_PATH_MAX];

    if (bw_setup_home(home, sizeof(home)) != 0) {
        return -1;
    }
    return join(out, n, home, "extract");
}

int bw_setup_config_path(char *out, size_t n)
{
    char home[BW_SETUP_PATH_MAX];

    if (bw_setup_home(home, sizeof(home)) != 0) {
        return -1;
    }
    return join(out, n, home, CONFIG_NAME);
}

/* ------------------------------------------------------------------ */
/* The config file                                                     */
/* ------------------------------------------------------------------ */

int bw_setup_config_load(BwSetupConfig *c)
{
    char path[BW_SETUP_PATH_MAX];
    char line[BW_SETUP_PATH_MAX + 32];
    FILE *fp;

    memset(c, 0, sizeof(*c));
    if (bw_setup_config_path(path, sizeof(path)) != 0) {
        return -1;
    }
    if (bw_path_kind_utf8(path) == BW_PATH_NONE) {
        return 1;
    }
    fp = bw_fopen_utf8(path, "r");
    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        char *eq = strchr(line, '=');

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (line[0] == '#' || eq == NULL) {
            continue;
        }
        *eq = '\0';
        if (strcmp(line, "data_root") == 0) {
            (void)copy(c->data_root, sizeof(c->data_root), eq + 1);
        } else if (strcmp(line, "rom") == 0) {
            (void)copy(c->rom, sizeof(c->rom), eq + 1);
        } else if (strcmp(line, "played") == 0) {
            (void)copy(c->played, sizeof(c->played), eq + 1);
        } else if (strcmp(line, "game.black") == 0) {
            (void)copy(c->game_root[0], sizeof(c->game_root[0]), eq + 1);
        } else if (strcmp(line, "game.white") == 0) {
            (void)copy(c->game_root[1], sizeof(c->game_root[1]), eq + 1);
        } else if (strncmp(line, "rom.", 4) == 0 && strlen(line) == 8
                   && c->nroms < BW_SETUP_ROMS_MAX) {
            memcpy(c->roms[c->nroms].code, line + 4, 5);
            if (copy(c->roms[c->nroms].path, sizeof(c->roms[c->nroms].path), eq + 1) == 0) {
                c->nroms++;
            }
        }
    }
    fclose(fp);
    return 0;
}

int bw_setup_config_save(const BwSetupConfig *c)
{
    char home[BW_SETUP_PATH_MAX];
    char path[BW_SETUP_PATH_MAX];
    char tmp[BW_SETUP_PATH_MAX + 8];
    FILE *fp;
    int bad, i;

    if (bw_setup_home(home, sizeof(home)) != 0
        || join(path, sizeof(path), home, CONFIG_NAME) != 0
        || bw_mkdirs_utf8(home) != 0) {
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = bw_fopen_utf8(tmp, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "# eidos: where the game data is and which ROM it came from.\n"
                "# Written by eidos; --reset-data and --move-data change it.\n");
    if (c->data_root[0] != '\0') {
        fprintf(fp, "data_root=%s\n", c->data_root);
    }
    if (c->rom[0] != '\0') {
        fprintf(fp, "rom=%s\n", c->rom);
    }
    if (c->played[0] != '\0') {
        fprintf(fp, "played=%s\n", c->played);
    }
    if (c->game_root[0][0] != '\0') {
        fprintf(fp, "game.black=%s\n", c->game_root[0]);
    }
    if (c->game_root[1][0] != '\0') {
        fprintf(fp, "game.white=%s\n", c->game_root[1]);
    }
    for (i = 0; i < c->nroms; i++) {
        fprintf(fp, "rom.%s=%s\n", c->roms[i].code, c->roms[i].path);
    }
    bad = ferror(fp);
    if (fclose(fp) != 0 || bad) {
        (void)bw_remove_tree_utf8(tmp);
        return -1;
    }
    if (bw_remove_tree_utf8(path) != 0 || bw_rename_utf8(tmp, path) != 0) {
        (void)bw_remove_tree_utf8(tmp);
        return -1;
    }
    return 0;
}

/* The config's lines of one game and one cartridge: the folder `game` was
 * last played from, and the ROM the cartridge `code` came from (NULL: left
 * as it is). */
static void config_note(BwSetupConfig *c, BwGame game, const char *dir,
                        const char *code, const char *rom)
{
    int i;

    if (dir != NULL && (game == BW_GAME_BLACK || game == BW_GAME_WHITE)) {
        (void)copy(c->game_root[game == BW_GAME_WHITE], BW_SETUP_PATH_MAX, dir);
    }
    if (rom == NULL || code == NULL || strlen(code) != 4) {
        return;
    }
    for (i = 0; i < c->nroms && strcmp(c->roms[i].code, code) != 0; i++) {
    }
    if (i == c->nroms) {
        if (c->nroms == BW_SETUP_ROMS_MAX) {
            return;
        }
        memcpy(c->roms[c->nroms++].code, code, 5);
    }
    (void)copy(c->roms[i].path, sizeof(c->roms[i].path), rom);
}

int bw_setup_remember(const char *extract_dir, const char *rom)
{
    BwSetupConfig c;
    char code[5];
    BwGame game;

    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    if (extract_dir != NULL
        && bw_os_abspath(extract_dir, c.data_root, sizeof(c.data_root)) != 0) {
        (void)copy(c.data_root, sizeof(c.data_root), extract_dir);
    }
    if (rom != NULL && bw_os_abspath(rom, c.rom, sizeof(c.rom)) != 0) {
        (void)copy(c.rom, sizeof(c.rom), rom);
    }
    trim_seps(c.data_root);
    /* the same for the game the folder holds (a folder named for one run,
     * `extract_dir` NULL, is that run's: only its ROM is remembered) */
    if (extract_dir != NULL) {
        game = bw_setup_game_of(c.data_root, code);
        config_note(&c, game, c.data_root, code, rom != NULL ? c.rom : NULL);
    } else if (rom != NULL && bw_rom_peek_code(c.rom, code) == 0) {
        config_note(&c, BW_GAME_UNKNOWN, NULL, code, c.rom);
    }
    return bw_setup_config_save(&c);
}

/* ------------------------------------------------------------------ */
/* The two games                                                       */
/* ------------------------------------------------------------------ */

BwGame bw_setup_game_named(const char *name)
{
    static const char *const names[2] = { "black", "white" };
    int g, i;

    if (name == NULL) {
        return BW_GAME_UNKNOWN;
    }
    for (g = 0; g < 2; g++) {
        for (i = 0; names[g][i] != '\0'; i++) {
            char ch = name[i];

            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (ch != names[g][i]) {
                break;
            }
        }
        if (names[g][i] == '\0' && name[i] == '\0') {
            return g == 0 ? BW_GAME_BLACK : BW_GAME_WHITE;
        }
    }
    return BW_GAME_UNKNOWN;
}

const char *bw_setup_game_title(BwGame game)
{
    return game == BW_GAME_BLACK ? "Black" : game == BW_GAME_WHITE ? "White" : "";
}

BwGame bw_setup_game_of_code(const char *gamecode)
{
    if (gamecode == NULL || strlen(gamecode) != 4) {
        return BW_GAME_UNKNOWN;
    }
    if (memcmp(gamecode, "IRB", 3) == 0) {
        return BW_GAME_BLACK;
    }
    if (memcmp(gamecode, "IRA", 3) == 0) {
        return BW_GAME_WHITE;
    }
    return BW_GAME_UNKNOWN;
}

BwGame bw_setup_game_of(const char *extract_dir, char code[5])
{
    char ex[BW_SETUP_PATH_MAX], data[BW_SETUP_PATH_MAX], path[BW_SETUP_PATH_MAX];
    char local[5];

    if (code == NULL) {
        code = local;
    }
    code[0] = '\0';
    if (extract_dir == NULL || extract_dir[0] == '\0') {
        return BW_GAME_UNKNOWN;
    }
    /* (header.bin is the ROM's first bytes, so its game code is read as a
     * ROM file's is) */
    bw_setup_split(extract_dir, ex, sizeof(ex), data, sizeof(data));
    if (join(path, sizeof(path), ex, "header.bin") != 0
        || bw_rom_peek_code(path, code) != 0) {
        code[0] = '\0';
        return BW_GAME_UNKNOWN;
    }
    return bw_setup_game_of_code(code);
}

/* Whether `dir` is an extraction of `game` the install made (not a
 * trusted folder); `playable`: and it can be played as it is. */
static int holds(const char *dir, BwGame game, int playable)
{
    if (dir == NULL || dir[0] == '\0' || bw_setup_game_of(dir, NULL) != game) {
        return 0;
    }
    return !playable || bw_setup_check(dir, 0, NULL, 0) == BW_ROOT_OK;
}

/* <home>/extract (index -1), or <home>/extract-<code> of known dump
 * `index`; -1 past the last. */
static int home_folder(int index, char *out, size_t n)
{
    char home[BW_SETUP_PATH_MAX], name[32];
    const char *code;

    if (bw_setup_home(home, sizeof(home)) != 0) {
        return -1;
    }
    if (index < 0) {
        return join(out, n, home, "extract");
    }
    code = bw_rom_known_code(index);
    if (code == NULL) {
        return -1;
    }
    snprintf(name, sizeof(name), "extract-%s", code);
    return join(out, n, home, name);
}

/* The install's folder of `game`, in bw_setup_find_game's order. */
static int game_folder(BwGame game, int playable, char *out, size_t n)
{
    BwSetupConfig c;
    char dir[BW_SETUP_PATH_MAX];
    int i;

    if (game != BW_GAME_BLACK && game != BW_GAME_WHITE) {
        return -1;
    }
    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    if (holds(c.game_root[game == BW_GAME_WHITE], game, playable)) {
        return copy(out, n, c.game_root[game == BW_GAME_WHITE]);
    }
    if (holds(c.played, game, playable)) {
        return copy(out, n, c.played);
    }
    if (holds(c.data_root, game, playable)) {
        return copy(out, n, c.data_root);
    }
    for (i = -1; home_folder(i, dir, sizeof(dir)) == 0; i++) {
        if (holds(dir, game, playable)) {
            return copy(out, n, dir);
        }
    }
    return -1;
}

int bw_setup_find_game(BwGame game, char *out, size_t n)
{
    return game_folder(game, 1, out, n);
}

int bw_setup_place_for(const char *gamecode, char *out, size_t n)
{
    BwSetupConfig c;
    char dir[BW_SETUP_PATH_MAX], have[5];
    int i;

    if (gamecode == NULL || strlen(gamecode) != 4) {
        return -1;
    }
    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    /* the folder that holds this cartridge already (whatever state it is
     * in: it is made again in place) */
    if (c.data_root[0] != '\0' && bw_setup_game_of(c.data_root, have) != BW_GAME_UNKNOWN
        && strcmp(have, gamecode) == 0) {
        return copy(out, n, c.data_root);
    }
    for (i = -1; home_folder(i, dir, sizeof(dir)) == 0; i++) {
        if (bw_setup_game_of(dir, have) != BW_GAME_UNKNOWN && strcmp(have, gamecode) == 0) {
            return copy(out, n, dir);
        }
    }
    /* a new one: English Black's is <home>/extract unless another
     * cartridge has that folder */
    if (strcmp(gamecode, "IRBO") == 0 && home_folder(-1, dir, sizeof(dir)) == 0) {
        int kind = bw_path_kind_utf8(dir);

        if (kind == BW_PATH_NONE || (kind == BW_PATH_DIR && bw_dir_is_empty_utf8(dir) == 1)
            || (kind == BW_PATH_DIR && bw_setup_game_of(dir, have) == BW_GAME_UNKNOWN
                && bw_setup_check(dir, 0, NULL, 0) != BW_ROOT_OK)) {
            return copy(out, n, dir);
        }
    }
    {
        char home[BW_SETUP_PATH_MAX], name[32];

        if (bw_setup_home(home, sizeof(home)) != 0) {
            return -1;
        }
        snprintf(name, sizeof(name), "extract-%s", gamecode);
        return join(out, n, home, name);
    }
}

int bw_setup_set_current(const char *extract_dir)
{
    BwSetupConfig c;
    char dir[BW_SETUP_PATH_MAX];
    BwGame game;

    if (extract_dir == NULL || extract_dir[0] == '\0') {
        return -1;
    }
    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    if (bw_os_abspath(extract_dir, dir, sizeof(dir)) != 0) {
        (void)copy(dir, sizeof(dir), extract_dir);
    }
    trim_seps(dir);
    game = bw_setup_game_of(dir, NULL);
    if (strcmp(c.played, dir) == 0
        && (game == BW_GAME_UNKNOWN || strcmp(c.game_root[game == BW_GAME_WHITE], dir) == 0)) {
        return 0;       /* nothing to write */
    }
    (void)copy(c.played, sizeof(c.played), dir);
    config_note(&c, game, dir, NULL, NULL);
    return bw_setup_config_save(&c);
}

int bw_setup_remember_game(const char *extract_dir, const char *rom)
{
    BwSetupConfig c;
    char dir[BW_SETUP_PATH_MAX], file[BW_SETUP_PATH_MAX], def[BW_SETUP_PATH_MAX];
    char code[5];
    BwGame game;

    if (extract_dir == NULL || extract_dir[0] == '\0') {
        return -1;
    }
    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    if (bw_os_abspath(extract_dir, dir, sizeof(dir)) != 0) {
        (void)copy(dir, sizeof(dir), extract_dir);
    }
    trim_seps(dir);
    file[0] = '\0';
    if (rom != NULL && bw_os_abspath(rom, file, sizeof(file)) != 0) {
        (void)copy(file, sizeof(file), rom);
    }
    game = bw_setup_game_of(dir, code);
    (void)copy(c.played, sizeof(c.played), dir);
    config_note(&c, game, dir, code, file[0] != '\0' ? file : NULL);
    /* the folder the first two lines mean (the one named there, else the
     * default one): its ROM line follows, as it always did */
    if (c.data_root[0] != '\0') {
        (void)copy(def, sizeof(def), c.data_root);
    } else if (bw_setup_default_extract(def, sizeof(def)) != 0) {
        def[0] = '\0';
    }
    if (def[0] != '\0') {
        char full[BW_SETUP_PATH_MAX];

        if (bw_os_abspath(def, full, sizeof(full)) == 0) {
            (void)copy(def, sizeof(def), full);     /* (the same spelling as `dir`) */
        }
        trim_seps(def);
    }
    if (file[0] != '\0' && def[0] != '\0' && strcmp(def, dir) == 0) {
        (void)copy(c.rom, sizeof(c.rom), file);
    }
    return bw_setup_config_save(&c);
}

/* ------------------------------------------------------------------ */
/* The check                                                           */
/* ------------------------------------------------------------------ */

/* The two manifest fields the check reads.  The manifest is the
 * installer's own small JSON file, so a scan for the keys is enough. */
static int read_manifest(const char *path, long *version, char sha[41])
{
    char buf[4096];
    size_t got;
    const char *p;
    FILE *fp = bw_fopen_utf8(path, "rb");

    *version = -1;
    sha[0] = '\0';
    if (fp == NULL) {
        return -1;
    }
    got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[got] = '\0';
    p = strstr(buf, "\"version\":");
    if (p != NULL) {
        *version = strtol(p + 10, NULL, 10);
    }
    p = strstr(buf, "\"rom_sha1\": \"");
    if (p != NULL && strlen(p + 13) >= 40) {
        memcpy(sha, p + 13, 40);
        sha[40] = '\0';
    }
    return 0;
}

static int has(const char *dir, const char *name, int kind)
{
    char path[BW_SETUP_PATH_MAX];

    return join(path, sizeof(path), dir, name) == 0
           && bw_path_kind_utf8(path) == kind;
}

void bw_setup_split(const char *path, char *extract_dir, size_t ne,
                    char *data_dir, size_t nd)
{
    char p[BW_SETUP_PATH_MAX];

    if (copy(p, sizeof(p), path) != 0) {
        p[0] = '\0';
    }
    trim_seps(p);
    if (!has(p, MANIFEST_NAME, BW_PATH_FILE) && !has(p, "data", BW_PATH_DIR)
        && has(p, "a", BW_PATH_DIR)) {
        /* The NitroFS folder itself (what BW_DATA_ROOT names). */
        parent_of(p, extract_dir, ne);
        (void)copy(data_dir, nd, p);
        return;
    }
    (void)copy(extract_dir, ne, p);
    if (join(data_dir, nd, p, "data") != 0) {
        data_dir[0] = '\0';
    }
}

BwRootState bw_setup_check(const char *path, int trusted,
                           char *why, size_t why_len)
{
    char ex[BW_SETUP_PATH_MAX];
    char data[BW_SETUP_PATH_MAX];
    char mf[BW_SETUP_PATH_MAX];
    char sha[41];
    long version;
    int kind;

    bw_setup_split(path, ex, sizeof(ex), data, sizeof(data));
    if (has(data, "a", BW_PATH_DIR) && trusted) {
        return BW_ROOT_OK;
    }
    kind = bw_path_kind_utf8(ex);
    if (kind == BW_PATH_NONE
        || (kind == BW_PATH_DIR && bw_dir_is_empty_utf8(ex) == 1)) {
        say(why, why_len, "There is no game data in %s yet.", ex);
        return BW_ROOT_MISSING;
    }
    if (join(mf, sizeof(mf), ex, MANIFEST_NAME) != 0
        || read_manifest(mf, &version, sha) != 0) {
        if (trusted) {
            say(why, why_len, "%s has no game data in it (no data%sa folder).",
                ex, BW_SETUP_SEP);
        } else {
            say(why, why_len, "%s holds files, but not a finished eidos "
                "extraction.", ex);
        }
        return BW_ROOT_INCOMPLETE;
    }
    if (version != strtol(BW_INSTALL_VERSION, NULL, 10)) {
        say(why, why_len, "The game data in %s was made by another version of "
            "eidos and has to be made again from your ROM.", ex);
        return BW_ROOT_OLD;
    }
    if (!trusted && !bw_rom_known_sha1(sha)) {
        say(why, why_len, "The game data in %s was made from a ROM that is not "
            "a supported dump of Pokemon Black or White (SHA-1 %s).", ex, sha);
        return BW_ROOT_OTHER_IMAGE;
    }
    if (!has(data, "a", BW_PATH_DIR) || !has(ex, "arm9_dec.bin", BW_PATH_FILE)) {
        say(why, why_len, "Files are missing from the game data in %s; it has "
            "to be made again from your ROM.", ex);
        return BW_ROOT_DAMAGED;
    }
    return BW_ROOT_OK;
}

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

int bw_setup_parse_arg(BwSetupRequest *req, int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    const char **slot = NULL;

    if (strcmp(a, "--data-root") == 0) {
        slot = &req->data_root;
    } else if (strcmp(a, "--rom") == 0) {
        slot = &req->rom;
    } else if (strncmp(a, "--rom=", 6) == 0 && a[6] != '\0') {
        /* --rom=PATH in one argument, for a front end that drops a whole
         * argument when it has no file for it */
        req->rom = a + 6;
        return 1;
    } else if (strcmp(a, "--move-data") == 0) {
        slot = &req->move_to;
    } else if (strcmp(a, "--no-window") == 0) {
        req->no_window = 1;
        return 1;
    } else if (strcmp(a, "--reset-data") == 0) {
        req->reset = 1;
        return 1;
    } else if (strcmp(a, "--game") == 0 || strncmp(a, "--game=", 7) == 0) {
        /* --game black, --game=black: the game to start */
        const char *name = a[6] == '=' ? a + 7 : (*i + 1 < argc ? argv[++*i] : NULL);

        req->game = bw_setup_game_named(name);
        return req->game != BW_GAME_UNKNOWN ? 1 : -1;
    } else {
        return 0;
    }
    if (*i + 1 >= argc) {
        return -1;
    }
    *slot = argv[++*i];
    return 1;
}

const char *bw_setup_usage(void)
{
    return "  --data-root PATH   the game data (an extraction folder, or its data\n"
           "                     folder); default: the saved one, else the\n"
           "                     platform's (see docs/installer.md)\n"
           "  --game NAME        the game to start: black or white; without it the\n"
           "                     one played last\n"
           "  --rom PATH         (or --rom=PATH) make the game data from this ROM,\n"
           "                     no window; each game has its own data, neither\n"
           "                     replaces the other\n"
           "  --no-window        never open the setup window; say what is missing\n"
           "  --reset-data       delete the game data eidos made, then exit\n"
           "  --move-data PATH   move the game data to PATH, then exit\n";
}

int bw_setup_resolve(const BwSetupRequest *req, BwSetupResolved *out)
{
    BwSetupConfig c;
    char env[BW_SETUP_PATH_MAX];
    const char *path = NULL;
    char def[BW_SETUP_PATH_MAX];
    int trusted = 0;

    memset(out, 0, sizeof(*out));
    if (bw_setup_config_load(&c) < 0) {
        memset(&c, 0, sizeof(c));
    }
    (void)copy(out->rom, sizeof(out->rom), c.rom);

    if (req != NULL && req->data_root != NULL && req->data_root[0] != '\0') {
        path = req->data_root;
        out->source = BW_SOURCE_FLAG;
        trusted = 1;
    } else if (bw_os_getenv("BW_DATA_ROOT", env, sizeof(env)) != NULL) {
        path = env;
        out->source = BW_SOURCE_ENV;
        trusted = 1;
    } else if (req != NULL && req->game != BW_GAME_UNKNOWN) {
        /* --game: that game's folder in this install.  None yet: the
         * install decides the folder when it knows the ROM's cartridge
         * (bw_setup_install_game), and until then the place English
         * White or Black would get stands for it in the messages. */
        out->source = BW_SOURCE_GAME;
        if (game_folder((BwGame)req->game, 1, def, sizeof(def)) != 0
            /* (none to play: one whose data has to be made again, which
             * the check below words, and whose ROM may be remembered) */
            && game_folder((BwGame)req->game, 0, def, sizeof(def)) != 0) {
            if (bw_setup_place_for(req->game == BW_GAME_WHITE ? "IRAO" : "IRBO", def, sizeof(def)) != 0) {
                def[0] = '\0';
            }
            bw_setup_split(def, out->extract_dir, sizeof(out->extract_dir),
                           out->data_dir, sizeof(out->data_dir));
            out->state = BW_ROOT_MISSING;
            say(out->why, sizeof(out->why), "There is no game data of Pokemon %s yet.",
                bw_setup_game_title((BwGame)req->game));
            out->rom[0] = '\0';
            return 0;
        }
        path = def;
    } else if (req != NULL && req->follow_last && c.played[0] != '\0'
               && bw_setup_game_of(c.played, NULL) != BW_GAME_UNKNOWN) {
        /* a standalone game: the game played last, in whatever state its
         * folder is (the check below words it) */
        (void)copy(def, sizeof(def), c.played);
        path = def;
        out->source = BW_SOURCE_LAST;
    } else if (c.data_root[0] != '\0') {
        path = c.data_root;
        out->source = BW_SOURCE_CONFIG;
    } else if (bw_setup_default_extract(def, sizeof(def)) == 0) {
        path = def;
        out->source = BW_SOURCE_DEFAULT;
    } else {
        out->state = BW_ROOT_MISSING;
        (void)copy(out->why, sizeof(out->why),
                   "This platform has no default place for the game data; "
                   "pass --data-root.");
        return -1;
    }
    bw_setup_split(path, out->extract_dir, sizeof(out->extract_dir),
                   out->data_dir, sizeof(out->data_dir));
    out->state = bw_setup_check(path, trusted, out->why, sizeof(out->why));
    if (out->state == BW_ROOT_OK && trusted && req != NULL && req->game != BW_GAME_UNKNOWN) {
        /* a folder named for this run, and --game: they have to agree */
        BwGame have = bw_setup_game_of(out->extract_dir, NULL);

        if (have != BW_GAME_UNKNOWN && have != (BwGame)req->game) {
            out->state = BW_ROOT_OTHER_GAME;
            say(out->why, sizeof(out->why), "%s holds Pokemon %s, and --game asks for Pokemon %s.",
                out->extract_dir, bw_setup_game_title(have), bw_setup_game_title((BwGame)req->game));
        }
    }
    if (out->source == BW_SOURCE_GAME || out->source == BW_SOURCE_LAST
        || ((out->source == BW_SOURCE_CONFIG || out->source == BW_SOURCE_DEFAULT) && out->rom[0] == '\0')) {
        /* the ROM this folder's cartridge came from, for a later
         * re-extraction (the first two lines' ROM stands for their folder
         * when it is there) */
        char code[5];
        int i;

        if (out->source == BW_SOURCE_GAME || out->source == BW_SOURCE_LAST) {
            out->rom[0] = '\0';
        }
        if (bw_setup_game_of(out->extract_dir, code) != BW_GAME_UNKNOWN) {
            for (i = 0; i < c.nroms; i++) {
                if (strcmp(c.roms[i].code, code) == 0) {
                    (void)copy(out->rom, sizeof(out->rom), c.roms[i].path);
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Installing without a window                                         */
/* ------------------------------------------------------------------ */

const char *bw_setup_identity(const BwRomInfo *info)
{
    if (info->trimmed && info->verified && info->padded_sha1_hex[0] != '\0') {
        return info->padded_sha1_hex;
    }
    return info->sha1_hex;
}

u64 bw_setup_space_needed(u32 rom_size)
{
    return (u64)rom_size + (u64)16u * 1024u * 1024u;
}

int bw_setup_install(const char *rom, const char *extract_dir,
                     int allow_unverified, int remember,
                     BwProgressFn progress, void *ctx,
                     BwSetupOutcome *outcome, char *msg, size_t msg_len)
{
    BwSetupOutcome local;
    BwRomInfo *info;
    BwExtractOptions opt;
    BwExtractResult res;
    char err[1024];
    u64 free_bytes;
    int rc;

    if (outcome == NULL) {
        outcome = &local;
    }
    memset(outcome, 0, sizeof(*outcome));
    info = &outcome->info;
    outcome->stage = BW_STAGE_IDENTIFY;
    rc = bw_rom_identify(rom, info, progress, ctx, err, sizeof(err));
    if (rc != 0) {
        say(msg, msg_len, "%s", err);
        return rc;
    }
    outcome->stage = BW_STAGE_VERDICT;
    outcome->verdict = bw_rom_verdict(info, err, sizeof(err));
    if (outcome->verdict != BW_VERDICT_OK && !allow_unverified) {
        say(msg, msg_len, "%s", err);
        return -1;
    }
    outcome->stage = BW_STAGE_SPACE;
    if (bw_free_space_utf8(extract_dir, &free_bytes) == 0
        && free_bytes < bw_setup_space_needed(info->rom_size)) {
        char need[32], have[32];
        snprintf(need, sizeof(need), "%lu",
                 (unsigned long)(bw_setup_space_needed(info->rom_size) >> 20));
        snprintf(have, sizeof(have), "%lu", (unsigned long)(free_bytes >> 20));
        if (msg != NULL && msg_len > 0) {
            snprintf(msg, msg_len, "There is not enough free space for the game "
                     "data in %s: %s MB are needed and %s MB are free.",
                     extract_dir, need, have);
        }
        return -1;
    }
    memset(&opt, 0, sizeof(opt));
    /* a known good dump of any cartridge the port plays, else English
     * Black only (or anything, when the player allows it) */
    opt.require_gamecode = allow_unverified || info->verified ? NULL : "IRBO";
    opt.rom_sha1_hex = bw_setup_identity(info);
    opt.replace = 1;        /* the player chose this ROM for this folder */
    outcome->stage = BW_STAGE_EXTRACT;
    rc = bw_rom_extract(rom, extract_dir, &opt, &res, progress, ctx,
                        err, sizeof(err));
    if (rc != 0) {
        if (rc == BW_CANCELLED) {
            say(msg, msg_len, "Cancelled; nothing was written.");
        } else {
            say(msg, msg_len, "The game data could not be written: %s", err);
        }
        return rc;
    }
    outcome->stage = BW_STAGE_DONE;
    outcome->already = res.already;
    if (remember && bw_setup_remember(extract_dir, rom) != 0) {
        say(msg, msg_len, "The game data is ready, but the settings file could "
            "not be written.");
    }
    return 0;
}

int bw_setup_install_game(const char *rom, BwGame want,
                          int allow_unverified, int remember,
                          BwProgressFn progress, void *ctx,
                          BwSetupOutcome *outcome, char *dir_out, size_t dir_len,
                          char *msg, size_t msg_len)
{
    char code[5], dir[BW_SETUP_PATH_MAX];
    BwGame game;

    if (dir_out != NULL && dir_len > 0) {
        dir_out[0] = '\0';
    }
    if (bw_rom_peek_code(rom, code) != 0) {
        /* not readable as a cartridge: the install says why, in its own
         * words, and writes nothing */
        code[0] = '\0';
    }
    game = bw_setup_game_of_code(code);
    if (want != BW_GAME_UNKNOWN && game != BW_GAME_UNKNOWN && game != want) {
        if (outcome != NULL) {
            memset(outcome, 0, sizeof(*outcome));
            outcome->stage = BW_STAGE_VERDICT;
            outcome->other_game = 1;
        }
        say(msg, msg_len, "This ROM is Pokemon %s. Choose your Pokemon %s ROM.",
            bw_setup_game_title(game), bw_setup_game_title(want));
        return -1;
    }
    if (bw_setup_place_for(code[0] != '\0' ? code : "IRBO", dir, sizeof(dir)) != 0) {
        say(msg, msg_len, "This platform has no default place for the game data; "
                          "pass --data-root.");
        return -1;
    }
    if (dir_out != NULL && copy(dir_out, dir_len, dir) != 0) {
        dir_out[0] = '\0';
    }
    {
        int rc = bw_setup_install(rom, dir, allow_unverified, 0, progress, ctx, outcome, msg, msg_len);

        if (rc == 0 && remember && bw_setup_remember_game(dir, rom) != 0) {
            say(msg, msg_len, "The game data is ready, but the settings file could "
                "not be written.");
        }
        return rc;
    }
}

static int stderr_bar(void *ctx, u32 done, u32 total, const char *what)
{
    static int last = -1;
    static int last_hash = -1;
    int hashing = strcmp(what, "hashing") == 0;
    int pct = total ? (int)((u64)done * 100u / total) : 100;

    (void)ctx;
    if (hashing != last_hash) {
        last_hash = hashing;
        last = -1;
    }
    if (pct != last) {
        fprintf(stderr, "\r  %s %3d%%", hashing ? "checking the ROM  " :
                                                  "writing game data ", pct);
        if (pct == 100) {
            fputc('\n', stderr);
        }
        fflush(stderr);
        if (getenv("RECOMPLAUNCHER") != NULL) {
            /* RecompLauncher's setup step reads whole lines from the
             * output: "progress <0 to 1> <text>".  The check is the first
             * half of the bar, the writing the second. */
            printf("progress %.2f %s\n", (hashing ? 0.0 : 0.5) + pct / 200.0,
                   hashing ? "Checking your ROM" : "Writing the game data");
            fflush(stdout);
        }
        last = pct;
    }
    return 0;
}

/* The setup step is done.  A folder that held this ROM's data already has
 * nothing written, so the bar stopped at the end of the check: its last
 * line says the whole of it. */
static void bar_end(void)
{
    if (getenv("RECOMPLAUNCHER") != NULL) {
        printf("progress 1.00 The game data is ready\n");
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/* Reset and move                                                      */
/* ------------------------------------------------------------------ */

/* A staging or set-aside folder of a killed run, if it is recognisably
 * the installer's own. */
static void remove_leftover(const char *base, const char *suffix)
{
    char p[BW_SETUP_PATH_MAX];

    if (snprintf(p, sizeof(p), "%s%s", base, suffix) < (int)sizeof(p)
        && bw_path_kind_utf8(p) == BW_PATH_DIR
        && (has(p, PARTIAL_MARK, BW_PATH_FILE) || has(p, MANIFEST_NAME, BW_PATH_FILE))) {
        (void)bw_remove_tree_utf8(p);
    }
}

int bw_setup_reset(const char *extract_dir, char *msg, size_t msg_len)
{
    BwSetupConfig c;
    char ex[BW_SETUP_PATH_MAX];
    int kind;

    if (copy(ex, sizeof(ex), extract_dir) != 0) {
        return -1;
    }
    trim_seps(ex);
    kind = bw_path_kind_utf8(ex);
    if (kind == BW_PATH_FILE
        || (kind == BW_PATH_DIR && bw_dir_is_empty_utf8(ex) != 1
            && !has(ex, MANIFEST_NAME, BW_PATH_FILE))) {
        say(msg, msg_len, "%s holds files eidos did not write; it was left "
            "alone.  Delete it yourself if you want it gone.", ex);
        return -1;
    }
    if (kind == BW_PATH_DIR && bw_remove_tree_utf8(ex) != 0) {
        say(msg, msg_len, "%s could not be deleted completely (is a file in it "
            "open?).", ex);
        return -1;
    }
    remove_leftover(ex, ".partial");
    remove_leftover(ex, ".old");
    if (bw_setup_config_load(&c) >= 0) {
        c.data_root[0] = '\0';
        c.rom[0] = '\0';
        (void)bw_setup_config_save(&c);
    }
    say(msg, msg_len, kind == BW_PATH_NONE
        ? "There was no game data at %s; the saved settings are cleared."
        : "The game data at %s is deleted; the next start asks for the ROM "
          "again.", ex);
    return 0;
}

int bw_setup_move(const char *from, const char *to, char *msg, size_t msg_len)
{
    BwSetupConfig c;
    char src[BW_SETUP_PATH_MAX];
    char dst[BW_SETUP_PATH_MAX];
    char parent[BW_SETUP_PATH_MAX];
    int kind;

    if (copy(src, sizeof(src), from) != 0 || copy(dst, sizeof(dst), to) != 0) {
        return -1;
    }
    trim_seps(src);
    trim_seps(dst);
    if (!has(src, MANIFEST_NAME, BW_PATH_FILE)) {
        say(msg, msg_len, "There is no eidos game data at %s to move.", src);
        return -1;
    }
    kind = bw_path_kind_utf8(dst);
    if (kind == BW_PATH_FILE
        || (kind == BW_PATH_DIR && bw_dir_is_empty_utf8(dst) != 1)) {
        say(msg, msg_len, "%s is not empty; choose a new or empty folder.",
            dst);
        return -1;
    }
    if (kind == BW_PATH_DIR && bw_remove_tree_utf8(dst) != 0) {
        say(msg, msg_len, "%s cannot be replaced.", dst);
        return -1;
    }
    parent_of(dst, parent, sizeof(parent));
    (void)bw_mkdirs_utf8(parent);
    if (bw_rename_utf8(src, dst) != 0) {
        /* Another volume: make it again there, then delete the old one. */
        char err[512];
        if (bw_setup_config_load(&c) < 0 || c.rom[0] == '\0'
            || bw_path_kind_utf8(c.rom) != BW_PATH_FILE) {
            say(msg, msg_len, "%s cannot be moved to %s directly (another drive?) "
                "and the ROM it came from is not known; start eidos with "
                "--rom <your ROM> --data-root <the new folder> instead.", src, dst);
            return -1;
        }
        if (bw_setup_install(c.rom, dst, 0, 0, stderr_bar, NULL, NULL, err, sizeof(err)) != 0) {
            say(msg, msg_len, "%s", err);
            return -1;
        }
        if (bw_remove_tree_utf8(src) != 0) {
            fprintf(stderr, "eidos: the old game data at %s could not be "
                            "deleted; delete it yourself\n", src);
        }
    }
    if (bw_setup_remember(dst, NULL) != 0) {
        say(msg, msg_len, "The game data is now at %s, but the settings file "
            "could not be written; pass --data-root %s.", dst, dst);
        return -1;
    }
    say(msg, msg_len, "The game data is now at %s.", dst);
    return 0;
}

/* ------------------------------------------------------------------ */
/* First run without a window                                          */
/* ------------------------------------------------------------------ */

static const char *source_name(int s)
{
    switch (s) {
    case BW_SOURCE_FLAG:   return "--data-root";
    case BW_SOURCE_ENV:    return "BW_DATA_ROOT";
    case BW_SOURCE_CONFIG: return "the saved settings";
    case BW_SOURCE_GAME:   return "--game";
    case BW_SOURCE_LAST:   return "the game played last";
    default:               return "the default place";
    }
}

int bw_setup_prepare(const BwSetupRequest *req, BwSetupResolved *out)
{
    char made[BW_SETUP_PATH_MAX];       /* the folder --rom made here */
    char msg[1024];
    BwSetupRequest none;

    made[0] = '\0';
    if (req == NULL) {
        memset(&none, 0, sizeof(none));
        req = &none;
    }
    if (bw_setup_resolve(req, out) != 0) {
        fprintf(stderr, "eidos: %s\n", out->why);
        return BW_PREPARE_FAILED;
    }
    if (req->reset) {
        int rc = bw_setup_reset(out->extract_dir, msg, sizeof(msg));
        fprintf(rc == 0 ? stdout : stderr, "eidos: %s\n", msg);
        return rc == 0 ? BW_PREPARE_EXIT_OK : BW_PREPARE_FAILED;
    }
    if (req->move_to != NULL) {
        int rc = bw_setup_move(out->extract_dir, req->move_to, msg, sizeof(msg));
        fprintf(rc == 0 ? stdout : stderr, "eidos: %s\n", msg);
        return rc == 0 ? BW_PREPARE_EXIT_OK : BW_PREPARE_FAILED;
    }
    if (out->state == BW_ROOT_OTHER_GAME) {
        fprintf(stderr, "eidos: %s\n", out->why);
        return BW_PREPARE_FAILED;
    }
    if (req->rom != NULL && out->source != BW_SOURCE_FLAG && out->source != BW_SOURCE_ENV) {
        /* the install's own data: each cartridge in its own folder, so a
         * second game's ROM never replaces the first game's data */
        char dir[BW_SETUP_PATH_MAX];

        printf("eidos: making the game data from %s\n", req->rom);
        fflush(stdout);
        if (bw_setup_install_game(req->rom, (BwGame)req->game, 0, 1, stderr_bar, NULL,
                                  NULL, dir, sizeof(dir), msg, sizeof(msg)) != 0) {
            fprintf(stderr, "eidos: %s\n", msg);
            return BW_PREPARE_FAILED;
        }
        printf("eidos: the game data is in %s\n", dir);
        fflush(stdout);
        bar_end();
        (void)copy(made, sizeof(made), dir);
    } else if (req->rom != NULL) {
        char code[5];

        /* a folder named for this run: the ROM's data goes there, as asked */
        if (req->game != BW_GAME_UNKNOWN && bw_rom_peek_code(req->rom, code) == 0
            && bw_setup_game_of_code(code) != BW_GAME_UNKNOWN
            && bw_setup_game_of_code(code) != (BwGame)req->game) {
            fprintf(stderr, "eidos: this ROM is Pokemon %s, and --game asks for Pokemon %s.\n",
                    bw_setup_game_title(bw_setup_game_of_code(code)),
                    bw_setup_game_title((BwGame)req->game));
            return BW_PREPARE_FAILED;
        }
        printf("eidos: making the game data in %s from %s\n",
               out->extract_dir, req->rom);
        fflush(stdout);
        if (bw_setup_install(req->rom, out->extract_dir, 0,
                             out->source != BW_SOURCE_ENV, stderr_bar, NULL,
                             NULL, msg, sizeof(msg)) != 0) {
            fprintf(stderr, "eidos: %s\n", msg);
            return BW_PREPARE_FAILED;
        }
        bar_end();
    } else if (out->state == BW_ROOT_OLD && out->rom[0] != '\0'
               && bw_path_kind_utf8(out->rom) == BW_PATH_FILE && req->no_window) {
        printf("eidos: %s\neidos: making it again from %s\n", out->why, out->rom);
        fflush(stdout);
        if (bw_setup_install(out->rom, out->extract_dir, 0,
                             out->source != BW_SOURCE_ENV, stderr_bar, NULL,
                             NULL, msg, sizeof(msg)) != 0) {
            fprintf(stderr, "eidos: %s\n", msg);
            return BW_PREPARE_FAILED;
        }
        bar_end();
    }
    if (req->rom != NULL || out->state != BW_ROOT_OK) {
        BwSetupRequest again = *req;
        again.rom = NULL;
        if (made[0] != '\0') {
            again.data_root = made;     /* this start plays what it made */
        }
        if (bw_setup_resolve(&again, out) != 0) {
            return BW_PREPARE_FAILED;
        }
    }
    if (out->state == BW_ROOT_OK) {
        return BW_PREPARE_READY;
    }
    if (!req->no_window) {
        return BW_PREPARE_WIZARD;
    }
    if (out->source == BW_SOURCE_GAME) {
        fprintf(stderr,
                "eidos: %s\n"
                "eidos: start eidos with --rom <your Pokemon %s ROM> to make it.\n",
                out->why, bw_setup_game_title((BwGame)req->game));
        return BW_PREPARE_FAILED;
    }
    fprintf(stderr,
            "eidos: no playable game data (looked in %s, from %s).\n"
            "eidos: %s\n"
            "eidos: start eidos with --rom <your Pokemon Black or White ROM> to make it, or run\n"
            "         eidos_extract <your Pokemon Black or White ROM> \"%s\"\n",
            out->extract_dir, source_name(out->source), out->why, out->extract_dir);
    return BW_PREPARE_FAILED;
}
