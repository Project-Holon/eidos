/*
 * sdk/follow.h -- following Pokemon: the lead Pokemon walks behind the
 * player on the field (sdk/follow.c; Extras, "Following Pokemon").
 */

#ifndef BW_PORT_FOLLOW_H
#define BW_PORT_FOLLOW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Once a game frame, after the game's own frame work (sdk/mod_frame.c). */
void bw_follow_frame(void);

#ifdef __cplusplus
}
#endif

#endif
