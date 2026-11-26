/*
 * patches: src/game/ov210/zknsearch_list.c ZKNSEARCH_LIST_InitFrameList2
 *
 * The Pokedex search lists the types mods add (sdk/type_chart.c,
 * limit.types).
 *
 * The type list has a row a type, the row's value the type id, and the
 * search keeps the chosen ids in its record (0xFF for none), so a type past
 * 16 needs only its row: the list holds 50.  The rows take their names
 * from bank 277 (ZKNSEARCH_MAIN_LookupEntryId: an added type's name is
 * there too, patch_ov210_zknsearch_main.c).  The rest is the game's own
 * (the build outside Japan).
 *
 * symbols:
 *   MSGDATA_AllocString 0x02045EC0 arm
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   FRAME_LIST_Create 0x021B7754 thumb ov067
 *   FRAME_LIST_AddItem 0x021B79E4 thumb ov067
 *   FRAME_LIST_CopyMapToBuffer 0x021B7A10 thumb ov067
 *   FRAME_LIST_LoadPalettes 0x021B7A4C thumb ov067
 *   ZKNSEARCH_MAIN_LookupEntryId 0x021BCEF4 thumb ov210
 *   d_021C0C48 0x021C0C48 data ov210
 *
 * The game's own record
 * ---------------------
 * ZknSearchWork (src/game/ov210/zknsearch_types.h), as far as this reads it:
 * msgBank 0x1D0, frameList 0x1E0, entries 0x1E4 (50), entryCount 0x2AC.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define GAME_TYPES 17

struct MsgBank;
struct Strbuf;

typedef struct Template {
    unsigned char prefix[24];
    unsigned char zero, count;
    unsigned short offset;
    unsigned words[2];
    void *ctx;
} Template;

typedef struct ZknSearchWork {
    u8 pad_000[0x1D0];
    void *msgBank;              /* 0x1D0 */
    u8 pad_1d4[0x1E0 - 0x1D4];
    void *frameList;            /* 0x1E0 */
    void *entries[50];          /* 0x1E4 */
    u32 entryCount;             /* 0x2AC */
} ZknSearchWork;

typedef char search_entries_at_0x1e4[sizeof(void *) != 4 || __builtin_offsetof(ZknSearchWork, entries) == 0x1E4 ? 1 : -1];
typedef char search_count_at_0x2ac[sizeof(void *) != 4 || __builtin_offsetof(ZknSearchWork, entryCount) == 0x2AC ? 1 : -1];

extern const Template d_021C0C48;

/* sdk/type_chart.h */
extern u32 bw_type_count(void);

struct Strbuf *MSGDATA_AllocString(struct MsgBank *bank, u32 index);
void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
void *FRAME_LIST_Create(const void *params, u32 heapId);
void FRAME_LIST_AddItem(void *list, unsigned mapIndex, unsigned value);
void FRAME_LIST_CopyMapToBuffer(void *list, void *source, u32 fileIndex, u32 memberIndex, u32 mapSlot);
void FRAME_LIST_LoadPalettes(void *list, void *palettes, u32 firstSlot, u32 srcSlot, u32 slotCount);
u16 ZKNSEARCH_MAIN_LookupEntryId(ZknSearchWork *work, int index);

void ZKNSEARCH_LIST_InitFrameList2(ZknSearchWork *ctx)
{
    void *archive = ARC_TOOL_CreateHandle(159, 0x807b);
    Template config;
    unsigned i;
    unsigned count = bw_type_count();
    config = d_021C0C48;
    config.zero = 0;
    config.offset = 0;
    config.ctx = ctx;
    ctx->frameList = FRAME_LIST_Create(&config, 123);
    ctx->entryCount = config.count;
    FRAME_LIST_CopyMapToBuffer(ctx->frameList, archive, 97, 0, 0);
    FRAME_LIST_LoadPalettes(ctx->frameList, archive, 73, 2, 3);
    ARC_TOOL_DeleteHandle(archive);
    /* (the types mods add after the game's 17) */
    for (i = 0; i < count && i < 50; i++) { FRAME_LIST_AddItem(ctx->frameList, 0, i); ctx->entries[i] = MSGDATA_AllocString(ctx->msgBank, ZKNSEARCH_MAIN_LookupEntryId(ctx, i)); }
}
