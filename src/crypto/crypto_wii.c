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
 * TODO: Implement using mbedTLS:
 *   #include <mbedtls/sha256.h>
 *
 *   wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
 *                               unsigned char out[32]) {
 *       if (!in || !out) return WF_ERR_INVALID_ARG;
 *       mbedtls_sha256(in, len, out, 0);
 *       return WF_OK;
 *   }
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
    size_t in_len, padded_len, padding, i;
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
