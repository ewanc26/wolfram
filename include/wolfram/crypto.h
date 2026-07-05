/**
 * crypto.h — signing and verification primitives.
 *
 * AT Protocol repos are signed with secp256k1 (k256) or P-256, over
 * the DAG-CBOR encoding of a commit. This module scaffolds the API
 * surface; implementations will wrap a real crypto library (likely
 * libsecp256k1 for k256, and OpenSSL/LibreSSL for P-256) rather than
 * rolling our own field arithmetic.
 */

#ifndef WOLFRAM_CRYPTO_H
#define WOLFRAM_CRYPTO_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_key_type {
    WF_KEY_TYPE_UNKNOWN = 0,
    WF_KEY_TYPE_SECP256K1,
    WF_KEY_TYPE_P256,
} wf_key_type;

/** An unencrypted private signing key held in memory. */
typedef struct wf_signing_key {
    wf_key_type type;
    unsigned char bytes[32]; /* raw scalar; format depends on `type` */
} wf_signing_key;

/**
 * Generate a new signing key of the given type.
 */
wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out);

/**
 * Sign a message (typically the SHA-256 digest of a DAG-CBOR block)
 * and write the raw signature bytes into `sig_out`, which must be at
 * least 64 bytes.
 */
wf_status wf_sign(const wf_signing_key *key,
                   const unsigned char *msg, size_t msg_len,
                   unsigned char *sig_out, size_t sig_out_cap);

/**
 * Verify a signature against a did:key- or multibase-encoded public
 * key.
 *
 * Supports secp256k1 (multicodec 0xe701, raw compressed/uncompressed)
 * and P-256 (multicodec 0x1200/0x1201).
 */
wf_status wf_verify(const char *public_key_multibase,
                     const unsigned char *msg, size_t msg_len,
                     const unsigned char *sig, size_t sig_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CRYPTO_H */
