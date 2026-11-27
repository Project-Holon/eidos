/*
 * sdk/dex_save.c -- the Pokedex's record of each species past the save's
 * room (docs/game-tables.md, "New species").
 *
 * The game's dex block in the save (arm9 zukan_save.c, struct ZukanSave)
 * keeps one bit per species in its caught, seen and shown rows, 0x54 bytes
 * each: room for species 1..672.  A mod raises the species limit
 * (limit.species, up to 2047), so the bits of a species past 672 live
 * here, in the dex extension: one file per mod beside the save, the bits
 * of the species that mod adds (the mod that ships the species' stats
 * record, a/0/1/6 member n; a later mod wins, as for the data), stored
 * through the per-save layer (sdk/mod_savedata.c, namespace
 * "<mod folder>.dex") and written with each game save.  A species no mod
 * ships goes to "eidos.dex".  The game's own block keeps its layout and
 * stays valid for the plain game.  The last species the dex showed is a
 * 10-bit field in the block; a species past 1023 is kept in its mod's
 * file and the block holds 0.
 *
 * The game's functions that read or write those rows take their place here
 * under the game's own names (the boot plan cuts the game's copies, as for
 * stubs/reimpl_vm_control.c): each does what the game's does, through
 * row_byte, which answers the block's byte below 0x54 and the extension's
 * at and past it.  The regional counts also stop at the regional table's
 * end (the game's table has a row for species 0..649 only).
 *
 * Forms.  The block keeps a seen bit and a shown bit (normal and shiny)
 * for each form of the species in the game's form table (arm9 0x0209DE4C,
 * sZukanFormTable: {species, form count} rows, 72 bits in all, full).
 * Arceus and Genesect are not in it: the game's Pokedex lists them as one
 * form.  A mod adds forms by putting the species' forms at stats slots of
 * its own (formStatsIndex from 715, sdk/dex_data.c bw_dex_mod_forms); the
 * Pokedex then lists every form its stats record counts (Arceus 0 to 17
 * with a Fairy plate).  A form the table has a bit for keeps that bit; the
 * bits of the others are in the dex extension of the mod that ships the
 * species' stats record, after its species bits (DexFormsHead, then one
 * DexForms a species).  An older eidos reads the species part alone.  With
 * the mod off the species has the game's forms and bits again, and the
 * block always holds a form the game knows as the one shown.  The game
 * keeps no caught bit per form, and neither does the extension.
 *
 * Pokemon in a form past the box record's room (32 and up, sdk/mon_form.c)
 * keep their form in the same file, after the form bits (a second head,
 * then one BwMonForm a Pokemon of the species the mod ships).
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "dex_data.h"
#include "mon_form.h"
#include "plat_dir.h"
#include "types.h"
#include "vfs.h"

/* ------------------------------------------------------------------ */
/* The game's own records and calls                                    */
/* ------------------------------------------------------------------ */

/* arm9 zukan_save.c: the dex block, the game's layout. */
struct ZukanSave {
    u32 magic;
    u32 nationalDexEnabled : 1;
    u32 dexObtained : 10;
    u32 viewMode : 1;
    u32 lastViewedSpecies : 10;
    u32 unusedBits : 10;
    u8 caughtFlags[0x54];
    u8 seenFlags[4][0x54];
    u8 viewGenderFlags[4][0x54];
    u8 seenFormFlags[2][9];
    u8 viewFormFlags[2][9];
    u8 languageFlags[0x1B0];
    u32 spindaPersonality;
};

typedef char dex_block_is_0x4d4[sizeof(struct ZukanSave) == 0x4D4 ? 1 : -1];

struct PokemonParam;
struct PersonalData;

/* The game's functions are weak: the library also links without the game
 * (tests/test_armdata_real.c lists the functions above by address, which
 * brings this file in).  With the game they are always there. */
#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

/* the game's prototypes (arm9 poke_tool.h, poke_personal.h): the same
 * shape, as the wasm32 build links by it */
extern u32 POKE_TOOL_DecodePartyParam(struct PokemonParam *pp) GAME_FN;
extern u32 POKE_TOOL_EncodePartyParam(struct PokemonParam *pp, u32 didDecode) GAME_FN;
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, u32 arg) GAME_FN;
extern u32 POKE_TOOL_IsShiny(struct PokemonParam *pp) GAME_FN;
extern u8 POKE_TOOL_GetGender(struct PokemonParam *pp) GAME_FN;
extern struct PersonalData *POKE_PERSONAL_Create(u32 species, int form, u32 heap) GAME_FN;
extern void POKE_PERSONAL_Delete(struct PersonalData *data) GAME_FN;
extern u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field) GAME_FN;

/* the game's own, kept (arm9 zukan_save.c) */
extern u32 ZUKAN_SAVE_IsInvalidSpecies(u32 species) GAME_FN;
extern void ZUKAN_SAVE_SetLanguageFlag(struct ZukanSave *pZukan, u32 species, u32 language) GAME_FN;
extern int ZUKAN_SAVE_IsCountedForRegionalCompletion(u32 species) GAME_FN;
extern u32 ZUKAN_SAVE_GetFormBitBase(u16 species) GAME_FN;
extern u32 ZUKAN_SAVE_GetFormTableIndex(u32 species) GAME_FN;

/* arm9 zukan_save.c: the game's form table, {species, form count} rows, a
 * species 0 ends it (the ARM9 image's data, docs/armdata.md) */
struct ZukanFormRow {
    u16 species;
    u16 count;
};
extern void *bw_dsref_sZukanFormTable GAME_FN;

/* arm9 save_control.c: the save and its work area (every block of the
 * main save, as the game writes them) */
struct SaveControl;
extern struct SaveControl *SAVE_CONTROL_Get(void) GAME_FN;
extern void *SAVE_CONTROL_GetWorkArea(struct SaveControl *saveControl, u32 *size) GAME_FN;

/* sdk/mod_savedata.c: the per-save layer */
extern const char *bw_mod_savedata_namespace(void);
extern void bw_mod_savedata_set_namespace(const char *ns);
extern int bw_mod_savedata_load(void *buf, u32 size);
extern int bw_mod_savedata_store(const void *buf, u32 size);

/* ------------------------------------------------------------------ */
/* The extension                                                       */
/* ------------------------------------------------------------------ */

#define DEX_SPECIES 2048u               /* the species limit's ceiling + 1 */
#define DEX_ROW     (DEX_SPECIES / 8u)
#define DEX_MAGIC   0x58454444u         /* "DDEX" */
#define DEX_FILES   33                  /* one per mod (the per-save layer's 32), and eidos's own */

typedef struct DexExt {
    u32 magic;
    u16 lastViewed;                     /* past 1023, else 0 */
    u16 pad;
    u8 caught[DEX_ROW];
    u8 seen[4][DEX_ROW];
    u8 view[4][DEX_ROW];
} DexExt;

/* The form bits after the species bits: a head, then one record a species
 * that has a bit set. */
#define DEX_FORMS_MAGIC 0x4D524F46u     /* "FORM" */
#define DEX_FORM_BITS   256u            /* a stats record's form count is a byte */
#define DEX_FORM_ROW    (DEX_FORM_BITS / 8u)

typedef struct DexFormsHead {
    u32 magic;
    u16 version;                        /* 1 */
    u16 count;                          /* DexForms records after it */
} DexFormsHead;

typedef struct DexForms {
    u16 species;
    u16 pad;
    u8 seen[2][DEX_FORM_ROW];           /* [shiny], one bit a form */
    u8 view[2][DEX_FORM_ROW];           /* the form shown, [shiny] */
} DexForms;

/* The Pokemon whose form is past the box record's room, after the form
 * records: a DexFormsHead with this magic, then `count` BwMonForm. */
#define DEX_MONS_MAGIC  0x534E4F4Du     /* "MONS" */
#define DEX_MONS_MAX    0xFFFFu         /* (the head's count) */

/* A file: its namespace, its bits once read. */
typedef struct DexFile {
    char ns[80];
    int loaded;
    int onDisk;                         /* the save has this file */
    DexExt ext;
    DexForms *forms;                    /* (malloc'd) */
    u32 nforms, capforms;
    BwMonForm *mons;                    /* the Pokemon records read, until merged (sdk/mon_form.c) */
    u32 nmons;
} DexFile;

/* The files, on the heap at the first use (the 32-bit image has little
 * room, cmake/check_image.cmake); eidos's own alone if that fails. */
static DexFile g_file0;
static DexFile *g_files = &g_file0;
static int g_cap = 1;
static int g_nfiles;
static u8 g_owner[DEX_SPECIES];         /* species -> file + 1; 0 not worked out */
static u16 g_form_count[DEX_SPECIES];   /* species -> its Pokedex forms + 1; 0 not worked out */
static int g_owners_done;

/* The per-save layer's calls for one of the port's namespaces; the
 * caller's namespace (a mod's, when a mod's code is running) comes back
 * after. */
static int ext_io(DexFile *f, void *buf, u32 size, int store)
{
    char prev[80];
    const char *ns = bw_mod_savedata_namespace();
    int rc;

    snprintf(prev, sizeof prev, "%s", ns != NULL ? ns : "");
    bw_mod_savedata_set_namespace(f->ns);
    rc = store ? bw_mod_savedata_store(buf, size) : bw_mod_savedata_load(buf, size);
    bw_mod_savedata_set_namespace(prev);
    return rc;
}

/* A form record with no bit set (not written). */
static int forms_empty(const DexForms *r)
{
    const u8 *p = &r->seen[0][0];
    size_t i;

    for (i = 0; i < sizeof r->seen + sizeof r->view; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static u32 forms_used(const DexFile *f)
{
    u32 i, n = 0;

    for (i = 0; i < f->nforms; i++) {
        n += forms_empty(&f->forms[i]) ? 0u : 1u;
    }
    return n;
}

/* The file read: the species bits, then the form records and the Pokemon
 * records, if any. */
static int ext_load(DexFile *f)
{
    u32 cap = (u32)(sizeof f->ext + sizeof(DexFormsHead) + DEX_SPECIES * sizeof(DexForms) + sizeof(DexFormsHead)
                    + DEX_MONS_MAX * sizeof(BwMonForm));
    u8 *buf = (u8 *)malloc(cap);
    int got, ok;

    free(f->forms);
    f->forms = NULL;
    f->nforms = f->capforms = 0;
    free(f->mons);
    f->mons = NULL;
    f->nmons = 0;
    if (buf == NULL) {
        return 0;
    }
    got = ext_io(f, buf, cap, 0);
    ok = got >= (int)sizeof f->ext;
    if (ok) {
        DexFormsHead h;
        u32 at = (u32)sizeof f->ext;

        memcpy(&f->ext, buf, sizeof f->ext);
        if ((u32)got >= at + sizeof h) {
            memcpy(&h, buf + at, sizeof h);
            if (h.magic == DEX_FORMS_MAGIC && h.version == 1
                && (u32)got >= at + sizeof h + h.count * (u32)sizeof(DexForms)
                && (f->forms = (DexForms *)malloc((h.count > 0 ? h.count : 1u) * sizeof(DexForms))) != NULL) {
                memcpy(f->forms, buf + at + sizeof h, h.count * sizeof(DexForms));
                f->nforms = f->capforms = h.count;
                at += (u32)sizeof h + h.count * (u32)sizeof(DexForms);
            }
        }
        if ((u32)got >= at + sizeof h) {
            memcpy(&h, buf + at, sizeof h);
            if (h.magic == DEX_MONS_MAGIC && h.version == 1
                && (u32)got >= at + sizeof h + h.count * (u32)sizeof(BwMonForm)
                && (f->mons = (BwMonForm *)malloc((h.count > 0 ? h.count : 1u) * sizeof(BwMonForm))) != NULL) {
                memcpy(f->mons, buf + at + sizeof h, h.count * sizeof(BwMonForm));
                f->nmons = h.count;
            }
        }
    }
    free(buf);
    return ok;
}

static int file_of(u32 species);

/* The Pokemon records a file holds: those of the species its mod ships. */
static u32 mons_of(int file)
{
    u32 i, n = 0;

    for (i = 0; i < bw_mon_form_count() && n < DEX_MONS_MAX; i++) {
        n += file_of(bw_mon_form_at(i)->species) == file ? 1u : 0u;
    }
    return n;
}

/* The file written: the species bits, then the form records with a bit,
 * then the Pokemon records. */
static int ext_store(DexFile *f)
{
    DexFormsHead h;
    int file = (int)(f - g_files);
    u32 used = forms_used(f), mons = mons_of(file), i, size;
    u8 *buf, *at;
    int rc;

    size = (u32)sizeof f->ext
           + (used > 0 || mons > 0 ? (u32)sizeof h + used * (u32)sizeof(DexForms) : 0u)
           + (mons > 0 ? (u32)sizeof h + mons * (u32)sizeof(BwMonForm) : 0u);
    buf = (u8 *)malloc(size);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, &f->ext, sizeof f->ext);
    at = buf + sizeof f->ext;
    if (used > 0 || mons > 0) {
        /* (an empty form head when only Pokemon records follow: the reader
         * takes the sections in order) */
        h.magic = DEX_FORMS_MAGIC;
        h.version = 1;
        h.count = (u16)used;
        memcpy(at, &h, sizeof h);
        at += sizeof h;
        for (i = 0; i < f->nforms; i++) {
            if (!forms_empty(&f->forms[i])) {
                memcpy(at, &f->forms[i], sizeof(DexForms));
                at += sizeof(DexForms);
            }
        }
    }
    if (mons > 0) {
        u32 n = 0;

        h.magic = DEX_MONS_MAGIC;
        h.version = 1;
        h.count = (u16)mons;
        memcpy(at, &h, sizeof h);
        at += sizeof h;
        for (i = 0; i < bw_mon_form_count() && n < mons; i++) {
            if (file_of(bw_mon_form_at(i)->species) == file) {
                memcpy(at, bw_mon_form_at(i), sizeof(BwMonForm));
                at += sizeof(BwMonForm);
                n++;
            }
        }
    }
    rc = ext_io(f, buf, size, 1);
    free(buf);
    return rc;
}

static int file_named(const char *ns)
{
    int i;

    for (i = 0; i < g_nfiles; i++) {
        if (strcmp(g_files[i].ns, ns) == 0) {
            return i;
        }
    }
    if (g_cap == 1) {
        DexFile *all = (DexFile *)calloc(DEX_FILES, sizeof(DexFile));

        if (all != NULL) {
            memcpy(all, g_files, (size_t)g_nfiles * sizeof(DexFile));
            g_files = all;
            g_cap = DEX_FILES;
        }
    }
    if (g_nfiles == g_cap) {
        return 0;               /* (eidos's own, file 0, takes the rest) */
    }
    memset(&g_files[g_nfiles], 0, sizeof g_files[g_nfiles]);
    snprintf(g_files[g_nfiles].ns, sizeof g_files[g_nfiles].ns, "%s", ns);
    return g_nfiles++;
}

/* A stats member file's index from its name ("715", "0715", "715.bin"). */
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
    int n = member_index(name);

    if (n > 0 && n < (int)DEX_SPECIES) {
        g_owner[n] = (u8)(((OwnerWalk *)ctx)->file + 1);
    }
    return 0;
}

/* Which file each species belongs to: the last mod (in load order) that
 * ships its stats record, else eidos's own.  The species bits of one past
 * 672 and the form bits the block has no room for are kept there. */
static void owners(void)
{
    u32 m;
    int i;

    if (g_owners_done) {
        return;
    }
    g_owners_done = 1;
    memset(g_owner, 0, sizeof g_owner);
    memset(g_form_count, 0, sizeof g_form_count);
    for (i = 0; i < g_nfiles; i++) {
        free(g_files[i].forms);
        free(g_files[i].mons);
    }
    g_nfiles = 0;
    (void)file_named("eidos.dex");      /* file 0 */
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200], ns[80];
        OwnerWalk w;

        const char *root = bw_mod_root_at(m), *base;
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
        snprintf(ns, sizeof ns, "%.70s.dex", base);
        for (k = 0; ns[k] != '\0'; k++) {
            char c = ns[k];

            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
                ns[k] = '_';
            }
        }
        w.file = file_named(ns);
        snprintf(path, sizeof path, "%s/a/0/1/6", bw_mod_root_at(m));
        (void)bw_plat_list_dir(path, owner_entry, &w);
    }
}

/* A new game since the last save.load: the files beside the save are an
 * older game's, so the extensions start empty (each old file is written
 * over, empty, at the first save). */
static int g_new_game;

/* A file's bits, read from beside the save when first asked. */
static void ext_read(DexFile *f)
{
    if (f->loaded) {
        return;
    }
    f->loaded = 1;
    f->onDisk = ext_load(f) && f->ext.magic == DEX_MAGIC;
    if (!f->onDisk || g_new_game) {
        memset(&f->ext, 0, sizeof f->ext);
        f->ext.magic = DEX_MAGIC;
        f->nforms = 0;
        f->nmons = 0;
    }
    /* the Pokemon records go into the table in force (sdk/mon_form.c) */
    bw_mon_form_merge(f->mons, f->nmons);
    free(f->mons);
    f->mons = NULL;
    f->nmons = 0;
}

/* The file a species' records are kept in: the mod's that ships its stats
 * record, else eidos's own. */
static int file_of(u32 species)
{
    owners();
    return species < DEX_SPECIES && g_owner[species] != 0 ? g_owner[species] - 1 : 0;
}

/* Every file of the save in force read (sdk/mon_form.c asks before its
 * first question after a load: a Pokemon's record may be in any of them). */
void bw_dex_ext_read_all(void)
{
    int i;

    owners();
    for (i = 0; i < g_nfiles; i++) {
        ext_read(&g_files[i]);
    }
}

/* The extension that holds `species` (past 672), read when first asked. */
static DexExt *ext_of(u32 species)
{
    DexFile *f;

    owners();
    f = &g_files[species < DEX_SPECIES && g_owner[species] != 0 ? g_owner[species] - 1 : 0];
    ext_read(f);
    return &f->ext;
}

/* An extension that holds no bit at all. */
static int ext_empty(const DexExt *e)
{
    const u8 *p = (const u8 *)e + offsetof(DexExt, lastViewed);
    size_t i;

    for (i = 0; i < sizeof *e - offsetof(DexExt, lastViewed); i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* The game saves (sdk/mod_event_session.c, save.begin): each extension
 * that was read is stored with it, so it holds what the save holds.  One
 * that holds nothing and had no file is not written (no empty file beside
 * each save). */
void bw_dex_ext_save(void)
{
    struct SaveControl *save = SAVE_CONTROL_Get != NULL ? SAVE_CONTROL_Get() : NULL;
    int i;

    /* the Pokemon records: every file read (a record may move to another
     * file when its Pokemon evolves), and only those of Pokemon the save
     * still holds kept (sdk/mon_form.c) */
    if (bw_mon_form_count() > 0) {
        bw_dex_ext_read_all();
        if (save != NULL && SAVE_CONTROL_GetWorkArea != NULL) {
            u32 size = 0;
            const void *work = SAVE_CONTROL_GetWorkArea(save, &size);

            bw_mon_form_keep_saved(work, size);
        }
    }
    for (i = 0; i < g_nfiles; i++) {
        DexFile *f = &g_files[i];

        if (f->loaded && (f->onDisk || !ext_empty(&f->ext) || forms_used(f) > 0 || mons_of(i) > 0)) {
            if (ext_store(f) == 0) {
                f->onDisk = 1;
            }
        }
    }
}

/* game.new (sdk/mod_event_session.c): a new game's dex, every extension
 * empty. */
void bw_dex_ext_new_game(void)
{
    int i;

    g_new_game = 1;
    bw_mon_form_clear();
    owners();
    for (i = 0; i < g_nfiles; i++) {
        g_files[i].loaded = 0;
        ext_read(&g_files[i]);
    }
}

void bw_dex_ext_forget(void);

/* save.load (sdk/mod_event_session.c): the loaded save's own files. */
void bw_dex_ext_save_loaded(void)
{
    g_new_game = 0;
    bw_mon_form_clear();
    bw_dex_ext_forget();
}

/* The save in force or the mod list changed: read again when next asked
 * (the Pokemon records in force stay; a file read again adds the ones it
 * has that are not). */
void bw_dex_ext_forget(void)
{
    int i;

    for (i = 0; i < g_nfiles; i++) {
        g_files[i].loaded = 0;
    }
    g_owners_done = 0;
    bw_mon_form_unready();
}

/* The byte that holds species' bit in row `row` of a bit group: the
 * block's when it has room, else its mod's extension's. */
enum { ROW_CAUGHT, ROW_SEEN, ROW_VIEW };

static u8 *row_byte(struct ZukanSave *z, int group, u32 row, u32 species)
{
    u32 byte = (u16)(species - 1) >> 3;
    DexExt *e;

    if (byte < 0x54u) {
        return group == ROW_CAUGHT ? &z->caughtFlags[byte]
               : group == ROW_SEEN ? &z->seenFlags[row][byte] : &z->viewGenderFlags[row][byte];
    }
    if (byte >= DEX_ROW) {
        static u8 none;     /* (past the ceiling: never reached, the limit stops it) */

        none = 0;
        return &none;
    }
    e = ext_of(species);
    return group == ROW_CAUGHT ? &e->caught[byte] : group == ROW_SEEN ? &e->seen[row][byte] : &e->view[row][byte];
}

static void set_species(struct ZukanSave *z, int group, u32 row, u32 species)
{
    u16 bit = (u16)(species - 1);

    *row_byte(z, group, row, species) |= (u8)(1 << (bit & 7));
}

static void reset_species(struct ZukanSave *z, int group, u32 row, u32 species)
{
    u16 bit = (u16)(species - 1);

    *row_byte(z, group, row, species) &= (u8)~(1 << (bit & 7));
}

static u32 test_species(struct ZukanSave *z, int group, u32 row, u32 species)
{
    u16 bit = (u16)(species - 1);

    return (*row_byte(z, group, row, species) >> (bit & 7)) & 1u;
}

/* ------------------------------------------------------------------ */
/* The game's functions, through the rows above                        */
/* ------------------------------------------------------------------ */

void ZUKAN_SAVE_Init(struct ZukanSave *pZukan)
{
    /* (the game runs this on the block before it loads a save too: the
     * extensions start empty at game.new, bw_dex_ext_new_game) */
    memset(pZukan, 0, sizeof(struct ZukanSave));
    pZukan->magic = 0xBEEFCAFE;
}

void ZUKAN_SAVE_SetSeenGenderFlag(struct ZukanSave *pZukan, u32 species, int sel, int half)
{
    /* rows: 0 male, 1 female, 2 shiny male, 3 shiny female; a genderless one is a male's */
    set_species(pZukan, ROW_SEEN, (sel == 1 ? 1u : 0u) + (half == 1 ? 2u : 0u), species);
}

void ZUKAN_SAVE_SetLastViewedSpecies(struct ZukanSave *pZukan, u32 value)
{
    int i;

    owners();
    for (i = 0; i < g_nfiles; i++) {        /* (one place holds it: the others forget theirs) */
        if (g_files[i].loaded) {
            g_files[i].ext.lastViewed = 0;
        }
    }
    if (value > 1023u) {
        ext_of(value)->lastViewed = (u16)value;
        pZukan->lastViewedSpecies = 0;      /* (a value the plain game takes) */
    } else {
        pZukan->lastViewedSpecies = value;
    }
}

u16 ZUKAN_SAVE_GetLastViewedSpecies(struct ZukanSave *pZukan)
{
    int i;

    owners();
    for (i = 0; i < g_nfiles; i++) {        /* (each file once read; the one that holds it answers) */
        DexFile *f = &g_files[i];

        ext_read(f);
        if (f->ext.lastViewed != 0) {
            return f->ext.lastViewed;
        }
    }
    return (u16)pZukan->lastViewedSpecies;
}

/* ------------------------------------------------------------------ */
/* Forms                                                               */
/* ------------------------------------------------------------------ */

/* The game's form page has room for 64 entries (ov208 Ov208Form.ents), one
 * a form in each colour seen (stubs/ov/patch_ov208_zukan_detail_form.c
 * stops the list there): the Pokedex lists 64 forms of a species at most.
 * The bits of the others are kept. */
#define DEX_VIEW_FORMS 64u

/* The game's form table's count for a species; 0 when it is not listed. */
static u32 table_forms(u32 species)
{
    const struct ZukanFormRow *rows;
    u32 i;

    if (&bw_dsref_sZukanFormTable == NULL || bw_dsref_sZukanFormTable == NULL) {
        return 0;
    }
    rows = (const struct ZukanFormRow *)bw_dsref_sZukanFormTable;
    i = ZUKAN_SAVE_GetFormTableIndex(species);
    return i != (u32)-1 ? rows[i].count : 0u;
}

/* The forms the Pokedex keeps for a species: the game's table's, or, when
 * a mod puts the species' forms at its own stats slots, every form its
 * stats record counts; 1 for a species with none. */
static u32 dex_forms(u32 species)
{
    u32 n, t, first, added;

    if (species == 0 || species >= DEX_SPECIES) {
        return 1;
    }
    owners();
    if (g_form_count[species] != 0) {
        return g_form_count[species] - 1u;
    }
    t = table_forms(species);
    added = bw_dex_mod_forms(species, &first);
    n = added > 0 ? added + 1u : t;
    if (n < t) {
        n = t;
    }
    if (n == 0) {
        n = 1;
    }
    g_form_count[species] = (u16)(n + 1u);
    return n;
}

/* The form record of a species in its file, made when `make`; NULL when
 * there is none.  A species the game's table does not list may have been
 * seen before a mod gave it forms: a new record starts with its first form
 * seen and shown as the species' rows have it. */
static DexForms *forms_of(struct ZukanSave *z, u32 species, int make)
{
    DexFile *f;
    DexForms *r;
    u32 i;

    owners();
    f = &g_files[species < DEX_SPECIES && g_owner[species] != 0 ? g_owner[species] - 1 : 0];
    ext_read(f);
    for (i = 0; i < f->nforms; i++) {
        if (f->forms[i].species == species) {
            return &f->forms[i];
        }
    }
    if (!make) {
        return NULL;
    }
    if (f->nforms == f->capforms) {
        u32 cap = f->capforms > 0 ? f->capforms * 2u : 8u;
        DexForms *more = (DexForms *)realloc(f->forms, cap * sizeof(DexForms));

        if (more == NULL) {
            return NULL;
        }
        f->forms = more;
        f->capforms = cap;
    }
    r = &f->forms[f->nforms++];
    memset(r, 0, sizeof *r);
    r->species = (u16)species;
    if (table_forms(species) == 0) {
        r->seen[0][0] = (u8)(test_species(z, ROW_SEEN, 0, species) | test_species(z, ROW_SEEN, 1, species));
        r->seen[1][0] = (u8)(test_species(z, ROW_SEEN, 2, species) | test_species(z, ROW_SEEN, 3, species));
        if (test_species(z, ROW_VIEW, 2, species) || test_species(z, ROW_VIEW, 3, species)) {
            r->view[1][0] = 1;
        } else if (test_species(z, ROW_VIEW, 0, species) || test_species(z, ROW_VIEW, 1, species)) {
            r->view[0][0] = 1;
        }
    }
    return r;
}

/* The byte and mask of a form's bit, seen (`view` 0) or shown (1): the
 * block's for a form the game's table has, else the extension's.  NULL for
 * a form with no record when `make` is 0 (no bit set). */
static u8 *form_byte(struct ZukanSave *z, int view, u32 shiny, u32 species, u32 form, int make, u8 *mask)
{
    u32 t = table_forms(species);
    DexForms *r;

    shiny = shiny == 1 ? 1u : 0u;
    if (form < t) {
        u32 bit = (u16)(ZUKAN_SAVE_GetFormBitBase((u16)species) + form);

        *mask = (u8)(1u << (bit & 7));
        return view ? &z->viewFormFlags[shiny][bit >> 3] : &z->seenFormFlags[shiny][bit >> 3];
    }
    if (form >= DEX_FORM_BITS || (r = forms_of(z, species, make)) == NULL) {
        return NULL;
    }
    *mask = (u8)(1u << (form & 7));
    return view ? &r->view[shiny][form >> 3] : &r->seen[shiny][form >> 3];
}

static void form_set(struct ZukanSave *z, int view, u32 shiny, u32 species, u32 form, int on)
{
    u8 mask, *b = form_byte(z, view, shiny, species, form, on, &mask);

    if (b != NULL) {
        if (on) {
            *b |= mask;
        } else {
            *b &= (u8)~mask;
        }
    }
}

static u32 form_test(struct ZukanSave *z, int view, u32 shiny, u32 species, u32 form)
{
    u8 mask, *b = form_byte(z, view, shiny, species, form, 0, &mask);

    return b != NULL && (*b & mask) != 0;
}

u16 ZUKAN_SAVE_GetFormCount(u32 species)
{
    u32 n = dex_forms(species);

    return (u16)(n < DEX_VIEW_FORMS ? n : DEX_VIEW_FORMS);
}

void ZUKAN_SAVE_SetSeenFormFlag(struct ZukanSave *pZukan, u32 species, u32 sel, u32 shiny, u32 form)
{
    u32 n = dex_forms(species), k;

    (void)sel;
    if (n < 2) {
        return;
    }
    if (form >= n) {
        form = 0;
    }
    switch (species) {
    case 0x15F:     /* Castform */
    case 0x1A5:     /* Cherrim */
    case 0x22B:     /* Darmanitan */
    case 0x288:     /* Meloetta */
        /* (their forms change in battle, which registers a Pokemon as it
         * comes in: the game marks all of its forms of them seen) */
        for (k = 0; k < table_forms(species); k++) {
            form_set(pZukan, 0, shiny, species, k, 1);
        }
        break;
    }
    form_set(pZukan, 0, shiny, species, form, 1);
}

void ZUKAN_SAVE_UpdateViewFlags(struct ZukanSave *pZukan, u16 species, u32 gender, u32 shiny, u32 form)
{
    u32 n = dex_forms(species), t = table_forms(species), k;

    if (n >= 2) {
        for (k = 0; k < n; k++) {
            form_set(pZukan, 1, 0, species, k, 0);
            form_set(pZukan, 1, 1, species, k, 0);
        }
        if (form >= n) {
            form = 0;
        }
        form_set(pZukan, 1, shiny, species, form, 1);
        if (form >= t && t > 0) {
            /* a form the game has no bit for: the block shows the first
             * form, one of the game's own */
            form_set(pZukan, 1, shiny, species, 0, 1);
        }
    }
    for (k = 0; k < 4; k++) {
        reset_species(pZukan, ROW_VIEW, k, species);
    }
    set_species(pZukan, ROW_VIEW, (gender == 1 ? 1u : 0u) + (shiny == 1 ? 2u : 0u), species);
}

/* The form shown (the shown bits) of a species with forms, and whether
 * shiny; 0 when no form bit says. */
static int view_form(struct ZukanSave *z, u32 species, u32 *form, u32 *shiny)
{
    u32 n = dex_forms(species), t = table_forms(species), k;

    /* the forms past the game's first: the block's bit is then the form
     * it shows without the mod */
    for (k = t; k < n; k++) {
        if (form_test(z, 1, 0, species, k) || form_test(z, 1, 1, species, k)) {
            *form = k;
            *shiny = form_test(z, 1, 0, species, k) ? 0u : 1u;
            return 1;
        }
    }
    for (k = 0; k < t; k++) {
        if (form_test(z, 1, 0, species, k) || form_test(z, 1, 1, species, k)) {
            *form = k;
            *shiny = form_test(z, 1, 0, species, k) ? 0u : 1u;
            return 1;
        }
    }
    return 0;
}

/* The one-screen interface's question (engine/ui/recipe_dex.c): the form
 * and colour the Pokedex shows for a species, as the game's
 * ZUKAN_SAVE_GetViewGenderAndForm finds them. */
u32 bw_dex_view_form(void *save, u32 species, u32 *shiny)
{
    struct ZukanSave *z = (struct ZukanSave *)save;
    u32 form = 0;

    *shiny = 0;
    if (z == NULL || ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return 0;
    }
    if (dex_forms(species) < 2 || !view_form(z, species, &form, shiny)) {
        form = 0;
        *shiny = test_species(z, ROW_VIEW, 2, species) || test_species(z, ROW_VIEW, 3, species);
    }
    return form;
}

void ZUKAN_SAVE_GetViewGenderAndForm(struct ZukanSave *pZukan, u16 species, u32 *outGender, u32 *outShiny, u32 *outForm,
                                     u16 language)
{
    struct PersonalData *personal;
    u32 n = dex_forms(species), t = table_forms(species), k;

    if (n >= 2) {
        if (!view_form(pZukan, species, outForm, outShiny) && t == 0) {
            /* seen before a mod gave it forms: its first form */
            *outForm = 0;
            *outShiny = test_species(pZukan, ROW_VIEW, 2, species) || test_species(pZukan, ROW_VIEW, 3, species);
        }
        if (test_species(pZukan, ROW_VIEW, 0, species) || test_species(pZukan, ROW_VIEW, 2, species)) {
            *outGender = 0;
        }
        if (test_species(pZukan, ROW_VIEW, 1, species) || test_species(pZukan, ROW_VIEW, 3, species)) {
            *outGender = 1;
        }
    } else {
        *outForm = 0;
        for (k = 0; k < 4; k++) {
            if (test_species(pZukan, ROW_VIEW, k, species)) {
                *outGender = k & 1;
                *outShiny = k >> 1;
            }
        }
    }
    personal = POKE_PERSONAL_Create(species, (u16)*outForm, language);
    if (POKE_PERSONAL_GetParam(personal, 0x14) == 0xFF) {
        *outGender = 2;
    }
    POKE_PERSONAL_Delete(personal);
}

u32 ZUKAN_SAVE_IsSeen(struct ZukanSave *pZukan, u16 species)
{
    u32 row;

    if (ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return 0;
    }
    for (row = 0; row < 4; row++) {
        if (test_species(pZukan, ROW_SEEN, row, species)) {
            return 1;
        }
    }
    return 0;
}

u32 ZUKAN_SAVE_IsCaught(struct ZukanSave *pZukan, u16 species)
{
    if (ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return 0;
    }
    return test_species(pZukan, ROW_CAUGHT, 0, species);
}

/* What RegisterSeen and RegisterCaught share: the member's values, read
 * with its record decoded; 0 when it is an egg. */
static int member_values(struct PokemonParam *pMon, u16 *species, u32 *personality, u32 *form, u32 *language,
                         u32 *gender, u32 *shiny)
{
    u32 saved = POKE_TOOL_DecodePartyParam(pMon);

    if (POKE_TOOL_GetPartyValue(pMon, 0x4C, 0) == 1) {
        POKE_TOOL_EncodePartyParam(pMon, saved);
        return 0;
    }
    *species = (u16)POKE_TOOL_GetPartyValue(pMon, 5, 0);
    *personality = POKE_TOOL_GetPartyValue(pMon, 0, 0);
    *form = POKE_TOOL_GetPartyValue(pMon, 0x6F, 0);
    *language = POKE_TOOL_GetPartyValue(pMon, 0xC, 0);
    *gender = POKE_TOOL_GetGender(pMon);
    *shiny = POKE_TOOL_IsShiny(pMon);
    POKE_TOOL_EncodePartyParam(pMon, saved);
    return 1;
}

/* A species seen before a mod gave it forms gets its form record before
 * this sighting is written: its first form, as the rows have it until now
 * (forms_of). */
static void forms_keep_first(struct ZukanSave *z, u32 species)
{
    if (ZUKAN_SAVE_IsSeen(z, (u16)species) && dex_forms(species) >= 2 && table_forms(species) == 0) {
        (void)forms_of(z, species, 1);
    }
}

void ZUKAN_SAVE_RegisterSeen(struct ZukanSave *pZukan, void *pMon)
{
    u16 species;
    u32 personality, form, language, gender, shiny;

    if (!member_values((struct PokemonParam *)pMon, &species, &personality, &form, &language, &gender, &shiny)
        || ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return;
    }
    forms_keep_first(pZukan, species);
    if (ZUKAN_SAVE_IsSeen(pZukan, species) == 0) {
        ZUKAN_SAVE_UpdateViewFlags(pZukan, species, gender, shiny, form);
    }
    if (species == 0x147 && pZukan->spindaPersonality == 0) {
        pZukan->spindaPersonality = personality;
    }
    ZUKAN_SAVE_SetSeenGenderFlag(pZukan, species, (int)gender, (int)shiny);
    ZUKAN_SAVE_SetSeenFormFlag(pZukan, species, gender, shiny, form);
}

void ZUKAN_SAVE_RegisterCaught(struct ZukanSave *pZukan, void *pMon)
{
    u16 species;
    u32 personality, form, language, gender, shiny;

    if (!member_values((struct PokemonParam *)pMon, &species, &personality, &form, &language, &gender, &shiny)
        || ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return;
    }
    forms_keep_first(pZukan, species);
    if (ZUKAN_SAVE_IsSeen(pZukan, species) == 0) {
        ZUKAN_SAVE_UpdateViewFlags(pZukan, species, gender, shiny, form);
    }
    if (species == 0x147 && pZukan->spindaPersonality == 0) {
        pZukan->spindaPersonality = personality;
    }
    ZUKAN_SAVE_SetSeenGenderFlag(pZukan, species, (int)gender, (int)shiny);
    ZUKAN_SAVE_SetSeenFormFlag(pZukan, species, gender, shiny, form);
    ZUKAN_SAVE_SetLanguageFlag(pZukan, species, language);
    set_species(pZukan, ROW_CAUGHT, 0, species);
}

u32 ZUKAN_SAVE_HasSeenGenderForm(struct ZukanSave *pZukan, u16 species, u32 gender, u32 shiny, u32 form)
{
    if (ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return (u32)-1;
    }
    if (dex_forms(species) >= 2) {
        if (form == 0 && table_forms(species) == 0 && forms_of(pZukan, species, 0) == NULL) {
            /* seen before a mod gave it forms: its first form, as the
             * species' rows have it */
            return test_species(pZukan, ROW_SEEN, shiny == 1 ? 2u : 0u, species)
                   || test_species(pZukan, ROW_SEEN, shiny == 1 ? 3u : 1u, species);
        }
        return form_test(pZukan, 0, shiny, species, form);
    }
    if (gender == 0 || gender == 1) {
        return test_species(pZukan, ROW_SEEN, gender + (shiny == 1 ? 2u : 0u), species);
    }
    /* genderless: either row of the shiny state */
    return test_species(pZukan, ROW_SEEN, shiny == 1 ? 2u : 0u, species)
           || test_species(pZukan, ROW_SEEN, shiny == 1 ? 3u : 1u, species);
}

/* In the regional Pokedex: the regional table (a/0/1/6 member 668 with the
 * mods' rows, sdk/dex_data.c) has a number for it, not 999. */
static int regional(u32 species)
{
    return bw_dex_regional(species) >= 0;
}

u16 ZUKAN_SAVE_CountCaughtRegional(struct ZukanSave *pZukan, u32 heap)
{
    u16 count = 0;
    u32 species;

    (void)heap;

    for (species = 1; species <= bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST); species++) {
        if (ZUKAN_SAVE_IsCaught(pZukan, (u16)species) == 1 && regional(species)) {
            count++;
        }
    }
    return count;
}

u16 ZUKAN_SAVE_CountSeenRegional(struct ZukanSave *pZukan, u32 heap)
{
    u16 count = 0;
    u32 species;

    (void)heap;

    for (species = 1; species <= bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST); species++) {
        if (ZUKAN_SAVE_IsSeen(pZukan, (u16)species) == 1 && regional(species)) {
            count++;
        }
    }
    return count;
}

int ZUKAN_SAVE_CountCaughtRegionalCounted(struct ZukanSave *pZukan, int heap)
{
    u16 count = 0;
    u32 species;

    (void)heap;

    for (species = 1; species <= bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST); species++) {
        if (ZUKAN_SAVE_IsCaught(pZukan, (u16)species) == 1 && regional(species)
            && ZUKAN_SAVE_IsCountedForRegionalCompletion(species) == 1) {
            count++;
        }
    }
    return count;
}

u16 ZUKAN_SAVE_CountSeenRegionalCounted(struct ZukanSave *pZukan, u32 heap)
{
    u16 count = 0;
    u32 species;

    (void)heap;

    for (species = 1; species <= bw_limit_get(BW_PORT_LIMIT_SPECIES_LAST); species++) {
        if (ZUKAN_SAVE_IsSeen(pZukan, (u16)species) == 1 && regional(species)
            && ZUKAN_SAVE_IsCountedForRegionalCompletion(species) == 1) {
            count++;
        }
    }
    return count;
}
