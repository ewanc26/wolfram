/**
 * xrpc.c — libcurl-backed implementation of the XRPC transport.
 *
 * This is the one module in the initial scaffold that actually does
 * something end to end: it can hit a real PDS and get bytes back.
 * Everything else (identity, crypto, repo) builds on top of this.
 */

#include "wolfram/xrpc.h"
#include "wolfram/version.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wf_xrpc_client {
    char *base_url;   /* e.g. "https://eurosky.social", no trailing slash */
    char *auth_header; /* "Authorization: Bearer <jwt>", or NULL */
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
    free(client);
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

    CURL *curl = curl_easy_init();
    if (!curl) {
        return WF_ERR_ALLOC;
    }

    /* Build "<base>/xrpc/<nsid>[?query]" */
    size_t url_cap = strlen(client->base_url) + strlen("/xrpc/") + strlen(nsid) + 1
                     + (query_string ? strlen(query_string) + 1 : 0);
    char *url = malloc(url_cap);
    if (!url) {
        curl_easy_cleanup(curl);
        return WF_ERR_ALLOC;
    }

    if (query_string && query_string[0] != '\0') {
        snprintf(url, url_cap, "%s/xrpc/%s?%s", client->base_url, nsid, query_string);
    } else {
        snprintf(url, url_cap, "%s/xrpc/%s", client->base_url, nsid);
    }

    struct wf_buffer buf = {0};
    struct wf_header_capture capture = {0};
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;

    if (client->auth_header) {
        headers = curl_slist_append(headers, client->auth_header);
    }
    if (is_post) {
        size_t header_len = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *header = malloc(header_len);
        if (!header) {
            status = WF_ERR_ALLOC;
        } else {
            snprintf(header, header_len, "Content-Type: %s", content_type);
            headers = curl_slist_append(headers, header);
            free(header);
            if (!headers) {
                status = WF_ERR_ALLOC;
            }
        }
    }

    if (status == WF_OK) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, wf_curl_header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        if (is_post) {
            const char *post_body = body ? (const char *)body : "";
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            status = WF_ERR_NETWORK;
        } else {
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            out->status = http_status;
            out->body = buf.data;
            out->body_len = buf.len;
            out->dpop_nonce = capture.dpop_nonce;
            capture.dpop_nonce = NULL;

            if (http_status < 200 || http_status >= 300) {
                status = WF_ERR_HTTP;
            }
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
    return wf_xrpc_request(client, nsid, NULL, data, data_len, content_type, 1, out);
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

wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                       const char *content_type, const char *body,
                       const wf_http_header *extra, size_t extra_count,
                       wf_response *out) {
    CURL *curl;
    struct wf_buffer buf = {0};
    struct wf_header_capture capture = {0};
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;
    size_t i;
    if (!client || !url || !content_type || !body || !out ||
        (extra_count && !extra)) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    curl = curl_easy_init();
    if (!curl) return WF_ERR_ALLOC;
    {
        size_t n = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *line = malloc(n);
        if (!line) { curl_easy_cleanup(curl); return WF_ERR_ALLOC; }
        snprintf(line, n, "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, line);
        free(line);
        if (!headers) status = WF_ERR_ALLOC;
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
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, wf_curl_header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);
        if (curl_easy_perform(curl) != CURLE_OK) status = WF_ERR_NETWORK;
    }
    if (status == WF_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
        out->body = buf.data;
        out->body_len = buf.len;
        out->dpop_nonce = capture.dpop_nonce;
        buf.data = NULL;
        capture.dpop_nonce = NULL;
        if (out->status < 200 || out->status >= 300) status = WF_ERR_HTTP;
    }
    free(buf.data);
    free(capture.dpop_nonce);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
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
    CURL *curl;
    struct wf_buffer buf = {0};
    struct wf_header_capture capture = {0};
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;
    size_t i;

    if (!client || !url || !out || (extra_count && !extra)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    curl = curl_easy_init();
    if (!curl) return WF_ERR_ALLOC;

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

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, wf_curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);

    if (curl_easy_perform(curl) != CURLE_OK) status = WF_ERR_NETWORK;

    if (status == WF_OK) {
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        out->status = http_status;
        out->body = buf.data;
        out->body_len = buf.len;
        out->dpop_nonce = capture.dpop_nonce;

        if (http_status < 200 || http_status >= 300) status = WF_ERR_HTTP;
    }

    if (status != WF_OK && buf.data) {
        free(buf.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
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
