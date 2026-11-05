/*
 * sdk/battle_yesno.c -- the battle's yes / no answers, kept for the
 * one-screen interface (stubs/ov/patch_ov093_btlv_core.c,
 * BTLV_CORE_StartYesNoMenu).
 *
 * The game builds the two answers ("YES" / "NO", or "FORGET" / "KEEP" when
 * a move is to be replaced) into two strings, opens the choice screen with
 * them and frees them at once: the DS's lower screen has drawn them by
 * then.  The one-screen interface reads them later, while the choice is
 * open (engine/ui/recipe_battle.c reads them from the choice's task work),
 * so the patch keeps a copy here and points the task work at it.
 *
 * The copy is the game's own string record, header and characters, so the
 * interface reads it as it reads any of the game's strings.
 */
#include "battle_yesno.h"

#include <string.h>

#include "mod_hooks.h"

/* A record's header is 8 bytes (capacity, length, magic) and the game
 * makes these two with room for 0x20 characters. */
#define KEEP_BYTES (8u + 0x20u * 2u)

static u32 g_keep[2][(KEEP_BYTES + 3u) / 4u];

int bw_battle_yesno_on(void)
{
    return bw_hook_enabled(BW_HOOK_BATTLE_YESNO);
}

void *bw_battle_yesno_keep(int which, const void *record)
{
    const u8 *src = (const u8 *)record;
    u8 *dst;
    u32 cap, bytes;

    if (which < 0 || which > 1 || record == NULL) {
        return NULL;
    }
    dst = (u8 *)g_keep[which];
    cap = (u32)src[0] | ((u32)src[1] << 8);       /* the record's capacity */
    bytes = 8u + cap * 2u;
    if (bytes > KEEP_BYTES) {
        bytes = KEEP_BYTES;
        cap = (KEEP_BYTES - 8u) / 2u;
    }
    memcpy(dst, src, bytes);
    dst[0] = (u8)cap;                             /* what fits here */
    dst[1] = (u8)(cap >> 8);
    if (((u32)dst[2] | ((u32)dst[3] << 8)) > cap) { /* the length, clamped */
        dst[2] = (u8)cap;
        dst[3] = (u8)(cap >> 8);
    }
    return dst;
}
