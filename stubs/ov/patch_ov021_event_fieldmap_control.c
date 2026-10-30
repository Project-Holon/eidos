/*
 * patches: src/game/ov021/event_fieldmap_control.c EVENT_FIELDMAP_CONTROL_RunDefaultFadeIn EVENT_FIELDMAP_CONTROL_GetExitEffect EVENT_FIELDMAP_CONTROL_GetEntranceEffect
 * keeps declarations
 *
 * The door fade-in: the game captures the old screen into VRAM bank A and
 * cross-fades from it into the new map with the alpha blend, one level of
 * 16 a frame, so going through a door shows 16 frames of fade.  The port
 * steps eidos_door_fade_step() levels a frame (sdk/field_fade.c: 4, a
 * quarter of the time, with the "Map transitions" setting at fast or
 * seamless); at original the step is 1 and this is the matched function.
 * After the blend the game restores the planes and loads the message
 * window's graphics, one step a frame; at fast and seamless a step that
 * moved on returns 0x21 (GAME_EVENT_Control's "again"), so the steps that
 * do not wait run in the frame the blend ends.
 *
 * EVENT_FIELDMAP_CONTROL_GetExitEffect and _GetEntranceEffect pick the
 * fades of a map change from the zone groups of both zones (the game's
 * tables gExitEffectTable and gEntranceEffectTable): 2 is the door
 * capture and cross-fade, 0 a fade through black.  With the setting at
 * seamless a change whose two fades are each 0 or 2 takes 2 for both, so
 * leaving a building or taking stairs cross-fades like a door instead of
 * going black.  A change with any other effect (a wipe, a season, the
 * white fade) keeps the game's pair: the capture must come from the
 * matching fade-out.  At original and fast both are the matched
 * functions.
 *
 * symbols:
 *   FADE_Start  0x0204E720  arm
 *   GX_SetBankForLCDC  0x0208068C  arm
 *   GX_SetBankForBG  0x0207FDCC  arm
 *   BG_SYS_SetPriority  0x0204047C  arm
 *   BG_SYS_SetPlaneVisible  0x02040588  arm
 *   FIELDMAP_SetUpdateStopped  0x02189A58  thumb  ov021
 *   G2x_SetBlendAlpha_  0x0207D63C  arm
 *   MIi_CpuClearFast  0x02082AC0  arm
 *   GX_DisableBankForLCDC  0x02080B98  arm
 *   SYMBOL_MAP_StopBg2Scale  0x02166B2C  thumb  ov010
 *   FIELDMAP_ResetBgAffineHideMsg  0x02188DF0  thumb  ov021
 *   FIELDMAP_GetMsgBg  0x02188C54  thumb  ov021
 *   FIELD_MSGBG_IsSetupDone  0x021903CC  thumb  ov021
 *   FIELD_MSGBG_LoadWindowGraphicsStep  0x021902AC  thumb  ov021
 *   ZONEDATA_GetExpObjSetId  0x02013FD8  thumb
 *
 * The function keeps the game's signature, so the unit's own prototype
 * stays (its creator, EVENT_FIELDMAP_CONTROL_CreateFadeInEventDefault,
 * names it before the definition).
 *
 * The game's own records
 * ----------------------
 *
 * FieldFadeWork (src/game/ov021/event_fieldmap_control.c) up to blendStep,
 * the same members in the same order.
 */

typedef unsigned int u32;
typedef unsigned short u16;

struct GameEvent;

typedef struct FadeWorkHead {
    void *gameSystem;
    void *fieldMap;
    int effect;
    int fadeKind;
    int fadeSpeed;
    int skipWait;
    int restoreBg;
    int blendStep;
} FadeWorkHead;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(FadeWorkHead, blendStep) == 0x1C, "FieldFadeWork.blendStep");
#endif

enum {
    STEP_START,
    STEP_BLEND,
    STEP_SAVE_PLANES,
    STEP_HIDE_MSG,
    STEP_WAIT_MSG_BG,
    STEP_LOAD_WINDOW_1,
    STEP_LOAD_WINDOW_2,
    STEP_FINISH,
    STEP_END
};

/* GAME_EVENT_Control: run the current event again in this frame */
#define GAME_EVENT_RESULT_AGAIN 0x21

#define FADE_LEVEL_MAX     16
#define FADE_SPEED_DEFAULT (-1)

extern void *bw_ds_ptr(u32 addr);
extern void bw_io_write16(u32 addr, u16 value);
extern int eidos_door_fade_step(void);
extern int eidos_field_transition(void);

extern void FIELDMAP_SetUpdateStopped(void *fieldMap, int stopped);
extern void BG_SYS_SetPriority(int plane, int priority);
extern void BG_SYS_SetPlaneVisible(int plane, int visible);
extern void G2x_SetBlendAlpha_(u32 *reg, int plane1, int plane2, int ev1, int ev2);
extern void FADE_Start(u32 kind, int start, int end, int speed);
extern void GX_SetBankForLCDC(int bank);
extern void MIi_CpuClearFast(u32 data, void *dest, u32 size);
extern void GX_DisableBankForLCDC(void);
extern void GX_SetBankForBG(int bank);
extern void SYMBOL_MAP_StopBg2Scale(void);     /* overlay 10's (symbols block) */
extern void EVENT_FIELDMAP_CONTROL_ResetBgModeKeepPlanes(void *fieldMap);
extern void FIELDMAP_ResetBgAffineHideMsg(void *fieldMap);
extern void *FIELDMAP_GetMsgBg(void *fieldMap);
extern int FIELD_MSGBG_IsSetupDone(void *msgBg);
extern void FIELD_MSGBG_LoadWindowGraphicsStep(void *msgBg, int step);
extern int ZONEDATA_GetExpObjSetId(int zoneId);
extern unsigned int EVENT_FIELDMAP_CONTROL_LookupExitEffect(int fromGroup, int toGroup);
extern unsigned int EVENT_FIELDMAP_CONTROL_LookupEntranceEffect(int fromGroup, int toGroup);

#define REG_BLDCNT_ADDR   0x04000050u
#define REG_BLDALPHA_ADDR 0x04000052u
#define HW_LCDC_VRAM      0x06800000u

int EVENT_FIELDMAP_CONTROL_RunDefaultFadeIn(struct GameEvent *event, int *seq, void *workPtr)
{
    FadeWorkHead *work = (FadeWorkHead *)workPtr;
    void *msgBg;
    int before = *seq;

    (void)event;
    switch (*seq) {
    case STEP_START:
        FIELDMAP_SetUpdateStopped(work->fieldMap, 1);
        work->blendStep = FADE_LEVEL_MAX;
        BG_SYS_SetPriority(2, 0);
        BG_SYS_SetPriority(0, 1);
        BG_SYS_SetPlaneVisible(0, 1);
        G2x_SetBlendAlpha_((u32 *)bw_ds_ptr(REG_BLDCNT_ADDR), 4, 1, work->blendStep, 0);
        FADE_Start((u32)work->fadeKind, FADE_LEVEL_MAX, 0, FADE_SPEED_DEFAULT);
        *seq = *seq + 1;
        break;
    case STEP_BLEND:
        if (work->blendStep != 0) {
            int step = eidos_door_fade_step();

            work->blendStep = work->blendStep > step ? work->blendStep - step : 0;
            bw_io_write16(REG_BLDALPHA_ADDR,
                          (u16)(((FADE_LEVEL_MAX - work->blendStep) << 8) | work->blendStep));
            break;
        }
        BG_SYS_SetPlaneVisible(2, 0);
        GX_SetBankForLCDC(8);
        MIi_CpuClearFast(0, bw_ds_ptr(HW_LCDC_VRAM), 0x20000);
        GX_DisableBankForLCDC();
        GX_SetBankForBG(8);
        SYMBOL_MAP_StopBg2Scale();
        *seq = *seq + 1;
        break;
    case STEP_SAVE_PLANES:
        EVENT_FIELDMAP_CONTROL_ResetBgModeKeepPlanes(work->fieldMap);
        *seq = *seq + 1;
        break;
    case STEP_HIDE_MSG:
        FIELDMAP_ResetBgAffineHideMsg(work->fieldMap);
        *seq = *seq + 1;
        break;
    case STEP_WAIT_MSG_BG:
        msgBg = FIELDMAP_GetMsgBg(work->fieldMap);
        if (FIELD_MSGBG_IsSetupDone(msgBg) != 0) {
            FIELD_MSGBG_LoadWindowGraphicsStep(msgBg, 0);
            *seq = *seq + 1;
        }
        break;
    case STEP_LOAD_WINDOW_1:
        FIELD_MSGBG_LoadWindowGraphicsStep(FIELDMAP_GetMsgBg(work->fieldMap), 1);
        *seq = *seq + 1;
        break;
    case STEP_LOAD_WINDOW_2:
        FIELD_MSGBG_LoadWindowGraphicsStep(FIELDMAP_GetMsgBg(work->fieldMap), 2);
        *seq = *seq + 1;
        break;
    case STEP_FINISH:
        *seq = *seq + 1;
        /* fall through */
    case STEP_END:
        FIELDMAP_SetUpdateStopped(work->fieldMap, 0);
        return 1;
    }
    if (*seq != before && *seq > STEP_BLEND && eidos_field_transition() != 0) {
        return GAME_EVENT_RESULT_AGAIN;     /* the next step now */
    }
    return 0;
}

/* The fades of a map change (GetFadeKind's effects) */
#define EFFECT_BLACK 0
#define EFFECT_DOOR  2
#define TRANSITION_SEAMLESS 2

/* 1 when the setting is seamless and both of the change's fades are plain:
 * through black or the door's own. */
static int IsSeamlessPair(int fromZoneId, int toZoneId)
{
    int fromGroup;
    int toGroup;
    unsigned int exitEffect;
    unsigned int entranceEffect;

    if (eidos_field_transition() != TRANSITION_SEAMLESS) {
        return 0;
    }
    fromGroup = ZONEDATA_GetExpObjSetId(fromZoneId);
    toGroup = ZONEDATA_GetExpObjSetId(toZoneId);
    exitEffect = EVENT_FIELDMAP_CONTROL_LookupExitEffect(fromGroup, toGroup);
    entranceEffect = EVENT_FIELDMAP_CONTROL_LookupEntranceEffect(fromGroup, toGroup);
    return (exitEffect == EFFECT_BLACK || exitEffect == EFFECT_DOOR)
        && (entranceEffect == EFFECT_BLACK || entranceEffect == EFFECT_DOOR);
}

int EVENT_FIELDMAP_CONTROL_GetExitEffect(int fromZoneId, unsigned int toZoneId)
{
    if (IsSeamlessPair(fromZoneId, (int)toZoneId)) {
        return EFFECT_DOOR;
    }
    return EVENT_FIELDMAP_CONTROL_LookupExitEffect(ZONEDATA_GetExpObjSetId(fromZoneId), ZONEDATA_GetExpObjSetId(toZoneId));
}

int EVENT_FIELDMAP_CONTROL_GetEntranceEffect(int fromZoneId, int toZoneId)
{
    int fromGroup;

    if (IsSeamlessPair(fromZoneId, toZoneId)) {
        return EFFECT_DOOR;
    }
    fromGroup = ZONEDATA_GetExpObjSetId(fromZoneId);
    return EVENT_FIELDMAP_CONTROL_LookupEntranceEffect(fromGroup, ZONEDATA_GetExpObjSetId(toZoneId));
}
