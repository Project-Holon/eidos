/*
 * patches: src/game/ov094/btlv_effvm.c BTLV_EFFVM_ResolveParticleKey BTLV_EFFVM_CmdLoadClactFromPoke
 *
 * The battle effects a ball picks by its ball id take a mod's ball's own
 * files (eidos/tables/balls.txt, sdk/balls.h):
 *
 *   - BTLV_EFFVM_ResolveParticleKey: the six particle files of the throw,
 *     the opening and the send-out (a/0/0/6 keys 3, 0x1D and 0x2E..0x31,
 *     the game's 3 + ball, 0x1D + ball, 0x2E + 4 x ball);
 *   - BTLV_EFFVM_CmdLoadClactFromPoke: the 2D ball that shakes after the
 *     throw (a/0/9/5: character 0x81 + 2 x ball and palette 0x82 + 2 x ball,
 *     the cell and animation 0x81 and 0x82 shared by all).
 *
 * A file the line names is used as it is; the rest are those of the ball
 * the mod's ball works like.  A ball id the table does not know is the
 * Poke Ball's, as the game has it for 0 and any id past 25.  With no table
 * line these are the game's functions.
 *
 * symbols:
 *   VM_GetU32                   0x0201134C  thumb
 *   NNS_G3dWorldPosToScrPos     0x0206DF3C  arm
 *   ITEM_GetBallIdForItem       0x0202142C  thumb
 *   BTLV_EFFECT_GetMcss         0x021F8694  thumb  ov094
 *   BTLV_EFFECT_GetClact        0x021F86F0  thumb  ov094
 *   BTLV_MCSS_GetSlotPosition   0x021FF3F4  thumb  ov094
 *   BTLV_MCSS_GetSpriteBallId   0x022003C4  thumb  ov094
 *   BTLV_CLACT_LoadActor        0x02201B20  thumb  ov094
 *   BTLV_EFFVM_ResolveSlotKey   0x021FCF2C  thumb  ov094
 */

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef short s16;
typedef int fx32;

typedef struct VecFx32 {
    fx32 x, y, z;
} VecFx32;

/* src/game/ov094/btlv_effvm_types.h: BtlvEffVmParams */
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

/* BtlvEffVmWork up to ballSource, the same members in the same order. */
typedef struct EffVmBallHead {
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
    u32 waitMode;
    int layoutMode;
    BtlvEffVmParams params;
    u16 effectId;
    u8 pad25A[2];
    int ballSource;
} EffVmBallHead;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(EffVmBallHead, params) == 0x248, "BtlvEffVmWork.params");
_Static_assert(__builtin_offsetof(EffVmBallHead, ballSource) == 0x25C, "BtlvEffVmWork.ballSource");
#endif

struct ScriptVm;

extern u32 VM_GetU32(struct ScriptVm *vm);
extern int NNS_G3dWorldPosToScrPos(const VecFx32 *pWorld, int *px, int *py);
extern unsigned short ITEM_GetBallIdForItem(int value);
extern void *BTLV_EFFECT_GetMcss(void);
extern void *BTLV_EFFECT_GetClact(void);
extern void BTLV_MCSS_GetSlotPosition(void *work, VecFx32 *out, int pos);
extern int BTLV_MCSS_GetSpriteBallId(void *work, int pos);
extern int BTLV_CLACT_LoadActor(void *clact, u32 archiveId, int charFile, u32 palFile, int cellFile, int animFile,
                                s16 x, s16 y, u32 scaleX, u32 scaleY);
extern int BTLV_EFFVM_ResolveSlotKey(struct ScriptVm *vm, int idx);

/* sdk/balls.c */
extern int bw_ball_shown(int ballId);
extern int bw_ball_particle(int ballId, int key);
extern int bw_ball_sprite(int ballId, int which);

/* The battle's 2D effects archive (a/0/9/5). */
#define BTLV_EFFECT_CLACT_ARC 0x5f

unsigned int BTLV_EFFVM_ResolveParticleKey(EffVmBallHead *work, unsigned int key)
{
    int mode = work->ballSource;
    unsigned int ballId;
    int own;

    if (mode == 9)
        ballId = BTLV_MCSS_GetSpriteBallId(BTLV_EFFECT_GetMcss(), work->attackerPos);
    else if (mode != 8)
        ballId = BTLV_MCSS_GetSpriteBallId(BTLV_EFFECT_GetMcss(), mode);
    else
        ballId = ITEM_GetBallIdForItem(work->params.ballItem);
    /* a mod's ball: its own file, or the ball it works like */
    own = bw_ball_particle((int)ballId, (int)key);
    if (own >= 0)
        return (unsigned int)own;
    ballId = (unsigned int)bw_ball_shown((int)ballId);
    if (ballId == 0 || ballId > 0x19)
        ballId = 4;
    ballId--;
    switch (key) {
    case 3:
        key += ballId;
        break;
    case 0x1d:
        if (ballId + 1 == 0x19)
            ballId = 0x10;
        else if (ballId + 1 >= 0x10)
            ballId = 3;
        key += ballId;
        break;
    case 0x2e:
    case 0x2f:
    case 0x30:
    case 0x31:
        key += ballId << 2;
        break;
    }
    return key;
}

int BTLV_EFFVM_CmdLoadClactFromPoke(struct ScriptVm *vm, EffVmBallHead *work)
{
    int slot = VM_GetU32(vm);
    int battlerPos = BTLV_EFFVM_ResolveSlotKey(vm, VM_GetU32(vm));
    int dx = VM_GetU32(vm);
    int dy = VM_GetU32(vm);
    int scaleX = VM_GetU32(vm);
    int scaleY = VM_GetU32(vm);
    VecFx32 pos;
    int screenX;
    int screenY;
    int ballId;
    int charFile;
    int palFile;

    BTLV_MCSS_GetSlotPosition(BTLV_EFFECT_GetMcss(), &pos, battlerPos);
    pos.x += dx;
    pos.y += dy;
    NNS_G3dWorldPosToScrPos(&pos, &screenX, &screenY);
    ballId = ITEM_GetBallIdForItem(work->params.ballItem);
    /* a mod's ball: its own pair of files, or the ball it works like */
    charFile = bw_ball_sprite(ballId, 0);
    palFile = bw_ball_sprite(ballId, 1);
    ballId = bw_ball_shown(ballId);
    ballId = (ballId == 0 || ballId > 0x19) ? 4 : ballId;
    ballId = (ballId == 0x19) ? 0x11 : ballId;
    ballId *= 2;
    if (charFile < 0)
        charFile = ballId + 0x81;
    if (palFile < 0)
        palFile = ballId + 0x82;
    work->resources[slot] = BTLV_CLACT_LoadActor(BTLV_EFFECT_GetClact(), BTLV_EFFECT_CLACT_ARC, charFile, palFile, 0x81,
                                                 0x82, screenX, screenY, scaleX, scaleY);
    return work->result;
}
