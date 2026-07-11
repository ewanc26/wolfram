#include "internal.h"

#include <curl/curl.h>

#include <stdlib.h>
#include <string.h>

void wf_oauth_par_response_free(wf_oauth_par_response *r) {
    if (!r) return;
    free(r->request_uri);
    memset(r, 0, sizeof(*r));
}

void wf_oauth_token_response_free(wf_oauth_token_response *r) {
    if (!r) return;
    free(r->access_token); free(r->token_type); free(r->sub); free(r->scope);
    free(r->refresh_token);
    memset(r, 0, sizeof(*r));
}

wf_status wf_oauth_par_response_parse(const char *json, size_t len,
                                      wf_oauth_par_response *out) {
    cJSON *root; wf_status s;
    if (!json || !len || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return WF_ERR_PARSE; }
    s = wf_oauth_json_string(root, "request_uri", 1, &out->request_uri);
    if (s == WF_OK) s = wf_oauth_positive_integer(root, "expires_in", 1,
                                                  &out->expires_in, NULL);
    cJSON_Delete(root);
    if (s != WF_OK) wf_oauth_par_response_free(out);
    return s;
}

wf_status wf_oauth_token_response_parse(const char *json, size_t len,
                                        wf_oauth_token_response *out) {
    cJSON *root; wf_status s;
    if (!json || !len || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return WF_ERR_PARSE; }
    s = wf_oauth_json_string(root, "access_token", 1, &out->access_token);
    if (s == WF_OK) s = wf_oauth_json_string(root, "token_type", 1, &out->token_type);
    if (s == WF_OK) s = wf_oauth_json_string(root, "sub", 1, &out->sub);
    if (s == WF_OK) s = wf_oauth_json_string(root, "scope", 1, &out->scope);
    if (s == WF_OK) s = wf_oauth_json_string(root, "refresh_token", 0, &out->refresh_token);
    if (s == WF_OK) s = wf_oauth_positive_integer(root, "expires_in", 0,
                                                  &out->expires_in,
                                                  &out->expires_in_present);
    cJSON_Delete(root);
    if (s == WF_OK && strcmp(out->token_type, "DPoP") != 0) s = WF_ERR_PARSE;
    if (s == WF_OK && strncmp(out->sub, "did:", 4) != 0) s = WF_ERR_PARSE;
    if (s == WF_OK && !wf_oauth_scope_has(out->scope, "atproto")) s = WF_ERR_PARSE;
    if (s != WF_OK) wf_oauth_token_response_free(out);
    return s;
}

wf_status wf_oauth_token_response_validate_subject(
    const wf_oauth_token_response *response, const char *expected_sub) {
    if (!response || !response->sub || !expected_sub ||
        strncmp(expected_sub, "did:", 4) != 0) return WF_ERR_INVALID_ARG;
    return strcmp(response->sub, expected_sub) == 0 ? WF_OK : WF_ERR_PARSE;
}

static wf_status wf_oauth_form_encode(const wf_xrpc_param *params, size_t count,
                                      char **out) {
    CURL *curl = curl_easy_init(); size_t i, len = 1, off = 0; char **values;
    if (!curl) return WF_ERR_ALLOC;
    values = calloc(count * 2, sizeof(*values));
    if (!values) { curl_easy_cleanup(curl); return WF_ERR_ALLOC; }
    for (i = 0; i < count; i++) {
        values[i * 2] = curl_easy_escape(curl, params[i].name, 0);
        values[i * 2 + 1] = curl_easy_escape(curl, params[i].value, 0);
        if (!values[i * 2] || !values[i * 2 + 1]) break;
        len += strlen(values[i * 2]) + strlen(values[i * 2 + 1]) + 2;
    }
    *out = i == count ? malloc(len) : NULL;
    if (*out) for (i = 0; i < count; i++) off += (size_t)snprintf(*out + off, len - off,
        "%s%s=%s", i ? "&" : "", values[i * 2], values[i * 2 + 1]);
    for (i = 0; i < count * 2; i++) curl_free(values[i]);
    free(values); curl_easy_cleanup(curl);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static int wf_oauth_use_nonce(const wf_response *r) {
    cJSON *root; const cJSON *error; int yes = 0;
    if (!r->dpop_nonce || !r->body) return 0;
    root = cJSON_ParseWithLength(r->body, r->body_len);
    error = root ? cJSON_GetObjectItemCaseSensitive(root, "error") : NULL;
    yes = cJSON_IsString(error) && strcmp(error->valuestring, "use_dpop_nonce") == 0;
    cJSON_Delete(root); return yes;
}

static wf_status wf_oauth_post(wf_xrpc_client *transport, const char *endpoint,
                               const wf_oauth_dpop_key *key, const char *form,
                               wf_response *out) {
    char *proof = NULL; wf_http_header header; wf_status s; int attempt;
    char *nonce = NULL;
    for (attempt = 0; attempt < 2; attempt++) {
        wf_oauth_dpop_proof_options opts = {"POST", endpoint, nonce, NULL, NULL, 0};
        s = wf_oauth_dpop_proof_create(key, &opts, &proof);
        if (s != WF_OK) return s;
        header.name = "DPoP"; header.value = proof;
        s = wf_http_post(transport, endpoint, "application/x-www-form-urlencoded",
                         form, &header, 1, out);
        free(proof); proof = NULL;
        if (s != WF_ERR_HTTP || attempt || !wf_oauth_use_nonce(out)) {
            free(nonce);
            return s;
        }
        nonce = wf_oauth_strdup(out->dpop_nonce);
        wf_response_free(out);
        if (!nonce) return WF_ERR_ALLOC;
    }
    free(nonce); return s;
}

static int wf_oauth_jwks_has_signing_key(const char *jwks_json,
                                         const char *key_id) {
    cJSON *root = NULL;
    const cJSON *keys, *key, *item, *op;
    int found = 0;
    if (!jwks_json || !key_id) return 0;
    root = cJSON_Parse(jwks_json);
    keys = root ? cJSON_GetObjectItemCaseSensitive(root, "keys") : NULL;
    if (!cJSON_IsObject(root) || !cJSON_IsArray(keys)) goto done;
    cJSON_ArrayForEach(key, keys) {
        int can_sign = 1;
        item = cJSON_GetObjectItemCaseSensitive(key, "kid");
        if (!cJSON_IsString(item) || strcmp(item->valuestring, key_id) != 0) continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "kty");
        if (!cJSON_IsString(item) || strcmp(item->valuestring, "EC") != 0) continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "crv");
        if (!cJSON_IsString(item) || strcmp(item->valuestring, "P-256") != 0) continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "alg");
        if (item && (!cJSON_IsString(item) || strcmp(item->valuestring, "ES256") != 0))
            continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "use");
        if (item && (!cJSON_IsString(item) || strcmp(item->valuestring, "sig") != 0))
            continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "revoked");
        if (item && (!cJSON_IsBool(item) || cJSON_IsTrue(item))) continue;
        item = cJSON_GetObjectItemCaseSensitive(key, "key_ops");
        if (item) {
            can_sign = 0;
            if (!cJSON_IsArray(item)) continue;
            cJSON_ArrayForEach(op, item) {
                if (cJSON_IsString(op) && strcmp(op->valuestring, "sign") == 0) {
                    can_sign = 1;
                    break;
                }
            }
        }
        if (can_sign) { found = 1; break; }
    }
done:
    cJSON_Delete(root);
    return found;
}

wf_status wf_oauth_client_auth_validate(
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *auth) {
    const char *method;
    if (!server || !server->issuer || !client || !auth || !auth->client_id ||
        !client->client_id || strcmp(auth->client_id, client->client_id) != 0 ||
        !client->token_endpoint_auth_method) return WF_ERR_INVALID_ARG;
    method = auth->signing_key ? "private_key_jwt" : "none";
    if (strcmp(client->token_endpoint_auth_method, method) != 0 ||
        !wf_oauth_string_list_has(&server->token_endpoint_auth_methods_supported,
                                  method)) return WF_ERR_PARSE;
    if (!auth->signing_key) {
        return (!auth->key_id && !auth->authorization_server_issuer &&
                !client->token_endpoint_auth_signing_alg) ? WF_OK : WF_ERR_PARSE;
    }
    if (!auth->key_id || !*auth->key_id || !auth->authorization_server_issuer ||
        strcmp(auth->authorization_server_issuer, server->issuer) != 0 ||
        !client->token_endpoint_auth_signing_alg ||
        strcmp(client->token_endpoint_auth_signing_alg, "ES256") != 0 ||
        !wf_oauth_string_list_has(
            &server->token_endpoint_auth_signing_alg_values_supported, "ES256") ||
        (!client->jwks_json && !client->jwks_uri) ||
        (client->jwks_json &&
         !wf_oauth_jwks_has_signing_key(client->jwks_json, auth->key_id)))
        return WF_ERR_PARSE;
    return WF_OK;
}

static wf_status wf_oauth_add_client_auth(wf_xrpc_param *params, size_t *count,
                                          const wf_oauth_client_auth *auth,
                                          char **assertion_out) {
    wf_oauth_client_assertion_options options;
    wf_status status;
    if (!params || !count || !auth || !auth->client_id || !*auth->client_id ||
        !assertion_out) return WF_ERR_INVALID_ARG;
    *assertion_out = NULL;
    params[(*count)++] = (wf_xrpc_param){"client_id", auth->client_id};
    if (!auth->signing_key) {
        return (!auth->authorization_server_issuer && !auth->key_id) ?
            WF_OK : WF_ERR_INVALID_ARG;
    }
    options = (wf_oauth_client_assertion_options){
        auth->client_id, auth->authorization_server_issuer, auth->key_id, NULL, 0
    };
    status = wf_oauth_client_assertion_create(auth->signing_key, &options,
                                              assertion_out);
    if (status != WF_OK) return status;
    params[(*count)++] = (wf_xrpc_param){"client_assertion_type",
                                         WF_OAUTH_CLIENT_ASSERTION_TYPE};
    params[(*count)++] = (wf_xrpc_param){"client_assertion", *assertion_out};
    return WF_OK;
}

wf_status wf_oauth_par(wf_xrpc_client *t, const char *endpoint,
                       const wf_oauth_dpop_key *key, const wf_oauth_par_request *r,
                       wf_oauth_par_response *out) {
    wf_oauth_client_auth auth;
    if (!r) return WF_ERR_INVALID_ARG;
    auth = (wf_oauth_client_auth){r->client_id, NULL, NULL, NULL};
    return wf_oauth_par_with_auth(t, endpoint, key, &auth, r, out);
}

wf_status wf_oauth_par_with_auth(
    wf_xrpc_client *t, const char *endpoint, const wf_oauth_dpop_key *key,
    const wf_oauth_client_auth *auth, const wf_oauth_par_request *r,
    wf_oauth_par_response *out) {
    wf_xrpc_param p[11]; size_t n = 0; char *form = NULL, *assertion = NULL;
    wf_response res = {0}; wf_status s;
    if (!t || !endpoint || !key || !auth || !auth->client_id || !r || !out || !r->client_id ||
        strcmp(r->client_id, auth->client_id) != 0 || !r->redirect_uri ||
        !r->scope || !r->state || !r->code_challenge) return WF_ERR_INVALID_ARG;
#define ADD(k,v) do { p[n++] = (wf_xrpc_param){k,v}; } while (0)
    s = wf_oauth_add_client_auth(p, &n, auth, &assertion);
    ADD("redirect_uri", r->redirect_uri);
    ADD("scope", r->scope); ADD("state", r->state); ADD("code_challenge", r->code_challenge);
    ADD("code_challenge_method", "S256"); ADD("response_type", "code");
    ADD("response_mode", "query");
    if (r->login_hint) ADD("login_hint", r->login_hint);
    if (s == WF_OK) s = wf_oauth_form_encode(p, n, &form);
    if (s == WF_OK) s = wf_oauth_post(t, endpoint, key, form, &res);
    if (s == WF_OK) s = wf_oauth_par_response_parse(res.body, res.body_len, out);
    wf_response_free(&res); free(assertion); free(form); return s;
#undef ADD
}

wf_status wf_oauth_exchange_code(wf_xrpc_client *t, const char *endpoint,
 const wf_oauth_dpop_key *key, const char *client_id, const char *code,
 const char *redirect_uri, const char *verifier, wf_oauth_token_response *out) {
    wf_oauth_client_auth auth = {client_id, NULL, NULL, NULL};
    return wf_oauth_exchange_code_with_auth(t, endpoint, key, &auth, code,
                                             redirect_uri, verifier, out);
}

wf_status wf_oauth_exchange_code_with_auth(
    wf_xrpc_client *t, const char *endpoint, const wf_oauth_dpop_key *key,
    const wf_oauth_client_auth *auth, const char *code,
    const char *redirect_uri, const char *verifier,
    wf_oauth_token_response *out) {
    wf_xrpc_param p[7]; size_t n = 0; char *form = NULL, *assertion = NULL;
    wf_response res = {0}; wf_status s;
    if (!t || !endpoint || !key || !auth || !code || !redirect_uri ||
        !verifier || !out) return WF_ERR_INVALID_ARG;
    p[n++] = (wf_xrpc_param){"grant_type", "authorization_code"};
    s = wf_oauth_add_client_auth(p, &n, auth, &assertion);
    p[n++] = (wf_xrpc_param){"code", code};
    p[n++] = (wf_xrpc_param){"redirect_uri", redirect_uri};
    p[n++] = (wf_xrpc_param){"code_verifier", verifier};
    if (s == WF_OK) s = wf_oauth_form_encode(p, n, &form);
    if (s == WF_OK) s = wf_oauth_post(t, endpoint, key, form, &res);
    if (s == WF_OK) s = wf_oauth_token_response_parse(res.body, res.body_len, out);
    wf_response_free(&res); free(assertion); free(form); return s;
}

wf_status wf_oauth_refresh(wf_xrpc_client *t, const char *endpoint,
 const wf_oauth_dpop_key *key, const char *client_id, const char *refresh_token,
 const char *expected_sub, wf_oauth_token_response *out) {
    wf_oauth_client_auth auth = {client_id, NULL, NULL, NULL};
    return wf_oauth_refresh_with_auth(t, endpoint, key, &auth, refresh_token,
                                      expected_sub, out);
}

wf_status wf_oauth_refresh_with_auth(
    wf_xrpc_client *t, const char *endpoint, const wf_oauth_dpop_key *key,
    const wf_oauth_client_auth *auth, const char *refresh_token,
    const char *expected_sub, wf_oauth_token_response *out) {
    wf_xrpc_param p[5]; size_t n = 0;
    char *form = NULL, *assertion = NULL; wf_response res = {0}; wf_status s;
    if (!t || !endpoint || !key || !auth || !refresh_token || !expected_sub ||
        !out || strncmp(expected_sub, "did:", 4) != 0) return WF_ERR_INVALID_ARG;
    p[n++] = (wf_xrpc_param){"grant_type", "refresh_token"};
    s = wf_oauth_add_client_auth(p, &n, auth, &assertion);
    p[n++] = (wf_xrpc_param){"refresh_token", refresh_token};
    if (s == WF_OK) s = wf_oauth_form_encode(p, n, &form);
    if (s == WF_OK) s = wf_oauth_post(t, endpoint, key, form, &res);
    if (s == WF_OK) s = wf_oauth_token_response_parse(res.body, res.body_len, out);
    if (s == WF_OK &&
        wf_oauth_token_response_validate_subject(out, expected_sub) != WF_OK) {
        wf_oauth_token_response_free(out);
        s = WF_ERR_PARSE;
    }
    wf_response_free(&res);
    free(assertion); free(form);
    return s;
}

wf_status wf_oauth_session_refresh(
    wf_xrpc_client *transport,
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_auth *client_auth,
    wf_oauth_session_state *session,
    int64_t now) {
    wf_oauth_token_response token = {0};
    char *audience = NULL;
    wf_status status;
    if (!transport || !server || !client_auth || !session ||
        !session->refresh_token || !server->token_endpoint || !server->issuer ||
        !client_auth->client_id || !session->subject || now <= 0) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_oauth_refresh_with_auth(
        transport, server->token_endpoint, session->dpop_key, client_auth,
        session->refresh_token, session->subject, &token);
    if (status != WF_OK) return status;
    status = wf_oauth_verify_token_subject(transport, token.sub, server->issuer, &audience);
    if (status != WF_OK) {
        wf_oauth_token_response_free(&token);
        return status;
    }
    char *new_access_token = token.access_token ? wf_oauth_strdup(token.access_token) : NULL;
    char *new_refresh_token = token.refresh_token ? wf_oauth_strdup(token.refresh_token) : NULL;
    char *new_scope = token.scope ? wf_oauth_strdup(token.scope) : NULL;
    int64_t new_expires_at = (token.expires_in_present) ? (now + token.expires_in) : session->expires_at;
    free(session->access_token); session->access_token = new_access_token;
    free(session->refresh_token); session->refresh_token = new_refresh_token;
    free(session->scope); session->scope = new_scope;
    free(session->audience); session->audience = audience;
    session->expires_at = new_expires_at;
    wf_oauth_token_response_free(&token);
    return WF_OK;
}

wf_status wf_oauth_revoke(wf_xrpc_client *transport,
                          const char *endpoint,
                          const wf_oauth_dpop_key *dpop_key,
                          const wf_oauth_client_auth *auth,
                          const char *token) {
    wf_xrpc_param params[4];
    size_t count = 0;
    char *assertion = NULL, *form = NULL;
    wf_response response = {0};
    wf_status status;
    if (!transport || !endpoint || !dpop_key || !auth || !token || !*token)
        return WF_ERR_INVALID_ARG;
    params[count++] = (wf_xrpc_param){"token", token};
    status = wf_oauth_add_client_auth(params, &count, auth, &assertion);
    if (status == WF_OK) status = wf_oauth_form_encode(params, count, &form);
    if (status == WF_OK) status = wf_oauth_post(transport, endpoint, dpop_key,
                                                form, &response);
    wf_response_free(&response);
    free(assertion);
    free(form);
    return status;
}
