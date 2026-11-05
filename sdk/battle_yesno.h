/*
 * sdk/battle_yesno.h -- the battle's yes / no answers, kept for the
 * one-screen interface (sdk/battle_yesno.c).
 */
#ifndef BW_PORT_BATTLE_YESNO_H
#define BW_PORT_BATTLE_YESNO_H

#include "types.h"

#define BW_HOOK_BATTLE_YESNO "battle_yesno"

/* 1 while the hook is on (docs/hooks.md). */
int bw_battle_yesno_on(void);

/* Copy the game's string record `record` into the port's own place for
 * answer `which` (0 the first, 1 the second); the copy, or NULL. */
void *bw_battle_yesno_keep(int which, const void *record);

#endif /* BW_PORT_BATTLE_YESNO_H */
