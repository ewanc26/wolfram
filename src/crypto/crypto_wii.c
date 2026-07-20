/**
 * crypto_wii.c — Real crypto backend for the Nintendo Wii.
 *
 * Backed by mbedTLS (cross-compiled as a Wii portlib) for P-256 EC
 * operations and by the project's pure-C SHA-256 (openssl_compat.c). This
 * implements everything the read path needs — hashing, base64url, P-256
 * signature verification, did:key codec, and JWK parsing — plus P-256 key
 * generation/signing for OAuth DPoP. secp256k1 operations remain honest
 * stubs (WF_ERR_INVALID_ARG) because the Wii port has no libsecp256k1; this
 * matches the project's stated policy.
 */

#include "wolfram/crypto.h"
#include "openssl_compat.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <cJSON.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/error.h>

/* ── SHA-256 (shared pure-C implementation) ─────────────────────────── */

wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
                           unsigned char out[32]) {
    if (!in || !out) return WF_ERR_INVALID_ARG;
    SHA256(in, len, out);
    return WF_OK;
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

/* ── base58btc (multibase 'z') codec ────────────────────────────────── */

static const char wf_b58_alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static char *wf_b58_encode(const unsigned char *data, size_t len) {
    size_t zeros = 0;
    while (zeros < len && data[zeros] == 0) zeros++;
    size_t out_size = len * 138 / 100 + 1;
    unsigned char *buf = calloc(out_size, 1);
    if (!buf) return NULL;
    for (size_t i = zeros; i < len; i++) {
        unsigned carry = data[i];
        for (size_t j = out_size; j-- > 0;) {
            carry += (unsigned)buf[j] * 256u;
            buf[j] = (unsigned char)(carry % 58u);
            carry /= 58u;
        }
    }
    size_t out_len = 0;
    while (out_len < out_size && buf[out_len] == 0) out_len++;
    size_t total = zeros + (out_size - out_len);
    char *result = malloc(total + 1);
    if (!result) { free(buf); return NULL; }
    size_t p = 0;
    for (size_t i = 0; i < zeros; i++) result[p++] = '1';
    for (size_t i = out_len; i < out_size; i++) result[p++] = wf_b58_alphabet[buf[i]];
    result[p] = '\0';
    free(buf);
    return result;
}

static wf_status wf_b58_decode(const char *str, size_t len,
                               unsigned char **out, size_t *out_len) {
    if (!str || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL; *out_len = 0;
    if (len == 0) return WF_ERR_INVALID_ARG;
    size_t zeros = 0;
    while (zeros < len && str[zeros] == '1') zeros++;
    size_t buf_size = len * 733 / 1000 + 1;
    unsigned char *buf = calloc(buf_size, 1);
    if (!buf) return WF_ERR_ALLOC;
    for (size_t i = zeros; i < len; i++) {
        const char *p = strchr(wf_b58_alphabet, str[i]);
        if (!p) { free(buf); return WF_ERR_INVALID_ARG; }
        unsigned carry = (unsigned)(p - wf_b58_alphabet);
        for (size_t j = buf_size; j-- > 0;) {
            carry += (unsigned)buf[j] * 58u;
            buf[j] = (unsigned char)(carry & 0xff);
            carry >>= 8;
        }
    }
    size_t start = 0;
    while (start < buf_size && buf[start] == 0) start++;
    size_t payload = buf_size - start;
    unsigned char *result = malloc(zeros + payload ? zeros + payload : 1);
    if (!result) { free(buf); return WF_ERR_ALLOC; }
    size_t k = 0;
    for (size_t i = 0; i < zeros; i++) result[k++] = 0;
    for (size_t i = start; i < buf_size; i++) result[k++] = buf[i];
    free(buf);
    *out = result;
    *out_len = zeros + payload;
    return WF_OK;
}

/* ── P-256 helpers (mbedTLS) ────────────────────────────────────────── */

/* P-256 domain parameters (NIST curve prime256v1). */
#define WII_P256_P "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF"
#define WII_P256_A "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC"
#define WII_P256_B "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B"
#define WII_P256_N "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"

/* Recover the Y coordinate of a compressed P-256 point. P-256's prime is
 * 3 mod 4, so a modular square root is a^((p+1)/4). Returns WF_OK on success. */
static wf_status wii_p256_decompress(const unsigned char comp[33],
                                     unsigned char x[32], unsigned char y[32]) {
    mbedtls_mpi P, A, B, X, X2, X3, T, RHS, E, Y;
    mbedtls_mpi_init(&P); mbedtls_mpi_init(&A); mbedtls_mpi_init(&B);
    mbedtls_mpi_init(&X); mbedtls_mpi_init(&X2); mbedtls_mpi_init(&X3);
    mbedtls_mpi_init(&T); mbedtls_mpi_init(&RHS); mbedtls_mpi_init(&E);
    mbedtls_mpi_init(&Y);

    mbedtls_mpi_read_string(&P, 16, WII_P256_P);
    mbedtls_mpi_read_string(&A, 16, WII_P256_A);
    mbedtls_mpi_read_string(&B, 16, WII_P256_B);
    mbedtls_mpi_read_binary(&X, comp + 1, 32);

    mbedtls_mpi_mul_mpi(&X2, &X, &X); mbedtls_mpi_mod_mpi(&X2, &X2, &P);
    mbedtls_mpi_mul_mpi(&X3, &X2, &X); mbedtls_mpi_mod_mpi(&X3, &X3, &P);
    mbedtls_mpi_mul_mpi(&T, &A, &X); mbedtls_mpi_mod_mpi(&T, &T, &P);
    mbedtls_mpi_add_mpi(&RHS, &X3, &T); mbedtls_mpi_mod_mpi(&RHS, &RHS, &P);
    mbedtls_mpi_add_mpi(&RHS, &RHS, &B); mbedtls_mpi_mod_mpi(&RHS, &RHS, &P);

    /* E = (P + 1) / 4  →  sqrt(RHS) mod P */
    mbedtls_mpi_add_int(&E, &P, 1);
    mbedtls_mpi_shift_r(&E, 2);
    mbedtls_mpi_exp_mod(&Y, &RHS, &E, &P, NULL);

    /* Match the sign bit encoded in the prefix (0x02 = even, 0x03 = odd). */
    int want_odd = (comp[0] & 0x01);
    if ((int)mbedtls_mpi_get_bit(&Y, 0) != want_odd)
        mbedtls_mpi_sub_mpi(&Y, &P, &Y);

    mbedtls_mpi_write_binary(&X, x, 32);
    mbedtls_mpi_write_binary(&Y, y, 32);

    mbedtls_mpi_free(&P); mbedtls_mpi_free(&A); mbedtls_mpi_free(&B);
    mbedtls_mpi_free(&X); mbedtls_mpi_free(&X2); mbedtls_mpi_free(&X3);
    mbedtls_mpi_free(&T); mbedtls_mpi_free(&RHS); mbedtls_mpi_free(&E);
    mbedtls_mpi_free(&Y);
    return WF_OK;
}

/* Enforce low-S (canonical) ECDSA signatures unless malleability is allowed. */
static int wii_p256_is_low_s(const unsigned char sig[64]) {
    mbedtls_mpi N, Half, S;
    mbedtls_mpi_init(&N); mbedtls_mpi_init(&Half); mbedtls_mpi_init(&S);
    mbedtls_mpi_read_string(&N, 16, WII_P256_N);
    mbedtls_mpi_copy(&Half, &N);
    mbedtls_mpi_shift_r(&Half, 1);
    mbedtls_mpi_read_binary(&S, sig + 32, 32);
    int low = (mbedtls_mpi_cmp_mpi(&S, &Half) <= 0);
    mbedtls_mpi_free(&N); mbedtls_mpi_free(&Half); mbedtls_mpi_free(&S);
    return low ? WF_OK : WF_ERR_PARSE;
}

static wf_status wii_p256_verify_raw(const unsigned char x[32],
                                     const unsigned char y[32],
                                     const unsigned char hash[32],
                                     const unsigned char *sig, size_t sig_len,
                                     int allow_malleable) {
    if (sig_len != 64) return WF_ERR_INVALID_ARG;
    if (!allow_malleable && wii_p256_is_low_s(sig) != WF_OK)
        return WF_ERR_PARSE;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&Q.X, x, 32);
    mbedtls_mpi_read_binary(&Q.Y, y, 32);
    mbedtls_mpi_lset(&Q.Z, 1);
    mbedtls_mpi_read_binary(&r, sig, 32);
    mbedtls_mpi_read_binary(&s, sig + 32, 32);

    int rc = mbedtls_ecdsa_verify(&grp, hash, 32, &Q, &r, &s);
    wf_status status = (rc == 0) ? WF_OK : WF_ERR_PARSE;

    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return status;
}

/* ── P-256 verify (public API) ──────────────────────────────────────── */

wf_status wf_crypto_p256_verify(const unsigned char x[32],
                                const unsigned char y[32],
                                const unsigned char *msg, size_t msg_len,
                                const unsigned char *sig, size_t sig_len) {
    unsigned char hash[32];
    if (!msg || msg_len == 0) return WF_ERR_INVALID_ARG;
    SHA256(msg, msg_len, hash);
    return wii_p256_verify_raw(x, y, hash, sig, sig_len, 0);
}

wf_status wf_crypto_p256_jwk_coords(const char *jwk_json,
                                     unsigned char x[32], unsigned char y[32]) {
    cJSON *root, *item;
    char *bx = NULL, *by = NULL;
    size_t lx = 0, ly = 0;
    if (!jwk_json || !x || !y) return WF_ERR_INVALID_ARG;
    root = cJSON_Parse(jwk_json);
    if (!root) return WF_ERR_PARSE;
    item = cJSON_GetObjectItemCaseSensitive(root, "x");
    if (!cJSON_IsString(item)) { cJSON_Delete(root); return WF_ERR_PARSE; }
    item = cJSON_GetObjectItemCaseSensitive(root, "y");
    if (!cJSON_IsString(item)) { cJSON_Delete(root); return WF_ERR_PARSE; }
    /* Re-parse to capture both strings; cJSON_GetObjectItem returns same root. */
    cJSON *jx = cJSON_GetObjectItemCaseSensitive(root, "x");
    cJSON *jy = cJSON_GetObjectItemCaseSensitive(root, "y");
    wf_status s = wf_crypto_base64url_decode(jx->valuestring,
                                             (unsigned char **)&bx, &lx);
    if (s == WF_OK)
        s = wf_crypto_base64url_decode(jy->valuestring,
                                       (unsigned char **)&by, &ly);
    if (s == WF_OK && lx == 32 && ly == 32) {
        memcpy((void *)x, bx, 32);
        memcpy((void *)y, by, 32);
    } else {
        s = WF_ERR_PARSE;
    }
    free(bx); free(by);
    cJSON_Delete(root);
    return s;
}

/* ── did:key codec ──────────────────────────────────────────────────── */

wf_status wf_didkey_encode(wf_key_type type, const unsigned char *raw_pub,
                           size_t raw_len, char **out_didkey) {
    unsigned char prefixed[35];
    if (!raw_pub || raw_len != 33 || !out_didkey) return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;
    if (type == WF_KEY_TYPE_P256) { prefixed[0] = 0x80; prefixed[1] = 0x24; }
    else if (type == WF_KEY_TYPE_SECP256K1) { prefixed[0] = 0xe7; prefixed[1] = 0x01; }
    else return WF_ERR_INVALID_ARG;
    memcpy(prefixed + 2, raw_pub, 33);

    char *b58 = wf_b58_encode(prefixed, sizeof(prefixed));
    if (!b58) return WF_ERR_ALLOC;
    char *didkey = malloc(strlen("did:key:z") + strlen(b58) + 1);
    if (!didkey) { free(b58); return WF_ERR_ALLOC; }
    sprintf(didkey, "did:key:z%s", b58);
    free(b58);
    *out_didkey = didkey;
    return WF_OK;
}

wf_status wf_didkey_decode(const char *didkey, wf_key_type *out_type,
                           unsigned char **out_raw, size_t *out_raw_len) {
    if (!didkey || !out_type || !out_raw || !out_raw_len)
        return WF_ERR_INVALID_ARG;
    *out_type = WF_KEY_TYPE_UNKNOWN;
    *out_raw = NULL; *out_raw_len = 0;

    const char *payload = didkey;
    if (strncmp(payload, "did:key:", 8) == 0) payload += 8;
    if (payload[0] != 'z') return WF_ERR_INVALID_ARG;
    payload += 1;

    unsigned char *decoded = NULL;
    size_t dlen = 0;
    wf_status s = wf_b58_decode(payload, strlen(payload), &decoded, &dlen);
    if (s != WF_OK) return s;
    if (dlen != 35) { free(decoded); return WF_ERR_PARSE; }

    wf_key_type type;
    if (decoded[0] == 0x80 && decoded[1] == 0x24) type = WF_KEY_TYPE_P256;
    else if (decoded[0] == 0xe7 && decoded[1] == 0x01) type = WF_KEY_TYPE_SECP256K1;
    else { free(decoded); return WF_ERR_PARSE; }

    unsigned char *raw = malloc(33);
    if (!raw) { free(decoded); return WF_ERR_ALLOC; }
    memcpy(raw, decoded + 2, 33);
    free(decoded);

    *out_type = type;
    *out_raw = raw;
    *out_raw_len = 33;
    return WF_OK;
}

wf_status wf_didkey_from_verification_method(
    const char *verification_type, const char *public_key_multibase,
    char **out_didkey) {
    if (!verification_type || !public_key_multibase || !out_didkey)
        return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;

    if (strcmp(verification_type, "Multikey") == 0) {
        wf_key_type type;
        unsigned char *raw = NULL;
        size_t raw_len = 0;
        wf_status status = wf_didkey_decode(public_key_multibase, &type,
                                            &raw, &raw_len);
        if (status != WF_OK) { free(raw); return status; }
        status = wf_didkey_encode(type, raw, raw_len, out_didkey);
        free(raw);
        return status;
    }

    wf_key_type type;
    if (strcmp(verification_type, "EcdsaSecp256k1VerificationKey2019") == 0)
        type = WF_KEY_TYPE_SECP256K1;
    else if (strcmp(verification_type, "EcdsaSecp256r1VerificationKey2019") == 0)
        type = WF_KEY_TYPE_P256;
    else
        return WF_ERR_NOT_IMPLEMENTED;

    const char *encoded = public_key_multibase;
    if (encoded[0] != 'z') return WF_ERR_INVALID_ARG;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    wf_status status = wf_b58_decode(encoded + 1, strlen(encoded + 1),
                                     &raw, &raw_len);
    unsigned char compressed[33];
    const unsigned char *key_bytes = raw;
    size_t key_len = raw_len;

    if (status == WF_OK && raw_len == 65) {
        mbedtls_ecp_group grp;
        mbedtls_ecp_point pt;
        mbedtls_ecp_group_init(&grp);
        mbedtls_ecp_point_init(&pt);
        mbedtls_ecp_group_load(&grp, type == WF_KEY_TYPE_P256
                                      ? MBEDTLS_ECP_DP_SECP256R1
                                      : MBEDTLS_ECP_DP_SECP256K1);
        if (mbedtls_ecp_point_read_binary(&grp, &pt, raw, raw_len) != 0 ||
            mbedtls_ecp_point_write_binary(&grp, &pt, MBEDTLS_ECP_PF_COMPRESSED,
                                          &(size_t){0}, compressed,
                                          sizeof(compressed)) != 0) {
            status = WF_ERR_PARSE;
        } else {
            key_bytes = compressed;
            key_len = sizeof(compressed);
        }
        mbedtls_ecp_point_free(&pt);
        mbedtls_ecp_group_free(&grp);
    }
    if (status == WF_OK)
        status = wf_didkey_encode(type, key_bytes, key_len, out_didkey);
    free(raw);
    return status;
}

wf_status wf_didkey_verification_method_id(const char *didkey, char **out_id) {
    if (!didkey || !out_id) return WF_ERR_INVALID_ARG;
    *out_id = NULL;
    size_t did_len = strlen(didkey);
    char *id = malloc(did_len * 2 + 2);
    if (!id) return WF_ERR_ALLOC;
    sprintf(id, "%s#%s", didkey, didkey);
    *out_id = id;
    return WF_OK;
}

/* ── Verification entry points ──────────────────────────────────────── */

static wf_status wii_verify_didkey(const char *public_key_multibase,
                                   const unsigned char *msg, size_t msg_len,
                                   const unsigned char *sig, size_t sig_len,
                                   int allow_malleable) {
    if (!public_key_multibase || !msg || msg_len == 0 || !sig)
        return WF_ERR_INVALID_ARG;

    const char *encoded = public_key_multibase;
    if (strncmp(encoded, "did:key:", 8) == 0) encoded += 8;
    if (*encoded++ != 'z' || *encoded == '\0') return WF_ERR_INVALID_ARG;

    unsigned char *decoded = NULL;
    size_t dlen = 0;
    wf_status s = wf_b58_decode(encoded, strlen(encoded), &decoded, &dlen);
    if (s != WF_OK) return s;
    if (dlen != 35) { free(decoded); return WF_ERR_PARSE; }

    int is_p256 = (decoded[0] == 0x80 && decoded[1] == 0x24);
    int is_k256 = (decoded[0] == 0xe7 && decoded[1] == 0x01);
    if (!is_p256 && !is_k256) { free(decoded); return WF_ERR_PARSE; }
    if (is_k256) {
        /* No libsecp256k1 on the Wii: K-256 signatures cannot be verified. */
        free(decoded);
        return WF_ERR_INVALID_ARG;
    }

    unsigned char x[32], y[32];
    s = wii_p256_decompress(decoded + 2, x, y);
    free(decoded);
    if (s != WF_OK) return s;

    unsigned char hash[32];
    SHA256(msg, msg_len, hash);
    return wii_p256_verify_raw(x, y, hash, sig, sig_len, allow_malleable);
}

wf_status wf_verify(const char *public_key_multibase,
                    const unsigned char *msg, size_t msg_len,
                    const unsigned char *sig, size_t sig_len) {
    return wii_verify_didkey(public_key_multibase, msg, msg_len, sig, sig_len, 0);
}

wf_status wf_verify_allow_malleable(const char *public_key_multibase,
                                    const unsigned char *msg, size_t msg_len,
                                    const unsigned char *sig, size_t sig_len) {
    return wii_verify_didkey(public_key_multibase, msg, msg_len, sig, sig_len, 1);
}

/* ── P-256 key generation / signing ─────────────────────────────────── */

extern int wii_tls_random(void *p, unsigned char *out, size_t len);

wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    if (type == WF_KEY_TYPE_P256) {
        mbedtls_ecp_group grp;
        mbedtls_mpi d, Nm1;
        mbedtls_ecp_point Q;
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_mpi_init(&Nm1);
        mbedtls_ecp_point_init(&Q);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        /* Generate d in [1, N-1] from the DRBG, then derive Q = d·G. */
        int rc = WF_ERR_CRYPTO;
        for (int tries = 0; tries < 16; tries++) {
            unsigned char buf[32];
            if (wii_tls_random(NULL, buf, 32) != 0) break;
            mbedtls_mpi_read_binary(&d, buf, 32);
            mbedtls_mpi_mod_mpi(&d, &d, &grp.N);
            mbedtls_mpi_sub_int(&Nm1, &grp.N, 1);
            if (mbedtls_mpi_cmp_int(&d, 1) < 0 ||
                mbedtls_mpi_cmp_mpi(&d, &Nm1) > 0)
                continue;
            if (mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, wii_tls_random, NULL) != 0)
                continue;
            rc = mbedtls_mpi_write_binary(&d, out->bytes, 32);
            break;
        }
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d);
        mbedtls_mpi_free(&Nm1);
        mbedtls_ecp_point_free(&Q);
        if (rc != WF_OK) return WF_ERR_CRYPTO;
        out->type = WF_KEY_TYPE_P256;
        return WF_OK;
    }
    /* secp256k1 has no backend on the Wii. */
    return WF_ERR_INVALID_ARG;
}

wf_status wf_signing_key_public_didkey(const wf_signing_key *key,
                                       char **out_didkey) {
    unsigned char raw[33];
    if (!key || !out_didkey) return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;
    if (key->type == WF_KEY_TYPE_P256) {
        mbedtls_ecp_group grp;
        mbedtls_mpi d;
        mbedtls_ecp_point Q;
        size_t olen = 0;
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        mbedtls_mpi_read_binary(&d, key->bytes, 32);
        int rc = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, wii_tls_random, NULL);
        if (rc == 0)
            rc = mbedtls_ecp_point_write_binary(&grp, &Q,
                                                MBEDTLS_ECP_PF_COMPRESSED,
                                                &olen, raw, sizeof(raw));
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_point_free(&Q);
        if (rc != 0 || olen != 33) return WF_ERR_CRYPTO;
        return wf_didkey_encode(WF_KEY_TYPE_P256, raw, 33, out_didkey);
    }
    return WF_ERR_INVALID_ARG;
}

wf_status wf_sign(const wf_signing_key *key,
                  const unsigned char *msg, size_t msg_len,
                  unsigned char *sig_out, size_t sig_out_cap) {
    unsigned char hash[32];
    if (!key || !msg || msg_len == 0 || !sig_out || sig_out_cap < 64)
        return WF_ERR_INVALID_ARG;
    if (key->type == WF_KEY_TYPE_P256) {
        mbedtls_ecp_group grp;
        mbedtls_mpi d, r, s;
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        mbedtls_mpi_read_binary(&d, key->bytes, 32);
        SHA256(msg, msg_len, hash);
        int rc = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, 32,
                                    wii_tls_random, NULL);
        if (rc == 0) {
            mbedtls_mpi_write_binary(&r, sig_out, 32);
            mbedtls_mpi_write_binary(&s, sig_out + 32, 32);
        }
        mbedtls_ecp_group_free(&grp);
        mbedtls_mpi_free(&d); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
        return rc == 0 ? WF_OK : WF_ERR_CRYPTO;
    }
    return WF_ERR_INVALID_ARG;
}
