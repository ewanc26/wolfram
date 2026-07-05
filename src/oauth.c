/**
 * oauth.c — AT Protocol OAuth metadata, PKCE, and DPoP foundations.
 */

#include "wolfram/oauth.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct wf_oauth_dpop_key {
    EC_KEY *ec;
};

static char *wf_oauth_strdup(const char *value) {
    size_t len;
    char *copy;
    if (!value) return NULL;
    len = strlen(value);
    copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

static void wf_oauth_string_list_free(wf_oauth_string_list *list) {
    size_t i;
    if (!list) return;
    for (i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int wf_oauth_string_list_has(const wf_oauth_string_list *list,
                                    const char *value) {
    size_t i;
    if (!list || !value) return 0;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], value) == 0) return 1;
    }
    return 0;
}

static wf_status wf_oauth_json_string(const cJSON *object, const char *name,
                                       int required, char **out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *out = NULL;
    if (!item) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
        return WF_ERR_PARSE;
    }
    *out = wf_oauth_strdup(item->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status wf_oauth_json_array(const cJSON *object, const char *name,
                                      int required, wf_oauth_string_list *out) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
    const cJSON *item;
    size_t index = 0;
    memset(out, 0, sizeof(*out));
    if (!array) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) == 0) return WF_ERR_PARSE;
    out->count = (size_t)cJSON_GetArraySize(array);
    out->items = calloc(out->count, sizeof(*out->items));
    if (!out->items) return WF_ERR_ALLOC;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
            wf_oauth_string_list_free(out);
            return WF_ERR_PARSE;
        }
        out->items[index] = wf_oauth_strdup(item->valuestring);
        if (!out->items[index]) {
            wf_oauth_string_list_free(out);
            return WF_ERR_ALLOC;
        }
        index++;
    }
    return WF_OK;
}

static wf_status wf_oauth_json_bool(const cJSON *object, const char *name,
                                     int *value, int *present) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *value = 0;
    if (present) *present = item != NULL;
    if (!item) return WF_OK;
    if (!cJSON_IsBool(item)) return WF_ERR_PARSE;
    *value = cJSON_IsTrue(item) ? 1 : 0;
    return WF_OK;
}

static wf_status wf_oauth_json_object_encoded(const cJSON *object, const char *name,
                                               char **out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *out = NULL;
    if (!item) return WF_OK;
    if (!cJSON_IsObject(item)) return WF_ERR_PARSE;
    *out = cJSON_PrintUnformatted(item);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

/* Validate absolute URLs using libcurl's maintained URL parser. */
static int wf_oauth_url_valid(const char *url, int https_only, int origin_only,
                              int reject_port) {
    CURLU *parsed = NULL;
    char *scheme = NULL, *host = NULL, *part = NULL;
    int valid = 0;
    if (!url || !*url) return 0;
    parsed = curl_url();
    if (!parsed) return 0;
    if (curl_url_set(parsed, CURLUPART_URL, url, CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK) {
        goto done;
    }
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        !scheme || !host || !*host) {
        goto done;
    }
    if (https_only && strcmp(scheme, "https") != 0) goto done;
    if (!https_only && strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0) {
        goto done;
    }
    if (curl_url_get(parsed, CURLUPART_USER, &part, 0) == CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_PASSWORD, &part, 0) == CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_FRAGMENT, &part, 0) == CURLUE_OK) goto done;
    if (reject_port && curl_url_get(parsed, CURLUPART_PORT, &part, 0) == CURLUE_OK) goto done;
    if (origin_only && curl_url_get(parsed, CURLUPART_PORT, &part, 0) == CURLUE_OK) {
        if ((strcmp(scheme, "https") == 0 && strcmp(part, "443") == 0) ||
            (strcmp(scheme, "http") == 0 && strcmp(part, "80") == 0)) {
            goto done;
        }
        curl_free(part);
        part = NULL;
    }
    if (origin_only) {
        if (curl_url_get(parsed, CURLUPART_QUERY, &part, 0) == CURLUE_OK) goto done;
        if (curl_url_get(parsed, CURLUPART_PATH, &part, 0) == CURLUE_OK &&
            part && strcmp(part, "/") != 0 && part[0] != '\0') {
            goto done;
        }
        curl_free(part);
        part = NULL;
    }
    valid = 1;
done:
    curl_free(part);
    curl_free(host);
    curl_free(scheme);
    curl_url_cleanup(parsed);
    return valid;
}

static int wf_oauth_ascii_equal_fold(const char *left, const char *right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        left++;
        right++;
    }
    return *left == *right;
}

static int wf_oauth_url_hosts_equal(const char *left, const char *right) {
    CURLU *left_url = NULL, *right_url = NULL;
    char *left_host = NULL, *right_host = NULL;
    int equal = 0;
    left_url = curl_url();
    right_url = curl_url();
    if (!left_url || !right_url ||
        curl_url_set(left_url, CURLUPART_URL, left, 0) != CURLUE_OK ||
        curl_url_set(right_url, CURLUPART_URL, right, 0) != CURLUE_OK ||
        curl_url_get(left_url, CURLUPART_HOST, &left_host, 0) != CURLUE_OK ||
        curl_url_get(right_url, CURLUPART_HOST, &right_host, 0) != CURLUE_OK) {
        goto done;
    }
    equal = wf_oauth_ascii_equal_fold(left_host, right_host);
done:
    curl_free(left_host);
    curl_free(right_host);
    curl_url_cleanup(left_url);
    curl_url_cleanup(right_url);
    return equal;
}

static int wf_oauth_client_id_valid(const char *client_id) {
    CURLU *parsed = NULL;
    char *path = NULL, *host = NULL;
    size_t path_len, i;
    int host_is_ipv4 = 1, valid = 0;
    if (!wf_oauth_url_valid(client_id, 1, 0, 1)) return 0;
    parsed = curl_url();
    if (!parsed || curl_url_set(parsed, CURLUPART_URL, client_id, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_PATH, &path, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
        goto done;
    }
    path_len = strlen(path);
    if (path_len <= 1 || path[path_len - 1] == '/') goto done;
    if (strchr(host, ':')) goto done; /* IPv6 literal. */
    for (i = 0; host[i]; i++) {
        if (!isdigit((unsigned char)host[i]) && host[i] != '.') {
            host_is_ipv4 = 0;
            break;
        }
    }
    if (host_is_ipv4) goto done;
    valid = 1;
done:
    curl_free(path);
    curl_free(host);
    curl_url_cleanup(parsed);
    return valid;
}

static int wf_oauth_scope_has(const char *scope, const char *wanted) {
    const char *cursor;
    size_t wanted_len;
    if (!scope || !wanted) return 0;
    cursor = scope;
    wanted_len = strlen(wanted);
    while (*cursor) {
        const char *end;
        while (*cursor == ' ') cursor++;
        end = strchr(cursor, ' ');
        if (!end) end = cursor + strlen(cursor);
        if ((size_t)(end - cursor) == wanted_len &&
            memcmp(cursor, wanted, wanted_len) == 0) {
            return 1;
        }
        cursor = end;
    }
    return 0;
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

typedef wf_status (*wf_oauth_parse_metadata_fn)(const char *, size_t,
                                                 const char *, void *);

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

static wf_status wf_oauth_base64url(const unsigned char *input, size_t input_len,
                                     char **out) {
    size_t encoded_len = 4 * ((input_len + 2) / 3);
    char *encoded;
    int result;
    if ((!input && input_len != 0) || !out || input_len > (size_t)INT_MAX) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    encoded = malloc(encoded_len + 1);
    if (!encoded) return WF_ERR_ALLOC;
    result = EVP_EncodeBlock((unsigned char *)encoded, input, (int)input_len);
    if (result < 0) {
        free(encoded);
        return WF_ERR_PARSE;
    }
    while (result > 0 && encoded[result - 1] == '=') result--;
    encoded[result] = '\0';
    for (int i = 0; i < result; i++) {
        if (encoded[i] == '+') encoded[i] = '-';
        else if (encoded[i] == '/') encoded[i] = '_';
    }
    *out = encoded;
    return WF_OK;
}

static int wf_oauth_pkce_char_valid(unsigned char value) {
    return isalnum(value) || value == '-' || value == '.' || value == '_' || value == '~';
}

wf_status wf_oauth_pkce_from_verifier(const char *verifier, wf_oauth_pkce *out) {
    size_t len, i;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char *challenge = NULL;
    wf_status status;
    if (!verifier || !out) return WF_ERR_INVALID_ARG;
    len = strlen(verifier);
    if (len < 43 || len > WF_OAUTH_PKCE_VERIFIER_MAX) return WF_ERR_INVALID_ARG;
    for (i = 0; i < len; i++) {
        if (!wf_oauth_pkce_char_valid((unsigned char)verifier[i])) return WF_ERR_INVALID_ARG;
    }
    SHA256((const unsigned char *)verifier, len, digest);
    status = wf_oauth_base64url(digest, sizeof(digest), &challenge);
    if (status != WF_OK) return status;
    if (strlen(challenge) != WF_OAUTH_PKCE_CHALLENGE_LEN) {
        free(challenge);
        return WF_ERR_PARSE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->verifier, verifier, len + 1);
    memcpy(out->challenge, challenge, WF_OAUTH_PKCE_CHALLENGE_LEN + 1);
    free(challenge);
    return WF_OK;
}

wf_status wf_oauth_pkce_generate(wf_oauth_pkce *out) {
    unsigned char random[32];
    char *verifier = NULL;
    wf_status status;
    if (!out) return WF_ERR_INVALID_ARG;
    if (RAND_bytes(random, sizeof(random)) != 1) return WF_ERR_PARSE;
    status = wf_oauth_base64url(random, sizeof(random), &verifier);
    if (status == WF_OK) status = wf_oauth_pkce_from_verifier(verifier, out);
    free(verifier);
    return status;
}

static wf_status wf_oauth_dpop_key_wrap(EC_KEY *ec, wf_oauth_dpop_key **out) {
    wf_oauth_dpop_key *key;
    if (!ec || !out) return WF_ERR_INVALID_ARG;
    key = calloc(1, sizeof(*key));
    if (!key) {
        EC_KEY_free(ec);
        return WF_ERR_ALLOC;
    }
    key->ec = ec;
    *out = key;
    return WF_OK;
}

wf_status wf_oauth_dpop_key_generate(wf_oauth_dpop_key **out) {
    EC_KEY *ec;
    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ec) return WF_ERR_ALLOC;
    if (EC_KEY_generate_key(ec) != 1) {
        EC_KEY_free(ec);
        return WF_ERR_PARSE;
    }
    return wf_oauth_dpop_key_wrap(ec, out);
}

wf_status wf_oauth_dpop_key_import(const unsigned char private_key[32],
                                    wf_oauth_dpop_key **out) {
    EC_KEY *ec = NULL;
    BIGNUM *scalar = NULL, *order = NULL;
    EC_POINT *public_key = NULL;
    const EC_GROUP *group;
    wf_status status = WF_ERR_PARSE;
    if (!private_key || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    scalar = BN_bin2bn(private_key, 32, NULL);
    order = BN_new();
    if (!ec || !scalar || !order) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    group = EC_KEY_get0_group(ec);
    if (EC_GROUP_get_order(group, order, NULL) != 1 || BN_is_zero(scalar) ||
        BN_is_negative(scalar) || BN_cmp(scalar, order) >= 0) {
        goto done;
    }
    public_key = EC_POINT_new(group);
    if (!public_key) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (EC_POINT_mul(group, public_key, scalar, NULL, NULL, NULL) != 1 ||
        EC_KEY_set_private_key(ec, scalar) != 1 ||
        EC_KEY_set_public_key(ec, public_key) != 1 ||
        EC_KEY_check_key(ec) != 1) {
        goto done;
    }
    status = wf_oauth_dpop_key_wrap(ec, out);
    if (status == WF_OK) ec = NULL;
done:
    EC_POINT_free(public_key);
    BN_clear_free(scalar);
    BN_free(order);
    EC_KEY_free(ec);
    return status;
}

wf_status wf_oauth_dpop_key_export(const wf_oauth_dpop_key *key,
                                    unsigned char private_key_out[32]) {
    const BIGNUM *scalar;
    if (!key || !key->ec || !private_key_out) return WF_ERR_INVALID_ARG;
    scalar = EC_KEY_get0_private_key(key->ec);
    if (!scalar || BN_bn2binpad(scalar, private_key_out, 32) != 32) return WF_ERR_PARSE;
    return WF_OK;
}

void wf_oauth_dpop_key_free(wf_oauth_dpop_key *key) {
    if (!key) return;
    EC_KEY_free(key->ec);
    free(key);
}

static wf_status wf_oauth_dpop_coordinates(const wf_oauth_dpop_key *key,
                                            unsigned char x[32],
                                            unsigned char y[32]) {
    const EC_GROUP *group;
    const EC_POINT *point;
    BIGNUM *x_bn = NULL, *y_bn = NULL;
    wf_status status = WF_ERR_PARSE;
    if (!key || !key->ec) return WF_ERR_INVALID_ARG;
    group = EC_KEY_get0_group(key->ec);
    point = EC_KEY_get0_public_key(key->ec);
    if (!group || !point) return WF_ERR_PARSE;
    x_bn = BN_new();
    y_bn = BN_new();
    if (!x_bn || !y_bn) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (EC_POINT_get_affine_coordinates_GFp(group, point, x_bn, y_bn, NULL) != 1 ||
        BN_bn2binpad(x_bn, x, 32) != 32 || BN_bn2binpad(y_bn, y, 32) != 32) {
        goto done;
    }
    status = WF_OK;
done:
    BN_free(x_bn);
    BN_free(y_bn);
    return status;
}

static wf_status wf_oauth_dpop_jwk(const wf_oauth_dpop_key *key, cJSON **out,
                                    char **x_out, char **y_out) {
    unsigned char x[32], y[32];
    cJSON *jwk = NULL;
    wf_status status;
    *out = NULL;
    *x_out = NULL;
    *y_out = NULL;
    status = wf_oauth_dpop_coordinates(key, x, y);
    if (status != WF_OK) return status;
    status = wf_oauth_base64url(x, sizeof(x), x_out);
    if (status == WF_OK) status = wf_oauth_base64url(y, sizeof(y), y_out);
    if (status != WF_OK) goto done;
    jwk = cJSON_CreateObject();
    if (!jwk || !cJSON_AddStringToObject(jwk, "kty", "EC") ||
        !cJSON_AddStringToObject(jwk, "crv", "P-256") ||
        !cJSON_AddStringToObject(jwk, "x", *x_out) ||
        !cJSON_AddStringToObject(jwk, "y", *y_out)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    *out = jwk;
    return WF_OK;
done:
    cJSON_Delete(jwk);
    free(*x_out);
    free(*y_out);
    *x_out = NULL;
    *y_out = NULL;
    return status;
}

wf_status wf_oauth_dpop_key_thumbprint(const wf_oauth_dpop_key *key,
                                        char thumbprint_out[44]) {
    cJSON *unused = NULL;
    char *x = NULL, *y = NULL, *canonical = NULL, *encoded = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    size_t needed;
    wf_status status;
    if (!key || !thumbprint_out) return WF_ERR_INVALID_ARG;
    status = wf_oauth_dpop_jwk(key, &unused, &x, &y);
    if (status != WF_OK) return status;
    needed = strlen(x) + strlen(y) + strlen("{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"\",\"y\":\"\"}") + 1;
    canonical = malloc(needed);
    if (!canonical) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(canonical, needed,
             "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\"}",
             x, y);
    SHA256((const unsigned char *)canonical, strlen(canonical), digest);
    status = wf_oauth_base64url(digest, sizeof(digest), &encoded);
    if (status == WF_OK && strlen(encoded) != 43) status = WF_ERR_PARSE;
    if (status == WF_OK) memcpy(thumbprint_out, encoded, 44);
done:
    cJSON_Delete(unused);
    free(x);
    free(y);
    free(canonical);
    free(encoded);
    return status;
}

static char *wf_oauth_htu(const char *uri) {
    CURLU *parsed;
    char *scheme = NULL, *host = NULL, *credentials = NULL;
    const char *query, *fragment, *end;
    size_t len;
    char *htu;
    if (!uri) return NULL;
    parsed = curl_url();
    if (!parsed) return NULL;
    if (curl_url_set(parsed, CURLUPART_URL, uri, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0)) goto invalid;
    if (curl_url_get(parsed, CURLUPART_USER, &credentials, 0) == CURLUE_OK) goto invalid;
    if (curl_url_get(parsed, CURLUPART_PASSWORD, &credentials, 0) == CURLUE_OK) goto invalid;
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);
    query = strchr(uri, '?');
    fragment = strchr(uri, '#');
    end = uri + strlen(uri);
    if (query && query < end) end = query;
    if (fragment && fragment < end) end = fragment;
    len = (size_t)(end - uri);
    htu = malloc(len + 1);
    if (!htu) return NULL;
    memcpy(htu, uri, len);
    htu[len] = '\0';
    return htu;
invalid:
    curl_free(scheme);
    curl_free(host);
    curl_free(credentials);
    curl_url_cleanup(parsed);
    return NULL;
}

static int wf_oauth_http_method_valid(const char *method) {
    const unsigned char *cursor = (const unsigned char *)method;
    if (!cursor || !*cursor) return 0;
    while (*cursor) {
        if (!isupper(*cursor) && !isdigit(*cursor) && strchr("!#$%&'*+-.^_`|~", *cursor) == NULL) {
            return 0;
        }
        cursor++;
    }
    return 1;
}

static wf_status wf_oauth_random_jti(char **out) {
    unsigned char random[16];
    if (RAND_bytes(random, sizeof(random)) != 1) return WF_ERR_PARSE;
    return wf_oauth_base64url(random, sizeof(random), out);
}

static wf_status wf_oauth_es256_sign(const wf_oauth_dpop_key *key,
                                      const unsigned char *input, size_t input_len,
                                      unsigned char signature[64]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    ECDSA_SIG *sig;
    const BIGNUM *r, *s;
    if (!key || !key->ec || !input || !signature) return WF_ERR_INVALID_ARG;
    SHA256(input, input_len, digest);
    sig = ECDSA_do_sign(digest, sizeof(digest), key->ec);
    if (!sig) return WF_ERR_PARSE;
    ECDSA_SIG_get0(sig, &r, &s);
    if (BN_bn2binpad(r, signature, 32) != 32 ||
        BN_bn2binpad(s, signature + 32, 32) != 32) {
        ECDSA_SIG_free(sig);
        return WF_ERR_PARSE;
    }
    ECDSA_SIG_free(sig);
    return WF_OK;
}

wf_status wf_oauth_dpop_proof_create(const wf_oauth_dpop_key *key,
                                      const wf_oauth_dpop_proof_options *options,
                                      char **jwt_out) {
    cJSON *header = NULL, *payload = NULL, *jwk = NULL;
    char *x = NULL, *y = NULL, *header_json = NULL, *payload_json = NULL;
    char *header_b64 = NULL, *payload_b64 = NULL, *signature_b64 = NULL;
    char *htu = NULL, *jti = NULL, *ath = NULL, *signing_input = NULL, *jwt = NULL;
    unsigned char signature[64], digest[SHA256_DIGEST_LENGTH];
    size_t input_len, jwt_len;
    int64_t issued_at;
    wf_status status;
    if (!key || !options || !jwt_out ||
        !wf_oauth_http_method_valid(options->http_method) || !options->http_uri) {
        return WF_ERR_INVALID_ARG;
    }
    *jwt_out = NULL;
    htu = wf_oauth_htu(options->http_uri);
    if (!htu) return WF_ERR_INVALID_ARG;
    if (options->jti) {
        if (!*options->jti) {
            status = WF_ERR_INVALID_ARG;
            goto done;
        }
        jti = wf_oauth_strdup(options->jti);
        status = jti ? WF_OK : WF_ERR_ALLOC;
    } else {
        status = wf_oauth_random_jti(&jti);
    }
    if (status != WF_OK) goto done;
    status = wf_oauth_dpop_jwk(key, &jwk, &x, &y);
    if (status != WF_OK) goto done;
    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (!header || !payload ||
        !cJSON_AddStringToObject(header, "typ", "dpop+jwt") ||
        !cJSON_AddStringToObject(header, "alg", "ES256")) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (!cJSON_AddItemToObject(header, "jwk", jwk)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    jwk = NULL; /* ownership moved to header */
    if (!cJSON_AddStringToObject(payload, "jti", jti) ||
        !cJSON_AddStringToObject(payload, "htm", options->http_method) ||
        !cJSON_AddStringToObject(payload, "htu", htu)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    issued_at = options->issued_at > 0 ? options->issued_at : (int64_t)time(NULL);
    if (!cJSON_AddNumberToObject(payload, "iat", (double)issued_at)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (options->nonce &&
        !cJSON_AddStringToObject(payload, "nonce", options->nonce)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (options->access_token) {
        SHA256((const unsigned char *)options->access_token,
               strlen(options->access_token), digest);
        status = wf_oauth_base64url(digest, sizeof(digest), &ath);
        if (status != WF_OK || !cJSON_AddStringToObject(payload, "ath", ath)) {
            if (status == WF_OK) status = WF_ERR_ALLOC;
            goto done;
        }
    }
    header_json = cJSON_PrintUnformatted(header);
    payload_json = cJSON_PrintUnformatted(payload);
    if (!header_json || !payload_json) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    status = wf_oauth_base64url((const unsigned char *)header_json,
                                strlen(header_json), &header_b64);
    if (status == WF_OK) status = wf_oauth_base64url(
        (const unsigned char *)payload_json, strlen(payload_json), &payload_b64);
    if (status != WF_OK) goto done;
    input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    signing_input = malloc(input_len + 1);
    if (!signing_input) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(signing_input, input_len + 1, "%s.%s", header_b64, payload_b64);
    status = wf_oauth_es256_sign(key, (const unsigned char *)signing_input,
                                 input_len, signature);
    if (status == WF_OK) status = wf_oauth_base64url(signature, sizeof(signature),
                                                      &signature_b64);
    if (status != WF_OK) goto done;
    jwt_len = input_len + 1 + strlen(signature_b64);
    jwt = malloc(jwt_len + 1);
    if (!jwt) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(jwt, jwt_len + 1, "%s.%s", signing_input, signature_b64);
    *jwt_out = jwt;
    jwt = NULL;
    status = WF_OK;
done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    cJSON_Delete(jwk);
    free(x);
    free(y);
    free(header_json);
    free(payload_json);
    free(header_b64);
    free(payload_b64);
    free(signature_b64);
    free(htu);
    free(jti);
    free(ath);
    free(signing_input);
    free(jwt);
    return status;
}

wf_status wf_oauth_client_assertion_create(
    const wf_oauth_dpop_key *key,
    const wf_oauth_client_assertion_options *options,
    char **jwt_out) {
    cJSON *header = NULL, *payload = NULL;
    char *jti = NULL, *header_json = NULL, *payload_json = NULL;
    char *header_b64 = NULL, *payload_b64 = NULL, *signature_b64 = NULL;
    char *signing_input = NULL, *jwt = NULL;
    unsigned char signature[64];
    size_t input_len, jwt_len;
    int64_t issued_at;
    wf_status status;
    if (!key || !options || !jwt_out || !options->client_id ||
        !*options->client_id || !options->authorization_server_issuer ||
        !wf_oauth_url_valid(options->authorization_server_issuer, 1, 1, 0) ||
        !options->key_id || !*options->key_id) return WF_ERR_INVALID_ARG;
    *jwt_out = NULL;
    issued_at = options->issued_at > 0 ? options->issued_at : (int64_t)time(NULL);
    if (issued_at <= 0 || issued_at > INT64_MAX - 60) return WF_ERR_INVALID_ARG;
    if (options->jti) {
        if (!*options->jti) return WF_ERR_INVALID_ARG;
        jti = wf_oauth_strdup(options->jti);
        status = jti ? WF_OK : WF_ERR_ALLOC;
    } else {
        status = wf_oauth_random_jti(&jti);
    }
    if (status != WF_OK) goto done;
    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (!header || !payload ||
        !cJSON_AddStringToObject(header, "alg", "ES256") ||
        !cJSON_AddStringToObject(header, "kid", options->key_id) ||
        !cJSON_AddStringToObject(payload, "iss", options->client_id) ||
        !cJSON_AddStringToObject(payload, "sub", options->client_id) ||
        !cJSON_AddStringToObject(payload, "aud",
                                options->authorization_server_issuer) ||
        !cJSON_AddStringToObject(payload, "jti", jti) ||
        !cJSON_AddNumberToObject(payload, "iat", (double)issued_at) ||
        !cJSON_AddNumberToObject(payload, "exp", (double)(issued_at + 60))) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    header_json = cJSON_PrintUnformatted(header);
    payload_json = cJSON_PrintUnformatted(payload);
    if (!header_json || !payload_json) { status = WF_ERR_ALLOC; goto done; }
    status = wf_oauth_base64url((const unsigned char *)header_json,
                                strlen(header_json), &header_b64);
    if (status == WF_OK) status = wf_oauth_base64url(
        (const unsigned char *)payload_json, strlen(payload_json), &payload_b64);
    if (status != WF_OK) goto done;
    input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    signing_input = malloc(input_len + 1);
    if (!signing_input) { status = WF_ERR_ALLOC; goto done; }
    snprintf(signing_input, input_len + 1, "%s.%s", header_b64, payload_b64);
    status = wf_oauth_es256_sign(key, (const unsigned char *)signing_input,
                                 input_len, signature);
    if (status == WF_OK) status = wf_oauth_base64url(signature, sizeof(signature),
                                                      &signature_b64);
    if (status != WF_OK) goto done;
    jwt_len = input_len + 1 + strlen(signature_b64);
    jwt = malloc(jwt_len + 1);
    if (!jwt) { status = WF_ERR_ALLOC; goto done; }
    snprintf(jwt, jwt_len + 1, "%s.%s", signing_input, signature_b64);
    *jwt_out = jwt;
    jwt = NULL;
done:
    cJSON_Delete(header); cJSON_Delete(payload);
    free(jti); free(header_json); free(payload_json);
    free(header_b64); free(payload_b64); free(signature_b64);
    free(signing_input); free(jwt);
    return status;
}

void wf_oauth_string_free(char *value) {
    free(value);
}

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

static wf_status wf_oauth_positive_integer(const cJSON *root, const char *name,
                                            int required, int64_t *out, int *present) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    int64_t value;
    if (present) *present = item != NULL;
    if (!item) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0 ||
        item->valuedouble > 9007199254740991.0) return WF_ERR_PARSE;
    value = (int64_t)item->valuedouble;
    if ((double)value != item->valuedouble) return WF_ERR_PARSE;
    *out = value;
    return WF_OK;
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
    if (r->login_hint) ADD("login_hint", r->login_hint);
    if (s == WF_OK) s = wf_oauth_form_encode(p, n, &form);
    if (s == WF_OK) s = wf_oauth_post(t, endpoint, key, form, &res);
    if (s == WF_OK) s = wf_oauth_par_response_parse(res.body, res.body_len, out);
    wf_response_free(&res); free(assertion); free(form); return s;
}
#undef ADD

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

void wf_oauth_callback_result_free(wf_oauth_callback_result *result) {
    if (!result) return;
    free(result->state);
    free(result->code);
    free(result->issuer);
    free(result->error);
    free(result->error_description);
    memset(result, 0, sizeof(*result));
}

void wf_oauth_authorization_state_free(wf_oauth_authorization_state *state) {
    if (!state) return;
    free(state->issuer);
    free(state->code_verifier);
    free(state->app_state);
    free(state->client_auth_key_id);
    wf_oauth_dpop_key_free(state->dpop_key);
    memset(state, 0, sizeof(*state));
}

wf_status wf_oauth_authorization_state_create(
    const char *issuer, const wf_oauth_pkce *pkce,
    const wf_oauth_dpop_key *dpop_key, const char *app_state,
    const char *client_auth_key_id, int64_t now, int64_t ttl_seconds,
    wf_oauth_authorization_state *out) {
    unsigned char private_key[32];
    wf_status status;
    if (!issuer || !pkce || !dpop_key || !out || now <= 0 || ttl_seconds <= 0 ||
        now > INT64_MAX - ttl_seconds ||
        !wf_oauth_url_valid(issuer, 1, 1, 0)) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (wf_oauth_pkce_from_verifier(pkce->verifier, &(wf_oauth_pkce){0}) != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }
    out->issuer = wf_oauth_strdup(issuer);
    out->code_verifier = wf_oauth_strdup(pkce->verifier);
    if (app_state) out->app_state = wf_oauth_strdup(app_state);
    if (client_auth_key_id) out->client_auth_key_id = wf_oauth_strdup(client_auth_key_id);
    status = wf_oauth_dpop_key_export(dpop_key, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &out->dpop_key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (!out->issuer || !out->code_verifier || (app_state && !out->app_state) ||
        (client_auth_key_id && (!*client_auth_key_id || !out->client_auth_key_id)) ||
        status != WF_OK) {
        if (status == WF_OK) status = WF_ERR_ALLOC;
        wf_oauth_authorization_state_free(out);
        return status;
    }
    out->expires_at = now + ttl_seconds;
    return WF_OK;
}

static wf_status wf_oauth_private_jwk(const wf_oauth_dpop_key *key, cJSON **out) {
    cJSON *jwk = NULL;
    char *x = NULL, *y = NULL, *d = NULL;
    unsigned char private_key[32];
    wf_status status = wf_oauth_dpop_jwk(key, &jwk, &x, &y);
    if (status == WF_OK) status = wf_oauth_dpop_key_export(key, private_key);
    if (status == WF_OK) status = wf_oauth_base64url(private_key, sizeof(private_key), &d);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK && !cJSON_AddStringToObject(jwk, "d", d)) status = WF_ERR_ALLOC;
    free(x); free(y); free(d);
    if (status != WF_OK) { cJSON_Delete(jwk); return status; }
    *out = jwk;
    return WF_OK;
}

wf_status wf_oauth_authorization_state_serialize(
    const wf_oauth_authorization_state *state, char **json_out) {
    cJSON *root = NULL, *jwk = NULL, *auth = NULL;
    wf_status status;
    if (!state || !state->issuer || !state->code_verifier || !state->dpop_key ||
        state->expires_at <= 0 || !json_out) return WF_ERR_INVALID_ARG;
    *json_out = NULL;
    root = cJSON_CreateObject(); auth = cJSON_CreateObject();
    status = root && auth ? WF_OK : WF_ERR_ALLOC;
    if (status == WF_OK) status = wf_oauth_private_jwk(state->dpop_key, &jwk);
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(root, "iss", state->issuer) ||
         !cJSON_AddStringToObject(root, "verifier", state->code_verifier) ||
         !cJSON_AddNumberToObject(root, "expiresAt", (double)state->expires_at)))
        status = WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(auth, "method", state->client_auth_key_id ?
                                  "private_key_jwt" : "none") ||
         (state->client_auth_key_id &&
          !cJSON_AddStringToObject(auth, "kid", state->client_auth_key_id))))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "authMethod", auth)) status = WF_ERR_ALLOC;
        else auth = NULL;
    }
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "dpopJwk", jwk)) status = WF_ERR_ALLOC;
        else jwk = NULL;
    }
    if (status == WF_OK && state->app_state &&
        !cJSON_AddStringToObject(root, "appState", state->app_state)) status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        *json_out = cJSON_PrintUnformatted(root);
        if (!*json_out) status = WF_ERR_ALLOC;
    }
    cJSON_Delete(jwk);
    cJSON_Delete(auth);
    cJSON_Delete(root);
    return status;
}

static wf_status wf_oauth_base64url_32_decode(const char *encoded,
                                               unsigned char out[32]) {
    char padded[45];
    unsigned char decoded[33];
    int result, i;
    if (!encoded || strlen(encoded) != 43) return WF_ERR_PARSE;
    memcpy(padded, encoded, 43);
    padded[43] = '='; padded[44] = '\0';
    for (i = 0; i < 43; i++) {
        if (padded[i] == '-') padded[i] = '+';
        else if (padded[i] == '_') padded[i] = '/';
        else if (!isalnum((unsigned char)padded[i]) && padded[i] != '+' && padded[i] != '/')
            return WF_ERR_PARSE;
    }
    result = EVP_DecodeBlock(decoded, (const unsigned char *)padded, 44);
    if (result != 33) return WF_ERR_PARSE;
    memcpy(out, decoded, 32);
    OPENSSL_cleanse(decoded, sizeof(decoded));
    return WF_OK;
}

static wf_status wf_oauth_private_jwk_parse(const cJSON *jwk,
                                             wf_oauth_dpop_key **out) {
    const cJSON *item;
    const char *x, *y, *d;
    unsigned char private_key[32];
    wf_oauth_dpop_key *key = NULL;
    cJSON *expected = NULL;
    char *expected_x = NULL, *expected_y = NULL;
    wf_status status = WF_OK;
    *out = NULL;
    item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, "kty") : NULL;
    if (!cJSON_IsObject(jwk) || !cJSON_IsString(item) ||
        strcmp(item->valuestring, "EC") != 0) return WF_ERR_PARSE;
    item = cJSON_GetObjectItemCaseSensitive(jwk, "crv");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, "P-256") != 0)
        return WF_ERR_PARSE;
#define REQUIRED_JWK_STRING(name, target) do { \
    item = cJSON_GetObjectItemCaseSensitive(jwk, name); \
    target = cJSON_IsString(item) ? item->valuestring : NULL; \
    if (!target) return WF_ERR_PARSE; \
} while (0)
    REQUIRED_JWK_STRING("x", x);
    REQUIRED_JWK_STRING("y", y);
    REQUIRED_JWK_STRING("d", d);
#undef REQUIRED_JWK_STRING
    status = wf_oauth_base64url_32_decode(d, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK) status = wf_oauth_dpop_jwk(key, &expected,
                                                    &expected_x, &expected_y);
    if (status == WF_OK &&
        (strcmp(x, expected_x) != 0 || strcmp(y, expected_y) != 0)) {
        status = WF_ERR_PARSE;
    }
    free(expected_x); free(expected_y); cJSON_Delete(expected);
    if (status != WF_OK) { wf_oauth_dpop_key_free(key); return status; }
    *out = key;
    return WF_OK;
}

wf_status wf_oauth_authorization_state_parse(
    const char *json, size_t len, int64_t now, wf_oauth_authorization_state *out) {
    cJSON *root = NULL;
    const cJSON *jwk, *item;
    char *issuer = NULL, *verifier = NULL, *app_state = NULL, *auth_key_id = NULL;
    const char *x, *y, *d;
    unsigned char private_key[32];
    wf_oauth_dpop_key *key = NULL;
    cJSON *expected_jwk = NULL;
    char *expected_x = NULL, *expected_y = NULL;
    int64_t expires_at = 0;
    wf_status status = WF_OK;
    if (!json || !len || now <= 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { status = WF_ERR_PARSE; goto done; }
    status = wf_oauth_json_string(root, "iss", 1, &issuer);
    if (status == WF_OK) status = wf_oauth_json_string(root, "verifier", 1, &verifier);
    if (status == WF_OK) status = wf_oauth_json_string(root, "appState", 0, &app_state);
    if (status == WF_OK) status = wf_oauth_positive_integer(root, "expiresAt", 1,
                                                             &expires_at, NULL);
    {
        const cJSON *auth = cJSON_GetObjectItemCaseSensitive(root, "authMethod");
        const cJSON *method = auth ? cJSON_GetObjectItemCaseSensitive(auth, "method") : NULL;
        if (status == WF_OK && (!cJSON_IsObject(auth) || !cJSON_IsString(method) ||
            (strcmp(method->valuestring, "none") != 0 &&
             strcmp(method->valuestring, "private_key_jwt") != 0))) status = WF_ERR_PARSE;
        if (status == WF_OK && strcmp(method->valuestring, "private_key_jwt") == 0)
            status = wf_oauth_json_string(auth, "kid", 1, &auth_key_id);
        if (status == WF_OK && strcmp(method->valuestring, "none") == 0 &&
            cJSON_GetObjectItemCaseSensitive(auth, "kid")) status = WF_ERR_PARSE;
    }
    jwk = cJSON_GetObjectItemCaseSensitive(root, "dpopJwk");
    item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, "kty") : NULL;
    if (status == WF_OK && (!cJSON_IsObject(jwk) || !cJSON_IsString(item) ||
                            strcmp(item->valuestring, "EC") != 0)) status = WF_ERR_PARSE;
#define JWK_STRING(name, target) do { item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, name) : NULL; \
    target = cJSON_IsString(item) ? item->valuestring : NULL; \
    if (status == WF_OK && !target) status = WF_ERR_PARSE; } while (0)
    JWK_STRING("crv", x); if (status == WF_OK && strcmp(x, "P-256") != 0) status = WF_ERR_PARSE;
    JWK_STRING("x", x); JWK_STRING("y", y); JWK_STRING("d", d);
#undef JWK_STRING
    if (status == WF_OK && (!wf_oauth_url_valid(issuer, 1, 1, 0) || expires_at <= now))
        status = WF_ERR_PARSE;
    if (status == WF_OK && wf_oauth_pkce_from_verifier(verifier, &(wf_oauth_pkce){0}) != WF_OK)
        status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_base64url_32_decode(d, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK) status = wf_oauth_dpop_jwk(key, &expected_jwk, &expected_x, &expected_y);
    if (status == WF_OK && (strcmp(x, expected_x) != 0 || strcmp(y, expected_y) != 0))
        status = WF_ERR_PARSE;
    if (status == WF_OK) {
        out->issuer = issuer; issuer = NULL;
        out->code_verifier = verifier; verifier = NULL;
        out->app_state = app_state; app_state = NULL;
        out->client_auth_key_id = auth_key_id; auth_key_id = NULL;
        out->expires_at = expires_at;
        out->dpop_key = key; key = NULL;
    }
done:
    free(issuer); free(verifier); free(app_state); free(auth_key_id);
    free(expected_x); free(expected_y);
    cJSON_Delete(expected_jwk);
    wf_oauth_dpop_key_free(key);
    cJSON_Delete(root);
    if (status != WF_OK) wf_oauth_authorization_state_free(out);
    return status;
}

void wf_oauth_session_state_free(wf_oauth_session_state *session) {
    if (!session) return;
    free(session->issuer); free(session->subject); free(session->audience);
    free(session->scope); free(session->access_token); free(session->refresh_token);
    free(session->client_auth_key_id);
    wf_oauth_dpop_key_free(session->dpop_key);
    memset(session, 0, sizeof(*session));
}

wf_status wf_oauth_session_state_create(
    const char *issuer, const char *audience, const wf_oauth_dpop_key *dpop_key,
    const wf_oauth_token_response *token, const char *client_auth_key_id, int64_t now,
    wf_oauth_session_state *out) {
    unsigned char private_key[32];
    wf_status status;
    if (!issuer || !audience || !dpop_key || !token || !out || now <= 0 ||
        !token->sub || !token->scope || !token->access_token ||
        !token->token_type || strcmp(token->token_type, "DPoP") != 0 ||
        strncmp(token->sub, "did:", 4) != 0 ||
        !wf_oauth_url_valid(issuer, 1, 1, 0) ||
        !wf_oauth_url_valid(audience, 1, 1, 0) ||
        (token->expires_in_present &&
         (token->expires_in <= 0 || now > INT64_MAX - token->expires_in))) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->issuer = wf_oauth_strdup(issuer);
    out->subject = wf_oauth_strdup(token->sub);
    out->audience = wf_oauth_strdup(audience);
    out->scope = wf_oauth_strdup(token->scope);
    out->access_token = wf_oauth_strdup(token->access_token);
    if (token->refresh_token) out->refresh_token = wf_oauth_strdup(token->refresh_token);
    if (client_auth_key_id) out->client_auth_key_id = wf_oauth_strdup(client_auth_key_id);
    status = wf_oauth_dpop_key_export(dpop_key, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &out->dpop_key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (!out->issuer || !out->subject || !out->audience || !out->scope ||
        !out->access_token || (token->refresh_token && !out->refresh_token) ||
        (client_auth_key_id && (!*client_auth_key_id || !out->client_auth_key_id)) ||
        status != WF_OK) {
        if (status == WF_OK) status = WF_ERR_ALLOC;
        wf_oauth_session_state_free(out);
        return status;
    }
    if (token->expires_in_present) out->expires_at = now + token->expires_in;
    return WF_OK;
}

wf_status wf_oauth_session_state_serialize(const wf_oauth_session_state *session,
                                            char **json_out) {
    cJSON *root = NULL, *tokens = NULL, *jwk = NULL, *auth = NULL;
    wf_status status;
    if (!session || !json_out || !session->issuer || !session->subject ||
        !session->audience || !session->scope || !session->access_token ||
        !session->dpop_key) return WF_ERR_INVALID_ARG;
    *json_out = NULL;
    root = cJSON_CreateObject(); tokens = cJSON_CreateObject(); auth = cJSON_CreateObject();
    status = root && tokens && auth ? wf_oauth_private_jwk(session->dpop_key, &jwk) : WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(tokens, "iss", session->issuer) ||
         !cJSON_AddStringToObject(tokens, "sub", session->subject) ||
         !cJSON_AddStringToObject(tokens, "aud", session->audience) ||
         !cJSON_AddStringToObject(tokens, "scope", session->scope) ||
         !cJSON_AddStringToObject(tokens, "access_token", session->access_token) ||
         !cJSON_AddStringToObject(tokens, "token_type", "DPoP"))) status = WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(auth, "method", session->client_auth_key_id ?
                                  "private_key_jwt" : "none") ||
         (session->client_auth_key_id &&
          !cJSON_AddStringToObject(auth, "kid", session->client_auth_key_id))))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "authMethod", auth)) status = WF_ERR_ALLOC;
        else auth = NULL;
    }
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "dpopJwk", jwk)) status = WF_ERR_ALLOC;
        else jwk = NULL;
    }
    if (status == WF_OK && session->refresh_token &&
        !cJSON_AddStringToObject(tokens, "refresh_token", session->refresh_token))
        status = WF_ERR_ALLOC;
    if (status == WF_OK && session->expires_at > 0 &&
        !cJSON_AddNumberToObject(tokens, "expires_at", (double)session->expires_at))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "tokenSet", tokens)) status = WF_ERR_ALLOC;
        else tokens = NULL;
    }
    if (status == WF_OK) {
        *json_out = cJSON_PrintUnformatted(root);
        if (!*json_out) status = WF_ERR_ALLOC;
    }
    cJSON_Delete(jwk); cJSON_Delete(auth); cJSON_Delete(tokens); cJSON_Delete(root);
    return status;
}

wf_status wf_oauth_session_state_parse(const char *json, size_t len,
                                        const char *expected_subject,
                                        wf_oauth_session_state *out) {
    cJSON *root = NULL;
    const cJSON *tokens, *jwk, *auth, *method, *type;
    wf_status status;
    if (!json || !len || !expected_subject || !out ||
        strncmp(expected_subject, "did:", 4) != 0) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { status = WF_ERR_PARSE; goto done; }
    auth = cJSON_GetObjectItemCaseSensitive(root, "authMethod");
    method = auth ? cJSON_GetObjectItemCaseSensitive(auth, "method") : NULL;
    tokens = cJSON_GetObjectItemCaseSensitive(root, "tokenSet");
    jwk = cJSON_GetObjectItemCaseSensitive(root, "dpopJwk");
    if (!cJSON_IsObject(auth) || !cJSON_IsString(method) ||
        (strcmp(method->valuestring, "none") != 0 &&
         strcmp(method->valuestring, "private_key_jwt") != 0) ||
        !cJSON_IsObject(tokens)) { status = WF_ERR_PARSE; goto done; }
    status = wf_oauth_json_string(tokens, "iss", 1, &out->issuer);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "sub", 1, &out->subject);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "aud", 1, &out->audience);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "scope", 1, &out->scope);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "access_token", 1,
                                                        &out->access_token);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "refresh_token", 0,
                                                        &out->refresh_token);
    if (status == WF_OK && strcmp(method->valuestring, "private_key_jwt") == 0)
        status = wf_oauth_json_string(auth, "kid", 1, &out->client_auth_key_id);
    if (status == WF_OK && strcmp(method->valuestring, "none") == 0 &&
        cJSON_GetObjectItemCaseSensitive(auth, "kid")) status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_positive_integer(tokens, "expires_at", 0,
                                                             &out->expires_at, NULL);
    type = cJSON_GetObjectItemCaseSensitive(tokens, "token_type");
    if (status == WF_OK && (!cJSON_IsString(type) || strcmp(type->valuestring, "DPoP") != 0))
        status = WF_ERR_PARSE;
    if (status == WF_OK && (!wf_oauth_url_valid(out->issuer, 1, 1, 0) ||
                            !wf_oauth_url_valid(out->audience, 1, 1, 0) ||
                            strcmp(out->subject, expected_subject) != 0)) status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_private_jwk_parse(jwk, &out->dpop_key);
done:
    cJSON_Delete(root);
    if (status != WF_OK) wf_oauth_session_state_free(out);
    return status;
}

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

wf_status wf_oauth_callback_validate(const wf_oauth_callback_params *params,
                                     const char *expected_state,
                                     const char *expected_issuer,
                                     int issuer_parameter_required,
                                     wf_oauth_callback_result *out) {
    if (!params || !expected_state || !expected_issuer || !out ||
        expected_state[0] == '\0' || expected_issuer[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (params->response || !params->state || params->state[0] == '\0' ||
        strcmp(params->state, expected_state) != 0) {
        return WF_ERR_PARSE;
    }
    if ((params->code && params->error) || (!params->code && !params->error) ||
        (params->code && params->code[0] == '\0') ||
        (params->error && params->error[0] == '\0')) {
        return WF_ERR_PARSE;
    }
    if (params->issuer) {
        if (strcmp(params->issuer, expected_issuer) != 0) return WF_ERR_PARSE;
    } else if (issuer_parameter_required) {
        return WF_ERR_PARSE;
    }
    out->state = wf_oauth_strdup(params->state);
    if (params->code) out->code = wf_oauth_strdup(params->code);
    if (params->issuer) out->issuer = wf_oauth_strdup(params->issuer);
    if (params->error) out->error = wf_oauth_strdup(params->error);
    if (params->error_description) {
        if (!params->error) {
            wf_oauth_callback_result_free(out);
            return WF_ERR_PARSE;
        }
        out->error_description = wf_oauth_strdup(params->error_description);
    }
    if (!out->state || (params->code && !out->code) ||
        (params->issuer && !out->issuer) || (params->error && !out->error) ||
        (params->error_description && !out->error_description)) {
        wf_oauth_callback_result_free(out);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}
