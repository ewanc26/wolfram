#ifndef WOLFRAM_CRYPTO_INTERNAL_H
#define WOLFRAM_CRYPTO_INTERNAL_H

/* P-256 EVP_PKEY helpers shared across modules that need raw-scalar/raw-point
 * key construction and low-S-normalized raw (r||s) ECDSA sign/verify, so
 * this logic exists in exactly one place rather than being reimplemented
 * per caller (see crypto.c and session/oauth/dpop.c). Not part of the public
 * API -- declared here, not under include/wolfram/. */

#include <openssl/evp.h>

#include "wolfram/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build a full P-256 keypair EVP_PKEY (private + its derived public point)
 * from a raw 32-byte private scalar. Does not validate the scalar is in
 * range for the curve order -- callers importing untrusted scalars must
 * check that themselves before calling this. */
wf_status wf_p256_pkey_from_private(const unsigned char priv_bytes[32],
                                    EVP_PKEY **out);

/* Build a public-only P-256 EVP_PKEY from a SEC1 octet-encoded point
 * (compressed 33 bytes or uncompressed 65 bytes, as produced by
 * EC_POINT_point2oct / consumed by EC_POINT_oct2point). */
wf_status wf_p256_pkey_from_public_point(const unsigned char *pub_oct,
                                         size_t pub_oct_len, EVP_PKEY **out);

/* Sign a precomputed 32-byte hash with `pkey` (P-256), producing a raw
 * (r||s) 64-byte signature with S normalized to the lower half of the curve
 * order (low-S). */
wf_status wf_p256_sign_hash(EVP_PKEY *pkey, const unsigned char hash[32],
                            unsigned char sig_out[64]);

/* Verify a raw (r||s) 64-byte P-256 signature over a precomputed 32-byte
 * hash. Rejects a non-low-S signature unless `allow_malleable`. */
wf_status wf_p256_verify_hash_pkey(EVP_PKEY *pkey, const unsigned char hash[32],
                                   const unsigned char sig[64],
                                   int allow_malleable);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CRYPTO_INTERNAL_H */
