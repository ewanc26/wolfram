/*
 * HPKE secret export for exactly one suite, spelled out in ECDH and HKDF as the
 * proposal's appendix and @atproto/space do, so it does not depend on the
 * OpenSSL version having an HPKE API.
 */

#include "wolfram/spaces/kem.h"

#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>

#include "../crypto/crypto_internal.h"

#define POINT_BYTES 65

/* 0x0010 DHKEM(P-256, HKDF-SHA256), 0x0001 HKDF-SHA256, 0xffff export-only. */
static const unsigned char KEM_ID[] = {'K', 'E', 'M', 0x00, 0x10};
static const unsigned char SUITE_ID[] = {'H',  'P',  'K',  'E',  0x00,
                                         0x10, 0x00, 0x01, 0xff, 0xff};

typedef struct buffer {
    unsigned char bytes[256];
    size_t len;
} buffer;

static void put(buffer *b, const void *data, size_t len) {
    /* Every input here is a fixed size well under the buffer. */
    if (len) {
        memcpy(b->bytes + b->len, data, len);
        b->len += len;
    }
}

static int hmac_sha256(const unsigned char *key, size_t key_len,
                       const unsigned char *data, size_t data_len,
                       unsigned char out[32]) {
    unsigned int out_len = 32;
    return HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out,
                &out_len) != NULL &&
           out_len == 32;
}

static int labeled_extract(const unsigned char *id, size_t id_len,
                           const unsigned char *salt, size_t salt_len,
                           const char *label, const void *ikm, size_t ikm_len,
                           unsigned char prk[32]) {
    static const unsigned char empty[1] = {0};
    buffer b = {{0}, 0};
    int ok;

    put(&b, "HPKE-v1", 7);
    put(&b, id, id_len);
    put(&b, label, strlen(label));
    put(&b, ikm, ikm_len);
    ok = hmac_sha256(salt ? salt : empty, salt_len, b.bytes, b.len, prk);
    OPENSSL_cleanse(&b, sizeof(b));
    return ok;
}

/* LabeledExpand for at most one hash block, which is all this suite needs. */
static int labeled_expand(const unsigned char *id, size_t id_len,
                          const unsigned char prk[32], const char *label,
                          const unsigned char *info, size_t info_len,
                          unsigned char out[32]) {
    static const unsigned char length[2] = {0x00, 0x20};
    static const unsigned char counter = 0x01;
    buffer b = {{0}, 0};
    int ok;

    put(&b, length, sizeof(length));
    put(&b, "HPKE-v1", 7);
    put(&b, id, id_len);
    put(&b, label, strlen(label));
    put(&b, info, info_len);
    put(&b, &counter, 1);
    ok = hmac_sha256(prk, 32, b.bytes, b.len, out);
    OPENSSL_cleanse(&b, sizeof(b));
    return ok;
}

/* SetupBaseS/R followed by Export, given the ECDH result. */
static int export_secret(const unsigned char dh[32],
                         const unsigned char enc[POINT_BYTES],
                         const unsigned char recipient[POINT_BYTES],
                         unsigned char secret[32]) {
    unsigned char eae_prk[32];
    unsigned char kem_secret[32];
    unsigned char psk_id_hash[32];
    unsigned char info_hash[32];
    unsigned char schedule_prk[32];
    unsigned char exporter[32];
    unsigned char kem_context[2 * POINT_BYTES];
    unsigned char context[1 + 2 * 32];
    int ok;

    memcpy(kem_context, enc, POINT_BYTES);
    memcpy(kem_context + POINT_BYTES, recipient, POINT_BYTES);

    ok = labeled_extract(KEM_ID, sizeof(KEM_ID), NULL, 0, "eae_prk", dh, 32,
                         eae_prk) &&
         labeled_expand(KEM_ID, sizeof(KEM_ID), eae_prk, "shared_secret",
                        kem_context, sizeof(kem_context), kem_secret) &&
         labeled_extract(SUITE_ID, sizeof(SUITE_ID), NULL, 0, "psk_id_hash",
                         NULL, 0, psk_id_hash) &&
         labeled_extract(SUITE_ID, sizeof(SUITE_ID), NULL, 0, "info_hash",
                         "atproto-mac", 11, info_hash);
    if (ok) {
        context[0] = 0x00; /* base mode */
        memcpy(context + 1, psk_id_hash, 32);
        memcpy(context + 33, info_hash, 32);
        ok = labeled_extract(SUITE_ID, sizeof(SUITE_ID), kem_secret, 32,
                             "secret", NULL, 0, schedule_prk) &&
             labeled_expand(SUITE_ID, sizeof(SUITE_ID), schedule_prk, "exp",
                            context, sizeof(context), exporter) &&
             labeled_expand(SUITE_ID, sizeof(SUITE_ID), exporter, "sec", NULL,
                            0, secret);
    }
    OPENSSL_cleanse(eae_prk, sizeof(eae_prk));
    OPENSSL_cleanse(kem_secret, sizeof(kem_secret));
    OPENSSL_cleanse(schedule_prk, sizeof(schedule_prk));
    OPENSSL_cleanse(exporter, sizeof(exporter));
    return ok;
}

/* The uncompressed point of a P-256 did:key; HPKE encodes keys that way. */
static wf_status didkey_point(const char *didkey,
                              unsigned char out[POINT_BYTES]) {
    wf_key_type type;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    EC_GROUP *group = NULL;
    EC_POINT *point = NULL;
    wf_status status = wf_didkey_decode(didkey, &type, &raw, &raw_len);

    if (status != WF_OK) {
        return status;
    }
    if (type != WF_KEY_TYPE_P256) {
        free(raw);
        return WF_ERR_INVALID_ARG;
    }
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    point = group ? EC_POINT_new(group) : NULL;
    if (!point) {
        status = WF_ERR_ALLOC;
    } else if (EC_POINT_oct2point(group, point, raw, raw_len, NULL) != 1 ||
               EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED,
                                  out, POINT_BYTES, NULL) != POINT_BYTES) {
        status = WF_ERR_PARSE;
    }
    EC_POINT_free(point);
    EC_GROUP_free(group);
    free(raw);
    return status;
}

static wf_status ecdh(const wf_signing_key *own,
                      const unsigned char peer[POINT_BYTES],
                      unsigned char dh[32]) {
    EVP_PKEY *priv = NULL;
    EVP_PKEY *pub = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    size_t len = 32;
    wf_status status = wf_p256_pkey_from_private(own->bytes, &priv);

    if (status == WF_OK) {
        status = wf_p256_pkey_from_public_point(peer, POINT_BYTES, &pub);
    }
    if (status == WF_OK) {
        ctx = EVP_PKEY_CTX_new_from_pkey(NULL, priv, NULL);
        if (!ctx || EVP_PKEY_derive_init(ctx) != 1 ||
            EVP_PKEY_derive_set_peer(ctx, pub) != 1 ||
            EVP_PKEY_derive(ctx, dh, &len) != 1 || len != 32) {
            status = WF_ERR_CRYPTO;
        }
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pub);
    EVP_PKEY_free(priv);
    return status;
}

/* The uncompressed public point of a private key. */
static wf_status own_point(const wf_signing_key *key, char **out_didkey,
                           unsigned char out[POINT_BYTES]) {
    char *didkey = NULL;
    wf_status status = wf_signing_key_public_didkey(key, &didkey);

    if (status == WF_OK) {
        status = didkey_point(didkey, out);
    }
    if (status == WF_OK && out_didkey) {
        *out_didkey = didkey;
    } else {
        free(didkey);
    }
    return status;
}

wf_status wf_space_kem_encapsulate(const char *author_kem_key,
                                   char **out_key_share,
                                   unsigned char secret[32]) {
    unsigned char recipient[POINT_BYTES];
    unsigned char enc[POINT_BYTES];
    unsigned char dh[32];
    wf_signing_key ephemeral;
    char *share = NULL;
    wf_status status;

    if (out_key_share) {
        *out_key_share = NULL;
    }
    if (secret) {
        memset(secret, 0, 32);
    }
    if (!author_kem_key || !out_key_share || !secret) {
        return WF_ERR_INVALID_ARG;
    }
    status = didkey_point(author_kem_key, recipient);
    if (status != WF_OK) {
        return status;
    }
    status = wf_signing_key_generate(WF_KEY_TYPE_P256, &ephemeral);
    if (status == WF_OK) {
        status = own_point(&ephemeral, &share, enc);
    }
    if (status == WF_OK) {
        status = ecdh(&ephemeral, recipient, dh);
    }
    if (status == WF_OK && !export_secret(dh, enc, recipient, secret)) {
        status = WF_ERR_CRYPTO;
    }
    OPENSSL_cleanse(&ephemeral, sizeof(ephemeral));
    OPENSSL_cleanse(dh, sizeof(dh));
    if (status == WF_OK) {
        *out_key_share = share;
    } else {
        free(share);
        OPENSSL_cleanse(secret, 32);
    }
    return status;
}

wf_status wf_space_kem_decapsulate(const wf_signing_key *kem_key,
                                   const char *key_share,
                                   unsigned char secret[32]) {
    unsigned char recipient[POINT_BYTES];
    unsigned char enc[POINT_BYTES];
    unsigned char dh[32];
    wf_status status;

    if (secret) {
        memset(secret, 0, 32);
    }
    if (!kem_key || !key_share || !secret ||
        kem_key->type != WF_KEY_TYPE_P256) {
        return WF_ERR_INVALID_ARG;
    }
    status = didkey_point(key_share, enc);
    if (status == WF_OK) {
        status = own_point(kem_key, NULL, recipient);
    }
    if (status == WF_OK) {
        status = ecdh(kem_key, enc, dh);
    }
    if (status == WF_OK && !export_secret(dh, enc, recipient, secret)) {
        status = WF_ERR_CRYPTO;
    }
    OPENSSL_cleanse(dh, sizeof(dh));
    if (status != WF_OK) {
        OPENSSL_cleanse(secret, 32);
    }
    return status;
}
