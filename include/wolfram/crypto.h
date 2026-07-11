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
 * Derive the `did:key:z...` multibase-encoded public key for a signing key.
 *
 * The returned string uses the same multicodec prefixes the SDK's verifier
 * understands (secp256k1-pub 0xe701, p256-pub 0x8024) and is suitable for
 * use as a PLC rotation/verification key or as the `sig` map key when signing
 * a DID PLC operation.
 *
 * On WF_OK, *out_didkey is heap-allocated and owned by the caller (free() it).
 * secp256k1 support requires HAVE_LIBSECP256K1; otherwise it returns
 * WF_ERR_INVALID_ARG.
 */
wf_status wf_signing_key_public_didkey(const wf_signing_key *key,
                                        char **out_didkey);

/**
 * Encode a raw compressed public key (33 bytes) of the given type into its
 * `did:key:z...` multibase form. Inverse of wf_didkey_decode; complements
 * wf_signing_key_public_didkey (which derives the key from a private scalar).
 *
 * On WF_OK, *out_didkey is heap-allocated and owned by the caller (free()).
 */
wf_status wf_didkey_encode(wf_key_type type, const unsigned char *raw_pub,
                           size_t raw_len, char **out_didkey);

/**
 * Decode a `did:key:z...` or bare multikey `z...` string into its key type and
 * raw 33-byte compressed public point. On WF_OK, *out_raw is heap-allocated
 * and owned by the caller (free()).
 */
wf_status wf_didkey_decode(const char *didkey, wf_key_type *out_type,
                           unsigned char **out_raw, size_t *out_raw_len);

/* Normalize DID verificationMethod material into `did:key:z...` form.
 * Supports Multikey and the legacy EcdsaSecp256k1/P256VerificationKey2019
 * types used by atproto DID documents. The caller frees `*out_didkey`. */
wf_status wf_didkey_from_verification_method(
    const char *verification_type, const char *public_key_multibase,
    char **out_didkey);

/**
 * Compute the verification-method id for a did:key, `${did}#${did}`.
 * On WF_OK, *out_id is heap-allocated and owned by the caller (free()).
 */
wf_status wf_didkey_verification_method_id(const char *didkey, char **out_id);

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
 * Accepts a `did:key:z...` or bare `z...` base58btc multikey. Supports
 * secp256k1-pub (multicodec 0xe7 0x01) and p256-pub (0x80 0x24).
 */
wf_status wf_verify(const char *public_key_multibase,
                      const unsigned char *msg, size_t msg_len,
                      const unsigned char *sig, size_t sig_len);

/**
 * Verify an ECDSA signature while accepting either low-S or high-S form.
 *
 * This exists for AT Protocol service-JWT interoperability, whose reference
 * verifier explicitly permits malleable signatures. Repository, commit, and
 * record verification should continue to use wf_verify so canonical low-S
 * signatures remain required. Inputs and key formats match wf_verify.
 */
wf_status wf_verify_allow_malleable(const char *public_key_multibase,
                                    const unsigned char *msg, size_t msg_len,
                                    const unsigned char *sig, size_t sig_len);

/* ------------------------------------------------------------------ */
/* Generic P-256 / hashing / base64url helpers                         */
/*                                                                     */
/* These are thin, honest wrappers around OpenSSL used by higher-level */
/* protocol code (e.g. OAuth DPoP/Bearer verification) so the SDK never */
/* hand-rolls field arithmetic, digest, or encoding logic.             */
/* ------------------------------------------------------------------ */

/**
 * Compute the SHA-256 digest of `in` (`len` bytes), writing 32 bytes to `out`.
 */
wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
                           unsigned char out[32]);

/**
 * Base64url-decode the NUL-terminated string `in` (RFC 4648, no padding
 * required). On WF_OK, *out points to a heap-allocated buffer of *out_len
 * bytes; the caller frees *out with free().
 */
wf_status wf_crypto_base64url_decode(const char *in,
                                    unsigned char **out, size_t *out_len);

/**
 * Base64url-encode `in` (`len` bytes) without padding. On WF_OK, *out is a
 * heap-allocated NUL-terminated string; the caller frees *out with free().
 */
wf_status wf_crypto_base64url_encode(const unsigned char *in, size_t len,
                                     char **out);

/**
 * Verify an ES256 signature over `msg` (`msg_len` bytes) using the P-256
 * public key with affine coordinates `x` and `y` (32 bytes each). `sig` is
 * the raw 64-byte (r || s) ECDSA signature. Returns WF_OK on a valid
 * signature, WF_ERR_PARSE on an invalid one.
 */
wf_status wf_crypto_p256_verify(const unsigned char x[32],
                                const unsigned char y[32],
                                const unsigned char *msg, size_t msg_len,
                                const unsigned char *sig, size_t sig_len);

/**
 * Parse a public JWK ({"kty":"EC","crv":"P-256","x":"...","y":"..."}) and write
 * its P-256 affine coordinates (32 bytes each) into `x` and `y`.
 */
wf_status wf_crypto_p256_jwk_coords(const char *jwk_json,
                                    unsigned char x[32], unsigned char y[32]);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CRYPTO_H */
