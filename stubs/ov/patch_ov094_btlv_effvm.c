/*
 * patches: src/game/ov094/btlv_effvm.c BTLV_EFFVM_CmdSetPokePosition BTLV_EFFVM_StartScript
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
 * symbols:
 *   VM_GetContext 0x02011324 thumb
 *   VM_Start 0x0201127C thumb
 *   MIi_CpuClear16 0x02082A48 arm
 *   ARC_TOOL_LoadMember 0x02048F60 arm
 *   BG_SYS_SetPlaneVisible 0x02040588 arm
 *   BTLV_EFFECT_DeleteTasksByArg 0x021F8A10 thumb ov094
 *   BTLV_EFFECT_SetAllPokeSpriteAnim 0x021F8934 thumb ov094
 *   BTLV_EFFECT_GaugeSetSideVisible 0x021F8198 thumb ov094
 *   BTLV_EFFECT_GetMcss 0x021F8694 thumb ov094
 *   BTLV_MCSS_IsSpriteLoaded 0x021FFD9C thumb ov094
 *   BTLV_MCSS_GetSpriteAttrFlags 0x021FFF38 thumb ov094
 *   BTLV_EFFVM_IsMultiSideResourceId 0x021FEAA0 thumb ov094
 *   BTLV_EFFVM_StartSeVolumeTweenTask 0x021FE410 thumb ov094
 *
 * The script of a move a mod gives its own (sdk/move_anims.h).  The effect
 * VM reads an id below 0x231 (BTLV_EFFECT_MOVE_END) as a move's script,
 * member id of archive 0x42 (a/0/6/6), and an id from 0x231 as another
 * effect, member id - 0x231 of archive 0x43 (a/0/6/7).  The scripts name the
 * other effects by those numbers (CallScript, JumpToArchiveScript), so the
 * boundary stays where the game has it, and a move from 0x231 on cannot be
 * asked for by its id.
 *
 * The battle view's move request (stubs/ov/patch_ov093_btlv_scu.c) asks
 * for such a move's own script as 0x8000 | the move id: its script is
 * member move id of a/0/6/6, and the VM runs it as the move effect 0x230
 * (the one move-range id the VM treats in no special way: the game has no
 * move 560).  Every other id is the game's own, line for line.
 *
 * StartScript's record
 * ---------------------
 *
 * BtlvEffVmWork (src/game/ov094/btlv_effvm_types.h) at its offsets: flags
 * 0x000 (bit 6 fallbackScript, bit 17 fallbackPending, bit 18
 * keepBackground, bit 12 stopSeOnEnd), scriptVar 0x004, attackerPos 0x0DC,
 * defenderPos 0x0E0, scriptBuf 0x1E8, pendingScriptBuf 0x1EC,
 * pendingScriptOffset 0x1F0, heapId 0x238, layoutMode 0x244, params 0x248
 * (16 bytes: kind, variant, option, side, ...), effectId 0x258, ballSource
 * 0x25C, lastEffectId 0x2A4, lastSlot 0x2A8.  A script file: u8
 * variantCount, then a 0x38-byte record a variant from 4 whose first u32 is
 * the variant's entry offset.  The offsets are the DS ones: pointers are
 * four bytes in every build that runs the overlays.
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

/* ------------------------------------------------------------------ */
/* BTLV_EFFVM_StartScript                                              */
/* ------------------------------------------------------------------ */

struct ScriptVm;
struct BtlvEffVmWork;

typedef struct BtlvEffVmParams {
    u8 kind;
    u8 variant;
    u8 option;
    u8 side;
    int argA;
    int argB;
    u16 ballItem;
    u8 pad[2];
} BtlvEffVmParams;

typedef char params_is_0x10[sizeof(BtlvEffVmParams) == 0x10 ? 1 : -1];

void *VM_GetContext(struct ScriptVm *vm);
void VM_Start(struct ScriptVm *vm, u8 *pc);
void MIi_CpuClear16(u16 data, void *destp, u32 size);
void *ARC_TOOL_LoadMember(u32 archiveID, u32 memberIdx, u16 heapID);
void BG_SYS_SetPlaneVisible(unsigned char which, unsigned char enable);
void BTLV_EFFECT_DeleteTasksByArg(int arg);
void BTLV_EFFECT_SetAllPokeSpriteAnim(int mode);
void BTLV_EFFECT_GaugeSetSideVisible(int visible, int side);
int BTLV_MCSS_IsSpriteLoaded(void *work, int pos);
int BTLV_MCSS_GetSpriteAttrFlags(void *work, int pos);
int BTLV_EFFVM_IsMultiSideResourceId(int effectId);
void BTLV_EFFVM_StartSeVolumeTweenTask(struct BtlvEffVmWork *work, int fromLevel, int toLevel, int steps);

#define MOVE_END        0x231u      /* BTLV_EFFECT_MOVE_END, the game's */
#define OWN_SCRIPT      0x8000u     /* (patch_ov093_btlv_scu.c: a mod move's own script) */
#define QUIET_MOVE_ID   0x230u      /* the move-range id with no special case */

#define F(w, type, off)        (*(type *)((char *)(w) + (off)))
#define W_FLAGS(w)             F(w, u32, 0x000)
#define W_SCRIPTVAR(w)         F(w, int, 0x004)
#define W_ATTACKER(w)          F(w, int, 0x0DC)
#define W_DEFENDER(w)          F(w, int, 0x0E0)
#define W_SCRIPTBUF(w)         F(w, u8 *, 0x1E8)
#define W_PENDINGBUF(w)        F(w, u8 *, 0x1EC)
#define W_PENDINGOFF(w)        F(w, int, 0x1F0)
#define W_HEAP(w)              F(w, u16, 0x238)
#define W_LAYOUT(w)            F(w, int, 0x244)
#define W_PARAMS(w)            F(w, BtlvEffVmParams, 0x248)
#define W_EFFECTID(w)          F(w, u16, 0x258)
#define W_TYPESOURCE(w)        F(w, int, 0x25C)
#define W_LASTEFFECT(w)        F(w, u32, 0x2A4)
#define W_LASTSLOT(w)          F(w, u32, 0x2A8)

#define FLAG_FALLBACK_SCRIPT   (1u << 6)
#define FLAG_STOP_SE_ON_END    (1u << 12)
#define FLAG_FALLBACK_PENDING  (1u << 17)
#define FLAG_KEEP_BACKGROUND   (1u << 18)

#define SET_FLAG(w, bit, on)   (W_FLAGS(w) = (on) ? (W_FLAGS(w) | (bit)) : (W_FLAGS(w) & ~(bit)))

#define REG_BLDALPHA_ADDR 0x04000052

void BTLV_EFFVM_StartScript(void *vm, u32 attackerPos, u32 defenderPos, u16 effectId, BtlvEffVmParams *params)
{
    void *work;
    int entryOffset;
    int useFallback;
    u8 *scriptData;
    u32 member = effectId;          /* the member of a/0/6/6 a move's script is */
    u16 heap;

    if ((effectId & OWN_SCRIPT) != 0) {
        member = effectId & ~OWN_SCRIPT;
        effectId = QUIET_MOVE_ID;
    }
    work = VM_GetContext((struct ScriptVm *)vm);
    W_SCRIPTBUF(work) = 0;
    BTLV_EFFECT_DeleteTasksByArg(1);
    if (params != 0) {
        W_PARAMS(work) = *params;
    } else {
        MIi_CpuClear16(0, &W_PARAMS(work), 0x10);
    }
    W_ATTACKER(work) = (int)attackerPos;
    W_DEFENDER(work) = (int)defenderPos;
    W_LAYOUT(work) = 1;
    W_EFFECTID(work) = effectId;
    if (W_EFFECTID(work) >= MOVE_END && (u16)(W_EFFECTID(work) + (u16)-(MOVE_END + 0x30)) <= 1
        && W_LASTEFFECT(work) == W_EFFECTID(work) && W_LASTSLOT(work) == attackerPos) {
        return;
    }
    W_LASTEFFECT(work) = W_EFFECTID(work);
    W_LASTSLOT(work) = attackerPos;
    if (BTLV_EFFVM_IsMultiSideResourceId(W_EFFECTID(work)) == 0) {
        BTLV_EFFECT_SetAllPokeSpriteAnim(0);
    }
    heap = (u16)((W_HEAP(work) & 0x7fff) | 0x8000);
    if (W_EFFECTID(work) < MOVE_END) {
        W_SCRIPTBUF(work) = (u8 *)ARC_TOOL_LoadMember(0x42, member, heap);
        if (W_EFFECTID(work) != 0x90 && W_EFFECTID(work) != 0x77 && W_EFFECTID(work) != 0x10b) {
            BTLV_EFFECT_GaugeSetSideVisible(0, 2);
        }
        BG_SYS_SetPlaneVisible(1, 0);
        BG_SYS_SetPlaneVisible(2, 0);
        BG_SYS_SetPlaneVisible(3, 0);
        SET_FLAG(work, FLAG_FALLBACK_SCRIPT, 0);
        BTLV_EFFVM_StartSeVolumeTweenTask((struct BtlvEffVmWork *)work, 0x7F000, 0x6B000, 0x14);
    } else {
        W_SCRIPTBUF(work) = (u8 *)ARC_TOOL_LoadMember(0x43, effectId - MOVE_END, heap);
        SET_FLAG(work, FLAG_FALLBACK_SCRIPT, 1);
        if (effectId >= MOVE_END + 0x25 && (*(volatile u16 *)REG_BLDALPHA_ADDR & 0x1f) == 0) {
            BG_SYS_SetPlaneVisible(1, 0);
        }
    }
    entryOffset = 4;
    if (params != 0) {
        if (W_SCRIPTBUF(work)[0] <= params->variant) {
            params->variant = 0;
        }
        entryOffset += params->variant * 0x38;
    }
    useFallback = 0;
    if (BTLV_MCSS_IsSpriteLoaded(BTLV_EFFECT_GetMcss(), W_ATTACKER(work))) {
        if (((W_EFFECTID(work) == 0xf8 || W_EFFECTID(work) == 0x161) && params->variant == 1)
            || W_EFFECTID(work) == 0x90 || W_EFFECTID(work) == 0x77 || W_EFFECTID(work) == 0x10b) {
        } else {
            useFallback = BTLV_MCSS_GetSpriteAttrFlags(BTLV_EFFECT_GetMcss(), W_ATTACKER(work)) & 1;
        }
    }
    if (W_EFFECTID(work) == 0x27e) {
        SET_FLAG(work, FLAG_KEEP_BACKGROUND, 1);
    }
    if (W_EFFECTID(work) == 0x291) {
        SET_FLAG(work, FLAG_KEEP_BACKGROUND, 0);
    }
    if (W_EFFECTID(work) == 0x27e && useFallback) {
        W_PENDINGBUF(work) = W_SCRIPTBUF(work);
        W_PENDINGOFF(work) = entryOffset;
        W_SCRIPTBUF(work) = (u8 *)ARC_TOOL_LoadMember(0x43, 0x33, heap);
        SET_FLAG(work, FLAG_FALLBACK_SCRIPT, 1);
        SET_FLAG(work, FLAG_FALLBACK_PENDING, 1);
        entryOffset = 4;
    } else if (!(W_FLAGS(work) & FLAG_FALLBACK_SCRIPT) && W_EFFECTID(work) != 0x90
               && !(W_FLAGS(work) & FLAG_FALLBACK_PENDING) && useFallback) {
        W_PENDINGBUF(work) = W_SCRIPTBUF(work);
        W_PENDINGOFF(work) = entryOffset;
        W_SCRIPTBUF(work) = (u8 *)ARC_TOOL_LoadMember(0x43, 0x33, heap);
        SET_FLAG(work, FLAG_FALLBACK_SCRIPT, 1);
        entryOffset = 4;
    }
    scriptData = W_SCRIPTBUF(work);
    W_SCRIPTVAR(work) = 0;
    SET_FLAG(work, FLAG_STOP_SE_ON_END, 0);
    W_TYPESOURCE(work) = 8;
    VM_Start((struct ScriptVm *)vm, W_SCRIPTBUF(work) + *(int *)(scriptData + entryOffset));
}
