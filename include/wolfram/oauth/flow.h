#ifndef WOLFRAM_OAUTH_FLOW_H
#define WOLFRAM_OAUTH_FLOW_H

#include <stdint.h>

#include "wolfram/oauth/callback.h"
#include "wolfram/oauth/metadata.h"
#include "wolfram/oauth/par.h"
#include "wolfram/oauth/state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_oauth_authorization_begin_options {
    const char *redirect_uri;
    const char *scope;      /* optional; defaults to client metadata scope */
    const char *login_hint; /* optional handle or DID */
    const char *app_state;  /* optional application-owned state */
    int64_t now;            /* current Unix seconds */
    int64_t state_ttl;      /* defaults to 600 when <= 0 */
} wf_oauth_authorization_begin_options;

/** Owned output of the authorization flow's initial PAR transition. */
typedef struct wf_oauth_authorization_begin_result {
    char *authorization_url;
    char *state;
    char *state_json; /* persist atomically under `state` before redirecting */
} wf_oauth_authorization_begin_result;

/**
 * Generate PKCE/state/DPoP material, submit PAR, and construct the browser URL.
 * The caller owns `state_json` persistence and must consume it on callback.
 */
wf_status wf_oauth_authorization_begin(
    wf_xrpc_client *transport,
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *client_auth,
    const wf_oauth_authorization_begin_options *options,
    wf_oauth_authorization_begin_result *out);
void wf_oauth_authorization_begin_result_free(
    wf_oauth_authorization_begin_result *result);

/** Owned output of a callback-to-session transition. */
typedef struct wf_oauth_authorization_complete_result {
    wf_oauth_session_state session;
    char *session_json; /* persist atomically under session.subject */
    char *app_state;    /* optional application state restored from the flow */
    char *error;        /* populated for a validated authorization denial */
    char *error_description;
} wf_oauth_authorization_complete_result;

/**
 * Finish an authorization callback and create a verified durable session.
 */
wf_status wf_oauth_authorization_complete(
    wf_xrpc_client *transport,
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *client_auth,
    const wf_oauth_callback_params *params,
    const char *expected_state,
    const char *state_json,
    size_t state_json_len,
    const char *redirect_uri,
    int64_t now,
    wf_oauth_authorization_complete_result *out);
void wf_oauth_authorization_complete_result_free(
    wf_oauth_authorization_complete_result *result);

/** Construct the browser URL for an already-created PAR request URI. */
wf_status wf_oauth_authorization_url_create(
    const char *authorization_endpoint, const char *client_id,
    const char *request_uri, char **url_out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_FLOW_H */
