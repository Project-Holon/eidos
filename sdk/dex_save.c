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
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bw_limits.h"
#include "dex_data.h"
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
extern u16 ZUKAN_SAVE_GetFormCount(u32 species) GAME_FN;
extern void ZUKAN_SAVE_SetSeenFormFlag(struct ZukanSave *pZukan, u32 species, u32 sel, u32 shiny, u32 form) GAME_FN;

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

/* A file: its namespace, its bits once read. */
typedef struct DexFile {
    char ns[80];
    int loaded;
    int onDisk;                         /* the save has this file */
    DexExt ext;
} DexFile;

/* The files, on the heap at the first use (the 32-bit image has little
 * room, cmake/check_image.cmake); eidos's own alone if that fails. */
static DexFile g_file0;
static DexFile *g_files = &g_file0;
static int g_cap = 1;
static int g_nfiles;
static u8 g_owner[DEX_SPECIES];         /* species -> file + 1; 0 not worked out */
static int g_owners_done;

/* The per-save layer's calls for one of the port's namespaces; the
 * caller's namespace (a mod's, when a mod's code is running) comes back
 * after. */
static int ext_io(DexFile *f, int store)
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

    if (n > 672 && n < (int)DEX_SPECIES) {
        g_owner[n] = (u8)(((OwnerWalk *)ctx)->file + 1);
    }
    return 0;
}

/* Which file each species past 672 belongs to: the last mod (in load
 * order) that ships its stats record, else eidos's own. */
static void owners(void)
{
    u32 m;

    if (g_owners_done) {
        return;
    }
    g_owners_done = 1;
    memset(g_owner, 0, sizeof g_owner);
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
    f->onDisk = ext_io(f, 0) == (int)sizeof f->ext && f->ext.magic == DEX_MAGIC;
    if (!f->onDisk || g_new_game) {
        memset(&f->ext, 0, sizeof f->ext);
        f->ext.magic = DEX_MAGIC;
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
    int i;

    for (i = 0; i < g_nfiles; i++) {
        DexFile *f = &g_files[i];

        if (f->loaded && (f->onDisk || !ext_empty(&f->ext))) {
            if (ext_io(f, 1) == 0) {
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
    bw_dex_ext_forget();
}

/* The save in force or the mod list changed: read again when next asked. */
void bw_dex_ext_forget(void)
{
    int i;

    for (i = 0; i < g_nfiles; i++) {
        g_files[i].loaded = 0;
    }
    g_owners_done = 0;
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

static void form_bit(u8 *flags, u32 bit, int on)
{
    if (on) {
        flags[bit >> 3] |= (u8)(1 << (bit & 7));
    } else {
        flags[bit >> 3] &= (u8)~(1 << (bit & 7));
    }
}

void ZUKAN_SAVE_UpdateViewFlags(struct ZukanSave *pZukan, u16 species, u32 gender, u32 shiny, u32 form)
{
    u32 k;

    if (ZUKAN_SAVE_GetFormTableIndex(species) != (u32)-1) {
        u32 base = ZUKAN_SAVE_GetFormBitBase(species), count = ZUKAN_SAVE_GetFormCount(species);

        for (k = 0; k < count; k++) {
            form_bit(pZukan->viewFormFlags[0], base + k, 0);
            form_bit(pZukan->viewFormFlags[1], base + k, 0);
        }
        if (form >= count) {
            form = 0;
        }
        form_bit(pZukan->viewFormFlags[shiny == 1 ? 1 : 0], base + form, 1);
    }
    for (k = 0; k < 4; k++) {
        reset_species(pZukan, ROW_VIEW, k, species);
    }
    set_species(pZukan, ROW_VIEW, (gender == 1 ? 1u : 0u) + (shiny == 1 ? 2u : 0u), species);
}

void ZUKAN_SAVE_GetViewGenderAndForm(struct ZukanSave *pZukan, u16 species, u32 *outGender, u32 *outShiny, u32 *outForm,
                                     u16 language)
{
    struct PersonalData *personal;
    u32 k;

    if (ZUKAN_SAVE_GetFormTableIndex(species) != (u32)-1) {
        u32 base = ZUKAN_SAVE_GetFormBitBase(species), count = ZUKAN_SAVE_GetFormCount(species);

        for (k = 0; k < count; k++) {
            u32 bit = base + k;

            if (pZukan->viewFormFlags[0][bit >> 3] & (1 << (bit & 7))) {
                *outForm = k;
                *outShiny = 0;
                break;
            }
            if (pZukan->viewFormFlags[1][bit >> 3] & (1 << (bit & 7))) {
                *outForm = k;
                *outShiny = 1;
                break;
            }
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

void ZUKAN_SAVE_RegisterSeen(struct ZukanSave *pZukan, void *pMon)
{
    u16 species;
    u32 personality, form, language, gender, shiny;

    if (!member_values((struct PokemonParam *)pMon, &species, &personality, &form, &language, &gender, &shiny)
        || ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return;
    }
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
    u32 base;

    if (ZUKAN_SAVE_IsInvalidSpecies(species) != 0) {
        return (u32)-1;
    }
    base = ZUKAN_SAVE_GetFormBitBase(species);
    if (base != (u32)-1) {
        u32 bit = (u16)(base + form);

        return (pZukan->seenFormFlags[shiny][bit >> 3] >> (bit & 7)) & 1u;
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
