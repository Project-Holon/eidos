/*
 * sdk/mod_v6.h -- the port side of the services mod API version 6 added
 * (docs/mod-services.md: "Members in memory", "Text entries", "Mod
 * files", "Work variables and badges", "Field scripts", "The type chart",
 * "Text input", "Other players in the field").
 *
 * Each call that a mod reaches through its tables works for the mod that
 * is calling: the loader sets the namespace around every call into a mod
 * (sdk/mod_savedata.c, bw_mod_savedata_namespace), and the mod's folder is
 * found from it (sdk/mod_loader.h, bw_mod_loader_root_of).  The `_for`
 * forms take the mod's folder and namespace directly, for the tests and
 * the port's own tools.
 */

#ifndef BW_PORT_MOD_V6_H
#define BW_PORT_MOD_V6_H

#include "types.h"
#include "mod_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BW_PORT_WEAK
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif
#endif

/* ---- Members in memory (sdk/mod_vfs.c) ---------------------------- */

/* The limits: one mod's memory layer, and every mod's together. */
#define BW_MOD_VFS_MOD_MAX   (16u * 1024u * 1024u)
#define BW_MOD_VFS_TOTAL_MAX (32u * 1024u * 1024u)

int bw_mod_vfs_member_set(const char *archive, u32 index, const void *data, u32 size);
int bw_mod_vfs_member_clear(const char *archive, u32 index);
int bw_mod_vfs_member_get(const char *archive, u32 index, void *out, u32 size);

int bw_mod_vfs_member_set_for(const char *root, const char *archive, u32 index,
                              const void *data, u32 size);
int bw_mod_vfs_member_clear_for(const char *root, const char *archive, u32 index);

/* The swaps queued for archives that are open: done between frames. */
void bw_mod_vfs_frame(void);
u32  bw_mod_vfs_pending(void);

/* ---- Text entries (sdk/mod_vfs.c) --------------------------------- */

int bw_mod_text_get(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
int bw_mod_text_set(u32 archive, u32 file, u32 entry, u32 block, const u16 *units, u32 count);
u32 bw_mod_text_language(void);
int bw_mod_text_set_for(const char *root, u32 archive, u32 file, u32 entry, u32 block,
                        const u16 *units, u32 count);

/*
 * The bank helpers the text calls use, for the tests: read one entry of
 * one block (units without the end code; the count, or -1), and the bank
 * with one entry set (any block count; an entry past the end grows every
 * block with empty lines).  0 with a malloc'd *out, or -1.
 */
int bw_msg_bank_get(const u8 *bank, u32 size, u32 entry, u32 block, u16 *out, u32 max_units);
int bw_msg_bank_set(const u8 *bank, u32 size, u32 entry, u32 block,
                    const u16 *units, u32 count, u8 **out, u32 *out_size);

/* ---- Mod files (sdk/mod_file.c) ----------------------------------- */

#define BW_MOD_FILE_QUOTA    (16u * 1024u * 1024u)
#define BW_MOD_FILE_NAME_MAX 64u

int bw_mod_file_write(const char *name, const void *data, u32 size);
int bw_mod_file_read(const char *name, void *out, u32 size, u32 *got);
int bw_mod_file_list(char *out, u32 size);
int bw_mod_file_delete(const char *name);

/* The folder the mod files live under (default: <save dir>/modfiles);
 * a test points it at a scratch folder.  NULL restores the default. */
void bw_mod_file_set_base(const char *dir);
/* A name a mod may use: 1 or 0. */
int  bw_mod_file_name_ok(const char *name);
int  bw_mod_file_write_for(const char *ns, const char *name, const void *data, u32 size);
int  bw_mod_file_read_for(const char *ns, const char *name, void *out, u32 size, u32 *got);
int  bw_mod_file_list_for(const char *ns, char *out, u32 size);
int  bw_mod_file_delete_for(const char *ns, const char *name);

/* ---- Work variables, badges, field scripts (sdk/mod_v6.c) --------- */

int  bw_mod_v6_work_get(u16 id, u16 *out);
int  bw_mod_v6_work_set(u16 id, u16 value);
int  bw_mod_v6_badge_set(u32 bit);
int  bw_mod_v6_game_code(char *out, u32 size);
/* The field VM's dispatch names the command work of the command it runs
 * (NULL after it); returns the one before, to restore. */
void *bw_mod_v6_set_field_cmd_work(void *cmd_work);
int  bw_mod_v6_script_run(u16 script_id, u32 *ticket);
int  bw_mod_v6_script_run_for(const char *ns, u16 script_id, u32 *ticket);
/* The scripts queued or running. */
u32  bw_mod_v6_script_pending(void);

/* ---- Text input (sdk/mod_v6.c) ------------------------------------ */

int  bw_mod_v6_text_input(const char *title, u32 max_units, u32 kind, u32 *ticket);
/* The services table's player_name: the multiplayer name, "" while unset. */
int  bw_mod_v6_player_name(char *out, u32 size);
int  bw_mod_v6_text_input_for(const char *ns, const char *title, u32 max_units, u32 kind,
                              u32 *ticket);
u32  bw_mod_v6_text_input_pending(void);

/* ---- The type chart (sdk/mod_v6.c) -------------------------------- */

int  bw_mod_v6_type_chart_set(const BwTypeChart *chart);
int  bw_mod_v6_type_chart_get(BwTypeChart *out);
/* NULL as the chart takes the mod's chart away. */
int  bw_mod_v6_type_chart_set_for(const char *ns, const BwTypeChart *chart);
/* From the overlay loader: an overlay's data is in place. */
void bw_mod_v6_overlay_loaded(u32 id);

/* ---- World sessions, the Archipelago link (sdk/mod_world.c) ------- */

int  bw_mod_world_create(const char *config_json, u32 *ticket);
int  bw_mod_world_owner(const char *code, const char *owner_key, u32 action);
int  bw_mod_world_join(const char *code, const char *name_or_key, u32 *ticket);
int  bw_mod_world_checks(const u32 *location_ids, u32 count);
void bw_mod_world_leave(void);
int  bw_mod_world_read(void *out, u32 size);
int  bw_mod_ap_link(const char *room, const char *slot, const char *password, u32 *ticket);
int  bw_mod_ap_send(const char *json, u32 size);
int  bw_mod_ap_read(void *out, u32 size);
int  bw_mod_world_peer_position(int on);
/* Once a frame: the queued messages fire their events. */
void bw_mod_world_frame(void);
/* For the tests: a frame as if metaxy sent it (the reader's hook). */
void bw_mod_world_feed(char type, const u8 *payload, u32 size);

/* ---- Town Map markers (sdk/mod_world.c) --------------------------- */

typedef struct BwTownmapMarker {
    int  used;
    u32  id;
    u16  zone;
    u32  icon;
    char name[64];
} BwTownmapMarker;

int  bw_mod_townmap_marker_set(u32 id, u16 zone, u32 icon, const char *name);
int  bw_mod_townmap_marker_clear(u32 id);
/* The n-th marker set, for the drawing sides; NULL past the last. */
const BwTownmapMarker *bw_mod_townmap_marker_at(u32 n);

/* ---- The loader's way down ---------------------------------------- */

/* Everything a mod holds in the v6 services goes when it stops. */
void bw_mod_v6_remove_for(const char *ns, const char *root);
/* Once a frame, from bw_mod_frame_tick. */
void bw_mod_v6_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_V6_H */
