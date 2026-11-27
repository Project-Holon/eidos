/*
 * patches: src/game/ov094/btlv_effvm.c BTLV_EFFVM_CmdSetPokePosition BTLV_EFFVM_StartScript BTLV_EFFVM_Create BTLV_EFFVM_Destroy BTLV_EFFVM_CmdLoadParticleResource BTLV_EFFVM_AllocEmitterSlot BTLV_EFFVM_FindEmitterSlot BTLV_EFFVM_StopScript BTLV_EFFVM_DeleteEmitter
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
 *   VM_Create 0x0201121C thumb
 *   VM_Delete 0x02011264 thumb
 *   VM_Init 0x0201126C thumb
 *   VM_GetU32 0x0201134C thumb
 *   VM_SetWait 0x020113D0 thumb
 *   MIi_CpuClear16 0x02082A48 arm
 *   ARC_TOOL_LoadMember 0x02048F60 arm
 *   ARC_TOOL_GetMemberSize 0x020490A8 arm
 *   GFL_HEAP_Alloc 0x02030734 arm
 *   GFL_HEAP_Free 0x020307B0 arm
 *   GFL_USE_AddMainTask 0x020056A0 thumb
 *   PARTICLE_LoadArchiveMember 0x0205121C arm
 *   PARTICLE_SetupSystemNoHandlers 0x02051380 arm
 *   PARTICLE_GetWorkBase 0x02051640 arm
 *   PARTICLE_DeleteSystem 0x02050DB8 arm
 *   PARTICLE_GetEffectCount 0x02051614 arm
 *   BTLV_EFFECT_GetClact 0x021F86F0 thumb ov094
 *   BTLV_CLACT_ReleaseActor 0x02201CA4 thumb ov094
 *   BTLV_EFFVM_IsAllIdle 0x021FC744 thumb ov094
 *   BTLV_EFFVM_ReleaseScriptResources 0x021FC448 thumb ov094
 *   WAZA_TOOL_GetParam 0x0201BD28 thumb
 *   GFL_HEAP_GetAllocatableSize 0x0203038C arm
 *   ARC_TOOL_ReadMember 0x02048F4C arm
 *   BG_SYS_SetPlaneVisible 0x02040588 arm
 *   BTLV_EFFECT_DeleteTasksByArg 0x021F8A10 thumb ov094
 *   BTLV_EFFECT_SetAllPokeSpriteAnim 0x021F8934 thumb ov094
 *   BTLV_EFFECT_GaugeSetSideVisible 0x021F8198 thumb ov094
 *   BTLV_EFFECT_GetMcss 0x021F8694 thumb ov094
 *   BTLV_EFFECT_RegisterTask 0x021F896C thumb ov094
 *   BTLV_MCSS_IsSpriteLoaded 0x021FFD9C thumb ov094
 *   BTLV_MCSS_GetSpriteAttrFlags 0x021FFF38 thumb ov094
 *   BTLV_EFFVM_IsMultiSideResourceId 0x021FEAA0 thumb ov094
 *   BTLV_EFFVM_StartSeVolumeTweenTask 0x021FE410 thumb ov094
 *   BTLV_EFFVM_StartSePlayTask 0x021FBA64 thumb ov094
 *   BTLV_EFFVM_ResolveParticleKey 0x021FE38C thumb ov094
 *   BTLV_EFFVM_SetEmitterHandle 0x021FD19C thumb ov094
 *   BTLV_EFFVM_ParticleLoadTaskMain 0x021FE9BC thumb ov094
 *   BTLV_EFFVM_WaitParticleCallback 0x021FC9F4 thumb ov094
 *   d_0220AE68 0x0220AE68 data ov094 a1
 *   d_02209DCC 0x02209DCC data ov094 a4
 *
 * The script a move plays (sdk/move_anims.h).  The effect VM reads an id
 * below 0x231 (BTLV_EFFECT_MOVE_END) as a move's script, member id of
 * archive 0x42 (a/0/6/6), and an id from 0x231 as another effect, member
 * id - 0x231 of archive 0x43 (a/0/6/7).  The scripts name the other effects
 * by those numbers (CallScript, JumpToArchiveScript), so the boundary stays
 * where the game has it, and a move from 0x231 on cannot be asked for by
 * its id.
 *
 * The battle view's move request (stubs/ov/patch_ov093_btlv_scu.c) is the
 * move's script when that is a game move the effect is treated as, the
 * game's own path.  Any other request is 0x8000 | an index the port reads
 * back (bw_move_anim_request): the script is the member of a/0/6/6 the port
 * names, and the VM runs it as the effect id of the game move it is treated
 * as (the Transform, Mirror Move, Nature Power, Future Sight, Doom Desire,
 * Baton Pass, Ally Switch and Tailwind cases here and in
 * ReleaseScriptResources and StartParticleEffectInternal read that id), or
 * 0x230, the one move-range id the VM treats in no special way (the game
 * has no move 560).  Every other id is the game's own, line for line.
 *
 * StartScript's record
 * ---------------------
 *
 * BtlvEffVmWork (src/game/ov094/btlv_effvm_types.h) at its offsets: flags
 * 0x000 (bit 6 fallbackScript, bit 17 fallbackPending, bit 18
 * keepBackground, bit 12 stopSeOnEnd, bit 16 particleSysActive), scriptVar
 * 0x004, owner 0x008, emitters[16] 0x00C, voiceIds[16] 0x04C,
 * resources[4] 0x0CC, attackerPos 0x0DC, defenderPos 0x0E0, scriptBuf
 * 0x1E8, pendingScriptBuf 0x1EC, pendingScriptOffset 0x1F0, heapId 0x238,
 * result 0x23C, layoutMode 0x244, params 0x248 (16 bytes: kind, variant,
 * option, side, ...), effectId 0x258, ballSource 0x25C, seVoiceIds[6]
 * 0x26C, lastEffectId 0x2A4, lastSlot 0x2A8; 0x2AC bytes.  A script file:
 * u8 variantCount, then a 0x38-byte record a variant from 4 whose first
 * u32 is the variant's entry offset.  The offsets are the DS ones: pointers
 * are four bytes in every build that runs the overlays.
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
extern void *BTLV_EFFECT_GetClact(void);
struct ScriptVm;
extern void BTLV_CLACT_ReleaseActor(void *clact, int slot);
extern int BTLV_EFFVM_IsAllIdle(struct ScriptVm *vm, void *arg);
extern int BTLV_EFFVM_ReleaseScriptResources(struct ScriptVm *vm, void *work);
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

/* sdk/move_anims.c */
extern u32 bw_move_anim_request_script(u32 request);
extern u32 bw_move_anim_request_treated(u32 request);
extern u32 bw_move_anim_request_move(u32 request);

/* The move whose animation plays (ParticleColor's source 3), 0 for another
 * effect. */
static u32 s_anim_move;

#define MOVE_END        0x231u      /* BTLV_EFFECT_MOVE_END, the game's */
#define PORT_REQUEST    0x8000u     /* (sdk/move_anims.h BW_ANIM_REQUEST) */

#define F(w, type, off)        (*(type *)((char *)(w) + (off)))
#define W_FLAGS(w)             F(w, u32, 0x000)
#define W_SCRIPTVAR(w)         F(w, int, 0x004)
#define W_OWNER(w)             F(w, void *, 0x008)
#define W_EMITTER(w, i)        F(w, void *, 0x00C + 4 * (i))
#define W_VOICEID(w, i)        F(w, int, 0x04C + 4 * (i))
#define W_RESOURCE(w, i)       F(w, int, 0x0CC + 4 * (i))
#define W_ATTACKER(w)          F(w, int, 0x0DC)
#define W_DEFENDER(w)          F(w, int, 0x0E0)
#define W_SCRIPTBUF(w)         F(w, u8 *, 0x1E8)
#define W_PENDINGBUF(w)        F(w, u8 *, 0x1EC)
#define W_PENDINGOFF(w)        F(w, int, 0x1F0)
#define W_HEAP(w)              F(w, u16, 0x238)
#define W_RESULT(w)            F(w, int, 0x23C)
#define W_LAYOUT(w)            F(w, int, 0x244)
#define W_PARAMS(w)            F(w, BtlvEffVmParams, 0x248)
#define W_EFFECTID(w)          F(w, u16, 0x258)
#define W_TYPESOURCE(w)        F(w, int, 0x25C)
#define W_SEVOICEID(w, i)      F(w, int, 0x26C + 4 * (i))
#define W_LASTEFFECT(w)        F(w, u32, 0x2A4)
#define W_LASTSLOT(w)          F(w, u32, 0x2A8)
#define WORK_SIZE              0x2ACu

#define FLAG_FALLBACK_SCRIPT   (1u << 6)
#define FLAG_STOP_SE_ON_END    (1u << 12)
#define FLAG_PARTICLE_SYS      (1u << 16)
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

    s_anim_move = bw_move_anim_request_move(effectId);
    if ((effectId & PORT_REQUEST) != 0) {
        member = bw_move_anim_request_script(effectId);
        effectId = (u16)bw_move_anim_request_treated(effectId);
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

/* ------------------------------------------------------------------ */
/* BTLV_EFFVM_Create: the command table                                */
/* ------------------------------------------------------------------ */

/*
 * The VM runs a command id below its table's count and stops at any other
 * (arm9 vm.c VM_Control).  The game's table (d_02209DCC: call stack 16,
 * registers 8, 78 commands at d_02209E88) is copied here at the first
 * Create, and the port's commands follow it from id 78
 * (docs/game-tables.md, "Effect script commands of eidos").  The game's 78
 * are the game's own functions, so its scripts run as before.
 */
typedef int (*ScriptCmdFn)(struct ScriptVm *vm, void *user);

typedef struct ScriptVmParams {
    u16 stackCap;
    u16 ringCap;
    const ScriptCmdFn *cmds;
    u32 cmdCount;
} ScriptVmParams;

typedef char vm_params_is_12[sizeof(void *) != 4 || sizeof(ScriptVmParams) == 12 ? 1 : -1];

extern ScriptVmParams d_02209DCC;   /* the game's VM parameters */

void *GFL_HEAP_Alloc(u16 heapId, u32 size, int clear, const char *file, u16 line);
void GFL_HEAP_Free(void *p);
struct ScriptVm *VM_Create(u32 heapId, const ScriptVmParams *params);
void VM_Init(struct ScriptVm *vm, void *user);

extern const char d_0220AE68[];     /* the unit's file name for the heap's records */

int BTLV_EFFVM_CmdPlaySoundByName(struct ScriptVm *vm, void *work);
extern void bw_anim_effvm_flags(const u32 *flags);

#define GAME_CMDS 78u
#define PORT_CMDS 2u                /* PlaySoundByName (78), ParticleColor (79) */

int BTLV_EFFVM_CmdParticleColor(struct ScriptVm *vm, void *work);

static ScriptCmdFn s_cmds[GAME_CMDS + PORT_CMDS];
static ScriptCmdFn s_game[GAME_CMDS];   /* the game's own, for the wrappers below */
static ScriptVmParams s_params;

/* The game's commands the port wraps (the room past its arrays, below). */
#define CMD_LOAD_CLACT_AT_POKE   45
#define CMD_CLACT_POS_TWEEN      46
#define CMD_CLACT_SCALE_TWEEN    47
#define CMD_SET_CLACT_ANIM       48
#define CMD_FADE_CLACT_PALETTE   49
#define CMD_RELEASE_CLACT        50
#define CMD_WAIT_IDLE            56     /* aletheia's BTLV_EFFVM_CmdWaitFrames: waits on IsAllIdle */
#define CMD_LOAD_CLACT_FROM_POKE 69
#define CMD_END                  77     /* BTLV_EFFVM_ReleaseScriptResources */

static int CmdLoadClactAtPokeMore(struct ScriptVm *vm, void *work);
static int CmdClactPosTweenMore(struct ScriptVm *vm, void *work);
static int CmdClactScaleTweenMore(struct ScriptVm *vm, void *work);
static int CmdSetClactAnimMore(struct ScriptVm *vm, void *work);
static int CmdFadeClactPaletteMore(struct ScriptVm *vm, void *work);
static int CmdReleaseClactMore(struct ScriptVm *vm, void *work);
static int CmdLoadClactFromPokeMore(struct ScriptVm *vm, void *work);
static int CmdWaitIdleMore(struct ScriptVm *vm, void *work);
static int CmdEndMore(struct ScriptVm *vm, void *work);

struct ScriptVm *BTLV_EFFVM_Create(void *owner, u32 heapId)
{
    void *work;
    struct ScriptVm *vm;
    int i;

    if (s_params.cmds == 0) {
        for (i = 0; i < (int)GAME_CMDS && i < (int)d_02209DCC.cmdCount; i++) {
            s_cmds[i] = s_game[i] = d_02209DCC.cmds[i];
        }
        if (d_02209DCC.cmdCount == GAME_CMDS) {
            s_cmds[CMD_LOAD_CLACT_AT_POKE] = CmdLoadClactAtPokeMore;
            s_cmds[CMD_CLACT_POS_TWEEN] = CmdClactPosTweenMore;
            s_cmds[CMD_CLACT_SCALE_TWEEN] = CmdClactScaleTweenMore;
            s_cmds[CMD_SET_CLACT_ANIM] = CmdSetClactAnimMore;
            s_cmds[CMD_FADE_CLACT_PALETTE] = CmdFadeClactPaletteMore;
            s_cmds[CMD_RELEASE_CLACT] = CmdReleaseClactMore;
            s_cmds[CMD_LOAD_CLACT_FROM_POKE] = CmdLoadClactFromPokeMore;
            s_cmds[CMD_WAIT_IDLE] = CmdWaitIdleMore;
            s_cmds[CMD_END] = CmdEndMore;
        }
        s_cmds[GAME_CMDS + 0] = BTLV_EFFVM_CmdPlaySoundByName;
        s_cmds[GAME_CMDS + 1] = BTLV_EFFVM_CmdParticleColor;
        s_params.stackCap = d_02209DCC.stackCap;
        s_params.ringCap = d_02209DCC.ringCap;
        s_params.cmds = s_cmds;
        s_params.cmdCount = d_02209DCC.cmdCount == GAME_CMDS ? GAME_CMDS + PORT_CMDS : d_02209DCC.cmdCount;
    }
    work = GFL_HEAP_Alloc((u16)heapId, WORK_SIZE, 1, d_0220AE68, 0x27D);
    W_HEAP(work) = (u16)heapId;
    W_OWNER(work) = owner;
    W_RESULT(work) = 1;
    W_LAYOUT(work) = 1;
    for (i = 0; i < 16; i++) {
        W_VOICEID(work, i) = -1;
    }
    for (i = 0; i < 4; i++) {
        W_RESOURCE(work, i) = -1;
    }
    for (i = 0; i < 6; i++) {
        W_SEVOICEID(work, i) = -1;
    }
    vm = VM_Create(heapId, &s_params);
    VM_Init(vm, work);
    /* the one-screen layout reads whether an effect background shows
     * (its status bits are the work's first word, sdk/move_anims.h) */
    bw_anim_effvm_flags((const u32 *)work);
    return vm;
}

void BTLV_EFFVM_StopScript(void *vm);
void VM_Delete(struct ScriptVm *vm);

void BTLV_EFFVM_Destroy(void *vm)
{
    void *work = VM_GetContext((struct ScriptVm *)vm);

    bw_anim_effvm_flags(0);
    BTLV_EFFVM_StopScript(vm);
    GFL_HEAP_Free(work);
    VM_Delete((struct ScriptVm *)vm);
}

/* ------------------------------------------------------------------ */
/* 78 PlaySoundByName                                                  */
/* ------------------------------------------------------------------ */

/*
 * PlaySoundByName name slot pan delay pitch volume modDepth modSpeed: the
 * arguments of PlaySe (52) without its unused last word, the sound given by
 * the name of its sequence instead of its number.  A mod's sounds get
 * their numbers when the sound archive is merged, in the order of the mods
 * loaded (sdk/vfs_sdat.h), so a script names them: `<mod>:<seq>`.  `name`
 * is the byte offset of the NUL-ended name from the end of the command, as
 * a jump's offset is.  An unknown name plays nothing.
 */
extern int bw_anim_sound_id(const char *name);
void BTLV_EFFVM_StartSePlayTask(void *vm, u32 seId, int slot, u32 pan, int pitch, u32 volume, int modDepth,
                                int modSpeed, int delay);

#define VM_PC(vm) (*(u8 **)((char *)(vm) + 0x14))     /* ScriptVm.pc (arm9 script_vm_types.h) */

int BTLV_EFFVM_CmdPlaySoundByName(struct ScriptVm *vm, void *work)
{
    int nameOffset = VM_GetU32(vm);
    int slot = VM_GetU32(vm);
    int pan = VM_GetU32(vm);
    int delay = VM_GetU32(vm);
    int pitch = VM_GetU32(vm);
    int volume = VM_GetU32(vm);
    int modDepth = VM_GetU32(vm);
    int modSpeed = VM_GetU32(vm);
    int se = bw_anim_sound_id((const char *)(VM_PC(vm) + nameOffset));

    if (se >= 0) {
        BTLV_EFFVM_StartSePlayTask(vm, (u32)se, slot, (u32)pan, pitch, (u32)volume, modDepth, modSpeed, delay);
    }
    return W_RESULT(work);
}

/* ------------------------------------------------------------------ */
/* 6 LoadParticleResource                                              */
/* ------------------------------------------------------------------ */

/*
 * The game's command with the particle system sized for the file: the
 * system takes its emitters, particles and the file's records from the
 * work buffer with no check of its end, and 0x4800 holds a file of the
 * game's sizes only (sdk/move_anims.c bw_anim_particle_work).  The pools
 * are the game's 24 emitters and 200 particles unless a mod's
 * particles.txt raises them for the file (bw_anim_particle_pools).  The
 * buffer is the game's 0x4800, allocated where the game allocates it; a
 * file that needs more gets a buffer of its need from the port in its
 * place (freed by DeleteEmitter below), so the battle heap gives what it
 * gives in the game.
 */
typedef struct ParticleLoadJob {
    void *work;
    void *emitter;
    void *resource;
} ParticleLoadJob;

extern u32 bw_anim_particle_work(const u8 *file, u32 size, u32 emitters, u32 particles);
extern void bw_anim_particle_pools(u32 member, u32 *emitters, u32 *particles);
extern void *bw_anim_particle_alloc(u32 size);
extern int bw_anim_particle_free(void *buffer);
extern void *bw_particle_create(void *workBuf, u32 workSize, u32 withCamera, u32 emitters, u32 particles,
                                u32 limitA, u32 limitB, u32 maxResIndex, u32 heapId);
u32 ARC_TOOL_GetMemberSize(u32 archiveID, u32 memberIdx);
void *PARTICLE_LoadArchiveMember(u32 archiveId, u32 member, u32 heapId);
void PARTICLE_SetupSystemNoHandlers(void *sys, void *block);
void *GFL_USE_AddMainTask(void (*fn)(void *task, void *arg), void *arg, int priority);
void BTLV_EFFECT_RegisterTask(void *task, void (*end)(void *), int arg);
void VM_SetWait(struct ScriptVm *vm, int (*wait)(struct ScriptVm *vm, void *arg));
u32 BTLV_EFFVM_ResolveParticleKey(void *work, u32 key);
int BTLV_EFFVM_AllocEmitterSlot(void *work, int key, int *out);
void BTLV_EFFVM_SetEmitterHandle(void *work, int slot, void *resource);
void BTLV_EFFVM_ParticleLoadTaskMain(void *task, void *arg);
int BTLV_EFFVM_WaitParticleCallback(struct ScriptVm *vm, void *arg);

#define PARTICLE_ARCHIVE 6
#define GAME_WORK_SIZE   0x4800u

/*
 * What a block costs the battle heap, from the game's allocator (arm9
 * gfl_heap.c, nns_fnd_expheap.c).  GFL_HEAP_AllocMemory asks the expanded
 * heap for the size and its 0x1C-byte block header (heap id, signature, the
 * caller's file name and line: struct BlockHeader).  NNS_FndAllocFromExpHeapEx
 * rounds that up to 4, and the carve (NNSi_FndAllocFromExpHeap) puts a
 * 0x10-byte memory block header in front of it, taken from the free block
 * too.  The heap id here has bit 15 set, so the block comes from the tail of
 * a free block, aligned down to 4: no alignment gap.
 *
 * GFL_HEAP_GetAllocatableSize is the usable size of the largest free block
 * (NNSi_FndGetListAllocatableSize at alignment 4), not the free total.  When
 * that block holds the sum of the costs, every allocation finds room: one
 * that takes another block leaves the largest as it was.
 *
 * The blocks one LoadParticles takes: the work buffer, the file
 * (ARC_TOOL_LoadMember), the system's record (bw_particle_create, struct
 * ParticleSys: 0x260 bytes with the DS's 4-byte pointers) and the load job.
 * The load task and the emitter handle take none (a TCB of the main task
 * list, a slot of the work).
 *
 * The script's later commands take blocks of the battle heap too (each
 * particle start a request and its copy, the motion buffers, the tasks'
 * jobs), and the game ends when one finds no room.  The heap was sized for
 * the game's scripts: they hold at most GAME_FILES_AT_ONCE particle files at
 * once, none larger than GAME_LARGEST_FILE (both from the game's data: every
 * path of every script of a/0/6/6 and a/0/6/7, following its calls and
 * jumps, and every member of a/0/0/6).  A load inside those bounds takes
 * its file and buffer from the battle heap as the game does (when the heap
 * has room for their costs); a file past them comes from the port, so the
 * battle heap holds no more files, nor larger ones, than the game's own
 * scripts put there, and keeps room for the commands after.  The bounds are
 * a limit from the data, not a proof of the heap's room: five files of the
 * largest size were tested together (no game script holds that).
 */
#define GFL_BLOCK_HEAD   0x1Cu      /* GFL_HEAP_AllocMemory's block header */
#define EXP_MBLOCK_HEAD  0x10u      /* the expanded heap's memory block header */
#define HEAP_COST(size)  ((((u32)(size) + GFL_BLOCK_HEAD + 3u) & ~3u) + EXP_MBLOCK_HEAD)
#define PTC_SYS_SIZE     0x260u     /* sizeof(struct ParticleSys), arm9 particle.c */
#define HEAP_RECORDS     (HEAP_COST(PTC_SYS_SIZE) + HEAP_COST(sizeof(ParticleLoadJob)))
#define GAME_FILES_AT_ONCE  5       /* a/0/6/7 member 3 */
#define GAME_LARGEST_FILE   0x88ACu /* a/0/0/6 member 418 */

extern u32 GFL_HEAP_GetAllocatableSize(u32 heapId);
extern void ARC_TOOL_ReadMember(void *dst, u32 archiveID, u32 memberIdx);
static int files_held(void *work);

int BTLV_EFFVM_CmdLoadParticleResource(struct ScriptVm *vm, void *work)
{
    int slot;
    int key;
    void *buffer;
    void *resource;
    ParticleLoadJob *job;
    int line;
    u32 workSize = GAME_WORK_SIZE;
    u32 emitters, particles, member;
    int inGame;
    u16 heap;

    key = (int)BTLV_EFFVM_ResolveParticleKey(work, VM_GetU32(vm));
    if (BTLV_EFFVM_AllocEmitterSlot(work, key, &slot) == 1) {
        heap = (u16)((W_HEAP(work) & 0x7fff) | 0x8000);
        line = 0x715;
        /* the game's buffer where the battle heap holds it with the file and
         * the system's records and the load is one the game makes; else the
         * port's (a script with more or larger files than the heap was sized
         * for) */
        member = ARC_TOOL_GetMemberSize(PARTICLE_ARCHIVE, (u32)key);
        inGame = files_held(work) <= GAME_FILES_AT_ONCE && member <= GAME_LARGEST_FILE;
        if (inGame
            && GFL_HEAP_GetAllocatableSize(heap) >= HEAP_COST(GAME_WORK_SIZE) + HEAP_COST(member) + HEAP_RECORDS) {
            buffer = GFL_HEAP_Alloc(heap, GAME_WORK_SIZE, 0, d_0220AE68, (u16)line);
        } else {
            buffer = bw_anim_particle_alloc(GAME_WORK_SIZE);
        }
        if (inGame && GFL_HEAP_GetAllocatableSize(heap) >= HEAP_COST(member) + HEAP_RECORDS) {
            resource = PARTICLE_LoadArchiveMember(PARTICLE_ARCHIVE, (u32)key, heap);
        } else if ((resource = bw_anim_particle_alloc(member)) != 0) {
            ARC_TOOL_ReadMember(resource, PARTICLE_ARCHIVE, (u32)key);  /* (freed by PARTICLE_DeleteSystem) */
        }
        bw_anim_particle_pools((u32)key, &emitters, &particles);
        if (resource != 0) {
            workSize = bw_anim_particle_work((const u8 *)resource, ARC_TOOL_GetMemberSize(PARTICLE_ARCHIVE, (u32)key),
                                             emitters, particles);
            if (workSize == 0) {
                if (!bw_anim_particle_free(resource)) {
                    GFL_HEAP_Free(resource);
                }
                resource = 0;
            } else if (workSize > GAME_WORK_SIZE && buffer != 0) {
                if (!bw_anim_particle_free(buffer)) {
                    GFL_HEAP_Free(buffer);
                }
                buffer = bw_anim_particle_alloc(workSize);
            }
        }
        /* the system's record and the load job come from the battle heap
         * (the game's allocator ends the game when it has no room): with
         * no room for them the file is not loaded */
        if (buffer != 0 && resource != 0 && GFL_HEAP_GetAllocatableSize(heap) >= HEAP_RECORDS) {
            W_EMITTER(work, slot) = bw_particle_create(buffer, workSize, 0, emitters, particles, 5, 6, 0x36, heap);
            if (W_EMITTER(work, slot) != 0) {
                PARTICLE_SetupSystemNoHandlers(W_EMITTER(work, slot), resource);
                line += 10;
                job = (ParticleLoadJob *)GFL_HEAP_Alloc(heap, sizeof(*job), 0, d_0220AE68, (u16)line);
                job->work = work;
                job->emitter = W_EMITTER(work, slot);
                job->resource = resource;
                SET_FLAG(work, FLAG_PARTICLE_SYS, 1);
                BTLV_EFFECT_RegisterTask(GFL_USE_AddMainTask(BTLV_EFFVM_ParticleLoadTaskMain, job, 0), 0, 1);
                VM_SetWait(vm, BTLV_EFFVM_WaitParticleCallback);
                BTLV_EFFVM_SetEmitterHandle(work, slot, resource);
            }
        }
        if (W_EMITTER(work, slot) == 0) {
            if (buffer != 0 && !bw_anim_particle_free(buffer)) {
                GFL_HEAP_Free(buffer);
            }
            if (resource != 0 && !bw_anim_particle_free(resource)) {
                GFL_HEAP_Free(resource);
            }
            W_VOICEID(work, slot) = -1;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Particle files past the 16 a script holds                           */
/* ------------------------------------------------------------------ */

/*
 * A script holds 16 particle files at once: BtlvEffVmWork.emitters[16]
 * (the system), voiceIds[16] (the file's key, -1 free) and
 * soundHandles[16] (the file's resource count), indexed by a slot that
 * AllocEmitterSlot and FindEmitterSlot give for a key.  Every command that
 * uses a file asks FindEmitterSlot for its slot and reads the arrays there
 * in the same command; no slot number is kept across commands (the load
 * task and the start requests keep the system's address).
 *
 * The port keeps the files past the 16 in a list of its own (one effect VM
 * a battle: btlv_effect.c is BTLV_EFFVM_Create's only caller).  When a
 * command asks for a file on the list, FindEmitterSlot moves it into one of
 * the 16 slots, a free one or else the next in turn, whose file goes to the
 * list in its place.  So every game command finds every file in the game's
 * arrays, and a script with 16 files or fewer never touches the list.  A
 * script holds BW_PTC_FILES_MORE more files; the particle manager holds 64
 * systems for the whole game (sdk/particle_ext.h).
 */
#define GAME_FILES          16
#define BW_PTC_FILES_MORE   48
#define W_FILEHANDLE(w, i)  F(w, int, 0x08C + 4 * (i))
#define W_WAITMODE(w)       F(w, u32, 0x240)

typedef struct MoreFile {
    void *sys;                      /* 0: free */
    int key;
    int handle;
} MoreFile;

static MoreFile s_more_files[BW_PTC_FILES_MORE];
static int s_next_out;              /* the slot that goes to the list next */

extern void *PARTICLE_GetWorkBase(void *sys);
extern void PARTICLE_DeleteSystem(void *sys);
extern int PARTICLE_GetEffectCount(void *sys);

/* The game's, with a work buffer of the port's freed there. */
void BTLV_EFFVM_DeleteEmitter(void *emitter)
{
    void *q = PARTICLE_GetWorkBase(emitter);

    PARTICLE_DeleteSystem(emitter);
    if (!bw_anim_particle_free(q)) {
        GFL_HEAP_Free(q);
    }
}

/* The particle files the script holds: the game's 16 slots and the list
 * (a slot just given to a file being loaded counts). */
static int files_held(void *work)
{
    int n = 0, i;

    for (i = 0; i < GAME_FILES; i++) {
        n += W_VOICEID(work, i) != -1;
    }
    for (i = 0; i < BW_PTC_FILES_MORE; i++) {
        n += s_more_files[i].sys != 0;
    }
    return n;
}

/* Swap slot `slot` of the work with entry `i` of the list. */
static void swap_file(void *work, int slot, int i)
{
    MoreFile was;

    was.sys = W_EMITTER(work, slot);
    was.key = W_VOICEID(work, slot);
    was.handle = W_FILEHANDLE(work, slot);
    W_EMITTER(work, slot) = s_more_files[i].sys;
    W_VOICEID(work, slot) = s_more_files[i].sys != 0 ? s_more_files[i].key : -1;
    W_FILEHANDLE(work, slot) = s_more_files[i].handle;
    if (was.key == -1) {
        was.sys = 0;                /* a free slot leaves a free entry */
    }
    s_more_files[i] = was;
}

/* A slot of the 16 to take a file from the list: a free one, else the next
 * in turn (whose file the swap moves to the list). */
static int slot_to_swap(void *work)
{
    int slot;

    for (slot = 0; slot < GAME_FILES; slot++) {
        if (W_VOICEID(work, slot) == -1) {
            return slot;
        }
    }
    slot = s_next_out;
    s_next_out = (s_next_out + 1) % GAME_FILES;
    return slot;
}

int BTLV_EFFVM_FindEmitterSlot(void *work, int key)
{
    int slot, i;

    for (slot = 0; slot < GAME_FILES; slot++) {
        if (W_VOICEID(work, slot) == key) {
            return slot;
        }
    }
    for (i = 0; i < BW_PTC_FILES_MORE; i++) {
        if (s_more_files[i].sys != 0 && s_more_files[i].key == key) {
            slot = slot_to_swap(work);
            swap_file(work, slot, i);
            return slot;
        }
    }
    return GAME_FILES;              /* (not loaded: the game's answer) */
}

int BTLV_EFFVM_AllocEmitterSlot(void *work, int key, int *out)
{
    int slot = BTLV_EFFVM_FindEmitterSlot(work, key);
    int i;

    if (slot < GAME_FILES) {
        *out = slot;
        return 0;                   /* loaded already */
    }
    for (slot = 0; slot < GAME_FILES; slot++) {
        if (W_VOICEID(work, slot) == -1) {
            break;
        }
    }
    if (slot == GAME_FILES) {
        for (i = 0; i < BW_PTC_FILES_MORE && s_more_files[i].sys != 0; i++) {
        }
        if (i == BW_PTC_FILES_MORE) {
            *out = GAME_FILES;
            return 0;               /* no room at all: the file is not loaded */
        }
        slot = slot_to_swap(work);
        swap_file(work, slot, i);   /* the slot's file to the list, the slot free */
    }
    W_VOICEID(work, slot) = key;
    *out = slot;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Cell actors past the 4 a script holds                               */
/* ------------------------------------------------------------------ */

/*
 * The cell actor commands (45 to 50, 69) name a slot of
 * BtlvEffVmWork.resources[4] by number, with no check: slot 4 is the word
 * after it (attackerPos), slot 5 the next (defenderPos).  The battle's cell
 * actor unit has 7 actors (btlv_clact.c), and actor i takes OBJ palette
 * row 10 + i (`i * 32 + 0x140`); the main OBJ palette has 16 rows, so actor
 * 6 would write past it.  Six actors fit: the port adds script slots 4
 * and 5, held in two words of its own.
 *
 * The game's own command runs for those slots too, with the slot's word
 * lent to it: the port's word is put where the command reads slot 4 or 5,
 * and taken back after.  A load command (45, 69) reads the attacker and
 * target positions before it writes the slot, so for those the word is
 * only taken after.  A slot past 5 is refused (a log line) and its
 * arguments skipped.
 */
#define GAME_CLACTS         4
#define MORE_CLACTS         2
#define W_RESOURCE_AT(w, i) F(w, int, 0x0CC + 4 * (i))

static int s_more_clacts[MORE_CLACTS] = {-1, -1};

/* The command's argument count (btlv_effvm.c). */
static int clact_args(int cmd)
{
    switch (cmd) {
    case CMD_LOAD_CLACT_AT_POKE: return 7;
    case CMD_CLACT_POS_TWEEN: return 7;
    case CMD_CLACT_SCALE_TWEEN: return 7;
    case CMD_SET_CLACT_ANIM: return 2;
    case CMD_FADE_CLACT_PALETTE: return 5;
    case CMD_RELEASE_CLACT: return 1;
    case CMD_LOAD_CLACT_FROM_POKE: return 6;
    }
    return 0;
}

extern void bw_anim_log_clact_slot(int slot);

static int clact_window(struct ScriptVm *vm, void *work, int cmd)
{
    u8 *pc = VM_PC(vm);             /* the command's first argument, the slot */
    int slot = (int)((u32)pc[0] | (u32)pc[1] << 8 | (u32)pc[2] << 16 | (u32)pc[3] << 24);
    int *word, lent, rc;

    if (slot >= 0 && slot < GAME_CLACTS) {
        return s_game[cmd](vm, work);
    }
    if (slot < 0 || slot >= GAME_CLACTS + MORE_CLACTS) {
        bw_anim_log_clact_slot(slot);
        VM_PC(vm) = pc + 4 * clact_args(cmd);
        return W_RESULT(work);
    }
    word = &W_RESOURCE_AT(work, slot);
    lent = *word;
    if (cmd != CMD_LOAD_CLACT_AT_POKE && cmd != CMD_LOAD_CLACT_FROM_POKE) {
        *word = s_more_clacts[slot - GAME_CLACTS];
    }
    rc = s_game[cmd](vm, work);
    s_more_clacts[slot - GAME_CLACTS] = *word;
    *word = lent;
    return rc;
}

static int CmdLoadClactAtPokeMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_LOAD_CLACT_AT_POKE);
}

static int CmdClactPosTweenMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_CLACT_POS_TWEEN);
}

static int CmdClactScaleTweenMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_CLACT_SCALE_TWEEN);
}

static int CmdSetClactAnimMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_SET_CLACT_ANIM);
}

static int CmdFadeClactPaletteMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_FADE_CLACT_PALETTE);
}

static int CmdReleaseClactMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_RELEASE_CLACT);
}

static int CmdLoadClactFromPokeMore(struct ScriptVm *vm, void *work)
{
    return clact_window(vm, work, CMD_LOAD_CLACT_FROM_POKE);
}

/* ------------------------------------------------------------------ */
/* The end and the waits, with the port's files and actors             */
/* ------------------------------------------------------------------ */


/* What ReleaseScriptResources does for the game's 16 files and 4 actors,
 * for the port's. */
static void release_more(void)
{
    int i;

    for (i = 0; i < BW_PTC_FILES_MORE; i++) {
        if (s_more_files[i].sys != 0) {
            BTLV_EFFVM_DeleteEmitter(s_more_files[i].sys);
            s_more_files[i].sys = 0;
        }
    }
    for (i = 0; i < MORE_CLACTS; i++) {
        if (s_more_clacts[i] != -1) {
            BTLV_CLACT_ReleaseActor(BTLV_EFFECT_GetClact(), s_more_clacts[i]);
            s_more_clacts[i] = -1;
        }
    }
    s_next_out = 0;
}

/* 77: the script's end. */
static int CmdEndMore(struct ScriptVm *vm, void *work)
{
    release_more();
    return s_game[CMD_END](vm, work);
}

/* The game's, with the port's lists let go first. */
void BTLV_EFFVM_StopScript(void *vm)
{
    void *work = VM_GetContext((struct ScriptVm *)vm);

    if (W_SCRIPTBUF(work) != 0) {
        release_more();
        BTLV_EFFVM_ReleaseScriptResources((struct ScriptVm *)vm, VM_GetContext((struct ScriptVm *)vm));
        GFL_HEAP_Free(W_SCRIPTBUF(work));
        W_SCRIPTBUF(work) = 0;
    }
}

/* IsAllIdle, with the files on the port's list: wait modes 0 and 2 wait for
 * every particle effect to end (btlv_effvm.c). */
static int IsAllIdleMore(struct ScriptVm *vm, void *arg)
{
    int i;

    if (W_WAITMODE(arg) == 0 || W_WAITMODE(arg) == 2) {
        for (i = 0; i < BW_PTC_FILES_MORE; i++) {
            if (s_more_files[i].sys != 0 && PARTICLE_GetEffectCount(s_more_files[i].sys) != 0) {
                return 0;
            }
        }
    }
    return BTLV_EFFVM_IsAllIdle(vm, arg);
}

/* 56: wait until the systems of the wait mode are idle. */
static int CmdWaitIdleMore(struct ScriptVm *vm, void *work)
{
    int rc = s_game[CMD_WAIT_IDLE](vm, work);

    VM_SetWait(vm, IsAllIdleMore);
    return rc;
}

/* ------------------------------------------------------------------ */
/* 79 ParticleColor                                                    */
/* ------------------------------------------------------------------ */

/*
 * ParticleColor file source value: every emitter of the loaded particle
 * file `file` (a key as LoadParticles takes it) draws with a colour, now
 * and for the emitters it starts later, until the file is unloaded or the
 * script ends.  The colour multiplies each particle's own (its header
 * colour, its colour animation, its children's), as the emitter's colour
 * does in the game, where it is always white (0x7FFF): a white or grey
 * particle takes the colour, a coloured one is darkened toward it.
 *
 *   source 0: `value` is the colour (BGR555, 0x7FFF white);
 *   source 1: the colour of the type the script's variant numbers (Judgment
 *             plays the variant of its type);
 *   source 2: the colour of type `value`;
 *   source 3: the colour of the type of the move whose animation plays
 *             (its move data's type).
 *
 * A type's colour: sdk/move_anims.h bw_anim_type_color.
 */
extern u16 bw_anim_type_color(u32 type);
extern void bw_particle_set_tint(void *sys, u16 color);
extern u32 WAZA_TOOL_GetParam(u32 move, u32 param);

int BTLV_EFFVM_CmdParticleColor(struct ScriptVm *vm, void *work)
{
    int key = (int)BTLV_EFFVM_ResolveParticleKey(work, VM_GetU32(vm));
    u32 source = VM_GetU32(vm);
    u32 value = VM_GetU32(vm);
    int slot = BTLV_EFFVM_FindEmitterSlot(work, key);
    u16 color;

    switch (source) {
    case 0:
        color = (u16)(value & 0x7FFF);
        break;
    case 1:
        color = bw_anim_type_color(W_PARAMS(work).variant);
        break;
    case 2:
        color = bw_anim_type_color(value);
        break;
    case 3:
        color = bw_anim_type_color(s_anim_move != 0 ? WAZA_TOOL_GetParam(s_anim_move, 0) : 0);
        break;
    default:
        return W_RESULT(work);
    }
    if (slot < GAME_FILES && W_EMITTER(work, slot) != 0) {
        bw_particle_set_tint(W_EMITTER(work, slot), color);
    }
    return W_RESULT(work);
}
