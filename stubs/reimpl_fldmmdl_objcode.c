/*
 * FLDMMDL_ObjCodeToTableIndex -- the row of an object code in a/0/4/8's
 * parameter table.  REIMPLEMENTATION, not a match: the game's own three
 * ranges (src/game/arm9/fldmmdl_save.c, matched), plus the port's private
 * codes for the other players' figures (engine/avatar/avatar_field.c).
 *
 * Each other player in the field is drawn in that player's own look
 * (engine/avatar/avatar.h).  The field keys a sprite's texture and palette
 * by object code, so every figure of one code shares them: a look of its
 * own needs a code of its own.  The port adds codes 0x3000 and up, each a
 * copy of the walking player's row whose sprite is a copy of the walking
 * sprite (both added through the port's own file layer, sdk/vfs.h, root
 * ""); this function finds their rows after the game's last one.  With no
 * private code registered the answers are the game's own.
 */
#include "stubs.h"

/* engine/avatar/avatar_field.c: where the private rows start and how many */
u32 g_bw_objcode_extra_base;
u32 g_bw_objcode_extra_count;

#define BW_OBJCODE_PRIVATE 0x3000u

u32 FLDMMDL_ObjCodeToTableIndex(u32 x)
{
    if (x < 0xdf) {
        return x;
    }
    if (x >= 0x1000 && x < 0x1236) {
        return (u16)(x - 0xf21);
    }
    if (x >= 0x2000 && x < 0x200a) {
        return (u16)(x - 0x1ceb);
    }
    if (x >= BW_OBJCODE_PRIVATE && x < BW_OBJCODE_PRIVATE + g_bw_objcode_extra_count) {
        return (u16)(g_bw_objcode_extra_base + (x - BW_OBJCODE_PRIVATE));
    }
    return 0xa;
}
