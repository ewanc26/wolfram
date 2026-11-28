/**
 * key_multibase.c — private signing keys as multibase strings.
 *
 * The encoding is the one `goat key generate` prints and indigo's atcrypto
 * parses (ParsePrivateMultibase): 'z' + base58btc(varint multicodec || raw
 * 32-byte scalar), with secp256k1-priv (0x1301, varint 0x81 0x26) and
 * p256-priv (0x1306, varint 0x86 0x26) from the multicodec table.
 *
 * Pure C on purpose: the scalar range checks compare against the curve
 * orders byte by byte, so the parser rejects the same strings with or
 * without libsecp256k1 and on the mbedTLS backend.
 */

#include "wolfram/crypto.h"

#include <stdlib.h>
#include <string.h>

#include "base58.h"

#define WF_KEY_MB_PREFIX_LEN 2u
#define WF_KEY_MB_RAW_LEN (WF_KEY_MB_PREFIX_LEN + 32u)
/* A 34-byte payload is at most 47 base58 characters; anything much longer
 * is not a key, so refuse it before doing quadratic base conversion. */
#define WF_KEY_MB_MAX_CHARS 64u

static const unsigned char wf_k256_order[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
    0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

static const unsigned char wf_p256_order[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17,
    0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51};

void wf_secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    if (!p) return;
    while (len--) *p++ = 0;
}

static int wf_key_mb_prefix(wf_key_type type, unsigned char out[2]) {
    if (type == WF_KEY_TYPE_SECP256K1) {
        out[0] = 0x81;
        out[1] = 0x26;
        return 1;
    }
    if (type == WF_KEY_TYPE_P256) {
        out[0] = 0x86;
        out[1] = 0x26;
        return 1;
    }
    return 0;
}

/* 1 when 0 < scalar < order for the key's curve. */
static int wf_key_scalar_in_range(wf_key_type type,
                                  const unsigned char scalar[32]) {
    const unsigned char *order = type == WF_KEY_TYPE_SECP256K1 ? wf_k256_order
                                 : type == WF_KEY_TYPE_P256    ? wf_p256_order
                                                               : NULL;
    unsigned char any = 0;
    size_t i;
    if (!order) return 0;
    for (i = 0; i < 32; i++) any |= scalar[i];
    if (!any) return 0;
    return memcmp(scalar, order, 32) < 0;
}

wf_status wf_signing_key_to_multibase(const wf_signing_key *key,
                                      char **out_secret) {
    unsigned char raw[WF_KEY_MB_RAW_LEN];
    char *b58;
    char *out;
    size_t n;

    if (!out_secret) return WF_ERR_INVALID_ARG;
    *out_secret = NULL;
    if (!key || !wf_key_mb_prefix(key->type, raw) ||
        !wf_key_scalar_in_range(key->type, key->bytes))
        return WF_ERR_INVALID_ARG;

    memcpy(raw + WF_KEY_MB_PREFIX_LEN, key->bytes, 32);
    b58 = wf_b58_encode(raw, sizeof(raw));
    wf_secure_zero(raw, sizeof(raw));
    if (!b58) return WF_ERR_ALLOC;

    n = strlen(b58);
    out = malloc(n + 2);
    if (!out) {
        wf_signing_key_secret_free(b58);
        return WF_ERR_ALLOC;
    }
    out[0] = 'z';
    memcpy(out + 1, b58, n + 1);
    wf_signing_key_secret_free(b58);
    *out_secret = out;
    return WF_OK;
}

wf_status wf_signing_key_from_multibase(const char *secret,
                                        wf_signing_key *out) {
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    size_t len;
    wf_key_type type = WF_KEY_TYPE_UNKNOWN;
    wf_signing_key candidate;
    wf_status s;

    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!secret) return WF_ERR_INVALID_ARG;

    len = strlen(secret);
    if (len < 2 || len > WF_KEY_MB_MAX_CHARS || secret[0] != 'z')
        return WF_ERR_PARSE;

    s = wf_b58_decode(secret + 1, len - 1, &raw, &raw_len);
    if (s == WF_ERR_ALLOC) return s;
    if (s != WF_OK) return WF_ERR_PARSE;

    if (raw_len != WF_KEY_MB_RAW_LEN) {
        s = WF_ERR_PARSE;
    } else if (raw[0] == 0x81 && raw[1] == 0x26) {
        type = WF_KEY_TYPE_SECP256K1;
    } else if (raw[0] == 0x86 && raw[1] == 0x26) {
        type = WF_KEY_TYPE_P256;
    } else {
        s = WF_ERR_PARSE;
    }

    if (s == WF_OK) {
        memset(&candidate, 0, sizeof(candidate));
        candidate.type = type;
        memcpy(candidate.bytes, raw + WF_KEY_MB_PREFIX_LEN, 32);
        if (!wf_key_scalar_in_range(type, candidate.bytes)) {
            s = WF_ERR_PARSE;
        } else {
            *out = candidate;
        }
        wf_secure_zero(&candidate, sizeof(candidate));
    }

    wf_secure_zero(raw, raw_len);
    free(raw);
    return s;
}

void wf_signing_key_secret_free(char *secret) {
    if (!secret) return;
    wf_secure_zero(secret, strlen(secret));
    free(secret);
}
