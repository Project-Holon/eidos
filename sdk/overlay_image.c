/*
 * sdk/overlay_image.c -- loading an overlay that is compiled into the
 * executable: its file copied into its RAM window, its names bound, its
 * pointers relocated; and the run-time half of tools/ovlink.py's
 * trampolines and stubs.  docs/overlays.md is the design.
 *
 * Why this exists.  On the DS, FS_LoadOverlay copies the overlay's file to
 * its RAM window, zeroes the .bss after it and runs its static
 * initialisers; the code then runs from the window.  In the port the code
 * is host code (renamed per overlay by tools/ovlink.py so overlays sharing
 * a window link side by side), but the data stays where the DS has it: the
 * window in the port's flat main RAM (sdk/dsmem.c), the same model as the
 * ARM9 image's data (docs/armdata.md).  So a load is, per BwOvlImage:
 *
 *   1. bw_data_load: copy the overlay's file to [ram_address, +ram_size),
 *      zero the .bss, fill its host objects;
 *   2. bw_data_bind: point every data name the overlay's sources use at its
 *      address (the overlay's own pointer variables, bw_dsref_ovNNN_*);
 *   3. bw_data_relocate: rewrite the pointer words the metadata lists, to
 *      the overlay's own functions, the ARM9's, a trampoline, or data.
 *
 * Unloading needs nothing: the window keeps its bytes until the next
 * overlay is copied over them, as on the DS, and sdk/overlay.c stops
 * calling the overlay resident.
 *
 * A word in an overlay's data that holds the address of one of that
 * overlay's own functions is called through, so it does not pass a call
 * site and the slot the build puts at the call sites never sees it
 * (docs/mod-interface.md, section 2).  The load asks the registry for
 * those words, and a mod's hook for the function takes the word's place;
 * the overlay's region is the overlay's own, because two overlays share
 * one RAM window.  The ARM9's own stored pointers are the ARM9's business
 * (sdk/armdata.c).
 *
 * This module plugs into sdk/overlay.c through bw_overlay_set_provider; the
 * loader keeps the residency bookkeeping.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "armdata.h"
#include "overlay.h"
#include "overlay_image.h"
#include "langmap.h"
#include "mod_slot.h"

#define OVL_MAX 1024u

static const BwOvlImageSet *g_set;
static unsigned             g_flags;

/* Overlay files, read once (or set by a test) and kept. */
typedef struct OvlFile {
    u8  *bytes;
    u32  size;
    u8   tried;
} OvlFile;

static OvlFile g_files[OVL_MAX];

/* ------------------------------------------------------------------ */
/* The overlay files                                                    */
/* ------------------------------------------------------------------ */

/* <extract>/<rel>: the data root's parent, as sdk/narc.c finds arm9_dec.bin. */
static int extract_path(char *out, size_t cap, const char *rel)
{
    const char *root = bw_data_root();
    size_t n = strlen(root);

    if (n + strlen(rel) + 2 > cap) {
        return -1;
    }
    memcpy(out, root, n + 1);
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') {
        n--;
    }
    strcpy(out + n, rel);
    return 0;
}

static u8 *read_whole(const char *path, u32 *size_out)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *buf;

    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (u8 *)malloc((size_t)end);
    if (buf == NULL || fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (u32)end;
    return buf;
}

static void load_file(u32 id)
{
    static const char *const dirs[] = { "overlays_dec/", "overlays/" };
    OvlFile *f = &g_files[id];
    char rel[64];
    char path[1100];
    u32 k;

    f->tried = 1;
    for (k = 0; k < 2 && f->bytes == NULL; k++) {
        snprintf(rel, sizeof(rel), "%sy9_%04lu.bin", dirs[k], (unsigned long)id);
        if (extract_path(path, sizeof(path), rel) == 0) {
            f->bytes = read_whole(path, &f->size);
        }
    }
    /* another cartridge than English Black: the file laid out as English
     * Black's, at its English place (sdk/langmap.h) */
    if (f->bytes != NULL && bw_lang_active()) {
        const BwOverlayDesc *d = bw_overlay_desc(id);
        u32 size = 0;
        u8 *eng = d != NULL ? bw_lang_relayout_overlay(id, d->ram_address, d->ram_size, f->bytes,
                                                        f->size, &size) : NULL;

        if (eng != NULL) {
            free(f->bytes);
            f->bytes = eng;
            f->size = size;
        }
    }
}

const void *bw_ovl_file_bytes(u32 id, u32 offset, u32 size)
{
    OvlFile *f;

    if (id >= OVL_MAX) {
        return NULL;
    }
    f = &g_files[id];
    if (!f->tried) {
        load_file(id);
    }
    if (f->bytes == NULL || offset > f->size || size > f->size - offset) {
        return NULL;
    }
    return f->bytes + offset;
}

void bw_ovl_set_file(u32 id, const void *bytes, u32 size)
{
    OvlFile *f;

    if (id >= OVL_MAX) {
        return;
    }
    f = &g_files[id];
    free(f->bytes);
    f->bytes = NULL;
    f->size = 0;
    f->tried = 0;
    if (bytes == NULL || size == 0) {
        return;
    }
    f->bytes = (u8 *)malloc(size);
    if (f->bytes != NULL) {
        memcpy(f->bytes, bytes, size);
        f->size = size;
    }
    f->tried = 1;
}

/* ------------------------------------------------------------------ */
/* The overlay's own stored code pointers                               */
/* ------------------------------------------------------------------ */

/*
 * A word in an overlay's data that holds the address of one of that
 * overlay's own functions is called through, so it does not pass a call
 * site and the slot the build puts at the call sites never sees it
 * (docs/mod-interface.md, section 2).  The ARM9's own data asks the
 * registry for the function the word points at (bw_arm9_code_entry,
 * sdk/armdata.c); an overlay's own data is the overlay's own business,
 * and this is where it asks.
 *
 * The region is the overlay's own (BW_MOD_REGION_OVL(id), the overlay's
 * id): two overlays share one RAM window, so the address alone does not
 * say whose function a word points at.
 *
 * The words are asked when the overlay loads, and the game loads an
 * overlay when it needs it, which is after the mod layer has loaded (the
 * mods load at start-up, sdk/mod_loader.c): the registry already holds
 * the hooks then.  A hook installed later, when the player changes the
 * mod list in game (the settings' mod menu, engine/platform/settings_ui.c),
 * reaches the words through bw_ovl_resolve_code_pointers, the way the
 * ARM9's do (bw_arm9_resolve_code_pointers).
 */

/*
 * The code words the port rewrote in the overlays' data, so a hook
 * installed after the overlay loaded can be put in their place, and a hook
 * taken out can be taken back.  The word's own content says whether the
 * game has written over it since: the port wrote a host address there,
 * and a word that holds something else is the game's now.
 */
#define OVL_CODE_WORDS_MAX 4096

typedef struct OvlCodeWord {
    u8   *word;      /* the word the port rewrote                */
    u32   host;      /* what the port wrote there                 */
    u32   plain;     /* the game's own function                   */
    u32   addr;      /* the function's even address               */
    u32   overlay;   /* the overlay whose data holds the word     */
} OvlCodeWord;

static OvlCodeWord g_code_words[OVL_CODE_WORDS_MAX];
static u32          g_code_word_count;

/*
 * The overlay's own function at `key` (the even address), or NULL: the
 * entry table holds every function of the overlay, the stubs too, and
 * nothing else.  The table is sorted by the value a code pointer to the
 * function holds, and the lookup is on the even address alone: a data
 * word carries the instruction-set tag the game gave it and the table
 * carries the tag the listing gave the function, and the two disagree for
 * a few (bw_ovl_code_entry has the same note).
 */
static const BwDataFunc *ovl_own_entry(const BwOvlImage *o, u32 key)
{
    u32 lo = 0, hi;

    if (o == NULL || o->entries == NULL) {
        return NULL;
    }
    hi = o->entry_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2u;

        if ((o->entries[mid].addr & ~1u) < key) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < o->entry_count && (o->entries[lo].addr & ~1u) == key) {
        return &o->entries[lo];
    }
    return NULL;
}

/*
 * The host address of the word at the DS address `where`: inside one of
 * the overlay's own host objects, or in the flat copy of its window.
 * These are the two places sdk/armdata.c's flat_word looks, so this finds
 * the word the relocation wrote.
 */
static u8 *ovl_word(const BwDataImage *img, u32 where)
{
    u32 i;

    for (i = 0; i < img->object_count; i++) {
        const BwDataObject *ob = &img->objects[i];

        if (where - ob->addr < ob->size && ob->size - (where - ob->addr) >= 4u) {
            return (u8 *)ob->host + (where - ob->addr);
        }
    }
    for (i = 0; i < img->range_count; i++) {
        const BwDataRange *r = &img->ranges[i];
        u8 *base;

        if (where - r->addr < r->size && r->size - (where - r->addr) >= 4u) {
            /* The base of the flat copy of the range, as bw_data_load
             * found it: the word is at its offset in that block. */
            base = (u8 *)bw_ds_try_ptr(r->addr);
            if (base == NULL) {
                return NULL;
            }
            return base + (where - r->addr);
        }
    }
    return NULL;
}

/*
 * The host address the registry answers for the pair: the mod's hook when
 * one is installed, the game's own function otherwise.  A hook whose
 * switch is off is skipped by the registry, and the pair then holds the
 * game's own function (sdk/mod_slot.h).
 */
static u32 ovl_hook_address(u32 region, u32 addr, u32 plain)
{
    void *h = bw_mod_slot_lookup(region, addr);

    return h != NULL ? (u32)(uintptr_t)h : plain;
}

/* Remember a rewritten code word, so a hook installed later can be put in
 * its place and a hook taken out can be taken back. */
static void remember_code_word(u8 *word, u32 host, u32 plain, u32 addr,
                               u32 overlay)
{
    if (g_code_word_count >= OVL_CODE_WORDS_MAX) {
        return;
    }
    g_code_words[g_code_word_count].word = word;
    g_code_words[g_code_word_count].host = host;
    g_code_words[g_code_word_count].plain = plain;
    g_code_words[g_code_word_count].addr = addr;
    g_code_words[g_code_word_count].overlay = overlay;
    g_code_word_count++;
}

/*
 * Drop the words of the overlays that are no longer resident: their window
 * holds another overlay's bytes now, and the game does not call them.
 * The overlay that is loading is dropped as well: its data is written
 * again below, so its words are new.
 */
static void drop_code_words(u32 overlay)
{
    u32 i, k = 0;

    for (i = 0; i < g_code_word_count; i++) {
        OvlCodeWord *cw = &g_code_words[i];

        if (cw->overlay == overlay || !bw_overlay_is_loaded(cw->overlay)) {
            continue;
        }
        g_code_words[k++] = *cw;
    }
    g_code_word_count = k;
}

/*
 * Put the hooks in the overlay's own stored code pointers.
 *
 * The relocation (bw_data_relocate, sdk/armdata.c) wrote the build's own
 * function into every listed code word of the overlay's data; a mod's hook
 * for that function takes its place, so a call through the word reaches
 * the hook.  A function nobody hooked keeps its own address, and a word
 * that holds an address in the middle of a function is not a code pointer
 * the port knows of, so it is left alone.
 *
 * Only the overlay's own functions are asked for: a word that points at a
 * function of another overlay is left as the relocation wrote it (the
 * trampolines reach those, docs/overlays.md), and a word that points at an
 * ARM9 function is left as well: the ARM9's region is the ARM9's layer's
 * business (sdk/armdata.c).
 *
 * Returns how many words it changed.
 */
static u32 ovl_apply_code_pointers(const BwOvlImage *o)
{
    const BwDataImage *img = o->data;
    u32 n = 0;
    u32 i;

    if (img == NULL || img->relocs == NULL || img->funcs == NULL) {
        return 0;
    }
    if (!bw_data_relocation_supported()) {
        /* A host pointer does not fit the DS's 32-bit word on this host:
         * the relocation was refused and the words keep their DS
         * addresses, so there is nothing to point at a hook
         * (sdk/armdata.h, "Pointer width"). */
        return 0;
    }
    for (i = 0; i < img->reloc_count; i++) {
        const BwDataReloc *r = &img->relocs[i];
        const BwDataFunc *f;
        u8 *w;
        u32 key, plain, want;

        if (r->kind != BW_DATA_RELOC_CODE || r->index >= img->func_count) {
            continue;
        }
        f = &img->funcs[r->index];
        key = f->addr & ~1u;
        if (ovl_own_entry(o, key) == NULL) {
            continue;           /* not one of the overlay's own functions */
        }
        w = ovl_word(img, r->where);
        if (w == NULL) {
            continue;
        }
        plain = (u32)(uintptr_t)f->fn;
        want = ovl_hook_address(BW_MOD_REGION_OVL(o->id), key, plain);
        if (want != plain) {
            bw_wr32(w, want);
            n++;
        }
        remember_code_word(w, want, plain, key, o->id);
    }
    return n;
}

/*
 * Put the hooks in the overlays' stored code pointers again.
 *
 * The words are written when the overlay loads, which is after the mod
 * layer has loaded, so the registry held the hooks when they were
 * written.  This asks it again for every word the port rewrote, for a
 * hook installed (or taken out) since: the player changes the mod list in
 * game, and the mods that are loaded then are not the ones the overlay
 * was loaded with.
 *
 * A word the game has written over since is left alone: the port wrote a
 * host address there, and a word that holds something else is the game's
 * now.  Returns how many words it changed.
 */
u32 bw_ovl_resolve_code_pointers(void)
{
    u32 n = 0;
    u32 i;

    for (i = 0; i < g_code_word_count; i++) {
        OvlCodeWord *cw = &g_code_words[i];
        u32 now, want;

        if (cw->word == NULL || !bw_overlay_is_loaded(cw->overlay)) {
            continue;
        }
        now = bw_rd32(cw->word);
        if (now != cw->host) {
            continue;           /* the game wrote its own value there */
        }
        want = ovl_hook_address(BW_MOD_REGION_OVL(cw->overlay), cw->addr,
                                cw->plain);
        if (want == now) {
            continue;           /* nothing changed */
        }
        bw_wr32(cw->word, want);
        cw->host = want;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The provider sdk/overlay.c calls                                     */
/* ------------------------------------------------------------------ */

const BwOvlImage *bw_ovl_image(u32 id)
{
    u32 i;

    if (g_set == NULL) {
        return NULL;
    }
    for (i = 0; i < g_set->count; i++) {
        if (g_set->images[i] != NULL && g_set->images[i]->id == id) {
            return g_set->images[i];
        }
    }
    return NULL;
}

static int provider_compiled(u32 id)
{
    return bw_ovl_image(id) != NULL;
}

static void load_panic(const BwOverlayDesc *d, const char *step)
{
    char why[400];

    snprintf(why, sizeof(why), "overlay %lu: %s: %s", (unsigned long)d->id, step,
             bw_data_error());
    bw_panic("FS_LoadOverlay", d->ram_address, why);
}

/*
 * Before overlay `id` loads: take back the host objects of every compiled
 * overlay that is no longer resident (sdk/overlay.c has already dropped the
 * ones this load overwrites, and any unloaded since) and of `id` itself (a
 * reload), each one's contents copied back into the window first.  An
 * unloaded overlay's objects stay registered until the next load, over
 * bytes nothing else has claimed yet, as the window keeps them on the DS.
 */
static void withdraw_passed(u32 id)
{
    u32 i;

    for (i = 0; i < g_set->count; i++) {
        const BwOvlImage *o = g_set->images[i];

        if (o != NULL && (o->id == id || !bw_overlay_is_loaded(o->id))) {
            bw_data_withdraw(o->data);
        }
    }
}

static void provider_load(const BwOverlayDesc *d)
{
    const BwOvlImage *img = bw_ovl_image(d->id);
    int rc;

    if (img == NULL) {
        return;
    }
    withdraw_passed(d->id);
    /* The stored code words of this overlay's last load, and of the
     * overlays that are no longer resident, go away with their data: the
     * window holds this overlay's bytes now. */
    drop_code_words(d->id);
    rc = bw_data_load(img->data);
    if (rc != BW_DATA_OK) {
        load_panic(d, "copying the image");
    }
    /* The overlay's declared slots are installed with its window: publish
     * each original so a wrap override can call it (mod_slot.h). */
    bw_ovl_mod_publish(d->id);
    rc = bw_data_bind(img->data);
    if (rc != BW_DATA_OK) {
        load_panic(d, "binding its names");
    }
    rc = bw_data_relocate(img->data);
    if (rc == BW_DATA_E_LP64 && (g_flags & BW_DATA_ALLOW_UNRELOCATED)) {
        return;
    }
    if (rc != BW_DATA_OK) {
        load_panic(d, "relocating its pointers");
    }
    /* The overlay's own stored code pointers reach the registry: the
     * relocation wrote the build's own functions into them, and a mod's
     * hook for one of them takes its place. */
    (void)ovl_apply_code_pointers(img);
}

static void provider_start(const BwOverlayDesc *d)
{
    const BwOvlImage *img = bw_ovl_image(d->id);
    u32 i;

    if (img == NULL) {
        return;
    }
    for (i = 0; i < img->sinit_count; i++) {
        img->sinits[i]();
    }
}

static const BwOverlayProvider k_provider = {
    provider_compiled, provider_load, provider_start
};

void bw_ovl_install(const BwOvlImageSet *set, unsigned flags)
{
    g_set = set;
    g_flags = flags;
    bw_overlay_set_provider(set != NULL ? &k_provider : NULL);
}

/* ------------------------------------------------------------------ */
/* Trampolines and stubs                                                */
/* ------------------------------------------------------------------ */

int bw_ovl_resident_at(u32 addr)
{
    const BwOverlayTable *t = bw_overlay_table();
    u32 i;

    /* The compiled overlays first: a handful, and the usual answer. */
    if (g_set != NULL) {
        for (i = 0; i < g_set->count; i++) {
            const BwOverlayDesc *o;

            if (g_set->images[i] == NULL) {
                continue;
            }
            o = bw_overlay_desc(g_set->images[i]->id);
            if (o != NULL && addr - o->ram_address < o->ram_size + o->bss_size
                && bw_overlay_is_loaded(o->id)) {
                return (int)o->id;
            }
        }
    }
    /* Then every overlay, to name a resident one this build lacks. */
    for (i = 0; i < t->overlay_count; i++) {
        const BwOverlayDesc *o = &t->overlays[i];

        if (addr - o->ram_address < o->ram_size + o->bss_size
            && bw_overlay_is_loaded(o->id)) {
            return (int)o->id;
        }
    }
    return -1;
}

BwDataFn bw_ovl_code_entry(u32 value)
{
    BwDataFn found = NULL;
    /* The DS entry is the function's even address; the low bit is the
     * instruction-set tag.  A data word carries the tag the game gave it and
     * the entry table carries the tag the listing gave the function, and the
     * two disagree for a few (FLDMMDL_GetMovedPosSaveSize is Thumb in its
     * source header and ARM in the listing), so the lookup keys on the
     * address alone: the host function is the same either way. */
    u32 key = value & ~1u;
    u32 i;
    int ambiguous = 0;

    if (g_set == NULL) {
        return NULL;
    }
    for (i = 0; i < g_set->count; i++) {
        const BwOvlImage *o = g_set->images[i];
        u32 lo = 0, hi;

        if (o == NULL || o->entries == NULL) {
            continue;
        }
        hi = o->entry_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2u;
            if ((o->entries[mid].addr & ~1u) < key) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo < o->entry_count && (o->entries[lo].addr & ~1u) == key) {
            void *h = bw_mod_slot_lookup(o->id, key);

            if (h != NULL) {
                /* A mod installed an override for this (region, address)
                 * pair (mod_slot.h): it wins over the table entry. */
                return (BwDataFn)h;
            }
            if (bw_overlay_is_loaded(o->id)) {
                /* Overlays share windows, so another overlay may have a
                 * function at this address too (0x021BE53C is ov204's and
                 * ov099's): the one resident now is the one the DS runs. */
                return o->entries[lo].fn;
            }
            /* Not resident: a later image may be the resident one (ov223's
             * 0x021BB9DC is ov088's and ov091's too), so keep looking. */
            if (found != NULL) {
                ambiguous = 1;      /* two overlays: needs a trampoline */
            }
            found = o->entries[lo].fn;
        }
    }
    return ambiguous ? NULL : found;
}

void bw_ovl_stub(u32 id, u32 addr)
{
    char why[200];

    snprintf(why, sizeof(why),
             "overlay %lu: function 0x%08lX is not in this build (not matched in "
             "aletheia, or its source is excluded; tools/ovlink.py's report says "
             "which)", (unsigned long)id, (unsigned long)addr);
    bw_stub_reached("overlay stub", addr, why);
}

void bw_ovl_arm9_stub(u32 addr, const char *name)
{
    char why[200];

    snprintf(why, sizeof(why),
             "%s: ARM9 function an overlay calls and this link does not define "
             "(tools/ovlink.py --arm9-stubs)", name);
    bw_stub_reached("overlay ARM9 stub", addr, why);
}

void bw_ovl_dispatch_fail(u32 addr, const char *name, int resident, int why)
{
    char text[240];

    switch (why) {
    case BW_OVL_WHY_NOT_COMPILED:
        snprintf(text, sizeof(text),
                 "%s: overlay %d is resident over 0x%08lX but is not compiled "
                 "into this build", name, resident, (unsigned long)addr);
        break;
    case BW_OVL_WHY_SIGNATURE:
        snprintf(text, sizeof(text),
                 "%s: overlay %d is resident over 0x%08lX, and its function there "
                 "has another prototype than the trampoline's", name, resident,
                 (unsigned long)addr);
        break;
    default:
        snprintf(text, sizeof(text),
                 "%s: called with no overlay resident over 0x%08lX", name,
                 (unsigned long)addr);
        break;
    }
    bw_panic("overlay trampoline", addr, text);
}

void bw_mwcc_clear(void *dst, unsigned int size)
{
    memset(dst, 0, size);
}

void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size)
{
    return memcpy(dst, src, size);
}
