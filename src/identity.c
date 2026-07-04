/**
 * identity.c — DID and handle resolution.
 *
 * Implements:
 *   - did:plc resolution via https://plc.directory/<did>
 *   - did:web resolution via https://<host>/.well-known/did.json
 *   - handle resolution via DNS TXT _atproto.<handle> then
 *     https://<handle>/.well-known/atproto-did fallback
 *
 * Depends on cJSON for parsing DID documents (JSON).
 */

#include "wolfram/identity.h"
#include "wolfram/xrpc.h"
#include "wolfram/version.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

struct wf_buffer {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t wf_identity_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct wf_buffer *buf = (struct wf_buffer *)userdata;
    size_t chunk = size * nmemb;

    if (buf->len + chunk + 1 > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap * 2;
        while (new_cap < buf->len + chunk + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(buf->data, new_cap);
        if (!grown) {
            return 0;
        }
        buf->data = grown;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, chunk);
    buf->len += chunk;
    buf->data[buf->len] = '\0';
    return chunk;
}

static char *wf_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static void wf_did_doc_init(wf_did_document *doc) {
    doc->did = NULL;
    doc->pds_endpoint = NULL;
    doc->signing_key = NULL;
    doc->method = WF_DID_METHOD_UNKNOWN;
}

static wf_status wf_did_doc_parse_json(wf_did_document *doc, cJSON *root) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(id) || !id->valuestring) {
        return WF_ERR_PARSE;
    }
    doc->did = wf_strdup(id->valuestring);
    if (!doc->did) return WF_ERR_ALLOC;

    cJSON *service = cJSON_GetObjectItemCaseSensitive(root, "service");
    if (cJSON_IsArray(service)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, service) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
            cJSON *endpoint = cJSON_GetObjectItemCaseSensitive(item, "serviceEndpoint");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "AtprotoPersonalDataServer") == 0 &&
                cJSON_IsString(endpoint) && endpoint->valuestring) {
                doc->pds_endpoint = wf_strdup(endpoint->valuestring);
                break;
            }
        }
    }

    cJSON *verification = cJSON_GetObjectItemCaseSensitive(root, "verificationMethod");
    if (cJSON_IsArray(verification)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, verification) {
            cJSON *controller = cJSON_GetObjectItemCaseSensitive(item, "controller");
            cJSON *public_key = cJSON_GetObjectItemCaseSensitive(item, "publicKeyMultibase");
            if (cJSON_IsString(controller) && controller->valuestring && doc->did &&
                strcmp(controller->valuestring, doc->did) == 0 &&
                cJSON_IsString(public_key) && public_key->valuestring) {
                doc->signing_key = wf_strdup(public_key->valuestring);
                break;
            }
        }
    }

    return WF_OK;
}

static wf_status wf_did_resolve_plc(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    size_t url_len = strlen("https://plc.directory/") + strlen(did) + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://plc.directory/%s", did);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    status = wf_did_doc_parse_json(out, root);
    cJSON_Delete(root);
    return status;
}

static wf_status wf_did_resolve_web(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    const char *host = did + strlen("did:web:");
    if (!host || host[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    size_t url_len = strlen("https://") + strlen(host) + strlen("/.well-known/did.json") + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://%s/.well-known/did.json", host);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    status = wf_did_doc_parse_json(out, root);
    cJSON_Delete(root);
    return status;
}

wf_did_method wf_did_method_of(const char *did) {
    if (!did) return WF_DID_METHOD_UNKNOWN;

    if (strncmp(did, "did:plc:", 8) == 0) {
        return WF_DID_METHOD_PLC;
    }
    if (strncmp(did, "did:web:", 8) == 0) {
        return WF_DID_METHOD_WEB;
    }
    return WF_DID_METHOD_UNKNOWN;
}

wf_status wf_did_resolve(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    if (!client || !did || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_did_doc_init(out);
    out->method = wf_did_method_of(did);

    switch (out->method) {
        case WF_DID_METHOD_PLC:
            return wf_did_resolve_plc(client, did, out);
        case WF_DID_METHOD_WEB:
            return wf_did_resolve_web(client, did, out);
        default:
            return WF_ERR_INVALID_ARG;
    }
}

void wf_did_document_free(wf_did_document *doc) {
    if (!doc) return;
    free(doc->did);
    free(doc->pds_endpoint);
    free(doc->signing_key);
    doc->did = NULL;
    doc->pds_endpoint = NULL;
    doc->signing_key = NULL;
    doc->method = WF_DID_METHOD_UNKNOWN;
}

static wf_status wf_handle_resolve_dns_txt(wf_xrpc_client *client, const char *handle, char **out_did) {
    (void)client;
    (void)handle;
    (void)out_did;
    /* TODO: Implement DNS TXT lookup for _atproto.<handle>
     * Requires a DNS library or DoH endpoint. For now, skip to
     * well-known fallback which is also spec-compliant. */
    return WF_ERR_NETWORK;
}

static wf_status wf_handle_resolve_well_known(wf_xrpc_client *client, const char *handle, char **out_did) {
    size_t url_len = strlen("https://") + strlen(handle) + strlen("/.well-known/atproto-did") + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://%s/.well-known/atproto-did", handle);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    if (res.body_len == 0 || res.body[res.body_len - 1] != '\n') {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    char *did = wf_strdup(res.body);
    wf_response_free(&res);
    if (!did) return WF_ERR_ALLOC;

    char *newline = strchr(did, '\n');
    if (newline) *newline = '\0';

    if (strncmp(did, "did:", 4) != 0) {
        free(did);
        return WF_ERR_PARSE;
    }

    *out_did = did;
    return WF_OK;
}

wf_status wf_handle_resolve(wf_xrpc_client *client, const char *handle, char **out_did) {
    if (!client || !handle || !out_did || handle[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    *out_did = NULL;

    wf_status status = wf_handle_resolve_dns_txt(client, handle, out_did);
    if (status == WF_OK) {
        return WF_OK;
    }

    return wf_handle_resolve_well_known(client, handle, out_did);
}