#include "internal.h"

#include <ctype.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <stdlib.h>
#include <string.h>

static int wf_oauth_pkce_char_valid(unsigned char value) {
    return isalnum(value) || value == '-' || value == '.' || value == '_' || value == '~';
}

wf_status wf_oauth_pkce_from_verifier(const char *verifier, wf_oauth_pkce *out) {
    size_t len, i;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char *challenge = NULL;
    wf_status status;
    if (!verifier || !out) return WF_ERR_INVALID_ARG;
    len = strlen(verifier);
    if (len < 43 || len > WF_OAUTH_PKCE_VERIFIER_MAX) return WF_ERR_INVALID_ARG;
    for (i = 0; i < len; i++) {
        if (!wf_oauth_pkce_char_valid((unsigned char)verifier[i])) return WF_ERR_INVALID_ARG;
    }
    SHA256((const unsigned char *)verifier, len, digest);
    status = wf_oauth_base64url(digest, sizeof(digest), &challenge);
    if (status != WF_OK) return status;
    if (strlen(challenge) != WF_OAUTH_PKCE_CHALLENGE_LEN) {
        free(challenge);
        return WF_ERR_PARSE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->verifier, verifier, len + 1);
    memcpy(out->challenge, challenge, WF_OAUTH_PKCE_CHALLENGE_LEN + 1);
    free(challenge);
    return WF_OK;
}

wf_status wf_oauth_pkce_generate(wf_oauth_pkce *out) {
    unsigned char random[32];
    char *verifier = NULL;
    wf_status status;
    if (!out) return WF_ERR_INVALID_ARG;
    if (RAND_bytes(random, sizeof(random)) != 1) return WF_ERR_PARSE;
    status = wf_oauth_base64url(random, sizeof(random), &verifier);
    if (status == WF_OK) status = wf_oauth_pkce_from_verifier(verifier, out);
    free(verifier);
    return status;
}
