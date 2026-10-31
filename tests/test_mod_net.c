/*
 * tests/test_mod_net.c -- the peer sessions and the external control: the
 * relay protocol the port speaks, the session it keeps, the frame the
 * handlers run on, and the local endpoint the tools connect to
 * (docs/mod-services.md, "Peer sessions" and "External control";
 * sdk/mod_net.c).
 *
 * What is checked here:
 *
 *   - the transport seam: with no transport the session calls say "no
 *     relay", with a transport but no relay address they say so too, a
 *     connection that fails is an IO error, a bad argument is refused and
 *     a send without a session says so;
 *   - the frames the client writes: the host request, the join request, a
 *     message and the leave, each one of the form
 *     "<type> <length>\n<payload>", and the three things the port has to
 *     know to reach the relay (the host, the port and the code);
 *   - the relay's answers: the code, the peer's own number, a join, a
 *     drop, a message and an error, and the reason an error carries is
 *     reported;
 *   - the session: the host call gives the code back, the join reaches the
 *     handler, a message reaches the mod with the same key and nobody
 *     else, the messages keep their order, a drop is reported, and the
 *     peer number is the relay's own;
 *   - the frame: the handler runs on the frame and nowhere else, so a
 *     message that has arrived does not reach the handler until the port's
 *     frame hook is called;
 *   - the limits: a message over 4 KB is refused, a message of exactly
 *     4 KB is taken, and a transport that refuses the write is reported;
 *   - a frame the port did not expect: the stream is refused, the
 *     connection is closed and the peers are dropped;
 *   - the external control: a tool connects, its command reaches the
 *     handler, the port publishes to the connected tools, the listener is
 *     on 127.0.0.1 only, and the port's reset stops it and drops the
 *     tools.
 *
 * The relay is stood in for: the transport under the client is a pair of
 * buffers, and the test's own miniature relay writes the frames the
 * protocol says into one of them and reads the frames the client wrote
 * out of the other, so the test needs no network at all.  The tool that
 * connects to the external control is the port's own client
 * (bw_mod_net_tcp_transport), so the control is reached the way a stream
 * overlay would reach it, and the "127.0.0.1 only" rule is checked with
 * that same client against the other loopback address.
 *
 * The saved-data layer is the port's own (sdk/mod_savedata.c): the
 * namespace in force is the one the layer holds, the way the loader puts
 * it around every call it makes into a mod, so a handler is bound under
 * a mod's own name and a message is keyed on it.  Nothing else is stood
 * in for: the frame hook is the port's own call (bw_mod_net_frame) and
 * the relay's address and the control's port are the port's own calls.
 *
 * Nothing here needs game data: the network is the port's own, and the
 * relay is a stand-in.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_net.h"
#include "plat_thread.h"
#include "types.h"

#include "port_test.h"

/*
 * A step the test cannot go on without.  It is checked the way the
 * framework's own require is, but the test's parts are void functions, so
 * the step ends the part and not the whole test.
 */
#define BW_STEP(label, cond)                                             \
    do {                                                                  \
        bw_checks++;                                                      \
        if (!(cond)) {                                                    \
            bw_failures++;                                                \
            printf("  FAIL %-40s (%s)\n", (label), #cond);                \
            return;                                                       \
        }                                                                 \
    } while (0)

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/*
 * The saved-data layer's own calls (sdk/mod_savedata.c has no header of its
 * own; its prototypes live at the top of that file).  The loader puts the
 * mod's namespace in force around every call it makes into a mod, and the
 * network's service asks the layer which mod is calling, so the test does
 * the same to be a mod.
 */
extern void        bw_mod_savedata_set_namespace(const char *ns);
extern const char *bw_mod_savedata_namespace(void);

/* ------------------------------------------------------------------ */
/* The test's own names                                                */
/* ------------------------------------------------------------------ */

/* The mods the test's handlers belong to. */
#define T_NS_A "alpha"
#define T_NS_B "beta"

/* The relay the port is pointed at. */
#define T_RELAY_HOST "relay.test"
#define T_RELAY_PORT 4242

/* The name this player has in a session. */
#define T_NAME "host-player"

/* The code the relay hands out. */
#define T_CODE "ABC123"

/* The code the relay hands out for the join. */
#define T_JOIN_CODE "XYZ789"

/* The protocol version the client states in its requests. */
#define T_PROTO "eidos-net/1"

/* The port the external control listens on.  The first one that is free
 * is used, so a port another program holds does not fail the test. */
#define T_CONTROL_PORT  45671
#define T_CONTROL_PORTS 8

/* ------------------------------------------------------------------ */
/* The relay, stood in for                                             */
/* ------------------------------------------------------------------ */

/*
 * The transport under the client (BwModNetTransport) is a pair of buffers:
 * what the client wrote is kept in one, and what the relay says is
 * written into the other.  The relay is a miniature one: it says a frame
 * with relay_say, and the test reads the frames the client wrote with
 * relay_take.
 *
 * The port serializes the transport's calls (the client holds its own
 * lock around every one of them), but the test's own side of the buffer
 * is written by the test's thread while the port's reader thread reads
 * it, so the test's own lock is here.
 */
#define T_BUF 65536

typedef struct FakeRelay {
    BwMutex *lock;
    u8   to_relay[T_BUF];        /* the bytes the client wrote    */
    u32  to_relay_len;
    u8   to_client[T_BUF];       /* the bytes the relay says      */
    u32  to_client_len;
    u32  to_client_pos;          /* the bytes the client has read */
    char host[BW_MOD_NET_HOST_MAX];
    u16  port;
    int  connected;
    int  closed;
    int  fail_connect;
    int  fail_send;
} FakeRelay;

static FakeRelay g_relay;

/* The transport the port is given.  Its own calls are filled in when the
 * test starts, so the table can name the functions below. */
static BwModNetTransport g_fake_transport;

/* ------------------------------------------------------------------ */
/* The transport                                                       */
/* ------------------------------------------------------------------ */

static int fake_connect(void *user, const char *host, u16 port)
{
    FakeRelay *r = (FakeRelay *)user;

    bw_mutex_lock(r->lock);
    snprintf(r->host, sizeof r->host, "%s", (host != NULL) ? host : "");
    r->port = port;
    if (r->fail_connect) {
        bw_mutex_unlock(r->lock);
        return -1;
    }
    r->connected = 1;
    r->closed = 0;
    bw_mutex_unlock(r->lock);
    return 0;
}

static int fake_send(void *user, const void *buf, u32 size)
{
    FakeRelay *r = (FakeRelay *)user;

    bw_mutex_lock(r->lock);
    if (r->fail_send) {
        bw_mutex_unlock(r->lock);
        return -1;
    }
    if (size > T_BUF - r->to_relay_len) {
        bw_mutex_unlock(r->lock);
        return -1;
    }
    if (size > 0) {
        memcpy(r->to_relay + r->to_relay_len, buf, size);
    }
    r->to_relay_len += size;
    bw_mutex_unlock(r->lock);
    return 0;
}

static int fake_recv(void *user, void *buf, u32 size, u32 *out_size)
{
    FakeRelay *r = (FakeRelay *)user;
    u32 n;

    *out_size = 0;
    bw_mutex_lock(r->lock);
    n = r->to_client_len - r->to_client_pos;
    if (n > size) {
        n = size;
    }
    if (n > 0) {
        memcpy(buf, r->to_client + r->to_client_pos, n);
        r->to_client_pos += n;
    }
    *out_size = n;
    bw_mutex_unlock(r->lock);
    if (n == 0) {
        return 0;                   /* nothing there right now */
    }
    return 1;
}

static void fake_close(void *user)
{
    FakeRelay *r = (FakeRelay *)user;

    bw_mutex_lock(r->lock);
    r->closed = 1;
    r->connected = 0;
    bw_mutex_unlock(r->lock);
}

/* ------------------------------------------------------------------ */
/* The miniature relay                                                */
/* ------------------------------------------------------------------ */

/* Forget everything the relay and the client said. */
static void relay_reset(void)
{
    bw_mutex_lock(g_relay.lock);
    g_relay.to_relay_len = 0;
    g_relay.to_client_len = 0;
    g_relay.to_client_pos = 0;
    g_relay.connected = 0;
    g_relay.closed = 0;
    g_relay.fail_connect = 0;
    g_relay.fail_send = 0;
    g_relay.host[0] = '\0';
    g_relay.port = 0;
    bw_mutex_unlock(g_relay.lock);
}

/* Forget what the client wrote, and leave the relay's own side alone. */
static void relay_forget_written(void)
{
    bw_mutex_lock(g_relay.lock);
    g_relay.to_relay_len = 0;
    bw_mutex_unlock(g_relay.lock);
}

/* The relay says a frame: the type, the length, a newline and the
 * payload.  This is the protocol's own shape. */
static void relay_say_n(char type, const void *payload, u32 size)
{
    char head[32];
    int n;

    n = snprintf(head, sizeof head, "%c %u\n", type, (unsigned int)size);
    bw_mutex_lock(g_relay.lock);
    if (n > 0 && (u32)n + size <= T_BUF - g_relay.to_client_len) {
        memcpy(g_relay.to_client + g_relay.to_client_len, head, (size_t)n);
        g_relay.to_client_len += (u32)n;
        if (size > 0 && payload != NULL) {
            memcpy(g_relay.to_client + g_relay.to_client_len, payload, size);
            g_relay.to_client_len += size;
        }
    }
    bw_mutex_unlock(g_relay.lock);
}

/* The relay says a frame whose payload is text. */
static void relay_say(char type, const char *payload)
{
    relay_say_n(type, payload, (payload != NULL) ? (u32)strlen(payload) : 0u);
}

/* The relay writes bytes that are not a frame: the port must refuse the
 * stream. */
static void relay_write_raw(const char *text)
{
    size_t n;

    if (text == NULL) {
        return;
    }
    n = strlen(text);
    bw_mutex_lock(g_relay.lock);
    if (n <= T_BUF - g_relay.to_client_len) {
        memcpy(g_relay.to_client + g_relay.to_client_len, text, n);
        g_relay.to_client_len += (u32)n;
    }
    bw_mutex_unlock(g_relay.lock);
}

/* The first bytes the client wrote, so the shape of the frame can be
 * looked at before it is taken. */
static void relay_peek(char *out, size_t size)
{
    size_t n;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    bw_mutex_lock(g_relay.lock);
    n = g_relay.to_relay_len;
    if (n > size - 1) {
        n = size - 1;
    }
    memcpy(out, g_relay.to_relay, n);
    out[n] = '\0';
    bw_mutex_unlock(g_relay.lock);
}

/* One frame the client wrote: its type and its payload. */
typedef struct RelayFrame {
    char type;
    u32  size;
    char payload[8192];
} RelayFrame;

/*
 * Take the next frame the client wrote out of the buffer.  1 when there is
 * one, 0 when there is none.  The frame is read the way the protocol says
 * it is: the type, a space, the length in decimal, a newline, and the
 * payload.  Bytes that are not that are not a frame, and the answer is 0.
 */
static int relay_take(RelayFrame *out)
{
    u32 pos;
    u32 len = 0;
    int digits = 0;

    bw_mutex_lock(g_relay.lock);
    if (g_relay.to_relay_len < 4 || g_relay.to_relay[1] != ' ') {
        bw_mutex_unlock(g_relay.lock);
        return 0;
    }
    out->type = (char)g_relay.to_relay[0];
    pos = 2;
    while (pos < g_relay.to_relay_len && g_relay.to_relay[pos] != (u8)'\n') {
        u8 c = g_relay.to_relay[pos];

        if (c < (u8)'0' || c > (u8)'9') {
            bw_mutex_unlock(g_relay.lock);
            return 0;
        }
        len = len * 10u + (u32)(c - (u8)'0');
        digits++;
        pos++;
    }
    if (digits == 0 || pos >= g_relay.to_relay_len) {
        bw_mutex_unlock(g_relay.lock);
        return 0;
    }
    pos++;                         /* the newline */
    if (len > sizeof(out->payload) - 1 ||
        pos + len > g_relay.to_relay_len) {
        bw_mutex_unlock(g_relay.lock);
        return 0;
    }
    memcpy(out->payload, g_relay.to_relay + pos, len);
    out->payload[len] = '\0';
    out->size = len;
    memmove(g_relay.to_relay, g_relay.to_relay + pos + len,
            g_relay.to_relay_len - (pos + len));
    g_relay.to_relay_len -= (pos + len);
    bw_mutex_unlock(g_relay.lock);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The mods' own handlers                                             */
/* ------------------------------------------------------------------ */

/* How many messages a handler log keeps, to check the order. */
#define T_MESSAGES_MAX 8

typedef struct HandlerLog {
    int  joins;
    int  leaves;
    int  messages;
    int  calls;
    u32  join_peer;
    u32  leave_peer;
    u32  msg_peer;
    char msg_text[256];
    char order[T_MESSAGES_MAX][256];
    int  order_count;
} HandlerLog;

/* What a command handler was told. */
typedef struct CommandLog {
    int  calls;
    char text[256];
} CommandLog;

static HandlerLog g_alpha_session;
static HandlerLog g_beta_session;
static CommandLog g_alpha_command;

static void log_reset(HandlerLog *log)
{
    memset(log, 0, sizeof(*log));
}

/*
 * What a handler was told.  The three kinds are kept apart, because a
 * frame carries a join and a message at once and the last call is not
 * always the one the test is looking at.
 */
static void note_session(HandlerLog *log, u32 peer, int kind,
                         const void *buf, u32 size)
{
    log->calls++;
    if (kind == (int)BW_SESSION_JOIN) {
        log->joins++;
        log->join_peer = peer;
        return;
    }
    if (kind == (int)BW_SESSION_LEAVE) {
        log->leaves++;
        log->leave_peer = peer;
        return;
    }
    log->messages++;
    log->msg_peer = peer;
    log->msg_text[0] = '\0';
    if (buf != NULL && size > 0) {
        u32 n = size;

        if (n > sizeof(log->msg_text) - 1) {
            n = (u32)sizeof(log->msg_text) - 1;
        }
        memcpy(log->msg_text, buf, n);
        log->msg_text[n] = '\0';
    }
    if (log->order_count < T_MESSAGES_MAX) {
        snprintf(log->order[log->order_count],
                 sizeof(log->order[log->order_count]), "%s", log->msg_text);
    }
    log->order_count++;
}

static void on_alpha_session(u32 peer, int kind, const void *buf, u32 size)
{
    note_session(&g_alpha_session, peer, kind, buf, size);
}

static void on_beta_session(u32 peer, int kind, const void *buf, u32 size)
{
    note_session(&g_beta_session, peer, kind, buf, size);
}

static void on_alpha_command(const char *line)
{
    g_alpha_command.calls++;
    snprintf(g_alpha_command.text, sizeof g_alpha_command.text, "%s",
             (line != NULL) ? line : "");
}

/* ------------------------------------------------------------------ */
/* The port's stderr, taken over for the moment                        */
/* ------------------------------------------------------------------ */

/*
 * The relay's refusal is reported on the port's stderr, so the test takes
 * the stream over, lets the service write into a file, and reads the file
 * back.  The stream is put back where it was, so the report of the test
 * itself (stdout) and every later message are as they were.
 */

#ifdef _WIN32
#include <io.h>
#define T_DUP(fd)    _dup(fd)
#define T_DUP2(a, b) _dup2(a, b)
#define T_CLOSE(fd)  _close(fd)
#define T_FILENO(f)  _fileno(f)
#else
#include <unistd.h>
#define T_DUP(fd)    dup(fd)
#define T_DUP2(a, b) dup2(a, b)
#define T_CLOSE(fd)  close(fd)
#define T_FILENO(f)  fileno(f)
#endif

/* The file the reports are taken into, in the test's own directory. */
#define T_ERR_FILE "mod_net_stderr.txt"

/* The stderr file descriptor. */
#define T_STDERR 2

static int g_err_saved = -1;
static FILE *g_err_file;
static char g_err_text[1024];

/*
 * Take the port's stderr over, so what is written into it can be read.
 * The file is opened for reading and writing, so it is emptied for every
 * capture and read back from its own start.
 */
static void err_begin(void)
{
    fflush(stderr);
    g_err_file = fopen(T_ERR_FILE, "w+b");
    if (g_err_file == NULL) {
        return;
    }
    g_err_saved = T_DUP(T_STDERR);
    if (g_err_saved >= 0) {
        T_DUP2(T_FILENO(g_err_file), T_STDERR);
    }
}

/*
 * Give the port's stderr back and keep what was written into it.  The
 * file is read from its own start, so the whole report is there whatever
 * the stream had written.
 */
static const char *err_end(void)
{
    size_t n = 0;

    fflush(stderr);
    if (g_err_saved >= 0) {
        T_DUP2(g_err_saved, T_STDERR);
        T_CLOSE(g_err_saved);
        g_err_saved = -1;
    }
    g_err_text[0] = '\0';
    if (g_err_file != NULL) {
        fseek(g_err_file, 0, SEEK_SET);
        n = fread(g_err_text, 1, sizeof g_err_text - 1, g_err_file);
        fclose(g_err_file);
        g_err_file = NULL;
    }
    g_err_text[n] = '\0';
    return g_err_text;
}

/* The file the reports were taken into is the test's own litter: it goes. */
static void err_cleanup(void)
{
    remove(T_ERR_FILE);
}

/* Whether the report the service wrote says `what`. */
static int reported(const char *what)
{
    return strstr(g_err_text, what) != NULL;
}

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* Sleep for a moment: the network's own threads work in their own time. */
static void t_sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

/* How long a wait may take before the test gives up: long, as a busy CI
 * runner (ctest -j) can hold the listener thread back for seconds; a wait
 * that succeeds returns at once. */
#define T_WAIT_NS 10000000000ull

/* Wait until the port's own count of the peers is `want`. */
static int wait_peers(u32 want)
{
    unsigned long long deadline = bw_clock_ns() + T_WAIT_NS;

    while (bw_clock_ns() < deadline) {
        if (bw_mod_net_peer_count() == want) {
            return 1;
        }
        t_sleep_ms(1);
    }
    return (bw_mod_net_peer_count() == want);
}

/* Wait until this player's own peer number is `want`. */
static int wait_self_peer(u32 want)
{
    unsigned long long deadline = bw_clock_ns() + T_WAIT_NS;

    while (bw_clock_ns() < deadline) {
        if (bw_mod_net_self_peer() == want) {
            return 1;
        }
        t_sleep_ms(1);
    }
    return (bw_mod_net_self_peer() == want);
}

/* Wait until the transport is closed. */
static int wait_closed(void)
{
    unsigned long long deadline = bw_clock_ns() + T_WAIT_NS;

    for (;;) {
        int closed;

        bw_mutex_lock(g_relay.lock);
        closed = g_relay.closed;
        bw_mutex_unlock(g_relay.lock);
        if (closed) {
            return 1;
        }
        if (bw_clock_ns() >= deadline) {
            return 0;
        }
        t_sleep_ms(1);
    }
}

/* Wait until the control has `want` tools connected. */
static int wait_clients_for(int want, unsigned long long ns)
{
    unsigned long long deadline = bw_clock_ns() + ns;

    while (bw_clock_ns() < deadline) {
        if (bw_mod_net_control_clients() == want) {
            return 1;
        }
        t_sleep_ms(1);
    }
    return (bw_mod_net_control_clients() == want);
}

static int wait_clients(int want)
{
    return wait_clients_for(want, T_WAIT_NS);
}

/* Whether the token the client made is one the port could have made: a
 * run of thirty-two hexadecimal digits. */
static int is_token(const char *text)
{
    size_t i;

    if (text == NULL || strlen(text) != 32) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        char c = text[i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The transport seam                                                 */
/* ------------------------------------------------------------------ */

static void test_seam(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];

    printf("with no transport\n");
    bw_mod_net_reset();
    bw_mod_net_set_transport(NULL);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    BW_CHECK_EQ("no relay: the host call says so",
                bw_mod_net_session_host(code, sizeof code),
                (int)BW_MOD_NET_E_NO_RELAY);
    BW_CHECK_EQ("no relay: the code is empty", code[0], 0);
    BW_CHECK_EQ("no relay: the join says so",
                bw_mod_net_session_join(T_CODE),
                (int)BW_MOD_NET_E_NO_RELAY);
    BW_CHECK_EQ("no relay: there is no session",
                bw_mod_net_session_active(), 0);

    printf("with a transport but no relay to reach\n");
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay("", 0);
    BW_CHECK_EQ("no address: the host call says so",
                bw_mod_net_session_host(code, sizeof code),
                (int)BW_MOD_NET_E_NO_RELAY);
    bw_mod_net_set_relay(T_RELAY_HOST, 0);
    BW_CHECK_EQ("no port: the host call says so",
                bw_mod_net_session_host(code, sizeof code),
                (int)BW_MOD_NET_E_NO_RELAY);
    BW_CHECK_EQ("no port: the join says so",
                bw_mod_net_session_join(T_CODE),
                (int)BW_MOD_NET_E_NO_RELAY);

    printf("the connection fails\n");
    relay_reset();
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mutex_lock(g_relay.lock);
    g_relay.fail_connect = 1;
    bw_mutex_unlock(g_relay.lock);
    BW_CHECK_EQ("io: the host call says so",
                bw_mod_net_session_host(code, sizeof code),
                (int)BW_MOD_NET_E_IO);
    BW_CHECK_EQ("io: the join says so",
                bw_mod_net_session_join(T_CODE), (int)BW_MOD_NET_E_IO);
    BW_CHECK_EQ("io: there is no session",
                bw_mod_net_session_active(), 0);

    printf("a bad argument\n");
    BW_CHECK_EQ("arg: no room for the code",
                bw_mod_net_session_host(NULL, 16), (int)BW_MOD_NET_E_ARG);
    BW_CHECK_EQ("arg: no room at all",
                bw_mod_net_session_host(code, 0), (int)BW_MOD_NET_E_ARG);
    BW_CHECK_EQ("arg: no code",
                bw_mod_net_session_join(""), (int)BW_MOD_NET_E_ARG);
    BW_CHECK_EQ("arg: no code at all",
                bw_mod_net_session_join(NULL), (int)BW_MOD_NET_E_ARG);

    printf("no session\n");
    BW_CHECK_EQ("no session: the send says so",
                bw_mod_net_session_send(0, "x", 1),
                (int)BW_MOD_NET_E_NO_SESSION);
    BW_CHECK_EQ("no session: nothing was sent", g_relay.to_relay_len, 0);
}

/* ------------------------------------------------------------------ */
/* The frames the client writes                                       */
/* ------------------------------------------------------------------ */

static void test_protocol(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];
    char head[64];
    char version[64];
    char token[64];
    char name[64];
    char join_code[64];
    char host_token[64];
    RelayFrame frame;
    int got;

    printf("the host request\n");
    bw_mod_net_reset();
    relay_reset();
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
               bw_mod_net_session_host(code, sizeof code) ==
               (int)BW_MOD_NET_OK);
    BW_CHECK_EQ("the code is the relay's", strcmp(code, T_CODE), 0);
    BW_CHECK_EQ("the session is running", bw_mod_net_session_active(), 1);
    BW_CHECK_EQ("the port reached the relay it was told to reach",
                strcmp(g_relay.host, T_RELAY_HOST), 0);
    BW_CHECK_EQ("on the port it was told to reach",
                (long)g_relay.port, (long)T_RELAY_PORT);
    relay_peek(head, sizeof head);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is the host request", frame.type, 'H');
    snprintf(name, sizeof name, "H %u\n", (unsigned int)frame.size);
    BW_CHECK_EQ("the frame's shape is the protocol's",
                strncmp(head, name, strlen(name)), 0);
    got = sscanf(frame.payload, "%63s %63s %63s", version, token, name);
    BW_CHECK_EQ("the request has three fields", got, 3);
    BW_CHECK_EQ("the first is the protocol's version",
                strcmp(version, T_PROTO), 0);
    BW_CHECK_EQ("the second is the token the relay remembers us by",
                is_token(token), 1);
    BW_CHECK_EQ("the third is the player's name", strcmp(name, T_NAME), 0);
    snprintf(host_token, sizeof host_token, "%s", token);

    printf("the join request\n");
    bw_mod_net_session_leave();
    relay_reset();
    bw_mod_net_set_name(T_NAME);
    relay_say('W', "7 " T_NAME);
    BW_STEP("the join succeeds",
               bw_mod_net_session_join(T_JOIN_CODE) == (int)BW_MOD_NET_OK);
    BW_CHECK_EQ("the port reached the relay it was told to reach",
                strcmp(g_relay.host, T_RELAY_HOST), 0);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is the join request", frame.type, 'J');
    got = sscanf(frame.payload, "%63s %63s %63s %63s", version, token,
                 join_code, name);
    BW_CHECK_EQ("the request has four fields", got, 4);
    BW_CHECK_EQ("the first is the protocol's version",
                strcmp(version, T_PROTO), 0);
    BW_CHECK_EQ("the second is the token", is_token(token), 1);
    BW_CHECK_EQ("another code has a token of its own",
                strcmp(token, host_token) != 0, 1);
    BW_CHECK_EQ("the third is the code the port was given",
                strcmp(join_code, T_JOIN_CODE), 0);
    BW_CHECK_EQ("the fourth is the player's name", strcmp(name, T_NAME), 0);

    printf("a join back to the hosted code\n");
    bw_mod_net_session_leave();
    relay_reset();
    relay_say('W', "1 " T_NAME);
    BW_STEP("the join succeeds",
            bw_mod_net_session_join(" abc-123 ") == (int)BW_MOD_NET_OK);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    got = sscanf(frame.payload, "%63s %63s %63s %63s", version, token,
                 join_code, name);
    BW_CHECK_EQ("the request has four fields", got, 4);
    BW_CHECK_EQ("the code is cleaned: upper case, no spaces or dashes",
                strcmp(join_code, T_CODE), 0);
    BW_CHECK_EQ("the token is the one kept for that code",
                strcmp(token, host_token), 0);

    printf("the message\n");
    relay_reset();
    bw_mod_savedata_set_namespace(T_NS_A);
    BW_STEP("the send is taken",
               bw_mod_net_session_send(0, "hello", 5) == (int)BW_MOD_NET_OK);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is a message", frame.type, 'D');
    BW_CHECK_EQ("the message names the peer, the mod and the bytes",
                strcmp(frame.payload, "0\n" T_NS_A "\nhello"), 0);

    printf("the leave\n");
    relay_reset();
    bw_mod_net_session_leave();
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is the leave", frame.type, 'B');
    BW_CHECK_EQ("it is empty", frame.size, 0);
    BW_CHECK_EQ("the session is over", bw_mod_net_session_active(), 0);
    BW_CHECK_EQ("the connection is closed", wait_closed(), 1);
}

/* ------------------------------------------------------------------ */
/* The relay's answers and the session                                */
/* ------------------------------------------------------------------ */

static void test_answers(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];
    char peer_name[BW_MOD_NET_NAME_MAX + 1];
    int rc;

    printf("the session and the two mods\n");
    bw_mod_net_reset();
    relay_reset();
    log_reset(&g_alpha_session);
    log_reset(&g_beta_session);
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    /* the two mods bind their own handler, each under its own name */
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_session(on_alpha_session);
    bw_mod_savedata_set_namespace(T_NS_B);
    bw_mod_net_on_session(on_beta_session);

    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
               bw_mod_net_session_host(code, sizeof code) ==
               (int)BW_MOD_NET_OK);
    BW_CHECK_EQ("the code is the relay's", strcmp(code, T_CODE), 0);

    printf("the peer's own number\n");
    relay_say('W', "1 " T_NAME);
    BW_CHECK_EQ("the port waits for the number", wait_self_peer(1), 1);
    BW_CHECK_EQ("the number is the relay's own", bw_mod_net_self_peer(), 1);
    BW_CHECK_EQ("the peer's name is the relay's",
                bw_mod_net_peer_name(1, peer_name, sizeof peer_name), 0);
    BW_CHECK_EQ("and it is the name the relay gave",
                strcmp(peer_name, T_NAME), 0);

    printf("a peer joins\n");
    relay_say('P', "2 guest");
    BW_CHECK_EQ("the port waits for the peer", wait_peers(1), 1);
    BW_CHECK_EQ("the peer is there", bw_mod_net_peer_count(), 1);
    BW_CHECK_EQ("the peer's name is the relay's",
                bw_mod_net_peer_name(2, peer_name, sizeof peer_name), 0);
    BW_CHECK_EQ("and it is the name the relay gave",
                strcmp(peer_name, "guest"), 0);
    BW_CHECK_EQ("the join does not reach the handler without the frame",
                g_alpha_session.joins, 0);
    bw_mod_net_frame();
    BW_CHECK_EQ("the frame runs the handler", g_alpha_session.joins, 1);
    BW_CHECK_EQ("the peer is the one that joined",
                g_alpha_session.join_peer, 2);
    BW_CHECK_EQ("the other mod is told too", g_beta_session.joins, 1);

    printf("a message reaches the mod with the same key\n");
    relay_say('D', "2\n" T_NS_A "\nhello");
    relay_say('P', "3 other");      /* the marker: the reader is here */
    BW_CHECK_EQ("the port waits for the peer", wait_peers(2), 1);
    BW_CHECK_EQ("the message does not reach the handler without the frame",
                g_alpha_session.messages, 0);
    bw_mod_net_frame();
    BW_CHECK_EQ("the frame runs the message's handler",
                g_alpha_session.messages, 1);
    BW_CHECK_EQ("the peer is the one that sent it",
                g_alpha_session.msg_peer, 2);
    BW_CHECK_EQ("the message is the mod's own bytes",
                strcmp(g_alpha_session.msg_text, "hello"), 0);
    BW_CHECK_EQ("the other mod is not told", g_beta_session.messages, 0);

    printf("a message for a mod that is not here\n");
    relay_say('D', "2\ngamma\nhello");
    relay_say('P', "4 fourth");     /* the marker: the reader is here */
    BW_CHECK_EQ("the port waits for the peer", wait_peers(3), 1);
    bw_mod_net_frame();
    BW_CHECK_EQ("no mod has that key", g_alpha_session.messages, 1);
    BW_CHECK_EQ("and neither has the other", g_beta_session.messages, 0);

    printf("the messages keep their order\n");
    relay_say('D', "2\n" T_NS_A "\nfirst");
    relay_say('D', "2\n" T_NS_A "\nsecond");
    relay_say('P', "5 fifth");      /* the marker: the reader is here */
    BW_CHECK_EQ("the port waits for the peer", wait_peers(4), 1);
    bw_mod_net_frame();
    BW_CHECK_EQ("both messages reached the handler",
                g_alpha_session.messages, 3);
    BW_CHECK_EQ("the first is the first",
                strcmp(g_alpha_session.order[1], "first"), 0);
    BW_CHECK_EQ("the second is the second",
                strcmp(g_alpha_session.order[2], "second"), 0);

    printf("a peer is dropped\n");
    relay_say('X', "3");
    BW_CHECK_EQ("the port waits for the peer to be gone", wait_peers(3), 1);
    bw_mod_net_frame();
    BW_CHECK_EQ("the drop reaches the handler", g_alpha_session.leaves, 1);
    BW_CHECK_EQ("the other mod is told too", g_beta_session.leaves, 1);
    BW_CHECK_EQ("the peer is the one that left",
                g_alpha_session.leave_peer, 3);

    printf("the relay refuses\n");
    err_begin();
    relay_say('E', "the session is over");
    BW_CHECK_EQ("the stream is closed", wait_closed(), 1);
    err_end();
    BW_CHECK_EQ("the relay's reason is reported",
                reported("the relay says: the session is over"), 1);

    printf("the relay refuses the session\n");
    bw_mod_net_reset();
    relay_reset();
    err_begin();
    relay_say('E', "the session is full");
    rc = bw_mod_net_session_host(code, sizeof code);
    err_end();
    BW_CHECK_EQ("the host call says the relay refused", rc,
                (int)BW_MOD_NET_E_RELAY);
    BW_CHECK_EQ("the reason is reported",
                reported("the relay refused the session: the session is full"),
                1);
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

static void test_frame(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];

    printf("the handler runs on the frame, never on a network thread\n");
    bw_mod_net_reset();
    relay_reset();
    log_reset(&g_alpha_session);
    log_reset(&g_beta_session);
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_session(on_alpha_session);

    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
               bw_mod_net_session_host(code, sizeof code) ==
               (int)BW_MOD_NET_OK);

    /* the relay says a message and then a peer joins: the peer is the
     * marker, so the port has read the message when the peer is there */
    relay_say('D', "2\n" T_NS_A "\nhello");
    relay_say('P', "2 guest");
    BW_CHECK_EQ("the port waits for the peer", wait_peers(1), 1);
    BW_CHECK_EQ("the message is waiting, not running",
                g_alpha_session.messages, 0);
    t_sleep_ms(50);
    BW_CHECK_EQ("without the frame hook nothing runs",
                g_alpha_session.messages, 0);
    bw_mod_net_frame();
    BW_CHECK_EQ("the frame hook runs the handler",
                g_alpha_session.messages, 1);
    BW_CHECK_EQ("and the message is the mod's own bytes",
                strcmp(g_alpha_session.msg_text, "hello"), 0);
}

/*
 * A frame whose bytes come in over two reads, with a read between them
 * that finds nothing: the internet splits a 4 KB frame into packets.  The
 * frame's type is read in the first part and the frame ends in the last
 * one, so the type must outlive the read in between.  (A relay over the
 * internet lost exactly such frames: their type was forgotten.)
 */
static void test_split_frame(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];
    const char *body = "2\n" T_NS_A "\nsplit";
    char rest[64];

    printf("a frame split over reads\n");
    bw_mod_net_reset();
    relay_reset();
    log_reset(&g_alpha_session);
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_session(on_alpha_session);

    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
            bw_mod_net_session_host(code, sizeof code) == (int)BW_MOD_NET_OK);

    /* the type byte alone, then a read that finds nothing, then the rest */
    relay_write_raw("D");
    t_sleep_ms(50);
    snprintf(rest, sizeof rest, " %u\n%s", (unsigned)strlen(body), body);
    relay_write_raw(rest);
    relay_say('P', "2 guest");          /* the marker: the reader is past it */
    BW_CHECK_EQ("the reader reads on", wait_peers(1), 1);
    bw_mod_net_frame();
    BW_CHECK_EQ("the split frame arrives", g_alpha_session.messages, 1);
    BW_CHECK_EQ("whole", strcmp(g_alpha_session.msg_text, "split"), 0);
}

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

static void test_limits(void)
{
    static u8 big[BW_MOD_NET_MESSAGE_MAX + 1];
    char code[BW_MOD_NET_CODE_MAX + 1];

    printf("a message over the limit\n");
    memset(big, 'x', sizeof big);
    bw_mod_net_reset();
    relay_reset();
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    bw_mod_savedata_set_namespace(T_NS_A);
    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
               bw_mod_net_session_host(code, sizeof code) ==
               (int)BW_MOD_NET_OK);
    relay_forget_written();

    BW_CHECK_EQ("the limit is 4 KB", (long)BW_MOD_NET_MESSAGE_MAX, 4096);
    BW_CHECK_EQ("too big: the send is refused",
                bw_mod_net_session_send(0, big, sizeof big),
                (int)BW_MOD_NET_E_TOO_BIG);
    BW_CHECK_EQ("too big: nothing was written", g_relay.to_relay_len, 0);
    BW_CHECK_EQ("the limit itself is taken",
                bw_mod_net_session_send(0, big, BW_MOD_NET_MESSAGE_MAX),
                (int)BW_MOD_NET_OK);

    printf("the transport refuses the write\n");
    bw_mutex_lock(g_relay.lock);
    g_relay.fail_send = 1;
    bw_mutex_unlock(g_relay.lock);
    BW_CHECK_EQ("io: the send is reported",
                bw_mod_net_session_send(0, "hello", 5),
                (int)BW_MOD_NET_E_IO);
    bw_mutex_lock(g_relay.lock);
    g_relay.fail_send = 0;
    bw_mutex_unlock(g_relay.lock);
}

/* ------------------------------------------------------------------ */
/* A frame the port did not expect                                     */
/* ------------------------------------------------------------------ */

static void test_bad_frame(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];

    printf("a frame the port did not expect\n");
    bw_mod_net_reset();
    relay_reset();
    log_reset(&g_alpha_session);
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_session(on_alpha_session);
    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
               bw_mod_net_session_host(code, sizeof code) ==
               (int)BW_MOD_NET_OK);
    /* an empty frame of a type the port does not know comes first: the
     * port reads past it and takes the next one */
    relay_say('Z', "");
    relay_say('P', "2 guest");
    BW_CHECK_EQ("the port waits for the peer", wait_peers(1), 1);
    BW_CHECK_EQ("an empty frame does not end the session",
                bw_mod_net_session_active(), 1);

    /* the length is not a number: the frame is not the protocol's */
    relay_write_raw("Z 1x\n");
    BW_CHECK_EQ("the stream is refused", wait_closed(), 1);
    BW_CHECK_EQ("the session is over", bw_mod_net_session_active(), 0);
    bw_mod_net_frame();
    BW_CHECK_EQ("the peers are dropped", g_alpha_session.leaves, 1);
    BW_CHECK_EQ("and it is the peer that was there",
                g_alpha_session.leave_peer, 2);
}

/* ------------------------------------------------------------------ */
/* The external control                                                */
/* ------------------------------------------------------------------ */

static void test_control(void)
{
    const BwModNetTransport *tcp = bw_mod_net_tcp_transport();
    char code[BW_MOD_NET_CODE_MAX + 1];
    char line[512];
    u32 got = 0;
    int port = 0;
    int i;

    printf("the control is off to begin with\n");
    bw_mod_net_reset();
    BW_CHECK_EQ("the control has no port", bw_mod_net_control_port(), 0);
    BW_CHECK_EQ("and no tools", bw_mod_net_control_clients(), 0);

    printf("the control listens\n");
    for (i = 0; i < T_CONTROL_PORTS; i++) {
        port = T_CONTROL_PORT + i;
        if (bw_mod_net_control_set_port(port) != 0) {
            continue;
        }
        /* a port another program also holds (GitHub's Windows runner has
         * one in this range) can take the connection: the port is ours
         * only when the control sees the tool arrive */
        if (tcp->connect(tcp->user, "127.0.0.1", (u16)port) == 0
            && wait_clients_for(1, 2000000000ull)) {
            break;
        }
    }
    /* the probe leaves: the steps below start with no tool, as before */
    tcp->close(tcp->user);
    (void)wait_clients_for(0, 2000000000ull);
    BW_STEP("the port is set", i < T_CONTROL_PORTS);
    BW_CHECK_EQ("the port is the one that was set",
                bw_mod_net_control_port(), port);

    printf("the control is on 127.0.0.1 only\n");
    /* the port's own client, pointed at the other loopback address: a
     * listener on the wildcard would answer there, and one on 127.0.0.1
     * does not */
    bw_mod_net_set_transport(tcp);
    bw_mod_net_set_relay("127.0.0.2", (u16)port);
    BW_CHECK_EQ("the other loopback address is not served",
                bw_mod_net_session_host(code, sizeof code),
                (int)BW_MOD_NET_E_IO);
    bw_mod_net_set_transport(NULL);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);

    printf("a tool connects\n");
    /* the mod that uses the service binds its handler */
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_command(on_alpha_command);
    BW_STEP("the tool connects",
               tcp->connect(tcp->user, "127.0.0.1", (u16)port) == 0);
    BW_CHECK_EQ("the port sees the tool", wait_clients(1), 1);

    printf("the command reaches the handler\n");
    BW_STEP("the tool sends its command",
               tcp->send(tcp->user, "hello there\n", 12) == 0);
    t_sleep_ms(250);
    BW_CHECK_EQ("without the frame hook the command does not run",
                g_alpha_command.calls, 0);
    for (i = 0; i < 2000 && g_alpha_command.calls == 0; i++) {
        bw_mod_net_frame();
        if (g_alpha_command.calls == 0) {
            t_sleep_ms(1);
        }
    }
    BW_CHECK_EQ("the frame hook runs the command's handler",
                g_alpha_command.calls, 1);
    BW_CHECK_EQ("the line is the tool's, without the newline",
                strcmp(g_alpha_command.text, "hello there"), 0);

    printf("the port publishes to the tools\n");
    bw_mod_net_publish("run", "{\"state\":\"title\"}");
    got = 0;
    for (i = 0; i < 2000; i++) {
        if (tcp->recv(tcp->user, line, sizeof line - 1, &got) == 1 &&
            got > 0) {
            break;
        }
        t_sleep_ms(1);
    }
    line[got] = '\0';
    BW_CHECK_EQ("the tool got the message",
                strcmp(line, "run {\"state\":\"title\"}\n"), 0);

    printf("the port's reset stops the control\n");
    bw_mod_net_reset();
    BW_CHECK_EQ("the tools are gone", bw_mod_net_control_clients(), 0);
    BW_CHECK_EQ("the listener is stopped",
                tcp->connect(tcp->user, "127.0.0.1", (u16)port) != 0, 1);
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The port's own messages                                            */
/* ------------------------------------------------------------------ */

static HandlerLog g_port_log;
static int        g_port_ended;
static char       g_port_reason[64];

static void on_port(u32 peer, int kind, const void *buf, u32 size)
{
    if (kind == BW_MOD_NET_PORT_ENDED) {
        g_port_ended++;
        snprintf(g_port_reason, sizeof g_port_reason, "%.*s", (int)size,
                 (const char *)buf);
        return;
    }
    note_session(&g_port_log, peer, kind, buf, size);
}

/* Run the frame until `cond` holds or the wait is over. */
static int frame_until(const int *value, int want)
{
    unsigned long long deadline = bw_clock_ns() + T_WAIT_NS;

    while (bw_clock_ns() < deadline) {
        bw_mod_net_frame();
        if (*value >= want) {
            return 1;
        }
        t_sleep_ms(1);
    }
    return 0;
}

static void test_relay_address(void)
{
    printf("the relay's address\n");
    bw_mod_net_set_relay("relay.example.org", 4141);
    BW_CHECK_EQ("a domain is kept as it is",
                strcmp(bw_mod_net_relay(), "relay.example.org"), 0);
    BW_CHECK_EQ("with the port given", (long)bw_mod_net_relay_port(), 4141);
    bw_mod_net_set_relay("relay.example.org:5000", 4141);
    BW_CHECK_EQ("a port after the name: the name",
                strcmp(bw_mod_net_relay(), "relay.example.org"), 0);
    BW_CHECK_EQ("and that port wins", (long)bw_mod_net_relay_port(), 5000);
    bw_mod_net_set_relay("::1", 4141);
    BW_CHECK_EQ("an IPv6 address is left alone",
                strcmp(bw_mod_net_relay(), "::1"), 0);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
}

static void test_port(void)
{
    char code[BW_MOD_NET_CODE_MAX + 1];
    RelayFrame frame;

    printf("the port's own messages\n");
    bw_mod_net_reset();
    relay_reset();
    log_reset(&g_alpha_session);
    log_reset(&g_port_log);
    g_port_ended = 0;
    bw_mod_net_set_transport(&g_fake_transport);
    bw_mod_net_set_relay(T_RELAY_HOST, T_RELAY_PORT);
    bw_mod_net_set_name(T_NAME);
    bw_mod_net_set_port_handler(on_port);
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_net_on_session(on_alpha_session);
    relay_say('C', T_CODE);
    BW_STEP("the host call succeeds",
            bw_mod_net_session_host(code, sizeof code) == (int)BW_MOD_NET_OK);
    BW_CHECK_EQ("the port knows it hosts", bw_mod_net_session_hosting(), 1);
    relay_reset();

    BW_STEP("the port's send is taken",
            bw_mod_net_port_send(2, "lobby", 5) == (int)BW_MOD_NET_OK);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is a message", frame.type, 'D');
    BW_CHECK_EQ("under the port's key",
                strcmp(frame.payload, "2\n" BW_MOD_NET_PORT_KEY "\nlobby"), 0);

    BW_STEP("the host removes a player",
            bw_mod_net_session_remove(2) == (int)BW_MOD_NET_OK);
    BW_STEP("the client wrote a frame", relay_take(&frame) == 1);
    BW_CHECK_EQ("the frame is the removal", frame.type, 'Q');
    BW_CHECK_EQ("of that peer", strcmp(frame.payload, "2"), 0);

    relay_say('P', "2 guest");
    relay_say('D', "2\n" BW_MOD_NET_PORT_KEY "\nhello port");
    BW_CHECK_EQ("the port hears the join and its message",
                frame_until(&g_port_log.messages, 1), 1);
    BW_CHECK_EQ("the join too", g_port_log.joins, 1);
    BW_CHECK_EQ("the message is the port's",
                strcmp(g_port_log.msg_text, "hello port"), 0);
    BW_CHECK_EQ("the mod heard the join", g_alpha_session.joins, 1);
    BW_CHECK_EQ("but not the port's message", g_alpha_session.messages, 0);

    relay_say('E', "removed");
    BW_CHECK_EQ("the port hears the end of the session",
                frame_until(&g_port_ended, 1), 1);
    BW_CHECK_EQ("with the relay's reason",
                strcmp(g_port_reason, "removed"), 0);
    BW_CHECK_EQ("and no session runs any more",
                bw_mod_net_session_active(), 0);
    bw_mod_net_set_port_handler(NULL);
    bw_mod_net_session_leave();
}

int main(void)
{
    g_relay.lock = bw_mutex_create();
    BW_REQUIRE("the test's own lock is made", g_relay.lock != NULL);
    g_fake_transport.user = (void *)&g_relay;
    g_fake_transport.connect = fake_connect;
    g_fake_transport.send = fake_send;
    g_fake_transport.recv = fake_recv;
    g_fake_transport.close = fake_close;

    test_seam();
    test_protocol();
    test_answers();
    test_frame();
    test_split_frame();
    test_limits();
    test_bad_frame();
    test_port();
    test_relay_address();
    test_control();

    bw_mod_net_reset();
    err_cleanup();
    bw_mutex_destroy(g_relay.lock);
    BW_REPORT("test_mod_net");
}
