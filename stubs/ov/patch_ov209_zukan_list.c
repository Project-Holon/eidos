/*
 * patches: src/game/ov209/zukan_list.c ZUKAN_LIST_CreateFrameList ZUKAN_LIST_DeleteFrameList ZUKAN_LIST_FrameListMoveCallback
 *
 * symbols:
 *   gZukanListFrameListParams 0x021BE7AC data ov209
 *   gSpeciesNameBank 0x020A7370 data
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   ZUKAN_SAVE_GetLastViewedSpecies 0x0200CA8C thumb
 *   ZUKAN_SAVE_IsCaught 0x0200CED4 thumb
 *   ZUKAN_SAVE_IsSeen 0x0200D064 thumb
 *   FRAME_LIST_Create 0x021B7754 thumb ov067
 *   FRAME_LIST_Delete 0x021B796C thumb ov067
 *   FRAME_LIST_CopyMapToBuffer 0x021B7A10 thumb ov067
 *   FRAME_LIST_LoadPalettes 0x021B7A4C thumb ov067
 *   FRAME_LIST_AddItem 0x021B79E4 thumb ov067
 *   FRAME_LIST_GetItemValue 0x021B9398 thumb ov067
 *   MSGDATA_AllocString 0x02045EC0 arm
 *   STRBUF_Clone 0x02045874 arm
 *   STRBUF_Delete 0x02045808 arm
 *
 * The Pokedex list with more rows than the DS has species (sdk/bw_limits.c,
 * limit.species).  The list screen keeps one name a row in its work, an
 * array of 649 (ZukanList.speciesText, 0x04C), and makes its frame list for
 * 649 items: a National list of a raised species limit wrote past both.
 * Rows 0..648 keep their place in the work; the rows after them have theirs
 * here (one list screen is open at a time).  The frame list is made for the
 * list's own count when that is more than 649.  Otherwise the three
 * functions are the game's own, line for line.
 *
 * The game's own records
 * ----------------------
 *
 * ZukanList (src/game/ov209/zukan_list.c) at its offsets: args 0x000,
 * msgBank 0x03C, speciesText 0x04C, frameList 0xC2C, rowCount 0xC34.
 * ZukanListArgs: save 0x04, count 0x0A, ids 0x0C.  FrameListParams
 * (src/game/ov067/frame_list_params.h) is copied whole.  The offsets are the
 * DS ones: pointers are four bytes in every build that runs the overlays.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct ZukanList;
struct ZukanSave;
struct FrameList;
struct MsgBank;
struct Strbuf;

struct FrameListHitEntry;
struct FrameListOps;

typedef struct FrameListParams {
    u8 bgLayer;
    u8 bgLayerB;
    u8 tileX;
    signed char firstRow;
    u8 tileWidth;
    u8 rowHeight;
    u8 winX;
    u8 winY;
    u8 winW;
    u8 winH;
    u8 winPltt;
    u8 stepTable[6];
    u8 cursorPltt;
    u8 scrollBarPad;
    u8 pad13;
    u16 itemCount;
    u16 mapCount;
    u8 initialCursor;
    u8 visibleRows;
    u16 initialScroll;
    const struct FrameListHitEntry *hitEntries;
    const struct FrameListOps *ops;
    void *userData;
} FrameListParams;

typedef char frame_list_params_is_0x28[sizeof(FrameListParams) == 0x28 || sizeof(void *) != 4 ? 1 : -1];

extern const FrameListParams gZukanListFrameListParams;
extern struct MsgBank *gSpeciesNameBank;

void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
u16 ZUKAN_SAVE_GetLastViewedSpecies(struct ZukanSave *pZukan);
u32 ZUKAN_SAVE_IsCaught(struct ZukanSave *pZukan, u16 species);
u32 ZUKAN_SAVE_IsSeen(struct ZukanSave *pZukan, u16 species);
struct FrameList *FRAME_LIST_Create(const FrameListParams *params, u32 heapId);
void FRAME_LIST_Delete(struct FrameList *list);
void FRAME_LIST_AddItem(struct FrameList *list, unsigned mapIndex, unsigned value);
void FRAME_LIST_CopyMapToBuffer(struct FrameList *list, void *source, u32 fileIndex, u32 memberIndex, u32 mapSlot);
void FRAME_LIST_LoadPalettes(struct FrameList *list, u8 *palettes, u32 firstSlot, u32 srcSlot, u32 slotCount);
u32 FRAME_LIST_GetItemValue(struct FrameList *list, int index);
struct Strbuf *MSGDATA_AllocString(struct MsgBank *bank, u32 index);
struct Strbuf *STRBUF_Clone(struct Strbuf *src, u32 heapId);
void STRBUF_Delete(struct Strbuf *strbuf);

void ZUKAN_LIST_PrintListRow(struct ZukanList *p, unsigned char *dest, struct Strbuf *text0, unsigned index);
void ZUKAN_LIST_ShowSpeciesActor(struct ZukanList *p, u16 species, int pos, int mode);

#define W_PTR(work, off)  (*(void **)((char *)(work) + (off)))
#define W_U16(work, off)  (*(u16 *)((char *)(work) + (off)))
#define W_INT(work, off)  (*(int *)((char *)(work) + (off)))

#define L_ARGS(p)        W_PTR(p, 0x000)
#define L_MSGBANK(p)     ((struct MsgBank *)W_PTR(p, 0x03C))
#define L_FRAMELIST(p)   ((struct FrameList *)W_PTR(p, 0xC2C))
#define L_ROWCOUNT(p)    W_INT(p, 0xC34)
#define A_SAVE(a)        ((struct ZukanSave *)W_PTR(a, 0x04))
#define A_COUNT(a)       W_U16(a, 0x0A)
#define A_IDS(a)         ((u16 *)W_PTR(a, 0x0C))

#define DS_ROWS   649u      /* speciesText's length */
#define MAX_ROWS  2048u     /* the species limit's ceiling, and one */

static struct Strbuf *g_rows_after[MAX_ROWS - DS_ROWS];

/* The place of row i's name. */
static struct Strbuf **row_text(struct ZukanList *p, u32 i)
{
    if (i < DS_ROWS) {
        return (struct Strbuf **)((char *)p + 0x04C + 4u * i);
    }
    return i < MAX_ROWS ? &g_rows_after[i - DS_ROWS] : &g_rows_after[0];
}

void ZUKAN_LIST_CreateFrameList(struct ZukanList *p)
{
    FrameListParams tmpl;
    void *arc, *args = L_ARGS(p);
    struct Strbuf *text;
    u32 species, rowIndex, count = A_COUNT(args);
    u16 *ids = A_IDS(args);

    arc = ARC_TOOL_CreateHandle(159, 0x806b);
    species = ZUKAN_SAVE_GetLastViewedSpecies(A_SAVE(args));
    tmpl = gZukanListFrameListParams;
    tmpl.itemCount = count > DS_ROWS ? (u16)count : (u16)DS_ROWS;
    tmpl.userData = p;
    for (rowIndex = 0; rowIndex < count; rowIndex++) {
        if (species == ids[rowIndex]) {
            if (rowIndex > count - 7) {
                tmpl.initialCursor = (u8)(rowIndex - (count - 7));
                tmpl.initialScroll = (u16)(rowIndex - tmpl.initialCursor);
            } else if (rowIndex < 7) {
                tmpl.initialCursor = (u8)rowIndex;
            } else {
                tmpl.initialScroll = (u16)rowIndex;
            }
            break;
        }
    }
    W_PTR(p, 0xC2C) = FRAME_LIST_Create(&tmpl, 107);
    if (count < 7) {
        L_ROWCOUNT(p) = (int)count;
    } else {
        L_ROWCOUNT(p) = 7;
    }
    FRAME_LIST_CopyMapToBuffer(L_FRAMELIST(p), arc, 66, 0, 0);
    FRAME_LIST_CopyMapToBuffer(L_FRAMELIST(p), arc, 67, 0, 1);
    FRAME_LIST_CopyMapToBuffer(L_FRAMELIST(p), arc, 68, 0, 2);
    FRAME_LIST_LoadPalettes(L_FRAMELIST(p), arc, 48, 2, 3);
    ARC_TOOL_DeleteHandle(arc);
    for (rowIndex = 0; rowIndex < count && rowIndex < MAX_ROWS; rowIndex++) {
        if (ZUKAN_SAVE_IsCaught(A_SAVE(args), ids[rowIndex]) == 1) {
            text = MSGDATA_AllocString(gSpeciesNameBank, ids[rowIndex]);
            *row_text(p, rowIndex) = STRBUF_Clone(text, 107);
            STRBUF_Delete(text);
            FRAME_LIST_AddItem(L_FRAMELIST(p), 0, 0x20000000u | ids[rowIndex]);
        } else if (ZUKAN_SAVE_IsSeen(A_SAVE(args), ids[rowIndex]) == 1) {
            text = MSGDATA_AllocString(gSpeciesNameBank, ids[rowIndex]);
            *row_text(p, rowIndex) = STRBUF_Clone(text, 107);
            STRBUF_Delete(text);
            FRAME_LIST_AddItem(L_FRAMELIST(p), 1, 0x10000000u | ids[rowIndex]);
        } else {
            *row_text(p, rowIndex) = MSGDATA_AllocString(L_MSGBANK(p), 7);
            FRAME_LIST_AddItem(L_FRAMELIST(p), 2, ids[rowIndex]);
        }
    }
}

void ZUKAN_LIST_DeleteFrameList(struct ZukanList *p)
{
    u32 i;

    FRAME_LIST_Delete(L_FRAMELIST(p));
    for (i = 0; i < MAX_ROWS; i++) {
        struct Strbuf **text = row_text(p, i);

        if (*text != 0) {
            STRBUF_Delete(*text);
            if (i >= DS_ROWS) {
                *text = 0;      /* (the work's own go with the work) */
            }
        }
    }
}

void ZUKAN_LIST_FrameListMoveCallback(struct ZukanList *p, int index, unsigned char *dest, u16 position, int mode)
{
    u32 itemValue;
    u32 speciesId;

    itemValue = FRAME_LIST_GetItemValue(L_FRAMELIST(p), index);
    speciesId = itemValue & 0x0FFFFFFF;
    ZUKAN_LIST_PrintListRow(p, dest, *row_text(p, (u32)index), speciesId);
    if ((itemValue & 0xF0000000) >> 28) {
        ZUKAN_LIST_ShowSpeciesActor(p, (u16)speciesId, (int)position, mode);
    }
}
