/*
 * patches: src/game/ov131/p_sta_info.c P_STA_INFO_ShowPage
 *
 * The summary's info page shows a Pokemon's types, and a type a mod adds
 * (sdk/type_icons.c).
 *
 * The page puts a type's icon on its two type actors by the character the
 * summary keeps for each of the 17 types (PStatusWork.typeChrRes[type]); a
 * type past 16 would read the fields after the array.  An added type takes
 * a character of its own instead (bw_type_icon_char), drawn with the
 * colours the summary's added types' row gives it.  The type palette is at
 * main OBJ row 8 on this screen (P_STATUS_LoadObjResource) and every main OBJ
 * row but 15 is taken, so row 15 is that row (palette offset 7).  The
 * summary has no palette animator.  The rest is the game's own.
 *
 * symbols:
 *   POKE_TOOL_GetBoxValue 0x02017E88 thumb
 *   BG_SYS_PushTile 0x020409B4 arm
 *   BG_SYS_DecodeTile 0x02040AE0 arm
 *   BG_SYS_DrawTileEx 0x020410D0 arm
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
 *   CLACT_SetActorAnimSeq 0x0204B878 arm
 *   P_STATUS_GetPokePara 0x021D6064 thumb ov131
 *
 * The game's own records
 * ----------------------
 * PStatusWork (src/game/ov131/p_status_types.h), as far as this reads it:
 * heapId 0x000, isEgg 0x038, typeChrRes 0x130, buttonActor 0x178,
 * typeIconActor 0x198.  PStaInfoWork (src/game/ov131/p_sta_info.c): win 0x0C
 * (11, the memo the last), bgScrnData 0x4C, eggScrnData 0x54, palData 0x5C,
 * barScrnData 0x64.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PSTATUS_TYPE_MAX 17
#define STA_INFO_WIN_MEMO 10
#define STA_INFO_WIN_MAX 11

struct BmpWin;
struct ClactActor;
struct ClactVec2;
struct ImageProxy;

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

typedef struct PStatusWork {
    u16 heapId;                         /* 0x000 */
    u8 pad_002[0x38 - 0x02];
    u32 isEgg;                          /* 0x038 */
    u8 pad_03c[0x130 - 0x3C];
    u32 typeChrRes[PSTATUS_TYPE_MAX];   /* 0x130 */
    void *clunit;                       /* 0x174 */
    void *buttonActor[8];               /* 0x178 */
    void *typeIconActor[2];             /* 0x198 */
} PStatusWork;

typedef struct PStaInfoWork {
    u32 isDrawn;                        /* 0x00 */
    int isWinActive;                    /* 0x04 */
    u16 *dexTable;                      /* 0x08 */
    struct BmpWin *win[STA_INFO_WIN_MAX];   /* 0x0C */
    void *msg[5];                       /* 0x38 */
    void *bgScrnData;                   /* 0x4C */
    void *bgScrnFile;                   /* 0x50 */
    void *eggScrnData;                  /* 0x54 */
    void *eggScrnFile;                  /* 0x58 */
    void *palData;                      /* 0x5C */
    void *palFile;                      /* 0x60 */
    void *barScrnData;                  /* 0x64 */
} PStaInfoWork;

typedef char pstatus_icons_at_0x198[sizeof(void *) != 4 || __builtin_offsetof(PStatusWork, typeIconActor) == 0x198 ? 1 : -1];
typedef char psta_info_bar_at_0x64[sizeof(void *) != 4 || __builtin_offsetof(PStaInfoWork, barScrnData) == 0x64 ? 1 : -1];

/* version.h: the European cartridges' language at run time */
extern int g_pm_language;
#define LANGUAGE_FRENCH  3
#define LANGUAGE_ITALIAN 4
#define LANGUAGE_GERMAN  5
#define LANGUAGE_SPANISH 7
#define PM_LANG_EU(english, french, german, italian, spanish)                                                   \
    (g_pm_language == LANGUAGE_FRENCH ? (french) : g_pm_language == LANGUAGE_GERMAN ? (german)                \
     : g_pm_language == LANGUAGE_ITALIAN ? (italian) : g_pm_language == LANGUAGE_SPANISH ? (spanish) : (english))
#define P_STA_INFO_TYPE1_X PM_LANG_EU(96, 96, 97, 96, 97)
#define P_STA_INFO_TYPE2_X PM_LANG_EU(130, 130, 131, 130, 131)

/* sdk/type_icons.h */
extern void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                  u16 type_rows);
extern int bw_type_icon_char(const void *owner, u32 type, u16 heap, u32 vram);

u32 POKE_TOOL_GetBoxValue(void *box, u32 paramId, void *arg);
void BG_SYS_PushTile(u32 index);
void BG_SYS_DecodeTile(u32 index, const void *src, u32 mode);
void BG_SYS_DrawTileEx(u32 index, u8 dx, u8 dy, u8 cw, u8 ch, const void *src, u8 sx, u8 sy, u8 sw, u8 sh);
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
void CLACT_SetActorAnimSeq(void *actor, u16 value);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
void *P_STATUS_GetPokePara(PStatusWork *wk);

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

void P_STA_INFO_ShowPage(PStatusWork *wk, PStaInfoWork *work)
{
    StaSpriteRes header1,header2;
    StaOamPoint offset;
    u8 i;
    void *mon;
    struct BmpWin *win;
    u32 type1,type2;
    mon=P_STATUS_GetPokePara(wk);
    if(wk->isEgg==0) BG_SYS_DrawTileEx(2,0,0,32,24,(void *)((u8 *)work->bgScrnData+12),0,0,32,32);
    else BG_SYS_DrawTileEx(2,0,0,32,24,(void *)((u8 *)work->eggScrnData+12),0,0,32,32);
    BG_SYS_MarkTileDirty(2);
    BG_SYS_DecodeTile(6,(u8 *)work->palData+12,((StaGraphicsFile *)((u8 *)work->palData))->dataSize);
    BG_SYS_MarkTileDirty(6);
    BG_SYS_DrawTileEx(5,0,0,32,3,(void *)((u8 *)work->barScrnData+12),0,0,32,32);
    BG_SYS_MarkTileDirty(5);
    for(i=0;i<10;i++) {
        win=work->win[i];
        BMP_WIN_TransferChars(win);
        BMP_WIN_MakeScreen(win);
        BG_SYS_PushTile(BMP_WIN_GetBgLayer(win));
    }
    win=work->win[STA_INFO_WIN_MEMO];
    BMP_WIN_TransferChars(win);
    BMP_WIN_MakeScreen(win);
    BG_SYS_PushTile(BMP_WIN_GetBgLayer(win));
    if(wk->isEgg==0) {
        type1=POKE_TOOL_GetBoxValue(mon,174,0);
        type2=POKE_TOOL_GetBoxValue(mon,175,0);
        CLACT_GetCharacterProxy(type_chr(wk,type1),(struct ImageProxy *)(&header1));
        CLACT_SetActorImageProxy(wk->typeIconActor[0],(struct ImageProxy *)(&header1));
        CLACT_SetActorPaletteOffset(wk->typeIconActor[0],APP_ICON_GetTypePaletteOffset((u8)type1),1);
        offset.x=P_STA_INFO_TYPE1_X;
        offset.y=48;
        CLACT_AddActorSurfacePos((struct ClactActor *)wk->typeIconActor[0],(const struct ClactVec2 *)&offset,0);
        CLACT_SetActorDrawEnable(wk->typeIconActor[0],1);
        CLACT_SetActorBgPriority(wk->typeIconActor[0],1);
        if(type1!=type2) {
            CLACT_GetCharacterProxy(type_chr(wk,type2),(struct ImageProxy *)(&header2));
            CLACT_SetActorImageProxy(wk->typeIconActor[1],(struct ImageProxy *)(&header2));
            CLACT_SetActorPaletteOffset(wk->typeIconActor[1],APP_ICON_GetTypePaletteOffset((u8)type2),1);
            offset.x=P_STA_INFO_TYPE2_X;
            offset.y=48;
            CLACT_AddActorSurfacePos((struct ClactActor *)wk->typeIconActor[1],(const struct ClactVec2 *)&offset,0);
            CLACT_SetActorDrawEnable(wk->typeIconActor[1],1);
            CLACT_SetActorBgPriority(wk->typeIconActor[1],1);
        } else CLACT_SetActorDrawEnable(wk->typeIconActor[1],0);
    } else {
        CLACT_SetActorDrawEnable(wk->typeIconActor[0],0);
        CLACT_SetActorDrawEnable(wk->typeIconActor[1],0);
    }
    CLACT_SetActorAnimSeq(wk->buttonActor[0],3);
}
