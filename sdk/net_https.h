/*
 * sdk/net_https.h -- a file from an https address (sdk/net_https.c).
 *
 * The game's own mod manager reads the mods index and downloads packages
 * with it (sdk/mod_catalog.c).  One call gets one address: it follows the
 * redirects GitHub's release downloads answer with, takes the body in any
 * of the three ways a server may send it (a stated length, chunks, or until
 * the server closes) and hands the bytes to the caller's sink as they come,
 * so a package goes to a file and is never held in memory.
 *
 * The call waits on the network, so it is made from a worker thread, never
 * from the game's frame.
 *
 * The bytes travel over a stream the caller names.  The game's own is a
 * TLS connection whose certificate is checked against the roots in
 * sdk/tls_roots.c (bw_net_tls_stream, sdk/mod_net.c); the tests put a
 * stream of canned answers in its place.
 */

#ifndef BW_PORT_NET_HTTPS_H
#define BW_PORT_NET_HTTPS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The longest address taken, a redirect's included (GitHub's signed
 * download addresses are about 900 characters). */
#define BW_HTTPS_URL_MAX 2048u

/* How many redirects one download follows. */
#define BW_HTTPS_REDIRECTS_MAX 5

/* The largest header block an answer may have. */
#define BW_HTTPS_HEAD_MAX (16u * 1024u)

/* How long the server may send nothing before the download gives up. */
#define BW_HTTPS_IDLE_MS 20000

/*
 * A two-way stream to a server: one connection in use at a time.  A
 * connection closed with `keep` may be kept open and given again by the
 * next open of the same server: github.com drops some of the connections
 * one address makes in quick succession, and a list of mods asks it one
 * file after the other.
 */
typedef struct BwNetStream {
    void *user;

    /* Connect to `host`:`port` and make the stream ready; with `fresh` 0 a
     * kept connection to that server may be given instead.  0: a new
     * connection.  1: a kept one (the server may have closed it meanwhile:
     * the caller then opens again with `fresh`).  -1: failed, with a short
     * reason in `err`. */
    int  (*open)(void *user, const char *host, u16 port, int fresh, char *err, size_t err_size);

    /* Write `size` bytes.  0, or -1. */
    int  (*send)(void *user, const void *buf, u32 size);

    /* Read up to `size` bytes, waiting `timeout_ms` at most: the number
     * read, 0 when the server closed the stream, -1 when it broke, -2 when
     * nothing came in time. */
    int  (*recv)(void *user, void *buf, u32 size, int timeout_ms);

    /* The connection in use is done with (also after a failed open).
     * `keep`: the answer was read whole and the server keeps the
     * connection open, so it may be kept for the next open. */
    void (*close)(void *user, int keep);

    /* Close the kept connections.  May be NULL. */
    void (*idle)(void *user);
} BwNetStream;

/* One download: what the caller sets, and what the call answers. */
typedef struct BwHttpsGet {
    /* The stream; NULL for the game's own (bw_net_tls_stream). */
    const BwNetStream *stream;

    /* The largest body taken, in bytes.  A longer one stops the download. */
    unsigned long long max;

    /* The body's bytes, in order.  Non-zero stops the download. */
    int  (*sink)(void *user, const void *data, u32 size);
    void *user;

    /* Set to non-zero by another thread to stop the download.  May be
     * NULL. */
    const volatile int *cancel;

    /* Called as the body comes: the bytes so far and the stated length (0
     * when the server gave none).  May be NULL. */
    void (*progress)(void *user, unsigned long long got, unsigned long long total);

    /* A request body: with `body` not NULL the request is a POST of
     * `body_size` bytes with this Content-Type (no CR or LF in it).  A
     * 301, 302 or 303 redirect goes on as a GET with no body, as browsers
     * do; 307 and 308 send the body again. */
    const void *body;
    u32 body_size;
    const char *content_type;

    /* Non-zero: the body of an answer other than 200 (and other than a
     * redirect) reaches the sink too, and the call answers 0 with the
     * status in `status` (a web service answers its errors with a body). */
    int any_status;

    /* Out: the last answer's HTTP status (0: none came), and one line that
     * says what went wrong. */
    int  status;
    char err[200];
} BwHttpsGet;

/*
 * Get `url` (https only), or post to it (g->body).  0 when the whole body
 * reached the sink; -1 otherwise, with the reason in g->err.
 */
int bw_https_get(const char *url, BwHttpsGet *g);

/* The downloads are over for now: the stream's kept connections are closed
 * (`stream` NULL: the game's own). */
void bw_https_idle(const BwNetStream *stream);

/*
 * Split an https address: the host (lower case), the port (443 when none
 * is written) and the path with its query ("/" when none).  0, or -1 when
 * the address is not https, is malformed or does not fit.
 */
int bw_https_split(const char *url, char *host, size_t host_size, u16 *port,
                   char *path, size_t path_size);

/* The game's own stream (sdk/mod_net.c): TLS 1.2 or newer, the server's
 * certificate checked against bw_tls_web_roots_pem.  In a build without
 * TLS its open fails and says so. */
const BwNetStream *bw_net_tls_stream(void);

/* A worker's wait: the calling thread sleeps `ms` milliseconds and the
 * game goes on meanwhile, in the wasm32 build too, whose threads take
 * turns (sdk/mod_net.c). */
void bw_net_sleep_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_NET_HTTPS_H */
