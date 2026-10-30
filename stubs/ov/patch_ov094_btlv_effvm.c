/*
 * patches: src/game/ov094/btlv_effvm.c BTLV_EFFVM_CmdSetPokePosition
 *
 * An effect script's move of a sprite to a fixed point (kind 0) gets the
 * place's slot move (sdk/battle_view.c, eidos_bv_fixed_point).
 *
 * The port moves the battle's slots for the place: the foe's side back and
 * up, the trainers by the place's offsets.  It does so in the slot tables
 * (BTLV_MCSS_CalcSlotPosition), so every sprite that stands at its slot
 * follows.  A few effect scripts put a sprite at a point of their own
 * instead: a trainer back mid-battle to speak (effect 63) stands at
 * (0.74, 0.4, -9.0) and slides in from there.  Without the move such a
 * sprite misses the place's foe distance and trainer offsets, and stands in
 * front of its Pokemon on a place that moves the foe back.  The point gets
 * the same move as the slot, so the trainer stands where the place puts its
 * side.  The relative moves after it (kind 1) follow from there.
 *
 * With the hook off (bw_hook_enabled("battle_fixed_points") 0) this is the
 * matched function.
 *
 * The game's own records
 * ----------------------
 *
 * The command reads its arguments and writes nothing to the VM work but
 * returns its `result`; the head of BtlvEffVmWork up to that member is
 * copied here (src/game/ov094/btlv_effvm_types.h), the same members in the
 * same order, so the host compiler gives `result` the game's own offset.
 */

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int u32;

typedef struct EffVmHead {
    u32 flags;
    int scriptVar;
    void *owner;
    void *emitters[16];
    int voiceIds[16];
    int soundHandles[16];
    int resources[4];
    int attackerPos;
    int defenderPos;
    int savedAttackerPos[16];
    int savedDefenderPos[16];
    void *savedScriptBuf[16];
    int savedScriptVar[16];
    int waitFrames;
    void *scriptBuf;
    void *pendingScriptBuf;
    int pendingScriptOffset;
    void *allocBuffers[16];
    int allocBufferCount;
    u16 heapId;
    u8 pad23A[2];
    int result;
} EffVmHead;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(EffVmHead, result) == 0x23C, "BtlvEffVmWork.result");
#endif

extern u32 VM_GetU32(void *vm);
extern int BTLV_EFFECT_GetPokeSlot(int pos);
extern void *BTLV_EFFECT_GetMcss(void);
extern void BTLV_MCSS_StartPositionTween(void *work, int pos, int kind, int *target, int frames, int waitFrames,
                                         int repeat);
extern int eidos_bv_fixed_point(int pos, int *xyz);

int BTLV_EFFVM_CmdSetPokePosition(void *vm, EffVmHead *work)
{
    int target[3];
    int kind;
    int targetPos;
    int frames, waitFrames, repeat;

    targetPos = BTLV_EFFECT_GetPokeSlot(VM_GetU32(vm));
    kind = VM_GetU32(vm);
    target[0] = VM_GetU32(vm);
    target[1] = VM_GetU32(vm);
    target[2] = VM_GetU32(vm);
    frames = VM_GetU32(vm);
    waitFrames = VM_GetU32(vm);
    repeat = VM_GetU32(vm);
    if (kind == 0) {
        eidos_bv_fixed_point(targetPos, target);
    }
    BTLV_MCSS_StartPositionTween(BTLV_EFFECT_GetMcss(), targetPos, kind, target, frames, waitFrames, repeat);
    return work->result;
}
