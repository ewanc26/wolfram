/**
 * openssl_compat.h — Minimal OpenSSL API compatibility layer for Wii.
 *
 * When WOLFRAM_WII is defined, this header is included via -I priority
 * in place of the real OpenSSL headers. It provides the type declarations
 * and function signatures that the wolfram source files need, backed by
 * pure-C or mbedTLS implementations in openssl_compat.c.
 *
 * This is NOT a full OpenSSL port — just enough to compile the SDK's
 * non-OAuth modules (atproto_lex, repo/cid, repo/mst, plc).
 */

#ifndef WOLFRAM_OPENSSL_COMPAT_H
#define WOLFRAM_OPENSSL_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── SHA-256 ────────────────────────────────────────────────────────── */

/* SHA256_DIGEST_LENGTH is needed by some consumers. */
#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

/**
 * Compute SHA-256 hash of `data` (len bytes) into `out` (32 bytes).
 * Implemented via mbedTLS in openssl_compat.c.
 */
void SHA256(const unsigned char *data, size_t len, unsigned char *out);

/* ── Base64 encode/decode (EVP) ─────────────────────────────────────── */

/**
 * Base64-encode `in` (len bytes) into `out` which must have at least
 * `4 * ((len + 2) / 3)` bytes of space. Returns the number of bytes
 * written (excluding NUL). Implemented in pure C.
 */
int EVP_EncodeBlock(unsigned char *out, const unsigned char *in, int len);

/**
 * Base64-decode `in` (NUL-terminated, padded) into `out`.
 * Returns the number of bytes written, or -1 on error.
 * Implemented in pure C.
 */
int EVP_DecodeBlock(unsigned char *out, const unsigned char *in, int len);

/* ── Random bytes ───────────────────────────────────────────────────── */

/**
 * Fill `buf` with `len` cryptographically random bytes.
 * Stub: returns 0 (failure) until mbedTLS or /dev/urandom is wired up.
 */
int RAND_bytes(unsigned char *buf, int len);

/* ── EC / ECDSA (stubs) ────────────────────────────────────────────── */

/* Opaque stub types — consumers never dereference these on Wii. */
typedef struct {
    int dummy;
} EC_KEY;
typedef struct {
    int dummy;
} EC_GROUP;
typedef struct {
    int dummy;
} EC_POINT;
typedef struct {
    int dummy;
} ECDSA_SIG;
typedef struct {
    int dummy;
} BIGNUM;

/* ── EVP message digest (SHA-256 streaming) ──────────────────────────── */

/* Opaque: the concrete context wraps an mbedtls_sha256_context and lives in
 * openssl_compat.c. repo/cid's incremental hasher only ever handles the
 * pointer. */
typedef struct wf_evp_md_ctx EVP_MD_CTX;
typedef struct wf_evp_md EVP_MD;

/* Matches OpenSSL's EVP_MAX_MD_SIZE (SHA-512's digest size). The only digest
 * this layer implements is SHA-256, so a 32-byte output buffer would do, but
 * the constant is part of the surface cid.c sizes a buffer with. */
#define EVP_MAX_MD_SIZE 64

EVP_MD_CTX *EVP_MD_CTX_new(void);
void EVP_MD_CTX_free(EVP_MD_CTX *ctx);

/** The SHA-256 digest descriptor. Only SHA-256 is implemented. */
const EVP_MD *EVP_sha256(void);

/** (Re)start a digest. `type` must be EVP_sha256(); `impl` is ignored.
 *  Returns 1 on success, 0 on failure. */
int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, void *impl);

/** Absorb `len` bytes. Returns 1 on success, 0 on failure. */
int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t len);

/** Produce the digest and reset the context. `out_len` receives the digest
 *  length. Returns 1 on success, 0 on failure. */
int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *out,
                       unsigned int *out_len);

typedef struct {
    int dummy;
} EVP_CIPHER_CTX;
typedef struct {
    int dummy;
} EVP_CIPHER;

#define NID_X9_62_prime256v1 415
#define POINT_CONVERSION_COMPRESSED 2

/* Stub constants */
#define EC_KEY_new_by_curve_name(x) NULL
#define EC_KEY_free(x) ((void)0)
#define EC_KEY_get0_group(x) NULL
#define EC_KEY_get0_private_key(x) NULL
#define EC_KEY_set_private_key(x, y) 0
#define EC_KEY_generate_key(x) 0
#define EC_GROUP_get0_order(x) NULL
#define EC_POINT_new(x) NULL
#define EC_POINT_free(x) ((void)0)
#define EC_POINT_mul(w, x, y, z, a, b) 0
#define EC_POINT_point2oct(w, x, y, z, a, b) 0
#define EC_POINT_oct2point(w, x, y, z, a) 0
#define EC_KEY_set_public_key(x, y) 0
#define ECDSA_SIG_new() NULL
#define ECDSA_SIG_free(x) ((void)0)
#define ECDSA_SIG_get0(x, y, z) ((void)0)
#define ECDSA_SIG_set0(x, y, z) 0
#define ECDSA_do_sign(x, y, z) NULL
#define ECDSA_do_verify(x, y, z, a) 0
#define BN_bin2bn(x, y, z) NULL
#define BN_bn2binpad(x, y, z) 0
#define BN_dup(x) NULL
#define BN_free(x) ((void)0)
#define BN_cmp(x, y) 0
#define BN_rshift1(x, y) 0
#define BN_sub(x, y, z) 0

/* ── OpenSSL string memory (stubs) ──────────────────────────────────── */

static inline void OPENSSL_cleanse(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OPENSSL_COMPAT_H */
