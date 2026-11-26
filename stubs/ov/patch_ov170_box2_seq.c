/*
 * patches: src/game/ov170/box2_seq.c BOX2_SEQ_LoadBoxScreen BOX2_SEQ_ShowHandSlot BOX2_SEQ_ReleaseBoxGraphics
 *
 * The PC box's status panel shows the types mods add (sdk/type_icons.c).
 *
 * The box makes one type icon actor a type (actors 0x1C..0x2C, characters
 * 40..56) and shows a Pokemon's types as actors[type + 0x1C]; a type past 16
 * would show the party's actors that follow (0x2D..).  These copies show an
 * added type on the actor of a game type that is not on the panel instead
 * (from Dark down), with a character of the added type's own
 * (bw_type_icon_char) and the added types' palette offset, until that game
 * type is shown again, which gives the actor its own image back.  The
 * box's own loops over the 17 actors hide them with the rest.
 *
 * The type palette is the whole file at sub OBJ row 0 here, and the
 * palettes loaded after it (rows 5 and 6, whole files too) leave row 7 to no
 * picture of this screen, so row 7 is the added types' row (palette offset
 * 7); the box has no palette animator on the sub screen.  The colours are
 * written when an icon is shown, after those loads.
 * BOX2_SEQ_LoadBoxScreen starts the row over (its palettes were loaded
 * again) and BOX2_SEQ_ReleaseBoxGraphics, which runs once the box's actors
 * are gone (BOX2_SEQ_DeleteBox2UiScene), forgets the borrowed actors, frees
 * the added types' characters and leaves the row.  The rest is the game's
 * own.
 *
 * symbols:
 *   APP_ICON_GetArchiveId 0x020275E4 thumb
 *   APP_ICON_GetTouchBarFileId 0x020275E8 thumb
 *   APP_ICON_GetTypePaletteFileId 0x020275FC thumb
 *   APP_ICON_GetTypeAnimFileId 0x02027600 thumb
 *   APP_ICON_GetBallOpenAnimFileId 0x02027748 thumb
 *   APP_ICON_GetEffectCellFileId 0x0202774C thumb
 *   APP_ICON_GetEffectPaletteFileId 0x02027750 thumb
 *   APP_ICON_GetEffectAnimFileId 0x02027754 thumb
 *   APP_ICON_GetGearDataFileId 0x02027768 thumb
 *   APP_ICON_GetGearExtraFileId 0x0202776C thumb
 *   APP_ICON_GetGearFrameFileId 0x02027770 thumb
 *   APP_ICON_GetGearAnim2FileId 0x02027774 thumb
 *   ARC_TOOL_CreateHandle 0x020490F4 arm
 *   ARC_TOOL_DeleteHandle 0x02049238 arm
 *   CLACT_RegisterCharacter 0x0204A6C8 arm
 *   CLACT_ReleaseCharacter 0x0204A8D4 arm
 *   CLACT_GetCharacterProxy 0x0204AB84 arm
 *   CLACT_RegisterPalette 0x0204ABF0 arm
 *   CLACT_ReleasePalette 0x0204ADA4 arm
 *   CLACT_RegisterCellAnim 0x0204AF28 arm
 *   CLACT_ReleaseCellAnim 0x0204AFD8 arm
 *   CLACT_SetActorPaletteOffset 0x0204B6F4 arm
 *   CLACT_SetActorImageProxy 0x0204B784 arm
 *   BOX2_SEQ_SetActorDrawEnable 0x021EC000 thumb ov170
 *   BOX2_SEQ_AddActorSurfacePos 0x021EC08C thumb ov170
 *
 * The game's own record
 * ---------------------
 * BoxWork (src/game/ov170/box2_types.h), as far as these read it: actors
 * 0x9E74 (0x60), charRes 0x9FF4 (0x45), palRes 0xA108 (0xB), cellRes 0xA134
 * (0xC).
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#define GAME_TYPES 17
#define TYPE_ACTOR 0x1C     /* actors[TYPE_ACTOR + type] */
#define TYPE_CHAR 40        /* charRes[TYPE_CHAR + type] */
#define BOX_HEAP 0x4C

struct ClactActor;
struct ImageProxy;

typedef struct BoxWork {
    u8 pad_0000[0x9E74];
    struct ClactActor *actors[0x60];    /* 0x9E74 */
    int charRes[0x45];                  /* 0x9FF4 */
    int palRes[0xB];                    /* 0xA108 */
    int cellRes[0xC];                   /* 0xA134 */
} BoxWork;

typedef char box_actors_at_0x9e74[sizeof(void *) != 4 || __builtin_offsetof(BoxWork, actors) == 0x9E74 ? 1 : -1];
typedef char box_char_at_0x9ff4[sizeof(void *) != 4 || __builtin_offsetof(BoxWork, charRes) == 0x9FF4 ? 1 : -1];
typedef char box_cell_at_0xa134[sizeof(void *) != 4 || __builtin_offsetof(BoxWork, cellRes) == 0xA134 ? 1 : -1];

typedef struct ImageProxyWords {
    u32 word[9];
} ImageProxyWords;

/* sdk/type_icons.h */
extern void bw_type_screen_push(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                u16 type_rows);
extern void bw_type_screen_ensure(const void *owner, int engine, int base, int row, void *palanm, int bank,
                                  u16 type_rows);
extern void bw_type_screen_pop(const void *owner);
extern int bw_type_icon_char(const void *owner, u32 type, u16 heap, u32 vram);
extern void bw_type_icon_chars_release(const void *owner);

void BOX2_SEQ_SetActorDrawEnable(char *work, int i, int v);
void BOX2_SEQ_AddActorSurfacePos(char *work, int i, int a, int b, unsigned short c);
void CLACT_GetCharacterProxy(u32 idx, struct ImageProxy *out);
void CLACT_SetActorImageProxy(char *actor_base, const struct ImageProxy *proxy);
void CLACT_SetActorPaletteOffset(struct ClactActor *actor, u32 offset, u32 pltt_op);
void CLACT_ReleaseCharacter(u32 idx);
void CLACT_ReleasePalette(u32 idx);
void CLACT_ReleaseCellAnim(u32 idx);
void *ARC_TOOL_CreateHandle(u32 archiveID, unsigned int heapID);
void ARC_TOOL_DeleteHandle(void *handle);
s32 CLACT_RegisterCharacter(void *archive, u32 file_idx, u32 compressed, u32 vram_mode, u16 heap_id);
s32 CLACT_RegisterCellAnim(void *archive, u32 cell_file_idx, u32 anim_file_idx, u32 heap_id);
s32 CLACT_RegisterPalette(void *archive, u32 file_idx, u32 vram_mode, u32 vram_offset, u16 heap_id);
u32 APP_ICON_GetArchiveId(void);
u32 APP_ICON_GetTypeCellFileId(u32 type);
unsigned char APP_ICON_GetTypePaletteOffset(int type);
u32 APP_ICON_GetTypePaletteFileId(u32 mapping);
u32 APP_ICON_GetTypeAnimFileId(u32 mapping);
u32 APP_ICON_GetTouchBarFileId(void);
int APP_ICON_GetEffectCellFileId(int mapping);
int APP_ICON_GetEffectPaletteFileId(int mapping);
int APP_ICON_GetEffectAnimFileId(int mapping);
int APP_ICON_GetBallOpenAnimFileId(void);
int APP_ICON_GetGearExtraFileId(int mapping);
int APP_ICON_GetGearFrameFileId(int mapping);
int APP_ICON_GetGearAnim2FileId(int mapping);
int APP_ICON_GetGearDataFileId(void);

/* ------------------------------------------------------------------ */
/* Added types on borrowed actors                                      */
/* ------------------------------------------------------------------ */

static char *g_box;                 /* the box the borrowed actors are of   */
static u8 g_borrowed[GAME_TYPES];   /* the added type a game type's actor shows, 0: its own */

static void give_back(char *work, int t)
{
    ImageProxyWords proxy;
    BoxWork *w = (BoxWork *)work;

    CLACT_GetCharacterProxy((u32)w->charRes[TYPE_CHAR + t], (struct ImageProxy *)&proxy);
    CLACT_SetActorImageProxy((char *)w->actors[TYPE_ACTOR + t], (const struct ImageProxy *)&proxy);
    CLACT_SetActorPaletteOffset(w->actors[TYPE_ACTOR + t], APP_ICON_GetTypePaletteOffset(t), 1);
    g_borrowed[t] = 0;
}

/* The actor (index into actors) that shows `type`; `other` is the panel's
 * other type. */
static int type_actor(char *work, int type, int other)
{
    BoxWork *w = (BoxWork *)work;
    ImageProxyWords proxy;
    int t, pick = -1, c;

    if (g_box != work) {
        g_box = work;
        for (t = 0; t < GAME_TYPES; t++) {
            g_borrowed[t] = 0;
        }
    }
    if (type < GAME_TYPES) {
        if (g_borrowed[type] != 0) {
            give_back(work, type);
        }
        return TYPE_ACTOR + type;
    }
    for (t = GAME_TYPES - 1; t >= 0 && pick < 0; t--) {
        if (g_borrowed[t] == type) {
            pick = t;
        }
    }
    for (t = GAME_TYPES - 1; t >= 0 && pick < 0; t--) {
        if (g_borrowed[t] == 0 && t != other) {
            pick = t;
        }
    }
    bw_type_screen_ensure(work, 1, 0, 7, 0, 0, 0);
    c = bw_type_icon_char(work, (u32)type, BOX_HEAP, 1);
    if (c < 0) {
        return TYPE_ACTOR;      /* (no icon for it: Normal's actor) */
    }
    CLACT_GetCharacterProxy((u32)c, (struct ImageProxy *)&proxy);
    CLACT_SetActorImageProxy((char *)w->actors[TYPE_ACTOR + pick], (const struct ImageProxy *)&proxy);
    CLACT_SetActorPaletteOffset(w->actors[TYPE_ACTOR + pick], APP_ICON_GetTypePaletteOffset(type), 1);
    g_borrowed[pick] = (u8)type;
    return TYPE_ACTOR + pick;
}

/* ------------------------------------------------------------------ */
/* The game's functions                                                */
/* ------------------------------------------------------------------ */

void BOX2_SEQ_LoadBoxScreen(char *w)
{
    void *h;
    unsigned i;

    h = ARC_TOOL_CreateHandle(APP_ICON_GetArchiveId(), 0x804C);
    for (i = 0; i < 0x11; i++)
        ((BoxWork *)w)->charRes[i + 40] = CLACT_RegisterCharacter(h, APP_ICON_GetTypeCellFileId((unsigned char)i), (int)0, (int)1, (unsigned)0x4C);
    ((BoxWork *)w)->cellRes[4] = CLACT_RegisterCellAnim(h, APP_ICON_GetTypePaletteFileId(2), APP_ICON_GetTypeAnimFileId(2), 0x4C);
    ((BoxWork *)w)->palRes[4] = CLACT_RegisterPalette(h, APP_ICON_GetTouchBarFileId(), 1, 0, 0x4C);
    ((BoxWork *)w)->charRes[67] = CLACT_RegisterCharacter(h, APP_ICON_GetEffectCellFileId(2), (int)0, (int)1, (unsigned)0x4C);
    ((BoxWork *)w)->cellRes[10] = CLACT_RegisterCellAnim(h, APP_ICON_GetEffectPaletteFileId(2), APP_ICON_GetEffectAnimFileId(2), 0x4C);
    ((BoxWork *)w)->palRes[9] = CLACT_RegisterPalette(h, APP_ICON_GetBallOpenAnimFileId(), 1, 0xA0, 0x4C);
    ((BoxWork *)w)->charRes[68] = CLACT_RegisterCharacter(h, APP_ICON_GetGearExtraFileId(2), (int)0, (int)1, (unsigned)0x4C);
    ((BoxWork *)w)->cellRes[11] = CLACT_RegisterCellAnim(h, APP_ICON_GetGearFrameFileId(2), APP_ICON_GetGearAnim2FileId(2), 0x4C);
    ((BoxWork *)w)->palRes[10] = CLACT_RegisterPalette(h, APP_ICON_GetGearDataFileId(), 1, 0xC0, 0x4C);
    ((BoxWork *)w)->charRes[66] = CLACT_RegisterCharacter(h, APP_ICON_GetEffectCellFileId(2), (int)0, (int)0, (unsigned)0x4C);
    ((BoxWork *)w)->cellRes[9] = CLACT_RegisterCellAnim(h, APP_ICON_GetEffectPaletteFileId(2), APP_ICON_GetEffectAnimFileId(2), 0x4C);
    ((BoxWork *)w)->palRes[8] = CLACT_RegisterPalette(h, APP_ICON_GetBallOpenAnimFileId(), 0, 0x1A0, 0x4C);
    ARC_TOOL_DeleteHandle(h);
    /* the sub OBJ palettes were loaded again: the added types' row starts over */
    bw_type_screen_push(w, 1, 0, 7, 0, 0, 0);
}

void BOX2_SEQ_ShowHandSlot(char *work, int a, int b)
{
    int k = type_actor(work, a, b);
    BOX2_SEQ_SetActorDrawEnable((char *)work, k, 1);
    BOX2_SEQ_AddActorSurfacePos((char *)work, k, 0x58, 0x38, 1);
    if (b != 0 && a != b) {
        k = type_actor(work, b, a);
        BOX2_SEQ_SetActorDrawEnable((char *)work, k, 1);
        BOX2_SEQ_AddActorSurfacePos((char *)work, k, 0x7a, 0x38, 1);
    }
}

void BOX2_SEQ_ReleaseBoxGraphics(char *work)
{
    unsigned i;
    if (g_box == work) {
        /* (the actors are gone: nothing to give back) */
        for (i = 0; i < GAME_TYPES; i++)
            g_borrowed[i] = 0;
        g_box = 0;
    }
    bw_type_icon_chars_release(work);
    bw_type_screen_pop(work);
    for (i = 0; i < 0x45; i++)
        if (((BoxWork *)work)->charRes[i] != -1)
            CLACT_ReleaseCharacter(((BoxWork *)work)->charRes[i]);
    for (i = 0; i < 0xB; i++)
        if (((BoxWork *)work)->palRes[i] != -1)
            CLACT_ReleasePalette(((BoxWork *)work)->palRes[i]);
    for (i = 0; i < 0xC; i++)
        if (((BoxWork *)work)->cellRes[i] != -1)
            CLACT_ReleaseCellAnim(((BoxWork *)work)->cellRes[i]);
}
