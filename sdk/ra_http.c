/*
 * sdk/ra_http.c -- the RetroAchievements client's HTTPS request
 * (sdk/ra_http.h).  One connection per request, `Connection: close`, the
 * answer read to the end; the server's Content-Length and chunked bodies
 * are both handled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_net.h"
#include "ra_http.h"

#ifdef EIDOS_HAVE_TLS

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "ra_roots.h"
#include "tls_roots.h"

#define MAX_RESPONSE (8u * 1024u * 1024u)

static void fail(BwRaHttpResult *out, const char *what, int rc)
{
    char why[100];

    if (rc != 0) {
        mbedtls_strerror(rc, why, sizeof(why));
        snprintf(out->error, sizeof(out->error), "%s: %s", what, why);
    } else {
        snprintf(out->error, sizeof(out->error), "%s", what);
    }
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int rc = bw_net_conn_send((BwNetConn *)ctx, buf, (int)len);

    return rc < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : rc;
}

static int bio_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t ms)
{
    int rc = bw_net_conn_recv((BwNetConn *)ctx, buf, (int)len, ms > 0 ? (int)ms : 1);

    if (rc == -2) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
    return rc < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : rc;
}

/* "https://host[:port]/path" -> host, port, path. */
static int split_url(const char *url, char *host, size_t hn, char *port,
                     size_t pn, const char **path)
{
    const char *p, *e, *colon;
    size_t len;

    if (strncmp(url, "https://", 8) != 0) {
        return -1;
    }
    p = url + 8;
    e = p;
    while (*e != '\0' && *e != '/' && *e != '?') {
        e++;
    }
    colon = memchr(p, ':', (size_t)(e - p));
    len = (size_t)((colon != NULL ? colon : e) - p);
    if (len == 0 || len >= hn) {
        return -1;
    }
    memcpy(host, p, len);
    host[len] = '\0';
    if (colon != NULL) {
        size_t pl = (size_t)(e - colon - 1);

        if (pl == 0 || pl >= pn) {
            return -1;
        }
        memcpy(port, colon + 1, pl);
        port[pl] = '\0';
    } else {
        snprintf(port, pn, "443");
    }
    *path = (*e == '\0') ? "/" : e;
    return 0;
}

/* The decoded chunked body of `in` (length n), in a new buffer. */
static char *unchunk(const char *in, size_t n, size_t *olen)
{
    char *out = (char *)malloc(n + 1);
    size_t o = 0;
    const char *p = in, *end = in + n;

    if (out == NULL) {
        return NULL;
    }
    while (p < end) {
        char *next;
        unsigned long sz = strtoul(p, &next, 16);

        if (next == p) {
            break;
        }
        while (next < end && *next != '\n') {
            next++;
        }
        if (next >= end) {
            break;
        }
        p = next + 1;
        if (sz == 0 || sz > (unsigned long)(end - p)) {
            if (sz != 0) {          /* cut short: keep what came */
                sz = (unsigned long)(end - p);
            } else {
                break;
            }
        }
        memcpy(out + o, p, sz);
        o += sz;
        p += sz;
        if (p < end && *p == '\r') {
            p++;
        }
        if (p < end && *p == '\n') {
            p++;
        }
    }
    out[o] = '\0';
    *olen = o;
    return out;
}

static int ci_has(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);

    for (; *hay != '\0'; hay++) {
        size_t i = 0;

        while (i < nl && hay[i] != '\0' &&
               ((hay[i] | 0x20) == (needle[i] | 0x20))) {
            i++;
        }
        if (i == nl) {
            return 1;
        }
    }
    return 0;
}

int bw_ra_http_request(const char *url, const char *user_agent,
                       const char *content_type, const char *body,
                       size_t body_len, int timeout_ms, BwRaHttpResult *out)
{
    BwNetConn *net = NULL;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    char host[128], port[8], *req = NULL, *resp = NULL, *hdr_end, *payload;
    const char *path, *extra;
    size_t req_cap, resp_len = 0, resp_cap = 0, sent = 0;
    int rc, result = -1, chunked;

    memset(out, 0, sizeof(*out));
    if (split_url(url, host, sizeof(host), port, sizeof(port), &path) != 0) {
        fail(out, "the address is not an https:// one", 0);
        return -1;
    }
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&ca);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    if (psa_crypto_init() != PSA_SUCCESS) {
        fail(out, "the crypto library did not start", 0);
        goto done;
    }
    if ((rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char *)"eidos-ra", 8)) != 0) {
        fail(out, "no random source", rc);
        goto done;
    }
    if ((rc = mbedtls_x509_crt_parse(&ca, (const unsigned char *)bw_ra_roots_pem,
                                     strlen(bw_ra_roots_pem) + 1)) != 0 ||
        (rc = mbedtls_x509_crt_parse(&ca, (const unsigned char *)bw_tls_roots_pem,
                                     strlen(bw_tls_roots_pem) + 1)) != 0) {
        fail(out, "the built-in roots", rc);
        goto done;
    }
    extra = getenv("EIDOS_TLS_CA");
    if (extra != NULL && extra[0] != '\0' &&
        (rc = mbedtls_x509_crt_parse_file(&ca, extra)) != 0) {
        fail(out, "EIDOS_TLS_CA", rc);
        goto done;
    }
    if ((rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        fail(out, "the TLS settings", rc);
        goto done;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_read_timeout(&conf, (uint32_t)timeout_ms);
    if ((rc = mbedtls_ssl_setup(&ssl, &conf)) != 0 ||
        (rc = mbedtls_ssl_set_hostname(&ssl, host)) != 0) {
        fail(out, "the TLS session", rc);
        goto done;
    }
    net = bw_net_conn_open(host, (unsigned short)atoi(port));
    if (net == NULL) {
        fail(out, "could not reach the server", 0);
        goto done;
    }
    mbedtls_ssl_set_bio(&ssl, net, bio_send, NULL, bio_recv_timeout);
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);

            if (flags != 0 && flags != 0xFFFFFFFFu) {
                char vb[100];

                mbedtls_x509_crt_verify_info(vb, sizeof(vb), "", flags);
                snprintf(out->error, sizeof(out->error),
                         "the server's certificate was refused:%s", vb);
            } else {
                fail(out, "the TLS handshake", rc);
            }
            goto done;
        }
    }

    req_cap = 512 + strlen(path) + strlen(host) + body_len +
              (user_agent != NULL ? strlen(user_agent) : 0) +
              (content_type != NULL ? strlen(content_type) : 0);
    req = (char *)malloc(req_cap);
    if (req == NULL) {
        fail(out, "out of memory", 0);
        goto done;
    }
    {
        int n = snprintf(req, req_cap,
                         "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
                         "Accept: */*\r\nConnection: close\r\n",
                         body != NULL ? "POST" : "GET", path, host,
                         user_agent != NULL ? user_agent : "eidos");

        if (body != NULL) {
            n += snprintf(req + n, req_cap - (size_t)n,
                          "Content-Type: %s\r\nContent-Length: %lu\r\n",
                          content_type != NULL ? content_type
                                               : "application/x-www-form-urlencoded",
                          (unsigned long)body_len);
        }
        n += snprintf(req + n, req_cap - (size_t)n, "\r\n");
        if (body != NULL) {
            memcpy(req + n, body, body_len);
            n += (int)body_len;
        }
        req_cap = (size_t)n;
    }
    while (sent < req_cap) {
        rc = mbedtls_ssl_write(&ssl, (const unsigned char *)req + sent, req_cap - sent);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (rc < 0) {
            fail(out, "the request was not sent", rc);
            goto done;
        }
        sent += (size_t)rc;
    }
    for (;;) {
        if (resp_len + 4097 > resp_cap) {
            char *bigger;

            resp_cap = resp_cap == 0 ? 16384 : resp_cap * 2;
            if (resp_cap > MAX_RESPONSE + 8192) {
                fail(out, "the answer is too large", 0);
                goto done;
            }
            bigger = (char *)realloc(resp, resp_cap);
            if (bigger == NULL) {
                fail(out, "out of memory", 0);
                goto done;
            }
            resp = bigger;
        }
        rc = mbedtls_ssl_read(&ssl, (unsigned char *)resp + resp_len, 4096);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == 0) {
            break;
        }
        if (rc == MBEDTLS_ERR_SSL_TIMEOUT) {
            fail(out, "the server took too long", 0);
            goto done;
        }
        if (rc < 0) {
            if (resp_len > 0) {         /* closed without the alert */
                break;
            }
            fail(out, "the answer was not read", rc);
            goto done;
        }
        resp_len += (size_t)rc;
    }
    if (resp == NULL) {
        fail(out, "no answer", 0);
        goto done;
    }
    resp[resp_len] = '\0';
    hdr_end = strstr(resp, "\r\n\r\n");
    if (strncmp(resp, "HTTP/1.", 7) != 0 || hdr_end == NULL) {
        fail(out, "the answer is not HTTP", 0);
        goto done;
    }
    out->status = atoi(resp + 9);
    *hdr_end = '\0';
    chunked = ci_has(resp, "transfer-encoding: chunked");
    payload = hdr_end + 4;
    {
        size_t plen = resp_len - (size_t)(payload - resp);

        if (chunked) {
            out->body = unchunk(payload, plen, &out->body_len);
        } else {
            out->body = (char *)malloc(plen + 1);
            if (out->body != NULL) {
                memcpy(out->body, payload, plen);
                out->body[plen] = '\0';
                out->body_len = plen;
            }
        }
    }
    if (out->body == NULL) {
        fail(out, "out of memory", 0);
        goto done;
    }
    result = 0;
done:
    free(req);
    free(resp);
    if (net != NULL) {
        mbedtls_ssl_close_notify(&ssl);
    }
    bw_net_conn_close(net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&ca);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return result;
}

#else /* !EIDOS_HAVE_TLS */

int bw_ra_http_request(const char *url, const char *user_agent,
                       const char *content_type, const char *body,
                       size_t body_len, int timeout_ms, BwRaHttpResult *out)
{
    (void)url; (void)user_agent; (void)content_type; (void)body;
    (void)body_len; (void)timeout_ms;
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error),
             "this build has no TLS (run tools/fetch_mbedtls.py)");
    return -1;
}

#endif
