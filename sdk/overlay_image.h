/*
 * sdk/overlay_image.h -- overlays compiled into the executable: their code
 * under a per-overlay namespace, their data copied into the window at load.
 * docs/overlays.md is the design; tools/ovlink.py writes the tables.
 *
 * What a compiled overlay is, at run time:
 *
 *   - its functions, renamed ovNNN_<name> by tools/ovlink.py's forced-include
 *     headers, so overlays that share a RAM window link side by side;
 *   - a BwDataImage (sdk/armdata.h) describing its window: the file's bytes
 *     copied to [ram_address, ram_address + ram_size), the .bss zeroed, the
 *     overlay's own data names bound and its pointer words relocated, all at
 *     load (FS_LoadOverlayImage), because the window holds whichever overlay
 *     was loaded last;
 *   - stubs for every function of the overlay the build lacks, which panic
 *     naming the overlay and the address when reached.
 *
 * Code outside an overlay reaches a function inside a window through a
 * trampoline (ovl_dispatch.c), which asks bw_ovl_resident_at which overlay
 * is there.
 *
 * The bytes come from the player's own extract: overlays_dec/y9_NNNN.bin
 * for a compressed overlay, overlays/y9_NNNN.bin for one stored plain, next
 * to arm9_dec.bin (the data root's parent).  Nothing of the game is in the
 * repository.
 */

#ifndef BW_PORT_OVERLAY_IMAGE_H
#define BW_PORT_OVERLAY_IMAGE_H

#include "types.h"
#include "armdata.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One compiled overlay (tools/ovlink.py, ovNNN_link.c). */
typedef struct BwOvlImage {
    u32                id;
    const BwDataImage *data;
    const BwDataFn    *sinits;          /* static initialisers, in table order */
    u32                sinit_count;
    u32                defined_count;   /* functions compiled from matched C   */
    u32                stub_count;      /* functions the build stands a stub in */
    const BwDataFunc  *entries;         /* every function (and stub) by the value
                                           a code pointer to it holds, Thumb bit
                                           set; sorted                          */
    u32                entry_count;
} BwOvlImage;

/* The overlays one executable carries (ovl_dispatch.c, bw_ovl_set_<name>). */
typedef struct BwOvlImageSet {
    const BwOvlImage *const *images;
    u32                      count;
} BwOvlImageSet;

/*
 * Make `set` the overlays this executable carries (NULL: none) and connect
 * them to the overlay loader (sdk/overlay.c): from now on FS_LoadOverlay of
 * one of them copies its image, binds and relocates it, and FS_StartOverlay
 * runs its static initialisers.  `flags` takes BW_DATA_ALLOW_UNRELOCATED:
 * where pointers are eight bytes the image is copied and bound but its
 * pointer words keep their DS addresses (sdk/armdata.h, "Pointer width");
 * without the flag that is a panic at load.
 */
void bw_ovl_install(const BwOvlImageSet *set, unsigned flags);

/* The compiled overlay `id`, or NULL. */
const BwOvlImage *bw_ovl_image(u32 id);

/*
 * The overlay resident over DS address `addr`: the id of a loaded overlay
 * whose window [ram_address, ram_address + ram_size + bss_size) contains
 * it, or -1.  What every trampoline asks.
 */
int bw_ovl_resident_at(u32 addr);

/*
 * The host function for a code pointer into an overlay window, `value` as
 * the DS stores it (Thumb bit set for a Thumb function), when exactly one
 * compiled overlay has a function there; NULL otherwise (none, or two
 * overlays sharing the window that both do: that pointer needs a
 * trampoline).  sdk/armdata.c's scan relocates the ARM9 data's pointers
 * into windows with it (the save block table points into ov010).
 */
BwDataFn bw_ovl_code_entry(u32 value);

/* Whether any compiled overlay has a function at `value` (its Thumb bit
 * ignored), resident or not: the movable-memory data scan (sdk/armdata.c)
 * leaves such a word as the DS code address it is. */
int bw_ovl_is_code_entry(u32 value);

/*
 * Put the hooks in the overlays' own stored code pointers again.
 *
 * A word in an overlay's data that holds the address of one of that
 * overlay's own functions is called through, so it does not pass a call
 * site and the slot the build puts at the call sites never sees it
 * (docs/mod-interface.md, section 2).  The overlay's own data asks the
 * registry when the overlay loads, and the overlay loads after the mod
 * layer has loaded, so the registry already holds the hooks then.  This
 * asks it again for the words the port rewrote, for a hook installed (or
 * taken out) after the overlay loaded: the player changes the mod list in
 * game.  It is the overlay's own version of bw_arm9_resolve_code_pointers
 * (sdk/armdata.h), which the mod loader calls for the ARM9's words.
 *
 * A word the game has written over since is left alone.  Returns how many
 * words it changed.
 */
u32 bw_ovl_resolve_code_pointers(void);

/*
 * The overlay file's bytes [offset, offset + size), or NULL when the file
 * is missing or shorter.  Read once per overlay and kept.
 */
const void *bw_ovl_file_bytes(u32 id, u32 offset, u32 size);

/*
 * Use `size` bytes at `bytes` as overlay `id`'s file instead of the
 * extract's (copied).  For tests with a synthetic image; NULL clears it.
 */
void bw_ovl_set_file(u32 id, const void *bytes, u32 size);

/* Why a trampoline could not forward. */
enum {
    BW_OVL_WHY_NONE = 0,         /* no overlay is resident over the address    */
    BW_OVL_WHY_NOT_COMPILED = 1, /* the resident overlay is not in this build  */
    BW_OVL_WHY_SIGNATURE = 2     /* its function there has another prototype   */
};

/* The panics the generated code calls; none of them returns. */
void bw_ovl_stub(u32 id, u32 addr);
void bw_ovl_arm9_stub(u32 addr, const char *name);
void bw_ovl_dispatch_fail(u32 addr, const char *name, int resident, int why);

/* mwcc's builtins, for the matched files that call them undeclared. */
void  bw_mwcc_clear(void *dst, unsigned int size);
void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_OVERLAY_IMAGE_H */
