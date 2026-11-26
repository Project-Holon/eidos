/*
 * patches: src/game/ov173/poke_summary_actor.c POKE_SUMMARY_ACTOR_InitSystem POKE_SUMMARY_ACTOR_DeleteSystem POKE_SUMMARY_ACTOR_SetMoveIcon POKE_SUMMARY_ACTOR_FlushPendingTransfers POKE_SUMMARY_ACTOR_LayoutMoveActors
 *
 * The move list summary (ov173) shows the types mods add
 * (sdk/type_icons.c).  It shows move types on both screens.
 *
 * Main screen: four icons made by their type (POKE_SUMMARY_ACTOR_SetMoveIcon
 * takes the palette offset, POKE_SUMMARY_ACTOR_FlushPendingTransfers the
 * tiles, both by APP_ICON_*).  The type palette is the whole file at main OBJ
 * row 0, and the Pokemon's palette (row 4) is the only later main load, so
 * row 7 holds no picture and is the added types' row (palette offset 7).
 * The two functions select the main screen before they ask for an icon.
 *
 * Sub screen: four actors share one sheet of 18 type icons (a/1/2/4 member
 * 23) and pick theirs by animation, sequence = type, with the palette row in
 * each cell; a type past 17 runs past the sheet's 18 sequences.  For an added
 * type POKE_SUMMARY_ACTOR_LayoutMoveActors gives the actor a character of
 * the type's own icon (bw_type_icon_char), sequence 0 (Normal's cell, row 0
 * of the type palette, tile 0) and the added types' palette offset; the
 * type palette's rows 0..2 are at sub OBJ row 6 here and rows 9..15 hold no
 * picture, so sub row 13 is the added types' row (offset 7).  A game type
 * gives the actor back the sheet and offset 0.
 *
 * The screen's cell actor system has room for its own nine characters and
 * no more (d_021B820C n_chars 9), so with added types
 * POKE_SUMMARY_ACTOR_InitSystem asks for four more, one a sub icon.
 * POKE_SUMMARY_ACTOR_DeleteSystem frees the added types' characters before
 * CLACT_ExitSystem frees the rest, and leaves both rows.  The rest is the
 * game's own.
 *
 * symbols:
 *   WAZA_TOOL_GetParam 0x0201BD28 thumb
 *   BG_SYS_SetMainPlaneVisible 0x020434DC arm
 *   BG_SYS_SetSubPlaneVisible 0x02043598 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   CLACT_InitSystem 0x0204A48C arm
 *   CLACT_CreateUnit 0x0204B100 arm
 *   CLACT_SetUnitDefaultRenderer 0x0204B270 arm
 *   d_021B820C 0x021B820C data ov173
 *   d_021B8250 0x021B8250 data ov173
 *   CLACT_ExitSystem 0x0204A5AC arm
 *   CLACT_GetCharacterProxy 0x0204AB84 arm
 *   CLACT_DeleteUnit 0x0204B1CC arm
 *   CLACT_DeleteActor 0x0204B3B4 arm
 *   CLACT_SetActorDrawEnable 0x0204B3DC arm
 *   CLACT_AddActorSurfacePos 0x0204B404 arm
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   CLACT_SetActorImageProxy 0x0204B784 arm
 *   CLACT_SetActorAnimSeq 0x0204B878 arm
 *   POKE_SUMMARY_ACTOR_TransferCharacter 0x021B6F2C thumb ov173
 *
 * The game's own records
 * ----------------------
 * PokeSummaryWork (src/game/ov173/poke_summary_types.h), as far as these
 * read it: mon 0x000 (moveList 0x10, scrollIndex 0x16), reg 0x070
 * (charHandles 0x70: 3 the sheet, 4..7 the main icons), unit 0x100, actors
 * 0x104 (14: 4..7 the sub icons, 8..11 the main ones), resourceHandle 0x13C,
 * transfers 0x140 (8 of {param, flag}), moveCount 0x1B8.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define GAME_TYPES 17
#define POKE_SUMMARY_MOVE_ICON_X 0x2f   /* (outside Japan) */
#define SUMMARY_HEAP 79

struct ClactActor;
struct ClactVec2;
struct ImageProxy;

typedef struct ImageProxyWords {
    u32 word[9];
} ImageProxyWords;

typedef struct PokeSummaryMon {
    void *party;                /* 0x00 */
    void *myStatus;             /* 0x04 */
    u32 config;                 /* 0x08 */
    void *gameSystem;           /* 0x0C */
    u16 *moveList;              /* 0x10 */
    u16 selectedIndex;          /* 0x14 */
    u16 scrollIndex;            /* 0x16 */
} PokeSummaryMon;

typedef struct PokeSummaryTransfer {
    u32 param;
    u32 flag;
} PokeSummaryTransfer;

typedef struct PokeSummaryWork {
    PokeSummaryMon *mon;                /* 0x000 */
    u8 pad_004[0x70 - 0x04];
    u32 reg[24];                        /* 0x070 */
    u8 pad_0d0[0x100 - 0xD0];
    void *unit;                         /* 0x100 */
    struct ClactActor *actors[14];      /* 0x104 */
    void *resourceHandle;               /* 0x13C */
    PokeSummaryTransfer transfers[8];   /* 0x140 */
    u8 pad_180[0x1B8 - 0x180];
    u8 moveCount;                       /* 0x1B8 */
} PokeSummaryWork;

typedef char summary_actors_at_0x104[sizeof(void *) != 4 || __builtin_offsetof(PokeSummaryWork, actors) == 0x104 ? 1 : -1];
typedef char summary_transfers_at_0x140[sizeof(void *) != 4 || __builtin_offsetof(PokeSummaryWork, transfers) == 0x140 ? 1 : -1];
typedef char summary_count_at_0x1b8[sizeof(void *) != 4 || __builtin_offsetof(PokeSummaryWork, moveCount) == 0x1B8 ? 1 : -1];

/* sdk/type_icons.h */
extern void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                  u16 type_rows);
extern void bw_type_screen_select(const void *owner);
extern void bw_type_screen_pop(const void *owner);
extern int bw_type_icon_char(const void *owner, u32 type, u16 heap, u32 vram);
extern void bw_type_icon_chars_release(const void *owner);
extern int bw_types_added(void);

/* arm9 clact.h */
struct ClactSysInit {
    short surface_pos[4];
    u8 oam_main_base;
    u8 oam_main_count;
    u8 oam_sub_base;
    u8 oam_sub_count;
    u32 n_transfers;
    u16 n_chars;
    u16 n_pltts;
    u16 n_cells;
    u16 n_spare;
    u16 n_main_blocks;
    u16 n_sub_blocks;
};
struct DispVramCfg;

extern const struct ClactSysInit d_021B820C;
extern u8 d_021B8250[];

void CLACT_InitSystem(const struct ClactSysInit *cfg, const struct DispVramCfg *res_cfg, u32 heapId);
void *CLACT_CreateUnit(int count, u8 priority, u32 heapId);
void CLACT_SetUnitDefaultRenderer(void *unit);
void BG_SYS_SetMainPlaneVisible(u32 mask, u32 enable);
void BG_SYS_SetSubPlaneVisible(u32 mask, u32 enable);
u32 WAZA_TOOL_GetParam(u32 move, u32 param);
void ARC_TOOL_DeleteHandle(void *handle);
void CLACT_ExitSystem(void);
void CLACT_GetCharacterProxy(u32 idx, struct ImageProxy *out);
u32 CLACT_DeleteUnit(void *unit);
void CLACT_DeleteActor(struct ClactActor *actor);
void CLACT_SetActorDrawEnable(struct ClactActor *actor, u32 value);
void CLACT_AddActorSurfacePos(struct ClactActor *actor, const struct ClactVec2 *delta, u32 which);
void CLACT_SetActorPaletteOffset(struct ClactActor *actor, u32 offset, u32 pltt_op);
void CLACT_SetActorImageProxy(char *actor_base, const struct ImageProxy *proxy);
void CLACT_SetActorAnimSeq(struct ClactActor *actor, u16 value);
u32 APP_ICON_GetTypeCellFileId(u32 type);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
void POKE_SUMMARY_ACTOR_TransferCharacter(void *archiveHandle, int charId, u32 memberIdx, u32 heapID, u16 heap);

/* The main screen's and the sub screen's owners, one summary at a time. */
static const char g_main, g_sub;
static u8 g_own[4];             /* a sub actor shows an added type's own character */

static void select_main(void)
{
    bw_type_screen_ensure(&g_main, 0, 0, 7, 0, 0, 0);
    bw_type_screen_select(&g_main);
}

void POKE_SUMMARY_ACTOR_InitSystem(u8 *work, void *archiveHandle)
{
    struct ClactSysInit cfg = d_021B820C;
    void *b;
    (void)archiveHandle;
    if (bw_types_added()) {
        cfg.n_chars += 4;     /* (the sub icons of added types) */
    }
    CLACT_InitSystem(&cfg, (const struct DispVramCfg *)d_021B8250, SUMMARY_HEAP);
    b = CLACT_CreateUnit(14, 0, SUMMARY_HEAP);
    ((PokeSummaryWork *)work)->unit = b;
    CLACT_SetUnitDefaultRenderer(b);
    BG_SYS_SetMainPlaneVisible(16, 1);
    BG_SYS_SetSubPlaneVisible(16, 1);
}

void POKE_SUMMARY_ACTOR_DeleteSystem(u8 *work)
{
    u32 i;
    bw_type_icon_chars_release(&g_sub);
    bw_type_screen_pop(&g_sub);
    bw_type_screen_pop(&g_main);
    for (i = 0; i < 4; i++) g_own[i] = 0;
    for (i = 0; i < 14; i++) CLACT_DeleteActor(((PokeSummaryWork *)work)->actors[i]);
    CLACT_DeleteUnit(*(void * *)&((PokeSummaryWork *)work)->unit);
    CLACT_ExitSystem();
    ARC_TOOL_DeleteHandle(((PokeSummaryWork *)work)->resourceHandle);
}

void POKE_SUMMARY_ACTOR_SetMoveIcon(u8 *work, u16 id, u16 i) {u32 n;n=WAZA_TOOL_GetParam(id,0);if(n>=GAME_TYPES)select_main();((PokeSummaryWork *)work)->transfers[i].param=n;((PokeSummaryWork *)work)->transfers[i].flag=1;CLACT_SetActorPaletteOffset((struct ClactActor *)((struct ClactActor *)((PokeSummaryWork *)work)->actors[i+8]),APP_ICON_GetTypePaletteOffset((u8)n),1);}

void POKE_SUMMARY_ACTOR_FlushPendingTransfers(char *work)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (((PokeSummaryWork *)work)->transfers[i].flag != 0) {
            if (((PokeSummaryWork *)work)->transfers[i].param >= GAME_TYPES) select_main();
            POKE_SUMMARY_ACTOR_TransferCharacter(((PokeSummaryWork *)work)->resourceHandle, (int)(((PokeSummaryWork *)work)->reg[4+i]), APP_ICON_GetTypeCellFileId((u8)((PokeSummaryWork *)work)->transfers[i].param), 0, 0x4f);
            ((PokeSummaryWork *)work)->transfers[i].flag = 0;
        }
    }
}

void POKE_SUMMARY_ACTOR_LayoutMoveActors(char *work)
{
    u16 rect[2];
    u32 i;
    u32 value;
    ImageProxyWords proxy;
    int c;
    for (i = 0; i < 4; i++) {
        rect[0] = POKE_SUMMARY_MOVE_ICON_X;
        rect[1] = i * 24 + 0x50;
        CLACT_AddActorSurfacePos(((PokeSummaryWork *)work)->actors[4+i], (struct ClactVec2 *)rect, 1);
        if (((PokeSummaryWork *)work)->mon->scrollIndex + i >= ((PokeSummaryWork *)work)->moveCount) {
            CLACT_SetActorDrawEnable(((PokeSummaryWork *)work)->actors[4+i], 0);
        } else {
            value = WAZA_TOOL_GetParam(*(u16 *)(*(char * *)&((PokeSummaryWork *)work)->mon->moveList + (((PokeSummaryWork *)work)->mon->scrollIndex + i) * 2), 0);
            CLACT_SetActorDrawEnable(((PokeSummaryWork *)work)->actors[4+i], 1);
            if (value >= GAME_TYPES) {
                /* an added type: its own character on Normal's cell */
                bw_type_screen_ensure(&g_sub, 1, 6, 13, 0, 0, 0);
                bw_type_screen_select(&g_sub);
                c = bw_type_icon_char(&g_sub, value, SUMMARY_HEAP, 1);
                if (c >= 0) {
                    CLACT_GetCharacterProxy((u32)c, (struct ImageProxy *)&proxy);
                    CLACT_SetActorImageProxy((char *)((PokeSummaryWork *)work)->actors[4+i], (const struct ImageProxy *)&proxy);
                    CLACT_SetActorPaletteOffset(((PokeSummaryWork *)work)->actors[4+i], APP_ICON_GetTypePaletteOffset((int)value), 1);
                    g_own[i] = 1;
                }
                value = 0;
            } else if (g_own[i]) {
                CLACT_GetCharacterProxy(((PokeSummaryWork *)work)->reg[3], (struct ImageProxy *)&proxy);
                CLACT_SetActorImageProxy((char *)((PokeSummaryWork *)work)->actors[4+i], (const struct ImageProxy *)&proxy);
                CLACT_SetActorPaletteOffset(((PokeSummaryWork *)work)->actors[4+i], 0, 1);
                g_own[i] = 0;
            }
            CLACT_SetActorAnimSeq(((PokeSummaryWork *)work)->actors[4+i], value);
        }
    }
}
