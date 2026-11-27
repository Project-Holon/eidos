/*
 * patches: src/game/ov208/zukan_detail_form.c ZUKAN_DETAIL_FORM_BuildFormLabelText ZUKAN_DETAIL_FORM_CreateTextWindows ZUKAN_DETAIL_FORM_BuildFormEntries
 *
 * symbols:
 *   data_021C8830  0x021C8830  data
 *   data_021C8794  0x021C8794  data
 *   data_021C8810  0x021C8810  data
 *   data_021C87FC  0x021C87FC  data
 *   ZUKAN_DETAIL_COMMON_GetCurrentEntry 0x021BC3C0 thumb ov208
 *   ZUKAN_DETAIL_COMMON_GetSource 0x021BC3A4 thumb ov208
 *   ZUKAN_SAVE_GetFromGameData 0x0200CA1C thumb
 *   ZUKAN_SAVE_GetFormCount 0x0200CC20 thumb
 *   ZUKAN_SAVE_GetViewGenderAndForm 0x0200CC40 thumb
 *   ZUKAN_SAVE_HasSeenGenderForm 0x0200D144 thumb
 *   POKE_PERSONAL_Create 0x0201ADE4 thumb
 *   POKE_PERSONAL_GetParam 0x0201AE1C thumb
 *   POKE_PERSONAL_Delete 0x0201AE14 thumb
 *   ZUKAN_DETAIL_FORM_FlushPrintWindow 0x021C4B60 thumb ov208
 *   ARC_UTIL_UploadPaletteFromArchive 0x02049B90 arm
 *   BMP_WIN_Create 0x020450F0 arm
 *   BMP_WIN_GetBmp 0x02045770 arm
 *   BMP_WIN_TransferChars 0x02045334 arm
 *   BMP_Clear 0x02043B5C arm
 *   MSGDATA_CreateFromArchive 0x02045B38 arm
 *   MSGDATA_AllocString 0x02045EC0 arm
 *   PRINTSYS_PrintQueColor 0x0201C724 thumb
 *   STRBUF_Create 0x020457B0 arm
 *   STRBUF_Delete 0x02045808 arm
 *   WORDSET_CreateDefault 0x0201EC64 thumb
 *   WORDSET_SetSlotSpeciesName 0x0201EE60 thumb
 *   WORDSET_CopyTaggedText 0x0201F250 thumb
 *   WORDSET_Delete 0x0201ED04 thumb
 *
 * The Pokedex form page names the forms mods add (sdk/dex_save.c,
 * sdk/dex_data.c, docs/game-tables.md).
 *
 * The page names form f of a species with the line of bank 244 of a/0/0/2
 * (the form names, one a dex record) at the record f steps along the form
 * chain (a/0/9/8 member 0) from the species.  The game's chain ends at
 * record 726 and has no chain for a species a mod adds, so a form a mod adds
 * had no name.  Here the chain is bw_dex_links's (the game's, with the
 * chains of the species mods add and of a game species whose forms a mod
 * moved to its own stats slots, records from 727), and the walk starts at
 * the species' record (bw_dex_record: the species itself for the game's).
 * Otherwise the two functions are the game's own, line for line.
 *
 * The page's list of what it shows (ZUKAN_DETAIL_FORM_BuildFormEntries)
 * has room for 64 entries: Ov208Form.ents from 0x148 to 0x548, and the 64
 * of each buffer ZUKAN_DETAIL_FORM_HandleKeyEvent passes.  The game writes
 * one entry a form for each colour seen and never more than its forms
 * hold.  A species may have up to 64 forms here (sdk/dex_save.c), each seen
 * normal and shiny, so the list stops at 64 entries: every form is listed
 * when only one colour of each is seen.  The entry of the form the save
 * shows is always listed (the last entry, when it comes after the 64th).
 * The page's FORMS SEEN and SHINY SEEN count every form seen, listed or
 * not.  Otherwise it is the game's own, line for line.
 *
 * The game's own record
 * ---------------------
 *
 * Ov208Form (src/game/ov208/zukan_detail_form_types.h) at its offsets:
 * font 0x004, ents 0x148 (16 bytes each: kind 0x0, mode 0x4, groupKey 0x8),
 * win 0x554, active 0x568, print 0x57C, msg 0x590, marc 0x594, msg2 0x598,
 * str 0x59C.  The offsets are the DS ones: pointers are four bytes in every
 * build that runs the overlays.
 */

#include "dex_data.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;

struct Ov208Form;
struct Ov208Ctx;
struct BmpWin;
struct Bmp;
struct MsgBank;
struct GfFont;
struct Strbuf;
struct WordSet;
struct PrintQueue;

struct Ov208WinTbl {
    u8 d[5][7];
};

extern const struct Ov208WinTbl data_021C8830;
extern const u16 data_021C8794[6];

unsigned short ZUKAN_DETAIL_COMMON_GetCurrentEntry(struct Ov208Ctx *p);
void ZUKAN_DETAIL_FORM_FlushPrintWindow(void *heap, struct Ov208Form *p, struct Ov208Ctx *key, int i);
void ARC_UTIL_UploadPaletteFromArchive(u32 archiveID, u32 memberIdx, u32 kind, u32 offset, u32 dest, u32 size, u16 flags);
struct BmpWin *BMP_WIN_Create(u32 bgLayer, u32 posX, u32 posY, u8 width, u8 height, u8 palette, u8 allocDir);
struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
void BMP_WIN_TransferChars(struct BmpWin *win);
void BMP_Clear(struct Bmp *bmp, u8 value);
struct MsgBank *MSGDATA_CreateFromArchive(u32 resident, u32 arcId, u16 fileId, u32 heapId);
struct Strbuf *MSGDATA_AllocString(struct MsgBank *bank, u32 index);
void PRINTSYS_PrintQueColor(struct PrintQueue *queue, struct Bmp *bmp, u32 x, s16 y, struct Strbuf *str,
                            struct GfFont *font, u16 color);
struct Strbuf *STRBUF_Create(u32 size, u32 heapId);
void STRBUF_Delete(struct Strbuf *strbuf);
struct WordSet *WORDSET_CreateDefault(u32 heapId);
void WORDSET_SetSlotSpeciesName(struct WordSet *words, u32 slot, u16 speciesId);
void WORDSET_CopyTaggedText(struct WordSet *words, struct Strbuf *dst, struct Strbuf *src);
u32 WORDSET_Delete(struct WordSet *words);

/* the form names' bank (the game's: 0xf3 in the Japanese build) */
#define ZUKAN_DETAIL_FORM_MSG_ARCHIVE2 0xf4

#define F(p, type, off)     (*(type *)((char *)(p) + (off)))
#define P_FONT(p)           F(p, struct GfFont *, 0x004)
#define P_ENT_KIND(p, i)    F(p, unsigned int, 0x148 + (i) * 16 + 0x0)
#define P_ENT_MODE(p, i)    F(p, unsigned int, 0x148 + (i) * 16 + 0x4)
#define P_ENT_GROUPKEY(p, i) F(p, unsigned short, 0x148 + (i) * 16 + 0x8)
#define P_WIN(p, i)         (((struct BmpWin **)((char *)(p) + 0x554))[i])
#define P_ACTIVE(p, i)      (((unsigned int *)((char *)(p) + 0x568))[i])
#define P_PRINT(p, i)       (((struct PrintQueue **)((char *)(p) + 0x57C))[i])
#define P_MSG(p)            F(p, struct MsgBank *, 0x590)
#define P_MARC(p)           F(p, void *, 0x594)
#define P_MSG2(p)           F(p, struct MsgBank *, 0x598)
#define P_STR(p, i)         (((struct Strbuf **)((char *)(p) + 0x59C))[i])

void ZUKAN_DETAIL_FORM_BuildFormLabelText(u16 *heap, struct Ov208Form *p, struct Ov208Ctx *key, struct Strbuf **outStr,
                                          struct Strbuf **outB, int *outC, int *outD, u16 idx)
{
    struct Strbuf *str = 0;
    struct Strbuf *b = 0;
    int c = 0;
    int d = 0;
    u16 k;
    struct WordSet *ws;
    struct Strbuf *src;
    u16 n;
    u16 i;

    k = ZUKAN_DETAIL_COMMON_GetCurrentEntry(key);
    switch (P_ENT_KIND(p, idx)) {
    case 0:
        ws = WORDSET_CreateDefault(*heap);
        src = P_STR(p, 2);
        str = STRBUF_Create(0x40, *heap);
        WORDSET_SetSlotSpeciesName(ws, 0, k);
        WORDSET_CopyTaggedText(ws, str, src);
        WORDSET_Delete(ws);
        c = 1;
        break;
    case 1:
        switch (P_ENT_GROUPKEY(p, idx)) {
        case 0:
            str = P_STR(p, 3);
            break;
        case 1:
            str = P_STR(p, 4);
            break;
        }
        break;
    case 2:
        n = P_ENT_GROUPKEY(p, idx);
        k = (u16)bw_dex_record(k);      /* (the game: the species) */
        for (i = 0; i != n; i++) {
            k = ((u16 *)P_MARC(p))[k];
            if (k == 0)
                break;
        }
        str = MSGDATA_AllocString(P_MSG2(p), k);
        c = 1;
        break;
    }
    switch (P_ENT_MODE(p, idx)) {
    case 0:
    case 1:
        break;
    case 2:
        b = P_STR(p, 5);
        d = 0;
        break;
    }
    *outStr = str;
    *outB = b;
    *outC = c;
    *outD = d;
}

void ZUKAN_DETAIL_FORM_CreateTextWindows(u16 *heap, struct Ov208Form *p, struct Ov208Ctx *key)
{
    struct Ov208WinTbl t;
    u32 rows;
    struct Bmp *bmp;
    struct Strbuf *s;
    u8 i;

    t = data_021C8830;
    ARC_UTIL_UploadPaletteFromArchive(0x17, 5, 4, 0, 0, 0x20, *heap);
    ARC_UTIL_UploadPaletteFromArchive(0x17, 5, 0, 0, 0, 0x20, *heap);
    for (i = 0; i < 5; i++) {
        P_WIN(p, i) = BMP_WIN_Create((unsigned int)t.d[i][0], (unsigned int)t.d[i][1], (unsigned int)t.d[i][2], t.d[i][3],
                                     t.d[i][4], t.d[i][5], t.d[i][6]);
        BMP_Clear(BMP_WIN_GetBmp(P_WIN(p, i)), 0);
        BMP_WIN_TransferChars(P_WIN(p, i));
    }
    P_MSG(p) = MSGDATA_CreateFromArchive(0, 2, 0xea, *heap);
    P_MSG2(p) = MSGDATA_CreateFromArchive(0, 2, ZUKAN_DETAIL_FORM_MSG_ARCHIVE2, *heap);
    /* the form chain, with the mods' chains (the game: a/0/9/8 member 0,
     * ARC_UTIL_LoadMemberWithSize(0x62, 0, 0, ...)); freed by the game's
     * ZUKAN_DETAIL_FORM_DeleteTextWindows */
    P_MARC(p) = bw_dex_links(*heap, &rows);
    bmp = BMP_WIN_GetBmp(P_WIN(p, 2));
    s = MSGDATA_AllocString(P_MSG(p), 0xb8);
    PRINTSYS_PrintQueColor(P_PRINT(p, 2), bmp, 0, 1, s, P_FONT(p), 0x3c40);
    STRBUF_Delete(s);
    s = MSGDATA_AllocString(P_MSG(p), 0xb9);
    PRINTSYS_PrintQueColor(P_PRINT(p, 2), bmp, 0, 0x11, s, P_FONT(p), 0x3c40);
    STRBUF_Delete(s);
    P_ACTIVE(p, 2) = 1;
    ZUKAN_DETAIL_FORM_FlushPrintWindow(heap, p, key, 2);
    for (i = 0; i < 6; i++)
        P_STR(p, i) = MSGDATA_AllocString(P_MSG(p), data_021C8794[i]);
}

/* ------------------------------------------------------------------ */
/* The page's entries                                                  */
/* ------------------------------------------------------------------ */

/* the room for entries: Ov208Form.ents (0x148..0x548, 16 bytes each) and
 * ZUKAN_DETAIL_FORM_HandleKeyEvent's bufA and bufB */
#define ZUKAN_DETAIL_FORM_ENTRIES 64

struct PersonalData;

struct Ov208Ent {
    unsigned int kind;
    unsigned int mode;
    unsigned short groupKey;
    unsigned char gender;
    unsigned char shiny;
    unsigned short form;
    unsigned short pade;
};

typedef struct {
    u32 v[3];
} Ov208Gender3;

typedef struct {
    u32 v[2];
} Ov208Shiny2;

extern const Ov208Gender3 data_021C8810;
extern const Ov208Shiny2 data_021C87FC;

void *ZUKAN_DETAIL_COMMON_GetSource(struct Ov208Ctx *p);
void *ZUKAN_SAVE_GetFromGameData(void *gameData);
u16 ZUKAN_SAVE_GetFormCount(u32 species);
void ZUKAN_SAVE_GetViewGenderAndForm(void *pZukan, u16 species, u32 *outGender, u32 *outShiny, u32 *outForm,
                                     u16 language);
u32 ZUKAN_SAVE_HasSeenGenderForm(void *pZukan, u16 species, u32 gender, u32 shiny, u32 form);
struct PersonalData *POKE_PERSONAL_Create(u32 species, int form, u32 heap);
u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field);
void POKE_PERSONAL_Delete(struct PersonalData *data);

void ZUKAN_DETAIL_FORM_BuildFormEntries(u16 *heap, struct Ov208Ctx *key, struct Ov208Ent *out, u16 *count, u16 *groups,
                                        u16 *twos, u16 *sel, u16 *next)
{
    Ov208Gender3 genders = data_021C8810;
    Ov208Shiny2 shinies = data_021C87FC;
    u16 species = ZUKAN_DETAIL_COMMON_GetCurrentEntry(key);
    void *pd = ZUKAN_SAVE_GetFromGameData(ZUKAN_DETAIL_COMMON_GetSource(key));
    struct PersonalData *h = POKE_PERSONAL_Create(species, 0, *heap);
    u32 ratio;
    u32 forms;
    u32 curGender;
    u32 curShiny;
    u32 curForm;
    u16 cur;
    u16 n = 0;
    u8 j;
    u8 k;
    u32 found;
    u32 g;
    u32 s;
    u8 m;
    u16 prev = 0;
    u32 t;
    u32 i;
    u32 allGroups = 0, allTwos = 0;     /* forms seen, forms seen shiny: all of them */

    POKE_PERSONAL_GetParam(h, 0x20);
    ratio = POKE_PERSONAL_GetParam(h, 0x14);
    POKE_PERSONAL_Delete(h);
    forms = ZUKAN_SAVE_GetFormCount(species);
    ZUKAN_SAVE_GetViewGenderAndForm(pd, species, &curGender, &curShiny, &curForm, *heap);
    cur = 0;
    {
        u32 en[3] = { 0, 0, 0 };

        switch (ratio) {
        case 0:
            en[0] = 1;
            break;
        case 0xfe:
            en[1] = 1;
            break;
        case 0xff:
            en[2] = 1;
            break;
        default:
            en[0] = 1;
            en[1] = 1;
            break;
        }
        /* (the game: no bound.  Here, once the room is full, only the
         * entry shown is still written: it takes the last one, so the page
         * keeps the form the save shows) */
        for (i = 0; i < forms; i++) {
            u32 seen[2] = { 0, 0 };

            for (j = 0; j < 3; j++) {
                if (en[j] != 0) {
                    g = genders.v[j];
                    for (k = 0; k < 2; k++) {
                        u16 at = n < ZUKAN_DETAIL_FORM_ENTRIES ? n : ZUKAN_DETAIL_FORM_ENTRIES - 1;

                        u32 mine = curForm == i && curShiny == shinies.v[k] && curGender == genders.v[j];

                        found = 0;
                        s = shinies.v[k];
                        if (mine) {
                            cur = at;
                            found = 1;
                        } else if ((forms < 2 || curForm != i || curShiny != s)
                                   && ZUKAN_SAVE_HasSeenGenderForm(pd, species, g, s, i) != 0) {
                            found = 1;
                        }
                        if (found) {
                            if ((forms >= 2 && seen[k] == 0) || forms < 2) {
                                seen[k] = 1;
                                if (n == ZUKAN_DETAIL_FORM_ENTRIES && !mine) {
                                    continue;   /* (seen, counted below, not listed) */
                                }
                                if (forms >= 2) {
                                    out[at].kind = 2;
                                    out[at].mode = s != 0 ? 2 : 1;
                                    out[at].groupKey = (u16)i;
                                } else {
                                    t = 0;
                                    if (ratio != 0xff)
                                        t = 1;
                                    out[at].kind = t;
                                    out[at].mode = s != 0 ? 2 : 1;
                                    out[at].groupKey = g == 1 ? 1 : 0;
                                }
                                out[at].gender = (u8)g;
                                out[at].shiny = (u8)s;
                                out[at].form = (u16)i;
                                if (n < ZUKAN_DETAIL_FORM_ENTRIES) {
                                    n++;
                                }
                            }
                        }
                    }
                }
            }
            allGroups += seen[0] | seen[1];
            allTwos += seen[1];
        }
        if (forms >= 2 && curForm >= forms) {
            /* the form shown is one past the 64 the page lists (seen
             * first, or chosen on the one-screen device): it takes an
             * entry, the last one when the list is full */
            u16 at = n < ZUKAN_DETAIL_FORM_ENTRIES ? n : ZUKAN_DETAIL_FORM_ENTRIES - 1;

            out[at].kind = 2;
            out[at].mode = curShiny != 0 ? 2 : 1;
            out[at].groupKey = (u16)curForm;
            out[at].gender = (u8)curGender;
            out[at].shiny = (u8)curShiny;
            out[at].form = (u16)curForm;
            cur = at;
            if (n < ZUKAN_DETAIL_FORM_ENTRIES) {
                n++;
            }
            allGroups++;
            allTwos += curShiny != 0;
        }
    }
    *count = n;
    *sel = cur;
    *next = (u16)(n != 0 ? (cur + 1) % n : 0);     /* (the game: no entry cannot happen there) */
    *groups = 0;
    *twos = 0;
    for (m = 0; m < *count; m++) {
        if (out[m].mode == 2)
            (*twos)++;
    }
    for (m = 0; m < *count; m++) {
        if (out[m].kind == 0) {
            *groups = 1;
        } else if (m == 0 || prev != out[m].groupKey) {
            prev = out[m].groupKey;
            (*groups)++;
        }
    }
    if (forms >= 2) {
        /* the page's FORMS SEEN and SHINY SEEN (its only use of these two):
         * every form seen, the ones past the list too (the same counts as
         * the game's when nothing is cut) */
        *groups = (u16)allGroups;
        *twos = (u16)allTwos;
    }
}
