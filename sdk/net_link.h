/*
 * sdk/net_link.h -- the game's local wireless over a relay (docs/link-play.md).
 *
 * A NetLink is one player's end of a Link: a parent that others join, or a
 * child that scans for parents and joins one.  It knows nothing of the game
 * (the device table, stubs/net_link_device.c, sits on it) and nothing of
 * the network (a NetLinkTransport carries its messages: metaxy in the game,
 * an in-process relay in tests/test_net_link.c).
 *
 * Data sharing is the radio features' lockstep: each frame every connected
 * player contributes a block of `block_size` bytes and every player reads
 * all of them.  Over the internet a frame comes late, so the blocks a step
 * hands in are the ones the game reads NET_LINK_DELAY frames later; the
 * first NET_LINK_DELAY frames read as zeros.  A step that finds a player's
 * frame missing reports "not yet" and takes nothing, as the DS's data
 * sharing does when the radio drops a frame; the game hands the same block
 * in again next frame.
 */
#ifndef BW_NET_LINK_H
#define BW_NET_LINK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_LINK_MAX_PLAYERS 5       /* the Union Room's five             */
#define NET_LINK_MAX_BLOCK   512     /* a parent's data-sharing block     */
#define NET_LINK_MAX_BEACON  128
#define NET_LINK_MAX_SCAN    16      /* the radio device's scan list      */
#define NET_LINK_DELAY       12      /* frames between a step and its read */
#define NET_LINK_BATCH       4       /* data-sharing frames per message   */

typedef struct NetLinkTransport {
    void *user;
    /* send `n` bytes to `peer` (0: every other player of the session) */
    int (*send)(void *user, u32 peer, const u8 *bytes, u32 n);
    /* the next message, or 0 when none: its sender and size */
    int (*recv)(void *user, u32 *peer, u8 *bytes, u32 cap, u32 *n);
} NetLinkTransport;

typedef enum NetLinkState {
    NET_LINK_IDLE = 0,
    NET_LINK_PARENT,                 /* announcing, children may join     */
    NET_LINK_SCANNING,
    NET_LINK_JOINING,
    NET_LINK_CONNECTED,              /* a child with its aid              */
    NET_LINK_ENDED,
    NET_LINK_ERROR
} NetLinkState;

typedef struct NetLink NetLink;

NetLink *net_link_create(const NetLinkTransport *t, u32 self_peer);
void net_link_destroy(NetLink *l);

/* Each frame: send what is due, take in the peers' messages. */
void net_link_update(NetLink *l);

/* A parent: announce `beacon` to the session; children join up to
 * `max_players` (parent included). */
int net_link_start_parent(NetLink *l, const u8 *beacon, u32 size, u32 max_players);
/* A child: listen for parents; net_link_scan_* list them. */
int net_link_start_scan(NetLink *l);
u32 net_link_scan_count(const NetLink *l);
const u8 *net_link_scan_beacon(const NetLink *l, u32 index, u32 *size);
u32 net_link_scan_peer(const NetLink *l, u32 index);
void net_link_clear_scan(NetLink *l);
/* A child: ask the parent of scan entry `index` to take it. */
int net_link_join(NetLink *l, u32 index);
/* A parent: whether a child that asks is taken (1, the default) or refused. */
void net_link_set_accept(NetLink *l, int accept);

NetLinkState net_link_state(const NetLink *l);
int net_link_own_aid(const NetLink *l);          /* -1 before it has one   */
u32 net_link_aid_bitmap(const NetLink *l);       /* bit n: aid n connected */

/* Data sharing among the connected aids with `block_size` bytes each. */
int net_link_ds_start(NetLink *l, u32 block_size);
void net_link_ds_end(NetLink *l);
int net_link_ds_active(const NetLink *l);
/* Hand in this frame's block; 1 when a frame's blocks are ready to read
 * (net_link_ds_block), 0 when a player's frame has not come yet. */
int net_link_ds_step(NetLink *l, const u8 *block);
const u8 *net_link_ds_block(const NetLink *l, u32 aid);  /* NULL: no such aid */

/* A message for the other connected players (to the parent from a child,
 * to every child from the parent); delivered in order. */
int net_link_send(NetLink *l, const u8 *bytes, u32 size);
/* The next message received: its sender's aid, or 0 when none. */
int net_link_recv(NetLink *l, int *from_aid, u8 *bytes, u32 cap, u32 *size);

/* Leave the Link (the others see this player gone). */
void net_link_leave(NetLink *l);

#ifdef __cplusplus
}
#endif

#endif
