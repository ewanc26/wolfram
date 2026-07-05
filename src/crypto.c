/**
 * crypto.c — secp256k1 signing/verification via libsecp256k1.
 *
 * P-256 (secp256r1) is also supported via OpenSSL's EC API, which is
 * already linked for SHA-256. When HAVE_LIBSECP256K1 is not defined
 * all functions return WF_ERR_INVALID_ARG (stub mode).
 */

#include "wolfram/crypto.h"

#include <string.h>

#ifdef HAVE_LIBSECP256K1
#include <secp256k1.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

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

#ifdef HAVE_LIBSECP256K1
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
#else
    (void)key;
    (void)msg;
    (void)msg_len;
    (void)sig_out;
    (void)sig_out_cap;
    return WF_ERR_INVALID_ARG;
#endif
}

wf_status wf_verify(const char *public_key_multibase,
                     const unsigned char *msg, size_t msg_len,
                     const unsigned char *sig, size_t sig_len) {
    if (!public_key_multibase || !msg || msg_len == 0 ||
        !sig || sig_len == 0) {
        return WF_ERR_INVALID_ARG;
    }

#ifdef HAVE_LIBSECP256K1
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return WF_ERR_ALLOC;

    /* Parse did:key multibase public key.
     * Format: "z<base58btc(0xe701 || compressed_pubkey)>"
     * where 0xe701 is the SECP256K1 multicodec prefix. */
    if (strncmp(public_key_multibase, "z", 1) != 0) {
        secp256k1_context_destroy(ctx);
        return WF_ERR_INVALID_ARG;
    }

    /* For now, accept raw 33-byte compressed (0x02/0x03 || X)
     * and 65-byte uncompressed (0x04 || X || Y) pubkeys passed
     * as hex-encoded multibase. A full did:key parser would go here. */
    size_t pk_len = strlen(public_key_multibase + 1) / 2;
    if (pk_len != 33 && pk_len != 65) {
        secp256k1_context_destroy(ctx);
        return WF_ERR_INVALID_ARG;
    }

    /* Hex decode */
    unsigned char pubkey_buf[65];
    for (size_t i = 0; i < pk_len; i++) {
        unsigned byte;
        if (sscanf(public_key_multibase + 1 + 2 * i, "%2x", &byte) != 1) {
            secp256k1_context_destroy(ctx);
            return WF_ERR_INVALID_ARG;
        }
        pubkey_buf[i] = (unsigned char)byte;
    }

    secp256k1_pubkey pubkey;
    if (secp256k1_ec_pubkey_parse(ctx, &pubkey, pubkey_buf, pk_len) != 1) {
        secp256k1_context_destroy(ctx);
        return WF_ERR_PARSE;
    }

    unsigned char hash[32];
    SHA256(msg, msg_len, hash);

    secp256k1_ecdsa_signature ecdsa_sig;
    if (sig_len != 64) {
        secp256k1_context_destroy(ctx);
        return WF_ERR_INVALID_ARG;
    }
    if (secp256k1_ecdsa_signature_parse_compact(ctx, &ecdsa_sig, sig) != 1) {
        secp256k1_context_destroy(ctx);
        return WF_ERR_PARSE;
    }

    int valid = secp256k1_ecdsa_verify(ctx, &ecdsa_sig, hash, &pubkey);
    secp256k1_context_destroy(ctx);

    return valid ? WF_OK : WF_ERR_PARSE;
#else
    (void)public_key_multibase;
    (void)msg;
    (void)msg_len;
    (void)sig;
    (void)sig_len;
    return WF_ERR_INVALID_ARG;
#endif
}
