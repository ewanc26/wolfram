/**
 * xrpc.c — libcurl-backed implementation of the XRPC transport.
 *
 * This is the one module in the initial scaffold that actually does
 * something end to end: it can hit a real PDS and get bytes back.
 * Everything else (identity, crypto, repo) builds on top of this.
 */

#include "wolfram/xrpc.h"
#include "wolfram/version.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WF_LOG_LEVEL_DEBUG 0
#define WF_LOG_LEVEL_INFO  1
#define WF_LOG_LEVEL_WARN  2
#define WF_LOG_LEVEL_ERROR 3

#ifndef WOLFRAM_LOG_LEVEL
#define WOLFRAM_LOG_LEVEL WF_LOG_LEVEL_WARN
#endif

static void wf_log(int level, const char *fmt, ...) {
    if (level < WOLFRAM_LOG_LEVEL) return;
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    va_list ap;
    fprintf(stderr, "Wolfram [%s] ", names[level]);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#define WF_LOG_DEBUG(...) wf_log(WF_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define WF_LOG_INFO(...)  wf_log(WF_LOG_LEVEL_INFO,  __VA_ARGS__)
#define WF_LOG_WARN(...)  wf_log(WF_LOG_LEVEL_WARN,  __VA_ARGS__)
#define WF_LOG_ERROR(...) wf_log(WF_LOG_LEVEL_ERROR, __VA_ARGS__)

struct wf_xrpc_client {
    char *base_url;   /* e.g. "https://eurosky.social", no trailing slash */
    char *auth_header; /* "Authorization: Bearer <jwt>", or NULL */
    char *ca_bundle;  /* path to custom CA bundle, or NULL for system default */
    wf_xrpc_handler_fn handler; /* NULL in production; test seam */
    void *handler_userdata;
    wf_xrpc_refresh_fn refresh_cb; /* NULL unless auto-refresh is enabled */
    void *refresh_userdata;
    int refreshing; /* re-entrancy guard while a refresh is in flight */
};

/* curl write callback: append incoming bytes to a growable buffer. */
struct wf_buffer {
    char  *data;
    size_t len;
    size_t cap;
};

struct wf_header_capture { char *dpop_nonce; };

static size_t wf_curl_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct wf_header_capture *capture = userdata;
    size_t len = size * nmemb;
    static const char name[] = "DPoP-Nonce:";
    size_t i, start, end;
    if (len < sizeof(name) - 1) return len;
    for (i = 0; i < sizeof(name) - 1; i++) {
        if (tolower((unsigned char)ptr[i]) != tolower((unsigned char)name[i])) return len;
    }
    start = sizeof(name) - 1;
    while (start < len && (ptr[start] == ' ' || ptr[start] == '\t')) start++;
    end = len;
    while (end > start && (ptr[end - 1] == '\r' || ptr[end - 1] == '\n' ||
                           ptr[end - 1] == ' ' || ptr[end - 1] == '\t')) end--;
    char *value = malloc(end - start + 1);
    if (!value) return 0;
    memcpy(value, ptr + start, end - start);
    value[end - start] = '\0';
    free(capture->dpop_nonce);
    capture->dpop_nonce = value;
    return len;
}

static size_t wf_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct wf_buffer *buf = (struct wf_buffer *)userdata;
    size_t chunk = size * nmemb;

    if (buf->len + chunk + 1 > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap * 2;
        while (new_cap < buf->len + chunk + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(buf->data, new_cap);
        if (!grown) {
            return 0; /* signals error to curl */
        }
        buf->data = grown;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, chunk);
    buf->len += chunk;
    buf->data[buf->len] = '\0';
    return chunk;
}

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
    if (!service_base_url || service_base_url[0] == '\0') {
        return NULL;
    }

    wf_xrpc_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;

    client->base_url = wf_normalise_base(service_base_url);
    if (!client->base_url) {
        free(client);
        return NULL;
    }

    client->auth_header = NULL;
    return client;
}

void wf_xrpc_client_free(wf_xrpc_client *client) {
    if (!client) return;
    free(client->base_url);
    free(client->auth_header);
    free(client->ca_bundle);
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
        snprintf(client->auth_header, needed, "Authorization: Bearer %s", access_jwt);
    }
}

void wf_xrpc_client_set_ca_bundle(wf_xrpc_client *client, const char *path) {
    if (!client) return;

    free(client->ca_bundle);
    client->ca_bundle = NULL;

    if (!path) return;

    client->ca_bundle = strdup(path);
}

wf_status wf_xrpc_client_set_base_url(wf_xrpc_client *client,
                                      const char *service_base_url) {
    if (!client || !service_base_url || service_base_url[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    char *normalised = wf_normalise_base(service_base_url);
    if (!normalised) return WF_ERR_ALLOC;

    free(client->base_url);
    client->base_url = normalised;
    return WF_OK;
}

void wf_xrpc_client_set_refresh_handler(wf_xrpc_client *client,
                                        wf_xrpc_refresh_fn fn, void *userdata) {
    if (!client) return;
    client->refresh_cb = fn;
    client->refresh_userdata = userdata;
}

/* Convert a curl header list into an array of wf_http_header (owned copies). */
static wf_http_header *wf_slist_to_headers(struct curl_slist *list,
                                           size_t *count) {
    size_t n = 0;
    for (struct curl_slist *p = list; p; p = p->next) n++;
    *count = n;
    if (n == 0) return NULL;
    wf_http_header *arr = calloc(n, sizeof(*arr));
    if (!arr) {
        *count = 0;
        return NULL;
    }
    size_t i = 0;
    for (struct curl_slist *p = list; p && i < n; p = p->next, i++) {
        const char *data = p->data;
        const char *colon = strchr(data, ':');
        if (!colon) {
            arr[i].name = (const char *)strdup(data);
            arr[i].value = (const char *)strdup("");
            continue;
        }
        size_t nl = (size_t)(colon - data);
        char *name = malloc(nl + 1);
        if (name) {
            memcpy(name, data, nl);
            name[nl] = '\0';
        }
        arr[i].name = (const char *)name;
        const char *v = colon + 1;
        while (*v == ' ' || *v == '\t') v++;
        arr[i].value = (const char *)strdup(v);
    }
    return arr;
}

static void wf_http_headers_free(wf_http_header *arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) {
        free((void *)arr[i].name);
        free((void *)arr[i].value);
    }
    free(arr);
}

/*
 * Single transport primitive shared by every request path. When a test handler
 * is installed it receives the fully-resolved request; otherwise the request is
 * issued over libcurl. Network I/O therefore remains isolated to this module.
 */
static wf_status wf_xrpc_perform(wf_xrpc_client *client, const char *method,
                                 const char *url, const char *content_type,
                                 const void *body, size_t body_len,
                                 struct curl_slist *headers, wf_response *out) {
    if (client->handler) {
        wf_http_header *harr = NULL;
        size_t hcount = 0;
        if (headers) {
            harr = wf_slist_to_headers(headers, &hcount);
            if (hcount && !harr) {
                curl_slist_free_all(headers);
                return WF_ERR_ALLOC;
            }
        }
        memset(out, 0, sizeof(*out));
        wf_status status = client->handler(client->handler_userdata, method, url,
                                           content_type, (const char *)body,
                                           body_len, harr, hcount, out);
        wf_http_headers_free(harr, hcount);
        curl_slist_free_all(headers);
        return status;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(headers);
        return WF_ERR_ALLOC;
    }

    struct wf_buffer buf = {0};
    struct wf_header_capture capture = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, wf_curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);
    if (client->ca_bundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, client->ca_bundle);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (strcmp(method, "POST") == 0) {
        const char *post_body = body ? (const char *)body : "";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    }

    WF_LOG_DEBUG("HTTP %s %s", method, url);
    wf_status status = WF_OK;
    CURLcode curl_rc = curl_easy_perform(curl);
    if (curl_rc != CURLE_OK) {
        WF_LOG_ERROR("HTTP %s %s failed: %s", method, url, curl_easy_strerror(curl_rc));
        status = WF_ERR_NETWORK;
    } else {
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        out->status = http_status;
        out->body = buf.data;
        out->body_len = buf.len;
        out->dpop_nonce = capture.dpop_nonce;
        capture.dpop_nonce = NULL;

        WF_LOG_DEBUG("HTTP response %ld for %s", http_status, url);

        if (http_status < 200 || http_status >= 300) {
            status = WF_ERR_HTTP;
        }
    }

    /* On WF_ERR_HTTP the body was already transferred to `out` (the caller
     * owns and must free it per the API contract), so only discard it for
     * the error statuses where `out` was not populated. */
    if (status != WF_OK && status != WF_ERR_HTTP && buf.data) {
        free(buf.data);
    }
    free(capture.dpop_nonce);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

/* Detect an expired/invalid access token in a completed response: either an
 * HTTP 401, or an XRPC error envelope of "ExpiredToken"/"InvalidToken". */
static int wf_xrpc_response_is_expired(const wf_response *out) {
    if (!out) return 0;
    if (out->status == 401) return 1;

    char *err = NULL;
    if (wf_xrpc_error(out, &err, NULL) == WF_OK && err) {
        int match = strcmp(err, "ExpiredToken") == 0 ||
                    strcmp(err, "InvalidToken") == 0;
        free(err);
        return match;
    }
    free(err);
    return 0;
}

/* Build the request headers (auth + optional content-type) for one attempt. */
static wf_status wf_xrpc_build_headers(wf_xrpc_client *client, int is_post,
                                       const char *content_type,
                                       struct curl_slist **out_headers) {
    struct curl_slist *headers = NULL;

    if (client->auth_header) {
        headers = curl_slist_append(headers, client->auth_header);
    }
    if (is_post) {
        size_t header_len = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *header = malloc(header_len);
        if (!header) {
            curl_slist_free_all(headers);
            return WF_ERR_ALLOC;
        }
        snprintf(header, header_len, "Content-Type: %s", content_type);
        struct curl_slist *grown = curl_slist_append(headers, header);
        free(header);
        if (!grown) {
            curl_slist_free_all(headers);
            return WF_ERR_ALLOC;
        }
        headers = grown;
    }

    *out_headers = headers;
    return WF_OK;
}

/* Shared request path for both query (GET) and POST variants. */
static wf_status wf_xrpc_request(wf_xrpc_client *client,
                                 const char *nsid,
                                 const char *query_string,
                                 const void *body,
                                 size_t body_len,
                                 const char *content_type,
                                 int is_post,
                                 wf_response *out) {
    if (!client || !nsid || !out || (body_len > 0 && !body) ||
        (is_post && !content_type)) {
        return WF_ERR_INVALID_ARG;
    }

    /* Whether this call went out authenticated — only such calls are eligible
     * for a refresh+retry on an expired token. */
    int had_auth = client->auth_header != NULL;
    wf_status status = WF_OK;
    char *url = NULL;

    for (int attempt = 0; ; attempt++) {
        /* Rebuild the URL each attempt: a refresh may re-point the client at a
         * newly discovered PDS base URL. */
        free(url);
        size_t url_cap = strlen(client->base_url) + strlen("/xrpc/") +
                         strlen(nsid) + 1 +
                         (query_string ? strlen(query_string) + 1 : 0);
        url = malloc(url_cap);
        if (!url) {
            return WF_ERR_ALLOC;
        }
        if (query_string && query_string[0] != '\0') {
            snprintf(url, url_cap, "%s/xrpc/%s?%s", client->base_url, nsid,
                     query_string);
        } else {
            snprintf(url, url_cap, "%s/xrpc/%s", client->base_url, nsid);
        }

        WF_LOG_DEBUG("XRPC %s %s", is_post ? "POST" : "GET", url);

        struct curl_slist *headers = NULL;
        status = wf_xrpc_build_headers(client, is_post, content_type, &headers);
        if (status != WF_OK) {
            break;
        }

        status = wf_xrpc_perform(client, is_post ? "POST" : "GET", url,
                                 content_type, body, body_len, headers, out);

        /* Retry at most once, and only when an authenticated request came back
         * with an expired/invalid token and a refresh path is available. The
         * re-entrancy guard prevents a refresh call (which itself issues XRPC)
         * from recursively triggering another refresh. */
        if (attempt == 0 && had_auth && client->refresh_cb &&
            !client->refreshing && wf_xrpc_response_is_expired(out)) {
            client->refreshing = 1;
            wf_status refreshed = client->refresh_cb(client->refresh_userdata);
            client->refreshing = 0;
            if (refreshed == WF_OK) {
                wf_response_free(out);
                continue; /* re-issue once with the refreshed credentials */
            }
        }
        break;
    }

    free(url);
    return status;
}

wf_status wf_xrpc_query(wf_xrpc_client *client,
                         const char *nsid,
                         const char *query_string,
                         wf_response *out) {
    return wf_xrpc_request(client, nsid, query_string, NULL, 0, NULL, 0, out);
}

wf_status wf_xrpc_query_params(wf_xrpc_client *client,
                                const char *nsid,
                                const wf_xrpc_param *params,
                                size_t param_count,
                                wf_response *out) {
    if (!client || !nsid || !out || (param_count > 0 && !params)) {
        return WF_ERR_INVALID_ARG;
    }
    if (param_count == 0) {
        return wf_xrpc_query(client, nsid, NULL, out);
    }

    CURL *curl = curl_easy_init();
    if (!curl) return WF_ERR_ALLOC;

    char **names = calloc(param_count, sizeof(*names));
    char **values = calloc(param_count, sizeof(*values));
    if (!names || !values) {
        free(names);
        free(values);
        curl_easy_cleanup(curl);
        return WF_ERR_ALLOC;
    }

    size_t query_len = 1;
    wf_status status = WF_OK;
    for (size_t i = 0; i < param_count; i++) {
        if (!params[i].name || !params[i].value) {
            status = WF_ERR_INVALID_ARG;
            break;
        }
        names[i] = curl_easy_escape(curl, params[i].name, 0);
        values[i] = curl_easy_escape(curl, params[i].value, 0);
        if (!names[i] || !values[i]) {
            status = WF_ERR_ALLOC;
            break;
        }
        query_len += strlen(names[i]) + 1 + strlen(values[i]);
        if (i > 0) query_len++;
    }

    char *query = NULL;
    if (status == WF_OK) {
        query = malloc(query_len);
        if (!query) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK) {
        size_t offset = 0;
        for (size_t i = 0; i < param_count; i++) {
            int written = snprintf(query + offset, query_len - offset,
                                   "%s%s=%s", i ? "&" : "",
                                   names[i], values[i]);
            if (written < 0 || (size_t)written >= query_len - offset) {
                status = WF_ERR_ALLOC;
                break;
            }
            offset += (size_t)written;
        }
    }

    for (size_t i = 0; i < param_count; i++) {
        curl_free(names[i]);
        curl_free(values[i]);
    }
    free(names);
    free(values);
    curl_easy_cleanup(curl);

    if (status == WF_OK) {
        status = wf_xrpc_query(client, nsid, query, out);
    }
    free(query);
    return status;
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client,
                             const char *nsid,
                             const char *json_body,
                             wf_response *out) {
    size_t body_len;

    if (!client || !nsid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    body_len = json_body ? strlen(json_body) : 0;
    return wf_xrpc_request(client, nsid, NULL, json_body, body_len,
                           "application/json", 1, out);
}

wf_status wf_xrpc_upload_blob(wf_xrpc_client *client,
                               const char *nsid,
                               const void *data,
                               size_t data_len,
                               const char *content_type,
                               wf_response *out) {
    if (!client || !nsid || !data || data_len == 0 || !content_type || !*content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_xrpc_upload_blob_with_headers(client, nsid, data, data_len,
                                           content_type, NULL, 0, out);
}

wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data,
    size_t data_len, const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out) {
    struct curl_slist *list = NULL;
    wf_status status = WF_OK;
    char *base_url = NULL;
    char *url = NULL;

    if (!client || !nsid || !data || data_len == 0 || !content_type || !*content_type ||
        !out || (header_count && !headers)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    base_url = wf_xrpc_get_base_url(client);
    if (!base_url) return WF_ERR_ALLOC;
    size_t url_cap = strlen(base_url) + strlen("/xrpc/") + strlen(nsid) + 1;
    url = malloc(url_cap);
    if (!url) {
        free(base_url);
        return WF_ERR_ALLOC;
    }
    snprintf(url, url_cap, "%s/xrpc/%s", base_url, nsid);

    if (client->auth_header) {
        list = curl_slist_append(list, client->auth_header);
        if (!list) status = WF_ERR_ALLOC;
    }

    if (status == WF_OK) {
        size_t n = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *line = malloc(n);
        if (!line) {
            status = WF_ERR_ALLOC;
        } else {
            snprintf(line, n, "Content-Type: %s", content_type);
            list = curl_slist_append(list, line);
            free(line);
            if (!list) status = WF_ERR_ALLOC;
        }
    }
    for (size_t i = 0; status == WF_OK && i < header_count; i++) {
        if (!headers[i].name || !headers[i].value) {
            status = WF_ERR_INVALID_ARG;
            break;
        }
        size_t n = strlen(headers[i].name) + strlen(headers[i].value) + 3;
        char *line = malloc(n);
        if (!line) {
            status = WF_ERR_ALLOC;
            break;
        }
        snprintf(line, n, "%s: %s", headers[i].name, headers[i].value);
        struct curl_slist *grown = curl_slist_append(list, line);
        free(line);
        if (!grown) {
            status = WF_ERR_ALLOC;
            break;
        }
        list = grown;
    }

    if (status == WF_OK) {
        status = wf_xrpc_perform(client, "POST", url, content_type, data,
                                 data_len, list, out);
    } else {
        curl_slist_free_all(list);
    }
    free(url);
    free(base_url);
    return status;
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

/* Copy a cJSON string member into a caller-owned buffer, or NULL if absent. */
static char *xrpc_copy_str(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) return NULL;
    size_t n = strlen(item->valuestring) + 1;
    char *s = malloc(n);
    if (s) memcpy(s, item->valuestring, n);
    return s;
}

wf_status wf_xrpc_error(const wf_response *resp,
                        char **out_error, char **out_message) {
    if (out_error) *out_error = NULL;
    if (out_message) *out_message = NULL;
    if (!resp) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_ParseWithLength(resp->body ? resp->body : "",
                                        resp->body_len);
    if (!root) return WF_ERR_PARSE;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }

    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (!cJSON_IsString(err) || err->valuestring == NULL) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }

    char *e = xrpc_copy_str(root, "error");
    char *m = xrpc_copy_str(root, "message");
    if (!e) { free(m); cJSON_Delete(root); return WF_ERR_PARSE; }

    if (out_error) *out_error = e; else free(e);
    if (out_message) *out_message = m; else free(m);
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                        const char *content_type, const char *body,
                        const wf_http_header *extra, size_t extra_count,
                        wf_response *out) {
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;
    size_t i;
    if (!client || !url || !content_type || !body || !out ||
        (extra_count && !extra)) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    {
        size_t n = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *line = malloc(n);
        if (!line) return WF_ERR_ALLOC;
        snprintf(line, n, "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, line);
        free(line);
        if (!headers) return WF_ERR_ALLOC;
    }
    for (i = 0; status == WF_OK && i < extra_count; i++) {
        size_t n;
        char *line;
        struct curl_slist *grown;
        if (!extra[i].name || !extra[i].value) { status = WF_ERR_INVALID_ARG; break; }
        n = strlen(extra[i].name) + strlen(extra[i].value) + 3;
        line = malloc(n);
        if (!line) { status = WF_ERR_ALLOC; break; }
        snprintf(line, n, "%s: %s", extra[i].name, extra[i].value);
        grown = curl_slist_append(headers, line);
        free(line);
        if (!grown) { status = WF_ERR_ALLOC; break; }
        headers = grown;
    }
    if (status == WF_OK) {
        status = wf_xrpc_perform(client, "POST", url, content_type, body,
                                 strlen(body), headers, out);
    } else {
        curl_slist_free_all(headers);
    }
    return status;
}

/**
 * Returns a copy of the client's base URL.
 * The caller owns the returned string and must free it.
 */
char *wf_xrpc_get_base_url(wf_xrpc_client *client) {
    if (!client || !client->base_url) return NULL;
    return strdup(client->base_url);
}

/**
 * Perform a generic HTTP GET with extra headers.
 *
 * Uses the client's transport and auth settings. `url` must be a complete
 * absolute URL.
 *
 * On WF_OK, `out` is populated and must be released with `wf_response_free`.
 */
wf_status wf_http_get_with_headers(wf_xrpc_client *client, const char *url,
                                   const wf_http_header *extra, size_t extra_count,
                                   wf_response *out) {
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;
    size_t i;

    if (!client || !url || !out || (extra_count && !extra)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (client->auth_header) {
        headers = curl_slist_append(headers, client->auth_header);
    }

    for (i = 0; status == WF_OK && i < extra_count; i++) {
        size_t n;
        char *line;
        struct curl_slist *grown;
        if (!extra[i].name || !extra[i].value) { status = WF_ERR_INVALID_ARG; break; }
        n = strlen(extra[i].name) + strlen(extra[i].value) + 3;
        line = malloc(n);
        if (!line) { status = WF_ERR_ALLOC; break; }
        snprintf(line, n, "%s: %s", extra[i].name, extra[i].value);
        grown = curl_slist_append(headers, line);
        free(line);
        if (!grown) { status = WF_ERR_ALLOC; break; }
        headers = grown;
    }

    if (status == WF_OK) {
        status = wf_xrpc_perform(client, "GET", url, NULL, NULL, 0, headers, out);
    } else {
        curl_slist_free_all(headers);
    }
    return status;
}

/**
 * Perform a generic HTTP GET to an arbitrary URL (not an XRPC endpoint).
 *
 * Uses the client's transport and auth settings. `url` must be a complete
 * absolute URL. On WF_OK, `out` is populated and must be released with
 * wf_response_free.
 */
wf_status wf_http_get(wf_xrpc_client *client, const char *url, wf_response *out) {
    return wf_http_get_with_headers(client, url, NULL, 0, out);
}
