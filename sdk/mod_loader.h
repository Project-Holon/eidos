/*
 * sdk/mod_loader.h -- the port's loader (docs/mod-implementation.md,
 * section 3).
 *
 * The loader opens a mod's library, checks the versions, calls the mod's
 * start-up with the host table, and installs the mod's overrides.  On the
 * way down it reverts the slots the mod installed, calls the mod's stop and
 * unloads the library.
 *
 * The load point is after the save is chosen and before the game runs; the
 * stop is on exit and when the player disables a mod.
 */

#ifndef BW_PORT_MOD_LOADER_H
#define BW_PORT_MOD_LOADER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load every mod that has a `code` key, in load order.  Returns how many
 * were loaded.  A mod the port refuses (a version that does not match, a
 * manifest that cannot be read) is reported and not loaded.
 */
int bw_mod_loader_load_all(void);

/*
 * Stop and unload every loaded mod, the last loaded first: the mod's state
 * is stored, its slots are reverted, its stop is called and its library is
 * unloaded, in that order.
 */
void bw_mod_loader_stop_all(void);

/*
 * Stop and unload one loaded mod, the one at `index` in load order.  This
 * is the path the player takes when they disable a mod.  Returns 0, or -1
 * when there is no such mod.
 */
int bw_mod_loader_stop(u32 index);

/* How many mods are loaded. */
u32 bw_mod_loader_count(void);

/* A loaded mod's name, in load order, or NULL. */
const char *bw_mod_loader_name_at(u32 index);

/* A loaded mod's state namespace, in load order, or NULL. */
const char *bw_mod_loader_namespace_at(u32 index);

/* The folder of the loaded mod with namespace `ns`, or NULL. */
const char *bw_mod_loader_root_of(const char *ns);

/* The multiplayer compatibility number of the loaded mod at `index`
 * (manifest `multiplayer = <n>`), or 0 when it states none. */
int bw_mod_loader_multiplayer_at(u32 index);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_LOADER_H */
