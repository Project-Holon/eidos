/*
 * sdk/ra_http.h -- the one HTTPS request the RetroAchievements client
 * makes (docs/retroachievements.md, "The connection").
 *
 * Blocking: it is called from the RA worker thread (sdk/ra.c), never from
 * the game's frame.  HTTPS only, TLS 1.2 or 1.3 through Mbed TLS, the
 * server's certificate checked against sdk/ra_roots.c, sdk/tls_roots.c and
 * the PEM file EIDOS_TLS_CA names.  Without Mbed TLS (EIDOS_HAVE_TLS) every
 * call fails with a sentence.
 */

#ifndef BW_PORT_RA_HTTP_H
#define BW_PORT_RA_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BwRaHttpResult {
    int    status;          /* the HTTP status, 0 when none came          */
    char  *body;            /* NUL-terminated, malloc'd; free with free() */
    size_t body_len;
    char   error[160];      /* a sentence, empty on success               */
} BwRaHttpResult;

/*
 * POST `body` (or GET when `body` is NULL) to `url`, an https:// URL.
 * `user_agent` and `content_type` may be NULL.  Returns 0 when a response
 * was read (whatever its status), -1 otherwise with `out->error` set.
 */
int bw_ra_http_request(const char *url, const char *user_agent,
                       const char *content_type, const char *body,
                       size_t body_len, int timeout_ms, BwRaHttpResult *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_RA_HTTP_H */
