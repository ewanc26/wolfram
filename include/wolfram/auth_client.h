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
 * Issue an authenticated XRPC query (GET).
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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AUTH_CLIENT_H */
