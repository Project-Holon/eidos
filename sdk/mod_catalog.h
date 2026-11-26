/*
 * sdk/mod_catalog.h -- the mods a player can get: the katalogos index, read
 * and installed by the game itself (sdk/mod_catalog.c).
 *
 * What this file is
 * -----------------
 *
 * The model behind the "available" half of the game's own mod manager
 * (docs/distribution.md, "Mods without the launcher").  The launcher does
 * the same work for its profiles (launcher/src-tauri/src/catalog.rs), and
 * the two agree on every rule, so a mod one of them installed is the same
 * mod to the other:
 *
 *   - the index is one JSON file (katalogos's index.json); each mod in it
 *     has a release with `mod.json` (what the list shows, the package's
 *     size and SHA-256) and `mod.bw` (the package);
 *   - a mod is listed only when its mod.json reads, names the index's own
 *     mod, and the mod works with this game: the loaded game (Black or
 *     White), the mod interface this eidos speaks, and code mods only where
 *     the build can run them;
 *   - an install downloads mod.json and mod.bw, refuses the package when
 *     its size or SHA-256 differ from mod.json or when it breaks the
 *     package limits (sdk/mod_bw.h), unpacks it beside the installed mods
 *     and moves it into place, with a `katalogos.json` in the folder that
 *     says which mod and version it is.
 *
 * Threads
 * -------
 *
 * Reading the index and installing wait on the network, so they run on a
 * worker thread; the unpacking runs on the thread beside the game
 * (bw_thread_start_beside), because the wasm32 build's threads take turns
 * and a large package would stop the game.  Every call below is made on the
 * game's thread and returns at once; the screen asks for the state each
 * frame.  What the worker found reaches the lists here only inside
 * bw_mod_catalog_poll, on the game's thread, so the text a call returns
 * stays as it is until the next poll.
 */

#ifndef BW_PORT_MOD_CATALOG_H
#define BW_PORT_MOD_CATALOG_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many mods of the index the list holds. */
#define BW_MOD_CATALOG_MAX 96u

/* The index the game reads (EIDOS_MOD_INDEX names another one). */
#define BW_MOD_CATALOG_INDEX "https://raw.githubusercontent.com/Project-Holon/katalogos/main/index.json"

/* Where a mod's release files are (EIDOS_MOD_RELEASES names another
 * server, for tests). */
#define BW_MOD_CATALOG_RELEASES "https://github.com"

/* The index repository: an official mod lives in it (mods/<id>) and its id
 * is "katalogos/<id>". */
#define BW_MOD_CATALOG_REPO "Project-Holon/katalogos"

/* The file an installed mod's folder carries, as the launcher writes it:
 * {"id": ..., "version": ...}. */
#define BW_MOD_CATALOG_INSTALLED "katalogos.json"

/* The largest index or mod.json read, and the largest package taken (the
 * package limit, sdk/mod_bw.h). */
#define BW_MOD_CATALOG_JSON_MAX    (1u << 20)
#define BW_MOD_CATALOG_PACKAGE_MAX (4ull << 30)

/* The list's state. */
enum {
    BW_MOD_CATALOG_NONE = 0,            /* not asked for yet              */
    BW_MOD_CATALOG_READING,             /* the worker reads the index      */
    BW_MOD_CATALOG_READY,               /* the list is there               */
    BW_MOD_CATALOG_FAILED               /* it could not be read (the error) */
};

/* An install's state. */
enum {
    BW_MOD_CATALOG_JOB_NONE = 0,
    BW_MOD_CATALOG_JOB_DOWNLOADING,     /* got and total say how far       */
    BW_MOD_CATALOG_JOB_UNPACKING,
    BW_MOD_CATALOG_JOB_DONE,            /* installed; bw_mod_catalog_finish next */
    BW_MOD_CATALOG_JOB_FAILED           /* the error says why              */
};

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

/*
 * Ask for the list: the worker starts reading the index.  Nothing happens
 * while it already reads, or while a list read less than `fresh_s` seconds
 * ago is there (0: read again whatever its age).
 */
void bw_mod_catalog_read(unsigned fresh_s);

/*
 * Once a frame while the screen is open: take what the worker finished.
 * Returns the list's state.
 */
int bw_mod_catalog_poll(void);

/* The list's state without taking anything, and the line that says why it
 * failed. */
int bw_mod_catalog_state(void);
const char *bw_mod_catalog_error(void);

/* The mods of the index that work with this game, official ones first,
 * then by name. */
u32 bw_mod_catalog_count(void);
const char *bw_mod_catalog_id_at(u32 index);
const char *bw_mod_catalog_name_at(u32 index);
const char *bw_mod_catalog_author_at(u32 index);
const char *bw_mod_catalog_version_at(u32 index);
const char *bw_mod_catalog_about_at(u32 index);
unsigned long long bw_mod_catalog_size_at(u32 index);

/* How many mods of the index were left out because they do not work with
 * this game (the other game's, a newer mod interface, code where the build
 * runs none), and how many could not be read. */
u32 bw_mod_catalog_unfit(void);
u32 bw_mod_catalog_unread(void);

/*
 * Is the mod at `index` installed?  0: no.  1: yes, this version.  2: yes,
 * another version (the index has an update).  The installed mods folder is
 * read when the list arrives and after every install or removal, not at
 * each call.
 */
int bw_mod_catalog_installed_at(u32 index);

/* The folder the mod at `index` is installed in (the mod menu's root of
 * that mod, bw_mod_menu_root_at), or "" when it is not installed. */
const char *bw_mod_catalog_root_at(u32 index);

/* ------------------------------------------------------------------ */
/* Installing                                                          */
/* ------------------------------------------------------------------ */

/*
 * Install the mod at `index`, or update it.  An installed copy that runs is
 * switched off first (the mod menu's model, sdk/mod_menu.h: its files are
 * about to be replaced), so this is a call for the game's own thread at a
 * point where the mod list may change.  0 when the work started; -1 when
 * another install runs or there is no such mod.
 */
int bw_mod_catalog_install(u32 index);

/* The install's state (BW_MOD_CATALOG_JOB_*); with it the mod's place in
 * the list (-1 when the list changed since) and the package's bytes so far
 * and in all (0: not known yet).  Any of the three may be NULL. */
int bw_mod_catalog_job(int *index, unsigned long long *got, unsigned long long *total);
const char *bw_mod_catalog_job_error(void);

/*
 * After BW_MOD_CATALOG_JOB_DONE, on the game's thread: the mod menu reads
 * the installed folder again and applies it, so the new mod is loaded (a
 * new mod joins the list switched on; an updated one gets the switch it
 * had).  After a failure it puts back the switch of the copy that was
 * switched off.  Either way the job's state goes back to none.  0, or -1
 * when the reload failed.
 */
int bw_mod_catalog_finish(void);

/* Stop what the worker does (the screen closes).  The worker ends at its
 * next step; a download half done is thrown away. */
void bw_mod_catalog_stop(void);

/* 1 while the worker runs (a read or an install, or one that was stopped
 * and has not ended yet). */
int bw_mod_catalog_busy(void);

/* ------------------------------------------------------------------ */
/* The installed mods                                                  */
/* ------------------------------------------------------------------ */

/*
 * What the folder `root` says it was installed as: the katalogos id and the
 * version.  1, or 0 when the folder carries no katalogos.json (a mod the
 * player put there by hand).
 */
int bw_mod_catalog_installed_as(const char *root, char *id, size_t id_size,
                                char *version, size_t version_size);

/* 1 when `root` is a folder of the installed mods folder itself, so the
 * manager may remove it.  (A mod named by BW_MOD_ROOT, or a `.bw` file's
 * cache, is not.) */
int bw_mod_catalog_can_remove(const char *root);

/*
 * Remove the installed mod in `root`: it is switched off and unloaded
 * first, then its folder is deleted, then the mod menu reads the folder
 * again.  The mod's saved data (docs/mod-state.md) stays.  A call for the
 * game's own thread.  0, or -1 (not a removable folder, or a file of it is
 * held open; the line bw_mod_catalog_job_error says which).
 */
int bw_mod_catalog_remove(const char *root);

/* ------------------------------------------------------------------ */
/* The rules, as plain functions (the tests read them)                 */
/* ------------------------------------------------------------------ */

/* One line of the index. */
typedef struct BwCatalogEntry {
    char official[52];                  /* "mod": an official mod's id, or ""  */
    char repo[104];                     /* "repo": owner/name, or ""           */
    char version[24];                   /* "version": x.y.z, or ""             */
    int  unofficial;                    /* "unofficial": true, with "mod"      */
    int  marked_official;               /* "official": true, with "repo"       */
} BwCatalogEntry;

/* A mod.json, the parts the game uses. */
typedef struct BwCatalogMod {
    char id[108];
    char name[64];
    char author[64];
    char version[24];
    char about[400];
    char sha256[65];
    unsigned long long size;            /* the package's bytes             */
    u32  files;                         /* the files it unpacks to         */
    int  games;                         /* BW_MOD_GAME_* bits (sdk/vfs.h)  */
    int  api;                           /* the mod interface it needs, 0: none stated */
    int  has_code;
    int  official;                      /* the badge: hosted in the index repository,
                                         * or as the index's marks say */
} BwCatalogMod;

/*
 * The index's entries.  0, or -1 with the reason in `err` (not JSON, not
 * format 1).  An entry of a shape the index does not allow is left out
 * and counted in `*bad`.
 */
int bw_mod_catalog_parse_index(const char *text, size_t size, BwCatalogEntry *out,
                               u32 max, u32 *count, u32 *bad, char *err, size_t err_size);

/* The id the entry's mod.json must carry: "katalogos/<id>", or the
 * repository in lower case. */
void bw_mod_catalog_entry_id(const BwCatalogEntry *e, char *out, size_t size);

/* The address of a release file of the entry ("mod.json", "mod.bw") under
 * `base` (BW_MOD_CATALOG_RELEASES).  0, or -1 when it does not fit. */
int bw_mod_catalog_entry_url(const BwCatalogEntry *e, const char *base, const char *file,
                             char *out, size_t size);

/*
 * A mod.json, read and checked as the launcher checks it (katalogos's
 * verify): format 1, the entry's own id, repo, id and path in agreement,
 * an x.y.z version, a name and a description, games of black and white
 * only, the package named mod.bw with a size and a SHA-256.  0, or -1 with
 * the reason in `err`.
 */
int bw_mod_catalog_parse_mod(const char *text, size_t size, const BwCatalogEntry *e,
                             BwCatalogMod *out, char *err, size_t err_size);

/*
 * Does the mod work with this game?  `game`: the loaded game's bit
 * (bw_mod_game_loaded; 0 when not known: both fit), `api`: the interface
 * this eidos speaks, `code`: 1 when the build runs code mods.
 */
int bw_mod_catalog_fits(const BwCatalogMod *m, int game, int api, int code);

/* The folder name a mod's name gives, as the launcher makes it: the
 * characters Windows refuses become '_', dots and spaces at the ends go,
 * 60 characters at most, "mod" when nothing is left. */
void bw_mod_catalog_folder_name(const char *name, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_CATALOG_H */
