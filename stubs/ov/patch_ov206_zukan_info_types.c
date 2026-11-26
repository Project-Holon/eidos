/*
 * patches: src/game/ov206/zukan_info.c ZUKAN_INFO_CreateIconActors ZUKAN_INFO_DeleteIconActors ZUKAN_INFO_ShowFormPair ZUKAN_INFO_LayoutFormPairEx
 *
 * The Pokedex page shows the types mods add (sdk/type_icons.c).
 *
 * The page has two ways to show a species' types: 17 actors made up front,
 * one a type, shown as iconActors[type] (the page in its own language,
 * mode 2), or a character and an actor made for each of the two icons when
 * they are shown (charIds, actorSlots: the other pages, and the languages).
 * A type past 16 would read past the 17.  With added types the page always
 * takes the second way (ZUKAN_INFO_CreateIconActors), which asks for each
 * icon by its type (APP_ICON_GetTypeCellFileId, APP_ICON_GetTypePaletteOffset).
 *
 * The type palette is at OBJ row 2 here (main, or sub for the language side,
 * femaleFlag), rows 0..2 of the file.  The form icons take row 5, the
 * language highlight row 6; row 7 is free on both and inside the rows the
 * page's fades take (0..7), so it is the added types' row (palette offset
 * 5).  The page and its language side can both be up, one on each screen:
 * ZUKAN_INFO_ShowFormPair selects its own page (bw_type_screen_select)
 * before the icons are asked for.
 *
 * The languages: ZUKAN_INFO_LayoutFormPairEx draws over the icon the type's
 * name in the language shown, from a/1/5/9's sheet of that language, at the
 * cell d_021BC150 gives the type (17 rows).  An added type's six labels are
 * in a member of its own (types.txt `label`: Japanese, French, German,
 * Italian, Spanish, Korean, 32x16 each), drawn with the same colour pair as
 * its icon (bw_type_label_member); with none the icon shows as it is.  The
 * rest is the game's own.
 *
 * symbols:
 *   POKE_PERSONAL_GetParam 0x0201AE1C thumb
 *   APP_ICON_GetTouchBarFileId 0x020275E8 thumb
 *   APP_ICON_GetTypePaletteFileId 0x020275FC thumb
 *   APP_ICON_GetTypeAnimFileId 0x02027600 thumb
 *   GFL_HEAP_Free 0x020307B0 arm
 *   ARC_UTIL_LoadBgCharDataFromHandle 0x02049DF4 arm
 *   CLACT_RegisterCharacter 0x0204A6C8 arm
 *   CLACT_ReleaseCharacter 0x0204A8D4 arm
 *   CLACT_TransferCharacterPart 0x0204AA94 arm
 *   CLACT_RegisterPaletteEx 0x0204AC18 arm
 *   CLACT_ReleasePalette 0x0204ADA4 arm
 *   CLACT_RegisterCellAnim 0x0204AF28 arm
 *   CLACT_ReleaseCellAnim 0x0204AFD8 arm
 *   CLACT_CreateActor 0x0204B294 arm
 *   CLACT_DeleteActor 0x0204B3B4 arm
 *   CLACT_SetActorDrawEnable 0x0204B3DC arm
 *   CLACT_SetActorObjMode 0x0204B688 arm
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   CLACT_SetActorDrawPriority 0x0204B800 arm
 *   ZUKAN_INFO_LayoutFormPair 0x021BB534 thumb ov206
 *   ZUKAN_INFO_DeleteRow 0x021BB92C thumb ov206
 *   ZUKAN_INFO_DisableOtherRows 0x021BB980 thumb ov206
 *   d_021BC128 0x021BC128 data ov206
 *   d_021BC150 0x021BC150 data ov206
 *
 * The game's own record
 * ---------------------
 * ZukanInfo (src/game/ov206/zukan_info.c), as far as these read it: heap
 * 0x000, mode 0x018, femaleFlag 0x01C, clactUnit 0x024, charIds 0x0B8,
 * paletteHandle 0x0C8, cellAnimHandle 0x0CC, actorSlots 0x0D0, iconEnabled
 * 0x0E0, iconVisible 0x0E4, iconChars 0x0E8, iconActors 0x12C, shownFormA
 * 0x170, shownFormB 0x171, yOffset 0x1B4, w 0x1C4, x 0x26C.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#define POKETYPE_NONE 31    /* (arm9 poke_type.h, the PC build) */
#define GAME_TYPES 17

enum { B_FALSE, B_TRUE };

struct ClactActor;
struct ClactVec2;
struct Unit;
struct PersonalData;

typedef struct {
    unsigned short a, b, c;
    unsigned char d, e;
} ZukanInfoActorRect;

typedef struct {
    short x, y, z, w;
} ShortRect4;

typedef struct {
    unsigned char a, b;
} Tile;

/* NNSG2dCharacterData (arm9 nns_g2d_res_types.h) */
typedef struct Blob {
    u16 H;
    u16 W;
    u32 pixelFmt;
    u32 mapingType;
    u32 characterFmt;
    u32 szByte;
    void *pRawData;
} Blob;

typedef struct ZukanInfo {
    u16 heap;                           /* 0x000 */
    u8 pad_002[0x18 - 0x02];
    int mode;                           /* 0x018 */
    int femaleFlag;                     /* 0x01C */
    u8 pad_020[0x24 - 0x20];
    struct Unit *clactUnit;             /* 0x024 */
    u8 pad_028[0xB8 - 0x28];
    int charIds[2][2];                  /* 0x0B8 */
    u32 paletteHandle;                  /* 0x0C8 */
    u32 cellAnimHandle;                 /* 0x0CC */
    void *actorSlots[2][2];             /* 0x0D0 */
    int iconEnabled;                    /* 0x0E0 */
    int iconVisible;                    /* 0x0E4 */
    int iconChars[17];                  /* 0x0E8 */
    struct ClactActor *iconActors[17];  /* 0x12C */
    u8 shownFormA;                      /* 0x170 */
    u8 shownFormB;                      /* 0x171 */
    u8 pad_172[0x1B4 - 0x172];
    int yOffset;                        /* 0x1B4 */
    void *g[3];                         /* 0x1B8 */
    void *w[7];                         /* 0x1C4 */
    void *objs[35];                     /* 0x1E0 */
    void *x;                            /* 0x26C */
} ZukanInfo;

typedef char zukan_slots_at_0xd0[sizeof(void *) != 4 || __builtin_offsetof(ZukanInfo, actorSlots) == 0xD0 ? 1 : -1];
typedef char zukan_icons_at_0x12c[sizeof(void *) != 4 || __builtin_offsetof(ZukanInfo, iconActors) == 0x12C ? 1 : -1];
typedef char zukan_w_at_0x1c4[sizeof(void *) != 4 || __builtin_offsetof(ZukanInfo, w) == 0x1C4 ? 1 : -1];
typedef char zukan_x_at_0x26c[sizeof(void *) != 4 || __builtin_offsetof(ZukanInfo, x) == 0x26C ? 1 : -1];

extern const unsigned short d_021BC128[];
extern const Tile d_021BC150[];

/* sdk/type_icons.h */
extern int bw_types_added(void);
extern void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                u16 type_rows);
extern void bw_type_screen_pop(const void *owner);
extern void bw_type_screen_select(const void *owner);
extern u32 bw_type_label_member(u32 type);

u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field);
u32 APP_ICON_GetTouchBarFileId(void);
u32 APP_ICON_GetTypePaletteFileId(u32 mapping);
u32 APP_ICON_GetTypeAnimFileId(u32 mapping);
u32 APP_ICON_GetTypeCellFileId(u32 type);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
u32 GFL_HEAP_Free(void *block);
void *ARC_UTIL_LoadBgCharDataFromHandle(void *arc, u32 memberIdx, u32 heapID, void *unpackedOut, u16 flags);
s32 CLACT_RegisterCharacter(void *archive, u32 file_idx, u32 compressed, u32 vram_mode, u16 heap_id);
void CLACT_ReleaseCharacter(u32 idx);
void CLACT_TransferCharacterPart(int idx, void *src, u32 size, int offset, int target);
s32 CLACT_RegisterPaletteEx(void *archive, u32 file_idx, u32 vram_mode, u32 vram_offset, u16 first_color,
                            u16 color_count, u16 heap_id);
void CLACT_ReleasePalette(u32 idx);
s32 CLACT_RegisterCellAnim(void *archive, u32 cell_file_idx, u32 anim_file_idx, u32 heap_id);
void CLACT_ReleaseCellAnim(u32 idx);
struct ClactActor *CLACT_CreateActor(struct Unit *unit, u32 char_idx, u32 pltt_idx, u32 cell_idx,
                                     const void *init, u16 space, u16 heap_id);
void CLACT_DeleteActor(struct ClactActor *actor);
void CLACT_SetActorDrawEnable(struct ClactActor *actor, u32 value);
void CLACT_SetActorObjMode(struct ClactActor *actor, u32 value);
void CLACT_SetActorPaletteOffset(struct ClactActor *actor, u32 offset, u32 pltt_op);
void CLACT_SetActorDrawPriority(struct ClactActor *actor, u8 priority);
void ZUKAN_INFO_LayoutFormPair(ZukanInfo *p, unsigned x, unsigned y, int row);
void ZUKAN_INFO_DeleteRow(char *p, int index);
void ZUKAN_INFO_DisableOtherRows(ZukanInfo *p);
void ZUKAN_INFO_LayoutFormPairEx(ZukanInfo *p, unsigned x, unsigned y, int z, int row);

void ZUKAN_INFO_CreateIconActors(ZukanInfo *p)
{
    unsigned char a, b, i;
    int flag;
    int sh;
    int u;
    void *arc;
    ZukanInfoActorRect r;

    u = p->femaleFlag;
    flag = u == 0 ? B_FALSE : B_TRUE;
    sh = 0;
    if (u != 0) sh = 1;
    arc = p->w[3];
    for (a = 0; a < 2; a++) {
        for (b = 0; b < 2; b++) {
            p->charIds[a][b] = -1;
        }
    }
    p->iconEnabled = 0;
    p->paletteHandle = (u32)CLACT_RegisterPaletteEx(arc, APP_ICON_GetTouchBarFileId(), flag, 0x40, 0, 3, p->heap);
    p->cellAnimHandle = (u32)CLACT_RegisterCellAnim(arc, APP_ICON_GetTypePaletteFileId(2), APP_ICON_GetTypeAnimFileId(2), p->heap);
    /* the added types' row, on this page's screen */
    bw_type_screen_push(p, flag, 2, 7, 0, 0, 0);
    if (p->mode == 2 && p->femaleFlag == 0 && !bw_types_added()) {
        r.a = 0; r.b = 0; r.c = 0; r.d = 0; r.e = 0;
        p->iconVisible = 1;
        p->shownFormA = POKETYPE_NONE;
        p->shownFormB = POKETYPE_NONE;
        for (i = 0; i < 0x11; i++) {
            p->iconChars[i] = CLACT_RegisterCharacter(arc, APP_ICON_GetTypeCellFileId(i), 0, flag, p->heap);
            p->iconActors[i] = CLACT_CreateActor(p->clactUnit, p->iconChars[i], p->paletteHandle, p->cellAnimHandle, &r, sh, p->heap);
            CLACT_SetActorPaletteOffset(p->iconActors[i], APP_ICON_GetTypePaletteOffset(i), 1);
            CLACT_SetActorDrawPriority(p->iconActors[i], 2);
            CLACT_SetActorObjMode(p->iconActors[i], 1);
            CLACT_SetActorDrawEnable(p->iconActors[i], 0);
        }
    } else {
        p->iconVisible = 0;
    }
}

void ZUKAN_INFO_DeleteIconActors(char *p)
{
    unsigned char i;
    for(i=0;i<2;i++) ZUKAN_INFO_DeleteRow(p,i);
    if(((ZukanInfo *)p)->iconVisible) {
        for(i=0;i<17;i++) {
            CLACT_DeleteActor((struct ClactActor *)(((ZukanInfo *)p)->iconActors[i]));
            CLACT_ReleaseCharacter(((ZukanInfo *)p)->iconChars[i]);
        }
        ((ZukanInfo *)p)->iconVisible=0;
    }
    CLACT_ReleaseCellAnim(((ZukanInfo *)p)->cellAnimHandle);
    CLACT_ReleasePalette(((ZukanInfo *)p)->paletteHandle);
    bw_type_screen_pop(p);
}

void ZUKAN_INFO_ShowFormPair(char *p,int mode,int which)
{
    void *mon=((ZukanInfo *)p)->x;
    unsigned char first=POKE_PERSONAL_GetParam((struct PersonalData *)mon,6);
    unsigned char second=POKE_PERSONAL_GetParam((struct PersonalData *)mon,7);
    if(first==second) second=POKETYPE_NONE;
    bw_type_screen_select(p);
    if(mode==6) ZUKAN_INFO_LayoutFormPair((ZukanInfo *)p, first, second, which);
    else ZUKAN_INFO_LayoutFormPairEx((ZukanInfo *)p, first, second, mode, which);
    ZUKAN_INFO_DisableOtherRows((ZukanInfo *)p);
}

void ZUKAN_INFO_LayoutFormPairEx(ZukanInfo *p, unsigned x, unsigned y, int z, int row)
{
    ShortRect4 rr[2];
    unsigned char sel[2];
    void *arc;
    void *handle;
    void *labelHandle;
    unsigned char *data;
    unsigned char *label;
    int ta;
    int tb;
    unsigned char id;
    int ifv;
    int tern;
    short v;
    int j;
    short *zp;
    Blob *blob;
    Blob *labelBlob;
    u32 member;

    zp = (short *)rr;
    zp[0] = 0;
    zp[1] = 0;
    zp[2] = 0;
    zp[3] = 0;
    zp[4] = 0;
    zp[5] = 0;
    zp[6] = 0;
    zp[7] = 0;
    ifv = 0;
    if (p->femaleFlag != 0) ifv = 1;
    tern = p->femaleFlag == 0 ? B_FALSE : B_TRUE;
    sel[0] = x;
    sel[1] = y;
    rr[0].x = 0xa8;
    v = p->yOffset + 0x54;
    rr[0].y = v;
    rr[1].x = 0xd0;
    rr[1].y = v;
    arc = p->w[3];
    handle = ARC_UTIL_LoadBgCharDataFromHandle((void *)p->w[0], d_021BC128[z], 0, (void *)((&blob)), p->heap);
    data = (unsigned char *)blob->pRawData;
    for (j = 0; j < 2; j++) {
        p->charIds[row][j] = -1;
        id = sel[j];
        if (id != POKETYPE_NONE && id < GAME_TYPES) {
            p->charIds[row][j] = CLACT_RegisterCharacter(arc, APP_ICON_GetTypeCellFileId(id), 0, ifv, p->heap);
            ta = d_021BC150[id].b << 7;
            tb = d_021BC150[id].a << 11;
            CLACT_TransferCharacterPart((p->charIds[row][j]), (void *)(data + (tb + ta)), 0x80, 0, ifv);
            CLACT_TransferCharacterPart((p->charIds[row][j]), (void *)(data + ((tb + (1 << 10)) + ta)), 0x80, 0x80, ifv);
        } else if (id != POKETYPE_NONE) {
            /* an added type: its own labels, one 32x16 picture (8 tiles,
             * top row first) a language */
            p->charIds[row][j] = CLACT_RegisterCharacter(arc, APP_ICON_GetTypeCellFileId(id), 0, ifv, p->heap);
            member = bw_type_label_member(id);
            if (member != 0xFFFFFFFFu && z >= 0 && z < 6) {
                labelHandle = ARC_UTIL_LoadBgCharDataFromHandle((void *)p->w[0], member, 0, (void *)(&labelBlob), p->heap);
                label = (unsigned char *)labelBlob->pRawData + z * 0x100;
                CLACT_TransferCharacterPart((p->charIds[row][j]), (void *)label, 0x80, 0, ifv);
                CLACT_TransferCharacterPart((p->charIds[row][j]), (void *)(label + 0x80), 0x80, 0x80, ifv);
                GFL_HEAP_Free(labelHandle);
            }
        }
    }
    GFL_HEAP_Free(handle);
    for (j = 0; j < 2; j++) {
        if (p->charIds[row][j] != -1) {
            p->actorSlots[row][j] = CLACT_CreateActor(p->clactUnit, (u32)p->charIds[row][j], p->paletteHandle, p->cellAnimHandle, &rr[j], (unsigned short)tern, p->heap);
            CLACT_SetActorPaletteOffset(p->actorSlots[row][j], APP_ICON_GetTypePaletteOffset(sel[j]), 1);
            CLACT_SetActorDrawPriority(p->actorSlots[row][j], 2);
            CLACT_SetActorObjMode(p->actorSlots[row][j], 1);
        }
    }
}
