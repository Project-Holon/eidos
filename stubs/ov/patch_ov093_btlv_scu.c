/*
 * patches: src/game/ov093/btlv_scu.c BTLV_SCU_StartWazaEffect
 *
 * The animation a move plays (sdk/move_anims.h, eidos/tables/move_anims.txt).
 * The battle view asks for a move's animation by the move's id, and the
 * effect machine reads an id from 0x231 on as another kind of effect
 * (ov094 btlv_effvm.c, BTLV_EFFVM_StartScript): a mod's move past the
 * game's would play some other effect.  The request is the one place a
 * move's id goes in as an effect, so this is where it becomes the port's
 * request (bw_move_anim_request): the game move's id when the move plays
 * the game's script of a move it is treated as, else 0x8000 | an index the
 * effect machine's patch reads back for the script and the treated move
 * (stubs/ov/patch_ov094_btlv_effvm.c).  Otherwise the function is the
 * game's.
 *
 * The game's own records
 * ----------------------
 *
 * The scene unit (ov093 btlv_scu_types.h, BtlvScu) is copied from its
 * procedure slots on, which is all this function writes, at the offsets
 * the game's header gives: the same on every target where pointers are
 * four bytes (the DS's, i686's and wasm32's), checked below.
 */

#include <stddef.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* sdk/move_anims.c */
extern u32 bw_move_anim_request(u32 move);

typedef struct BtlvScuWazaRequest {
    u16 wazaId;
    int attackerPos;                /* view positions */
    int defenderPos;
    u8 turnCount;
    u8 continueCount;
    int effectSeq;
} BtlvScuWazaRequest;

struct BtlvScu;
typedef int (*BtlvScuProc)(int *state, struct BtlvScu *scu);

typedef struct BtlvScu {
    u8 head[0xE0];                  /* (the members before, not used here) */
    BtlvScuProc preProc;            /* 0x0E0 */
    BtlvScuProc mainProc;           /* 0x0E4 */
    struct BtlvScu *procScu;        /* 0x0E8 */
    int procState;                  /* 0x0EC */
    BtlvScuWazaRequest waza;        /* 0x0F0: intro.waza */
} BtlvScu;

#define SCU_CHECK(name, cond) typedef char scu_check_##name[(cond) ? 1 : -1]
SCU_CHECK(main, sizeof(void *) != 4 || offsetof(BtlvScu, mainProc) == 0x0E4);
SCU_CHECK(state, sizeof(void *) != 4 || offsetof(BtlvScu, procState) == 0x0EC);
SCU_CHECK(waza, sizeof(void *) != 4 || offsetof(BtlvScu, waza) == 0x0F0);

int BTLV_SCU_ProcWazaEffect(int *state, BtlvScu *scu);

/* Starts a move effect: the message window fades out first. */
void BTLV_SCU_StartWazaEffect(BtlvScu *scu, int attackerPos, int defenderPos, u32 wazaId, u32 effectSeq, int turnCount,
                              u8 continueCount)
{
    BtlvScuWazaRequest *req = &scu->waza;

    /* the script it plays and the game move it is treated as
     * (sdk/move_anims.h, stubs/ov/patch_ov094_btlv_effvm.c) */
    req->wazaId = (u16)bw_move_anim_request(wazaId);
    req->attackerPos = attackerPos;
    req->defenderPos = defenderPos;
    req->turnCount = (u8)turnCount;
    req->continueCount = continueCount;
    req->effectSeq = (int)effectSeq;
    scu->preProc = 0;
    scu->mainProc = BTLV_SCU_ProcWazaEffect;
    scu->procScu = scu;
    scu->procState = 0;
}
