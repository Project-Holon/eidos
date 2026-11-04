/*
 * patches: src/game/ov021/field_ground_anime.c FIELD_GROUND_ANIME_MainA FIELD_GROUND_ANIME_CreateB
 * keeps declarations
 *
 * The door animations around a map change, both changed with the "Map
 * transitions" setting at fast or seamless (sdk/field_fade.c); at original
 * both are the matched functions.
 *
 * FIELD_GROUND_ANIME_CreateB, the walk into a door (the "B" task).  The
 * task moves one state a frame unless its work's chainSteps is set, when
 * a state that does not wait returns 0x21 and the next one runs in the
 * same frame (the game sets it for entrances without an entrance camera).
 * The port sets it for every entrance: the player starts walking in at
 * once instead of standing at the door for four states.
 *
 * FIELD_GROUND_ANIME_MainA, the arrival through a door (the "A" task: the
 * new map, still black).  Its first state lets the camera's follow delay
 * drain before the fade-in starts: about twelve frames of black screen in
 * which the camera catches up with a player nobody can see yet.  The port
 * stops the delay at once (FIELD_CAMERA_StopDelay, the camera's own way to
 * drop it) and goes on; the picture that fades in is the one the drained
 * camera would show.  It also goes on in the same frame after a state
 * that moved on (0x21, as the "B" task does with chainSteps).
 *
 * symbols:
 *   EVENT_MAPCHANGE_SetPlayerVanish  0x02185B88  thumb  ov020
 *   FIELD_CAMERA_HasDelay  0x0218F0F8  thumb  ov021
 *   FIELD_CAMERA_EndDelay  0x0218F0A8  thumb  ov021
 *   FIELD_CAMERA_StopDelay  0x0218F0D4  thumb  ov021
 *   FIELD_CAMERA_IsDelayEnabled  0x0218F0E8  thumb  ov021
 *   FIELD_CAMERA_ReloadSetting  0x0218E160  thumb  ov021
 *   FIELDMAP_GetCamera  0x02188C58  thumb  ov021
 *   FIELDMAP_GetTaskManager  0x02188D38  thumb  ov021
 *   FIELDMAP_GetZoneId  0x02188C9C  thumb  ov021
 *   ENTRANCE_CAMERA_Create  0x021BDC94  thumb  ov021
 *   ENTRANCE_CAMERA_Setup  0x021BDCC8  thumb  ov021
 *   ENTRANCE_CAMERA_IsActive  0x021BDD40  thumb  ov021
 *   ENTRANCE_CAMERA_IsInstant  0x021BDD48  thumb  ov021
 *   ENTRANCE_CAMERA_Start  0x021BDD20  thumb  ov021
 *   ENTRANCE_CAMERA_Restore  0x021BDD34  thumb  ov021
 *   ENTRANCE_CAMERA_Delete  0x021BDCB8  thumb  ov021
 *   FIELD_SOUND_SYSTEM_PostFadeIn  0x02029D7C  thumb
 *   EVENT_FIELD_PROCLINK_StartObjAnim  0x021618E0  thumb  ov010
 *   EVENT_FIELD_PROCLINK_IsSlotIndexOutOfRange  0x02162370  thumb  ov010
 *   GAME_EVENT_Create  0x020122C0  thumb
 *   GAME_EVENT_GetWork  0x020124E8  thumb
 *   GAME_EVENT_CallEvent  0x02012374  thumb
 *   GAME_DATA_GetBgmInfo  0x02012A30  thumb
 *   FIELD_TASK_MANAGER_IsEmpty  0x021C6E48  thumb  ov021
 *
 * The game's own records
 * ----------------------
 *
 * GroundAnimeWork (src/game/ov021/field_ground_anime.c), whole: the "B"
 * task is created here with its size.  GroundAnimeArgs up to the members
 * these functions read, in the same order.
 */

typedef unsigned char u8;
typedef unsigned short u16;

struct GameEvent;

typedef struct ArgsHead {
    void *reserved00;
    void *gameSystem;
    void *gameData;
    void *fieldMap;
    int exitEntranceParam;      /* used by the "B" task */
    int enterEntranceParam;     /* used by the "A" task */
} ArgsHead;

typedef struct EntranceCameraRequest {
    int entranceType;
    int phase;
    int usePlayerPos;
} EntranceCameraRequest;

typedef struct VecFx32 {
    int x, y, z;
} VecFx32;

#define GROUND_ANIME_STEP_MAX 9

typedef struct GroundAnimeWork {
    ArgsHead *args;
    void *bgmInfo;
    void *entrance;
    EntranceCameraRequest entranceRequest;
    u16 frameCounter;
    u8 playerMoveMode;
    u8 reserved1B;
    VecFx32 entryPos;
    void *buildModel;
    u8 steps[GROUND_ANIME_STEP_MAX];
    u8 reserved35;
    u8 stepIndex;
    u8 reserved37;
    int chainSteps;
} GroundAnimeWork;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(sizeof(GroundAnimeWork) == 0x3C, "GroundAnimeWork");
_Static_assert(__builtin_offsetof(GroundAnimeWork, chainSteps) == 0x38, "GroundAnimeWork.chainSteps");
#endif

/* States of FIELD_GROUND_ANIME_MainA */
#define GROUND_A_LOCK_CAMERA 0
#define GROUND_A_START_ENTRANCE 1
#define GROUND_A_PLAY_MODEL_FORWARD 2
#define GROUND_A_WAIT_MODEL_FORWARD 3
#define GROUND_A_START_PLAYER_ANIM 4
#define GROUND_A_FINISH_ENTRANCE 5
#define GROUND_A_PLAY_MODEL_REVERSE 6
#define GROUND_A_WAIT_MODEL_REVERSE 7
#define GROUND_A_END 8
#define GROUND_A_WAIT_CAMERA 9
#define GROUND_A_DELAY 10

/* GAME_EVENT_Control: run the current event again in this frame */
#define GAME_EVENT_RESULT_AGAIN 0x21

extern int eidos_field_transition(void);

extern void EVENT_MAPCHANGE_SetPlayerVanish(void *fieldMap, int vanish);   /* overlay 20's */
extern int FIELD_CAMERA_HasDelay(void *camera);
extern void FIELD_CAMERA_EndDelay(void *camera);
extern void FIELD_CAMERA_StopDelay(void *camera, int unused);
extern unsigned int FIELD_CAMERA_IsDelayEnabled(void *camera);
extern void FIELD_CAMERA_ReloadSetting(void *camera);
extern void *FIELDMAP_GetCamera(void *fieldMap);
extern void *FIELDMAP_GetTaskManager(void *fieldMap);
extern int FIELDMAP_GetZoneId(void *fieldMap);
extern void *ENTRANCE_CAMERA_Create(void *fieldMap);
extern void ENTRANCE_CAMERA_Setup(void *entrance, EntranceCameraRequest *request);
extern int ENTRANCE_CAMERA_IsActive(void *entrance);
extern int ENTRANCE_CAMERA_IsInstant(void *entrance);
extern void ENTRANCE_CAMERA_Start(void *entrance);
extern void ENTRANCE_CAMERA_Restore(void *entrance);
extern void ENTRANCE_CAMERA_Delete(void *entrance);
extern void FIELD_SOUND_SYSTEM_PostFadeIn(void *bgmInfo, void *gameData);
extern void *EVENT_FIELD_PROCLINK_StartObjAnim(void *gameSystem, void *fieldMap);   /* overlay 10's */
extern int EVENT_FIELD_PROCLINK_IsSlotIndexOutOfRange(int entranceParam);         /* overlay 10's */
extern struct GameEvent *GAME_EVENT_Create(void *gameSystem, struct GameEvent *parent,
                                           int (*func)(struct GameEvent *, int *, void *), unsigned int workSize);
extern void *GAME_EVENT_GetWork(struct GameEvent *event);
extern void GAME_EVENT_CallEvent(void *event, void *child);
extern void *GAME_DATA_GetBgmInfo(void *gameData);
extern int FIELD_TASK_MANAGER_IsEmpty(void *taskManager);

/* the unit's own */
extern unsigned char FIELD_GROUND_ANIME_GetNextStep(GroundAnimeWork *work);
extern unsigned char FIELD_GROUND_ANIME_GetCurrentStep(GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_AdvanceStep(GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_CallEnterEvent(void *task, GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_PlayBuildModelForward(GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_PlayBuildModelReverse(GroundAnimeWork *work);
extern int FIELD_GROUND_ANIME_IsBuildModelDone(GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_DeleteBuildModel(GroundAnimeWork *work);
extern int FIELD_GROUND_ANIME_GetPlayerMoveMode(void *fieldMap);
extern void FIELD_GROUND_ANIME_GetEntryPosByMapKind(void *fieldMap, VecFx32 *out);
extern void FIELD_GROUND_ANIME_CreateBuildModel(GroundAnimeWork *work);
extern void FIELD_GROUND_ANIME_SetupStepsB(GroundAnimeWork *work);
extern int FIELD_GROUND_ANIME_MainB(struct GameEvent *task, int *state, void *workPtr);

int FIELD_GROUND_ANIME_MainA(struct GameEvent *task, int *state, void *workPtr)
{
    GroundAnimeWork *work = workPtr;
    ArgsHead *args;
    void *gameSystem;
    void *fieldMap;
    void *gameData;
    void *bgmInfo;
    void *camera;
    void *fieldProc;
    int before = *state;

    args = work->args;
    gameSystem = args->gameSystem;
    fieldMap = args->fieldMap;
    gameData = args->gameData;
    bgmInfo = work->bgmInfo;
    camera = FIELDMAP_GetCamera(fieldMap);
    fieldProc = FIELDMAP_GetTaskManager(fieldMap);
    FIELDMAP_GetZoneId(fieldMap);

    switch (*state) {
    case GROUND_A_LOCK_CAMERA:
        EVENT_MAPCHANGE_SetPlayerVanish(fieldMap, 1);
        if (FIELD_CAMERA_HasDelay(camera) == 0) {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        } else if (eidos_field_transition() != 0) {
            /* the screen is black: drop the delay instead of draining it */
            FIELD_CAMERA_StopDelay(camera, 0);
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        } else {
            FIELD_CAMERA_EndDelay(camera);
            *state = GROUND_A_WAIT_CAMERA;
        }
        break;
    case GROUND_A_WAIT_CAMERA:
        if (FIELD_CAMERA_IsDelayEnabled(camera) == 0) {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        }
        break;
    case GROUND_A_START_ENTRANCE:
        ENTRANCE_CAMERA_Setup(work->entrance, &work->entranceRequest);
        FIELD_SOUND_SYSTEM_PostFadeIn(bgmInfo, gameData);
        FIELD_GROUND_ANIME_CallEnterEvent(task, work);
        *state = FIELD_GROUND_ANIME_GetNextStep(work);
        FIELD_GROUND_ANIME_AdvanceStep(work);
        break;
    case GROUND_A_PLAY_MODEL_FORWARD:
        FIELD_GROUND_ANIME_PlayBuildModelForward(work);
        *state = FIELD_GROUND_ANIME_GetNextStep(work);
        FIELD_GROUND_ANIME_AdvanceStep(work);
        break;
    case GROUND_A_WAIT_MODEL_FORWARD:
        if (FIELD_GROUND_ANIME_IsBuildModelDone(work) == 1) {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        }
        break;
    case GROUND_A_START_PLAYER_ANIM:
        EVENT_MAPCHANGE_SetPlayerVanish(fieldMap, 0);
        GAME_EVENT_CallEvent(task, EVENT_FIELD_PROCLINK_StartObjAnim(gameSystem, fieldMap));
        if (ENTRANCE_CAMERA_IsActive(work->entrance) != 0 && ENTRANCE_CAMERA_IsInstant(work->entrance) != 0) {
            *state = GROUND_A_DELAY;
        } else {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        }
        break;
    case GROUND_A_PLAY_MODEL_REVERSE:
        FIELD_GROUND_ANIME_PlayBuildModelReverse(work);
        *state = FIELD_GROUND_ANIME_GetNextStep(work);
        FIELD_GROUND_ANIME_AdvanceStep(work);
        break;
    case GROUND_A_DELAY:
        if (work->frameCounter++ > 15) {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        }
        break;
    case GROUND_A_FINISH_ENTRANCE:
        ENTRANCE_CAMERA_Start(work->entrance);
        *state = FIELD_GROUND_ANIME_GetNextStep(work);
        FIELD_GROUND_ANIME_AdvanceStep(work);
        break;
    case GROUND_A_WAIT_MODEL_REVERSE:
        if (FIELD_GROUND_ANIME_IsBuildModelDone(work) == 1) {
            *state = FIELD_GROUND_ANIME_GetNextStep(work);
            FIELD_GROUND_ANIME_AdvanceStep(work);
        }
        break;
    case GROUND_A_END:
        break;
    }

    if (FIELD_GROUND_ANIME_GetCurrentStep(work) == GROUND_A_END && FIELD_TASK_MANAGER_IsEmpty(fieldProc) != 0) {
        ENTRANCE_CAMERA_Restore(work->entrance);
        FIELD_CAMERA_ReloadSetting(camera);
        ENTRANCE_CAMERA_Delete(work->entrance);
        FIELD_GROUND_ANIME_DeleteBuildModel(work);
        return 1;
    }
    if (*state != before && eidos_field_transition() != 0) {
        return GAME_EVENT_RESULT_AGAIN;     /* the next state, or the event it called, now */
    }
    return 0;
}

void *FIELD_GROUND_ANIME_CreateB(ArgsHead *args)
{
    struct GameEvent *task;
    GroundAnimeWork *work;

    task = GAME_EVENT_Create(args->gameSystem, 0, FIELD_GROUND_ANIME_MainB, sizeof(GroundAnimeWork));
    work = GAME_EVENT_GetWork(task);
    work->args = args;
    work->bgmInfo = GAME_DATA_GetBgmInfo(args->gameData);
    work->buildModel = 0;
    work->frameCounter = 0;
    work->playerMoveMode = (u8)FIELD_GROUND_ANIME_GetPlayerMoveMode(args->fieldMap);
    work->stepIndex = 0;
    work->entrance = ENTRANCE_CAMERA_Create(args->fieldMap);
    work->entranceRequest.entranceType = args->exitEntranceParam;
    work->entranceRequest.phase = 0;
    work->entranceRequest.usePlayerPos = 1;
    FIELD_GROUND_ANIME_GetEntryPosByMapKind(args->fieldMap, &work->entryPos);
    FIELD_GROUND_ANIME_CreateBuildModel(work);
    FIELD_GROUND_ANIME_SetupStepsB(work);
    if (EVENT_FIELD_PROCLINK_IsSlotIndexOutOfRange(args->exitEntranceParam) != 0
        || eidos_field_transition() != 0) {
        work->chainSteps = 1;   /* the port: every entrance, at fast and seamless */
    } else {
        work->chainSteps = 0;
    }
    return task;
}
