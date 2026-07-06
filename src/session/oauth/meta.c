#include "internal.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef wf_status (*wf_oauth_parse_metadata_fn)(const char *, size_t,
                                                const char *, void *);

static char *wf_oauth_well_known_url(const char *origin, const char *path) {
    size_t origin_len, needed;
    char *url;
    if (!origin || !path) return NULL;
    origin_len = strlen(origin);
    while (origin_len > 0 && origin[origin_len - 1] == '/') origin_len--;
    needed = origin_len + strlen(path) + 1;
    url = malloc(needed);
    if (!url) return NULL;
    snprintf(url, needed, "%.*s%s", (int)origin_len, origin, path);
    return url;
}

static char *wf_oauth_origin(const char *url) {
    CURLU *parsed = NULL;
    char *scheme = NULL, *host = NULL, *port = NULL, *origin = NULL;
    size_t needed;
    parsed = curl_url();
    if (!parsed || curl_url_set(parsed, CURLUPART_URL, url, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
        goto done;
    }
    if (curl_url_get(parsed, CURLUPART_PORT, &port, 0) != CURLUE_OK) port = NULL;
    needed = strlen(scheme) + strlen("://") + strlen(host) +
             (port ? strlen(port) + 1 : 0) + 1;
    origin = malloc(needed);
    if (!origin) goto done;
    snprintf(origin, needed, "%s://%s%s%s", scheme, host, port ? ":" : "",
             port ? port : "");
done:
    curl_free(scheme);
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(parsed);
    return origin;
}

static wf_status wf_oauth_metadata_get(wf_xrpc_client *transport, const char *url,
                                       const char *expected,
                                       wf_oauth_parse_metadata_fn parse,
                                       void *out) {
    wf_response response = {0};
    wf_status status;
    if (!transport || !url || !expected || !parse || !out) return WF_ERR_INVALID_ARG;
    status = wf_http_get(transport, url, &response);
    if (status == WF_OK && response.status != 200) status = WF_ERR_HTTP;
    if (status == WF_OK) status = parse(response.body, response.body_len, expected, out);
    wf_response_free(&response);
    return status;
}

/* Function-pointer adapters avoid weakening the public typed parse APIs. */
static wf_status wf_oauth_parse_resource_adapter(const char *json, size_t len,
                                                 const char *expected, void *out) {
    return wf_oauth_resource_metadata_parse(json, len, expected, out);
}

static wf_status wf_oauth_parse_server_adapter(const char *json, size_t len,
                                               const char *expected, void *out) {
    return wf_oauth_server_metadata_parse(json, len, expected, out);
}

static wf_status wf_oauth_parse_client_adapter(const char *json, size_t len,
                                               const char *expected, void *out) {
    return wf_oauth_client_metadata_parse(json, len, expected, out);
}

void wf_oauth_resource_metadata_free(wf_oauth_resource_metadata *metadata) {
    if (!metadata) return;
    free(metadata->resource);
    wf_oauth_string_list_free(&metadata->authorization_servers);
    wf_oauth_string_list_free(&metadata->scopes_supported);
    memset(metadata, 0, sizeof(*metadata));
}

void wf_oauth_server_metadata_free(wf_oauth_server_metadata *metadata) {
    if (!metadata) return;
    free(metadata->issuer);
    free(metadata->authorization_endpoint);
    free(metadata->token_endpoint);
    free(metadata->revocation_endpoint);
    free(metadata->pushed_authorization_request_endpoint);
    wf_oauth_string_list_free(&metadata->response_types_supported);
    wf_oauth_string_list_free(&metadata->grant_types_supported);
    wf_oauth_string_list_free(&metadata->code_challenge_methods_supported);
    wf_oauth_string_list_free(&metadata->token_endpoint_auth_methods_supported);
    wf_oauth_string_list_free(&metadata->token_endpoint_auth_signing_alg_values_supported);
    wf_oauth_string_list_free(&metadata->scopes_supported);
    wf_oauth_string_list_free(&metadata->dpop_signing_alg_values_supported);
    wf_oauth_string_list_free(&metadata->protected_resources);
    memset(metadata, 0, sizeof(*metadata));
}

void wf_oauth_client_metadata_free(wf_oauth_client_metadata *metadata) {
    if (!metadata) return;
    free(metadata->client_id);
    free(metadata->client_name);
    free(metadata->client_uri);
    free(metadata->scope);
    free(metadata->token_endpoint_auth_method);
    free(metadata->token_endpoint_auth_signing_alg);
    free(metadata->jwks_uri);
    free(metadata->jwks_json);
    free(metadata->application_type);
    wf_oauth_string_list_free(&metadata->redirect_uris);
    wf_oauth_string_list_free(&metadata->response_types);
    wf_oauth_string_list_free(&metadata->grant_types);
    memset(metadata, 0, sizeof(*metadata));
}

wf_status wf_oauth_resource_metadata_parse(const char *json, size_t json_len,
                                           const char *expected_resource,
                                           wf_oauth_resource_metadata *out) {
    cJSON *root = NULL;
    wf_status status;
    if (!json || json_len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_oauth_json_string(root, "resource", 1, &out->resource);
    if (status == WF_OK) status = wf_oauth_json_array(root, "authorization_servers", 1,
                                                       &out->authorization_servers);
    if (status == WF_OK) status = wf_oauth_json_array(root, "scopes_supported", 0,
                                                       &out->scopes_supported);
    cJSON_Delete(root);
    if (status == WF_OK && !wf_oauth_url_valid(out->resource, 1, 1, 0)) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && expected_resource && strcmp(out->resource, expected_resource) != 0) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && out->authorization_servers.count != 1) status = WF_ERR_PARSE;
    if (status == WF_OK &&
        !wf_oauth_url_valid(out->authorization_servers.items[0], 1, 1, 0)) {
        status = WF_ERR_PARSE;
    }
    if (status != WF_OK) wf_oauth_resource_metadata_free(out);
    return status;
}

wf_status wf_oauth_server_metadata_parse(const char *json, size_t json_len,
                                         const char *expected_issuer,
                                         wf_oauth_server_metadata *out) {
    cJSON *root = NULL;
    wf_status status;
    if (!json || json_len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
#define WF_OAUTH_GET_STRING(field, name, required) \
    do { if (status == WF_OK) status = wf_oauth_json_string(root, name, required, &out->field); } while (0)
#define WF_OAUTH_GET_ARRAY(field, name) \
    do { if (status == WF_OK) status = wf_oauth_json_array(root, name, 1, &out->field); } while (0)
    status = WF_OK;
    WF_OAUTH_GET_STRING(issuer, "issuer", 1);
    WF_OAUTH_GET_STRING(authorization_endpoint, "authorization_endpoint", 1);
    WF_OAUTH_GET_STRING(token_endpoint, "token_endpoint", 1);
    WF_OAUTH_GET_STRING(revocation_endpoint, "revocation_endpoint", 0);
    WF_OAUTH_GET_STRING(pushed_authorization_request_endpoint,
                        "pushed_authorization_request_endpoint", 1);
    WF_OAUTH_GET_ARRAY(response_types_supported, "response_types_supported");
    WF_OAUTH_GET_ARRAY(grant_types_supported, "grant_types_supported");
    WF_OAUTH_GET_ARRAY(code_challenge_methods_supported, "code_challenge_methods_supported");
    WF_OAUTH_GET_ARRAY(token_endpoint_auth_methods_supported,
                       "token_endpoint_auth_methods_supported");
    WF_OAUTH_GET_ARRAY(token_endpoint_auth_signing_alg_values_supported,
                       "token_endpoint_auth_signing_alg_values_supported");
    WF_OAUTH_GET_ARRAY(scopes_supported, "scopes_supported");
    WF_OAUTH_GET_ARRAY(dpop_signing_alg_values_supported,
                       "dpop_signing_alg_values_supported");
    if (status == WF_OK) status = wf_oauth_json_array(root, "protected_resources", 0,
                                                       &out->protected_resources);
    if (status == WF_OK) status = wf_oauth_json_bool(
        root, "authorization_response_iss_parameter_supported",
        &out->authorization_response_iss_parameter_supported, NULL);
    if (status == WF_OK) status = wf_oauth_json_bool(
        root, "require_pushed_authorization_requests",
        &out->require_pushed_authorization_requests, NULL);
    if (status == WF_OK) status = wf_oauth_json_bool(
        root, "require_request_uri_registration", &out->require_request_uri_registration,
        &out->require_request_uri_registration_present);
    if (status == WF_OK) status = wf_oauth_json_bool(
        root, "client_id_metadata_document_supported",
        &out->client_id_metadata_document_supported, NULL);
#undef WF_OAUTH_GET_ARRAY
#undef WF_OAUTH_GET_STRING
    cJSON_Delete(root);
    if (status == WF_OK && (!wf_oauth_url_valid(out->issuer, 1, 1, 0) ||
                            !wf_oauth_url_valid(out->authorization_endpoint, 1, 0, 0) ||
                            !wf_oauth_url_valid(out->token_endpoint, 1, 0, 0) ||
                            (out->revocation_endpoint &&
                             !wf_oauth_url_valid(out->revocation_endpoint, 1, 0, 0)) ||
                            !wf_oauth_url_valid(out->pushed_authorization_request_endpoint,
                                                1, 0, 0))) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && expected_issuer && strcmp(out->issuer, expected_issuer) != 0) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK &&
        (!wf_oauth_string_list_has(&out->response_types_supported, "code") ||
         !wf_oauth_string_list_has(&out->grant_types_supported, "authorization_code") ||
         !wf_oauth_string_list_has(&out->grant_types_supported, "refresh_token") ||
         !wf_oauth_string_list_has(&out->code_challenge_methods_supported, "S256") ||
         !wf_oauth_string_list_has(&out->token_endpoint_auth_methods_supported, "none") ||
         !wf_oauth_string_list_has(&out->token_endpoint_auth_methods_supported,
                                   "private_key_jwt") ||
         !wf_oauth_string_list_has(&out->token_endpoint_auth_signing_alg_values_supported,
                                   "ES256") ||
         wf_oauth_string_list_has(&out->token_endpoint_auth_signing_alg_values_supported,
                                  "none") ||
         !wf_oauth_string_list_has(&out->scopes_supported, "atproto") ||
         !wf_oauth_string_list_has(&out->dpop_signing_alg_values_supported, "ES256") ||
         !out->authorization_response_iss_parameter_supported ||
         !out->require_pushed_authorization_requests ||
         (out->require_request_uri_registration_present &&
          !out->require_request_uri_registration) ||
         !out->client_id_metadata_document_supported)) {
        status = WF_ERR_PARSE;
    }
    if (status != WF_OK) wf_oauth_server_metadata_free(out);
    return status;
}

static wf_status wf_oauth_default_array(wf_oauth_string_list *list, const char *value) {
    list->items = calloc(1, sizeof(*list->items));
    if (!list->items) return WF_ERR_ALLOC;
    list->items[0] = wf_oauth_strdup(value);
    if (!list->items[0]) {
        wf_oauth_string_list_free(list);
        return WF_ERR_ALLOC;
    }
    list->count = 1;
    return WF_OK;
}

wf_status wf_oauth_client_metadata_parse(const char *json, size_t json_len,
                                         const char *expected_client_id,
                                         wf_oauth_client_metadata *out) {
    cJSON *root = NULL;
    wf_status status;
    int has_jwks;
    if (!json || json_len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_oauth_json_string(root, "client_id", 1, &out->client_id);
    if (status == WF_OK) status = wf_oauth_json_string(root, "client_name", 0,
                                                        &out->client_name);
    if (status == WF_OK) status = wf_oauth_json_string(root, "client_uri", 0,
                                                        &out->client_uri);
    if (status == WF_OK) status = wf_oauth_json_string(root, "scope", 1, &out->scope);
    if (status == WF_OK) status = wf_oauth_json_string(
        root, "token_endpoint_auth_method", 1, &out->token_endpoint_auth_method);
    if (status == WF_OK) status = wf_oauth_json_string(
        root, "token_endpoint_auth_signing_alg", 0,
        &out->token_endpoint_auth_signing_alg);
    if (status == WF_OK) status = wf_oauth_json_string(root, "jwks_uri", 0,
                                                        &out->jwks_uri);
    if (status == WF_OK) status = wf_oauth_json_object_encoded(root, "jwks",
                                                                &out->jwks_json);
    if (status == WF_OK) status = wf_oauth_json_string(root, "application_type", 0,
                                                        &out->application_type);
    if (status == WF_OK && !out->application_type) {
        out->application_type = wf_oauth_strdup("web");
        if (!out->application_type) status = WF_ERR_ALLOC;
    }
    if (status == WF_OK) status = wf_oauth_json_array(root, "redirect_uris", 1,
                                                       &out->redirect_uris);
    if (status == WF_OK) status = wf_oauth_json_array(root, "response_types", 0,
                                                       &out->response_types);
    if (status == WF_OK && out->response_types.count == 0) {
        status = wf_oauth_default_array(&out->response_types, "code");
    }
    if (status == WF_OK) status = wf_oauth_json_array(root, "grant_types", 0,
                                                       &out->grant_types);
    if (status == WF_OK && out->grant_types.count == 0) {
        status = wf_oauth_default_array(&out->grant_types, "authorization_code");
    }
    if (status == WF_OK) status = wf_oauth_json_bool(
        root, "dpop_bound_access_tokens", &out->dpop_bound_access_tokens,
        &out->dpop_bound_access_tokens_present);
    cJSON_Delete(root);
    has_jwks = out->jwks_json != NULL;
    if (status == WF_OK && (!wf_oauth_client_id_valid(out->client_id) ||
                            (expected_client_id &&
                             strcmp(out->client_id, expected_client_id) != 0) ||
                            !wf_oauth_scope_has(out->scope, "atproto") ||
                            !wf_oauth_string_list_has(&out->response_types, "code") ||
                            !wf_oauth_string_list_has(&out->grant_types,
                                                      "authorization_code") ||
                            !out->dpop_bound_access_tokens_present ||
                            !out->dpop_bound_access_tokens ||
                            (out->jwks_uri && has_jwks))) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && out->client_uri &&
        (!wf_oauth_url_valid(out->client_uri, 1, 0, 0) ||
         !wf_oauth_url_hosts_equal(out->client_id, out->client_uri))) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && out->jwks_uri &&
        !wf_oauth_url_valid(out->jwks_uri, 1, 0, 0)) status = WF_ERR_PARSE;
    if (status == WF_OK && strcmp(out->token_endpoint_auth_method, "none") == 0) {
        if (out->token_endpoint_auth_signing_alg) status = WF_ERR_PARSE;
    } else if (status == WF_OK &&
               strcmp(out->token_endpoint_auth_method, "private_key_jwt") == 0) {
        if (!out->token_endpoint_auth_signing_alg ||
            strcmp(out->token_endpoint_auth_signing_alg, "none") == 0 ||
            (!out->jwks_uri && !has_jwks)) {
            status = WF_ERR_PARSE;
        }
    } else if (status == WF_OK) {
        status = WF_ERR_PARSE;
    }
    if (status != WF_OK) wf_oauth_client_metadata_free(out);
    return status;
}

wf_status wf_oauth_resource_metadata_get(wf_xrpc_client *transport,
                                         const char *resource,
                                         wf_oauth_resource_metadata *out) {
    char *url, *origin;
    wf_status status;
    if (!transport || !resource || !out || !wf_oauth_url_valid(resource, 1, 1, 0)) {
        return WF_ERR_INVALID_ARG;
    }
    origin = wf_oauth_origin(resource);
    if (!origin) return WF_ERR_ALLOC;
    url = wf_oauth_well_known_url(origin, "/.well-known/oauth-protected-resource");
    if (!url) {
        free(origin);
        return WF_ERR_ALLOC;
    }
    status = wf_oauth_metadata_get(transport, url, origin,
                                   wf_oauth_parse_resource_adapter, out);
    free(url);
    free(origin);
    return status;
}

wf_status wf_oauth_server_metadata_get(wf_xrpc_client *transport,
                                       const char *issuer,
                                       wf_oauth_server_metadata *out) {
    char *url, *origin;
    wf_status status;
    if (!transport || !issuer || !out || !wf_oauth_url_valid(issuer, 1, 1, 0)) {
        return WF_ERR_INVALID_ARG;
    }
    origin = wf_oauth_origin(issuer);
    if (!origin) return WF_ERR_ALLOC;
    url = wf_oauth_well_known_url(origin, "/.well-known/oauth-authorization-server");
    if (!url) {
        free(origin);
        return WF_ERR_ALLOC;
    }
    status = wf_oauth_metadata_get(transport, url, origin,
                                   wf_oauth_parse_server_adapter, out);
    free(url);
    free(origin);
    return status;
}

wf_status wf_oauth_client_metadata_get(wf_xrpc_client *transport,
                                       const char *client_id,
                                       wf_oauth_client_metadata *out) {
    if (!transport || !client_id || !out || !wf_oauth_client_id_valid(client_id)) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_oauth_metadata_get(transport, client_id, client_id,
                                 wf_oauth_parse_client_adapter, out);
}

wf_status wf_oauth_discover(wf_xrpc_client *transport,
                            const char *resource,
                            wf_oauth_resource_metadata *resource_out,
                            wf_oauth_server_metadata *server_out) {
    wf_status status;
    if (!transport || !resource || !resource_out || !server_out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(resource_out, 0, sizeof(*resource_out));
    memset(server_out, 0, sizeof(*server_out));
    status = wf_oauth_resource_metadata_get(transport, resource, resource_out);
    if (status != WF_OK) return status;
    status = wf_oauth_server_metadata_get(transport,
                                          resource_out->authorization_servers.items[0],
                                          server_out);
    if (status == WF_OK && server_out->protected_resources.count > 0 &&
        !wf_oauth_string_list_has(&server_out->protected_resources,
                                  resource_out->resource)) {
        status = WF_ERR_PARSE;
    }
    if (status != WF_OK) {
        wf_oauth_resource_metadata_free(resource_out);
        wf_oauth_server_metadata_free(server_out);
    }
    return status;
}
