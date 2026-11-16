/*
 * sdk/mod_world.c -- metaxy world sessions and the Archipelago link, mod
 * API version 6 (docs/mod-services.md, "World sessions"; metaxy's
 * docs/SPEC.md, Part C; arkhipelagos docs/metaxy-proposal.md, Parts C and
 * D).
 *
 * The connection is the relay client's (sdk/mod_net.c): a world session
 * is a relay connection that speaks the world frames.  This file builds
 * the frames a mod's calls send, and turns the frames metaxy sends into
 * the events `world` and `ap`.
 *
 * - The reader thread hands every frame to bw_mod_world_in, which only
 *   queues: a big payload's F pieces are put together first, and the frame
 *   that names them (`@<id>`) carries the whole bytes.
 * - On the game's frame (bw_mod_world_frame) each queued message fires its
 *   event; inside the handler world_read and ap_read give its bytes.  So a
 *   1 MB player data never needs a copy in a wasm mod's port page: the mod
 *   reads it into its own memory.
 * - The checks a mod sent are kept for the session and sent again after
 *   each join (metaxy ignores a check it has), so a check made while the
 *   connection was down is never lost.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_api.h"
#include "mod_event.h"
#include "mod_net.h"
#include "mod_v6.h"
#include "plat_thread.h"
#include "mod_avatar.h"
#include "mod_context.h"

/* sdk/save_games.c (weak: the world tests link without it) */
extern void bw_games_world_code(const char *code) __attribute__((weak));

#define WORLD_PROTO     "eidos-net/2"
#define PIECE_MAX       4096u                 /* the bytes of one F piece */
#define BIG_MAX         (2u * 1024u * 1024u)  /* an Archipelago message */
#define ASSEMBLY_MAX    4
#define CHECKS_MAX      8192u

/* ------------------------------------------------------------------ */
/* The queue the reader fills                                          */
/* ------------------------------------------------------------------ */

typedef struct WorldMsg {
    struct WorldMsg *next;
    u32  event;      /* BW_EVENT_WORLD or BW_EVENT_AP */
    u32  kind;
    u32  ticket;
    u32  player;
    u8  *bytes;
    u32  size;
} WorldMsg;

/* F pieces being put together, one per message id. */
typedef struct Assembly {
    int  used;
    u32  id;
    u8  *bytes;
    u32  size;
    int  done;       /* the last piece came */
} Assembly;

static BwMutex  *g_wlock;
static WorldMsg *g_head;
static WorldMsg *g_tail;
static Assembly  g_asm[ASSEMBLY_MAX];

/* What the game thread knows (no lock: only the game thread). */
static u32  g_create_ticket;      /* the world_create answered by R */
static u32  g_join_ticket;        /* the world_join answered by S and W */
static u32  g_ap_ticket;          /* the ap_link answered by L */
static u32  g_ticket;
static u32  g_out_id;             /* our F message ids */
static int  g_was_open;
static u32 *g_checks;             /* the checks of this session */
static u32  g_check_count;
static u32  g_self;               /* our player number in the world */

/* The message being handled: world_read and ap_read give it. */
static const WorldMsg *g_current;

static void wlock(void)
{
    if (g_wlock == NULL) {
        g_wlock = bw_mutex_create();
    }
    bw_mutex_lock(g_wlock);
}

static void wunlock(void)
{
    bw_mutex_unlock(g_wlock);
}

static void push(u32 event, u32 kind, u32 player, const u8 *bytes, u32 size)
{
    WorldMsg *m = (WorldMsg *)calloc(1, sizeof(WorldMsg));

    if (m == NULL) {
        return;
    }
    m->bytes = (u8 *)malloc(size + 1u);
    if (m->bytes == NULL) {
        free(m);
        return;
    }
    if (size != 0) {
        memcpy(m->bytes, bytes, size);
    }
    m->bytes[size] = 0;
    m->event = event;
    m->kind = kind;
    m->player = player;
    m->size = size;
    wlock();
    if (g_tail != NULL) {
        g_tail->next = m;
    } else {
        g_head = m;
    }
    g_tail = m;
    wunlock();
}

static u32 lead_number(const u8 *p, u32 size)
{
    u32 v = 0, i;

    for (i = 0; i < size && p[i] >= '0' && p[i] <= '9'; i++) {
        v = v * 10u + (u32)(p[i] - '0');
    }
    return v;
}

/* An F piece: "<id> <last>\n<bytes>". */
static void piece(const u8 *p, u32 size)
{
    u32 id = lead_number(p, size), i, last = 0, k;
    Assembly *a = NULL;

    for (i = 0; i < size && p[i] != ' '; i++) {
    }
    if (i + 1 < size) {
        last = p[i + 1] == '1';
    }
    while (i < size && p[i] != '\n') {
        i++;
    }
    if (i >= size) {
        return;
    }
    i++;
    for (k = 0; k < ASSEMBLY_MAX; k++) {
        if (g_asm[k].used && g_asm[k].id == id && !g_asm[k].done) {
            a = &g_asm[k];
        }
    }
    for (k = 0; a == NULL && k < ASSEMBLY_MAX; k++) {
        if (!g_asm[k].used) {
            a = &g_asm[k];
            memset(a, 0, sizeof(*a));
            a->used = 1;
            a->id = id;
        }
    }
    if (a == NULL || a->size + (size - i) > BIG_MAX) {
        return;
    }
    {
        u8 *grown = (u8 *)realloc(a->bytes, a->size + (size - i) + 1u);

        if (grown == NULL) {
            return;
        }
        a->bytes = grown;
    }
    memcpy(a->bytes + a->size, p + i, size - i);
    a->size += size - i;
    a->done = last != 0;
}

/* "@<id>": the assembled bytes, taken out of the table (the caller frees). */
static u8 *take_assembled(const u8 *p, u32 size, u32 *out_size)
{
    u32 id, k;

    if (size < 2 || p[0] != '@') {
        return NULL;
    }
    id = lead_number(p + 1, size - 1);
    for (k = 0; k < ASSEMBLY_MAX; k++) {
        if (g_asm[k].used && g_asm[k].done && g_asm[k].id == id) {
            u8 *b = g_asm[k].bytes;

            *out_size = g_asm[k].size;
            memset(&g_asm[k], 0, sizeof(g_asm[k]));
            return b;
        }
    }
    return NULL;
}

/* A frame's text, with its body (after the first newline, or the whole
 * payload when `whole`) put together from F pieces when it is "@<id>". */
static void push_with_body(u32 event, u32 kind, const u8 *p, u32 size, int whole)
{
    u32 nl = 0, body_size = 0;
    u8 *body;

    if (!whole) {
        while (nl < size && p[nl] != '\n') {
            nl++;
        }
        if (nl >= size) {
            push(event, kind, lead_number(p, size), p, size);
            return;
        }
        nl++;
    }
    body = take_assembled(p + nl, size - nl, &body_size);
    if (body == NULL) {
        push(event, kind, lead_number(p, size), p, size);
        return;
    }
    {
        u8 *all = (u8 *)malloc(nl + body_size + 1u);

        if (all != NULL) {
            memcpy(all, p, nl);
            memcpy(all + nl, body, body_size);
            push(event, kind, whole ? 0u : lead_number(p, size), all, nl + body_size);
            free(all);
        }
        free(body);
    }
}

/* The reader's hook: every frame of a world session. */
static void world_in(char type, const u8 *p, u32 size)
{
    switch (type) {
    case 'F':
        piece(p, size);
        break;
    case 'R':
        if (size >= 5 && memcmp(p, "wait ", 5) == 0) {
            push(BW_EVENT_WORLD, BW_WORLD_WAIT, 0, p + 5, size - 5);
        } else {
            push(BW_EVENT_WORLD, BW_WORLD_GENERATED, 0, p, size);
        }
        break;
    case 'O':
        push(BW_EVENT_WORLD, BW_WORLD_OWNER_DONE, 0, p, size);
        break;
    case 'S':
        push_with_body(BW_EVENT_WORLD, BW_WORLD_JOINED, p, size, 0);
        break;
    case 'Y':
        push(BW_EVENT_WORLD, BW_WORLD_ITEMS, 0, p, size);
        break;
    case 'U':
        push(BW_EVENT_WORLD, BW_WORLD_POSITION, lead_number(p, size), p, size);
        break;
    case 'W':
    case 'P':
        push(BW_EVENT_WORLD, BW_WORLD_PLAYER_IN, lead_number(p, size), p, size);
        break;
    case 'X':
        push(BW_EVENT_WORLD, BW_WORLD_PLAYER_OUT, lead_number(p, size), p, size);
        break;
    case 'E':
        push(BW_EVENT_WORLD, BW_WORLD_ERROR, 0, p, size);
        if (size >= 3 && memcmp(p, "ap-", 3) == 0) {
            push(BW_EVENT_AP, BW_AP_ERROR, 0, p, size);
        }
        break;
    case 'L':
        push(BW_EVENT_AP, BW_AP_LINKED, 0, p, size);
        break;
    case 'T':
        push_with_body(BW_EVENT_AP, BW_AP_MESSAGE, p, size, 1);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static u32 next_ticket(void)
{
    if (++g_ticket == 0) {
        g_ticket = 1;
    }
    return g_ticket;
}

static int ensure_open(void)
{
    bw_mod_net_set_world_hook(world_in);
    if (bw_mod_net_world_open() != BW_MOD_NET_OK) {
        return -1;
    }
    g_was_open = 1;
    return 0;
}

/*
 * One frame `type` whose text is `head` (a line) followed by `body`
 * (after a newline when `sep`).  A frame bigger than a piece sends the
 * body as F pieces first and "@<id>" in its place.
 */
static int send_big(char type, const char *head, int sep, const u8 *body, u32 body_size)
{
    u32 head_size = (u32)strlen(head);
    u8 *frame;
    int rc;

    if (head_size + 1u + body_size <= PIECE_MAX) {
        frame = (u8 *)malloc(head_size + 1u + body_size + 1u);
        if (frame == NULL) {
            return -1;
        }
        memcpy(frame, head, head_size);
        if (sep) {
            frame[head_size++] = '\n';
        }
        memcpy(frame + head_size, body, body_size);
        rc = bw_mod_net_world_send(type, frame, head_size + body_size);
        free(frame);
        return rc;
    }
    {
        u32 id = ++g_out_id, at = 0;
        char line[64], ref[40];

        frame = (u8 *)malloc(PIECE_MAX + 64u);
        if (frame == NULL) {
            return -1;
        }
        while (at < body_size) {
            u32 n = body_size - at > PIECE_MAX ? PIECE_MAX : body_size - at;
            int len = snprintf(line, sizeof(line), "%u %u\n", id, at + n >= body_size ? 1u : 0u);

            memcpy(frame, line, (size_t)len);
            memcpy(frame + len, body + at, n);
            if (bw_mod_net_world_send('F', frame, (u32)len + n) != 0) {
                free(frame);
                return -1;
            }
            at += n;
        }
        free(frame);
        snprintf(ref, sizeof(ref), "@%u", id);
        frame = (u8 *)malloc(head_size + 2u + strlen(ref));
        if (frame == NULL) {
            return -1;
        }
        memcpy(frame, head, head_size);
        if (sep) {
            frame[head_size++] = '\n';
        }
        memcpy(frame + head_size, ref, strlen(ref));
        rc = bw_mod_net_world_send(type, frame, head_size + (u32)strlen(ref));
        free(frame);
        return rc;
    }
}

static void token(char out[33])
{
    bw_mod_net_random_hex(out);
}

/* A field the frames take as one word: no space, no newline, not empty. */
static int word_ok(const char *s, size_t max)
{
    size_t n = s != NULL ? strlen(s) : 0, i;

    if (n == 0 || n > max) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if ((unsigned char)s[i] <= ' ') {
            return 0;
        }
    }
    return 1;
}

int bw_mod_world_create(const char *config_json, u32 *ticket)
{
    char head[96], tok[33];
    size_t n = config_json != NULL ? strlen(config_json) : 0;

    if (n == 0 || n > 64u * 1024u || ensure_open() != 0) {
        return -1;
    }
    token(tok);
    snprintf(head, sizeof(head), "%s %s", WORLD_PROTO, tok);
    if (send_big('N', head, 1, (const u8 *)config_json, (u32)n) != 0) {
        return -1;
    }
    g_create_ticket = next_ticket();
    if (ticket != NULL) {
        *ticket = g_create_ticket;
    }
    return 0;
}

int bw_mod_world_owner(const char *code, const char *owner_key, u32 action)
{
    static const char *const names[] = { "start", "close", "delete" };
    char frame[160];
    int n;

    if (action > BW_WORLD_OWNER_DELETE || !word_ok(code, 16) || !word_ok(owner_key, 64) ||
        ensure_open() != 0) {
        return -1;
    }
    n = snprintf(frame, sizeof(frame), "%s %s %s %s", WORLD_PROTO, code, owner_key, names[action]);
    return bw_mod_net_world_send('O', frame, (u32)n);
}

/*
 * The names this run joined worlds with, by code.  A join with a stored
 * player key can meet `E bad-key` when metaxy released the claim before the
 * game confirmed it (the connection ended before the V after the push):
 * the port then joins once more by the name, and the mod keeps the new key
 * the push carries (metaxy SPEC, section 21).  Without a name for the code
 * the error goes to the mod, which joins by name itself.
 */
#define NAMES_MAX 8

typedef struct JoinName {
    char code[17];
    char name[65];
} JoinName;

static JoinName g_names[NAMES_MAX];
static char     g_join_code[17];
static int      g_join_by_key;     /* the join in flight used a key */
static int      g_join_retried;

/* 32 hex digits: metaxy takes such a field as a key. */
static int is_key(const char *s)
{
    size_t i;

    if (strlen(s) != 32) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        char c = s[i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static const char *name_for(const char *code)
{
    u32 i;

    for (i = 0; i < NAMES_MAX; i++) {
        if (g_names[i].code[0] != '\0' && strcmp(g_names[i].code, code) == 0) {
            return g_names[i].name;
        }
    }
    return NULL;
}

static void keep_name(const char *code, const char *name)
{
    u32 i, at = 0;

    for (i = 0; i < NAMES_MAX; i++) {
        if (g_names[i].code[0] == '\0' || strcmp(g_names[i].code, code) == 0) {
            at = i;
            break;
        }
        at = (at + 1) % NAMES_MAX;
    }
    snprintf(g_names[at].code, sizeof(g_names[at].code), "%s", code);
    snprintf(g_names[at].name, sizeof(g_names[at].name), "%s", name);
}

static int send_join(const char *code, const char *name_or_key)
{
    char frame[200], tok[33];
    int n;

    if (ensure_open() != 0) {
        return -1;
    }
    token(tok);
    n = snprintf(frame, sizeof(frame), "%s %s %s %s", WORLD_PROTO, tok, code, name_or_key);
    if (n < 0 || (size_t)n >= sizeof(frame) || bw_mod_net_world_send('I', frame, (u32)n) != 0) {
        return -1;
    }
    snprintf(g_join_code, sizeof(g_join_code), "%s", code);
    g_join_by_key = is_key(name_or_key);
    return 0;
}

int bw_mod_world_join(const char *code, const char *name_or_key, u32 *ticket)
{
    if (!word_ok(code, 16) || name_or_key == NULL || name_or_key[0] == '\0' ||
        strlen(name_or_key) > 64 || strchr(name_or_key, '\n') != NULL) {
        return -1;
    }
    if (!is_key(name_or_key)) {
        keep_name(code, name_or_key);
    }
    if (send_join(code, name_or_key) != 0) {
        return -1;
    }
    g_join_retried = 0;
    g_join_ticket = next_ticket();
    if (ticket != NULL) {
        *ticket = g_join_ticket;
    }
    return 0;
}

/* `E bad-key` for a join by key: join again by the name, once.  1 when the
 * join went out again (the error is not the mod's then). */
static int retry_by_name(const WorldMsg *m)
{
    const char *name;

    if (m->event != BW_EVENT_WORLD || m->kind != BW_WORLD_ERROR || !g_join_by_key ||
        g_join_retried || m->size < 7 || memcmp(m->bytes, "bad-key", 7) != 0) {
        return 0;
    }
    name = name_for(g_join_code);
    if (name == NULL) {
        return 0;
    }
    g_join_retried = 1;
    /* metaxy closes the connection after an error: the join opens it again */
    return send_join(g_join_code, name) == 0;
}

/* Send checks in V frames of at most a piece's size. */
static int send_checks(const u32 *ids, u32 count)
{
    char frame[PIECE_MAX + 16];
    u32 i, len = 0;

    for (i = 0; i < count; i++) {
        char one[16];
        int n = snprintf(one, sizeof(one), "%s%u", len == 0 ? "" : " ", ids[i]);

        if (len + (u32)n > PIECE_MAX) {
            if (bw_mod_net_world_send('V', frame, len) != 0) {
                return -1;
            }
            len = 0;
            n = snprintf(one, sizeof(one), "%u", ids[i]);
        }
        memcpy(frame + len, one, (size_t)n);
        len += (u32)n;
    }
    if (len > 0 && bw_mod_net_world_send('V', frame, len) != 0) {
        return -1;
    }
    return 0;
}

int bw_mod_world_checks(const u32 *location_ids, u32 count)
{
    u32 i, k;

    if (location_ids == NULL || count == 0 || !bw_mod_net_world_running()) {
        return -1;
    }
    if (count == 1 && location_ids[0] == 0) {
        return bw_mod_net_world_send('V', "goal", 4);
    }
    /* keep them for the next join */
    for (i = 0; i < count; i++) {
        int seen = 0;

        for (k = 0; k < g_check_count && !seen; k++) {
            seen = g_checks[k] == location_ids[i];
        }
        if (!seen && g_check_count < CHECKS_MAX) {
            if (g_checks == NULL) {
                g_checks = (u32 *)malloc(CHECKS_MAX * sizeof(u32));
            }
            if (g_checks != NULL) {
                g_checks[g_check_count++] = location_ids[i];
            }
        }
    }
    return send_checks(location_ids, count);
}

void bw_mod_world_leave(void)
{
    bw_mod_net_world_close();
    g_check_count = 0;
    g_self = 0;
}

static int read_current(u32 event, void *out, u32 size)
{
    if (g_current == NULL || g_current->event != event || (out == NULL && size != 0)) {
        return -1;
    }
    if (size != 0) {
        memcpy(out, g_current->bytes, g_current->size < size ? g_current->size : size);
    }
    return (int)g_current->size;
}

int bw_mod_world_read(void *out, u32 size)
{
    return read_current(BW_EVENT_WORLD, out, size);
}

int bw_mod_ap_read(void *out, u32 size)
{
    return read_current(BW_EVENT_AP, out, size);
}

int bw_mod_ap_link(const char *room, const char *slot, const char *password, u32 *ticket)
{
    char head[96], tok[33];
    size_t n = room != NULL ? strlen(room) : 0;

    /* the slot and the password go in Archipelago's own Connect message,
     * which the mod sends with ap_send once the link is up; the port only
     * checks that they are given */
    (void)password;
    if (n == 0 || n > 512 || slot == NULL || strchr(room, '\n') != NULL || ensure_open() != 0) {
        return -1;
    }
    token(tok);
    snprintf(head, sizeof(head), "%s %s", WORLD_PROTO, tok);
    if (send_big('L', head, 1, (const u8 *)room, (u32)n) != 0) {
        return -1;
    }
    g_ap_ticket = next_ticket();
    if (ticket != NULL) {
        *ticket = g_ap_ticket;
    }
    return 0;
}

int bw_mod_ap_send(const char *json, u32 size)
{
    if (json == NULL || size == 0 || size > BIG_MAX || !bw_mod_net_world_running()) {
        return -1;
    }
    return send_big('T', "", 0, (const u8 *)json, size);
}

/* ------------------------------------------------------------------ */
/* Other players in the field                                          */
/* ------------------------------------------------------------------ */

/*
 * With peer_position on, the port sends this game's zone on every change
 * and its tile while another player shares the zone (at most every eighth
 * frame: metaxy passes 8 a second), and draws the other players of the
 * world session in the same zone as field avatars (sdk/mod_avatar.h).
 * The game deletes avatars at each map change, so they are made again in
 * the new zone.  The sprite is the hero's (object code 1): the session
 * does not say which hero another player plays.
 */
#define PEERS_MAX     16
#define AVATAR_SPRITE 1u

/* engine/avatar/avatar.h (weak: the tests link without the engine) */
extern u32 avatar_field_code(const u8 look_bytes[5]) __attribute__((weak));
#define TILE_FRAMES   8u

typedef struct WorldPeer {
    int  present;
    u32  zone;
    s32  x, y, z;
    u32  dir;
    u32  avatar;     /* 0: none */
} WorldPeer;

static int       g_share;
static WorldPeer g_peers[PEERS_MAX + 1];
static u32       g_my_zone = 0xFFFFFFFFu;
static s32       g_my_x, g_my_z;
static u32       g_tile_wait;

static void peer_show(u32 n)
{
    WorldPeer *w = &g_peers[n];

    if (!g_share || !w->present || w->zone != g_my_zone) {
        if (w->avatar != 0) {
            bw_mod_avatar_delete(w->avatar);
            w->avatar = 0;
        }
        return;
    }
    if (w->avatar == 0) {
        /* a world's players send no look: the game's own colours, not
         * this player's (a private code with no look) */
        static const u8 plain[5] = { 0, 0, 0, 0, 0 };

        w->avatar = bw_mod_avatar_create(avatar_field_code != NULL ? avatar_field_code(plain) : AVATAR_SPRITE,
                                         (s16)w->x, (s16)w->z, (u8)w->dir);
        return;
    }
    bw_mod_avatar_move(w->avatar, (s16)w->x, (s16)w->y, (s16)w->z, (u8)w->dir, 0);
}

int bw_mod_world_peer_position(int on)
{
    u32 i;

    g_share = on != 0;
    for (i = 1; i <= PEERS_MAX; i++) {
        peer_show(i);
    }
    g_my_zone = 0xFFFFFFFFu;
    return 0;
}

/* A position frame: player, zone, x, y, z and direction. */
static void peer_position_in(const u8 *bytes)
{
    unsigned int player, zone, dir;
    int x, y, z;

    if (sscanf((const char *)bytes, "%u %u %d %d %d %u", &player, &zone, &x, &y, &z, &dir) != 6 ||
        player == 0 || player > PEERS_MAX || player == g_self) {
        return;
    }
    g_peers[player].present = 1;
    g_peers[player].zone = zone;
    g_peers[player].x = x;
    g_peers[player].y = y;
    g_peers[player].z = z;
    g_peers[player].dir = dir;
    peer_show(player);
}

static void peer_gone(u32 player)
{
    if (player == 0 || player > PEERS_MAX) {
        return;
    }
    g_peers[player].present = 0;
    peer_show(player);
}

static int zone_shared(void)
{
    u32 i;

    for (i = 1; i <= PEERS_MAX; i++) {
        if (g_peers[i].present && g_peers[i].zone == g_my_zone) {
            return 1;
        }
    }
    return 0;
}

static void send_position(u32 zone, s16 x, s16 y, s16 z)
{
    char frame[64];
    int n = snprintf(frame, sizeof(frame), "%u %d %d %d %u", zone, x, y, z,
                     (unsigned)bw_mod_field_context.dir);

    (void)bw_mod_net_world_send('U', frame, (u32)n);
    g_my_x = x;
    g_my_z = z;
    g_tile_wait = TILE_FRAMES;
}

static void position_frame(void)
{
    const BwModState *st = bw_mod_state_table();
    s16 x, y, z;
    u32 zone, i;

    if (!g_share || g_self == 0 || st == NULL || st->zone == NULL || st->player_pos == NULL ||
        st->player_pos(&x, &y, &z) != 0) {
        return;
    }
    zone = st->zone();
    if (zone != g_my_zone) {
        g_my_zone = zone;
        /* the map change took the avatars: make the ones of this zone */
        for (i = 1; i <= PEERS_MAX; i++) {
            g_peers[i].avatar = 0;
            peer_show(i);
        }
        send_position(zone, x, y, z);
        return;
    }
    if (g_tile_wait > 0) {
        g_tile_wait--;
    }
    if ((x != g_my_x || z != g_my_z) && g_tile_wait == 0 && zone_shared()) {
        send_position(zone, x, y, z);
    }
}

/* ------------------------------------------------------------------ */
/* Town Map markers                                                    */
/* ------------------------------------------------------------------ */

/*
 * The markers a mod sets: an id, a zone, an icon and a name.  The drawing
 * sides (the game's Town Map, the one-screen Town Map) read them with
 * bw_mod_townmap_marker_at.
 */
#define MARKERS_MAX 32

static BwTownmapMarker g_markers[MARKERS_MAX];

int bw_mod_townmap_marker_set(u32 id, u16 zone, u32 icon, const char *name)
{
    u32 i, at = MARKERS_MAX;

    for (i = 0; i < MARKERS_MAX; i++) {
        if (g_markers[i].used && g_markers[i].id == id) {
            at = i;
            break;
        }
        if (!g_markers[i].used && at == MARKERS_MAX) {
            at = i;
        }
    }
    if (at == MARKERS_MAX) {
        return -1;
    }
    g_markers[at].used = 1;
    g_markers[at].id = id;
    g_markers[at].zone = zone;
    g_markers[at].icon = icon;
    snprintf(g_markers[at].name, sizeof(g_markers[at].name), "%s", name != NULL ? name : "");
    return 0;
}

int bw_mod_townmap_marker_clear(u32 id)
{
    u32 i;

    for (i = 0; i < MARKERS_MAX; i++) {
        if (g_markers[i].used && g_markers[i].id == id) {
            g_markers[i].used = 0;
            return 0;
        }
    }
    return -1;
}

const BwTownmapMarker *bw_mod_townmap_marker_at(u32 n)
{
    u32 i, seen = 0;

    for (i = 0; i < MARKERS_MAX; i++) {
        if (g_markers[i].used && seen++ == n) {
            return &g_markers[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The game's frame                                                    */
/* ------------------------------------------------------------------ */

static u32 ticket_of(const WorldMsg *m)
{
    if (m->event == BW_EVENT_AP) {
        return g_ap_ticket;
    }
    switch (m->kind) {
    case BW_WORLD_GENERATED:
    case BW_WORLD_WAIT:
        return g_create_ticket;
    case BW_WORLD_JOINED:
        return g_join_ticket;
    case BW_WORLD_PLAYER_IN:
        return m->player == g_self ? g_join_ticket : 0u;
    case BW_WORLD_ERROR:
        return g_join_ticket != 0 ? g_join_ticket : g_create_ticket;
    default:
        return 0;
    }
}

void bw_mod_world_frame(void)
{
    for (;;) {
        WorldMsg *m;
        BwEventPayload p;

        wlock();
        m = g_head;
        if (m != NULL) {
            g_head = m->next;
            if (g_head == NULL) {
                g_tail = NULL;
            }
        }
        wunlock();
        if (m == NULL) {
            break;
        }
        if (retry_by_name(m)) {
            free(m->bytes);
            free(m);
            continue;
        }
        if (m->event == BW_EVENT_WORLD && m->kind == BW_WORLD_POSITION) {
            peer_position_in(m->bytes);
        }
        if (m->event == BW_EVENT_WORLD && m->kind == BW_WORLD_PLAYER_OUT) {
            peer_gone(m->player);
        }
        if (m->event == BW_EVENT_WORLD && m->kind == BW_WORLD_JOINED) {
            g_self = m->player;
        }
        if (m->event == BW_EVENT_WORLD && m->kind == BW_WORLD_GENERATED && bw_games_world_code != NULL) {
            /* "<code> <owner key>": the new game's save lists the world's
             * code under Your games (sdk/save_games.h) */
            char code[17];
            u32 k = 0;

            while (k < m->size && k < sizeof code - 1 && m->bytes[k] != ' ') {
                code[k] = (char)m->bytes[k];
                k++;
            }
            code[k] = '\0';
            bw_games_world_code(code);
        }
        memset(&p, 0, sizeof(p));
        if (m->event == BW_EVENT_WORLD) {
            p.world.kind = m->kind;
            p.world.size = m->size;
            p.world.ticket = ticket_of(m);
            p.world.player = m->player;
        } else {
            p.ap.kind = m->kind;
            p.ap.size = m->size;
            p.ap.ticket = ticket_of(m);
        }
        g_current = m;
        (void)bw_mod_event_fire(m->event, &p);
        g_current = NULL;
        if (m->event == BW_EVENT_WORLD && m->kind == BW_WORLD_JOINED) {
            /* After the mod's handler has kept the player key: the checks
             * of this session, at once and on every join.  metaxy ignores
             * the ones it has, so one made while the link was down is not
             * lost, and this V (empty when there are none) confirms a
             * first join's claim of its name (metaxy SPEC, section 21). */
            if (g_check_count > 0) {
                (void)send_checks(g_checks, g_check_count);
            } else {
                (void)bw_mod_net_world_send('V', "", 0);
            }
        }
        free(m->bytes);
        free(m);
    }
    if (bw_mod_net_world_running()) {
        position_frame();
    }
    /* the link ended (an error, or the connection was lost) */
    if (g_was_open && !bw_mod_net_world_running()) {
        BwEventPayload p;

        g_was_open = 0;
        memset(&p, 0, sizeof(p));
        p.world.kind = BW_WORLD_CLOSED;
        (void)bw_mod_event_fire(BW_EVENT_WORLD, &p);
    }
}

/* For the tests: hand the reader's hook a frame as if metaxy sent it. */
void bw_mod_world_feed(char type, const u8 *payload, u32 size)
{
    world_in(type, payload, size);
}
