/**
 * crypto.c — secp256k1 signing/verification via libsecp256k1,
 *             P-256 signing/verification via OpenSSL EC API.
 */

#include "wolfram/crypto.h"

#include <string.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

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
