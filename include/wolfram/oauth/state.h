#ifndef WOLFRAM_OAUTH_STATE_H
#define WOLFRAM_OAUTH_STATE_H

#include <stdint.h>

#include "wolfram/oauth/par.h"
#include "wolfram/oauth/pkce.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Owned durable OAuth token session, corresponding to atproto TokenSet. */
typedef struct wf_oauth_session_state {
    char *issuer;
    char *subject;
    char *audience;
    char *scope;
    char *access_token;
    char *refresh_token;       /* optional */
    char *client_auth_key_id;  /* optional; private_key_jwt when populated */
    int64_t expires_at;        /* Unix seconds; zero when unspecified */
    wf_oauth_dpop_key *dpop_key;
} wf_oauth_session_state;

/** Owned state required to finish an authorization redirect flow. */
typedef struct wf_oauth_authorization_state {
    char *issuer;
    char *code_verifier;
    char *app_state; /* optional */
    char *client_auth_key_id; /* optional; private_key_jwt when populated */
    int64_t expires_at; /* Unix seconds */
    wf_oauth_dpop_key *dpop_key;
} wf_oauth_authorization_state;

/**
 * Create a deep-owned pending state. `ttl_seconds` must be positive; callers
 * normally use 600 seconds, matching the maintained atproto browser client.
 */
wf_status wf_oauth_authorization_state_create(
    const char *issuer, const wf_oauth_pkce *pkce,
    const wf_oauth_dpop_key *dpop_key, const char *app_state,
    const char *client_auth_key_id, int64_t now, int64_t ttl_seconds,
    wf_oauth_authorization_state *out);

/** Serialize state, including its private DPoP JWK, to owned JSON. */
wf_status wf_oauth_authorization_state_serialize(
    const wf_oauth_authorization_state *state, char **json_out);

/** Restore and validate unexpired state. `now` is Unix seconds. */
wf_status wf_oauth_authorization_state_parse(
    const char *json, size_t json_len, int64_t now,
    wf_oauth_authorization_state *out);

void wf_oauth_authorization_state_free(wf_oauth_authorization_state *state);

wf_status wf_oauth_session_state_create(
    const char *issuer, const char *audience,
    const wf_oauth_dpop_key *dpop_key,
    const wf_oauth_token_response *token_response,
    const char *client_auth_key_id, int64_t now,
    wf_oauth_session_state *out);
wf_status wf_oauth_session_state_serialize(
    const wf_oauth_session_state *session, char **json_out);
wf_status wf_oauth_session_state_parse(
    const char *json, size_t json_len, const char *expected_subject,
    wf_oauth_session_state *out);
void wf_oauth_session_state_free(wf_oauth_session_state *session);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_STATE_H */
