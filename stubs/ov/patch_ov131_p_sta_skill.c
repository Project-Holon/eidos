/*
 * patches: src/game/ov131/p_sta_skill.c P_STA_SKILL_ShowCantForgetMsg P_STA_SKILL_ApplyMovesScreen P_STA_SKILL_ShowSlot
 * keeps declarations
 *
 * The summary's refusal to forget a move, known to the one-screen interface
 * (engine/ui/recipe_party.c, forget_view; sdk/ui_text.c).
 *
 * P_STA_SKILL_ShowCantForgetMsg is what the forget-a-move page runs when
 * the move chosen is one the game does not let go (an HM move): it prints
 * "HM moves can't be forgotten now." in the place of the move's description
 * and plays the refusal sound.  The game's own sign of it,
 * PStaSkillWork.msgPrintPending, lasts until the print queue is done, which
 * is one frame or two, so an interface that draws one frame in several (the
 * fast speeds) never sees it.  The function does what the game's does and
 * also tells the port (bw_ui_forget_refused), which counts the refusals.
 *
 * The moves page shows the types mods add (sdk/type_icons.c).
 * P_STA_SKILL_ApplyMovesScreen (the Pokemon's types) and P_STA_SKILL_ShowSlot
 * (a move's type) put a type's icon on an actor by the character the summary
 * keeps for each of the 17 types (PStatusWork.typeChrRes[type]); a type past
 * 16 would read the fields after the array.  An added type takes a
 * character of its own instead (bw_type_icon_char), drawn with the colours
 * of the summary's added types' row, main OBJ row 15 (palette offset 7 from
 * the type palette's row 8, as in patch_ov131_p_sta_info.c).  The rest is
 * the game's own.
 *
 * symbols:
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   BMP_Clear  0x02043B5C  arm
 *   P_STATUS_PrintMsgToWin  0x021D618C  thumb  ov131
 *   GFL_USE_PlaySeDefaultVolume  0x020061E4  thumb
 *   POKE_TOOL_GetBoxValue 0x02017E88 thumb
 *   WAZA_TOOL_GetType 0x0201C1C8 thumb
 *   BG_SYS_PushTile 0x020409B4 arm
 *   BG_SYS_PushTileSource 0x020409E0 arm
 *   BG_SYS_DecodeTile 0x02040AE0 arm
 *   BG_SYS_DrawTileEx 0x020410D0 arm
 *   BG_SYS_ClearLayerEx 0x02041570 arm
 *   BG_SYS_MarkTileDirty 0x02041B6C arm
 *   BMP_WIN_TransferChars 0x02045334 arm
 *   BMP_WIN_MakeScreen 0x02045374 arm
 *   BMP_WIN_GetBgLayer 0x02045730 arm
 *   CLACT_GetCharacterProxy 0x0204AB84 arm
 *   CLACT_SetActorDrawEnable 0x0204B3DC arm
 *   CLACT_AddActorSurfacePos 0x0204B404 arm
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   CLACT_SetActorImageProxy 0x0204B784 arm
 *   CLACT_SetActorBgPriority 0x0204B84C arm
 *   P_STATUS_GetPokePara 0x021D6064 thumb ov131
 *   P_STA_OAM_TransferBitmap 0x021DBCF4 thumb ov131
 *
 * The game's own records
 * ----------------------
 * PStatusWork (src/game/ov131/p_status_types.h), as far as these read it:
 * heapId 0x000, param 0x008 (its newMove at 0x14), typeChrRes 0x130,
 * typeIconActor 0x198.  PStaSkillWork (src/game/ov131/p_sta_skill.c):
 * msgPrintPending 0x0C, slotCount 0x2B, movesPlttData 0x44, movesScrnData
 * 0x54, win 0x5C, statsObj 0x90, moveTypeObj 0x94, slot 0xB0 (5 of 0x18
 * bytes: index 0x00, redrawPending 0x04, frameObj 0x08, typeObj 0x0C,
 * nameActors 0x14).
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PSTATUS_TYPE_MAX 17

struct BmpWin;
struct Bmp;
struct ClactActor;
struct ClactVec2;
struct ImageProxy;
struct StaOamBitmapActors;

#define STA_SKILL_SLOT_MAX    5
#define STA_SKILL_WIN_MAX     13
#define STA_SKILL_WIN_MESSAGE 12

typedef struct StaSpriteRes {
    unsigned int word[9];
} StaSpriteRes;

typedef struct StaOamPoint {
    short x, y;
} StaOamPoint;

typedef struct StaGraphicsFile {
    unsigned char pad_0[8];
    unsigned int dataSize;
    unsigned char data[1];
} StaGraphicsFile;

typedef struct PStatusProcParam {
    u8 pad_00[0x14];
    u16 newMove;                        /* 0x14: move to learn in mode 2 */
} PStatusProcParam;

typedef struct PStatusWork {
    u16 heapId;                         /* 0x000 */
    u8 pad_002[0x08 - 0x02];
    PStatusProcParam *param;            /* 0x008 */
    u8 pad_00c[0x130 - 0x0C];
    u32 typeChrRes[PSTATUS_TYPE_MAX];   /* 0x130 */
    void *clunit;                       /* 0x174 */
    void *buttonActor[8];               /* 0x178 */
    void *typeIconActor[2];             /* 0x198 */
} PStatusWork;

typedef struct PStaSkillSlot {
    u8 index;                           /* 0x00: slot number, 0xFF once deleted */
    u8 pad_1[3];
    int redrawPending;                  /* 0x04 */
    struct ClactActor *frameObj;        /* 0x08 */
    struct ClactActor *typeObj;         /* 0x0C */
    struct Bmp *nameBmp;                /* 0x10 */
    struct StaOamBitmapActors *nameActors;  /* 0x14 */
} PStaSkillSlot;

/* The game's PStaSkillWork (src/game/ov131/p_sta_skill.c), as far as these
 * functions read it. */
typedef struct PStaSkillWork {
    u32 isOpen;                             /* 0x00 */
    u32 statsPrintPending;                  /* 0x04 */
    u32 movePrintPending;                   /* 0x08 */
    int msgPrintPending;                    /* 0x0C: 1 = message window waits for the print queue */
    u8 pad_10[0x2B - 0x10];
    u8 slotCount;                           /* 0x2B */
    u8 pad_2c[0x44 - 0x2C];
    void *movesPlttData;                    /* 0x44 */
    void *movesPlttFile;                    /* 0x48 */
    void *statsScrnData;                    /* 0x4C */
    void *statsScrnFile;                    /* 0x50 */
    void *movesScrnData;                    /* 0x54 */
    void *movesScrnFile;                    /* 0x58 */
    struct BmpWin *win[STA_SKILL_WIN_MAX];  /* 0x5C: STA_SKILL_WIN_* */
    struct ClactActor *statsObj;            /* 0x90 */
    struct ClactActor *moveTypeObj;         /* 0x94 */
    u8 pad_98[0xB0 - 0x98];
    PStaSkillSlot slot[STA_SKILL_SLOT_MAX]; /* 0xB0 */
} PStaSkillWork;

typedef char psta_skill_win_at_0x5c[sizeof(void *) != 4 || __builtin_offsetof(PStaSkillWork, win) == 0x5C ? 1 : -1];
typedef char psta_skill_slot_at_0xb0[sizeof(void *) != 4 || __builtin_offsetof(PStaSkillWork, slot) == 0xB0 ? 1 : -1];
typedef char psta_skill_slot_is_0x18[sizeof(void *) != 4 || sizeof(PStaSkillSlot) == 0x18 ? 1 : -1];
typedef char pstatus_icons_at_0x198[sizeof(void *) != 4 || __builtin_offsetof(PStatusWork, typeIconActor) == 0x198 ? 1 : -1];

extern void bw_ui_forget_refused(int result);

/* sdk/type_icons.h */
extern void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                  u16 type_rows);
extern int bw_type_icon_char(const void *owner, u32 type, u16 heap, u32 vram);

struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
void BMP_Clear(struct Bmp *bmp, u8 value);
void P_STATUS_PrintMsgToWin(PStatusWork *wk, struct BmpWin *bmpWinData, int msgId, int x, unsigned short y,
                            unsigned short color);
void GFL_USE_PlaySeDefaultVolume(u32 seqNo);
u32 POKE_TOOL_GetBoxValue(void *box, u32 paramId, void *arg);
u8 WAZA_TOOL_GetType(u16 move);
void BG_SYS_PushTile(u32 index);
void BG_SYS_PushTileSource(u32 index, const void *src, u32 mode, u32 offset);
void BG_SYS_DecodeTile(u32 index, const void *src, u32 mode);
void BG_SYS_DrawTileEx(u32 index, u8 dx, u8 dy, u8 cw, u8 ch, const void *src, u8 sx, u8 sy, u8 sw, u8 sh);
void BG_SYS_ClearLayerEx(int channel, u16 val);
void BG_SYS_MarkTileDirty(u32 bit);
void BMP_WIN_TransferChars(struct BmpWin *win);
void BMP_WIN_MakeScreen(struct BmpWin *win);
u8 BMP_WIN_GetBgLayer(struct BmpWin *win);
void CLACT_GetCharacterProxy(u32 idx, struct ImageProxy *out);
void CLACT_SetActorDrawEnable(void *actor, u32 value);
void CLACT_AddActorSurfacePos(struct ClactActor *actor, const struct ClactVec2 *delta, u32 which);
void CLACT_SetActorPaletteOffset(void *actor, u32 offset, u32 pltt_op);
void CLACT_SetActorImageProxy(void *actor_base, const struct ImageProxy *proxy);
void CLACT_SetActorBgPriority(void *actor, unsigned int priority);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
void *P_STATUS_GetPokePara(PStatusWork *wk);
void P_STA_SKILL_ShowSlot(PStatusWork *wk, PStaSkillWork *work, PStaSkillSlot *slot);
void P_STA_OAM_TransferBitmap(struct StaOamBitmapActors *grid);

void P_STA_SKILL_ShowCantForgetMsg(PStatusWork *wk, PStaSkillWork *work, int result)
{
    work->msgPrintPending = 1;
    BMP_Clear(BMP_WIN_GetBmp(work->win[STA_SKILL_WIN_MESSAGE]), 0);
    if (result == 1) {
        P_STATUS_PrintMsgToWin(wk, work->win[STA_SKILL_WIN_MESSAGE], 156, 0, 0, 15424);
    }
    GFL_USE_PlaySeDefaultVolume(1367);
    bw_ui_forget_refused(result);
}

/* The character that shows `type`: the summary's own below 17, the added
 * type's own above. */
static u32 type_chr(PStatusWork *wk, u32 type)
{
    int c;

    if (type < PSTATUS_TYPE_MAX) {
        return wk->typeChrRes[type];
    }
    bw_type_screen_ensure(wk, 0, 8, 15, 0, 0, 0);
    c = bw_type_icon_char(wk, type, wk->heapId, 0);
    return c >= 0 ? (u32)c : wk->typeChrRes[0];
}

void P_STA_SKILL_ApplyMovesScreen(PStatusWork *wk, PStaSkillWork *work)
{
    StaSpriteRes type1Res,type2Res;
    StaOamPoint pos;
    u8 i;
    void *win;
    void *pp;
    u32 type1,type2;
    BG_SYS_ClearLayerEx(4,0);
    for(i=8;i<=11;i++) {
        win=work->win[i];
        BMP_WIN_TransferChars(win);
        BMP_WIN_MakeScreen(win);
        BG_SYS_PushTile(BMP_WIN_GetBgLayer(win));
    }
    i=0;
    BG_SYS_PushTileSource(6,(u8 *)work->movesPlttData+12,((StaGraphicsFile *)work->movesPlttData)->dataSize,0);
    BG_SYS_DecodeTile(6,(const void *)((u8 *)work->movesPlttData+12),((StaGraphicsFile *)work->movesPlttData)->dataSize);
    BG_SYS_MarkTileDirty(6);
    BG_SYS_DrawTileEx(5,0,0,32,3,(const void *)(((u8 *)work->movesScrnData+12)),0,0,32,32);
    BG_SYS_MarkTileDirty(5);
    CLACT_SetActorDrawEnable((struct ClactActor *)work->statsObj,0);
    CLACT_SetActorDrawEnable((struct ClactActor *)work->moveTypeObj,1);
    for(;i<work->slotCount;i++) P_STA_SKILL_ShowSlot(wk,work,&work->slot[i]);
    pp=P_STATUS_GetPokePara(wk);
    type1=POKE_TOOL_GetBoxValue(pp,174,0);
    type2=POKE_TOOL_GetBoxValue(pp,175,0);
    CLACT_GetCharacterProxy(type_chr(wk,type1),(struct ImageProxy *)(&type1Res));
    CLACT_SetActorImageProxy(wk->typeIconActor[0],(struct ImageProxy *)(&type1Res));
    CLACT_SetActorPaletteOffset(wk->typeIconActor[0],APP_ICON_GetTypePaletteOffset((u8)type1),1);
    pos.x=208;
    pos.y=120;
    CLACT_AddActorSurfacePos((struct ClactActor *)wk->typeIconActor[0],(const struct ClactVec2 *)&pos,0);
    CLACT_SetActorDrawEnable(wk->typeIconActor[0],1);
    CLACT_SetActorBgPriority(wk->typeIconActor[0],0);
    if(type1!=type2) {
        CLACT_GetCharacterProxy(type_chr(wk,type2),(struct ImageProxy *)(&type2Res));
        CLACT_SetActorImageProxy(wk->typeIconActor[1],(struct ImageProxy *)(&type2Res));
        CLACT_SetActorPaletteOffset(wk->typeIconActor[1],APP_ICON_GetTypePaletteOffset((u8)type2),1);
        pos.x=240;
        pos.y=120;
        CLACT_AddActorSurfacePos((struct ClactActor *)wk->typeIconActor[1],(const struct ClactVec2 *)&pos,0);
        CLACT_SetActorDrawEnable(wk->typeIconActor[1],1);
        CLACT_SetActorBgPriority(wk->typeIconActor[1],0);
    } else CLACT_SetActorDrawEnable(wk->typeIconActor[1],0);
}

void P_STA_SKILL_ShowSlot(PStatusWork *wk, PStaSkillWork *work, PStaSkillSlot *slot) {
    unsigned moveId;
    int moveType;
    StaSpriteRes typeRes;
    void *pp;
    pp=P_STATUS_GetPokePara(wk);
    if(slot->index<4) moveId=POKE_TOOL_GetBoxValue(pp,slot->index+54,0);
    else moveId=wk->param->newMove;
    if(slot->redrawPending==1) {
        if(moveId!=0) {
            moveType=WAZA_TOOL_GetType(moveId);
            CLACT_GetCharacterProxy(type_chr(wk,(u32)moveType),(struct ImageProxy *)(&typeRes));
            CLACT_SetActorImageProxy((char *)slot->typeObj,(const struct ImageProxy *)((struct ImageProxy *)(&typeRes)));
            CLACT_SetActorPaletteOffset((struct ClactActor *)slot->typeObj,APP_ICON_GetTypePaletteOffset(moveType),1);
            CLACT_SetActorDrawEnable((struct ClactActor *)slot->typeObj,1);
        } else CLACT_SetActorDrawEnable((struct ClactActor *)slot->typeObj,0);
        P_STA_OAM_TransferBitmap((struct StaOamBitmapActors *)slot->nameActors);
        CLACT_SetActorDrawEnable((struct ClactActor *)slot->frameObj,1);
        slot->redrawPending=0;
    }
}
