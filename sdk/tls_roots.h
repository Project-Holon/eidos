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

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_TLS_ROOTS_H */
