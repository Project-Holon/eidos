/*
 * sdk/arm7/wm.c -- the ARM7's wireless manager, PXI tag 10, over an air.
 *
 * Why a stand-in, and what the air is: sdk/arm7/wm.h.  The protocol is the
 * one the game's matched WM library speaks (src/game/arm9/wm_system.c,
 * wm_standard.c, wm_mp.c, wm_etc.c, wm_sync.c, wm_ds.c), with the shared
 * structures' layouts from the SDK's DWARF (WMStatus, WMParentParam and the
 * callback records):
 *
 *   - A command is one PXI word: the address of a 256-byte buffer in the
 *     ARM9's fifoBuf, its apiid in the first halfword and its arguments
 *     after (u32 each for WMi_SendCommand, a request record for
 *     WMi_SendCommandDirect).  The ARM7 sets bit 15 of the first halfword
 *     once it has read the buffer, which is what lets the ARM9 use it again
 *     (WmGetCommandBuffer4Arm7).
 *   - A reply is one PXI word too: the address of a callback record the
 *     ARM7 wrote.  WmReceiveFifo calls the callback the command installed
 *     (CallbackTable[apiid]), or for apiid 0x81 the send's own callback, for
 *     0x82 the port's, and sets bit 15 of the record's apiid when done.
 *     The records live in the ARM7's buffer the ARM9 gave it
 *     (WMArm9Buf.WM7, 0x300 bytes): six slots of 0x80, each reused only
 *     once the ARM9 has marked it done.  Data a record points to (a port's
 *     received bytes) is in this file's own buffer for that slot.
 *   - The status block (WMArm9Buf.status, 2000 bytes) is the ARM7's to
 *     write; the ARM9 reads the state, the aid, the children and the MP
 *     sizes from it.
 *
 * The radio.  A DS parent sends beacons; a child scans, finds a parent
 * and connects; then MP frames carry each port's packets between the
 * parent and its children (the data sharing of wm_ds.c rides on those).
 * Here every console in a Link session hears every other on the air (one
 * relay session), and the radio's frames are records in air messages:
 *
 *   B  a parent's beacon: its channel, beacon period and WMGameInfo
 *   J  a child asks a parent (by MAC) to take it, with its 24-byte SSID
 *   A  the parent takes it: its aid and the MP sizes      R  or refuses
 *   D  a port's packet: the parent's MAC, the sender's aid, the aids it
 *      is for, the port, a sequence number and the bytes
 *   X  a link ends: a child leaves, or the parent drops one or all
 *   K  a child is still there (when it has sent nothing for a second)
 *
 * Records go out bundled, at most one air message per 20 ms, which keeps a
 * console under the relay's 60 messages a second whatever the game sends.
 * A packet's send is reported done (0x81) when it is on the air; the data
 * sharing's lockstep waits for the other side's packets, so the game moves
 * one shared frame per round trip, as it does on a DS whose radio drops
 * frames.  Links end after 6 s with nothing heard, as out of range.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7.h"
#include "wm.h"
#include "dsmem.h"
#include "firmware.h"
#include "plat_thread.h"
#include "pxi.h"

/* ------------------------------------------------------------------ */
/* The SDK's numbers                                                   */
/* ------------------------------------------------------------------ */

enum {  /* WMState */
    ST_READY = 0, ST_STOP = 1, ST_IDLE = 2, ST_CLASS1 = 3, ST_SCAN = 5,
    ST_CONNECT = 6, ST_PARENT = 7, ST_CHILD = 8, ST_MP_PARENT = 9,
    ST_MP_CHILD = 10
};

enum {  /* WMApiid */
    API_INITIALIZE = 0, API_RESET = 1, API_END = 2, API_ENABLE = 3,
    API_DISABLE = 4, API_POWER_ON = 5, API_POWER_OFF = 6, API_SET_P_PARAM = 7,
    API_START_PARENT = 8, API_END_PARENT = 9, API_START_SCAN = 10,
    API_END_SCAN = 11, API_START_CONNECT = 12, API_DISCONNECT = 13,
    API_START_MP = 14, API_SET_MP_DATA = 15, API_END_MP = 16,
    API_SET_GAMEINFO = 24, API_SET_BEACON_IND = 25, API_MEASURE_CHANNEL = 30,
    API_START_SCAN_EX = 38,
    API_PORT_SEND = 0x81, API_PORT_RECV = 0x82
};

enum {  /* WMStateCode */
    SC_PARENT_START = 0, SC_BEACON_SENT = 2, SC_PARENT_NOT_FOUND = 4,
    SC_PARENT_FOUND = 5, SC_CONNECT_START = 6, SC_CONNECTED = 7,
    SC_DISCONNECTED = 9, SC_MP_START = 10, SC_PORT_SEND = 20,
    SC_PORT_RECV = 21, SC_DISCONNECTED_FROM_MYSELF = 26
};

enum {  /* WMErrCode */
    ERR_SUCCESS = 0, ERR_FAILED = 1, ERR_ILLEGAL_STATE = 3, ERR_TIMEOUT = 9,
    ERR_NO_ENTRY = 11, ERR_OVER_MAX_ENTRY = 12
};

/* WMStatus (2000 bytes), the members this file keeps.                     */
#define S_STATE             0x000
#define S_MP_FLAG           0x00C
#define S_MP_PARENT_SIZE    0x030
#define S_MP_CHILD_SIZE     0x032
#define S_MP_PARENT_MAX     0x034
#define S_MP_CHILD_MAX      0x036
#define S_MP_SEND_SIZE      0x038
#define S_MP_RECV_SIZE      0x03A
#define S_MP_MAX_SEND       0x03C
#define S_MP_MAX_RECV       0x03E
#define S_MP_RECV_BUF_SIZE  0x072
#define S_MP_RECV_BUF       0x074
#define S_MP_SEND_BUF       0x07C
#define S_MP_SEND_BUF_SIZE  0x080
#define S_MP_READY_BITMAP   0x086
#define S_CURR_TGID         0x0BA
#define S_LINK_LEVEL        0x0BC
#define S_BEACON_IND        0x0C2
#define S_PWR_MGT_MODE      0x0C6
#define S_MISC_FLAGS        0x0C8
#define S_MAC               0x0E0
#define S_PPARAM            0x0E8      /* WMParentParam, 64 bytes          */
#define S_CHILD_MAC         0x128      /* [15][6], aid 1 first              */
#define S_CHILD_BITMAP      0x182
#define S_AID               0x188
#define S_PARENT_MAC        0x18A
#define S_SCAN_CHANNEL      0x190
#define S_ENABLE_CHANNEL    0x1F4
#define S_ALLOWED_CHANNEL   0x1F6
#define S_SIZE              2000

/* WMParentParam.                                                          */
#define PP_USER_INFO        0x00
#define PP_USER_INFO_LEN    0x04
#define PP_GGID             0x08
#define PP_TGID             0x0C
#define PP_ENTRY            0x0E
#define PP_MAX_ENTRY        0x10
#define PP_MULTIBOOT        0x12
#define PP_KS               0x14
#define PP_CS               0x16
#define PP_BEACON_PERIOD    0x18
#define PP_CHANNEL          0x32
#define PP_PARENT_MAX       0x34
#define PP_CHILD_MAX        0x36

/* WMBssDesc (0xC0 bytes) and its WMGameInfo.                              */
#define BSS_SIZE            0xC0
#define GI_HEADER           0x10
#define GI_MAX              0x80

/* The WM's allowed channels: 1, 7 and 13, bit n-1 for channel n (the
 * ARM7 publishes them at HW_WM_ALLOWED_CHANNEL; WM_GetAllowedChannel).     */
#define ALLOWED_CHANNELS    0x1041u
#define HW_ALLOWED_CHANNEL  0x02FFFCFAu

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

#define SLOTS        6
#define SLOT_SIZE    0x80
#define PENDING_MAX  96
#define PARENTS_MAX  16
#define HELD_MAX     32
#define DATA_MAX     512

#define LINK_TIMEOUT_MS   6000u
#define FLUSH_MS          20u
#define BEACON_AIR_MS     500u
#define JOIN_RETRY_MS     250u
#define JOIN_TIMEOUT_MS   4000u
#define KEEPALIVE_MS      1000u
#define PARENT_FRESH_MS   3000u

/* A reply never reaches the ARM9 inside the call that sent the command: the
 * DS's ARM7 takes a while, and the game's code after a WM call (WIH's
 * StateIn functions set their state after the call returns) relies on it. */
#define REPLY_DELAY_NS    1000000ull

typedef struct Pending {
    u8  rec[SLOT_SIZE];
    u16 data_off;               /* where the data pointer goes, 0 none     */
    u16 data_len;
    u64 due_ns;                 /* not before: the ARM7 answers later    */
    u8  data[DATA_MAX];
} Pending;

typedef struct Parent {
    int used;
    u8  mac[6];
    u16 channel;
    u16 beacon_period;
    u16 gi_len;
    u8  gi[GI_MAX];
    u64 heard_ms;
} Parent;

typedef struct Child {
    int used;
    u8  mac[6];
    u64 heard_ms;
} Child;

typedef struct Held {
    u8  sender[6];
    u16 from, dest, port, seq, len;
    u8  bytes[DATA_MAX];
} Held;

static struct {
    BwWmAir air;
    int has_air;
    int registered;

    /* the ARM9's buffers, as host pointers, and their wire values */
    u8 *status;
    u8 *w7;
    u32 w7_wire;
    int slot_busy[SLOTS];
    u64 slot_since[SLOTS];
    u8  slot_data[SLOTS][DATA_MAX];

    Pending pend[PENDING_MAX];
    u32 pend_head, pend_count;

    u8  mac[6];

    /* a parent */
    Child children[16];         /* by aid, 1..15 */
    u64 next_beacon_air, next_beacon_sent;
    int gi_override;
    u16 gi_len;
    u8  gi[GI_MAX];

    /* a child */
    u8  parent_mac[6];
    u16 parent_channel;
    u16 join_parent_max, join_child_max;
    u8  join_ssid[24];
    int joining;
    u64 join_start, join_next;
    int power_save;
    u64 parent_heard;
    u64 last_sent;

    /* scanning */
    int scanning;
    u64 scan_due;
    u16 scan_channels;
    u32 scan_buf_wire;
    u16 scan_buf_size;
    u8  scan_bssid[6];

    Parent parents[PARENTS_MAX];

    Held held[HELD_MAX];
    u32 held_count;

    u16 port_seq[16];

    /* the air */
    u8  out[BW_WM_AIR_MAX];
    u32 out_len;
    u64 last_flush;
    u8  in[BW_WM_AIR_MAX + 64];
    u32 sent, heard;

    int in_step;
} g_wm;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static u64 now_ms(void)
{
    return bw_clock_ns() / 1000000ull;
}

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* A pointer value the ARM9 wrote (a DS address on every port host).        */
static u8 *host_of(u32 v)
{
    if (v == 0)
        return NULL;
    if (v >= BW_DS_MAIN_BASE && v < BW_DS_MAIN_BASE + BW_DS_MAIN_SIZE)
        return (u8 *)bw_ds_try_ptr(v);
#if UINTPTR_MAX == 0xFFFFFFFFu
    return (u8 *)(uintptr_t)v;
#else
    return NULL;
#endif
}

static u16 st16(u32 off) { return g_wm.status ? rd16(g_wm.status + off) : 0; }
static void set16(u32 off, u32 v) { if (g_wm.status) wr16(g_wm.status + off, v); }
static void set32(u32 off, u32 v) { if (g_wm.status) wr32(g_wm.status + off, v); }
static u16 state(void) { return st16(S_STATE); }
static void set_state(u16 s) { set16(S_STATE, s); }

static int is_parent(void) { u16 s = state(); return s == ST_PARENT || s == ST_MP_PARENT; }
static int is_child(void)  { u16 s = state(); return s == ST_CHILD || s == ST_MP_CHILD; }

static void load_mac(void)
{
    memcpy(g_wm.mac, bw_ds_ptr(BW_FW_MAC_ADDR), 6);
}

/* ------------------------------------------------------------------ */
/* Callbacks to the ARM9                                               */
/* ------------------------------------------------------------------ */

static int wm_log(void);

static Pending *post_begin(void)
{
    Pending *p;

    if (g_wm.pend_count >= PENDING_MAX) {
        fprintf(stderr, "wm: callback queue full, one dropped\n");
        return NULL;
    }
    p = &g_wm.pend[(g_wm.pend_head + g_wm.pend_count) % PENDING_MAX];
    g_wm.pend_count++;
    memset(p->rec, 0, sizeof p->rec);
    p->data_off = 0;
    p->data_len = 0;
    p->due_ns = bw_clock_ns() + REPLY_DELAY_NS;
    return p;
}

static void post_basic(u16 apiid, u16 err)
{
    Pending *p = post_begin();

    if (p == NULL)
        return;
    wr16(p->rec + 0, apiid);
    wr16(p->rec + 2, err);
}

static void deliver(void)
{
    u32 i;
    u64 now = now_ms();

    if (g_wm.w7 == NULL)
        return;
    for (i = 0; i < SLOTS && g_wm.pend_count > 0; i++) {
        u8 *slot = g_wm.w7 + i * SLOT_SIZE;
        Pending *p;

        if (g_wm.slot_busy[i]) {
            if (!(rd16(slot) & 0x8000u) && now - g_wm.slot_since[i] < 2000u)
                continue;
            g_wm.slot_busy[i] = 0;
        }
        p = &g_wm.pend[g_wm.pend_head];
        if (bw_clock_ns() < p->due_ns)
            break;
        g_wm.pend_head = (g_wm.pend_head + 1u) % PENDING_MAX;
        g_wm.pend_count--;
        memcpy(slot, p->rec, SLOT_SIZE);
        if (p->data_off != 0) {
            memcpy(g_wm.slot_data[i], p->data, p->data_len);
            wr32(slot + p->data_off, (u32)(uintptr_t)g_wm.slot_data[i]);
        }
        g_wm.slot_busy[i] = 1;
        g_wm.slot_since[i] = now;
        if (wm_log() && rd16(slot) != API_PORT_SEND && rd16(slot) != API_PORT_RECV)
            fprintf(stderr, "wm: reply %u err %u state-code %u\n", rd16(slot), rd16(slot + 2),
                    rd16(slot) == API_START_MP ? rd16(slot + 4) : rd16(slot + 8));
        (void)bw_pxi_arm7_send(BW_PXI_TAG_WM, g_wm.w7_wire + i * SLOT_SIZE, 0);
    }
}

/* WMStartParentCallback: a child came or went, the parent started.        */
static void post_parent(u16 sc, u16 aid, const u8 *mac, const u8 *ssid24, u16 reason)
{
    Pending *p = post_begin();

    if (p == NULL)
        return;
    wr16(p->rec + 0x00, API_START_PARENT);
    wr16(p->rec + 0x08, sc);
    if (mac != NULL)
        memcpy(p->rec + 0x0A, mac, 6);
    wr16(p->rec + 0x10, aid);
    wr16(p->rec + 0x12, reason);
    if (ssid24 != NULL)
        memcpy(p->rec + 0x14, ssid24, 24);
    wr16(p->rec + 0x2C, st16(S_PPARAM + PP_PARENT_MAX));
    wr16(p->rec + 0x2E, st16(S_PPARAM + PP_CHILD_MAX));
}

/* WMStartConnectCallback.                                                 */
static void post_connect(u16 err, u16 sc, u16 aid, u16 reason)
{
    Pending *p = post_begin();

    if (p == NULL)
        return;
    wr16(p->rec + 0x00, API_START_CONNECT);
    wr16(p->rec + 0x02, err);
    wr16(p->rec + 0x08, sc);
    wr16(p->rec + 0x0A, aid);
    wr16(p->rec + 0x0C, reason);
    memcpy(p->rec + 0x10, g_wm.parent_mac, 6);
    wr16(p->rec + 0x16, g_wm.join_parent_max);
    wr16(p->rec + 0x18, g_wm.join_child_max);
}

/* ------------------------------------------------------------------ */
/* The air                                                             */
/* ------------------------------------------------------------------ */

#define AIR_HEADER 10      /* 'L' 'K' version reserved, then the sender's MAC */

static void flush(void)
{
    if (g_wm.out_len <= AIR_HEADER || !g_wm.has_air)
        return;
    if (g_wm.air.send(g_wm.air.user, g_wm.out, g_wm.out_len) == 0)
        g_wm.sent++;
    g_wm.out_len = 0;
    g_wm.last_flush = now_ms();
}

static u8 *record(u8 type, u32 len)
{
    u8 *r;

    if (!g_wm.has_air || len + 3u + AIR_HEADER > BW_WM_AIR_MAX)
        return NULL;
    if (g_wm.out_len + 3u + len > BW_WM_AIR_MAX)
        flush();
    if (g_wm.out_len == 0) {
        g_wm.out[0] = 'L';
        g_wm.out[1] = 'K';
        g_wm.out[2] = 1;
        g_wm.out[3] = 0;
        memcpy(g_wm.out + 4, g_wm.mac, 6);
        g_wm.out_len = AIR_HEADER;
    }
    r = g_wm.out + g_wm.out_len;
    r[0] = type;
    wr16(r + 1, len);
    g_wm.out_len += 3u + len;
    g_wm.last_sent = now_ms();
    return r + 3;
}

static void air_leave(const u8 *parent, u16 aid)
{
    u8 *r = record('X', 8);

    if (r != NULL) {
        memcpy(r, parent, 6);
        wr16(r + 6, aid);
    }
}

/* The beacon's WMGameInfo, from the parent parameters or WM_SetGameInfo.  */
static u16 build_gameinfo(u8 *gi)
{
    const u8 *pp = g_wm.status + S_PPARAM;
    u16 ulen;
    const u8 *user;

    memset(gi, 0, GI_MAX);
    wr16(gi + 0, 1);                        /* magicNumber                  */
    gi[2] = 1;                              /* ver                          */
    gi[3] = 0;                              /* platform: NITRO              */
    wr16(gi + 0xC, rd16(pp + PP_PARENT_MAX));
    wr16(gi + 0xE, rd16(pp + PP_CHILD_MAX));
    if (g_wm.gi_override) {
        memcpy(gi + 4, g_wm.gi + 4, 4);
        memcpy(gi + 8, g_wm.gi + 8, 2);
        gi[0xA] = g_wm.gi[0xA];
        gi[0xB] = g_wm.gi[0xB];
        ulen = g_wm.gi[0xA];
        memcpy(gi + GI_HEADER, g_wm.gi + GI_HEADER, ulen);
        return (u16)(GI_HEADER + ulen);
    }
    memcpy(gi + 4, pp + PP_GGID, 4);
    memcpy(gi + 8, pp + PP_TGID, 2);
    ulen = rd16(pp + PP_USER_INFO_LEN);
    if (ulen > GI_MAX - GI_HEADER)
        ulen = GI_MAX - GI_HEADER;
    gi[0xA] = (u8)ulen;
    gi[0xB] = (u8)((rd16(pp + PP_ENTRY) & 1) | (rd16(pp + PP_MULTIBOOT) & 1) << 1
                   | (rd16(pp + PP_KS) & 1) << 2 | (rd16(pp + PP_CS) & 1) << 3);
    user = host_of(rd32(pp + PP_USER_INFO));
    if (user != NULL && ulen > 0)
        memcpy(gi + GI_HEADER, user, ulen);
    return (u16)(GI_HEADER + ulen);
}

static void air_beacon(void)
{
    u8 gi[GI_MAX];
    u16 len = build_gameinfo(gi);
    u8 *r = record('B', 6u + len);

    if (r == NULL)
        return;
    wr16(r + 0, st16(S_PPARAM + PP_CHANNEL));
    wr16(r + 2, st16(S_PPARAM + PP_BEACON_PERIOD));
    wr16(r + 4, len);
    memcpy(r + 6, gi, len);
}

/* ------------------------------------------------------------------ */
/* Links                                                               */
/* ------------------------------------------------------------------ */

static u16 child_count(void)
{
    u16 n = 0;
    u32 a;

    for (a = 1; a < 16; a++)
        n += g_wm.children[a].used ? 1 : 0;
    return n;
}

static void child_drop(u16 aid, u16 sc, u16 reason)
{
    Child *c = &g_wm.children[aid];
    u8 mac[6];

    if (aid == 0 || aid > 15 || !c->used)
        return;
    memcpy(mac, c->mac, 6);
    c->used = 0;
    set16(S_CHILD_BITMAP, st16(S_CHILD_BITMAP) & ~(1u << aid));
    memset(g_wm.status + S_CHILD_MAC + (aid - 1u) * 6u, 0, 6);
    post_parent(sc, aid, mac, NULL, reason);
}

/* A child loses its parent: the WM is idle again.                         */
static void child_lost(u16 sc, u16 reason)
{
    u16 aid = st16(S_AID);

    set_state(ST_IDLE);
    set32(S_MP_FLAG, 0);
    set16(S_AID, 0);
    g_wm.held_count = 0;
    post_connect(ERR_SUCCESS, sc, aid, reason);
}

static void drop_all_links(int tell)
{
    u32 a;

    if (is_parent()) {
        if (tell)
            air_leave(g_wm.mac, 0xFFFF);
        for (a = 1; a < 16; a++)
            g_wm.children[a].used = 0;
        set16(S_CHILD_BITMAP, 0);
    } else if (is_child() || g_wm.joining) {
        if (tell && is_child())
            air_leave(g_wm.parent_mac, st16(S_AID));
    }
    g_wm.joining = 0;
    g_wm.scanning = 0;
    g_wm.held_count = 0;
    set32(S_MP_FLAG, 0);
    set16(S_AID, 0);
}

/* A port's packet for this console.                                       */
static void post_port_recv(const u8 *sender, u16 from, u16 port, u16 seq,
                           const u8 *bytes, u16 len)
{
    Pending *p = post_begin();

    if (p == NULL)
        return;
    if (len > DATA_MAX)
        len = DATA_MAX;
    wr16(p->rec + 0x00, API_PORT_RECV);
    wr16(p->rec + 0x04, SC_PORT_RECV);
    wr16(p->rec + 0x06, port);
    wr32(p->rec + 0x08, rd32(g_wm.status + S_MP_RECV_BUF));
    wr16(p->rec + 0x10, len);
    wr16(p->rec + 0x12, from);
    memcpy(p->rec + 0x14, sender, 6);
    wr16(p->rec + 0x1A, seq);
    wr16(p->rec + 0x20, st16(S_AID));
    wr16(p->rec + 0x3E, 0);
    wr16(p->rec + 0x40, st16(S_MP_SEND_SIZE));
    wr16(p->rec + 0x42, st16(S_MP_RECV_SIZE));
    p->data_off = 0x0C;
    p->data_len = len;
    memcpy(p->data, bytes, len);
}

static void hold(const u8 *sender, u16 from, u16 dest, u16 port, u16 seq,
                 const u8 *bytes, u16 len)
{
    Held *h;

    if (g_wm.held_count >= HELD_MAX || len > DATA_MAX)
        return;
    h = &g_wm.held[g_wm.held_count++];
    memcpy(h->sender, sender, 6);
    h->from = from;
    h->dest = dest;
    h->port = port;
    h->seq = seq;
    h->len = len;
    memcpy(h->bytes, bytes, len);
}

static void release_held(void)
{
    u32 i;

    for (i = 0; i < g_wm.held_count; i++) {
        Held *h = &g_wm.held[i];

        post_port_recv(h->sender, h->from, h->port, h->seq, h->bytes, h->len);
    }
    g_wm.held_count = 0;
}

/* ------------------------------------------------------------------ */
/* What comes in on the air                                            */
/* ------------------------------------------------------------------ */

static Parent *parent_slot(const u8 *mac)
{
    u32 i;
    Parent *free_one = NULL, *oldest = &g_wm.parents[0];

    for (i = 0; i < PARENTS_MAX; i++) {
        Parent *p = &g_wm.parents[i];

        if (p->used && memcmp(p->mac, mac, 6) == 0)
            return p;
        if (!p->used && free_one == NULL)
            free_one = p;
        if (p->heard_ms < oldest->heard_ms)
            oldest = p;
    }
    return free_one != NULL ? free_one : oldest;
}

static void on_beacon(const u8 *sender, const u8 *r, u32 len, u64 now)
{
    Parent *p;
    u16 gl;

    if (len < 6)
        return;
    gl = rd16(r + 4);
    if (gl < GI_HEADER || gl > GI_MAX || 6u + gl > len)
        return;
    p = parent_slot(sender);
    p->used = 1;
    memcpy(p->mac, sender, 6);
    p->channel = rd16(r);
    p->beacon_period = rd16(r + 2);
    p->gi_len = gl;
    memcpy(p->gi, r + 6, gl);
    p->heard_ms = now;
    if (is_child() && memcmp(sender, g_wm.parent_mac, 6) == 0)
        g_wm.parent_heard = now;
}

static void on_join(const u8 *sender, const u8 *r, u32 len, u64 now)
{
    u16 aid, max_entry;
    u8 *a;

    if (len < 30 || memcmp(r, g_wm.mac, 6) != 0 || !is_parent())
        return;
    for (aid = 1; aid < 16; aid++) {
        if (g_wm.children[aid].used && memcmp(g_wm.children[aid].mac, sender, 6) == 0)
            break;
    }
    if (aid == 16) {
        u16 refuse = 0;

        max_entry = st16(S_PPARAM + PP_MAX_ENTRY);
        if (max_entry > 15)
            max_entry = 15;
        if (!(st16(S_PPARAM + PP_ENTRY) & 1))
            refuse = ERR_NO_ENTRY;
        else if (child_count() >= max_entry)
            refuse = ERR_OVER_MAX_ENTRY;
        if (refuse != 0) {
            u8 *x = record('R', 8);

            if (x != NULL) {
                memcpy(x, sender, 6);
                wr16(x + 6, refuse);
            }
            return;
        }
        for (aid = 1; aid <= max_entry; aid++) {
            if (!g_wm.children[aid].used)
                break;
        }
        g_wm.children[aid].used = 1;
        memcpy(g_wm.children[aid].mac, sender, 6);
        g_wm.children[aid].heard_ms = now;
        set16(S_CHILD_BITMAP, st16(S_CHILD_BITMAP) | 1u << aid);
        memcpy(g_wm.status + S_CHILD_MAC + (aid - 1u) * 6u, sender, 6);
        post_parent(SC_CONNECTED, aid, sender, r + 6, 0);
    }
    a = record('A', 12);
    if (a != NULL) {
        memcpy(a, sender, 6);
        wr16(a + 6, aid);
        wr16(a + 8, st16(S_PPARAM + PP_PARENT_MAX));
        wr16(a + 10, st16(S_PPARAM + PP_CHILD_MAX));
    }
}

static void on_accept(const u8 *sender, const u8 *r, u32 len, u64 now)
{
    u16 aid;

    if (len < 12 || memcmp(r, g_wm.mac, 6) != 0 || !g_wm.joining
        || memcmp(sender, g_wm.parent_mac, 6) != 0)
        return;
    aid = rd16(r + 6);
    g_wm.joining = 0;
    g_wm.join_parent_max = rd16(r + 8);
    g_wm.join_child_max = rd16(r + 10);
    g_wm.parent_heard = now;
    set_state(ST_CHILD);
    set16(S_AID, aid);
    memcpy(g_wm.status + S_PARENT_MAC, sender, 6);
    set16(S_MP_PARENT_SIZE, g_wm.join_parent_max);
    set16(S_MP_CHILD_SIZE, g_wm.join_child_max);
    set16(S_MP_PARENT_MAX, g_wm.join_parent_max);
    set16(S_MP_CHILD_MAX, g_wm.join_child_max);
    set16(S_MP_SEND_SIZE, g_wm.join_child_max);
    set16(S_MP_RECV_SIZE, g_wm.join_parent_max);
    set16(S_MP_MAX_SEND, g_wm.join_child_max);
    set16(S_MP_MAX_RECV, g_wm.join_parent_max);
    set16(S_PWR_MGT_MODE, g_wm.power_save ? 1 : 0);
    set16(S_LINK_LEVEL, 3);
    post_connect(ERR_SUCCESS, SC_CONNECTED, aid, 0);
}

static void on_refuse(const u8 *sender, const u8 *r, u32 len)
{
    if (len < 8 || memcmp(r, g_wm.mac, 6) != 0 || !g_wm.joining
        || memcmp(sender, g_wm.parent_mac, 6) != 0)
        return;
    g_wm.joining = 0;
    set_state(ST_IDLE);
    post_connect(rd16(r + 6), SC_CONNECT_START, 0, 0);
}

static void on_data(const u8 *sender, const u8 *r, u32 len, u64 now)
{
    u16 from, dest, port, seq, n;

    if (len < 16)
        return;
    from = rd16(r + 6);
    dest = rd16(r + 8);
    port = rd16(r + 10);
    seq = rd16(r + 12);
    n = rd16(r + 14);
    if (16u + n > len || port >= 16)
        return;
    if (is_parent() && memcmp(r, g_wm.mac, 6) == 0) {
        if (from == 0 || from > 15 || !g_wm.children[from].used
            || memcmp(g_wm.children[from].mac, sender, 6) != 0 || !(dest & 1))
            return;
        g_wm.children[from].heard_ms = now;
    } else if (is_child() && memcmp(r, g_wm.parent_mac, 6) == 0) {
        if (from != 0 || !(dest & (1u << st16(S_AID))))
            return;
        g_wm.parent_heard = now;
    } else {
        return;
    }
    if (rd32(g_wm.status + S_MP_FLAG) != 1) {
        hold(sender, from, dest, port, seq, r + 16, n);
        return;
    }
    post_port_recv(sender, from, port, seq, r + 16, n);
}

static void on_leave(const u8 *sender, const u8 *r, u32 len)
{
    u16 aid;

    if (len < 8)
        return;
    aid = rd16(r + 6);
    if (is_parent() && memcmp(r, g_wm.mac, 6) == 0) {
        if (aid > 0 && aid < 16 && g_wm.children[aid].used
            && memcmp(g_wm.children[aid].mac, sender, 6) == 0)
            child_drop(aid, SC_DISCONNECTED, 1);
    } else if (is_child() && memcmp(r, g_wm.parent_mac, 6) == 0
               && memcmp(sender, g_wm.parent_mac, 6) == 0
               && (aid == 0xFFFF || aid == st16(S_AID))) {
        child_lost(SC_DISCONNECTED, 1);
    }
}

static void on_keepalive(const u8 *sender, const u8 *r, u32 len, u64 now)
{
    u16 aid;

    if (len < 8 || !is_parent() || memcmp(r, g_wm.mac, 6) != 0)
        return;
    aid = rd16(r + 6);
    if (aid > 0 && aid < 16 && g_wm.children[aid].used
        && memcmp(g_wm.children[aid].mac, sender, 6) == 0)
        g_wm.children[aid].heard_ms = now;
}

static void air_in(const u8 *m, u32 n, u64 now)
{
    const u8 *sender;
    u32 off;

    if (n < AIR_HEADER || m[0] != 'L' || m[1] != 'K' || m[2] != 1)
        return;
    sender = m + 4;
    if (memcmp(sender, g_wm.mac, 6) == 0)
        return;
    g_wm.heard++;
    for (off = AIR_HEADER; off + 3u <= n;) {
        u8 type = m[off];
        u32 len = rd16(m + off + 1);
        const u8 *r = m + off + 3;

        if (off + 3u + len > n)
            break;
        if (g_wm.status != NULL) {
            switch (type) {
            case 'B': on_beacon(sender, r, len, now); break;
            case 'J': on_join(sender, r, len, now); break;
            case 'A': on_accept(sender, r, len, now); break;
            case 'R': on_refuse(sender, r, len); break;
            case 'D': on_data(sender, r, len, now); break;
            case 'X': on_leave(sender, r, len); break;
            case 'K': on_keepalive(sender, r, len, now); break;
            default: break;
            }
        }
        off += 3u + len;
    }
}

/* ------------------------------------------------------------------ */
/* The scan                                                            */
/* ------------------------------------------------------------------ */

static void scan_finish(u64 now)
{
    Pending *p;
    u8 *buf = host_of(g_wm.scan_buf_wire);
    u16 count = 0;
    u32 i, room = g_wm.scan_buf_size / BSS_SIZE;
    static const u8 any[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    g_wm.scanning = 0;
    if (room > 16)
        room = 16;
    p = post_begin();
    if (p == NULL)
        return;
    wr16(p->rec + 0x00, API_START_SCAN_EX);
    wr16(p->rec + 0x0A, g_wm.scan_channels);
    for (i = 0; i < PARENTS_MAX && count < room && buf != NULL; i++) {
        Parent *pa = &g_wm.parents[i];
        u8 *d;

        if (!pa->used || now - pa->heard_ms > PARENT_FRESH_MS)
            continue;
        if (pa->channel == 0 || pa->channel > 16
            || !(g_wm.scan_channels & (1u << (pa->channel - 1u))))
            continue;
        if (memcmp(g_wm.scan_bssid, any, 6) != 0 && memcmp(g_wm.scan_bssid, pa->mac, 6) != 0)
            continue;
        d = buf + count * BSS_SIZE;
        memset(d, 0, BSS_SIZE);
        wr16(d + 0x00, (u16)((0x40u + pa->gi_len) / 2u));
        wr16(d + 0x02, 0x30);                       /* rssi                */
        memcpy(d + 0x04, pa->mac, 6);
        wr16(d + 0x0A, 32);                         /* ssidLength          */
        memcpy(d + 0x0C, pa->gi + 4, 4);            /* ggid, tgid          */
        memcpy(d + 0x10, pa->gi + 8, 2);
        wr16(d + 0x2C, 0x0021);                     /* capaInfo: ESS, short preamble */
        wr16(d + 0x2E, 0x0003);
        wr16(d + 0x30, 0x0003);
        wr16(d + 0x32, pa->beacon_period);
        wr16(d + 0x34, 1);
        wr16(d + 0x36, pa->channel);
        wr16(d + 0x3C, pa->gi_len);
        memcpy(d + 0x40, pa->gi, pa->gi_len);
        wr32(p->rec + 0x10 + count * 4u, g_wm.scan_buf_wire + count * BSS_SIZE);
        wr16(p->rec + 0x50 + count * 2u, 3);
        count++;
    }
    wr16(p->rec + 0x08, count ? SC_PARENT_FOUND : SC_PARENT_NOT_FOUND);
    wr16(p->rec + 0x0E, count);
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void reset_slots(void)
{
    memset(g_wm.slot_busy, 0, sizeof g_wm.slot_busy);
    g_wm.pend_head = 0;
    g_wm.pend_count = 0;
}

static void bind_buffers(const u8 *cmd)
{
    u32 w7 = rd32(cmd + 4);

    g_wm.w7 = host_of(w7);
    g_wm.w7_wire = (u32)(uintptr_t)g_wm.w7;
    g_wm.status = host_of(rd32(cmd + 8));
    if (g_wm.w7_wire >= (1u << 26)) {
        fprintf(stderr, "wm: the ARM7 buffer is out of the FIFO's reach\n");
        g_wm.w7 = NULL;
    }
}

static void initial_status(u32 misc)
{
    memset(g_wm.status, 0, S_SIZE);
    load_mac();
    memcpy(g_wm.status + S_MAC, g_wm.mac, 6);
    set16(S_ENABLE_CHANNEL, ALLOWED_CHANNELS);
    set16(S_ALLOWED_CHANNEL, ALLOWED_CHANNELS);
    set16(S_LINK_LEVEL, 3);
    set16(S_BEACON_IND, 1);
    set32(S_MISC_FLAGS, misc);
    wr16((u8 *)bw_ds_ptr(HW_ALLOWED_CHANNEL), ALLOWED_CHANNELS);
}

static void cmd_start_mp(const u8 *cmd)
{
    Pending *p;
    u32 recv = rd32(cmd + 4), recv_size = rd32(cmd + 8);
    u32 send = rd32(cmd + 12), send_size = rd32(cmd + 16);

    set_state(st16(S_AID) == 0 && state() == ST_PARENT ? ST_MP_PARENT : ST_MP_CHILD);
    set32(S_MP_FLAG, 1);
    set32(S_MP_RECV_BUF, recv);
    set32(S_MP_RECV_BUF + 4, recv);
    set16(S_MP_RECV_BUF_SIZE, recv_size);
    set32(S_MP_SEND_BUF, send);
    set16(S_MP_SEND_BUF_SIZE, send_size);
    set16(S_MP_READY_BITMAP, st16(S_CHILD_BITMAP));
    p = post_begin();
    if (p != NULL) {
        wr16(p->rec + 0x00, API_START_MP);
        wr16(p->rec + 0x04, SC_MP_START);
        wr32(p->rec + 0x08, recv);
    }
    release_held();
}

static void cmd_set_mp_data(const u8 *cmd)
{
    u32 data = rd32(cmd + 4);
    u16 size = (u16)rd32(cmd + 8);
    u16 dest = (u16)rd32(cmd + 12);
    u16 port = (u16)(rd32(cmd + 16) & 15u);
    u32 callback = rd32(cmd + 24), arg = rd32(cmd + 28);
    const u8 *bytes = host_of(data);
    u16 seq = (u16)(g_wm.port_seq[port]++ << 1);
    u16 sent = 0, err = ERR_SUCCESS;
    Pending *p;

    if (is_parent()) {
        dest &= st16(S_CHILD_BITMAP);
    } else if (is_child()) {
        dest = 1;
    } else {
        dest = 0;
    }
    if (dest != 0 && bytes != NULL && size <= DATA_MAX) {
        u8 *r = record('D', 16u + size);

        if (r != NULL) {
            memcpy(r, is_parent() ? g_wm.mac : g_wm.parent_mac, 6);
            wr16(r + 6, st16(S_AID));
            wr16(r + 8, dest);
            wr16(r + 10, port);
            wr16(r + 12, seq);
            wr16(r + 14, size);
            memcpy(r + 16, bytes, size);
            sent = dest;
        } else {
            err = 15;   /* WM_ERRCODE_SEND_FAILED */
        }
    }
    p = post_begin();
    if (p == NULL)
        return;
    wr16(p->rec + 0x00, API_PORT_SEND);
    wr16(p->rec + 0x02, err);
    wr16(p->rec + 0x08, SC_PORT_SEND);
    wr16(p->rec + 0x0A, port);
    wr16(p->rec + 0x0C, (u16)rd32(cmd + 12));
    wr16(p->rec + 0x0E, 0);
    wr16(p->rec + 0x10, sent);
    wr32(p->rec + 0x14, data);
    wr16(p->rec + 0x18, size);
    wr16(p->rec + 0x1A, seq);
    wr32(p->rec + 0x1C, callback);
    wr32(p->rec + 0x20, arg);
    wr16(p->rec + 0x24, st16(S_MP_SEND_SIZE));
    wr16(p->rec + 0x26, st16(S_MP_RECV_SIZE));
}

static void cmd_disconnect(u16 bitmap)
{
    u32 a;

    if (is_parent()) {
        for (a = 1; a < 16; a++) {
            if ((bitmap & (1u << a)) && g_wm.children[a].used) {
                air_leave(g_wm.mac, (u16)a);
                child_drop((u16)a, SC_DISCONNECTED_FROM_MYSELF, 0);
            }
        }
        post_basic(API_DISCONNECT, ERR_SUCCESS);
    } else if (is_child()) {
        air_leave(g_wm.parent_mac, st16(S_AID));
        post_basic(API_DISCONNECT, ERR_SUCCESS);
        child_lost(SC_DISCONNECTED_FROM_MYSELF, 0);
    } else {
        post_basic(API_DISCONNECT, ERR_ILLEGAL_STATE);
    }
}

static int wm_log(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("EIDOS_WM_LOG");

        v = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return v;
}

static void command(u8 *cmd)
{
    u16 apiid = (u16)(rd16(cmd) & 0x7FFFu);
    u64 now = now_ms();

    if (wm_log() && apiid != API_SET_MP_DATA)
        fprintf(stderr, "wm: command %u (state %u)\n", apiid, state());

    if (apiid != API_INITIALIZE && apiid != API_ENABLE && g_wm.status == NULL) {
        post_basic(apiid, ERR_ILLEGAL_STATE);
        return;
    }
    switch (apiid) {
    case API_INITIALIZE:
    case API_ENABLE:
        bind_buffers(cmd);
        reset_slots();
        if (g_wm.status == NULL || g_wm.w7 == NULL) {
            g_wm.status = NULL;
            return;
        }
        memset(g_wm.children, 0, sizeof g_wm.children);
        memset(g_wm.parents, 0, sizeof g_wm.parents);
        g_wm.gi_override = 0;
        g_wm.joining = 0;
        g_wm.scanning = 0;
        g_wm.held_count = 0;
        initial_status(rd32(cmd + 16));
        set_state(apiid == API_INITIALIZE ? ST_IDLE : ST_STOP);
        post_basic(apiid, ERR_SUCCESS);
        if (g_wm.has_air && g_wm.air.power != NULL)
            g_wm.air.power(g_wm.air.user, 1);
        break;
    case API_DISABLE:
        set_state(ST_READY);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_POWER_ON:
        set_state(ST_IDLE);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_POWER_OFF:
        set_state(ST_STOP);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_RESET:
        drop_all_links(1);
        set_state(ST_IDLE);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_END:
        drop_all_links(1);
        flush();
        set_state(ST_READY);
        post_basic(apiid, ERR_SUCCESS);
        if (g_wm.has_air && g_wm.air.power != NULL)
            g_wm.air.power(g_wm.air.user, 0);
        break;
    case API_SET_P_PARAM: {
        const u8 *pp = host_of(rd32(cmd + 4));

        if (pp != NULL)
            memcpy(g_wm.status + S_PPARAM, pp, 64);
        set16(S_CURR_TGID, st16(S_PPARAM + PP_TGID));
        post_basic(apiid, pp != NULL ? ERR_SUCCESS : ERR_FAILED);
        break;
    }
    case API_START_PARENT:
        set_state(ST_PARENT);
        set16(S_AID, 0);
        set16(S_CHILD_BITMAP, 0);
        memset(g_wm.children, 0, sizeof g_wm.children);
        set16(S_MP_PARENT_SIZE, st16(S_PPARAM + PP_PARENT_MAX));
        set16(S_MP_CHILD_SIZE, st16(S_PPARAM + PP_CHILD_MAX));
        set16(S_MP_PARENT_MAX, st16(S_PPARAM + PP_PARENT_MAX));
        set16(S_MP_CHILD_MAX, st16(S_PPARAM + PP_CHILD_MAX));
        set16(S_MP_SEND_SIZE, st16(S_PPARAM + PP_PARENT_MAX));
        set16(S_MP_RECV_SIZE, st16(S_PPARAM + PP_CHILD_MAX));
        set16(S_MP_MAX_SEND, st16(S_PPARAM + PP_PARENT_MAX));
        set16(S_MP_MAX_RECV, st16(S_PPARAM + PP_CHILD_MAX));
        set16(S_PWR_MGT_MODE, rd32(cmd + 4) ? 1 : 0);
        post_parent(SC_PARENT_START, 0, g_wm.mac, NULL, 0);
        air_beacon();
        g_wm.next_beacon_air = now + BEACON_AIR_MS;
        g_wm.next_beacon_sent = now + 200u;
        break;
    case API_END_PARENT:
        drop_all_links(1);
        set_state(ST_IDLE);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_START_SCAN_EX: {
        u16 t = rd16(cmd + 0x0A);

        set_state(ST_SCAN);
        g_wm.scanning = 1;
        g_wm.scan_channels = rd16(cmd + 2);
        g_wm.scan_buf_wire = rd32(cmd + 4);
        g_wm.scan_buf_size = rd16(cmd + 8);
        memcpy(g_wm.scan_bssid, cmd + 0x0C, 6);
        g_wm.scan_due = now + (t < 10 ? 10 : t);
        {
            u16 ch, c = g_wm.scan_channels;

            for (ch = 1; ch <= 16 && !(c & 1); ch++)
                c >>= 1;
            set16(S_SCAN_CHANNEL, ch);
        }
        break;
    }
    case API_START_SCAN:
        post_basic(apiid, ERR_FAILED);
        break;
    case API_END_SCAN:
        g_wm.scanning = 0;
        set_state(ST_IDLE);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_START_CONNECT: {
        const u8 *bss = host_of(rd32(cmd + 4));

        if (bss == NULL) {
            post_connect(ERR_FAILED, SC_CONNECT_START, 0, 0);
            break;
        }
        memcpy(g_wm.parent_mac, bss + 4, 6);
        g_wm.parent_channel = rd16(bss + 0x36);
        g_wm.join_parent_max = rd16(bss + 0x40 + 0xC);
        g_wm.join_child_max = rd16(bss + 0x40 + 0xE);
        memcpy(g_wm.join_ssid, cmd + 8, 24);
        g_wm.power_save = rd32(cmd + 0x20) != 0;
        g_wm.joining = 1;
        g_wm.join_start = now;
        g_wm.join_next = now;
        set_state(ST_CONNECT);
        post_connect(ERR_SUCCESS, SC_CONNECT_START, 0, 0);
        break;
    }
    case API_DISCONNECT:
        cmd_disconnect((u16)rd32(cmd + 4));
        break;
    case API_START_MP:
        cmd_start_mp(cmd);
        break;
    case API_SET_MP_DATA:
        cmd_set_mp_data(cmd);
        break;
    case API_END_MP:
        set32(S_MP_FLAG, 0);
        if (state() == ST_MP_PARENT)
            set_state(ST_PARENT);
        else if (state() == ST_MP_CHILD)
            set_state(ST_CHILD);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_SET_GAMEINFO: {
        const u8 *user = host_of(rd32(cmd + 4));
        u16 ulen = (u16)rd32(cmd + 8);

        if (ulen > GI_MAX - GI_HEADER)
            ulen = GI_MAX - GI_HEADER;
        memset(g_wm.gi, 0, sizeof g_wm.gi);
        wr32(g_wm.gi + 4, rd32(cmd + 12));
        wr16(g_wm.gi + 8, rd32(cmd + 16));
        g_wm.gi[0xA] = (u8)ulen;
        g_wm.gi[0xB] = (u8)rd32(cmd + 20);
        if (user != NULL)
            memcpy(g_wm.gi + GI_HEADER, user, ulen);
        g_wm.gi_override = 1;
        set16(S_CURR_TGID, rd32(cmd + 16));
        if (is_parent())
            air_beacon();
        post_basic(apiid, ERR_SUCCESS);
        break;
    }
    case API_SET_BEACON_IND:
        set16(S_BEACON_IND, rd32(cmd + 4) ? 1 : 0);
        post_basic(apiid, ERR_SUCCESS);
        break;
    case API_MEASURE_CHANNEL: {
        Pending *p = post_begin();

        if (p != NULL) {
            wr16(p->rec + 0x00, apiid);
            wr16(p->rec + 0x08, rd16(cmd + 6));
            wr16(p->rec + 0x0A, (u16)((now + rd16(cmd + 6) * 7u) % 9u));
        }
        break;
    }
    default:
        post_basic(apiid, ERR_SUCCESS);
        break;
    }
}

static void wm_callback(int tag, u32 data, int err)
{
    u8 *cmd;

    (void)tag;
    if (err)
        return;
    cmd = host_of(data);
    if (cmd == NULL)
        return;
    command(cmd);
    wr16(cmd, rd16(cmd) | 0x8000u);     /* the buffer is the ARM9's again */
}

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

static void timers(u64 now)
{
    u32 a;

    if (g_wm.status == NULL)
        return;
    if (g_wm.scanning && now >= g_wm.scan_due)
        scan_finish(now);
    if (is_parent()) {
        if (now >= g_wm.next_beacon_air) {
            air_beacon();
            g_wm.next_beacon_air = now + BEACON_AIR_MS;
        }
        if (now >= g_wm.next_beacon_sent) {
            u16 period = st16(S_PPARAM + PP_BEACON_PERIOD);

            if (st16(S_BEACON_IND))
                post_parent(SC_BEACON_SENT, 0, g_wm.mac, NULL, 0);
            g_wm.next_beacon_sent = now + (period < 50 ? 200u : period);
        }
        for (a = 1; a < 16; a++) {
            if (g_wm.children[a].used
                && (!g_wm.has_air || now - g_wm.children[a].heard_ms > LINK_TIMEOUT_MS))
                child_drop((u16)a, SC_DISCONNECTED, 1);
        }
    }
    if (g_wm.joining) {
        if (!g_wm.has_air || now - g_wm.join_start > JOIN_TIMEOUT_MS) {
            g_wm.joining = 0;
            set_state(ST_IDLE);
            post_connect(ERR_TIMEOUT, SC_CONNECT_START, 0, 0);
        } else if (now >= g_wm.join_next) {
            u8 *r = record('J', 30);

            if (r != NULL) {
                memcpy(r, g_wm.parent_mac, 6);
                memcpy(r + 6, g_wm.join_ssid, 24);
            }
            g_wm.join_next = now + JOIN_RETRY_MS;
        }
    }
    if (is_child()) {
        if (!g_wm.has_air || now - g_wm.parent_heard > LINK_TIMEOUT_MS) {
            child_lost(SC_DISCONNECTED, 1);
        } else if (now - g_wm.last_sent > KEEPALIVE_MS) {
            u8 *r = record('K', 8);

            if (r != NULL) {
                memcpy(r, g_wm.parent_mac, 6);
                wr16(r + 6, st16(S_AID));
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The responder's interface                                           */
/* ------------------------------------------------------------------ */

static void sync_registration(void)
{
    int want = g_wm.has_air || g_wm.status != NULL;

    if (want && !g_wm.registered) {
        bw_pxi_arm7_set_callback(BW_PXI_TAG_WM, wm_callback);
        g_wm.registered = 1;
    } else if (!want && g_wm.registered) {
        bw_pxi_arm7_set_callback(BW_PXI_TAG_WM, NULL);
        g_wm.registered = 0;
    }
}

void bw_arm7_wm_set_air(const BwWmAir *air)
{
    if (air != NULL) {
        g_wm.air = *air;
        g_wm.has_air = 1;
    } else {
        flush();
        memset(&g_wm.air, 0, sizeof g_wm.air);
        g_wm.has_air = 0;
        g_wm.out_len = 0;
    }
    if (bw_pxi_powered())
        sync_registration();
}

int bw_arm7_wm_has_air(void)
{
    return g_wm.has_air;
}

void bw_arm7_wm_init(void)
{
    g_wm.registered = 0;
    g_wm.status = NULL;
    g_wm.w7 = NULL;
    reset_slots();
    sync_registration();
}

void bw_arm7_wm_reset(void)
{
    BwWmAir air = g_wm.air;
    int has = g_wm.has_air;

    memset(&g_wm, 0, sizeof g_wm);
    g_wm.air = air;
    g_wm.has_air = has;
}

void bw_arm7_wm_step(void)
{
    u64 now;
    u32 n, guard;

    if (!g_wm.has_air && g_wm.status == NULL)
        return;
    if (g_wm.in_step)
        return;
    g_wm.in_step = 1;
    now = now_ms();
    if (g_wm.has_air) {
        for (guard = 0; guard < 64; guard++) {
            n = 0;
            if (!g_wm.air.recv(g_wm.air.user, g_wm.in, sizeof g_wm.in, &n))
                break;
            air_in(g_wm.in, n, now);
        }
    }
    timers(now);
    if (g_wm.out_len > AIR_HEADER && now - g_wm.last_flush >= FLUSH_MS)
        flush();
    deliver();
    if (g_wm.status != NULL && state() == ST_READY && !g_wm.has_air) {
        g_wm.status = NULL;
        sync_registration();
    }
    g_wm.in_step = 0;
}

void bw_arm7_wm_info(BwWmInfo *out)
{
    u32 i;
    u64 now = now_ms();

    memset(out, 0, sizeof *out);
    out->state = state();
    out->aid = st16(S_AID);
    out->child_bitmap = st16(S_CHILD_BITMAP);
    for (i = 0; i < PARENTS_MAX; i++) {
        if (g_wm.parents[i].used && now - g_wm.parents[i].heard_ms <= PARENT_FRESH_MS)
            out->parents_heard++;
    }
    out->sent = g_wm.sent;
    out->heard = g_wm.heard;
}
