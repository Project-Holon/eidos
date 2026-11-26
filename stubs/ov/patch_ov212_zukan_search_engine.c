/*
 * patches: src/game/ov212/zukan_search_engine.c ZUKAN_SEARCH_ENGINE_Filter ZUKAN_SEARCH_ENGINE_LoadSpeciesIds ZUKAN_SEARCH_ENGINE_LoadFormIds ZUKAN_SEARCH_ENGINE_LoadTables ZUKAN_SEARCH_ENGINE_FinalTest
 *
 * symbols:
 *   GFL_HEAP_Alloc 0x02030734 arm
 *   GFL_HEAP_Free 0x020307B0 arm
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   ARC_UTIL_LoadHandleMemberWithSize 0x0204A39C arm
 *   ZUKAN_SAVE_IsSeen 0x0200D064 thumb
 *   ZUKAN_SAVE_IsCaught 0x0200CED4 thumb
 *   ZUKAN_SAVE_GetViewGenderAndForm 0x0200CC40 thumb
 *   POKE_PERSONAL_Create 0x0201ADE4 thumb
 *   POKE_PERSONAL_Delete 0x0201AE14 thumb
 *   POKE_PERSONAL_GetParam 0x0201AE1C thumb
 *   POKE_PERSONAL_LoadFormTable 0x0201AF80 thumb
 *   MSGDATA_AllocString 0x02045EC0 arm
 *   STRBUF_GetChars 0x02045A90 arm
 *   STRBUF_Delete 0x02045808 arm
 *   gSpeciesNameBank 0x020A7370 data
 *   sZukanSearchEngineSite 0x021C99A8 data ov212
 *
 * The Pokedex's lists and search with the species a mod adds (sdk/bw_limits.c,
 * limit.species; sdk/dex_data.c).
 *
 * The game reads its lists from a/0/9/8: member 4 the species in National
 * order (the Unova list is the ones of it with a Unova number), member 5 in
 * name order, members 6 to 9 the species and their forms tallest, shortest,
 * heaviest and lightest first.  The lists hold the 649 species and the dex's
 * 76 forms, each id packed as a 10-bit species and a 6-bit form.  Member 1
 * is a species' letter group (the first letter of its name), member 2 a
 * dex record's body shape, member 0 the records' form chain (the species'
 * record, then one record a form: 651..726).
 *
 * Here the lists are made when the search opens, from the species there are
 * (sdk/dex_data.c, bw_dex_species_exists) and their records:
 *
 *   National order   the species in id order, each followed by its forms.
 *   Unova            the species with a Unova number, in that number's
 *                    order (the game's: Victini 0 first).
 *   Name order       the game's list, with each added species put before
 *                    the first name that sorts after its own (letters
 *                    compared without case and accents).
 *   Height, weight   a stable sort of the National order by the stats
 *                    record's height or weight (the game's lists are exactly
 *                    this: equal values stay in National order).
 *
 * A species a mod adds is 715 or more, as are its forms' records (a form f
 * at formStatsIndex + f - 1): their dex records are 727 + (id - 715), past
 * the game's 727.  Their shape comes from eidos/tables/pokedex.txt, their
 * letter group from there or from the name's first letter.
 *
 * The search's own list of ids keeps one u32 an entry (the species in the low
 * half, the form in the high), so no species limit or form count is too
 * large for it.  The rest of the search is the game's own, line for line.
 *
 * With no species added the lists come out as the game's.
 *
 * The game's own records
 * ----------------------
 *
 * Search and Query (src/game/ov212/zukan_search_engine.c) are copied whole:
 * the same offsets on any target where pointers are four bytes (the DS's,
 * i686's and wasm32's).
 */

#include "dex_data.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct ZukanSave;
struct PersonalData;
struct MsgBank;
struct Strbuf;

struct Query { u8 mode; u8 rule; u8 kind; u8 type1; u8 type2; u8 attr; u8 last; };

struct Search {
    void *save; struct Query *query; u16 state; u16 padA; void *arc;
    u16 index; u16 count; u16 *ids; u8 *reject;
    u16 count1; u16 pad1; u16 *table1; u16 count2; u16 pad2; u8 *table2;
    u16 count3; u16 pad3; u8 *table3; u16 count4; u16 pad4; u16 *table4;
    void *personal; u16 resultCount; u16 pad42; u16 *result;
};

typedef char search_is_0x48[sizeof(struct Search) == 0x48 || sizeof(void *) != 4 ? 1 : -1];

void *GFL_HEAP_Alloc(u32 heapId, u32 size, int clear, const char *site, u16 tag);
u32 GFL_HEAP_Free(void *block);
void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
void *ARC_UTIL_LoadHandleMemberWithSize(void *narc, int fileID, int decompress, u32 heapID, u32 *out_size);
u32 ZUKAN_SAVE_IsSeen(struct ZukanSave *pZukan, u16 species);
u32 ZUKAN_SAVE_IsCaught(struct ZukanSave *pZukan, u16 species);
void ZUKAN_SAVE_GetViewGenderAndForm(struct ZukanSave *pZukan, u16 species, u32 *outGender, u32 *outShiny, u32 *outForm,
                                     u16 language);
struct PersonalData *POKE_PERSONAL_Create(u32 species, int form, u32 heap);
void POKE_PERSONAL_Delete(struct PersonalData *data);
u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field);
u16 *POKE_PERSONAL_LoadFormTable(u32 heap, u16 *countOut);
struct Strbuf *MSGDATA_AllocString(struct MsgBank *bank, u32 index);
u16 *STRBUF_GetChars(struct Strbuf *strbuf);
void STRBUF_Delete(struct Strbuf *strbuf);
u32 bw_limit_get(int id);

extern struct MsgBank *gSpeciesNameBank;
extern const char sZukanSearchEngineSite[];

#define LIMIT_SPECIES_LAST 4    /* sdk/bw_limits.h, BW_PORT_LIMIT_SPECIES_LAST */

#define PARAM_TYPE1        6
#define PARAM_TYPE2        7
#define PARAM_COLOR        33
#define PARAM_HEIGHT       37
#define PARAM_WEIGHT       38

#define FIRST_MOD_ID   715u
#define NO_VALUE       255u     /* a letter or shape no search choice is */

#define ENTRY(species, form)  ((u32)(species) | (u32)(form) << 16)
#define E_SPECIES(v)          ((v) & 0xFFFFu)
#define E_FORM(v)             ((v) >> 16)

static u32 species_last(void)
{
    return bw_limit_get(LIMIT_SPECIES_LAST);
}

/* ------------------------------------------------------------------ */
/* The lists                                                           */
/* ------------------------------------------------------------------ */

/* The species and forms in National order: each species, then its forms
 * in the form chain (sdk/dex_data.c, bw_dex_links).  `withForms` 0: the
 * species alone.  The block is on `heap`; *n the entries. */
static u32 *national(u32 heap, int withForms, u32 *n)
{
    u32 last = species_last(), s, used = 0, cap = last + 1u, rows = 0;
    u16 *links = 0;
    u32 *out;

    if (withForms) {
        links = bw_dex_links(heap, &rows);
        cap += rows;
    }
    out = (u32 *)GFL_HEAP_Alloc(heap, cap * 4u, 0, sZukanSearchEngineSite, 1001);
    for (s = 1; s <= last; s++) {
        u32 r, f = 0;

        if (!bw_dex_species_exists(s)) {
            continue;
        }
        out[used++] = ENTRY(s, 0);
        for (r = bw_dex_record(s); withForms && r < rows && links[r] != 0 && used < cap;) {
            r = links[r];
            out[used++] = ENTRY(s, ++f);
        }
    }
    if (links != 0) {
        GFL_HEAP_Free(links);
    }
    *n = used;
    return out;
}

/* A stable sort of entries by a stats record value (height, weight). */
static void sort_by_param(u32 *v, u32 n, u32 param, int largestFirst, u32 heap)
{
    u16 *key = (u16 *)GFL_HEAP_Alloc(heap, (n > 0 ? n : 1u) * 2u, 0, sZukanSearchEngineSite, 1002);
    u32 i, j;

    for (i = 0; i < n; i++) {
        struct PersonalData *pd = POKE_PERSONAL_Create(E_SPECIES(v[i]), (int)E_FORM(v[i]), heap);

        key[i] = (u16)POKE_PERSONAL_GetParam(pd, param);
        POKE_PERSONAL_Delete(pd);
    }
    for (i = 1; i < n; i++) {        /* (insertion: stable, and the lists are short) */
        u32 e = v[i];
        u16 k = key[i];

        for (j = i; j > 0 && (largestFirst ? key[j - 1] < k : key[j - 1] > k); j--) {
            v[j] = v[j - 1];
            key[j] = key[j - 1];
        }
        v[j] = e;
        key[j] = k;
    }
    GFL_HEAP_Free(key);
}

/* A letter for comparing names: case and the Latin accents set aside. */
static u32 fold(u16 c)
{
    /* U+00C0..U+00FF: the letter each accented one is, '*' for the others */
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUY*saaaaaaaceeeeiiiidnooooo*ouuuuy*y";

    if (c >= 'a' && c <= 'z') {
        return (u32)(c - 'a' + 'A');
    }
    if (c >= 0xC0 && c <= 0xFF && latin1[c - 0xC0] >= 'A') {
        char l = latin1[c - 0xC0];

        return (u32)(l >= 'a' ? l - 'a' + 'A' : l);
    }
    return c;
}

/* A species' name compared with another's: <0, 0, >0. */
static int name_cmp(const u16 *a, const u16 *b)
{
    for (;; a++, b++) {
        u32 x = *a == 0xFFFF ? 0 : fold(*a), y = *b == 0xFFFF ? 0 : fold(*b);

        if (x != y || x == 0) {
            return (int)x - (int)y;
        }
    }
}

/* The letter group of a name (A 0 .. Z 25), or NO_VALUE. */
static u32 name_letter(const u16 *name)
{
    u32 c = name[0] == 0xFFFF ? 0 : fold(name[0]);

    return c >= 'A' && c <= 'Z' ? c - 'A' : NO_VALUE;
}

/* The game's name-order list with the added species put in. */
static u32 *by_name(void *arc, u32 heap, u32 *n)
{
    u32 bytes, game, i, s, used, last = species_last();
    u16 *ids = (u16 *)ARC_UTIL_LoadHandleMemberWithSize(arc, 5, 0, heap, &bytes);
    u32 *out;

    game = bytes / 2u;
    out = (u32 *)GFL_HEAP_Alloc(heap, (game + (last >= FIRST_MOD_ID ? last - FIRST_MOD_ID + 1u : 0u) + 1u) * 4u, 0,
                                sZukanSearchEngineSite, 1003);
    for (i = 0; i < game; i++) {
        out[i] = ENTRY(ids[i] & 1023u, ids[i] >> 10);
    }
    used = game;
    GFL_HEAP_Free(ids);
    for (s = FIRST_MOD_ID; s <= last; s++) {
        struct Strbuf *mine;
        u32 at;

        if (!bw_dex_species_exists(s)) {
            continue;
        }
        mine = MSGDATA_AllocString(gSpeciesNameBank, s);
        for (at = 0; at < used; at++) {
            struct Strbuf *other = MSGDATA_AllocString(gSpeciesNameBank, E_SPECIES(out[at]));
            int c = name_cmp(STRBUF_GetChars(other), STRBUF_GetChars(mine));

            STRBUF_Delete(other);
            if (c > 0) {
                break;
            }
        }
        STRBUF_Delete(mine);
        for (i = used; i > at; i--) {
            out[i] = out[i - 1];
        }
        out[at] = ENTRY(s, 0);
        used++;
    }
    *n = used;
    return out;
}

u32 ZUKAN_SEARCH_ENGINE_LoadSpeciesIds(u32 mode, u16 heap, u16 **out)
{
    u32 n, i, j, used = 0;
    u32 *all = national(heap, 0, &n);
    u16 *result = (u16 *)GFL_HEAP_Alloc(heap, (n > 0 ? n : 1u) * 2u, 0, sZukanSearchEngineSite, 825);

    if (mode == 0) {
        for (i = 0; i < n; i++) {
            result[used++] = (u16)E_SPECIES(all[i]);
        }
    } else {
        /* the Unova list: by Unova number (each number once in the game's
         * table; equal numbers keep National order) */
        for (i = 0; i < n; i++) {
            int r = bw_dex_regional(E_SPECIES(all[i]));

            if (r < 0) {
                continue;
            }
            for (j = used; j > 0 && bw_dex_regional(result[j - 1]) > r; j--) {
                result[j] = result[j - 1];
            }
            result[j] = (u16)E_SPECIES(all[i]);
            used++;
        }
    }
    GFL_HEAP_Free(all);
    *out = result;
    return used;
}

/* The list of a search's order (the game's g_021C9968: rule 0 member 4,
 * 1 member 5, 2 member 8, 3 member 9, 4 member 6, 5 member 7).  The ids
 * are u32 here (Filter reads them so). */
unsigned int ZUKAN_SEARCH_ENGINE_LoadFormIds(void *archive, int mode, int rule, int heapId, u16 **out)
{
    u32 n = 0, *v;

    (void)mode;
    switch (rule) {
    case 1:
        v = by_name(archive, (u32)heapId, &n);
        break;
    case 2:
    case 3:
        v = national((u32)heapId, 1, &n);
        sort_by_param(v, n, PARAM_WEIGHT, rule == 2, (u32)heapId);
        break;
    case 4:
    case 5:
        v = national((u32)heapId, 1, &n);
        sort_by_param(v, n, PARAM_HEIGHT, rule == 4, (u32)heapId);
        break;
    default:
        v = national((u32)heapId, 0, &n);
        break;
    }
    *out = (u16 *)v;
    return n;
}

/* ------------------------------------------------------------------ */
/* The search's tables                                                 */
/* ------------------------------------------------------------------ */

void ZUKAN_SEARCH_ENGINE_LoadTables(void *archive, u32 heapId, u16 *count1, void **table1,
                                    u16 *count2, void **table2, u16 *count3, void **table3, u16 *count4, void **table4)
{
    u32 bytes2, bytes3, rows2, rows34, s, i, last = species_last();
    u8 *letters, *shapes, *gameLetters, *gameShapes;
    u16 *links;

    *table1 = POKE_PERSONAL_LoadFormTable(heapId, 0);
    *count1 = (u16)(last + 1u);

    gameLetters = (u8 *)ARC_UTIL_LoadHandleMemberWithSize(archive, 1, 0, heapId, &bytes2);
    gameShapes = (u8 *)ARC_UTIL_LoadHandleMemberWithSize(archive, 2, 0, heapId, &bytes3);
    links = bw_dex_links(heapId, &rows34);      /* (member 0 with the added species' chains) */

    /* the rows: every species up to the limit, every record of theirs */
    rows2 = last + 1u > bytes2 ? last + 1u : bytes2;
    if (bytes3 > rows34) {
        rows34 = bytes3;
    }
    letters = (u8 *)GFL_HEAP_Alloc(heapId, rows2, 0, sZukanSearchEngineSite, 1004);
    shapes = (u8 *)GFL_HEAP_Alloc(heapId, rows34, 0, sZukanSearchEngineSite, 1005);
    for (i = 0; i < rows2; i++) {
        letters[i] = i < bytes2 ? gameLetters[i] : (u8)NO_VALUE;
    }
    for (i = 0; i < rows34; i++) {
        shapes[i] = i < bytes3 ? gameShapes[i] : (u8)NO_VALUE;
    }
    GFL_HEAP_Free(gameLetters);
    GFL_HEAP_Free(gameShapes);

    /* the mods' values (eidos/tables/pokedex.txt) over the game's; an added
     * species without one: its name's letter, no shape, a form its
     * species' shape */
    for (s = 1; s <= last; s++) {
        u32 r, f, base;
        int v;

        if (!bw_dex_species_exists(s)) {
            continue;
        }
        v = bw_dex_letter(s);
        if (v < 0 && s >= FIRST_MOD_ID) {
            struct Strbuf *name = MSGDATA_AllocString(gSpeciesNameBank, s);

            v = (int)name_letter(STRBUF_GetChars(name));
            STRBUF_Delete(name);
        }
        if (v >= 0) {
            letters[s] = (u8)v;
        }
        base = r = bw_dex_record(s);
        for (f = 0; r < rows34; f++) {
            v = bw_dex_shape(s, f);
            if (v >= 0) {
                shapes[r] = (u8)v;
            } else if (s >= FIRST_MOD_ID && f > 0) {
                shapes[r] = shapes[base];
            }
            if (links[r] == 0) {
                break;
            }
            r = links[r];
        }
    }
    *table2 = letters;
    *count2 = (u16)rows2;
    *table3 = shapes;
    *count3 = (u16)rows34;
    *table4 = links;
    *count4 = (u16)rows34;
}

/* The shape of a species' form: the form chain from the species' record. */
u8 ZUKAN_SEARCH_ENGINE_FinalTest(const u8 *t, const u16 *u, u32 s, u32 f)
{
    u16 n;

    s = bw_dex_record(s);
    n = 0;
    if (f != 0) {
        do {
            s = u[s];
            if (s == 0) {
                break;
            }
            n++;
        } while (n != f);
    }
    return t[s];
}

/* ------------------------------------------------------------------ */
/* The search                                                          */
/* ------------------------------------------------------------------ */

int ZUKAN_SEARCH_ENGINE_Filter(struct Search *p, u16 heap, u16 *outCount, u16 **out)
{
    u32 unused1, unused2, outForm;
    u16 end, i, j, used, species, form;
    u16 count;
    u16 *result;
    u8 type1, type2, id;
    u16 of, n;
    const u32 *ids;

    switch (p->state) {
    case 0:
        p->arc = ARC_TOOL_CreateHandle(98, heap);
        p->state = 1;
        break;
    case 1:
        p->count = (u16)ZUKAN_SEARCH_ENGINE_LoadFormIds(p->arc, p->query->mode, p->query->rule, heap, &p->ids);
        p->reject = (u8 *)GFL_HEAP_Alloc(heap, p->count > 0 ? p->count : 1u, 1, sZukanSearchEngineSite, 551);
        p->state = 2;
        break;
    case 2:
        ZUKAN_SEARCH_ENGINE_LoadTables(p->arc, heap, &p->count1, (void **)&p->table1, &p->count2, (void **)&p->table2,
                                       &p->count3, (void **)&p->table3, &p->count4, (void **)&p->table4);
        p->state = 3;
        break;
    case 3:
        ids = (const u32 *)p->ids;
        n = p->count;
        i = p->index;
        if (i >= n) {
            p->state = 4;
            break;
        }
        end = i + 7;
        if (end > n) {
            end = n;
        }
        p->index = end;
        for (; i < end; i++) {
            species = (u16)E_SPECIES(ids[i]);
            form = (u16)E_FORM(ids[i]);
            if (!ZUKAN_SAVE_IsSeen(p->save, species)) {
                p->reject[i] = 1;
                continue;
            }
            ZUKAN_SAVE_GetViewGenderAndForm(p->save, species, &unused1, &unused2, &outForm, heap);
            of = (u16)outForm;
            /* (the game: `count == SPECIES_LAST`, the lists of species
             * alone, rules 0 and 1, take the form the dex shows) */
            if (p->query->rule < 2) {
                form = of;
            }
            if (form != of) {
                p->reject[i] = 1;
                continue;
            }
            if ((p->query->rule >= 2 && p->query->rule <= 5) || p->query->type1 != 255 || p->query->type2 != 255) {
                if (!ZUKAN_SAVE_IsCaught(p->save, species)) {
                    p->reject[i] = 1;
                    continue;
                }
            }
            if (p->query->mode == 1 && p->table1[species] == 999) {
                p->reject[i] = 1;
                continue;
            }
            if (p->query->kind != 255 && p->query->kind != p->table2[species]) {
                p->reject[i] = 1;
                continue;
            }
            if (p->personal) {
                POKE_PERSONAL_Delete(p->personal);
            }
            p->personal = POKE_PERSONAL_Create(species, form, heap);
            type1 = (u8)POKE_PERSONAL_GetParam(p->personal, PARAM_TYPE1);
            type2 = (u8)POKE_PERSONAL_GetParam(p->personal, PARAM_TYPE2);
            if (p->query->type1 != 255 && p->query->type2 != 255) {
                if ((type1 == p->query->type1 && type2 == p->query->type2)
                    || (type1 == p->query->type2 && type2 == p->query->type1)) {
                } else {
                    p->reject[i] = 1;
                    continue;
                }
            } else if (p->query->type1 != 255 && p->query->type2 == 255) {
                if (type1 != p->query->type1 && type2 != p->query->type1) {
                    p->reject[i] = 1;
                    continue;
                }
            } else if (p->query->type1 == 255 && p->query->type2 != 255) {
                if (type1 != p->query->type2 && type2 != p->query->type2) {
                    p->reject[i] = 1;
                    continue;
                }
            }
            if (p->query->attr != 255) {
                id = (u8)POKE_PERSONAL_GetParam(p->personal, PARAM_COLOR);
                if (id != p->query->attr) {
                    p->reject[i] = 1;
                    continue;
                }
            }
            if (p->query->last != 255) {
                u8 fr = ZUKAN_SEARCH_ENGINE_FinalTest(p->table3, p->table4, species, form);

                if (fr != p->query->last) {
                    p->reject[i] = 1;
                    continue;
                }
            }
        }
        break;
    case 4:
        if (p->personal) {
            POKE_PERSONAL_Delete(p->personal);
        }
        ids = (const u32 *)p->ids;
        count = 0;
        for (j = 0; j < p->count; j++) {
            if (p->reject[j] == 0) {
                count++;
            }
        }
        if (count) {
            used = 0;
            result = (u16 *)GFL_HEAP_Alloc(heap, count * 2u, 0, sZukanSearchEngineSite, 742);
            for (j = 0; j < p->count; j++) {
                if (p->reject[j] == 0) {
                    result[used] = (u16)E_SPECIES(ids[j]);
                    used++;
                }
            }
        } else {
            result = 0;
        }
        GFL_HEAP_Free(p->ids);
        GFL_HEAP_Free(p->reject);
        GFL_HEAP_Free(p->table1);
        GFL_HEAP_Free(p->table2);
        GFL_HEAP_Free(p->table3);
        GFL_HEAP_Free(p->table4);
        p->resultCount = count;
        p->result = result;
        ARC_TOOL_DeleteHandle(p->arc);
        p->state = 5;
        break;
    case 5:
    default:
        *out = p->result;
        *outCount = p->resultCount;
        return 1;
    }
    return 0;
}
