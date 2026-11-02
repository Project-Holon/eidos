/*
 * sdk/net_link.c -- the game's local wireless over a relay (sdk/net_link.h,
 * docs/link-play.md).
 *
 * Messages, the first byte their kind:
 *   'B' beacon      max_players u8, size u16, bytes   (a parent, to everyone)
 *   'J' join                                          (a child, to a parent)
 *   'R' refuse                                        (a parent, to a child)
 *   'M' members     bitmap u8, then a u32 peer per aid (a parent, to its members)
 *   'D' frames      first u32, count u8, size u16, blocks (to the members)
 *   'G' message     size u16, bytes                   (child to parent, parent to children)
 *   'L' leave
 * Numbers are little endian.
 */

#include <stdlib.h>
#include <string.h>

#include "net_link.h"

#define RING 64u                     /* frames kept per aid; > DELAY + BATCH */
#define INBOX 32u

typedef struct ScanEntry {
    u32 peer;
    u32 size;
    u8 beacon[NET_LINK_MAX_BEACON];
} ScanEntry;

typedef struct Msg {
    int aid;
    u32 size;
    u8 bytes[NET_LINK_MAX_BLOCK];
} Msg;

struct NetLink {
    NetLinkTransport t;
    u32 self;
    NetLinkState state;
    /* a parent */
    u8 beacon[NET_LINK_MAX_BEACON];
    u32 beacon_size, max_players;
    int accept;
    u32 beacon_age;
    /* a child */
    ScanEntry scan[NET_LINK_MAX_SCAN];
    u32 nscan;
    u32 parent;
    /* both */
    int aid;
    u32 bitmap;
    u32 peer_of[NET_LINK_MAX_PLAYERS];
    /* data sharing */
    int ds_on;
    u32 block_size, ds_bitmap;
    u32 next_read;                   /* the frame the next step reads     */
    u8 ring[NET_LINK_MAX_PLAYERS][RING][NET_LINK_MAX_BLOCK];
    u32 have[NET_LINK_MAX_PLAYERS][RING];   /* frame + 1 held in each slot */
    u8 read[NET_LINK_MAX_PLAYERS][NET_LINK_MAX_BLOCK];
    u32 queued_first, queued;        /* own frames not sent yet           */
    int stalled;
    /* messages in */
    Msg inbox[INBOX];
    u32 in_head, in_count;
};

static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { put16(p, v); put16(p + 2, v >> 16); }
static u32 get16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 get32(const u8 *p) { return get16(p) | get16(p + 2) << 16; }

NetLink *net_link_create(const NetLinkTransport *t, u32 self_peer)
{
    NetLink *l = (NetLink *)calloc(1, sizeof *l);

    if (l == NULL || t == NULL) {
        free(l);
        return NULL;
    }
    l->t = *t;
    l->self = self_peer;
    l->aid = -1;
    l->accept = 1;
    return l;
}

void net_link_destroy(NetLink *l)
{
    free(l);
}

static void send_to(NetLink *l, u32 peer, const u8 *b, u32 n)
{
    if (l->t.send != NULL) {
        (void)l->t.send(l->t.user, peer, b, n);
    }
}

/* to every other member (the parent: its children; a child: everyone the
 * parent listed) */
static void send_members(NetLink *l, const u8 *b, u32 n)
{
    u32 a;

    for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
        if ((l->bitmap & (1u << a)) && (int)a != l->aid && l->peer_of[a] != 0) {
            send_to(l, l->peer_of[a], b, n);
        }
    }
}

static int aid_of_peer(const NetLink *l, u32 peer)
{
    u32 a;

    for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
        if ((l->bitmap & (1u << a)) && l->peer_of[a] == peer) {
            return (int)a;
        }
    }
    return -1;
}

static void send_beacon(NetLink *l, u32 peer)
{
    u8 b[4 + NET_LINK_MAX_BEACON];

    b[0] = 'B';
    b[1] = (u8)l->max_players;
    put16(b + 2, l->beacon_size);
    memcpy(b + 4, l->beacon, l->beacon_size);
    send_to(l, peer, b, 4 + l->beacon_size);
}

static void send_members_list(NetLink *l)
{
    u8 b[2 + 4 * NET_LINK_MAX_PLAYERS];
    u32 a;

    b[0] = 'M';
    b[1] = (u8)l->bitmap;
    for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
        put32(b + 2 + 4 * a, l->peer_of[a]);
    }
    send_members(l, b, sizeof b);
}

int net_link_start_parent(NetLink *l, const u8 *beacon, u32 size, u32 max_players)
{
    if (l == NULL || size > NET_LINK_MAX_BEACON || max_players < 2 || max_players > NET_LINK_MAX_PLAYERS) {
        return 0;
    }
    memcpy(l->beacon, beacon, size);
    l->beacon_size = size;
    l->max_players = max_players;
    l->state = NET_LINK_PARENT;
    l->aid = 0;
    l->bitmap = 1;
    memset(l->peer_of, 0, sizeof l->peer_of);
    l->peer_of[0] = l->self;
    l->beacon_age = 0;
    send_beacon(l, 0);
    return 1;
}

int net_link_start_scan(NetLink *l)
{
    if (l == NULL) {
        return 0;
    }
    l->state = NET_LINK_SCANNING;
    return 1;
}

u32 net_link_scan_count(const NetLink *l)
{
    return l != NULL ? l->nscan : 0;
}

const u8 *net_link_scan_beacon(const NetLink *l, u32 index, u32 *size)
{
    if (l == NULL || index >= l->nscan) {
        return NULL;
    }
    if (size != NULL) {
        *size = l->scan[index].size;
    }
    return l->scan[index].beacon;
}

u32 net_link_scan_peer(const NetLink *l, u32 index)
{
    return l != NULL && index < l->nscan ? l->scan[index].peer : 0;
}

void net_link_clear_scan(NetLink *l)
{
    if (l != NULL) {
        l->nscan = 0;
    }
}

int net_link_join(NetLink *l, u32 index)
{
    u8 b = 'J';

    if (l == NULL || index >= l->nscan) {
        return 0;
    }
    l->parent = l->scan[index].peer;
    l->state = NET_LINK_JOINING;
    send_to(l, l->parent, &b, 1);
    return 1;
}

void net_link_set_accept(NetLink *l, int accept)
{
    if (l != NULL) {
        l->accept = accept != 0;
    }
}

NetLinkState net_link_state(const NetLink *l)
{
    return l != NULL ? l->state : NET_LINK_ERROR;
}

int net_link_own_aid(const NetLink *l)
{
    return l != NULL ? l->aid : -1;
}

u32 net_link_aid_bitmap(const NetLink *l)
{
    return l != NULL ? l->bitmap : 0;
}

int net_link_ds_start(NetLink *l, u32 block_size)
{
    if (l == NULL || l->aid < 0 || block_size == 0 || block_size > NET_LINK_MAX_BLOCK) {
        return 0;
    }
    l->ds_on = 1;
    l->block_size = block_size;
    l->ds_bitmap = l->bitmap;
    l->next_read = 0;
    l->queued = 0;
    l->queued_first = NET_LINK_DELAY;
    memset(l->have, 0, sizeof l->have);
    memset(l->read, 0, sizeof l->read);
    return 1;
}

void net_link_ds_end(NetLink *l)
{
    if (l != NULL) {
        l->ds_on = 0;
    }
}

int net_link_ds_active(const NetLink *l)
{
    return l != NULL && l->ds_on;
}

static int frame_held(const NetLink *l, u32 aid, u32 f)
{
    return f < NET_LINK_DELAY || l->have[aid][f % RING] == f + 1u;
}

static void flush_frames(NetLink *l)
{
    u8 b[8 + NET_LINK_BATCH * NET_LINK_MAX_BLOCK];
    u32 i, n = l->queued > NET_LINK_BATCH ? NET_LINK_BATCH : l->queued;

    while (n > 0) {
        b[0] = 'D';
        put32(b + 1, l->queued_first);
        b[5] = (u8)n;
        put16(b + 6, l->block_size);
        for (i = 0; i < n; i++) {
            u32 f = l->queued_first + i;

            memcpy(b + 8 + i * l->block_size, l->ring[l->aid][f % RING], l->block_size);
        }
        send_members(l, b, 8 + n * l->block_size);
        l->queued_first += n;
        l->queued -= n;
        n = l->queued > NET_LINK_BATCH ? NET_LINK_BATCH : l->queued;
    }
}

int net_link_ds_step(NetLink *l, const u8 *block)
{
    u32 a, f;

    if (l == NULL || !l->ds_on || l->aid < 0) {
        return 0;
    }
    f = l->next_read;
    for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
        if ((l->ds_bitmap & (1u << a)) && (int)a != l->aid && !frame_held(l, a, f)) {
            l->stalled = 1;          /* not yet: the frames we hold go out */
            return 0;
        }
    }
    for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
        if (!(l->ds_bitmap & (1u << a))) {
            memset(l->read[a], 0, l->block_size);
        } else if (f < NET_LINK_DELAY) {
            memset(l->read[a], 0, l->block_size);
        } else {
            memcpy(l->read[a], l->ring[a][f % RING], l->block_size);
        }
    }
    /* this step's block is read NET_LINK_DELAY frames from now */
    memcpy(l->ring[l->aid][(f + NET_LINK_DELAY) % RING], block, l->block_size);
    l->have[l->aid][(f + NET_LINK_DELAY) % RING] = f + NET_LINK_DELAY + 1u;
    l->queued++;
    l->next_read = f + 1;
    l->stalled = 0;
    return 1;
}

const u8 *net_link_ds_block(const NetLink *l, u32 aid)
{
    if (l == NULL || aid >= NET_LINK_MAX_PLAYERS || !(l->ds_bitmap & (1u << aid))) {
        return NULL;
    }
    return l->read[aid];
}

int net_link_send(NetLink *l, const u8 *bytes, u32 size)
{
    u8 b[3 + NET_LINK_MAX_BLOCK];

    if (l == NULL || l->aid < 0 || size > NET_LINK_MAX_BLOCK) {
        return 0;
    }
    b[0] = 'G';
    put16(b + 1, size);
    memcpy(b + 3, bytes, size);
    if (l->aid == 0) {
        send_members(l, b, 3 + size);
    } else {
        send_to(l, l->peer_of[0], b, 3 + size);
    }
    return 1;
}

int net_link_recv(NetLink *l, int *from_aid, u8 *bytes, u32 cap, u32 *size)
{
    Msg *m;

    if (l == NULL || l->in_count == 0) {
        return 0;
    }
    m = &l->inbox[l->in_head];
    if (from_aid != NULL) {
        *from_aid = m->aid;
    }
    if (size != NULL) {
        *size = m->size;
    }
    memcpy(bytes, m->bytes, m->size < cap ? m->size : cap);
    l->in_head = (l->in_head + 1) % INBOX;
    l->in_count--;
    return 1;
}

static void drop_member(NetLink *l, int aid)
{
    if (aid <= 0 || aid >= NET_LINK_MAX_PLAYERS) {
        return;
    }
    l->bitmap &= ~(1u << aid);
    l->ds_bitmap &= ~(1u << aid);
    l->peer_of[aid] = 0;
}

void net_link_leave(NetLink *l)
{
    u8 b = 'L';

    if (l == NULL) {
        return;
    }
    if (l->aid >= 0) {
        send_members(l, &b, 1);
    }
    l->state = NET_LINK_ENDED;
    l->aid = -1;
    l->bitmap = 0;
    l->ds_on = 0;
}

static void take(NetLink *l, u32 peer, const u8 *b, u32 n)
{
    int from = aid_of_peer(l, peer);

    switch (b[0]) {
    case 'B':
        if (l->state == NET_LINK_SCANNING && n >= 4 && get16(b + 2) <= NET_LINK_MAX_BEACON
            && 4 + get16(b + 2) <= n) {
            u32 i;

            for (i = 0; i < l->nscan && l->scan[i].peer != peer; i++) {
            }
            if (i == l->nscan && l->nscan < NET_LINK_MAX_SCAN) {
                l->nscan++;
            }
            if (i < l->nscan) {
                l->scan[i].peer = peer;
                l->scan[i].size = get16(b + 2);
                memcpy(l->scan[i].beacon, b + 4, l->scan[i].size);
            }
        }
        break;
    case 'J':
        if (l->state == NET_LINK_PARENT) {
            u32 a, count = 0;
            int got = aid_of_peer(l, peer);

            for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
                count += (l->bitmap >> a) & 1u;
            }
            if (got < 0 && l->accept && count < l->max_players) {
                for (a = 1; a < NET_LINK_MAX_PLAYERS && (l->bitmap & (1u << a)); a++) {
                }
                l->bitmap |= 1u << a;
                l->peer_of[a] = peer;
                got = (int)a;
            }
            if (got < 0) {
                u8 r = 'R';

                send_to(l, peer, &r, 1);
            } else {
                send_members_list(l);
            }
        }
        break;
    case 'R':
        if (l->state == NET_LINK_JOINING && peer == l->parent) {
            l->state = NET_LINK_SCANNING;
        }
        break;
    case 'M':
        if ((l->state == NET_LINK_JOINING || l->state == NET_LINK_CONNECTED) && peer == l->parent
            && n >= 2 + 4 * NET_LINK_MAX_PLAYERS) {
            u32 a;

            l->bitmap = b[1];
            l->aid = -1;
            for (a = 0; a < NET_LINK_MAX_PLAYERS; a++) {
                l->peer_of[a] = get32(b + 2 + 4 * a);
                if ((l->bitmap & (1u << a)) && l->peer_of[a] == l->self) {
                    l->aid = (int)a;
                }
            }
            l->ds_bitmap &= l->bitmap;
            l->state = l->aid > 0 ? NET_LINK_CONNECTED : NET_LINK_SCANNING;
        }
        break;
    case 'D':
        if (from >= 0 && from != l->aid && n >= 8) {
            u32 first = get32(b + 1), count = b[5], size = get16(b + 6), i;

            if (size == l->block_size && 8 + count * size <= n) {
                for (i = 0; i < count; i++) {
                    u32 f = first + i;

                    memcpy(l->ring[from][f % RING], b + 8 + i * size, size);
                    l->have[from][f % RING] = f + 1u;
                }
            }
        }
        break;
    case 'G':
        if (from >= 0 && n >= 3 && get16(b + 1) <= NET_LINK_MAX_BLOCK && 3 + get16(b + 1) <= n
            && l->in_count < INBOX) {
            Msg *m = &l->inbox[(l->in_head + l->in_count) % INBOX];

            m->aid = from;
            m->size = get16(b + 1);
            memcpy(m->bytes, b + 3, m->size);
            l->in_count++;
        }
        break;
    case 'L':
        if (from == 0 && l->aid > 0) {
            l->state = NET_LINK_ENDED;       /* the parent went: the Link is over */
            l->aid = -1;
            l->bitmap = 0;
            l->ds_on = 0;
        } else if (from > 0 && l->aid == 0) {
            drop_member(l, from);
            send_members_list(l);
        } else if (from > 0) {
            drop_member(l, from);
        }
        break;
    default:
        break;
    }
}

void net_link_update(NetLink *l)
{
    u8 buf[16 + NET_LINK_BATCH * NET_LINK_MAX_BLOCK];
    u32 peer, n;

    if (l == NULL) {
        return;
    }
    while (l->t.recv != NULL && l->t.recv(l->t.user, &peer, buf, sizeof buf, &n)) {
        if (n > 0 && peer != l->self) {
            take(l, peer, buf, n);
        }
    }
    /* a parent repeats its beacon twice a second for children that scan later */
    if (l->state == NET_LINK_PARENT && ++l->beacon_age >= 30) {
        l->beacon_age = 0;
        send_beacon(l, 0);
    }
    if (l->ds_on && l->aid >= 0 && (l->queued >= NET_LINK_BATCH || (l->stalled && l->queued > 0))) {
        flush_frames(l);
    }
}
