/*
 * patches: src/game/ov095/btl_id_sets.c BTL_ID_IsEncoreFailMove BTL_ID_IsMeFirstFailMove BTL_ID_IsMetronomeItemIgnoredMove BTL_ID_IsPressureNoExtraPpMove BTL_ID_IsGemIgnoredMove BTL_ID_IsCallMoveExcluded BTL_ID_IsSleepTalkExcludedMove BTL_ID_IsAssistExcludedMove BTL_ID_IsCopycatExcludedMove BTL_ID_IsMimicExcludedMove
 *
 * The game's lists of moves (moves Encore cannot repeat, Me First cannot
 * copy, Sleep Talk, Assist, Copycat and Mimic cannot pick, the Metronome
 * item and Pressure leave alone, gems ignore) take a mod's move that works
 * like a listed one (sdk/battle_handlers.h bw_bh_move_in,
 * eidos/tables/move_handlers.txt) as that move.  With no table lines each
 * function is the game's own list test.
 *
 * symbols:
 *   gEncoreFailMoves              0x0689E2DA  data  a2
 *   gMeFirstFailMoves             0x0689E31C  data  a2
 *   gMetronomeItemIgnoredMoves    0x0689E2B6  data  a2
 *   gPressureNoExtraPpMoves       0x0689E2C4  data  a2
 *   gGemIgnoredMoves              0x0689E2B0  data  a2
 *   gCallMoveExcludedMoves        0x0689E330  data  a2
 *   gSleepTalkExtraExcludedMoves  0x0689E346  data  a2
 *   gAssistExtraExcludedMoves     0x0689E3D4  data  a2
 *   gCopycatExtraExcludedMoves    0x0689E3AC  data  a2
 *   gMimicExcludedMoves           0x0689E2CE  data  a2
 */

typedef unsigned short u16;

extern u16 gEncoreFailMoves[6];
extern u16 gMeFirstFailMoves[10];
extern u16 gMetronomeItemIgnoredMoves[3];
extern u16 gPressureNoExtraPpMoves[5];
extern u16 gGemIgnoredMoves[3];
extern u16 gCallMoveExcludedMoves[11];
extern u16 gSleepTalkExtraExcludedMoves[11];
extern u16 gAssistExtraExcludedMoves[20];
extern u16 gCopycatExtraExcludedMoves[20];
extern u16 gMimicExcludedMoves[6];

/* sdk/battle_handlers.c */
extern int bw_bh_move_in(int move, const u16 *list, int count);

int BTL_ID_IsEncoreFailMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gEncoreFailMoves, 6);
}

int BTL_ID_IsMeFirstFailMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gMeFirstFailMoves, 10);
}

int BTL_ID_IsMetronomeItemIgnoredMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gMetronomeItemIgnoredMoves, 3);
}

int BTL_ID_IsPressureNoExtraPpMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gPressureNoExtraPpMoves, 5);
}

int BTL_ID_IsGemIgnoredMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gGemIgnoredMoves, 3);
}

int BTL_ID_IsCallMoveExcluded(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gCallMoveExcludedMoves, 0xb);
}

int BTL_ID_IsSleepTalkExcludedMove(unsigned moveId)
{
    if (BTL_ID_IsCallMoveExcluded(moveId))
        return 1;
    return bw_bh_move_in((int)moveId, gSleepTalkExtraExcludedMoves, 11);
}

int BTL_ID_IsAssistExcludedMove(unsigned moveId)
{
    if (BTL_ID_IsCallMoveExcluded(moveId))
        return 1;
    return bw_bh_move_in((int)moveId, gAssistExtraExcludedMoves, 20);
}

int BTL_ID_IsCopycatExcludedMove(unsigned moveId)
{
    if (BTL_ID_IsCallMoveExcluded(moveId))
        return 1;
    return bw_bh_move_in((int)moveId, gCopycatExtraExcludedMoves, 0x14);
}

int BTL_ID_IsMimicExcludedMove(unsigned moveId)
{
    return bw_bh_move_in((int)moveId, gMimicExcludedMoves, 6);
}
