/*
 * sdk/tm_pocket.c -- a TM pocket with room for the TMs mods add
 * (docs/game-tables.md, machine_items.txt).
 *
 * The game's bag block (arm9 myitem_savedata.c, struct MyItem, save block
 * 0x19) has 109 slots in its TM pocket: the game's 101 machines and 8 more.
 * Mods add up to 27 TMs (TM96..TM122), so while one does, the TM pocket is
 * the port's own array of 109 + 27 slots, and the game's MYITEM_SAVEDATA_
 * GetPocket and GetSlot hand it out in place of the block's (they are the
 * only places the game uses the pocket's array and its size).
 *
 * The block keeps its layout and stays valid for the plain game.  When the
 * game saves (save.begin), the pocket is written back: the game's own
 * machines into the block, and each added TM into the per-save file of
 * the mod that adds it (the mod that
 * ships its item record, a/0/2/4 member; a later mod wins, as for the
 * data), through the per-save layer (sdk/mod_savedata.c, namespace
 * "<mod folder>.bag"), or "eidos.bag" for none.  When the save is loaded,
 * the pocket is built again from the block and those files.  A file of a
 * mod that is off is not read and stays as it is.
 *
 * With no mod TM, the functions are the game's own: the block's pocket.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "plat_dir.h"
#include "types.h"
#include "vfs.h"

/* ------------------------------------------------------------------ */
/* The game's bag block                                                */
/* ------------------------------------------------------------------ */

#define BAG_POCKET_ITEMS     0
#define BAG_POCKET_MEDICINE  1
#define BAG_POCKET_TMHM      2
#define BAG_POCKET_BERRIES   3
#define BAG_POCKET_KEY_ITEMS 4

#define BAG_ITEMS_COUNT      0x136
#define BAG_KEY_ITEMS_COUNT  0x53
#define BAG_TMHM_COUNT       0x6D
#define BAG_MEDICINE_COUNT   0x30
#define BAG_BERRIES_COUNT    0x40

struct BagSlot {
    u16 itemId;
    u16 quantity;
};

/* arm9 myitem_savedata.c: the bag block, the game's layout */
struct MyItem {
    struct BagSlot items[BAG_ITEMS_COUNT];         /* 000 */
    struct BagSlot keyItems[BAG_KEY_ITEMS_COUNT];  /* 4D8 */
    struct BagSlot tmhm[BAG_TMHM_COUNT];           /* 624 */
    struct BagSlot medicine[BAG_MEDICINE_COUNT];   /* 7D8 */
    struct BagSlot berries[BAG_BERRIES_COUNT];     /* 898 */
    u8 reserved[0x28];                             /* 998 */
};

typedef char bag_block_is_0x9c0[sizeof(struct MyItem) == 0x9C0 ? 1 : -1];

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

extern void MYITEM_SAVEDATA_SortPocketByItemOrder(struct BagSlot *slots, u32 count);   /* sdk/machines.c */

extern const char *bw_mod_savedata_namespace(void);
extern void bw_mod_savedata_set_namespace(const char *ns);
extern int bw_mod_savedata_load(void *buf, u32 size);
extern int bw_mod_savedata_store(const void *buf, u32 size);

/* ------------------------------------------------------------------ */
/* The port's pocket                                                   */
/* ------------------------------------------------------------------ */

#define ADDED_TMS     27u                               /* TM96..TM122 */
#define POCKET_SLOTS  (BAG_TMHM_COUNT + ADDED_TMS)
#define FIRST_ADDED   101u                              /* TM96's machine bit */
#define BAG_MAGIC     0x47414254u                       /* "TBAG" */
#define BAG_FILES     33                                /* one per mod (the per-save layer's 32), and eidos's own */

/* One file beside the save: the added TMs of one mod that the block had
 * no room for. */
typedef struct BagExt {
    u32 magic;
    u32 count;
    struct BagSlot slots[ADDED_TMS];
} BagExt;

typedef struct BagFile {
    char ns[80];
    int loaded;
    int onDisk;                         /* the save has this file */
    BagExt ext;
} BagFile;

static struct BagSlot g_pocket[POCKET_SLOTS];
static const struct MyItem *g_bound;    /* the block the pocket was built from */
static int g_new_game;                  /* a new game since the last save.load */

static BagFile *g_files;
static int g_nfiles;
static u8 g_owner[ADDED_TMS];           /* added TM -> file + 1; 0 not worked out */
static int g_owners_done;

/* Whether a mod adds a TM (the pocket is the port's), and the item of
 * added TM k (0 for none). */
static u32 added_item(u32 k)
{
    return bw_machine_item_of_bit(FIRST_ADDED + k);
}

static int pocket_on(void)
{
    u32 k;

    for (k = 0; k < ADDED_TMS; k++) {
        if (added_item(k) != 0u) {
            return 1;
        }
    }
    return 0;
}

/* The added TM an item is (0..26), -1 for any other. */
static int added_index(u32 item)
{
    int bit = item != 0u ? bw_machine_bit_of_item(item) : -1;

    return bit >= (int)FIRST_ADDED && bit < (int)(FIRST_ADDED + ADDED_TMS) ? bit - (int)FIRST_ADDED : -1;
}

static int file_named(const char *ns)
{
    int i;

    for (i = 0; i < g_nfiles; i++) {
        if (strcmp(g_files[i].ns, ns) == 0) {
            return i;
        }
    }
    if (g_files == NULL && (g_files = (BagFile *)calloc(BAG_FILES, sizeof(BagFile))) == NULL) {
        return -1;
    }
    if (g_nfiles == BAG_FILES) {
        return 0;               /* (eidos's own, file 0, takes the rest) */
    }
    memset(&g_files[g_nfiles], 0, sizeof g_files[g_nfiles]);
    snprintf(g_files[g_nfiles].ns, sizeof g_files[g_nfiles].ns, "%s", ns);
    return g_nfiles++;
}

/* An item member file's index from its name ("627", "0627", "627.bin"). */
static int member_index(const char *name)
{
    int n = 0, k = 0;

    while (name[k] >= '0' && name[k] <= '9' && k < 6) {
        n = n * 10 + (name[k++] - '0');
    }
    if (k == 0 || (name[k] != '\0' && name[k] != '.')) {
        return -1;
    }
    return n;
}

typedef struct OwnerWalk {
    int file;
} OwnerWalk;

static int owner_entry(void *ctx, const char *name)
{
    int n = member_index(name), k = n > 0 ? added_index((u32)n) : -1;

    if (k >= 0) {
        g_owner[k] = (u8)(((OwnerWalk *)ctx)->file + 1);
    }
    return 0;
}

/* Which file each added TM belongs to: the last mod (in load order) that
 * ships its item record, else eidos's own. */
static void owners(void)
{
    u32 m;

    if (g_owners_done) {
        return;
    }
    g_owners_done = 1;
    memset(g_owner, 0, sizeof g_owner);
    g_nfiles = 0;
    if (file_named("eidos.bag") != 0) {
        return;
    }
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200], ns[80];
        const char *root = bw_mod_root_at(m), *base;
        OwnerWalk w;
        size_t k;

        if (root == NULL) {
            continue;
        }
        /* the mod's folder name (the per-save layer's names are
         * [A-Za-z0-9._-]: anything else becomes '_') */
        base = root + strlen(root);
        while (base > root && base[-1] != '/' && base[-1] != '\\') {
            base--;
        }
        snprintf(ns, sizeof ns, "%.70s.bag", base);
        for (k = 0; ns[k] != '\0'; k++) {
            char c = ns[k];

            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-')) {
                ns[k] = '_';
            }
        }
        w.file = file_named(ns);
        snprintf(path, sizeof path, "%s/a/0/2/4", root);
        (void)bw_plat_list_dir(path, owner_entry, &w);
    }
}

/* The per-save layer's calls for one of the port's namespaces; the
 * caller's namespace (a mod's, when a mod's code is running) comes back
 * after. */
static int ext_io(BagFile *f, int store)
{
    char prev[80];
    const char *ns = bw_mod_savedata_namespace();
    int rc;

    snprintf(prev, sizeof prev, "%s", ns != NULL ? ns : "");
    bw_mod_savedata_set_namespace(f->ns);
    rc = store ? bw_mod_savedata_store(&f->ext, sizeof f->ext) : bw_mod_savedata_load(&f->ext, sizeof f->ext);
    bw_mod_savedata_set_namespace(prev);
    return rc;
}

static void ext_read(BagFile *f)
{
    if (f->loaded) {
        return;
    }
    f->loaded = 1;
    f->onDisk = ext_io(f, 0) == (int)sizeof f->ext && f->ext.magic == BAG_MAGIC && f->ext.count <= ADDED_TMS;
    if (!f->onDisk || g_new_game) {
        memset(&f->ext, 0, sizeof f->ext);
        f->ext.magic = BAG_MAGIC;
    }
}

static int pocket_has(u16 item)
{
    u32 i;

    for (i = 0; i < POCKET_SLOTS; i++) {
        if (g_pocket[i].itemId == item && g_pocket[i].quantity != 0) {
            return 1;
        }
    }
    return 0;
}

/* The pocket of `bag`: the block's slots, then the added TMs its files
 * hold, in the pocket's order. */
static void bind(const struct MyItem *bag)
{
    int i;
    u32 k, at = BAG_TMHM_COUNT;

    if (g_bound == bag) {
        return;
    }
    g_bound = bag;
    memset(g_pocket, 0, sizeof g_pocket);
    memcpy(g_pocket, bag->tmhm, sizeof bag->tmhm);
    owners();
    for (i = 0; i < g_nfiles; i++) {
        BagFile *f = &g_files[i];

        ext_read(f);
        for (k = 0; k < f->ext.count && at < POCKET_SLOTS; k++) {
            const struct BagSlot *s = &f->ext.slots[k];

            /* (an item the block holds too is not taken twice) */
            if (s->itemId != 0 && s->quantity != 0 && !pocket_has(s->itemId)) {
                g_pocket[at++] = *s;
            }
        }
    }
    MYITEM_SAVEDATA_SortPocketByItemOrder(g_pocket, POCKET_SLOTS);
}

/* The game saves (sdk/mod_event_session.c, save.begin, before the game
 * writes its blocks): the pocket goes back into the block, the game's own
 * machines, and each added TM into its mod's file, so the block holds no
 * item the plain game does not know (with the mod off, the bag has no
 * empty "None" rows).  A file that was read is written again, one that
 * holds nothing and had no file is not written. */
void bw_tm_pocket_save(void *bagBlock)
{
    struct MyItem *bag = (struct MyItem *)bagBlock;
    u32 i, n = 0;
    int pass, f;

    if (bag == NULL || !pocket_on()) {
        return;
    }
    /* (a game that never asked for the pocket since the load: built here,
     * so a save from before moves its added TMs out of the block) */
    bind(bag);
    owners();
    for (f = 0; f < g_nfiles; f++) {
        ext_read(&g_files[f]);          /* (whether the save has it) */
        g_files[f].ext.count = 0;
        memset(g_files[f].ext.slots, 0, sizeof g_files[f].ext.slots);
    }
    memset(bag->tmhm, 0, sizeof bag->tmhm);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < POCKET_SLOTS; i++) {
            const struct BagSlot *s = &g_pocket[i];
            int k = added_index(s->itemId);

            if (s->quantity == 0 || (pass == 0) != (k < 0)) {
                continue;
            }
            if (k < 0 && n < BAG_TMHM_COUNT) {
                bag->tmhm[n++] = *s;
            } else if (g_nfiles > 0) {
                /* an added TM to its mod's file; anything else past the
                 * block's room (a mod's own item in this pocket) to eidos's */
                BagFile *file = &g_files[k >= 0 && g_owner[k] != 0 ? g_owner[k] - 1 : 0];

                if (file->ext.count < ADDED_TMS) {
                    file->ext.slots[file->ext.count++] = *s;
                }
            }
        }
    }
    for (f = 0; f < g_nfiles; f++) {
        BagFile *file = &g_files[f];

        file->ext.magic = BAG_MAGIC;
        if (file->loaded && (file->onDisk || file->ext.count != 0)) {
            if (ext_io(file, 1) == 0) {
                file->onDisk = 1;
            }
        }
    }
}

static void files_unread(void)
{
    int i;

    for (i = 0; i < g_nfiles; i++) {
        g_files[i].loaded = 0;
    }
}

/* game.new (sdk/mod_event_session.c): a new game's bag; the files beside
 * the save are an older game's, so none is read (each one there is written
 * over at the first save). */
void bw_tm_pocket_new_game(void)
{
    g_new_game = 1;
    g_bound = NULL;
    files_unread();
}

/* save.load (sdk/mod_event_session.c): the loaded save's own block and
 * files, built again when the pocket is next asked for. */
void bw_tm_pocket_save_loaded(void)
{
    g_new_game = 0;
    g_bound = NULL;
    files_unread();
}

/* The mod list changed: the owners are worked out again (the pocket in
 * play stays as it is until the next save or load). */
void bw_tm_pocket_forget(void)
{
    g_owners_done = 0;
}

/* ------------------------------------------------------------------ */
/* The game's functions                                                */
/* ------------------------------------------------------------------ */

void MYITEM_SAVEDATA_Init(struct MyItem *bag)
{
    memset(bag, 0, sizeof(struct MyItem));
    if (g_bound == bag) {
        g_bound = NULL;
    }
}

/* Array and size of a pocket. */
u32 MYITEM_SAVEDATA_GetPocket(struct MyItem *bag, u32 pocket, struct BagSlot **slots, u32 *count)
{
    switch (pocket) {
    case BAG_POCKET_KEY_ITEMS:
        *slots = bag->keyItems;
        *count = BAG_KEY_ITEMS_COUNT;
        break;
    case BAG_POCKET_ITEMS:
        *slots = bag->items;
        *count = BAG_ITEMS_COUNT;
        break;
    case BAG_POCKET_BERRIES:
        *slots = bag->berries;
        *count = BAG_BERRIES_COUNT;
        break;
    case BAG_POCKET_MEDICINE:
        *slots = bag->medicine;
        *count = BAG_MEDICINE_COUNT;
        break;
    case BAG_POCKET_TMHM:
        if (pocket_on()) {
            bind(bag);
            *slots = g_pocket;
            *count = POCKET_SLOTS;
        } else {
            *slots = bag->tmhm;
            *count = BAG_TMHM_COUNT;
        }
        break;
    default:
        *slots = NULL;
        break;
    }
    return pocket;
}

/* Slot `index` of a pocket, NULL past its end. */
struct BagSlot *MYITEM_SAVEDATA_GetSlot(struct MyItem *bag, u32 pocket, u32 index)
{
    struct BagSlot *slots = NULL;
    u32 count = 0;

    if (pocket > BAG_POCKET_KEY_ITEMS) {
        return NULL;            /* (the game's reads an unset array here; no caller passes one) */
    }
    MYITEM_SAVEDATA_GetPocket(bag, pocket, &slots, &count);
    if (index >= count) {
        return NULL;
    }
    return &slots[index];
}
