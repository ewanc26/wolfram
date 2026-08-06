/**
 * crypto.c — secp256k1 signing/verification via libsecp256k1,
 *             P-256 signing/verification via OpenSSL EC API.
 */

#include "wolfram/crypto.h"

#include "crypto_internal.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>

#include <openssl/sha.h>
#include <cJSON.h>

#include "wolfram/log.h"

static wf_status wf_p256_is_low_s(const BIGNUM *s, const EC_GROUP *group) {
    const BIGNUM *order;
    BIGNUM *half_order;
    int low;

    if (!s || !group) return WF_ERR_INVALID_ARG;
    order = EC_GROUP_get0_order(group);
    if (!order) return WF_ERR_ALLOC;

    half_order = BN_dup(order);
    if (!half_order) return WF_ERR_ALLOC;
    if (!BN_rshift1(half_order, half_order)) {
        BN_free(half_order);
        return WF_ERR_ALLOC;
    }

    low = BN_cmp(s, half_order) <= 0;
    BN_free(half_order);
    return low ? WF_OK : WF_ERR_PARSE;
}

static wf_status wf_p256_normalize_s(const EC_GROUP *group, BIGNUM **s_io) {
    const BIGNUM *order;
    BIGNUM *half_order;
    BIGNUM *s;

    if (!group || !s_io || !*s_io) return WF_ERR_INVALID_ARG;
    s = *s_io;
    order = EC_GROUP_get0_order(group);
    if (!order) return WF_ERR_ALLOC;

    half_order = BN_dup(order);
    if (!half_order) return WF_ERR_ALLOC;
    if (!BN_rshift1(half_order, half_order)) {
        BN_free(half_order);
        return WF_ERR_ALLOC;
    }

    if (BN_cmp(s, half_order) > 0) {
        BIGNUM *normalized = BN_dup(order);
        if (!normalized || !BN_sub(normalized, normalized, s)) {
            BN_free(normalized);
            BN_free(half_order);
            return WF_ERR_ALLOC;
        }
        BN_free(s);
        *s_io = normalized;
    }

    BN_free(half_order);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* P-256 EVP_PKEY construction (OpenSSL 3.0 API, no EC_KEY object)     */
/* ------------------------------------------------------------------ */

/* Build a full P-256 keypair EVP_PKEY (private + its derived public point)
 * from a raw 32-byte private scalar. The public point is computed the same
 * way wf_signing_key_public_didkey does (EC_GROUP + EC_POINT_mul) and
 * supplied explicitly, rather than relying on EVP_PKEY_fromdata to derive it
 * from "priv" alone. */
wf_status wf_p256_pkey_from_private(const unsigned char priv_bytes[32],
                                    EVP_PKEY **out) {
    EC_GROUP *group = NULL;
    EC_POINT *pub_point = NULL;
    BIGNUM *priv_bn = NULL;
    unsigned char pub_oct[65];
    size_t pub_oct_len;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    wf_status status = WF_ERR_ALLOC;

    *out = NULL;
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    priv_bn = BN_bin2bn(priv_bytes, 32, NULL);
    pub_point = group ? EC_POINT_new(group) : NULL;
    if (!group || !priv_bn || !pub_point ||
        EC_POINT_mul(group, pub_point, priv_bn, NULL, NULL, NULL) != 1) {
        goto done;
    }
    pub_oct_len =
        EC_POINT_point2oct(group, pub_point, POINT_CONVERSION_UNCOMPRESSED,
                           pub_oct, sizeof(pub_oct), NULL);
    if (pub_oct_len != sizeof(pub_oct)) goto done;

    bld = OSSL_PARAM_BLD_new();
    if (!bld ||
        !OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         "P-256", 0) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv_bn) ||
        !OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub_oct,
                                          pub_oct_len)) {
        goto done;
    }
    params = OSSL_PARAM_BLD_to_param(bld);
    pctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!params || !pctx || EVP_PKEY_fromdata_init(pctx) != 1 ||
        EVP_PKEY_fromdata(pctx, out, EVP_PKEY_KEYPAIR, params) != 1 || !*out) {
        goto done;
    }
    status = WF_OK;
done:
    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    EC_POINT_free(pub_point);
    BN_free(priv_bn);
    EC_GROUP_free(group);
    return status;
}

/* Build a public-only P-256 EVP_PKEY from a SEC1 octet-encoded point
 * (compressed 33 bytes or uncompressed 65 bytes, as produced by
 * EC_POINT_point2oct / consumed by EC_POINT_oct2point). */
wf_status wf_p256_pkey_from_public_point(const unsigned char *pub_oct,
                                         size_t pub_oct_len, EVP_PKEY **out) {
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    wf_status status = WF_ERR_ALLOC;

    *out = NULL;
    bld = OSSL_PARAM_BLD_new();
    if (!bld ||
        !OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         "P-256", 0) ||
        !OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub_oct,
                                          pub_oct_len)) {
        goto done;
    }
    params = OSSL_PARAM_BLD_to_param(bld);
    pctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!params || !pctx || EVP_PKEY_fromdata_init(pctx) != 1 ||
        EVP_PKEY_fromdata(pctx, out, EVP_PKEY_PUBLIC_KEY, params) != 1 ||
        !*out) {
        goto done;
    }
    status = WF_OK;
done:
    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    return status;
}

/* Sign a precomputed 32-byte hash with `pkey` (P-256), producing a raw
 * (r||s) 64-byte signature with S normalized to the lower half of the curve
 * order (low-S), matching this SDK's existing verification convention.
 * EVP_PKEY_sign produces a standard DER ECDSA-Sig-Value; d2i_ECDSA_SIG (not
 * deprecated -- it decodes the signature *value*, unlike the EC_KEY-taking
 * ECDSA_do_sign/verify) recovers r/s for the raw-encoding + low-S rewrite.
 * No ECDSA math is hand-rolled: signing itself is entirely EVP_PKEY_sign's
 * job. */
wf_status wf_p256_sign_hash(EVP_PKEY *pkey, const unsigned char hash[32],
                            unsigned char sig_out[64]) {
    EVP_PKEY_CTX *pctx = NULL;
    unsigned char *der = NULL;
    size_t der_len = 0;
    const unsigned char *der_p;
    ECDSA_SIG *sig = NULL;
    const BIGNUM *r0, *s0;
    BIGNUM *r = NULL, *s = NULL;
    EC_GROUP *group = NULL;
    wf_status status = WF_ERR_ALLOC;

    pctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!pctx || EVP_PKEY_sign_init(pctx) != 1 ||
        EVP_PKEY_sign(pctx, NULL, &der_len, hash, 32) != 1)
        goto done;
    der = malloc(der_len);
    if (!der || EVP_PKEY_sign(pctx, der, &der_len, hash, 32) != 1) goto done;

    der_p = der;
    sig = d2i_ECDSA_SIG(NULL, &der_p, (long)der_len);
    if (!sig) goto done;
    ECDSA_SIG_get0(sig, &r0, &s0);
    if (!r0 || !s0) goto done;
    r = BN_dup(r0);
    s = BN_dup(s0);
    if (!r || !s) goto done;

    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    if (!group || wf_p256_normalize_s(group, &s) != WF_OK) goto done;

    if (BN_bn2binpad(r, sig_out, 32) != 32 ||
        BN_bn2binpad(s, sig_out + 32, 32) != 32)
        goto done;
    status = WF_OK;
done:
    EC_GROUP_free(group);
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
    free(der);
    EVP_PKEY_CTX_free(pctx);
    return status;
}

/* Verify a raw (r||s) 64-byte P-256 signature over a precomputed 32-byte
 * hash. Rejects a non-low-S signature unless `allow_malleable`. Builds a DER
 * ECDSA-Sig-Value via i2d_ECDSA_SIG (not deprecated) since EVP_PKEY_verify
 * expects the standard encoding, not raw r||s. */
wf_status wf_p256_verify_hash_pkey(EVP_PKEY *pkey, const unsigned char hash[32],
                                   const unsigned char sig[64],
                                   int allow_malleable) {
    ECDSA_SIG *ecdsa_sig = NULL;
    BIGNUM *r = NULL, *s = NULL;
    EC_GROUP *group = NULL;
    unsigned char *der = NULL;
    int der_len;
    EVP_PKEY_CTX *pctx = NULL;
    wf_status status = WF_ERR_PARSE;

    r = BN_bin2bn(sig, 32, NULL);
    s = BN_bin2bn(sig + 32, 32, NULL);
    if (!r || !s) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    if (!group) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (!allow_malleable && wf_p256_is_low_s(s, group) != WF_OK) goto done;

    ecdsa_sig = ECDSA_SIG_new();
    if (!ecdsa_sig) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (ECDSA_SIG_set0(ecdsa_sig, r, s) != 1) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    r = NULL;
    s = NULL; /* ownership moved into ecdsa_sig */

    der_len = i2d_ECDSA_SIG(ecdsa_sig, &der);
    if (der_len <= 0) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    pctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!pctx || EVP_PKEY_verify_init(pctx) != 1) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    status = EVP_PKEY_verify(pctx, der, (size_t)der_len, hash, 32) == 1
                 ? WF_OK
                 : WF_ERR_PARSE;
done:
    EVP_PKEY_CTX_free(pctx);
    OPENSSL_free(der);
    ECDSA_SIG_free(ecdsa_sig);
    BN_free(r);
    BN_free(s);
    EC_GROUP_free(group);
    return status;
}

#ifdef HAVE_LIBSECP256K1
#include <secp256k1.h>
#endif

/* base58btc (multibase 'z') encoder — used only for did:key derivation. */
static const char wf_b58_alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static char *wf_b58_encode(const unsigned char *data, size_t len) {
    /* Count leading zero bytes. */
    size_t zeros = 0;
    while (zeros < len && data[zeros] == 0) zeros++;

    /* Big-number base conversion into a temporary buffer (8 bits -> 58). */
    size_t out_size = len * 138 / 100 + 1; /* conservative upper bound */
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

    /* Build the string: leading '1' per zero byte, then base58 digits. */
    size_t total = zeros + (out_size - out_len);
    char *result = malloc(total + 1);
    if (!result) {
        free(buf);
        return NULL;
    }
    size_t p = 0;
    for (size_t i = 0; i < zeros; i++) result[p++] = '1';
    for (size_t i = out_len; i < out_size; i++) {
        result[p++] = wf_b58_alphabet[buf[i]];
    }
    result[p] = '\0';

    free(buf);
    return result;
}

wf_status wf_signing_key_public_didkey(const wf_signing_key *key,
                                       char **out_didkey) {
    unsigned char raw[33];
    unsigned char prefixed[35];
    char *b58;
    char *didkey;

    WF_LOG_DEBUG("crypto", "wf_signing_key_public_didkey: enter, key_type=%d",
                 key ? (int)key->type : -1);

    if (!key || !out_didkey) {
        WF_LOG_ERROR("crypto", "wf_signing_key_public_didkey: invalid args");
        return WF_ERR_INVALID_ARG;
    }
    *out_didkey = NULL;

    if (key->type == WF_KEY_TYPE_P256) {
        WF_LOG_DEBUG("crypto",
                     "wf_signing_key_public_didkey: deriving P-256 public key");
        /* EC_GROUP/EC_POINT are the still-current OpenSSL group-arithmetic
         * API; the legacy EC_KEY object wrapper is deprecated as of OpenSSL
         * 3.0 and nothing here needs it -- the group is all EC_POINT_mul
         * requires to turn the raw private scalar into the public point. */
        EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
        EC_POINT *point;
        BIGNUM *bn_priv;
        size_t n;

        if (!group) {
            WF_LOG_ERROR("crypto",
                         "wf_signing_key_public_didkey: EC_GROUP_new failed");
            return WF_ERR_ALLOC;
        }
        bn_priv = BN_bin2bn(key->bytes, 32, NULL);
        point = bn_priv ? EC_POINT_new(group) : NULL;
        if (!bn_priv || !point ||
            EC_POINT_mul(group, point, bn_priv, NULL, NULL, NULL) != 1) {
            WF_LOG_ERROR("crypto",
                         "wf_signing_key_public_didkey: EC_POINT_mul failed");
            EC_POINT_free(point);
            BN_free(bn_priv);
            EC_GROUP_free(group);
            return WF_ERR_ALLOC;
        }
        n = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, raw,
                               sizeof(raw), NULL);
        EC_POINT_free(point);
        BN_free(bn_priv);
        EC_GROUP_free(group);
        if (n != 33) {
            WF_LOG_ERROR("crypto",
                         "wf_signing_key_public_didkey: compressed point size "
                         "!= 33 (got %zu)",
                         n);
            return WF_ERR_ALLOC;
        }
        prefixed[0] = 0x80;
        prefixed[1] = 0x24;
        memcpy(prefixed + 2, raw, 33);
        WF_LOG_DEBUG("crypto",
                     "wf_signing_key_public_didkey: P-256 public key derived");
    } else if (key->type == WF_KEY_TYPE_SECP256K1) {
#ifdef HAVE_LIBSECP256K1
        WF_LOG_DEBUG(
            "crypto",
            "wf_signing_key_public_didkey: deriving secp256k1 public key");
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        secp256k1_pubkey pubkey;
        size_t clen = 33;
        if (!ctx) {
            WF_LOG_ERROR("crypto", "wf_signing_key_public_didkey: "
                                   "secp256k1_context_create failed");
            return WF_ERR_ALLOC;
        }
        if (secp256k1_ec_pubkey_create(ctx, &pubkey, key->bytes) != 1 ||
            secp256k1_ec_pubkey_serialize(ctx, raw, &clen, &pubkey,
                                          SECP256K1_EC_COMPRESSED) != 1) {
            WF_LOG_ERROR("crypto", "wf_signing_key_public_didkey: secp256k1 "
                                   "pubkey create/serialize failed");
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }
        secp256k1_context_destroy(ctx);
        prefixed[0] = 0xe7;
        prefixed[1] = 0x01;
        memcpy(prefixed + 2, raw, 33);
        WF_LOG_DEBUG(
            "crypto",
            "wf_signing_key_public_didkey: secp256k1 public key derived");
#else
        WF_LOG_ERROR("crypto",
                     "wf_signing_key_public_didkey: secp256k1 not available");
        (void)multicodec;
        (void)raw;
        return WF_ERR_INVALID_ARG;
#endif
    } else {
        WF_LOG_ERROR("crypto",
                     "wf_signing_key_public_didkey: unsupported key type %d",
                     key->type);
        return WF_ERR_INVALID_ARG;
    }

    b58 = wf_b58_encode(prefixed, sizeof(prefixed));
    if (!b58) {
        WF_LOG_ERROR("crypto",
                     "wf_signing_key_public_didkey: base58 encode failed");
        return WF_ERR_ALLOC;
    }

    didkey = malloc(strlen("did:key:z") + strlen(b58) + 1);
    if (!didkey) {
        WF_LOG_ERROR("crypto", "wf_signing_key_public_didkey: malloc failed");
        free(b58);
        return WF_ERR_ALLOC;
    }
    sprintf(didkey, "did:key:z%s", b58);
    free(b58);

    WF_LOG_DEBUG("crypto", "wf_signing_key_public_didkey: success, didkey=%s",
                 didkey);
    *out_didkey = didkey;
    return WF_OK;
}

/* base58btc (multibase 'z') decoder — inverse of wf_b58_encode. */
static wf_status wf_b58_decode(const char *str, size_t len, unsigned char **out,
                               size_t *out_len) {
    if (!str || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    if (len == 0) return WF_ERR_INVALID_ARG;

    size_t zeros = 0;
    while (zeros < len && str[zeros] == '1') zeros++;

    /* Conservative upper bound: log_256(58) < 0.733 per input char. */
    size_t buf_size = len * 733 / 1000 + 1;
    unsigned char *buf = calloc(buf_size, 1);
    if (!buf) return WF_ERR_ALLOC;

    for (size_t i = zeros; i < len; i++) {
        const char *p = strchr(wf_b58_alphabet, str[i]);
        if (!p) {
            free(buf);
            return WF_ERR_INVALID_ARG;
        }
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
    if (!result) {
        free(buf);
        return WF_ERR_ALLOC;
    }
    size_t k = 0;
    for (size_t i = 0; i < zeros; i++) result[k++] = 0;
    for (size_t i = start; i < buf_size; i++) result[k++] = buf[i];
    free(buf);

    *out = result;
    *out_len = zeros + payload;
    return WF_OK;
}

/**
 * Encode a raw compressed public key (33 bytes) of the given type into its
 * `did:key:z...` multibase form. This is the inverse of wf_didkey_decode and
 * complements wf_signing_key_public_didkey (which derives the key from a
 * private scalar).
 *
 * On WF_OK, *out_didkey is heap-allocated and owned by the caller (free()).
 */
wf_status wf_didkey_encode(wf_key_type type, const unsigned char *raw_pub,
                           size_t raw_len, char **out_didkey) {
    unsigned char prefixed[35];
    if (!raw_pub || raw_len != 33 || !out_didkey) return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;

    if (type == WF_KEY_TYPE_P256) {
        prefixed[0] = 0x80;
        prefixed[1] = 0x24;
    } else if (type == WF_KEY_TYPE_SECP256K1) {
        prefixed[0] = 0xe7;
        prefixed[1] = 0x01;
    } else {
        return WF_ERR_INVALID_ARG;
    }
    memcpy(prefixed + 2, raw_pub, 33);

    char *b58 = wf_b58_encode(prefixed, sizeof(prefixed));
    if (!b58) return WF_ERR_ALLOC;
    char *didkey = malloc(strlen("did:key:z") + strlen(b58) + 1);
    if (!didkey) {
        free(b58);
        return WF_ERR_ALLOC;
    }
    sprintf(didkey, "did:key:z%s", b58);
    free(b58);
    *out_didkey = didkey;
    return WF_OK;
}

/**
 * Decode a `did:key:z...` or bare multikey `z...` string back into its key
 * type and raw 33-byte compressed public point.
 *
 * On WF_OK, *out_raw is heap-allocated and owned by the caller (free()).
 */
wf_status wf_didkey_decode(const char *didkey, wf_key_type *out_type,
                           unsigned char **out_raw, size_t *out_raw_len) {
    if (!didkey || !out_type || !out_raw || !out_raw_len)
        return WF_ERR_INVALID_ARG;
    *out_type = WF_KEY_TYPE_UNKNOWN;
    *out_raw = NULL;
    *out_raw_len = 0;

    const char *payload = didkey;
    if (strncmp(payload, "did:key:", 8) == 0) payload += 8;
    if (payload[0] != 'z')
        return WF_ERR_INVALID_ARG; /* did:key: or bare multikey */
    payload += 1;

    unsigned char *decoded = NULL;
    size_t dlen = 0;
    wf_status s = wf_b58_decode(payload, strlen(payload), &decoded, &dlen);
    if (s != WF_OK) return s;
    if (dlen != 35) {
        free(decoded);
        return WF_ERR_PARSE;
    }

    wf_key_type type;
    if (decoded[0] == 0x80 && decoded[1] == 0x24)
        type = WF_KEY_TYPE_P256;
    else if (decoded[0] == 0xe7 && decoded[1] == 0x01)
        type = WF_KEY_TYPE_SECP256K1;
    else {
        free(decoded);
        return WF_ERR_PARSE;
    }

    unsigned char *raw = malloc(33);
    if (!raw) {
        free(decoded);
        return WF_ERR_ALLOC;
    }
    memcpy(raw, decoded + 2, 33);
    free(decoded);

    *out_type = type;
    *out_raw = raw;
    *out_raw_len = 33;
    return WF_OK;
}

wf_status wf_didkey_from_verification_method(const char *verification_type,
                                             const char *public_key_multibase,
                                             char **out_didkey) {
    if (!verification_type || !public_key_multibase || !out_didkey)
        return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;

    if (strcmp(verification_type, "Multikey") == 0) {
        wf_key_type type;
        unsigned char *raw = NULL;
        size_t raw_len = 0;
        wf_status status =
            wf_didkey_decode(public_key_multibase, &type, &raw, &raw_len);
        if (status != WF_OK) {
            free(raw);
            return status;
        }
        status = wf_didkey_encode(type, raw, raw_len, out_didkey);
        free(raw);
        return status;
    }

    wf_key_type type;
    if (strcmp(verification_type, "EcdsaSecp256k1VerificationKey2019") == 0) {
        type = WF_KEY_TYPE_SECP256K1;
    } else if (strcmp(verification_type, "EcdsaSecp256r1VerificationKey2019") ==
               0) {
        type = WF_KEY_TYPE_P256;
    } else {
        return WF_ERR_NOT_IMPLEMENTED;
    }

    const char *encoded = public_key_multibase;
    if (encoded[0] != 'z') return WF_ERR_INVALID_ARG;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    wf_status status =
        wf_b58_decode(encoded + 1, strlen(encoded + 1), &raw, &raw_len);
    unsigned char compressed[33];
    const unsigned char *key_bytes = raw;
    size_t key_len = raw_len;
    EC_GROUP *group = NULL;
    EC_POINT *point = NULL;
    if (status == WF_OK && raw_len == 65) {
        int nid =
            type == WF_KEY_TYPE_P256 ? NID_X9_62_prime256v1 : NID_secp256k1;
        group = EC_GROUP_new_by_curve_name(nid);
        point = group ? EC_POINT_new(group) : NULL;
        if (!point ||
            EC_POINT_oct2point(group, point, raw, raw_len, NULL) != 1 ||
            EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
                               compressed, sizeof(compressed),
                               NULL) != sizeof(compressed)) {
            status = WF_ERR_PARSE;
        } else {
            key_bytes = compressed;
            key_len = sizeof(compressed);
        }
    }
    if (status == WF_OK)
        status = wf_didkey_encode(type, key_bytes, key_len, out_didkey);
    EC_POINT_free(point);
    EC_GROUP_free(group);
    free(raw);
    return status;
}

/**
 * Compute the verification-method id for a did:key, which in the AT Protocol
 * is `${did}#${did}` (the fragment equals the full DID). On WF_OK, *out_id is
 * heap-allocated and owned by the caller (free()).
 */
wf_status wf_didkey_verification_method_id(const char *didkey, char **out_id) {
    if (!didkey || !out_id) return WF_ERR_INVALID_ARG;
    *out_id = NULL;
    /* The verification-method id for a did:key is `${did}#${did}`. */
    size_t did_len = strlen(didkey);
    char *id = malloc(did_len * 2 + 2);
    if (!id) return WF_ERR_ALLOC;
    sprintf(id, "%s#%s", didkey, didkey);
    *out_id = id;
    return WF_OK;
}

wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out) {
    WF_LOG_DEBUG("crypto", "wf_signing_key_generate: enter, type=%d", type);

    if (!out) {
        WF_LOG_ERROR("crypto", "wf_signing_key_generate: null output");
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (type == WF_KEY_TYPE_P256) {
        WF_LOG_DEBUG("crypto", "wf_signing_key_generate: generating P-256 key");
        /* EVP_PKEY_Q_keygen is the current OpenSSL 3.0 one-shot keygen
         * helper; EC_KEY_new_by_curve_name + EC_KEY_generate_key is the
         * deprecated legacy-object equivalent. */
        EVP_PKEY *pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
        BIGNUM *priv = NULL;
        if (!pkey) {
            WF_LOG_ERROR("crypto",
                         "wf_signing_key_generate: EVP_PKEY_Q_keygen failed");
            return WF_ERR_ALLOC;
        }
        if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY, &priv) != 1 ||
            !priv || BN_bn2binpad(priv, out->bytes, 32) != 32) {
            WF_LOG_ERROR(
                "crypto",
                "wf_signing_key_generate: failed to extract private key");
            BN_free(priv);
            EVP_PKEY_free(pkey);
            return WF_ERR_ALLOC;
        }
        out->type = WF_KEY_TYPE_P256;
        BN_free(priv);
        EVP_PKEY_free(pkey);
        WF_LOG_DEBUG(
            "crypto",
            "wf_signing_key_generate: P-256 key generated successfully");
        return WF_OK;
    }

    if (type != WF_KEY_TYPE_SECP256K1) {
        WF_LOG_ERROR("crypto",
                     "wf_signing_key_generate: unsupported key type %d", type);
        return WF_ERR_INVALID_ARG;
    }

#ifdef HAVE_LIBSECP256K1
    WF_LOG_DEBUG("crypto", "wf_signing_key_generate: generating secp256k1 key");
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) {
        WF_LOG_ERROR(
            "crypto",
            "wf_signing_key_generate: secp256k1_context_create failed");
        return WF_ERR_ALLOC;
    }

    /* Try up to 10 times to generate a valid key */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (RAND_bytes(out->bytes, 32) != 1) {
            WF_LOG_ERROR(
                "crypto",
                "wf_signing_key_generate: RAND_bytes failed on attempt %d",
                attempt);
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }
        if (secp256k1_ec_seckey_verify(ctx, out->bytes)) {
            out->type = WF_KEY_TYPE_SECP256K1;
            secp256k1_context_destroy(ctx);
            WF_LOG_DEBUG("crypto",
                         "wf_signing_key_generate: secp256k1 key generated "
                         "successfully on attempt %d",
                         attempt);
            return WF_OK;
        }
        WF_LOG_DEBUG("crypto",
                     "wf_signing_key_generate: secp256k1_ec_seckey_verify "
                     "failed on attempt %d, retrying",
                     attempt);
    }

    WF_LOG_ERROR("crypto", "wf_signing_key_generate: failed to generate valid "
                           "secp256k1 key after 10 attempts");
    secp256k1_context_destroy(ctx);
    memset(out, 0, sizeof(*out));
    return WF_ERR_ALLOC;
#else
    WF_LOG_ERROR("crypto", "wf_signing_key_generate: secp256k1 not available");
    (void)type;
    return WF_ERR_INVALID_ARG;
#endif
}

wf_status wf_sign(const wf_signing_key *key, const unsigned char *msg,
                  size_t msg_len, unsigned char *sig_out, size_t sig_out_cap) {
    WF_LOG_DEBUG("crypto",
                 "wf_sign: enter, key_type=%d, msg_len=%zu, sig_cap=%zu",
                 key ? (int)key->type : -1, msg_len, sig_out_cap);

    if (!key || !msg || msg_len == 0 || !sig_out || sig_out_cap < 64) {
        WF_LOG_ERROR("crypto", "wf_sign: invalid args");
        return WF_ERR_INVALID_ARG;
    }

    if (key->type == WF_KEY_TYPE_P256) {
        WF_LOG_DEBUG("crypto", "wf_sign: signing with P-256");
        EVP_PKEY *pkey = NULL;
        wf_status status = wf_p256_pkey_from_private(key->bytes, &pkey);
        if (status != WF_OK) {
            WF_LOG_ERROR("crypto", "wf_sign: failed to build P-256 key");
            return status;
        }

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        status = wf_p256_sign_hash(pkey, hash, sig_out);
        EVP_PKEY_free(pkey);
        if (status != WF_OK) {
            WF_LOG_ERROR("crypto", "wf_sign: P-256 signing failed status=%d",
                         status);
            return status;
        }
        WF_LOG_DEBUG("crypto", "wf_sign: P-256 signature created successfully");
        return WF_OK;
    }

#ifdef HAVE_LIBSECP256K1
    if (key->type == WF_KEY_TYPE_SECP256K1) {
        WF_LOG_DEBUG("crypto", "wf_sign: signing with secp256k1");
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        if (!ctx) {
            WF_LOG_ERROR("crypto", "wf_sign: secp256k1_context_create failed");
            return WF_ERR_ALLOC;
        }

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        secp256k1_ecdsa_signature sig;
        if (secp256k1_ecdsa_sign(ctx, &sig, hash, key->bytes,
                                 secp256k1_nonce_function_rfc6979, NULL) != 1) {
            WF_LOG_ERROR("crypto", "wf_sign: secp256k1_ecdsa_sign failed");
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }

        secp256k1_ecdsa_signature_serialize_compact(ctx, sig_out, &sig);
        secp256k1_context_destroy(ctx);
        WF_LOG_DEBUG("crypto",
                     "wf_sign: secp256k1 signature created successfully");
        return WF_OK;
    }
#endif

    WF_LOG_ERROR("crypto", "wf_sign: unsupported key type %d", key->type);
    return WF_ERR_INVALID_ARG;
}

static wf_status wf_verify_internal(const char *public_key_multibase,
                                    const unsigned char *msg, size_t msg_len,
                                    const unsigned char *sig, size_t sig_len,
                                    int allow_malleable) {
    if (!public_key_multibase || !msg || msg_len == 0 || !sig || sig_len == 0) {
        return WF_ERR_INVALID_ARG;
    }

    /* Parse did:key:z... or bare z<base58btc(multicodec || pubkey)>. */
    const char *encoded = public_key_multibase;
    if (strncmp(encoded, "did:key:", 8) == 0) encoded += 8;
    if (*encoded++ != 'z' || *encoded == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    static const char alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    unsigned char pk_buf[67] = {0};
    size_t pk_len = 0;
    for (const char *p = encoded; *p; p++) {
        const char *digit = strchr(alphabet, *p);
        if (!digit) return WF_ERR_INVALID_ARG;
        unsigned carry = (unsigned)(digit - alphabet);
        for (size_t i = 0; i < pk_len; i++) {
            size_t at = pk_len - 1 - i;
            unsigned value = (unsigned)pk_buf[at] * 58u + carry;
            pk_buf[at] = (unsigned char)(value & 0xffu);
            carry = value >> 8;
        }
        while (carry) {
            if (pk_len == sizeof(pk_buf)) return WF_ERR_INVALID_ARG;
            memmove(pk_buf + 1, pk_buf, pk_len++);
            pk_buf[0] = (unsigned char)(carry & 0xffu);
            carry >>= 8;
        }
    }
    for (const char *p = encoded; *p == '1'; p++) {
        if (pk_len == sizeof(pk_buf)) return WF_ERR_INVALID_ARG;
        memmove(pk_buf + 1, pk_buf, pk_len++);
        pk_buf[0] = 0;
    }

    /* Multicodec varints: secp256k1-pub=e7 01, p256-pub=80 24. */
    int is_p256 = 0;
    unsigned char *raw_pk = NULL;
    size_t raw_pk_len = 0;

    if (pk_len == 35 && pk_buf[0] == 0x80 && pk_buf[1] == 0x24) {
        is_p256 = 1;
        raw_pk = pk_buf + 2;
        raw_pk_len = 33;
    } else if (pk_len == 35 && pk_buf[0] == 0xe7 && pk_buf[1] == 0x01) {
        raw_pk = pk_buf + 2;
        raw_pk_len = 33;
    } else {
        return WF_ERR_INVALID_ARG;
    }

    if (is_p256) {
        if (sig_len != 64) return WF_ERR_INVALID_ARG;

        EVP_PKEY *pkey = NULL;
        wf_status status =
            wf_p256_pkey_from_public_point(raw_pk, raw_pk_len, &pkey);
        if (status != WF_OK) return WF_ERR_PARSE;

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        status = wf_p256_verify_hash_pkey(pkey, hash, sig, allow_malleable);
        EVP_PKEY_free(pkey);
        return status;
    }

#ifdef HAVE_LIBSECP256K1
    {
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        if (!ctx) return WF_ERR_ALLOC;

        secp256k1_pubkey pubkey;
        if (secp256k1_ec_pubkey_parse(ctx, &pubkey, raw_pk, raw_pk_len) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_PARSE;
        }

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        if (sig_len != 64) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_INVALID_ARG;
        }
        secp256k1_ecdsa_signature ecdsa_sig;
        secp256k1_ecdsa_signature normalized_sig;
        if (secp256k1_ecdsa_signature_parse_compact(ctx, &ecdsa_sig, sig) !=
            1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_PARSE;
        }
        if (secp256k1_ecdsa_signature_normalize(ctx, &normalized_sig,
                                                &ecdsa_sig) &&
            !allow_malleable) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_PARSE;
        }

        int valid = secp256k1_ecdsa_verify(ctx, &normalized_sig, hash, &pubkey);
        secp256k1_context_destroy(ctx);
        return valid ? WF_OK : WF_ERR_PARSE;
    }
#else
    return WF_ERR_INVALID_ARG;
#endif
}

wf_status wf_verify(const char *public_key_multibase, const unsigned char *msg,
                    size_t msg_len, const unsigned char *sig, size_t sig_len) {
    return wf_verify_internal(public_key_multibase, msg, msg_len, sig, sig_len,
                              0);
}

wf_status wf_verify_allow_malleable(const char *public_key_multibase,
                                    const unsigned char *msg, size_t msg_len,
                                    const unsigned char *sig, size_t sig_len) {
    return wf_verify_internal(public_key_multibase, msg, msg_len, sig, sig_len,
                              1);
}

/* ------------------------------------------------------------------ */
/* Generic P-256 / hashing / base64url helpers                         */
/* ------------------------------------------------------------------ */

wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
                           unsigned char out[32]) {
    if (!in || !out) return WF_ERR_INVALID_ARG;
    SHA256(in, len, out);
    return WF_OK;
}

wf_status wf_crypto_base64url_decode(const char *in, unsigned char **out,
                                     size_t *out_len) {
    size_t in_len, padded_len, padding, i;
    char *padded = NULL;
    unsigned char *decoded = NULL;
    int result;
    if (!in || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    in_len = strlen(in);
    if (in_len == 0) return WF_ERR_PARSE;
    padded_len = ((in_len + 3) / 4) * 4;
    padded = malloc(padded_len + 1);
    decoded = malloc(padded_len + 1);
    if (!padded || !decoded) {
        free(padded);
        free(decoded);
        return WF_ERR_ALLOC;
    }
    for (i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        else if (!isalnum((unsigned char)c) && c != '+' && c != '/') {
            free(padded);
            free(decoded);
            return WF_ERR_PARSE;
        }
        padded[i] = c;
    }
    for (; i < padded_len; i++) padded[i] = '=';
    padded[padded_len] = '\0';
    result = EVP_DecodeBlock(decoded, (const unsigned char *)padded,
                             (int)padded_len);
    padding = padded_len - in_len;
    free(padded);
    if (result < 0 || (size_t)result < padding) {
        free(decoded);
        return WF_ERR_PARSE;
    }
    *out_len = (size_t)result - padding;
    *out = decoded;
    return WF_OK;
}

wf_status wf_crypto_base64url_encode(const unsigned char *in, size_t len,
                                     char **out) {
    size_t padded_len;
    char *padded = NULL, *b64 = NULL;
    size_t i, out_len;
    if (!in || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    if (len == 0) {
        b64 = calloc(1, 1);
        if (!b64) return WF_ERR_ALLOC;
        *out = b64;
        return WF_OK;
    }
    padded_len = ((len + 2) / 3) * 4;
    padded = malloc(padded_len + 1);
    if (!padded) return WF_ERR_ALLOC;
    EVP_EncodeBlock((unsigned char *)padded, in, (int)len);
    /* Translate standard base64 to base64url (no padding). */
    for (i = 0; i < padded_len; i++) {
        char c = padded[i];
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
        else if (c == '=')
            break;
        padded[i] = c;
    }
    out_len = i;
    b64 = malloc(out_len + 1);
    if (!b64) {
        free(padded);
        return WF_ERR_ALLOC;
    }
    memcpy(b64, padded, out_len);
    b64[out_len] = '\0';
    free(padded);
    *out = b64;
    return WF_OK;
}

/* Verify a raw 64-byte (r||s) ES256 signature over a precomputed SHA-256
 * `hash` using the P-256 public key at affine coordinates x, y. */
static wf_status wf_p256_verify_hash(const unsigned char x[32],
                                     const unsigned char y[32],
                                     const unsigned char hash[32],
                                     const unsigned char *sig, size_t sig_len) {
    unsigned char point_oct[65];
    EVP_PKEY *pkey = NULL;
    wf_status status;

    if (!x || !y || !hash || !sig || sig_len != 64) return WF_ERR_INVALID_ARG;
    point_oct[0] = 0x04;
    memcpy(point_oct + 1, x, 32);
    memcpy(point_oct + 33, y, 32);

    status =
        wf_p256_pkey_from_public_point(point_oct, sizeof(point_oct), &pkey);
    if (status != WF_OK) return WF_ERR_PARSE;

    status = wf_p256_verify_hash_pkey(pkey, hash, sig, /*allow_malleable=*/0);
    EVP_PKEY_free(pkey);
    return status;
}

wf_status wf_crypto_p256_verify(const unsigned char x[32],
                                const unsigned char y[32],
                                const unsigned char *msg, size_t msg_len,
                                const unsigned char *sig, size_t sig_len) {
    unsigned char hash[32];
    if (!msg || msg_len == 0) return WF_ERR_INVALID_ARG;
    SHA256(msg, msg_len, hash);
    return wf_p256_verify_hash(x, y, hash, sig, sig_len);
}

wf_status wf_crypto_p256_jwk_coords(const char *jwk_json, unsigned char x[32],
                                    unsigned char y[32]) {
    cJSON *root = NULL, *item;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    wf_status status;
    if (!jwk_json || !x || !y) return WF_ERR_INVALID_ARG;
    root = cJSON_Parse(jwk_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "kty");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, "EC") != 0) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "crv");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, "P-256") != 0) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "x");
    if (!cJSON_IsString(item)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_crypto_base64url_decode(item->valuestring, &raw, &raw_len);
    if (status != WF_OK || raw_len != 32) {
        free(raw);
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    memcpy(x, raw, 32);
    free(raw);
    raw = NULL;
    item = cJSON_GetObjectItemCaseSensitive(root, "y");
    if (!cJSON_IsString(item)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_crypto_base64url_decode(item->valuestring, &raw, &raw_len);
    if (status != WF_OK || raw_len != 32) {
        free(raw);
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    memcpy(y, raw, 32);
    free(raw);
    cJSON_Delete(root);
    return WF_OK;
}
