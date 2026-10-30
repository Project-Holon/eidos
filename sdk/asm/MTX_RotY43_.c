/*
 * sdk/asm/MTX_RotY43_.c -- MTX_RotY43_ at 0x0207B640.
 *
 * Replaces the asm MTX_RotY43_ (src/game/arm9/fx_mtx43.c,
 * src/matched/arm9_sdk/func_0207B640.c; libfx).  Writes the 4x3 rotation
 * about Y: rows (cos, 0, -sin), (0, 1, 0), (sin, 0, cos) and the
 * translation (0, 0, 0), as the stm/str sequence of the asm does, word for
 * word; fx32 in 20.12, so 1 is 0x1000.  The words are written as u32 so a
 * negation of 0x80000000 wraps as the asm's `neg` does.  The evolution
 * scene (ov195) turns its model with it.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void MTX_RotY43_(void *dst, fx32 sinVal, fx32 cosVal);

void MTX_RotY43_(void *dst, fx32 sinVal, fx32 cosVal)
{
    u32 *m = (u32 *)dst;

    m[0] = (u32)cosVal;
    m[1] = 0;
    m[2] = 0u - (u32)sinVal;
    m[3] = 0;
    m[4] = ONE;
    m[5] = 0;
    m[6] = (u32)sinVal;
    m[7] = 0;
    m[8] = (u32)cosVal;
    m[9] = 0;
    m[10] = 0;
    m[11] = 0;
}
