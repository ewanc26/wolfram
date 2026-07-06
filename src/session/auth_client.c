/**
 * auth_client.c — Authenticated XRPC client with automatic DPoP and session management.
 */

#include "wolfram/auth_client.h"
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

struct wf_auth_client {
    wf_xrpc_client *client;
    wf_oauth_session_state *session;
    const wf_oauth_server_metadata *server;
    const wf_oauth_client_auth *client_auth;
};

wf_auth_client *wf_auth_client_new(wf_xrpc_client *client,
                                  wf_oauth_session_state *session,
                                  const wf_oauth_server_metadata *server,
                                  const wf_oauth_client_auth *client_auth) {
    if (!client || !session || !client_auth) return NULL;
    wf_auth_client *auth_client = calloc(1, sizeof(*auth_client));
    if (!auth_client) return NULL;
    auth_client->client = client;
    auth_client->session = session;
    auth_client->server = server;
    auth_client->client_auth = client_auth;
    return auth_client;
}

void wf_auth_client_free(wf_auth_client *auth_client) {
    if (!auth_client) return;
    free(auth_client);
}

/* Forward declarations of internal helpers */
static wf_status wf_auth_client_request_internal(wf_auth_client *auth_client,
                                                 const char *nsid,
                                                 const char *query_string,
                                                 const char *json_body,
                                                 int is_procedure,
                                                 wf_response *out);

static wf_status wf_auth_client_ensure_session(wf_auth_client *auth_client,
                                               int64_t now);

wf_status wf_auth_client_query(wf_auth_client *auth_client,
                               const char *nsid,
                               const char *query_string,
                               wf_response *out) {
    if (!auth_client || !nsid || !out) return WF_ERR_INVALID_ARG;
    
    wf_status status = wf_auth_client_ensure_session(auth_client, time(NULL));
    if (status != WF_OK) return status;

    return wf_auth_client_request_internal(auth_client, nsid, query_string, NULL, 0, out);
}

wf_status wf_auth_client_procedure(wf_auth_client *auth_client,
                                   const char *nsid,
                                   const char *json_body,
                                   wf_response *out) {
    if (!auth_client || !nsid || !out) return WF_ERR_INVALID_ARG;

    wf_status status = wf_auth_client_ensure_session(auth_client, time(NULL));
    if (status != WF_OK) return status;

    return wf_auth_client_request_internal(auth_client, nsid, NULL, json_body, 1, out);
}

static wf_status wf_auth_client_ensure_session(wf_auth_client *auth_client,
                                                int64_t now) {
    if (auth_client->session->expires_at <= 0 || now < auth_client->session->expires_at) {
        return WF_OK;
    }
    if (!auth_client->server || !auth_client->client_auth) {
        return WF_ERR_NOT_FOUND;
    }
    return wf_oauth_session_refresh(auth_client->client,
                                    auth_client->server,
                                    auth_client->client_auth,
                                    auth_client->session,
                                    now);
}

static wf_status wf_auth_client_request_internal(wf_auth_client *auth_client,
                                                 const char *nsid,
                                                 const char *query_string,
                                                 const char *json_body,
                                                 int is_procedure,
                                                 wf_response *out) {
    wf_status status = WF_OK;
    char *base_url = NULL;
    char *full_url = NULL;
    char *dpop_nonce = NULL;
    wf_oauth_dpop_proof_options proof_opts = {0};
    char *proof_jwt = NULL;
    wf_oauth_dpop_key *dpop_key = auth_client->session->dpop_key;

    if (!dpop_key) return WF_ERR_INVALID_ARG;

    base_url = wf_xrpc_get_base_url(auth_client->client);
    if (!base_url) return WF_ERR_ALLOC;

    size_t url_cap = strlen(base_url) + strlen("/xrpc/") + strlen(nsid) + 1
                     + (query_string ? strlen(query_string) + 1 : 0);
    full_url = malloc(url_cap);
    if (!full_url) {
        free(base_url);
        return WF_ERR_ALLOC;
    }

    if (query_string && query_string[0] != '\0') {
        snprintf(full_url, url_cap, "%s/xrpc/%s?%s", base_url, nsid, query_string);
    } else {
        snprintf(full_url, url_cap, "%s/xrpc/%s", base_url, nsid);
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        proof_opts.http_method = is_procedure ? "POST" : "GET";
        proof_opts.http_uri = full_url;
        if (dpop_nonce) {
            proof_opts.nonce = dpop_nonce;
        }

        status = wf_oauth_dpop_proof_create(dpop_key, &proof_opts, &proof_jwt);
        if (status != WF_OK) {
            free(proof_jwt);
            break;
        }

        wf_http_header headers[1];
        headers[0].name = "DPoP";
        headers[0].value = proof_jwt;

        if (is_procedure) {
            status = wf_http_post(auth_client->client, full_url, "application/json", json_body, headers, 1, out);
        } else {
            status = wf_http_get_with_headers(auth_client->client, full_url, headers, 1, out);
        }

        free(proof_jwt);
        proof_jwt = NULL;

        if (status == WF_OK) {
            break;
        } else if (status == WF_ERR_HTTP && out->status == 401 && out->dpop_nonce) {
            dpop_nonce = out->dpop_nonce;
            out->dpop_nonce = NULL; 
            wf_response_free(out); 
        } else {
            break;
        }
    }

    if (dpop_nonce) free(dpop_nonce);
    free(full_url);
    free(base_url);

    return status;
}
