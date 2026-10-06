/*
 * signature.c -- wf_update_verify_signature: check update.json against its
 * detached Ed25519 signature (docs/update.md). C99, built with ed25519.c.
 */

#include "wolfram/ed25519.h"
#include "wolfram/update.h"

#include <string.h>

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

wf_status
wf_update_verify_signature(const void *manifest, size_t manifest_len,
                           const char *sig_text, size_t sig_len,
                           const unsigned char pk[WF_ED25519_PUBLIC_LEN]) {
    unsigned char sig[WF_ED25519_SIGNATURE_LEN];
    size_t i;

    if (!manifest || !sig_text || !pk) return WF_ERR_INVALID_ARG;
    /* The signature file is 128 hex characters; one trailing newline (LF or
     * CRLF) is what a shell redirect leaves, so allow that and nothing else. */
    if (sig_len > 0 && sig_text[sig_len - 1] == '\n') sig_len--;
    if (sig_len > 0 && sig_text[sig_len - 1] == '\r') sig_len--;
    if (sig_len != 2 * WF_ED25519_SIGNATURE_LEN) return WF_ERR_PARSE;
    for (i = 0; i < WF_ED25519_SIGNATURE_LEN; i++) {
        int hi = hexval((unsigned char)sig_text[2 * i]);
        int lo = hexval((unsigned char)sig_text[2 * i + 1]);
        if (hi < 0 || lo < 0) return WF_ERR_PARSE;
        sig[i] = (unsigned char)(hi * 16 + lo);
    }
    return wf_ed25519_verify(sig, manifest, manifest_len, pk);
}
