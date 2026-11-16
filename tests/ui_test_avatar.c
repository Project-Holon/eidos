/*
 * tests/ui_test_avatar.c -- the player's look for the interface tests
 * (test_ui_kit, test_ui_input, test_ui_pc), which build the kit's sources
 * without the platform library that holds engine/avatar: every picture of
 * the player keeps the game's own colours.
 */
#include "avatar.h"

u32 avatar_version(void)
{
    return 1;
}

int avatar_recolor(const u8 *idx, int w, int h, const u16 *pal, int c0_clear, const AvatarLook *look, u8 *rgba)
{
    (void)idx; (void)w; (void)h; (void)pal; (void)c0_clear; (void)look; (void)rgba;
    return 0;                       /* the game's own colours */
}
