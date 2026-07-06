#ifndef WOLFRAM_OAUTH_PKCE_H
#define WOLFRAM_OAUTH_PKCE_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_OAUTH_PKCE_VERIFIER_MAX 128
#define WF_OAUTH_PKCE_CHALLENGE_LEN 43

typedef struct wf_oauth_pkce {
    char verifier[WF_OAUTH_PKCE_VERIFIER_MAX + 1];
    char challenge[WF_OAUTH_PKCE_CHALLENGE_LEN + 1];
} wf_oauth_pkce;

/** Generate a fresh 32-octet PKCE verifier and its S256 challenge. */
wf_status wf_oauth_pkce_generate(wf_oauth_pkce *out);

/** Validate a caller-provided RFC 7636 verifier and derive its S256 challenge. */
wf_status wf_oauth_pkce_from_verifier(const char *verifier,
                                      wf_oauth_pkce *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_PKCE_H */
