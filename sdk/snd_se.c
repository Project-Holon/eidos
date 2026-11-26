/*
 * sdk/snd_se.c -- the player a sound effect a mod adds plays on.
 *
 * GFL_USE_GetSeSlot (arm9 gfl_use.c) picks a sound effect's slot from its
 * sequence record (the player number, byte 5, minus one) for the game's
 * sound effects, sequences 0x546 to 0x81A, and puts every other sequence on
 * slot 1.  A mod's sequences are appended after the game's last
 * (sdk/vfs_sdat.h: from 0x81C on), so a sound effect a mod adds played on
 * slot 1 whatever player its declaration names.  Here a mod's sequence
 * takes its own record's player too; the game's are as they were.
 */

#include <stddef.h>

#include "types.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

extern u8 *NNS_SndArcGetSeqParam(int entryNo) GAME_FN;

#define GAME_SE_FIRST   0x546u
#define GAME_SE_END     0x81Bu      /* the game's sound effects end here */
#define FIRST_MOD_SEQ   0x81Cu      /* the game's sequence count: a mod's from here */

int GFL_USE_GetSeSlot(u32 seqNo)
{
    u8 *param;

    if (seqNo < GAME_SE_FIRST || (seqNo >= GAME_SE_END && seqNo < FIRST_MOD_SEQ)) {
        return 1;
    }
    param = NNS_SndArcGetSeqParam((int)seqNo);
    if (param == NULL || param[5] == 0) {
        return 1;                   /* (no such sequence, or no player: as the game's other ids) */
    }
    return param[5] - 1;
}
