/*
 * sdk/mod_web.h -- a mod's web requests, mod API version 7 (sdk/mod_web.c;
 * docs/mod-services.md, "Version 7: the web, the clock, the game's hash and
 * the pop-up").
 */

#ifndef BW_PORT_MOD_WEB_H
#define BW_PORT_MOD_WEB_H

#include "types.h"
#include "net_https.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most requests that wait at once, all mods together. */
#define BW_WEB_WAITING     4u
/* The largest body a request sends, and the largest answer it takes. */
#define BW_WEB_BODY_MAX    (64u * 1024u)
#define BW_WEB_ANSWER_MAX  (4u * 1024u * 1024u)

/* The services table's web_request and web_read (sdk/mod_state.h). */
int bw_mod_web_request(const char *url, const void *body, u32 body_size, const char *content_type,
                       u32 *ticket);
int bw_mod_web_read(void *out, u32 size);

/* The game's frame: each finished request fires `web` (sdk/mod_frame.c). */
void bw_mod_web_frame(void);

/* The requests made and not yet answered. */
u32 bw_mod_web_waiting(void);

/* The stream the requests use (tests); NULL: the game's own TLS stream. */
void bw_mod_web_set_stream(const BwNetStream *stream);
const BwNetStream *bw_mod_web_stream(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_WEB_H */
