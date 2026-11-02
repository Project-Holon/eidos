/*
 * sdk/link.c -- Link sessions (sdk/link.h, docs/link-play.md).
 *
 * The session is the relay client's (sdk/mod_net.c); this file puts the
 * console's radio (sdk/arm7/wm.c) on it.  The radio's air messages go to
 * every other player under BW_MOD_NET_LINK_KEY, and the ones that come in
 * wait in a queue the radio reads at its next step.  Messages reach this
 * file on the game's frame (bw_mod_net_frame), the radio reads them on the
 * game's thread too; the queue has a lock all the same, for a port whose
 * frame hook runs elsewhere.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "arm7/wm.h"
#include "dsmem.h"
#include "firmware.h"
#include "mod_net.h"
#include "mp.h"
#include "plat_thread.h"
#include "save.h"

#define QUEUE_MAX 128

typedef struct Msg {
    u32 n;
    u8  bytes[BW_WM_AIR_MAX];
} Msg;

static struct {
    BwLinkState state;
    int host;
    char code[40];
    char reason[64];
    BwMutex *lock;
    Msg *q;
    u32 q_head, q_count;
    u32 dropped;
    int was_active;
} g_link;

static void lock(void)
{
    if (g_link.lock == NULL)
        g_link.lock = bw_mutex_create();
    if (g_link.lock != NULL)
        bw_mutex_lock(g_link.lock);
}

static void unlock(void)
{
    if (g_link.lock != NULL)
        bw_mutex_unlock(g_link.lock);
}

/* ------------------------------------------------------------------ */
/* The air over the relay                                              */
/* ------------------------------------------------------------------ */

static int air_send(void *user, const u8 *bytes, u32 n)
{
    (void)user;
    return bw_mod_net_link_send(0, bytes, n) == 0 ? 0 : -1;
}

static int air_recv(void *user, u8 *bytes, u32 cap, u32 *n)
{
    int got = 0;

    (void)user;
    lock();
    if (g_link.q_count > 0) {
        Msg *m = &g_link.q[g_link.q_head];
        u32 k = m->n < cap ? m->n : cap;

        memcpy(bytes, m->bytes, k);
        *n = k;
        g_link.q_head = (g_link.q_head + 1u) % QUEUE_MAX;
        g_link.q_count--;
        got = 1;
    }
    unlock();
    return got;
}

static void on_link(u32 peer, int kind, const void *buf, u32 size)
{
    (void)peer;
    if (kind != BW_SESSION_MESSAGE || buf == NULL || size == 0 || size > BW_WM_AIR_MAX)
        return;
    lock();
    if (g_link.q != NULL && g_link.q_count < QUEUE_MAX) {
        Msg *m = &g_link.q[(g_link.q_head + g_link.q_count) % QUEUE_MAX];

        m->n = size;
        memcpy(m->bytes, buf, size);
        g_link.q_count++;
    } else {
        g_link.dropped++;
    }
    unlock();
}

static const BwWmAir k_air = { NULL, air_send, air_recv };

/* ------------------------------------------------------------------ */
/* The MAC address                                                     */
/* ------------------------------------------------------------------ */

static int parse_mac(const char *s, u8 out[6])
{
    unsigned v[6];

    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return 0;
    for (int i = 0; i < 6; i++)
        out[i] = (u8)v[i];
    return 1;
}

void bw_link_mac(u8 out[6])
{
    static const u8 port_default[6] = { 0x02, 0, 0, 0, 0, 0x01 };
    char path[512], text[64];
    long n;
    u64 t;

    memcpy(out, bw_firmware_settings()->mac, 6);
    if (memcmp(out, port_default, 6) != 0)
        return;
    if (bw_save_path(path, sizeof path, "link_mac.txt") == 0) {
        n = bw_save_read(path, (u8 *)text, sizeof text - 1u);
        if (n > 0) {
            text[n] = '\0';
            if (parse_mac(text, out))
                return;
        }
    }
    /* A locally administered unicast address, random once per profile.   */
    t = bw_clock_ns();
    srand((unsigned)(t ^ (t >> 32)));
    out[0] = 0x02;
    for (int i = 1; i < 6; i++)
        out[i] = (u8)((rand() >> 3) ^ (t >> (i * 8)));
    if (path[0] != '\0') {
        int k = snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X\n",
                         out[0], out[1], out[2], out[3], out[4], out[5]);

        (void)bw_save_write_atomic(path, text, (u32)k);
    }
}

/* ------------------------------------------------------------------ */
/* The session                                                         */
/* ------------------------------------------------------------------ */

static void radio_on(void)
{
    u8 mac[6];

    lock();
    if (g_link.q == NULL)
        g_link.q = (Msg *)calloc(QUEUE_MAX, sizeof(Msg));
    g_link.q_head = 0;
    g_link.q_count = 0;
    unlock();
    bw_link_mac(mac);
    memcpy(bw_ds_ptr(BW_FW_MAC_ADDR), mac, 6);
    bw_mod_net_set_link_handler(on_link);
    bw_arm7_wm_set_air(&k_air);
    g_link.state = BW_LINK_OPEN;
    g_link.was_active = 1;
    g_link.reason[0] = '\0';
}

static void radio_off(void)
{
    bw_arm7_wm_set_air(NULL);
    bw_mod_net_set_link_handler(NULL);
    lock();
    g_link.q_count = 0;
    unlock();
}

int bw_link_host(char *code, u32 size)
{
    char got[40];
    int rc;

    if (bw_mp_state() != BW_MP_OFF && bw_mp_state() != BW_MP_ENDED)
        return -1;
    if (g_link.state == BW_LINK_OPEN)
        bw_link_leave();
    rc = bw_mod_net_session_host(got, sizeof got);
    if (rc != 0)
        return rc;
    snprintf(g_link.code, sizeof g_link.code, "%s", got);
    g_link.host = 1;
    radio_on();
    if (code != NULL && size > 0)
        snprintf(code, size, "%s", got);
    return 0;
}

int bw_link_join(const char *code)
{
    int rc;

    if (code == NULL)
        return -1;
    if (bw_mp_state() != BW_MP_OFF && bw_mp_state() != BW_MP_ENDED)
        return -1;
    if (g_link.state == BW_LINK_OPEN)
        bw_link_leave();
    rc = bw_mod_net_session_join(code);
    if (rc != 0)
        return rc;
    snprintf(g_link.code, sizeof g_link.code, "%s", code);
    {
        char *e = g_link.code + strlen(g_link.code);

        while (e > g_link.code && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
            *--e = '\0';
    }
    g_link.host = 0;
    radio_on();
    return 0;
}

void bw_link_leave(void)
{
    if (g_link.state != BW_LINK_OPEN)
        return;
    radio_off();
    bw_mod_net_session_leave();
    g_link.state = BW_LINK_OFF;
    g_link.was_active = 0;
}

void bw_link_frame(void)
{
    if (g_link.state != BW_LINK_OPEN)
        return;
    if (!bw_mod_net_session_active()) {
        radio_off();
        g_link.state = BW_LINK_ENDED;
        snprintf(g_link.reason, sizeof g_link.reason, "%s",
                 bw_mod_net_last_error()[0] != '\0' ? bw_mod_net_last_error() : "lost");
    }
}

BwLinkState bw_link_state(void) { return g_link.state; }
int bw_link_is_host(void) { return g_link.state == BW_LINK_OPEN && g_link.host; }
const char *bw_link_code(void) { return g_link.code; }
const char *bw_link_end_reason(void) { return g_link.reason; }

u32 bw_link_players(void)
{
    return g_link.state == BW_LINK_OPEN ? bw_mod_net_peer_count() : 0;
}

int bw_link_wireless_on(void)
{
    return g_link.state == BW_LINK_OPEN && bw_arm7_wm_has_air();
}
