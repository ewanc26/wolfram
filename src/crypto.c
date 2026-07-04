/**
 * crypto.c — stubs for signing/verification.
 *
 * Deliberately not implemented: rolling your own secp256k1 is a
 * good way to introduce a subtle, catastrophic bug. Once a backend
 * is chosen (libsecp256k1 is the obvious candidate — it's what
 * bitcoin-core and most atproto implementations use), these become
 * thin wrappers.
 */

#include "wolfram/crypto.h"

wf_status wf_signing_key_generate(wf_key_type type, wf_signing_key *out) {
    (void)type;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_sign(const wf_signing_key *key,
                   const unsigned char *msg, size_t msg_len,
                   unsigned char *sig_out, size_t sig_out_cap) {
    (void)key;
    (void)msg;
    (void)msg_len;
    (void)sig_out;
    (void)sig_out_cap;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_verify(const char *public_key_multibase,
                     const unsigned char *msg, size_t msg_len,
                     const unsigned char *sig, size_t sig_len) {
    (void)public_key_multibase;
    (void)msg;
    (void)msg_len;
    (void)sig;
    (void)sig_len;
    return WF_ERR_INVALID_ARG;
}
