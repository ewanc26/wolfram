/**
 * xrpc_wii.c — Wii stub implementation of the XRPC transport.
 *
 * All functions return WF_ERR_NOT_IMPLEMENTED. Replace with a real
 * lwIP + mbedTLS transport when building the Wii integration.
 */

#include "wolfram/xrpc.h"
#include "wolfram/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wf_xrpc_client {
    char *base_url;
    char *auth_header;
    wf_xrpc_handler_fn handler;
    void *handler_userdata;
};

/* Strip a trailing slash, if any, so we can join paths predictably. */
static char *wf_normalise_base(const char *base_url) {
    size_t len = strlen(base_url);
    while (len > 0 && base_url[len - 1] == '/') {
        len--;
    }
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, base_url, len);
    out[len] = '\0';
    return out;
}

wf_xrpc_client *wf_xrpc_client_new(const char *service_base_url) {
    if (!service_base_url || service_base_url[0] == '\0') return NULL;
    wf_xrpc_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->base_url = wf_normalise_base(service_base_url);
    if (!client->base_url) { free(client); return NULL; }
    return client;
}

void wf_xrpc_client_free(wf_xrpc_client *client) {
    if (!client) return;
    free(client->base_url);
    free(client->auth_header);
    free(client);
}

void wf_xrpc_set_handler(wf_xrpc_client *client, wf_xrpc_handler_fn fn,
                         void *userdata) {
    if (!client) return;
    client->handler = fn;
    client->handler_userdata = userdata;
}

void wf_xrpc_client_set_auth(wf_xrpc_client *client, const char *access_jwt) {
    if (!client) return;
    free(client->auth_header);
    client->auth_header = NULL;
    if (!access_jwt) return;
    size_t needed = strlen("Authorization: Bearer ") + strlen(access_jwt) + 1;
    client->auth_header = malloc(needed);
    if (client->auth_header) {
        snprintf(client->auth_header, needed, "Authorization: Bearer %s",
                 access_jwt);
    }
}

char *wf_xrpc_get_base_url(wf_xrpc_client *client) {
    if (!client || !client->base_url) return NULL;
    return strdup(client->base_url);
}

void wf_response_free(wf_response *res) {
    if (!res) return;
    free(res->body);
    free(res->dpop_nonce);
    res->body = NULL;
    res->body_len = 0;
    res->dpop_nonce = NULL;
    res->status = 0;
}

/* ── Stubs ──────────────────────────────────────────────────────────── */

/*
 * TODO: Implement using lwIP sockets + mbedTLS:
 *   1. Parse URL → host, port, path
 *   2. gethostbyname() to resolve host
 *   3. socket() + connect()
 *   4. mbedTLS SSL handshake
 *   5. Send HTTP request, read response
 *   6. Parse headers (DPoP-Nonce, Content-Length, etc.)
 *   7. Read body into growable buffer
 */

wf_status wf_xrpc_query(wf_xrpc_client *client, const char *nsid,
                         const char *query_string, wf_response *out) {
    (void)client; (void)nsid; (void)query_string; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_xrpc_query_params(wf_xrpc_client *client, const char *nsid,
                                const wf_xrpc_param *params,
                                size_t param_count, wf_response *out) {
    (void)client; (void)nsid; (void)params; (void)param_count; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                             const char *json_body, wf_response *out) {
    (void)client; (void)nsid; (void)json_body; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_xrpc_upload_blob(wf_xrpc_client *client, const char *nsid,
                               const void *data, size_t data_len,
                               const char *content_type, wf_response *out) {
    (void)client; (void)nsid; (void)data; (void)data_len;
    (void)content_type; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data,
    size_t data_len, const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out) {
    (void)client; (void)nsid; (void)data; (void)data_len;
    (void)content_type; (void)headers; (void)header_count; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_http_get(wf_xrpc_client *client, const char *url,
                      wf_response *out) {
    (void)client; (void)url; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_http_get_with_headers(wf_xrpc_client *client, const char *url,
                                   const wf_http_header *extra,
                                   size_t extra_count, wf_response *out) {
    (void)client; (void)url; (void)extra; (void)extra_count; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                        const char *content_type, const char *body,
                        const wf_http_header *extra, size_t extra_count,
                        wf_response *out) {
    (void)client; (void)url; (void)content_type; (void)body;
    (void)extra; (void)extra_count; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}
