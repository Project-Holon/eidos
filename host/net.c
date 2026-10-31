/*
 * host/net.c -- TCP sockets for the module (sdk/wasm_host.h, sock_*;
 * sdk/mod_net.c's wasm32 part).
 *
 * The module names a socket by a small number, an index into this table.
 * Every socket is non-blocking: the module's threads are fibers of one host
 * thread, so the module waits by asking again between turns, never here.
 */

#include <stdio.h>
#include <string.h>

#include "host.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NativeSock;
#define NS_INVALID INVALID_SOCKET
#define ns_close closesocket
static int ns_would_block(void)
{
    int e = WSAGetLastError();

    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NativeSock;
#define NS_INVALID (-1)
#define ns_close close
static int ns_would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}
#endif

#define SOCKS_MAX 64

static NativeSock g_sock[SOCKS_MAX];
static int g_used[SOCKS_MAX];
static int g_started;

static int start(void)
{
#if defined(_WIN32)
    if (!g_started) {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return -1;
        }
    }
#endif
    g_started = 1;
    return 0;
}

static int nonblocking(NativeSock s)
{
#if defined(_WIN32)
    u_long on = 1;

    return ioctlsocket(s, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int f = fcntl(s, F_GETFL, 0);

    return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static int slot_for(NativeSock s)
{
    int i;

    for (i = 0; i < SOCKS_MAX; i++) {
        if (!g_used[i]) {
            g_used[i] = 1;
            g_sock[i] = s;
            return i;
        }
    }
    ns_close(s);
    return -1;
}

static int valid(u32 s)
{
    return s < SOCKS_MAX && g_used[s];
}

u32 w2c_eidos_sock_open(struct w2c_eidos *e)
{
    NativeSock s;

    (void)e;
    if (start() != 0) {
        return (u32)-1;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == NS_INVALID) {
        return (u32)-1;
    }
    if (nonblocking(s) != 0) {
        ns_close(s);
        return (u32)-1;
    }
    return (u32)slot_for(s);
}

u32 w2c_eidos_sock_connect(struct w2c_eidos *e, u32 s, u32 addr, u32 port)
{
    struct sockaddr_in a;

    (void)e;
    if (!valid(s)) {
        return (u32)-1;
    }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = addr;           /* network order already */
    a.sin_port = htons((u16)port);
    if (connect(g_sock[s], (const struct sockaddr *)&a, sizeof a) == 0) {
        return 0;
    }
    return ns_would_block() ? 1 : (u32)-1;
}

u32 w2c_eidos_sock_poll(struct w2c_eidos *e, u32 s, u32 for_write)
{
    int rc;

    (void)e;
    if (!valid(s)) {
        return (u32)-1;
    }
#if defined(_WIN32)
    {
        WSAPOLLFD p;

        p.fd = g_sock[s];
        p.events = for_write ? POLLWRNORM : POLLRDNORM;
        p.revents = 0;
        rc = WSAPoll(&p, 1, 0);
        if (rc > 0 && (p.revents & (POLLERR | POLLNVAL)) && !(p.revents & (POLLWRNORM | POLLRDNORM | POLLHUP))) {
            return (u32)-1;
        }
    }
#else
    {
        struct pollfd p;

        p.fd = g_sock[s];
        p.events = for_write ? POLLOUT : POLLIN;
        p.revents = 0;
        rc = poll(&p, 1, 0);
        if (rc > 0 && (p.revents & (POLLERR | POLLNVAL)) && !(p.revents & (POLLOUT | POLLIN | POLLHUP))) {
            return (u32)-1;
        }
    }
#endif
    return rc > 0 ? 1 : rc == 0 ? 0 : (u32)-1;
}

u32 w2c_eidos_sock_error(struct w2c_eidos *e, u32 s)
{
    int err = 0;
#if defined(_WIN32)
    int len = (int)sizeof err;
#else
    socklen_t len = (socklen_t)sizeof err;
#endif

    (void)e;
    if (!valid(s)) {
        return 1;
    }
    if (getsockopt(g_sock[s], SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0) {
        return 1;
    }
    return (u32)err;
}

u32 w2c_eidos_sock_send(struct w2c_eidos *e, u32 s, u32 buf, u32 n)
{
    const char *p = (const char *)host_ptr(buf, n);
    long rc;

    (void)e;
    if (!valid(s) || p == NULL) {
        return (u32)-1;
    }
#if defined(_WIN32)
    rc = send(g_sock[s], p, (int)n, 0);
#else
    rc = (long)send(g_sock[s], p, n, MSG_NOSIGNAL);
#endif
    if (rc < 0) {
        return ns_would_block() ? (u32)-2 : (u32)-1;
    }
    return (u32)rc;
}

u32 w2c_eidos_sock_recv(struct w2c_eidos *e, u32 s, u32 buf, u32 n)
{
    char *p = (char *)host_ptr(buf, n);
    long rc;

    (void)e;
    if (!valid(s) || p == NULL) {
        return (u32)-1;
    }
#if defined(_WIN32)
    rc = recv(g_sock[s], p, (int)n, 0);
#else
    rc = (long)recv(g_sock[s], p, n, 0);
#endif
    if (rc < 0) {
        return ns_would_block() ? (u32)-2 : (u32)-1;
    }
    return (u32)rc;
}

void w2c_eidos_sock_close(struct w2c_eidos *e, u32 s)
{
    (void)e;
    if (valid(s)) {
        ns_close(g_sock[s]);
        g_used[s] = 0;
    }
}

/* Bind to 127.0.0.1:port and listen: the external control's listener,
 * which sdk/mod_net.c keeps on the loopback address. */
u32 w2c_eidos_sock_listen_loopback(struct w2c_eidos *e, u32 s, u32 port)
{
    struct sockaddr_in a;
    int on = 1;

    (void)e;
    if (!valid(s)) {
        return (u32)-1;
    }
    /* Windows' SO_REUSEADDR lets a second socket bind a port another one
     * listens on (a tool then reaches that one), and even the default bind
     * shares a port another program holds on the wildcard address: there
     * the port is taken for this socket alone (SO_EXCLUSIVEADDRUSE), so the
     * bind fails on a port in use. */
#ifndef _WIN32
    (void)setsockopt(g_sock[s], SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
#else
    (void)setsockopt(g_sock[s], SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&on, sizeof on);
#endif
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((u16)port);
    if (bind(g_sock[s], (const struct sockaddr *)&a, sizeof a) != 0
            || listen(g_sock[s], 8) != 0) {
        return (u32)-1;
    }
    return 0;
}

u32 w2c_eidos_sock_accept(struct w2c_eidos *e, u32 s)
{
    NativeSock c;

    (void)e;
    if (!valid(s)) {
        return (u32)-1;
    }
    c = accept(g_sock[s], NULL, NULL);
    if (c == NS_INVALID) {
        return (u32)-1;
    }
    if (nonblocking(c) != 0) {
        ns_close(c);
        return (u32)-1;
    }
    return (u32)slot_for(c);
}

/* The first IPv4 address of a host name, network order. */
u32 w2c_eidos_sock_resolve(struct w2c_eidos *e, u32 name, u32 out)
{
    const char *n = (const char *)host_ptr(name, 1);
    struct addrinfo hints, *res = NULL;
    u32 *o = (u32 *)host_ptr(out, 4);
    int rc;

    (void)e;
    if (n == NULL || o == NULL || start() != 0) {
        return (u32)-1;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(n, NULL, &hints, &res);
    if (rc != 0 || res == NULL) {
        return (u32)-1;
    }
    memcpy(o, &((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr, 4);
    freeaddrinfo(res);
    return 0;
}
