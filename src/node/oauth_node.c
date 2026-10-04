#include "wolfram/oauth_node.h"
#include "wolfram/oauth.h"
#include "wolfram/auth_client.h"

#include <openssl/rand.h>
#include <cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NODE_MAX_PAIRS 64
#define NODE_CODE_MAX 64
#define NODE_TOKEN_MAX 96
#define NODE_ERROR_MAX 192
#define NODE_SESSION_TTL (30 * 24 * 60 * 60)

typedef struct node_pair {
    int used;
    int callback_consuming;
    int complete;
    int64_t expires_at;

    char pair_code[NODE_CODE_MAX];
    char session_token[NODE_TOKEN_MAX];

    char *handle;
    char *did;
    char *pds_url;
    char *authorization_url;
    char *oauth_state;
    char *oauth_state_json;

    wf_oauth_server_metadata server;
    wf_oauth_session_state session;
    char error[NODE_ERROR_MAX];
} node_pair;

struct wf_oauth_node {
    wf_xrpc_server *server;
    pthread_mutex_t lock;

    char *public_base_url;
    char *client_name;
    char *scope;
    char *slingshot_url;
    unsigned int pairing_ttl;

    char *client_id;
    char *redirect_uri;
    char *metadata_json;
    wf_oauth_client_metadata client;

    node_pair pairs[NODE_MAX_PAIRS];
};

static char *dupstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int64_t now_seconds(void) {
    return (int64_t)time(NULL);
}

static int random_bytes(unsigned char *buf, size_t len) {
    return buf && len && RAND_bytes(buf, (int)len) == 1;
}

static int random_code(char *out, size_t cap) {
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    unsigned char raw[12];
    if (!out || cap < 17 || !random_bytes(raw, sizeof raw)) return 0;
    for (size_t i = 0; i < sizeof raw; ++i) out[i] = alphabet[raw[i] & 31U];
    out[sizeof raw] = '\0';
    return 1;
}

static int random_token(char *out, size_t cap) {
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[32];
    if (!out || cap < sizeof raw * 2 + 1 || !random_bytes(raw, sizeof raw))
        return 0;
    for (size_t i = 0; i < sizeof raw; ++i) {
        out[i * 2] = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 15U];
    }
    out[sizeof raw * 2] = '\0';
    return 1;
}

static void pair_free(node_pair *p) {
    if (!p) return;
    free(p->handle);
    free(p->did);
    free(p->pds_url);
    free(p->authorization_url);
    free(p->oauth_state);
    free(p->oauth_state_json);
    wf_oauth_server_metadata_free(&p->server);
    wf_oauth_session_state_free(&p->session);
    memset(p, 0, sizeof *p);
}

static node_pair *pair_find(wf_oauth_node *node, const char *code) {
    for (size_t i = 0; i < NODE_MAX_PAIRS; ++i) {
        if (node->pairs[i].used && strcmp(node->pairs[i].pair_code, code) == 0)
            return &node->pairs[i];
    }
    return NULL;
}

static node_pair *pair_alloc(wf_oauth_node *node) {
    int64_t now = now_seconds();
    for (size_t i = 0; i < NODE_MAX_PAIRS; ++i) {
        node_pair *p = &node->pairs[i];
        if (p->used && !p->complete && p->expires_at < now &&
            !p->callback_consuming)
            pair_free(p);
        if (!p->used) {
            memset(p, 0, sizeof *p);
            p->used = 1;
            p->expires_at = now + (int64_t)node->pairing_ttl;
            return p;
        }
    }
    return NULL;
}

static const char *param_string(const wf_xrpc_request *req, const char *name) {
    cJSON *v = req && req->params
                   ? cJSON_GetObjectItemCaseSensitive(req->params, name)
                   : NULL;
    return v && cJSON_IsString(v) ? v->valuestring : NULL;
}

static void set_json(wf_xrpc_response *resp, cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                   "Could not encode the response.");
        return;
    }
    wf_xrpc_response_set_content_type(resp, "application/json");
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
}

static void html(wf_xrpc_response *resp, int status, const char *body) {
    resp->http_status = status;
    wf_xrpc_response_set_content_type(resp, "text/html; charset=utf-8");
    wf_xrpc_response_set_body(resp, body, strlen(body));
}

static void redirect(wf_xrpc_response *resp, const char *location) {
    static const char body[] =
        "<!doctype html><meta charset=utf-8><title>Continue</title>"
        "<p>Continue to the account sign-in page.</p>";
    resp->http_status = 302;
    wf_xrpc_response_add_header(resp, "Location", location);
    html(resp, 302, body);
}

static char *escape_html(const char *s) {
    size_t cap = 1;
    for (const char *p = s ? s : ""; *p; ++p)
        cap += (*p == '&')                ? 5
               : (*p == '<' || *p == '>') ? 4
               : (*p == '"')              ? 6
               : (*p == '\'')             ? 5
                                          : 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    char *q = out;
    for (const char *p = s ? s : ""; *p; ++p) {
        switch (*p) {
            case '&':
                memcpy(q, "&amp;", 5);
                q += 5;
                break;
            case '<':
                memcpy(q, "&lt;", 4);
                q += 4;
                break;
            case '>':
                memcpy(q, "&gt;", 4);
                q += 4;
                break;
            case '"':
                memcpy(q, "&quot;", 6);
                q += 6;
                break;
            case '\'':
                memcpy(q, "&#39;", 5);
                q += 5;
                break;
            default:
                *q++ = *p;
                break;
        }
    }
    *q = '\0';
    return out;
}

static char *path_code(const char *path) {
    const char *prefix = "/pair/";
    if (!path || strncmp(path, prefix, strlen(prefix)) != 0) return NULL;
    path += strlen(prefix);
    size_t n = strcspn(path, "/");
    if (!n || n >= NODE_CODE_MAX) return NULL;
    char *code = malloc(n + 1);
    if (!code) return NULL;
    memcpy(code, path, n);
    code[n] = '\0';
    return code;
}

static wf_status slingshot_query(wf_oauth_node *node, const char *nsid,
                                 const wf_xrpc_param *params, size_t count,
                                 wf_response *out) {
    wf_xrpc_client *client = wf_xrpc_client_new(node->slingshot_url);
    if (!client) return WF_ERR_ALLOC;
    wf_status st = wf_xrpc_query_params(client, nsid, params, count, out);
    wf_xrpc_client_free(client);
    return st;
}

static wf_status resolve_handle(wf_oauth_node *node, const char *handle,
                                char **did_out) {
    wf_response res = {0};
    wf_xrpc_param p = {"handle", handle};
    wf_status st = slingshot_query(node, "com.atproto.identity.resolveHandle",
                                   &p, 1, &res);
    if (st != WF_OK) {
        wf_response_free(&res);
        return st;
    }
    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) return WF_ERR_PARSE;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    if (!did || !cJSON_IsString(did) || !did->valuestring[0]) {
        cJSON_Delete(root);
        return WF_ERR_HANDLE_RESOLVE;
    }
    *did_out = dupstr(did->valuestring);
    cJSON_Delete(root);
    return *did_out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status resolve_pds(wf_oauth_node *node, const char *did,
                             char **pds_out) {
    wf_response res = {0};
    wf_xrpc_param p = {"identifier", did};
    wf_status st = slingshot_query(
        node, "blue.microcosm.identity.resolveMiniDoc", &p, 1, &res);
    if (st != WF_OK) {
        wf_response_free(&res);
        return st;
    }
    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) return WF_ERR_PARSE;
    cJSON *rdid = cJSON_GetObjectItemCaseSensitive(root, "did");
    cJSON *pds = cJSON_GetObjectItemCaseSensitive(root, "pds");
    if (!rdid || !cJSON_IsString(rdid) || strcmp(rdid->valuestring, did) != 0 ||
        !pds || !cJSON_IsString(pds) || !pds->valuestring[0]) {
        cJSON_Delete(root);
        return WF_ERR_DID_RESOLVE;
    }
    *pds_out = dupstr(pds->valuestring);
    cJSON_Delete(root);
    return *pds_out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status start_pair(wf_oauth_node *node, node_pair *pair,
                            const char *handle) {
    wf_status st;
    wf_xrpc_client *transport = NULL;
    wf_oauth_resource_metadata resource = {0};
    char *did = NULL;
    char *pds = NULL;

    st = resolve_handle(node, handle, &did);
    if (st == WF_OK) st = resolve_pds(node, did, &pds);
    if (st != WF_OK) goto done;

    transport = wf_xrpc_client_new(pds);
    if (!transport) {
        st = WF_ERR_ALLOC;
        goto done;
    }

    st = wf_oauth_discover(transport, pds, &resource, &pair->server);
    if (st == WF_OK) {
        wf_oauth_client_auth auth = {.client_id = node->client_id,
                                     .authorization_server_issuer =
                                         pair->server.issuer,
                                     .signing_key = NULL,
                                     .key_id = NULL};
        wf_oauth_authorization_begin_options options = {
            .redirect_uri = node->redirect_uri,
            .scope = node->scope,
            .login_hint = did,
            .app_state = pair->pair_code,
            .now = now_seconds(),
            .state_ttl = (int64_t)node->pairing_ttl};
        wf_oauth_authorization_begin_result begun = {0};
        st = wf_oauth_authorization_begin(
            transport, &pair->server, &node->client, &auth, &options, &begun);
        if (st == WF_OK) {
            pair->handle = dupstr(handle);
            pair->did = did;
            did = NULL;
            pair->pds_url = pds;
            pds = NULL;
            pair->authorization_url = begun.authorization_url;
            begun.authorization_url = NULL;
            pair->oauth_state = begun.state;
            begun.state = NULL;
            pair->oauth_state_json = begun.state_json;
            begun.state_json = NULL;
            pair->expires_at = now_seconds() + (int64_t)node->pairing_ttl;
        }
        wf_oauth_authorization_begin_result_free(&begun);
    }

done:
    wf_oauth_resource_metadata_free(&resource);
    wf_xrpc_client_free(transport);
    free(did);
    free(pds);
    if (st != WF_OK) wf_oauth_server_metadata_free(&pair->server);
    return st;
}

static wf_status begin_handler(void *ctx, const wf_xrpc_request *req,
                               wf_xrpc_response *resp) {
    wf_oauth_node *node = ctx;
    const char *handle = param_string(req, "handle");
    if (!handle || !handle[0]) {
        wf_xrpc_response_set_error(resp, 400, "InvalidRequest",
                                   "handle is required");
        return WF_OK;
    }

    pthread_mutex_lock(&node->lock);
    node_pair *pair = pair_alloc(node);
    pthread_mutex_unlock(&node->lock);
    if (!pair) {
        wf_xrpc_response_set_error(resp, 503, "Unavailable",
                                   "No pairing slots are available.");
        return WF_OK;
    }

    if (!random_code(pair->pair_code, sizeof pair->pair_code) ||
        !random_token(pair->session_token, sizeof pair->session_token) ||
        start_pair(node, pair, handle) != WF_OK) {
        pthread_mutex_lock(&node->lock);
        pair_free(pair);
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(
            resp, 502, "OAuthStartFailed",
            "The handle could not be resolved or OAuth could not be started.");
        return WF_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                   "Out of memory.");
        return WF_OK;
    }
    char url[512];
    snprintf(url, sizeof url, "%s/pair/%s", node->public_base_url,
             pair->pair_code);
    cJSON_AddStringToObject(root, "pair_code", pair->pair_code);
    cJSON_AddStringToObject(root, "pair_url", url);
    cJSON_AddNumberToObject(root, "expires_at", (double)pair->expires_at);
    set_json(resp, root);
    return WF_OK;
}

static wf_status poll_handler(void *ctx, const wf_xrpc_request *req,
                              wf_xrpc_response *resp) {
    wf_oauth_node *node = ctx;
    const char *code = param_string(req, "code");
    if (!code || !code[0]) {
        wf_xrpc_response_set_error(resp, 400, "InvalidRequest",
                                   "code is required");
        return WF_OK;
    }

    pthread_mutex_lock(&node->lock);
    node_pair *pair = pair_find(node, code);
    if (!pair) {
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(resp, 404, "NotFound",
                                   "Unknown pairing code.");
        return WF_OK;
    }
    if (!pair->complete && pair->expires_at < now_seconds()) {
        pair->error[0] = 'e';
        snprintf(pair->error, sizeof pair->error,
                 "This pairing request expired.");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                   "Out of memory.");
        return WF_OK;
    }
    if (pair->error[0]) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", pair->error);
    } else if (!pair->complete) {
        cJSON_AddStringToObject(root, "status", "pending");
    } else {
        cJSON_AddStringToObject(root, "status", "complete");
        cJSON_AddStringToObject(root, "token", pair->session_token);
        cJSON_AddStringToObject(root, "handle", pair->handle);
        cJSON_AddStringToObject(root, "did", pair->did);
        cJSON_AddStringToObject(root, "service", node->public_base_url);
    }
    pthread_mutex_unlock(&node->lock);
    set_json(resp, root);
    return WF_OK;
}

static wf_status pair_page_handler(void *ctx, const wf_xrpc_request *req,
                                   wf_xrpc_response *resp) {
    wf_oauth_node *node = ctx;
    char *code = path_code(req->path);
    if (!code) {
        html(resp, 404, "<h1>Invalid pairing link</h1>");
        return WF_OK;
    }

    pthread_mutex_lock(&node->lock);
    node_pair *pair = pair_find(node, code);
    if (!pair) {
        pthread_mutex_unlock(&node->lock);
        free(code);
        html(resp, 404,
             "<h1>Pairing link not found</h1><p>Request a new link from the "
             "console.</p>");
        return WF_OK;
    }
    char *handle = dupstr(pair->handle);
    char *auth_url = dupstr(pair->authorization_url);
    int complete = pair->complete;
    int failed = pair->error[0] != '\0';
    pthread_mutex_unlock(&node->lock);
    free(code);

    if (complete) {
        free(handle);
        free(auth_url);
        html(resp, 200,
             "<!doctype html><meta charset=utf-8><meta name=viewport "
             "content=width=device-width,initial-scale=1>"
             "<body "
             "style='font-family:system-ui,sans-serif;max-width:42rem;margin:"
             "4rem auto;padding:0 1.25rem'>"
             "<h1>Account connected</h1><p>You can return to the console "
             "now.</p></body>");
        return WF_OK;
    }
    if (failed || !handle || !auth_url) {
        free(handle);
        free(auth_url);
        html(resp, 410,
             "<!doctype html><meta charset=utf-8><body "
             "style='font-family:system-ui,sans-serif;max-width:42rem;margin:"
             "4rem auto;padding:0 1.25rem'>"
             "<h1>Pairing unavailable</h1><p>Request a new sign-in link from "
             "the console.</p></body>");
        return WF_OK;
    }

    char *safe_handle = escape_html(handle);
    char *safe_url = escape_html(auth_url);
    free(handle);
    free(auth_url);
    if (!safe_handle || !safe_url) {
        free(safe_handle);
        free(safe_url);
        html(resp, 500, "<h1>Out of memory</h1>");
        return WF_OK;
    }

    size_t cap = strlen(safe_handle) + strlen(safe_url) + 2048;
    char *body = malloc(cap);
    if (!body) {
        free(safe_handle);
        free(safe_url);
        html(resp, 500, "<h1>Out of memory</h1>");
        return WF_OK;
    }
    snprintf(body, cap,
             "<!doctype html><meta charset=utf-8><meta name=viewport "
             "content=width=device-width,initial-scale=1>"
             "<title>Connect account</title><body "
             "style='font-family:system-ui,sans-serif;max-width:42rem;margin:"
             "4rem auto;padding:0 1.25rem'>"
             "<h1>Connect your account</h1><p>This request was opened for "
             "<strong>%s</strong>.</p>"
             "<p>The next page is your PDS's own sign-in and consent screen. "
             "This OAuth node never asks for or receives your PDS password.</p>"
             "<p><a href='%s' style='display:inline-block;padding:.8rem "
             "1rem;border-radius:.7rem;background:#111;color:#fff;text-"
             "decoration:none'>"
             "Continue to PDS sign-in</a></p><p style='color:#666'>After "
             "authorising the client, you will be returned here.</p></body>",
             safe_handle, safe_url);
    free(safe_handle);
    free(safe_url);
    html(resp, 200, body);
    free(body);
    return WF_OK;
}

static wf_status metadata_handler(void *ctx, const wf_xrpc_request *req,
                                  wf_xrpc_response *resp) {
    (void)req;
    wf_oauth_node *node = ctx;
    wf_xrpc_response_set_content_type(resp, "application/json");
    wf_xrpc_response_set_body(resp, node->metadata_json,
                              strlen(node->metadata_json));
    return WF_OK;
}

static wf_status callback_handler(void *ctx, const wf_xrpc_request *req,
                                  wf_xrpc_response *resp) {
    wf_oauth_node *node = ctx;
    const char *state = param_string(req, "state");
    if (!state || !state[0]) {
        html(resp, 400, "<h1>Missing OAuth state</h1>");
        return WF_OK;
    }

    pthread_mutex_lock(&node->lock);
    node_pair *pair = NULL;
    for (size_t i = 0; i < NODE_MAX_PAIRS; ++i) {
        if (node->pairs[i].used && !node->pairs[i].callback_consuming &&
            node->pairs[i].oauth_state &&
            strcmp(node->pairs[i].oauth_state, state) == 0) {
            pair = &node->pairs[i];
            pair->callback_consuming = 1;
            break;
        }
    }
    if (!pair) {
        pthread_mutex_unlock(&node->lock);
        html(resp, 400, "<h1>Invalid or replayed OAuth state</h1>");
        return WF_OK;
    }

    char *pds = dupstr(pair->pds_url);
    char *state_json = dupstr(pair->oauth_state_json);
    char *expected_did = dupstr(pair->did);
    char *expected_state = dupstr(pair->oauth_state);
    pthread_mutex_unlock(&node->lock);

    if (!pds || !state_json || !expected_did || !expected_state) {
        free(pds);
        free(state_json);
        free(expected_did);
        free(expected_state);
        pthread_mutex_lock(&node->lock);
        pair->callback_consuming = 0;
        pthread_mutex_unlock(&node->lock);
        html(resp, 500, "<h1>Could not load pairing state</h1>");
        return WF_OK;
    }

    wf_xrpc_client *transport = wf_xrpc_client_new(pds);
    free(pds);
    if (!transport) {
        free(state_json);
        free(expected_did);
        free(expected_state);
        pthread_mutex_lock(&node->lock);
        pair->callback_consuming = 0;
        pthread_mutex_unlock(&node->lock);
        html(resp, 502, "<h1>Could not contact the PDS</h1>");
        return WF_OK;
    }

    wf_oauth_callback_params params = {
        .response = NULL,
        .state = state,
        .code = param_string(req, "code"),
        .issuer = param_string(req, "iss"),
        .error = param_string(req, "error"),
        .error_description = param_string(req, "error_description")};
    wf_oauth_client_auth auth = {.client_id = node->client_id,
                                 .authorization_server_issuer =
                                     pair->server.issuer,
                                 .signing_key = NULL,
                                 .key_id = NULL};
    wf_oauth_authorization_complete_result result = {0};
    wf_status st = wf_oauth_authorization_complete(
        transport, &pair->server, &node->client, &auth, &params, expected_state,
        state_json, strlen(state_json), node->redirect_uri, now_seconds(),
        &result);
    wf_xrpc_client_free(transport);

    pthread_mutex_lock(&node->lock);
    pair->callback_consuming = 0;
    free(pair->oauth_state);
    pair->oauth_state = NULL;
    free(pair->oauth_state_json);
    pair->oauth_state_json = NULL;
    free(expected_state);

    bool accepted = false;
    if (st == WF_OK && result.session.subject &&
        strcmp(result.session.subject, expected_did) == 0) {
        wf_oauth_session_state_free(&pair->session);
        pair->session = result.session;
        memset(&result.session, 0, sizeof result.session);
        pair->complete = 1;
        pair->expires_at = now_seconds() + NODE_SESSION_TTL;
        pair->error[0] = '\0';
        accepted = true;
    } else if (st == WF_ERR_HTTP && result.error) {
        snprintf(pair->error, sizeof pair->error, "%s",
                 result.error_description ? result.error_description
                                          : result.error);
    } else {
        snprintf(
            pair->error, sizeof pair->error, "%s",
            st == WF_OK
                ? "The authorised account did not match the verified handle."
                : "The PDS rejected or could not complete the OAuth exchange.");
    }
    pthread_mutex_unlock(&node->lock);

    free(expected_did);
    free(state_json);
    wf_oauth_authorization_complete_result_free(&result);

    if (accepted) {
        html(resp, 200,
             "<!doctype html><meta charset=utf-8><meta name=viewport "
             "content=width=device-width,initial-scale=1>"
             "<title>Account connected</title><body "
             "style='font-family:system-ui,sans-serif;max-width:42rem;margin:"
             "4rem auto;padding:0 1.25rem'>"
             "<h1>Account connected</h1><p>You can return to the console "
             "now.</p></body>");
    } else {
        html(resp, 400,
             "<!doctype html><meta charset=utf-8><meta name=viewport "
             "content=width=device-width,initial-scale=1>"
             "<title>Sign-in failed</title><body "
             "style='font-family:system-ui,sans-serif;max-width:42rem;margin:"
             "4rem auto;padding:0 1.25rem'>"
             "<h1>Sign-in failed</h1><p>The console will show the error. You "
             "can close this page.</p></body>");
    }
    return WF_OK;
}

static int bearer_is(const char *header, const char *token) {
    static const char prefix[] = "Bearer ";
    return header && token && strncmp(header, prefix, sizeof prefix - 1) == 0 &&
           strcmp(header + sizeof prefix - 1, token) == 0;
}

static wf_status proxy_handler(void *ctx, const wf_xrpc_request *req,
                               wf_xrpc_response *resp) {
    wf_oauth_node *node = ctx;
    const char *auth = req->auth_header;
    static const char prefix[] = "Bearer ";
    if (!auth || strncmp(auth, prefix, sizeof prefix - 1) != 0) {
        wf_xrpc_response_set_error(resp, 401, "AuthRequired",
                                   "A node bearer token is required.");
        return WF_OK;
    }

    const char *token = auth + sizeof prefix - 1;
    pthread_mutex_lock(&node->lock);
    node_pair *pair = NULL;
    for (size_t i = 0; i < NODE_MAX_PAIRS; ++i) {
        if (node->pairs[i].used && node->pairs[i].complete &&
            bearer_is(auth, node->pairs[i].session_token)) {
            pair = &node->pairs[i];
            break;
        }
    }
    if (!pair || pair->expires_at < now_seconds()) {
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(resp, 401, "InvalidToken",
                                   "The node session is not valid.");
        return WF_OK;
    }

    wf_xrpc_client *transport = wf_xrpc_client_new(pair->pds_url);
    if (!transport) {
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(resp, 502, "UpstreamUnavailable",
                                   "Could not create the PDS client.");
        return WF_OK;
    }

    wf_oauth_client_auth client_auth = {.client_id = node->client_id,
                                        .authorization_server_issuer =
                                            pair->server.issuer,
                                        .signing_key = NULL,
                                        .key_id = NULL};
    wf_auth_client *auth_client = wf_auth_client_new(
        transport, &pair->session, &pair->server, &client_auth);
    if (!auth_client) {
        wf_xrpc_client_free(transport);
        pthread_mutex_unlock(&node->lock);
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                   "Could not create the PDS client.");
        return WF_OK;
    }

    wf_response upstream = {0};
    wf_status st;
    if (strcmp(req->method, "GET") == 0) {
        st = wf_auth_client_query(auth_client, req->nsid, req->raw_query,
                                  &upstream);
    } else if (strcmp(req->method, "POST") == 0) {
        if (req->content_type &&
            strncasecmp(req->content_type, "application/json", 16) != 0) {
            st = wf_auth_client_upload_blob(auth_client, req->nsid, req->body,
                                            req->body_len, req->content_type,
                                            &upstream);
        } else {
            st = wf_auth_client_procedure(
                auth_client, req->nsid,
                req->body ? (const char *)req->body : NULL, &upstream);
        }
    } else {
        st = WF_ERR_INVALID_ARG;
    }

    resp->http_status =
        upstream.status > 0 ? (int)upstream.status : (st == WF_OK ? 200 : 502);
    wf_xrpc_response_set_content_type(resp, "application/json");
    if (upstream.body)
        wf_xrpc_response_set_body(resp, upstream.body, upstream.body_len);
    wf_response_free(&upstream);
    wf_auth_client_free(auth_client);
    wf_xrpc_client_free(transport);
    pthread_mutex_unlock(&node->lock);
    return WF_OK;
}

wf_oauth_node *wf_oauth_node_new(const wf_oauth_node_config *cfg) {
    if (!cfg || !cfg->public_base_url || !cfg->public_base_url[0]) return NULL;

    wf_oauth_node *node = calloc(1, sizeof *node);
    if (!node) return NULL;

    node->public_base_url = dupstr(cfg->public_base_url);
    node->client_name =
        dupstr(cfg->client_name ? cfg->client_name : "Wolfram OAuth Node");
    node->scope = dupstr(cfg->scope ? cfg->scope : "atproto repo:* blob:*/*");
    node->slingshot_url =
        dupstr(cfg->slingshot_url ? cfg->slingshot_url
                                  : "https://slingshot.microcosm.blue");
    node->pairing_ttl = cfg->pairing_ttl ? cfg->pairing_ttl : 600;

    if (!node->public_base_url || !node->client_name || !node->scope ||
        !node->slingshot_url) {
        wf_oauth_node_free(node);
        return NULL;
    }

    while (strlen(node->public_base_url) > 1 &&
           node->public_base_url[strlen(node->public_base_url) - 1] == '/')
        node->public_base_url[strlen(node->public_base_url) - 1] = '\0';

    size_t n = strlen(node->public_base_url) +
               strlen("/oauth-client-metadata.json") + 1;
    node->client_id = malloc(n);
    if (!node->client_id) {
        wf_oauth_node_free(node);
        return NULL;
    }
    snprintf(node->client_id, n, "%s/oauth-client-metadata.json",
             node->public_base_url);

    n = strlen(node->public_base_url) + strlen("/oauth/callback") + 1;
    node->redirect_uri = malloc(n);
    if (!node->redirect_uri) {
        wf_oauth_node_free(node);
        return NULL;
    }
    snprintf(node->redirect_uri, n, "%s/oauth/callback", node->public_base_url);

    cJSON *meta = cJSON_CreateObject();
    cJSON *redirects = cJSON_CreateArray();
    cJSON *grants = cJSON_CreateArray();
    cJSON *responses = cJSON_CreateArray();
    if (!meta || !redirects || !grants || !responses) {
        cJSON_Delete(meta);
        cJSON_Delete(redirects);
        cJSON_Delete(grants);
        cJSON_Delete(responses);
        wf_oauth_node_free(node);
        return NULL;
    }
    cJSON_AddStringToObject(meta, "client_id", node->client_id);
    cJSON_AddStringToObject(meta, "client_name", node->client_name);
    cJSON_AddStringToObject(meta, "client_uri", node->public_base_url);
    cJSON_AddStringToObject(meta, "scope", node->scope);
    cJSON_AddStringToObject(meta, "token_endpoint_auth_method", "none");
    cJSON_AddBoolToObject(meta, "dpop_bound_access_tokens", 1);
    cJSON_AddItemToArray(redirects, cJSON_CreateString(node->redirect_uri));
    cJSON_AddItemToArray(grants, cJSON_CreateString("authorization_code"));
    cJSON_AddItemToArray(grants, cJSON_CreateString("refresh_token"));
    cJSON_AddItemToArray(responses, cJSON_CreateString("code"));
    cJSON_AddItemToObject(meta, "redirect_uris", redirects);
    cJSON_AddItemToObject(meta, "grant_types", grants);
    cJSON_AddItemToObject(meta, "response_types", responses);

    node->metadata_json = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    if (!node->metadata_json) {
        wf_oauth_node_free(node);
        return NULL;
    }

    if (pthread_mutex_init(&node->lock, NULL) != 0) {
        wf_oauth_node_free(node);
        return NULL;
    }

    if (wf_oauth_client_metadata_parse(
            node->metadata_json, strlen(node->metadata_json), node->client_id,
            &node->client) != WF_OK) {
        wf_oauth_node_free(node);
        return NULL;
    }
    return node;
}

wf_status wf_oauth_node_start(wf_oauth_node *node, const char *listen_address,
                              uint16_t port, unsigned int thread_count) {
    if (!node || !listen_address || node->server) return WF_ERR_INVALID_ARG;

    node->server = wf_xrpc_server_start(listen_address, port, thread_count);
    if (!node->server) return WF_ERR_INTERNAL;

    wf_status st = wf_xrpc_server_register_static_get(
        node->server, "/oauth-client-metadata.json", "application/json",
        node->metadata_json, strlen(node->metadata_json));
    if (st == WF_OK)
        st = wf_xrpc_server_register_http_prefix(node->server, "GET", "/pair/",
                                                 pair_page_handler, node);
    if (st == WF_OK)
        st = wf_xrpc_server_register_http_route(
            node->server, "GET", "/oauth/callback", callback_handler, node);
    if (st == WF_OK)
        st = wf_xrpc_server_register_query(
            node->server, "uk.ewancroft.oauth.poll", poll_handler, node);
    if (st == WF_OK)
        st = wf_xrpc_server_register_procedure(
            node->server, "uk.ewancroft.oauth.begin", begin_handler, node);
    if (st == WF_OK)
        st = wf_xrpc_server_set_fallback(node->server, proxy_handler, node);

    if (st != WF_OK) {
        wf_xrpc_server_free(node->server);
        node->server = NULL;
    }
    return st;
}

void wf_oauth_node_stop(wf_oauth_node *node) {
    if (node && node->server) wf_xrpc_server_stop(node->server);
}

void wf_oauth_node_free(wf_oauth_node *node) {
    if (!node) return;
    wf_oauth_node_stop(node);
    if (node->server) wf_xrpc_server_free(node->server);
    for (size_t i = 0; i < NODE_MAX_PAIRS; ++i) pair_free(&node->pairs[i]);
    wf_oauth_client_metadata_free(&node->client);
    free(node->metadata_json);
    free(node->client_id);
    free(node->redirect_uri);
    free(node->public_base_url);
    free(node->client_name);
    free(node->scope);
    free(node->slingshot_url);
    pthread_mutex_destroy(&node->lock);
    free(node);
}

uint16_t wf_oauth_node_port(const wf_oauth_node *node) {
    return node && node->server ? wf_xrpc_server_port(node->server) : 0;
}

wf_xrpc_server *wf_oauth_node_server(wf_oauth_node *node) {
    return node ? node->server : NULL;
}
