/**
 * wii_tls.h — Thin TLS wrapper for the Nintendo Wii transport.
 *
 * Wraps mbedTLS (cross-compiled as a Wii portlib) over a lwIP TCP socket so
 * the rest of the SDK can open HTTPS connections without knowing about
 * entropy sources, CA stores, or the TLS record layer. One global init seeds
 * the RNG and installs the bundled root-CA store; per-connection opens do DNS
 * (lwIP), TCP connect, and the mbedTLS handshake with certificate verification.
 */

#ifndef WOLFRAM_WII_TLS_H
#define WOLFRAM_WII_TLS_H

#include <stddef.h>
#include <stdint.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque TLS connection handle. */
typedef struct wii_tls_conn wii_tls_conn;

/**
 * One-time global initialisation. Seeds the DRBG/entropy backend, parses the
 * bundled root-CA PEM into the trust store, and configures a verifying TLS
 * client profile. Idempotent; safe to call repeatedly. Returns WF_OK, or
 * WF_ERR_CRYPTO / WF_ERR_ALLOC on setup failure.
 *
 * The caller is responsible for net_init() (libogc) before any connect.
 */
wf_status wii_tls_global_init(void);

/**
 * Open a verified TLS connection to `host` on `port` (typically 443).
 * Performs DNS resolution, TCP connect, and the mbedTLS handshake with CA
 * verification and SNI. Returns a connection handle, or NULL on failure
 * (DNS error, connect failure, TLS error, or untrusted certificate).
 */
wii_tls_conn *wii_tls_connect(const char *host, uint16_t port);

/**
 * Read up to `cap` bytes into `buf`. Returns the number of bytes read (>=0),
 * 0 at a clean EOF, or a negative wf_status on transport error.
 */
long wii_tls_recv(wii_tls_conn *c, void *buf, size_t cap);

/**
 * Send exactly `len` bytes from `buf`. Returns the number of bytes sent (==len
 * on success) or a negative wf_status on error.
 */
long wii_tls_send(wii_tls_conn *c, const void *buf, size_t len);

/** Close and free a connection (issues TLS close_notify first). */
void wii_tls_close(wii_tls_conn *c);

/**
 * Append a NUL-terminated PEM certificate to the trusted CA store. Call before
 * connecting when a PDS presents a certificate chain rooted at an authority not
 * in the bundle. Returns WF_OK or WF_ERR_PARSE if the PEM fails to parse.
 */
wf_status wii_tls_add_ca_pem(const char *pem);

/**
 * mbedTLS-compatible RNG callback backed by the global DRBG. Used by the crypto
 * module for P-256 signing/key generation. Returns 0 on success.
 */
int wii_tls_random(void *p, unsigned char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_WII_TLS_H */
