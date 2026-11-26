/*
 * patches: src/game/ov094/btlv_input.c BTLV_INPUT_LoadCommonGraphics BTLV_INPUT_Delete
 *
 * The battle's move buttons show the types mods add (sdk/type_icons.c).
 *
 * The buttons' type icons take the type palette (a/0/8/3 member 0x21) at
 * sub OBJ row 8.  The load is the whole file, so sub row 15 holds its row 7,
 * which no picture uses, and no later load of this screen reaches row 15:
 * the finger cursor writes row 11, the Pokemon icons of a double or triple
 * battle rows 11..13, the wireless icon row 14.  So row 15 is the added
 * types' row on this screen: BTLV_INPUT_LoadCommonGraphics pushes the
 * screen (bw_type_screen_push) once the type palette is in, and
 * BTLV_INPUT_Delete pops it.  The input's palette animator (the battle
 * effect's) holds the sub OBJ palette's rows 0..14 only (0x1E0 bytes), so
 * row 15 is written to the hardware alone: no fade of the animator touches
 * it, and an added type's icon keeps its colours when the buttons of a
 * Pokemon that can only Struggle are dimmed.  The rest of both is the
 * game's own.
 *
 * symbols:
 *   PALANM_LoadHandlePaletteToSlot 0x020216B4 thumb
 *   PALANM_CopyHwPaletteToSlot 0x020216D4 thumb
 *   PALANM_GetDefaultWork 0x02021728 thumb
 *   APP_ICON_GetArchiveId 0x020275E4 thumb
 *   APP_ICON_GetTouchBarFileId 0x020275E8 thumb
 *   APP_ICON_GetTypePaletteFileId 0x020275FC thumb
 *   APP_ICON_GetTypeAnimFileId 0x02027600 thumb
 *   GFL_HEAP_Free 0x020307B0 arm
 *   TCB_Noop 0x02030DA4 arm
 *   MSGDATA_Delete 0x02045C04 arm
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   ARC_UTIL_UploadBgCharFromHandle 0x02049658 arm
 *   ARC_UTIL_UploadBgPaletteFromHandle 0x020498F4 arm
 *   CLACT_RegisterCharacter 0x0204A6C8 arm
 *   CLACT_RegisterPalette 0x0204ABF0 arm
 *   CLACT_GetPaletteVramAddress 0x0204AEF4 arm
 *   CLACT_RegisterCellAnim 0x0204AF28 arm
 *   MIi_CpuCopy16 0x02082A60 arm
 *   BTLV_EFFECT_GetPalanm 0x021F86E0 thumb ov094
 *   BTLV_INPUT_ReleaseScreenResources 0x022023D0 thumb ov094
 *   BTLV_INPUT_DeleteAllTasks 0x022068E8 thumb ov094
 *   BTLV_FINGER_CURSOR_Delete 0x02209B30 thumb ov094
 *
 * The game's own record
 * ---------------------
 * BtlvInput (src/game/ov094/btlv_input_shared_types.h), as far as these two
 * read it: taskMgr 0x00, taskMgrMem 0x04, arc 0x48, msgData 0x5C,
 * fingerCursor 0x60, palAnm 0x64, resCommonChr 0x104, resCommonPal 0x108,
 * resCommonCell 0x10C, resButtonChr 0x110, resButtonCell 0x114, typeChr
 * 0x1E0, typePal 0x1F0, typeCell 0x1F4, infoWinEnabled 0x23C, heap 0x240,
 * palBackup 0x284.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

typedef struct BtlvInput {
    void *taskMgr;              /* 0x000 */
    void *taskMgrMem;           /* 0x004 */
    u8 pad_008[0x48 - 0x08];
    void *arc;                  /* 0x048 */
    u8 pad_04c[0x5C - 0x4C];
    void *msgData;              /* 0x05C */
    void *fingerCursor;         /* 0x060 */
    void *palAnm;               /* 0x064 */
    u8 pad_068[0x104 - 0x68];
    int resCommonChr;           /* 0x104 */
    int resCommonPal;           /* 0x108 */
    int resCommonCell;          /* 0x10C */
    int resButtonChr;           /* 0x110 */
    int resButtonCell;          /* 0x114 */
    u8 pad_118[0x1E0 - 0x118];
    int typeChr[4];             /* 0x1E0 */
    int typePal;                /* 0x1F0 */
    int typeCell;               /* 0x1F4 */
    u8 pad_1f8[0x23C - 0x1F8];
    int infoWinEnabled;         /* 0x23C */
    u16 heap;                   /* 0x240 */
    u8 pad_242[0x284 - 0x242];
    u16 palBackup[16];          /* 0x284 */
} BtlvInput;

typedef char btlv_input_palanm_at_0x64[sizeof(void *) != 4 || __builtin_offsetof(BtlvInput, palAnm) == 0x64 ? 1 : -1];
typedef char btlv_input_typepal_at_0x1f0[sizeof(void *) != 4 || __builtin_offsetof(BtlvInput, typePal) == 0x1F0 ? 1 : -1];
typedef char btlv_input_heap_at_0x240[sizeof(void *) != 4 || __builtin_offsetof(BtlvInput, heap) == 0x240 ? 1 : -1];
typedef char btlv_input_palbackup_at_0x284[sizeof(void *) != 4 || __builtin_offsetof(BtlvInput, palBackup) == 0x284 ? 1 : -1];

/* sdk/type_icons.h */
extern void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                u16 type_rows);
extern void bw_type_screen_pop(const void *owner);

void PALANM_LoadHandlePaletteToSlot(void *fade, void *arcHandle, u32 fileId, u32 heapId, u32 slotIdx, u32 byteSize,
                                    u16 dstOffset);
void PALANM_CopyHwPaletteToSlot(void *fade, u32 bank, u32 dstOffset, u32 size);
u16 *PALANM_GetDefaultWork(void *p, int i);
u32 APP_ICON_GetArchiveId(void);
u32 APP_ICON_GetTouchBarFileId(void);
u32 APP_ICON_GetTypePaletteFileId(u32 mapping);
u32 APP_ICON_GetTypeAnimFileId(u32 mapping);
u32 APP_ICON_GetTypeCellFileId(u32 type);
u32 GFL_HEAP_Free(void *block);
void TCB_Noop(void *sys);
u32 MSGDATA_Delete(void *bank);
void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
u32 ARC_UTIL_UploadBgCharFromHandle(void *arc, u32 memberIdx, u32 bgId, u32 tileOffset, u32 size, u32 heapID,
                                    u16 allocFlags);
void ARC_UTIL_UploadBgPaletteFromHandle(void *arc, u32 memberIdx, u32 channel, u32 offset, u32 size, u32 heapID,
                                        u16 flags);
s32 CLACT_RegisterCharacter(void *archive, u32 file_idx, u32 compressed, u32 vram_mode, u16 heap_id);
s32 CLACT_RegisterPalette(void *archive, u32 file_idx, u32 vram_mode, u32 vram_offset, u16 heap_id);
int CLACT_GetPaletteVramAddress(u32 idx, u32 alternate);
s32 CLACT_RegisterCellAnim(void *archive, u32 cell_file_idx, u32 anim_file_idx, u32 heap_id);
void MIi_CpuCopy16(const void *srcp, void *destp, u32 size);
void *BTLV_EFFECT_GetPalanm(void);
void BTLV_INPUT_ReleaseScreenResources(BtlvInput *input);
void BTLV_INPUT_DeleteAllTasks(BtlvInput *input);
void BTLV_FINGER_CURSOR_Delete(void *cursor);

void BTLV_INPUT_Delete(BtlvInput *input)
{
    if (input == 0)
        return;
    bw_type_screen_pop(input);
    BTLV_INPUT_DeleteAllTasks((void *)input);
    BTLV_INPUT_ReleaseScreenResources(input);
    MSGDATA_Delete(input->msgData);
    if (input->fingerCursor != 0)
        BTLV_FINGER_CURSOR_Delete(input->fingerCursor);
    TCB_Noop(input->taskMgr);
    GFL_HEAP_Free(input->taskMgrMem);
    GFL_HEAP_Free(input);
}

void BTLV_INPUT_LoadCommonGraphics(BtlvInput *input)
{
    void *arc;
    u32 file;
    int i;
    int base;

    ARC_UTIL_UploadBgCharFromHandle(input->arc, 0x5b, 4, 0, 0, 0, input->heap);
    ARC_UTIL_UploadBgCharFromHandle(input->arc, 0x71, 7, 0, 0x8000, 0, input->heap);
    ARC_UTIL_UploadBgPaletteFromHandle(input->arc, 0x72, 7, 0, 0, 0, input->heap);
    PALANM_LoadHandlePaletteToSlot(input->palAnm, input->arc, 0x5c, input->heap, 1, 0x1e0, 0);
    if (input->infoWinEnabled == 1)
        MIi_CpuCopy16((const void *)(PALANM_GetDefaultWork(BTLV_EFFECT_GetPalanm(), 1) + 0x20), input->palBackup, 0x20);
    input->resCommonChr = CLACT_RegisterCharacter(input->arc, 0x73, 0, 1, input->heap);
    input->resCommonCell = CLACT_RegisterCellAnim(input->arc, 0x75, 0x76, input->heap);
    input->resCommonPal = CLACT_RegisterPalette(input->arc, 0x74, 1, 0, input->heap);
    PALANM_CopyHwPaletteToSlot(input->palAnm, 3, (u16)(CLACT_GetPaletteVramAddress(input->resCommonPal, 1) >> 1), 0x100);
    input->resButtonChr = CLACT_RegisterCharacter(input->arc, 0x77, 0, 1, input->heap);
    input->resButtonCell = CLACT_RegisterCellAnim(input->arc, 0x78, 0x79, input->heap);
    arc = ARC_TOOL_CreateHandle(APP_ICON_GetArchiveId(), (u16)((input->heap & 0x7fff) | 0x8000));
    file = APP_ICON_GetTypePaletteFileId(0);
    input->typeCell = CLACT_RegisterCellAnim(arc, file, APP_ICON_GetTypeAnimFileId(0), input->heap);
    input->typePal = CLACT_RegisterPalette(arc, APP_ICON_GetTouchBarFileId(), 1, 0x100, input->heap);
    PALANM_CopyHwPaletteToSlot(input->palAnm, 3, (u16)(CLACT_GetPaletteVramAddress(input->typePal, 1) >> 1), 0x60);
    /* the added types' row: the type palette's row 7, the last of the sub
     * OBJ palette */
    base = CLACT_GetPaletteVramAddress(input->typePal, 1) / 32;
    bw_type_screen_push(input, 1, base, base + 7, 0, 0, 0);
    for (i = 0; i < 4; i++)
        input->typeChr[i] = CLACT_RegisterCharacter(arc, APP_ICON_GetTypeCellFileId(0), 0, 1, input->heap);
    ARC_TOOL_DeleteHandle(arc);
}
