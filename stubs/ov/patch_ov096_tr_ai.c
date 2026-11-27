/*
 * patches: src/game/ov096/tr_ai.c TR_AI_CompareMove TR_AI_CmdLoadCurrentMove TR_AI_CmdLoadLastUsedMove TR_AI_CompareKnowsMove TR_AI_CmdLoadMoveStreak TR_AI_CmdLoadHasType
 *
 * The trainer AI's scripts test moves by id ("is the move Protect", "does
 * the foe know Spikes").  A mod's move that works like a
 * built-in one (sdk/battle_handlers.h, eidos/tables/move_handlers.txt) is
 * that move to the scripts: the move it compares and loads is the base,
 * and "knows the move" counts a move that works like it.  The AI's other
 * tests read the move record (power, type, effect sequence), which a mod's
 * move has of its own.  With no table lines every function is the game's
 * own.
 *
 * "Has the type" counts a third type a move gave the Pokemon (add_type,
 * Trick-or-Treat, Forest's Curse: sdk/battle_moves.h): the AI reads its
 * client's copy, so it asks the server's by the Pokemon's battle id.
 *
 * symbols:
 *   VM_GetU32                              0x0201134C  thumb  arm9
 *   TR_AI_Compare                          0x0218A0F4  thumb  ov096
 *   TR_AI_GetPosition                      0x0218A160  thumb  ov096
 *   TR_AI_GetPoke                          0x0218A2FC  thumb  ov096
 *   BTL_POKEPARAM_GetMoveCount             0x021D5448  thumb  ov093
 *   BTL_POKEPARAM_GetMoveId                0x021D54C0  thumb  ov093
 *   BTL_POKEPARAM_IsFainted                0x021D5B68  thumb  ov093
 *   BTL_POKEPARAM_GetLastUsedMoveId        0x021D67A8  thumb  ov093
 *   BTL_POKEPARAM_GetMoveStreak            0x021D679C  thumb  ov093
 *   BTL_POKEPARAM_GetTypePair              0x021D57FC  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId                0x021D5440  thumb  ov093
 *   BTL_TYPEPAIR_GetHigh                   0x021E8CFC  thumb  ov093
 *   BTL_TYPEPAIR_GetLow                    0x021E8D04  thumb  ov093
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct ScriptVm;
struct BtlPokeParam;

/* tr_ai_types.h: the fields of TrAiWork these commands use */
#define AI_MOVE_ID(ai)          (*(u16 *)((u8 *)(ai) + 0x02))
#define AI_WORK_VALUE(ai)       (*(int *)((u8 *)(ai) + 0x38))
#define AI_KNOWN_MOVE(ai, p, s) (((u16 *)((u8 *)(ai) + 0x4C))[(p) * 4 + (s)])
#define AI_SELF_POKE(ai)        (*(struct BtlPokeParam **)((u8 *)(ai) + 0xBC))
#define AI_CMD_RETURN(ai)       (*(int *)((u8 *)(ai) + 0xC4))

#define BTL_MOVE_PROTECT        0xB6
#define BTL_MOVE_DETECT         0xC5
#define BTL_MOVE_ENDURE         0xCB

typedef struct TrAiWork TrAiWork;

extern u32 VM_GetU32(struct ScriptVm *vm);
extern int TR_AI_Compare(struct ScriptVm *vm, int op, int left, int right, int jumpOffset);
extern u8 TR_AI_GetPosition(TrAiWork *ai, int kind);
extern struct BtlPokeParam *TR_AI_GetPoke(TrAiWork *ai, int pos);
extern u8 BTL_POKEPARAM_GetMoveCount(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetMoveId(struct BtlPokeParam *pp, u8 slot);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetLastUsedMoveId(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_GetMoveStreak(struct BtlPokeParam *pp);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);
extern int bw_bh_move_is(int move, int builtin);

#define BW_BH_MOVE   0
#define NAMED_FIRST  0x10000    /* eidos's own bases: no game id */

/* The move a script sees: the built-in move a mod's move works like. */
static int script_move(int move)
{
    int base = bw_bh_base(BW_BH_MOVE, move);

    return base > 0 && base < NAMED_FIRST ? base : move;
}

int TR_AI_CompareMove(struct ScriptVm *vm, TrAiWork *ai, int op)
{
    int compareMove, jumpOffset;

    compareMove = (int)VM_GetU32(vm);
    jumpOffset = (int)VM_GetU32(vm);
    return TR_AI_Compare(vm, op, script_move(AI_MOVE_ID(ai)), compareMove, jumpOffset);
}

int TR_AI_CmdLoadCurrentMove(struct ScriptVm *vm, TrAiWork *ai)
{
    (void)vm;
    AI_WORK_VALUE(ai) = script_move(AI_MOVE_ID(ai));
    return AI_CMD_RETURN(ai);
}

int TR_AI_CmdLoadLastUsedMove(struct ScriptVm *vm, TrAiWork *ai)
{
    u8 pos;

    pos = TR_AI_GetPosition(ai, (int)VM_GetU32(vm));
    AI_WORK_VALUE(ai) = script_move(BTL_POKEPARAM_GetLastUsedMoveId(TR_AI_GetPoke(ai, pos)));
    return AI_CMD_RETURN(ai);
}

int TR_AI_CompareKnowsMove(struct ScriptVm *vm, TrAiWork *ai, int op)
{
    u16 value;
    int side, jumpOffset, found, moveSlot;
    struct BtlPokeParam *poke;
    int pos;

    side = (int)VM_GetU32(vm);
    value = (u16)VM_GetU32(vm);
    jumpOffset = (int)VM_GetU32(vm);
    pos = TR_AI_GetPosition(ai, side);
    found = 0;
    switch (side) {
    case 1:
        for (moveSlot = 0; moveSlot < BTL_POKEPARAM_GetMoveCount(AI_SELF_POKE(ai)); moveSlot++) {
            if (bw_bh_move_is(BTL_POKEPARAM_GetMoveId(AI_SELF_POKE(ai), (u8)moveSlot), value)) {
                found = 1;
                break;
            }
        }
        break;
    case 3:
        poke = TR_AI_GetPoke(ai, TR_AI_GetPosition(ai, 3));
        if (BTL_POKEPARAM_IsFainted(poke) == 0) {
            for (moveSlot = 0; moveSlot < BTL_POKEPARAM_GetMoveCount(poke); moveSlot++) {
                if (bw_bh_move_is(BTL_POKEPARAM_GetMoveId(poke, (u8)moveSlot), value)) {
                    found = 1;
                    break;
                }
            }
        }
        break;
    case 0:
        for (moveSlot = 0; moveSlot < 4; moveSlot++) {
            if (bw_bh_move_is(AI_KNOWN_MOVE(ai, pos, moveSlot), value)) {
                found = 1;
                break;
            }
        }
        break;
    }
    return TR_AI_Compare(vm, op, found, 1, jumpOffset);
}

/* The Protect streak of a Pokemon whose last move was Protect, Detect or Endure (or works like one), else 0. */
int TR_AI_CmdLoadMoveStreak(struct ScriptVm *vm, TrAiWork *ai)
{
    struct BtlPokeParam *poke;
    int last;

    poke = TR_AI_GetPoke(ai, TR_AI_GetPosition(ai, (int)VM_GetU32(vm)));
    last = BTL_POKEPARAM_GetLastUsedMoveId(poke);
    if (!bw_bh_move_is(last, BTL_MOVE_PROTECT) && !bw_bh_move_is(last, BTL_MOVE_DETECT) &&
        !bw_bh_move_is(last, BTL_MOVE_ENDURE))
        AI_WORK_VALUE(ai) = 0;
    else
        AI_WORK_VALUE(ai) = BTL_POKEPARAM_GetMoveStreak(poke);
    return AI_CMD_RETURN(ai);
}

extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern u8 BTL_TYPEPAIR_GetHigh(u16 pair);
extern u8 BTL_TYPEPAIR_GetLow(u16 pair);

/* sdk/battle_moves.c */
extern int bw_bm_third_type_any(int pokeId);

/* Whether the Pokemon at a position (the AI's own, its target, ...) has
 * the type: its two types, or a third one a move gave it. */
int TR_AI_CmdLoadHasType(struct ScriptVm *vm, TrAiWork *ai)
{
    int side;
    u8 typeId;
    u16 types;
    u8 pos;
    struct BtlPokeParam *pp;

    side = (int)VM_GetU32(vm);
    typeId = (u8)VM_GetU32(vm);
    pos = TR_AI_GetPosition(ai, side);
    pp = TR_AI_GetPoke(ai, pos);
    types = BTL_POKEPARAM_GetTypePair(pp);
    AI_WORK_VALUE(ai) = typeId == BTL_TYPEPAIR_GetHigh(types) || typeId == BTL_TYPEPAIR_GetLow(types) ||
                        typeId == bw_bm_third_type_any(BTL_POKEPARAM_GetPokeId(pp));
    return AI_CMD_RETURN(ai);
}
