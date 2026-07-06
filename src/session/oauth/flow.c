#include "internal.h"

#include "wolfram/identity.h"

#include <curl/curl.h>

#include <stdlib.h>
#include <string.h>

void wf_oauth_authorization_begin_result_free(
    wf_oauth_authorization_begin_result *result) {
    if (!result) return;
    free(result->authorization_url);
    free(result->state);
    free(result->state_json);
    memset(result, 0, sizeof(*result));
}

wf_status wf_oauth_authorization_url_create(
    const char *endpoint, const char *client_id, const char *request_uri,
    char **out) {
    CURL *curl = NULL;
    char *escaped_client = NULL, *escaped_request = NULL, *url = NULL;
    size_t needed;
    const char *separator;
    if (!endpoint || !client_id || !request_uri || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    curl = curl_easy_init();
    if (!curl) return WF_ERR_ALLOC;
    escaped_client = curl_easy_escape(curl, client_id, 0);
    escaped_request = curl_easy_escape(curl, request_uri, 0);
    if (!escaped_client || !escaped_request) {
        curl_free(escaped_client); curl_free(escaped_request); curl_easy_cleanup(curl);
        return WF_ERR_ALLOC;
    }
    separator = strchr(endpoint, '?') ? "&" : "?";
    needed = strlen(endpoint) + 1 + strlen("client_id=") + strlen(escaped_client) +
             strlen("&request_uri=") + strlen(escaped_request) + 1;
    url = malloc(needed);
    if (url) snprintf(url, needed, "%s%sclient_id=%s&request_uri=%s",
                      endpoint, separator, escaped_client, escaped_request);
    curl_free(escaped_client); curl_free(escaped_request); curl_easy_cleanup(curl);
    if (!url) return WF_ERR_ALLOC;
    *out = url;
    return WF_OK;
}

wf_status wf_oauth_verify_token_subject(wf_xrpc_client *transport,
                                        const char *subject,
                                        const char *issuer,
                                        char **audience_out) {
    wf_did_document document = {0};
    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata discovered = {0};
    wf_status status;
    *audience_out = NULL;
    status = wf_did_resolve(transport, subject, &document);
    if (status == WF_ERR_INVALID_ARG) status = WF_ERR_PARSE;
    if (status == WF_OK &&
        (!document.did || strcmp(document.did, subject) != 0 ||
         !document.pds_endpoint)) status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_discover(
        transport, document.pds_endpoint, &resource, &discovered);
    if (status == WF_ERR_INVALID_ARG) status = WF_ERR_PARSE;
    if (status == WF_OK && strcmp(discovered.issuer, issuer) != 0)
        status = WF_ERR_PARSE;
    if (status == WF_OK) {
        *audience_out = wf_oauth_strdup(resource.resource);
        if (!*audience_out) status = WF_ERR_ALLOC;
    }
    wf_oauth_server_metadata_free(&discovered);
    wf_oauth_resource_metadata_free(&resource);
    wf_did_document_free(&document);
    return status;
}

static void wf_oauth_revoke_issued_token(
    wf_xrpc_client *transport, const wf_oauth_server_metadata *server,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *client_auth,
    const wf_oauth_token_response *token, int prefer_refresh) {
    const char *value;
    if (!server->revocation_endpoint) return;
    value = prefer_refresh && token->refresh_token ? token->refresh_token
                                                   : token->access_token;
    if (value) (void)wf_oauth_revoke(transport, server->revocation_endpoint,
                                      dpop_key, client_auth, value);
}

wf_status wf_oauth_authorization_begin(
    wf_xrpc_client *transport, const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *client_auth,
    const wf_oauth_authorization_begin_options *options,
    wf_oauth_authorization_begin_result *out) {
    wf_oauth_pkce pkce = {0};
    wf_oauth_dpop_key *dpop_key = NULL;
    wf_oauth_authorization_state stored = {0};
    wf_oauth_par_response par = {0};
    wf_oauth_par_request request;
    char *state = NULL, *state_json = NULL, *authorization_url = NULL;
    const char *scope;
    int64_t ttl;
    wf_status status;
    if (!transport || !server || !client || !client_auth || !options || !out ||
        !options->redirect_uri || options->now <= 0 || !server->authorization_endpoint ||
        !server->pushed_authorization_request_endpoint || !server->issuer ||
        !client->client_id || strcmp(client->client_id, client_auth->client_id) != 0 ||
        !wf_oauth_string_list_has(&client->redirect_uris, options->redirect_uri)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    status = wf_oauth_client_auth_validate(server, client, client_auth);
    if (status != WF_OK) return status;
    scope = options->scope ? options->scope : client->scope;
    if (!scope || !wf_oauth_scope_has(scope, "atproto")) return WF_ERR_INVALID_ARG;
    ttl = options->state_ttl > 0 ? options->state_ttl : 600;
    status = wf_oauth_pkce_generate(&pkce);
    if (status == WF_OK) status = wf_oauth_dpop_key_generate(&dpop_key);
    if (status == WF_OK) status = wf_oauth_random_jti(&state);
    if (status == WF_OK) status = wf_oauth_authorization_state_create(
        server->issuer, &pkce, dpop_key, options->app_state,
        client_auth->key_id, options->now, ttl, &stored);
    if (status == WF_OK) status = wf_oauth_authorization_state_serialize(
        &stored, &state_json);
    request = (wf_oauth_par_request){
        client->client_id, options->redirect_uri, scope, state,
        pkce.challenge, options->login_hint
    };
    if (status == WF_OK) status = wf_oauth_par_with_auth(
        transport, server->pushed_authorization_request_endpoint, dpop_key,
        client_auth, &request, &par);
    if (status == WF_OK) status = wf_oauth_authorization_url_create(
        server->authorization_endpoint, client->client_id, par.request_uri,
        &authorization_url);
    if (status == WF_OK) {
        out->authorization_url = authorization_url; authorization_url = NULL;
        out->state = state; state = NULL;
        out->state_json = state_json; state_json = NULL;
    }
    free(authorization_url); free(state); free(state_json);
    wf_oauth_par_response_free(&par);
    wf_oauth_authorization_state_free(&stored);
    wf_oauth_dpop_key_free(dpop_key);
    return status;
}

void wf_oauth_authorization_complete_result_free(
    wf_oauth_authorization_complete_result *result) {
    if (!result) return;
    wf_oauth_session_state_free(&result->session);
    free(result->session_json);
    free(result->app_state);
    free(result->error);
    free(result->error_description);
    memset(result, 0, sizeof(*result));
}

wf_status wf_oauth_authorization_complete(
    wf_xrpc_client *transport, const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *client_auth,
    const wf_oauth_callback_params *params, const char *expected_state,
    const char *state_json, size_t state_json_len, const char *redirect_uri,
    int64_t now, wf_oauth_authorization_complete_result *out) {
    wf_oauth_authorization_state pending = {0};
    wf_oauth_callback_result callback = {0};
    wf_oauth_token_response token = {0};
    char *audience = NULL;
    wf_status status;
    if (!transport || !server || !client || !client_auth || !params ||
        !expected_state || !*expected_state || !state_json || !state_json_len ||
        !redirect_uri || !out || now <= 0 || !server->issuer ||
        !server->token_endpoint || !client->client_id ||
        strcmp(client->client_id, client_auth->client_id) != 0 ||
        !wf_oauth_string_list_has(&client->redirect_uris, redirect_uri)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    status = wf_oauth_authorization_state_parse(state_json, state_json_len,
                                                now, &pending);
    if (status != WF_OK) goto done;
    if (pending.app_state) {
        out->app_state = wf_oauth_strdup(pending.app_state);
        if (!out->app_state) { status = WF_ERR_ALLOC; goto done; }
    }
    if (strcmp(pending.issuer, server->issuer) != 0 ||
        (pending.client_auth_key_id &&
         (!client_auth->signing_key || !client_auth->key_id ||
          strcmp(pending.client_auth_key_id, client_auth->key_id) != 0)) ||
        (!pending.client_auth_key_id &&
         (client_auth->signing_key || client_auth->key_id))) {
        status = WF_ERR_PARSE;
        goto done;
    }
    status = wf_oauth_client_auth_validate(server, client, client_auth);
    if (status != WF_OK) goto done;
    status = wf_oauth_callback_validate(
        params, expected_state, pending.issuer,
        server->authorization_response_iss_parameter_supported, &callback);
    if (status != WF_OK) goto done;
    if (callback.error) {
        out->error = callback.error; callback.error = NULL;
        out->error_description = callback.error_description;
        callback.error_description = NULL;
        status = WF_ERR_HTTP;
        goto done;
    }
    status = wf_oauth_exchange_code_with_auth(
        transport, server->token_endpoint, pending.dpop_key, client_auth,
        callback.code, redirect_uri, pending.code_verifier, &token);
    if (status != WF_OK) goto done;
    status = wf_oauth_verify_token_subject(transport, token.sub,
                                           pending.issuer, &audience);
    if (status != WF_OK) {
        wf_oauth_revoke_issued_token(transport, server, pending.dpop_key,
                                     client_auth, &token, 0);
        goto done;
    }
    status = wf_oauth_session_state_create(
        pending.issuer, audience, pending.dpop_key, &token,
        pending.client_auth_key_id, now, &out->session);
    if (status == WF_OK) status = wf_oauth_session_state_serialize(
        &out->session, &out->session_json);
    if (status != WF_OK) {
        wf_oauth_revoke_issued_token(transport, server, pending.dpop_key,
                                     client_auth, &token, 1);
        wf_oauth_session_state_free(&out->session);
        free(out->session_json);
        out->session_json = NULL;
    }
done:
    free(audience);
    wf_oauth_token_response_free(&token);
    wf_oauth_callback_result_free(&callback);
    wf_oauth_authorization_state_free(&pending);
    return status;
}
