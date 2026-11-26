/*
 * patches: src/game/ov070/itemmenu.c ITEMMENU_SetMoveTypeIconsVisible ITEMMENU_PrintMoveInfoText ITEMMENU_FreeGraphics ITEMMENU_DrawMoveInfo
 *
 * The bag's TM and HM pocket shows the type of a move a mod gives a type it
 * adds (sdk/type_icons.c).
 *
 * The bag makes one type icon actor a type, 17 of them (typeActors), and
 * shows the one of the move under the cursor: typeActors[type].  A type past
 * 16 would read the fields after the array.  These copies show an added
 * type's icon on the Dark actor instead (type_actor, below), with the
 * tiles of the icon drawn with the colours the bag's added types' row
 * gives the type: the type palette is at sub OBJ row 4 here (0x80 bytes in,
 * rows 0..2 of the file), and nothing else on this screen loads sub OBJ
 * rows past 7, so the added types' row is 11 (palette offset 7).  The bag has
 * no palette animator on the sub screen.  ITEMMENU_FreeGraphics forgets the
 * added type's character (CLACT_ExitSystem frees it with the rest) and the
 * bag's row.
 *
 * ITEMMENU_DrawMoveInfo prints a TM's number as its machine id + 1 (TM01 is
 * id 0): a TM a mod adds (TM96 on: sdk/machines.c) has the id TM number +
 * 5, so it prints that number, in three digits from TM100
 * (bw_machine_bag_number).  The rest is the game's own.
 * The rest is the game's own.
 *
 * symbols:
 *   WAZA_TOOL_GetParam 0x0201BD28 thumb
 *   WAZA_TOOL_GetMaxPP 0x0201C158 thumb
 *   WAZA_TOOL_GetType 0x0201C1C8 thumb
 *   WAZA_TOOL_IsAlwaysHit 0x0201C1F4 thumb
 *   WORDSET_SetSlotNumber 0x0201EFA4 thumb
 *   WORDSET_CopyTaggedText 0x0201F250 thumb
 *   FILE_UTIL_GetMachineMove 0x020212B0 thumb
 *   APP_ICON_GetArchiveId 0x020275E4 thumb
 *   BMP_Delete 0x020437AC arm
 *   BMP_FillRect 0x02043AF4 arm
 *   BMP_WIN_Delete 0x020452E8 arm
 *   BMP_WIN_GetBmp 0x02045770 arm
 *   MSGDATA_ReadString 0x02045CAC arm
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   CLACT_ExitSystem 0x0204A5AC arm
 *   CLACT_RegisterCharacter 0x0204A6C8 arm
 *   CLACT_ReleaseCharacter 0x0204A8D4 arm
 *   CLACT_GetCharacterProxy 0x0204AB84 arm
 *   CLACT_ReleasePalette 0x0204ADA4 arm
 *   CLACT_ReleaseCellAnim 0x0204AFD8 arm
 *   CLACT_DeleteUnit 0x0204B1CC arm
 *   CLACT_DeleteActor 0x0204B3B4 arm
 *   CLACT_SetActorDrawEnable 0x0204B3DC arm
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   CLACT_SetActorImageProxy 0x0204B784 arm
 *   WORDSET_SetSlotFromMsgCB 0x0201EF34 thumb
 *   FILE_UTIL_ReadStringAlt 0x02020FB4 thumb
 *   FILE_UTIL_GetMachineIndex 0x02021350 thumb
 *   BMP_WIN_TransferChars 0x02045334 arm
 *   BMP_WIN_ClearScreen 0x020456F8 arm
 *   BMP_Clear 0x02043B5C arm
 *   ITEMMENU_RegisterItemName 0x021B61F4 thumb ov070
 *   ITEMMENU_CreateMovingCursor 0x021B9E14 thumb ov070
 *   ITEMMENU_GetSlot 0x021B616C thumb ov070
 *   ITEMMENU_GetCursorIndex 0x021B61B8 thumb ov070
 *   ITEMMENU_PrintToWindow 0x021BB6E0 thumb ov070
 *   ITEMMENU_FlushWindowAt 0x021BB724 thumb ov070
 *
 * The game's own record
 * ---------------------
 * ItemMenuWork (src/game/ov070/itemmenu_types.h), as far as these read it:
 * msgBank 0x51C, wordSet 0x520, tempString 0x524, messageString 0x528, heapId
 * 0x544, cellRes 0x548, cellAnimRes 0x54C, cellTransRes 0x550, typeIconRes
 * 0x59C,
 * cursorCharRes2 0x5E0 (the type palette), cursorPaletteRes2 0x5E4 (the type
 * icons' cell), slotBitmap 0x614, clactSys 0x69C, movingCursorActor 0x6A0,
 * cursorActor 0x6A8, damageClassActors 0x6C0, typeActors 0x6CC, listWindows
 * 0x734 (8 bytes each), screenState 0x840.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PM_LANG_HALF_WIDTH_DIGITS 1
#define GAME_TYPES 17u

struct Actor;
struct ClactActor;
struct Bmp;
struct BmpWin;
struct MsgBank;
struct WordSet;
struct Strbuf;
struct Unit;

typedef struct ItemMenuWindow {
    struct BmpWin *window;
    u32 extra;
} ItemMenuWindow;

typedef struct BagSlot {
    u16 itemId;
    u16 count;
} BagSlot;

typedef int TextCoord;

enum {
    ITEMMENU_WINDOW_ITEM_NAME = 0,
    ITEMMENU_WINDOW_ITEM_COUNT = 1,
    ITEMMENU_WINDOW_ITEM_PRICE = 2,
    ITEMMENU_WINDOW_ITEM_DESC = 3,
    ITEMMENU_WINDOW_MENU_0 = 4,
    ITEMMENU_WINDOW_MENU_3 = 7,
    ITEMMENU_WINDOW_MENU_4 = 8
};

typedef struct ItemMenuWork {
    u8 pad_000[0x51C];
    struct MsgBank *msgBank;            /* 0x51C */
    struct WordSet *wordSet;            /* 0x520 */
    struct Strbuf *tempString;          /* 0x524 */
    struct Strbuf *messageString;       /* 0x528 */
    u8 pad_52c[0x544 - 0x52C];
    u16 heapId;                         /* 0x544 */
    u8 pad_546[2];
    int cellRes;                        /* 0x548 */
    int cellAnimRes;                    /* 0x54C */
    int cellTransRes;                   /* 0x550 */
    u8 pad_554[0x59C - 0x554];
    u32 typeIconRes[17];                /* 0x59C */
    u32 cursorCharRes2;                 /* 0x5E0: the type palette */
    u32 cursorPaletteRes2;              /* 0x5E4: the type icons' cell */
    u8 pad_5e8[0x614 - 0x5E8];
    struct Bmp *slotBitmap[8];          /* 0x614 */
    u8 pad_634[0x69C - 0x634];
    void *clactSys;                     /* 0x69C */
    struct Actor *movingCursorActor;    /* 0x6A0 */
    struct Actor *selectorActor;        /* 0x6A4 */
    struct Actor *cursorActor;          /* 0x6A8 */
    struct Actor *tabActor[5];          /* 0x6AC */
    struct Actor *damageClassActors[3]; /* 0x6C0 */
    struct Actor *typeActors[17];       /* 0x6CC */
    struct Actor *sideActor[7];         /* 0x710 */
    struct Actor *inputGuard;           /* 0x72C */
    void *touchTask;                    /* 0x730 */
    ItemMenuWindow listWindows[9];      /* 0x734 */
    u8 pad_77c[0x80C - 0x77C];
    int currentPocket;                  /* 0x80C */
    u8 pad_810[0x840 - 0x810];
    int screenState;                    /* 0x840 */
} ItemMenuWork;

typedef char item_menu_heap_at_0x544[sizeof(void *) != 4 || __builtin_offsetof(ItemMenuWork, heapId) == 0x544 ? 1 : -1];
typedef char item_menu_type_actors_at_0x6cc[sizeof(void *) != 4 || __builtin_offsetof(ItemMenuWork, typeActors) == 0x6CC ? 1 : -1];
typedef char item_menu_windows_at_0x734[sizeof(void *) != 4 || __builtin_offsetof(ItemMenuWork, listWindows) == 0x734 ? 1 : -1];
typedef char item_menu_state_at_0x840[sizeof(void *) != 4 || __builtin_offsetof(ItemMenuWork, screenState) == 0x840 ? 1 : -1];

/* sdk/type_icons.h */
extern void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                u16 type_rows);
extern void bw_type_screen_pop(const void *owner);

u32 WAZA_TOOL_GetParam(u32 move, u32 param);
u8 WAZA_TOOL_GetMaxPP(u16 move, u32 ppUp);
u8 WAZA_TOOL_GetType(u16 move);
u32 WAZA_TOOL_IsAlwaysHit(u16 move);
void WORDSET_SetSlotNumber(struct WordSet *words, u32 slot, u32 number, u32 topDigitIndex, u32 padMode,
                           u32 isHalfWidth);
void WORDSET_CopyTaggedText(struct WordSet *words, struct Strbuf *dst, struct Strbuf *src);
u16 FILE_UTIL_GetMachineMove(u16 machineId);
u32 APP_ICON_GetArchiveId(void);
u32 APP_ICON_GetTypeCellFileId(u32 type);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
void BMP_Delete(struct Bmp *bmp);
void BMP_FillRect(struct Bmp *bmp, u32 x, u32 y, u32 w, u16 h, u8 value);
u32 BMP_WIN_Delete(struct BmpWin *win);
struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
void MSGDATA_ReadString(struct MsgBank *bank, u32 index, struct Strbuf *dest);
void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
void CLACT_ExitSystem(void);
int CLACT_RegisterCharacter(void *archive, u32 file_idx, u32 compressed, u32 vram_mode, u16 heap_id);
void CLACT_ReleaseCharacter(u32 idx);
void CLACT_ReleasePalette(u32 idx);
void CLACT_ReleaseCellAnim(u32 idx);
u32 CLACT_DeleteUnit(struct Unit *unit);
void CLACT_DeleteActor(struct ClactActor *actor);
void CLACT_SetActorDrawEnable(struct ClactActor *actor, u32 value);
void CLACT_SetActorPaletteOffset(struct ClactActor *actor, u32 offset, u32 pltt_op);
struct ImageProxy;
void CLACT_GetCharacterProxy(u32 idx, struct ImageProxy *out);
void CLACT_SetActorImageProxy(char *actor_base, const struct ImageProxy *proxy);
BagSlot *ITEMMENU_GetSlot(ItemMenuWork *work, int index);
int ITEMMENU_GetCursorIndex(ItemMenuWork *work);
void ITEMMENU_PrintToWindow(ItemMenuWork *work, ItemMenuWindow *slot, void *str, u16 x, TextCoord y, u16 color);
void ITEMMENU_FlushWindowAt(ItemMenuWindow *slot);
void WORDSET_SetSlotFromMsgCB(struct WordSet *words, u32 slot, u32 entryIndex);
void FILE_UTIL_ReadStringAlt(void *dst, u32 entry, u32 heap);
u8 FILE_UTIL_GetMachineIndex(u16 machineId);
u32 FILE_UTIL_GetId(u32 machineId);
void BMP_WIN_TransferChars(struct BmpWin *win);
void BMP_WIN_ClearScreen(struct BmpWin *win);
void BMP_Clear(struct Bmp *bmp, u8 value);
void ITEMMENU_RegisterItemName(ItemMenuWork *work, int slot, unsigned int itemId);
void ITEMMENU_CreateMovingCursor(ItemMenuWork *work, int id);
/* sdk/machines.c */
extern u32 bw_machine_bag_number(u32 id);

#define WAZA_TOOL_GetDamageType(a) WAZA_TOOL_GetParam((a), 2)

/* ------------------------------------------------------------------ */
/* An added type's icon on a borrowed actor                            */
/* ------------------------------------------------------------------ */

/*
 * The bag's actor unit has no room for one more actor (CLACT_CreateActor
 * fails), and the bag shows one type icon at a time.  So an added type is
 * shown on the last type's actor (Dark): its image is the added type's tiles
 * (a character of their own) and its palette offset the added types' row,
 * until Dark itself is shown again, which gives it its own back.  The
 * bag's own loops over the 17 actors hide it with the rest.
 */
#define BORROWED 16u

typedef struct ImageProxy {
    u32 word[9];
} ImageProxy;

static ItemMenuWork *g_work;    /* the bag the borrowed actor belongs to   */
static int g_char = -1;         /* the added type's character, or -1       */
static u32 g_type;              /* its type                                */
static int g_borrowed;          /* the actor shows the added type          */

/* The actor that shows `type`. */
static struct ClactActor *type_actor(ItemMenuWork *work, u32 type)
{
    struct ClactActor *actor = (struct ClactActor *)work->typeActors[BORROWED];
    ImageProxy proxy;
    void *archive;
    int old;

    if (g_work != work) {
        g_work = work;
        g_char = -1;
        g_borrowed = 0;
    }
    if (type < GAME_TYPES) {
        if (g_borrowed && type == BORROWED) {
            CLACT_GetCharacterProxy(work->typeIconRes[BORROWED], &proxy);
            CLACT_SetActorImageProxy((char *)actor, &proxy);
            CLACT_SetActorPaletteOffset(actor, APP_ICON_GetTypePaletteOffset((int)BORROWED), 1);
            g_borrowed = 0;
        }
        return (struct ClactActor *)work->typeActors[type];
    }
    if (g_char < 0) {
        bw_type_screen_push(work, 1, 4, 11, 0, 0, 0);
    }
    if (g_char >= 0 && g_type == type) {
        APP_ICON_GetTypeCellFileId(type);       /* (its colours, lately shown) */
    } else {
        old = g_char;
        archive = ARC_TOOL_CreateHandle(APP_ICON_GetArchiveId(), work->heapId);
        g_char = CLACT_RegisterCharacter(archive, APP_ICON_GetTypeCellFileId(type), 0, 1, work->heapId);
        ARC_TOOL_DeleteHandle(archive);
        g_type = type;
        g_borrowed = 0;
        if (old >= 0) {
            CLACT_GetCharacterProxy((u32)g_char, &proxy);
            CLACT_SetActorImageProxy((char *)actor, &proxy);
            g_borrowed = 1;
            CLACT_ReleaseCharacter((u32)old);
        }
    }
    if (!g_borrowed) {
        CLACT_GetCharacterProxy((u32)g_char, &proxy);
        CLACT_SetActorImageProxy((char *)actor, &proxy);
        g_borrowed = 1;
    }
    CLACT_SetActorPaletteOffset(actor, APP_ICON_GetTypePaletteOffset((int)type), 1);
    return actor;
}

/* ------------------------------------------------------------------ */
/* The game's functions                                                */
/* ------------------------------------------------------------------ */

void ITEMMENU_SetMoveTypeIconsVisible(ItemMenuWork *work, int mode)
{
    u16 moveId;
    if (mode == 1) {
        work->screenState = 1;
    } else {
        work->screenState = 2;
    }
    if (work->movingCursorActor != 0) {
        CLACT_SetActorDrawEnable((struct ClactActor *)work->movingCursorActor, mode);
    }
    moveId = FILE_UTIL_GetMachineMove(ITEMMENU_GetSlot(work, ITEMMENU_GetCursorIndex(work))->itemId);
    if (moveId != 0) {
        CLACT_SetActorDrawEnable((struct ClactActor *)(work->damageClassActors[WAZA_TOOL_GetDamageType(moveId)]), mode);
        CLACT_SetActorDrawEnable(type_actor(work, WAZA_TOOL_GetType(moveId)), mode);
    }
}

void ITEMMENU_PrintMoveInfoText(ItemMenuWork *work, int moveId)
{
    int maxPp;
    int power;
    int accuracy;
    ItemMenuWindow *infoWindow;

    infoWindow = &work->listWindows[0];

    CLACT_SetActorDrawEnable((struct ClactActor *)(work->damageClassActors[WAZA_TOOL_GetDamageType((u16)moveId)]), 1);
    CLACT_SetActorDrawEnable(type_actor(work, WAZA_TOOL_GetType((u16)moveId)), 1);
    maxPp = (int)WAZA_TOOL_GetMaxPP(moveId, 0);
    power = (int)WAZA_TOOL_GetParam(moveId, 3);
    accuracy = (int)WAZA_TOOL_GetParam(moveId, 4);
    BMP_FillRect(BMP_WIN_GetBmp(infoWindow->window), 0xB0, 0, 0x20, 0x28, 0);
    BMP_FillRect(BMP_WIN_GetBmp(infoWindow->window), 0xE8, 0, 0x18, 0x18, 0);
    if (power <= 1) {
        MSGDATA_ReadString(work->msgBank, 0x17, work->tempString);
        ITEMMENU_PrintToWindow(work, infoWindow, work->tempString, 0xB0, (TextCoord)4, 0x3DC0);
    } else {
        MSGDATA_ReadString(work->msgBank, 0x5F, work->tempString);
        WORDSET_SetSlotNumber(work->wordSet, 0, power, 3, 0, PM_LANG_HALF_WIDTH_DIGITS);
        WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
        ITEMMENU_PrintToWindow(work, infoWindow, work->messageString, 0xB0, (TextCoord)4, 0x3DC0);
    }
    if (accuracy == 0 || (int)WAZA_TOOL_IsAlwaysHit(moveId) == 1) {
        MSGDATA_ReadString(work->msgBank, 0x17, work->tempString);
        ITEMMENU_PrintToWindow(work, infoWindow, work->tempString, 0xB0, (TextCoord)0x18, 0x3DC0);
    } else {
        MSGDATA_ReadString(work->msgBank, 0x5F, work->tempString);
        WORDSET_SetSlotNumber(work->wordSet, 0, accuracy, 3, 0, PM_LANG_HALF_WIDTH_DIGITS);
        WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
        ITEMMENU_PrintToWindow(work, infoWindow, work->messageString, 0xB0, (TextCoord)0x18, 0x3DC0);
    }
    MSGDATA_ReadString(work->msgBank, 0x5E, work->tempString);
    WORDSET_SetSlotNumber(work->wordSet, 0, maxPp, 2, 0, PM_LANG_HALF_WIDTH_DIGITS);
    WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
    ITEMMENU_PrintToWindow(work, infoWindow, work->messageString, 0xE8, (TextCoord)4, 0x3DC0);
    ITEMMENU_FlushWindowAt(infoWindow);
}

void ITEMMENU_FreeGraphics(ItemMenuWork *work)
{
    int i;
    if (g_work == work) {
        /* (CLACT_ExitSystem below frees the added type's character) */
        g_work = 0;
        g_char = -1;
        g_borrowed = 0;
    }
    bw_type_screen_pop(work);
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_ITEM_COUNT].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_ITEM_DESC].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_ITEM_NAME].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_MENU_0].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_MENU_3].window));
    BMP_WIN_Delete((work->listWindows[ITEMMENU_WINDOW_MENU_4].window));
    CLACT_DeleteActor((struct ClactActor *)work->cursorActor);
    if (work->movingCursorActor != 0) {
        CLACT_DeleteActor((struct ClactActor *)work->movingCursorActor);
        CLACT_ReleaseCharacter(work->cellAnimRes);
        CLACT_ReleasePalette(work->cellRes);
    }
    CLACT_ReleaseCellAnim(work->cellTransRes);
    for (i = 0; i < 8; i++) {
        BMP_Delete(work->slotBitmap[i]);
    }
    CLACT_DeleteUnit((struct Unit *)work->clactSys);
    CLACT_ExitSystem();
}

void ITEMMENU_DrawMoveInfo(ItemMenuWork *work)
{
    u32 i;
    u32 j;
    u16 *slotPtr;
    u16 moveId;
    u8 machineIndex;
    u32 number;

    j = 0;
    for (i = 0; i < 3; i++)
        CLACT_SetActorDrawEnable((struct ClactActor *)work->damageClassActors[i], 0);
    for (j = 0; j < 17; j++)
        CLACT_SetActorDrawEnable((struct ClactActor *)work->typeActors[j], 0);
    slotPtr = (void *)ITEMMENU_GetSlot(work, ITEMMENU_GetCursorIndex(work));
    if (slotPtr == 0 || *slotPtr == 0) {
        ITEMMENU_SetMoveTypeIconsVisible(work, 0);
        return;
    }
    ITEMMENU_SetMoveTypeIconsVisible(work, 1);
    BMP_Clear(BMP_WIN_GetBmp(work->listWindows[ITEMMENU_WINDOW_ITEM_COUNT].window), 0);
    BMP_Clear(BMP_WIN_GetBmp(work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE].window), 0);
    BMP_Clear(BMP_WIN_GetBmp(work->listWindows[ITEMMENU_WINDOW_ITEM_DESC].window), 0);
    moveId = FILE_UTIL_GetMachineMove(*slotPtr);
    if (moveId == 0) {
        MSGDATA_ReadString(work->msgBank, 0x86, work->tempString);
        ITEMMENU_RegisterItemName(work, 0, *slotPtr);
        WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
        ITEMMENU_PrintToWindow(work, &work->listWindows[ITEMMENU_WINDOW_ITEM_COUNT], work->messageString, 0, (TextCoord)4, 0x3DC0);
        if (work->currentPocket != 4) {
            MSGDATA_ReadString(work->msgBank, 0x83, work->tempString);
            WORDSET_SetSlotNumber(work->wordSet, 0, slotPtr[1], 3, 0, PM_LANG_HALF_WIDTH_DIGITS);
            WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
            ITEMMENU_PrintToWindow(work, &work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE], work->messageString, 0, (TextCoord)4, 0x3DC0);
        } else {
            BMP_WIN_TransferChars(work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE].window);
        }
        BMP_WIN_ClearScreen((work->listWindows[ITEMMENU_WINDOW_ITEM_NAME].window));
    } else {
        machineIndex = FILE_UTIL_GetMachineIndex(*slotPtr);
        if (machineIndex == 0xFF) {
            MSGDATA_ReadString(work->msgBank, 0x45, work->tempString);
            number = bw_machine_bag_number(FILE_UTIL_GetId(*slotPtr));
            WORDSET_SetSlotNumber(work->wordSet, 0, number, number >= 100 ? 3 : 2, 2, PM_LANG_HALF_WIDTH_DIGITS);
        } else {
            MSGDATA_ReadString(work->msgBank, 0x47, work->tempString);
            WORDSET_SetSlotNumber(work->wordSet, 0, machineIndex + 1, 2, 2, PM_LANG_HALF_WIDTH_DIGITS);
        }
        WORDSET_SetSlotFromMsgCB(work->wordSet, 1, moveId);
        WORDSET_CopyTaggedText(work->wordSet, work->messageString, work->tempString);
        ITEMMENU_PrintToWindow(work, &work->listWindows[ITEMMENU_WINDOW_ITEM_COUNT], work->messageString, 0, (TextCoord)4, 0x3DC0);
        ITEMMENU_PrintMoveInfoText(work, moveId);
        BMP_WIN_TransferChars(work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE].window);
    }
    FILE_UTIL_ReadStringAlt(work->tempString, *slotPtr, work->heapId);
    ITEMMENU_PrintToWindow(work, &work->listWindows[ITEMMENU_WINDOW_ITEM_DESC], work->tempString, 0, (TextCoord)4, 0x3DC0);
    ITEMMENU_FlushWindowAt(&work->listWindows[ITEMMENU_WINDOW_ITEM_COUNT]);
    ITEMMENU_FlushWindowAt(&work->listWindows[ITEMMENU_WINDOW_ITEM_PRICE]);
    ITEMMENU_FlushWindowAt(&work->listWindows[ITEMMENU_WINDOW_ITEM_DESC]);
    ITEMMENU_CreateMovingCursor(work, *slotPtr);
}
