/**
 * auth_client.h — Authenticated XRPC client with automatic DPoP and session management.
 *
 * This module provides a high-level wrapper around `wf_xrpc_client` that
 * automatically attaches DPoP proofs to requests and handles session
 * expiration and DPoP nonce rotation.
 */

#ifndef WOLFRAM_AUTH_CLIENT_H
#define WOLFRAM_AUTH_CLIENT_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/oauth.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque authenticated client. */
typedef struct wf_auth_client wf_auth_client;

/**
 * Create a new authenticated client.
 * The client does not own the `xrpc_client` or the `session_state`.
 * They must outlive the `auth_client`.
 *
 * Returns NULL on allocation failure.
 */
wf_auth_client *wf_auth_client_new(wf_xrpc_client *client,
                                  wf_oauth_session_state *session,
                                  const wf_oauth_server_metadata *server,
                                  const wf_oauth_client_auth *client_auth);

/**
 * Free the authenticated client.
 * Does not free the `xrpc_client` or the `session_state`.
 * Safe to call with NULL.
 */
void wf_auth_client_free(wf_auth_client *auth_client);

/**
 * Issue an authenticated XRPC query (GET) with a pre-encoded query string.
 *
 * Automatically handles DPoP proof generation, DPoP nonce rotation,
 * and session refresh if the token has expired.
 *
 * On WF_OK, `out` is populated and must be released with `wf_response_free`.
 */
wf_status wf_auth_client_query(wf_auth_client *auth_client,
                               const char *nsid,
                               const char *query_string,
                               wf_response *out);

/**
 * Issue an authenticated XRPC query (GET) with structured parameters.
 *
 * Wraps `wf_auth_client_query` with the same parameter encoding used by
 * `wf_xrpc_query_params`. Designed for generated lexicon wrappers.
 */
wf_status wf_auth_client_query_params(wf_auth_client *auth_client,
                                      const char *nsid,
                                      const wf_xrpc_param *params,
                                      size_t param_count,
                                      wf_response *out);

/**
 * Issue an authenticated XRPC procedure (POST).
 *
 * Automatically handles DPoP proof generation, DPoP nonce rotation,
 * and session refresh if the token has expired.
 *
 * On WF_OK, `out` is populated and must be released with `wf_response_free`.
 */
wf_status wf_auth_client_procedure(wf_auth_client *auth_client,
                                   const char *nsid,
                                   const char *json_body,
                                   wf_response *out);

/**
 * Upload an authenticated blob to `xrpc/{nsid}`.
 *
 * Automatically handles DPoP proof generation, DPoP nonce rotation,
 * and transparent access-token refresh (refreshing once on a 401 and
 * re-issuing with the new token).
 */
wf_status wf_auth_client_upload_blob(wf_auth_client *auth_client,
                                      const char *nsid,
                                      const void *data,
                                      size_t data_len,
                                      const char *content_type,
                                      wf_response *out);

/**
 * Best-effort refresh of the bound session.
 *
 * Refreshes the access token in place only when it is missing or expired
 * (`expires_at <= 0` or in the past); if a still-valid token is present no
 * network call is made. Returns WF_OK when a usable access token is available.
 *
 * Ownership: `auth_client` is not freed; the refreshed `wf_oauth_session_state`
 * is updated in place and remains owned by whoever supplied it to
 * `wf_auth_client_new`.
 */
wf_status wf_auth_client_ensure_valid(wf_auth_client *auth_client);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AUTH_CLIENT_H */
