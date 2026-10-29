/*
 * sdk/mod_net.c -- the peer sessions and the external control: the port's
 * side of the two services (docs/mod-services.md, "Peer sessions" and
 * "External control"; docs/mod-abi.md, section 1; docs/modding.md, section 3).
 *
 * What this file owns
 * -------------------
 *
 * Two of the port's services, and nothing else:
 *
 *   - the peer sessions: two or more eidos instances talking to each other
 *     through the project's relay, exchanging the mods' own messages;
 *   - the external control: a local endpoint a stream overlay or a chat bot
 *     connects to, sending text commands in and taking JSON messages out.
 *
 * The rules the services keep
 * ---------------------------
 *
 * (docs/mod-services.md; the words are the document's)
 *
 *   - The relay is a server the project runs.  The port is the client, and
 *     the protocol is the port's to speak.  The port has no relay yet, so
 *     the transport under the client is an interface the port fills (a
 *     connect, a send, a receive, a close); the default transport reports
 *     "no relay".
 *   - A message is up to 4 KB of the mod's own bytes, delivered in order,
 *     at most once, to every player in the session (or one, by peer
 *     number).
 *   - Each mod receives only the messages the same mod sent on the other
 *     side, keyed by the mod's name.
 *   - The handler runs on the game's frame, never on a network thread.
 *   - A player has a peer number for the session's lifetime and a display
 *     name.  A dropped player is reported to the handler; a rejoin with the
 *     same code gets the same number back.
 *   - The external control listens on 127.0.0.1 only, on a port chosen in
 *     the port's settings, and only while a mod that uses the service is
 *     enabled.  A command is one line of text; the handler runs on the
 *     game's frame.  publish sends a JSON message to every connected tool.
 *
 * The threads
 * -----------
 *
 * Two, both of the port's own (sdk/plat_thread.h): the session's reader and
 * the control's listener.  Neither ever calls a mod's handler: they queue
 * what they read, and the port's frame hook drains the queues and calls the
 * handlers (bw_mod_net_frame).  That is the whole of "the handler runs on
 * the game's frame, never on a network thread": the only place a handler is
 * called from is the frame.
 *
 * Who is calling
 * --------------
 *
 * The services table is filled per mod (sdk/mod_loader.c gives every mod a
 * table of its own) and the loader binds the same functions into every one
 * of them, so the state cannot be one set of variables: the port says which
 * mod is calling the way it says so everywhere else, with the loader's
 * namespace (bw_mod_savedata_namespace), and this file keys its handlers on
 * it.  The namespace is the mod's own name (its manifest's `state` key, or
 * its name when the manifest states none), and it is the key a message
 * carries: a mod on one side receives only what the mod with the same key
 * sent on the other side.
 *
 * The relay's protocol
 * --------------------
 *
 * The client speaks it; the relay (the project's own server) has to answer
 * it.  It is a stream of frames, each one a type letter, the payload's
 * length in decimal, a newline, and the payload:
 *
 *     <type> <length>\n<payload>
 *
 * The client sends:
 *
 *     H  <version> <token> <name>          host a session; the relay
 *                                           answers with a code
 *     J  <version> <token> <code> <name>   join a session
 *     D  <peer>\n<key>\n<bytes>            a mod's message, to one peer or
 *                                           to every peer (peer 0)
 *     B  (empty)                           leave the session
 *     K  (empty)                           keep-alive, after 20 s of silence
 *
 * and the relay sends:
 *
 *     C  <code>                            the session's short code
 *     W  <peer> <name>                     you are in; this is your number
 *     P  <peer> <name>                     a peer joined
 *     X  <peer>                            a peer left (or was dropped)
 *     D  <peer>\n<key>\n<bytes>            a mod's message
 *     E  <reason>                          an error; the session is over
 *
 * The relay's rules, which the client relies on:
 *
 *   - it gives the host a short code and tells the host the code (C);
 *   - it tells every client its own peer number (W) and every client's
 *     join (P) and drop (X);
 *   - it forwards a message to one peer or to all the others, in the order
 *     it was sent, once each.  The peer number in the message it forwards
 *     is the sender's own, not the target the sender named;
 *   - it keeps a peer's number for the session's lifetime: the same token
 *     joining the same code again gets the same number back (the token is
 *     the client's, and it is what the relay remembers);
 *   - it refuses a join with an unknown code with E.
 *
 * The port has to know three things to reach the relay: the host (a name
 * or a dotted address), the port, and the session's code (which the relay
 * hands out and the players type).
 *
 * The sockets
 * ----------
 *
 * The port has no socket layer of its own yet, so the two endpoints this
 * file owns use the host's sockets directly.  On Windows they come from
 * ws2_32.dll, which is loaded at run time: the port's own build does not
 * name that library, so nothing here depends on a link line somebody else
 * has to add.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_net.h"
#include "plat_thread.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The mod that is calling: the loader sets the namespace around every call
 * it makes into a mod (sdk/mod_loader.c), so a service call always knows
 * whose it is.  The saved-data layer has no header of its own yet, so the
 * prototype is here.
 */
extern const char *bw_mod_savedata_namespace(void);
extern void        bw_mod_savedata_set_namespace(const char *ns);

/*
 * The namespace a handler runs under.  A mod's own handler must see its own
 * namespace in force, the way the event path's and the frame tick's handlers
 * do, so that a state call it makes reaches its own file.  The namespace
 * that was in force is put back afterwards.
 */
static const char *net_enter_mod(const char *ns)
{
    const char *saved;

    if (ns == NULL || *ns == '\0') {
        return NULL;
    }
    saved = bw_mod_savedata_namespace();
    bw_mod_savedata_set_namespace(ns);
    return saved;
}

static void net_leave_mod(const char *saved)
{
    if (saved != NULL) {
        bw_mod_savedata_set_namespace(saved);
    }
}

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* The largest frame the client reads: a data message is 4 KB of the mod's
 * own bytes, plus the peer number and the mod's key. */
#define BW_MOD_NET_RX_MAX 8192u

/* The largest frame the client writes: the same, plus the header. */
#define BW_MOD_NET_TX_MAX (BW_MOD_NET_MESSAGE_MAX + 256u)

/* How long the client waits for the relay to answer a host or a join, and
 * for a connection to be made. */
#define BW_MOD_NET_TIMEOUT_MS 3000u

/* After how long without a frame of its own the client sends a keep-alive
 * (the relay drops a client that is silent for 60 seconds). */
#define BW_MOD_NET_KEEPALIVE_MS 20000u

/* How many session codes the client keeps a token for. */
#define BW_MOD_NET_TOKENS_MAX 32u

/* How long the reader waits between two polls of the transport. */
#define BW_MOD_NET_POLL_MS 2u

/* How many tools may be connected to the external control at once. */
#define BW_MOD_NET_CLIENTS_MAX 8u

/* The longest command line the control reads. */
#define BW_MOD_NET_COMMAND_MAX 1024u

/* The queue the network threads write into and the frame drains: 64 KB of
 * records, which is sixteen 4 KB messages. */
#define BW_MOD_NET_QUEUE_BYTES (64u * 1024u)

/* ------------------------------------------------------------------ */
/* The protocol                                                        */
/* ------------------------------------------------------------------ */

/* The protocol version the client states in its host and join requests. */
#define BW_MOD_NET_PROTO "eidos-net/1"

/* The frame types. */
#define BW_MOD_NET_MSG_HOST    'H'   /* client -> relay: host a session    */
#define BW_MOD_NET_MSG_JOIN    'J'   /* client -> relay: join a session    */
#define BW_MOD_NET_MSG_DATA    'D'   /* both ways: a mod's message         */
#define BW_MOD_NET_MSG_BYE     'B'   /* client -> relay: leave             */
#define BW_MOD_NET_MSG_KEEPALIVE 'K' /* client -> relay: still here        */
#define BW_MOD_NET_MSG_REMOVE 'Q'    /* client -> relay: the host removes a player */
#define BW_MOD_NET_MSG_CODE    'C'   /* relay -> client: the session's code */
#define BW_MOD_NET_MSG_WELCOME 'W'   /* relay -> client: you are in        */
#define BW_MOD_NET_MSG_PEER    'P'   /* relay -> client: a peer joined     */
#define BW_MOD_NET_MSG_DROP    'X'   /* relay -> client: a peer left       */
#define BW_MOD_NET_MSG_ERROR   'E'   /* relay -> client: an error          */

/* The record kinds in the queue.  The first three are the session kinds of
 * sdk/mod_state.h; the fourth is the control's own. */
#define BW_MOD_NET_EV_COMMAND 3u
/* The session is over (the relay's error, or the connection lost): told
 * to the port's own handler only, with the reason as the bytes. */
#define BW_MOD_NET_EV_ENDED 100u

/* ------------------------------------------------------------------ */
/* The sockets                                                         */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

typedef SOCKET BwSock;
typedef int     BwSockLen;
typedef int     BwOptLen;

#define BW_SOCK_INVALID INVALID_SOCKET
#define BW_SOCKET_ERROR  (-1)

/* The winsock entry points, loaded at run time. */
typedef int (WSAAPI *fn_wsastartup)(WORD, WSADATA *);
typedef int (WSAAPI *fn_wsacleanup)(void);
typedef SOCKET (WSAAPI *fn_wssocket)(int, int, int);
typedef int (WSAAPI *fn_wsconnect)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *fn_wsbind)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *fn_wslisten)(SOCKET, int);
typedef SOCKET (WSAAPI *fn_wsaccept)(SOCKET, struct sockaddr *, int *);
typedef int (WSAAPI *fn_wssend)(SOCKET, const char *, int, int);
typedef int (WSAAPI *fn_wsrecv)(SOCKET, char *, int, int);
typedef int (WSAAPI *fn_wsclosesocket)(SOCKET);
typedef int (WSAAPI *fn_wssetsockopt)(SOCKET, int, int, const char *, int);
typedef int (WSAAPI *fn_wsgetsockopt)(SOCKET, int, int, char *, int *);
typedef int (WSAAPI *fn_wsselect)(int, fd_set *, fd_set *, fd_set *,
                                  const struct timeval *);
typedef int (WSAAPI *fn_wsnioctlsocket)(SOCKET, long, unsigned long *);
typedef struct hostent *(WSAAPI *fn_wsgethostbyname)(const char *);
typedef unsigned long (WSAAPI *fn_wsinetaddr)(const char *);
typedef unsigned short (WSAAPI *fn_wshtons)(unsigned short);
typedef unsigned long (WSAAPI *fn_wshtonl)(unsigned long);
typedef int (WSAAPI *fn_wslasterror)(void);

static struct {
    int                loaded;
    HMODULE            lib;
    fn_wsastartup      startup;
    fn_wsacleanup      cleanup;
    fn_wssocket        socket;
    fn_wsconnect       connect;
    fn_wsbind          bind;
    fn_wslisten        listen;
    fn_wsaccept        accept;
    fn_wssend          send;
    fn_wsrecv          recv;
    fn_wsclosesocket   closesocket;
    fn_wssetsockopt    setsockopt;
    fn_wsgetsockopt    getsockopt;
    fn_wsselect        select;
    fn_wsnioctlsocket  ioctlsocket;
    fn_wsgethostbyname gethostbyname;
    fn_wsinetaddr      inet_addr;
    fn_wshtons         htons;
    fn_wshtonl         htonl;
    fn_wslasterror     lasterror;
} g_ws;

/* The symbol in ws2_32.dll, cast to its own type. */
#define WS_SYM(type, name) ((type)(void *)GetProcAddress(g_ws.lib, (name)))

#define SOCK_SOCKET         g_ws.socket
#define SOCK_CONNECT        g_ws.connect
#define SOCK_BIND           g_ws.bind
#define SOCK_LISTEN         g_ws.listen
#define SOCK_ACCEPT         g_ws.accept
#define SOCK_SEND           g_ws.send
#define SOCK_RECV           g_ws.recv
#define SOCK_CLOSE          g_ws.closesocket
#define SOCK_SETSOCKOPT     g_ws.setsockopt
#define SOCK_GETSOCKOPT     g_ws.getsockopt
#define SOCK_SELECT         g_ws.select
#define SOCK_GETHOSTBYNAME  g_ws.gethostbyname
#define SOCK_INET_ADDR      g_ws.inet_addr
#define SOCK_HTONS          g_ws.htons
#define SOCK_HTONL          g_ws.htonl
#define SOCK_LAST_ERROR     g_ws.lasterror
#define SOCK_OPT_VALUE      (const char *)
#define SOCK_OPT_BACK       (char *)

/* Would the last socket call have blocked? */
static int sock_would_block(void)
{
    int err = SOCK_LAST_ERROR();

    return (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT ||
            err == WSAEINPROGRESS || err == WSAEINTR) ? 1 : 0;
}

#elif defined(__wasm__)

/*
 * The wasm32 build: the host program's sockets (sdk/wasm_host.h, sock_*).
 * A socket is the host's number for it.  The host's sockets never block:
 * the game's threads are fibers of one host thread, so a wait here gives
 * the other fibers their turn (bw_thread_sleep_ns) and asks again, and a
 * blocking socket's send or receive is that wait plus the timeout
 * sock_set_timeouts asked for.  select is only ever asked about one socket
 * (sock_wait).
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>

#include "wasm_host.h"

typedef int       BwSock;
typedef socklen_t BwSockLen;
typedef socklen_t BwOptLen;

struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; };

#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef SO_ERROR
#define SO_ERROR 4
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 21
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 20
#endif

#define BW_SOCK_INVALID (-1)
#define BW_SOCKET_ERROR (-1)
#define WSOCK_MAX       64

/* What the module keeps of each socket: its mode and timeout, and the
 * port a bind named (the host binds when listen comes). */
static struct {
    int nonblock;
    int timeout_ms;
    u16 port;
} g_wsock[WSOCK_MAX];

static int wsock_ok(int s)
{
    return s >= 0 && s < WSOCK_MAX;
}

/* One turn for the other fibers, then the time that passed. */
static void wsock_yield(void)
{
    bw_thread_sleep_ns(1000000ull);         /* 1 ms */
}

static int wsock_socket(void)
{
    int s = bw_host_sock_open();

    if (s >= 0 && !wsock_ok(s)) {
        bw_host_sock_close(s);          /* past the module's table */
        return -1;
    }
    if (wsock_ok(s)) {
        g_wsock[s].nonblock = 0;
        g_wsock[s].timeout_ms = 0;
        g_wsock[s].port = 0;
    }
    return wsock_ok(s) ? s : -1;
}

static int wsock_connect(int s, const struct sockaddr *a)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    int rc = bw_host_sock_connect(s, in->sin_addr.s_addr, ntohs(in->sin_port));

    if (rc == 0) {
        return 0;
    }
    errno = rc > 0 ? EINPROGRESS : ECONNREFUSED;
    return -1;
}

/* Wait until `s` is ready, the timeout runs out (0: none, -1: no limit),
 * or an error.  1, 0, -1. */
static int wsock_wait(int s, int for_write, int timeout_ms)
{
    unsigned long long end = bw_clock_ns() + (unsigned long long)timeout_ms * 1000000ull;

    for (;;) {
        int rc = bw_host_sock_poll(s, for_write);

        if (rc != 0) {
            return rc;
        }
        if (timeout_ms == 0 || (timeout_ms > 0 && bw_clock_ns() >= end)) {
            return 0;
        }
        wsock_yield();
    }
}

static int wsock_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    fd_set *set = w != NULL ? w : r;
    int s, ms = tv != NULL ? (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000) : -1, rc;

    (void)e;
    for (s = 0; s < nfds; s++) {
        if (set != NULL && FD_ISSET(s, set)) {
            break;
        }
    }
    if (s == nfds) {
        errno = EINVAL;
        return -1;
    }
    rc = wsock_wait(s, w != NULL, ms);
    if (rc <= 0) {
        FD_CLR(s, set);
    }
    if (rc < 0) {
        errno = EIO;
    }
    return rc;
}

static int wsock_send(int s, const void *b, int n)
{
    for (;;) {
        int rc = bw_host_sock_send(s, b, (unsigned)n);

        if (rc >= 0) {
            return rc;
        }
        if (rc != -2) {
            errno = EPIPE;
            return -1;
        }
        if (!wsock_ok(s) || g_wsock[s].nonblock
            || wsock_wait(s, 1, g_wsock[s].timeout_ms > 0 ? g_wsock[s].timeout_ms : -1) != 1) {
            errno = EAGAIN;
            return -1;
        }
    }
}

static int wsock_recv(int s, void *b, int n)
{
    for (;;) {
        int rc = bw_host_sock_recv(s, b, (unsigned)n);

        if (rc >= 0) {
            return rc;
        }
        if (rc != -2) {
            errno = ECONNRESET;
            return -1;
        }
        if (!wsock_ok(s) || g_wsock[s].nonblock
            || wsock_wait(s, 0, g_wsock[s].timeout_ms > 0 ? g_wsock[s].timeout_ms : -1) != 1) {
            errno = EAGAIN;
            return -1;
        }
    }
}

static int wsock_setsockopt(int s, int opt, const void *v)
{
    if (!wsock_ok(s)) {
        return -1;
    }
    if (opt == SO_SNDTIMEO || opt == SO_RCVTIMEO) {
        const struct timeval *tv = (const struct timeval *)v;

        g_wsock[s].timeout_ms = (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
    }
    return 0;                           /* SO_REUSEADDR: the host sets it */
}

static int wsock_getsockopt(int s, int opt, void *v)
{
    if (opt != SO_ERROR) {
        return -1;
    }
    *(int *)v = bw_host_sock_error(s);
    return 0;
}

static int wsock_bind(int s, const struct sockaddr *a)
{
    if (!wsock_ok(s)) {
        return -1;
    }
    g_wsock[s].port = ntohs(((const struct sockaddr_in *)a)->sin_port);
    return 0;
}

static int wsock_listen(int s)
{
    return wsock_ok(s) ? bw_host_sock_listen_loopback(s, g_wsock[s].port) : -1;
}

static int wsock_accept(int s)
{
    int c = bw_host_sock_accept(s);

    if (c >= 0 && !wsock_ok(c)) {
        bw_host_sock_close(c);          /* past the module's table */
    }
    if (!wsock_ok(c)) {
        errno = EAGAIN;
        return -1;
    }
    g_wsock[c].nonblock = 0;
    g_wsock[c].timeout_ms = 0;
    g_wsock[c].port = 0;
    return c;
}

static struct hostent *wsock_gethostbyname(const char *name)
{
    static u32 addr;
    static char *list[2];
    static struct hostent he;

    if (bw_host_sock_resolve(name, &addr) != 0) {
        return NULL;
    }
    list[0] = (char *)&addr;
    list[1] = NULL;
    he.h_name = (char *)name;
    he.h_addrtype = AF_INET;
    he.h_length = 4;
    he.h_addr_list = list;
    return &he;
}

/* A dotted address, network order; 0xFFFFFFFF when `h` is not one. */
static in_addr_t wsock_inet_addr(const char *h)
{
    unsigned v[4], k;
    char tail;

    if (sscanf(h, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) {
        return (in_addr_t)0xFFFFFFFFu;
    }
    for (k = 0; k < 4; k++) {
        if (v[k] > 255) {
            return (in_addr_t)0xFFFFFFFFu;
        }
    }
    return htonl((v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3]);
}

#define SOCK_SOCKET(a, b, c)            ((void)(a), (void)(b), (void)(c), wsock_socket())
#define SOCK_CONNECT(s, a, l)           ((void)(l), wsock_connect((s), (a)))
#define SOCK_BIND(s, a, l)              ((void)(l), wsock_bind((s), (a)))
#define SOCK_LISTEN(s, n)               ((void)(n), wsock_listen(s))
#define SOCK_ACCEPT(s, a, l)            ((void)(a), (void)(l), wsock_accept(s))
#define SOCK_SEND(s, b, n, f)           ((void)(f), wsock_send((s), (b), (n)))
#define SOCK_RECV(s, b, n, f)           ((void)(f), wsock_recv((s), (b), (n)))
#define SOCK_CLOSE(s)                   (bw_host_sock_close(s), 0)
#define SOCK_SETSOCKOPT(s, l, o, v, n)  ((void)(l), (void)(n), wsock_setsockopt((s), (o), (v)))
#define SOCK_GETSOCKOPT(s, l, o, v, n)  ((void)(l), (void)(n), wsock_getsockopt((s), (o), (v)))
#define SOCK_SELECT(n, r, w, e, t)      wsock_select((n), (r), (w), (e), (t))
#define SOCK_GETHOSTBYNAME(h)           wsock_gethostbyname(h)
#define SOCK_INET_ADDR(h)               wsock_inet_addr(h)
#define SOCK_HTONS                      htons
#define SOCK_HTONL                      htonl
#define SOCK_LAST_ERROR                 (errno)
#define SOCK_OPT_VALUE                  (const void *)
#define SOCK_OPT_BACK                   (void *)

static int sock_would_block(void)
{
    return errno == EAGAIN || errno == EINPROGRESS;
}

#else /* POSIX */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

typedef int       BwSock;
typedef socklen_t BwSockLen;
typedef socklen_t BwOptLen;

#define BW_SOCK_INVALID (-1)
#define BW_SOCKET_ERROR (-1)

#define SOCK_SOCKET         socket
#define SOCK_CONNECT        connect
#define SOCK_BIND           bind
#define SOCK_LISTEN         listen
#define SOCK_ACCEPT         accept
#define SOCK_SEND           send
#define SOCK_RECV           recv
#define SOCK_CLOSE          close
#define SOCK_SETSOCKOPT     setsockopt
#define SOCK_GETSOCKOPT     getsockopt
#define SOCK_SELECT         select
#define SOCK_GETHOSTBYNAME  gethostbyname
#define SOCK_INET_ADDR      inet_addr
#define SOCK_HTONS          htons
#define SOCK_HTONL          htonl
#define SOCK_LAST_ERROR     (errno)
#define SOCK_OPT_VALUE      (const void *)
#define SOCK_OPT_BACK       (void *)

/* Would the last socket call have blocked? */
static int sock_would_block(void)
{
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 1 : 0;
}

#endif /* _WIN32 */

/* The last socket error, as text, for the port's own reports. */
static const char *sock_error(void)
{
#ifdef _WIN32
    static char text[64];

    snprintf(text, sizeof(text), "winsock error %d", SOCK_LAST_ERROR());
    return text;
#else
    static char text[64];

    snprintf(text, sizeof(text), "socket error %d", errno);
    return text;
#endif
}

/* The socket layer is up.  0 on success. */
static int sock_startup(void)
{
#ifdef _WIN32
    WSADATA data;

    if (g_ws.loaded) {
        return 0;
    }
    g_ws.lib = LoadLibraryA("ws2_32.dll");
    if (g_ws.lib == NULL) {
        return -1;
    }
    /* The cast goes through a void *, which is the one conversion every
     * host allows for a function pointer. */
    g_ws.startup = WS_SYM(fn_wsastartup, "WSAStartup");
    g_ws.cleanup = WS_SYM(fn_wsacleanup, "WSACleanup");
    g_ws.socket = WS_SYM(fn_wssocket, "socket");
    g_ws.connect = WS_SYM(fn_wsconnect, "connect");
    g_ws.bind = WS_SYM(fn_wsbind, "bind");
    g_ws.listen = WS_SYM(fn_wslisten, "listen");
    g_ws.accept = WS_SYM(fn_wsaccept, "accept");
    g_ws.send = WS_SYM(fn_wssend, "send");
    g_ws.recv = WS_SYM(fn_wsrecv, "recv");
    g_ws.closesocket = WS_SYM(fn_wsclosesocket, "closesocket");
    g_ws.setsockopt = WS_SYM(fn_wssetsockopt, "setsockopt");
    g_ws.getsockopt = WS_SYM(fn_wsgetsockopt, "getsockopt");
    g_ws.select = WS_SYM(fn_wsselect, "select");
    g_ws.ioctlsocket = WS_SYM(fn_wsnioctlsocket, "ioctlsocket");
    g_ws.gethostbyname = WS_SYM(fn_wsgethostbyname, "gethostbyname");
    g_ws.inet_addr = WS_SYM(fn_wsinetaddr, "inet_addr");
    g_ws.htons = WS_SYM(fn_wshtons, "htons");
    g_ws.htonl = WS_SYM(fn_wshtonl, "htonl");
    g_ws.lasterror = WS_SYM(fn_wslasterror, "WSAGetLastError");
    if (g_ws.startup == NULL || g_ws.cleanup == NULL ||
        g_ws.socket == NULL || g_ws.connect == NULL || g_ws.bind == NULL ||
        g_ws.listen == NULL || g_ws.accept == NULL || g_ws.send == NULL ||
        g_ws.recv == NULL || g_ws.closesocket == NULL ||
        g_ws.setsockopt == NULL || g_ws.getsockopt == NULL ||
        g_ws.select == NULL || g_ws.ioctlsocket == NULL ||
        g_ws.gethostbyname == NULL || g_ws.inet_addr == NULL ||
        g_ws.htons == NULL || g_ws.htonl == NULL ||
        g_ws.lasterror == NULL) {
        FreeLibrary(g_ws.lib);
        g_ws.lib = NULL;
        return -1;
    }
    if (g_ws.startup(MAKEWORD(2, 2), &data) != 0) {
        FreeLibrary(g_ws.lib);
        g_ws.lib = NULL;
        return -1;
    }
    g_ws.loaded = 1;
    return 0;
#else
    return 0;
#endif
}

/* The address of a host: a name or a dotted address, and a port. */
static int sock_addr(const char *host, u16 port, struct sockaddr_in *out)
{
    struct hostent *he;
    struct in_addr addr;

    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = SOCK_HTONS(port);
    if (host == NULL || *host == '\0') {
        out->sin_addr.s_addr = SOCK_HTONL(INADDR_LOOPBACK);   /* 127.0.0.1 */
        return 0;
    }
    addr.s_addr = SOCK_INET_ADDR(host);
    if (addr.s_addr != 0xFFFFFFFFu) {
        out->sin_addr = addr;
        return 0;
    }
    he = SOCK_GETHOSTBYNAME(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
        return -1;
    }
    memcpy(&out->sin_addr, he->h_addr_list[0], sizeof(out->sin_addr));
    return 0;
}

/* The socket's send and receive timeouts, in milliseconds. */
static void sock_set_timeouts(BwSock s, int ms)
{
#ifdef _WIN32
    DWORD tv = (DWORD)ms;

    (void)SOCK_SETSOCKOPT(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv,
                          (int)sizeof(tv));
    (void)SOCK_SETSOCKOPT(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                          (int)sizeof(tv));
#else
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    (void)SOCK_SETSOCKOPT(s, SOL_SOCKET, SO_SNDTIMEO, SOCK_OPT_VALUE(&tv),
                          (BwOptLen)sizeof(tv));
    (void)SOCK_SETSOCKOPT(s, SOL_SOCKET, SO_RCVTIMEO, SOCK_OPT_VALUE(&tv),
                          (BwOptLen)sizeof(tv));
#endif
}

/* Non-blocking or blocking again.  0 on success. */
static int sock_set_nonblock(BwSock s, int on)
{
#ifdef _WIN32
    unsigned long mode = on ? 1ul : 0ul;

    if (g_ws.ioctlsocket == NULL) {
        return -1;
    }
    if (g_ws.ioctlsocket(s, (long)FIONBIO, &mode) == BW_SOCKET_ERROR) {
        return -1;
    }
    return 0;
#elif defined(__wasm__)
    if (!wsock_ok(s)) {
        return -1;
    }
    g_wsock[s].nonblock = on;
    return 0;
#else
    int flags = fcntl(s, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    if (fcntl(s, F_SETFL, flags) < 0) {
        return -1;
    }
    return 0;
#endif
}

/* Wait for a socket to be readable (or writable).  1 when it is, 0 when
 * the wait ran out, -1 on an error. */
static int sock_wait(BwSock s, int timeout_ms, int for_write)
{
    fd_set set;
    struct timeval tv;
    int rc;

    FD_ZERO(&set);
    FD_SET(s, &set);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (for_write) {
        rc = SOCK_SELECT((int)s + 1, NULL, &set, NULL, &tv);
    } else {
        rc = SOCK_SELECT((int)s + 1, &set, NULL, NULL, &tv);
    }
    if (rc < 0) {
        return -1;
    }
    return (rc > 0) ? 1 : 0;
}

/* A new stream socket. */
static BwSock sock_new(void)
{
    BwSock s;

    if (sock_startup() != 0) {
        return BW_SOCK_INVALID;
    }
    s = SOCK_SOCKET(AF_INET, SOCK_STREAM, 0);
    if (s == BW_SOCK_INVALID) {
        return BW_SOCK_INVALID;
    }
    sock_set_timeouts(s, BW_MOD_NET_TIMEOUT_MS);
    return s;
}

/*
 * Connect to `host`:`port`, with a timeout: the socket is non-blocking for
 * the connect itself, so a relay that is not there costs the timeout and
 * not the operating system's own (which is minutes).
 */
static int sock_connect_to(BwSock s, const char *host, u16 port)
{
    struct sockaddr_in addr;
    int rc;

    if (sock_addr(host, port, &addr) != 0) {
        return -1;
    }
    if (sock_set_nonblock(s, 1) != 0) {
        return -1;
    }
    rc = SOCK_CONNECT(s, (const struct sockaddr *)&addr,
                       (BwSockLen)sizeof(addr));
    if (rc != 0) {
        int err = 0;
        BwOptLen len = (BwOptLen)sizeof(err);

        if (sock_wait(s, BW_MOD_NET_TIMEOUT_MS, 1) != 1) {
            (void)sock_set_nonblock(s, 0);
            return -1;
        }
        if (SOCK_GETSOCKOPT(s, SOL_SOCKET, SO_ERROR, SOCK_OPT_BACK(&err),
                            &len) != 0 || err != 0) {
            (void)sock_set_nonblock(s, 0);
            return -1;
        }
    }
    if (sock_set_nonblock(s, 0) != 0) {
        return -1;
    }
    return 0;
}

/*
 * The external control's listener: a socket bound to 127.0.0.1 and to
 * `port`, and nothing else.  The loopback address is the document's rule
 * ("The port listens on 127.0.0.1 only"), and it is this call that keeps
 * it: the address is written here and nowhere else.
 */
static BwSock sock_listen_loopback(u16 port)
{
    struct sockaddr_in addr;
    BwSock s;
    int on = 1;

    s = sock_new();
    if (s == BW_SOCK_INVALID) {
        return BW_SOCK_INVALID;
    }
    (void)SOCK_SETSOCKOPT(s, SOL_SOCKET, SO_REUSEADDR, SOCK_OPT_VALUE(&on),
                          (BwOptLen)sizeof(on));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = SOCK_HTONS(port);
    addr.sin_addr.s_addr = SOCK_HTONL(INADDR_LOOPBACK);        /* 127.0.0.1 */
    if (SOCK_BIND(s, (const struct sockaddr *)&addr,
                  (BwSockLen)sizeof(addr)) == BW_SOCKET_ERROR) {
        SOCK_CLOSE(s);
        return BW_SOCK_INVALID;
    }
    if (SOCK_LISTEN(s, 8) == BW_SOCKET_ERROR) {
        SOCK_CLOSE(s);
        return BW_SOCK_INVALID;
    }
    (void)sock_set_nonblock(s, 1);
    return s;
}

/* One connection, or an invalid socket when there is none waiting. */
static BwSock sock_accept_one(BwSock listener)
{
    return SOCK_ACCEPT(listener, NULL, NULL);
}

/* Read what is there.  1 when bytes were read, 0 when there is nothing
 * right now, -1 when the connection is closed or broken. */
static int sock_recv_some(BwSock s, void *buf, u32 size, u32 *out)
{
    int rc;

    *out = 0;
    if (sock_wait(s, 0, 0) != 1) {
        return 0;
    }
    rc = SOCK_RECV(s, (char *)buf, (int)size, 0);
    if (rc == 0) {
        return -1;                      /* the other end closed it */
    }
    if (rc < 0) {
        return sock_would_block() ? 0 : -1;
    }
    *out = (u32)rc;
    return 1;
}

/* Write all of it.  0 on success. */
static int sock_send_all(BwSock s, const void *buf, u32 size)
{
    const u8 *p = (const u8 *)buf;
    u32 left = size;

    while (left > 0) {
        int rc = SOCK_SEND(s, (const char *)p, (int)left, 0);

        if (rc <= 0) {
            return -1;
        }
        p += rc;
        left -= (u32)rc;
    }
    return 0;
}

/* Close it. */
static void sock_shutdown(BwSock s)
{
    if (s != BW_SOCK_INVALID) {
        SOCK_CLOSE(s);
    }
}

/* Sleep a little: the reader polls the transport, and the control's
 * listener waits for a connection or a line. */
static void net_sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#elif defined(__wasm__)
    /* the other fibers run meanwhile (a libc sleep would stop them all) */
    bw_thread_sleep_ns((unsigned long long)ms * 1000000ull);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* One player of the session. */
typedef struct BwModNetPeer {
    u32  number;
    char name[BW_MOD_NET_NAME_MAX + 1];
    int  present;
} BwModNetPeer;

/* The session: one at a time, per port. */
typedef struct BwModNetSession {
    int  running;
    int  hosting;
    char code[BW_MOD_NET_CODE_MAX + 1];
    u32  self;                                   /* this player's number */
    char name[BW_MOD_NET_NAME_MAX + 1];
    char token[33];                              /* the relay's name for us */
    BwModNetPeer peers[BW_MOD_NET_PEERS_MAX];
} BwModNetSession;

/* One mod's handlers, keyed on the mod's namespace (its own name). */
typedef struct BwModNetMod {
    char             ns[BW_MOD_NET_KEY_MAX + 1];
    BwSessionHandler session;
    BwCommandHandler command;
    int              used;
} BwModNetMod;

/* The read buffer: the bytes that came from the transport and the state
 * of the frame parser. */
typedef struct BwModNetRx {
    u8  buf[BW_MOD_NET_RX_MAX];
    u32 len;                                     /* the bytes in the buffer  */
    u32 pos;                                     /* the byte being read      */
    u32 want;                                    /* the payload's length     */
    u32 acc;                                     /* the length so far        */
    u32 digits;                                  /* the length's digits      */
    int state;                                   /* the parser's state       */
    char type;                                   /* the frame's type: kept
                                                    while its bytes come in
                                                    over several reads      */
} BwModNetRx;

/* The queue: the network threads write into it, the frame drains it.  Two
 * of them, swapped at the frame, so the drain never holds the lock while
 * it calls a handler. */
typedef struct BwModNetQueue {
    u8 *mem;
    u32 cap;
    u32 head;      /* the first record's byte */
    u32 len;       /* the bytes in use       */
} BwModNetQueue;

static BwModNetSession  g_session;
static BwModNetMod      g_mods[BW_MOD_NET_MODS_MAX];
static BwModNetRx       g_rx;
static BwModNetQueue    g_q_in;
static BwModNetQueue    g_q_out;

static BwMutex  *g_lock;
static BwThread *g_net_thread;
static int       g_net_stop;

static BwModNetTransport g_transport;
static int               g_have_transport;
static char              g_relay[BW_MOD_NET_HOST_MAX];
static u16               g_relay_port;
static char              g_player[BW_MOD_NET_NAME_MAX + 1];

static BwSock   g_listener = BW_SOCK_INVALID;
static BwThread *g_ctl_thread;
static int       g_ctl_stop;
static BwSock   g_clients[BW_MOD_NET_CLIENTS_MAX];
static int      g_client_used[BW_MOD_NET_CLIENTS_MAX];
static int      g_client_count;
static int      g_control_port;

static u32      g_dropped;

/* The port's own handler: the messages under BW_MOD_NET_PORT_KEY, every
 * join and leave, and the end of the session. */
static BwSessionHandler g_port_handler;

/* The relay's last error (`E <reason> <text>`), for the port: the reason
 * word ("unknown-code" after a relay restart, "removed", "full" and the
 * others) and the relay's own text after it, if any. */
static char g_last_error[32];
static char g_last_error_text[256];

static void copy_text(char *dst, size_t size, const char *src);

static void set_last_error(const char *reason)
{
    u32 n = 0;

    copy_text(g_last_error_text, sizeof(g_last_error_text), reason);
    while (reason[n] != '\0' && reason[n] != ' ' && n + 1 < sizeof(g_last_error)) {
        g_last_error[n] = reason[n];
        n++;
    }
    g_last_error[n] = '\0';
}

/* When the client last wrote a frame (for the keep-alive). */
static unsigned long long g_last_tx_ns;

/* The tokens: one per session code, so a player who comes back to a code
 * (after a drop, or after the game was restarted) is the same peer, and the
 * relay cannot link one player's sessions. */
typedef struct BwModNetToken {
    char code[BW_MOD_NET_CODE_MAX + 1];
    char token[33];
} BwModNetToken;

static BwModNetToken g_tokens[BW_MOD_NET_TOKENS_MAX];
static u32           g_token_count;
static u32           g_token_next;      /* the entry a new code replaces */
static char          g_token_file[512];
static int           g_tokens_read;

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* Copy `src` into `dst`, cutting it at the buffer's end. */
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t n;

    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/*
 * Copy `len` bytes of `src` into `dst`, cutting it at the buffer's end.
 * The payload of a frame is not a C string, so this is the copy the frame
 * handlers use.
 */
static void copy_text_n(char *dst, size_t size, const char *src, size_t len)
{
    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* The namespace of the mod that is calling, or "" when there is none (the
 * port itself is calling). */
static const char *current_namespace(void)
{
    const char *ns = bw_mod_savedata_namespace();

    return (ns != NULL) ? ns : "";
}

/* SplitMix64: the port's own generator (sdk/mod_timer.c has the same one),
 * used here for the token the relay remembers this player by. */
static u64 splitmix64(u64 *state)
{
    u64 z;

    *state += UINT64_C(0x9E3779B97F4A7C15);
    z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4F5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* The lock and the queues, made when they are first needed.  The first
 * call comes from the game's thread, before any network thread is
 * running. */
static void net_init(void)
{
    if (g_lock == NULL) {
        g_lock = bw_mutex_create();
    }
    if (g_q_in.mem == NULL) {
        g_q_in.mem = (u8 *)malloc(BW_MOD_NET_QUEUE_BYTES);
        if (g_q_in.mem != NULL) {
            g_q_in.cap = BW_MOD_NET_QUEUE_BYTES;
        }
    }
    if (g_q_out.mem == NULL) {
        g_q_out.mem = (u8 *)malloc(BW_MOD_NET_QUEUE_BYTES);
        if (g_q_out.mem != NULL) {
            g_q_out.cap = BW_MOD_NET_QUEUE_BYTES;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The queue                                                           */
/* ------------------------------------------------------------------ */

/* The size of the record at the queue's head, or 0 when there is none. */
static u32 queue_record_size(const BwModNetQueue *q)
{
    u32 key_len;
    u32 size;

    if (q->len < 16u) {
        return 0;
    }
    key_len = (u32)q->mem[q->head + 8] |
              ((u32)q->mem[q->head + 9] << 8) |
              ((u32)q->mem[q->head + 10] << 16) |
              ((u32)q->mem[q->head + 11] << 24);
    size = (u32)q->mem[q->head + 12] |
           ((u32)q->mem[q->head + 13] << 8) |
           ((u32)q->mem[q->head + 14] << 16) |
           ((u32)q->mem[q->head + 15] << 24);
    if (16u + key_len + size > q->len) {
        return 0;                       /* the record is not whole */
    }
    return 16u + key_len + size;
}

/* Drop the oldest record of the queue. */
static void queue_drop_oldest(BwModNetQueue *q)
{
    u32 n = queue_record_size(q);

    if (n == 0 || n > q->len) {
        q->head = 0;
        q->len = 0;
        return;
    }
    q->head += n;
    q->len -= n;
    if (q->len == 0) {
        q->head = 0;
    }
}

/* Put one record into the queue.  0 on success, -1 when there was no room
 * (the record is dropped, and the port counts it). */
static int queue_put(BwModNetQueue *q, u32 peer, u32 kind,
                     const char *key, u32 key_len,
                     const void *data, u32 size)
{
    u32 need = 16u + key_len + size;
    u8 *p;

    if (q->mem == NULL || need > q->cap) {
        return -1;
    }
    while (q->len + need > q->cap) {
        if (q->len == 0) {
            break;
        }
        queue_drop_oldest(q);
    }
    if (q->len + need > q->cap) {
        return -1;
    }
    if (q->head + q->len + need > q->cap) {
        /* the tail does not fit: move what is left to the front */
        if (q->len > 0) {
            memmove(q->mem, q->mem + q->head, q->len);
        }
        q->head = 0;
    }
    p = q->mem + q->head + q->len;
    p[0] = (u8)(peer & 0xFFu);
    p[1] = (u8)((peer >> 8) & 0xFFu);
    p[2] = (u8)((peer >> 16) & 0xFFu);
    p[3] = (u8)((peer >> 24) & 0xFFu);
    p[4] = (u8)(kind & 0xFFu);
    p[5] = (u8)((kind >> 8) & 0xFFu);
    p[6] = (u8)((kind >> 16) & 0xFFu);
    p[7] = (u8)((kind >> 24) & 0xFFu);
    p[8] = (u8)(key_len & 0xFFu);
    p[9] = (u8)((key_len >> 8) & 0xFFu);
    p[10] = (u8)((key_len >> 16) & 0xFFu);
    p[11] = (u8)((key_len >> 24) & 0xFFu);
    p[12] = (u8)(size & 0xFFu);
    p[13] = (u8)((size >> 8) & 0xFFu);
    p[14] = (u8)((size >> 16) & 0xFFu);
    p[15] = (u8)((size >> 24) & 0xFFu);
    if (key_len > 0 && key != NULL) {
        memcpy(p + 16, key, key_len);
    }
    if (size > 0 && data != NULL) {
        memcpy(p + 16 + key_len, data, size);
    }
    q->len += need;
    return 0;
}

/* Take one record out of the queue.  1 when there was one. */
static int queue_take(BwModNetQueue *q, u32 *peer, u32 *kind,
                      char *key, u32 key_max, const u8 **data, u32 *size)
{
    const u8 *p;
    u32 n;

    if (q->len == 0) {
        return 0;
    }
    n = queue_record_size(q);
    if (n == 0 || n > q->len) {
        q->head = 0;
        q->len = 0;
        return 0;
    }
    p = q->mem + q->head;
    *peer = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
            ((u32)p[3] << 24);
    *kind = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) |
            ((u32)p[7] << 24);
    {
        u32 key_len = (u32)p[8] | ((u32)p[9] << 8) | ((u32)p[10] << 16) |
                      ((u32)p[11] << 24);
        u32 body = (u32)p[12] | ((u32)p[13] << 8) | ((u32)p[14] << 16) |
                   ((u32)p[15] << 24);

        if (key_len > key_max) {
            key_len = key_max;
        }
        if (key_len > 0) {
            memcpy(key, p + 16, key_len);
        }
        key[key_len] = '\0';
        *data = p + 16 + key_len;
        *size = body;
    }
    q->head += n;
    q->len -= n;
    if (q->len == 0) {
        q->head = 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The mods                                                            */
/* ------------------------------------------------------------------ */

/*
 * The mod with the namespace `ns`, made when it is not there yet.  The
 * registry is written on the game's thread (a mod binds its handlers from
 * its start, and the port resets it) and read on the game's thread (the
 * frame), so it needs no lock of its own.
 */
static BwModNetMod *mod_for(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        ns = "";
    }
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        if (g_mods[i].used && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        if (g_mods[i].used) {
            continue;
        }
        memset(&g_mods[i], 0, sizeof(g_mods[i]));
        copy_text(g_mods[i].ns, sizeof(g_mods[i].ns), ns);
        g_mods[i].used = 1;
        return &g_mods[i];
    }
    return NULL;
}

/* The mod with the namespace `ns`, or NULL when there is none. */
static BwModNetMod *mod_lookup(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        return NULL;
    }
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        if (g_mods[i].used && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The peers                                                           */
/* ------------------------------------------------------------------ */

static BwModNetPeer *peer_find(u32 number)
{
    u32 i;

    for (i = 0; i < BW_MOD_NET_PEERS_MAX; i++) {
        if (g_session.peers[i].present &&
            g_session.peers[i].number == number) {
            return &g_session.peers[i];
        }
    }
    return NULL;
}

/* A peer joined, or is here again after a rejoin: the number is the same
 * one it had, so the record is reused. */
static void peer_set(u32 number, const char *name)
{
    BwModNetPeer *p = peer_find(number);
    u32 i;

    if (p == NULL) {
        for (i = 0; i < BW_MOD_NET_PEERS_MAX; i++) {
            if (!g_session.peers[i].present) {
                p = &g_session.peers[i];
                break;
            }
        }
    }
    if (p == NULL) {
        return;
    }
    p->number = number;
    p->present = 1;
    copy_text(p->name, sizeof(p->name), name);
}

/* A peer left. */
static void peer_clear(u32 number)
{
    BwModNetPeer *p = peer_find(number);

    if (p != NULL) {
        p->present = 0;
        p->name[0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* The transport                                                       */
/* ------------------------------------------------------------------ */

/* The default transport: there is no relay, and it says so. */
static int default_connect(void *user, const char *host, u16 port)
{
    (void)user;
    (void)host;
    (void)port;
    return -1;
}

static int default_send(void *user, const void *buf, u32 size)
{
    (void)user;
    (void)buf;
    (void)size;
    return -1;
}

static int default_recv(void *user, void *buf, u32 size, u32 *out_size)
{
    (void)user;
    (void)buf;
    (void)size;
    if (out_size != NULL) {
        *out_size = 0;
    }
    return 0;
}

static void default_close(void *user)
{
    (void)user;
}

static const BwModNetTransport k_no_relay = {
    NULL,
    default_connect,
    default_send,
    default_recv,
    default_close
};

/* The transport in force. */
static const BwModNetTransport *the_transport(void)
{
    if (!g_have_transport) {
        return &k_no_relay;
    }
    return &g_transport;
}

/* The port's own TCP transport.  A relay written `tls://name` is reached
 * over TLS on the same transport (below, "TLS"). */
static int g_relay_tls;             /* the relay's address said tls:// */

#ifdef EIDOS_HAVE_TLS
/* ------------------------------------------------------------------ */
/* TLS                                                                 */
/* ------------------------------------------------------------------ */

/*
 * The relay over TLS (metaxy docs/SPEC.md sections 2 and 9): Mbed TLS 3.6
 * (tools/fetch_mbedtls.py), TLS 1.2 or 1.3, the relay's certificate checked
 * against Let's Encrypt's two roots (sdk/tls_roots.c) and the relay's name.
 * EIDOS_TLS_CA names one more root in a PEM file, for a relay with a
 * certificate of its own (a self-hosted test).  The protocol is the same
 * one; only the bytes go through the TLS layer.
 */
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#if defined(__wasm__)
#include <unistd.h>

#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

/* The random bytes Mbed TLS asks for (cmake/mbedtls_wasm.h): WASI's
 * random_get, the platform's own generator on the host's side. */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen);
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    size_t done = 0;

    (void)data;
    while (done < len) {
        size_t n = len - done > 256 ? 256 : len - done;

        if (getentropy(out + done, n) != 0) {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        done += n;
    }
    *olen = len;
    return 0;
}

/* Mbed TLS's millisecond clock (MBEDTLS_PLATFORM_MS_TIME_ALT): it knows
 * none for WASI. */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)(bw_clock_ns() / 1000000ull);
}
#endif
#include "psa/crypto.h"
#include "tls_roots.h"

typedef struct BwTls {
    int                      up;        /* the handshake is done */
    int                      made;      /* the structures below are set up */
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         ca;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
} BwTls;

static BwTls g_tls;

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    BwSock s = *(BwSock *)ctx;
    int rc = SOCK_SEND(s, (const char *)buf, (int)len, 0);

    if (rc < 0) {
        return sock_would_block() ? MBEDTLS_ERR_SSL_WANT_WRITE
                                  : MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return rc;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    BwSock s = *(BwSock *)ctx;
    int rc;

    if (sock_wait(s, 0, 0) != 1) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    rc = SOCK_RECV(s, (char *)buf, (int)len, 0);
    if (rc < 0) {
        return sock_would_block() ? MBEDTLS_ERR_SSL_WANT_READ
                                  : MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return rc;                          /* 0: the other end closed it */
}

static void tls_free(void)
{
    if (g_tls.made) {
        mbedtls_ssl_free(&g_tls.ssl);
        mbedtls_ssl_config_free(&g_tls.conf);
        mbedtls_x509_crt_free(&g_tls.ca);
        mbedtls_ctr_drbg_free(&g_tls.drbg);
        mbedtls_entropy_free(&g_tls.entropy);
    }
    memset(&g_tls, 0, sizeof(g_tls));
}

static void tls_say(const char *what, int rc)
{
    char why[128];

    mbedtls_strerror(rc, why, sizeof(why));
    fprintf(stderr, "eidos: the relay's TLS: %s: %s (-0x%04X)\n", what, why,
            (unsigned)-rc);
}

/* The handshake over the connected socket `*s`, with the relay `host`. */
static int tls_open(BwSock *s, const char *host)
{
    const char *extra = getenv("EIDOS_TLS_CA");
    unsigned long long until = bw_clock_ns() + (unsigned long long)BW_MOD_NET_TIMEOUT_MS * 3000000ull;
    int rc;

    tls_free();
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "eidos: the relay's TLS: the crypto library did not start\n");
        return -1;
    }
    mbedtls_ssl_init(&g_tls.ssl);
    mbedtls_ssl_config_init(&g_tls.conf);
    mbedtls_x509_crt_init(&g_tls.ca);
    mbedtls_entropy_init(&g_tls.entropy);
    mbedtls_ctr_drbg_init(&g_tls.drbg);
    g_tls.made = 1;
    if ((rc = mbedtls_ctr_drbg_seed(&g_tls.drbg, mbedtls_entropy_func,
                                    &g_tls.entropy, (const unsigned char *)"eidos", 5)) != 0) {
        tls_say("no random source", rc);
        return -1;
    }
    if ((rc = mbedtls_x509_crt_parse(&g_tls.ca, (const unsigned char *)bw_tls_roots_pem,
                                     strlen(bw_tls_roots_pem) + 1)) != 0) {
        tls_say("the built-in roots", rc);
        return -1;
    }
    if (extra != NULL && extra[0] != '\0' &&
        (rc = mbedtls_x509_crt_parse_file(&g_tls.ca, extra)) != 0) {
        tls_say("EIDOS_TLS_CA", rc);
        return -1;
    }
    if ((rc = mbedtls_ssl_config_defaults(&g_tls.conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        tls_say("the settings", rc);
        return -1;
    }
    mbedtls_ssl_conf_authmode(&g_tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_tls.conf, &g_tls.ca, NULL);
    mbedtls_ssl_conf_rng(&g_tls.conf, mbedtls_ctr_drbg_random, &g_tls.drbg);
    mbedtls_ssl_conf_min_tls_version(&g_tls.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    if ((rc = mbedtls_ssl_setup(&g_tls.ssl, &g_tls.conf)) != 0 ||
        (rc = mbedtls_ssl_set_hostname(&g_tls.ssl, host)) != 0) {
        tls_say("the session", rc);
        return -1;
    }
    mbedtls_ssl_set_bio(&g_tls.ssl, s, tls_bio_send, tls_bio_recv, NULL);
    while ((rc = mbedtls_ssl_handshake(&g_tls.ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            u32 flags = mbedtls_ssl_get_verify_result(&g_tls.ssl);

            if (flags != 0 && flags != (u32)-1) {
                char why[256];

                mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
                fprintf(stderr, "eidos: the relay's TLS: its certificate is "
                        "refused: %s", why);
            }
            tls_say("the handshake", rc);
            return -1;
        }
        if (bw_clock_ns() > until) {
            fprintf(stderr, "eidos: the relay's TLS: the handshake took too long\n");
            return -1;
        }
        (void)sock_wait(*s, 50, rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    g_tls.up = 1;
    fprintf(stderr, "eidos: the relay's TLS: %s, %s\n",
            mbedtls_ssl_get_version(&g_tls.ssl),
            mbedtls_ssl_get_ciphersuite(&g_tls.ssl));
    return 0;
}

static int tls_send_all(const void *buf, u32 size)
{
    const unsigned char *p = (const unsigned char *)buf;
    u32 left = size;

    while (left > 0) {
        int rc = mbedtls_ssl_write(&g_tls.ssl, p, left);

        if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
            continue;
        }
        if (rc <= 0) {
            tls_say("a send", rc);
            return -1;
        }
        p += rc;
        left -= (u32)rc;
    }
    return 0;
}

/* 1 when bytes were read, 0 when there is nothing now, -1 when closed. */
static int tls_recv_some(void *buf, u32 size, u32 *out)
{
    int rc = mbedtls_ssl_read(&g_tls.ssl, (unsigned char *)buf, size);

    *out = 0;
    if (rc > 0) {
        *out = (u32)rc;
        return 1;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE ||
        rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
        return 0;
    }
    if (rc != 0 && rc != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        tls_say("a read", rc);
    }
    return -1;
}
#endif /* EIDOS_HAVE_TLS */

static int tcp_connect(void *user, const char *host, u16 port)
{
    BwSock *s = (BwSock *)user;

    if (s == NULL) {
        return -1;
    }
    *s = BW_SOCK_INVALID;
    *s = sock_new();
    if (*s == BW_SOCK_INVALID) {
        return -1;
    }
    if (sock_connect_to(*s, host, port) != 0) {
        sock_shutdown(*s);
        *s = BW_SOCK_INVALID;
        return -1;
    }
    if (g_relay_tls) {
#ifdef EIDOS_HAVE_TLS
        if (tls_open(s, host) != 0) {
            sock_shutdown(*s);
            *s = BW_SOCK_INVALID;
            return -1;
        }
#else
        fprintf(stderr, "eidos: the relay %s asks for TLS, and this build has "
                "none (tools/fetch_mbedtls.py)\n", host);
        sock_shutdown(*s);
        *s = BW_SOCK_INVALID;
        return -1;
#endif
    }
    return 0;
}

static int tcp_send(void *user, const void *buf, u32 size)
{
    BwSock *s = (BwSock *)user;

    if (s == NULL || *s == BW_SOCK_INVALID) {
        return -1;
    }
#ifdef EIDOS_HAVE_TLS
    if (g_tls.up) {
        return tls_send_all(buf, size);
    }
#endif
    return sock_send_all(*s, buf, size);
}

static int tcp_recv(void *user, void *buf, u32 size, u32 *out_size)
{
    BwSock *s = (BwSock *)user;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (s == NULL || *s == BW_SOCK_INVALID) {
        return -1;
    }
#ifdef EIDOS_HAVE_TLS
    if (g_tls.up) {
        return tls_recv_some(buf, size, out_size);
    }
#endif
    return sock_recv_some(*s, buf, size, out_size);
}

static void tcp_close(void *user)
{
    BwSock *s = (BwSock *)user;

    if (s != NULL && *s != BW_SOCK_INVALID) {
#ifdef EIDOS_HAVE_TLS
        if (g_tls.up) {
            (void)mbedtls_ssl_close_notify(&g_tls.ssl);
        }
        tls_free();
#endif
        sock_shutdown(*s);
        *s = BW_SOCK_INVALID;
    }
}

static BwSock g_tcp_socket = BW_SOCK_INVALID;
static BwModNetTransport g_tcp_transport = {
    &g_tcp_socket,
    tcp_connect,
    tcp_send,
    tcp_recv,
    tcp_close
};

/* ------------------------------------------------------------------ */
/* The protocol: writing                                               */
/* ------------------------------------------------------------------ */

/* Write one frame to the transport.  0 on success. */
static int relay_send(char type, const void *payload, u32 size)
{
    const BwModNetTransport *t = the_transport();
    char head[32];
    int n;

    if (t->send == NULL) {
        return -1;
    }
    n = snprintf(head, sizeof(head), "%c %u\n", type, (unsigned int)size);
    if (n < 0 || (size_t)n >= sizeof(head)) {
        return -1;
    }
    if (t->send(t->user, head, (u32)n) != 0) {
        return -1;
    }
    if (size > 0 && t->send(t->user, payload, size) != 0) {
        return -1;
    }
    g_last_tx_ns = bw_clock_ns();
    return 0;
}

/* ------------------------------------------------------------------ */
/* The protocol: reading                                               */
/* ------------------------------------------------------------------ */

/* Fill the read buffer from the transport.  1 when bytes came, 0 when
 * there is nothing right now, -1 when the connection is broken. */
static int rx_fill(void)
{
    const BwModNetTransport *t = the_transport();
    u32 got = 0;
    int rc;

    if (t->recv == NULL) {
        return -1;
    }
    if (g_rx.pos > 0) {
        /* Drop what has been read: the buffer never grew, but the bytes
         * that are parsed leave the front of it behind, and without this
         * the buffer fills up and the session ends (a few kilobytes of
         * frames were the whole session's life). */
        memmove(g_rx.buf, g_rx.buf + g_rx.pos, g_rx.len - g_rx.pos);
        g_rx.len -= g_rx.pos;
        g_rx.pos = 0;
    }
    if (g_rx.len >= sizeof(g_rx.buf)) {
        return -1;                      /* a frame bigger than the buffer */
    }
    rc = t->recv(t->user, g_rx.buf + g_rx.len,
                 (u32)(sizeof(g_rx.buf) - g_rx.len), &got);
    if (rc < 0) {
        return -1;
    }
    if (rc == 0 || got == 0) {
        return 0;
    }
    g_rx.len += got;
    return 1;
}

/*
 * Parse one frame out of the read buffer.  1 when there is one (the type
 * and the payload, which points into the buffer), 0 when more bytes are
 * needed, -1 when the stream is not the protocol's.
 */
static int rx_parse(char *type, u8 **payload, u32 *size)
{
    for (;;) {
        u8 c;

        switch (g_rx.state) {
        case 0:                         /* the type */
            if (g_rx.pos >= g_rx.len) {
                return 0;
            }
            g_rx.type = (char)g_rx.buf[g_rx.pos++];
            g_rx.acc = 0;
            g_rx.digits = 0;
            g_rx.state = 1;
            break;

        case 1:                         /* the length, in decimal */
            if (g_rx.pos >= g_rx.len) {
                return 0;
            }
            c = g_rx.buf[g_rx.pos];
            if (c == (u8)' ') {
                g_rx.pos++;             /* the space after the type */
                break;
            }
            if (c == (u8)'\n') {
                /* A length of 0 is a frame with an empty payload. */
                if (g_rx.digits == 0 || g_rx.acc > BW_MOD_NET_RX_MAX) {
                    return -1;
                }
                g_rx.want = g_rx.acc;
                g_rx.state = 2;
                g_rx.pos++;
                break;
            }
            if (c < (u8)'0' || c > (u8)'9') {
                return -1;
            }
            if (g_rx.acc > (BW_MOD_NET_RX_MAX + 1u) / 10u) {
                return -1;
            }
            g_rx.acc = g_rx.acc * 10u + (u32)(c - (u8)'0');
            g_rx.digits++;
            g_rx.pos++;
            break;

        case 2:                         /* the payload */
            if (g_rx.len - g_rx.pos < g_rx.want) {
                return 0;
            }
            /* the type comes from the state, not the caller: a frame whose
             * bytes came in over two reads was started by an earlier call,
             * and the caller's own `type` is a fresh variable each time */
            *type = g_rx.type;
            *payload = g_rx.buf + g_rx.pos;
            *size = g_rx.want;
            g_rx.pos += g_rx.want;
            g_rx.state = 0;
            return 1;

        default:
            return -1;
        }
    }
}

/* Read one frame, waiting for the transport to give one.  1 when a frame
 * was read, 0 when nothing came, -1 when the connection is broken. */
static int rx_frame(char *type, u8 **payload, u32 *size)
{
    for (;;) {
        int rc = rx_parse(type, payload, size);

        if (rc != 0) {
            return rc;
        }
        rc = rx_fill();
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The session                                                         */
/* ------------------------------------------------------------------ */

/* `n` bytes from the operating system's random source.  0 on success. */
static int os_random(u8 *out, u32 n)
{
#if defined(_WIN32)
    typedef LONG (WINAPI *GenRandomFn)(void *, unsigned char *, ULONG, ULONG);
    static GenRandomFn gen;
    static int tried;

    if (!tried) {
        HMODULE lib = LoadLibraryA("bcrypt.dll");

        tried = 1;
        if (lib != NULL) {
            gen = (GenRandomFn)(void *)GetProcAddress(lib, "BCryptGenRandom");
        }
    }
    /* 2 is BCRYPT_USE_SYSTEM_PREFERRED_RNG. */
    return (gen != NULL && gen(NULL, out, n, 2u) == 0) ? 0 : -1;
#elif defined(__wasm__)
    /* WASI's random_get, the platform's generator on the host's side */
    return getentropy(out, n) == 0 ? 0 : -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;

    if (f == NULL) {
        return -1;
    }
    got = fread(out, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
#endif
}

/* A new token: 32 hexadecimal digits from the operating system's random
 * source, or from the clock when there is none (said once). */
static void token_make(char token[33])
{
    static const char digits[] = "0123456789abcdef";
    u8 raw[16];
    int i;

    if (os_random(raw, sizeof(raw)) != 0) {
        u64 state = (u64)bw_clock_ns() ^
                    ((u64)(uintptr_t)token * UINT64_C(0x9E3779B97F4A7C15));
        u64 a = splitmix64(&state);
        u64 b = splitmix64(&state);

        fprintf(stderr, "eidos: no random source for the session token; "
                "the clock stands in\n");
        for (i = 0; i < 8; i++) {
            raw[i] = (u8)(a >> (8 * i));
            raw[8 + i] = (u8)(b >> (8 * i));
        }
    }
    for (i = 0; i < 16; i++) {
        token[2 * i] = digits[raw[i] >> 4];
        token[2 * i + 1] = digits[raw[i] & 0xFu];
    }
    token[32] = '\0';
}

/* A session code as the relay knows it: upper case, without the spaces and
 * dashes a player may type. */
static void code_clean(char *out, size_t size, const char *code)
{
    size_t n = 0;

    for (; code != NULL && *code != '\0' && n + 1 < size; code++) {
        char c = *code;

        if (c == ' ' || c == '-' || c == '\t') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[n++] = c;
    }
    out[n] = '\0';
}

/* Read the token file once: "<code> <token>" lines. */
static void tokens_read(void)
{
    FILE *f;
    char line[128];

    if (g_tokens_read || g_token_file[0] == '\0') {
        return;
    }
    g_tokens_read = 1;
    f = fopen(g_token_file, "rb");
    if (f == NULL) {
        return;
    }
    while (g_token_count < BW_MOD_NET_TOKENS_MAX &&
           fgets(line, sizeof(line), f) != NULL) {
        BwModNetToken *t = &g_tokens[g_token_count];

        if (sscanf(line, "%32s %32s", t->code, t->token) == 2 &&
            strlen(t->token) == 32) {
            g_token_count++;
        }
    }
    fclose(f);
    g_token_next = g_token_count % BW_MOD_NET_TOKENS_MAX;
}

/* Write the token file whole. */
static void tokens_write(void)
{
    FILE *f;
    u32 i;

    if (g_token_file[0] == '\0') {
        return;
    }
    f = fopen(g_token_file, "wb");
    if (f == NULL) {
        return;
    }
    for (i = 0; i < g_token_count; i++) {
        fprintf(f, "%s %s\n", g_tokens[i].code, g_tokens[i].token);
    }
    fclose(f);
}

/* The token kept for a code, or NULL. */
static const char *token_find(const char *code)
{
    u32 i;

    tokens_read();
    for (i = 0; i < g_token_count; i++) {
        if (strcmp(g_tokens[i].code, code) == 0) {
            return g_tokens[i].token;
        }
    }
    return NULL;
}

/* Keep `token` for `code`; the oldest entry goes when the table is full. */
static void token_keep(const char *code, const char *token)
{
    BwModNetToken *t;
    u32 i;

    tokens_read();
    for (i = 0; i < g_token_count; i++) {
        if (strcmp(g_tokens[i].code, code) == 0) {
            copy_text(g_tokens[i].token, sizeof(g_tokens[i].token), token);
            tokens_write();
            return;
        }
    }
    if (g_token_count < BW_MOD_NET_TOKENS_MAX) {
        t = &g_tokens[g_token_count++];
    } else {
        t = &g_tokens[g_token_next];
        g_token_next = (g_token_next + 1u) % BW_MOD_NET_TOKENS_MAX;
    }
    copy_text(t->code, sizeof(t->code), code);
    copy_text(t->token, sizeof(t->token), token);
    tokens_write();
}

void bw_mod_net_random_hex(char out[33])
{
    token_make(out);
}

void bw_mod_net_set_token_file(const char *path)
{
    net_init();
    bw_mutex_lock(g_lock);
    copy_text(g_token_file, sizeof(g_token_file), path);
    g_tokens_read = 0;
    g_token_count = 0;
    g_token_next = 0;
    bw_mutex_unlock(g_lock);
}

/* The peer number in a frame's "<peer> <name>" text. */
static u32 relay_peer(const u8 *payload, u32 size, u32 *name_at)
{
    u32 peer = 0;
    u32 i;

    for (i = 0; i < size && payload[i] != (u8)' '; i++) {
        if (payload[i] >= (u8)'0' && payload[i] <= (u8)'9') {
            peer = peer * 10u + (u32)(payload[i] - (u8)'0');
        }
    }
    if (name_at != NULL) {
        *name_at = (i < size) ? i + 1u : size;
    }
    return peer;
}

/* Handle a frame the relay sent. */
static void relay_frame(char type, const u8 *payload, u32 size)
{
    char text[BW_MOD_NET_NAME_MAX + 1];
    u32 peer;
    u32 at;

    switch (type) {
    case BW_MOD_NET_MSG_WELCOME:
        /* "<peer> <name>" */
        peer = relay_peer(payload, size, &at);
        g_session.self = peer;
        copy_text_n(text, sizeof(text), (const char *)payload + at, size - at);
        copy_text(g_session.name, sizeof(g_session.name), text);
        break;

    case BW_MOD_NET_MSG_PEER:
        /* "<peer> <name>" */
        peer = relay_peer(payload, size, &at);
        copy_text_n(text, sizeof(text), (const char *)payload + at, size - at);
        peer_set(peer, text);
        if (queue_put(&g_q_in, peer, (u32)BW_SESSION_JOIN, NULL, 0,
                      NULL, 0) != 0) {
            g_dropped++;
        }
        break;

    case BW_MOD_NET_MSG_DROP:
        /* "<peer>" */
        peer = relay_peer(payload, size, &at);
        peer_clear(peer);
        if (queue_put(&g_q_in, peer, (u32)BW_SESSION_LEAVE, NULL, 0,
                      NULL, 0) != 0) {
            g_dropped++;
        }
        break;

    case BW_MOD_NET_MSG_DATA:
        /* "<peer>\n<key>\n<bytes>" */
        {
            u32 key_start;
            u32 key_len;
            u32 body;

            peer = 0;
            for (at = 0; at < size && payload[at] != (u8)'\n'; at++) {
                if (payload[at] >= (u8)'0' && payload[at] <= (u8)'9') {
                    peer = peer * 10u + (u32)(payload[at] - (u8)'0');
                }
            }
            if (at >= size) {
                break;                      /* malformed: no key */
            }
            at++;
            key_start = at;
            while (at < size && payload[at] != (u8)'\n') {
                at++;
            }
            if (at >= size) {
                break;                      /* malformed: no body */
            }
            key_len = at - key_start;
            body = size - (at + 1);
            if (key_len > BW_MOD_NET_KEY_MAX) {
                break;
            }
            if (queue_put(&g_q_in, peer, (u32)BW_SESSION_MESSAGE,
                          (const char *)payload + key_start, key_len,
                          payload + at + 1, body) != 0) {
                g_dropped++;
            }
        }
        break;

    case BW_MOD_NET_MSG_ERROR:
        {
            char reason[256];

            copy_text_n(reason, sizeof(reason), (const char *)payload, size);
            fprintf(stderr, "eidos: the relay says: %s\n", reason);
            set_last_error(reason);
            if (queue_put(&g_q_in, 0, BW_MOD_NET_EV_ENDED, NULL, 0,
                          (const u8 *)reason, (u32)strlen(reason)) != 0) {
                g_dropped++;
            }
        }
        /* the relay closes the connection after an error: the session is
         * over now, not when the reader notices the close */
        g_session.running = 0;
        g_net_stop = 1;
        break;

    default:
        break;
    }
}

/* The session's reader: it reads the frames and queues what the mods are
 * to be told.  It never calls a handler. */
static void net_reader(void *arg)
{
    (void)arg;

    for (;;) {
        char type = 0;
        u8 *payload = NULL;
        u32 size = 0;
        int rc;

        bw_mutex_lock(g_lock);
        if (g_net_stop) {
            bw_mutex_unlock(g_lock);
            break;
        }
        rc = rx_frame(&type, &payload, &size);
        if (rc == 1) {
            relay_frame(type, payload, size);
        }
        /* The keep-alive: the relay drops a client that is silent for a
         * minute, and a player who stands still sends nothing. */
        if (rc == 0 && bw_clock_ns() - g_last_tx_ns >
                (unsigned long long)BW_MOD_NET_KEEPALIVE_MS * 1000000ull) {
            (void)relay_send(BW_MOD_NET_MSG_KEEPALIVE, NULL, 0);
        }
        bw_mutex_unlock(g_lock);
        if (rc < 0) {
            /* the connection is gone: the peers are gone too */
            bw_mutex_lock(g_lock);
            {
                u32 i;

                for (i = 0; i < BW_MOD_NET_PEERS_MAX; i++) {
                    if (g_session.peers[i].present) {
                        u32 peer = g_session.peers[i].number;

                        g_session.peers[i].present = 0;
                        if (queue_put(&g_q_in, peer, (u32)BW_SESSION_LEAVE,
                                      NULL, 0, NULL, 0) != 0) {
                            g_dropped++;
                        }
                    }
                }
            }
            g_session.running = 0;
            if (!g_net_stop &&
                queue_put(&g_q_in, 0, BW_MOD_NET_EV_ENDED, NULL, 0,
                          (const u8 *)"lost", 4u) != 0) {
                g_dropped++;
            }
            g_net_stop = 1;
            bw_mutex_unlock(g_lock);
            break;
        }
        if (rc == 0) {
            net_sleep_ms(BW_MOD_NET_POLL_MS);
        }
    }
    /* the reader ends: the connection goes with it */
    bw_mutex_lock(g_lock);
    if (g_transport.close != NULL) {
        g_transport.close(g_transport.user);
    }
    bw_mutex_unlock(g_lock);
}

/* Stop the session: the reader ends, the transport is closed and the
 * session's state is dropped. */
static void session_stop(void)
{
    BwThread *thread;

    bw_mutex_lock(g_lock);
    g_net_stop = 1;
    thread = g_net_thread;
    g_net_thread = NULL;
    bw_mutex_unlock(g_lock);
    if (thread != NULL) {
        bw_thread_join(thread);
    }
    bw_mutex_lock(g_lock);
    memset(&g_session, 0, sizeof(g_session));
    g_rx.state = 0;
    g_rx.len = 0;
    g_rx.pos = 0;
    bw_mutex_unlock(g_lock);
}

/* The session's calls ---------------------------------------------------- */

/*
 * `session_host`: the port connects to the relay and asks it for a
 * session; the relay answers with a short code, which is written into
 * `code_out`.
 *
 * The call waits for the relay: the services table's call answers with the
 * code, so it cannot return before the relay has spoken.  The wait is
 * bounded (a few seconds), and the connection is dropped when the relay
 * does not answer in time.
 */
int bw_mod_net_session_host(char *code_out, u32 size)
{
    const BwModNetTransport *t;
    char payload[256];
    char reply[BW_MOD_NET_CODE_MAX + 1];
    char token[33];
    unsigned long long deadline;
    int n;
    int rc;

    net_init();
    if (code_out == NULL || size == 0u) {
        return BW_MOD_NET_E_ARG;
    }
    code_out[0] = '\0';
    bw_mutex_lock(g_lock);
    if (g_session.running) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_BUSY;
    }
    bw_mutex_unlock(g_lock);
    /* a reader left over from a session that ended (a dropped connection)
     * goes away before the new one starts */
    session_stop();
    bw_mutex_lock(g_lock);
    if (!g_have_transport) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_RELAY;
    }
    if (g_relay[0] == '\0' || g_relay_port == 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_RELAY;
    }
    t = the_transport();
    if (t->connect == NULL ||
        t->connect(t->user, g_relay, g_relay_port) != 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_IO;
    }
    /* A new session: a new token, kept under the code once the relay has
     * named it. */
    token_make(token);
    n = snprintf(payload, sizeof(payload), "%s %s %s", BW_MOD_NET_PROTO,
                 token, g_player[0] != '\0' ? g_player : "player");
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_ARG;
    }
    if (relay_send(BW_MOD_NET_MSG_HOST, payload, (u32)n) != 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_IO;
    }
    /* wait for the code */
    deadline = bw_clock_ns() +
               (unsigned long long)BW_MOD_NET_TIMEOUT_MS * 1000000ull;
    reply[0] = '\0';
    rc = BW_MOD_NET_E_RELAY;
    for (;;) {
        char type = 0;
        u8 *frame = NULL;
        u32 frame_size = 0;
        int got;

        got = rx_frame(&type, &frame, &frame_size);
        if (got < 0) {
            rc = BW_MOD_NET_E_IO;
            break;
        }
        if (got == 1) {
            if (type == BW_MOD_NET_MSG_CODE) {
                copy_text_n(reply, sizeof(reply), (const char *)frame,
                            frame_size);
                rc = BW_MOD_NET_OK;
                break;
            }
            if (type == BW_MOD_NET_MSG_ERROR) {
                char reason[256];

                copy_text_n(reason, sizeof(reason), (const char *)frame,
                            frame_size);
                set_last_error(reason);
                fprintf(stderr, "eidos: the relay refused the session: %s\n",
                        reason);
                rc = BW_MOD_NET_E_RELAY;
                break;
            }
            relay_frame(type, frame, frame_size);
            continue;
        }
        if (bw_clock_ns() > deadline) {
            rc = BW_MOD_NET_E_RELAY;
            break;
        }
        net_sleep_ms(BW_MOD_NET_POLL_MS);
    }
    if (rc != BW_MOD_NET_OK) {
        if (t->close != NULL) {
            t->close(t->user);
        }
        bw_mutex_unlock(g_lock);
        return rc;
    }
    /* the session is up */
    g_session.running = 1;
    g_session.hosting = 1;
    copy_text(g_session.code, sizeof(g_session.code), reply);
    copy_text(code_out, (size_t)size, reply);
    if (g_session.name[0] == '\0') {
        copy_text(g_session.name, sizeof(g_session.name),
                  g_player[0] != '\0' ? g_player : "player");
    }
    copy_text(g_session.token, sizeof(g_session.token), token);
    {
        char clean[BW_MOD_NET_CODE_MAX + 1];

        code_clean(clean, sizeof(clean), reply);
        token_keep(clean, token);
    }
    g_net_stop = 0;
    /* The read buffer is kept: the frames that came in the same read as the
     * relay's answer (the welcome after the code, the peers after the
     * welcome) are the reader's first. */
    g_net_thread = bw_thread_start(net_reader, NULL);
    bw_mutex_unlock(g_lock);
    return BW_MOD_NET_OK;
}

/*
 * `session_join`: the port connects to the relay and asks it to link this
 * player to the session the code names.
 */
int bw_mod_net_session_join(const char *code)
{
    const BwModNetTransport *t;
    char payload[256];
    char clean[BW_MOD_NET_CODE_MAX + 1];
    char token[33];
    unsigned long long deadline;
    int n;
    int rc;

    net_init();
    if (code == NULL) {
        return BW_MOD_NET_E_ARG;
    }
    code_clean(clean, sizeof(clean), code);
    if (clean[0] == '\0') {
        return BW_MOD_NET_E_ARG;
    }
    bw_mutex_lock(g_lock);
    if (g_session.running) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_BUSY;
    }
    bw_mutex_unlock(g_lock);
    /* a reader left over from a session that ended (a dropped connection)
     * goes away before the new one starts */
    session_stop();
    bw_mutex_lock(g_lock);
    if (!g_have_transport) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_RELAY;
    }
    if (g_relay[0] == '\0' || g_relay_port == 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_RELAY;
    }
    t = the_transport();
    if (t->connect == NULL ||
        t->connect(t->user, g_relay, g_relay_port) != 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_IO;
    }
    /* The token kept for this code: a player who comes back to a code is
     * the same peer, even after the game was restarted. */
    {
        const char *kept = token_find(clean);

        if (kept != NULL) {
            copy_text(token, sizeof(token), kept);
        } else {
            token_make(token);
            token_keep(clean, token);
        }
    }
    n = snprintf(payload, sizeof(payload), "%s %s %s %s", BW_MOD_NET_PROTO,
                 token, clean, g_player[0] != '\0' ? g_player : "player");
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_ARG;
    }
    if (relay_send(BW_MOD_NET_MSG_JOIN, payload, (u32)n) != 0) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_IO;
    }
    /* wait for the welcome: the relay's answer carries our peer number */
    deadline = bw_clock_ns() +
               (unsigned long long)BW_MOD_NET_TIMEOUT_MS * 1000000ull;
    rc = BW_MOD_NET_E_RELAY;
    for (;;) {
        char type = 0;
        u8 *frame = NULL;
        u32 frame_size = 0;
        int got;

        got = rx_frame(&type, &frame, &frame_size);
        if (got < 0) {
            rc = BW_MOD_NET_E_IO;
            break;
        }
        if (got == 1) {
            if (type == BW_MOD_NET_MSG_WELCOME) {
                relay_frame(type, frame, frame_size);
                rc = BW_MOD_NET_OK;
                break;
            }
            if (type == BW_MOD_NET_MSG_ERROR) {
                char reason[256];

                copy_text_n(reason, sizeof(reason), (const char *)frame,
                            frame_size);
                set_last_error(reason);
                fprintf(stderr, "eidos: the relay refused the join: %s\n",
                        reason);
                rc = BW_MOD_NET_E_RELAY;
                break;
            }
            relay_frame(type, frame, frame_size);
            continue;
        }
        if (bw_clock_ns() > deadline) {
            rc = BW_MOD_NET_E_RELAY;
            break;
        }
        net_sleep_ms(BW_MOD_NET_POLL_MS);
    }
    if (rc != BW_MOD_NET_OK) {
        if (t->close != NULL) {
            t->close(t->user);
        }
        bw_mutex_unlock(g_lock);
        return rc;
    }
    g_session.running = 1;
    /* a host whose connection dropped joins its own code again as peer 1,
     * and is the host again */
    g_session.hosting = (g_session.self == 1u);
    copy_text(g_session.code, sizeof(g_session.code), clean);
    if (g_session.name[0] == '\0') {
        copy_text(g_session.name, sizeof(g_session.name),
                  g_player[0] != '\0' ? g_player : "player");
    }
    copy_text(g_session.token, sizeof(g_session.token), token);
    g_net_stop = 0;
    /* The read buffer is kept: the frames that came in the same read as the
     * relay's answer (the welcome after the code, the peers after the
     * welcome) are the reader's first. */
    g_net_thread = bw_thread_start(net_reader, NULL);
    bw_mutex_unlock(g_lock);
    return BW_MOD_NET_OK;
}

/*
 * `session_leave`: the port tells the relay it is going and drops the
 * connection.
 */
void bw_mod_net_session_leave(void)
{
    net_init();
    bw_mutex_lock(g_lock);
    if (!g_session.running) {
        bw_mutex_unlock(g_lock);
        return;
    }
    (void)relay_send(BW_MOD_NET_MSG_BYE, NULL, 0);
    bw_mutex_unlock(g_lock);
    session_stop();
}

/*
 * `session_send`: a message of the calling mod's own bytes.  `peer` is the
 * number of the peer to send it to, or 0 for every peer of the session.
 *
 * The message carries the calling mod's key, so that the mod with the same
 * key on the other side is the one that receives it.
 */
static int send_keyed(u32 peer, const char *key, const void *buf, u32 size);

int bw_mod_net_session_send(u32 peer, const void *buf, u32 size)
{
    return send_keyed(peer, current_namespace(), buf, size);
}

/* The port's own message, under BW_MOD_NET_PORT_KEY. */
int bw_mod_net_port_send(u32 peer, const void *buf, u32 size)
{
    return send_keyed(peer, BW_MOD_NET_PORT_KEY, buf, size);
}

void bw_mod_net_set_port_handler(BwSessionHandler fn)
{
    net_init();
    bw_mutex_lock(g_lock);
    g_port_handler = fn;
    bw_mutex_unlock(g_lock);
}

/* `Q`: the host removes a player from the session. */
int bw_mod_net_session_remove(u32 peer)
{
    char text[16];
    int n;
    int rc;

    net_init();
    if (peer == 0) {
        return BW_MOD_NET_E_ARG;
    }
    bw_mutex_lock(g_lock);
    if (!g_session.running) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_SESSION;
    }
    if (!g_session.hosting) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_ARG;
    }
    n = snprintf(text, sizeof(text), "%u", (unsigned int)peer);
    rc = relay_send(BW_MOD_NET_MSG_REMOVE, text, (u32)n);
    bw_mutex_unlock(g_lock);
    return (rc == 0) ? BW_MOD_NET_OK : BW_MOD_NET_E_IO;
}

const char *bw_mod_net_last_error(void)
{
    return g_last_error;
}

const char *bw_mod_net_last_error_text(void)
{
    const char *t = strchr(g_last_error_text, ' ');

    return t != NULL ? t + 1 : "";
}

/* Whether this player hosts the session. */
int bw_mod_net_session_hosting(void)
{
    return g_session.running && g_session.hosting;
}

static int send_keyed(u32 peer, const char *key, const void *buf, u32 size)
{
    u8 payload[BW_MOD_NET_TX_MAX];
    u32 key_len;
    int n;
    int rc;

    net_init();
    if (buf == NULL && size > 0) {
        return BW_MOD_NET_E_ARG;
    }
    if (size > BW_MOD_NET_MESSAGE_MAX) {
        return BW_MOD_NET_E_TOO_BIG;
    }
    bw_mutex_lock(g_lock);
    if (!g_session.running) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_NO_SESSION;
    }
    key_len = (u32)strlen(key);
    if (key_len == 0) {
        /* a mod with no namespace has no key a peer could match */
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_ARG;
    }
    if (key_len > BW_MOD_NET_KEY_MAX) {
        key_len = BW_MOD_NET_KEY_MAX;
    }
    n = snprintf((char *)payload, sizeof(payload), "%u\n", (unsigned int)peer);
    if (n < 0 || (u32)n + key_len + 1u + size > sizeof(payload)) {
        bw_mutex_unlock(g_lock);
        return BW_MOD_NET_E_ARG;
    }
    memcpy(payload + n, key, key_len);
    payload[n + key_len] = (u8)'\n';
    if (size > 0) {
        memcpy(payload + n + key_len + 1u, buf, size);
    }
    rc = relay_send(BW_MOD_NET_MSG_DATA, payload,
                    (u32)n + key_len + 1u + size);
    bw_mutex_unlock(g_lock);
    return (rc == 0) ? BW_MOD_NET_OK : BW_MOD_NET_E_IO;
}

/*
 * `on_session`: the mod binds the handler that receives the session's
 * events.  The loader binds this call into every mod's table, so the
 * namespace in force is the mod's own, and the handler belongs to it.
 */
void bw_mod_net_on_session(BwSessionHandler fn)
{
    BwModNetMod *m;

    net_init();
    if (fn == NULL) {
        return;
    }
    bw_mutex_lock(g_lock);
    m = mod_for(current_namespace());
    if (m != NULL) {
        m->session = fn;
    }
    bw_mutex_unlock(g_lock);
}

/* ------------------------------------------------------------------ */
/* The external control                                                */
/* ------------------------------------------------------------------ */

/*
 * The control's listener thread: it takes the connections and reads the
 * lines the tools send.  It never calls a handler: the lines go into the
 * queue, and the frame calls the handlers.
 *
 * The thread owns the listener it was started with and closes it when it
 * ends, so that the port's own stop never closes a socket another thread
 * is waiting on.
 */
static void control_reader(void *arg)
{
    u8 buf[1024];
    char line[BW_MOD_NET_COMMAND_MAX + 1];
    u32 line_len = 0;
    BwSock listener = (BwSock)(uintptr_t)arg;
    int i;

    for (;;) {
        int stop;

        bw_mutex_lock(g_lock);
        stop = g_ctl_stop;
        bw_mutex_unlock(g_lock);
        if (stop) {
            break;
        }
        /* a connection waiting? */
        if (sock_wait(listener, 0, 0) == 1) {
            BwSock s = sock_accept_one(listener);
            int slot = -1;

            if (s != BW_SOCK_INVALID) {
                bw_mutex_lock(g_lock);
                for (i = 0; i < (int)BW_MOD_NET_CLIENTS_MAX; i++) {
                    if (!g_client_used[i]) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= 0) {
                    g_clients[slot] = s;
                    g_client_used[slot] = 1;
                    g_client_count++;
                }
                bw_mutex_unlock(g_lock);
                if (slot < 0) {
                    sock_shutdown(s);   /* no room for this tool */
                }
            }
        }
        /* a line waiting? */
        for (i = 0; i < (int)BW_MOD_NET_CLIENTS_MAX; i++) {
            u32 got = 0;
            int rc;
            u32 j;

            bw_mutex_lock(g_lock);
            if (!g_client_used[i]) {
                bw_mutex_unlock(g_lock);
                continue;
            }
            rc = sock_recv_some(g_clients[i], buf, (u32)sizeof(buf), &got);
            if (rc < 0) {
                /* the tool is gone */
                sock_shutdown(g_clients[i]);
                g_clients[i] = BW_SOCK_INVALID;
                g_client_used[i] = 0;
                g_client_count--;
            } else if (rc > 0) {
                for (j = 0; j < got; j++) {
                    u8 c = buf[j];

                    if (c == (u8)'\r') {
                        continue;
                    }
                    if (c == (u8)'\n') {
                        line[line_len] = '\0';
                        if (line_len > 0) {
                            if (queue_put(&g_q_in, 0,
                                          (u32)BW_MOD_NET_EV_COMMAND,
                                          NULL, 0, line, line_len) != 0) {
                                g_dropped++;
                            }
                        }
                        line_len = 0;
                        continue;
                    }
                    if (line_len < BW_MOD_NET_COMMAND_MAX) {
                        line[line_len++] = (char)c;
                    }
                }
            }
            bw_mutex_unlock(g_lock);
        }
        net_sleep_ms(BW_MOD_NET_POLL_MS);
    }
    /* the listener ends: the tools go with it */
    bw_mutex_lock(g_lock);
    for (i = 0; i < (int)BW_MOD_NET_CLIENTS_MAX; i++) {
        if (g_client_used[i]) {
            sock_shutdown(g_clients[i]);
            g_clients[i] = BW_SOCK_INVALID;
            g_client_used[i] = 0;
        }
    }
    g_client_count = 0;
    bw_mutex_unlock(g_lock);
    sock_shutdown(listener);
}

/* Start the control's listener.  0 on success. */
static int control_start(void)
{
    BwSock listener;

    net_init();
    if (g_control_port <= 0) {
        return -1;
    }
    if (g_listener != BW_SOCK_INVALID) {
        return 0;
    }
    listener = sock_listen_loopback((u16)g_control_port);
    if (listener == BW_SOCK_INVALID) {
        fprintf(stderr, "eidos: the external control could not listen on "
                "127.0.0.1:%d (%s)\n", g_control_port, sock_error());
        return -1;
    }
    bw_mutex_lock(g_lock);
    g_listener = listener;
    g_ctl_stop = 0;
    g_ctl_thread = bw_thread_start(control_reader,
                                   (void *)(uintptr_t)listener);
    bw_mutex_unlock(g_lock);
    return 0;
}

/*
 * Stop the control's listener and drop the tools.  The listener socket
 * belongs to the thread that waits on it: the thread closes it when it
 * ends, and the stop waits for the thread.
 */
static void control_stop(void)
{
    BwThread *thread;

    net_init();
    bw_mutex_lock(g_lock);
    g_ctl_stop = 1;
    thread = g_ctl_thread;
    g_ctl_thread = NULL;
    g_listener = BW_SOCK_INVALID;
    bw_mutex_unlock(g_lock);
    if (thread != NULL) {
        bw_thread_join(thread);
    }
}

/*
 * `on_command`: the mod binds the handler that receives the lines a tool
 * sends.  The first mod that binds one starts the listener, and the
 * listener runs only while a mod that uses the service is enabled: the
 * port stops it (bw_mod_net_reset) when the mods go away.
 */
void bw_mod_net_on_command(BwCommandHandler fn)
{
    BwModNetMod *m;

    net_init();
    if (fn == NULL) {
        return;
    }
    bw_mutex_lock(g_lock);
    m = mod_for(current_namespace());
    if (m != NULL) {
        m->command = fn;
    }
    bw_mutex_unlock(g_lock);
    (void)control_start();
}

/*
 * The port's reset while a multiplayer game runs: the mods' handlers go (the
 * mods start again), the session and the port's own handler stay.
 */
void bw_mod_net_reset_mods(void)
{
    u32 i;

    net_init();
    bw_mutex_lock(g_lock);
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        memset(&g_mods[i], 0, sizeof(g_mods[i]));
    }
    bw_mutex_unlock(g_lock);
}

/*
 * Forget the handlers of one mod: the loader calls this when it stops the
 * mod, before the mod's code goes away.
 */
void bw_mod_net_remove_for(const char *ns)
{
    u32 i;

    net_init();
    if (ns == NULL) {
        return;
    }
    bw_mutex_lock(g_lock);
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        if (g_mods[i].used && strcmp(g_mods[i].ns, ns) == 0) {
            memset(&g_mods[i], 0, sizeof(g_mods[i]));
        }
    }
    bw_mutex_unlock(g_lock);
}

/*
 * `publish`: a JSON message for every connected tool.  The line is the
 * topic, a space, and the mod's JSON, and nothing else: a tool reads one
 * line per message.
 */
void bw_mod_net_publish(const char *topic, const char *json)
{
    char line[BW_MOD_NET_TX_MAX];
    int n;
    int i;

    net_init();
    if (topic == NULL) {
        return;
    }
    if (json == NULL) {
        json = "";
    }
    n = snprintf(line, sizeof(line), "%s %s\n", topic, json);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return;
    }
    bw_mutex_lock(g_lock);
    for (i = 0; i < (int)BW_MOD_NET_CLIENTS_MAX; i++) {
        if (!g_client_used[i]) {
            continue;
        }
        if (sock_send_all(g_clients[i], line, (u32)n) != 0) {
            sock_shutdown(g_clients[i]);
            g_clients[i] = BW_SOCK_INVALID;
            g_client_used[i] = 0;
            g_client_count--;
        }
    }
    bw_mutex_unlock(g_lock);
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

/*
 * The frame tick: the port's frame hook calls this once a frame.  The
 * queued session events and commands reach the mods' handlers here, on the
 * game's thread, which is the rule the document fixes ("The handler runs
 * on the game's frame, never on a network thread").
 *
 * The handlers are called without the lock: a handler may call back into
 * the port (a send, a publish), and the lock is not recursive.
 */
void bw_mod_net_frame(void)
{
    BwModNetQueue out;

    net_init();
    bw_mutex_lock(g_lock);
    out = g_q_out;
    g_q_out = g_q_in;
    g_q_in = out;
    bw_mutex_unlock(g_lock);

    for (;;) {
        u32 peer = 0;
        u32 kind = 0;
        u32 size = 0;
        char key[BW_MOD_NET_KEY_MAX + 1];
        const u8 *data = NULL;
        u32 i;

        if (!queue_take(&g_q_out, &peer, &kind, key, BW_MOD_NET_KEY_MAX,
                        &data, &size)) {
            break;
        }
        if (kind == BW_MOD_NET_EV_ENDED) {
            if (g_port_handler != NULL) {
                g_port_handler(peer, (int)kind, (const void *)data, size);
            }
            continue;
        }
        if (kind == (u32)BW_SESSION_MESSAGE &&
            strcmp(key, BW_MOD_NET_PORT_KEY) == 0) {
            if (g_port_handler != NULL) {
                g_port_handler(peer, (int)kind, (const void *)data, size);
            }
            continue;
        }
        if (kind == (u32)BW_SESSION_MESSAGE) {
            /* the message goes to the mod with the same key, and to
             * nobody else */
            BwModNetMod *m = mod_lookup(key);

            if (m != NULL && m->session != NULL) {
                const char *saved = net_enter_mod(m->ns);

                m->session(peer, (int)BW_SESSION_MESSAGE,
                           (const void *)data, size);
                net_leave_mod(saved);
            }
            continue;
        }
        if (kind == (u32)BW_MOD_NET_EV_COMMAND) {
            char line[BW_MOD_NET_COMMAND_MAX + 1];

            copy_text_n(line, sizeof(line), (const char *)data, size);
            for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
                if (g_mods[i].used && g_mods[i].command != NULL) {
                    const char *saved = net_enter_mod(g_mods[i].ns);

                    g_mods[i].command(line);
                    net_leave_mod(saved);
                }
            }
            continue;
        }
        /* a join or a drop: the port and every mod that asked for the
         * session's events are told */
        if (g_port_handler != NULL) {
            g_port_handler(peer, (int)kind, NULL, 0);
        }
        for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
            if (g_mods[i].used && g_mods[i].session != NULL) {
                g_mods[i].session(peer, (int)kind, NULL, 0);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

void bw_mod_net_set_transport(const BwModNetTransport *transport)
{
    net_init();
    bw_mutex_lock(g_lock);
    if (transport == NULL) {
        g_have_transport = 0;
        memset(&g_transport, 0, sizeof(g_transport));
    } else {
        memcpy(&g_transport, transport, sizeof(g_transport));
        g_have_transport = 1;
    }
    bw_mutex_unlock(g_lock);
}

const BwModNetTransport *bw_mod_net_tcp_transport(void)
{
    return &g_tcp_transport;
}

void bw_mod_net_set_relay(const char *host, u16 port)
{
    const char *colon = host != NULL ? strchr(host, ':') : NULL;

    net_init();
    bw_mutex_lock(g_lock);
    /* "tls://relay.example.org": the relay is reached over TLS */
    g_relay_tls = host != NULL && strncmp(host, "tls://", 6) == 0;
    if (g_relay_tls) {
        host += 6;
        colon = strchr(host, ':');
    }
    copy_text(g_relay, sizeof(g_relay), host);
    g_relay_port = port;
    /* "relay.example.org:4141": a port written after the name wins (one
     * colon only, so an IPv6 address is left alone) */
    if (colon != NULL && strchr(colon + 1, ':') == NULL && colon[1] != '\0') {
        long v = strtol(colon + 1, NULL, 10);

        if (v > 0 && v <= 65535) {
            g_relay[colon - host < (ptrdiff_t)sizeof(g_relay)
                    ? colon - host : (ptrdiff_t)sizeof(g_relay) - 1] = '\0';
            g_relay_port = (u16)v;
        }
    }
    bw_mutex_unlock(g_lock);
}

const char *bw_mod_net_relay(void)
{
    return g_relay;
}

u16 bw_mod_net_relay_port(void)
{
    return g_relay_port;
}

void bw_mod_net_set_name(const char *name)
{
    net_init();
    bw_mutex_lock(g_lock);
    copy_text(g_player, sizeof(g_player), name);
    bw_mutex_unlock(g_lock);
}

const char *bw_mod_net_name(void)
{
    return g_player[0] != '\0' ? g_player : "player";
}

/*
 * The port the external control listens on.  The number comes from the
 * port's own settings (engine/platform/settings.h); zero means the
 * control is off, and that is the state the port starts in.
 */
int bw_mod_net_control_set_port(int port)
{
    int restart = 0;

    net_init();
    if (port < 0 || port > 65535) {
        return -1;
    }
    bw_mutex_lock(g_lock);
    if (g_control_port != port) {
        g_control_port = port;
        restart = 1;
    }
    bw_mutex_unlock(g_lock);
    if (restart) {
        control_stop();
        if (port > 0) {
            (void)control_start();
        }
    }
    return 0;
}

int bw_mod_net_control_port(void)
{
    return g_control_port;
}

int bw_mod_net_control_clients(void)
{
    int n;

    net_init();
    bw_mutex_lock(g_lock);
    n = g_client_count;
    bw_mutex_unlock(g_lock);
    return n;
}
u32 bw_mod_net_peer_count(void)
{
    u32 i;
    u32 n = 0;

    net_init();
    bw_mutex_lock(g_lock);
    for (i = 0; i < BW_MOD_NET_PEERS_MAX; i++) {
        if (g_session.peers[i].present) {
            n++;
        }
    }
    bw_mutex_unlock(g_lock);
    return n;
}

int bw_mod_net_peer_name(u32 peer, char *out, u32 out_size)
{
    BwModNetPeer *p;
    int rc = -1;

    net_init();
    bw_mutex_lock(g_lock);
    p = peer_find(peer);
    if (p != NULL && out != NULL && out_size > 0) {
        copy_text(out, (size_t)out_size, p->name);
        rc = 0;
    } else if (peer == g_session.self && out != NULL && out_size > 0) {
        copy_text(out, (size_t)out_size, g_session.name);
        rc = 0;
    }
    bw_mutex_unlock(g_lock);
    return rc;
}

u32 bw_mod_net_dropped(void)
{
    return g_dropped;
}

u32 bw_mod_net_self_peer(void)
{
    u32 peer;

    net_init();
    bw_mutex_lock(g_lock);
    peer = g_session.running ? g_session.self : 0u;
    bw_mutex_unlock(g_lock);
    return peer;
}

int bw_mod_net_session_active(void)
{
    int running;

    net_init();
    bw_mutex_lock(g_lock);
    running = g_session.running;
    bw_mutex_unlock(g_lock);
    return running;
}

/*
 * The reset: the session, the control endpoint, the handlers and the
 * queues go away.  The port calls this at power-on and at every reset
 * (bw_port_reset), and when the player disables the mods.
 */
void bw_mod_net_reset(void)
{
    u32 i;

    net_init();
    control_stop();
    session_stop();
    bw_mutex_lock(g_lock);
    for (i = 0; i < BW_MOD_NET_MODS_MAX; i++) {
        memset(&g_mods[i], 0, sizeof(g_mods[i]));
    }
    g_q_in.head = 0;
    g_q_in.len = 0;
    g_q_out.head = 0;
    g_q_out.len = 0;
    g_rx.state = 0;
    g_rx.len = 0;
    g_rx.pos = 0;
    g_dropped = 0;
    bw_mutex_unlock(g_lock);
}
