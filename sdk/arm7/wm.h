/*
 * sdk/arm7/wm.h -- the ARM7's wireless manager (PXI tag 10) over a relay.
 *
 * The DS's local wireless (trades and battles in the Union Room, the
 * Entralink with friends, the C-Gear's nearby players) is the SDK's WM
 * library on the ARM9 talking to the radio firmware on the ARM7.  The ARM9
 * side is the game's own matched code (src/game/arm9/wm_*.c) and runs
 * unchanged; sdk/arm7/wm.c answers its commands as the firmware does, and
 * carries the radio's frames over an "air": a broadcast channel that every
 * console in a Link session shares (sdk/link.c gives the relay's,
 * tests give an in-process one).  docs/link-play.md is the design.
 *
 * With no air the tag stays unregistered, which is the SDK's own
 * "wireless not available" (sdk/arm7/arm7.h): the game sees a DS with its
 * wireless off, as it always has in the port.
 */
#ifndef BW_PORT_ARM7_WM_H
#define BW_PORT_ARM7_WM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The largest air message wm.c sends (the relay takes 4 KB).             */
#define BW_WM_AIR_MAX 3800u

typedef struct BwWmAir {
    void *user;
    /* Send `n` bytes to every other console on the air. 0 on success.    */
    int (*send)(void *user, const u8 *bytes, u32 n);
    /* The next message heard, 1 when there was one, 0 when none.         */
    int (*recv)(void *user, u8 *bytes, u32 cap, u32 *n);
    /* The game turned its radio on (WM initialised) or off (WM ended);
     * NULL when the air does not care.                                     */
    void (*power)(void *user, int on);
} BwWmAir;

/*
 * Put the console on an air (NULL takes it off).  With an air the WM tag
 * is registered, so the next WM_Init succeeds; without one, a running WM
 * loses every link (the others are gone, as out of range) and the next
 * WM_Init answers "wireless not available".
 */
void bw_arm7_wm_set_air(const BwWmAir *air);
int  bw_arm7_wm_has_air(void);

/* The responder's life cycle (sdk/arm7/arm7.c calls these).              */
void bw_arm7_wm_init(void);
void bw_arm7_wm_reset(void);
void bw_arm7_wm_step(void);

/* What the radio is doing, for the port's screens and the tests.          */
typedef struct BwWmInfo {
    u16 state;          /* the WM state (WMStatus.state)                    */
    u16 aid;            /* own aid (0 a parent)                             */
    u16 child_bitmap;   /* a parent's connected children                    */
    u16 parents_heard;  /* parents whose beacon came in the last 3 s        */
    u32 sent, heard;    /* air messages                                     */
} BwWmInfo;
void bw_arm7_wm_info(BwWmInfo *out);

#ifdef __cplusplus
}
#endif

#endif
