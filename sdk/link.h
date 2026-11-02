/*
 * sdk/link.h -- Link sessions: the DS's local wireless between players
 * anywhere (docs/link-play.md).
 *
 * A Link session is a relay session (sdk/mod_net.h) in which every player
 * plays their own save.  While one is open the console's radio is on: the
 * game's wireless features (the Union Room, the Entralink, the C-Gear's
 * nearby players) run on sdk/arm7/wm.c, whose frames this file carries over
 * the relay under BW_MOD_NET_LINK_KEY.  Everyone in the session is in
 * radio range of everyone else.
 *
 * A Link session and a co-op session (sdk/mp.h) are never open at once.
 */
#ifndef BW_PORT_LINK_H
#define BW_PORT_LINK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BwLinkState {
    BW_LINK_OFF = 0,
    BW_LINK_OPEN,         /* in a session: the radio is on                    */
    BW_LINK_ENDED         /* the session ended (the reason says why)          */
} BwLinkState;

/* Host a Link session; the relay's code goes to `code`.  0 on success, else
 * the relay client's error (sdk/mod_net.h), or -1 while co-op is open.    */
int  bw_link_host(char *code, u32 size);
/* Join the Link session with `code`.  0 on success.                       */
int  bw_link_join(const char *code);
/* Leave: the radio goes off (the game sees every link drop).              */
void bw_link_leave(void);

BwLinkState bw_link_state(void);
int  bw_link_is_host(void);
const char *bw_link_code(void);
const char *bw_link_end_reason(void);
/* Players in the session, this one included (0 when off).                 */
u32  bw_link_players(void);

/* 1 while the console has a radio: a Link session is open, or a relay is
 * set (the shared air, joined when the game turns its radio on).  The
 * game's APP_ENV_IsWirelessAvailable answers it.                          */
int  bw_link_wireless_on(void);

/* 1 while the console is in one of metaxy's public rooms (the air).      */
int  bw_link_on_air(void);

/* Each frame, on the game's thread: notices the end of the session.      */
void bw_link_frame(void);

/* The console's wireless MAC address in a Link: the firmware's, unless that
 * is the port's default (every profile has it), in which case a random
 * one kept in <save dir>/link_mac.txt, so two players never share one.   */
void bw_link_mac(u8 out[6]);

#ifdef __cplusplus
}
#endif

#endif
