/**
 * crypto.c — secp256k1 signing/verification via libsecp256k1,
 *             P-256 signing/verification via OpenSSL EC API.
 */

#include "wolfram/crypto.h"

#include <string.h>
#include <ctype.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cJSON.h>

static wf_status wf_p256_is_low_s(const BIGNUM *s, const EC_KEY *eckey) {
    const BIGNUM *order;
    BIGNUM *half_order;
    int low;

    if (!s || !eckey) return WF_ERR_INVALID_ARG;
    order = EC_GROUP_get0_order(EC_KEY_get0_group(eckey));
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

static wf_status wf_p256_normalize_s(const EC_KEY *eckey, BIGNUM **s_io) {
    const BIGNUM *order;
    BIGNUM *half_order;
    BIGNUM *s;

    if (!eckey || !s_io || !*s_io) return WF_ERR_INVALID_ARG;
    s = *s_io;
    order = EC_GROUP_get0_order(EC_KEY_get0_group(eckey));
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
    const unsigned char *multicodec;
    char *b58;
    char *didkey;

    if (!key || !out_didkey) return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;

    if (key->type == WF_KEY_TYPE_P256) {
        EC_KEY *eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        const EC_GROUP *group;
        const BIGNUM *priv;
        EC_POINT *point;
        BIGNUM *bn_priv;
        size_t n;

        if (!eckey) return WF_ERR_ALLOC;
        group = EC_KEY_get0_group(eckey);
        bn_priv = BN_bin2bn(key->bytes, 32, NULL);
        if (!bn_priv || EC_KEY_set_private_key(eckey, bn_priv) != 1) {
            BN_free(bn_priv);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        priv = EC_KEY_get0_private_key(eckey);
        point = EC_POINT_new(group);
        if (!point || EC_POINT_mul(group, point, priv, NULL, NULL, NULL) != 1) {
            EC_POINT_free(point);
            BN_free(bn_priv);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        n = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
                               raw, sizeof(raw), NULL);
        EC_POINT_free(point);
        BN_free(bn_priv);
        EC_KEY_free(eckey);
        if (n != 33) return WF_ERR_ALLOC;
        prefixed[0] = 0x80;
        prefixed[1] = 0x24;
        memcpy(prefixed + 2, raw, 33);
    } else if (key->type == WF_KEY_TYPE_SECP256K1) {
#ifdef HAVE_LIBSECP256K1
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        secp256k1_pubkey pubkey;
        size_t clen = 33;
        if (!ctx) return WF_ERR_ALLOC;
        if (secp256k1_ec_pubkey_create(ctx, &pubkey, key->bytes) != 1 ||
            secp256k1_ec_pubkey_serialize(ctx, raw, &clen, &pubkey,
                                          SECP256K1_EC_COMPRESSED) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }
        secp256k1_context_destroy(ctx);
        prefixed[0] = 0xe7;
        prefixed[1] = 0x01;
        memcpy(prefixed + 2, raw, 33);
#else
        (void)multicodec;
        (void)raw;
        return WF_ERR_INVALID_ARG; /* TODO: secp256k1 support needs libsecp256k1 */
#endif
    } else {
        return WF_ERR_INVALID_ARG;
    }

    b58 = wf_b58_encode(prefixed, sizeof(prefixed));
    if (!b58) return WF_ERR_ALLOC;

    didkey = malloc(strlen("did:key:z") + strlen(b58) + 1);
    if (!didkey) {
        free(b58);
        return WF_ERR_ALLOC;
    }
    sprintf(didkey, "did:key:z%s", b58);
    free(b58);

    *out_didkey = didkey;
    return WF_OK;
}

/* base58btc (multibase 'z') decoder — inverse of wf_b58_encode. */
static wf_status wf_b58_decode(const char *str, size_t len,
                               unsigned char **out, size_t *out_len) {
    if (!str || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL; *out_len = 0;
    if (len == 0) return WF_ERR_INVALID_ARG;

    size_t zeros = 0;
    while (zeros < len && str[zeros] == '1') zeros++;

    /* Conservative upper bound: log_256(58) < 0.733 per input char. */
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
    if (!raw_pub || raw_len != 33 || !out_didkey)
        return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;

    if (type == WF_KEY_TYPE_P256) {
        prefixed[0] = 0x80; prefixed[1] = 0x24;
    } else if (type == WF_KEY_TYPE_SECP256K1) {
        prefixed[0] = 0xe7; prefixed[1] = 0x01;
    } else {
        return WF_ERR_INVALID_ARG;
    }
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
    *out_raw = NULL; *out_raw_len = 0;

    const char *payload = didkey;
    if (strncmp(payload, "did:key:", 8) == 0) payload += 8;
    else if (payload[0] == 'z') payload += 1; /* bare multikey form */
    else return WF_ERR_INVALID_ARG;
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
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    if (type == WF_KEY_TYPE_P256) {
        EC_KEY *eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        if (!eckey) return WF_ERR_ALLOC;
        if (EC_KEY_generate_key(eckey) != 1) {
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        const BIGNUM *priv = EC_KEY_get0_private_key(eckey);
        if (!priv || BN_bn2binpad(priv, out->bytes, 32) != 32) {
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        out->type = WF_KEY_TYPE_P256;
        EC_KEY_free(eckey);
        return WF_OK;
    }

    if (type != WF_KEY_TYPE_SECP256K1) {
        return WF_ERR_INVALID_ARG;
    }

#ifdef HAVE_LIBSECP256K1
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return WF_ERR_ALLOC;

    /* Try up to 10 times to generate a valid key */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (RAND_bytes(out->bytes, 32) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }
        if (secp256k1_ec_seckey_verify(ctx, out->bytes)) {
            out->type = WF_KEY_TYPE_SECP256K1;
            secp256k1_context_destroy(ctx);
            return WF_OK;
        }
    }

    secp256k1_context_destroy(ctx);
    memset(out, 0, sizeof(*out));
    return WF_ERR_ALLOC;
#else
    (void)type;
    return WF_ERR_INVALID_ARG;
#endif
}

wf_status wf_sign(const wf_signing_key *key,
                   const unsigned char *msg, size_t msg_len,
                   unsigned char *sig_out, size_t sig_out_cap) {
    if (!key || !msg || msg_len == 0 || !sig_out || sig_out_cap < 64) {
        return WF_ERR_INVALID_ARG;
    }

    if (key->type == WF_KEY_TYPE_P256) {
        EC_KEY *eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        if (!eckey) return WF_ERR_ALLOC;

        BIGNUM *priv = BN_bin2bn(key->bytes, 32, NULL);
        if (!priv || EC_KEY_set_private_key(eckey, priv) != 1) {
            BN_free(priv);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        BN_free(priv);

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        ECDSA_SIG *sig = ECDSA_do_sign(hash, 32, eckey);
        if (!sig) {
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }

        const BIGNUM *r0, *s0;
        BIGNUM *r = NULL;
        BIGNUM *s = NULL;
        wf_status status = WF_OK;

        ECDSA_SIG_get0(sig, &r0, &s0);
        if (!r0 || !s0) {
            ECDSA_SIG_free(sig);
            return WF_ERR_ALLOC;
        }

        r = BN_dup(r0);
        s = BN_dup(s0);
        if (!r || !s) {
            BN_free(r);
            BN_free(s);
            ECDSA_SIG_free(sig);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }

        status = wf_p256_normalize_s(eckey, &s);
        if (status != WF_OK) {
            BN_free(r);
            BN_free(s);
            ECDSA_SIG_free(sig);
            EC_KEY_free(eckey);
            return status;
        }

        if (BN_bn2binpad(r, sig_out, 32) != 32 ||
            BN_bn2binpad(s, sig_out + 32, 32) != 32) {
            BN_free(r);
            BN_free(s);
            ECDSA_SIG_free(sig);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }

        BN_free(r);
        BN_free(s);
        ECDSA_SIG_free(sig);
        EC_KEY_free(eckey);
        return WF_OK;
    }

#ifdef HAVE_LIBSECP256K1
    if (key->type == WF_KEY_TYPE_SECP256K1) {
        secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        if (!ctx) return WF_ERR_ALLOC;

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        secp256k1_ecdsa_signature sig;
        if (secp256k1_ecdsa_sign(ctx, &sig, hash, key->bytes,
                                   secp256k1_nonce_function_rfc6979, NULL) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_ALLOC;
        }

        secp256k1_ecdsa_signature_serialize_compact(ctx, sig_out, &sig);
        secp256k1_context_destroy(ctx);
        return WF_OK;
    }
#endif

    return WF_ERR_INVALID_ARG;
}

wf_status wf_verify(const char *public_key_multibase,
                     const unsigned char *msg, size_t msg_len,
                     const unsigned char *sig, size_t sig_len) {
    if (!public_key_multibase || !msg || msg_len == 0 ||
        !sig || sig_len == 0) {
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
        EC_KEY *eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        if (!eckey) return WF_ERR_ALLOC;

        /* Compressed 33-byte P-256 point (0x02/0x03 || X) */
        EC_POINT *point = EC_POINT_new(EC_KEY_get0_group(eckey));
        if (!point) { EC_KEY_free(eckey); return WF_ERR_ALLOC; }
        if (EC_POINT_oct2point(EC_KEY_get0_group(eckey), point,
                                raw_pk, raw_pk_len, NULL) != 1) {
            EC_POINT_free(point);
            EC_KEY_free(eckey);
            return WF_ERR_PARSE;
        }
        if (EC_KEY_set_public_key(eckey, point) != 1) {
            EC_POINT_free(point);
            EC_KEY_free(eckey);
            return WF_ERR_PARSE;
        }
        EC_POINT_free(point);

        unsigned char hash[32];
        SHA256(msg, msg_len, hash);

        ECDSA_SIG *ecdsa_sig = ECDSA_SIG_new();
        if (!ecdsa_sig) { EC_KEY_free(eckey); return WF_ERR_ALLOC; }
        if (sig_len != 64) {
            ECDSA_SIG_free(ecdsa_sig);
            EC_KEY_free(eckey);
            return WF_ERR_INVALID_ARG;
        }
        BIGNUM *r = BN_bin2bn(sig, 32, NULL);
        BIGNUM *s = BN_bin2bn(sig + 32, 32, NULL);
        if (!r || !s) {
            BN_free(r); BN_free(s);
            ECDSA_SIG_free(ecdsa_sig);
            EC_KEY_free(eckey);
            return WF_ERR_ALLOC;
        }
        ECDSA_SIG_set0(ecdsa_sig, r, s);

        if (wf_p256_is_low_s(s, eckey) != WF_OK) {
            ECDSA_SIG_free(ecdsa_sig);
            EC_KEY_free(eckey);
            return WF_ERR_PARSE;
        }

        int ok = ECDSA_do_verify(hash, 32, ecdsa_sig, eckey);
        ECDSA_SIG_free(ecdsa_sig);
        EC_KEY_free(eckey);
        return ok == 1 ? WF_OK : WF_ERR_PARSE;
    }

#ifdef HAVE_LIBSECP256K1
    {
        secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
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
        if (secp256k1_ecdsa_signature_parse_compact(ctx, &ecdsa_sig, sig) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_PARSE;
        }
        if (secp256k1_ecdsa_signature_normalize(ctx, &normalized_sig, &ecdsa_sig)) {
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

/* ------------------------------------------------------------------ */
/* Generic P-256 / hashing / base64url helpers                         */
/* ------------------------------------------------------------------ */

wf_status wf_crypto_sha256(const unsigned char *in, size_t len,
                           unsigned char out[32]) {
    if (!in || !out) return WF_ERR_INVALID_ARG;
    SHA256(in, len, out);
    return WF_OK;
}

wf_status wf_crypto_base64url_decode(const char *in,
                                    unsigned char **out, size_t *out_len) {
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
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
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
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
        else if (c == '=') break;
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
    EC_KEY *eckey = NULL;
    EC_POINT *point = NULL;
    ECDSA_SIG *ecdsa_sig = NULL;
    BIGNUM *r = NULL, *s = NULL;
    unsigned char point_oct[65];
    int ok;
    wf_status status = WF_ERR_PARSE;

    if (!x || !y || !hash || !sig || sig_len != 64) return WF_ERR_INVALID_ARG;
    eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!eckey) return WF_ERR_ALLOC;
    point = EC_POINT_new(EC_KEY_get0_group(eckey));
    if (!point) goto done;
    point_oct[0] = 0x04;
    memcpy(point_oct + 1, x, 32);
    memcpy(point_oct + 33, y, 32);
    if (EC_POINT_oct2point(EC_KEY_get0_group(eckey), point, point_oct,
                           sizeof(point_oct), NULL) != 1) {
        goto done;
    }
    if (EC_KEY_set_public_key(eckey, point) != 1) goto done;

    ecdsa_sig = ECDSA_SIG_new();
    if (!ecdsa_sig) { status = WF_ERR_ALLOC; goto done; }
    r = BN_bin2bn(sig, 32, NULL);
    s = BN_bin2bn(sig + 32, 32, NULL);
    if (!r || !s) { status = WF_ERR_ALLOC; goto done; }
    if (wf_p256_is_low_s(s, eckey) != WF_OK) goto done;
    ECDSA_SIG_set0(ecdsa_sig, r, s);
    r = NULL;
    s = NULL;

    ok = ECDSA_do_verify(hash, 32, ecdsa_sig, eckey);
    status = ok == 1 ? WF_OK : WF_ERR_PARSE;
done:
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(ecdsa_sig);
    EC_POINT_free(point);
    EC_KEY_free(eckey);
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

wf_status wf_crypto_p256_jwk_coords(const char *jwk_json,
                                    unsigned char x[32], unsigned char y[32]) {
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
    if (!cJSON_IsString(item)) { cJSON_Delete(root); return WF_ERR_PARSE; }
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
    if (!cJSON_IsString(item)) { cJSON_Delete(root); return WF_ERR_PARSE; }
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
