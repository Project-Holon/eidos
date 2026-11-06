/*
 * sdk/overlay.c -- the overlay half of libfs, for a port whose overlays are
 * compiled in rather than loaded.
 *
 * Why a shim.  The SDK's overlay loader (fs_overlay.o, matched in aletheia)
 * reads the overlay table and the overlay file off the cartridge, copies
 * the image to its RAM address, flushes the caches and calls the static
 * initialisers through pointers stored in the image.  None of that has a
 * host meaning: the code is already in the executable, at host addresses.
 * What survives is the contract the game relies on, implemented over the
 * registry in sdk/overlay.h:
 *
 *   FS_LoadOverlayInfo   the overlay table entry, from the generated table
 *                        (the same eight words as y9.bin), plus the file's
 *                        ROM extent from the FAT when the port has one;
 *   FS_LoadOverlayImage  the presence check, then the data reset hook, then
 *                        the overlay is resident and any overlay sharing its
 *                        RAM is not;  an overlay compiled into the executable
 *                        (sdk/overlay_image.c, the provider) skips the
 *                        presence check and loads through the provider;
 *   FS_StartOverlay      the static initialisers, in table order;
 *   FS_EndOverlay /      not resident any more (the game has no global
 *   FS_UnloadOverlay...  destructors, aletheia's docs/MATCHING.md, so the
 *                        SDK's destructor walk has nothing to run).
 *
 * The SDK names, from layout/symbols_sdk.txt (libfs.TWL.HYB.a, fs_overlay.o):
 *
 *     02078C40  FSi_InitOverlay          0207902C  FS_StartOverlay
 *     02078D34  FS_LoadOverlayInfo       02079164  FS_EndOverlay
 *     02078EB0  FS_LoadOverlayImage      02079254  FS_UnloadOverlayImage
 *     02079264  FS_LoadOverlay           020792B8  FS_UnloadOverlay
 *
 * The game reaches them through its overlay manager in src/matched/arm9/:
 * sub_02034AC4 (FS_LoadOverlay(0, id)), sub_02034A3C (func_020792B8, the
 * unload), sub_02034D60 and sub_02034DC4 (FS_LoadOverlayInfo).
 *
 * The rule this file exists to enforce: loading an overlay whose code is not
 * all in the build stops at the load, naming the overlay and the first
 * function that is missing, instead of jumping later into code that is not
 * there.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "fs.h"
#include "overlay.h"
#include "langmap.h"
#include "input.h"

#define BW_OVL_MAX 1024u

static const BwOverlayTable *g_table;
static BwOverlayImageHook    g_image_hook;
static const BwOverlayProvider *g_provider;   /* sdk/overlay_image.c */
static u8                    g_loaded[BW_OVL_MAX];

/* 1 if overlay `id` is compiled into this executable (the provider says). */
static int compiled(u32 id)
{
    return g_provider != NULL && g_provider->compiled(id);
}

/* ------------------------------------------------------------------ */
/* The registry                                                         */
/* ------------------------------------------------------------------ */

const BwOverlayTable *bw_overlay_table(void)
{
    return g_table != NULL ? g_table : &bw_overlay_table_generated;
}

void bw_overlay_use_table(const BwOverlayTable *table)
{
    if (table != NULL && table->overlay_count > BW_OVL_MAX) {
        bw_panic("bw_overlay_use_table", 0, "more overlays than the registry holds");
    }
    g_table = table;
    memset(g_loaded, 0, sizeof(g_loaded));
}

const BwOverlayDesc *bw_overlay_desc(u32 id)
{
    const BwOverlayTable *t = bw_overlay_table();

    if (id >= t->overlay_count) {
        return NULL;
    }
    return &t->overlays[id];
}

int bw_overlay_check(u32 id, const BwOverlayFunc **missing)
{
    const BwOverlayTable *t = bw_overlay_table();
    const BwOverlayDesc *d = bw_overlay_desc(id);
    u32 i;

    if (missing != NULL) {
        *missing = NULL;
    }
    if (d == NULL) {
        return BW_OVL_NO_SUCH;
    }
    if (d->func_count == 0) {
        return BW_OVL_UNKNOWN;
    }
    for (i = 0; i < d->func_count; i++) {
        const BwOverlayFunc *f = &t->funcs[d->func_first + i];

        if (f->status != BW_OVF_PRESENT) {
            if (missing != NULL) {
                *missing = f;
            }
            return BW_OVL_ABSENT;
        }
    }
    return BW_OVL_PRESENT;
}

void bw_overlay_describe(u32 id, char *out, u32 out_size)
{
    const BwOverlayDesc *d = bw_overlay_desc(id);
    const BwOverlayFunc *f;
    char fname[64];
    int r;

    if (out == NULL || out_size == 0) {
        return;
    }
    r = bw_overlay_check(id, &f);
    if (r == BW_OVL_NO_SUCH) {
        snprintf(out, out_size, "overlay %lu: no such overlay (the table has %lu)",
                 (unsigned long)id, (unsigned long)bw_overlay_table()->overlay_count);
        return;
    }
    if (r == BW_OVL_UNKNOWN) {
        snprintf(out, out_size,
                 "overlay %lu (0x%08lX..0x%08lX): no function of it is known to "
                 "aletheia's metadata, so this build cannot claim its code",
                 (unsigned long)id, (unsigned long)d->ram_address,
                 (unsigned long)(d->ram_address + d->ram_size));
        return;
    }
    if (compiled(id)) {
        snprintf(out, out_size,
                 "overlay %lu (0x%08lX..0x%08lX): compiled into this build; a "
                 "function it lacks is a stub that panics when reached",
                 (unsigned long)id, (unsigned long)d->ram_address,
                 (unsigned long)(d->ram_address + d->ram_size));
        return;
    }
    if (r == BW_OVL_PRESENT) {
        snprintf(out, out_size,
                 "overlay %lu (0x%08lX..0x%08lX): present, %lu of %lu known functions",
                 (unsigned long)id, (unsigned long)d->ram_address,
                 (unsigned long)(d->ram_address + d->ram_size),
                 (unsigned long)d->present_count, (unsigned long)d->func_count);
        return;
    }
    if (f->name != NULL) {
        snprintf(fname, sizeof(fname), "%s", f->name);
    } else {
        snprintf(fname, sizeof(fname), "func_%08lX", (unsigned long)f->address);
    }
    snprintf(out, out_size,
             "overlay %lu (0x%08lX..0x%08lX) is not in this build: first missing "
             "function %s at 0x%08lX (%s); %lu of %lu known functions present, "
             "%lu matched in aletheia",
             (unsigned long)id, (unsigned long)d->ram_address,
             (unsigned long)(d->ram_address + d->ram_size), fname,
             (unsigned long)f->address,
             f->status == BW_OVF_MATCHED ? "matched in aletheia, not compiled into this build"
                                         : "not matched in aletheia",
             (unsigned long)d->present_count, (unsigned long)d->func_count,
             (unsigned long)d->matched_count);
}

void bw_overlay_set_image_hook(BwOverlayImageHook hook)
{
    g_image_hook = hook;
}

void bw_overlay_set_provider(const BwOverlayProvider *provider)
{
    g_provider = provider;
}

int bw_overlay_is_loaded(u32 id)
{
    return id < BW_OVL_MAX && bw_overlay_desc(id) != NULL && g_loaded[id] != 0;
}

void bw_overlay_sdk_init(void)
{
    memset(g_loaded, 0, sizeof(g_loaded));
}

/* FSi_InitOverlay at 0x02078C40, under its SDK name. */
void FSi_InitOverlay(void)
{
    bw_overlay_sdk_init();
}

/* ------------------------------------------------------------------ */
/* The SDK surface                                                      */
/* ------------------------------------------------------------------ */

#define BW_MI_PROCESSOR_ARM9 0u

/* The descriptor an info block names, or NULL if it names none. */
static const BwOverlayDesc *desc_of(const BwOverlayInfo *info)
{
    const BwOverlayDesc *d;

    if (info == NULL || info->target != BW_MI_PROCESSOR_ARM9) {
        return NULL;
    }
    d = bw_overlay_desc(info->id);
    if (d == NULL || d->ram_address != info->ram_address) {
        return NULL;
    }
    return d;
}

/*
 * FS_LoadOverlayInfo(info, target, id) at 0x02078D34.  TRUE with the entry,
 * FALSE for an id past the table or the ARM7 (the game has no ARM7
 * overlays, and the port no ARM7 code to overlay).
 */
BOOL FS_LoadOverlayInfo(BwOverlayInfo *info, u32 target, u32 id)
{
    const BwOverlayDesc *d;
    u32 top;
    u32 bottom;

    if (info == NULL || target != BW_MI_PROCESSOR_ARM9) {
        return FALSE;
    }
    d = bw_overlay_desc(id);
    if (d == NULL) {
        return FALSE;
    }
    info->id = d->id;
    info->ram_address = d->ram_address;
    info->ram_size = d->ram_size;
    info->bss_size = d->bss_size;
    info->sinit_init = d->sinit_init;
    info->sinit_init_end = d->sinit_init_end;
    info->file_id = d->file_id;
    info->compressed = d->compressed;
    info->target = target;
    info->file_offset = 0;
    info->file_length = 0;
    if (bw_fs_file_extent(d->file_id, &top, &bottom) == 0) {
        info->file_offset = top;
        info->file_length = bottom - top;
    }
    return TRUE;
}

/*
 * FS_LoadOverlayImage(info) at 0x02078EB0.  The presence check is here,
 * because every load path goes through here.  An overlay that is not all
 * present panics with bw_overlay_describe's diagnosis.
 */
BOOL FS_LoadOverlayImage(BwOverlayInfo *info)
{
    const BwOverlayTable *t = bw_overlay_table();
    const BwOverlayDesc *d = desc_of(info);
    char why[512];
    int is_compiled;
    u32 lo;
    u32 hi;
    u32 i;

    if (d == NULL) {
        return FALSE;
    }
    is_compiled = compiled(d->id);
    if (!is_compiled && bw_overlay_check(d->id, NULL) != BW_OVL_PRESENT) {
        bw_overlay_describe(d->id, why, sizeof(why));
        bw_panic("FS_LoadOverlay", d->ram_address, why);
    }
    if (!is_compiled && g_image_hook == NULL) {
        snprintf(why, sizeof(why),
                 "overlay %lu is present but no image hook is installed to reset "
                 "its data (0x%lX bytes at 0x%08lX, .bss 0x%lX); see "
                 "bw_overlay_set_image_hook",
                 (unsigned long)d->id, (unsigned long)d->ram_size,
                 (unsigned long)d->ram_address, (unsigned long)d->bss_size);
        bw_panic("FS_LoadOverlay", d->ram_address, why);
    }

    /* Whatever shared this RAM is overwritten, as on the DS. */
    lo = d->ram_address;
    hi = d->ram_address + d->ram_size + d->bss_size;
    for (i = 0; i < t->overlay_count && i < BW_OVL_MAX; i++) {
        const BwOverlayDesc *o = &t->overlays[i];
        u32 olo = o->ram_address;
        u32 ohi = o->ram_address + o->ram_size + o->bss_size;

        if (g_loaded[i] && olo < hi && lo < ohi) {
            g_loaded[i] = 0;
        }
    }
    if (is_compiled) {
        g_provider->load(d);
    } else {
        g_image_hook(d);
    }
    g_loaded[d->id] = 1;
    /* another cartridge: words that point into a shared window follow
     * the overlay now there (sdk/langmap.h) */
    bw_lang_overlay_loaded(d->id);
    return TRUE;
}

/*
 * FS_StartOverlay(info) at 0x0207902C: run the static initialisers.  In the
 * retail image only ov230 has any (five, aletheia's docs/MATCHING.md, "The
 * one exception, ov230"); the presence check has already required them.
 */
void FS_StartOverlay(BwOverlayInfo *info)
{
    const BwOverlayTable *t = bw_overlay_table();
    const BwOverlayDesc *d = desc_of(info);
    u32 i;

    if (d == NULL) {
        return;
    }
    if (compiled(d->id)) {
        g_provider->start(d);
        return;
    }
    for (i = 0; i < d->sinit_count; i++) {
        const BwOverlaySinit *s = &t->sinits[d->sinit_first + i];

        if (s->fn == NULL) {
            char why[160];

            snprintf(why, sizeof(why),
                     "overlay %lu: static initialiser 0x%08lX is not in this build",
                     (unsigned long)d->id, (unsigned long)s->address);
            bw_panic("FS_StartOverlay", s->address, why);
        }
        s->fn();
    }
}

/* FS_EndOverlay(info) at 0x02079164. */
void FS_EndOverlay(BwOverlayInfo *info)
{
    const BwOverlayDesc *d = desc_of(info);

    if (d != NULL) {
        g_loaded[d->id] = 0;
    }
}

/* FS_UnloadOverlayImage(info) at 0x02079254. */
BOOL FS_UnloadOverlayImage(BwOverlayInfo *info)
{
    FS_EndOverlay(info);
    return TRUE;
}

/* FS_LoadOverlay(target, id) at 0x02079264. */
/* EIDOS_OVERLAY_LOG=1 (tests): each overlay load and unload with the input
 * frame, to find which overlay runs a scene. */
static void overlay_log(const char *what, u32 id)
{
    static int on = -1;

    if (on < 0) {
        const char *v = getenv("EIDOS_OVERLAY_LOG");

        on = v != NULL && *v != '\0' && strcmp(v, "0") != 0;
    }
    if (on) {
        fprintf(stderr, "eidos: overlay %u %s at frame %u\n", (unsigned)id, what,
                (unsigned)bw_input_frame_number());
    }
}

BOOL FS_LoadOverlay(u32 target, u32 id)
{
    BwOverlayInfo info;

    overlay_log("load", id);
    if (!FS_LoadOverlayInfo(&info, target, id)) {
        return FALSE;
    }
    if (!FS_LoadOverlayImage(&info)) {
        return FALSE;
    }
    FS_StartOverlay(&info);
    return TRUE;
}

/* FS_UnloadOverlay(target, id) at 0x020792B8. */
BOOL FS_UnloadOverlay(u32 target, u32 id)
{
    BwOverlayInfo info;

    overlay_log("unload", id);
    if (!FS_LoadOverlayInfo(&info, target, id)) {
        return FALSE;
    }
    return FS_UnloadOverlayImage(&info);
}

/* ------------------------------------------------------------------ */
/* Address aliases                                                      */
/* ------------------------------------------------------------------ */

/* src/matched/arm9/sub_02034A3C.c: func_020792B8(0, id), the one unload. */
BOOL func_020792B8(u32 target, u32 id)
{
    return FS_UnloadOverlay(target, id);
}

BOOL func_02079264(u32 target, u32 id)
{
    return FS_LoadOverlay(target, id);
}

BOOL func_02078D34(BwOverlayInfo *info, u32 target, u32 id)
{
    return FS_LoadOverlayInfo(info, target, id);
}
