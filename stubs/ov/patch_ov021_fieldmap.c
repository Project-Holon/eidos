/*
 * patches: src/game/ov021/fieldmap.c FIELDMAP_ProcMain
 * keeps declarations
 *
 * The field proc's main step.  The game runs FIELDMAP_Main once a frame,
 * and FIELDMAP_Main runs one step of the field's sequence: building a
 * field (FIELDMAP_SeqInitSystem, the seven steps of
 * FIELDMAP_SeqCreateSubsystems, FIELDMAP_SeqStart) takes about ten
 * frames and tearing one down two, spread out so the DS keeps its frame
 * rate while the screen is black.  With the "Map transitions" setting at
 * fast or seamless (sdk/field_fade.c) the port runs those steps back to
 * back in one frame, until the field is ready (FIELDMAP_IsReady) or gone.
 * A running field is ready, so it still steps once a frame.  At original
 * this is the matched function.
 *
 * The function keeps the game's signature, so the unit's own prototype
 * stays.
 */

struct GfProc;
struct GameSystem;
struct FieldMap;

extern int FIELDMAP_Main(struct GameSystem *gameSystem, struct FieldMap *fieldMap);
extern int FIELDMAP_IsReady(struct FieldMap *fieldMap);
extern int eidos_field_transition(void);

/* the most steps one frame runs: a field builds in about 12 */
#define FIELD_STEPS_PER_FRAME 64

int FIELDMAP_ProcMain(struct GfProc *proc, int *seq, struct GameSystem *gameSystem, void *workPtr)
{
    struct FieldMap *fieldMap = *(struct FieldMap **)workPtr;   /* FieldMapProcWork.fieldMap */
    int steps = 0;

    (void)proc;
    (void)seq;
    for (;;) {
        if (FIELDMAP_Main(gameSystem, fieldMap) == 1) {
            return 1;
        }
        if (eidos_field_transition() == 0 || FIELDMAP_IsReady(fieldMap) != 0
            || ++steps >= FIELD_STEPS_PER_FRAME) {
            return 0;
        }
    }
}
