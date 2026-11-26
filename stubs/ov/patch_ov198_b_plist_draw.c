/*
 * patches: src/game/ov198/b_plist_draw.c B_PLIST_DRAW_CreateActors B_PLIST_DRAW_DeleteActors
 *
 * The battle's party list shows the types mods add (sdk/type_icons.c).
 *
 * The list asks for each type icon it shows by its type
 * (B_PLIST_DRAW_TransferCharacter: APP_ICON_GetTypeCellFileId,
 * APP_ICON_GetTypePaletteOffset), so an added type only needs the list's
 * place for its colours.  The type palette is the whole file at sub OBJ
 * row 5; the gear icon's palette, the whole file at row 9, comes after it,
 * so rows 10..15 hold no picture of the list, and row 12 (the type
 * palette's row 7, palette offset 7) is the added types' row.  Its colours
 * go to the list's palette animator too (bank 3, which the list fills
 * from the hardware's rows 0..14 when its actors are made), and its fades
 * over the three type rows 5..7 take row 12 with them.
 * B_PLIST_DRAW_CreateActors gives the list that row once the animator holds
 * the list's rows, and B_PLIST_DRAW_DeleteActors takes it back.  The rest is
 * the game's own.
 *
 * symbols:
 *   PALANM_CopyHwPaletteToSlot 0x020216D4 thumb
 *   GFL_NET_RefreshWmIconAtTopRight 0x0203D34C arm
 *   CLACT_ReleaseCharacter 0x0204A8D4 arm
 *   CLACT_ReleasePalette 0x0204ADA4 arm
 *   CLACT_ReleaseCellAnim 0x0204AFD8 arm
 *   CLACT_CreateUnit 0x0204B100 arm
 *   CLACT_DeleteUnit 0x0204B1CC arm
 *   CLACT_DeleteActor 0x0204B3B4 arm
 *   B_APP_TOOL_DeleteActors 0x0220AFC4 thumb ov196
 *   B_PLIST_DRAW_CreateActorFromParams 0x0220E684 thumb ov198
 *   B_PLIST_DRAW_ApplyActorPalettes 0x0220E7E4 thumb ov198
 *   d_02211DFC 0x02211DFC data ov198
 *
 * The game's own records
 * ----------------------
 * B_PlistWork (src/game/ov198/b_plist_types.h), as far as these read it:
 * owner 0x0000 (its heapId at 0x10), palAnm 0x01D8, unit 0x1EB8, actor 0x1EBC
 * (41), appTool 0x2188, charId 0x24A4 (26), palId 0x250C (6), cellId 0x2524
 * (6).
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct PaletteFade;

typedef struct B_PlistHost {
    u8 pad_00[0x10];
    u16 heapId;                 /* 0x10 */
} B_PlistHost;

typedef struct B_PlistWork {
    B_PlistHost *owner;         /* 0x0000 */
    u8 pad_0004[0x1D8 - 0x04];
    void *palAnm;               /* 0x01D8 */
    u8 pad_01dc[0x1EB8 - 0x1DC];
    void *unit;                 /* 0x1EB8 */
    void *actor[41];            /* 0x1EBC */
    u8 pad_1f60[0x2188 - 0x1F60];
    void *appTool;              /* 0x2188 */
    u8 pad_218c[0x24A4 - 0x218C];
    int charId[26];             /* 0x24A4 */
    int palId[6];               /* 0x250C */
    int cellId[6];              /* 0x2524 */
} B_PlistWork;

typedef char plist_palanm_at_0x1d8[sizeof(void *) != 4 || __builtin_offsetof(B_PlistWork, palAnm) == 0x1D8 ? 1 : -1];
typedef char plist_unit_at_0x1eb8[sizeof(void *) != 4 || __builtin_offsetof(B_PlistWork, unit) == 0x1EB8 ? 1 : -1];
typedef char plist_tool_at_0x2188[sizeof(void *) != 4 || __builtin_offsetof(B_PlistWork, appTool) == 0x2188 ? 1 : -1];
typedef char plist_cell_at_0x2524[sizeof(void *) != 4 || __builtin_offsetof(B_PlistWork, cellId) == 0x2524 ? 1 : -1];

extern const int d_02211DFC[][5];

/* sdk/type_icons.h */
extern void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                u16 type_rows);
extern void bw_type_screen_pop(const void *owner);

void PALANM_CopyHwPaletteToSlot(struct PaletteFade *fade, u32 bank, u32 dstOffset, u32 size);
void GFL_NET_RefreshWmIconAtTopRight(u32 restartParamA, u32 restartParamB);
void CLACT_ReleaseCharacter(u32 idx);
void CLACT_ReleasePalette(u32 idx);
void CLACT_ReleaseCellAnim(u32 idx);
void *CLACT_CreateUnit(int count, u8 priority, u32 heapId);
u32 CLACT_DeleteUnit(void *unit);
void CLACT_DeleteActor(void *actor);
void B_APP_TOOL_DeleteActors(void *tool);
void *B_PLIST_DRAW_CreateActorFromParams(B_PlistWork *p, const int *params);
void B_PLIST_DRAW_ApplyActorPalettes(B_PlistWork *p);

void B_PLIST_DRAW_CreateActors(B_PlistWork *p)
{
    unsigned i;
    p->unit=(void *)CLACT_CreateUnit((unsigned)45,(unsigned)0,(u16)((p->owner->heapId&0x7fff)|0x8000));
    for(i=0;i<41;i++)
p->actor[i]=B_PLIST_DRAW_CreateActorFromParams(p,d_02211DFC[i]);
    GFL_NET_RefreshWmIconAtTopRight(0,p->owner->heapId);
    PALANM_CopyHwPaletteToSlot((struct PaletteFade *)p->palAnm,3,0,0x1e0);
    /* the added types' row: the type palette's row 7 (sub row 12) */
    bw_type_screen_push(p, 1, 5, 12, p->palAnm, 3, (u16)(7u << 5));
    B_PLIST_DRAW_ApplyActorPalettes(p);
}

void B_PLIST_DRAW_DeleteActors(B_PlistWork *p)
{
    unsigned i;
    bw_type_screen_pop(p);
    for(i=0;i<41;i++) CLACT_DeleteActor(p->actor[i]);
    B_APP_TOOL_DeleteActors(p->appTool);
    for(i=0;i<26;i++) CLACT_ReleaseCharacter((p->charId[i]));
    for(i=0;i<6;i++) CLACT_ReleasePalette((p->palId[i]));
    for(i=0;i<6;i++) CLACT_ReleaseCellAnim((p->cellId[i]));
    CLACT_DeleteUnit(p->unit);
}
