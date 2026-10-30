/*
 * patches: src/game/ov020/event_mapchange.c EVENT_MAPCHANGE_CreateChangeFieldEvent
 * keeps declarations
 *
 * The change field event (EVENT_MAPCHANGE_ChangeFieldEvent) closes the old
 * field, swaps the zone, opens the new field and waits for the music to
 * settle, one step a frame: each step that starts a child event returns 0,
 * so the child only starts on the next frame.  The game's event loop
 * (GAME_EVENT_Control) has its own way to go on in the same frame, the
 * result 0x21, which EVENT_MAPCHANGE_EntranceEvent uses after its first
 * step.  With the "Map transitions" setting at fast or seamless
 * (sdk/field_fade.c) the event runs through a step that moved on and
 * returns 0x21 for it, so the next step or the child it started runs at
 * once; a step that waits still returns 0.  At original the event is the
 * game's own and this is the matched function.
 *
 * The function keeps the game's signature, so the unit's own prototype
 * stays.
 *
 * The game's own records
 * ----------------------
 *
 * MapChangeWork (src/game/ov020/event_mapchange_types.h) up to changeKind
 * and ChangeFieldWork (src/game/ov020/event_mapchange.c).
 */

typedef unsigned char u8;

struct GameEvent;
struct GameSystem;

typedef struct MapChangeHead {
    struct GameSystem *gameSystem;  /* 0x00 */
    u8 reserved04[0x30 - 0x04];
    u8 changeKind;                  /* 0x30 */
} MapChangeHead;

typedef struct ChangeFieldWork {
    MapChangeHead *mapChange;
} ChangeFieldWork;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(MapChangeHead, changeKind) == 0x30, "MapChangeWork.changeKind");
#endif

/* GAME_EVENT_Control: run the current event again in this frame */
#define GAME_EVENT_RESULT_AGAIN 0x21

extern struct GameEvent *GAME_EVENT_Create(struct GameSystem *gameSystem, struct GameEvent *parent,
                                           int (*func)(struct GameEvent *, int *, void *), unsigned int workSize);
extern void *GAME_EVENT_GetWork(struct GameEvent *event);
extern int EVENT_MAPCHANGE_ChangeFieldEvent(struct GameEvent *event, int *seq, void *workPtr);
extern int eidos_field_transition(void);

/* The change field event, going on in the same frame after a step that
 * moved on (fast and seamless). */
static int ChangeFieldEventFast(struct GameEvent *event, int *seq, void *workPtr)
{
    int before = *seq;
    int result = EVENT_MAPCHANGE_ChangeFieldEvent(event, seq, workPtr);

    if (result == 0 && *seq != before) {
        return GAME_EVENT_RESULT_AGAIN;
    }
    return result;
}

struct GameEvent *EVENT_MAPCHANGE_CreateChangeFieldEvent(void *workPtr, int changeKind)
{
    MapChangeHead *work = workPtr;
    struct GameEvent *event;
    ChangeFieldWork *changeField;

    event = GAME_EVENT_Create(work->gameSystem, 0,
                              eidos_field_transition() != 0 ? ChangeFieldEventFast
                                                            : EVENT_MAPCHANGE_ChangeFieldEvent,
                              sizeof(ChangeFieldWork));
    changeField = GAME_EVENT_GetWork(event);
    changeField->mapChange = work;
    work->changeKind = (u8)changeKind;
    return event;
}
