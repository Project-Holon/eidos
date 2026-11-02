/*
 * tests/test_net_link.c -- sdk/net_link.c over an in-process relay
 * (docs/link-play.md, phase 1): scanning, joining, data sharing with the
 * network's delay, messages, leaving.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_link.h"

static int g_checks, g_fails;

#define CHECK(name, cond) do { g_checks++; if (!(cond)) { g_fails++; \
    fprintf(stderr, "  FAIL %s (line %d)\n", name, __LINE__); } } while (0)

/* The relay: every message waits `latency` frames, then reaches its peer
 * (peer 0: everyone but the sender). */
#define PEERS 4
#define QMAX 4096

typedef struct Pending {
    u32 from, to, due, n;
    u8 bytes[2600];
} Pending;

static Pending *g_q;
static u32 g_nq, g_now, g_latency, g_sent;

typedef struct End {
    u32 peer;
} End;

static End g_end[PEERS];

static int relay_send(void *user, u32 peer, const u8 *b, u32 n)
{
    End *e = (End *)user;
    u32 p;

    g_sent++;
    for (p = 1; p <= PEERS; p++) {
        if ((peer == 0 && p != e->peer) || peer == p) {
            Pending *q;

            if (g_nq >= QMAX || n > sizeof q->bytes) {
                return 0;
            }
            q = &g_q[g_nq++];
            q->from = e->peer;
            q->to = p;
            q->due = g_now + g_latency;
            q->n = n;
            memcpy(q->bytes, b, n);
        }
    }
    return 1;
}

static int relay_recv(void *user, u32 *peer, u8 *b, u32 cap, u32 *n)
{
    End *e = (End *)user;
    u32 i;

    for (i = 0; i < g_nq; i++) {
        if (g_q[i].to == e->peer && g_q[i].due <= g_now) {
            *peer = g_q[i].from;
            *n = g_q[i].n < cap ? g_q[i].n : cap;
            memcpy(b, g_q[i].bytes, *n);
            memmove(&g_q[i], &g_q[i + 1], (g_nq - i - 1) * sizeof g_q[0]);
            g_nq--;
            return 1;
        }
    }
    return 0;
}

static NetLink *make(u32 peer)
{
    NetLinkTransport t;

    g_end[peer - 1].peer = peer;
    t.user = &g_end[peer - 1];
    t.send = relay_send;
    t.recv = relay_recv;
    return net_link_create(&t, peer);
}

static void tick(NetLink **l, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        net_link_update(l[i]);
    }
    g_now++;
}

/* Three players: a parent and two children, frames carried with `latency`
 * frames of delay; each player steps every frame, and a read frame f must
 * hold every player's block of step f - NET_LINK_DELAY. */
static void run(u32 latency, const char *label)
{
    NetLink *l[3];
    u8 beacon[8] = { 'U', 'N', 'I', 'O', 'N', 0, 1, 2 };
    u32 i, f, steps[3] = { 0, 0, 0 }, frames = 600, bad = 0, stalls = 0;
    char name[96];

    g_nq = 0;
    g_now = 0;
    g_latency = latency;
    g_sent = 0;
    for (i = 0; i < 3; i++) {
        l[i] = make(i + 1);
    }
    snprintf(name, sizeof name, "%s: parent starts", label);
    CHECK(name, net_link_start_parent(l[0], beacon, sizeof beacon, 5));
    net_link_start_scan(l[1]);
    net_link_start_scan(l[2]);
    for (i = 0; i < 40; i++) {
        tick(l, 3);
    }
    snprintf(name, sizeof name, "%s: the children hear the beacon", label);
    CHECK(name, net_link_scan_count(l[1]) == 1 && net_link_scan_count(l[2]) == 1);
    {
        u32 sz = 0;
        const u8 *b = net_link_scan_beacon(l[1], 0, &sz);

        snprintf(name, sizeof name, "%s: the beacon's bytes", label);
        CHECK(name, b != NULL && sz == sizeof beacon && memcmp(b, beacon, sz) == 0);
    }
    net_link_join(l[1], 0);
    net_link_join(l[2], 0);
    for (i = 0; i < 4 * latency + 10; i++) {
        tick(l, 3);
    }
    snprintf(name, sizeof name, "%s: aids 0 1 2", label);
    CHECK(name, net_link_own_aid(l[0]) == 0 && net_link_own_aid(l[1]) >= 1 && net_link_own_aid(l[2]) >= 1
                && net_link_own_aid(l[1]) != net_link_own_aid(l[2]));
    snprintf(name, sizeof name, "%s: everyone sees three players", label);
    CHECK(name, net_link_aid_bitmap(l[0]) == 7 && net_link_aid_bitmap(l[1]) == 7 && net_link_aid_bitmap(l[2]) == 7);

    for (i = 0; i < 3; i++) {
        net_link_ds_start(l[i], 24);
    }
    for (f = 0; f < frames; f++) {
        for (i = 0; i < 3; i++) {
            u8 block[24];
            int aid = net_link_own_aid(l[i]);

            memset(block, 0, sizeof block);
            block[0] = (u8)aid;
            block[1] = (u8)steps[i];
            block[2] = (u8)(steps[i] >> 8);
            if (net_link_ds_step(l[i], block)) {
                u32 a, rf = steps[i];

                for (a = 0; a < 3; a++) {
                    const u8 *r = net_link_ds_block(l[i], a);
                    u32 want = rf >= NET_LINK_DELAY ? rf - NET_LINK_DELAY : 0;

                    if (r == NULL) {
                        bad++;
                    } else if (rf < NET_LINK_DELAY) {
                        bad += r[0] != 0 || r[1] != 0;
                    } else if (r[0] != a || (u32)(r[1] | r[2] << 8) != want) {
                        bad++;
                    }
                }
                steps[i]++;
            } else {
                stalls++;
            }
        }
        tick(l, 3);
    }
    snprintf(name, sizeof name, "%s: every read frame holds the right blocks", label);
    CHECK(name, bad == 0);
    snprintf(name, sizeof name, "%s: the frames move (%u, %u, %u of %u)", label, steps[0], steps[1], steps[2], frames);
    CHECK(name, steps[0] > frames * 8 / 10 && steps[1] > frames * 8 / 10 && steps[2] > frames * 8 / 10);
    snprintf(name, sizeof name, "%s: under metaxy's rate (%u messages in %u frames, 3 players)", label, g_sent,
             g_now);
    CHECK(name, g_sent * 60u / g_now / 3u <= 40u);

    /* messages: a child to the parent, the parent to the children */
    {
        u8 m[16], got[16];
        int from;
        u32 sz;

        memcpy(m, "trade:pokemon01", 16);
        net_link_send(l[1], m, 16);
        net_link_send(l[0], (const u8 *)"accept-trade-ok", 16);
        for (i = 0; i < latency + 3; i++) {
            tick(l, 3);
        }
        snprintf(name, sizeof name, "%s: the parent has the child's message", label);
        CHECK(name, net_link_recv(l[0], &from, got, sizeof got, &sz) && sz == 16 && memcmp(got, m, 16) == 0
                    && from == net_link_own_aid(l[1]));
        snprintf(name, sizeof name, "%s: both children have the parent's", label);
        CHECK(name, net_link_recv(l[1], &from, got, sizeof got, &sz) && from == 0
                    && memcmp(got, "accept-trade-ok", 16) == 0
                    && net_link_recv(l[2], &from, got, sizeof got, &sz) && from == 0);
    }

    /* a child leaves: the others drop it and the frames go on */
    net_link_leave(l[2]);
    for (i = 0; i < 2 * latency + 4; i++) {
        tick(l, 3);
    }
    snprintf(name, sizeof name, "%s: the parent drops the child that left", label);
    CHECK(name, net_link_aid_bitmap(l[0]) == 3);
    snprintf(name, sizeof name, "%s: the other child sees it gone", label);
    CHECK(name, net_link_aid_bitmap(l[1]) == 3);
    {
        u32 before = steps[0], k;
        u8 block[24];

        memset(block, 0, sizeof block);
        for (k = 0; k < 120; k++) {
            if (net_link_ds_step(l[0], block)) {
                steps[0]++;
            }
            (void)net_link_ds_step(l[1], block);
            tick(l, 2);
        }
        snprintf(name, sizeof name, "%s: the two left keep sharing frames", label);
        CHECK(name, steps[0] > before + 60);
    }
    /* the parent leaves: the child's Link is over */
    net_link_leave(l[0]);
    for (i = 0; i < latency + 3; i++) {
        tick(l, 2);
    }
    snprintf(name, sizeof name, "%s: the child sees the Link end", label);
    CHECK(name, net_link_state(l[1]) == NET_LINK_ENDED);
    for (i = 0; i < 3; i++) {
        net_link_destroy(l[i]);
    }
}

int main(void)
{
    g_q = (Pending *)calloc(QMAX, sizeof *g_q);
    if (g_q == NULL) {
        return 1;
    }
    run(0, "next frame");
    run(3, "50 ms");
    run(6, "100 ms");
    fprintf(stderr, "test_net_link: %d checks, %d failures\n", g_checks, g_fails);
    free(g_q);
    return g_fails != 0;
}
