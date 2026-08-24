/**
 * xrpc.c — libcurl-backed implementation of the XRPC transport.
 *
 * This is the one module in the initial scaffold that actually does
 * something end to end: it can hit a real PDS and get bytes back.
 * Everything else (identity, crypto, repo) builds on top of this.
 */

#include "wolfram/xrpc.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/log.h"

#if !defined(WOLFRAM_CURL_MBEDTLS) && defined(WOLFRAM_WIIU)
#define WOLFRAM_CURL_MBEDTLS 1
#endif

/*
 * The application TLS RNG hook (wf_xrpc_client_set_tls_rng) reaches into
 * libcurl's mbedTLS backend, so it is only compiled where mbedTLS headers are
 * guaranteed to be present and libcurl is actually built against them. The Wii
 * U is the platform that needs it; another target can opt in by defining
 * WOLFRAM_CURL_MBEDTLS.
 */
#if !defined(WOLFRAM_CURL_MBEDTLS) && defined(WOLFRAM_WIIU)
#define WOLFRAM_CURL_MBEDTLS 1
#endif

#if defined(WOLFRAM_CURL_MBEDTLS)
#include <mbedtls/ssl.h>
#endif

struct wf_xrpc_client {
    char *base_url;    /* e.g. "https://eurosky.social", no trailing slash */
    char *auth_header; /* "Authorization: Bearer <jwt>", or NULL */
    char *ca_bundle; /* path to custom CA bundle, or NULL for system default */
    wf_xrpc_handler_fn handler; /* NULL in production; test seam */
    void *handler_userdata;
    wf_xrpc_refresh_fn refresh_cb; /* NULL unless auto-refresh is enabled */
    void *refresh_userdata;
    int refreshing;        /* re-entrancy guard while a refresh is in flight */
    wf_tls_rng_fn tls_rng; /* NULL unless the application supplied one */
    void *tls_rng_userdata;
    char *last_error; /* XRPC error message from the last non-2xx response */
    pthread_mutex_t mutex;           /* guards all mutable fields above */
    struct wf_xrpc_pending *pending; /* linked list of in-flight async ops */
};

/* A point-in-time snapshot of the client's transport config. Workers
 * operate on a snapshot so that wf_xrpc_client_set_auth (or other setters)
 * called from another thread cannot tear down strings mid-request. The
 * snapshot strings are heap-owned copies; free with wf_config_free. */
struct wf_client_config {
    char *base_url;
    char *auth_header;
    char *ca_bundle;
    wf_xrpc_handler_fn handler;
    void *handler_userdata;
    wf_tls_rng_fn tls_rng;
    void *tls_rng_userdata;
};

static void wf_config_free(struct wf_client_config *cfg);
static struct wf_client_config *wf_client_snapshot(wf_xrpc_client *client);

/* ── One-time libcurl initialization ────────────────────────────────── */

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;

static void wf_curl_global_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void wf_curl_ensure_init(void) {
    pthread_once(&curl_once, wf_curl_global_init);
}

/* ── Config snapshot ────────────────────────────────────────────────── */

static void wf_config_free(struct wf_client_config *cfg) {
    if (!cfg) return;
    free(cfg->base_url);
    free(cfg->auth_header);
    free(cfg->ca_bundle);
    free(cfg);
}

/* Take a point-in-time snapshot of the client's transport config. The
 * caller owns the result and must free it with wf_config_free. Returns NULL
 * on allocation failure. Safe to call from any thread. */
static struct wf_client_config *wf_client_snapshot(wf_xrpc_client *client) {
    if (!client) return NULL;
    struct wf_client_config *cfg =
        (struct wf_client_config *)calloc(1, sizeof(*cfg));
    if (!cfg) return NULL;
    pthread_mutex_lock(&client->mutex);
    cfg->base_url = client->base_url ? strdup(client->base_url) : NULL;
    cfg->auth_header = client->auth_header ? strdup(client->auth_header) : NULL;
    cfg->ca_bundle = client->ca_bundle ? strdup(client->ca_bundle) : NULL;
    cfg->handler = client->handler;
    cfg->handler_userdata = client->handler_userdata;
    cfg->tls_rng = client->tls_rng;
    cfg->tls_rng_userdata = client->tls_rng_userdata;
    pthread_mutex_unlock(&client->mutex);
    if ((!cfg->base_url && client->base_url) ||
        (!cfg->auth_header && client->auth_header) ||
        (!cfg->ca_bundle && client->ca_bundle)) {
        wf_config_free(cfg);
        return NULL;
    }
    return cfg;
}

/* ── Async pending handle ───────────────────────────────────────────── */

struct wf_xrpc_pending {
    wf_xrpc_client *client; /* borrowed for unlink on free; NULL if gone */
    struct wf_client_config *config; /* snapshot, owned */
    char *nsid;                      /* strdup'd, owned */
    char *query_string;              /* strdup'd for query, owned */
    char *body;                      /* malloc'd copy for procedure, owned */
    size_t body_len;
    char *content_type; /* strdup'd, owned */
    int is_post;
    wf_response result; /* response (valid after completion) */
    char *error_msg;    /* reserved */
    wf_status status;   /* final status */
    pthread_t thread;   /* worker thread (if started) */
    bool started;
    bool done;
    bool cancelled;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct wf_xrpc_pending *next; /* owned by client->pending list */
};

/* ── Application TLS RNG ─────────────────────────────────────────────── */

int wf_xrpc_tls_rng_supported(void) {
#if defined(WOLFRAM_CURL_MBEDTLS)
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info || !info->ssl_version) return 0;
    return strncmp(info->ssl_version, "mbedTLS", 7) == 0;
#else
    return 0;
#endif
}

#if defined(WOLFRAM_CURL_MBEDTLS)
/*
 * curl invokes this from mbed_connect_step1 after its own
 * mbedtls_ssl_conf_rng() call and before mbedtls_ssl_setup(), so installing
 * ours here replaces curl's DRBG for the whole handshake.
 */
static CURLcode wf_tls_ctx_cb_cfg(CURL *curl, void *ssl_ctx, void *userdata) {
    struct wf_client_config *cfg = (struct wf_client_config *)userdata;
    (void)curl;

    if (!cfg || !cfg->tls_rng || !ssl_ctx) return CURLE_OK;

    mbedtls_ssl_conf_rng((mbedtls_ssl_config *)ssl_ctx, cfg->tls_rng,
                         cfg->tls_rng_userdata);
    return CURLE_OK;
}
#endif

wf_status wf_xrpc_client_set_tls_rng(wf_xrpc_client *client, wf_tls_rng_fn fn,
                                     void *userdata) {
    if (!client) return WF_ERR_INVALID_ARG;

    pthread_mutex_lock(&client->mutex);
    /* Clearing is always allowed: it restores libcurl's own RNG, which every
     * build can do. */
    if (!fn) {
        client->tls_rng = NULL;
        client->tls_rng_userdata = NULL;
        pthread_mutex_unlock(&client->mutex);
        return WF_OK;
    }

    if (!wf_xrpc_tls_rng_supported()) {
        pthread_mutex_unlock(&client->mutex);
        WF_LOG_WARN("xrpc", "application TLS RNG requested but unsupported in "
                            "this build; leaving libcurl's own RNG in place");
        return WF_ERR_UNSUPPORTED;
    }

    client->tls_rng = fn;
    client->tls_rng_userdata = userdata;
    pthread_mutex_unlock(&client->mutex);
    return WF_OK;
}

/* curl write callback: append incoming bytes to a growable buffer. */
struct wf_buffer {
    char *data;
    size_t len;
    size_t cap;
};

struct wf_header_capture {
    char *dpop_nonce;
    char *set_cookie;
    char *location;
};

/*
 * Copy the header value following `name:` (case-insensitive, whitespace and
 * CRLF trimmed) into `*slot`, replacing whatever was there. Shared by every
 * single-header capture below — Set-Cookie included — rather than
 * duplicating the trim logic per header, which is how DPoP-Nonce's own
 * version of this drifted from a copy the last time one was added.
 */
static void wf_capture_header(const char *ptr, size_t len, const char *name,
                              size_t name_len, char **slot) {
    size_t i, start, end;
    if (len < name_len) return;
    for (i = 0; i < name_len; i++) {
        if (tolower((unsigned char)ptr[i]) != tolower((unsigned char)name[i]))
            return;
    }
    start = name_len;
    while (start < len && (ptr[start] == ' ' || ptr[start] == '\t')) start++;
    end = len;
    while (end > start && (ptr[end - 1] == '\r' || ptr[end - 1] == '\n' ||
                           ptr[end - 1] == ' ' || ptr[end - 1] == '\t'))
        end--;
    char *value = malloc(end - start + 1);
    if (!value) return;
    memcpy(value, ptr + start, end - start);
    value[end - start] = '\0';
    free(*slot);
    *slot = value;
}

static size_t wf_curl_header_cb(char *ptr, size_t size, size_t nmemb,
                                void *userdata) {
    struct wf_header_capture *capture = userdata;
    size_t len = size * nmemb;
    wf_capture_header(ptr, len, "DPoP-Nonce:", sizeof("DPoP-Nonce:") - 1,
                      &capture->dpop_nonce);
    wf_capture_header(ptr, len, "Set-Cookie:", sizeof("Set-Cookie:") - 1,
                      &capture->set_cookie);
    wf_capture_header(ptr, len, "Location:", sizeof("Location:") - 1,
                      &capture->location);
    return len;
}

static size_t wf_curl_write_cb(char *ptr, size_t size, size_t nmemb,
                               void *userdata) {
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
    if (pthread_mutex_init(&client->mutex, NULL) != 0) {
        free(client->base_url);
        free(client);
        return NULL;
    }
    client->pending = NULL;
    wf_curl_ensure_init();
    return client;
}

void wf_xrpc_client_free(wf_xrpc_client *client) {
    if (!client) return;
    /* Detach all outstanding pending handles so their free() won't dereference
     * a freed client when unlinking from the list. The worker threads use only
     * their config snapshot and never touch the client, so they keep running
     * safely; the caller still must call wf_xrpc_pending_free on each handle.
     */
    pthread_mutex_lock(&client->mutex);
    for (struct wf_xrpc_pending *p = client->pending; p; p = p->next) {
        p->client = NULL;
    }
    pthread_mutex_unlock(&client->mutex);
    pthread_mutex_destroy(&client->mutex);
    free(client->base_url);
    free(client->auth_header);
    free(client->ca_bundle);
    free(client->last_error);
    free(client);
}

const char *wf_xrpc_last_error(const wf_xrpc_client *client) {
    if (!client) return NULL;
    pthread_mutex_lock(&((wf_xrpc_client *)client)->mutex);
    const char *err = client->last_error;
    const char *copy = err ? strdup(err) : NULL;
    pthread_mutex_unlock(&((wf_xrpc_client *)client)->mutex);
    /* Return a transient copy: the caller may not hold a client lock, and
     * last_error can be overwritten by the next request. The caller must
     * free the returned string — but to stay compatible with existing callers
     * that treat the return as borrowed, return the live pointer under lock.
     * Callers that need a stable copy should call this before any other
     * request on the same client. */
    free((void *)copy);
    return err;
}

void wf_xrpc_set_handler(wf_xrpc_client *client, wf_xrpc_handler_fn fn,
                         void *userdata) {
    if (!client) return;
    pthread_mutex_lock(&client->mutex);
    client->handler = fn;
    client->handler_userdata = userdata;
    pthread_mutex_unlock(&client->mutex);
}

void wf_xrpc_client_set_auth(wf_xrpc_client *client, const char *access_jwt) {
    if (!client) return;

    pthread_mutex_lock(&client->mutex);
    free(client->auth_header);
    client->auth_header = NULL;

    if (!access_jwt) {
        pthread_mutex_unlock(&client->mutex);
        return;
    }

    size_t needed = strlen("Authorization: Bearer ") + strlen(access_jwt) + 1;
    client->auth_header = malloc(needed);
    if (client->auth_header) {
        snprintf(client->auth_header, needed, "Authorization: Bearer %s",
                 access_jwt);
    }
    pthread_mutex_unlock(&client->mutex);
}

void wf_xrpc_client_set_ca_bundle(wf_xrpc_client *client, const char *path) {
    if (!client) return;

    pthread_mutex_lock(&client->mutex);
    free(client->ca_bundle);
    client->ca_bundle = NULL;

    if (!path) {
        pthread_mutex_unlock(&client->mutex);
        return;
    }

    client->ca_bundle = strdup(path);
    pthread_mutex_unlock(&client->mutex);
}

wf_status wf_xrpc_client_set_base_url(wf_xrpc_client *client,
                                      const char *service_base_url) {
    if (!client || !service_base_url || service_base_url[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    char *normalised = wf_normalise_base(service_base_url);
    if (!normalised) return WF_ERR_ALLOC;

    pthread_mutex_lock(&client->mutex);
    free(client->base_url);
    client->base_url = normalised;
    pthread_mutex_unlock(&client->mutex);
    return WF_OK;
}

void wf_xrpc_client_set_refresh_handler(wf_xrpc_client *client,
                                        wf_xrpc_refresh_fn fn, void *userdata) {
    if (!client) return;
    pthread_mutex_lock(&client->mutex);
    client->refresh_cb = fn;
    client->refresh_userdata = userdata;
    pthread_mutex_unlock(&client->mutex);
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

/* Record the XRPC error envelope of a non-2xx response into `*slot`,
 * replacing whatever was there. Prefers `message`, falls back to the `error`
 * code, and clears the slot when the body has no envelope. */
static void wf_xrpc_set_error_str(char **slot, const wf_response *out) {
    if (!slot || !out) return;
    char *err = NULL, *msg = NULL;
    if (wf_xrpc_error(out, &err, &msg) != WF_OK) {
        free(*slot);
        *slot = NULL;
        return;
    }
    const char *chosen = (msg && *msg) ? msg : err;
    if (chosen && chosen[0]) {
        char *copy = strdup(chosen);
        if (copy) {
            free(*slot);
            *slot = copy;
        }
    }
    free(err);
    free(msg);
}

/*
 * Single transport primitive shared by every request path. Takes a config
 * snapshot so it can run without holding the client's mutex — the snapshot is
 * valid for the lifetime of the call. Does not touch client state; callers
 * are responsible for updating `last_error` under the client mutex if needed.
 */
static wf_status wf_xrpc_perform_cfg(const struct wf_client_config *cfg,
                                     const char *method, const char *url,
                                     const char *content_type, const void *body,
                                     size_t body_len,
                                     struct curl_slist *headers,
                                     wf_response *out) {
    if (cfg->handler) {
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
        wf_status status =
            cfg->handler(cfg->handler_userdata, method, url, content_type,
                         (const char *)body, body_len, harr, hcount, out);
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "wolfram/" WOLFRAM_VERSION_STRING);
    /* Bound the connection handshake and abort a genuinely stalled transfer
     * (a peer that accepts the connection but never sends a response) --
     * previously every request made through this function could hang
     * indefinitely. A low-speed abort rather than a blanket total timeout so
     * legitimately slow-but-progressing large transfers (e.g. blob uploads)
     * are not cut off. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    if (cfg->ca_bundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, cfg->ca_bundle);
    }
#if defined(WOLFRAM_CURL_MBEDTLS)
    if (cfg->tls_rng) {
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, wf_tls_ctx_cb_cfg);
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, (void *)cfg);
    }
#endif
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (strcmp(method, "POST") == 0) {
        const char *post_body = body ? (const char *)body : "";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         (curl_off_t)body_len);
    }

    WF_LOG_DEBUG("xrpc", "HTTP %s %s", method, url);
    wf_status status = WF_OK;
    CURLcode curl_rc = curl_easy_perform(curl);
    if (curl_rc != CURLE_OK) {
        WF_LOG_ERROR("xrpc", "HTTP %s %s failed: %s", method, url,
                     curl_easy_strerror(curl_rc));
        status = WF_ERR_NETWORK;
    } else {
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        out->status = http_status;
        out->body = buf.data;
        out->body_len = buf.len;
        out->dpop_nonce = capture.dpop_nonce;
        capture.dpop_nonce = NULL;
        out->set_cookie = capture.set_cookie;
        capture.set_cookie = NULL;
        out->location = capture.location;
        capture.location = NULL;

        WF_LOG_DEBUG("xrpc", "HTTP response %ld for %s", http_status, url);

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
    free(capture.set_cookie);
    free(capture.location);
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
static wf_status wf_xrpc_build_headers(const struct wf_client_config *cfg,
                                       int is_post, const char *content_type,
                                       struct curl_slist **out_headers) {
    struct curl_slist *headers = NULL;

    if (cfg->auth_header) {
        headers = curl_slist_append(headers, cfg->auth_header);
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

/* Shared request path for both query (GET) and POST variants.
 * Thread-safe: creates a config snapshot under the client mutex before
 * issuing the request, so wf_xrpc_client_set_auth called from another
 * thread cannot tear down strings mid-flight. */
static void wf_xrpc_update_last_error(wf_xrpc_client *client,
                                      const wf_response *out, wf_status status);
static wf_status wf_xrpc_request(wf_xrpc_client *client, const char *nsid,
                                 const char *query_string, const void *body,
                                 size_t body_len, const char *content_type,
                                 int is_post, wf_response *out) {
    if (!client || !nsid || !out || (body_len > 0 && !body) ||
        (is_post && !content_type)) {
        return WF_ERR_INVALID_ARG;
    }

    struct wf_client_config *cfg = wf_client_snapshot(client);
    if (!cfg) return WF_ERR_ALLOC;

    /* Whether this call went out authenticated — only such calls are eligible
     * for a refresh+retry on an expired token. */
    int had_auth = cfg->auth_header != NULL;
    wf_status status = WF_OK;
    char *url = NULL;

    for (int attempt = 0;; attempt++) {
        /* Rebuild the URL each attempt: a refresh may re-point the client at a
         * newly discovered PDS base URL; re-snapshot for the retry. */
        free(url);
        size_t url_cap = strlen(cfg->base_url) + strlen("/xrpc/") +
                         strlen(nsid) + 1 +
                         (query_string ? strlen(query_string) + 1 : 0);
        url = malloc(url_cap);
        if (!url) {
            status = WF_ERR_ALLOC;
            break;
        }
        if (query_string && query_string[0] != '\0') {
            snprintf(url, url_cap, "%s/xrpc/%s?%s", cfg->base_url, nsid,
                     query_string);
        } else {
            snprintf(url, url_cap, "%s/xrpc/%s", cfg->base_url, nsid);
        }

        WF_LOG_DEBUG("xrpc", "XRPC %s %s", is_post ? "POST" : "GET", url);

        struct curl_slist *headers = NULL;
        status = wf_xrpc_build_headers(cfg, is_post, content_type, &headers);
        if (status != WF_OK) {
            break;
        }

        status =
            wf_xrpc_perform_cfg(cfg, is_post ? "POST" : "GET", url,
                                content_type, body, body_len, headers, out);

        /* Retry at most once, and only when an authenticated request came back
         * with an expired/invalid token and a refresh path is available. The
         * re-entrancy guard prevents a refresh call (which itself issues XRPC)
         * from recursively triggering another refresh. */
        if (attempt == 0 && had_auth && wf_xrpc_response_is_expired(out)) {
            wf_xrpc_refresh_fn refresh_cb = NULL;
            void *refresh_userdata = NULL;
            int refreshing = 0;
            pthread_mutex_lock(&client->mutex);
            refresh_cb = client->refresh_cb;
            refresh_userdata = client->refresh_userdata;
            refreshing = client->refreshing;
            pthread_mutex_unlock(&client->mutex);

            if (refresh_cb && !refreshing) {
                pthread_mutex_lock(&client->mutex);
                client->refreshing = 1;
                pthread_mutex_unlock(&client->mutex);

                wf_status refreshed = refresh_cb(refresh_userdata);

                pthread_mutex_lock(&client->mutex);
                client->refreshing = 0;
                pthread_mutex_unlock(&client->mutex);

                if (refreshed == WF_OK) {
                    wf_response_free(out);
                    wf_config_free(cfg);
                    cfg = wf_client_snapshot(client);
                    if (!cfg) {
                        status = WF_ERR_ALLOC;
                        break;
                    }
                    had_auth = cfg->auth_header != NULL;
                    free(url);
                    url = NULL;
                    continue; /* re-issue once with the refreshed credentials */
                }
            }
        }
        break;
    }

    wf_xrpc_update_last_error(client, out, status);

    free(url);
    wf_config_free(cfg);
    return status;
}

/* Update last_error from a response, under the client mutex. */
static void wf_xrpc_update_last_error(wf_xrpc_client *client,
                                      const wf_response *out,
                                      wf_status status) {
    pthread_mutex_lock(&client->mutex);
    free(client->last_error);
    client->last_error = NULL;
    if (status == WF_ERR_HTTP) {
        wf_xrpc_set_error_str(&client->last_error, out);
    }
    pthread_mutex_unlock(&client->mutex);
}

wf_status wf_xrpc_query(wf_xrpc_client *client, const char *nsid,
                        const char *query_string, wf_response *out) {
    return wf_xrpc_request(client, nsid, query_string, NULL, 0, NULL, 0, out);
}

static wf_status wf_xrpc_encode_params(const wf_xrpc_param *params,
                                       size_t param_count, char **out_query) {
    if (!out_query || (param_count > 0 && !params)) return WF_ERR_INVALID_ARG;
    *out_query = NULL;
    if (param_count == 0) return WF_OK;

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

    char *query = status == WF_OK ? malloc(query_len) : NULL;
    if (status == WF_OK && !query) status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        size_t offset = 0;
        for (size_t i = 0; i < param_count; i++) {
            int written =
                snprintf(query + offset, query_len - offset, "%s%s=%s",
                         i ? "&" : "", names[i], values[i]);
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
    if (status == WF_OK)
        *out_query = query;
    else
        free(query);
    return status;
}

wf_status wf_xrpc_query_params(wf_xrpc_client *client, const char *nsid,
                               const wf_xrpc_param *params, size_t param_count,
                               wf_response *out) {
    if (!client || !nsid || !out || (param_count > 0 && !params)) {
        return WF_ERR_INVALID_ARG;
    }
    if (param_count == 0) {
        return wf_xrpc_query(client, nsid, NULL, out);
    }

    char *query = NULL;
    wf_status status = wf_xrpc_encode_params(params, param_count, &query);
    if (status == WF_OK) {
        status = wf_xrpc_query(client, nsid, query, out);
    }
    free(query);
    return status;
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                            const char *json_body, wf_response *out) {
    size_t body_len;

    if (!client || !nsid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    body_len = json_body ? strlen(json_body) : 0;
    return wf_xrpc_request(client, nsid, NULL, json_body, body_len,
                           "application/json", 1, out);
}

wf_status wf_xrpc_upload_blob(wf_xrpc_client *client, const char *nsid,
                              const void *data, size_t data_len,
                              const char *content_type, wf_response *out) {
    if (!client || !nsid || !data || data_len == 0 || !content_type ||
        !*content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_xrpc_upload_blob_with_headers(client, nsid, data, data_len,
                                            content_type, NULL, 0, out);
}

static wf_status wf_xrpc_upload_blob_query_with_headers(
    wf_xrpc_client *client, const char *nsid, const char *query_string,
    const void *data, size_t data_len, const char *content_type,
    const wf_http_header *headers, size_t header_count, wf_response *out);

wf_status wf_xrpc_upload_blob_params(wf_xrpc_client *client, const char *nsid,
                                     const wf_xrpc_param *params,
                                     size_t param_count, const void *data,
                                     size_t data_len, const char *content_type,
                                     wf_response *out) {
    if (!client || !nsid || !out || (param_count > 0 && !params))
        return WF_ERR_INVALID_ARG;
    char *query = NULL;
    wf_status status = wf_xrpc_encode_params(params, param_count, &query);
    if (status == WF_OK) {
        status = wf_xrpc_upload_blob_query_with_headers(
            client, nsid, query, data, data_len, content_type, NULL, 0, out);
    }
    free(query);
    return status;
}

wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data, size_t data_len,
    const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out) {
    return wf_xrpc_upload_blob_query_with_headers(client, nsid, NULL, data,
                                                  data_len, content_type,
                                                  headers, header_count, out);
}

static wf_status wf_xrpc_upload_blob_query_with_headers(
    wf_xrpc_client *client, const char *nsid, const char *query_string,
    const void *data, size_t data_len, const char *content_type,
    const wf_http_header *headers, size_t header_count, wf_response *out) {
    struct curl_slist *list = NULL;
    wf_status status = WF_OK;
    char *url = NULL;

    if (!client || !nsid || !data || data_len == 0 || !content_type ||
        !*content_type || !out || (header_count && !headers)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    struct wf_client_config *cfg = wf_client_snapshot(client);
    if (!cfg) return WF_ERR_ALLOC;

    size_t url_cap =
        strlen(cfg->base_url) + strlen("/xrpc/") + strlen(nsid) + 1 +
        (query_string && query_string[0] ? strlen(query_string) + 1 : 0);
    url = malloc(url_cap);
    if (!url) {
        wf_config_free(cfg);
        return WF_ERR_ALLOC;
    }
    if (query_string && query_string[0])
        snprintf(url, url_cap, "%s/xrpc/%s?%s", cfg->base_url, nsid,
                 query_string);
    else
        snprintf(url, url_cap, "%s/xrpc/%s", cfg->base_url, nsid);

    if (cfg->auth_header) {
        list = curl_slist_append(list, cfg->auth_header);
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
        status = wf_xrpc_perform_cfg(cfg, "POST", url, content_type, data,
                                     data_len, list, out);
    } else {
        curl_slist_free_all(list);
    }
    wf_xrpc_update_last_error(client, out, status);
    free(url);
    wf_config_free(cfg);
    return status;
}

void wf_response_free(wf_response *res) {
    if (!res) return;
    free(res->body);
    free(res->dpop_nonce);
    free(res->set_cookie);
    free(res->location);
    res->body = NULL;
    res->body_len = 0;
    res->dpop_nonce = NULL;
    res->set_cookie = NULL;
    res->location = NULL;
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

wf_status wf_xrpc_error(const wf_response *resp, char **out_error,
                        char **out_message) {
    if (out_error) *out_error = NULL;
    if (out_message) *out_message = NULL;
    if (!resp) return WF_ERR_INVALID_ARG;

    cJSON *root =
        cJSON_ParseWithLength(resp->body ? resp->body : "", resp->body_len);
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
    if (!e) {
        free(m);
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    if (out_error)
        *out_error = e;
    else
        free(e);
    if (out_message)
        *out_message = m;
    else
        free(m);
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
        (extra_count && !extra))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    struct wf_client_config *cfg = wf_client_snapshot(client);
    if (!cfg) return WF_ERR_ALLOC;

    {
        size_t n = strlen("Content-Type: ") + strlen(content_type) + 1;
        char *line = malloc(n);
        if (!line) {
            wf_config_free(cfg);
            return WF_ERR_ALLOC;
        }
        snprintf(line, n, "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, line);
        free(line);
        if (!headers) {
            wf_config_free(cfg);
            return WF_ERR_ALLOC;
        }
    }
    /* If the caller supplied an Authorization header explicitly (e.g. admin
     * Basic auth overriding a cached Bearer token), skip the client's own
     * auth_header to avoid sending two Authorization headers, which would
     * cause the server to use whichever MHD returns first. */
    bool has_explicit_auth = false;
    for (i = 0; i < extra_count; i++) {
        if (extra[i].name && strcmp(extra[i].name, "Authorization") == 0) {
            has_explicit_auth = true;
            break;
        }
    }
    if (cfg->auth_header && !has_explicit_auth) {
        headers = curl_slist_append(headers, cfg->auth_header);
        if (!headers) {
            curl_slist_free_all(headers);
            wf_config_free(cfg);
            return WF_ERR_ALLOC;
        }
    }
    for (i = 0; status == WF_OK && i < extra_count; i++) {
        size_t n;
        char *line;
        struct curl_slist *grown;
        if (!extra[i].name || !extra[i].value) {
            status = WF_ERR_INVALID_ARG;
            break;
        }
        n = strlen(extra[i].name) + strlen(extra[i].value) + 3;
        line = malloc(n);
        if (!line) {
            status = WF_ERR_ALLOC;
            break;
        }
        snprintf(line, n, "%s: %s", extra[i].name, extra[i].value);
        grown = curl_slist_append(headers, line);
        free(line);
        if (!grown) {
            status = WF_ERR_ALLOC;
            break;
        }
        headers = grown;
    }
    if (status == WF_OK) {
        status = wf_xrpc_perform_cfg(cfg, "POST", url, content_type, body,
                                     strlen(body), headers, out);
    } else {
        curl_slist_free_all(headers);
    }
    wf_xrpc_update_last_error(client, out, status);
    wf_config_free(cfg);
    return status;
}

/**
 * Returns a copy of the client's base URL.
 * The caller owns the returned string and must free it.
 */
char *wf_xrpc_get_base_url(wf_xrpc_client *client) {
    if (!client) return NULL;
    pthread_mutex_lock(&client->mutex);
    char *url = client->base_url ? strdup(client->base_url) : NULL;
    pthread_mutex_unlock(&client->mutex);
    return url;
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
                                   const wf_http_header *extra,
                                   size_t extra_count, wf_response *out) {
    struct curl_slist *headers = NULL;
    wf_status status = WF_OK;
    size_t i;

    if (!client || !url || !out || (extra_count && !extra)) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    struct wf_client_config *cfg = wf_client_snapshot(client);
    if (!cfg) return WF_ERR_ALLOC;

    if (cfg->auth_header) {
        headers = curl_slist_append(headers, cfg->auth_header);
    }

    for (i = 0; status == WF_OK && i < extra_count; i++) {
        size_t n;
        char *line;
        struct curl_slist *grown;
        if (!extra[i].name || !extra[i].value) {
            status = WF_ERR_INVALID_ARG;
            break;
        }
        n = strlen(extra[i].name) + strlen(extra[i].value) + 3;
        line = malloc(n);
        if (!line) {
            status = WF_ERR_ALLOC;
            break;
        }
        snprintf(line, n, "%s: %s", extra[i].name, extra[i].value);
        grown = curl_slist_append(headers, line);
        free(line);
        if (!grown) {
            status = WF_ERR_ALLOC;
            break;
        }
        headers = grown;
    }

    if (status == WF_OK) {
        status =
            wf_xrpc_perform_cfg(cfg, "GET", url, NULL, NULL, 0, headers, out);
    } else {
        curl_slist_free_all(headers);
    }
    wf_xrpc_update_last_error(client, out, status);
    wf_config_free(cfg);
    return status;
}

/**
 * Perform a generic HTTP GET to an arbitrary URL (not an XRPC endpoint).
 *
 * Uses the client's transport and auth settings. `url` must be a complete
 * absolute URL. On WF_OK, `out` is populated and must be released with
 * wf_response_free.
 */
wf_status wf_http_get(wf_xrpc_client *client, const char *url,
                      wf_response *out) {
    return wf_http_get_with_headers(client, url, NULL, 0, out);
}

/* ── Async API ──────────────────────────────────────────────────────── */

/*
 * Worker thread: takes a config snapshot and request parameters from the
 * pending handle (all owned copies), performs the request, and stores the
 * result. Never touches the client after the snapshot is taken.
 *
 * Auto-refresh is not supported on the async path — callers that need it
 * should handle it explicitly via wf_xrpc_client_set_auth before re-submitting.
 */
static void *wf_async_worker(void *arg) {
    struct wf_xrpc_pending *p = (struct wf_xrpc_pending *)arg;

    pthread_mutex_lock(&p->mutex);
    if (p->cancelled) {
        p->done = true;
        p->status = WF_ERR_STATE;
        pthread_cond_broadcast(&p->cond);
        pthread_mutex_unlock(&p->mutex);
        return NULL;
    }
    pthread_mutex_unlock(&p->mutex);

    struct wf_client_config *cfg = p->config;
    if (!cfg || !cfg->base_url) {
        pthread_mutex_lock(&p->mutex);
        p->done = true;
        p->status = WF_ERR_STATE;
        pthread_cond_broadcast(&p->cond);
        pthread_mutex_unlock(&p->mutex);
        return NULL;
    }

    /* Build the full XRPC URL. */
    size_t url_cap = strlen(cfg->base_url) + strlen("/xrpc/") +
                     strlen(p->nsid) + 1 +
                     (p->query_string ? strlen(p->query_string) + 1 : 0);
    char *url = malloc(url_cap);
    if (!url) {
        pthread_mutex_lock(&p->mutex);
        p->done = true;
        p->status = WF_ERR_ALLOC;
        pthread_cond_broadcast(&p->cond);
        pthread_mutex_unlock(&p->mutex);
        return NULL;
    }
    if (p->query_string && p->query_string[0] != '\0') {
        snprintf(url, url_cap, "%s/xrpc/%s?%s", cfg->base_url, p->nsid,
                 p->query_string);
    } else {
        snprintf(url, url_cap, "%s/xrpc/%s", cfg->base_url, p->nsid);
    }

    WF_LOG_DEBUG("xrpc", "async XRPC %s %s", p->is_post ? "POST" : "GET", url);

    struct curl_slist *headers = NULL;
    wf_status status =
        wf_xrpc_build_headers(cfg, p->is_post, p->content_type, &headers);

    if (status == WF_OK) {
        memset(&p->result, 0, sizeof(p->result));
        status = wf_xrpc_perform_cfg(cfg, p->is_post ? "POST" : "GET", url,
                                     p->content_type, p->body, p->body_len,
                                     headers, &p->result);
    } else if (headers) {
        curl_slist_free_all(headers);
    }

    free(url);

    pthread_mutex_lock(&p->mutex);
    p->status = status;
    p->done = true;
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);

    return NULL;
}

/*
 * Shared helper: create a pending handle from a config snapshot + request
 * parameters, link it into the client, and spawn the worker thread.
 */
static wf_status wf_xrpc_async_submit(wf_xrpc_client *client, const char *nsid,
                                      const char *query_string,
                                      const void *body, size_t body_len,
                                      const char *content_type, int is_post,
                                      wf_xrpc_pending **out) {
    if (!client || !nsid || !out) {
        return WF_ERR_INVALID_ARG;
    }

    struct wf_client_config *cfg = wf_client_snapshot(client);
    if (!cfg) return WF_ERR_ALLOC;

    char *nsid_copy = strdup(nsid);
    char *query_copy = query_string ? strdup(query_string) : NULL;
    char *body_copy = NULL;
    char *content_type_copy = NULL;

    if (body_len > 0) {
        body_copy = malloc(body_len);
        if (!body_copy) {
            free(nsid_copy);
            free(query_copy);
            wf_config_free(cfg);
            return WF_ERR_ALLOC;
        }
        memcpy(body_copy, body, body_len);
    }
    if (content_type) {
        content_type_copy = strdup(content_type);
        if (!content_type_copy) {
            free(nsid_copy);
            free(query_copy);
            free(body_copy);
            wf_config_free(cfg);
            return WF_ERR_ALLOC;
        }
    }

    struct wf_xrpc_pending *p = (struct wf_xrpc_pending *)calloc(1, sizeof(*p));
    if (!p) {
        free(nsid_copy);
        free(query_copy);
        free(body_copy);
        free(content_type_copy);
        wf_config_free(cfg);
        return WF_ERR_ALLOC;
    }

    p->config = cfg;
    p->client = client;
    p->nsid = nsid_copy;
    p->query_string = query_copy;
    p->body = body_copy;
    p->body_len = body_len;
    p->content_type = content_type_copy;
    p->is_post = is_post;
    p->status = WF_OK;
    p->started = false;
    p->done = false;
    p->cancelled = false;
    if (pthread_mutex_init(&p->mutex, NULL) != 0 ||
        pthread_cond_init(&p->cond, NULL) != 0) {
        pthread_mutex_destroy(&p->mutex);
        pthread_cond_destroy(&p->cond);
        free(nsid_copy);
        free(query_copy);
        free(body_copy);
        free(content_type_copy);
        wf_config_free(cfg);
        free(p);
        return WF_ERR_ALLOC;
    }

    if (pthread_create(&p->thread, NULL, wf_async_worker, p) != 0) {
        pthread_mutex_destroy(&p->mutex);
        pthread_cond_destroy(&p->cond);
        free(nsid_copy);
        free(query_copy);
        free(body_copy);
        free(content_type_copy);
        wf_config_free(cfg);
        free(p);
        return WF_ERR_ALLOC;
    }
    p->started = true;
    pthread_mutex_lock(&client->mutex);
    p->next = client->pending;
    client->pending = p;
    pthread_mutex_unlock(&client->mutex);
    *out = p;
    return WF_OK;
}

wf_status wf_xrpc_query_async(wf_xrpc_client *client, const char *nsid,
                              const char *query_string, wf_xrpc_pending **out) {
    return wf_xrpc_async_submit(client, nsid, query_string, NULL, 0, NULL, 0,
                                out);
}

wf_status wf_xrpc_procedure_async(wf_xrpc_client *client, const char *nsid,
                                  const char *json_body,
                                  wf_xrpc_pending **out) {
    size_t body_len = json_body ? strlen(json_body) : 0;
    return wf_xrpc_async_submit(client, nsid, NULL, json_body, body_len,
                                "application/json", 1, out);
}

wf_status wf_xrpc_pending_wait(wf_xrpc_pending *p) {
    if (!p) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&p->mutex);
    while (!p->done) {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    wf_status s = p->status;
    pthread_mutex_unlock(&p->mutex);
    return s;
}

wf_status wf_xrpc_pending_result(wf_xrpc_pending *p, wf_response *out) {
    if (!p || !out) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&p->mutex);
    if (!p->done) {
        pthread_mutex_unlock(&p->mutex);
        return WF_ERR_WOULD_BLOCK;
    }
    *out = p->result;
    /* Clear the stored result so wf_xrpc_pending_free doesn't double-free. */
    memset(&p->result, 0, sizeof(p->result));
    pthread_mutex_unlock(&p->mutex);
    return p->status;
}

void wf_xrpc_pending_cancel(wf_xrpc_pending *p) {
    if (!p) return;
    pthread_mutex_lock(&p->mutex);
    p->cancelled = true;
    pthread_mutex_unlock(&p->mutex);
}

void wf_xrpc_pending_free(wf_xrpc_pending *p) {
    if (!p) return;
    /* If the worker hasn't been started or is still running, wait for it
     * to complete. The worker only touches p's own fields (under p's mutex),
     * so this is race-free. */
    pthread_mutex_lock(&p->mutex);
    while (p->started && !p->done) {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    pthread_mutex_unlock(&p->mutex);

    if (p->started) {
        pthread_join(p->thread, NULL);
    }

    /* Unlink from the client's pending list (if the client still exists).
     * The client may have already been freed; in that case client is NULL. */
    if (p->client) {
        pthread_mutex_lock(&p->client->mutex);
        for (struct wf_xrpc_pending **pp = &p->client->pending; *pp;
             pp = &(*pp)->next) {
            if (*pp == p) {
                *pp = p->next;
                break;
            }
        }
        pthread_mutex_unlock(&p->client->mutex);
    }

    pthread_mutex_destroy(&p->mutex);
    pthread_cond_destroy(&p->cond);
    free(p->nsid);
    free(p->query_string);
    free(p->content_type);
    free(p->body);
    wf_response_free(&p->result);
    free(p->error_msg);
    wf_config_free(p->config);
    free(p);
}
