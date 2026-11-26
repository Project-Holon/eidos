/*
 * patches: src/game/ov206/zukan_info.c ZUKAN_INFO_CreateWindows ZUKAN_INFO_DrawMainPage ZUKAN_INFO_DrawPage
 *
 * symbols:
 *   ZUKAN_INFO_DrawMessage 0x021BAA08 thumb ov206
 *   ZUKAN_INFO_DrawText 0x021BA960 thumb ov206
 *   ZUKAN_INFO_FlushSlotWindows 0x021BAF7C thumb ov206
 *   d_021BC110 0x021BC110 data ov206
 *   d_021BC11C 0x021BC11C data ov206
 *   d_021BC174 0x021BC174 data ov206
 *   ARC_UTIL_UploadPaletteFromHandleWhole 0x02049B68 arm
 *   BMP_WIN_Create 0x020450F0 arm
 *   BMP_WIN_GetBmp 0x02045770 arm
 *   BMP_Clear 0x02043B5C arm
 *   POKE_PERSONAL_LoadFormTable 0x0201AF80 thumb
 *   GFL_HEAP_Free 0x020307B0 arm
 *   WORDSET_CreateDefault 0x0201EC64 thumb
 *   WORDSET_SetSlotNumber 0x0201EFA4 thumb
 *   WORDSET_Delete 0x0201ED04 thumb
 *   MSGDATA_AllocString 0x02045EC0 arm
 *   PRINTSYS_PrintQueColor 0x0201C724 thumb
 *   STRBUF_Delete 0x02045808 arm
 *   gSpeciesNameBank 0x020A7370 data
 *
 * The Pokedex page of a species a mod adds (sdk/bw_limits.c, limit.species;
 * sdk/dex_data.c).  The page's height and weight are text lines (banks 244,
 * 245 and 268 of a/0/0/2) of the species' dex record: the species, or the
 * record its form chain (a/0/9/8 member 0) gives a form.  The game's records
 * end at 726, so a species from 715 and each of its forms have the records
 * from 727 (bw_dex_record): the page starts from the species' record, and
 * its form chain has the added species' chains (bw_dex_links).  For the
 * game's species the record is the species, and the three functions are the
 * game's own, line for line.
 *
 * The game's own record
 * ---------------------
 *
 * ZukanInfo (src/game/ov206/zukan_info.c) at its offsets: heap 0x000,
 * formTableNeeded 0x004, showCurrent 0x008, species 0x00C, form 0x00E,
 * mode 0x018, femaleFlag 0x01C, font 0x028, printQue 0x02C, layerMain
 * 0x04C, win 0x06C, slotDirty 0x07C, w 0x1C4, objs 0x1E0, text.input 0x270,
 * speciesTable 0x2B0.  The offsets are the DS ones: pointers are four bytes
 * in every build that runs the overlays.
 */

#include "dex_data.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;

struct BmpWin;
struct Bmp;
struct MsgBank;
struct GfFont;
struct Strbuf;
struct WordSet;
struct PrintQueue;

struct Tab16 { u8 b[16]; };

extern const struct Tab16 d_021BC174;
extern const u16 d_021BC110[];
extern const u16 d_021BC11C[];
extern struct MsgBank *gSpeciesNameBank;
extern int g_pm_language;

void ZUKAN_INFO_DrawMessage(int heap, struct BmpWin *window, struct MsgBank *bank, void *printer, struct GfFont *font,
                            int message, unsigned short x, unsigned short y, unsigned short color, int alignment,
                            struct WordSet *expansion);
void ZUKAN_INFO_DrawText(int heap, struct BmpWin *window, void *printer, struct GfFont *font, struct Strbuf *input,
                         unsigned short right, unsigned short y, unsigned short color, int alignment,
                         struct WordSet *expansion);
void ZUKAN_INFO_FlushSlotWindows(char *p);
void ARC_UTIL_UploadPaletteFromHandleWhole(void *arc, u32 memberIdx, int kind, u32 dest, int size, u16 flags);
struct BmpWin *BMP_WIN_Create(u32 bgLayer, u32 posX, u32 posY, u8 width, u8 height, u8 palette, u8 allocDir);
struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
void BMP_Clear(struct Bmp *bmp, u8 value);
u16 *POKE_PERSONAL_LoadFormTable(u32 heap, u16 *countOut);
u32 GFL_HEAP_Free(void *block);
struct WordSet *WORDSET_CreateDefault(u32 heapId);
void WORDSET_SetSlotNumber(struct WordSet *words, u32 slot, u32 number, u32 topDigitIndex, u32 padMode, u32 isHalfWidth);
u32 WORDSET_Delete(struct WordSet *words);
struct Strbuf *MSGDATA_AllocString(struct MsgBank *bank, u32 index);
void PRINTSYS_PrintQueColor(struct PrintQueue *queue, struct Bmp *bmp, u32 x, s16 y, struct Strbuf *str,
                            struct GfFont *font, u16 color);
void STRBUF_Delete(struct Strbuf *strbuf);

/* version.h: the European cartridges' language at run time */
#define LANGUAGE_FRENCH  3
#define LANGUAGE_ITALIAN 4
#define LANGUAGE_GERMAN  5
#define LANGUAGE_SPANISH 7
#define PM_LANG_EU(english, french, german, italian, spanish)                                                   \
    (g_pm_language == LANGUAGE_FRENCH ? (french) : g_pm_language == LANGUAGE_GERMAN ? (german)                \
     : g_pm_language == LANGUAGE_ITALIAN ? (italian) : g_pm_language == LANGUAGE_SPANISH ? (spanish) : (english))
#define PM_LANG_HALF_WIDTH_DIGITS 1
#define ZUKAN_INFO_HEIGHT_VALUE_X PM_LANG_EU(0x65, 0x5e, 0x5e, 0x5e, 0x5e)
#define ZUKAN_INFO_WEIGHT_VALUE_X PM_LANG_EU(0x66, 0x64, 0x64, 0x64, 0x64)

#define F(p, type, off)    (*(type *)((char *)(p) + (off)))
#define I_HEAP(p)          F(p, u16, 0x000)
#define I_FORMTABLENEEDED(p) F(p, int, 0x004)
#define I_SHOWCURRENT(p)   F(p, int, 0x008)
#define I_SPECIES(p)       F(p, u16, 0x00C)
#define I_FORM(p)          F(p, u16, 0x00E)
#define I_MODE(p)          F(p, int, 0x018)
#define I_FEMALEFLAG(p)    F(p, int, 0x01C)
#define I_FONT(p)          F(p, struct GfFont *, 0x028)
#define I_PRINTQUE(p)      F(p, void *, 0x02C)
#define I_LAYERMAIN(p)     F(p, u8, 0x04C)
#define I_WIN(p, i)        (((struct BmpWin **)((char *)(p) + 0x06C))[i])
#define I_SLOTDIRTY(p, i)  (((int *)((char *)(p) + 0x07C))[i])
#define I_W(p, i)          (((void **)((char *)(p) + 0x1C4))[i])
#define I_OBJS(p, i)       (((struct MsgBank **)((char *)(p) + 0x1E0))[i])
#define I_INPUT(p, i)      (((struct Strbuf **)((char *)(p) + 0x270))[i])
#define I_SPECIESTABLE(p)  F(p, u16 *, 0x2B0)

void ZUKAN_INFO_CreateWindows(char *p)
{
    struct Tab16 tab;
    u32 rows;
    int r6;
    int i;

    /* the form chain, with the added species' (the game: a/0/9/8 member 0 through its handle w[1]) */
    I_SPECIESTABLE(p) = bw_dex_links(I_HEAP(p), &rows);
    r6 = 0;
    if (I_FEMALEFLAG(p) != 0) {
        r6 = 4;
    }
    ARC_UTIL_UploadPaletteFromHandleWhole(I_W(p, 2), 5, r6, 0x60, 0x20, I_HEAP(p));
    tab = d_021BC174;
    for (i = 0; i < 4; i++) {
        I_WIN(p, i) = BMP_WIN_Create(I_LAYERMAIN(p), tab.b[i * 4], tab.b[i * 4 + 1], tab.b[i * 4 + 2], tab.b[i * 4 + 3], 3, 1);
        BMP_Clear(BMP_WIN_GetBmp(I_WIN(p, i)), 0);
    }
    for (i = 0; i < 4; i++) {
        I_SLOTDIRTY(p, i) = 1;
    }
}

void ZUKAN_INFO_DrawMainPage(char *p)
{
    struct MsgBank *l2c = I_OBJS(p, 0);
    struct MsgBank *l28 = I_OBJS(p, 1);
    struct MsgBank *l24 = I_OBJS(p, 2);
    struct MsgBank *l20 = I_OBJS(p, 3);
    struct MsgBank *l1c = I_OBJS(p, 4);
    u16 t = (u16)bw_dex_record(I_SPECIES(p));       /* (the game: the species) */
    u16 i = 0;
    struct WordSet *h2;
    u16 r7;
    u16 r6;
    u16 m;
    struct Strbuf *name;
    u8 k;

    if (I_FORM(p) != 0) {
        u16 *arr = I_SPECIESTABLE(p);

        do {
            t = arr[t];
            if (t == 0) {
                break;
            }
            i = (u16)(i + 1);
        } while (i != I_FORM(p));
    }
    if (t == 0) {
        t = (u16)bw_dex_record(I_SPECIES(p));
    }
    if (I_MODE(p) != 2) {
        ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 0), l2c, I_PRINTQUE(p), I_FONT(p), 0x90, 0, 5, 0x3c40, 2, 0);
    }
    r7 = I_SPECIES(p);
    if (I_FORMTABLENEEDED(p) == 0) {
        u16 *tab = POKE_PERSONAL_LoadFormTable(I_HEAP(p), 0);

        r7 = tab[I_SPECIES(p)];
        GFL_HEAP_Free(tab);
    }
    if (r7 != 0x3e7) {
        h2 = WORDSET_CreateDefault(I_HEAP(p));
        WORDSET_SetSlotNumber(h2, 0, r7, 3, 2, PM_LANG_HALF_WIDTH_DIGITS);
        ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 1), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 0), 8, 5, 0x3e7 + 0x59, 0, h2);
        WORDSET_Delete(h2);
    } else {
        ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 1), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 1), 8, 5, 0x3e7 + 0x59, 0, 0);
    }
    name = MSGDATA_AllocString(gSpeciesNameBank, I_SPECIES(p));
    PRINTSYS_PrintQueColor((struct PrintQueue *)I_PRINTQUE(p), BMP_WIN_GetBmp(I_WIN(p, 1)), 0x30, 5, name, I_FONT(p), 0x440);
    STRBUF_Delete(name);
    m = 0;
    if (I_SHOWCURRENT(p) != 0) {
        m = I_SPECIES(p);
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 1), l28, I_PRINTQUE(p), I_FONT(p), m, 0, 0x16, 0x440, 2, 0);
    ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 2), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 2), 0, 4, 0x440, 0, 0);
    ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 2), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 3), 0, 0x14, 0x440, 0, 0);
    r6 = 0;
    if (I_SHOWCURRENT(p) != 0) {
        r6 = t;
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 2), l24, I_PRINTQUE(p), I_FONT(p), r6, ZUKAN_INFO_HEIGHT_VALUE_X, 4, 0x440, 1, 0);
    r6 = 0;
    if (I_SHOWCURRENT(p) != 0) {
        r6 = t;
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 2), l20, I_PRINTQUE(p), I_FONT(p), r6, ZUKAN_INFO_WEIGHT_VALUE_X, 0x14, 0x440, 1, 0);
    t = 0;
    if (I_SHOWCURRENT(p) != 0) {
        t = I_SPECIES(p);
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 3), l1c, I_PRINTQUE(p), I_FONT(p), t, 4, 5, 0x3c40, 0, 0);
    for (k = 0; k < 4; k++) {
        I_SLOTDIRTY(p, k) = 0;
    }
    ZUKAN_INFO_FlushSlotWindows(p);
}

void ZUKAN_INFO_DrawPage(char *p, int idx)
{
    struct MsgBank *titleBank = I_OBJS(p, 0);
    struct MsgBank *categoryBank = I_OBJS(p, idx + 5);
    struct MsgBank *heightBank = I_OBJS(p, idx + 11);
    struct MsgBank *weightBank = I_OBJS(p, idx + 17);
    struct MsgBank *entryBank = I_OBJS(p, idx + 23);
    struct MsgBank *speciesNameBank = I_OBJS(p, idx + 29);
    unsigned short cur = (unsigned short)bw_dex_record(I_SPECIES(p));     /* (the game: the species) */
    unsigned short c;
    struct WordSet *h;
    unsigned char i;

    if (cur == 0x1e7 && I_FORM(p) != 0) {
        cur = 0x1ee;
    } else if (cur == 0x1ec && I_FORM(p) != 0) {
        cur = 0x1ef;
    }
    if (I_MODE(p) != 2) {
        ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 0), titleBank, I_PRINTQUE(p), I_FONT(p), 0x90, 0, 5, 0x3c40, 2, 0);
    }
    c = I_SPECIES(p);
    if (I_FORMTABLENEEDED(p) == 0) {
        unsigned short *tab = POKE_PERSONAL_LoadFormTable(I_HEAP(p), 0);

        c = tab[I_SPECIES(p)];
        GFL_HEAP_Free(tab);
    }
    if (c != 0x3e7) {
        h = WORDSET_CreateDefault(I_HEAP(p));
        if (idx == 0) {
            WORDSET_SetSlotNumber(h, 0, c, 3, 2, 0);
        } else {
            WORDSET_SetSlotNumber(h, 0, c, 3, 2, 1);
        }
        ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 1), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 0), 8, 5, 0x440, 0, h);
        WORDSET_Delete(h);
    } else {
        ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 1), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, 1), 8, 5, 0x440, 0, 0);
    }
    c = 0;
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 1), speciesNameBank, I_PRINTQUE(p), I_FONT(p), I_SPECIES(p), 0x30, 5, 0x440, 0, 0);
    if (I_SHOWCURRENT(p) != 0) {
        c = I_SPECIES(p);
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 1), categoryBank, I_PRINTQUE(p), I_FONT(p), c, 0, 0x16, 0x440, 2, 0);
    ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 2), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, idx + 4), 0, 4, 0x440, 0, 0);
    ZUKAN_INFO_DrawText(I_HEAP(p), I_WIN(p, 2), I_PRINTQUE(p), I_FONT(p), I_INPUT(p, idx + 10), 0, 0x14, 0x440, 0, 0);
    c = 0;
    if (I_SHOWCURRENT(p) != 0) {
        c = cur;
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 2), heightBank, I_PRINTQUE(p), I_FONT(p), c, d_021BC110[idx], 4, 0x440, 1, 0);
    c = 0;
    if (I_SHOWCURRENT(p) != 0) {
        c = cur;
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 2), weightBank, I_PRINTQUE(p), I_FONT(p), c, d_021BC11C[idx], 0x14, 0x440, 1, 0);
    c = 0;
    if (I_SHOWCURRENT(p) != 0) {
        c = I_SPECIES(p);
    }
    ZUKAN_INFO_DrawMessage(I_HEAP(p), I_WIN(p, 3), entryBank, I_PRINTQUE(p), I_FONT(p), c, 4, 5, 0x3c40, 0, 0);
    for (i = 0; i < 4; i++) {
        I_SLOTDIRTY(p, i) = 0;
    }
    ZUKAN_INFO_FlushSlotWindows(p);
}
