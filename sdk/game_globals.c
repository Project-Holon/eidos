/*
 * sdk/game_globals.c -- the ARM9 global objects the matched code reaches
 * by name, defined for the host.
 *
 * These are not SDK functions.  They are the *data* half of the shim: objects
 * that live at fixed addresses in the DS image and that some not-yet-matched
 * start-up code initialises there.  Until that start-up code is decompiled,
 * the port creates each object itself and documents what fills it.
 *
 * `python tools/closure.py --seed ...` lists these under the `data`
 * class; every name in that list must appear here or the link fails, which is
 * exactly the agreement between the closure script and the linker the port is
 * built around.
 *
 * Two of the definitions below deliberately differ from the matched file's
 * own declaration, and both differences are findings, not conveniences:
 *
 *  1. `g_02146270` (src/matched/unit_02019C54.c) is declared there as
 *
 *         struct Memo { u16 b; u16 a; u8 obj[0x10]; };
 *
 *     but the write into `obj` is 0x3C bytes wide: src/matched/unit_0201ADA4.c
 *     calls func_020495F4(handle, 0x3C, dest) with that same buffer, and 0x3C
 *     is the personal record's size.  On the DS the under-declaration is
 *     harmless, because the real object at 0x02146274 is a plain global with
 *     the rest of the record after it and nothing in unit_02019C54.c indexes
 *     past +0x10.  On the host the object has to be big enough or the write
 *     runs off it, so it is defined here at the true size.  The matched file
 *     is left exactly as it is; it still produces the right DS bytes.
 *
 *  2. `func_02019B04_Table` (src/matched/unit_02019B04.c) is declared there
 *     `const s8 [][5]`.  The port cannot define it const, because its
 *     contents are game data that has to be copied in at run time out of the
 *     player's own extracted ARM9 image.  A definition whose qualifiers
 *     differ from another translation unit's declaration is outside what C99
 *     guarantees; it links and behaves correctly on every toolchain this port
 *     targets, and the alternative (editing the matched file) is forbidden.
 *     Recorded in docs/building.md under "Known type mismatches".
 */

#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "dsmem_ext.h"
#include "armdata.h"

/* Pointer-bearing boot singletons need independent host storage: adjacent
 * four-byte DS pointer cells cannot each hold an eight-byte host pointer.
 * Each registration below retains only its original DS address extent. */
void *g_ProcSys;
void *svsys;
void *_pOverlayTables;
void *_pTouchPanelWork;
struct BwUiState {
    void *context;
    int (*bounds)(void *lower, void *upper);
} gUiState;
struct BwGflUseState {
    void *vblank_arg;
    u32 extra_heap;
    void (*vblank_hook)(void *);
    u32 frame_count;
    void *runtime;
    void *extra_heap_block;
} _GflUse;

/* ------------------------------------------------------------------ */
/* g_Sink -- the assertion sink src/matched/sub_02034734.c dispatches to */
/* ------------------------------------------------------------------ */

typedef void (*BwAssertHandler)(void);

struct BwSink {
    u32 unk0;
    u32 unk4;
    u32 unk8;
    BwAssertHandler handler;
};

static void sink_handler(void)
{
    bw_panic("game assertion", 0x02034734, "sub_02034734 assert sink fired");
}

struct BwSink g_Sink = { 0, 0, 0, sink_handler };

/* ------------------------------------------------------------------ */
/* The __FILE__ / message strings the archive readers pass to the sink  */
/* ------------------------------------------------------------------ */

/*
 * On the DS these are pointers into the packed string area (0x020A7EE0 and
 * 0x020A7F18 for func_02049578, 0x020A78xx for func_0204944C), and the
 * matched files declare them as externs pinned by address because a
 * `__FILE__` literal has no attestable witness of its own
 * (aletheia's docs/idioms/file-string-is-an-abs32-into-the-string-area.md).
 * Their contents never affect behaviour: they are arguments to an assert
 * that only fires on an out-of-range member.  The port supplies its own
 * text, which is why no ROM string is reproduced here.
 */
const char func_02049578_File[] = "arc_tool.c";
const char func_02049578_Msg[]  = "member index out of range";
const char func_0204944C_File[] = "arc_tool.c";
const char func_0204944C_Msg[]  = "member index out of range";
const char func_02048DDC_File[] = "arc_tool.c";
const char func_02048DDC_Msg[]  = "member index out of range";

/* ------------------------------------------------------------------ */
/* g_02146A68 -- the archive name table                                 */
/* ------------------------------------------------------------------ */

/*
 * func_02048DDC opens an archive by id through this: +0 is the table, +4 the
 * entry count, +8 a flag selecting the (dead) memory arm.  The game installs
 * it once at boot with func_02048CEC(0x020A6C58, 0xEB)
 * (src/matched/sub_0200545C.c), and the table is 235 ARM9 addresses of
 * "a/x/y/z" strings, entry N being N's three decimal digits (aletheia's
 * docs/ASSETS.md).
 *
 * The shape is src/matched/unit_02048DDC.c's, the reader's: `u32 * volatile
 * unk00`.  src/matched/unit_02048C58.c, the writer, declares the same object
 * as three u32, which is the same bytes only where a pointer is four; the
 * writer is not in the build, and bw_arc_install_table does its one store
 * against the reader's layout, so this works at either pointer width.  The
 * entries themselves stay DS addresses (u32) on every host, which is what
 * the open-by-path shim in sdk/fs.c expects.
 */
struct BwArcTable {
    const u32 *volatile table;
    u32                 count;
    u32                 memory;
};

struct BwArcTable g_02146A68;

int bw_arc_install_table(u32 address, u32 count)
{
    const void *t = bw_arm9_rodata(address, count * 4u);

    if (t == NULL || count == 0u) {
        return -1;
    }
    g_02146A68.table = (const u32 *)t;
    g_02146A68.count = count;
    g_02146A68.memory = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* g_02146270 -- the one-entry personal-record memo                     */
/* ------------------------------------------------------------------ */

#define BW_PERSONAL_RECORD_SIZE 0x3C

struct BwMemo {
    u16 b;
    u16 a;
    u8  obj[BW_PERSONAL_RECORD_SIZE + 4];   /* see note 1 above */
};

struct BwMemo g_02146270;

/* ------------------------------------------------------------------ */
/* g_0214652C -- the personal-data archive handle                       */
/* ------------------------------------------------------------------ */

static BwArcFile g_personal_arc;

void *g_0214652C;

int bw_open_personal_archive(void)
{
    if (g_0214652C != NULL) {
        return 0;
    }
    if (bw_arc_open(&g_personal_arc, "a/0/1/6") != 0) {
        return -1;
    }
    g_0214652C = &g_personal_arc;

    /*
     * The memo caches by (a, b) and starts zeroed, which would read as
     * "species 0, form 0 already loaded".  Poison the key so the first real
     * call always misses and goes to the archive.
     */
    g_02146270.a = 0xFFFF;
    g_02146270.b = 0xFFFF;
    memset(g_02146270.obj, 0, sizeof(g_02146270.obj));
    return 0;
}

void bw_close_personal_archive(void)
{
    if (g_0214652C == NULL) {
        return;
    }
    bw_arc_close(&g_personal_arc);
    g_0214652C = NULL;
}

/* ------------------------------------------------------------------ */
/* func_02019B04_Table -- the nature multiplier table                   */
/* ------------------------------------------------------------------ */

/*
 * 25 natures by 5 stats of { -1, 0, +1 }, at ARM9 0x0209E324.  The index is
 * [nature][n - 1] with n as src/matched/sub_02018A7C.c passes it: 1 attack,
 * 2 defence, 3 sp. attack, 4 sp. defence, 5 speed.  Loaded from the player's
 * own extract/arm9_dec.bin; never stored in this repository.
 */
#define BW_NATURE_TABLE_ADDRESS 0x0209E324u
#define BW_NATURE_COUNT         25
#define BW_NATURE_STATS         5

s8 func_02019B04_Table[BW_NATURE_COUNT][BW_NATURE_STATS];

int bw_load_nature_table(void)
{
    const void *src = bw_arm9_rodata(BW_NATURE_TABLE_ADDRESS,
                                     BW_NATURE_COUNT * BW_NATURE_STATS);

    if (src == NULL) {
        return -1;
    }
    memcpy(func_02019B04_Table, src, BW_NATURE_COUNT * BW_NATURE_STATS);
    return 0;
}

/* ------------------------------------------------------------------ */
/* g_EnvModule -- the heap manager at 0x02146954                        */
/* ------------------------------------------------------------------ */

/*
 * The struct declarations below are copied field for field from
 * src/matched/sub_02030098.c, which is the widest view of the object any
 * matched file in the build takes; src/matched/sub_0202FC48.c, the
 * constructor, names the three halfwords at +8 idCount, resCount and
 * entryCount, and every declaration of the object in the corpus agrees on
 * the offsets it shares.  Copying the shape rather than inventing one is what
 * keeps the port and the matched code agreeing about where the fields are on
 * an LP64 host, where the two leading pointers are eight bytes each.
 *
 * Two ways of filling it in, decided by EIDOS_HEAP_MATCHED (CMakeLists.txt):
 *
 *  - ON, which needs 32-bit pointers.  The game's own constructor builds it:
 *    src/matched/sub_0202FC48.c takes the entry table, the id table and one
 *    region per heap out of arena 0 (sdk/os_arena.c) and turns every region
 *    into a NitroSystem expanded heap, and the entry's +0 is that heap's
 *    handle.  Allocation and release then run matched to the bottom: the
 *    free-list walks, the carve, the coalescing free.  The port chooses only
 *    how big each heap is, because the table the game passes
 *    (src/matched/sub_0200545C.c: d_0209D8A0 on TWL, d_0209D8B0 on NTR) is
 *    ARM9 rodata and lives in the player's own image.
 *  - OFF, the LP64 configuration.  The expanded heap cannot run there (its
 *    block header is 0x10 bytes with two pointers in it), so the port fills
 *    in the fields the matched upper layers read, with a distinct non-null
 *    token as each heap's handle, and stubs/reimpl_02060A08.c,
 *    stubs/reimpl_02060ABC.c and stubs/reimpl_02030164.c take blocks from
 *    the host allocator instead of from an arena.
 */

struct BwEnvEntry {
    void *ptr00;     /* the heap handle */
    void *field04;   /* parent heap handle */
    void *field08;   /* child heap's region */
    u16   field0c;   /* live allocations; sub_02030098 increments it */
    u8    field0e;
};

struct BwEnvModule {
    struct BwEnvEntry *entries;
    u8                *ids;
    u16                count;    /* sub_0202FC48: idCount, the id bound */
    u16                field0a;  /* sub_0202FC48: resCount, the first spare */
    u16                field0c;  /* sub_0202FC48: entryCount */
    u16                error;
};

struct BwEnvModule g_EnvModule;

/*
 * The NitroSystem root heap list, 0x02146B60/0x02146B64.  func_020606D0 (the
 * heap-head initialiser) appends every new top-level heap to it and
 * func_020606A8 looks a heap's parent up in it by address.  The second of
 * those names the list `g_02146B64` where the first says `sRootList`; the
 * DS links both to one word by address, a host links by name, so
 * CMakeLists.txt compiles func_020606A8.c with `g_02146B64` defined to
 * `sRootList` and this is the one object both see.  Defined at both pointer
 * widths: the LP64 heap floor never touches it, but the game's boot
 * (EIDOS_BOOT) compiles the matched heap files at either width and links
 * against it (tools/armdata.py lists it as a port object).
 */
struct BwFndList {
    void *head;
    void *tail;
    u16   count;
    u16   offset;
};

struct BwFndList sRootList;
int              sRootListInitialized;

#if EIDOS_HEAP_MATCHED

/* The resource record sub_0202FC48 reads, per heap. */
struct BwEnvRes {
    u32 size;
    u32 unk_04;
};

extern int sub_0202FC48(const struct BwEnvRes *res, u16 count, u16 idCount,
                        u32 extra);
extern int sub_0202FDC4(u32 src, u32 dst, u32 size);

int bw_heap_init_sizes(const u32 *sizes, u32 heaps, u32 id_count)
{
    struct BwEnvRes res[BW_HEAP_MAX];
    u32 i;
    u32 total;
    u32 spares;

    if (heaps == 0u || heaps > BW_HEAP_MAX) {
        return -1;
    }
    /* The constructor reserves 0x18 spare entries past the caller's heaps
     * (the child heaps func_0202FED8 binds) and clamps the id table up to
     * cover them. */
    spares = heaps + 0x18u;
    if (id_count < spares) {
        id_count = spares;
    }
    if (id_count > 0xFFFFu) {
        return -1;
    }

    /* The whole arena: the entry table (16 bytes per entry), the id table,
     * each heap's region and the four-byte rounding of every one of them. */
    total = spares * 16u + ((id_count + 3u) & ~3u);
    for (i = 0; i < heaps; i++) {
        res[i].size = (sizes[i] + 3u) & ~3u;
        res[i].unk_04 = 0;
        total += res[i].size;
    }
    if (bw_arena_reset(total) != 0) {
        return -1;
    }

    /* A fresh boot: no heap exists, so the root list is empty and not yet
     * initialised.  Without this a second init would find the previous
     * run's heaps, which occupied the same addresses, still on the list. */
    memset(&sRootList, 0, sizeof(sRootList));
    sRootListInitialized = 0;
    memset(&g_EnvModule, 0, sizeof(g_EnvModule));

    return sub_0202FC48(res, (u16)heaps, (u16)id_count, 0) ? 0 : -1;
}

int bw_heap_init(u32 heaps)
{
    u32 sizes[BW_HEAP_MAX];
    u32 i;

    if (heaps == 0u || heaps > BW_HEAP_MAX) {
        heaps = BW_HEAP_COUNT;
    }
    for (i = 0; i < heaps; i++) {
        sizes[i] = BW_HEAP_DEFAULT_SIZE;
    }
    return bw_heap_init_sizes(sizes, heaps, heaps);
}

int bw_heap_create_child(u32 parent, u32 child, u32 size)
{
    return sub_0202FDC4(parent, child, size) ? 0 : -1;
}

void *bw_heap_handle(u32 heap_id)
{
    u8 idx;

    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count) {
        return NULL;
    }
    idx = g_EnvModule.ids[heap_id];
    return idx == 0xffu ? NULL : g_EnvModule.entries[idx].ptr00;
}

#else /* !EIDOS_HEAP_MATCHED */

static struct BwEnvEntry g_heap_entries[BW_HEAP_MAX];
static u8                g_heap_ids[BW_HEAP_MAX];
static u32               g_heap_arena[BW_HEAP_MAX];

int bw_heap_init_sizes(const u32 *sizes, u32 heaps, u32 id_count)
{
    u32 i;

    (void)sizes;   /* host blocks come from malloc; a heap has no size */
    if (heaps == 0u || heaps > BW_HEAP_MAX) {
        return -1;
    }
    if (id_count < heaps) {
        id_count = heaps;
    }
    if (id_count > BW_HEAP_MAX) {
        id_count = BW_HEAP_MAX;
    }
    memset(g_heap_entries, 0, sizeof(g_heap_entries));
    for (i = 0; i < BW_HEAP_MAX; i++) {
        /*
         * 0xff is the manager's "no such resource" marker: sub_0202FBAC
         * returns 0 for it, which is the raw allocator's result code 2.
         * Ids past `heaps` keep it, as they do after sub_0202FC48.
         */
        g_heap_ids[i] = i < heaps ? (u8)i : (u8)0xff;
        g_heap_arena[i] = 0x48454150u + i;          /* 'HEAP' + index */
        g_heap_entries[i].ptr00 = i < heaps ? (void *)&g_heap_arena[i] : NULL;
    }
    g_EnvModule.entries = g_heap_entries;
    g_EnvModule.ids = g_heap_ids;
    g_EnvModule.count = (u16)id_count;
    g_EnvModule.field0a = (u16)heaps;
    g_EnvModule.field0c = (u16)heaps;
    g_EnvModule.error = 0;
    return 0;
}

int bw_heap_init(u32 heaps)
{
    if (heaps == 0u || heaps > BW_HEAP_MAX) {
        heaps = BW_HEAP_COUNT;
    }
    return bw_heap_init_sizes(NULL, heaps, heaps);
}

/* Child heaps are sub-regions of a parent's arena; there is no arena here. */
int bw_heap_create_child(u32 parent, u32 child, u32 size)
{
    (void)parent;
    (void)child;
    (void)size;
    return -1;
}

void *bw_heap_handle(u32 heap_id)
{
    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count
        || g_EnvModule.ids[heap_id] == 0xffu) {
        return NULL;
    }
    return g_EnvModule.entries[g_EnvModule.ids[heap_id]].ptr00;
}

#endif /* EIDOS_HEAP_MATCHED */

long bw_heap_live_blocks(u32 heap_id)
{
    u8 idx;

    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count) {
        return -1;
    }
    idx = g_EnvModule.ids[heap_id];
    if (idx == 0xffu) {
        return -1;
    }
    return (long)g_EnvModule.entries[idx].field0c;
}

u32 bw_heap_error(void)
{
    return g_EnvModule.error;
}

void bw_heap_note_release(u32 heap_id)
{
    u8 idx;

    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count) {
        return;
    }
    idx = g_EnvModule.ids[heap_id];
    if (idx != 0xffu && g_EnvModule.entries[idx].field0c != 0) {
        g_EnvModule.entries[idx].field0c--;
    }
}

/* ------------------------------------------------------------------ */
/* g_020A7820 -- the watched-heap id                                    */
/* ------------------------------------------------------------------ */

/*
 * src/matched/sub_02030A00.c gates its whole body on this: a signed watch id
 * where any negative value means "watch nothing", and a non-negative one
 * names the single heap whose allocations are handed to the provenance audit
 * sub_02030984.
 *
 * It is a data word inside the ARM9 image (0x020A7820 is below the image end
 * at 0x020A9E80, so it is in .data and not in BSS), which is why it is
 * loaded out of the player's own extract/arm9_dec.bin at run time rather
 * than spelled out here, exactly like the nature table above.  The value the
 * retail image carries is the "watch nothing" one; a port that failed to
 * load the image would otherwise default to heap 0 and run the audit on
 * every allocation, so the definition starts at -1 and the load only ever
 * replaces it.
 */
#define BW_WATCH_ID_ADDRESS 0x020A7820u

s32 g_020A7820 = -1;

int bw_load_watch_id(void)
{
    const void *src = bw_arm9_rodata(BW_WATCH_ID_ADDRESS, 4);

    if (src == NULL) {
        return -1;
    }
    g_020A7820 = (s32)bw_rd32((const u8 *)src);
    return 0;
}

/* ------------------------------------------------------------------ */
/* sub_0203101C_File -- tcbl.c's allocation-site anchor                 */
/* ------------------------------------------------------------------ */

/*
 * src/matched/sub_0203101C.c passes this as the fourth argument of every
 * allocation it makes, and sub_02030880 stamps the first 0x12 bytes of it
 * into the block header's provenance field.  It is not a __FILE__ literal:
 * the addresses these sites pass are ARM9 data words, and aletheia's
 * layout/file_anchors.txt names 0x020A7834 as the anchor of tcbl.c (the two
 * rows for 0x020310C0 and 0x02031284).  So the text is a file name that
 * aletheia's own metadata attests, and the port supplies it as text rather
 * than copying bytes out of the image: the value reaches nothing but a
 * provenance string that the release build never reads back.
 */
const char sub_0203101C_File[] = "tcbl.c";

/*
 * src/matched/sub_020311B8.c names the same word 0x020A7834 under its own
 * file-local spelling, which is what a matched source does with an anchor it
 * loads out of its own pool: the two rows layout/file_anchors.txt records for
 * tcbl.c are 0x020310C0 (the constructor's) and 0x02031284 (the registrar's),
 * one address.  The DS build resolves both names to it; a host linker sees
 * two, so the registrar gets its own definition of the same string rather
 * than a forwarder, because a `const char []` cannot be aliased the way
 * sdk/aliases.c aliases a function.
 */
const char sub_020311B8_File[] = "tcbl.c";

/* ------------------------------------------------------------------ */
/* DS addresses of the objects above                                    */
/* ------------------------------------------------------------------ */

/*
 * Every object above that has a DS address is registered at it, so that a
 * matched literal cast to that address (rewritten by tools/dsaddr.py into
 * bw_ds_ptr) and the matched file's `extern` name reach the same bytes
 * (sdk/dsmem.h, bw_ds_register).  The addresses are the ones the matched
 * files' own headers list for each name.  bw_ds_reset calls this.
 *
 * Sizes are the object's extent on the DS, which is what a cast can reach;
 * each is checked against the host object, which is never smaller.  On an
 * LP64 host the objects with pointer members are laid out differently past
 * the first pointer, so a DS-offset access beyond it lands on the host
 * layout's bytes; that is the pointer-width question docs/pointer-width.md
 * describes, not something registration can repair.
 *
 * Names that alias one DS address are registered once.  The DS links
 * func_02049578_File, func_0204944C_File and func_02048DDC_File all to
 * 0x020A7EE0, and func_02049578_Msg and func_0204944C_Msg both to
 * 0x020A7F18; sub_0203101C_File and sub_020311B8_File both to 0x020A7834.
 * A host links by name, so each spelling is its own copy of the same text,
 * and the address resolves to the first spelling of each group.  The port's
 * strings are shorter than the ROM's, so each registers only its own bytes.
 * The strings are const on the host and must only be read through their
 * DS addresses, which is all the matched code does with them.  The
 * heap-handle tables of the LP64 configuration are port objects with no
 * DS address and are not registered.
 */
#define BW_REG(addr, obj, ds_size)                                          \
    do {                                                                    \
        (void)sizeof(char[(ds_size) <= sizeof(obj) ? 1 : -1]);              \
        bw_ds_register((addr), (void *)&(obj), (ds_size), #obj);            \
    } while (0)

void bw_game_globals_register(void)
{
    /*
     * The ARM9 image's data (sdk/armdata.c), when the program installed it:
     * copied into flat main RAM first, while nothing is registered, and its
     * names and pointers bound last, so that an address one of the objects
     * below owns resolves to that object.  Both are no-ops otherwise.
     */
    bw_data_reset_load();

    g_ProcSys = NULL;
    svsys = NULL;
    _pOverlayTables = NULL;
    _pTouchPanelWork = NULL;
    memset(&gUiState, 0, sizeof(gUiState));
    memset(&_GflUse, 0, sizeof(_GflUse));
    BW_REG(0x02146990u, g_ProcSys, 4u);
    BW_REG(0x02146994u, svsys, 4u);
    BW_REG(0x021469B8u, _pOverlayTables, 4u);
    BW_REG(0x021469BCu, gUiState, 8u);
    BW_REG(0x021469C4u, _pTouchPanelWork, 4u);
    BW_REG(0x020AA194u, _GflUse, 0x18u);

    BW_REG(0x0214699Cu, g_Sink, 0x10u);
    BW_REG(0x020A7EE0u, func_02048DDC_File, (u32)sizeof(func_02048DDC_File));
    BW_REG(0x020A7EECu, func_02048DDC_Msg, (u32)sizeof(func_02048DDC_Msg));
    /* The ROM's string here is 0x18 bytes and the next one starts at
     * 0x020A7F30 (layout/data_objects.json); the port's text is two bytes
     * longer, so it registers only the DS extent. */
    BW_REG(0x020A7F18u, func_02049578_Msg, 0x18u);
    BW_REG(0x02146A68u, g_02146A68, 0x0Cu);
    /* 0x40, not the declared 0x14: the record write at +4 is 0x3C wide. */
    BW_REG(0x02146270u, g_02146270, 4u + BW_PERSONAL_RECORD_SIZE);
    BW_REG(0x0214652Cu, g_0214652C, 4u);
    BW_REG(BW_NATURE_TABLE_ADDRESS, func_02019B04_Table,
           BW_NATURE_COUNT * BW_NATURE_STATS);
    BW_REG(0x02146954u, g_EnvModule, 0x10u);
    BW_REG(BW_WATCH_ID_ADDRESS, g_020A7820, 4u);
    BW_REG(0x020A7834u, sub_0203101C_File, (u32)sizeof(sub_0203101C_File));
    BW_REG(0x02146B60u, sRootListInitialized, 4u);
    BW_REG(0x02146B64u, sRootList, 0x0Cu);

    bw_data_reset_bind();
}
