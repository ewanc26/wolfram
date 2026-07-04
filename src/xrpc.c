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

/* Shared request path for both query (GET) and procedure (POST). */
static wf_status wf_xrpc_request(wf_xrpc_client *client,
                                  const char *nsid,
                                  const char *query_string,
                                  const char *json_body,
                                  wf_response *out) {
    if (!client || !nsid || !out) {
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
    struct curl_slist *headers = NULL;

    if (client->auth_header) {
        headers = curl_slist_append(headers, client->auth_header);
    }
    if (json_body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (json_body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_body));
    }
    /* Otherwise GET, which is curl's default — nothing to set. */

    CURLcode res = curl_easy_perform(curl);

    wf_status status = WF_OK;
    if (res != CURLE_OK) {
        status = WF_ERR_NETWORK;
    } else {
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        out->status = http_status;
        out->body = buf.data;
        out->body_len = buf.len;

        if (http_status < 200 || http_status >= 300) {
            status = WF_ERR_HTTP;
        }
    }

    if (status != WF_OK && buf.data) {
        free(buf.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(url);

    return status;
}

wf_status wf_xrpc_query(wf_xrpc_client *client,
                         const char *nsid,
                         const char *query_string,
                         wf_response *out) {
    return wf_xrpc_request(client, nsid, query_string, NULL, out);
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client,
                             const char *nsid,
                             const char *json_body,
                             wf_response *out) {
    return wf_xrpc_request(client, nsid, NULL, json_body, out);
}

void wf_response_free(wf_response *res) {
    if (!res) return;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    res->status = 0;
}

wf_status wf_http_get(wf_xrpc_client *client, const char *url, wf_response *out) {
    if (!client || !url || !out) {
        return WF_ERR_INVALID_ARG;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        return WF_ERR_ALLOC;
    }

    struct wf_buffer buf = {0};
    struct curl_slist *headers = NULL;

    if (client->auth_header) {
        headers = curl_slist_append(headers, client->auth_header);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wf_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wolfram/" WOLFRAM_VERSION_STRING);
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);

    wf_status status = WF_OK;
    if (res != CURLE_OK) {
        status = WF_ERR_NETWORK;
    } else {
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        out->status = http_status;
        out->body = buf.data;
        out->body_len = buf.len;

        if (http_status < 200 || http_status >= 300) {
            status = WF_ERR_HTTP;
        }
    }

    if (status != WF_OK && buf.data) {
        free(buf.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return status;
}
