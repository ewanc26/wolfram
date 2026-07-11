/**
 * crypto_wii.c — Wii stub implementation of the crypto module.
 *
 * SHA-256 and base64url are platform-independent (pure C) and are
 * implemented here. P-256 and secp256k1 operations return
 * WF_ERR_NOT_IMPLEMENTED until mbedTLS is linked.
 */

#include "wolfram/crypto.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ── SHA-256 (mbedTLS) ──────────────────────────────────────────────── */

/*
 * TODO: Implement using mbedTLS once the Wii port provides a trustworthy
 * entropy source for the shared crypto/TLS backend. The digest operation
 * itself does not consume entropy, but shipping a partial backend would make
 * the transport appear usable when TLS cannot be seeded securely.
 */
wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
                           unsigned char out[32]) {
    (void)in; (void)len; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

/* ── Base64url (pure C, no platform dependency) ─────────────────────── */

wf_status wf_crypto_base64url_decode(const char *in,
                                     unsigned char **out, size_t *out_len) {
    static const char b64tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t in_len, padded_len, padding = 0, i;
    unsigned char *decoded = NULL;
    if (!in || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    in_len = strlen(in);
    if (in_len == 0) return WF_ERR_PARSE;

    padded_len = ((in_len + 3) / 4) * 4;
    decoded = malloc(padded_len);
    if (!decoded) return WF_ERR_ALLOC;

    /* Decode base64 characters */
    size_t bits = 0, nbits = 0, idx = 0;
    for (i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '-' || c == '+') c = '+';
        else if (c == '_' || c == '/') c = '/';
        else if (c == '=') { padding++; continue; }
        else if (!isalnum((unsigned char)c)) { free(decoded); return WF_ERR_PARSE; }
        const char *p = strchr(b64tbl, c);
        if (!p) { free(decoded); return WF_ERR_PARSE; }
        bits = (bits << 6) | (size_t)(p - b64tbl);
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            decoded[idx++] = (unsigned char)((bits >> nbits) & 0xFF);
        }
    }
    if (idx < padding) { free(decoded); return WF_ERR_PARSE; }
    *out_len = idx - padding;
    *out = decoded;
    return WF_OK;
}

wf_status wf_crypto_base64url_encode(const unsigned char *in, size_t len,
                                     char **out) {
    static const char b64tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *result = NULL;
    size_t out_len;
    if (!in || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    if (len == 0) {
        result = calloc(1, 1);
        if (!result) return WF_ERR_ALLOC;
        *out = result;
        return WF_OK;
    }
    out_len = 4 * ((len + 2) / 3);
    result = malloc(out_len + 1);
    if (!result) return WF_ERR_ALLOC;
    size_t i, j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        result[j++] = b64tbl[(in[i] >> 2) & 0x3F];
        result[j++] = b64tbl[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
        result[j++] = b64tbl[((in[i + 1] & 0x0F) << 2) | (in[i + 2] >> 6)];
        result[j++] = b64tbl[in[i + 2] & 0x3F];
    }
    if (i < len) {
        result[j++] = b64tbl[(in[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            result[j++] = b64tbl[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
            result[j++] = b64tbl[(in[i + 1] & 0x0F) << 2];
        } else {
            result[j++] = b64tbl[(in[i] & 0x03) << 4];
            result[j++] = '=';
        }
        if (i + 2 >= len) result[j++] = '=';
    }
    /* Convert to base64url: strip padding, swap +/- and / */
    size_t end = j;
    while (end > 0 && result[end - 1] == '=') end--;
    for (size_t k = 0; k < end; k++) {
        if (result[k] == '+') result[k] = '-';
        else if (result[k] == '/') result[k] = '_';
    }
    result[end] = '\0';
    *out = result;
    return WF_OK;
}

/* ── ECDSA / secp256k1 stubs ────────────────────────────────────────── */

/*
 * TODO: Implement P-256 operations using mbedTLS:
 *   - mbedtls_ecdsa_sign (wf_sign for P256)
 *   - mbedtls_ecdsa_verify (wf_verify for P256)
 *   - mbedtls_ecp_gen_keypair (wf_signing_key_generate for P256)
 *   - mbedtls_ecp_point_mul (wf_signing_key_public_didkey for P256)
 *
 * secp256k1 stubs can remain as-is (returning WF_ERR_INVALID_ARG)
 * since Channel Blue doesn't need secp256k1.
 */

wf_status wf_signing_key_public_didkey(const wf_signing_key *key,
                                       char **out_didkey) {
    (void)key; (void)out_didkey;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out) {
    (void)type; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_sign(const wf_signing_key *key,
                  const unsigned char *msg, size_t msg_len,
                  unsigned char *sig_out, size_t sig_out_cap) {
    (void)key; (void)msg; (void)msg_len; (void)sig_out; (void)sig_out_cap;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_verify(const char *public_key_multibase,
                    const unsigned char *msg, size_t msg_len,
                    const unsigned char *sig, size_t sig_len) {
    (void)public_key_multibase; (void)msg; (void)msg_len;
    (void)sig; (void)sig_len;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_verify_allow_malleable(const char *public_key_multibase,
                                    const unsigned char *msg, size_t msg_len,
                                    const unsigned char *sig, size_t sig_len) {
    (void)public_key_multibase; (void)msg; (void)msg_len;
    (void)sig; (void)sig_len;
    /* TODO: implement with the same device crypto backend as wf_verify. */
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_crypto_p256_verify(const unsigned char x[32],
                                const unsigned char y[32],
                                const unsigned char *msg, size_t msg_len,
                                const unsigned char *sig, size_t sig_len) {
    (void)x; (void)y; (void)msg; (void)msg_len; (void)sig; (void)sig_len;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_crypto_p256_jwk_coords(const char *jwk_json,
                                    unsigned char x[32], unsigned char y[32]) {
    (void)jwk_json; (void)x; (void)y;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_didkey_encode(wf_key_type type, const unsigned char *raw_pub,
                           size_t raw_len, char **out_didkey) {
    (void)type; (void)raw_pub; (void)raw_len;
    if (out_didkey) *out_didkey = NULL;
    /* TODO: Share the desktop base58btc codec without pulling in OpenSSL. */
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_didkey_decode(const char *didkey, wf_key_type *out_type,
                           unsigned char **out_raw, size_t *out_raw_len) {
    (void)didkey;
    if (out_type) *out_type = WF_KEY_TYPE_UNKNOWN;
    if (out_raw) *out_raw = NULL;
    if (out_raw_len) *out_raw_len = 0;
    /* TODO: Share the desktop base58btc codec without pulling in OpenSSL. */
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_didkey_from_verification_method(
    const char *verification_type, const char *public_key_multibase,
    char **out_didkey) {
    (void)verification_type; (void)public_key_multibase;
    if (out_didkey) *out_didkey = NULL;
    /* TODO: Normalize verification keys after the shared base58btc codec is
     * available on embedded targets. */
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_didkey_verification_method_id(const char *didkey, char **out_id) {
    (void)didkey;
    if (out_id) *out_id = NULL;
    /* TODO: Enable with the rest of the embedded did:key backend. */
    return WF_ERR_NOT_IMPLEMENTED;
}
