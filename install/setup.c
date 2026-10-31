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
    int bad;

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

int bw_setup_remember(const char *extract_dir, const char *rom)
{
    BwSetupConfig c;

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
    } else if (strcmp(a, "--move-data") == 0) {
        slot = &req->move_to;
    } else if (strcmp(a, "--no-window") == 0) {
        req->no_window = 1;
        return 1;
    } else if (strcmp(a, "--reset-data") == 0) {
        req->reset = 1;
        return 1;
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
           "  --rom PATH         make the game data from this ROM, no window\n"
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
        last = pct;
    }
    return 0;
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
    default:               return "the default place";
    }
}

int bw_setup_prepare(const BwSetupRequest *req, BwSetupResolved *out)
{
    char msg[1024];
    BwSetupRequest none;

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
    if (req->rom != NULL) {
        printf("eidos: making the game data in %s from %s\n",
               out->extract_dir, req->rom);
        fflush(stdout);
        if (bw_setup_install(req->rom, out->extract_dir, 0,
                             out->source != BW_SOURCE_ENV, stderr_bar, NULL,
                             NULL, msg, sizeof(msg)) != 0) {
            fprintf(stderr, "eidos: %s\n", msg);
            return BW_PREPARE_FAILED;
        }
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
    }
    if (req->rom != NULL || out->state != BW_ROOT_OK) {
        BwSetupRequest again = *req;
        again.rom = NULL;
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
    fprintf(stderr,
            "eidos: no playable game data (looked in %s, from %s).\n"
            "eidos: %s\n"
            "eidos: start eidos with --rom <your Pokemon Black ROM> to make it, or run\n"
            "         eidos_extract <your Pokemon Black ROM> \"%s\"\n",
            out->extract_dir, source_name(out->source), out->why, out->extract_dir);
    return BW_PREPARE_FAILED;
}
