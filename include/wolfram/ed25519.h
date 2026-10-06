/*
 * ed25519.h -- Ed25519 signature verification (RFC 8032, pure Ed25519).
 *
 * Verification only: Wolfram never holds a signing key. It exists so a client
 * can check a detached signature on a release manifest against a public key
 * compiled in (docs/update.md), on every target including the consoles, with no
 * OpenSSL or mbedTLS behind it. It is C99 (fixed-width integers), so it is not
 * part of the Mac OS 9 C89 target; that client verifies through its bridge.
 *
 * The implementation is a compact constant-time-ish port of the TweetNaCl
 * algorithms plus a SHA-512. It is not hardened against timing or fault
 * attacks beyond that, which does not matter for verifying a public signature.
 */

#ifndef WOLFRAM_ED25519_H
#define WOLFRAM_ED25519_H

#include "wolfram/xrpc.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_ED25519_PUBLIC_LEN 32
#define WF_ED25519_SIGNATURE_LEN 64

/*
 * Check that `sig` is a valid Ed25519 signature of `msg` (`len` bytes) under
 * the public key `pk`. WF_OK if it is; WF_ERR_VALIDATION if the signature does
 * not verify, is not canonical (S not below the group order) or the public key
 * is not a valid point; WF_ERR_INVALID_ARG for a NULL argument.
 */
wf_status wf_ed25519_verify(const unsigned char sig[WF_ED25519_SIGNATURE_LEN],
                            const void *msg, size_t len,
                            const unsigned char pk[WF_ED25519_PUBLIC_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ED25519_H */
