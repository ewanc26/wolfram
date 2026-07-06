#ifndef WOLFRAM_OAUTH_PAR_H
#define WOLFRAM_OAUTH_PAR_H

#include <stdint.h>

#include "wolfram/oauth/dpop.h"
#include "wolfram/oauth/metadata.h"
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_oauth_session_state wf_oauth_session_state;

typedef struct wf_oauth_par_response {
    char *request_uri;
    int64_t expires_in;
} wf_oauth_par_response;

typedef struct wf_oauth_token_response {
    char *access_token;
    char *token_type;
    char *sub;
    char *scope;
    char *refresh_token;
    int64_t expires_in;
    int expires_in_present;
} wf_oauth_token_response;

typedef struct wf_oauth_par_request {
    const char *client_id;
    const char *redirect_uri;
    const char *scope;
    const char *state;
    const char *code_challenge;
    const char *login_hint; /* optional */
} wf_oauth_par_request;

/** Borrowed client authentication configuration for OAuth endpoint calls. */
typedef struct wf_oauth_client_auth {
    const char *client_id;
    const char *authorization_server_issuer; /* required for private_key_jwt */
    const wf_oauth_dpop_key *signing_key;     /* NULL selects `none` */
    const char *key_id;                       /* required with signing_key */
} wf_oauth_client_auth;

/** Cross-check client authentication against discovered metadata. */
wf_status wf_oauth_client_auth_validate(
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *auth);

wf_status wf_oauth_par_response_parse(const char *json, size_t json_len,
                                      wf_oauth_par_response *out);
wf_status wf_oauth_token_response_parse(const char *json, size_t json_len,
                                        wf_oauth_token_response *out);
/** Validate that a parsed token response remains bound to an expected DID. */
wf_status wf_oauth_token_response_validate_subject(
    const wf_oauth_token_response *response, const char *expected_sub);
void wf_oauth_par_response_free(wf_oauth_par_response *response);
void wf_oauth_token_response_free(wf_oauth_token_response *response);

/** Public-client PAR with one mandatory use_dpop_nonce retry. */
wf_status wf_oauth_par(wf_xrpc_client *transport, const char *endpoint,
                       const wf_oauth_dpop_key *key,
                       const wf_oauth_par_request *request,
                       wf_oauth_par_response *out);
wf_status wf_oauth_par_with_auth(
    wf_xrpc_client *transport, const char *endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const wf_oauth_par_request *request, wf_oauth_par_response *out);

/** Public-client authorization-code exchange with DPoP nonce retry. */
wf_status wf_oauth_exchange_code(wf_xrpc_client *transport,
                                 const char *token_endpoint,
                                 const wf_oauth_dpop_key *key,
                                 const char *client_id, const char *code,
                                 const char *redirect_uri,
                                 const char *code_verifier,
                                 wf_oauth_token_response *out);
wf_status wf_oauth_exchange_code_with_auth(
    wf_xrpc_client *transport, const char *token_endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const char *code, const char *redirect_uri, const char *code_verifier,
    wf_oauth_token_response *out);

/**
 * Exchange a public client's refresh token. The returned AT Protocol subject
 * must match expected_sub; callers should verify that DID's issuer before use.
 */
wf_status wf_oauth_refresh(wf_xrpc_client *transport,
                           const char *token_endpoint,
                           const wf_oauth_dpop_key *key,
                           const char *client_id,
                           const char *refresh_token,
                           const char *expected_sub,
                           wf_oauth_token_response *out);
wf_status wf_oauth_refresh_with_auth(
    wf_xrpc_client *transport, const char *token_endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const char *refresh_token, const char *expected_sub,
    wf_oauth_token_response *out);

/**
 * Refresh an existing session using its refresh token.
 * Updates `session` in-place if successful.
 */
wf_status wf_oauth_session_refresh(
    wf_xrpc_client *transport,
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_auth *client_auth,
    wf_oauth_session_state *session,
    int64_t now);

/** Best-effort callers may ignore WF_ERR_HTTP, matching RFC 7009 semantics. */
wf_status wf_oauth_revoke(wf_xrpc_client *transport,
                          const char *revocation_endpoint,
                          const wf_oauth_dpop_key *dpop_key,
                          const wf_oauth_client_auth *auth,
                          const char *token);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_PAR_H */
