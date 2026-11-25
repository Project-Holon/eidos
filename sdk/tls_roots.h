/*
 * sdk/tls_roots.h -- the certificate roots a TLS relay is checked against
 * (sdk/tls_roots.c).
 */

#ifndef BW_PORT_TLS_ROOTS_H
#define BW_PORT_TLS_ROOTS_H

#ifdef __cplusplus
extern "C" {
#endif

extern const char bw_tls_roots_pem[];

/* The roots a download's server is checked against: the relay's, and the
 * ones GitHub's servers answer with. */
extern const char bw_tls_web_roots_pem[];
/* And Mozilla's root list (sdk/tls_mozilla_roots.c, tools/ca_roots.py): the
 * roots a browser trusts for servers, one PEM block a string, so a mod's
 * web request reaches any site with a certificate a browser accepts. */
extern const char *const bw_tls_mozilla_roots[];
extern const unsigned bw_tls_mozilla_root_count;

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_TLS_ROOTS_H */
