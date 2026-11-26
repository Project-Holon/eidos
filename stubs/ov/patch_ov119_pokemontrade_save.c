/*
 * patches: src/game/ov119/pokemontrade_save.c POKETRADE_SAVE_CreateTypeIconActor POKETRADE_SAVE_DeleteTypeIcons
 *
 * The trade screen's summary shows the types mods add (sdk/type_icons.c).
 *
 * Each type icon is made by its type (APP_ICON_GetTypeCellFileId,
 * APP_ICON_GetTypePaletteOffset) with the type palette's rows 0..2 at OBJ
 * row 8 of its screen; the status icon takes row 12, the summary icon row
 * 13, so row 15 (palette offset 7) is the added types' row.  The trade has
 * no palette animator on that screen.  POKETRADE_SAVE_CreateTypeIconActor
 * gives the trade that row before it asks for the icon, and
 * POKETRADE_SAVE_DeleteTypeIcons takes it back.  The rest is the game's own.
 *
 * symbols:
 *   POKE_TOOL_GetPartyValue 0x02017E1C thumb
 *   APP_ICON_GetArchiveId 0x020275E4 thumb
 *   APP_ICON_GetTouchBarFileId 0x020275E8 thumb
 *   APP_ICON_GetTypePaletteFileId 0x020275FC thumb
 *   APP_ICON_GetTypeAnimFileId 0x02027600 thumb
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   UI_SCENE_LoadActorGfx 0x021B6208 thumb ov067
 *   UI_SCENE_CreateActor 0x021B629C thumb ov067
 *   POKETRADE_SAVE_DeleteTypeIconActor 0x021E0DA0 thumb ov119
 *
 * The game's own records
 * ----------------------
 * TradeWork (src/game/ov119/pokemontrade_types.h): typeIcon 0x900 (4 of
 * 0x14 bytes).  TradeIconSlot: gfx 0x00 (ActorGfxSet, 0x10), actor 0x10.
 * ActorGfxDesc (src/game/ov067/ui_scene_types.h).
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct ClactActor;
struct Unit;

typedef struct {
    int plttHandle;
    int charHandle;
    int animHandle;
    int screen;
} ActorGfxSet;

typedef struct {
    int screen;
    u32 flags;
    int arcId;
    int charId;
    int plttId;
    int cellId;
    int animId;
    u8 plttSlot;
    u8 charBase;
    u8 charSize;
} ActorGfxDesc;

typedef struct TradeIconSlot {
    ActorGfxSet gfx;            /* 0x00 */
    struct ClactActor *actor;   /* 0x10 */
} TradeIconSlot;

typedef struct TradeWork {
    u8 pad_000[0x900];
    TradeIconSlot typeIcon[4];  /* 0x900 */
} TradeWork;

typedef char trade_icons_at_0x900[sizeof(void *) != 4 || __builtin_offsetof(TradeWork, typeIcon) == 0x900 ? 1 : -1];
typedef char trade_slot_is_0x14[sizeof(void *) != 4 || sizeof(TradeIconSlot) == 0x14 ? 1 : -1];

/* sdk/type_icons.h */
extern void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                  u16 type_rows);
extern void bw_type_screen_pop(const void *owner);

u32 POKE_TOOL_GetPartyValue(void *pp, u32 paramId, void *arg);
u32 APP_ICON_GetArchiveId(void);
u32 APP_ICON_GetTouchBarFileId(void);
u32 APP_ICON_GetTypeCellFileId(u32 type);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
u32 APP_ICON_GetTypePaletteFileId(u32 mapping);
u32 APP_ICON_GetTypeAnimFileId(u32 mapping);
void CLACT_SetActorPaletteOffset(struct ClactActor *actor, u32 offset, u32 pltt_op);
void UI_SCENE_LoadActorGfx(ActorGfxSet *out, ActorGfxDesc *desc, void *actorSys, u16 heapId);
void *UI_SCENE_CreateActor(ActorGfxSet *gfx, void *actorSys, u16 x, u16 y, u8 animIndex, u16 heapId);
void POKETRADE_SAVE_DeleteTypeIconActor(TradeIconSlot *p);

/* the trade's type icons, one screen at a time */
static const char g_trade_icons;

void POKETRADE_SAVE_CreateTypeIconActor(TradeIconSlot *p,void *mon,int index,struct Unit *slot,int x,int y,int a,u16 b) {ActorGfxDesc d;u8 v;int z=0;bw_type_screen_ensure(&g_trade_icons,a,8,15,0,0,0);v=POKE_TOOL_GetPartyValue(mon,index+174,0);d.screen=a;d.flags=z;d.arcId=APP_ICON_GetArchiveId();d.charId=APP_ICON_GetTouchBarFileId();d.plttId=APP_ICON_GetTypeCellFileId(v);d.cellId=APP_ICON_GetTypePaletteFileId(2);d.animId=APP_ICON_GetTypeAnimFileId(2);d.plttSlot=8;d.charBase=z;d.charSize=3;UI_SCENE_LoadActorGfx(&p->gfx,&d,(void *)(slot),b);p->actor=(struct ClactActor *)UI_SCENE_CreateActor( &p->gfx, (void *)(slot), (u8)x, (u8)y,z,b);CLACT_SetActorPaletteOffset((struct ClactActor *)(p->actor),APP_ICON_GetTypePaletteOffset(v),1);}

void POKETRADE_SAVE_DeleteTypeIcons(TradeWork *p)
{
    TradeIconSlot *q;
    int i = 0;
    q = p->typeIcon;
    for (; i < 4; i++) {
        POKETRADE_SAVE_DeleteTypeIconActor(q + i);
    }
    bw_type_screen_pop(&g_trade_icons);
}
